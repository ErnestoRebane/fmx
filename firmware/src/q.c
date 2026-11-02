/* SPDX-License-Identifier: GPL-3.0-only */
/* Q (FMX, the UI mockup's): with Q at BAR (or 4 BARS) and the transport running, what changes the music waits for the
 * next bar (or the next four-bar phrase from PLAY) instead of jumping in: a sound (PRESETS), the BREAK's STYLE and
 * loops, the CHORDS generator's settings, the BASS phrase written again, the mutes and the solos. The UI queues them
 * here (q_param, q_cg, q_steps: the main loop, a short stretch with the audio IRQ off each); the audio ISR applies the
 * lot on the boundary, before the steps of that block (seq.c events_block), so the downbeat plays it. Knobs that sweep
 * (FILTER, MANGLE, levels), the moves and the FX panel never wait. Stopped, or Q at NOW: at once, as ever. GLO + key 11 steps
 * Q round (NOW, BAR, 4 BARS); it is kept with the settings (panel.c). */
enum { QM_NOW, QM_BAR, QM_4BAR, QM_N };
static const char *const N_Q[QM_N] = {"NOW", "BAR", "4 BARS"};
static uint8_t q_mode;                              /* (panel.c: the settings word) */
#define QW_N 96u
#define QX_ENG 0xF0u                                /* a write's id past the parameters: the engine, the preset */
#define QX_PRESET 0xF1u
static struct {
    uint8_t n;                                      /* the parameter writes waiting */
    struct { uint8_t trk, id; int16_t v; } w[QW_N];
    uint8_t panic;                                  /* tracks to release on the boundary (a new sound) */
    uint8_t solo, solo_set;                         /* a solo mask waiting */
    uint8_t cg_set, st_set;                         /* the CHORDS generator; the BASS phrase */
    chord_cfg_t cg;
    uint8_t prog[CG_MAX];
    step_t st[BL_STEPS];                            /* the BASS phrase */
    uint8_t what_set;                               /* tracks with a name to show (yellow) */
    char what[NTRK][12];
    volatile uint32_t gen;                          /* bumped on a queue and on the boundary (the screens) */
} qq;

#if FELUCCA_ARRANGER
static void song_keep_p(uint32_t k, uint32_t id, int32_t v);   /* arranger_scene.c: song mode's loop you made has it too */
static void song_keep_turn(uint32_t k, uint32_t id, int32_t d, int32_t lo, int32_t hi);
static void song_keep_eng(uint32_t k, uint32_t e, uint32_t pre);
static void song_keep_trk(uint32_t k, int steps);
#else
static void song_keep_p(uint32_t k, uint32_t id, int32_t v) { (void)k, (void)id, (void)v; }
static void song_keep_turn(uint32_t k, uint32_t id, int32_t d, int32_t lo, int32_t hi) { (void)k, (void)id, (void)d, (void)lo, (void)hi; }
static void song_keep_eng(uint32_t k, uint32_t e, uint32_t pre) { (void)k, (void)e, (void)pre; }
static void song_keep_trk(uint32_t k, int steps) { (void)k, (void)steps; }
#endif
/* the main loop's: the same, with the audio IRQ off (the ISR reads what the sections put back from it) */
static void song_keep_now(uint32_t k, int steps)
{
    fm1_irq_off();
    song_keep_trk(k, steps);
    fm1_irq_on();
}

static int q_on(void) { return q_mode != QM_NOW && song.playing; }
static int q_waits(void) { return qq.n || qq.solo_set || qq.cg_set || qq.st_set; }
/* the bar (1..4 of the phrase from PLAY) what waits lands on */
static uint32_t q_bar_at(void) { return q_mode == QM_4BAR ? 1u : ((clk_beat >> 2) + 1u) % 4u + 1u; }

/* the value parameter id of track k will have (what waits, else what is) */
static int16_t q_val(uint32_t k, uint32_t id)
{
    uint32_t i;
    for (i = 0; i < qq.n; i++)
        if (qq.w[i].trk == k && qq.w[i].id == id)
            return qq.w[i].v;
    return id < P_COUNT ? trk[k % NTRK].p[id] : 0;
}
/* does a write for parameter id (or QX_*) of track k wait? Its value in *v. (A scroll starts from what waits, so
 * it goes on from the last turn instead of from the sound that plays until the bar.) */
static int q_pend(uint32_t k, uint32_t id, int16_t *v)
{
    uint32_t i;
    for (i = 0; i < qq.n; i++)
        if (qq.w[i].trk == k && qq.w[i].id == id) {
            *v = qq.w[i].v;
            return 1;
        }
    return 0;
}
/* parameter id (or QX_*) of track k to v on the boundary; with Q off: at once (parameters only) */
static void q_param(uint32_t k, uint32_t id, int32_t v)
{
    uint32_t i;
    if (!q_on()) {
        if (id < P_COUNT) {
            trk[k % NTRK].p[id] = (int16_t)v;
            song_keep_p(k, id, v);
        }
        return;
    }
    fm1_irq_off();
    for (i = 0; i < qq.n && !(qq.w[i].trk == k && qq.w[i].id == id); i++)
        ;
    if (i < QW_N) {
        qq.w[i].trk = (uint8_t)k;
        qq.w[i].id = (uint8_t)id;
        qq.w[i].v = (int16_t)v;
        if (i == qq.n)
            qq.n++;
    }
    qq.gen++;
    fm1_irq_on();
}
static void q_name(uint32_t k, const char *s)       /* what to show waiting on track k */
{
    str_cpy(qq.what[k % NTRK], s, sizeof qq.what[0]);
    qq.what_set |= (uint8_t)(1u << (k % NTRK));
}
/* p (a whole set of the track's parameters) for track k: the ones that differ from what it will have, queued */
static void q_params(uint32_t k, const int16_t *p)
{
    uint32_t i;
    for (i = 0; i < P_COUNT; i++)
        if (p[i] != q_val(k, i))
            q_param(k, i, p[i]);
}
static void q_solo(uint32_t mask)
{
    if (!q_on()) {
        song.solo = (uint8_t)mask;
        return;
    }
    fm1_irq_off();
    qq.solo = (uint8_t)mask;
    qq.solo_set = 1;
    qq.gen++;
    fm1_irq_on();
}
static uint32_t q_solo_val(void) { return qq.solo_set ? qq.solo : song.solo; }
/* the CHORDS generator's settings to edit: what waits (from what is, the first time), or the live ones with Q off */
static chord_cfg_t *q_cg(void)
{
    if (!q_on())
        return &cg;
    if (!qq.cg_set) {
        fm1_irq_off();
        qq.cg = cg;
        fm1_irq_on();
    }
    return &qq.cg;
}
/* after an edit of q_cg(): its progression made now, to swap in on the boundary (with Q off: cg_poll's, as ever) */
static void q_cg_done(void)
{
    uint8_t p[CG_MAX];
    chord_cfg_t live;
    if (!q_on())
        return;
    fm1_irq_off();
    live = cg;
    cg = qq.cg;                                     /* (cg_generate reads the settings: these, a moment) */
    cg_generate(p, cg_mask());
    cg = live;
    memcpy(qq.prog, p, sizeof p);
    qq.cg_set = 1;
    qq.gen++;
    fm1_irq_on();
}
/* the BASS phrase written (bassline.c bl_write) on the boundary */
static void q_steps(uint32_t k, const step_t *st)
{
    (void)k;
    fm1_irq_off();
    memcpy(qq.st, st, sizeof qq.st);
    qq.st_set = 1;
    qq.gen++;
    fm1_irq_on();
}
/* nothing waits for track k (a user preset loaded at once must not be thrown over by a preset queued before it) */
static void q_drop(uint32_t k)
{
    uint32_t i, n = 0;
    fm1_irq_off();
    for (i = 0; i < qq.n; i++)
        if (qq.w[i].trk != k)
            qq.w[n++] = qq.w[i];
    qq.n = (uint8_t)n;
    qq.panic &= (uint8_t)~(1u << (k % NTRK));
    qq.what_set &= (uint8_t)~(1u << (k % NTRK));
    qq.gen++;
    fm1_irq_on();
}
static void q_clear(void)                           /* nothing waits (a load, NEW, stop) */
{
    fm1_irq_off();
    qq.n = 0;
    qq.panic = qq.solo_set = qq.cg_set = qq.st_set = qq.what_set = 0;
    qq.gen++;
    fm1_irq_on();
}

/* the audio ISR (events_block): on the boundary, before the block's steps, or at once when Q is off or stopped */
static void q_block(void)
{
    uint32_t i;
    if (!q_waits())
        return;
    if (q_on() && !(!(clk_beat & 3u) && (clk_beat >> 2) != fill_last_bar && (q_mode != QM_4BAR || !((clk_beat >> 2) & 3u))))
        return;                                     /* (not the boundary yet: fill_last_bar is this bar once seen) */
    for (i = 0; i < qq.n; i++) {
        track_t *t = &trk[qq.w[i].trk % NTRK];
        if (qq.w[i].id < P_COUNT) {
            t->p[qq.w[i].id] = qq.w[i].v;
            song_keep_p(qq.w[i].trk, qq.w[i].id, qq.w[i].v);
        } else if (qq.w[i].id == QX_ENG) {
            t->eng_req = (uint8_t)(qq.w[i].v % NENGINES);   /* (voice.c engine_block fades over) */
            song_keep_eng(qq.w[i].trk, t->eng_req, t->preset);
        } else if (qq.w[i].id == QX_PRESET) {
            t->preset = (uint8_t)qq.w[i].v;
            t->user = 0;
            song_keep_eng(qq.w[i].trk, t->eng_req, t->preset);
        }
    }
    panic_req |= qq.panic;
    if (qq.solo_set)
        song.solo = qq.solo;
    if (qq.cg_set) {
        cg = qq.cg;
        memcpy(cgr.prog, qq.prog, sizeof cgr.prog);
        cgr.gen++;                                  /* (cg_poll makes the same progression: nothing changes) */
    }
    if (qq.st_set) {
        track_t *t = &trk[0];
        memcpy(t->step, qq.st, sizeof qq.st);
        t->p[P_SLEN] = (int16_t)BL_STEPS;
        t->p[P_SDIV] = 2;
        t->seq_active = 1;
        blr.changed = -1;
        blr.gen++;
    }
    qq.n = 0;
    qq.panic = qq.solo_set = qq.cg_set = qq.st_set = qq.what_set = 0;
    qq.gen++;
}
