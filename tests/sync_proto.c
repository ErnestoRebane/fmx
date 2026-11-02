/* SPDX-License-Identifier: GPL-3.0-only */
/* SYNC's slave saw, before and after the two-sample BLEP (eng_analog.c analog_lead; after Braids' RenderSaw): a
 * listening test and a measurement before the firmware. The slave alone (MIX 0), no filter, no drive: the part that
 * changed.
 *   1. aliasing: the master at an exact rational increment (2^32 a / 256: the signal repeats every 256 samples), so
 *      over 4096 samples every true harmonic falls on a bin that is a multiple of 16 a and every alias falls between
 *      them. Printed: the energy off the harmonics against the whole, old and new, in dB.
 *   2. level: peak, RMS, DC of the same renders.
 *   3. WAVs for listening (build/sync/listen): the sweep a SYNC LEAD makes (the slave 0 .. +24 semitones over 3 s)
 *      on C2, C4 and C6, old and new.
 * cc -O2 -Ibuild/gen -Ifirmware/src -o build/host/sync_proto tests/sync_proto.c -lm && build/host/sync_proto build/sync/listen */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "felucca_tables.h"
static inline int32_t clamp(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : v > hi ? hi : v; }
static inline int32_t mulq15(int32_t a, int32_t b) { return (a * b) >> 15; }
static inline int32_t blep(uint32_t ph, uint32_t inc)            /* dsp.c's */
{
    uint32_t d = inc >> 15;
    int32_t x;
    if (!d)
        return 0;
    if (ph < inc) {
        x = (int32_t)(ph / d);
        return x + x - ((x * x) >> 15) - 32768;
    }
    if (ph > 0xFFFFFFFFu - inc) {
        x = -(int32_t)((0xFFFFFFFFu - ph) / d);
        return ((x * x) >> 15) + x + x + 32768;
    }
    return 0;
}
static inline int32_t osc_saw(uint32_t ph, uint32_t inc) { return ((int32_t)(ph >> 16) - 32768) - blep(ph, inc); }

typedef struct { uint32_t ph0, ph1; int32_t nx; } osc_t;

static int32_t old_tick(osc_t *o, uint32_t inc1, uint32_t inc2)  /* HEAD's analog_lead, SYNC, MIX 0 */
{
    int32_t s = osc_saw(o->ph1, inc2);
    o->ph1 += inc2;
    if (o->ph0 + inc1 < o->ph0)
        o->ph1 = (uint32_t)(((uint64_t)(o->ph0 + inc1) * inc2) / inc1);
    o->ph0 += inc1;
    return s;
}

/* the firmware's, copied: keep in step with eng_analog.c */
static inline uint32_t sync_frac(uint32_t ph, uint32_t rcp)
{
    uint32_t t = (ph >> 16) * rcp + (((ph & 0xFFFFu) * rcp) >> 16);
    return t > 65535u ? 65535u : t;
}
static inline int32_t blep_this(uint32_t t) { return (int32_t)(t * t >> 18); }
static inline int32_t blep_next(uint32_t t) { t = 65535u - t; return -(int32_t)(t * t >> 18); }

static int32_t new_tick(osc_t *o, uint32_t inc1, uint32_t inc2, uint32_t rcp1, uint32_t rcp2)
{
    int32_t cur = o->nx;
    uint32_t ph1n = o->ph1 + inc2, tm = 0;
    int reset = 0, wrap_before = 0;
    o->nx = 0;
    if (o->ph0 + inc1 < o->ph0) {
        uint32_t at;
        int32_t jump;
        tm = sync_frac(o->ph0 + inc1, rcp1);
        at = o->ph1 + (65535u - tm) * (inc2 >> 16);
        wrap_before = at < o->ph1;
        jump = (int32_t)(at >> 17);
        cur -= jump * blep_this(tm) >> 15;
        o->nx -= jump * blep_next(tm) >> 15;
        reset = 1;
    }
    if ((wrap_before || !reset) && ph1n < o->ph1) {
        uint32_t ts = sync_frac(ph1n, rcp2);
        cur -= blep_this(ts);
        o->nx -= blep_next(ts);
    }
    if (reset)
        ph1n = tm * (inc2 >> 16);
    o->nx += (int32_t)(ph1n >> 17);
    o->ph1 = ph1n;
    o->ph0 += inc1;
    return (cur - 16384) * 2;
}

#define N 4096
static double alias_db(const int32_t *x, uint32_t step)          /* off-harmonic energy / all (DC out), dB */
{
    double tot = 0, off = 0;
    uint32_t k, i;
    for (k = 1; k <= N / 2; k++) {
        double re = 0, im = 0, p;
        for (i = 0; i < N; i++) {
            double w = 2 * M_PI * (double)((k * i) % N) / N;
            re += x[i] * cos(w);
            im -= x[i] * sin(w);
        }
        p = re * re + im * im;
        tot += p;
        if (k % step)
            off += p;
    }
    return 10 * log10(off / tot);
}

static void stats(const int32_t *x, uint32_t n, int32_t *pk, double *rms, double *dc)
{
    double s = 0, s2 = 0;
    uint32_t i;
    *pk = 0;
    for (i = 0; i < n; i++) {
        int32_t a = x[i] < 0 ? -x[i] : x[i];
        if (a > *pk)
            *pk = a;
        s += x[i];
        s2 += (double)x[i] * x[i];
    }
    *rms = 20 * log10(sqrt(s2 / n) / 32768);
    *dc = s / n;
}

static void wav(const char *path, const int16_t *x, uint32_t n)
{
    FILE *f = fopen(path, "wb");
    uint32_t v;
    uint16_t h;
    if (!f) {
        printf("cannot write %s\n", path);
        return;
    }
    fwrite("RIFF", 1, 4, f), v = 36 + n * 2, fwrite(&v, 4, 1, f), fwrite("WAVEfmt ", 1, 8, f);
    v = 16, fwrite(&v, 4, 1, f), h = 1, fwrite(&h, 2, 1, f), fwrite(&h, 2, 1, f);
    v = FS, fwrite(&v, 4, 1, f), v = FS * 2, fwrite(&v, 4, 1, f), h = 2, fwrite(&h, 2, 1, f), h = 16, fwrite(&h, 2, 1, f);
    fwrite("data", 1, 4, f), v = n * 2, fwrite(&v, 4, 1, f), fwrite(x, 2, n, f);
    fclose(f);
}

int main(int argc, char **argv)
{
    static const uint32_t A[] = {3, 7, 13, 29};                 /* master 517, 1206, 2240, 4996 Hz */
    static const double R[] = {1.37, 2.71, 4.13};               /* the slave's ratio */
    static int32_t xo[N], xn[N];
    static int16_t w[FS * 3];
    const char *dir = argc > 1 ? argv[1] : "build/sync/listen";
    uint32_t a, r, i;
    printf("the ceiling: a plain saw (no sync) at the same pitches, naive / dsp.c's polyBLEP\n");
    for (a = 0; a < 4; a++) {
        uint32_t inc = (uint32_t)(4294967296.0 * A[a] / 256), ph = 0;
        for (i = 0; i < N; i++, ph += inc) {
            xo[i] = (int32_t)(ph >> 16) - 32768;
            xn[i] = osc_saw(ph, inc);
        }
        printf("  %4.0f Hz         naive %6.1f  polyBLEP %6.1f\n", FS * A[a] / 256.0, alias_db(xo, 16 * A[a]),
               alias_db(xn, 16 * A[a]));
    }
    printf("aliasing (energy off the harmonics, dB; lower is cleaner)    level (peak, RMS dBFS, DC)\n");
    for (a = 0; a < 4; a++)
        for (r = 0; r < 3; r++) {
            uint32_t inc1 = (uint32_t)(4294967296.0 * A[a] / 256), inc2 = (uint32_t)(inc1 * R[r]);
            uint32_t rcp1 = 0xFFFFFFFFu / inc1, rcp2 = 0xFFFFFFFFu / inc2;
            osc_t o = {0, 0x40000000u, 0}, q = {0, 0x40000000u, (int32_t)(0x40000000u >> 17)};
            int32_t pko, pkn;
            double rmo, rmn, dco, dcn;
            for (i = 0; i < 1024; i++)                          /* settle: past the start */
                old_tick(&o, inc1, inc2), new_tick(&q, inc1, inc2, rcp1, rcp2);
            for (i = 0; i < N; i++) {
                xo[i] = old_tick(&o, inc1, inc2);
                xn[i] = new_tick(&q, inc1, inc2, rcp1, rcp2);
            }
            stats(xo, N, &pko, &rmo, &dco);
            stats(xn, N, &pkn, &rmn, &dcn);
            printf("  %4.0f Hz x %.2f   old %6.1f  new %6.1f  (%+5.1f)    old %5d %5.1f %6.0f   new %5d %5.1f %6.0f\n",
                   FS * A[a] / 256.0, R[r], alias_db(xo, 16 * A[a]), alias_db(xn, 16 * A[a]),
                   alias_db(xn, 16 * A[a]) - alias_db(xo, 16 * A[a]), pko, rmo, dco, pkn, rmn, dcn);
        }
    {                                                           /* 4. cost: ns per sample on this Mac, best of 5 */
        uint32_t inc1 = pitch_inc(60 * 16), inc2 = pitch_inc(60 * 16 + 150), rcp1 = 0xFFFFFFFFu / inc1;
        uint32_t rcp2 = 0xFFFFFFFFu / inc2, ver, rep;
        for (ver = 0; ver < 2; ver++) {
            double best = 1e9;
            for (rep = 0; rep < 5; rep++) {
                osc_t o = {0, 0x40000000u, 0};
                volatile int32_t sink = 0;
                struct timespec t0, t1;
                clock_gettime(CLOCK_MONOTONIC, &t0);
                for (i = 0; i < 20000000u; i++)
                    sink += ver ? new_tick(&o, inc1, inc2, rcp1, rcp2) : old_tick(&o, inc1, inc2);
                clock_gettime(CLOCK_MONOTONIC, &t1);
                double ns = ((t1.tv_sec - t0.tv_sec) * 1e9 + (t1.tv_nsec - t0.tv_nsec)) / 20e6;
                if (ns < best)
                    best = ns;
            }
            printf("cost %s: %.2f ns / sample (C4, the slave +9 semitones)\n", ver ? "new" : "old", best);
        }
    }
    {
        static const int NOTE[] = {36, 60, 84};
        static const char *const NN[] = {"C2", "C4", "C6"};
        uint32_t k, ver;
        for (k = 0; k < 3; k++)
            for (ver = 0; ver < 2; ver++) {
                osc_t o = {0, 0x40000000u, (int32_t)(0x40000000u >> 17)};
                char path[256];
                uint32_t inc1 = pitch_inc((uint32_t)NOTE[k] * 16u), rcp1 = 0xFFFFFFFFu / inc1;
                for (i = 0; i < FS * 3; i++) {
                    uint32_t p2 = (uint32_t)clamp(NOTE[k] * 16 + (int32_t)(i * 384u / (FS * 3)), 0, 2047);
                    uint32_t inc2 = pitch_inc(p2);
                    int32_t s = ver ? new_tick(&o, inc1, inc2, rcp1, 0xFFFFFFFFu / inc2) : old_tick(&o, inc1, inc2);
                    w[i] = (int16_t)clamp(s >> 2, -32768, 32767);   /* -12 dB: a bare saw is loud */
                }
                snprintf(path, sizeof path, "%s/sync_%s_%s.wav", dir, NN[k], ver ? "new" : "old");
                wav(path, w, FS * 3);
            }
        printf("wrote %s/sync_{C2,C4,C6}_{old,new}.wav (the slave swept 0 .. +24 semitones over 3 s)\n", dir);
    }
    return 0;
}
