#include "adamnet.h"
#include "coleco.h"
#include <cstdio>
#include <QDebug>
#include <QtConcurrentRun>
#include <QFuture>
#include <cstring>




bool isFormatting = false;
static const byte interleave[8] = { 0, 5, 2, 7, 4, 1, 6, 3 };

// ============================================================================
// CP/M-only transfer state (prevents buffer/length races across busy polls)
// NOTE: EOS/T-DOS paths are untouched; only CP/M uses this.
// ============================================================================
struct CPM_XferState {
    byte active_id = 0xFF;   // disk/tape index currently active
    byte dev = 0xFF;         // device number
    bool pending_read = false;
    word buf = 0;
    int len = 0;
};

static CPM_XferState g_cpm_dsk;
static CPM_XferState g_cpm_tap;

static inline void FlushHoldingBufToRAM_CPM(const CPM_XferState& st)
{
    word dst = st.buf;
    int n = st.len;
    if (n <= 0) return;
    if (n > 0x0400) n = 0x0400;
    for (int i = 0; i < n; ++i)
        RAM_Memory[(word)(dst + i)] = HoldingBuf[i];
}

void AdamFlushCache_CPM()
{
    // Backward-compatible wrapper: flush whichever CP/M transfer is pending.
    if (g_cpm_dsk.pending_read) {
        FlushHoldingBufToRAM_CPM(g_cpm_dsk);
        return;
    }
    if (g_cpm_tap.pending_read) {
        FlushHoldingBufToRAM_CPM(g_cpm_tap);
        return;
    }
}

void UpdateDSK_CPM(byte N, byte Dev, int V)
{
    int I, LEN, SEC;
    int byte_idx;
    int K;
    byte *Data;
    uint32_t block;

    if (N >= MAX_DISKS) {
        return;
    }

    // ========================================================================
    // POLLING / BUSY CHECK
    // ========================================================================
    if (V < 0) {
        static int poll_count = 0;
        if (g_cpm_dsk.active_id == N && ++poll_count % 1000 == 0) {
            qDebug() << "[DISK-POLL] N=" << N << "Dev=" << Dev
                     << "io_busy=" << io_busy
                     << "active=" << (g_cpm_dsk.active_id == N ? "YES" : "NO");
        }

        // Alleen deze disk pollen als het de actieve is
        if (g_cpm_dsk.active_id == N && io_busy > 0) {
            io_busy--;
            if (io_busy == 0) {
                // IO done for CP/M disk
                if (g_cpm_dsk.pending_read) {
                    FlushHoldingBufToRAM_CPM(g_cpm_dsk);
                    g_cpm_dsk.pending_read = false;
                }
                g_cpm_dsk.active_id = 0xFF;
                g_cpm_dsk.dev = 0xFF;
                g_cpm_dsk.buf = 0;
                g_cpm_dsk.len = 0;

                SetDCB(Dev, DCB_CMD_STAT, RSP_STATUS);
                qDebug() << "[DISK-POLL] IO DONE - N=" << N << "Dev=" << Dev;
            }
            else {
                SetDCB(Dev, DCB_CMD_STAT, 0x00);
            }
        }
        return;
    }

    // if (V < 0) {
    //     if (io_busy > 0) {
    //         io_busy--;
    //         if (io_busy == 0) {
    //             if (last_command_read) {
    //                 last_command_read = 0;
    //                 AdamFlushCache_CPM();
    //             }

    //             // 1. Update de standaard DCB (voor de vorm)
    //             SetDCB(Dev, DCB_CMD_STAT, RSP_STATUS);

    //             // 2. --- DE AUTOMATISCHE DOORBRAAK ---
    //             // We kijken direct op de drie adressen die je hebt gevonden.
    //             // Als de BIOS daar op 'Busy' (01) staat, zetten we hem op 'Ready' (80).
    //             /*
    //             if (RAM_Memory[0xFED9] == 0x01) {
    //                 RAM_Memory[0xFED9] = 0x80;
    //                 qDebug() << "[AUTO-SYNC] BIOS loop op $FED9 doorbroken!";
    //             }

    //             if (RAM_Memory[0xFEEE] == 0x01) {
    //                 RAM_Memory[0xFEEE] = 0x80;
    //                 qDebug() << "[AUTO-SYNC] BIOS loop op $FEEE doorbroken!";
    //             }

    //             if (RAM_Memory[0xFF03] == 0x01) {
    //                 RAM_Memory[0xFF03] = 0x80;
    //                 qDebug() << "[AUTO-SYNC] BIOS loop op $FF03 doorbroken!";
    //             }
    //             */
    //         }
    //         else {
    //                     SetDCB(Dev, DCB_CMD_STAT, RSP_STATUS);
    //         //            RAM_Memory[0xFED9] = 0xC6; // bypass hier wacht hij eerst
    //              }
    //     }
    //     return;
    // }

    // ========================================================================
    // COMMANDO VERWERKING
    // ========================================================================
    switch (V) {
    case CMD_STATUS:
        io_busy = 0;  // ← Ook hier
        ReportDevice(Dev, 0x0400, 1);
        // 2. Zet de status op 'Succes / Ready' (0x80)
        SetDCB(Dev, DCB_CMD_STAT, RSP_STATUS);
        break;

    case CMD_SOFT_RESET:
        io_busy = 0;  // ← Ook hier
        // CP/M-only: reset any in-flight transfer bookkeeping
        g_cpm_dsk.active_id = 0xFF;
        g_cpm_dsk.dev = 0xFF;
        g_cpm_dsk.pending_read = false;
        g_cpm_dsk.buf = 0;
        g_cpm_dsk.len = 0;
        SetDCB(Dev, DCB_CMD_STAT, RSP_STATUS);
        break;

    case CMD_READ:
    case CMD_WRITE:
        qDebug() << "[DSK-CMD]" << (V == CMD_READ ? "READ" : "WRITE")
                 << "N=" << N << "Dev=" << Dev
                 << "Sector=" << GetDCBSector(Dev)
                 << "Setting active_disk=" << N << "io_busy=" << (DELAY_IO * 100);

        g_diskSoundActive.store(true, std::memory_order_relaxed);
        io_show_status = (V == CMD_READ) ? 1 : 2;

        // If a CP/M disk operation is already in-flight, keep this request BUSY.
        // This prevents saved buffer/len from being overwritten before the poll-time flush.
        if (io_busy > 0 && g_cpm_dsk.active_id != 0xFF) {
            SetDCB(Dev, DCB_CMD_STAT, 0x00);
            return;
        }

        SetDCB(Dev, DCB_CMD_STAT, 0x00);
        g_cpm_dsk.active_id = N;
        g_cpm_dsk.dev = Dev;
        g_cpm_dsk.pending_read = false;
        g_cpm_dsk.buf = 0;
        g_cpm_dsk.len = 0;
        io_busy = DELAY_IO * 100;  // busy counter

        if (!Disks[N].Data) {
            qDebug() << "[DSK] No disk data! N=" << N;
            io_busy = 0;
            g_cpm_dsk.active_id = 0xFF;
            g_cpm_dsk.dev = 0xFF;
            g_cpm_dsk.pending_read = false;
            SetDCB(Dev, DCB_CMD_STAT, RSP_ACK + 0x06); // no media / I/O error
            return;
        }

        word buf = GetDCBBase(Dev);
        LEN = GetDCBLen(Dev);
        LEN = (LEN < 0x0400) ? LEN : 0x0400;
        SEC = GetDCBSector(Dev);

        // qDebug() << "[DSK]" << (V == CMD_READ ? "READ" : "WRITE")
        //          << "Dev=" << Dev
        //          << "Sector=" << SEC
        //          << "Len=" << LEN
        //          << "Buf=" << QString("0x%1").arg(BUF, 4, 16, QChar('0'));

        // store for poll-time flush (CP/M-only)
        g_cpm_dsk.buf = buf;
        g_cpm_dsk.len = LEN;
        block = SEC * 2;

        bool ok = true;
        for (I = 0; I < LEN; I += 512)
        {

            // NOTE: only apply 8-sector interleave if the mounted image actually uses 8 sectors/track.
            uint32_t physical_sector = block;
            if (Disks[N].Sectors == 8)
                physical_sector = (block & (~7u)) | interleave[block & 7u];

            Data = LinearFDI(&Disks[N], physical_sector);
            if (!Data) { ok = false; break; }
            K = (I + 512 > LEN) ? LEN - I : 512;

            if (V == CMD_READ)
            {
                for (byte_idx = 0; byte_idx < K; ++byte_idx, ++buf) {
                    HoldingBuf[I + byte_idx] = Data[byte_idx];
                }
                // LOGGING VOOR DE OPDRACHT:
                // QString hex;
                // for(int b=0; b<16 && b<K; b++) hex += QString("%1 ").arg(HoldingBuf[I+b], 2, 16, QChar('0'));
                // qDebug() << "[FINAL-CHECK] Block" << SEC << "Sector" << block << "Phys" << physical_sector << "Data:" << hex;
            }
            else {
                for (byte_idx = 0; byte_idx < K; ++byte_idx, ++buf)
                    Data[byte_idx] = RAM_Memory[buf & 0xFFFF];
                Disks[N].Dirty = 1;
            }
            block++;
        }

        if (!ok) {
            // Fail safely: don't flush partial data into RAM, report error.
            g_cpm_dsk.pending_read = false;
            io_busy = 0;
            g_cpm_dsk.active_id = 0xFF;
            g_cpm_dsk.dev = 0xFF;
            SetDCB(Dev, DCB_CMD_STAT, RSP_ACK + 0x06);
            return;
        }

        if (V == CMD_READ)
            g_cpm_dsk.pending_read = true;

        break;
    }
}

void UpdateTAP_CPM(byte N,byte Dev,int V)
{
    int I,J,K,LEN,SEC;
    word BUF;
    byte *Data;

    /* If reading DCB status, stop here */
    if (V < 0)
    {
        if (g_cpm_tap.active_id == N && io_busy > 0)
        {
            io_busy--;
            if (io_busy == 0)
            {
                if (g_cpm_tap.pending_read) {
                    FlushHoldingBufToRAM_CPM(g_cpm_tap);
                    g_cpm_tap.pending_read = false;
                }
                g_cpm_tap.active_id = 0xFF;
                g_cpm_tap.dev = 0xFF;
                g_cpm_tap.buf = 0;
                g_cpm_tap.len = 0;
                SetDCB(Dev, DCB_CMD_STAT, RSP_STATUS);
            }
            else
            {
                SetDCB(Dev, DCB_CMD_STAT, 0x00);
            }
        }
        return;
    }

    /* Reset errors, report missing tapes */
    SetDCB(Dev,DCB_NODE_TYPE,(Tapes[N&2].Data? 0x00:0x03)|(Tapes[(N&2)+1].Data? 0x00:0x30));

    /* Depending on the command... */
    switch(V)
    {
    case CMD_STATUS:
        /* Block-based device, 1kB buffer */
        ReportDevice(Dev,0x0400,1);
        break;

    case CMD_SOFT_RESET:
        io_busy = 0;
        g_cpm_tap.active_id = 0xFF;
        g_cpm_tap.dev = 0xFF;
        g_cpm_tap.pending_read = false;
        g_cpm_tap.buf = 0;
        g_cpm_tap.len = 0;
        SetDCB(Dev,DCB_CMD_STAT,RSP_STATUS);
        break;

    case CMD_WRITE:
    case CMD_READ:
        g_tapeSoundActive.store(true, std::memory_order_relaxed);
        io_show_status = (V==CMD_READ) ? 1:2;
        // TODO if (io_show_status == 2) adam_unsaved_data = 1;
        /* Busy status by default */
        // If a CP/M tape operation is already in-flight, keep this request BUSY.
        if (io_busy > 0 && g_cpm_tap.active_id != 0xFF) {
            SetDCB(Dev, DCB_CMD_STAT, 0x00);
            return;
        }

        SetDCB(Dev,DCB_CMD_STAT,0x00);
        g_cpm_tap.active_id = N;
        g_cpm_tap.dev = Dev;
        g_cpm_tap.pending_read = false;
        g_cpm_tap.buf = 0;
        g_cpm_tap.len = 0;
        io_busy = DELAY_IO;
        /* If no tape, stop here */
        if(!Tapes[N].Data) {
            io_busy = 0;
            g_cpm_tap.active_id = 0xFF;
            g_cpm_tap.dev = 0xFF;
            SetDCB(Dev, DCB_CMD_STAT, RSP_ACK + 0x06);
            return;
        }
        /* Determine buffer address, length, block number */
        BUF = GetDCBBase(Dev);
        LEN = GetDCBLen(Dev);
        LEN = LEN<0x0400? LEN:0x0400;
        SEC = GetDCBSector(Dev);
        g_cpm_tap.buf = BUF;
        g_cpm_tap.len = LEN;

        /* For each 512-byte sector... */
        {
        bool ok = true;
        for(I=0, SEC<<=1 ; I<LEN ; ++SEC, I+=0x200)
        {
            /* Get pointer to sector data on tape */
            Data = LinearFDI(&Tapes[N],SEC);
            /* If wrong sector number, stop here */
            if(!Data)
            {
                ok = false;
                SetDCB(Dev,DCB_NODE_TYPE,GetDCB(Dev,DCB_NODE_TYPE)|0x02);
                SetDCB(Dev,DCB_SEC_LO, 0);
                SetDCB(Dev,DCB_SEC_HI, 0);
                break;
            }
            /* Read or write sectors */
            K = I+0x200>LEN? LEN-I:0x200;
            if(V==CMD_READ)
            {
                last_command_read = true;
                for(J=0;J<K;++J,++BUF)
                {
                    HoldingBuf[I+J] = Data[J];
                }
            }
            else
            {
                last_command_read = false;
                for(J=0;J<K;++J,++BUF) Data[J] = RAM(BUF);
                Tapes[N].Dirty = 1;
            }
            /* If disk access failed, stop here */
            if(J<K)
            {
                SetDCB(Dev,DCB_NODE_TYPE,GetDCB(Dev,DCB_NODE_TYPE)|0x06);
                SetDCB(Dev,DCB_SEC_LO, 0);
                SetDCB(Dev,DCB_SEC_HI, 0);
                break;
            }
        }
        if (!ok) {
            g_cpm_tap.pending_read = false;
            io_busy = 0;
            g_cpm_tap.active_id = 0xFF;
            g_cpm_tap.dev = 0xFF;
            SetDCB(Dev, DCB_CMD_STAT, RSP_ACK + 0x06);
            return;
        }

        if (V == CMD_READ)
            g_cpm_tap.pending_read = true;
        }
        /* Done */
        break;
    }
}

void UpdateDCB_CPM(byte Dev, int V)
{

    byte DevID;

    if(!V) return;

    DevID = (GetDCB(Dev,DCB_DEV_NUM)<<4) + (GetDCB(Dev,DCB_ADD_CODE)&0x0F);


    switch(DevID)
    {
    case 0x01: UpdateKBD(Dev,V);break;
    case 0x02: UpdatePRN(Dev,V);break;
    case 0x04:
    case 0x05:
    case 0x06:
    case 0x07: UpdateDSK_CPM(DiskID=DevID-4,Dev,V);break;
    case 0x08:
    case 0x09:
    case 0x18:
    case 0x19: UpdateTAP_CPM((DevID>>4)+((DevID&1)<<1),Dev,V);break;
    case 0x52: UpdateDSK_CPM(DiskID,Dev,-2);break;
    default:
        SetDCB(Dev,DCB_CMD_STAT,RSP_ACK+0x0B);
        break;
    }
}
