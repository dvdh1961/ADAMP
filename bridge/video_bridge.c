#include "video_bridge.h"
#include <string.h>
#include <qglobal.h>
#include <error.h>
#include <errno.h>

uint32_t g_video_frame[VB_WIDTH * VB_HEIGHT];
volatile int g_video_dirty = 0;
extern int z80_irq_line;

#if defined(Q_OS_LINUX)
static inline int memcpy_s(void *dest, size_t dest_sz, const void *src, size_t count) {
    if (dest == NULL || src == NULL || dest_sz < count) {
        return EINVAL;  // Invalid argument error
    }
    memcpy(dest, src, count);
    return 0;  // Success
}
#endif


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
