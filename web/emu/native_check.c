/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* web/emu/sloop_web.c built natively (cc, as the host tests): boots the device and drives the BREAK track through the
 * panel as a hand does (its screen, the keys, the pages, PLAY), checks the state after each gesture (the track's p[] and
 * bstep[], bp), and writes the screens as PPM (DIR/emu-*.ppm) and the audio (float32 L R) to DIR/emu.f32; then the
 * track as the autosave keeps it (project.c: SPB1 in OBJ_AUTOSAVE) and brings it back.
 *   cc -O2 -ffp-contract=off -w -Ibuild/gen -Ifirmware/src -Ifirmware/hal -o build/emu/native_check web/emu/native_check.c -lm
 *   build/emu/native_check DIR */
#include "sloop_web.c"

static FILE *out;
static double peak, energy;
static uint32_t nan_count;
static void render(double ms, int rec)            /* 128-frame calls, as the worklet */
{
    uint32_t n = (uint32_t)(ms * 44.1 + 0.5), k, i;
    for (k = 0; k < n; k += 128u) {
        uint32_t m = n - k < 128u ? n - k : 128u;
        web_render(m);
        for (i = 0; rec && i < m; i++) {
            float s[2] = {out_l[i], out_r[i]};
            if (s[0] != s[0] || s[1] != s[1])
                nan_count++;
            peak = fabs(s[0]) > peak ? fabs(s[0]) : peak;
            energy += (double)s[0] * s[0] + (double)s[1] * s[1];
            fwrite(s, sizeof s, 1, out);
        }
    }
}
static uint32_t lit_pixels(void)                  /* pixels that are not the background colour */
{
    uint32_t i, n = 0;
    for (i = 0; i < 240u * 240u; i++)
        n += web_fb[i] != web_fb[0];
    return n;
}
static void ppm(const char *dir, const char *name)
{
    char path[512];
    FILE *f;
    unsigned i;
    snprintf(path, sizeof path, "%s/emu-%s.ppm", dir, name);
    if (!(f = fopen(path, "wb")))
        return;
    fprintf(f, "P6\n240 240\n255\n");
    for (i = 0; i < 240u * 240u; i++) {
        uint16_t p = (uint16_t)((web_fb[i] >> 8) | (web_fb[i] << 8));
        uint8_t rgb[3] = {(uint8_t)((p >> 11) * 255 / 31), (uint8_t)(((p >> 5) & 63) * 255 / 63), (uint8_t)((p & 31) * 255 / 31)};
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
}

static uint32_t fails;
static void check(const char *what, int ok)
{
    printf("emu: %-70s %s\n", what, ok ? "ok" : "FAIL");
    fails += !ok;
}
static void press(uint32_t labels)                 /* a button, tapped: down 60 ms, up 60 ms */
{
    web_buttons(labels);
    render(60, 0);
    web_buttons(0);
    render(60, 0);
}
static void key_hold(uint32_t mask, double ms)
{
    web_keys(mask);
    render(ms, 0);
}

static uint32_t steps_on(void)                     /* the BREAK track's first 16 steps that play, bit per step */
{
    uint32_t i, m = 0;
    for (i = 0; i < 16u; i++)
        m |= (uint32_t)(TBRK->bstep[i].on != 0u) << i;
    return m;
}
static void white_tap(uint32_t i)
{
    key_hold(1u << key_of_white(i), 60);
    web_keys(0);
    render(40, 0);
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "build/emu";
    char path[512], name[8];
    uint32_t logo, home, i, draws0, trigs0;
    snprintf(path, sizeof path, "%s/emu.f32", dir);
    if (!(out = fopen(path, "wb")))
        return 2;
    web_nor_erase();
    {   /* the standard set in USR1, as the page puts it at the first switch-on: user loops BR1..BR3 after the built-in ones */
        FILE *sf = fopen(argc > 2 ? argv[2] : "tests/fixtures/breaks_cc.bin", "rb");
        size_t got = sf ? fread(nor + SMP_USER_OFF(0u), 1, SMP_USER_SIZE, sf) : 0;
        if (sf)
            fclose(sf);
        check("a set of three is in USR1 (tests/fixtures/breaks_cc.bin)", got > SMP_USER_DATA);
    }
    web_boot();
    render(500, 0);
    logo = lit_pixels();
    ppm(dir, "1-logo");
    render(1500, 0);
    home = lit_pixels();
    ppm(dir, "2-home");
    check("the logo and the home screen are drawn", logo > 2000u && home > 1000u);
    check("the BREAK track has its defaults (STYLE JUNGLE: CMPLX 55, PHRSE 4, MANGL 10; every step on, not muted)",
          BPV(BP_STYLE) == BS_JUNGLE && BPV(BP_CMPLX) == 55 && BPV(BP_PHRASE) == 2 && BPV(BP_MANGLE) == 10 &&
          steps_on() == 0xFFFFu && TBRK->p[P_MUTE] == 0);
    brkp_loop_name(BP_NB, name);
    check("the loops: FMX's 14 built in (AMEN A ..), BR1..BR3 of USR1 after them", BP_NB == 14u && brkp_nloops() == BP_NB + 3u &&
          !strcmp(name, "BR1"));
    check("LOOP A..D = 0..3 (AMEN A, AMEN B, THINK, FUNKY), all four built by the main loop", BPV(BP_LOOPA) == 0 &&
          BPV(BP_LOOPD) == 3 && bpb[0].src && bpb[1].src && bpb[2].src && bpb[3].src);
    check("the built-in loops at their own rate (16 kHz): AMEN A one bar, AMEN B two", bpb[0].src->rate == 23777u &&
          bpb[1].src->rate == 23777u && bpb[0].auto_bars == 1u && bpb[1].auto_bars == 2u);

    /* FMX: a new device powers on in LIVE (ui_live.c); HOME: STUDIO, the DECK */
    check("power-on: the LIVE screen (no settings yet)", live_on && live_shown);
    ppm(dir, "2b-live");
    press(1u << B_HOME);
    render(60, 0);
    /* the screen: the DECK, ALGORITHM left up to the BREAK track (the top lane; still the DECK), SEQ opens it */
    check("HOME: STUDIO, TRACKS is its first screen", !live_on && !on_break_page() && cur_page()->scope == SC_TRK);
    for (i = 0; i < 3u; i++) {
        web_enc(EN_ALGO, -1);
        render(40, 0);
    }
    check("ALGORITHM left x3: the BREAK track (the top lane) is selected, still TRACKS", song.sel == TRK_BRK && !on_break_page());
    press(1u << B_SEQ);
    check("SEQ: the BREAK screen, STYLE", on_break_page() && bk_page == BK_STYLE);
    ppm(dir, "3-break");
    /* keys: a tap on a white key switches the step */
    white_tap(0);
    check("a white key tapped: step 1 off", steps_on() == 0xFFFEu);
    white_tap(0);
    check("... and again: on", steps_on() == 0xFFFFu);
    for (i = 1; i < 16u; i += 2u)                    /* the 8th notes */
        white_tap(i);
    check("eight white keys: the odd steps off, the 8th notes left", steps_on() == 0x5555u);
    /* a pin: a step held + black key 3 */
    key_hold(1u << key_of_white(2) | 1u << BK_BLACK[2], 80);
    web_keys(1u << key_of_white(2));
    render(40, 0);
    web_keys(0);
    render(60, 0);
    check("a step held + black key 3: the step is pinned to slice 3, and stays on", TBRK->bstep[2].slice == 3 &&
          TBRK->bstep[2].on);
    key_hold(1u << key_of_white(2) | 1u << BK_BLACK[2], 80);
    web_keys(1u << key_of_white(2));
    render(40, 0);
    web_keys(0);
    render(60, 0);
    check("... the same again: back to the generator", TBRK->bstep[2].slice == 0 && steps_on() == 0x5555u);
    /* a slice key by itself is a live slice, let go it ends */
    key_hold(1u << BK_BLACK[4], 80);
    check("a black key held: that slice is held live", (bp.keys & 0x10u) != 0u && bp.count == 1u);
    web_keys(0);
    render(60, 0);
    check("... let go: released", bp.keys == 0u && bp.count == 0u);
    /* the pages: SEQ groove, ARP retrigger, EDIT loops (again: bars), GLO mix, SEL phrase, ENV tune, LFO pattern;
     * SELECT walks them */
    press(1u << B_ARP);
    check("ARP: the RETRIG page", bk_page == BK_RETRIG);
    press(1u << B_EDIT);
    check("EDIT: the LOOPS page", bk_page == BK_LOOPS);
    press(1u << B_EDIT);
    check("EDIT again: the BARS page", bk_page == BK_BARS);
    press(1u << B_GLO);
    check("GLO: the MIX page", bk_page == BK_MIX);
    press(1u << B_SCL);
    check("SEL: the PHRASE page", bk_page == BK_PHRASE);
    press(1u << B_ENV);
    check("ENV: the TUNE page", bk_page == BK_TUNE);
    press(1u << B_LFO);
    check("LFO: the PATTERN page", bk_page == BK_PATTERN);
    press(1u << B_SEQ);
    check("SEQ: the STYLE page", bk_page == BK_STYLE && on_break_page());
    press(1u << B_SEQ);
    check("SEQ again: the GROOVE page", bk_page == BK_GROOVE && on_break_page());
    web_enc(EN_SELECT, 1);
    render(60, 0);
    check("SELECT: the next page", bk_page == BK_RETRIG);
    web_enc(EN_SELECT, -1);
    render(60, 0);
    web_enc(EN_K1, 5);
    render(60, 0);
    web_enc(EN_K4, 7);
    render(60, 0);
    check("KNOB 1 and 4 on GROOVE: CMPLX and FILL (the track's P_E0, P_E3)", TBRK->p[P_E0] > 55 && TBRK->p[P_E3] > 60);
    web_enc(EN_PRESET, 1);
    render(60, 0);
    check("PRESETS: LOOP A (AMEN B)", TBRK->p[P_LRATE] == 1 && bk_page == BK_LOOPS);
    web_enc(EN_PRESET, -1);
    render(60, 0);
    press(1u << B_OCTDN);
    check("OCT-: MUTE", TBRK->p[P_MUTE] == 1);
    press(1u << B_OCTDN);
    check("OCT- again: unmuted", TBRK->p[P_MUTE] == 0);
    ppm(dir, "4-break-set");
    /* playing: the track plays with the transport (a built-in loop); it is heard */
    BPV(BP_CMPLX) = 50;
    for (i = 1; i < 16u; i += 2u)
        TBRK->bstep[i].on = 1;
    trigs0 = bp.st_trigs;
    press(1u << B_PLAY);
    draws0 = web_screen_draws();
    render(2000, 1);
    ppm(dir, "5-break-playing");
    check("PLAY: the transport runs, the track sounds (AMEN A, built in)", web_playing() && peak > 0.05 && !nan_count &&
          bp.running && bp.st_trigs > trigs0);
    check("the screen follows (the playhead)", web_screen_draws() - draws0 > 5u);
    key_hold(1u << BK_BLACK[8], 300);              /* reverse held */
    check("black key 9 held: reverse", bp.reverse == 1);
    web_keys(0);
    render(100, 1);
    check("... let go: forwards", bp.reverse == 0);
    /* REC on: a slice key records the hit into the step it falls on */
    for (i = 0; i < 16u; i++)
        TBRK->bstep[i].slice = 0;
    song.rec |= 1u << TRK_BRK;
    key_hold(1u << BK_BLACK[5], 80);
    web_keys(0);
    render(60, 1);
    song.rec &= ~(1u << TRK_BRK);
    {
        uint32_t got = 0;
        for (i = 0; i < trk_len(TBRK); i++)
            got += TBRK->bstep[i].slice == 6 && TBRK->bstep[i].on;
        check("REC on, black key 6 while playing: slice 6 recorded into a step", got == 1u);
    }
    press(1u << B_PLAY);
    render(300, 0);
    check("PLAY again: stopped, the track off", !web_playing() && !bp.running);
    /* a user loop: PRESETS onto BR1 (USR1's first zone), built by the main loop, plays */
    web_enc(EN_PRESET, (int32_t)BP_NB);
    render(200, 0);
    check("PRESETS x3: LOOP A = BR1 (user loop BP_NB + 0), built", TBRK->p[P_LRATE] == (int16_t)BP_NB && brkp_bank_src(0) &&
          bpb[0].key == (BP_NB | smp_user_gen << 8));
    peak = 0;
    press(1u << B_PLAY);
    render(1500, 1);
    check("PLAY: BR1 sounds", web_playing() && peak > 0.05 && !nan_count);
    press(1u << B_PLAY);
    render(300, 0);
    /* leaving */
    web_enc(EN_ALGO, 1);
    render(60, 0);
    check("ALGORITHM right: back to TRACKS, the next lane (BASS) selected", !on_break_page() && song.sel == 0u &&
          cur_page()->scope == SC_TRK);
    press(1u << B_ENV);
    check("ENV: a synth page of BASS", !on_break_page() && cur_page()->scope != SC_TRK && cur_fam() == FAM_ENV);
    web_enc(EN_ALGO, -1);
    render(60, 0);
    check("ALGORITHM left up onto the BREAK track from it: the BREAK screen", on_break_page() && song.sel == TRK_BRK);
    press(1u << B_HOME);
    check("HOME: back to TRACKS", !on_break_page() && cur_page()->scope == SC_TRK);

    /* the kept state: the working project, autosaved (stopped, idle) as SPB1 and read back at power-on */
    {
        static project_t q;
        static bstep_t was[NSTEP];
        int16_t pw[P_COUNT];
        st_hdr_t h;
        int n;
        TBRK->p[P_E0] = 81;                       /* CMPLX */
        TBRK->p[P_PAN] = -33;
        TBRK->p[P_LWAVE] = (int16_t)(BP_NB + 1u);  /* LOOP B = BR2 */
        TBRK->p[P_ED_PIT] = -5;                    /* PTCH */
        for (i = 0; i < NSTEP; i++)
            memset(&TBRK->bstep[i], 0, sizeof TBRK->bstep[i]);
        for (i = 0; i < 16u; i++)
            TBRK->bstep[i].on = (uint8_t)((0x4321u >> i) & 1u);
        TBRK->bstep[5].slice = 8;
        TBRK->bstep[5].on = 1;
        TBRK->bstep[8].lvl = LV_HARD;
        TBRK->bstep[8].rat = 2;
        TBRK->bstep[8].pitch = 7;
        TBRK->bstep[8].flags = BS_REV;
        memcpy(was, TBRK->bstep, sizeof was);
        memcpy(pw, TBRK->p, sizeof pw);
        for (i = 0, n = 0; i < 60u; i++) {        /* (AUTOSAVE_GAP since the last, AUTOSAVE_IDLE without input) */
            render(500, 0);
            if (st_current(OBJ_AUTOSAVE, &h) >= 0 && (n = st_load(OBJ_AUTOSAVE, &q, sizeof q)) == (int)sizeof q &&
                q.t[TRK_BRK].p[BPP[BP_CMPLX]] == 81)
                break;
        }
        check("autosave: the working project is in flash (SPB1)", n == (int)sizeof q && proj_ok(&q) && q.magic == PROJ_MAGIC);
        check("... with the BREAK track's generator in its p[] and its steps", q.t[TRK_BRK].p[BPP[BP_CMPLX]] == 81 &&
              q.t[TRK_BRK].p[BPP[BP_LOOPB]] == (int16_t)(BP_NB + 1u) && q.t[TRK_BRK].p[P_PAN] == -33 &&
              !memcmp(q.t[TRK_BRK].bstep, was, sizeof was));
        brk_defaults(TBRK);
        TBRK->p[P_PAN] = 0;
        autosave_resume();
        check("after a power cycle (autosave_resume): the BREAK track as it was", !memcmp(TBRK->p, pw, sizeof pw) &&
              !memcmp(TBRK->bstep, was, sizeof was));
        {   /* a damaged track: every value back inside its range */
            q.t[TRK_BRK].p[BPP[BP_CMPLX]] = 120;
            q.t[TRK_BRK].p[BPP[BP_PHRASE]] = 9;
            q.t[TRK_BRK].bstep[3].slice = 12;
            q.t[TRK_BRK].bstep[3].rat = 9;
            q.t[TRK_BRK].bstep[3].on = 5;
            q.t[TRK_BRK].bstep[3].pitch = 100;
            q.sum = proj_sum(&q);
            check("a project with the track out of range", st_save(OBJ_AUTOSAVE, &q, sizeof q) == 0);
            autosave_resume();
            check("... reads clamped (CMPLX 100, PHRSE 16, slice / ratchet 0, pitch +24)", BPV(BP_CMPLX) == 100 &&
                  BPV(BP_PHRASE) == 4 && TBRK->bstep[3].slice == 0 && TBRK->bstep[3].rat == 0 && TBRK->bstep[3].on == 1 &&
                  TBRK->bstep[3].pitch == 24);
        }
        {   /* a SLOOP project (FUN5) is not read: the track stays */
            q.magic = 0x46554E35u;
            q.sum = proj_sum(&q);
            BPV(BP_CMPLX) = 33;
            check("an object of SLOOP (FUN5)", st_save(OBJ_AUTOSAVE, &q, sizeof q) == 0);
            autosave_resume();
            check("... not read: the track as it was", BPV(BP_CMPLX) == 33);
        }
    }
    {   /* the demo set (web_demo_set, as the page at the first switch-on, after the packs): four songs; LIVE plays song 1,
         * OCT+ picks song 2, white key 8 x3 CUT: on the next 8 bars song 2 plays, from its DROP */
        static const char *const PK[3] = {"web/packs/RAVEHITS.bin", "web/packs/ANIME.bin", "web/packs/PIANO.bin"};
        uint32_t k, f, bpm[4], diff = 1;
        for (k = 0; k < 3u; k++) {
            FILE *pf = fopen(PK[k], "rb");
            memset(nor + SMP_USER_OFF(k), 0xFF, SMP_USER_SIZE);
            if (pf) {
                (void)!fread(nor + SMP_USER_OFF(k), 1, SMP_USER_SIZE, pf);
                fclose(pf);
            }
            web_slot_changed(k);
        }
        if (!song.playing) {                            /* (as it may be on the page: playing; it stops first) */
            press(1u << B_PLAY);
            render(200, 0);
        }
        check("the demo set: four songs saved (playing: stopped first)", web_demo_set() && project_used(0) && project_used(1) && project_used(2) &&
              project_used(3));
        for (k = 0; k < 4u; k++)
            bpm[k] = (uint32_t)proj_slot[k].g[G_BPM];
        for (k = 1; k < 4u; k++)
            diff &= bpm[k] != bpm[k - 1u] && proj_slot[k].t[1].preset != proj_slot[k - 1u].t[1].preset;
        check("... each its own tempo and pad; song 1 loaded", diff && song.g[G_BPM] == (int16_t)bpm[0]);
        if (!live_on) {
            press(1u << B_HOME);
            render(100, 0);
        }
        press(1u << B_PLAY);
        render(300, 0);
        check("LIVE: PLAY, the song (song 1)", web_playing() && live_on && arrangement_clock.running);
        {   /* OCT+ round the set: song 2, 3, 4, 2 (not song 1: it plays) */
            int ok = live_song == 0;
            static const int8_t W[4] = {1, 2, 3, 1};
            for (k = 0; k < 4u; k++) {
                press(1u << B_OCTUP);
                ok &= live_next == W[k];
            }
            check("... song 1 plays; OCT+ round the others: SONG 2, 3, 4, 2", ok);
        }
        key_hold(1u << 12, 60); key_hold(0, 60);       /* (white key 8: C4, key 12) */
        key_hold(1u << 12, 60); key_hold(0, 60);
        key_hold(1u << 12, 60); key_hold(0, 60);
        check("... OCT+ song 2, white key 8 x3: CUT", live_next == 1 && live_tr == TR_CUT);
        ppm(dir, "9-live-next");
        for (f = 0; f < 400u && live_song != 1; f++)
            render(50, 0);
        check("... on the next 8 bars: song 2 plays, its tempo, from its DROP", live_song == 1 && song.g[G_BPM] == (int16_t)bpm[1] &&
              web_playing() && live_role() == AR_DROP);
        ppm(dir, "9-live-song2");
        press(1u << B_PLAY);
        render(200, 0);
        {   /* stopped: white key 8 only chooses the way (no load), OCT+ loads the next song */
            int32_t s0 = live_song;
            key_hold(1u << 12, 60); key_hold(0, 60);
            check("stopped: white key 8 chooses the way, loads nothing", !web_playing() && live_song == s0 && live_tr == TR_BRIDGE);
            press(1u << B_OCTUP);
            render(100, 0);
            check("... OCT+: the next song loaded (its tempo)", live_song == s0 + 1 && song.g[G_BPM] == (int16_t)bpm[s0 + 1] &&
                  live_tr == TR_NONE);
        }
        {   /* LIVE playing, SEL on the BREAK: its screen in STUDIO; PRESETS a loop: the next section keeps it; Q BAR: LOOP A
             * waits for the bar, the dial shows it */
            int16_t pick;
            uint32_t role, qm = q_mode;
            if (!live_on) {
                press(1u << B_HOME);
                render(100, 0);
            }
            press(1u << B_PLAY);
            render(300, 0);
            for (f = 0; f < 8u && !is_brk(TSEL); f++) {
                web_enc(EN_ALGO, -1);
                render(50, 0);
            }
            press(1u << B_SCL);
            render(100, 0);
            check("LIVE playing, SEL on the BREAK: its screen in STUDIO", web_playing() && !live_on && on_break_page());
            q_mode = QM_NOW;
            pick = (int16_t)((BPV(BP_LOOPA) + 1) % (int32_t)brkp_nloops());
            web_enc(EN_PRESET, 1);
            render(100, 0);
            role = live_role();
            for (f = 0; f < 800u && live_role() == role; f++)
                render(50, 0);
            render(500, 0);
            check("... PRESETS a loop: the next section keeps it", live_role() != role && BPV(BP_LOOPA) == pick);
            ppm(dir, "10-break-from-live");
            q_mode = QM_BAR;
            web_enc(EN_K1, 1);
            render(30, 0);
            check("... Q BAR: LOOP A waits for the bar (the dial shows it)", q_val(TRK_BRK, P_LRATE) != BPV(BP_LOOPA) &&
                  bk_page == BK_LOOPS);
            ppm(dir, "10b-break-q-wait");
            for (f = 0; f < 60u && q_waits(); f++)
                render(50, 0);
            check("... on the bar: it plays", !q_waits() && BPV(BP_LOOPA) == (pick + 1) % (int32_t)brkp_nloops());
            q_mode = (uint8_t)qm;
            press(1u << B_PLAY);
            render(200, 0);
        }
    }
    if (song.playing)
        press(1u << B_PLAY);
    render(200, 0);
    web_demo_set();                                    /* (as the page does: the four songs in the slots, song 1 loaded) */
    render(200, 0);
    check("the set's song 1, just loaded: nothing unsaved (LIVE's OCT loads another at once)", live_song == 0 && !project_unsaved());
    proj_touched = 1;                                  /* (as a knob turned in STUDIO leaves it: ui_input.c) */
    check("... edited in STUDIO: unsaved (OCT asks once more)", project_unsaved() != 0);
    project_save(0);
    check("... saved: nothing unsaved again", !project_unsaved() && live_song == 0);
    printf("emu: %s\n", fails ? "FAILED" : "all ok");
    fclose(out);
    return fails != 0;
}
