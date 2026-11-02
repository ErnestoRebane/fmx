/* SPDX-License-Identifier: GPL-3.0-only */
/* The CHOP track (FMX): the fourth track, for what the other three are not, in three MODEs (SEL on its screen,
 * ui_chop.c). The material is the sample's; the variation is the hands' (the FX panel, the keys) and, in SLICE, a
 * generator that answers the BREAK instead of doubling it.
 *   SLICE    a loop the loop page cut into slices (a piano riff, a perc loop, a Memphis recital, an anime line), a
 *            slice a trigger, in phase with the drums from PLAY. Its generator is the BREAK's (brkpart.c, BB Gen: a
 *            MOVE to another slice weighted by an anchor that keeps the downbeat, or a STAY that repeats or walks),
 *            with what a loop with harmony needs:
 *            - a slice is only ever swapped for one of the same bar (the same chord): the loop's harmony stays where
 *              it is against the bass and the pads;
 *            - it answers the break: in the BREAK's fill bar (its PHRASE's last) the loop plays straight, and the
 *              bar before it is the loop's own (more moves, the only place it rolls); never a roll while the break
 *              rolls.
 *            STYLE: STRAIGHT (the loop as it is), HOLD (a beat stuck, as a record that skips back), SKIP (beats of a
 *            bar swapped about), ENDS (straight, moving only in its own bar), HALF (half speed: an octave down, a
 *            slice every two), CHOP (all of it, rolls in its bar). CMPLX how much, RETRIG the rolls (50: the STYLE's).
 *            A white key held: its slice on every trigger (let go: the generator again).
 *   PADS     a kit (shouts, FX, hits: a sound pack, the editor's CHOP cuts) played by hand: a hit a white key.
 *   INST     an instrument (the analog and sampled leads, plucks, stabs: PRESETS) and its MELODY, a theme of 8 bars
 *            over the song's own chords (cm_build: MIDI, the user's; ARP, SING; OFF), THEME moves it on (MIDI: picks); the
 *            white keys walk the session's scale (the CHORDS generator's ROOT and SCALE) from C4, in the keys' octave.
 * In every mode REC records the white keys into the track's steps (LENGTH 1, 2, 4 bars; RATE the grid, SWING) and the
 * steps play them: in SLICE a recorded slice plays in place of the generator's on its step.
 *   the black keys (held)  1 STUTTER (what sounds again every 16th), 2 GAP (silence), 3 DOWN (an octave down: the tape
 *            slowed; INST: the notes)
 *   TUNE     (SLICE, the loop's key known) OFF; KEY: the loop to the session's key the nearest way, -5..+6;
 *            SNAP: each slice's note pulled to the session's scale; FOLLOW: KEY, and with the chords (the chord's
 *            root, as BASS FOLLOW). By the playback speed (eng_sample.c cs_tune16).
 * The settings are the CHOP track's p[P_X0..P_X7] (params.c; kept with the project). The generator and the keys held
 * are the audio ISR's (cs_tick, at the top of the CHOP track's seq_tick; cs_step as its steps play). */
#define CS_TRK 2u                        /* the CHOP track */
#define CS_STEPS 64u                     /* the longest recording: 4 bars of 1/16 */
enum { CM_PADS, CM_SLICE, CM_INST, CM_N };
enum { CT_OFF, CT_KEY, CT_SNAP, CT_FOLLOW, CT_N };
enum { CY_STRAIGHT, CY_HOLD, CY_SKIP, CY_ENDS, CY_HALF, CY_CHOP, CY_N };   /* SLICE's STYLE */
#define CF_STUT 1u                       /* the black keys held */
#define CF_GAP 2u
#define CF_DOWN 4u
#define CSV(i) (trk[CS_TRK].p[P_X0 + (i)])
static uint32_t cs_mode(void) { return (uint32_t)CSV(0) < CM_N ? (uint32_t)CSV(0) : CM_PADS; }
static int cs_slice(void) { return cs_mode() == CM_SLICE; }
static uint32_t cs_style(void) { return (uint32_t)CSV(1) % CY_N; }               /* SLICE: STYLE */
static uint32_t cs_cmplx(void) { return (uint32_t)clamp(CSV(2), 0, 100); }       /* ... CMPLX */
static uint32_t cs_retrig(void) { return (uint32_t)clamp(CSV(3), 0, 100); }      /* ... RETRIG: 50 the STYLE's rolls */
static uint32_t cs_tune(void) { return (uint32_t)CSV(5) % CT_N; }                 /* ... TUNE */
/* TIME (the CHOP's third page; CSV(7), 0 in an older project): each MODE's RATE, 2 bits a mode from bit 0 (the steps'
 * grid; SLICE: the loop's speed too; INST: the theme's too), and INST's OCT (the theme's octave, 3 bits from bit 6) */
enum { CR_16, CR_8, CR_4, CR_32, CR_N };            /* (0: 1/16, what an older project played) */
static const char *const N_CS_RATE[CR_N] = {"1/16", "1/8", "1/4", "1/32"};
static const uint8_t CR_U32[CR_N] = {2, 4, 8, 1};     /* a step, in 32nds */
static const uint8_t CR_SDIV[CR_N] = {2, 1, 0, 3};    /* ... as the steps' DIV (params.c N_SDIV) */
static const uint8_t CR_ORDER[CR_N] = {CR_4, CR_8, CR_16, CR_32};   /* the knob's way: slower to faster (as DIV) */
static uint32_t cs_rate(uint32_t mode) { return ((uint32_t)CSV(7) >> (2u * mode)) & 3u; }
static uint32_t cs_u32(uint32_t mode) { return CR_U32[cs_rate(mode)]; }
static int32_t cs_oct(void) { int32_t o = (int32_t)(((uint32_t)CSV(7) >> 6) & 7u); return o >= 4 ? o - 8 : o; }
static void cs_rate_step(uint32_t mode, int32_t s)  /* RATE of a mode a step faster (> 0) or slower */
{
    uint32_t i;
    for (i = 0; i < CR_N && CR_ORDER[i] != cs_rate(mode); i++)
        ;
    i = (uint32_t)clamp((int32_t)i + s, 0, CR_N - 1);
    CSV(7) = (int16_t)(((uint32_t)CSV(7) & ~(3u << (2u * mode))) | (uint32_t)CR_ORDER[i] << (2u * mode));
    if (mode == cs_mode())                          /* (the steps' grid at once; cs_poll keeps it so) */
        trk[CS_TRK].p[P_SDIV] = (int16_t)CR_SDIV[cs_rate(mode)];
}
static void cs_oct_set(int32_t o)
{
    CSV(7) = (int16_t)(((uint32_t)CSV(7) & ~(7u << 6)) | ((uint32_t)clamp(o, -2, 2) & 7u) << 6);
}
static const char *const N_CS_MODE[CM_N] = {"PADS", "SLICE", "INST"};
static const char *const N_CS_STYLE[CY_N] = {"STRAIGHT", "HOLD", "SKIP", "ENDS", "HALF", "CHOP"};
/* a STYLE: its CMPLX (set when STYLE turns), ANCHR, the STAY branch's share (BB Gen's ROLL), moves in every bar (else
 * in its own bar only), the rolls' chances a bar in its own bar: 2X 3X 4X 8X */
static const uint8_t CS_STY[CY_N][8] = {
    [CY_STRAIGHT] = {0, 0, 0, 1, 0, 0, 0, 0},
    [CY_HOLD] = {35, 50, 85, 1, 0, 0, 0, 0},
    [CY_SKIP] = {40, 40, 10, 1, 0, 0, 0, 0},
    [CY_ENDS] = {60, 30, 25, 0, 30, 10, 20, 0},
    [CY_HALF] = {0, 0, 0, 1, 0, 0, 0, 0},
    [CY_CHOP] = {60, 30, 25, 1, 25, 10, 20, 10},
};

/* what the loop page found, after a user slot's zones (eng_sample.c smp_user_hdr_t: 480 B, the data at 512) */
#define CS_META_AT 480u
#define CS_META_MAGIC 0x31434C53u        /* "SLC1" */
#define CS_NOKEY 0xFFu
typedef struct {
    uint32_t magic;
    uint8_t key;                         /* the tonic 0..11 | 0x10 minor; CS_NOKEY: no clear pitch (drums, rap) */
    uint8_t bars;                        /* the loop's bars (1, 2, 4, 8), 0 = not known */
    uint8_t n;                           /* its slices (the slot's zones, in order) */
    uint8_t rsv;
    uint8_t note[16];                    /* each slice's note (MIDI), CS_NOKEY = none (chords, no pitch) */
    uint32_t rsv2[2];
} cs_meta_t;
_Static_assert(sizeof(cs_meta_t) == 32u && CS_META_AT + 32u <= SMP_USER_DATA, "the meta fits before the slot's data");

static struct {
    uint32_t at;                         /* the 1/32 handled last + 1 (0: from PLAY) */
    uint32_t rng;                        /* SLICE's dice (the ISR's) */
    uint8_t rt_n, rt_per, rt_note;       /* ... this trigger's retriggers: how many, 1/32s apart, the note */
    uint32_t rt_at;                      /* ... the 1/32 of its trigger */
    volatile uint8_t cur;                /* ... the slice it played last */
    volatile uint8_t hist[16], hist_n;   /* ... this bar's triggers: the slice each (| 0x80: moved off its place) */
    volatile uint8_t bb;                 /* ... where the break is: 2 its fill bar, 1 the loop's own bar, 0 else */
    uint8_t mel_on[4], mel_end[4];       /* INST: the theme's notes sounding, the 16th each ends on */
    volatile uint8_t held;               /* the note a white key went down on (SLICE: its slice while held) */
    volatile uint8_t fx;                 /* the black keys held: CF_* (the UI) */
    volatile uint8_t sl;                 /* the screen: the slice sounding (0xFF none), */
    volatile uint8_t pos;                /* ... the loop's 16th */
    volatile uint8_t hit;                /* ... the note last started (a step, the generator, a key) */
    volatile uint32_t hits;              /* ... bumped with it */
    volatile uint32_t gen;               /* ... bumped on every write */
} csr = {.sl = 0xFFu, .rng = 0x2545F491u};

/* the kit the track plays: its lowest key, its slices; 0 = the sound is not a kit */
static int cs_kit(uint32_t *base, uint32_t *n)
{
    const track_t *t = &trk[CS_TRK];
    return ENGINES[t->eng_req % NENGINES] == &ENG_SAMPLE && smp_kit((uint32_t)t->p[P_E0] % SMP_NALL, base, n);
}
/* the loop page's findings for the kit playing, 0 = none (a built-in kit, a slot the loop page did not cut) */
static const cs_meta_t *cs_meta(void)
{
    uint32_t si = (uint32_t)trk[CS_TRK].p[P_E0] % SMP_NALL, k;
    const cs_meta_t *m;
    if (si - SMP_NSETS >= SMP_POOL_N || ENGINES[trk[CS_TRK].eng_req % NENGINES] != &ENG_SAMPLE)
        return 0;
    k = si - SMP_NSETS;
    if (!usr_nz[k])
        return 0;
    m = (const cs_meta_t *)(smp_user_xip(k) + CS_META_AT);
    return m->magic == CS_META_MAGIC && m->n == usr_nz[k] ? m : 0;
}
static int cs_keyed(const cs_meta_t *m) { return m && m->key != CS_NOKEY && (m->key & 15u) < 12u; }

/* the 16ths of a slice: the loop's bars over its slices (not known: a beat) */
static uint32_t cs_sl16(uint32_t n)
{
    const cs_meta_t *m = cs_meta();
    uint32_t l = m && m->bars && n ? 16u * m->bars / n : 4u;
    return l ? l : 1u;
}
/* degree d (0 the root; past the scale's notes: the next octave) of the session's scale, over C of octave note c */
static int32_t cs_deg(int32_t d, int32_t c)
{
    uint32_t m = cg_mask(), deg[12], n = 0, i;
    int32_t o;
    for (i = 0; i < 12u; i++)
        if ((m >> i) & 1u)
            deg[n++] = i;
    if (!n)
        return c + (int32_t)cg_root();
    o = d >= 0 ? d / (int32_t)n : -((-d + (int32_t)n - 1) / (int32_t)n);
    return c + (int32_t)cg_root() + (int32_t)deg[d - o * (int32_t)n] + 12 * o;
}
/* the note white key w plays in INST: the session's scale from C4 (OCT) */
static uint32_t cs_key_note(uint32_t w)
{
    return (uint32_t)clamp(cs_deg((int32_t)w, 60 + 12 * song.octave), 0, 127);
}
static int cs_has_notes(void)
{
    uint32_t i;
    for (i = 0; i < trk_len(&trk[CS_TRK]); i++)
        if (trk[CS_TRK].step[i].time == ST_NOTE && trk[CS_TRK].step[i].n)
            return 1;
    return 0;
}

/* ---- SLICE's generator (the audio ISR) ------------------------------------------------------------------- */
static uint32_t cs_rnd(void) { return bl_rand(&csr.rng) >> 16; }   /* 0..65535 */
static int cs_chance(uint32_t pct) { return cs_rnd() < pct * 65536u / 100u; }
/* where the BREAK is in its phrase at 16th p16 from PLAY: 2 its fill bar (it speaks: the loop plays straight), 1 the
 * bar before it (the loop's own: it moves and may roll), 0 else; with PHRASE off, a phrase of 4. Its bar counted on its
 * own grid (DIV 1/8: a "bar" of 16 steps is two), as brkpart.c bp_step counts it (the CHOP's block comes before the
 * BREAK's: bp.bar is the bar before on a bar's first trigger) */
static uint32_t cs_brk_bar(uint32_t p16)
{
    uint32_t pb = BP_PHRASE_BARS[(uint32_t)BPV(BP_PHRASE) % 5u], div = (uint32_t)clamp(TBRK->p[P_SDIV], 0, 2), b;
    pb = pb ? pb : 4u;
    b = p16 / (4u >> div) / 16u % pb;
    return b == pb - 1u ? 2u : b == pb - 2u ? 1u : 0u;
}
/* the slice to play at the loop's slice pos (of n, spb a bar): BB Gen's choice (brkpart.c bp_select) inside pos's bar */
static uint32_t cs_select(uint32_t pos, uint32_t n, uint32_t spb, uint32_t own)
{
    const uint8_t *s = CS_STY[cs_style()];
    uint32_t c = cs_cmplx(), a = s[1], r = s[2], b0 = pos / spb * spb, at = (pos - b0) * 8u / spb;
    uint32_t cur = csr.cur >= b0 && csr.cur < b0 + spb ? csr.cur : pos;
    if (!c || spb < 2u || (!s[3] && !own))
        return pos;                                      /* (straight: no complexity, a slice a bar, or not its bar) */
    if (own)                                             /* (its own bar: more) */
        c += (100u - c) / 2u;
    if (cs_chance(100u - r)) {                           /* MOVE: another of the bar's, the anchor keeping the downbeat */
        if (cs_chance(c * bp_weight(at, a) / 1024u))
            return b0 + (cs_rnd() * spb >> 16);
        return pos;
    }
    if (cs_chance(c)) {                                  /* STAY: the one before again (HOLD), else a walk in the bar */
        uint32_t w = bp_weight(at, a);
        if (w < 1024u ? cs_chance(100u - w * 100u / 1024u) : 0)
            return pos;
        return cs_chance(70u) ? cur : b0 + (cur - b0 + (cs_chance(50u) ? 1u : spb - 1u)) % spb;
    }
    return pos;
}
/* the retriggers of a trigger in the loop's own bar: 1 (none), 2, 3, 4 or 8, by the STYLE's chances a bar times RETRIG
 * (50: as the STYLE says) spread over the bar's triggers; one of those that hit at random */
static uint32_t cs_rolls(uint32_t per_bar)
{
    static const uint8_t R[4] = {2, 3, 4, 8};
    const uint8_t *s = CS_STY[cs_style()];
    uint32_t i, nh = 0, k = cs_retrig();
    uint8_t hit[4];
    for (i = 0; i < 4u; i++) {
        uint32_t p = (uint32_t)s[4u + i] * k / 50u;
        p = p > 100u ? 100u : p;
        if (p && cs_chance((p + per_bar - 1u) / per_bar))
            hit[nh++] = R[i];
    }
    return nh ? hit[cs_rnd() * nh >> 16] : 1u;
}

/* ---- INST: the melody (the main loop builds it, the audio ISR plays it) ---------------------------------- */
/* An anime / game theme of 8 bars over the song's own CHORDS progression (the chord of each eighth: chordgen.c
 * cg_node_at8), its notes chosen on it: a chord tone on each beat, the scale between. Each song its own theme (its
 * VARIANT and key seed it; THEME moves it on). MELODY:
 *   MIDI   8 bars of a MIDI file's melody the user sent from the companion app (the melody bank, cm_mel_midi):
 *          THEME picks which; to the song's key and fitted to its chords (it replaced MOTIF, 2026-10-30)
 *   ARP    a hook in eighths over the chord's tones, one figure and one rhythm with gaps all through, the phrase
 *          closed on the tonic
 *   SING   a singing line of halves and quarters rising to bar 6, down to the tonic; bar 3 as bar 1
 * (ARP and SING prototyped as renders and chosen by ear, 2026-10-26: build/mel.) It plays over the track's steps, which stay the
 * hand's: REC records on top. */
enum { CML_OFF, CML_MIDI, CML_ARP, CML_SING, CML_N };
static const char *const N_CS_MEL[CML_N] = {"OFF", "MIDI", "ARP", "SING"};
static uint32_t cs_melody(void) { return (uint32_t)CSV(4) % CML_N; }              /* INST: MELODY */
static uint32_t cs_theme(void) { return (uint32_t)CSV(6) % 100u; }               /* ... THEME */
#define CM_BARS 8u
#define CM_NEV 160u
#define CM_LO 67                         /* the theme's range: G4 .. D6 */
#define CM_HI 86
typedef struct { uint8_t at, len, note, vel; } cm_ev_t;   /* at: the 16th of the 8 bars; len: 16ths */
static cm_ev_t cm_ev[2][CM_NEV] __attribute__((section(".pool")));   /* (big: the pool, not .bss) */
static volatile uint8_t cm_n[2], cm_b;   /* the buffer the ISR plays (the main loop writes the other, then flips) */
static uint32_t cm_sig = 0xFFFFFFFFu, cm_rs;
static cm_ev_t cm_w[CM_NEV] __attribute__((section(".pool")));   /* (the one being written) */
static uint32_t cm_wn;
static uint32_t cm_rn(uint32_t n) { return bl_rand(&cm_rs) % n; }
/* the chord at 16th s: its pitch classes (the triad first), its count */
static uint32_t cm_pcs(uint32_t s, uint8_t *pc)
{
    uint32_t node = cg_node_at8(s / 2u + (cg_feel() == CF_PUSH)), code = cgr.prog[node % CG_MAX], i, n;
    uint8_t iv[6];
    n = cg_tones(code, node, iv);
    for (i = 0; i < n; i++)
        pc[i] = (uint8_t)((cg_root() + (code & 15u) + iv[i]) % 12u);
    return n;
}
static int cm_scale(int32_t x) { return (cg_mask() >> (uint32_t)((x - (int32_t)cg_root() + 120) % 12)) & 1u; }
static int cm_ctone(int32_t x, uint32_t s, uint32_t triad)
{
    uint8_t pc[6];
    uint32_t n = cm_pcs(s, pc), i;
    if (triad && n > 3u)
        n = 3u;
    for (i = 0; i < n; i++)
        if ((uint32_t)((x % 12 + 12) % 12) == pc[i])
            return 1;
    return 0;
}
static int32_t cm_fold(int32_t x)
{
    while (x > CM_HI)
        x -= 12;
    while (x < CM_LO)
        x += 12;
    return x;
}
static int32_t cm_near(int32_t x, uint32_t s)        /* the chord tone (the triad's) nearest x */
{
    int32_t d;
    for (d = 0; d < 7; d++) {
        if (cm_ctone(x + d, s, 1))
            return cm_fold(x + d);
        if (cm_ctone(x - d, s, 1))
            return cm_fold(x - d);
    }
    return cm_fold(x);
}
static int32_t cm_step(int32_t x, int32_t k)         /* k steps through the scale */
{
    uint32_t guard = 64;
    while (k > 0 && guard--) {
        x++;
        while (!cm_scale(x) && guard--)
            x++;
        k--;
    }
    while (k < 0 && guard--) {
        x--;
        while (!cm_scale(x) && guard--)
            x--;
        k++;
    }
    return x;
}
static int32_t cm_tonic(int32_t x)                   /* the tonic nearest x in the range */
{
    int32_t best = CM_LO, d = 99, y;
    for (y = CM_LO; y <= CM_HI; y++)
        if (!((y - (int32_t)cg_root()) % 12) && (y > x ? y - x : x - y) < d)
            d = y > x ? y - x : x - y, best = y;
    return best;
}
static void cm_put(uint32_t at, uint32_t len, int32_t note, uint32_t vel)
{
    if (cm_wn < CM_NEV && at < CM_BARS * 16u) {
        cm_w[cm_wn].at = (uint8_t)at;
        cm_w[cm_wn].len = (uint8_t)(len ? len : 1u);
        cm_w[cm_wn].note = (uint8_t)clamp(note, 0, 127);
        cm_w[cm_wn].vel = (uint8_t)vel;
        cm_wn++;
    }
}
static void cm_legato(uint32_t max)                  /* each note up to the next, max at most */
{
    uint32_t i;
    for (i = 0; i + 1u < cm_wn; i++) {
        uint32_t gap = (uint32_t)cm_w[i + 1u].at - cm_w[i].at;
        cm_w[i].len = (uint8_t)(gap < max ? gap : max);
    }
}
/* MIDI: a theme the user brought (the companion app cuts 8 bars of a MIDI file's melody into the melody bank, a pool
 * entry with the "MEL1" meta: eng_sample.c SMP_MEL_MAGIC). THEME picks one of the bank (modulo its count). It goes to the
 * song's key the nearest way (its own tonic to the song's ROOT, -6..+5), by octaves into the theme's range, then each
 * note fitted: off the song's scale -> the scale's neighbour (towards the song's third: a minor melody in a major
 * song takes the major third, and the other way), and a held note on a strong beat (or one of half a bar) that is
 * not a tone of the chord under it -> the chord's tone a semitone off, the line's way kept (cm_fit). The rhythm
 * and the contour stay the file's.
 * The bank (the entry's data): 4 bytes of the silent zone, u16 offsets of the n themes (from the data), each theme
 * {name[8], key (the tonic 0..11 | 0x10 minor), count, count x {at, len, note, vel}} (cm_ev_t). */
typedef struct { uint32_t magic; uint8_t n, rsv[3]; uint32_t rsv2[6]; } cm_bank_t;   /* the meta at +480 */
_Static_assert(sizeof(cm_bank_t) == 32u, "the bank's meta is the slot meta's 32 bytes");
#define CM_REC 10u                                   /* a theme's head: name, key, count */
/* the bank (its data and length; 0: none in the pool) and its count */
static const uint8_t *cm_bank(uint32_t *n, uint32_t *len)
{
    uint32_t k;
    for (k = 0; k < SMP_POOL_N; k++)
        if (usr_nz[k]) {
            const uint8_t *p = smp_user_xip(k);
            const cm_bank_t *b = (const cm_bank_t *)(p + CS_META_AT);
            if (b->magic == SMP_MEL_MAGIC && b->n) {
                *n = b->n;
                *len = ((const smp_user_hdr_t *)p)->data_len;
                return p + SMP_USER_DATA;
            }
        }
    return 0;
}
/* theme i of the bank (its head; 0: none or broken) */
static const uint8_t *cm_theme_rec(uint32_t i)
{
    uint32_t n, len, off;
    const uint8_t *d = cm_bank(&n, &len);
    if (!d || 4u + 2u * n > len)
        return 0;
    i %= n;
    off = d[4u + 2u * i] | (uint32_t)d[5u + 2u * i] << 8;
    if (off + CM_REC > len || d[off + 9u] > CM_NEV || off + CM_REC + 4u * d[off + 9u] > len)
        return 0;
    return d + off;
}
/* THEME's count: MIDI the bank's (0: no bank sent), else 100 */
static uint32_t cm_themes(void)
{
    uint32_t n = 0, len;
    if (cs_melody() != CML_MIDI)
        return 100u;
    return cm_bank(&n, &len) ? n : 0u;
}
/* x (the file's note moved to the key) on the song's scale: an off-scale note to its neighbour towards the song's
 * third (a minor melody in a major song takes the major third, and the other way), or the other neighbour when that
 * keeps the line's way from the note before (prev, dir: -1, 0, 1; prev < 0: the first) */
#define CM_WAY(y, prev) ((y) > (prev) ? 1 : (y) < (prev) ? -1 : 0)
static int32_t cm_inscale(int32_t x, int32_t prev, int32_t dir)
{
    int32_t up = (cg_mask() >> 4) & 1u ? 1 : -1;
    if (cm_scale(x))
        return x;
    if (!cm_scale(x + up) || (prev >= 0 && CM_WAY(x + up, prev) != dir && cm_scale(x - up)))
        up = -up;
    return cm_scale(x + up) ? x + up : x;
}
/* ... and a held note on a strong beat (or one of half a bar) at 16th s a semitone onto the chord under it, when
 * that keeps the line's way */
static int32_t cm_fit(int32_t x, uint32_t s, uint32_t len, int32_t prev, int32_t dir)
{
    int32_t y;
    x = cm_inscale(x, prev, dir);
    if (!((!(s % 8u) && len >= 4u) || len >= 8u) || cm_ctone(x, s, 0))
        return x;
    for (y = x - 1; y <= x + 1; y += 2)
        if (cm_ctone(y, s, 0) && (prev < 0 || CM_WAY(y, prev) == dir))
            return y;
    return x;
}
static void cm_mel_midi(void)
{
    const uint8_t *r = cm_theme_rec(cs_theme()), *e;
    uint32_t ne, i;
    int32_t sh, sum = 0, oct, prev = 0;
    if (!r)
        return;
    ne = r[9];
    e = r + CM_REC;
    sh = ((int32_t)cg_root() - (int32_t)(r[8] & 15u) + 18) % 12 - 6;
    for (i = 0; i < ne; i++)
        sum += e[4u * i + 2u];
    oct = ne ? (CM_LO + CM_HI) / 2 - (sum / (int32_t)ne + sh) : 0;
    oct = (oct + 126) / 12 * 12 - 120;               /* (to the nearest octave) */
    for (i = 0; i < ne; i++, e += 4) {
        int32_t x = e[2] + sh + oct, dir = i ? (e[2] > e[-2] ? 1 : e[2] < e[-2] ? -1 : 0) : 0;
        while (x < CM_LO - 7)
            x += 12;
        while (x > CM_HI + 7)
            x -= 12;
        x = cm_fit(x, e[0], e[1], i ? prev : -1, dir);
        cm_put(e[0], e[1], x, e[3] ? e[3] : 100u);
        prev = x;
    }
}
/* ARP */
static void cm_mel_arp(void)
{
    static const int8_t SH[6][8] = {{0, 1, 2, 3, 2, 1, 2, 3}, {0, 2, 1, 3, 2, 4, 3, 2}, {3, 2, 1, 0, 1, 2, 3, 4},
                                   {0, 1, 2, 1, 3, 2, 4, 3}, {2, 0, 1, 2, 3, 1, 2, 4}, {0, 2, 4, 2, 3, 1, 2, 0}};
    static const char *const RH[4] = {"xxxxxxxx", "xx-xxx-x", "x-xxx-xx", "xxx-xx-x"};
    const int8_t *sh = SH[cm_rn(6)];
    const char *rh = RH[cm_rn(4)];
    uint32_t bar, i;
    int32_t base = 69;
    for (bar = 0; bar < CM_BARS; bar++) {
        uint32_t t = bar * 16u;
        for (i = 0; i < 8u; i++) {
            int32_t lad[8], k = 0, y;
            if (rh[i] != 'x' || (bar == CM_BARS - 1u && i > 4u))
                continue;
            for (y = base; y < base + 24 && k < 8; y++)   /* the chord's tones (of this eighth) up from base: the ladder */
                if (cm_ctone(y, t + i * 2u, 0))
                    lad[k++] = y;
            if (k)
                cm_put(t + i * 2u, 2, lad[(uint32_t)sh[i] % (uint32_t)k], i % 4u ? 86 : 104);
        }
        if (bar == CM_BARS - 1u)
            cm_put(t + 10u, 6, cm_tonic(base + 7), 100);   /* (the phrase's end) */
        base = cm_near(base, t + 16u) - 2;           /* (the next chord's ladder from near this one's) */
        if (base < 64)
            base += 12;
        if (base > 74)
            base -= 12;
    }
    cm_legato(2);
}
/* SING */
static void cm_mel_sing(void)
{
    static const char *const RB[6] = {"x-------x---x---", "x---x---x-------", "x-----x---x-----", "x-----x-x-------",
                                      "x---x-x-x-------", "x-------x-----x-"};
    uint32_t bar, i, pick[CM_BARS];
    int32_t x = cm_near(72, 0);
    for (bar = 0; bar < CM_BARS; bar++)
        pick[bar] = cm_rn(6);
    pick[2] = pick[0];                               /* (the line comes back: bar 3 as bar 1) */
    for (bar = 0; bar < CM_BARS; bar++) {
        uint32_t t = bar * 16u;
        int32_t target = bar < 6u ? 72 + (int32_t)bar * 2 : 72;
        const char *rb = bar == CM_BARS - 1u ? "x---x-----------" : RB[pick[bar]];
        for (i = 0; i < 16u; i++) {
            if (rb[i] != 'x')
                continue;
            if (bar == CM_BARS - 1u && i == 4u) {
                x = cm_tonic(x);
                cm_put(t + i, 12, x, 100);
                continue;
            }
            x = cm_step(x, x < target ? (int32_t)(1u + cm_rn(2)) : -(int32_t)(1u + cm_rn(2)));
            if (!(i % 4u))
                x = cm_near(x, t + i);
            x = cm_fold(x);
            cm_put(t + i, 4, x, i ? 92 : 106);
        }
    }
    cm_legato(12);
}
/* the theme for what the song is now (the main loop, cs_poll): written again when the MELODY, THEME, the key or the
 * progression change; the ISR switches to it at once (from where it is) */
static void cm_build(void)
{
    uint32_t mel = cs_mode() == CM_INST ? cs_melody() : CML_OFF, b;
    uint32_t sig = mel * 7919u + cs_theme() * 131u + cgr.gen * 977u + cg.var * 31u + cg_root() * 7u + cg_mask() * 3u +
                   cg.num + cg.rate * 101u + smp_user_gen * 65537u;   /* (MIDI: the bank sent again) */
    if (sig == cm_sig)
        return;
    cm_sig = sig;
    cm_wn = 0;
    cm_rs = 0x9E3779B9u ^ (cg.var * 977u + cg_root() * 31u + cs_theme() * 7919u + 1u);   /* (each song its own) */
    if (mel == CML_MIDI)
        cm_mel_midi();
    else if (mel == CML_ARP)
        cm_mel_arp();
    else if (mel == CML_SING)
        cm_mel_sing();
    b = cm_b ^ 1u;
    memcpy(cm_ev[b], cm_w, sizeof cm_w);
    cm_n[b] = (uint8_t)cm_wn;
    fm1_irq_off();
    cm_b = (uint8_t)b;
    fm1_irq_on();
    csr.gen++;
}

/* ---- playing (the audio ISR) ------------------------------------------------------------------------------ */
/* the step about to play, as it plays (seq.c seq_step): 0 = silent (GAP held), else s or a copy in o (INST: DOWN an
 * octave down) */
static const step_t *cs_step(const track_t *t, const step_t *s, step_t *o)
{
    (void)t;
    if (s->time != ST_NOTE || !s->n)
        return s;
    if (csr.fx & CF_GAP)
        return 0;
    if (cs_mode() == CM_INST && (csr.fx & CF_DOWN)) {
        uint32_t j;
        *o = *s;
        for (j = 0; j < o->n; j++)
            o->note[j] = (uint8_t)clamp((int32_t)o->note[j] - 12, 0, 127);
        return o;
    }
    return s;
}
/* a note started on the CHOP track (a step, the generator, a key): what the screen shows as playing now */
static void cs_hit(uint32_t note)
{
    csr.hit = (uint8_t)note;
    csr.hits++;
}
/* a white key of the CHOP track down (seq.c input_on): SLICE plays its slice on every trigger while it is held */
static void cs_key(uint32_t note)
{
    csr.held = (uint8_t)note;
    cs_hit(note);
}
static void cs_on(track_t *t, uint32_t note, uint32_t vel)
{
    trk_note_on(t, note, vel);
    seq_out_on(t, note, vel);
    cs_hit(note);
}
/* every block of the CHOP track, before its steps: the keys held (STUTTER, GAP, DOWN), INST's theme, SLICE's generator
 * (noinline: a block's work, not a sample's; kept out of the mix's own code, tests/target_budget.py) */
__attribute__((noinline)) static void cs_tick(track_t *t)
{
    uint32_t base = 0, n = 0, kit = (uint32_t)cs_kit(&base, &n), p32, p16, sl, spb, T32, ph;
    uint32_t su = cs_u32(CM_SLICE), mu = cs_u32(CM_INST), fx = csr.fx, half = cs_slice() && cs_style() == CY_HALF;
    int16_t dn = (int16_t)(((fx & CF_DOWN) || half) && cs_mode() != CM_INST ? -12 * 16 : 0);
    if (cs_dn16 != dn)
        cs_dn16 = dn;
    if (!song.playing) {
        uint32_t i;
        for (i = 0; i < 4u; i++)                     /* (the theme's notes end with the transport) */
            if (csr.mel_on[i]) {
                trk_note_off(t, csr.mel_on[i]);
                csr.mel_on[i] = 0;
            }
        csr.at = 0;
        csr.sl = 0xFFu;
        return;
    }
    p32 = clk_beat * 8u + clk_pos * 8u / BEAT_U;
    if (csr.at == p32 + 1u)
        return;
    if (!csr.at) {                                       /* PLAY: the dice from the start */
        csr.rng = 0x2545F491u ^ ((uint32_t)CSV(1) + 1u) * 2654435761u;
        csr.cur = 0;
        csr.rt_n = 0;
    }
    csr.at = p32 + 1u;
    p16 = p32 >> 1;
    if (fx & CF_GAP) {                                   /* GAP: silence while held */
        if (t->seq_n)
            seq_release(t);
        trk_all_off(t);
        memset(csr.mel_on, 0, sizeof csr.mel_on);
        csr.rt_n = 0;
        csr.sl = 0xFFu;
        return;
    }
    if ((fx & CF_STUT) && !(p32 & 1u)) {                 /* STUTTER: what sounds again every 16th */
        uint32_t note = t->seq_n ? t->seq_notes[0] : csr.hit;
        trk_note_off(t, note);
        cs_on(t, note, 96);
    }
    if (cs_mode() == CM_INST && !(p32 % mu)) {       /* INST: the theme, a step of its RATE at a time */
        const cm_ev_t *e = cm_ev[cm_b];
        uint32_t pos = p32 / mu % (CM_BARS * 16u), i, ne = cm_n[cm_b], dn = (fx & CF_DOWN) ? 12u : 0u;
        int32_t oc = cs_oct() * 12;
        for (i = 0; i < 4u; i++)                     /* (the notes that end here) */
            if (csr.mel_on[i] && csr.mel_end[i] == pos) {
                trk_note_off(t, csr.mel_on[i]);
                seq_out_off(t, csr.mel_on[i]);
                csr.mel_on[i] = 0;
            }
        for (i = 0; i < ne && !(fx & CF_STUT); i++)
            if (e[i].at == pos) {
                uint32_t k, note = (uint32_t)clamp((int32_t)e[i].note + oc - (int32_t)dn, 0, 127);
                for (k = 0; k < 4u && csr.mel_on[k]; k++)
                    ;
                if (k == 4u) {                       /* (none free: the oldest goes) */
                    k = 0;
                    trk_note_off(t, csr.mel_on[0]);
                }
                cs_on(t, note, e[i].vel);
                csr.mel_on[k] = (uint8_t)note;
                csr.mel_end[k] = (uint8_t)((pos + e[i].len) % (CM_BARS * 16u));
            }
    }
    n = n > 16u ? 16u : n;
    if (!cs_slice() || !kit || !n) {
        csr.sl = kit && n && csr.hit >= base && csr.hit < base + n && t->seq_n ? (uint8_t)(csr.hit - base) : 0xFFu;
        return;
    }
    sl = cs_sl16(n);
    spb = sl < 16u ? 16u / sl : 1u;                      /* the loop's slices a bar */
    T32 = su * sl * (half ? 2u : 1u);                    /* a trigger every slice (HALF: every two; RATE: its 16th) */
    ph = p32 % T32;
    csr.pos = (uint8_t)(p32 / su % (sl * n));
    if (!ph) {                                           /* a trigger */
        uint32_t pos = p32 / T32 % n, bb = cs_brk_bar(p16), slice, hi = p32 % 32u / T32;
        const step_t *s = !(p32 % su) ? &t->step[p32 / su % trk_len(t) % NSTEP] : 0;
        csr.rt_n = 0;
        csr.bb = (uint8_t)bb;
        if (!hi)
            csr.hist_n = 0;
        if (s && s->time == ST_NOTE && s->n) {           /* a recorded slice: its step plays it */
            csr.cur = (uint8_t)((s->note[0] - base) % n);
        } else if (!(fx & CF_STUT)) {
            if (cs_hand && csr.held >= base && csr.held < base + n)
                slice = csr.held - base;                 /* a key held: its slice */
            else
                slice = bb == 2u ? pos : cs_select(pos, n, spb, bb == 1u);   /* (the break's fill: straight) */
            csr.cur = (uint8_t)slice;
            cs_on(t, base + slice, 100);
            if (hi < 16u) {
                csr.hist[hi] = (uint8_t)(slice | (slice != pos ? 0x80u : 0u));
                csr.hist_n = (uint8_t)(hi + 1u);
            }
            if (bb == 1u && bp.sub_div <= 1u) {          /* the loop's own bar, the break not rolling: a roll, maybe */
                csr.rt_n = (uint8_t)cs_rolls(32u / T32 ? 32u / T32 : 1u);
                csr.rt_per = (uint8_t)(T32 / csr.rt_n ? T32 / csr.rt_n : 1u);
                csr.rt_note = (uint8_t)(base + slice);
                csr.rt_at = p32;
            }
        }
    } else if (csr.rt_n > 1u && !((p32 - csr.rt_at) % csr.rt_per) && (p32 - csr.rt_at) / csr.rt_per < csr.rt_n) {
        trk_note_off(t, csr.rt_note);                    /* a retrigger: the slice's head again */
        cs_on(t, csr.rt_note, 92);
    }
    csr.sl = csr.cur;
}

/* ---- TUNE (the main loop: eng_sample.c reads cs_tune16 at every block of a CHOP voice) ------------------ */
/* the loop's key to the session's, the nearest way: -5..+6 (a minor key as its relative major) */
static int32_t cs_shift(const cs_meta_t *m)
{
    int32_t lk = (int32_t)(m->key & 15u) + ((m->key & 0x10u) ? 3 : 0);
    int32_t sk = (int32_t)cg_root() + (cg_minor(cg_mask()) ? 3 : 0), d = ((sk - lk) % 12 + 24) % 12;
    return d > 6 ? d - 12 : d;
}
/* the chord's root over the key's, the nearest way (as chordgen.c cg_follow, whatever BASS FOLLOW says) */
static int32_t cs_chord(void)
{
    int node;
    int32_t r;
    if (!cg_on())
        return 0;
    node = cgr.live ? cgr.now : cg_node_now();
    if (node < 0)
        return 0;
    r = (int32_t)(cgr.prog[node % (int)CG_MAX] & 15u);
    return r > 6 ? r - 12 : r;
}
/* note x moved to the nearest of the session's scale (down first on a tie) */
static int32_t cs_snap(int32_t x)
{
    uint32_t mask = cg_mask(), root = cg_root();
    int32_t d;
    for (d = 0; d < 6; d++) {
        if ((mask >> (uint32_t)((x - d - (int32_t)root + 120) % 12)) & 1u)
            return x - d;
        if ((mask >> (uint32_t)((x + d - (int32_t)root + 120) % 12)) & 1u)
            return x + d;
    }
    return x;
}
static void cs_poll(void)
{
    const cs_meta_t *m = cs_meta();
    uint32_t mode = cs_tune(), i;
    int32_t sh, t16[16];
    if (cs_mode() != CM_INST && ENGINES[trk[CS_TRK].eng_req % NENGINES] != &ENG_SAMPLE)
        CSV(0) = CM_INST;                               /* (an instrument came some other way: the DECK's ENG) */
    cm_build();                                         /* (INST's theme, for the song as it is now) */
    if (trk[CS_TRK].p[P_SDIV] != CR_SDIV[cs_rate(cs_mode())])
        trk[CS_TRK].p[P_SDIV] = (int16_t)CR_SDIV[cs_rate(cs_mode())];   /* (the steps' grid: the mode's RATE) */
    if (!cs_slice() || mode == CT_OFF || !cs_keyed(m)) {
        cs_tune_on = 0;
        return;
    }
    sh = cs_shift(m) + (mode == CT_FOLLOW ? cs_chord() : 0);
    for (i = 0; i < 16u; i++) {
        t16[i] = sh * 16;
        if (mode == CT_SNAP && i < m->n && m->note[i] != CS_NOKEY) {
            int32_t x = (int32_t)m->note[i] + sh;
            t16[i] += (cs_snap(x) - x) * 16;
        }
    }
    for (i = 0; i < 16u; i++)
        cs_tune16[i] = (int16_t)t16[i];                 /* (one 16-bit store each: the ISR reads one whole) */
    cs_tune_on = 1;
}

