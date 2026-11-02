/* SPDX-License-Identifier: GPL-3.0-only */
/* The FX panel (FMX): FX held, on any screen of LIVE (ui_live.c) or STUDIO (ui_layers.c), or locked open (FX held +
 * HOME); the same panel and the same effects in both. The 16 white keys are effects in four groups of four, for as long
 * as a key is held; black keys 1..4 pick their tracks (BREAK, BASS, CHORDS, CHOP; none = all four), black key 5 is
 * LATCH, three ways round: OFF (the keys held), TRACKS (black keys 1..4 tapped stick as the tracks, the white keys still
 * held; one padlock), TRACKS + FX (a white key tapped sticks too, tapped again: off; two padlocks). What is held as a way
 * comes in sticks (the tracks, then the effects); back to OFF, the keys are held again (what stuck plays on). Black key 6
 * CLEAR: every effect stuck off, every tail cut, the tracks and LATCH off. The keys come in the audio ISR (seq.c
 * lfx_key_down); KNOB 1..4 (the panel shown): AMOUNT, TIME, TONE, FEEDBACK.
 *   SPACE   1 DUB VERB    the tracks thrown into the reverb through a dark spring-like band (their dry as it is)
 *           2 ECHO THROW  ... into the echo (TIME: 1/8 dotted ..), its feedback (FEEDBACK) dying away after (2 bars)
 *           3 SPARKLE     ... an octave up (a two-head pitch shifter), bright, into the reverb: a glassy shimmer
 *           4 VOID        the tracks' dry gone, their reverb blooming; let go: the dry back on the next beat
 *   TIME    5 FREEZE      the tracks from the press, a short window of them (TIME's length) held as a cloud of two grains
 *           6 SHARDS      the beat after the press, then that beat in 1/32 shards out of order, some an octave up / down
 *           7 TAPE        the tape wavering (wow, flutter); held past a beat it slows to a stop
 *           8 REVERSE     each stretch (TIME's length) played backwards, a stretch late
 *           Their tracks' dry goes through a stem of its own (fx.c: mixacc_t tl / tr) into a ring of 0.37 s at 22.05 kHz
 *           (lt_ring, .lowram), one effect of the group at a time (the last pressed); AMOUNT its level.
 *   COLOUR  9 DUB FILTER  a resonant low-pass closing over a beat down to TONE's (100 Hz .. 1 kHz); let go: open again
 *          10 PSX CRUSH   fewer samples and fewer bits (AMOUNT: from a little grit to 5 kHz and 5 bits)
 *          11 VOWEL       two formants gliding a-e-i-o-u, a vowel a beat: the track talks
 *          12 RESONATOR   a comb tuned to the song's root (C3..B3), ringing (FEEDBACK)
 *           In each track of theirs, after its FILTER and before its level and sends (fx.c mix_part, lfx_colour): with
 *           TIME and SPACE at once (colour, then time, then space); several at once, in that order.
 *   RHYTHM (13..16) to come.
 * A throw is a send of its own per track (the track's sends and its project untouched), added in the mix (fx.c) and
 * ramped over each block. */
#define LFX_N 16u
enum { LM_OFF, LM_TRACKS, LM_ALL, LM_N };                /* LATCH: nothing sticks; the tracks; the tracks and the effects */
enum { LX_DUB, LX_ECHO, LX_SPARK, LX_VOID, LX_FREEZE, LX_SHARDS, LX_TAPE, LX_REVERSE, LX_DFILT, LX_CRUSH, LX_VOWEL,
       LX_RESON, LX_READY };   /* (the ones that
                                                         * play so far: 0 .. LX_READY - 1) */
static const char *const LFX_NAME[LFX_N] = {"DUB", "ECHO", "SPARK", "VOID", "FREEZE", "SHARDS", "TAPE", "REVRS",
                                           "FILTER", "CRUSH", "VOWEL", "RESON", "GATE", "RATCH", "REPEAT", "DRIFT"};
#define LFX_SPK_N 2048u                                  /* SPARKLE's line: 46 ms, its two heads half of it apart */
static int16_t lfx_spk_buf[LFX_SPK_N];
static struct {
    volatile uint16_t held;                             /* the white keys held (the ISR's) */
    volatile uint16_t latch;                            /* stuck on (LATCH: a white key tapped) */
    volatile uint8_t tgt[LFX_N];                        /* each effect's tracks (bit k) as it was pressed */
    volatile uint8_t black;                             /* black keys 1..6 held (bit j) */
    volatile uint8_t mode;                              /* LATCH (black key 5 tapped round): LM_OFF, LM_TRACKS, LM_ALL */
    volatile uint8_t tsel;                              /* LATCH's tracks, stuck (bits as black keys 1..4) */
    volatile uint8_t clear;                             /* black key 6: every tail cut (the ISR, next block) */
    uint8_t amt, time, tone, fb;                        /* the knobs: AMOUNT 0..127, TIME (G_DTIME), TONE 0..127, FEEDBACK */
    volatile uint8_t last;                              /* the effect pressed last (the knobs', the screen's) */
    /* the ISR's */
    uint16_t on;                                        /* in force this block */
    uint8_t voided, vback, echo;                        /* VOID's tracks; those let go, back on the next beat; the echo taken */
    uint32_t vbeat;                                     /* the beat they were let go in */
    uint32_t tail;                                      /* blocks of the echo's tail to go */
    int16_t g_dt, g_fb, g_col, g_mix;                   /* the delay's own, while the echo has it */
    int32_t rev[2][NTRK], spk[2][NTRK], dly[2][NTRK], dry[2][NTRK];   /* per track, Q15: [0] the block's start, [1] end */
    int32_t lp, hp, slp, shp;                           /* DUB's band, SPARKLE's high-pass */
    uint32_t sw, sph;                                   /* SPARKLE: its line's write index, the heads' phase (Q16) */
    uint8_t any_rev, any_spk;                           /* (nothing thrown: no work on the buses) */
    volatile uint8_t ins;                               /* the tracks through the TIME stem (the mix reads it, both cores) */
    volatile uint8_t col[4];                            /* COLOUR: each effect's tracks this block (DFILT, CRUSH, VOWEL, RESON) */
    volatile uint8_t colact;                            /* the tracks with a COLOUR effect on or still going out */
} lfx = {.amt = 110, .time = 6, .tone = 56, .fb = 88, .dry = {{32767, 32767, 32767, 32767}, {32767, 32767, 32767, 32767}}};

/* a white key down (the ISR): its tracks the black keys held (LATCH: those stuck; none: all); LATCH TRACKS + FX: it
 * sticks */
static void lfx_press(uint32_t w)
{
    uint32_t b = (lfx.mode ? lfx.tsel : lfx.black) & 15u;
    lfx.tgt[w] = (uint8_t)(b ? (b & 1u ? 1u << TRK_BRK : 0u) | (b & 2u ? 1u : 0u) | (b & 4u ? 2u : 0u) | (b & 8u ? 4u : 0u)
                             : 15u);           /* (black keys 1..4: BREAK, BASS, CHORDS, CHOP; tracks 3, 0, 1, 2) */
    lfx.last = (uint8_t)w;
    if (lfx.mode == LM_ALL)
        lfx.latch = (uint16_t)(lfx.latch ^ (1u << w));
    else
        lfx.held = (uint16_t)(lfx.held | 1u << w);
}
/* black key j (0..5) down (the ISR): 1..4 the tracks (LATCH: tapped, they stick), 5 LATCH round, 6 CLEAR */
static void lfx_black(uint32_t j)
{
    lfx.black = (uint8_t)(lfx.black | 1u << j);
    if (j < 4u) {
        if (lfx.mode)
            lfx.tsel = (uint8_t)(lfx.tsel ^ 1u << j);
    } else if (j == 4u) {
        lfx.mode = (uint8_t)((lfx.mode + 1u) % LM_N);
        if (lfx.mode == LM_TRACKS)                      /* (what is held as a way comes in sticks: the tracks, ...) */
            lfx.tsel = (uint8_t)(lfx.black & 15u);
        else if (lfx.mode == LM_ALL)                    /* (... the effects) */
            lfx.latch = (uint16_t)(lfx.latch | lfx.held);
        else
            lfx.tsel = 0;
    } else {
        lfx.clear = 1;
        lfx.mode = LM_OFF;
        lfx.tsel = 0;
    }
}
static void lfx_release(uint32_t w) { lfx.held = (uint16_t)(lfx.held & ~(1u << w)); }
/* effect e on track k, this block */
static int lfx_at(uint32_t on, uint32_t e, uint32_t k) { return ((on >> e) & 1u) && ((lfx.tgt[e] >> k) & 1u); }

static void lt_block(uint32_t on);
static void lc_block(uint32_t on);
/* once a block, before the tracks (fx.c mix_block): each track's throws, VOID's dry, the echo's settings and tail */
__attribute__((noinline)) static void lfx_block(void)   /* (out of the ISR's own code: a block's work, not a sample's) */
{
    uint32_t on = lfx.held | lfx.latch, k, v = 0, any = 0, spk = 0;
    int32_t a = lfx.amt * 258;
    if (lfx.clear) {                                    /* black key 6: every tail cut, everything stuck off */
        lfx.clear = 0;
        lfx.latch = 0;
        on = lfx.held;
        memset(dly_buf, 0, sizeof dly_buf);
        memset(spu_buf, 0, sizeof spu_buf);
        memset(lfx_spk_buf, 0, sizeof lfx_spk_buf);
        memset(fx.ls, 0, sizeof fx.ls);
        memset(fx.ld, 0, sizeof fx.ld);
        fx.dly_lp = 0;
        lfx.tail = 0;
    }
    if ((as_throw & 15u) && !((on >> LX_DUB) & 1u)) {   /* AUTO SONG's throw of a drop's first hit (arranger_scene.c):
                                                         * the panel's own, on the tracks it says (not when held) */
        on |= 1u << LX_DUB;
        lfx.tgt[LX_DUB] = (uint8_t)(as_throw & 15u);
    }
    if ((as_throw >> 4) && !((on >> LX_ECHO) & 1u)) {
        on |= 1u << LX_ECHO;
        lfx.tgt[LX_ECHO] = (uint8_t)(as_throw >> 4);
    }
    lfx.on = (uint16_t)on;
    lt_block(on);
    lc_block(on);
    for (k = 0; k < NTRK; k++)
        if (lfx_at(on, LX_VOID, k))
            v |= 1u << k;
    if (lfx.voided & ~v & ~lfx.vback) {                 /* VOID let go: the dry back on the next beat */
        lfx.vback |= (uint8_t)(lfx.voided & ~v);
        lfx.vbeat = clk_beat;
    }
    lfx.vback &= (uint8_t)~v;
    if (lfx.vback && (!song.playing || clk_beat != lfx.vbeat))
        lfx.vback = 0;
    v |= lfx.vback;
    lfx.voided = (uint8_t)v;
    for (k = 0; k < NTRK; k++) {
        lfx.rev[0][k] = lfx.rev[1][k];
        lfx.spk[0][k] = lfx.spk[1][k];
        lfx.dly[0][k] = lfx.dly[1][k];
        lfx.dry[0][k] = lfx.dry[1][k];
        lfx.rev[1][k] = lfx_at(on, LX_DUB, k) || ((v >> k) & 1u) ? a : 0;
        lfx.spk[1][k] = lfx_at(on, LX_SPARK, k) ? a : 0;
        lfx.dly[1][k] = lfx_at(on, LX_ECHO, k) ? a : 0;
        lfx.dry[1][k] = (v >> k) & 1u ? 0 : 32767;
        any |= (uint32_t)(lfx.rev[0][k] | lfx.rev[1][k]);
        spk |= (uint32_t)(lfx.spk[0][k] | lfx.spk[1][k]);
    }
    lfx.any_rev = (uint8_t)(any != 0u || lfx.lp != 0);
    lfx.any_spk = (uint8_t)(spk != 0u);
    if ((on >> LX_ECHO) & 1u) {                         /* the echo: its time and feedback the knobs' */
        if (!lfx.echo) {
            lfx.echo = 1;
            lfx.g_dt = song.g[G_DTIME];
            lfx.g_fb = song.g[G_DFDBK];
            lfx.g_col = song.g[G_DCOLOR];
            lfx.g_mix = song.g[G_DMIX];
        }
        song.g[G_DTIME] = (int16_t)(lfx.time % 8u);
        song.g[G_DFDBK] = (int16_t)clamp(lfx.fb, 0, 110);
        song.g[G_DCOLOR] = (int16_t)clamp(lfx.tone, 0, 127);
        song.g[G_DMIX] = 110;
        lfx.tail = 8u * BEAT_U / ((uint32_t)song.g[G_BPM] * CTL);
    } else if (lfx.echo) {                              /* let go: its tail dies away over 2 bars, then the delay back */
        uint32_t all = 8u * BEAT_U / ((uint32_t)song.g[G_BPM] * CTL);
        if (lfx.tail) {
            lfx.tail--;
            song.g[G_DFDBK] = (int16_t)(lfx.g_fb + (clamp(lfx.fb, 0, 110) - lfx.g_fb) * (int32_t)lfx.tail /
                                        (int32_t)(all ? all : 1u));
        } else {
            song.g[G_DTIME] = lfx.g_dt;
            song.g[G_DFDBK] = lfx.g_fb;
            song.g[G_DCOLOR] = lfx.g_col;
            song.g[G_DMIX] = lfx.g_mix;
            lfx.echo = 0;
        }
    }
}

/* the throws of the buses, one sample (fx_buses): DUB's band into the reverb, SPARKLE an octave up into it too */
static int32_t lfx_rev_in(int32_t dub, int32_t spk)
{
    int32_t out = 0;
    if (lfx.any_rev) {                                  /* a dark spring: a band, 150 Hz .. TONE (500 Hz .. 4 kHz) */
        int32_t c = 2200 + lfx.tone * 70;
        lfx.lp += (int32_t)(((int64_t)(dub - lfx.lp) * c) >> 15);
        lfx.hp += (int32_t)(((int64_t)(lfx.lp - lfx.hp) * 700) >> 15);
        out += (lfx.lp - lfx.hp) << 1;
    }
    if (lfx.any_spk) {                                  /* two heads reading at twice the speed, crossfaded: +12 */
        uint32_t d0, d1, p0, p1;
        int32_t w0, w1, y;
        lfx_spk_buf[lfx.sw & (LFX_SPK_N - 1u)] = (int16_t)clamp(spk >> 1, -32768, 32767);
        lfx.sph += 65536u;                              /* (the heads move one sample a sample faster than the write) */
        d0 = (lfx.sph >> 16) & (LFX_SPK_N - 1u);
        d1 = (d0 + LFX_SPK_N / 2u) & (LFX_SPK_N - 1u);
        p0 = (lfx.sw - (LFX_SPK_N - 1u - d0)) & (LFX_SPK_N - 1u);
        p1 = (lfx.sw - (LFX_SPK_N - 1u - d1)) & (LFX_SPK_N - 1u);
        w0 = (int32_t)(d0 < LFX_SPK_N / 2u ? d0 : LFX_SPK_N - 1u - d0) * 65535 / (int32_t)LFX_SPK_N;   /* triangles */
        w1 = 32767 - w0;
        y = mulq15(lfx_spk_buf[p0], w0) + mulq15(lfx_spk_buf[p1], w1);
        lfx.sw++;
        lfx.shp += (int32_t)(((int64_t)((y << 1) - lfx.shp) * 2600) >> 15);   /* bright: the lows off (~600 Hz) */
        out += ((y << 1) - lfx.shp) << 1;
    }
    return out;
}
/* a block of them (fx.c mix_block, only while any plays): into the reverb's send, as loud as a send at 1.5 */
__attribute__((noinline)) static void lfx_bus(const int32_t *dub, const int32_t *spk, int32_t *rev, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; i++) {
        int32_t y = lfx_rev_in(dub[i], spk[i]);
        rev[i] += y + (y >> 1);
    }
}

/* ---- TIME: the stem of its tracks through a ring (the ISR, a block at a time, only while one plays) */
#define LTR_N 8192u                                      /* 0.37 s at 22.05 kHz */
static int16_t lt_ring[LTR_N] __attribute__((section(".lowram")));
static struct {
    uint8_t fx;                                         /* the effect playing (LX_FREEZE ..), 0 none */
    uint8_t held;                                       /* still held (else fading out) */
    uint32_t w;                                         /* ring samples written */
    uint32_t t0;                                        /* w at the press */
    int32_t pend;                                       /* the first of a pair of output samples (22.05 kHz: their mean) */
    uint32_t n;                                         /* output samples since the press */
    int32_t tw;                                         /* the wet's weight, Q15 */
    uint32_t len;                                       /* ring samples: FREEZE's window, SHARDS' beat, REVERSE's stretch */
    uint32_t gp[2], go[2];                              /* FREEZE: each grain's place in it, its offset; SHARDS: (gp[0]) */
    uint32_t rp;                                        /* TAPE / SHARDS: the read position, Q16 ring samples */
    int32_t rate;                                       /* ... its speed, Q16 (65536 = as recorded) */
    uint32_t rng;
} lt = {.rng = 0x2545F491u};
static uint32_t lt_rand(void) { lt.rng = lt.rng * 1664525u + 1013904223u; return lt.rng >> 8; }
static int32_t lt_rd(uint32_t p16)                     /* the ring at Q16 position p16 (ring samples), linear */
{
    int32_t a = lt_ring[(p16 >> 16) & (LTR_N - 1u)], b = lt_ring[((p16 >> 16) + 1u) & (LTR_N - 1u)];
    return a + (((b - a) * (int32_t)((p16 >> 1) & 0x7FFFu)) >> 15);
}
static int32_t lt_tri(uint32_t q, uint32_t l)          /* a triangle over l, Q15 */
{
    return l ? (int32_t)((q < l / 2u ? q : l - q) * 65534u / l) : 0;
}
/* the effect's sample now (at the output's rate), 0 while it is not ready (the dry plays) */
static int32_t lt_wet(int *ready)
{
    uint32_t since = lt.w - lt.t0, i;
    int32_t y = 0;
    *ready = 0;
    switch (lt.fx) {
    case LX_FREEZE: {                                   /* two grains of 3/4 of the window, half a grain apart */
        uint32_t g = lt.len * 3u / 4u;
        if (since < lt.len || !g)
            return 0;
        for (i = 0; i < 2u; i++) {
            if (++lt.gp[i] >= 2u * g) {                 /* (a grain round: another place in the window) */
                lt.gp[i] = 0;
                lt.go[i] = lt_rand() % (lt.len / 4u + 1u);
            }
            y += lt_rd((lt.t0 + lt.go[i] + lt.gp[i] / 2u) << 16) * lt_tri(lt.gp[i], 2u * g) >> 15;
        }
        *ready = 1;
        return y;
    }
    case LX_SHARDS: {                                   /* 1/32 shards of the beat recorded, out of order */
        uint32_t piece = lt.len / 8u ? lt.len / 8u : 1u, olen = 2u * piece;
        if (since < lt.len)
            return 0;
        if (lt.gp[0] >= olen || !lt.n) {                /* the next shard: where, how fast */
            uint32_t k = lt_rand() % 8u, sp = lt_rand() % 6u;
            lt.rate = sp == 0u ? 131072 : sp == 1u ? 32768 : 65536;
            if (lt.rate > 65536 && k > 5u)
                k -= 2u;                                /* (an octave up reads two: inside the beat) */
            lt.rp = (lt.t0 + k * piece) << 16;
            lt.gp[0] = 0;
        }
        y = lt_rd(lt.rp);
        lt.rp += (uint32_t)lt.rate >> 1;
        {   /* short fades at its ends */
            uint32_t q = lt.gp[0]++, f = 64u;
            if (q < f)
                y = y * (int32_t)q / (int32_t)f;
            else if (olen - q < f)
                y = y * (int32_t)(olen - q) / (int32_t)f;
        }
        *ready = 1;
        return y;
    }
    case LX_TAPE: {                                     /* wavering 18 ms behind; past a beat, slowing to a stop */
        uint32_t beat = (uint32_t)(dly_samples(0) / 2u), wob;
        int32_t w0 = osc_sine(lt.n * 73043u) * 60 + osc_sine(lt.n * 681000u) * 6;   /* (wow ~0.75 Hz, flutter ~7 Hz) */
        if (since < 520u)
            return 0;
        if (!lt.rp)
            lt.rp = (lt.w - 400u) << 16;
        if (lt.n > 2u * beat) {                         /* (held past a beat: a beat more to a stop) */
            uint32_t k = lt.n - 2u * beat;
            lt.rate = k >= 2u * beat ? 0 : (int32_t)(65536u - (uint32_t)((uint64_t)k * 65536u / (2u * beat)));
        } else {
            lt.rate = 65536;
        }
        lt.rp += (uint32_t)lt.rate >> 1;
        wob = (uint32_t)(w0 >> 15);
        y = lt_rd(lt.rp - (wob << 16));
        if (lt.rate < 65536)
            y = (int32_t)((int64_t)y * (lt.rate + 8192) / (65536 + 8192));   /* (dying with it) */
        *ready = 1;
        return y;
    }
    case LX_REVERSE: {                                  /* the stretch before, backwards */
        uint32_t c = lt.len ? lt.len : 1u, k = since / c, q = since % c;
        if (k < 1u)
            return 0;
        y = lt_rd((lt.t0 + k * c - 1u - q) << 16);
        if (q < 128u)                                   /* (short fades: no click where it turns) */
            y = y * (int32_t)q / 128;
        else if (c - q < 128u)
            y = y * (int32_t)(c - q) / 128;
        *ready = 1;
        return y;
    }
    default:
        return 0;
    }
}
/* a block of it (fx.c mix_block, only while one plays): the stem in, the stem or the effect out into the mix */
__attribute__((noinline)) static void lfx_time(const int32_t *tl, const int32_t *tr, int32_t *l, int32_t *r, uint32_t n)
{
    uint32_t i;
    int32_t a = lfx.amt * 258;
    for (i = 0; i < n; i++) {
        int32_t s = (tl[i] + tr[i]) >> 1, y, to, ready;
        if (lt.n & 1u) {                                /* (22.05 kHz: a pair's mean) */
            lt_ring[lt.w & (LTR_N - 1u)] = (int16_t)clamp((lt.pend + s) >> 3, -32768, 32767);   /* (the mix runs to
                                                         * ~2^17: a pair's mean / 4 fits 16 bits) */
            lt.w++;
        } else {
            lt.pend = s;
        }
        y = lt_wet(&ready) * 4;
        lt.n++;
        to = lt.held && ready ? 32767 : 0;
        lt.tw += to > lt.tw ? (to - lt.tw > 128 ? 128 : to - lt.tw) : -(lt.tw - to > 128 ? 128 : lt.tw - to);
        y = (int32_t)(((int64_t)y * a) >> 15);         /* (64 bits: the mix's values pass 2^16) */
        l[i] += tl[i] + (int32_t)(((int64_t)(y - tl[i]) * lt.tw) >> 15);
        r[i] += tr[i] + (int32_t)(((int64_t)(y - tr[i]) * lt.tw) >> 15);
    }
    if (!lt.held && !lt.tw) {                           /* faded out: the tracks back into the mix */
        lt.fx = 0;
        lfx.ins = 0;
    }
}
/* lfx_block's part: which TIME effect plays (the last pressed of those on), from its press */
static void lt_block(uint32_t on)
{
    uint32_t t = on & 0xF0u, e = 0;
    if (t)
        e = (t >> lfx.last) & 1u ? lfx.last : (t & 0x10u ? 4u : t & 0x20u ? 5u : t & 0x40u ? 6u : 7u);
    if (e && (e != lt.fx || !lt.held)) {               /* pressed (or another): from now */
        uint32_t len = dly_samples(lfx.time % 8u) / 2u;
        lt.fx = (uint8_t)e;
        lt.t0 = lt.w;
        lt.n = 0;
        lt.gp[0] = lt.gp[1] = 0;
        lt.go[0] = lt.go[1] = 0;
        lt.rp = 0;
        lt.rate = 65536;
        lt.len = e == LX_SHARDS ? dly_samples(0) / 2u : len < 64u ? 64u : len > LTR_N / 2u ? LTR_N / 2u : len;
        if (lt.len > LTR_N - 256u)
            lt.len = LTR_N - 256u;
        lfx.ins = lfx.tgt[e];
    }
    lt.held = (uint8_t)(e != 0u);
}

/* ---- COLOUR: in each of its tracks (fx.c mix_part, on either core: each track's own state only) */
#define LC_CN 512u                                      /* RESONATOR's comb: periods to 338 samples (C3) */
static const int32_t LC_PER[12] = {86303, 81460, 76888, 72572, 68499, 64655, 61026, 57601, 54368, 51316, 48436, 45718};
static const int16_t LC_V1[5] = {3733, 1867, 1400, 2101, 1541};   /* a e i o u: F1, F2 as SVF coefficients (Q15) */
static const int16_t LC_V2[5] = {5363, 7917, 10690, 3733, 3034};
static const int16_t LC_DF[33] = {280, 323, 374, 431, 498, 575, 664, 767, 886, 1023, 1181, 1364, 1575, 1819, 2100, 2425,
                                  2800, 3233, 3733, 4311, 4977, 5745, 6631, 7653, 8831, 10188, 11749, 13543, 15601,
                                  17958, 20648, 23707, 27167};   /* 60 Hz .. 6 kHz, exponential */
static struct {
    int32_t w[4];                                       /* each effect's weight, Q15 */
    int32_t s;                                          /* DUB FILTER's cutoff: its table index, Q16 */
    int32_t dl, db, v1l, v1b, v2l, v2b;                 /* the filters' states */
    int32_t hold, hn;                                   /* CRUSH: the sample held, its samples to go */
    uint32_t cw;                                        /* RESONATOR: the comb's write index */
    int16_t comb[LC_CN];
} lc[NTRK] __attribute__((section(".pool")));
static int32_t m15(int32_t a, int32_t b) { return (int32_t)(((int64_t)a * b) >> 15); }
/* lfx_block's part: each COLOUR effect's tracks; those still going out (a weight left, DUB FILTER opening) stay on */
static void lc_block(uint32_t on)
{
    uint32_t e, k, act = 0;
    for (e = 0; e < 4u; e++) {
        lfx.col[e] = (uint8_t)((on >> (LX_DFILT + e)) & 1u ? lfx.tgt[LX_DFILT + e] : 0u);
        act |= lfx.col[e];
    }
    for (k = 0; k < NTRK; k++)
        if (lc[k].w[0] | lc[k].w[1] | lc[k].w[2] | lc[k].w[3])
            act |= 1u << k;
    lfx.colact = (uint8_t)act;
}
/* a block of track k's mono b[] (before its level): the COLOUR effects on it, in order */
__attribute__((noinline)) static void lfx_colour(uint32_t k, int32_t *b, uint32_t n)
{
    uint32_t i, beat = dly_samples(0) ? dly_samples(0) : 1u, on[4];
    int32_t open = 32 << 16, low = (lfx.tone * 14 / 127 + 4) << 16, down = (int32_t)((32u << 16) / beat);
    uint32_t root = (uint32_t)clamp(trk[0].p[P_ROOT], 0, 11), bq = clk_beat * 256u + clk_pos / (BEAT_U / 256u);
    int32_t per = LC_PER[root], fb = clamp(lfx.fb, 0, 110) * 290, amt = lfx.amt * 258;
    int32_t f1, f2, vf = (int32_t)(bq & 255u);
    uint32_t v = (bq >> 8) % 5u;
    typeof(lc[0]) *c = &lc[k % NTRK];
    for (i = 0; i < 4u; i++)
        on[i] = (lfx.col[i] >> k) & 1u;
    f1 = LC_V1[v] + (((LC_V1[(v + 1u) % 5u] - LC_V1[v]) * vf) >> 8);   /* (gliding to the next vowel over the beat) */
    f2 = LC_V2[v] + (((LC_V2[(v + 1u) % 5u] - LC_V2[v]) * vf) >> 8);
    if (!c->w[0])
        c->s = open;
    for (i = 0; i < n; i++) {
        int32_t x = b[i] >> 3, y, e;                    /* (a track's buffer runs to ~2^18: worked at ~2^15) */
        for (e = 0; e < 4; e++) {                       /* the weights: in and out over ~3 ms */
            int32_t to = on[e] || (e == 0 && c->s < open) ? 32767 : 0;
            c->w[e] += to > c->w[e] ? (to - c->w[e] > 256 ? 256 : to - c->w[e]) : -(c->w[e] - to > 256 ? 256 : c->w[e] - to);
        }
        if (c->w[0]) {                                  /* DUB FILTER: down over a beat (held), up in a quarter of one */
            int32_t idx, fc, hp;
            if (on[0])
                c->s = c->s - down > low ? c->s - down : low;
            else
                c->s = c->s + 4 * down < open ? c->s + 4 * down : open;
            idx = c->s >> 16;
            fc = idx >= 32 ? LC_DF[32] : LC_DF[idx] + (int32_t)(((int64_t)(LC_DF[idx + 1] - LC_DF[idx]) * (c->s & 0xFFFF)) >> 16);
            c->dl += m15(fc, c->db);
            hp = x - c->dl - m15(7209, c->db);          /* (q 0.22: it sings at the cutoff) */
            c->db += m15(fc, hp);
            y = clamp(c->dl, -60000, 60000);
            x += m15(y - x, c->w[0]);
        }
        if (c->w[2]) {                                  /* VOWEL: two band-passes, the formants */
            int32_t hp1, hp2;
            c->v1l += m15(f1, c->v1b);
            hp1 = x - c->v1l - m15(9830, c->v1b);
            c->v1b += m15(f1, hp1);
            c->v2l += m15(f2, c->v2b);
            hp2 = x - c->v2l - m15(9830, c->v2b);
            c->v2b += m15(f2, hp2);
            y = c->v1b + m15(c->v2b, 22000);
            x += m15(y + (y >> 1) - x, m15(c->w[2], amt));   /* (x1.5: a band-pass is quieter) */
        }
        if (c->w[3]) {                                  /* RESONATOR: a comb at the root, ringing */
            uint32_t d = (uint32_t)per >> 8, fr = (uint32_t)per & 255u;
            int32_t a = c->comb[(c->cw - d) & (LC_CN - 1u)], bb = c->comb[(c->cw - d - 1u) & (LC_CN - 1u)];
            int32_t dly = a + (((bb - a) * (int32_t)fr) >> 8), out;
            out = x + m15(dly, fb);
            c->comb[c->cw & (LC_CN - 1u)] = (int16_t)clamp(out, -32767, 32767);
            c->cw++;
            y = m15(out, 34000 - fb);
            x += m15(y - x, m15(c->w[3], amt));
        }
        if (c->w[1]) {                                  /* PSX CRUSH: held samples, fewer bits */
            uint32_t h = 1u + (uint32_t)lfx.amt * 7u / 127u, bits = 2u + (uint32_t)lfx.amt * 9u / 127u;
            if (c->hn <= 0) {
                c->hold = x & ~((1 << bits) - 1);
                c->hn = (int32_t)h;
            }
            c->hn--;
            x += m15(c->hold - x, c->w[1]);
        }
        b[i] = x << 3;
    }
}
