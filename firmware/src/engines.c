/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Engine table: order = PRESETS browsing order (and the engine numbers of the editor protocol). */
#include "dsp.c"
static inline int32_t voice_amp(int32_t s, const vmod_t *m, uint32_t i) { return mulq15(mulq15(s, amp_at(m, i)), VOICE_FS); }
static inline int32_t soft_knee(int32_t y, int32_t k)   /* linear up to k, then only the peaks saturate */
{
    int32_t a = y < 0 ? -y : y;
    if (a <= k)
        return y;
    a = k + (softclip((a - k) * 2) >> 1);
    return y < 0 ? -a : a;
}
/* (pitch_inc: felucca_tables.h, SLOOP 2.5: from the top octave) */
/* FMX: two engines. ANALOG plays the BASS track (sub, reese, 808 slides), SAMPLE the pads and the chops */
#include "eng_analog.c"
#include "eng_sample.c"
#if FELUCCA_SLC
#include "eng_slice.c"
#endif
#if FELUCCA_BREAK
#include "brkpart.c"            /* the BREAK part: breakbeat player with BB Gen (X0X); SLOOP 2.6. After SLICE: it uses its sources */
#endif

static const engine_t *const ENGINES[NENGINES] = {&ENG_ANALOG, &ENG_SAMPLE,
#if FELUCCA_SLICE
                                                    &ENG_SLICE,
#endif
};
_Static_assert(ENGI_ANALOG == 0u && ENGI_SAMPLE == 1u && ENGI_SLICE == 2u, "ENGINES[] order");

/* every factory sound as loud as the others: a level trim per preset, 1/2 dB, measured on a phrase
 * that fits the sound (tools/level_presets.py writes preset_trim.h); a track keeps it in P_ED_FX */
#include "preset_trim.h"
static int16_t preset_trim(uint32_t e, uint32_t pi)
{
    return e < PT_ENGINES && pi < PT_MAX ? PRESET_TRIM[e][pi] : 0;
}
