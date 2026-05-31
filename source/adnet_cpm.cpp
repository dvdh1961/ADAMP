#include "adnet.h"
#include "cv.h"
#include <cstring>
#include <QDebug>

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

void ReadPCB_CPM(word A)
{
    if (!IsPCB(A)) return;
    A -= PCBAddr;

    if (A == PCB_CMD_STAT) {
        return;
    }
    else if (!((A - PCB_SIZE) % DCB_SIZE)) {
        const byte Dev = (A - PCB_SIZE) / DCB_SIZE;
        if (Dev <= GetMaxDCB()) UpdateDCB_CPM(Dev, -1);
    }
}

void WritePCB_CPM(word A, byte V)
{
    if (!IsPCB(A)) return;
    A -= PCBAddr;

    if (A == PCB_CMD_STAT) {
        switch (V) {
        case CMD_PCB_SYNC1:
        case CMD_PCB_SYNC2:
        case CMD_PCB_SNA:
            if (V == CMD_PCB_SNA) MovePCB(GetPCBBase(), GetMaxDCB());
            SetPCB(PCB_CMD_STAT, RSP_STATUS | V);
            break;
        case CMD_PCB_IDLE:
        case CMD_PCB_WAIT:
            break;
        case CMD_PCB_RESET:
            ResetPCB_CPM();
            break;
        default:
            break;
        }
    }
    else if (!((A - PCB_SIZE) % DCB_SIZE)) {
        const byte Dev = (A - PCB_SIZE) / DCB_SIZE;
        if (Dev <= GetMaxDCB()) UpdateDCB_CPM(Dev, V);
    }
}

void ResetPCB_CPM(void)
{
    m_cpm_selected = true;
    std::memset(PCBTable, 0, 0x10000);
    PCBAddr = 0x0000;
    MovePCB(0xFEC0, 15);

    KBDStatus = (byte)(RSP_STATUS | 0x00);
    LastKey = 0x00;
    g_key_buffer_head = 0;
    g_key_buffer_tail = 0;

    io_busy = 0;
    last_command_read = 0;
    savedBUF = 0;
    savedLEN = 0;

    adam_drive_local_reset();
}

void AdamFlushCache_CPM(void)
{
    // Compatibility wrapper; the local drive core now owns its own pending read state.
    for (word i = 0; i < savedLEN; ++i) {
        RAM_Memory[(savedBUF + i) & 0xFFFF] = HoldingBuf[i];
    }
}

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

void UpdateTAP_CPM(byte N, byte Dev, int V)
{
        int I,J,K,LEN,SEC;
        word BUF;
        byte *Data;

        /* If reading DCB status, stop here */
        static byte active_tape = 0xFF;  // Welke tape is nu bezig?

        if (V < 0)
        {
            // Alleen deze tape pollen als het de actieve is
            if (active_tape == N && io_busy > 0)
            {
                io_busy--;
                if (io_busy == 0)
                {
                    active_tape = 0xFF;  // Reset
                    // Net klaar: flush (indien READ) en direct READY zetten
                    if (last_command_read) {
                        last_command_read = 0;
                        AdamFlushCache();
                    }
                    SetDCB(Dev, DCB_CMD_STAT, RSP_STATUS);
                }
                else
                {
                    // Nog bezig
                    SetDCB(Dev, DCB_CMD_STAT, 0x00);
                }
            }
            // GEEN else - laat status zoals die is
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
            SetDCB(Dev,DCB_CMD_STAT,RSP_STATUS);
            break;

        case CMD_WRITE:
        case CMD_READ:
            g_tapeSoundActive.store(true, std::memory_order_relaxed);
            io_show_status = (V==CMD_READ) ? 1:2;
            // TODO if (io_show_status == 2) adam_unsaved_data = 1;
            /* Busy status by default */
            SetDCB(Dev,DCB_CMD_STAT,0x00);
            active_tape = N;  // Markeer deze tape als actief
            io_busy = DELAY_IO;
            /* If no tape, stop here */
            if(!Tapes[N].Data) break;
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
                /* Get pointer to sector data on tape */
                Data = LinearFDI(&Tapes[N],SEC);
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
                    for(J=0;J<K;++J,++BUF) Data[J] = RAM(BUF);
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

void UpdateDCB_CPM(byte Dev, int V)
{
    if (V == 0) return;

    const byte DevID = (GetDCB(Dev, DCB_DEV_NUM) << 4) + (GetDCB(Dev, DCB_ADD_CODE) & 0x0F);

    switch (DevID)
    {
    case 0x01:
        UpdateKBD(Dev, V);
        break;

    case 0x02:
        UpdatePRN(Dev, V);
        break;

    case 0x04:
        UpdateDSK_CPM(0, Dev, V);
        break;

    case 0x05:
        UpdateDSK_CPM(1, Dev, V);
        break;

    case 0x08:
        UpdateTAP_CPM(0, Dev, V);
        break;

    case 0x18:
        UpdateTAP_CPM(2, Dev, V);
        break;

    default:
        SetDCB(Dev, DCB_CMD_STAT, RSP_TIMEOUT);
        break;
    }
}
