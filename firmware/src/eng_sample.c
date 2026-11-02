/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* SAMPLE: IMA ADPCM sample sets. */
static const char *const N_ONOFF_E[] = {"OFF", "ON"};
static const char *const N_SMP_INTP[] = {"LIN", "PSX"};
/* IMA ADPCM sample sets (tools/gen_samples.py), native rates, zones across
 * the keyboard, loops with the ADPCM state stored at the loop start.
 * SET is P_E0 (EDIT 1, KNOB 1). */
typedef struct {
    uint32_t off, n, ls, le;     /* byte offset, samples, loop start / end (sample index) */
    uint32_t rate;               /* source rate / 44100, Q16 */
    int16_t root16;              /* root pitch in 1/16 semitone */
    int16_t pred;                /* ADPCM state at the loop start */
    uint8_t idx, lo, hi, looped;
} smp_zone_t;
typedef struct {
    const char *name;
    uint16_t z0, nz;
} smp_set_t;
#include "felucca_samples.h"

static const int16_t IMA_STEP[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97,
    107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796,
    876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428,
    4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350,
    22385, 24623, 27086, 29794, 32767};
static const int8_t IMA_IDX[8] = {-1, -1, -1, -1, 2, 4, 6, 8};
/* 2^(i/192), Q16: pitch ratios in 1/16 semitones, d16 >= -3072 (16 octaves down) */
static uint32_t pow2_q16(int32_t d16)
{
    uint32_t u = (uint32_t)(d16 + 192 * 16), oct = u / 192u;     /* no loop for negative d16 */
    uint32_t r = pitch_inc(1600 + u % 192u) / (pitch_inc(1600) >> 16);   /* 2^(d/192) via the pitch table */
    return oct >= 16u ? r << (oct - 16u) : r >> (16u - oct);
}

/* ---- the sample pool (FMX): the user's sounds in the free flash, as many as fit (see web/EDITOR_PROTOCOL.md)
 * Two regions of 4 KiB sectors: 0xA0000..0xDBFFF (sectors 0..59) and 0xE7000..0xFAFFF (60..79), 320 KiB, read
 * through the plain XIP window. An entry starts at any sector and takes as many as it needs, inside one region:
 * header (magic, count, name, data length, CRC32) + up to 16 zones in the smp_zone_t layout (off relative to the
 * entry's data at +512) + IMA ADPCM data; FMX's SLC1 meta (chopgen.c) in the 32 B at +480. The boot scan finds
 * them: a sector that starts with a valid header is an entry, and the sectors it covers are skipped. An entry's id
 * (what a sound keeps in SET: SMP_NSETS + id) is its header's rsv byte - 1; rsv 0, an entry written before the
 * pool, takes the id of the old slot at its place (USR1..USR4 = ids 0..3), anywhere else the first free one. */
#include "../hal/fm1_xip.h"   /* relative: hostsim includes this file too */
#define SMP_POOL_N 80u                              /* ids: one entry a sector at most */
#define SMP_POOL_SECT 0x1000u
#define SMP_POOL_NS0 60u                            /* sectors in the first region (0xA0000), then 20 at 0xE7000 */
#define SMP_POOL_ZONES 384u                         /* the zones of all the entries (RAM): 24 full ones */
#define SMP_SECT_OFF(s) ((s) < SMP_POOL_NS0 ? 0xA0000u + (s) * SMP_POOL_SECT : 0xE7000u + ((s) - SMP_POOL_NS0) * SMP_POOL_SECT)
#define SMP_SECT_END(s) ((s) < SMP_POOL_NS0 ? SMP_POOL_NS0 : SMP_POOL_N)   /* the end of sector s's region */
#define SMP_POOL_MAXDATA (SMP_POOL_NS0 * SMP_POOL_SECT - SMP_USER_DATA)
#define SMP_USER_SLOTS 4u                           /* the old slots USR1..USR4 (ids 0..3): their places, 80 KiB */
#define SMP_USER_SIZE 0x14000u
#define SMP_USER_SECT(k) ((k) < 3u ? (k) * 20u : 60u)
#define SMP_USER_OFF(k) SMP_SECT_OFF(SMP_USER_SECT(k))
#define SMP_USER_DATA 512u
#define SMP_USER_MAGIC 0x504D5346u                  /* "FSMP" */
#define SMP_MEL_MAGIC 0x314C454Du                   /* "MEL1" at +480: INST's melody bank (chopgen.c cm_bank), not a sound */
#define SMP_NALL (SMP_LATE0 + SMP_NLATE)              /* SET: the first built-in sets, the pool's ids, the later sets */
_Static_assert(SMP_GEN_POOL_N == SMP_POOL_N && SMP_LATE0 == SMP_NSETS + SMP_POOL_N, "tools/gen_samples.py POOL_N");
typedef struct {
    uint32_t magic;
    uint16_t version;
    uint8_t nz, rsv;                                /* rsv: the id + 1 (0: by its place) */
    char name[8];
    uint32_t data_len, crc, rsv2[2];
    smp_zone_t zone[16];
} smp_user_hdr_t;                                   /* 32 + 16 x 28 = 480 B, data at +512 */
static smp_zone_t usr_zt[SMP_POOL_ZONES];           /* RAM copy of every entry's zones, off rebased onto SMP_DATA */
static uint16_t usr_z0[SMP_POOL_N];                 /* an entry's first zone in usr_zt */
static uint8_t usr_nz[SMP_POOL_N];                  /* its zones, 0 = no entry with that id */
static uint8_t usr_sect[SMP_POOL_N], usr_nsect[SMP_POOL_N];   /* where it is: its first sector, its sectors */
static char usr_name[SMP_POOL_N][9];                /* its name (its header's; "U12" when that is empty) */
#define USR_Z(k, i) (&usr_zt[usr_z0[k] + (i)])      /* zone i of entry k */
/* SET's names: the built-in sets, each id's entry ("-": none), the built-in sets made after the pool (FMX 2026-10-26:
 * after it, so that a user sound's SET, SMP_NSETS + id, stays what it was) */
static const char *SMP_ALL_NAMES[SMP_NALL] = {SMP_SET_NAMES_INIT, [SMP_NSETS ... SMP_LATE0 - 1u] = "-", SMP_LATE_NAMES_INIT};
/* the built-in set SET si stands for, 0 = an id of the pool */
static const smp_set_t *smp_builtin(uint32_t si)
{
    return si < SMP_NSETS ? &SMP_SETS[si] : si - SMP_LATE0 < SMP_NLATE ? &SMP_SETS[SMP_NSETS + si - SMP_LATE0] : 0;
}

#ifndef SMP_POOL_XIP                                /* host tests: a RAM image of the flash */
#define SMP_POOL_XIP(off) fm1_xip_ptr(off)
#endif
static const uint8_t *smp_user_xip(uint32_t k) { return SMP_POOL_XIP(SMP_SECT_OFF((uint32_t)usr_sect[k])); }
#if FELUCCA_SLICE
#define SLC_NUSR 3u                                 /* SLICE's sources USR1..3: ids 0..2 */
static void slc_user_scan(uint32_t k, int valid);   /* eng_slice.c: SLICE's slice table of the entry */
#endif
static uint32_t smp_user_gen;                       /* + 1 a pool scan (what was built from an entry follows uploads) */
static int16_t cs_tune16[16];                       /* FMX, chopgen.c: TUNE, each slice of the CHOP track's slot (1/16 */
static volatile uint8_t cs_tune_on;                 /* semitones; the main loop writes them, a CHOP voice reads them) */
static volatile int16_t cs_dn16;                    /* chopgen.c: SLICE's DOWN (1/16 semitones, every CHOP voice; the ISR's) */

/* a zone of an entry's header usable (inside the entry's data, well formed) */
static int smp_zone_ok(const smp_zone_t *z, uint32_t data_len)
{
    return z->off <= data_len && z->n <= 2u * SMP_POOL_MAXDATA && z->off + (z->n + 1u) / 2u <= data_len &&
           z->idx <= 88u && z->rate && z->rate <= 4u << 16 && z->ls <= z->le && z->le < z->n && z->lo <= z->hi;
}
/* a header of an entry that fits in cap bytes (from its header to its region's end), every zone well formed */
static int smp_hdr_ok(const smp_user_hdr_t *h, uint32_t cap)
{
    uint32_t i;
    if (h->magic != SMP_USER_MAGIC || h->version != 1 || !h->nz || h->nz > 16u || cap < SMP_USER_DATA ||
        h->data_len > cap - SMP_USER_DATA)
        return 0;
    for (i = 0; i < h->nz; i++)
        if (!smp_zone_ok(&h->zone[i], h->data_len))
            return 0;
    return 1;
}
#define SMP_HDR_NSECT(h) ((SMP_USER_DATA + (h)->data_len + SMP_POOL_SECT - 1u) / SMP_POOL_SECT)

/* read the pool from the flash again: at boot, after an upload or an erase (main loop: SLICE scans the audio of its
 * slots here). Every entry is written again under one IRQ-off stretch: a voice keeps its entry's id and zone number
 * (sample_note_on), so one still playing an entry that is there after the scan goes on as it was */
static void smp_pool_scan(void)
{
    static uint8_t fs[SMP_POOL_N], fn[SMP_POOL_N], at[SMP_POOL_N];   /* the entries found: sector, sectors; an id's */
    uint32_t s, nf = 0, i, k, z0 = 0;
    smp_user_gen++;
    for (s = 0; s < SMP_POOL_N;) {
        const smp_user_hdr_t *h = (const smp_user_hdr_t *)SMP_POOL_XIP(SMP_SECT_OFF(s));
        if (!smp_hdr_ok(h, (SMP_SECT_END(s) - s) * SMP_POOL_SECT)) {
            s++;
            continue;
        }
        fs[nf] = (uint8_t)s;
        fn[nf] = (uint8_t)SMP_HDR_NSECT(h);
        s += fn[nf++];
    }
    for (k = 0; k < SMP_POOL_N; k++)
        at[k] = 0xFF;
    for (i = 0; i < nf; i++) {                      /* the ids: the header's own, */
        k = ((const smp_user_hdr_t *)SMP_POOL_XIP(SMP_SECT_OFF(fs[i])))->rsv;
        if (k && k <= SMP_POOL_N && at[k - 1u] == 0xFF)
            at[k - 1u] = (uint8_t)i;
    }
    for (i = 0; i < nf; i++)                        /* an old slot's at its place (rsv 0), */
        if (!((const smp_user_hdr_t *)SMP_POOL_XIP(SMP_SECT_OFF(fs[i])))->rsv)
            for (k = 0; k < SMP_USER_SLOTS; k++)
                if (fs[i] == SMP_USER_SECT(k) && at[k] == 0xFF)
                    at[k] = (uint8_t)i;
    for (i = 0; i < nf; i++) {                      /* then the first free one (there are as many ids as sectors) */
        for (k = 0; k < SMP_POOL_N && at[k] != i; k++)
            ;
        if (k < SMP_POOL_N)
            continue;
        for (k = 0; at[k] != 0xFF; k++)
            ;
        at[k] = (uint8_t)i;
    }
#if FELUCCA_SLICE
    for (k = 0; k < SLC_NUSR; k++)
        slc_user_scan(k, 0);
#endif
    fm1_irq_off();
    for (k = 0; k < SMP_POOL_N; k++) {
        const smp_user_hdr_t *h;
        uint32_t base;
        usr_nz[k] = 0;
        usr_nsect[k] = 0;
        if (at[k] == 0xFF)
            continue;
        h = (const smp_user_hdr_t *)SMP_POOL_XIP(SMP_SECT_OFF(fs[at[k]]));
        usr_sect[k] = fs[at[k]];
        usr_nsect[k] = fn[at[k]];                   /* (there: its place is taken, even with no room for its zones) */
        if (z0 + h->nz > SMP_POOL_ZONES)
            continue;
        base = (uint32_t)(uintptr_t)((const uint8_t *)h + SMP_USER_DATA) - (uint32_t)(uintptr_t)SMP_DATA;
        for (i = 0; i < h->nz; i++) {
            usr_zt[z0 + i] = h->zone[i];
            usr_zt[z0 + i].off += base;
        }
        usr_z0[k] = (uint16_t)z0;
        usr_nz[k] = h->nz;
        z0 += h->nz;
    }
    fm1_irq_on();
    for (k = 0; k < SMP_POOL_N; k++) {
        const smp_user_hdr_t *h = (const smp_user_hdr_t *)smp_user_xip(k);
        char *nm = usr_name[k];
        nm[0] = 0;
        SMP_ALL_NAMES[SMP_NSETS + k] = "-";
        if (!usr_nz[k] || *(const uint32_t *)((const uint8_t *)h + 480u) == SMP_MEL_MAGIC)
            continue;                               /* (the melody bank: no SET of its own) */
        for (i = 0; i < 8u && h->name[i] >= ' ' && h->name[i] < 127; i++)
            nm[i] = h->name[i];
        nm[i] = 0;
        if (!i) {                                   /* (no name: U and its number) */
            nm[0] = 'U';
            nm[1] = (char)(k + 1u >= 10u ? '0' + (k + 1u) / 10u : '0' + k + 1u);
            nm[2] = (char)(k + 1u >= 10u ? '0' + (k + 1u) % 10u : 0);
            nm[3] = 0;
        }
        SMP_ALL_NAMES[SMP_NSETS + k] = nm;
    }
#if FELUCCA_SLICE
    for (k = 0; k < SLC_NUSR; k++)
        if (usr_nz[k])
            slc_user_scan(k, 1);
#endif
}

/* sectors [s, s + n) inside one region and clear of every entry but entry k: a place an upload of k may take */
static int smp_pool_room(uint32_t s, uint32_t n, uint32_t k)
{
    uint32_t j;
    if (!n || s >= SMP_POOL_N || n > SMP_SECT_END(s) - s)
        return 0;
    for (j = 0; j < SMP_POOL_N; j++)
        if (j != k && usr_nsect[j] && usr_sect[j] < s + n && s < (uint32_t)usr_sect[j] + usr_nsect[j])
            return 0;
    return 1;
}

/* SET turned by steps: the sets that are there, the built-in ones and the pool's entries (an id with none is
 * skipped); it stops at either end */
static int32_t smp_set_turn(int32_t v, int32_t steps)
{
    int32_t d = steps > 0 ? 1 : -1, n = steps * d;
    while (n-- > 0) {
        int32_t w = v + d;
        while (w >= (int32_t)SMP_NSETS && w < (int32_t)SMP_LATE0 && !usr_nz[w - (int32_t)SMP_NSETS])
            w += d;
        if (w < 0 || w >= (int32_t)SMP_NALL)
            break;
        v = w;
    }
    return v;
}

/* FMX, the CHOP track: set si (0..SMP_NALL - 1) as a kit of hits, one per key: its zones (the lowest key, their
 * count, *n); 0 = not a kit (a built-in set of pitched notes: its zones span several keys; an id with no entry) */
static int smp_kit(uint32_t si, uint32_t *base, uint32_t *n)
{
    const smp_zone_t *z;
    const smp_set_t *set = smp_builtin(si);
    uint32_t nz, i, lo = 127;
    if (set) {
        z = &SMP_ZONES[set->z0];
        nz = set->nz;
    } else if (si - SMP_NSETS < SMP_POOL_N) {
        z = USR_Z(si - SMP_NSETS, 0);
        nz = usr_nz[si - SMP_NSETS];
    } else {
        return 0;
    }
    if (!nz)
        return 0;
    for (i = 0; i < nz; i++) {
        if (z[i].lo != z[i].hi)
            return 0;                               /* (an instrument, a slot of break loops: not a kit) */
        lo = z[i].lo < lo ? z[i].lo : lo;
    }
    *base = lo;
    *n = nz;
    return 1;
}

/* the first entry of the pool that holds a kit (the CHOP KIT preset's set); id 0 when none does */
static uint32_t smp_first_kit(void)
{
    uint32_t k, b, n;
    for (k = 0; k < SMP_POOL_N; k++)
        if (smp_kit(SMP_NSETS + k, &b, &n))
            return SMP_NSETS + k;
    return SMP_NSETS;
}

/* the set of a preset on a sound pack (SMP_PRESET_PACK): the first entry of the pool with that name ("*KIT": the first
 * that holds a kit); without it a free id (the preset is silent until the pack is loaded), else id 0 */
static const char *const SMP_PRESET_PACK[] = {SMP_PRESET_PACK_INIT};
static uint32_t smp_pack_slot(const char *pack)
{
    uint32_t k;
    if (pack[0] == '*')
        return smp_first_kit();
    for (k = 0; k < SMP_POOL_N; k++)
        if (usr_nz[k] && str_eq(usr_name[k], pack))
            return SMP_NSETS + k;
    for (k = 0; k < SMP_POOL_N; k++)
        if (!usr_nz[k])
            return SMP_NSETS + k;
    return SMP_NSETS;
}

/* zone index in a voice: < 0x8000 built-in, else 0x8000 | id << 4 | zone (an entry gone: a zone of no samples, the
 * voice ends) */
/* the sample data: the built-in sets, and the pool (its zones' offsets rebased onto it, past its end) read
 * through a plain pointer, not as an index into the array */
static const uint8_t *const smp_data = SMP_DATA;
static const smp_zone_t SMP_NOZONE;
static inline const smp_zone_t *smp_zone(uint32_t zi)
{
    uint32_t k = (zi >> 4) & 0x7FFu, i = zi & 15u;
    return zi < 0x8000u ? &SMP_ZONES[zi] : k < SMP_POOL_N && i < usr_nz[k] ? USR_Z(k, i) : &SMP_NOZONE;
}

/* voice: ph[0] position (samples), ph[1] fraction Q16, s[0] predictor, s[1] step
 * index, s[2] previous sample, s[3] current sample, s[4] zone, s[5] step Q16 */
static inline int32_t sample_next(const smp_zone_t *z, voice_t *v, int loop)
{
    uint32_t pos = v->ph[0], b = smp_data[z->off + (pos >> 1)];
    uint32_t code = (pos & 1u) ? (b >> 4) : (b & 15u);
    int32_t step = IMA_STEP[(uint32_t)v->s[1] <= 88u ? v->s[1] : 88], vd = step >> 3;
    if (code & 4u)
        vd += step;
    if (code & 2u)
        vd += step >> 1;
    if (code & 1u)
        vd += step >> 2;
    v->s[0] = clamp(v->s[0] + ((code & 8u) ? -vd : vd), -32768, 32767);
    v->s[1] = clamp(v->s[1] + IMA_IDX[code & 7u], 0, 88);
    pos++;
    if (pos > z->le && z->looped && loop) {
        pos = z->ls;
        v->s[0] = z->pred;
        v->s[1] = z->idx;
    }
    v->ph[0] = pos;
    return v->s[0];
}

static void sample_note_on(track_t *t, voice_t *v)
{
    uint32_t si = (uint32_t)t->p[P_E0] % SMP_NALL, i, zi = 0xFFFFu;
    const smp_set_t *set = smp_builtin(si);
    if (set) {
        for (i = 0; i < set->nz; i++)
            if (v->note >= SMP_ZONES[set->z0 + i].lo && v->note <= SMP_ZONES[set->z0 + i].hi)
                zi = set->z0 + i;
        v->s[4] = (int32_t)(zi == 0xFFFFu ? set->z0 : zi);
    } else {                                        /* an entry of the pool: silent if there is none */
        uint32_t k = si - SMP_NSETS;
        for (i = 0; i < usr_nz[k]; i++)
            if (v->note >= USR_Z(k, i)->lo && v->note <= USR_Z(k, i)->hi)
                zi = 0x8000u | k << 4 | i;
        v->s[4] = (int32_t)(zi == 0xFFFFu ? 0 : zi);
    }
    v->ph[0] = 0;
    v->ph[1] = 0;
    v->s[0] = 0;
    v->s[1] = 0;
    v->s[2] = v->s[3] = 0;
    v->s[5] = 0;                 /* INTP PSX: the two samples before s[2] (16 bits each) */
    v->s[6] = zi == 0xFFFFu;     /* 1 = sample ended (one-shot); a kit key with no sound stays silent */
    v->s[7] = 0;                 /* lo-pass state */
}

static void sample_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *p = t->p;
    const smp_zone_t *z = smp_zone((uint32_t)v->s[4]);
    uint32_t i, frac = v->ph[1];
    int32_t d16 = m->pitch16 + p[P_E1] * 16 - z->root16;
    uint32_t r, stepq;
    int32_t bits = p[P_E2], lp = 4000 + ((clamp((p[P_E4] << 8) + m->cutoff, 0, 127 << 8) * 28767) >> 15);
    int32_t drv = p[P_E6], sh = bits / 10, psx = p[P_E5];
    if (t == &trk[2]) {
        if (cs_tune_on && (uint32_t)v->s[4] >= 0x8000u)
            d16 += cs_tune16[v->s[4] & 15];           /* SLICE's TUNE: the slice to the session's key */
        d16 += cs_dn16;                               /* ... its DOWN */
    }
    d16 = clamp(d16, -1536, 576);                     /* <= 3 octaves up: bounded decode load */
    r = pow2_q16(d16);
    stepq = (r >> 8) * (z->rate >> 8);                /* Q16 samples per output */
    if (v->s[6] || !z->n) {
        v->active = 0;
        return;
    }
    if (v->ph[0] == 0 && v->ph[1] == 0 && v->s[3] == 0)
        v->s[3] = sample_next(z, v, p[P_E3]);         /* prime the interpolator */
    for (i = 0; i < n; i++) {
        int32_t s;
        frac += stepq;
        while (frac >= 65536u) {
            frac -= 65536u;
            v->s[5] = (int32_t)(((uint32_t)v->s[5] >> 16) | (uint32_t)(v->s[2] & 0xFFFF) << 16);   /* (older, old) */
            v->s[2] = v->s[3];
            if (v->ph[0] >= z->n) {                   /* one-shot reached its end */
                v->s[6] = 1;
                v->s[3] = 0;
                break;
            }
            v->s[3] = sample_next(z, v, p[P_E3]);
        }
        if (psx) {                                    /* INTP PSX: the SPU's Gaussian over the four newest */
            const int16_t *g = &GAUSS4[((frac >> 8) & 255u) * 4u];   /* (a one-shot that just ended: frac past 1) */
            s = (g[0] * (int32_t)(int16_t)(v->s[5] & 0xFFFF) + g[1] * (int32_t)(int16_t)((uint32_t)v->s[5] >> 16) +
                 g[2] * v->s[2] + g[3] * v->s[3]) >> 15;
        } else {
            s = v->s[2] + (((v->s[3] - v->s[2]) * (int32_t)(frac >> 1)) >> 15);
        }
        if (sh)                                       /* BITS: 0 = clean, up to 12 bits removed */
            s = (s >> sh) << sh;
        if (drv)
            s = softclip(s + (((s >> 2) * (drv * 150)) >> 11));   /* = s * drv * 600 / 32768, no overflow */
        v->s[7] += mulq15(s - v->s[7], lp);           /* gentle tone control (CUT) */
        out[i] += mulq15(mulq15(v->s[7], amp_at(m, i)), VOICE_FS) << 1;
        if (v->s[6])
            break;
    }
    v->ph[1] = frac;
}


static const engine_t ENG_SAMPLE = {
    "SAMPLE", {"SET", "TONE"},
    {
        {"SET", F_ENUM, 0, SMP_NALL - 1, 0, SMP_ALL_NAMES, 0},
        {"TUNE", F_SEMI, -24, 24, 0, 0, 0},
        {"BITS", F_INT, 0, 127, 0, 0, 0},
        {"LOOP", F_ENUM, 0, 1, 1, N_ONOFF_E, 0},
        {"CUT", F_INT, 0, 127, 127, 0, 0},
        {"INTP", F_ENUM, 0, 1, 0, N_SMP_INTP, 0},     /* FMX: LIN, or PSX (the SPU's Gaussian, softer) */
        {"DRV", F_PCT, 0, 127, 0, 0, 0},
        {"-", F_INT, 0, 0, 0, 0, 0},
    },
    SMP_PRESET_TABLE, sizeof(SMP_PRESET_TABLE) / sizeof(SMP_PRESET_TABLE[0]), -1, sample_note_on, sample_render,
    0xFFFF, {P_E0, P_E4, P_ATK, P_REL},
};
