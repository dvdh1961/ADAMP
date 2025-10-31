#include "video_bridge.h"
#include <string.h>
#include "z80.h"
#include "tms9928a.h"

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
    coleco_vdp_check_irq();   // <-- VDP interrupt genereren
}

void coleco_vdp_check_irq(void)
{
    // Bit 7 (0x80) van statusregister = VBlank/INT flag
    if (tms.SR & 0x80) {
        // Breng de Z80 IRQ-lijn hoog
        z80_set_irq_line(0, ASSERT_LINE);

        // Maak de flag leeg (zoals echte Coleco doet bij status-read)
        tms.SR &= 0x7F;
    } else {
        z80_set_irq_line(0, CLEAR_LINE);
    }
}
