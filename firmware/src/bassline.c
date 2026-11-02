/* SPDX-License-Identifier: GPL-3.0-only */
/* The BASS track's phrase generator (FMX): a jungle bassline of two bars, written into the track's steps (they show on
 * the step pages, can be edited by hand and are kept with the project like any steps), that changes a little as it
 * plays.
 *   the rhythm   RHYTHM KICKS: on the kicks of the BREAK track's loop A (the slices that open on a kick, brkpart.c);
 *                HALF (the half-time sub: 1 and the "and" of 3), ROLLER (3 + 3 + 4 + 2 ...), DUB (1, the "a" of 1,
 *                3 ...), STEPPER (every beat). VARIANT leaves out or adds a syncopation here and there.
 *   the notes    long, mostly the root: MOVE 0 the root only; up from it the octave, the fifth, the seventh, the fifth
 *                and seventh below, then the passing ones (third, fourth, sixth, second). The first note is the root.
 *                Of the session's key (the CHORDS generator's: ROOT and SCALE, a scale without seven notes: minor),
 *                OCT the octave; BASS FOLLOW (chordgen.c) moves the steps with the chords as it moves any.
 *   the phrase   bar 1 the call, bar 2 the answer: its first half as bar 1's, its end per ANSWER: SAME, OCT (up an
 *                octave), RUN (a run of short notes into the root of the next time round), SYNC (bar 1's end a 16th
 *                late, other notes), REST (it drops out), FREE (a bar of its own); AUTO: one of OCT / RUN / SYNC / FREE
 *                as VARIANT has it. HOLD: SHORT (a stab), MID (half the gap), LONG (legato, to the next note; with
 *                MOVE some of the moves slide).
 *   the change   each time round the phrase changes in one place with the chance CHANGE (0 = LOCK): a note moves to
 *                another of MOVE's, one is longer or shorter, one goes, one comes; never the first.
 *   KICKS, playing  the BREAK track's generator moves its slices about (STYLE, CMPLX ...): its kicks are seldom where
 *                the loop had them. So with RHYTHM KICKS (and a loop that has kicks) the bass strikes on the kicks the
 *                BREAK plays, as they come (bl_on_kick), and on the first step of each bar; the steps are then the
 *                line it follows: what note at each step (written legato, a rest only where ANSWER REST drops out),
 *                HOLD how long a strike sounds (SHORT 2 steps, MID 4, LONG 8 or to the next), a slide where the line
 *                slides. Two strikes are an eighth apart at least.
 * The steps are written when a setting of the phrase changes (bl_write, the main loop), on the BASS screen's NEW / ANSWER
 * / BACK keys, and when the screen opens on a track with no notes; the change is the audio ISR's (bl_cycle, as the
 * track's loop comes round). */
#define BL_STEPS 32u                     /* two bars of 1/16 */
enum { BR_KICKS, BR_HALF, BR_ROLLER, BR_DUB, BR_STEPPER, BR_N };
enum { BA_AUTO, BA_SAME, BA_OCT, BA_RUN, BA_SYNC, BA_REST, BA_FREE, BA_N };
enum { BH_SHORT, BH_MID, BH_LONG, BH_N };
static const char *const N_BL_RHYTHM[BR_N] = {"KICKS", "HALF", "ROLLER", "DUB", "STEPPER"};
static const char *const N_BL_ANSWER[BA_N] = {"AUTO", "SAME", "OCT", "RUN", "SYNC", "REST", "FREE"};
static const char *const N_BL_HOLD[BH_N] = {"SHORT", "MID", "LONG"};
typedef struct {                         /* what a project keeps (project.c): 8 bytes */
    uint16_t var;                        /* VARIANT 0..999: the seed */
    uint8_t rhythm;                      /* BR_* */
    uint8_t move;                        /* MOVE 0..100 */
    uint8_t change;                      /* CHANGE 0..100 % a time round (0 = LOCK) */
    uint8_t answer;                      /* BA_* */
    uint8_t hold;                        /* BH_* */
    uint8_t mix;                         /* OCT + 1 (0..3) | the answer's own seed << 2 (0..31) | 0x80: the steps are the
                                          * generator's (bl_write: only then does the phrase change as it plays) */
} bl_cfg_t;
#define BL_DEF {.var = 1, .rhythm = BR_KICKS, .move = 35, .change = 12, .answer = BA_AUTO, .hold = BH_LONG, .mix = 1}
static bl_cfg_t bl = BL_DEF;

static struct {
    uint32_t rng;                        /* the change's dice (the ISR's) */
    volatile int8_t changed;             /* the step the last change was at (-1 none), for the screen */
    volatile uint32_t gen;               /* bumped on every write and change (the screen redraws) */
    volatile uint32_t hits;              /* KICKS, playing: the steps struck this time round (the screen) */
    uint32_t last;                       /* the BASS track's grid step of the last strike */
    uint8_t struck;                      /* (one since PLAY) */
} blr = {.rng = 0x6C8E9CF5u, .changed = -1};

static int32_t bl_oct(void) { return (int32_t)(bl.mix & 3u) - 1; }
static uint32_t bl_aseed(void) { return (bl.mix >> 2) & 31u; }
static int bl_on(void) { return (bl.mix >> 7) & 1u; }
static void bl_set_oct(int32_t o) { bl.mix = (uint8_t)((bl.mix & ~3u) | (uint32_t)(clamp(o, -1, 2) + 1)); }
static void bl_defaults(void)
{
    static const bl_cfg_t D = BL_DEF;
    bl = D;
}
static void bl_clamp(bl_cfg_t *c)                    /* every value back inside its range (a project) */
{
    c->var = (uint16_t)(c->var % 1000u);
    c->rhythm %= BR_N;
    c->move = (uint8_t)clamp(c->move, 0, 100);
    c->change = (uint8_t)clamp(c->change, 0, 100);
    c->answer %= BA_N;
    c->hold %= BH_N;
}

static int bl_step_on(const step_t *st) { return st->time == ST_NOTE && st->n; }   /* (as ui.c step_on) */

/* ---- the notes ------------------------------------------------------------------------------------- */
/* the vocabulary: degrees of the scale from the root (-3 the fifth below ... 7 the octave), MOVE 0's and MOVE 100's
 * weights (between them: mixed) */
#define BL_NV 10u
static const int8_t BL_DEG[BL_NV] = {0, 7, 4, 6, -3, -1, 2, 3, 5, 1};
static const uint8_t BL_W1[BL_NV] = {28, 12, 14, 10, 7, 6, 8, 7, 4, 4};
static uint32_t bl_rand(uint32_t *s)                 /* xorshift32 */
{
    uint32_t x = *s ? *s : 0x9E3779B9u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *s = x;
    return x;
}
static int32_t bl_pick(uint32_t *s, uint32_t move)   /* a degree, weighted by MOVE */
{
    uint32_t w[BL_NV], sum = 0, i, r;
    for (i = 0; i < BL_NV; i++)
        sum += w[i] = (i ? 0u : (100u - move) * 100u) + BL_W1[i] * move;
    r = bl_rand(s) % sum;
    for (i = 0; i + 1u < BL_NV && r >= w[i]; i++)
        r -= w[i];
    return BL_DEG[i];
}
/* the register of a jungle sub (docs/bass.md): the root from E1 to D#2 (41 .. 78 Hz; over it the bass sits on the
 * kick's body, under it a club's system and the FM-1's out have little left), and no note under E1: one goes an
 * octave up (the fifth under the root: the fourth over it) */
#define BASS_FLOOR 28                                    /* E1 */
static int32_t bass_floor(int32_t n)
{
    while (n < BASS_FLOOR)
        n += 12;
    return n;
}
/* the MIDI note of degree d of the session's scale, OCT */
static uint32_t bl_note(int32_t d)
{
    uint32_t m = cg_mask(), deg[12], n = 0, i;
    int32_t o, r = (int32_t)cg_root();
    for (i = 0; i < 12u; i++)
        if ((m >> i) & 1u)
            deg[n++] = i;
    o = d >= 0 ? d / (int32_t)n : -((-d + (int32_t)n - 1) / (int32_t)n);
    r += r < BASS_FLOOR - 24 ? 36 : 24;                  /* the root: E1 .. D#2 */
    return (uint32_t)clamp(bass_floor(r + bl_oct() * 12 + o * 12 + (int32_t)deg[d - o * (int32_t)n]), 0, 127);
}

/* ---- the rhythm ------------------------------------------------------------------------------------ */
#define B(x) (1u << (x))
static const uint32_t BL_RHY[BR_N] = {
    B(0) | B(10) | B(16) | B(26),                                                   /* (KICKS: when the loop has none) */
    B(0) | B(10) | B(16) | B(26),                                                   /* HALF */
    B(0) | B(3) | B(6) | B(10) | B(12) | B(16) | B(19) | B(22) | B(26) | B(28),     /* ROLLER */
    B(0) | B(3) | B(8) | B(11) | B(16) | B(19) | B(24) | B(27) | B(30),             /* DUB */
    B(0) | B(4) | B(8) | B(12) | B(16) | B(20) | B(24) | B(28),                     /* STEPPER */
};
#undef B
/* KICKS: the kick slices of the BREAK's loop A on the two bars (a slice is an eighth of the loop); 0 = not known */
static uint32_t bl_kicks(void)
{
    uint32_t bars = brkp_bank_bars(0), kicks = bpb[0].kicks, len, sl, p, m = 0;
    if (!brkp_bank_src(0) || !kicks || !bars)
        return 0;
    len = 16u * bars;                                   /* the loop's 16ths */
    sl = len / BRK_SLICES;
    for (p = 0; p < BL_STEPS; p++)
        if (!(p % len % sl) && ((kicks >> (p % len / sl)) & 1u))
            m |= 1u << p;
    return m;
}
static uint32_t bl_rhythm(uint32_t *s)
{
    uint32_t m = bl.rhythm == BR_KICKS ? bl_kicks() : 0u, bar, k;
    if (bl.rhythm == BR_KICKS && (!(m & 0xFFFEu) || !(m >> 16)))
        m |= BL_RHY[BR_HALF];                           /* (a loop with one kick a bar: the half-time sub under it) */
    if (!m)
        m = BL_RHY[bl.rhythm % BR_N];
    for (bar = 0; bar < 2u; bar++) {                     /* VARIANT: one left out, one syncopation added */
        uint32_t r = bl_rand(s) % 100u, at;
        if (r < 30u) {
            for (k = 0; k < 4u; k++) {
                at = bar * 16u + 2u + bl_rand(s) % 14u;
                if ((m >> at) & 1u) {
                    m &= ~(1u << at);
                    break;
                }
            }
        } else if (r < 65u) {
            for (k = 0; k < 4u; k++) {
                at = bar * 16u + 3u + 4u * (bl_rand(s) % 4u) - (bl_rand(s) & 1u) * 2u;   /* an "a" or "e" */
                if (at % 16u && !((m >> at) & 7u) && !(at && ((m >> (at - 1u)) & 1u))) {
                    m |= 1u << at;
                    break;
                }
            }
        }
    }
    return m | 1u;
}

/* ---- the phrase ------------------------------------------------------------------------------------ */
/* the two bars: onsets, degrees (BL_REST: none), slides; into out[] as steps */
#define BL_REST 127
static void bl_build(step_t *out)
{
    uint32_t s = 0x2545F491u ^ ((uint32_t)bl.var * 2654435761u), sa, m, p, ans = bl.answer;
    uint32_t hold = bl.rhythm == BR_KICKS ? BH_LONG : bl.hold;   /* (KICKS: the line, legato; HOLD is the strike's) */
    int8_t deg[BL_STEPS];
    uint8_t slide[BL_STEPS];
    m = bl_rhythm(&s);
    for (p = 0; p < 16u; p++)                            /* bar 1: the call, the root first */
        deg[p] = (int8_t)(((m >> p) & 1u) ? (p ? bl_pick(&s, bl.move) : 0) : BL_REST);
    sa = s ^ (bl_aseed() * 0x9E3779B9u);
    if (ans == BA_AUTO) {
        static const uint8_t AUTO[4] = {BA_OCT, BA_RUN, BA_SYNC, BA_FREE};
        ans = AUTO[bl_rand(&sa) % 4u];
    }
    for (p = 16; p < BL_STEPS; p++)                      /* bar 2: the answer */
        deg[p] = ans == BA_FREE ? (int8_t)(((m >> p) & 1u) ? (p == 16u ? 0 : bl_pick(&sa, bl.move)) : BL_REST)
                                : deg[p - 16u];
    if (ans == BA_SYNC || ans == BA_RUN || ans == BA_REST) /* (its end changed: from the 3rd beat on) */
        for (p = 24; p < BL_STEPS; p++)
            deg[p] = BL_REST;
    if (ans == BA_SYNC) {                                /* bar 1's end a 16th late, other notes */
        for (p = 8; p < 15u; p++)
            if (deg[p] != BL_REST)
                deg[p + 17u] = (int8_t)bl_pick(&sa, bl.move < 30u ? 30u : bl.move);
    } else if (ans == BA_RUN) {                          /* a run into the root: down from the fifth, or up from below */
        static const int8_t RUN[2][3] = {{4, 2, 1}, {-3, -2, -1}};
        const int8_t *r = RUN[bl_rand(&sa) & 1u];
        deg[24] = deg[8] != BL_REST ? deg[8] : 0;
        deg[26] = r[0];
        deg[28] = r[1];
        deg[30] = r[2];
    } else if (ans == BA_OCT) {                          /* its last notes an octave up */
        uint32_t n = 0;
        for (p = BL_STEPS; p-- > 17u && n < 2u;)
            if (deg[p] != BL_REST) {
                if (deg[p] < 7)                          /* (an octave already: as it is) */
                    deg[p] = (int8_t)(deg[p] + 7);
                n++;
            }
    }
    memset(slide, 0, sizeof slide);
    memset(out, 0, sizeof *out * BL_STEPS);
    for (p = 0; p < BL_STEPS; p++)                       /* the steps: NOTE, then TIEs as HOLD has them */
        if (deg[p] != BL_REST) {
            uint32_t gap = 1, len, k;
            while (p + gap < BL_STEPS && deg[p + gap] == BL_REST && !(ans == BA_REST && p + gap == 24u))
                gap++;                                   /* (REST: the line stops at bar 2's 3rd beat) */
            len = hold == BH_SHORT ? (gap < 2u ? gap : 2u) : hold == BH_MID ? (gap + 1u) / 2u : gap;
            if (gap > 2u && hold == BH_MID && len < 2u)
                len = 2;
            out[p].time = ST_NOTE;
            out[p].n = 1;
            out[p].note[0] = (uint8_t)bl_note(deg[p]);
            out[p].vel = 100;
            for (k = 1; k < len; k++)
                out[p + k].time = ST_TIE;
            if (hold == BH_LONG && p + gap < BL_STEPS && deg[p + gap] != deg[p] &&
                bl_rand(&s) % 300u < bl.move)            /* (a third of MOVE's moves slide) */
                slide[p + len - 1u] = 1;
        }
    for (p = 0; p < BL_STEPS; p++) {
        if (out[p].time != ST_NOTE && out[p].time != ST_TIE)
            out[p].time = ST_REST;
        if (slide[p])
            out[p].flags |= SF_SLIDE;
    }
}

static int q_on(void);                              /* q.c */
static void q_steps(uint32_t k, const step_t *st);
static void q_name(uint32_t k, const char *s);
/* the phrase into the BASS track's steps (the main loop): two bars of 1/16; with Q, on the boundary (q.c) */
static void bl_write(void)
{
    static step_t st[BL_STEPS];
    track_t *t = &trk[0];
    bl_build(st);
    if (q_on()) {
        bl.mix |= 0x80u;
        q_steps(0, st);
        q_name(0, "PHRASE");
        return;
    }
    fm1_irq_off();
    memcpy(t->step, st, sizeof st);
    t->p[P_SLEN] = (int16_t)BL_STEPS;
    t->p[P_SDIV] = 2;                                   /* (1/16) */
    t->seq_active = 1;
    bl.mix |= 0x80u;                                    /* (the steps are the generator's: they may change) */
    blr.changed = -1;
    blr.gen++;
    fm1_irq_on();
}
/* the track has a note in its loop */
static int bl_has_notes(void)
{
    uint32_t i;
    for (i = 0; i < trk_len(&trk[0]); i++)
        if (bl_step_on(&trk[0].step[i]))
            return 1;
    return 0;
}

/* ---- the change (the audio ISR: the BASS track's loop comes round) ------------------------------------ */
/* the note starting at or before step p (its NOTE step), -1 none */
static int bl_head(const step_t *st, int p)
{
    while (p >= 0 && st[p].time == ST_TIE)
        p--;
    return p >= 0 && bl_step_on(&st[p]) ? p : -1;
}
static void bl_cycle(void)
{
    track_t *t = &trk[0];
    step_t *st = t->step;
    uint32_t len = trk_len(t), op, k, n = 0, h, e;
    uint8_t heads[NSTEP];
    if (!bl_on() || !bl.change || len < 2u || bl_rand(&blr.rng) % 100u >= bl.change)
        return;
    for (k = 1; k < len; k++)                            /* the notes after the first */
        if (bl_step_on(&st[k]))
            heads[n++] = (uint8_t)k;
    op = bl_rand(&blr.rng) % 100u;
    if (n && op < 50u) {                                 /* a note to another of MOVE's */
        uint8_t was;
        h = heads[bl_rand(&blr.rng) % n];
        was = st[h].note[0];
        for (k = 0; k < 4u && st[h].note[0] == was; k++)
            st[h].note[0] = (uint8_t)bl_note(bl_pick(&blr.rng, bl.move < 20u ? 20u : bl.move));
        st[h].n = 1;
    } else if (n && op < 70u) {                         /* longer (into a rest) or shorter */
        h = heads[bl_rand(&blr.rng) % n];
        for (e = h + 1u; e < len && st[e].time == ST_TIE; e++)
            ;
        if (e < len && (st[e].time == ST_REST || !st[e].n) && (bl_rand(&blr.rng) & 1u)) {
            st[e].time = ST_TIE;
        } else if (e - h > 1u) {
            st[e - 1u].time = ST_REST;
            st[e - 1u].flags = 0;
        }
    } else if (n > 2u && op < 85u) {                    /* one goes */
        h = heads[bl_rand(&blr.rng) % n];
        st[h].time = ST_REST;
        st[h].flags = 0;
        for (e = h + 1u; e < len && st[e].time == ST_TIE; e++)
            st[e].time = ST_REST;
    } else {                                            /* one comes: in a rest, on an off-beat 16th or the "and" */
        for (k = 0; k < 6u; k++) {
            h = 1u + bl_rand(&blr.rng) % (len - 1u);
            if (st[h].time != ST_TIE && !bl_step_on(&st[h]) && (h & 1u || h % 4u == 2u)) {
                int p0 = bl_head(st, (int)h - 1);
                memset(&st[h], 0, sizeof st[h]);
                st[h].time = ST_NOTE;
                st[h].n = 1;
                st[h].vel = 100;
                st[h].note[0] = (uint8_t)(p0 >= 0 && (bl_rand(&blr.rng) & 1u) ? st[p0].note[0] : bl_note(0));
                break;
            }
        }
        if (k == 6u)
            return;
    }
    blr.changed = (int8_t)h;
    blr.gen++;
}

/* ---- KICKS, playing: the bass on the kicks the BREAK plays (the audio ISR) ---------------------------- */
static int bl_live(void) { return bl_on() && bl.rhythm == BR_KICKS && brkp_bank_src(0) && bpb[0].kicks; }
/* the line at step idx: its note (the one sounding there, through its TIEs), -1 = a rest; *slide: slid into */
static int bl_line_at(uint32_t idx, int *slide)
{
    const step_t *st = trk[0].step;
    int p = (int)idx;
    while (p >= 0 && st[p].time == ST_TIE)
        p--;
    *slide = p > 0 && (st[p - 1].flags & SF_SLIDE) != 0u;
    return p >= 0 && bl_step_on(&st[p]) ? st[p].note[0] : -1;
}
/* a strike of the line's note at step idx: the track's seq notes, as seq.c seq_step plays a step of one note */
static void bl_strike(uint32_t idx)
{
    static const uint8_t HOLD_STEPS[BH_N] = {2, 4, 8};
    track_t *t = &trk[0];
    uint32_t into, slen, vel = 100, i, glide;
    int slide, n = bl_line_at(idx % NSTEP, &slide);
    uint8_t note;
    if (n < 0)
        return;                                         /* (a rest of the line: ANSWER REST) */
    note = (uint8_t)clamp(bass_floor(n + cg_follow()), 0, 127);   /* (BASS FOLLOW) */
    trk_grid(t, &into, &slen);
    glide = slide && t->seq_n && t->seq_notes[0] != note;
    t->slide_glide = (uint8_t)glide;
    if (!glide)
        seq_release(t);
    if (glide)
        trk_note_on(t, note, vel);
    else
        trk_note_chord(t, &note, 1, 0, vel);
    seq_out_on(t, note, vel);
    for (i = 0; glide && i < t->seq_n; i++)
        if (t->seq_notes[i] != note) {
            trk_note_off(t, t->seq_notes[i]);
            seq_out_off(t, t->seq_notes[i]);
        }
    t->seq_n = 1;
    t->seq_notes[0] = note;
    t->seq_off = HOLD_STEPS[bl.hold % BH_N] * slen * (uint32_t)t->p[P_SGATE] / 128u;
    t->seq_hold = 0;
    blr.last = t->seq_abs;
    blr.struck = 1;
    blr.hits |= 1u << (idx & 31u);
}
/* seq.c: a kick slice started at the BREAK track's step (after the BASS track's step, the same block) */
static void bl_on_kick(void)
{
    const track_t *t = &trk[0];
    if (!bl_live() || !song.playing || t->seq_abs == SEQ_NONE || t->seq_skip)
        return;
    if (blr.struck && t->seq_abs - blr.last < 2u)
        return;                                         /* (an eighth apart at least) */
    bl_strike(t->seq_idx);
}
