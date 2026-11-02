/* SPDX-License-Identifier: GPL-3.0-only */
/* AUTO SONG (firmware/src/arranger_scene.c) with the real sequencer, engines and mixer, on a session as a user makes
 * one: the BREAK (AMEN), the BASS phrase generator (REESE SUB), the CHORDS generator (PSX PAD), the CHOP track in
 * SLICE (RAVE HITS, a hit a beat; or a slot the loop page cut). At 170 BPM, FORM SHORT (32 bars):
 *   the sections   each on its bar (to the block); INTRO without the BASS and the BREAK, DROP everything, BREAKDOWN
 *                  without the BREAK and the BASS; DROP 2 a new BASS phrase on its first block, the BREAK's MANGLE the loop's own
 *   the end        the last beat a TAPE STOP; the song stops after its 32 bars, the loop as it was (the BASS phrase,
 *                  MANGLE), nothing held, no moves or mutes left
 *   ENDLESS        no OUTRO: after the second drop a new round (BUILD first), still playing
 *   a jump         a section asked for starts on the next bar
 *   the next song  LIVE's ways to it (BRIDGE, SWAP, CUT; AUTO's OUTRO as the bridge): where it comes in, the BREAK
 *                  carrying the bridge, the tempo gliding, the filter's V, key 8's last choice, MAN, STOP, ENDLESS
 *   song_audio_test [song.wav [slot.bin [FORM [VARIANT]]]]: the test song as a WAV; with a slot (web/slice.js's
 *   image: the CHOP track's loop), FORM (0 SHORT, 1 CLUB, 2 LONG) and VARIANT: the whole song rendered instead. */
#define FELUCCA_ARRANGER 1
#include <stdint.h>
#define main hostsim_main
#include "hostsim.c"
#undef main
#define PROJ_HOST 1
static uint32_t trk_def_engine(uint32_t i) { return i < NPART ? i : 0; }
#include "../firmware/src/project.c"
static struct { uint8_t force; } ui;
static uint8_t sync_reload;
#include "../firmware/src/arranger_scene.c"

static int fails;
static void check(int ok, const char *what) { printf("song: %-88s %s\n", what, ok ? "ok" : "FAIL"); fails += !ok; }
static int32_t out[CTL * 2];

/* the session: 170 BPM, F minor; the CHOP track's loop from a slot image (or RAVE HITS) */
static void session(const char *slot)
{
    host_tracks_init();
    song.g[G_BPM] = 170;
    host_preset(&trk[0], ENGI_ANALOG, 22);            /* REESE SUB */
    host_preset(&trk[1], ENGI_SAMPLE, 0);             /* PSX PAD */
    host_preset(&trk[2], ENGI_SAMPLE, 8);             /* CHOP KIT: USR1, the RAVE HITS pack */
    trk[0].p[P_ROOT] = trk[1].p[P_ROOT] = 5;          /* F minor */
    trk[0].p[P_SCALE] = trk[1].p[P_SCALE] = 2;
    cg_defaults();
    cg.play = CP_HOLD;
    cg_poll();
    bl_defaults();
    bl_write();
    trk[2].p[P_X0] = CM_PADS;                         /* (a loop given: SLICE) */
    if (slot) {
        FILE *f = fopen(slot, "rb");
        size_t n = f ? fread(host_slot(0), 1, SMP_USER_SIZE, f) : 0;
        if (f)
            fclose(f);
        smp_pool_scan();
        if (n > SMP_USER_DATA && usr_nz[0]) {
            trk[2].p[P_E0] = (int16_t)SMP_NSETS;      /* USR1: the loop */
            trk[2].p[P_X0] = CM_SLICE;
            trk[2].p[P_VOICE] = V_MONO;
            trk[2].p[P_REL] = 120;
            trk[2].p[P_X0 + 5] = CT_KEY;
            trk[1].p[P_LEVEL] = (int16_t)(trk[1].p[P_LEVEL] * 2 / 3);   /* (the pad under the loop) */
            cg.num = 2;                               /* (the loop carries the harmony: the pad a slow two) */
            cg_poll();
        } else {
            printf("song: %s: not a slot image, RAVE HITS instead\n", slot);
        }
    }
    if (!usr_nz[0])
        host_pack(0, "RAVEHITS");                     /* CHOP KIT: the RAVE HITS pack in USR1, as the editor puts it */
    brkp_poll();
    if (trk[2].p[P_X0] == CM_PADS) {                  /* PADS: a few hits off the snares, as a player records them */
        static const uint8_t AT[4] = {0, 6, 13, 22};
        uint32_t base, nh, i;
        memset(trk[2].step, 0, sizeof trk[2].step);
        trk[2].p[P_SLEN] = 32;
        if (smp_kit((uint32_t)trk[2].p[P_E0] % SMP_NALL, &base, &nh))
            for (i = 0; i < 4u; i++) {
                step_t *st = &trk[2].step[AT[i]];
                st->time = ST_NOTE;
                st->n = 1;
                st->note[0] = (uint8_t)(base + i * 2u % nh);
                st->vel = 100;
            }
        trk[2].seq_active = 1;
    }
}
static uint32_t sounding(void)                       /* bit k: track k has a voice on, and is not faded out (a mute) */
{
    uint32_t i, k, m = 0;
    for (i = 0; i < NPART; i++)
        for (k = 0; k < NVOICE; k++)
            if (trk[i].v[k].active && trk[i].v[k].gate && trk[i].att < 32767)
                m |= 1u << i;
    if ((bp.v[0].on || bp.v[1].on) && TBRK->att < 32767)
        m |= 8u;
    return m;
}
static void block(FILE *f)
{
    uint32_t k;
    brkp_poll();
    cs_poll();
    as_poll();
    mix_block(out, CTL);
    if (f)
        for (k = 0; k < CTL; k++)
            wav_put(f, out[2 * k], out[2 * k + 1]);
}

/* the whole song (a slot given): FORM, VARIANT, into a WAV with two seconds of tail */
static int render(const char *wav, const char *slot, uint32_t form, uint32_t var)
{
    FILE *f;
    uint32_t total, frames, t, i, bar = 4u * 60u * FS / 170u;
    session(slot);
    arrangement_enabled = 1;
    as_set(form, var, 0);
    total = as_total(&arrangement);
    frames = ((total * bar + 2u * FS) / CTL + 1u) * CTL;
    f = fopen(wav, "wb");
    if (!f)
        return 1;
    wav_hdr(f, frames);
    printf("song: FORM %s, VARIANT %u: %u bars,", N_AS_FORM[form % 3u], var, total);
    for (i = 0; i < arrangement.count; i++)
        printf(" %s %u", AR_NAME[arrangement.entry[i].scene % AR_N], arrangement.entry[i].bars);
    printf("\n");
    transport_req = 1;
    for (t = 0; t < frames; t += CTL)
        block(f);
    fclose(f);
    printf("song: %s (%u s)\n", wav, frames / FS);
    return song.playing;                              /* (the song stopped by itself) */
}

/* the seams (arranger_scene.c): ROLL 4 bars into BREAKDOWN 4, from the ROLL's 2nd bar to the BREAKDOWN's 3rd, into
 * a WAV: seam SEAM_CLEAN / SEAM_DUB, as_sub the pads under water in the ROLL */
static void seam_render(const char *path, uint32_t seam, uint32_t sub)
{
    uint32_t bar = 4u * 60u * FS / 170u, t, n = 6u * bar;
    FILE *f = fopen(path, "wb");
    if (!f)
        return;
    session(0);
    as_seam = (uint8_t)seam;
    as_sub = (uint8_t)sub;
    if (getenv("SEAM_SOLO")) {                        /* (a check: the BREAK alone, or the pads) */
        uint32_t k, keep = *getenv("SEAM_SOLO") == 'p' ? 1u : TRK_BRK;
        for (k = 0; k < NTRK; k++)
            trk[k].p[P_MUTE] = (int16_t)(k != keep);
    }
    arrangement_enabled = 1;
    as_set(2, 0, 0);
    transport_req = 1;
    block(0);
    arrangement.count = 3;
    arrangement.entry[0] = (arr_entry_t){AR_ROLL, 4};
    arrangement.entry[1] = (arr_entry_t){AR_BREAKDOWN, 4};
    arrangement.entry[2] = (arr_entry_t){AR_DROP, 4};
    arrangement_clock.index = 0;
    arrangement_clock.bar = 0;
    arrangement_clock.phase = 0;
    arrangement_clock.jump = -1;
    arrangement_apply(AR_ROLL);
    seq_reset_tracks(0);
    for (t = 0; t < bar; t += CTL)                    /* (the ROLL's first bar: the echo and reverb settled) */
        block(0);
    wav_hdr(f, (n / CTL) * CTL);
    for (t = 0; t + CTL <= n; t += CTL)
        block(f);
    fclose(f);
    transport_req = 2;
    block(0);
    block(0);
    printf("seams: %s (%s%s)\n", path, seam ? "DUB" : "CLEAN", sub ? ", the pads under water" : "");
}

/* the way into the DROP (arranger_scene.c as_gap, as_gap_tail): the SHORT song from its INTRO's last half bar to the
 * DROP's 2nd bar (BUILD at bar 4, DROP at 8), into a WAV */
static void gap_render(const char *path, uint32_t beats, uint32_t tail)
{
    uint32_t bar = 4u * 60u * FS / 170u, t, a = bar * 7u / 2u / CTL * CTL, z = 10u * bar / CTL * CTL;
    FILE *f = fopen(path, "wb");
    if (!f)
        return;
    session(0);
    as_drop &= (uint8_t)~DR_TSTOP;                  /* (the gap, not the tape stop) */
    as_gap = (uint8_t)beats;
    as_gap_tail = (uint8_t)tail;
    arrangement_enabled = 1;
    as_set(0, 0, 0);
    transport_req = 1;
    wav_hdr(f, z - a);
    for (t = 0; t + CTL <= z; t += CTL)
        block(t >= a ? f : 0);
    fclose(f);
    transport_req = 2;
    block(0);
    block(0);
    as_gap = 1;
    as_gap_tail = 0;
    as_drop |= DR_TSTOP;
    printf("gaps: %s (%u beat%s, %s)\n", path, beats, beats > 1u ? "s" : "", tail ? "the echo fading into it" : "cut");
}

/* the drop itself (arranger_scene.c as_drop): the SHORT song from its INTRO's last half bar to the DROP's 4th bar,
 * into a WAV; one a process (the echo's and the generators' state carry over) */
static void drop_render(const char *path, uint32_t drop, int style)
{
    uint8_t was = as_drop;
    uint32_t bar = 4u * 60u * FS / 170u, t, a = bar * 7u / 2u / CTL * CTL, z = 12u * bar / CTL * CTL;
    FILE *f = fopen(path, "wb");
    if (!f)
        return;
    session(0);
    song.master_q12 = 2048;                           /* (MASTER where the device starts: at full the limiter flattens
                                                       * the BUILD and the DROP to one level) */
    as_drop = (uint8_t)drop;
    if (style >= 0)                                   /* (the BREAK's STYLE: the loop's own in every section) */
        brk_style_p(TBRK->p, (uint32_t)style % BS_NSTYLE);
    arrangement_enabled = 1;
    as_set(0, 0, 0);
    transport_req = 1;
    wav_hdr(f, z - a);
    for (t = 0; t + CTL <= z; t += CTL)
        block(t >= a ? f : 0);
    fclose(f);
    transport_req = 2;
    block(0);
    block(0);
    as_drop = was;
    printf("drops: %s (%s%s%s)\n", path, drop & DR_THROW ? "the first hit thrown " : "", drop & DR_LOUD ? "the BUILD -4 dB " : "",
           drop & DR_DIVE ? "the slow sub drop" : "");
}

/* a drop's first hit (brkpart.c drop_kick): in the SHORT song, whatever the BREAK's STYLE, the first slice the BREAK
 * plays in the DROP and in the DROP 2 is one that opens on a kick of its loop */
static void drop_kicks(void)
{
    static const uint8_t ST[3] = {BS_JUNGLE, BS_CHAOS, BS_STRAIGHT};
    uint32_t s, t, bar = 4u * 60u * FS / 170u;
    for (s = 0; s < 3u; s++) {
        uint32_t role = AR_N, seen = 0, kick = 0, starts = 0;
        char what[96];
        session(0);
        brk_style_p(TBRK->p, ST[s]);
        arrangement_enabled = 1;
        as_set(0, 0, 0);
        transport_req = 1;
        for (t = 0; t < 22u * bar; t += CTL) {
            block(0);
            if (as.role != role) {
                role = as.role;
                if (role == AR_DROP || role == AR_DROP2)
                    seen |= 1u << role, starts = bp.st_starts - 1u;   /* (the hit of this block counts) */
                else
                    starts = 0xFFFFFFFFu;
            }
            if ((role == AR_DROP || role == AR_DROP2) && starts != 0xFFFFFFFFu && bp.st_starts != starts) {
                uint32_t k = bpb[(uint32_t)bp.st_bank & 3u].kicks ? bpb[(uint32_t)bp.st_bank & 3u].kicks : 1u;
                if (bp.st_slice >= 0 && ((k >> (uint32_t)bp.st_slice) & 1u))
                    kick |= 1u << role;
                starts = 0xFFFFFFFFu;
            }
        }
        transport_req = 2;
        block(0);
        block(0);
        snprintf(what, sizeof what, "drop kick: STYLE %s, the DROP's and the DROP 2's first hit a kick of the loop's",
                 N_BP_STYLE[ST[s]]);
        check(seen == (1u << AR_DROP | 1u << AR_DROP2) && kick == seen, what);
    }
}

/* the CHOP (chopgen.c): the session (a SLICE loop from a slot image, else RAVE HITS in PADS) at bpm, SLICE's STYLE, the
 * sections given, into a WAV */
static void chop_render(const char *path, const char *slot, uint32_t bpm, uint32_t style, const arr_entry_t *ent, uint32_t ne)
{
    uint32_t bar = 4u * 60u * FS / bpm, t, n = 0, i;
    FILE *f = fopen(path, "wb");
    if (!f)
        return;
    for (i = 0; i < ne; i++)
        n += ent[i].bars * bar;
    session(slot);
    song.g[G_BPM] = (int16_t)bpm;
    trk[2].p[P_X0 + 5] = CT_OFF;                      /* (the loop as it is: in the session's key already) */
    if (getenv("CHOP_SOLO"))                          /* (a check: the CHOP alone) */
        trk[0].p[P_MUTE] = trk[1].p[P_MUTE] = TBRK->p[P_MUTE] = 1;
    if (getenv("SOLO")) {                             /* (track k alone: 0 BASS, 1 CHORDS, 2 CHOP, 3 BREAK) */
        uint32_t k = (uint32_t)atoi(getenv("SOLO"));
        for (i = 0; i < NTRK; i++)
            trk[i].p[P_MUTE] = (int16_t)(i != k);
    }
    trk[2].p[P_X0 + 1] = (int16_t)style;
    trk[2].p[P_X0 + 2] = CS_STY[style % CY_N][0];
    trk[2].p[P_X0 + 3] = 50;
    arrangement_enabled = 1;
    as_set(2, 0, 0);
    transport_req = 1;
    block(0);
    arrangement.count = (uint8_t)ne;
    for (i = 0; i < ne; i++)
        arrangement.entry[i] = ent[i];
    arrangement_clock.index = 0;
    arrangement_clock.bar = 0;
    arrangement_clock.phase = 0;
    arrangement_clock.jump = -1;
    arrangement_apply(ent[0].scene);
    seq_reset_tracks(0);
    wav_hdr(f, (n / CTL) * CTL);
    for (t = 0; t + CTL <= n; t += CTL)
        block(f);
    fclose(f);
    transport_req = 2;
    block(0);
    block(0);
    printf("chop: %s\n", path);
}

/* LIVE's FX panel (lfx.c): a DROP of 8 bars with the riff, an effect pressed at the beats given on the tracks given
 * (black keys 1..4: bits 0..3), into a WAV */
typedef struct { uint8_t fx, black, at, len; } lfx_hit_t;
static void lfx_render(const char *path, const char *slot, const lfx_hit_t *h, uint32_t nh)
{
    uint32_t beat = 60u * FS / 170u, t, n = 32u * beat, i;
    static const arr_entry_t DROP8[1] = {{AR_DROP, 8}};
    FILE *f = fopen(path, "wb");
    if (!f)
        return;
    session(slot);
    trk[2].p[P_X0 + 5] = CT_OFF;
    arrangement_enabled = 1;
    as_set(2, 0, 0);
    transport_req = 1;
    block(0);
    arrangement.count = 1;
    arrangement.entry[0] = DROP8[0];
    arrangement_clock.index = 0;
    arrangement_clock.bar = 0;
    arrangement_clock.phase = 0;
    arrangement_clock.jump = -1;
    arrangement_apply(AR_DROP);
    seq_reset_tracks(0);
    lfx.held = lfx.latch = 0;
    lfx.clear = 1;
    wav_hdr(f, (n / CTL) * CTL);
    for (t = 0; t + CTL <= n; t += CTL) {
        for (i = 0; i < nh; i++) {
            uint32_t s0 = h[i].at * beat, s1 = (h[i].at + h[i].len) * beat;
            if (t <= s0 && s0 < t + CTL) {
                lfx.black = h[i].black;
                lfx_press(h[i].fx);
                lfx.black = 0;
            }
            if (t <= s1 && s1 < t + CTL)
                lfx_release(h[i].fx);
        }
        block(f);
    }
    fclose(f);
    transport_req = 2;
    block(0);
    block(0);
    printf("lfx: %s\n", path);
}

/* LIVE: the way to the next song (arranger_scene.c), asked as ui_live.c asks it (as_tr_ask, as_tr_slot; AUTO:
 * as_auto_next). The song SHORT at 170 BPM (INTRO 4, BUILD 4, DROP 8, BREAKDOWN 4, DROP 2 8, OUTRO 4), the next one
 * in slot 2 at 160. Bars counted by the arranger's clock (its phase wrapping), the tempo moving them */
static uint32_t tr_bar, tr_ph;
static void tr_block(void);
static void tr_setup(uint32_t endless)
{
    project_t *p = &proj_slot[1];
    transport_req = 2;
    block(0);
    block(0);
    session(0);
    memset(sec_card, 0, sizeof sec_card);
    arrangement_enabled = 1;
    as_set(0, 0, 0);
    song.g[G_BPM] = 160;
    proj_capture(p);
    p->sum = proj_sum(p);
    song.g[G_BPM] = 170;
    as_set(0, 0, endless);
    as_auto_next = as_tr_ask = as_tr_slot = 0;
    arrangement_clock.hold = 0;
    transport_req = 1;
    tr_bar = tr_ph = 0;
    tr_block();                                       /* (PLAY taken) */
}
static void tr_block(void)
{
    block(0);
    if (arrangement_clock.phase < tr_ph)
        tr_bar++;
    tr_ph = arrangement_clock.phase;
}
static int tr_late(void) { return arrangement_clock.phase > FS * 240u / 4u; }   /* (past the bar's first beat) */
/* until bar n (or the song stopped, or the next song in: *swap its bar) */
static void tr_until(uint32_t n, uint32_t *swap)
{
    uint32_t s0 = as_swapped, guard = 0;
    while (tr_bar < n && song.playing && guard++ < 200u * FS / CTL) {
        tr_block();
        if (swap && as_swapped != s0 && !*swap)
            *swap = tr_bar;
    }
}
/* LIVE's section keys, as ui_live.c live_poll_section puts the one asked (as_pend) into the arranger: on the phrase's
 * last bar (Q 4 BARS: every 4th of the section, else every bar), the next entry of it */
static void live_emul(void)
{
    uint32_t q = q_mode == QM_4BAR ? 4u : 1u, n = arrangement.count, cur = arrangement_clock.index, i;
    if (as_pend < 0 || arrangement_clock.jump >= 0 || (arrangement_clock.bar + 1u) % q)
        return;
    for (i = 1; i <= n && arrangement.entry[(cur + i) % n].scene != (uint8_t)as_pend; i++)
        ;
    arrangement_clock.jump = (int8_t)((cur + (i > n ? 1u : i)) % n);
    as_pend = -1;
}
static void transitions(void)
{
    uint32_t brk = 0, other = 0, swap = 0, role = AR_N, s0, k;
    int bpm_jump = 0, bpm_prev, bpm_moved = -1, flt_a = 99, flt_z = 99, flt_n = 99, flt_up = 0, fade_min = 4096,
        halftime = 0, held = 1, way15 = -1, way14 = -1, bpm_end = 0;
    double tail = 0, body = 0;

    /* AUTO, a next song in the set: the OUTRO is the bridge */
    tr_setup(0);
    as_auto_next = 2;
    s0 = as_swapped;
    tr_until(28, 0);
    bpm_prev = song.g[G_BPM];
    while (tr_bar < 34u && song.playing) {
        tr_block();
        if (as_swapped == s0) {
            int d = song.g[G_BPM] - bpm_prev;
            bpm_jump = d < 0 ? (-d > bpm_jump ? -d : bpm_jump) : (d > bpm_jump ? d : bpm_jump);
            bpm_prev = bpm_end = song.g[G_BPM];
            if (as.role != AR_OUTRO)
                continue;
            fade_min = as_fade < fade_min ? as_fade : fade_min;
            halftime |= BPV(BP_STYLE) == BS_HALFTIME;
            if (flt_a == 99)
                flt_a = TBRK->p[P_TFLT];
            flt_up |= TBRK->p[P_TFLT] < flt_z && flt_z != 99;   /* (a step back down) */
            flt_z = TBRK->p[P_TFLT];
            if (tr_late())
                brk |= (sounding() >> 3 & 1u) << (tr_bar - 28u);
            if (tr_bar > 28u && tr_late())
                other |= sounding() & 7u;
            for (k = 0; k < CTL; k++) {                /* (the OUTRO's last beat against its second bar) */
                double v = (double)out[2 * k] * out[2 * k];
                if (tr_bar == 31u && arrangement_clock.phase > FS * 240u * 3u / 4u)
                    tail += v;
                else if (tr_bar == 29u && arrangement_clock.phase > FS * 240u * 3u / 4u)
                    body += v;
            }
        } else if (!swap) {
            swap = tr_bar;
            role = as.role;
            flt_n = TBRK->p[P_TFLT];
        }
    }
    check(fade_min == 4096 && !halftime, "next song: AUTO's OUTRO is the bridge: no fade, the BREAK STRAIGHT (no HALFTIME)");
    check(brk == 15u && !other, "... the BREAK alone carries it, in every bar (BASS, CHORDS, CHOP out)");
    check(bpm_jump <= 1 && bpm_end == 160, "... the tempo glides 170 to 160, 1 BPM at a time, 160 on the seam");
    check(flt_a <= 3 && flt_z >= 44 && !flt_up, "... the BREAK thins into a high-pass over it (its own to 45)");
    check(swap == 32u && role == AR_BUILD && song.playing && song.g[G_BPM] == 160,
          "... the next song on the bar after the OUTRO, at its BUILD, at its tempo");
    check(flt_n >= 44 && flt_n <= flt_z + 4, "... its BUILD takes the BREAK's high-pass on from where the bridge left it (a V)");
    check(tail > body / 30.0, "... no silence at the seam (the OUTRO's last beat within 15 dB of its second bar)");
    check(as.role == AR_BUILD && (sounding() & 8u) && !as_tr_way && !arrangement_clock.hold,
          "... its BUILD plays on in AUTO (its 3rd bar), nothing left of the way");

    /* white key 8, BRIDGE: asked in the DROP, SWAP first then BRIDGE (the last choice), taken on the phrase */
    tr_setup(0);
    tr_until(10, 0);
    as_tr_slot = 2;
    as_tr_ask = TR_SWAP;
    tr_until(11, 0);
    as_tr_ask = TR_BRIDGE;
    tr_until(14, 0);
    way14 = as_tr_way;
    brk = other = swap = 0;
    bpm_moved = -1;
    s0 = as_swapped;
    while (tr_bar < 26u && song.playing) {
        tr_block();
        if (tr_bar == 15u && way15 < 0)
            way15 = as_tr_way;
        if (as_swapped == s0) {
            if (bpm_moved < 0 && song.g[G_BPM] != 170)
                bpm_moved = (int)tr_bar;
            if (tr_bar >= 16u) {
                held &= as.role == AR_BREAKDOWN;
                if (tr_late())
                    brk |= (sounding() >> 3 & 1u) << (tr_bar - 16u);
                if (tr_bar >= 17u && tr_late())
                    other |= sounding() & 7u;
            }
        } else if (!swap) {
            swap = tr_bar;
            role = as.role;
            bpm_end = song.g[G_BPM];
        }
    }
    check(way14 == 0 && way15 == TR_BRIDGE && !as_tr_ask,
          "key 8: taken on the 8-bar phrase's last bar (the DROP's 8th), the last choice (BRIDGE, not SWAP)");
    check(held && brk == 255u && !other,
          "... 8 bars over the BREAKDOWN, held: the BREAK in every bar (no hole), BASS, CHORDS, CHOP out");
    check(bpm_moved == 16 && swap == 24u && role == AR_BUILD && bpm_end == 160,
          "... the tempo from the bridge's first bar; the next song after 8 bars, at its BUILD, at 160");

    /* cancelled while it waits: nothing happens */
    tr_setup(0);
    tr_until(10, 0);
    as_tr_slot = 2;
    as_tr_ask = TR_BRIDGE;
    tr_until(12, 0);
    as_tr_ask = 0;
    s0 = as_swapped;
    tr_until(26, 0);
    check(as_swapped == s0 && !as_tr_way && song.g[G_BPM] == 170 && as.role == AR_DROP2,
          "key 8: cancelled before the phrase (none): no bridge, the song goes on");

    /* SWAP: asked in the INTRO, the old BREAKDOWN from the next phrase (not the BUILD), 8 bars, the next DROP */
    tr_setup(0);
    tr_until(1, 0);
    as_tr_slot = 2;
    as_tr_ask = TR_SWAP;
    s0 = as_swapped;
    held = 1;
    brk = 0;
    swap = 0;
    bpm_moved = -1;
    fade_min = 4096;
    while (tr_bar < 14u && song.playing) {
        tr_block();
        if (as_swapped == s0) {
            fade_min = as_fade < fade_min ? as_fade : fade_min;
            if (bpm_moved < 0 && song.g[G_BPM] != 170)
                bpm_moved = (int)tr_bar;
            if (tr_bar >= 4u) {
                held &= as.role == AR_BREAKDOWN;
                if (tr_late())
                    brk |= sounding() & 9u;
            }
        } else if (!swap) {
            swap = tr_bar;
            role = as.role;
            bpm_end = song.g[G_BPM];
            flt_n = !((as_keys >> PA_VERB) & 1u) && as_fade == 4096;
        }
    }
    check(held && !brk && bpm_moved == 4, "SWAP: the old song's BREAKDOWN from the INTRO's end (no BREAK, no BASS), the tempo on its way");
    check(swap == 12u && role == AR_DROP && bpm_end == 160 && flt_n && !fade_min,
          "... 8 bars, then the next song's DROP, at 160: out of the gap (silence), dry");

    /* CUT: the next song's DROP on the bar after the phrase, the tempo at once */
    tr_setup(0);
    tr_until(1, 0);
    as_tr_slot = 2;
    as_tr_ask = TR_CUT;
    s0 = as_swapped;
    swap = 0;
    bpm_moved = -1;
    while (tr_bar < 6u && song.playing) {
        tr_block();
        if (as_swapped == s0) {
            if (bpm_moved < 0 && song.g[G_BPM] != 170)
                bpm_moved = (int)tr_bar;
        } else if (!swap) {
            swap = tr_bar;
            role = as.role;
        }
    }
    check(bpm_moved < 0 && swap == 4u && role == AR_DROP && song.g[G_BPM] == 160,
          "CUT: the next song's DROP on the INTRO's next bar, the tempo there at once");

    /* MAN (AUTO off): after the BRIDGE the next song's BUILD plays on */
    tr_setup(0);
    arrangement_clock.hold = 1;
    tr_until(1, 0);
    as_tr_slot = 2;
    as_tr_ask = TR_BRIDGE;
    swap = 0;
    tr_until(12, &swap);
    tr_until(24, 0);
    check(swap == 12u && as.role == AR_BUILD && arrangement_clock.hold && song.playing,
          "MAN: the bridge from the INTRO, the next song's BUILD plays on (12 bars after it, still)");

    /* MAN, the BUILD on repeat, the DROP picked by hand (in its 2nd bar): the way in as AUTO's, before the bar it lands
     * on (Q BAR: the next; Q 4 BARS: after the BUILD's 4th) */
    {
        uint8_t qm = q_mode;
        for (k = 0; k < 2u; k++) {
            int wash = -1, gap_at = -1, land = -1, dive = 0, dry = 1, held = 1;
            tr_setup(0);
            tr_until(5, 0);                           /* (the BUILD from bar 4) */
            arrangement_clock.hold = 1;
            tr_until(9, 0);                           /* (its 2nd round: bars 8..11) */
            held = as.role == AR_BUILD;
            q_mode = k ? QM_4BAR : QM_BAR;
            as_pend = AR_DROP;
            while (tr_bar < 16u && song.playing) {
                uint32_t at = tr_bar * 4u + (clk_beat & 3u), v;
                live_emul();
                tr_block();
                if (as.role != AR_DROP) {
                    if (wash < 0 && (as_keys >> PA_WASH) & 1u && as_mute != 15u)
                        wash = (int)at;
                    if (gap_at < 0 && as_keys == 1u << PA_TSTOP)
                        gap_at = (int)at;
                } else {
                    if (land < 0)
                        land = (int)tr_bar, dry = !((as_keys >> PA_VERB) & 1u) && as_fade == 4096;
                    for (v = 0; v < NVOICE; v++)
                        dive |= tr_bar == (uint32_t)land && trk[0].v[v].active && trk[0].v[v].dive > 16000;
                }
            }
            if (!k)
                check(held && wash >= 36 && wash < 39 && gap_at == 39 && land == 10 && dive && dry,
                      "MAN, Q BAR: the BUILD on repeat, the DROP picked: the WASH and the roll, the tape stop, the DROP on the next bar, dry, the sub drop");
            else
                check(held && wash == 43 && gap_at == 47 && land == 12 && dive && dry,
                      "... Q 4 BARS: the way in from the BUILD's 3rd bar (as AUTO's), the DROP after its 4th");
        }
        q_mode = qm;
        as_pend = -1;
    }

    /* STOP in the bridge: the way gone, AUTO's hold back, the song (its tempo) as it was */
    tr_setup(0);
    tr_until(1, 0);
    as_tr_slot = 2;
    as_tr_ask = TR_BRIDGE;
    tr_until(7, 0);
    k = as_tr_way;
    transport_req = 2;
    block(0);
    block(0);
    check(k == TR_BRIDGE && !as_tr_way && !as_swap && !arrangement_clock.hold && song.g[G_BPM] == 170,
          "STOP in the bridge: the way gone, the hold given back, the tempo the song's");

    /* ENDLESS: AUTO does not go on by itself */
    tr_setup(1);
    as_auto_next = 2;
    s0 = as_swapped;
    tr_until(64, 0);
    check(as_swapped == s0 && song.playing && !as_tr_way, "ENDLESS: AUTO stays on the song (64 bars, two rounds and more)");
    transport_req = 2;
    block(0);
    block(0);
    as_auto_next = 0;
}

/* the DROP's 8 bars, track k alone (0 BASS, 1 CHORDS, 2 CHOP, 3 BREAK; 4 all), its sends off: the dry stems the
 * reverb prototypes (tests/verb_proto.c) run on. Each from the same start: the mutes change no voice */
static void stem_render(const char *path, uint32_t k)
{
    uint32_t beat = 60u * FS / 170u, t, n = 32u * beat, i;
    static const arr_entry_t DROP8[1] = {{AR_DROP, 8}};
    FILE *f = fopen(path, "wb");
    if (!f)
        return;
    session(0);
    arrangement_enabled = 1;
    as_set(2, 0, 0);
    transport_req = 1;
    block(0);
    arrangement.count = 1;
    arrangement.entry[0] = DROP8[0];
    arrangement_clock.index = 0;
    arrangement_clock.bar = 0;
    arrangement_clock.phase = 0;
    arrangement_clock.jump = -1;
    arrangement_apply(AR_DROP);
    seq_reset_tracks(0);
    wav_hdr(f, (n / CTL) * CTL);
    for (t = 0; t + CTL <= n; t += CTL) {
        for (i = 0; i < NTRK; i++) {
            trk[i].p[P_MUTE] = (int16_t)(k < NTRK && i != k);
            trk[i].p[P_REV] = trk[i].p[P_CHOR] = trk[i].p[P_DLY] = 0;
        }
        block(f);
    }
    fclose(f);
    transport_req = 2;
    block(0);
    block(0);
    printf("stem: %s\n", path);
}

int main(int argc, char **argv)
{
    uint32_t t, bar = 4u * 60u * FS / 170u, at[8] = {0}, n = 0, idx = 0xFF, i, f_stop = 0;
    uint32_t heard[8] = {0}, heard_late[8] = {0}, heard_first[8] = {0};
    int32_t flt_lo[8], flt_hi[8], flt_end[8];
    int impact = 0, tstop1 = 0, tstop1_fade = 4096, hushed = 0, tstop = 0, tstop_fade = 4096, dive[8] = {0},
        dive_late[8] = {0}, filt_now = 0, build_hp = 0, drop_steep = 0, roll_before = 0, outro_start = -1, outro_end = -1, fade_end = 4096;
    double tail = 0, body = 0;
    static step_t bass0[BL_STEPS];
    int16_t mangle0;
    int bass_new = 0;
    FILE *f;
    if (argc > 2 && !strcmp(argv[1], "stems")) {     /* song_audio_test stems OUTDIR: the DROP's dry stems */
        static const char *const NM[NTRK + 1] = {"bass", "chords", "chop", "break", "all"};
        char pth[512];
        for (i = 0; i <= NTRK; i++) {
            snprintf(pth, sizeof pth, "%s/stem-%s.wav", argv[2], NM[i]);
            stem_render(pth, i);
        }
        return 0;
    }
    if (argc > 3 && !strcmp(argv[1], "lfx")) {       /* song_audio_test lfx OUTDIR SLOT K: the SPACE group's K */
        static const lfx_hit_t DUB[3] = {{LX_DUB, 8, 7, 1}, {LX_DUB, 1, 15, 1}, {LX_DUB, 0, 23, 2}};
        static const lfx_hit_t ECH[3] = {{LX_ECHO, 1, 7, 1}, {LX_ECHO, 8, 15, 1}, {LX_ECHO, 0, 23, 1}};
        static const lfx_hit_t SPK[3] = {{LX_SPARK, 4, 4, 4}, {LX_SPARK, 8, 16, 4}, {LX_SPARK, 0, 24, 4}};
        static const lfx_hit_t VOI[2] = {{LX_VOID, 1, 8, 4}, {LX_VOID, 0, 20, 4}};
        static const lfx_hit_t FRZ[2] = {{LX_FREEZE, 8, 7, 4}, {LX_FREEZE, 0, 20, 4}};
        static const lfx_hit_t SHD[2] = {{LX_SHARDS, 1, 8, 4}, {LX_SHARDS, 0, 20, 6}};
        static const lfx_hit_t TAP[2] = {{LX_TAPE, 8, 4, 3}, {LX_TAPE, 0, 16, 10}};
        static const lfx_hit_t REV[2] = {{LX_REVERSE, 8, 8, 5}, {LX_REVERSE, 0, 20, 7}};
        static const lfx_hit_t DFL[2] = {{LX_DFILT, 1, 8, 4}, {LX_DFILT, 0, 20, 6}};
        static const lfx_hit_t CRU[2] = {{LX_CRUSH, 8, 4, 4}, {LX_CRUSH, 0, 16, 8}};
        static const lfx_hit_t VOW[2] = {{LX_VOWEL, 4, 4, 8}, {LX_VOWEL, 8, 16, 8}};
        static const lfx_hit_t RES[2] = {{LX_RESON, 1, 8, 4}, {LX_RESON, 0, 20, 6}};
        static const char *const NM[13] = {"0-dry", "1-dub-verb", "2-echo-throw", "3-sparkle", "4-void", "5-freeze",
                                           "6-shards", "7-tape", "8-reverse", "9-dub-filter", "10-psx-crush", "11-vowel",
                                           "12-resonator"};
        static const lfx_hit_t *const HS[13] = {DUB, DUB, ECH, SPK, VOI, FRZ, SHD, TAP, REV, DFL, CRU, VOW, RES};
        static const uint32_t NS[13] = {0, 3, 3, 3, 2, 2, 2, 2, 2, 2, 2, 2, 2};
        uint32_t k = argc > 4 ? (uint32_t)atoi(argv[4]) % 13u : 0u;   /* (one a run: each from the same start) */
        char pth[512];
        snprintf(pth, sizeof pth, "%s/lfx-%s.wav", argv[2], NM[k]);
        lfx_render(pth, argv[3], HS[k], NS[k]);
        return 0;
    }
    if (argc > 2 && !strcmp(argv[1], "melody")) {    /* song_audio_test melody OUTDIR: INST's MELODY, 3 ways x 2 songs */
        static const struct { uint8_t root, num, color, bpm; uint16_t var; const char *name; } SG[2] = {
            {5, 4, CC_7TH, 170, 1, "song1-Fm"}, {9, 3, CC_ADD9, 165, 95, "song2-Am"}};
        static const char *const MN[CML_N] = {"off", "midi", "arp", "sing"};
        uint32_t sgi, mi, pi;
        for (sgi = 0; sgi < 2u; sgi++)
            for (mi = CML_ARP; mi < CML_N; mi++) {
                char pth[512];
                FILE *f;
                uint32_t n = 16u * 4u * 60u * FS / SG[sgi].bpm, t;
                session(0);
                song.g[G_BPM] = SG[sgi].bpm;
                trk[0].p[P_ROOT] = trk[1].p[P_ROOT] = SG[sgi].root;
                cg.var = SG[sgi].var;
                cg.num = SG[sgi].num;
                cg.color = SG[sgi].color;
                cg_poll();
                bl_write();
                for (pi = 0; pi < ENG_ANALOG.npresets && !str_eq(ENG_ANALOG.presets[pi].name, "GAME SQR"); pi++)
                    ;
                host_preset(&trk[2], ENGI_ANALOG, pi);
                trk[2].p[P_X0] = CM_INST;
                trk[2].p[P_X0 + 4] = (int16_t)mi;
                trk[2].p[P_X0 + 6] = 0;
                memset(trk[2].step, 0, sizeof trk[2].step);
                if (getenv("SOLO"))
                    trk[0].p[P_MUTE] = trk[1].p[P_MUTE] = TBRK->p[P_MUTE] = 1;
                snprintf(pth, sizeof pth, "%s/%s-%s.wav", argv[2], SG[sgi].name, MN[mi]);
                f = fopen(pth, "wb");
                if (!f)
                    return 1;
                wav_hdr(f, (n / CTL) * CTL);
                transport_req = 1;
                for (t = 0; t + CTL <= n; t += CTL)
                    block(f);
                fclose(f);
                transport_req = 2;
                block(0);
                block(0);
                printf("melody: %s (%u notes)\n", pth, cm_n[cm_b]);
            }
        return 0;
    }
    if (argc > 3 && !strcmp(argv[1], "leads")) {     /* song_audio_test leads OUTDIR NAME..: INST's sound NAME (ANALOG or
                                                       * SAMPLE) on MELODY in F minor at 170, 8 bars (SOLO=1: alone) */
        int a;
        for (a = 3; a < argc; a++) {
            char pth[512], nm[32];
            FILE *f;
            uint32_t n = 8u * 4u * 60u * FS / 170u, t, pi, e = ENGI_ANALOG, c;
            for (pi = 0; pi < ENG_ANALOG.npresets && !str_eq(ENG_ANALOG.presets[pi].name, argv[a]); pi++)
                ;
            if (pi == ENG_ANALOG.npresets) {
                e = ENGI_SAMPLE;
                for (pi = 0; pi < ENG_SAMPLE.npresets && !str_eq(ENG_SAMPLE.presets[pi].name, argv[a]); pi++)
                    ;
                if (pi == ENG_SAMPLE.npresets) {
                    printf("leads: no preset %s\n", argv[a]);
                    return 1;
                }
            }
            session(0);
            song.g[G_BPM] = 170;
            trk[0].p[P_ROOT] = trk[1].p[P_ROOT] = 5;
            cg.var = 1;
            cg.num = 4;
            cg.color = CC_7TH;
            cg_poll();
            bl_write();
            host_preset(&trk[2], e, pi);
            trk[2].p[P_X0] = CM_INST;
            trk[2].p[P_X0 + 4] = getenv("MEL") ? (int16_t)atoi(getenv("MEL")) : (int16_t)CML_ARP;
            trk[2].p[P_X0 + 6] = 0;
            memset(trk[2].step, 0, sizeof trk[2].step);
            if (getenv("SOLO"))
                trk[0].p[P_MUTE] = trk[1].p[P_MUTE] = TBRK->p[P_MUTE] = 1;
            for (c = 0; argv[a][c] && c < 31u; c++)
                nm[c] = argv[a][c] == ' ' ? '-' : (char)(argv[a][c] | 0x20);
            nm[c] = 0;
            snprintf(pth, sizeof pth, "%s/%02d-%s.wav", argv[2], a - 2, nm);
            f = fopen(pth, "wb");
            if (!f)
                return 1;
            wav_hdr(f, (n / CTL) * CTL);
            transport_req = 1;
            for (t = 0; t + CTL <= n; t += CTL)
                block(f);
            fclose(f);
            transport_req = 2;
            block(0);
            block(0);
            printf("leads: %s\n", pth);
        }
        return 0;
    }
    if (argc > 3 && !strcmp(argv[1], "chops")) {     /* song_audio_test chops OUTDIR SLOT [BPM]: SLICE's STYLEs on a loop */
        static const arr_entry_t DROP8[1] = {{AR_DROP, 16}};
        char pth[512];
        uint32_t st, bpm = argc > 4 ? (uint32_t)atoi(argv[4]) : 170u;
        for (st = 0; st < CY_N; st++) {
            char nm[16];
            uint32_t c;
            for (c = 0; N_CS_STYLE[st][c] && c < 15u; c++)
                nm[c] = (char)(N_CS_STYLE[st][c] | 0x20);
            nm[c] = 0;
            snprintf(pth, sizeof pth, "%s/slice-%u-%s.wav", argv[2], st + 1u, nm);
            chop_render(pth, argv[3], bpm, st, DROP8, 1);
        }
        return 0;
    }
    if (argc > 2 && !strcmp(argv[1], "gaps")) {      /* song_audio_test gaps OUTDIR: the way into the DROP, five ways */
        static const struct { uint8_t beats, tail; const char *name; } G[5] = {
            {1, 0, "a-1beat-cut"}, {2, 0, "b-2beats-cut"}, {2, 1, "c-2beats-echo"}, {4, 0, "d-1bar-cut"}, {4, 1, "e-1bar-echo"}};
        char pth[512];
        uint32_t k;
        for (k = 0; k < 5u; k++) {
            snprintf(pth, sizeof pth, "%s/gap-%s.wav", argv[2], G[k].name);
            gap_render(pth, G[k].beats, G[k].tail);
        }
        return 0;
    }
    if (argc > 3 && !strcmp(argv[1], "drops")) {      /* song_audio_test drops OUTDIR K [STYLE]: BUILD -> DROP, way K
                                                       * (0..3), the BREAK in STYLE (0 STRAIGHT .. 5 CHAOS) */
        static const struct { uint8_t drop; const char *name; } D[4] = {
            {0, "a-plain"}, {DR_THROW, "b-first-hit-thrown"}, {DR_LOUD | DR_DIVE, "c-build-down-slow-sub"},
            {DR_THROW | DR_LOUD | DR_DIVE, "d-all"}};
        char pth[512];
        uint32_t k = (uint32_t)atoi(argv[3]) & 3u;
        int st = argc > 4 ? atoi(argv[4]) : -1;
        if (st >= 0)
            snprintf(pth, sizeof pth, "%s/drop-%s-%s.wav", argv[2], D[k].name, N_BP_STYLE[st % BS_NSTYLE]);
        else
            snprintf(pth, sizeof pth, "%s/drop-%s.wav", argv[2], D[k].name);
        drop_render(pth, D[k].drop, st);
        return 0;
    }
    if (argc > 2 && !strcmp(argv[1], "seams")) {     /* song_audio_test seams OUTDIR: ROLL -> BREAKDOWN three ways */
        char pth[512];
        snprintf(pth, sizeof pth, "%s/roll-breakdown-1-now.wav", argv[2]);
        seam_render(pth, SEAM_CLEAN, 0);
        snprintf(pth, sizeof pth, "%s/roll-breakdown-2-dub.wav", argv[2]);
        seam_render(pth, SEAM_DUB, 0);
        snprintf(pth, sizeof pth, "%s/roll-breakdown-3-dub-pads-under-water.wav", argv[2]);
        seam_render(pth, SEAM_DUB, 1);
        return 0;
    }
    if (argc > 2)
        return render(argv[1], argv[2], argc > 3 ? (uint32_t)atoi(argv[3]) : 1u, argc > 4 ? (uint32_t)atoi(argv[4]) : 0u);
    f = argc > 1 ? fopen(argv[1], "wb") : 0;

    session(0);
    memcpy(bass0, trk[0].step, sizeof bass0);
    BPV(BP_MANGLE) = 10;
    mangle0 = BPV(BP_MANGLE);
    arrangement_enabled = 1;
    as_set(0, 0, 0);                                  /* SHORT, VARIANT 0, to the end */
    check(arrangement.count == 6u && as_total(&arrangement) == 32u && arrangement.entry[2].scene == AR_DROP &&
          arrangement.entry[3].scene == AR_BREAKDOWN && arrangement.entry[4].scene == AR_DROP2,
          "SHORT: INTRO 4, BUILD 4, DROP 8, BREAKDOWN 4, DROP 2 8, OUTRO 4");
    if (f)
        wav_hdr(f, (33u * bar / CTL + 1u) * CTL);
    for (i = 0; i < 8u; i++)
        flt_lo[i] = 99, flt_hi[i] = -99, flt_end[i] = 99;
    transport_req = 1;
    for (t = 0; t < 33u * bar; t += CTL) {
        uint32_t s, k;
        block(f);
        for (k = 0; k < CTL && song.playing; k++) {   /* (the OUTRO's last beat against a bar of the DROP) */
            double v = (double)out[2 * k] * out[2 * k];
            if (t >= 31u * bar + bar * 3u / 4u)
                tail += v;
            else if (t >= 12u * bar && t < 13u * bar)
                body += v;
        }
        if (!song.playing) {
            if (!f_stop)
                f_stop = t;
            continue;
        }
        if (arrangement_clock.index != idx) {         /* a new section: when */
            idx = arrangement_clock.index;
            if (n < 8u)
                at[n++] = t;
            if (arrangement.entry[idx].scene == AR_DROP2)
                bass_new = memcmp(bass0, trk[0].step, sizeof bass0) != 0 && BPV(BP_MANGLE) == mangle0;
            if (arrangement.entry[idx].scene == AR_DROP)
                filt_now = !tflt[TRK_BRK].mode && !TBRK->p[P_TFLT];   /* (its own: 0, at once) */
        }
        s = sounding();
        if (idx < 8u) {
            int32_t fl = arrangement.entry[idx].scene == AR_INTRO ? trk[CS_TRK].p[P_TFLT] : TBRK->p[P_TFLT];
            heard[idx] |= s;
            if (t % bar > bar / 4u)                   /* (past the first beat: tails, fades gone) */
                heard_late[idx] |= s;
            if (clk_beat == 0u)
                heard_first[idx] |= s;
            flt_lo[idx] = fl < flt_lo[idx] ? fl : flt_lo[idx];
            flt_hi[idx] = fl > flt_hi[idx] ? fl : flt_hi[idx];
            flt_end[idx] = fl;
            for (k = 0; k < NVOICE; k++)              /* the sub drop: on a voice of the BASS in the first beat; none
                                                       * left after the section's first bar */
                if (trk[0].v[k].active && trk[0].v[k].dive) {
                    if (arrangement_clock.bar == 0u && clk_beat == 0u && trk[0].v[k].dive > 16000)
                        dive[idx] = 1;
                    if (arrangement_clock.bar >= 1u)
                        dive_late[idx] = 1;
                }
        }
        if (idx == 1u && arrangement_clock.bar == 3u && (clk_beat & 3u) == 1u)   /* the roll: before the gap */
            roll_before |= (as_keys >> PA_ROLL16) & 1u;
        if (idx == 1u && arrangement_clock.bar == 3u && (clk_beat & 3u) == 1u)   /* the BUILD's end: no low end */
            build_hp = tflt[1].steep && tflt[2].steep && tflt[3].steep && tflt[3].mode > 0 && trk[1].p[P_TFLT] >= 24 &&
                       trk[2].p[P_TFLT] >= 24;
        if (idx == 2u)
            drop_steep |= tflt[1].steep | tflt[2].steep | tflt[3].steep;
        if (idx == 1u && arrangement_clock.bar == 3u && (clk_beat & 3u) == 3u) {   /* the tape stop into the DROP */
            tstop1 |= as_keys == 1u << PA_TSTOP && punch.cur == PX_TSTOP;
            tstop1_fade = as_fade < tstop1_fade ? as_fade : tstop1_fade;
            hushed |= fx_hush && fx.hush >= DLY_LEN + CHO_LEN + sizeof spu_buf / sizeof spu_buf[0];
        }
        if (idx == 3u && arrangement_clock.bar == 3u && (clk_beat & 3u) == 3u) {   /* the tape stop */
            tstop |= as_keys == 1u << PA_TSTOP && punch.cur == PX_TSTOP;
            tstop_fade = as_fade < tstop_fade ? as_fade : tstop_fade;
        }
        if (idx == 2u && !clk_beat)
            impact |= (as_keys >> PA_VERB) & 1u;
        if (idx == 5u) {
            if (outro_start < 0)
                outro_start = BPV(BP_STYLE);
            outro_end = BPV(BP_STYLE);
            fade_end = as_fade;
        }
    }
    if (f)
        fclose(f);
    {
        static const uint32_t BAR_AT[6] = {0, 4, 8, 16, 20, 28};
        int ok = n == 6u;
        for (i = 0; i < n && i < 6u; i++) {
            double want = BAR_AT[i] * 4.0 * 60.0 * FS / 170.0;   /* (the bar exactly: the clock keeps its remainder) */
            ok &= at[i] + CTL > want && at[i] <= want + CTL;
        }
        check(ok, "each section on its bar (to the block)");
    }
    check(!(heard_late[0] & (1u | 8u)) && (heard[0] & 2u), "INTRO: CHORDS (and CHOP), no BASS, no BREAK");
    check(flt_lo[0] <= -55 && flt_end[0] >= -3, "INTRO: the CHOP out of a low-pass (FILT -56 up to its own)");
    check((heard[1] & 8u) && !(heard[1] & 1u), "BUILD: the BREAK in, no BASS");
    check(flt_hi[1] >= 44 && flt_end[1] >= 26 && flt_end[1] <= 30,
          "BUILD: the BREAK through a high-pass coming down (45 to 28: no low end until the drop)");
    check(build_hp && !drop_steep, "... the pads and the CHOP through one going up (to 25); all three the steep high-pass, none in the DROP");
    check(roll_before && tstop1 && tstop1_fade == 4096 && hushed,
          "the beat before the DROP: the tape stop (the roll before it, the BUILD at full level, the tails emptied under it)");
    check(heard[2] == 15u && (heard_first[2] & 9u) == 9u, "DROP: all four, the BASS and the BREAK from its first beat");
    check(!impact && filt_now, "DROP: dry on its first beat (no reverb thrown), the BREAK's filter its own at once");
    check(dive[2] && !dive_late[2] && !bass_dive, "DROP: the BASS's first note dives in from an octave up (the sub drop), that one only");
    check(tstop && tstop_fade == 4096, "the beat before the DROP 2: the tape stop (no gap)");
    check(dive[4] && !dive_late[4], "DROP 2: the sub drop too");
    check(!(heard_late[3] & (1u | 8u)) && (heard[3] & 2u), "BREAKDOWN: the BREAK and the BASS out (after their tails)");
    check(bass_new, "DROP 2: a new BASS phrase on its first block, the BREAK's MANGLE as the loop's own");
    check((heard[4] & 1u) && (heard[4] & 8u), "DROP 2: the BASS and the BREAK back");
    check(outro_start == BS_STRAIGHT && outro_end == BS_HALFTIME, "OUTRO: the BREAK STRAIGHT, then slowing down (HALFTIME)");
    check(fade_end < 100 && tail < body / 300.0, "OUTRO: the mix faded out to its end (its last beat -25 dB under the DROP)");
    check(f_stop >= 32u * bar - CTL && f_stop <= 32u * bar + 2u * CTL, "the song stops after its 32 bars");
    check(!memcmp(bass0, trk[0].step, sizeof bass0) && BPV(BP_MANGLE) == mangle0 && !as_keys && !as_mute &&
          !(sounding() & 1u), "stopped: the loop as it was (the BASS phrase, MANGLE), no moves, no mutes");

    /* ENDLESS: no OUTRO; after the second drop, a new round */
    as_set(0, 0, 1);
    check(arrangement.loop && arrangement.entry[arrangement.count - 1u].scene == AR_DROP2 && as_total(&arrangement) == 28u,
          "ENDLESS: the chain without its OUTRO (28 bars)");
    transport_req = 1;
    for (t = 0; t < 30u * bar; t += CTL)
        block(0);
    check(song.playing && as.cycle == 1u && arrangement.entry[0].scene == AR_BUILD && arrangement_clock.index == 0u,
          "... after 28 bars: a new round (BUILD first), still playing");
    for (t = 0; t < 40u * bar && as.role != AR_ROLL; t += CTL)
        block(0);
    check(as.role == AR_ROLL && BPV(BP_STYLE) == BS_ROLLER && !(heard[0] & 0), "ROLL: the BREAK as a ROLLER");
    for (t = 0; t < 40u * bar && as.role != AR_BREAKDOWN; t += CTL)
        block(0);
    for (t = 0, i = 0; t < bar / 8u; t += CTL) {      /* (after a ROLL: the pads out, now back) */
        block(0);
        i |= sounding() & 2u;
    }
    check(as.role == AR_BREAKDOWN && i, "BREAKDOWN after a ROLL: the CHORDS from its first beat (not a bar late)");
    /* a jump: BREAKDOWN asked for, on the next bar */
    for (i = 0; i < arrangement.count && arrangement.entry[i].scene != AR_BREAKDOWN; i++)
        ;
    arrangement_clock.jump = (int8_t)i;
    for (t = 0; t < bar + CTL && arrangement_clock.index != i; t += CTL)
        block(0);
    check(arrangement_clock.index == i && as.role == AR_BREAKDOWN && t <= bar && arrangement_clock.bar == 0u,
          "a jump: BREAKDOWN from the next bar");
    {   /* LIVE: REWIND resolved into a section (as_rw_to, ui_live.c): no lead, the rewind is its way in; a jump of a
         * section key: the tape stop before it */
        static const uint8_t TO[2] = {AR_DROP2, AR_DROP};
        uint32_t k, d, ts[2] = {0, 0};
        for (k = 0; k < 2u; k++) {
            for (d = 0; d < arrangement.count && arrangement.entry[d].scene != TO[k]; d++)
                ;
            arrangement_clock.jump = (int8_t)d;
            as_rw_to = (int8_t)(k ? -1 : (int)d);
            for (t = 0; t < bar + CTL && arrangement_clock.index != d; t += CTL) {
                block(0);
                ts[k] |= as_keys & (1u << PA_TSTOP);
            }
            as_rw_to = -1;
            check(arrangement_clock.index == d && as.role == TO[k] && arrangement_clock.bar == 0u, k ?
                  "... a jump into the DROP: on the next bar" : "REWIND into the DROP 2: on the next bar");
        }
        check(!ts[0] && ts[1], "... REWIND's: no tape stop before it; a section key's: the tape stop");
        for (d = 0; d < arrangement.count && arrangement.entry[d].scene != AR_BREAKDOWN; d++)
            ;
        arrangement_clock.jump = (int8_t)d;             /* (back in the BREAKDOWN, as below) */
        for (t = 0; t < bar + CTL && arrangement_clock.index != d; t += CTL)
            block(0);
    }
    {   /* LIVE: the next song (as_swap, ui_live.c): on the next bar, with its tempo and sound, from the chain's first DROP;
         * the BREAK on through; STOP keeps the new one */
        project_t *p = &proj_slot[1];
        uint32_t d, brk = 0;
        proj_capture(p);
        p->g[G_BPM] = 160;
        p->t[0].p[P_TFLT] = -20;
        p->sum = proj_sum(p);
        as_swap = 2;
        for (t = 0; t < bar + CTL && as_swap; t += CTL)
            block(0);
        for (d = 0; d < arrangement.count && arrangement.entry[d].scene != AR_DROP; d++)
            ;
        check(!as_swap && t <= bar && song.g[G_BPM] == 160 && trk[0].p[P_TFLT] == -20 && arrangement_clock.index == d &&
              as.role == AR_DROP && arrangement_clock.bar == 0u && song_keep.g[G_BPM] == 160,
              "LIVE: the next song on the next bar: its tempo, its sound, from its DROP");
        for (t = 0; t < bar / 2u; t += CTL) {
            block(0);
            brk |= sounding() & 8u;
        }
        check(brk && song.playing, "... the BREAK plays on");
        transport_req = 2;
        block(0);
        block(0);
        check(!song.playing && song.g[G_BPM] == 160 && trk[0].p[P_TFLT] == -20, "... STOP: the new song stays");
    }
    {   /* a change of yours while it plays (LIVE's PRESETS through q.c, the BREAK screen in STUDIO): the next section
         * and STOP keep it; a knob the section drives (a BUILD's high-pass) by your turn only */
        uint32_t nl = brkp_nloops(), role;
        int16_t loop1, flt0;
        memset(sec_card, 0, sizeof sec_card);
        as_set(0, 0, 0);
        transport_req = 1;
        for (t = 0; t < bar / 2u; t += CTL)
            block(0);
        loop1 = (int16_t)((TBRK->p[P_LRATE] + 1) % (int32_t)(nl ? nl : 1u));
        q_param(TRK_BRK, P_LRATE, loop1);                 /* (Q NOW: at once) */
        role = as.role;
        for (t = 0; t < 16u * bar && as.role == role; t += CTL)
            block(0);
        check(as.role != role && TBRK->p[P_LRATE] == loop1, "yours while it plays: a loop picked, the next section keeps it");
        for (t = 0; t < 16u * bar && as.role != AR_BUILD; t += CTL)
            block(0);
        for (t = 0; t < bar; t += CTL)
            block(0);
        flt0 = song_keep.t[TRK_BRK].p[P_TFLT];
        {   /* (ui_break.c: FILT turned down 5 in the BUILD) */
            int32_t was = TBRK->p[P_TFLT];
            TBRK->p[P_TFLT] = (int16_t)clamp(was - 5, -64, 63);
            song_keep_turn(TRK_BRK, P_TFLT, TBRK->p[P_TFLT] - was, -64, 63);
        }
        for (t = 0; t < 16u * bar && as.role == AR_BUILD; t += CTL)
            block(0);
        for (t = 0; t < bar / 2u; t += CTL)
            block(0);
        check(as.role != AR_BUILD && TBRK->p[P_TFLT] == flt0 - 5,
              "yours while it plays: FILT turned in a BUILD, after it the loop's own filter less your turn (no high-pass kept)");
        transport_req = 2;
        block(0);
        block(0);
        check(!song.playing && TBRK->p[P_LRATE] == loop1 && TBRK->p[P_TFLT] == flt0 - 5, "yours while it plays: STOP keeps them");
    }
    {   /* the section cards (the song's, project.c): a card changes only what it says; an empty one nothing */
        uint32_t k, late = 0, intro_bass = 0, i2;
        int ok;
        transport_req = 2;
        block(0);
        block(0);
        memset(sec_card, 0, sizeof sec_card);
        as_set(0, 0, 0);
        i2 = as_total(&arrangement);
        check(i2 == 32u && arrangement.entry[0].bars == 4u && arrangement.entry[2].bars == 8u,
              "cards: all empty: the chain as the FORM makes it (SHORT 32 bars: INTRO 4, DROP 8)");
        sec_card[AR_INTRO] = (sec_card_t){.len = 2, .who = 1u};                         /* 8 bars; BASS on */
        sec_card[AR_DROP] = (sec_card_t){.len = 1, .who = (2u << 2) | (3u << 4), .loop = 2, .style = BS_BRKCORE + 1, .bass = 3};
                                                                                         /* 4 bars; CHORDS off, CHOP under water;
                                                                                          * loop B; BRKCORE; BASS preset 2 */
        as_set(0, 0, 0);
        check(arrangement.entry[0].scene == AR_INTRO && arrangement.entry[0].bars == 8u && arrangement.entry[2].scene == AR_DROP &&
              arrangement.entry[2].bars == 4u && arrangement.entry[1].bars == 4u,
              "cards: LENGTH: INTRO 8 bars, DROP 4; the others as the FORM's");
        transport_req = 1;
        for (t = 0; t < 8u * bar; t += CTL) {                                           /* (the INTRO: BASS on by its card) */
            block(0);
            if (as.role == AR_INTRO && t > bar / 4u)
                intro_bass |= sounding() & 1u;
        }
        check(intro_bass, "cards: WHO: the INTRO's BASS ON (the role leaves it out)");
        for (t = 0; t < 12u * bar && as.role != AR_DROP; t += CTL)
            block(0);
        for (t = 0; t < 3u * bar; t += CTL) {                                           /* (the DROP, past its first beat) */
            block(0);
            if (t > bar / 2u)
                late |= sounding();
        }
        ok = as.role == AR_DROP && !(late & 2u) && (late & 1u) && (late & 8u);
        check(ok, "cards: WHO: the DROP's CHORDS OFF, the BASS and the BREAK on");
        check(TBRK->p[BPP[BP_STYLE]] == BS_BRKCORE, "cards: STYLE: the DROP's BREAK is BRKCORE");
        check(TBRK->p[BPP[BP_LOOPA]] == song_keep.t[TRK_BRK].p[BPP[BP_LOOPB]] && brkp_bank_src(1),
              "cards: LOOP: the DROP's bank A plays loop B (the project's loop A not)");
        check(trk[CS_TRK].p[P_TFLT] == SEAM_SUB && (late & 4u),
              "cards: WHO: the DROP's CHOP under water (low-passed, still sounding)");
        for (k = 0, ok = 0; k < P_COUNT; k++)
            ok += trk[0].p[k] != song_keep.t[0].p[k];
        check(as_bass_mask && ok && trk[1].p[P_E0] == song_keep.t[1].p[P_E0],
              "cards: BASS: the DROP's sound is the card's preset (the BASS's parameters changed, the pads' not)");
        for (t = 0; t < 12u * bar && as.role == AR_DROP; t += CTL)
            block(0);
        for (t = 0; t < bar / 2u; t += CTL)
            block(0);
        for (k = 0, ok = 1; k < P_COUNT; k++)
            ok &= trk[0].p[k] == song_keep.t[0].p[k] || (k == P_TFLT || k == P_LEVEL || k == P_MUTE);
        check(as.role != AR_DROP && !as_bass_mask && ok && TBRK->p[BPP[BP_LOOPA]] == song_keep.t[TRK_BRK].p[BPP[BP_LOOPA]] &&
              TBRK->p[BPP[BP_STYLE]] == song_keep.t[TRK_BRK].p[BPP[BP_STYLE]] && trk[CS_TRK].p[P_TFLT] == song_keep.t[CS_TRK].p[P_TFLT],
              "cards: the next section has none of them: the loop's own sound, loop, STYLE and filter back");
        transport_req = 2;
        block(0);
        block(0);
        memset(sec_card, 0, sizeof sec_card);
        as_set(0, 0, 0);
        check(as_total(&arrangement) == 32u, "cards: emptied: the chain as the FORM's again");
    }
    transport_req = 2;
    block(0);
    transitions();
    drop_kicks();
    printf(fails ? "song: %d FAILED\n" : "song: all ok\n", fails);
    return fails != 0;
}
