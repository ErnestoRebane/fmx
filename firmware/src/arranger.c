/* SPDX-License-Identifier: GPL-3.0-only */
/* Firmware adapter (FMX: AUTO SONG, arranger_scene.c, makes the chain; nothing is stored per section). No flash
 * operation, UI draw or allocation is allowed from these audio callbacks. */
#include "arranger.h"
static arr_config_t arrangement;
static arr_clock_t arrangement_clock;
static uint8_t arrangement_enabled;
static void arrangement_apply(uint32_t scene);         /* project.c */
/* FMX AUTO SONG's sections (arranger_scene.c): an entry's scene is one of these roles */
enum { AR_INTRO, AR_BUILD, AR_DROP, AR_ROLL, AR_BREAKDOWN, AR_DROP2, AR_OUTRO, AR_N };
_Static_assert(AR_N == ARR_ROLES, "the roles: arranger.h ARR_ROLES");
static const char *const AR_NAME[AR_N] = {"INTRO", "BUILD", "DROP", "ROLL", "BREAKDOWN", "DROP 2", "OUTRO"};
static const char *const AR_SHORT[AR_N] = {"IN", "BLD", "DRP", "ROL", "BRK", "DR2", "OUT"};
static const char *const N_AS_FORM[3] = {"SHORT", "CLUB", "LONG"};
static const char *const N_AS_MODE[2] = {"SONG", "ENDLESS"};
static uint32_t as_form(void) { return arrangement.reserved[0] % 3u; }   /* SHORT 32, CLUB 64, LONG 128 bars */
static uint32_t as_var(void) { return arrangement.reserved[1]; }
static uint32_t as_endless(void) { return arrangement.loop & 1u; }
static uint32_t arrangement_ready(void) { return (1u << AR_N) - 1u; }   /* (every role: nothing stored) */
static sec_card_t sec_card[ARR_ROLES];                  /* the song's section cards (the project's: project.c, the same object) */
static uint32_t as_rng, as_bvar;                    /* the chain's dice, its VARIANT (as_build) */
static uint32_t as_r(uint32_t n)                        /* the variant's dice: 0..n-1 */
{
    as_rng = as_rng * 1664525u + 1013904223u;
    return (as_rng >> 16) % n;
}
static int as_coin(uint32_t pct) { return as_bvar && as_r(100) < pct; }   /* (VARIANT 0: never) */
static void as_add(arr_config_t *c, uint32_t role, uint32_t bars)
{
    if (c->count < ARR_STEPS && bars) {
        c->entry[c->count].scene = (uint8_t)role;
        c->entry[c->count].bars = (uint8_t)(bars > 64u ? 64u : bars);
        c->count++;
    }
}
/* the chain: round 0 the song (in ENDLESS without its OUTRO), later rounds BUILD .. DROP 2 again */
static void as_build(arr_config_t *c, uint32_t cycle)
{
    uint32_t f = c->reserved[0] % 3u, u = f ? 8u : 4u, lng = f == 2u, i;
    c->count = 0;
    as_bvar = c->reserved[1];
    as_rng = 0x2545F491u ^ ((uint32_t)c->reserved[1] * 7919u + cycle * 104729u + f * 31u);
    if (!cycle)
        as_add(c, AR_INTRO, (lng ? 2u * u : u) >> (as_coin(30) ? 1u : 0u));
    as_add(c, AR_BUILD, cycle || as_coin(30) ? u / 2u : u);
    as_add(c, AR_DROP, (lng ? 3u : 2u) * u);
    if (lng || as_coin(40) || cycle)
        as_add(c, AR_ROLL, lng || as_coin(50) ? u : u / 2u);
    as_add(c, AR_BREAKDOWN, as_coin(35) ? u / 2u : lng ? 2u * u : u);
    if (lng || as_coin(40))
        as_add(c, AR_BUILD, u / 2u);
    as_add(c, AR_DROP2, (lng ? 3u : 2u) * u);
    if (lng && !as_coin(50))
        as_add(c, AR_ROLL, u);
    if (!(c->loop & 1u))
        as_add(c, AR_OUTRO, lng ? 2u * u : u);
    for (i = 0; i < c->count; i++) {                    /* a card's length: that many bars (4 / 8 / 16 / 32), the FORM's no more */
        uint32_t l = sec_card[c->entry[i].scene % AR_N].len;
        if (l >= 1u && l <= 4u)
            c->entry[i].bars = (uint8_t)(4u << (l - 1u));
    }
}
/* FORM, VARIANT or MODE changed (the UI, stopped or playing: the chain is the next PLAY's) */
static void as_set(uint32_t form, uint32_t var, uint32_t endless)
{
    arr_config_t c = arrangement;
    c.reserved[0] = (uint8_t)(form % 3u);
    c.reserved[1] = (uint8_t)var;
    c.loop = (uint8_t)(endless & 1u);
    fm1_irq_off();
    if (song.playing && arrangement_clock.running) {    /* the song playing keeps its chain (the settings: the next's) */
        arrangement.reserved[0] = c.reserved[0];
        arrangement.reserved[1] = c.reserved[1];
        arrangement.loop = c.loop;
        fm1_irq_on();
        return;
    }
    as_build(&c, 0);
    arrangement = c;
    fm1_irq_on();
}

static uint32_t as_total(const arr_config_t *c)         /* the chain's bars */
{
    uint32_t i, n = 0;
    for (i = 0; i < c->count; i++)
        n += c->entry[i].bars;
    return n;
}

/* FMX LIVE: the section PLAY starts from. arr_armed: asked for with a section key while stopped (-1: none);
 * arr_resume: the one the song was left in (INTRO for a new song; ui_live.c keeps it; project_load resets it) */
static int8_t arr_armed = -1;
static uint8_t arr_resume;

static int arrangement_start(void)
{
    int scene;
    uint32_t start = 0;
    if (!arrangement_enabled) return 1;
    as_build(&arrangement, 0);                       /* (AUTO SONG: the chain made now) */
    {   /* the armed section, else where it was left: the chain from there, no INTRO first */
        uint32_t role = arr_armed >= 0 ? (uint32_t)arr_armed : arr_resume;
        arr_armed = -1;
        if (role < AR_N && role != AR_INTRO) {
            while (start < arrangement.count && arrangement.entry[start].scene != role)
                start++;
            if (start >= arrangement.count) {        /* (this form has none: the first entry becomes it) */
                start = 0;
                arrangement.entry[0].scene = (uint8_t)role;
            }
        }
    }
    scene = arr_begin_at(&arrangement_clock, &arrangement, arrangement_ready(), start);
    if (scene < 0) return 0;
    arrangement_apply((uint32_t)scene);
    song.rec = 0;                                    /* song playback does not overwrite patterns */
    return 1;
}
