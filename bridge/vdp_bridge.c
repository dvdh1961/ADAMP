#include "vdp_bridge.h"
#include "video_bridge.h"

#include "pico9918.h"
#include "pico9918_frame.h"
#include "pico9918_config.h"
#include "gpu/gpu.h"
#include "overlay/diag.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* A full 640x480 VGA frame. The border is part of the picture, not something to
   crop - the splash and diagnostics panel are drawn into it. Vertical geometry is
   the library's: pico9918_frame_geometry() sets vPixelScale and vVirtualPixels per
   mode, so 30-row, double-row and 80-column need no cases here. */
#define VDP_H_VIRTUAL 640u
#define VDP_V_OUTPUT  480u

/* No horizontal geometry of our own. A line moves as 320 32-bit words holding two
   16-bit pixels each, which is the one pixel width the library ships, so its offsets
   land finished: 640 pixels, the picture doubled into 64..575 with 64 of border either
   side, and every one of them written on every line it handles. */

/* A little slack: a fine-h-scrolled tile layer's last quad can reach past the line. */
#define VDP_LINE_SLACK 16u

/* The PICO9918_INST macros require the instance to be named tms9918. */
#if !PICO9918_SINGLE_INSTANCE
static pico9918_t* tms9918 = 0;
#endif

/* The PICO9918's stored settings: a 256-byte config block, as the board keeps in
   flash. g_config is ADAMP's copy - validated, migrated, written to disk;
   g_deviceConfig is the instance's live block, obtained by the handshake in
   vdp_bridge_config_capture(). Bytes 0-7 are identity, which the bus cannot write
   (VR59 rejects an index below 8) but software reads back over VR58/SR12, so we push
   the whole block and read only byte 8 up. */
#define VDP_CONFIG_SETTABLE 8   /* first byte the device is allowed to write back */

/* Packed major(4) | minor(4) | patch(8) - pico9918_config_fields' introducedIn. */
#define VDP_CONFIG_VERSION                                      ((uint16_t)(((PICO9918_CORE_VER_MAJOR & 0x0f) << 12) |                  ((PICO9918_CORE_VER_MINOR & 0x0f) <<  8) |                   (PICO9918_CORE_VER_PATCH & 0xff)))

/* The identity we report: base model, board v1.0, and the linked core's version.
   Byte 0 doubles as the "is this block mine" test on load.
   HW_VERSION is a packed nibble pair, high major and low minor - real boards are
   v0.3 and v1.0, and 0.3 cannot be an integer. A plain 1 read back as "v0.1". */
#define VDP_CONFIG_MODEL      0
#define VDP_CONFIG_HW_VERSION 0x10

/* The personality ADAMP asked for. PICO9918 until told otherwise, so a caller that
   never selects one gets what this bridge has always rendered. */
static pico9918_chip_t g_chip = PICO9918_CHIP_PICO9918;

/* The splash, the diagnostics panels and the flash block are this board's alone. The
   library refuses them for the other two personalities (PICO9918_FEAT_OVERLAY); this is
   the same statement for the parts the bridge drives itself. */
static inline int vdp_bridge_is_pico9918(void)
{
    return g_chip == PICO9918_CHIP_PICO9918;
}

/* Whether a core is up. The personality can be chosen before there is one to tell. */
static int      g_coreUp = 0;

/* Set by the GUI thread in vdp_bridge_set_chip(), consumed by the emulator thread in
   vdp_bridge_loop(). One word written on one side and read on the other: the worst a
   torn read can cost is applying the personality one scanline later. */
static volatile int g_chipPending = 0;

/* The host overlay that can take a frame - see vdp_bridge_set_overlay_hook. */
static int      (*g_overlayPresent)(void) = 0;

static uint8_t  g_config[CONFIG_BYTES];
static uint8_t* g_deviceConfig  = 0;
static char     g_configPath[512];
static int      g_configCapturing = 0;

static unsigned int g_scanlines = 262u;
static unsigned int g_line      = 0u;

static PICO9918_PIXEL_T g_pixels[VDP_H_VIRTUAL + VDP_LINE_SLACK];

/* The library renders the board's format, BGR12 in the low 12 bits; the video bridge
   takes ARGB32. One lookup a pixel, in the copy into the frame that had to happen
   anyway, and the transform itself is the library's so the nibble order lives in one
   place. Indexed by the low 12 bits: the dead copy of green in 15-12 is masked off, as
   the library masks it wherever it matters. */
static uint32_t g_argb[VDP_H_VIRTUAL];
static uint32_t g_bgr12Argb[4096];

static void vdp_bridge_init_pixel_map(void)
{
    static int built = 0;

    if (built) {
        return;
    }
    built = 1;

    for (unsigned int v = 0u; v < 4096u; ++v) {
        g_bgr12Argb[v] = 0xff000000u | pico9918_pixel_rgb888((PICO9918_PIXEL_T)v);
    }
}

static void vdp_bridge_convert_line(void)
{
    for (unsigned int x = 0u; x < VDP_H_VIRTUAL; ++x) {
        g_argb[x] = g_bgr12Argb[g_pixels[x] & 0x0fffu];
    }
}

static pico9918_scanline_params_t g_params;
static pico9918_frame_display_t   g_display;
static pico9918_frame_geometry_t  g_geometry;

/* cv.cpp calls once per TMS scanline, but the field is the VGA one: one virtual line
   per call at vPixelScale 2, two under double rows. g_fieldLines includes the porch,
   so the frame ends where the emulated machine's does. */
static unsigned int g_linesPerCall = 1u;
static unsigned int g_fieldLines   = 262u;

static void vdp_bridge_recompute_cadence(void)
{
    g_linesPerCall = (g_display.vPixelScale >= 2u) ? 1u : 2u;
    g_fieldLines   = g_scanlines * g_linesPerCall;
    g_params.vVirtualPixels = g_display.vVirtualPixels;
}

static void vdp_bridge_configure(void)
{
    g_params.hVirtualPixels       = (uint16_t)VDP_H_VIRTUAL;
    g_params.interlaced           = false;
    g_params.interlacedFieldOrder = 0u;

    /* vPixelScale and vVirtualPixels are seeded, then owned by the library. */
    g_display.displayPixels  = (int)VDP_V_OUTPUT;
    g_display.interlaced     = false;
    g_display.vPixelScale    = 2u;
    g_display.vVirtualPixels = (uint16_t)(VDP_V_OUTPUT / 2u);

    /* Geometry before the first frame, so the trigger line is armed from line 0. */
    g_geometry = pico9918_frame_geometry(PICO9918_INST &g_display);
    vdp_bridge_recompute_cadence();
}

/* ------------------------------------------------------------------------- */
/* Chip personality                                                          */
/* ------------------------------------------------------------------------- */

/* ADAMP's VdpType is the library's ladder in the same order, but map it rather than
   cast it: the two enumerations are owned by different repositories. */
static pico9918_chip_t vdp_bridge_chip_from_type(int vdpType)
{
    switch (vdpType) {
        case 0:  return PICO9918_CHIP_TMS9918A;
        case 1:  return PICO9918_CHIP_F18A;
        default: return PICO9918_CHIP_PICO9918;
    }
}

void vdp_bridge_set_chip(int vdpType)
{
    g_chip = vdp_bridge_chip_from_type(vdpType);

    /* Not applied here. This is called from the GUI thread - the hardware dialog - while
       the emulator thread may be part-way through a line, and pico9918_set_chip()
       rewrites lockedMask, isUnlocked and the feature mask, all of which the renderer
       reads as it goes. So flag it and let the emulator thread apply it at the top of
       the next scanline batch, where nothing is mid-line. The reset applies g_chip
       directly, because a reset is already on that thread. */
    if (g_coreUp) {
        g_chipPending = 1;
    }
}

void vdp_bridge_set_overlay_hook(int (*present)(void))
{
    g_overlayPresent = present;
}

/* ------------------------------------------------------------------------- */
/* PICO9918 stored config                                                    */
/* ------------------------------------------------------------------------- */

void vdp_bridge_set_config_path(const char* path)
{
    if (!path) {
        g_configPath[0] = '\0';
        return;
    }
    snprintf(g_configPath, sizeof(g_configPath), "%s", path);
}

static void vdp_bridge_config_stamp(void)
{
    g_config[PICO9918_CONF_PICO_MODEL]       = VDP_CONFIG_MODEL;
    g_config[PICO9918_CONF_HW_VERSION]       = VDP_CONFIG_HW_VERSION;
    g_config[PICO9918_CONF_SW_VERSION]       = (uint8_t)(VDP_CONFIG_VERSION >> 8);
    g_config[PICO9918_CONF_SW_PATCH_VERSION] = (uint8_t)(VDP_CONFIG_VERSION & 0xff);
}

static void vdp_bridge_config_store(void)
{
    if (!g_configPath[0]) {
        return;
    }

    FILE* f = fopen(g_configPath, "wb");
    if (!f) {
        return;
    }
    fwrite(g_config, 1, CONFIG_BYTES, f);
    fclose(f);
}

/* Read the block back and let the library validate it: bad blocks are defaulted, old
   ones migrated. It returns true when the stamp needs rewriting. */
static void vdp_bridge_config_load(void)
{
    memset(g_config, 0, sizeof(g_config));

    if (g_configPath[0]) {
        FILE* f = fopen(g_configPath, "rb");
        if (f) {
            if (fread(g_config, 1, CONFIG_BYTES, f) != CONFIG_BYTES) {
                memset(g_config, 0, sizeof(g_config));
            }
            fclose(f);
        }
    }

    const bool modelMatches = (g_config[PICO9918_CONF_PICO_MODEL] == VDP_CONFIG_MODEL);

    bool wasReset = false;
    const bool rewrite =
        pico9918_config_validate(g_config, modelMatches, VDP_CONFIG_VERSION, &wasReset);

    /*
     * Stamp unconditionally. Identity is ours, not the file's - the block on disk was
     * written by whatever build ran last, and a valid one is returned untouched, so
     * stamping only on rewrite left a stale identity to be pushed to the device.
     * That is what reported an old board revision and what failed a version probe.
     */
    const bool identityStale =
        (g_config[PICO9918_CONF_PICO_MODEL] != VDP_CONFIG_MODEL) ||
        (g_config[PICO9918_CONF_HW_VERSION] != VDP_CONFIG_HW_VERSION) ||
        (g_config[PICO9918_CONF_SW_VERSION] != (uint8_t)(VDP_CONFIG_VERSION >> 8)) ||
        (g_config[PICO9918_CONF_SW_PATCH_VERSION] != (uint8_t)(VDP_CONFIG_VERSION & 0xff));

    vdp_bridge_config_stamp();

    if (rewrite || identityStale) {
        vdp_bridge_config_store();
    }
}

/* The GPU's config-action hook. It hands over the live config block, the only route
   the public API offers to that pointer - hence the handshake below. */
static void vdp_bridge_config_saved(uint8_t* config, uint8_t key)
{
    g_deviceConfig = config;

    if (g_configCapturing) {
        return; /* the handshake, not a real save - nothing to persist */
    }

    (void)key; /* save, forced save, pending confirm and cancel all just persist */

    memcpy(g_config + VDP_CONFIG_SETTABLE,
           config + VDP_CONFIG_SETTABLE,
           CONFIG_BYTES - VDP_CONFIG_SETTABLE);
    vdp_bridge_config_stamp();
    vdp_bridge_config_store();
}

/* Put the saved settings back after the startup diagnostics screen has been forced
   on, which is what takes it away again. Runs ahead of the diagnostics refresh in
   the same frame, so the overlay reads restored bytes rather than stale ones. */
static void vdp_bridge_config_reload(void)
{
    if (!g_deviceConfig) {
        return;
    }
    memcpy(g_deviceConfig, g_config, CONFIG_BYTES);
}

/* Learn where the instance keeps its config block, and push the stored settings in.
   The library exposes no accessor, so we arm the save command as the configurator
   does and step the GPU once purely to be handed the pointer; g_configCapturing keeps
   that from being mistaken for a save. The closing VR50 write re-locks the F18A and
   marks the config dirty, so the settings are applied at end of frame. */
static void vdp_bridge_config_capture(void)
{
    g_configCapturing = 1;

    /* Unlock: two consecutive VR57 writes with the low two bits clear. */
    pico9918_write_reg_value(PICO9918_INST 0x80u | 0x39u, 0x1Cu);
    pico9918_write_reg_value(PICO9918_INST 0x80u | 0x39u, 0x1Cu);

    /* VR58 selects the config byte, VR59 writes it. */
    pico9918_write_reg_value(PICO9918_INST 0x80u | 58u, PICO9918_CONF_SAVE_TO_FLASH);
    pico9918_write_reg_value(PICO9918_INST 0x80u | 59u, 1u);
    pico9918_gpu_step(PICO9918_INST_ONLY);

    g_configCapturing = 0;

    if (g_deviceConfig) {
        memcpy(g_deviceConfig, g_config, CONFIG_BYTES);
    }

    pico9918_write_reg_value(PICO9918_INST 0x80u | 0x32u, 0xC0u);

    /* That write is the only way to reach the config-dirty flag (VR50 bit 6), and bit 7
       - which it needs - runs the library's whole vdpRegisterReset(). That leaves R1 and
       R7 at their power-on defaults with the display ACTIVE, undoing the blanking
       pico9918_reset() had just done deliberately. Put it back, or the PICO9918 comes out
       of every reset rendering uninitialised tables while the TMS9918A and F18A
       personalities - which never run this capture - come up correctly blanked. */
    pico9918_write_reg_value(PICO9918_INST 0x80u | 0x01u, 0x00u);
    pico9918_write_reg_value(PICO9918_INST 0x80u | 0x07u, 0x00u);
}

/* The diagnostics panel's host half. pico9918_diag_init() builds the glyph mask
   table and nothing in the library calls it; miss it and the panels draw backgrounds
   and no text. The rest are values only a host can know. */
static void vdp_bridge_diag_setup(void)
{
    static int initialised = 0;
    if (!initialised) {
        pico9918_diag_init();
        initialised = 1;
    }

    char hw[8];
    char fw[16];
    snprintf(hw, sizeof(hw), "%u.%u", VDP_CONFIG_HW_VERSION >> 4,
             VDP_CONFIG_HW_VERSION & 0x0f);
    snprintf(fw, sizeof(fw), "%u.%u.%u",
             PICO9918_CORE_VER_MAJOR, PICO9918_CORE_VER_MINOR, PICO9918_CORE_VER_PATCH);
    pico9918_diag_set_version_info(hw, fw);

    /* Retained by pointer, so the strings must outlive the call - literals do. */
    pico9918_diag_set_output_name("480P ", "@60");

    /* The preset the emulated board would run at, rather than the host's clock. */
    pico9918_diag_set_clock_hz(252000000.0f);
}

static int vdp_bridge_ensure(void)
{
    vdp_bridge_init_pixel_map();
#if PICO9918_SINGLE_INSTANCE
    if (!g_coreUp) {
        pico9918_init();
        pico9918_gpu_init(PICO9918_INST_ONLY);
        pico9918_gpu_set_config_save_callback(vdp_bridge_config_saved);
        pico9918_frame_set_config_reload_callback(vdp_bridge_config_reload);
        vdp_bridge_configure();
        g_coreUp = 1;
        pico9918_set_chip(PICO9918_INST g_chip);
    }
    return 1;
#else
    if (!tms9918) {
        tms9918 = pico9918_new();
        if (tms9918) {
            pico9918_gpu_init(PICO9918_INST_ONLY);
            pico9918_gpu_set_config_save_callback(vdp_bridge_config_saved);
            pico9918_frame_set_config_reload_callback(vdp_bridge_config_reload);
            vdp_bridge_configure();
            g_coreUp = 1;
            pico9918_set_chip(PICO9918_INST g_chip);
        }
    }
    return tms9918 != 0;
#endif
}

void vdp_bridge_reset(unsigned int scanlines)
{
    if (!vdp_bridge_ensure()) {
        return;
    }

    if (scanlines) {
        g_scanlines = scanlines;
    }
    g_line = 0u;

    /* Before the reset, so the reset's SR1 identifies the chip we are about to be. */
    g_chipPending = 0;
    pico9918_set_chip(PICO9918_INST g_chip);

    /* Clears VR56, so any program still running stops here rather than carrying on
       into the machine we are about to bring up. */
    pico9918_reset(PICO9918_INST_ONLY);

    pico9918_gpu_init(PICO9918_INST_ONLY);

    /* The flash block, the diagnostics panel and the splash are this board's - not a
       TMS9918A's and not an F18A's. The library refuses them for those personalities;
       skipping the host half is the same statement on this side. */
    if (vdp_bridge_is_pico9918()) {
        vdp_bridge_diag_setup();

        /* A board reads its settings out of flash at power-on. So do we. */
        vdp_bridge_config_load();
        vdp_bridge_config_capture();
    }

    vdp_bridge_configure();
    vdp_bridge_recompute_cadence();

    /* Hands the GPU to the library: it runs a program from the write that arms it, so an
       F18A detection probe reading its result back a few cycles later cannot miss it,
       and paces the rest per scanline. */
    pico9918_gpu_set_clock(PICO9918_INST PICO9918_GPU_IPS_PRO);
}

void vdp_bridge_writedata(unsigned char value)
{
    if (vdp_bridge_ensure()) {
        pico9918_write_data(PICO9918_INST value);
    }
}

void vdp_bridge_writectrl(unsigned char value)
{
    if (!vdp_bridge_ensure()) {
        return;
    }

    pico9918_write_addr(PICO9918_INST value);
}

unsigned char vdp_bridge_readdata(void)
{
    return vdp_bridge_ensure() ? pico9918_read_data(PICO9918_INST_ONLY) : 0u;
}

unsigned char vdp_bridge_readctrl(void)
{
    return vdp_bridge_ensure() ? pico9918_read_status(PICO9918_INST_ONLY) : 0u;
}

int vdp_bridge_irq_level(void)
{
    if (!vdp_bridge_ensure()) {
        return 0;
    }
    return pico9918_interrupt_status(PICO9918_INST_ONLY) ? 1 : 0;
}


/* One virtual (VGA) line: render it, and write it out vPixelScale times. */
static void vdp_bridge_virtual_line(void)
{
    if (g_line < g_params.vVirtualPixels) {
        /* Through the frame module, not the bare pico9918_scan_line(): SR3, the GPU
           trigger, the palette LUT, the overlays and the CRT dim all live in there. It
           writes all 640 pixels on every line it handles - border lines wholesale,
           active lines as left border, picture and right border - so there is nothing
           to seed, and it renders once per virtual line however many output rows share
           it. */
        const unsigned int scale = g_display.vPixelScale ? g_display.vPixelScale : 1u;
        for (unsigned int rep = 0; rep < scale; ++rep) {
            const unsigned int dy = g_line * scale + rep;
            if (dy >= VDP_V_OUTPUT) {
                continue;
            }

            /* False means the buffer is untouched, so the conversion still stands. */
            if (pico9918_frame_output_line(PICO9918_INST dy, &g_params, g_pixels)) {
                vdp_bridge_convert_line();
            }

            vb_present_scanline_size((int)dy, g_argb,
                                     (int)VDP_H_VIRTUAL, (int)VDP_V_OUTPUT);
        }
    }

    /* First line past the display: this frame's end-of-frame interrupt. */
    if (g_line == g_geometry.triggerScanline) {
        pico9918_frame_end_of_scanline(PICO9918_INST_ONLY);
    }

    /* Start of the vertical porch, once the visible field is complete. */
    if (g_line == g_params.vVirtualPixels) {
        pico9918_frame_porch(PICO9918_INST_ONLY);

        /* A host overlay presents its own lines and its own frame - it is a different
           size from the VGA one, and the last writer sets what the screen shows. */
        if (!(g_overlayPresent && g_overlayPresent())) {
            vb_present_frame();
        }
    }

    if (++g_line >= g_fieldLines) {
        g_line = 0u;

        /* True end of frame: advances the counter, refreshes diagnostics and
           recomputes geometry, which is where a mode change moves vPixelScale - hence
           the cadence re-derive. The temperature is a plausible stand-in. */
        const float frameRateHz = (g_scanlines > 300u) ? 50.0f : 60.0f;
        g_geometry = pico9918_frame_end(PICO9918_INST 40.0f, frameRateHz, &g_display);
        vdp_bridge_recompute_cadence();
    }
}

int vdp_bridge_loop(void)
{
    if (!vdp_bridge_ensure()) {
        return 0;
    }

    /* A personality picked in the GUI lands here, between lines rather than inside one. */
    if (g_chipPending) {
        g_chipPending = 0;
        pico9918_set_chip(PICO9918_INST g_chip);
    }

    for (unsigned int i = 0; i < g_linesPerCall; ++i) {
        vdp_bridge_virtual_line();
    }

    return pico9918_interrupt_status(PICO9918_INST_ONLY) ? 1 : 0;
}

unsigned char vdp_bridge_get_register(unsigned char reg)
{
    if (!vdp_bridge_ensure()) {
        return 0u;
    }
    /* No mask here: pico9918_reg_value() applies the instance's own lockedMask - 0x07
       locked, 0x3F unlocked - so masking to 0x07 first aliased every enhanced register
       onto the first eight, and a viewer asking for R50 was quietly handed R2. */
    return pico9918_reg_value(PICO9918_INST (pico9918_register_t)reg);
}

unsigned char vdp_bridge_peek_vram(unsigned int address)
{
    if (!vdp_bridge_ensure()) {
        return 0u;
    }
    return pico9918_vram_value(PICO9918_INST (uint16_t)address);
}

void vdp_bridge_poke_vram(unsigned int address, unsigned char value)
{
    if (!vdp_bridge_ensure()) {
        return;
    }

    /* The library exposes a VRAM read but no write, so this goes the way a guest would:
       latch the address, then write the byte. It therefore perturbs the address latch
       and any read-ahead the running program is part-way through - unavoidable from out
       here, and the alternative is a debugger memory editor that silently does nothing.
       A pico9918_vram_write() upstream would make this exact. */
    pico9918_write_addr(PICO9918_INST (unsigned char)(address & 0xFFu));
    pico9918_write_addr(PICO9918_INST (unsigned char)(0x40u | ((address >> 8) & 0x3Fu)));
    pico9918_write_data(PICO9918_INST value);
}

