#ifndef F18A_H
#define F18A_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * F18A module - B3.2
 *
 * This is the first real internal state version.
 * It keeps its own VRAM/registers/status/address latch.
 *
 * No rendering yet.
 * No dependency on tms9928a.c/h.
 */

void f18a_reset(void);

void f18a_set_enabled(int enabled);
int  f18a_is_enabled(void);
void f18a_set_cpm40_shift_left(int enabled);

void f18a_set_scanlines(unsigned int lines);

unsigned char f18a_loop(void);

void          f18a_writedata(unsigned char value);
unsigned char f18a_readdata(void);

unsigned char f18a_writectrl(unsigned char value);
unsigned char f18a_readctrl(void);

unsigned char f18a_get_register(unsigned char reg);

/* State access helpers */
unsigned char f18a_peek_vram(unsigned int address);
void          f18a_poke_vram(unsigned int address, unsigned char value);
unsigned int  f18a_get_vram_address(void);
unsigned char f18a_get_status(void);

/* B4.1 F18A infrastructure helpers; not yet forced into rendering. */
unsigned char  f18a_get_enhanced_register(unsigned char reg);
void           f18a_set_enhanced_register(unsigned char reg, unsigned char value);
unsigned short f18a_get_palette_entry(unsigned int index);
void           f18a_set_palette_entry(unsigned int index, unsigned short rgb12);
unsigned char  f18a_palette_is_dirty(void);
void           f18a_palette_clear_dirty(void);

/* Present the host 80-column terminal buffer, whichever engine is rendering the VDP.
   The buffer is ADAMP's, not the VDP's - CP/M and T-DOS write it through TERM80 - so it
   outlives f18a.c's own renderer. Returns 1 if it took the frame, 0 if 80-column is not
   up and the VDP frame stands. */
int  f18a_present_80col_overlay(void);

/* Reset the host 80-column overlay's own state - the palette it draws with and the
   display buffer. Call from the VDP reset on every engine: f18a_reset() is only
   reached on the legacy path, and without this the overlay draws black on black. */
void f18a_reset_80col_overlay(void);

/* F18A 80-column diagnostic/self-test controls. */
void f18a_set_80col_enabled(int enabled);
int  f18a_is_80col_enabled(void);
void f18a_set_80col_selftest_enabled(int enabled);
int  f18a_is_80col_selftest_enabled(void);

/* C7: internal 80x24 text buffer API. */
void f18a_80col_clear(unsigned char ch, unsigned char fg, unsigned char bg);
void f18a_80col_put_char(unsigned int row, unsigned int col,
                         unsigned char ch,
                         unsigned char fg,
                         unsigned char bg);
void f18a_80col_write_text(unsigned int row, unsigned int col,
                           const char* text,
                           unsigned char fg,
                           unsigned char bg);

void f18a_hide_current_sprites(void);

#ifdef __cplusplus
}
#endif

#endif // F18A_H
