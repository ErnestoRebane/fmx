/* SPDX-License-Identifier: GPL-3.0-only */
/* The BREAK screen: the BREAK track's own page (X0X's break page, 240 x 240, in SLOOP's live style). The loop playing
 * cut in its eight slices (the slice playing lit), 16 steps of the track with the slice each plays, the loops A..D,
 * four dials.
 *   reached  the BREAK track selected: SEQ / ARP / EDIT / SEL from TRACKS, or ALGORITHM onto it from a synth page;
 *            HOME, or ALGORITHM right (the next lane: BASS), goes back
 *   white keys  16 steps: press one to let the break play there, press it again to stop it
 *   black keys  1-8 hold that slice (let go, the generator takes over; REC on and playing: recorded into the step);
 *               9 reverse, 10 half speed, 11 stutter
 *   a white key held + black key 1-8: the step plays that slice, from its start, whatever the generator would
 *               choose (a second press of the same key gives it back to the generator); + black key 9: the step
 *               plays backwards (again: forwards); + KNOB 1..4: its SLICE, LEVEL, RETRIG (x2 .. x8), PITCH
 *   SEQ  STYLE (STYLE, MANGL, DIV, SWING), again GROOVE (CMPLX, ANCHR, ROLL, FILL)
 *   ARP  RETRIG (2X, 3X, 4X, 8X: the chance a bar of stuttering a beat)
 *   EDIT LOOPS (LOOP A..D), then BARS (A..D), then LAYER (LAYER: B, C or D under every slice, L MIX, L PTCH)
 *   SEL  PHRASE (PHRSE, B CHN, C CHN, D CHN)
 *   GLO  MIX (LVL, PAN, REV, DLY)                ENV  TUNE (PTCH, LEN, DRIVE, FILT)
 *   LFO  PATTERN (STEPS, DIV, SWING, INTP: LIN or PSX, the PlayStation's softer Gaussian)
 *   SELECT the pages, PRESETS loop A, OCT- mute, OCT+ the next 16 steps (a pattern longer than 16), PLAY the transport
 * The loops are BR1, BR2 ... (every zone of the pool's entries named BR...: the web page "Break loops", its standard set
 * AMEN 2, 3, 4). The waveform is the loop as it is stored (peaks of its ADPCM, taken when the loop is built). */
#define BK_COL RGB(255, 138, 58)                        /* the BREAK track's orange (ui_studio.c TRK_COL, as LIVE and the DECK) */
#define BK_MID RGB(156, 84, 35)
#define BK_DIM RGB(66, 36, 15)
enum { BK_STYLE, BK_GROOVE, BK_RETRIG, BK_LOOPS, BK_BARS, BK_LAYER, BK_PHRASE, BK_MIX, BK_TUNE, BK_PATTERN, BK_NPAGE };
static const char *const BK_PAGE_NAME[BK_NPAGE] = {"style", "groove", "retrig", "loops", "bars", "layer", "phrase", "mix",
                                                   "tune", "pattern"};
static const uint8_t BK_PAGE[BK_NPAGE][4] = {           /* the track's p[] (the generator's: brkpart.c BPP) */
    {P_PRIO, P_ED_FLT, P_SDIV, P_SSWING},               /* STYLE MANGL DIV SWING */
    {P_E0, P_E1, P_E2, P_E3},                           /* CMPLX ANCHR ROLL FILL */
    {P_E4, P_E5, P_E6, P_E7},                           /* 2X 3X 4X 8X */
    {P_LRATE, P_LWAVE, P_LPHASE, P_LFADE},              /* LOOP A..D */
    {P_LD_PIT, P_LD_FLT, P_LD_SHP, P_LD_AMP},           /* A..D BARS */
    {P_GLMODE, P_DETUNE, P_GLIDE, 0xFF},                /* LAYER L MIX L PTCH */
    {P_ATK, P_DEC, P_SUS, P_REL},                       /* PHRSE B CHN C CHN D CHN */
    {P_LEVEL, P_PAN, P_REV, P_DLY},
    {P_ED_PIT, P_ED_SHP, P_DIST, P_TFLT},               /* PTCH LEN DRIVE FILT */
    {P_SLEN, P_SDIV, P_SSWING, P_ALLOC},                /* STEPS DIV SWING INTP */
};
static const int8_t BK_BLACK[11] = {1, 3, 5, 8, 10, 13, 15, 17, 20, 22, 25};   /* the black keys, F#3 up: key index */
static uint8_t bk_page, bk_spage;                       /* the page of dials; the 16 steps shown when stopped */
static uint16_t bk_wdown, bk_wused, bk_live;            /* white steps down, used as a modifier; black slice keys held live */

static int on_break_page(void) { return !ui.home && cur_page()->scope == SC_BREAK && is_brk(TSEL); }

/* the step the transport is on, -1 stopped */
static int bk_step_now(void) { return song.playing && TBRK->seq_abs != SEQ_NONE ? (int)(TBRK->seq_idx % NSTEP) : -1; }
/* the first of the 16 steps shown: the page of the playhead, else the one OCT+ picked */
static uint32_t bk_first(void)
{
    uint32_t len = trk_len(TBRK);
    int step = bk_step_now();
    if (step >= 0)
        return (uint32_t)step / 16u * 16u;
    if (bk_spage * 16u >= len)
        bk_spage = 0;
    return bk_spage * 16u;
}
static int bk_is_loop(uint32_t id) { return id >= P_LRATE && id <= P_LFADE; }   /* LOOP A..D: BR1 .. */

static void break_screen_draw(void)
{
    static uint32_t head, wave_sig, steps_sig, info_sig, foot;
    const track_t *t = TBRK;
    uint32_t i, j, sig, bank = bp_bank(), live = bp.keys & 0x7FFu, playing = song.playing && bp.running;
    uint32_t first = bk_first(), len = trk_len(t);
    int step = bk_step_now(), cur = playing && bp.st_slice >= 0 ? bp.st_slice : -1;
    te_header(BK_PAGE_NAME[bk_page % BK_NPAGE], BK_COL, &head);
    /* the loop, in eight slices: the slice playing lit */
    sig = (uint32_t)(cur + 1) * 131u + bank * 7u + live * 977u + bpb[bank].key * 31u + (brkp_bank_src(bank) != 0) * 5u;
    if (ui.force || sig != wave_sig) {
        const uint8_t *w = bpb[bank].peaks;
        wave_sig = sig;
        cv_begin(240, 64, C_BLACK);
        for (i = 0; i < BP_NSL; i++) {
            int32_t x0 = (int32_t)i * 30;
            uint32_t on = (int)i == cur || ((live >> i) & 1u);
            char n[2] = {(char)('1' + i), 0};
            if (on)
                cv_rect(x0, 0, 30, 54, BK_DIM);
            for (j = 0; j < 30u; j++) {
                int32_t h = (int32_t)w[i * 30u + j] * 26 / 255;
                if (w[i * 30u + j] && !h)
                    h = 1;
                cv_rect(x0 + (int32_t)j, 27 - h, 1, 2 * h + 1, on ? C_WHITE : BK_COL);
            }
            if (i)
                cv_rect(x0, 0, 1, 54, TE_G2);
            te_text_c(x0 + 15, 48, n, on ? C_WHITE : TE_G3);
        }
        if (!brkp_bank_src(bank))
            te_text_c(120, 22, "no loop", TE_G3);
        cv_blit(0, 40);
    }
    /* 16 steps: on / off, a step's own slice, its level, the playhead */
    sig = first * 31u + (uint32_t)(step + 1) * 7u + len * 3u + bk_wdown * 7919u;
    for (i = 0; i < 16u; i++) {
        const bstep_t *b = &t->bstep[(first + i) % NSTEP];
        sig = sig * 5u + b->on + b->slice * 2u + b->lvl * 37u + b->rat * 101u + b->flags * 13u + (uint32_t)(b->pitch + 24) * 211u;
    }
    if (ui.force || sig != steps_sig) {
        steps_sig = sig;
        cv_begin(240, 36, C_BLACK);
        for (i = 0; i < 16u; i++) {
            uint32_t idx = first + i;
            int32_t x = (int32_t)i * 15;
            const bstep_t *b = &t->bstep[idx % NSTEP];
            uint32_t on = idx < len && b->on;
            char n[2] = {(char)('0' + b->slice), 0};
            if (idx >= len)
                continue;
            cv_rect(x, 2, 13, 22, on ? (b->lvl == LV_GHOST ? BK_DIM : b->lvl == LV_SOFT ? BK_MID : b->lvl == LV_HARD ? C_WHITE : BK_COL)
                                     : (i & 3u) ? TE_G1 : TE_G2);
            if (b->slice)
                te_text_c(x + 6, 4, n, on ? C_BLACK : TE_G3);
            if (on && b->rat)
                cv_rect(x + 2, 20, (int32_t)b->rat * 2, 2, C_BLACK);   /* a retrigger: a notch per step of it */
            if (b->flags & BS_REV)
                cv_rect(x, 2, 13, 2, on ? C_WHITE : TE_G3);              /* reverse: a line on top */
            if (b->pitch)
                cv_rect(x + 10, b->pitch > 0 ? 6 : 16, 2, 2, on ? C_BLACK : TE_G3);   /* pitched up / down: a dot */
            if ((bk_wdown >> i) & 1u)
                cv_rect(x, 24, 13, 2, TE_RED);                            /* held: being edited */
            if (step >= 0 && (uint32_t)step == idx)
                cv_rect(x, 27, 13, 3, C_WHITE);
        }
        if (len > 16u) {                                /* "2/4": the page of steps */
            char p[4] = {(char)('1' + first / 16u), '/', (char)('0' + (len + 15u) / 16u), 0};
            cv_text(208, 26, &FONT_S, p, TE_G3);
        }
        cv_blit(0, 104);
    }
    /* the loops, and what plays */
    sig = bank * 3u + playing * 5u + (uint32_t)t->p[P_MUTE] * 977u + (uint32_t)(cur + 1) * 13u + (uint32_t)bp.st_div * 31u +
          bp.st_mangle * 1009u + (uint32_t)BPV(BP_LAYER) * 6007u +
          live * 59u + (uint32_t)(song.rec >> TRK_BRK & 1u) * 4099u;
    for (i = 0; i < BP_NBANK; i++)
        sig = sig * 7u + bpb[i].key * 17u + brkp_bank_bars(i) * 101u + (brkp_bank_src(i) != 0) * 11u;
    if (ui.force || sig != info_sig) {
        int32_t x;
        char b[16], nm[8];
        uint32_t bk;
        info_sig = sig;
        cv_begin(240, 44, C_BLACK);
        x = 2;
        for (bk = 0; bk < BP_NBANK; bk++) {             /* "a BR1  b BR2  c BR3  d --": the playing one white */
            uint16_t c = bank == bk && playing ? C_WHITE : TE_G4;
            b[0] = (char)('a' + bk);
            b[1] = 0;
            x = cv_text(x, 3, &FONT_S, b, bank == bk && playing ? BK_COL : TE_G3);
            if (brkp_bank_src(bk)) {
                uint32_t r, w = 0;
                brkp_loop_name(brkp_bank_loop(bk), b);
                for (r = 0; b[r] && w < 5u; r++)        /* "AMEN 2" -> "AMEN2": five characters, no space */
                    if (b[r] != ' ')
                        nm[w++] = b[r];
                nm[w] = 0;
                x = cv_text(x + 2, 3, &FONT_S, nm, c);
            } else {
                x = cv_text(x + 2, 3, &FONT_S, "--", TE_G3);
            }
            x += 10;
        }
        if (t->p[P_MUTE]) {
            cv_text(4, 24, &FONT_S, "muted", TE_RED);
        } else if (!playing) {
            cv_text(4, 24, &FONT_S, (song.rec >> TRK_BRK) & 1u ? "rec armed" : "stopped", TE_G3);
        } else {
            str_cpy(b, "playing ", sizeof b);
            b[8] = (char)('a' + bank);
            b[9] = ' ';
            fmt_int(b + 10, (int32_t)brkp_bank_bars(bank));
            if (BPV(BP_LAYER) && (uint32_t)BPV(BP_LAYER) != bank) {   /* "+c": its LAYER */
                uint32_t e = str_len(b);
                b[e] = ' ';
                b[e + 1u] = '+';
                b[e + 2u] = (char)('a' + BPV(BP_LAYER));
                b[e + 3u] = 0;
            }
            x = cv_text(4, 24, &FONT_S, b, C_WHITE);
            if ((song.rec >> TRK_BRK) & 1u)
                x = cv_text(x + 8, 24, &FONT_S, "rec", TE_RED);
            if (live & 0x100u)
                x = cv_text(x + 8, 24, &FONT_S, "rev", BK_COL);
            if (live & 0x200u)
                x = cv_text(x + 8, 24, &FONT_S, "half", BK_COL);
            if (live & 0x400u)
                x = cv_text(x + 8, 24, &FONT_S, "stut", BK_COL);
            if (bp.st_mangle) {                         /* MANGLE's last trick */
                static const char *const MG[6] = {"", "stut", "rev", "jump", "dive", "gap"};
                x = cv_text(x + 8, 24, &FONT_S, MG[bp.st_mangle % 6u], TE_RED);
            } else if (bp.st_div > 1) {
                fmt_int(b, bp.st_div);
                str_cpy(b + str_len(b), "x", 2);
                cv_text(x + 8, 24, &FONT_S, b, BK_COL);
            }
        }
        cv_blit(0, 140);
    }
    if (bk_wdown) {   /* a step held: KNOB 1..4 are its SLICE, LEVEL, RETRIG, PITCH */
        static const char *const LAB[4] = {"slice", "level", "retrig", "pitch"};
        static const char *const LV[4] = {"norm", "ghost", "soft", "hard"};
        static const char *const RT[5] = {"--", "x2", "x3", "x4", "x8"};
        static char v[4][12];
        const char *val[4] = {v[0], v[1], v[2], v[3]};
        int32_t ratio[4];
        uint32_t w = 0;
        const bstep_t *b;
        while (w < 15u && !((bk_wdown >> w) & 1u))
            w++;
        b = &t->bstep[(first + w) % NSTEP];
        if (b->slice)
            fmt_int(v[0], b->slice);
        else
            str_cpy(v[0], "gen", 4);
        str_cpy(v[1], LV[b->lvl & 3u], 8);
        str_cpy(v[2], RT[b->rat % 5u], 8);
        fmt_int(v[3], b->pitch);
        if (b->flags & BS_REV)
            str_cpy(v[0] + str_len(v[0]), "<", 2);
        ratio[0] = b->slice * 1000 / BRK_SLICES;
        ratio[1] = (int32_t)lvl_rank(b->lvl) * 333;
        ratio[2] = b->rat * 250;
        ratio[3] = (b->pitch + 24) * 1000 / 48;
        te_hint_txt = "WHITE: STEPS  BLACK: SLICES";
        te_dials(184, LAB, val, ratio, TRK_BRK + 1u, &foot);
        return;
    }
    {   /* KNOB 1..4 of the page */
        static char v[4][12], l[4][8];
        const char *lab[4], *val[4];
        int32_t ratio[4];
        for (i = 0; i < 4u; i++) {
            uint32_t id = BK_PAGE[bk_page % BK_NPAGE][i];
            const param_desc_t *d;
            const char *unit = "";
            int32_t pv;
            if (id == 0xFFu) {
                l[i][0] = v[i][0] = 0;
                lab[i] = l[i];
                val[i] = v[i];
                ratio[i] = 0;
                continue;
            }
            d = track_desc(t, id);
            pv = q_val(TRK_BRK, id);                    /* (with Q: what waits for the bar, so a turn shows) */
            if (bk_is_loop(id)) {                       /* AMEN 2, BR1 .. */
                uint32_t n = brkp_nloops();
                if ((uint32_t)pv < n)
                    brkp_loop_name((uint32_t)pv, v[i]);
                else
                    str_cpy(v[i], "--", 3);
                ratio[i] = n > 1u ? pv * 1000 / (int32_t)(n - 1u) : 0;
                if (ratio[i] > 1000)
                    ratio[i] = 1000;
            } else {
                param_format(d, pv, v[i], &unit);
                ratio[i] = d->max > d->min ? (pv - d->min) * 1000 / (d->max - d->min) : 0;
            }
            te_lower(l[i], id == P_DIST ? "DRIVE" : id == P_TFLT ? "FILT" : d->label, sizeof l[i]);
            if (id == P_GLMODE && pv) {                 /* LAYER: its bank's letter and loop's name ("bFUNKY", as the info
                                                         * line), cut to the dial's width */
                uint32_t n;
                v[i][0] = (char)('a' + (pv & 3));
                brkp_loop_name(brkp_bank_loop((uint32_t)pv), v[i] + 1);
                for (n = str_len(v[i]); n > 2u && text_w(&FONT_S, v[i]) > 55; n--)
                    v[i][n - 1u] = 0;
            }
            if (pv != t->p[id]) {                       /* waiting: "@AMEN B" (as LIVE's "@3 DARK REESE") */
                char w[12];
                w[0] = '@';
                str_cpy(w + 1, v[i], sizeof w - 1u);
                str_cpy(v[i], w, sizeof v[i]);
            }
            lab[i] = l[i];
            val[i] = v[i];
        }
        te_hint_txt = "WHITE: STEPS  BLACK: SLICES";
        te_dials(184, lab, val, ratio, TRK_BRK + 1u, &foot);
    }
}

static void bk_page_set(uint32_t pg)
{
    bk_page = (uint8_t)pg;
    ui.msg_t = 0;
    ui.force = 1;
}
/* the button taps of the screen (ui_input.c layer_tap): the pages (SEQ again: STYLE <-> GROOVE, EDIT again: LOOPS <->
 * BARS) */
static int brk_layer_tap(uint32_t layer)
{
    uint32_t pg;
    switch (layer) {
    case LY_STEP: pg = bk_page == BK_STYLE ? BK_GROOVE : BK_STYLE; break;
    case LY_ROLL: pg = BK_RETRIG; break;
    case LY_ERASE: pg = bk_page == BK_LOOPS ? BK_BARS : bk_page == BK_BARS ? BK_LAYER : BK_LOOPS; break;
    case LY_MIX: pg = BK_MIX; break;
    case LY_SCALE: pg = BK_PHRASE; break;
    default: return 0;
    }
    bk_page_set(pg);
    return 1;
}
/* the screen opened by a page button (from TRACKS or another page): that button's first page */
static void brk_layer_open(uint32_t layer)
{
    if (layer == LY_STEP)
        bk_page_set(BK_STYLE);
    else if (layer == LY_ERASE)
        bk_page_set(BK_LOOPS);
    else
        brk_layer_tap(layer);
}

/* a key of the screen (ui_input.c layers_input: the queue of seq.c key_down / key_up): k = the key index */
static void brk_key(uint32_t k, uint32_t down)
{
    track_t *t = TBRK;
    uint32_t i, first = bk_first();
    int white = -1, black = -1;
    for (i = 0; i < 16u; i++)
        if (key_of_white(i) == k)
            white = (int)i;
    for (i = 0; i < 11u; i++)
        if (BK_BLACK[i] == (int8_t)k)
            black = (int)i;
    if (white >= 0) {
        uint32_t bit = 1u << white, idx = first + (uint32_t)white;
        if (down) {
            bk_wdown |= (uint16_t)bit;
        } else if (bk_wdown & bit) {
            if (!(bk_wused & bit) && idx < trk_len(t)) {
                undo_mark(t, (undo_sess += 4u) | 3u);
                t->bstep[idx].on = (uint8_t)!t->bstep[idx].on;   /* a tap: the step plays / does not */
                t->seq_active = 1;
            }
            bk_wdown &= (uint16_t)~bit;
            bk_wused &= (uint16_t)~bit;
        }
        ui.force = 1;
        return;
    }
    if (black < 0)
        return;
    if (down && black == 8 && bk_wdown) {               /* a step held + REV: it plays backwards (again: forwards) */
        undo_mark(t, (undo_sess += 4u) | 3u);
        for (i = 0; i < 16u; i++)
            if ((bk_wdown >> i) & 1u && first + i < trk_len(t)) {
                bstep_t *b = &t->bstep[first + i];
                b->flags ^= BS_REV;
                b->on = 1;
                bk_wused |= (uint16_t)(1u << i);
            }
        ui.force = 1;
        return;
    }
    if (down && black < 8 && bk_wdown) {                /* a step held: its own slice (the same again: back to the generator) */
        undo_mark(t, (undo_sess += 4u) | 3u);
        for (i = 0; i < 16u; i++)
            if ((bk_wdown >> i) & 1u && first + i < trk_len(t)) {
                bstep_t *b = &t->bstep[first + i];
                b->slice = b->slice == (uint8_t)(black + 1) ? 0u : (uint8_t)(black + 1);
                b->on = 1;
                bk_wused |= (uint16_t)(1u << i);
            }
        ui.force = 1;
        return;
    }
    if (down)
        bk_live |= (uint16_t)(1u << black);
    else if (!((bk_live >> black) & 1u))
        return;                                         /* (its press was a pin) */
    else
        bk_live &= (uint16_t)~(1u << black);
    if (down && black < 8 && ((song.rec >> TRK_BRK) & 1u) && song.playing) {
        fm1_irq_off();                                  /* REC: the slice into the step it falls on */
        rec_hit(t, (uint32_t)black, LV_NORM, 0);
        fm1_irq_on();
    }
    brkp_key((uint32_t)black, down);
}

static void break_leave(void)
{
    bp.keys = 0;
    bk_live = bk_wdown = bk_wused = 0;
    go_home();
}

static void break_screen_input(uint32_t pressed, uint32_t home)
{
    track_t *t = TBRK;
    uint32_t k;
    int32_t s;
    if (home == BT_TAP) {
        break_leave();
        return;
    }
    if (pressed & (1u << panel.btn[B_PLAY]))
        studio_play();                                  /* (ui_input.c: the loop) */
    if (pressed & (1u << panel.btn[B_ENV]))             /* ENV: TUNE */
        bk_page_set(BK_TUNE);
    if (pressed & (1u << panel.btn[B_LFO]))             /* LFO: PATTERN */
        bk_page_set(BK_PATTERN);
    if (pressed & (1u << panel.btn[B_OCTDN])) {
        t->p[P_MUTE] = !t->p[P_MUTE];
        ui_message(t->p[P_MUTE] ? "BREAK MUTED" : "BREAK ON");
    }
    if (pressed & (1u << panel.btn[B_OCTUP])) {         /* the next 16 steps (stopped; playing: the playhead's) */
        uint32_t pages = (trk_len(t) + 15u) / 16u;
        bk_spage = (uint8_t)((bk_spage + 1u) % (pages ? pages : 1u));
        ui.force = 1;
    }
    if ((s = panel_enc(EN_ALGO)) != 0 && !ft_on && s > 0) {   /* ALGORITHM right: the next lane down (BASS), on the
                                                                * DECK; left: the BREAK is the first, nothing */
        track_step(s);
        break_leave();
        return;
    }
    if ((s = panel_enc(EN_SELECT)) != 0)
        bk_page_set((uint32_t)clamp((int32_t)bk_page + (s > 0 ? 1 : -1), 0, BK_NPAGE - 1));
    if ((s = panel_enc(EN_PRESET)) != 0) {                /* PRESETS: loop A */
        uint32_t n = brkp_nloops();
        t->p[P_LRATE] = (int16_t)clamp(t->p[P_LRATE] + s, 0, n ? (int32_t)n - 1 : 0);
        song_keep_p(TRK_BRK, P_LRATE, t->p[P_LRATE]);   /* (song mode: the next section keeps it) */
        ui.hot_col = 0;
        ui.hot_t = 40;
        bk_page = BK_LOOPS;
        ui.force = 1;
    }
    if (bk_wdown) {                                     /* a step held: KNOB 1..4 edit it (and every other held one) */
        uint32_t first = bk_first(), i;
        for (k = 0; k < 4u; k++) {
            if ((s = panel_enc(EN_K1 + k)) == 0)
                continue;
            ui.hot_col = (uint8_t)k;
            ui.hot_t = 40;
            undo_mark(t, (undo_sess += 4u) | 3u);
            for (i = 0; i < 16u; i++) {
                bstep_t *b;
                if (!((bk_wdown >> i) & 1u) || first + i >= trk_len(t))
                    continue;
                b = &t->bstep[first + i];
                b->on = 1;
                if (k == 0u) {
                    b->slice = (uint8_t)clamp((int32_t)b->slice + s, 0, BRK_SLICES);
                    if (b->slice && !song.playing)
                        audition_slice(b->slice - 1u, b->lvl);
                } else if (k == 1u) {
                    b->lvl = LV_UP[clamp((int32_t)lvl_rank(b->lvl) + s, 0, 3)];
                } else if (k == 2u) {
                    b->rat = (uint8_t)clamp((int32_t)b->rat + s, 0, 4);
                } else {
                    b->pitch = (int8_t)clamp(b->pitch + s, -24, 24);
                }
                bk_wused |= (uint16_t)(1u << i);
            }
            ui.force = 1;
        }
        return;
    }
    for (k = 0; k < 4u; k++) {
        uint32_t id = BK_PAGE[bk_page % BK_NPAGE][k];
        const param_desc_t *d;
        if ((s = panel_enc(EN_K1 + k)) == 0)
            continue;
        ui.hot_col = (uint8_t)k;
        ui.hot_t = 40;
        if (id == 0xFFu)
            continue;
        if (id == P_PRIO) {                             /* STYLE: its parameters (with Q: on the boundary, q.c) */
            int32_t st = clamp(q_val(TRK_BRK, P_PRIO) + s, 0, BS_NSTYLE - 1);
            if (st != q_val(TRK_BRK, P_PRIO)) {
                static int16_t p[P_COUNT];
                uint32_t j;
                for (j = 0; j < P_COUNT; j++)
                    p[j] = q_val(TRK_BRK, j);
                brk_style_p(p, (uint32_t)st);
                q_params(TRK_BRK, p);
                if (q_on())
                    q_name(TRK_BRK, N_BP_STYLE[st]);
                ui_say("STYLE ", N_BP_STYLE[st]);
                ui.force = 1;
            }
            continue;
        }
        d = track_desc(t, id);
        if (bk_is_loop(id)) {                           /* only the loops there are (with Q: on the boundary) */
            uint32_t n = brkp_nloops();
            q_param(TRK_BRK, id, clamp(q_val(TRK_BRK, id) + s, 0, n ? (int32_t)n - 1 : 0));
            if (q_on())
                q_name(TRK_BRK, "LOOP");
        } else {
            int32_t was = t->p[id];
            t->p[id] = (int16_t)clamp(t->p[id] + accel(EN_K1 + k, s, d->max - d->min), d->min, d->max);
            song_keep_turn(TRK_BRK, id, t->p[id] - was, d->min, d->max);   /* (song mode: the next section keeps it) */
        }
        if (id == P_SLEN && bk_spage * 16u >= trk_len(t))
            bk_spage = 0;
    }
}

/* the keys' lights: the steps that play (the playhead blinks), the slices playing, the live keys */
static uint32_t brk_keys_lit(void)
{
    const track_t *t = TBRK;
    uint32_t m = 0, i, blink = (fm1_ms / 125u) & 1u, first = bk_first(), len = trk_len(t);
    int step = bk_step_now();
    for (i = 0; i < 16u; i++) {
        uint32_t idx = first + i, on = idx < len && t->bstep[idx % NSTEP].on;
        if (step >= 0 && (uint32_t)step == idx)
            on = !on || blink;
        if (on)
            m |= 1u << key_of_white(i);
    }
    for (i = 0; i < 11u; i++)
        if (((bp.keys >> i) & 1u) || (i < 8u && bp.running && bp.st_slice == (int8_t)i))
            m |= 1u << BK_BLACK[i];
    return m | fm1_in.notes;
}
