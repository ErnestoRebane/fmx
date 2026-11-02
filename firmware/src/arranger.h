/* SPDX-License-Identifier: GPL-3.0-only */
/* Song order, independent of UI/flash/audio drivers. Durations are 4/4 bars.
 * Audio calls next BEFORE rendering a block, then elapse AFTER it. The clock
 * accumulates samples * BPM, keeping the remainder at every bar boundary.
 * FMX: an entry's scene is a section's role (arranger_scene.c AUTO SONG: INTRO .. OUTRO), the chain made from FORM
 * and VARIANT (reserved[0], reserved[1]); loop 1 = ENDLESS. A jump (the SONG layer's keys) goes to another entry on
 * the next bar. */
#ifndef FM1_ARRANGER_H
#define FM1_ARRANGER_H
#include <stdint.h>
#define ARR_STEPS 16u
#define ARR_SCENES 4u
#define ARR_ROLES 7u                     /* FMX: the section roles (arranger_scene.c AR_*) */
#define ARR_NONE (-1)
#define ARR_DONE (-2)
#define ARR_INVALID (-3)
typedef struct { uint8_t scene, bars; } arr_entry_t;
/* FMX: a section's card (what the section is in this song; saved in the project, SPB4). 0 in every field = "as in the
 * project": a new card changes nothing. A card is a role's (INTRO .. OUTRO), the chain's entries of that role share it. */
typedef struct {
    uint8_t len;                         /* the bars: 0 the FORM's, 1..4 = 4 / 8 / 16 / 32 */
    uint8_t who;                         /* 2 bits a track, in the track order (BASS CHORDS CHOP BREAK): 0 as the project,
                                          * 1 ON, 2 OFF, 3 under water */
    uint8_t loop;                        /* the BREAK's loop: 0 as the project, 1..4 = A..D */
    uint8_t style;                       /* the BREAK's STYLE: 0 as the project, 1.. = BS_* + 1 */
    uint8_t bass;                        /* the BASS preset: 0 as the project, n + 1 = the engine's preset n */
} sec_card_t;
typedef struct {
    uint8_t count, loop, reserved[2];
    arr_entry_t entry[ARR_STEPS];
} arr_config_t;
typedef struct {
    uint32_t phase;
    uint8_t running, index, bar, error;
    volatile int8_t jump;                /* the entry asked for (-1 none): on the next bar */
    volatile uint8_t hold;               /* FMX LIVE, AUTO off: the entry plays on, round and round (not saved) */
} arr_clock_t;

static void arr_defaults(arr_config_t *c)        /* (FMX: FORM CLUB, VARIANT 0, SONG; the chain made at PLAY) */
{
    uint32_t i;
    c->count = 1; c->loop = 0; c->reserved[0] = 1; c->reserved[1] = 0;
    for (i = 0; i < ARR_STEPS; i++) {
        c->entry[i].scene = 2;
        c->entry[i].bars = 8;
    }
}
static int arr_valid(const arr_config_t *c, uint32_t ready)
{
    uint32_t i;
    if (!c->count || c->count > ARR_STEPS || c->loop > 1u) return 0;
    for (i = 0; i < c->count; i++) {
        const arr_entry_t *e = &c->entry[i];
        if (e->scene >= ARR_ROLES || !e->bars || e->bars > 64u ||
            !(ready & (1u << e->scene))) return 0;
    }
    return 1;
}
static int arr_begin(arr_clock_t *r, const arr_config_t *c, uint32_t ready)
{
    r->phase = 0; r->index = 0; r->bar = 0; r->running = 0; r->jump = -1;
    r->error = (uint8_t)!arr_valid(c, ready);
    if (r->error) return ARR_INVALID;
    r->running = 1;
    return c->entry[0].scene;
}
/* FMX LIVE: PLAY from an armed section: the chain from entry `start` (not the first) */
static int arr_begin_at(arr_clock_t *r, const arr_config_t *c, uint32_t ready, uint32_t start)
{
    int scene = arr_begin(r, c, ready);
    if (scene < 0 || start >= c->count) return scene;
    r->index = (uint8_t)start;
    return c->entry[start].scene;
}
static int arr_next(arr_clock_t *r, const arr_config_t *c, uint32_t sample_rate)
{
    int result = ARR_NONE;
    uint32_t period = sample_rate * 240u;
    if (!r->running || !period) return ARR_NONE;
    while (r->phase >= period) {
        r->phase -= period;
        if (r->jump >= 0 && r->jump < (int)c->count) {   /* a jump: that entry from this bar */
            r->index = (uint8_t)r->jump;
            r->jump = -1;
            r->bar = 0;
            result = c->entry[r->index].scene;
            continue;
        }
        r->jump = -1;
        if (++r->bar < c->entry[r->index].bars) continue;
        r->bar = 0;
        if (r->hold) continue;                       /* (LIVE: the section again, nothing re-applied) */
        if (++r->index >= c->count) {
            if (!c->loop) { r->running = 0; return ARR_DONE; }
            r->index = 0;
        }
        result = c->entry[r->index].scene;
    }
    return result;
}
static void arr_elapse(arr_clock_t *r, uint32_t samples, uint32_t bpm)
{
    if (r->running) r->phase += samples * bpm;
}
#endif
