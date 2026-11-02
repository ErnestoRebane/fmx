/* SPDX-License-Identifier: GPL-3.0-only */
/* The CHOP screen (FMX): track 3, 240 x 240. Under the header the three MODEs as tabs: OCT- / OCT+ walk them (the one
 * playing goes on), SEL plays the one seen (2026-10-30: no cut while looking), HOME leaves. A tab not playing shows its
 * sound to come and its own settings (nothing of the sound playing changes). Then the page (SELECT: the mode's own,
 * STEPS, TIME), the line of what plays, four dials and a hint.
 *   PADS     the kit as 16 pads, lit as they sound: KIT, MODE (ONE: a hit plays out; GATE: it stops when let go), CHOKE
 *            (a hit cuts the one before), TUNE.
 *   SLICE    the loop's slices (the one sounding yellow, where the loop is pink) and under them this bar's triggers:
 *            the slice each played (a moved one bright), and whose bar it is (the break's fill: the loop plays
 *            straight; the loop's own: it moves, may roll): STYLE (STRAIGHT, HOLD, SKIP, ENDS, HALF, CHOP: sets CMPLX
 *            too), CMPLX, RETRIG, TUNE (OFF, KEY, SNAP, FOLLOW).
 *   INST     the sound, the key the white keys walk, its theme as a roll of 8 bars (the 16th playing marked): SOUND,
 *            MELODY (OFF, MIDI, ARP, SING), THEME (another of this song's; MIDI: which), CHOKE; black key 4 NEW (the next THEME).
 *   STEPS    what REC recorded, a row 16 steps, each hit with its name (the pad's number, the slice's letter, the
 *            note's), held on dimmer: LENGTH (1, 2, 4 bars: what REC records into), RATE (this mode's), SWING.
 *   TIME     each MODE's RATE apart (1/32, 1/16, 1/8, 1/4: the steps' grid; SLICE the loop's speed, INST the theme's)
 *            and INST's OCTAVE (the theme's, -2..+2).
 *   white keys  play: PADS a hit each, SLICE a slice each (held: on every trigger), INST the session's scale from C4
 *               (the keys' octave, OCT on the other screens: here OCT walks the tabs); REC records them (REC held:
 *               the track cleared)
 *   black keys  held: 1 STUTTER, 2 GAP, 3 DOWN; INST: 4 NEW (the next THEME)
 *   PRESETS  the mode's sounds only: the kits (PADS), the loops and kits (SLICE), the instruments (INST); with Q on
 *            the bar. HOME or ALGORITHM goes back; PLAY the transport. A change while the song plays is kept by the next
 *            section and by STOP (song_keep_now). */
#define CP_COL RGB(255, 120, 170)                       /* pink: the CHOP screen */
#define CP_DIM RGB(80, 30, 52)
#define CP_YEL RGB(255, 226, 122)                       /* the slice sounding */
enum { CPG_MAIN, CPG_STEPS, CPG_TIME, CPG_N };
static const uint16_t CP_SND[6] = {RGB(255, 120, 170), RGB(255, 226, 122), RGB(64, 220, 200), RGB(110, 160, 255),
                                   RGB(255, 150, 70), RGB(150, 230, 110)};   /* the sounds of the steps, in order */
static uint8_t cp_page;
static uint8_t cp_view;                                 /* the tab seen (OCT- / OCT+); SEL makes it the one playing */
static uint8_t cp_mem[2] = {0xFFu, 0xFFu};              /* PADS's kit, SLICE's loop (SET): what each plays when chosen */
static uint8_t cp_inst = 0;                             /* INST's sound: its place in the mode's list */
static uint32_t cp_hits, cp_hit_ms;                     /* the last hit seen, when */

static int on_chop_page(void) { return !ui.home && cur_page()->scope == SC_CHOP && song.sel == 2u; }

/* the kit the track plays: its first key and its hits; 0 = the sound is not a kit */
static int cp_kit(uint32_t *base, uint32_t *n) { return cs_kit(base, n); }

/* a 3 x 5 font for the steps' names (0-9, A-P), its rows top down, 3 bits each */
static const uint16_t CP_TINY[26] = {
    075557, 026227, 071747, 071717, 055711, 074717, 074757, 071222, 075757, 075717,   /* 0-9 */
    025755, 065656, 034443, 065556, 074647, 074644, 034553, 055755, 072227, 011152,   /* A-J */
    055655, 044447, 057755, 065555, 025552, 065644,                                    /* K-P */
};
static void cp_tiny(int32_t x, int32_t y, const char *s, uint16_t c, int32_t sc)
{
    for (; *s; s++) {
        uint32_t g = *s >= '0' && *s <= '9' ? (uint32_t)(*s - '0') : *s >= 'A' && *s <= 'P' ? (uint32_t)(*s - 'A') + 10u : 99u;
        int32_t r, b;
        if (g < 26u)
            for (r = 0; r < 5; r++)
                for (b = 0; b < 3; b++)
                    if ((CP_TINY[g] >> ((4 - r) * 3 + (2 - b))) & 1u)
                        cv_rect(x + b * sc, y + r * sc, sc, sc, c);
        x += 4 * sc;
    }
}
/* a note's name as the screen says it: PADS the pad's number, SLICE the slice's letter, INST the note (A3) */
static void cp_name(char *b, uint32_t note, uint32_t mode, uint32_t base)
{
    if (mode == CM_INST) {
        str_cpy(b, N_NOTE[note % 12u], 4);
        fmt_int(b + str_len(b), (int32_t)note / 12 - 1);
    } else if (mode == CM_SLICE) {
        b[0] = (char)('A' + (note - base) % 16u);
        b[1] = 0;
    } else {
        fmt_int(b, (int32_t)(note - base) + 1);
    }
}
static int cp_hit_now(void) { return song.playing && (uint32_t)(fm1_ms - cp_hit_ms) < 300u; }

static void chop_tabs_draw(uint32_t *cache)
{
    uint32_t m = cs_mode(), i, blink = cp_view != m && ((fm1_ms / 300u) & 1u), sig = 0x51u + m * 7u + cp_view * 131u + blink;
    if (!ui.force && sig == *cache)
        return;
    *cache = sig;
    cv_begin(240, 22, C_BLACK);
    ps_ground(40);
    for (i = 0; i < CM_N; i++) {                        /* the one playing lit; the one seen (not playing) framed */
        int32_t x = 4 + (int32_t)i * 78;
        ps_win(x, 1, 74, 20, i == m || i == cp_view ? CP_COL : PS_LINE);
        if (i == m)
            cv_rect(x + 2, 3, 70, 16, tile_glow(CP_COL));
        te_text_c(x + 37, 3, N_CS_MODE[i], i == m ? C_WHITE : i == cp_view ? (blink ? C_WHITE : CP_COL) : PS_LBL);
    }
    cv_blit(0, 40);
}

/* STEPS: what was recorded, a row 16 */
static void chop_steps_draw(uint32_t *cache)
{
    const track_t *t = &trk[CS_TRK];
    uint32_t len = trk_len(t) > CS_STEPS ? CS_STEPS : trk_len(t), now = song.playing ? t->seq_idx : 0xFFFFu;
    uint32_t mode = cs_mode(), base = 0, n = 0, i, nw = 0;
    uint32_t sig = steps_hash(t) * 31u + now * 7919u + mode * 3u + len;
    uint8_t words[6];
    uint16_t col = CP_COL;
    char b[6];
    cs_kit(&base, &n);
    if (!ui.force && sig == *cache)
        return;
    *cache = sig;
    cv_begin(240, 88, C_BLACK);
    ps_ground(62);
    for (i = 0; i < len; i++) {
        const step_t *s = &t->step[i];
        int32_t x = 8 + (int32_t)(i % 16u) * 14 + (int32_t)(i % 16u / 4u), y = 4 + (int32_t)(i / 16u) * 21;
        if (s->time == ST_NOTE && s->n) {
            uint32_t k;
            for (k = 0; k < nw && words[k] != s->note[0]; k++)
                ;
            if (k == nw && nw < 6u)
                words[nw++] = s->note[0];
            col = CP_SND[k % 6u];
            cv_rect(x, y, 12, 12, col);
            cp_name(b, s->note[0], mode, base);
            if (mode == CM_INST)
                b[1] = 0;                               /* (the note's letter; its name in full on the line under) */
            if (str_len(b) == 1u)
                cp_tiny(x + 3, y + 1, b, C_BLACK, 2);
            else
                cp_tiny(x + 1, y + 4, b, C_BLACK, 1);
        } else if (s->time == ST_TIE && i) {
            cv_rect(x - 2, y + 4, 14, 4, mix565_ps(C_BLACK, col, 150));   /* (held on) */
        } else if (!(i % 4u)) {
            cv_rect(x + 5, y + 5, 2, 2, PS_DIM);         /* (a beat, nothing on it) */
        }
        if (i == now) {
            cv_rect(x - 1, y - 1, 14, 1, C_WHITE);
            cv_rect(x - 1, y + 12, 14, 1, C_WHITE);
            cv_rect(x - 1, y - 1, 1, 14, C_WHITE);
            cv_rect(x + 12, y - 1, 1, 14, C_WHITE);
        }
    }
    if (!cs_has_notes())
        ps_text(8, 34, &FONT_S, "nothing recorded: REC + keys", PS_LBL);
    cv_blit(0, 62);
}

/* PADS: the kit as the 16 pads, lit as they sound */
static void chop_pads_draw(uint32_t tiles[2])
{
    static tile_t tl[16];
    const track_t *t = &trk[CS_TRK];
    uint32_t base = 0, n = 0, i, lit = 0, kit = (uint32_t)cp_kit(&base, &n);
    for (i = 0; i < NVOICE; i++) {                      /* the hits sounding */
        const voice_t *v = &t->v[i];
        if (kit && v->active && v->stage <= 3u && v->note >= base && v->note < base + n)
            lit |= 1u << (v->note - base);
    }
    if (kit && cp_hit_now() && csr.hit >= base && csr.hit < base + n)
        lit |= 1u << (csr.hit - base);                  /* (and the last hit a moment) */
    memset(tl, 0, sizeof tl);
    for (i = 0; i < 16u && i < n; i++) {
        fmt_int(tl[i].lab, (int32_t)i + 1);
        if ((lit >> i) & 1u)
            tile_set(&tl[i], CP_COL, tile_glow(CP_COL), PS_TXT);
        else
            tile_set(&tl[i], PS_LINE, 0, PS_LBL);
    }
    tiles_draw(tl, tiles);
}
/* SLICE: the loop's slices; this bar's triggers under them */
static void chop_slice_draw(uint32_t *cache)
{
    uint32_t base = 0, n = 0, kit = (uint32_t)cs_kit(&base, &n), i, play = song.playing, hn = play ? csr.hist_n : 0u;
    uint32_t sl = play ? csr.sl : 0xFFu, at = play && n ? csr.pos / cs_sl16(n > 16u ? 16u : n) : 0xFFu;
    uint32_t sig = sl * 7u + at * 13u + n * 65537u + (uint32_t)trk[CS_TRK].p[P_E0] * 3u + hn * 977u + csr.bb * 31u;
    char b[4];
    n = n > 16u ? 16u : n;
    for (i = 0; i < hn && i < 16u; i++)
        sig = sig * 5u + csr.hist[i];
    if (!ui.force && sig == *cache)
        return;
    *cache = sig;
    cv_begin(240, 88, C_BLACK);
    ps_ground(62);
    for (i = 0; kit && i < n; i++) {                    /* sounding yellow, where the loop is pink */
        uint32_t cols = n > 8u ? 8u : n;
        int32_t x0 = 6 + (int32_t)((i % cols) * 228u / cols), x1 = 6 + (int32_t)((i % cols + 1u) * 228u / cols);
        int32_t y = 2 + (int32_t)(i / cols) * 24;
        uint32_t s = i == sl, w = i == at;
        cv_rect(x0, y, x1 - x0 - 2, 20, s ? CP_YEL : w ? CP_COL : CP_DIM);
        b[0] = (char)('A' + i);
        b[1] = 0;
        te_text_c((x0 + x1 - 2) / 2, y + 2, b, s || w ? C_BLACK : TE_G3);
    }
    if (kit && n && play) {                             /* this bar's triggers: what each played */
        uint32_t sl16 = cs_sl16(n), nt = sl16 < 16u ? 16u / sl16 : 1u;
        for (i = 0; i < nt && i < 16u; i++) {
            int32_t x0 = 6 + (int32_t)(i * 228u / nt), x1 = 6 + (int32_t)((i + 1u) * 228u / nt);
            uint32_t h = i < hn ? csr.hist[i] : 0xFFu;
            cv_rect(x0, 54, x1 - x0 - 2, 14, h == 0xFFu ? PS_DIM : (h & 0x80u) ? CP_YEL : CP_DIM);
            if (h != 0xFFu) {
                b[0] = (char)('A' + (h & 15u));
                b[1] = 0;
                cp_tiny((x0 + x1 - 2) / 2 - 3, 56, b, (h & 0x80u) ? C_BLACK : CP_COL, 2);
            }
        }
        ps_text(6, 70, &FONT_S, csr.bb == 2u ? "the break fills: straight" : csr.bb == 1u ? "the loop's bar" : "",
                csr.bb == 1u ? CP_YEL : PS_LBL);
    }
    if (!kit)
        ps_text(8, 30, &FONT_S, "not a kit: PRESETS", PS_LBL);
    cv_blit(0, 62);
}
/* INST: the sound, the keys, the theme as a roll */
static void chop_inst_draw(uint32_t *cache)
{
    const track_t *t = &trk[CS_TRK];
    const engine_t *e = ENGINES[t->eng_req % NENGINES];
    const cm_ev_t *ev = cm_ev[cm_b];
    uint32_t ne = cm_n[cm_b], i, now = song.playing ? (clk_beat * 8u + clk_pos * 8u / BEAT_U) / cs_u32(CM_INST) % (CM_BARS * 16u) : 0xFFu;
    uint32_t sig = t->eng_req * 131u + t->preset * 7u + cg_root() * 31u + cg_mask() + (uint32_t)(song.octave + 4) * 977u +
                   csr.gen * 7919u + (now / 2u) * 13u + ne;
    char b[24];
    if (!ui.force && sig == *cache)
        return;
    *cache = sig;
    cv_begin(240, 88, C_BLACK);
    ps_ground(62);
    ps_win(4, 2, 232, 26, CP_COL);
    str_cpy(b, e->npresets ? e->presets[t->preset % e->npresets].name : e->name, sizeof b);
    te_text_c(120, 7, b, C_WHITE);
    str_cpy(b, "keys ", sizeof b);
    str_cpy(b + str_len(b), N_NOTE[cg_root()], 4);
    str_cpy(b + str_len(b), cg_minor(cg_mask()) ? " minor  C" : " major  C", 10);
    fmt_int(b + str_len(b), 4 + song.octave);
    ps_text(8, 32, &FONT_S, b, PS_LBL);
    cv_rect(6, 50, 228, 34, RGB(14, 18, 52));            /* the theme: 8 bars, its notes by pitch */
    for (i = 1; i < CM_BARS; i++)
        cv_rect(6 + (int32_t)(i * 228u / CM_BARS), 50, 1, 34, PS_LINE);
    for (i = 0; i < ne; i++) {
        int32_t x0 = 6 + (int32_t)(ev[i].at * 228u / (CM_BARS * 16u)), w = (int32_t)(ev[i].len * 228u / (CM_BARS * 16u));
        int32_t y = 50 + 31 - clamp((int32_t)ev[i].note - CM_LO, 0, CM_HI - CM_LO) * 31 / (CM_HI - CM_LO);
        uint32_t lit = now >= ev[i].at && now < (uint32_t)ev[i].at + ev[i].len;
        cv_rect(x0, y, w > 1 ? w - 1 : 1, 2, lit ? CP_YEL : CP_COL);
    }
    if (now != 0xFFu)
        cv_rect(6 + (int32_t)(now * 228u / (CM_BARS * 16u)), 50, 1, 34, C_WHITE);
    if (!ne)
        ps_text(12, 58, &FONT_S, "melody off: the keys", PS_LBL);
    cv_blit(0, 62);
}

/* the line of what plays: the sound's name; what plays now (yellow a moment after each hit), else SLICE's loop key */
static void chop_info_draw(uint32_t *cache)
{
    const track_t *t = &trk[CS_TRK];
    const cs_meta_t *m = cs_meta();
    uint32_t mode = cs_mode(), base = 0, n = 0, kit = (uint32_t)cs_kit(&base, &n), keyed = (uint32_t)cs_keyed(m);
    uint32_t now = (uint32_t)cp_hit_now();
    uint32_t sig = mode * 7u + (uint32_t)t->p[P_E0] * 131u + t->preset * 3u + cg_root() * 77u + cg_mask() +
                   keyed * 999983u + (keyed ? m->key : 0u) * 101u + cp_page * 19u + now * (csr.hit + 1u) * 7919u;
    char b[24];
    uint16_t c = PS_LBL;
    if (!ui.force && sig == *cache)
        return;
    *cache = sig;
    cv_begin(240, 28, C_BLACK);
    ps_ground(150);
    if (mode == CM_INST) {
        const engine_t *e = ENGINES[t->eng_req % NENGINES];
        te_lower(b, e->npresets ? e->presets[t->preset % e->npresets].name : e->name, 14);
    } else {
        te_lower(b, kit ? SMP_ALL_NAMES[(uint32_t)t->p[P_E0] % SMP_NALL] : "not a kit", 14);
    }
    ps_text(6, 6, &FONT_S, b, PS_TXT);
    b[0] = 0;
    if (now && (mode == CM_INST || (kit && csr.hit >= base && csr.hit < base + n))) {   /* what plays now */
        str_cpy(b, mode == CM_PADS ? "now pad " : mode == CM_SLICE ? "now slice " : "now ", 12);
        cp_name(b + str_len(b), csr.hit, mode, base);
        c = PS_YEL;
    } else if (mode == CM_SLICE) {                      /* the loop's key to the session's: Fm > Am +4 */
        if (keyed) {
            int32_t sh = cs_shift(m);
            str_cpy(b, N_NOTE[(m->key & 15u) % 12u], 4);
            str_cpy(b + str_len(b), (m->key & 0x10u) ? "m > " : " > ", 5);
            str_cpy(b + str_len(b), N_NOTE[cg_root()], 4);
            str_cpy(b + str_len(b), cg_minor(cg_mask()) ? "m " : " ", 3);
            if (sh >= 0)
                str_cpy(b + str_len(b), "+", 2);
            fmt_int(b + str_len(b), sh);
        } else {
            str_cpy(b, "no key", sizeof b);
        }
    }
    ps_text(234 - text_w(&FONT_S, b), 6, &FONT_S, b, c);
    cv_blit(0, 150);
}

/* ---- a tab seen while another plays: its own settings, its sound to come (nothing that plays now changes) */
/* the n-th of INST's instruments (the CHOP's list in INST: ui.c preset_pos), its name in b */
static uint32_t chop_inst_count(void)
{
    if (!bank_ready)
        bank_resolve();
    return bank_nvis() + up_count();
}
static void chop_inst_name(uint32_t n, char *b)
{
    uint32_t nv = bank_nvis();
    if (n < nv) {
        uint32_t i = bank_vis(n);
        const engine_t *e = ENGINES[BANK[i].e % NENGINES];
        str_cpy(b, e->npresets ? e->presets[(bank_pi[i] == 0xFF ? 0u : bank_pi[i]) % e->npresets].name : e->name, 14);
    } else {
        up_name(up_nth(n - nv), b);
    }
}
/* the sound tab m plays when chosen: its name in b (14), its place in its list and the list's length */
static uint32_t chop_mem_pos(uint32_t m, uint32_t *total, char *b)
{
    uint32_t i, n = 0, bb, nh, s;
    if (m == CM_INST) {
        *total = chop_inst_count();
        n = *total ? cp_inst % *total : 0u;
        if (*total)
            chop_inst_name(n, b);
        else
            str_cpy(b, "-", 14);
        return n;
    }
    kit_nth(0, total);
    s = cp_mem[m] != 0xFFu && smp_kit(cp_mem[m], &bb, &nh) ? cp_mem[m] : (cs_kit(&bb, &nh) ? (uint32_t)trk[CS_TRK].p[P_E0] % SMP_NALL : kit_nth(0, total));
    for (i = 0; i < s; i++)
        n += (uint32_t)smp_kit(i, &bb, &nh);
    str_cpy(b, SMP_ALL_NAMES[s % SMP_NALL], 14);
    return n;
}
static void chop_mem_step(uint32_t m, int32_t s)    /* PRESETS / KNOB 1 on a tab not playing: its sound to come */
{
    uint32_t total, cur;
    char b[16];
    cur = chop_mem_pos(m, &total, b);
    if (!total)
        return;
    cur = (uint32_t)(((int32_t)cur + s % (int32_t)total + (int32_t)total) % (int32_t)total);
    if (m == CM_INST)
        cp_inst = (uint8_t)cur;
    else
        cp_mem[m] = (uint8_t)kit_nth(cur, &total);
    ui.force = 1;
}
/* the body of a tab not playing: its sound, and how to play it */
static void chop_idle_draw(uint32_t *cache)
{
    uint32_t total, sig;
    char b[24], nm[16];
    chop_mem_pos(cp_view, &total, nm);
    sig = 0x1D1Eu + cp_view * 7u + cs_mode() * 31u + (cp_view == CM_INST ? cp_inst : cp_mem[cp_view]) * 977u;
    if (!ui.force && sig == *cache)
        return;
    *cache = sig;
    cv_begin(240, 88, C_BLACK);
    ps_ground(62);
    ps_win(4, 2, 232, 26, CP_COL);
    te_text_c(120, 7, nm, C_WHITE);
    str_cpy(b, "playing now: ", sizeof b);
    str_cpy(b + str_len(b), N_CS_MODE[cs_mode()], 8);
    te_lower(b, b, sizeof b);
    ps_text(8, 34, &FONT_S, b, PS_LBL);
    str_cpy(b, "sel: play ", sizeof b);
    str_cpy(b + str_len(b), N_CS_MODE[cp_view], 8);
    te_lower(b, b, sizeof b);
    ps_text(8, 56, &FONT_S, b, CP_COL);
    cv_blit(0, 62);
}

/* a mode's RATE as a dial: 1/4 .. 1/32 */
static int32_t chop_rate_ratio(uint32_t mode)
{
    uint32_t i;
    for (i = 0; i < CR_N && CR_ORDER[i] != cs_rate(mode); i++)
        ;
    return (int32_t)i * 333;
}
static void chop_screen_draw(void)
{
    static uint32_t head, tabs, body, tiles[2], info, foot;
    static const char *const MAIN_NAME[CM_N] = {"pads", "slice", "sound"};
    const track_t *t = &trk[CS_TRK];
    uint32_t play = cs_mode(), mode = cp_view < CM_N ? cp_view : play, idle = mode != play, len = trk_len(t), b = 0, n = 0;
    uint32_t pads = cp_page == CPG_MAIN && mode == CM_PADS && !idle;
    static char v[4][12];
    const char *val[4] = {v[0], v[1], v[2], v[3]};
    const char *lab[4];
    int32_t ratio[4] = {0, 0, 0, 0};
    if (csr.hits != cp_hits) {                          /* (a hit: what plays now, from now on a moment) */
        cp_hits = csr.hits;
        cp_hit_ms = fm1_ms;
    }
    te_header(cp_page == CPG_STEPS ? "steps" : cp_page == CPG_TIME ? "time" : MAIN_NAME[mode], CP_COL, &head);
    chop_tabs_draw(&tabs);
    if (cp_page == CPG_STEPS || (cp_page == CPG_TIME && play == CM_PADS))
        chop_steps_draw(&body);
    else if (cp_page == CPG_MAIN && idle)
        chop_idle_draw(&body);
    else if (cp_page == CPG_TIME ? play == CM_PADS : mode == CM_PADS)
        chop_pads_draw(tiles);
    else if ((cp_page == CPG_TIME ? play : mode) == CM_SLICE)
        chop_slice_draw(&body);
    else
        chop_inst_draw(&body);
    if (!pads) {                                        /* (the pads reach the dials) */
        tiles[0] = tiles[1] = 0;
        chop_info_draw(&info);
    } else {
        body = info = 0;
    }
    v[0][0] = v[1][0] = v[2][0] = v[3][0] = 0;
    if (cp_page == CPG_STEPS) {
        static const char *const L[4] = {"length", "rate", "swing", ""};
        memcpy(lab, L, sizeof lab);
        fmt_int(v[0], (int32_t)(len <= 16u ? 1 : len <= 32u ? 2 : 4));
        str_cpy(v[0] + str_len(v[0]), len > 16u ? " bars" : " bar", 6);
        str_cpy(v[1], N_CS_RATE[cs_rate(play)], 12);
        fmt_int(v[2], t->p[P_SSWING]);
        ratio[0] = len <= 16u ? 0 : len <= 32u ? 500 : 1000;
        ratio[1] = chop_rate_ratio(play);
        ratio[2] = t->p[P_SSWING] * 10;
        te_hint_txt = "REC RECORDS  REC HELD: CLEAR";
    } else if (cp_page == CPG_TIME) {                   /* each mode its RATE; INST its theme's octave */
        static const char *const L[4] = {"pads", "slice", "inst", "octave"};
        uint32_t i;
        memcpy(lab, L, sizeof lab);
        for (i = 0; i < CM_N; i++) {
            str_cpy(v[i], N_CS_RATE[cs_rate(i)], 12);
            ratio[i] = chop_rate_ratio(i);
        }
        if (cs_oct() > 0)
            v[3][0] = '+';
        fmt_int(v[3] + (cs_oct() > 0), cs_oct());
        ratio[3] = (cs_oct() + 2) * 250;
        te_hint_txt = "RATE: EACH MODE ITS OWN";
    } else if (mode == CM_PADS) {
        static const char *const L[4] = {"kit", "mode", "choke", "tune"};
        memcpy(lab, L, sizeof lab);
        if (idle) {                                     /* (MODE, CHOKE, TUNE: the track's, of what plays now) */
            uint32_t total, at = chop_mem_pos(CM_PADS, &total, v[0]);
            te_lower(v[0], v[0], 7);
            str_cpy(v[1], "-", 12);
            str_cpy(v[2], "-", 12);
            str_cpy(v[3], "-", 12);
            ratio[0] = total > 1u ? (int32_t)(at * 1000u / (total - 1u)) : 0;
        } else {
            te_lower(v[0], cs_kit(&b, &n) ? SMP_ALL_NAMES[(uint32_t)t->p[P_E0] % SMP_NALL] : "-", 7);
            str_cpy(v[1], t->p[P_REL] >= 100 ? "one" : "gate", 12);
            str_cpy(v[2], t->p[P_VOICE] == V_MONO ? "on" : "off", 12);
            fmt_int(v[3], t->p[P_E1]);
            ratio[0] = (int32_t)((uint32_t)t->p[P_E0] % SMP_NALL) * 1000 / (int32_t)(SMP_NALL - 1u);
            ratio[1] = t->p[P_REL] >= 100 ? 1000 : 0;
            ratio[2] = t->p[P_VOICE] == V_MONO ? 1000 : 0;
            ratio[3] = (t->p[P_E1] + 24) * 1000 / 48;
        }
        te_hint_txt = "BLACK 1 STUTTER 2 GAP 3 DOWN";
    } else if (mode == CM_SLICE) {
        static const char *const L[4] = {"style", "cmplx", "retrig", "tune"};
        memcpy(lab, L, sizeof lab);
        te_lower(v[0], N_CS_STYLE[cs_style()], 12);
        fmt_int(v[1], (int32_t)cs_cmplx());
        fmt_int(v[2], (int32_t)cs_retrig());
        te_lower(v[3], idle || cs_keyed(cs_meta()) ? N_CS_TUNE[cs_tune()] : "no key", 12);
        ratio[0] = (int32_t)cs_style() * 200;
        ratio[1] = (int32_t)cs_cmplx() * 10;
        ratio[2] = (int32_t)cs_retrig() * 10;
        ratio[3] = (int32_t)cs_tune() * 333;
        te_hint_txt = "BLACK 1 STUTTER 2 GAP 3 DOWN";
    } else {
        static const char *const L[4] = {"sound", "melody", "theme", "choke"};
        uint32_t total, cur = idle ? chop_mem_pos(CM_INST, &total, v[0]) : preset_pos(&total);
        memcpy(lab, L, sizeof lab);
        fmt_int(v[0], (int32_t)cur + 1);
        te_lower(v[1], N_CS_MEL[cs_melody()], 12);
        uint32_t nth = cm_themes(), th = nth ? cs_theme() % nth : 0u;
        const uint8_t *rec = cs_melody() == CML_MIDI ? cm_theme_rec(th) : 0;
        if (rec) {                                   /* MIDI: the theme's name (the file's) */
            uint32_t i;
            for (i = 0; i < 8u && rec[i] >= ' ' && rec[i] < 127; i++)
                v[2][i] = (char)rec[i];
            v[2][i] = 0;
            te_lower(v[2], v[2], 12);
        } else if (cs_melody() == CML_MIDI) {
            str_cpy(v[2], "none", 12);
        } else {
            fmt_int(v[2], (int32_t)th + 1);
        }
        str_cpy(v[3], idle ? "-" : t->p[P_VOICE] == V_MONO ? "on" : "off", 12);
        ratio[0] = total > 1u ? (int32_t)(cur * 1000u / (total - 1u)) : 0;
        ratio[1] = (int32_t)cs_melody() * 333;
        ratio[2] = nth > 1u ? (int32_t)(th * 1000u / (nth - 1u)) : 0;
        ratio[3] = t->p[P_VOICE] == V_MONO ? 1000 : 0;
        te_hint_txt = cs_melody() == CML_MIDI && !nth ? "SEND MIDI FROM THE APP" : "OCT: TABS  BLACK 4: NEW THEME";
    }
    if (idle && cp_page == CPG_MAIN)
        te_hint_txt = "OCT: TABS  SEL: PLAY THIS ONE";
    te_dials(178, lab, val, ratio, 7u + mode * 31u + cp_page * 977u + idle * 7919u, &foot);
}

/* a change of the CHOP while the song plays: the next section and STOP keep it (arranger_scene.c song_keep_trk) */
static void chop_changed(void)
{
    song_keep_now(CS_TRK, 1);
    ui.force = 1;
}

/* MODE m: its sound (the kit PADS / SLICE had, INST's instrument), its voice */
static void chop_mode_set(uint32_t m)
{
    track_t *t = &trk[CS_TRK];
    uint32_t b, n, total, k, e;
    if (cs_mode() == CM_INST)
        cp_inst = (uint8_t)preset_pos(&total);
    else if (cs_kit(&b, &n))
        cp_mem[cs_mode()] = (uint8_t)t->p[P_E0];
    q_drop(CS_TRK);
    csr.fx = 0;
    CSV(0) = (int16_t)(m % CM_N);
    if (m == CM_INST) {
        preset_pos(&total);
        e = preset_at(total ? cp_inst % total : 0u, &k);
        if (e == NENGINES) {
            up_load(k);
        } else {
            if (e != t->eng_req)
                set_engine_of(t, e);
            apply_preset_to(t, k);
        }
    } else {                                            /* PADS's kit, SLICE's loop: its own (else the kit there) */
        uint32_t want = cp_mem[m];
        if (want == 0xFFu || !smp_kit(want, &b, &n))
            want = cs_kit(&b, &n) ? (uint32_t)t->p[P_E0] % SMP_NALL : kit_nth(0, &total);
        if (!cs_kit(&b, &n) || want != (uint32_t)t->p[P_E0] % SMP_NALL) {
            chop_kit_go(want);
            t->p[P_E0] = (int16_t)want;                 /* (at once: no Q for the mode's own sound) */
        }
        cp_mem[m] = (uint8_t)want;
    }
    if (m == CM_SLICE) {
        t->p[P_VOICE] = V_MONO;                         /* (a slice cuts the one before: the loop chopped) */
        t->p[P_REL] = 120;                              /* (each plays out to the next) */
    }
    cp_view = (uint8_t)(m % CM_N);                      /* (LIVE's black keys too: the screen opens on it) */
    chop_changed();
    ui_message(N_CS_MODE[m % CM_N]);
}

/* SEL on the screen: the tab seen plays (ui_input.c layer_tap); the one playing goes on till then (OCT- / OCT+ walk
 * the tabs: chop_screen_input) */
static int chp_layer_tap(uint32_t layer)
{
    if (layer != LY_SCALE)
        return 0;
    if (cp_view < CM_N && cp_view != cs_mode())
        chop_mode_set(cp_view);
    else
        ui_message("PLAYING");
    cp_view = (uint8_t)cs_mode();
    return 1;
}
static int chop_oct_led(int up)                    /* OCT- / OCT+ lit: a tab that way (ui_input.c) */
{
    uint32_t v = cp_view < CM_N ? cp_view : cs_mode();
    return up ? v + 1u < CM_N : v > 0u;
}
static void chop_open(void)
{
    cp_page = CPG_MAIN;
    cp_view = (uint8_t)cs_mode();
}

/* a black key of the screen (ui_input.c, the queue of seq.c): held 1 STUTTER, 2 GAP, 3 DOWN; INST: 4 NEW */
static void chp_key(uint32_t k, uint32_t down)
{
    static const int8_t BLACK[4] = {1, 3, 5, 8};
    static const uint8_t FX[3] = {CF_STUT, CF_GAP, CF_DOWN};
    uint32_t b;
    for (b = 0; b < 4u && BLACK[b] != (int8_t)k; b++)
        ;
    if (b < 3u) {
        csr.fx = (uint8_t)(down ? (csr.fx | FX[b]) : (csr.fx & ~FX[b]));
        ui.force = 1;
    } else if (b == 3u && down && cs_mode() == CM_INST) {
        uint32_t nth = cm_themes();
        CSV(6) = (int16_t)(nth ? (cs_theme() + 1u) % nth : 0u);
        chop_changed();
        ui_message("NEW THEME");
    }
}

static void chop_leave(void)
{
    csr.fx = 0;
    go_home();
}

/* the CHOP's kit to SET set (PRESETS anywhere, KNOB 1 KIT): the engine to SAMPLE first (at once: a kit plays there),
 * the kit itself on the bar with Q (q.c keeps it for the song then) */
static void chop_kit_go(uint32_t set)
{
    track_t *t = &trk[CS_TRK];
    if (ENGINES[t->eng_req % NENGINES] != &ENG_SAMPLE) {
        uint32_t pi;
        for (pi = 0; pi < ENG_SAMPLE.npresets && !str_eq(ENG_SAMPLE.presets[pi].name, "CHOP KIT"); pi++)
            ;
        q_drop(CS_TRK);
        set_engine_of(t, ENGI_SAMPLE);
        apply_preset_to(t, pi < ENG_SAMPLE.npresets ? pi : 0u);
        t->p[P_E0] = (int16_t)set;
        song_keep_now(CS_TRK, 0);
    } else if (q_on()) {
        char w[16];
        q_param(CS_TRK, P_E0, (int32_t)set);
        w[0] = '@';
        fmt_int(w + 1, (int32_t)q_bar_at());
        str_cpy(w + str_len(w), " ", 2);
        ui_say(w, SMP_ALL_NAMES[set % SMP_NALL]);
    } else {
        t->p[P_E0] = (int16_t)set;
        song_keep_now(CS_TRK, 0);
        ui_say("", SMP_ALL_NAMES[set % SMP_NALL]);
    }
    ui.force = 1;
}
/* PRESETS, KNOB 1 KIT / SOUND: the mode's list (ui.c preset_pos: the kits, or the instruments) */
static void chop_kit_knob(int32_t s)
{
    uint32_t total, cur = preset_pos(&total);
    if (total)
        preset_go((uint32_t)(((int32_t)cur + s % (int32_t)total + (int32_t)total) % (int32_t)total));
}

static void chop_knob(uint32_t k, int32_t s)
{
    track_t *t = &trk[CS_TRK];
    uint32_t mode = cs_mode();
    if (cp_page == CPG_STEPS) {
        if (k == 0u) {
            static const uint8_t LENS[3] = {16u, 32u, 64u};
            int32_t i = trk_len(t) <= 16u ? 0 : trk_len(t) <= 32u ? 1 : 2;
            t->p[P_SLEN] = (int16_t)LENS[clamp(i + s, 0, 2)];
        } else if (k == 1u) {
            cs_rate_step(mode, s);                      /* (this mode's RATE: the TIME page's too) */
        } else if (k == 2u) {
            t->p[P_SSWING] = (int16_t)clamp(t->p[P_SSWING] + accel(EN_K3, s, 100), 0, 100);
        }
    } else if (cp_page == CPG_TIME) {
        if (k < CM_N)
            cs_rate_step(k, s);
        else
            cs_oct_set(cs_oct() + s);
    } else if (cp_view < CM_N && cp_view != mode) {   /* a tab not playing: its own settings only */
        if (cp_view == CM_PADS || (cp_view == CM_INST && k == 0u)) {
            if (k == 0u)
                chop_mem_step(cp_view, s);
        } else if (cp_view == CM_SLICE) {
            if (k == 0u) {
                CSV(1) = (int16_t)clamp((int32_t)cs_style() + s, 0, CY_N - 1);
                CSV(2) = CS_STY[cs_style()][0];
            } else if (k == 1u) {
                CSV(2) = (int16_t)clamp((int32_t)cs_cmplx() + accel(EN_K2, s, 100), 0, 100);
            } else if (k == 2u) {
                CSV(3) = (int16_t)clamp((int32_t)cs_retrig() + accel(EN_K3, s, 100), 0, 100);
            } else {
                CSV(5) = (int16_t)clamp((int32_t)cs_tune() + s, 0, CT_N - 1);
            }
        } else if (k == 1u) {
            CSV(4) = (int16_t)clamp((int32_t)cs_melody() + s, 0, CML_N - 1);
        } else if (k == 2u) {
            CSV(6) = (int16_t)(cm_themes() ? ((int32_t)(cs_theme() % cm_themes()) + (int32_t)cm_themes() + s) %
                                                 (int32_t)cm_themes() : 0);
        }
    } else if (mode == CM_PADS) {
        if (k == 0u)
            chop_kit_knob(s);
        else if (k == 1u)
            t->p[P_REL] = (int16_t)(s > 0 ? 120 : 6);  /* ONE: the release outlasts the hit; GATE: it stops */
        else if (k == 2u)
            t->p[P_VOICE] = (int16_t)(s > 0 ? V_MONO : V_POLY);
        else
            t->p[P_E1] = (int16_t)clamp(t->p[P_E1] + s, -24, 24);
    } else if (mode == CM_SLICE) {
        if (k == 0u) {
            CSV(1) = (int16_t)clamp((int32_t)cs_style() + s, 0, CY_N - 1);
            CSV(2) = CS_STY[cs_style()][0];             /* (the STYLE's own CMPLX) */
        } else if (k == 1u) {
            CSV(2) = (int16_t)clamp((int32_t)cs_cmplx() + accel(EN_K2, s, 100), 0, 100);
        } else if (k == 2u) {
            CSV(3) = (int16_t)clamp((int32_t)cs_retrig() + accel(EN_K3, s, 100), 0, 100);
        } else {
            CSV(5) = (int16_t)clamp((int32_t)cs_tune() + s, 0, CT_N - 1);
        }
    } else {
        if (k == 0u)
            chop_kit_knob(s);
        else if (k == 1u)
            CSV(4) = (int16_t)clamp((int32_t)cs_melody() + s, 0, CML_N - 1);
        else if (k == 2u)
            CSV(6) = (int16_t)(cm_themes() ? ((int32_t)(cs_theme() % cm_themes()) + (int32_t)cm_themes() + s) %
                                                 (int32_t)cm_themes() : 0);
        else
            t->p[P_VOICE] = (int16_t)(s > 0 ? V_MONO : V_POLY);
    }
    chop_changed();
}

static void chop_screen_input(uint32_t pressed, uint32_t home)
{
    uint32_t k;
    int32_t s;
    if (home == BT_TAP) {
        chop_leave();
        return;
    }
    if (pressed & (1u << panel.btn[B_PLAY]))
        studio_play();                                  /* (ui_input.c: the loop) */
    if ((s = panel_enc(EN_ALGO)) != 0 && !ft_on) {     /* ALGORITHM: another track, on TRACKS */
        track_step(s);
        chop_leave();
        return;
    }
    if ((s = panel_enc(EN_SELECT)) != 0) {              /* SELECT: the mode's page, STEPS, TIME */
        cp_page = (uint8_t)clamp((int32_t)cp_page + (s > 0 ? 1 : -1), 0, CPG_N - 1);
        ui.msg_t = 0;
        ui.force = 1;
    }
    if ((s = panel_enc(EN_PRESET)) != 0) {              /* PRESETS: the mode's sounds (a tab not playing: its own) */
        if (cp_view < CM_N && cp_view != cs_mode())
            chop_mem_step(cp_view, s);
        else
            chop_kit_knob(s);
        ui.force = 1;
    }
    if (pressed & ((1u << panel.btn[B_OCTDN]) | (1u << panel.btn[B_OCTUP]))) {   /* OCT- / OCT+: the tabs (SEL plays) */
        int32_t d = (pressed & (1u << panel.btn[B_OCTUP])) ? 1 : -1;
        cp_view = (uint8_t)clamp((cp_view < CM_N ? (int32_t)cp_view : (int32_t)cs_mode()) + d, 0, CM_N - 1);
        cp_page = CPG_MAIN;
        ui.msg_t = 0;
        ui.force = 1;
    }
    for (k = 0; k < 4u; k++) {
        if ((s = panel_enc(EN_K1 + k)) == 0)
            continue;
        ui.hot_col = (uint8_t)k;
        ui.hot_t = 40;
        chop_knob(k, s);
    }
}
