/* SPDX-License-Identifier: GPL-3.0-only */
/* The sample pool (FMX: firmware/src/eng_sample.c, its uploads firmware/src/pool.c) on the Mac, on a RAM image of the
 * flash (hostsim.c's host_nor) with a NOR-like driver (an erase sets 0xFF, a write ANDs):
 *   build/host/pool_test
 * 1. an empty pool: no entry, SET's names "-", SET turned stays on the built-in sets.
 * 2. uploads as the editor sends them (begin, writes, end): many more than the 4 old slots, small and large (an entry
 *    larger than an old slot of 80 KiB), each in its own place; their ids, names, zones; a sound of each plays.
 * 3. the places: across the two regions, outside the pool, over another entry: refused; the upload's errors.
 * 4. an id uploaded again elsewhere: its old place is free, a voice on it ends; an erase.
 * 5. the old slots (rsv 0): USR1..USR4 at their places keep ids 0..3 (old projects play them), elsewhere the first
 *    free id; a scan again keeps every id.
 * 6. SET turned skips the ids with no entry; the CHOP's kits and the BREAK's loops across the pool.
 * 7. the zone table full: the entry keeps its place, has no zones. */
#include <stdarg.h>
#include <stdint.h>
#define main hostsim_main
#include "hostsim.c"
#undef main

static int fails;
static void check(const char *what, int ok, const char *fmt, ...)
{
    printf("pool: %-80s %s", what, ok ? "ok" : "FAIL");
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

/* ---- pool.c's flash driver and CRC, on the RAM image ---- */
static uint8_t *nor8(void) { return (uint8_t *)host_nor; }
static uint32_t n_erase, n_write;
static int fl_erase4k_quiet(uint32_t off, uint32_t *took)
{
    memset(nor8() + (off & ~0xFFFu), 0xFF, 0x1000u);
    *took = 0;
    n_erase++;
    return 0;
}
static int fl_write(uint32_t off, const uint8_t *src, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; i++)
        nor8()[off + i] &= src[i];
    n_write++;
    return 0;
}
static void fl_inval(uint32_t off, uint32_t len) { (void)off; (void)len; }
static void fm1_wdt_feed(void) {}
static uint32_t st_crc32(const void *p, uint32_t n)   /* zlib CRC-32, as storage.c */
{
    const uint8_t *b = p;
    uint32_t c = 0xFFFFFFFFu, k;
    while (n--) {
        c ^= *b++;
        for (k = 0; k < 8u; k++)
            c = c & 1u ? (c >> 1) ^ 0xEDB88320u : c >> 1;
    }
    return ~c;
}
#include "../firmware/src/pool.c"

/* ---- an entry image: nz zones of n samples each (kit: one key each from 36, else one zone over every key) ---- */
static uint8_t img[0x3C000];
static uint32_t make(const char *name, uint32_t nz, uint32_t n, int kit, uint8_t fill)
{
    smp_user_hdr_t *h = (smp_user_hdr_t *)img;
    uint32_t i, per = (n + 1u) / 2u;
    memset(img, 0, sizeof img);
    h->magic = SMP_USER_MAGIC;
    h->version = 1;
    h->nz = (uint8_t)nz;
    strncpy(h->name, name, 8);
    h->data_len = per * nz;
    for (i = 0; i < nz; i++) {
        smp_zone_t *z = &h->zone[i];
        z->off = i * per;
        z->n = n;
        z->ls = 0;
        z->le = n - 1u;
        z->rate = 1u << 15;                         /* 22050 Hz */
        z->root16 = (int16_t)((kit ? 36 + (int)i : 60) * 16);
        z->lo = (uint8_t)(kit ? 36u + i : 0u);
        z->hi = (uint8_t)(kit ? 36u + i : 127u);
    }
    memset(img + SMP_USER_DATA, fill, h->data_len);
    h->crc = st_crc32(img + SMP_USER_DATA, h->data_len);
    return (SMP_USER_DATA + h->data_len + SMP_POOL_SECT - 1u) / SMP_POOL_SECT;   /* its sectors */
}
/* as the editor sends it: begin, the data in 256 B writes, the header; the first rc that is not 0 (x 10 + step) */
static uint32_t upload(uint32_t k, uint32_t s, uint32_t n)
{
    const smp_user_hdr_t *h = (const smp_user_hdr_t *)img;
    uint8_t hdr[512];
    uint32_t off, rc;
    if ((rc = smp_up_begin(k, s, n)) != 0)
        return 10u + rc;
    for (off = 0; off < h->data_len; off += 256u)
        if ((rc = smp_up_write(k, SMP_USER_DATA + off, img + SMP_USER_DATA + off,
                               h->data_len - off < 256u ? h->data_len - off : 256u)) != 0)
            return 20u + rc;
    memcpy(hdr, img, sizeof hdr);
    return (rc = smp_up_end(k, hdr, sizeof(smp_user_hdr_t))) != 0 ? 30u + rc : 0u;
}

/* entry k played: the samples a note sounds over a few blocks (0 = silent) */
static uint32_t sounds(uint32_t k, uint32_t note)
{
    track_t *t = &trk[1];
    voice_t v;
    vmod_t m;
    int32_t out[256];
    uint32_t i, j, nz = 0;
    host_preset(t, ENGI_SAMPLE, 0);
    t->p[P_E0] = (int16_t)(SMP_NSETS + k);
    memset(&v, 0, sizeof v);
    memset(&m, 0, sizeof m);
    v.note = (uint8_t)note;
    v.active = 1;
    m.pitch16 = (int32_t)note * 16;
    m.amp0 = m.amp1 = 32767;
    m.cutoff = 127 << 8;
    sample_note_on(t, &v);
    for (j = 0; j < 8u && v.active; j++) {
        memset(out, 0, sizeof out);
        sample_render(t, &v, out, 256u, &m);
        for (i = 0; i < 256u; i++)
            nz += out[i] != 0;
    }
    return nz;
}

static void pool_erased(void)                       /* both regions erased, as a new device */
{
    memset(nor8() + 0xA0000u, 0xFF, 0x3C000u);
    memset(nor8() + 0xE7000u, 0xFF, 0x14000u);
    smp_pool_scan();
}
static uint32_t entries(void)
{
    uint32_t k, n = 0;
    for (k = 0; k < SMP_POOL_N; k++)
        n += usr_nsect[k] != 0;
    return n;
}

int main(void)
{
    uint32_t k, n, rc, bad;

    host_tracks_init();
    /* 1 */
    pool_erased();
    for (k = 0, bad = 0; k < SMP_POOL_N; k++)
        bad += usr_nz[k] || usr_nsect[k] || !str_eq(SMP_ALL_NAMES[SMP_NSETS + k], "-");
    check("an empty pool: no entry, SET's names \"-\"", !bad && entries() == 0u, "%u", bad);
    check("SET turned past the first built-in sets: over the empty pool onto the later ones (SMP_LATE0..), then it "
          "stays on the last", smp_set_turn(SMP_NSETS - 1, 1) == SMP_LATE0 && smp_set_turn(SMP_LATE0, -1) == SMP_NSETS - 1 &&
          smp_set_turn(SMP_NSETS - 1, SMP_NLATE + 5) == SMP_NALL - 1 && smp_set_turn(0, -3) == 0, 0);
    check("... a later set plays as a built-in one, not as an id of the pool", SMP_NLATE == 0 ||
          (smp_builtin(SMP_LATE0) == &SMP_SETS[SMP_NSETS] && !smp_builtin(SMP_NSETS) && !smp_builtin(SMP_LATE0 - 1)), 0);

    /* 2: 40 kits of one sector each (8 hits of 400 samples: 1600 B), then a long loop of 150 KiB */
    for (k = 0, bad = 0; k < 40u; k++) {
        char nm[9];
        snprintf(nm, sizeof nm, "KIT%u", (unsigned)k);
        n = make(nm, 8, 400, 1, (uint8_t)(0x11u * (k % 7u + 1u)));
        rc = upload(k, k < 20u ? k : 60u + (k - 20u), n);  /* (the first 20 in region 0, the rest at 0xE7000) */
        bad += rc != 0u;
        if (rc)
            printf("  kit %u: rc %u\n", (unsigned)k, (unsigned)rc);
    }
    check("40 uploads of one sector each: every one taken", !bad && entries() == 40u, "%u entries", entries());
    for (k = 0, bad = 0; k < 40u; k++) {
        char nm[9];
        snprintf(nm, sizeof nm, "KIT%u", (unsigned)k);
        bad += usr_nz[k] != 8u || usr_nsect[k] != 1u || usr_sect[k] != (k < 20u ? k : 60u + k - 20u) ||
               !str_eq(usr_name[k], nm) || SMP_ALL_NAMES[SMP_NSETS + k] != usr_name[k] ||
               ((const smp_user_hdr_t *)smp_user_xip(k))->rsv != k + 1u;
    }
    check("... each its id, place, zones, name (SET shows it), rsv = id + 1", !bad, "%u bad", bad);
    for (k = 0, bad = 0; k < 40u; k++)
        bad += sounds(k, 36u + k % 8u) < 300u;
    check("... a hit of each of the 40 sounds (SET = SMP_NSETS + id)", !bad, "%u silent", bad);
    {
        uint32_t b, nh;
        check("... each a kit (the CHOP's KIT), 8 hits from key 36", smp_kit(SMP_NSETS + 37u, &b, &nh) && b == 36u && nh == 8u, 0);
    }
    n = make("LONGLOOP", 1, 300000, 0, 0x34);       /* 150,000 B: more than an old slot (80 KiB) */
    rc = upload(45, 20, n);
    check("a loop of 150 KB (more than an old slot) in sectors 20..56: taken", rc == 0u && usr_nz[45] == 1u &&
          usr_nsect[45] == n && n == 37u && USR_Z(45, 0)->n == 300000u, "rc %u, %u sectors", (unsigned)rc, (unsigned)n);
    {
        track_t *t = &trk[1];
        voice_t v;
        vmod_t m;
        int32_t out[64];
        uint32_t i, nz = 0;
        host_preset(t, ENGI_SAMPLE, 0);
        t->p[P_E0] = (int16_t)(SMP_NSETS + 45u);
        t->p[P_E3] = 0;                             /* (LOOP off: a one-shot) */
        memset(&v, 0, sizeof v);
        memset(&m, 0, sizeof m);
        v.note = 60;
        v.active = 1;
        m.pitch16 = 60 * 16;
        m.amp0 = m.amp1 = 32767;
        m.cutoff = 127 << 8;
        sample_note_on(t, &v);
        v.ph[0] = 299000u;                          /* near its end, past the old 80 KiB */
        v.s[3] = 1;
        for (i = 0; i < 4u && v.active; i++) {
            memset(out, 0, sizeof out);
            sample_render(t, &v, out, 64u, &m);
            for (k = 0; k < 64u; k++)
                nz += out[k] != 0;
        }
        check("... the samples past 160,000 play (from the entry's own data)", nz > 100u, "%u", nz);
    }

    /* 3: the places */
    n = make("X", 1, 2000, 0, 0x22);
    check("a place across the two regions (sectors 58..61): refused (3)", smp_up_begin(50, 58, 4) == 3u, 0);
    check("a place outside the pool (sector 80), none long: refused (3)", smp_up_begin(50, 80, 1) == 3u &&
          smp_up_begin(50, 70, 0) == 3u && smp_up_begin(SMP_POOL_N, 70, 1) == 3u, 0);
    check("a place over another entry (KIT3 at sector 3; the long loop over 40): refused (2)",
          smp_up_begin(50, 2, 2) == 2u && smp_up_begin(50, 40, 1) == 2u && entries() == 41u && usr_nz[3] == 8u, 0);
    check("a write with no upload open: 4; an end: 6", smp_up_write(50, 512, img, 256) == 4u && smp_up_end(50, img, 480) == 6u, 0);
    check("a begin in a free place (sector 57)", smp_up_begin(50, 57, 1) == 0u && smp_up.on && smp_up.id == 50u, 0);
    check("... a write before 512, not on 256, past the place, of another id: refused",
          smp_up_write(50, 256, img, 256) == 1u && smp_up_write(50, 600, img, 256) == 1u &&
          smp_up_write(50, 4096, img, 256) == 1u && smp_up_write(51, 512, img, 256) == 4u, 0);
    {
        uint8_t hdr[512];
        memcpy(hdr, img, sizeof hdr);
        ((smp_user_hdr_t *)hdr)->crc ^= 1u;
        check("... an end with the wrong CRC: 3, no entry", smp_up_end(50, hdr, 480) == 3u && !usr_nsect[50], 0);
    }
    check("... the same again from the begin: there", upload(50, 57, 1) == 0u && usr_nz[50] == 1u && usr_sect[50] == 57u, 0);

    /* 4: an id again, elsewhere; a voice on it ends; an erase */
    {
        track_t *t = &trk[1];
        voice_t v;
        vmod_t m;
        int32_t out[64];
        host_preset(t, ENGI_SAMPLE, 0);
        t->p[P_E0] = (int16_t)(SMP_NSETS + 50u);
        memset(&v, 0, sizeof v);
        memset(&m, 0, sizeof m);
        v.note = 60;
        v.active = 1;
        m.amp0 = m.amp1 = 32767;
        m.cutoff = 127 << 8;
        sample_note_on(t, &v);
        sample_render(t, &v, out, 64u, &m);
        rc = smp_up_begin(50, 58, 2);
        sample_render(t, &v, out, 64u, &m);
        check("id 50 uploaded again at 58: its old place free, a voice on it ends", rc == 0u && !v.active && !usr_nsect[50] &&
              smp_pool_room(57, 1, 0), 0);
        n = make("AGAIN", 2, 4000, 0, 0x45);
        check("... the new one there (2 sectors at 58)", upload(50, 58, n) == 0u && usr_sect[50] == 58u && usr_nsect[50] == 2u &&
              usr_nz[50] == 2u && str_eq(usr_name[50], "AGAIN"), 0);
    }
    check("an erase: id 7 is gone, its sector free; SET's name \"-\"", smp_pool_drop(7) == 0 && !usr_nsect[7] && !usr_nz[7] &&
          smp_pool_room(7, 1, 99) && str_eq(SMP_ALL_NAMES[SMP_NSETS + 7u], "-") && entries() == 41u, 0);

    /* 5: the old slots, rsv 0 */
    pool_erased();
    make("OLD2", 3, 1000, 1, 0x56);
    memcpy(nor8() + SMP_USER_OFF(1), img, SMP_USER_DATA + ((smp_user_hdr_t *)img)->data_len);   /* USR2's place */
    make("OLD4", 2, 1000, 1, 0x57);
    memcpy(nor8() + SMP_USER_OFF(3), img, SMP_USER_DATA + ((smp_user_hdr_t *)img)->data_len);   /* USR4's (0xE7000) */
    make("STRAY", 1, 1000, 0, 0x58);
    memcpy(nor8() + SMP_SECT_OFF(5), img, SMP_USER_DATA + ((smp_user_hdr_t *)img)->data_len);   /* not an old place */
    make("NEW", 1, 1000, 0, 0x59);
    ((smp_user_hdr_t *)img)->rsv = 1;                                                            /* id 0, by its header */
    memcpy(nor8() + SMP_SECT_OFF(30), img, SMP_USER_DATA + ((smp_user_hdr_t *)img)->data_len);
    smp_pool_scan();
    check("old slots at USR2 / USR4's places: ids 1, 3 (as USR2, USR4 were)", usr_nz[1] == 3u && str_eq(usr_name[1], "OLD2") &&
          usr_nz[3] == 2u && str_eq(usr_name[3], "OLD4") && usr_sect[3] == 60u, 0);
    check("... an entry with its id in rsv: that id (0); an old one elsewhere: the first free (2)",
          str_eq(usr_name[0], "NEW") && usr_sect[0] == 30u && str_eq(usr_name[2], "STRAY") && usr_sect[2] == 5u && entries() == 4u, 0);
    check("... an old project's SET = USR2 plays OLD2", sounds(1, 37) > 300u, 0);
    smp_pool_scan();
    check("... a scan again keeps every id", str_eq(usr_name[0], "NEW") && str_eq(usr_name[1], "OLD2") &&
          str_eq(usr_name[2], "STRAY") && str_eq(usr_name[3], "OLD4"), 0);
    make("OLD1", 1, 1000, 0, 0x5B);
    memcpy(nor8() + SMP_USER_OFF(0), img, SMP_USER_DATA + ((smp_user_hdr_t *)img)->data_len);   /* USR1's place, rsv 0 */
    smp_pool_scan();
    for (k = 0; k < SMP_POOL_N && !(usr_nsect[k] && usr_sect[k] == 0u); k++)
        ;
    check("... an old slot at USR1's place while NEW has id 0 in its header: NEW keeps 0, OLD1 a free id",
          str_eq(usr_name[0], "NEW") && k < SMP_POOL_N && k != 0u && str_eq(usr_name[k], "OLD1"), "OLD1: id %u", (unsigned)k);
    memset(nor8() + SMP_USER_OFF(0), 0xFF, SMP_POOL_SECT);
    smp_pool_scan();
    n = make("BIG", 1, 150000, 0, 0x5A);
    check("an old editor's BEGIN of USR1 (its place, 80 KiB at 0xA0000): over STRAY at sector 5: refused (2)",
          smp_up_begin(0, SMP_USER_SECT(0), SMP_USER_SIZE / SMP_POOL_SECT) == 2u, 0);
    check("... of USR3 (its place free): taken by id 2 (STRAY's sector 5 free again)",
          upload(2, SMP_USER_SECT(2), SMP_USER_SIZE / SMP_POOL_SECT) == 0u && usr_sect[2] == 40u && str_eq(usr_name[2], "BIG") &&
          smp_pool_room(5, 1, 99), 0);

    /* 6: SET's turn; the BREAK's loops in every BR entry */
    check("SET turned: from id 0 on, the ids there (1, 2, 3), then the later built-in sets",
          smp_set_turn(SMP_NSETS, 1) == SMP_NSETS + 1 && smp_set_turn(SMP_NSETS, 3) == SMP_NSETS + 3 &&
          smp_set_turn(SMP_NSETS + 3, 1) == (SMP_NLATE ? SMP_LATE0 : SMP_NSETS + 3) && smp_set_turn(SMP_NSETS + 3, -2) == SMP_NSETS + 1, 0);
    n = make("BRX", 1, 8000, 0, 0x66);              /* (2 sectors each, from 62) */
    for (k = 0, bad = 0; k < 6u; k++)
        bad += upload(20u + k * 3u, 62u + k * 2u, n) != 0u;
    check("SET turned from id 3: id 20, then 23 (the ids between have none)", smp_set_turn(SMP_NSETS + 3, 1) == SMP_NSETS + 20 &&
          smp_set_turn(SMP_NSETS + 3, 2) == SMP_NSETS + 23, 0);
    check("six entries named BR..: six loops for the BREAK after the built-in ones", !bad && brkp_nloops() == BP_NB + 6u,
          "%u", brkp_nloops() - BP_NB);
    {
        uint32_t sl, z;
        check("... the last (BR6) in id 35's zone 0", bp_loop_zone(BP_NB + 5u, &sl, &z) && sl == 35u && z == 0u, 0);
    }

    /* 7: the zone table full */
    pool_erased();
    n = make("FULL", 16, 200, 1, 0x71);
    for (k = 0, bad = 0; k < SMP_POOL_ZONES / 16u; k++)
        bad += upload(k, k, n) != 0u;
    rc = upload(30, 30, n);
    check("24 entries of 16 zones fill the RAM's zone table: the 25th is there with no zones (5)", !bad && rc == 35u &&
          usr_nsect[30] == 1u && !usr_nz[30], "rc %u", (unsigned)rc);
    check("... its place stays taken", smp_up_begin(31, 30, 1) == 2u, 0);
    check("... with one gone, a scan gives it its zones", smp_pool_drop(0) == 0 && usr_nz[30] == 16u, 0);

    printf("pool: %s (%u erases, %u writes)\n", fails ? "FAILED" : "all ok", (unsigned)n_erase, (unsigned)n_write);
    return fails ? 1 : 0;
}
