/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Felucca UI input: LEDs, knobs and buttons, the layers (ui_layers.c), holds, SEQ step entry, panel setup. */
/* ----------------------------------------------------------- LEDs --- */
/* The LED picture is built off-line and copied one byte per column: clearing
 * and relighting would let the 10 kHz scan catch the dark gap and flicker. */
static uint8_t led_pos[41];                        /* (col << 3) | row bit, 0xFF = none */

static void led_pos_init(void)
{
    uint32_t id, p, r;
    for (id = 0; id < 41u; id++) {
        led_pos[id] = 0xFF;
        for (p = 0; p < FM1_NCOL; p++)
            for (r = 1; r < 5u; r++)
                if (FM1_KEYMAP[r][p] == (int8_t)id)
                    led_pos[id] = (uint8_t)((p << 3) | r);
    }
}

static void led_put(uint8_t *nl, uint32_t id, int on)
{
    uint8_t q = led_pos[id];
    if (q != 0xFF && on)
        nl[q >> 3] |= (uint8_t)(1u << (q & 7u));
}

static const uint8_t FAM_BTN[FAM_COUNT] = {B_HOME, B_ENV, B_LFO, B_FX, B_SCL, B_EDIT, B_GLO, B_SAVE,
                                           B_ARP, B_SEQ, B_HOME};   /* button of each page family (TRACKS: HOME) */

static uint32_t cur_fam(void) { return ui.home ? FAM_HOME : cur_page()->fam; }
static uint32_t cur_btn(void)                      /* the button of the screen shown */
{
    if (!ui.home && cur_page()->scope == SC_SONG)
        return B_SAVE;
    if (!ui.home && cur_page()->scope == SC_BREAK)
        return B_SEQ;
    if (!ui.home && (cur_page()->scope == SC_CHORD || cur_page()->scope == SC_CHOP || cur_page()->scope == SC_BASS))
        return B_SCL;
    return FAM_BTN[cur_fam()];
}

/* PLAY flashes on every beat, as heard (the sound leaves ~9 ms after it is rendered) */
static int play_led(void)
{
    uint32_t p = clk_pos, lat = HALF_FRAMES * 3u / 2u * (uint32_t)song.g[G_BPM];
    if (!song.playing)
        return 0;
    p = p >= lat ? p - lat : p + BEAT_U - lat;
    return p < BEAT_U / 4u;
}

/* the keys' lights: what the layer held does, else the keys down and the drum hits */
/* what sounds on track t, as keys: the drum hits (each lights its key a few frames) or, on a synth
 * track, the voices still held, mapped back through the keyboard (octave, scale, chords; by
 * @renebohne, PR #11). Shown at once with menu NOTES on, on every screen and layer (keys_notes_dim) */
static uint32_t keys_sounding(const track_t *t)
{
    uint32_t i, m = 0;
    if (is_brk(t)) {
        for (i = 0; i < BRK_SLICES; i++)
            if (pad_lit[i])
                m |= 1u << brk_key_of_slice(i);
        return m;
    }
    for (i = 0; i < NVOICE; i++) {
        const voice_t *v = &t->v[i];
        if (v->active && v->gate && v->stage <= 2) {
            uint32_t k, note = v->note;
            for (k = 0; k < 27u; k++)
                if (kb_map(t, k) == note)
                    m |= 1u << k;
        }
    }
    if (t == TSEL)                                 /* + the notes just started, a few frames (pads_tick) */
        for (i = 0; i < 27u; i++)
            if (key_lit[i])
                m |= 1u << i;
    return m;
}

static uint32_t scale_keys(uint32_t root_only)   /* SEL: the keys in the scale (or its roots only) */
{
    uint32_t i, m = 0, root = (uint32_t)trk[0].p[P_ROOT] % 12u, mask = SCALE_MASK[clamp(trk[0].p[P_SCALE], 0, NSCALES - 1)];
    for (i = 0; i < 27u; i++) {
        uint32_t d = (53u + i - root + 120u) % 12u;
        if ((mask >> d) & 1u && (!root_only || !d))
            m |= 1u << i;
    }
    return m;
}

static uint32_t erase_lanes(const track_t *t)   /* EDIT erase, BREAK track: the slices its steps hold */
{
    uint32_t i, m = 0;
    if (is_brk(t))
        for (i = 0; i < trk_len(t); i++)
            if (t->bstep[i].slice)
                m |= 1u << brk_key_of_slice(t->bstep[i].slice - 1u);
    return m;
}

/* the LIVE screen (ui_live.c, after this file) */
static void live_input(uint32_t pressed, uint32_t home);
static void live_set(uint32_t on);
static void live_play(void);
static uint8_t home_live;                               /* STUDIO came from LIVE's SEL: HOME goes back to LIVE */
static uint32_t live_keys_lit(void);

static uint32_t keys_lit(void)
{
    uint32_t m = 0, i, blink = (fm1_ms / 125u) & 1u;
    track_t *t = TSEL;
    if (live_on && !ui.menu)
        return live_keys_lit();
    switch (ui.layer) {
    case LY_FX:                                    /* the FX panel: the effects in force, the black keys (ui_live.c) */
        return lfx_keys_lit();
    case LY_STEP: {                                /* the steps that play; the playhead blinks */
        uint32_t len = trk_len(t);
        for (i = 0; i < 16u; i++) {
            uint32_t idx = ui.step_page * 16u + i, on;
            if (idx >= len)
                continue;
            on = is_brk(t) ? t->bstep[idx].on != 0u : step_on(&t->step[idx]);
            if (song.playing && idx == t->seq_idx)
                on = !on || blink;
            if (on)
                m |= 1u << key_of_white(i);
        }
        return m | fm1_in.notes;
    }
    case LY_SCALE:                                 /* the keys in the scale; the root blinks */
        if (lights_notes)                          /* NOTES: the scale goes dim (keys_notes_dim), what sounds lit */
            return (blink ? scale_keys(1) : 0u) | keys_sounding(t) | fm1_in.notes;
        return scale_keys(0) & ~(blink ? 0u : scale_keys(1));
    case LY_MIX:                                   /* LIVE's lanes heard: 1..4; soloed: 5..8; a fill: 9, armed / on: 10; tap: the beat */
        for (i = 0; i < 4u; i++) {
            if (!trk_silent(&trk[LIVE_LANE[i]]))
                m |= 1u << key_of_white(i);
            if ((song.solo >> LIVE_LANE[i]) & 1u)
                m |= 1u << key_of_white(4u + i);
        }
        if (fill_now)
            m |= 1u << key_of_white(8);
        if (fill_arm || fill_bar_on)
            m |= 1u << key_of_white(9);
        if (play_led())
            m |= 1u << key_of_white(15);
        return m;
    case LY_ERASE:                                 /* the sounds the pattern holds (NOTES: dim, the hits lit) */
        return (lights_notes ? 0u : erase_lanes(t)) | fm1_in.notes | (lights_notes ? keys_sounding(t) : 0u);
    default:                                       /* playing, ARP roll, SAVE song, every page and the menu */
        break;
    }
    if (kb_brk)                                    /* the BREAK screen: the steps, the slices, the live keys */
        return brk_keys_lit();
    if (kb_chd)                                    /* the CHORDS screen: the nodes, the steps */
        return chd_keys_lit();
    m = fm1_in.notes;
    if (is_brk(t) || lights_notes)                 /* the BREAK track: each hit lights its key; NOTES: the synths too */
        m |= keys_sounding(t);
    return m;
}

/* menu NOTES, on the layers whose keys are tiles (FX effects, SEQ steps, GLO mute / solo): what
 * sounds glows dimly under the tiles, which keep their full light. SEL (the scale) and EDIT on the
 * drum track (the sounds of the pattern): those glow, and what sounds is lit (keys_lit) */
static uint32_t keys_notes_dim(void)
{
    if (!lights_notes)
        return 0u;
    switch (ui.layer) {
    case LY_FX:
    case LY_STEP:
    case LY_MIX:
        return keys_sounding(TSEL);
    case LY_SCALE:
        return scale_keys(0);
    case LY_ERASE:
        return erase_lanes(TSEL);
    default:
        return 0u;
    }
}

/* the keys' landmarks, dim: the first key of each row of the 4 x 4 grid on screen (white keys
 * 1, 5, 9, 13) while a layer is held */
static uint32_t keys_guide(void)
{
    if (ui.layer == LY_PLAY)
        return 0u;
    return 1u << key_of_white(0) | 1u << key_of_white(4) | 1u << key_of_white(8) | 1u << key_of_white(12);
}

/* the keys the backlight lights (menu KEYS): the Cs, every white key, or every key; bit k = key k (0 = F3) */
static uint32_t lights_keys_mask(void)
{
    uint32_t k, m = 0;
    if (!lights_lvl || !lights_keys)
        return 0u;
    for (k = 0; k < 27u; k++) {
        uint32_t pc = (53u + k) % 12u;             /* key 0 = F3 (53) */
        if (lights_keys == KEYS_ALL || (lights_keys == KEYS_C ? pc == 0u : ((0xAB5u >> pc) & 1u) != 0u))   /* 0xAB5: C D E F G A B */
            m |= 1u << k;
    }
    return m;
}

static void ui_leds(void)
{
    uint8_t nl[FM1_NCOL] = {0}, dl[FM1_NCOL] = {0}, bl[FM1_NCOL] = {0};
    uint32_t k, c, keys, guide, back;
    static uint8_t ready;
    if (!ready) {
        led_pos_init();
        ready = 1;
    }
    led_put(nl, panel.btn[ui.layer != LY_PLAY ? LAYER_BTN[ui.layer] : cur_btn()],
            ly_lock == LY_PLAY || ((fm1_ms / 300u) & 1u) != 0u || (fm1_in.buttons & ly_bit[ly_lock % LY_COUNT]) != 0u);   /* locked: blinks */
    led_put(nl, panel.btn[B_PLAY], play_led() || (song.playing && !song.rec && ft_on) ||
                                      (ci_on && ci_u % BEAT_U < BEAT_U / 4u));   /* (the count-in's beats) */
    led_put(nl, panel.btn[B_REC], song.rec != 0u || ft_on || (rec_wait && ((fm1_ms / 125u) & 1u)) ||
                                     (ui.hold_kind == 1u && ((fm1_ms / 60u) & 1u)));   /* blinks: armed; fast: clearing */
    if (is_brk(TSEL)) {                            /* the BREAK track: OCT- / OCT+ lit while ghost / hard */
        led_put(nl, panel.btn[B_OCTDN], (fm1_in.buttons & dyn_bit[0]) != 0u);
        led_put(nl, panel.btn[B_OCTUP], (fm1_in.buttons & dyn_bit[1]) != 0u);
    } else if (on_chop_page()) {                   /* the CHOP screen: OCT- / OCT+ walk its tabs */
        led_put(nl, panel.btn[B_OCTDN], chop_oct_led(0));
        led_put(nl, panel.btn[B_OCTUP], chop_oct_led(1));
    } else {
        led_put(nl, panel.btn[B_OCTDN], song.octave < 0);
        led_put(nl, panel.btn[B_OCTUP], song.octave > 0);
    }
    keys = keys_lit();
    guide = (keys_guide() | keys_notes_dim()) & ~keys;
    back = lights_keys_mask() & ~keys & ~guide;
    for (k = 0; k < 27u; k++) {
        led_put(nl, 14u + k, (int)((keys >> k) & 1u));
        led_put(dl, 14u + k, (int)((guide >> k) & 1u));
        led_put(bl, 14u + k, (int)((back >> k) & 1u));
    }
    if (lights_lvl)                                /* menu LIGHTS: every button glows, the lit ones stay full */
        for (k = 0; k < NB; k++)
            led_put(bl, panel.btn[k], 1);
    for (c = 0; c < FM1_NCOL; c++) {
        fm1_led[c] = nl[c];
        fm1_led_dim[c] = dl[c];
        fm1_led_bg[c] = (uint8_t)(bl[c] & ~nl[c]);
    }
    fm1_led_bg_ns = LIGHTS_NS[lights_lvl % LIGHTS_N];
}

/* ---------------------------------------------------------- input --- */
static int32_t accel(uint32_t role, int32_t s, int32_t range)
{
    uint32_t now = fm1_ticks(), dt = now - ui.enc_t[role];
    ui.enc_t[role] = now;
    if (range > 40 && dt < 60u * 1000u * FM1_TICKS_PER_US)
        return s * (range > 150 ? 6 : 3);
    return s;
}

/* STEP page: KNOB 1 the cursor, 2 the step's note, 3 its TIME (note, tie, rest), 4 its FLAG (accent, slide) */
static void step_edit(uint32_t slot, int32_t steps)
{
    step_t *st = &TSEL->step[ui.cursor];
    uint32_t i;
    if (is_brk(TSEL))
        return;                                           /* (the BREAK track: its screen) */
    switch (slot) {
    case 0:                                               /* STEP: the cursor */
        cursor_set(ui.cursor + steps);
        break;
    case 1:                                               /* NOTE: transpose the step */
        if (!st->n) {
            st->note[0] = last_note;
            st->n = 1;
            st->time = ST_NOTE;
            break;
        }
        for (i = 0; i < st->n; i++)
            st->note[i] = (uint8_t)clamp(st->note[i] + steps, 1, 127);
        st->time = ST_NOTE;
        last_note = st->note[0];
        break;
    case 2:
        st->time = (uint8_t)clamp((int32_t)st->time + (steps > 0 ? 1 : -1), ST_NOTE, ST_REST);
        break;
    default: {                                            /* FLAG: - / ACC / SLD / A+S */
        uint32_t f = (st->flags & SF_ACCENT ? 1u : 0u) | (st->flags & SF_SLIDE ? 2u : 0u);
        f = (uint32_t)clamp((int32_t)f + (steps > 0 ? 1 : -1), 0, 3);
        st->flags = (uint8_t)((st->flags & ~(SF_ACCENT | SF_SLIDE)) | (f & 1u ? SF_ACCENT : 0u) | (f & 2u ? SF_SLIDE : 0u));
        break;
    }
    }
}

/* a new project: every track empty, the default sounds, 90 BPM (TOOLS > NEW) */
static void project_new(void)
{
    uint32_t i;
    transport_req = 2;
    q_clear();                                       /* (Q: nothing of the old one waits) */
    panic_req = (1u << NTRK) - 1u;
    for (i = 0; i < NTRK; i++) {
        track_t *t = &trk[i];
        undo_mark(t, (undo_sess += 4u) | 3u);
        fm1_irq_off();
        track_defaults(t);
        if (i < NPART) {
            set_engine_of(t, TRK_DEF[i][0]);
            apply_preset_to(t, TRK_DEF[i][1]);
        }
        fm1_irq_on();
    }
    for (i = 0; i < G_COUNT; i++)
        if (i != G_SLOT && i != G_SYNC && i != G_MIDI && i != G_ROUTE)
            song.g[i] = GP[i].def;
    fm1_irq_off();
    cg_defaults();                                      /* the CHORDS generator too */
    bl_defaults();                                      /* and the BASS track's phrase generator */
    fm1_irq_on();
    memset(sec_card, 0, sizeof sec_card);               /* the song: no cards, FORM CLUB, VARIANT 0, SONG, from the INTRO */
    as_set(1, 0, 0);
    arr_armed = -1;
    arr_resume = AR_INTRO;
    live_song = -1;
    proj_touched = 0;                                   /* (nothing to lose yet: what is recorded from here is) */
    proj_gen++;
    song.solo = 0;
    song.octave = 0;
    sync_reload = 1;
    ui.force = 1;
}

static void edit_param(uint32_t slot, int32_t steps)
{
    int16_t *vp;
    const page_t *pg = cur_page();
    const param_desc_t *d;
    uint32_t id = pg->id[slot];
    int32_t v;
    if (is_brk(TSEL) && !page_for_brk(pg))
        return;                                           /* "BREAK TRACK": nothing to edit here */
    if (pg->scope == SC_STEP) {
        step_edit(slot, steps);
        return;
    }
    if (pg->scope == SC_TRK) {
        deck_edit(slot, steps);                           /* the DECK's macros (ui_deck.c) */
        return;
    }
    if (pg->graph == GR_BROWSE) {                         /* KNOB 1: one preset, KNOB 2: the next / previous engine */
        if (slot == 0u && !is_brk(TSEL)) {
            uint32_t total, cur = preset_pos(&total);
            if (total)
                preset_go((uint32_t)(((int32_t)cur + steps % (int32_t)total + (int32_t)total) % (int32_t)total));
        } else if (slot == 1u && !is_brk(TSEL)) {
            select_engine((TSEL->eng_req + (steps > 0 ? 1u : NENGINES - 1u)) % NENGINES);
        }
        return;
    }
    if (pg->graph == GR_USER) {                           /* KNOB 1 slot; LOAD / ERASE / SAVE: GO buttons */
        static const char *const UP_GO[3] = {"LOAD", "ERASE", "SAVE"};
        if (slot == 0u) {
            ui.uslot = (uint8_t)clamp((int32_t)ui.uslot + steps, 0, UP_SLOTS - 1);
            ui.arm = 0;
            return;
        }
        if (steps <= 0)
            return;
        if (ui.arm != 0xE0u + slot) {                     /* one detent arms, a second one within ~1.5 s acts */
            ui.arm = (uint8_t)(0xE0u + slot);
            ui.arm_t = 90;
            ui_say("AGAIN: ", UP_GO[slot - 1u]);
            return;
        }
        ui.arm = 0;
        up_ui(slot - 1u, ui.uslot);
        return;
    }
    d = page_desc(pg, slot, &vp);
    if (!d || !vp || d->max == d->min)
        return;
    v = d->names == SMP_ALL_NAMES ? smp_set_turn(*vp, accel(EN_K1 + slot, steps, d->max - d->min))   /* (SAMPLE's SET: */
        : clamp(*vp + accel(EN_K1 + slot, steps, d->max - d->min), d->min, d->max);           /* the pool's ids with none skipped) */
    *vp = (int16_t)v;
    if (pg->scope != SC_GLOBAL && p_lockable(id))
        ui.lock_par = (uint8_t)id;                        /* the SEQ layer's lock parameter: the last one touched */
    if (!v)
        return;
    if (pg->scope == SC_GLOBAL && id == G_INITSND && ui.arm != id) {   /* one detent arms, a second one within ~1.5 s
                                                           * acts (CLEAR PATTERN: at once, EDIT + OCT- undoes it) */
        *vp = 0;
        ui.arm = (uint8_t)id;
        ui.arm_t = 90;
        ui_say("AGAIN: ", d->label);
        return;
    }
    ui.arm = 0;
    if (pg->scope != SC_GLOBAL)
        return;
    switch (id) {                                         /* GO buttons (TOOLS): act, then back to 0; the songs' LOAD,
                                                           * SAVE and NEW are on the SONG screen's SET page */
    case G_CLRSEQ:
        *vp = 0;
        undo_mark(TSEL, (undo_sess += 4u) | 3u);
        fm1_irq_off();
        track_defaults_steps(TSEL);
        fm1_irq_on();
        ui_message("CLEARED: EDIT+OCT- UNDO");
        break;
    case G_INITSND:
        *vp = 0;
        set_engine(TSEL->eng_req);                              /* engine defaults + its first preset */
        ui_message("SOUND INIT");
        ui.force = 1;
        break;
    default:
        break;
    }
}

/* SEQ step entry, acid style: the keys pressed together (POLY: up to 4, MONO:
 * the last one) become the cursor step; releasing all keys moves on. Not while recording or armed:
 * the keys record live then (they would be written twice) */
static void seq_entry(uint32_t pressed)
{
    track_t *t = TSEL;
    step_t *st = &t->step[ui.cursor];
    uint32_t k;
    if (is_brk(t) || song.rec || rec_wait || ft_on)
        return;
    for (k = 0; k < 27u; k++) {
        uint32_t note;
        if (!((pressed >> k) & 1u))
            continue;
        note = kb_map(t, k);
        if (note == KB_SILENT)
            continue;
        if (!ui.entry_open) {
            ui.entry_open = 1;
            undo_mark(t, (undo_sess += 4u) | 3u);
            st->n = 0;
            st->lvl = st->rat = 0;
            st->time = ST_NOTE;
        }
        if (t->p[P_CHORD] && !t->p[P_VOICE]) {            /* chord mode (2.4.1): the chord as it sounded (CHORD+,
                                                            * VLEAD), not only its root; (not played yet: built here) */
            uint8_t c[4];
            uint32_t m, j;
            if (kb_kind[k] == KS_NOTE && kb_n[k] && kb_root[k] == note) {
                m = kb_n[k] > 4u ? 4u : kb_n[k];
                memcpy(c, kb_nt[k], m);
            } else {
                m = chord_notes(t, note, c);
            }
            for (j = 0; j < m && st->n < 4u; j++)
                st->note[st->n++] = c[j];
        } else if (t->p[P_VOICE]) {
            st->note[0] = (uint8_t)note;
            st->n = 1;
        } else if (st->n < 4u) {
            st->note[st->n++] = (uint8_t)note;
        }
        last_note = (uint8_t)note;
    }
    if (ui.entry_open && !fm1_in.notes)
        cursor_set(ui.cursor + 1);
}

/* HOME: tap on release, hold 0.7 s fires once. t0 = press time | 1,
 * bit 1 = fired (or swallowed: then the release is no tap either) */
enum { BT_NONE, BT_TAP, BT_HOLD };
static uint32_t btn_hold(uint32_t *t0, uint32_t label, uint32_t now, int hold_ok)
{
    uint32_t tap;
    if ((fm1_in.buttons >> panel.btn[label]) & 1u) {
        if (!*t0)
            *t0 = (now | 1u) & ~2u;
        else if (hold_ok && !(*t0 & 2u) && now - (*t0 & ~3u) > 700u * 1000u * FM1_TICKS_PER_US) {
            *t0 |= 2u;
            return BT_HOLD;
        }
        return BT_NONE;
    }
    tap = *t0 && !(*t0 & 2u);
    *t0 = 0;
    return tap ? BT_TAP : BT_NONE;
}

/* a layer button tapped (pressed and let go, nothing touched): its pages, as before the layers */
static void layer_tap(uint32_t layer)
{
    if (on_break_page() && brk_layer_tap(layer))       /* the BREAK screen: SEQ, ARP, EDIT, GLO, SEL pick its pages */
        return;
    if (on_chord_page() && chd_layer_tap(layer))       /* the CHORDS screen: SEL, its two pages */
        return;
    if (on_bass_page() && bss_layer_tap(layer))         /* the BASS screen: SEL, its two pages */
        return;
    if (on_chop_page() && chp_layer_tap(layer))         /* the CHOP screen: SEL, the tab seen plays */
        return;
    if (song.sel == 0u && layer == LY_SCALE) {          /* the BASS track: SEL opens its phrase generator */
        studio_open(SC_BASS);
        bss_open();
        return;
    }
    if (song.sel == 1u && layer == LY_SCALE) {          /* the CHORDS track: SEL opens its generator */
        studio_open(SC_CHORD);
        return;
    }
    if (song.sel == 2u && layer == LY_SCALE) {          /* the CHOP track: SEL opens its screen (a phrase if none) */
        studio_open(SC_CHOP);
        chop_open();
        return;
    }
    if (is_brk(TSEL) && (layer == LY_STEP || layer == LY_ROLL || layer == LY_ERASE || layer == LY_SCALE)) {
        studio_open(SC_BREAK);                          /* the BREAK track: its own pages, on its screen */
        brk_layer_open(layer);
        return;
    }
    switch (layer) {
    case LY_FX:
        open_family(FAM_FX);
        break;
    case LY_ERASE:
    case LY_STEP:
        if (layer == LY_ERASE && song.seq_mode && cur_page()->scope == SC_STEP) {
            undo_mark(TSEL, (undo_sess += 4u) | 3u);      /* STEP page: EDIT clears the step */
            step_clear(&TSEL->step[ui.cursor]);
            cursor_set(ui.cursor + 1);
            ui_message("STEP CLEARED");
            break;
        }
        open_family(layer == LY_STEP ? FAM_SEQ : FAM_EDIT);
        break;
    case LY_ROLL:
        open_family(FAM_ARP);
        break;
    case LY_SCALE:
        open_family(FAM_SCL);
        break;
    case LY_MIX:
        open_family(FAM_GLO);
        break;
    default:
        break;
    }
}

/* a layer locked open: a layer button held + HOME tapped. The layer stays with the button let go (both
 * hands free for the keys and the knobs); any other button but PLAY, REC and OCT- / OCT+ lets it go
 * (and does only that: its press is eaten) */
static uint8_t home_eat;                                  /* HOME pressed to unlock: its tap is eaten */
static void layer_unlock(void)
{
    if (ly_lock != LY_PLAY) {
        ly_lock = LY_PLAY;
        ui.force = 1;
    }
}

/* #39 of Felucca: a layer that showed lets go; KNOB 1..4 belong to no page for LY_QUIET_MS after (the knob still
 * turning as the button comes up is the layer's, not the page's under it). fm1_ms | 1, 0 = none */
#define LY_QUIET_MS 250u
static uint32_t ly_quiet_t;
static int ly_quiet(void)
{
    if (ly_quiet_t && fm1_ms - (ly_quiet_t & ~1u) >= LY_QUIET_MS)
        ly_quiet_t = 0;
    return ly_quiet_t != 0;
}
/* the layers, once a frame: which one is held (or locked), the taps on release, its keys and knobs.
 * Returns 1 while one is held or locked (the page does not take the knobs then) */
static int layers_input(uint32_t note_edges, uint32_t *pressed, uint32_t home)
{
    static uint8_t down[LY_COUNT], used[LY_COUNT];
    static uint32_t t0[LY_COUNT];
    uint32_t l, now = fm1_ms, held = LY_PLAY, eat = 0;
    if (ly_lock != LY_PLAY) {
        uint32_t keep = 1u << panel.btn[B_PLAY] | 1u << panel.btn[B_REC] | 1u << panel.btn[B_OCTDN] | 1u << panel.btn[B_OCTUP];
        eat = *pressed & ~keep;
        if (eat) {
            layer_unlock();
            if (eat & 1u << panel.btn[B_HOME])
                home_eat = 1;
            *pressed &= ~eat;
        }
    }
    for (l = LY_FX; l < LY_COUNT; l++) {
        uint32_t d = (fm1_in.buttons & ly_bit[l]) != 0u;
        if (on_break_page() && l != LY_FX) {   /* the BREAK screen: SEQ, ARP, EDIT, GLO, SEL are its pages, on the press */
            if (d && !down[l] && !ui.menu)
                layer_tap(l);
            down[l] = (uint8_t)d;
            continue;
        }
        if (d && !down[l]) {
            t0[l] = now;
            used[l] = (uint8_t)((eat & ly_bit[l]) != 0u);  /* (the press that unlocked: not a tap) */
        }
        if (d && note_edges)
            used[l] = 1;                                  /* a key while held: not a tap */
        if (!d && down[l] && !used[l] && now - t0[l] < TAP_MS && !ui.menu && !ui.confirm)
            layer_tap(l);
        down[l] = (uint8_t)d;
        if (d && held == LY_PLAY)
            held = l;
    }
    if (held != LY_PLAY && home == BT_TAP && !home_eat) {  /* held + HOME: locked open */
        ly_lock = (uint8_t)held;
        used[held] = 1;
        ui.layer = (uint8_t)held;
        ui.force = 1;
    }
    if (held == LY_PLAY && ly_lock != LY_PLAY) {
        held = ly_lock;
        used[held] = 1;
    }
    if (held != LY_PLAY && held != ui.layer && ui.layer != LY_PLAY)
        ui.layer = (uint8_t)held;                         /* (from one layer straight to another) */
    if (held == LY_PLAY) {
        while (lk_r != lk_w) {                            /* a key let go after its layer: its release only */
            uint32_t e = lk_q[lk_r % LKQ];
            lk_r++;
            if ((e >> 8) == KB_BRK) {                     /* the BREAK screen: a step / slice key, down and up */
                brk_key(e & 31u, (e >> 7) & 1u);
            } else if ((e >> 8) == KB_CHD) {              /* the CHORDS screen: a node / step key */
                chd_key(e & 31u, (e >> 7) & 1u);
            } else if ((e >> 8) == KB_CHP) {              /* the CHOP screen: a black key */
                chp_key(e & 31u, (e >> 7) & 1u);
            } else if ((e >> 8) == KB_BSS) {              /* the BASS screen: a black key */
                bss_key(e & 31u, (e >> 7) & 1u);
            } else if (!((e >> 7) & 1u)) {
                layer_key(e >> 8, e & 31u, 0);
            }
        }
        if (ui.layer != LY_PLAY) {
            ly_quiet_t = fm1_ms | 1u;                     /* a layer shown lets go: KNOB 1..4 quiet a while (#39) */
            ui.layer = LY_PLAY;
            ui.step_held = 0;
            ui.force = 1;                                 /* the page comes back */
        }
        ui.step_sess = 0;
        return 0;
    }
    ui.layer_used = used[held];
    while (lk_r != lk_w) {                                /* the keys of SEQ, SEL, GLO (seq.c) */
        uint32_t e = lk_q[lk_r % LKQ];
        lk_r++;
        used[held] = 1;
        if ((e >> 8) == KB_BRK) {                         /* (a key of the BREAK screen let go under a layer: its release) */
            brk_key(e & 31u, (e >> 7) & 1u);
            continue;
        }
        if ((e >> 8) == KB_CHD) {
            chd_key(e & 31u, (e >> 7) & 1u);
            continue;
        }
        if ((e >> 8) == KB_CHP) {
            chp_key(e & 31u, (e >> 7) & 1u);
            continue;
        }
        if ((e >> 8) == KB_BSS) {
            bss_key(e & 31u, (e >> 7) & 1u);
            continue;
        }
        layer_key(e >> 8, e & 31u, (e >> 7) & 1u);
    }
    layer_knobs(held);
    if (ui.layer_used)
        used[held] = 1;
    if (ui.layer == LY_PLAY && (used[held] || now - t0[held] >= SHOW_MS)) {
        ui.layer = (uint8_t)held;                         /* show it (not for a quick tap) */
        if (held == LY_STEP && (uint32_t)ui.step_page * 16u >= trk_len(TSEL))
            ui.step_page = 0;
    }
    if (held == LY_ERASE) {                               /* EDIT + OCT- / OCT+: undo / redo */
        uint32_t ob = 1u << panel.btn[B_OCTDN], pb = 1u << panel.btn[B_OCTUP];
        static uint32_t prev;
        uint32_t b = fm1_in.buttons & (ob | pb), press = b & ~prev;
        prev = b;
        if (press) {
            used[held] = 1;
            if (press & ob)
                ui_message(undo_swap(0) ? "UNDO" : "NOTHING TO UNDO");
            else
                ui_message(undo_swap(1) ? "REDO" : "NOTHING TO REDO");
        }
    }
    if (held == LY_STEP) {                                /* SEQ + OCT- / OCT+: the page; a step held: OCT- its nudge, locks and
                                                           * condition go, OCT+ its fill condition (normal, fill only, no fill) */
        uint32_t ob = 1u << panel.btn[B_OCTDN], pb = 1u << panel.btn[B_OCTUP];
        static uint32_t prev;
        uint32_t b = fm1_in.buttons & (ob | pb), press = b & ~prev, pages = (trk_len(TSEL) + 15u) / 16u;
        prev = b;
        if (press) {
            used[held] = 1;
            if ((press & ob) && ui.step_held)
                steps_held_clear();
            else if ((press & pb) && ui.step_held)
                steps_held_fill();
            else
                ui.step_page = (uint8_t)((ui.step_page + ((press & pb) ? 1u : pages - 1u)) % pages);
        }
    }
    return 1;
}

/* the REC screen while armed (ui_studio.c rec_screen_draw): KNOB 1 MODE free / tempo (an empty
 * project), KNOB 2 the length of the selected track (1, 2 or 4 bars), KNOB 3 START note / count;
 * MODE and START are settings of the FM-1 (panel.c lights_word: saved once stopped) */
static void rec_knobs(void)
{
    static const uint8_t LENS[3] = {16u, 32u, 64u};
    int32_t s;
    uint32_t empty = (uint32_t)project_empty(), tempo = !empty || rec_tempo;
    if ((s = panel_enc(EN_K1)) != 0 && empty) {
        rec_tempo = (uint8_t)(s > 0);
        settings_later = 1;
        ui.hot_col = 0, ui.hot_t = 40;
    }
    if ((s = panel_enc(EN_K2)) != 0 && tempo) {
        track_t *t = TSEL;
        int32_t len = t->p[P_SLEN], i, to = len;
        if (s > 0) {
            for (i = 0; i < 3; i++) if (LENS[i] > len) { to = LENS[i]; break; }
        } else {
            for (i = 2; i >= 0; i--) if (LENS[i] < len) { to = LENS[i]; break; }
        }
        t->p[P_SLEN] = (int16_t)to;
        ui.hot_col = 1, ui.hot_t = 40;
    }
    if ((s = panel_enc(EN_K3)) != 0 && tempo) {
        rec_count = (uint8_t)(s > 0);
        settings_later = 1;
        ui.hot_col = 2, ui.hot_t = 40;
    }
    panel_enc(EN_K4);
}

/* REC: acts on the press (no lag). Held 0.7 s the press is undone and a ring fills: held on to the
 * end, the selected track is cleared (EDIT + OCT- brings it back); let go before, nothing happens.
 * (SAVE: on its press, the SONG screen: ui_input) */
static void holds_input(uint32_t pressed, uint32_t now_ms)
{
    static uint8_t prev_rec, prev_wait, eaten, rec_on, save_on;   /* *_on: pressed, *_t0 its time */
    static uint32_t rec_t0, save_t0;
    uint32_t rb = 1u << panel.btn[B_REC], sb = 1u << panel.btn[B_SAVE];
    uint32_t rec_down = (fm1_in.buttons & rb) != 0u, save_down = (fm1_in.buttons & sb) != 0u;
    if (on_song_page())
        rec_down = 0, pressed &= ~rb;                     /* (the song page: REC stores a section) */
    /* REC */
    if (pressed & rb) {
        eaten = (uint8_t)ft_owns_press();                 /* (the press closed a free take: seq.c) */
        prev_rec = song.rec;
        prev_wait = rec_wait;
        rec_t0 = now_ms;
        rec_on = 1;
        rec_toggle();
    }
    if (rec_down && rec_on && !eaten && !ui.hold_kind && now_ms - rec_t0 >= 700u) {
        song.rec = prev_rec;                              /* a hold: the press is undone */
        rec_wait = prev_wait;
        ui.msg_t = 0;
        ui.hold_kind = 1;
        ui.hold_t0 = now_ms;
        ui.hold_trk = song.sel;
        ui.force = 1;
    }
    if (ui.hold_kind == 1u && rec_down && now_ms - ui.hold_t0 >= HOLD_CLEAR_MS) {
        track_t *t = &trk[ui.hold_trk % NTRK];
        char b[12] = "1 CLEARED";
        undo_mark(t, (undo_sess += 4u) | 3u);
        fm1_irq_off();
        track_defaults_steps(t);
        fm1_irq_on();
        b[0] = (char)('1' + ui.hold_trk);
        ui_say("TRACK ", b);
        proj_touched = 1;                                 /* (a button did it: the song is edited) */
        ui.hold_kind = 0;
        rec_on = 0;                                       /* (until REC is up: nothing more) */
        ui.force = 1;
    }
    if (!rec_down) {
        if (ui.hold_kind == 1u) {
            ui.hold_kind = 0;                             /* let go before the end: nothing */
            ui.force = 1;
        }
        rec_on = 0;
    }
    (void)sb, (void)save_down, (void)save_on, (void)save_t0;   /* (SAVE is a layer now: ui_layers.c) */
}

/* PLAY in STUDIO (and in the menu opened from it, and on every STUDIO screen): the loop you make, never AUTO SONG's
 * chain (song mode is set by LIVE's PLAY alone: live_play; a MIDI START: seq.c). A song LIVE started and still
 * playing here: PLAY stops it. */
static void studio_play(void)
{
    if (ft_owns_press())                                /* (it closed or dropped a free take: seq.c) */
        return;
#if FELUCCA_ARRANGER
    if (!song.playing)
        arrangement_enabled = 0;
#endif
    transport_req = song.playing ? 2 : 1;
}

static uint32_t ui_in_notes;                           /* (ui_input_body: the keys of this pass) */
static void ui_input_body(void);
/* the panel, once a frame; STUDIO's knob turns and keys mark the song edited (a load, save or NEW in the same pass
 * clears it: that one is later) */
static void ui_input(void)
{
    uint32_t turns = enc_turns, gen = proj_gen, studio = !ui.menu && !live_on;
    ui_in_notes = 0;
    ui_input_body();
    if (studio && !ui.menu && (enc_turns != turns || ui_in_notes) && proj_gen == gen)
        proj_touched = 1;
}

static void ui_input_body(void)
{
    uint32_t pressed = fm1_input_edges(0), notes = fm1_input_note_edges(), now = fm1_ticks(), id, b, k;
    uint32_t home = btn_hold(&ui.home_t0, B_HOME, now, 1);
    int32_t s;
    int layered;
    enc_hold = 0;                                       /* (panel.c: every knob readable again this pass) */
    ui_in_notes = notes;
    if (pressed || notes)
        ui_input_ms = fm1_ms;
    if (home == BT_HOLD) {                              /* HOME held: open the menu, or leave it */
        if (ui.menu) {
            menu_close();
        } else {
            layer_unlock();
            ui.menu = 1;
            ui.menu_sel = 0;
            ui.confirm = 0;
            ui.force = 1;
            song.seq_mode = 0;
        }
    }
    if (ui.menu) {                                      /* HOME / REC taps do nothing here; PLAY plays */
        if ((pressed >> panel.btn[B_PLAY]) & 1u) {
            if (live_on)
                live_play();
            else
                studio_play();
        }
        if (!ui.home_t0)
            menu_input(pressed);
        return;
    }
    if (live_on) {                                      /* FMX: the LIVE screen, its own map (ui_live.c) */
        live_input(pressed, home);
        return;
    }
    kb_live = 0;
    layered = layers_input(notes, &pressed, home);
    if (home_eat && !((fm1_in.buttons >> panel.btn[B_HOME]) & 1u)) {   /* (the HOME that unlocked: let go) */
        if (home == BT_TAP)
            home = BT_NONE;
        home_eat = 0;
    }
    pressed &= ~(ly_bit[LY_FX] | ly_bit[LY_ERASE] | ly_bit[LY_ROLL] | ly_bit[LY_STEP] | ly_bit[LY_SCALE] | ly_bit[LY_MIX]);
    holds_input(pressed, fm1_ms);
    if (((pressed >> panel.btn[B_SAVE]) & 1u) && !layered && !ui.hold_kind) {   /* SAVE: the song, always (FMX): its
                                                         * screen, the SET page (the four songs: LOAD, SAVE, NEW); on it,
                                                         * the SET page again. Held it is no layer (the sections: LIVE) */
        sg_page = 0;
        if (!on_song_page())
            studio_open(SC_SONG);
        ui.force = 1;
    }
    pressed &= ~((on_song_page() ? 0u : 1u << panel.btn[B_REC]) | (1u << panel.btn[B_SAVE]));
    kb_brk = (uint8_t)(on_break_page() && !ui.menu);    /* (seq.c: the keys are the BREAK screen's) */
    kb_chd = (uint8_t)(on_chord_page() && !ui.menu);    /* (the CHORDS screen's) */
    kb_chp = (uint8_t)(on_chop_page() && !ui.menu);     /* (the CHOP screen's: the white keys its hits) */
    kb_bss = (uint8_t)(on_bass_page() && !ui.menu);     /* (the BASS screen's: its black keys) */
    if (layered || ui.hold_kind) {                      /* a layer / a hold: the rest waits */
        if ((s = panel_enc(EN_SELECT)) != 0) {           /* (the tempo always) */
            song.g[G_BPM] = (int16_t)clamp(song.g[G_BPM] + accel(EN_SELECT, s, 200), GP[G_BPM].min, GP[G_BPM].max);
            ui.bpm_t = 40;
        }
        panel_enc(EN_ALGO);                             /* (track and sound wait: no jump afterwards) */
        panel_enc(EN_PRESET);
        if (ui.hold_kind)
            for (k = 0; k < 4u; k++)
                panel_enc(EN_K1 + k);
        pressed &= ~((1u << panel.btn[B_OCTDN]) | (1u << panel.btn[B_OCTUP]));   /* (undo, pages, ghost / hard) */
        if (!(pressed & (1u << panel.btn[B_PLAY])))
            return;
        pressed &= 1u << panel.btn[B_PLAY];             /* PLAY still plays */
        enc_hold = (1u << NE) - 1u;                     /* #102: the knobs the layer took are not read again this
                                                         * pass (a detent counted since went to the page too) */
    } else if (ly_quiet()) {                            /* #39: the turns as a layer lets go are dropped */
        for (k = 0; k < 4u; k++)
            panel_enc(EN_K1 + k);
        enc_hold |= 15u << EN_K1;
    }
    if (rec_wait && !ft_on && !ci_on) {                 /* the REC screen, armed: how it records */
        rec_knobs();                                    /* (tempo and sound still work; the track too: */
    } else if (rec_wait || ft_on) {                     /* the arm follows) */
        for (k = 0; k < 4u; k++)                        /* a take / the count-in: KNOB 1..4 edit nothing */
            panel_enc(EN_K1 + k);
    }
    if (home == BT_TAP && home_live) {                  /* SEL from LIVE: HOME goes back there (the screen let go
                                                         * as its own HOME does), not to the DECK */
        if (on_break_page())
            break_leave();
        else if (on_chord_page())
            chord_leave();
        else if (on_chop_page())
            chop_leave();
        else
            go_home();
        live_set(1);
        return;
    }
#if FELUCCA_ARRANGER
    if (on_song_page()) {
        song_screen_input(pressed, home);
        return;
    }
#endif
    if (on_break_page()) {
        break_screen_input(pressed, home);
        return;
    }
    if (on_chord_page()) {
        chord_screen_input(pressed, home);
        return;
    }
    if (on_chop_page()) {
        chop_screen_input(pressed, home);
        return;
    }
    if (on_bass_page()) {
        bass_screen_input(pressed, home);
        return;
    }
    if (home == BT_TAP) {                               /* HOME acts on release: a hold opens the menu */
        if (!ui.home && cur_page()->scope == SC_TRK && !vis_on)
            live_set(1);                                /* HOME on HOME (the DECK): LIVE (ui_live.c) */
        else {
            vis_on = 0;                                 /* (HOME again: back to the TRACKS screen) */
            go_home();
        }
    }
    cursor_fix();                                       /* LEN may have changed (knob, editor, load) */
    for (id = 0; id < 14u; id++) {
        if (!((pressed >> id) & 1u))
            continue;
        b = panel_btn_of(id);
        switch (b) {
        case B_PLAY:
            studio_play();
            break;
        case B_OCTDN:
        case B_OCTUP: {
            uint32_t both = (1u << panel.btn[B_OCTDN]) | (1u << panel.btn[B_OCTUP]);
            if (is_brk(TSEL))
                break;                                  /* the BREAK track: ghost / hard while held (seq.c) */
            if ((fm1_in.buttons & both) == both)
                song.octave = 0;
            else
                song.octave += b == B_OCTDN ? (song.octave > -3 ? -1 : 0) : (song.octave < 3 ? 1 : 0);
            break;
        }
        case B_ENV:
            open_family(FAM_ENV);
            break;
        case B_LFO:
            open_family(FAM_LFO);
            break;
        default:
            break;
        }
    }
    if (song.seq_mode && cur_page()->scope == SC_STEP)
        seq_entry(notes);

    if ((s = panel_enc(EN_PRESET)) != 0 && (ui.home || cur_page()->graph == GR_BROWSE || cur_fam() == FAM_TRK)) {
        /* PRESETS browses the selected part's presets (all engines, then user presets) on HOME, the PRESETS
         * page and TRACKS only (the BREAK track: its loop A); elsewhere a stray turn would throw away the sound
         * being edited. Every detent counts */
        uint32_t total, cur = preset_pos(&total), nl = brkp_nloops();
        if (is_brk(TSEL))
            BPV(BP_LOOPA) = (int16_t)clamp(BPV(BP_LOOPA) + s, 0, nl ? (int32_t)nl - 1 : 0);
        else if (total)
            preset_go((uint32_t)(((int32_t)cur + s % (int32_t)total + (int32_t)total) % (int32_t)total));
    }
    if ((s = panel_enc(EN_ALGO)) != 0 && !ft_on) {   /* ALGORITHM: the selected track, on every page (not in a take) */
        track_step(s);
        if (is_brk(TSEL) && !ui.home && cur_page()->scope != SC_TRK && !page_for_brk(cur_page()))
            studio_open(SC_BREAK);                      /* the BREAK track from a synth page: its screen */
    }
    if (vis_shown() && !rec_wait && !ft_on) {           /* the visualiser: SELECT its style; KNOB 1..4 (the TRACKS
                                                         * screen's, out of sight) do nothing */
        if ((s = panel_enc(EN_SELECT)) != 0)
            vis_select(s);
        for (k = 0; k < 4u; k++)
            panel_enc(EN_K1 + k);
    }
    if ((s = panel_enc(EN_SELECT)) != 0 && (rec_wait || ft_on || !page_walk(s))) {   /* SELECT: the pages of the
                                                     * family shown, else (and on the REC screen) the tempo */
        song.g[G_BPM] = (int16_t)clamp(song.g[G_BPM] + accel(EN_SELECT, s, 200), GP[G_BPM].min, GP[G_BPM].max);
        ui.bpm_t = 40;                              /* the header's BPM lights up; no message over the header */
    }
    for (k = 0; k < 4u; k++) {
        const page_t *pg = cur_page();
        int16_t *hv;
        if ((s = panel_enc(EN_K1 + k)) == 0)
            continue;
        if (ui.home || pg->scope == SC_STEP || pg->scope == SC_TRK || page_desc(pg, k, &hv) ||
            (pg->graph == GR_USER && k == 0u)) {     /* (not an empty column, nor "DRUM TRACK") */
            ui.hot_col = (uint8_t)k;
            ui.hot_t = 40;
        }
        edit_param(k, s);
    }
}

/* ---------------------------------------------------- panel setup --- */
/* 30 s without input: give up and keep the old table (a stuck key cannot hang the boot) */
#define SETUP_IDLE_MS 30000u
static void panel_setup(void)
{
    uint32_t i, used = 0, t0 = fm1_ms;
    const panel_t old = panel;
    lcd_fill(0, 0, 240, 240, C_BLACK);
    draw_text_box(0, 10, 240, &FONT_S, "HARDWARE CALIBRATION", C_WHITE, 1);
    draw_text_box(0, 30, 240, &FONT_S, "TEACH EACH BUTTON AND KNOB", C_GRAY, 1);
    while (fm1_in.buttons) {                             /* wait for OCT-/OCT+ release */
        fm1_wdt_feed();
        if (fm1_ms - t0 > SETUP_IDLE_MS)
            goto timeout;
    }
    fm1_input_edges(0);
    for (i = 0; i < NB; i++) {
        uint32_t p = 0, id;
        draw_text_box(0, 80, 240, &FONT_S, "PRESS", C_GRAY, 1);
        draw_text_box(0, 100, 240, &FONT_L, B_NAME[i], C_WHITE, 1);
        t0 = fm1_ms;
        while (!(p & ~used)) {
            fm1_wdt_feed();
            p |= fm1_input_edges(0);
            if (fm1_ms - t0 > SETUP_IDLE_MS)
                goto timeout;
        }
        for (id = 0; id < 14u; id++)
            if (((p & ~used) >> id) & 1u)
                break;
        panel.btn[i] = (uint8_t)id;
        used |= 1u << id;
    }
    used = 0;
    for (i = 0; i < NE; i++) {
        uint32_t e;
        int32_t st = 0;
        draw_text_box(0, 80, 240, &FONT_S, "TURN RIGHT", C_GRAY, 1);
        draw_text_box(0, 100, 240, &FONT_L, E_NAME[i], C_WHITE, 1);
        for (e = 0; e < 7u; e++)
            fm1_enc_take(e);
        t0 = fm1_ms;
        for (;;) {
            fm1_wdt_feed();
            if (fm1_ms - t0 > SETUP_IDLE_MS)
                goto timeout;
            for (e = 0; e < 7u; e++)
                if (!((used >> e) & 1u) && (st = fm1_enc_take(e)) != 0)
                    break;
            if (e < 7u)
                break;
        }
        panel.enc[i] = (uint8_t)e;
        panel.dir[i] = (int8_t)(st > 0 ? 1 : -1);
        used |= 1u << e;
        fm1_delay_ms(300);
        fm1_enc_take(e);
    }
    panel.magic = PANEL_MAGIC;
    lcd_fill(0, 0, 240, 240, C_BLACK);
    layers_init();
    ui.force = 1;
    return;
timeout:
    panel = old;
    lcd_fill(0, 0, 240, 240, C_BLACK);
    ui.force = 1;
    ui_message("SETUP CANCELLED");
}
