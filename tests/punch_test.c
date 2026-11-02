/* SPDX-License-Identifier: GPL-3.0-only */
/* The moves (punch.c): the mix effects (REWIND, WASH) each change the mix while on, stay bounded, and give the exact dry
 * mix back once off (after the 64-sample fade); the moves (LIVE's keys 13..16, AUTO SONG's sections) do what they say:
 * ROLL 16 into the BREAK, FILL the steps' fill, VERB the reverb, REWIND back on the bar, WASH thins the mix (a high-pass
 * up over a bar: a low tone gone, a high one through) into an echo, the reverb open; the last mix effect pressed wins
 * and the one before comes back. The keyboard with FX held (STUDIO, as LIVE) is the FX panel's (lfx.c): a white key its
 * effect, the black keys its tracks, LATCH a toggle, CLEAR; it plays and records nothing.
 * argv[1]: a WAV demo (a jungle loop, then WASH, ROLL, REWIND).
 *   tests/punch_test OUT.wav */
#define main hostsim_main
#include "hostsim.c"
#undef main
#include <assert.h>
#define BLOCKS (FS * 3u / CTL)                        /* 3 s at 120 BPM */
static int32_t got[CTL * 2u];
static int fails;
static void check(int ok, const char *what) { printf("moves: %-74s %s\n", what, ok ? "ok" : "FAIL"); fails += !ok; }

static void beat_setup(void)                         /* the BREAK track: AMEN 2 as it is (CMPLX 0), 16 steps */
{
    host_tracks_init();
    song.g[G_BPM] = 120;
    TBRK->p[P_E0] = 0;                                /* (CMPLX 0: the slices in order, no random choice) */
    TBRK->p[P_SLEN] = 16;
    punch.req = -1;
    punch.cur = -1;
    punch.g = 0;
    live_keys = 0;
    as_keys = 0;
    perf.prev = perf.on = perf.wait = 0;
    clk_beat = 0;
    clk_pos = 0;
    brkp_poll();
    transport_req = 1;
}
static void blocks(uint32_t n)
{
    while (n--)
        mix_block(got, CTL);
}

/* a deterministic "mix": a decaying 110 Hz tone every quarter + noise bursts every 1/16 */
static void test_mix(uint32_t b, int32_t *l, int32_t *r)
{
    static uint32_t seed = 1;
    uint32_t i;
    if (b == 0) seed = 1;
    for (i = 0; i < CTL; i++) {
        uint32_t t = b * CTL + i, q = t % (FS / 2u), s16 = t % (FS / 8u);
        int32_t tone = (sine_i((uint32_t)t * 10712093u) * (int32_t)(FS / 2u - q) / (int32_t)(FS / 2u)) >> 1;
        int32_t nz = s16 < 2000u ? (int32_t)((seed = seed * 1664525u + 1013904223u) >> 18) - 8192 : 0;
        l[i] = (tone + nz) * 3;
        r[i] = (tone - nz) * 3;
    }
}

int main(int argc, char **argv)
{
    uint32_t fx, i, b, on = FS / CTL, off = on + FS / 2u / CTL;   /* held from 1 s to 1.5 s */
    FILE *f = argc > 1 ? fopen(argv[1], "wb") : 0;
    /* ---- the mix effects: on, off, dry again */
    for (fx = 0; fx < PUNCH_NFX; fx++) {
        uint64_t diff = 0;
        uint32_t bounded = 1, dry = 1;
        beat_setup();
        song.playing = 1;
        clk_pos = 0;
        perf.rise = 0;
        for (b = 0; b < BLOCKS; b++) {
            int32_t l[CTL], r[CTL], l0[CTL], r0[CTL];
            test_mix(b, l, r);
            memcpy(l0, l, sizeof l), memcpy(r0, r, sizeof r);
            if (b == on) punch.req = (int8_t)fx;
            if (b == off) punch.req = -1;
            if (b >= on && b < off)
                perf.rise += CTL;                     /* (WASH's progress: perf_block's) */
            punch_process(l, r, CTL);
            clk_pos = (clk_pos + CTL * (uint32_t)song.g[G_BPM]) % BEAT_U;   /* (the transport clock) */
            for (i = 0; i < CTL; i++) {
                bounded &= l[i] > -(1 << 22) && l[i] < (1 << 22) && r[i] > -(1 << 22) && r[i] < (1 << 22);
                if (b < on || b >= off + 3u)
                    dry &= l[i] == l0[i] && r[i] == r0[i];   /* dry before, dry again after */
                else
                    diff += (uint64_t)(l[i] > l0[i] ? l[i] - l0[i] : l0[i] - l[i]);
            }
        }
        {
            static const char *const NAME[PUNCH_NFX] = {"REWIND", "WASH", "TAPE STOP"};
            char w[80];
            snprintf(w, sizeof w, "%s: changes the mix while on, bounded, the dry mix after", NAME[fx]);
            check(diff > 2000u * (off - on) && bounded && dry && punch.cur == -1, w);
        }
        song.playing = 0;
    }

    /* ---- the tape stop: a steady 440 Hz through it for a beat; its pitch falls (fewer zero crossings), to silence */
    {
        uint32_t beat = (uint32_t)FS * 60u / (uint32_t)song.g[G_BPM], zc[4] = {0}, t = 0;
        int32_t prev = 0, pk_end = 0;
        beat_setup();
        song.playing = 1;
        punch.req = -1;
        for (b = 0; b < 40u; b++) {                   /* (the ring filled with the tone first) */
            int32_t l[CTL], r[CTL];
            for (i = 0; i < CTL; i++, t++)
                l[i] = r[i] = sine_i(t * 42852372u) >> 2;
            punch_process(l, r, CTL);
        }
        punch.req = PX_TSTOP;
        for (b = 0; b * CTL < beat; b++) {
            int32_t l[CTL], r[CTL];
            for (i = 0; i < CTL; i++, t++)
                l[i] = r[i] = sine_i(t * 42852372u) >> 2;
            punch_process(l, r, CTL);
            for (i = 0; i < CTL; i++) {
                uint32_t q = (b * CTL + i) * 4u / beat;
                zc[q & 3u] += (prev < 0) != (l[i] < 0);
                prev = l[i];
                if ((b * CTL + i) * 16u >= beat * 15u)
                    pk_end = l[i] > pk_end ? l[i] : -l[i] > pk_end ? -l[i] : pk_end;
            }
        }
        check(zc[0] > 2u * zc[2] && zc[2] > zc[3] && pk_end < (sine_i(1u << 30) >> 2) / 5,
              "TAPE STOP: the pitch falls over the beat (quarter 1 against 3), silent at its end");
        punch.req = -1;
        song.playing = 0;
    }

    /* ---- the moves (as LIVE's keys and AUTO SONG's sections ask for them) */
    beat_setup();
    blocks(40);
    live_keys = 1u << PA_ROLL16;
    blocks(1);
    check(bp.roll == 2u, "ROLL 16: two hits a trigger");
    live_keys = 0;
    blocks(1);
    check(!bp.roll, "... let go: the BREAK as it was");
    as_keys = 1u << PA_FILL | 1u << PA_VERB;
    blocks(2);
    check(fill_now && perf_verb == 2u, "FILL: the steps' fill; VERB: the reverb x3 (AUTO SONG's)");
    as_keys = 0;
    blocks(2);
    check(!perf_verb, "... over: the reverb as it was");
    live_gap = 1;
    blocks(2);
    check(trk_silent(TBRK) && !trk_silent(&trk[0]), "GAP (LIVE): the BREAK out, the BASS on");
    live_gap = 0;
    blocks(2);
    check(!trk_silent(TBRK), "... let go: back");
    live_keys = 1u << PA_REWIND;
    blocks(2);
    check(punch.req == PX_REWIND, "REWIND: the mix spun back");
    live_keys = 1u << PA_REWIND | 1u << PA_WASH;
    blocks(1);
    check(punch.req == PX_WASH && perf_verb == 2u, "... and WASH: the last pressed plays, the reverb open");
    live_keys = 1u << PA_REWIND;
    blocks(1);
    check(punch.req == PX_REWIND, "... WASH let go: REWIND again");
    while ((clk_pos / (BEAT_U / 64u)) < 8u || clk_beat % 4u != 1u)   /* let go a little into a bar's 2nd beat */
        blocks(1);
    live_keys = 0;
    blocks(2);
    check(perf.wait == 1u << PA_REWIND && punch.req == PX_REWIND, "REWIND let go: it waits for the bar");
    {
        uint32_t bar = clk_beat / 4u, n = 0;
        while (clk_beat / 4u == bar && n < 2000u) {
            blocks(1);
            n++;
        }
        blocks(1);
        check(!perf.wait && punch.req < 0, "... on the bar: the mix back");
    }
    blocks(80);
    check(punch.cur == -1 && !perf.on && !perf_mute, "all let go: nothing left on");

    /* ---- the keyboard while FX is held (STUDIO, as LIVE): the FX panel; nothing sounds, nothing records */
    {
        uint32_t k, ok = 1;
        beat_setup();
        song.sel = 0;
        song.rec = 1u;
        mix_block(got, CTL);
        ly_bit[LY_FX] = 1u << 2;                      /* FX held (seq.c layer_now) */
        fm1_in.buttons = ly_bit[LY_FX];
        for (k = 0; k < 54u; k++) {                   /* (the second time round: the BREAK track selected) */
            int32_t w = white_of_key(k % 27u), j;
            uint32_t h = bp.st_hits;
            for (j = 0; j < 6 && LIVE_BLACK[j] != k % 27u; j++)
                ;
            song.sel = (uint8_t)(k < 27u ? 0u : TRK_BRK);
            song.rec = (uint8_t)(1u << song.sel);
            fm1_in.notes = 1u << (k % 27u);
            events_block(CTL);
            ok &= w >= 0 ? lfx.held == 1u << w : j < 4 ? lfx.black == 1u << j : !lfx.held;
            ok &= !trk[0].step[0].n && !trk[0].v[0].active && bp.st_hits == h && !TBRK->bstep[0].slice;
            fm1_in.notes = 0;
            events_block(CTL);
            ok &= !lfx.held && !lfx.black;
            if (j == 4) {                             /* (LATCH went to TRACKS: round to OFF again) */
                key_down(k % 27u), key_up(k % 27u);
                key_down(k % 27u), key_up(k % 27u);
            }
            lfx_block();
        }
        check(ok && !lfx.mode && !lfx.latch, "FX held: a white key its effect, a black key its track; nothing plays, nothing records");
        fm1_in.notes = 1u << LIVE_BLACK[4];           /* LATCH TRACKS with BASS held: the BASS stuck; white key 1 held only;
                                                       * LATCH TRACKS + FX: white key 1 tapped sticks */
        events_block(CTL);
        fm1_in.notes = 1u << LIVE_BLACK[4] | 1u << LIVE_BLACK[1];
        events_block(CTL);
        fm1_in.notes = 1u << LIVE_BLACK[1];
        events_block(CTL);
        check(lfx.mode == LM_TRACKS && lfx.tsel == 2u, "LATCH tapped: TRACKS, a track tapped sticks");
        fm1_in.notes = 0;
        events_block(CTL);
        fm1_in.notes = 1u << 0;                       /* (white key 1: key 0, F) */
        events_block(CTL);
        check(lfx.held == 1u && lfx.tgt[0] == 1u && !lfx.latch, "... LATCH TRACKS: a white key held, on the track stuck");
        fm1_in.notes = 0;
        events_block(CTL);
        check(!lfx.held && !lfx.latch, "... let go: off (the effects do not stick)");
        fm1_in.notes = 1u << LIVE_BLACK[4];
        events_block(CTL);
        fm1_in.notes = 0;
        events_block(CTL);
        fm1_in.notes = 1u << 0;
        events_block(CTL);
        fm1_in.notes = 0;
        events_block(CTL);
        check(lfx.mode == LM_ALL && lfx.latch == 1u && lfx.tgt[0] == 1u && !lfx.held, "LATCH tapped again: TRACKS + FX, a white key tapped sticks, on that track");
        fm1_in.notes = 1u << LIVE_BLACK[5];
        events_block(CTL);
        fm1_in.notes = 0;
        events_block(CTL);
        lfx_block();
        check(!lfx.latch && !lfx.mode && !lfx.tsel, "CLEAR: nothing stuck, LATCH off");
        fm1_in.buttons = 0;
        song.rec = 0;
        transport_req = 2;
        events_block(CTL);
    }
    if (f) {   /* demo at 170: two bars dry, WASH a bar, ROLL, REWIND */
        uint32_t bar = (uint32_t)(4.0 * 60.0 / 170.0 * FS / CTL), total = bar * 12u, q;
        static int32_t blk[CTL * 2u];
        wav_hdr(f, total * CTL);
        beat_setup();
        song.g[G_BPM] = 170;
        brk_style(TBRK, BS_JUNGLE);
        host_preset(&trk[0], ENGI_ANALOG, 22);
        bl_write();
        host_preset(&trk[1], ENGI_SAMPLE, 0);
        cg.play = CP_HOLD;
        cg_poll();
        for (b = 0; b < total; b++) {
            uint32_t at = b / bar, in = b % bar;
            live_keys = (uint16_t)(at == 3u ? 1u << PA_WASH :
                                   at == 6u && in > bar / 2u ? 1u << PA_ROLL16 :
                                   at == 8u && in > bar * 3u / 4u ? 1u << PA_REWIND : 0u);
            mix_block(blk, CTL);
            for (q = 0; q < CTL; q++) wav_put(f, blk[2 * q], blk[2 * q + 1]);
        }
        live_keys = 0;
        fclose(f);
        printf("moves: demo %s: 12 bars at 170 (WASH, ROLL 16, REWIND)\n", argv[1]);
    }
    {   /* WASH, all the way up: a 55 Hz tone gone, a 4 kHz one through (and its echo); the reverb open */
        static const uint32_t HZ[2] = {55u, 4000u};
        double ratio[2];
        uint32_t k;
        for (k = 0; k < 2u; k++) {
            double ein = 0, eout = 0;
            beat_setup();
            song.playing = 1;
            punch.req = PX_WASH;
            perf.rise = sweep_samples() * 2u;           /* (past SWEEP: the high-pass at its top) */
            memset(punch_ring, 0, sizeof punch_ring);   /* (the echo: nothing of the tests before) */
            for (b = 0; b < (uint32_t)(FS * 2u / CTL); b++) {
                int32_t l[CTL], r[CTL];
                for (i = 0; i < CTL; i++)
                    l[i] = r[i] = sine_i((b * CTL + i) * (uint32_t)(4294967296.0 * HZ[k] / FS)) >> 2;
                if (b >= (uint32_t)(FS / CTL))          /* (the second second: the ring is the wash's own) */
                    for (i = 0; i < CTL; i++)
                        ein += (double)l[i] * l[i];
                punch_process(l, r, CTL);
                if (b >= (uint32_t)(FS / CTL))
                    for (i = 0; i < CTL; i++)
                        eout += (double)l[i] * l[i];
            }
            ratio[k] = sqrt(eout / ein);
        }
        live_keys = 1u << PA_WASH;
        perf_block(CTL);
        check(ratio[0] < 0.15 && ratio[1] > 0.7 && perf_verb == 2u && punch.req == PX_WASH,
              "WASH at its top: 55 Hz gone, 4 kHz through (with its echo), the reverb open");
        if (ratio[0] >= 0.15 || ratio[1] <= 0.7)
            printf("moves:   wash 55 Hz %.3f, 4 kHz %.3f\n", ratio[0], ratio[1]);
        live_keys = 0;
        perf_block(CTL);
    }
    {   /* LIVE, the BREAK in focus: black keys 1..4 hold its loop A..D, now (Q 4 BARS), in place; the last one down wins;
         * let go, the generator's loop; black key 5 still a slice */
        int ok = 1;
        uint32_t h;
        beat_setup();
        q_mode = QM_4BAR;
        kb_live = 1;
        song.sel = TRK_BRK;
        fm1_in.buttons = 0;
        blocks(BLOCKS / 4u);
        ok &= bp.running && bp_bank() == 0u && brkp_bank_src(1) && brkp_bank_src(3);
        key_down(LIVE_BLACK[1]);
        ok &= bp_bank() == 1u && bp.v[bp.cur].bank == 1u && kb_kind[LIVE_BLACK[1]] == KS_BANK;
        blocks(BLOCKS / 4u);
        ok &= bp.st_bank == 1;
        key_down(LIVE_BLACK[3]);
        ok &= bp_bank() == 3u && bp.v[bp.cur].bank == 3u;
        key_up(LIVE_BLACK[3]);
        ok &= bp_bank() == 1u && bp.v[bp.cur].bank == 1u;
        key_up(LIVE_BLACK[1]);
        ok &= !bp.hold && bp_bank() == (brkp_bank_src(bp.engine_bank) ? bp.engine_bank : 0u);
        h = bp.st_hits;
        key_down(LIVE_BLACK[4]);
        key_up(LIVE_BLACK[4]);
        ok &= bp.st_hits == h + 1u;
        check(ok, "LIVE: BREAK black keys 1..4 hold loop A..D now (whatever Q), let go: back; key 5 a slice");
        kb_live = 0;
        q_mode = 0;
        song.sel = 0;
        transport_req = 2;
        events_block(CTL);
    }
    printf(fails ? "moves: %d FAILED\n" : "moves: the moves, their mix effects, the FX-held keyboard PASS\n", fails);
    return fails != 0;
}
