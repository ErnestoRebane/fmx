/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Felucca user interface. Four columns map to KNOB 1..4. Rendering is lazy:
 * every element remembers what it last drew and is redrawn only on change. */
#ifndef FELUCCA_VERSION
#define FELUCCA_VERSION "FMX 1.0.0_beta"  /* breakcore / jungle firmware for the FM-1 (based on SLOOP 2.5 and Felucca) */
#endif
static void project_save(uint32_t slot);
static void arrangement_save(void);
static void panel_setup(void);
static void project_load(uint32_t slot);
static int project_used(uint32_t slot);
static int up_used(uint32_t k);              /* user presets: upreset.c */
static int up_load(uint32_t k);
static uint32_t up_count(void);
static uint32_t up_nth(uint32_t n);
static uint32_t up_rank(uint32_t slot);
static void up_name(uint32_t k, char *b);
static void up_slot_label(char *b, uint32_t k);
static void up_ui(uint32_t op, uint32_t k);
static uint32_t user_of(const track_t *t)    /* user preset slot its sound came from, UP_SLOTS = none */
{
    return t->user && up_used(t->user - 1u) ? t->user - 1u : UP_SLOTS;
}
static uint32_t up_gen;                      /* bumped on every user bank change (redraws) */
static uint8_t sync_reload;                  /* engine / preset / project / user preset loaded: editor RELOAD push */

#define ACC C_HI                   /* amber everywhere; white is the only accent */
#define VAL(c) ((c) == ui.hot_col && ui.hot_t ? C_WHITE : TE_COL[(c) & 3u])   /* LIVE: one colour per knob */
#define RATIO(d, v) ((d)->max > (d)->min ? ((int32_t)(v) - (d)->min) * 1000 / ((d)->max - (d)->min) : -1)
/* layout: four 60 px columns, 4 px inset */
/* Terminus 8x16 (S) and 16x32 (L) */
#define Y_HEAD 0
#define H_HEAD 20
#define Y_LABEL 26
#define Y_VALUE 44
#define Y_GAUGE 64
#define Y_SEP_END 70
#define Y_GRAPH 54
#define H_GRAPH 124
#define G_OY 24                       /* graphs draw in the lower 100 px; the focus readout sits on top */
#define Y_FOOT 202
#define H_FOOT 38

static struct {
    uint8_t home;
    uint8_t page;                /* index into PAGES */
    uint8_t fam_last[FAM_COUNT]; /* last page used per family */
    uint8_t bank;                /* SEQ: 16-step bank (follows the cursor) */
    uint8_t cursor;              /* SEQ: step being edited (STEP page KNOB 1 moves it) */
    uint8_t entry_open;          /* SEQ: keys held since the first press of this entry */
    uint8_t hot_col, hot_t;      /* column whose knob was just turned (drawn white) */
    uint8_t menu;                /* 0 off, 1 list, 2 about (HOME held) */
    uint8_t menu_sel;
    uint32_t menu_sig, home_t0;  /* HOME press time (btn_hold) */
    uint8_t force;               /* full redraw pending */
    uint8_t msg_t;               /* transient message frames */
    uint8_t bpm_t;               /* frames the BPM stays highlighted after a SELECT turn */
    uint8_t arm, arm_t;          /* destructive action armed: param id, frames left to confirm */
    uint32_t rec_t0;             /* REC press time (btn_hold) */
    uint8_t confirm;             /* 1 = "clear the sequence?" (REC held on SEQ / ARP), 2 = "clear track n?" (TRACKS) */
    uint8_t confirm_trk;         /* the track the dialog clears */
    uint8_t uslot;               /* SAVE > USER: the selected user preset slot */
    /* layers (ui_layers.c): a function button held, the keys and knobs do something else */
    uint8_t layer;               /* the layer drawn (LY_*), LY_PLAY = none */
    uint8_t layer_btn;           /* its button (B_*), NB = none */
    uint8_t layer_used;          /* a key / knob / OCT was used while it was held: no tap on release */
    uint32_t layer_t0;           /* its press time (ms) */
    uint8_t step_page;           /* SEQ layer: the 16 steps shown (page x 16) */
    uint16_t step_held;          /* SEQ layer: the step keys held (white key index) */
    uint32_t step_sess;          /* SEQ layer: the undo session of this hold */
    uint8_t lock_par;            /* SEQ layer, a step held + KNOB 4: the parameter it locks (the last sound
                                  * parameter a knob changed on a page of the selected track; P_ED_FLT at boot) */
    uint8_t hold_kind;           /* a hold to confirm: 1 = clear the track (REC), 2 = save (SAVE) */
    uint32_t hold_t0;            /* (ms) */
    uint8_t hold_trk;
    uint32_t tap_ms[4];          /* tap tempo: the last taps */
    uint8_t tap_n;
    char msg[24];
    uint32_t enc_t[NE];
    /* drawn-state cache */
    char col[4][32];
    char focus_l[8], focus_v[8], focus_u[8];   /* the touched column, shown large */
    char big_l[4][8], big_v[4][10], big_u[4][8];   /* the four columns, for the big values (2.4: pages without a graph) */
    uint16_t big_c[4];
    uint32_t graph_sig, head_sig, foot_sig, frame;
    uint8_t graph_top;           /* the graph strip's top G_OY rows hold something */
} ui;

static const page_t *cur_page(void) { return &PAGES[ui.page]; }
static int32_t accel(uint32_t role, int32_t s, int32_t range);   /* ui_input.c */
static void layer_screen_draw(void);                            /* ui_layers.c */
static void hold_screen_draw(void);
static uint8_t layer_shown;

static uint32_t page_first(uint32_t fam)
{
    uint32_t i;
    for (i = 0; i < NPAGES; i++)
        if (PAGES[i].fam == fam)
            return i;
    return 0;
}

/* transient message in the top bar: a + b */
static void ui_say(const char *a, const char *b)
{
    uint32_t n;
    str_cpy(ui.msg, a, sizeof ui.msg);
    n = str_len(ui.msg);
    str_cpy(ui.msg + n, b, sizeof ui.msg - n);
    ui.msg_t = 40;
}

static void ui_message(const char *s) { ui_say(s, ""); }

static void page_entered(void)
{
    const page_t *pg = cur_page();
    song.seq_mode = !ui.home && pg->fam == FAM_SEQ;
    ui.entry_open = 0;
    ui.hot_t = 0;                                /* the white value / focus box was the old page's */
    ui.force = 1;
}

static int step_on(const step_t *st) { return st->time == ST_NOTE && st->n; }
/* step i of track t has something to play (synth: notes, BREAK: ON) */
static int trk_step_on(const track_t *t, uint32_t i)
{
    return is_brk(t) ? t->bstep[i % NSTEP].on != 0u : step_on(&t->step[i % NSTEP]);
}

static void step_clear(step_t *st)
{
    memset(st, 0, sizeof *st);
    st->time = ST_REST;
}

/* undo / redo (EDIT + OCT- / OCT+): the marked pattern and the one now swap places */
static int undo_swap(int redo)
{
    track_t *t;
    int16_t len;
    if (!undo.valid || (uint32_t)!!redo != undo.undone)
        return 0;
    t = &trk[undo.trk % NTRK];
    fm1_irq_off();
    {
        uint32_t i;
        for (i = 0; i < NSTEP; i++) {
            step_t x = t->step[i];
            t->step[i] = undo.st[i];
            undo.st[i] = x;
        }
    }
    len = t->p[P_SLEN];
    t->p[P_SLEN] = undo.len;
    undo.len = len;
    undo.undone = (uint8_t)!redo;
    fm1_irq_on();
    sync_reload = 1;
    ui.force = 1;
    return 1;
}

/* SEQ cursor: wraps inside the pattern length, the bank follows, a step entry ends */
static void cursor_set(int32_t c)
{
    int32_t len = TSEL->p[P_SLEN] > 0 ? TSEL->p[P_SLEN] : 1;
    ui.cursor = (uint8_t)((c % len + len) % len);
    ui.bank = (uint8_t)(ui.cursor / 16u);
    ui.entry_open = 0;
}

static void cursor_fix(void)                           /* LEN got shorter: onto the last step */
{
    if (ui.cursor >= (uint32_t)TSEL->p[P_SLEN])
        cursor_set(TSEL->p[P_SLEN] - 1);
}

static void note_name(char *b, uint32_t n)
{
    str_cpy(b, N_NOTE[n % 12u], 4);
    fmt_int(b + str_len(b), (int32_t)(n / 12u) - 1);
}

static void open_family(uint32_t fam)
{
    if (!ui.home && cur_page()->fam == fam) {          /* same button again: next page */
        uint32_t i = ui.page + 1u;
        if (i >= NPAGES || PAGES[i].fam != fam)
            i = page_first(fam);
        ui.page = (uint8_t)i;
    } else {
        ui.page = ui.fam_last[fam] && PAGES[ui.fam_last[fam]].fam == fam ? ui.fam_last[fam]
                                                                          : (uint8_t)page_first(fam);
    }
    ui.fam_last[fam] = ui.page;
    ui.home = 0;
    page_entered();
}

/* SELECT on a page: the previous / next page of its family, as tapping the button again but both ways,
 * stopping at the ends. Not the screens of their own (SONG, BREAK) nor a family of one page (TRACKS):
 * 0 then, and SELECT is the tempo there */
static int page_walk(int32_t s)
{
    const page_t *pg = cur_page();
    uint32_t i, fam = pg->fam, n = 0;
    int32_t cur = -1, to;
    uint8_t idx[8];
    if (ui.home || pg->scope >= SC_SONG)                 /* (the screens: SONG, BREAK, CHORDS, CHOP, BASS) */
        return 0;
    for (i = 0; i < NPAGES && n < 8u; i++) {
        if (PAGES[i].fam != fam || PAGES[i].scope >= SC_SONG)
            continue;
        if (i == ui.page)
            cur = (int32_t)n;
        idx[n++] = (uint8_t)i;
    }
    if (n < 2u || cur < 0)
        return 0;
    to = clamp(cur + s, 0, (int32_t)n - 1);
    if (to != cur) {
        ui.page = idx[to];
        ui.fam_last[fam] = ui.page;
        page_entered();
    }
    return 1;
}

static void go_home(void)
{
#if FELUCCA_ARRANGER
    ui.home = 0;
    ui.page = (uint8_t)page_first(FAM_TRK);
#else
    ui.home = 1;
#endif
    ui.entry_open = 0;
    ui.hot_t = 0;
    song.seq_mode = 0;
    ui.force = 1;
}

/* ------------------------------------------------------- track setup --- */
/* LIVE: no factory sequence patterns. Loading a sound (factory or user preset) never
 * writes the sequencer: every pattern is the one the player records or enters. */

/* the parts' sounds at power-on (engine, preset): BASS, CHORDS, CHOP */
static const uint8_t TRK_DEF[NPART][2] = {{ENGI_ANALOG, 22}, {ENGI_SAMPLE, 0}, {ENGI_SAMPLE, 8}};   /* REESE SUB, PSX PAD, CHOP KIT (a kit pack) */
static uint32_t trk_def_engine(uint32_t i) { return i < NPART ? TRK_DEF[i][0] : 0u; }

static void track_defaults_steps(track_t *t) { steps_clear(t); }

/* what loading a sound (factory or user preset) leaves alone: the mix (LEVEL, PAN, MUTE:
 * the TRACKS faders, and the track FILTER), the pattern parameters (LEN, DIV, SWING, GATE) and the key the part plays
 * in (ROOT, SCALE, QNT, CHORD: the song's; SEL + key sets the root of every part). The SLICER is
 * part of the sound: a factory preset turns it OFF (its defaults), a user preset brings its own */
static int param_kept(uint32_t i)
{
    return i == P_LEVEL || i == P_PAN || i == P_MUTE || (i >= P_SLEN && i <= P_SGATE) ||
           (i >= P_ROOT && i <= P_QUANT) || (i >= P_X0 && i <= P_X7) || i == P_CHORD || i == P_TFLT || i == P_STRUM || i == P_VLEAD;
}

/* preset pi of the engine the track asked for: the whole sound (not the pattern parameters) */
/* preset pi of engine ei into a track's parameters p (as apply_preset_to: the sound, not the pattern, not the mix) */
static void preset_fill(int16_t *p, uint32_t ei, uint32_t pi)
{
    const engine_t *e = ENGINES[ei % NENGINES];
    const preset_t *pr;
    uint32_t i;
    if (!e->npresets)
        return;
    pr = &e->presets[pi % e->npresets];
    for (i = 0; i < P_E0; i++)                        /* the rest of the sound to its defaults: a preset */
        if (!param_kept(i))
            p[i] = TP[i].def;                        /* sounds the same after any edit (not the pattern, not the mix) */
    for (i = 0; i < 8u; i++)
        p[P_E0 + i] = (int16_t)pr->e[i];
    if (e == &ENG_SAMPLE && pi % e->npresets < sizeof SMP_PRESET_PACK / sizeof SMP_PRESET_PACK[0] &&
        SMP_PRESET_PACK[pi % e->npresets])
        p[P_E0] = (int16_t)smp_pack_slot(SMP_PRESET_PACK[pi % e->npresets]);   /* a preset on a sound pack: its slot */
    p[P_ATK] = pr->env[0];
    p[P_DEC] = pr->env[1];
    p[P_SUS] = pr->env[2];
    p[P_REL] = pr->env[3];
    p[P_ED_FLT] = pr->fenv;
    p[P_ED_FX] = preset_trim(ei % NENGINES, pi % e->npresets);   /* level-matched (tools/level_presets.py) */
    p[P_VOICE] = pr->mono ? V_LEGATO : V_POLY;      /* mono presets keep the legato feel */
    {   /* the rest of the patch: the sends (never a pattern: LIVE) */
        static const uint8_t FX_DEF[4] = {0, 24, 28, 36};
        for (i = 0; i < 4u; i++) {
            p[P_DIST + i] = (int16_t)(pr->fx[i] ? pr->fx[i] - 1 : FX_DEF[i]);
        }
        preset_extras(p, pr);                        /* glide, pitch / LFO modulation, voice mode */
    }
}
static void apply_preset_to(track_t *t, uint32_t pi)
{
    const engine_t *e = ENGINES[t->eng_req % NENGINES];
    if (is_brk(t))
        return;
    panic_req |= (uint8_t)(1u << trk_index(t));       /* MONO/POLY may change: release what sounds */
    t->user = 0;
    if (t == TSEL)
        sync_reload = 1;
    if (!e->npresets)
        return;
    t->preset = (uint8_t)(pi % e->npresets);
    preset_fill(t->p, t->eng_req % NENGINES, pi);
}
/* Q (q.c): preset pi of engine ei for track t, on the boundary (its engine too, its voices released then) */
static void q_preset(track_t *t, uint32_t ei, uint32_t pi)
{
    static int16_t p[P_COUNT];
    const engine_t *e = ENGINES[ei % NENGINES];
    uint32_t k = trk_index(t), i;
    if (!e->npresets)
        return;
    for (i = 0; i < P_COUNT; i++)
        p[i] = q_val(k, i);
    if (ei % NENGINES != t->eng_req)                  /* another engine: its defaults first (set_engine_of) */
        for (i = 0; i < 8u; i++)
            p[P_E0 + i] = e->edit[i].def;
    preset_fill(p, ei, pi);
    q_params(k, p);
    q_param(k, QX_ENG, (int32_t)(ei % NENGINES));
    q_param(k, QX_PRESET, (int32_t)(pi % e->npresets));
    qq.panic |= (uint8_t)(1u << k);
    q_name(k, e->presets[pi % e->npresets].name);
}

/* the engine's defaults and its first preset. With the audio IRQ off: the ISR sees the old engine with
 * its values or the new one with its own (voice.c engine_block), never one with the other's */
static void set_engine_of(track_t *t, uint32_t ei)
{
    const engine_t *e = ENGINES[ei % NENGINES];
    uint32_t i;
    if (is_brk(t))
        return;
    fm1_irq_off();
    locks_restore(t);                                 /* (a lock in force: its base first, then the new sound) */
    t->eng_req = (uint8_t)(ei % NENGINES);
    for (i = 0; i < 8u; i++)
        t->p[P_E0 + i] = e->edit[i].def;
    apply_preset_to(t, 0);
    fm1_irq_on();
}

static void apply_preset(uint32_t pi) { apply_preset_to(TSEL, pi); }
static void set_engine(uint32_t ei) { set_engine_of(TSEL, ei); }

static void track_defaults(track_t *t)
{
    uint32_t i;
    for (i = 0; i < P_E0; i++)
        t->p[i] = TP[i].def;
    track_defaults_steps(t);
    if (is_brk(t))
        brk_defaults(t);                              /* its generator's parameters, every step on */
}

/* switch engine (its defaults + first preset) and say so */
static void select_engine(uint32_t e)
{
    if (is_brk(TSEL))
        return;
    set_engine(e);
    ui_say("ENGINE ", ENGINES[TSEL->eng_req]->name);
    ui.force = 1;
}

/* the factory presets as one list by kind (basses, keys, organs, pads, leads, plucks and bells, stabs,
 * the rest), then the used user presets: the PRESETS knob and the PRESETS page browse it. By name: an
 * engine's preset table may change order; tests/ui_pages_test.c checks every preset is here once */
enum { BK_BASS, BK_KEYS, BK_ORGAN, BK_PAD, BK_LEAD, BK_PLUCK, BK_STAB, BK_FX, BK_KIT };
static const char *const BANK_KIND[] = {"BASS", "KEYS", "ORGN", "PAD", "LEAD", "PLCK", "STAB", "FX", "KIT"};
static const struct { uint8_t kind, e; const char *name; } BANK[] = {
    {BK_KIT, ENGI_SAMPLE, "CHOP KIT"},
    {BK_PAD, ENGI_SAMPLE, "PSX PAD"}, {BK_PAD, ENGI_SAMPLE, "PSX CHOIR"}, {BK_PAD, ENGI_SAMPLE, "PSX GLASS"},
    {BK_PAD, ENGI_SAMPLE, "PSX DARK"},
    /* FMX, the pads of docs/pads.md: from the breath and the glass to the moan and the 8-bit strings */
    {BK_PAD, ENGI_SAMPLE, "SOUNDTRK"}, {BK_PAD, ENGI_SAMPLE, "GLASS VOX"}, {BK_PAD, ENGI_SAMPLE, "CRYSTAL"},
    {BK_PAD, ENGI_SAMPLE, "UNIVERSE"}, {BK_PAD, ENGI_SAMPLE, "TIBET"}, {BK_PAD, ENGI_SAMPLE, "STRETCH"},
    {BK_PAD, ENGI_SAMPLE, "DARKSIDE"}, {BK_PAD, ENGI_SAMPLE, "DETROIT"}, {BK_PAD, ENGI_ANALOG, "BLUE PAD"},
    {BK_ORGAN, ENGI_SAMPLE, "RAVE ORGAN"},
    {BK_BASS, ENGI_ANALOG, "REESE SUB"}, {BK_BASS, ENGI_ANALOG, "DARK REESE"}, {BK_BASS, ENGI_ANALOG, "JUNGLE SUB"},
    {BK_BASS, ENGI_ANALOG, "VALVE SUB"}, {BK_BASS, ENGI_ANALOG, "808 JUNGLE"}, {BK_BASS, ENGI_ANALOG, "DUB BASS"},
    {BK_BASS, ENGI_ANALOG, "HOOVER"}, {BK_BASS, ENGI_ANALOG, "DRED BASS"},
    {BK_BASS, ENGI_ANALOG, "808 BOOM"}, {BK_BASS, ENGI_ANALOG, "808 DIRTY"},
    {BK_BASS, ENGI_ANALOG, "808 SLIDE"}, {BK_BASS, ENGI_ANALOG, "SUB BASS"},
    {BK_BASS, ENGI_ANALOG, "PLUGG BASS"}, {BK_BASS, ENGI_ANALOG, "REESE"}, {BK_BASS, ENGI_ANALOG, "WOBBLE"},
    {BK_BASS, ENGI_ANALOG, "ACID 303"}, {BK_BASS, ENGI_ANALOG, "FUNK BASS"}, {BK_BASS, ENGI_ANALOG, "MONO BASS"},
    {BK_BASS, ENGI_ANALOG, "SAW BASS"}, {BK_KEYS, ENGI_SAMPLE, "GRAND PNO"},
    {BK_KEYS, ENGI_SAMPLE, "DUSTY PNO"}, {BK_KEYS, ENGI_SAMPLE, "LOFI KEYS"}, {BK_KEYS, ENGI_SAMPLE, "JAZZ EP"},
    {BK_PAD, ENGI_ANALOG, "WARM PAD"},
    {BK_PAD, ENGI_ANALOG, "DARK STR"}, {BK_PAD, ENGI_ANALOG, "ATMOS PAD"}, {BK_PAD, ENGI_ANALOG, "SINE PAD"},
    /* FMX, the CHOP's INST leads (docs/leads.md): rave, J-core, the game consoles, acid, liquid, phonk */
    {BK_LEAD, ENGI_ANALOG, "HOOVER LD"}, {BK_LEAD, ENGI_ANALOG, "J-CORE SAW"}, {BK_LEAD, ENGI_ANALOG, "CHIP PULSE"},
    {BK_LEAD, ENGI_ANALOG, "GAME SQR"}, {BK_LEAD, ENGI_ANALOG, "16BIT FM"}, {BK_LEAD, ENGI_ANALOG, "SYNC LEAD"},
    {BK_LEAD, ENGI_ANALOG, "ACID LEAD"}, {BK_LEAD, ENGI_ANALOG, "JUNO LEAD"}, {BK_LEAD, ENGI_ANALOG, "LIQUID FLT"},
    {BK_LEAD, ENGI_ANALOG, "FM BELL"}, {BK_LEAD, ENGI_ANALOG, "COWBELL"},
#if FELUCCA_SLICE
    {BK_FX, ENGI_SLICE, "BREAK 16"}, {BK_FX, ENGI_SLICE, "CHOP 8"}, {BK_FX, ENGI_SLICE, "REVERSE"},
    {BK_FX, ENGI_SLICE, "USR SLICE"},
#endif
};
#define NBANK (sizeof BANK / sizeof BANK[0])
static uint8_t bank_pi[NBANK];                       /* the preset index of each entry in its engine */
static uint8_t bank_ready;
static void bank_resolve(void)
{
    uint32_t i, k;
    for (i = 0; i < NBANK; i++) {
        const engine_t *e = ENGINES[BANK[i].e % NENGINES];
        bank_pi[i] = 0xFF;
        for (k = 0; k < e->npresets; k++)
            if (str_eq(e->presets[k].name, BANK[i].name))
                bank_pi[i] = (uint8_t)k;
    }
    bank_ready = 1;
}
/* FMX: each part browses the kinds it is for: BASS the basses, CHORDS the pads, keys, organs, the stabs and the rest,
 * CHOP its kits and, in INST, the leads only (docs/leads.md); the user presets on every part */
static const uint16_t BANK_KINDS[NPART] = {1u << BK_BASS,
                                          1u << BK_PAD | 1u << BK_KEYS | 1u << BK_ORGAN | 1u << BK_STAB | 1u << BK_FX,
                                          1u << BK_KIT | 1u << BK_LEAD};
static int bank_for(uint32_t i)
{
    if (song.sel == CS_TRK && BANK[i].kind == BK_KIT)
        return 0;                                    /* (the CHOP's kits: chop_kits) */
    return song.sel < NPART && ((BANK_KINDS[song.sel] >> BANK[i].kind) & 1u);
}
/* FMX: the CHOP track in PADS or SLICE browses its kits (the built-in ones and the pool's: a kit, or a loop the loop
 * page cut) and nothing else; in INST the instruments of BANK. Every place PRESETS turns goes through preset_pos /
 * preset_go, so LIVE, the DECK, the PRESETS page and the CHOP screen agree */
#define PRESET_KIT (NENGINES + 1u)                   /* preset_at: a kit, *k its SET */
static int chop_kits(void) { return song.sel == CS_TRK && cs_mode() != CM_INST; }
static uint32_t kit_nth(uint32_t n, uint32_t *count) /* the n-th kit's SET (n past them: the count in *count) */
{
    uint32_t s, b, nh, c = 0, at = 0;
    for (s = 0; s < SMP_NALL; s++)
        if (smp_kit(s, &b, &nh)) {
            if (c == n)
                at = s;
            c++;
        }
    *count = c;
    return at;
}
static uint32_t bank_nvis(void)                      /* the factory entries the selected part browses */
{
    uint32_t i, n = 0;
    for (i = 0; i < NBANK; i++)
        n += (uint32_t)bank_for(i);
    return n;
}
static uint32_t bank_vis(uint32_t n)                 /* its n-th one -> the BANK index */
{
    uint32_t i;
    for (i = 0; i < NBANK; i++)
        if (bank_for(i) && !n--)
            return i;
    return 0;
}
static uint32_t preset_pos(uint32_t *total)          /* list index of the selected track's preset */
{
    uint32_t i, cur = 0, n = 0, nv = bank_nvis(), te = TSEL->eng_req, tp = TSEL->preset, user = user_of(TSEL) < UP_SLOTS;
    int16_t pe, pp;
    if (q_pend(song.sel, QX_ENG, &pe) && q_pend(song.sel, QX_PRESET, &pp)) {   /* Q: from the last turn, not from
                                                                                 * the sound that plays till the bar */
        te = (uint32_t)pe;
        tp = (uint32_t)pp;
        user = 0;
    }
    if (chop_kits()) {                               /* (the CHOP's kits: the one playing, or waiting for Q) */
        uint32_t s = (uint32_t)q_val(CS_TRK, P_E0) % SMP_NALL, b, nh;
        for (i = 0; i < s; i++)
            n += (uint32_t)smp_kit(i, &b, &nh);
        kit_nth(0, total);
        return n < *total ? n : 0u;
    }
    if (!bank_ready)
        bank_resolve();
    for (i = 0; i < NBANK; i++)
        if (bank_for(i)) {
            if (BANK[i].e == te && bank_pi[i] == tp)
                cur = n;
            n++;
        }
    if (user)
        cur = nv + up_rank(user_of(TSEL));
    *total = nv + up_count();
    return cur;
}

/* list index n (< total) -> engine, *k its preset; NENGINES = user preset, *k its slot */
static uint32_t preset_at(uint32_t n, uint32_t *k)
{
    if (chop_kits()) {
        uint32_t c;
        *k = kit_nth(n, &c);
        return PRESET_KIT;
    }
    if (!bank_ready)
        bank_resolve();
    if (n >= bank_nvis()) {
        *k = up_nth(n - bank_nvis());
        return NENGINES;
    }
    n = bank_vis(n);
    *k = bank_pi[n] == 0xFF ? 0u : bank_pi[n];
    return BANK[n].e;
}
static const char *preset_kind(uint32_t n)
{
    if (chop_kits())
        return cs_slice() ? "LOOP" : "KIT";
    return n < bank_nvis() ? BANK_KIND[BANK[bank_vis(n)].kind] : "USER";
}
static void chop_kit_go(uint32_t set);               /* (ui_chop.c) */

static void preset_go(uint32_t n)                    /* load list index n into the selected track */
{
    uint32_t k, e = preset_at(n, &k);
    if (is_brk(TSEL))
        return;                                      /* (the BREAK track's loops: PRESETS on its screen) */
    if (e == PRESET_KIT) {
        chop_kit_go(k);
        return;
    }
    if (e == NENGINES) {
        q_drop(song.sel);                            /* (a preset waiting for the bar would throw it over) */
        up_load(k);
        song_keep_now(song.sel, 0);                  /* (song mode: the next section and STOP keep it) */
        return;
    }
    if (q_on()) {                                    /* Q: on the bar (q.c) */
        char w[16];
        q_preset(TSEL, e, k);
        w[0] = '@';
        fmt_int(w + 1, (int32_t)q_bar_at());
        str_cpy(w + str_len(w), " ", 2);
        ui_say(w, ENGINES[e % NENGINES]->presets[k % ENGINES[e % NENGINES]->npresets].name);   /* "@3 DARK REESE" */
        ui.force = 1;
        return;
    }
    if (e != TSEL->eng_req)
        select_engine(e);
    apply_preset(k);
    song_keep_now(song.sel, 0);                      /* (song mode: the next section and STOP keep it) */
    ui_say("", ENGINES[e % NENGINES]->presets[k % ENGINES[e % NENGINES]->npresets].name);
    ui.force = 1;
}

/* select track i (KNOB 1 on TRACKS, the editor): its sound, pages and pattern from now on */
static void track_select(uint32_t i)
{
    if (i >= NTRK || i == song.sel)
        return;
    song.sel = (uint8_t)i;
    rec_follow(i);                                   /* LIVE: recording follows the selected track */
    ui.entry_open = 0;
    ui.cursor = 0;
    ui.bank = 0;
    sync_reload = 1;
    ui.force = 1;
}
/* ALGORITHM: the next / previous track as the DECK lists them, top down (the BREAK first, then BASS, CHORDS, CHOP),
 * stopping at either end */
static void track_step(int32_t s)
{
    int32_t pos = song.sel == TRK_BRK ? 0 : (int32_t)song.sel + 1;
    pos = clamp(pos + (s > 0 ? 1 : -1), 0, NTRK - 1);
    track_select(pos ? (uint32_t)pos - 1u : TRK_BRK);
}
