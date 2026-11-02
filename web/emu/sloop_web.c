/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * The design (the device clock driven by the audio the worklet asks for, the exports below, the saved
 * flash handed back before boot) follows X0X's browser build, web/emu/x0x_web.c of charlesvestal/fm1-x0x
 * (GPL-3.0), and Felucca's web/emu/felucca_web.c, adapted to SLOOP's sources. */
/* SLOOP in the browser: the firmware's sources, as the host tests build them (tests/hostsim.c: libc,
 * engines, voices, FX, sequencer; then the UI, main.c, storage and projects, in src/felucca.c's order),
 * compiled to WebAssembly and run in an AudioWorklet against a simulated FM-1:
 *   - the device clock: TIMER4 ticks (24 MHz) and fm1_ms advance with the audio the worklet asks for; the
 *     audio ISR (audio.c fm1_alnk0_irq, the real one) renders each I2S half (HALF_FRAMES) as the DMA would
 *     ask for it, and the main loop (main.c's, below in web_frame) runs every 15 ms, with ui_input on every
 *     millisecond in between as main.c's wait loop calls it;
 *   - the panel: buttons and encoders by their printed labels (panel.c's table maps them to the matrix),
 *     the keys as fm1_in.notes, the LEDs read back from fm1_led / fm1_led_dim / fm1_led_bg through the key matrix;
 *   - the screen: lcd_fill / lcd_blit into a 240 x 240 RGB565 frame (the panel's byte order);
 *   - the flash: a 1 MiB NOR image behind storage.c's hooks (settings, projects, the autosave, user presets,
 *     the user sample slots read through it as XIP); the page keeps its storage sectors and gives them back at boot.
 * Not here: USB (MIDI, audio, the editor, updates), TRS MIDI, the serial console, the battery (full), the
 * crash / UBOOT paths. Nothing of the firmware changes for this file.
 *   web/emu/build.sh  ->  build/emu/{sloop.wasm, worklet.js, index.html} */
#include <stdint.h>

#define FELUCCA_WEB 1
#define FELUCCA_ARRANGER 1
#ifndef FELUCCA_CDC
#define FELUCCA_CDC 0
#endif
#define FELUCCA_FLASH 1
#define FELUCCA_OTA 0
#define FELUCCA_UAC 0
#define FELUCCA_UART 0
#ifndef FELUCCA_VERSION
#define FELUCCA_VERSION "WEB"
#endif

static uint8_t nor[0x100000];            /* the SPI NOR (erased: 0xFF) */
#define SMP_POOL_XIP(off) ((const uint8_t *)nor + (off))   /* eng_sample.c: the sample pool */
#define main hostsim_main
#define FMX_WEB 1                         /* (hostsim.c: the second core's double left out, one core) */
#define HOST_HAS_UI_C 1                   /* (ui.c is included: its preset_fill) */
#include "../../tests/hostsim.c"
#undef main

/* ------------------------------------------------------------ the clock --- */
#define FM1_TICKS_PER_US 24u             /* TIMER4: 24 MHz */
static uint32_t web_ticks;
static uint32_t fm1_ticks(void) { return web_ticks; }

/* ------------------------------------------------------ audio (ALNK0) --- */
static uint32_t web_half;                /* the half the next render fills */
#define FM1_AUDIO_HALF 1u
static uint8_t fm1_audio_pending(void) { return FM1_AUDIO_HALF; }
static void fm1_audio_ack_aux(uint8_t p) { (void)p; }
static uint32_t fm1_audio_free_half(void) { return web_half; }
static void fm1_audio_ack_half(void) {}
static void fm1_audio_init(int32_t *buf, uint32_t words, void (*isr)(void), uint32_t prio)
{ (void)buf; (void)words; (void)isr; (void)prio; }
static void fm1_audio_stop(void) {}
void isr_alnk0(void) {}
#include "../../firmware/src/audio.c"

/* ------------------------------------------------------------ the panel --- */
#define FM1_NCOL 11u
static const int8_t FM1_KEYMAP[6][FM1_NCOL] = {      /* as hal/fm1_input.h: key id at (column, row bit) */
    {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    { 5, 11,  4, 10,  3,  9,  2,  8, -1, -1, -1},
    {34, 35, 36, 37, 38, 40, 39, 13,  7,  6, 12},
    {23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33},
    { 0,  1, 15, 14, 17, 16, 19, 18, 20, 21, 22},
    {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
};
static uint8_t fm1_led[FM1_NCOL], fm1_led_dim[FM1_NCOL], fm1_led_bg[FM1_NCOL];
static volatile uint16_t fm1_led_bg_ns;
static void fm1_led_key(uint32_t id, int on)       /* (hal/fm1_input.h) */
{
    uint32_t p, r;
    for (p = 0; p < FM1_NCOL; p++)
        for (r = 1; r < 5u; r++)
            if (FM1_KEYMAP[r][p] == (int8_t)id) {
                if (on)
                    fm1_led[p] |= (uint8_t)(1u << r);
                else
                    fm1_led[p] &= (uint8_t)~(1u << r);
            }
}
static uint32_t web_pressed, web_released, web_notes_pressed;
static int32_t web_enc_steps[7];
static uint32_t fm1_input_edges(uint32_t *released)
{
    uint32_t p = web_pressed;
    if (released)
        *released = web_released;
    web_pressed = web_released = 0;
    return p;
}
static uint32_t fm1_input_note_edges(void) { uint32_t n = web_notes_pressed; web_notes_pressed = 0; return n; }
static int32_t fm1_enc_take(uint32_t e) { int32_t s = web_enc_steps[e % 7u]; web_enc_steps[e % 7u] = 0; return s; }
static void fm1_input_init(void) {}
static void fm1_input_tick(void) {}
static void fm1_wdt_feed(void) {}

/* ----------------------------------------------------------- the screen --- */
static uint16_t web_fb[240 * 240];       /* RGB565 as the panel gets it (high byte first) */
static uint32_t web_draws;
static void lcd_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c)
{
    uint32_t i, j;
    uint16_t sw = (uint16_t)((c >> 8) | (c << 8));
    for (j = 0; j < h && y + j < 240u; j++)
        for (i = 0; i < w && x + i < 240u; i++)
            web_fb[(y + j) * 240u + x + i] = sw;
    web_draws++;
}
static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *p)
{
    uint32_t i, j;
    for (j = 0; j < h && y + j < 240u; j++)
        for (i = 0; i < w && x + i < 240u; i++)
            web_fb[(y + j) * 240u + x + i] = p[j * w + i];
    web_draws++;
}
static void lcd_sync(void) {}
static void lcd_init(void) {}

#include "../../firmware/src/gfx.c"
#include "../../firmware/src/panel.c"
#include "../../firmware/src/ui.c"
#include "../../firmware/src/psx3d.c"
#define te_dials te_dials_fw                       /* (the knobs' labels and the hint line, kept for web_ui) */
#include "../../firmware/src/ui_studio.c"
#undef te_dials
static char web_lab[4][16], web_hint[48];
static uint32_t web_lab_ms;
static void te_dials(int32_t y0, const char *const lab[4], const char *const val[4], const int32_t ratio[4],
                     uint32_t sig, uint32_t *cache)
{
    const char *hint = te_hint_txt;
    for (uint32_t k = 0; k < 4u; k++)
        str_cpy(web_lab[k], lab[k] ? lab[k] : "", sizeof web_lab[k]);
    str_cpy(web_hint, hint ? hint : "", sizeof web_hint);
    web_lab_ms = fm1_ms;
    te_dials_fw(y0, lab, val, ratio, sig, cache);
}
#include "../../firmware/src/ui_deck.c"
#include "../../firmware/src/ui_song.c"
#include "../../firmware/src/ui_draw.c"
#include "../../firmware/src/ui_vis.c"
#include "../../firmware/src/ui_layers.c"
#include "../../firmware/src/ui_menu.c"
#include "../../firmware/src/ui_input.c"
#include "../../firmware/src/ui_live.c"
#include "../../firmware/src/ui_break.c"
#include "../../firmware/src/ui_chord.c"
#include "../../firmware/src/ui_chop.c"
#include "../../firmware/src/ui_bass.c"

/* ------------------------------------------------------------ the flash --- */
static uint32_t web_flash_writes;
static uint8_t flash_ok = 1;
static int st_read(uint32_t off, void *dst, uint32_t n)
{
    if (off >= sizeof nor || n > sizeof nor - off)
        return -1;
    memcpy(dst, nor + off, n);
    return 0;
}
static int st_erase(uint32_t off)
{
    if (off >= sizeof nor || 4096u > sizeof nor - off)
        return -8;
    memset(nor + (off & ~4095u), 0xFF, 4096);
    web_flash_writes++;
    return 0;
}
static int st_prog(uint32_t off, const void *src, uint32_t n)       /* NOR: a write only clears bits */
{
    const uint8_t *s = src;
    uint32_t i;
    if (off >= sizeof nor || n > sizeof nor - off)
        return -8;
    for (i = 0; i < n; i++)
        nor[off + i] &= s[i];
    web_flash_writes++;
    return 0;
}
static uint32_t irq_save(void) { return 0; }
static void irq_restore(uint32_t f) { (void)f; }
static uint32_t fl_jedec_ram(void) { return 0x856014u; }             /* the FM-1's part (project.c) */
static void fl_plain_window_init(void) {}
#define FL_FAR(fn) (fn)
#include "../../firmware/src/storage.c"
#include "../../firmware/src/upreset.c"
#include "../../firmware/src/project.c"

/* ------------------------------------------------- main.c and its HAL --- */
/* (fm1_crash_t: ui_menu.c's host stand-in, for the LAST CRASH page) */
static struct { uint8_t p3_rst, wdt_con; uint32_t rst_src; } fm1_boot;
uint32_t _data_start[1], _data_end[1], _data_load[1], _bss_start[1], _bss_end[1];
uint32_t _pool_start[1], _pool_end[1], _rt_start[1], _rt_end[1], _rt_load[1];
uint32_t _c1_start[1], _c1_end[1], _c1_load[1], _lr_start[1], _lr_end[1];   /* (the second core's: none here) */
static void fm1_supplies_stock(void) {}
static void cpu1_boot(void) {}                    /* (main.c: the browser has one core; hostsim's double is inline) */
static uint32_t web_master_adc = 700;   /* MASTER, 0..1023 as the ADC reads it */
enum { FM1_ADC_BATT = 3, FM1_ADC_MASTER = 4 };
static int32_t fm1_adc_read(uint32_t ch) { return ch == FM1_ADC_MASTER ? (int32_t)web_master_adc : 620; }
static void fm1_adc_init(void) {}
static void fm1_timer5_ack(void) {}
static void fm1_timer5_start(void (*isr)(void), uint32_t prio) { (void)isr; (void)prio; }
void isr_timer5(void) {}
static void fm1_reboot(void) {}
static void fm1_enter_uboot(void) {}
static void fm1_guard_lock_top(void) {}
static void fm1_irq_enable_all(void) {}
static void fm1_irq_init(void) {}
static void fm1_time_init(void) {}
static void fm1_reset_reason(void) {}
static void fm1_wdt_arm(uint32_t v) { (void)v; }
static void fm1_mailbox_clear(void) {}
static void fm1_guard_enable(uint32_t m) { (void)m; }
enum { FM1_GUARD_STACK = 1, FM1_GUARD_WRITE = 2, FM1_GUARD_BUS = 4, FM1_GUARD_PC = 8 };
/* main.c's calls into the USB driver (registers): nothing here (usb.c's own functions stay as they are) */
#define usb_poll() ((void)0)
#define usb_start() ((void)0)
#define usb_retry(ms) ((void)(ms))
#define usb_detach() ((void)0)
#include "../../firmware/src/splash.c"
#include "../../firmware/src/main.c"
#undef usb_poll
#undef usb_start
#undef usb_retry
#undef usb_detach

/* --------------------------------------------------------- the device --- */
static uint32_t web_booted, web_boot_ms, web_last_frame, web_boot_f;
static int32_t web_knob = 512 * 16;

/* main.c fm1_main's boot, up to the main loop (no USB, no UART, no panel setup) */
static void web_power_on(void)
{
    persist_boot();
    settings_init();
    lcd_init();
    boot_splash();                        /* the FMX boot screen (splash.c) */
    memset(&felucca_dbg, 0, sizeof felucca_dbg);
    felucca_dbg.magic = DBG_MAGIC;
    fm1_input_init();
    fm1_adc_init();
    panel_init();
    felucca_init();
    audio_init();
    web_boot_ms = fm1_ms + 930u;          /* main.c: 30 ms, then boot_anim (about 900 ms) */
    web_boot_f = 0;
    web_booted = 1;
}

/* one pass of main.c fm1_main's loop (what the browser has: no USB, editor, UBOOT, console) */
static void web_frame(void)
{
    int32_t b = fm1_adc_read(FM1_ADC_BATT), a = fm1_adc_read(FM1_ADC_MASTER);
    if (b > 0)
        song.batt_raw = song.batt_raw ? song.batt_raw + (b - song.batt_raw) / 32 : b;
    if (a >= 0) {
        uint32_t k10;
        web_knob += (a * 16 - web_knob) / 8;
        k10 = (uint32_t)(web_knob / 16);
        song.master_q12 = (k10 * k10) >> 8;                /* 0 .. ~4096 (main.c) */
    }
    felucca_dbg.ui_frames++;
    felucca_dbg.page = ui.page;
    felucca_dbg.home = ui.home;
    ui_input();
    ui_leds();
    ui_draw();
    autosave_tick();
#if FELUCCA_BREAK
    brkp_poll();
    cg_poll();
    cs_poll();
    as_poll();                                   /* AUTO SONG: the next DROP 2's BASS phrase */
#endif
#if FELUCCA_ARRANGER
    sections_flush();
#endif
}

/* output ring, stereo float, filled by web_ms, drained by web_render */
#define RING 4096u
static float ring[RING * 2];
static uint32_t ring_w, ring_r, web_samples_due;

/* one millisecond of the device: the clock, the audio halves due by now, the main loop */
static void web_ms(void)
{
    fm1_ms++;
    web_ticks += 1000u * FM1_TICKS_PER_US;
    web_samples_due += FS;
    while (web_samples_due >= 1000u * HALF_FRAMES) {      /* an I2S half: the audio ISR renders it */
        uint32_t k;
        const int32_t *o = &abuf[web_half * HALF_WORDS];
        web_samples_due -= 1000u * HALF_FRAMES;
        fm1_alnk0_irq();
        for (k = 0; k < HALF_FRAMES; k++) {
            uint32_t i = (ring_w++ % RING) * 2u;
            ring[i] = (float)o[2u * k] / 8388608.0f;
            ring[i + 1] = (float)o[2u * k + 1u] / 8388608.0f;
        }
        web_half ^= 1u;
    }
    if (!web_booted)
        return;
    if ((int32_t)(fm1_ms - web_boot_ms) < 0) {           /* main.c boot_anim: the gem gathers (a frame every 35 ms) */
        uint32_t f = (930u - (web_boot_ms - fm1_ms)) / 35u;
        if (f >= BOOT_NF)
            f = BOOT_NF - 1u;
        if (f != web_boot_f) {
            web_boot_f = f;
            boot_frame(f);
        }
        return;
    }
    if (web_boot_ms) {                                    /* main.c: the logo goes, the loop starts */
        web_boot_ms = 0;
        lcd_fill(0, 0, 240, 240, C_BLACK);
        web_last_frame = fm1_ms - 15u;
    }
    if (fm1_ms - web_last_frame >= 15u) {                /* main.c: a frame, then ui_input until 15 ms are over */
        web_last_frame = fm1_ms;
        web_frame();
    } else {
        ui_input();                                       /* main.c's wait loop */
    }
}

#define EXPORT __attribute__((used, visibility("default")))
#undef __attribute__

static float out_l[1024], out_r[1024];
EXPORT float *web_out_l(void) { return out_l; }
EXPORT float *web_out_r(void) { return out_r; }
/* n frames (<= 1024) into out_l / out_r: the device runs until they exist */
EXPORT void web_render(uint32_t n)
{
    uint32_t k;
    if (n > 1024u)
        n = 1024u;
    while (ring_w - ring_r < n)
        web_ms();
    for (k = 0; k < n; k++) {
        uint32_t i = (ring_r++ % RING) * 2u;
        out_l[k] = ring[i];
        out_r[k] = ring[i + 1];
    }
}

/* the flash: erased, then the page's saved sectors (web_nor), then boot */
EXPORT uint8_t *web_nor(void) { return nor; }
EXPORT uint32_t web_nor_size(void) { return sizeof nor; }
EXPORT void web_nor_erase(void) { memset(nor, 0xFF, sizeof nor); }
EXPORT uint32_t web_flash_writes_count(void) { return web_flash_writes; }
EXPORT void web_boot(void)
{
    if (!web_booted)
        web_power_on();
}

/* input, by the printed labels: buttons bit b = B_FX .. B_OCTUP (panel.c), keys bit k = key k (F3 .. G5),
 * encoders by role (EN_SELECT .. EN_K4), MASTER 0..1023 */
static uint32_t web_btn_labels, web_key_mask;
EXPORT void web_buttons(uint32_t labels)
{
    uint32_t m = 0, b, old = fm1_in.buttons;
    web_btn_labels = labels;
    for (b = 0; b < NB; b++)
        if ((labels >> b) & 1u)
            m |= 1u << panel.btn[b];
    web_pressed |= m & ~old;
    web_released |= old & ~m;
    fm1_in.buttons = m;
}
EXPORT void web_keys(uint32_t mask)
{
    uint32_t old = fm1_in.notes;
    mask &= (1u << 27) - 1u;
    web_key_mask = mask;
    web_notes_pressed |= mask & ~old;
    fm1_in.notes = mask;
}
EXPORT void web_enc(uint32_t role, int32_t n)
{
    if (role < NE)
        web_enc_steps[panel.enc[role] % 7u] += n * panel.dir[role];
}
EXPORT void web_master(uint32_t v) { web_master_adc = v > 1023u ? 1023u : v; }
/* a channel message from Web MIDI, as a USB-MIDI packet arrives (usb.c): 0 = the ring was full */
EXPORT uint32_t web_midi(uint32_t status, uint32_t d1, uint32_t d2)
{
    if (status < 0x80u || status >= 0xF0u)
        return 1;
    if (mi_w - mi_r >= MQ)
        return 0;
    midi_in_q[mi_w % MQ] = (status >> 4) | status << 8 | (d1 & 127u) << 16 | (d2 & 127u) << 24;
    RING_PUBLISH();
    mi_w++;
    return 1;
}

/* the LEDs: bit b = the button labelled b lit (bit 14: PLAY's green), keys bit k; the glow and the backlight alike */
static uint32_t led_on(const uint8_t *l, uint32_t id)
{
    uint32_t c, r;
    for (c = 0; c < FM1_NCOL; c++)
        for (r = 1; r < 5u; r++)
            if (FM1_KEYMAP[r][c] == (int8_t)id)
                return (l[c] >> r) & 1u;
    return 0;
}
static uint32_t led_buttons(const uint8_t *l)
{
    uint32_t b, m = 0;
    for (b = 0; b < NB; b++)
        m |= led_on(l, panel.btn[b]) << b;
    return m;
}
static uint32_t led_keys(const uint8_t *l)
{
    uint32_t k, m = 0;
    for (k = 0; k < 27u; k++)
        m |= led_on(l, 14u + k) << k;
    return m;
}
EXPORT uint32_t web_lit_buttons(void) { return led_buttons(fm1_led); }
EXPORT uint32_t web_lit_keys(void) { return led_keys(fm1_led); }
EXPORT uint32_t web_dim_buttons(void)             /* the glow, and the backlight (menu LIGHTS) when it is on */
{
    return led_buttons(fm1_led_dim) | (fm1_led_bg_ns ? led_buttons(fm1_led_bg) : 0u);
}
EXPORT uint32_t web_dim_keys(void) { return led_keys(fm1_led_dim) | (fm1_led_bg_ns ? led_keys(fm1_led_bg) : 0u); }
EXPORT uint32_t web_dim_level(void) { return 0; }
EXPORT uint32_t web_breath_buttons(void) { return 0; }   /* (Felucca 1.x: not in SLOOP) */
EXPORT uint32_t web_breath_keys(void) { return 0; }
EXPORT uint32_t web_mid_keys(void) { return 0; }
EXPORT uint8_t *web_anim_levels(void) { return 0; }

#if FELUCCA_BREAK
/* for the test and the bench: the BREAK track selected and its screen open, a pattern of 16ths with a pin, the
 * generator's CMPLX 50 (PLAY is the page's to press) */
EXPORT void web_test_break(void)
{
    uint32_t i;
    track_select(TRK_BRK);
    for (i = 0; i < NSTEP; i++) {
        memset(&TBRK->bstep[i], 0, sizeof TBRK->bstep[i]);
        TBRK->bstep[i].on = (uint8_t)((0xB6DBu >> (i % 16u)) & 1u);
    }
    TBRK->bstep[4].slice = 6;
    BPV(BP_CMPLX) = 50;
    TBRK->p[P_MUTE] = 0;
    studio_open(SC_BREAK);
    ui.force = 1;
}
#endif

/* the page wrote an entry of the sample pool into the flash image (web_nor; k: its id, an old slot's place for 0..3):
 * the firmware reads the pool again, as after an upload (smp_pool_scan), and the page keeps the changed sectors */
EXPORT void web_slot_changed(uint32_t k)
{
    if (k < SMP_POOL_N) {
        smp_pool_scan();
        web_flash_writes++;
    }
}

/* the page sent a SLICE loop to the pool's entry k (web/slice.js): the CHOP track plays it in SLICE (as PRESETS, then
 * SEL to SLICE), STYLE ENDS */
EXPORT uint32_t web_slice_use(uint32_t k)
{
    track_t *t = &trk[CS_TRK];
    if (k >= SMP_POOL_N || !usr_nz[k])
        return 0;
    if (ENGINES[t->eng_req % NENGINES] != &ENG_SAMPLE) {
        uint32_t pi;
        for (pi = 0; pi < ENG_SAMPLE.npresets && !str_eq(ENG_SAMPLE.presets[pi].name, "CHOP KIT"); pi++)
            ;
        set_engine_of(t, ENGI_SAMPLE);
        apply_preset_to(t, pi < ENG_SAMPLE.npresets ? pi : 0u);
    }
    t->p[P_E0] = (int16_t)(SMP_NSETS + k);
    t->p[P_E1] = 0;
    t->p[P_VOICE] = V_MONO;
    t->p[P_REL] = 120;
    CSV(0) = CM_SLICE;
    CSV(1) = CY_ENDS;                                   /* (the loop straight, moving in its own bar before the break's
                                                         * fill) */
    CSV(2) = CS_STY[CY_ENDS][0];
    CSV(3) = 50;
    steps_clear(t);
    ui.force = 1;
    return 1;
}

/* the demo set (FMX LIVE): four songs in the four project slots, each its own tempo, key, loops, BREAK style and
 * sounds (the PIANO pack in USR1, as the page puts it at the first switch-on; no kit pack: the CHOP stays empty);
 * then song 1 loaded. Stopped only (a save writes the flash). 1 = done */
static const struct {
    uint8_t bpm, root, loop[4], style, cmplx, mangle;
    uint8_t bass, rhythm;                               /* ANALOG preset, BR_* */
    uint8_t pad, num, color, mood;                      /* SAMPLE preset, the CHORDS generator */
    uint8_t chop, space;                                /* user slot of the CHOP kit, SPACE */
    uint16_t var;                                       /* the seeds */
    uint16_t chops;                                     /* the CHOP's hits, as a player would put them (bit per 16th) */
} DEMO[4] = {
    {172, 5, {0, 1, 2, 4}, BS_JUNGLE, 55, 10, 22, BR_KICKS, 0, 4, CC_7TH, 20, 0, 64, 3, 0x8204},     /* F minor: AMEN, a pad */
    {165, 9, {4, 5, 6, 7}, BS_ROLLER, 40, 5, 24, BR_ROLLER, 2, 3, CC_ADD9, 60, 1, 95, 17, 0x2210},  /* A minor: glass, voices */
    {174, 0, {2, 3, 8, 12}, BS_BRKCORE, 70, 45, 23, BR_STEPPER, 4, 2, CC_TRIAD, 85, 0, 50, 41, 0x9002},   /* C minor: dark */
    {168, 3, {10, 11, 9, 13}, BS_HALFTIME, 35, 0, 25, BR_DUB, 6, 4, CC_LUSH, 40, 1, 110, 77, 0x0201},   /* D# minor: dusty keys */
};
EXPORT uint32_t web_demo_set(void)
{
    uint32_t s, k;
    if (song.playing) {                                 /* (playing: stopped first, as STOP would) */
        transport_req = 2;
        web_render(256);
        if (song.playing)
            return 0;
    }
    for (s = 0; s < 4u; s++) {
        track_t *ch = &trk[CS_TRK];
        song.g[G_BPM] = DEMO[s].bpm;
        song.g[G_SPACE] = DEMO[s].space;
        song.g[G_FILT] = 0;
        set_engine_of(&trk[0], ENGI_ANALOG);
        apply_preset_to(&trk[0], DEMO[s].bass);
        set_engine_of(&trk[1], ENGI_SAMPLE);
        apply_preset_to(&trk[1], DEMO[s].pad);
        set_engine_of(ch, ENGI_SAMPLE);
        apply_preset_to(ch, 8);                         /* CHOP KIT: the first kit in the pool (none: the CHOP stays empty) */
        for (k = 0; k < NPART; k++) {
            trk[k].p[P_ROOT] = DEMO[s].root;
            trk[k].p[P_SCALE] = 2;                      /* (minor) */
            trk[k].engine = trk[k].eng_req;
            trk[k].p[P_MUTE] = 0;
        }
        brk_defaults(TBRK);
        brk_style(TBRK, DEMO[s].style);
        for (k = 0; k < 4u; k++)
            BPV(BP_LOOPA + k) = DEMO[s].loop[k];
        BPV(BP_CMPLX) = DEMO[s].cmplx;
        BPV(BP_MANGLE) = DEMO[s].mangle;
        cg_defaults();
        cg.var = DEMO[s].var;
        cg.num = DEMO[s].num;
        cg.color = DEMO[s].color;
        cg.mood = DEMO[s].mood;
        cg.play = CP_HOLD;
        cg_poll();
        bl_defaults();
        bl.var = DEMO[s].var;
        bl.rhythm = DEMO[s].rhythm;
        bl.mix = 0x81u;                                 /* (the steps the generator's) */
        bl_write();
        ch->p[P_X0] = CM_PADS;                          /* the CHOP: a few hits of its kit off the snares, by hand */
        steps_clear(ch);
        ch->p[P_SLEN] = 32;
        ch->p[P_SDIV] = 2;
        {
            uint32_t base, nh, i;
            if (smp_kit((uint32_t)ch->p[P_E0] % SMP_NALL, &base, &nh))
                for (i = 0; i < 32u; i++)
                    if ((DEMO[s].chops >> (15u - i % 16u)) & 1u) {
                        step_t *st = &ch->step[i];
                        st->note[0] = (uint8_t)(base + (i / 16u * 3u + s + (i % 16u) / 5u) % nh);
                        st->n = 1;
                        st->time = ST_NOTE;
                        st->vel = 100;
                    }
        }
        ch->seq_active = 1;
        project_save(s);
    }
    project_load(0);
    live_song = 0;                                      /* (LIVE: song 1 plays; NEXT from song 2) */
    live_next = -1;
    return 1;
}

#if FELUCCA_BREAK
EXPORT uint32_t web_break_loops(void) { return brkp_nloops(); }          /* for the test: the loops the track has */
EXPORT uint32_t web_break_builtin(void) { return BP_NB; }                 /* ... the built-in ones (user loops: BP_NB + k) */
EXPORT void web_break_loop(uint32_t a, uint32_t b)                        /* ... LOOP A and LOOP B set */
{
    BPV(BP_LOOPA) = (int16_t)a;
    BPV(BP_LOOPB) = (int16_t)b;
}
EXPORT uint32_t web_break_ready(uint32_t bank) { return brkp_bank_src(bank) != 0; }
EXPORT uint32_t web_break_trigs(void) { return bp.st_trigs; }            /* ... the generator's triggers so far */
EXPORT uint32_t web_break_steps(void)                                     /* ... the first 16 steps that play, bit per step */
{
    uint32_t i, m = 0;
    for (i = 0; i < 16u; i++)
        m |= (uint32_t)(TBRK->bstep[i].on != 0u) << i;
    return m;
}
EXPORT int32_t web_break_param(uint32_t i) { return i < BP_NP ? BPV(i) : 0; }   /* ... a generator parameter (brkpart.c BP_*) */
#endif

EXPORT uint32_t web_test_user_preset(uint32_t i) { return (uint32_t)up_used(i); }

EXPORT uint16_t *web_screen(void) { return web_fb; }
EXPORT uint32_t web_screen_draws(void) { return web_draws; }
EXPORT uint32_t web_now_ms(void) { return fm1_ms; }
EXPORT uint32_t web_playing(void) { return song.playing; }
EXPORT uint32_t web_sample_rate(void) { return FS; }

/* what the panel does now, for the page's labels (web/app_emu.js CONTROL_HELP): the screen as numbers, then the four
 * knobs' labels and the hint line the screen drew last (the last number: drawn just now, else they are another
 * screen's: the menu draws none), a line each */
static char web_ui_txt[256];
EXPORT char *web_ui(void)
{
    char *b = web_ui_txt;
    const page_t *pg = cur_page();
    uint32_t n = 0, k;
    const int32_t v[] = {ui.menu, live_on, live_on ? live_fx_held() : ui.layer == LY_FX, vis_on, live_on ? 0 : ui.layer,
                         ui.home, pg->scope, pg->fam, song.sel, is_brk(TSEL), cp_view, cp_page, sg_page, bs_page,
                         ch_page, bk_page, rec_wait || ci_on || ft_on, song.playing, live_auto, q_mode,
                         fm1_ms - web_lab_ms < 250u};
    for (k = 0; k < sizeof v / sizeof v[0]; k++) {
        if (k)
            b[n++] = ' ';
        fmt_int(b + n, v[k]);
        n += str_len(b + n);
    }
    b[n++] = '\n';
    str_cpy(b + n, pg->title ? pg->title : "", 24);
    n += str_len(b + n);
    for (k = 0; k < 5u; k++) {
        b[n++] = '\n';
        str_cpy(b + n, k < 4u ? web_lab[k] : web_hint, sizeof web_ui_txt - n);
        n += str_len(b + n);
    }
    b[n] = 0;
    return b;
}
