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

#define RAM(A)         (RAM_Memory[A])

extern byte coleco_port60;

byte PCBTable[0x10000];
byte HoldingBuf[4096];
word io_busy = 0;
word PCBAddr = 0x0000;
const byte InterleaveTable[8] = { 0, 5, 2, 7, 4, 1, 6, 3 };

bool m_cpm_enabled;
bool m_tdos_enabled;
bool m_cpm_status;
byte last_command_read;
byte io_show_status;
byte KBDStatus, LastKey, DiskID;
word savedBUF, savedLEN;

// Game mode flag: true = Adam games (scancodes), false = Writer/BASIC (ASCII)
static bool g_force_game_mode = false;

/**
 * @brief Stel game mode in voor correcte keypad routing
 * @param enabled true = game mode (scancodes), false = writer mode (ASCII)
 */
extern "C" void adamnet_set_game_mode(bool enabled) {
    g_force_game_mode = enabled;
    qDebug() << "[ADAMNET] Game mode" << (enabled ? "ENABLED" : "DISABLED")
             << "- Keypad will use " << (enabled ? "scancodes" : "ASCII");
}

/**
 * @brief Check of we in game mode zijn
 * @return true als game mode actief is
 */
extern "C" bool adamnet_is_game_mode(void) {
    return g_force_game_mode;
}

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

#include <stdint.h>

static int g_block_ascii_fkeys = 0;  // countdown tegen T..Y die nog via PutKBD zouden lekken

#ifdef __cplusplus
extern "C" {
void adam_printer_chunk(const uint8_t* data, int len);
}
#endif

extern "C" void adamnet_block_ascii_fkeys(int count)
{
    if (count < 0) count = 0;
    g_block_ascii_fkeys = count;
}

extern "C" void adamnet_host_prn_write_ascii(const char* s)
{
    if (!s) return;

    // Simuleer een PRN "write" blok zoals AdamNet dat zou leveren:
    // We sturen de bytes meteen door via jouw bestaande bridge.
    // (Het is AdamNet-PRN pad qua flow, alleen zonder SmartWriter als bron.)
    const uint8_t* p = reinterpret_cast<const uint8_t*>(s);

    // Stuur in hapklare blokken zodat de UI soepel blijft
    while (*p) {
        // snijd tot max ~512 bytes per 'blok' (vergelijkbaar met sector)
        int n = 0;
        const uint8_t* start = p;
        while (p[n] && n < 512) ++n;
        adam_printer_chunk(start, n);
        p += n;
    }
}

// --- AdamNet printer sink: UI kan zich hierop abonneren ---
extern "C" {typedef void (*AdamPrinterSink)(const char* data, int len);
static AdamPrinterSink g_printer_sink = nullptr;
void adam_printer_set_sink(AdamPrinterSink sink) { g_printer_sink = sink; }
}

// Injecteer een ADAM scancode rechtstreeks voor de Writer (EmulTwo-stijl via LastKey)
extern "C" void adamnet_inject_scancode(uint8_t sc)
{
    // Stuur de scancode (bv. 0xB4 of 0x34) naar de queue
    adamnet_queue_key(sc);
}


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
        //qDebug() << "[AdamNet] ENQUEUE (na remap):" << Qt::hex << key_code;
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

            //qDebug() << "[AdamNet] Toets geactiveerd in PCB & DCB:" << Qt::hex << key_code;
        }
}


// --- Interne Helper Functies ---

/**
 * @brief Haalt een key-event op uit de buffer.
 * @return De key-code, of 0 als de buffer leeg is.
 */
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

/**
 * @brief Controleert of de key buffer data bevat.
 * @return 1 als niet leeg, 0 als leeg.
 */
int adamnet_is_key_available(void)
{
    return (g_key_buffer_head != g_key_buffer_tail);
}


// --- AdamNet Hooks (aangeroepen door coleco.cpp) ---


/** GetDCB() *************************************************/
/** Get DCB byte at given offset.                           **/
/*************************************************************/
byte GetDCB(byte Dev,byte Offset)
{
    word A = (PCBAddr+PCB_SIZE+Dev*DCB_SIZE+Offset)&0xFFFF;
    return(RAM_Memory[A]);
}

word GetDCBBase(byte Dev)
{
    return(GetDCB(Dev,DCB_BA_LO)+((word)GetDCB(Dev,DCB_BA_HI)<<8));
}

word GetDCBLen(byte Dev)
{
    return(GetDCB(Dev,DCB_BUF_LEN_LO)+((word)GetDCB(Dev,DCB_BUF_LEN_HI)<<8));
}

unsigned int GetDCBSector(byte Dev)
{
    return(
        GetDCB(Dev,DCB_SEC_NUM_0)
        + ((unsigned int)GetDCB(Dev,DCB_SEC_NUM_1)<<8)
        + ((unsigned int)GetDCB(Dev,DCB_SEC_NUM_2)<<16)
        + ((unsigned int)GetDCB(Dev,DCB_SEC_NUM_3)<<24)
        );
}

/** GetPCB() *************************************************/
/** Get PCB byte at given offset.                           **/
/*************************************************************/
byte GetPCB(word Offset)
{
    word A = (PCBAddr+Offset)&0xFFFF;
    return(RAM_Memory[A]);
}

word GetPCBBase(void)
{
    return(GetPCB(PCB_BA_LO)+((word)GetPCB(PCB_BA_HI)<<8));
}

word GetMaxDCB(void)
{
    return(GetPCB(PCB_MAX_DCB));
}

/** SetDCB() *************************************************/
/** Set DCB byte at given offset.                           **/
/*************************************************************/
void SetDCB(byte Dev,byte Offset,byte Value)
{
    word A = (PCBAddr+PCB_SIZE+Dev*DCB_SIZE+Offset)&0xFFFF;

    RAM_Memory[A] = Value;
}

/** SetPCB() *************************************************/
/** Set PCB byte at given offset.                           **/
/*************************************************************/
void SetPCB(word Offset,byte Value)
{
    word A = (PCBAddr+Offset)&0xFFFF;
    RAM_Memory[A] = Value;
}

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

    for(J=0;J<=MaxDCB;++J)
    {
        SetDCB(J,DCB_DEV_NUM,0);
        SetDCB(J,DCB_ADD_CODE,J);
    }
}

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

// --- adamnet.cpp ---

/** PutKBD() *************************************************/
/** Voeg ASCII-toets toe aan de (oude) KBD-buffer.          **/
/*************************************************************/
void PutKBD(unsigned int Key)
{
//     if (Key & 0x80) {
//         // release: 0xC1 voor 'A' → basis = 0x41
//         byte baseKey = (byte)(Key & 0x7F);
//         if (baseKey == LastKey) LastKey = 0x00;
//         return;
//     }
//     LastKey = (byte)Key;    // press
// KBDStatus = (byte)(RSP_STATUS | 0x0C);

// We gebruiken de bestaande logica om de "LastKey" status te beheren
// voor backward compatibility in GetKBD, maar sturen ook naar de queue.

// --- OUDE LASTKEY LOGICA ---
// Dit deel is puur voor ASCII-toetsen (A-Z, 0-9, enz.).

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
        qDebug() << "SCANCODE:" << Qt::hex << sc;
        return sc;
    }
    if (LastKey != 0) {
        qDebug() << "ASCII:" << Qt::hex << LastKey;
    }

    //return 0x00;

    // 2. Als die leeg is, check de ASCII LastKey (voor '9', 'A', etc.)
    byte Result = LastKey;
    LastKey = 0x00;
    return(Result);

}

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
        //SetDCB(Dev,DCB_CMD_STAT,KBDStatus);
        break;
    }
}

void UpdatePRN(byte Dev,int V)
{

    int N;
    word A;

    switch(V)
    {
    case CMD_STATUS:
    case CMD_SOFT_RESET:
        /* Character-based device, single character buffer */
        ReportDevice(Dev,0x0001,0);
        break;
    case CMD_READ:
        SetDCB(Dev,DCB_CMD_STAT,RSP_ACK+0x0B);
        break;
    case CMD_WRITE:
    {
        SetDCB(Dev,DCB_CMD_STAT,0x00);
        A = GetDCBBase(Dev);
        N = GetDCBLen(Dev);

        if (N > 0) {
            // Kopieer in een tijdelijke buffer en stuur door
            // (klein en veilig; kan ook in stukken als je wil)
            static uint8_t s_buf[1024];
            while (N > 0) {
                int chunk = N > (int)sizeof(s_buf) ? (int)sizeof(s_buf) : N;
                for (int j = 0; j < chunk; ++j, A=(A+1)&0xFFFF)
                    s_buf[j] = RAM_Memory[A];
                adam_printer_chunk(s_buf, chunk);
                N -= chunk;
            }
        }

        // (void)A;
        // (void)N;
        // //for(J=0 ; J<N ; ++J, A=(A+1)&0xFFFF)
        // //Printer(RAM(A));
        // // 1) Stuur de bytes naar de UI (indien een sink is geïnstalleerd)
        // if (g_printer_sink && N > 0) {
        //     g_printer_sink(reinterpret_cast<const char*>(&RAM_Memory[A]), N);
        //}

        // 2) (optioneel) bestaand gedrag zoals file-capture laten staan
        // prn_write_bytes(&RAM_Memory[A], N);

        // Klaar
        SetDCB(Dev, DCB_CMD_STAT, 0x00);  // ACK
    }
        break;
    default:
        SetDCB(Dev,DCB_CMD_STAT,RSP_STATUS);
        break;
    }
}

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
                UpdateDCB_CPM(Dev, -1); // Deze functie update de status in RAM
            else
                UpdateDCB_EOS(Dev, -1); // Deze functie update de status in RAM

            // Retourneer de zojuist geüpdatete DCB-status
            //return GetDCB(Dev, DCB_CMD_STAT);
        }
    }

    // Fallback: Als het geen status-read was, retourneer de
    // onbewerkte byte uit het PCB-geheugen (bv. BA_LO, BA_HI).
    //return RAM_Memory[PCBAddr + A];
    //qDebug() << "ReadPCB at" << Qt::hex << A;
}

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
                UpdateDCB_CPM(Dev,V);
            else
                UpdateDCB_EOS(Dev,V); }

    }
}

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

            // DEBUG: Zie of de game de status opvraagt
            if (retval & 0x01) {
                //qDebug() << "[DEBUG-IO] Z80 leest STATUS E0: DATA KLAAR (0x01). Retval:" << Qt::hex << retval;
            }
            if (!m_cpm_enabled)
            {
            PCBTable[0] &= ~0x01; // Wis Bit 0: Data-In Full
            }
        }
        return retval;
}
