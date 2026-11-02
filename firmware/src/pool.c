/* SPDX-License-Identifier: GPL-3.0-only */
/* FMX: an upload into the sample pool (the pool itself: eng_sample.c; the commands: editor.c SMP_BEGIN / WRITE / END /
 * ERASE, web/EDITOR_PROTOCOL.md). Entry id k goes into sectors [s, s + n) of the pool:
 *   begin  the place checked (inside one region, clear of every other entry), the entry that had the id before is
 *          gone wherever it was, the new header sector erased: the entry is not there from now on;
 *   write  the data (offset >= 512 inside the place, each further sector erased when a write reaches its start);
 *   end    the header: checked, its id put in (rsv), the data's CRC checked, the header written last, the pool scanned.
 * Main loop only. Needs the flash driver (fl_erase4k_quiet, fl_write, fl_inval) and st_crc32: the host test
 * (tests/pool_test.c) brings its own on a RAM image. */
static struct {
    uint8_t on, id, sect, nsect;                    /* the upload open (begin done, end not yet): write / end may act */
} smp_up;

static void smp_pool_inval(uint32_t s, uint32_t n)  /* (the cache over sectors [s, s + n): the flash under it changed) */
{
    fm1_irq_off();
    fl_inval(SMP_SECT_OFF(s), n * SMP_POOL_SECT);
    fm1_irq_on();
}

/* entry k gone: no voice takes it from here and a sounding one ends (smp_zone), its header sector erased; 0 ok */
static int smp_pool_drop(uint32_t k)
{
    uint32_t s = usr_sect[k], n = usr_nsect[k], took;
    int rc;
    if (smp_up.on && smp_up.id == k)
        smp_up.on = 0;
    if (!n)
        return 0;
    fm1_irq_off();
    usr_nz[k] = 0;
    fm1_irq_on();
    rc = fl_erase4k_quiet(SMP_SECT_OFF(s), &took);
    fm1_wdt_feed();
    smp_pool_inval(s, n);
    smp_pool_scan();
    return rc;
}

/* 0 ok, 1 flash, 2 the place overlaps another entry, 3 no such place (outside the pool, across its two regions) */
static uint32_t smp_up_begin(uint32_t k, uint32_t s, uint32_t n)
{
    uint32_t took;
    smp_up.on = 0;
    if (k >= SMP_POOL_N || !n || s >= SMP_POOL_N || n > SMP_SECT_END(s) - s)
        return 3;
    if (!smp_pool_room(s, n, k))
        return 2;
    if (smp_pool_drop(k) || fl_erase4k_quiet(SMP_SECT_OFF(s), &took))
        return 1;
    fm1_wdt_feed();
    smp_pool_inval(s, n);
    smp_up.on = 1;
    smp_up.id = (uint8_t)k;
    smp_up.sect = (uint8_t)s;
    smp_up.nsect = (uint8_t)n;
    return 0;
}

/* len bytes at offset off of the entry: 0 ok, 1 arguments, 2 erase, 3 write, 4 no upload open for k (begin first) */
static uint32_t smp_up_write(uint32_t k, uint32_t off, const uint8_t *d, uint32_t len)
{
    uint32_t at = SMP_SECT_OFF((uint32_t)smp_up.sect) + off, took;
    if (!smp_up.on || smp_up.id != k)
        return 4;
    if (off < SMP_USER_DATA || (off & 0xFFu) || !len || len > 256u || off + len > smp_up.nsect * SMP_POOL_SECT)
        return 1;
    if (!(off & 0xFFFu) && fl_erase4k_quiet(at, &took))   /* the first write into a sector: erase it */
        return 2;
    return fl_write(at, d, len) ? 3u : 0u;
}

/* the header (n: 480 bytes, or 512 with the SLC1 meta after it; changed here: its id): 0 ok, 1 size, 2 header,
 * 3 data CRC, 4 flash, 5 zones (no room for them, RAM), 6 no upload open for k */
static uint32_t smp_up_end(uint32_t k, uint8_t *hdr, uint32_t n)
{
    smp_user_hdr_t *h = (smp_user_hdr_t *)hdr;
    uint32_t i, s = smp_up.sect, ns = smp_up.nsect;
    if (!smp_up.on || smp_up.id != k)
        return 6;                                   /* (a header over a header: flash ANDs them) */
    if (n != sizeof(smp_user_hdr_t) && n != SMP_USER_DATA)
        return 1;
    if (h->magic != SMP_USER_MAGIC || h->version != 1 || !h->nz || h->nz > 16u ||
        h->data_len > ns * SMP_POOL_SECT - SMP_USER_DATA)
        return 2;
    for (i = 0; i < h->nz; i++)                     /* the zones checked before anything is written */
        if (!smp_zone_ok(&h->zone[i], h->data_len))
            return 2;
    h->rsv = (uint8_t)(k + 1u);                     /* its id, wherever it is */
    smp_up.on = 0;
    smp_pool_inval(s, ns);
    if (st_crc32(SMP_POOL_XIP(SMP_SECT_OFF(s)) + SMP_USER_DATA, h->data_len) != h->crc)
        return 3;
    if (fl_write(SMP_SECT_OFF(s), hdr, n))
        return 4;
    smp_pool_inval(s, ns);
    smp_pool_scan();
    return usr_nz[k] ? 0u : 5u;
}
