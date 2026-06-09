#include "adnet_core.h"
#include "CORE/cv.h"

#include <cstring>
#include <QDebug>

/*
 * AdamNet DDP/TAPE handlers
 * -------------------------
 * Alle tape/DDP-logica zit hier gegroepeerd per OS:
 *   - EOS  : UpdateTAP_EOS()
 *   - CP/M : UpdateTAP_CPM()
 *   - T-DOS: UpdateTAP_TDOS()
 *
 * De publieke functienamen blijven identiek.
 */

// -----------------------------------------------------------------------------
// EOS tape/DDP handler
// -----------------------------------------------------------------------------

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


// -----------------------------------------------------------------------------
// CP/M tape/DDP handler
// -----------------------------------------------------------------------------

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


// -----------------------------------------------------------------------------
// T-DOS tape/DDP handler
// -----------------------------------------------------------------------------

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

