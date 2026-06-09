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
#include "CORE/cv.h"
#include <cstdio>
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
        unsigned char retval = 0x02; // DOE (bit 1) is altijd 1

        // Poorten 0xE0 t/m 0xE3 worden gebruikt voor het lezen van de AdamNet Status/Data.
        if (Address >= 0xE0 && Address <= 0xE3)
        {
            // Lees de status uit PCBTable[0] (PCB_CMD_STAT)
            retval = PCBTable[0];

            if (!m_cpm_enabled)
            {
             PCBTable[0] &= ~0x01; // Wis Bit 0: Data-In Full
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
                UpdateDCB_EOS(Dev,V); }

    }

}
//--------------------------------------------------------------------------------------
// Reset PCB and attached hardware.
void ResetPCB_EOS(void)
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

        switch (DevID)
        {
            case 0x01: UpdateKBD(Dev,V);break;
            case 0x02: UpdatePRN(Dev,V);break;
            case 0x04: UpdateDSK_CPM(0, Dev, V);break;
            case 0x05: UpdateDSK_CPM(1, Dev, V);break;
            case 0x08: UpdateTAP_CPM(0, Dev, V);break;
            case 0x18: UpdateTAP_CPM(2, Dev, V);break;
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
//--------------------------------------------------------------------------------------
