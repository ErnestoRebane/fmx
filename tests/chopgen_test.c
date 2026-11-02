/* SPDX-License-Identifier: GPL-3.0-only */
/* Host test of the CHOP track (firmware/src/chopgen.c), on a slot cut by the loop page (16 slices of a 4-bar loop in A
 * minor, made here: a RAM image of USR1 with its cs_meta_t):
 *   SLICE    STRAIGHT: slice k on trigger k from PLAY; every STYLE swaps a slice only for one of its own bar; the
 *            BREAK's fill bar straight, the rolls only in the bar before it; HALF a trigger every two slices, an
 *            octave down; a white key held: its slice on every trigger; a recorded slice: its step plays it
 *   the keys GAP silence, STUTTER every 16th, DOWN an octave (INST: the notes)
 *   TUNE     KEY the nearest way (Am -> Dm: +5), SNAP, a slice with no note shifted only, FOLLOW with the chords,
 *            no key / OFF / PADS: none; the voice plays faster by as much
 *   the project  the settings kept by a save and a load
 * With a file (web/test_slice.mjs's slot image): that slot read as the page meant it, and played. */
#include <stdint.h>
#define main hostsim_main
#include <stdarg.h>
#include "hostsim.c"
#undef main
#define PROJ_HOST 1
#include "../firmware/src/project.c"

static int fails;
static void check(const char *what, int ok, const char *fmt, ...)
{
    printf("chop: %-76s %s", what, ok ? "ok" : "FAIL");
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
static int32_t o[2 * CTL];
#define BASE 36u
/* USR1: 16 zones of 600 samples (a soft ADPCM buzz), keys 36..51, and the loop page's meta (4 bars) */
static void make_slot(uint8_t key, const uint8_t *notes)
{
    uint8_t *u = host_slot(0);
    smp_user_hdr_t *h = (smp_user_hdr_t *)u;
    cs_meta_t *m = (cs_meta_t *)(u + CS_META_AT);
    uint32_t i;
    memset(u, 0, SMP_USER_SIZE);
    h->magic = SMP_USER_MAGIC;
    h->version = 1;
    h->nz = 16;
    memcpy(h->name, "VOX", 4);
    for (i = 0; i < 16u; i++) {
        smp_zone_t *z = &h->zone[i];
        z->off = i * 300u;
        z->n = 600;
        z->ls = 0;
        z->le = 599;
        z->rate = 1u << 15;                             /* 22.05 kHz */
        z->root16 = (int16_t)((BASE + i) * 16u);
        z->lo = z->hi = (uint8_t)(BASE + i);
    }
    h->data_len = 16u * 300u;
    for (i = 0; i < h->data_len; i++)
        u[SMP_USER_DATA + i] = (uint8_t)((i & 1u) ? 0x3B : 0xB3);
    m->magic = CS_META_MAGIC;
    m->key = key;
    m->bars = 4;                                      /* (16 slices of 4 bars: a beat each) */
    m->n = 16;
    for (i = 0; i < 16u; i++)
        m->note[i] = notes ? notes[i] : CS_NOKEY;
    smp_pool_scan();
}
/* USR2's place: INST's melody bank (the companion app's "MEL1" entry) of two themes, both written in C major: the
 * first with an F# on a beat (off the scale) and a held D (not a chord tone of F minor's i), the second of 3 notes */
static const uint8_t BANK_A[][4] = {{0, 4, 60, 100}, {4, 2, 62, 90}, {6, 2, 64, 90}, {8, 4, 66, 100}, {12, 4, 67, 100},
                                    {16, 8, 62, 100}, {24, 4, 64, 90}, {28, 4, 72, 100}, {64, 16, 60, 100}};
static void make_bank(int on)
{
    uint8_t *u = host_slot(1), *d = u + SMP_USER_DATA;
    smp_user_hdr_t *h = (smp_user_hdr_t *)u;
    cm_bank_t *m = (cm_bank_t *)(u + CS_META_AT);
    uint32_t na = sizeof BANK_A / 4u, o = 4u + 2u * 2u, i;
    memset(u, 0xFF, SMP_USER_SIZE);
    if (on) {
        memset(u, 0, SMP_USER_DATA + 256u);
        h->magic = SMP_USER_MAGIC;
        h->version = 1;
        h->nz = 1;
        memcpy(h->name, "MELODIES", 8);
        h->zone[0].n = 2;                               /* (the silent zone the entry needs) */
        h->zone[0].le = 1;
        h->zone[0].rate = 1u << 16;
        m->magic = SMP_MEL_MAGIC;
        m->n = 2;
        d[4] = (uint8_t)o;
        memcpy(d + o, "THEME A", 8);
        d[o + 8] = 0;                                   /* (C major) */
        d[o + 9] = (uint8_t)na;
        memcpy(d + o + CM_REC, BANK_A, sizeof BANK_A);
        o += CM_REC + 4u * na;
        d[6] = (uint8_t)o;
        memcpy(d + o, "THEME B", 8);
        d[o + 8] = 0;
        d[o + 9] = 3;
        for (i = 0; i < 3u; i++) {
            d[o + CM_REC + 4u * i] = (uint8_t)(i * 8u);
            d[o + CM_REC + 4u * i + 1u] = 8;
            d[o + CM_REC + 4u * i + 2u] = (uint8_t)(72u - i * 3u);
            d[o + CM_REC + 4u * i + 3u] = 100;
        }
        h->data_len = o + CM_REC + 12u;
    }
    smp_pool_scan();
}
static void fresh(void)
{
    host_tracks_init();
    host_preset(&trk[CS_TRK], ENGI_SAMPLE, 5);
    trk[CS_TRK].p[P_E0] = (int16_t)SMP_NSETS;           /* KIT: USR1 */
    trk[CS_TRK].p[P_E1] = 0;
    trk[CS_TRK].p[P_REL] = 120;
    trk[CS_TRK].p[P_VOICE] = V_MONO;
    trk[CS_TRK].p[P_X0] = CM_SLICE;
    trk[CS_TRK].p[P_X1] = CY_STRAIGHT;
    trk[CS_TRK].p[P_X2] = 0;
    trk[CS_TRK].p[P_X3] = 50;
    trk[CS_TRK].p[P_X5] = CT_OFF;
    memset(trk[CS_TRK].step, 0, sizeof trk[CS_TRK].step);
    trk[0].p[P_ROOT] = trk[1].p[P_ROOT] = 9;            /* the session: A minor */
    trk[0].p[P_SCALE] = trk[1].p[P_SCALE] = 2;
    q_mode = QM_NOW;
    csr.fx = 0;
    cs_hand = 0;
}
static void play_from_start(void)
{
    transport_req = 2;
    mix_block(o, CTL);
    mix_block(o, CTL);
    seq_reset_tracks(0);
    transport_req = 1;                                  /* (the first trigger: in the trace after) */
}
/* the notes started (csr.hits), as played: the 1/32 from PLAY, the slice, where the break was (csr.bb) */
static struct { uint32_t p32, sl, bb, note; } lg[4096];
static uint32_t nlog;
static void trace(uint32_t n32)
{
    uint32_t guard = 2000000u, h = csr.hits;
    nlog = 0;
    while (guard-- && (!csr.at || csr.at - 1u < n32)) {
        mix_block(o, CTL);
        if (csr.hits != h && csr.at && nlog < 4096u) {
            lg[nlog].p32 = csr.at - 1u;
            lg[nlog].sl = (csr.hit - BASE) & 15u;
            lg[nlog].bb = csr.bb;
            lg[nlog].note = csr.hit;
            nlog++;
        }
        h = csr.hits;
    }
}
static uint32_t notes_at(uint32_t i) { return trk[CS_TRK].step[i].time == ST_NOTE && trk[CS_TRK].step[i].n; }
/* a slot image the loop page made (web/test_slice.mjs: an A minor tune, 8 slices of 2 bars; its 512-byte header, then
 * the data) in USR1: the meta and the kit read as the page meant them, TUNE from its notes, the phrase plays them */
static int web_image(const char *path)
{
    static const uint8_t AM[8] = {57, 60, 64, 62, 57, 55, 53, 52};
    FILE *f = fopen(path, "rb");
    uint32_t m = 0, v = 0, i, ok, hits = 0, b;
    size_t n;
    memset(host_slot(0), 0xFF, SMP_USER_SIZE);
    n = f ? fread(host_slot(0), 1, SMP_USER_SIZE, f) : 0;
    if (f)
        fclose(f);
    smp_pool_scan();
    fresh();
    check("web image: read", n > SMP_USER_DATA && usr_nz[0] == 8u, "%u B, %u zones", (unsigned)n, usr_nz[0]);
    check("... SLC1: A minor, 2 bars, 8 slices, the notes", cs_meta() && cs_meta()->key == (9u | 0x10u) &&
          cs_meta()->bars == 2u && !memcmp(cs_meta()->note, AM, 8) && cs_meta()->note[8] == CS_NOKEY, 0);
    check("... a kit of 8 from key 36; a slice a quarter", cs_kit(&m, &v) && m == BASE && v == 8u && cs_sl16(8) == 4u,
          "%u %u %u", m, v, cs_sl16(8));
    trk[0].p[P_ROOT] = trk[1].p[P_ROOT] = 2;            /* D minor: +5, every note of it on the scale */
    CSV(5) = CT_SNAP;
    cs_poll();
    for (i = 0, ok = cs_tune_on; i < 8u; i++)
        ok &= cs_tune16[i] == 5 * 16;
    check("... TUNE SNAP to D minor: every slice +5", ok, "%d", cs_tune16[0]);
    play_from_start();
    trace(64u);                                         /* two bars: 8 slices, a quarter each, STRAIGHT */
    for (i = 0, ok = nlog >= 8u; i < 8u && i < nlog; i++)
        ok &= lg[i].sl == i && lg[i].p32 == i * 8u;
    for (b = 0; b < 200u; b++) {
        mix_block(o, CTL);
        for (i = 0; i < NVOICE; i++)
            hits += trk[CS_TRK].v[i].active && trk[CS_TRK].v[i].note >= BASE && trk[CS_TRK].v[i].note < BASE + 8u;
    }
    check("... STRAIGHT: A..H, one a beat, and they sound", ok && hits > 0, "%u voice-blocks", hits);
    printf(fails ? "chop: %d FAILED\n" : "chop: all ok\n", fails);
    return fails != 0;
}

/* the melody bank the companion app made (web/test_melody.mjs: the pack's n tunes) in USR2's place: n themes, no SET of
 * its own, each theme as many notes as the bank holds, in the song's scale (F minor), the file's rhythm */
static int web_bank(const char *path, uint32_t want)
{
    FILE *f = fopen(path, "rb");
    uint32_t th, bad = 0, notes = 0;
    size_t n;
    memset(host_slot(1), 0xFF, SMP_USER_SIZE);
    n = f ? fread(host_slot(1), 1, SMP_USER_SIZE, f) : 0;
    if (f)
        fclose(f);
    smp_pool_scan();
    fresh();
    host_preset(&trk[CS_TRK], ENGI_ANALOG, 0);
    CSV(0) = CM_INST;
    CSV(4) = CML_MIDI;
    trk[0].p[P_ROOT] = trk[1].p[P_ROOT] = 5;
    cg_defaults();
    cg_poll();
    check("web bank: read, its themes, no SET of its own", n > SMP_USER_DATA && cm_themes() == want &&
          !strcmp(SMP_ALL_NAMES[SMP_NSETS + 1u], "-"), "%u B, %u themes", (unsigned)n, cm_themes());
    for (th = 0; th < want; th++) {
        const uint8_t *r = cm_theme_rec(th);
        uint32_t k;
        CSV(6) = (int16_t)th;
        cs_poll();
        bad += !r || cm_n[cm_b] != r[9];
        for (k = 0; r && k < cm_n[cm_b]; k++) {
            const cm_ev_t *e = &cm_ev[cm_b][k];
            bad += e->at != r[CM_REC + 4u * k] || e->len != r[CM_REC + 4u * k + 1u] || !cm_scale(e->note);
            notes++;
        }
    }
    check("... every theme: the bank's notes, its rhythm, in F minor", !bad && notes, "%u bad of %u notes", bad, notes);
    return fails ? 1 : 0;
}

int main(int argc, char **argv)
{
    static const uint8_t NOTES[16] = {69, 72, 76, 74, 68, 0xFF, 69, 71, 72, 74, 76, 77, 79, 81, 61, 64};
    uint32_t i, m, v, ok, st, bad;

    if (argc > 3 && !strcmp(argv[1], "--mel"))
        return web_bank(argv[2], (uint32_t)atoi(argv[3]));
    if (argc > 1)
        return web_image(argv[1]);
    make_slot(9u | 0x10u, NOTES);                       /* A minor, 4 bars */
    fresh();
    check("USR1: 16 slices, the meta read, a kit; a slice a beat", usr_nz[0] == 16u && cs_meta() && cs_keyed(cs_meta()) &&
          cs_kit(&m, &v) && m == BASE && v == 16u && cs_sl16(16) == 4u, 0);
    {   /* a kit: every zone on one key; a slot of break loops or an instrument (a zone over keys) is not one */
        smp_user_hdr_t *h = (smp_user_hdr_t *)host_slot(0);
        uint32_t b2, n2;
        h->zone[3].hi = (uint8_t)(h->zone[3].lo + 4u);
        smp_pool_scan();
        check("a slot with a zone over several keys (break loops, an instrument): not a kit", !cs_kit(&b2, &n2), 0);
        h->zone[3].hi = h->zone[3].lo;
        smp_pool_scan();
        check("... every zone on its key again: a kit", cs_kit(&b2, &n2) && n2 == 16u, 0);
    }

    /* ---- SLICE */
    play_from_start();
    trace(8u * 32u);
    for (i = 0, ok = nlog >= 32u; i < nlog && i < 32u; i++)
        ok &= lg[i].p32 == i * 8u && lg[i].sl == i % 16u;
    check("STRAIGHT: slice k on trigger k from PLAY, all 16, twice round", ok, "%u started", nlog);
    for (st = CY_HOLD, bad = 0; st < CY_N; st++) {
        uint32_t moved = 0, fills = 0, rolls_off = 0;
        if (st == CY_HALF)
            continue;
        fresh();
        CSV(1) = (int16_t)st;
        CSV(2) = 100;
        CSV(3) = 100;
        play_from_start();
        trace(32u * 32u);
        for (i = 0; i < nlog; i++) {
            uint32_t pos = lg[i].p32 / 8u % 16u, on_trig = !(lg[i].p32 % 8u);
            bad += lg[i].sl / 4u != pos / 4u;             /* (a slice of the bar it is in: the same chord) */
            moved += on_trig && lg[i].sl != pos;
            if (lg[i].bb == 2u && on_trig)
                fills += lg[i].sl != pos;                 /* (the break's fill bar: straight) */
            if (!on_trig && lg[i].bb != 1u)
                rolls_off++;                              /* (a retrigger out of the loop's own bar) */
        }
        bad += fills + rolls_off;
        printf("chop:   %-8s %u notes, %u moved, fills off %u, rolls out of its bar %u\n", N_CS_STYLE[st], nlog, moved,
               fills, rolls_off);
        bad += !moved;
    }
    check("every STYLE moves a slice only inside its bar; the break's fill straight; rolls in the loop's bar only", !bad, "%u", bad);
    fresh();
    CSV(1) = CY_HALF;
    play_from_start();
    trace(8u * 32u);
    for (i = 0, ok = nlog >= 16u; i < nlog && i < 16u; i++)
        ok &= lg[i].p32 == i * 16u && lg[i].sl == i;
    check("HALF: a trigger every two slices, the loop in order, an octave down", ok && cs_dn16 == -12 * 16, "%u %d", nlog, cs_dn16);
    fresh();
    CSV(1) = CY_CHOP;
    CSV(2) = 100;
    cs_hand = 1;
    csr.held = (uint8_t)(BASE + 6u);
    play_from_start();
    trace(4u * 32u);
    for (i = 0, ok = nlog >= 16u; i < nlog; i++)
        ok &= lg[i].sl == 6u || (lg[i].p32 % 8u);
    cs_hand = 0;
    check("a white key held: its slice on every trigger", ok, "%u", nlog);
    fresh();
    trk[CS_TRK].step[4].time = ST_NOTE;                  /* a recorded slice: J on step 5 */
    trk[CS_TRK].step[4].n = 1;
    trk[CS_TRK].step[4].note[0] = (uint8_t)(BASE + 9u);
    trk[CS_TRK].step[4].vel = 100;
    trk[CS_TRK].p[P_SLEN] = 16;
    play_from_start();
    trace(32u);
    for (i = 0, ok = 0, v = 0; i < nlog; i++) {
        ok += lg[i].p32 == 8u && lg[i].sl == 9u;
        v += lg[i].p32 == 8u && lg[i].sl == 1u;
    }
    check("a recorded slice plays in place of the generator's on its step", ok == 1u && !v, "%u %u", ok, v);
    {   /* a slice still sounding at the next trigger (the song faster than the loop): MONO restarts the voice, and
         * from its start at the level it had the waveform jumped (a click at every seam). Its tail goes to a free
         * voice and fades out (voice.c voice_start), the next slice starts from silence */
        track_t *t = &trk[CS_TRK];
        uint32_t k, tails = 0, left = 0, was;
        fresh();
        trk_note_on(t, BASE, 100);
        for (k = 0; k < 8u; k++)
            mix_block(o, CTL);
        was = t->v[0].active && t->v[0].stage != 0u && t->v[0].env > 0;
        trk_note_on(t, BASE + 1u, 100);
        for (k = 1; k < NVOICE; k++)
            tails += t->v[k].active && t->v[k].stage == 4u && !t->v[k].gate && t->v[k].note == BASE;
        ok = was && tails == 1u && t->v[0].note == BASE + 1u && t->v[0].gate && t->v[0].env == 0;
        for (k = 0; k < KILL_BLOCKS + 2u; k++)
            mix_block(o, CTL);
        for (k = 1; k < NVOICE; k++)
            left += t->v[k].active;
        trk_all_off(t);
        check("a slice restarted while it sounds: its tail fades on a free voice, the next from silence", ok && !left,
              "tails %u, env %d, left %u", tails, (int)t->v[0].env, left);
    }

    /* ---- the keys held */
    fresh();
    play_from_start();
    trace(4u * 8u);
    csr.fx = CF_GAP;
    trace(6u * 8u);
    check("GAP held: silence", !nlog && csr.sl == 0xFFu, "%u", nlog);
    csr.fx = 0;
    trace(7u * 8u);
    check("... let go: the loop where it is (beat 8: H)", nlog && lg[0].sl == 7u, "%u", nlog ? lg[0].sl : 99u);
    csr.fx = CF_STUT;
    trace(9u * 8u);
    check("STUTTER held: what sounds again every 16th (8 in two beats)", nlog >= 7u && nlog <= 9u, "%u", nlog);
    csr.fx = CF_DOWN;
    mix_block(o, CTL);
    mix_block(o, CTL);
    check("DOWN held (SLICE): the tape an octave down", cs_dn16 == -12 * 16, "%d", cs_dn16);
    csr.fx = 0;
    mix_block(o, CTL);
    check("... let go: back", cs_dn16 == 0, 0);
    {
        step_t cp, s0;
        const step_t *s;
        memset(&s0, 0, sizeof s0);
        s0.time = ST_NOTE;
        s0.n = 1;
        s0.note[0] = 72;
        CSV(0) = CM_INST;
        csr.fx = CF_DOWN;
        s = cs_step(&trk[CS_TRK], &s0, &cp);
        check("INST DOWN held: the notes an octave down", s == &cp && cp.note[0] == 60u, 0);
        csr.fx = CF_GAP;
        check("GAP held: the step silent", !cs_step(&trk[CS_TRK], &s0, &cp), 0);
        csr.fx = 0;
        CSV(0) = CM_SLICE;
    }

    /* ---- TUNE */
    fresh();
    CSV(5) = CT_KEY;
    trk[0].p[P_ROOT] = trk[1].p[P_ROOT] = 2;            /* D minor: +5 */
    cs_poll();
    for (i = 0, ok = cs_tune_on; i < 16u; i++)
        ok &= cs_tune16[i] == 5 * 16;
    check("KEY: Am -> Dm, every slice +5", ok, "%d", cs_tune16[0]);
    trk[0].p[P_ROOT] = trk[1].p[P_ROOT] = 4;            /* E minor: the nearest way is up 7 = down 5 */
    cs_poll();
    check("KEY: Am -> Em, the nearest way (-5)", cs_tune16[0] == -5 * 16, "%d", cs_tune16[0]);
    trk[0].p[P_ROOT] = trk[1].p[P_ROOT] = 2;
    CSV(5) = CT_SNAP;
    cs_poll();
    check("SNAP: slice 15 (C#, + 5 = F#, not D minor's) to F (-1)", cs_tune16[14] == 4 * 16, "%d", cs_tune16[14]);
    check("SNAP: a slice on the scale stays at +5; one with no note too", cs_tune16[0] == 80 && cs_tune16[5] == 80,
          "%d %d", cs_tune16[0], cs_tune16[5]);
    CSV(5) = CT_FOLLOW;
    cg.play = CP_OFF;
    cs_poll();
    check("FOLLOW, no chords: the key's shift", cs_tune16[0] == 80, 0);
    CSV(5) = CT_OFF;
    cs_poll();
    check("TUNE OFF: none", !cs_tune_on, 0);
    CSV(5) = CT_KEY;
    CSV(0) = CM_PADS;
    cs_poll();
    check("PADS: none", !cs_tune_on, 0);
    CSV(0) = CM_SLICE;
    {   /* the voice plays faster by +5 */
        uint32_t adv0, adv1, k;
        voice_t *vv;
        transport_req = 2;
        mix_block(o, CTL);
        cs_tune_on = 0;
        trk_note_on(&trk[CS_TRK], BASE, 100);
        for (k = 0; k < 4u; k++)
            mix_block(o, CTL);
        for (k = 0; k < NVOICE && !(trk[CS_TRK].v[k].active && trk[CS_TRK].v[k].note == BASE); k++)
            ;
        vv = &trk[CS_TRK].v[k % NVOICE];
        adv0 = vv->ph[0];
        trk_note_off(&trk[CS_TRK], BASE);
        panic_req = 1u << CS_TRK;
        mix_block(o, CTL);
        cs_poll();
        trk_note_on(&trk[CS_TRK], BASE, 100);
        for (k = 0; k < 4u; k++)
            mix_block(o, CTL);
        for (k = 0; k < NVOICE && !(trk[CS_TRK].v[k].active && trk[CS_TRK].v[k].note == BASE); k++)
            ;
        vv = &trk[CS_TRK].v[k % NVOICE];
        adv1 = vv->ph[0];
        check("the slice plays faster by +5 semitones (x1.33)", adv0 && adv1 * 100u > adv0 * 125u &&
              adv1 * 100u < adv0 * 142u, "%u -> %u samples", adv0, adv1);
    }
    make_slot(CS_NOKEY, 0);                             /* a perc loop: no key */
    cs_poll();
    check("no key (a perc loop): never tuned, whatever TUNE says", !cs_tune_on, 0);

    /* ---- INST's melody */
    {
        static cm_ev_t a[CM_NEV];
        uint32_t na, mel, bad = 0, same, k;
        fresh();
        host_preset(&trk[CS_TRK], ENGI_ANALOG, 0);
        CSV(0) = CM_INST;
        trk[0].p[P_ROOT] = trk[1].p[P_ROOT] = 5;        /* F minor */
        cg_defaults();
        cg.play = CP_HOLD;
        cg_poll();
        CSV(4) = CML_OFF;
        cs_poll();
        check("MELODY OFF: no theme", cm_n[cm_b] == 0u, "%u", cm_n[cm_b]);
        for (mel = CML_ARP; mel < CML_N; mel++) {
            const cm_ev_t *e;
            uint32_t n, beats = 0, ctones = 0;
            CSV(4) = (int16_t)mel;
            cs_poll();
            e = cm_ev[cm_b];
            n = cm_n[cm_b];
            bad += n < 8u;
            for (k = 0; k < n; k++) {
                int32_t lo = mel == CML_ARP ? 60 : CM_LO, hi = mel == CML_ARP ? 98 : CM_HI;   /* (ARP: its ladder, 2 octaves) */
                bad += (!cm_scale(e[k].note) && !cm_ctone(e[k].note, e[k].at, 0)) || e[k].note < lo || e[k].note > hi ||
                       e[k].at >= CM_BARS * 16u;        /* (in the key, or a borrowed chord's tone) */
                if (!(e[k].at % 4u)) {
                    beats++;
                    ctones += (uint32_t)cm_ctone(e[k].note, e[k].at, 0);
                }
            }
            bad += ctones * 10u < beats * 8u;           /* (most of the beats on a chord tone) */
            if (mel != CML_ARP)
                bad += n && (e[n - 1u].note - cg_root()) % 12u != 0u;   /* (SING: the cadence on the tonic) */
            printf("chop:   %-6s %u notes, %u of %u beats on a chord tone\n", N_CS_MEL[mel], n, ctones, beats);
        }
        check("ARP, SING: 8 bars in range, in the key (or its chord), chord tones on the beats, the tonic at the end",
              !bad, "%u", bad);
        CSV(4) = CML_MIDI;                              /* MIDI: the user's themes */
        make_bank(0);
        cs_poll();
        check("MIDI, no bank sent: no theme, THEME has none", cm_n[cm_b] == 0u && cm_themes() == 0u, "%u", cm_n[cm_b]);
        make_bank(1);
        cs_poll();
        {
            const cm_ev_t *e = cm_ev[cm_b];
            uint32_t n = cm_n[cm_b], na2 = sizeof BANK_A / 4u, off = 0, strong = 0, ctones = 0;
            bad = n != na2 || cm_themes() != 2u || strcmp(SMP_ALL_NAMES[SMP_NSETS + 1u], "-");
            for (k = 0; !bad && k < n; k++) {
                int32_t want = (int32_t)BANK_A[k][2] + 5;   /* (C to F: up a fourth, the nearest way) */
                bad += e[k].at != BANK_A[k][0] || e[k].len != BANK_A[k][1] || !cm_scale(e[k].note) ||
                       e[k].note < CM_LO - 7 || e[k].note > CM_HI + 7;
                if ((!(e[k].at % 8u) && e[k].len >= 4u) || e[k].len >= 8u) {
                    strong++;
                    ctones += (uint32_t)cm_ctone(e[k].note, e[k].at, 0);
                }
                if (k)                                  /* (the line's way: up, down or the same, as the file's) */
                    bad += (BANK_A[k][2] > BANK_A[k - 1u][2]) - (BANK_A[k][2] < BANK_A[k - 1u][2]) !=
                           (e[k].note > e[k - 1u].note) - (e[k].note < e[k - 1u].note);
                off += abs((int32_t)e[k].note % 12 - (want % 12 + 12) % 12) % 11 > 1;
                printf("chop:   MIDI %3u %2u  %3d -> %3u%s\n", e[k].at, e[k].len, want, e[k].note,
                       cm_ctone(e[k].note, e[k].at, 0) ? " (chord)" : "");
            }
            check("MIDI: the file's rhythm and line (up, down, the same) in F minor's scale", !bad, "%u", bad);
            check("MIDI: the strong held notes on the chord, most", ctones * 3u >= strong * 2u, "%u of %u", ctones, strong);
            check("MIDI: the file's notes a fourth up (a semitone for the mode; few moved more)", off * 4u <= n,
                  "%u of %u", off, n);
            CSV(6) = 1;
            cs_poll();
            check("MIDI THEME 2: the bank's second", cm_n[cm_b] == 3u, "%u", cm_n[cm_b]);
            CSV(6) = 2;
            cs_poll();
            check("MIDI THEME 3 of 2: the first again", cm_n[cm_b] == na2, "%u", cm_n[cm_b]);
            CSV(6) = 0;
            make_bank(0);
        }
        CSV(4) = CML_SING;
        cs_poll();
        na = cm_n[cm_b];
        memcpy(a, cm_ev[cm_b], sizeof a);
        trk[0].p[P_ROOT] = trk[1].p[P_ROOT] = 9;        /* another song: A minor, another progression */
        cg.var = 95;
        cg.num = 3;
        cg_poll();
        cs_poll();
        for (k = 0, same = cm_n[cm_b] == na; same && k < na; k++)   /* (the same rhythm and the same intervals: a
                                                         * transposition) */
            same = cm_ev[cm_b][k].at == a[k].at && (k == 0u || cm_ev[cm_b][k].note - cm_ev[cm_b][k - 1u].note ==
                                                              a[k].note - a[k - 1u].note);
        check("another song (key, VARIANT): another theme, not the same one transposed", !same, 0);
        memcpy(a, cm_ev[cm_b], sizeof a);
        na = cm_n[cm_b];
        CSV(6) = 1;
        cs_poll();
        check("THEME 2: another theme of the same song", cm_n[cm_b] != na || memcmp(a, cm_ev[cm_b], na * sizeof a[0]), 0);
        CSV(6) = 0;
        cs_poll();
        play_from_start();
        trace(8u * 32u);
        check("INST playing: the theme sounds", nlog >= 8u, "%u notes", nlog);
        {                                               /* TIME: INST's RATE and OCT (the MIDI bank's first theme) */
            uint32_t at[3], nt0;
            CSV(4) = CML_MIDI;
            make_bank(1);
            CSV(7) = 0;
            cs_poll();
            play_from_start();
            trace(40u);
            nt0 = lg[0].note;
            for (k = 0; k < 3u; k++)
                at[k] = nlog > k ? lg[k].p32 : 999u;
            check("TIME, an older project: INST at 1/16, the theme's 16ths (0, 4, 6)", at[0] == 0u && at[1] == 8u && at[2] == 12u,
                  "%u %u %u", at[0], at[1], at[2]);
            cs_rate_step(CM_INST, -1);
            cs_poll();
            play_from_start();
            trace(60u);
            for (k = 0; k < 3u; k++)
                at[k] = nlog > k ? lg[k].p32 : 999u;
            check("INST RATE 1/8: the theme at half speed", cs_rate(CM_INST) == CR_8 && at[0] == 0u && at[1] == 16u &&
                  at[2] == 24u && trk[CS_TRK].p[P_SDIV] == 1, "%u %u %u", at[0], at[1], at[2]);
            cs_rate_step(CM_INST, 2);
            cs_oct_set(1);
            cs_poll();
            play_from_start();
            trace(20u);
            for (k = 0; k < 3u; k++)
                at[k] = nlog > k ? lg[k].p32 : 999u;
            check("INST RATE 1/32, OCT +1: double speed, an octave up", cs_rate(CM_INST) == CR_32 && at[1] == 4u &&
                  at[2] == 6u && nlog && lg[0].note == nt0 + 12u && cs_oct() == 1, "%u %u, %u -> %u", at[1], at[2], nt0, lg[0].note);
            cs_oct_set(-5);
            check("OCT stops at -2", cs_oct() == -2, "%d", cs_oct());
            check("... the other modes' RATE untouched", cs_rate(CM_PADS) == CR_16 && cs_rate(CM_SLICE) == CR_16, 0);
            CSV(7) = 0;
            make_bank(0);
            transport_req = 2;
            mix_block(o, CTL);
        }
        transport_req = 2;
        mix_block(o, CTL);
    }

    /* ---- TIME: SLICE's RATE (the loop's speed), PADS's (the steps' grid), each its own */
    fresh();
    CSV(1) = CY_STRAIGHT;
    CSV(2) = 0;
    cs_poll();
    play_from_start();
    trace(40u);
    {
        uint32_t d16 = nlog > 1u ? lg[1].p32 - lg[0].p32 : 0u, d8;
        cs_rate_step(CM_SLICE, -1);
        cs_poll();
        play_from_start();
        trace(80u);
        d8 = nlog > 1u ? lg[1].p32 - lg[0].p32 : 0u;
        check("SLICE RATE 1/8: a slice every two beats, not one (STRAIGHT)", d16 == 8u && d8 == 16u && lg[1].sl == 1u,
              "%u -> %u", d16, d8);
        cs_rate_step(CM_PADS, 1);
        cs_poll();
        check("... the steps' DIV is SLICE's (1/8)", trk[CS_TRK].p[P_SDIV] == 1, "%d", trk[CS_TRK].p[P_SDIV]);
        CSV(0) = CM_PADS;
        cs_poll();
        check("PADS RATE 1/32: the steps' DIV 1/32, SLICE keeps its own", trk[CS_TRK].p[P_SDIV] == 3 &&
              cs_rate(CM_SLICE) == CR_8, "%d", trk[CS_TRK].p[P_SDIV]);
        transport_req = 2;
        mix_block(o, CTL);
    }

    /* ---- the project */
    fresh();
    CSV(0) = CM_SLICE;
    CSV(1) = CY_SKIP;
    CSV(2) = 77;
    CSV(3) = 33;
    CSV(5) = CT_FOLLOW;
    {
        static project_t pj;
        int16_t keep[8];
        memcpy(keep, &trk[CS_TRK].p[P_X0], sizeof keep);
        proj_capture(&pj);
        host_tracks_init();
        proj_apply(&pj, 1);
        check("a save and a load keep the CHOP's settings", !memcmp(keep, &trk[CS_TRK].p[P_X0], sizeof keep), 0);
    }

    printf(fails ? "chop: %d FAILED\n" : "chop: all ok\n", fails);
    return fails != 0;
}
