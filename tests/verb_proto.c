/* SPDX-License-Identifier: GPL-3.0-only */
/* The track reverbs' prototype (a listening test before the firmware): six reverb types, each a send of one track,
 * in the firmware's own arithmetic (int16 lines, Q15 coefficients, the SPU's 22.05 kHz, its send low cut), on the dry
 * stems song_audio_test stems writes. Each type's line sizes are the ones it would have on the device:
 *   SPRING   two springs: a 38 / 43 ms loop through 10 stretched all-passes (the chirp), low-passed     3.6 KB
 *   PLATE    a small Dattorro plate: 4 input diffusers, a figure-eight tank of two all-passes and
 *            two lines a side, its all-passes swaying                                                  11.1 KB
 *   METAL    four combs 4..10 ms (pipe resonances, two swaying) into two all-passes: a tunnel            3.8 KB
 *   LOFI     a 4-line FDN at 7.35 kHz, its lines 8-bit, the output held: an old sampler's room          1.4 KB
 *   GATED    a dense bright FDN with a long tail, cut by a gate the loudest hits open for 1/8                   4.8 KB
 *   SHIMMER  an FDN with an octave-up shifter in its loop: the tail climbs                               9.4 KB
 *   HALL     the SPU's HALL as the firmware has it now (fx.c): the reference
 * verb_proto STEM.wav DRY.wav TYPE SEND OUT.wav: DRY plus TYPE's wet of STEM at SEND (0..127, as REV) */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static inline int32_t clamp(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : v > hi ? hi : v; }
static inline int16_t sat16(int32_t v) { return (int16_t)clamp(v, -32768, 32767); }
/* a x b, Q15, rounded toward 0 (fx.c's mz: a feedback loop of these only loses energy) */
static inline int32_t mz(int32_t a, int32_t b)
{
    int32_t p = clamp(a, -65535, 65535) * b;
    return p >= 0 ? p >> 15 : -((-p) >> 15);
}
#define Q(x) ((int32_t)((x) * 32768.0 + ((x) < 0 ? -0.5 : 0.5)))

/* ---- the pieces: a line (n samples, written once a tick), read d back (1..n); the 8-bit line is mu-law-like (an old
 * sampler's): sign, a 3-bit exponent, a 4-bit mantissa over 15 bits, rounded toward 0 (the tail dies: no limit cycle);
 * quiet keeps its detail, loud turns gritty */
typedef struct { int16_t *b; uint16_t n, w; } dl_t;
typedef struct { int8_t *b; uint16_t n, w; } dl8_t;
static uint32_t mem_bytes;
static void dl_init(dl_t *d, uint32_t n) { d->b = calloc(n, 2); d->n = (uint16_t)n; d->w = 0; mem_bytes += n * 2u; }
static void dl8_init(dl8_t *d, uint32_t n) { d->b = calloc(n, 1); d->n = (uint16_t)n; d->w = 0; mem_bytes += n; }
static inline int32_t dl_rd(const dl_t *d, uint32_t k) { return d->b[d->w >= k ? d->w - k : d->w + d->n - k]; }
static inline void dl_wr(dl_t *d, int32_t x) { d->b[d->w] = sat16(x); d->w = d->w + 1u >= d->n ? 0 : d->w + 1u; }
static inline int32_t ml_dec(int8_t b)
{
    uint32_t u = (uint8_t)b, e = (u >> 4) & 7u, m = u & 15u;
    int32_t a = (int32_t)(e ? (16u + m) << e : m << 1) << 3;
    return u & 128u ? -a : a;
}
static inline int8_t ml_enc(int32_t x)
{
    uint32_t a = (uint32_t)(x < 0 ? -x : x) >> 3, e = 0, m;
    if (a > 4095u)
        a = 4095u;
    if (a >= 32u)
        e = 31u - (uint32_t)__builtin_clz(a) - 4u;   /* a in [16 << e, 32 << e) */
    m = e ? (a >> e) - 16u : a >> 1;
    return (int8_t)((x < 0 ? 128u : 0u) | e << 4 | m);
}
static inline int32_t dl8_rd(const dl8_t *d, uint32_t k) { return ml_dec(d->b[d->w >= k ? d->w - k : d->w + d->n - k]); }
static inline void dl8_wr(dl8_t *d, int32_t x)
{
    d->b[d->w] = ml_enc(x);
    d->w = d->w + 1u >= d->n ? 0 : d->w + 1u;
}
/* d back with a Q8 fraction (a swaying read) */
static inline int32_t dl_rdf(const dl_t *d, uint32_t q8)
{
    int32_t a = dl_rd(d, q8 >> 8), b = dl_rd(d, (q8 >> 8) + 1u);
    return a + (((b - a) * (int32_t)(q8 & 255u)) >> 8);
}
/* a Schroeder all-pass on its whole line: w = x - g z; y = z + g w */
static inline int32_t ap(dl_t *d, int32_t x, int32_t g)
{
    int32_t z = dl_rd(d, d->n), w = x - mz(z, g);
    dl_wr(d, w);
    return z + mz(w, g);
}
static inline int32_t ap_at(dl_t *d, uint32_t q8, int32_t x, int32_t g)   /* the same, z read q8 back (< n) */
{
    int32_t z = dl_rdf(d, q8), w = x - mz(z, g);
    dl_wr(d, w);
    return z + mz(w, g);
}
static inline int32_t lp1(int32_t *s, int32_t x, int32_t a) { return *s += mz(x - *s, a); }
/* a triangle LFO, -32767..32767, one turn in 2^32 */
static inline int32_t tri(uint32_t ph) { int32_t v = (int32_t)(ph >> 15) - 65536; return (v < 0 ? -v : v) - 32768; }

/* ---- SPRING */
#define SP_N 10
static struct {
    dl_t loop[2];
    int32_t z[2][SP_N][2], lp[2];   /* the stretched all-passes' last two w (K = 2), the loop's low-pass */
    uint8_t k;
} sp;
static void spring_init(void) { dl_init(&sp.loop[0], 838); dl_init(&sp.loop[1], 951); }
static void spring_tick(int32_t in, int32_t *l, int32_t *r)
{
    int32_t c, o[2];
    for (c = 0; c < 2; c++) {
        int32_t d = dl_rd(&sp.loop[c], sp.loop[c].n), x, i;
        lp1(&sp.lp[c], d, Q(0.62));                   /* ~4 kHz: a spring's top */
        x = in + mz(sp.lp[c], Q(0.86));
        for (i = 0; i < SP_N; i++) {                  /* the chirp: H = (a + z^-2) / (1 + a z^-2), a = 0.6 */
            int32_t *z = sp.z[c][i], zk = z[sp.k], w = x - mz(zk, Q(0.6));
            x = zk + mz(w, Q(0.6));
            z[sp.k] = w;
        }
        dl_wr(&sp.loop[c], x);
        o[c] = d;
    }
    sp.k ^= 1u;
    *l = o[0];
    *r = o[1];
}

/* ---- PLATE: Dattorro's plate (1997) at 22.05 kHz, its tank 0.33 of his */
static struct {
    dl_t di[4], ap1[2], d1[2], ap2[2], d2[2];
    int32_t bw, damp[2], fb[2];
    uint32_t ph;
} pl;
static const uint16_t PL_DI[4] = {53, 40, 140, 103}, PL_AP1[2] = {165, 223}, PL_D1[2] = {1094, 1036},
                      PL_AP2[2] = {442, 653}, PL_D2[2] = {914, 777};
static const uint16_t PL_D1_0[2] = {4453, 4217}, PL_AP2_0[2] = {1800, 2656}, PL_D2_0[2] = {3720, 3163};
static void plate_init(void)
{
    uint32_t i;
    for (i = 0; i < 4; i++)
        dl_init(&pl.di[i], PL_DI[i]);
    for (i = 0; i < 2; i++) {
        dl_init(&pl.ap1[i], PL_AP1[i] + 12u);         /* (+12: the sway's room) */
        dl_init(&pl.d1[i], PL_D1[i]);
        dl_init(&pl.ap2[i], PL_AP2[i]);
        dl_init(&pl.d2[i], PL_D2[i]);
    }
}
/* a tap of Dattorro's, at t of his line of len0 samples, on ours */
#define PT(dl, t, len0) dl_rd(&(dl), (uint32_t)((t) * (dl).n / (len0)) + 1u)
static void plate_tick(int32_t in, int32_t *l, int32_t *r)
{
    int32_t x = lp1(&pl.bw, in, Q(0.9)), c, nf[2], sw;
    x = ap(&pl.di[0], x, Q(0.75));
    x = ap(&pl.di[1], x, Q(0.75));
    x = ap(&pl.di[2], x, Q(0.625));
    x = ap(&pl.di[3], x, Q(0.625));
    pl.ph += 107000u;                                 /* ~0.55 Hz */
    sw = tri(pl.ph);
    for (c = 0; c < 2; c++) {                         /* each side: fed by the other's end */
        int32_t y = x + pl.fb[c], s = c ? -sw : sw;
        y = ap_at(&pl.ap1[c], (uint32_t)((PL_AP1[c] << 8) + (s * 6 >> 7)), y, -Q(0.70));
        dl_wr(&pl.d1[c], y);
        y = lp1(&pl.damp[c], dl_rd(&pl.d1[c], pl.d1[c].n), Q(0.72));
        y = mz(y, Q(0.78));                           /* DECAY */
        y = ap(&pl.ap2[c], y, Q(0.50));
        dl_wr(&pl.d2[c], y);
        nf[c] = mz(dl_rd(&pl.d2[c], pl.d2[c].n), Q(0.78));
    }
    pl.fb[0] = nf[1];
    pl.fb[1] = nf[0];
    /* the output taps (his: L from the right tank mostly, R from the left) */
    *l = mz(PT(pl.d1[1], 266, 4217) + PT(pl.d1[1], 2974, 4217) - PT(pl.ap2[1], 1913, 2656) + PT(pl.d2[1], 1996, 3163) -
            PT(pl.d1[0], 1990, 4453) - PT(pl.ap2[0], 187, 1800) - PT(pl.d2[0], 1066, 3720), Q(0.6));
    *r = mz(PT(pl.d1[0], 353, 4453) + PT(pl.d1[0], 3627, 4453) - PT(pl.ap2[0], 1228, 1800) + PT(pl.d2[0], 2673, 3720) -
            PT(pl.d1[1], 2111, 4217) - PT(pl.ap2[1], 335, 2656) - PT(pl.d2[1], 121, 3163), Q(0.6));
    (void)PL_D1_0; (void)PL_AP2_0; (void)PL_D2_0;
}

/* ---- METAL: four pipe combs (inharmonic, two swaying: a slow flange), then two all-passes a side */
static struct {
    dl_t cb[4], apl[2];
    int32_t lp[4];
    uint32_t ph;
} mt;
static const uint16_t MT_CB[4] = {97, 131, 173, 229};
static void metal_init(void)
{
    uint32_t i;
    for (i = 0; i < 4; i++)
        dl_init(&mt.cb[i], MT_CB[i] + 8u);
    dl_init(&mt.apl[0], 557);
    dl_init(&mt.apl[1], 743);
}
static void metal_tick(int32_t in, int32_t *l, int32_t *r)
{
    int32_t y[4], k, sw;
    mt.ph += 60000u;                                  /* ~0.3 Hz */
    sw = tri(mt.ph);
    for (k = 0; k < 4; k++) {
        uint32_t q8 = (uint32_t)(MT_CB[k] << 8);
        if (k & 1)
            q8 = (uint32_t)((int32_t)q8 + (k == 1 ? sw : -sw) * 3 / 128);   /* +-3 samples */
        y[k] = dl_rdf(&mt.cb[k], q8);
        dl_wr(&mt.cb[k], (in >> 1) + mz(lp1(&mt.lp[k], y[k], Q(0.55)), Q(0.95)));
    }
    *l = ap(&mt.apl[0], y[0] - y[1] + y[2] - y[3], Q(0.6));
    *r = ap(&mt.apl[1], y[0] + y[1] - y[2] - y[3], Q(0.6));
}

/* ---- the FDN: four lines, a Householder mix (y - sum / 2), a low-pass and a gain in each, two input all-passes */
typedef struct {
    dl_t di[2], ln[4];
    int32_t lp[4], damp, fb;
} fdn_t;
static void fdn_init(fdn_t *f, const uint16_t *di, const uint16_t *ln, int32_t damp, int32_t fb)
{
    uint32_t i;
    for (i = 0; i < 2; i++)
        dl_init(&f->di[i], di[i]);
    for (i = 0; i < 4; i++)
        dl_init(&f->ln[i], ln[i]);
    f->damp = damp;
    f->fb = fb;
}
/* one tick: in (mono), out the four lines' ends in y */
static void fdn_tick(fdn_t *f, int32_t in, int32_t *y)
{
    int32_t x = ap(&f->di[1], ap(&f->di[0], in, Q(0.7)), Q(0.7)), s, k;
    for (k = 0; k < 4; k++)
        y[k] = dl_rd(&f->ln[k], f->ln[k].n);
    s = (y[0] + y[1] + y[2] + y[3]) >> 1;
    for (k = 0; k < 4; k++)
        dl_wr(&f->ln[k], (k & 1 ? -x : x) + mz(lp1(&f->lp[k], y[k] - s, f->damp), f->fb));
}

/* ---- LOFI: the FDN at a third of the rate (7.35 kHz), 8-bit lines, its output held */
static struct {
    dl_t di[2];
    dl8_t ln[4];
    int32_t lp[4], acc, ol, or_;
    uint8_t n;
} lf;
static void lofi_init(void)
{
    static const uint16_t LN[4] = {223, 271, 337, 409};
    uint32_t i;
    dl_init(&lf.di[0], 31);
    dl_init(&lf.di[1], 47);
    for (i = 0; i < 4; i++)
        dl8_init(&lf.ln[i], LN[i]);
}
static void lofi_tick(int32_t in, int32_t *l, int32_t *r)
{
    lf.acc += in;
    if (++lf.n == 3u) {
        int32_t x = lf.acc / 3, y[4], s, k;
        lf.n = 0;
        lf.acc = 0;
        x = ap(&lf.di[1], ap(&lf.di[0], x, Q(0.6)), Q(0.6));
        for (k = 0; k < 4; k++)
            y[k] = dl8_rd(&lf.ln[k], lf.ln[k].n);
        s = (y[0] + y[1] + y[2] + y[3]) >> 1;
        for (k = 0; k < 4; k++)
            dl8_wr(&lf.ln[k], (k & 1 ? -x : x) + mz(lp1(&lf.lp[k], y[k] - s, Q(0.75)), Q(0.92)));
        lf.ol = y[0] + y[2];
        lf.or_ = y[1] + y[3];
    }
    *l = lf.ol;
    *r = lf.or_;
}

/* ---- GATED: a dense bright FDN that would ring ~2 s, a gate the hits open: an onset (the fast envelope above
 * 0.7 of the slow peak, rising) opens it for HOLD (1/8 at the tempo), then it shuts in ~10 ms */
static struct {
    fdn_t f;
    int32_t fast, peak, prev, g;
    uint32_t hold, since, hold_n;
} gt;
#ifndef GT_THR
#define GT_THR Q(0.7)                                 /* a hit opens it: the loudest only (the snares, the kicks) */
#endif
#ifndef GT_HOLD16
#define GT_HOLD16 2u                                  /* open for 2/16 */
#endif
static void gated_init(uint32_t bpm)
{
    static const uint16_t DI[2] = {113, 167}, LN[4] = {409, 487, 577, 661};
    fdn_init(&gt.f, DI, LN, Q(0.8), Q(0.93));
    gt.hold_n = 22050u * 60u * GT_HOLD16 / (bpm * 4u);
}
static void gated_tick(int32_t in, int32_t *l, int32_t *r)
{
    int32_t y[4], a = in < 0 ? -in : in;
    gt.fast = a > gt.fast ? a : gt.fast - (gt.fast >> 6);          /* ~3 ms */
    gt.peak = a > gt.peak ? a : gt.peak - (gt.peak >> 14);         /* ~0.75 s */
    gt.since++;
    if (gt.fast > mz(gt.peak, GT_THR) && gt.fast > gt.prev + (gt.prev >> 3) && gt.since > 660u && gt.fast > 300) {
        gt.hold = gt.hold_n;                          /* (a new hit: 30 ms after the last at least) */
        gt.since = 0;
    }
    gt.prev = gt.fast;
    if (gt.hold) {
        gt.hold--;
        gt.g += (32767 - gt.g) >> 4;                  /* (open in ~1 ms) */
    } else {
        gt.g -= gt.g >> 7;                            /* (shut in ~10 ms) */
    }
    fdn_tick(&gt.f, in, y);
    *l = mz(y[0] + y[2], gt.g);
    *r = mz(y[1] + y[3], gt.g);
}

/* ---- SHIMMER: an FDN, its sum shifted an octave up (two heads at twice the speed over a 23 ms window, crossfaded)
 * and fed back in, low-passed */
#define SH_W 512u
static struct {
    fdn_t f;
    dl_t sb;
    int32_t fb, lp;
    uint32_t ph;
} sh;
static void shimmer_init(void)
{
    static const uint16_t DI[2] = {149, 211}, LN[4] = {701, 863, 1031, 1223};
    fdn_init(&sh.f, DI, LN, Q(0.7), Q(0.90));
    dl_init(&sh.sb, SH_W + 2u);
}
static void shimmer_tick(int32_t in, int32_t *l, int32_t *r)
{
    int32_t y[4], d1, d2, g1, g2, s;
    fdn_tick(&sh.f, in + sh.fb, y);
    dl_wr(&sh.sb, (y[0] + y[1] + y[2] + y[3]) >> 2);
    sh.ph = (sh.ph + 1u) % SH_W;                      /* the heads' delay falls a sample a tick: twice the speed */
    d1 = (int32_t)(SH_W - sh.ph);
    d2 = (int32_t)((SH_W - sh.ph + SH_W / 2u) % SH_W) + 1;
    g1 = 32767 - (int32_t)((labs(2 * d1 - (int32_t)SH_W) * 32767) / SH_W);   /* (triangles: silent at the wrap) */
    g2 = 32767 - (int32_t)((labs(2 * d2 - (int32_t)SH_W) * 32767) / SH_W);
    s = mz(dl_rd(&sh.sb, (uint32_t)d1), g1) + mz(dl_rd(&sh.sb, (uint32_t)d2), g2);
    sh.fb = mz(lp1(&sh.lp, s, Q(0.6)), Q(0.45));
    *l = y[0] + y[2];
    *r = y[1] + y[3];
}

/* ---- HALL: fx.c's SPU reverb, its HALL preset, DAMP 40 (the reference) */
static struct {
    dl_t ln[4], apl[4];
    int32_t ls[2], ld[2];
} hl;
static void hall_init(void)
{
    uint32_t k;
    for (k = 0; k < 4; k++) {
        dl_init(&hl.ln[k], 6400);
        dl_init(&hl.apl[k], 1792);
    }
}
static void hall_tick(int32_t in, int32_t *l, int32_t *r)
{
    static const uint16_t DS[2] = {3900, 3620}, DD[2] = {3760, 4050}, T[2][4] = {{3600, 3250, 2850, 2450}, {3480, 3150, 2750, 2350}},
                          A1[2] = {900, 840}, A2[2] = {700, 640};
    static const int16_t C[4] = {21000, -18000, 16400, -15000};
    int32_t iir = 27000 - 40 * 64, wall = -23600, k, o[2];
    /* (fx.c reads before it writes, its lines indexed from the write position: here d back is d + 1 as ours count) */
    for (k = 0; k < 2; k++) {
        int32_t ws = dl_rd(&hl.ln[k], DS[k]), wd = dl_rd(&hl.ln[3 - k], DD[k]);
        hl.ls[k] += mz(in + mz(ws, wall) - hl.ls[k], iir);
        hl.ld[k] += mz(in + mz(wd, wall) - hl.ld[k], iir);
    }
    for (k = 0; k < 2; k++) {
        dl_wr(&hl.ln[k], hl.ls[k]);
        dl_wr(&hl.ln[2 + k], hl.ld[k]);
    }
    for (k = 0; k < 2; k++) {
        int32_t x = mz(dl_rd(&hl.ln[k], T[k][0] + 1u), C[0]) + mz(dl_rd(&hl.ln[2 + k], T[k][1] + 1u), C[1]) +
                    mz(dl_rd(&hl.ln[k], T[k][2] + 1u), C[2]) + mz(dl_rd(&hl.ln[2 + k], T[k][3] + 1u), C[3]);
        {
            dl_t *a1 = &hl.apl[k], *a2 = &hl.apl[2 + k];
            int32_t a = dl_rd(a1, A1[k]), b;
            x -= mz(a, 21300);
            dl_wr(a1, x);
            x = mz(x, 21300) + a;
            b = dl_rd(a2, A2[k]);
            x -= mz(b, 19700);
            dl_wr(a2, x);
            x = mz(x, 19700) + b;
        }
        o[k] = mz(x, 16000);
    }
    *l = o[0];
    *r = o[1];
}

/* ---- the WAVs: 44.1 kHz 16-bit stereo */
static int16_t *wav_read(const char *p, uint32_t *frames)
{
    FILE *f = fopen(p, "rb");
    uint8_t h[44];
    int16_t *s;
    if (!f || fread(h, 1, 44, f) != 44) {
        fprintf(stderr, "verb_proto: %s?\n", p);
        exit(1);
    }
    *frames = (uint32_t)(h[40] | h[41] << 8 | h[42] << 16 | (uint32_t)h[43] << 24) / 4u;
    s = malloc(*frames * 4u);
    *frames = (uint32_t)fread(s, 4, *frames, f);
    fclose(f);
    return s;
}
static void wav_write(const char *p, const int32_t *lr, uint32_t frames)
{
    FILE *f = fopen(p, "wb");
    uint32_t i, n = frames * 4u, v;
    uint8_t h[44] = {'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'A', 'V', 'E', 'f', 'm', 't', ' ', 16, 0, 0, 0, 1, 0, 2, 0,
                     0x44, 0xAC, 0, 0, 0x10, 0xB1, 2, 0, 4, 0, 16, 0, 'd', 'a', 't', 'a'};
    v = n + 36u;
    memcpy(h + 4, &v, 4);
    memcpy(h + 40, &n, 4);
    fwrite(h, 1, 44, f);
    for (i = 0; i < frames * 2u; i++) {
        int16_t s = sat16(lr[i]);
        fwrite(&s, 2, 1, f);
    }
    fclose(f);
}

typedef void (*tick_fn)(int32_t, int32_t *, int32_t *);
int main(int argc, char **argv)
{
    static const char *const NM[7] = {"spring", "plate", "metal", "lofi", "gated", "shimmer", "hall"};
    static const tick_fn TK[7] = {spring_tick, plate_tick, metal_tick, lofi_tick, gated_tick, shimmer_tick, hall_tick};
    /* each type's out level: its wet as loud as the HALL's (RMS, the BREAK, the CHORDS and the CHOP at REV 90; the
     * GATED 2 dB over: it is heard a third of the time) */
    static const int32_t TRIM[7] = {Q(0.355), Q(0.42), Q(0.30), Q(0.25), Q(0.224), Q(0.224), 32767};
    uint32_t fs, fd, i, t, tail = 44100u * 3u;
    int16_t *st, *dr;
    int32_t *o, hp[2] = {0, 0}, in = 0, ol[2] = {0, 0}, orr[2] = {0, 0}, send, peak = 1;
    int half = 0;
    double wsum = 0;
    if (argc < 6) {
        fprintf(stderr, "verb_proto STEM.wav DRY.wav TYPE SEND OUT.wav\n");
        return 1;
    }
    for (t = 0; t < 7u && strcmp(argv[3], NM[t]); t++)
        ;
    if (t == 7u) {
        fprintf(stderr, "verb_proto: no type %s\n", argv[3]);
        return 1;
    }
    switch (t) {
    case 0: spring_init(); break;
    case 1: plate_init(); break;
    case 2: metal_init(); break;
    case 3: lofi_init(); break;
    case 4: gated_init(170); break;
    case 5: shimmer_init(); break;
    default: hall_init(); break;
    }
    send = atoi(argv[4]) * 258;
    st = wav_read(argv[1], &fs);
    dr = wav_read(argv[2], &fd);
    if (fd < fs)
        fs = fd;
    o = calloc((fs + tail) * 2u, sizeof(int32_t));
    for (i = 0; i < fs + tail; i++) {
        int32_t x = i < fs ? (st[2 * i] + st[2 * i + 1]) / 2 : 0, rl, rr;
        int32_t e = x - hp[0] + hp[1], d = e >> 5;    /* (fx.c's send low cut, ~220 Hz) */
        hp[1] = e - (d << 5);
        hp[0] += d;
        x = mz(x - hp[0], send);
        if (!half) {
            in = x;
        } else {
            ol[0] = ol[1];
            orr[0] = orr[1];
            TK[t]((in + x) >> 1, &ol[1], &orr[1]);
            ol[1] = mz(ol[1], TRIM[t]);
            orr[1] = mz(orr[1], TRIM[t]);
        }
        half ^= 1;
        rl = half ? ol[1] : (ol[0] + ol[1]) >> 1;
        rr = half ? orr[1] : (orr[0] + orr[1]) >> 1;
        wsum += (double)rl * rl + (double)rr * rr;
        o[2 * i] = (i < fs ? dr[2 * i] : 0) + (rl << 1);
        o[2 * i + 1] = (i < fs ? dr[2 * i + 1] : 0) + (rr << 1);
    }
    for (i = 0; i < (fs + tail) * 2u; i++)
        if (abs(o[i]) > peak)
            peak = abs(o[i]);
    if (peak > 29000)                                 /* (no clipping: the whole file down) */
        for (i = 0; i < (fs + tail) * 2u; i++)
            o[i] = (int32_t)((int64_t)o[i] * 29000 / peak);
    wav_write(argv[5], o, fs + tail);
    printf("%-8s %5u B  wet rms %6.1f dBFS  peak %d%s\n", NM[t], mem_bytes,
           20 * log10(sqrt(wsum * 4 / (2.0 * (fs + tail))) / 32768 + 1e-12), peak, peak > 29000 ? " (scaled)" : "");
    return 0;
}
