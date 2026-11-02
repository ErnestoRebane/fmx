/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Keyboard and its layers, scale and chords, arpeggiator, note repeat, sequencer, recording and
 * transport. Runs in the audio ISR, once per CTL-sample block (events_block), and ends in
 * trk_note_on / trk_note_off (the BREAK track: brk_hit): engines never see where a note came from.
 *
 * Time: the transport clock of fx.c (clk_beat, clk_pos, in units of a sample at 1 BPM). Every track
 * reads its step from it (trk_grid): the step is the clock divided by the track's DIV, swung, modulo
 * its LEN. So the tracks, the click, the arp, the rolls and the song arranger never drift apart, a
 * tempo or DIV change plays at most one step a block, and a polymeter (any LEN) stays in phase.
 *
 * Four tracks: tracks 1..3 are synth parts (steps of up to 4 notes), track 4 the drum track (steps
 * of 16 lanes, one per white key). Each step note / lane has a level (ghost .. hard) and a ratchet
 * (x1..x4 hits in its step). The keys play the selected track; MIDI channels 1..3 play parts 1..3,
 * the DRUMS channel (GLO > DRUMS, default 10) the drum track, any other channel the selected track.
 *
 * Layers: a function button held turns the keys into something else (TE style: hold + touch):
 *   FX   the FX panel (lfx.c, as LIVE's)  EDIT  erase that note / sound (while held, as it plays)
 *   ARP  note repeat (roll) at G_ROLL     SEQ   steps 1..16 (the UI: ui_layers.c)
 *   SEL  the key of the song (the UI)     GLO   mute / solo / fill / tap tempo (the UI)
 * On the drum track OCT- / OCT+ held play (and record) ghost / hard hits. */
static const uint16_t SCALE_MASK[] = {
    0xFFF,                                   /* CHR */
    (1 << 0) | (1 << 2) | (1 << 4) | (1 << 5) | (1 << 7) | (1 << 9) | (1 << 11),   /* MAJ */
    (1 << 0) | (1 << 2) | (1 << 3) | (1 << 5) | (1 << 7) | (1 << 8) | (1 << 10),   /* MIN */
    (1 << 0) | (1 << 2) | (1 << 3) | (1 << 5) | (1 << 7) | (1 << 9) | (1 << 10),   /* DOR */
    (1 << 0) | (1 << 2) | (1 << 4) | (1 << 5) | (1 << 7) | (1 << 9) | (1 << 10),   /* MIX */
    (1 << 0) | (1 << 2) | (1 << 4) | (1 << 7) | (1 << 9),                          /* PEN */
    (1 << 0) | (1 << 3) | (1 << 5) | (1 << 7) | (1 << 10),                         /* MPEN */
    (1 << 0) | (1 << 2) | (1 << 3) | (1 << 5) | (1 << 7) | (1 << 8) | (1 << 11),   /* HARM */
    (1 << 0) | (1 << 1) | (1 << 3) | (1 << 5) | (1 << 7) | (1 << 8) | (1 << 10),   /* PHRY */
    (1 << 0) | (1 << 2) | (1 << 4) | (1 << 6) | (1 << 7) | (1 << 9) | (1 << 11),   /* LYD */
    (1 << 0) | (1 << 1) | (1 << 3) | (1 << 5) | (1 << 6) | (1 << 8) | (1 << 10),   /* LOC */
    (1 << 0) | (1 << 2) | (1 << 3) | (1 << 5) | (1 << 7) | (1 << 9) | (1 << 11),   /* MEL (ascending) */
    (1 << 0) | (1 << 3) | (1 << 5) | (1 << 6) | (1 << 7) | (1 << 10),              /* BLUES (minor) */
    (1 << 0) | (1 << 2) | (1 << 4) | (1 << 6) | (1 << 8) | (1 << 10),              /* WHOLE */
    (1 << 0) | (1 << 1) | (1 << 3) | (1 << 4) | (1 << 6) | (1 << 7) | (1 << 9) | (1 << 10), /* DIMHW */
    (1 << 0) | (1 << 2) | (1 << 3) | (1 << 5) | (1 << 6) | (1 << 8) | (1 << 9) | (1 << 11), /* DIMWH */
};
#define NSCALES (sizeof SCALE_MASK / sizeof SCALE_MASK[0])
#define SEQ_NONE 0xFFFFFFFFu

#define KB_SILENT 255u
static uint32_t kb_prev;
/* per key: what its press started, so its release ends the same (whatever the layer or track is now) */
enum { KS_NONE, KS_NOTE, KS_DRUM, KS_ROLL, KS_ERASE, KS_UI, KS_MOD, KS_LIVE, KS_LFX, KS_LFXB, KS_BANK };   /* KS_MOD: a chord modifier (2.4);
                                                         * KS_LIVE: a LIVE move key (kb_nt[k][0] its move); KS_LFX,
                                                         * KS_LFXB: LIVE's FX panel, a white key (its effect), a black
                                                         * key (1..6: a target, LATCH, CLEAR); KS_BANK: LIVE, the BREAK's
                                                         * loop A..D held (kb_nt[k][0]) */
static uint8_t kb_kind[27], kb_trk[27], kb_n[27], kb_nt[27][4], kb_root[27];   /* kb_root: a chord key's note (CHORD+) */
static uint8_t last_note = 60;
static uint8_t pen_n = 1, pen_note[4] = {60};   /* the last chord / note played: the SEQ layer writes it */
static uint8_t pen_lane;                       /* the last slice played on the BREAK track: the SEQ layer's */
static volatile uint8_t transport_req;   /* 1 start, 2 stop (from the UI); 3 start from a MIDI START */
static volatile uint8_t panic_req;       /* bit per track: release every sounding note (preset / engine change) */

static uint32_t trk_index(const track_t *t) { return (uint32_t)(t - trk); }

static uint32_t trk_midi_ch(uint32_t i)    /* MIDI channel 0..15 of track i (keys -> MIDI out) */
{
    return i;                                  /* BASS 1, CHORDS 2, CHOP 3, BREAK 4 */
}

/* MIDI OUT of what the sequencer, the arp and the rolls play (GLO > SYSTEM > MIDI = SEQ; the keys always
 * go out: key_down / key_up). Notes from a computer or the jack are never echoed (no MIDI loop). A set per
 * track of the notes sent on, so a note is ended once, and STOP or MIDI = KEYS end them all */
static uint32_t mo_set[NTRK][4];
static void seq_out_off(const track_t *t, uint32_t note)
{
    uint32_t i = trk_index(t) % NTRK;
    if (note > 127u || !(mo_set[i][note >> 5] & (1u << (note & 31u))))
        return;
    mo_set[i][note >> 5] &= ~(1u << (note & 31u));
    midi_out_event(0x08u | (0x80u | trk_midi_ch(i)) << 8 | note << 16);
}
static uint8_t mo_any;                     /* something was sent on since the last check (events_block) */
static void seq_out_on(const track_t *t, uint32_t note, uint32_t vel)
{
    uint32_t i = trk_index(t) % NTRK;
    if (!song.g[G_MIDI] || note > 127u)
        return;
    seq_out_off(t, note);                      /* played again while on: off first */
    mo_set[i][note >> 5] |= 1u << (note & 31u);
    mo_any = 1;
    midi_out_event(0x09u | (0x90u | trk_midi_ch(i)) << 8 | note << 16 | (vel ? vel & 127u : 1u) << 24);
}
static void seq_out_track_off(const track_t *t)      /* every note of the track still on */
{
    uint32_t i = trk_index(t) % NTRK, w, b;
    for (w = 0; w < 4u; w++)
        for (b = 0; mo_set[i][w]; b++)
            if (mo_set[i][w] & (1u << b)) {
                mo_set[i][w] &= ~(1u << b);
                midi_out_event(0x08u | (0x80u | trk_midi_ch(i)) << 8 | (w * 32u + b) << 16);
            }
}
static void seq_out_all_off(void)
{
    uint32_t i;
    for (i = 0; i < NTRK; i++)
        seq_out_track_off(&trk[i]);
}

static uint32_t scale_mask(const track_t *t)
{
    return SCALE_MASK[clamp(t->p[P_SCALE], 0, NSCALES - 1)];
}

/* ---------------------------------------------------------- layers --- */
enum { LY_PLAY, LY_FX, LY_ERASE, LY_ROLL, LY_STEP, LY_SCALE, LY_MIX, LY_COUNT };   /* (SAVE: a tap only, the SONG screen) */
static uint32_t ly_bit[LY_COUNT];        /* the button (fm1_in.buttons bit) of each layer: the UI sets them */
static uint32_t dyn_bit[2];              /* OCT- / OCT+: ghost / hard on the drum track */
/* a layer locked open (its button held + HOME tapped: ui_input.c), LY_PLAY = none: the keys and knobs
 * stay in it with the button let go, as if it were held */
static volatile uint8_t ly_lock = LY_PLAY;
static uint32_t layer_buttons(void) { return fm1_in.buttons | (ly_lock != LY_PLAY ? ly_bit[ly_lock % LY_COUNT] : 0u); }
/* the layer the keys are in: the held function button (FX, EDIT, ARP, SEQ, SEL, GLO in that order),
 * else the locked one */
static uint32_t layer_now(void)
{
    uint32_t b = layer_buttons(), l;
    for (l = LY_FX; l < LY_COUNT; l++)
        if (b & ly_bit[l])
            return l;
    return LY_PLAY;
}
static volatile uint8_t kb_brk;                       /* the BREAK screen shown (ui_break.c): its keys are steps / slices
                                                       * (KB_BRK events of lk_q) and play nothing themselves */
#define KB_BRK LY_COUNT
static volatile uint8_t kb_chd;                       /* the CHORDS screen shown (ui_chord.c): its keys are nodes / steps */
#define KB_CHD (LY_COUNT + 1u)
static volatile uint8_t kb_chp;                       /* the CHOP screen shown (ui_chop.c): a white key plays its hit (a note
                                                       * of the CHOP track, recorded as any), a black key is the screen's */
#define KB_CHP (LY_COUNT + 2u)
static int cs_slice(void);                             /* (chopgen.c) */
static uint32_t cs_mode(void);
static uint32_t cs_key_note(uint32_t w);
static volatile uint8_t cs_hand;                       /* the CHOP track's keys held by hand (SLICE's bed waits for them) */
static void cs_hit(uint32_t note);
static void cs_key(uint32_t note);
static volatile uint8_t kb_bss;                       /* the BASS screen shown (ui_bass.c): its black keys are its (WRITE, NEW) */
#define KB_BSS (LY_COUNT + 3u)
static uint8_t live_on = 1;                            /* LIVE shown (ui_live.c; settings bit 27 set = STUDIO) */
static volatile uint8_t kb_live;                      /* the LIVE screen (ui_live.c): white keys 1..12 its (sections, NEXT,
                                                       * mutes: KB_LIVE events), 13..16 its moves, black keys play the
                                                       * focus track; no layer button counts */
#define KB_LIVE (LY_COUNT + 4u)
/* LIVE's moves on keys 13..16: GAP (live_gap), WASH, ROLL (ROLL16), REWIND (punch.c) */
static const int8_t LIVE_MOVE[4] = {-1, PA_WASH, PA_ROLL16, PA_REWIND};
static const uint8_t LIVE_BLACK[11] = {1, 3, 5, 8, 10, 13, 15, 17, 20, 22, 25};   /* the black keys, low to high */
static const uint8_t LIVE_WK[7] = {0, 2, 4, 6, 7, 9, 11};                       /* white key w of an octave: F G A B C D E */
/* the white key (0..15) of key k, -1 = a black key */
static int white_of_key(uint32_t k)
{
    static const int8_t W[12] = {0, -1, 1, -1, 2, -1, 3, 4, -1, 5, -1, 6};   /* from F */
    int32_t i = W[k % 12u];
    return i < 0 ? -1 : (int)((k / 12u) * 7u + (uint32_t)i);
}
/* the keys of the layers the UI handles (steps, key, mix): key k down / up, in order */
#define LKQ 16u
static volatile uint16_t lk_q[LKQ];
static volatile uint32_t lk_w, lk_r;
static void lk_push(uint32_t layer, uint32_t k, uint32_t down)
{
    if (lk_w - lk_r < LKQ) {
        lk_q[lk_w % LKQ] = (uint16_t)(layer << 8 | down << 7 | k);
        RING_PUBLISH();
        lk_w++;
    }
}

/* ------------------------------------------------------------- keys --- */
/* key k -> note on a synth part (KB_SILENT: none). WHITE (and chord mode): the white keys walk the
 * scale from C4 = the root, the black keys are silent; SNAP: every key, rounded down into the scale */
static uint32_t kb_map(const track_t *t, uint32_t k)
{
    static const int8_t DEGREE[12] = {0, -1, 1, -1, 2, 3, -1, 4, -1, 5, -1, 6};
    int32_t n = 53 + (int32_t)k;
    if (is_brk(t))
        return 36u + brk_slice_of_key(k);
    if (t == &trk[2] && kb_chp) {                  /* the CHOP screen: white key w, hit w of the kit (none past it);
                                                    * INST: degree w of the session's scale from C4 */
        uint32_t base, nh;
        int w = white_of_key(k);
        if (w >= 0 && cs_mode() == 2u)
            return cs_key_note((uint32_t)w);
        if (w < 0 || ENGINES[t->eng_req % NENGINES] != &ENG_SAMPLE || !smp_kit((uint32_t)t->p[P_E0] % SMP_NALL, &base, &nh) ||
            (uint32_t)w >= nh)
            return KB_SILENT;
        return base + (uint32_t)w;
    }
#if FELUCCA_SLICE
    if (ENGINES[t->eng_req % NENGINES] == &ENG_SLICE)   /* SLICE: lowest key = slice 0 (C4 + ROOT), no scale */
        return (uint32_t)clamp(SLC_BASE + t->p[P_ROOT] + 12 * song.octave + (int32_t)k, 0, 127);
#endif
    if (t->p[P_QUANT] == 1 && !t->p[P_CHORD]) {  /* SNAP: every key, rounded down to the scale (the old ON) */
        uint32_t mask = scale_mask(t), guard = 12;
        n += 12 * song.octave + t->p[P_TRANS];
        while (guard-- && !((mask >> (uint32_t)((n - t->p[P_ROOT] + 120) % 12)) & 1u))
            n--;
        return (uint32_t)clamp(n, 0, 127);
    }
    if (t->p[P_QUANT] == 2 || t->p[P_CHORD]) {   /* WHITE: white keys walk the scale, black keys are silent */
        uint32_t mask = t->p[P_CHORD] && !t->p[P_SCALE] ? SCALE_MASK[2] : scale_mask(t), i;
        int32_t count = 0, degree = DEGREE[n % 12], oct;
        if (degree < 0)
            return KB_SILENT;
        /* C4 is the root. Walk scale degrees on successive white keys, including
         * below C4; scales with 5, 6, 8 or 12 notes still have no duplicated degrees. */
        degree += (n / 12 - 5) * 7;
        for (i = 0; i < 12u; i++)
            count += (mask >> i) & 1u;
        oct = degree / count;
        degree %= count;
        if (degree < 0) {
            degree += count;
            oct--;
        }
        for (i = 0; i < 12u; i++)
            if ((mask >> i) & 1u) {
                if (!degree)
                    break;
                degree--;
            }
        n = 60 + t->p[P_ROOT] + 12 * oct + (int32_t)i;
    }
    return (uint32_t)clamp(n + 12 * song.octave + t->p[P_TRANS], 0, 127);
}

/* chord mode (P_CHORD): the chord of the scale built on note n (in the scale; CHR: minor), into c[];
 * the notes it holds (<= 4, the most a step keeps) */
static const int8_t CHORD_DEG[6][4] = {
    {0, -1, -1, -1},                     /* OFF */
    {0, 2, 4, -1},                       /* TRIAD: 1 3 5 */
    {0, 2, 4, 6},                        /* 7TH: 1 3 5 7 */
    {0, 2, 6, 8},                        /* 9TH: 1 3 7 9 (the lo-fi / R&B voicing) */
    {0, 3, 4, -1},                       /* SUS4: 1 4 5 */
    {0, -1, -1, -1},                     /* POWER: 1 5 8 (semitones, below) */
};
static uint32_t chord_notes(const track_t *t, uint32_t n, uint8_t *c)
{
    uint32_t type = (uint32_t)clamp(t->p[P_CHORD], 0, 5), mask = t->p[P_SCALE] ? scale_mask(t) : SCALE_MASK[2];
    uint32_t k = 0, j;
    if (type == 5u) {
        static const uint8_t PW[3] = {0, 7, 12};
        for (j = 0; j < 3u; j++)
            if (n + PW[j] < 128u)
                c[k++] = (uint8_t)(n + PW[j]);
        return k;
    }
    for (j = 0; j < 4u && CHORD_DEG[type][j] >= 0; j++) {
        int32_t m = (int32_t)n, d = CHORD_DEG[type][j], guard = 48;
        while (d > 0 && guard--) {                       /* d scale degrees up */
            m++;
            if ((mask >> (uint32_t)((m - t->p[P_ROOT] + 120) % 12)) & 1u)
                d--;
        }
        if (m < 128)
            c[k++] = (uint8_t)m;
    }
    return k;
}

/* CHORD+ (2.4, after HiChord / minichord): in chord mode the black keys are modifiers. Held while a white key
 * plays (or pressed while it is held: the chord changes under the finger), they change its chord: F# flips its
 * third (major <-> minor), G# adds the 7th, A# makes it sus4, C# adds the 9th, D# inverts it (its lowest note an
 * octave up); several at once combine. P_VLEAD ON voices each chord nearest the last one played on the track. */
enum { CM_MINOR = 1, CM_SEVEN = 2, CM_SUS4 = 4, CM_NINE = 8, CM_INV = 16 };
static uint32_t chord_mod_of_key(uint32_t k)            /* key k's modifier (0: a white key) */
{
    switch ((53u + k) % 12u) {
    case 6: return CM_MINOR;                            /* F# */
    case 8: return CM_SEVEN;                            /* G# */
    case 10: return CM_SUS4;                            /* A# */
    case 1: return CM_NINE;                             /* C# */
    case 3: return CM_INV;                              /* D# */
    default: return 0;
    }
}
static uint8_t vl_prev[NPART][4], vl_n[NPART];          /* the last chord played on each part (voice leading) */
static uint32_t scale_up(const track_t *t, uint32_t n, uint32_t deg)   /* deg scale degrees above n */
{
    uint32_t mask = t->p[P_SCALE] ? scale_mask(t) : SCALE_MASK[2];
    int32_t m = (int32_t)n, guard = 48;
    while (deg > 0 && guard--) {
        m++;
        if ((mask >> (uint32_t)((m - t->p[P_ROOT] + 120) % 12)) & 1u)
            deg--;
    }
    return (uint32_t)m;
}
static void sort_notes(uint8_t *c, uint32_t n)
{
    uint32_t a, b;
    for (a = 1; a < n; a++)
        for (b = a; b > 0 && c[b - 1u] > c[b]; b--) {
            uint8_t x = c[b]; c[b] = c[b - 1u]; c[b - 1u] = x;
        }
}
/* the chord of white key n with the modifiers held, voiced (VLEAD); its notes (<= 4) into c[] */
static uint32_t chord_play_notes(track_t *t, uint32_t n, uint32_t mods, uint8_t *c)
{
    uint32_t k = chord_notes(t, n, c), j, type = (uint32_t)clamp(t->p[P_CHORD], 0, 5), part = trk_index(t);
    if (type != 5u && k >= 2u && mods) {                /* (POWER: the inversion only) */
        uint32_t third = scale_up(t, n, 2u);
        for (j = 0; j < k; j++) {
            if (c[j] == third && (mods & CM_SUS4))
                c[j] = (uint8_t)scale_up(t, n, 3u);     /* the 4th instead of the 3rd */
            else if (c[j] == third && (mods & CM_MINOR))
                c[j] = (uint8_t)(c[j] - n == 4u ? c[j] - 1u : c[j] - n == 3u ? c[j] + 1u : c[j]);
        }
        if ((mods & CM_SEVEN) && k < 4u) {
            uint32_t s7 = scale_up(t, n, 6u);
            for (j = 0; j < k && c[j] != s7; j++)
                ;
            if (j == k && s7 < 128u)
                c[k++] = (uint8_t)s7;
        }
        if (mods & CM_NINE) {
            uint32_t s9 = scale_up(t, n, 8u), s5 = scale_up(t, n, 4u);
            for (j = 0; j < k && c[j] != s9; j++)
                ;
            if (j == k && s9 < 128u) {
                if (k < 4u) {
                    c[k++] = (uint8_t)s9;
                } else {                                /* four already: the 9th for the 5th */
                    for (j = 0; j < k && c[j] != s5; j++)
                        ;
                    c[j < k ? j : k - 1u] = (uint8_t)s9;
                }
            }
        }
    }
    sort_notes(c, k);
    if (t->p[P_VLEAD] && part < NPART && vl_n[part] && k) {   /* the inversion and octave nearest the last chord */
        uint8_t best[4], cand[4];
        int32_t bcost = 0x7FFFFFFF, inv, oct;
        for (inv = 0; inv < (int32_t)k; inv++)
            for (oct = -1; oct <= 1; oct++) {
                int32_t cost = 0, ok = 1;
                for (j = 0; j < k; j++) {
                    int32_t v = c[j] + 12 * oct + (j < (uint32_t)inv ? 12 : 0);
                    if (v < 24 || v > 108)
                        ok = 0;
                    cand[j] = (uint8_t)(v < 0 ? 0 : v > 127 ? 127 : v);
                }
                if (!ok)
                    continue;
                sort_notes(cand, k);
                for (j = 0; j < k; j++) {
                    int32_t d = (int32_t)cand[j] - vl_prev[part][j < vl_n[part] ? j : vl_n[part] - 1u];
                    cost += d < 0 ? -d : d;
                }
                if (cost < bcost) {
                    bcost = cost;
                    memcpy(best, cand, k);
                }
            }
        if (bcost != 0x7FFFFFFF)
            memcpy(c, best, k);
    }
    if ((mods & CM_INV) && k >= 2u && c[0] + 12u < 128u) {   /* an inversion: the lowest an octave up */
        c[0] = (uint8_t)(c[0] + 12u);
        sort_notes(c, k);
    }
    if (part < NPART) {
        memcpy(vl_prev[part], c, k);
        vl_n[part] = (uint8_t)k;
    }
    return k;
}

/* ------------------------------------------------------------- grid --- */
/* units an odd step starts late: the track's + the global SWING (MPC: 0 = 50 %, 100 = 75 %) */
static uint32_t swing_units(int32_t pct, uint32_t u)
{
    return (uint32_t)clamp(pct, 0, 100) * u / 200u;
}
/* a position on a grid of steps u units long, odd steps sw late: the step, units into it, its length */
static uint32_t grid_swing(uint32_t abs, uint32_t frac, uint32_t u, uint32_t sw, uint32_t *into, uint32_t *len)
{
    if (abs & 1u) {                                      /* an odd step: sw late */
        if (frac < sw) {
            abs--;
            frac += u;
            *len = u + sw;
        } else {
            frac -= sw;
            *len = u - sw;
        }
    } else {
        *len = u + sw;
    }
    *into = frac;
    return abs;
}
/* the clock on a grid of den steps a beat (the rolls: ROLL_DEN) */
static uint32_t grid_den(uint32_t den, uint32_t sw, uint32_t *into, uint32_t *len)
{
    uint32_t u = BEAT_U / den;
    return grid_swing(clk_beat * den + clk_pos / u, clk_pos % u, u, sw, into, len);
}
/* the clock on the grid of a division (N_SDIV: steps inside a beat, or of 2, 4, 8 whole beats) */
static uint32_t grid_at(uint32_t div, uint32_t sw, uint32_t *into, uint32_t *len)
{
    uint32_t m;
    if (div < NDIV_SHORT)
        return grid_den(DIV_DEN[div], sw, into, len);
    m = DIV_BEATS[(div - NDIV_SHORT) % 3u];              /* whole beats: the step from the beat count */
    return grid_swing(clk_beat / m, (clk_beat % m) * BEAT_U + clk_pos, BEAT_U * m, sw, into, len);
}
/* swing is for the straight grids inside a beat: off on the triplet grids (on 8T the odd steps would
 * change from one beat to the next, as on most machines) and on steps of whole beats */
static uint32_t swings(uint32_t div) { return div < 4u; }
static uint32_t trk_div(const track_t *t) { return (uint32_t)t->p[P_SDIV] % NDIV_STEP; }
static uint32_t trk_grid(const track_t *t, uint32_t *into, uint32_t *len)
{
    uint32_t div = trk_div(t);
    return grid_at(div, swings(div) ? swing_units(t->p[P_SSWING] + song.g[G_SWING], div_units(div)) : 0u, into, len);
}
static uint32_t trk_len(const track_t *t) { return t->p[P_SLEN] > 0 ? (uint32_t)t->p[P_SLEN] : 1u; }

/* ------------------------------------------------------------- undo --- */
/* One step back (and forward again) for the pattern of one track: what it was before the last
 * recording pass, erase, step edit, tool or clear (a session: one mark). EDIT + OCT- / OCT+. */
static struct {
    uint8_t valid, undone, trk;
    int16_t len;
    uint32_t sess;
    step_t st[NSTEP];
} undo;
static uint32_t undo_sess = 1;           /* UI sessions (seq.c: recording passes use the track's pass) */
static void undo_mark(const track_t *t, uint32_t sess)
{
    uint32_t i = trk_index(t);
    if (undo.valid && !undo.undone && undo.trk == i && undo.sess == sess)
        return;                                          /* (this session is marked already) */
    memcpy(undo.st, t->step, sizeof undo.st);
    undo.len = t->p[P_SLEN];
    undo.trk = (uint8_t)i;
    undo.sess = sess;
    undo.valid = 1;
    undo.undone = 0;
}
#define UNDO_REC(t) (((t)->pass << 2) | 1u)      /* a recording pass of track t */
static uint32_t undo_erase_sess;

/* ------------------------------------------------------- recording --- */
/* key to ear, in samples: the key's debounce (~3 ms) and the audio out buffer (HALF_FRAMES to
 * 2 x HALF_FRAMES, ~9 ms on average). A note played in time with what the player hears reaches
 * the sequencer this much later than the sound it was played to: recording takes it back. */
#define REC_LAT 512u

/* the step a note played now goes into (as heard: REC_LAT earlier): the one playing, or the next one
 * when it is past the middle of the playing one; *later: it has not played yet (it must not sound twice) */
static uint32_t rec_target(const track_t *t, uint32_t *later)
{
    uint32_t into, slen, abs = trk_grid(t, &into, &slen), half = slen / 2u, lat = REC_LAT * (uint32_t)song.g[G_BPM];
    if (into > half + (lat < half ? lat : half))
        abs++;
    *later = abs != t->seq_abs;
    return abs;
}

/* note into synth step idx (overdub: a step that holds notes gets this one added, a chord of up to 4;
 * when full, the last note is replaced; MONO / LEGATO / UNISON parts keep one note per step) */
static void step_add(track_t *t, uint32_t idx, uint32_t note, uint32_t vel, uint32_t lvl, uint32_t rat)
{
    step_t *s = &t->step[idx];
    uint32_t k;
    if (s->time != ST_NOTE || !s->n || t->p[P_VOICE] != V_POLY) {
        s->n = 0;                                   /* a fresh step */
        s->flags = 0;
        s->vel = 0;
        s->lvl = 0;
        s->rat = 0;
    }
    for (k = 0; k < s->n && s->note[k] != note; k++)
        ;
    if (k == s->n) {
        if (s->n < 4u)
            s->n++;
        k = s->n - 1u;
        s->note[k] = (uint8_t)note;
    }
    s->lvl = (uint8_t)((s->lvl & ~(3u << (2u * k))) | (lvl & 3u) << (2u * k));
    s->rat = (uint8_t)((s->rat & ~(3u << (2u * k))) | (rat & 3u) << (2u * k));
    s->time = ST_NOTE;
    if (vel > s->vel)
        s->vel = (uint8_t)vel;
}

/* live recording into a synth part: the nearest step (rec_target). Held on: each further step the
 * sequencer enters while the note is held becomes a TIE (rec_hold), up to the pattern length and
 * never over a step with notes (overdub keeps them); a release before the middle of the last one
 * puts that step back (rec_release), so a short note stays one step. A note recorded into another
 * step ends the hold before (the step model ties the notes of one step only). */
static void rec_note(track_t *t, uint32_t note, uint32_t vel, uint32_t rat, int hold)
{
    uint32_t len = trk_len(t), later, abs = rec_target(t, &later), idx = abs % len, k;
    undo_mark(t, UNDO_REC(t));
    step_add(t, idx, note, vel, vel_lvl(vel), rat);
    t->seq_active = 1;
    if (later) {                                    /* it sounds now: the step must not trigger it again */
        if (t->rskip_abs != abs)
            t->rskip_n = 0;
        t->rskip_abs = abs;
        if (t->rskip_n < 4u)
            t->rskip[t->rskip_n++] = (uint8_t)note;
    }
    if (!hold)
        return;
    if (!t->rh_n || t->rh_start != idx) {           /* a new hold (one in another step ends) */
        t->rh_n = 0;
        t->rh_start = (uint8_t)idx;
        t->rh_ties = 0;
    }
    for (k = 0; k < t->rh_n && t->rh_note[k] != note; k++)
        ;
    if (k == t->rh_n && t->rh_n < 4u)
        t->rh_note[t->rh_n++] = (uint8_t)note;      /* a chord: held until its last key is up */
}

/* live recording into the BREAK track: the step gets the slice, its level and retrigger (ON) */
static void rec_hit(track_t *t, uint32_t sl, uint32_t lvl, uint32_t rat)
{
    uint32_t later, abs = rec_target(t, &later);
    bstep_t *st = &t->bstep[abs % trk_len(t)];
    undo_mark(t, UNDO_REC(t));
    st->on = 1;
    st->slice = (uint8_t)(sl % BRK_SLICES + 1u);
    st->lvl = (uint8_t)(lvl & 3u);
    st->rat = (uint8_t)rat;
    t->seq_active = 1;
    if (later) {                                    /* it sounds already: the step plays it without a new hit */
        t->rskip_abs = abs;
        t->rskip_n = 1;
    }
}

/* the sequencer enters step idx (before playing it): a recorded note still held ties into it */
static void rec_hold(track_t *t, uint32_t idx, uint32_t len, uint32_t abs)
{
    step_t *s;
    if (!t->rh_n)
        return;
    if (!((song.rec >> trk_index(t)) & 1u) || t->rh_ties + 1u >= len) {
        t->rh_n = 0;                                /* disarmed, or the whole pattern is this note */
        return;
    }
    if (idx == t->rh_start)
        return;                                     /* (recorded ahead into the step now starting) */
    s = &t->step[idx];
    if (s->time == ST_NOTE && s->n) {
        t->rh_n = 0;                                /* a step with notes: the hold ends before it */
        return;
    }
    t->rh_bak = *s;
    t->rh_last = (uint8_t)idx;
    t->rh_last_abs = abs;
    t->rh_ties++;
    memset(s, 0, sizeof *s);
    s->time = ST_TIE;
}

/* a key of a recorded note is up: the hold ends with the last one */
static void rec_release(track_t *t, uint32_t note)
{
    uint32_t i, k = 0, into, slen, half, lat;
    for (i = 0; i < t->rh_n; i++)
        if (t->rh_note[i] != note)
            t->rh_note[k++] = t->rh_note[i];
    if (k == t->rh_n || (t->rh_n = (uint8_t)k))
        return;                                     /* not one of them, or others still held */
    if (!t->rh_ties || trk_grid(t, &into, &slen) != t->rh_last_abs)
        return;
    half = slen / 2u;
    lat = REC_LAT * (uint32_t)song.g[G_BPM];
    if (into <= half + (lat < half ? lat : half) && t->step[t->rh_last].time == ST_TIE)
        t->step[t->rh_last] = t->rh_bak;            /* released early in it: not held into this step */
}

/* LIVE recording, no click needed (REC: ui_input.c rec_toggle).
 *  playing: REC records the selected track at once (song.rec), quantised, overdub.
 *  stopped: REC arms (rec_wait). The first note played on the selected track:
 *   - a project with notes: starts the transport, that note is step 1, recording on.
 *     PLAY while armed starts the transport and the recording together.
 *   - an empty project: a FREE TAKE (ft_on). Play freely, as long as you like: no tempo, no
 *     grid. REC on the next downbeat closes the loop: its length sets the tempo (1, 2 or 4
 *     bars, the nearest the current tempo; within 3 % of it the tempo is kept), the notes are
 *     quantised to 1/16 into it with their lengths, and the loop plays on. PLAY drops the take.
 *  The REC screen (ui_studio.c) sets how (settings of the FM-1, panel.c lights_word):
 *   KNOB 1 MODE (an empty project): FREE (the free take above) or TEMPO (record at the tempo set,
 *          as in a project with notes);
 *   KNOB 3 START (TEMPO, or a project with notes): NOTE (the first note starts the loop, as above)
 *          or COUNT (PLAY clicks one bar, 4 beats, then the loop and the recording start; notes
 *          played meanwhile only sound). */
static volatile uint8_t rec_wait;             /* 1: armed, waits for a note */
static uint8_t rec_tempo;                     /* REC screen MODE: 0 FREE, 1 TEMPO (an empty project) */
static uint8_t rec_count;                     /* REC screen START: 0 NOTE, 1 COUNT (one bar of clicks) */
static volatile uint8_t ci_on;                /* the count-in runs (armed, COUNT, PLAY) */
static volatile uint8_t ci_beat;              /* its beats clicked so far - 1 (the UI shows 4 - ci_beat) */
static uint32_t ci_u;                         /* clock units since it started */
static volatile uint8_t rec_go;               /* recording just started (the UI says so) */
static void seq_start(void);
static void rec_begin(void)
{
    song.rec = (uint8_t)(1u << (song.sel % NTRK));
    rec_wait = 0;
    rec_go = 1;
}

#define FT_MAX 192u
#define FT_BLOCKS (24u * FS / CTL)            /* 24 s: 4 bars at 40 BPM, the longest loop */
#define FT_OPEN 0xFFFFu                       /* the key is still down */
typedef struct { uint16_t t, d; uint8_t note, vel; } ft_ev_t;   /* in blocks from the first note; drums: lane, level */
static ft_ev_t ft_ev[FT_MAX];
static uint32_t ft_n;
static volatile uint8_t ft_on;                /* a free take runs */
static uint8_t ft_trk;
static volatile uint32_t ft_t;                /* blocks since its first note */
static volatile uint32_t ft_btn_mask;         /* the REC button (the UI sets it): closes the take */
static volatile uint32_t ft_drop_mask;        /* the PLAY button: drops it */
static uint32_t ft_btn_prev;
static volatile uint8_t ft_bars;              /* the loop just closed: bars (the UI says so), 0xFF dropped */
static volatile uint32_t ft_close_ms;         /* when (the UI drops the press that closed it) */
static volatile uint8_t ft_closed;            /* (ft_close_ms is set) */

static int track_empty(const track_t *t)
{
    uint32_t k;
    for (k = 0; k < NSTEP; k++)
        if (is_brk(t) ? t->bstep[k].on || t->bstep[k].slice : t->step[k].time == ST_NOTE && t->step[k].n)
            return 0;
    return 1;
}
static int project_empty(void)
{
    uint32_t i;
    for (i = 0; i < NTRK; i++)
        if (!track_empty(&trk[i]))
            return 0;
    return 1;
}
/* -------------------------------------------------- parameter locks --- */
/* A lock (track_t.lock, SLOOP 2.4): on its step the track's p[param] takes its value; the parameter goes back
 * to what it was at the next step without a lock on it (Elektron style: notes still ringing follow, the
 * engines read p[] every block). The locks in force are listed in lk_* (param, the base to go back to, the
 * value set). A knob turned while a lock is on wins: the value found is kept as the new base. Only the
 * sound parameters lock (p_lockable): the sequencer's, the arp's, the key's and the voice mode's do not. */
static int p_lockable(uint32_t id)
{
    return id <= P_LD_AMP || id == P_SGATE || (id >= P_DIST && id <= P_REV) || id == P_GLIDE || id == P_PAN ||
           id == P_DETUNE || (id >= P_SLCR && id <= P_SLDEPTH) || (id >= P_E0 && id <= P_E7) || id == P_TFLT;
}
/* the range of p[id] on track t (the engine that renders: t->engine; the BREAK track: its generator's) */
static const param_desc_t *lock_desc(const track_t *t, uint32_t id)
{
    if (is_brk(t) && brk_desc(id))
        return brk_desc(id);
    if (id >= P_E0 && id <= P_E7)
        return &ENGINES[t->engine % NENGINES]->edit[id - P_E0];
    return &TP[id % P_COUNT];
}
static void lock_write(track_t *t, uint32_t id, int32_t v)
{
    const param_desc_t *d = lock_desc(t, id);
    t->p[id % P_COUNT] = (int16_t)clamp(v, d->min, d->max);
}
static void locks_restore(track_t *t)        /* every lock in force let go (STOP, a load, a cleared pattern) */
{
    uint32_t i;
    for (i = 0; i < t->lk_n && i < NLOCK; i++)
        if (t->p[t->lk_param[i] % P_COUNT] == t->lk_set[i])
            lock_write(t, t->lk_param[i], t->lk_base[i]);
    t->lk_n = 0;
}
static void locks_clear(track_t *t)          /* no lock, no nudge (an empty pattern) */
{
    uint32_t i;
    locks_restore(t);
    memset(t->micro, 0, sizeof t->micro);
    for (i = 0; i < NLOCK; i++) {
        t->lock[i].step = LOCK_FREE;
        t->lock[i].param = 0;
        t->lock[i].val = 0;
    }
}
/* the sequencer enters step idx: its locks take hold, the last step's that it does not share let go */
static void lock_step(track_t *t, uint32_t idx)
{
    uint32_t i, k, n = 0;
    for (i = 0; i < t->lk_n && i < NLOCK; i++) {      /* in force, no lock here: back to the base (or the knob) */
        uint32_t p = t->lk_param[i] % P_COUNT, has = 0;
        for (k = 0; k < NLOCK; k++)
            if (t->lock[k].step == idx && t->lock[k].param == p)
                has = 1;
        if (has) {
            t->lk_param[n] = (uint8_t)p;
            t->lk_base[n] = t->lk_base[i];
            t->lk_set[n] = t->lk_set[i];
            n++;
        } else if (t->p[p] == t->lk_set[i]) {
            lock_write(t, p, t->lk_base[i]);
        }
    }
    t->lk_n = (uint8_t)n;
    for (k = 0; k < NLOCK; k++) {                     /* this step's locks */
        const plock_t *l = &t->lock[k];
        uint32_t p = l->param;
        if (l->step != idx || p >= P_COUNT || !p_lockable(p))
            continue;
        for (i = 0; i < t->lk_n && t->lk_param[i] != p; i++)
            ;
        if (i == t->lk_n) {
            if (i >= NLOCK)
                continue;
            t->lk_param[i] = (uint8_t)p;
            t->lk_base[i] = t->p[p];
            t->lk_n++;
        } else if (t->p[p] != t->lk_set[i]) {
            t->lk_base[i] = t->p[p];                  /* turned meanwhile: that is the new base */
        }
        lock_write(t, p, l->val);
        t->lk_set[i] = t->p[p];
    }
}
/* a lock of (step, param): its slot, or a free one for it (-1: none left) */
static int lock_find(const track_t *t, uint32_t step, uint32_t param, int make)
{
    uint32_t k;
    int fr = -1;
    for (k = 0; k < NLOCK; k++) {
        if (t->lock[k].step == step && t->lock[k].param == param)
            return (int)k;
        if (fr < 0 && t->lock[k].step == LOCK_FREE)
            fr = (int)k;
    }
    return make ? fr : -1;
}
/* set (or make) the lock of (step, param) at v, clamped; 0 = no slot left or not lockable. Callers hold the IRQ off */
static int lock_set(track_t *t, uint32_t step, uint32_t param, int32_t v)
{
    int k;
    const param_desc_t *d;
    if (step >= NSTEP || param >= P_COUNT || !p_lockable(param) || (is_brk(t) && !brk_lockable(param)) ||
        (k = lock_find(t, step, param, 1)) < 0)
        return 0;
    d = lock_desc(t, param);
    t->lock[k].step = (uint8_t)step;
    t->lock[k].param = (uint8_t)param;
    t->lock[k].val = (int16_t)clamp(v, d->min, d->max);
    return 1;
}
static void lock_del(track_t *t, uint32_t step, uint32_t param)   /* param P_COUNT: every lock of the step */
{
    uint32_t k;
    for (k = 0; k < NLOCK; k++)
        if (t->lock[k].step == step && (param >= P_COUNT || t->lock[k].param == param))
            t->lock[k].step = LOCK_FREE;
}
static int step_locked(const track_t *t, uint32_t step)   /* the step carries a lock or a nudge (the UI's mark) */
{
    uint32_t k;
    if (step < NSTEP && t->micro[step])
        return 1;
    for (k = 0; k < NLOCK; k++)
        if (t->lock[k].step == step)
            return 1;
    return 0;
}

/* ---- step conditions (SLOOP 2.4 fill, core.h FC_*): 2 bits a step in t->fill; GLO + key 9 held, or key 10
 * for the next bar, makes the fill: FC_FILL steps play only then, FC_NOFILL steps are silent then */
static uint32_t step_fill(const track_t *t, uint32_t idx)
{
    idx %= NSTEP;
    return (uint32_t)(t->fill[idx / 4u] >> (2u * (idx % 4u))) & 3u;
}
static void step_fill_set(track_t *t, uint32_t idx, uint32_t v)
{
    uint32_t sh;
    idx %= NSTEP;
    sh = 2u * (idx % 4u);
    t->fill[idx / 4u] = (uint8_t)((t->fill[idx / 4u] & ~(3u << sh)) | (v & 3u) << sh);
}
static volatile uint8_t fill_held;           /* GLO + key 9 down (the UI) */
static volatile uint8_t fill_arm;            /* GLO + key 10: the next bar is a fill (the UI; the ISR clears it) */
static uint8_t fill_bar_on;                  /* that bar, while it plays (the ISR) */
static uint8_t fill_now;                     /* this block is a fill: fill_held || fill_bar_on, read once a block */
static uint32_t fill_last_bar = 0xFFFFFFFFu; /* clk_beat / 4 of the last bar seen (events_block) */
static uint32_t step_plays(const track_t *t, uint32_t idx)   /* its condition holds now (3: as normal) */
{
    uint32_t c = step_fill(t, idx);
    return c == FC_FILL ? fill_now : c == FC_NOFILL ? !fill_now : 1u;
}

static void steps_clear(track_t *t)           /* an empty pattern (synth: REST steps, BREAK: every step off); no lock, no nudge, no condition */
{
    uint32_t k;
    memset(t->step, 0, sizeof t->step);
    if (!is_brk(t))
        for (k = 0; k < NSTEP; k++)
            t->step[k].time = ST_REST;
    locks_clear(t);
    memset(t->fill, 0, sizeof t->fill);
}

/* tempo x 10 of a loop of T blocks holding n bars of 4/4 */
static uint32_t ft_bpm10(uint32_t T, uint32_t n)
{
    uint32_t den = T * CTL;                       /* (n * 2400 * FS < 2^32 up to n = 4) */
    return den ? (n * 2400u * FS + den / 2u) / den : 0u;
}

/* the bars a loop of T blocks holds (1, 2 or 4: the tempo the nearest the current one, in
 * 40..240), 0 none; *bpm its tempo */
static uint32_t ft_fit(uint32_t T, uint32_t *bpm)
{
    static const uint8_t BARS[3] = {1, 2, 4};
    uint32_t i, best = 0, cur = (uint32_t)song.g[G_BPM] * 10u, err = 0xFFFFFFFFu;
    for (i = 0; i < 3u; i++) {
        uint32_t b = ft_bpm10(T, BARS[i]), e;
        if (b < 400u || b > 2400u)
            continue;
        e = b > cur ? b * 1000u / cur : cur * 1000u / b;   /* the ratio, x 1000 */
        if (e < err) {
            err = e;
            best = BARS[i];
            *bpm = (b + 5u) / 10u;
            if (e <= 1030u)
                *bpm = cur / 10u;                           /* played to the tempo set: keep it */
        }
    }
    return best;
}

static void ft_start(track_t *t)
{
    ft_on = 1;
    ft_trk = (uint8_t)trk_index(t);
    ft_t = 0;
    ft_n = 0;
    rec_wait = 0;
}

static void ft_note_on(uint32_t note, uint32_t vel)
{
    if (ft_n < FT_MAX && ft_t < FT_BLOCKS) {
        ft_ev[ft_n].t = (uint16_t)ft_t;
        ft_ev[ft_n].d = FT_OPEN;
        ft_ev[ft_n].note = (uint8_t)note;
        ft_ev[ft_n].vel = (uint8_t)vel;
        ft_n++;
    }
}

static void ft_note_off(uint32_t note)
{
    uint32_t i = ft_n;
    while (i--)
        if (ft_ev[i].note == note && ft_ev[i].d == FT_OPEN) {
            ft_ev[i].d = (uint16_t)(ft_t - ft_ev[i].t);
            return;
        }
}

/* REC on the downbeat: the loop is the time from the first note to now */
static void ft_close(void)
{
    track_t *t = &trk[ft_trk % NTRK];
    uint32_t T = ft_t, bars, bpm = 0, len, i, k;
    ft_on = 0;
    ft_close_ms = fm1_ms;
    ft_closed = 1;
    bars = ft_n && T >= FS / CTL / 2u ? ft_fit(T, &bpm) : 0u;
    if (!bars) {
        ft_bars = 0xFF;                               /* nothing played, or no tempo fits */
        return;
    }
    len = 16u * bars;
    undo_mark(t, (undo_sess += 4u) | 3u);             /* (undo: back to the empty project) */
    steps_clear(t);                                   /* (no tie left over from an old pattern) */
    for (i = 0; i < NTRK; i++) {                      /* the project is empty: one loop length */
        trk[i].p[P_SLEN] = (int16_t)len;
        trk[i].p[P_SDIV] = 2;                         /* 1/16 */
    }
    for (i = 0; i < ft_n; i++) {                      /* the notes, to the nearest step */
        uint32_t idx = (ft_ev[i].t * len * 2u + T) / (2u * T) % len;
        if (is_brk(t)) {                              /* (the BREAK track: note = the slice, vel = the level) */
            t->bstep[idx].on = 1;
            t->bstep[idx].slice = (uint8_t)(ft_ev[i].note % BRK_SLICES + 1u);
            t->bstep[idx].lvl = (uint8_t)(ft_ev[i].vel & 3u);
        } else
            step_add(t, idx, ft_ev[i].note, ft_ev[i].vel, vel_lvl(ft_ev[i].vel), 0);
    }
    if (!is_brk(t))
        for (i = 0; i < ft_n; i++) {                  /* their lengths: TIE steps, up to the next note */
            uint32_t d = ft_ev[i].d == FT_OPEN || ft_ev[i].t + ft_ev[i].d > T ? T - ft_ev[i].t : ft_ev[i].d;
            uint32_t idx = (ft_ev[i].t * len * 2u + T) / (2u * T) % len;
            uint32_t steps = (d * len * 2u + T) / (2u * T);
            for (k = 1; k < steps && k < len; k++) {
                step_t *s = &t->step[(idx + k) % len];
                if (s->time == ST_NOTE && s->n)
                    break;
                memset(s, 0, sizeof *s);
                s->time = ST_TIE;
            }
        }
    t->seq_active = 1;
    song.g[G_BPM] = (int16_t)bpm;
    ft_bars = (uint8_t)bars;
#if FELUCCA_ARRANGER
    arrangement_enabled = 0;
#endif
    seq_start();                                      /* this is the downbeat: the loop plays */
}

/* the UI: a REC / PLAY press is this one's (it runs the take, or just closed it): not the UI's */
static int ft_owns_press(void)
{
    return ft_on || (ft_closed && (uint32_t)(fm1_ms - ft_close_ms) < 300u);
}

/* a free take, once per block: REC closes it, PLAY drops it (their press, timed here, not by the UI) */
static void ft_block(void)
{
    uint32_t b = fm1_in.buttons & (ft_btn_mask | ft_drop_mask), press = b & ~ft_btn_prev;
    ft_btn_prev = b;
    if (!ft_on)
        return;
    ft_t++;
    if (press & ft_drop_mask) {
        ft_on = 0;                                    /* PLAY: dropped, nothing changes */
        ft_bars = 0xFF;
        ft_close_ms = fm1_ms;
        ft_closed = 1;
    } else if ((press & ft_btn_mask) || ft_t >= FT_BLOCKS) {
        ft_close();
    }
}

/* ------------------------------------------------------------ erase --- */
/* EDIT + key: that note / sound goes from the selected pattern. Playing: from the step playing and
 * every step the sequencer enters while the key is held (MPC style); stopped: from every step. */
static uint8_t er_trk;
static uint16_t er_lanes;                     /* BREAK: the slices held */
static uint32_t er_notes[4];                  /* synth: notes held (bit per MIDI note) */
static volatile uint8_t er_flash;             /* something was erased (the UI flashes) */

static int er_has(uint32_t note) { return (er_notes[(note >> 5) & 3u] >> (note & 31u)) & 1u; }
static void erase_step(track_t *t, uint32_t idx)
{
    if (is_brk(t)) {                          /* a step's own slice, held: the generator's again */
        bstep_t *s = &t->bstep[idx];
        if (s->slice && ((er_lanes >> ((s->slice - 1u) % BRK_SLICES)) & 1u)) {
            s->slice = 0;
            er_flash = 1;
        }
    } else {
        step_t *s = &t->step[idx];
        uint32_t i, k = 0, lv = 0, rt = 0;
        if (s->time != ST_NOTE)
            return;
        for (i = 0; i < s->n; i++)
            if (!er_has(s->note[i])) {
                lv |= ((s->lvl >> (2u * i)) & 3u) << (2u * k);
                rt |= ((s->rat >> (2u * i)) & 3u) << (2u * k);
                s->note[k++] = s->note[i];
            }
        if (k == s->n)
            return;
        er_flash = 1;
        s->n = (uint8_t)k;
        s->lvl = (uint8_t)lv;
        s->rat = (uint8_t)rt;
        if (!k) {
            s->time = ST_REST;
            s->flags = 0;
            s->vel = 0;
        }
    }
}
static void erase_now(track_t *t)             /* a key just went down */
{
    uint32_t i, len = trk_len(t);
    undo_mark(t, undo_erase_sess);
    if (song.playing && t->seq_abs != SEQ_NONE) {
        erase_step(t, t->seq_idx % len);
    } else {
        for (i = 0; i < len; i++)
            erase_step(t, i);
    }
}
static int erasing(const track_t *t) { return trk_index(t) == er_trk && (er_lanes || er_notes[0] || er_notes[1] || er_notes[2] || er_notes[3]); }


/* ------------------------------------------------------- note input --- */
/* armed and stopped, a note on the selected track: an empty project starts a free take, else the
 * note is the downbeat (the transport starts, recording on) */
static void arm_start(track_t *t)
{
    if (!rec_wait || t != TSEL || song.playing || ci_on)
        return;
    if (project_empty() && !rec_tempo) {
        ft_start(t);                              /* the first take sets the loop and the tempo */
    } else if (rec_count) {
        return;                                   /* COUNT: PLAY counts in; a note only sounds */
    } else {
#if FELUCCA_ARRANGER
        arrangement_enabled = 0;
#endif
        seq_start();                              /* the note is the downbeat */
        if (song.playing)
            rec_begin();
    }
}

static void brk_input(uint32_t sl, uint32_t lvl, uint32_t rat, int rec);
static const uint8_t *in_chord;                    /* input_on's note is note in_chord_i of in_chord (CHORD+) */
static uint32_t in_chord_n, in_chord_i;
static void input_on(track_t *t, uint32_t note, uint32_t vel)
{
    if (is_brk(t)) {                              /* (a note on the BREAK track: its slice) */
        brk_input(brk_slice_of_note(note), vel_lvl(vel), 0, 1);
        return;
    }
    last_note = (uint8_t)note;
    if (t == &trk[2] && cs_hand < 16u) {
        cs_hand++;
        cs_key(note);
    }
    arm_start(t);
    if (ft_on && t == &trk[ft_trk % NTRK])
        ft_note_on(note, vel);
    if (((song.rec >> trk_index(t)) & 1u) && song.playing)
        rec_note(t, note, vel, 0, 1);
    if (in_chord)                                 /* a chord from the keys: maybe strummed (voice.c) */
        trk_note_chord(t, in_chord, in_chord_n, in_chord_i, vel);
    else
        trk_note_on(t, note, vel);
}

static void input_off(track_t *t, uint32_t note)
{
    if (is_brk(t)) {
        if (ft_on && ft_trk == TRK_BRK)
            ft_note_off(brk_slice_of_note(note));
        return;
    }
    if (ft_on && t == &trk[ft_trk % NTRK])
        ft_note_off(note);
    if (t == &trk[2] && cs_hand)
        cs_hand--;
    rec_release(t, note);
    trk_note_off(t, note);
}

/* a hit on the BREAK track from a key, MIDI or a roll: slice, level; rat: its retrigger when recorded (rolls); rec:
 * it may be recorded (a roll records one hit a step) */
static void brk_input(uint32_t sl, uint32_t lvl, uint32_t rat, int rec)
{
    track_t *t = TBRK;
    sl %= BRK_SLICES;
    pen_lane = (uint8_t)sl;
    arm_start(t);
    if (ft_on && ft_trk == TRK_BRK)
        ft_note_on(sl, lvl);
    if (rec && ((song.rec >> TRK_BRK) & 1u) && song.playing)
        rec_hit(t, sl, lvl, rat);
    trk_note_on(t, 36u + sl, lvl_vel(lvl, 100));
}

/* -------------------------------------------------------------- roll --- */
/* ARP + key: note repeat. The key plays at G_ROLL (1/8, 1/16, 1/32, 32T, 1/64) on the transport's grid
 * (stopped: from the press) until it or ARP is up. Recorded, a roll faster than the track's steps
 * becomes ratchets (1/32 on 1/16 steps: x2, 32T: x3, 1/64: x4). */
static const uint8_t ROLL_DEN[5] = {2, 4, 8, 12, 16};
#define NROLL 4u
static struct {
    uint8_t on, key, trk, note, lvl;    /* note: the synth note, or the slice */
    uint32_t last;                      /* playing: the roll step last played; stopped: units since */
    uint32_t off;                       /* synth: units to its note-off, 0 = not sounding */
    uint32_t rec_abs;                   /* the step its last recorded hit went into */
} roll[NROLL];
static uint32_t roll_den(void) { return ROLL_DEN[(uint32_t)clamp(song.g[G_ROLL], 0, 4)]; }
/* a roll plays this note live on track t: the pattern does not play it meanwhile (no flam with what was recorded,
 * nor with what this roll is recording) */
static int roll_has(const track_t *t, uint32_t note)
{
    uint32_t r;
    for (r = 0; r < NROLL; r++)
        if (roll[r].on && roll[r].trk == trk_index(t) && roll[r].note == note)
            return 1;
    return 0;
}

static void roll_hit(uint32_t r)
{
    track_t *t = &trk[roll[r].trk % NTRK];
    uint32_t u = BEAT_U / roll_den(), su = div_units((uint32_t)t->p[P_SDIV]);
    uint32_t hits = su / u, rat = hits > 4u ? 3u : hits > 1u ? hits - 1u : 0u;
    uint32_t armed = ((song.rec >> trk_index(t)) & 1u) && song.playing, later, abs = 0, rec = 0;
    if (armed) {                                     /* one recorded hit a step: the ratchet does the rest */
        abs = rec_target(t, &later);
        rec = abs != roll[r].rec_abs;
        roll[r].rec_abs = abs;
    }
    if (is_brk(t)) {
        brk_input(roll[r].note, roll[r].lvl, rat, rec || !armed);
        seq_out_on(t, 36u + roll[r].note % BRK_SLICES, lvl_vel(roll[r].lvl, 100));
        return;
    }
    arm_start(t);
    if (roll[r].off)
        trk_note_off(t, roll[r].note);
    trk_note_on(t, roll[r].note, lvl_vel(roll[r].lvl, 100));
    seq_out_on(t, roll[r].note, lvl_vel(roll[r].lvl, 100));
    roll[r].off = u / 2u;
    if (rec)
        rec_note(t, roll[r].note, 100, rat, 0);
    if (ft_on && t == &trk[ft_trk % NTRK])
        ft_note_on(roll[r].note, 100), ft_note_off(roll[r].note);
}

static void roll_start(uint32_t k, track_t *t, uint32_t note, uint32_t lvl)
{
    uint32_t r, den = roll_den(), into, slen, abs;
    for (r = 0; r < NROLL && roll[r].on; r++)
        ;
    if (r == NROLL)
        return;
    roll[r].on = 1;
    roll[r].key = (uint8_t)k;
    roll[r].trk = (uint8_t)trk_index(t);
    roll[r].note = (uint8_t)note;
    roll[r].lvl = (uint8_t)lvl;
    roll[r].off = 0;
    roll[r].rec_abs = SEQ_NONE;
    if (song.playing) {
        abs = grid_den(den, 0, &into, &slen);
        roll[r].last = abs;
        if (into * 4u >= slen * 3u)
            return;                                  /* the grid is just ahead: it starts there */
    } else {
        roll[r].last = 0;
    }
    roll_hit(r);
}

static void roll_end(uint32_t r)
{
    if (roll[r].on && roll[r].off && roll[r].trk != TRK_BRK) {
        trk_note_off(&trk[roll[r].trk % NTRK], roll[r].note);
        seq_out_off(&trk[roll[r].trk % NTRK], roll[r].note);
    }
    if (roll[r].on && roll[r].trk == TRK_BRK)
        seq_out_off(TBRK, 36u + roll[r].note % BRK_SLICES);
    roll[r].on = 0;
}

static void roll_block(uint32_t adv)
{
    uint32_t r, den = roll_den(), u = BEAT_U / den, into, slen;
    for (r = 0; r < NROLL; r++) {
        if (!roll[r].on)
            continue;
        if (roll[r].off) {
            if (roll[r].off <= adv) {
                trk_note_off(&trk[roll[r].trk % NTRK], roll[r].note);
                seq_out_off(&trk[roll[r].trk % NTRK], roll[r].note);
                roll[r].off = 0;
            } else {
                roll[r].off -= adv;
            }
        }
        if (song.playing) {
            uint32_t abs = grid_den(den, 0, &into, &slen);
            if (abs != roll[r].last) {
                roll[r].last = abs;
                roll_hit(r);
            }
        } else {
            roll[r].last += adv;
            if (roll[r].last >= u) {
                roll[r].last = roll[r].last - u < u ? roll[r].last - u : 0;
                roll_hit(r);
            }
        }
    }
}

/* ---------------------------------------------------------- keyboard --- */
/* the level of a key on the drum track: OCT- held ghost, OCT+ held hard */
static uint32_t key_lvl(void)
{
    uint32_t b = fm1_in.buttons;
    return (b & dyn_bit[0]) ? LV_GHOST : (b & dyn_bit[1]) ? LV_HARD : LV_NORM;
}

/* CHORD+: the modifiers held (the black keys down in chord mode) */
static uint32_t chord_mods(void)
{
    uint32_t k, m = 0;
    for (k = 0; k < 27u; k++)
        if (kb_kind[k] == KS_MOD)
            m |= chord_mod_of_key(k);
    return m;
}
/* a modifier went down or up: every chord held on part sel changes under the finger (the notes it loses end,
 * the ones it gains start; the ones it keeps ring on) */
static void chord_revoice(uint32_t sel)
{
    uint32_t k, i, j, mods = chord_mods(), mc = trk_midi_ch(sel);
    track_t *t = &trk[sel % NTRK];
    for (k = 0; k < 27u; k++) {
        uint8_t nw[4];
        uint32_t nn;
        if (kb_kind[k] != KS_NOTE || kb_trk[k] != sel || !t->p[P_CHORD] || is_brk(t))
            continue;
        nn = chord_play_notes(t, kb_root[k], mods, nw);
        for (i = 0; i < kb_n[k]; i++) {             /* the notes it loses */
            for (j = 0; j < nn && nw[j] != kb_nt[k][i]; j++)
                ;
            if (j == nn) {
                input_off(t, kb_nt[k][i]);
                midi_out_event(0x08u | (0x80u | mc) << 8 | (uint32_t)kb_nt[k][i] << 16);
            }
        }
        for (j = 0; j < nn; j++) {                  /* the notes it gains */
            for (i = 0; i < kb_n[k] && kb_nt[k][i] != nw[j]; i++)
                ;
            if (i == kb_n[k]) {
                input_on(t, nw[j], 100);
                midi_out_event(0x09u | (0x90u | mc) << 8 | (uint32_t)nw[j] << 16 | 100u << 24);
            }
        }
        memcpy(kb_nt[k], nw, nn);
        kb_n[k] = (uint8_t)nn;
    }
}

/* the FX panel's keys (FX held, or locked open; LIVE and STUDIO alike: lfx.c): a white key its effect, black keys 1..6
 * the tracks, LATCH, CLEAR; nothing plays, nothing records */
static void lfx_key_down(uint32_t k)
{
    int32_t w = white_of_key(k);
    uint32_t j;
    if (w >= 0) {
        kb_kind[k] = KS_LFX;
        kb_nt[k][0] = (uint8_t)(w % (int32_t)LFX_N);
        lfx_press((uint32_t)w % LFX_N);
        return;
    }
    for (j = 0; j < 6u && LIVE_BLACK[j] != k; j++)
        ;
    if (j < 6u) {
        kb_kind[k] = KS_LFXB;
        kb_nt[k][0] = (uint8_t)j;
        lfx_black(j);
    }
}

/* LIVE (audio ISR): white keys 1..12 to the UI, 13..16 its moves, a black key plays the focus track: on the BREAK
 * black keys 1..4 hold its loop A..D (now, whatever Q), the others a slice; on the CHOP a kit's hit, else the note white
 * key j would play */
static void live_key_down(uint32_t k, track_t *t)
{
    int32_t w = white_of_key(k);
    uint32_t j, n, i, base, nh;
    if (layer_buttons() & ly_bit[LY_FX]) {             /* the FX panel */
        lfx_key_down(k);
        return;
    }
    if (w >= 12) {
        kb_kind[k] = KS_LIVE;
        kb_nt[k][0] = (uint8_t)(w - 12);
        if (w == 12)
            live_gap = 1;
        else {
            if (w == 15)
                live_rw_cut = 0;                     /* (a new REWIND: its own again) */
            live_keys = (uint16_t)(live_keys | 1u << LIVE_MOVE[w - 12]);
        }
        lk_push(KB_LIVE, k, 1);
        return;
    }
    if (w >= 0) {
        kb_kind[k] = KS_UI;
        kb_nt[k][0] = (uint8_t)KB_LIVE;
        lk_push(KB_LIVE, k, 1);
        return;
    }
    for (j = 0; j < 11u && LIVE_BLACK[j] != k; j++)
        ;
    if (is_brk(t) && j < 4u) {
        kb_kind[k] = KS_BANK;
        kb_nt[k][0] = (uint8_t)j;
        brk_hold_bank(j, 1);
        return;
    }
    if (is_brk(t)) {
        kb_kind[k] = KS_DRUM;
        kb_nt[k][0] = (uint8_t)(j % BRK_SLICES);
        brk_input(j % BRK_SLICES, key_lvl(), 0, 1);
        return;
    }
    if (t == &trk[2] && j < 3u) {                      /* the CHOP (chopgen.c CS_TRK): 1..3 its MODE, as the BREAK's
                                                         * loops (ui_live.c live_key: the main loop switches it) */
        kb_kind[k] = KS_UI;
        kb_nt[k][0] = (uint8_t)KB_LIVE;
        lk_push(KB_LIVE, k, 1);
        return;
    }
    if (t == &trk[2])
        j -= 3u;                                      /* (its hits, its notes: from black key 4) */
    if (ENGINES[t->eng_req % NENGINES] == &ENG_SAMPLE && smp_kit((uint32_t)t->p[P_E0] % SMP_NALL, &base, &nh))
        n = j < nh ? base + j : KB_SILENT;            /* (a kit: hit j) */
    else
        n = kb_map(t, (j / 7u) * 12u + LIVE_WK[j % 7u]);
    if (n == KB_SILENT)
        return;
    kb_kind[k] = KS_NOTE;
    if (t->p[P_CHORD]) {
        kb_n[k] = (uint8_t)chord_play_notes(t, n, 0, kb_nt[k]);
    } else {
        kb_nt[k][0] = (uint8_t)n;
        kb_n[k] = 1;
    }
    for (i = 0; i < kb_n[k]; i++)
        input_on(t, kb_nt[k][i], 100);
}

static void key_down(uint32_t k)
{
    uint32_t layer = layer_now(), sel = song.sel % NTRK, i, mc;
    track_t *t = &trk[sel];
    kb_kind[k] = KS_NONE;
    kb_trk[k] = (uint8_t)sel;
    kb_n[k] = 0;
    if (kb_live) {
        live_key_down(k, t);
        return;
    }
    switch (layer) {
    case LY_FX:                                       /* FX held: the FX panel (lfx.c), as in LIVE */
        lfx_key_down(k);
        return;
    case LY_STEP:
    case LY_SCALE:
    case LY_MIX:                                      /* the UI's: steps, the key, the mix */
        kb_kind[k] = KS_UI;
        kb_nt[k][0] = (uint8_t)layer;                 /* (its key-up goes to the same layer) */
        lk_push(layer, k, 1);
        return;
    case LY_ERASE:
        kb_kind[k] = KS_ERASE;
        if (!erasing(t) || er_trk != sel)
            undo_erase_sess = (undo_sess += 4u) | 2u;  /* a new erase: one undo */
        if (er_trk != sel) {
            er_lanes = 0;
            er_notes[0] = er_notes[1] = er_notes[2] = er_notes[3] = 0;
            er_trk = (uint8_t)sel;
        }
        if (is_brk(t)) {
            kb_nt[k][0] = (uint8_t)brk_slice_of_key(k);
            kb_n[k] = 1;
            er_lanes |= (uint16_t)(1u << kb_nt[k][0]);
        } else {
            uint32_t n = kb_map(t, k);
            if (n == KB_SILENT)
                return;
            kb_n[k] = (uint8_t)(t->p[P_CHORD] ? chord_notes(t, n, kb_nt[k]) : 1u);
            if (!t->p[P_CHORD])
                kb_nt[k][0] = (uint8_t)n;
            for (i = 0; i < kb_n[k]; i++)
                er_notes[(kb_nt[k][i] >> 5) & 3u] |= 1u << (kb_nt[k][i] & 31u);
        }
        erase_now(t);
        return;
    default:
        break;
    }
    if (layer == LY_PLAY && kb_chp && white_of_key(k) < 0) {   /* the CHOP screen: a black key is the screen's */
        kb_kind[k] = KS_UI;
        kb_nt[k][0] = (uint8_t)KB_CHP;
        lk_push(KB_CHP, k, 1);
        return;
    }
    if (layer == LY_PLAY && kb_bss && white_of_key(k) < 0 && k < 6u) {   /* the BASS screen: black keys 1..3 */
        kb_kind[k] = KS_UI;
        kb_nt[k][0] = (uint8_t)KB_BSS;
        lk_push(KB_BSS, k, 1);
        return;
    }
    if (layer == LY_PLAY && kb_chd) {                 /* the CHORDS screen: a node / step key, played by the screen */
        kb_kind[k] = KS_UI;
        kb_nt[k][0] = (uint8_t)KB_CHD;
        lk_push(KB_CHD, k, 1);
        return;
    }
    if (layer == LY_PLAY && kb_brk) {                 /* the BREAK screen: a step / slice key, played by the part */
        kb_kind[k] = KS_UI;
        kb_nt[k][0] = (uint8_t)KB_BRK;
        lk_push(KB_BRK, k, 1);
        return;
    }
    if (is_brk(t)) {                                  /* the BREAK track off its screen: the key's slice */
        uint32_t lane = brk_slice_of_key(k), lvl = key_lvl();
        kb_nt[k][0] = (uint8_t)lane;
        kb_n[k] = 1;
        if (layer == LY_ROLL) {
            kb_kind[k] = KS_ROLL;
            roll_start(k, t, lane, lvl);
            return;
        }
        kb_kind[k] = KS_DRUM;
        brk_input(lane, lvl, 0, 1);
        mc = trk_midi_ch(sel);
        midi_out_event(0x09u | (0x90u | mc) << 8 | (36u + lane) << 16 | lvl_vel(lvl, 100) << 24);
        return;
    }
    {
        uint32_t n = kb_map(t, k);
        if (n == KB_SILENT) {
            if (t->p[P_CHORD] && layer == LY_PLAY && chord_mod_of_key(k)) {   /* CHORD+: a modifier key */
                kb_kind[k] = KS_MOD;
                chord_revoice(sel);
            }
            return;
        }
        if (layer == LY_ROLL) {
            kb_kind[k] = KS_ROLL;
            kb_nt[k][0] = (uint8_t)n;
            kb_n[k] = 1;
            roll_start(k, t, n, LV_NORM);
            return;
        }
        kb_kind[k] = KS_NOTE;
        kb_root[k] = (uint8_t)n;
        if (t->p[P_CHORD]) {
            kb_n[k] = (uint8_t)chord_play_notes(t, n, chord_mods(), kb_nt[k]);   /* (the modifiers held, voiced) */
        } else {
            kb_nt[k][0] = (uint8_t)n;
            kb_n[k] = 1;
        }
        mc = trk_midi_ch(sel);
        for (i = 0; i < kb_n[k]; i++) {
            in_chord = kb_n[k] > 1u ? kb_nt[k] : 0;
            in_chord_n = kb_n[k];
            in_chord_i = i;
            input_on(t, kb_nt[k][i], 100);
            in_chord = 0;
            midi_out_event(0x09u | (0x90u | mc) << 8 | (uint32_t)kb_nt[k][i] << 16 | 100u << 24);
        }
        /* the pen of the SEQ layer: the keys down now (a chord), else this note */
        if (!(kb_prev & ~(1u << k)) || pen_n >= 4u)
            pen_n = 0;
        for (i = 0; i < kb_n[k] && pen_n < 4u; i++)
            pen_note[pen_n++] = kb_nt[k][i];
    }
}

static void key_up(uint32_t k)
{
    uint32_t i, mc, kind = kb_kind[k];
    track_t *t = &trk[kb_trk[k] % NTRK];
    kb_kind[k] = KS_NONE;
    switch (kind) {
    case KS_UI:
        lk_push(kb_nt[k][0], k, 0);
        return;
    case KS_LFX:
        lfx_release(kb_nt[k][0]);
        return;
    case KS_LFXB:
        lfx.black = (uint8_t)(lfx.black & ~(1u << kb_nt[k][0]));
        return;
    case KS_BANK:
        brk_hold_bank(kb_nt[k][0], 0);
        return;
    case KS_LIVE:
        if (kb_nt[k][0] == 0u)
            live_gap = 0;
        else
            live_keys = (uint16_t)(live_keys & ~(1u << LIVE_MOVE[kb_nt[k][0] & 3u]));
        lk_push(KB_LIVE, k, 0);                       /* (REWIND let go: the section from its start, ui_live.c) */
        return;
    case KS_MOD:                                    /* a CHORD+ modifier let go: the chords held change back */
        chord_revoice(kb_trk[k]);
        return;
    case KS_ERASE:
        if (is_brk(t))
            er_lanes &= (uint16_t)~(1u << kb_nt[k][0]);
        else
            for (i = 0; i < kb_n[k]; i++)
                er_notes[(kb_nt[k][i] >> 5) & 3u] &= ~(1u << (kb_nt[k][i] & 31u));
        return;
    case KS_ROLL:
        for (i = 0; i < NROLL; i++)
            if (roll[i].on && roll[i].key == k)
                roll_end(i);
        return;
    case KS_NOTE:
        mc = trk_midi_ch(kb_trk[k] % NTRK);
        for (i = 0; i < kb_n[k]; i++) {
            input_off(t, kb_nt[k][i]);
            midi_out_event(0x08u | (0x80u | mc) << 8 | (uint32_t)kb_nt[k][i] << 16);
        }
        return;
    case KS_DRUM:
        if (ft_on && ft_trk == TRK_BRK)
            ft_note_off(kb_nt[k][0]);
        mc = trk_midi_ch(TRK_BRK);
        midi_out_event(0x08u | (0x80u | mc) << 8 | (36u + kb_nt[k][0] % BRK_SLICES) << 16);
        return;
    default:
        return;
    }
}

/* the UI asks to hear a slice of the BREAK track (a slice picked with a knob, a step set from a key): aud_sl the
 * slice + 1 at level aud_lvl, played here, in the audio context */
static volatile uint8_t aud_sl, aud_lvl;
static void audition_slice(uint32_t sl, uint32_t lvl)
{
    fm1_irq_off();
    aud_lvl = (uint8_t)(lvl & 3u);
    aud_sl = (uint8_t)(sl % BRK_SLICES + 1u);
    fm1_irq_on();
}
static void audition_block(void)
{
    uint32_t sl = aud_sl;
    if (!sl)
        return;
    aud_sl = 0;
    trk_note_on(TBRK, 36u + sl - 1u, lvl_vel(aud_lvl, 100));
}

static void keyboard_block(void)
{
    uint32_t cur = fm1_in.notes, ch = cur ^ kb_prev, k, r;
    audition_block();
    if (!(layer_buttons() & ly_bit[LY_ROLL]))         /* ARP up (and not locked): the rolls end (the keys stay silent) */
        for (r = 0; r < NROLL; r++)
            if (roll[r].on)
                roll_end(r);
    if (!ch)
        return;
    for (k = 0; k < 27u; k++) {
        if (!((ch >> k) & 1u))
            continue;
        if ((cur >> k) & 1u)
            key_down(k);
        else
            key_up(k);
        kb_prev ^= 1u << k;                           /* (key_down sees the keys down before it) */
    }
}

/* LIVE: one record arm at most, on the selected track; selecting another track moves it there
 * (an arm follows too: it waits for a note on the track selected; a free take stays on its track) */
static void rec_follow(uint32_t sel)
{
    if (song.rec)
        song.rec = (uint8_t)(1u << (sel % NTRK));
}

/* -------------------------------------------------------- metronome --- */
/* A wood block on every beat, louder on the first of the bar, from the drum kit's voices
 * (not recorded, not muted with a track; click.c click_on: its own voice).
 * GLO > GLOBAL CLICK: OFF (0, default: LIVE needs none), REC = while a track records,
 * ON = while playing. */
#define CLICK_REC 1
#define CLICK_ON 2
static uint32_t click_last = SEQ_NONE;          /* the beat it last played */
static void click_tick(void)
{
    uint32_t mode = (uint32_t)song.g[G_CLOCK];
    if (!song.playing || clk_beat == click_last)
        return;
    click_last = clk_beat;
    if (mode == CLICK_ON || (mode == CLICK_REC && song.rec))
        click_on(clk_beat % 4u == 0u);
}

/* -------------------------------------------------------- sequencer --- */
static void seq_reset_tracks(uint32_t pos);
static volatile uint8_t as_swap;                       /* LIVE: the next song's slot + 1, on the next bar (arranger_scene.c) */
static volatile uint8_t as_swapped;                    /* (bumped on each: the UI sees it done) */
static volatile uint8_t as_swapped_to;                 /* ... the slot + 1 of the song it put in */
static void as_swap_now(void);
/* LIVE, the way to the next song (arranger_scene.c): what white key 8 asks, taken on the last bar
 * of an 8-bar phrase; AUTO's next song, the OUTRO its bridge; the way under way, from that bar to the next song's first */
enum { TR_NONE, TR_BRIDGE, TR_SWAP, TR_CUT, TR_N };
static volatile uint8_t as_tr_ask;                     /* the way asked (TR_*): the last choice; 0 none */
static volatile uint8_t as_tr_slot;                    /* ... to the song of this slot + 1 */
static volatile uint8_t as_auto_next;                  /* AUTO: the set's next song (slot + 1); 0: this is the last */
static volatile uint8_t as_tr_way;                     /* the way under way (TR_*, 0 none) */
static volatile uint8_t as_tr_to;                      /* ... to the song of this slot + 1 */
static volatile int8_t as_pend = -1;                   /* LIVE: the section asked for (a role; -1 none), not yet in the
                                                        * arranger (ui_live.c live_poll): AUTO off too, the arranger
                                                        * leads into it as into the chain's next (as_block) */
static volatile int8_t as_rw_to = -1;                  /* LIVE: the jump REWIND resolves into (its entry; -1 none): no
                                                        * lead, the rewind is its way in (as_block) */
static void as_tr_bar(void);
static void as_hold(uint32_t on);
#if FELUCCA_ARRANGER
static void as_block(void);                        /* project.c (arranger_scene.c): AUTO SONG's moves */
#endif

/* every track from its step 0, together, at pos units into beat 0 (a song section: the arranger's
 * remainder, so the new section starts exactly on its bar) */
static void seq_reset_tracks(uint32_t pos)
{
    uint32_t i;
    for (i = 0; i < NTRK; i++) {
        track_t *t = &trk[i];
        t->seq_abs = SEQ_NONE;
        t->seq_idx = 0;
        t->rskip_n = 0;
        t->rskip_abs = SEQ_NONE;
        t->rh_n = 0;
    }
    for (i = 0; i < NROLL; i++)
        roll[i].last = SEQ_NONE - 1u;               /* (a roll held over the start: on the grid from here) */
    clk_beat = 0;
    clk_pos = pos;
    fill_last_bar = 0xFFFFFFFFu;                   /* (the same for an armed fill bar) */
    click_last = SEQ_NONE;
    song.tick = 0;
    song.playing = 1;
    slicer_start(pos);                             /* slicer.c: its step 0 with the sequencer's */
}


static void song_backup(void);                     /* project.c: song mode keeps the loop you made */
static void song_restore(void);

static void seq_start(void)
{
#if FELUCCA_ARRANGER
    if (arrangement_enabled && !song.playing) {
        rec_wait = 0;                              /* song mode plays, it does not record */
        song_backup();
    }
    if (!arrangement_start()) return;
#endif
    seq_reset_tracks(0);
}

static void seq_release(track_t *t)
{
    uint32_t i;
    for (i = 0; i < t->seq_n; i++) {
        trk_note_off(t, t->seq_notes[i]);
        seq_out_off(t, t->seq_notes[i]);
    }
    if (is_brk(t))
        seq_out_track_off(t);                       /* the hits of the step: ended with it */
    t->seq_n = 0;
    t->seq_hold = 0;
    t->slide_glide = 0;                             /* live MONO / LEG keys must not glide after it */
}

static void seq_stop(void)
{
    uint32_t i;
    fill_held = 0;                                  /* STOP ends a fill, held or armed */
    fill_arm = 0;
    fill_bar_on = 0;
    song.playing = 0;
    for (i = 0; i < NTRK; i++) {
        seq_release(&trk[i]);
        locks_restore(&trk[i]);                    /* the parameters back to their base */
        trk[i].rh_n = 0;                           /* a recorded note held over the stop: as far as it got */
    }
    seq_out_all_off();
#if FELUCCA_ARRANGER
    if (arrangement_clock.running) {
        arrangement_clock.running = 0;
        song_restore();                            /* back to the loop you were making */
    }
#endif
}

/* the velocity of note i of synth step s */
static uint32_t step_vel(const step_t *s, uint32_t i)
{
    uint32_t base = (s->flags & SF_ACCENT) ? 127u : (s->vel ? s->vel : 96u);
    return lvl_vel((s->lvl >> (2u * i)) & 3u, base);
}

/* play one synth step: TIE extends, REST releases, NOTE (re)triggers; a SLIDE on the previous
 * step makes this one legato with a glide (acid style). skip: bit k = note k already sounds
 * from live recording (not triggered, not released here). len: the step's length (units). */
#include "chordgen.c"                                /* the CHORDS generator (FMX): HOLD, STEPS, BASS FOLLOW */
#include "bassline.c"                                /* the BASS track's phrase generator (FMX) */
#include "chopgen.c"                                 /* the CHOP track's SLICE (FMX) */
#include "q.c"                                       /* Q: changes on the bar (FMX) */

static void seq_step(track_t *t, const step_t *s, uint32_t slen, uint32_t skip)
{
    step_t cs;
    if (t == &trk[0] && bl_live()) {                /* KICKS, playing: the bass strikes on the BREAK's kicks */
        if (!(t->seq_idx % 16u) && bl_step_on(s))
            bl_strike(t->seq_idx);                  /* (and on each bar's first step) */
        return;
    }
    if (t == &trk[1] && cg.play == CP_HOLD)
        return;                                     /* (HOLD: the generator holds the chords, cg_block) */
    if (t == &trk[2]) {                             /* the CHOP: GAP held, DENSITY SONG (chopgen.c) */
        if (!(s = cs_step(t, s, &cs))) {
            seq_release(t);
            return;
        }
        if (s->time == ST_NOTE && s->n)
            cs_hit(s->note[0]);
    }
    if (t == &trk[1] && cg.play == CP_STEPS && s->time == ST_NOTE && s->n) {   /* STEPS: the chord of the moment */
        uint8_t n[6];
        uint32_t k = cg_step_notes(n), j;
        cs = *s;
        cs.n = (uint8_t)(k > 4u ? 4u : k);
        for (j = 0; j < cs.n; j++)
            cs.note[j] = n[j];
        s = &cs;
    } else if (t == &trk[0] && s->time == ST_NOTE && s->n && cg_follow()) {   /* BASS FOLLOW: with the chord's root */
        uint32_t j;
        int32_t d = cg_follow();
        cs = *s;
        for (j = 0; j < cs.n; j++)
            cs.note[j] = (uint8_t)clamp(bass_floor(cs.note[j] + d), 0, 127);   /* (never under E1: bassline.c) */
        s = &cs;
    }
    uint32_t i, j, gate = slen * (uint32_t)t->p[P_SGATE] / 128u;
    uint32_t slide_in = t->seq_hold && t->seq_n;
    uint32_t next_tie = t->step[(t->seq_idx + 1u) % trk_len(t)].time == ST_TIE;
    if (s->time == ST_TIE) {
        if (t->seq_n) {
            t->seq_off = gate + slen / 2u;
            t->seq_hold = (s->flags & SF_SLIDE) != 0 || next_tie;   /* chains hold at any GATE / swing */
        }
        return;
    }
    if (s->time == ST_REST || !s->n) {
        seq_release(t);
        return;
    }
    if (s->rat) {                                   /* ratchets: each hit its share of the step */
        uint32_t hits = 1u + ((s->rat >> 0) & 3u);
        for (i = 1; i < s->n; i++)
            if (1u + ((s->rat >> (2u * i)) & 3u) > hits)
                hits = 1u + ((s->rat >> (2u * i)) & 3u);
        gate /= hits;
    }
    t->slide_glide = (uint8_t)slide_in;
    if (!slide_in)
        seq_release(t);
    for (i = 0; i < s->n; i++)
        if (roll_has(t, s->note[i]))
            skip |= 1u << i;                        /* (a roll plays it) */
    for (i = 0; i < s->n; i++)
        if (!((skip >> i) & 1u)) {
            if (slide_in)
                trk_note_on(t, s->note[i], step_vel(s, i));
            else                                    /* (STRUM: a chord's notes one after the other) */
                trk_note_chord(t, s->note, s->n, i, step_vel(s, i));
            if (!slide_in || !(mo_set[trk_index(t) % NTRK][s->note[i] >> 5] & (1u << (s->note[i] & 31u))))
                seq_out_on(t, s->note[i], step_vel(s, i));   /* (a slide into the same note: one MIDI note) */
        }
    if (slide_in)                                   /* release what is not held over */
        for (i = 0; i < t->seq_n; i++) {
            for (j = 0; j < s->n && s->note[j] != t->seq_notes[i]; j++)
                ;
            if (j == s->n) {
                trk_note_off(t, t->seq_notes[i]);
                seq_out_off(t, t->seq_notes[i]);
            }
        }
    t->seq_n = 0;
    for (i = 0; i < s->n; i++)
        if (!((skip >> i) & 1u))
            t->seq_notes[t->seq_n++] = s->note[i];
    t->seq_off = gate;
    t->seq_hold = !s->rat && ((s->flags & SF_SLIDE) != 0 || next_tie);   /* next step a TIE: keep the notes to it */
}

/* ratchets: the further hits of the playing step's notes / lanes, each at its share of the step */
static void seq_ratchets(track_t *t, uint32_t into, uint32_t slen)
{
    uint32_t i;
    if (t->seq_skip)
        return;                                     /* (its condition failed: no hit at all) */
    if (is_brk(t))
        return;                                     /* (the BREAK track retriggers its slices itself: brkpart.c) */
    {
        const step_t *s = &t->step[t->seq_idx % NSTEP];
        if (s->time != ST_NOTE || !s->rat)
            return;
        for (i = 0; i < s->n; i++) {
            uint32_t hits = 1u + ((s->rat >> (2u * i)) & 3u), h;
            if (hits == 1u || roll_has(t, s->note[i]))
                continue;
            h = into * hits / slen;
            if (h > t->rat_done[i] && h < hits) {
                uint32_t j;
                t->rat_done[i] = (uint8_t)h;
                trk_note_off(t, s->note[i]);
                trk_note_on(t, s->note[i], step_vel(s, i));
                seq_out_on(t, s->note[i], step_vel(s, i));
                t->seq_off = slen / hits * (uint32_t)t->p[P_SGATE] / 128u;
                for (j = 0; j < t->seq_n && t->seq_notes[j] != s->note[i]; j++)
                    ;
                if (j == t->seq_n && t->seq_n < 4u)
                    t->seq_notes[t->seq_n++] = s->note[i];   /* (its gate ends it) */
            }
        }
    }
}

/* the nudge of grid step abs of track t, in units of a step slen long: where in its own step it fires
 * (micro >= 0), or how far before its step (micro < 0, as a negative number) */
static int32_t micro_units(const track_t *t, uint32_t abs, uint32_t slen)
{
    int32_t m = t->micro[abs % trk_len(t) % NSTEP];
    return (int32_t)(slen / 64u) * m;                /* |m| <= 32: fits */
}

/* the BREAK track: how late (clock units) step nabs fires in this block, after its nudged time (0: on time, or
 * early) */
static uint32_t brk_late_u(const track_t *t, uint32_t abs, uint32_t nabs, uint32_t into, uint32_t slen)
{
    int32_t mu = micro_units(t, nabs, slen);
    if (abs != nabs)
        return 0;
    return (int32_t)into > mu ? (uint32_t)((int32_t)into - (mu > 0 ? mu : 0)) : 0u;
}

/* The steps fire in order, one a block at most, each at its nudged time (micro: 1/64 of a step early
 * or late): the step after the last one played (seq_abs) is due when the grid is in its own step past
 * its nudge, or, nudged early, in the previous grid step past (length - |nudge|). So a step is never
 * skipped or played twice, whatever its neighbours' nudges (two that cross play in order, a block
 * apart), and a step nudged late past the next one's early nudge still plays first. Ratchets and
 * the recording stay on the grid (rec_target); seq_ratchets gets the time since the step fired. */
static void seq_tick(track_t *t, uint32_t adv)
{
    uint32_t len = trk_len(t), into, slen, abs, idx, nabs, fire = 0;
    int32_t rel;
    if (t->seq_n && !t->seq_hold) {
        if (t->seq_off <= adv)
            seq_release(t);
        else
            t->seq_off -= adv;
    }
    if (t == &trk[2])
        cs_tick(t);                                  /* the CHOP track: SLICE's bed, the keys held (chopgen.c) */
    if (!song.playing)
        return;
    abs = trk_grid(t, &into, &slen);
    {
        uint32_t div = trk_div(t);
        if (t->seq_abs != SEQ_NONE && div != t->seq_den)
            t->seq_abs = abs;                        /* DIV changed: the next step of the new grid plays */
        t->seq_den = (uint8_t)div;
    }
    if (t->seq_abs == SEQ_NONE) {                    /* PLAY: the step the grid is in (nudged late: once there) */
        nabs = abs;
        fire = micro_units(t, nabs, slen) <= (int32_t)into;
    } else {
        int32_t mu;
        nabs = t->seq_abs + 1u;
        mu = micro_units(t, nabs, slen);
        if (abs == nabs)
            fire = mu <= (int32_t)into;              /* its own step: past its nudge (early: due already) */
        else if (abs + 1u == nabs)
            fire = mu < 0 && (int32_t)slen + mu <= (int32_t)into;   /* the step before: nudged early into it */
        else if ((int32_t)(abs - nabs) > 0)
            fire = 1;                                /* the grid jumped ahead: catch up, a step a block */
        /* (abs + 1 == seq_abs: SWING turned up inside a played odd step: nothing until the grid is back) */
    }
    if (fire) {                                      /* a new step: one a block at most */
        uint32_t round = t->seq_abs != SEQ_NONE;     /* (not the step PLAY starts on) */
        t->seq_abs = nabs;
        idx = nabs % len;
        t->seq_idx = (uint16_t)idx;
        t->rat_done[0] = t->rat_done[1] = t->rat_done[2] = t->rat_done[3] = 0;
        if (!idx) {
            t->pass++;                               /* a new pass of the loop (recording: one undo) */
            if (t == &trk[0]) {
                blr.hits = 0;                        /* (the screen: this time round's strikes) */
                if (round)
                    bl_cycle();                      /* the loop came round: the BASS phrase changes, maybe */
            }
        }
        if (erasing(t))
            erase_step(t, idx);                      /* EDIT + key held: gone as it passes */
        t->seq_skip = (uint8_t)!step_plays(t, idx);
        if (t->seq_skip) {                           /* its fill condition fails: as a REST with no lock */
            lock_step(t, NSTEP);                     /* (no step has locks there: the bases are back) */
            t->rskip_n = 0;
            if (is_brk(t)) {
                seq_out_track_off(t);
                brk_seq_step(t, idx, nabs, brk_late_u(t, abs, nabs, into, slen), 0, 0);   /* (counted, silent) */
            } else {
                rec_hold(t, idx, len, nabs);
                seq_release(t);
            }
        } else if (is_brk(t)) {
            uint32_t hush = t->rskip_n && t->rskip_abs == nabs;   /* a key hit recorded into it sounds already */
            lock_step(t, idx);                       /* its parameter locks, before the block renders */
            t->rskip_n = 0;
            seq_out_track_off(t);
            if (t->bstep[idx].on && !hush)
                seq_out_on(t, 36u + (t->bstep[idx].slice ? t->bstep[idx].slice - 1u : 0u) % BRK_SLICES,
                           lvl_vel(t->bstep[idx].lvl, 100));
            {
                uint32_t k0 = bp.kicks_n;
                brk_seq_step(t, idx, nabs, brk_late_u(t, abs, nabs, into, slen), 1, (int)hush);
                if (bp.kicks_n != k0)
                    bl_on_kick();                    /* a kick: the BASS phrase strikes (KICKS, bassline.c) */
            }
        } else {
            const step_t *s = &t->step[idx];
            uint32_t skip = 0, i, k;
            lock_step(t, idx);
            rec_hold(t, idx, len, nabs);
            if (t->rskip_n && t->rskip_abs == nabs)
                for (i = 0; i < s->n; i++)
                    for (k = 0; k < t->rskip_n; k++)
                        if (s->note[i] == t->rskip[k])
                            skip |= 1u << i;
            t->rskip_n = 0;
            seq_step(t, s, slen, skip);
        }
    }
    /* the ratchets of the step playing, timed from where it fired (its hits ride with its nudge) */
    if (t->seq_abs == abs)
        rel = (int32_t)into - micro_units(t, abs, slen);
    else if (t->seq_abs == abs + 1u)
        rel = (int32_t)into - ((int32_t)slen + micro_units(t, abs + 1u, slen));
    else
        rel = (int32_t)into;
    if (t->seq_abs != SEQ_NONE)
        seq_ratchets(t, rel < 0 ? 0u : (uint32_t)rel, slen);
}

/* MIDI in: the track a channel plays (0..15): 1..4 the tracks, 10 (the GM drum channel) the BREAK track, the others
 * the selected one */
static track_t *midi_track(uint32_t ch)
{
    if (ch == 9u)
        return TBRK;
    return ch < NTRK ? &trk[ch] : TSEL;
}

/* a channel that plays the selected track: its note-off goes to the track its note-on went to,
 * even when another track was selected in between (else that note would hang) */
static uint8_t midi_sel_on[16][128];                  /* per channel and note: track + 1, 0 = none */
static track_t *midi_route(uint32_t ch, uint32_t note, int on)
{
    track_t *t = midi_track(ch);
    if (ch < NTRK || ch == 9u)
        return t;                                     /* a track's own channel, or the drum channel */
    if (on)
        midi_sel_on[ch & 15u][note & 127u] = (uint8_t)(song.sel + 1u);
    else if (midi_sel_on[ch & 15u][note & 127u]) {
        t = &trk[(midi_sel_on[ch & 15u][note & 127u] - 1u) % NTRK];
        midi_sel_on[ch & 15u][note & 127u] = 0;
    }
    return t;
}

/* SLOOP 2.5: MIDI CCs set track parameters, after Felucca 1.1.5's standard CC map (#103, Leo Kuroshita).
 * A CC acts on the track its channel plays, as the notes do (1-3 the synths, the drum channel the drum
 * track, 4-16 the selected track), and sets its parameter as a knob would: 0..127 over the parameter's
 * range, 64 the middle of a bipolar one. 5 GLIDE, 7 LEVEL, 10 PAN, 71 the engine's resonance (RES or Q;
 * an engine without one ignores it), 72 / 73 / 75 release / attack / decay, 74 the track's FILTER (64 off,
 * below a low-pass, above a high-pass: on every engine and the BREAK track), 91 / 93 / 94 the reverb, chorus and
 * delay sends. The BREAK track takes 7, 10, 74, 91 and 94. */
#define MCC_RES 0xFFu
static const uint8_t MIDI_CC_MAP[][2] = {
    {5, P_GLIDE}, {7, P_LEVEL}, {10, P_PAN}, {71, MCC_RES}, {72, P_REL}, {73, P_ATK}, {74, P_TFLT}, {75, P_DEC},
    {91, P_REV}, {93, P_CHOR}, {94, P_DLY},
};
static void __attribute__((noinline)) midi_cc(track_t *t, uint32_t cc, uint32_t value)
{
    const param_desc_t *d = 0;
    int16_t *slot = 0;
    uint32_t i, id = 0xFFFFu;
    for (i = 0; i < NELEM(MIDI_CC_MAP); i++)
        if (MIDI_CC_MAP[i][0] == cc)
            id = MIDI_CC_MAP[i][1];
    if (id == 0xFFFFu)
        return;
    if (is_brk(t)) {
        if (id == P_LEVEL || id == P_REV || id == P_DLY || id == P_PAN || id == P_TFLT) {
            d = &TP[id];
            slot = &t->p[id];
        }
    } else if (id == MCC_RES) {
        const engine_t *e = ENGINES[t->eng_req % NENGINES];
        for (i = 0; i < 8u && !d; i++)
            if (str_eq(e->edit[i].label, "RES") || str_eq(e->edit[i].label, "Q")) {
                d = &e->edit[i];
                slot = &t->p[P_E0 + i];
            }
    } else {
        d = &TP[id];
        slot = &t->p[id];
    }
    if (!d || d->max <= d->min)
        return;
    *slot = (int16_t)(d->min + ((int32_t)value * (d->max - d->min) + 63) / 127);
}

/* MIDI clock in (GLO > SYSTEM > SYNC = USB or TRS; after Felucca 1.0's midi_clock.c, from contributions by
 * ChanceTheMaker and keremimo): 24 pulses a beat. While the clock runs, the sequencer advances by the
 * pulses (a pulse = BEAT_U / 24 units), interpolated up to the next one from the last interval but never
 * past it, so it follows the master's tempo changes and cannot drift; BPM shows the master's tempo
 * (the slicer, delay and arp follow it). START restarts from the top, CONTINUE carries on where it
 * stopped, STOP stops. With no pulse for 0.5 s, the internal tempo takes over (PLAY works as ever). */
#define MCLK_PULSE_U (BEAT_U / 24u)
static struct {
    uint32_t pos, done;          /* units: the master's position (pulses since START), ours */
    uint32_t last_ms, iv_ms;     /* the last pulse, the interval between pulses (smoothed) */
    uint32_t beat_ms;            /* when pulse 0 of the last 24 came: the tempo */
    uint8_t have, n24;           /* a pulse since START; pulses towards the next tempo reading */
    uint8_t alive;               /* pulses are coming (from the SYNC source) */
} mclk;

static int mclk_on(void)                          /* the clock drives the sequencer */
{
    return song.g[G_SYNC] && mclk.alive && fm1_ms - mclk.last_ms < 500u;
}

static void mclk_event(uint32_t st, uint32_t src)  /* a realtime message; src 1 USB, 2 TRS */
{
    uint32_t now = fm1_ms;
    if (!song.g[G_SYNC] || src != (uint32_t)song.g[G_SYNC])
        return;
    if (st == 0xFAu || st == 0xFBu) {              /* START: from the top; CONTINUE: on from where it stopped */
        mclk.pos = mclk.done = 0;
        mclk.have = 0;
        if (st == 0xFAu)
            transport_req = 3;                     /* (not 1: the master counts, never a count-in) */
        else if (!song.playing)
            song.playing = 1;
        return;
    }
    if (st == 0xFCu) {                             /* STOP */
        transport_req = 2;
        return;
    }
    if (st != 0xF8u)
        return;
    if (mclk.alive && now - mclk.last_ms < 200u) { /* the interval, smoothed (a gap is not a tempo) */
        uint32_t iv = now - mclk.last_ms;
        mclk.iv_ms = mclk.iv_ms ? (mclk.iv_ms * 3u + iv + 2u) / 4u : iv;
    }
    if (!mclk.alive || now - mclk.last_ms >= 500u) {   /* (re)started: count a fresh beat */
        mclk.n24 = 0;
        mclk.beat_ms = now;
    } else if (++mclk.n24 == 24u) {                /* a beat: the tempo */
        uint32_t dt = now - mclk.beat_ms;
        mclk.n24 = 0;
        mclk.beat_ms = now;
        if (dt >= 250u && dt <= 1500u)             /* 40..240 BPM */
            song.g[G_BPM] = (int16_t)clamp((int32_t)((60000u + dt / 2u) / dt), 40, 240);
    }
    mclk.alive = 1;
    mclk.last_ms = now;
    if (song.playing || transport_req == 1u || transport_req == 3u) {   /* (a START queued with it: the next block starts) */
        if (mclk.have)
            mclk.pos += MCLK_PULSE_U;
        mclk.have = 1;                             /* the first pulse after START is the downbeat */
    }
}

static uint32_t mclk_adv(uint32_t n)               /* units to advance this block (mclk_on) */
{
    uint32_t el, off = 0, tgt, adv, cap = n * 2u * (uint32_t)song.g[G_BPM];
    if (!mclk.have)
        return 0;                                  /* START seen: wait for the downbeat */
    el = fm1_ms - mclk.last_ms;
    if (mclk.iv_ms) {
        if (el > mclk.iv_ms)
            el = mclk.iv_ms;
        off = el * MCLK_PULSE_U / mclk.iv_ms;      /* (<= 200 x 110250: fits 32 bits) */
        if (off >= MCLK_PULSE_U)
            off = MCLK_PULSE_U - 1u;
    }
    tgt = mclk.pos + off;
    adv = (int32_t)(tgt - mclk.done) > 0 ? tgt - mclk.done : 0u;
    if (adv > cap)
        adv = cap;                                 /* behind: catch up at twice the tempo, no burst */
    mclk.done += adv;
    return adv;
}

/* everything that happens between two rendered blocks: transport, input, the steps of every
 * track at the clock, the click, the rolls and the arps; then the clock moves on by n samples */
static void events_block(uint32_t n)
{
    uint32_t i, pr, adv;
    if (mo_any && !song.g[G_MIDI]) {            /* MIDI = KEYS again: end what the sequencer had sent */
        seq_out_all_off();
        mo_any = 0;
    }
    if (transport_req == 1u || transport_req == 3u) {
        uint32_t ext = transport_req == 3u;         /* a MIDI START: the master counts; cut a count-in short */
        transport_req = 0;
#if FELUCCA_ARRANGER
        if (ext && !live_on && !song.playing)
            arrangement_enabled = 0;                /* (STUDIO plays the loop: ui_input.c studio_play) */
#endif
        if (ext && ci_on)
            ci_on = 0;
        if (ft_on) {
            ft_close();                             /* (PLAY from elsewhere: the editor) */
        } else if (ci_on) {
            ci_on = 0;                              /* PLAY again during the count-in: back to armed */
        } else if (!ext && rec_wait && rec_count && !song.playing && !mclk_on() &&
                   !(project_empty() && !rec_tempo)) {
            ci_on = 1;                              /* COUNT: one bar of clicks first (below) */
            ci_u = 0;
            ci_beat = 0;
            click_on(1);
        } else {
            seq_start();
            if (rec_wait && song.playing)
                rec_begin();                        /* PLAY while armed: record from the top */
        }
    } else if (transport_req == 2u) {
        seq_stop();
        transport_req = 0;
        song.rec = 0;                               /* STOP ends the take (and the wait) */
        rec_wait = 0;
        if (ft_on) {
            ft_on = 0;                              /* a free take: dropped */
            ft_bars = 0xFF;
        }
    }
    if (ci_on) {                                    /* the count-in: 4 beats at the tempo, then go */
        if (!rec_wait || song.playing) {
            ci_on = 0;                              /* (REC cancelled it, or it started some other way) */
        } else {
            uint32_t b;
            ci_u += n * (uint32_t)song.g[G_BPM];
            b = ci_u / BEAT_U;
            if (b >= 4u) {
                ci_on = 0;
                seq_start();
                if (song.playing)
                    rec_begin();
            } else if (b != ci_beat) {
                ci_beat = (uint8_t)b;
                click_on(0);
            }
        }
    }
    if (rec_wait && song.playing)
        rec_begin();                                /* started some other way: record now */
    adv = mclk_on() && song.playing ? mclk_adv(n) : n * (uint32_t)song.g[G_BPM];   /* (after a START) */
    ft_block();
#if FELUCCA_ARRANGER
    if (song.playing && arrangement_clock.running) {
        uint32_t was = arrangement_clock.phase;     /* (a bar crossed: arr_next takes the period off) */
        int scene = arr_next(&arrangement_clock, &arrangement, FS);
        if (scene == ARR_DONE) {
            arrangement_clock.running = 1;          /* (arr_next cleared it: seq_stop brings the loop back) */
            if (as_swap)
                as_swap_now();                      /* (LIVE, AUTO: the set goes on, the next song) */
            else
                seq_stop();
        }
        else if (scene >= 0) {
            arrangement_apply((uint32_t)scene);
            seq_reset_tracks(arrangement_clock.phase);   /* (the remainder: exactly on the bar) */
        }
        if (as_swap && song.playing && arrangement_clock.phase < was)
            as_swap_now();                          /* (LIVE: the next song, on this bar) */
        if (song.playing && arrangement_clock.phase < was)
            as_tr_bar();                            /* (LIVE: the way to it, bar by bar) */
    }
    as_block();
#endif
    q_block();                                      /* Q: what waits, on its boundary (q.c: before fill_last_bar moves) */
    if (song.playing && !(clk_beat & 3u) && (clk_beat >> 2) != fill_last_bar) {   /* a new bar: the armed fill bar
                                                                                    * (after a section: its bar 0) */
        fill_last_bar = clk_beat >> 2;
        fill_bar_on = fill_arm;
        fill_arm = 0;
    }
    fill_now = (uint8_t)(fill_held || fill_bar_on || ((perf.on >> PA_FILL) & 1u));   /* (+ the moves' FILL: AUTO SONG) */
    pr = panic_req;
    panic_req = 0;
    for (i = 0; i < NTRK; i++) {
        track_t *t = &trk[i];
        if ((pr >> i) & 1u) {
            trk_all_off(t);
        }
        if (i < NPART)
            engine_block(t);                          /* engine switch: fade, then switch (voice.c) */
    }
    keyboard_block();
    strum_block(n);                                   /* (voice.c: the strummed notes due) */
    while (mi_r != mi_w) {                            /* USB-MIDI (and TRS) in */
        uint32_t pkt = midi_in_q[mi_r % MQ], st = (pkt >> 8) & 0xF0u, ch = (pkt >> 8) & 0x0Fu;
        uint32_t d1 = (pkt >> 16) & 0x7Fu, d2 = (pkt >> 24) & 0x7Fu;
        track_t *t;
        mi_r++;
        if ((pkt & 15u) == 0xFu) {                    /* clock / transport: cable 0 USB, 1 TRS */
            mclk_event((pkt >> 8) & 0xFFu, ((pkt >> 4) & 15u) ? 2u : 1u);
            continue;
        }
        if (st == 0xB0u) {                            /* a CC (IN = CLOCK: none) */
            if (!song.g[G_ROUTE])
                midi_cc(midi_track(ch), d1, d2);
            continue;
        }
        if (st != 0x90u && st != 0x80u)
            continue;
        if (song.g[G_ROUTE] && st == 0x90u && d2)
            continue;                                 /* GLO > SYSTEM > IN = CLOCK: no notes (the note-offs still
                                                       * end what was held when it was set) */
        t = midi_route(ch, d1, st == 0x90u && d2);
        if (is_brk(t)) {
            if (st == 0x90u && d2)
                brk_input(brk_slice_of_note(d1), vel_lvl(d2), 0, 1);
        } else if (st == 0x90u && d2) {
            input_on(t, d1, d2);
        } else {
            input_off(t, d1);
        }
    }
    for (i = 0; i < NTRK; i++)
        seq_tick(&trk[i], adv);
    brk_block();                                      /* the BREAK track: the transport stopped */
    cg_block();                                       /* the CHORDS generator: live nodes, HOLD's chords */
    click_tick();
    roll_block(adv);
    if (song.playing) {
        song.tick++;
        clk_pos += adv;
        while (clk_pos >= BEAT_U) {
            clk_pos -= BEAT_U;
            clk_beat++;
        }
#if FELUCCA_ARRANGER
        arr_elapse(&arrangement_clock, adv, 1u);   /* (units: n x BPM, or the MIDI clock) */
#endif
    }
}
