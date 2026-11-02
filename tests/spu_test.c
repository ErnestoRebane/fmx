/* SPDX-License-Identifier: GPL-3.0-only */
/* Host test of FMX's PlayStation sound (firmware/src/fx.c spu_tick, eng_sample.c and brkpart.c INTP PSX):
 *   the SPU reverb  every preset bounded under a loud noise and silent after it, its tail as long as its kind (ROOM <
 *                   STUDIO < HALL), DELAY's echo where it should be (290 ms), the two sides different, DAMP darker
 *   INTP PSX        the SPU's Gaussian: a sample and the BREAK softer than LIN (less top), as loud
 * and writes DIR/spu-reverbs.wav: a PSX PAD chord through ROOM, STUDIO, HALL, SPACE, ECHO, DELAY, 3 s each.
 *   tests/spu_test DIR */
#define main hostsim_main
#include <stdarg.h>
#include "hostsim.c"
#undef main

static int fails;
static void check(const char *what, int ok, const char *fmt, ...)
{
    printf("spu: %-77s %s", what, ok ? "ok" : "FAIL");
    if (fmt) {
        va_list ap;
        va_start(ap, fmt);
        printf("  (");
        vprintf(fmt, ap);
        printf(")");
        va_end(ap);
    }
    printf("\n");
    fails += !ok;
}

static void spu_clear(void)
{
    memset(spu_buf, 0, sizeof spu_buf);
    memset(&fx, 0, sizeof fx);
}
/* the reverb's answer (left, right) to `in` (n samples of the mono send), into l[], r[] */
static void reverb(const int32_t *in, uint32_t n, int32_t *l, int32_t *r)
{
    static int32_t z[CTL], wl[CTL], wr[CTL], rin[CTL];
    uint32_t i, k;
    for (i = 0; i < n; i += CTL) {
        for (k = 0; k < CTL; k++)
            rin[k] = i + k < n ? in[i + k] : 0;
        fx_buses(z, z, rin, wl, wr, CTL);
        for (k = 0; k < CTL && i + k < n; k++) {
            l[i + k] = wl[k];
            r[i + k] = wr[k];
        }
    }
}
static double energy(const int32_t *x, uint32_t a, uint32_t b)
{
    double e = 0;
    uint32_t i;
    for (i = a; i < b; i++)
        e += (double)x[i] * x[i];
    return e;
}
/* the time (s) the response's energy in 10 ms windows stays above -40 dB of its loudest window (and above an RMS of
 * 8: the last bits' rounding is not a tail) */
static double tail(const int32_t *x, uint32_t n)
{
    uint32_t w = FS / 100u, i, last = 0;
    double mx = 0, e, floor_e = 64.0 * w;
    for (i = 0; i + w <= n; i += w)
        mx = (e = energy(x, i, i + w)) > mx ? e : mx;
    for (i = 0; i + w <= n; i += w)
        if ((e = energy(x, i, i + w)) > mx * 1e-4 && e > floor_e)
            last = i + w;
    return (double)last / FS;
}
static double hf2(const int32_t *x, uint32_t a, uint32_t b)   /* the top's energy (the second difference's) to the signal's */
{
    double d = 0, s = 0;
    uint32_t i;
    for (i = a + 2u; i < b; i++) {
        double v = (double)x[i] - 2.0 * x[i - 1u] + x[i - 2u];
        d += v * v;
        s += (double)x[i] * x[i];
    }
    return s > 0 ? d / s : 0;
}
static double hf(const int32_t *x, uint32_t a, uint32_t b)   /* the share of the top: the differences against the signal */
{
    double d = 0, s = 0;
    uint32_t i;
    for (i = a + 1u; i < b; i++) {
        d += fabs((double)x[i] - x[i - 1u]);
        s += fabs((double)x[i]);
    }
    return s > 0 ? d / s : 0;
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "build/host";
    static int32_t in[FS * 6], l[FS * 6], r[FS * 6];
    double tl[SPU_N];
    uint32_t p, i, n = FS * 5u;
    host_tracks_init();
    /* ---- every preset: bounded, silent after, its tail */
    for (p = 0; p < SPU_N; p++) {
        uint32_t seed = 0x12345u, pk = 0, after = 0;
        song.g[G_RSIZE] = (int16_t)p;
        song.g[G_RDAMP] = 40;
        spu_clear();
        for (i = 0; i < FS * 2u; i++) {               /* 2 s of loud noise */
            seed = seed * 1664525u + 1013904223u;
            in[i] = (int32_t)(seed >> 16) - 32768;
        }
        memset(in + FS * 2u, 0, (n - FS * 2u) * sizeof in[0]);
        reverb(in, FS * 4u, l, r);
        for (i = 0; i < FS * 2u; i++)
            pk = (uint32_t)abs(l[i]) > pk ? (uint32_t)abs(l[i]) : pk;
        for (i = FS * 2u + FS * 3u / 2u; i < FS * 4u; i++)
            after = (uint32_t)abs(l[i]) > after ? (uint32_t)abs(l[i]) : after;
        {
            char what[96];
            snprintf(what, sizeof what, "%s: loud noise in: bounded; 1.5 s after it: under a tenth", N_SPU[p]);
            check(what, pk < 60000u && pk > 1000u && after < pk / 10u, "peak %u, after %u", pk, after);
        }
        spu_clear();                                  /* the impulse response */
        memset(in, 0, n * sizeof in[0]);
        in[0] = 30000;
        reverb(in, n, l, r);
        tl[p] = tail(l, n);
        {
            int32_t mx = 0, mn = 0;
            char what[96];
            for (i = n - FS / 4u; i < n; i++) {
                mx = l[i] > mx ? l[i] : mx;
                mn = l[i] < mn ? l[i] : mn;
                mx = r[i] > mx ? r[i] : mx;
                mn = r[i] < mn ? r[i] : mn;
            }
            snprintf(what, sizeof what, "%s: 5 s after an impulse: silence, no DC, no low-level ringing", N_SPU[p]);
            check(what, mx - mn <= 2 && mx <= 1 && mn >= -1, "%d .. %d", mn, mx);
        }
        if (p == SPU_DELAY) {
            uint32_t at = 0, mx = 0, want = 6390u * 2u;
            for (i = 100; i < FS / 2u; i++)
                if ((uint32_t)abs(l[i]) > mx) {
                    mx = (uint32_t)abs(l[i]);
                    at = i;
                }
            check("DELAY: the first echo 290 ms after", at + 8u >= want && at <= want + 16u, "at %.1f ms", at * 1000.0 / FS);
        }
        if (p == SPU_HALL) {
            double d = 0, a = 0;
            for (i = 0; i < n; i++) {
                d += fabs((double)l[i] - r[i]);
                a += fabs((double)l[i]);
            }
            check("HALL: the two sides differ (stereo, not a copy)", d > a * 0.5, "%.2f", a > 0 ? d / a : 0);
        }
    }
    printf("spu: tails (-40 dB): ROOM %.2f s, STUDIO %.2f s, HALL %.2f s, SPACE %.2f s, ECHO %.2f s, DELAY %.2f s\n",
           tl[SPU_ROOM], tl[SPU_STUDIO], tl[SPU_HALL], tl[SPU_SPACE], tl[SPU_ECHO], tl[SPU_DELAY]);
    check("the tails as long as their kind: ROOM < STUDIO < HALL < SPACE", tl[SPU_ROOM] < tl[SPU_STUDIO] &&
          tl[SPU_STUDIO] < tl[SPU_HALL] && tl[SPU_HALL] < tl[SPU_SPACE], 0);
    {   /* DAMP: darker */
        double h0, h1;
        song.g[G_RSIZE] = SPU_HALL;
        memset(in, 0, n * sizeof in[0]);
        in[0] = 30000;
        song.g[G_RDAMP] = 0;
        spu_clear();
        reverb(in, FS, l, r);
        h0 = hf(l, FS / 10u, FS);
        song.g[G_RDAMP] = 127;
        spu_clear();
        reverb(in, FS, l, r);
        h1 = hf(l, FS / 10u, FS);
        check("DAMP up: the tail darker", h1 < h0 * 0.9, "top %.3f -> %.3f", h0, h1);
        song.g[G_RDAMP] = GP[G_RDAMP].def;
    }
    /* ---- INTP PSX: a sample softer, as loud */
    {
        double hl, hp, el, ep;
        uint32_t k, b;
        int32_t o[2 * CTL];
        static int32_t cap[2][FS];
        for (k = 0; k < 2u; k++) {
            host_tracks_init();
            host_preset(&trk[1], ENGI_SAMPLE, 5);   /* GRAND PNO: bright */
            trk[1].p[P_E5] = (int16_t)k;
            trk[1].p[P_DLY] = trk[1].p[P_REV] = trk[1].p[P_CHOR] = 0;
            trk_note_on(&trk[1], 60, 100);
            for (b = 0; b < FS / CTL; b++) {
                mix_block(o, CTL);
                for (i = 0; i < CTL; i++)
                    cap[k][b * CTL + i] = acc[0].l[i];
            }
        }
        hl = hf2(cap[0], 2000, FS / 2u);
        hp = hf2(cap[1], 2000, FS / 2u);
        el = energy(cap[0], 2000, FS / 2u);
        ep = energy(cap[1], 2000, FS / 2u);
        check("INTP PSX: a sample softer than LIN (less top)", hp < hl * 0.8, "top %.5f -> %.5f", hl, hp);
        check("INTP PSX: as loud (within 2 dB)", ep > el * 0.63 && ep < el * 1.58, "%.2f dB", 10 * log10(ep / el));
    }
    {   /* the BREAK: softer too */
        double hl, hp;
        uint32_t k, b;
        int32_t o[2 * CTL];
        static int32_t cap[2][FS];
        for (k = 0; k < 2u; k++) {
            host_tracks_init();
            BPV(BP_INTP) = (int16_t)k;
            BPV(BP_PITCH) = 7;
            TBRK->p[P_REV] = TBRK->p[P_DLY] = 0;
            memset(&bp.v, 0, sizeof bp.v);
            brk_hit(1, LV_NORM);
            for (b = 0; b < FS / CTL; b++) {
                mix_block(o, CTL);
                for (i = 0; i < CTL; i++)
                    cap[k][b * CTL + i] = acc[0].l[i];
            }
        }
        hl = hf(cap[0], 200, FS / 4u);
        hp = hf(cap[1], 200, FS / 4u);
        check("BREAK INTP PSX: softer than LIN", hp < hl * 0.97, "top %.3f -> %.3f", hl, hp);
    }
    /* ---- the CHOP kits: every hit heard, bounded, each its own; a demo of them all */
    {
        static const char *const KITS[2] = {"RAVEHITS", "ANIME"};   /* the sound packs, in USR1 and USR2 */
        char path[512];
        FILE *w;
        uint32_t kk, h, b, total = 0, bad = 0, same = 0, pi;
        uint64_t sig[32] = {0};
        snprintf(path, sizeof path, "%s/chop-kits.wav", dir);
        if (!(w = fopen(path, "wb")))
            return 1;
        wav_hdr(w, 22u * (FS / 2u + FS / 8u));
        for (kk = 0; kk < 2u; kk++) {
            uint32_t base = 0, n = 0;
            host_tracks_init();
            if (!host_pack(kk, KITS[kk]))
                bad++;
            for (pi = 0; pi < ENG_SAMPLE.npresets && !str_eq(ENG_SAMPLE.presets[pi].name, "CHOP KIT"); pi++)
                ;
            host_preset(&trk[2], ENGI_SAMPLE, pi);
            trk[2].p[P_E0] = (int16_t)(SMP_NSETS + kk);
            trk[2].p[P_REV] = trk[2].p[P_DLY] = trk[2].p[P_CHOR] = 0;
            if (!smp_kit((uint32_t)trk[2].p[P_E0] % SMP_NALL, &base, &n))
                bad++;
            for (h = 0; h < n; h++) {
                uint32_t pk = 0;
                uint64_t g = 1469598103934665603ull;
                trk_note_on(&trk[2], base + h, 100);
                for (b = 0; b < (FS / 2u + FS / 8u) / CTL; b++) {
                    int32_t o[2 * CTL];
                    if (b == FS / 2u / CTL)
                        trk_note_off(&trk[2], base + h);
                    mix_block(o, CTL);
                    for (i = 0; i < CTL; i++) {
                        pk = (uint32_t)abs(o[2 * i]) > pk ? (uint32_t)abs(o[2 * i]) : pk;
                        g = (g ^ (uint64_t)(o[2 * i] >> 6)) * 1099511628211ull;
                        wav_put(w, o[2 * i], o[2 * i + 1]);
                    }
                }
                bad += pk < 1500u || pk > 32767u;
                for (i = 0; i < total; i++)
                    same += sig[i] == g;
                if (total < 32u)
                    sig[total++] = g;
            }
        }
        fclose(w);
        check("CHOP kits (the packs): RAVE HITS 9 and ANIME KIT 13 hits, every one heard and bounded", total == 22u && !bad, "%u hits, %u bad", total, bad);
        check("CHOP kits: every hit its own sound", !same, 0);
        printf("spu: demo chop-kits.wav: RAVE HITS, then ANIME KIT, a hit each half second\n");
    }
    /* ---- the demo: a PSX PAD chord through every preset */
    {
        char path[512];
        FILE *w;
        uint32_t f, pk = 0, b;
        static const uint8_t CH[4] = {57, 60, 64, 67};
        snprintf(path, sizeof path, "%s/spu-reverbs.wav", dir);
        if (!(w = fopen(path, "wb")))
            return 1;
        wav_hdr(w, SPU_N * 3u * FS);
        for (p = 0; p < SPU_N; p++) {
            host_tracks_init();
            host_preset(&trk[1], ENGI_SAMPLE, 0);   /* PSX PAD */
            trk[1].p[P_REV] = 90;
            trk[1].p[P_ATK] = 10;
            song.g[G_RSIZE] = (int16_t)p;
            spu_clear();
            for (i = 0; i < 4u; i++)
                trk_note_on(&trk[1], CH[i], 100);
            for (f = 0, b = 0; f < 3u * FS; f += CTL, b++) {
                int32_t o[2 * CTL];
                if (b == FS / 2u / CTL)
                    for (i = 0; i < 4u; i++)
                        trk_note_off(&trk[1], CH[i]);
                mix_block(o, CTL);
                for (i = 0; i < CTL; i++) {
                    pk = (uint32_t)abs(o[2 * i]) > pk ? (uint32_t)abs(o[2 * i]) : pk;
                    wav_put(w, o[2 * i], o[2 * i + 1]);
                }
            }
        }
        fclose(w);
        printf("spu: demo spu-reverbs.wav: a PSX PAD chord, 0.5 s held, through ROOM .. DELAY, peak %u\n", pk);
        fails += pk < 2000u;
    }
    printf(fails ? "spu: %d FAILED\n" : "spu: all ok\n", fails);
    return fails != 0;
}
