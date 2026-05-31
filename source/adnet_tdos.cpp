#include "adnet.h"
#include "cv.h"
#include <cstdio>
#include <QDebug>
#include <QtConcurrentRun>
#include <QFuture>
#include <cstring>

bool tisFormatting = false;

//--------------------------------------------------------------------------------------
/** ReadPCB() ************************************************/
/** Read value from a given PCB or DCB address.             **/
/*************************************************************/
void ReadPCB_TDOS(word A)
{
    // FIX 1: Retourneer 0x00 als het geen PCB-adres is.
    if (!IsPCB(A)) return;

    // Bereken offset binnen PCB/DCB
    A -= PCBAddr;

    // Als de BIOS de PCB-status leest...
    if (A == PCB_CMD_STAT)
    {
        // Do nothing
    }
    // Als de BIOS de status van een *apparaat* leest...
    else if (!((A - PCB_SIZE) % DCB_SIZE))
    {
        byte Dev = (A - PCB_SIZE) / DCB_SIZE;
        if (Dev <= GetMaxDCB())
        {
            UpdateDCB_TDOS(Dev, -1);
        }
    }
}
//--------------------------------------------------------------------------------------
/** WritePCB() ***********************************************/
/** Write value to a given PCB or DCB address.              **/
/*************************************************************/
void WritePCB_TDOS(word A,byte V)
{
    if(!IsPCB(A)) return;

    /* Compute offset within PCB/DCB */
    A -= PCBAddr;

    /* If writing a PCB command... */
    if(A==PCB_CMD_STAT)
    {
        switch(V)
        {
        case CMD_PCB_SYNC1: /* Sync Z80 */
            SetPCB(PCB_CMD_STAT,RSP_STATUS|V);
            break;
        case CMD_PCB_SYNC2: /* Sync master 6801 */
            SetPCB(PCB_CMD_STAT,RSP_STATUS|V);
            break;
        case CMD_PCB_SNA: /* Rellocate PCB */
            MovePCB(GetPCBBase(),GetMaxDCB());
            SetPCB(PCB_CMD_STAT,RSP_STATUS|V);
            break;
        case CMD_PCB_IDLE:
        case CMD_PCB_WAIT:
            break;
        case CMD_PCB_RESET:
            memset(PCBTable,0,0x10000);
            break;
        default:
            memset(PCBTable,0,0x10000);
            break;
        }
    }
    /* If writing a DCB command... */
    else if(!((A-PCB_SIZE)%DCB_SIZE))
    {
        byte Dev = (A-PCB_SIZE)/DCB_SIZE;
        if(Dev<=GetMaxDCB()) {
                UpdateDCB_TDOS(Dev,V);
        }
    }
}
//--------------------------------------------------------------------------------------
/** ResetPCB() ***********************************************/
/** Reset PCB and attached hardware.                        **/
/*************************************************************/
void ResetPCB_TDOS(void)
{
    m_cpm_selected = false;
    /* PCB/DCB not mapped yet */
    memset(PCBTable,0,0x10000);

    /* Set starting PCB address */
    PCBAddr = 0x0000;
    MovePCB(0xFEC0,15);

    /* Reset keyboard state */
    KBDStatus = (byte)(RSP_STATUS | 0x00); // Set op 0x80 (Ready, No data)
    LastKey   = 0x00; // Reset oude buffer

    // Reset de *nieuwe* buffer
    g_key_buffer_head = 0;
    g_key_buffer_tail = 0;
}
//--------------------------------------------------------------------------------------


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


void UpdateTAP_TDOS(byte N,byte Dev,int V)
{
    int I,J,K,LEN,SEC;
    word BUF;
    byte *Data;

    /* If reading DCB status, stop here */
    if (V < 0)
    {
        if (io_busy > 0)
        {
            io_busy--;
            if (io_busy == 0)
            {
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
        else
        {
            // Veiligheid: al klaar
            SetDCB(Dev, DCB_CMD_STAT, RSP_STATUS);
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
        SetDCB(Dev,DCB_CMD_STAT,RSP_STATUS);
        break;

    case CMD_WRITE:
    case CMD_READ:
        g_tapeSoundActive.store(true, std::memory_order_relaxed);
        io_show_status = (V==CMD_READ) ? 1:2;
        // TODO if (io_show_status == 2) adam_unsaved_data = 1;
        /* Busy status by default */
        SetDCB(Dev,DCB_CMD_STAT,0x00);
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

void UpdateDCB_TDOS(byte Dev, int V)
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
    case 0x07: UpdateDSK_TDOS(DiskID=DevID-4,Dev,V);break;
    case 0x08:
    case 0x09:
    case 0x18:
    case 0x19: UpdateTAP_TDOS((DevID>>4)+((DevID&1)<<1),Dev,V);break;
    case 0x52: UpdateDSK_TDOS(DiskID,Dev,-2);break;
    default:
        SetDCB(Dev,DCB_CMD_STAT,RSP_ACK+0x0B);
        break;
    }
}

