/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *
 *  Adamnet core
 *
 * Based on   PCB emulation (C) Marat Fayzullin 1994-2021
 *
*/
#include <QDebug>

#include "6801/adnet_core.h"
#include "6801/adnet_mcu2.h"
#include "6801/adnet_mcu2_fujinet.h"
#include "CORE/cv.h"
#include <cstdio>
#include <cstdarg>
#include <atomic>

#define RAM(A)  (RAM_Memory[A])

extern byte coleco_port60;

byte PCBTable[0x10000];
byte HoldingBuf[4096];
word io_busy = 0;
word PCBAddr = 0x0000;
const byte InterleaveTable[8] = { 0, 5, 2, 7, 4, 1, 6, 3 };
std::atomic<bool> g_diskSoundActive(false);
std::atomic<bool> g_tapeSoundActive(false);

/*
 * Keyboard and printer device handlers live in:
 *   - adnet_kb.cpp
 *   - adnet_prn.cpp
 *
 * Keep the public function names unchanged. EOS/CPM/TDOS can still call
 * UpdateKBD(), UpdatePRN(), PutKBD(), GetKBD(), ...
 */

bool m_cpm_enabled;
bool m_tdos_enabled;
bool m_cpm_selected;
bool m_cpm_status;
byte last_command_read;
byte io_show_status;
byte KBDStatus, LastKey, DiskID;
word savedBUF, savedLEN;

void adamnet_eos_trace(const char* format, ...)
{
    static FILE* trace = nullptr;
    if (!trace)
        trace = std::fopen("ADAMP_EOS_TRACE.log", "w");
    if (!trace)
        return;

    va_list args;
    va_start(args, format);
    std::vfprintf(trace, format, args);
    va_end(args);
    std::fputc('\n', trace);
    std::fflush(trace);
}

// --- AdamNet Hooks (aangeroepen door coleco.cpp) ---
//--------------------------------------------------------------------------------------
byte GetDCB(byte Dev,byte Offset)
{
    word A = (PCBAddr+PCB_SIZE+Dev*DCB_SIZE+Offset)&0xFFFF;
    return(RAM_Memory[A]);
}
//--------------------------------------------------------------------------------------
word GetDCBBase(byte Dev)
{
    return(GetDCB(Dev,DCB_BA_LO)+((word)GetDCB(Dev,DCB_BA_HI)<<8));
}
//--------------------------------------------------------------------------------------
word GetDCBLen(byte Dev)
{
    return(GetDCB(Dev,DCB_BUF_LEN_LO)+((word)GetDCB(Dev,DCB_BUF_LEN_HI)<<8));
}
//--------------------------------------------------------------------------------------
unsigned int GetDCBSector(byte Dev)
{
    return(
        GetDCB(Dev,DCB_SEC_NUM_0)
        + ((unsigned int)GetDCB(Dev,DCB_SEC_NUM_1)<<8)
        + ((unsigned int)GetDCB(Dev,DCB_SEC_NUM_2)<<16)
        + ((unsigned int)GetDCB(Dev,DCB_SEC_NUM_3)<<24)
        );
}
//--------------------------------------------------------------------------------------
byte GetPCB(word Offset)
{
    word A = (PCBAddr+Offset)&0xFFFF;
    return(RAM_Memory[A]);
}
//--------------------------------------------------------------------------------------
word GetPCBBase(void)
{
    return(GetPCB(PCB_BA_LO)+((word)GetPCB(PCB_BA_HI)<<8));
}
//--------------------------------------------------------------------------------------
word GetMaxDCB(void)
{
    return(GetPCB(PCB_MAX_DCB));
}
//--------------------------------------------------------------------------------------
void SetDCB(byte Dev,byte Offset,byte Value)
{
    word A = (PCBAddr+PCB_SIZE+Dev*DCB_SIZE+Offset)&0xFFFF;

    RAM_Memory[A] = Value;
}
//--------------------------------------------------------------------------------------
void SetPCB(word Offset,byte Value)
{
    word A = (PCBAddr+Offset)&0xFFFF;
    RAM_Memory[A] = Value;
}
//--------------------------------------------------------------------------------------
int IsPCB(word A)
{
    /* Quick check for PCB presence */
    if(!PCBTable[A]) return(0);


    /* Check if PCB is mapped in */
    if((A<0x2000) && ((coleco_port60&0x03)!=1)) return(0);
    if((A<0x8000) && ((coleco_port60&0x03)!=1) && ((coleco_port60&0x03)!=3)) return(0);
    if((A>=0x8000) && (coleco_port60&0x0C)) return(0);

    /* Check number of active devices */
    if(A>=PCBAddr+PCB_SIZE+GetMaxDCB()*DCB_SIZE) return(0);
    /* This address belongs to AdamNet */
    return(1);
}
//--------------------------------------------------------------------------------------
void MovePCB(word NewAddr, byte MaxDCB)
{
    int J;
    const word old_lo = PCBAddr;
    const word old_len = PCB_SIZE + (GetMaxDCB() + 1) * DCB_SIZE;
    const word new_len = PCB_SIZE + (MaxDCB + 1) * DCB_SIZE;

    // Volledige oude range wissen
    for (J = 0; J < old_len; ++J)
        PCBTable[(old_lo + J) & 0xFFFF] = 0;

    // Volledige nieuwe range markeren
    for (J = 0; J < new_len; ++J)
        PCBTable[(NewAddr + J) & 0xFFFF] = 1;

    PCBAddr = NewAddr;
    SetPCB(PCB_BA_LO, NewAddr & 0xFF);
    SetPCB(PCB_BA_HI, NewAddr >> 8);
    SetPCB(PCB_MAX_DCB, MaxDCB);

    for (J = 0; J <= MaxDCB; ++J) {
        SetDCB(J, DCB_DEV_NUM, 0);
        SetDCB(J, DCB_ADD_CODE, J);
    }
}
//--------------------------------------------------------------------------------------
// Reply to STATUS command with device parameters.
void ReportDevice(byte Dev,word MsgSize,byte IsBlock)
{
    SetDCB(Dev,DCB_CMD_STAT, RSP_STATUS);
    SetDCB(Dev,DCB_MAXL_LO,  MsgSize&0xFF);
    SetDCB(Dev,DCB_MAXL_HI,  MsgSize>>8);
    SetDCB(Dev,DCB_DEV_TYPE, IsBlock? 0x01:0x00);
}
//--------------------------------------------------------------------------------------
void AdamFlushCache(void)
{
    for (word i=0; i<savedLEN; i++)
    {
        // Copy data from holding buffer...
        RAM_Memory[savedBUF] = HoldingBuf[i];
        savedBUF++;
    }
}
//--------------------------------------------------------------------------------------
// Read value from a given PCB or DCB address.
void ReadPCB(word A)
{
    if(m_cpm_enabled && !m_tdos_enabled)  ReadPCB_CPM(A);
    else if (m_cpm_enabled && m_tdos_enabled) ReadPCB_TDOS(A);
    else  if (!m_cpm_enabled) ReadPCB_EOS(A);
}
//--------------------------------------------------------------------------------------
// Write value to a given PCB or DCB address.
void WritePCB(word A,byte V)
{
    if(m_cpm_enabled && !m_tdos_enabled)  WritePCB_CPM(A,V);
    else if (m_cpm_enabled && m_tdos_enabled) WritePCB_TDOS(A,V);
    else  if (!m_cpm_enabled) WritePCB_EOS(A,V);
}
//--------------------------------------------------------------------------------------
// Reset PCB and attached hardware.
void ResetPCB(void)
{
    if(m_cpm_enabled && !m_tdos_enabled)  ResetPCB_CPM();
    else if (m_cpm_enabled && m_tdos_enabled) ResetPCB_TDOS();
    else  if (!m_cpm_enabled)  ResetPCB_EOS();
}
//--------------------------------------------------------------------------------------
// Change tape image in a given drive. Closes current tape
// image if Name=0 was given. Creates a new tape image if
// Name="" was given. Returns 1 on success or 0 on failure.
byte ChangeTape(byte N,const char *FileName)
{
    byte *P;

    /* We only have MAX_TAPES drives */
    if(N>=MAX_TAPES) return(0);

    /* Eject disk if requested */
    if(!FileName) { EjectFDI(&Tapes[N]);return(1); }

    /* If FileName not empty, try loading tape image */
    if(*FileName && LoadFDI(&Tapes[N],FileName,FMT_DDP))
    {
        /* Done */
        return(1);
    }

    /* If no existing file, create a new 256kB tape image */
    P = FormatFDI(&Tapes[N],FMT_DDP);
    return(!!P);
}
//--------------------------------------------------------------------------------------
// Change disk image in a given drive. Closes current disk
// image if Name=0 was given. Creates a new disk image if
// Name="" was given. Returns 1 on success or 0 on failure.
byte ChangeDisk(byte N,const char *FileName)
{
    byte *P;

    /* We only have MAX_DISKS drives */
    if(N>=MAX_DISKS) return(0);

    /* Eject disk if requested */
    if(!FileName) { EjectFDI(&Disks[N]);return(1); }

    /* If FileName not empty, try loading disk image */
    if(*FileName && LoadFDI(&Disks[N],FileName,FMT_ADMDSK))
    {
        /* Done */
        return(1);
    }

    /* If no existing file, create a new 160kB disk image */
    P = FormatFDI(&Disks[N],FMT_ADMDSK);
    return(!!P);
}
//--------------------------------------------------------------------------------------
extern "C" unsigned char adamnet_read_io(int Address)
{
        Address &= 0xFF;
        unsigned char retval = 0x02; // DOE (bit 1) is always available.

        // Poorten 0xE0 t/m 0xE3 worden gebruikt voor het lezen van de AdamNet Status/Data.
        if (Address >= 0xE0 && Address <= 0xE3)
        {
            /* Preserve DOE while reporting the asynchronous Data-In Full
             * notification. Returning PCBTable[0] directly changed 0x02
             * into 0x01, which is not a valid ready status for EOS.
             */
            retval |= (PCBTable[0] & AN_STAT_DIF);

            if (!m_cpm_enabled)
            {
             if (PCBTable[0] & AN_STAT_DIF)
             {
                 qDebug() << "[ADAMNET] EOS acknowledged Data-In Full; port="
                          << Qt::hex << Address << "status=" << int(retval);
             }
             PCBTable[0] &= ~AN_STAT_DIF;
            }
        }
        return retval;
}
//--------------------------------------------------------------------------------------

/*
 * OS PCB/DCB routers are kept in this file now.
 * Media handlers remain grouped by physical device:
 *   - adnet_dsk.cpp : UpdateDSK_EOS/CPM/TDOS
 *   - adnet_ddp.cpp : UpdateTAP_EOS/CPM/TDOS
 */

//======================================================================================
// EOS PCB/DCB routering
//======================================================================================

//--------------------------------------------------------------------------------------
// Read value from a given PCB or DCB address.
void ReadPCB_EOS(word A)
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
                UpdateDCB_EOS(Dev, -1); // Deze functie update de status in RAM
        }
    }
}
//--------------------------------------------------------------------------------------
// Write value to a given PCB or DCB address.
void WritePCB_EOS(word A,byte V)
{
    if(!IsPCB(A)) return;

    /* Compute offset within PCB/DCB */
    A -= PCBAddr;

    /* If writing a PCB command... */
    if(A==PCB_CMD_STAT)
    {
        adamnet_eos_trace("PCB command=%02X pcb=%04X maxdcb=%u", V, PCBAddr, GetMaxDCB());
        switch(V)
        {
        case CMD_PCB_SYNC1: /* Sync Z80 */
            SetPCB(PCB_CMD_STAT,RSP_STATUS|V);
            break;
        case CMD_PCB_SYNC2: /* Sync master 6801 */
            SetPCB(PCB_CMD_STAT,RSP_STATUS|V);
            break;
        case CMD_PCB_SNA: /* Rellocate PCB */
        {
            const word oldPcb = PCBAddr;
            const word newPcb = GetPCBBase();
            const byte maxDcb = GetMaxDCB();
            /* The Z80 waits for 83 at the command byte of the old PCB.  Reply
             * there before changing PCBAddr; otherwise SetPCB() writes 83 at
             * the new PCB and the caller loops forever on the old 03.
             */
            RAM_Memory[oldPcb] = RSP_STATUS | V;
            MovePCB(newPcb,maxDcb);
            SetPCB(PCB_CMD_STAT,RSP_STATUS|V);
            qDebug() << "[AdamNet] PCB relocated from"
                     << QStringLiteral("%1").arg(oldPcb,4,16,QLatin1Char('0'))
                     << "to"
                     << QStringLiteral("%1").arg(newPcb,4,16,QLatin1Char('0'))
                     << "; SNA response 83 preserved at old PCB";
            break;
        }
        case CMD_PCB_IDLE:
        case CMD_PCB_WAIT:
            break;
        case CMD_PCB_RESET:
            /*
             * A PCB reset must rebuild the default EOS PCB mapping as well as
             * reset the attached devices.  Merely clearing PCBTable makes the
             * AdamNet hooks unreachable: after that, even a new SNA/relocate
             * command can no longer be observed by the emulator.
             */
            ResetPCB_EOS();
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
                UpdateDCB_EOS(Dev,V); }

    }

}
//--------------------------------------------------------------------------------------
// Reset PCB and attached hardware.
void ResetPCB_EOS(void)
{
    mcu2_disk_reset();
    m_cpm_selected = false;

    /*
     * A PCB reset cancels every outstanding AdamNet transfer.
     *
     * Without this, a delayed EOS disk read survives the reset.  Recipe
     * Filer issues a read of directory block 1 to $0000 and then resets and
     * synchronizes the PCB.  The stale completion used to copy that block
     * into RAM while EOS had its return stack at $00F3, replacing the saved
     * return address with $0000.  The RET at $FA2E consequently jumped into
     * directory text and the application appeared to hang.
     */
    io_busy = 0;
    last_command_read = 0;
    savedBUF = 0;
    savedLEN = 0;

    /* PCB/DCB not mapped yet */
    memset(PCBTable,0,0x10000);

    /* Set starting PCB address */
    PCBAddr = 0x0000;
    MovePCB(0xFEC0,15);

    /* Reset keyboard state */
    ResetKBDPendingRead();
    KBDStatus = (byte)(RSP_STATUS | 0x00); // Set op 0x80 (Ready, No data)
    LastKey   = 0x00; // Reset oude buffer

    // Reset de *nieuwe* buffer
    g_key_buffer_head = 0;
    g_key_buffer_tail = 0;
}
//--------------------------------------------------------------------------------------
// UpdateDSK_EOS moved to adnet_dsk.cpp/adnet_ddp.cpp
// UpdateTAP_EOS moved to adnet_dsk.cpp/adnet_ddp.cpp
//--------------------------------------------------------------------------------------
void UpdateDCB_EOS(byte Dev,int V)
{
    byte DevID;

    /* When writing, ignore invalid commands */
    if(!V || (V>=0x80)) return;

    /* Compute device ID */
    DevID = (GetDCB(Dev,DCB_DEV_NUM)<<4) + (GetDCB(Dev,DCB_ADD_CODE)&0x0F);

    if (V > 0)
        adamnet_eos_trace("DCB dev=%u devid=%02X command=%02X", Dev, DevID, V);

    /* Depending on the device ID... */
    switch(DevID)
    {
    case 0x01: UpdateKBD(Dev,V);break;
    case 0x02: UpdatePRN(Dev,V);break;
    case 0x04:
    case 0x05:
    case 0x06:
    case 0x07:
        if (mcu2_block_device_is_enabled(DevID)) {
            UpdateDSK_MCU2_EOS(Dev, V);
            break;
        }
        UpdateDSK_EOS(DiskID=DevID-4,Dev,V);break;
    case 0x08:
    case 0x09:
    case 0x18:
    case 0x19:
        if (mcu2_block_device_is_enabled(DevID)) {
            UpdateDSK_MCU2_EOS(Dev, V);
            break;
        }
        UpdateTAP_EOS((DevID>>4)+((DevID&1)<<1),Dev,V);break;
    case 0x52: UpdateDSK_EOS(DiskID,Dev,-2);break;

    case 0x0F:
        if (mcu2_disk_is_enabled()) {
            UpdateFUJINET_MCU2_EOS(Dev, V);
            break;
        }
        SetDCB(Dev, DCB_CMD_STAT, RSP_ACK + 0x0B);
        break;

    default:
        SetDCB(Dev,DCB_CMD_STAT,RSP_ACK+0x0B);
        break;
    }
}
//--EINDE EOS---------------------------------------------------------------------------

//======================================================================================
// CP/M PCB/DCB routering
//======================================================================================

// CP/M PCB/DCB routering blijft hier.
// Disk/tape mediahandlers zijn verhuisd naar adnet_dsk.cpp en adnet_ddp.cpp.

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
    mcu2_disk_reset();
    ResetKBDPendingRead();
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

// UpdateDSK_CPM moved to adnet_dsk.cpp/adnet_ddp.cpp
// UpdateTAP_CPM moved to adnet_dsk.cpp/adnet_ddp.cpp

    void UpdateDCB_CPM(byte Dev, int V)
    {
        if (V == 0) return;

        const byte DevID = (GetDCB(Dev, DCB_DEV_NUM) << 4) + (GetDCB(Dev, DCB_ADD_CODE) & 0x0F);

        /* Some ADAM CP/M loaders build a second, temporary block DCB whose
         * device-number/add-code fields are workspace values rather than the
         * normal 00/04 D5 identity.  The local software drive historically
         * gets the next block through its CP/M cache, but a physical D5 must
         * still receive this DCB.  Keep the compatibility route deliberately
         * narrow: unknown identity, physical D5 enabled, a 1024-byte block
         * buffer and either a block READ/WRITE command or a status poll of
         * that same DCB.  Character devices and recognised drives are never
         * redirected. */
        const byte command = byte(V < 0 ? GetDCB(Dev, DCB_CMD_STAT)
                                        : (V & 0x7F));
        const bool knownDevice = DevID == 0x01 || DevID == 0x02
                              || DevID == 0x04 || DevID == 0x05
                              || DevID == 0x08 || DevID == 0x18;
        const bool physicalCpmD5Block = !knownDevice
                                     && mcu2_block_device_is_enabled(0x04)
                                     && GetDCBLen(Dev) == 0x0400
                                     && (V < 0 || command == CMD_READ
                                               || command == CMD_WRITE);
        if (physicalCpmD5Block) {
            if (V >= 0) {
                qDebug() << "[MCU2][CPM] temporary block DCB" << int(Dev)
                         << "identity=" << Qt::hex << int(DevID)
                         << "normalised to physical D5; command=" << int(command)
                         << "sector=" << Qt::dec << GetDCBSector(Dev);
            }
            if (V < 0 || command == CMD_READ) {
                UpdateDSK_MCU2_CPM_D5(Dev, V < 0 ? V : int(command));
                return;
            }
            /* UpdateDSK_MCU2_EOS copies the complete DCB before starting its
             * asynchronous gateway request.  Present the physical D5 identity
             * during that copy; otherwise the selected fallback still sends
             * the request to the temporary add-code (02 = printer). */
            const byte savedDeviceNumber = GetDCB(Dev, DCB_DEV_NUM);
            const byte savedAddCode = GetDCB(Dev, DCB_ADD_CODE);
            SetDCB(Dev, DCB_DEV_NUM, 0x00);
            SetDCB(Dev, DCB_ADD_CODE, 0x04);
            UpdateDSK_MCU2_EOS(Dev, V);
            /* Preserve CP/M's workspace until the real DCB reply completes.
             * The pending request already owns its normalised copy. */
            SetDCB(Dev, DCB_DEV_NUM, savedDeviceNumber);
            SetDCB(Dev, DCB_ADD_CODE, savedAddCode);
            return;
        }

        switch (DevID)
        {
            case 0x01: UpdateKBD(Dev,V);break;
            case 0x02: UpdatePRN(Dev,V);break;
            case 0x04:
                if (mcu2_block_device_is_enabled(DevID)) {
                    if (V < 0 || command == CMD_READ)
                        UpdateDSK_MCU2_CPM_D5(Dev, V < 0 ? V : int(command));
                    else
                        UpdateDSK_MCU2_EOS(Dev, V);
                }
                else UpdateDSK_CPM(0, Dev, V);
                break;
            case 0x05:
                if (mcu2_block_device_is_enabled(DevID)) UpdateDSK_MCU2_EOS(Dev, V);
                else UpdateDSK_CPM(1, Dev, V);
                break;
            case 0x06:
            case 0x07:
            case 0x09:
            case 0x19:
                if (mcu2_block_device_is_enabled(DevID)) UpdateDSK_MCU2_EOS(Dev, V);
                else SetDCB(Dev, DCB_CMD_STAT, RSP_TIMEOUT);
                break;
            case 0x08:
                if (mcu2_block_device_is_enabled(DevID)) UpdateDSK_MCU2_EOS(Dev, V);
                else UpdateTAP_CPM(0, Dev, V);
                break;
            case 0x18:
                if (mcu2_block_device_is_enabled(DevID)) UpdateDSK_MCU2_EOS(Dev, V);
                else UpdateTAP_CPM(2, Dev, V);
                break;
            default:
                SetDCB(Dev, DCB_CMD_STAT, RSP_TIMEOUT);
           break;
        }
}

//======================================================================================
// T-DOS PCB/DCB routering
//======================================================================================

// T-DOS disk format state moved to adnet_dsk.cpp

//--------------------------------------------------------------------------------------
// Read value from a given PCB or DCB address.
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
// Write value to a given PCB or DCB address.
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
// Reset PCB and attached hardware.
void ResetPCB_TDOS(void)
{
    mcu2_disk_reset();
    ResetKBDPendingRead();
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

// UpdateDSK_TDOS moved to adnet_dsk.cpp/adnet_ddp.cpp
// UpdateTAP_TDOS moved to adnet_dsk.cpp/adnet_ddp.cpp

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
    case 0x07:
        if (mcu2_block_device_is_enabled(DevID)) {
            UpdateDSK_MCU2_EOS(Dev, V);
            break;
        }
        UpdateDSK_TDOS(DiskID=DevID-4,Dev,V);break;
    case 0x08:
    case 0x09:
    case 0x18:
    case 0x19:
        if (mcu2_block_device_is_enabled(DevID)) {
            UpdateDSK_MCU2_EOS(Dev, V);
            break;
        }
        UpdateTAP_TDOS((DevID>>4)+((DevID&1)<<1),Dev,V);break;
    case 0x52: UpdateDSK_TDOS(DiskID,Dev,-2);break;
    default:
        SetDCB(Dev,DCB_CMD_STAT,RSP_ACK+0x0B);
        break;
    }
}
//--------------------------------------------------------------------------------------
