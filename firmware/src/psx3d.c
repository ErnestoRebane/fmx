/* SPDX-License-Identifier: GPL-3.0-only */
/* FMX's low-poly 3D, in the manner of a PlayStation: flat-shaded triangles, no depth buffer (the faces sorted far
 * to near), the shade quantised to 16 steps and dithered with a 4 x 4 ordered pattern (as the console's GPU dithered
 * its 24-bit colours down to 15), an integer perspective. Draws into the canvas (gfx.c), cv_oy aware: a screen drawn
 * in two bands of 120 rows draws the same mesh twice.
 *   p3_mesh(...)   a convex mesh centred on 0 (each face's normal taken outward), turned (Y, then X), placed, lit
 *   p3_backdrop()  the dithered gradient behind (PS1 menus' dark blue)
 *   p3_tri_pal()   one triangle in a colour already dithered (p3_pal_rgb: any colour, as the console's GPU did) */
typedef struct { int16_t x, y, z; } p3v_t;
typedef struct { uint8_t a, b, c, col; } p3f_t;
static const uint8_t P3_BAYER[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};

static inline int32_t p3_sin(uint32_t a) { return SINE[a & 1023u]; }          /* a: 1024 a turn; Q15 */
static inline int32_t p3_cos(uint32_t a) { return SINE[(a + 256u) & 1023u]; }
static uint32_t p3_isqrt(uint32_t v)
{
    uint32_t r = 0, b = 1u << 30;
    while (b > v)
        b >>= 2;
    while (b) {
        if (v >= r + b) {
            v -= r + b;
            r = (r >> 1) + b;
        } else {
            r >>= 1;
        }
        b >>= 2;
    }
    return r;
}
/* colour c at shade s (0..255), dithered: the 16 cells of the pattern (16 steps, the step between by the pattern) */
static void p3_pal(uint16_t c, uint32_t s, uint16_t *pal)
{
    uint32_t i, r = (c >> 11) & 31u, g = (c >> 5) & 63u, b = c & 31u;
    for (i = 0; i < 16u; i++) {
        uint32_t q = (s * 15u + P3_BAYER[i] * 16u) / 255u;
        if (q > 15u)
            q = 15u;
        pal[i] = swap16(r * q / 15u << 11 | g * q / 15u << 5 | b * q / 15u);
    }
}
/* colour (r, g, b) (8 bits a channel) to 15-bit as the console's GPU dithered it (its own 4 x 4 table, -4..3): the 16
 * cells of the pattern */
static const int8_t P3_DITHER[16] = {-4, 0, -3, 1, 2, -2, 3, -1, -3, 1, -4, 0, 3, -1, 2, -2};
static void p3_pal_rgb(int32_t r, int32_t g, int32_t b, uint16_t *pal)
{
    uint32_t i;
    for (i = 0; i < 16u; i++) {
        int32_t k = P3_DITHER[i];
        pal[i] = swap16((uint32_t)clamp(r + k, 0, 255) >> 3 << 11 | (uint32_t)clamp(g + k, 0, 255) >> 3 << 6 |
                        (uint32_t)clamp(b + k, 0, 255) >> 3);
    }
}
/* a triangle in screen pixels, in the 16 cells of pal (p3_pal, p3_pal_rgb) */
static void p3_tri_pal(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t x2, int32_t y2, const uint16_t *pal)
{
    int32_t t, y, ya, yb;
    if (y1 < y0) { t = y0; y0 = y1; y1 = t; t = x0; x0 = x1; x1 = t; }
    if (y2 < y0) { t = y0; y0 = y2; y2 = t; t = x0; x0 = x2; x2 = t; }
    if (y2 < y1) { t = y1; y1 = y2; y2 = t; t = x1; x1 = x2; x2 = t; }
    if (y2 == y0)
        return;
    ya = y0 + cv_oy < 0 ? -cv_oy : y0;
    yb = y2 + cv_oy > (int32_t)cv_h - 1 ? (int32_t)cv_h - 1 - cv_oy : y2;
    for (y = ya; y <= yb; y++) {
        int32_t xa = x0 + (x2 - x0) * (y - y0) / (y2 - y0), xb, x, row;
        if (y < y1)
            xb = x0 + (x1 - x0) * (y - y0) / (y1 - y0);
        else
            xb = y2 == y1 ? x1 : x1 + (x2 - x1) * (y - y1) / (y2 - y1);
        if (xa > xb) { t = xa; xa = xb; xb = t; }
        if (xa < 0)
            xa = 0;
        if (xb > (int32_t)cv_w - 1)
            xb = (int32_t)cv_w - 1;
        row = (y + cv_oy) * (int32_t)cv_w;
        for (x = xa; x <= xb; x++)                      /* (both ends: no seams between the faces) */
            cv_px[row + x] = pal[((uint32_t)y & 3u) * 4u + ((uint32_t)x & 3u)];
    }
}
/* a triangle in screen pixels, colour c at shade s, dithered */
static void p3_tri(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t x2, int32_t y2, uint16_t c, uint32_t s)
{
    uint16_t pal[16];
    p3_pal(c, s, pal);
    p3_tri_pal(x0, y0, x1, y1, x2, y2, pal);
}

#define P3_MAXV 32u
#define P3_MAXF 48u
/* mesh (v: model units, |v| < 128; f: faces), turned ay about Y then ax about X (1024 a turn), at (cx, cy) on screen,
 * size `zoom` (pixels for 64 model units at the centre), the colours cols[f.col]; ambient amb (0..255); boom (0..256) pushes
 * each face out from the centre by that much of its distance (a burst: on screen, cheap) */
static void p3_mesh(const p3v_t *v, uint32_t nv, const p3f_t *f, uint32_t nf, const uint16_t *cols, uint32_t ay,
                    uint32_t ax, int32_t cx, int32_t cy, int32_t zoom, uint32_t amb, uint32_t boom)
{
    static int32_t tx[P3_MAXV], ty[P3_MAXV], tz[P3_MAXV], sx[P3_MAXV], sy[P3_MAXV];
    static uint8_t ord[P3_MAXF], shade[P3_MAXF];
    static int32_t depth[P3_MAXF];
    int32_t sa = p3_sin(ay), ca = p3_cos(ay), sb = p3_sin(ax), cb = p3_cos(ax);
    const int32_t D = 320;                               /* the camera's distance, model units */
    uint32_t i, n = 0;
    if (nv > P3_MAXV)
        nv = P3_MAXV;
    if (nf > P3_MAXF)
        nf = P3_MAXF;
    for (i = 0; i < nv; i++) {                            /* turn, then project */
        int32_t x = (v[i].x * ca + v[i].z * sa) >> 15, z = (v[i].z * ca - v[i].x * sa) >> 15;
        int32_t y = (v[i].y * cb - z * sb) >> 15;
        z = (v[i].y * sb + z * cb) >> 15;
        tx[i] = x;
        ty[i] = y;
        tz[i] = z;
        sx[i] = cx + x * zoom * D / (64 * (z + D));
        sy[i] = cy + y * zoom * D / (64 * (z + D));
    }
    for (i = 0; i < nf; i++) {                            /* the faces that look at the camera, lit */
        const p3f_t *q = &f[i];
        int32_t ux = tx[q->b] - tx[q->a], uy = ty[q->b] - ty[q->a], uz = tz[q->b] - tz[q->a];
        int32_t wx = tx[q->c] - tx[q->a], wy = ty[q->c] - ty[q->a], wz = tz[q->c] - tz[q->a];
        int32_t nx = uy * wz - uz * wy, ny = uz * wx - ux * wz, nz = ux * wy - uy * wx;
        int32_t mx = tx[q->a] + tx[q->b] + tx[q->c], my = ty[q->a] + ty[q->b] + ty[q->c], mz = tz[q->a] + tz[q->b] + tz[q->c];
        int32_t len, lit;
        if ((int64_t)nx * mx + (int64_t)ny * my + (int64_t)nz * mz < 0) {   /* (outward: away from the centre) */
            nx = -nx;
            ny = -ny;
            nz = -nz;
        }
        if ((int64_t)nx * mx + (int64_t)ny * my + (int64_t)nz * (mz + 3 * D) >= 0)
            continue;                                     /* (faces away from the camera) */
        len = (int32_t)p3_isqrt((uint32_t)(nx / 8 * (nx / 8) + ny / 8 * (ny / 8) + nz / 8 * (nz / 8))) * 8 + 1;
        lit = -(nx * 3 / 8 + ny * 5 / 8 + nz * 6 / 8) * 255 / len;   /* the light from the upper left, towards us */
        lit = (int32_t)amb + lit * (int32_t)(255u - amb) / 255;
        shade[i] = (uint8_t)clamp(lit, (int32_t)amb / 2, 255);
        depth[i] = mz;
        ord[n++] = (uint8_t)i;
    }
    for (i = 1; i < n; i++) {                             /* far to near (insertion: a few dozen faces) */
        uint8_t k = ord[i];
        uint32_t j = i;
        while (j && depth[ord[j - 1u]] < depth[k]) {
            ord[j] = ord[j - 1u];
            j--;
        }
        ord[j] = k;
    }
    for (i = 0; i < n; i++) {
        const p3f_t *q = &f[ord[i]];
        int32_t dx = ((sx[q->a] + sx[q->b] + sx[q->c]) / 3 - cx) * (int32_t)boom / 256;
        int32_t dy = ((sy[q->a] + sy[q->b] + sy[q->c]) / 3 - cy) * (int32_t)boom / 256;
        p3_tri(sx[q->a] + dx, sy[q->a] + dy, sx[q->b] + dx, sy[q->b] + dy, sx[q->c] + dx, sy[q->c] + dy, cols[q->col],
               shade[ord[i]]);
    }
}

/* the dithered gradient: c0 at the top of the screen to c1 at its bottom (240 rows), in the band drawn */
static void p3_backdrop(uint16_t c0, uint16_t c1)
{
    int32_t y, x;
    uint32_t r0 = (c0 >> 11) & 31u, g0 = (c0 >> 5) & 63u, b0 = c0 & 31u;
    uint32_t r1 = (c1 >> 11) & 31u, g1 = (c1 >> 5) & 63u, b1 = c1 & 31u;
    for (y = 0; y < (int32_t)cv_h; y++) {
        int32_t sy = y - cv_oy, row = y * (int32_t)cv_w;
        uint32_t k = (uint32_t)clamp(sy, 0, 239) * 256u / 240u, i;
        uint16_t px4[4];
        for (i = 0; i < 4u; i++) {                       /* (the channel's fraction rounded by the pattern) */
            uint32_t d = P3_BAYER[((uint32_t)sy & 3u) * 4u + i] * 16u + 8u;
            px4[i] = swap16(((r0 * (256u - k) + r1 * k + d) >> 8) << 11 | ((g0 * (256u - k) + g1 * k + d) >> 8) << 5 |
                            ((b0 * (256u - k) + b1 * k + d) >> 8));
        }
        for (x = 0; x < (int32_t)cv_w; x++)
            cv_px[row + x] = px4[x & 3];
    }
}

/* the meshes: an icosahedron (20 faces) and a diamond (an octahedron stretched: 8) */
static const p3v_t P3_GEM_V[6] = {{0, -78, 0}, {0, 78, 0}, {48, 0, 0}, {-48, 0, 0}, {0, 0, 48}, {0, 0, -48}};
static const p3f_t P3_GEM_F[8] = {
    {0, 2, 4, 0}, {0, 4, 3, 1}, {0, 3, 5, 0}, {0, 5, 2, 1}, {1, 4, 2, 1}, {1, 3, 4, 0}, {1, 5, 3, 1}, {1, 2, 5, 0}};
