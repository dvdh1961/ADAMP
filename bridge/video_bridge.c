#include "video_bridge.h"
#include <string.h>

uint32_t g_video_frame[VB_WIDTH * VB_HEIGHT];
volatile int g_video_dirty = 0;
extern int z80_irq_line;

void vb_present_scanline(int y, const uint32_t *argb32_line)
{
    if (y < 0 || y >= VB_HEIGHT || !argb32_line) return;
    memcpy_s(&g_video_frame[y * VB_WIDTH],
             VB_WIDTH * sizeof(uint32_t),
             argb32_line,
             VB_WIDTH * sizeof(uint32_t));
}

void vb_present_frame(void)
{
    // Simpel signaal: markeer dat er een nieuw frame ligt.
    g_video_dirty = 1;
}
