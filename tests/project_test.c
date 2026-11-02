/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Host test of the project format (firmware/src/project.c, -DPROJ_HOST part). FMX writes "SPB4" and reads it and SPB3:
 * a stored image comes back byte for byte (proj_import), the working project goes through proj_capture / proj_apply
 * (the parts' steps, the BREAK track's bstep_t steps and its generator in p[], nudges, locks, fill conditions,
 * globals, the CHORDS generator, the BASS track's phrase generator), every value back inside its range on apply (a part's EDIT by its engine, the BREAK track's generator by
 * brk_desc, its steps' fields, the nudges, the locks); SLOOP's FUN1..FUN5 images and damaged ones are refused.
 * Run by tests/run_tests.sh (needs build/gen). */
#define FELUCCA_ARRANGER 1                    /* (the song's cards and FORM live in arranger.c)
 */
#define main hostsim_main
#include <stddef.h>
#include "hostsim.c"
#undef main
#define PROJ_HOST 1
static void arrangement_apply(uint32_t scene) { (void)scene; }   /* (the parts of project.c the format does not need: the */
static void as_block(void) {}                                     /*  AUTO SONG's moves and the song's backup) */
static void as_swap_now(void) {}
static void as_tr_bar(void) {}
static void song_backup(void) {}
static void song_restore(void) {}
static void song_keep_p(uint32_t k, uint32_t id, int32_t v) { (void)k, (void)id, (void)v; }
static void song_keep_turn(uint32_t k, uint32_t id, int32_t d, int32_t lo, int32_t hi) { (void)k, (void)id, (void)d, (void)lo, (void)hi; }
static void song_keep_eng(uint32_t k, uint32_t e, uint32_t pre) { (void)k, (void)e, (void)pre; }
static void song_keep_trk(uint32_t k, int steps) { (void)k, (void)steps; }
#include "../firmware/src/project.c"

static int check(const char *what, int ok)
{
    printf("%-66s %s\n", what, ok ? "ok" : "FAIL");
    return ok ? 0 : 1;
}

/* a blob of n bytes as SLOOP stored its projects: magic, size, then anything, the FNV-1a of all but the last word */
static void old_blob(uint8_t *b, uint32_t n, uint32_t magic)
{
    uint32_t i, s;
    for (i = 0; i < n; i++)
        b[i] = (uint8_t)(i * 7u + 3u);
    memcpy(b, &magic, 4);
    memcpy(b + 4, &n, 4);
    s = proj_hash(b, n - 4u);
    memcpy(b + n - 4u, &s, 4);
}

int main(void)
{
    static project_t q, q2;
    static uint8_t buf[4096];
    uint32_t t, i;
    int bad = 0, ok;

    bad += check("SPB4: the magic, one flash object", PROJ_MAGIC == 0x53504234u && !memcmp(&(uint32_t){PROJ_MAGIC}, "4BPS", 4) &&
                                                         sizeof(project_t) <= 3840u);   /* (storage.c ST_PAYLOAD_MAX) */

    /* the working project: parts, the BREAK track (steps and generator), nudges, locks, conditions, globals */
    host_tracks_init();
    for (t = 0; t < NTRK; t++)
        trk[t].p[P_SLEN] = (int16_t)(5 + t);
    trk[0].eng_req = ENGI_ANALOG, trk[1].eng_req = ENGI_SAMPLE, trk[2].eng_req = ENGI_SAMPLE;
    trk[1].preset = 3;
    trk[1].step[2].n = 2, trk[1].step[2].note[0] = 60, trk[1].step[2].note[1] = 64, trk[1].step[2].time = ST_NOTE;
    trk[1].step[2].lvl = 0x0D;
    trk[1].step[2].rat = 0x06;
    TBRK->bstep[9] = (bstep_t){.on = 1, .slice = 6, .lvl = LV_SOFT, .rat = 3, .pitch = -7, .flags = BS_REV};
    TBRK->bstep[10].on = 0;
    TBRK->p[P_E0] = 77;                            /* CMPLX */
    TBRK->p[P_ATK] = 3;                            /* PHRSE 8 */
    TBRK->p[P_DEC] = 40;                           /* B CHN */
    TBRK->p[P_ED_PIT] = -5;                        /* PTCH */
    TBRK->p[P_LWAVE] = 2;                          /* LOOP B: AMEN 4 */
    TBRK->p[P_LD_PIT] = 3;                         /* A BARS 4 */
    TBRK->p[P_LEVEL] = 90;
    song.g[G_DUST] = 33;
    song.g[G_BPM] = 172;
    song.sel = TRK_BRK;
    trk[1].micro[2] = -20;
    TBRK->micro[9] = 12;
    lock_set(&trk[1], 2, P_ED_FLT, -30);
    lock_set(&trk[1], 2, P_E1, 5);
    lock_set(TBRK, 9, P_DIST, 64);
    lock_set(TBRK, 9, P_E0, 100);                  /* CMPLX locked on a step */
    step_fill_set(&trk[1], 2, FC_FILL);
    step_fill_set(TBRK, 9, FC_NOFILL);
    step_fill_set(TBRK, 63, FC_FILL);
    bl.var = 321;                                  /* the BASS phrase generator */
    bl.rhythm = BR_DUB;
    bl.move = 66;
    bl.change = 0;
    bl.answer = BA_RUN;
    bl.hold = BH_MID;
    bl.mix = 0x80u | 5u << 2 | 0u;                 /* (its steps, answer seed 5, OCT -1) */
    sec_card[AR_DROP] = (sec_card_t){.len = 2, .who = 0x1Bu, .loop = 3, .style = 2, .bass = 5};   /* the song */
    sec_card[AR_OUTRO] = (sec_card_t){.len = 4};
    arrangement.reserved[0] = 2;
    arrangement.reserved[1] = 201;
    arrangement.loop = 1;
    proj_capture(&q);
    bad += check("capture: a valid SPB4 image", proj_ok(&q) && q.magic == PROJ_MAGIC && q.size == sizeof q && q.sel == TRK_BRK);
    memcpy(buf, &q, sizeof q);
    bad += check("import: a stored image back byte for byte", proj_import(&q2, buf, (int)sizeof q) && !memcmp(&q, &q2, sizeof q));
    host_tracks_init();
    memset(sec_card, 0, sizeof sec_card);
    arrangement.reserved[0] = arrangement.reserved[1] = arrangement.loop = 0;
    proj_apply(&q2, 1);
    bad += check("apply: the song (FORM, VARIANT, MODE) and the section cards",
                 arrangement.reserved[0] == 2 && arrangement.reserved[1] == 201 && arrangement.loop == 1 &&
                 sec_card[AR_DROP].len == 2 && sec_card[AR_DROP].who == 0x1Bu && sec_card[AR_DROP].loop == 3 &&
                 sec_card[AR_DROP].style == 2 && sec_card[AR_DROP].bass == 5 && sec_card[AR_OUTRO].len == 4 &&
                 !sec_card[AR_INTRO].len && !sec_card[AR_INTRO].who && !sec_card[AR_BUILD].style);
    ok = trk[2].p[P_SLEN] == 7 && TBRK->p[P_SLEN] == 8 && trk[1].step[2].n == 2 && trk[1].step[2].lvl == 0x0D &&
         trk[1].step[2].rat == 0x06 && trk[1].eng_req == ENGI_SAMPLE && trk[1].preset == 3 && song.g[G_DUST] == 33 &&
         song.g[G_BPM] == 172 && trk[1].micro[2] == -20 && TBRK->micro[9] == 12 && trk[1].micro[3] == 0 &&
         lock_find(&trk[1], 2, P_ED_FLT, 0) >= 0 && trk[1].lock[lock_find(&trk[1], 2, P_ED_FLT, 0)].val == -30 &&
         lock_find(&trk[1], 2, P_E1, 0) >= 0 && step_fill(&trk[1], 2) == FC_FILL && step_fill(&trk[1], 3) == FC_NORM;
    bad += check("apply: the parts (steps, levels, ratchets, engine, preset), globals, nudges, locks, conditions", ok);
    bad += check("apply: the BASS generator (VARIANT, RHYTHM, MOVE, LOCK, ANSWER, HOLD, OCT, its own steps)",
                 bl.var == 321 && bl.rhythm == BR_DUB && bl.move == 66 && bl.change == 0 && bl.answer == BA_RUN &&
                 bl.hold == BH_MID && bl_oct() == -1 && bl_aseed() == 5u && bl_on());
    ok = !memcmp(&TBRK->bstep[9], &(bstep_t){.on = 1, .slice = 6, .lvl = LV_SOFT, .rat = 3, .pitch = -7, .flags = BS_REV},
                 sizeof(bstep_t)) && TBRK->bstep[8].on && !TBRK->bstep[8].slice && !TBRK->bstep[10].on &&
         TBRK->p[P_E0] == 77 && TBRK->p[P_ATK] == 3 && TBRK->p[P_DEC] == 40 && TBRK->p[P_ED_PIT] == -5 &&
         TBRK->p[P_LWAVE] == 2 && TBRK->p[P_LD_PIT] == 3 && TBRK->p[P_LEVEL] == 90 && TBRK->p[P_ED_FX] == BP_TRIM &&
         lock_find(TBRK, 9, P_DIST, 0) >= 0 && lock_find(TBRK, 9, P_E0, 0) >= 0 &&
         TBRK->lock[lock_find(TBRK, 9, P_E0, 0)].val == 100 && step_fill(TBRK, 9) == FC_NOFILL &&
         step_fill(TBRK, 63) == FC_FILL && step_fill(TBRK, 8) == FC_NORM && TBRK->eng_req == 0;
    bad += check("apply: the BREAK track (its steps, its generator in p[], a CMPLX lock, nudge, conditions)", ok);
    {   /* (the BREAK track's p[] on apply: BPV reads them; brkp_poll follows LOOP B) */
        brkp_poll();
        ok = BPV(BP_CMPLX) == 77 && BPV(BP_PHRASE) == 3 && BPV(BP_BCH) == 40 && BPV(BP_PITCH) == -5 &&
             brkp_bank_loop(1) == 2u && bpb[1].key == 2u && bpb[1].src && brkp_bank_bars(0) == 4u;
        bad += check("... as the generator reads them (CMPLX, PHRSE, B CHN, PTCH, LOOP B built, A BARS 4)", ok);
    }
    proj_apply(&q2, 0);                            /* (as a song section: the globals stay) */
    song.g[G_DUST] = 5;
    proj_apply(&q2, 0);
    bad += check("a song section: the tracks, not the globals", song.g[G_DUST] == 5 && TBRK->p[P_E0] == 77);

    /* a damaged image: every value back inside its range */
    q = q2;
    q.t[0].engine = 7;                             /* (an engine SLOOP had: none here) */
    q.t[0].p[P_E0] = 999;                          /* ANALOG's EDIT 1 past its range */
    q.t[TRK_BRK].engine = 1;                       /* (the BREAK track has none: 0) */
    q.t[1].micro[7] = 100;
    q.t[1].micro[8] = -100;
    q.t[1].lock[5].step = 4, q.t[1].lock[5].param = P_SLEN, q.t[1].lock[5].val = 8;
    q.t[1].lock[6].step = 64, q.t[1].lock[6].param = P_E0, q.t[1].lock[6].val = 1;
    q.t[1].lock[7].step = 4, q.t[1].lock[7].param = P_LEVEL, q.t[1].lock[7].val = 999;
    q.t[1].lock[8].step = 4, q.t[1].lock[8].param = 200, q.t[1].lock[8].val = 1;
    q.t[1].fill[1] = 0xF9;                         /* steps 5, 6: fill only, no fill; 7, 8: 3 (-> normal) */
    q.t[1].step[11].n = 9, q.t[1].step[11].time = 7, q.t[1].step[11].note[0] = 200;
    q.bl.var = 5000, q.bl.rhythm = 40, q.bl.move = 200, q.bl.change = 222, q.bl.answer = 99, q.bl.hold = 9;   /* (BASS) */
    proj_apply(&q, 1);
    bad += check("apply: the BASS generator back inside its ranges",
                 bl.var < 1000u && bl.rhythm < BR_N && bl.move == 100 && bl.change == 100 && bl.answer < BA_N && bl.hold < BH_N);
    ok = trk[1].micro[7] == MICRO_MAX && trk[1].micro[8] == MICRO_MIN && trk[1].lock[5].step == LOCK_FREE &&
         trk[1].lock[6].step == LOCK_FREE && trk[1].lock[8].step == LOCK_FREE && trk[1].lock[7].step == 4 &&
         trk[1].lock[7].val == 127 && lock_find(&trk[1], 2, P_ED_FLT, 0) >= 0 &&
         step_fill(&trk[1], 4) == FC_FILL && step_fill(&trk[1], 5) == FC_NOFILL && trk[1].fill[1] == 0x09 &&
         trk[1].step[11].n == 4 && trk[1].step[11].time == ST_REST && trk[1].step[11].note[0] == 72;
    bad += check("apply: a nudge past the range clamped, a lock on LEN / step 64 / param 200 freed, LEVEL 999 -> 127, "
                 "condition 3 -> normal, a step's notes / count / time masked", ok);
    bad += check("apply: an engine there is not -> one there is, EDIT clamped by it; the BREAK track's engine 0",
                 trk[0].eng_req == 7u % NENGINES && trk[0].p[P_E0] <= ENGINES[trk[0].eng_req]->edit[0].max &&
                 TBRK->eng_req == 0);
    q = q2;
    for (i = 0; i < NSTEP; i++)                    /* the BREAK track's steps: every field past its range */
        q.t[TRK_BRK].bstep[i] = (bstep_t){.on = (uint8_t)(i + 2u), .slice = (uint8_t)(9u + i), .lvl = 7, .rat = 5,
                                          .pitch = (int8_t)(i & 1u ? 100 : -100), .flags = 0xFF};
    q.t[TRK_BRK].bstep[1].slice = BRK_SLICES;
    q.t[TRK_BRK].p[P_E0] = 300;                    /* CMPLX 0..100 */
    q.t[TRK_BRK].p[P_E5] = -4;                     /* 3X 0..100 */
    q.t[TRK_BRK].p[P_ATK] = 9;                     /* PHRSE 0..4 */
    q.t[TRK_BRK].p[P_ED_SHP] = 20;                 /* LEN 0..6 */
    q.t[TRK_BRK].p[P_ED_PIT] = -50;                /* PTCH -12..12 */
    q.t[TRK_BRK].p[P_LRATE] = 999;                 /* LOOP A 0..BP_MAXLOOP */
    q.t[TRK_BRK].p[P_LD_AMP] = 7;                  /* D BARS 0..4 */
    q.t[TRK_BRK].p[P_LEVEL] = 500;                 /* (a mix parameter: TP's) */
    q.t[TRK_BRK].lock[0] = (plock_t){.step = 3, .param = P_E0, .val = 500};    /* CMPLX locked past its range */
    q.t[TRK_BRK].lock[1] = (plock_t){.step = 3, .param = P_LRATE, .val = 1};   /* LOOP A: not lockable */
    proj_apply(&q, 1);
    ok = 1;
    for (i = 0; i < NSTEP; i++) {
        const bstep_t *b = &TBRK->bstep[i];
        ok &= b->on == 1u && b->slice == (i == 1u ? BRK_SLICES : 0u) && b->lvl == 3u && b->rat == 0u &&
              b->pitch == (i & 1u ? 24 : -24) && b->flags == BS_REV;
    }
    bad += check("apply: the BREAK track's steps: ON 0 / 1, a slice past 8 the generator's, level, ratchet, pitch, flags", ok);
    ok = TBRK->p[P_E0] == 100 && TBRK->p[P_E5] == 0 && TBRK->p[P_ATK] == 4 && TBRK->p[P_ED_SHP] == 6 &&
         TBRK->p[P_ED_PIT] == -12 && TBRK->p[P_LRATE] == brk_desc(P_LRATE)->max && TBRK->p[P_LD_AMP] == 4 &&
         TBRK->p[P_LEVEL] == TP[P_LEVEL].max && brk_desc(P_LRATE)->max == (int16_t)BP_MAXLOOP;
    bad += check("apply: the BREAK track's generator clamped by brk_desc (CMPLX, 3X, PHRSE, LEN, PTCH, LOOP A, D BARS), LEVEL by TP", ok);
    bad += check("apply: a CMPLX lock clamped to 100, a LOOP A lock freed",
                 TBRK->lock[0].step == 3 && TBRK->lock[0].param == P_E0 && TBRK->lock[0].val == 100 &&
                 TBRK->lock[1].step == LOCK_FREE);

    {   /* a card out of range comes back inside it; a project with no song saved leaves the song as it is */
        project_t bad_card = q;
        bad_card.card[AR_ROLL] = (sec_card_t){.len = 9, .who = 0xFFu, .loop = 7, .style = BS_NSTYLE + 3, .bass = 200};
        bad_card.song_mode = 0;
        bad_card.sum = proj_sum(&bad_card);
        arrangement.reserved[0] = 1;
        arrangement.reserved[1] = 9;
        arrangement.loop = 0;
        proj_apply(&bad_card, 1);
        bad += check("apply: a card out of its ranges is clamped (who: any value); no song saved: FORM, VARIANT, MODE stay",
                     !sec_card[AR_ROLL].len && sec_card[AR_ROLL].who == 0xFFu && !sec_card[AR_ROLL].loop &&
                     !sec_card[AR_ROLL].style && !sec_card[AR_ROLL].bass && arrangement.reserved[0] == 1 &&
                     arrangement.reserved[1] == 9 && arrangement.loop == 0);
    }
    {   /* SPB3: read, made an SPB4: 22 locks on one track keep the first 20, the rest of the project as it was */
        static project3_t o;
        uint32_t k, n3 = 0;
        project3_t *po = &o;
        memset(po, 0, sizeof *po);
        po->magic = PROJ_MAGIC3;
        po->size = sizeof *po;
        po->sel = 2;
        po->space = 99;
        po->g[G_BPM] = 168;
        po->cg = cg;
        po->bl = bl;
        for (k = 0; k < NTRK; k++) {
            memcpy(po->t[k].p, trk[k].p, sizeof po->t[k].p);
            po->t[k].engine = (uint8_t)k;
            po->t[k].preset = (uint8_t)(k + 1);
            memcpy(po->t[k].step, trk[k].step, sizeof po->t[k].step);
            memcpy(po->t[k].micro, trk[k].micro, sizeof po->t[k].micro);
            memcpy(po->t[k].fill, trk[k].fill, sizeof po->t[k].fill);
            for (i = 0; i < NLOCK_V3; i++) {
                po->t[k].lock[i].step = LOCK_FREE;
                po->t[k].lock[i].param = 0;
            }
        }
        for (i = 0; i < 22u; i++) {                  /* track 1: 22 locks (step i, CMPLX-like P_E0 value i) */
            po->t[1].lock[i].step = (uint8_t)i;
            po->t[1].lock[i].param = P_E0;
            po->t[1].lock[i].val = (int16_t)i;
        }
        po->t[2].lock[7].step = 3;                   /* track 2: one, in a high slot of the old table */
        po->t[2].lock[7].param = P_E1;
        po->t[2].lock[7].val = 4;
        po->sum = proj_hash(po, sizeof *po - 4u);
        memcpy(buf, po, sizeof *po);
        ok = proj_import(&q2, buf, (int)sizeof *po) && proj_ok(&q2) && q2.magic == PROJ_MAGIC && q2.size == sizeof q2 &&
             q2.sel == 2 && q2.space == 99 && q2.g[G_BPM] == 168 && q2.t[1].engine == 1 && q2.t[1].preset == 2 &&
             !memcmp(q2.t[1].step, po->t[1].step, sizeof q2.t[1].step) && !q2.song_mode && !q2.song_var;
        for (k = 0; k < ARR_ROLES; k++)
            ok &= !q2.card[k].len && !q2.card[k].who && !q2.card[k].loop && !q2.card[k].style && !q2.card[k].bass;
        for (i = 0; i < NLOCK; i++)
            n3 += q2.t[1].lock[i].step < NSTEP && q2.t[1].lock[i].val == (int16_t)q2.t[1].lock[i].step;
        ok &= n3 == NLOCK && q2.t[2].lock[0].step == 3 && q2.t[2].lock[0].param == P_E1 && q2.t[2].lock[0].val == 4 &&
              q2.t[2].lock[1].step == LOCK_FREE;
        bad += check("SPB3 is read as an SPB4: the project as it was, 22 locks keep the first 20, locks packed, empty cards, no song", ok);
        po->sum ^= 1u;                               /* (damaged) */
        memcpy(buf, po, sizeof *po);
        bad += check("... an SPB3 with a bad checksum: refused", !proj_import(&q2, buf, (int)sizeof *po));
    }

    /* refused: SLOOP's formats, damaged images, wrong sizes */
    {
        static const uint32_t OLD[5][2] = {{0x46554E31u, 688u}, {0x46554E32u, 2552u}, {0x46554E33u, 2584u},
                                           {0x46554E34u, 3112u}, {0x46554E35u, 3840u}};   /* FUN1..FUN5 as stored */
        ok = 1;
        for (i = 0; i < 5u; i++) {
            old_blob(buf, OLD[i][1], OLD[i][0]);
            ok &= !proj_import(&q2, buf, (int)OLD[i][1]);
        }
        old_blob(buf, sizeof q, 0x46554E35u);      /* a FUN5 magic on an SPB3-sized image, its sum right */
        ok &= !proj_import(&q2, buf, (int)sizeof q);
        old_blob(buf, 3840u, PROJ_MAGIC);          /* SPB4 on FUN5's size */
        ok &= !proj_import(&q2, buf, 3840);
        old_blob(buf, sizeof(project3_t), PROJ_MAGIC);   /* SPB4's magic on SPB3's size, its sum right */
        ok &= !proj_import(&q2, buf, (int)sizeof(project3_t));
        old_blob(buf, sizeof q, 0x53504231u);      /* SPB1 (0.1 before the CHORDS generator) */
        ok &= !proj_import(&q2, buf, (int)sizeof q);
        old_blob(buf, 3828u, 0x53504232u);         /* SPB2 (before the BASS generator), as stored */
        ok &= !proj_import(&q2, buf, 3828);
        bad += check("SLOOP's FUN1..FUN5 images and FMX's SPB1 / SPB2 (checksums right): refused", ok);
    }
    proj_capture(&q);
    memcpy(buf, &q, sizeof q);
    buf[200] ^= 1u;
    bad += check("SPB4 with a bad checksum: refused", !proj_import(&q2, buf, (int)sizeof q));
    buf[200] ^= 1u;
    bad += check("SPB4 with a wrong length: refused", !proj_import(&q2, buf, (int)sizeof q - 2) && !proj_import(&q2, buf, 0));
    q.size = sizeof q + 4u;
    q.sum = proj_sum(&q);
    memcpy(buf, &q, sizeof q);
    bad += check("SPB2 whose size field is not its own: refused", !proj_import(&q2, buf, (int)sizeof q));

    printf("%s\n", bad ? "PROJECT FORMAT TEST FAILED" : "project format test passed");
    return bad != 0;
}
