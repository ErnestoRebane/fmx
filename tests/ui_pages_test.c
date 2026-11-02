/* SPDX-License-Identifier: GPL-3.0-only */
/* The real UI (ui.c, ui_draw.c, ui_studio.c, ui_layers.c, ui_song.c, ui_menu.c, ui_input.c) on a
 * framebuffer with panel / flash / storage doubles, the audio (mix_block) running between frames as on
 * the device. Renders every screen to PPM for review (DIR/page-*.ppm, live-*.ppm, layer-*.ppm) and
 * drives the panel:
 *   taps    a layer button tapped opens its pages; held + a key / knob it does not
 *   FX      held + a white key: the FX panel, LIVE's (lfx.c); knobs: amount, time, tone, feedback
 *   SEQ     held: the steps on the white keys (BREAK: ON, the generator's slice), OCT: pages, a step key
 *           held + KNOB 1..4: slice / note, level, ratchet, nudge (2.4); PRESETS a lock of the parameter
 *           ALGORITHM picks (the sound parameter last touched first); + OCT-: its nudge, locks and fill
 *           condition go; + OCT+: the condition (normal, fill only, no fill)
 *   EDIT    held + a key: erase; OCT- / OCT+: undo / redo; KNOB 1 shift, 2 length x2
 *   ARP     held + a key: a roll; KNOB 1 the rate
 *   SEL     held + a key: the key of the song
 *   GLO     held + keys: mute, solo, fill (held) / fill the next bar, tap tempo; knobs: levels
 *   REC     press: arm / record at once; held: the clear ring, to the end: cleared (undo brings it back)
 *   SAVE    tapped: the song page; held: the SONG layer (AUTO SONG: a key a section of the chain, a jump on the
 *           next bar; loop / song, to the end / endless, a new song); the song page's dials
 *   BREAK   track 4 off its screen: a key's slice, OCT- / OCT+ held ghost / hard, recorded into the steps (keys,
 *           MIDI channel 10, rolls with their retrigger), erased; its screen (track 4 + SEQ, or ALGORITHM onto it
 *           from a synth page): the pages, white keys the steps, white held + black key a pin, black keys live,
 *           PRESETS loop A, OCT- mute, OCT+ the next 16 steps, ALGORITHM down / HOME back
 * then 20000 frames of random use: every draw stays on the screen. */
#define FELUCCA_ARRANGER 1
#define HOST_HAS_UI_C 1                           /* (ui.c is included below: its preset_fill) */
#define main hostsim_main
#include "hostsim.c"
#undef main
#include <assert.h>
static uint16_t screen[240*240];
static void lcd_sync(void) {}
static uint32_t blit_px;                         /* pixels sent to the screen (the DRUMMER's cost) */
static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *p)
{ uint32_t i,j; blit_px += w * h; assert(x+w<=240 && y+h<=240); for(j=0;j<h;j++) for(i=0;i<w;i++) screen[(y+j)*240+x+i]=p[j*w+i]; }
#include "../firmware/src/gfx.c"
static void lcd_fill(uint32_t x,uint32_t y,uint32_t w,uint32_t h,uint16_t c)
{ uint32_t i,j; assert(x+w<=240&&y+h<=240); for(j=0;j<h;j++)for(i=0;i<w;i++)screen[(y+j)*240+x+i]=swap16(c); }
static int32_t encs[7];
static uint32_t fm1_ticks(void) { return fm1_ms * 1000u * 24u; }
#define FM1_TICKS_PER_US 24u
static int32_t fm1_enc_take(uint32_t e) { int32_t s = encs[e]; encs[e] = 0; return s; }
static uint8_t fm1_led[16], fm1_led_dim[16], fm1_led_bg[16];
static volatile uint16_t fm1_led_bg_ns;
#define FM1_NCOL 16u
static const int8_t FM1_KEYMAP[5][16];
static void fm1_led_key(uint32_t id, int on) { (void)id; (void)on; }
static uint32_t edges_btn, notes_seen;
static uint32_t fm1_input_edges(int x) { uint32_t e = edges_btn; (void)x; edges_btn = 0; return e; }
static uint32_t fm1_input_note_edges(void) { uint32_t e = fm1_in.notes & ~notes_seen; notes_seen = fm1_in.notes; return e; }
static uint32_t wdt_tick_ms;                       /* (stress_test: 1, the blocking PANEL setup's clock runs out) */
static void fm1_wdt_feed(void) { fm1_ms += wdt_tick_ms; }
static int32_t fm1_adc_read(int c) { (void)c; return -1; }
static struct { uint32_t magic, stage, page, home, ui_frames; } felucca_dbg;
#define SCOPE_N 512u
static int16_t scope_buf[SCOPE_N], scope_bufr[SCOPE_N];
static uint32_t scope_w;
#include "../firmware/src/panel.c"
#include "../firmware/src/ui.c"
static uint32_t saves, loads;
static int project_used(uint32_t i) { return i < 2; }
static void project_save(uint32_t i) { (void)i; saves++; ui_message("SAVED"); }
static void project_load(uint32_t i) { (void)i; loads++; }
static int unsaved;                                  /* (project_unsaved: what the test says) */
static int project_unsaved(void) { return unsaved; }
static void arrangement_save(void) {}
static void arrangement_apply(uint32_t s) { (void)s; }
static void as_block(void) {}
static void song_backup(void) {}
static void song_restore(void) {}
static void song_keep_p(uint32_t k, uint32_t id, int32_t v) { (void)k, (void)id, (void)v; }
static void song_keep_turn(uint32_t k, uint32_t id, int32_t d, int32_t lo, int32_t hi) { (void)k, (void)id, (void)d, (void)lo, (void)hi; }
static void song_keep_eng(uint32_t k, uint32_t e, uint32_t pre) { (void)k, (void)e, (void)pre; }
static void song_keep_trk(uint32_t k, int steps) { (void)k, (void)steps; }
static void as_swap_now(void) { as_swap = 0; as_swapped++; }
static void as_tr_bar(void) {}                     /* (the way to the next song: arranger_scene.c, song_audio_test) */
static void as_hold(uint32_t on) { arrangement_clock.hold = (uint8_t)(on != 0u); }
/* (the SONG screen's: the card's state of a track in a section, the tracks out in it, as arranger_scene.c has them) */
static uint32_t as_who(uint32_t role, uint32_t k) { return role >= AR_N ? 0u : (uint32_t)(sec_card[role].who >> (2u * k)) & 3u; }
static uint32_t as_role_mute(uint32_t role)
{
    static const uint8_t OUT[AR_N] = {9, 1, 0, 2, 9, 0, 5};
    uint32_t m = role >= AR_N ? 0u : OUT[role], k;
    for (k = 0; k < 4u; k++)
        m = as_who(role, k) == 2u ? m | (1u << k) : (as_who(role, k) & 1u) ? m & ~(1u << k) : m;
    return m;
}
static int project_info(uint32_t i, int32_t *bpm) { if (bpm) *bpm = 170 + (int32_t)i; return i < 2; }
static int project_song_info(uint32_t i, int32_t *bpm, uint32_t *form, uint32_t *var)
{
    if (!project_info(i, bpm)) return 0;
    *form = i == 0 ? 1u : 3u;
    *var = i;
    return 1;
}
static int up_used(uint32_t k) { return k < 2; }
static int up_load(uint32_t k) { (void)k; return 0; }
static uint32_t up_count(void) { return 2; }
static uint32_t up_nth(uint32_t n) { return n; }
static uint32_t up_rank(uint32_t s) { return s; }
static void up_name(uint32_t k, char *b) { str_cpy(b, k ? "MY PAD" : "MY LEAD", 13); }
static void up_slot_label(char *b, uint32_t k) { fmt_int(b, (int32_t)k + 1); }
static void up_ui(uint32_t op, uint32_t k) { (void)op; (void)k; }
static void settings_save(void) {}
#include "../firmware/src/psx3d.c"
#include "../firmware/src/ui_studio.c"
#include "../firmware/src/ui_deck.c"
#include "../firmware/src/ui_song.c"
#include "../firmware/src/ui_draw.c"
#include "../firmware/src/ui_vis.c"
#include "../firmware/src/ui_layers.c"
#include "../firmware/src/ui_menu.c"
#include "../firmware/src/ui_input.c"
#include "../firmware/src/ui_live.c"
#include "../firmware/src/ui_break.c"
#include "../firmware/src/ui_chord.c"
#include "../firmware/src/ui_chop.c"
#include "../firmware/src/ui_bass.c"
#include "../firmware/src/splash.c"
static const char *outdir;
static void ppm(const char *name) {
    char path[512]; snprintf(path,sizeof path,"%s/%s.ppm",outdir,name);
    FILE *f=fopen(path,"wb"); assert(f); fprintf(f,"P6\n240 240\n255\n");
    for(unsigned i=0;i<240*240;i++) { uint16_t p=swap16(screen[i]); uint8_t rgb[3]={(p>>11)*255/31,((p>>5)&63)*255/63,(p&31)*255/31}; fwrite(rgb,1,3,f); }
    fclose(f);
}
/* one UI frame (~16 ms): the audio between (as the ISR does), then input, LEDs, draw */
static void frame(void)
{
    uint32_t q;
    static int32_t o[CTL * 2];
    for (q = 0; q < 22u; q++) {
        uint32_t i;
        mix_block(o, CTL);
        for (i = 1; i < CTL; i += 2u) {             /* the scope, as audio.c fills it (the visualiser) */
            scope_bufr[scope_w & (SCOPE_N - 1u)] = vis_tap[2u * i + 1u];
            scope_buf[scope_w++ & (SCOPE_N - 1u)] = vis_tap[2u * i];
        }
    }
    ui_input(); ui_leds(); ui_draw(); fm1_ms += 16;
    brkp_poll();                                    /* (the main loop's: the BREAK track's loops follow LOOP A..D) */
    cs_poll();
}
static void frames(uint32_t n) { while (n--) frame(); }
static uint32_t BT(uint32_t b) { return 1u << panel.btn[b]; }
static void press(uint32_t b) { edges_btn |= BT(b); fm1_in.buttons |= BT(b); frame(); }
static void release(uint32_t b) { fm1_in.buttons &= ~BT(b); frame(); }
static void tap(uint32_t b) { press(b); release(b); }
static void key(uint32_t k) { fm1_in.notes |= 1u << k; frame(); fm1_in.notes &= ~(1u << k); frame(); }
static int fails;
static void check(int ok, const char *what) { printf("ui: %-74s %s\n", what, ok ? "ok" : "FAIL"); fails += !ok; }
static const char *sub_line(void) { return layer_sub_shown; }   /* the layer's sub line as last drawn (ui_layers.c) */

#ifndef UI_PAGES_HARNESS_ONLY                       /* (tests/stress_test.c: the harness, its own main) */
int main(int argc, char **argv)
{
    uint32_t i;
    outdir = argc > 1 ? argv[1] : "build/host";
    {   /* the preset list by kind (ui.c BANK): every factory preset of every engine once, every name found */
        uint32_t e, k, n, hits;
        bank_resolve();
        for (n = 0; n < NBANK; n++)
            if (bank_pi[n] == 0xFF) {
                printf("ui: BANK %s: no such preset in engine %u\n", BANK[n].name, BANK[n].e);
                fails++;
            }
        for (e = 0; e < NENGINES; e++)
            for (k = 0; k < ENGINES[e]->npresets; k++) {
                for (hits = 0, n = 0; n < NBANK; n++)
                    hits += BANK[n].e == e && bank_pi[n] == k;
                if (hits != 1) {
                    printf("ui: preset %s (engine %u) is %u times in BANK\n", ENGINES[e]->presets[k].name, e, hits);
                    fails++;
                }
            }
    }
    {   /* FMX: each part browses its kinds (BASS the basses, CHORDS pads / keys / organs / stabs / the rest, CHOP's INST
         * the leads); every factory entry on exactly one part; the defaults are on their part's list */
        uint32_t p, n, total, seen[64] = {0}, bad = 0, k;
        static const uint16_t MASK[NPART] = {1u << BK_BASS,
                                             1u << BK_PAD | 1u << BK_KEYS | 1u << BK_ORGAN | 1u << BK_STAB | 1u << BK_FX,
                                             1u << BK_LEAD};
        trk[2].p[P_X0] = CM_INST;                       /* (the CHOP in INST: the instruments; else its kits) */
        for (p = 0; p < NPART; p++) {
            song.sel = (uint8_t)p;
            preset_pos(&total);
            for (n = 0; n < bank_nvis(); n++) {
                uint32_t b = bank_vis(n);
                bad += !((MASK[p] >> BANK[b].kind) & 1u);
                seen[b % 64u]++;
                bad += preset_at(n, &k) != BANK[b].e || k != bank_pi[b];
            }
            bad += total != bank_nvis() + up_count();
        }
        for (n = 0; n < NBANK; n++)
            bad += seen[n] != (BANK[n].kind == BK_KIT ? 0u : 1u);
        trk[2].p[P_X0] = CM_PADS;
        song.sel = 2;
        kit_nth(0, &n);
        bad += (n && preset_at(0, &k) != PRESET_KIT) || (preset_pos(&total), total != n);   /* (no packs yet: none) */
        song.sel = 0;
        check(!bad && NBANK <= 64u, "PRESETS: BASS the basses, CHORDS pads / keys / organs / stabs, CHOP INST the leads, CHOP PADS its kits");
        check(BANK[bank_vis(0)].kind == BK_BASS, "PRESETS on track 1: a bass first");
    }
    panel = PANEL_DEFAULT;
    live_on = 0;                                       /* (STUDIO: the pages below; LIVE is tested at the end) */
    layers_init();
    settings.palette = 0;
    palette_set(0);
    host_tracks_init();
    check(host_pack(0, "RAVEHITS") && host_pack(1, "ANIME"), "the sound packs RAVEHITS and ANIME in USR1 and USR2 (web/packs)");
    for (i = 0; i < NPART; i++) { set_engine_of(&trk[i], TRK_DEF[i][0]); apply_preset_to(&trk[i], TRK_DEF[i][1]); trk[i].engine = trk[i].eng_req; }
    boot_splash(); ppm("page-splash");
    boot_frame(BOOT_NF - 1u); ppm("page-splash-end");
    ui.menu = 2; ui.force = 1; frame(); ppm("page-about"); ui.menu = 0;
    go_home(); ui.force = 1; frame(); ppm("page-tracks");
    {   /* FMX: the DECK (the home screen): KNOB 1..4 the macros FILTER, MANGLE, BASS (the phrase's MOVE), SPACE */
        int16_t f0 = song.g[G_FILT], m0 = trk[TRK_BRK].p[BPP[BP_MANGLE]], sp0 = song.g[G_SPACE];
        uint8_t mv0 = bl.move;
        check(cur_page()->scope == SC_TRK && song.g[G_SPACE] == 64, "DECK: the home screen; SPACE 64 (the reverb as before)");
        encs[panel.enc[EN_K1]] = -10; frame();
        encs[panel.enc[EN_K2]] = 20; frame();
        encs[panel.enc[EN_K4]] = 6; frame();
        check(song.g[G_FILT] < f0 && trk[TRK_BRK].p[BPP[BP_MANGLE]] > m0 && song.g[G_SPACE] > sp0,
              "DECK: KNOB 1 FILTER (a low-pass), KNOB 2 the BREAK's MANGLE, KNOB 4 SPACE");
        bl.mix &= 0x7Fu;                               /* (the bass's steps its own: not the generator's) */
        encs[panel.enc[EN_K3]] = 5; frame();
        check(bl.move == mv0, "DECK: KNOB 3 BASS leaves steps the generator did not write");
        bl_write();
        encs[panel.enc[EN_K3]] = 5; frame();
        check(bl.move > mv0, "DECK: KNOB 3 BASS: the phrase's MOVE, written again");
        ui.force = 1; frame(); ppm("page-deck");
        song.g[G_FILT] = f0; trk[TRK_BRK].p[BPP[BP_MANGLE]] = m0; song.g[G_SPACE] = sp0;
        bl_defaults(); memset(trk[0].step, 0, sizeof trk[0].step);
        ui.msg_t = 0; frames(2);
    }
    open_family(FAM_ENV); ui.force = 1; ui.hot_col = 1; ui.hot_t = 30; frame(); ppm("page-env");
    open_family(FAM_EDIT); ui.force = 1; frame(); ppm("page-edit");
    open_family(FAM_FX); ui.force = 1; frame(); ppm("page-fx");
    open_family(FAM_SEQ); ui.force = 1; frame(); ppm("page-step");
    open_family(FAM_GLO); ui.force = 1; frame(); ppm("page-global");
    open_family(FAM_GLO); ui.force = 1; frame(); ppm("page-master");
    open_family(FAM_SCL); ui.force = 1; frame(); ppm("page-scale");
    {   /* the sound's pages are the EDIT family's now (FMX: the songs are on the SONG screen): PRESETS, USER, TOOLS */
        uint32_t i, n = 0;
        for (i = 0; i < NPAGES; i++)
            n += PAGES[i].fam == FAM_SAVE || !strcmp(PAGES[i].title, "PROJECT");
        check(!n, "no SAVE family and no PROJECT page: the songs are on the SONG screen");
        for (i = 0; i < NPAGES && strcmp(PAGES[i].title, "TOOLS"); i++)
            ;
        check(i < NPAGES && PAGES[i].fam == FAM_EDIT && PAGES[i].id[3] == 0xFF, "TOOLS (clear, init): with EDIT, no NEW there");
        for (i = 0; i < NPAGES && strcmp(PAGES[i].title, "USER"); i++)
            ;
        check(i < NPAGES && PAGES[i].fam == FAM_EDIT, "USER presets: with EDIT");
    }
    {   /* (2.4) GLO > SYSTEM: KNOB 1 is MIDI OUT (KEYS / SEQ), shown as such; the USB status in the third column */
        uint32_t i, m0 = (uint32_t)song.g[G_MIDI];
        for (i = 0; i < NPAGES; i++) if (!strcmp(PAGES[i].title, "SYSTEM")) break;
        open_family(FAM_GLO); ui.page = (uint8_t)i; ui.fam_last[FAM_GLO] = (uint8_t)i; page_entered(); ui.force = 1; frame();
        song.g[G_MIDI] = 0; ui.force = 1; frame();
        encs[panel.enc[EN_K1]] = 1; frame();
        check(i < NPAGES && song.g[G_MIDI] == 1, "GLO > SYSTEM: KNOB 1 sets MIDI = SEQ");
        {
            char val[12]; const char *unit = "";
            int16_t *vp; const param_desc_t *d = page_desc(cur_page(), 0, &vp);
            param_format(d, *vp, val, &unit);
            check(d && !strcmp(d->label, "MIDI") && !strcmp(val, "SEQ"), "GLO > SYSTEM: the first column reads MIDI SEQ");
        }
        ui.force = 1; frame(); ppm("page-system");
        {   /* (2.4) KNOB 3: IN = CLOCK (notes ignored), kept in the settings word; back to NOTES */
            int16_t r0 = song.g[G_ROUTE];
            int16_t *vp; const param_desc_t *d = page_desc(cur_page(), 2, &vp);
            char val[12]; const char *unit = "";
            encs[panel.enc[EN_K3]] = 1; frame();
            param_format(d, *vp, val, &unit);
            check(song.g[G_ROUTE] == 1 && !strcmp(d->label, "IN") && !strcmp(val, "CLOCK"), "GLO > SYSTEM: KNOB 3 sets IN = CLOCK");
            lights_min = 1;
            check((lights_word() >> 15 & 1u) == 1u, "IN = CLOCK kept in the settings word (bit 15)");
            lights_from_word(lights_word() & ~(1u << 15));
            check(lights_min == 0u, "IN read back from the settings word");
            encs[panel.enc[EN_K3]] = -1; frame();
            song.g[G_ROUTE] = r0;
        }
        song.g[G_MIDI] = (int16_t)m0; go_home(); frame();
    }

    /* ---- taps open pages, holds are layers */
    go_home(); ui.force = 1; frame();
    { uint8_t was = song.sel; song.sel = 0; check(keys_guide() == 0u, "synth track, no layer: no landmarks (a piano)"); song.sel = was; }
    tap(B_SEQ); check(cur_fam() == FAM_SEQ, "SEQ tapped: the SEQ pages");
    go_home(); frame();
    tap(B_FX); check(cur_fam() == FAM_FX, "FX tapped: the FX pages");
    go_home(); frame();
    press(B_FX); frames(12); check(ui.layer == LY_FX, "FX held: the FX panel shows (LIVE's, lfx.c)");
    check(keys_guide() == (1u << key_of_white(0) | 1u << key_of_white(4) | 1u << key_of_white(8) | 1u << key_of_white(12)),
          "FX held: keys 1, 5, 9, 13 lit dim (the rows of the grid)");
    ppm("layer-fx");
    fm1_in.notes = 1u << key_of_white(0); frames(3);
    check((lfx.on & 1u) && lfx.tgt[0] == 15u && lfx.rev[1][0] && lfx.rev[1][TRK_BRK] && !trk[0].step[0].n,
          "STUDIO, FX + white key 1: DUB VERB on all four (LIVE's effect), nothing recorded");
    check((keys_lit() >> key_of_white(0)) & 1u, "... its key lit");
    ui.force = 1; frame(); ppm("layer-fx-on");
    fm1_in.notes = 0; frames(3); check(!(lfx.on & 1u) && !lfx.held, "... let go: off");
    {   uint8_t a0 = lfx.amt, t0 = lfx.time;
        encs[panel.enc[EN_K1]] = -5; frame(); check(lfx.amt < a0, "FX + KNOB 1: AMOUNT");
        encs[panel.enc[EN_K2]] = -1; frame(); check(lfx.time == t0 - 1u, "FX + KNOB 2: TIME");
        lfx.amt = a0; lfx.time = t0; }
    release(B_FX); check(cur_page()->scope == SC_TRK && ui.layer == LY_PLAY, "FX used then let go: no FX page");

    /* ---- a layer locked open: held + HOME tapped; any other button (not PLAY, REC, OCT) lets it go */
    go_home(); frame();
    press(B_FX); frames(3); tap(B_HOME); release(B_FX); frames(3);
    check(ly_lock == LY_FX && ui.layer == LY_FX, "FX held + HOME: locked open, FX let go");
    check(cur_page()->scope == SC_TRK, "FX + HOME: no FX page, no HOME jump");
    fm1_in.notes = 1u << key_of_white(1); frames(3);
    check((lfx.on >> 1) & 1u, "locked FX + white key 2: ECHO THROW (no hands on FX)");
    fm1_in.notes = 0; frames(3); check(!((lfx.on >> 1) & 1u), "locked FX, key up: it ends");
    {   uint8_t t0 = lfx.tone;
        encs[panel.enc[EN_K3]] = 6; frame(); check(lfx.tone > t0, "locked FX + KNOB 3: TONE");
        lfx.tone = t0; }
    ui.force = 1; frame(); ppm("layer-locked");
    { uint8_t was = song.playing; tap(B_PLAY); frames(2);
      check(ly_lock == LY_FX && song.playing != was, "locked: PLAY plays and keeps the lock"); tap(B_PLAY); frames(2); }
    tap(B_ENV); frames(2);
    check(ly_lock == LY_PLAY && ui.layer == LY_PLAY && cur_page()->scope == SC_TRK,
          "locked, ENV pressed: unlocked, and only that (no ENV page)");
    press(B_FX); frames(3); tap(B_HOME); release(B_FX); frames(3);
    tap(B_HOME); frames(2);
    check(ly_lock == LY_PLAY && ui.layer == LY_PLAY && cur_page()->scope == SC_TRK, "locked, HOME tapped: unlocked");
    press(B_FX); frames(3); tap(B_HOME); release(B_FX); frames(3);
    tap(B_FX); frames(2);
    check(ly_lock == LY_PLAY && ui.layer == LY_PLAY && cur_fam() != FAM_FX, "locked, FX tapped: unlocked, no FX page");
    go_home(); frame();
    press(B_SEQ); frames(3); tap(B_HOME); release(B_SEQ); frames(3);
    check(ly_lock == LY_STEP && ui.layer == LY_STEP, "SEQ held + HOME: the steps locked open");
    ui.force = 1; frame(); ppm("layer-steps-locked");
    press(B_FX); frames(12);
    check(ly_lock == LY_PLAY && ui.layer == LY_FX, "locked SEQ, FX held: unlocked, the FX layer");
    release(B_FX); frames(2); check(ui.layer == LY_PLAY, "and FX let go: back to playing");
    press(B_FX); frames(3); tap(B_HOME); release(B_FX); frames(3);
    press(B_HOME); frames(50); release(B_HOME); frames(2);
    check(ui.menu && ly_lock == LY_PLAY, "locked, HOME held: the menu, unlocked");
    ui.menu = 0; ui.force = 1; frames(2);
    song.g[G_DUST] = 0; song.g[G_FILT] = 0;

    /* ---- SEQ layer: the BREAK track's steps on the white keys (ON: the generator's slice) */
    song.sel = TRK_BRK; go_home(); frame();
    fm1_irq_off(); steps_clear(TBRK); fm1_irq_on();   /* (every step off) */
    key(4);                                          /* A3: slice 3, played: the layer's slice */
    check(pen_lane == 2, "a key on the BREAK track played: its slice (white key 3: slice 3)");
    press(B_SEQ); frames(10);
    key(0); key(7); key(14);                          /* steps 1, 5, 9 */
    check(TBRK->bstep[0].on && TBRK->bstep[4].on && TBRK->bstep[8].on && !TBRK->bstep[1].on && !TBRK->bstep[4].slice,
          "SEQ + white keys 1, 5, 9: BREAK steps on (the generator's slice)");
    ppm("layer-steps");
    fm1_in.notes = 1u << 7 | 1u << 14; frame();       /* steps 5, 9 held + KNOB 1 / 2 / 3: slice, level, retrigger */
    encs[panel.enc[EN_K1]] = 3; frame();
    encs[panel.enc[EN_K2]] = 1; frame();
    encs[panel.enc[EN_K3]] = 2; frame();
    fm1_in.notes = 0; frame();
    check(TBRK->bstep[4].slice == 3u && TBRK->bstep[4].lvl == LV_HARD && TBRK->bstep[4].rat == 2u &&
          TBRK->bstep[8].slice == 3u && TBRK->bstep[8].on, "steps 5, 9 held + KNOB 1 / 2 / 3: slice 3, hard, retrigger x3");
    key(0); check(!TBRK->bstep[0].on, "step 1 again: off");
    release(B_SEQ); check(ui.layer == LY_PLAY && !on_break_page(), "SEQ used then let go: no page change (no BREAK screen)");

    /* ---- EDIT: undo / redo, erase, length */
    press(B_EDIT); frames(10);
    edges_btn |= BT(B_OCTDN); fm1_in.buttons |= BT(B_OCTDN); frame(); fm1_in.buttons &= ~BT(B_OCTDN); frame();
    check(!TBRK->bstep[4].on && !TBRK->bstep[8].on, "EDIT + OCT-: undo (the SEQ hold's steps gone)");
    edges_btn |= BT(B_OCTUP); fm1_in.buttons |= BT(B_OCTUP); frame(); fm1_in.buttons &= ~BT(B_OCTUP); frame();
    check(!TBRK->bstep[0].on && TBRK->bstep[4].lvl == LV_HARD && TBRK->bstep[8].slice == 3u, "EDIT + OCT+: redo (back, as left)");
    ppm("layer-erase");
    TBRK->bstep[12].on = 1; TBRK->bstep[12].slice = 6;   /* (a pin of another slice) */
    key(4);                                           /* stopped: every pin of slice 3 goes */
    check(!TBRK->bstep[4].slice && !TBRK->bstep[8].slice && TBRK->bstep[4].on && TBRK->bstep[8].on && TBRK->bstep[12].slice == 6u,
          "EDIT + slice 3 (stopped): its pins erased (the generator's again, still on), slice 6 kept");
    encs[panel.enc[EN_K2]] = 1; frame();
    check(TBRK->p[P_SLEN] == 32, "EDIT + KNOB 2: length x2");
    release(B_EDIT);
    edges_btn |= BT(B_EDIT); fm1_in.buttons |= BT(B_EDIT); frame();      /* undo the length too */
    edges_btn |= BT(B_OCTDN); fm1_in.buttons |= BT(B_OCTDN); frame(); fm1_in.buttons &= ~BT(B_OCTDN); frame();
    release(B_EDIT);
    check(TBRK->p[P_SLEN] == 16, "EDIT + OCT-: the length back to 16");

    /* ---- ARP layer: a roll, rate knob */
    press(B_ARP); frames(10);
    encs[panel.enc[EN_K1]] = 1; frame();
    check(song.g[G_ROLL] == 2, "ARP + KNOB 1: the roll rate (1/32)");
    fm1_in.notes = 1u << 7; frames(3); check(roll[0].on, "ARP + a key: it rolls");
    ppm("layer-roll");
    fm1_in.notes = 0; frame(); check(!roll[0].on, "key up: the roll ends");
    release(B_ARP);

    /* ---- SEL: the key of the song; GLO: mute, solo, tap */
    song.sel = 0; go_home(); frame();
    press(B_SCL); frames(10);
    key(9);                                           /* D4 */
    check(trk[0].p[P_ROOT] == 2 && trk[1].p[P_ROOT] == 2 && trk[2].p[P_ROOT] == 2, "SEL + D: every part in D");
    encs[panel.enc[EN_K1]] = 2; frame();
    check(trk[0].p[P_CHORD] == 2, "SEL + KNOB 1: chords (7TH) on the track");
    ppm("layer-key");
    release(B_SCL);
    press(B_GLO); frames(10);
    key(0); check(TBRK->p[P_MUTE] == 1 && !trk[0].p[P_MUTE], "GLO + key 1: the BREAK out (LIVE's lanes: BREAK BASS CHORDS CHOP)");
    key(9); check(song.solo == 1u, "GLO + key 6: the BASS soloed");
    ppm("layer-mix");
    for (i = 0; i < 4u; i++) { fm1_in.notes = 1u << 26; frame(); fm1_in.notes = 0; frames(36); }   /* ~0.6 s apart */
    check(song.g[G_BPM] >= 95 && song.g[G_BPM] <= 105, "GLO + the last key, tapped at ~0.6 s: ~100 BPM");
    key(0); key(9);
    check(!TBRK->p[P_MUTE] && !song.solo, "again: in, no solo");
    key(key_of_white(3)); key(key_of_white(7));
    check(trk[2].p[P_MUTE] == 1 && song.solo == 4u, "GLO + keys 4, 8: the CHOP out, soloed");
    { int32_t l0 = trk[TRK_BRK].p[P_LEVEL]; encs[panel.enc[EN_K1]] = -3; frame();
      check(trk[TRK_BRK].p[P_LEVEL] < l0, "GLO + KNOB 1: the BREAK's level"); trk[TRK_BRK].p[P_LEVEL] = (int16_t)l0; }
    key(key_of_white(3)); key(key_of_white(7));
    check(!trk[2].p[P_MUTE] && !song.solo, "again: in, no solo");
    /* (2.4) key 9 held: a fill; key 10: the next bar is one (again: cancelled) */
    fm1_in.notes = 1u << key_of_white(8); frame(); frame();
    check(fill_held == 1u && fill_now == 1u && (keys_lit() & (1u << key_of_white(8))) != 0, "GLO + key 9 held: FILL (the key lit)");
    ui.force = 1; frame(); ppm("layer-mix-fill");
    fm1_in.notes = 0; frame(); frame();
    check(fill_held == 0u && fill_now == 0u, "key 9 let go: the fill ends");
    key(key_of_white(9)); check(fill_arm == 1u && (keys_lit() & (1u << key_of_white(9))) != 0, "GLO + key 10: FILL NEXT BAR armed (the key lit)");
    key(key_of_white(9)); check(fill_arm == 0u, "GLO + key 10 again: cancelled");
    release(B_GLO);
    fm1_in.notes = 1u << key_of_white(8); frame();     /* (no layer: a plain key, no fill) */
    check(fill_held == 0u, "key 9 without GLO: no fill");
    fm1_in.notes = 0; frame();
    trk[0].p[P_CHORD] = 0;

    /* ---- SEQ layer (2.4): a step held + KNOB 4 nudges it, PRESETS locks the lock parameter (ALGORITHM picks it),
     * KNOB 1 still edits the note, OCT- clears nudge and locks; PRESETS / ALGORITHM browse nothing meanwhile */
    check(ui.lock_par == P_ED_FLT, "the lock parameter starts as ENV > FLT");
    song.sel = 0; go_home(); frames(16);              /* (KNOB 1..4 quiet ~250 ms after a layer: #39) */
    open_family(FAM_FX); frame();                     /* the FX page: KNOB 1 = DIST */
    encs[panel.enc[EN_K1]] = 2; frame();
    check(ui.lock_par == P_DIST && trk[0].p[P_DIST] > 0, "a sound knob turned (FX > DIST): the lock parameter");
    open_family(FAM_SEQ); frame();                    /* PATTERN page knobs (LEN, DIV..) do not lock */
    open_family(FAM_SEQ); frame();
    if (cur_page()->id[1] == P_SDIV) { encs[panel.enc[EN_K2]] = 1; frame(); encs[panel.enc[EN_K2]] = -1; frame(); }
    check(ui.lock_par == P_DIST, "a pattern knob (DIV) is not a lock parameter");
    go_home(); frame();
    press(B_SEQ); frames(10);
    key(7);                                           /* step 5 set with the pen */
    fm1_in.notes = 1u << 7; frame();                  /* step 5 held */
    {
        uint32_t n0 = trk[0].step[4].note[0];
        encs[panel.enc[EN_K1]] = 2; frame();
        check(trk[0].step[4].note[0] == n0 + 2u && trk[0].step[4].n == 1, "step 5 held + KNOB 1: the note two up (as in 2.3)");
    }
    encs[panel.enc[EN_K4]] = -3; frame();
    encs[panel.enc[EN_K4]] = -4; frame();
    check(trk[0].micro[4] == -7 && trk[0].step[4].n == 1, "step 5 held + KNOB 4: nudged -7 (the step kept)");
    {
        int k0 = lock_find(&trk[0], 4, P_DIST, 0);
        int32_t d0 = trk[0].p[P_DIST];
        uint32_t sel0 = song.sel, eng0 = trk[0].eng_req, pr0 = trk[0].preset;
        encs[panel.enc[EN_PRESET]] = 1; frame();
        {
            int k = lock_find(&trk[0], 4, P_DIST, 0);
            check(k0 < 0 && k >= 0 && trk[0].lock[k].val == d0 + 1, "step 5 held + PRESETS: a DIST lock at the track's value, moved one up");
            encs[panel.enc[EN_PRESET]] = 5; frame();
            check(k >= 0 && trk[0].lock[k].val > d0 + 1 && trk[0].p[P_DIST] == d0, "PRESETS again: the lock moves, the track's DIST does not");
            check(trk[0].eng_req == eng0 && trk[0].preset == pr0, "PRESETS with a step held browses no sound");
        }
        ui.force = 1; frame(); ppm("layer-steps-lock");
        check(step_locked(&trk[0], 4) && !step_locked(&trk[0], 5), "the step shows its mark (a nudge or a lock)");
        encs[panel.enc[EN_ALGO]] = 1; frame();
        check(ui.lock_par == P_CHOR && song.sel == sel0, "step held + ALGORITHM: the lock parameter (DIST -> CHO), the track stays selected");
        encs[panel.enc[EN_ALGO]] = -1; frame(); encs[panel.enc[EN_ALGO]] = -1; frame();
        check(ui.lock_par == P_SGATE && song.sel == sel0, "ALGORITHM back twice: CHO -> DIST -> GATE (LEN, DIV, SWG skipped)");
        encs[panel.enc[EN_PRESET]] = 1; frame();
        check(lock_find(&trk[0], 4, P_SGATE, 0) >= 0 && lock_find(&trk[0], 4, P_DIST, 0) >= 0, "PRESETS: a second lock (GATE) on the same step");
        for (i = 0; i < 80u; i++) { encs[panel.enc[EN_ALGO]] = 1; frame(); if (ui.lock_par == P_SGATE) break; }
        check(i > 0 && i < 80u && ui.lock_par == P_SGATE, "ALGORITHM wraps round through the lockable parameters");
        encs[panel.enc[EN_K4]] = -40; frame();
        check(trk[0].micro[4] == MICRO_MIN, "KNOB 4 bounded at -32");
        edges_btn |= BT(B_OCTDN); fm1_in.buttons |= BT(B_OCTDN); frame(); fm1_in.buttons &= ~BT(B_OCTDN); frame();
        check(trk[0].micro[4] == 0 && lock_find(&trk[0], 4, P_DIST, 0) < 0 && lock_find(&trk[0], 4, P_SGATE, 0) < 0 &&
              trk[0].step[4].n == 1 && ui.step_page == 0, "step held + OCT-: its nudge and locks cleared, the step kept, no page change");
    }
    {   /* (2.4) OCT+ with the step held: its fill condition round; OCT- resets it with the nudge and locks */
        edges_btn |= BT(B_OCTUP); fm1_in.buttons |= BT(B_OCTUP); frame(); fm1_in.buttons &= ~BT(B_OCTUP); frame();
        check(step_fill(&trk[0], 4) == FC_FILL && ui.step_page == 0 && trk[0].step[4].n == 1, "step held + OCT+: FILL ONLY (no page change, the step kept)");
        ui.force = 1; frame(); ppm("layer-steps-fill");
        edges_btn |= BT(B_OCTUP); fm1_in.buttons |= BT(B_OCTUP); frame(); fm1_in.buttons &= ~BT(B_OCTUP); frame();
        check(step_fill(&trk[0], 4) == FC_NOFILL, "OCT+ again: NO FILL");
        ui.force = 1; frame(); ppm("layer-steps-nofill");
        edges_btn |= BT(B_OCTUP); fm1_in.buttons |= BT(B_OCTUP); frame(); fm1_in.buttons &= ~BT(B_OCTUP); frame();
        check(step_fill(&trk[0], 4) == FC_NORM, "OCT+ again: normal");
        edges_btn |= BT(B_OCTUP); fm1_in.buttons |= BT(B_OCTUP); frame(); fm1_in.buttons &= ~BT(B_OCTUP); frame();
        encs[panel.enc[EN_K4]] = 3; frame();
        edges_btn |= BT(B_OCTDN); fm1_in.buttons |= BT(B_OCTDN); frame(); fm1_in.buttons &= ~BT(B_OCTDN); frame();
        check(step_fill(&trk[0], 4) == FC_NORM && trk[0].micro[4] == 0, "FILL ONLY + a nudge, OCT-: the condition and the nudge reset");
    }
    fm1_in.notes = 0; frame();
    check(trk[0].step[4].n == 1, "the step edited is kept when let go");
    fm1_in.notes = 1u << 7; frame(); encs[panel.enc[EN_PRESET]] = 1; frame(); fm1_in.notes = 0; frame();
    check(trk[0].step[4].n == 1 && lock_find(&trk[0], 4, P_SGATE, 0) >= 0, "a set step held + PRESETS: a lock, the step kept");
    fm1_in.notes = 1u << 7; frame();
    edges_btn |= BT(B_OCTUP); fm1_in.buttons |= BT(B_OCTUP); frame(); fm1_in.buttons &= ~BT(B_OCTUP); frame();
    fm1_in.notes = 0; frame();
    key(7);                                           /* a set step pressed and let go: cleared, with its locks and condition */
    check(trk[0].step[4].n == 0 && lock_find(&trk[0], 4, P_SGATE, 0) < 0 && step_fill(&trk[0], 4) == FC_NORM,
          "a step cleared: its locks and fill condition go too");
    { uint32_t sel0 = song.sel; encs[panel.enc[EN_ALGO]] = 1; frame(); check(song.sel == sel0 && ui.lock_par == P_SGATE, "no step held, SEQ held: ALGORITHM does nothing (no track change)"); }
    release(B_SEQ);
    { uint32_t sel0 = song.sel; encs[panel.enc[EN_ALGO]] = 1; frame(); check(song.sel == sel0 + 1u, "SEQ let go: ALGORITHM selects the track again"); song.sel = (uint8_t)sel0; }
    song.sel = TRK_BRK; go_home(); frame();
    press(B_SEQ); frames(10);
    key(0);                                           /* a BREAK step: KNOB 1 its own slice, KNOB 4 its nudge */
    fm1_in.notes = 1u << 0; frame();
    encs[panel.enc[EN_K1]] = 2; frame();
    check(TBRK->bstep[0].on && TBRK->bstep[0].slice == 2u, "the BREAK track: step 1 held + KNOB 1: its own slice (2)");
    encs[panel.enc[EN_K4]] = 5; frame();
    encs[panel.enc[EN_PRESET]] = 1; frame();
    check(TBRK->micro[0] == 5 && lock_find(TBRK, 0, P_E0, 0) >= 0 && ui.lock_par == P_E0 && TBRK->p[P_LRATE] == 0,
          "the BREAK track: step 1 held + KNOB 4 / PRESETS: nudge +5, a CMPLX lock (not GATE), loop A untouched");
    encs[panel.enc[EN_ALGO]] = 1; frame();
    check(ui.lock_par != P_E0 && brk_lockable(ui.lock_par) && song.sel == TRK_BRK,
          "the BREAK track: step held + ALGORITHM: the next of its lockable parameters");
    fm1_in.notes = 0; frame();
    edges_btn |= BT(B_OCTDN); fm1_in.buttons |= BT(B_OCTDN); frame(); fm1_in.buttons &= ~BT(B_OCTDN); frame();
    check(TBRK->micro[0] == 5 && ui.step_page == 0, "no step held + OCT-: the page (not a clear)");
    release(B_SEQ);
    fm1_irq_off(); steps_clear(TBRK); steps_clear(&trk[0]); fm1_irq_on();
    check(!TBRK->bstep[0].on && lock_find(TBRK, 0, P_E0, 0) < 0 && !TBRK->micro[0], "the BREAK track cleared: every step off, no lock, no nudge");
    trk[0].p[P_DIST] = 0;
    ui.lock_par = P_ED_FLT;
    song.sel = 0; go_home(); frame();

    /* ---- the BREAK track off its screen, playing and recording: a key's slice into the step it falls on, OCT- / OCT+
     * held ghost / hard, MIDI channel 10, a roll (one hit a step, its retrigger); EDIT + a key held playing: its pins go */
    {
        uint32_t j, n[5] = {0, 0, 0, 0, 0}, h0;
        #define BRK_COUNT() do { memset(n, 0, sizeof n); \
            for (j = 0; j < 16u; j++) { const bstep_t *b = &TBRK->bstep[j]; if (!b->on) continue; \
                n[0] += b->slice == 1u && b->lvl == LV_GHOST; n[1] += b->slice == 2u && b->lvl == LV_HARD; \
                n[2] += b->slice == 3u && b->lvl == LV_NORM; n[3] += b->slice == 4u && b->lvl == LV_HARD; \
                n[4] += b->slice == 6u && b->rat == 1u; } } while (0)
        song.sel = TRK_BRK; go_home(); frames(16);
        transport_req = 1; frames(4);
        tap(B_REC);
        check(song.playing && song.rec == 1u << TRK_BRK, "the BREAK track, playing: REC records it");
        h0 = bp.st_hits;
        press(B_OCTDN); key(key_of_white(0)); release(B_OCTDN); frames(12);   /* slice 1, ghost */
        check(song.octave == 0 && bp.st_hits == h0 + 1u, "OCT- held + a key on the BREAK track: a hit (no octave change)");
        press(B_OCTUP); key(key_of_white(1)); release(B_OCTUP); frames(12);   /* slice 2, hard */
        key(key_of_white(2)); frames(12);                                      /* slice 3, as played */
        midi_in_q[mi_w % MQ] = 0x9u | 0x99u << 8 | 39u << 16 | 120u << 24; mi_w++; frame();   /* channel 10: slice 4 */
        midi_in_q[mi_w % MQ] = 0x8u | 0x89u << 8 | 39u << 16; mi_w++; frames(12);
        BRK_COUNT();
        check(n[0] == 1 && n[1] == 1 && n[2] == 1, "REC: three keys, three steps: slice 1 ghost (OCT-), 2 hard (OCT+), 3 as played");
        check(n[3] == 1 && pen_lane == 3, "REC: MIDI channel 10, note 39 velocity 120: slice 4, hard");
        song.g[G_ROLL] = 2;                            /* 1/32 on 1/16 steps: x2 */
        press(B_ARP); frames(3);
        fm1_in.notes = 1u << key_of_white(5); frames(50);
        fm1_in.notes = 0; frame(); release(B_ARP);
        BRK_COUNT();
        check(n[4] >= 4u && n[0] == 1, "REC + ARP + a key: the roll records one hit a step (slice 6, x2), the rest kept");
        ui.force = 1; frame(); ppm("live-break-rec");
        tap(B_REC);
        press(B_EDIT); frames(3);
        fm1_in.notes = 1u << key_of_white(5); frames(140);          /* held over a bar (2 s at 120) */
        fm1_in.notes = 0; frame(); release(B_EDIT);
        BRK_COUNT();
        for (h0 = 0, j = 0; j < 16u; j++) h0 += TBRK->bstep[j].slice == 6u;
        check(!song.rec && h0 == 0 && n[0] == 1 && n[1] == 1 && n[3] == 1, "EDIT + slice 6 held a bar, playing: its pins gone as they pass, the others kept");
        transport_req = 2; frames(4);
        fm1_irq_off(); steps_clear(TBRK); fm1_irq_on();
        song.sel = 0; go_home(); frame();
        #undef BRK_COUNT
    }

    /* ---- REC: press arms; held: the ring; to the end: cleared */
    go_home(); frame();
    song.playing = 0; song.rec = 0; rec_wait = 0;
    tap(B_REC); check(rec_wait == 1, "REC tapped (stopped): armed");
    ui.force = 1; frame(); ppm("live-rec-ready");
    tap(B_REC); check(rec_wait == 0, "REC again: cancelled");
    trk[0].step[3].n = 1, trk[0].step[3].note[0] = 60, trk[0].step[3].time = ST_NOTE;
    press(B_REC); frames(50);                         /* 0.8 s: the ring */
    check(ui.hold_kind == 1u && rec_wait == 0, "REC held: the press undone, the clear ring");
    ppm("hold-clear");
    frames(90);                                       /* to the end */
    check(!trk[0].step[3].n && ui.hold_kind == 0, "REC held to the end: track 1 cleared");
    release(B_REC);
    press(B_EDIT); frames(10);
    edges_btn |= BT(B_OCTDN); fm1_in.buttons |= BT(B_OCTDN); frame(); fm1_in.buttons &= ~BT(B_OCTDN); frame();
    release(B_EDIT);
    check(trk[0].step[3].n == 1, "EDIT + OCT-: the cleared track back");
    press(B_REC); frames(60); release(B_REC);         /* let go before the end: nothing */
    check(trk[0].step[3].n == 1 && !rec_wait, "REC let go before the end: nothing cleared");

    /* ---- SAVE: the song page; held, no layer (the sections are LIVE's) */
    go_home(); frame();
    song.playing = 0; arrangement_enabled = 0;
    arr_defaults(&arrangement);
    as_set(1, 0, 0);
    check(arrangement.count == 6u && as_total(&arrangement) == 64u && arrangement.entry[0].scene == AR_INTRO &&
          arrangement.entry[5].scene == AR_OUTRO, "AUTO SONG: CLUB, VARIANT 0: INTRO .. OUTRO, 64 bars");
    press(B_SAVE); frames(15);
    check(ui.layer == LY_PLAY && on_song_page() && sg_page == SG_SET && saves == 0,
          "SAVE held: no layer, the song screen's SET page (on the press), nothing saved");
    { uint32_t n0 = arrangement.count, j0 = (uint32_t)arrangement_clock.jump, e0 = arrangement_enabled;
      key(key_of_white(12)); key(key_of_white(3));
      check(arrangement.count == n0 && (uint32_t)arrangement_clock.jump == j0 && arrangement_enabled == e0 && as_var() == 0u,
            "SAVE held + white keys: no sections, no song mode (the old SONG layer is gone)"); }
    release(B_SAVE);
    go_home(); frame();
    tap(B_SAVE); check(on_song_page() && sg_page == SG_SET, "SAVE tapped on TRACKS: the song screen, the SET page");
    frames(16);
    encs[panel.enc[EN_SELECT]] = 1; frame();
    check(sg_page == SG_CHAIN, "song page: SELECT right: the CHAIN page");
    encs[panel.enc[EN_K1]] = 1; frame();
    check(as_form() == 2u && as_total(&arrangement) >= 112u, "song page: KNOB 1 FORM LONG");
    encs[panel.enc[EN_K2]] = 1; frame();
    check(as_var() == 1u, "... KNOB 2 VARIANT");
    encs[panel.enc[EN_K4]] = -9; frame();
    check(sg_start() == AR_INTRO && arr_armed == AR_INTRO && !arrangement_enabled, "... KNOB 4 START: back to the INTRO (no song mode)");
    encs[panel.enc[EN_K4]] = 1; frame();
    check(sg_start() > AR_INTRO && sg_in_chain(sg_start()) && arr_armed == (int8_t)sg_start(),
          "... KNOB 4 START: the chain's next section (LIVE's PLAY from there)");
    ui.force = 1; frame(); ppm("page-song");
    arr_armed = -1;
    {   /* the cards: SELECT the page, ALGORITHM the section, the knobs the card (project.c sec_card) */
        uint32_t was_total;
        memset(sec_card, 0, sizeof sec_card);
        arrangement_enabled = 0;
        encs[panel.enc[EN_SELECT]] = 1; frame();
        check(sg_page == SG_CARD && sg_role == 0u, "song page: SELECT right: the CARD page, INTRO");
        encs[panel.enc[EN_ALGO]] = 1; frame();
        encs[panel.enc[EN_ALGO]] = 1; frame();
        check(sg_role == AR_DROP, "... ALGORITHM right twice: the DROP");
        was_total = as_total(&arrangement);
        encs[panel.enc[EN_K1]] = 4; frame();
        check(sec_card[AR_DROP].len == 4u && as_total(&arrangement) != was_total, "... KNOB 1 LENGTH 32 bars: the chain made again");
        encs[panel.enc[EN_K2]] = 2; frame();
        encs[panel.enc[EN_K3]] = 3; frame();
        encs[panel.enc[EN_K4]] = 2; frame();
        check(sec_card[AR_DROP].loop == 2u && sec_card[AR_DROP].style == 3u && sec_card[AR_DROP].bass == 2u &&
              !sec_card[AR_INTRO].len && !sec_card[AR_BUILD].loop,
              "... KNOB 2 LOOP B, 3 STYLE, 4 BASS: the DROP's card only");
        encs[panel.enc[EN_K1]] = -9; frame();
        check(!sec_card[AR_DROP].len, "... KNOB 1 back: LENGTH the FORM's (0), stops at the end");
        ui.force = 1; frame(); ppm("page-song-card");
        encs[panel.enc[EN_SELECT]] = 1; frame();
        check(sg_page == SG_TRACKS, "... SELECT right: the TRACKS page");
        encs[panel.enc[EN_K1]] = 2; frame();
        encs[panel.enc[EN_K2]] = 1; frame();
        encs[panel.enc[EN_K2]] = 1; frame();
        encs[panel.enc[EN_K3]] = 9; frame();
        encs[panel.enc[EN_K4]] = -1; frame();
        check(as_who(AR_DROP, 0) == 2u && as_who(AR_DROP, 1) == 2u && as_who(AR_DROP, 2) == 3u && as_who(AR_DROP, 3) == 0u,
              "... KNOBS: BASS OFF, CHORDS OFF, CHOP under water, BREAK as the role says");
        ui.force = 1; frame(); ppm("page-song-tracks");
        encs[panel.enc[EN_SELECT]] = -9; frame();
        check(sg_page == SG_SET, "... SELECT left: back to the SET page");
        memset(sec_card, 0, sizeof sec_card);
        sg_role = 0;
        as_set(as_form(), as_var(), as_endless());
    }
    arrangement_enabled = 0;
    {   /* SAVE tapped, from anywhere in STUDIO: the SONG screen, on the SET page (the songs of the set) */
        go_home(); ui.force = 1; frames(16);
        tap(B_ENV); frames(2);
        check(cur_page()->fam == FAM_ENV, "SAVE: off the DECK, the ENV pages");
        sg_page = 2;
        tap(B_SAVE); frames(2);
        check(on_song_page() && sg_page == SG_SET, "... SAVE tapped there: the SONG screen, the SET page");
        ui.force = 1; frame(); ppm("page-song-set");
        tap(B_SAVE); frames(2);
        check(on_song_page() && sg_page == SG_SET, "... SAVE again on it: still the SET page");
        {   /* the set: KNOB 1 the slot; LOAD, SAVE, NEW are GO knobs, turned twice */
            int16_t s0 = song.g[G_SLOT];
            uint32_t l0 = loads, v0 = saves;
            frames(16);
            song.g[G_SLOT] = 1;
            encs[panel.enc[EN_K1]] = 2; frame();
            check(song.g[G_SLOT] == 3, "SET: KNOB 1 the slot (3)");
            encs[panel.enc[EN_K2]] = 1; frame();
            check(loads == l0 && ui.arm == 0xD1u, "... KNOB 2 once: LOAD armed, nothing loaded yet");
            encs[panel.enc[EN_K2]] = -1; frame();
            check(!ui.arm && loads == l0, "... KNOB 2 back: disarmed");
            encs[panel.enc[EN_K2]] = 1; frame();
            encs[panel.enc[EN_K2]] = 1; frame();
            check(loads == l0 + 1u && !ui.arm, "... KNOB 2 twice: LOAD the slot");
            encs[panel.enc[EN_K3]] = 1; frame();
            check(saves == v0 + 1u && !ui.arm, "... KNOB 3 once: SAVE into an empty slot (3) at once");
            song.g[G_SLOT] = 1;
            encs[panel.enc[EN_K3]] = 1; frame();
            check(saves == v0 + 1u && ui.arm == 0xD2u, "... KNOB 3 once on a used slot (1): SAVE armed, nothing written");
            encs[panel.enc[EN_K3]] = 1; frame();
            check(saves == v0 + 2u, "... KNOB 3 twice: SAVE over the song in the slot");
            song.g[G_SLOT] = 3;
            sec_card[AR_DROP].len = 2;
            encs[panel.enc[EN_K4]] = 1; frame();
            check(sec_card[AR_DROP].len == 2u && ui.arm == 0xD3u, "... KNOB 4 once: NEW armed, the song still there");
            ui.force = 1; frame(); ppm("page-song-set-armed");
            encs[panel.enc[EN_K4]] = -1; frame();
            sec_card[AR_DROP].len = 0;
            song.g[G_SLOT] = s0;
            as_set(as_form(), as_var(), as_endless());
        }
    }

    /* ---- the BREAK screen, reached as a user does: track 4 on TRACKS (ALGORITHM), SEQ tapped */
    song.sel = 0; go_home(); ui.force = 1; frames(16);   /* (after SAVE: the knobs quiet, #39) */
    for (i = 0; i < 3u; i++) { encs[panel.enc[EN_ALGO]] = 1; frame(); }
    check(song.sel == 2u, "TRACKS + ALGORITHM right: down the lanes, BASS CHORDS CHOP, stopping at the last");
    for (i = 0; i < 4u; i++) { encs[panel.enc[EN_ALGO]] = -1; frame(); }
    check(song.sel == TRK_BRK && !on_break_page() && cur_page()->scope == SC_TRK, "TRACKS + ALGORITHM left: up to the top lane, the BREAK track, TRACKS kept");
    {
        char nm[16];
        trk_short_name(TRK_BRK, nm);
        check(!strcmp(nm, "AMEN A"), "TRACKS: the BREAK track's column names its loop A (AMEN A)");
        encs[panel.enc[EN_PRESET]] = 1; frame();
        trk_short_name(TRK_BRK, nm);
        check(TBRK->p[P_LRATE] == 1 && !strcmp(nm, "AMEN B") && brkp_bank_src(0), "TRACKS, the BREAK track + PRESETS: loop A AMEN B (built)");
        ui.force = 1; frame(); ppm("page-tracks-break");
        encs[panel.enc[EN_PRESET]] = -1; frame();
    }
    tap(B_SEQ);
    check(on_break_page() && bk_page == BK_STYLE, "TRACKS, the BREAK track, SEQ tapped: the BREAK screen, STYLE");
    {   /* its pages: the buttons, SELECT */
        static const struct { uint8_t b, pg; const char *what; } PG[] = {
            {B_ARP, BK_RETRIG, "ARP: RETRIG (2X 3X 4X 8X)"}, {B_EDIT, BK_LOOPS, "EDIT: LOOPS (LOOP A..D)"},
            {B_EDIT, BK_BARS, "EDIT again: BARS (A..D)"}, {B_SCL, BK_PHRASE, "SEL: PHRASE"}, {B_GLO, BK_MIX, "GLO: MIX"},
            {B_ENV, BK_TUNE, "ENV: TUNE"}, {B_LFO, BK_PATTERN, "LFO: PATTERN"}, {B_SEQ, BK_STYLE, "SEQ: STYLE"},
            {B_SEQ, BK_GROOVE, "SEQ again: GROOVE"}};
        char what[96];
        uint32_t k;
        for (k = 0; k < sizeof PG / sizeof PG[0]; k++) {
            tap(PG[k].b);
            snprintf(what, sizeof what, "BREAK screen, %s", PG[k].what);
            check(on_break_page() && bk_page == PG[k].pg, what);
        }
        encs[panel.enc[EN_SELECT]] = 1; frame();
        check(bk_page == BK_RETRIG, "BREAK screen: SELECT right: the next page");
        encs[panel.enc[EN_SELECT]] = -1; frame();
        encs[panel.enc[EN_SELECT]] = -1; frame();
        encs[panel.enc[EN_SELECT]] = -1; frame();
        check(bk_page == BK_STYLE && on_break_page(), "BREAK screen: SELECT left: stops at the first (STYLE)");
    }
    {   /* STYLE: KNOB 1 sets the generator's starting point at once; MANGL is KNOB 2 */
        encs[panel.enc[EN_K1]] = 2; frame();
        check(TBRK->p[P_PRIO] == BS_BRKCORE && TBRK->p[P_E0] == 85 && TBRK->p[P_ED_FLT] == 55 && TBRK->p[P_SDIV] == 2,
              "STYLE: KNOB 1 right twice: BRKCORE, CMPLX 85, MANGL 55");
        encs[panel.enc[EN_K1]] = -1; frame();
        check(TBRK->p[P_PRIO] == BS_HALFTIME && TBRK->p[P_SDIV] == 1, "STYLE: HALFTIME: the steps 1/8 (the loop at half speed)");
        encs[panel.enc[EN_K2]] = 3; frame();
        check(TBRK->p[P_ED_FLT] > 5, "STYLE: KNOB 2 MANGL up");
        ui.force = 1; frame(); ppm("page-break-style");
        encs[panel.enc[EN_K1]] = -1; frame();
        check(TBRK->p[P_PRIO] == BS_JUNGLE && TBRK->p[P_E0] == 55 && TBRK->p[P_SDIV] == 2, "STYLE: back to JUNGLE (its own MANGL)");
        tap(B_SEQ);
        check(bk_page == BK_GROOVE, "STYLE, SEQ: GROOVE");
    }
    {   /* the dials: the generator's parameters in the track's p[]; LOOP A..D only the loops there are */
        int16_t c0 = TBRK->p[P_E0];
        encs[panel.enc[EN_K1]] = 5; frame();
        check(TBRK->p[P_E0] > c0 && TBRK->p[P_E0] <= 100, "GROOVE: KNOB 1 CMPLX up");
        TBRK->p[P_E0] = c0;
        tap(B_EDIT);
        encs[panel.enc[EN_K4]] = 10; frames(2);
        check(TBRK->p[P_LFADE] == (int16_t)brkp_nloops() - 1 && brkp_bank_src(3), "LOOPS: KNOB 4 LOOP D, up to the last loop there is (built)");
        ui.force = 1; frame(); ppm("page-break-loops");
        encs[panel.enc[EN_PRESET]] = 1; frame();
        check(TBRK->p[P_LRATE] == 1 && bk_page == BK_LOOPS, "BREAK screen: PRESETS loop A");
        encs[panel.enc[EN_PRESET]] = -5; frame();
        check(TBRK->p[P_LRATE] == 0, "BREAK screen: PRESETS bounded (the first loop)");
        {   /* Q BAR, playing: KNOB 1 LOOP A waits for the bar, the dial shows it ("@AMEN B") */
            uint8_t pl = song.playing, qm = q_mode;
            song.playing = 1;
            q_mode = QM_BAR;
            encs[panel.enc[EN_K1]] = 1; frame();
            check(TBRK->p[P_LRATE] == 0 && q_val(TRK_BRK, P_LRATE) == 1, "LOOPS, Q BAR playing: KNOB 1 LOOP A waits for the bar");
            ui.force = 1; frame(); ppm("page-break-loops-q");
            q_clear();
            q_mode = qm;
            song.playing = pl;
        }
        {   /* LAYER: its bank's letter on the dial ("cHOTPNT"), the name cut to the dial */
            int16_t l0 = TBRK->p[P_GLMODE];
            tap(B_EDIT); tap(B_EDIT);
            encs[panel.enc[EN_K1]] = 2; frame();
            check(bk_page == BK_LAYER && TBRK->p[P_GLMODE] == 2, "EDIT, EDIT: LAYER; KNOB 1: bank C");
            ui.force = 1; frame(); ppm("page-break-layer");
            TBRK->p[P_GLMODE] = l0;
            tap(B_EDIT);
        }
        tap(B_SEQ);
        check(bk_page == BK_STYLE, "LOOPS, SEQ: STYLE");
    }
    {   /* white keys: the steps; a white key held + black key 1-8: the step's own slice; black keys: live */
        uint32_t s0;
        fm1_irq_off(); steps_clear(TBRK); fm1_irq_on(); ui.force = 1; frame();
        key(key_of_white(2));
        check(TBRK->bstep[2].on && !TBRK->bstep[3].on && !TBRK->bstep[2].slice, "BREAK screen: white key 3: step 3 plays (the generator's slice)");
        check(((keys_lit() >> key_of_white(2)) & 1u) && !((keys_lit() >> key_of_white(3)) & 1u), "BREAK screen: the steps that play lit");
        key(key_of_white(2));
        check(!TBRK->bstep[2].on, "BREAK screen: white key 3 again: off");
        fm1_in.notes = 1u << key_of_white(4); frame();
        fm1_in.notes |= 1u << BK_BLACK[2]; frame();
        fm1_in.notes = 1u << key_of_white(4); frame();
        fm1_in.notes = 0; frame();
        check(TBRK->bstep[4].on && TBRK->bstep[4].slice == 3u && !bp.keys, "BREAK screen: white key 5 held + black key 3: step 5 plays slice 3");
        fm1_in.notes = 1u << key_of_white(4); frame();
        fm1_in.notes |= 1u << BK_BLACK[2]; frame();
        fm1_in.notes = 0; frame();
        check(TBRK->bstep[4].on && !TBRK->bstep[4].slice, "... the same again: the generator's slice, the step kept");
        fm1_in.notes = 1u << key_of_white(4); frame();      /* a step held: KNOB 1..4 its slice, level, retrig, pitch */
        encs[panel.enc[EN_K1]] = 2; frame();
        encs[panel.enc[EN_K2]] = 1; frame();
        encs[panel.enc[EN_K3]] = 4; frame();
        encs[panel.enc[EN_K4]] = -5; frame();
        ui.force = 1; frame(); ppm("page-break-step");
        fm1_in.notes |= 1u << BK_BLACK[8]; frame();
        fm1_in.notes = 1u << key_of_white(4); frame();
        fm1_in.notes = 0; frame();
        check(TBRK->bstep[4].on && TBRK->bstep[4].slice == 2u && TBRK->bstep[4].lvl == LV_HARD && TBRK->bstep[4].rat == 4u &&
              TBRK->bstep[4].pitch == -5 && (TBRK->bstep[4].flags & BS_REV) && !bp.keys && !bp.reverse,
              "BREAK screen: step 5 held + KNOB 1..4, black key 9: slice 2, hard, x8, -5, backwards; still on");
        fm1_in.notes = 1u << key_of_white(4); frame();
        fm1_in.notes |= 1u << BK_BLACK[8]; frame();
        fm1_in.notes = 0; frame();
        check(TBRK->bstep[4].on && !(TBRK->bstep[4].flags & BS_REV), "... black key 9 again: forwards");
        s0 = bp.st_starts;
        fm1_in.notes = 1u << BK_BLACK[5]; frames(2);
        check(bp.keys == 1u << 5 && bp.st_starts > s0, "BREAK screen: black key 6 held: slice 6 live, heard at once");
        fm1_in.notes |= 1u << BK_BLACK[8]; frames(2);
        check(bp.keys == (1u << 5 | 1u << 8) && bp.reverse, "BREAK screen: + black key 9: reverse");
        fm1_in.notes = 0; frames(2);
        check(!bp.keys && !bp.count && !bp.reverse, "BREAK screen: let go: the generator's again");
        TBRK->p[P_SLEN] = 32;                            /* OCT+: the next 16 steps */
        tap(B_OCTUP); key(key_of_white(0));
        check(TBRK->bstep[16].on && !TBRK->bstep[0].on, "BREAK screen, 32 steps: OCT+ then white key 1: step 17");
        tap(B_OCTUP); key(key_of_white(0));
        check(TBRK->bstep[0].on, "OCT+ again: steps 1-16");
        TBRK->p[P_SLEN] = 16;
    }
    {   /* playing: a scene to look at, a slice held live wins over the generator, REC records it; OCT- mutes */
        static int32_t o[CTL * 2];
        uint32_t j, b, n;
        int32_t loud = 0, q = 0;
        for (j = 0; j < 16u; j++) {
            bstep_t *st = &TBRK->bstep[j];
            memset(st, 0, sizeof *st);
            st->on = j != 7u && j != 15u;
            st->lvl = (uint8_t)(j % 4u == 0u ? LV_HARD : j % 4u == 2u ? LV_NORM : j & 1u ? LV_GHOST : LV_SOFT);
            st->slice = (uint8_t)(j == 4u || j == 12u ? 3u : j == 10u ? 1u : 0u);
            st->rat = (uint8_t)(j == 14u ? 2u : 0u);
        }
        for (j = 0; j < 16u; j += 3u) { trk[0].step[j].n = 1; trk[0].step[j].note[0] = 36; trk[0].step[j].time = ST_NOTE; }
        for (j = 0; j < 16u; j += 4u) { trk[1].step[j].n = 3; trk[1].step[j].time = ST_NOTE; }
        transport_req = 1; frames(20);
        fm1_in.notes = 1u << BK_BLACK[1]; frames(40);
        check(bp.running && bp.st_slice == 1 && bp.st_trigs > 1u, "BREAK screen, playing, black key 2 held: the triggers play slice 2");
        ui.force = 1; frame(); ppm("page-break");
        fm1_in.notes = 0; frames(2);
        tap(B_REC);
        check(song.rec == 1u << TRK_BRK, "BREAK screen, playing: REC records the BREAK track");
        fm1_in.notes = 1u << BK_BLACK[6]; frame(); fm1_in.notes = 0; frame();
        for (n = 0, j = 0; j < 16u; j++) n += TBRK->bstep[j].slice == 7u && TBRK->bstep[j].on;
        check(n == 1u, "BREAK screen, REC: black key 7: slice 7 recorded into the step it falls on");
        tap(B_REC);
        for (j = 0; j < NPART; j++) trk[j].p[P_MUTE] = 1;   /* (the BREAK alone) */
        for (b = 0; b < FS / CTL; b++) { mix_block(o, CTL); for (j = 0; j < CTL * 2u; j++) if ((o[j] < 0 ? -o[j] : o[j]) > loud) loud = o[j] < 0 ? -o[j] : o[j]; }
        tap(B_OCTDN);
        check(TBRK->p[P_MUTE] == 1 && !song.octave, "BREAK screen: OCT- mutes it");
        for (b = 0; b < 4u * FS / CTL; b++) mix_block(o, CTL);   /* (the reverb's tail) */
        for (b = 0; b < FS / 2u / CTL; b++) { mix_block(o, CTL); for (j = 0; j < CTL * 2u; j++) if ((o[j] < 0 ? -o[j] : o[j]) > q) q = o[j] < 0 ? -o[j] : o[j]; }
        printf("ui: the BREAK alone, playing: peak %d; muted: %d\n", loud, q);
        check(loud > 2000 && q < 64, "BREAK screen: playing it is heard, muted it is silent");
        ui.force = 1; frame(); ppm("page-break-muted");
        tap(B_OCTDN);
        check(!TBRK->p[P_MUTE], "BREAK screen: OCT- again: on");
        for (j = 0; j < NPART; j++) trk[j].p[P_MUTE] = 0;
        transport_req = 2; frames(2);
    }
    {   /* out of it: ALGORITHM down, HOME; into it: ALGORITHM onto track 4 from a synth page */
        encs[panel.enc[EN_ALGO]] = 1; frame();
        check(song.sel == 0u && !on_break_page() && cur_page()->scope == SC_TRK, "BREAK screen, ALGORITHM right: the next lane, BASS, on TRACKS");
        open_family(FAM_ENV); frame();
        encs[panel.enc[EN_ALGO]] = -1; frame();
        check(song.sel == TRK_BRK && on_break_page(), "a synth page (ENV), ALGORITHM up onto the BREAK: the BREAK screen");
        tap(B_HOME); frame();
        check(!on_break_page() && cur_page()->scope == SC_TRK && song.sel == TRK_BRK, "BREAK screen: HOME goes back to TRACKS");
    }
    {   /* the BASS screen: track 1, SEL: a phrase on an empty track; its pages, the dials, NEW, ANSWER, BACK */
        uint16_t v0;
        uint32_t i, n = 0;
        static step_t was[BL_STEPS];
        bl_defaults();
        memset(trk[0].step, 0, sizeof trk[0].step);
        song.sel = 0; go_home(); ui.force = 1; frames(16);
        tap(B_SCL);
        for (i = 0; i < BL_STEPS; i++)
            n += step_on(&trk[0].step[i]);
        check(on_bass_page() && n >= 2u && trk_len(&trk[0]) == BL_STEPS && step_on(&trk[0].step[0]),
              "track 1 (BASS), SEL: the BASS screen; an empty track gets a phrase of two bars");
        ui.force = 1; frame(); ppm("page-bass");
        memcpy(was, trk[0].step, sizeof was);
        v0 = bl.var;
        encs[panel.enc[EN_K1]] = 1; frame();
        check(bl.var == v0 + 1u && memcmp(was, trk[0].step, sizeof was), "BASS: KNOB 1 VARIANT: another phrase");
        encs[panel.enc[EN_K4]] = -50; frame();
        check(bl.change == 0, "BASS: KNOB 4 CHANGE down to LOCK");
        memcpy(was, trk[0].step, sizeof was);
        fm1_in.notes = 1u << 1; frames(2); fm1_in.notes = 0; frames(2);   /* black key 1: NEW */
        check(bl.var == v0 + 2u && memcmp(was, trk[0].step, sizeof was), "BASS: black key 1: a new phrase");
        memcpy(was, trk[0].step, sizeof was);
        fm1_in.notes = 1u << 3; frames(2); fm1_in.notes = 0; frames(2);   /* black key 2: ANSWER */
        check(bl_aseed() == 1u && !memcmp(was, trk[0].step, sizeof was[0] * 16u), "BASS: black key 2: another answer, the same call");
        trk[0].step[5].time = ST_NOTE, trk[0].step[5].n = 1, trk[0].step[5].note[0] = 70;   /* (a hand edit) */
        fm1_in.notes = 1u << 5; frames(2); fm1_in.notes = 0; frames(2);   /* black key 3: BACK */
        bl_build(was);
        check(!memcmp(was, trk[0].step, sizeof was), "BASS: black key 3: the phrase back as written");
        tap(B_SCL);
        check(on_bass_page() && bs_page == BS_SHAPE, "BASS: SEL again: its SHAPE page");
        encs[panel.enc[EN_K1]] = 2; frame();
        encs[panel.enc[EN_K2]] = -1; frame();
        check(bl.answer == BA_OCT && bl.hold == BH_MID, "BASS: KNOB 1 ANSWER OCT, KNOB 2 HOLD MID");
        transport_req = 1; frames(30);
        ui.force = 1; frame(); ppm("page-bass-shape");
        transport_req = 2; frames(4);
        tap(B_HOME); frame();
        check(!on_bass_page() && cur_page()->scope == SC_TRK, "BASS: HOME goes back to TRACKS");
        bl_defaults();
        memset(trk[0].step, 0, sizeof trk[0].step);
    }
    {   /* the CHOP screen: track 3, SEL; three MODEs, the black keys, PRESETS by mode, SLICE answering the break, REC */
        uint32_t base, n, v, on;
        song.sel = 2; go_home(); ui.force = 1; frames(16);
        set_engine_of(&trk[2], ENGI_SAMPLE);
        for (v = 0; v < ENG_SAMPLE.npresets && !str_eq(ENG_SAMPLE.presets[v].name, "CHOP KIT"); v++)
            ;
        apply_preset_to(&trk[2], v);
        trk[2].engine = trk[2].eng_req;
        trk[2].p[P_X0] = CM_PADS;
        memset(trk[2].step, 0, sizeof trk[2].step);
        tap(B_SCL);
        check(on_chop_page() && cs_mode() == CM_PADS && cp_page == CPG_MAIN, "track 3 (CHOP), SEL: the CHOP screen, PADS");
        check(cp_kit(&base, &n) && n == 9u, "CHOP: CHOP KIT plays USR1, RAVE HITS: a kit of 9 hits");
        check(!cs_has_notes(), "CHOP: nothing written for the player (no generator in PADS)");
        ui.force = 1; frame(); ppm("page-chop");
        fm1_in.notes = 1u << key_of_white(2); frames(2);
        for (v = 0, on = 0; v < NVOICE; v++)
            on |= trk[2].v[v].active && trk[2].v[v].gate && trk[2].v[v].note == base + 2u;
        check(on, "CHOP PADS: white key 3 plays hit 3");
        fm1_in.notes = 0; frames(2);
        fm1_in.notes = 1u << 1; frames(2);
        check(csr.fx == CF_STUT, "CHOP: black key 1 held: STUTTER");
        fm1_in.notes = 1u << 3; frames(2);
        check(csr.fx == CF_GAP, "CHOP: black key 2 held: GAP");
        fm1_in.notes = 1u << 5; frames(2);
        check(csr.fx == CF_DOWN, "CHOP: black key 3 held: DOWN");
        fm1_in.notes = 0; frames(2);
        check(!csr.fx, "... let go");
        encs[panel.enc[EN_K1]] = 1; frame();
        check(cp_kit(&base, &n) && n == 13u && trk[2].p[P_E0] == (int16_t)(SMP_NSETS + 1u),
              "CHOP: KNOB 1 KIT: the next kit, ANIME KIT in USR2 (13 hits)");
        {   /* PRESETS on the screen: the kits only, round and back to where it was */
            int16_t e0 = trk[2].p[P_E0];
            uint32_t r, allkits = 1;
            for (r = 0; r < 12u; r++) {
                encs[panel.enc[EN_PRESET]] = 1; frame();
                allkits &= (uint32_t)cp_kit(&base, &n);
            }
            for (r = 0; r < 12u; r++) {
                encs[panel.enc[EN_PRESET]] = -1; frame();
                allkits &= (uint32_t)cp_kit(&base, &n);
            }
            check(allkits && trk[2].p[P_E0] == e0, "CHOP PADS: PRESETS goes round the kits only, and back to the same");
        }
        encs[panel.enc[EN_K2]] = -1; frame();
        encs[panel.enc[EN_K3]] = 1; frame();
        check(trk[2].p[P_REL] < 100 && trk[2].p[P_VOICE] == V_MONO, "CHOP PADS: KNOB 2 GATE, KNOB 3 CHOKE on");
        trk[2].p[P_VOICE] = V_POLY;
        encs[panel.enc[EN_SELECT]] = 1; frame();
        check(cp_page == CPG_STEPS, "CHOP: SELECT: STEPS");
        {
            uint32_t sdiv = (uint32_t)trk[2].p[P_SDIV], len = trk_len(&trk[2]);
            encs[panel.enc[EN_K2]] = -1; frame();
            encs[panel.enc[EN_K1]] = len < 64u ? 1 : -1; frame();
            check(trk[2].p[P_SDIV] == (int16_t)sdiv - 1 && cs_rate(CM_PADS) == CR_8 && trk_len(&trk[2]) != len,
                  "CHOP STEPS: KNOB 2 RATE 1/8 (PADS's), KNOB 1 LENGTH");
            trk[2].p[P_X7] = 0;
            trk[2].p[P_SDIV] = 2;
            trk[2].p[P_SLEN] = 16;
        }
        encs[panel.enc[EN_SELECT]] = -1; frame();
        {                                             /* OCT+: the SLICE tab seen, PADS still plays */
            int16_t e0 = trk[2].p[P_E0], rel = trk[2].p[P_REL], sty = (int16_t)cs_style();
            int oct = song.octave;
            tap(B_OCTUP); frame();
            check(on_chop_page() && cs_mode() == CM_PADS && cp_view == CM_SLICE && song.octave == oct,
                  "CHOP: OCT+: the SLICE tab seen, PADS plays on (the keys' octave untouched)");
            ui.force = 1; frame(); ppm("page-chop-seen");
            encs[panel.enc[EN_K1]] = 1; frame();
            check(cs_style() != (uint32_t)sty && trk[2].p[P_E0] == e0 && trk[2].p[P_REL] == rel && cs_mode() == CM_PADS,
                  "CHOP: the SLICE tab seen: KNOB 1 its STYLE, nothing of PADS's sound changes");
            CSV(1) = sty;
            tap(B_OCTDN); frame();
            check(cp_view == CM_PADS && cs_mode() == CM_PADS, "CHOP: OCT-: back to PADS");
            tap(B_SCL); frame();
            check(cs_mode() == CM_PADS, "CHOP: SEL on the tab playing: nothing changes");
            tap(B_OCTUP); frame();
        }
        tap(B_SCL);                                   /* SEL on the screen: SLICE */
        check(on_chop_page() && cs_slice() && trk[2].p[P_VOICE] == V_MONO && cp_kit(&base, &n),
              "CHOP: SEL: SLICE, the same kit, CHOKE on");
        encs[panel.enc[EN_K1]] = 1; frame();
        check(cs_style() == CY_HOLD + 0u || cs_style() == (uint32_t)clamp(CY_ENDS + 1, 0, CY_N - 1), "CHOP SLICE: KNOB 1 STYLE");
        trk[2].p[P_X0 + 1] = CY_CHOP;
        trk[2].p[P_X0 + 2] = 100;
        transport_req = 1;
        {
            uint32_t moved = 0, played = 0, f;
            for (f = 0; f < 120u; f++) {
                frames(1);
                played |= csr.sl != 0xFFu;
                moved |= csr.hist_n && (csr.hist[0] & 0x80u);
            }
            check(played, "SLICE: PLAY: the generator plays the loop's slices");
        }
        ui.force = 1; frame(); ppm("page-chop-slice");
        transport_req = 2;
        frames(2);
        {                                             /* OCT+: INST seen while SLICE plays; PRESETS: its sound to come */
            uint32_t tot, want;
            char nm[16];
            int16_t e0 = trk[2].p[P_E0];
            tap(B_OCTUP); frame();
            want = (chop_mem_pos(CM_INST, &tot, nm) + 1u) % (tot ? tot : 1u);
            encs[panel.enc[EN_PRESET]] = 1; frame();
            check(cs_slice() && trk[2].p[P_E0] == e0 && chop_mem_pos(CM_INST, &tot, nm) == want,
                  "CHOP: INST seen, PRESETS: its next sound chosen, SLICE plays on as it was");
            tap(B_SCL);                               /* INST */
            frames(16);                               /* (#39: the knobs quiet a while after SEL) */
            check(cs_mode() == CM_INST && preset_pos(&tot) == want, "CHOP: SEL: INST plays the sound chosen while seen");
        }                                   /* (#39: the knobs quiet a while after SEL) */
        {
            uint32_t r, kits = 0, total;
            check(on_chop_page() && cs_mode() == CM_INST && !chop_kits(), "CHOP: SEL: INST");
            for (r = 0; r < 20u; r++) {
                uint32_t k, e;
                encs[panel.enc[EN_PRESET]] = 1; frame();
                e = preset_at(preset_pos(&total), &k);
                kits += e == PRESET_KIT || cp_kit(&base, &n);
            }
            check(!kits && total > 5u, "CHOP INST: PRESETS the instruments (analog too), never a kit");
            for (r = 0; r < 40u && ENGINES[trk[2].eng_req % NENGINES] != &ENG_ANALOG; r++) {
                encs[panel.enc[EN_PRESET]] = -1; frame();   /* (an analog one: the packs' sounds may be away) */
            }
            frames(4);
            ui.force = 1; frame(); ppm("page-chop-inst");
            fm1_in.notes = 1u << key_of_white(0); frames(2);
            for (v = 0, on = 0; v < NVOICE; v++)
                on |= trk[2].v[v].active && trk[2].v[v].gate && trk[2].v[v].note == (uint32_t)cs_deg(0, 60);
            fm1_in.notes = 0; frames(2);
            check(on, "CHOP INST: white key 1 plays the session's root at C4");
            memset(trk[2].step, 0, sizeof trk[2].step);
            transport_req = 1; frames(4);
            tap(B_REC); frames(2);
            fm1_in.notes = 1u << key_of_white(2); frames(6);
            fm1_in.notes = 0; frames(2);
            tap(B_REC); frames(2);
            transport_req = 2; frames(2);
            check(cs_has_notes(), "CHOP INST: REC records the white keys into the steps");
            encs[panel.enc[EN_SELECT]] = 1; frame();
            ui.force = 1; frame(); ppm("page-chop-steps");
            encs[panel.enc[EN_SELECT]] = 1; frame();
            check(cp_page == CPG_TIME, "CHOP: SELECT again: TIME");
            encs[panel.enc[EN_K3]] = -1; frame();
            encs[panel.enc[EN_K4]] = 1; frame();
            check(cs_rate(CM_INST) == CR_8 && cs_rate(CM_PADS) == CR_16 && cs_rate(CM_SLICE) == CR_16 && cs_oct() == 1 &&
                  trk[2].p[P_SDIV] == 1, "CHOP TIME: KNOB 3 INST's RATE 1/8 (the others kept), KNOB 4 OCT +1");
            ui.force = 1; frame(); ppm("page-chop-time");
            trk[2].p[P_X7] = 0;
            encs[panel.enc[EN_SELECT]] = 1; frame();
            check(cp_page == CPG_TIME, "CHOP: SELECT past TIME: stays");
            encs[panel.enc[EN_SELECT]] = -1; frame();
            encs[panel.enc[EN_SELECT]] = -1; frame();
        }
        tap(B_OCTDN); frame();
        tap(B_OCTDN); frame();
        check(cs_mode() == CM_INST && cp_view == CM_PADS, "CHOP: OCT- twice: PADS seen, INST plays on");
        tap(B_SCL);                                   /* PADS again: the kit it had */
        check(on_chop_page() && cs_mode() == CM_PADS && cp_kit(&base, &n) && n == 13u, "CHOP: SEL: PADS again, its kit back");
        tap(B_HOME); frame();
        check(!on_chop_page() && cur_page()->scope == SC_TRK, "CHOP: HOME goes back to TRACKS");
    }
    {   /* the CHORDS screen: track 2, SEL; its pages, the dials, a node held (plays, pins), the steps */
        uint16_t v0;
        uint8_t prog0[CG_MAX];
        cg_defaults();
        cg_poll();
        song.sel = 1; go_home(); ui.force = 1; frames(16);
        tap(B_SCL);
        check(on_chord_page() && ch_page == CH_GEN, "track 2 (CHORDS), SEL: the CHORDS screen, GEN");
        ui.force = 1; frame(); ppm("page-chords");
        v0 = cg.var;
        encs[panel.enc[EN_K1]] = 1; frame(); cg_poll();
        check(cg.var == (uint16_t)(v0 + 1u), "CHORDS: KNOB 1 VARIANT");
        encs[panel.enc[EN_K2]] = 1; frame();
        encs[panel.enc[EN_K3]] = 1; frame();
        check(cg.num == 5u && cg.color == CC_ADD9, "CHORDS: KNOB 2 CHORDS 5, KNOB 3 COLOR ADD9");
        cg_poll();
        memcpy(prog0, cgr.prog, sizeof prog0);
        fm1_in.notes = 1u << 3; frames(2);           /* the black key of node 2 (G#3) held */
        check(cgr.live == 2u && cgr.now == 1, "CHORDS: node 2's black key held: it plays");
        ui.force = 1; frame(); ppm("page-chords-node");
        encs[panel.enc[EN_K1]] = 3; frame(); cg_poll();
        check((cg.pins & 2u) && cgr.prog[1] == prog0[1], "CHORDS: node 2 held + VARIANT: node 2 pinned, kept");
        tap(B_OCTDN);
        check(!(cg.pins & 2u), "CHORDS: node 2 held + OCT-: unpinned");
        fm1_in.notes = 0; frames(2);
        check(!cgr.live && cgr.now < 0, "CHORDS: let go: silent (stopped)");
        tap(B_SCL);
        check(ch_page == CH_PLAY, "CHORDS: SEL again: PLAY");
        encs[panel.enc[EN_K1]] = 1; frame();
        encs[panel.enc[EN_K2]] = 2; frame();
        encs[panel.enc[EN_K3]] = 1; frame();
        check(cg.play == CP_STEPS && cg_rate() == 3u && cg_feel() == CF_ACCEL, "PLAY: KNOB 1 STEPS, KNOB 2 4BAR, KNOB 3 FEEL ACCEL");
        ui.force = 1; frame(); ppm("page-chords-play");
        trk[1].p[P_SLEN] = 16;
        fm1_irq_off(); steps_clear(&trk[1]); fm1_irq_on();
        key(key_of_white(4));
        check(step_on(&trk[1].step[4]), "CHORDS: white key 5: step 5 strikes the chord");
        key(key_of_white(4));
        check(!step_on(&trk[1].step[4]), "... again: off");
        tap(B_HOME); frame();
        check(!on_chord_page() && cur_page()->scope == SC_TRK, "CHORDS: HOME goes back to TRACKS");
        cg_defaults();
        cg.play = CP_OFF;
        cg_poll();
    }
    {   /* the TRACKS screen live (the BREAK playing as track 4) */
        song.sel = 1; go_home(); ui.force = 1;
        transport_req = 1; frames(30); song.rec = 2u; frame(); ui.force = 1; frame(); ppm("live-tracks");
        song.rec = 0; transport_req = 2; frames(2);
        song.sel = TRK_BRK;
    }
    {   /* the free take screens */
        static track_t keep[NTRK];
        memcpy(keep, trk, sizeof keep);
        for (i = 0; i < NTRK; i++) steps_clear(&trk[i]);
        rec_tempo = 0; rec_count = 0;
        rec_wait = 1; ui.force = 1; frame(); ppm("live-rec-free");
        /* the REC screen's dials (2.3): KNOB 1 MODE, KNOB 2 LENGTH, KNOB 3 START */
        encs[panel.enc[EN_K1]] = 1; frame();
        check(rec_tempo == 1u && rec_wait, "REC screen, empty project: KNOB 1 -> MODE tempo");
        ui.force = 1; frame(); ppm("live-rec-tempo");
        trk[song.sel].p[P_SLEN] = 16;
        encs[panel.enc[EN_K2]] = 1; frame();
        encs[panel.enc[EN_K2]] = 1; frame();
        check(trk[song.sel].p[P_SLEN] == 64, "REC screen: KNOB 2 -> LENGTH 1, 2, 4 bars");
        encs[panel.enc[EN_K2]] = -1; frame();
        check(trk[song.sel].p[P_SLEN] == 32, "REC screen: KNOB 2 back -> 2 bars");
        encs[panel.enc[EN_K3]] = 1; frame();
        check(rec_count == 1u, "REC screen: KNOB 3 -> START count");
        ui.force = 1; frame(); ppm("live-rec-count");
        check((lights_word() >> 9 & 3u) == 3u, "REC screen: MODE and START saved with the settings");
        ci_on = 1; ci_beat = 1; ui.force = 1; frame(); ppm("live-rec-countin");
        encs[panel.enc[EN_K1]] = -1; frame();
        check(rec_tempo == 1u, "REC screen: the count-in running, the dials wait");
        ci_on = 0; ci_beat = 0;
        trk[1].step[0].time = ST_NOTE; trk[1].step[0].n = 1; trk[1].step[0].note[0] = 60;
        encs[panel.enc[EN_K1]] = -1; frame();
        check(rec_tempo == 1u, "REC screen, a project with notes: no MODE (always the tempo set)");
        ui.force = 1; frame(); ppm("live-rec-notes");
        steps_clear(&trk[1]);
        encs[panel.enc[EN_K3]] = -1; frame();
        encs[panel.enc[EN_K1]] = -1; frame();
        check(rec_tempo == 0u && rec_count == 0u, "REC screen: KNOB 3 / 1 back -> note, free");
        rec_count = 1; encs[panel.enc[EN_K3]] = -1; frame();
        check(rec_count == 1u, "REC screen, MODE free: no START (the take starts on the first note)");
        rec_count = 0;
        trk[song.sel].p[P_SLEN] = 16;
        rec_wait = 0; ft_on = 1; ft_t = (uint32_t)(5.4 * FS / CTL); ui.force = 1; ui_draw(); ppm("live-free-take");
        ft_on = 0; ft_t = 0; memcpy(trk, keep, sizeof keep);
        rec_wait = 0; frame();
    }
    for (i = 0; i < 2u * BP_NBANK; i++) {             /* every loop A..D there is, and none: drawn */
        TBRK->p[P_LRATE] = (int16_t)i; studio_open(SC_BREAK); ui.force = 1; frames(2);
    }
    TBRK->p[P_LRATE] = 0; go_home(); frame();

    {   /* menu NOTES (PR #11 by @renebohne): sounding synth voices light their keys */
        song.sel = 0; go_home(); ui.force = 1; frame();
        trk[0].p[P_CHORD] = 0; trk[0].p[P_QUANT] = 0; trk[0].p[P_ROOT] = 0; trk[0].p[P_TRANS] = 0;
        song.octave = 0;
        lights_notes = 0;
        trk[0].v[0].active = 1; trk[0].v[0].gate = 1; trk[0].v[0].stage = 1; trk[0].v[0].note = 60;
        frame();
        check((keys_lit() & (1u << 7)) == 0, "NOTES off: a sounding synth voice lights no key");
        ui.menu = 1; ui.menu_sel = MI_NOTES; ui.force = 1; frame();
        encs[panel.enc[EN_K1 + mi_row(ui.menu_sel)]] = 1; frame();
        check(lights_notes == 1u, "menu NOTES: its knob (KNOB 3) right: ON");
        ui.force = 1; frame(); ppm("page-menu-notes");
        ui.menu = 0; ui.force = 1; go_home(); frame();
        check((keys_lit() & (1u << 7)) != 0, "NOTES on: a sounding synth voice lights key 7 (C4)");
        {   /* a short note (a sequencer 1/16) that starts and ends between two frames still lights its key */
            int32_t o[CTL * 2];
            uint32_t n = 0, f;
            trk[0].v[0].active = 0; trk[0].v[0].gate = 0;
            frame(); frame();
            check((keys_lit() >> 9 & 1u) == 0, "NOTES on: D4 not lit before it plays");
            trk_note_on(&trk[0], 62, 100);
            mix_block(o, CTL);
            trk_note_off(&trk[0], 62);
            for (f = 0; f < 20; f++) mix_block(o, CTL);
            frame();
            check((keys_lit() >> 9 & 1u) != 0, "NOTES on: a note over before the frame still lights its key (D4)");
            for (f = 0; f < 10; f++) { frame(); n += keys_lit() >> 9 & 1u; }
            check(n >= 4u && n <= 7u && (keys_lit() >> 9 & 1u) == 0, "NOTES on: ... for about 6 frames, then off");
            song.sel = 1; frame(); song.sel = 0; frame();
            trk[0].v[0].active = 1; trk[0].v[0].gate = 1; trk[0].v[0].stage = 1; trk[0].v[0].note = 60;
        }
        {   /* every screen and layer: lit where the keys are notes, a glow under the tiles */
            static const struct { uint32_t ly; int lit; const char *name; } L[] = {
                {LY_ERASE, 1, "EDIT erase"}, {LY_ROLL, 1, "ARP roll"}, {LY_SCALE, 1, "SEL key"},
                {LY_FX, 0, "FX panel"}, {LY_STEP, 0, "SEQ steps"}, {LY_MIX, 0, "GLO mix"}};
            uint32_t k, ok = 1, ly0 = ui.layer;
            char what[96];
            for (k = 0; k < sizeof L / sizeof L[0]; k++) {
                ui.layer = (uint8_t)L[k].ly;
                if (L[k].lit)
                    ok = (keys_lit() >> 7 & 1u) != 0;
                else
                    ok = (keys_notes_dim() >> 7 & 1u) != 0 && (keys_lit() >> 7 & 1u) == 0;
                snprintf(what, sizeof what, "NOTES on, %s layer: the sounding C4 %s", L[k].name, L[k].lit ? "lit" : "glows under the tiles");
                check(ok, what);
            }
            ui.layer = LY_SCALE;
            check((keys_notes_dim() & scale_keys(0)) == scale_keys(0), "NOTES on, SEL: the scale glows");
            lights_notes = 0;
            ui.layer = LY_ERASE;
            check((keys_lit() >> 7 & 1u) == 0 && keys_notes_dim() == 0u, "NOTES off, EDIT erase: as before (no note lights)");
            ui.layer = LY_SCALE;
            check(keys_notes_dim() == 0u, "NOTES off, SEL: as before");
            lights_notes = 1;
            ui.layer = (uint8_t)ly0;
        }
        song.octave = -1;
        frame();
        check((keys_lit() & (1u << 19)) != 0 && (keys_lit() & (1u << 7)) == 0, "NOTES on: the octave moves the lit key");
        song.octave = 0;
        trk[0].v[0].gate = 0; trk[0].v[0].stage = 3;
        frame();
        check((keys_lit() & (1u << 7)) == 0, "NOTES on: the release turns the key off");
        trk[0].v[0].active = 0;
        check((lights_word() >> 8 & 1u) == 1u, "NOTES: saved with the light settings");
        ui.menu = 1; ui.menu_sel = MI_USB; ui.force = 1; frame(); ppm("menu-usb");
        usb_full = 0;
        encs[panel.enc[EN_K1 + mi_row(ui.menu_sel)]] = 1; frame();
        check(usb_full == 1u && (lights_word() >> 11 & 1u) == 1u, "menu USB AUDIO: its knob (KNOB 2) -> FULL, saved with the settings");
        tap(B_OCTUP);
        check(usb_full == 0u && ui.menu == 1, "menu USB AUDIO: OCT+ toggles back to MASTER");
        ui.menu_sel = MI_SERIAL; ui.force = 1; frame();
        check(usb_serial == 0u && !(lights_word() >> 16 & 1u), "menu USB SERIAL: OFF by default (2.3 settings read so)");
        encs[panel.enc[EN_K1 + mi_row(ui.menu_sel)]] = 1; frame();
        check(usb_serial == 1u && (lights_word() >> 16 & 1u), "menu USB SERIAL: its knob (KNOB 3) -> ON, saved with the settings");
        ui.force = 1; frame(); ppm("menu-serial");
        lights_from_word(lights_word());
        check(usb_serial == 1u, "menu USB SERIAL: read back");
        tap(B_OCTUP);
        check(usb_serial == 0u && ui.menu == 1, "menu USB SERIAL: OCT+ toggles back to OFF");
        ui.menu = 0; ui.force = 1; go_home(); frame();
        ui.menu = 1; ui.menu_sel = MI_NOTES; ui.force = 1; frame();
        tap(B_OCTUP);
        check(lights_notes == 0u && ui.menu == 1, "menu NOTES: OCT+ toggles it off");
        ui.menu = 0; ui.force = 1; go_home(); frame();
    }

    {   /* the menu closed: back on the screen it was opened from, not the DECK (N2) */
        uint8_t pg;
        go_home(); frame();
        tap(B_ENV); frames(2);
        pg = ui.page;
        press(B_HOME); frames(50); release(B_HOME); frames(2);
        check(ui.menu, "ENV page, HOME held: the menu");
        press(B_HOME); frames(50); release(B_HOME); frames(2);
        check(!ui.menu && ui.page == pg && cur_page()->fam == FAM_ENV, "... HOME held again: back on the ENV page");
        proj_touched = 0;
        encs[panel.enc[EN_K1]] = 1; frames(2);
        check(proj_touched, "STUDIO: a knob turned on a page marks the song edited (LIVE's OCT will ask)");
        proj_touched = 0;
        go_home(); frame();
    }

    {   /* menu LIGHTS / KEYS: the backlight for playing in the dark */
        uint32_t m, nbits;
        go_home(); ui.force = 1; frame();
        check(lights_lvl == LIGHTS_OFF && fm1_led_bg_ns == 0u && lights_keys_mask() == 0u, "LIGHTS: off by default");
        ui.menu = 1; ui.menu_sel = MI_LIGHTS; ui.force = 1; frame();
        encs[panel.enc[EN_K1 + mi_row(ui.menu_sel)]] = 1; frame();
        check(lights_lvl == LIGHTS_LOW && fm1_led_bg_ns == 500u, "LIGHTS: KNOB 1 right: LOW (a 0.5 us pulse a frame)");
        encs[panel.enc[EN_K1 + mi_row(ui.menu_sel)]] = 1; frame();
        check(lights_lvl == LIGHTS_MID && fm1_led_bg_ns == 1000u, "LIGHTS: MID (1 us)");
        encs[panel.enc[EN_K1 + mi_row(ui.menu_sel)]] = 1; frame(); encs[panel.enc[EN_K1 + mi_row(ui.menu_sel)]] = 1; frame();
        check(lights_lvl == LIGHTS_HIGH && fm1_led_bg_ns == 2000u, "LIGHTS: HIGH (2 us, under the 4 us glow), and it stops there");
        ui.force = 1; frame(); ppm("page-menu-lights");
        encs[panel.enc[EN_K1 + mi_row(ui.menu_sel)]] = -1; frame();
        check(lights_lvl == LIGHTS_MID, "LIGHTS: KNOB 1 left: dimmer");
        tap(B_OCTUP);
        check(lights_lvl == LIGHTS_HIGH && ui.menu == 1, "LIGHTS: OCT+ steps round, the menu stays");
        tap(B_OCTUP);
        check(lights_lvl == LIGHTS_OFF && fm1_led_bg_ns == 0u, "LIGHTS: ... back to OFF");
        ui.menu_sel = MI_KEYS; ui.force = 1; frame();
        encs[panel.enc[EN_K1 + mi_row(ui.menu_sel)]] = 1; frame();
        m = lights_keys_mask();
        check(lights_keys == KEYS_C && lights_lvl == LIGHTS_LOW && m == (1u << 7 | 1u << 19),
              "KEYS (KNOB 2): C KEYS (C4, C5), LIGHTS raised to LOW");
        encs[panel.enc[EN_K1 + mi_row(ui.menu_sel)]] = 1; frame();
        for (m = lights_keys_mask(), nbits = 0; m; m &= m - 1u)
            nbits++;
        check(lights_keys == KEYS_WHITE && nbits == 16u && !(lights_keys_mask() & (1u << 1)),
              "KEYS: WHITE KEYS (16 of 27, F#3 dark)");
        encs[panel.enc[EN_K1 + mi_row(ui.menu_sel)]] = 1; frame(); encs[panel.enc[EN_K1 + mi_row(ui.menu_sel)]] = 1; frame();
        check(lights_keys == KEYS_ALL && lights_keys_mask() == 0x7FFFFFFu, "KEYS: ALL KEYS (every one of the 27 keys), and it stops there");
        ui.force = 1; frame(); ppm("page-menu-keys-all");
        tap(B_OCTUP);
        check(lights_keys == KEYS_OFF, "KEYS: OCT+ from ALL KEYS steps round to OFF");
        lights_keys = KEYS_ALL;
        check(lights_word() >> 4 & 15u, "KEYS: ALL KEYS kept in the settings word");
        lights_from_word(lights_word());
        check(lights_keys == KEYS_ALL, "KEYS: ALL KEYS read back");
        lights_lvl = LIGHTS_OFF; frame();
        check(lights_keys_mask() == 0u && fm1_led_bg_ns == 0u, "KEYS: nothing lit while LIGHTS is OFF");
        lights_keys = KEYS_OFF;
        ui.menu = 0; ui.force = 1; go_home(); frame();
    }

    {   /* the menu in sections (2.4): SELECT the section, KNOB 1..3 its rows, PRESETS the cursor, OCT+ opens */
        uint32_t sec;
        ui.menu = 1; ui.menu_sel = 0; ui.force = 1; frame(); ppm("menu-screen");
        check(mi_sec(ui.menu_sel) == MS_SCREEN, "menu: opens on SCREEN (COLOR, ZOOM)");
        encs[panel.enc[EN_SELECT]] = 1; frame();
        check(mi_sec(ui.menu_sel) == MS_LIGHTS && ui.menu_sel == MI_LIGHTS, "menu: SELECT right: LIGHTS, the cursor on its first row");
        encs[panel.enc[EN_K2]] = 1; frame(); encs[panel.enc[EN_K2]] = 1; frame();
        check(lights_keys == KEYS_WHITE && ui.menu_sel == MI_KEYS, "menu LIGHTS: KNOB 2 sets KEYS (row 2), the cursor follows");
        encs[panel.enc[EN_K3]] = 1; frame();
        check(lights_notes == 1u && ui.menu_sel == MI_NOTES, "menu LIGHTS: KNOB 3 sets NOTES (row 3)");
        encs[panel.enc[EN_K4]] = 1; frame();
        check(ui.menu_sel == MI_NOTES, "menu: KNOB 4 on a section of three rows does nothing");
        ui.force = 1; frame(); ppm("menu-lights");
        encs[panel.enc[EN_PRESET]] = 1; frame();
        check(ui.menu_sel == MI_LIGHTS, "menu: PRESETS moves the cursor round the section's rows");
        encs[panel.enc[EN_SELECT]] = 1; frame(); ui.force = 1; frame(); ppm("menu-audio");
        check(mi_sec(ui.menu_sel) == MS_AUDIO, "menu: SELECT right: AUDIO (LOWCUT, USB AUDIO, USB SERIAL)");
        encs[panel.enc[EN_SELECT]] = 5; frame();
        sec = mi_sec(ui.menu_sel);
        check(sec == MS_SYSTEM, "menu: SELECT stops at the last section, SYSTEM");
        encs[panel.enc[EN_K2]] = -1; frame();
        check(ui.menu_sel == MI_CPU1 && cpu1_off == 1u && cpu1_units == 0, "menu SYSTEM: KNOB 2 left: 2ND CORE OFF (no unit for core 1)");
        encs[panel.enc[EN_K2]] = 1; frame();
        check(cpu1_off == 0u && cpu1_units == -1 && (lights_word() >> 26 & 1u) == 0u, "menu SYSTEM: KNOB 2 right: 2ND CORE ON, kept in the settings word");
        encs[panel.enc[EN_K3]] = 1; frame();
        check(ui.menu == 1 && ui.menu_sel == MI_ABOUT, "menu SYSTEM: a knob does not open ABOUT, it moves the cursor");
        tap(B_OCTUP);
        check(ui.menu == 2, "menu SYSTEM: OCT+ on ABOUT opens it");
        tap(B_OCTUP);
        check(ui.menu == 2, "menu: OCT+ on ABOUT does nothing more");
        tap(B_OCTDN);
        check(ui.menu == 1 && ui.menu_sel == MI_ABOUT, "menu: OCT- from ABOUT: back to SYSTEM");
        encs[panel.enc[EN_K4]] = 1; frame();
        ui.force = 1; frame(); ppm("page-menu-system");
        check(ui.menu == 1 && ui.menu_sel == MI_CRASH, "menu SYSTEM: KNOB 4: the cursor on LAST CRASH (four rows)");
        fm1_crash.magic = 0;
        tap(B_OCTUP);
        check(ui.menu == 3, "menu SYSTEM: OCT+ on LAST CRASH opens it");
        ui.force = 1; frame(); ppm("page-menu-no-crash");
        tap(B_OCTDN);
        check(ui.menu == 1 && ui.menu_sel == MI_CRASH, "... OCT-: back to SYSTEM");
        fm1_crash.magic = FM1_CRASH_MAGIC;                 /* a crash of a past run (the record is kept over the reboot) */
        fm1_crash.count = 2, fm1_crash.vec = 1, fm1_crash.pc = 0x01C00BAEu, fm1_crash.rets = 0x0200B550u, fm1_crash.emu = 0;
        fm1_crash.rete = 0x02009D40u, fm1_crash.retx = 0;
        fm1_crash.dbg = 0x10u, fm1_crash.sp = 0x01C7F000u, fm1_crash.psr = 0x80000u, fm1_crash.uptime_ms = 83500u;
        fm1_crash.etm[0] = 0x01C00BACu, fm1_crash.etm[1] = 0x01C0A114u, fm1_crash.etm[2] = 0x01C0A100u, fm1_crash.etm[3] = 0x01C0A0F0u;
        tap(B_OCTUP);
        ui.force = 1; frame(); ppm("page-menu-crash");
        check(ui.menu == 3, "... a record: its page (VEC, PC, RETS, DBG, SP, PSR, EMU, the last branches, the build)");
        tap(B_OCTDN);
        check(ui.menu == 1 && ui.menu_sel == MI_CRASH, "... OCT-: back to SYSTEM");
        fm1_crash.magic = 0;
        encs[panel.enc[EN_K3]] = 1; frame();
        check(ui.menu_sel == MI_ABOUT, "... KNOB 3: the cursor on ABOUT again");
        tap(B_OCTDN);
        check(ui.menu == 0, "menu: OCT- closes");
        lights_keys = KEYS_OFF; lights_notes = 0; lights_lvl = LIGHTS_OFF;
        go_home(); ui.force = 1; frame();
    }

    {   /* SELECT (2.4): the pages of the family shown; the tempo on HOME (TRACKS), in a layer, on REC */
        int16_t bpm;
        #define PT() (PAGES[ui.page].title)
        #define SEL(n) do { encs[panel.enc[EN_SELECT]] = (n); frame(); } while (0)
        song.playing = 0; song.sel = 0; go_home(); ui.force = 1; frame();
        bpm = song.g[G_BPM];
        SEL(1);
        check(song.g[G_BPM] > bpm, "SELECT on HOME (TRACKS): the tempo");
        song.g[G_BPM] = bpm;
        tap(B_LFO);
        check(!strcmp(PT(), "LFO"), "SELECT: LFO opens its first page");
        SEL(1);
        check(!strcmp(PT(), "LFO DEST") && song.g[G_BPM] == bpm, "SELECT on LFO: next page LFO DEST, the tempo untouched");
        SEL(1);
        check(!strcmp(PT(), "LFO DEST") && song.g[G_BPM] == bpm, "SELECT: stops at the last page of the family");
        SEL(-1);
        check(!strcmp(PT(), "LFO"), "SELECT left: the page before");
        tap(B_FX); SEL(1);
        check(!strcmp(PT(), "FILTER"), "SELECT on FX: next page, the track FILTER");
        TSEL->p[P_TFLT] = -32; ui.force = 1; frames(2); ppm("page-filter");
        check(!strcmp(ui.big_l[0], "FILT") && !strcmp(ui.big_v[0], "LP50") && !ui.big_l[1][0],
              "FILTER page: its one value drawn large (LP50 %)");
        {   /* the big value in the graph strip: white pixels in the middle of the screen */
            uint32_t x, y, lit = 0;
            for (y = Y_GRAPH + 30; y < Y_GRAPH + 90; y++)
                for (x = 40; x < 200; x++)
                    lit += screen[y * 240u + x] != 0;
            check(lit > 300u, "FILTER page: the graph strip shows it");
        }
        TSEL->p[P_TFLT] = 0;
        SEL(9);
        check(!strcmp(PT(), "REV/CHO"), "SELECT on FX: on to the last page, REV/CHO");
        tap(B_EDIT); SEL(-9);
        check(!strcmp(PT(), "EDIT 1"), "SELECT on EDIT: back to the first page");
        SEL(9);
        check(!strcmp(PT(), "TOOLS"), "SELECT on EDIT: up to TOOLS (the sound's pages: EDIT 1, 2, VOICE, VOICE 2, PRESETS, USER, TOOLS)");
        tap(B_SEQ); SEL(9);
        check(!strcmp(PT(), "PATTERN"), "SELECT on SEQ: STEP <-> PATTERN, not into the SONG screen");
        tap(B_ENV);
        check(!strcmp(PT(), "ENV"), "the button still opens its family");
        tap(B_ENV);
        check(!strcmp(PT(), "ENV DEST"), "... and a second tap still steps the page");
        press(B_GLO); frames(3);
        SEL(1);
        check(!strcmp(PT(), "ENV DEST") && song.g[G_BPM] > bpm, "SELECT with a layer held (GLO): the tempo");
        release(B_GLO); frames(2);
        song.g[G_BPM] = bpm;
        go_home(); frame(); SEL(-1);
        check(song.g[G_BPM] < bpm, "back on HOME: SELECT is the tempo again");
        song.g[G_BPM] = bpm;
        #undef SEL
        #undef PT
    }

    {   /* Felucca #39 / #102 (2.4): a knob turned as a layer lets go, or while PLAY is pressed in a layer, never
         * edits the page under the layer */
        int16_t r0, w0;
        song.playing = 0; song.sel = 0; go_home(); ui.force = 1; frames(3);
        tap(B_LFO); frames(2);
        r0 = trk[0].p[P_LRATE]; w0 = trk[0].p[P_LWAVE];
        press(B_FX); frames(10);
        encs[panel.enc[EN_K2]] = 3; frame();
        encs[panel.enc[EN_K1]] = 4;                         /* turned in the frame FX comes up */
        fm1_in.buttons &= ~BT(B_FX); frame();
        check(trk[0].p[P_LRATE] == r0, "#39: KNOB 1 turned as FX lets go: the LFO page under it is untouched");
        frames(5); encs[panel.enc[EN_K1]] = 2; frame();
        check(trk[0].p[P_LRATE] == r0, "#39: ... and for a moment after (the knob still turning)");
        frames(20); encs[panel.enc[EN_K1]] = 2; frame();
        check(trk[0].p[P_LRATE] > r0, "#39: then KNOB 1 is the page's again");
        trk[0].p[P_LRATE] = r0;
        press(B_FX); frames(10);
        edges_btn |= BT(B_PLAY); fm1_in.buttons |= BT(B_PLAY);
        encs[panel.enc[EN_K2]] = 2; frame();
        fm1_in.buttons &= ~BT(B_PLAY);
        check(trk[0].p[P_LWAVE] == w0 && lfx.time != 6u && transport_req, "#102: PLAY pressed in the FX layer plays, KNOB 2 is TIME only");
        lfx.time = 6;
        transport_req = 0; song.g[G_DUST] = 0;
        release(B_FX); frames(20);
        song.g[G_BPM] = 120;
    }

    {   /* SEQ layer, the BREAK track: KNOB 1 picks the slice and plays it (its key lit); KNOB 2 DIV (2.4) */
        uint32_t i;
        song.playing = 0; song.sel = TRK_BRK; go_home(); frame();
        fm1_irq_off(); steps_clear(TBRK); fm1_irq_on();
        memset(pad_lit, 0, sizeof pad_lit);
        pen_lane = 0;
        press(B_SEQ); frames(3);
        encs[panel.enc[EN_K1]] = 2; frames(2);
        check(pen_lane == 2 && pad_lit[2], "SEQ layer, the BREAK track: KNOB 1 picks the slice and plays it");
        release(B_SEQ); frames(2);
        song.sel = 1; go_home(); frame();
        trk[1].p[P_SDIV] = 5;
        press(B_SEQ); frames(3);
        for (i = 0; i < 5u; i++) { encs[panel.enc[EN_K2]] = 1; frame(); }
        check(trk[1].p[P_SDIV] == (int16_t)(NDIV_STEP - 1u), "SEQ layer: KNOB 2 DIV goes on to 1/2, 1BAR, 2BAR");
        release(B_SEQ); frames(2);
        trk[1].p[P_SDIV] = 2;
        song.sel = 0; go_home(); frame();
    }

    {   /* (2.4) STEP, then ALGO to the BREAK track: its screen (STEP is not a page for it: its steps are no notes) */
        uint32_t j, b0;
        song.sel = 0; go_home(); frames(2);
        for (j = 0; j < NSTEP; j++) { TBRK->bstep[j].on = 1; TBRK->bstep[j].slice = (uint8_t)(1u + j % 8u); TBRK->bstep[j].rat = 3; }
        for (j = 0; j < NPAGES; j++) if (!strcmp(PAGES[j].title, "STEP")) break;
        open_family(FAM_SEQ); ui.page = (uint8_t)j; ui.fam_last[FAM_SEQ] = (uint8_t)j; page_entered(); ui.force = 1; frames(2);
        b0 = ui.page;
        check(!page_for_brk(&PAGES[b0]), "STEP is not a page for the BREAK track");
        for (j = 0; j < 4u; j++) { encs[panel.enc[EN_ALGO]] = -1; frames(2); }
        check(song.sel == TRK_BRK && on_break_page(), "STEP page, ALGORITHM up to the BREAK: the BREAK screen");
        fm1_irq_off(); steps_clear(TBRK); fm1_irq_on();
        song.sel = 0; go_home(); frames(4);
    }
    {   /* (2.4.1) the STEP page in chord mode: a key writes the whole chord it sounds (it wrote only the root, as
         * 2.3 did); with a CHORD+ modifier held, the changed chord; a single note without chord mode */
        uint32_t j, k5 = key_of_white(4), kmin = 1u;            /* key 5 = C4 (the I chord); F#3 (key index 1): minor */
        track_t *t = &trk[1];
        song.sel = 1; go_home(); frames(2);
        steps_clear(t); t->p[P_SLEN] = 16; t->p[P_VOICE] = V_POLY;
        t->p[P_ROOT] = 0; t->p[P_SCALE] = 1; t->p[P_QUANT] = 0; t->p[P_CHORD] = 1;   /* C major, TRIAD */
        for (j = 0; j < NPAGES; j++) if (!strcmp(PAGES[j].title, "STEP")) break;
        open_family(FAM_SEQ); ui.page = (uint8_t)j; ui.fam_last[FAM_SEQ] = (uint8_t)j; page_entered(); ui.force = 1; frames(2);
        cursor_set(0); frames(1);
        key(k5); frames(2);
        check(t->step[0].n == 3u && t->step[0].note[0] % 12u == 0u && t->step[0].note[1] % 12u == 4u && t->step[0].note[2] % 12u == 7u,
              "STEP page, chord mode TRIAD: C4 writes the chord C E G (not only C)");
        fm1_in.notes = 1u << kmin; frames(2);
        key(k5); frames(2);
        fm1_in.notes = 0; frames(2);
        check(t->step[1].n == 3u && t->step[1].note[1] % 12u == 3u, "STEP page, CHORD+: F# held, C4 writes C minor (C Eb G)");
        t->p[P_CHORD] = 0;
        key(k5); frames(2);
        check(t->step[2].n == 1u && t->step[2].note[0] % 12u == 0u, "STEP page, no chord mode: one note");
        steps_clear(t); song.sel = 0; go_home(); frames(4);
    }
    {   /* the visualiser (2.4): open (vis_open: FMX's HOME on HOME is LIVE), SELECT its 12 styles, HOME / a page closes it; a layer
         * shows its screen over it; the style is kept with the settings */
        uint32_t st, j, x, lit, k;
        static const uint8_t BASS[2] = {38, 34};
        song.playing = 0; song.sel = 0;
        for (j = 0; j < NTRK; j++) { steps_clear(&trk[j]); trk[j].p[P_SLEN] = 16; }
        for (j = 0; j < 16u; j++) {                 /* a groove to look at */
            step_t *sp;
            if (!(j & 1u)) { sp = &trk[0].step[j]; sp->n = 1; sp->note[0] = (uint8_t)(BASS[j / 8u] + ((j & 2u) ? 12 : 0)); sp->time = ST_NOTE; sp->vel = 100; }
            if ((j % 4u) == 2u) { sp = &trk[1].step[j]; sp->n = 3; sp->note[0] = 65; sp->note[1] = 69; sp->note[2] = 72; sp->time = ST_NOTE; sp->vel = 100; }
            if (!(j % 8u)) { sp = &trk[2].step[j]; sp->n = 2; sp->note[0] = 57; sp->note[1] = 64; sp->time = ST_NOTE; sp->vel = 100; }
            memset(&TBRK->bstep[j], 0, sizeof(bstep_t));  /* the break: every step, slice 3 on the backbeats */
            TBRK->bstep[j].on = 1;
            TBRK->bstep[j].slice = (uint8_t)((j % 8u) == 4u ? 3u : 0u);
            TBRK->bstep[j].lvl = (uint8_t)((j & 1u) ? LV_SOFT : LV_NORM);
            for (k = 0; k < 4u; k++) trk[k].seq_active = 1;
        }
        go_home(); frames(20);                      /* (the knobs quiet after the layers above) */
        transport_req = 1; frames(30);
        check(!vis_shown(), "visualiser: closed on the TRACKS screen");
        vis_open(); frames(2);                      /* (FMX: HOME on the DECK is LIVE now; where the visualiser goes: open) */
        check(vis_shown() && vis_on, "visualiser: open");
        vis_style = 0; ui.force = 1;
        for (st = 0; st < VIS_N; st++) {
            char nm[80];
            frames(st == 0u ? 40u : 70u);           /* (past the name's second) */
            ui.force = 1; frames(2);
            for (lit = 0, x = 0; x < 240u * 240u; x++)
                lit += screen[x] != 0;
            snprintf(nm, sizeof nm, "vis-%02u-%s", st + 1u, VIS_NAME[st]);
            for (x = 0; nm[x]; x++) if (nm[x] == ' ') nm[x] = '-';
            ppm(nm);
            snprintf(nm, sizeof nm, "visualiser %2u %-12s draws (%u px lit)", st + 1u, VIS_NAME[st], lit);
            check(lit > (st == 3u ? 400u : 1500u) && vis_style == st, nm);   /* (Lissajous: points) */
            if (st == 0u) {   /* the DRUMMER playing the BREAK: always moving (DRUM_ANIM=1: its frames as PPMs) */
                uint32_t fi, changes = 0, nd = 0, longest = 0, run = 0, same = 1;
                uint8_t last = dv.pose;
                blit_px = 0;
                for (fi = 0; fi < 250u; fi++) {
                    frame();
                    if (!ui.msg_t) {                   /* what is on the screen: the state drawn whole (bit for bit) */
                        static uint16_t want[240 * 240];
                        static uint8_t fb[240 * 30];
                        static const uint8_t ON[3] = {DF_LOGO, 0, 0};
                        uint32_t q, yy, xx, fr[3];
                        fr[0] = ON[0];
                        fr[1] = dv.pose;
                        fr[2] = dv.kick ? DF_KICK_HIT : DF_KICK_OFF;
                        for (q = 0; q < 240u * 240u; q++) want[q] = 0xFFFFu;
                        for (q = 0; q < 3u; q++) {
                            const drum_frame_t *r = &DRUM_F[fr[q]];
                            uint32_t st = (r->w + 7u) >> 3, dy = q ? dv.bob : 0u;
                            drum_decode(fr[q], fb);
                            for (yy = 0; yy < r->h; yy++)
                                for (xx = 0; xx < r->w; xx++)
                                    if ((fb[yy * st + (xx >> 3)] >> (7u - (xx & 7u))) & 1u && r->y + yy + dy < 240u)
                                        want[(r->y + yy + dy) * 240u + r->x + xx] = 0;
                        }
                        same &= !memcmp(want, screen, sizeof want);
                    }
                    if (dv.pose != last) {
                        changes++;
                        run = 0;
                    } else if (++run > longest) {
                        longest = run;
                    }
                    nd += dv.pose >= DF_ND1;
                    last = dv.pose;
                    if (getenv("DRUM_ANIM") && (fi & 1u)) {
                        char an[32];
                        snprintf(an, sizeof an, "drummer-%03u", fi / 2u);
                        ppm(an);
                    }
                }
                snprintf(nm, sizeof nm, "DRUMMER, 4 s of the BREAK: always moving (%u poses, at most %u frames still)",
                         changes, longest);
                check(changes >= 30u && longest <= 20u && !nd, nm);
                check(same, "DRUMMER: only what changed drawn, the screen always the whole picture (bit for bit)");
                snprintf(nm, sizeof nm, "DRUMMER: %u%% of the screen sent a second (every other frame whole: 3000%%)",
                         (unsigned)(blit_px / 4u * 100u / (240u * 240u)));
                check(blit_px / 4u < 240u * 240u * 18u, nm);
            }
            encs[panel.enc[EN_SELECT]] = 1; frame();
        }
        {   /* MASTER at 0: silence out, the picture as if MASTER were all the way up */
            uint32_t m0 = song.master_q12, pk = 0, op = 0, b;
            static int32_t o2[CTL * 2];
            song.master_q12 = 0; frames(20);
            for (j = 0; j < SCOPE_N; j++) { uint32_t a = (uint32_t)(scope_buf[j] < 0 ? -scope_buf[j] : scope_buf[j]); if (a > pk) pk = a; }
            for (b = 0; b < 200u; b++) { mix_block(o2, CTL); for (j = 0; j < CTL * 2u; j++) { uint32_t a = (uint32_t)(o2[j] < 0 ? -o2[j] : o2[j]); if (a > op) op = a; } }
            ui.force = 1; frames(2);
            for (lit = 0, x = 0; x < 240u * 240u; x++) lit += screen[x] != 0;
            check(op < 64u && pk > 2000u && lit > 1500u, "visualiser: MASTER at 0: no sound, the picture as at full volume");
            song.master_q12 = m0; frames(4);
        }
        check(vis_style == 0u, "visualiser: SELECT goes round (one style)");
        encs[panel.enc[EN_SELECT]] = -1; frames(2);
        check(vis_style == 0u && (lights_word() >> 17 & 15u) == 0u, "visualiser: SELECT left: POLY, the only one");
        lights_from_word(lights_word());
        check(vis_style == 0u, "visualiser: the style read back");
        { int16_t bpm = song.g[G_BPM], lv = trk[0].p[P_LEVEL];
          encs[panel.enc[EN_K2]] = 5; frame();
          check(trk[0].p[P_LEVEL] == lv && song.g[G_BPM] == bpm, "visualiser: KNOB 2 (the hidden TRACKS screen) edits nothing, SELECT is not the tempo"); }
        press(B_GLO); frames(40);                  /* (held past a tap) */
        check(ui.layer == LY_MIX, "visualiser: a layer held shows its screen");
        release(B_GLO); frames(4);
        check(vis_shown(), "visualiser: ... and it comes back");
        tap(B_HOME); frames(2);
        check(!vis_shown() && !vis_on, "visualiser: HOME again closes it");
        vis_open(); frames(2);
        tap(B_LFO); frames(2);
        check(!vis_shown() && !vis_on && !strcmp(PAGES[ui.page].title, "LFO"), "visualiser: a page button closes it and opens its page");
        transport_req = 2; frames(4);
        for (j = 0; j < NTRK; j++) steps_clear(&trk[j]);
        vis_style = 0; song.sel = 0; go_home(); frames(20);
    }
    {   /* the DRUMMER: its sprites as drawn (the decoder against gen_drummer.py's CRCs); the BREAK's snares found;
         * the poses: a style's snare and other hits, the hands, the kick, NO DRUMS when still or at HALFTIME */
        static uint8_t fb[240 * 30];
        uint32_t f, okc = 1, b, ok = 1, n;
        int16_t sty = BPV(BP_STYLE);
        for (f = 0; f < DRUM_NF; f++) {
            uint32_t c = 0xFFFFFFFFu, i, k;
            drum_decode(f, fb);
            for (i = 0; i < drum_bytes(f); i++)
                for (c ^= fb[i], k = 0; k < 8u; k++)
                    c = c & 1u ? (c >> 1) ^ 0xEDB88320u : c >> 1;
            okc &= (c ^ 0xFFFFFFFFu) == DRUM_F[f].crc;
        }
        check(okc, "DRUMMER: every sprite decoded as drawn (CRC)");
        for (b = 0, n = 0; b < BP_NBANK; b++)
            if (brkp_bank_src(b)) {
                n++;
                ok &= bpb[b].snares && !(bpb[b].snares & bpb[b].kicks);
                printf("ui:   loop %c: kicks %02x snares %02x\n", 'A' + b, bpb[b].kicks, bpb[b].snares);
            }
        check(n && ok, "DRUMMER: the built-in loops' snares found (none on a kick)");
        transport_req = 1; frames(4);
        BPV(BP_STYLE) = BS_JUNGLE;
        vis_bo++; vis_update();                       /* (a hit now: playing, not still) */
        dv.phrase = clk_beat >> 2;                    /* (this bar: the style set here) */
        dv.style = DS_BOTH; dv.hold = 0;
        vis_bs++; vis_update();
        ok = dv.pose == DF_BOTH_HIT;
        vis_update(); ok &= dv.pose == DF_BOTH_HIT;
        vis_update();
        check(ok && dv.pose == DF_BOTH_OFF, "DRUMMER: BOTH: a snare, the hit pose two frames, then the rest pose");
        vis_update(); vis_update();
        dv.style = DS_LSNARE; dv.hand = 0;
        vis_bs++; vis_update(); ok = dv.pose == DF_LSN_RH;
        vis_bs++; vis_update(); ok &= dv.pose == DF_LSN_LH;
        vis_bo++; vis_update(); ok &= dv.pose == DF_LCY_RH;
        vis_bo++; vis_update(); ok &= dv.pose == DF_LCY_LH;
        check(ok, "DRUMMER: L SNARE: a hand each snare, the other hits the left cymbal, a hand each");
        dv.style = DS_ALTER;
        vis_bs++; vis_update(); ok = dv.pose == DF_ALTER_R;
        vis_bs++; vis_update(); ok &= dv.pose == DF_ALTER_L;
        vis_bk++; vis_update(); ok &= dv.kick == 4u && dv.bob == 4u && dv.pose >= DF_LCY_LH && dv.pose <= DF_RCY_RH;
        vis_update(); vis_update(); vis_update(); vis_update(); ok &= !dv.kick && !dv.bob;
        check(ok, "DRUMMER: ALTER: drum to drum; a kick: the kick's hit, the kit thumping, the hands on the cymbals");
        {   /* the groove: a 16th without a hit, the hands go on */
            uint8_t p0 = dv.pose;
            clk_pos += BEAT_U / 4u;
            if (clk_pos >= BEAT_U) { clk_pos -= BEAT_U; clk_beat++; }
            vis_update();
            check(dv.pose != p0 && dv.pose >= DF_LCY_LH && dv.pose <= DF_RCY_RH, "DRUMMER: a 16th without a hit: the hands go on");
        }
        BPV(BP_STYLE) = BS_HALFTIME;
        vis_bs++; vis_update();
        check(dv.pose >= DF_ND1, "DRUMMER: HALFTIME: NO DRUMS");
        BPV(BP_STYLE) = BS_JUNGLE;
        clk_beat += 3u;                               /* (three beats on, no hit) */
        vis_update();
        check(dv.pose >= DF_ND1, "DRUMMER: two beats without a hit: NO DRUMS");
        transport_req = 2; frames(4);
        vis_update();
        check(dv.pose >= DF_ND1, "DRUMMER: stopped: NO DRUMS");
        BPV(BP_STYLE) = sty;
    }

    {   /* LIVE (ui_live.c): HOME on the DECK opens it; PLAY plays the song; a section key asks, it comes on the phrase and
         * (AUTO off) plays on; a track out on Q; GAP held; a black key plays the focus; HOME back to STUDIO */
        uint32_t i, was = 0, at = 0, bars, v, on;
        transport_req = 2; frames(4);
        song.sel = 0; go_home(); vis_on = 0; ui.force = 1; frames(2);
        tap(B_HOME); frames(2);
        check(live_on && kb_live, "LIVE: HOME on the DECK opens it");
        ui.force = 1; frame(); ppm("page-live");
        tap(B_PLAY); frames(10);
        check(song.playing && arrangement_enabled && arrangement_clock.running && live_role() == AR_INTRO,
              "LIVE: PLAY plays the song (AUTO SONG), from the INTRO");
        q_mode = QM_4BAR;
        key(key_of_white(2));
        check(live_pend == AR_DROP && live_auto && !arrangement_clock.hold, "LIVE: white key 3 asks for the DROP; the mode stays AUTO");
        ui.force = 1; frame(); ppm("page-live-wait");
        for (i = 0; i < 6u * 90u && live_role() != AR_DROP; i++) {
            was = arrangement_clock.bar;
            frame();
        }
        at = arrangement_clock.bar;
        check(live_role() == AR_DROP && live_pend < 0 && (was + 1u) % 4u == 0u && at == 0u,
              "LIVE: the DROP comes on the phrase (after a 4th bar), from its first bar");
        check(live_auto && arrangement.entry[(arrangement_clock.index + 1u) % arrangement.count].scene != AR_DROP,
              "... AUTO still on: the song goes on from the DROP");
        tap(B_SEQ); frames(2);
        check(!live_auto && arrangement_clock.hold, "LIVE: SEQ: AUTO off (the only switch)");
        bars = arrangement.entry[arrangement_clock.index].bars;
        frames((bars + 2u) * 90u);
        check(live_role() == AR_DROP && song.playing, "LIVE: AUTO off: the DROP plays on past its bars");
        tap(B_HOME); frames(2);
        check(!live_on && arrangement_clock.hold, "LIVE: AUTO off, HOME to STUDIO: the section still held");
        frames((bars + 4u) * 90u);                      /* (whole phrases: the Q 4 BARS checks below keep their timing) */
        check(live_role() == AR_DROP && song.playing, "... STUDIO: the DROP plays on past its bars (time to edit)");
        go_home(); tap(B_HOME); frames(2);
        check(live_on && !live_auto && arrangement_clock.hold && live_role() == AR_DROP, "... HOME back to LIVE: still MAN");
        key(key_of_white(9));
        check(q_val(0, P_MUTE) == 1 && trk[0].p[P_MUTE] == 0, "LIVE: white key 10 takes the BASS out: it waits for the phrase");
        frames(4u * 90u + 10u);
        check(trk[0].p[P_MUTE] == 1 && trk_silent(&trk[0]), "... and is out on it");
        key(key_of_white(9)); frames(4u * 90u + 10u);
        check(trk[0].p[P_MUTE] == 0, "... white key 10 again: back in");
        fm1_in.notes |= 1u << key_of_white(12); frames(3);
        check(live_gap && trk_silent(TBRK), "LIVE: white key 13 held: GAP, the BREAK out");
        ui.force = 1; frame(); ppm("page-live-gap");
        fm1_in.notes = 0; frames(3);
        check(!live_gap && !trk_silent(TBRK), "... let go: back");
        {   /* REWIND held + a section key: the rewind resolves into that section on the bar, from its first bar, with no
             * lead of its own (no tape stop into a DROP 2); let go after: nothing more */
            static const uint8_t TO[2] = {AR_DROP2, AR_DROP};
            uint32_t k16 = 1u << key_of_white(15), j, ts, nb, b0;
            for (j = 0; j < 2u; j++) {
                fm1_in.notes |= k16; frames(3);
                check(punch.req == PX_REWIND && (live_keys_lit() & k16), "LIVE: white key 16 held: REWIND");
                fm1_in.notes |= 1u << key_of_white(TO[j]); frames(2);
                fm1_in.notes &= ~(1u << key_of_white(TO[j])); frame();
                check(live_pend == TO[j] && live_rw_cut && arrangement_clock.jump == as_rw_to && !(live_keys_lit() & k16),
                      j ? "... + white key 3: into the DROP" : "... + white key 6: it resolves into the DROP 2 (key 16 out)");
                for (i = 0, ts = 0, nb = 0, b0 = clk_beat; i < 4u * 90u && live_role() != TO[j]; i++) {
                    ts |= as_keys & (1u << PA_TSTOP);
                    frame();
                    nb += clk_beat != b0;               /* (the beats gone by) */
                    b0 = clk_beat;
                }
                check(live_role() == TO[j] && arrangement_clock.bar == 0u && nb <= 4u && !ts,
                      "... it comes on the next bar, from its first, no tape stop before it");
                frames(4);
                check(punch.req < 0 && live_pend < 0 && as_rw_to < 0, "... the rewind over on that bar");
                fm1_in.notes = 0; frames(3);
                check(live_role() == TO[j] && arrangement_clock.jump < 0 && punch.req < 0,
                      "... REWIND let go after: it plays on (not again from its start)");
            }
        }
        song.sel = 2;                                   /* (the focus: CHOP, the RAVE HITS kit in USR1) */
        {
            uint32_t m0 = cs_mode(), b0 = 0, n0 = 0;
            fm1_in.notes |= 1u << 3; frames(2);         /* (black key 2: SLICE) */
            fm1_in.notes = 0; frames(2);
            check(m0 == CM_PADS && cs_mode() == CM_SLICE && live_on, "LIVE, focus CHOP: black key 2: SLICE, LIVE stays");
            fm1_in.notes |= 1u << 1; frames(2);         /* (black key 1: PADS again) */
            fm1_in.notes = 0; frames(2);
            check(cs_mode() == CM_PADS && cp_kit(&b0, &n0), "... black key 1: PADS, its kit");
            fm1_in.notes |= 1u << 8; frames(2);         /* (black key 4: the kit's first hit) */
            for (v = 0, on = 0; v < NVOICE; v++)
                on |= trk[2].v[v].active && trk[2].v[v].gate && trk[2].v[v].note == b0;
            fm1_in.notes = 0; frames(2);
            check(on, "LIVE: black key 4 plays the focus track (CHOP: its first hit)");
        }
        encs[panel.enc[EN_K3]] = 5; frames(2);
        check(song.g[G_SPACE] > 64, "LIVE: KNOB 3 SPACE");
        tap(B_SEQ); frames(2);
        check(live_auto && !arrangement_clock.hold, "LIVE: SEQ: AUTO on again");
        {   /* PRESETS under Q: each detent goes on from the one that waits, not from the sound that plays until the
             * bar; its name is shown at once; a user preset loaded after drops what waited */
            uint32_t total, c0, i;
            int16_t pp;
            song.sel = 1; frames(2);
            c0 = preset_pos(&total);
            for (i = 0; i < 3u; i++) {
                encs[panel.enc[EN_PRESET]] = 1; frames(2);
            }
            check(q_pend(1, QX_PRESET, &pp) && preset_pos(&total) == (c0 + 3u) % total,
                  "LIVE: PRESETS under Q: three detents, three on (it goes on from what waits)");
            check(ui.msg[0] == '@', "... the name is said at once: \"@bar NAME\"");
            q_drop(1);
            check(!q_pend(1, QX_PRESET, &pp) && !q_waits(), "... q_drop: nothing waits for the track");
            q_clear();
            song.sel = 2;
        }
        tap(B_ENV); frames(4);
        check(vis_on && vis_shown_last && live_on, "LIVE: ENV: the show, the DRUMMER");
        key(key_of_white(9)); frames(2);
        check(q_val(0, P_MUTE) == 1, "... the keys still LIVE's (white key 10: the BASS out, waiting)");
        q_clear();
        ui.force = 1; frame(); ppm("page-live-show");
        tap(B_ENV); frames(2);
        check(!vis_on && live_shown, "... ENV again: back to LIVE");
        {   /* the UI audit's fixes: PLAY in the menu, the menu back where it was opened, SAVE from
             * LIVE, the set guarded when the work is not saved */
            uint32_t l0 = loads, was = song.playing;
            if (song.playing) { transport_req = 2; frames(4); }
            press(B_HOME); frames(50); release(B_HOME); frames(2);
            check(ui.menu && live_on, "LIVE: HOME held: the menu, over LIVE");
            tap(B_PLAY); frames(4);
            check(song.playing, "... PLAY in the menu plays (N1)");
            tap(B_PLAY); frames(4);
            check(!song.playing, "... and stops");
            press(B_HOME); frames(50); release(B_HOME); frames(2);
            check(!ui.menu && live_on, "... HOME held again: back on LIVE");
            {
                int16_t f0 = song.g[G_FILT];
                proj_touched = 0;
                encs[panel.enc[EN_K1]] = 1; frame();
                check(!proj_touched, "LIVE: a knob turned is playing, not editing: the song not marked edited");
                song.g[G_FILT] = f0;
            }
            live_song = 0; unsaved = 1;
            tap(B_OCTUP); frames(2);
            check(loads == l0, "LIVE stopped, the work not saved: OCT+ loads nothing yet (N3)");
            tap(B_OCTUP); frames(2);
            check(loads == l0 + 1u, "... the same OCT again: it loads");
            unsaved = 0; live_song = 0;
            tap(B_OCTUP); frames(2);
            check(loads == l0 + 2u, "... the work saved: OCT+ loads at once");
            live_song = 0;
            tap(B_SAVE); frames(2);
            check(!live_on && on_song_page() && sg_page == SG_SET, "LIVE: SAVE: the SONG screen's SET page");
            tap(B_HOME); frames(3);
            check(live_on, "... HOME there: back to LIVE");
            if (was) { transport_req = 1; frames(4); }
        }
        {   /* the set (the stubs: songs in slots 1 and 2): OCT+ the next, white key 8 the way (the last tap asked of the
             * arranger: as_tr_ask), the arranger taking it (as_tr_way: what plays now), then that song (as_swapped); the
             * bridge itself: song_audio_test */
            uint32_t s0;
            live_song = 0; live_next = -1; live_next_for = 0;
            tap(B_OCTUP); frames(2);
            check(live_next == 1, "LIVE: OCT+: the next song, slot 2");
            check(live_set_t > 0, "... OCT: the set shows in the top row for a few seconds");
            ui.force = 1; frame(); ppm("page-live-set");
            frames(160);
            check(!live_set_t, "... and goes again");
            key(key_of_white(7));
            check(live_tr == TR_BRIDGE && as_tr_ask == TR_BRIDGE && as_tr_slot == 2u,
                  "LIVE: white key 8: the way there, BRIDGE, asked of the arranger for slot 2");
            key(key_of_white(7)); key(key_of_white(7)); key(key_of_white(7));
            check(live_tr == TR_NONE && !as_tr_ask, "... tapped round: SWAP, CUT, none (nothing asked: cancelled)");
            key(key_of_white(7)); key(key_of_white(7));
            check(live_tr == TR_SWAP && as_tr_ask == TR_SWAP, "... and on: the last tap is what is asked (SWAP)");
            key(key_of_white(7)); key(key_of_white(7)); key(key_of_white(7));
            check(live_tr == TR_BRIDGE && as_tr_ask == TR_BRIDGE, "... BRIDGE again");
            {   /* asked: white key 8 and AUTO flash twice a second, together */
                uint32_t g, ups = 0, was8 = 0, same = 1;
                for (g = 0; g < 200u; g++) {            /* (from the flash going out: the two that follow are in the next second) */
                    uint32_t b = live_dblink();
                    frame();
                    if (b && !live_dblink())
                        break;
                }
                for (g = 0; g < 63u; g++) {             /* (~1 s of frames) */
                    uint32_t on8 = (keys_lit() >> key_of_white(7)) & 1u;
                    ups += on8 && !was8;
                    was8 = on8;
                    same &= on8 == (uint32_t)live_dblink();
                    frame();
                }
                check(ups == 2u && same, "... asked: white key 8 flashes twice a second, AUTO with it");
            }
            ui.force = 1; frame(); ppm("page-live-next");
            as_tr_ask = 0; as_tr_way = TR_BRIDGE; as_tr_to = 2; frames(2);   /* (the arranger takes it, on the phrase) */
            check(live_tr == TR_NONE && live_next_on(), "... taken: the way under way, white key 8 still flashing");
            ui.force = 1; frame(); ppm("page-live-bridge");
            key(key_of_white(7)); key(key_of_white(2)); frames(2);
            check(!as_tr_ask && live_pend < 0, "... under way: white key 8 and the sections do nothing (it plays out)");
            tap(B_OCTDN); frames(2);
            check(live_next == 1, "... nor OCT (the song it goes to stays)");
            s0 = as_swapped;
            as_tr_way = 0; as_tr_to = 0; as_swapped_to = 2; as_swapped = (uint8_t)(s0 + 1u); frames(2);   /* (swapped) */
            check(live_song == 1 && live_tr == TR_NONE && live_target() < 0 && !as_auto_next && !as_tr_slot,
                  "... swapped: song 2 plays, the set's last (no next song: AUTO ends the set)");
            key(key_of_white(7)); frames(2);
            check(live_next == 0 && as_tr_ask == TR_BRIDGE && as_tr_slot == 1u,
                  "... the last song, white key 8: round to song 1, asked for");
            key(key_of_white(7)); key(key_of_white(7)); key(key_of_white(7)); frames(2);
            check(live_target() < 0 && !as_tr_ask && !as_auto_next, "... tapped to none: the set ends again (AUTO goes nowhere)");
            live_song = 0; live_next_for = -2; frames(2);
            check(live_next < 0 && live_target() == 1 && as_auto_next == (live_auto ? 2u : 0u),
                  "... from song 1 again, nothing picked: AUTO's next is the set's, song 2");
        }
        {   /* the FX panel: FX held; black key 1 (BREAK) + white key 1: DUB VERB on the BREAK; black key 5 twice
             * (LATCH TRACKS + FX), white 2: ECHO THROW stuck on all; KNOB 1 AMOUNT; let go: the panel gone, the latch on; black key 6 clears it */
            uint32_t a0 = lfx.amt;
            int16_t sp0 = song.g[G_SPACE];
            fm1_in.buttons |= BT(B_FX); frames(3);
            check(live_fx_held(), "LIVE: FX held: the FX panel");
            fm1_in.notes = 1u << 1; frames(1);             /* (black key 1) */
            fm1_in.notes |= 1u << key_of_white(0); frames(3);
            check((lfx.on & 1u) && lfx.tgt[0] == 1u << TRK_BRK && lfx.rev[1][TRK_BRK] && !lfx.rev[1][0],
                  "... black key 1 + white key 1: DUB VERB, on the BREAK only");
            ui.force = 1; frame(); ppm("page-live-fx");
            fm1_in.notes = 0; frames(3);
            check(!(lfx.on & 1u) && !lfx.rev[1][TRK_BRK], "... let go: off");
            key(10); key(10);                              /* (black key 5 twice: LATCH TRACKS + FX) */
            key(key_of_white(1)); frames(2);
            check(lfx.mode == LM_ALL && (lfx.latch & 2u) && lfx.tgt[1] == 15u && lfx.dly[1][0] && lfx.dly[1][TRK_BRK],
                  "... black key 5 twice, white key 2 tapped: ECHO THROW stuck, on all");
            encs[panel.enc[EN_K1]] = -10; frames(2);
            check(lfx.amt < a0 && song.g[G_SPACE] == sp0 && !song.g[G_FILT], "... KNOB 1: its AMOUNT (not FILTER's)");
            fm1_in.buttons &= ~BT(B_FX); frames(3);
            check(!live_fx_held() && (lfx.latch & 2u), "... FX let go: the panel gone, the echo still latched");
            fm1_in.buttons |= BT(B_FX); frames(2);
            fm1_in.notes = 1u << 13; frames(3); fm1_in.notes = 0; frames(3);   /* (black key 6: CLEAR) */
            fm1_in.buttons &= ~BT(B_FX); frames(3);
            check(!lfx.latch, "... black key 6: CLEAR, the latch off");
            fm1_in.buttons |= BT(B_FX); frames(2);
            fm1_in.notes = 1u << 8; frames(1);             /* (black key 4: CHOP) */
            fm1_in.notes |= 1u << key_of_white(4); frames(3);
            check(lt.fx == LX_FREEZE && lfx.ins == 4u, "... black key 4 + white key 5: FREEZE, the CHOP through the TIME stem");
            fm1_in.notes = 0; frames(40);
            check(!lfx.ins && !lt.fx, "... let go: faded out, the CHOP back in the mix");
            fm1_in.notes = 1u << 1; frames(1);             /* (black key 1: BREAK) */
            fm1_in.notes |= 1u << key_of_white(8); frames(3);
            check(lfx.col[0] == 1u << TRK_BRK && (lfx.colact >> TRK_BRK) & 1u, "... black key 1 + white key 9: DUB FILTER on the BREAK");
            fm1_in.notes = 0; frames(60);
            check(!lfx.col[0] && !lfx.colact, "... let go: open again, then off");
            fm1_in.buttons &= ~BT(B_FX); frames(3);
            lfx.amt = (uint8_t)a0;
            /* LATCH, three ways round: black key 5 tapped: TRACKS; black key 2 (BASS) tapped sticks; white key 3 held
             * only; black key 5 again: TRACKS + FX, white key 3 tapped sticks on the BASS, tapped again goes off; FX + HOME
             * locks the panel open; black key 6: everything off; HOME lets go */
            fm1_in.buttons |= BT(B_FX); frames(2);
            key(LIVE_BLACK[4]);
            check(lfx.mode == LM_TRACKS && !lfx.tsel && (keys_lit() >> LIVE_BLACK[4] & 1u), "LIVE FX: black key 5 tapped: LATCH TRACKS, lit");
            ui.force = 1; frame(); ppm("page-live-fx-latch1");
            key(LIVE_BLACK[1]);
            check(lfx.tsel == 2u && (keys_lit() >> LIVE_BLACK[1] & 1u), "... black key 2 tapped: the BASS stuck, lit");
            fm1_in.notes = 1u << key_of_white(2); frames(2);
            check((lfx.held & 4u) && lfx.tgt[2] == 1u && !lfx.latch, "... white key 3 held: SPARKLE on the BASS, held only");
            fm1_in.notes = 0; frames(2);
            check(!lfx.held && !lfx.latch, "... let go: off");
            key(LIVE_BLACK[4]);
            check(lfx.mode == LM_ALL && lfx.tsel == 2u, "... black key 5 again: LATCH TRACKS + FX, the BASS still stuck");
            key(key_of_white(2)); frames(2);
            check((lfx.latch & 4u) && lfx.tgt[2] == 1u && lfx.spk[1][0] && !lfx.spk[1][TRK_BRK],
                  "... white key 3 tapped: SPARKLE stuck, on the BASS only");
            key(LIVE_BLACK[0]); key(LIVE_BLACK[3]);
            check(lfx.tsel == 11u && (lfx.latch & 4u) && lfx.tgt[2] == 1u, "... black keys 1, 4 tapped: three tracks stuck (the next effect's)");
            ui.force = 1; frame(); ppm("page-live-fx-latch");
            fm1_in.buttons &= ~BT(B_FX); frames(3);
            check(!live_fx_held() && (lfx.latch & 4u) && lfx.mode == LM_ALL, "... FX let go: still stuck, LATCH still on");
            fm1_in.buttons |= BT(B_FX); frames(2); tap(B_HOME); fm1_in.buttons &= ~BT(B_FX); frames(3);
            check(live_on && ly_lock == LY_FX && live_fx_held(), "LIVE: FX held + HOME: the panel locked open (still LIVE)");
            key(key_of_white(2)); frames(2);
            check(!(lfx.latch & 4u), "... locked, white key 3 tapped again: SPARKLE off");
            key(key_of_white(0)); key(LIVE_BLACK[5]); frames(2);
            check(!lfx.latch && !lfx.mode && !lfx.tsel, "... black key 6: CLEAR, nothing stuck, LATCH off");
            fm1_in.notes = 1u << key_of_white(0); frames(2);
            check(lfx.held == 1u && lfx.tgt[0] == 15u, "... LATCH off: a white key held again, on all");
            fm1_in.notes = 0; frames(2);
            tap(B_HOME); frames(2);
            check(live_on && ly_lock == LY_PLAY && !live_fx_held(), "... HOME: let go, LIVE's own screen again");
            fm1_in.buttons |= BT(B_FX); frames(2); tap(B_HOME); fm1_in.buttons &= ~BT(B_FX); frames(2);
            tap(B_SEQ); frames(2);
            check(live_on && ly_lock == LY_PLAY && live_auto, "... locked, SEQ: let go, and only that (AUTO as it was)");
        }
        {   /* stopped: a section key arms it, PLAY starts from it (its first bar, no INTRO first); STOP, PLAY: from there */
            transport_req = 2; frames(4);
            check(!song.playing && live_arm() == (int32_t)arr_resume, "LIVE: stopped: the section it was left in is lit");
            key(key_of_white(2));
            check(arr_armed == AR_DROP && live_arm() == AR_DROP, "LIVE: stopped: white key 3 arms the DROP");
            tap(B_PLAY); frames(10);
            check(song.playing && live_role() == AR_DROP && arrangement_clock.bar == 0u && arr_armed < 0 && live_pend < 0,
                  "... PLAY: from the DROP's first bar, no INTRO first");
            transport_req = 2; frames(4);
            tap(B_PLAY); frames(10);
            check(song.playing && live_role() == AR_DROP, "... STOP, PLAY: on from the section it was left in");
            transport_req = 2; frames(4);
        }
        {   /* SEL: the focus track's screen in STUDIO, as SEL on the DECK; on the release, once (no second SEL there) */
            uint32_t sl = cs_slice();
            song.sel = 2;
            press(B_SCL); frames(10);
            check(live_on, "LIVE: SEL held: still LIVE");
            release(B_SCL); frames(4);
            check(!live_on && !kb_live && on_chop_page() && cs_slice() == sl,
                  "LIVE: SEL let go: STUDIO, the focus track's screen (CHOP), its mode as it was");
            tap(B_HOME); frames(2);
            check(live_on && kb_live && !on_chop_page(), "... HOME there: back to LIVE (not the DECK)");
            song.sel = 0;
            tap(B_SCL); frames(4);
            check(!live_on && on_bass_page(), "LIVE: focus BASS, SEL: the BASS screen");
            encs[panel.enc[EN_ALGO]] = 1; frames(2);
            tap(B_HOME); frames(2);
            check(live_on, "... ALGORITHM turned there, HOME: still back to LIVE");
            tap(B_HOME); frames(2);
            check(!live_on && !ui.home && cur_page()->scope == SC_TRK, "... HOME in LIVE: STUDIO's DECK, as before");
            song.sel = TRK_BRK;
            tap(B_SCL); frames(2);
            check(on_break_page(), "STUDIO's SEL on the DECK: the track's screen");
            tap(B_HOME); frames(2);
            check(!live_on && cur_page()->scope == SC_TRK, "... HOME: the DECK (it came from STUDIO)");
            tap(B_HOME); frames(2);
            song.sel = 0;
            press(B_FX); frames(4); press(B_SCL); frames(2); release(B_SCL); release(B_FX); frames(4);
            check(live_on, "LIVE: SEL with FX held: not STUDIO (the FX panel's)");
        }
        tap(B_HOME); frames(2);
        check(!live_on && !kb_live && cur_page()->scope == SC_TRK, "LIVE: HOME: back to STUDIO (the DECK)");
        if (!song.playing) {                            /* (LIVE's song playing, then STUDIO) */
            tap(B_HOME); frames(2);
            tap(B_PLAY); frames(10);
            tap(B_HOME); frames(2);
        }
        check(!live_on && song.playing && arrangement_enabled && arrangement_clock.running,
              "STUDIO: the song LIVE started plays on");
        tap(B_PLAY); frames(4);
        check(!song.playing, "STUDIO: PLAY over it: it stops");
        tap(B_PLAY); frames(10);
        check(song.playing && !arrangement_enabled && !arrangement_clock.running,
              "STUDIO: PLAY plays the loop, not AUTO SONG (the song is LIVE's)");
        tap(B_PLAY); frames(4);
        tap(B_SAVE); frames(2);
        tap(B_PLAY); frames(10);
        check(on_song_page() && song.playing && !arrangement_enabled && !arrangement_clock.running,
              "... on the SONG screen too: the loop");
        tap(B_PLAY); frames(4);
        {   /* START on the CHAIN page: where LIVE's PLAY begins; STUDIO keeps it */
            uint32_t st;
            sg_page = SG_SET;
            encs[panel.enc[EN_SELECT]] = 1; frame();
            encs[panel.enc[EN_K4]] = -9; frame();
            encs[panel.enc[EN_K4]] = 2; frame();
            st = sg_start();
            check(sg_page == SG_CHAIN && st > AR_INTRO && sg_in_chain(st), "SONG CHAIN: KNOB 4 START two sections on");
            go_home(); frame();
            tap(B_HOME); frames(2);
            check(live_on && live_arm() == (int32_t)st, "... HOME, HOME: LIVE, that section armed");
            tap(B_PLAY); frames(10);
            check(song.playing && arrangement_enabled && live_role() == st, "... LIVE: PLAY plays the song from it");
            tap(B_PLAY); frames(4);
            tap(B_HOME); frames(2);
            go_home(); tap(B_HOME); frames(2);
            check(live_on && live_arm() == (int32_t)st, "... to STUDIO and back: LIVE still starts there");
        }
        {   /* a MIDI START from a clock master in STUDIO, after LIVE played the song: the loop */
            int16_t sync = song.g[G_SYNC];
            song.g[G_SYNC] = 1;
            tap(B_HOME); frames(2);
            check(arrangement_enabled, "... back in STUDIO: song mode as LIVE's PLAY left it");
            mclk_event(0xFAu, 1u); frames(10);
            check(!live_on && song.playing && !arrangement_enabled && !arrangement_clock.running,
                  "STUDIO: a MIDI START: the loop");
            mclk_event(0xFCu, 1u); frames(4);
            song.g[G_SYNC] = sync;
        }
        arr_armed = -1;
        arr_resume = AR_INTRO;
        song.g[G_SPACE] = 64;
        q_mode = QM_NOW;
    }
    {   /* fuzz: 20000 frames of random buttons (held or tapped), knobs and keys, with the audio running
         * between frames; every draw stays on the screen (lcd_blit / lcd_fill assert it) */
        uint32_t f, seed = 777, held = 0, brk_frames = 0;
        #define R(n) ((seed = seed * 1664525u + 1013904223u) >> 8) % (n)
        song.sel = 0; go_home(); ui.force = 1;
        for (f = 0; f < 20000u; f++) {
            if (R(6) == 0) {
                uint32_t bt = R(NB);
                edges_btn |= 1u << panel.btn[bt];
                if (bt != B_HOME && R(3) == 0) held ^= 1u << panel.btn[bt];
            }
            if (R(30) == 0) held = 0;
            if (R(700) == 0) ly_lock = (uint8_t)R(LY_COUNT);              /* (a layer locked open, at random) */
            fm1_in.buttons = held & ~(1u << panel.btn[B_HOME]);
            if (R(4) == 0) encs[R(7)] += (int32_t)R(5) - 2;
            fm1_in.notes = R(10) == 0 ? (1u << R(27)) : (R(3) ? fm1_in.notes : 0);
            frame();
            brk_frames += on_break_page();
            if (ui.menu) { ui.menu = 0; ui.force = 1; }               /* (the menu is tested above) */
        }
        fm1_in.buttons = 0; fm1_in.notes = 0; frames(4);
        printf("ui: fuzz: %u frames on the BREAK screen\n", brk_frames);
        check(brk_frames > 0u, "fuzz: the BREAK screen reached and used too");
        #undef R
    }
    {   /* NEW on the SET page (after the fuzz: it empties every track): an empty song, no cards, FORM CLUB */
        tap(B_HOME); frames(2);
        go_home(); ui.force = 1; frames(16);
        tap(B_SAVE); frames(2);
        sec_card[AR_DROP].len = 2;
        as_set(2, 5, 1);
        frames(16);
        encs[panel.enc[EN_K4]] = 1; frame();
        encs[panel.enc[EN_K4]] = 1; frame();
        check(on_song_page() && !sec_card[AR_DROP].len && as_form() == 1u && as_var() == 0u && !as_endless() && live_song < 0,
              "SET: KNOB 4 twice: NEW, an empty song (no cards, FORM CLUB, VARIANT 0, no song loaded)");
        go_home(); ui.force = 1; frames(4);
    }
    printf("ui: %s\n", fails ? "FAILED" : "pages, layers (fx, steps, erase, roll, key, mix), layer lock, SAVE (no layer), REC hold, BREAK track and screen, REC, 20000-frame fuzz PASS");
    return fails;
}
#endif
