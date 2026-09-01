#ifndef F18A_TERM80_H
#define F18A_TERM80_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * F18A TERM80 - separate 80-column terminal layer.
 *
 * This module does NOT use cpm80.cpp and does NOT draw any PNG overlay.
 * It owns the 80x24 character buffer and the code that draws it - both moved here out
 * of f18a.c, so that CP/M and T-DOS no longer need the F18A renderer to be the engine.
 * Whichever engine is rendering presents it through vdp_bridge_set_overlay_hook().
 *
 * Later CP/M and T-DOS CONOUT hooks can feed characters into
 * f18a_term80_put_char().
 */

#define F18A_TERM80_COLS 80u
#define F18A_TERM80_ROWS 24u

/* Drop the 80-column display buffer back to its unwritten state. Called from the VDP
   reset, which used to clear the flag itself when the buffer lived in f18a.c. */
void f18a_term80_display_reset(void);

void f18a_term80_set_enabled(int enabled);
int  f18a_term80_is_enabled(void);

void f18a_term80_reset(void);
void f18a_term80_clear(void);

void f18a_term80_set_colors(unsigned char fg, unsigned char bg);
void f18a_term80_apply_colors(unsigned char fg, unsigned char bg);
void f18a_term80_get_colors(unsigned char* fg, unsigned char* bg);

void f18a_term80_set_cursor(unsigned int row, unsigned int col);
void f18a_term80_get_cursor(unsigned int* row, unsigned int* col);

void f18a_term80_show_cursor(int show);
int  f18a_term80_cursor_visible(void);
void f18a_term80_tick(void);

void f18a_term80_delete_previous_char(void);
void f18a_term80_put_char(unsigned char ch);
void f18a_term80_write_text(const char* text);

unsigned char f18a_term80_get_char(unsigned int row, unsigned int col);
unsigned char f18a_term80_get_fg(unsigned int row, unsigned int col);
unsigned char f18a_term80_get_bg(unsigned int row, unsigned int col);
void f18a_term80_put_cell(unsigned int row, unsigned int col,
                          unsigned char ch,
                          unsigned char fg,
                          unsigned char bg);
void f18a_term80_write_at(unsigned int row, unsigned int col,
                          const char* text,
                          unsigned char fg,
                          unsigned char bg);

void f18a_term80_set_smartkeys_visible(int visible);
int  f18a_term80_smartkeys_visible(void);
unsigned int f18a_term80_last_text_row(void);

void f18a_term80_demo_screen(void);

#ifdef __cplusplus
}
#endif

#endif // F18A_TERM80_H
