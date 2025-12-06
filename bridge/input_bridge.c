#include "input_bridge.h"

volatile uint8_t  ib_joy1_dir  = 0;
volatile uint8_t  ib_joy1_btn  = 0;
volatile uint16_t ib_keypad1   = 0; // elke bit = 1 wanneer ingedrukt

volatile int16_t  ib_analog_x1 = 0;
volatile uint8_t ib_paddle_mode = 0;

void ib_set_joy1_dir(uint8_t mask, int pressed) {
    if (pressed) ib_joy1_dir |= mask;
    else         ib_joy1_dir &= (uint8_t)~mask;
}

void ib_set_joy1_btn(uint8_t mask, int pressed) {
    if (pressed) ib_joy1_btn |= mask;
    else         ib_joy1_btn &= (uint8_t)~mask;
}

void ib_set_keypad_bit(int index, int pressed) {
    if (index < 0 || index > 11) return;
    uint16_t bit = (uint16_t)1u << index;
    if (pressed) ib_keypad1 |= bit;
    else         ib_keypad1 &= (uint16_t)~bit;
}

void ib_set_analog_x1(int16_t value) {
    ib_analog_x1 = value;
}
