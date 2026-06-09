#include "adnet_core.h"
#include "CORE/cv.h"

#include <cstring>
#include <QDebug>
#include <QtConcurrentRun>
#include <QFuture>
#include <QThread>

/*
 * AdamNet DISK handlers
 * ---------------------
 * Alle disk-logica zit hier gegroepeerd per OS:
 *   - EOS  : UpdateDSK_EOS()
 *   - CP/M : UpdateDSK_CPM() + lokale CP/M drive/cache core
 *   - T-DOS: UpdateDSK_TDOS() + format-emulatie
 *
 * De publieke functienamen blijven identiek, zodat adnet_eos.cpp,
 * adnet_cpm.cpp en adnet_tdos.cpp gewoon kunnen blijven routeren.
 */

// -----------------------------------------------------------------------------
// CP/M local disk-drive core (vroeger in adnet_cpm.cpp / adnet_drive.cpp)
// -----------------------------------------------------------------------------
// -----------------------------------------------------------------------------
// CP/M local disk-drive core (merged from adnet_drive.cpp)
// -----------------------------------------------------------------------------
AdamDrive_t   AdamDrive[MAX_DRIVES];
DriveStatus_t AdamDriveStatus[MAX_DRIVES];
static bool s_driveInitDone = false;

static const byte s_driveTimeouts[] = { 5, 10, 15 };
static const byte s_interleaveTable[8] = { 0, 5, 2, 7, 4, 1, 6, 3 };
static const unsigned int INVALID_BLOCK = 0xFFFFFFFFu;

static inline byte clamp_timeout_index()
{
    return 1; // medium default
}

static inline bool drive_is_disk(byte drive)
{
    return drive == BAY_DISK1 || drive == BAY_DISK2;
}

static inline bool drive_present(byte drive)
{
        return Disks[drive].Data != nullptr;
}

static inline byte *drive_sector_ptr(byte drive, unsigned int sector)
{
        const unsigned int phys = AdamDrive[drive].skew ? ((sector & ~7u) | s_interleaveTable[sector & 7u]) : sector;
        return LinearFDI(&Disks[drive], phys);
}

static inline void drive_set_missing_status(byte drive, byte device)
{
        SetDCB(device, DCB_NODE_TYPE, (GetDCB(device, DCB_NODE_TYPE) & 0xF0) | (drive_present(drive) ? 0x00 : 0x03));
}

void adam_drive_local_init(void)
{
    std::memset(AdamDrive, 0, sizeof(AdamDrive));
    std::memset(AdamDriveStatus, 0, sizeof(AdamDriveStatus));

    AdamDrive[BAY_DISK1].driveType = DRIVE_TYPE_DISK;
    AdamDrive[BAY_DISK2].driveType = DRIVE_TYPE_DISK;
    AdamDrive[BAY_DISK1].skew = 1;
    AdamDrive[BAY_DISK2].skew = 1;

    for (int i = 0; i < MAX_DRIVES; ++i) {
        AdamDriveStatus[i].status = RSP_STATUS;
        AdamDriveStatus[i].newstatus = RSP_STATUS;
        AdamDriveStatus[i].timeout = 0;
        AdamDriveStatus[i].io_status = 0;
        AdamDriveStatus[i].lastblock = INVALID_BLOCK;
        AdamDriveStatus[i].saved_buf = 0;
        AdamDriveStatus[i].saved_len = 0;
        AdamDriveStatus[i].pending_read = 0;
    }
    s_driveInitDone = true;
}

void adam_drive_local_reset(void)
{
    if (!s_driveInitDone) adam_drive_local_init();
    for (int i = 0; i < MAX_DRIVES; ++i) {
        AdamDriveStatus[i].status = RSP_STATUS;
        AdamDriveStatus[i].newstatus = RSP_STATUS;
        AdamDriveStatus[i].timeout = 0;
        AdamDriveStatus[i].io_status = 0;
        AdamDriveStatus[i].lastblock = INVALID_BLOCK;
        AdamDriveStatus[i].saved_buf = 0;
        AdamDriveStatus[i].saved_len = 0;
        AdamDriveStatus[i].pending_read = 0;
    }
}

void adam_drive_local_cache_check(void)
{
    if (!s_driveInitDone) adam_drive_local_init();

    for (int i = 0; i < MAX_DRIVES; ++i) {
        if (AdamDriveStatus[i].timeout) {
            if (!--AdamDriveStatus[i].timeout) {
                AdamDriveStatus[i].pending_read = 0;
                AdamDriveStatus[i].newstatus = RSP_STATUS;
                AdamDriveStatus[i].status = RSP_STATUS;
            }
        }
    }
}

// -----------------------------------------------------------------------------
// EOS disk handler
// -----------------------------------------------------------------------------

void UpdateDSK_EOS(byte N,byte Dev,int V)
{
    static const byte InterleaveTable[8]= { 0,5,2,7,4,1,6,3 };
    int I,J,K,LEN,SEC;
    word BUF;
    byte *Data;

    /* We have limited number of disks */
    if(N>=MAX_DISKS) return;

    /* If reading DCB status, stop here */
    if(V<0)
    {
        if (io_busy)
        {
            io_busy--;
            SetDCB(Dev,DCB_CMD_STAT,0x00);

            if (io_busy == 0 && last_command_read)
            {
                last_command_read=0;
                AdamFlushCache();
            }
        }
        else
        {
            SetDCB(Dev,DCB_CMD_STAT,RSP_STATUS);
        }
        return;
    }

    /* Reset errors, report missing disks */
    SetDCB(Dev,DCB_NODE_TYPE,(GetDCB(Dev,DCB_NODE_TYPE)&0xF0) | (Disks[N].Data? 0x00:0x03));

    /* Depending on the command... */
    switch(V)
    {
    case CMD_STATUS:
        /* Block-based device, 1kB buffer */
        ReportDevice(Dev,0x0400,1);
        break;

    case CMD_SOFT_RESET:
        SetDCB(Dev,DCB_CMD_STAT,RSP_STATUS);
        break;

    case CMD_WRITE:
    case CMD_READ:
        io_show_status = (V==CMD_READ) ? 1:2;
        //TODO if (io_show_status == 2) adam_unsaved_data = 1;
        /* Busy status by default */
        SetDCB(Dev,DCB_CMD_STAT,0x00);
        io_busy = DELAY_IO;
        /* If no disk, stop here */
        if(!Disks[N].Data) break;
        /* Determine buffer address, length, block number */
        BUF = GetDCBBase(Dev);
        LEN = GetDCBLen(Dev);
        LEN = LEN<0x0400? LEN:0x0400;
        SEC = GetDCBSector(Dev);
        savedBUF = BUF;
        savedLEN = LEN;
        /* For each 512-byte sector... */
        for(I=0, SEC<<=1 ; I<LEN ; ++SEC, I+=0x200)
        {
            /* Remap sector number via interleave table */
            K = (SEC&~7) | InterleaveTable[SEC&7];
            /* Get pointer to sector data on disk */
            Data = LinearFDI(&Disks[N],K);
            /* If wrong sector number, stop here */
            if(!Data)
            {
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
                for(J=0;J<K;++J,++BUF)
                {
                    Data[J] = RAM_Memory[BUF];
                }
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
        /* Done */
        break;
    }
}


// -----------------------------------------------------------------------------
// CP/M disk handler
// -----------------------------------------------------------------------------

void UpdateDSK_CPM(byte N, byte Dev, int V)
{
    N=BAY_DISK1 + N;

    if (!s_driveInitDone) adam_drive_local_init();
    if (N >= MAX_DRIVES) return;

    DriveStatus_t &st = AdamDriveStatus[N];

    if (V < 0) {
        SetDCB(Dev, DCB_CMD_STAT, st.status);
        return;
    }

    drive_set_missing_status(N, Dev);
    V &= 0x7F;

    switch (V) {
    case CMD_RESET:
        st.lastblock = INVALID_BLOCK;
        st.timeout = 0;
        st.pending_read = 0;
        st.status = st.newstatus = RSP_STATUS;
        SetDCB(Dev, DCB_CMD_STAT, st.status);
        break;

    case CMD_STATUS:
        ReportDevice(Dev, 0x0400, 1);
        st.status = st.newstatus;
        SetDCB(Dev, DCB_CMD_STAT, st.status);
        break;

    case CMD_SOFT_RESET:
        st.timeout = 0;
        st.pending_read = 0;
        st.status = st.newstatus = RSP_STATUS;
        SetDCB(Dev, DCB_CMD_STAT, st.status);
        break;

    case CMD_WRITE:
    case CMD_READ:
    {
        if (st.io_status != 2) st.io_status = (V == CMD_READ) ? 1 : 2;
        io_show_status = st.io_status;
        if (drive_is_disk(N)) g_diskSoundActive.store(true, std::memory_order_relaxed);
        else g_tapeSoundActive.store(true, std::memory_order_relaxed);

        st.status = st.newstatus;
        if (st.status == RSP_TIMEOUT) {
            if (st.timeout == 0) {
                st.status = st.newstatus = RSP_STATUS;
            } else {
                SetDCB(Dev, DCB_CMD_STAT, st.status);
                break;
            }
        }

        if (!drive_present(N)) {
            SetDCB(Dev, DCB_NODE_TYPE, GetDCB(Dev, DCB_NODE_TYPE) | 0x06);
            st.lastblock = INVALID_BLOCK;
            st.status = st.newstatus = RSP_TIMEOUT;
            SetDCB(Dev, DCB_CMD_STAT, st.status);
            break;
        }

        word buf = GetDCBBase(Dev);
        int len = GetDCBLen(Dev);
        len = len < 0x0400 ? len : 0x0400;
        const unsigned int blk = GetDCBSector(Dev);
        unsigned int sec = blk * 2u;

        if (blk == st.lastblock || V == CMD_WRITE) {
            int copied = 0;
            for (int i = 0; i < len; ++sec, i += 0x200) {
                byte *data = drive_sector_ptr(N, sec);
                if (!data) {
                    SetDCB(Dev, DCB_NODE_TYPE, GetDCB(Dev, DCB_NODE_TYPE) | 0x02);
                    st.lastblock = INVALID_BLOCK;
                    SetDCB(Dev, DCB_SEC_LO, 0);
                    SetDCB(Dev, DCB_SEC_HI, 0);
                    copied = 0;
                    break;
                }

                const int k = (i + 0x200 > len) ? (len - i) : 0x200;
                if (V == CMD_READ) {
                    const word pcb_lo = PCBAddr;
                    const word pcb_hi = (word)(PCBAddr + PCB_SIZE + (GetMaxDCB() + 1) * DCB_SIZE);

                    for (int j = 0; j < k; ++j, ++buf) {
                        if (buf >= pcb_lo && buf < pcb_hi) continue;
                        RAM_Memory[buf] = data[j];
                    }
                } else {
                    for (int j = 0; j < k; ++j, ++buf) {
                        data[j] = RAM_Memory[buf];
                    }
                }
                copied += k;
            }

            if (V == CMD_WRITE) {
                st.lastblock = INVALID_BLOCK;
            }

            if (copied == len) {
                st.pending_read = 0;
                last_command_read = 0;
                st.status = st.newstatus = RSP_STATUS;
                SetDCB(Dev, DCB_CMD_STAT, st.status);
            } else {
                SetDCB(Dev, DCB_CMD_STAT, st.status);
            }
        }
        else {
            st.status = st.newstatus = RSP_TIMEOUT;
            st.timeout = s_driveTimeouts[clamp_timeout_index()];
            st.lastblock = blk;
            st.pending_read = 0;
            last_command_read = 0;
            SetDCB(Dev, DCB_CMD_STAT, st.status);
        }
        break;
    }


    default:
        SetDCB(Dev, DCB_CMD_STAT, RSP_ACK + 0x0B);
        break;
    }

}


// -----------------------------------------------------------------------------
// T-DOS disk handler
// -----------------------------------------------------------------------------

static bool tisFormatting = false;

void UpdateDSK_TDOS(byte N, byte Dev, int V)
{
    static const byte InterleaveTable[8] = { 0,5,2,7,4,1,6,3 };
    int I, J, K, LEN, SEC;
    word BUF;
    byte *Data;

    // 1. HARDGUARD:
    if (N >= MAX_DISKS) return;

    SEC = GetDCBSector(Dev);

    // 2. T-DOS FORMAT TRIGGER ($FACE of V == 5)
    if (SEC == 0xFACE || V == 5)
    {
        if (!tisFormatting) {
            tisFormatting = true;

            SetDCB(Dev,DCB_SEC_LO, 0x00);
            SetDCB(Dev,DCB_SEC_HI, 0x00);

            // BUSY to Z80
            SetDCB(Dev, DCB_CMD_STAT, 0x00);
            io_busy = 320;

            auto future =QtConcurrent::run([N, Dev]() {
                if (Disks[N].Data) {
                    for (int track = 0; track < 40; track++) {

                        g_diskSoundActive.store(true, std::memory_order_relaxed);

                        for (int s = 0; s < 8; s++) {
                            int sectorIdx = (track * 8) + s;
                            byte *d = LinearFDI(&Disks[N], sectorIdx);
                            if (d) std::memset(d, 0xE5, 512);
                        }

                         QThread::msleep(250);

                        io_busy = (40 - track) * 8;
                    }
                }

                io_busy = 0;
                tisFormatting = false;
                SetDCB(Dev, DCB_CMD_STAT, RSP_STATUS);
                qDebug() << "Format voltooid op realistisch tempo.";
            });
        }
        return;
    }

    // 3. POLLING / BUSY-CHECK (V < 0)
    if (V < 0) {
        if (tisFormatting || io_busy > 0) {
            // decrement counter thread not ready
            if (io_busy > 0) io_busy--;

                    SetDCB(Dev, DCB_CMD_STAT, 0x00); // wait to AdamNet
                    if (io_busy == 0) {
                        if (last_command_read) {
                            last_command_read = 0;
                            AdamFlushCache();
                        }
                    }
                } else {
                    SetDCB(Dev, DCB_CMD_STAT, RSP_STATUS);
                }
                return;
            }

    // 4. COMMAND LOGIC (V >= 0)
    SetDCB(Dev, DCB_NODE_TYPE, (GetDCB(Dev, DCB_NODE_TYPE) & 0xF0) | (Disks[N].Data ? 0x00 : 0x03));

    switch (V) {
    case CMD_STATUS: // CMD_STATUS
        ReportDevice(Dev, 0x0400, 1);
        SetDCB(Dev, DCB_CMD_STAT, RSP_STATUS);
        break;

    case CMD_SOFT_RESET: // CMD_SOFT_RESET
         SetDCB(Dev, DCB_CMD_STAT, RSP_STATUS);
        break;

    case CMD_READ:
    case CMD_WRITE:
        if (V == CMD_READ || V == CMD_WRITE) {
            g_diskSoundActive.store(true, std::memory_order_relaxed);
        }
        if (!Disks[N].Data) {
            SetDCB(Dev, DCB_CMD_STAT, RSP_STATUS);
            break;
        }

        io_show_status = (V == 4) ? 1 : 2;
        SetDCB(Dev, DCB_CMD_STAT, 0x00); // place at BUSY
        io_busy = DELAY_IO;

        BUF = GetDCBBase(Dev);
        LEN = GetDCBLen(Dev);
        if (LEN > 0x0400) LEN = 0x0400;
        SEC = GetDCBSector(Dev);

        savedBUF = BUF;
        savedLEN = LEN;

        {
            word tcurrentBUF = BUF;
            for (I = 0, SEC <<= 1; I < LEN; ++SEC, I += 0x200) {
                K = (SEC & ~7) | InterleaveTable[SEC & 7];
                Data = LinearFDI(&Disks[N], K);

                if (!Data) {
                    SetDCB(Dev, DCB_NODE_TYPE, GetDCB(Dev, DCB_NODE_TYPE) | 0x02);
                    break;
                }

                K = (I + 0x200 > LEN) ? (LEN - I) : 0x200;

                if (V == 4) {
                    last_command_read = true;
                    for (J = 0; J < K; ++J) HoldingBuf[I + J] = Data[J];
                } else {
                    last_command_read = false;
                    for (J = 0; J < K; ++J) {
                        // word-cast prevent > 64KB RAM
                        Data[J] = RAM_Memory[(word)(tcurrentBUF + J)];
                    }
                }
                tcurrentBUF += K;
            }
        }
        if (io_busy == 0) SetDCB(Dev, DCB_CMD_STAT, RSP_STATUS);
        break;

    default:
        SetDCB(Dev, DCB_CMD_STAT, RSP_STATUS);
        break;
    }
}


