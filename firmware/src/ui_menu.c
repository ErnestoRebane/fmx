/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* SLOOP menu (HOME held), in sections as the pages are (2.4): SCREEN (COLOR, ZOOM), LIGHTS (LIGHTS, KEYS, NOTES),
 * AUDIO (LOWCUT, USB AUDIO, USB SERIAL), SYSTEM (HARDWARE CALIBRATION, 2ND CORE, ABOUT, LAST CRASH). SELECT goes to
 * the section before / after (stopping at the ends), KNOB 1..4 set the section's settings in order (the knob's
 * colour marks its row), PRESETS moves the cursor; OCT+ steps the cursor's setting round or opens it (CALIBRATION,
 * ABOUT, LAST CRASH), OCT- closes (from ABOUT or LAST CRASH: back to the section). */
/* ------------------------------------------------------------ menu --- */
enum { MI_COLOR, MI_ZOOM, MI_LIGHTS, MI_KEYS, MI_NOTES, MI_LOWCUT, MI_USB, MI_SERIAL, MI_PANEL, MI_CPU1, MI_ABOUT, MI_CRASH,
       MI_COUNT };
static const char *const MI_NAME[MI_COUNT] = {"COLOR", "ZOOM", "LIGHTS", "KEYS", "NOTES", "SPEAKER LOWCUT", "USB AUDIO",
                                              "USB SERIAL", "HARDWARE CALIBRATION", "2ND CORE", "ABOUT", "LAST CRASH"};
#ifndef FM1_CRASH_MAGIC                                /* (the host: fm1_irq.h is the target's; no record, the tests make one) */
#define FM1_CRASH_MAGIC 0x43525332u
typedef struct { uint32_t magic, count, vec, pc, rets, emu, dbg, sp, psr, icfg, uptime_ms; uint32_t etm[4]; uint32_t rete, retx; uint32_t early; } fm1_crash_t;
static fm1_crash_t fm1_crash;
#endif
enum { MS_SCREEN, MS_LIGHTS, MS_AUDIO, MS_SYSTEM, MS_COUNT };
static const char *const MS_NAME[MS_COUNT] = {"SCREEN", "LIGHTS", "AUDIO", "SYSTEM"};   /* (AUDIO: and USB) */
static const uint8_t MS_FIRST[MS_COUNT + 1] = {MI_COLOR, MI_LIGHTS, MI_LOWCUT, MI_PANEL, MI_COUNT};   /* rows of each */
static const char *const LIGHTS_NAME[LIGHTS_N] = {"OFF", "LOW", "MID", "HIGH"};   /* every button lit, the labels readable */
static const char *const KEYS_NAME[KEYS_N] = {"OFF", "C KEYS", "WHITE KEYS", "ALL KEYS"};      /* keys lit too, at the LIGHTS level */
#define MI_Y0 26                                   /* the first row, under the section tabs */
#define MI_DY 50                                   /* a row: the label, the value in large type */
#define MI_DY4 38                                  /* a row of a section with four (SYSTEM): the value in small type */

static uint32_t mi_sec(uint32_t i)                 /* the section of item i */
{
    uint32_t k = 0;
    while (k + 1u < MS_COUNT && i >= MS_FIRST[k + 1u])
        k++;
    return k;
}
static uint32_t mi_row(uint32_t i) { return i - MS_FIRST[mi_sec(i)]; }   /* its row: KNOB 1 + row sets it */

/* item i's value as shown, and its colour */
static const char *mi_value(uint32_t i, uint16_t *c)
{
    *c = C_HI;
    switch (i) {
    case MI_COLOR: return PALETTES[settings.palette].name;
    case MI_ZOOM: return settings.zoom ? "ON" : "OFF";
    case MI_LIGHTS: return LIGHTS_NAME[lights_lvl % LIGHTS_N];
    case MI_KEYS:
        *c = lights_lvl ? C_HI : C_DIM;               /* (needs LIGHTS) */
        return KEYS_NAME[lights_keys % KEYS_N];
    case MI_NOTES: return lights_notes ? "ON" : "OFF";
    case MI_LOWCUT: return settings.lowcut ? "ON" : "OFF";
    case MI_USB: return usb_full ? "FULL" : "MASTER";
    case MI_SERIAL: return usb_serial ? "ON" : "OFF";
    case MI_CPU1: return cpu1_off ? "OFF" : "ON";
    default:
        *c = C_DIM;
        return "";                                    /* (an action: OCT+ opens it) */
    }
}

static void draw_menu(void)
{
    uint32_t i, pass, sec = mi_sec(ui.menu_sel % MI_COUNT);
    uint32_t sig = ui.menu * 7u + ui.menu_sel * 131u + settings.palette * 1009u + settings.lowcut * 7919u +
                   settings.zoom * 104729u + lights_lvl * 1299709u + lights_keys * 15485863u +
                   lights_notes * 32452843u + usb_full * 49979687u + usb_serial * 86028121u;
    if (!ui.force && sig == ui.menu_sig)
        return;
    ui.menu_sig = sig;
    if (ui.force)                                   /* head + rule + two bands cover rows 0..229 */
        lcd_fill(0, H_HEAD + 1 + 124 + 95, 240, 240 - (H_HEAD + 1 + 124 + 95), C_BLACK);
    cv_begin(240, H_HEAD, C_BLACK);
    cv_text(4, 1, &FONT_S, ui.menu == 3 ? "LAST CRASH" : ui.menu == 2 ? "ABOUT" : "MENU", C_HI);
    if (ui.menu >= 2)                              /* ABOUT, LAST CRASH: the way back */
        cv_text(236 - text_w(&FONT_S, "OCT-: BACK"), 1, &FONT_S, "OCT-: BACK", C_AMB);
    else {                                         /* the section and its number, as a page's "ENV DEST 2/2" */
        char t[16];
        str_cpy(t, MS_NAME[sec], sizeof t);
        str_cpy(t + str_len(t), " ", 2);
        fmt_int(t + str_len(t), (int32_t)sec + 1);
        str_cpy(t + str_len(t), "/", 2);
        fmt_int(t + str_len(t), MS_COUNT);
        cv_text(236 - text_w(&FONT_S, t), 1, &FONT_S, t, C_GRAY);
    }
    cv_blit(0, Y_HEAD);
    lcd_fill(0, H_HEAD, 240, 1, C_LINE);
    for (pass = 0; pass < 2u; pass++) {             /* the canvas holds 124 rows: draw in two bands */
        cv_begin(240, pass ? 95u : 124u, C_BLACK);
        cv_oy = pass ? -124 : 0;
        if (ui.menu == 3) {                         /* the record the last crash left (fm1_irq.h fm1_fault_c), kept over the reboot */
            static const char HX[] = "0123456789ABCDEF";
            static const char *const LN[8] = {"VEC ", "RETE", "RETS", "DBG ", "SP  ", "RETI", "RETX", "EMU "};
            const fm1_crash_t *c = &fm1_crash;
            char b[24];
            uint32_t q, j;
            if (c->magic != FM1_CRASH_MAGIC) {
                cv_text(4, 4, &FONT_S, "NO CRASH RECORDED", C_WHITE);
                cv_text(4, 22, &FONT_S, "(KEPT UNTIL THE POWER GOES)", C_GRAY);
            } else {
                uint32_t v[8] = {c->vec, c->rete, c->rets, c->dbg, c->sp, c->pc, c->retx, c->emu};
                str_cpy(b, "CRASH ", 7);
                fmt_int(b + 6, (int32_t)c->count);
                str_cpy(b + str_len(b), "  UP ", 6);
                fmt_int(b + str_len(b), (int32_t)(c->uptime_ms / 1000u));
                str_cpy(b + str_len(b), "S", 2);
                cv_text(4, 4, &FONT_S, b, C_WHITE);
                for (q = 0; q < 8u; q++) {          /* VEC, RETE, RETS, DBG, SP, RETI, RETX, EMU */
                    char h[9];
                    int32_t y = 22 + (int32_t)q * 14;
                    for (j = 0; j < 8u; j++)
                        h[j] = HX[(v[q] >> (28u - 4u * j)) & 15u];
                    h[8] = 0;
                    cv_text(4, y, &FONT_S, LN[q], C_GRAY);
                    cv_text(48, y, &FONT_S, h, q <= 1u ? C_WHITE : C_HI);
                }
                cv_text(130, 22, &FONT_S, c->vec == 1u ? "EXCEPTION" : "IRQ VEC", C_DIM);
                cv_text(130, 36, &FONT_S, "STOPPED AT", C_DIM);
                cv_text(130, 50, &FONT_S, "CALLED FROM", C_DIM);
                cv_text(130, 64, &FONT_S, (c->dbg & 0x10u) ? "AXI READ BAD" : (c->dbg & 0x20u) ? "AXI WRITE BAD" :
                                          (c->dbg & 0x3F0000u) ? "A CORE'S BUS BAD" : "", C_AMB);
                for (q = 0; q < 4u; q++) {          /* the last branches (ETM) */
                    char h[9];
                    for (j = 0; j < 8u; j++)
                        h[j] = HX[(c->etm[q] >> (28u - 4u * j)) & 15u];
                    h[8] = 0;
                    cv_text(4 + 118 * (int32_t)(q & 1u), 138 + 14 * (int32_t)(q >> 1), &FONT_S, h, C_DIM);
                }
                cv_text(4, 170, &FONT_S, "BUILD " __DATE__ " " __TIME__, C_DIM);
            }
            cv_text(4, 188, &FONT_S, "OCT-: BACK", C_HI);
        } else if (ui.menu == 2) {
            cv_text(4, 4, &FONT_L, "FMX", C_WHITE);
            cv_text(70, 18, &FONT_S, "BREAKCORE / JUNGLE", C_AMB);
            cv_text(4, 40, &FONT_S, FELUCCA_VERSION, C_HI);
            cv_text(236 - text_w(&FONT_S, __DATE__ " " __TIME__), 40, &FONT_S, __DATE__ " " __TIME__, C_GRAY);   /* the build's date
                                                                                                          * and time: which one runs */
            cv_text(4, 58, &FONT_S, "BASED ON SLOOP (ISOD89)", C_HI);
            cv_text(4, 72, &FONT_S, "AND FELUCCA: LEO KUROSHITA", C_HI);
            cv_text(4, 86, &FONT_S, "H\xDCGELTON INSTRUMENTS", C_AMB);   /* Latin-1 U-umlaut */
            cv_text(4, 104, &FONT_S, "BREAK: X0X, CHARLES VESTAL", C_AMB);
            cv_text(4, 118, &FONT_S, "2 CORES: A. REZEMINI, MELODEE", C_AMB);
            cv_text(4, 134, &FONT_S, "GPL-3.0, NO WARRANTY", C_HI);
            cv_text(236 - text_w(&FONT_S, cpu1_label()), 134, &FONT_S, cpu1_label(), C_AMB);   /* 2 CORES / 1 CORE */
            cv_text(4, 150, &FONT_S, "FONT: TERMINUS (OFL)", C_GRAY);
            cv_text(4, 163, &FONT_S, "SAMPLES: VERSILIAN (CC0)", C_GRAY);
            cv_text(4, 176, &FONT_S, "+ SONIC PI (CC0)", C_GRAY);
            cv_text(4, 189, &FONT_S, "VOICES: LEMONJOLLY (CC BY)", C_GRAY);
            cv_text(4, 202, &FONT_S, "BREAKS: OASYNTAX (CC BY)", C_GRAY);
        } else {
            int32_t x = 4;
            for (i = 0; i < MS_COUNT; i++) {        /* the sections as tabs: this one lit */
                int32_t w = text_w(&FONT_S, MS_NAME[i]);
                if (i == sec)
                    cv_rect(x - 2, 3, w + 4, 18, C_LINE);
                cv_text(x, 4, &FONT_S, MS_NAME[i], i == sec ? C_WHITE : C_GRAY);
                x += w + 10;
            }
            for (i = MS_FIRST[sec]; i < MS_FIRST[sec + 1u]; i++) {
                uint32_t r = i - MS_FIRST[sec];
                int32_t dy = MS_FIRST[sec + 1u] - MS_FIRST[sec] > 3u ? MI_DY4 : MI_DY, y = MI_Y0 + (int32_t)r * dy, xv;
                int cur = i == ui.menu_sel;
                uint16_t vc;
                const char *v = mi_value(i, &vc);
                char k[4] = {'K', (char)('1' + r), 0, 0};
                cv_rect(4, y + 2, 4, dy - 8, cur ? C_WHITE : TE_COL[r & 3u]);   /* the knob's colour */
                xv = cv_text(14, y, &FONT_S, k, TE_COL[r & 3u]);
                cv_text(xv + 6, y, &FONT_S, MI_NAME[i], cur ? C_WHITE : C_GRAY);
                if (dy == MI_DY4) {                     /* four rows (SYSTEM): the value in small type under the name */
                    xv = cv_text(14, y + 16, &FONT_S, v[0] ? v : "OCT+ OPENS", v[0] ? (cur && vc == C_HI ? C_WHITE : vc) :
                                                                                 cur ? C_WHITE : C_GRAY);
                } else if (v[0] && text_w(&FONT_L, v) <= 222) {
                    xv = cv_text(14, y + 16, &FONT_L, v, cur && vc == C_HI ? C_WHITE : vc);
                } else if (v[0]) {                      /* (too wide for the large type: WHITE KEYS) */
                    xv = cv_text(14, y + 24, &FONT_S, v, cur && vc == C_HI ? C_WHITE : vc);
                } else {
                    xv = cv_text(14, y + 24, &FONT_S, "OCT+ OPENS", cur ? C_WHITE : C_GRAY);
                }
                if (i == MI_COLOR) {                    /* the palette's colours */
                    uint32_t q;
                    for (q = 0; q < 5u; q++)
                        cv_rect(xv + 10 + (int32_t)q * 14, y + 28, 10, 10, pal[q]);
                }
                if (i == MI_SERIAL && usb_serial != usb_cdc_now())
                    cv_text(xv + 8, y + 30, &FONT_S, "RESTART", C_AMB);   /* (usb.c: at the next start) */
            }
            {
                int32_t hy = MI_Y0 + (MS_FIRST[sec + 1u] - MS_FIRST[sec] > 3u ? 4 * MI_DY4 : 3 * MI_DY);
                cv_text(4, hy + 6, &FONT_S, "SELECT SECTION  KNOB SETS", C_AMB);
                cv_text(4, hy + 22, &FONT_S, "OCT+ OK   OCT- CLOSE", C_AMB);   /* (rows .. 218) */
            }
        }
        cv_oy = 0;
        cv_blit(0, H_HEAD + 1 + pass * 124u);
    }
}

static void enc_drop(void)                             /* knob turns nobody takes */
{
    uint32_t k;
    for (k = 0; k < NE; k++)
        panel_enc(k);
}

static void menu_close(void)
{
    if (song.playing || transport_req)
        settings_later = 1;                            /* (a flash write stops the audio: once stopped) */
    else
        settings_save();                               /* palette / panel table, if changed */
    ui.menu = 0;                                       /* back to the screen it was opened from (the menu is over it) */
    live_shown = 0;
    lcd_fill(0, 0, 240, 240, C_BLACK);
    ui.force = 1;
}

/* item i: a knob turned s (> 0 right, < 0 left), or 0: OCT+ (steps round, toggles, or opens) */
static void mi_set(uint32_t i, int32_t s)
{
    switch (i) {
    case MI_COLOR:                                     /* both ways round; OCT+ the next */
        settings.palette = (settings.palette + (s < 0 ? NPALETTES - 1u : 1u)) % NPALETTES;
        palette_set(settings.palette);
        break;
    case MI_ZOOM:
    case MI_LOWCUT: {                                  /* right ON, left OFF; OCT+ toggles */
        uint32_t *v = i == MI_LOWCUT ? &settings.lowcut : &settings.zoom;
        *v = s > 0 ? 1u : s < 0 ? 0u : !*v;
        fx_lowcut = (uint8_t)(settings.lowcut != 0);
        break;
    }
    case MI_USB:                                       /* right FULL, left MASTER */
        usb_full = (uint8_t)(s > 0 ? 1u : s < 0 ? 0u : !usb_full);
        break;
    case MI_SERIAL:
        usb_serial = (uint8_t)(s > 0 ? 1u : s < 0 ? 0u : !usb_serial);
        break;
    case MI_NOTES:
        lights_notes = (uint8_t)(s > 0 ? 1u : s < 0 ? 0u : !lights_notes);
        break;
    case MI_LIGHTS:
    case MI_KEYS: {                                    /* brighter / dimmer (stops at the ends); OCT+ steps round */
        uint8_t *v = i == MI_LIGHTS ? &lights_lvl : &lights_keys;
        uint32_t n = i == MI_LIGHTS ? LIGHTS_N : KEYS_N;
        if (s > 0 && *v + 1u < n)
            (*v)++;
        else if (s < 0 && *v > 0u)
            (*v)--;
        else if (!s)
            *v = (uint8_t)((*v + 1u) % n);
        if (i == MI_KEYS && lights_keys && !lights_lvl)
            lights_lvl = LIGHTS_LOW;                   /* keys lit need a level: the lowest */
        break;
    }
    case MI_CPU1:                                      /* right ON, left OFF; at once (the next block renders so) */
        cpu1_off = (uint8_t)(s > 0 ? 0u : s < 0 ? 1u : !cpu1_off);
        cpu1_units = cpu1_off ? 0 : -1;
        if (!cpu1_off && !cpu1_on())                   /* (as SLOOP 2.7: core 1 starts only at power-on) */
            ui_message("ON AT NEXT POWER-ON");
        break;
    case MI_PANEL:                                     /* actions: OCT+ only */
        if (!s) {
            panel_setup();
            ui.force = 1;
        }
        break;
    case MI_ABOUT:
        if (!s) {
            ui.menu = 2;
            ui.force = 1;
        }
        break;
    case MI_CRASH:                                     /* the last crash's record: OCT+ only */
        if (!s) {
            ui.menu = 3;
            ui.force = 1;
        }
        break;
    default:
        break;
    }
}

/* menu: SELECT the section, KNOB 1..3 its settings, PRESETS the cursor, OCT+ ok, OCT- close (ABOUT -> the section) */
static void menu_input(uint32_t pressed)
{
    int32_t s;
    uint32_t k, ok = (pressed >> panel.btn[B_OCTUP]) & 1u, back = (pressed >> panel.btn[B_OCTDN]) & 1u;
    uint32_t sel = ui.menu_sel % MI_COUNT, sec = mi_sec(sel), n = MS_FIRST[sec + 1u] - MS_FIRST[sec];
    if (back) {
        if (ui.menu >= 2)
            ui.menu = 1, ui.force = 1;
        else
            menu_close();
        return;
    }
    if (ui.menu != 1) {
        enc_drop();
        return;
    }
    if ((s = panel_enc(EN_SELECT)) != 0) {             /* the section before / after (stops at the ends) */
        int32_t to = clamp((int32_t)sec + (s > 0 ? 1 : -1), 0, MS_COUNT - 1);
        if ((uint32_t)to != sec) {
            ui.menu_sel = MS_FIRST[to];
            ui.force = 1;
        }
        enc_drop();
        return;
    }
    if ((s = panel_enc(EN_PRESET)) != 0)               /* the cursor, round the section's rows */
        ui.menu_sel = (uint8_t)(MS_FIRST[sec] + (sel - MS_FIRST[sec] + (s > 0 ? 1u : n - 1u)) % n);
    for (k = 0; k < 4u; k++)
        if ((s = panel_enc(EN_K1 + k)) != 0 && k < n) {
            ui.menu_sel = (uint8_t)(MS_FIRST[sec] + k);   /* (the cursor follows the knob turned) */
            mi_set(MS_FIRST[sec] + k, s);
        }
    if (ok)
        mi_set(ui.menu_sel % MI_COUNT, 0);
    enc_drop();                                        /* swallow the rest while the menu is up */
}
