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
 * adampcb.cpp
 *
 * Based on   PCB emulation (C) Marat Fayzullin 1994-2021
 *
*/
#include <QDebug>

#include "adamnet.h"
#include "coleco.h"
#include "screenwidget.h"
#include <cstdio>
#include <stdint.h>
#include "printwindow.h"

#define RAM(A)  (RAM_Memory[A])

extern byte coleco_port60;

byte PCBTable[0x10000];
byte HoldingBuf[4096];
word io_busy = 0;
word PCBAddr = 0x0000;
const byte InterleaveTable[8] = { 0, 5, 2, 7, 4, 1, 6, 3 };

int g_prn_line_counter = 0;
bool g_prn_in_wp = false;

bool m_cpm_enabled;
bool m_tdos_enabled;
bool m_cpm_status;
byte last_command_read;
byte io_show_status;
byte KBDStatus, LastKey, DiskID;
word savedBUF, savedLEN;

// Game mode flag: true = Adam games (scancodes), false = Writer/BASIC (ASCII)
static bool g_force_game_mode = false;

// Flag to track if F000 area has been cleared after boot
static bool g_vdp_cleared = false;
std::atomic<bool> g_diskSoundActive(false);
std::atomic<bool> g_tapeSoundActive(false);

// Een ring-buffer voor 8 toetsaanslagen (press/release events)
#define KEY_BUFFER_SIZE 8
static volatile uint8_t g_key_buffer[KEY_BUFFER_SIZE];
static volatile uint8_t g_key_buffer_head = 0;
static volatile uint8_t g_key_buffer_tail = 0;

// Status van het AdamNet keyboard device
enum AdamKeyboardStatus {
    KBD_IDLE = 0x00,       // Wacht op commando
    KBD_SCANNING = 0x01,   // BIOS heeft scan gevraagd, wacht op toets
    KBD_DATA_READY = 0x80  // Data is beschikbaar in de buffer
};

//--------------------------------------------------------------------------------------
// Stel game mode in voor correcte keypad routing
// enabled true = game mode (scancodes), false = writer mode (ASCII)
extern "C" void adamnet_set_game_mode(bool enabled) {
    g_force_game_mode = enabled;
}
// brief Check of we in game mode zijn
// @return true als game mode actief is
extern "C" bool adamnet_is_game_mode(void) { return g_force_game_mode;}
static int g_block_ascii_fkeys = 0;  // countdown tegen T..Y die nog via PutKBD zouden lekken
//--------------------------------------------------------------------------------------
#ifdef __cplusplus
extern "C" {
void adam_printer_chunk(const uint8_t* data, int len);
}
#endif
//--------------------------------------------------------------------------------------
extern "C" void adamnet_block_ascii_fkeys(int count)
{
    if (count < 0) count = 0;
    g_block_ascii_fkeys = count;
}
//--------------------------------------------------------------------------------------
extern "C" void adamnet_host_prn_write_ascii(const char* s)
{
    if (!s) return;

    const uint8_t* p = reinterpret_cast<const uint8_t*>(s);

    while (*p) {
        int n = 0;
        const uint8_t* start = p;
        while (p[n] && n < 512) ++n;
        adam_printer_chunk(start, n);
        p += n;
    }
}
//--------------------------------------------------------------------------------------
// --- AdamNet printer sink: UI kan zich hierop abonneren ---
extern "C" {typedef void (*AdamPrinterSink)(const char* data, int len);
static AdamPrinterSink g_printer_sink = nullptr;
void adam_printer_set_sink(AdamPrinterSink sink) { g_printer_sink = sink; }
}
//--------------------------------------------------------------------------------------
// Injecteer een ADAM scancode rechtstreeks voor de Writer (EmulTwo-stijl via LastKey)
extern "C" void adamnet_inject_scancode(uint8_t sc)
{
    // Stuur de scancode (bv. 0xB4 of 0x34) naar de queue
    adamnet_queue_key(sc);
}
//--------------------------------------------------------------------------------------
void adamnet_queue_key(uint8_t key_code)
 {
    uint8_t mapped = 0;
    // FG1..FG6 remap + F7..F10
    if ((key_code & 0x7F) >= 0x54 && (key_code & 0x7F) <= 0x5D) {

        uint8_t idx = (key_code & 0x7F) - 0x54;    // 0..7
        if (idx<6)
            mapped = 0x81 + idx;               // MAKE = 0xB4..0xB9
        else
            if (idx==6) mapped = 0X93; // F7
        else
            if (idx==7) mapped = 0x95; // F8
        else
            if (idx==8) mapped = 0x96; // F9
        else
            if (idx==9) mapped = 0x97; // F10

        if (key_code & 0x80){
             mapped = mapped ^ 0x80;
        }
        key_code = mapped;
    }

    // ONDERSCHEP KEYBOARD EVENTS VOOR DE TELLER
    // We kijken naar 'key_code' (de rauwe scancode voor mapping)

    // 1. ENTER check (Scancode 0x0D)
    if (key_code == 0x0D) {
        // g_prn_line_counter++;
        // qDebug() << "[ADAMNET] ENTER gedrukt: Lijn teller nu op" << g_prn_line_counter;
    }

    // 2. F8 check (RESET via de gemapte code 0x95)
    // (Zorg dat 'mapped' hierboven al is berekend)
    if (mapped == 0x95) {
        g_prn_line_counter = 0;
        qDebug() << "[ADAMNET] F8 gedrukt: Lijn teller gereset naar 0";
    }

    // Bereken de volgende 'head' positie
    uint8_t next_head = (g_key_buffer_head + 1) % KEY_BUFFER_SIZE;

    // Als de buffer niet vol is...
    if (next_head != g_key_buffer_tail)
    {
        g_key_buffer[g_key_buffer_head] = key_code;
        g_key_buffer_head = next_head;
        // 1. Update interne status
        KBDStatus = (byte)(RSP_STATUS | 0x0C);
        // 2. STUUR NAAR DE Z80 RAM (Cruciaal voor games!)
        // Device 0 is het keyboard. Schrijf de status direct in de DCB.
        SetDCB(0, DCB_CMD_STAT, KBDStatus);
        // 3. ZET DE I/O VLAG (Voor poort 0xE0 polling)
        // AN_STAT_DIF (0x01) betekent: "Er zit data in de Host Adapter voor de CPU"
        PCBTable[0] |= 0x01;
    }
}

// --- Interne Helper Functies ---
//--------------------------------------------------------------------------------------
// @brief Haalt een key-event op uit de buffer.
// @return De key-code, of 0 als de buffer leeg is.
uint8_t adamnet_dequeue_key(void)
{
    // Als de buffer leeg is...
    if (g_key_buffer_head == g_key_buffer_tail)
    {
        return 0; // 0 = Geen toets
    }

    uint8_t key_code = g_key_buffer[g_key_buffer_tail];
    // Verplaats de 'tail'
    g_key_buffer_tail = (g_key_buffer_tail + 1) % KEY_BUFFER_SIZE;
qDebug() << "[AdamNet] DEQUEUE (naar BIOS):" << Qt::hex << key_code;
    return key_code;
}
//--------------------------------------------------------------------------------------
// @brief Controleert of de key buffer data bevat.
// @return 1 als niet leeg, 0 als leeg.
int adamnet_is_key_available(void)
{
    return (g_key_buffer_head != g_key_buffer_tail);
}

// --- AdamNet Hooks (aangeroepen door coleco.cpp) ---
//--------------------------------------------------------------------------------------
/** GetDCB() *************************************************/
/** Get DCB byte at given offset.                           **/
/*************************************************************/
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
/** GetPCB() *************************************************/
/** Get PCB byte at given offset.                           **/
/*************************************************************/
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
/** SetDCB() *************************************************/
/** Set DCB byte at given offset.                           **/
/*************************************************************/
void SetDCB(byte Dev,byte Offset,byte Value)
{
    word A = (PCBAddr+PCB_SIZE+Dev*DCB_SIZE+Offset)&0xFFFF;

    RAM_Memory[A] = Value;
}
//--------------------------------------------------------------------------------------
/** SetPCB() *************************************************/
/** Set PCB byte at given offset.                           **/
/*************************************************************/
void SetPCB(word Offset,byte Value)
{
    word A = (PCBAddr+Offset)&0xFFFF;
    RAM_Memory[A] = Value;
}
//--------------------------------------------------------------------------------------
/** IsPCB() **************************************************/
/** Return 1 if given address belongs to PCB, 0 otherwise.  **/
/*************************************************************/
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
/** MovePCB() ************************************************/
/** Move PCB and related DCBs to a new address.             **/
/*************************************************************/
void MovePCB(word NewAddr,byte MaxDCB)
{
    int J;

    PCBTable[PCBAddr] = 0;
    for(J=0;J<15*DCB_SIZE;J+=DCB_SIZE)
        PCBTable[(PCBAddr+PCB_SIZE+J)&0xFFFF] = 0;

    PCBTable[NewAddr] = 1;
    for(J=0;J<15*DCB_SIZE;J+=DCB_SIZE)
        PCBTable[(NewAddr+PCB_SIZE+J)&0xFFFF] = 1;

    PCBAddr = NewAddr;
    SetPCB(PCB_BA_LO,   NewAddr&0xFF);
    SetPCB(PCB_BA_HI,   NewAddr>>8);
    SetPCB(PCB_MAX_DCB, MaxDCB);

    qDebug() << "[PCB] MaxDCB =" << MaxDCB << "at address"
             << QString("0x%1").arg(NewAddr, 4, 16, QChar('0'));


    for(J=0;J<MaxDCB;++J)
    {
        SetDCB(J,DCB_DEV_NUM,0);
        SetDCB(J,DCB_ADD_CODE,J);
    }

    // if (m_cpm_enabled && !m_tdos_enabled)
    // {
    //     // Speciale toewijzing voor CP/M
    //     for(J=0; J<=MaxDCB; ++J)
    //     {
    //         SetDCB(J, DCB_DEV_NUM, 0);

    //         // Speciale mapping voor CP/M devices:
    //         if (J == 0) SetDCB(J, DCB_ADD_CODE, 1);      // Keyboard
    //         else if (J == 1) SetDCB(J, DCB_ADD_CODE, 2); // Printer
    //         else if (J == 2) SetDCB(J, DCB_ADD_CODE, 8); // Tape 0
    //         else if (J == 3) SetDCB(J, DCB_ADD_CODE, 9); // Tape 1
    //         else SetDCB(J, DCB_ADD_CODE, J);             // Rest: slot = ADD_CODE
    //     }
    // }


    // CHECK:
    byte check = GetDCB(J, DCB_ADD_CODE);
    if (check != J) {
        qDebug() << "[PCB ERROR] DCB" << J << "has ADD_CODE" << check << "instead of" << J;
    }

    // // FIX VOOR CP/M DISK DEVICES:
    // if (m_cpm_enabled) {
    //     SetDCB(4, DCB_DEV_NUM, 4);  // Disk 0
    //     SetDCB(5, DCB_DEV_NUM, 5);  // Disk 1
    //     SetDCB(6, DCB_DEV_NUM, 6);  // Disk 2
    //     SetDCB(7, DCB_DEV_NUM, 7);  // Disk 3
    // }

    // Check wat er staat:
    byte count = RAM_Memory[0xFEC3];
    qDebug() << "[PCB] Device count at 0xFEC3 =" << count;
}
//--------------------------------------------------------------------------------------
/** ReportDevice() *******************************************/
/** Reply to STATUS command with device parameters.         **/
/*************************************************************/
void ReportDevice(byte Dev,word MsgSize,byte IsBlock)
{
    SetDCB(Dev,DCB_CMD_STAT, RSP_STATUS);
    SetDCB(Dev,DCB_MAXL_LO,  MsgSize&0xFF);
    SetDCB(Dev,DCB_MAXL_HI,  MsgSize>>8);
    SetDCB(Dev,DCB_DEV_TYPE, IsBlock? 0x01:0x00);
}
//--------------------------------------------------------------------------------------
/** PutKBD() *************************************************/
/** Voeg ASCII-toets toe aan de (oude) KBD-buffer.          **/
/*************************************************************/
void PutKBD(unsigned int Key)
{
if (Key & 0x80) {
    // release: 0xC1 voor 'A' → basis = 0x41
    byte baseKey = (byte)(Key & 0x7F);
    if (baseKey == LastKey) LastKey = 0x00;
} else {
    // press: 0x41 voor 'A'
    LastKey = (byte)Key;
}

// De KBDStatus moet worden bijgewerkt, maar de queue wordt hier niet gevuld.
KBDStatus = (byte)(RSP_STATUS | 0x0C);
}
//--------------------------------------------------------------------------------------
/** GetKBD() *************************************************/
/** Haal éérst LastKey, anders uit AdamNet ringbuffer.      **/
/*************************************************************/
byte GetKBD()
{
    extern BYTE RAM_Memory[];
    extern BYTE VDP_Memory[];

    // PATCH wissen rommel in scherm bij opstart T-Dos bios
    if (m_tdos_enabled && !m_80colEnabled)
    {
        if (g_vdp_cleared == false) {
            memset(RAM_Memory + 0xF900, 0, 0x284);
        }
        if (VDP_Memory[0x3747]==0x00 || VDP_Memory[0x3747]==0x20  || VDP_Memory[0x3747]==0xff) g_vdp_cleared = true;
        else g_vdp_cleared = false;
    }

    if (adamnet_is_key_available())
    {
        byte sc = adamnet_dequeue_key();
       // qDebug() << "SCANCODE:" << Qt::hex << sc;
        return sc;
    }
    if (LastKey != 0) {
        //qDebug() << "ASCII:" << Qt::hex << LastKey;
    }

    if (LastKey==0x1B) // Escape gedrukt
        {
        g_prn_in_wp = true; // Printer in wordprocessor

        PrintWindow* w = PrintWindow::instance();
            if (w) {
                    QMetaObject::invokeMethod(w, "updatePrinterMode", Qt::QueuedConnection, Q_ARG(bool, g_prn_in_wp));
            }

            g_prn_line_counter = 0;
        }
    // 2. Als die leeg is, check de ASCII LastKey (voor '9', 'A', etc.)
    byte Result = LastKey;
    LastKey = 0x00;
    return(Result);

}
//--------------------------------------------------------------------------------------
/** UpdateKBD() **********************************************/
void UpdateKBD(byte Dev,int V)
{
    int J,N;
    word A;

    switch(V)
    {
    case -1:
        SetDCB(Dev,DCB_CMD_STAT,KBDStatus);
        break;
    case CMD_STATUS:
    case CMD_SOFT_RESET:
    {
        // Is er een key?
        const int ready = adamnet_is_key_available() || (LastKey != 0);
        KBDStatus = (byte)(RSP_STATUS | (ready ? 0x0C : 0x00));

        qDebug() << "[KBD_STATUS] Rdy:" << ready
                 << " Status:" << Qt::hex << KBDStatus
                 << " Queue Size:" << (g_key_buffer_head - g_key_buffer_tail);

        ReportDevice(Dev,0x0001,0);

        // KBDStatus = status + "data available" indien ready
        KBDStatus = (byte)(RSP_STATUS | (ready ? 0x0C : 0x00));
        SetDCB(Dev,DCB_CMD_STAT, KBDStatus);
    }
    break;
    case CMD_WRITE:
        SetDCB(Dev,DCB_CMD_STAT,RSP_ACK+0x0B);
        KBDStatus = RSP_STATUS;
        break;
    case CMD_READ:
        SetDCB(Dev,DCB_CMD_STAT,0x00);
        A = GetDCBBase(Dev);
        N = GetDCBLen(Dev);
        for(J=0 ; (J<N) && (V=GetKBD()) ; ++J, A=(A+1)&0xFFFF)
        {
            RAM_Memory[A] = V;
        }
        KBDStatus = RSP_STATUS+(J<N? 0x0C:0x00);
        break;
    }
}
//--------------------------------------------------------------------------------------
void UpdatePRN(byte Dev, int V)
{
    int N;
    word A;
    static char g_row_buf[121];
    static int g_cur_x = 0;
    static bool g_row_init = false;

    if (!g_row_init) {
        memset(g_row_buf, ' ', 120);
        g_row_buf[120] = '\0';
        g_row_init = true;
    }

    switch(V)
    {
    case CMD_STATUS:
    case CMD_SOFT_RESET:
        // Rapporteer altijd dat het apparaat 'Gezond' is (0x0001)
        ReportDevice(Dev, 0x0001, 0);
        // Forceer de status direct op Idle
        SetDCB(Dev, DCB_CMD_STAT, 0x80);
        break;

    case CMD_READ:
        SetDCB(Dev,DCB_CMD_STAT,RSP_ACK+0x0B);
        break;

    case CMD_WRITE:
    {
        SetDCB(Dev, DCB_CMD_STAT, 0x00);
        A = GetDCBBase(Dev);
        N = GetDCBLen(Dev);

        if (N > 0) {
            for (int j = 0; j < N; ++j, A = (A + 1) & 0xFFFF) {
                uint8_t c = RAM_Memory[A];

                if (c == 13 || c == 10 || c == 11) {
                    std::string line(g_row_buf, 120);

                    size_t first = line.find_first_not_of(' ');
                    size_t last = line.find_last_not_of(' ');

                    if (first != std::string::npos) {
                        std::string processed = line.substr(first, (last - first + 1));

                        // Gebruik de globale teller voor de spiegel-logica
                        if (g_prn_line_counter % 2 != 0) {
                            qDebug() << "[ADAMNET] Lijn teller:" << g_prn_line_counter ;
                            std::reverse(processed.begin(), processed.end());
                        }
                        else
                            qDebug() << "[ADAMNET] Lijn teller:" << g_prn_line_counter ;

                        adam_printer_chunk((uint8_t*)processed.c_str(), processed.length());
                        uint8_t nl = '\n';
                        adam_printer_chunk(&nl, 1);

                        // bij wordprocessor mode
                        if (g_prn_in_wp)
                        {
                            g_prn_line_counter++;
                        }
                    }

                    memset(g_row_buf, ' ', 120);
                    g_cur_x = 0;
                }
                else if (c == 8) {
                    if (g_cur_x > 0) g_cur_x--;
                }
                else if (c >= 32) {
                    if (g_cur_x < 120) {
                        g_row_buf[g_cur_x] = (char)c;
                        g_cur_x++;
                    }
                }
            }
        }


        SetDCB(Dev, DCB_CMD_STAT, RSP_ACK + 0x0B);
    }
    break;

    default:
        //SetDCB(Dev, DCB_CMD_STAT, RSP_STATUS);
        // Voor alle andere commando's: zeg gewoon 'OK'
        SetDCB(Dev, DCB_CMD_STAT, 0x80);
        break;
    }
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
/** ReadPCB() ************************************************/
/** Read value from a given PCB or DCB address.             **/
/*************************************************************/
void ReadPCB(word A)
{
    // FIX 1: Retourneer 0x00 als het geen PCB-adres is.
    if (!IsPCB(A)) return;

    // Bereken offset binnen PCB/DCB
    A -= PCBAddr;

    // FIX 2: Als de BIOS de PCB-status leest...
    if (A == PCB_CMD_STAT)
    {
        // Retourneer de PCB-status die in het RAM staat
        //return RAM_Memory[PCBAddr + PCB_CMD_STAT];
    }
    // FIX 3: Als de BIOS de status van een *apparaat* leest...
    else if (!((A - PCB_SIZE) % DCB_SIZE))
    {
        byte Dev = (A - PCB_SIZE) / DCB_SIZE;
        if (Dev <= GetMaxDCB())
        {
            if(m_cpm_enabled)
                if (m_tdos_enabled) UpdateDCB_TDOS(Dev, -1);
                else UpdateDCB_CPM(Dev, -1); // Deze functie update de status in RAM
            else
                UpdateDCB_EOS(Dev, -1); // Deze functie update de status in RAM
        }
    }
}
//--------------------------------------------------------------------------------------
/** WritePCB() ***********************************************/
/** Write value to a given PCB or DCB address.              **/
/*************************************************************/
void WritePCB(word A,byte V)
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
            memset(PCBTable,0,sizeof(PCBTable));
            break;
        default:
            memset(PCBTable,0,sizeof(PCBTable));
            break;
        }
    }
    /* If writing a DCB command... */
    else if(!((A-PCB_SIZE)%DCB_SIZE))
    {
        byte Dev = (A-PCB_SIZE)/DCB_SIZE;
        if(Dev<=GetMaxDCB()) {
            if (m_cpm_enabled)
                if (m_tdos_enabled) UpdateDCB_TDOS(Dev,V);
                else UpdateDCB_CPM(Dev,V);
            else
                UpdateDCB_EOS(Dev,V); }

    }

}
//--------------------------------------------------------------------------------------
/** ResetPCB() ***********************************************/
/** Reset PCB and attached hardware.                        **/
/*************************************************************/
void ResetPCB(void)
{
    /* PCB/DCB not mapped yet */
    memset(PCBTable,0,sizeof(PCBTable));

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
/** ChangeTape() *********************************************/
/** Change tape image in a given drive. Closes current tape **/
/** image if Name=0 was given. Creates a new tape image if  **/
/** Name="" was given. Returns 1 on success or 0 on failure.**/
/*************************************************************/
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
/** ChangeDisk() *********************************************/
/** Change disk image in a given drive. Closes current disk **/
/** image if Name=0 was given. Creates a new disk image if  **/
/** Name="" was given. Returns 1 on success or 0 on failure.**/
/*************************************************************/
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
