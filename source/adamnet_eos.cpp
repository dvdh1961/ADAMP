#include "adamnet.h"
#include "coleco.h"
#include <cstdio>
#include <QDebug>

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

void UpdateTAP_EOS(byte N,byte Dev,int V)
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
        qDebug() << "TAPE CMD_READ buffer filled";
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

void UpdateDCB_EOS(byte Dev,int V)
{
    byte DevID;

    /* When writing, ignore invalid commands */
    if(!V || (V>=0x80)) return;

    /* Compute device ID */
    DevID = (GetDCB(Dev,DCB_DEV_NUM)<<4) + (GetDCB(Dev,DCB_ADD_CODE)&0x0F);

    /* Depending on the device ID... */
    switch(DevID)
    {
    case 0x01: UpdateKBD(Dev,V);break;
    case 0x02: UpdatePRN(Dev,V);break;
    case 0x04:
    case 0x05:
    case 0x06:
    case 0x07: UpdateDSK_EOS(DiskID=DevID-4,Dev,V);break;
    case 0x08:
    case 0x09:
    case 0x18:
    case 0x19: UpdateTAP_EOS((DevID>>4)+((DevID&1)<<1),Dev,V);break;
    case 0x52: UpdateDSK_EOS(DiskID,Dev,-2);break;

    default:
        SetDCB(Dev,DCB_CMD_STAT,RSP_ACK+0x0B);
        break;
    }
}
//--EINDE EOS---------------------------------------------------------------------------
