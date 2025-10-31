#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Globale ARGB32 framebuffer (256×192)
#define VB_WIDTH  256
#define VB_HEIGHT 192

extern uint32_t g_video_frame[VB_WIDTH * VB_HEIGHT];
extern volatile int g_video_dirty; // 1 als er vers frame klaar staat

// Door de VDP aan te roepen na het tekenen van 1 scanline (optioneel)
void vb_present_scanline(int y, const uint32_t *argb32_line);

// Door de VDP aan te roepen na voltooid frame
void vb_present_frame(void);
void coleco_vdp_check_irq(void);

#ifdef __cplusplus
}
#endif
