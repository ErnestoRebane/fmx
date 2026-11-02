/* SPDX-License-Identifier: GPL-3.0-only */
/* AUTO SONG (FMX): song mode plays the loop you made as a whole track, its sections made from it, none of them
 * stored. A section is a role, what it does to the session for its bars (the tracks it leaves out, the moves (punch.c)
 * it holds, those of its last bar, its filters and the BREAK's STYLE):
 *   INTRO      CHORDS and CHOP alone, the CHOP coming out of a low-pass (its FILT opening over the section, late);
 *              a WASH over its last bar (a high-pass rising into an echo, the reverb open)
 *   BUILD      the BREAK in through a high-pass coming down (~680 Hz to ~200 Hz, late), no BASS: no low end until
 *              the drop; 4 dB under the rest (the drop comes in above it); the BREAK rolled at its end, a WASH
 *   DROP       everything, dry, its first hit a kick of the loop's (whatever the STYLE), thrown into the dub reverb
 *              and the echo; the BASS's first note dives in from an octave up over ~200 ms (the sub drop); a FILL at
 *              its end
 *   ROLL       BREAK (STYLE ROLLER), BASS and CHOP: the pads out; a FILL at its end
 *   BREAKDOWN  the BREAK and the BASS out, the reverb open; a WASH over its last bar
 *   DROP 2     as DROP (the sub drop too), with a new BASS phrase (its next VARIANT); the break's character is the
 *              loop's own
 *   OUTRO      BREAK and CHORDS, the BREAK STRAIGHT, then over its second half slowing down (HALFTIME) as the whole
 *              mix fades out
 * The last 2 beats before a DROP are the gap (as_gap): the tracks out on its 1, the WASH's echo ringing on and
 * fading to silence over 3/4 of it, then the echo's and the reverb's lines emptied (fx_hush), so the drop's first
 * beat comes in dry out of nothing. The last bar's moves (WASH, the roll) come before the gap. The beat before a DROP and a DROP 2 is a tape stop (punch.c TSTOP, as_drop DR_TSTOP; the gap is SWAP's): the
 * mix slowing down to a stop, the tails emptied under it; the DROP 2 at full speed. Each section starts from the
 * loop's own STYLE and filters (song_keep).
 * The chain is made from FORM (SHORT 32 bars, CLUB 64, LONG 128) and VARIANT (0: the form as it is; others move
 * the lengths, add or leave out a ROLL or a second BUILD), at PLAY (arranger.c arrangement_start) and when either
 * changes. MODE SONG plays it once and stops; ENDLESS goes on after the second drop, a new round of BUILD .. DROP 2
 * each time (the same VARIANT, another round: other choices). The moves and mutes go through punch.c
 * (perf_block: as_keys, as_mute); the BASS phrase is made in the main loop (as_poll) the bar before its section and
 * swapped in on its first block. Song mode keeps the loop you made (song_backup at PLAY, song_restore at STOP). */
#define AM_BASS 1u                                      /* (perf_mute bits: track k) */
#define AM_CHORDS 2u
#define AM_CHOP 4u
#define AM_BREAK 8u
static const struct {
    uint8_t mute;                                       /* the tracks out */
    uint16_t hold, last, half;                          /* the moves held through it, over its last bar, its last 2 beats */
} AR[AR_N] = {
    [AR_INTRO] = {AM_BASS | AM_BREAK, 0, 1u << PA_WASH, 0},
    [AR_BUILD] = {AM_BASS, 0, 1u << PA_WASH, 1u << PA_ROLL16},
    [AR_DROP] = {0, 0, 0, 1u << PA_FILL},
    [AR_ROLL] = {AM_CHORDS, 0, 0, 1u << PA_FILL},
    [AR_BREAKDOWN] = {AM_BASS | AM_BREAK, 1u << PA_VERB, 1u << PA_WASH, 0},
    [AR_DROP2] = {0, 0, 0, 1u << PA_FILL},
    [AR_OUTRO] = {AM_BASS | AM_CHOP, 0, 0, 0},
};
/* FMX: the seams between the sections (2026-10-21). SEAM_CLEAN: a section starts on its bar as it is (as before);
 * SEAM_DUB: over the bar before it, the tracks it takes out go the dub way (the BREAK and the CHOP thrown into a
 * dotted-eighth echo on the last beat, dry out on the one, the echo ringing on 2 bars; the BASS closing through a
 * low-pass over the last 2 beats; the pads thrown into the reverb) and those it brings in come up through a low-pass
 * over that bar (no riser). as_sub: in a ROLL the pads go under water (low-passed, not out), and come up out of it */
enum { SEAM_CLEAN, SEAM_DUB };
/* the way into a DROP: as_gap beats of silence (1, 2 or 4); as_gap_tail 1: the WASH's echo rings into it and fades
 * (the tracks out), instead of the cut. The user chose 2 beats with the echo by ear (2026-10-25: 1 beat cut was too
 * short, unmusical); song_audio_test gaps renders the others */
static uint8_t as_gap = 2, as_gap_tail = 1;
/* the drop itself (2026-10-25, the user: still dry): DR_THROW its first hit thrown into the dub reverb and the echo
 * (lfx.c DUB VERB, ECHO THROW: the BREAK, the BASS and the CHOP; the echo without the BASS), the groove dry under it;
 * DR_LOUD the BUILD 4 dB down, so the drop comes in above it; DR_DIVE the sub drop slower (1/e in ~200 ms, not 60).
 * The user chose all three by ear; song_audio_test drops renders each way. 2026-10-29, heard on the device: the
 * gap's silence was not enough of a way into the first DROP, the tape stop before DROP 2 was: DR_TSTOP the tape stop
 * before the DROP too (the gap only for SWAP, and without it); and with the new master the BUILD needs no
 * DR_LOUD */
enum { DR_THROW = 1, DR_LOUD = 2, DR_DIVE = 4, DR_TSTOP = 8 };
static uint8_t as_drop = DR_THROW | DR_DIVE | DR_TSTOP;
#define AS_THROW_REV (1u << TRK_BRK | 1u | 4u)
#define AS_THROW_DLY (1u << TRK_BRK | 4u)
#define AS_BUILD_LV 2580u                               /* (Q12: -4 dB) */
static uint8_t as_seam = SEAM_DUB, as_sub = 1;
#define SEAM_SUB (-42)                                  /* the pads under water: P_TFLT */
#define SEAM_SUBL 24                                    /* ... and LEVEL down 24 (1/2 dB steps: -12 dB) */
#define SEAM_FB 92                                      /* the throw's feedback (G_DFDBK) */
static struct {
    uint8_t thrown;                                     /* 1: the throw's sends up; 2: its tail ringing */
    uint8_t moved;                                      /* the tracks whose FILTER / sends the seam moved (bit k) */
    uint8_t swell;                                      /* the pads' swell begun (their chord sounded again) */
    uint32_t tail;                                      /* the tail's blocks to go */
} sm;
/* FMX LIVE, the way to the next song of the set (ui_live.c asks: as_tr_ask, as_auto_next). The way
 * asked is taken on the last bar of an 8-bar phrase (or of the section): in that bar the tracks leave (BRIDGE: all but
 * the BREAK, the dub way; SWAP: the BREAKDOWN asked for the next bar; CUT: the next song on the next bar). Then the
 * bridge, the section held: TR_BARS bars (AUTO: the OUTRO's own), the tempo gliding to the next song's, the BREAK alone,
 * STRAIGHT, thinning out into a high-pass; the next song on the bar after it, at its BUILD (bringing the BREAK's
 * high-pass down from there: a V), or after SWAP at its DROP */
#define TR_BARS 8u
#define BUILD_HP0 45                                    /* a BUILD's high-pass on the BREAK: from ~680 Hz (P_TFLT) ... */
#define BUILD_HP1 28                                    /* ... down to ~200 Hz at its end (the kick's body still out) */
#define BUILD_HPP 25                                    /* ... and on the pads and the CHOP: up to ~170 Hz */
#define TR_HP BUILD_HP0                                 /* the high-pass the BREAK thins into (where a BUILD takes it on) */
#define TR_MUTE (AM_BASS | AM_CHORDS | AM_CHOP)         /* the bridge: the BREAK alone */
static struct {
    uint8_t state;                                      /* 1: the bar the tracks leave in; 2: the bridge */
    uint8_t bars, done;                                 /* the bridge's bars, those gone */
    uint8_t hold;                                       /* the clock's hold to give back (AUTO off) */
    int16_t bpm0, bpm1, flt;                            /* the tempo from, to; the BREAK's own filter */
} tr;
/* a section's card (the song's, project.c): the state of track k (BASS CHORDS CHOP BREAK) in it: 0 as the role says,
 * 1 ON, 2 OFF, 3 under water (on, low-passed and quieter) */
static uint32_t as_who(uint32_t role, uint32_t k) { return role >= AR_N ? 0u : (uint32_t)(sec_card[role].who >> (2u * k)) & 3u; }
static uint32_t as_role_mute(uint32_t role)            /* the tracks out in a section */
{
    uint32_t m, k;
    if (role >= AR_N)
        return 0u;
    m = AR[role].mute & ~(as_sub && role == AR_ROLL ? AM_CHORDS : 0u);
    for (k = 0; k < 4u; k++) {                          /* the card: ON and under water bring a track in, OFF takes it out */
        uint32_t w = as_who(role, k);
        if (w == 1u || w == 3u)
            m &= ~(1u << k);
        else if (w == 2u)
            m |= 1u << k;
    }
    return m;
}
static uint8_t as_under_on;                             /* the tracks the card has put under water now (bit k) */
static uint64_t as_bass_mask;                           /* the BASS parameters the card's preset changed (bit i = p[i]) */
static project_t song_keep __attribute__((section(".pool")));   /* the loop you made (song_backup) */
static uint8_t song_kept;
static struct {
    uint8_t role, started;
    uint16_t cycle;                                     /* ENDLESS: the round */
    step_t bass[BL_STEPS];                              /* the next BASS phrase (as_poll), for entry bass_at */
    volatile int8_t bass_at;                            /* (-1 none) */
    volatile uint32_t gen;                              /* bumped on every section (the screen) */
} as = {.bass_at = -1};


/* the BASS track's sound for a section: the card's preset n (card value n + 1) of its engine over the loop's own sound
 * (only the parameters the preset changes, remembered in as_bass_mask); 0 (or another section's none): the loop's own */
static void as_bass_apply(uint32_t card)
{
    track_t *t = &trk[0];
    const engine_t *e = ENGINES[t->eng_req % NENGINES];
    int16_t tmp[P_COUNT];
    uint32_t i;
    for (i = 0; i < P_COUNT; i++)
        if ((as_bass_mask >> i) & 1u)
            t->p[i] = song_keep.t[0].p[i];
    as_bass_mask = 0;
    if (!card || !e->npresets)
        return;
    memcpy(tmp, t->p, sizeof tmp);
    preset_fill(tmp, t->eng_req % NENGINES, (card - 1u) % e->npresets);
    for (i = 0; i < P_COUNT; i++)
        if (tmp[i] != t->p[i]) {
            as_bass_mask |= (uint64_t)1u << i;
            t->p[i] = tmp[i];
        }
    panic_req |= 1u;                                    /* (MONO / POLY may have changed: the voices released) */
}

/* ---- the sections (the audio ISR) */
/* a new section (seq.c, the arranger's clock: PLAY, a section's end, a jump) */
static void arrangement_apply(uint32_t role)
{
    if (as.started && arrangement.loop && !arrangement_clock.index) {   /* ENDLESS: the next round */
        as.cycle++;
        as_build(&arrangement, as.cycle);
        role = arrangement.entry[0].scene;
    }
    as.role = (uint8_t)(role % AR_N);
    bass_dive = as.role == AR_DROP || as.role == AR_DROP2;   /* a drop: the BASS's first note dives in (voice.c) */
    dive_k = as_drop & DR_DIVE ? 118u : 395u;
    bp.drop_kick = (uint8_t)bass_dive;                  /* ... and its first hit is a kick of the BREAK's loop (brkpart.c) */
    if (bass_dive)                                      /* ... and the BREAK at its own filter at once (not gliding down
                                                         * from the BUILD's high-pass: fx.c djf_block; out until now, or
                                                         * out of the tape stop, silent at its end) */
        memset(&tflt[TRK_BRK], 0, sizeof tflt[TRK_BRK]);
    {   /* the mutes for this block's notes already: those the last section had (the drop-out too) come off at once */
        uint32_t was = as_mute;
        as_mute = as_role_mute(as.role);
        perf_mute = (uint8_t)((perf_mute & ~was) | as_mute);
    }
    if (!as.started) {                                  /* PLAY: the tracks out silent from the first block */
        uint32_t k;
        for (k = 0; k < NTRK; k++)
            if ((as_mute >> k) & 1u)
                trk[k].att = 32767;
    }
    as.started = 1;
    {   /* the BREAK's STYLE, its filter, the CHOP's: the loop's own; then the section's */
        const int16_t *kb = song_keep.t[TRK_BRK].p;
        uint32_t i;
        for (i = 0; i < 8u; i++)
            TBRK->p[BPP[BP_CMPLX + i]] = kb[BPP[BP_CMPLX + i]];
        TBRK->p[BPP[BP_PHRASE]] = kb[BPP[BP_PHRASE]];
        TBRK->p[BPP[BP_STYLE]] = kb[BPP[BP_STYLE]];
        TBRK->p[P_SDIV] = kb[P_SDIV];
        TBRK->p[P_TFLT] = kb[P_TFLT];
        trk[CS_TRK].p[P_TFLT] = song_keep.t[CS_TRK].p[P_TFLT];
        trk[1].p[P_TFLT] = song_keep.t[1].p[P_TFLT];   /* (the pads: a BUILD's high-pass gone) */
        BPV(BP_MANGLE) = (int16_t)clamp(live_mangle >= 0 ? live_mangle : kb[BPP[BP_MANGLE]], 0, 100);   /* (LIVE's ENERGY,
                                                         * when turned, instead of the loop's) */
        if (as.role == AR_ROLL)
            brk_style_p(TBRK->p, BS_ROLLER);
        if (as.role == AR_OUTRO)
            brk_style_p(TBRK->p, BS_STRAIGHT);          /* (its second half: HALFTIME, as_block) */
        {   /* the card: the loop that plays (bank A takes loop B, C or D's: the player rebuilds it), the STYLE, the
             * BASS sound; a field at 0 is the project's */
            const sec_card_t *cd = &sec_card[as.role % AR_N];
            TBRK->p[BPP[BP_LOOPA]] = kb[BPP[BP_LOOPA]];
            TBRK->p[BPP[BP_BARSA]] = kb[BPP[BP_BARSA]];
            if (cd->loop >= 2u && cd->loop <= 4u && brkp_bank_src(cd->loop - 1u)) {
                TBRK->p[BPP[BP_LOOPA]] = kb[BPP[BP_LOOPA + cd->loop - 1u]];
                TBRK->p[BPP[BP_BARSA]] = kb[BPP[BP_BARSA + cd->loop - 1u]];
            }
            if (cd->style && cd->style <= BS_NSTYLE)
                brk_style_p(TBRK->p, cd->style - 1u);
            as_bass_apply(cd->bass);
        }
    }
    if (as.bass_at == (int8_t)arrangement_clock.index) {   /* the BASS phrase made for it */
        track_t *t = &trk[0];
        memcpy(t->step, as.bass, sizeof as.bass);
        t->p[P_SLEN] = (int16_t)BL_STEPS;
        t->p[P_SDIV] = 2;
        t->seq_active = 1;
        blr.changed = -1;
        blr.gen++;
    }
    as.bass_at = -1;
    as.gen++;
    sync_reload = 1;
    ui.force = 1;
}
/* FMX LIVE, the next song of the set (as_tr_bar asks: as_swap = its slot + 1): on the bar the arranger has just
 * crossed (seq.c), it becomes the song: its sounds, steps, tempo and generators, its chain, what the sections and STOP
 * keep (song_keep); it plays from its chain's first BUILD (after a BRIDGE) or DROP. The BREAK plays on through (its
 * voices are not cut) */
static void as_tr_clear(void)
{
    if (tr.state == 2u)
        arrangement_clock.hold = tr.hold;
    memset(&tr, 0, sizeof tr);
    as_tr_way = as_tr_to = 0;
}
static void as_swap_now(void)
{
    const project_t *p = &proj_slot[(as_swap - 1u) & 3u];
    uint32_t i, k, to = as_tr_way == TR_BRIDGE ? AR_BUILD : AR_DROP;   /* (where it comes in) */
    uint8_t slot = as_swap;
    as_swap = 0;
    as_tr_clear();
    as_tr_ask = 0;
    if (!proj_ok(p))
        return;
    for (k = 0; k < NPART; k++)
        trk_all_off(&trk[k]);
    proj_apply(p, 1);
    as_under_on = 0;
    as_bass_mask = 0;
    song.sel = (uint8_t)(p->sel < NTRK ? p->sel : 0u);
    song_keep = *p;
    song_kept = 1;
    live_mangle = -1;
    as.cycle = 0;
    as_build(&arrangement, 0);                          /* (its own chain: its FORM, VARIANT, MODE and cards) */
    for (i = 0; i < arrangement.count && arrangement.entry[i].scene != to; i++)
        ;
    for (k = 0; i >= arrangement.count && k < arrangement.count; k++)
        if (arrangement.entry[k].scene == AR_DROP)
            i = k;
    if (i >= arrangement.count)
        i = 0;
    arrangement_clock.index = (uint8_t)i;
    arrangement_clock.bar = 0;
    arrangement_clock.jump = -1;
    arrangement_apply(arrangement.entry[i].scene);
    seq_reset_tracks(arrangement_clock.phase);
    as_swapped_to = slot;
    as_swapped++;
}
/* LIVE: AUTO on / off (SEQ, PLAY): the clock's hold; a bridge holds the section itself, and gives this back after it */
static void as_hold(uint32_t on)
{
    if (tr.state == 2u)
        tr.hold = (uint8_t)(on != 0u);
    else
        arrangement_clock.hold = (uint8_t)(on != 0u);
}
/* the entry of a section: the next one of it after the playing; none: the next entry becomes it */
static uint32_t as_entry_of(uint32_t role)
{
    uint32_t n = arrangement.count, cur = arrangement_clock.index, i;
    for (i = 1; i <= n; i++)
        if (arrangement.entry[(cur + i) % n].scene == role)
            return (cur + i) % n;
    arrangement.entry[(cur + 1u) % n].scene = (uint8_t)role;
    return (cur + 1u) % n;
}
static void as_tr_begin(uint32_t bars)                 /* the bridge's first bar */
{
    const project_t *p = &proj_slot[(as_tr_to - 1u) & 3u];
    tr.state = 2;
    tr.bars = (uint8_t)(bars ? bars : 1u);
    tr.done = 0;
    tr.hold = arrangement_clock.hold;
    arrangement_clock.hold = 1;                         /* (the section plays on: the old song's chain goes nowhere) */
    tr.bpm0 = song.g[G_BPM];
    tr.bpm1 = (int16_t)clamp(p->g[G_BPM], GP[G_BPM].min, GP[G_BPM].max);
    tr.flt = song_keep.t[TRK_BRK].p[P_TFLT];
    if (as_tr_way == TR_BRIDGE)
        brk_style_p(TBRK->p, BS_STRAIGHT);
    if (tr.bars == 1u)
        as_swap = as_tr_to;
}
/* every bar crossed while playing (seq.c, after the arranger's clock and a swap): the way asked taken on the phrase,
 * the bridge's bars counted, the next song asked for the bar after its last */
static void as_tr_bar(void)
{
    const arr_entry_t *e = &arrangement.entry[arrangement_clock.index % ARR_STEPS];
    uint32_t b = arrangement_clock.bar, w = as_tr_ask, to = as_tr_slot;
    if (tr.state == 2u) {
        if (++tr.done + 1u >= tr.bars)
            as_swap = as_tr_to;                         /* (its last bar begun: the next song on the next) */
        return;
    }
    if (tr.state == 1u) {                               /* the tracks have left: the bridge (CUT: swapped already) */
        if (as_tr_way != TR_CUT)
            as_tr_begin(TR_BARS);
        return;
    }
    if (as.role == AR_OUTRO && !b && as_auto_next && !arrangement_clock.hold &&
        proj_ok(&proj_slot[(as_auto_next - 1u) & 3u])) {   /* AUTO, a next song: the OUTRO is the bridge */
        as_tr_ask = 0;
        as_tr_way = TR_BRIDGE;
        as_tr_to = as_auto_next;
        as_tr_begin(e->bars);
        return;
    }
    if (!w || w >= TR_N || !to || ((b + 1u) % 8u && b + 1u < e->bars))
        return;                                         /* (none asked, or not the phrase's last bar) */
    as_tr_ask = 0;
    if (!proj_ok(&proj_slot[(to - 1u) & 3u]))
        return;
    as_tr_way = (uint8_t)w;
    as_tr_to = (uint8_t)to;
    tr.state = 1;
    if (w == TR_CUT)
        as_swap = (uint8_t)to;
    else if (w == TR_SWAP)
        arrangement_clock.jump = (int8_t)as_entry_of(AR_BREAKDOWN);
}
/* SEAM_DUB, every block (as_block): the bar before the next section (the chain's, or a jump asked for), its throws
 * and swells; after it, the sends back and the echo's tail dying out */
static uint32_t as_seams(uint32_t r, const arr_entry_t *e, uint32_t nx, uint32_t mute)
{
    uint32_t nxr = AR_N, cur = as_role_mute(r), nm, leave, enter, pb, k, br = tr.state == 2u && as_tr_way == TR_BRIDGE, ahead;
    const int16_t *g0 = song_keep.g;
    if (br)
        cur = TR_MUTE;                                  /* (the bridge: the BREAK alone) */
    if (arrangement_clock.jump >= 0 && arrangement_clock.jump < (int)arrangement.count)
        nxr = arrangement.entry[arrangement_clock.jump].scene % AR_N;
    else if (!arrangement_clock.hold && arrangement_clock.bar + 1u >= e->bars)
        nxr = nx;
    ahead = (tr.state == 1u && as_tr_way == TR_BRIDGE) ||   /* (a bridge from the next bar: the tracks leave for it) */
            (!tr.state && nxr == AR_OUTRO && as_auto_next && !arrangement_clock.hold);
    pb = (clk_beat & 3u) * 1024u + clk_pos / (BEAT_U / 1024u);   /* (into the bar, 0..4095) */
    pb = pb > 4095u ? 4095u : pb;
    if ((nxr < AR_N && nxr != r && !br) || ahead) {
        nm = ahead ? TR_MUTE : as_role_mute(nxr);
        leave = ~cur & nm & 15u;
        enter = cur & ~nm & 15u;
        if ((leave & (AM_BREAK | AM_CHOP)) && pb >= 3072u) {   /* the last beat: thrown into the echo */
            song.g[G_DTIME] = 6;                        /* (1/8D) */
            song.g[G_DFDBK] = SEAM_FB;
            song.g[G_DCOLOR] = 40;
            song.g[G_DMIX] = 110;
            if (leave & AM_BREAK)
                TBRK->p[P_DLY] = 110;
            if (leave & AM_CHOP)
                trk[CS_TRK].p[P_DLY] = 90;
            sm.moved |= leave & (AM_BREAK | AM_CHOP);
            sm.thrown = 1;
        }
        if ((leave & AM_BASS) && pb >= 2048u) {         /* the BASS: closing over the last 2 beats */
            int32_t b = song_keep.t[0].p[P_TFLT];
            trk[0].p[P_TFLT] = (int16_t)(b + (((-50 - b) * (int32_t)(pb - 2048u)) >> 11));
            sm.moved |= AM_BASS;
        }
        if ((leave & AM_CHORDS) && pb >= 3072u) {       /* the pads: thrown into the reverb */
            trk[1].p[P_REV] = 127;
            sm.moved |= AM_CHORDS;
        }
        if (enter & AM_CHORDS) {                        /* the pads come in: up through a low-pass over the bar */
            int32_t b = song_keep.t[1].p[P_TFLT], s2 = (int32_t)(pb * pb >> 12);
            mute &= ~AM_CHORDS;
            perf_mute &= (uint8_t)~AM_CHORDS;           /* (this block's notes already) */
            if (!sm.swell) {                            /* the chord held (dropped while out): sounded now */
                track_t *t = &trk[1];
                if (cg.play == CP_HOLD && cgr.now >= 0)
                    cg_sound((uint32_t)cgr.now);        /* (the CHORDS generator's) */
                else
                    for (k = 0; k < t->seq_n; k++)
                        trk_note_on(t, t->seq_notes[k], 100);   /* (the track's own steps) */
                sm.swell = 1;
            }
            int32_t lv = song_keep.t[1].p[P_LEVEL], l0 = lv > 60 ? lv - 60 : 0;   /* (and from 30 dB under its LEVEL) */
            trk[1].p[P_TFLT] = (int16_t)(-60 + (((b + 60) * s2) >> 12));
            trk[1].p[P_LEVEL] = (int16_t)(l0 + (((lv - l0) * (int32_t)pb) >> 12));
            sm.moved |= AM_CHORDS;
        } else if (as_sub && !(nm & AM_CHORDS) && !(cur & AM_CHORDS) && (r == AR_ROLL) != (nxr == AR_ROLL)) {
            int32_t b = song_keep.t[1].p[P_TFLT], s2 = (int32_t)(pb * pb >> 12);   /* into the water, or out of it */
            int32_t a = r == AR_ROLL ? SEAM_SUB : b, z = r == AR_ROLL ? b : SEAM_SUB;
            int32_t lv = song_keep.t[1].p[P_LEVEL], lw = lv > SEAM_SUBL ? lv - SEAM_SUBL : 0;
            int32_t la = r == AR_ROLL ? lw : lv, lz = r == AR_ROLL ? lv : lw;
            trk[1].p[P_TFLT] = (int16_t)(a + (((z - a) * s2) >> 12));
            trk[1].p[P_LEVEL] = (int16_t)(la + (((lz - la) * (int32_t)pb) >> 12));
            sm.moved |= AM_CHORDS;
        }
        return mute;
    }
    if (sm.moved) {                                     /* the section has begun: the moved tracks back to their own */
        for (k = 0; k < NPART; k++)
            if ((sm.moved >> k) & 1u) {
                trk[k].p[P_TFLT] = song_keep.t[k].p[P_TFLT];
                trk[k].p[P_DLY] = song_keep.t[k].p[P_DLY];
                trk[k].p[P_REV] = song_keep.t[k].p[P_REV];
                trk[k].p[P_LEVEL] = song_keep.t[k].p[P_LEVEL];
            }
        if (sm.moved & AM_BREAK)
            TBRK->p[P_DLY] = song_keep.t[TRK_BRK].p[P_DLY];
        sm.moved = 0;
        sm.swell = 0;
    }
    for (k = 0; k < 4u; k++) {                          /* the card's under water: the track low-passed and 12 dB down, and back */
        if (as_who(r, k) == 3u && !br) {
            int32_t lv = song_keep.t[k].p[P_LEVEL];
            trk[k].p[P_TFLT] = SEAM_SUB;
            trk[k].p[P_LEVEL] = (int16_t)(lv > SEAM_SUBL ? lv - SEAM_SUBL : 0);
            as_under_on |= (uint8_t)(1u << k);
        } else if ((as_under_on >> k) & 1u) {
            trk[k].p[P_TFLT] = song_keep.t[k].p[P_TFLT];
            trk[k].p[P_LEVEL] = song_keep.t[k].p[P_LEVEL];
            as_under_on &= (uint8_t)~(1u << k);
        }
    }
    if (as_sub && r == AR_ROLL && !br) {                /* (under water all through the ROLL) */
        int32_t lv = song_keep.t[1].p[P_LEVEL];
        trk[1].p[P_TFLT] = SEAM_SUB;
        trk[1].p[P_LEVEL] = (int16_t)(lv > SEAM_SUBL ? lv - SEAM_SUBL : 0);
    }
    if (sm.thrown == 1) {                               /* the echo rings on 2 bars, dying away */
        sm.thrown = 2;
        sm.tail = 8u * BEAT_U / ((uint32_t)song.g[G_BPM] * CTL);
    }
    if (sm.thrown == 2) {
        uint32_t all = 8u * BEAT_U / ((uint32_t)song.g[G_BPM] * CTL);
        if (sm.tail) {
            sm.tail--;
            song.g[G_DFDBK] = (int16_t)(g0[G_DFDBK] + (SEAM_FB - g0[G_DFDBK]) * (int32_t)sm.tail / (int32_t)(all ? all : 1u));
        } else {
            song.g[G_DTIME] = g0[G_DTIME];
            song.g[G_DFDBK] = g0[G_DFDBK];
            song.g[G_DCOLOR] = g0[G_DCOLOR];
            song.g[G_DMIX] = g0[G_DMIX];
            sm.thrown = 0;
        }
    }
    return mute;
}
/* every block while playing (seq.c): the section's sweeps (Q12: how far into it), its moves, the gap before a drop,
 * the fade */
static void as_block(void)
{
    const arr_entry_t *e;
    uint32_t keys, mute, beat, nx, i, p, r, br, gap = 0, steep = 0, left, lead, gq = 0, lv;
    if (!arrangement_clock.running || !song.playing) {
        as_keys = 0;
        as_mute = 0;
        as_fade = 4096;
        fx_hush = 0;
        as_throw = 0;
        for (i = 0; i < NTRK; i++)
            tflt[i].steep = 0;
        return;
    }
    i = arrangement_clock.index % ARR_STEPS;
    e = &arrangement.entry[i];
    r = as.role;
    beat = clk_beat & 3u;
    nx = i + 1u < arrangement.count ? arrangement.entry[i + 1u].scene : arrangement.loop ? arrangement.entry[0].scene : AR_N;
    p = (clk_beat * 256u + clk_pos / (BEAT_U / 256u)) * 4u / (e->bars ? e->bars : 1u);   /* (beats Q8 over bars x 4 beats) */
    p = p > 4096u ? 4096u : p;
    br = tr.state == 2u && as_tr_way == TR_BRIDGE;
    keys = br ? 0u : AR[r].hold;
    mute = br ? TR_MUTE : as_role_mute(r);              /* (the bridge: the BREAK alone, whatever the section says) */
    left = (e->bars > arrangement_clock.bar ? e->bars - arrangement_clock.bar : 1u) * 4u - beat;   /* beats to the section's
                                                         * end, this one counted */
    if (arrangement_clock.hold)
        left = 99u;                                     /* (LIVE, AUTO off: the section plays on, no lead ...) */
    if (!tr.state && arrangement_clock.jump >= 0 && arrangement_clock.jump == as_rw_to) {
        left = 99u;                                     /* (... nor into the one REWIND resolves into: the rewind is its
                                                         * way in) */
    } else if (!tr.state && arrangement_clock.jump >= 0 && arrangement_clock.jump < (int)arrangement.count &&
               arrangement_clock.jump != (int)i) {      /* (... but into a section asked for: it lands on the next bar) */
        nx = arrangement.entry[arrangement_clock.jump].scene;
        left = 4u - beat;
    } else if (!tr.state && as_pend >= 0 && as_pend < AR_N && arrangement_clock.jump < 0) {   /* (asked, not yet in:
                                                         * it lands after the phrase's last bar, ui_live.c live_poll_section) */
        uint32_t b = arrangement_clock.bar, k = 0, q = q_mode == QM_4BAR ? 4u : 1u;
        while ((b + 1u) % q && k < 16u) {               /* (the bars to it; held, the section wraps round) */
            b = b + 1u >= e->bars ? 0u : b + 1u;
            k++;
        }
        if (arrangement_clock.hold || arrangement_clock.bar + k < e->bars) {
            nx = (uint32_t)as_pend;
            left = k * 4u + 4u - beat;
        }
    }
    lead = nx == AR_DROP ? (as_drop & DR_TSTOP ? 1u : as_gap) : nx == AR_DROP2 ? 1u : 0u;   /* the beats of the way in:
                                                         * the tape stop (or the gap) */
    if (tr.state == 2u && as_tr_way == TR_SWAP) {       /* SWAP: the BREAKDOWN's lead into the next song's DROP */
        left = (tr.bars > tr.done ? tr.bars - tr.done : 1u) * 4u - beat;
        lead = as_gap;
    } else if (br) {
        left = 99u;
    }
    if (left > lead && left - lead <= 4u)               /* the moves of the last bar, the last 2 beats: before the way in */
        keys |= AR[r].last | (left - lead <= 2u ? AR[r].half : 0u);
    if (left <= lead) {
        gap = (nx == AR_DROP2 || (nx == AR_DROP && (as_drop & DR_TSTOP))) && !(tr.state == 2u) ? 2u : 1u;
        gq = ((lead - left) * BEAT_U + clk_pos) / (lead * (BEAT_U / 4096u));   /* (how far into it, Q12) */
        gq = gq > 4096u ? 4096u : gq;
    }
    fx_hush = 0;
    if (gap == 1u && as_gap_tail && gq < 3072u) {       /* GAP TAIL: the tracks out on its 1, the WASH's echo ringing
                                                         * on and fading over its first 3 quarters; then as below */
        keys = 1u << PA_WASH;
        mute |= 15u;
    } else if (gap == 1u) {                             /* before a DROP: silence, the tails emptied under it */
        keys = 0;
        mute |= AM_BREAK | AM_BASS;
        fx_hush = 1;
    } else if (gap == 2u) {                             /* before a DROP (DR_TSTOP) and a DROP 2: the tape stop; it plays the beat's first half
                                                         * slowed over the whole beat, so the tails go in its second */
        keys = 1u << PA_TSTOP;
        fx_hush = clk_pos >= BEAT_U / 2u;
    }
    if (r == AR_INTRO && !br) {                         /* the CHOP out of the low-pass: FILT -56 to the loop's own, late */
        int32_t base = song_keep.t[CS_TRK].p[P_TFLT], s2 = (int32_t)(p * p >> 12);
        trk[CS_TRK].p[P_TFLT] = (int16_t)(-56 + (((base + 56) * s2) >> 12));
    }
    if (r == AR_BUILD && !br) {                         /* no low end until the drop: the BREAK through the high-pass
                                                         * coming down, late (BUILD_HP0 to BUILD_HP1, the loop's own when
                                                         * higher; its own at the drop); the pads and the CHOP through one
                                                         * going up, early (to BUILD_HPP), their own again in the gap */
        int32_t base = song_keep.t[TRK_BRK].p[P_TFLT], s2 = (int32_t)(p * p >> 12), s1 = (int32_t)(p * (8192u - p) >> 12);
        int32_t z = base > BUILD_HP1 ? base : BUILD_HP1, a = z > BUILD_HP0 ? z : BUILD_HP0;
        uint32_t k;
        steep = 1u << TRK_BRK | 1u << 1 | 1u << CS_TRK;
        TBRK->p[P_TFLT] = (int16_t)(a + (((z - a) * s2) >> 12));
        for (k = 1; k <= CS_TRK; k++) {
            int32_t own = song_keep.t[k].p[P_TFLT], hp = own > BUILD_HPP ? own : BUILD_HPP;
            trk[k].p[P_TFLT] = (int16_t)(gap == 1u ? own : own + (((hp - own) * s1) >> 12));
        }
    }
    lv = (as_drop & DR_LOUD) && r == AR_BUILD && !br ? AS_BUILD_LV : 4096u;   /* (the mix's level: the BUILD under the drop) */
    if (gap == 1u && as_gap_tail && gq < 3072u) {       /* the echo fading: equal power, to silence at 3/4 */
        uint32_t lin = 4096u - gq * 4u / 3u;
        as_fade = (uint16_t)((lin * lin >> 12) * lv >> 12);
    } else if (gap == 1u) {                             /* the cut: the mix down over 4 blocks (~3 ms: no click) */
        as_fade = as_fade > 1024u ? (uint16_t)(as_fade - 1024u) : 0u;
    } else if (r == AR_OUTRO && p > 2048u && !tr.state) {   /* its second half: the BREAK slowing down (HALFTIME), the
                                                         * fade, equal power to silence (the set's last song: no bridge) */
        uint32_t lin = 4096u - (p - 2048u) * 2u;
        if ((uint32_t)TBRK->p[BPP[BP_STYLE]] != BS_HALFTIME)
            brk_style_p(TBRK->p, BS_HALFTIME);
        as_fade = (uint16_t)(lin * lin >> 12);
    } else {                                            /* (down over ~14 ms; up at once: the drop's first hit whole) */
        as_fade = (uint16_t)(as_fade > lv + 64u ? as_fade - 64u : lv);
    }
    as_throw = (as_drop & DR_THROW) && (r == AR_DROP || r == AR_DROP2) && !br && !clk_beat && clk_pos < BEAT_U / 2u
                   ? (uint8_t)(AS_THROW_REV | AS_THROW_DLY << 4) : 0u;   /* (the drop's first 1/8: its first hit thrown) */
    if (tr.state == 2u) {                               /* the bridge: how far (Q12); the tempo on its way, 1 BPM at a time */
        uint32_t q = arrangement_clock.phase / (FS * 240u / 4096u);
        int32_t d = tr.bpm1 - tr.bpm0;
        q = ((uint32_t)tr.done * 4096u + (q > 4095u ? 4095u : q)) / tr.bars;
        q = q > 4096u ? 4096u : q;
        song.g[G_BPM] = (int16_t)(tr.bpm0 + (d * (int32_t)q + (d < 0 ? -2048 : 2048)) / 4096);
        p = q;
    }
    if (as_seam == SEAM_DUB)
        mute = as_seams(r, e, nx, mute);
    if (br) {                                           /* the BREAK STRAIGHT, thinning into the high-pass a BUILD takes on */
        int32_t s2 = (int32_t)(p * p >> 12);
        if ((uint32_t)TBRK->p[BPP[BP_STYLE]] != BS_STRAIGHT)
            brk_style_p(TBRK->p, BS_STRAIGHT);
        TBRK->p[P_TFLT] = (int16_t)(tr.flt + (((TR_HP - tr.flt) * s2) >> 12));
        steep = 1u << TRK_BRK;
    }
    for (i = 0; i < NTRK; i++)                          /* (the BUILD's and the bridge's high-passes: the steep one) */
        tflt[i].steep = (uint8_t)((steep >> i) & 1u);
    perf_mute &= (uint8_t)~(as_mute & ~mute);           /* (back in: this block's notes already, not a block late) */
    as_mute = (uint8_t)mute;
    as_keys = (uint16_t)keys;
}

/* ---- the main loop: the next DROP 2's BASS phrase, made in the bar before it */
static void as_poll(void)
{
    uint32_t i, nx;
    if (!song.playing || !arrangement_clock.running || as.bass_at >= 0 || !bl_on())
        return;
    i = arrangement_clock.index;
    nx = i + 1u < arrangement.count ? i + 1u : 0u;
    if (arrangement_clock.bar + 1u < arrangement.entry[i % ARR_STEPS].bars || arrangement.entry[nx].scene != AR_DROP2 ||
        (!nx && !arrangement.loop))
        return;
    bl.var = (uint16_t)((bl.var + 1u) % 1000u);
    bl_build(as.bass);
    as.bass_at = (int8_t)nx;
}

/* ---- song mode keeps the loop you made: PLAY in song mode puts it aside (each section then plays
 * over the tracks), STOP (or the song's end) brings it back */
static void song_backup(void)                  /* (audio ISR: seq_start) */
{
    proj_capture(&song_keep);
    song_kept = 1;
    as_under_on = 0;
    as_bass_mask = 0;
    memset(&sm, 0, sizeof sm);
    as_tr_clear();
    as_swap = 0;
    as.started = 0;
    as.cycle = 0;
    as.bass_at = -1;
}
/* a change of yours while it plays (q.c, the BREAK screen): the loop put aside has it too, so the next section (and
 * STOP) keeps it instead of going back to what it was at PLAY (the BREAK's loop picked in STUDIO, 2026-10-25) */
static void song_keep_p(uint32_t k, uint32_t id, int32_t v)
{
    if (song_kept && k < NTRK && id < P_COUNT)
        song_keep.t[k].p[id] = (int16_t)v;
}
/* ... a knob turned by d: by d there too (the sections drive some (a BUILD's high-pass, a throw's sends): what is
 * shown is theirs, the turn is yours) */
static void song_keep_turn(uint32_t k, uint32_t id, int32_t d, int32_t lo, int32_t hi)
{
    if (song_kept && k < NTRK && id < P_COUNT)
        song_keep.t[k].p[id] = (int16_t)clamp(song_keep.t[k].p[id] + d, lo, hi);
}
/* ... a sound picked (PRESETS, Q's on the bar): its engine and preset kept too */
static void song_keep_eng(uint32_t k, uint32_t e, uint32_t pre)
{
    if (song_kept && k < NTRK) {
        song_keep.t[k].engine = (uint8_t)e;
        song_keep.t[k].preset = (uint8_t)pre;
    }
}
/* ... a track's whole sound (and with steps: its steps) changed by you while the song plays: a preset, the CHOP screen
 * (its MODE, kit, phrase). The parameters the sections drive (filter, level, sends, mute) stay as they were put aside */
static void song_keep_trk(uint32_t k, int steps)
{
    uint32_t i;
    if (!song_kept || k >= NPART)
        return;
    for (i = 0; i < P_COUNT; i++)
        if (i != P_TFLT && i != P_LEVEL && i != P_DLY && i != P_REV && i != P_MUTE)
            song_keep.t[k].p[i] = trk[k].p[i];
    song_keep.t[k].engine = trk[k].eng_req;
    song_keep.t[k].preset = trk[k].preset;
    if (!k)
        as_bass_mask = 0;                               /* (a BASS sound of yours: no card's preset to take back) */
    if (steps) {
        memcpy(song_keep.t[k].step, trk[k].step, sizeof trk[k].step);
        memcpy(song_keep.t[k].micro, trk[k].micro, sizeof trk[k].micro);
    }
}
static void song_restore(void)                 /* (audio ISR: seq_stop) */
{
    uint32_t i;
    as_keys = 0;
    as_mute = 0;
    as_fade = 4096;
    fx_hush = 0;
    bass_dive = 0;
    as_throw = 0;
    as_tr_clear();                                      /* (a bridge under way: gone, the hold given back) */
    as_swap = 0;
    if (!song_kept)
        return;
    song_kept = 0;
    for (i = 0; i < NTRK; i++)
        trk_all_off(&trk[i]);
    proj_apply(&song_keep, 2);                          /* (2: the song, its cards, as they are now) */
    song.sel = (uint8_t)(song_keep.sel < NTRK ? song_keep.sel : 0u);
    sync_reload = 1;
    ui.force = 1;
}
