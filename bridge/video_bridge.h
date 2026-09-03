#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

/*
 * Dynamic video bridge.
 * - Normal TMS/F18A-compatible output uses 256x192.
 * - F18A 80-column output can use 480x192.
 * The backing buffer is wide enough for both.
 */
#define VB_WIDTH       256
#define VB_HEIGHT      192

/* The PICO9918 renders a full 640x480 VGA frame, overlays in the border included.
   The smaller TMS/F18A output still fits inside it. */
#define VB_MAX_WIDTH   640
#define VB_MAX_HEIGHT  480

extern uint32_t g_video_frame[VB_MAX_WIDTH * VB_MAX_HEIGHT];

void video_set_dirty(int value);
int  video_get_dirty(void);

int  vb_current_width(void);
int  vb_current_height(void);
void vb_set_frame_size(int width, int height);
void vb_present_scanline_size(int y, const uint32_t *argb32_line, int width, int height);
void vb_reset_frame_size(void);

void vb_present_scanline(int y, const uint32_t *argb32_line);
void vb_present_scanline_ex(int y, const uint32_t *argb32_line, int width);
void vb_present_frame(void);

#ifdef __cplusplus
}
#endif
