/* SPDX-License-Identifier: GPL-3.0-only */
/* The moves (FMX; SLOOP's punch-in FX before it, then STUDIO's PERFORM): what LIVE's held keys 13..16 (ui_live.c) and
 * AUTO SONG's sections (arranger_scene.c as_keys) play, each for as long as it is asked for. The FX panel (lfx.c) is the
 * effects both LIVE and STUDIO play by hand (FX held); these are the moves under it:
 *    ROLL 16   the BREAK's hits rolled (two a trigger)      REWIND  the mix spun back, then silence; let go: back on the bar
 *    FILL      the steps' fill (as GLO + key 9)             VERB    the reverb's input up (its tail rings on)
 *    WASH      the mix thinning out (a high-pass rising over a bar) into a dotted-eighth echo that grows with it, the
 *              reverb open; let go: dry at once (the drop)
 *    TSTOP     the tape stop: the mix slowing down to a stop over a beat (AUTO SONG's way into a DROP 2); let go: at
 *              full speed at once
 * Everything here runs in the audio ISR: perf_block once a block before the events, punch_process on the master (fx.c
 * mix_block). The mix effects (REWIND, WASH, TSTOP: one at a time, the last pressed) read a mono ring of the mix (PUNCH_N
 * samples, 0.74 s); every change crossfades over 64 samples. */
#define PUNCH_N 32768u                    /* power of two */
#define PERF_N 6u
enum { PA_ROLL16, PA_REWIND, PA_FILL, PA_VERB, PA_WASH, PA_TSTOP };
#define PERF_WAITS (1u << PA_REWIND)      /* (let go: back on the bar) */
#define PERF_SWEEP 1u                     /* WASH's rise: a bar (sweep_samples) */
#define PERF_DEPTH 70                     /* ... up to where its high-pass goes */
/* the mix effects, one at a time */
#define PUNCH_NFX 3u
enum { PX_REWIND, PX_WASH, PX_TSTOP };
static const int8_t PERF_PX[PERF_N] = {-1, PX_REWIND, -1, -1, PX_WASH, PX_TSTOP};
static int16_t punch_ring[PUNCH_N] __attribute__((section(".pool")));
static struct {
    volatile int8_t req;          /* the mix effect asked for (-1 none): perf_block */
    int8_t cur;                   /* effect playing (fading out when req differs) */
    int32_t g;                    /* wet gain Q15 */
    uint32_t w;                   /* ring write index */
    uint32_t len;                 /* REWIND: its length */
    uint32_t dspd;                /* rewind, tape stop: speed step per sample */
    uint32_t rp;                  /* rewind, tape stop: read position Q16 */
    uint32_t spd;                 /* its speed Q16 (tape stop: Q24) */
    int32_t amp, damp;            /* rewind, tape stop: its level, falling to silence */
    int32_t f1, f2, f3, f4;       /* WASH: its high-pass, left and right */
} punch = {.req = -1, .cur = -1};
static struct {
    uint16_t prev;                /* (last block's keys) */
    uint16_t on;                  /* the moves in force: asked for, or let go and waiting for the bar */
    uint16_t wait;                /* let go, coming back on the bar */
    uint16_t order;               /* (the last mix effect pressed: its action) */
    uint32_t beat;                /* the beat seen last (clk_beat) */
    uint32_t rise;                /* WASH: samples since its press */
} perf;

static uint32_t beat_samples(void) { return (uint32_t)FS * 60u / (uint32_t)song.g[G_BPM]; }
static uint32_t sweep_samples(void) { return beat_samples() * (2u << PERF_SWEEP); }

static int32_t ring_raw(uint32_t i) { return punch_ring[i & (PUNCH_N - 1u)]; }   /* the stored 16 bits (mix / 8) */
static int32_t ring_q16(uint32_t p)                 /* read at Q16 position, linear (on the stored values: no overflow) */
{
    int32_t a = ring_raw(p >> 16), b = ring_raw((p >> 16) + 1u);
    return (a + (((b - a) * (int32_t)((p >> 1) & 0x7FFFu)) >> 15)) << 3;
}

/* FMX LIVE (ui_live.c): its moves held (keys 13..16: the moves they are, as bits of PA_*), GAP (the BREAK out while
 * held), and ENERGY turned (-1: not; the sections then keep it, arranger_scene.c) */
static volatile uint16_t live_keys;
static volatile uint8_t live_gap;
static int16_t live_mangle = -1;
static volatile uint8_t live_rw_cut;      /* REWIND resolved into a section (ui_live.c): still held, it counts as let go
                                           * (a new press of it: its own again, seq.c) */
static uint32_t live_moves(void) { return live_keys & ~(live_rw_cut ? 1u << PA_REWIND : 0u); }
/* once a block, before the events: the keys' edges, what waits for the bar, the moves into the engines */
static void perf_block(uint32_t n)
{
    uint32_t keys = as_keys | live_moves(), down = keys & ~perf.prev, up = perf.prev & ~keys, beat = clk_beat, i;
    perf.prev = (uint16_t)keys;
    for (i = 0; i < PERF_N; i++)
        if (((down >> i) & 1u) && PERF_PX[i] >= 0)
            perf.order = (uint16_t)i;                   /* (the last mix effect pressed wins) */
    if (down & (1u << PA_WASH))
        perf.rise = 0;
    perf.wait = (uint16_t)((perf.wait & ~down) | (up & PERF_WAITS));
    if (perf.wait && (!song.playing || beat / 4u != perf.beat / 4u))
        perf.wait = 0;                                  /* (the bar: back) */
    perf.beat = beat;
    perf.on = (uint16_t)(keys | perf.wait);
    perf_mute = (uint8_t)(as_mute | (live_gap ? 1u << TRK_BRK : 0u));   /* (the song's section: AUTO SONG; LIVE's GAP) */
    perf_verb = (uint8_t)(perf.on & ((1u << PA_VERB) | (1u << PA_WASH)) ? 2u : 0u);
    bp.roll = (uint8_t)(perf.on & (1u << PA_ROLL16) ? 2u : 0u);
    {   /* the mix effect: the last pressed of those in force */
        int32_t px = (perf.on >> perf.order) & 1u ? PERF_PX[perf.order] : -1;
        for (i = 0; px < 0 && i < PERF_N; i++)
            if (((perf.on >> i) & 1u) && PERF_PX[i] >= 0)
                px = PERF_PX[i];
        punch.req = (int8_t)px;
    }
    if (perf.on & (1u << PA_WASH))
        perf.rise += n;
}

static void punch_start(int32_t fx)
{
    uint32_t len = beat_samples();
    punch.cur = (int8_t)fx;
    if (len > PUNCH_N - 1024u)
        len = PUNCH_N - 1024u;
    punch.len = len;
    punch.rp = punch.w << 16;
    punch.spd = 65536u;
    punch.dspd = 2u * 65536u / (len / 2u) + 1u;
    punch.amp = 32767;
    punch.damp = 32767 / (int32_t)(len * 3u / 4u) + 1;
    punch.f1 = punch.f2 = punch.f3 = punch.f4 = 0;
    if (fx == PX_TSTOP) {                               /* from now, 1x down to 0 over the beat; the last quarter fades */
        punch.rp = (punch.w - 1u) << 16;
        punch.spd = 1u << 24;
        punch.dspd = (1u << 24) / len;
        punch.damp = 32767 / (int32_t)(len / 4u) + 1;
    }
}

/* l, r: the mix before the master (fx.c mix_block), n samples, in place */
static void punch_process(int32_t *l, int32_t *r, uint32_t n)
{
    uint32_t i, echo_d;
    int32_t want = punch.req, fw;
    tsvf_t cw;
    if (punch.cur < 0 && want < 0) {                   /* idle: only the ring */
        for (i = 0; i < n; i++)
            punch_ring[punch.w++ & (PUNCH_N - 1u)] = (int16_t)clamp((l[i] + r[i]) >> 4, -32768, 32767);
        return;
    }
    if (punch.cur < 0 && want >= 0)
        punch_start(want);
    echo_d = beat_samples() * 3u / 4u;                  /* per block: no divide per sample */
    if (echo_d > PUNCH_N - 64u) echo_d = PUNCH_N - 64u;
    {   /* WASH: how far into its rise (Q12): the high-pass up to its top, the echo's feedback up */
        uint32_t sw = sweep_samples(), p = perf.rise > sw ? sw : perf.rise, q = p / (sw / 4096u + 1u);
        int32_t top = 60 + PERF_DEPTH * 35 / 100;       /* (cut 8 .. 84: up to ~1 kHz) */
        q = q > 4096u ? 4096u : q;
        tsvf_coef(&cw, (8 << 8) + (int32_t)(((uint32_t)(top - 8) << 8) * q >> 12), 40);
        fw = 6000 + (int32_t)(q * 16000u >> 12);        /* (x 8 / 4096 after the ring: ~0.18 .. ~0.67) */
    }
    for (i = 0; i < n; i++) {
        int32_t x = l[i], y = r[i], wl = x, wr = y, m = (x + y) >> 1;
        int32_t fx = punch.cur;
        uint32_t target = want == fx ? 32767u : 0u;
        punch_ring[punch.w & (PUNCH_N - 1u)] = (int16_t)clamp(m >> 3, -32768, 32767);
        if (fx == PX_REWIND) {                          /* backwards, faster and faster (up to 3x), fading out */
            int32_t v = ring_q16(punch.rp);
            punch.rp -= punch.spd;
            punch.spd = punch.spd + punch.dspd > 3u * 65536u ? 3u * 65536u : punch.spd + punch.dspd;
            punch.amp = punch.amp > punch.damp ? punch.amp - punch.damp : 0;
            wl = wr = ((v >> 3) * (punch.amp >> 3)) >> 9;
        } else if (fx == PX_TSTOP) {                    /* forward, slower and slower: what was played, behind it */
            int32_t v = ring_q16(punch.rp);
            punch.rp += punch.spd >> 8;
            punch.spd = punch.spd > punch.dspd ? punch.spd - punch.dspd : 0u;
            if (punch.spd < (1u << 22))
                punch.amp = punch.amp > punch.damp ? punch.amp - punch.damp : 0;
            wl = wr = ((v >> 3) * (punch.amp >> 3)) >> 9;
        } else if (fx == PX_WASH) {                     /* the mix through the high-pass, into the echo (it feeds back) */
            int32_t hl = x - (tsvf_lp(&cw, x >> 1, &punch.f1, &punch.f2) << 1);
            int32_t hr = y - (tsvf_lp(&cw, y >> 1, &punch.f3, &punch.f4) << 1);
            int32_t e = (ring_raw(punch.w - echo_d) * fw) >> 12;
            punch_ring[punch.w & (PUNCH_N - 1u)] = (int16_t)clamp((((hl + hr) >> 1) + e) >> 3, -32768, 32767);
            wl = hl + e;
            wr = hr + e;
        }
        if ((uint32_t)punch.g < target)
            punch.g = punch.g + 512 > 32767 ? 32767 : punch.g + 512;
        else if ((uint32_t)punch.g > target)
            punch.g = punch.g < 512 ? 0 : punch.g - 512;
        l[i] = x + ((((wl - x) >> 3) * (punch.g >> 3)) >> 9);   /* (no 32-bit overflow) */
        r[i] = y + ((((wr - y) >> 3) * (punch.g >> 3)) >> 9);
        punch.w++;
        if (punch.g == 0 && want != fx) {                /* faded out: the next one, or none */
            if (want >= 0)
                punch_start(want);
            else
                punch.cur = -1;
        }
    }
}

/* the keyboard (the layers the UI runs: SEQ steps, GLO mix): white key index 0..15, -1 = black */
static int32_t punch_key(uint32_t k)
{
    static const int8_t W[12] = {0, -1, 1, -1, 2, -1, 3, 4, -1, 5, -1, 6};   /* from F */
    int32_t i = W[k % 12u];
    return i < 0 ? -1 : (int32_t)(k / 12u) * 7 + i;
}
