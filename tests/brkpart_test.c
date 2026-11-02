/* SPDX-License-Identifier: GPL-3.0-only */
/* The BREAK track (track 4: firmware/src/brkpart.c, played by seq.c seq_tick, mixed by fx.c mix_part) on the Mac, the
 * firmware's sources through hostsim.c:
 *   build/host/brkpart_test LOOP LOOPS DEMO_DIR
 * Built in: AMEN 2, 3, 4 (loops 0..2, one bar each at 137 / 137 / 139 BPM). USR1: the standard set
 * (tests/fixtures/kan/breaks_standard.bin: AMEN 2, 3, 4 again; local only) = BR1, BR2, BR3; USR2: LOOP.hdr / LOOP.bin (tests/slice_loop.py's two
 * bars at 96 BPM, a slot named BR1) = BR4; USR3: LOOPS.hdr / LOOPS.bin (three loops: tests/break_loops.py) = BR5..BR7.
 * 0. the loops: the built-in ones and every zone of the slots, their slice tables (grid of decoder states == the
 *    decoder's), the guess of their bars, A..D BARS, the waveform, the banks following a slot that is written or erased.
 * 1. the clock: the sequencer's steps (transport_req, mix_block), the first trigger forced (slice 0), the cadence of
 *    LEN, the step's DIV.
 * 2. the generator (BB Gen's slice_select): CMPLX 0 bypasses it; ANCHR holds beats 0 and 4; CMPLX 100 draws every slice
 *    about as often; ROLL only repeats, walks +-1 or jumps 2..4; ROLL and FILL come in with CMPLX; FILL moves the last bar
 *    of a phrase; the retrigger's chance a bar becomes a chance a trigger; a step's lock of CMPLX.
 * 3. the sound: CMPLX 0 and every step on plays the loop as it is; a step off is a hole (and the break goes on under it);
 *    a pinned step plays its own slice; a step's retrigger, level, pitch and reverse; the live keys (held slices,
 *    reverse, half speed, stutter); loops B, C, D by chance at the end of a phrase; the retrigger replays the head of
 *    a slice; mute and solo through mix_part; STOP; bounded.
 * 4. hits from MIDI and the keys, recorded into the steps (REC on the track), not sounded twice by their step.
 * 5. the project: capture / apply of the steps and the generator's parameters.
 * 6. demos into DEMO_DIR. */
#include <stdarg.h>
#include <stdint.h>
#include <sys/stat.h>
#define main hostsim_main
#include "hostsim.c"
#undef main
#define PROJ_HOST 1
#include "../firmware/src/project.c"

static int fails;
static void check(const char *what, int ok, const char *fmt, ...)
{
    printf("brkpart: %-72s %s", what, ok ? "ok" : "FAIL");
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

#define BRK_USR(k) (BP_NB + (k))                         /* the user loops: BR1 = BRK_USR(0) */
#define BP(i) (TBRK->p[BPP[i]])                           /* a generator parameter, as the UI writes it */
/* a fresh track: the defaults, every step on, the transport stopped, the generator seeded the same every time, the
 * mix neutral (no sends, centre, no click) */
static void fresh(void)
{
    host_tracks_init();
    song.playing = 0;
    transport_req = 0;
    memset(&bp, 0, sizeof bp);
    bp.pending_bank = -1;
    bp.pin_next = -1;
    bp.st_slice = -1;
    bp.rng = 0x2545F491u;
    clk_beat = clk_pos = 0;
    song.g[G_BPM] = 120;
    song.g[G_CLOCK] = 0;
    song.solo = 0;
    song.rec = 0;
    TBRK->p[P_SLEN] = 16;
    TBRK->p[P_REV] = TBRK->p[P_DLY] = TBRK->p[P_CHOR] = 0;
    TBRK->p[P_PAN] = 0;
    TBRK->p[P_LEVEL] = 70;                          /* (the master's soft knee stays out of it: the loop is loud) */
    BP(BP_MANGLE) = 0;                              /* (the power-on style, JUNGLE, mangles a little: not here) */
    BP(BP_INTP) = 0;                                /* (LIN: the correlations below compare against a linear decode) */
    TBRK->p[P_SDIV] = 2;
    brkp_poll();
}
static void steps_on(uint32_t mask)                 /* the first 16 steps: on where mask has a bit */
{
    uint32_t i;
    for (i = 0; i < 16u; i++)
        TBRK->bstep[i].on = (uint8_t)((mask >> i) & 1u);
}
static void set(int cmplx, int anchr, int roll, int fill, int rtg)
{
    BP(BP_CMPLX) = (int16_t)cmplx;
    BP(BP_ANCHOR) = (int16_t)anchr;
    BP(BP_ROLL) = (int16_t)roll;
    BP(BP_FILL) = (int16_t)fill;
    BP(BP_R2) = BP(BP_R3) = BP(BP_R4) = BP(BP_R8) = (int16_t)rtg;
}

/* the transport at once (the sequencer's start), then n blocks; the triggers fired are logged with the step playing */
typedef struct { uint32_t block; int8_t slice, bank, div, forced; uint8_t step; int32_t amp; int8_t semi, rev, mg; } trig_t;
static trig_t trigs[4096];
static uint32_t ntrig;
static int32_t pcm[44100 * 20][2], pre[44100 * 20];   /* the master out, and the mix before the master (mix_l: what the track gave it) */
static uint32_t npcm;
static void play(uint32_t blocks, int record)
{
    uint32_t b, last = bp.st_trigs;
    if (!song.playing) {
        transport_req = 1;
        ntrig = 0;
        npcm = 0;
    }
    for (b = 0; b < blocks; b++) {
        int32_t o[2 * CTL];
        uint32_t i;
        mix_block(o, CTL);
        if (bp.st_trigs != last) {
            uint32_t k = bp.st_trigs - last;
            last = bp.st_trigs;
            if (k == 1u && ntrig < 4096u) {
                const bpv_t *v = &bp.v[bp.cur];
                trigs[ntrig].block = b;
                trigs[ntrig].slice = bp.st_slice;
                trigs[ntrig].bank = bp.st_bank;
                trigs[ntrig].div = bp.st_div;
                trigs[ntrig].forced = (int8_t)bp.st_forced;
                trigs[ntrig].step = (uint8_t)TBRK->seq_idx;
                trigs[ntrig].amp = v->amp;
                trigs[ntrig].semi = v->semi;
                trigs[ntrig].mg = (int8_t)bp.st_mangle;
                trigs[ntrig++].rev = (int8_t)v->rev;
            }
        }
        if (record)
            for (i = 0; i < CTL && npcm < sizeof pcm / sizeof pcm[0]; i++, npcm++) {
                pre[npcm] = acc[0].l[i];
                pcm[npcm][0] = o[2 * i];
                pcm[npcm][1] = o[2 * i + 1];
            }
    }
}
#define BAR_BLOCKS(bpm) ((uint32_t)(4.0 * 60.0 / (bpm) * FS / CTL + 0.5))
static void stop(void)
{
    transport_req = 2;
    play(8, 0);
}
static double rms_last(uint32_t n)                  /* the mix before the master, its last n samples */
{
    double e = 0;
    uint32_t i;
    for (i = npcm - n; i < npcm; i++)
        e += (double)pre[i] * pre[i];
    return sqrt(e / n);
}
/* the best correlation of pcm[from..to) with ref (a lag of up to +-maxlag) */
static double corr(const float *ref, uint32_t from, uint32_t to, int32_t maxlag)
{
    double best = 0;
    int32_t lag;
    uint32_t i;
    for (lag = -maxlag; lag <= maxlag; lag++) {
        double xy = 0, xx = 0, yy = 0;
        for (i = from; i < to; i++) {
            double x = ref[i], y = pcm[(uint32_t)((int32_t)i + lag)][0];
            xy += x * y;
            xx += x * x;
            yy += y * y;
        }
        if (xx > 0 && yy > 0 && xy / sqrt(xx * yy) > best)
            best = xy / sqrt(xx * yy);
    }
    return best;
}
/* bank 0's loop at 44.1 kHz, 0.5 source samples a sample, linearly (its own tempo) */
static void ref_of(float *ref, uint32_t frames)
{
    slc_dec_t d;
    int32_t x0 = 0, x1;
    uint32_t i;
    int32_t lc[2] = {0, 0};                          /* (the BREAK's role low cut, fx.c ROLE_CUT: the reference through it) */
    slc_dec_at(&d, 0, 0);
    for (i = 0; i < bpb[0].t.len && 2u * i + 1u < frames; i++) {
        x1 = slc_dec_next(&bpb[0].t, &d);
        ref[2u * i] = (float)lowcut_sh(x0 * 64, lc, ROLE_CUT[TRK_BRK]);
        ref[2u * i + 1u] = (float)lowcut_sh((x0 + x1) * 32, lc, ROLE_CUT[TRK_BRK]);
        x0 = x1;
    }
}

int main(int argc, char **argv)
{
    const char *loop = argc > 1 ? argv[1] : "build/host/slice_loop";
    const char *loops = argc > 2 ? argv[2] : "build/host/break_loops";
    const char *dir = argc > 3 ? argv[3] : "build/brkpart_demo";
    char path[512];
    uint32_t i, k, bad;
    long n;
    FILE *f;

    {   /* the slots: USR1 the standard set (header area + data in one file), USR2 and USR3 a .hdr and a .bin each */
        const char *std = argc > 4 ? argv[4] : "tests/fixtures/kan/breaks_standard.bin";
        uint8_t *u = host_slot(0);
        f = fopen(std, "rb");
        n = f ? (long)fread(u, 1, SMP_USER_SIZE, f) : -1;
        if (f)
            fclose(f);
        if (n <= (long)SMP_USER_DATA) {
            printf("brkpart: no standard set (%s: tools/upload_breaks.py --standard-image)\n", std);
            return 1;
        }
        for (k = 1; k < 3u; k++) {
            const char *pre = k == 1u ? loop : loops;
            snprintf(path, sizeof path, "%s.hdr", pre);
            f = fopen(path, "rb");
            n = f ? (long)fread(u + k * SMP_USER_SIZE, 1, SMP_USER_DATA, f) : -1;
            if (f)
                fclose(f);
            snprintf(path, sizeof path, "%s.bin", pre);
            f = n == (long)sizeof(smp_user_hdr_t) ? fopen(path, "rb") : 0;
            n = f ? (long)fread(u + k * SMP_USER_SIZE + SMP_USER_DATA, 1, SMP_USER_SIZE - SMP_USER_DATA, f) : -1;
            if (f)
                fclose(f);
            if (n <= 0) {
                printf("brkpart: no %s.hdr / .bin (tests/slice_loop.py + fm1_sample_upload.py build BR1; tests/break_loops.py)\n", pre);
                return 1;
            }
        }
    }
    smp_pool_scan();
    check("USR1, USR2, USR3 valid", usr_nz[0] == 3u && usr_nz[1] == 1u && usr_nz[2] == 3u, "%u + %u + %u zones", usr_nz[0], usr_nz[1], usr_nz[2]);

    /* 0: the loops */
    {
        char nm[8];
        uint32_t g;
        fresh();
        check("the loops: AMEN 2, 3, 4 built in, BR1..BR3 (USR1), BR4 (USR2), BR5..BR7 (USR3)", BP_NB == 3u && brkp_nloops() == 10u, "%u built in, %u", BP_NB, brkp_nloops());
        brkp_loop_name(0, nm);
        bad = strcmp(nm, "AMEN 2");
        brkp_loop_name(2, nm);
        bad |= strcmp(nm, "AMEN 4");
        brkp_loop_name(BRK_USR(0), nm);
        bad |= strcmp(nm, "BR1");
        brkp_loop_name(BRK_USR(6), nm);
        bad |= strcmp(nm, "BR7");
        check("their names: AMEN 2, AMEN 4, BR1, BR7", !bad, 0);
        {
            uint32_t slot = 9, zone = 9;
            check("BR1 is USR1's zone 0, BR4 USR2's zone 0, BR6 USR3's zone 1; BR8 and a built-in loop none",
                  bp_loop_zone(BRK_USR(0), &slot, &zone) && slot == 0 && zone == 0 && bp_loop_zone(BRK_USR(3), &slot, &zone) && slot == 1 &&
                  zone == 0 && bp_loop_zone(BRK_USR(5), &slot, &zone) && slot == 2 && zone == 1 && !bp_loop_zone(BRK_USR(7), &slot, &zone) &&
                  !bp_loop_zone(1, &slot, &zone), 0);
        }
        check("LOOP A..D by default AMEN 2, 3, 4 and BR1: built, one bar each (AMEN: by definition, BR1: guessed)",
              BP(BP_LOOPA) == 0 && BP(BP_LOOPD) == (int16_t)BRK_USR(0) && bpb[0].src == &bpb[0].t && bpb[1].src == &bpb[1].t &&
              bpb[2].src == &bpb[2].t && bpb[3].src == &bpb[3].t && bpb[0].t.len == 38628u && bpb[2].t.len == 38072u &&
              bpb[3].t.len == 38628u && bpb[0].auto_bars == 1 && bpb[3].auto_bars == 1 && bpb[0].t.rate == 32768u,
              "%u, %u, %u, %u samples", bpb[0].t.len, bpb[1].t.len, bpb[2].t.len, bpb[3].t.len);
        BP(BP_LOOPA) = (int16_t)BRK_USR(3);
        BP(BP_LOOPB) = (int16_t)BRK_USR(5);
        brkp_poll();
        check("LOOP A = BR4, LOOP B = BR6: both built", bpb[0].src == &bpb[0].t && bpb[1].src == &bpb[1].t && bpb[0].t.len == 110250u &&
              bpb[1].t.len == 27562u, "%u, %u samples", bpb[0].t.len, bpb[1].t.len);
        for (bad = 0, g = 0; g < 4u; g++) {            /* the grid of decoder states == the decoder's */
            const slc_src_t *t = &bpb[g].t;
            slc_dec_t d;
            uint32_t pos, k2 = 0;
            slc_dec_at(&d, 0, 0);
            for (pos = 0; pos < t->len; pos++) {
                while (k2 < SLC_GRID && slc_gpos(t, k2) == pos)
                    bad += t->grid[k2++] != slc_dec_st(&d);
                slc_dec_next(t, &d);
            }
            bad += SLC_GRID - k2;
        }
        check("their slice tables (BR4, BR6, AMEN 4, BR1): every grid state == the decoder's", !bad, "%u differ", bad);
        check("the guess of the bars: 5 s 2, 2.5 s 1, 1.25 s 1, 1.875 s 1, 1 s 1, 13.6 s 8, 2.82 s 2 (170, not 85)",
              bp_guess_bars(110250u, 32768u) == 2u && bp_guess_bars(55125u, 32768u) == 1u && bp_guess_bars(27562u, 32768u) == 1u &&
              bp_guess_bars(41343u, 32768u) == 1u && bp_guess_bars(44100u, 32768u) == 1u && bp_guess_bars(300000u, 32768u) == 8u &&
              bp_guess_bars(62250u, 32768u) == 2u,
              "%u %u %u %u", bp_guess_bars(110250u, 32768u), bp_guess_bars(55125u, 32768u), bp_guess_bars(27562u, 32768u), bp_guess_bars(41343u, 32768u));
        check("A's bars are 2 (guessed), B's 1; A BARS 4 and back to AUTO: 4 and 2; D BARS 8: 8", brkp_bank_bars(0) == 2u && brkp_bank_bars(1) == 1u &&
              (BP(BP_BARSA) = 3, brkp_bank_bars(0) == 4u) && (BP(BP_BARSA) = 0, brkp_bank_bars(0) == 2u) &&
              (BP(BP_BARSD) = 4, brkp_bank_bars(3) == 8u), 0);
        {
            uint32_t mx = 0, nz = 0;
            for (g = 0; g < BP_WPEAKS; g++) {
                mx = bpb[0].peaks[g] > mx ? bpb[0].peaks[g] : mx;
                nz += bpb[0].peaks[g] != 0u;
            }
            check("the waveform: 240 columns, the largest 255, most columns not silent", mx == 255u && nz > BP_WPEAKS / 2u, "%u non-zero", nz);
        }
        {   /* only slots named BR... are loops: an instrument's zones are not */
            smp_user_hdr_t *h = (smp_user_hdr_t *)(host_slot(1));
            char c0 = h->name[0];
            h->name[0] = 'P';
            check("a slot named otherwise (an instrument) has no loops", brkp_nloops() == 9u, "%u", brkp_nloops());
            h->name[0] = c0;
            check("... a slot named BR... does", brkp_nloops() == 10u, "%u", brkp_nloops());
        }
        {   /* a slot written: the banks follow */
            uint32_t gen = smp_user_gen;
            smp_pool_scan();
            brkp_poll();
            check("a slot scanned again: the banks are built again", smp_user_gen != gen && bpb[1].src == &bpb[1].t && bpb[1].t.len == 27562u &&
                  bpb[0].key == (BRK_USR(3) | smp_user_gen << 8), 0);
        }
        BP(BP_LOOPB) = (int16_t)BRK_USR(6);
        brkp_poll();
        check("LOOP B = BR7: built", bpb[1].src == &bpb[1].t && bpb[1].t.len == 41343u, "%u", bpb[1].t.len);
        BP(BP_LOOPB) = (int16_t)BRK_USR(30);
        brkp_poll();
        check("a loop there is not (BR31): no source, the bank is silent", bpb[1].src == 0, 0);
        {   /* USR3 erased: its loops are gone, A is BR4 still */
            uint8_t nz1 = usr_nz[2];
            usr_nz[2] = 0;
            smp_user_gen++;
            BP(BP_LOOPB) = (int16_t)BRK_USR(5);
            brkp_poll();
            check("USR3 erased: BR6 is no more (no source), BR4 plays", brkp_nloops() == 7u && bpb[1].src == 0 && bpb[0].src == &bpb[0].t, "%u loops", brkp_nloops());
            usr_nz[2] = nz1;
            smp_user_gen++;
            brkp_poll();
            check("... and back", brkp_nloops() == 10u && bpb[1].src == &bpb[1].t, 0);
        }
    }

    /* 1: the clock: the sequencer's steps */
    {
        fresh();
        set(50, 0, 0, 0, 0);
        play(BAR_BLOCKS(120) * 2u, 0);
        check("the first trigger after PLAY: slice 0, forced, no replay, on step 0", ntrig && trigs[0].forced && trigs[0].slice == 0 && trigs[0].div == 0 &&
              trigs[0].step == 0, "%u triggers", ntrig);
        /* LEN AUTO, a loop of a bar: a trigger every 1/8 bar = every 2 steps, 8 a bar */
        bad = 0;
        for (i = 1; i < ntrig && i < 16u; i++) {
            uint32_t db = trigs[i].block - trigs[i - 1u].block, want = BAR_BLOCKS(120) / 8u;
            bad += db + 2u < want || db > want + 2u || trigs[i].step != (i * 2u) % 16u;
        }
        check("LEN AUTO (a loop of 1 bar): a trigger every 1/8 bar, on the even steps", ntrig >= 15u && !bad, "%u triggers in 2 bars", ntrig);
        stop();
        fresh();
        set(0, 0, 0, 0, 0);
        BP(BP_LEN) = 4;                                 /* 2 bars: a trigger every 1/4 bar */
        play(BAR_BLOCKS(120) * 2u, 0);
        check("LEN 2: a trigger every 1/4 bar", ntrig >= 7u && ntrig <= 9u, "%u in 2 bars", ntrig);
        stop();
        fresh();
        set(0, 0, 0, 0, 0);
        BP(BP_LEN) = 1;                                 /* 1/4 bar: 32 a bar: a trigger every 32nd note */
        play(BAR_BLOCKS(120), 0);
        check("LEN 1/4: 32 triggers a bar (the second of a step half a step later)", ntrig >= 30u && ntrig <= 33u, "%u", ntrig);
        stop();
        fresh();
        set(0, 0, 0, 0, 0);
        BP(BP_LOOPA) = (int16_t)BRK_USR(3);             /* BR4: 2 bars (guessed): AUTO is a trigger every 1/4 bar */
        brkp_poll();
        play(BAR_BLOCKS(120) * 2u, 0);
        check("LEN AUTO, a loop of 2 bars: a trigger every 1/4 bar (its 8 slices in 2 bars)", ntrig >= 7u && ntrig <= 9u, "%u in 2 bars", ntrig);
        stop();
        fresh();
        set(0, 0, 0, 0, 0);
        TBRK->p[P_SDIV] = 1;                            /* DIV 1/8: a step is BB Gen's 16th, twice as long */
        play(BAR_BLOCKS(120) * 2u, 0);
        check("DIV 1/8: the break at half the rate (8 triggers in 2 bars)", ntrig >= 7u && ntrig <= 9u, "%u in 2 bars", ntrig);
        stop();
        fresh();
        set(0, 0, 0, 0, 0);
        play(BAR_BLOCKS(120) * 2u, 0);
        for (bad = 0, i = 0; i < ntrig; i++)
            bad += trigs[i].slice != (int8_t)(i % 8u) || trigs[i].bank != 0;
        check("CMPLX 0: the slices in the loop's own order, 8 a bar, from A", !bad && ntrig >= 15u, "%u triggers", ntrig);
        stop();
    }

    /* 2: the generator */
    {
        uint32_t hist[8] = {0}, runs = 4000u, same0 = 0, same4 = 0, ok = 1, walk_ok = 0, prev = 0;
        fresh();
        set(100, 100, 0, 0, 0);                         /* ANCHR 100: weight 0 at beats 0 and 4: they never swap */
        bp.current_slice = 0;
        for (i = 0; i < runs; i++) {
            uint32_t sl = bp_select(i % 8u, i / 8u);
            same0 += i % 8u == 0u && sl == 0u;
            same4 += i % 8u == 4u && sl == 4u;
            bp.current_slice = (int8_t)sl;
        }
        check("ANCHR 100, CMPLX 100: beats 0 and 4 always play their own slice", same0 == runs / 8u && same4 == runs / 8u, "%u/%u %u/%u", same0, runs / 8u,
              same4, runs / 8u);
        fresh();
        set(100, 0, 0, 0, 0);
        for (i = 0; i < runs; i++)
            hist[bp_select(i % 8u, i / 8u)]++;
        for (k = 0; k < 8u; k++)
            ok &= hist[k] > runs * 9u / 100u && hist[k] < runs * 16u / 100u;
        check("CMPLX 100, ANCHR 0: every slice about 1 time in 8", ok, "%u %u %u %u %u %u %u %u", hist[0], hist[1], hist[2], hist[3], hist[4], hist[5], hist[6], hist[7]);
        fresh();
        set(100, 0, 100, 0, 0);                         /* ROLL 100: only the STAY branch */
        for (i = 0; i < 2000u; i++) {
            uint32_t sl = bp_select(i % 8u, i / 8u), d = (sl + 8u - prev) & 7u;
            if (i)
                walk_ok += d == 0u || d == 1u || d == 7u || (d >= 2u && d <= 4u);
            prev = sl;
            bp.current_slice = (int8_t)sl;
        }
        check("ROLL 100: it repeats, walks +-1 or jumps 2..4", walk_ok == 1999u, "%u/1999", walk_ok);
        fresh();
        set(0, 0, 100, 100, 0);
        for (bad = 0, i = 0; i < 1000u; i++)
            bad += bp_select(i % 8u, i / 8u) != i % 8u;
        check("CMPLX 0 bypasses the generator (ROLL and FILL do nothing)", !bad, "%u", bad);
        fresh();
        set(8, 0, 100, 0, 0);                           /* ROLL comes in with CMPLX: 8 % -> a third of it (ramp 32 %) */
        {
            uint32_t on = 0;
            for (i = 0; i < 4000u; i++) {
                uint32_t sl = bp_select(i % 8u, i / 8u);
                on += sl == i % 8u;
                bp.current_slice = (int8_t)sl;
            }
            check("ROLL comes in with CMPLX (8 %: the loop's own slice about 2 times in 3)", on > 4000u * 55u / 100u && on < 4000u * 85u / 100u, "%u/4000", on);
        }
        fresh();
        set(25, 0, 0, 100, 0);                          /* PHRSE 4, FILL 100: the last bar of a phrase is CMPLX 100 */
        BP(BP_PHRASE) = 2;
        {
            uint32_t moved_in = 0, moved = 0, nin = 0, nfill = 0;
            for (i = 0; i < 8000u; i++) {
                uint32_t sl = bp_select(i % 8u, i / 8u);
                if (i / 8u % 4u == 3u) {
                    nfill++;
                    moved += sl != i % 8u;
                } else {
                    nin++;
                    moved_in += sl != i % 8u;
                }
                bp.current_slice = (int8_t)sl;
            }
            check("FILL 100: CMPLX 25 in the phrase (about 22 % moved), 100 in its last bar (about 87 %)",
                  moved > nfill * 80u / 100u && moved_in > nin * 14u / 100u && moved_in < nin * 30u / 100u, "%u/%u in the fill, %u/%u elsewhere", moved, nfill, moved_in, nin);
        }
        {   /* the chance a bar -> a trigger: 50 % a bar is 8.3 % a trigger at 8 a bar (tpt 12), 15.9 % at 4, 29.3 % at 2, 50 % at 1 */
            uint32_t p12 = bp_rate_p(50, 12), p24 = bp_rate_p(50, 24), p48 = bp_rate_p(50, 48), p96 = bp_rate_p(50, 96), p3 = bp_rate_p(50, 3);
            check("2X..8X: 1 - (1 - P)^(1 / triggers a bar)", p12 > 65536u * 82u / 1000u && p12 < 65536u * 84u / 1000u && p24 > 65536u * 158u / 1000u && p24 < 65536u * 160u / 1000u &&
                  p48 > 65536u * 292u / 1000u && p48 < 65536u * 294u / 1000u && p96 > 65536u * 499u / 1000u && p96 < 65536u * 501u / 1000u &&
                  p3 > 65536u * 21u / 1000u && p3 < 65536u * 22u / 1000u && bp_rate_p(100, 12) == 65536u && bp_rate_p(0, 12) == 0u,
                  "%.3f %.3f %.3f %.3f %.3f", p12 / 65536.0, p24 / 65536.0, p48 / 65536.0, p96 / 65536.0, p3 / 65536.0);
        }
        fresh();
        set(0, 0, 0, 0, 100);                           /* 100 % on every rate: every trigger rolls; each rate about 1 time in 4 */
        {
            uint32_t cnt[9] = {0}, nd;
            play(BAR_BLOCKS(120) * 40u, 0);
            for (nd = 0, i = 1; i < ntrig; i++) {       /* (the first: forced, no replay) */
                cnt[(uint32_t)trigs[i].div > 8u ? 0u : (uint32_t)trigs[i].div]++;
                nd++;
            }
            check("2X..8X 100: every trigger replays, at a random one of the four rates", nd > 200u && cnt[0] == 0u && cnt[2] > nd * 20u / 100u && cnt[3] > nd * 20u / 100u &&
                  cnt[4] > nd * 20u / 100u && cnt[8] > nd * 20u / 100u, "2:%u 3:%u 4:%u 8:%u of %u", cnt[2], cnt[3], cnt[4], cnt[8], nd);
            stop();
        }
        /* a parameter lock: CMPLX 100 on step 4 only, CMPLX 0 elsewhere: only that step's trigger moves, CMPLX is back after */
        fresh();
        set(0, 0, 0, 0, 0);
        check("a lock of CMPLX on a step is taken (LOOP A is not lockable)", lock_set(TBRK, 4, P_E0, 100) && !lock_set(TBRK, 4, P_LRATE, 3), 0);
        play(BAR_BLOCKS(120) * 24u, 0);
        {
            uint32_t moved4 = 0, n4 = 0, other = 0;
            for (i = 0; i < ntrig; i++)
                if (trigs[i].step == 4u) {
                    n4++;
                    moved4 += trigs[i].slice != 2;
                } else {
                    other += trigs[i].slice != (int8_t)((trigs[i].step / 2u) % 8u);
                }
            check("... CMPLX 100 on step 4 (its slice moves about 7 times in 8), the others in order, CMPLX 0 again after it",
                  n4 >= 23u && moved4 > n4 / 2u && !other && BP(BP_CMPLX) == 0, "%u/%u moved on step 4, %u elsewhere", moved4, n4, other);
        }
        stop();
        check("... STOP: the lock let go", BP(BP_CMPLX) == 0 && TBRK->lk_n == 0, 0);
    }

    /* 3: the sound */
    {
        static float ref[44100 * 2];
        uint32_t frames = FS * 2u;
        double best;
        fresh();
        set(0, 0, 0, 0, 0);
        song.g[G_BPM] = 137;                            /* AMEN 2's own tempo: 0.5 source samples a sample */
        play(BAR_BLOCKS(137) + 20u, 1);
        stop();
        ref_of(ref, frames);
        best = corr(ref, 3000, 40000, 400);
        check("AMEN 2 at its own tempo (137), CMPLX 0, every step on: it plays as it is (correlation)", best > 0.95, "%.3f", best);
        {
            uint32_t pk = 0, jumps = 0;
            for (i = 0; i < 44100u; i++) {
                uint32_t a = (uint32_t)abs(pcm[i][0]);
                pk = a > pk ? a : pk;
                if (i && abs(pcm[i][0] - pcm[i - 1u][0]) > 9000)
                    jumps++;
            }
            check("... bounded, and no clicks", pk > 4000u && pk < 32767u && jumps < 40u, "peak %u, %u big jumps", pk, jumps);
        }
        /* a user loop (BR4, two bars of 5 s, guessed: 2 bars) at 96 BPM, CMPLX 0, every step on: it plays as it is */
        fresh();
        set(0, 0, 0, 0, 0);
        song.g[G_BPM] = 96;
        BP(BP_LOOPA) = (int16_t)BRK_USR(3);
        brkp_poll();
        play(BAR_BLOCKS(96) + 20u, 1);
        stop();
        {
            static float ref2[FS * 2];
            ref_of(ref2, frames);
            best = corr(ref2, 3000, 40000, 400);
            check("BR4 (its bars guessed: 2) at 96 BPM, CMPLX 0: the user's loop plays as it is (correlation)", best > 0.9, "%.3f", best);
        }
        /* a step off is a hole: the 16th is silent after the ramp (2 ms), the break goes on under it */
        fresh();
        steps_on(0xAAAAu);                              /* the odd steps on, the even ones off */
        set(0, 0, 0, 0, 0);
        song.g[G_BPM] = 137;
        play(BAR_BLOCKS(137) + 20u, 1);
        stop();
        {
            uint32_t step = FS * 60u / 137u / 4u, hole = 0, heard = 0, s;
            for (s = 0; s < 16u; s++) {
                double e = 0;
                for (i = s * step + 400; i < (s + 1u) * step - 100u; i++)
                    e += (double)pre[i] * pre[i];
                e = sqrt(e / (step - 500.0));
                if ((0xAAAAu >> s) & 1u)
                    heard += e > 60.0;
                else
                    hole += e < 50.0;
            }
            check("a step off is a hole (the other 16ths play)", hole >= 7u && heard >= 7u, "%u holes, %u heard of 8 + 8", hole, heard);
        }
        {   /* ... and the break goes on under it: step 9 (on, after the hole of step 8, where the trigger fired) is the loop's own */
            uint32_t step = FS * 60u / 137u / 4u, s9 = 9;
            best = corr(ref, s9 * step + 400, (s9 + 1u) * step - 400u, 200);
            check("... the break runs under it (the step after a hole is the loop's)", best > 0.9, "%.3f", best);
        }
        /* a pinned step: slice 5 (the loop's 5/8) from its start at step 0; its first samples are the slice's first */
        fresh();
        steps_on(0x0001u);
        TBRK->bstep[0].slice = 6;                       /* slice 5 (pins count from 1) */
        set(0, 0, 0, 0, 0);
        song.g[G_BPM] = 137;
        play(40u, 1);
        stop();
        {
            slc_dec_t dd;
            uint32_t lo = bp_slice_lo(&bpb[0].t, 5);
            double xy = 0, xx = 0, yy = 0;
            static int16_t src[1500];
            slc_dec_at(&dd, lo, bpb[0].t.grid[5 * BP_GPS]);
            int32_t lc[2] = {0, 0};
            for (i = 0; i < 1500u; i++)
                src[i] = (int16_t)slc_dec_next(&bpb[0].t, &dd);
            for (i = 200; i < 1200u; i++) {                /* 0.5 source samples a sample: output sample j is source sample lo + j / 2 */
                double x = 0, y = pre[i];                  /* (the mix before the master: the master's limiter is a block late) */
                uint32_t j;
                for (j = i == 200u ? 0u : i; j <= i; j++)  /* (the source through the BREAK's role low cut, from the start) */
                    x = lowcut_sh(src[j / 2u] * 64, lc, ROLE_CUT[TRK_BRK]);
                xy += x * y;
                xx += x * x;
                yy += y * y;
            }
            check("a pinned step plays its own slice from its start (slice 5)", xx > 0 && yy > 0 && xy / sqrt(xx * yy) > 0.85, "%.3f at the start", xx > 0 && yy > 0 ? xy / sqrt(xx * yy) : 0.0);
            check("... and the trigger log says slice 5", ntrig >= 1 && trigs[0].slice == 5, "%d", ntrig ? trigs[0].slice : -1);
        }
        /* a step's own retrigger, level, pitch and reverse */
        fresh();
        set(0, 0, 0, 0, 0);
        TBRK->bstep[2].rat = 3;                         /* x4 */
        TBRK->bstep[4].lvl = LV_GHOST;
        TBRK->bstep[6].lvl = LV_HARD;
        TBRK->bstep[8].pitch = 12;
        TBRK->bstep[10].flags = BS_REV;
        TBRK->bstep[12].slice = 8;                      /* (a pin with its own retrigger) */
        TBRK->bstep[12].rat = 1;
        play(BAR_BLOCKS(120) * 2u, 0);
        {
            uint32_t okr = 1, seen = 0;
            for (i = 0; i < ntrig; i++) {
                const trig_t *t = &trigs[i];
                uint32_t s = t->step;
                seen |= 1u << s;
                okr &= t->div == (s == 2u ? 4 : s == 12u ? 2 : 0);
                okr &= t->amp == bp_lvl_amp(s == 4u ? LV_GHOST : s == 6u ? LV_HARD : LV_NORM);
                okr &= t->semi == (s == 8u ? 12 : 0) && t->rev == (s == 10u);
                okr &= t->slice == (s == 12u ? 7 : (int8_t)(s / 2u));
            }
            check("a step's own retrigger (x4, a pin's x2), level (ghost, hard), pitch (+12) and reverse, each on its step only",
                  okr && seen == 0x5555u, "steps 0x%04X", seen);
        }
        stop();
        fresh();                                        /* a retrigger on a step between two triggers: the slice playing again */
        set(0, 0, 0, 0, 0);
        TBRK->bstep[3].rat = 1;                         /* x2 */
        play(BAR_BLOCKS(120) / 16u * 3u, 0);
        {
            uint32_t s0 = bp.st_starts, b = 0;
            while (TBRK->seq_idx != 4u && b++ < 100u)
                play(1, 0);
            check("... on a step between triggers: the slice playing, twice", bp.st_starts - s0 == 2u, "%u voices started in step 3", bp.st_starts - s0);
        }
        stop();
        /* held slices play whatever the step says, and the gate opens on them */
        fresh();
        steps_on(0);
        set(0, 0, 0, 0, 0);
        play(40u, 0);
        brkp_key(3, 1);
        play(30u, 1);
        {
            double e = 0;
            for (i = 400; i < 800u; i++)                   /* (the mix before the master: its drive is not the track's) */
                e += (double)pre[i] * pre[i];
            check("a held slice key sounds with every step off, as slice 3", sqrt(e / 400.0) > 500.0 && bp.current_slice == 3 && bp.count == 1, "rms %.0f", sqrt(e / 400.0));
        }
        brkp_key(3, 0);
        play(40u, 0);
        check("... and lets go: the gate closes", bp.gate_to == 0 && bp.gate == 0, 0);
        stop();
        /* reverse: continues from the playhead the other way */
        fresh();
        set(0, 0, 0, 0, 0);
        play(60u, 0);
        brkp_key(8, 1);
        play(4u, 0);
        check("reverse held: the voice plays backwards", bp.v[bp.cur].on && bp.v[bp.cur].rev, 0);
        play(300u, 0);
        brkp_key(8, 0);
        play(4u, 0);
        check("... and let go: forwards again", bp.v[bp.cur].on && !bp.v[bp.cur].rev, 0);
        stop();
        /* half speed: every other trigger fires, and the rate is halved */
        fresh();
        set(0, 0, 0, 0, 0);
        brkp_key(9, 1);
        play(BAR_BLOCKS(120) * 2u, 0);
        check("half speed: every other trigger fires (the clock still counts them)", ntrig >= 7u && ntrig <= 10u, "%u triggers in 2 bars (16 counted)", ntrig);
        brkp_key(9, 0);
        stop();
        /* stutter: a 4x retrigger on every trigger */
        fresh();
        set(0, 0, 0, 0, 0);
        brkp_key(10, 1);
        play(BAR_BLOCKS(120), 0);
        for (bad = 0, i = 1; i < ntrig; i++)
            bad += trigs[i].div != 4;
        check("stutter: every trigger is a 4x retrigger", ntrig >= 7u && !bad, "%u", ntrig);
        brkp_key(10, 0);
        stop();
        /* the retrigger replays the head: the voices' position falls back to the slice start sub_div - 1 times in a trigger */
        fresh();
        set(0, 0, 0, 0, 100);
        {
            uint32_t found = 0, restarts = 0, last = 0, b, st0 = 0;
            transport_req = 1;
            for (b = 0; b < BAR_BLOCKS(120) * 8u && !found; b++) {
                int32_t o[2 * CTL];
                mix_block(o, CTL);
                if (bp.st_trigs > 1u && bp.st_div == 4 && !bp.st_forced) {
                    found = 1;
                    st0 = bp.st_trigs;
                }
            }
            if (found) {
                const bpv_t *v;
                uint32_t sl = (uint32_t)bp.st_slice, lo = bp_slice_lo(&bpb[0].t, sl), b2;
                v = &bp.v[bp.cur];
                last = v->d.pos;
                for (b2 = 0; b2 < BAR_BLOCKS(120) / 8u && bp.st_trigs == st0; b2++) {
                    int32_t o[2 * CTL];
                    mix_block(o, CTL);
                    v = &bp.v[bp.cur];
                    if (v->d.pos < last && v->d.pos < lo + 600u)
                        restarts++;
                    last = v->d.pos;
                }
            }
            check("4X: a 4x retrigger replays the head of its slice 3 times", found && restarts == 3u, "%u restarts", restarts);
            stop();
        }
        /* loops B, C, D and the phrase: PHRSE 2, B CHN 100: B at the last bar of every phrase, the first trigger of a bank forced */
        fresh();
        set(0, 0, 0, 0, 0);
        BP(BP_PHRASE) = 1;                              /* 2 bars */
        BP(BP_BCH) = 100;
        play(BAR_BLOCKS(120) * 6u, 0);
        {
            uint32_t bank_b = 0, bank_a = 0, forced_b = 0, other = 0;
            for (i = 0; i < ntrig; i++) {
                bank_b += trigs[i].bank == 1;
                bank_a += trigs[i].bank == 0;
                other += trigs[i].bank > 1;
                forced_b += trigs[i].bank == 1 && trigs[i].forced;
            }
            check("PHRSE 2, B CHN 100: B plays the last bar of each phrase, A the rest", bank_a >= 16u && bank_b >= 16u && forced_b >= 2u && !other,
                  "A %u, B %u triggers, %u forced into B", bank_a, bank_b, forced_b);
        }
        stop();
        fresh();
        set(0, 0, 0, 0, 0);
        BP(BP_PHRASE) = 1;
        BP(BP_CCH) = BP(BP_DCH) = 100;                  /* C and D both hit: one of them at random */
        play(BAR_BLOCKS(120) * 24u, 0);
        {
            uint32_t cnt[4] = {0}, bad_bar = 0;
            for (i = 0; i < ntrig; i++) {
                uint32_t bar = trigs[i].block / BAR_BLOCKS(120);
                cnt[trigs[i].bank & 3]++;
                bad_bar += (trigs[i].bank != 0) != (bar % 2u == 1u) && trigs[i].block % BAR_BLOCKS(120) > 2u;
            }
            check("PHRSE 2, C and D CHN 100: C or D at random in each phrase's last bar, A the rest, never B",
                  cnt[0] >= 80u && cnt[1] == 0u && cnt[2] >= 16u && cnt[3] >= 16u && !bad_bar, "A %u, B %u, C %u, D %u, %u off the phrase", cnt[0], cnt[1], cnt[2], cnt[3], bad_bar);
        }
        stop();
        fresh();
        set(0, 0, 0, 0, 0);
        BP(BP_PHRASE) = 1;
        BP(BP_DCH) = 100;
        BP(BP_LOOPD) = (int16_t)BRK_USR(30);            /* D has no loop: A goes on */
        brkp_poll();
        play(BAR_BLOCKS(120) * 4u, 0);
        for (bad = 0, i = 0; i < ntrig; i++)
            bad += trigs[i].bank != 0;
        check("a chance that hits a bank with no loop: A goes on", !bad && ntrig >= 30u, "%u off A", bad);
        stop();
        /* mute fades, solo silences (the mix before the master: the master's own tail is not the track's) */
        fresh();
        set(0, 0, 0, 0, 0);
        play(BAR_BLOCKS(120) / 2u, 1);
        TBRK->p[P_MUTE] = 1;
        play(40u, 1);
        check("MUTE fades the track out (silent after ~6 ms)", rms_last(600) < 1.0, "rms %.1f", rms_last(600));
        TBRK->p[P_MUTE] = 0;
        song.solo = 1;                                  /* another track soloed: the BREAK is silent */
        play(60u, 1);
        check("another track soloed silences it", rms_last(600) < 1.0, "rms %.1f", rms_last(600));
        song.solo = 1u << TRK_BRK;
        play(60u, 1);
        check("... soloed itself, it is heard", rms_last(600) > 100.0, "rms %.1f", rms_last(600));
        song.solo = 0;
        TBRK->p[P_LEVEL] = 0;
        play(60u, 1);
        check("LEVEL 0: silent", rms_last(600) < 1.0, "rms %.1f", rms_last(600));
        TBRK->p[P_LEVEL] = 70;
        play(60u, 1);
        check("... and it is back", rms_last(600) > 100.0, "rms %.1f", rms_last(600));
        stop();
        /* STOP: silent, the voices off */
        fresh();
        play(50u, 0);
        stop();
        npcm = 0;
        for (i = 0; i < 40u; i++) {
            int32_t o[2 * CTL];
            uint32_t q;
            mix_block(o, CTL);
            for (q = 0; q < CTL; q++, npcm++)
                pre[npcm] = acc[0].l[q];
        }
        {
            uint32_t pk = 0;
            for (i = 0; i < npcm; i++) {
                uint32_t a = (uint32_t)abs(pre[i]);
                pk = a > pk ? a : pk;
            }
            check("STOP: silent, the voices off", pk == 0u && !bp.v[0].on && !bp.v[1].on && !bp.running, "peak %u", pk);
        }
        /* every setting at once, every loop, tempo extremes: bounded */
        {
            uint32_t ok = 1, maxpk = 0, loopi, bpm;
            for (loopi = 0; loopi < brkp_nloops(); loopi++)
                for (bpm = 40; bpm <= 240u; bpm += 100u) {
                    uint32_t pk = 0, j;
                    fresh();
                    set(100, 50, 30, 50, 100);
                    BP(BP_PHRASE) = 1;
                    BP(BP_BCH) = BP(BP_CCH) = BP(BP_DCH) = 50;
                    BP(BP_LOOPA) = (int16_t)loopi;
                    BP(BP_LOOPB) = (int16_t)(loopi ? loopi - 1u : 0u);
                    BP(BP_LOOPC) = (int16_t)((loopi + 4u) % brkp_nloops());
                    BP(BP_BARSA) = (int16_t)(loopi % 5u);
                    BP(BP_BARSB) = (int16_t)((loopi + 2u) % 5u);
                    brkp_poll();
                    BP(BP_PITCH) = (int16_t)(bpm == 40u ? -12 : bpm == 240u ? 12 : 0);
                    BP(BP_LEN) = (int16_t)(loopi % 7u);
                    for (i = 0; i < 16u; i++) {          /* every step's own values */
                        TBRK->bstep[i].slice = (uint8_t)((i * 5u + loopi) % 9u);
                        TBRK->bstep[i].rat = (uint8_t)((i + loopi) % 5u);
                        TBRK->bstep[i].lvl = (uint8_t)(i & 3u);
                        TBRK->bstep[i].pitch = (int8_t)((int)(i % 7u) * 8 - 24);
                        TBRK->bstep[i].flags = (uint8_t)(i % 3u == 0u ? BS_REV : 0u);
                    }
                    song.g[G_BPM] = (int16_t)bpm;
                    bp.keys = (uint16_t)((bpm == 140u ? 0x100u : 0u) | (bpm == 240u ? 0x200u : 0u));
                    transport_req = 1;
                    for (j = 0; j < FS * 3u / CTL; j++) {
                        int32_t o[2 * CTL];
                        uint32_t q;
                        mix_block(o, CTL);
                        for (q = 0; q < 2u * CTL; q++)
                            pk = (uint32_t)abs(o[q]) > pk ? (uint32_t)abs(o[q]) : pk;
                    }
                    bp.keys = 0;
                    stop();
                    ok &= pk < 32767u;
                    maxpk = pk > maxpk ? pk : maxpk;
                }
            check("every loop / bars / tempo / pitch / LEN / step value with the generator at full: bounded", ok, "peak %u", maxpk);
        }
    }

    /* 4: hits from MIDI and the keys, recorded into the steps */
    {
        static const uint8_t AT[4] = {3, 6, 10, 13}, SL[4] = {1, 5, 2, 7}, LV[4] = {LV_GHOST, LV_HARD, LV_SOFT, LV_NORM};
        uint32_t h0, s0;
        fresh();
        set(0, 0, 0, 0, 0);
        steps_on(0);
        song.sel = TRK_BRK;
        brk_input(4, LV_NORM, 0, 1);                    /* stopped, not armed: it sounds, nothing records */
        check("a hit while stopped: heard, not recorded", bp.st_hits == 1u && bp.v[bp.cur].on && bp.gate_to && !TBRK->bstep[0].on && !TBRK->bstep[0].slice, 0);
        for (i = 0; i < BAR_BLOCKS(120) / 2u; i++) {    /* (stopped: a slice is 1/8 bar) */
            int32_t o[2 * CTL];
            mix_block(o, CTL);
        }
        check("... and over at its slice's end", !bp.v[0].on && !bp.v[1].on && bp.gate == 0, 0);
        song.rec = 1u << TRK_BRK;                       /* REC on the BREAK track */
        h0 = bp.st_hits;
        for (i = 0; i < 4u; i++) {
            uint32_t vel = LV[i] == LV_GHOST ? 30u : LV[i] == LV_HARD ? 120u : LV[i] == LV_SOFT ? 70u : 100u;
            while (TBRK->seq_idx != AT[i])
                play(1, 0);
            if (i < 3u)                                 /* MIDI ch 10, 4, 10 (C2 = slice 1) */
                midi_in_q[mi_w++ % MQ] = 0x09u | (i == 1u ? 0x93u : 0x99u) << 8 | (36u + SL[i]) << 16 | vel << 24;
            else                                        /* a key, the BREAK track selected (no ghost / hard held) */
                fm1_in.notes = 1u << brk_key_of_slice(SL[i]);
            play(1, 0);
            fm1_in.notes = 0;
            play(1, 0);
        }
        for (bad = 0, i = 0; i < 4u; i++) {
            const bstep_t *b = &TBRK->bstep[AT[i]];
            bad += !b->on || b->slice != SL[i] + 1u || b->lvl != LV[i];
        }
        check("REC: MIDI ch 10 / ch 4 / a key into the step heard: on, its slice, the level of its velocity", !bad && bp.st_hits == h0 + 4u &&
              !TBRK->bstep[4].on && !TBRK->bstep[11].on, "%u wrong", bad);
        /* a hit late in a step lands on the next one, which does not start it again */
        {
            uint32_t q, guard = 0;
            while (!(at_step(TBRK, &q) == 7u && q > 85u) && guard++ < 100000u)
                play(1, 0);
            midi_in_q[mi_w++ % MQ] = 0x09u | 0x99u << 8 | (36u + 6u) << 16 | 100u << 24;
            play(1, 0);
            s0 = bp.st_starts;
            while (TBRK->seq_idx != 8u && guard++ < 100000u)
                play(1, 0);
            check("a hit late in step 7 lands on step 8 (slice 7), which does not start it twice",
                  TBRK->bstep[8].on && TBRK->bstep[8].slice == 7u && bp.st_starts == s0, "%u voices started on step 8", bp.st_starts - s0);
        }
        song.rec = 0;
        while (TBRK->seq_idx != 1u)
            play(1, 0);
        midi_in_q[mi_w++ % MQ] = 0x09u | 0x99u << 8 | 40u << 16 | 100u << 24;
        play(2, 0);
        check("REC off: a hit plays, nothing records", !TBRK->bstep[1].on && !TBRK->bstep[2].on && bp.st_hits == h0 + 6u, 0);
        stop();
    }

    /* 5: the project: the steps and the generator's parameters, captured and applied */
    {
        static project_t pj;
        bstep_t want[NSTEP];
        int16_t wp[BP_NP];
        fresh();
        set(37, 12, 5, 66, 9);
        BP(BP_PHRASE) = 3;
        BP(BP_CCH) = 44;
        BP(BP_LEN) = 5;
        BP(BP_PITCH) = -3;
        BP(BP_LOOPC) = (int16_t)BRK_USR(4);
        BP(BP_BARSD) = 2;
        for (i = 0; i < NSTEP; i++)
            TBRK->bstep[i] = (bstep_t){.on = (uint8_t)(i % 3u != 0u), .slice = (uint8_t)(i % 9u), .lvl = (uint8_t)(i & 3u),
                                       .rat = (uint8_t)(i % 5u), .pitch = (int8_t)((int)(i % 49u) - 24), .flags = (uint8_t)(i & 1u)};
        lock_set(TBRK, 5, P_E1, 90);
        memcpy(want, TBRK->bstep, sizeof want);
        for (i = 0; i < BP_NP; i++)
            wp[i] = BP(i);
        proj_capture(&pj);
        fresh();
        proj_apply(&pj, 1);
        brkp_poll();
        bad = memcmp(want, TBRK->bstep, sizeof want) != 0;
        for (i = 0; i < BP_NP; i++)
            bad += wp[i] != BP(i);
        check("the project: every step (on, slice, level, retrigger, pitch, reverse) and the generator back, a lock of ANCHR too",
              !bad && lock_find(TBRK, 5, P_E1, 0) >= 0 && bpb[2].key == (BRK_USR(4) | smp_user_gen << 8) && bpb[2].src, "%u differ", bad);
    }

    /* 5a: the kicks of a loop (DUCK): found when it is built, a kick slice ducks the parts */
    {
        uint32_t k0;
        fresh();
        check("AMEN 2: its kicks found (slice 1 and 6 on a kick, the snares on 3 and 7 not)",
              (bpb[0].kicks & 1u) && (bpb[0].kicks & 0x20u) && !(bpb[0].kicks & 0x44u), "0x%02x", bpb[0].kicks);
        check("AMEN 3 and 4: slice 1 a kick, the snare on 2 (slice 3) not", (bpb[1].kicks & 1u) && !(bpb[1].kicks & 0x04u) &&
              (bpb[2].kicks & 1u) && !(bpb[2].kicks & 0x04u), "0x%02x 0x%02x", bpb[1].kicks, bpb[2].kicks);
        bp.kick = 0;
        brk_hit(2, LV_NORM);
        k0 = bp.kick;
        brk_hit(0, LV_NORM);
        check("a snare slice does not duck, a kick slice does", !k0 && bp.kick, 0);
        bp.kick = 0;
        stop();
    }

    /* 5c: LAYER: a second loop under every slice */
    {
        uint32_t b, seen = 0, same = 1, layered = 0, bank_ok = 1, amp_ok = 1, pitch_ok = 1;
        double e0, e1;
        fresh();
        set(0, 0, 0, 0, 0);
        play(BAR_BLOCKS(120), 1);
        for (b = 2; b < 4u; b++)
            seen |= bp.v[b].on;
        e0 = rms_last(npcm);
        check("LAYER OFF: no layer voice", !seen, 0);
        stop();
        fresh();
        set(0, 0, 0, 0, 0);
        BP(BP_LAYER) = 2;                            /* C: AMEN 4 under AMEN 2 */
        BP(BP_LMIX) = 70;
        BP(BP_LPTCH) = -12;
        transport_req = 1;
        for (b = 0; b < BAR_BLOCKS(120); b++) {
            int32_t o[2 * CTL];
            uint32_t t0 = bp.st_starts;
            mix_block(o, CTL);
            if (bp.st_starts != t0 && bp.lcur >= 2u && bp.v[bp.lcur].on) {
                const bpv_t *m = &bp.v[bp.cur], *l = &bp.v[bp.lcur];
                const slc_src_t *ls = brkp_bank_src(2);
                layered++;
                bank_ok &= l->bank == 2u;
                same &= ls && l->lo == bp_slice_lo(ls, (uint32_t)bp.st_slice);
                amp_ok &= l->amp == (int32_t)((uint32_t)m->amp * 70u / 100u);
                pitch_ok &= l->semi == m->semi - 12;
            }
        }
        check("LAYER C: every hit starts the same slice of loop C beside it", layered >= 7u && bank_ok && same, "%u hits", layered);
        check("LAYER: L MIX of the hit's level, L PTCH on its pitch", amp_ok && pitch_ok, 0);
        stop();
        fresh();
        set(0, 0, 0, 0, 0);
        BP(BP_LAYER) = 2;
        BP(BP_LMIX) = 100;
        play(BAR_BLOCKS(120), 1);
        e1 = rms_last(npcm);
        check("LAYER: heard (the mix fuller than the loop alone)", e1 > e0 * 1.1, "rms %.0f -> %.0f", e0, e1);
        stop();
        fresh();                                     /* the layer on the loop already playing: none */
        set(0, 0, 0, 0, 0);
        BP(BP_LAYER) = 0;
        brk_hit(1, LV_NORM);
        BP(BP_LAYER) = (int16_t)bp.v[bp.cur].bank;
        bp.v[2].on = bp.v[3].on = 0;
        brk_hit(1, LV_NORM);
        check("LAYER on the loop that plays: no doubling", !bp.v[2].on && !bp.v[3].on, 0);
        check("LAYER is not a step's lock, L MIX and L PTCH are", !brk_lockable(P_GLMODE) && brk_lockable(P_DETUNE) &&
              brk_lockable(P_GLIDE), 0);
    }

    /* 5b: STYLE and MANGLE */
    {
        uint32_t i, kinds = 0, nm = 0, dive = 0, fell = 0, peak = 0, nan = 0;
        fresh();
        brk_style(TBRK, BS_BRKCORE);
        check("STYLE BRKCORE: CMPLX 85, ANCHR 10, 8X 25, PHRSE 2, MANGL 55, steps 1/16",
              BP(BP_CMPLX) == 85 && BP(BP_ANCHOR) == 10 && BP(BP_R8) == 25 && BP(BP_PHRASE) == 1 && BP(BP_MANGLE) == 55 &&
              TBRK->p[P_SDIV] == 2 && BP(BP_STYLE) == BS_BRKCORE, 0);
        brk_style(TBRK, BS_STRAIGHT);
        check("STYLE STRAIGHT: no generator, no retrigger, no MANGLE", BP(BP_CMPLX) == 0 && BP(BP_R2) == 0 && BP(BP_MANGLE) == 0, 0);
        check("STYLE is not a step's lock", !brk_lockable(P_PRIO) && brk_lockable(P_ED_FLT), 0);
        /* HALFTIME: 1/8 steps: the loop's slices half as fast (8 triggers over 2 bars, not 1) */
        fresh();
        brk_style(TBRK, BS_HALFTIME);
        set(0, 0, 0, 0, 0);
        BP(BP_MANGLE) = 0;
        play(BAR_BLOCKS(120), 0);
        check("HALFTIME: steps of 1/8: a bar plays 4 of the loop's 8 slices, in order", ntrig >= 4u && ntrig <= 5u &&
              trigs[0].slice == 0 && trigs[1].slice == 1 && trigs[3].slice == 3, "%u triggers", ntrig);
        stop();
        /* MANGLE 100: three triggers in four mangled, every trick; 0: none */
        fresh();
        set(50, 0, 0, 0, 0);
        BP(BP_MANGLE) = 100;
        play(16u * BAR_BLOCKS(120), 1);
        for (i = 0; i < ntrig; i++) {
            if (trigs[i].mg) {
                nm++;
                kinds |= 1u << trigs[i].mg;
            }
            dive += trigs[i].mg == 4;
        }
        for (i = 0; i < npcm; i++) {
            uint32_t a = (uint32_t)abs(pcm[i][0]);
            peak = a > peak ? a : peak;
            nan += pcm[i][0] != pcm[i][0];
        }
        check("MANGLE 100: 60..90 % of the generator's triggers mangled", ntrig > 50u && nm * 100u >= ntrig * 60u &&
              nm * 100u <= ntrig * 90u, "%u of %u", nm, ntrig);
        check("MANGLE 100: stutter, reverse, jump, dive and gap all heard", kinds == 0x3Eu, "kinds 0x%x", kinds);
        check("MANGLE 100: bounded", peak < 32768u && !nan && peak > 2000u, "peak %u", peak);
        stop();
        fresh();
        set(50, 0, 0, 0, 0);
        BP(BP_MANGLE) = 100;
        for (i = 0; i < 16u * BAR_BLOCKS(120) && !fell; i++) {   /* a dive bends the voice down a block at a time */
            play(1, 0);
            if (bp.st_mangle == 4u && bp.v[bp.cur].dbend < 0) {
                int16_t b0 = bp.v[bp.cur].bend;
                play(4, 0);
                fell = bp.v[bp.cur].bend < b0 || bp.st_mangle != 4u;
            }
        }
        check("MANGLE dive: the pitch falls as it plays", fell, "%u dives in the 16 bars", dive);
        stop();
        fresh();
        set(50, 0, 0, 0, 0);
        play(8u * BAR_BLOCKS(120), 0);
        for (nm = 0, i = 0; i < ntrig; i++)
            nm += trigs[i].mg != 0;
        check("MANGLE 0: nothing mangled", nm == 0u, "%u", nm);
        stop();
    }

    /* 6: the demos */
    {
        typedef struct { const char *file; int loop, bars, cmplx, anchr, roll, fill, rtg; uint32_t bpm, nbars, phrase, bch; uint16_t on; } dm_t;
        static const dm_t DM[] = {
            {"part-straight.wav", 0, 0, 0, 0, 0, 0, 0, 137, 4, 0, 0, 0xFFFFu},
            {"part-amen3.wav", 1, 0, 0, 0, 0, 0, 0, 137, 4, 0, 0, 0xFFFFu},
            {"part-amen4.wav", 2, 0, 0, 0, 0, 0, 0, 139, 4, 0, 0, 0xFFFFu},
            {"part-holes.wav", 0, 0, 0, 0, 0, 0, 0, 120, 4, 0, 0, 0xB6DBu},
            {"part-gen.wav", 0, 0, 50, 0, 0, 0, 0, 120, 8, 0, 0, 0xFFFFu},
            {"part-groove.wav", 0, 0, 30, 70, 40, 60, 0, 120, 8, 2, 0, 0x5555u},
            {"part-cut-up.wav", 0, 0, 85, 0, 0, 40, 25, 120, 8, 2, 0, 0xFFFFu},
            {"part-rolls.wav", 0, 0, 40, 30, 0, 50, 70, 120, 8, 2, 0, 0xFFFFu},
            {"part-user-loop.wav", BP_NB + 3, 0, 40, 40, 0, 40, 10, 96, 8, 2, 30, 0xFFFFu},
            {"part-two-loops.wav", BP_NB + 4, 0, 40, 30, 0, 50, 20, 96, 8, 2, 100, 0xFFFFu},
            {"part-jungle.wav", 0, 0, 60, 20, 20, 60, 30, 172, 16, 2, 60, 0xFFFFu},
        };
        mkdir(dir, 0777);
        for (k = 0; k < sizeof DM / sizeof DM[0]; k++) {
            const dm_t *m = &DM[k];
            uint32_t frames, fr, pk = 0;
            FILE *w;
            snprintf(path, sizeof path, "%s/%s", dir, m->file);
            if (!(w = fopen(path, "wb")))
                return 1;
            fresh();
            TBRK->p[P_REV] = 16;                        /* (as it powers on) */
            song.g[G_BPM] = (int16_t)m->bpm;
            set(m->cmplx, m->anchr, m->roll, m->fill, m->rtg);
            steps_on(m->on);
            BP(BP_PHRASE) = (int16_t)m->phrase;
            BP(BP_BCH) = (int16_t)m->bch;
            BP(BP_CCH) = BP(BP_DCH) = (int16_t)(m->bch / 2u);
            BP(BP_LOOPA) = (int16_t)m->loop;
            BP(BP_LOOPB) = (int16_t)(m->loop == BP_NB + 4 ? BP_NB + 5u : 1u);
            BP(BP_BARSA) = (int16_t)m->bars;
            brkp_poll();
            frames = (uint32_t)(m->nbars * 4.0 * 60.0 / m->bpm * FS);
            wav_hdr(w, frames + FS / 2u);
            transport_req = 1;
            for (fr = 0; fr < frames + FS / 2u; fr += CTL) {
                int32_t o[2 * CTL];
                if (fr >= frames && fr < frames + CTL)
                    transport_req = 2;
                mix_block(o, CTL);
                for (i = 0; i < CTL; i++) {
                    pk = (uint32_t)abs(o[2 * i]) > pk ? (uint32_t)abs(o[2 * i]) : pk;
                    wav_put(w, o[2 * i], o[2 * i + 1]);
                }
            }
            fclose(w);
            printf("brkpart: demo %-22s %u bars at %u BPM, peak %u\n", m->file, m->nbars, m->bpm, pk);
            fails += pk < 2000u || pk > 32767u;
        }
    }
    {   /* the styles at 170 BPM, 8 bars each, on AMEN 2 (B, C: AMEN 3, 4 at the phrase ends) */
        static const struct { const char *file; uint32_t st; } SD[] = {
            {"style-roller.wav", BS_ROLLER}, {"style-jungle.wav", BS_JUNGLE}, {"style-halftime.wav", BS_HALFTIME},
            {"style-breakcore.wav", BS_BRKCORE}, {"style-chaos.wav", BS_CHAOS}};
        uint32_t k, i;
        char path[512];
        for (k = 0; k < sizeof SD / sizeof SD[0]; k++) {
            uint32_t frames = (uint32_t)(8.0 * 4.0 * 60.0 / 170.0 * FS), fr, pk = 0;
            FILE *w;
            snprintf(path, sizeof path, "%s/%s", dir, SD[k].file);
            if (!(w = fopen(path, "wb")))
                return 1;
            fresh();
            TBRK->p[P_REV] = 16;
            song.g[G_BPM] = 170;
            brk_style(TBRK, SD[k].st);
            BP(BP_BCH) = 40;
            BP(BP_CCH) = 30;
            brkp_poll();
            wav_hdr(w, frames + FS / 2u);
            transport_req = 1;
            for (fr = 0; fr < frames + FS / 2u; fr += CTL) {
                int32_t o[2 * CTL];
                if (fr >= frames && fr < frames + CTL)
                    transport_req = 2;
                mix_block(o, CTL);
                for (i = 0; i < CTL; i++) {
                    pk = (uint32_t)abs(o[2 * i]) > pk ? (uint32_t)abs(o[2 * i]) : pk;
                    wav_put(w, o[2 * i], o[2 * i + 1]);
                }
            }
            fclose(w);
            printf("brkpart: demo %-22s 8 bars at 170 BPM, peak %u\n", SD[k].file, pk);
            fails += pk < 2000u || pk > 32767u;
        }
    }
    {   /* a session: the BREAK in JUNGLE, a bass line on REESE SUB / JUNGLE SUB ducked by the loop's kicks, 170 BPM */
        static const struct { const char *file; uint32_t preset; } SS[] = {{"session-reese.wav", 22}, {"session-sub.wav", 24}};
        static const int8_t LINE[32] = {38, -1, -1, -1, -1, -1, 41, 0, 38, -1, -1, -1, -1, 0, 36, -1,
                                        33, -1, -1, -1, -1, -1, 36, 0, 38, -1, -1, -1, 41, -1, 40, -1};   /* -1 tie, 0 rest */
        uint32_t k, i;
        char path[512];
        for (k = 0; k < sizeof SS / sizeof SS[0]; k++) {
            uint32_t frames = (uint32_t)(8.0 * 4.0 * 60.0 / 170.0 * FS), fr, pk = 0;
            FILE *w;
            snprintf(path, sizeof path, "%s/%s", dir, SS[k].file);
            if (!(w = fopen(path, "wb")))
                return 1;
            fresh();
            TBRK->p[P_REV] = 16;
            song.g[G_BPM] = 170;
            song.g[G_DUCK] = GP[G_DUCK].def;
            brk_style(TBRK, BS_JUNGLE);
            BP(BP_BCH) = 40;
            host_preset(&trk[0], ENGI_ANALOG, SS[k].preset);
            trk[0].p[P_LEVEL] = 100;
            trk[0].p[P_SLEN] = 32;
            for (i = 0; i < 32u; i++) {
                uint8_t n = (uint8_t)(LINE[i] > 0 ? LINE[i] : 0);
                put_step(&trk[0], i, LINE[i] > 0 ? 1u : 0u, &n, LINE[i] > 0 ? ST_NOTE : LINE[i] < 0 ? ST_TIE : ST_REST,
                         LINE[i] > 0 && LINE[i + (i < 31u)] < 0 ? SF_SLIDE : 0u);
            }
            trk[0].seq_active = 1;
            brkp_poll();
            wav_hdr(w, frames + FS / 2u);
            transport_req = 1;
            for (fr = 0; fr < frames + FS / 2u; fr += CTL) {
                int32_t o[2 * CTL];
                if (fr >= frames && fr < frames + CTL)
                    transport_req = 2;
                mix_block(o, CTL);
                for (i = 0; i < CTL; i++) {
                    pk = (uint32_t)abs(o[2 * i]) > pk ? (uint32_t)abs(o[2 * i]) : pk;
                    wav_put(w, o[2 * i], o[2 * i + 1]);
                }
            }
            fclose(w);
            printf("brkpart: demo %-22s 8 bars at 170 BPM, the BREAK + a bass ducked, peak %u\n", SS[k].file, pk);
            fails += pk < 2000u || pk > 32767u;
        }
    }
    printf(fails ? "brkpart: %d FAILED\n" : "brkpart: all ok\n", fails);
    return fails != 0;
}
