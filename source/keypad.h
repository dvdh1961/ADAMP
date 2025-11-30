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

#pragma once
#include <cstdint>

enum class ColecoStrobe : uint8_t { Joystick = 0, Keypad = 1 };

struct ColecoControllerState {
    // Actief-hoog intern, we inverteren pas bij het IN-resultaat.
    bool up = false, down = false, left = false, right = false;
    bool fireL = false, fireR = false;
    // Keypad: -1 = niets; 0..9,10='*',11='#'
    int keypad = -1;
};

void coleco_setController(int idx, const ColecoControllerState& s); // 0=port1, 1=port2
uint8_t coleco_io_read(uint8_t port);   // Z80 IN
void    coleco_io_write(uint8_t port, uint8_t value); // Z80 OUT
