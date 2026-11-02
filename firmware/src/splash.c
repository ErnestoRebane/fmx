/* SPDX-License-Identifier: GPL-3.0-only */
/* FMX boot screen (psx3d.c): the companion app's hero (web/app_draw.js heroFrame) - a low-poly gem in the four track
 * colours over a jungle canyon at dusk - its shards gathering as the camera flies up the river and slows down, the name
 * in the sky over it. Silent; no one's logo. The whole screen each frame, in two bands of 120 rows (the canvas').
 *   boot_splash()  the whole screen, the first frame (main.c, right after lcd_init)
 *   boot_anim()    the frames that follow, then the last one stays (main.c, in place of the logo's 900 ms)
 *   boot_frame(f)  frame f of BOOT_NF (the browser build draws them on its own clock) */
#define BOOT_NF 20u
#define BOOT_MS 35u                                     /* a frame (the browser build's clock too: sloop_web.c) */
#define BOOT_HOR 92                                     /* the horizon's row */
#define BOOT_FOC 124                                    /* the focal length, pixels (the hero's 132 at 256 wide) */
#define BOOT_EYE 538                                    /* the camera's height (2.1); world units Q8 from here on */
#define BOOT_GEMZ 1331                                  /* the gem's distance (5.2) */
#define BOOT_ROW0 3                                     /* where the flight starts on the river (a stretch that reads) */
static const int32_t BOOT_SKY[4][3] = {{5, 8, 25}, {27, 22, 80}, {90, 42, 106}, {255, 138, 58}};   /* (the last: BREAK) */
static const int32_t BOOT_TRK[4][3] = {{255, 138, 58}, {74, 140, 255}, {64, 220, 200}, {255, 120, 170}};
/* (TRK_COL of ui_studio.c in the hero's order: BREAK, BASS, CHORDS, CHOP) */
static const int32_t BOOT_JUNGLE[3] = {31, 107, 85}, BOOT_ROCK[3] = {58, 77, 122}, BOOT_RIVER[3] = {42, 47, 120};
static const int16_t BOOT_WALL[9] = {0, 97, 240, 406, 590, 788, 999, 1221, 1452};   /* (x - 1)^1.3 * 0.38 */
typedef struct { int32_t x, y, z, sx, sy; } boot_v_t;

static void boot_mix(const int32_t *a, const int32_t *b, int32_t t, int32_t *o)   /* t: 0..256 */
{
    o[0] = a[0] + (b[0] - a[0]) * t / 256;
    o[1] = a[1] + (b[1] - a[1]) * t / 256;
    o[2] = a[2] + (b[2] - a[2]) * t / 256;
}
static uint32_t boot_hash(int32_t i, int32_t j)        /* 0..65535 */
{
    uint32_t h = (uint32_t)i * 374761393u + (uint32_t)j * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return (h ^ (h >> 16)) >> 16;
}
static int32_t boot_div(int32_t a, int32_t b) { return a >= 0 ? (a + b / 2) / b : -((b / 2 - a) / b); }   /* rounded */
static void boot_project(boot_v_t *v)                 /* snapped to the pixel: the PSX wobble */
{
    v->sx = 120 + boot_div(BOOT_FOC * v->x, v->z);
    v->sy = BOOT_HOR - boot_div(BOOT_FOC * (v->y - BOOT_EYE), v->z);
}
/* the ground at column i (|i| <= 9), row j: the river bed, then the canyon's walls, rough */
static int32_t boot_ground(int32_t i, int32_t j)
{
    int32_t x = i < 0 ? -i : i, h = (int32_t)(boot_hash(i, j) >> 8);
    if (x <= 1)
        return -38 + h * 26 / 256;
    return BOOT_WALL[x - 1] + (x > 2 ? h : h * 102 / 256) - 51;
}
/* a face, lit from the upper left (as heroFrame), faded into the dusk with distance, moved (dx, dy) on screen */
static void boot_face(const boot_v_t *a, const boot_v_t *b, const boot_v_t *c, const int32_t *col, int32_t dx, int32_t dy)
{
    int32_t ux = b->x - a->x, uy = b->y - a->y, uz = b->z - a->z, wx = c->x - a->x, wy = c->y - a->y, wz = c->z - a->z;
    int32_t nx = (uy * wz - uz * wy) >> 4, ny = (uz * wx - ux * wz) >> 4, nz = (ux * wy - uy * wx) >> 4;
    int32_t len = (int32_t)p3_isqrt((uint32_t)(nx * nx) + (uint32_t)(ny * ny) + (uint32_t)(nz * nz)) + 1;
    int32_t l = (nx * -115 + ny * 192 + nz * -128) / len, k, fog, rgb[3];
    uint16_t pal[16];
    if (a->z < 102 || b->z < 102 || c->z < 102)
        return;                                         /* (behind the near plane) */
    k = 82 + (l < 0 ? 0 : l) * 200 / 256;
    rgb[0] = col[0] * k / 256;
    rgb[1] = col[1] * k / 256;
    rgb[2] = col[2] * k / 256;
    fog = clamp(((a->z + b->z + c->z) / 3 - 1536) * 256 / 3840, 0, 256);
    boot_mix(rgb, BOOT_SKY[2], fog, rgb);
    p3_pal_rgb(rgb[0], rgb[1], rgb[2], pal);
    p3_tri_pal(a->sx + dx, a->sy + dy, b->sx + dx, b->sy + dy, c->sx + dx, c->sy + dy, pal);
}

/* the sky: the dusk down to the horizon, the stars twinkling (frame f), the moon */
static void boot_sky(uint32_t f)
{
    int32_t y, x, c[3];
    uint32_t k;
    uint16_t pal[16];
    boot_v_t m[7];
    for (y = -cv_oy; y < 120 - cv_oy; y++) {          /* (y: the screen's row; the band's at y + cv_oy) */
        int32_t q = y * 256 / BOOT_HOR;
        if (q < 141)
            boot_mix(BOOT_SKY[0], BOOT_SKY[1], q * 256 / 141, c);
        else if (q < 230)
            boot_mix(BOOT_SKY[1], BOOT_SKY[2], (q - 141) * 256 / 89, c);
        else
            boot_mix(BOOT_SKY[2], BOOT_SKY[3], (q - 230 > 26 ? 26 : q - 230) * 256 / 26 * 7 / 10, c);
        p3_pal_rgb(c[0], c[1], c[2], pal);
        for (x = 0; x < 240; x++)
            cv_px[(y + cv_oy) * 240 + x] = pal[((uint32_t)y & 3u) * 4u + ((uint32_t)x & 3u)];
    }
    for (k = 0; k < 40u; k++) {
        static const int32_t DIM[3] = {90, 100, 170}, LIT[3] = {240, 240, 255};
        uint32_t sx = boot_hash((int32_t)k, 7) * 240u >> 16, sy = boot_hash((int32_t)k, 9) * (BOOT_HOR * 7u / 8u) >> 16;
        int32_t b = (int32_t)(boot_hash((int32_t)k, 3) >> 8);
        if ((int32_t)sy + cv_oy < 0 || (int32_t)sy + cv_oy > 119)
            continue;
        boot_mix(DIM, LIT, (p3_sin(f * 80u + (uint32_t)b * 25u) + 32768) * b >> 16, c);
        p3_pal_rgb(c[0], c[1], c[2], pal);
        cv_px[((int32_t)sy + cv_oy) * 240 + (int32_t)sx] = pal[(sy & 3u) * 4u + (sx & 3u)];
    }
    for (k = 0; k < 7u; k++) {                          /* the moon: a low-poly disc */
        m[k].sx = 193 + boot_div(p3_cos(k * 1024u / 7u) * 8, 32768);
        m[k].sy = BOOT_HOR - 28 + boot_div(p3_sin(k * 1024u / 7u) * 8, 32768);
    }
    p3_pal_rgb(233, 220, 196, pal);
    for (k = 1; k < 6u; k++)
        p3_tri_pal(m[0].sx, m[0].sy, m[k].sx, m[k].sy, m[k + 1u].sx, m[k + 1u].sy, pal);
}

/* the gem: a six-sided bipyramid, the faces above in the track colours, those below darker; turned ang (1024 a turn),
 * at height gy, its faces pushed out on screen by boom (0..256 of their distance from its centre) */
static void boot_gem(uint32_t ang, int32_t gy, int32_t boom)
{
    boot_v_t v[8], o = {0, gy, BOOT_GEMZ, 0, 0};
    int32_t sa = p3_sin(ang), ca = p3_cos(ang), sb = p3_sin(29u), cb = p3_cos(29u), depth[12];
    uint8_t ord[12];
    uint32_t i, k, n = 0;
    boot_project(&o);
    for (i = 0; i < 8u; i++) {
        int32_t x = 0, y = i ? -346 : 294, z = 0, t;
        if (i >= 2u) {
            x = p3_cos((i - 2u) * 1024u / 6u) * 200 >> 15;
            y = 46;
            z = p3_sin((i - 2u) * 1024u / 6u) * 200 >> 15;
        }
        t = (x * ca + z * sa) >> 15;                    /* turned about Y, then tilted towards us */
        z = (z * ca - x * sa) >> 15;
        x = t;
        t = (y * cb - z * sb) >> 15;
        z = (y * sb + z * cb) >> 15;
        v[i].x = x * 243 / 256;                         /* (size 0.95) */
        v[i].y = gy + t * 243 / 256;
        v[i].z = BOOT_GEMZ + z * 243 / 256;
        boot_project(&v[i]);
    }
    for (k = 0; k < 12u; k++) {                         /* the faces that look at the camera, far to near */
        const boot_v_t *p = &v[k & 1u], *a = &v[2u + (k >> 1)], *b = &v[2u + ((k >> 1) + 1u) % 6u];
        int32_t ux, uy, uz, wx, wy, wz, cx, cy, cz;
        if (!(k & 1u)) {                                /* (above: top, b, a; below: bottom, a, b - outward) */
            const boot_v_t *s = a;
            a = b;
            b = s;
        }
        ux = a->x - p->x, uy = a->y - p->y, uz = a->z - p->z, wx = b->x - p->x, wy = b->y - p->y, wz = b->z - p->z;
        cx = (p->x + a->x + b->x) / 3, cy = (p->y + a->y + b->y) / 3 - BOOT_EYE, cz = (p->z + a->z + b->z) / 3;
        if ((int64_t)(uy * wz - uz * wy) * cx + (int64_t)(uz * wx - ux * wz) * cy + (int64_t)(ux * wy - uy * wx) * cz >= 0)
            continue;
        depth[k] = cz;
        for (i = n++; i && depth[ord[i - 1u]] < cz; i--)
            ord[i] = ord[i - 1u];
        ord[i] = (uint8_t)k;
    }
    for (i = 0; i < n; i++) {
        static const int32_t SHADOW[3] = {10, 14, 40};
        const boot_v_t *p, *a, *b;
        int32_t col[3];
        k = ord[i];
        p = &v[k & 1u], a = &v[2u + (k >> 1)], b = &v[2u + ((k >> 1) + 1u) % 6u];
        if (!(k & 1u)) {
            const boot_v_t *s = a;
            a = b;
            b = s;
        }
        boot_mix(BOOT_TRK[((k >> 1) + (k & 1u) * 2u) & 3u], SHADOW, k & 1u ? 90 : 0, col);
        boot_face(p, a, b, col, ((p->sx + a->sx + b->sx) / 3 - o.sx) * boom / 256,
                  ((p->sy + a->sy + b->sy) / 3 - o.sy) * boom / 256);
    }
}

/* the canyon from far to near, row by row (a height field seen from its river: no sorting), the gem in its place;
 * scroll: how far up the river (Q8 rows) */
static void boot_land(int32_t scroll, uint32_t ang, int32_t gy, int32_t boom)
{
    boot_v_t row[2][19], *nr, *fr;
    int32_t base = scroll >> 8, frac = scroll & 255, j, i, k, gem = 0;
    for (j = 22; j >= 0; j--) {
        nr = row[j & 1];
        fr = row[(j + 1) & 1];
        for (i = 0; i < 19; i++) {
            nr[i].x = (i - 9) * 282;                    /* (1.1 apart) */
            nr[i].y = boot_ground(i - 9, j + base);
            nr[i].z = (j + 1) * 256 - frac;
            boot_project(&nr[i]);
        }
        if (j == 22)
            continue;
        if (!gem && nr[0].z + 128 < BOOT_GEMZ) {
            boot_gem(ang, gy, boom);
            gem = 1;
        }
        for (k = 0; k < 18; k++) {                      /* (the walls first, the river last) */
            int32_t q = k & 1 ? 17 - k / 2 : k / 2, a2 = 2 * (q - 9) + 1, dark[3];
            const int32_t *col = a2 > -3 && a2 < 3 ? BOOT_RIVER : a2 < -10 || a2 > 10 ? BOOT_ROCK : BOOT_JUNGLE;
            boot_face(&nr[q], &fr[q], &fr[q + 1], col, 0, 0);
            dark[0] = col[0] * 235 / 256;
            dark[1] = col[1] * 235 / 256;
            dark[2] = col[2] * 235 / 256;
            boot_face(&nr[q], &fr[q + 1], &nr[q + 1], dark, 0, 0);
        }
    }
}

/* a line centred at row y, over the scene: a drop shadow under it, as PS1 menus had */
static void boot_line(int32_t y, const char *s, uint16_t c)
{
    int32_t x = 120 - text_w(&FONT_S, s) / 2;
    cv_text(x + 1, y + 1, &FONT_S, s, RGB(8, 6, 24));
    cv_text(x, y, &FONT_S, s, c);
}

static void boot_frame(uint32_t f)
{
    uint32_t t = f >= BOOT_NF - 1u ? 256u : f * 256u / (BOOT_NF - 1u), e = 256u - (256u - t) * (256u - t) / 256u, band;
    for (band = 0; band < 2u; band++) {                 /* (eased: the flight slows, the shards come in and close up) */
        cv_begin(240, 120, C_BLACK);
        cv_oy = -120 * (int32_t)band;
        boot_sky(f);
        boot_land(BOOT_ROW0 * 256 + (int32_t)e * 5, (696u + e * 3u) & 1023u, BOOT_EYE, (int32_t)((256u - e) * 3u / 2u));
        boot_line(14, "FMX - Jungle Groovebox", C_WHITE);
        boot_line(34, "1.0.0_beta", RGB(150, 200, 255));   /* (the label; FELUCCA_VERSION follows later) */
        boot_line(218, "based on SLOOP and felucca", RGB(150, 160, 200));
        cv_blit(0, 120u * band);
    }
    cv_oy = 0;
}

static void boot_splash(void) { boot_frame(0); }

static void boot_anim(void)
{
    uint32_t f, t0 = fm1_ms;
    for (f = 1; f < BOOT_NF; f++) {
        int32_t wait;
        boot_frame(f);
        wait = (int32_t)(t0 + f * BOOT_MS - fm1_ms);    /* (by the clock: the frame's drawing is in its time) */
        if (wait > 0)
            fm1_delay_ms((uint32_t)wait);
    }
    fm1_delay_ms(250);                                  /* (the gem, whole, a moment) */
}
