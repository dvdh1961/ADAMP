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
#include <QMetaObject>

#include "6801/adnet_core.h"
#include "CORE/cv.h"
#include "CORE/z80.h"
#include "screenwidget.h"
#include "printwindow.h"
#include "GRAPH/f18a.h"

#include <cstring>
#include <stdint.h>

#define RAM(A)  (RAM_Memory[A])

// Game mode flag: true = Adam games (scancodes), false = Writer/BASIC (ASCII)
static bool g_force_game_mode = false;

// Flag to track if F000 area has been cleared after boot
static bool g_vdp_cleared = false;

static int g_block_ascii_fkeys = 0;  // countdown tegen T..Y die nog via PutKBD zouden lekken

volatile uint8_t g_key_buffer[KEY_BUFFER_SIZE] = {0};
volatile uint8_t g_key_buffer_head = 0;
volatile uint8_t g_key_buffer_tail = 0;

/* Ordinary host keyboard input used to live in the single LastKey byte.  A
 * second key arriving while EOS/FujiNet was still processing the first one
 * silently overwrote it.  Keep ASCII input in its own FIFO; LastKey remains a
 * compatibility mirror of the oldest queued byte for code outside this file.
 */
static constexpr uint8_t ASCII_KEY_BUFFER_SIZE = 32;
static uint8_t g_ascii_key_buffer[ASCII_KEY_BUFFER_SIZE] = {0};
static uint8_t g_ascii_key_buffer_head = 0;
static uint8_t g_ascii_key_buffer_tail = 0;

static bool ascii_key_available(void)
{
    return g_ascii_key_buffer_head != g_ascii_key_buffer_tail;
}

static void update_last_key_mirror(void)
{
    LastKey = ascii_key_available()
        ? g_ascii_key_buffer[g_ascii_key_buffer_tail] : 0;
}

static bool queue_ascii_key(uint8_t key)
{
    const uint8_t next = uint8_t(
        (g_ascii_key_buffer_head + 1) % ASCII_KEY_BUFFER_SIZE);
    if (next == g_ascii_key_buffer_tail)
        return false;
    g_ascii_key_buffer[g_ascii_key_buffer_head] = key;
    g_ascii_key_buffer_head = next;
    update_last_key_mirror();
    return true;
}

static uint8_t dequeue_ascii_key(void)
{
    if (!ascii_key_available())
        return 0;
    const uint8_t key = g_ascii_key_buffer[g_ascii_key_buffer_tail];
    g_ascii_key_buffer_tail = uint8_t(
        (g_ascii_key_buffer_tail + 1) % ASCII_KEY_BUFFER_SIZE);
    update_last_key_mirror();
    return key;
}

/*
 * Some EOS programs issue one keyboard READ and keep polling that same DCB
 * until a key arrives.  The original handler completed an empty READ at once,
 * which works for software that repeatedly submits READ commands but leaves
 * a blocking client such as the FujiNet loader waiting forever.
 */
static bool g_pending_keyboard_read = false;
static byte g_pending_keyboard_dev = 0;
static word g_pending_keyboard_buffer = 0;
static word g_pending_keyboard_length = 0;

// A command reply belongs to its DCB, not to the next queued host key.
// Keep completed replies stable until the guest submits a new command.
static byte g_keyboard_reply[256] = {0};
static bool g_keyboard_reply_valid[256] = {false};
static unsigned g_keyboard_trace_lines = 0;
static bool keyboard_trace_enabled(void)
{
    // Bound diagnostics so tight BIOS polling cannot flood the GUI logger.
    if (g_keyboard_trace_lines >= 2000)
        return false;
    ++g_keyboard_trace_lines;
    return true;
}


static void set_keyboard_reply(byte dev, byte reply)
{
    g_keyboard_reply[dev] = reply;
    g_keyboard_reply_valid[dev] = true;
    SetDCB(dev, DCB_CMD_STAT, reply);
}

// Diagnostic version 4: watch actual character deliveries, not idle polls.
// Logging remains readable even after the bounded boot diagnostics expire.
struct KeyboardDeliveryWatch {
    bool active;
    word address;
    byte key;
    unsigned sequence;
};
static KeyboardDeliveryWatch g_keyboard_watches[16] = {};
static unsigned g_keyboard_delivery_sequence = 0;
static unsigned g_keyboard_watch_count = 0;
static byte g_keyboard_last_command[256] = {};

static void watch_keyboard_delivery(byte dev, word address, byte key)
{
    for (auto &watch : g_keyboard_watches) {
        if (watch.active && watch.address == address) {
            qDebug() << "[KBD4] replaced before observed buffer read; seq="
                     << watch.sequence << "address=" << Qt::hex << int(address)
                     << "previous-key=" << int(watch.key) << "next-key=" << int(key);
            watch.active = false;
            --g_keyboard_watch_count;
        }
    }
    const unsigned sequence = ++g_keyboard_delivery_sequence;
    auto &watch = g_keyboard_watches[sequence % 16];
    if (!watch.active)
        ++g_keyboard_watch_count;
    watch = {true, address, key, sequence};
    qDebug() << "[KBD4] DELIVER seq=" << sequence
             << "DCB=" << int(dev) << "address=" << Qt::hex << int(address)
             << "key=" << int(key) << "PC=" << int(Z80.pc.w.l)
             << "port60=" << int(coleco_port60) << "RAMPage=" << int(RAMPage)
             << "requested=" << Qt::dec << int(GetDCBLen(dev));
}

static bool complete_pending_keyboard_read(void);

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
extern "C" bool adamnet_is_game_mode(void) { return g_force_game_mode; }
//--------------------------------------------------------------------------------------
extern "C" void adamnet_block_ascii_fkeys(int count)
{
    if (count < 0) count = 0;
    g_block_ascii_fkeys = count;
}
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
    // // FG1..FG6 remap + F7..F10
    // if ((key_code & 0x7F) >= 0x54 && (key_code & 0x7F) <= 0x5D) {

    //     uint8_t idx = (key_code & 0x7F) - 0x54;    // 0..7
    //     if (idx<6)
    //         mapped = 0x81 + idx;               // MAKE = 0xB4..0xB9
    //     else
    //         if (idx==6) mapped = 0X93; // F7
    //     else
    //         if (idx==7) mapped = 0x95; // F8
    //     else
    //         if (idx==8) mapped = 0x96; // F9
    //     else
    //         if (idx==9) mapped = 0x97; // F10

    //     if (key_code & 0x80){
    //          mapped = mapped ^ 0x80;
    //     }
    //     key_code = mapped;
    // }

    const bool isRelease = (key_code & 0x80) != 0;
    const uint8_t rawKey = key_code & 0x7F;

    // FG1..FG6 + F7..F10 zitten op raw 0x54..0x5D.
    // In CP/M gebruiken we die als macro/smartkey.
    // Release niet doorsturen, anders krijg je ^A, ^B, ^C...
    if (m_cpm_enabled && isRelease && rawKey >= 0x54 && rawKey <= 0x5D)
    {
        return;
    }

    // FG1..FG6 remap + F7..F10
    if (rawKey >= 0x54 && rawKey <= 0x5D)
    {
        uint8_t idx = rawKey - 0x54;    // 0..9

        if (idx < 6)
            mapped = 0x81 + idx;        // F1..F6
        else if (idx == 6)
            mapped = 0x93;              // F7
        else if (idx == 7)
            mapped = 0x95;              // F8
        else if (idx == 8)
            mapped = 0x96;              // F9
        else if (idx == 9)
            mapped = 0x97;              // F10

        if (isRelease)
        {
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
        if (!g_keyboard_reply_valid[1] && (!m_cpm_enabled || m_tdos_enabled))
            SetDCB(1, DCB_CMD_STAT, KBDStatus);
        // 3. ZET DE I/O VLAG (Voor poort 0xE0 polling)
        // AN_STAT_DIF (0x01) betekent: "Er zit data in de Host Adapter voor de CPU"
       // PCBTable[0] |= 0x01;

        /* If EOS already has a blocking READ open, deliver this key directly
         * through that DCB instead of requiring a second READ command.
         */
        // CP/M may replace a speculative CONST READ before CONIN uses it.
        // Do not consume a host key until the guest polls or issues a READ.
        if (!m_cpm_enabled || m_tdos_enabled)
            complete_pending_keyboard_read();
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

/** PutKBD() *************************************************/
/** Voeg ASCII-toets toe aan de (oude) KBD-buffer.          **/
/*************************************************************/
void PutKBD(unsigned int Key)
{
if (Key & 0x80) {
    /* Releases must not erase a press which EOS has not consumed yet. */
    return;
} else {
    if (!queue_ascii_key(byte(Key))) {
        qWarning() << "[KBD] ASCII FIFO full; key dropped="
                   << Qt::hex << byte(Key);
        return;
    }
}

qDebug() << "[KBD4] QUEUED key=" << Qt::hex << int(byte(Key))
         << "pending=" << g_pending_keyboard_read
         << "DCB=" << int(g_pending_keyboard_dev)
         << "last-command=" << int(g_keyboard_last_command[g_pending_keyboard_dev]);

// De KBDStatus moet worden bijgewerkt zodra de ASCII FIFO data bevat.
KBDStatus = (byte)(RSP_STATUS | 0x0C);

/* EOS/TDOS complete blocking reads asynchronously. CP/M waits for the
 * guest's DCB poll so an obsolete speculative READ cannot swallow the key. */
if (!(Key & 0x80) && (!m_cpm_enabled || m_tdos_enabled))
    complete_pending_keyboard_read();
}
//--------------------------------------------------------------------------------------
/** GetKBD() *************************************************/
/** Haal éérst AdamNet-scancodes, daarna host-ASCII FIFO.  **/
/*************************************************************/
byte GetKBD()
{
    extern BYTE RAM_Memory[];
    extern BYTE VDP_Memory[];

    // PATCH wissen rommel in scherm bij opstart T-Dos bios
    // if (m_tdos_enabled && !m_80colEnabled)
    // {
    //     if (g_vdp_cleared == false) {
    //         memset(RAM_Memory + 0xF900, 0, 0x284);
    //     }
    //     if (VDP_Memory[0x3747]==0x00 || VDP_Memory[0x3747]==0x20  || VDP_Memory[0x3747]==0xff) g_vdp_cleared = true;
    //     else g_vdp_cleared = false;
    // }
    if (m_tdos_enabled && !m_80colEnabled)
    {
        unsigned char checkByte = 0;

        if (coleco_vdp_has_f18a()) {
            // F18A gebruikt eigen VRAM-buffer
            checkByte = f18a_peek_vram(0x3747);
        } else {
            // Klassieke TMS route
            checkByte = VDP_Memory[0x3747];
        }

        if (g_vdp_cleared == false) {
            memset(RAM_Memory + 0xF900, 0, 0x284);
        }

        if (checkByte == 0x00 || checkByte == 0x20 || checkByte == 0xFF)
            g_vdp_cleared = true;
        else
            g_vdp_cleared = false;
    }


    if (adamnet_is_key_available())
    {
        byte sc = adamnet_dequeue_key();
       // qDebug() << "SCANCODE:" << Qt::hex << sc;
        return sc;
    }
    const byte asciiKey = dequeue_ascii_key();
    if (asciiKey && keyboard_trace_enabled())
        qDebug() << "[KBD] ASCII consumed=" << Qt::hex << int(asciiKey);
    if (asciiKey==0x1B) // Escape gedrukt
        {
        g_prn_in_wp = true; // Printer in wordprocessor

        PrintWindow* w = PrintWindow::instance();
            if (w) {
                    QMetaObject::invokeMethod(w, "updatePrinterMode", Qt::QueuedConnection, Q_ARG(bool, g_prn_in_wp));
            }

            g_prn_line_counter = 0;
        }
    // 2. Als die leeg is, haal de oudste gewone ASCII-toets uit de FIFO.
    return asciiKey;

}

//--------------------------------------------------------------------------------------
/* Complete a previously blocked keyboard READ when at least one key exists. */
static bool complete_pending_keyboard_read(void)
{
    if (!g_pending_keyboard_read)
        return false;
    if (!adamnet_is_key_available() && !ascii_key_available())
        return false;

    word address = g_pending_keyboard_buffer;
    int transferred = 0;
    const int requested = g_pending_keyboard_length;
    byte key = 0;

    while (transferred < requested && (key = GetKBD()) != 0) {
        RAM_Memory[address] = key;
        watch_keyboard_delivery(g_pending_keyboard_dev, address, key);
        address = (address + 1) & 0xFFFF;
        ++transferred;
    }

    const byte device = g_pending_keyboard_dev;
    g_pending_keyboard_read = false;
    g_pending_keyboard_length = 0;
    KBDStatus = byte(RSP_STATUS | (transferred < requested ? 0x0C : 0x00));
    set_keyboard_reply(device, KBDStatus);

    /* Notify EOS through ADAMnet port E0 that the asynchronous READ changed
     * from BUSY to complete. adamnet_read_io() acknowledges and clears bit 0.
     * Restrict this notification to the pending-READ path so established
     * Writer/BASIC keyboard polling remains unchanged.
     */
    PCBTable[0] |= AN_STAT_DIF;

    qDebug() << "[KBD] pending READ completed; DCB=" << device
             << "key=" << Qt::hex << int(key)
             << "bytes=" << Qt::dec << transferred
             << "data-in-full=1";
    return true;
}

void AdamNetKbdDiagnosticMemoryRead(word Address)
{
    if (!g_keyboard_watch_count || !m_cpm_enabled || m_tdos_enabled)
        return;
    for (auto &watch : g_keyboard_watches) {
        if (!watch.active || watch.address != Address)
            continue;
        watch.active = false;
        --g_keyboard_watch_count;
        const byte *mapped = MemoryMap[Address >> 13] + (Address & 0x1FFF);
        qDebug() << "[KBD4] BUFFER READ seq=" << watch.sequence
                 << "address=" << Qt::hex << int(Address)
                 << "expected=" << int(watch.key)
                 << "base-value=" << int(RAM_Memory[Address])
                 << "mapped-value=" << int(*mapped)
                 << "base-bank=" << (mapped == RAM_Memory + Address)
                 << "PC=" << int(Z80.pc.w.l)
                 << "port60=" << int(coleco_port60) << "RAMPage=" << int(RAMPage);
    }
}

void ResetKBDPendingRead(void)
{
    g_pending_keyboard_read = false;
    g_pending_keyboard_dev = 0;
    g_pending_keyboard_buffer = 0;
    g_pending_keyboard_length = 0;
    g_ascii_key_buffer_head = 0;
    g_ascii_key_buffer_tail = 0;
    LastKey = 0;
    std::memset(g_keyboard_reply_valid, 0, sizeof(g_keyboard_reply_valid));
    g_keyboard_trace_lines = 0;
    g_keyboard_delivery_sequence = 0;
    g_keyboard_watch_count = 0;
    std::memset(g_keyboard_watches, 0, sizeof(g_keyboard_watches));
    std::memset(g_keyboard_last_command, 0, sizeof(g_keyboard_last_command));
}
//--------------------------------------------------------------------------------------
/** UpdateKBD() **********************************************/
void UpdateKBD(byte Dev,int V)
{
    int J,N;
    word A;

    if (V >= 0) {
        g_keyboard_last_command[Dev] = byte(V);
        g_keyboard_reply_valid[Dev] = false;
        if ((V != CMD_STATUS || ascii_key_available() || adamnet_is_key_available())
                && keyboard_trace_enabled())
        qDebug() << "[KBD] poll-delivery=v4 command; DCB=" << int(Dev)
                 << "cmd=" << Qt::hex << V
                 << "buffer=" << int(GetDCBBase(Dev))
                 << "requested=" << Qt::dec << int(GetDCBLen(Dev))
                 << "ascii-ready=" << ascii_key_available()
                 << "scancode-ready=" << adamnet_is_key_available();
    }

    // A new command on this DCB supersedes its previous blocking READ.
    // STATUS must never leave an old buffer armed: the next host key would
    // otherwise be consumed by that obsolete READ before the new READ sees it.
    // Polling (-1) is not a new command and must keep the blocking READ alive.
    if (V >= 0 && g_pending_keyboard_read && g_pending_keyboard_dev == Dev) {
        g_pending_keyboard_read = false;
        g_pending_keyboard_length = 0;
    }

    switch(V)
    {
    case -1:
        if (g_pending_keyboard_read && g_pending_keyboard_dev == Dev) {
            if (!complete_pending_keyboard_read())
                SetDCB(Dev, DCB_CMD_STAT, 0x00);
            break;
        }
        if (g_keyboard_reply_valid[Dev])
            SetDCB(Dev, DCB_CMD_STAT, g_keyboard_reply[Dev]);
        else
            SetDCB(Dev, DCB_CMD_STAT, KBDStatus);
        break;
    case CMD_STATUS:
    case CMD_SOFT_RESET:
    {
        if (V == CMD_SOFT_RESET)
            ResetKBDPendingRead();

        // Is er een key?
        const int ready = adamnet_is_key_available() || ascii_key_available();
        KBDStatus = (byte)(RSP_STATUS | (ready ? 0x0C : 0x00));

        // qDebug() << "[KBD_STATUS] Rdy:" << ready
        //          << " Status:" << Qt::hex << KBDStatus
        //          << " Queue Size:" << (g_key_buffer_head - g_key_buffer_tail);

        ReportDevice(Dev,0x0001,0);

        // STATUS is a live readiness query; only completed READ/WRITE replies
        // are latched. Programs may keep polling this query for a new key.
        KBDStatus = (byte)(RSP_STATUS | (ready ? 0x0C : 0x00));
        SetDCB(Dev, DCB_CMD_STAT, KBDStatus);
    }
    break;
    case CMD_WRITE:
        set_keyboard_reply(Dev, RSP_ACK+0x0B);
        KBDStatus = RSP_STATUS;
        break;
    case CMD_READ:
        SetDCB(Dev,DCB_CMD_STAT,0x00);
        A = GetDCBBase(Dev);
        N = GetDCBLen(Dev);

        /* EOS permits a character device READ with a zero requested length.
         * For the keyboard this means "wait for one character"; the device's
         * reported maximum message size is one byte.  Treating zero as an
         * immediate empty completion leaves the FujiNet loader waiting on a
         * buffer that can never receive its key.
         */
        if (N <= 0)
            N = 1;

        if (!adamnet_is_key_available() && !ascii_key_available()) {
            g_pending_keyboard_read = true;
            g_pending_keyboard_dev = Dev;
            g_pending_keyboard_buffer = A;
            g_pending_keyboard_length = word(N);
            KBDStatus = 0x00;
            break;
        }

        for(J=0 ; (J<N) && (V=GetKBD()) ; ++J, A=(A+1)&0xFFFF)
        {
            RAM_Memory[A] = V;
            watch_keyboard_delivery(Dev, A, byte(V));
        }
        KBDStatus = RSP_STATUS+(J<N? 0x0C:0x00);
        set_keyboard_reply(Dev, KBDStatus);
        if (keyboard_trace_enabled())
        qDebug() << "[KBD] READ completed; DCB=" << int(Dev)
                 << "buffer=" << Qt::hex << int(GetDCBBase(Dev))
                 << "requested=" << Qt::dec << N << "bytes=" << J
                 << "reply=" << Qt::hex << int(KBDStatus);
        break;
    }
}
