/* ADAMP_EMU  - A Windows ColecoVision emulator.
 * Copyright (C) 2025 DannyVdH
 *
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
 */

#include "keypad.h"

static ColecoStrobe s_strobe = ColecoStrobe::Joystick;
static volatile ColecoControllerState s_pad[2]{};
// Keypad diode-encoder naar 4-bit code (actief-laag op bus).
// Tabel komt rechtstreeks uit de veld-notities / service docs.
// index: 0..9, 10='*', 11='#'  | waarde: 0..15 nibble die LAAG wordt bij IN
static const uint8_t KEYPAD_NIBBLE[12] = {
    /* 0 */ 0x5,
    /* 1 */ 0x2,
    /* 2 */ 0x8,
    /* 3 */ 0x3,
    /* 4 */ 0xD,
    /* 5 */ 0xC,
    /* 6 */ 0x1,
    /* 7 */ 0xA,
    /* 8 */ 0xE,
    /* 9 */ 0x4,
    /* * */ 0x6,
    /* # */ 0x9
};

// NB: Sommige bronnen tonen kleine variaties; deze tabel volgt de bekende “CV-Tech” mapping.
// Voel je vrij om per game te patchen als iets afwijkt.

void coleco_setController(int idx, const ColecoControllerState& s) {
//    if (idx >= 0 && idx < 2) s_pad[idx] = s;
    if (idx >= 0 && idx < 2) {
        // Wijs elk veld handmatig toe.
        // Dit omzeilt de C++ operator= en werkt met volatile.
        s_pad[idx].up = s.up;
        s_pad[idx].down = s.down;
        s_pad[idx].left = s.left;
        s_pad[idx].right = s.right;
        s_pad[idx].fireL = s.fireL;
        s_pad[idx].fireR = s.fireR;
        s_pad[idx].keypad = s.keypad;
    }
}

static uint8_t read_controller_bits(int idx) {
    const auto& c = s_pad[idx];
    // 7 lijnen actief-laag, bit7 ongebruikt blijft 1
    uint8_t v = 0x7F;

    if (s_strobe == ColecoStrobe::Joystick) {
        // JOYSTICK-stand: bit6 = Left Fire, bits0..3 = U,R,D,L (actief-laag)
        if (c.fireL) v &= ~(1u << 6);
        if (c.up)    v &= ~(1u << 0);
        if (c.right) v &= ~(1u << 1);
        if (c.down)  v &= ~(1u << 2);
        if (c.left)  v &= ~(1u << 3);
    } else {
        // KEYPAD-stand: bit6 = Right Fire, bits0..3 = keypad-nibble (actief-laag)
        if (c.fireR) v &= ~(1u << 6);

        if (c.keypad >= 0 && c.keypad <= 11) {
            uint8_t nibble = KEYPAD_NIBBLE[c.keypad] & 0x0F;
             // actief-laag in de bus (nib = index van lijn die laag moet)
            v = (v & 0xF0) | (~nibble & 0x0F);
        }
    }
    // bit7=1, lijnen actief-laag in 0..3 en 6
    return v;
}

uint8_t coleco_io_read(uint8_t port) {
    // smalle decode
    if (port == 0xFC || port == 0xBF || port == 0xBE) return read_controller_bits(0);
    if (port == 0xFF) return read_controller_bits(1);

    // brede decode: 0xE0..0xE3 → A1 (bit1) selecteert pad
    if ((port & 0xE0) == 0xE0) {
        int pad = (port & 0x02) ? 1 : 0;   // A1=1 → pad2, A1=0 → pad1
        return read_controller_bits(pad);
    }

    // (optioneel, tolerant) IN op strobe-ranges aliasen:
    if ((port & 0xE0) == 0x80) return read_controller_bits(0);
    if ((port & 0xE0) == 0xC0) return read_controller_bits(1);

    return 0xFF;
}

void coleco_io_write(uint8_t port, uint8_t /*value*/) {
    // alleen de stand instellen
    if ((port & 0xE0) == 0x80) s_strobe = ColecoStrobe::Keypad;
    else if ((port & 0xE0) == 0xC0) s_strobe = ColecoStrobe::Joystick;
}
