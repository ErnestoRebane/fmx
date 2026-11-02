/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Felucca core types: tracks, voices, engines, parameters.
 * FMX: four tracks. trk[0..2] are the synth parts BASS, CHORDS and CHOP (each its own engine, preset,
 * parameters, voices and 64-step pattern; they share one budget of NVOICE sounding voices, voice.c);
 * trk[3] is the BREAK track (brkpart.c: loops A..D cut into slices, rearranged by BB Gen; its steps are
 * bstep_t, its generator's parameters ride in its p[], brkpart.c BPP). */
#include <stdint.h>
#define NVOICE 8                 /* voices per part, and the budget shared by all parts */
#define NPOLY 8
#define NPART 3                  /* synth parts: BASS, CHORDS, CHOP */
#define NTRK 4                   /* + the BREAK track */
#define TRK_BRK 3
enum { V_POLY, V_MONO, V_LEGATO, V_UNISON };   /* P_VOICE */
#define NSTEP 64
#define HALF_FRAMES 256          /* I2S half buffer: 5.8 ms at 44.1 kHz */
#ifndef FELUCCA_SLICE
#define FELUCCA_SLICE 0          /* the SLICE engine (eng_slice.c): kept in the tree, not built by default */
#endif
/* FMX: ANALOG (the bass) and SAMPLE (pads, chops); SLICE, when built, comes last */
#define FELUCCA_BREAK 1          /* the BREAK track (brkpart.c, ui_break.c): X0X's breakbeat player with its BB Gen generator
                                  * (fm1-x0x, after schwung-breakbeat by mestela). It plays the loops on SLICE's sources (the
                                  * decoder and the slice tables of eng_slice.c, built in with it). Always built */
#define FELUCCA_SLC 1   /* eng_slice.c's sources and tables are in the build */
#define NENGINES (2 + FELUCCA_SLICE)
#define ENGI_ANALOG 0u
#define ENGI_SAMPLE 1u
#define ENGI_SLICE 2u            /* with FELUCCA_SLICE: after SAMPLE */
#define UP_SLOTS 32u             /* user presets (upreset.c) */
#define NELEM(a) (sizeof(a) / sizeof((a)[0]))

/* ------------------------------------------------------- parameters --- */
enum {
    F_INT, F_PCT, F_BIPCT, F_TIME, F_LFOHZ, F_CUTOFF, F_DB, F_SEMI, F_ENUM, F_BPM, F_NOTE,
    F_ONOFF, F_OCT, F_STEPS,
    F_SWING,                    /* 0..100: MPC swing, 50 % (straight) .. 75 % */
    F_FILT                      /* -64..63: the DJ filter, LP <- OFF -> HP */
};

typedef struct {
    const char *label;
    uint8_t fmt;
    int16_t min, max, def;
    const char *const *names;   /* F_ENUM */
    const char *unit;           /* F_INT / F_ENUM optional unit */
} param_desc_t;

enum {                          /* per-track parameters */
    P_LEVEL,
    P_ATK, P_DEC, P_SUS, P_REL,
    P_ED_FLT, P_ED_PIT, P_ED_SHP, P_ED_FX,     /* P_ED_FX: the sound's level trim (1/2 dB), set by the presets so
                                                * every factory sound comes out as loud as the others (fx.c) */
    P_LRATE, P_LWAVE, P_LPHASE, P_LFADE,
    P_LD_PIT, P_LD_FLT, P_LD_SHP, P_LD_AMP,
    P_X0, P_X1, P_X2, P_X3,                    /* FMX: a track's generator settings, where SLOOP's arpeggiator's were (the
                                                * arp is gone; the ROLL stays): the CHOP track's SLICE (chopgen.c) */
    P_X4, P_X5, P_X6, P_X7,
    P_ROOT, P_SCALE, P_QUANT, P_TRANS,
    P_SLEN, P_SDIV, P_SSWING, P_SGATE,
    P_DIST, P_CHOR, P_DLY, P_REV,
    P_VOICE, P_GLIDE, P_PAN, P_MUTE,
    P_GLMODE, P_PRIO, P_ALLOC, P_DETUNE,
    P_SLCR, P_SLPAT, P_SLRATE, P_SLDEPTH,      /* SLICER insert (slicer.c); new common parameters go just
                                                * before P_E0 (user presets and projects map by count) */
    P_CHORD,                                   /* chord mode: one key plays a chord of the scale (seq.c) */
    P_TFLT,                                    /* SLOOP 2.4: the track's filter, < 0 low-pass, > 0 high-pass (fx.c) */
    P_STRUM,                                   /* SLOOP 2.4: a chord's notes one after the other, ms each (> 0 down, < 0 up) */
    P_VLEAD,                                   /* SLOOP 2.4: chord mode, each chord voiced nearest the last (seq.c) */
    P_E0, P_E1, P_E2, P_E3, P_E4, P_E5, P_E6, P_E7,
    P_COUNT
};

enum {                          /* global parameters; FMX: the ones a project keeps first (G_NPROJ) */
    G_BPM, G_SWING, G_CLOCK, G_TUNE,
    G_DTIME, G_DFDBK, G_DCOLOR, G_DMIX,
    G_RSIZE, G_RDAMP, G_CRATE, G_CDEPTH,
    G_DUST, G_DUCK, G_FILT,     /* the master bus: lo-fi / vinyl, the kick ducking the parts, the DJ filter (fx.c) */
    G_ROLL,                     /* note repeat rate (ARP + key, seq.c) */
    G_NPROJ,                    /* (the rest: settings of the FM-1 and actions, not a project's) */
    G_MIDI = G_NPROJ, G_SYNC, G_ROUTE, G_INFO,
    G_SLOT, G_NAME, G_LOAD, G_SAVE,
    G_ENGSEL, G_ENGGO,          /* no page (the ENGINE page is gone); a SET of G_ENGSEL switches the engine (editor) */
    G_CLRSEQ, G_INITSND,
    G_NEWPRJ,                   /* TOOLS > NEW: a new project (GO) */
    G_SPACE,                    /* FMX: SPACE, the DECK's KNOB 4: the SPU reverb's input (64 = as before); a project's,
                                 * kept in project_t.space (no room in g[]: the format stays) */
    G_COUNT
};

/* ----------------------------------------------------------- voices --- */
typedef struct {
    uint8_t note, vel, gate, active;
    uint8_t stage;               /* env: 0 off, 1 attack, 2 decay/sustain, 3 release, 4 fading out (given up) */
    uint8_t kill;                /* stage 4: blocks of the fade left */
    uint8_t pitch_frac;          /* the glide between 1/16 semitones: pitch_cur + pitch_frac / 256 */
    int16_t penv;                /* the pitch envelope (ENV > PIT): Q15, a fast fall from the note's start */
    int16_t dive;                /* AUTO SONG's sub drop (bass_dive): Q15 of an octave above the note, falling */
    int32_t env;                 /* Q24 */
    int32_t env_out;             /* last control-rate amplitude, Q15 */
    int32_t pitch16, pitch_cur;  /* 1/16 semitone, with glide */
    int32_t gstep;               /* glide: 1/4096 semitone per control tick, 0 = RATE (GLIDE: the time an octave takes) */
    int32_t fine;                /* unison detune: phase increment * (1 + fine / 4096) */
    uint32_t ph[3];
    int32_t s[8];                /* engine state (filters, envs) */
    uint32_t age;
} voice_t;

typedef struct {                 /* per-voice control-rate modulation, computed in voice.c */
    uint32_t inc;                /* phase increment of the base pitch */
    int32_t pitch16;
    int32_t amp0, amp1;          /* Q15 ramp over the block */
    int32_t cutoff;              /* 0..127 << 8 */
    int32_t shape;               /* 0..127 << 8 */
    int32_t envq15;              /* env value (for engines that use it as a mod source) */
    int32_t fine;                /* the residual below 1/16 semitone in inc: unison detune, TUNE, the glide's
                                  * fraction (1/4096); FM6 reads it (the others take inc) */
} vmod_t;

typedef struct {
    const char *name;
    int8_t e[8];                 /* P_E0..P_E7 (signed: an interval below the note; every value fits) */
    uint8_t env[4];              /* ATK DEC SUS REL */
    int8_t fenv;                 /* ENV -> FILTER amount (-64..63) */
    uint8_t mono;                /* 1 = MONO (bass / lead), 0 = POLY */
    /* the rest of the patch; each value is stored + 1, 0 = the default */
    uint8_t fx[4];               /* DIST, CHORUS, DELAY, REVERB sends */
    uint8_t pat;                 /* sequence pattern (PATTERNS[pat - 1]), loaded only into an empty sequencer */
    int8_t x[8];                 /* more of the patch: up to 4 (track parameter id + 1, value) pairs, 0 = none
                                  * (glide, pitch / LFO modulation, voice mode...: preset_extras) */
} preset_t;
#define FX(d, c, dl, r) .fx = {(d) + 1, (c) + 1, (dl) + 1, (r) + 1}
#define PAT(n) .pat = (n)
#define XP(...) .x = {__VA_ARGS__}  /* XP(P_GLIDE + 1, 30, P_ED_PIT + 1, 8): parameter id + 1, value */

struct track;
typedef struct {
    const char *name;            /* "VA" */
    const char *page_title[2];
    param_desc_t edit[8];        /* P_E0..P_E7 */
    const preset_t *presets;
    uint8_t npresets;
    int8_t fil_page;             /* EDIT page that holds the filter, -1 = none */
    void (*note_on)(struct track *t, voice_t *v);
    void (*render)(struct track *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m);
    uint16_t color;              /* accent colour of the engine (RGB565) */
    uint8_t macro[4];            /* HOME: the four parameters on KNOB 1..4 */
    uint8_t poly;                /* voice cap for POLY and UNISON, 0 = NVOICE */
    /* optional (0 = none): the voice amplitude instead of the ADSR curve, once per control tick;
     * gets the ADSR value (Q15, env_tick already ran: it still gates the voice), returns Q15 */
    int32_t (*amp)(struct track *t, voice_t *v, int32_t adsr);
    /* optional: a mode-dependent descriptor of EDIT k (the same range and default as edit[k],
     * another label / value names), 0 = edit[k] */
    const param_desc_t *(*desc)(const struct track *t, uint32_t k);
    /* optional: once per block and part, before its voices (also with no voice sounding) */
    void (*block)(struct track *t);
    /* 1 = the engine's own envelopes are the voice's amplitude (FM6): voice.c renders it at 1.0 (no ADSR, no
     * velocity; LFO -> AMP and the fades still apply; ENV is 0) and the voice ends when done() says so (once
     * per control tick, before the render), not at the end of the ADSR's release */
    uint8_t ownenv;
    int (*done)(struct track *t, voice_t *v);
} engine_t;

/* ------------------------------------------------------------ track --- */
enum { ST_NOTE, ST_TIE, ST_REST };
#define SF_ACCENT 1u
#define SF_SLIDE 2u
/* the dynamics of a note or a drum hit (2 bits each in the steps): played without velocity
 * (OCT- / OCT+ held on the drum track, the step page) or from a MIDI velocity */
enum { LV_NORM, LV_GHOST, LV_SOFT, LV_HARD };
typedef struct {                 /* a step of a synth part (10 bytes): up to 4 notes (POLY), time, accent, slide */
    uint8_t note[4];
    uint8_t n;                   /* notes in use, 0 = empty */
    uint8_t time;                /* ST_NOTE / ST_TIE / ST_REST */
    uint8_t flags;               /* SF_ACCENT | SF_SLIDE */
    uint8_t vel;
    uint8_t lvl;                 /* 2 bits per note (note k: bits 2k..2k+1): LV_* */
    uint8_t rat;                 /* 2 bits per note: the hits it plays in its step - 1 (ratchet x1..x4) */
} step_t;
#define BRK_SLICES 8u            /* a loop's slices (brkpart.c) */
typedef struct {                 /* a step of the BREAK track (10 bytes, the size of a step_t) */
    uint8_t on;                  /* 1 = it sounds: the generator's slice, or its own */
    uint8_t slice;               /* its own slice (a pin): 0 = the generator's, 1..BRK_SLICES */
    uint8_t lvl;                 /* LV_* */
    uint8_t rat;                 /* retrigger: 0 = the generator's, 1..4 = x2, x3, x4, x8 */
    int8_t pitch;                /* semitones on top of PTCH */
    uint8_t flags;               /* BS_* */
    uint8_t rsv[4];
} bstep_t;
#define BS_REV 1u                /* the step plays its slice backwards */
_Static_assert(sizeof(step_t) == 10 && sizeof(bstep_t) == 10, "a step is 10 bytes on every track");
/* SLOOP 2.4: per-step nudge (micro timing) and parameter locks (seq.c seq_tick / lock_step) */
#define MICRO_MIN (-32)          /* a step's nudge in 1/64 of its length: half a step early .. */
#define MICRO_MAX 31             /* .. just under half a step late (0 = on the grid) */
#define NLOCK 20                 /* parameter locks per track (several may share a step: other params; SPB3 had 24) */
typedef struct {                 /* a lock: on step `step` the track's p[param] is `val` (4 bytes) */
    uint8_t step;                /* 0xFF = a free slot */
    uint8_t param;               /* P_* (a lockable one: p_lockable) */
    int16_t val;
} plock_t;
#define LOCK_FREE 0xFFu
/* a step's condition (SLOOP 2.4 fill, GLO + key 9 / 10): plays always, only during a fill, never during one */
enum { FC_NORM, FC_FILL, FC_NOFILL };     /* (3: as FC_NORM) */

typedef struct track {
    int16_t p[P_COUNT];
    uint8_t engine, preset;      /* engine: what the audio ISR renders */
    uint8_t eng_req;             /* engine the UI asked for (the ISR switches at a block start) */
    uint8_t user;                /* user preset slot + 1 the sound came from (UI), 0 = none */
    voice_t v[NVOICE];
    /* LFO */
    uint32_t lfo_ph;
    int32_t lfo_val;             /* Q15 */
    int32_t lfo_fade;            /* Q15 ramp after note-on */
    uint32_t lfo_rnd;
    /* sequencer: synth parts step[], the BREAK track bstep[] */
    union {
        step_t step[NSTEP];
        bstep_t bstep[NSTEP];
    };
    int8_t micro[NSTEP];         /* each step's nudge, MICRO_MIN..MICRO_MAX (1/64 of a step; - early, + late) */
    plock_t lock[NLOCK];         /* parameter locks, unsorted (step LOCK_FREE = a free slot) */
    uint8_t fill[NSTEP / 4];     /* each step's condition, 2 bits (seq.c step_fill): FC_* */
    /* the locks in force (seq.c lock_step): the params overridden now, what they were, what the lock set */
    uint8_t lk_n;
    uint8_t lk_param[NLOCK];
    int16_t lk_base[NLOCK];
    int16_t lk_set[NLOCK];
    uint32_t seq_abs;            /* the step of the transport grid last played (seq.c trk_grid), SEQ_NONE */
    uint16_t seq_idx;            /* its index in the pattern */
    uint8_t seq_den;             /* the DIV it was played on (a DIV change waits for the next step) */
    uint8_t seq_notes[4];        /* sounding seq notes */
    uint8_t seq_n;
    uint8_t seq_hold;            /* last step slides: keep the notes until the next step */
    uint8_t seq_skip;            /* the step playing failed its fill condition: nothing of it sounds */
    uint8_t slide_glide;         /* next legato note glides (slide) */
    uint32_t seq_off;            /* units to the note-off of the step's notes */
    uint8_t seq_active;          /* any step programmed */
    uint8_t rat_done[4];         /* ratchet hits played in this step: per note (synth) */
    uint32_t rskip_abs;          /* live recording put notes into the step about to play: */
    uint8_t rskip_n, rskip[4];   /* do not trigger them again there (they sound already) */
    /* live recording of held notes (seq.c rec_hold): the steps they are held into become TIEs */
    uint8_t rh_n, rh_note[4];    /* recorded notes still held, 0 = none */
    uint8_t rh_start;            /* the step they were recorded into */
    uint8_t rh_ties;             /* TIE steps written after it */
    uint8_t rh_last;             /* the last of them; rh_bak: what it held (an early release puts it back) */
    uint32_t rh_last_abs;        /* (its step of the transport grid) */
    step_t rh_bak;
    uint32_t pass;               /* loops played since PLAY (a recording pass: one undo) */
    /* mono */
    uint8_t mono_stack[8];
    uint8_t nmono;
    uint8_t mono_note;           /* note the MONO / LEGATO / UNISON voice(s) play, 0 = none */
    uint8_t rr;                  /* POLY ROTATE: next voice to try */
    /* mix runtime */
    int32_t lvl;                 /* the LEVEL gain (Q12) of the last block: a change is ramped (fx.c) */
    int32_t peak;
    int32_t dist_hp, dist_lp1, dist_lp2;   /* DIST insert state (fx.c) */
    int32_t att;                 /* mute / solo fade: attenuation, Q15 (0 = heard; fx.c mix_part, drums_mix) */
    uint8_t dist_on;             /* DIST was on in the last block (its states restart when it comes on) */
    uint8_t tail;                /* blocks to mix after the last voice (the DIST tail) */
    /* engine switch (voice.c engine_block): the old engine's voices fade out, then it switches */
    uint8_t xf_on, xf;           /* fading; blocks of the fade still to render */
    int16_t pe_old[8];           /* P_E0..P_E7 of the sounding engine: the fade renders with these */
    uint8_t xp_n, xp_note[4], xp_vel[4];   /* note-ons during the fade, played on the new engine */
} track_t;

typedef struct {
    int16_t g[G_COUNT];
    uint8_t playing, seq_mode;
    uint8_t rec;                 /* live recording armed: bit per track */
    uint8_t sel;                 /* selected track 0..NTRK-1: keys, pages, editor */
    int8_t octave;
    volatile uint8_t solo;       /* bit per track soloed (GLO + key), 0 = none: the others are silent */
    uint32_t tick;               /* sub-blocks since play */
    uint32_t cpu_q8;             /* audio ISR load, 1/256 */
    uint32_t master_q12;
    int32_t batt_raw;            /* smoothed ADC ch3 (battery divider), 0 = not read yet */
} song_t;

static track_t trk[NTRK];        /* the instrument: three synth parts and the BREAK track */
static song_t song;

/* The transport clock (seq.c runs it). One unit = one sample at 1 BPM: a beat is BEAT_U units at any
 * tempo, and every division of it (1/4 .. 1/64, the triplets) is a whole number of units. The steps
 * of every track, the arp, the rolls, the click, the SLICER and the song arranger (which counts the
 * same units) all follow it: no rounding, no drift between them, a tempo change moves them together.
 * clk_beat: beats since PLAY, clk_pos: units into that beat, at the start of the block being rendered
 * (events_block advances it by CTL x BPM after the block's events). */
#ifndef FS
#define FS 44100                 /* (as build/gen/felucca_tables.h: tools/gen_tables.py) */
#endif
#define BEAT_U ((uint32_t)FS * 60u)
static volatile uint32_t clk_beat, clk_pos;
static const uint8_t DIV_DEN[6] = {1, 2, 4, 8, 3, 6};    /* N_DIV 0..5: beats = 1 / DEN */
#define NDIV_SHORT 6u            /* the divisions inside a beat (N_DIV, the arp's RATE) */
#define NDIV_STEP 9u             /* N_SDIV: + 1/2 note, a bar, two bars (whole beats: DIV_BEATS) */
#define NDIV_DLY 8u              /* N_DLY: + 1/8 and 1/16 dotted */
static const uint8_t DIV_BEATS[3] = {2, 4, 8};
/* a step's length in clock units (N_SDIV order; the arp's RATE uses 0..5) */
static uint32_t div_units(uint32_t div)
{
    return div < NDIV_SHORT ? BEAT_U / DIV_DEN[div] : BEAT_U * DIV_BEATS[(div - NDIV_SHORT) % 3u];
}
/* the delay's TIME in clock units (N_DLY order) */
static uint32_t dly_units(uint32_t d)
{
    return d < NDIV_SHORT ? BEAT_U / DIV_DEN[d] : d == NDIV_SHORT ? BEAT_U * 3u / 4u : BEAT_U * 3u / 8u;
}
/* length of one step (N_SDIV order) in samples at the song tempo (rounded down) */
static uint32_t div_samples(uint32_t div) { return div_units(div) / (uint32_t)song.g[G_BPM]; }
/* length of the delay's TIME (N_DLY order) in samples at the song tempo (rounded down) */
static uint32_t dly_samples(uint32_t d) { return dly_units(d) / (uint32_t)song.g[G_BPM]; }
#define TSEL (&trk[song.sel])    /* the selected track */
#define TBRK (&trk[TRK_BRK])
static int is_brk(const track_t *t) { return t == TBRK; }
/* the moves (punch.c, the ISR): the tracks they take out (AUTO SONG's sections, LIVE's GAP), the reverb they open (VERB, WASH) */
static uint8_t perf_mute, perf_verb;
static volatile uint16_t as_keys;       /* FMX AUTO SONG (arranger_scene.c): the moves (punch.c) a section holds, */
static volatile uint8_t as_mute;        /* ... the tracks it leaves out (bit k track k), */
static volatile uint16_t as_fade = 4096;   /* ... the mix's level (Q12: the OUTRO's fade, the gap before a DROP), */
static volatile uint8_t fx_hush;        /* ... the effects' tails emptied (fx.c fx_buses: the gap, the tape stop), */
static volatile uint8_t bass_dive;      /* ... the BASS's next note dives in from an octave up (voice.c: a drop) */
static volatile uint8_t as_throw;       /* ... the drop's first hit thrown (lfx.c: bits 0..3 DUB VERB's tracks, 4..7
                                         * ECHO THROW's) */
static uint16_t dive_k = 395;           /* the sub drop's fall a control tick (Q15): 1/e in ~60 ms (voice.c) */
/* silent: MUTE, or another track is soloed, or a move has it out */
static int trk_silent(const track_t *t)
{
    uint32_t i = (uint32_t)(t - trk);
    return t->p[P_MUTE] || (song.solo && !((song.solo >> i) & 1u)) || ((perf_mute >> i) & 1u);
}
#define RING_PUBLISH() __asm__ volatile("" ::: "memory")   /* slot store before the index update */
static volatile uint32_t fm1_ms;  /* milliseconds since boot (TIMER4-based, TIMER5 ISR in main.c) */
/* Two early failed boots -> USB recovery; recovery reset -> mask-ROM UBOOT. */
#include "bootguard.h"
bootguard_t bootguard __attribute__((section(".noinit")));
