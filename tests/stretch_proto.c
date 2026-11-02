/* SPDX-License-Identifier: GPL-3.0-only */
/* The BREAK at another tempo without its pitch moving: a listening test and a measurement before the firmware. A loop
 * (one bar, 16 kHz, as the firmware keeps it) played as 8 slices on the grid of a tempo, four ways, in integers:
 *   VARI   the firmware's now (brkpart.c bp_inc): the loop's speed set so a bar fills the tempo's bar; the pitch moves
 *          with the tempo
 *   SLICE  each slice at the loop's own speed, from its start on its trigger: cut by the next one (a 2 ms crossfade,
 *          BP_FADE) when the tempo is faster; faded out over 3 ms at its end when slower (a gap until the next)
 *   WSOLA  the whole loop stretched by WSOLA (after Clouds' wsola_sample_player.h, (c) 2014 Emilie Gillet, MIT):
 *          windows of 20 ms overlapped by half, each placed within +-8 ms of where it falls where it best matches the
 *          last one's continuation (a correlation of every 4th sample)
 *   SWSOLA WSOLA a slice at a time, each slice restarted on its trigger with its first window unfaded: the hits stay
 *          on the grid and keep their attack; WSOLA only fills (or shortens) what comes after. Heard: it doubles the
 *          hits (a window's search finds the hit again)
 *   SLICE+ SLICE, and slower than the loop: the slice's first 30 ms as they are, WSOLA over the rest of the slice, its
 *          search kept after the hit and inside the slice
 * Printed: the cost of each (ns per output sample on this Mac, and integer operations per output sample counted in
 * the code: a target estimate), the spectral centroid against the loop's (1.00 = the pitch kept), the peak.
 * WAVs (OUTDIR/LOOP_BPM_WAY.wav, 4 bars) for listening.
 * stretch_proto OUTDIR NATIVE_BPM:LOOP.raw..      (LOOP.raw: int16 mono at 16 kHz, one bar) */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define FS 44100
#define SR 16000
#define FADE 88                  /* BP_FADE: 2 ms */
#define TAILF 132                /* 3 ms: a slice's end, slower than its tempo */
#define WN 320                   /* WSOLA's window: 20 ms at 16 kHz */
#define WH (WN / 2)
#define WD 128                   /* its search: +-8 ms */
#define WSTEP 4                  /* its correlation: every 4th sample */
#define MAXOUT (FS * 12)

static inline int32_t clamp(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : v > hi ? hi : v; }
static int16_t hann[WN];                                  /* Q15, periodic: two at half overlap sum to 1 */
static uint64_t ops;                                      /* integer operations, counted (a rough target cost) */

static int32_t at(const int16_t *x, uint32_t len, uint32_t q16)   /* x at q16 (Q16 samples), linear, wrapping */
{
    uint32_t i = (q16 >> 16) % len, j = (i + 1u) % len;
    int32_t a = x[i], b = x[j];
    return a + (((b - a) * (int32_t)((q16 >> 1) & 0x7FFF)) >> 15);
}

/* ---- VARI ---- */
static void render_vari(const int16_t *x, uint32_t len, uint32_t bar, int32_t *y, uint32_t n)
{
    uint32_t step = (uint32_t)(((uint64_t)len << 16) / bar), q = 0, i;
    for (i = 0; i < n; i++, q += step)
        y[i] = at(x, len, q);
    ops += (uint64_t)n * 8;
}

/* ---- SLICE ---- */
typedef struct { uint32_t q, end; int32_t g, dg; int on; } sv_t;
static void render_slice(const int16_t *x, uint32_t len, uint32_t bar, int32_t *y, uint32_t n)
{
    const uint32_t step = (uint32_t)(((uint64_t)SR << 16) / FS);
    sv_t v[2] = {{0}};
    uint32_t i, k = 0, cur = 0;
    for (i = 0; i < n; i++) {
        int32_t s = 0, j;
        if (i == (uint32_t)((uint64_t)k * bar / 8u)) {   /* a trigger: the old voice out over 2 ms, slice k % 8 in */
            sv_t *o = &v[cur];
            if (o->on)
                o->dg = -(32767 / FADE + 1);
            cur ^= 1u;
            v[cur].q = (k % 8u) * (len / 8u) << 16;
            v[cur].end = ((k % 8u) + 1u) * (len / 8u) << 16;
            v[cur].g = 32767;
            v[cur].dg = 0;
            v[cur].on = 1;
            k++;
        }
        for (j = 0; j < 2; j++) {
            sv_t *a = &v[j];
            if (!a->on)
                continue;
            s += (at(x, len, a->q) * a->g) >> 15;
            a->q += step;
            if (a->dg == 0 && a->q >= a->end)            /* its slice is over before the next trigger: out over 3 ms */
                a->dg = -(32767 / TAILF + 1);
            a->g += a->dg;
            if (a->g <= 0)
                a->on = 0;
        }
        y[i] = s;
    }
    ops += (uint64_t)n * 22;
}

/* ---- WSOLA ---- */
static int32_t wcorr(const int16_t *x, uint32_t len, uint32_t a, uint32_t b)   /* sum x[a+j] x[b+j], every 4th j */
{
    int32_t c = 0;
    uint32_t j;
    for (j = 0; j < WN; j += WSTEP)
        c += (x[(a + j) % len] >> 4) * (x[(b + j) % len] >> 4);
    return c;
}
/* m source-rate samples of x stretched from p0 (in x) by WSOLA into z (z[0..m + WN) cleared by the caller): the
 * windows' nominal places p0 + k WH / alpha (alpha = m / src samples covered, Q16 as cover); first: the first window
 * unfaded (a slice's attack) */
static void wsola(const int16_t *x, uint32_t len, uint32_t p0, uint32_t cover, uint32_t m, int32_t *z, int first_flat)
{
    uint32_t k, prev = p0, nk = (m + WH - 1u) / WH + 1u;
    for (k = 0; k < nk; k++) {
        uint32_t nom = p0 + (uint32_t)((uint64_t)k * WH * cover / m), best = nom, j;
        if (k) {
            int32_t d, bc = INT32_MIN;
            uint32_t want = prev + WH;                      /* the last window's natural continuation */
            for (d = -WD; d <= WD; d++) {
                uint32_t c = (uint32_t)((int32_t)nom + d + (int32_t)len) % len;
                int32_t r = wcorr(x, len, c, want % len);
                if (r > bc)
                    bc = r, best = c;
            }
            ops += (uint64_t)(2 * WD + 1) * (WN / WSTEP) * 3;
        }
        prev = best;
        for (j = 0; j < WN && k * WH + j < m + WN; j++) {
            int32_t w = (first_flat && k == 0 && j < WH) ? 32767 : hann[j];
            z[k * WH + j] += (x[(best + j) % len] * w) >> 15;
        }
        ops += WN * 4;
    }
}
static int32_t zbuf[SR * 12 + 4 * WN];
static void resample_out(const int32_t *z, uint32_t zn, int32_t *y, uint32_t n)   /* 16 kHz -> 44.1 kHz, linear */
{
    const uint32_t step = (uint32_t)(((uint64_t)SR << 16) / FS);
    uint32_t i, q = 0;
    for (i = 0; i < n; i++, q += step) {
        uint32_t a = q >> 16, b = a + 1u < zn ? a + 1u : a;
        y[i] = z[a] + (((z[b] - z[a]) * (int32_t)((q >> 1) & 0x7FFF)) >> 15);
    }
    ops += (uint64_t)n * 8;
}
static void render_wsola(const int16_t *x, uint32_t len, uint32_t bar, int32_t *y, uint32_t n)
{
    uint32_t m = (uint32_t)((uint64_t)n * SR / FS) + 2u, bars = (n + bar - 1u) / bar;
    memset(zbuf, 0, (m + WN) * sizeof zbuf[0]);
    wsola(x, len, 0, len * bars, m, zbuf, 0);
    resample_out(zbuf, m, y, n);
}
/* SLICE+: a slice's first ATK samples as they are (the hit), then WSOLA over the rest of the slice only: every window
 * found after the hit and inside the slice, never before the last one (a hit heard once); faster than the loop: the
 * slice as it is, cut by the next (as SLICE) */
#define ATK 480                  /* 30 ms at 16 kHz */
static void render_slicep(const int16_t *x, uint32_t len, uint32_t bar, int32_t *y, uint32_t n)
{
    uint32_t m = (uint32_t)((uint64_t)n * SR / FS) + 2u, k, j, nsl = (n * 8u + bar - 1u) / bar, sl = len / 8u;
    uint32_t fade = FADE * SR / FS;
    static int32_t sg[SR * 2 + 4 * WN];
    memset(zbuf, 0, (m + WN) * sizeof zbuf[0]);
    for (k = 0; k < nsl; k++) {
        uint32_t t0 = (uint32_t)((uint64_t)k * bar / 8u * SR / FS), t1 = (uint32_t)((uint64_t)(k + 1u) * bar / 8u * SR / FS);
        uint32_t ms = t1 - t0, s0 = (k % 8u) * sl, s1 = s0 + sl, f, prev;
        if (t0 >= m)
            break;
        memset(sg, 0, (ms + WN + fade) * sizeof sg[0]);
        if (ms <= sl || sl <= ATK + WN) {               /* no slower than the loop: the slice as it is */
            for (j = 0; j < ms + fade; j++)
                sg[j] = x[(s0 + j) % len];
            ops += (uint64_t)(ms + fade) * 2;
        } else {
            uint32_t nf = (ms - ATK + WH - 1u) / WH + 1u, cover = sl - ATK - WN, room = ms - ATK;
            for (j = 0; j < ATK + WH; j++)              /* the hit, its last half window fading out */
                sg[j] = j < ATK ? x[s0 + j] : (x[s0 + j] * hann[WH + (j - ATK)]) >> 15;
            prev = s0 + ATK;                           /* frame 0: the hit's continuation, exactly */
            for (f = 0; f < nf; f++) {
                uint32_t nom = s0 + ATK + (uint32_t)((uint64_t)f * WH * cover / room), best = prev, o = ATK + f * WH;
                if (f) {
                    int32_t d, bc = INT32_MIN;
                    uint32_t lo = nom > WD ? nom - WD : 0, hi = nom + WD, want = prev + WH;
                    if (lo < prev)
                        lo = prev;                     /* never back to before the last window: no hit twice */
                    if (hi > s1 - WN)
                        hi = s1 - WN;
                    if (lo > hi)
                        lo = hi;
                    for (d = (int32_t)lo; d <= (int32_t)hi; d++) {
                        int32_t r = wcorr(x, len, (uint32_t)d, want);
                        if (r > bc)
                            bc = r, best = (uint32_t)d;
                    }
                    ops += (uint64_t)(hi - lo + 1u) * (WN / WSTEP) * 3;
                }
                prev = best;
                for (j = 0; j < WN && o + j < ms + WN + fade; j++)
                    sg[o + j] += (x[(best + j) % len] * hann[j]) >> 15;
                ops += WN * 4;
            }
        }
        for (j = 0; j < fade && t0 + j < m; j++)        /* the last slice out over 2 ms under this one's hit */
            zbuf[t0 + j] = (zbuf[t0 + j] * (int32_t)(fade - j)) / (int32_t)fade;
        for (j = fade; j < ms + WN + fade && t0 + j < m + WN; j++)
            zbuf[t0 + j] = 0;
        for (j = 0; j < ms + fade && t0 + j < m + WN; j++)
            zbuf[t0 + j] += sg[j];
    }
    resample_out(zbuf, m, y, n);
}

static int32_t seg[SR * 2 + 4 * WN];
static void render_swsola(const int16_t *x, uint32_t len, uint32_t bar, int32_t *y, uint32_t n)
{
    uint32_t m = (uint32_t)((uint64_t)n * SR / FS) + 2u, k, j, nsl = (n * 8u + bar - 1u) / bar;
    memset(zbuf, 0, (m + WN) * sizeof zbuf[0]);
    for (k = 0; k < nsl; k++) {                       /* slice k % 8 from its trigger to the next */
        uint32_t t0 = (uint32_t)((uint64_t)k * bar / 8u * SR / FS), t1 = (uint32_t)((uint64_t)(k + 1u) * bar / 8u * SR / FS);
        uint32_t ms = t1 - t0, fade = FADE * SR / FS;
        if (t0 >= m)
            break;
        memset(seg, 0, (ms + WN) * sizeof seg[0]);
        wsola(x, len, (k % 8u) * (len / 8u), len / 8u, ms, seg, 1);
        for (j = 0; j < fade && t0 + j < m; j++)        /* the last slice's tail out over 2 ms, this one in at once */
            zbuf[t0 + j] = (zbuf[t0 + j] * (int32_t)(fade - j)) / (int32_t)fade;
        for (j = fade; t0 + j < m + WN && j < ms + fade; j++)
            if (t0 + j < m + WN)
                zbuf[t0 + j] = 0;
        for (j = 0; j < ms + fade && t0 + j < m + WN; j++)
            zbuf[t0 + j] += seg[j];
    }
    resample_out(zbuf, m, y, n);
}

/* ---- measuring ---- */
static double centroid(const int32_t *y, uint32_t n, uint32_t rate)   /* mean over 2048-sample frames, Hz */
{
    static double re[2048], im[2048];
    double num = 0, den = 0;
    uint32_t f, i, k;
    for (f = 0; f + 2048 <= n; f += 2048) {
        for (k = 1; k < 512; k++) {                      /* (bins to rate / 4: enough to see a shift) */
            double r = 0, m = 0, p;
            for (i = 0; i < 2048; i += 2) {
                double w = 0.5 - 0.5 * cos(2 * M_PI * i / 2048), a = 2 * M_PI * k * i / 2048;
                r += y[f + i] * w * cos(a);
                m -= y[f + i] * w * sin(a);
            }
            p = sqrt(r * r + m * m);
            num += p * k * rate / 2048.0;
            den += p;
        }
    }
    (void)re, (void)im;
    return den > 0 ? num / den : 0;
}

static void wav(const char *path, const int32_t *y, uint32_t n)
{
    FILE *f = fopen(path, "wb");
    uint32_t v, i;
    uint16_t h;
    if (!f)
        return;
    fwrite("RIFF", 1, 4, f), v = 36 + n * 2, fwrite(&v, 4, 1, f), fwrite("WAVEfmt ", 1, 8, f);
    v = 16, fwrite(&v, 4, 1, f), h = 1, fwrite(&h, 2, 1, f), fwrite(&h, 2, 1, f);
    v = FS, fwrite(&v, 4, 1, f), v = FS * 2, fwrite(&v, 4, 1, f), h = 2, fwrite(&h, 2, 1, f), h = 16, fwrite(&h, 2, 1, f);
    fwrite("data", 1, 4, f), v = n * 2, fwrite(&v, 4, 1, f);
    for (i = 0; i < n; i++) {
        int16_t s = (int16_t)clamp(y[i], -32768, 32767);
        fwrite(&s, 2, 1, f);
    }
    fclose(f);
}

typedef void (*way_t)(const int16_t *, uint32_t, uint32_t, int32_t *, uint32_t);

int main(int argc, char **argv)
{
    static const way_t W[5] = {render_vari, render_slice, render_wsola, render_swsola, render_slicep};
    static const char *const WNAME[5] = {"vari", "slice", "wsola", "swsola", "slicep"};
    static const uint32_t BPM[4] = {120, 150, 190, 220};
    static int16_t x[SR * 4];
    static int32_t y[MAXOUT], ref[MAXOUT];
    int a;
    uint32_t i;
    for (i = 0; i < WN; i++)
        hann[i] = (int16_t)lround(32767 * (0.5 - 0.5 * cos(2 * M_PI * i / WN)));
    if (argc < 3) {
        printf("stretch_proto OUTDIR NATIVE_BPM:LOOP.raw..\n");
        return 2;
    }
    printf("%-8s %4s  %-7s %9s %9s %8s %6s\n", "loop", "BPM", "way", "ns/sample", "ops/smp", "centroid", "peak");
    for (a = 2; a < argc; a++) {
        char *colon = strchr(argv[a], ':'), name[64], path[512];
        uint32_t native = (uint32_t)atoi(argv[a]), len, b, w;
        const char *base;
        FILE *f = colon ? fopen(colon + 1, "rb") : 0;
        if (!f)
            return 1;
        len = (uint32_t)fread(x, 2, SR * 4, f);
        fclose(f);
        base = strrchr(colon + 1, '/') ? strrchr(colon + 1, '/') + 1 : colon + 1;
        snprintf(name, sizeof name, "%.*s", (int)(strchr(base, '.') - base), base);
        {   /* the loop at its own speed: the reference centroid */
            uint32_t bar0 = (uint32_t)((uint64_t)len * FS / SR);
            render_vari(x, len, bar0, ref, bar0 * 4u);
            printf("%-8s %4u  %-7s %9s %9s %8.0f\n", name, native, "(loop)", "", "", centroid(ref, bar0 * 4u, FS));
        }
        for (b = 0; b < 4; b++) {
            uint32_t bar = 4u * 60u * FS / BPM[b], n = bar * 4u;
            double c0;
            for (w = 0; w < 5; w++) {
                struct timespec t0, t1;
                double ns;
                int32_t pk = 0;
                ops = 0;
                clock_gettime(CLOCK_MONOTONIC, &t0);
                W[w](x, len, bar, y, n);
                clock_gettime(CLOCK_MONOTONIC, &t1);
                ns = ((t1.tv_sec - t0.tv_sec) * 1e9 + (t1.tv_nsec - t0.tv_nsec)) / n;
                for (i = 0; i < n; i++)
                    pk = abs(y[i]) > pk ? abs(y[i]) : pk;
                c0 = centroid(ref, (uint32_t)((uint64_t)len * FS / SR) * 4u, FS);
                printf("%-8s %4u  %-7s %9.1f %9.0f %8.2f %6d\n", name, BPM[b], WNAME[w], ns, (double)ops / n,
                       centroid(y, n, FS) / c0, pk);
                snprintf(path, sizeof path, "%s/%s_%u_%s.wav", argv[1], name, BPM[b], WNAME[w]);
                wav(path, y, n);
            }
        }
    }
    return 0;
}
