#ifndef F18A_TERM80_CPM_H
#define F18A_TERM80_CPM_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * F18A TERM80 CP/M output bridge.
 *
 * Safe step: output only.
 * - Does NOT use cpm80.cpp screen buffer.
 * - Does NOT draw PNG/smartkey overlay.
 * - Does NOT hook keyboard input yet.
 * - Only mirrors CP/M BIOS CONOUT characters to f18a_term80_put_char().
 */
void f18a_term80_cpm_before_opcode(uint16_t pc, uint8_t reg_c);
void f18a_term80_cpm_reset(void);
void f18a_term80_cpm_force_smartkey_rescan(void);
uint8_t f18a_term80_cpm_is_active(void);

uint8_t f18a_term80_cpm_has_smartkeys(void);
const char* f18a_term80_cpm_get_smartkey_text(int index);

#ifdef __cplusplus
}
#endif

#endif // F18A_TERM80_CPM_H
