/*
 * ADAMP's pixel policy for pico9918-core, force-included into the library build. The
 * desktop defaults are #ifndef-guarded, so this pins a correct policy without editing
 * pico9918-core. Two of them are wrong for a 32-bit pixel:
 *  1. PICO9918_PIXEL_FROM_RGB12 reads a palette entry as canonical 0x0RGB. A pram
 *     entry is byte-swapped RGB444, so it took alpha for green and swapped R and B.
 *  2. The LUT builder packs a converted pair into one word; the consumer expects one
 *     pixel per entry. Only holds at 16 bits, and truncated every entry here.
 * Emits ARGB32 rather than RGBA8888 to match QImage::Format_ARGB32.
 */

#ifndef ADAMP_PICO9918_PIXEL_POLICY_H
#define ADAMP_PICO9918_PIXEL_POLICY_H

/* pram is byte-swapped RGB444: 15-12=G, 11-8=B, 3-0=R. Emit 0xAARRGGBB. */
#define PICO9918_PIXEL_FROM_RGB12(rgb) \
  ((PICO9918_PIXEL_T)((uint32_t)((((rgb) >>  8) & 0x0f) * 0x11)        | \
                      (uint32_t)((((rgb) >> 12) & 0x0f) * 0x11) <<  8  | \
                      (uint32_t)((((rgb)      ) & 0x0f) * 0x11) << 16  | \
                      0xff000000u))

/* One pixel per LUT entry: pass the raw pram word through and let cachePixelPair()
   convert each half. Safe because PICO9918_PIXEL_PAIR has one use in the library,
   the LUT build in pico9918_palette.c. */
#define PICO9918_PIXEL_FROM_RGB12_PAIR(packed) (packed)
#define PICO9918_PIXEL_PAIR(p)                 PICO9918_PIXEL_FROM_RGB12(p)

#endif /* ADAMP_PICO9918_PIXEL_POLICY_H */
