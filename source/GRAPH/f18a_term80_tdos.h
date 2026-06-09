#ifndef F18A_TERM80_TDOS_H
#define F18A_TERM80_TDOS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * F18A TERM80 T-DOS bridge.
 *
 * This module is a separated copy/port of the existing ScreenWidget T-DOS
 * 80-column buffer logic. It does not use QPainter and does not depend on
 * cpm80.cpp. It reads the T-DOS 80-column RAM/VDP state and writes the result
 * cell-by-cell into the common F18A TERM80 framebuffer.
 */
void     f18a_term80_tdos_reset(void);
void     f18a_term80_tdos_before_opcode(void);
uint8_t  f18a_term80_tdos_sync_now(void);
uint8_t  f18a_term80_tdos_is_active(void);
uint16_t f18a_term80_tdos_buffer_addr(void);

/*
 * CP/M 80-column virtual terminal.
 *
 * Formerly declared in f18a_tdos80.h. This code traps CP/M BIOS console
 * routines and keeps its own 80x23 text buffer. When smartkeys are detected,
 * ScreenWidget reserves rows 22/23 for them.
 */
void     cpm80_reset(void);
void     cpm80_disable(void);
void     cpm80_clear_screen(void); /* Clears virtual terminal and restores last known CP/M prompt. */
void     cpm80_before_opcode(uint16_t pc, uint8_t reg_c);

/* Manual CP/M80 colors. enabled=0 means ScreenWidget may use automatic sampled colors. */
void     cpm80_set_fixed_colors(uint8_t enabled, uint8_t fg, uint8_t bg);
uint8_t  cpm80_fixed_colors_enabled(void);
uint8_t  cpm80_get_fixed_fg(void);
uint8_t  cpm80_get_fixed_bg(void);

/* Paste/edit support used by the CP/M80 right-click popup. */
void     cpm80_queue_paste_text(const char* text);
uint8_t  cpm80_paste_backspace(void);
uint8_t  cpm80_paste_commit(void);
uint8_t  cpm80_paste_pending(void);

uint8_t  cpm80_is_active(void);
uint8_t  cpm80_get_char(int row, int col);
uint8_t  cpm80_get_color(int row, int col);
int      cpm80_get_visible_rows(void);
uint8_t  cpm80_get_cursor_x(void);
uint8_t  cpm80_get_cursor_y(void);
uint16_t cpm80_get_conout_addr(void);
uint8_t  cpm80_has_smartkeys(void);
const char* cpm80_get_smartkey_text(int index);

#ifdef __cplusplus
}
#endif

#endif /* F18A_TERM80_TDOS_H */
