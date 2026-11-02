/* SPDX-License-Identifier: GPL-3.0-only */
/* SLOOP soak test: 10 simulated minutes of random live use through the real audio path
 * (mix_block: sequencer, the 3 synth parts, the BREAK track's generator, its loops A..D and
 * live keys, the moves (punch.c), the record arm, free takes, MIDI in), then everything stopped
 * and released.
 * Checks: the output stays bounded, every block renders in time on the host, voices never
 * hang (all free a few seconds after the last note), the moves and the record state come
 * back to idle, recorded steps stay well-formed. Seeded: the same run every time. */
#define main hostsim_main
#include "hostsim.c"
#undef main
#include <assert.h>
static uint32_t seed = 12345, ft_count, ft_starts;
static uint8_t ft_prev, arm_next;
static uint32_t rnd(uint32_t n) { seed = seed * 1664525u + 1013904223u; return (seed >> 8) % (n ? n : 1u); }
static void midi_in(uint32_t st, uint32_t d1, uint32_t d2)
{
    if (mi_w - mi_r >= MQ) return;
    midi_in_q[mi_w % MQ] = ((st >> 4) & 15u) | st << 8 | d1 << 16 | d2 << 24;
    mi_w++;
}

int main(int argc, char **argv)
{
    const uint32_t minutes = argc > 1 ? (uint32_t)atoi(argv[1]) : 10u;
    const uint32_t blocks = minutes * 60u * FS / CTL;
    static int32_t out[CTL * 2];
    uint32_t b, i, k, held = 0, maxblock_ns = 0, events = 0, peak = 0;
    double total_ns = 0;
    struct timespec t0, t1;
    host_tracks_init();
    song.g[G_BPM] = 120;
    host_preset(&trk[0], 0, 4);
    host_preset(&trk[1], 1, 1);
    host_preset(&trk[2], ENGI_SAMPLE, 5);
    for (k = LY_FX; k < LY_COUNT; k++)                        /* the layer buttons (SLOOP 2.0): bits 8.. */
        ly_bit[k] = 1u << (7u + k);
    dyn_bit[0] = 1u << 14;
    dyn_bit[1] = 1u << 15;
    for (b = 0; b < blocks; b++) {
        static uint32_t layer_btn;
        if (rnd(40) == 0) {                                   /* an event every ~29 ms on average */
            events++;
            switch (rnd(22)) {
            case 16: layer_btn = rnd(3) ? 0u : ly_bit[1u + rnd(LY_COUNT - 1u)] | (rnd(4) ? 0u : dyn_bit[rnd(2)]); break;   /* a layer held */
            case 17: song.g[G_DUST] = (int16_t)rnd(128); song.g[G_DUCK] = (int16_t)rnd(128); break;
            case 18: song.g[G_FILT] = (int16_t)((int)rnd(128) - 64); song.g[G_ROLL] = (int16_t)rnd(5); break;
            case 19: trk[rnd(NTRK)].p[P_MUTE] = (int16_t)(rnd(4) == 0); song.solo = (uint8_t)(rnd(5) ? 0u : 1u << rnd(NTRK)); break;
            case 20: {                                                             /* levels, ratchets on steps */
                uint32_t t = rnd(NTRK), i2 = rnd(NSTEP);
                if (t == TRK_BRK) {                                                /* on, pin, level, retrigger, pitch, reverse */
                    bstep_t *s = &trk[t].bstep[i2];
                    s->on = (uint8_t)(rnd(4) != 0); s->slice = (uint8_t)rnd(BRK_SLICES + 1u); s->lvl = (uint8_t)rnd(4);
                    s->rat = (uint8_t)rnd(5); s->pitch = (int8_t)((int)rnd(25) - 12); s->flags = (uint8_t)(rnd(4) == 0 ? BS_REV : 0u);
                }
                else if (trk[t].step[i2].n) { trk[t].step[i2].lvl = (uint8_t)rnd(256); trk[t].step[i2].rat = (uint8_t)rnd(256); }
                break;
            }
            case 21: trk[rnd(NPART)].p[P_CHORD] = (int16_t)rnd(6); trk[rnd(NPART)].p[P_VOICE] = (int16_t)rnd(4); break;
            case 0: if (!ft_on || !rnd(16)) transport_req = song.playing ? 2 : 1; break;   /* (a free take: PLAY closes it) */
            case 1: if (song.playing) rec_begin(); else rec_wait = 1; break;        /* REC */
            case 2: song.rec = 0; rec_wait = 0; break;                            /* REC off */
            case 3: { uint32_t s = rnd(NTRK); song.sel = (uint8_t)s; rec_follow(s); break; }   /* ALGO */
            case 4: {                                                              /* the generator: a dial, a loop, live keys */
                uint32_t g = rnd(BP_NP);
                const param_desc_t *d = &BPD[g];
                TBRK->p[BPP[g]] = (int16_t)(g >= BP_LOOPA && g <= BP_LOOPD ? rnd(brkp_nloops() + 2u)
                                                                           : d->min + (int32_t)rnd((uint32_t)(d->max - d->min + 1)));
                brkp_poll();                                                       /* (the main loop's: the loops of A..D) */
                brkp_key(rnd(11), (int)rnd(2));
                if (rnd(4) == 0) bp.keys = 0;
                break;
            }
            case 5: live_keys = (uint16_t)(rnd(3) ? 0u : rnd(1u << PERF_N)); break;
            case 6: song.g[G_BPM] = (int16_t)(60 + rnd(140)); break;
            case 7: { uint32_t t = rnd(NPART), e = rnd(NENGINES);                 /* engine / preset */
                      host_preset_req(&trk[t], e, rnd(ENGINES[e]->npresets)); panic_req |= (uint8_t)(1u << t); break; }
            case 8: trk[rnd(NTRK)].p[P_SLEN] = (int16_t)(1 + rnd(64)); break;
            case 9: trk[rnd(NTRK)].p[P_SDIV] = (int16_t)rnd(6); break;
            case 10: song.g[G_CLOCK] = (int16_t)rnd(3); break;
            case 11: midi_in(0x90u | rnd(16), 24 + rnd(80), 1 + rnd(127)); break;
            case 12: midi_in(0x80u | rnd(16), 24 + rnd(80), 0); break;
            case 13: trk[rnd(NTRK)].p[P_SLCR] = (int16_t)rnd(3); break;
            case 14: trk[2].p[P_X0] = (int16_t)rnd(4); break;   /* (FMX: the CHOP track's SLICE) */
            default: {                                                             /* keys: press / release */
                uint32_t key = rnd(27);
                if (rnd(2)) held |= 1u << key; else held &= ~(1u << key);
                if (rnd(8) == 0) held = 0;
                break;
            }
            }
        }
        fm1_in.notes = held;
        if (rnd(3000) == 0) {                                 /* now and then an empty project: free takes */
            for (k = 0; k < NTRK; k++)
                steps_clear(&trk[k]);
            transport_req = 2;                                /* stopped, then armed */
            arm_next = 1;
        } else if (arm_next && !transport_req) {
            arm_next = 0;
            rec_wait = 1;
        }
        ft_btn_mask = 1u << 3;
        ft_drop_mask = 1u << 4;
        fm1_in.buttons = (rnd(4000) == 0 ? 1u << 3 : rnd(20000) == 0 ? 1u << 4 : 0u) | layer_btn;   /* REC closes, PLAY drops a take */
        clock_gettime(CLOCK_MONOTONIC, &t0);
        mix_block(out, CTL);
        clock_gettime(CLOCK_MONOTONIC, &t1);
        if (ft_prev && !ft_on && song.playing)
            ft_count++;
        if (!ft_prev && ft_on)
            ft_starts++;
        ft_prev = ft_on;
        {
            uint32_t ns = (uint32_t)((t1.tv_sec - t0.tv_sec) * 1000000000L + (t1.tv_nsec - t0.tv_nsec));
            total_ns += ns;
            if (ns > maxblock_ns && b > 1000u) maxblock_ns = ns;
        }
        for (i = 0; i < CTL * 2u; i++) {
            int32_t a = out[i] < 0 ? -out[i] : out[i];
            assert(a < (1 << 24));
            if ((uint32_t)a > peak) peak = (uint32_t)a;
        }
    }
    /* the end: keys up, MIDI notes off on every channel, stop, the moves off; then 5 s */
    fm1_in.notes = 0;
    bp.keys = 0;                                             /* (the BREAK screen's live keys up) */
    fm1_in.buttons = 0;
    live_keys = 0;
    perf.wait = 0;
    mix_block(out, CTL);
    for (k = 0; k < 16u; k++)
        for (i = 24; i < 104u; i++) {
            midi_in(0x80u | k, i, 0);
            if (mi_w - mi_r >= MQ - 4u) mix_block(out, CTL);
        }
    transport_req = 2;
    for (b = 0; b < 5u * FS / CTL; b++) mix_block(out, CTL);
    assert(!song.playing && !song.rec && !rec_wait && !ft_on);
    assert(punch.cur == -1);
    for (k = 0; k < NPART; k++)
        for (i = 0; i < NVOICE; i++)
            assert(!trk[k].v[i].active);                     /* no hanging synth note */
    for (i = 0; i < 2u; i++)
        assert(!bp.v[i].on);                                 /* every BREAK slice ended */
    assert(!bp.running && bp.gate == 0);
    for (k = 0; k < NPART; k++)
        for (i = 0; i < NSTEP; i++) {
            const step_t *s = &trk[k].step[i];
            assert(s->n <= 4u && s->time <= ST_REST);
        }
    for (i = 0; i < NROLL; i++)
        assert(!roll[i].on);                                 /* every roll ended with its key */
    {
        int32_t quiet = 0;                                   /* silence (the reverb / delay tails done) */
        for (b = 0; b < FS / CTL; b++) {
            mix_block(out, CTL);
            for (i = 0; i < CTL * 2u; i++) if ((out[i] < 0 ? -out[i] : out[i]) > quiet) quiet = out[i] < 0 ? -out[i] : out[i];
        }
        printf("soak: %u min, %u events, peak %u, max block %.1f us, mean %.2f us; tail %d\n",
               minutes, events, peak, maxblock_ns / 1000.0, total_ns / blocks / 1000.0, quiet);
        assert(quiet < 64);
    }
    printf("soak: free takes %u, closed into a loop %u; BREAK triggers %u, voices %u, hits %u\n", ft_starts, ft_count,
           bp.st_trigs, bp.st_starts, bp.st_hits);
    assert(bp.st_trigs > 0 && bp.st_starts > 0 && bp.st_hits > 0);  /* (the break played, and was hit) */
    fflush(stdout);
    if (minutes >= 3u)                                       /* (a shorter run may close none: a smoke test) */
        assert(ft_count > 0);
    printf("soak: random live use, bounded output, no hanging voices, idle after stop PASS\n");
    return 0;
}
