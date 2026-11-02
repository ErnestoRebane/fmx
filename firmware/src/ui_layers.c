/* SPDX-License-Identifier: GPL-3.0-only */
/* LAYERS, teenage-engineering style: hold a function button, touch a key (or turn a knob). While the
 * button is held the screen shows what the 16 white keys and KNOB 1..4 do now; tapped (pressed and
 * let go without touching anything) the button opens its pages as before. FMX draws them as LIVE's FX
 * panel: the header, a strip of what the layer acts on, the keys as 4 x 4 windows, the knobs' gauges.
 *   FX    the FX panel (lfx.c), LIVE's     knobs: AMOUNT  TIME  TONE  FEEDBACK (ui_live.c lfx_draw)
 *   EDIT  erase that sound / note (seq.c)  knobs: SHIFT  LENGTH x2 / half  TRANSPOSE   OCT- undo  OCT+ redo
 *   ARP   note repeat (seq.c roll)         knobs: RATE
 *   SEQ   steps 1..16 of the page          knobs: SOUND / NOTE  DIV  SWING  LENGTH;  a step key held:
 *         (black keys 1..4: pages)                SOUND / NOTE  LEVEL  RATCHET  NUDGE, PRESETS the value of a
 *                                                 parameter lock, ALGORITHM its parameter (the sound parameter
 *                                                 last touched first); OCT+ cycles the step's fill condition
 *                                                 (normal, fill only, no fill), OCT- clears its nudge, locks and
 *                                                 condition
 *   SEL   any key: the key of the song     knobs: CHORD  SCALE  KEYS  TRANSPOSE
 *   GLO   keys 1..4 the tracks in / out,   knobs: their levels (LIVE's lanes: BREAK BASS CHORDS CHOP)
 *         5..8 solo, 9 fill while held, 10 fill the next bar, 11 Q, the last white key: tap tempo
 * SAVE is no layer (FMX): the sections are LIVE's, the song's modes the SONG screen's; tapped, it opens that.
 * The keys' part runs in the audio ISR (seq.c layer_now: no lag, no lost press); the SEQ, SEL and GLO
 * keys come to the UI through seq.c lk_q. HOLD: REC held clears the track (a ring fills; let go
 * before and nothing happens). */
static const uint8_t LAYER_BTN[LY_COUNT] = {NB, B_FX, B_EDIT, B_ARP, B_SEQ, B_SCL, B_GLO};
static const char *const LAYER_NAME[LY_COUNT] = {"", "FX", "ERASE", "ROLL", "STEPS", "KEY", "MIX"};
static const uint8_t LIVE_LANE[4] = {TRK_BRK, 0, 1, 2};   /* the lanes top down: BREAK BASS CHORDS CHOP (LIVE, GLO) */
static const char *const LIVE_LANE_NAME[4] = {"BREAK", "BASS", "CHORDS", "CHOP"};
static const char *const TRK_NAME[NTRK] = {"BASS", "CHORDS", "CHOP", "BREAK"};   /* by track */
static void lfx_draw(void);                             /* the FX panel (ui_live.c, after this file) */
static void lfx_knob(uint32_t k, int32_t s);
static uint32_t lfx_keys_lit(void);
#define TAP_MS 450u                                     /* a press shorter than this, untouched: a tap */
#define SHOW_MS 140u                                    /* the layer shows after this (a tap does not flash it) */

/* the ISR's view of the panel: the layers' buttons, OCT- / OCT+, REC / PLAY of a free take */
static void layers_init(void)
{
    uint32_t l;
    for (l = LY_FX; l < LY_COUNT; l++)
        ly_bit[l] = 1u << panel.btn[LAYER_BTN[l]];
    dyn_bit[0] = 1u << panel.btn[B_OCTDN];
    dyn_bit[1] = 1u << panel.btn[B_OCTUP];
    ft_btn_mask = 1u << panel.btn[B_REC];
    ft_drop_mask = 1u << panel.btn[B_PLAY];
    ui.lock_par = P_ED_FLT;                             /* the lock parameter until a sound knob is turned */
}

static uint32_t key_of_white(uint32_t w)              /* white key w (0..15) -> key index */
{
    static const uint8_t K[7] = {0, 2, 4, 6, 7, 9, 11};      /* F G A B C D E */
    w &= 15u;
    return (w / 7u) * 12u + K[w % 7u];
}

/* ------------------------------------------------------------ tools --- */
/* EDIT layer knobs; each a step of one undo (the layer's hold) */
static void layer_undo_mark(track_t *t)
{
    if (!ui.step_sess)
        ui.step_sess = (undo_sess += 4u) | 3u;
    undo_mark(t, ui.step_sess);
}
static void pattern_rotate(track_t *t, int32_t d)       /* every step one later (d > 0) / earlier */
{
    uint32_t len = trk_len(t), i;
    step_t keep;
    layer_undo_mark(t);
    fm1_irq_off();
    if (d > 0) {
        keep = t->step[len - 1u];
        for (i = len - 1u; i > 0; i--)
            t->step[i] = t->step[i - 1u];
        t->step[0] = keep;
    } else {
        keep = t->step[0];
        for (i = 0; i + 1u < len; i++)
            t->step[i] = t->step[i + 1u];
        t->step[len - 1u] = keep;
    }
    fm1_irq_on();
}
static void pattern_length(track_t *t, int32_t d)       /* x2 (the pattern again after itself), or half */
{
    uint32_t len = trk_len(t), i;
    layer_undo_mark(t);
    fm1_irq_off();
    if (d > 0 && len * 2u <= NSTEP) {
        for (i = 0; i < len; i++)
            t->step[len + i] = t->step[i];
        t->p[P_SLEN] = (int16_t)(len * 2u);
    } else if (d < 0 && len >= 2u) {
        t->p[P_SLEN] = (int16_t)(len / 2u);
    }
    fm1_irq_on();
    {
        char b[8];
        fmt_int(b, t->p[P_SLEN]);
        ui_say("STEPS ", b);
    }
}
static void pattern_transpose(track_t *t, int32_t d)    /* every note a semitone up / down (synth parts) */
{
    uint32_t i, j;
    if (is_brk(t))
        return;
    layer_undo_mark(t);
    fm1_irq_off();
    for (i = 0; i < NSTEP; i++)
        for (j = 0; j < t->step[i].n && j < 4u; j++)
            t->step[i].note[j] = (uint8_t)clamp(t->step[i].note[j] + (d > 0 ? 1 : -1), 0, 127);
    fm1_irq_on();
}

/* ------------------------------------------------------------- SEQ --- */
/* a step key (white key w) of the SEQ layer, Elektron style: an empty step is set at once (BREAK: ON, the
 * generator's slice; synth: the pen, the chord / note played last); a set one goes when its key is let go,
 * unless a knob changed it meanwhile (slice, level, retrigger, note) */
static uint16_t step_pend_off;                         /* the step keys that clear their step when let go */
static int step_is_on(const track_t *t, uint32_t idx)
{
    return is_brk(t) ? t->bstep[idx].on != 0u : step_on(&t->step[idx]);
}
static void step_down(uint32_t w)
{
    track_t *t = TSEL;
    uint32_t idx = ui.step_page * 16u + w;
    if (idx >= trk_len(t))
        return;
    if (song.playing && arrangement_enabled) {
        ui_message("STOP THE SONG FIRST");
        return;
    }
    if (step_is_on(t, idx)) {
        step_pend_off |= (uint16_t)(1u << w);
        return;
    }
    layer_undo_mark(t);
    fm1_irq_off();
    if (is_brk(t)) {
        t->bstep[idx].on = 1;
    } else {
        step_t *s = &t->step[idx];
        uint32_t i, n = pen_n ? pen_n : 1u;
        memset(s, 0, sizeof *s);
        for (i = 0; i < n && i < 4u; i++)
            s->note[i] = pen_n ? pen_note[i] : last_note;
        s->n = (uint8_t)(t->p[P_VOICE] == V_POLY ? n : 1u);
        s->time = ST_NOTE;
        s->vel = 100;
    }
    t->seq_active = 1;
    fm1_irq_on();
    sync_reload = 1;
}
static void step_up(uint32_t w)
{
    track_t *t = TSEL;
    uint32_t idx = ui.step_page * 16u + w;
    if (!((step_pend_off >> w) & 1u))
        return;
    step_pend_off &= (uint16_t)~(1u << w);
    if (idx >= trk_len(t) || !step_is_on(t, idx))
        return;
    layer_undo_mark(t);
    fm1_irq_off();
    if (is_brk(t))
        memset(&t->bstep[idx], 0, sizeof t->bstep[idx]);
    else
        step_clear(&t->step[idx]);
    {                                                   /* an empty step keeps no lock, no nudge, no condition */
        lock_del(t, idx, P_COUNT);
        t->micro[idx] = 0;
        step_fill_set(t, idx, FC_NORM);
    }
    fm1_irq_on();
    sync_reload = 1;
}
/* the slice KNOB 1 picks on the BREAK track (no step held): heard when it changes */
static void pen_lane_move(int32_t s)
{
    uint8_t l = (uint8_t)clamp(pen_lane + s, 0, BRK_SLICES - 1);
    if (l != pen_lane)
        audition_slice(l, LV_NORM);
    pen_lane = l;
}
/* KNOB 1..4 with step keys held: their slice / note (BREAK: the step's own slice, 0 = the generator's; synth:
 * every note), level, ratchet (BREAK: retrigger x2 .. x8), nudge (1/64 of a step, - early + late). knob 4 =
 * PRESETS: a lock of ui.lock_par on them, made at the track's value, then moved */
static void steps_held_edit(uint32_t knob, int32_t s)
{
    track_t *t = TSEL;
    uint32_t w, i;
    layer_undo_mark(t);
    step_pend_off &= (uint16_t)~ui.step_held;            /* (edited: kept when let go) */
    fm1_irq_off();
    for (w = 0; w < 16u; w++) {
        uint32_t idx = ui.step_page * 16u + w;
        if (!((ui.step_held >> w) & 1u) || idx >= trk_len(t))
            continue;
        if (knob == 3u) {                                 /* NUDGE: the whole step (drums: every lane) */
            t->micro[idx] = (int8_t)clamp(t->micro[idx] + s, MICRO_MIN, MICRO_MAX);
            continue;
        }
        if (knob == 4u) {                                 /* LOCK (PRESETS): ui.lock_par on this step */
            uint32_t id = ui.lock_par % P_COUNT;
            if (is_brk(t) && !brk_lockable(id))
                id = ui.lock_par = P_E0;                  /* the BREAK track: CMPLX until another is picked */
            int k = lock_find(t, idx, id, 0);
            const param_desc_t *d = lock_desc(t, id);
            int32_t v = k >= 0 ? t->lock[k].val : t->p[id];
            if (!lock_set(t, idx, id, v + accel(EN_PRESET, s, d->max - d->min)))
                ui_message(p_lockable(id) ? "NO LOCK LEFT" : "NOT LOCKABLE");
            continue;
        }
        if (is_brk(t)) {
            bstep_t *d = &t->bstep[idx];
            d->on = 1;
            if (knob == 0u) {
                d->slice = (uint8_t)clamp((int32_t)d->slice + s, 0, BRK_SLICES);
                if (d->slice)
                    audition_slice(d->slice - 1u, d->lvl);
            } else if (knob == 1u) {
                d->lvl = LV_UP[clamp((int32_t)lvl_rank(d->lvl) + s, 0, 3)];
            } else {
                d->rat = (uint8_t)clamp((int32_t)d->rat + s, 0, 4);
            }
        } else {
            step_t *st = &t->step[idx];
            if (!step_on(st))
                continue;
            for (i = 0; i < st->n; i++) {
                uint32_t sh = 2u * i, lv = (st->lvl >> sh) & 3u, rt = (st->rat >> sh) & 3u;
                if (knob == 0u) {
                    st->note[i] = (uint8_t)clamp(st->note[i] + s, 0, 127);
                    continue;
                }
                if (knob == 1u)
                    lv = LV_UP[clamp((int32_t)lvl_rank(lv) + s, 0, 3)];
                else
                    rt = (uint32_t)clamp((int32_t)rt + s, 0, 3);
                st->lvl = (uint8_t)((st->lvl & ~(3u << sh)) | lv << sh);
                st->rat = (uint8_t)((st->rat & ~(3u << sh)) | rt << sh);
            }
        }
    }
    fm1_irq_on();
    sync_reload = 1;
}
/* ALGORITHM with step keys held: the lock parameter, through the lockable ones (the BREAK track: its own,
 * brk_lockable), wrapping round */
static void lock_par_step(int32_t s)
{
    const track_t *t = TSEL;
    uint32_t id = ui.lock_par % P_COUNT, guard = P_COUNT;
    do {
        id = (id + (uint32_t)P_COUNT + (uint32_t)(s > 0 ? 1 : -1)) % P_COUNT;
    } while (guard-- && (!p_lockable(id) || lock_desc(t, id)->max <= lock_desc(t, id)->min ||
                         (is_brk(t) && !brk_lockable(id))));
    ui.lock_par = (uint8_t)id;
}
/* OCT- with step keys held: their nudge, locks and fill condition go */
static void steps_held_clear(void)
{
    track_t *t = TSEL;
    uint32_t w, n = 0;
    layer_undo_mark(t);
    step_pend_off &= (uint16_t)~ui.step_held;
    fm1_irq_off();
    for (w = 0; w < 16u; w++) {
        uint32_t idx = ui.step_page * 16u + w;
        if (!((ui.step_held >> w) & 1u) || idx >= trk_len(t))
            continue;
        n += step_locked(t, idx) || step_fill(t, idx);
        lock_del(t, idx, P_COUNT);
        t->micro[idx] = 0;
        step_fill_set(t, idx, FC_NORM);
    }
    fm1_irq_on();
    sync_reload = 1;
    ui_message(n ? "NUDGE, LOCKS, FILL CLEARED" : "NO NUDGE, LOCK OR FILL");
}
/* OCT+ with step keys held: their fill condition, round: normal -> fill only -> no fill (the first one's next) */
static void steps_held_fill(void)
{
    track_t *t = TSEL;
    static const char *const MSG[3] = {"FILL: NORMAL", "FILL ONLY", "NO FILL"};
    uint32_t w, v = 0, first = 1;
    layer_undo_mark(t);
    step_pend_off &= (uint16_t)~ui.step_held;
    fm1_irq_off();
    for (w = 0; w < 16u; w++) {
        uint32_t idx = ui.step_page * 16u + w;
        if (!((ui.step_held >> w) & 1u) || idx >= trk_len(t))
            continue;
        if (first)
            v = (step_fill(t, idx) + 1u) % 3u;
        first = 0;
        step_fill_set(t, idx, v);
    }
    fm1_irq_on();
    sync_reload = 1;
    ui_message(MSG[v % 3u]);
}

/* ------------------------------------------------------------- GLO --- */
static void tap_tempo(void)
{
    uint32_t now = fm1_ms, i, sum = 0;
    if (ui.tap_n && now - ui.tap_ms[(ui.tap_n - 1u) & 3u] > 2000u)
        ui.tap_n = 0;                                   /* a pause: a new count */
    ui.tap_ms[ui.tap_n & 3u] = now;
    ui.tap_n++;
    if (ui.tap_n < 2u)
        return;
    {
        uint32_t n = ui.tap_n - 1u > 3u ? 3u : ui.tap_n - 1u;
        for (i = 0; i < n; i++)
            sum += ui.tap_ms[(ui.tap_n - 1u - i) & 3u] - ui.tap_ms[(ui.tap_n - 2u - i) & 3u];
        if (sum)
            song.g[G_BPM] = (int16_t)clamp((int32_t)((60000u * n + sum / 2u) / sum), 40, 240);
        ui.bpm_t = 40;
    }
}

/* a key of the layers the UI runs (seq.c lk_q): SEQ steps, SEL key, GLO mix */
static void layer_key(uint32_t layer, uint32_t k, uint32_t down)
{
    int32_t w = punch_key(k);
    if (layer >= LY_COUNT)
        return;                                         /* (KB_BRK: the BREAK screen's, ui_break.c) */
    if (!down) {
        if (w >= 0) {
            ui.step_held &= (uint16_t)~(1u << w);
            if (layer == LY_STEP)
                step_up((uint32_t)w);
            if (layer == LY_MIX && w == 8)
                fill_held = 0;                              /* GLO + key 9 let go: the fill ends */
        }
        return;
    }
    switch (layer) {
    case LY_STEP:
        if (w < 0) {                                    /* the first four black keys: pages 1..4 */
            static const int8_t PG[12] = {-1, 0, -1, 1, -1, 2, -1, -1, 3, -1, -1, -1};
            if (k < 12u && PG[k] >= 0 && (uint32_t)PG[k] * 16u < trk_len(TSEL))
                ui.step_page = (uint8_t)PG[k];
            return;
        }
        ui.step_held |= (uint16_t)(1u << w);
        step_down((uint32_t)w);
        return;
    case LY_SCALE: {                                    /* the key of the song: every synth part */
        uint32_t i, root = (53u + k) % 12u;
        for (i = 0; i < NPART; i++)
            trk[i].p[P_ROOT] = (int16_t)root;
        ui_say("KEY ", N_NOTE[root]);
        return;
    }
    case LY_MIX:
        if (w >= 0 && w < 4) {                          /* LIVE's lanes (with Q: on the boundary, q.c) */
            uint32_t tk = LIVE_LANE[w];
            q_param(tk, P_MUTE, !q_val(tk, P_MUTE));
            if (q_on())
                q_name(tk, q_val(tk, P_MUTE) ? "MUTE" : "IN");
        } else if (w >= 4 && w < 8) {
            q_solo(q_solo_val() ^ (1u << LIVE_LANE[w - 4]));
        } else if (w == 10) {                           /* Q: NOW, BAR, 4 BARS (q.c) */
            q_mode = (uint8_t)((q_mode + 1u) % QM_N);
            ui_say("Q ", N_Q[q_mode]);
            settings_later = 1;
        } else if (w == 8) {
            fill_held = 1;                                  /* FILL while held */
        } else if (w == 9) {
            fill_arm = (uint8_t)!fill_arm;                  /* FILL NEXT BAR (again: cancelled) */
            ui_message(fill_arm ? "FILL: NEXT BAR" : "FILL BAR OFF");
        } else if (w == 15) {
            tap_tempo();
        }
        return;
    default:
        return;
    }
}

/* KNOB 1..4 while a layer is held: what the layer gives them (the page does not see them) */
static void layer_knobs(uint32_t layer)
{
    uint32_t k;
    int32_t s;
    track_t *t = TSEL;
    if (layer == LY_STEP && ui.step_held) {              /* a step held: PRESETS the lock's value, ALGORITHM its parameter
                                                          * (neither browses a sound nor selects a track meanwhile) */
        if ((s = panel_enc(EN_PRESET)) != 0) {
            ui.layer_used = 1;
            steps_held_edit(4, s);
        }
        if ((s = panel_enc(EN_ALGO)) != 0) {
            ui.layer_used = 1;
            lock_par_step(s);
        }
    }
    for (k = 0; k < 4u; k++) {
        if ((s = panel_enc(EN_K1 + k)) == 0)
            continue;
        ui.layer_used = 1;
        ui.hot_col = (uint8_t)k;
        ui.hot_t = 40;
        switch (layer) {
        case LY_FX:                                     /* the FX panel: AMOUNT TIME TONE FEEDBACK (ui_live.c) */
            lfx_knob(k, s);
            break;
        case LY_ERASE:
            if (k == 0u)
                pattern_rotate(t, s);
            else if (k == 1u)
                pattern_length(t, s);
            else if (k == 2u)
                pattern_transpose(t, s);
            break;
        case LY_ROLL:
            if (k == 0u)
                song.g[G_ROLL] = (int16_t)clamp(song.g[G_ROLL] + s, 0, 4);
            break;
        case LY_STEP:
            if (ui.step_held) {                         /* SLICE / NOTE  LEVEL  RATCHET  NUDGE */
                steps_held_edit(k, s);
            } else if (k == 0u) {
                if (is_brk(t)) {
                    pen_lane_move(s);
                } else {
                    pen_note[0] = (uint8_t)clamp(pen_note[0] + s, 0, 127);
                    pen_n = 1;
                }
            } else if (k == 1u) {
                t->p[P_SDIV] = (int16_t)clamp(t->p[P_SDIV] + s, 0, (int32_t)NDIV_STEP - 1);   /* (up to 2BAR) */
            } else if (k == 2u) {
                t->p[P_SSWING] = (int16_t)clamp(t->p[P_SSWING] + accel(EN_K3, s, 100), 0, 100);
            } else {
                t->p[P_SLEN] = (int16_t)clamp(t->p[P_SLEN] + accel(EN_K4, s, 63), 1, NSTEP);
                if ((uint32_t)ui.step_page * 16u >= trk_len(t))
                    ui.step_page = (uint8_t)((trk_len(t) - 1u) / 16u);
            }
            break;
        case LY_SCALE:
            if (k == 0u) {
                if (!is_brk(t))
                    t->p[P_CHORD] = (int16_t)clamp(t->p[P_CHORD] + s, 0, 5);
            } else if (k == 1u) {
                uint32_t i;
                int16_t v = (int16_t)clamp(trk[0].p[P_SCALE] + s, 0, NSCALES - 1);
                for (i = 0; i < NPART; i++)
                    trk[i].p[P_SCALE] = v;
            } else if (k == 2u) {
                if (!is_brk(t))
                    t->p[P_QUANT] = (int16_t)clamp(t->p[P_QUANT] + s, 0, 2);
            } else if (!is_brk(t)) {
                t->p[P_TRANS] = (int16_t)clamp(t->p[P_TRANS] + s, -24, 24);
            }
            break;
        case LY_MIX: {                                  /* the lanes' levels */
            int16_t *lv = &trk[LIVE_LANE[k]].p[P_LEVEL];
            *lv = (int16_t)clamp(*lv + accel(EN_K1 + k, s, 127), 0, 127);
            break;
        }
        default:
            break;
        }
    }
}

/* --------------------------------------------------------- drawing --- */
/* FMX: a layer is drawn as LIVE's FX panel is: the header (the layer's name), a strip of what it acts on (y 40..63: the
 * track, the page, a message; locked open: a padlock), the 16 white keys as windows 4 x 4 (y 62..177), the knobs'
 * gauges and a hint line (y 178.., as the pages') */
typedef struct {
    char lab[8];
    uint16_t edge, fill, fg;     /* the window's bevel (0: no window, the key does nothing here), a fill inside (0: none), the text */
    uint8_t marks;               /* small marks under the label (a ratchet), 0 = none */
    uint8_t tag;                 /* a dot in the top right corner (a nudge or a lock on the step), 0 = none */
    uint8_t cond;                /* top left: the step's fill condition (FC_FILL an "F", FC_NOFILL an "x"), 0 = none */
} tile_t;
static void tile_set(tile_t *t, uint16_t edge, uint16_t fill, uint16_t fg)
{
    t->edge = edge;
    t->fill = fill;
    t->fg = fg;
}
static uint16_t tile_glow(uint16_t c) { return mix565_ps(C_BLACK, c, 110); }   /* a lit window's fill (the FX panel's) */

/* the 16 windows in two bands (white keys 1..8, 9..16), each drawn again only when it changed */
static void tiles_draw(const tile_t *tl, uint32_t cache[2])
{
    uint32_t r, half, sig[2] = {7u, 11u};
    for (r = 0; r < 16u; r++)
        sig[r / 8u] = studio_hash(sig[r / 8u] * 31u + tl[r].edge * 3u + tl[r].fill * 5u + tl[r].fg * 7u + tl[r].marks +
                                  tl[r].tag * 11u + tl[r].cond * 13u, tl[r].lab);
    for (half = 0; half < 2u; half++) {
        if (!ui.force && sig[half] == cache[half])
            continue;
        cache[half] = sig[half];
        cv_begin(240, 58, C_BLACK);
        ps_ground(62 + (int32_t)half * 58);
        for (r = half * 8u; r < half * 8u + 8u; r++) {
            const tile_t *t = &tl[r];
            int32_t x = 3 + (int32_t)(r % 4u) * 59, y = 1 + (int32_t)(r / 4u - half * 2u) * 29;
            uint32_t m;
            if (!t->edge)
                continue;
            ps_win(x, y, 56, 27, t->edge);
            if (t->fill)
                cv_rect(x + 2, y + 2, 52, 23, t->fill);
            te_text_c(x + 28, y + 5, t->lab, t->fg);
            for (m = 0; m < t->marks; m++)
                cv_rect(x + 22 + (int32_t)m * 5, y + 21, 3, 2, t->fg);
            if (t->tag)
                cv_rect(x + 48, y + 4, 3, 3, t->fg);
            if (t->cond == FC_FILL) {                   /* an F, 5 x 7: plays in a fill only */
                cv_rect(x + 5, y + 4, 2, 7, t->fg);
                cv_rect(x + 5, y + 4, 5, 2, t->fg);
                cv_rect(x + 5, y + 7, 4, 2, t->fg);
            } else if (t->cond == FC_NOFILL) {          /* an x, 7 x 7: silent in a fill */
                int32_t d;
                for (d = 0; d < 6; d++) {
                    cv_rect(x + 5 + d, y + 4 + d, 2, 2, t->fg);
                    cv_rect(x + 10 - d, y + 4 + d, 2, 2, t->fg);
                }
            }
        }
        cv_blit(0, (uint32_t)(62 + (int32_t)half * 58));
    }
}

/* the strip under the header: what the layer acts on (a window in its colour: the track, the key), a line (a message
 * instead, yellow), and a padlock at the right while it is locked open (HOME: any button lets it go) */
static void lfx_lock(int32_t x, int32_t y, uint16_t c);  /* (ui_live.c) */
static void layer_strip(const char *chip, uint16_t cc, const char *sub, uint32_t *cache)
{
    uint32_t locked = ly_lock != LY_PLAY;
    const char *s = ui.msg_t ? ui.msg : sub;
    uint32_t sig = studio_hash(studio_hash(cc * 3u + (ui.msg_t != 0) * 7u + locked * 7919u, chip), s);
    int32_t x = 6;
    if (!ui.force && sig == *cache)
        return;
    *cache = sig;
    cv_begin(240, 22, C_BLACK);
    ps_ground(40);
    if (chip[0]) {
        int32_t w = text_w(&FONT_S, chip) + 12;
        ps_win(3, 1, w, 20, cc);
        te_text_c(3 + w / 2, 3, chip, cc);
        x = 3 + w + 6;
    }
    ps_text(x, 3, &FONT_S, s, ui.msg_t ? PS_YEL : PS_LBL);
    if (locked) {
        lfx_lock(225, 5, PS_INK);
        lfx_lock(224, 4, PS_YEL);
    }
    cv_blit(0, 40);
}

static const char *lock_label(const track_t *t, uint32_t id)   /* the lock parameter's name, lower case */
{
    static char b[8];
    te_lower(b, track_desc(t, id % P_COUNT)->label, 8);
    return b;
}

static uint8_t layer_shown;                              /* the screen holds a layer (or a hold) */
static const char *layer_sub_shown = "";                 /* the strip's line last drawn (the host tests read it) */
static void layer_screen_draw(void)
{
    static uint32_t head, strip, tiles[2], foot;
    static tile_t tl[16];
    static char v[4][10], sub[24], chip[8];
    const char *lab[4] = {"", "", "", ""}, *val[4] = {v[0], v[1], v[2], v[3]}, *hint = 0;
    int32_t ratio[4] = {-1, -1, -1, -1};
    uint32_t i, layer = ui.layer, sel = song.sel % NTRK;
    track_t *t = TSEL;
    uint16_t col = TRK_COL[sel], cc = col;               /* the title's colour; the strip's window */
    if (!layer_shown) {
        lcd_fill(0, 0, 240, 240, C_BLACK);
        ui.force = 1;
        layer_shown = 1;
    }
    if (layer == LY_FX) {                               /* the FX panel: LIVE's (ui_live.c) */
        lfx_draw();
        return;
    }
    for (i = 0; i < 4u; i++)
        v[i][0] = 0;
    memset(tl, 0, sizeof tl);
    sub[0] = 0;
    str_cpy(chip, TRK_NAME[sel], sizeof chip);          /* (most layers act on the track in focus) */
    switch (layer) {
    case LY_ERASE:
    case LY_ROLL: {                                     /* the keys' sounds: held lit, in the pattern framed */
        uint32_t held = fm1_in.notes;
        uint16_t lc = layer == LY_ERASE ? PS_RED : col;
        col = lc;
        for (i = 0; i < 16u; i++) {
            uint32_t k = key_of_white(i), down = (held >> k) & 1u, present = 0, j, silent = 0;
            if (is_brk(t)) {                            /* the key's slice */
                uint32_t sl = brk_slice_of_key(k);
                str_cpy(tl[i].lab, "SL ", 8);
                fmt_int(tl[i].lab + 3, (int32_t)sl + 1);
                for (j = 0; j < trk_len(t); j++)
                    present |= t->bstep[j].slice == sl + 1u;
            } else {
                uint32_t n = kb_map(t, k);
                if (n == KB_SILENT) {
                    str_cpy(tl[i].lab, "-", 8);
                    silent = 1;
                } else {
                    note_name(tl[i].lab, n);
                    for (j = 0; j < trk_len(t); j++)
                        if (t->step[j].time == ST_NOTE) {
                            uint32_t q;
                            for (q = 0; q < t->step[j].n; q++)
                                present |= t->step[j].note[q] == n;
                        }
                }
            }
            if (silent)
                tile_set(&tl[i], PS_DIM, 0, PS_DIM);
            else if (down)
                tile_set(&tl[i], C_WHITE, tile_glow(lc), PS_TXT);
            else if (present)
                tile_set(&tl[i], lc, 0, PS_TXT);
            else
                tile_set(&tl[i], PS_LINE, 0, PS_LBL);
        }
        if (layer == LY_ERASE) {
            lab[0] = "shift", lab[1] = "length", lab[2] = is_brk(t) ? "" : "transp";
            str_cpy(v[0], "<  >", 8);
            fmt_int(v[1], t->p[P_SLEN]);
            str_cpy(v[2], is_brk(t) ? "" : "-  +", 8);
            str_cpy(sub, song.playing ? "erase as it plays" : "erase every step", sizeof sub);
            hint = undo.valid ? (undo.undone ? "OCT+ REDO" : "OCT- UNDO  OCT+ REDO") : "A KEY HELD: ITS NOTES GO";
        } else {
            lab[0] = "rate";
            str_cpy(v[0], N_ROLL[clamp(song.g[G_ROLL], 0, 4)], 8);
            ratio[0] = song.g[G_ROLL] * 250;
            str_cpy(sub, "note repeat", sizeof sub);
            hint = "A KEY HELD: IT ROLLS";
        }
        break;
    }
    case LY_STEP: {                                     /* the 16 steps of the page: lit by level, the playhead white */
        uint32_t len = trk_len(t), page = ui.step_page;
        for (i = 0; i < 16u; i++) {
            uint32_t idx = page * 16u + i, on, lv = LV_NORM, rt = 0;
            if (idx >= len)
                continue;                               /* (past the pattern: no window) */
            if (is_brk(t)) {
                const bstep_t *d = &t->bstep[idx];
                on = d->on;
                if (on) {
                    lv = d->lvl & 3u;
                    rt = d->rat > 3u ? 3u : d->rat;
                    if (d->slice) {                     /* its own slice */
                        str_cpy(tl[i].lab, "S", 8);
                        fmt_int(tl[i].lab + 1, d->slice);
                    }
                }
            } else {
                const step_t *st = &t->step[idx];
                on = step_on(st);
                if (on) {
                    lv = st->lvl & 3u;
                    rt = st->rat & 3u;
                    note_name(tl[i].lab, st->note[0]);
                } else if (st->time == ST_TIE) {
                    str_cpy(tl[i].lab, "--", 8);
                }
            }
            if (!tl[i].lab[0])
                fmt_int(tl[i].lab, (int32_t)idx + 1);
            if (on) {                                   /* the level: ghost, soft, normal, hard ever brighter */
                int32_t k = lv == LV_GHOST ? 50 : lv == LV_SOFT ? 80 : lv == LV_HARD ? 190 : 120;
                tile_set(&tl[i], col, mix565_ps(C_BLACK, col, k), lv == LV_HARD ? PS_INK : PS_TXT);
            } else {
                tile_set(&tl[i], PS_LINE, 0, PS_DIM);
            }
            tl[i].marks = (uint8_t)(on ? rt : 0u);
            tl[i].tag = (uint8_t)step_locked(t, idx);   /* a nudge or a lock on it */
            tl[i].cond = (uint8_t)(step_fill(t, idx) % 3u);   /* its fill condition */
            if (song.playing && idx == t->seq_idx)
                tl[i].edge = C_WHITE;
            if ((ui.step_held >> i) & 1u)
                tl[i].edge = PS_YEL;
        }
        if (len > 16u) {
            char p[4] = {'1', '/', '1', 0};
            p[0] = (char)('1' + page);
            p[2] = (char)('0' + (len + 15u) / 16u);
            str_cpy(sub, "page ", sizeof sub);
            str_cpy(sub + 5, p, 4);
        }
        lab[0] = is_brk(t) ? "slice" : "note";
        if (is_brk(t)) {
            str_cpy(v[0], "SL ", 8);
            fmt_int(v[0] + 3, pen_lane + 1);
        } else {
            note_name(v[0], pen_note[0]);
        }
        ratio[0] = is_brk(t) ? pen_lane * 1000 / (BRK_SLICES - 1) : pen_note[0] * 1000 / 127;
        if (ui.step_held) {                             /* the first step held: its nudge; the strip: its lock */
            uint32_t id = ui.lock_par % P_COUNT, w, idx;
            int k;
            for (w = 0; w < 16u && !((ui.step_held >> w) & 1u); w++)
                ;
            idx = (page * 16u + w) % NSTEP;
            lab[1] = "level", lab[2] = "ratchet", lab[3] = "nudge";
            str_cpy(v[1], "-  +", 8);
            str_cpy(v[2], "x1 x4", 8);
            {
                int32_t m = t->micro[idx];
                if (m > 0) {
                    v[3][0] = '+';
                    fmt_int(v[3] + 1, m);
                } else {
                    fmt_int(v[3], m);
                }
                ratio[3] = (m - MICRO_MIN) * 1000 / (MICRO_MAX - MICRO_MIN);
            }
            str_cpy(sub, "lock ", sizeof sub);          /* "lock flt +12" / "lock flt --" (PRESETS, ALGORITHM) */
            str_cpy(sub + 5, lock_label(t, id), 8);
            str_cpy(sub + str_len(sub), " ", 2);
            k = lock_find(t, idx, id, 0);
            if (k >= 0) {
                const char *u;
                char b[8];
                param_format(lock_desc(t, id), t->lock[k].val, b, &u);
                te_lower(sub + str_len(sub), b, 8);
            } else {
                str_cpy(sub + str_len(sub), "--", 3);
            }
            hint = "OCT- CLEAR  OCT+ FILL";
        } else {
            lab[1] = "div", lab[2] = "swing", lab[3] = "steps";
            str_cpy(v[1], N_SDIV[t->p[P_SDIV] % NDIV_STEP], 8);
            swing_str(v[2], t->p[P_SSWING]);
            fmt_int(v[3], t->p[P_SLEN]);
            ratio[1] = t->p[P_SDIV] * 1000 / (NDIV_STEP - 1);
            ratio[2] = t->p[P_SSWING] * 10;
            ratio[3] = (t->p[P_SLEN] - 1) * 1000 / 63;
            if (!sub[0])
                str_cpy(sub, "a key: a step", sizeof sub);
            hint = len > 16u ? "A STEP HELD: EDIT  OCT: PAGE" : "A STEP HELD: EDIT IT";
        }
        break;
    }
    case LY_SCALE: {                                    /* the white keys' notes / chords; the root lit */
        uint32_t root = (uint32_t)trk[0].p[P_ROOT] % 12u, mask = SCALE_MASK[clamp(trk[0].p[P_SCALE], 0, NSCALES - 1)];
        col = cc = TRK_COL[1];                          /* (the key is the CHORDS generator's first: its colour) */
        str_cpy(chip, N_NOTE[root], sizeof chip);
        str_cpy(sub, "scale ", sizeof sub);
        te_lower(sub + 6, N_SCALE[clamp(trk[0].p[P_SCALE], 0, NSCALES - 1)], 8);
        for (i = 0; i < 16u; i++) {
            uint32_t k = key_of_white(i), pc = (53u + k) % 12u, in = (mask >> ((pc + 12u - root) % 12u)) & 1u;
            if (!is_brk(t) && t->p[P_CHORD]) {       /* chord mode: the chord this key plays (the i chord lit) */
                uint8_t c[4];
                uint32_t n = kb_map(t, k), m;
                if (n != KB_SILENT && (m = chord_notes(t, n, c)) != 0u) {
                    uint32_t third = m > 1u ? (uint32_t)(c[1] - c[0]) : 4u;
                    str_cpy(tl[i].lab, N_NOTE[c[0] % 12u], 8);
                    if (t->p[P_CHORD] == 5)
                        str_cpy(tl[i].lab + str_len(tl[i].lab), "5", 2);
                    else if (third == 3u)
                        str_cpy(tl[i].lab + str_len(tl[i].lab), "m", 2);
                    pc = c[0] % 12u;
                    in = 1;
                }
            } else {
                str_cpy(tl[i].lab, N_NOTE[pc], 8);
            }
            if (pc == root)
                tile_set(&tl[i], col, tile_glow(col), PS_TXT);
            else
                tile_set(&tl[i], PS_LINE, 0, in ? PS_TXT : PS_DIM);
        }
        lab[0] = "chord", lab[1] = "scale", lab[2] = "keys", lab[3] = "transp";
        te_lower(v[0], N_CHORD[clamp(t->p[P_CHORD], 0, 5)], 8);
        te_lower(v[1], N_SCALE[clamp(trk[0].p[P_SCALE], 0, NSCALES - 1)], 8);
        te_lower(v[2], N_QUANT[clamp(t->p[P_QUANT], 0, 2)], 8);
        fmt_int(v[3], t->p[P_TRANS]);
        if (is_brk(t))                                  /* (the BREAK: only SCALE, the song's, turns here) */
            lab[0] = lab[2] = lab[3] = "";
        ratio[0] = t->p[P_CHORD] * 200;
        ratio[1] = trk[0].p[P_SCALE] * 1000 / (NSCALES - 1);
        ratio[2] = t->p[P_QUANT] * 500;
        ratio[3] = (t->p[P_TRANS] + 24) * 1000 / 48;
        hint = "A KEY: THE SONG IN IT";
        break;
    }
    case LY_MIX: {                                      /* the tracks as LIVE's lanes: in / out, solo; fill, Q, tap */
        col = PS_TXT;
        chip[0] = 0;
        str_cpy(sub, "1-4 in/out  5-8 solo", sizeof sub);
        for (i = 0; i < 4u; i++) {
            uint32_t tk = LIVE_LANE[i];
            int m = q_val(tk, P_MUTE) != 0, wait = q_val(tk, P_MUTE) != trk[tk].p[P_MUTE], so = (q_solo_val() >> tk) & 1u;
            uint16_t c = TRK_COL[tk];                   /* (what will be, with Q: waiting framed yellow) */
            str_cpy(tl[i].lab, LIVE_LANE_NAME[i], 8);
            if (m)
                tile_set(&tl[i], wait ? PS_YEL : PS_DIM, 0, PS_DIM);
            else
                tile_set(&tl[i], wait ? PS_YEL : c, tile_glow(c), PS_TXT);
            str_cpy(tl[4 + i].lab, "SOLO", 8);
            if (so)
                tile_set(&tl[4 + i], C_WHITE, tile_glow(c), PS_TXT);
            else
                tile_set(&tl[4 + i], PS_LINE, 0, c);
        }
        str_cpy(tl[8].lab, "FILL", 8);                  /* key 9: held = a fill; key 10: the next bar is one */
        tile_set(&tl[8], fill_now ? C_WHITE : PS_LINE, fill_now ? tile_glow(PS_TXT) : 0, fill_now ? PS_TXT : PS_LBL);
        str_cpy(tl[9].lab, "BAR", 8);
        tile_set(&tl[9], fill_bar_on ? C_WHITE : fill_arm ? PS_YEL : PS_LINE, fill_bar_on ? tile_glow(PS_TXT) : 0,
                 fill_bar_on ? PS_TXT : fill_arm ? PS_YEL : PS_LBL);
        str_cpy(tl[10].lab, q_mode == QM_4BAR ? "Q 4" : q_mode == QM_BAR ? "Q 1" : "Q NOW", 8);   /* key 11: Q, as LIVE */
        tile_set(&tl[10], PS_YEL, 0, PS_YEL);
        str_cpy(tl[15].lab, "TAP", 8);                  /* the last key: tap tempo, lit on the beat */
        {
            int beat = song.playing && clk_pos < BEAT_U / 4u;
            tile_set(&tl[15], beat ? C_WHITE : PS_LINE, beat ? tile_glow(PS_TXT) : 0, beat ? PS_TXT : PS_LBL);
        }
        for (i = 0; i < 4u; i++) {
            static const char *const L[4] = {"break", "bass", "chords", "chop"};
            uint32_t lv = trk_level(LIVE_LANE[i]);
            lab[i] = L[i];
            fmt_int(v[i], (int32_t)lv * 100 / 127);
            ratio[i] = (int32_t)lv * 1000 / 127;
        }
        {   /* (a column is a track here: its gauge in the track's colour, as LIVE's lanes) */
            static uint16_t mc[4];
            for (i = 0; i < 4u; i++)
                mc[i] = TRK_COL[LIVE_LANE[i]];
            te_dial_col = mc;
        }
        hint = "KNOBS: THE LEVELS";
        break;
    }
    default:
        break;
    }
    layer_sub_shown = sub;
    te_header(LAYER_NAME[layer % LY_COUNT], col, &head);
    layer_strip(chip, cc, sub, &strip);
    tiles_draw(tl, tiles);
    {   /* (a message shows in the strip: the gauges stay) */
        uint8_t m = ui.msg_t;
        ui.msg_t = 0;
        te_hint_txt = hint;
        te_dials(178, lab, val, ratio, layer * 7919u, &foot);
        ui.msg_t = m;
    }
}

/* HOLD to confirm: a ring fills while REC (clear the track) is held */
#define HOLD_CLEAR_MS 1300u                             /* after the 0.7 s that make REC a hold */
#define HOLD_SAVE_MS 1000u
static void hold_screen_draw(void)
{
    static uint32_t head, cache, foot;
    static const char *const NONE[4] = {"", "", "", ""};
    static const int32_t NO_RATIO[4] = {-1, -1, -1, -1};
    uint32_t clear = ui.hold_kind == 1u, span = clear ? HOLD_CLEAR_MS : HOLD_SAVE_MS, el = fm1_ms - ui.hold_t0;
    uint32_t tk = ui.hold_trk % NTRK;
    int32_t ratio = el >= span ? 1000 : (int32_t)(el * 1000u / span);
    uint32_t sig = ui.hold_kind * 7u + (uint32_t)ratio / 20u + tk * 131u;
    uint16_t c = clear ? PS_RED : PS_TXT;
    if (!layer_shown) {
        lcd_fill(0, 0, 240, 240, C_BLACK);
        layer_shown = 1;
        ui.force = 1;
    }
    te_header(clear ? "CLEAR" : "SAVE", c, &head);
    if (ui.force || sig != cache) {
        cache = sig;
        cv_begin(240, 124, C_BLACK);
        ps_ground(40);
        {   /* the ring: 36 px, 7 thick, filling clockwise from the top */
            int32_t a, rr, end = ratio * 1024 / 1000;
            for (a = 0; a < 1024; a += 2) {
                int32_t co = SINE[((uint32_t)a + 768u) & 1023u], si = SINE[(uint32_t)a & 1023u];   /* from 12 o'clock */
                uint16_t rc = a <= end ? c : PS_DIM;
                for (rr = 30; rr <= 36; rr++)
                    cv_pset(120 + ((si * rr) >> 15), 50 + ((co * rr) >> 15), rc);
            }
        }
        if (clear) {                                    /* the track it clears, in its window */
            const char *n = TRK_NAME[tk];
            int32_t w = text_w(&FONT_S, n) + 16;
            ps_win(120 - w / 2, 96, w, 22, TRK_COL[tk]);
            te_text_c(120, 99, n, TRK_COL[tk]);
        } else {
            te_text_c(120, 99, "THE PROJECT", PS_TXT);
        }
        cv_blit(0, 40);
    }
    te_hint_txt = clear ? "LET GO: NOTHING GOES" : "LET GO: NOT SAVED";
    {
        const char *val[4] = {"", "", "", ""};
        te_dials(164, NONE, val, NO_RATIO, sig, &foot);
    }
}
