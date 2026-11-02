/* SPDX-License-Identifier: GPL-3.0-only */
/* The CHORDS screen (FMX): the CHORDS track's generator (chordgen.c), 240 x 240. The progression as bubbles on a
 * ring, in the order they play (clockwise from the top: the loop is the ring), joined by lines, breathing a little; the
 * one playing lit, a pinned one marked; under it their degrees (i - VI - III - VII); four dials.
 *   reached  track 2 (CHORDS) selected: SEL from TRACKS or any of its pages; HOME or ALGORITHM goes back
 *   black keys  1-8: the nodes; held, that chord plays (the last pressed wins), over the progression
 *   a black key held + KNOB 1 (VARIANT): the node is pinned, the others change; + OCT-: unpinned
 *   white keys  the first 16 steps of the track (PLAY STEPS: a step that plays strikes the chord of the moment)
 *   SEL  GEN (VARIANT, CHORDS, COLOR, MOOD), again PLAY (PLAY, RATE, FEEL, REG); SELECT the same two. BASS FOLLOW is
 *        on the BASS screen. The ring is the loop in time: a node where its chord starts (FEEL: not evenly)
 *   PRESETS the sound (the pads, keys and organs), PLAY the transport; the other buttons: the track's own pages */
#define CH_COL RGB(64, 220, 200)                        /* teal: the CHORDS screen */
#define CH_DIM RGB(18, 70, 66)
enum { CH_GEN, CH_PLAY, CH_NPAGE };
static uint8_t ch_page;
static uint16_t ch_wdown;                               /* the white keys down (steps) */

static int on_chord_page(void) { return !ui.home && cur_page()->scope == SC_CHORD && song.sel == 1u; }

/* the roman numeral of node i: its degree, upper case major, lower minor, ° diminished, b borrowed */
static void ch_roman(uint32_t i, char *b)
{
    static const char *const R[7] = {"I", "II", "III", "IV", "V", "VI", "VII"};
    uint8_t deg[7];
    uint32_t code = cgr.prog[i % CG_MAX], q = (code >> 4) & 3u, root = code & 15u, d, k, in = 0;
    cg_scale(cg_mask(), deg);
    for (k = 0; k < 7u; k++)
        in |= deg[k] == root;
    d = cg_degree(deg, root);
    b[0] = 0;
    if (!in)
        str_cpy(b, "b", 2);
    str_cpy(b + str_len(b), R[d], 4);
    if (q == CQ_MIN || q == CQ_DIM)
        for (k = 0; b[k]; k++)
            if (b[k] >= 'A' && b[k] <= 'Z')
                b[k] = (char)(b[k] - 'A' + 'a');
    if (q == CQ_DIM)
        str_cpy(b + str_len(b), "o", 2);
}

static void chord_screen_draw(void)
{
    static uint32_t head, info_sig, foot, last_ms;
    uint32_t i, n = (uint32_t)clamp(cg.num, 2, CG_MAX), live = cgr.live;
    int now = cgr.now;
    te_header(ch_page == CH_GEN ? "chords" : "play", CH_COL, &head);
    if (ui.force || fm1_ms - last_ms >= 33u) {         /* the graph: ~30 frames a second (it breathes) */
        int32_t px[CG_MAX], py[CG_MAX];
        uint32_t t = fm1_ms, d[CG_MAX], cyc = cg_durs(d), at = 0;
        last_ms = fm1_ms;
        cv_begin(240, 120, C_BLACK);
        for (i = 0; i < 24u; i++) {                    /* the ring, faint */
            uint32_t ph = i * 0x0AAAAAABu;
            cv_rect(120 + (sine_i(ph) * 84 >> 15), 58 - (sine_i(ph + 0x40000000u) * 42 >> 15), 1, 1, TE_G2);
        }
        for (i = 0; i < n; i++) {                       /* where the nodes go: round the ring where they start, a breath */
            uint32_t ph = (uint32_t)(((uint64_t)at << 32) / cyc);
            at += d[i];
            px[i] = 120 + (sine_i(ph) * 84 >> 15) + (sine_i((t << 18) + i * 788529152u) * 3 >> 15);
            py[i] = 58 - (sine_i(ph + 0x40000000u) * 42 >> 15) + (sine_i(t * 200704u + i * 1358954496u) * 3 >> 15);
        }
        for (i = 0; i < n; i++) {                       /* the lines, in the order they play; the last one home, dim */
            uint32_t j = (i + 1u) % n;
            uint16_t c = j ? CH_DIM : TE_G2;
            cv_line(px[i], py[i], px[j], py[j], c);
            cv_line(px[i], py[i] + 1, px[j], py[j] + 1, c);
        }
        for (i = 0; i < n; i++) {                       /* the bubbles */
            char nm[8];
            int32_t w, x, y;
            uint32_t lit = (int)i == now, held = (live >> i) & 1u;
            cg_name(i, nm);
            w = text_w(&FONT_S, nm) + 10;
            x = px[i] - w / 2;
            y = py[i] - 9;
            cv_rect(x, y, w, 18, held ? C_WHITE : lit ? CH_COL : TE_G1);
            if (!held && !lit) {                        /* an outline */
                cv_rect(x, y, w, 1, CH_DIM);
                cv_rect(x, y + 17, w, 1, CH_DIM);
                cv_rect(x, y, 1, 18, CH_DIM);
                cv_rect(x + w - 1, y, 1, 18, CH_DIM);
            }
            cv_text(x + 5, y + 1, &FONT_S, nm, held || lit ? C_BLACK : TE_G4);
            if ((cg.pins >> i) & 1u)
                cv_rect(x + 1, y + 1, 3, 3, held || lit ? C_BLACK : C_WHITE);   /* pinned */
        }
        cv_blit(0, 40);
    }
    {   /* the degrees, the key, what plays */
        uint32_t sig = cgr.gen * 31u + (uint32_t)(now + 1) * 7u + n * 3u + cg.play * 101u + cg_mask() * 13u + cg_root() * 977u + cg.rate * 7919u;
        if (ui.force || sig != info_sig) {
            char b[48], r[8];
            int32_t x = 4;
            info_sig = sig;
            cv_begin(240, 24, C_BLACK);
            for (i = 0; i < n && x < 236; i++) {
                ch_roman(i, r);
                x = cv_text(x, 4, &FONT_S, r, (int)i == now ? CH_COL : TE_G3);
                if (i + 1u < n)
                    x = cv_text(x + 2, 4, &FONT_S, "-", TE_G2) + 2;
            }
            if (cg.play == CP_OFF)
                str_cpy(b, "off", sizeof b);
            else
                te_lower(b, N_CG_FEEL[cg_feel()], sizeof b);   /* (how the time is shared out) */
            if (x + 8 < 236 - text_w(&FONT_S, b))
                cv_text(236 - text_w(&FONT_S, b), 4, &FONT_S, b, TE_G3);
            cv_blit(0, 160);
        }
    }
    {   /* KNOB 1..4: what they set (with Q: what waits, q.c) */
        const chord_cfg_t *dv = qq.cg_set ? &qq.cg : &cg;
        static char v[4][12];
        const char *val[4] = {v[0], v[1], v[2], v[3]};
        int32_t ratio[4];
        if (ch_page == CH_GEN) {
            static const char *const LAB[4] = {"variant", "chords", "color", "mood"};
            fmt_int(v[0], dv->var);
            fmt_int(v[1], dv->num);
            te_lower(v[2], N_CG_COLOR[dv->color % CC_N], 12);
            fmt_int(v[3], dv->mood);
            ratio[0] = dv->var;
            ratio[1] = ((int32_t)dv->num - 2) * 1000 / 6;
            ratio[2] = dv->color * 200;
            ratio[3] = dv->mood * 10;
            te_hint_txt = "BLACK: CHORDS  WHITE: STEPS";
            te_dials(184, LAB, val, ratio, 2u + dv->pins * 11u, &foot);
        } else {
            static const char *const LAB[4] = {"play", "rate", "feel", "reg"};
            te_lower(v[0], N_CG_PLAY[dv->play % CP_N], 12);
            te_lower(v[1], N_CG_RATE[(dv->rate & 3u)], 12);
            te_lower(v[2], N_CG_FEEL[((uint32_t)(dv->rate >> 2) % CF_N)], 12);
            fmt_int(v[3], dv->reg);
            ratio[0] = dv->play * 500;
            ratio[1] = (int32_t)(dv->rate & 3u) * 333;
            ratio[2] = (int32_t)((uint32_t)(dv->rate >> 2) % CF_N) * 333;
            ratio[3] = (dv->reg + 2) * 250;
            te_hint_txt = "BLACK: CHORDS  WHITE: STEPS";
            te_dials(184, LAB, val, ratio, 102u, &foot);
        }
    }
}

static void ch_page_set(uint32_t pg)
{
    ch_page = (uint8_t)(pg % CH_NPAGE);
    ui.msg_t = 0;
    ui.force = 1;
}
/* SEL on the screen: GEN <-> PLAY (ui_input.c layer_tap); the other buttons: the track's own pages */
static int chd_layer_tap(uint32_t layer)
{
    if (layer != LY_SCALE)
        return 0;
    ch_page_set(ch_page == CH_GEN ? CH_PLAY : CH_GEN);
    return 1;
}

/* a key of the screen (ui_input.c layers_input, the queue of seq.c): k = the key index */
static void chd_key(uint32_t k, uint32_t down)
{
    static const int8_t BLACK[11] = {1, 3, 5, 8, 10, 13, 15, 17, 20, 22, 25};
    track_t *t = &trk[1];
    uint32_t i;
    for (i = 0; i < 8u; i++)
        if (BLACK[i] == (int8_t)k) {                    /* a node: held, it plays */
            if (i < (uint32_t)cg.num) {
                if (down)
                    cgr.live |= (uint8_t)(1u << i);
                else
                    cgr.live &= (uint8_t)~(1u << i);
            }
            ui.force = 1;
            return;
        }
    for (i = 0; i < 16u; i++)
        if (key_of_white(i) == k) {                     /* a step: on (a note: the chord of the moment strikes) / off */
            uint32_t idx = i;                           /* (the first 16 steps: a rhythm for the chords) */
            if (!down) {
                ch_wdown &= (uint16_t)~(1u << i);
                return;
            }
            ch_wdown |= (uint16_t)(1u << i);
            if (idx >= trk_len(t))
                return;
            undo_mark(t, (undo_sess += 4u) | 3u);
            fm1_irq_off();
            if (step_on(&t->step[idx])) {
                step_clear(&t->step[idx]);
            } else {
                memset(&t->step[idx], 0, sizeof t->step[idx]);
                t->step[idx].note[0] = 60;
                t->step[idx].n = 1;
                t->step[idx].time = ST_NOTE;
                t->step[idx].vel = 100;
                t->seq_active = 1;
            }
            fm1_irq_on();
            ui.force = 1;
            return;
        }
}

static void chord_leave(void)
{
    cgr.live = 0;
    ch_wdown = 0;
    go_home();
}

static void chord_screen_input(uint32_t pressed, uint32_t home)
{
    uint32_t k;
    int32_t s;
    if (home == BT_TAP) {
        chord_leave();
        return;
    }
    if (pressed & (1u << panel.btn[B_PLAY]))
        studio_play();                                  /* (ui_input.c: the loop) */
    if (cgr.live && (pressed & (1u << panel.btn[B_OCTDN]))) {   /* a node held + OCT-: unpinned */
        for (k = 0; k < CG_MAX; k++)
            if ((cgr.live >> k) & 1u)
                cg_pin(k, 0);
        ui_message("UNPINNED");
        ui.force = 1;
    }
    if ((s = panel_enc(EN_ALGO)) != 0 && !ft_on) {     /* ALGORITHM: another track, on TRACKS */
        track_step(s);
        chord_leave();
        return;
    }
    if ((s = panel_enc(EN_SELECT)) != 0)
        ch_page_set(s > 0 ? CH_PLAY : CH_GEN);
    if ((s = panel_enc(EN_PRESET)) != 0) {             /* PRESETS: the sound */
        uint32_t total, cur = preset_pos(&total);
        if (total)
            preset_go((uint32_t)(((int32_t)cur + s % (int32_t)total + (int32_t)total) % (int32_t)total));
        ui.force = 1;
    }
    for (k = 0; k < 4u; k++) {
        chord_cfg_t *c;
        if ((s = panel_enc(EN_K1 + k)) == 0)
            continue;
        ui.hot_col = (uint8_t)k;
        ui.hot_t = 40;
        if (ch_page == CH_GEN && k == 0u) {             /* (the nodes held are pinned first: on the live chords) */
            uint32_t j;
            for (j = 0; j < CG_MAX; j++)
                if ((cgr.live >> j) & 1u)
                    cg_pin(j, 1);
            if (cgr.live)
                ui_message("HELD CHORDS KEPT");         /* (say so: the knob did more than its value) */
        }
        c = q_cg();                                     /* (with Q: the settings that wait, q.c) */
        if (c != &cg) {
            c->pins = cg.pins;
            memcpy(c->code, cg.code, sizeof c->code);
        }
        if (ch_page == CH_GEN) {
            if (k == 0u) {                              /* VARIANT */
                c->var = (uint16_t)((c->var + 1000 + s) % 1000);
            } else if (k == 1u) {
                c->num = (uint8_t)clamp(c->num + s, 2, CG_MAX);
                c->pins &= (uint8_t)((1u << c->num) - 1u);
            } else if (k == 2u) {
                c->color = (uint8_t)clamp(c->color + s, 0, CC_N - 1);
            } else {
                c->mood = (uint8_t)clamp(c->mood + accel(EN_K4, s, 100), 0, 100);
            }
        } else {
            if (k == 0u)
                c->play = (uint8_t)clamp(c->play + s, 0, CP_N - 1);
            else if (k == 1u)
                c->rate = (uint8_t)((c->rate & ~3u) | (uint32_t)clamp((int32_t)(c->rate & 3u) + s, 0, 3));
            else if (k == 2u)
                c->rate = (uint8_t)((c->rate & 3u) | (uint32_t)clamp((int32_t)((c->rate >> 2) % CF_N) + s, 0, CF_N - 1) << 2);
            else
                c->reg = (int8_t)clamp(c->reg + s, -2, 2);
        }
        if (c != &cg) {
            char b[12];
            q_cg_done();
            str_cpy(b, "VAR ", sizeof b);
            fmt_int(b + 4, c->var);
            q_name(1, b);
        }
        ui.force = 1;
    }
}

/* the keys' lights: the nodes (the one playing, the ones held), the steps that play (the playhead blinks) */
static uint32_t chd_keys_lit(void)
{
    static const int8_t BLACK[8] = {1, 3, 5, 8, 10, 13, 15, 17};
    const track_t *t = &trk[1];
    uint32_t m = 0, i, blink = (fm1_ms / 125u) & 1u;
    for (i = 0; i < (uint32_t)cg.num && i < 8u; i++)
        if ((int)i == cgr.now || ((cgr.live >> i) & 1u))
            m |= 1u << BLACK[i];
    for (i = 0; i < 16u; i++) {
        uint32_t idx = i, on = idx < trk_len(t) && step_on(&t->step[idx]);
        if (song.playing && idx == t->seq_idx)
            on = !on || blink;
        if (on)
            m |= 1u << key_of_white(i);
    }
    return m | fm1_in.notes;
}
