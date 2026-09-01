#include "video_bridge.h"
#include <stdatomic.h>
#include <string.h>
#include <qglobal.h>
#if defined(Q_OS_LINUX)
#include <errno.h>
#endif

uint32_t g_video_frame[VB_MAX_WIDTH * VB_MAX_HEIGHT];

static atomic_int g_video_dirty = 0;
static atomic_int g_video_width = VB_WIDTH;
static atomic_int g_video_height = VB_HEIGHT;

void video_set_dirty(int value)
{
    atomic_store(&g_video_dirty, value);
}

int video_get_dirty(void)
{
    return atomic_load(&g_video_dirty);
}

int vb_current_width(void)
{
    int w = atomic_load(&g_video_width);
    if (w <= 0 || w > VB_MAX_WIDTH) w = VB_WIDTH;
    return w;
}

int vb_current_height(void)
{
    int h = atomic_load(&g_video_height);
    if (h <= 0 || h > VB_MAX_HEIGHT) h = VB_HEIGHT;
    return h;
}

void vb_set_frame_size(int width, int height)
{
    if (width <= 0 || width > VB_MAX_WIDTH) width = VB_WIDTH;
    if (height <= 0 || height > VB_MAX_HEIGHT) height = VB_HEIGHT;
    atomic_store(&g_video_width, width);
    atomic_store(&g_video_height, height);
}

void vb_reset_frame_size(void)
{
    vb_set_frame_size(VB_WIDTH, VB_HEIGHT);
}

#if defined(Q_OS_LINUX)
static inline int memcpy_s(void *dest, size_t dest_sz, const void *src, size_t count) {
    if (dest == NULL || src == NULL || dest_sz < count) {
        return EINVAL;
    }
    memcpy(dest, src, count);
    return 0;
}
#endif

/* As vb_present_scanline_ex, but the caller states the frame height too - the
   PICO9918 frame is taller than the 192 active lines. */
void vb_present_scanline_size(int y, const uint32_t *argb32_line, int width, int height)
{
    if (y < 0 || y >= VB_MAX_HEIGHT || !argb32_line) return;
    if (width <= 0 || width > VB_MAX_WIDTH) width = VB_WIDTH;

    vb_set_frame_size(width, height);

    memcpy_s(&g_video_frame[y * VB_MAX_WIDTH],
             VB_MAX_WIDTH * sizeof(uint32_t),
             argb32_line,
             (size_t)width * sizeof(uint32_t));
}

void vb_present_scanline_ex(int y, const uint32_t *argb32_line, int width)
{
    if (y < 0 || y >= VB_MAX_HEIGHT || !argb32_line) return;
    if (width <= 0 || width > VB_MAX_WIDTH) width = VB_WIDTH;

    vb_set_frame_size(width, VB_HEIGHT);

    memcpy_s(&g_video_frame[y * VB_MAX_WIDTH],
             VB_MAX_WIDTH * sizeof(uint32_t),
             argb32_line,
             (size_t)width * sizeof(uint32_t));
}

void vb_present_scanline(int y, const uint32_t *argb32_line)
{
    vb_present_scanline_ex(y, argb32_line, VB_WIDTH);
}

void vb_present_frame(void)
{
    atomic_store(&g_video_dirty, 1);
}
