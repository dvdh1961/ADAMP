// coleco_io.h
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
