/* SPDX-License-Identifier: GPL-3.0-only */
/* The full-screen visualiser (after SLOOP 2.4's): on HOME (the DECK), HOME tapped opens it; HOME again or any page
 * closes it; a layer held shows its screen as ever, then the visualiser comes back. The keys, PLAY and REC work as
 * always. FMX has one: the DRUMMER, a drawn drummer playing the BREAK track (black lines on white; the sprites:
 * assets/drummer, tools/gen_drummer.py; POLY and SLOOP's others are gone). Updated every frame in the main loop, only
 * what changed drawn (a pose, the kick, a message: its rectangle, in bands the canvas holds), the poses the style
 * plays next decoded ahead (DRUM_NCACHE kept) so a hit is drawn the frame it comes.
 *   the logo     always
 *   the kick     its own sprite over everything: hit for a moment on every kick, the kit thumping down with it
 *   the drummer  always moving: a pose a hit (brkpart.c counts them: kicks, snares, the others: the ride, the hats,
 *                ghosts), and between hits a 16th at a time. A style a bar, among those that suit the BREAK's STYLE:
 *                  BOTH / HEAD    a snare: the hit pose, then the rest pose, a moment each
 *                  ALTER          the snares from one drum to the other
 *                  L / R SNARE    that snare, a hand each hit
 *                the other hits (and the 16ths between) the cymbals, a hand each: L / R SNARE that side's, the others
 *                crossing over. NO DRUMS (three poses, 1 2 3 2, a beat each) when the BREAK is still (stopped, out,
 *                two beats without a hit) or at HALFTIME (its STYLE).
 * SELECT on it: the one style there is (VIS_N, vis_style kept for the next). */
#include "felucca_drummer.h"
#define VIS_N 1u
static const char *const VIS_NAME[VIS_N] = {"DRUMMER"};
/* vis_on, vis_shown_last, vis_name_t: ui_draw.c; vis_style: panel.c (kept with the settings) */
enum { DF_LOGO, DF_KICK_OFF, DF_KICK_HIT, DF_BOTH_OFF, DF_BOTH_HIT, DF_HEAD_OFF, DF_HEAD_HIT, DF_ALTER_L, DF_ALTER_R,
       DF_LSN_LH, DF_LSN_RH, DF_RSN_LH, DF_RSN_RH, DF_LCY_LH, DF_LCY_RH, DF_RCY_LH, DF_RCY_RH, DF_ND1, DF_ND2, DF_ND3 };
_Static_assert(DRUM_NF == DF_ND3 + 1, "tools/gen_drummer.py FRAMES: the order here");
enum { DS_BOTH, DS_HEAD, DS_ALTER, DS_LSNARE, DS_RSNARE, DS_N };
/* the styles each BREAK STYLE may pick (bit per DS_*): STRAIGHT, ROLLER, JUNGLE, HALFTIME, BRKCORE, CHAOS */
static const uint8_t DS_FOR[BS_NSTYLE] = {0x19, 0x1C, 0x1F, 0x1F, 0x06, 0x06};
#define DRUM_NCACHE 4u                              /* poses kept decoded: a style's two and its cymbals' two */
static uint8_t drum_buf[DRUM_FIXED + DRUM_NCACHE * DRUM_POSE_MAX] __attribute__((section(".pool")));
static struct {
    uint8_t fixed;                                  /* the logo and the kick decoded */
    uint8_t pose;                                   /* the drummer's frame */
    uint8_t slot[DRUM_NCACHE], age[DRUM_NCACHE];    /* the poses decoded (0 = none), their last use */
    uint8_t clock;
    uint8_t dpose, dkick, dbob, dmsg;               /* what is on the screen (dpose 0: nothing yet) */
    uint8_t style, hand, side, hold, kick, bob;
    uint8_t k, s, o;                                /* the BREAK's counts seen */
    uint32_t phrase, q16, hit_q8, free_q8, last_ms;  /* the bar the style is for, the 16th seen, the last hit's beat */
} dv = {.pose = DF_ND1, .phrase = 0xFFFFFFFFu};

static int vis_shown(void) { return vis_on && !ui.home && !ui.menu && cur_page()->scope == SC_TRK; }
static void vis_open(void)
{
    vis_on = 1;
    vis_name_t = 30;
    ui.force = 1;
}
static uint32_t drum_bytes(uint32_t f) { return ((DRUM_F[f].w + 7u) >> 3) * DRUM_F[f].h; }
static uint32_t drum_bit(const uint8_t *b, uint32_t st, int32_t x, int32_t y, uint32_t w)
{
    return (uint32_t)x < w && y >= 0 ? (b[(uint32_t)y * st + ((uint32_t)x >> 3)] >> (7u - ((uint32_t)x & 7u))) & 1u : 0u;
}
/* frame f into out (1 bpp, rows of whole bytes): the range decoder under the static context model (gen_drummer.py) */
static void drum_decode(uint32_t f, uint8_t *out)
{
    const drum_frame_t *r = &DRUM_F[f];
    const uint8_t *d = DRUM_DATA + r->off, *e = d + r->len;
    uint32_t w = r->w, h = r->h, st = (w + 7u) >> 3, rng = 0xFFFFFFFFu, code, c, bound;
    int32_t x, y;
    memset(out, 0, st * h);
    code = (uint32_t)d[1] << 24 | (uint32_t)d[2] << 16 | (uint32_t)d[3] << 8 | d[4];
    d += 5;
    for (y = 0; y < (int32_t)h; y++)
        for (x = 0; x < (int32_t)w; x++) {
            c = drum_bit(out, st, x - 1, y, w) << 9 | drum_bit(out, st, x - 2, y, w) << 8 | drum_bit(out, st, x - 3, y, w) << 7 |
                drum_bit(out, st, x - 4, y, w) << 6 | drum_bit(out, st, x - 2, y - 1, w) << 5 | drum_bit(out, st, x - 1, y - 1, w) << 4 |
                drum_bit(out, st, x, y - 1, w) << 3 | drum_bit(out, st, x + 1, y - 1, w) << 2 | drum_bit(out, st, x + 2, y - 1, w) << 1 |
                drum_bit(out, st, x - 1, y - 2, w);
            bound = (rng >> 12) * ((uint32_t)DRUM_P0[c] << 4 | 8u);
            if (code < bound) {
                rng = bound;
            } else {
                code -= bound;
                rng -= bound;
                out[(uint32_t)y * st + ((uint32_t)x >> 3)] |= (uint8_t)(0x80u >> ((uint32_t)x & 7u));
            }
            while (rng < 1u << 24) {
                rng <<= 8;
                code = code << 8 | (d < e ? *d++ : 0u);
            }
        }
}
/* pose f's slot in the cache, -1 none */
static int drum_cached(uint32_t f)
{
    uint32_t i;
    for (i = 0; i < DRUM_NCACHE; i++)
        if (dv.slot[i] == f)
            return (int)i;
    return -1;
}
/* frame f's pixels: the logo and the kick's two decoded once; a pose from the cache, else decoded now into the slot
 * used longest ago */
static const uint8_t *drum_frame(uint32_t f)
{
    uint32_t i, at = 0;
    int c;
    if (!dv.fixed) {
        for (i = 0; i < 3u; i++) {
            drum_decode(i, drum_buf + at);
            at += drum_bytes(i);
        }
        dv.fixed = 1;
    }
    if (f < 3u) {
        for (i = 0; i < f; i++)
            at += drum_bytes(i);
        return drum_buf + at;
    }
    c = drum_cached(f);
    if (c < 0) {
        for (i = 0, c = 0; i < DRUM_NCACHE; i++)
            if ((uint8_t)(dv.clock - dv.age[i]) > (uint8_t)(dv.clock - dv.age[c]) || !dv.slot[i])
                c = (int)i;
        drum_decode(f, drum_buf + DRUM_FIXED + (uint32_t)c * DRUM_POSE_MAX);
        dv.slot[c] = (uint8_t)f;
    }
    dv.age[c] = dv.clock;
    return drum_buf + DRUM_FIXED + (uint32_t)c * DRUM_POSE_MAX;
}
/* the poses the style plays next, decoded ahead (one a frame, when nothing is drawn): its two and its cymbals' */
static void drum_prefetch(void)
{
    static const uint8_t SN[DS_N] = {DF_BOTH_OFF, DF_HEAD_OFF, DF_ALTER_L, DF_LSN_LH, DF_RSN_LH};
    uint8_t want[4];
    uint32_t i;
    want[0] = SN[dv.style % DS_N];
    want[1] = (uint8_t)(want[0] + 1u);
    want[2] = dv.style == DS_RSNARE ? DF_RCY_LH : DF_LCY_LH;
    want[3] = dv.style == DS_LSNARE ? DF_LCY_RH : DF_RCY_RH;
    for (i = 0; i < 4u; i++)
        if (drum_cached(want[i]) < 0) {
            dv.clock -= 16u;                            /* (decoded ahead: as if used a while ago) */
            drum_frame(want[i]);
            dv.clock += 16u;
            return;
        }
}
/* frame f's ink into the canvas, its origin at screen (x0, y0), dy rows down: black */
static void drum_put(uint32_t f, int32_t x0, int32_t y0, int32_t dy)
{
    const drum_frame_t *r = &DRUM_F[f];
    const uint8_t *b;
    uint32_t st = (r->w + 7u) >> 3, x, k;
    int32_t y, cx = (int32_t)r->x - x0;
    uint16_t ink = swap16(C_BLACK);
    if ((int32_t)r->y + dy >= y0 + (int32_t)cv_h || (int32_t)(r->y + r->h) + dy <= y0 ||
        cx >= (int32_t)cv_w || cx + (int32_t)r->w <= 0)
        return;                                         /* (not in the canvas) */
    b = drum_frame(f);
    for (y = 0; y < (int32_t)r->h; y++) {
        int32_t cy = (int32_t)r->y + y - y0 + dy;
        const uint8_t *row = b + (uint32_t)y * st;
        if (cy < 0 || cy >= (int32_t)cv_h)
            continue;
        for (x = 0; x < st; x++)
            if (row[x])
                for (k = 0; k < 8u; k++) {
                    int32_t px = cx + (int32_t)(x * 8u + k);
                    if (((row[x] >> (7u - k)) & 1u) && x * 8u + k < r->w && px >= 0 && px < (int32_t)cv_w)
                        cv_px[(uint32_t)cy * cv_w + (uint32_t)px] = ink;
                }
    }
}
/* the rectangle frame f covers dy rows down, into (x0, y0)..(x1, y1) */
static void drum_box(uint32_t f, int32_t dy, int32_t *b)
{
    const drum_frame_t *r = &DRUM_F[f];
    int32_t y = (int32_t)r->y + dy;
    b[0] = b[0] < (int32_t)r->x ? b[0] : (int32_t)r->x;
    b[1] = b[1] < y ? b[1] : y;
    b[2] = b[2] > (int32_t)(r->x + r->w) ? b[2] : (int32_t)(r->x + r->w);
    b[3] = b[3] > y + (int32_t)r->h ? b[3] : y + (int32_t)r->h;
}

/* beats since PLAY in 1/256 (stopped: a free count at the tempo, for what moves anyway) */
static uint32_t vis_beat_q8(void)
{
    if (song.playing)
        return clk_beat * 256u + clk_pos / (BEAT_U / 256u);
    return dv.free_q8;
}
/* the frame's pose. Always moving: a snare its style's pose; any other hit, or a 16th gone by without one (the
 * groove), the hands on the cymbals (L / R SNARE: that side; else crossing over); BOTH / HEAD back to their rest pose
 * a moment after a snare; a kick the kick's hit and the whole kit thumping down a few rows. The style a bar. */
static void vis_update(void)
{
    uint32_t now = fm1_ms, dt = now - dv.last_ms, k, s, o, half, still, bq, q16, groove;
    static const uint8_t SN[DS_N] = {DF_BOTH_OFF, DF_HEAD_OFF, DF_ALTER_L, DF_LSN_LH, DF_RSN_LH};
    static const uint8_t ND[4] = {DF_ND1, DF_ND2, DF_ND3, DF_ND2};
    dv.last_ms = now;
    dv.free_q8 += (dt > 200u ? 200u : dt) * (uint32_t)song.g[G_BPM] * 256u / 60000u;
    k = (uint8_t)(vis_bk - dv.k);
    s = (uint8_t)(vis_bs - dv.s);
    o = (uint8_t)(vis_bo - dv.o);
    dv.k = vis_bk;
    dv.s = vis_bs;
    dv.o = vis_bo;
    bq = vis_beat_q8();
    if (k) {
        dv.kick = 4;                                /* (frames: ~65 ms) */
        dv.bob = 4;
    } else {
        dv.kick = (uint8_t)(dv.kick ? dv.kick - 1u : 0u);
        dv.bob = (uint8_t)(dv.bob ? dv.bob - 1u : 0u);
    }
    if (k | s | o)
        dv.hit_q8 = bq;
    half = (uint32_t)BPV(BP_STYLE) == BS_HALFTIME;
    still = !song.playing || trk_silent(TBRK) || bq - dv.hit_q8 > 512u;   /* (two beats without a hit) */
    if (still || half) {
        dv.pose = ND[(bq >> 8) & 3u];
        dv.hold = 0;
        return;
    }
    if ((clk_beat >> 2) != dv.phrase) {             /* a new bar: a style the BREAK's STYLE suits */
        uint32_t m = DS_FOR[(uint32_t)BPV(BP_STYLE) % BS_NSTYLE], h = ((clk_beat >> 2) + 1u) * 2654435761u >> 24, n = 0, i;
        dv.phrase = clk_beat >> 2;
        for (i = 0; i < DS_N; i++)
            n += (m >> i) & 1u;
        for (i = 0, h %= n ? n : 1u; i < DS_N; i++)
            if ((m >> i) & 1u && !h--)
                break;
        dv.style = (uint8_t)(i < DS_N ? i : DS_ALTER);
    }
    q16 = bq >> 6;
    groove = q16 != dv.q16 && !(k | s | o);         /* (a 16th without a hit: the hands go on) */
    dv.q16 = q16;
    if (s) {                                        /* a snare */
        if (dv.style <= DS_HEAD) {
            dv.pose = (uint8_t)(SN[dv.style] + 1u);
            dv.hold = 4;
            return;
        }
        dv.hand ^= 1u;
        dv.pose = (uint8_t)(SN[dv.style] + dv.hand);
        return;
    }
    if (dv.hold) {                                  /* BOTH / HEAD: the hit's moment, then the rest pose a moment */
        dv.hold--;
        if (dv.hold == 2u)
            dv.pose = SN[dv.style];
        return;
    }
    if (o || k || groove) {                         /* the cymbals: a hand a hit */
        dv.hand ^= 1u;
        if (dv.style == DS_LSNARE)
            dv.side = 0;
        else if (dv.style == DS_RSNARE)
            dv.side = 1;
        else if (!dv.hand)
            dv.side ^= 1u;                          /* (crossing over: a side every two) */
        dv.pose = (uint8_t)((dv.side ? DF_RCY_LH : DF_LCY_LH) + dv.hand);
    }
}

/* the visualiser, every frame: the update, then only what changed (a pose, the kick, the thump, a message) drawn,
 * in bands the canvas holds; nothing changed: the next pose decoded ahead */
static void vis_draw(void)
{
    int32_t b[4] = {240, 240, 0, 0}, y;
    uint32_t msg = ui.msg_t ? (uint8_t)((str_len(ui.msg) * 31u + (uint8_t)ui.msg[0]) | 1u) : 0u;   /* (0: none) */
    dv.clock++;
    vis_update();
    if (!vis_shown_last || ui.force || !dv.dpose) {
        vis_shown_last = 1;
        b[0] = b[1] = 0;
        b[2] = b[3] = 240;
    } else {
        if (dv.pose != dv.dpose || dv.bob != dv.dbob) {
            drum_box(dv.dpose, dv.dbob, b);
            drum_box(dv.pose, dv.bob, b);
        }
        if (dv.bob != dv.dbob || (dv.kick != 0) != (dv.dkick != 0)) {
            drum_box(DF_KICK_HIT, dv.dbob, b);
            drum_box(DF_KICK_OFF, dv.dbob, b);
            drum_box(DF_KICK_HIT, dv.bob, b);
            drum_box(DF_KICK_OFF, dv.bob, b);
        }
        if (msg != dv.dmsg) {
            b[0] = 0;
            b[1] = b[1] < 0 ? b[1] : 0;
            b[2] = 240;
            b[3] = b[3] > 20 ? b[3] : 20;
        }
    }
    b[0] = clamp(b[0], 0, 240);
    b[1] = clamp(b[1], 0, 240);
    b[2] = clamp(b[2], 0, 240);
    b[3] = clamp(b[3], 0, 240);
    if (b[2] <= b[0] || b[3] <= b[1]) {                 /* nothing to draw: the next poses ahead */
        drum_prefetch();
        if (vis_name_t)
            vis_name_t--;
        return;
    }
    for (y = b[1]; y < b[3]; y += (int32_t)cv_h) {      /* the bands */
        uint32_t w = (uint32_t)(b[2] - b[0]), h = (uint32_t)(b[3] - y);
        cv_begin(w, h, C_WHITE);                         /* (h: what the canvas holds of it) */
        drum_put(DF_LOGO, b[0], y, 0);
        drum_put(dv.pose, b[0], y, dv.bob);             /* (a kick: the kit thumps down, back over 4 frames) */
        drum_put(dv.kick ? DF_KICK_HIT : DF_KICK_OFF, b[0], y, dv.bob);
        if (msg && y < 20) {                            /* a message */
            cv_oy = -y;
            cv_rect(-b[0], 0, 240, 20, C_BLACK);
            cv_text(120 - text_w(&FONT_S, ui.msg) / 2 - b[0], 2, &FONT_S, ui.msg, C_WHITE);
            cv_oy = 0;
        }
        cv_blit((uint32_t)b[0], (uint32_t)y);
    }
    dv.dpose = dv.pose;
    dv.dkick = dv.kick;
    dv.dbob = dv.bob;
    dv.dmsg = (uint8_t)msg;
    if (vis_name_t)
        vis_name_t--;
}
/* SELECT on the visualiser: the next / previous style (one, for now) */
static void vis_select(int32_t s)
{
    vis_style = (uint8_t)((vis_style + (s > 0 ? 1u : VIS_N - 1u)) % VIS_N);
    vis_name_t = 30;
    ui.force = 1;
    settings_later = 1;                                 /* (kept with the settings, once stopped) */
}
