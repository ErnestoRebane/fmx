/* SPDX-License-Identifier: GPL-3.0-only */
/* Host test of the BASS track's phrase generator (firmware/src/bassline.c):
 *   the phrase   the same settings give the same steps, VARIANT others; the first note the root; every note of the key,
 *                in range; MOVE 0: the root only (OCT: and its octave); HOLD SHORT / LONG; ANSWER SAME / REST / RUN;
 *                RHYTHM KICKS on the kicks of the BREAK's loop A; the steps well formed (a TIE only after a note)
 *   the change   LOCK: the steps stay as they play; CHANGE 100: they change, the first note stays the root, well formed;
 *                steps the generator did not write never change
 *   playing      the bass plays the phrase's notes
 * and writes a session demo (the BREAK in JUNGLE, the phrase on REESE SUB, a PSX pad holding the chords) to
 * DIR/session-bass.wav.
 *   tests/bassline_test DIR */
#define main hostsim_main
#include <stddef.h>
#include <stdarg.h>
#include "hostsim.c"
#undef main

static int fails;
static void check(const char *what, int ok, const char *fmt, ...)
{
    printf("bass: %-76s %s", what, ok ? "ok" : "FAIL");
    if (fmt) {
        va_list ap;
        va_start(ap, fmt);
        printf("  (");
        vprintf(fmt, ap);
        printf(")");
        va_end(ap);
    }
    printf("\n");
    fails += !ok;
}
static void key_of(uint32_t root, uint32_t scale)
{
    uint32_t i;
    for (i = 0; i < NPART; i++) {
        trk[i].p[P_ROOT] = (int16_t)root;
        trk[i].p[P_SCALE] = (int16_t)scale;
    }
}
static void fresh(void)
{
    host_tracks_init();
    key_of(9, 2);                                    /* A minor */
    bl_defaults();
    blr.changed = -1;
    host_preset(&trk[0], ENGI_ANALOG, 24);           /* JUNGLE SUB */
    brkp_poll();
}
static int well_formed(const step_t *st, uint32_t n)  /* a TIE only after a note or a TIE */
{
    uint32_t i;
    if (st[0].time == ST_TIE)
        return 0;
    for (i = 1; i < n; i++)
        if (st[i].time == ST_TIE && !(st[i - 1u].time == ST_TIE || bl_step_on(&st[i - 1u])))
            return 0;
    return 1;
}
static int in_key(uint32_t note)                     /* A minor */
{
    static const uint8_t AM[12] = {1, 0, 1, 1, 0, 1, 0, 1, 1, 0, 1, 0};   /* from A */
    return AM[(note + 12u - 9u) % 12u];
}
static uint32_t note_len(const step_t *st, uint32_t i, uint32_t n)
{
    uint32_t e = i + 1u;
    while (e < n && st[e].time == ST_TIE)
        e++;
    return e - i;
}
#define BARS_BLOCKS(n, bpm) ((uint32_t)((n) * 4.0 * 60.0 / (bpm) * FS / CTL) - 1u)

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "build/host";
    static step_t a[BL_STEPS], b[BL_STEPS];
    uint32_t v, i, r, an, h;

    /* ---- the phrase */
    fresh();
    {
        uint32_t same = 1, differ = 0, root1 = 1, key = 1, range = 1, wf = 1, root = bl_note(0);
        for (r = 0; r < BR_N; r++)
            for (an = 0; an < BA_N; an++)
                for (h = 0; h < BH_N; h++)
                    for (v = 0; v < 12u; v++) {
                        bl.rhythm = (uint8_t)r;
                        bl.answer = (uint8_t)an;
                        bl.hold = (uint8_t)h;
                        bl.move = (uint8_t)(v * 9u);
                        bl.var = (uint16_t)(v * 37u);
                        bl_build(a);
                        bl_build(b);
                        same &= !memcmp(a, b, sizeof a);
                        root1 &= bl_step_on(&a[0]) && a[0].note[0] == root;
                        wf &= well_formed(a, BL_STEPS);
                        for (i = 0; i < BL_STEPS; i++)
                            if (bl_step_on(&a[i])) {
                                key &= in_key(a[i].note[0]);
                                range &= a[i].note[0] + 12u >= root && a[i].note[0] <= root + 26u;
                            }
                    }
        check("the same settings: the same steps", same, 0);
        check("the first note: the root (A2), every rhythm, answer, hold, variant", root1, 0);
        check("every note of A minor, from an octave under the root to two over", key && range, 0);
        check("the steps well formed (a TIE only after a note)", wf, 0);
        bl_defaults();
        for (v = 0; v < 40u; v++) {
            bl.var = (uint16_t)v;
            bl_build(a);
            bl.var = (uint16_t)(v + 1u);
            bl_build(b);
            differ += memcmp(a, b, sizeof a) != 0;
        }
        check("the next VARIANT: another phrase", differ >= 36u, "%u of 40", differ);
    }
    {
        uint32_t root = bl_note(0), only = 1, oct = 1, shorts = 1, legato = 1, same = 1, rest = 1, run = 1;
        for (v = 0; v < 60u; v++) {
            bl_defaults();
            bl.var = (uint16_t)v;
            bl.move = 0;
            bl.answer = BA_SAME;
            bl_build(a);
            for (i = 0; i < BL_STEPS; i++)
                only &= !bl_step_on(&a[i]) || a[i].note[0] == root;
            same &= !memcmp(a, a + 16, sizeof a[0] * 16u);
            bl.answer = BA_OCT;
            bl_build(a);
            for (i = 0; i < BL_STEPS; i++)
                oct &= !bl_step_on(&a[i]) || a[i].note[0] == root || a[i].note[0] == root + 12u;
            bl.move = 50;
            bl.rhythm = BR_HALF;                        /* (KICKS writes the line legato: HOLD is its strikes') */
            bl.answer = BA_REST;
            bl.hold = BH_SHORT;
            bl_build(a);
            for (i = 0; i < BL_STEPS; i++) {
                if (bl_step_on(&a[i]))
                    shorts &= note_len(a, i, BL_STEPS) <= 2u;
                if (i >= 24u)
                    rest &= !bl_step_on(&a[i]);
            }
            bl.hold = BH_LONG;
            bl.answer = BA_RUN;
            bl_build(a);
            for (i = 0; i < BL_STEPS; i++)
                legato &= a[i].time != ST_REST;
            run &= bl_step_on(&a[26]) && bl_step_on(&a[28]) && bl_step_on(&a[30]);
        }
        check("MOVE 0, ANSWER SAME: the root only, bar 2 as bar 1", only && same, 0);
        check("MOVE 0, ANSWER OCT: the root and its octave", oct, 0);
        check("HOLD SHORT: no note longer than two steps; ANSWER REST: bar 2 silent from its 3rd beat", shorts && rest, 0);
        check("HOLD LONG: legato, no rest; ANSWER RUN: notes on the last three eighths", legato && run, 0);
    }
    {
        uint32_t k = bl_kicks(), on = 0, hit = 0;
        for (v = 0; v < 40u; v++) {
            bl_defaults();
            bl.var = (uint16_t)v;
            bl.rhythm = BR_KICKS;
            bl.answer = BA_SAME;
            bl_build(a);
            for (i = 0; i < BL_STEPS; i++)
                if (bl_step_on(&a[i])) {
                    on++;
                    hit += (k >> i) & 1u;
                }
        }
        check("RHYTHM KICKS: the notes on the kicks of loop A (AMEN 2), most of them", k && hit * 100u >= on * 70u,
              "kicks %08x, %u of %u", k, hit, on);
    }

    /* ---- the change, playing */
    {
        uint32_t n, heard = 0, notes = 0, k;
        int32_t o[2 * CTL];
        fresh();
        song.g[G_BPM] = 170;
        bl.change = 0;
        bl.rhythm = BR_HALF;                             /* (its steps as written: not KICKS) */
        bl_write();
        memcpy(a, trk[0].step, sizeof a);
        for (i = 0; i < BL_STEPS; i++)
            notes += bl_step_on(&a[i]);
        transport_req = 1;
        vis_hit = 0;
        for (k = 0; k < BARS_BLOCKS(2, 170); k++) {
            mix_block(o, CTL);
            if (vis_hit & 1u) {
                heard++;
                vis_hit = 0;
            }
        }
        check("RHYTHM HALF: the bass plays the phrase's notes (two bars)", heard == notes, "%u of %u", heard, notes);
        for (k = 0; k < BARS_BLOCKS(14, 170); k++)
            mix_block(o, CTL);
        check("LOCK: the steps as written, eight times round", !memcmp(a, trk[0].step, sizeof a), 0);
        bl.change = 100;
        for (k = 0; k < BARS_BLOCKS(16, 170); k++)
            mix_block(o, CTL);
        n = 0;
        for (i = 0; i < BL_STEPS; i++)
            n += memcmp(&a[i], &trk[0].step[i], sizeof a[i]) != 0;
        check("CHANGE 100: changed in places, the first note still the root, well formed",
              n >= 2u && bl_step_on(&trk[0].step[0]) && trk[0].step[0].note[0] == bl_note(0) &&
              well_formed(trk[0].step, BL_STEPS), "%u steps changed", n);
        transport_req = 2;
        for (k = 0; k < 8u; k++)
            mix_block(o, CTL);
        bl.mix &= 0x7Fu;                                 /* steps the generator did not write */
        memcpy(a, trk[0].step, sizeof a);
        transport_req = 1;
        for (k = 0; k < BARS_BLOCKS(16, 170); k++)
            mix_block(o, CTL);
        transport_req = 2;
        check("... never steps it did not write", !memcmp(a, trk[0].step, sizeof a), 0);
    }

    /* ---- KICKS, playing: the bass on the kicks the BREAK plays (its JUNGLE style moves them about) */
    {
        uint32_t k, kicks = 0, with = 0, strikes = 0, lone = 0, kn = 0, sinceb = 99, sincek = 99, pos, lastp = 0xFFFFu;
        int32_t o[2 * CTL];
        fresh();
        song.g[G_BPM] = 170;
        brk_style(TBRK, BS_JUNGLE);
        bl.change = 0;
        bl_write();
        check("KICKS: the line written legato (no rest), the loop A has kicks", bl_live(), 0);
        transport_req = 1;
        kn = bp.kicks_n;
        vis_hit = 0;
        for (k = 0; k < BARS_BLOCKS(8, 170); k++) {
            uint32_t kick, bass;
            mix_block(o, CTL);
            kick = bp.kicks_n != kn;
            kn = bp.kicks_n;
            bass = vis_hit & 1u;
            vis_hit = 0;
            pos = clk_beat * 4u + clk_pos / (BEAT_U / 4u);
            sinceb = bass ? 0u : sinceb + 1u;
            sincek = kick ? 0u : sincek + 1u;
            if (kick && (lastp == 0xFFFFu || pos - lastp >= 2u || sinceb <= 1u)) {   /* (an eighth after the last strike) */
                kicks++;
                with += sinceb <= 1u;                  /* (struck in the same block, or the one before) */
            }
            if (bass) {
                lastp = pos;
                strikes++;
                lone += sincek > 1u && pos % 16u != 0u;   /* neither a kick nor a bar's first step */
            }
        }
        transport_req = 2;
        for (k = 0; k < 8u; k++)
            mix_block(o, CTL);
        check("KICKS: the bass strikes with the BREAK's kicks (each an eighth after the last strike)", kicks && with * 100u >= kicks * 95u,
              "%u of %u kicks", with, kicks);
        check("KICKS: no strike but on a kick or a bar's first step", strikes && !lone, "%u strikes, %u lone", strikes, lone);
    }

    /* ---- the demo: the BREAK in JUNGLE, the phrase on REESE SUB (changing a little), a PSX pad on the chords */
    {
        char path[512];
        FILE *w;
        uint32_t frames = (uint32_t)(8.0 * 4.0 * 60.0 / 170.0 * FS), fr, pk = 0;
        fresh();
        snprintf(path, sizeof path, "%s/session-bass.wav", dir);
        if (!(w = fopen(path, "wb")))
            return 1;
        song.g[G_BPM] = 170;
        song.g[G_DUCK] = GP[G_DUCK].def;
        TBRK->p[P_REV] = 16;
        brk_style(TBRK, BS_JUNGLE);
        host_preset(&trk[0], ENGI_ANALOG, 22);       /* REESE SUB */
        bl.var = 3;
        bl.move = 40;
        bl.change = 25;
        bl_write();
        host_preset(&trk[1], ENGI_SAMPLE, 0);        /* PSX PAD */
        trk[1].p[P_LEVEL] = 90;
        cg.play = CP_HOLD;
        cg.rate = 2;
        cg.num = 4;
        cg.color = CC_9TH;
        cg.var = 11;
        cg.follow = 1;
        cg_poll();
        wav_hdr(w, frames + FS);
        transport_req = 1;
        for (fr = 0; fr < frames + FS; fr += CTL) {
            int32_t o[2 * CTL];
            if (fr >= frames && fr < frames + CTL)
                transport_req = 2;
            mix_block(o, CTL);
            for (i = 0; i < CTL; i++) {
                pk = (uint32_t)abs(o[2 * i]) > pk ? (uint32_t)abs(o[2 * i]) : pk;
                wav_put(w, o[2 * i], o[2 * i + 1]);
            }
        }
        fclose(w);
        printf("bass: demo session-bass.wav, 8 bars at 170 BPM (the BREAK, REESE SUB on the phrase, PSX PAD), peak %u\n", pk);
        fails += pk < 2000u || pk > 32767u;
    }
    printf(fails ? "bass: %d FAILED\n" : "bass: all ok\n", fails);
    return fails != 0;
}
