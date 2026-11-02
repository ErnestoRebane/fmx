/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* ANALOG: two band-limited oscillators (saw / square / tri / sine / PWM),
 * noise, drive and a trapezoidal low-pass. FMX, the jungle basses (docs/bass.md says why each is so):
 *   RSUB  the reese with its sub: two saws detuned either side of the note, each without its fundamental, through
 *         the filter, and a clean sine at the note after it, at the level NOIS turns into (SUB). The saws' beating
 *         stays in the mids, where it is the reese; the low end is the sine's, steady
 *   SUB   the sub: a sine at the note with its octave (HARM) and twelfth (ODD), and a distorted copy of it (DRV)
 *         through the filter as a band-pass (GRIT): mid growl beside a clean sub, never through it
 *   DRED  the reversed bass: the saws, the envelope played backwards (a swell that ends where a note starts)
 * and the CHOP's leads (docs/leads.md):
 *   HOOV  the Alpha Juno's pulse-saw (the hoover): a saw and the same saw a pulse width later, summed; the width moves
 *   SYNC  hard sync: a saw restarted by the note's period, its own pitch DTN quarter semitones up (+ SHP: the sweep)
 *   FM    two operators: a sine modulated by a sine at a ratio (DTN), the index MIX (+ SHP), the modulator's feedback
 *         NOIS (the DX7's bells, the YM2612's leads, an 808 cowbell played as a melody) */
static const char *const N_ANALOG_WAVE[] = {"SAW", "SQR", "TRI", "SIN", "PWM", "RSUB", "SUB", "DRED", "HOOV", "SYNC", "FM"};
#define AN_RSUB 5u
#define AN_SUB 6u
#define AN_DRED 7u
#define AN_HOOV 8u
#define AN_SYNC 9u
#define AN_FM 10u

static void analog_note_on(track_t *t, voice_t *v)
{
    if (!v->env && !v->env_out)
        v->ph[0] = v->ph[2] = 0;                      /* a fresh note: from phase 0 (an 808 starts the same every time) */
    v->ph[1] = v->ph[0] + 0x40000000u;
    v->s[0] = v->s[1] = 0;                            /* filter */
    if ((uint32_t)t->p[P_E0] >= AN_HOOV) {
        v->s[2] = 0;                                  /* (the leads: FM's feedback) */
        v->s[3] = (int32_t)(v->ph[1] >> 17);          /* (SYNC: the saw's next sample, where its phase starts) */
    }
    else if (!v->s[2])
        v->s[2] = 0x1234567 + (int32_t)v->age;        /* noise state */
}

/* SUB: the sine at the note (the sampler's test tone, an 808 played down the keys). HARM adds its octave (the sine
 * squared, the 2nd harmonic: a valve's, a sampler's warmth), ODD its twelfth (3 s - 4 s^3: a desk driven): both make
 * the sub heard on small speakers, where the fundamental is not (the ear rebuilds it from them). GRIT: the sine
 * through DRV (up to 16x) into the filter as a band-pass, CUT its centre, RES its width, added beside it. (Its own
 * function: tests/target_budget.py counts it apart from analog_render) */
__attribute__((noinline)) static void analog_sub(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m,
                                                  int32_t cut)
{
    const int16_t *p = t->p;
    int32_t odd = p[P_E1] * 129, grit = p[P_E2] * 516, harm = p[P_E3] * 258;   /* Q15: ODD up to 1/2, GRIT 2, HARM 1 */
    int32_t drive = 1 + p[P_E6] * 15 / 127, k = 8192 - p[P_E5] * 7600 / 127;   /* (k: tsvf_coef's damping) */
    uint32_t ph = v->ph[0], inc = m->inc, i;
    int32_t ic1 = v->s[0], ic2 = v->s[1];
    tsvf_t flt;
    tsvf_coef(&flt, cut, p[P_E5]);
    for (i = 0; i < n; i++) {
        int32_t s = osc_sine(ph), s2 = (s * s) >> 15, y = s >> 1;   /* (at half scale: the sum's headroom) */
        if (harm)
            y += mulq15(s2 - 16384, harm) >> 1;                     /* -cos 2x / 2 */
        if (odd)
            y += mulq15(3 * s - 4 * ((s2 * s) >> 15), odd) >> 1;    /* sin 3x */
        if (grit)
            y += mulq15(tsvf_bp(&flt, k, softclip(s * drive) >> 1, &ic1, &ic2), grit);
        ph += inc;
        out[i] += mulq15(mulq15(y, amp_at(m, i)), VOICE_FS) << 2;
    }
    v->ph[0] = ph;
    v->s[0] = ic1;
    v->s[1] = ic2;
}

/* FM's ratios (Q8), DTN / 8: the harmonic ones, the DX7 bell's 3.5, the cowbell's 1.48 (540 : 800 Hz) */
static const uint16_t FM_RATIO[16] = {128, 256, 379, 512, 640, 768, 896, 1024, 1280, 1536, 1792, 2048, 2304, 2560, 3072, 3584};

/* SYNC's slave saw (after Braids' RenderSaw, (c) 2012 Emilie Gillet, MIT): both its jumps, its own wrap and the
 * master's reset, are smoothed by a two-sample BLEP at the fraction of a sample where they fall, so the sweep does not
 * alias; the saw comes out a sample late. The fractions without a divide per sample: ph / inc as ph x (2^32 / inc),
 * that reciprocal taken once a block (ph < inc < 2^31: 0..65535) */
static inline uint32_t sync_frac(uint32_t ph, uint32_t rcp)
{
    uint32_t t = (ph >> 16) * rcp + (((ph & 0xFFFFu) * rcp) >> 16);
    return t > 65535u ? 65535u : t;
}
static inline int32_t blep_this(uint32_t t) { return (int32_t)(t * t >> 18); }          /* t: Q16 of a sample since */
static inline int32_t blep_next(uint32_t t) { t = 65535u - t; return -(int32_t)(t * t >> 18); }

/* HOOV, SYNC, FM: the leads' oscillators (docs/leads.md), then the same drive and filter as analog_render. (Its own
 * function: tests/target_budget.py counts it apart) */
__attribute__((noinline)) static void analog_lead(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m,
                                                   int32_t cut)
{
    const int16_t *p = t->p;
    uint32_t wave = (uint32_t)p[P_E0], i, inc1 = m->inc, inc2 = inc1, ph0 = v->ph[0], ph1 = v->ph[1], ph2 = v->ph[2];
    int32_t det = p[P_E1], mix = p[P_E2], extra = p[P_E3] * 258, sh = m->shape - (64 << 8);
    int32_t drive = 32768 + p[P_E6] * 768, dw = p[P_E6] * 258, drv = p[P_E6];
    int32_t ic1 = v->s[0], ic2 = v->s[1], fb = v->s[2], idx = 0, nx = v->s[3];
    uint32_t rcp1 = 0, rcp2 = 0;
    uint32_t pw = 0x80000000u + (uint32_t)(sh << 15);
    tsvf_t flt;
    tsvf_coef(&flt, cut, p[P_E5]);
    if (wave == AN_HOOV) {                            /* the second pulse-saw DTN cents up, NOIS the octave under */
        int32_t d16 = det * 16 / 100, rem = det * 16 - d16 * 100;
        inc2 = pitch_inc(clamp(m->pitch16 + d16, 0, 2047));
        inc2 += (uint32_t)((int32_t)(inc2 >> 12) * (rem * 2367 / 16000));
    } else if (wave == AN_SYNC) {                     /* the slave: DTN + SHP (the envelope's sweep) quarter semitones up */
        inc2 = pitch_inc(clamp(m->pitch16 + clamp(det + (sh >> 8), 0, 127) * 4, 0, 2047));
        rcp1 = 0xFFFFFFFFu / inc1;
        rcp2 = 0xFFFFFFFFu / inc2;
    } else {                                          /* FM: the modulator at the ratio, the index MIX + SHP */
        inc2 = (uint32_t)(((uint64_t)inc1 * FM_RATIO[(det >> 3) & 15]) >> 8);
        idx = clamp(mix * 258 + sh * 2, 0, 32767);
    }
    for (i = 0; i < n; i++) {
        int32_t s;
        if (wave == AN_HOOV) {
            int32_t a = (osc_saw(ph0, inc1) + osc_saw(ph0 + pw, inc1)) >> 1;
            int32_t b = (osc_saw(ph1, inc2) + osc_saw(ph1 + pw, inc2)) >> 1;
            s = mulq15(a, 32767 - mix * 258) + mulq15(b, mix * 258);
            if (extra)
                s += mulq15(osc_saw(ph2, inc1 >> 1), extra) >> 1;
            ph2 += inc1 >> 1;
            ph1 += inc2;
        } else if (wave == AN_SYNC) {
            int32_t m2 = mix * 258, cur = nx;
            uint32_t ph1n = ph1 + inc2, tm = 0;
            int reset = 0, wrap_before = 0;
            nx = 0;
            if (ph0 + inc1 < ph0) {                   /* the master wrapped tm ago: the slave starts over there */
                uint32_t at;
                int32_t jump;
                tm = sync_frac(ph0 + inc1, rcp1);
                at = ph1 + (65535u - tm) * (inc2 >> 16);   /* the slave's phase at the reset */
                wrap_before = at < ph1;
                jump = (int32_t)(at >> 17);
                cur -= jump * blep_this(tm) >> 15;
                nx -= jump * blep_next(tm) >> 15;
                reset = 1;
            }
            if ((wrap_before || !reset) && ph1n < ph1) {   /* the slave's own wrap */
                uint32_t ts = sync_frac(ph1n, rcp2);
                cur -= blep_this(ts);
                nx -= blep_next(ts);
            }
            if (reset)
                ph1n = tm * (inc2 >> 16);             /* as far in as it ran since */
            nx += (int32_t)(ph1n >> 17);
            ph1 = ph1n;
            s = mulq15((cur - 16384) * 2, 32767 - m2 / 2) + mulq15(osc_pulse(ph0, inc1, 0x80000000u), m2 / 2);
        } else {
            int32_t mo = osc_sine(ph1 + (uint32_t)(fb * extra >> 13) * 16384u);  /* (feedback: +-pi at NOIS 127) */
            fb = (fb + mo) >> 1;                      /* (the DX7's: the mean of the last two, no squeal) */
            s = osc_sine(ph0 + (uint32_t)(mulq15(mo, idx)) * 163840u);         /* (index up to ~8 radians) */
            ph1 += inc2;
        }
        ph0 += inc1;
        if (drv)
            s += mulq15(softclip(((s >> 2) * (drive >> 2)) >> 11) - s, dw);
        {
            int32_t y = tsvf_lp(&flt, s >> 1, &ic1, &ic2), a = y < 0 ? -y : y;
            if (a > 16000) {
                a = 16000 + (softclip((a - 16000) * 2) >> 1);
                y = y < 0 ? -a : a;
            }
            s = y << 1;
        }
        out[i] += mulq15(mulq15(s, amp_at(m, i)), VOICE_FS) << 1;
    }
    v->ph[0] = ph0;
    v->ph[1] = ph1;
    v->ph[2] = ph2;
    v->s[0] = ic1;
    v->s[1] = ic2;
    v->s[2] = fb;
    v->s[3] = nx;
}

static void analog_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *p = t->p;
    uint32_t wave = (uint32_t)p[P_E0], i;
    int32_t det = p[P_E1], mix = p[P_E2], noise = p[P_E3];
    int32_t cut = (p[P_E4] << 8) + m->cutoff + (p[P_E7] * (v->pitch16 - 60 * 16) >> 4);
    tsvf_t flt;
    if (wave == AN_SUB) {
        analog_sub(t, v, out, n, m, cut);
        return;
    }
    if (wave >= AN_HOOV) {
        analog_lead(t, v, out, n, m, cut);
        return;
    }
    int32_t drive = 32768 + p[P_E6] * 768;                       /* 1x .. 4x */
    int32_t dw = p[P_E6] * 258;                                  /* DRV: dry -> driven (a clean range low) */
    uint32_t inc1 = m->inc;
    /* DTN in cents: whole 1/16 semitones from the table, the rest as a fine factor */
    int32_t d16 = det * 16 / 100, rem = det * 16 - d16 * 100;            /* rem: 1/1600 semitone */
    uint32_t inc2 = pitch_inc(clamp(m->pitch16 + d16, 0, 2047));
    inc2 += (uint32_t)((int32_t)(inc2 >> 12) * (rem * 2367 / 16000));
    uint32_t rs = wave == AN_RSUB, inc0 = rs ? inc1 + inc1 - inc2 : inc1;   /* RSUB: the saws either side of the note */
    uint32_t pw = 0x80000000u + (uint32_t)((m->shape - (64 << 8)) << 15);
    int32_t m2 = mix * 258, m1 = 32767 - m2;                    /* osc mix Q15 */
    int32_t nz = noise * 200, drv = p[P_E6];
    uint32_t ph0 = v->ph[0], ph1 = v->ph[1], ph2 = v->ph[2];   /* state in locals: out[] may alias v->s[] */
    int32_t sub = wave == AN_RSUB ? noise * 258 : 0;            /* RSUB: NOIS is the sub's level */
    int32_t ic1 = v->s[0], ic2 = v->s[1], nst = v->s[2];
    tsvf_coef(&flt, cut, p[P_E5]);
    if (det == 0)
        inc2 = inc0 = inc1;
    for (i = 0; i < n; i++) {
        int32_t a, b = 0, s;
        switch (wave) {
        case 1:
            a = osc_pulse(ph0, inc1, 0x80000000u);
            if (m2)
                b = osc_pulse(ph1, inc2, 0x80000000u);
            break;
        case 2:
            a = osc_tri(ph0);
            if (m2)
                b = osc_tri(ph1);
            break;
        case 3:
            a = osc_sine(ph0);
            if (m2)
                b = osc_sine(ph1);
            break;
        case 4:
            a = osc_pulse(ph0, inc1, pw);
            if (m2)
                b = osc_pulse(ph1, inc2, pw);
            break;
        default:
            a = osc_saw(ph0, inc0);
            if (m2)
                b = osc_saw(ph1, inc2);
            if (rs) {                                 /* RSUB: a saw is -2/pi sin x + its harmonics: the sine back */
                a += mulq15(osc_sine(ph0), 20861);
                b += mulq15(osc_sine(ph1), 20861);
            }
            break;
        }
        ph0 += inc0;
        ph1 += inc2;
        s = m2 ? mulq15(a, m1) + mulq15(b, m2) : a;  /* (MIX 0, the 808s and subs: one oscillator) */
        if (sub) {
            s = s >> 1;                               /* (RSUB: the saws at half, the sub after the filter) */
        } else if (nz)
            s += mulq15((int32_t)(noise32(&nst) >> 17) - 16384, nz);
        if (drv)                                      /* pre-shifts: drive is up to 4x, no overflow */
            s += mulq15(softclip(((s >> 2) * (drive >> 2)) >> 11) - s, dw);
        {   /* filter: linear up to half scale, then a soft knee (only resonance peaks saturate) */
            int32_t y = tsvf_lp(&flt, s >> 1, &ic1, &ic2), a = y < 0 ? -y : y;
            if (a > 16000) {
                a = 16000 + (softclip((a - 16000) * 2) >> 1);
                y = y < 0 ? -a : a;
            }
            s = y << 1;
        }
        if (sub) {                                    /* RSUB: the sine at the note, clean */
            s += mulq15(osc_sine(ph2), sub);
            ph2 += inc1;
        }
        out[i] += mulq15(mulq15(s, amp_at(m, i)), VOICE_FS) << 1;
    }
    v->ph[0] = ph0;
    v->ph[1] = ph1;
    v->ph[2] = ph2;
    v->s[0] = ic1;
    v->s[1] = ic2;
    v->s[2] = nst;
}

static const preset_t ANALOG_PRESETS[] = {
    /* SLOOP hip-hop / drum & bass bank. {WAVE, DTN, MIX, NOIS, CUT, RES, DRV, KTR}, {A D S R}, fenv, mono */
    /* 808s: a sine that decays (SUS 0) with a little pitch drop (ENV -> PITCH) and slides (GLIDE, legato) */
    {"808 BOOM", {3, 0, 0, 0, 127, 0, 48, 0}, {0, 112, 0, 52}, 0, 1, FX(0, 0, 0, 0), XP(P_GLIDE + 1, 59, P_GLMODE + 1, 1, P_ED_PIT + 1, 18, P_TRANS + 1, -24)},
    {"808 DIRTY", {3, 0, 0, 0, 96, 12, 118, 30}, {0, 108, 0, 46}, 0, 1, FX(24, 0, 0, 0), XP(P_GLIDE + 1, 56, P_GLMODE + 1, 1, P_ED_PIT + 1, 24, P_TRANS + 1, -24)},
    {"SUB BASS", {3, 0, 0, 0, 127, 0, 10, 0}, {0, 60, 118, 24}, 0, 1, FX(0, 0, 0, 0), XP(P_GLIDE + 1, 40, P_TRANS + 1, -24)},
    {"808 SLIDE", {3, 0, 0, 0, 127, 0, 72, 0}, {0, 118, 0, 60}, 0, 1, FX(30, 0, 0, 0), XP(P_GLIDE + 1, 82, P_GLMODE + 1, 1, P_ED_PIT + 1, 12, P_TRANS + 1, -24)},
    /* acid: one saw, high resonance, the envelope on the filter, slides where notes overlap */
    {"ACID 303", {0, 0, 0, 0, 36, 112, 44, 64}, {0, 56, 24, 20}, 52, 1, FX(28, 0, 22, 10), XP(P_GLIDE + 1, 34, P_GLMODE + 1, 1, P_TRANS + 1, -12)},
    /* plugg / soft trap: a round triangle bass */
    {"PLUGG BASS", {2, 0, 0, 0, 92, 0, 30, 0}, {0, 96, 92, 34}, 0, 1, FX(0, 0, 0, 6), XP(P_GLIDE + 1, 52, P_GLMODE + 1, 1, P_TRANS + 1, -24)},
    /* drum & bass: two detuned saws, slowly moving filter */
    {"REESE", {0, 22, 64, 0, 56, 22, 36, 40}, {0, 70, 118, 28}, 0, 1, FX(0, 22, 0, 6), XP(P_LRATE + 1, 22, P_LD_FLT + 1, 10, P_TRANS + 1, -24)},
    {"WOBBLE", {0, 9, 64, 0, 38, 72, 62, 30}, {0, 64, 127, 22}, 0, 1, FX(10, 0, 0, 4), XP(P_LRATE + 1, 74, P_LD_FLT + 1, 40, P_TRANS + 1, -24)},
    /* west coast: a square bass that slides */
    {"FUNK BASS", {1, 6, 50, 0, 50, 30, 22, 64}, {0, 55, 62, 20}, 30, 1, FX(0, 0, 0, 4), XP(P_GLIDE + 1, 50, P_GLMODE + 1, 1, P_TRANS + 1, -24)},
    /* FMX, the CHOP's INST leads (docs/leads.md): in the old leads' places (the basses after keep their numbers), the
     * rest at the end */
    {"HOOVER LD", {8, 18, 64, 20, 84, 14, 30, 64}, {0, 80, 116, 36}, 0, 1, FX(0, 56, 18, 20),
     XP(P_ED_PIT + 1, -16, P_LD_SHP + 1, 30, P_LRATE + 1, 40, P_GLIDE + 1, 36)},
    {"ACID LEAD", {0, 0, 0, 0, 56, 108, 40, 64}, {0, 60, 60, 20}, 50, 1, FX(20, 0, 24, 10), XP(P_GLIDE + 1, 30, P_GLMODE + 1, 1)},
    {"JUNO LEAD", {4, 6, 64, 0, 92, 16, 0, 64}, {2, 70, 112, 36}, 8, 1, FX(0, 70, 24, 24), XP(P_LD_SHP + 1, 28, P_LRATE + 1, 50)},
    {"J-CORE SAW", {0, 14, 64, 0, 104, 8, 6, 64}, {2, 70, 116, 40}, 6, 1, FX(0, 16, 30, 30), XP(P_VOICE + 1, 3, P_DETUNE + 1, 70, P_GLIDE + 1, 24)},
    {"WARM PAD", {4, 12, 64, 6, 52, 8, 0, 32}, {75, 90, 115, 90}, 0, 0, FX(0, 55, 20, 55), XP(P_LRATE + 1, 30, P_LD_SHP + 1, 20)},
    {"DARK STR", {0, 18, 64, 0, 48, 6, 0, 32}, {60, 90, 118, 85}, 0, 0, FX(0, 50, 18, 60)},
    {"ATMOS PAD", {4, 25, 64, 20, 60, 15, 0, 32}, {90, 100, 120, 100}, 0, 0, FX(0, 60, 30, 75), XP(P_LRATE + 1, 18, P_LD_PIT + 1, 1)},
    /* (the INST leads, docs/leads.md) */
    {"CHIP PULSE", {4, 0, 0, 0, 127, 0, 0, 0}, {0, 60, 110, 10}, 0, 1, FX(0, 0, 22, 10),
     XP(P_ED_SHP + 1, -48, P_LD_PIT + 1, 1, P_LRATE + 1, 90, P_LFADE + 1, 60)},
    {"GAME SQR", {1, 6, 50, 0, 96, 10, 10, 64}, {0, 70, 108, 30}, 10, 1, FX(0, 20, 30, 24),
     XP(P_ED_PIT + 1, -12, P_GLIDE + 1, 30, P_LD_PIT + 1, 1, P_LRATE + 1, 88)},
    {"MONO BASS", {0, 4, 64, 0, 48, 36, 30, 64}, {0, 60, 70, 20}, 44, 1, FX(10, 0, 0, 4), XP(P_TRANS + 1, -24)},
    /* (an INST lead) */
    {"SYNC LEAD", {9, 12, 20, 0, 100, 10, 20, 64}, {0, 76, 100, 30}, 0, 1, FX(0, 10, 26, 18),
     XP(P_ED_SHP + 1, 48, P_GLIDE + 1, 30, P_LD_PIT + 1, 1, P_LRATE + 1, 90)},
    {"SAW BASS", {0, 6, 64, 0, 52, 30, 40, 64}, {0, 60, 60, 20}, 30, 1, FX(8, 0, 0, 4), XP(P_TRANS + 1, -24)},
    {"SINE PAD", {3, 10, 64, 4, 90, 0, 0, 32}, {80, 90, 118, 90}, 0, 0, FX(0, 60, 20, 65)},
    /* FMX: the jungle basses, each one of the scene's own (docs/bass.md: how they were made, why they sound so). The
     * BASS phrase plays them where a sub belongs, the root from E1 to D#2 (bassline.c bl_note); TRANS -24 puts the
     * keys' C4 at C2. (Their trims are set by hand, preset_trim.h: tools/level_presets.py skips them)
     *   REESE SUB   '94: the CZ reese sampled and played down the keys (Terrorist): slow beating, its own clean sub
     *   DARK REESE  '97 techstep: wider, driven before a lower, resonant filter that the LFO turns
     *   JUNGLE SUB  the sampler's sine test tone held long, a little octave and twelfth to be heard on any speaker
     *   808 JUNGLE  an 808 boom sampled and played as a bass: the pitch falling in, a long decay, the desk's grit
     *   HOOVER      the Alpha Juno's: moving pulse width, two detuned, chorus, the pitch scooping up into the note
     *   VALVE SUB   the sound system's: a clean sub and, beside it, the same sine driven into a mid band
     *   DUB BASS    ragga jungle's roots bass: round, plucked, a valve amp's bark opening with each note
     *   DRED BASS   the reversed acid line (Dead Dred): each note swells in, the filter opening, and cuts */
    {"REESE SUB", {5, 20, 64, 72, 70, 24, 40, 40}, {0, 70, 118, 36}, 0, 1, FX(0, 12, 0, 6),
     XP(P_LRATE + 1, 20, P_LD_FLT + 1, 12, P_GLIDE + 1, 30, P_TRANS + 1, -24)},
    {"DARK REESE", {5, 44, 64, 84, 52, 44, 96, 30}, {0, 70, 120, 36}, 12, 1, FX(16, 10, 0, 6),
     XP(P_LRATE + 1, 34, P_LD_FLT + 1, 22, P_GLIDE + 1, 20, P_TRANS + 1, -24)},
    {"JUNGLE SUB", {6, 8, 0, 26, 90, 0, 0, 0}, {0, 80, 122, 34}, 0, 1, FX(0, 0, 0, 0),
     XP(P_GLIDE + 1, 48, P_GLMODE + 1, 1, P_TRANS + 1, -24)},
    {"808 JUNGLE", {6, 16, 30, 48, 72, 10, 70, 40}, {0, 112, 0, 60}, 0, 1, FX(0, 0, 0, 0),
     XP(P_GLIDE + 1, 70, P_GLMODE + 1, 1, P_ED_PIT + 1, 24, P_TRANS + 1, -24)},
    {"HOOVER", {4, 34, 64, 0, 80, 16, 40, 50}, {0, 80, 110, 40}, 0, 1, FX(0, 48, 0, 20),
     XP(P_ED_PIT + 1, -20, P_GLIDE + 1, 40, P_LD_SHP + 1, 36, P_TRANS + 1, -12)},
    {"VALVE SUB", {6, 0, 100, 20, 52, 10, 110, 50}, {0, 80, 120, 34}, 16, 1, FX(0, 0, 0, 0),
     XP(P_GLIDE + 1, 40, P_GLMODE + 1, 1, P_TRANS + 1, -24)},
    {"DUB BASS", {6, 20, 80, 50, 46, 16, 56, 40}, {0, 64, 84, 44}, 24, 1, FX(0, 0, 0, 8),
     XP(P_GLIDE + 1, 56, P_GLMODE + 1, 1, P_TRANS + 1, -24)},
    {"DRED BASS", {7, 8, 30, 0, 40, 100, 60, 50}, {72, 60, 127, 8}, 40, 1, FX(10, 0, 18, 8),
     XP(P_GLIDE + 1, 20, P_TRANS + 1, -24)},
    /* FMX, the CHORDS pads (docs/pads.md): Vangelis's CS-80 brass pad (Blade Runner): detuned saws, a resonant low-pass
     * the slow envelope opens, a vibrato that comes in late (the CS-80's ribbon and aftertouch) */
    {"BLUE PAD", {0, 16, 64, 0, 34, 22, 8, 64}, {56, 100, 118, 96}, 44, 0, FX(0, 30, 26, 74),
     XP(P_LD_PIT + 1, 2, P_LRATE + 1, 74, P_LFADE + 1, 90)},
    /* FMX, the rest of the CHOP's INST leads (docs/leads.md) */
    {"LIQUID FLT", {2, 0, 0, 22, 84, 6, 0, 64}, {16, 70, 112, 44}, 0, 1, FX(0, 10, 30, 52),
     XP(P_ED_PIT + 1, 6, P_LD_PIT + 1, 1, P_LRATE + 1, 89, P_LFADE + 1, 70)},
    {"FM BELL", {10, 48, 6, 0, 110, 0, 0, 0}, {0, 96, 30, 60}, 0, 0, FX(0, 20, 30, 40), XP(P_ED_SHP + 1, 26)},
    {"16BIT FM", {10, 8, 64, 70, 110, 0, 20, 64}, {0, 70, 110, 30}, 0, 1, FX(0, 0, 24, 14),
     XP(P_ED_SHP + 1, 30, P_LD_PIT + 1, 1, P_LRATE + 1, 92, P_LFADE + 1, 60)},
    {"COWBELL", {10, 16, 16, 0, 96, 0, 30, 64}, {0, 64, 0, 40}, 0, 0, FX(20, 0, 20, 16), XP(P_ED_SHP + 1, 16)},
};

/* RSUB: NOIS is the sub's level; SUB: DTN, MIX, NOIS, CUT the sine's ODD, GRIT, HARM and the grit's BAND */
static const param_desc_t AN_SUB_DESC = {"SUB", F_PCT, 0, 127, 0, 0, 0};
static const param_desc_t AN_SINE_DESC[4] = {
    {"ODD", F_PCT, 0, 127, 10, 0, 0}, {"GRIT", F_PCT, 0, 127, 64, 0, 0}, {"HARM", F_PCT, 0, 127, 0, 0, 0},
    {"BAND", F_CUTOFF, 0, 127, 90, 0, 0},
};
/* the leads: HOOV's NOIS the octave under; SYNC's DTN the slave's pitch; FM's DTN the ratio, MIX the index, NOIS the
 * feedback */
static const param_desc_t AN_OCT_DESC = {"OCT", F_PCT, 0, 127, 0, 0, 0};
static const param_desc_t AN_SYNC_DESC = {"SYNC", F_INT, 0, 127, 24, 0, 0};
static const param_desc_t AN_FM_DESC[3] = {
    {"RATIO", F_INT, 0, 127, 8, 0, 0}, {"INDEX", F_PCT, 0, 127, 40, 0, 0}, {"FB", F_PCT, 0, 127, 0, 0, 0},
};
static const param_desc_t *analog_desc(const struct track *t, uint32_t k)
{
    if (t->p[P_E0] == (int16_t)AN_SUB)
        return k >= 1u && k <= 4u ? &AN_SINE_DESC[k - 1u] : 0;
    if (t->p[P_E0] == (int16_t)AN_FM)
        return k >= 1u && k <= 3u ? &AN_FM_DESC[k - 1u] : 0;
    if (t->p[P_E0] == (int16_t)AN_SYNC)
        return k == 1u ? &AN_SYNC_DESC : 0;
    if (t->p[P_E0] == (int16_t)AN_HOOV)
        return k == 3u ? &AN_OCT_DESC : 0;
    return k == 3u && t->p[P_E0] == (int16_t)AN_RSUB ? &AN_SUB_DESC : 0;
}
/* DRED: the envelope cubed, the attack a decay played backwards (slow, then rushing to the top); the filter's env too */
static int32_t analog_amp(track_t *t, voice_t *v, int32_t adsr)
{
    (void)v;
    return t->p[P_E0] == (int16_t)AN_DRED ? mulq15(mulq15(adsr, adsr), adsr) : adsr;
}

static const engine_t ENG_ANALOG = {
    "ANALOG", {"OSC", "FLT"},
    {
        {"WAVE", F_ENUM, 0, 10, 0, N_ANALOG_WAVE, 0},
        {"DTN", F_INT, 0, 127, 10, 0, "ct"},
        {"MIX", F_PCT, 0, 127, 64, 0, 0},
        {"NOIS", F_PCT, 0, 127, 0, 0, 0},
        {"CUT", F_CUTOFF, 0, 127, 90, 0, 0},
        {"RES", F_PCT, 0, 127, 30, 0, 0},
        {"DRV", F_PCT, 0, 127, 0, 0, 0},
        {"KTR", F_PCT, 0, 127, 64, 0, 0},
    },
    ANALOG_PRESETS, sizeof(ANALOG_PRESETS) / sizeof(ANALOG_PRESETS[0]), 1, analog_note_on, analog_render,
    0xF986, {P_E4, P_E5, P_ATK, P_REL}, 0, analog_amp, analog_desc,
};
