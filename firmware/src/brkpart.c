/* SPDX-License-Identifier: GPL-3.0-only */
/* The BREAK track (track 4, trk[TRK_BRK]). X0X's breakbeat player (fm1-x0x by Charles Vestal, GPL-3.0), which ported
 * BB Gen from schwung-breakbeat by mestela, used with the author's permission: X0X's breaks.c / breaks.h rewritten in
 * integers on SLOOP's sample sources (eng_slice.c: the ADPCM decoder, the slice tables of a user slot). FMX: it is
 * the track the GM drums were, so the sequencer plays it as it plays the others (seq.c seq_tick: the step length and
 * DIV, swing, the nudges, the fill conditions, the parameter locks) and the mix treats it as a part (fx.c mix_part:
 * DIST, SLICER, FILTER, LEVEL, PAN, the sends, mute / solo, the meter).
 *
 * What it is: four loops, A..D (each a zone of a pool entry named BR...: the standard set AMEN 2, 3, 4 or your own;
 * 1 / 2 / 4 / 8 bars), each cut into 8 equal slices, which the generator rearranges as it plays. A plays; B, C and D
 * come in by chance at the end of a phrase. The slices play at the song's tempo (varispeed: the loop's own `bars`
 * over the tempo, so the pitch follows it), times PTCH and the step's own pitch.
 *
 * What is BB Gen's (src/dsp/slice_select.c, perf.c, breakbeat.c), kept as X0X has it:
 *   - slice choice per trigger: COMPLEXITY 0 bypasses the generator and plays the trigger's beat position; otherwise a
 *     MOVE branch (probability 1 - ROLL: swap to a uniform random slice with probability COMPLEXITY x weight(beat
 *     position, ANCHOR), else the beat position's slice) or a STAY branch (5 % escape jump of 2..4 ahead, else repeat
 *     with probability 1 - weight(current slice, ANCHOR), else walk +-1). The weight curve {0, .5, 1, .7, 0, .5, 1,
 *     1.2}, blended from 1.0 by ANCHOR.
 *   - fill bar = the last bar of a phrase: COMPLEXITY -> c + (1 - c) x FILL, ANCHOR and ROLL x (1 - FILL). X0X: ROLL
 *     and FILL come in with COMPLEXITY (full from 25 %).
 *   - the cadence: one trigger every LENGTH / 8 bars, beat position = trigger count mod 8, the count restarting at
 *     transport start and at a loop switch, where slice 0 is forced.
 *   - retrigger: each of 2x / 3x / 4x / 8x rolls per trigger with p = 1 - (1 - P)^(1 / triggers a bar), one of the
 *     fired rates is picked at random; the head of the slice is replayed that many times.
 *   - phrases: the loop of the fill bar is rolled the bar before it (B, C, D: each its chance, one of those that hit
 *     at random, else A) and switched in at the fill bar; A is always scheduled back for the next phrase. (X0X: with
 *     PHRSE off, the chances roll every bar.)
 *   - the live layer: the held slices (last note priority, a re-press moves it up), reverse, half speed (only every
 *     other trigger fires, the rate halved), stutter (a 4x retrigger on every trigger).
 * What is X0X's, kept: a step with a slice of its own plays it (pin) in place of the generator's choice; the step's
 * ON gates the output (a ramp of 2 ms), the generator runs underneath, so a step turned on resumes exactly where the
 * break is.
 * What is FMX's: four loops; a step's level (ghost .. hard), retrigger, pitch and reverse; a hit from a key or
 * MIDI (a slice now, recorded into the step when REC is on); the parameters in the track's p[] (BPP), so a step can
 * lock them.
 * What is adapted: every jump (a trigger, a replay, reverse) crossfades two voices over 2 ms (BB Gen jumps and
 * clicks), and the loops are SLOOP's: the built-in bars if the build has any (SLOOP_BREAK_LOOPS), then every zone of
 * the pool's entries named "BR..." in id order (BR1, BR2, ..., as X0X numbers them). A loop's slices are the eighths of it,
 * on the 128 grid points a slice table holds (a start, a seek and the reverse windows all begin from a stored decoder
 * state): the tables of the four loops are built in the main loop (brkp_poll) when they are chosen or a slot is
 * written. Its length in bars is guessed from its duration (the count of 1, 2, 4, 8 that puts its tempo nearest 120
 * inside 80-180 BPM) unless A..D BARS say.
 *
 * Threading: brk_seq_step / brk_block / brkp_render / brk_input run in the audio ISR (events_block, mix_block); the
 * UI (main loop) writes the track's p[] and steps, and bp.keys (a word); the loops are the banks (bpb: src is 0 while
 * one is built or its slot is written), read at every block: a slot that is being written ends the voices that play
 * it. */
#define BP_NSL BRK_SLICES                /* slices: eighths of the loop */
#define BP_GPS (SLC_GRID / BP_NSL)       /* grid points per slice */
#define BP_FADE 88                       /* crossfade and gate ramp: 2 ms at 44.1 kHz, samples */
#define BP_RB 128u                       /* reverse: samples decoded at a time */
#define BP_NBANK 4u                      /* the loops A..D */
#define BP_TRIM 20                       /* the loop's level trim (P_ED_FX), 1/2 dB steps (a loop's drums are quiet on average) */
#if BRK_NBUILTIN > 0                     /* (built in with SLOOP_BREAK_LOOPS=N: tools/gen_samples.py) */
typedef struct { uint32_t off, n, rate; } bp_builtin_t;   /* a built-in loop: its data (SMP_DATA index), its samples, its rate (Q16) */
static const bp_builtin_t BP_BUILTIN[BRK_NBUILTIN] = BRK_BUILTIN_INIT;
static const char *const BP_BUILTIN_NAME[BRK_NBUILTIN] = {BRK_BUILTIN_NAMES_INIT};
#endif
#define BP_NB ((uint32_t)BRK_NBUILTIN)
#define BP_MAXLOOP (BP_NB + 127u)        /* LOOP A..D: 0..BP_NB - 1 the built-in loops, then BR1 .. (the pool's BR entries) */
#define BP_WPEAKS 240u                   /* waveform columns: 8 slices of 30 */

enum {                                   /* the generator's parameters: the track's p[BPP[i]] */
    BP_CMPLX, BP_ANCHOR, BP_ROLL, BP_FILL, BP_R2, BP_R3, BP_R4, BP_R8, BP_PHRASE, BP_BCH, BP_CCH, BP_DCH, BP_LEN,
    BP_PITCH, BP_LOOPA, BP_LOOPB, BP_LOOPC, BP_LOOPD, BP_BARSA, BP_BARSB, BP_BARSC, BP_BARSD, BP_MANGLE, BP_STYLE, BP_INTP,
    BP_LAYER, BP_LMIX, BP_LPTCH, BP_NP
};
/* where each one rides in the track's p[]: parameters a synth part has and the BREAK track has no use for (its
 * engine's P_E0..P_E7, the envelope, the LFO and its depths, the voice's priority, allocation, glide and detune); all
 * lockable (seq.c p_lockable) but STYLE (P_PRIO), INTP (P_ALLOC) and LAYER (P_GLMODE) */
static const uint8_t BPP[BP_NP] = {
    P_E0, P_E1, P_E2, P_E3, P_E4, P_E5, P_E6, P_E7, P_ATK, P_DEC, P_SUS, P_REL, P_ED_SHP,
    P_ED_PIT, P_LRATE, P_LWAVE, P_LPHASE, P_LFADE, P_LD_PIT, P_LD_FLT, P_LD_SHP, P_LD_AMP, P_ED_FLT, P_PRIO, P_ALLOC,
    P_GLMODE, P_DETUNE, P_GLIDE};
#define BPV(i) (trk[TRK_BRK].p[BPP[i]])
static const char *const N_BP_PHRASE[] = {"OFF", "2", "4", "8", "16"};
static const char *const N_BP_INTP[] = {"LIN", "PSX"};
static const char *const N_BP_LAYER[] = {"OFF", "B", "C", "D"};
static const char *const N_BP_LEN[] = {"AUTO", "1/4", "1/2", "1", "2", "4", "8"};   /* AUTO: the loop's bars */
static const char *const N_BP_BARS[] = {"AUTO", "1", "2", "4", "8"};
enum { BS_STRAIGHT, BS_ROLLER, BS_JUNGLE, BS_HALFTIME, BS_BRKCORE, BS_CHAOS, BS_NSTYLE };
static const char *const N_BP_STYLE[BS_NSTYLE] = {"STRAIGHT", "ROLLER", "JUNGLE", "HALFTIME", "BRKCORE", "CHAOS"};
static const param_desc_t BPD[BP_NP] = {
    [BP_CMPLX] = {"CMPLX", F_INT, 0, 100, 50, 0, "%"},
    [BP_ANCHOR] = {"ANCHR", F_INT, 0, 100, 0, 0, "%"},
    [BP_ROLL] = {"ROLL", F_INT, 0, 100, 0, 0, "%"},
    [BP_FILL] = {"FILL", F_INT, 0, 100, 0, 0, "%"},
    [BP_R2] = {"2X", F_INT, 0, 100, 0, 0, "%"},
    [BP_R3] = {"3X", F_INT, 0, 100, 0, 0, "%"},
    [BP_R4] = {"4X", F_INT, 0, 100, 0, 0, "%"},
    [BP_R8] = {"8X", F_INT, 0, 100, 0, 0, "%"},
    [BP_PHRASE] = {"PHRSE", F_ENUM, 0, 4, 2, N_BP_PHRASE, 0},
    [BP_BCH] = {"B CHN", F_INT, 0, 100, 0, 0, "%"},
    [BP_CCH] = {"C CHN", F_INT, 0, 100, 0, 0, "%"},
    [BP_DCH] = {"D CHN", F_INT, 0, 100, 0, 0, "%"},
    [BP_LEN] = {"LEN", F_ENUM, 0, 6, 0, N_BP_LEN, 0},
    [BP_PITCH] = {"PTCH", F_SEMI, -12, 12, 0, 0, 0},
    [BP_LOOPA] = {"LOOP A", F_INT, 0, BP_MAXLOOP, 0, 0, 0},       /* (shown by brkp_loop_name: AMEN 2, BR1 ..) */
    [BP_LOOPB] = {"LOOP B", F_INT, 0, BP_MAXLOOP, 1, 0, 0},
    [BP_LOOPC] = {"LOOP C", F_INT, 0, BP_MAXLOOP, 2, 0, 0},
    [BP_LOOPD] = {"LOOP D", F_INT, 0, BP_MAXLOOP, 3, 0, 0},
    [BP_BARSA] = {"A BARS", F_ENUM, 0, 4, 0, N_BP_BARS, 0},
    [BP_BARSB] = {"B BARS", F_ENUM, 0, 4, 0, N_BP_BARS, 0},
    [BP_BARSC] = {"C BARS", F_ENUM, 0, 4, 0, N_BP_BARS, 0},
    [BP_BARSD] = {"D BARS", F_ENUM, 0, 4, 0, N_BP_BARS, 0},
    [BP_MANGLE] = {"MANGL", F_INT, 0, 100, 0, 0, "%"},
    [BP_STYLE] = {"STYLE", F_ENUM, 0, BS_NSTYLE - 1, BS_JUNGLE, N_BP_STYLE, 0},
    [BP_INTP] = {"INTP", F_ENUM, 0, 1, 1, N_BP_INTP, 0},   /* LIN, or PSX (the SPU's Gaussian: softer, a PlayStation's) */
    [BP_LAYER] = {"LAYER", F_ENUM, 0, 3, 0, N_BP_LAYER, 0},   /* a second loop (B, C or D) under every slice, OFF */
    [BP_LMIX] = {"L MIX", F_INT, 0, 100, 70, 0, "%"},
    [BP_LPTCH] = {"L PTCH", F_SEMI, -12, 12, 0, 0, 0},
};
/* the styles: a starting point for the generator, set when STYLE is turned (brk_style; the parameters stay yours to
 * change after). CMPLX ANCHR ROLL FILL 2X 3X 4X 8X PHRSE MANGL, and the step's DIV (N_SDIV: 1 = 1/8, 2 = 1/16):
 *   STRAIGHT  the loop as it is          ROLLER    a steady DnB roll, the kick and snare kept, a fill every 8 bars
 *   JUNGLE    chopped, rolls and fills   HALFTIME  the loop at half speed (an octave down: the snare on 3)
 *   BRKCORE   cut up, stuttered, mangled CHAOS     everything at once */
static const uint8_t BP_STYLES[BS_NSTYLE][11] = {
    {0, 0, 0, 0, 0, 0, 0, 0, 2, 0, 2},
    {25, 70, 10, 40, 10, 0, 5, 0, 3, 0, 2},
    {55, 40, 25, 60, 20, 10, 15, 5, 2, 10, 2},
    {30, 80, 10, 40, 10, 0, 10, 0, 2, 5, 1},
    {85, 10, 40, 80, 30, 20, 30, 25, 1, 55, 2},
    {100, 0, 60, 100, 50, 40, 50, 50, 1, 100, 2},
};
static const uint8_t BP_LEN_TICKS[6] = {3, 6, 12, 24, 48, 96};   /* BB Gen: 12 x the length in bars, 24 ppq (1/4 bar .. 8 bars) */
static const uint8_t BP_PHRASE_BARS[5] = {0, 2, 4, 8, 16};
static const char BP_BANK_NAME[BP_NBANK] = {'A', 'B', 'C', 'D'};

/* the descriptor of p[id] on the BREAK track: the generator's, else TP's (0: not one of the generator's) */
static const param_desc_t *brk_desc(uint32_t id)
{
    uint32_t k;
    for (k = 0; k < BP_NP; k++)
        if (BPP[k] == id)
            return &BPD[k];
    return 0;
}

/* the parameters a step of the BREAK track may lock: the generator's (not LOOP A..D nor their BARS: a loop is built in
 * the main loop, brkp_poll, not between two steps), and the mix's it has (seq.c p_lockable) */
static int brk_lockable(uint32_t id)
{
    if (id == P_PRIO || id == P_GLMODE || id == P_ALLOC)
        return 0;                                        /* (STYLE sets other parameters; LAYER and INTP: the whole track's) */
    if (id >= P_LRATE && id <= P_LFADE)
        return 0;
    if (id >= P_LD_PIT && id <= P_LD_AMP)
        return 0;
    return brk_desc(id) || id == P_LEVEL || (id >= P_DIST && id <= P_REV) || id == P_PAN ||
           (id >= P_SLCR && id <= P_SLDEPTH) || id == P_TFLT;
}

typedef struct {                         /* a voice: one slice playing, fading in or out */
    uint8_t on, rev, bank, rvalid;
    uint8_t srev;                        /* the step asked for reverse (the live REV key flips it) */
    int8_t semi;                         /* the step's own pitch */
    int16_t bend, dbend;                 /* MANGLE's dive: 1/16 semitones on top, and its change a block */
    slc_dec_t d;                         /* forward: the decoder, d.pos = the next sample to decode (s1 is the one before) */
    uint32_t lo, hi, frac;               /* the slice [lo, hi), the fraction between s0 and s1 (Q16) */
    uint32_t rpos, rbase;                /* reverse: the position of s1, rb holds rbase .. rbase + BP_RB - 1 */
    int32_t s0, s1, g, dg;               /* output = s0 + (s1 - s0) x frac; gain Q15 and its slope a sample */
    int16_t h1, h2;                      /* INTP PSX: the two samples before s0 (the Gaussian's four: h2 h1 s0 s1) */
    int32_t amp;                         /* the hit's level (the step's ghost .. hard), Q15 */
    int16_t rb[BP_RB];
} bpv_t;

typedef struct {
    volatile uint16_t keys;              /* live: bit 0..7 hold slice, 8 reverse, 9 half speed, 10 stutter (the UI) */
    uint16_t keys_seen;
    bpv_t v[4];                          /* the sounding voice and the one fading out; 2, 3: the LAYER's (lcur) */
    uint8_t cur, lcur;
    uint32_t rng;                        /* xorshift32 */
    uint32_t late;                       /* the samples the step being played began before this block (a step is taken at a block's start) */
    /* transport and generator (BB Gen's breakbeat_t, tick mode) */
    uint8_t running, engine_bank, render_bank, step_on;
    int8_t pending_bank;                 /* the loop to switch to at the next bar, -1 = none */
    volatile uint8_t hold;               /* LIVE: the loop held on black keys 1..4 (brk_hold_bank) + 1, 0 = none: it plays
                                          * in place of the generator's, now, whatever Q */
    uint8_t hold_keys;                   /* (the loops' keys held, bit per bank) */
    int8_t current_slice;
    uint32_t bar, trigger_count;
    uint32_t sp16, spt;                  /* output samples per step, per trigger */
    int32_t trig_left;                   /* samples to a pending mid-step trigger (the 1/4 bar length) */
    uint8_t sub_div, sub_count;          /* retrigger */
    int32_t sub_elapsed, sub_len;        /* samples since the trigger, per sub-hit */
    int8_t pin_next;                     /* the step's own slice, -1 = the generator's */
    volatile uint8_t drop_kick;          /* AUTO SONG (arranger_scene.c): the next trigger is a kick of the loop's (a drop's
                                          * first hit), the generator's after it */
    uint8_t rat_next;                    /* the step's own retrigger (0: the generator's) */
    uint8_t lvl_next, srev_next;         /* the step's level and reverse */
    int8_t semi_next;                    /* the step's pitch */
    uint8_t hush;                        /* a key hit is sounding in this step already: it plays, no voice starts */
    uint8_t st_mangle;                   /* the last trigger's MANGLE: 0 none, 1 stutter, 2 reverse, 3 jump, 4 dive, 5 gap */
    uint8_t shot;                        /* a key hit while stopped / between steps: its slice is heard (the gate) */
    /* perf: the held slices, reverse, half speed, stutter */
    int8_t stack[8];
    uint8_t count, reverse, half, stutter;
    uint8_t roll;                        /* the moves (punch.c): ROLL 16 (each trigger's hits) */
    uint8_t half_acc;                    /* half speed: the trigger accumulator (every other one fires) */
    /* gate: the step's ON, ramped (Q15) */
    int32_t gate, gate_to;
    volatile uint8_t kick;               /* slice 0 of a loop began (fx.c DUCK) */
    volatile uint8_t hits;               /* bit per slice hit since the UI last looked (key LEDs) */
    /* the last trigger (the UI's readout, the tests' probe) */
    uint32_t st_trigs, st_starts, st_hits;   /* triggers, voices started, key / MIDI hits */
    uint32_t kicks_n;                    /* the slices started that open on a kick (the BASS phrase follows them) */
    int8_t st_slice, st_bank, st_div, st_bp;
    uint8_t st_forced;
} brkp_t;
static brkp_t bp = {.lcur = 2, .pending_bank = -1, .pin_next = -1, .st_slice = -1, .rng = 0x2545F491u};

/* ---- the track's defaults (track_defaults, NEW) ---------------------------------------------------------------- */
static void brk_style_p(int16_t *p, uint32_t st)      /* (a track's parameters: q.c stages a STYLE so) */
{
    const uint8_t *v = BP_STYLES[st % BS_NSTYLE];
    uint32_t i;
    for (i = 0; i < 8u; i++)
        p[BPP[BP_CMPLX + i]] = v[i];
    p[BPP[BP_PHRASE]] = v[8];
    p[BPP[BP_MANGLE]] = v[9];
    p[P_SDIV] = v[10];
    p[BPP[BP_STYLE]] = (int16_t)(st % BS_NSTYLE);
}
static void brk_style(track_t *t, uint32_t st) { brk_style_p(t->p, st); }
static void brk_defaults(track_t *t)
{
    uint32_t i;
    for (i = 0; i < BP_NP; i++)
        t->p[BPP[i]] = BPD[i].def;
    brk_style(t, BS_JUNGLE);
    t->p[P_ED_FX] = BP_TRIM;
    t->p[P_REV] = 16;
    for (i = 0; i < NSTEP; i++) {                         /* every step on: PLAY and the break plays */
        memset(&t->bstep[i], 0, sizeof t->bstep[i]);
        t->bstep[i].on = 1;
    }
}

/* ---- the loops: the built-in ones, then every zone of the pool's BR entries in id order (X0X's BR1, BR2, ...) ---- */
typedef struct {                         /* the loop of a bank, ready to play */
    slc_src_t t;                         /* its slice table (a user loop: one segment, the zone's data) */
    const slc_src_t *volatile src;       /* what the audio reads: &t; 0 = none (being built, or no such loop) */
    uint32_t key;                        /* what it was built for: loop | smp_user_gen << 8 (0xFFFFFFFF: nothing yet) */
    uint8_t auto_bars;
    uint8_t kicks;                       /* the slices that open on a kick (bit per slice): they duck the parts (DUCK) */
    uint8_t snares;                      /* ... on a snare (not a kick: the mid band loud), for the DRUMMER (ui_vis.c) */
    uint8_t rsv;
    uint8_t peaks[BP_WPEAKS];            /* the waveform for the screen: the peak of each 1/240 of the loop, 0..255 */
} bpbank_t;
static bpbank_t bpb[BP_NBANK] = {{.key = 0xFFFFFFFFu}, {.key = 0xFFFFFFFFu}, {.key = 0xFFFFFFFFu}, {.key = 0xFFFFFFFFu}};

/* an entry of loops: valid, and its name (8 bytes at 8 of its header) starts with BR */
static int bp_slot_loops(uint32_t k)
{
    const smp_user_hdr_t *h = (const smp_user_hdr_t *)smp_user_xip(k);
    return usr_nz[k] && h->name[0] == 'B' && h->name[1] == 'R';
}
/* the zone an entry's zone z is as a loop: not empty, not the data of an earlier zone (key ranges of one sound) */
static int bp_zone_ok(uint32_t k, uint32_t z)
{
    uint32_t j;
    if (USR_Z(k, z)->n < 64u || !bp_slot_loops(k))
        return 0;
    for (j = 0; j < z; j++)
        if (USR_Z(k, j)->off == USR_Z(k, z)->off)
            return 0;
    return 1;
}
static uint32_t brkp_nloops(void)        /* the loops there are: the built-in ones and the zones of the pool's entries */
{
    uint32_t k, z, n = BP_NB;
    for (k = 0; k < SMP_POOL_N; k++)
        for (z = 0; z < usr_nz[k]; z++)
            n += (uint32_t)bp_zone_ok(k, z);
    return n > BP_MAXLOOP + 1u ? BP_MAXLOOP + 1u : n;
}
/* loop idx (BP_NB..) -> its entry and zone; 0 = there is none */
static int bp_loop_zone(uint32_t idx, uint32_t *slot, uint32_t *zone)
{
    uint32_t k, z, n = 0;
    if (idx < BP_NB)
        return 0;
    idx = idx - BP_NB + 1u;                                     /* (1..: the first loop of the pool is 1) */
    for (k = 0; k < SMP_POOL_N; k++)
        for (z = 0; z < usr_nz[k]; z++)
            if (bp_zone_ok(k, z) && ++n == idx) {
                *slot = k;
                *zone = z;
                return 1;
            }
    return 0;
}
static void brkp_loop_name(uint32_t idx, char *b)   /* "AMEN 2" (built in), "BR12" */
{
#if BRK_NBUILTIN > 0
    if (idx < BP_NB) {
        str_cpy(b, BP_BUILTIN_NAME[idx], 8);
        return;
    }
#endif
    str_cpy(b, "BR", 3);
    fmt_int(b + 2, (int32_t)(idx - BP_NB + 1u));
}
static uint32_t brkp_bank_loop(uint32_t bank) { return (uint32_t)BPV(BP_LOOPA + (bank & 3u)); }
static const slc_src_t *brkp_bank_src(uint32_t bank) { return bpb[bank & 3u].src; }
/* the loop that plays: one held (LIVE), else the generator's; one with nothing in it: A */
static uint32_t bp_bank(void)
{
    if (bp.hold && brkp_bank_src(bp.hold - 1u))
        return (bp.hold - 1u) & 3u;
    return brkp_bank_src(bp.engine_bank) ? bp.engine_bank : 0u;
}
/* the bars of the loop of a bank: A..D BARS, or its guess */
static uint32_t brkp_bank_bars(uint32_t bank)
{
    int32_t o = BPV(BP_BARSA + (bank & 3u));
    return o > 0 ? 1u << (o - 1) : bpb[bank & 3u].auto_bars;
}
/* the count of bars (1, 2, 4, 8) that puts a loop of n samples at rate (Q16 of 44.1 kHz) nearest 170 BPM within 80..180
 * (after X0X's page, import_breaks.bars_of, which took the one nearest 120: FMX is a jungle firmware, a loop of 2.8 s
 * is 2 bars at 170, not 1 at 85; a raw break, 1 bar at 90..140, has no other reading inside the range); 1 when none
 * does */
static uint32_t bp_guess_bars(uint32_t n, uint32_t rate_q16)
{
    uint32_t hz = (rate_q16 * 44100u) >> 16, ms = hz ? n * 1000u / hz : 0u, b, best = 1, bd = 0xFFFFFFFFu;
    if (!ms)
        return 1;
    for (b = 1; b <= 8u; b <<= 1) {
        uint32_t bpm = b * 240000000u / ms, d;                   /* x 1000 */
        if (bpm < 80000u || bpm > 180000u)
            continue;
        d = bpm > 170000u ? bpm - 170000u : 170000u - bpm;
        if (d < bd) {
            bd = d;
            best = b;
        }
    }
    return best;
}

/* build the loop `loop` into bank b (main loop): its grid of decoder states, its waveform, its bars. The audio does not
 * read it meanwhile (src 0): its voices end */
static void bp_bank_build(uint32_t b, uint32_t loop)
{
    bpbank_t *k = &bpb[b];
    slc_src_t *t = &k->t;
    uint32_t slot, zone;
    k->src = 0;
    RING_PUBLISH();
    memset(k->peaks, 0, sizeof k->peaks);
    k->auto_bars = 1;
    k->kicks = 0;
    memset(t, 0, sizeof *t);
    t->nseg = 1;
    t->seg[0].at = 0;
    t->nauto = 1;
#if BRK_NBUILTIN > 0
    if (loop < BP_NB) {                                         /* a built-in loop: a bar or two, at its own rate */
        if (BP_BUILTIN[loop].n < 64u)
            return;
        t->len = BP_BUILTIN[loop].n;
        t->rate = BP_BUILTIN[loop].rate;
        t->seg[0].off = BP_BUILTIN[loop].off;
        t->seg[0].n = BP_BUILTIN[loop].n;
        k->auto_bars = (uint8_t)bp_guess_bars(t->len, t->rate);
    } else
#endif
    if (bp_loop_zone(loop, &slot, &zone)) {
        const smp_zone_t *z = USR_Z(slot, zone);
        t->len = z->n;
        t->rate = z->rate;
        t->seg[0].off = z->off;
        t->seg[0].n = z->n;
        k->auto_bars = (uint8_t)bp_guess_bars(z->n, z->rate);
    } else {
        return;                                                 /* no such loop */
    }
    {   /* one pass: the decoder state at each grid point, the peak of each column, and each slice's kick: the low end
         * (a one-pole low-pass, ~110 Hz at 22 kHz) over its first ~25 ms, against the loudest slice's; and its snare:
         * the band above it to ~1.5 kHz (a second low-pass) over the same, against the loudest of the slices that
         * are not kicks, 60 % (the ride and the hats are above it, quieter there) */
        slc_dec_t d;
        uint32_t col, g = 0, pos = 0, mx = 1, on = t->rate ? (uint32_t)(((uint64_t)FS * t->rate >> 16) / 40u) : 550u;
        uint32_t lowe[BP_NSL] = {0}, mide[BP_NSL] = {0}, lmax = 1, mmax = 1, sl;
        int32_t lp = 0, lp2 = 0;
        uint16_t pk[BP_WPEAKS];
        slc_dec_at(&d, 0, 0);
        for (col = 0; col < BP_WPEAKS; col++) {
            uint32_t end = (t->len * (col + 1u)) / BP_WPEAKS, p = 0;
            while (pos < end) {
                int32_t x;
                while (g < SLC_GRID && slc_gpos(t, g) == pos)
                    t->grid[g++] = slc_dec_st(&d);
                x = slc_dec_next(t, &d);
                lp += (x - lp) >> 5;
                lp2 += (x - lp2) >> 2;
                sl = pos * BP_NSL / t->len;
                if (pos - sl * t->len / BP_NSL < on) {
                    lowe[sl] += (uint32_t)(lp < 0 ? -lp : lp) >> 4;
                    mide[sl] += (uint32_t)(lp2 - lp < 0 ? lp - lp2 : lp2 - lp) >> 4;
                }
                pos++;
                p = (uint32_t)(x < 0 ? -x : x) > p ? (uint32_t)(x < 0 ? -x : x) : p;
            }
            pk[col] = (uint16_t)(p > 65535u ? 65535u : p);
            mx = p > mx ? p : mx;
        }
        while (g < SLC_GRID)                                    /* (grid points at the very end: none, len / 128 apart) */
            t->grid[g++] = slc_dec_st(&d);
        for (col = 0; col < BP_WPEAKS; col++)
            k->peaks[col] = (uint8_t)((uint32_t)pk[col] * 255u / mx);
        for (sl = 0; sl < BP_NSL; sl++)
            lmax = lowe[sl] > lmax ? lowe[sl] : lmax;
        k->kicks = 0;
        for (sl = 0; sl < BP_NSL; sl++)
            if (lowe[sl] * 100u >= lmax * 60u)
                k->kicks |= (uint8_t)(1u << sl);
        for (sl = 0; sl < BP_NSL; sl++)
            if (!((k->kicks >> sl) & 1u) && mide[sl] > mmax)
                mmax = mide[sl];
        k->snares = 0;
        for (sl = 0; sl < BP_NSL; sl++)
            if (!((k->kicks >> sl) & 1u) && mide[sl] * 100u >= mmax * 60u)
                k->snares |= (uint8_t)(1u << sl);
    }
    RING_PUBLISH();                                             /* the table is whole before the audio may read it */
    k->src = t;
}
/* the main loop, every frame: the loops of A..D follow the parameters and the slots */
static void brkp_poll(void)
{
    uint32_t b;
    for (b = 0; b < BP_NBANK; b++) {
        uint32_t loop = brkp_bank_loop(b), key = loop | smp_user_gen << 8;
        if (bpb[b].key != key) {
            bpb[b].key = key;
            bp_bank_build(b, loop);
        }
    }
}

/* ---- randomness and BB Gen's slice selection (slice_select.c) ------------------------------- */
static uint32_t bp_rnd(void)             /* 16 bits */
{
    uint32_t x = bp.rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    bp.rng = x;
    return x >> 16;
}
#define BP_P16(pct) ((uint32_t)(pct) * 65536u / 100u)
static int bp_chance(uint32_t p16) { return bp_rnd() < p16; }
static uint32_t bp_pct(uint32_t i) { return (uint32_t)clamp(BPV(i), 0, 100); }

/* the anchor weight: 1.0 blended to {0, .5, 1, .7, 0, .5, 1, 1.2} by the anchor (0..100), Q10 */
static uint32_t bp_weight(uint32_t slice, uint32_t anchor)
{
    static const uint16_t LOCKED[BP_NSL] = {0, 512, 1024, 717, 0, 512, 1024, 1229};
    return ((100u - anchor) * 1024u + LOCKED[slice & 7u] * anchor) / 100u;
}
/* the next slice at beat position `beat` of bar `bar` */
static uint32_t bp_select(uint32_t beat, uint32_t bar)
{
    uint32_t c = bp_pct(BP_CMPLX), a = bp_pct(BP_ANCHOR), r = bp_pct(BP_ROLL), f = bp_pct(BP_FILL);
    uint32_t ramp = c * 4u < 100u ? c * 4u : 100u, pb = BP_PHRASE_BARS[(uint32_t)BPV(BP_PHRASE) % 5u];
    uint32_t cur = (uint32_t)bp.current_slice & 7u;
    if (!c)
        return beat;                                     /* BB Gen: complexity 0 bypasses the generator */
    r = r * ramp / 100u;                                 /* X0X: ROLL and FILL come in with COMPLEXITY */
    f = f * ramp / 100u;
    if (pb && bar % pb == pb - 1u) {                     /* the fill bar */
        c += (100u - c) * f / 100u;
        a = a * (100u - f) / 100u;
        r = r * (100u - f) / 100u;
    }
    if (bp_chance(BP_P16(100u - r))) {                   /* MOVE */
        if (bp_chance((c * bp_weight(beat, a) * 5243u) >> 13))   /* c / 100 x w / 1024 x 65536 */
            return (bp_rnd() * BP_NSL) >> 16;
        return beat;                                     /* no swap: the beat position's own slice */
    }
    if (bp_chance(BP_P16(5u)))                           /* STAY: the escape, 2..4 ahead */
        return (cur + 2u + ((bp_rnd() * 3u) >> 16)) & 7u;
    {
        uint32_t w = bp_weight(cur, a);
        if (bp_chance(w < 1024u ? (1024u - w) * 64u : 0u))   /* repeat, p = 1 - w */
            return cur;
    }
    return (cur + (bp_chance(32768u) ? 1u : 7u)) & 7u;   /* walk +-1 */
}

/* the square root of a Q16 number below 1, in Q16 */
static uint32_t bp_sqrt16(uint32_t x)
{
    uint32_t v = x << 16, r = 0, b, t;
    for (b = 1u << 15; b; b >>= 1) {                     /* bit by bit: t x t <= 2^32 - 1 */
        t = r | b;
        if (t * t <= v)
            r = t;
    }
    return r;
}
/* the chance a trigger rolls a rate, from its chance a bar: 1 - (1 - P)^(1 / triggers a bar), Q16 (BB Gen's) */
static uint32_t bp_rate_p(uint32_t pct, uint32_t tpt)
{
    uint32_t q, n;
    if (!pct)
        return 0;
    if (pct >= 100u)
        return 65536u;
    q = 65536u - BP_P16(pct);                            /* 1 - P */
    for (n = tpt; n < 96u; n <<= 1)                      /* triggers a bar = 96 / tpt: a power of two */
        q = bp_sqrt16(q);
    return 65536u - q;
}
/* the loop of the fill bar: B, C and D roll their chances, one of those that hit (and have a loop) at random; 0 = A */
static uint32_t bp_alt_bank(void)
{
    uint32_t b, n = 0, hit[3];
    for (b = 1; b < BP_NBANK; b++)
        if (bp_chance(BP_P16(bp_pct(BP_BCH + b - 1u))) && brkp_bank_src(b))
            hit[n++] = b;
    return n ? hit[(bp_rnd() * n) >> 16] : 0u;
}

/* ---- the perf layer (perf.c) ---------------------------------------------------------------- */
static void bp_perf_release(int slice)
{
    int i, found = -1;
    for (i = (int)bp.count - 1; i >= 0; i--)
        if (bp.stack[i] == slice) {
            found = i;
            break;
        }
    if (found < 0)
        return;
    for (i = found; i < (int)bp.count - 1; i++)
        bp.stack[i] = bp.stack[i + 1];
    bp.count--;
}
static void bp_perf_push(int slice)
{
    if (slice < 0 || slice > 7)
        return;
    bp_perf_release(slice);                              /* a re-press moves it to the top */
    if (bp.count >= 8u)
        return;
    bp.stack[bp.count++] = (int8_t)slice;
}
static int bp_perf_top(void) { return bp.count ? bp.stack[bp.count - 1u] : -1; }
/* 1 = this clock trigger fires; half speed fires every other one */
static int bp_perf_fires(void)
{
    if (!bp.half) {
        bp.half_acc = 0;
        return 1;
    }
    bp.half_acc ^= 1u;
    return bp.half_acc == 0u;
}

/* ---- the voices ----------------------------------------------------------------------------- */
static uint32_t bp_slice_lo(const slc_src_t *s, uint32_t sl) { return slc_gpos(s, sl * BP_GPS); }
static uint32_t bp_slice_hi(const slc_src_t *s, uint32_t sl) { return slc_gpos(s, (sl + 1u) * BP_GPS); }
static uint32_t bp_slice_st(const slc_src_t *s, uint32_t sl) { return s->grid[sl * BP_GPS]; }
/* the slice a position is in */
static uint32_t bp_slice_of(const slc_src_t *s, uint32_t pos)
{
    uint32_t sl = 0;
    while (sl < 7u && bp_slice_lo(s, sl + 1u) <= pos)
        sl++;
    return sl;
}

/* the decoder at source sample pos (< len), from the checkpoint at or below it */
static void bp_seek(const slc_src_t *s, slc_dec_t *d, uint32_t pos)
{
    uint32_t k = (pos << SLC_GRID_LOG2) / s->len;
    while (k + 1u < SLC_GRID && slc_gpos(s, k + 1u) <= pos)
        k++;
    slc_dec_at(d, slc_gpos(s, k), s->grid[k]);
    while (d->pos < pos)
        slc_dec_next(s, d);
}
/* the next sample forward; past the end the loop starts again (decoder state 0 / 0) */
static inline int32_t bp_next(const slc_src_t *s, slc_dec_t *d)
{
    if (d->pos >= s->len)
        slc_dec_at(d, 0, 0);
    return slc_dec_next(s, d);
}
static int32_t bp_rev_fetch(bpv_t *v, const slc_src_t *s, uint32_t q)
{
    if (!v->rvalid || q < v->rbase || q >= v->rbase + BP_RB) {
        uint32_t base = q & ~(BP_RB - 1u), m = s->len - base;
        if (m > BP_RB)
            m = BP_RB;
        slc_fill(s, v->rb, base, m, 0, 0);
        v->rbase = base;
        v->rvalid = 1;
    }
    return v->rb[q - v->rbase];
}
static inline uint32_t bp_rev_next(const bpv_t *v, uint32_t q) { return q > v->lo ? q - 1u : v->hi - 1u; }

/* the loop's speed in source samples per output sample, Q16: its length over its bars at the tempo, then PITCH, the
 * step's pitch and half speed. X0X: k->sp16 / b->sp16 x 2^(semi / 12) x rate_mult */
static uint32_t bp_inc(const slc_src_t *s, uint32_t bank, int32_t semi16)   /* semi16: the hit's, 1/16 semitones */
{
    uint32_t bars = brkp_bank_bars(bank), sp = bp.sp16 > 1u ? bp.sp16 : FS / 8u;   /* 120 BPM until told */
    uint32_t r = (s->len * 4096u / bars) / sp;           /* len / (bars x 16) / sp16, Q16 */
    r = ((r >> 4) * (pow2_q16(clamp(BPV(BP_PITCH) * 16 + semi16, -576, 384)) >> 4)) >> 8;
    return bp.half ? r >> 1 : r;
}
static int32_t bp_vsemi(const bpv_t *v) { return v->semi * 16 + v->bend; }

/* voice v at sample p of loop s, forward or reversed; st: the decoder state before sample p (have_state), else sought */
static void bp_vinit(bpv_t *v, const slc_src_t *s, uint32_t p, int rev, int have_state, uint32_t st)
{
    if (p >= s->len)
        p = 0;
    v->rev = (uint8_t)rev;
    if (rev) {
        v->rvalid = 0;
        v->s0 = bp_rev_fetch(v, s, p);
        v->rpos = bp_rev_next(v, p);
        v->s1 = bp_rev_fetch(v, s, v->rpos);
    } else {
        if (have_state)
            slc_dec_at(&v->d, p, st);
        else
            bp_seek(s, &v->d, p);
        v->s0 = bp_next(s, &v->d);
        v->s1 = bp_next(s, &v->d);
    }
    v->h1 = v->h2 = (int16_t)clamp(v->s0, -32768, 32767);   /* (INTP PSX: no history before the slice's first sample) */
}
/* the LAYER (a second loop, B .. D, under every slice): the same slice of its loop as main voice m, at the same place in
 * it (from its start: the stored state), L MIX of its level, L PTCH on its pitch; the one before fades. LAYER OFF, its
 * loop missing, or the loop the main voice plays: none */
static void bp_layer_start(const bpv_t *m, uint32_t sl, uint32_t p, int from_start)
{
    uint32_t lb = (uint32_t)BPV(BP_LAYER) & 3u, lo, hi;
    const slc_src_t *s = lb ? brkp_bank_src(lb) : 0;
    bpv_t *o, *v;
    if (bp.lcur < 2u)
        bp.lcur = 2;                                     /* (a state cleared to 0) */
    o = &bp.v[bp.lcur];
    if (o->on)
        o->dg = -(32767 / BP_FADE + 1);
    if (!s || lb == m->bank)
        return;
    bp.lcur = (uint8_t)(bp.lcur == 2u ? 3u : 2u);
    v = &bp.v[bp.lcur];
    lo = bp_slice_lo(s, sl);
    hi = bp_slice_hi(s, sl);
    v->on = 1;
    v->bank = (uint8_t)lb;
    v->amp = (int32_t)((uint32_t)m->amp * (uint32_t)clamp(BPV(BP_LMIX), 0, 100) / 100u);
    v->srev = m->srev;
    v->semi = (int8_t)clamp(m->semi + BPV(BP_LPTCH), -36, 36);
    v->bend = m->bend;
    v->dbend = m->dbend;
    v->lo = lo;
    v->hi = hi;
    v->frac = 0;
    v->g = 0;
    v->dg = 32767 / BP_FADE + 1;
    if (from_start) {
        bp_vinit(v, s, lo, m->rev, !m->rev, bp_slice_st(s, sl));   /* (reversed: from lo, as the main voice) */
    } else {                                             /* (the same share of the slice as the main voice's) */
        uint32_t ml = m->hi > m->lo ? m->hi - m->lo : 1u, off = p > m->lo ? p - m->lo : 0u;
        bp_vinit(v, s, lo + (uint32_t)((uint64_t)(off < ml ? off : ml - 1u) * (hi - lo) / ml), m->rev, 0, 0);
    }
}

/* start a new voice at sample p of bank bk, slice sl, crossfading out the current one (and its LAYER's). st: the
 * decoder state before sample p (have_state), else sought */
static void bp_voice_start(uint32_t bk, uint32_t sl, uint32_t p, int rev, int have_state, uint32_t st)
{
    const slc_src_t *s = brkp_bank_src(bk);
    bpv_t *o = &bp.v[bp.cur], *v;
    int32_t amp = o->amp ? o->amp : 32767;
    uint8_t srev = o->srev;
    int8_t semi = o->semi;
    int16_t bend = o->bend, dbend = o->dbend;
    if (!s)
        return;
    if (o->on)
        o->dg = -(32767 / BP_FADE + 1);                  /* fade the old one out from where it is */
    bp.cur ^= 1u;
    v = &bp.v[bp.cur];
    v->on = 1;
    v->bank = (uint8_t)bk;
    v->amp = amp;                                        /* (a reverse switch or a replay keeps the hit's own) */
    v->srev = srev;
    v->semi = semi;
    v->bend = bend;
    v->dbend = dbend;
    v->lo = bp_slice_lo(s, sl);
    v->hi = bp_slice_hi(s, sl);
    v->frac = 0;
    v->g = 0;
    v->dg = 32767 / BP_FADE + 1;
    if (bp.late) {                                       /* the step began `late` samples ago: start where the loop is by now */
        uint32_t adv = (bp.late * bp_inc(s, bk, bp_vsemi(v))) >> 16;
        if (rev)
            p = p > v->lo + adv ? p - adv : v->lo;
        else
            p = p + adv < v->hi ? p + adv : v->hi - 1u;
    }
    bp_vinit(v, s, p, rev, have_state && !bp.late, st);
    bp_layer_start(v, sl, p, have_state && !bp.late && !rev);
}
/* MANGLE's dive on what sounds now (the main voice and its LAYER) */
static void bp_dive(int16_t bend, int16_t dbend)
{
    bp.v[bp.cur].bend = bend;
    bp.v[bp.cur].dbend = dbend;
    if (bp.lcur >= 2u) {
        bp.v[bp.lcur].bend = bend;
        bp.v[bp.lcur].dbend = dbend;
    }
}
static volatile uint8_t vis_bk, vis_bs, vis_bo;   /* the DRUMMER (ui_vis.c): the BREAK's kicks, snares, other hits so far */
/* a slice from its beginning (BB Gen's reverse: from its first sample), as a new hit: its level, pitch, reverse */
static void bp_slice_hit(uint32_t bk, uint32_t sl, int32_t amp, int srev, int semi)
{
    const slc_src_t *s = brkp_bank_src(bk);
    bpv_t *o = &bp.v[bp.cur];
    if (!s)
        return;
    o->amp = amp;                                        /* (bp_voice_start hands them on) */
    o->srev = (uint8_t)srev;
    o->semi = (int8_t)semi;
    o->bend = o->dbend = 0;
    bp_voice_start(bk, sl, bp_slice_lo(s, sl), bp.reverse ^ (srev != 0), 1, bp_slice_st(s, sl));
    bp.hits |= (uint8_t)(1u << (sl & 7u));
    bp.st_starts++;
    if (((bpb[bk & 3u].kicks ? bpb[bk & 3u].kicks : 1u) >> (sl & 7u)) & 1u) {
        bp.kick = 1;                                     /* (DUCK: a slice that opens on a kick) */
        bp.kicks_n++;
        vis_bk++;                                        /* (the DRUMMER: a kick, a snare, another hit) */
    } else if ((bpb[bk & 3u].snares >> (sl & 7u)) & 1u) {
        vis_bs++;
    } else {
        vis_bo++;
    }
}
static void bp_slice_start(uint32_t bk, uint32_t sl)     /* a replay of the hit sounding (a retrigger: its dive goes on) */
{
    const bpv_t *o = &bp.v[bp.cur];
    int16_t bend = o->bend, dbend = o->dbend;
    bp_slice_hit(bk, sl, o->amp ? o->amp : 32767, o->srev, o->semi);
    bp_dive(bend, dbend);
}
/* the velocity of a level: as played (LV_NORM: vel), ghost, soft, hard (the steps of every track) */
static uint32_t lvl_vel(uint32_t lvl, uint32_t vel)
{
    static const uint8_t V[4] = {0, 42, 72, 127};
    return lvl & 3u ? V[lvl & 3u] : vel;
}
/* a MIDI velocity -> the level it records as (keys play 100: LV_NORM) */
static uint32_t vel_lvl(uint32_t vel)
{
    return vel < 56u ? LV_GHOST : vel < 88u ? LV_SOFT : vel < 116u ? LV_NORM : LV_HARD;
}
static int32_t bp_lvl_amp(uint32_t lvl) { return (int32_t)lvl_vel(lvl, 100) * 258; }   /* LV_* -> Q15 */

/* ---- the generator (breakbeat.c) ------------------------------------------------------------- */
/* the ticks of a trigger: LEN (1/4 bar .. 8 bars), AUTO: the loop's bars, so the 8 slices are 8 triggers */
static uint32_t bp_ticks(uint32_t bank)
{
    uint32_t l = (uint32_t)BPV(BP_LEN) % 7u, b;
    if (l)
        return BP_LEN_TICKS[l - 1u];
    b = brkp_bank_bars(bank);
    return BP_LEN_TICKS[b == 1u ? 2u : b == 2u ? 3u : b == 4u ? 4u : 5u];
}
static void bp_gate_update(void)
{
    int held = bp.count > 0u;
    bp.gate_to = (held || bp.shot || (bp.running && bp.step_on)) ? 32767 : 0;
}

/* BB_FIRE_TRIGGER: decide, then sound. forced = transport start / loop switch: slice 0, no retrigger */
static void bp_fire(uint32_t beat, int forced)
{
    int slice, held;
    uint32_t tpt = bp_ticks(bp_bank());
    if (forced)
        slice = 0;
    else
        slice = (int)bp_select(beat, bp.bar);
    held = bp_perf_top();
    if (held >= 0)
        slice = held;                                    /* a held slice beats the engine */
    bp.current_slice = (int8_t)slice;
    bp.sub_div = 0;
    if (!forced) {
        static const uint8_t RDIV[4] = {2, 3, 4, 8};
        uint32_t i, n = 0, fired[4];
        for (i = 0; i < 4u; i++)
            if (bp_chance(bp_rate_p(bp_pct(BP_R2 + i), tpt)))
                fired[n++] = i;
        if (n)
            bp.sub_div = RDIV[fired[(bp_rnd() * n) >> 16]];
        if (bp.rat_next)
            bp.sub_div = RDIV[(bp.rat_next - 1u) & 3u];  /* the step's own retrigger */
        if (bp.stutter)
            bp.sub_div = 4;                              /* BB Gen's Stutter 4x pad */
        if (bp.roll)
            bp.sub_div = bp.roll;                        /* the moves' ROLL (punch.c): 2 (16ths) a trigger */
    }
    bp.render_bank = bp_bank();
    bp.st_mangle = 0;
    if (!forced && held < 0 && bp_chance(bp_pct(BP_MANGLE) * 492u))   /* MANGLE: up to 3 in 4 */
        bp.st_mangle = (uint8_t)(1u + ((bp_rnd() * 5u) >> 16));
    if (bp.st_mangle == 1u) {                            /* stutter: 4 .. 16 hits of the slice's head */
        static const uint8_t MDIV[4] = {4, 6, 8, 16};
        bp.sub_div = MDIV[(bp_rnd() * 4u) >> 16];
    }
    bp.sub_count = 0;
    bp.sub_elapsed = 0;
    bp.sub_len = bp.sub_div ? (int32_t)(bp.spt / bp.sub_div) : 0;
    if (bp.st_mangle == 5u) {                            /* gap: the trigger is a hole (what sounds fades) */
        uint32_t k;
        for (k = 0; k < 4u; k++)                         /* (its LAYER too) */
            if (bp.v[k].on && bp.v[k].dg >= 0)
                bp.v[k].dg = -(32767 / BP_FADE + 1);
        bp.sub_div = 0;
    } else if (!bp.hush) {
        static const int8_t JUMP[6] = {-12, -7, -5, 5, 7, 12};
        int semi = bp.semi_next + (bp.st_mangle == 3u ? JUMP[(bp_rnd() * 6u) >> 16] : 0);
        bp_slice_hit(bp.render_bank, (uint32_t)slice, bp_lvl_amp(bp.lvl_next), bp.srev_next ^ (bp.st_mangle == 2u), semi);
        if (bp.st_mangle == 4u) {                        /* dive: an octave down over the trigger, a tape stop */
            uint32_t blocks = bp.spt / CTL;
            bp_dive(0, (int16_t)-(192 / (int32_t)(blocks ? blocks : 1u) + 1));
        }
    }
    bp.st_trigs++;
    bp.st_slice = (int8_t)slice;
    bp.st_bank = (int8_t)bp.render_bank;
    bp.st_div = (int8_t)bp.sub_div;
    bp.st_bp = (int8_t)beat;
    bp.st_forced = (uint8_t)forced;
}

/* X0X: a step with a slice of its own plays it, from its start, in place of the generator's choice (no retrigger but
 * the step's own); the generator still counts the trigger the step would have made */
static void bp_pin_trigger(uint32_t slice)
{
    static const uint8_t RDIV[4] = {2, 3, 4, 8};
    bp.current_slice = (int8_t)slice;
    bp.render_bank = bp_bank();
    bp.sub_div = bp.rat_next ? RDIV[(bp.rat_next - 1u) & 3u] : 0u;
    bp.sub_count = 0;
    bp.sub_elapsed = 0;
    bp.sub_len = bp.sub_div ? (int32_t)(bp.sp16 / bp.sub_div) : 0;
    if (!bp.hush)
        bp_slice_hit(bp.render_bank, slice, bp_lvl_amp(bp.lvl_next), bp.srev_next, bp.semi_next);
    bp.st_trigs++;
    bp.st_slice = (int8_t)slice;
    bp.st_bank = (int8_t)bp.render_bank;
    bp.st_div = (int8_t)bp.sub_div;
    bp.st_forced = 0;
}

/* a clock trigger: BB Gen counts it whether or not half speed lets it fire */
static void bp_clock_trigger(void)
{
    uint32_t beat = bp.trigger_count % 8u;
    bp.trigger_count++;
    if (bp_perf_fires())
        bp_fire(beat, 0);
}

/* live key changes (bp.keys is written by the UI) */
static void bp_perf_sync(void)
{
    uint32_t k = bp.keys, d = (k ^ bp.keys_seen) & 0x7FFu, key;
    if (!d)
        return;
    for (key = 0; key <= 10u; key++) {
        uint32_t bit = 1u << key, down = (k & bit) != 0u;
        if (!(d & bit))
            continue;
        if (key < 8u) {
            if (down) {
                uint32_t bk = bp_bank();
                bp_perf_push((int)key);
                if (brkp_bank_src(bk)) {                 /* an instant hit, from the loop playing */
                    bp.current_slice = (int8_t)key;
                    bp.render_bank = (uint8_t)bk;
                    bp.sub_div = 0;
                    bp_slice_hit(bk, key, 32767, 0, 0);
                }
            } else {
                bp_perf_release((int)key);
            }
        } else if (key == 8u) {
            bp.reverse = (uint8_t)down;
        } else if (key == 9u) {
            bp.half = (uint8_t)down;
            bp.half_acc = 0;
        } else {
            bp.stutter = (uint8_t)down;
        }
    }
    bp.keys_seen = (uint16_t)k;
    bp_gate_update();
}

/* a step of the pattern (BB Gen's clock: a step is its 16th) */
static void bp_step(uint32_t step16, uint32_t bar, int enabled)
{
    uint32_t pb, tpt, tick;
    int forced = 0, pin;
    bp_perf_sync();
    bp.bar = bar;
    bp.step_on = (uint8_t)(enabled != 0);
    bp.shot = 0;                                         /* (a key hit between steps: heard up to here) */
    if (!bp.running) {                                   /* transport start (bb_reset_transport) */
        bp.running = 1;
        bp.trigger_count = 0;
        bp.current_slice = 0;
        bp.engine_bank = 0;
        bp.pending_bank = -1;
        bp.half_acc = 0;
        forced = 1;
    }
    pb = BP_PHRASE_BARS[(uint32_t)BPV(BP_PHRASE) % 5u];
    if (step16 == 0u) {                                  /* bar boundary */
        if (!forced && bp.pending_bank >= 0 && (uint32_t)bp.pending_bank != bp.engine_bank) {
            uint32_t was = bp_bank();
            bp.engine_bank = (uint8_t)bp.pending_bank;
            forced = bp_bank() != was;                   /* (a loop held: it goes on) */
        }
        bp.pending_bank = -1;
        if (pb) {
            uint32_t bip = bar % pb;
            if (bip == pb - 2u)
                bp.pending_bank = (int8_t)bp_alt_bank();   /* the fill bar's loop */
            else if (bip == pb - 1u)
                bp.pending_bank = 0;                     /* A again for the next phrase */
        } else if (!forced) {                            /* X0X: no phrase: the chances roll for every bar */
            uint32_t bank = bp_alt_bank();
            if (bank != bp.engine_bank) {
                uint32_t was = bp_bank();
                bp.engine_bank = (uint8_t)bank;
                forced = bp_bank() != was;
            }
        }
    }
    tpt = bp_ticks(bp_bank());
    bp.spt = bp.sp16 * tpt / 6u;
    tick = 6u * (step16 & 15u);
    bp.trig_left = 0;
    pin = bp.pin_next;
    bp.pin_next = -1;
    if (pin >= 0 && (!enabled || !brkp_bank_src(bp_bank()) || bp.count))
        pin = -1;                                        /* off, no loop, or a held slice key wins */
    if (bp.drop_kick && (forced || tick % tpt == 0u)) {  /* a drop's first hit: the loop's slice 0 when it opens on a kick,
                                                         * else its first kick (none found: slice 0); a held key wins */
        uint32_t k = bpb[bp_bank()].kicks, sl = 0;
        bp.drop_kick = 0;
        while (k && !((k >> sl) & 1u))
            sl++;
        if (brkp_bank_src(bp_bank()) && !bp.count)
            pin = (int)sl;
    }
    if (pin >= 0) {
        if (forced)
            bp.trigger_count = 1;
        else if (tick % tpt == 0u)
            bp.trigger_count++;
        bp_pin_trigger((uint32_t)pin);
    } else if (forced) {
        bp.trigger_count = 1;
        bp_fire(0, 1);
    } else if (tick % tpt == 0u) {
        bp_clock_trigger();
    } else if (bp.rat_next && enabled) {                 /* a retrigger on a step between triggers: the slice playing */
        static const uint8_t RDIV[4] = {2, 3, 4, 8};
        bp.sub_div = RDIV[(bp.rat_next - 1u) & 3u];
        bp.sub_count = 0;
        bp.sub_elapsed = 0;
        bp.sub_len = (int32_t)(bp.sp16 / bp.sub_div);
        if (bp.current_slice >= 0 && !bp.hush)
            bp_slice_hit(bp.render_bank, (uint32_t)bp.current_slice, bp_lvl_amp(bp.lvl_next), bp.srev_next, bp.semi_next);
    }
    if (tpt == 3u)                                       /* 1/4 bar: a 32nd-note trigger too, half a step later */
        bp.trig_left = (int32_t)(bp.sp16 / 2u) + 2;
    bp.hush = 0;
    bp_gate_update();
}

/* the transport stopped */
static void brkp_stop(void)
{
    bp.running = 0;
    bp.drop_kick = 0;
    bp.trig_left = 0;
    bp.sub_div = 0;
    bp_gate_update();
}

/* the sequencer reached step idx (grid step abs, `into` clock units after its start) of the BREAK track: its ON, its
 * own slice, level, retrigger, pitch and reverse (seq.c seq_tick, after the step's locks). plays = 0: its fill
 * condition failed (as a step off: the generator still counts it). hush: a key hit recorded into it sounds already */
static void brk_seq_step(track_t *t, uint32_t idx, uint32_t abs, uint32_t into, int plays, int hush)
{
    const bstep_t *s = &t->bstep[idx % NSTEP];
    uint32_t bpm = (uint32_t)clamp(song.g[G_BPM], 20, 400), on = plays && s->on;
    bp.sp16 = div_units((uint32_t)t->p[P_SDIV] % NDIV_STEP) / bpm;               /* the step, in samples */
    bp.late = into / bpm;                                /* (units a sample: BEAT_U = samples x BPM) */
    if (bp.late > bp.sp16)
        bp.late = 0;
    bp.pin_next = (int8_t)(on && s->slice ? (int)((s->slice - 1u) % BP_NSL) : -1);
    bp.rat_next = (uint8_t)(on ? s->rat % 5u : 0u);
    bp.lvl_next = (uint8_t)(s->lvl & 3u);
    bp.srev_next = (uint8_t)((s->flags & BS_REV) != 0u);
    bp.semi_next = (int8_t)clamp(s->pitch, -24, 24);
    bp.hush = (uint8_t)(hush != 0);
    bp_step(abs % 16u, abs / 16u, (int)on);
    bp.late = 0;
}

/* once a block (events_block, after the steps): the transport stopped */
static void brk_block(void)
{
    if (!song.playing && bp.running)
        brkp_stop();
}

/* a hit from a key or MIDI (seq.c brk_input): slice sl of the loop playing, now, at level lvl */
static void brk_hit(uint32_t sl, uint32_t lvl)
{
    uint32_t bk = bp_bank();
    bp.st_hits++;
    if (!brkp_bank_src(bk))
        return;
    bp.current_slice = (int8_t)(sl % BP_NSL);
    bp.render_bank = (uint8_t)bk;
    bp.sub_div = 0;
    bp.shot = 1;
    bp_slice_hit(bk, sl % BP_NSL, bp_lvl_amp(lvl), 0, 0);
    bp_gate_update();
}

/* LIVE (audio ISR): black key b (0..3 = loop A..D) down / up. The last one down plays now, whatever Q: the slice
 * sounding goes on from the same share of it in the held loop (in time with the bar); let go, the generator's loop
 * comes back the same way. Stopped: PLAY starts on it */
static void brk_hold_bank(uint32_t b, int down)
{
    uint32_t was = bp_bank(), now, i;
    b &= 3u;
    bp.hold_keys = (uint8_t)(down ? bp.hold_keys | 1u << b : bp.hold_keys & ~(1u << b));
    if (down)
        bp.hold = (uint8_t)(b + 1u);
    else if (bp.hold == b + 1u) {
        for (i = 0; i < 4u && !((bp.hold_keys >> i) & 1u); i++)
            ;
        bp.hold = (uint8_t)(i < 4u ? i + 1u : 0u);
    }
    now = bp_bank();
    if (now == was || !bp.running)
        return;
    bp.render_bank = (uint8_t)now;
    if (bp.current_slice >= 0 && bp.v[bp.cur].on && bp.v[bp.cur].dg >= 0) {
        const bpv_t *v = &bp.v[bp.cur];
        const slc_src_t *s = brkp_bank_src(now);
        uint32_t sl = (uint32_t)bp.current_slice % BP_NSL, lo = bp_slice_lo(s, sl), hi = bp_slice_hi(s, sl);
        uint32_t p = v->rev ? v->rpos : v->d.pos, ml = v->hi > v->lo ? v->hi - v->lo : 1u;
        uint32_t off = p > v->lo ? p - v->lo : 0u;
        if (off >= ml)
            off = ml - 1u;
        bp_voice_start(now, sl, lo + (uint32_t)((uint64_t)off * (hi - lo) / ml), v->rev, 0, 0);
    }
}

/* ---- render (breaks_render): n <= CTL samples of the mono track into out (WRITES), at a voice's scale (fx.c
 * mix_part takes it from there: DIST, SLICER, FILTER, LEVEL + P_ED_FX, PAN, the sends). 0 = silent ---- */
static int brkp_idle(void)           /* nothing can sound this block: no voice, the gate shut and staying shut */
{
    return !bp.v[0].on && !bp.v[1].on && !bp.v[2].on && !bp.v[3].on && bp.gate == 0 && bp.gate_to == 0 && bp.trig_left == 0 &&
           bp.sub_count + 1u >= bp.sub_div && bp.keys == bp.keys_seen;
}
/* the loop and the speed of each voice (the block's start, and after anything that started one) */
static void bp_srcinc(const slc_src_t **src, uint32_t *inc)
{
    uint32_t j;
    for (j = 0; j < 4u; j++) {
        const bpv_t *v = &bp.v[j];
        src[j] = v->on ? brkp_bank_src(v->bank) : 0;
        inc[j] = src[j] ? bp_inc(src[j], v->bank, bp_vsemi(v)) : 0;
    }
}
static uint32_t brkp_render(int32_t *out, uint32_t n)
{
    uint32_t inc[4] = {0, 0, 0, 0}, i, j, psx = BPV(BP_INTP) != 0;
    const slc_src_t *src[4] = {0, 0, 0, 0};
    if (brkp_idle())
        return 0;
    bp_perf_sync();
    for (j = 0; j < 4u; j++) {
        bpv_t *v = &bp.v[j];
        if (!v->on)
            continue;
        src[j] = brkp_bank_src(v->bank);
        if (v->dbend)                                    /* MANGLE's dive, a block at a time */
            v->bend = (int16_t)clamp(v->bend + v->dbend, -384, 0);
        if (!src[j] || v->hi > src[j]->len) {            /* its loop was replaced, or is being written */
            v->on = 0;
            src[j] = 0;
        } else {
            inc[j] = bp_inc(src[j], v->bank, bp_vsemi(v));
        }
    }
    {   /* reverse pressed or released: continue from where the playhead is, the other way */
        bpv_t *v = &bp.v[bp.cur];
        uint32_t want = bp.reverse ^ (v->srev != 0u);
        if (v->on && v->rev != want && v->dg >= 0) {
            const slc_src_t *s = src[bp.cur];
            uint32_t bk = v->bank, pos = v->rev ? v->rpos : (v->d.pos > 1u ? v->d.pos - 2u : 0u), sl;
            if (s) {
                sl = bp_slice_of(s, v->lo);
                bp_voice_start(bk, sl, pos, (int)want, 0, 0);
                bp_srcinc(src, inc);
            }
        }
    }
    for (i = 0; i < n; i++) {
        int32_t y = 0;
        if (bp.trig_left > 0 && --bp.trig_left == 0 && bp.running) {
            bp_clock_trigger();
            bp_srcinc(src, inc);
        }
        if (bp.sub_count + 1u < bp.sub_div) {            /* retrigger: replay the head of the slice */
            if (bp.sub_elapsed >= (int32_t)(bp.sub_count + 1u) * bp.sub_len) {
                bp.sub_count++;
                bp_slice_start(bp.render_bank, (uint32_t)bp.current_slice);
                bp_srcinc(src, inc);
            }
            bp.sub_elapsed++;
        }
        for (j = 0; j < 4u; j++) {
            bpv_t *v = &bp.v[j];
            const slc_src_t *s = src[j];
            if (!v->on)
                continue;
            if (!s) {
                v->on = 0;
                continue;
            }
            if (psx) {                                   /* INTP PSX: the Gaussian over h2 h1 s0 s1 (eng_sample.c's) */
                const int16_t *gk = &GAUSS4[((v->frac >> 8) & 255u) * 4u];
                y += mulq15(mulq15((gk[0] * v->h2 + gk[1] * v->h1 + gk[2] * v->s0 + gk[3] * v->s1) >> 15, v->g), v->amp);
            } else {
                y += mulq15(mulq15(v->s0 + (((v->s1 - v->s0) * (int32_t)(v->frac >> 1)) >> 15), v->g), v->amp);
            }
            v->g += v->dg;
            if (v->g >= 32767) {
                v->g = 32767;
                v->dg = 0;
            } else if (v->g <= 0) {
                v->on = 0;
                continue;
            }
            v->frac += inc[j];
            while (v->frac >= 65536u) {
                v->frac -= 65536u;
                v->h2 = v->h1;
                v->h1 = (int16_t)clamp(v->s0, -32768, 32767);
                v->s0 = v->s1;
                if (v->rev) {
                    v->rpos = bp_rev_next(v, v->rpos);
                    v->s1 = bp_rev_fetch(v, s, v->rpos);
                } else {
                    v->s1 = bp_next(s, &v->d);
                }
            }
            if (bp.shot && !bp.running && v->dg >= 0 &&               /* stopped: a key hit ends with its slice */
                (v->rev ? v->rpos == v->lo : v->d.pos > v->hi || v->d.pos < v->lo))
                v->dg = -(32767 / BP_FADE + 1);
        }
        if (bp.gate < bp.gate_to)
            bp.gate = bp.gate + 32767 / BP_FADE + 1 < bp.gate_to ? bp.gate + 32767 / BP_FADE + 1 : bp.gate_to;
        else if (bp.gate > bp.gate_to)
            bp.gate = bp.gate - (32767 / BP_FADE + 1) > bp.gate_to ? bp.gate - (32767 / BP_FADE + 1) : bp.gate_to;
        out[i] = mulq15(mulq15(y, bp.gate), VOICE_FS) << 1;   /* (at a voice's level, as mix_part takes it) */
    }
    if (!bp.running && bp.shot && !bp.v[bp.cur].on) {    /* stopped: a key hit ends with its slice */
        bp.shot = 0;
        bp_gate_update();
    }
    if (bp.gate == 0 && bp.gate_to == 0 && !bp.running) {
        bp.v[0].on = bp.v[1].on = 0;                     /* stopped and silent: stop decoding */
        bp.v[2].on = bp.v[3].on = 0;
    }
    return 1;
}

/* ---- the keys and MIDI ------------------------------------------------------------------------ */
/* the slice key k plays (0 = F3 .. 26 = G5) off the BREAK screen: the white keys in order, slices 1..8 twice; a black
 * key the white key left of it (two fingers on one slice) */
static uint32_t brk_slice_of_key(uint32_t k)
{
    static const int8_t W[12] = {0, -1, 1, -1, 2, -1, 3, 4, -1, 5, -1, 6};   /* from F */
    int32_t i = W[k % 12u];
    if (i < 0)
        i = W[(k - 1u) % 12u];
    return (uint32_t)((int32_t)(k / 12u) * 7 + i) % BP_NSL;
}
/* the white key (key index) of slice sl, its first one, for the key LEDs */
static uint32_t brk_key_of_slice(uint32_t sl)
{
    static const uint8_t K[7] = {0, 2, 4, 6, 7, 9, 11};      /* F G A B C D E */
    sl %= BP_NSL;
    return (sl / 7u) * 12u + K[sl % 7u];
}
static uint32_t brk_slice_of_note(uint32_t note) { return (note >= 36u ? note - 36u : note) % BP_NSL; }   /* C2 = slice 1 */

/* ---- the UI's ------------------------------------------------------------------------------- */
static void brkp_key(uint32_t key, int down)             /* 0-7 hold slice, 8 reverse, 9 half speed, 10 stutter */
{
    if (key > 10u)
        return;
    bp.keys = (uint16_t)(down ? (bp.keys | (1u << key)) : (bp.keys & ~(1u << key)));
}
