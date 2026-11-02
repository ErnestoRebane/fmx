/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Projects: four slots in .noinit RAM (the song sections A..D), so they survive resets and UBOOT
 * entry. With FELUCCA_FLASH every save also goes to flash through storage.c, and an empty RAM slot
 * is filled from flash on load. The working project is also kept in flash by itself (autosave, when
 * the transport is stopped and nothing sounds) and comes back at power-on: SLOOP starts where you
 * left it.
 *
 * Format: "SPB4" (FMX 0.1): the project's globals (G_NPROJ), the selected track, the song (FORM, VARIANT, MODE and the
 * seven section cards, arranger.h sec_card_t), the CHORDS generator's
 * settings (chordgen.c chord_cfg_t), the BASS track's phrase generator (bassline.c bl_cfg_t; its phrase is the track's steps), and per track its parameters (the BREAK track's
 * generator rides in them, brkpart.c BPP), engine and preset, 64 steps (the BREAK track's are bstep_t), the nudge
 * of each step, NLOCK (20) parameter locks and the steps' fill conditions. SPB3 (24 locks, no song) is still read:
 * proj_import makes it an SPB4 (the locks first to last, up to NLOCK; the song left as it is, empty cards). SLOOP's
 * projects ("FUN1".."FUN5") are not read, nor SPB1 / SPB2 (the builds before the CHORDS generator / the BASS
 * generator): their slots show empty until saved over.
 *
 * Built on the host too (tests/project_test.c, -DPROJ_HOST): the part above the #ifndef
 * PROJ_HOST needs core.h, params.c (TP), the engines, brkpart.c and trk_def_engine (ui.c). */
#include "arranger.h"                           /* sec_card_t, ARR_ROLES (a header: the arranger itself is not needed here) */
static sec_card_t sec_card[ARR_ROLES];         /* the song's section cards: the working ones (as arranger.c's: one object) */
#define PROJ_MAGIC 0x53504234u                 /* "SPB4" (0.1: + the song's section cards, 20 locks a track) */
#define PROJ_MAGIC3 0x53504233u                /* "SPB3": read, made an SPB4 (proj_import) */
#define NLOCK_V3 24
#define PROJ_NG G_NPROJ
typedef struct {                               /* one track; the BREAK track ignores engine / preset */
    int16_t p[P_COUNT];
    uint8_t engine, preset;
    union {
        step_t step[NSTEP];
        bstep_t bstep[NSTEP];
    };
    int8_t micro[NSTEP];                       /* each step's nudge (core.h) */
    plock_t lock[NLOCK];                       /* and its parameter locks (step LOCK_FREE = none) */
    uint8_t fill[NSTEP / 4];                   /* and its fill condition, 2 bits a step (FC_*) */
} proj_trk_t;
typedef struct {
    uint32_t magic, size;
    int16_t g[PROJ_NG];
    uint8_t sel;                               /* the selected track */
    uint8_t space;                             /* FMX: G_SPACE + 1 (0: a save from before it: 64) */
    uint8_t song_mode;                         /* the song: 0 not saved (the FORM, VARIANT and MODE as they are), else
                                                * 0x80 | ENDLESS << 2 | FORM (0..2) */
    uint8_t song_var;                          /* its VARIANT */
    sec_card_t card[ARR_ROLES];                /* the section cards (arranger.h) */
    chord_cfg_t cg;                            /* the CHORDS generator (chordgen.c) */
    bl_cfg_t bl;                               /* the BASS track's phrase generator (bassline.c) */
    proj_trk_t t[NTRK];
    uint32_t sum;
} project_t;
_Static_assert(sizeof(project_t) <= 3840u, "a project fits one flash object (storage.c ST_PAYLOAD_MAX)");

typedef struct {                               /* SPB3's track: 24 locks */
    int16_t p[P_COUNT];
    uint8_t engine, preset;
    union {
        step_t step[NSTEP];
        bstep_t bstep[NSTEP];
    };
    int8_t micro[NSTEP];
    plock_t lock[NLOCK_V3];
    uint8_t fill[NSTEP / 4];
} proj_trk3_t;
typedef struct {                               /* SPB3's project: 3836 B */
    uint32_t magic, size;
    int16_t g[PROJ_NG];
    uint8_t sel, space, rsv[2];
    chord_cfg_t cg;
    bl_cfg_t bl;
    proj_trk3_t t[NTRK];
    uint32_t sum;
} project3_t;
_Static_assert(sizeof(project3_t) == 3836u, "SPB3's size, as it was stored");
project_t proj_slot[4] __attribute__((section(".noinit")));

static uint32_t proj_hash(const void *p, uint32_t n)   /* FNV-1a over n bytes */
{
    const uint8_t *b = (const uint8_t *)p;
    uint32_t i, s = 0x811C9DC5u;
    for (i = 0; i < n; i++)
        s = (s ^ b[i]) * 16777619u;
    return s;
}
static uint32_t proj_sum(const project_t *p) { return proj_hash(p, sizeof *p - 4u); }
static int proj_ok(const project_t *q) { return q->magic == PROJ_MAGIC && q->size == sizeof *q && q->sum == proj_sum(q); }

/* n bytes of a stored project -> slot q; 0 = not a project. SPB3 (24 locks, no song) becomes an SPB4: the locks kept
 * first to last up to NLOCK, no song saved, empty cards (= as the project) */
static int proj_import(project_t *q, const void *b, int n)
{
    if (n == (int)sizeof *q && proj_ok((const project_t *)b)) {
        memcpy(q, b, sizeof *q);
        return 1;
    }
    if (n == (int)sizeof(project3_t)) {
        const project3_t *o = (const project3_t *)b;
        uint32_t k, i, j;
        if (o->magic != PROJ_MAGIC3 || o->size != sizeof *o || o->sum != proj_hash(o, sizeof *o - 4u))
            return 0;
        memset(q, 0, sizeof *q);
        q->magic = PROJ_MAGIC;
        q->size = sizeof *q;
        memcpy(q->g, o->g, sizeof q->g);
        q->sel = o->sel;
        q->space = o->space;
        q->cg = o->cg;
        q->bl = o->bl;
        for (k = 0; k < NTRK; k++) {
            memcpy(q->t[k].p, o->t[k].p, sizeof q->t[k].p);
            q->t[k].engine = o->t[k].engine;
            q->t[k].preset = o->t[k].preset;
            memcpy(q->t[k].step, o->t[k].step, sizeof q->t[k].step);
            memcpy(q->t[k].micro, o->t[k].micro, sizeof q->t[k].micro);
            memcpy(q->t[k].fill, o->t[k].fill, sizeof q->t[k].fill);
            for (i = 0, j = 0; i < NLOCK_V3 && j < NLOCK; i++)
                if (o->t[k].lock[i].step < NSTEP)
                    q->t[k].lock[j++] = o->t[k].lock[i];
            for (; j < NLOCK; j++) {
                q->t[k].lock[j].step = LOCK_FREE;
                q->t[k].lock[j].param = 0;
                q->t[k].lock[j].val = 0;
            }
        }
        q->sum = proj_sum(q);
        return 1;
    }
    return 0;
}

/* ---- the working project <-> a project_t */
static void proj_capture(project_t *p)        /* what is playing now, as a project */
{
    uint32_t i;
    memset(p, 0, sizeof *p);
    p->magic = PROJ_MAGIC;
    p->size = sizeof *p;
    for (i = 0; i < PROJ_NG; i++)
        p->g[i] = song.g[i];
    p->sel = song.sel;
    p->space = (uint8_t)(clamp(song.g[G_SPACE], 0, 127) + 1);
    p->cg = cg;
    p->bl = bl;
#if FELUCCA_ARRANGER
    p->song_mode = (uint8_t)(0x80u | (arrangement.loop & 1u) << 2 | arrangement.reserved[0] % 3u);
    p->song_var = arrangement.reserved[1];
#endif
    memcpy(p->card, sec_card, sizeof p->card);
    for (i = 0; i < NTRK; i++) {
        memcpy(p->t[i].p, trk[i].p, sizeof trk[i].p);
        p->t[i].engine = trk[i].eng_req;
        p->t[i].preset = trk[i].preset;
        memcpy(p->t[i].step, trk[i].step, sizeof trk[i].step);
        memcpy(p->t[i].micro, trk[i].micro, sizeof trk[i].micro);
        memcpy(p->t[i].lock, trk[i].lock, sizeof trk[i].lock);
        memcpy(p->t[i].fill, trk[i].fill, sizeof trk[i].fill);
    }
    p->sum = proj_sum(p);
}

/* a project's tracks (and its globals: a load; a song section leaves them) into the working one, every value back
 * inside its range. The audio ISR must not run meanwhile (the song sections: called from it; a load: IRQ off) */
static void proj_apply(const project_t *p, int all)
{
    uint32_t i, k;
    for (i = 0; i < PROJ_NG; i++)
        if (all)
            song.g[i] = (int16_t)clamp(p->g[i], GP[i].min, GP[i].max);
    if (all) {
        song.g[G_SPACE] = (int16_t)(p->space ? p->space - 1 : 64);   /* (SPACE: 0 = saved before it) */
        cg = p->cg;                                     /* the CHORDS generator, back inside its ranges */
        cg_clamp(&cg);
        bl = p->bl;                                     /* the BASS phrase generator */
        bl_clamp(&bl);
        for (i = 0; all == 1 && i < ARR_ROLES; i++) {   /* the section cards, each field inside its range (all 2: STOP's
                                                         * restore of the loop: the song as it is now, edited meanwhile) */
            sec_card_t *c = &sec_card[i];
            *c = p->card[i];
            if (c->len > 4u)
                c->len = 0;
            if (c->loop > 4u)
                c->loop = 0;
            if (c->style > BS_NSTYLE)
                c->style = 0;
            if (c->bass > 64u)
                c->bass = 0;
        }
#if FELUCCA_ARRANGER
        if (all == 1 && (p->song_mode & 0x80u)) {       /* the song: FORM, VARIANT, MODE (a project of before it: as they are) */
            arrangement.reserved[0] = (uint8_t)((p->song_mode & 3u) % 3u);
            arrangement.reserved[1] = p->song_var;
            arrangement.loop = (uint8_t)((p->song_mode >> 2) & 1u);
        }
#endif
    }
    for (k = 0; k < NTRK; k++) {
        track_t *t = &trk[k];
        const proj_trk_t *s = &p->t[k];
        uint32_t e = k < NPART ? s->engine % NENGINES : 0u;
        t->eng_req = (uint8_t)e;
        t->user = 0;                                    /* (no user preset slot is saved) */
        t->lk_n = 0;                                    /* (the locks in force: the values come from the project) */
        for (i = 0; i < P_COUNT; i++) {                 /* every value back inside its range */
            const param_desc_t *d = k == TRK_BRK && brk_desc(i) ? brk_desc(i) :   /* the BREAK track's generator */
                                    i >= P_E0 && i <= P_E7 ? &ENGINES[e]->edit[i - P_E0] : &TP[i];
            t->p[i] = (int16_t)clamp(s->p[i], d->min, d->max);
        }
        t->preset = (uint8_t)(ENGINES[e]->npresets ? (s->preset == 0xFFu ? 0u : s->preset) % ENGINES[e]->npresets : 0u);
        memcpy(t->step, s->step, sizeof t->step);
        for (i = 0; i < NSTEP; i++)                     /* the nudges and locks, each inside its range */
            t->micro[i] = (int8_t)clamp(s->micro[i], MICRO_MIN, MICRO_MAX);
        for (i = 0; i < NSTEP / 4u; i++) {              /* the conditions: 3 means normal (0) */
            uint32_t b = s->fill[i], j;
            for (j = 0; j < 4u; j++)
                if (((b >> (2u * j)) & 3u) == 3u)
                    b &= ~(3u << (2u * j));
            t->fill[i] = (uint8_t)b;
        }
        for (i = 0; i < NLOCK; i++) {
            const plock_t *l = &s->lock[i];
            uint32_t id = l->param;
            if (l->step < NSTEP && id < P_COUNT && p_lockable(id) && (k != TRK_BRK || brk_lockable(id))) {
                const param_desc_t *d = k == TRK_BRK && brk_desc(id) ? brk_desc(id) :
                                        id >= P_E0 && id <= P_E7 ? &ENGINES[e]->edit[id - P_E0] : &TP[id];
                t->lock[i].step = l->step;
                t->lock[i].param = (uint8_t)id;
                t->lock[i].val = (int16_t)clamp(l->val, d->min, d->max);
            } else {
                t->lock[i].step = LOCK_FREE;            /* no such step or parameter: the slot is free */
                t->lock[i].param = 0;
                t->lock[i].val = 0;
            }
        }
        if (k == TRK_BRK)
            for (i = 0; i < NSTEP; i++) {               /* the BREAK track's steps, each inside its range */
                bstep_t *st = &t->bstep[i];
                st->on = (uint8_t)(st->on != 0u);
                if (st->slice > BRK_SLICES)
                    st->slice = 0;
                st->lvl &= 3u;
                if (st->rat > 4u)
                    st->rat = 0;
                st->pitch = (int8_t)clamp(st->pitch, -24, 24);
                st->flags &= BS_REV;
            }
        else
            for (i = 0; i < NSTEP; i++) {
                step_t *st = &t->step[i];
                uint32_t j;
                if (st->n > 4u)
                    st->n = 4;
                if (st->time > ST_REST)
                    st->time = ST_REST;
                for (j = 0; j < 4u; j++)
                    st->note[j] &= 127u;
            }
    }
}

#ifndef PROJ_HOST
#if FELUCCA_ARRANGER
#include "arranger_scene.c"
#endif
static uint8_t sec_dirty, song_dirty;           /* live sections / the song: in RAM, not yet in flash */
static union {                                  /* a project read from flash, or captured before it is written */
    project_t v5;
} proj_tmp;
#if FELUCCA_FLASH
/* slot from flash into RAM: 1, 0 nothing there, -1 something there it cannot read */
static int proj_fetch(uint32_t slot)
{
    project_t *q = &proj_slot[slot & 3u];
    int n = st_load(OBJ_PROJECT0 + (slot & 3u), &proj_tmp, sizeof proj_tmp);
    if (proj_import(q, &proj_tmp, n))
        return 1;
    q->magic = 0;
    return n > 0 ? -1 : 0;
}
#endif

static void project_save(uint32_t slot)
{
    project_t *p = &proj_slot[slot & 3u];
#if FELUCCA_ARRANGER
    if (song.playing || transport_req) { ui_message("STOP BEFORE SAVE"); return; }
#endif
    proj_capture(&proj_tmp.v5);                         /* (the slot keeps its old song until the write is done) */
#if FELUCCA_FLASH
    if (flash_ok && st_save(OBJ_PROJECT0 + (slot & 3u), &proj_tmp.v5, sizeof proj_tmp.v5)) {
        ui_message("SAVE ERROR: NOT SAVED");
        return;
    }
#endif
    memcpy(p, &proj_tmp.v5, sizeof *p);
    live_song = (int8_t)(slot & 3u);                    /* (LIVE, the SONG screen: the song of this slot is the one loaded now) */
    proj_touched = 0;
    proj_gen++;
#if FELUCCA_FLASH
    if (flash_ok) {
        ui_message("SAVED");
        return;
    }
#endif
    ui_message("SAVED (RAM)");
}

/* LIVE: would loading another song lose work? Edited in STUDIO since it was loaded or saved (ui_input.c
 * proj_touched: LIVE's own moves, the arranger's and a transition's are playing it, not editing it); a song from no
 * slot: anything recorded */
static int project_unsaved(void)
{
    uint32_t i;
    if (live_song >= 0 || proj_touched)
        return proj_touched;
    for (i = 0; i < NTRK; i++)
        if (!track_empty(&trk[i]))
            return 1;
    return 0;
}

/* a project into the working one: the transport stops, everything sounding is released */
static void project_apply(const project_t *p)
{
    uint32_t k;
    transport_req = 2;
    panic_req = (1u << NTRK) - 1u;
    fm1_irq_off();                                      /* the audio ISR must not see half a project */
    proj_apply(p, 1);
    song.sel = (uint8_t)(p->sel < NTRK ? p->sel : 0u);
    fm1_irq_on();
    for (k = 0; k < NPART; k++)                         /* a format 1 project: the default sounds of tracks 2, 3 */
        if (p->t[k].preset == 0xFFu) {
            apply_preset_to(&trk[k], TRK_DEF[k][1]);
            steps_clear(&trk[k]);
        }
    sync_reload = 1;
    ui.force = 1;
}

static void project_load(uint32_t slot)
{
    project_t *p = &proj_slot[slot & 3u];
#if FELUCCA_ARRANGER
    if (song.playing || transport_req) { ui_message("STOP BEFORE LOAD"); return; }
#endif
#if FELUCCA_FLASH
    if (flash_ok && !proj_ok(p) && proj_fetch(slot) < 0) {
        ui_message("READ ERROR");
        return;
    }
#endif
    if (!proj_ok(p)) {
        ui_message("EMPTY SLOT");
        return;
    }
    q_clear();                                      /* (Q: nothing of the old one waits) */
    project_apply(p);
    arr_armed = -1;                                 /* (PLAY starts the new song from its INTRO) */
    arr_resume = AR_INTRO;
    live_song = (int8_t)(slot & 3u);                /* (LIVE: the song of the set playing) */
    live_next = -1;
    proj_touched = 0;
    proj_gen++;
    ui_message("LOADED");
}

/* LIVE (ui_live.c): is there a song in the slot (from flash if need be)? its tempo */
static int project_info(uint32_t slot, int32_t *bpm)
{
    project_t *p = &proj_slot[slot & 3u];
#if FELUCCA_FLASH
    if (flash_ok && !proj_ok(p))
        proj_fetch(slot);
#endif
    if (!proj_ok(p))
        return 0;
    if (bpm)
        *bpm = p->g[G_BPM];
    return 1;
}

/* the SONG screen's SET page: a slot's song: its tempo, FORM (0..2; 3: a project saved before the sections) and VARIANT */
static int project_song_info(uint32_t slot, int32_t *bpm, uint32_t *form, uint32_t *var)
{
    const project_t *p = &proj_slot[slot & 3u];
    if (!project_info(slot, bpm))
        return 0;
    *form = (p->song_mode & 0x80u) ? (uint32_t)(p->song_mode & 3u) % 3u : 3u;
    *var = p->song_var;
    return 1;
}

/* ---- the working project, kept in flash by itself: saved when it changed, the transport is stopped,
 * nothing sounds and the panel was not touched for AUTOSAVE_IDLE (a flash erase stops the audio for
 * ~50 ms: never while something plays); loaded at power-on (autosave_resume) */
#define AUTOSAVE_IDLE 2500u                    /* ms without input */
#define AUTOSAVE_GAP 20000u                    /* ms between two saves at least */
static project_t autosave_buf __attribute__((section(".pool")));
static uint32_t autosave_hash, autosave_ms, autosave_checked;

static int audio_quiet(void)
{
    uint32_t p, i;
    for (p = 0; p < NPART; p++)
        for (i = 0; i < NVOICE; i++)
            if (trk[p].v[i].active)
                return 0;
    return !bp.v[0].on && !bp.v[1].on;
}

static void autosave_tick(void)                /* main loop */
{
#if FELUCCA_FLASH
    uint32_t h, now = fm1_ms;
    if (!flash_ok || song.playing || transport_req || rec_wait || ft_on || ui.menu ||
        now - ui_input_ms < AUTOSAVE_IDLE || now - autosave_ms < AUTOSAVE_GAP || now - autosave_checked < 1000u)
        return;
    autosave_checked = now;
    proj_capture(&autosave_buf);
    h = autosave_buf.sum;
    if (h == autosave_hash || !audio_quiet())
        return;
    if (st_save(OBJ_AUTOSAVE, &autosave_buf, sizeof autosave_buf) == 0)
        autosave_hash = h;
    autosave_ms = fm1_ms;
#endif
}

static void autosave_resume(void)              /* power-on: the project as it was left (felucca_init) */
{
#if FELUCCA_FLASH
    project_t *q = &autosave_buf;
    int n;
    if (!flash_ok)
        return;
    n = st_load(OBJ_AUTOSAVE, &proj_tmp, sizeof proj_tmp);
    if (!proj_import(q, &proj_tmp, n))
        return;
    autosave_hash = q->sum;
    proj_apply(q, 1);
    song.sel = (uint8_t)(q->sel < NTRK ? q->sel : 0u);
    for (n = 0; n < NPART; n++)
        trk[n].engine = trk[n].eng_req;        /* (nothing sounds yet: no fade) */
#endif
}

/* settings + learned panel table: one flash object. The flash copy wins at
 * boot (the .noinit copies are garbage after a power-off). */
typedef struct {
    uint32_t magic, palette, lowcut, zoom;
    panel_t panel;
#if FELUCCA_ARRANGER
    arr_config_t arrangement;
#endif
    uint32_t lights;                               /* SLOOP 2.3: the backlight (panel.c lights_word); appended,
                                                    * so 2.2 still reads its part (st_load cuts at its size) */
} persist_t;
#define PERSIST_SIZE_V22 __builtin_offsetof(persist_t, lights)   /* the settings as 2.2 wrote them (no lights) */
_Static_assert(sizeof(persist_t) == PERSIST_SIZE_V22 + 4u, "lights: the last word, no padding before it");
#if FELUCCA_ARRANGER
_Static_assert(__builtin_offsetof(persist_t, arrangement) == 48u && sizeof(arr_config_t) == 36u,
               "the web editor's Song page edits bytes 48..83 of the settings (web/editor.html ARR)");
#endif
#if FELUCCA_ARRANGER
#define PERSIST_MAGIC 0x50455233u                  /* "PER3": includes the song order */
#else
#define PERSIST_MAGIC 0x50455232u
#endif
#if FELUCCA_FLASH
static persist_t persist_saved;
#endif

static void persist_boot(void)                    /* before settings_init / panel_init */
{
#if FELUCCA_ARRANGER
    arr_defaults(&arrangement);
#endif
#if FELUCCA_FLASH
    persist_t p;
    uint32_t f = irq_save();
    flash_ok = FL_FAR(fl_jedec_ram)() == 0x856014u;       /* the expected 1 MiB part, else stay RAM-only */
    irq_restore(f);
    if (!flash_ok)
        return;
    fl_plain_window_init();                        /* flash above 0x93000 reads as plaintext through XIP
                                                    * (user sample sets are played from there) */
    smp_pool_scan();
    {
        int n = st_load(OBJ_SETTINGS, &p, sizeof p);
        if (n == (int)PERSIST_SIZE_V22 && p.magic == PERSIST_MAGIC)
            p.lights = 0;                          /* from 2.2: backlight off */
        if (((n == (int)sizeof p || n == (int)PERSIST_SIZE_V22) && p.magic == PERSIST_MAGIC)
#if FELUCCA_ARRANGER
            || (n == (int)(16u + sizeof(panel_t)) && p.magic == 0x50455232u)
#endif
            ) {
            settings.magic = SETTINGS_MAGIC;
            settings.palette = p.palette;
            settings.lowcut = p.lowcut;
            settings.zoom = p.zoom;
            if (p.panel.magic == PANEL_MAGIC)
                panel = p.panel;
            if (p.magic == PERSIST_MAGIC)
                lights_from_word(p.lights);
            else
                p.lights = 0;
#if FELUCCA_ARRANGER
            if (p.magic == PERSIST_MAGIC && arr_valid(&p.arrangement, (1u << ARR_ROLES) - 1u))
                arrangement = p.arrangement;
            else
                p.arrangement = arrangement;
#endif
            persist_saved = p;
        } else if (n == (int)(8u + sizeof(panel_t)) && p.magic == 0x50455231u) {   /* "PER1": palette, panel */
            const uint32_t *w = (const uint32_t *)&p;
            panel_t old;
            memcpy(&old, w + 2, sizeof old);
            settings.magic = SETTINGS_MAGIC;
            settings.palette = w[1];
            settings.lowcut = 0;
            settings.zoom = 0;
            if (old.magic == PANEL_MAGIC)
                panel = old;
        }
    }
    {   /* projects: fill empty RAM slots from flash, so the slot list is right after power-on. A slot
         * still valid in RAM (a warm reset: an update, UPDATE MODE, a crash) may never have reached
         * flash (a live section stored while playing): marked to be written when quiet */
        uint32_t i;
        for (i = 0; i < 4u; i++)
            if (!proj_ok(&proj_slot[i])) {
                proj_fetch(i);
            } else {
                int n = st_load(OBJ_PROJECT0 + i, &proj_tmp, sizeof proj_tmp);
                if (n != (int)sizeof proj_slot[i] || memcmp(&proj_tmp.v5, &proj_slot[i], sizeof proj_slot[i]))
                    sec_dirty |= (uint8_t)(1u << i);
            }
    }
    up_boot();                                     /* user presets */
#endif
}

static int project_used(uint32_t slot) { return proj_ok(&proj_slot[slot & 3u]); }

#if FELUCCA_FLASH
/* the settings object: persist_t (SLOOP's settings left more after it: st_load cuts at our size, and our next save
 * drops it) */
static int settings_write(const persist_t *p)
{
    return st_save(OBJ_SETTINGS, p, sizeof *p) ? -1 : 0;
}
#endif

static void persist_fill(persist_t *p)              /* the settings as they are now */
{
    memset(p, 0, sizeof *p);
    p->magic = PERSIST_MAGIC;
    p->palette = settings.palette;
    p->lowcut = settings.lowcut;
    p->zoom = settings.zoom;
    p->panel = panel;
    p->lights = lights_word();
#if FELUCCA_ARRANGER
    p->arrangement = arrangement;
#endif
}

static void settings_save(void)
{
#if FELUCCA_FLASH
    persist_t p;
    if (!flash_ok)
        return;
    persist_fill(&p);
    if (!memcmp(&p, &persist_saved, sizeof p))
        return;                                    /* unchanged: no erase cycle */
    if (settings_write(&p) == 0)
        persist_saved = p;
    else
        ui_message("SETTINGS NOT SAVED");
#endif
}

#if FELUCCA_FLASH
_Static_assert(sizeof(project_t) <= ST_PAYLOAD_MAX, "project does not fit one flash sector");

/* ---- backup restore (editor.c BK_PUT): each object checked as a load checks it, then written through the
 * same A/B commit as a save. rc: 0 ok, 2 not a valid object, 3 stop the song first, 4 flash */
static int panel_valid(const panel_t *q)           /* a permutation of the buttons and of the knobs */
{
    uint32_t i, b = 0, e = 0;
    if (q->magic != PANEL_MAGIC)
        return 0;
    for (i = 0; i < NB; i++) {
        if (q->btn[i] >= 14u || (b >> q->btn[i]) & 1u)
            return 0;
        b |= 1u << q->btn[i];
    }
    for (i = 0; i < NE; i++) {
        if (q->enc[i] >= 7u || (e >> q->enc[i]) & 1u || (q->dir[i] != 1 && q->dir[i] != -1))
            return 0;
        e |= 1u << q->enc[i];
    }
    return 1;
}

static uint32_t settings_restore(const void *raw, uint32_t n)
{
    persist_t p;
    if (n != sizeof p && n != PERSIST_SIZE_V22)
        return 2;
    memset(&p, 0, sizeof p);
    memcpy(&p, raw, n);
    if (p.magic != PERSIST_MAGIC || p.palette >= NPALETTES || p.lowcut > 1u || p.zoom > 1u || !panel_valid(&p.panel))
        return 2;
#if FELUCCA_ARRANGER
    if (!arr_valid(&p.arrangement, (1u << ARR_ROLES) - 1u))
        return 2;
    if ((song.playing || transport_req) && memcmp(&p.arrangement, &arrangement, sizeof arrangement))
        return 3;                                    /* SLOOP 2.5: a new song order only when stopped (as on the SONG screen) */
#endif
    if (!flash_ok || settings_write(&p))
        return 4;
    persist_saved = p;
    settings.palette = p.palette;
    settings.lowcut = p.lowcut;
    settings.zoom = p.zoom;
    panel = p.panel;
#if FELUCCA_ARRANGER
    arrangement = p.arrangement;
#endif
    lights_from_word(p.lights);
    song.g[G_SYNC] = (int16_t)lights_sync;
    song.g[G_MIDI] = (int16_t)lights_mout;
    song.g[G_ROUTE] = (int16_t)lights_min;
    palette_set(settings.palette);
    fx_lowcut = (uint8_t)(settings.lowcut != 0);
    ui.force = 1;
    return 0;
}

/* slot 0..3 (n 0: empty), or 4: the working project (loaded now) */
static uint32_t project_restore(uint32_t slot, const void *raw, uint32_t n)
{
    if (song.playing || transport_req)
        return 3;
    if (slot < 4u && !n) {
        if (!flash_ok || st_save(OBJ_PROJECT0 + slot, raw, 0))
            return 4;
        memset(&proj_slot[slot], 0, sizeof proj_slot[slot]);
        sec_dirty &= (uint8_t)~(1u << slot);
        return 0;
    }
    if (!proj_import(&autosave_buf, raw, (int)n))
        return 2;
    if (slot == 4u) {
        project_apply(&autosave_buf);
        return 0;
    }
    if (!flash_ok || st_save(OBJ_PROJECT0 + slot, &autosave_buf, sizeof autosave_buf))
        return 4;
    memcpy(&proj_slot[slot], &autosave_buf, sizeof proj_slot[slot]);
    sec_dirty &= (uint8_t)~(1u << slot);
    return 0;
}
#endif
#if FELUCCA_ARRANGER
static void arrangement_save(void)
{
    if (song.playing || transport_req) { ui_message("STOP BEFORE SAVE"); return; }
    settings_save();
#if FELUCCA_FLASH
    if (flash_ok) {
        ui_message(memcmp(&persist_saved.arrangement, &arrangement, sizeof arrangement) ? "SAVE ERROR" : "SONG SAVED");
        return;
    }
#endif
    ui_message("SONG IN RAM ONLY");
}

/* ---- the sections are no longer stored (AUTO SONG, arranger_scene.c); project slots are projects. The dirty
 * project slots and the song settings go to flash once the transport is stopped and nothing sounds (an erase stops
 * the audio for ~50 ms). */
static void sections_write(void)                        /* the dirty sections and song into flash */
{
    uint32_t i;
#if FELUCCA_FLASH
    if (flash_ok)
        for (i = 0; i < 4u; i++)
            if (((sec_dirty >> i) & 1u) && st_save(OBJ_PROJECT0 + i, &proj_slot[i], sizeof proj_slot[i]) == 0)
                sec_dirty &= (uint8_t)~(1u << i);       /* (a failed write stays dirty: tried again later) */
    if (!flash_ok)
#endif
        sec_dirty = 0;
    (void)i;
    if (song_dirty) {
        song_dirty = 0;
        settings_save();
    }
}
/* before an intentional reset (an update, UPDATE MODE, UBOOT from the host): the audio is stopped, so
 * whatever is only in RAM goes to flash now: the live sections, the song, the working project */
static void persist_flush_now(void)
{
    sections_write();
#if FELUCCA_FLASH
    if (flash_ok && !arrangement_clock.running) {     /* (a song playing: the tracks hold a section) */
        proj_capture(&autosave_buf);
        if (autosave_buf.sum != autosave_hash && st_save(OBJ_AUTOSAVE, &autosave_buf, sizeof autosave_buf) == 0)
            autosave_hash = autosave_buf.sum;
    }
#endif
}
static void sections_flush(void)                        /* main loop */
{
    static uint32_t tried;
    if ((uint32_t)song.g[G_SYNC] != lights_sync) {      /* GLO > SYSTEM > SYNC: kept with the settings */
        lights_sync = (uint8_t)song.g[G_SYNC];
        settings_later = 1;
    }
    if ((uint32_t)(song.g[G_MIDI] != 0) != lights_mout) {   /* GLO > SYSTEM > MIDI: the same */
        lights_mout = (uint8_t)(song.g[G_MIDI] != 0);
        settings_later = 1;
    }
    if ((uint32_t)(song.g[G_ROUTE] != 0) != lights_min) {   /* GLO > SYSTEM > IN: the same */
        lights_min = (uint8_t)(song.g[G_ROUTE] != 0);
        settings_later = 1;
    }
    if (settings_later) {                               /* the menu closed while playing */
        settings_later = 0;
        song_dirty = 1;                                 /* (settings_save when quiet, with the song) */
    }
    if ((!sec_dirty && !song_dirty) || song.playing || transport_req || !audio_quiet() || fm1_ms - ui_input_ms < 1500u ||
        fm1_ms - tried < 5000u)
        return;
    tried = fm1_ms;                                     /* (a failed write: again in 5 s, not every frame) */
    sections_write();
    if (sec_dirty)
        ui_message("SAVE ERROR: RETRYING");
}
#endif
#endif /* PROJ_HOST */
