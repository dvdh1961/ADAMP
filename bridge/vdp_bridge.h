#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* VDP bridge - pico9918-core behind ADAMP's existing VDP entry points. cv.cpp's
   vdp_*_active() dispatchers call these instead of tms9928a.c / f18a.c, so the shapes
   match the functions they stand in for. Unlike f18a_loop(), which renders the whole
   frame at end-of-frame, this renders a line at a time, so mid-frame register writes
   land where they should. */

/* Where the board keeps its 256-byte config block. Set before the first reset; no
   path just means the settings do not survive the session. */
void          vdp_bridge_set_config_path(const char* path);

/* scanlines: the machine's line count, 262 NTSC or 313 PAL. 0 keeps the current. */
void          vdp_bridge_reset(unsigned int scanlines);

void          vdp_bridge_writedata(unsigned char value);
void          vdp_bridge_writectrl(unsigned char value);
unsigned char vdp_bridge_readdata(void);
unsigned char vdp_bridge_readctrl(void);

/* One emulated scanline, matching f18a_loop(). Also advances the GPU by a scanline's
   budget of instructions. Returns the IRQ line level. */
int           vdp_bridge_loop(void);

/* State reads for the debug viewers - Phase 3 will lean on these. */
unsigned char vdp_bridge_get_register(unsigned char reg);
unsigned char vdp_bridge_peek_vram(unsigned int address);

/* Bring a core up, render one scanline, tear it down. Returns the line width, or 0
   if the core would not start. Exercises the library across the qmake/CMake seam. */
int           vdp_bridge_selftest(void);

#ifdef __cplusplus
}
#endif
