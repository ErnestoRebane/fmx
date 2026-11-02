/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Effects: per-track DIST insert, then sends into three shared buses (chorus, tempo delay, reverb).
 * Stereo dry mix; the chorus and the reverb come back in stereo, the delay in the middle. */
#define DLY_LEN 32768u           /* 0.74 s: 1/4 down to 81 BPM, 1/8 dotted down to 61 (clamped below); FMX: the
                                  * other half of SLOOP's line is the SPU reverb's work area */
#define CHO_LEN 2048u
static int16_t dly_buf[DLY_LEN] __attribute__((section(".pool")));
static int16_t cho_buf[CHO_LEN] __attribute__((section(".pool")));
#ifndef FX_ENSEMBLE
#define FX_ENSEMBLE 1            /* the chorus bus as a string machine's ensemble (after Plaits' ensemble.h, (c) 2014 Emilie
                                  * Gillet, MIT): three taps a third of a turn apart, each moved by a slow LFO (CRT) and a
                                  * fast one 8.75 x faster at a tenth of the depth; left, centre, right. Chosen by ear on
                                  * the pads (tests/ensemble_proto.sh); 0 = SLOOP's two-tap chorus */
#endif
#if FX_ENSEMBLE
#define ENS_MID (176 << 8)       /* the taps' centre, Q8 samples (Plaits' 192 at 48 kHz: 4 ms) */
static uint32_t ens_slow, ens_fast;
#endif
/* FMX: the reverb of a PlayStation's sound chip (its topology as psx-spx describes the SPU's reverb, run at
 * 22.05 kHz like the chip; the presets are ours, by ear, not Sony's register tables). Per side:
 *   same-side reflection  Ls = (in + WALL x Ls[-ds] - Ls[-1]) x IIR + Ls[-1]   (a low-passed feedback line)
 *   other-side reflection Ld = (in + WALL x Rd[-dd] - Ld[-1]) x IIR + Ld[-1]   (fed by the other side's: the width)
 *   early echo            out = C1 Ls[-t1] + C2 Ld[-t2] + C3 Ls[-t3] + C4 Ld[-t4]
 *   late reverb           two all-pass filters (APF1, APF2) on out
 * The work area: four reflection lines of SPU_LINE and four all-pass lines of SPU_APL, 64 KB (.pool). G_RSIZE picks
 * the preset (ROOM .. DELAY), G_RDAMP darkens the reflections (the IIR) */
#define SPU_LINE 6400u           /* 290 ms at 22.05 kHz */
#define SPU_APL 1792u            /* 81 ms */
static int16_t spu_buf[4u * SPU_LINE + 4u * SPU_APL] __attribute__((section(".pool")));
typedef struct {
    uint16_t ds[2], dd[2];       /* the reflections' feedback delays (L, R), samples at 22.05 kHz */
    uint16_t t[2][4];            /* the early echo's taps (L, R): 0, 2 on the same-side line, 1, 3 on the other */
    uint16_t a1[2], a2[2];       /* the all-passes' delays */
    int16_t iir, wall, c[4], apf1, apf2, out;   /* Q15 */
} spu_rev_t;
enum { SPU_ROOM, SPU_STUDIO, SPU_HALL, SPU_SPACE, SPU_ECHO, SPU_DELAY, SPU_N };
static const spu_rev_t SPU_REV[SPU_N] = {
    /* ROOM: 35 ms, small and bright */
    {{780, 700}, {735, 820}, {{700, 650, 540, 470}, {660, 610, 520, 450}}, {260, 240}, {200, 180},
     27500, -18350, {21600, -17000, 17400, -16000}, 22280, 20300, 19000},
    /* STUDIO: 65 ms */
    {{1450, 1320}, {1390, 1500}, {{1300, 1180, 1020, 900}, {1250, 1120, 980, 860}}, {440, 400}, {330, 300},
     28200, -19660, {21600, -17000, 17400, -16000}, 22280, 20300, 18000},
    /* HALL: 175 ms, a long tail, a little darker */
    {{3900, 3620}, {3760, 4050}, {{3600, 3250, 2850, 2450}, {3480, 3150, 2750, 2350}}, {900, 840}, {700, 640},
     27000, -23600, {21000, -18000, 16400, -15000}, 21300, 19700, 16000},
    /* SPACE: 290 ms, the echoes heard one by one, washing out */
    {{6300, 6200}, {6250, 6350}, {{6200, 5600, 4400, 3300}, {6100, 5500, 4300, 3200}}, {1700, 1600}, {1300, 1200},
     24500, -21000, {20300, -19660, 15700, -13100}, 19660, 18000, 15000},
    /* ECHO: 82 ms repeats, little diffusion */
    {{1800, 1700}, {1750, 1850}, {{1790, 1740, 0, 0}, {1690, 1840, 0, 0}}, {100, 90}, {70, 60},
     29500, -24500, {26200, -9800, 0, 0}, 9800, 6500, 18000},
    /* DELAY: one line, 290 ms, no diffusion */
    {{6390, 6390}, {6390, 6390}, {{6390, 0, 0, 0}, {6390, 0, 0, 0}}, {1, 1}, {1, 1},
     30500, -21300, {26200, 0, 0, 0}, 0, 0, 18000},
};
static struct {
    uint32_t dly_w, cho_w, cho_ph;
    int32_t dly_lp;
    uint16_t wl, wa;             /* the SPU's write positions: in the reflection lines, in the all-pass lines */
    uint8_t half;                /* 22.05 kHz: every other sample */
    uint32_t hush;               /* fx_hush: the samples of the lines emptied so far */
    int32_t in, ol[2], orr[2];   /* the pair's input; the last two outputs (L, R): the samples between them */
    int32_t ls[2], ld[2];        /* the reflections' last values (the IIR's) */
    int32_t hp[2][2];            /* the chorus' and the reverb's send low cuts (lowcut_sh) */
} fx;
/* line k (0 Ls, 1 Rs, 2 Ld, 3 Rd), d (< SPU_LINE) samples back from the write position; all-pass line k likewise */
static inline int16_t *spu_line(uint32_t k, uint32_t d)
{
    uint32_t i = fx.wl >= d ? fx.wl - d : fx.wl + SPU_LINE - d;
    return &spu_buf[k * SPU_LINE + i];
}
static inline int16_t *spu_apl(uint32_t k, uint32_t d)
{
    uint32_t i = fx.wa >= d ? fx.wa - d : fx.wa + SPU_APL - d;
    return &spu_buf[4u * SPU_LINE + k * SPU_APL + i];
}
/* a x b, Q15, rounded toward 0 (a feedback loop of these only loses energy: no DC creeping in, no low-level ringing
 * left after the tail, as mulq15's rounding down would leave); a kept inside 17 bits (no 32-bit overflow) */
static inline int32_t mz(int32_t a, int32_t b)
{
    int32_t p = clamp(a, -65535, 65535) * b;
    return p >= 0 ? p >> 15 : -((-p) >> 15);
}
/* one SPU tick (22.05 kHz): in, the mono send; out *l, *r */
static void spu_tick(int32_t in, int32_t *l, int32_t *r)
{
    const spu_rev_t *v = &SPU_REV[(uint32_t)song.g[G_RSIZE] % SPU_N];
    int32_t iir = v->iir - song.g[G_RDAMP] * 64, k, o[2];
    for (k = 0; k < 2; k++) {                          /* the reflections */
        int32_t ws = *spu_line((uint32_t)k, v->ds[k]), wd = *spu_line(3u - (uint32_t)k, v->dd[k]);
        fx.ls[k] += mz(in + mz(ws, v->wall) - fx.ls[k], iir);
        fx.ld[k] += mz(in + mz(wd, v->wall) - fx.ld[k], iir);
    }
    for (k = 0; k < 2; k++) {                          /* (written after both read: L's other side is R's of this tick) */
        *spu_line((uint32_t)k, 0) = (int16_t)clamp(fx.ls[k], -32768, 32767);
        *spu_line(2u + (uint32_t)k, 0) = (int16_t)clamp(fx.ld[k], -32768, 32767);
    }
    for (k = 0; k < 2; k++) {                          /* the early echo, then the two all-passes */
        const uint16_t *t = v->t[k];
        int32_t x = mz(*spu_line((uint32_t)k, t[0]), v->c[0]) + mz(*spu_line(2u + (uint32_t)k, t[1]), v->c[1]) +
                    mz(*spu_line((uint32_t)k, t[2]), v->c[2]) + mz(*spu_line(2u + (uint32_t)k, t[3]), v->c[3]);
        int32_t a = *spu_apl((uint32_t)k, v->a1[k]), b;
        x -= mz(a, v->apf1);
        *spu_apl((uint32_t)k, 0) = (int16_t)clamp(x, -32768, 32767);
        x = mz(x, v->apf1) + a;
        b = *spu_apl(2u + (uint32_t)k, v->a2[k]);
        x -= mz(b, v->apf2);
        *spu_apl(2u + (uint32_t)k, 0) = (int16_t)clamp(x, -32768, 32767);
        x = mz(x, v->apf2) + b;
        o[k] = mz(x, v->out);
    }
    fx.wl = (uint16_t)(fx.wl + 1u >= SPU_LINE ? 0u : fx.wl + 1u);
    fx.wa = (uint16_t)(fx.wa + 1u >= SPU_APL ? 0u : fx.wa + 1u);
    *l = o[0];
    *r = o[1];
}

/* DIST: low cut -> drive (1x..8x, exponential) -> asymmetric soft clip
 * (a little bias = even harmonics) -> tone low-pass that closes with drive ->
 * make-up gain. State per part (track_t dist_*). */
static void track_dist(track_t *t, int32_t *b, uint32_t n)
{
    int32_t d = t->p[P_DIST], i, g, k, mk, bias = 2400, b0;
    if (!d) {
        t->dist_on = 0;
        return;
    }
    if (!t->dist_on) {                                  /* coming on: no stale high-pass state (a thump) */
        t->dist_on = 1;
        t->dist_hp = b[0];
        t->dist_lp1 = t->dist_lp2 = 0;
    }
    g = 4096 + d * d * 2;                                /* Q12: 1x .. ~9x, gentle at first */
    k = 32000 - d * 95;                                  /* tone: transparent at low drive .. ~3 kHz, Q15 */
    mk = 30000 - d * 120;                                /* make-up */
    b0 = softclip(bias);
    for (i = 0; i < (int32_t)n; i++) {
        int32_t x = b[i], y;
        t->dist_hp += (x - t->dist_hp + 64) >> 7;           /* ~55 Hz low cut: keep the bass out of the clipper */
        x = clamp(x - t->dist_hp, -230000, 230000);         /* (x >> 2) * g fits 32 bits; the clip is flat out there */
        y = softclip((((x >> 2) * g) >> 10) + bias) - b0;   /* >> 2 first: no overflow for loud poly */
        t->dist_lp1 += mulq15(y - t->dist_lp1, k);         /* two poles: tames the fizz */
        t->dist_lp2 += mulq15(t->dist_lp1 - t->dist_lp2, k);
        b[i] = mulq15(t->dist_lp2, mk);
    }
}

static volatile uint8_t fx_lowcut;     /* settings: 12 dB/oct ~110 Hz for the small speaker */
static int32_t lc_l1, lc_l2, lc_r1, lc_r2, dc_l, dc_r, dce_l, dce_r;

/* DC blocker (~2 Hz), always on: a leaky integrator of the input (Q6 state) subtracted from it.
 * The >> 12 step keeps its remainder (error feedback, 0..4095) and adds it to the next one, so no
 * part of the step is lost: the state follows the input exactly, down to 0 after the sound stops.
 * (It was rounded, and a rounded step of (x - dc) / 4096 stops moving at |x - dc| < 2048: a
 * constant offset of up to +-31 stayed at the output after silence.) */
static inline int32_t dc_block(int32_t x, int32_t *dc, int32_t *err)
{
    int32_t e = (x << 6) - *dc + *err, d = e >> 12;
    *err = e - (d << 12);
    *dc += d;
    return x - ((*dc + 32) >> 6);
}

static int32_t lce[4];
static inline int32_t lowcut1(int32_t x, int32_t *lc, int32_t *err)   /* x minus its one-pole low-pass */
{
    int32_t e = x - *lc + *err, d = e >> 6;
    *err = e - (d << 6);
    *lc += d;
    return x - *lc;
}
/* the same high-pass at another corner: x minus its one-pole low-pass of 1/2^sh (sh 5 ~220 Hz, 6 ~110, 7 ~55,
 * 8 ~27); st[0] the low-pass, st[1] the step's remainder (error feedback: no offset left after silence) */
static inline int32_t lowcut_sh(int32_t x, int32_t *st, uint32_t sh)
{
    int32_t e = x - st[0] + st[1], d = e >> sh;
    st[1] = e - (d << sh);
    st[0] += d;
    return x - st[0];
}

/* the USB audio input at full level (menu USB AUDIO = FULL): the limited mix before the knob, so the recording
 * level does not follow it. audio.c sets usb_full_now for a render while the computer records. */
static uint8_t usb_full;                 /* menu USB AUDIO: 0 MASTER (follows the knob), 1 FULL (panel.c lights_word) */
static uint8_t usb_full_now;             /* this render fills usb_out (audio.c) */
static int32_t usb_out[2u * CTL];
/* the visualiser's input (audio.c fills the scope with it): the limited mix before the knob, so the picture
 * does not follow the volume knob, even at 0 (2.4) */
static int16_t vis_tap[2u * CTL];

/* the master (2026-10-25, the full-mix audit): the mix at a fixed drive (-4 dB) -> the DC blocker (and the small
 * speaker's low cut) -> a limiter that sees one block ahead -> the ceiling (-1 dBFS) -> the knob. The knob comes after
 * the limiter: turning it up no longer drives the limiter harder (it used to: the top half of the knob only flattened
 * the song, a DROP no louder than its INTRO). The limiter: the output is the block before (0.73 ms late); its gain is
 * ramped linearly over that block to the smaller of what it and the block after need, so no sample passes the ceiling
 * and the gain never jumps (one divide a block). Release: a fast follower (~45 ms) for a lone peak, a slow one (falls
 * in ~25 ms, back in ~370 ms) for a held level, the smaller of the two: a snare passes without pumping the pads */
#define M_DRIVE 2580                     /* Q12: -4 dB */
#define M_CEIL 29200                     /* -1 dBFS */
static struct {
    int32_t l[2][CTL], r[2][CTL];        /* this block (into [w]) and the block before, waiting (in [w ^ 1]) */
    int32_t g, gf, gs, need;             /* gain now (Q15), the fast and slow followers, the last block's need */
    uint32_t w;
} lim = {{{0}}, {{0}}, 32767, 32767, 32767, 32767, 0};
static int32_t master_cur = -1;          /* the volume knob, ramped per sample (no zipper) */
static void master_out(int32_t *ml, int32_t *mr, int32_t *out, uint32_t n)
{
    uint32_t i;
    int32_t pk = 0, need, tgt, g0 = lim.g, g1, k0, k1;
    int32_t *nl = lim.l[lim.w], *nr = lim.r[lim.w], *ol = lim.l[lim.w ^ 1u], *or_ = lim.r[lim.w ^ 1u];
    lim.w ^= 1u;                          /* (this block waits for the next) */
    for (i = 0; i < n; i++) {
        int32_t l = ((ml[i] >> 2) * M_DRIVE) >> 10, r = ((mr[i] >> 2) * M_DRIVE) >> 10, a;
        l = dc_block(l, &dc_l, &dce_l);
        r = dc_block(r, &dc_r, &dce_r);
        if (fx_lowcut) {                  /* two one-pole high-passes, error feedback as dc_block (the */
            l = lowcut1(l, &lc_l1, &lce[0]);            /* rounded step stopped at |x - lc| < 32: an offset) */
            l = lowcut1(l, &lc_l2, &lce[1]);
            r = lowcut1(r, &lc_r1, &lce[2]);
            r = lowcut1(r, &lc_r2, &lce[3]);
        }
        nl[i] = l;
        nr[i] = r;
        a = l < 0 ? -l : l;
        if (a > pk)
            pk = a;
        a = r < 0 ? -r : r;
        if (a > pk)
            pk = a;
    }
    need = pk > M_CEIL ? (int32_t)(((uint32_t)M_CEIL << 15) / (uint32_t)pk) : 32767;
    tgt = need < lim.need ? need : lim.need;
    lim.need = need;
    lim.gf = tgt < lim.gf ? tgt : lim.gf + ((32767 - lim.gf) >> 6);
    lim.gs += tgt < lim.gs ? (tgt - lim.gs) >> 5 : (32767 - lim.gs) >> 9;
    g1 = lim.gf < lim.gs ? lim.gf : lim.gs;
    lim.g = g1 = g1 < tgt ? g1 : tgt;
    k1 = (int32_t)(((uint32_t)song.master_q12 * as_fade) >> 12);   /* (AUTO SONG's OUTRO fades the mix) */
    k0 = master_cur < 0 ? k1 : master_cur;
    master_cur = k1;
    for (i = 0; i < n; i++) {
        int32_t l = ol[i], r = or_[i], k = k0 + (((k1 - k0) * (int32_t)i) >> CTL_LOG2);
        if (g0 < 32767 || g1 < 32767) {
            int32_t g = g0 + (((g1 - g0) * (int32_t)i) >> CTL_LOG2);
            l = ((l >> 4) * g) >> 11;     /* (>> 4 first: |l| may pass Q15 before the gain) */
            r = ((r >> 4) * g) >> 11;
        }
        l = clamp(l, -32767, 32767);
        r = clamp(r, -32767, 32767);
        if (i & 1u) {                     /* the visualiser: every other sample, as audio.c reads it */
            vis_tap[2u * i] = (int16_t)l;
            vis_tap[2u * i + 1u] = (int16_t)r;
        }
        if (usb_full_now) {               /* USB AUDIO = FULL: before the knob */
            usb_out[2u * i] = l;
            usb_out[2u * i + 1u] = r;
        }
        out[2u * i] = (l * k) >> 12;
        out[2u * i + 1u] = (r * k) >> 12;
    }
}

/* the mix in the master's eyes (2026-10-25): a track's level for its role, Q12 - the pads sit 5 dB under the break and
 * the bass, the chops level with them (2026-10-26: 1 dB under, they were lost in the mix; their kit / loop trim +8 dB
 * too: preset_trim.h CHOP KIT); its low cut (lowcut_sh's sh, 0 none; CHORDS twice: 12 dB/oct) - the pads off the bass and
 * the kick (~110 Hz), the chops ~55 Hz, the break only its rumble (~27 Hz). Index: the track (BASS, CHORDS, CHOP,
 * BREAK) */
static const int16_t ROLE_Q12[NTRK] = {4096, 2300, 4096, 4096};
static const uint8_t ROLE_CUT[NTRK] = {0, 6, 7, 8};
static int32_t role_lc[NTRK][2][2];
/* the BREAK's peaks into a tanh knee, as a sampler's converter or a desk clipped them: linear up to BRK_T, above it
 * BRK_T + BRK_R tanh((a - BRK_T) / BRK_R) - about 4 dB off a loud loop's peaks, the body untouched; the master's
 * limiter then rests */
#define BRK_T 20000
static inline int32_t brk_clip(int32_t x)
{
    int32_t a = x < 0 ? -x : x, u;
    if (a <= BRK_T)
        return x;
    u = a - BRK_T < 49152 ? a - BRK_T : 49152;            /* (softclip is flat from 65536 = 49152 x 4 / 3) */
    a = BRK_T + ((softclip((u * 21845) >> 14) * 24000) >> 15);   /* R = 24000: x 4 / 3 in, x 24000 / 32768 out */
    return x < 0 ? -a : a;
}



#include "lfx.c"             /* the FX panel (FX held, LIVE and STUDIO): its throws, per track */

static uint32_t delay_samples(void)
{
    uint32_t s = dly_samples((uint32_t)song.g[G_DTIME]);
    return s < 16u ? 16u : s >= DLY_LEN ? DLY_LEN - 1u : s;
}

#if FX_ENSEMBLE
static inline int32_t cho_tap(int32_t r)               /* the line r (Q8 samples) back, linearly interpolated */
{
    uint32_t i = (uint32_t)r >> 8;
    int32_t c0 = cho_buf[(fx.cho_w - i) & (CHO_LEN - 1u)], c1 = cho_buf[(fx.cho_w - i - 1u) & (CHO_LEN - 1u)];
    return c0 + (((c1 - c0) * (r & 255)) >> 8);
}
#endif

/* process the three buses for one block; sends in, wet out (stereo). The LFOs (chorus, reverb line)
 * are computed per block and ramped: no sine per sample */
static void fx_buses(const int32_t *cho_in, const int32_t *dly_in, const int32_t *rev_in, int32_t *wet_l,
                     int32_t *wet_r, uint32_t n)
{
    uint32_t i, dl = delay_samples();
    int32_t fb = song.g[G_DFDBK] * 230, col = 2000 + song.g[G_DCOLOR] * 240;
    if (fx_hush) {                                      /* AUTO SONG's gap before a drop: no wet, nothing in; the lines
                                                         * emptied a piece a block (~0.1 s for all), the states cleared */
        static int16_t *const LN[3] = {dly_buf, cho_buf, spu_buf};
        static const uint32_t LL[3] = {DLY_LEN, CHO_LEN, sizeof spu_buf / sizeof spu_buf[0]};
        uint32_t left = 512u, k, at = fx.hush;
        for (k = 0; k < 3u && left; k++) {
            if (at >= LL[k]) {
                at -= LL[k];
                continue;
            }
            uint32_t m = LL[k] - at < left ? LL[k] - at : left;
            memset(LN[k] + at, 0, m * sizeof(int16_t));
            fx.hush += m;
            left -= m;
            at = 0;
        }
        fx.dly_lp = fx.in = 0;
        fx.ol[0] = fx.ol[1] = fx.orr[0] = fx.orr[1] = fx.ls[0] = fx.ls[1] = fx.ld[0] = fx.ld[1] = 0;
        for (i = 0; i < n; i++)
            wet_l[i] = wet_r[i] = 0;
        return;
    }
    fx.hush = 0;
    int32_t dmix = song.g[G_DMIX] * 258;
    int32_t cdepth = song.g[G_CDEPTH] * 6;
    int32_t rgain = song.g[G_SPACE] == 64 ? 13000 : 13000 * clamp(song.g[G_SPACE], 0, 127) / 64;   /* SPACE (DECK) */
    if (perf_verb)                                      /* the moves: VERB, WASH x3 (punch.c) */
        rgain = clamp(rgain * (1 + perf_verb), 0, 32767);
    int32_t ca0, ca1, cb0, cb1, dca, dcb;
    {   /* the chorus' two read points (Q8 samples back), at both ends of the block */
        int32_t s0 = osc_sine(fx.cho_ph), s1;
        fx.cho_ph += LFO_INC[song.g[G_CRATE] & 127];
        s1 = osc_sine(fx.cho_ph);
        ca0 = (400 << 8) + ((s0 + 32768) * cdepth >> 8), ca1 = (400 << 8) + ((s1 + 32768) * cdepth >> 8);
        cb0 = (400 << 8) + ((32767 - s0) * cdepth >> 8), cb1 = (400 << 8) + ((32767 - s1) * cdepth >> 8);
        dca = (ca1 - ca0) >> CTL_LOG2, dcb = (cb1 - cb0) >> CTL_LOG2;
    }
#if FX_ENSEMBLE
    int32_t em[3], dem[3];
    {   /* the three taps' offsets from ENS_MID (Q8 samples), at both ends of the block: depth up to 147 samples slow,
         * a tenth of it fast */
        int32_t a = song.g[G_CDEPTH] * 296, b = (a * 6554) >> 16, k, e0[3];
        uint32_t inc = LFO_INC[song.g[G_CRATE] & 127];
        for (k = 0; k < 3; k++)
            e0[k] = (int32_t)(((int64_t)osc_sine(ens_slow + (uint32_t)k * 0x55555555u) * a +
                               (int64_t)osc_sine(ens_fast + (uint32_t)k * 0x55555555u) * b) >> 15);
        ens_slow += inc;
        ens_fast += (inc >> 2) * 35u;                   /* 8.75 x: Plaits' 0.75 and 6.57 Hz */
        for (k = 0; k < 3; k++) {
            int32_t e1 = (int32_t)(((int64_t)osc_sine(ens_slow + (uint32_t)k * 0x55555555u) * a +
                                    (int64_t)osc_sine(ens_fast + (uint32_t)k * 0x55555555u) * b) >> 15);
            em[k] = e0[k];
            dem[k] = (e1 - e0[k]) >> CTL_LOG2;
        }
    }
#endif
    for (i = 0; i < n; i++) {
        int32_t yl, yr, x, a, rl, rr;
        int32_t ci = lowcut_sh(cho_in[i], fx.hp[0], 5), ri = lowcut_sh(rev_in[i], fx.hp[1], 5);   /* the chorus' and the
                                                         * reverb's sends from ~220 Hz up: the sub stays mono, dry */
        /* chorus: two modulated short delays, 5..15 ms, the LFO half a turn apart: left and right move apart */
        cho_buf[fx.cho_w & (CHO_LEN - 1u)] = (int16_t)clamp(ci >> 1, -32768, 32767);
#if FX_ENSEMBLE
        {   /* left, centre, right: tap 0 on the left, tap 2 on the right, tap 1 half on both (x 0.96: the old level
             * on the pads' chords, by render) */
            int32_t t1 = cho_tap(ENS_MID + em[1] + dem[1] * (int32_t)i);
            yl = ((2 * cho_tap(ENS_MID + em[0] + dem[0] * (int32_t)i) + t1) * 31615) >> 15;
            yr = ((2 * cho_tap(ENS_MID + em[2] + dem[2] * (int32_t)i) + t1) * 31615) >> 15;
        }
        if (0)
#endif
        {
            int32_t r0 = ca0 + dca * (int32_t)i, r1 = cb0 + dcb * (int32_t)i;
            uint32_t i0 = (uint32_t)r0 >> 8, i1 = (uint32_t)r1 >> 8;
            int32_t c0 = cho_buf[(fx.cho_w - i0) & (CHO_LEN - 1u)], c1 = cho_buf[(fx.cho_w - i0 - 1u) & (CHO_LEN - 1u)];
            int32_t d0 = cho_buf[(fx.cho_w - i1) & (CHO_LEN - 1u)], d1 = cho_buf[(fx.cho_w - i1 - 1u) & (CHO_LEN - 1u)];
            yl = (c0 + (((c1 - c0) * (r0 & 255)) >> 8)) << 1;
            yr = (d0 + (((d1 - d0) * (r1 & 255)) >> 8)) << 1;
        }
        fx.cho_w++;
        /* delay with a low-passed feedback (in the middle) */
        x = dly_buf[(fx.dly_w - dl) & (DLY_LEN - 1u)];
        fx.dly_lp += mulq15(x - fx.dly_lp, col);
        dly_buf[fx.dly_w & (DLY_LEN - 1u)] =
            (int16_t)clamp((dly_in[i] >> 1) + mulq15(fx.dly_lp, fb), -32768, 32767);
        fx.dly_w++;
        x = mulq15(x << 1, dmix);
        yl += x;
        yr += x;
        /* reverb: the SPU's, at half the rate: the pair's input averaged, its output between the last two */
        a = mulq15(ri, rgain);
        if (!fx.half) {
            fx.in = a;
        } else {
            fx.ol[0] = fx.ol[1];
            fx.orr[0] = fx.orr[1];
            spu_tick((fx.in + a) >> 1, &fx.ol[1], &fx.orr[1]);
        }
        fx.half ^= 1u;
        rl = fx.half ? fx.ol[1] : (fx.ol[0] + fx.ol[1]) >> 1;
        rr = fx.half ? fx.orr[1] : (fx.orr[0] + fx.orr[1]) >> 1;
        wet_l[i] = yl + (rl << 1);
        wet_r[i] = yr + (rr << 1);
    }
}

/* one block of the whole mix (shared with tests/hostsim.c): events -> each part
 * (the BREAK track too) -> dist -> SLICER -> FILTER -> level / pan / sends -> buses -> master; out: stereo Q15.
 * The four tracks are render units (mix_units below), each adding into an accumulator set: the dry mix and the three
 * sends. Core 0 renders its units into acc[0] and core 1 (cpu1.h, after SLOOP 2.7.1) its share into acc[1], added
 * before the buses: integer sums commute, so the output is the same, bit for bit, whatever the split
 * (tests/mix_split_test.c and tests/cpu1_modes.sh: the golden renders under every mode of the host double) */
static void events_block(uint32_t n);                    /* seq.c */
typedef struct { int32_t l[CTL], r[CTL], sc[CTL], sd[CTL], sr[CTL], sx[CTL], sk[CTL], tl[CTL], tr[CTL]; } mixacc_t;
/* (dry L, R; chorus, delay, reverb sends; LIVE's DUB / VOID and SPARKLE throws; the TIME stem's dry L, R) */
static mixacc_t acc[2] __attribute__((section(".lowram")));          /* core 0's, core 1's (.lowram: app.ld, main.c) */
static int32_t wet_l[CTL], wet_r[CTL];
static int32_t part_buf[2][CTL] __attribute__((section(".lowram")));   /* a part's scratch, one per core */

/* ---- mute / solo: a track that goes silent fades out over ~6 ms (and back in) */
#define MUTE_STEP 4096                                  /* Q15 per block: 8 blocks */
static int32_t gain_next(track_t *t)                    /* the track's mute gain at the end of this block */
{
    int32_t to = trk_silent(t) ? 32767 : 0, a = t->att;
    t->att = a < to ? (to - a > MUTE_STEP ? a + MUTE_STEP : to) : (a - to > MUTE_STEP ? a - MUTE_STEP : to);
    return 32767 - t->att;
}

/* ---- DUCK: every kick (slice 1 of the BREAK track's loop, its downbeat) dips the synth parts, which come back
 * over an eighth note: depth G_DUCK, the curve (1 - t / T)^2. brkpart.c sets bp.kick. */
static struct {
    uint32_t t;                                         /* units since the kick */
    int32_t g0, g1;                                     /* the parts' gain at the block start, end (Q15) */
} duck = {0xFFFFFFFFu, 32767, 32767};

static void duck_block(uint32_t adv)
{
    int32_t depth = song.g[G_DUCK] * 258, x;
    uint32_t len = BEAT_U / 2u;
    duck.g0 = duck.g1;
    if (bp.kick) {
        bp.kick = 0;
        duck.t = 0;
    }
    if (!depth || duck.t >= len) {
        duck.g1 = 32767;
        return;
    }
    x = 32767 - (int32_t)((duck.t << 10) / (len >> 5));      /* 1 - t / T, Q15 (t < len < 2^21) */
    if (x < 0)
        x = 0;
    duck.g1 = 32767 - mulq15(depth, mulq15(x, x));
    duck.t = duck.t + adv < duck.t ? 0xFFFFFFFFu : duck.t + adv;
}

/* ---- the DJ filter: v < 0 a low-pass closing, > 0 a high-pass opening, 0 off. The cutoff glides
 * to the knob (no zipper); at 0 it opens fully, then the filter is bypassed. On the master (G_FILT)
 * and, SLOOP 2.4, on each track (P_TFLT: its mono signal) */
typedef struct {
    int32_t cut;                                        /* now, 0..127 << 8 (CUTOFF_HZ index) */
    int8_t mode;                                        /* -1 LP, 1 HP, 0 off */
    uint8_t steep;                                      /* HP: the true 12 dB/oct one (AUTO SONG's BUILD: arranger_scene.c) */
    int32_t z[4][2];                                    /* the SVF states, per channel */
} djf_t;
static djf_t djf, tflt[NTRK];

/* the knob v -> this block's coefficients in *c; 0 = bypassed (nothing to do) */
static int djf_block(djf_t *f, int32_t v, tsvf_t *c)
{
    int32_t to;
    if (v < 0 && f->mode >= 0) {                        /* (switching side: from open) */
        f->mode = -1;
        f->cut = 127 << 8;
        memset(f->z, 0, sizeof f->z);
    } else if (v > 0 && f->mode <= 0) {
        f->mode = 1;
        f->cut = 0;
        memset(f->z, 0, sizeof f->z);
    }
    if (!f->mode)
        return 0;
    to = f->mode < 0 ? (v < 0 ? (127 << 8) + v * 90 * 4 : 127 << 8) : (v > 0 ? v * 90 * 4 : 0);
    f->cut += clamp(to - f->cut, -384, 384);            /* ~1.5 index a block */
    if (!v && f->cut == to) {
        f->mode = 0;                                    /* fully open again: off */
        return 0;
    }
    tsvf_coef(c, f->cut, 40);
    return 1;
}
/* n samples of channel ch through the filter (x within +-140000) */
static void djf_run(djf_t *f, const tsvf_t *c, int32_t *b, uint32_t n, uint32_t ch)
{
    uint32_t i;
    int32_t *z = f->z[ch];
    if (f->mode > 0 && f->steep) {                      /* (reso 40, as djf_block's: damping 8192 - 40 x 7600 / 127) */
        for (i = 0; i < n; i++)
            b[i] = tsvf_hp(c, 8192 - 40 * 7600 / 127, clamp(b[i], -140000, 140000), &z[0], &z[1]);
        return;
    }
    for (i = 0; i < n; i++) {
        int32_t x = clamp(b[i], -140000, 140000), y = tsvf_lp(c, x, &z[0], &z[1]);
        b[i] = f->mode < 0 ? y : x - y;
    }
}
/* a track's filter on a signal at 4x its level (b >> 2 into the filter's range, back << 2) */
static void tflt_run(djf_t *f, const tsvf_t *c, int32_t *b, uint32_t n, uint32_t ch)
{
    uint32_t i;
    for (i = 0; i < n; i++)
        b[i] >>= 2;
    djf_run(f, c, b, n, ch);
    for (i = 0; i < n; i++)
        b[i] <<= 2;
}

/* one synth part into the dry mix and the sends; a part with no voice sounding costs
 * the LFO tick and a cleared buffer only (after the DIST tail has run out) */
static void mix_part(track_t *t, uint32_t n, mixacc_t *mx, int32_t *b)
{
    uint32_t i;
    int32_t g0 = 32767 - t->att, g1 = gain_next(t);
    if (track_render(t, b, n))
        t->tail = 16;                                   /* blocks of DIST state to run out after the last voice */
    else if ((!t->tail || !t->p[P_DIST] || !--t->tail) && !slicer_busy(t)) {
        slicer_track(t, 0, n);                          /* (the SLICER's step clock runs on) */
        return;
    }
    if (!g0 && !g1) {                                   /* silent (MUTE / SOLO): the voices run, nothing is heard */
        slicer_track(t, 0, n);
        return;
    }
    {
        int32_t lvl = LEVEL_Q12[t->p[P_LEVEL] ? clamp(t->p[P_LEVEL] + t->p[P_ED_FX], 1, 127) : 0], pan = t->p[P_PAN];   /* (+ the sound's trim: 1/2 dB steps, as LEVEL's) */
        int32_t gl = 4096 - (pan > 0 ? pan * 64 : 0), gr = 4096 + (pan < 0 ? pan * 64 : 0);
        int32_t c = t->p[P_CHOR] * 258, d = t->p[P_DLY] * 258, r = t->p[P_REV] * 258, pk = t->peak;
        uint32_t tk = (uint32_t)(t - trk) % NTRK;      /* LIVE's throws on this track (lfx.c), ramped over the block */
        int32_t xr0 = lfx.rev[0][tk], dxr = (lfx.rev[1][tk] - xr0) >> CTL_LOG2;
        int32_t xk0 = lfx.spk[0][tk], dxk = (lfx.spk[1][tk] - xk0) >> CTL_LOG2;
        int32_t xd0 = lfx.dly[0][tk], dxd = (lfx.dly[1][tk] - xd0) >> CTL_LOG2;
        int32_t dr0 = lfx.dry[0][tk], ddr = (lfx.dry[1][tk] - dr0) >> CTL_LOG2;
        int32_t thrown = xr0 | lfx.rev[1][tk] | xk0 | lfx.spk[1][tk] | xd0 | lfx.dly[1][tk];
        int32_t *dst_l = (lfx.ins >> tk) & 1u ? mx->tl : mx->l, *dst_r = (lfx.ins >> tk) & 1u ? mx->tr : mx->r;   /* (TIME:
                                                         * its dry into the stem) */
        int32_t xmax = c > d ? c : d;
        if (thrown)
            xmax = 32767;
        int32_t ga = tk ? g0 : mulq15(g0, duck.g0), gb = tk ? g1 : mulq15(g1, duck.g1);   /* mute x duck: the BASS
                                                         * only (the kick and the sub; the pads and chops float), ramped */
        uint32_t cut = ROLE_CUT[tk];
        int32_t (*lc)[2] = role_lc[tk];
        xmax = 0x7FFFFFFF / ((xmax > r ? xmax : r) | 1);   /* sends: loud chords at a high LEVEL */
        track_dist(t, b, n);
        slicer_track(t, b, n);                          /* slicer.c: before the level, pan and sends */
        {
            tsvf_t fc;                                  /* the track's FILTER (P_TFLT), after the SLICER */
            djf_t *f = &tflt[(uint32_t)(t - trk) % NTRK];
            if (djf_block(f, t->p[P_TFLT], &fc))
                tflt_run(f, &fc, b, n, 0);
        }
        if ((lfx.colact >> ((uint32_t)(t - trk) % NTRK)) & 1u)
            lfx_colour((uint32_t)(t - trk) % NTRK, b, n);   /* LIVE's COLOUR on this track (lfx.c) */
        lvl = (lvl * ROLE_Q12[tk]) >> 12;               /* (its role's level: ROLE_Q12) */
        int32_t lvl0 = t->lvl ? t->lvl : lvl, dl = (lvl - lvl0) >> CTL_LOG2;   /* a new sound's trim: ramped */
        t->lvl = lvl;
        for (i = 0; i < n; i++) {
            int32_t x = ((b[i] >> 2) * (lvl0 + dl * (int32_t)i)) >> 10, a;   /* pre-shift: 8 loud voices */
            int32_t xs, g = ga + (((gb - ga) * (int32_t)i) >> CTL_LOG2);
            if (cut) {                                  /* its role's low cut (ROLE_CUT) */
                x = lowcut_sh(x, lc[0], cut);
                if (tk == 1u)
                    x = lowcut_sh(x, lc[1], cut);
            }
            if (tk == TRK_BRK)
                x = brk_clip(x);
            if (g < 32767)
                x = (x >> 4) * (g >> 3) >> 8;           /* (Q15 in two halves: no 32-bit overflow) */
            a = x < 0 ? -x : x;
            xs = clamp(x, -xmax, xmax);                 /* sends: mulq15 would overflow */
            if (a > pk)
                pk = a;
            if (c)
                mx->sc[i] += mulq15(xs, c);
            if (d)
                mx->sd[i] += mulq15(xs, d);
            if (r)
                mx->sr[i] += mulq15(xs, r);
            if (thrown) {                               /* (LIVE: its throws; VOID: its dry out, the sends on) */
                int32_t ii = (int32_t)i;
                mx->sx[i] += mulq15(xs, xr0 + dxr * ii);
                mx->sk[i] += mulq15(xs, xk0 + dxk * ii);
                mx->sd[i] += mulq15(xs, xd0 + dxd * ii);
            }
            if (dr0 != 32767 || ddr)
                x = (x >> 4) * ((dr0 + ddr * (int32_t)i) >> 3) >> 8;   /* (Q15 in two halves, as the mute's) */
            dst_l[i] += ((x >> 4) * gl) >> 8;           /* (x may pass 2^19: >> 4 first) */
            dst_r[i] += ((x >> 4) * gr) >> 8;
        }
        t->peak = pk;
    }
}


/* ---- DUST: the master through an old sampler and a record. G_DUST 0..127 turns up together: drive
 * into a soft clip, a lower sample rate (held samples, 44.1 -> 11 kHz), fewer bits (15 -> 8), a
 * one-pole low-pass (open -> ~3 kHz), a little hiss and crackle. The hiss and the crackle are the
 * record turning: they fade in with PLAY and out (~0.1 s) at STOP, so a stopped FMX is silent.
 * Stereo, ~25 ops a sample. */
static struct {
    int32_t hl, hr;                                     /* the held samples */
    int32_t ll, lr;                                     /* low-pass states */
    int32_t rnd, click;                                 /* noise state, a crackle decaying */
    int32_t bed;                                        /* hiss / crackle level, Q15: 0 stopped, 32767 playing */
    uint32_t ph;                                        /* the lower rate's phase, Q16 of a held sample */
    int32_t pl, pr, nl, nr;                             /* the last input, the next sample's share of a step */
} dust = {0, 0, 0, 0, 0x2545F491, 0, 0, 0, 0, 0, 0, 0};
/* DUST's lower rate without the buzz (after Plaits' sample_rate_reducer.h, (c) 2014 Emilie Gillet, MIT): the rate goes
 * down smoothly, each step lands where it falls between two samples and is smoothed by a two-sample BLEP. Chosen by
 * ear over the plain held samples (2026-10-29) */
static inline int32_t dust_blep_this(uint32_t t) { return (int32_t)(t * t >> 18); }   /* t: Q16 of a sample since */
static inline int32_t dust_blep_next(uint32_t t) { t = 65535u - t; return -(int32_t)(t * t >> 18); }

static int32_t crush_bits(int32_t v, int32_t shift)    /* fewer bits, rounded toward 0: no DC from tails */
{
    return v >= 0 ? (v >> shift) << shift : -((-v >> shift) << shift);
}

static void dust_process(int32_t *l, int32_t *r, uint32_t n)
{
    int32_t d = song.g[G_DUST], shift, a, drive, hiss, i, bed0, bed1;
    uint32_t pc;
    if (!d) {
        dust.bed = 0;
        return;
    }
    shift = d / 18;
    a = 32767 - d * 165;                                /* one-pole coefficient, Q15 */
    drive = 4096 + d * 24;                              /* Q12: 1x .. 1.75x */
    hiss = d * 2;
    pc = (uint32_t)d * 7u;                              /* crackle: chance per sample, x 2^-22 */
    bed0 = dust.bed;                                    /* ~0.1 s from 0 to full (238 a block of 32) */
    bed1 = dust.bed = clamp(dust.bed + (song.playing ? 238 : -238), 0, 32767);
    /* the rate as a fraction of 44.1 kHz, 1 / (1 + 3 d / 127): the old holds 1..4 at d = 0, 43, 85, 127, smooth between */
    uint32_t f = (uint32_t)((65536 * 127) / (127 + 3 * d)), rcp = 0xFFFFFFFFu / f;
    for (i = 0; i < (int32_t)n; i++) {
        int32_t x = l[i], y = r[i];
        int32_t bed = bed0 + (((bed1 - bed0) * i) >> CTL_LOG2);
        uint32_t nz = noise32(&dust.rnd);
        {
            int32_t cl = softclip(((x >> 2) * drive) >> 10), cr = softclip(((y >> 2) * drive) >> 10);
            int32_t ol = dust.nl, orr = dust.nr;
            dust.nl = dust.nr = 0;
            dust.ph += f;
            if (dust.ph >= 65536u) {                    /* a new held sample, t (Q16 of a sample) ago */
                uint32_t t;
                int32_t nl, nr;
                dust.ph -= 65536u;
                t = (dust.ph * rcp) >> 16;
                if (t > 65535u)
                    t = 65535u;
                nl = dust.pl + ((((cl - dust.pl) >> 1) * (int32_t)((65535u - t) >> 1)) >> 14);   /* where it fell */
                nr = dust.pr + ((((cr - dust.pr) >> 1) * (int32_t)((65535u - t) >> 1)) >> 14);
                if (shift) {
                    nl = crush_bits(nl, shift);
                    nr = crush_bits(nr, shift);
                }
                ol += (nl - dust.hl) * dust_blep_this(t) >> 15;
                orr += (nr - dust.hr) * dust_blep_this(t) >> 15;
                dust.nl += (nl - dust.hl) * dust_blep_next(t) >> 15;
                dust.nr += (nr - dust.hr) * dust_blep_next(t) >> 15;
                dust.hl = nl;
                dust.hr = nr;
            }
            dust.nl += dust.hl;
            dust.nr += dust.hr;
            dust.pl = cl;
            dust.pr = cr;
            dust.ll += mulq15(ol - dust.ll, a);
            dust.lr += mulq15(orr - dust.lr, a);
        }
        if ((nz >> 10) < pc)                            /* a speck of dust */
            dust.click = mulq15(((int32_t)(nz & 0x3FFu) - 512) * d / 8, bed);
        x = dust.ll + dust.click + mulq15(((int32_t)(nz >> 16) - 32768) * hiss >> 15, bed);
        y = dust.lr + dust.click + mulq15(((int32_t)(nz & 0xFFFFu) - 32768) * hiss >> 15, bed);
        dust.click -= dust.click >> 2;
        l[i] = x;
        r[i] = y;
    }
}

static void djf_process(int32_t *l, int32_t *r, uint32_t n)
{
    tsvf_t c;
    if (!djf_block(&djf, song.g[G_FILT], &c))
        return;
    djf_run(&djf, &c, l, n, 0);
    djf_run(&djf, &c, r, n, 1);
}

#include "punch.c"            /* the moves of LIVE and AUTO SONG on the whole mix (WASH, REWIND, ROLL ...) */

/* ---- the render units and the second core (after SLOOP 2.7.1). A unit is a track (mix_part: the three parts and
 * the BREAK track); unit k is bit k of a mask. What a unit on core 1 may not share with one on core 0: part_buf (one
 * per core), and rng() (libc.c): a part whose LFO wraps this block (voice.c track_lfo_tick draws) stays on core 0,
 * where the draws keep their order (units_split). The rest a unit touches is its own: its voices, its SLICER, its
 * FILTER, its DIST, its peak and mute fade; the BREAK track's generator state (brkpart.c bp) is the BREAK unit's
 * alone. The events (seq.c), DUCK, the metronome, the buses and the master run on core 0, before or after */
#define U_ALL ((1u << NTRK) - 1u)

static void mix_units(uint32_t mask, uint32_t n, mixacc_t *mx, int32_t *scratch)   /* the units of mask, in order */
{
    uint32_t k;
    for (k = 0; k < NTRK; k++)
        if (mask & (1u << k))
            mix_part(&trk[k], n, mx, scratch);         /* (the BREAK track too: brkpart.c renders it, track_render) */
}

/* the load figures: core 1's time in its job and core 0's time waiting for it, in ticks over a half (cpu1.h
 * cpu1_ticks; the job's are written by core 1, read and cleared by core 0 while core 1 is idle), then as 1/256
 * of the half, smoothed as song.cpu_q8 is (audio.c calls cpu1_load_half after each half; console.c shows them) */
static uint32_t cpu1_job_ticks, cpu1_wait_ticks, cpu1_busy_q8, cpu1_wait_q8;
static void cpu1_load_half(uint32_t half_us)
{
    uint32_t per = half_us * CPU1_TICKS_PER_US / 256u;  /* ticks per 1/256 of the half */
    if (!per)
        per = 1;
    cpu1_busy_q8 = (cpu1_busy_q8 * 15u + cpu1_job_ticks / per) / 16u;
    cpu1_wait_q8 = (cpu1_wait_q8 * 15u + cpu1_wait_ticks / per) / 16u;
    cpu1_job_ticks = cpu1_wait_ticks = 0;
}

static void cpu1_units_job(uint32_t arg)                /* core 1's job: the units of arg & 0xFF, n = arg >> 8 */
{
    uint32_t t0 = cpu1_ticks();
    mix_units(arg & 0xFFu, arg >> 8, &acc[1], part_buf[1]);
    cpu1_job_ticks += cpu1_ticks() - t0;
}

/* the units core 1 renders this block: CHOP and the BREAK track (FMX: the BASS's reese and the CHORDS pads stay
 * here, about half each way), or cpu1_units when set (the menu, the tests), less the parts that must stay here */
static uint32_t units_split(void)
{
    uint32_t mask = cpu1_units < 0 ? (1u << 2) | (1u << TRK_BRK) : (uint32_t)cpu1_units & U_ALL, k;
    for (k = 0; k < NPART; k++) {
        const track_t *t = &trk[k];
        if ((mask & (1u << k)) && t->lfo_ph + LFO_INC[t->p[P_LRATE] & 127] < t->lfo_ph)
            mask &= ~(1u << k);                         /* (its LFO wraps this block: an rng() draw) */
    }
    return mask;
}

static void mix_block(int32_t *out, uint32_t n)
{
    uint32_t i, c1;
    int32_t *mix_l = acc[0].l, *mix_r = acc[0].r;
    for (i = 0; i < n; i++) {
        acc[0].l[i] = acc[0].r[i] = acc[0].sc[i] = acc[0].sd[i] = acc[0].sr[i] = acc[0].sx[i] = acc[0].sk[i] = 0;
        acc[0].tl[i] = acc[0].tr[i] = 0;
    }
    perf_block(n);                                      /* the moves (punch.c): before the events (their mutes) */
    events_block(n);
    lfx_block();                                        /* the FX panel: this block's throws (lfx.c) */
    duck_block(n * (uint32_t)song.g[G_BPM]);
    c1 = cpu1_on() ? units_split() : 0;                 /* core 1's units this block */
    if (c1) {
        for (i = 0; i < n; i++) {
            acc[1].l[i] = acc[1].r[i] = acc[1].sc[i] = acc[1].sd[i] = acc[1].sr[i] = acc[1].sx[i] = acc[1].sk[i] = 0;
            acc[1].tl[i] = acc[1].tr[i] = 0;
        }
        if (!cpu1_run(cpu1_units_job, c1 | n << 8))
            c1 = 0;
    }
    mix_units(U_ALL & ~c1, n, &acc[0], part_buf[0]);    /* this core's units, meanwhile */
    if (c1) {
        uint32_t w0 = cpu1_ticks();
        int late = cpu1_wait();
        cpu1_wait_ticks += cpu1_ticks() - w0;
        if (late) {                                     /* core 1 did not finish in time: held from now on (cpu1.h);
                                                         * its units here, once (what it half did is this block's glitch) */
            for (i = 0; i < n; i++) {
                acc[1].l[i] = acc[1].r[i] = acc[1].sc[i] = acc[1].sd[i] = acc[1].sr[i] = acc[1].sx[i] = acc[1].sk[i] = 0;
                acc[1].tl[i] = acc[1].tr[i] = 0;
            }
            cpu1_units_job(c1 | n << 8);
        }
        for (i = 0; i < n; i++) {
            acc[0].l[i] += acc[1].l[i];
            acc[0].r[i] += acc[1].r[i];
            acc[0].sc[i] += acc[1].sc[i];
            acc[0].sd[i] += acc[1].sd[i];
            acc[0].sr[i] += acc[1].sr[i];
            acc[0].sx[i] += acc[1].sx[i];
            acc[0].sk[i] += acc[1].sk[i];
            acc[0].tl[i] += acc[1].tl[i];
            acc[0].tr[i] += acc[1].tr[i];
        }
    }
    click_render(mix_l, mix_r, n);                      /* the metronome: its own voice (click.c), any mute */
    if (lfx.ins)
        lfx_time(acc[0].tl, acc[0].tr, mix_l, mix_r, n);   /* LIVE's TIME stem: through its effect into the mix */
    if (lfx.any_rev | lfx.any_spk)
        lfx_bus(acc[0].sx, acc[0].sk, acc[0].sr, n);     /* LIVE's throws into the reverb's send (lfx.c), when any */
    fx_buses(acc[0].sc, acc[0].sd, acc[0].sr, wet_l, wet_r, n);
    for (i = 0; i < n; i++) {
        mix_l[i] += wet_l[i];
        mix_r[i] += wet_r[i];
    }
    dust_process(mix_l, mix_r, n);
    punch_process(mix_l, mix_r, n);
    djf_process(mix_l, mix_r, n);
    master_out(mix_l, mix_r, out, n);                  /* the limiter, the knob (and the USB, the visualiser) */
}
