/* SPDX-License-Identifier: GPL-3.0-only
 * After SLOOP 2.7.1's tests/mix_split_test.c (Andre Rezemini, the SLOOP dualcore branch, github.com/andrerezemini/sloop-lab) */
/* FMX: the render split over the two cores (fx.c mix_units): the same output, bit for bit, whatever the split.
 * Each scene renders about two seconds with cpu1_units = 0 (every track on this core) and again with every other
 * split: each track alone, pairs, all four, the firmware's own choice (-1); each render in a fresh fork()ed child of a
 * process that never touched the DSP state, as tests/regress.c does. The FNV-1a hash of the output must be the one of
 * 0 every time. The scenes:
 *   jungle    the BREAK in JUNGLE with a LAYER and MANGLE, the BASS phrase on REESE SUB with DIST (striking on the
 *             BREAK's kicks), the CHORDS generator holding PSX PAD, the CHOP track playing RAVE HITS, DUCK, the sends
 *   lfo_sh    S&H LFOs on the three parts, fast and at different rates (rng() draws inside the render: a part whose
 *             LFO wraps stays on core 0), chords on every part, the BREAK
 *   mutes     mutes and a solo coming and going, the CHORDS track through its SLICER and FILTER, an engine switched
 *             under sounding notes (the fade), the metronome
 * Runs under every mode of the host double of core 1 (tests/cpu1_host.c: CORES, CHAOS, CPU1_TIMEOUT_AT) and under
 * ThreadSanitizer (tests/cpu1_modes.sh). Prints the jobs handed to core 1, to show the split happened.
 *   build/host/mix_split_test [QUICK]       QUICK: the splits 0, 15 and -1 only (the TSan run) */
#define main hostsim_main
#include "hostsim.c"
#undef main

#define BLOCKS 1500u                                     /* ~1.1 s at 44.1 kHz, blocks of 32 */
static const int32_t MASKS[] = {0, -1, 1, 2, 4, 8, 3, 5, 12, 7, 15};
static const int32_t MASKS_QUICK[] = {0, 15, -1};
static const char *const SCENE_NAME[] = {"jungle", "lfo_sh", "mutes"};
#define NSCENE 3u

static void key_of(uint32_t root, uint32_t scale)
{
    uint32_t i;
    for (i = 0; i < NPART; i++) {
        trk[i].p[P_ROOT] = (int16_t)root;
        trk[i].p[P_SCALE] = (int16_t)scale;
    }
}
static void chords(track_t *t, uint32_t first, uint32_t every, uint32_t base)   /* 3-note chords on a 16-step pattern */
{
    uint32_t i;
    for (i = 0; i < 16u; i++) {
        uint8_t n[3] = {(uint8_t)(base + i % 5u), (uint8_t)(base + i % 5u + 3u), (uint8_t)(base + i % 5u + 7u)};
        int on = i >= first && (i - first) % every == 0u;
        put_step(t, i, on ? 3u : 0u, n, on ? ST_NOTE : ST_REST, i == first ? SF_ACCENT : 0u);
    }
    t->p[P_SLEN] = 16;
    t->seq_active = 1;
}
static void hits(track_t *t)                              /* CHOP: a hit on every other step, up the kit */
{
    uint32_t i;
    for (i = 0; i < 16u; i++) {
        uint8_t n = (uint8_t)(53u + i % 9u);
        put_step(t, i, i % 2u ? 0u : 1u, &n, i % 2u ? ST_REST : ST_NOTE, 0);
    }
    t->p[P_SLEN] = 16;
    t->seq_active = 1;
}

static void scene_setup(uint32_t s)
{
    uint32_t k;
    host_tracks_init();
    key_of(9, 2);                                        /* A minor */
    song.g[G_BPM] = 170;
    song.g[G_DUCK] = GP[G_DUCK].def;
    brk_style(TBRK, BS_JUNGLE);
    TBRK->p[P_REV] = 20;
    brkp_poll();
    if (s == 0u) {
        TBRK->p[P_ED_FLT] = 60;                          /* MANGLE */
        TBRK->p[P_GLMODE] = 2;                           /* LAYER: loop C under every slice */
        TBRK->p[P_DETUNE] = 60;                          /* (its mix) */
        brkp_poll();
        host_preset(&trk[0], ENGI_ANALOG, 22);           /* REESE SUB, through DIST */
        trk[0].p[P_DIST] = 40;
        bl.change = 30;
        bl_write();                                      /* the BASS phrase, on the BREAK's kicks */
        host_preset(&trk[1], ENGI_SAMPLE, 0);            /* PSX PAD, the CHORDS generator holding it */
        trk[1].p[P_REV] = 60;
        cg.play = CP_HOLD;
        cg.num = 4;
        cg.var = 11;
        cg_poll();
        host_pack(0, "RAVEHITS");                       /* CHOP KIT plays USR1: the RAVE HITS pack */
        host_preset(&trk[2], ENGI_SAMPLE, 8);
        hits(&trk[2]);
        trk[2].p[P_DLY] = 50;
    } else if (s == 1u) {
        static const int16_t RATE[3] = {118, 118, 124};     /* (BASS and CHORDS wrap in the same blocks: two draws) */
        for (k = 0; k < NPART; k++) {
            host_preset(&trk[k], k ? ENGI_SAMPLE : ENGI_ANALOG, k == 1u ? 4u : k == 2u ? 6u : 22u);
            trk[k].p[P_LWAVE] = 4;                       /* S&H: a draw at every wrap */
            trk[k].p[P_LRATE] = RATE[k];
            trk[k].p[P_LD_PIT] = 20;
            trk[k].p[P_LD_FLT] = 40;
            chords(&trk[k], k, 2u + k, (uint32_t)(45 + 12 * (int)k));
        }
    } else {
        host_preset(&trk[0], ENGI_ANALOG, 23);           /* DARK REESE */
        chords(&trk[0], 0, 4, 33);
        host_preset(&trk[1], ENGI_SAMPLE, 0);
        chords(&trk[1], 0, 2, 57);
        trk[1].p[P_SLCR] = 1;                            /* its SLICER */
        trk[1].p[P_TFLT] = -40;                          /* its FILTER, a low-pass */
        host_pack(0, "RAVEHITS");
        host_preset(&trk[2], ENGI_SAMPLE, 8);
        hits(&trk[2]);
        song.g[G_CLOCK] = 1;                             /* the metronome */
    }
    transport_req = 1;
}

static void scene_block(uint32_t s, uint32_t b)          /* what the UI would do between blocks */
{
    if (s != 2u)
        return;
    if (b == 300u)
        trk[2].p[P_MUTE] = 1;
    if (b == 500u)
        song.solo = 1u << 1;                             /* CHORDS soloed */
    if (b == 700u) {
        song.solo = 0;
        trk[2].p[P_MUTE] = 0;
        trk[1].p[P_TFLT] = 50;                           /* a high-pass now */
    }
    if (b == 900u)
        host_preset_req(&trk[0], ENGI_SAMPLE, 0);         /* under the sounding notes: the old engine fades */
    if (b == 1200u)
        TBRK->p[P_MUTE] = 1;
}

typedef struct { uint64_t hash; uint32_t jobs, timeouts; } res_t;

static res_t render(uint32_t s, int32_t mask)
{
    res_t r = {0xcbf29ce484222325ull, 0, 0};
    uint32_t b, i, k;
    cpu1_host_mode();                                    /* init: the env (CORES, CHAOS, CPU1_TIMEOUT_AT) */
    scene_setup(s);
    cpu1_units = mask;                                   /* (after the setup: host_tracks_init leaves it alone) */
    for (b = 0; b < BLOCKS; b++) {
        int32_t o[2 * CTL];
        scene_block(s, b);
        mix_block(o, CTL);
        for (i = 0; i < 2u * CTL; i++) {
            uint32_t u = (uint32_t)o[i];
            for (k = 0; k < 4u; k++) {
                r.hash ^= (u >> (8u * k)) & 0xFFu;
                r.hash *= 0x100000001B3ull;
            }
        }
    }
    r.jobs = cpu1_host_jobs();
    r.timeouts = cpu1_host_timeouts();
    return r;
}

static int child(uint32_t s, int32_t mask, res_t *out)  /* the render in a fresh process; 0 = it ran */
{
    int p[2], st = 0;
    pid_t pid;
    size_t got = 0;
    ssize_t n;
    if (pipe(p))
        return -1;
    fflush(stdout);
    if (!(pid = fork())) {
        res_t r = render(s, mask);
        close(p[0]);
        if (write(p[1], &r, sizeof r) != (ssize_t)sizeof r)
            _exit(1);
        _exit(0);
    }
    close(p[1]);
    while (got < sizeof *out && (n = read(p[0], (char *)out + got, sizeof *out - got)) > 0)
        got += (size_t)n;
    close(p[0]);
    waitpid(pid, &st, 0);
    return got == sizeof *out && WIFEXITED(st) && WEXITSTATUS(st) == 0 ? 0 : -1;
}

int main(int argc, char **argv)
{
    int quick = argc > 1 && !strcmp(argv[1], "QUICK"), fails = 0;
    const int32_t *masks = quick ? MASKS_QUICK : MASKS;
    uint32_t nm = quick ? sizeof MASKS_QUICK / sizeof MASKS_QUICK[0] : sizeof MASKS / sizeof MASKS[0], s, m;
    const char *e = getenv("CPU1_TIMEOUT_AT");
    uint32_t lost = e ? (uint32_t)atoi(e) : 0;
    for (s = 0; s < NSCENE; s++) {
        res_t ref;
        if (child(s, 0, &ref)) {
            printf("split: %-7s no split: CRASH\n", SCENE_NAME[s]);
            fails++;
            continue;
        }
        for (m = 0; m < nm; m++) {
            res_t r;
            int32_t mask = masks[m];
            const char *why = 0;
            if (child(s, mask, &r))
                why = "crashed";
            else if (r.hash != ref.hash)
                why = "the output differs from the unsplit render";
            else if (mask && !r.jobs)
                why = "no job was handed to core 1";
            else if (!mask && r.jobs)
                why = "jobs handed to core 1 with no unit for it";
            else if (lost && mask && r.jobs > lost)
                why = "jobs handed over after the lost one (core 1 should be held)";
            printf("split: %-7s units %3d  %016llx  jobs %4u  timeouts %u  %s\n", SCENE_NAME[s], mask,
                   (unsigned long long)r.hash, r.jobs, r.timeouts, why ? why : "same");
            if (why)
                fails++;
        }
    }
    printf("render split: %u scenes x %u splits, CORES=%d CHAOS=%s CPU1_TIMEOUT_AT=%u: %s\n", NSCENE, nm, cpu1_host_mode(),
           getenv("CHAOS") ? getenv("CHAOS") : "0", lost, fails ? "FAIL" : "PASS");
    return fails ? 1 : 0;
}
