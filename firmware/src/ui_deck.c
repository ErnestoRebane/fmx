/* SPDX-License-Identifier: GPL-3.0-only */
/* The DECK (FMX): the home screen, for playing live (the UI mockup, 2026-10). Four lanes, the BREAK first, each a
 * PS1 window with the track's gem, its name and what it does now:
 *   BREAK   its style (or MANGLE's last trick, yellow), the loop playing (A..D and its name), the 8 slices, the one
 *           playing lit
 *   BASS    the phrase's rhythm and CHANGE, the note sounding and the sound, the two bars of the phrase (a note's
 *           start lit, its length dim, the step playing white)
 *   CHORDS  the FEEL, the chord now and the next, the chords' lengths (FEEL), the one playing lit
 *   CHOP    the kit, the sound, its 16 pads (the ones sounding lit)
 * A silent track (MUTE, another soloed) greys out and says MUTE; the selected track's window has its colour, the one
 * armed for REC red, three thick and edged white (the others' edges dim). KNOB 1..4 are macros: FILTER (the master's DJ filter), MANGLE (the BREAK's), BASS (the phrase's
 * MOVE: written again), SPACE (the SPU reverb's input). ALGORITHM picks the track, top down as listed (its keys,
 * PRESETS, SEL: its screen), HOME: LIVE (the visualiser is LIVE's: ENV there). */
static uint32_t steps_hash(const track_t *t);          /* ui_draw.c */
#define DK_Y0 40                                        /* the lanes: from under the header .. */
#define DK_LH 36                                        /* .. 36 rows each (a window of 33 and the ground) */
#define DK_EDGE RGB(64, 72, 140)                        /* a lane not selected: its edge dim (the selected one: bright) */
static const uint8_t DK_ORDER[NTRK] = {TRK_BRK, 0, 1, 2};   /* the lanes, top down: the BREAK first */

/* the track's gem: a diamond, lit (its colour) or grey */
static void dk_gem(int32_t x, int32_t y, uint16_t c, int lit)
{
    uint16_t a = lit ? c : RGB(138, 138, 154), d = lit ? mix565_ps(c, C_BLACK, 90) : RGB(90, 90, 106);
    p3_tri(x + 6, y, x + 12, y + 8, x + 6, y + 21, d, 255);
    p3_tri(x + 6, y, x, y + 8, x + 6, y + 21, a, 255);
    p3_tri(x + 6, y, x + 12, y + 8, x, y + 8, mix565_ps(a, C_WHITE, 120), 255);
}

/* the lane of track k: what it shows, and its signature (a redraw when it changes) */
static uint32_t dk_lane(uint32_t k, int draw)
{
    const track_t *t = &trk[k];
    uint32_t sig = k * 977u, silent = trk_silent(t), sel = song.sel == k, i;
    uint32_t armed = sel && (song.rec || rec_wait), solo = (song.solo >> k) & 1u;
    uint16_t col = TRK_COL[k], fg = silent ? RGB(150, 150, 170) : PS_TXT;
    char l1[20], st[16], l2[24];
    uint32_t cells[32], ncell = 0;
    l1[0] = st[0] = l2[0] = 0;                         /* (the name alone: no number, 2026-10-28) */
    if (is_brk(t)) {
        uint32_t bank = song.playing ? bp_bank() : 0u, cur = (uint32_t)bp.current_slice;
        str_cpy(l1, "BREAK", 8);
        if (bp.st_mangle && song.playing) {
            static const char *const MG[6] = {"", "stutter", "reverse", "jump", "dive", "gap"};
            str_cpy(st, MG[bp.st_mangle % 6u], sizeof st);
            sig += 3u;
        } else {
            te_lower(st, N_BP_STYLE[(uint32_t)BPV(BP_STYLE) % BS_NSTYLE], sizeof st);
        }
        str_cpy(l2, "LOOP ", sizeof l2);
        l2[5] = (char)('A' + bank);
        l2[6] = ' ';
        brkp_loop_name(brkp_bank_loop(bank), l2 + 7);
        for (i = 0; i < BRK_SLICES; i++)
            cells[ncell++] = song.playing && i == cur ? 2u : 1u;
        sig += bank * 31u + (song.playing ? cur * 131u : 0u) + bp.st_mangle * 7u;
    } else if (k == 0u) {
        uint32_t n = t->seq_n ? t->seq_notes[0] : bl_note(0), idx = song.playing ? t->seq_idx : 0xFFFFu, len = trk_len(t);
        str_cpy(l1, "BASS", 8);
        te_lower(st, N_BL_RHYTHM[bl.rhythm % BR_N], sizeof st);
        if (bl.change) {
            str_cpy(st + str_len(st), " ", 2);
            fmt_int(st + str_len(st), bl.change);
            str_cpy(st + str_len(st), "%", 2);
        }
        note_name(l2, n);
        str_cpy(l2 + str_len(l2), " ", 2);
        str_cpy(l2 + str_len(l2), ENGINES[t->eng_req % NENGINES]->presets[t->preset % ENGINES[t->eng_req % NENGINES]->npresets].name,
                sizeof l2 - str_len(l2));
        for (i = 0; i < 32u; i++) {
            const step_t *s = &t->step[i];
            cells[ncell++] = i >= len ? 0u : i == idx ? 3u : step_on(s) ? 2u : s->time == ST_TIE ? 1u : 0u;
        }
        sig += n * 7u + (idx & 63u) * 131u + steps_hash(t) + bl.change * 13u + bl.rhythm;
    } else if (k == 1u) {
        int now = cgr.now;
        uint32_t d[CG_MAX], nn = (uint32_t)clamp(cg.num, 2, CG_MAX), cyc = cg_durs(d);
        str_cpy(l1, "CHORDS", 8);
        te_lower(st, cg.play == CP_OFF ? "off" : N_CG_FEEL[cg_feel()], sizeof st);
        cg_name(now < 0 ? 0u : (uint32_t)now, l2);
        if (now >= 0) {
            str_cpy(l2 + str_len(l2), " > ", 4);
            cg_name(((uint32_t)now + 1u) % nn, l2 + str_len(l2));
        }
        for (i = 0; i < nn; i++)                       /* (the lengths: drawn apart, below) */
            cells[ncell++] = d[i] * 1000u / cyc << 2 | ((int)i == now ? 2u : 1u);
        sig += (uint32_t)(now + 1) * 31u + cgr.gen * 7u + cg.rate * 977u + cg.play * 5u;
    } else {
        uint32_t base, n = 0, kit = ENGINES[t->eng_req % NENGINES] == &ENG_SAMPLE && smp_kit((uint32_t)t->p[P_E0] % SMP_NALL, &base, &n);
        str_cpy(l1, "CHOP", 8);
        te_lower(st, kit ? SMP_ALL_NAMES[(uint32_t)t->p[P_E0] % SMP_NALL] : "", sizeof st);
        str_cpy(l2, ENGINES[t->eng_req % NENGINES]->presets[t->preset % ENGINES[t->eng_req % NENGINES]->npresets].name, sizeof l2);
        for (i = 0; i < 16u; i++) {
            uint32_t v, on = 0;
            for (v = 0; kit && v < NVOICE; v++)
                on |= t->v[v].active && t->v[v].stage <= 3u && t->v[v].note == base + i;
            cells[ncell++] = i >= n ? 0u : on ? 2u : 1u;
        }
        sig += kit * 3u + n;
    }
    if (silent)
        str_cpy(l2, "MUTE", sizeof l2);
    else if (solo)
        str_cpy(st, "solo", sizeof st);
    if (((qq.what_set >> k) & 1u) && song.playing && q_waits()) {   /* Q: what waits on this track (q.c), yellow */
        st[0] = '@';
        fmt_int(st + 1, (int32_t)q_bar_at());
        st[2] = ' ';
        str_cpy(st + 3, qq.what[k], 9);
        sig += qq.gen * 7u + 1u;
    }
    sig = studio_hash(studio_hash(studio_hash(sig + silent * 4999u + sel * 7919u + armed * 104729u, l1), st), l2);
    for (i = 0; i < ncell; i++)
        sig = sig * 31u + cells[i];
    if (!draw)
        return sig;
    cv_begin(240, DK_LH, C_BLACK);
    ps_ground(DK_Y0 + (int32_t)(k == TRK_BRK ? 0u : k + 1u) * DK_LH);
    ps_win(3, 1, 234, 33, armed ? PS_RED : sel ? C_WHITE : DK_EDGE);
    if (sel) {                                          /* the selected one: white, then two rows of its colour, lit */
        uint16_t c = armed ? PS_RED : mix565_ps(col, C_WHITE, 90);
        cv_rect(4, 2, 232, 2, c);
        cv_rect(4, 30, 232, 2, c);
        cv_rect(4, 2, 2, 30, c);
        cv_rect(234, 2, 2, 30, c);
    }
    dk_gem(9, 7, col, !silent);
    ps_text(28, 2, &FONT_S, l1, silent ? RGB(110, 110, 130) : PS_LBL);
    ps_text(28, 17, &FONT_S, l2, fg);
    ps_text(232 - text_w(&FONT_S, st), 2, &FONT_S, st, st[0] == '@' || (bp.st_mangle && is_brk(t) && song.playing) ? PS_YEL : PS_LBL);
    if (is_brk(t)) {                                    /* 8 slices */
        for (i = 0; i < ncell; i++)
            cv_rect(144 + (int32_t)i * 11, 22, 10, 8, cells[i] == 2u ? col : mix565_ps(col, C_BLACK, 170));
    } else if (k == 0u) {                               /* the phrase: 32 steps */
        for (i = 0; i < ncell; i++) {
            uint16_t c = cells[i] == 3u ? C_WHITE : cells[i] == 2u ? col : cells[i] == 1u ? mix565_ps(col, C_BLACK, 140)
                                                                                        : RGB(34, 48, 110);
            cv_rect(136 + (int32_t)i * 3, cells[i] == 2u || cells[i] == 3u ? 22 : 25, 2, cells[i] == 2u || cells[i] == 3u ? 8 : 5, c);
        }
    } else if (k == 1u) {                               /* the chords' lengths */
        int32_t x = 138;
        for (i = 0; i < ncell; i++) {
            int32_t w = (int32_t)((cells[i] >> 2) * 94u / 1000u);
            cv_rect(x, 24, w - 2 > 1 ? w - 2 : 1, 6, (cells[i] & 3u) == 2u ? col : mix565_ps(col, C_BLACK, 150));
            x += w;
        }
    } else {                                            /* the pads: 8 x 2 */
        for (i = 0; i < ncell; i++)
            cv_rect(144 + (int32_t)(i % 8u) * 11, 20 + (int32_t)(i / 8u) * 6, 10, 5,
                    cells[i] == 2u ? col : cells[i] == 1u ? mix565_ps(col, C_BLACK, 170) : RGB(34, 38, 70));
    }
    cv_blit(0, (uint32_t)(DK_Y0 + (int32_t)(k == TRK_BRK ? 0u : k + 1u) * DK_LH));
    return sig;
}

/* the DECK's knobs: what they show */
static void dk_knobs(char v[4][12], int32_t ratio[4])
{
    int32_t f = song.g[G_FILT];
    if (!f)
        str_cpy(v[0], "open", 12);
    else {
        str_cpy(v[0], f < 0 ? "lp " : "hp ", 12);
        fmt_int(v[0] + 3, f < 0 ? -f : f);
    }
    fmt_int(v[1], BPV(BP_MANGLE));
    fmt_int(v[2], bl.move);
    fmt_int(v[3], song.g[G_SPACE]);
    ratio[0] = (f + 64) * 1000 / 127;
    ratio[1] = BPV(BP_MANGLE) * 10;
    ratio[2] = bl.move * 10;
    ratio[3] = song.g[G_SPACE] * 1000 / 127;
}

static void deck_draw(void)
{
    static uint32_t head, lane[NTRK], foot;
    uint32_t i;
    te_header("deck", PS_TXT, &head);
    for (i = 0; i < NTRK; i++) {
        uint32_t k = DK_ORDER[i], sig = dk_lane(k, 0);
        if (ui.force || sig != lane[k])
            lane[k] = dk_lane(k, 1);
    }
    {
        static const char *const LAB[4] = {"filter", "mangle", "bass", "space"};
        static char v[4][12];
        const char *val[4] = {v[0], v[1], v[2], v[3]};
        int32_t ratio[4];
        dk_knobs(v, ratio);
        te_hint_txt = "SEL: OPEN IT  HOME: LIVE";
        te_dials(184, LAB, val, ratio, song.sel, &foot);
    }
}

/* KNOB 1..4 on the DECK (ui_input.c page_knob): the macros */
static void deck_edit(uint32_t slot, int32_t steps)
{
    int16_t *m = &trk[TRK_BRK].p[BPP[BP_MANGLE]];
    switch (slot) {
    case 0:                                             /* FILTER: the master's (left a low-pass, right a high-pass) */
        song.g[G_FILT] = (int16_t)clamp(song.g[G_FILT] + accel(EN_K1, steps, 127), -64, 63);
        break;
    case 1:                                             /* MANGLE: the BREAK's */
        *m = (int16_t)clamp(*m + accel(EN_K2, steps, 100), 0, 100);
        break;
    case 2:                                             /* BASS: the phrase's MOVE, written again (its own steps only) */
        if (!bl_on()) {
            ui_message("BASS: ITS OWN STEPS");
            return;
        }
        bl.move = (uint8_t)clamp(bl.move + accel(EN_K3, steps, 100), 0, 100);
        bl_write();
        break;
    default:                                            /* SPACE: the SPU reverb's input */
        song.g[G_SPACE] = (int16_t)clamp(song.g[G_SPACE] + accel(EN_K4, steps, 127), 0, 127);
        break;
    }
}
