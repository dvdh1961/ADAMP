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

     // ADAM Writer cursor scancodes.
     // Deze codes hebben bit 7 gezet, maar zijn MAKE-codes,
     // geen release-events.
     if (key_code == 0xA0 || key_code == 0xA1 || key_code == 0xA2 || key_code == 0xA3)
     {
         uint8_t next_head = (g_key_buffer_head + 1) % KEY_BUFFER_SIZE;

         if (next_head != g_key_buffer_tail)
         {
             g_key_buffer[g_key_buffer_head] = key_code;
             g_key_buffer_head = next_head;

             KBDStatus = (byte)(RSP_STATUS | 0x0C);
             SetDCB(1, DCB_CMD_STAT, KBDStatus);
         }

         return;
     }

     // Cursor key releases negeren.
     // Anders komen 0xA0|0x80 enz. terug als rare codes.
     if (key_code == 0x20 || key_code == 0x21 || key_code == 0x22 || key_code == 0x23)
     {
         return;
     }

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
        SetDCB(1, DCB_CMD_STAT, KBDStatus);
        // 3. ZET DE I/O VLAG (Voor poort 0xE0 polling)
        // AN_STAT_DIF (0x01) betekent: "Er zit data in de Host Adapter voor de CPU"
       // PCBTable[0] |= 0x01;
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

        if (coleco_get_vdp_type() == COLECO_VDP_F18A) {
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

        // qDebug() << "[KBD_STATUS] Rdy:" << ready
        //          << " Status:" << Qt::hex << KBDStatus
        //          << " Queue Size:" << (g_key_buffer_head - g_key_buffer_tail);

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
