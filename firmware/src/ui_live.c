/* SPDX-License-Identifier: GPL-3.0-only */
/* LIVE (FMX): one screen and one map of the controls that never changes, whatever track is in focus; HOME goes to
 * STUDIO (every other screen) and back. A change waits for the phrase (Q), a move lasts while it is held.
 *   white keys  1..7 the sections INTRO BUILD DROP ROLL BREAKDOWN DROP 2 OUTRO: asked for, played from the next phrase
 *               (AUTO off: the section then plays on); 8 NEXT SONG: tap BRIDGE, SWAP, CUT, none, shown at once, the
 *               last one taken on the 8-bar phrase (stopped: kept for when it plays);
 *               9..12 the tracks BREAK BASS CHORDS CHOP
 *               in / out on the bar; held: 13 GAP (the BREAK out), 14 WASH, 15 ROLL, 16 REWIND (let go: the section
 *               from its first bar; a section key while it is held: the rewind resolves into that one, on the bar)
 *   black keys  the focus track: the BREAK's loops A..D held on 1..4 (now, whatever Q) and slices on the rest; the
 *               CHOP's MODE on 1..3 (PADS SLICE INST, a tap, now) and its kit's hits or notes on the rest; the notes of
 *               the others
 *   KNOB 1..4   FILTER (the master's DJ filter), ENERGY (the BREAK's MANGLE), SPACE (the reverb), LEVEL (the focus)
 *   ALGORITHM   the focus track; PRESETS its sound (the BREAK: its loop A); SELECT the tempo; SEL (let go): that
 *               track's screen in STUDIO, as SEL on the DECK; HOME there: back to LIVE
 *   OCT- / OCT+ the next song: a project slot with a song; the set is the four slots (stopped: that song loaded,
 *               the set leafed through; work not saved: the same OCT again)
 *   SAVE        the SONG screen's SET page (the four songs: LOAD, SAVE, NEW); HOME there: back to LIVE
 *   PLAY        the song (AUTO SONG's chain; AUTO: its OUTRO the bridge to the next song); SEQ AUTO on / off; GLO Q (NOW, BAR, 4 BARS); EDIT clears what waits
 *   FX held     the FX panel (lfx.c; STUDIO's FX held too): white keys its effects, black keys 1..4 their tracks, 5 LATCH
 *               (a toggle), 6 CLEAR; KNOB 1..4 AMOUNT, TIME, TONE, FEEDBACK; where the section is, to the beat, over the
 *               effects. FX held + HOME: the panel locked open (HOME, FX or another button but PLAY / OCT: let go)
 *   ENV         the show: the DRUMMER over the whole screen (the keys and knobs still LIVE's), ENV again: back
 *   HOME        tap: STUDIO (the FX panel held: locked open; locked: let go); held: the menu
 * The sections are AUTO SONG's (arranger_scene.c): PLAY in LIVE plays the song mode. */
static uint8_t live_set_t;                              /* frames the set's four songs show in the top row (OCT, white key 8) */
static int8_t live_pend = -1;                           /* the section asked for (a role), -1 none */
static int8_t live_jumped = -1;                         /* the entry it was put in the arranger as (-1: not yet) */
static uint8_t live_auto = 1;                           /* AUTO: the song goes on by itself (else the section plays on) */
static uint8_t live_sel;                                /* SEL pressed: the focus track's screen when it is let go */
/* the FX panel shown: FX held, or locked open (FX held + HOME: ly_lock, as STUDIO's layers) */
static int live_fx_held(void) { return (layer_buttons() & ly_bit[LY_FX]) != 0u; }
static int project_info(uint32_t slot, int32_t *bpm);   /* (project.c) */
static void project_load(uint32_t slot);
static int project_unsaved(void);                     /* (project.c: the working song differs from its slot) */
/* the set: the songs of the four project slots */
static int8_t live_song = -1;                           /* the slot playing (-1: none of them) */
static int8_t live_next = -1;                           /* the slot picked (OCT, key 8 round the set) as the next; -1 none */
static uint8_t live_round;                              /* ... by key 8 going round from the set's last song */
static int8_t live_dflt = -1;                           /* the set's own next song after the one playing (-1: the last) */
static int8_t live_next_for = -2;                       /* ... worked out for this song */
static uint8_t live_tr;                                 /* the way asked for (TR_*: seq.c), waiting for the phrase */
static uint8_t live_seen;                               /* as_swapped seen */
static const char *const LIVE_TR[TR_N] = {"", "BRIDGE", "SWAP", "CUT"};
/* (LIVE_LANE, LIVE_LANE_NAME: ui_layers.c, GLO's mix has the same lanes) */

static int live_running(void) { return song.playing && arrangement_enabled && arrangement_clock.running; }
static uint32_t live_q(void) { return q_mode == QM_4BAR ? 4u : 1u; }   /* the bars of a phrase (NOW: the next bar) */
static uint32_t live_role(void) { return arrangement.entry[arrangement_clock.index % ARR_STEPS].scene % AR_N; }
/* the section asked for, lit yellow: playing, the one on its way; stopped, the one PLAY starts from (a key armed it,
 * else the one the song was left in: INTRO for a new song) */
static int32_t live_arm(void) { return live_running() ? live_pend : arr_armed >= 0 ? arr_armed : (int32_t)arr_resume; }

/* LIVE on / off (HOME): the keys follow (seq.c kb_live) */
static void live_set(uint32_t on)
{
    live_on = (uint8_t)(on != 0u);
    kb_live = live_on;
    live_gap = 0;
    live_keys = 0;
    live_pend = live_jumped = -1;
    live_sel = 0;
    home_live = 0;
    vis_on = 0;
    if (!live_on) {
        live_mangle = -1;                               /* (STUDIO: the sections keep the loop's own again; AUTO / MAN
                                                         * stays as it was: MAN's section plays on while STUDIO edits) */
        live_tr = as_tr_ask = as_auto_next = 0;         /* (STUDIO goes to no next song: a bridge under way plays out) */
    } else {
        as_hold(!live_auto);
        live_next_for = -2;                             /* (the next song picked again: the slots may have changed) */
    }
    settings_later = 1;
    ui.force = 1;
}

/* the next slot with a song after slot s (dir +1 / -1), not the one playing; -1: none */
static int32_t live_slot_after(int32_t s, int32_t dir)
{
    uint32_t i;
    for (i = 0; i < 4u; i++) {
        s = (s + dir + 4) % 4;
        if (s != live_song && project_info((uint32_t)s, 0))
            return s;
    }
    return -1;
}
/* the set's next song after slot s: the next slot up with a song (the set does not go round); -1: none */
static int32_t live_slot_next(int32_t s)
{
    for (s = s < 0 ? 0 : s + 1; s < 4; s++)
        if (s != live_song && project_info((uint32_t)s, 0))
            return s;
    return -1;
}
static int32_t live_target(void) { return live_next >= 0 ? live_next : live_dflt; }   /* the next song: picked, else the set's */
/* the main loop, every pass: the way to the next song (arranger_scene.c takes it on the phrase, plays the bridge,
 * swaps): what the arranger has done, the next song for AUTO and for the way asked */
static void live_poll_next(void)
{
    if (as_swapped != live_seen) {                      /* swapped: that song is playing */
        live_seen = as_swapped;
        live_song = (int8_t)((as_swapped_to - 1u) & 3u);
        live_next = -1;                                 /* (the pick taken) */
        live_round = 0;
        live_tr = TR_NONE;
        live_pend = live_jumped = -1;
        ui.force = 1;
    }
    if (live_next_for != live_song) {                   /* (another song: the set's next one after it) */
        live_next_for = live_song;
        live_dflt = (int8_t)live_slot_next(live_song);
    }
    if (live_tr && !as_tr_ask) {                        /* (taken: under way, as_tr_way) */
        live_tr = TR_NONE;
        ui.force = 1;
    }
    as_tr_slot = (uint8_t)(live_target() + 1);
    as_auto_next = (uint8_t)(live_auto && live_target() >= 0 ? live_target() + 1 : 0);
}

/* the main loop, every pass: the section asked for, into the arranger on the last bar of the phrase (it lands on
 * the next bar: arr_next) */
static void live_poll_section(void);
static void live_poll(void)
{
    if (live_running())
        arr_resume = (uint8_t)(live_role() == AR_OUTRO ? AR_INTRO : live_role());   /* (PLAY after STOP: here; the
                                                                                     * OUTRO: the song again) */
    else
        live_pend = -1;                                 /* (stopped: a section key arms, arr_armed) */
    live_poll_next();
    live_poll_section();
    as_pend = (int8_t)(live_running() && live_jumped < 0 ? live_pend : -1);
}
/* the entry a section plays in: its next in the chain, else the next entry becomes it */
static uint32_t live_entry_of(uint32_t role)
{
    uint32_t i, n = arrangement.count, cur = arrangement_clock.index;
    for (i = 1; i <= n; i++)
        if (arrangement.entry[(cur + i) % n].scene == (uint8_t)role)
            return (cur + i) % n;
    arrangement.entry[(cur + 1u) % n].scene = (uint8_t)role;
    return (cur + 1u) % n;
}
static void live_poll_section(void)
{
    if (live_pend < 0 || !live_running()) {
        live_jumped = as_rw_to = -1;
        return;
    }
    if (live_jumped >= 0) {                             /* asked: it lands on the next bar (arr_next takes the jump) */
        if (arrangement_clock.jump < 0) {
            live_pend = live_jumped = as_rw_to = -1;
            ui.force = 1;
        }
        return;
    }
    if ((arrangement_clock.bar + 1u) % live_q())
        return;                                         /* (not the phrase's last bar yet) */
    live_jumped = (int8_t)live_entry_of((uint32_t)live_pend);
    arrangement_clock.jump = live_jumped;
}

/* a white key of the UI's (1..12, and 13..16 let go), from the keys' queue */
static void live_key(uint32_t k, uint32_t down)
{
    int32_t w = white_of_key(k);
    if (w == 15 && !down && live_running()) {           /* REWIND let go: the section from its first bar (resolved into
                                                         * one already: that one) */
        if (!live_rw_cut)
            arrangement_clock.jump = (int8_t)arrangement_clock.index;
        return;
    }
    if (w < 0) {                                        /* a black key here: the CHOP's 1..3, its MODE (seq.c) */
        uint32_t j;
        for (j = 0; j < 3u && LIVE_BLACK[j] != k; j++)
            ;
        if (down && j < 3u && song.sel == CS_TRK) {
            if (j != cs_mode())
                chop_mode_set(j);
            else
                ui_message(N_CS_MODE[j]);
        }
        return;
    }
    if (!down || w >= 12)
        return;
    if (w < 7) {                                        /* a section */
        if (!song.playing) {
            arr_armed = arr_armed == w ? -1 : (int8_t)w;   /* (armed: PLAY starts from it, its first bar) */
        } else {
            if (as_tr_way) {                            /* (the song is leaving: the next one's sections are its own) */
                ui_message("ON THE WAY: NEXT SONG");
                return;
            }
            if (((live_keys >> PA_REWIND) & 1u) && live_running() && (!live_rw_cut || live_jumped >= 0)) {
                live_rw_cut = 1;                        /* REWIND held: it resolves into that section, on the bar, from
                                                         * its first bar, whatever Q (another key before the bar: that one) */
                live_pend = (int8_t)w;
                live_jumped = (int8_t)(live_role() == (uint32_t)w ? arrangement_clock.index : live_entry_of((uint32_t)w));
                arrangement_clock.jump = as_rw_to = live_jumped;
                ui.force = 1;
                return;
            }
            if (live_jumped >= 0)
                return;                                 /* (one already on its way: it lands on the next bar) */
            live_pend = live_pend == w ? -1 : (int8_t)w;   /* (the mode is SEQ's alone: AUTO goes on from there) */
        }
        ui.force = 1;
        return;
    }
    if (w == 7) {                                       /* NEXT: the way there, shown at once; the last one taken on the
                                                         * 8-bar phrase (stopped: kept for when it plays) */
        char m[24];
        live_set_t = 150;
        sg_slot_t = 0;
        if (as_tr_way) {
            ui_message("ON THE WAY: NEXT SONG");        /* (under way: it plays out) */
            return;
        }
        if (live_next >= 0 && !project_info((uint32_t)live_next, 0))
            live_next = -1;
        if (live_target() < 0) {                        /* (the set's last song: round to the first, asked for) */
            live_next = (int8_t)live_slot_after(live_song >= 0 ? live_song : -1, 1);
            live_round = 1;
        }
        if (live_target() < 0) {
            ui_message("NO SONG: SAVE ONE FIRST");
            return;
        }
        live_tr = (uint8_t)((live_tr + 1u) % TR_N);
        if (!live_tr && live_round) {                   /* (none: the set ends again, not round) */
            live_next = -1;
            live_round = 0;
        }
        as_tr_slot = (uint8_t)(live_target() + 1);
        as_tr_ask = live_tr;
        str_cpy(m, live_tr ? LIVE_TR[live_tr] : "NO WAY", sizeof m);
        str_cpy(m + str_len(m), " > SONG ", 9);
        fmt_int(m + str_len(m), live_target() + 1);
        ui_message(live_tr ? m : "NEXT: NONE");
        ui.force = 1;
        return;
    }
    {   /* 9..12: a track in / out, on the bar (Q) */
        uint32_t t = LIVE_LANE[w - 8];
        int32_t m = !q_val(t, P_MUTE);
        q_param(t, P_MUTE, m);
        if (q_on())
            q_name(t, m ? "MUTE" : "IN");
    }
}

/* PLAY in LIVE (and in the menu opened from it): the song, from the armed section */
static void live_play(void)
{
    if (!song.playing) {
        arrangement_enabled = 1;                        /* (LIVE plays the song: AUTO SONG's sections) */
        if (!arr_valid(&arrangement, arrangement_ready())) {
            ui_message("EMPTY SECTION: REC");
            return;
        }
        as_hold(!live_auto);
        live_pend = live_jumped = -1;                   /* (the armed section plays from its first bar: seq_start) */
    }
    if (!ft_owns_press())
        transport_req = song.playing ? 2 : 1;
}

static void live_input(uint32_t pressed, uint32_t home)
{
    uint32_t id, b, k;
    int32_t s;
    live_poll();
    if (ly_lock != LY_FX)
        ly_lock = LY_PLAY;                              /* (no layer here but the FX panel: their buttons are LIVE's) */
    ui.layer = LY_PLAY;
    kb_brk = kb_chd = kb_chp = kb_bss = 0;
    kb_live = 1;
    while (lk_r != lk_w) {                              /* the keys of the UI's (the queue: seq.c) */
        uint32_t e = lk_q[lk_r % LKQ];
        lk_r++;
        if ((e >> 8) == KB_LIVE)
            live_key(e & 0x7Fu, (e >> 7) & 1u);
    }
    if (ly_lock == LY_FX) {                             /* the FX panel locked open: a button lets it go (that button
                                                         * only), not PLAY or OCT; HOME on its tap */
        uint32_t keep = 1u << panel.btn[B_PLAY] | 1u << panel.btn[B_OCTDN] | 1u << panel.btn[B_OCTUP] |
                        1u << panel.btn[B_HOME];
        if (pressed & ~keep) {
            ly_lock = LY_PLAY;
            pressed &= keep;
            ui.force = 1;
        }
    }
    if (home == BT_TAP) {
        if (ly_lock == LY_FX) {                         /* locked: let go */
            ly_lock = LY_PLAY;
            ui.force = 1;
        } else if (live_fx_held()) {                    /* FX held: the panel locked open */
            ly_lock = LY_FX;
            ui.force = 1;
            ui_message("FX LOCKED: HOME LETS GO");
        } else {                                        /* STUDIO */
            live_set(0);
            go_home();
        }
        return;
    }
    for (id = 0; id < 14u; id++) {
        if (!((pressed >> id) & 1u))
            continue;
        b = panel_btn_of(id);
        switch (b) {
        case B_PLAY:
            live_play();
            break;
        case B_OCTDN:
        case B_OCTUP: {                                 /* the next song */
            live_set_t = 150;                           /* (the set shows: the four songs, this one and the next) */
            sg_slot_t = 0;
            int32_t s = live_slot_after(live_next >= 0 && song.playing ? live_next : (live_song >= 0 ? live_song : -1),
                                        b == B_OCTUP ? 1 : -1);   /* (stopped: from the song loaded) */
            char m[24];
            if (as_tr_way)
                break;                                  /* (under way: that song) */
            if (s < 0) {
                ui_message("NO SONG: SAVE ONE FIRST");
                break;
            }
            if (!song.playing) {                        /* stopped: the set leafed through, that song loaded */
                static uint32_t ask_t;
                static int8_t ask_s = -1;
                if (project_unsaved() && !(ask_s == s && fm1_ms - ask_t < 2500u)) {
                    ask_s = (int8_t)s;                  /* work not saved: the same OCT again loads (within 2.5 s) */
                    ask_t = fm1_ms;
                    ui_message("NOT SAVED: AGAIN LOADS");
                    break;
                }
                ask_s = -1;
                project_load((uint32_t)s);
                live_tr = as_tr_ask = TR_NONE;
                str_cpy(m, "SONG ", sizeof m);
                fmt_int(m + str_len(m), s + 1);
                ui_message(m);
                break;
            }
            live_next = (int8_t)s;
            live_round = 0;
            str_cpy(m, "NEXT: SONG ", sizeof m);
            fmt_int(m + str_len(m), s + 1);
            ui_message(m);
            ui.force = 1;
            break;
        }
        case B_SAVE:                                    /* the SONG screen's SET page (LOAD, SAVE, NEW); HOME: back here */
            live_set(0);
            sg_page = 0;
            studio_open(SC_SONG);
            home_live = 1;
            return;
        case B_ENV:                                     /* the show: the DRUMMER, or back */
            if (vis_on) {
                vis_on = 0;
                ui.force = 1;
            } else {
                vis_open();
            }
            break;
        case B_SEQ:                                     /* AUTO: the song by itself, or the section plays on */
            live_auto = (uint8_t)!live_auto;
            as_hold(!live_auto);
            ui_message(!live_auto ? "AUTO OFF: THE SECTION STAYS" : arrangement.loop ? "AUTO: ENDLESS, 8: NEXT SONG" :
                       "AUTO: THE SONG GOES ON");
            break;
        case B_GLO:                                     /* Q: NOW, BAR, 4 BARS */
            q_mode = (uint8_t)((q_mode + 1u) % 3u);
            settings_later = 1;
            ui_message(q_mode == QM_NOW ? "Q NOW" : q_mode == QM_BAR ? "Q BAR" : "Q 4 BARS");
            break;
        case B_SCL:                                     /* the focus track's screen, on the release (STUDIO's SEL
                                                         * then sees it up: no second tap there) */
            if (!live_fx_held())
                live_sel = 1;
            break;
        case B_EDIT:                                    /* clear what waits */
            live_pend = live_jumped = as_rw_to = -1;
            live_tr = as_tr_ask = TR_NONE;              /* (the way asked; one under way plays out) */
            if (!as_tr_way) {
                arrangement_clock.jump = -1;
                if (live_round) {                       /* (the set ends again, not round) */
                    live_next = -1;
                    live_round = 0;
                }
            }
            q_clear();
            ui_message("CLEARED");
            break;
        default:
            break;
        }
    }
    if (live_sel && !(fm1_in.buttons & ly_bit[LY_SCALE])) {   /* SEL let go: STUDIO, the track's screen */
        live_set(0);
        go_home();
        layer_tap(LY_SCALE);
        home_live = 1;                                  /* (HOME: back to LIVE) */
        return;
    }
    if ((s = panel_enc(EN_SELECT)) != 0) {
        song.g[G_BPM] = (int16_t)clamp(song.g[G_BPM] + accel(EN_SELECT, s, 200), GP[G_BPM].min, GP[G_BPM].max);
        ui.bpm_t = 40;
    }
    if ((s = panel_enc(EN_ALGO)) != 0 && !ft_on)
        track_step(s);
    if ((s = panel_enc(EN_PRESET)) != 0) {              /* the focus track's sound (on the phrase: Q) */
        uint32_t total, cur = preset_pos(&total), nl = brkp_nloops();
        if (is_brk(TSEL))
            q_param(TRK_BRK, P_LRATE, clamp(q_val(TRK_BRK, P_LRATE) + s, 0, nl ? (int32_t)nl - 1 : 0));
        else if (total)
            preset_go((uint32_t)(((int32_t)cur + s % (int32_t)total + (int32_t)total) % (int32_t)total));
    }
    for (k = 0; k < 4u; k++) {
        if ((s = panel_enc(EN_K1 + k)) == 0)
            continue;
        ui.hot_col = (uint8_t)k;
        ui.hot_t = 40;
        if (live_fx_held()) {                           /* the FX panel's: AMOUNT, TIME, TONE, FEEDBACK */
            lfx_knob(k, s);
            continue;
        }
        if (k == 0u) {
            song.g[G_FILT] = (int16_t)clamp(song.g[G_FILT] + accel(EN_K1, s, 127), -64, 63);
        } else if (k == 1u) {
            live_mangle = (int16_t)clamp(BPV(BP_MANGLE) + accel(EN_K2, s, 100), 0, 100);
            BPV(BP_MANGLE) = live_mangle;
        } else if (k == 2u) {
            song.g[G_SPACE] = (int16_t)clamp(song.g[G_SPACE] + accel(EN_K3, s, 127), 0, 127);
        } else {
            TSEL->p[P_LEVEL] = (int16_t)clamp(TSEL->p[P_LEVEL] + accel(EN_K4, s, 127), 0, 127);
        }
    }
}

/* the next song on its way (asked, bridging, or AUTO in the OUTRO going on to it): white key 8 and the screen's AUTO
 * and its label flash twice a second, together (live_dblink) */
static int live_dblink(void)
{
    uint32_t p = fm1_ms % 1000u;
    return p < 120u || (p >= 240u && p < 360u);
}
static int live_next_on(void)
{
    return as_tr_way || (live_target() >= 0 && (live_tr || (as_auto_next && live_running() && live_role() == AR_OUTRO)));
}
/* the keys lit: the section playing (blinking: the one asked for), the tracks heard, the moves held; FX held: the
 * effects in force, the black keys held */
static uint32_t live_keys_lit(void)
{
    uint32_t m = 0, i, blink = (fm1_ms / 125u) & 1u;
    if (live_fx_held())
        return lfx_keys_lit();
    if (live_running())
        m |= 1u << key_of_white(live_role());
    if (live_arm() >= 0 && (blink || !live_running()))
        m ^= 1u << key_of_white((uint32_t)live_arm());
    if (live_next_on() && live_dblink())
        m |= 1u << key_of_white(7);
    for (i = 0; i < 4u; i++)
        if (!trk_silent(&trk[LIVE_LANE[i]]) && (!q_waits() || blink || q_val(LIVE_LANE[i], P_MUTE) == trk[LIVE_LANE[i]].p[P_MUTE]))
            m |= 1u << key_of_white(8u + i);
    if (live_gap)
        m |= 1u << key_of_white(12);
    for (i = 1; i < 4u; i++)
        if ((live_moves() >> LIVE_MOVE[i]) & 1u)
            m |= 1u << key_of_white(12u + i);
    return m;
}

/* ---- the FX panel (lfx.c), LIVE's and STUDIO's alike (ui_layers.c: FX held there too) */
/* KNOB k turned by s, the panel shown: AMOUNT, TIME, TONE, FEEDBACK */
static void lfx_knob(uint32_t k, int32_t s)
{
    if (k == 0u)
        lfx.amt = (uint8_t)clamp(lfx.amt + accel(EN_K1, s, 127), 0, 127);
    else if (k == 1u)
        lfx.time = (uint8_t)clamp(lfx.time + s, 0, 7);
    else if (k == 2u)
        lfx.tone = (uint8_t)clamp(lfx.tone + accel(EN_K3, s, 127), 0, 127);
    else
        lfx.fb = (uint8_t)clamp(lfx.fb + accel(EN_K4, s, 110), 0, 110);
}
/* the keys lit: the effects in force (held or stuck), the tracks (held; LATCH: stuck), LATCH not OFF, CLEAR held */
static uint32_t lfx_keys_lit(void)
{
    uint32_t m = 0, i, tg = lfx.mode ? lfx.tsel : lfx.black;
    for (i = 0; i < LFX_N; i++)
        if (((lfx.on | lfx.held | lfx.latch) >> i) & 1u)
            m |= 1u << key_of_white(i);
    for (i = 0; i < 4u; i++)
        if ((tg >> i) & 1u)
            m |= 1u << LIVE_BLACK[i];
    if (lfx.mode)
        m |= 1u << LIVE_BLACK[4];
    if (lfx.black & 32u)
        m |= 1u << LIVE_BLACK[5];
    return m;
}

/* where the music is, over the effects (y 40..63): the section (AUTO SONG's; else the loop's 4 bars), its four beats,
 * the bar of the section (its last bar: the one coming, yellow), the section's way to its end, to the beat */
static void lfx_where(uint32_t *cache)
{
    uint32_t run = live_running(), play = song.playing, beat = play ? clk_beat % 4u : 0u, i, sig;
    uint32_t role = run ? live_role() : AR_N, bars = 4u, bar = (clk_beat / 4u) % 4u, last;
    int32_t next = -1;
    char t[12];
    if (run) {
        uint32_t ix = arrangement_clock.index, n = arrangement.count;
        bars = arrangement.entry[ix % ARR_STEPS].bars;
        bar = arrangement_clock.bar;
        if (live_pend >= 0)                             /* (asked for, LIVE) */
            next = live_pend;
        else if (!arrangement_clock.hold && (ix + 1u < n || arrangement.loop))
            next = (int32_t)(arrangement.entry[(ix + 1u) % (n ? n : 1u)].scene % AR_N);
    }
    if (!bars)
        bars = 1u;
    last = play && bar + 1u >= bars && next >= 0;
    sig = role * 31u + bar * 977u + bars * 7919u + beat * 13u + play * 3u + run * 5u + (uint32_t)(next + 1) * 104729u +
          last * 65537u;
    if (!ui.force && sig == *cache)
        return;
    *cache = sig;
    cv_begin(240, 24, C_BLACK);
    ps_ground(40);
    ps_win(3, 2, 40, 20, run ? PS_LINE : PS_DIM);
    te_text_c(23, 4, run ? AR_SHORT[role] : play ? "LOOP" : "STOP", run ? PS_TXT : PS_LBL);
    for (i = 0; i < 4u; i++) {                          /* the beats: the one now lit (the downbeat red), those gone dim */
        int32_t x = 48 + (int32_t)i * 13;
        uint16_t c = !play ? PS_DIM : i == beat ? (i ? C_WHITE : PS_RED) : i < beat ? PS_LINE : PS_DIM;
        if (play && i <= beat)
            cv_rect(x, 6, 11, 12, c);
        else
            ps_win(x, 6, 11, 12, c);
    }
    if (last) {                                         /* the section's last bar: the one coming */
        str_cpy(t, "> ", sizeof t);
        str_cpy(t + 2, AR_SHORT[next], sizeof t - 2);
    } else {
        fmt_int(t, (int32_t)(bar % bars) + 1);
        str_cpy(t + str_len(t), "/", 2);
        fmt_int(t + str_len(t), (int32_t)bars);
    }
    ps_text(102, 4, &FONT_S, t, last ? PS_YEL : play ? PS_TXT : PS_LBL);
    ps_gauge(148, 10, 88, play ? (int32_t)(((bar % bars) * 4u + beat + 1u) * 1000u / (bars * 4u)) : 0,
             last ? PS_YEL : run ? PS_GRN : PS_LBL, 0);
    cv_blit(0, 40);
}

/* the header's right: FX (LATCH TRACKS: a padlock; TRACKS + FX: two, one behind the other), then the four tracks as black keys 1..4 are, in their colours: lit the
 * ones the next effect goes to (none picked: all four). Over te_header's own (its title left empty), drawn again with
 * it; x 152.. (the transport's bar.beat ends before; a multiple of 4: the ground's pattern in step) */
static void lfx_lock(int32_t x, int32_t y, uint16_t c)   /* a padlock 10 x 14: its shackle, its body, the keyhole */
{
    cv_rect(x + 2, y, 6, 2, c);
    cv_rect(x + 1, y + 1, 2, 6, c);
    cv_rect(x + 7, y + 1, 2, 6, c);
    cv_rect(x, y + 6, 10, 8, c);
    cv_rect(x + 4, y + 8, 2, 4, PS_INK);
}
static void lfx_head(uint32_t *head, uint32_t *mine)
{
    uint32_t h0 = *head, tg = (lfx.mode ? lfx.tsel : lfx.black) & 15u, sig = tg * 3u + lfx.mode, i;
    te_header("", PS_LBL, head);
    if (!ui.force && *head == h0 && sig == *mine)
        return;
    *mine = sig;
    cv_begin(88, 20, C_BLACK);
    ps_ground(0);
    if (lfx.mode == LM_ALL) {                           /* two padlocks: the one behind dimmer, the front one outlined */
        lfx_lock(4, 1, mix565_ps(PS_INK, PS_YEL, 150));
        lfx_lock(9, 4, PS_INK);
        lfx_lock(11, 4, PS_INK);
        lfx_lock(10, 6, PS_INK);
        lfx_lock(10, 5, PS_YEL);
    } else if (lfx.mode) {                              /* one padlock, shadowed as ps_text is */
        lfx_lock(9, 4, PS_INK);
        lfx_lock(8, 3, PS_YEL);
    } else {
        ps_text(2, 3, &FONT_S, "FX", RGB(120, 186, 255));
    }
    for (i = 0; i < 4u; i++) {
        int32_t x = 26 + (int32_t)i * 15;
        uint16_t c = TRK_COL[LIVE_LANE[i]];
        if (!tg || (tg >> i) & 1u) {
            cv_rect(x, 5, 13, 11, PS_INK);
            cv_rect(x + 1, 6, 11, 9, c);
        } else {
            ps_win(x, 5, 13, 11, PS_DIM);
        }
    }
    cv_blit(152, 0);
}

/* the panel: the header (the tracks the next effect goes to; LATCH), where the music is, the 16 effects in their groups
 * (in force lit, stuck outlined), the knobs of the effects */
static void lfx_draw(void)
{
    static uint32_t head, tracks, where, b1, b2, foot;
    static const uint16_t GC[4] = {RGB(120, 186, 255), RGB(255, 120, 170), RGB(255, 226, 122), RGB(255, 138, 58)};
    static const char *const LAB[4] = {"amount", "time", "tone", "fdbk"};
    char v[4][12];
    const char *val[4] = {v[0], v[1], v[2], v[3]};
    int32_t ratio[4];
    uint32_t on = lfx.on | lfx.held, lat = lfx.latch, i, half, sig;
    lfx_head(&head, &tracks);
    lfx_where(&where);
    sig = on * 3u + lat * 65537u + lfx.last * 7u;
    for (half = 0; half < 2u; half++) {                 /* two bands: the groups SPACE, TIME; COLOUR, RHYTHM */
        uint32_t *cache = half ? &b2 : &b1;
        if (!ui.force && sig == *cache)
            continue;
        cv_begin(240, 60, C_BLACK);
        ps_ground(64 + (int32_t)half * 60);
        for (i = half * 8u; i < half * 8u + 8u; i++) {
            int32_t row = (int32_t)(i / 4u) - (int32_t)half * 2, col = (int32_t)(i % 4u), x = 3 + col * 59, y = 1 + row * 30;
            uint32_t g = i / 4u, ready = i < LX_READY, lit = (on >> i) & 1u, held = (lat >> i) & 1u;
            uint16_t c = ready ? GC[g] : PS_DIM;
            if (!ready)
                continue;                               /* (an effect still to come: no tile, its key does nothing) */
            if (lit)
                cv_rect(x + 2, y + 2, 52, 24, mix565_ps(C_BLACK, c, 110));
            ps_win(x, y, 56, 28, held ? PS_YEL : lit ? c : ready ? PS_LINE : PS_DIM);
            te_text_c(x + 28, y + 6, LFX_NAME[i], lit ? PS_TXT : c);
        }
        cv_blit(0, (uint32_t)(64 + (int32_t)half * 60));
        *cache = sig;
    }
    str_cpy(v[0], "", 12);
    fmt_int(v[0], lfx.amt);
    te_lower(v[1], N_DLY[lfx.time % 8u], 12);
    fmt_int(v[2], lfx.tone);
    fmt_int(v[3], lfx.fb);
    ratio[0] = lfx.amt * 1000 / 127;
    ratio[1] = (int32_t)(lfx.time % 8u) * 1000 / 7;
    ratio[2] = lfx.tone * 1000 / 127;
    ratio[3] = lfx.fb * 1000 / 110;
    te_hint_txt = "WHITE: FX  BLACK 1-4: TRACKS";
    te_dials(184, LAB, val, ratio, sig, &foot);
}

static void live_draw(void)
{
    static uint32_t head, b1, b2, foot;
    char v[4][12];
    const char *val[4] = {v[0], v[1], v[2], v[3]};
    static const char *const LAB[4] = {"filter", "energy", "space", "level"};
    int32_t ratio[4], f = song.g[G_FILT];
    uint32_t sig, i, run = live_running(), role = run ? live_role() : AR_N, bar = arrangement_clock.bar, q = live_q();
    uint32_t bars = run ? arrangement.entry[arrangement_clock.index % ARR_STEPS].bars : 0u;
    static uint8_t fx_shown;
    if (live_fx_held() != fx_shown) {                   /* (the FX panel in or out: everything drawn again) */
        fx_shown = (uint8_t)live_fx_held();
        ui.force = 1;
    }
    if (fx_shown) {
        lfx_draw();
        return;
    }
    {   /* the header: the song of the set playing */
        static char title[12];
        str_cpy(title, "live", sizeof title);
        if (live_song >= 0) {
            str_cpy(title, "song ", sizeof title);
            fmt_int(title + 5, live_song + 1);
        }
        te_header(title, PS_YEL, &head);
    }
    /* the song: AUTO / Q, the sections (the one playing lit, the one asked for yellow), where the phrase is */
    sig = (uint32_t)live_auto * 3u + q_mode * 7u + role * 31u + (uint32_t)(live_arm() + 1) * 131u + bar * 977u +
          bars * 7919u + run * 3u + (uint32_t)song.playing * 5u + ((fm1_ms / 250u) & 1u) * (live_arm() >= 0) * 65537u +
          (uint32_t)(live_next_on() && live_dblink()) * 7777u + live_tr * 104729u + as_tr_way * 1299709u + (uint32_t)(live_target() + 1) * 15485863u + (uint32_t)(live_song + 1) * 7u +
          as_tr_to * 11u + as_auto_next * 13u;
    if (live_set_t) {                                   /* (the set's tempi: now and then, from the slots) */
        sig = sig * 31u + 1u + (uint32_t)live_dblink() * 7u + (uint32_t)(live_target() + 1) * 131u;
        for (i = 0; i < 4u; i++)
            sig = sig * 31u + sg_slot[i].used * 5u + (uint32_t)sg_slot[i].bpm;
        if (!sg_slot_t)
            sg_set_refresh();
        else
            sg_slot_t--;
        live_set_t--;
    }
    if (ui.force || sig != b1) {
        char t[24];
        b1 = sig;
        cv_begin(240, 72, C_BLACK);
        ps_ground(40);
        {   /* AUTO / MAN; flashing with white key 8 while the next song is on its way */
            int fl = live_next_on() && live_dblink();
            uint16_t c = fl ? PS_YEL : live_auto ? PS_GRN : PS_LBL;
            if (fl)
                cv_rect(6, 6, 42, 16, PS_LINE);
            ps_win(4, 4, 46, 20, fl ? PS_YEL : live_auto ? PS_GRN : PS_DIM);
            ps_text(9, 6, &FONT_S, live_auto ? "AUTO" : "MAN", c);
        }
        ps_win(54, 4, 46, 20, PS_YEL);
        ps_text(59, 6, &FONT_S, q_mode == QM_4BAR ? "Q 4" : q_mode == QM_BAR ? "Q 1" : "Q NOW", PS_YEL);
        if (live_set_t) {                               /* the set, a few seconds after OCT or white key 8: the four songs by
                                                         * their tempo; the one loaded green, the one next blinking */
            uint32_t i;
            for (i = 0; i < 4u; i++) {
                int32_t x = 104 + (int32_t)i * 33;
                uint32_t used = sg_slot[i].used, cur = (int32_t)i == live_song, nxt = (int32_t)i == live_target() && !cur;
                uint16_t c = cur ? PS_GRN : nxt ? (live_dblink() ? PS_YEL : PS_LBL) : used ? PS_LBL : PS_DIM;
                ps_win(x, 4, 31, 20, c);
                if (used)
                    fmt_int(t, sg_slot[i].bpm);
                else
                    str_cpy(t, "-", 2);
                te_text_c(x + 15, 6, t, cur ? PS_GRN : nxt ? PS_YEL : used ? PS_TXT : PS_DIM);
            }
        } else if (as_tr_way && as_tr_to) {             /* on the way to the next song */
            str_cpy(t, LIVE_TR[as_tr_way % TR_N], sizeof t);
            str_cpy(t + str_len(t), " > SONG ", 9);
            fmt_int(t + str_len(t), as_tr_to);
            ps_text(236 - text_w(&FONT_S, t), 6, &FONT_S, t, PS_GRN);
        } else if (live_tr && live_target() >= 0) {     /* asked: the next song, the way, from the next 8 bars */
            fmt_int(t, live_target() + 1);
            str_cpy(t + str_len(t), " ", 2);
            str_cpy(t + str_len(t), LIVE_TR[live_tr], 8);
            ps_text(236 - text_w(&FONT_S, t), 6, &FONT_S, t, live_dblink() ? PS_YEL : PS_GRN);
        } else if (live_pend >= 0) {                    /* what comes, in how many bars */
            str_cpy(t, AR_NAME[live_pend], sizeof t);
            if (run) {
                str_cpy(t + str_len(t), " IN ", 5);
                fmt_int(t + str_len(t), (int32_t)(q - (bar % q)));
            }
            ps_text(236 - text_w(&FONT_S, t), 6, &FONT_S, t, PS_YEL);
        } else if (run) {
            if (live_auto && bars) {
                uint32_t last = arrangement_clock.index + 1u >= arrangement.count,
                         nx = !last ? arrangement.entry[arrangement_clock.index + 1u].scene : AR_N;
                if (!last)
                    str_cpy(t, AR_NAME[nx % AR_N], sizeof t);
                else if (arrangement.loop)
                    str_cpy(t, "AGAIN", sizeof t);      /* (ENDLESS: AUTO stays on the song, a new round) */
                else if (as_auto_next) {                /* (the OUTRO: the bridge to it) */
                    str_cpy(t, "SONG ", sizeof t);
                    fmt_int(t + 5, as_auto_next);
                } else
                    str_cpy(t, "END", sizeof t);
                str_cpy(t + str_len(t), " IN ", 5);
                fmt_int(t + str_len(t), (int32_t)(bars - bar));
            } else {
                str_cpy(t, "LOOPING", sizeof t);
            }
            ps_text(236 - text_w(&FONT_S, t), 6, &FONT_S, t, PS_LBL);
        } else {
            str_cpy(t, "PLAY > ", sizeof t);                /* stopped: where PLAY starts (the key armed, or where it was left) */
            str_cpy(t + str_len(t), AR_NAME[live_arm() % (int32_t)AR_N], sizeof t - 7);
            ps_text(236 - text_w(&FONT_S, t), 6, &FONT_S, t, PS_LBL);
        }
        for (i = 0; i < AR_N; i++) {                    /* the sections: white keys 1..7 */
            int32_t x = 3 + (int32_t)i * 34;
            uint16_t c = i == role ? PS_TXT : (int32_t)i == live_arm() ? PS_YEL : PS_LBL;   /* (every key plays: readable) */
            if (i == role)
                cv_rect(x + 2, 30, 30, 22, PS_LINE);
            ps_win(x, 28, 34, 26, (int32_t)i == live_arm() && (run ? ((fm1_ms / 250u) & 1u) : 1u) ? PS_YEL :
                                  c == PS_LBL ? PS_LINE : c);
            te_text_c(x + 17, 33, AR_SHORT[i], c);
        }
        if (run && bars) {                              /* the bar of the phrase, its beats */
            str_cpy(t, "BAR ", sizeof t);
            fmt_int(t + 4, (int32_t)(bar % q + 1u));
            str_cpy(t + str_len(t), "/", 2);
            fmt_int(t + str_len(t), (int32_t)q);
            ps_text(6, 56, &FONT_S, t, PS_TXT);
            ps_gauge(100, 60, 136, (int32_t)((bar + 1u) * 1000u / bars), live_auto ? PS_GRN : PS_DIM, 0);
        }
        cv_blit(0, 40);
    }
    /* the tracks: heard, out, waiting for the bar; the focus (ALGORITHM) marked */
    sig = song.sel * 13u;
    for (i = 0; i < 4u; i++) {
        uint32_t t = LIVE_LANE[i];
        sig = sig * 31u + (uint32_t)trk_silent(&trk[t]) * 2u + (uint32_t)(q_val(t, P_MUTE) != trk[t].p[P_MUTE]) * 5u +
              (uint32_t)trk[t].p[P_LEVEL] * 7u;
    }
    sig = sig * 3u + live_gap + live_moves() * 11u;
    if (ui.force || sig != b2) {
        b2 = sig;
        cv_begin(240, 72, C_BLACK);
        ps_ground(112);
        for (i = 0; i < 4u; i++) {
            uint32_t t = LIVE_LANE[i];
            int32_t y = 1 + (int32_t)i * 18, out = trk_silent(&trk[t]), wait = q_val(t, P_MUTE) != trk[t].p[P_MUTE];
            if (t == song.sel % NTRK) {
                cv_rect(0, y, 240, 17, PS_LINE);
                ps_text(3, y, &FONT_S, ">", TRK_COL[t]);
            }
            ps_text(16, y, &FONT_S, LIVE_LANE_NAME[i], out ? PS_DIM : TRK_COL[t]);
            ps_gauge(100, y + 5, 80, out ? 0 : trk[t].p[P_LEVEL] * 1000 / 127, out ? PS_DIM : TRK_COL[t], 0);
            ps_text(186, y, &FONT_S, wait ? "@BAR" : out ? "OUT" : "IN", wait ? PS_YEL : out ? PS_DIM : PS_TXT);
        }
        cv_blit(0, 112);
    }
    /* the knobs */
    if (!f)
        str_cpy(v[0], "open", 12);
    else {
        str_cpy(v[0], f < 0 ? "lp " : "hp ", 12);
        fmt_int(v[0] + 3, f < 0 ? -f : f);
    }
    fmt_int(v[1], BPV(BP_MANGLE));
    fmt_int(v[2], song.g[G_SPACE]);
    fmt_int(v[3], TSEL->p[P_LEVEL]);
    ratio[0] = (f + 64) * 1000 / 127;
    ratio[1] = BPV(BP_MANGLE) * 10;
    ratio[2] = song.g[G_SPACE] * 1000 / 127;
    ratio[3] = TSEL->p[P_LEVEL] * 1000 / 127;
    te_hint_txt = "SEQ AUTO  GLO Q  SAVE SONGS";
    te_dials(184, LAB, val, ratio, song.sel * 977u + live_gap * 3u + live_moves() * 7u, &foot);
    if (live_gap || live_moves()) {                    /* a move held: its name over the song */
        static const char *const MV[4] = {"GAP", "WASH", "ROLL", "REWIND"};
        for (i = 0; i < 4u; i++)
            if (i ? (live_moves() >> LIVE_MOVE[i]) & 1u : live_gap)
                break;
        if (i < 4u) {
            cv_begin(140, 40, C_BLACK);
            ps_win(0, 0, 140, 40, PS_RED);
            te_text_c(70, 12, MV[i], PS_RED);
            cv_blit(50, 76);
            b1 = 0;                                     /* (the song band again when it is let go) */
        }
    }
}
