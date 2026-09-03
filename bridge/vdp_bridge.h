#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* VDP bridge - pico9918-core behind ADAMP's existing VDP entry points. The shapes
   match the tms9928a.c / f18a.c functions these stand in for, so the host's VDP
   dispatchers can call either. Unlike f18a_loop(), which renders the whole frame at
   end-of-frame, this renders a line at a time, so mid-frame register writes land where
   they should. */

/* Which chip the core answers as - ADAMP's VdpType: 0 TMS9918A, 1 F18A, 2 PICO9918.
   One archive renders all three, so this is what the VDP selection now means. Safe to
   call from another thread: on a running core the switch takes effect at the next
   scanline boundary rather than inside the line being rendered. Re-applied by every
   reset, because a reset does not clear it. */
void          vdp_bridge_set_chip(int vdpType);

/* Where the board keeps its 256-byte config block. Set before the first reset; no
   path just means the settings do not survive the session. Only a PICO9918 has one -
   the other two personalities never read or write it. */
void          vdp_bridge_set_config_path(const char* path);

/* A host overlay that takes the screen for a whole frame - ADAMP's 80-column terminal,
   which is a host-side character buffer rather than anything the VDP renders. Called
   once per frame at the porch; a non-zero return means it presented, and the VDP frame
   underneath it is not presented again. NULL (the default) means there is none. */
void          vdp_bridge_set_overlay_hook(int (*present)(void));

/* scanlines: the machine's line count, 262 NTSC or 313 PAL. 0 keeps the current. */
void          vdp_bridge_reset(unsigned int scanlines);

void          vdp_bridge_writedata(unsigned char value);
void          vdp_bridge_writectrl(unsigned char value);
unsigned char vdp_bridge_readdata(void);
unsigned char vdp_bridge_readctrl(void);

/* One emulated scanline, matching f18a_loop(). Also advances the GPU by a scanline's
   budget of instructions. Returns the IRQ line level. */
int           vdp_bridge_loop(void);

/* The IRQ line level right now, outside the scanline loop. A status read does not
   necessarily clear the interrupt: the core clears its latch only when the register
   selected by F18A VR15 is SR0, so the host cannot infer the level from having read. */
int           vdp_bridge_irq_level(void);

/* VDP state for the debugger and the viewers. Reach these through cv.cpp's
   coleco_vdp_*() accessors rather than directly, so that "which engine holds the
   state" is decided in one place. The poke goes through the address/data ports, so it
   disturbs the address latch a running program may be mid-sequence on. */
unsigned char vdp_bridge_get_register(unsigned char reg);
unsigned char vdp_bridge_peek_vram(unsigned int address);
void          vdp_bridge_poke_vram(unsigned int address, unsigned char value);

#ifdef __cplusplus
}
#endif
