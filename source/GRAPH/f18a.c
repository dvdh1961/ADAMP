#include "f18a.h"
#include "f18a_gpu.h"
#include "f18a_term80.h"
#include "video_bridge.h"

#include <string.h>
#include <stdint.h>

/*
 * F18A implementation
 * ------------------------
  * - Keep own VRAM/register/status/address latch
 * - TMS-compatible data/control port behaviour
 * - Minimal TMS-compatible rendering from own F18A VRAM/registers
 *
 * Implemented rendering:
 * - Blank screen / backdrop color
 * - Graphics I
 * - Graphics II basic
 * - Text mode basic
 * - Multicolor basic approximation
 * - basic 8x8 sprites
 * - basic 16x16 sprites
 */

#define F18A_VRAM_SIZE       0x10000u  /* complete F18A/GPU address space */
#define F18A_VRAM_MASK       0x3FFFu
#define F18A_REGISTER_COUNT  64u       /* TMS regs 0-7 + F18A/extended regs */
#define F18A_PALETTE_ENTRIES 64u       /* Infrastructure for later F18A palette RAM */

/*
 * timing calibration
 * -----------------------
 * One f18a_loop() call is treated as one emulated scanline.
 * Keep these values close to the TMS core. If the F18A route still runs
 * a little too fast/slow compared with TMS, tune F18A_TIMING_ADJUST only.
 *
 *   +1 / +2  = slightly slower
 *   -1 / -2  = slightly faster
 */
#define F18A_NTSC_SCANLINES 262u
#define F18A_PAL_SCANLINES  313u
#define F18A_TIMING_ADJUST  0
#define F18A_GPU_INSTRUCTIONS_PER_SCANLINE 400u

#define F18A_80COL_WIDTH   480
#define F18A_80COL_HEIGHT  192
#define F18A_80COL_COLS    80
#define F18A_80COL_ROWS    24


#define F18A_STATUS_VBLANK   0x80u
#define F18A_REG1_INT_ENABLE 0x20u
#define F18A_REG1_DISPLAY    0x40u
#define F18A_REG1_SPRITE_MAG  0x01u
#define F18A_REG1_SPRITE_16   0x02u

#define F18A_MODE_M1         0x10u     /* R1 bit 4: Text */
#define F18A_MODE_M2         0x08u     /* R1 bit 3: Multicolor */
#define F18A_MODE_M3         0x02u     /* R0 bit 1: Graphics II */

static int g_f18a_enabled = 0;
static int g_f18a_cpm40_shift_left = 0;

static unsigned char  g_vram[F18A_VRAM_SIZE];
static unsigned char  g_reg[F18A_REGISTER_COUNT];
static F18aGpu g_gpu;

/*
 * infrastructure only.
 * The renderer still uses the fixed TMS-compatible palette for now.
 * These entries are stored so later F18A palette writes can be connected
 * without changing the TMS-compatible rendering path again.
 */
static unsigned short g_palette12[F18A_PALETTE_ENTRIES];
static unsigned char  g_palette_dirty = 0;
static unsigned char  g_palette_mode = 0;
static unsigned char  g_palette_auto = 0;
static unsigned char  g_palette_address = 0;
static unsigned char  g_palette_first_byte = 0;
static unsigned char  g_palette_byte_latch = 0;

static unsigned char g_status = 0;
static unsigned char g_read_buffer = 0;

/*
 * Enhanced Register Mode (ERM).
 *
 * The real F18A powers up locked for compatibility with software that writes
 * register numbers above 7 on a TMS9918A.  ERM is enabled only by two
 * consecutive writes of >1C to VR57.  VR15 then selects the status register
 * returned by a control-port read.
 */
static unsigned char g_erm_unlocked = 0;
static unsigned char g_unlock_stage = 0;
static unsigned char g_status_select = 0;

static unsigned int  g_address = 0;
static unsigned char g_first_ctrl_byte = 0;
static int           g_ctrl_latch = 0;

static unsigned int g_scanlines = F18A_NTSC_SCANLINES;

static unsigned int g_loop_counter = 0;

static void f18a_gpu_prepare_mapped_memory(int blanking_override)
{
    unsigned int i;
    const unsigned int active_lines = (g_reg[49] & 0x40u) ? 240u : 192u;

    memcpy(&g_vram[0x6000u], g_reg, F18A_REGISTER_COUNT);
    for (i = 0u; i < F18A_PALETTE_ENTRIES; ++i) {
        const unsigned short color = g_palette12[i] & 0x0FFFu;
        g_vram[0x5000u + i * 2u] = (unsigned char)(color >> 8);
        g_vram[0x5001u + i * 2u] = (unsigned char)color;
    }
    g_vram[0x7000u] = (unsigned char)((g_loop_counter < 256u) ? g_loop_counter : 255u);
    g_vram[0x7001u] = (blanking_override >= 0)
                    ? (unsigned char)(blanking_override != 0)
                    : ((g_loop_counter >= active_lines) ? 1u : 0u);
    g_vram[0xB000u] = g_status;
    g_vram[0xB002u] |= 0x80u;
}

static void f18a_gpu_commit_mapped_memory(void)
{
    unsigned int i;
    memcpy(g_reg, &g_vram[0x6000u], F18A_REGISTER_COUNT);
    for (i = 0u; i < F18A_PALETTE_ENTRIES; ++i) {
        const unsigned short color = (unsigned short)(
            ((unsigned short)(g_vram[0x5000u + i * 2u] & 0x0Fu) << 8) |
            g_vram[0x5001u + i * 2u]);
        if (g_palette12[i] != color) {
            g_palette12[i] = color;
            g_palette_dirty = 1u;
        }
    }
    if (!g_gpu.running)
        g_vram[0xB002u] &= (unsigned char)~0x80u;
}

static void f18a_gpu_run(unsigned int budget, int blanking_override)
{
    f18a_gpu_prepare_mapped_memory(blanking_override);
    f18a_gpu_execute(&g_gpu, budget);
    f18a_gpu_commit_mapped_memory();
}
static unsigned int f18a_apply_timing_adjust(unsigned int base_lines)
{
    int adjusted = (int)base_lines + F18A_TIMING_ADJUST;

    /* Safety guard: do not allow an invalid frame length. */
    if (adjusted < 192)
        adjusted = (int)base_lines;

    return (unsigned int)adjusted;
}


static const uint32_t s_tms_palette[16] = {
    0xFF000000u, /* 0 transparent -> black for now */
    0xFF000000u, /* 1 black */
    0xFF21C842u, /* 2 medium green */
    0xFF5EDC78u, /* 3 light green */
    0xFF5455EDu, /* 4 dark blue */
    0xFF7D76FCu, /* 5 light blue */
    0xFFD4524Du, /* 6 dark red */
    0xFF42EBF5u, /* 7 cyan */
    0xFFFC5554u, /* 8 medium red */
    0xFFFF7978u, /* 9 light red */
    0xFFD4C154u, /* 10 dark yellow */
    0xFFE6CE80u, /* 11 light yellow */
    0xFF21B03Bu, /* 12 dark green */
    0xFFC95BBAu, /* 13 magenta */
    0xFFCCCCCCu, /* 14 gray */
    0xFFFFFFFFu  /* 15 white */
};

static const unsigned short s_default_palette12[16] = {
    0x000u, /* 0 transparent/black */
    0x000u, /* 1 black */
    0x2C4u, /* 2 medium green */
    0x5D7u, /* 3 light green */
    0x55Eu, /* 4 dark blue */
    0x77Fu, /* 5 light blue */
    0xD54u, /* 6 dark red */
    0x4EFu, /* 7 cyan */
    0xF55u, /* 8 medium red */
    0xF77u, /* 9 light red */
    0xDC5u, /* 10 dark yellow */
    0xED8u, /* 11 light yellow */
    0x2B3u, /* 12 dark green */
    0xC5Bu, /* 13 magenta */
    0xCCCu, /* 14 gray */
    0xFFFu  /* 15 white */
};

static void f18a_reset_palette(void)
{
    for (unsigned int i = 0; i < F18A_PALETTE_ENTRIES; ++i)
        g_palette12[i] = s_default_palette12[i & 0x0Fu];

    g_palette_dirty = 0;
}

/* The host 80-column overlay's own state: the palette its colours come from (entries
   0-15, which is all it ever asks for) and the display buffer itself. Neither is VDP
   state, so both have to be reset on whichever engine is rendering - f18a_reset() is
   only reached on the legacy path. */
void f18a_reset_80col_overlay(void)
{
    f18a_reset_palette();
    f18a_term80_display_reset();
}

static inline uint32_t f18a_color(unsigned int idx)
{
    const unsigned short rgb = g_palette12[idx & 0x3Fu];
    const unsigned int r = (rgb >> 8) & 0x0Fu;
    const unsigned int g = (rgb >> 4) & 0x0Fu;
    const unsigned int b = rgb & 0x0Fu;
    return 0xFF000000u | (r * 17u << 16) | (g * 17u << 8) | (b * 17u);
}

static inline unsigned char f18a_vram_read(unsigned int addr)
{
    return g_vram[addr & F18A_VRAM_MASK];
}

void f18a_hide_current_sprites(void)
{
    /*
     * F18A gebruikt eigen VRAM/registers.
     * Sprite Attribute Table base komt uit register 5,
     * zelfde TMS-compatible logica als f18a_draw_sprites_on_line().
     *
     * Alleen de sprite-list afsluiten met Y=0xD0.
     * Niet heel de sprite table wissen.
     */
    const unsigned int attr_base =
        ((unsigned int)(g_reg[5] & 0x7Fu) << 7) & F18A_VRAM_MASK;

    g_vram[(attr_base + 0u) & F18A_VRAM_MASK] = 0xD0u;
    g_vram[(attr_base + 1u) & F18A_VRAM_MASK] = 0x00u;
    g_vram[(attr_base + 2u) & F18A_VRAM_MASK] = 0x00u;
    g_vram[(attr_base + 3u) & F18A_VRAM_MASK] = 0x00u;
}

static void f18a_draw_sprites_on_line(int y, uint32_t* line,
                                      const unsigned char* tile_priority)
{
    const unsigned int attr_base = ((unsigned int)(g_reg[5] & 0x7Fu) << 7) & F18A_VRAM_MASK;
    const unsigned int patt_base = ((unsigned int)(g_reg[6] & 0x07u) << 11) & F18A_VRAM_MASK;
    const unsigned int ecm = g_erm_unlocked ? (g_reg[49] & 0x03u) : 0u;
    const unsigned int ecm_offset = 0x0800u >> ((g_reg[29] & 0xC0u) >> 6);
    const int magnified = (g_reg[1] & F18A_REG1_SPRITE_MAG) ? 1 : 0;
    const int row30 = g_erm_unlocked && (g_reg[49] & 0x40u);
    int sprite_count = (g_reg[51] > 0u && g_reg[51] <= 32u) ? g_reg[51] : 32;

    for (int i = 0; i < sprite_count; ++i)
    {
        const unsigned int sa = attr_base + (unsigned int)i * 4u;
        const unsigned char sy_raw = f18a_vram_read(sa + 0u);

        if (sy_raw == 0xD0u && !row30)
        {
            sprite_count = i;
            break;
        }
    }

    for (int i = sprite_count - 1; i >= 0; --i)
    {
        const unsigned int sa = attr_base + (unsigned int)i * 4u;
        const unsigned char sy_raw = f18a_vram_read(sa + 0u);
        int sx = (int)f18a_vram_read(sa + 1u);
        const unsigned char pattern = f18a_vram_read(sa + 2u);
        const unsigned char color_byte = f18a_vram_read(sa + 3u);
        int sprite_16 = (g_reg[1] & F18A_REG1_SPRITE_16) ? 1 : 0;
        int sprite_size;
        int sy;
        int rel_y;
        int source_y;
        unsigned int palette_base;

        if (g_erm_unlocked && !sprite_16 && (color_byte & 0x10u))
            sprite_16 = 1;
        sprite_size = sprite_16 ? 16 : 8;
        sy = row30 ? (int)sy_raw : (((int)sy_raw + 1) & 0xFF);
        if (sy > (row30 ? 0xF0 : 0xE0))
            sy -= 256;
        rel_y = y - sy;
        if (rel_y < 0 || rel_y >= (sprite_size << magnified))
            continue;
        source_y = rel_y >> magnified;
        if (g_erm_unlocked && (color_byte & 0x20u))
            source_y = sprite_size - source_y - 1;

        if (color_byte & 0x80u)
            sx -= 32;

        if (ecm == 1u)
            palette_base = ((unsigned int)(color_byte & 0x0Fu) << 1) |
                           (g_reg[24] & 0x20u);
        else if (ecm == 2u)
            palette_base = (unsigned int)(color_byte & 0x0Fu) << 2;
        else if (ecm == 3u)
            palette_base = (unsigned int)(color_byte & 0x0Eu) << 2;
        else
            palette_base = color_byte & 0x0Fu;
        if (ecm == 0u && palette_base == 0u)
            continue;

        for (int draw_x = 0; draw_x < sprite_size; ++draw_x) {
            const int source_x = (g_erm_unlocked && (color_byte & 0x40u))
                               ? sprite_size - draw_x - 1 : draw_x;
            const unsigned int quadrant = (unsigned int)((source_y >= 8) ? 1 : 0) |
                                          (unsigned int)((source_x >= 8) ? 2 : 0);
            const unsigned int patt = sprite_16
                                    ? ((unsigned int)(pattern & 0xFCu) + quadrant)
                                    : (unsigned int)pattern;
            const unsigned int bit = 0x80u >> (source_x & 7);
            const unsigned int row = (unsigned int)(source_y & 7);
            unsigned int pixel_value = 0u;
            const unsigned int planes = ecm ? ecm : 1u;

            for (unsigned int plane = 0u; plane < planes; ++plane) {
                const unsigned char bits = f18a_vram_read(
                    patt_base + (patt << 3) + row + plane * ecm_offset);
                if (bits & bit)
                    pixel_value |= 1u << plane;
            }
            if (pixel_value == 0u)
                continue;

            for (int mx = 0; mx <= magnified; ++mx) {
                const int px = sx + (draw_x << magnified) + mx;
                if (px >= 0 && px < VB_WIDTH &&
                    (!tile_priority || tile_priority[px] == 0u))
                    line[px] = f18a_color((ecm ? palette_base + pixel_value
                                               : palette_base) & 0x3Fu);
            }
        }
    }
}

static void f18a_present_solid(uint32_t argb)
{
    uint32_t line[VB_WIDTH];
    for (int x = 0; x < VB_WIDTH; ++x)
        line[x] = argb;

    for (int y = 0; y < VB_HEIGHT; ++y)
        vb_present_scanline(y, line);

    vb_present_frame();
    video_set_dirty(1);
}

static void f18a_render_graphics1(void)
{
    const unsigned int name_base    = ((unsigned int)(g_reg[2] & 0x0Fu) << 10) & F18A_VRAM_MASK;
    const unsigned int color_base   = ((unsigned int)g_reg[3] << 6) & F18A_VRAM_MASK;
    const unsigned int pattern_base = ((unsigned int)(g_reg[4] & 0x07u) << 11) & F18A_VRAM_MASK;
    const uint32_t border = f18a_color(g_reg[7] & 0x0Fu);

    uint32_t line[VB_WIDTH];
    unsigned char tile_priority[VB_WIDTH];
    for (int y = 0; y < VB_HEIGHT; ++y)
    {
        const unsigned int max_y = (g_reg[49] & 0x40u) ? 240u : 192u;
        unsigned int source_y = (unsigned int)y + (unsigned int)g_reg[28];
        unsigned int vertical_page = 0u;

        if (source_y >= max_y)
        {
            source_y %= max_y;
            if (g_reg[29] & 0x01u)
                vertical_page = 0x0800u;
        }

        const unsigned int tile_y = source_y >> 3;
        const unsigned int row = source_y & 7u;

        for (int x = 0; x < VB_WIDTH; ++x)
        {
            line[x] = border;
            tile_priority[x] = 0u;
        }

        for (int x = 0; x < VB_WIDTH; ++x)
        {
            const unsigned int source_x_full =
                (unsigned int)x + (unsigned int)g_reg[27];
            const unsigned int source_x = source_x_full & 0xFFu;
            const unsigned int horizontal_page =
                ((source_x_full & 0x100u) && (g_reg[29] & 0x02u)) ? 0x0400u : 0u;
            const unsigned int tile_x = source_x >> 3;
            const unsigned int bit = source_x & 7u;
            const unsigned int row_offset = tile_y * 32u;
            const unsigned int name_addr =
                name_base + vertical_page + horizontal_page + row_offset + tile_x;
            const unsigned char chr = f18a_vram_read(name_addr);

            /*
             * F18A ECM3 uses an attribute byte and three pattern planes.
             */
            if (((g_reg[49] >> 4) & 0x03u) == 0x03u)
            {
                const unsigned int attr_index =
                    (g_reg[50] & 0x02u)
                        ? row_offset + tile_x
                        : (unsigned int)chr;
                unsigned int attr_base = color_base;
                if (g_reg[50] & 0x02u)
                {
                    attr_base = (attr_base & ~0x0400u) |
                                ((name_base + horizontal_page) & 0x0400u);
                    attr_base += vertical_page;
                }
                const unsigned char attr =
                    f18a_vram_read((attr_base + attr_index) & F18A_VRAM_MASK);
                const unsigned int palette_base = ((unsigned int)attr & 0x0Eu) << 2;
                const unsigned int row_ecm =
                    (attr & 0x20u) ? (unsigned int)(7 - row) : (unsigned int)row;
                const unsigned int plane_offset =
                    0x0100u << (3u - (((unsigned int)g_reg[29] & 0x0Cu) >> 2));
                const unsigned int plane_address =
                    (pattern_base + ((unsigned int)chr << 3) + row_ecm) & F18A_VRAM_MASK;
                const unsigned char plane0 = f18a_vram_read(plane_address);
                const unsigned char plane1 = f18a_vram_read(plane_address + plane_offset);
                const unsigned char plane2 = f18a_vram_read(plane_address + (plane_offset << 1));
                const unsigned int bit_ecm =
                    (attr & 0x40u) ? 7u - bit : bit;
                const unsigned int mask = 0x80u >> bit_ecm;
                const unsigned int pixel =
                    ((plane0 & mask) ? 1u : 0u) |
                    ((plane1 & mask) ? 2u : 0u) |
                    ((plane2 & mask) ? 4u : 0u);

                if (pixel != 0u || (attr & 0x10u) == 0u)
                {
                    line[x] = f18a_color(palette_base | pixel);
                    tile_priority[x] = (attr & 0x80u) ? 1u : 0u;
                }
                continue;
            }

            const unsigned char pat = f18a_vram_read(pattern_base + ((unsigned int)chr << 3) + (unsigned int)row);
            const unsigned char col = f18a_vram_read(color_base + (unsigned int)(chr >> 3));

            unsigned int fg = (col >> 4) & 0x0Fu;
            unsigned int bg = col & 0x0Fu;
            if (fg == 0) fg = g_reg[7] & 0x0Fu;
            if (bg == 0) bg = g_reg[7] & 0x0Fu;

            const uint32_t fg_argb = f18a_color(fg);
            const uint32_t bg_argb = f18a_color(bg);
            line[x] = (pat & (0x80u >> bit)) ? fg_argb : bg_argb;
        }

        f18a_draw_sprites_on_line(y, line, tile_priority);
        vb_present_scanline(y, line);
    }

    vb_present_frame();
    video_set_dirty(1);
}

static void f18a_render_graphics2(void)
{
    /*
     * SCREEN 2 / Graphics II addressing.
     *
     * This intentionally follows the existing tms9928a.c _TMS9928A_mode2()
     * table-bank logic instead of using a generic mask formula.
     * Bagman depends on this exact behaviour: with R3=9F the color table
     * remains at the same base for all three 64-line screen thirds.
     */
    const unsigned int name_base =
        ((unsigned int)(g_reg[2] & 0x7Fu) << 10) & F18A_VRAM_MASK;

    const unsigned int color_base0 =
        ((unsigned int)(g_reg[3] & 0x80u) << 6) & F18A_VRAM_MASK;

    const unsigned int pattern_base0 =
        ((unsigned int)(g_reg[4] & 0x3Cu) << 11) & F18A_VRAM_MASK;

    const uint32_t border = f18a_color(g_reg[7] & 0x0Fu);

    uint32_t line[VB_WIDTH];

    for (int y = 0; y < VB_HEIGHT; ++y)
    {
        const int tile_y = y >> 3;
        const int row    = y & 7;

        unsigned int pattern_base = pattern_base0 + (unsigned int)row;
        unsigned int color_base   = color_base0   + (unsigned int)row;

        /* Exact bank selection as in tms9928a.c _TMS9928A_mode2(). */
        if (y >= 0x80)
        {
            switch (g_reg[4] & 0x03u)
            {
            case 0x00: break;
            case 0x01: pattern_base += 0x1000u; break;
            case 0x02: break;
            case 0x03: pattern_base += 0x1000u; break;
            }

            switch (g_reg[3] & 0x60u)
            {
            case 0x00: break;
            case 0x20: color_base += 0x1000u; break;
            case 0x40: break;
            case 0x60: color_base += 0x1000u; break;
            }
        }
        else if (y >= 0x40)
        {
            if (g_reg[4] & 0x02u)
                pattern_base += 0x0800u;

            if (g_reg[3] & 0x40u)
                color_base += 0x0800u;
        }

        pattern_base &= F18A_VRAM_MASK;
        color_base   &= F18A_VRAM_MASK;

        for (int x = 0; x < VB_WIDTH; ++x)
            line[x] = border;

        const unsigned int name_line = name_base + ((unsigned int)(y & 0xF8) << 2);

        for (int tile_x = 0; tile_x < 32; ++tile_x)
        {
            const unsigned char chr = f18a_vram_read(name_line + (unsigned int)tile_x);
            const unsigned int char_offset = ((unsigned int)chr << 3);

            /*
             * F18A Enhanced Color Mode 3 for tile layer 1 (VR49 ECMT=3).
             * Three pattern planes form a 3-bit pixel value.  In ECM modes
             * the color table expands to one attribute byte per screen
             * position; its low three bits select one of eight 8-color
             * palettes.  The default plane spacing is 2 KiB (VR29=0), which
             * is the layout used by f18abitmap1.rom.
             */
            if (((g_reg[49] >> 4) & 0x03u) == 0x03u)
            {
                const unsigned int attr_index =
                    (g_reg[50] & 0x02u)
                        ? (unsigned int)((y >> 3) * 32 + tile_x)
                        : (unsigned int)chr;
                const unsigned int attr_address =
                    (color_base0 + attr_index) & F18A_VRAM_MASK;
                const unsigned char attr = f18a_vram_read(attr_address);
                const unsigned int palette_base = ((unsigned int)attr & 0x07u) << 3;

                const unsigned char plane0 = f18a_vram_read(pattern_base + char_offset);
                const unsigned char plane1 = f18a_vram_read(pattern_base + char_offset + 0x0800u);
                const unsigned char plane2 = f18a_vram_read(pattern_base + char_offset + 0x1000u);
                const int px = tile_x << 3;

                for (int bit = 0; bit < 8; ++bit)
                {
                    const unsigned int mask = 0x80u >> bit;
                    const unsigned int pixel =
                        ((plane0 & mask) ? 1u : 0u) |
                        ((plane1 & mask) ? 2u : 0u) |
                        ((plane2 & mask) ? 4u : 0u);
                    line[px + bit] = f18a_color(palette_base | pixel);
                }
                continue;
            }

            const unsigned char pat = f18a_vram_read(pattern_base + char_offset);
            const unsigned char col = f18a_vram_read(color_base + char_offset);

            unsigned int fg = (col >> 4) & 0x0Fu;
            unsigned int bg = col & 0x0Fu;
            if (fg == 0) fg = g_reg[7] & 0x0Fu;
            if (bg == 0) bg = g_reg[7] & 0x0Fu;

            const uint32_t fg_argb = f18a_color(fg);
            const uint32_t bg_argb = f18a_color(bg);
            const int px = tile_x << 3;

            for (int bit = 0; bit < 8; ++bit)
                line[px + bit] = (pat & (0x80u >> bit)) ? fg_argb : bg_argb;
        }

        f18a_draw_sprites_on_line(y, line, NULL);

        /*
         * CP/M 40-column centering correction. This is the F18A equivalent
         * of the old TMS row += 8 bridge correction: shift the complete
         * 256-pixel scanline 8 pixels left and fill the right edge with
         * border color. Only active when cv.cpp explicitly enables it.
         */
        if (g_f18a_cpm40_shift_left)
        {
            const uint32_t fill = border;

            for (int sx = 0; sx < VB_WIDTH - 8; ++sx)
                line[sx] = line[sx + 8];

            for (int sx = VB_WIDTH - 8; sx < VB_WIDTH; ++sx)
                line[sx] = fill;
        }

        vb_present_scanline(y, line);
    }

    vb_present_frame();
    video_set_dirty(1);
}

static void f18a_render_text(void)
{
    const unsigned int name_base    = ((unsigned int)(g_reg[2] & 0x0Fu) << 10) & F18A_VRAM_MASK;
    const unsigned int pattern_base = ((unsigned int)(g_reg[4] & 0x07u) << 11) & F18A_VRAM_MASK;

    unsigned int fg = (g_reg[7] >> 4) & 0x0Fu;
    unsigned int bg = g_reg[7] & 0x0Fu;
    if (fg == 0) fg = 15;

    const uint32_t fg_argb = f18a_color(fg);
    const uint32_t bg_argb = f18a_color(bg);

    uint32_t line[VB_WIDTH];

    for (int y = 0; y < VB_HEIGHT; ++y)
    {
        const int char_y = y >> 3;
        const int row    = y & 7;

        for (int x = 0; x < VB_WIDTH; ++x)
            line[x] = bg_argb;

        for (int char_x = 0; char_x < 40; ++char_x)
        {
            const unsigned int name_addr = name_base + (unsigned int)(char_y * 40 + char_x);
            const unsigned char chr = f18a_vram_read(name_addr);
            const unsigned char pat = f18a_vram_read(pattern_base + ((unsigned int)chr << 3) + (unsigned int)row);

            const int px = 8 + char_x * 6;
            for (int bit = 0; bit < 6; ++bit)
                line[px + bit] = (pat & (0x80u >> bit)) ? fg_argb : bg_argb;
        }

        vb_present_scanline(y, line);
    }

    vb_present_frame();
    video_set_dirty(1);
}

static void f18a_render_text80(void)
{
    const unsigned int name_base =
        ((unsigned int)(g_reg[2] & 0x0Fu) << 10) & F18A_VRAM_MASK;
    const unsigned int color_base =
        ((unsigned int)g_reg[3] << 6) & F18A_VRAM_MASK;
    const unsigned int pattern_base =
        ((unsigned int)(g_reg[4] & 0x07u) << 11) & F18A_VRAM_MASK;
    const int per_position_color = (g_reg[50] & 0x02u) != 0;

    unsigned int main_fg = (g_reg[7] >> 4) & 0x0Fu;
    const unsigned int main_bg = g_reg[7] & 0x0Fu;
    if (main_fg == 0u)
        main_fg = main_bg;

    uint32_t line[F18A_80COL_WIDTH];

    for (int screen_y = 0; screen_y < F18A_80COL_HEIGHT; ++screen_y)
    {
        const unsigned int source_y =
            ((unsigned int)screen_y + (unsigned int)g_reg[28]) % 192u;
        const unsigned int tile_y = source_y >> 3;
        const unsigned int pattern_row = source_y & 7u;

        for (unsigned int column = 0; column < F18A_80COL_COLS; ++column)
        {
            const unsigned int name_offset = tile_y * F18A_80COL_COLS + column;
            const unsigned char chr = f18a_vram_read(name_base + name_offset);
            const unsigned char pattern =
                f18a_vram_read(pattern_base + ((unsigned int)chr << 3) + pattern_row);
            unsigned int fg = main_fg;
            unsigned int bg = main_bg;

            if (per_position_color)
            {
                const unsigned char color = f18a_vram_read(color_base + name_offset);
                const unsigned int attr_fg = (color >> 4) & 0x0Fu;
                const unsigned int attr_bg = color & 0x0Fu;
                fg = attr_fg ? attr_fg : main_bg;
                bg = attr_bg ? attr_bg : main_bg;
            }

            const uint32_t fg_argb = f18a_color(fg);
            const uint32_t bg_argb = f18a_color(bg);
            const unsigned int x0 = column * 6u;

            for (unsigned int bit = 0; bit < 6u; ++bit)
                line[x0 + bit] =
                    (pattern & (0x80u >> bit)) ? fg_argb : bg_argb;
        }

        vb_present_scanline_ex(screen_y, line, F18A_80COL_WIDTH);
    }

    vb_present_frame();
    video_set_dirty(1);
}

static void f18a_render_multicolor(void)
{
    const unsigned int name_base    = ((unsigned int)(g_reg[2] & 0x0Fu) << 10) & F18A_VRAM_MASK;
    const unsigned int pattern_base = ((unsigned int)(g_reg[4] & 0x07u) << 11) & F18A_VRAM_MASK;
    const uint32_t border = f18a_color(g_reg[7] & 0x0Fu);

    uint32_t line[VB_WIDTH];

    for (int y = 0; y < VB_HEIGHT; ++y)
    {
        const int tile_y = y >> 3;
        const int subrow = (y & 7) >> 2; /* 0 or 1 */

        for (int x = 0; x < VB_WIDTH; ++x)
            line[x] = border;

        for (int tile_x = 0; tile_x < 32; ++tile_x)
        {
            const unsigned int name_addr = name_base + (unsigned int)(tile_y * 32 + tile_x);
            const unsigned char chr = f18a_vram_read(name_addr);
            const unsigned char col = f18a_vram_read(pattern_base + ((unsigned int)chr << 3) + (unsigned int)(subrow * 4));

            const uint32_t left  = f18a_color((col >> 4) & 0x0Fu);
            const uint32_t right = f18a_color(col & 0x0Fu);
            const int px = tile_x << 3;

            for (int bit = 0; bit < 4; ++bit)
                line[px + bit] = left;
            for (int bit = 4; bit < 8; ++bit)
                line[px + bit] = right;
        }

        f18a_draw_sprites_on_line(y, line, NULL);
        vb_present_scanline(y, line);
    }

    vb_present_frame();
    video_set_dirty(1);
}

static void f18a_render_frame(void)
{
    vb_reset_frame_size();
    /* Display disabled: show backdrop color. */
    if ((g_reg[1] & F18A_REG1_DISPLAY) == 0)
    {
        f18a_present_solid(f18a_color(g_reg[7] & 0x0Fu));
        return;
    }

    const int m1 = (g_reg[1] & F18A_MODE_M1) ? 1 : 0;
    const int m2 = (g_reg[1] & F18A_MODE_M2) ? 1 : 0;
    const int m3 = (g_reg[0] & F18A_MODE_M3) ? 1 : 0;

    if (!m3 && (g_reg[0] & 0x04u))
    {
        f18a_render_text80();
    }
    else if (m1 && !m2 && !m3)
    {
        f18a_render_text();
    }
    else if (!m1 && m2 && !m3)
    {
        f18a_render_multicolor();
    }
    else if (!m1 && !m2 && m3)
    {
        f18a_render_graphics2();
    }
    else
    {
        f18a_render_graphics1();
    }
}


void f18a_set_enabled(int enabled)
{
    g_f18a_enabled = enabled ? 1 : 0;
}

int f18a_is_enabled(void)
{
    return g_f18a_enabled;
}

void f18a_set_cpm40_shift_left(int enabled)
{
    g_f18a_cpm40_shift_left = enabled ? 1 : 0;
}

void f18a_reset(void)
{
    memset(g_vram, 0x00, sizeof(g_vram));
    memset(g_reg,  0x00, sizeof(g_reg));
    f18a_gpu_init(&g_gpu, g_vram, sizeof(g_vram));
    f18a_reset_80col_overlay();
    g_status = 0;
    g_read_buffer = 0;
    g_palette_mode = 0;
    g_palette_auto = 0;
    g_palette_address = 0;
    g_palette_first_byte = 0;
    g_palette_byte_latch = 0;
    g_erm_unlocked = 0;
    g_unlock_stage = 0;
    g_status_select = 0;

    g_address = 0;
    g_first_ctrl_byte = 0;
    g_ctrl_latch = 0;

    g_loop_counter = 0;

    /*
     * Do not force timing during reset.
     * cv.cpp may already have selected NTSC/PAL before resetting the VDP.
     * If F18A starts directly without receiving timing yet, default to NTSC.
     */
    if (g_scanlines != f18a_apply_timing_adjust(F18A_NTSC_SCANLINES) &&
        g_scanlines != f18a_apply_timing_adjust(F18A_PAL_SCANLINES))
    {
        g_scanlines = f18a_apply_timing_adjust(F18A_NTSC_SCANLINES);
    }

    /* Default backdrop black until BIOS writes registers. */
    g_reg[7] = 0x01;

    f18a_present_solid(f18a_color(1));
}

void f18a_set_scanlines(unsigned int lines)
{
    if (lines == 0u)
        lines = F18A_NTSC_SCANLINES;

    /* cv.cpp passes the machine line count: 262 NTSC or 313 PAL. */
    g_scanlines = f18a_apply_timing_adjust(lines);
}

unsigned char f18a_loop(void)
{
    /* The F18A GPU runs alongside the host CPU, not only at its start write. */
    if (g_gpu.running) {
        const unsigned int active_lines = (g_reg[49] & 0x40u) ? 240u : 192u;
        const int vertical_blanking = g_loop_counter >= active_lines;

        /* Active display (or VBlank) followed by a short HBlank pulse. */
        f18a_gpu_run(350u, vertical_blanking ? 1 : 0);
        if (g_gpu.running && !vertical_blanking)
            f18a_gpu_run(F18A_GPU_INSTRUCTIONS_PER_SCANLINE - 350u, 1);
    }

    /*
     * Keep the IRQ line active as long as VBlank is pending.
     * Some games wait for this latched behaviour. The flag is cleared
     * when the status register is read via f18a_readctrl().
     */
    const unsigned char irq_pending =
        ((g_reg[1] & F18A_REG1_INT_ENABLE) && (g_status & F18A_STATUS_VBLANK)) ? 1u : 0u;

    g_loop_counter++;

    /*
     * One cv.cpp VDP loop call corresponds to one emulated scanline.
     * Use the machine's configured line count: 262 NTSC or 313 PAL.
     */
    if (g_loop_counter >= g_scanlines)
    {
        g_loop_counter = 0;

        /* The host 80-column terminal takes the screen when it is up - it lives in
           f18a_term80.c now, and presents its own frame and blinks its own cursor.
           The pico9918 bridge calls the same function at its porch. */
        if (!f18a_present_80col_overlay())
            f18a_render_frame();

        g_status |= F18A_STATUS_VBLANK;

        if (g_reg[1] & F18A_REG1_INT_ENABLE)
            return 1;
    }

    return irq_pending;
}

void f18a_writedata(unsigned char value)
{
    g_unlock_stage = 0;

    if (g_palette_mode)
    {
        if (!g_palette_byte_latch)
        {
            g_palette_first_byte = value;
            g_palette_byte_latch = 1;
        }
        else
        {
            g_palette12[g_palette_address & 0x3Fu] =
                (unsigned short)(((unsigned short)(g_palette_first_byte & 0x0Fu) << 8) | value);
            g_palette_dirty = 1;
            g_palette_byte_latch = 0;

            if (g_palette_auto && g_palette_address != 0x3Fu)
                g_palette_address = (unsigned char)((g_palette_address + 1u) & 0x3Fu);
            else
                g_palette_mode = 0;
        }

        g_ctrl_latch = 0;
        return;
    }

    g_vram[g_address & F18A_VRAM_MASK] = value;
    g_address = (g_address + 1u) & F18A_VRAM_MASK;
    g_ctrl_latch = 0;
}

unsigned char f18a_readdata(void)
{
    g_unlock_stage = 0;
    unsigned char result = g_read_buffer;

    g_read_buffer = g_vram[g_address & F18A_VRAM_MASK];
    g_address = (g_address + 1u) & F18A_VRAM_MASK;
    g_ctrl_latch = 0;

    return result;
}

unsigned char f18a_writectrl(unsigned char value)
{
    if (!g_ctrl_latch)
    {
        g_first_ctrl_byte = value;
        g_ctrl_latch = 1;
        return 0;
    }

    if (value & 0x80u)
    {
        const unsigned int reg = value & 0x3Fu;

        if (!g_erm_unlocked && reg == 57u && g_first_ctrl_byte == 0x1Cu)
        {
            if (g_unlock_stage)
            {
                g_erm_unlocked = 1;
                g_unlock_stage = 0;
            }
            else
            {
                g_unlock_stage = 1;
            }
        }
        else if (g_erm_unlocked && reg == 57u)
        {
            /* Any write to VR57 while unlocked returns to compatibility mode. */
            g_erm_unlocked = 0;
            /*
             * Treat >1C as the first write of a fresh unlock sequence too.
             * Some F18A-aware runtimes initialize ERM before their own
             * presence test and then issue the normal two-write sequence
             * again.  This keeps that sequence deterministic while an
             * unrelated VR57 value still performs a plain relock.
             */
            g_unlock_stage = (g_first_ctrl_byte == 0x1Cu) ? 1u : 0u;
        }
        else
        {
            const unsigned int effective_reg = g_erm_unlocked ? reg : (reg & 7u);
            g_unlock_stage = 0;

            if (effective_reg < F18A_REGISTER_COUNT)
            {
                g_reg[effective_reg] = g_first_ctrl_byte;
                if (g_erm_unlocked && effective_reg == 15u)
                    g_status_select = g_first_ctrl_byte & 0x0Fu;
                else if (g_erm_unlocked && effective_reg == 47u)
                {
                    g_palette_mode = (g_first_ctrl_byte & 0x80u) ? 1u : 0u;
                    g_palette_auto = (g_first_ctrl_byte & 0x40u) ? 1u : 0u;
                    g_palette_address = g_first_ctrl_byte & 0x3Fu;
                    g_palette_byte_latch = 0;
                }
                else if (g_erm_unlocked && effective_reg == 55u)
                {
                    f18a_gpu_start(&g_gpu,
                        (unsigned short)(((unsigned int)g_reg[54] << 8) | g_reg[55]));
                    f18a_gpu_run(1024u, -1);
                }
            }
        }
    }
    else
    {
        g_unlock_stage = 0;
        g_address = (((unsigned int)(value & 0x3Fu)) << 8) | g_first_ctrl_byte;
        g_address &= F18A_VRAM_MASK;

        if ((value & 0x40u) == 0)
        {
            g_read_buffer = g_vram[g_address & F18A_VRAM_MASK];
            g_address = (g_address + 1u) & F18A_VRAM_MASK;
        }
    }

    g_ctrl_latch = 0;
    return 0;
}

unsigned char f18a_readctrl(void)
{
    unsigned char result;

    g_unlock_stage = 0;

    if (!g_erm_unlocked || g_status_select == 0u)
    {
        result = g_status;
        g_status &= (unsigned char)~F18A_STATUS_VBLANK;
    }
    else if (g_status_select == 1u)
    {
        /* F18A MK1 identity bits.  Low condition bits are zero for now. */
        result = 0xE0u;
    }
    else if (g_status_select == 14u)
    {
        /* Report a compatible MK1 firmware revision (major 1, minor B). */
        result = 0x1Bu;
    }
    else
    {
        result = 0x00u;
    }

    g_ctrl_latch = 0;
    return result;
}

unsigned char f18a_get_register(unsigned char reg)
{
    return g_reg[reg % F18A_REGISTER_COUNT];
}

unsigned char f18a_peek_vram(unsigned int address)
{
    return g_vram[address & F18A_VRAM_MASK];
}

void f18a_poke_vram(unsigned int address, unsigned char value)
{
    g_vram[address & F18A_VRAM_MASK] = value;
}

unsigned int f18a_get_vram_address(void)
{
    return g_address & F18A_VRAM_MASK;
}

unsigned char f18a_get_status(void)
{
    return g_status;
}

unsigned char f18a_get_enhanced_register(unsigned char reg)
{
    return g_reg[reg % F18A_REGISTER_COUNT];
}

void f18a_set_enhanced_register(unsigned char reg, unsigned char value)
{
    g_reg[reg % F18A_REGISTER_COUNT] = value;
}

unsigned short f18a_get_palette_entry(unsigned int index)
{
    return g_palette12[index % F18A_PALETTE_ENTRIES] & 0x0FFFu;
}

void f18a_set_palette_entry(unsigned int index, unsigned short rgb12)
{
    g_palette12[index % F18A_PALETTE_ENTRIES] = rgb12 & 0x0FFFu;
    g_palette_dirty = 1;
}

unsigned char f18a_palette_is_dirty(void)
{
    return g_palette_dirty;
}

void f18a_palette_clear_dirty(void)
{
    g_palette_dirty = 0;
}
