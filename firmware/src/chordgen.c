/* SPDX-License-Identifier: GPL-3.0-only */
/* The CHORDS generator (FMX): track 2 plays a progression that music theory writes, not you. You choose how many
 * chords, how coloured, how dark, and scroll the variants until one sounds right (VARIANT is a seed: the same number
 * gives the same progression again); a chord you like can be pinned while the others change around it.
 *
 * The progression: chords on the degrees of the session's key (the parts' ROOT and SCALE, SEL + key; a scale without
 * seven notes counts as minor), the first on the tonic, each next one drawn from a table of how often one degree goes
 * to another (functional harmony, weighted towards what jungle and drum & bass use: i - VI - III - VII, i - iv - VII,
 * ii - V ...), the last one chosen so it leads back to the first (the loop has no seam). MOOD brings in chords from the
 * parallel modes (minor: the dorian IV, the harmonic V, the phrygian bII; major: bVI, bVII, iv, bIII). COLOR is applied
 * when a chord is voiced (the progression stays): triads, 7ths, add9, 9ths, sus, lush (maj9 / m11). Each chord is
 * voiced nearest the one before it (the inversion and octave that move the voices least), around REG.
 *
 * Playing: PLAY HOLD holds each chord for RATE (1/2, 1, 2 or 4 bars) on the transport's bars, as FEEL shares the time
 * out (the harmonic rhythm): EVEN, every chord RATE long; TONIC, the loop made the next 2 / 4 / 8 RATEs, the first
 * (tonic) chord and the stable ones longer, the one before the return shorter (3 chords: 2 + 1 + 1); ACCEL, the
 * changes coming faster towards the turn (4 chords: 1.5 + 1.5 + 0.5 + 0.5); PUSH, as TONIC with each change an eighth
 * early, on the "and" before the bar, as jungle pads and stabs anticipate it. (A loop of 3 RATEs would drift against
 * the BREAK's 4- and 8-bar phrases: TONIC, ACCEL and PUSH keep it to a power of two.) PLAY STEPS plays the
 * chord of the moment on each step of the track that has a note (its rhythm, the generator's harmony: rave stabs);
 * OFF leaves the track to its own steps. A node's black key on the CHORDS screen plays that chord while it is held.
 * BASS FOLLOW: the BASS track's steps move with the chord's root (the nearest way, -6 .. +5 semitones).
 *
 * Threading: cg (the settings: the UI, a project) and the progression (cgr.prog, built in the main loop by cg_poll) are
 * read by the audio ISR (cg_block, cg_step_notes, cg_follow), which owns cgr's playing state; the UI writes cgr.live. */
#define CG_MAX 8u
enum { CQ_MAJ, CQ_MIN, CQ_DIM, CQ_AUG };
enum { CC_TRIAD, CC_7TH, CC_ADD9, CC_9TH, CC_SUS, CC_LUSH, CC_N };
enum { CP_OFF, CP_HOLD, CP_STEPS, CP_N };
static const char *const N_CG_COLOR[CC_N] = {"TRIAD", "7TH", "ADD9", "9TH", "SUS", "LUSH"};
static const char *const N_CG_PLAY[CP_N] = {"OFF", "HOLD", "STEPS"};
static const char *const N_CG_RATE[4] = {"1/2", "1BAR", "2BAR", "4BAR"};
static const uint8_t CG_RATE_BEATS[4] = {2, 4, 8, 16};
enum { CF_EVEN, CF_TONIC, CF_ACCEL, CF_PUSH, CF_N };
static const char *const N_CG_FEEL[CF_N] = {"EVEN", "TONIC", "ACCEL", "PUSH"};
/* the chords' lengths in half RATEs, for 2..8 chords (each row sums to 4, 8, 8, 16, 16, 16, 16) */
static const uint8_t CG_TONIC[7][8] = {{3, 1}, {4, 2, 2}, {3, 2, 2, 1}, {4, 3, 3, 4, 2}, {4, 2, 2, 4, 2, 2},
                                       {4, 2, 2, 2, 2, 2, 2}, {3, 2, 2, 2, 2, 2, 2, 1}};
static const uint8_t CG_ACCEL[7][8] = {{3, 1}, {4, 2, 2}, {3, 3, 1, 1}, {4, 4, 4, 2, 2}, {4, 4, 2, 2, 2, 2},
                                       {4, 4, 2, 2, 2, 1, 1}, {3, 3, 2, 2, 2, 2, 1, 1}};

typedef struct {                         /* what a project keeps (project.c) */
    uint16_t var;                        /* VARIANT 0..999: the seed */
    uint8_t num;                         /* CHORDS 2..8 */
    uint8_t color;                       /* CC_* */
    uint8_t mood;                        /* 0..100 */
    uint8_t rate;                        /* CG_RATE_BEATS index | FEEL (CF_*) << 2 */
    uint8_t play;                        /* CP_* */
    int8_t reg;                          /* REG: the octave, -2..2 */
    uint8_t pins;                        /* a node pinned: bit per node, its chord in code[] */
    uint8_t follow;                      /* BASS FOLLOW 0 / 1 */
    uint8_t code[CG_MAX];                /* the pinned chords: root (semitones above the key) | quality << 4 */
} chord_cfg_t;
static chord_cfg_t cg = {.var = 1, .num = 4, .color = CC_7TH, .mood = 20, .rate = 1 | CF_TONIC << 2, .play = CP_HOLD, .follow = 1};

static void cg_defaults(void)
{
    memset(&cg, 0, sizeof cg);
    cg.var = 1;
    cg.num = 4;
    cg.color = CC_7TH;
    cg.mood = 20;
    cg.rate = 1 | CF_TONIC << 2;                     /* (a bar, FEEL TONIC) */
    cg.play = CP_HOLD;
    cg.follow = 1;
}
static void cg_clamp(chord_cfg_t *c)                 /* every value back inside its range (a project, the editor) */
{
    uint32_t i;
    c->var = (uint16_t)(c->var % 1000u);
    c->num = (uint8_t)clamp(c->num, 2, CG_MAX);
    c->color %= CC_N;
    c->mood = (uint8_t)clamp(c->mood, 0, 100);
    c->rate = (uint8_t)((c->rate & 3u) | (((uint32_t)(c->rate >> 2) < CF_N ? (uint32_t)(c->rate >> 2) : 0u) << 2));
    c->play %= CP_N;
    c->reg = (int8_t)clamp(c->reg, -2, 2);
    c->follow = (uint8_t)(c->follow != 0u);
    for (i = 0; i < CG_MAX; i++)
        c->code[i] = (uint8_t)((c->code[i] & 15u) % 12u | (c->code[i] & 0x30u));
}

static struct {
    uint8_t prog[CG_MAX];                /* the progression: codes */
    uint32_t key;                        /* what it was made for */
    volatile uint32_t gen;               /* bumped on every new progression (the screen redraws) */
    volatile uint8_t live;               /* the nodes held on the CHORDS screen (bit per node: the UI) */
    uint8_t live_seen;
    int8_t now;                          /* the node sounding (HOLD / STEPS / live), -1 = none */
    uint8_t notes[6], nn;                /* its voicing, sounding (HOLD, live) */
    uint8_t lead[6], nlead;              /* the last voicing made (the voice leading's reference) */
} cgr = {.key = 0xFFFFFFFFu, .now = -1};

/* ---- the key ---------------------------------------------------------------------------------------- */
static uint32_t cg_root(void) { return (uint32_t)trk[1].p[P_ROOT] % 12u; }
static uint32_t cg_mask(void)                        /* the scale: a seven-note one, else minor */
{
    uint32_t m = SCALE_MASK[clamp(trk[1].p[P_SCALE], 0, NSCALES - 1)], n = 0, i;
    for (i = 0; i < 12u; i++)
        n += (m >> i) & 1u;
    return n == 7u ? m : SCALE_MASK[2];
}
static int cg_minor(uint32_t mask) { return (mask >> 3) & 1u; }   /* a minor third over the tonic */
static void cg_scale(uint32_t mask, uint8_t *deg)   /* the seven notes, semitones above the key */
{
    uint32_t i, n = 0;
    for (i = 0; i < 12u && n < 7u; i++)
        if ((mask >> i) & 1u)
            deg[n++] = (uint8_t)i;
}
/* the quality of the triad on the scale note at index d */
static uint32_t cg_triad(const uint8_t *deg, uint32_t d)
{
    uint32_t r = deg[d % 7u], t = (deg[(d + 2u) % 7u] + 12u - r) % 12u, f = (deg[(d + 4u) % 7u] + 12u - r) % 12u;
    return t == 4u ? (f == 8u ? CQ_AUG : CQ_MAJ) : f == 6u ? CQ_DIM : CQ_MIN;
}
/* the degree (0..6) a root plays the part of: its scale note, else the nearest function (a borrowed chord) */
static uint32_t cg_degree(const uint8_t *deg, uint32_t root)
{
    static const uint8_t NEAR[12] = {0, 1, 1, 2, 2, 3, 3, 4, 5, 5, 6, 6};
    uint32_t i;
    for (i = 0; i < 7u; i++)
        if (deg[i] == root % 12u)
            return i;
    return NEAR[root % 12u];
}

/* ---- the progression --------------------------------------------------------------------------------- */
/* how often degree a goes to degree b (minor, major): functional harmony as jungle and drum & bass like it */
static const uint8_t CG_MINOR[7][7] = {
    /* i   ii  III iv  v   VI  VII */
    {0, 5, 10, 25, 10, 30, 20},                      /* i */
    {10, 0, 0, 10, 30, 0, 20},                       /* ii° */
    {10, 0, 0, 20, 5, 25, 25},                       /* III */
    {25, 0, 10, 0, 15, 15, 25},                      /* iv */
    {30, 0, 5, 10, 0, 25, 5},                        /* v */
    {15, 5, 20, 15, 10, 0, 30},                      /* VI */
    {30, 0, 25, 10, 5, 15, 0},                       /* VII */
};
static const uint8_t CG_MAJOR[7][7] = {
    /* I   ii  iii IV  V   vi  vii */
    {0, 15, 10, 25, 20, 25, 0},                      /* I */
    {5, 0, 0, 15, 40, 5, 5},                         /* ii */
    {5, 5, 0, 25, 5, 35, 0},                         /* iii */
    {25, 15, 5, 0, 30, 10, 0},                       /* IV */
    {35, 5, 5, 10, 0, 30, 0},                        /* V */
    {10, 25, 10, 30, 20, 0, 0},                      /* vi */
    {30, 0, 10, 0, 5, 5, 0},                         /* vii° */
};
static uint32_t cg_rng;
static uint32_t cg_rnd(uint32_t n)                   /* 0 .. n - 1 */
{
    uint32_t x = cg_rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    cg_rng = x;
    return n ? (x >> 8) % n : 0u;
}
static uint32_t cg_pick(const uint8_t *w, uint32_t n)   /* an index drawn by the weights w[0..n) */
{
    uint32_t i, sum = 0, r;
    for (i = 0; i < n; i++)
        sum += w[i];
    if (!sum)
        return 0;
    r = cg_rnd(sum);
    for (i = 0; i < n; i++) {
        if (r < w[i])
            return i;
        r -= w[i];
    }
    return n - 1u;
}
static uint8_t cg_code(uint32_t root, uint32_t q) { return (uint8_t)(root % 12u | q << 4); }
/* a borrowed chord standing in for degree d (MOOD), or 0xFF: minor IV (dorian), V (harmonic), bII (phrygian);
 * major bVI, bVII, iv, bIII */
static uint32_t cg_borrow(int minor, uint32_t d)
{
    if (minor)
        return d == 3u ? cg_code(5, CQ_MAJ) : d == 4u ? cg_code(7, CQ_MAJ) : d == 1u ? cg_code(1, CQ_MAJ) : 0xFFu;
    return d == 5u ? cg_code(8, CQ_MAJ) : d == 6u ? cg_code(10, CQ_MAJ) : d == 3u ? cg_code(5, CQ_MIN) :
           d == 2u ? cg_code(3, CQ_MAJ) : 0xFFu;
}
/* the progression of cg into out[], for the key's mask */
static void cg_generate(uint8_t *out, uint32_t mask)
{
    const uint8_t (*W)[7];
    uint8_t deg[7], w[7];
    uint32_t i, n = cg.num, d, first, prev, minor = (uint32_t)cg_minor(mask);
    cg_scale(mask, deg);
    W = minor ? CG_MINOR : CG_MAJOR;
    cg_rng = 0x9E3779B9u ^ (uint32_t)cg.var * 2654435761u ^ n << 24 ^ mask << 4 ^ cg.mood * 977u;
    cg_rnd(1);
    for (i = 0; i < n; i++) {
        if ((cg.pins >> i) & 1u) {                   /* a pinned node: its own chord, the chain goes on from it */
            out[i] = cg.code[i];
            continue;
        }
        if (i == 0u) {
            d = cg.mood > 60u && cg_rnd(100) < 30u ? (minor ? 5u : 5u) : 0u;   /* (dark: the VI / vi at times) */
        } else {
            prev = cg_degree(deg, out[i - 1u] & 15u);
            for (d = 0; d < 7u; d++) {
                uint32_t j, used = 0;
                for (j = 0; j < i; j++)
                    used |= cg_degree(deg, out[j] & 15u) == d;
                w[d] = (uint8_t)(used ? W[prev][d] / 4u : W[prev][d]);   /* (a chord heard already: less likely) */
            }
            if (i == n - 1u) {                       /* the last: one that leads back to the first */
                first = cg_degree(deg, out[0] & 15u);
                for (d = 0; d < 7u; d++)
                    w[d] = (uint8_t)((uint32_t)w[d] * W[d][first] / 10u + (w[d] && W[d][first] ? 1u : 0u));
            }
            d = cg_pick(w, 7);
            if (d == cg_degree(deg, out[i - 1u] & 15u))
                d = (d + 3u) % 7u;                   /* (never the same chord twice in a row) */
        }
        out[i] = cg_code(deg[d], cg_triad(deg, d));
        if (i && cg_rnd(200) < cg.mood) {            /* MOOD: a chord from the parallel mode, now and then */
            uint32_t b = cg_borrow((int)minor, d);
            if (b != 0xFFu)
                out[i] = (uint8_t)b;
        }
    }
    for (; i < CG_MAX; i++)
        out[i] = 0;
}
/* the main loop: a new progression when the settings, the key or the pins change */
static void cg_poll(void)
{
    uint32_t mask = cg_mask(), k, i;
    k = (uint32_t)cg.var * 31u ^ (uint32_t)cg.num << 12 ^ (uint32_t)cg.mood << 16 ^ mask << 20 ^ (uint32_t)cg.pins * 7919u;
    for (i = 0; i < CG_MAX; i++)
        if ((cg.pins >> i) & 1u)
            k = k * 33u + cg.code[i];
    if (k == cgr.key)
        return;
    {
        uint8_t p[CG_MAX];
        cg_generate(p, mask);
        fm1_irq_off();
        memcpy(cgr.prog, p, sizeof p);
        cgr.key = k;
        cgr.gen++;
        fm1_irq_on();
    }
}
/* pin / unpin node i (its chord kept while the others change) */
static void cg_pin(uint32_t i, int on)
{
    if (i >= CG_MAX)
        return;
    if (on) {
        cg.code[i] = cgr.prog[i];
        cg.pins |= (uint8_t)(1u << i);
    } else {
        cg.pins &= (uint8_t)~(1u << i);
    }
}

/* ---- a chord's notes ------------------------------------------------------------------------------- */
/* its tones in semitones above its root (COLOR; sus: sus2 on even nodes, sus4 on odd ones); n returned */
static uint32_t cg_tones(uint32_t code, uint32_t node, uint8_t *iv)
{
    uint32_t q = (code >> 4) & 3u, root = code & 15u, mask = cg_mask(), n = 0;
    uint32_t third = q == CQ_MAJ || q == CQ_AUG ? 4u : 3u, fifth = q == CQ_DIM ? 6u : q == CQ_AUG ? 8u : 7u;
    uint32_t sev = q == CQ_DIM ? 10u : ((mask >> ((root + 11u) % 12u)) & 1u) && q == CQ_MAJ ? 11u : 10u;
    if (q == CQ_MAJ && root == 7u && cg_minor(mask))
        sev = 10u;                                   /* (the harmonic V: a dominant 7th) */
    iv[n++] = 0;
    switch (cg.color) {
    case CC_SUS:
        iv[n++] = (uint8_t)(node & 1u ? 5u : 2u);
        iv[n++] = (uint8_t)fifth;
        iv[n++] = (uint8_t)sev;
        break;
    case CC_LUSH:                                    /* maj9 / m11 / m7b5 + 11: the 5th left out of the minor ones */
        iv[n++] = (uint8_t)third;
        if (q == CQ_MAJ || q == CQ_AUG)
            iv[n++] = (uint8_t)fifth;
        iv[n++] = (uint8_t)sev;
        iv[n++] = 14;
        if (q != CQ_MAJ && q != CQ_AUG)
            iv[n++] = 17;
        break;
    default:
        iv[n++] = (uint8_t)third;
        iv[n++] = (uint8_t)fifth;
        if (cg.color == CC_7TH || cg.color == CC_9TH)
            iv[n++] = (uint8_t)sev;
        if (cg.color == CC_ADD9 || cg.color == CC_9TH)
            iv[n++] = 14;
        break;
    }
    return n;
}
/* the voicing of node i: its tones in the inversion and octave nearest the last voicing (cgr.lead), around REG */
static uint32_t cg_voice(uint32_t i, uint8_t *out)
{
    uint8_t iv[6], pc[6], best[6] = {0};
    uint32_t code = cgr.prog[i % CG_MAX], n = cg_tones(code, i, iv), r, o, k, nb = 0;
    int32_t center = 60 + cg.reg * 12, bestc = 0x7FFFFFFF;
    for (k = 0; k < n; k++)
        pc[k] = (uint8_t)((cg_root() + (code & 15u) + iv[k]) % 12u);
    for (r = 0; r < n; r++)                          /* each tone in the bass ... */
        for (o = 0; o < 3u; o++) {                   /* ... in three octaves around REG */
            uint8_t v[6];
            int32_t lo = center - 12 + (int32_t)o * 12 - 6, base, cost = 0;
            uint32_t j;
            base = lo + (int32_t)((pc[r] + 120u - (uint32_t)lo % 12u) % 12u);
            for (j = 0; j < n; j++) {                /* the others stacked above it, each the nearest above */
                uint32_t t = pc[(r + j) % n];
                int32_t p = j ? v[j - 1u] + 1 : base;
                while ((uint32_t)p % 12u != t)
                    p++;
                v[j] = (uint8_t)clamp(p, 24, 108);
            }
            if (cgr.nlead == n) {                    /* the voices' movement */
                for (j = 0; j < n; j++)
                    cost += v[j] > cgr.lead[j] ? v[j] - cgr.lead[j] : cgr.lead[j] - v[j];
            } else {                                 /* (no chord before: the one around REG) */
                int32_t m = 0;
                for (j = 0; j < n; j++)
                    m += v[j];
                cost = m / (int32_t)n > center ? m / (int32_t)n - center : center - m / (int32_t)n;
            }
            cost += v[n - 1u] - v[0] > 24 ? 12 : 0;  /* (a voicing spread past two octaves: thinner) */
            cost += r ? 3 : 0;                       /* (the root in the bass, when it costs little) */
            if (cost < bestc) {
                bestc = cost;
                memcpy(best, v, n);
                nb = n;
            }
        }
    memcpy(out, best, nb);
    return nb;
}

/* the name of a chord: "Am9", "Fmaj7", "Bbsus2", b holds 8 */
static void cg_name(uint32_t i, char *b)
{
    static const char *const NOTE[12] = {"C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B"};
    uint32_t code = cgr.prog[i % CG_MAX], q = (code >> 4) & 3u, root = (cg_root() + (code & 15u)) % 12u;
    uint8_t iv[6];
    uint32_t n = cg_tones(code, i, iv), maj7 = n >= 4u && iv[3] == 11u;
    const char *s = "";
    (void)n;
    switch (cg.color) {
    case CC_TRIAD: s = q == CQ_MIN ? "m" : q == CQ_DIM ? "dim" : q == CQ_AUG ? "+" : ""; break;
    case CC_7TH: s = q == CQ_MIN ? "m7" : q == CQ_DIM ? "m7b5" : maj7 ? "maj7" : "7"; break;
    case CC_ADD9: s = q == CQ_MIN ? "madd9" : q == CQ_DIM ? "dim" : "add9"; break;
    case CC_9TH: s = q == CQ_MIN ? "m9" : q == CQ_DIM ? "m7b5" : maj7 ? "maj9" : "9"; break;
    case CC_SUS: s = i & 1u ? "sus4" : "sus2"; break;
    default: s = q == CQ_MIN ? "m11" : q == CQ_DIM ? "m7b5" : iv[3] == 11u ? "maj9" : "9"; break;
    }
    str_cpy(b, NOTE[root], 8);
    str_cpy(b + str_len(b), s, 8 - str_len(b));
}

/* ---- playing (the audio ISR) -------------------------------------------------------------------------- */
static int cg_on(void) { return cg.play != CP_OFF; }
static uint32_t cg_rate(void) { return cg.rate & 3u; }
static uint32_t cg_feel(void) { return (uint32_t)(cg.rate >> 2) % CF_N; }
static void cg_set_rate(int32_t r) { cg.rate = (uint8_t)((cg.rate & ~3u) | (uint32_t)clamp(r, 0, 3)); }
static void cg_set_feel(int32_t f) { cg.rate = (uint8_t)((cg.rate & 3u) | (uint32_t)clamp(f, 0, CF_N - 1) << 2); }
/* the chords' lengths in eighths (FEEL, RATE) into d[]; the loop's */
static uint32_t cg_durs(uint32_t *d)
{
    uint32_t n = (uint32_t)clamp(cg.num, 2, CG_MAX), f = cg_feel(), rb = CG_RATE_BEATS[cg_rate()], i, sum = 0;
    for (i = 0; i < n; i++) {                        /* (a half RATE is RATE's beats in eighths) */
        d[i] = (f == CF_EVEN ? 2u : f == CF_ACCEL ? CG_ACCEL[n - 2u][i] : CG_TONIC[n - 2u][i]) * rb;
        sum += d[i];
    }
    return sum;
}
/* the node at eighth pos8 of the transport */
static uint32_t cg_node_at8(uint32_t pos8)
{
    uint32_t d[CG_MAX], cyc = cg_durs(d), i = 0;
    pos8 %= cyc;
    while (pos8 >= d[i]) {
        pos8 -= d[i];
        i++;
    }
    return i;
}
/* the node the transport is on (HOLD, STEPS), -1 stopped: PUSH, an eighth early */
static int cg_node_now(void)
{
    if (!song.playing || !cg_on())
        return -1;
    return (int)cg_node_at8(clk_beat * 2u + clk_pos / (BEAT_U / 2u) + (cg_feel() == CF_PUSH));
}
static void cg_release(void)
{
    uint32_t j;
    for (j = 0; j < cgr.nn; j++) {
        trk_note_off(&trk[1], cgr.notes[j]);
        seq_out_off(&trk[1], cgr.notes[j]);
    }
    cgr.nn = 0;
}
static void cg_sound(uint32_t node)                  /* node's chord, held (released by cg_release) */
{
    uint32_t j;
    cg_release();
    cgr.nn = (uint8_t)cg_voice(node, cgr.notes);
    memcpy(cgr.lead, cgr.notes, cgr.nn);
    cgr.nlead = cgr.nn;
    for (j = 0; j < cgr.nn; j++) {
        trk_note_chord(&trk[1], cgr.notes, cgr.nn, j, 100);
        seq_out_on(&trk[1], cgr.notes[j], 100);
    }
}
/* once a block, after the steps (events_block): the live nodes, then HOLD's chord changes on the bars */
static void cg_block(void)
{
    uint32_t live = cgr.live;
    int now;
    if (live) {                                      /* a node held: it, the last one pressed wins */
        uint32_t fresh = live & ~cgr.live_seen, k = 0;
        cgr.live_seen = (uint8_t)live;
        if (fresh) {
            while (k < 7u && !((fresh >> k) & 1u))
                k++;
            cgr.now = (int8_t)k;
            cg_sound(k);
        }
        return;
    }
    if (cgr.live_seen) {                             /* let go: back to the transport's, or silence */
        cgr.live_seen = 0;
        cg_release();
        cgr.now = -1;
    }
    now = cg.play == CP_HOLD ? cg_node_now() : -1;
    if (now != cgr.now) {
        cgr.now = (int8_t)now;
        if (now < 0)
            cg_release();
        else
            cg_sound((uint32_t)now);
    }
    if (cg.play == CP_STEPS)
        cgr.now = (int8_t)cg_node_now();
}
/* PLAY STEPS: a step of the CHORDS track with a note plays the chord of the moment instead: its notes into n[] */
static uint32_t cg_step_notes(uint8_t *n)
{
    int node = cg_node_now();
    uint32_t k;
    if (node < 0)
        return 0;
    k = cg_voice((uint32_t)node, n);
    memcpy(cgr.lead, n, k);
    cgr.nlead = (uint8_t)k;
    return k;
}
/* BASS FOLLOW: what the BASS track's step notes move by (its root's nearest way to the chord's), 0 = none */
static int32_t cg_follow(void)
{
    int node;
    int32_t r;
    if (!cg.follow || !cg_on())
        return 0;
    node = cgr.live ? cgr.now : cg_node_now();
    if (node < 0)
        return 0;
    r = (int32_t)(cgr.prog[node % (int)CG_MAX] & 15u);
    return r > 6 ? r - 12 : r;
}
