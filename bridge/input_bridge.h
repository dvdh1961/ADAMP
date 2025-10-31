#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// D-pad bits
enum { IB_UP=1<<0, IB_DOWN=1<<1, IB_LEFT=1<<2, IB_RIGHT=1<<3 };
// Buttons
enum { IB_BTN1=1<<4, IB_BTN2=1<<5 };

// Globale, volatiele states (worden door Qt gezet)
extern volatile uint8_t  ib_joy1_dir;   // IB_UP/DOWN/LEFT/RIGHT combi
extern volatile uint8_t  ib_joy1_btn;   // IB_BTN1/BTN2 combi
extern volatile uint16_t ib_keypad1;    // bitmask 0..11 (0..9,*,#): 1=ingedrukt

// Setters (idempotent)
void ib_set_joy1_dir(uint8_t mask, int pressed);
void ib_set_joy1_btn(uint8_t mask, int pressed);
void ib_set_keypad_bit(int index, int pressed); // index: 0..9=digits,10='*',11='#'

#ifdef __cplusplus
}
#endif
