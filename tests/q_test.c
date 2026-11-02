/* SPDX-License-Identifier: GPL-3.0-only */
/* Q (firmware/src/q.c): with Q at BAR and the transport running, what is queued waits for the next bar and lands on
 * its first block, before that block's steps (a parameter, an engine and preset with the voices released, a mute, a
 * solo, the CHORDS generator with its progression, the BASS phrase); at 4 BARS on the next four-bar phrase; at NOW,
 * or stopped, at once. Knobs read what waits (q_val). */
#define main hostsim_main
#include <stdarg.h>
#include "hostsim.c"
#undef main

static int fails;
static void check(int ok, const char *what) { printf("q: %-76s %s\n", what, ok ? "ok" : "FAIL"); fails += !ok; }
static int32_t o[2 * CTL];
static uint32_t bar(void) { return clk_beat >> 2; }
static void blocks(uint32_t n)
{
    while (n--)
        mix_block(o, CTL);
}
/* run until the block before the next bar (clk_beat at the bar's last 16th) */
static void to_bar_end(void)
{
    uint32_t b = bar(), guard = 100000;
    while (guard--) {
        if (bar() != b)
            return;
        if ((clk_beat & 3u) == 3u && clk_pos > BEAT_U - BEAT_U / 16u)
            return;
        mix_block(o, CTL);
    }
}
/* run through the next bar's first block (the clock crosses at the end of one block; the next plays its downbeat) */
static void to_next_bar(void)
{
    uint32_t b = bar(), guard = 100000;
    while (bar() == b && guard--)
        mix_block(o, CTL);
    mix_block(o, CTL);
}

int main(void)
{
    host_tracks_init();
    song.g[G_BPM] = 170;
    q_mode = QM_BAR;
    /* stopped: at once */
    q_param(1, P_LEVEL, 33);
    check(trk[1].p[P_LEVEL] == 33 && !q_waits(), "stopped: a change at once (nothing waits)");
    transport_req = 1;
    blocks(10);
    /* a parameter, an engine and preset, a mute, a solo */
    q_param(1, P_LEVEL, 77);
    q_param(1, P_LEVEL, 88);                        /* (the same one again: the last wins) */
    q_param(0, QX_PRESET, 3);
    q_param(2, P_MUTE, 1);
    q_solo(1u << 1);
    qq.panic |= 1u;
    check(q_waits() && qq.n == 3u && trk[1].p[P_LEVEL] == 33 && q_val(1, P_LEVEL) == 88,
          "playing, Q BAR: the changes wait (q_val reads them, the track as it was)");
    to_bar_end();
    check(trk[1].p[P_LEVEL] == 33 && !trk[2].p[P_MUTE] && !song.solo && trk[0].preset != 3,
          "... nothing changes before the bar");
    {
        to_next_bar();
        check(trk[1].p[P_LEVEL] == 88 && trk[2].p[P_MUTE] == 1 && song.solo == 2u && trk[0].preset == 3 && !q_waits(),
              "... on the bar's first block: all of it, and nothing waits after");
        check(!(clk_beat & 3u) && clk_pos < BEAT_U / 8u, "... that block is the bar's first (its downbeat plays it)");
    }
    trk[2].p[P_MUTE] = 0;
    song.solo = 0;
    /* the CHORDS generator: its settings and progression together */
    {
        chord_cfg_t *c;
        uint8_t was[CG_MAX], want[CG_MAX];
        cg_defaults();
        cg.play = CP_HOLD;
        cg_poll();
        memcpy(was, cgr.prog, sizeof was);
        c = q_cg();
        c->var = 77;
        q_cg_done();
        {
            chord_cfg_t live = cg;
            cg = qq.cg;
            cg_generate(want, cg_mask());
            cg = live;
        }
        blocks(2);
        cg_poll();
        check(cg.var != 77 && !memcmp(cgr.prog, was, sizeof was), "CHORDS: VARIANT 77 waits, the progression playing as it was");
        to_next_bar();
        cg_poll();
        check(cg.var == 77 && !memcmp(cgr.prog, want, sizeof want), "... on the bar: VARIANT 77 and its progression");
    }
    /* the BASS phrase */
    {
        static step_t want[BL_STEPS], before[BL_STEPS];
        bl_defaults();
        bl.var = 5;
        memcpy(before, trk[0].step, sizeof before);
        bl_write();
        bl_build(want);
        blocks(1);
        check(!memcmp(trk[0].step, before, sizeof before) && qq.st_set && !strcmp(qq.what[0], "PHRASE"),
              "BASS: the phrase written waits (its steps as they were)");
        to_next_bar();
        check(!memcmp(trk[0].step, want, sizeof want) && bl_on(), "... on the bar: the new phrase");
    }
    /* 4 BARS: the next phrase of four */
    {
        uint32_t landed;
        q_mode = QM_4BAR;
        while (!((bar() & 3u) == 1u))
            to_next_bar();
        q_param(1, P_LEVEL, 99);
        to_next_bar();
        to_next_bar();
        check(trk[1].p[P_LEVEL] == 88, "Q 4 BARS: two bars on, it still waits");
        while (q_waits())
            mix_block(o, CTL);
        landed = bar();
        check(!(clk_beat & 3u) && clk_pos < BEAT_U / 8u, "... the block it lands in is the phrase's first");
        check(trk[1].p[P_LEVEL] == 99 && !(landed & 3u), "... it lands on the phrase's first bar");
    }
    /* NOW */
    q_mode = QM_NOW;
    q_param(1, P_LEVEL, 11);
    check(trk[1].p[P_LEVEL] == 11 && !q_waits(), "Q NOW, playing: at once");
    /* stopped with something waiting: at once */
    q_mode = QM_BAR;
    q_param(1, P_LEVEL, 22);
    transport_req = 2;
    blocks(3);
    check(trk[1].p[P_LEVEL] == 22 && !q_waits(), "stopped with a change waiting: it lands at once");
    printf(fails ? "q: %d FAILED\n" : "q: all ok\n", fails);
    return fails != 0;
}
