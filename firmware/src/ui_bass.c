/* SPDX-License-Identifier: GPL-3.0-only */
/* The BASS screen (FMX): the BASS track's phrase generator (bassline.c), 240 x 240. The two bars as a roll, from the
 * track's own steps (a hand edit shows): a note a bar as high as it is above the root (the root's line, the octave's
 * fainter), as long as it is held, a slide drawn into the next; bar 1 the call, bar 2 the answer; the step playing
 * lit, the note the last change was at white; RHYTHM KICKS, playing: under it a mark where the bass struck (the BREAK's
 * kicks: the roll is then the line it follows). Then the key, LOCK or the chance, the rhythm; four dials.
 *   reached  track 1 (BASS) selected: SEL from TRACKS or any of its pages (a track with no notes gets a phrase);
 *            HOME or ALGORITHM goes back
 *   black keys  1: NEW (the next VARIANT), 2: ANSWER (another answer to the same call), 3: BACK (the phrase as it was
 *               written: the changes since gone)
 *   white keys  play the bass as ever
 *   SEL  PHRASE (VARIANT, RHYTHM, MOVE, CHANGE), again SHAPE (ANSWER, HOLD, OCT, BASS FOLLOW); SELECT the same two.
 *        Every one but CHANGE and FOLLOW writes the phrase again.
 *   PRESETS the sound (the basses), PLAY the transport */
#define BS_COL RGB(40, 124, 255)                        /* the BASS track's blue */
#define BS_DIM RGB(14, 40, 86)
enum { BS_PHRASE, BS_SHAPE, BS_NPAGE };
static uint8_t bs_page;

static int on_bass_page(void) { return !ui.home && cur_page()->scope == SC_BASS && song.sel == 0u; }

static void bass_screen_draw(void)
{
    static uint32_t head, graph_sig, info_sig, foot;
    const track_t *t = &trk[0];
    uint32_t len = trk_len(t) < BL_STEPS ? trk_len(t) : BL_STEPS, now = song.playing ? t->seq_idx : 0xFFFFu, i;
    int32_t root = (int32_t)bl_note(0), changed = blr.changed;
    uint32_t hits = blr.hits, live = (uint32_t)bl_live();
    uint32_t sig = steps_hash(t) + blr.gen * 31u + now * 7919u + (uint32_t)root * 131u + (uint32_t)(changed + 1) * 977u +
                   hits * 2654435761u + live;
    te_header(bs_page == BS_PHRASE ? "phrase" : "shape", BS_COL, &head);
    if (ui.force || sig != graph_sig) {
        graph_sig = sig;
        cv_begin(240, 120, C_BLACK);
        cv_text(8, 0, &FONT_S, "call", TE_G3);
        cv_text(128, 0, &FONT_S, "answer", TE_G3);
        cv_rect(8, 106, 224, 1, BS_DIM);               /* the root's line, the octave's */
        for (i = 8; i < 232u; i += 4u)
            cv_rect((int32_t)i, 106 - 12 * 3, 2, 1, BS_DIM);
        for (i = 0; i <= 32u; i += 4u)                  /* the beats; the bars apart */
            cv_rect(8 + (int32_t)i * 7 - (i == 32u), 18, 1, i % 16u ? 3 : 98, TE_G2);
        if (now < len)
            cv_rect(8 + (int32_t)now * 7, 22, 7, 94, TE_G1);   /* the step playing */
        for (i = 0; i < len; i++) {
            const step_t *s = &t->step[i];
            uint32_t e = i + 1u, lit;
            int32_t y, x;
            if (!step_on(s))
                continue;
            while (e < len && t->step[e].time == ST_TIE)
                e++;
            y = 104 - clamp((int32_t)s->note[0] - root, -10, 26) * 3;
            x = 8 + (int32_t)i * 7;
            lit = now >= i && now < e;
            cv_rect(x, y, (int32_t)(e - i) * 7 - 1, 5, (int)i == changed ? C_WHITE : lit ? RGB(140, 190, 255) : BS_COL);
            cv_rect(x, y, 2, 5, C_WHITE);                /* (where it starts) */
            if ((t->step[e - 1u].flags & SF_SLIDE) && e < len && step_on(&t->step[e])) {
                int32_t y2 = 104 - clamp((int32_t)t->step[e].note[0] - root, -10, 26) * 3;
                cv_line(8 + (int32_t)e * 7 - 2, y + 2, 8 + (int32_t)e * 7 + 1, y2 + 2, C_WHITE);   /* a slide */
            }
        }
        for (i = 0; live && i < len; i++)               /* KICKS, playing: where it struck (on the BREAK's kicks) */
            if ((hits >> i) & 1u)
                cv_rect(8 + (int32_t)i * 7 + 1, 113, 4, 4, C_WHITE);
        cv_blit(0, 40);
    }
    {   /* the key, LOCK or the chance, the rhythm */
        uint32_t s2 = (uint32_t)root * 7u + bl.change * 31u + bl.rhythm * 1009u + (uint32_t)bl_on() * 3u;
        if (ui.force || s2 != info_sig) {
            char b[16];
            info_sig = s2;
            cv_begin(240, 24, C_BLACK);
            note_name(b, (uint32_t)root);
            str_cpy(b + str_len(b), cg_minor(cg_mask()) ? " min" : " maj", 5);
            cv_text(4, 4, &FONT_S, b, TE_G4);
            if (!bl.change) {
                str_cpy(b, "lock", sizeof b);
            } else {
                fmt_int(b, bl.change);
                str_cpy(b + str_len(b), "% change", 9);
            }
            te_text_c(128, 4, b, !bl.change ? BS_COL : bl_on() ? TE_G3 : TE_G2);
            te_lower(b, N_BL_RHYTHM[bl.rhythm % BR_N], sizeof b);
            cv_text(236 - text_w(&FONT_S, b), 4, &FONT_S, b, TE_G4);
            cv_blit(0, 160);
        }
    }
    {   /* KNOB 1..4 */
        static char v[4][12];
        const char *val[4] = {v[0], v[1], v[2], v[3]};
        int32_t ratio[4];
        if (bs_page == BS_PHRASE) {
            static const char *const LAB[4] = {"variant", "rhythm", "move", "change"};
            fmt_int(v[0], bl.var);
            te_lower(v[1], N_BL_RHYTHM[bl.rhythm % BR_N], 12);
            fmt_int(v[2], bl.move);
            if (bl.change)
                fmt_int(v[3], bl.change);
            else
                str_cpy(v[3], "lock", 12);
            ratio[0] = bl.var;
            ratio[1] = bl.rhythm * 250;
            ratio[2] = bl.move * 10;
            ratio[3] = bl.change * 10;
            te_hint_txt = "BLACK 1 NEW  2 ANSWER  3 BACK";
            te_dials(184, LAB, val, ratio, 5u, &foot);
        } else {
            static const char *const LAB[4] = {"answer", "hold", "oct", "follow"};
            te_lower(v[0], N_BL_ANSWER[bl.answer % BA_N], 12);
            te_lower(v[1], N_BL_HOLD[bl.hold % BH_N], 12);
            fmt_int(v[2], bl_oct());
            str_cpy(v[3], cg.follow ? "on" : "off", 12);
            ratio[0] = bl.answer * 1000 / (BA_N - 1);
            ratio[1] = bl.hold * 500;
            ratio[2] = (bl_oct() + 1) * 333;
            ratio[3] = cg.follow * 1000;
            te_hint_txt = "BLACK 1 NEW  2 ANSWER  3 BACK";
            te_dials(184, LAB, val, ratio, 106u, &foot);
        }
    }
}

static void bs_page_set(uint32_t pg)
{
    bs_page = (uint8_t)(pg % BS_NPAGE);
    ui.msg_t = 0;
    ui.force = 1;
}
/* SEL on the screen: PHRASE <-> SHAPE (ui_input.c layer_tap); the other buttons: the track's own pages */
static int bss_layer_tap(uint32_t layer)
{
    if (layer != LY_SCALE)
        return 0;
    bs_page_set(bs_page == BS_PHRASE ? BS_SHAPE : BS_PHRASE);
    return 1;
}
/* the screen opens (ui_input.c layer_tap): a track with no notes gets a phrase */
static void bss_open(void)
{
    if (!bl_has_notes())
        bl_write();
}

/* a black key of the screen (ui_input.c, the queue of seq.c): 1 NEW, 2 ANSWER, 3 BACK */
static void bss_key(uint32_t k, uint32_t down)
{
    if (!down)
        return;
    if (k == 1u) {
        bl.var = (uint16_t)((bl.var + 1u) % 1000u);
        ui_message("NEW PHRASE");
    } else if (k == 3u) {
        bl.mix = (uint8_t)((bl.mix & ~(31u << 2)) | ((bl_aseed() + 1u) & 31u) << 2);
        ui_message("NEW ANSWER");
    } else if (k == 5u) {
        ui_message("BACK");
    } else {
        return;
    }
    bl_write();
    ui.force = 1;
}

static void bass_screen_input(uint32_t pressed, uint32_t home)
{
    uint32_t k;
    int32_t s;
    if (home == BT_TAP) {
        go_home();
        return;
    }
    if (pressed & (1u << panel.btn[B_PLAY]))
        studio_play();                                  /* (ui_input.c: the loop) */
    if ((s = panel_enc(EN_ALGO)) != 0 && !ft_on) {     /* ALGORITHM: another track, on TRACKS */
        track_step(s);
        go_home();
        return;
    }
    if ((s = panel_enc(EN_SELECT)) != 0)
        bs_page_set(s > 0 ? BS_SHAPE : BS_PHRASE);
    if ((s = panel_enc(EN_PRESET)) != 0) {             /* PRESETS: the sound */
        uint32_t total, cur = preset_pos(&total);
        if (total)
            preset_go((uint32_t)(((int32_t)cur + s % (int32_t)total + (int32_t)total) % (int32_t)total));
        ui.force = 1;
    }
    for (k = 0; k < 4u; k++) {
        int write = 1;
        if ((s = panel_enc(EN_K1 + k)) == 0)
            continue;
        ui.hot_col = (uint8_t)k;
        ui.hot_t = 40;
        if (bs_page == BS_PHRASE) {
            if (k == 0u) {
                bl.var = (uint16_t)((bl.var + 1000 + s) % 1000);
            } else if (k == 1u) {
                bl.rhythm = (uint8_t)clamp(bl.rhythm + s, 0, BR_N - 1);
            } else if (k == 2u) {
                bl.move = (uint8_t)clamp(bl.move + accel(EN_K3, s, 100), 0, 100);
            } else {
                bl.change = (uint8_t)clamp(bl.change + accel(EN_K4, s, 100), 0, 100);
                write = 0;                              /* (the chance: the phrase stays) */
            }
        } else {
            if (k == 0u) {
                bl.answer = (uint8_t)clamp(bl.answer + s, 0, BA_N - 1);
            } else if (k == 1u) {
                bl.hold = (uint8_t)clamp(bl.hold + s, 0, BH_N - 1);
            } else if (k == 2u) {
                bl_set_oct(bl_oct() + s);
            } else {
                cg.follow = (uint8_t)(s > 0);           /* BASS FOLLOW (the CHORDS generator's) */
                write = 0;
            }
        }
        if (write)
            bl_write();
        ui.force = 1;
    }
}
