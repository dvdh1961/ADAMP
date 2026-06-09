#include "f18a.h"
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

#define F18A_VRAM_SIZE       0x4000u   /* TMS-compatible 16 KiB for now */
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
static int g_f18a_80col_enabled = 0;
static int g_f18a_80col_selftest_enabled = 0;
static int g_f18a_cpm40_shift_left = 0;

/*
 * C7: internal 80x24 text buffer.
 * This is independent from normal TMS-compatible VRAM rendering.
 * Later CP/M/debug/terminal code can write here directly.
 */
static unsigned char g_80col_char[F18A_80COL_ROWS][F18A_80COL_COLS];
static unsigned char g_80col_fg  [F18A_80COL_ROWS][F18A_80COL_COLS];
static unsigned char g_80col_bg  [F18A_80COL_ROWS][F18A_80COL_COLS];
static int g_80col_buffer_initialized = 0;

static unsigned char  g_vram[F18A_VRAM_SIZE];
static unsigned char  g_reg[F18A_REGISTER_COUNT];

/*
 * infrastructure only.
 * The renderer still uses the fixed TMS-compatible palette for now.
 * These entries are stored so later F18A palette writes can be connected
 * without changing the TMS-compatible rendering path again.
 */
static unsigned short g_palette12[F18A_PALETTE_ENTRIES];
static unsigned char  g_palette_dirty = 0;

static unsigned char g_status = 0;
static unsigned char g_read_buffer = 0;

static unsigned int  g_address = 0;
static unsigned char g_first_ctrl_byte = 0;
static int           g_ctrl_latch = 0;

static unsigned int g_scanlines = F18A_NTSC_SCANLINES;

static int g_logged_reset = 0;
static int g_logged_loop = 0;
static unsigned int g_loop_counter = 0;
static unsigned int g_frame_counter = 0;

static unsigned int f18a_apply_timing_adjust(unsigned int base_lines)
{
    int adjusted = (int)base_lines + F18A_TIMING_ADJUST;

    /* Safety guard: do not allow an invalid frame length. */
    if (adjusted < 192)
        adjusted = (int)base_lines;

    return (unsigned int)adjusted;
}


/*
 * debug logging
 * --------------------
 * Keep this at 1 while validating mode/register setup.
 * Set to 0 later to silence the register logs.
 */
#define F18A_DEBUG_REG_WRITES 0

#if F18A_DEBUG_REG_WRITES
static unsigned char g_reg_last_logged[F18A_REGISTER_COUNT];
static unsigned char g_reg_has_logged[F18A_REGISTER_COUNT];

static void f18a_log_register_write(unsigned int reg, unsigned char value)
{
    if (reg >= F18A_REGISTER_COUNT)
        return;

    /* Avoid endless spam: only log when the value changed. */
    if (g_reg_has_logged[reg] && g_reg_last_logged[reg] == value)
        return;

    g_reg_has_logged[reg] = 1;
    g_reg_last_logged[reg] = value;

    if (reg <= 7)
    {
        const unsigned int r0 = (reg == 0) ? value : g_reg[0];
        const unsigned int r1 = (reg == 1) ? value : g_reg[1];
        const int m1 = (r1 & F18A_MODE_M1) ? 1 : 0;
        const int m2 = (r1 & F18A_MODE_M2) ? 1 : 0;
        const int m3 = (r0 & F18A_MODE_M3) ? 1 : 0;

        const char* mode = "Graphics I";
        if (m1 && !m2 && !m3)      mode = "Text";
        else if (!m1 && m2 && !m3) mode = "Multicolor";
        else if (!m1 && !m2 && m3) mode = "Graphics II";

        printf("[F18A] REG%u = %02X  mode=%s  R0=%02X R1=%02X R2=%02X R3=%02X R4=%02X R5=%02X R6=%02X R7=%02X\n",
               reg, value, mode,
               (unsigned)g_reg[0], (unsigned)g_reg[1], (unsigned)g_reg[2], (unsigned)g_reg[3],
               (unsigned)g_reg[4], (unsigned)g_reg[5], (unsigned)g_reg[6], (unsigned)g_reg[7]);
        fflush(stdout);
    }
    else if (reg < 16)
    {
        printf("[F18A] REG%u = %02X\n", reg, value);
        fflush(stdout);
    }
    else
    {
        printf("[F18A] EXT REG%u = %02X stored, no visual effect yet\n", reg, value);
        fflush(stdout);
    }
}
#endif

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

static inline uint32_t f18a_color(unsigned int idx)
{
    /*
     * keep rendering on the stable TMS-compatible palette for now.
     * Later we can switch this to g_palette12[] when F18A palette register
     * writes are fully connected and validated.
     */
    return s_tms_palette[idx & 0x0Fu];
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

static void f18a_draw_sprites_on_line(int y, uint32_t* line)
{
    const unsigned int attr_base = ((unsigned int)(g_reg[5] & 0x7Fu) << 7) & F18A_VRAM_MASK;
    const unsigned int patt_base = ((unsigned int)(g_reg[6] & 0x07u) << 11) & F18A_VRAM_MASK;

    /*
     * TMS9918A sprite list:
     * Y = 0xD0 betekent EINDE VAN DE SPRITE LIJST.
     * Dus eerst zoeken hoeveel sprites geldig zijn.
     */
    int sprite_count = 32;

    for (int i = 0; i < 32; ++i)
    {
        const unsigned int sa = attr_base + (unsigned int)i * 4u;
        const unsigned char sy_raw = f18a_vram_read(sa + 0u);

        if (sy_raw == 0xD0u)
        {
            sprite_count = i;
            break;
        }
    }

    /*
     * Daarna tekenen van achter naar voor voor correcte prioriteit.
     * Maar alleen de geldige sprites vóór de 0xD0 marker.
     */
    for (int i = sprite_count - 1; i >= 0; --i)
    {
        const unsigned int sa = attr_base + (unsigned int)i * 4u;
        const unsigned char sy_raw = f18a_vram_read(sa + 0u);

        const int sy = ((int)sy_raw + 1) & 0xFF;
        const int sprite_16 = (g_reg[1] & F18A_REG1_SPRITE_16) ? 1 : 0;
        const int sprite_size = sprite_16 ? 16 : 8;
        const int rel_y = y - sy;

        if (rel_y < 0 || rel_y >= sprite_size)
            continue;

        int sx = (int)f18a_vram_read(sa + 1u);
        unsigned char pattern = f18a_vram_read(sa + 2u);
        const unsigned char color_byte = f18a_vram_read(sa + 3u);
        const unsigned int color = color_byte & 0x0Fu;

        if (color == 0)
            continue;

        if (color_byte & 0x80u)
            sx -= 32;

        const uint32_t argb = f18a_color(color);

        if (!sprite_16)
        {
            const unsigned char bits =
                f18a_vram_read(patt_base + ((unsigned int)pattern << 3) + (unsigned int)rel_y);

            for (int bit = 0; bit < 8; ++bit)
            {
                if ((bits & (0x80u >> bit)) == 0)
                    continue;

                const int px = sx + bit;
                if (px >= 0 && px < VB_WIDTH)
                    line[px] = argb;
            }
        }
        else
        {
            const unsigned int base_pattern = (unsigned int)(pattern & 0xFCu);
            const unsigned int row = (unsigned int)(rel_y & 7);

            const unsigned int left_pattern  = base_pattern + ((rel_y >= 8) ? 1u : 0u);
            const unsigned int right_pattern = base_pattern + ((rel_y >= 8) ? 3u : 2u);

            const unsigned char left_bits =
                f18a_vram_read(patt_base + (left_pattern << 3) + row);

            const unsigned char right_bits =
                f18a_vram_read(patt_base + (right_pattern << 3) + row);

            for (int bit = 0; bit < 8; ++bit)
            {
                if (left_bits & (0x80u >> bit))
                {
                    const int px = sx + bit;
                    if (px >= 0 && px < VB_WIDTH)
                        line[px] = argb;
                }

                if (right_bits & (0x80u >> bit))
                {
                    const int px = sx + 8 + bit;
                    if (px >= 0 && px < VB_WIDTH)
                        line[px] = argb;
                }
            }
        }
    }
}

static void f18a_log_once_reset(void)
{
    if (!g_logged_reset)
    {
        //printf("[F18A] B3.2b reset/state active\n");
        //fflush(stdout);
        g_logged_reset = 1;
    }
}

static void f18a_log_once_loop(void)
{
    if (!g_logged_loop)
    {
        //printf("[F18A] B3.2b loop active, 16x16 sprite pattern order fixed\n");
        //fflush(stdout);
        g_logged_loop = 1;
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

    for (int y = 0; y < VB_HEIGHT; ++y)
    {
        const int tile_y = y >> 3;
        const int row    = y & 7;

        for (int x = 0; x < VB_WIDTH; ++x)
            line[x] = border;

        for (int tile_x = 0; tile_x < 32; ++tile_x)
        {
            const unsigned int name_addr = name_base + (unsigned int)(tile_y * 32 + tile_x);
            const unsigned char chr = f18a_vram_read(name_addr);
            const unsigned char pat = f18a_vram_read(pattern_base + ((unsigned int)chr << 3) + (unsigned int)row);
            const unsigned char col = f18a_vram_read(color_base + (unsigned int)(chr >> 3));

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

        f18a_draw_sprites_on_line(y, line);
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

        f18a_draw_sprites_on_line(y, line);

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

        f18a_draw_sprites_on_line(y, line);
        vb_present_scanline(y, line);
    }

    vb_present_frame();
    video_set_dirty(1);
}

static uint8_t f18a_font5x7(char ch, int row)
{
    /* compact built-in font, 5 pixels wide inside a 6-pixel cell */
    static const uint8_t space[7]      = {0x00,0x00,0x00,0x00,0x00,0x00,0x00};
    static const uint8_t excl[7]       = {0x04,0x04,0x04,0x04,0x04,0x00,0x04};
    static const uint8_t quote[7]      = {0x0A,0x0A,0x0A,0x00,0x00,0x00,0x00};
    static const uint8_t hash[7]       = {0x0A,0x1F,0x0A,0x0A,0x1F,0x0A,0x00};
    static const uint8_t dollar[7]     = {0x04,0x0F,0x14,0x0E,0x05,0x1E,0x04};
    static const uint8_t percent[7]    = {0x18,0x19,0x02,0x04,0x08,0x13,0x03};
    static const uint8_t amp[7]        = {0x0C,0x12,0x14,0x08,0x15,0x12,0x0D};
    static const uint8_t apost[7]      = {0x04,0x04,0x08,0x00,0x00,0x00,0x00};
    static const uint8_t lparen[7]     = {0x02,0x04,0x08,0x08,0x08,0x04,0x02};
    static const uint8_t rparen[7]     = {0x08,0x04,0x02,0x02,0x02,0x04,0x08};
    static const uint8_t star[7]       = {0x00,0x15,0x0E,0x1F,0x0E,0x15,0x00};
    static const uint8_t plus[7]       = {0x00,0x04,0x04,0x1F,0x04,0x04,0x00};
    static const uint8_t comma[7]      = {0x00,0x00,0x00,0x00,0x0C,0x04,0x08};
    static const uint8_t dash[7]       = {0x00,0x00,0x00,0x1F,0x00,0x00,0x00};
    static const uint8_t dot[7]        = {0x00,0x00,0x00,0x00,0x00,0x0C,0x0C};
    static const uint8_t slash[7]      = {0x01,0x02,0x02,0x04,0x08,0x08,0x10};

    static const uint8_t colon[7]      = {0x00,0x0C,0x0C,0x00,0x0C,0x0C,0x00};
    static const uint8_t semicolon[7]  = {0x00,0x0C,0x0C,0x00,0x0C,0x04,0x08};
    static const uint8_t less[7]       = {0x02,0x04,0x08,0x10,0x08,0x04,0x02};
    static const uint8_t equal[7]      = {0x00,0x00,0x1F,0x00,0x1F,0x00,0x00};
    static const uint8_t greater[7]    = {0x10,0x08,0x04,0x02,0x04,0x08,0x10};
    static const uint8_t question[7]   = {0x0E,0x11,0x01,0x02,0x04,0x00,0x04};
    static const uint8_t atsign[7]     = {0x0E,0x11,0x17,0x15,0x17,0x10,0x0E};

    static const uint8_t lbrack[7]     = {0x0E,0x08,0x08,0x08,0x08,0x08,0x0E};
    static const uint8_t backslash[7]  = {0x10,0x08,0x08,0x04,0x02,0x02,0x01};
    static const uint8_t rbrack[7]     = {0x0E,0x02,0x02,0x02,0x02,0x02,0x0E};
    static const uint8_t caret[7]      = {0x04,0x0A,0x11,0x00,0x00,0x00,0x00};
    static const uint8_t underscore[7] = {0x00,0x00,0x00,0x00,0x00,0x00,0x1F};
    static const uint8_t grave[7]      = {0x08,0x04,0x02,0x00,0x00,0x00,0x00};
    static const uint8_t lbrace[7]     = {0x02,0x04,0x04,0x08,0x04,0x04,0x02};
    static const uint8_t pipe[7]       = {0x04,0x04,0x04,0x00,0x04,0x04,0x04};
    static const uint8_t rbrace[7]     = {0x08,0x04,0x04,0x02,0x04,0x04,0x08};
    static const uint8_t tilde[7]      = {0x00,0x00,0x08,0x15,0x02,0x00,0x00};

    static const uint8_t nums[10][7] = {
        /* 0 */ {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E},
        /* 1 */ {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E},
        /* 2 */ {0x0E,0x11,0x01,0x02,0x04,0x08,0x1F},
        /* 3 */ {0x1F,0x02,0x04,0x02,0x01,0x11,0x0E},
        /* 4 */ {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02},
        /* 5 */ {0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E},
        /* 6 */ {0x06,0x08,0x10,0x1E,0x11,0x11,0x0E},
        /* 7 */ {0x1F,0x01,0x02,0x04,0x08,0x08,0x08},
        /* 8 */ {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E},
        /* 9 */ {0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C}
    };

    static const uint8_t upper[26][7] = {
        /* A */ {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11},
        /* B */ {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E},
        /* C */ {0x0E,0x11,0x10,0x10,0x10,0x11,0x0E},
        /* D */ {0x1E,0x11,0x11,0x11,0x11,0x11,0x1E},
        /* E */ {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F},
        /* F */ {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10},
        /* G */ {0x0E,0x11,0x10,0x17,0x11,0x11,0x0F},
        /* H */ {0x11,0x11,0x11,0x1F,0x11,0x11,0x11},
        /* I */ {0x0E,0x04,0x04,0x04,0x04,0x04,0x0E},
        /* J */ {0x01,0x01,0x01,0x01,0x11,0x11,0x0E},
        /* K */ {0x11,0x12,0x14,0x18,0x14,0x12,0x11},
        /* L */ {0x10,0x10,0x10,0x10,0x10,0x10,0x1F},
        /* M */ {0x11,0x1B,0x15,0x15,0x11,0x11,0x11},
        /* N */ {0x11,0x19,0x15,0x13,0x11,0x11,0x11},
        /* O */ {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E},
        /* P */ {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10},
        /* Q */ {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D},
        /* R */ {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11},
        /* S */ {0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E},
        /* T */ {0x1F,0x04,0x04,0x04,0x04,0x04,0x04},
        /* U */ {0x11,0x11,0x11,0x11,0x11,0x11,0x0E},
        /* V */ {0x11,0x11,0x11,0x11,0x11,0x0A,0x04},
        /* W */ {0x11,0x11,0x11,0x15,0x15,0x1B,0x11},
        /* X */ {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11},
        /* Y */ {0x11,0x11,0x0A,0x04,0x04,0x04,0x04},
        /* Z */ {0x1F,0x01,0x02,0x04,0x08,0x10,0x1F}
    };

    static const uint8_t lower[26][7] = {
        /* a */ {0x00,0x00,0x0E,0x01,0x0F,0x11,0x0F},
        /* b */ {0x10,0x10,0x16,0x19,0x11,0x11,0x1E},
        /* c */ {0x00,0x00,0x0E,0x10,0x10,0x11,0x0E},
        /* d */ {0x01,0x01,0x0D,0x13,0x11,0x11,0x0F},
        /* e */ {0x00,0x00,0x0E,0x11,0x1F,0x10,0x0E},
        /* f */ {0x06,0x08,0x08,0x1E,0x08,0x08,0x08},
        /* g */ {0x00,0x00,0x0F,0x11,0x0F,0x01,0x0E},
        /* h */ {0x10,0x10,0x16,0x19,0x11,0x11,0x11},
        /* i */ {0x04,0x00,0x0C,0x04,0x04,0x04,0x0E},
        /* j */ {0x02,0x00,0x06,0x02,0x02,0x12,0x0C},
        /* k */ {0x10,0x10,0x12,0x14,0x18,0x14,0x12},
        /* l */ {0x0C,0x04,0x04,0x04,0x04,0x04,0x0E},
        /* m */ {0x00,0x00,0x1A,0x15,0x15,0x11,0x11},
        /* n */ {0x00,0x00,0x16,0x19,0x11,0x11,0x11},
        /* o */ {0x00,0x00,0x0E,0x11,0x11,0x11,0x0E},
        /* p */ {0x00,0x00,0x1E,0x11,0x1E,0x10,0x10},
        /* q */ {0x00,0x00,0x0F,0x11,0x0F,0x01,0x01},
        /* r */ {0x00,0x00,0x16,0x19,0x10,0x10,0x10},
        /* s */ {0x00,0x00,0x0F,0x10,0x0E,0x01,0x1E},
        /* t */ {0x08,0x08,0x1E,0x08,0x08,0x09,0x06},
        /* u */ {0x00,0x00,0x11,0x11,0x11,0x13,0x0D},
        /* v */ {0x00,0x00,0x11,0x11,0x11,0x0A,0x04},
        /* w */ {0x00,0x00,0x11,0x11,0x15,0x15,0x0A},
        /* x */ {0x00,0x00,0x11,0x0A,0x04,0x0A,0x11},
        /* y */ {0x00,0x00,0x11,0x11,0x0F,0x01,0x0E},
        /* z */ {0x00,0x00,0x1F,0x02,0x04,0x08,0x1F}
    };

    if (row < 0 || row >= 7)
        return 0;

    if (ch >= '0' && ch <= '9') return nums[ch - '0'][row];
    if (ch >= 'A' && ch <= 'Z') return upper[ch - 'A'][row];
    if (ch >= 'a' && ch <= 'z') return lower[ch - 'a'][row];

    switch (ch) {
        case ' ': return space[row];
        case '!': return excl[row];
        case '"': return quote[row];
        case '#': return hash[row];
        case '$': return dollar[row];
        case '%': return percent[row];
        case '&': return amp[row];
        case '\'': return apost[row];
        case '(': return lparen[row];
        case ')': return rparen[row];
        case '*': return star[row];
        case '+': return plus[row];
        case ',': return comma[row];
        case '-': return dash[row];
        case '.': return dot[row];
        case '/': return slash[row];
        case ':': return colon[row];
        case ';': return semicolon[row];
        case '<': return less[row];
        case '=': return equal[row];
        case '>': return greater[row];
        case '?': return question[row];
        case '@': return atsign[row];
        case '[': return lbrack[row];
        case '\\': return backslash[row];
        case ']': return rbrack[row];
        case '^': return caret[row];
        case '_': return underscore[row];
        case '`': return grave[row];
        case '{': return lbrace[row];
        case '|': return pipe[row];
        case '}': return rbrace[row];
        case '~': return tilde[row];
        default:  return space[row];
    }
}

static void f18a_80col_init_buffer_once(void)
{
    if (g_80col_buffer_initialized)
        return;

    f18a_80col_clear(' ', 15, 1);
    g_80col_buffer_initialized = 1;
}

void f18a_80col_clear(unsigned char ch, unsigned char fg, unsigned char bg)
{
    for (unsigned int r = 0; r < F18A_80COL_ROWS; ++r)
    {
        for (unsigned int c = 0; c < F18A_80COL_COLS; ++c)
        {
            g_80col_char[r][c] = ch;
            g_80col_fg[r][c] = fg & 0x0Fu;
            g_80col_bg[r][c] = bg & 0x0Fu;
        }
    }

    g_80col_buffer_initialized = 1;
}

void f18a_80col_put_char(unsigned int row, unsigned int col,
                         unsigned char ch,
                         unsigned char fg,
                         unsigned char bg)
{
    if (row >= F18A_80COL_ROWS || col >= F18A_80COL_COLS)
        return;

    g_80col_char[row][col] = ch;
    g_80col_fg[row][col] = fg & 0x0Fu;
    g_80col_bg[row][col] = bg & 0x0Fu;
}

void f18a_80col_write_text(unsigned int row, unsigned int col,
                           const char* text,
                           unsigned char fg,
                           unsigned char bg)
{
    if (!text || row >= F18A_80COL_ROWS || col >= F18A_80COL_COLS)
        return;

    while (*text && col < F18A_80COL_COLS)
    {
        f18a_80col_put_char(row, col, (unsigned char)*text, fg, bg);
        ++text;
        ++col;
    }
}

static void f18a_80col_build_selftest_text(void)
{
    f18a_80col_clear(' ', 15, 1);

    f18a_80col_write_text(0,  0, "ADAM+ F18A 80 COLUMN TEXT BUFFER API - 480X192 INTERNAL", 7, 1);
    f18a_80col_write_text(1,  0, "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ01234567", 15, 1);
    f18a_80col_write_text(2,  0, "COL 00000000011111111112222222222333333333344444444445555555555666666666677777777", 10, 1);
    f18a_80col_write_text(3,  0, "COL 01234567890123456789012345678901234567890123456789012345678901234567890123456789", 11, 1);
    f18a_80col_write_text(4,  0, "ROW 04  THE QUICK BROWN FOX JUMPS OVER THE LAZY DOG - 80 COLUMNS VISIBLE", 15, 1);
    f18a_80col_write_text(5,  0, "ROW 05  THIS SCREEN IS DRAWN FROM THE INTERNAL 80X24 TEXT BUFFER", 14, 1);
    f18a_80col_write_text(6,  0, "ROW 06  USE f18a_80col_put_char AND f18a_80col_write_text FOR CP/M LATER", 3, 1);
    f18a_80col_write_text(7,  0, "ROW 07  6X8 CELLS - 80 COLUMNS X 24 ROWS", 12, 1);
    f18a_80col_write_text(9,  0, "##########----------::::::::::++++++++++..........//////////##########", 9, 1);
    f18a_80col_write_text(11, 0, "ROW 11  NORMAL F18A GAME RENDERING IS UNCHANGED WHEN SELF TEST IS OFF", 15, 1);
    f18a_80col_write_text(14, 0, "ROW 14  NEXT STEP CAN CONNECT CP/M OUTPUT TO THIS TEXT BUFFER", 7, 1);
    f18a_80col_write_text(23, 0, "ROW 23  END OF F18A 80 COLUMN TEXT BUFFER TEST", 10, 1);
}

static void f18a_render_80col_textbuffer(void)
{
    f18a_80col_init_buffer_once();

    uint32_t line[F18A_80COL_WIDTH];

    for (int y = 0; y < F18A_80COL_HEIGHT; ++y)
    {
        const unsigned int text_row = (unsigned int)y >> 3;
        const int pixel_row = y & 7;

        for (unsigned int col = 0; col < F18A_80COL_COLS; ++col)
        {
            const unsigned char ch = g_80col_char[text_row][col];
            const uint32_t fg = f18a_color(g_80col_fg[text_row][col]);
            const uint32_t bg = f18a_color(g_80col_bg[text_row][col]);
            const uint8_t bits = f18a_font5x7((char)ch, pixel_row);
            const unsigned int x0 = col * 6u;

            for (unsigned int b = 0; b < 6u; ++b)
                line[x0 + b] = (b < 5u && (bits & (0x10u >> b))) ? fg : bg;
        }

        vb_present_scanline_ex(y, line, F18A_80COL_WIDTH);
    }

    vb_present_frame();
    video_set_dirty(1);
}

static void f18a_render_frame(void)
{
    if (g_f18a_80col_enabled)
    {
        /*
         * C8-safe-2:
         * The HardwareWindow "F18A 80-column self-test" checkbox now
         * exercises the separate TERM80 layer instead of writing the test
         * screen directly from f18a.c.
         *
         * This keeps CP/M/T-DOS terminal logic out of the VDP renderer.
         */
        if (g_f18a_80col_selftest_enabled)
        {
            if (!f18a_term80_is_enabled() || !g_80col_buffer_initialized)
                f18a_term80_demo_screen();
        }

        f18a_render_80col_textbuffer();
        return;
    }

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

    if (m1 && !m2 && !m3)
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


void f18a_set_80col_enabled(int enabled)
{
    g_f18a_80col_enabled = enabled ? 1 : 0;
}

int f18a_is_80col_enabled(void)
{
    return g_f18a_80col_enabled;
}

void f18a_set_80col_selftest_enabled(int enabled)
{
    g_f18a_80col_selftest_enabled = enabled ? 1 : 0;

    /*
     * C8-safe-2:
     * Self-test is the only UI option that should force the 480x192
     * 80-column renderer for now. When enabled, build the screen through
     * the separate TERM80 terminal layer. When disabled, return to normal
     * TMS-compatible F18A rendering.
     */
    if (g_f18a_80col_selftest_enabled)
    {
        f18a_term80_set_enabled(1);
        f18a_term80_demo_screen();
        g_f18a_80col_enabled = 1;
    }
    else
    {
        f18a_term80_set_enabled(0);
        g_f18a_80col_enabled = 0;
    }
}

int f18a_is_80col_selftest_enabled(void)
{
    return g_f18a_80col_selftest_enabled;
}

void f18a_set_enabled(int enabled)
{
    g_f18a_enabled = enabled ? 1 : 0;
    g_logged_loop = 0;
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
    f18a_reset_palette();
    g_80col_buffer_initialized = 0;
#if F18A_DEBUG_REG_WRITES
    memset(g_reg_last_logged, 0x00, sizeof(g_reg_last_logged));
    memset(g_reg_has_logged,  0x00, sizeof(g_reg_has_logged));
#endif

    g_status = 0;
    g_read_buffer = 0;

    g_address = 0;
    g_first_ctrl_byte = 0;
    g_ctrl_latch = 0;

    g_loop_counter = 0;
    g_frame_counter = 0;
    g_logged_loop = 0;

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
    f18a_log_once_reset();
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
    f18a_log_once_loop();

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
        g_frame_counter++;

        /* Blink TERM80 cursor once per rendered frame, before presenting. */
        if (f18a_term80_is_enabled())
            f18a_term80_tick();

        f18a_render_frame();

        g_status |= F18A_STATUS_VBLANK;

        if (g_reg[1] & F18A_REG1_INT_ENABLE)
            return 1;
    }

    return irq_pending;
}

void f18a_writedata(unsigned char value)
{
    g_vram[g_address & F18A_VRAM_MASK] = value;
    g_address = (g_address + 1u) & F18A_VRAM_MASK;
    g_ctrl_latch = 0;
}

unsigned char f18a_readdata(void)
{
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
        if (reg < F18A_REGISTER_COUNT)
        {
            g_reg[reg] = g_first_ctrl_byte;
#if F18A_DEBUG_REG_WRITES
            f18a_log_register_write(reg, g_first_ctrl_byte);
#endif
        }
    }
    else
    {
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
    const unsigned char result = g_status;
    g_status &= (unsigned char)~F18A_STATUS_VBLANK;
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
