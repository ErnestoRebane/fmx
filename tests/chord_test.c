/* SPDX-License-Identifier: GPL-3.0-only */
/* Host test of the CHORDS generator (firmware/src/chordgen.c) and its screen's model:
 *   the progression  the same VARIANT gives the same chords; the first on the tonic; no chord twice in a row; the last
 *                    one leads back to the first; MOOD 0 stays in the key, MOOD 100 borrows; a pinned node is kept
 *                    when VARIANT turns; CHORDS 2..8
 *   the voicing      COLOR's tones (triad 3, 7th 4, lush 5), names (Am7, Fmaj7, G7, Bbmaj9 ...), the voices move little
 *   playing          HOLD: the track's voices hold the chord, a new one on the bar (RATE); STEPS: a step's note
 *                    strikes the chord of the moment; a node's key held plays it stopped; BASS FOLLOW moves the bass
 *   the project      the generator's settings in a project and back, clamped
 * and writes a session demo (the BREAK, the bass following the chords, a pad holding them) to DIR/session-chords.wav.
 *   tests/chord_test DIR */
#define main hostsim_main
#include <stddef.h>
#include <stdarg.h>
#include "hostsim.c"
#undef main
#define PROJ_HOST 1
#include "../firmware/src/project.c"

static int fails;
static void check(const char *what, int ok, const char *fmt, ...)
{
    printf("chords: %-74s %s", what, ok ? "ok" : "FAIL");
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

static void key_of(uint32_t root, uint32_t scale)    /* the session's key: every part, as SEL + key sets it */
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
    cg_defaults();
    key_of(9, 2);                                    /* A minor */
    cgr.nlead = 0;
    cgr.live = 0;
    cg_poll();
}
static uint32_t sounding(const track_t *t, uint8_t *n)   /* the notes held on a part */
{
    uint32_t i, k = 0;
    for (i = 0; i < NVOICE; i++)
        if (t->v[i].active && t->v[i].gate)
            n[k++] = t->v[i].note;
    return k;
}
static int same_set(const uint8_t *a, uint32_t na, const uint8_t *b, uint32_t nb)
{
    uint32_t i, j;
    if (na != nb)
        return 0;
    for (i = 0; i < na; i++) {
        for (j = 0; j < nb && a[i] != b[j]; j++)
            ;
        if (j == nb)
            return 0;
    }
    return 1;
}
static void blocks(uint32_t n)
{
    int32_t o[2 * CTL];
    while (n--)
        mix_block(o, CTL);
}
#define BAR_BLOCKS(bpm) ((uint32_t)(4.0 * 60.0 / (bpm) * FS / CTL + 0.5))

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "build/host";
    uint32_t v, i, k;
    uint8_t deg[7];

    /* ---- the progression */
    {
        uint8_t a[CG_MAX], b[CG_MAX];
        uint32_t same = 1, tonic = 0, rep = 0, home = 0, inkey = 1, total = 0, borrowed = 0;
        fresh();
        cg_scale(cg_mask(), deg);
        for (v = 0; v < 200u; v++) {
            cg.var = (uint16_t)v;
            cg.num = (uint8_t)(2u + v % 7u);
            cg.mood = 0;
            cg_generate(a, cg_mask());
            cg_generate(b, cg_mask());
            same &= !memcmp(a, b, sizeof a);
            tonic += (a[0] & 15u) == 0u;
            for (i = 0; i < cg.num; i++) {
                uint32_t prev = a[(i + cg.num - 1u) % cg.num], d0 = cg_degree(deg, prev & 15u), d1 = cg_degree(deg, a[i] & 15u);
                if (i)
                    rep += a[i] == a[i - 1u];
                if (i == 0u)
                    home += CG_MINOR[d0][d1] > 0u;    /* the last one leads back to the first */
                for (k = 0; k < 7u && deg[k] != (a[i] & 15u); k++)
                    ;
                inkey &= k < 7u && ((a[i] >> 4) & 3u) == cg_triad(deg, k);
            }
            cg.mood = 100;
            cg_generate(a, cg_mask());
            for (i = 1; i < cg.num; i++) {
                for (k = 0; k < 7u && deg[k] != (a[i] & 15u); k++)
                    ;
                borrowed += k == 7u || ((a[i] >> 4) & 3u) != cg_triad(deg, k);
                total++;
            }
        }
        check("the same VARIANT gives the same chords", same, 0);
        check("the first chord on the tonic (MOOD 0)", tonic == 200u, "%u of 200", tonic);
        check("no chord twice in a row", rep == 0u, "%u", rep);
        check("the last chord leads back to the first (the loop has no seam)", home == 200u, "%u of 200", home);
        check("MOOD 0: every chord in the key, of its degree's quality", inkey, 0);
        check("MOOD 100: chords borrowed from the parallel mode, now and then", borrowed * 100u > total * 10u &&
              borrowed * 100u < total * 70u, "%u of %u", borrowed, total);
    }
    {   /* pins: a node kept while VARIANT turns, the others change */
        uint8_t a[CG_MAX];
        uint32_t kept = 1, moved = 0;
        fresh();
        cg.num = 4;
        cg.var = 7;
        cg_poll();
        memcpy(a, cgr.prog, sizeof a);
        cg_pin(2, 1);
        for (v = 8; v < 40u; v++) {
            cg.var = (uint16_t)v;
            cg_poll();
            kept &= cgr.prog[2] == a[2];
            moved += memcmp(cgr.prog, a, 4) != 0;
        }
        check("a pinned node keeps its chord while VARIANT turns, the others change", kept && moved > 20u, "%u moved", moved);
        cg_pin(2, 0);
        cg.var = 9;
        cg_poll();
        check("unpinned: it changes with the rest again", !(cg.pins & 4u), 0);
    }
    /* ---- the voicing and the names */
    {
        static const struct { uint8_t color, tones; } CT[] = {{CC_TRIAD, 3}, {CC_7TH, 4}, {CC_ADD9, 4}, {CC_9TH, 5},
                                                             {CC_SUS, 4}, {CC_LUSH, 5}};
        uint8_t n[6], iv[6];
        char nm[8];
        uint32_t ok = 1, worst = 0, c;
        fresh();
        cg.num = 4;
        cg.var = 1;
        cg.color = CC_TRIAD;
        cg_poll();
        cg_name(0, nm);
        check("names: A minor's tonic triad is Am", !strcmp(nm, "Am"), "%s", nm);
        cg.color = CC_7TH;
        cg_name(0, nm);
        check("... its 7th: Am7", !strcmp(nm, "Am7"), "%s", nm);
        for (c = 0; c < sizeof CT / sizeof CT[0]; c++) {
            cg.color = CT[c].color;
            for (i = 0; i < cg.num; i++)
                ok &= cg_tones(cgr.prog[i], i, iv) == CT[c].tones;
        }
        check("COLOR: triads 3 tones, 7th / add9 / sus 4, 9th / lush 5", ok, 0);
        cg.color = CC_7TH;
        key_of(0, 1);                                /* C major */
        cg.var = 1;
        cg_poll();
        for (i = 0, ok = 1; i < cg.num; i++) {
            cg_name(i, nm);
            ok &= (cgr.prog[i] & 15u) == 7u ? !strcmp(nm, "G7") : 1;
            ok &= (cgr.prog[i] & 15u) == 0u ? !strcmp(nm, "Cmaj7") : 1;
        }
        check("C major: the V is G7, the I Cmaj7", ok, 0);
        key_of(9, 2);
        cg.color = CC_LUSH;
        for (v = 1; v < 60u; v++) {                 /* the voice leading: the voices move little from chord to chord */
            uint32_t move = 0;
            cg.var = (uint16_t)v;
            cg_poll();
            cgr.nlead = 0;
            for (i = 0; i < cg.num; i++) {
                k = cg_voice(i, n);
                if (cgr.nlead == k)
                    for (c = 0; c < k; c++)
                        move += n[c] > cgr.lead[c] ? n[c] - cgr.lead[c] : cgr.lead[c] - n[c];
                memcpy(cgr.lead, n, k);
                cgr.nlead = (uint8_t)k;
                worst = n[k - 1u] - n[0] > worst ? n[k - 1u] - n[0] : worst;
            }
            ok &= move <= 7u * (uint32_t)cg.num * 3u;
        }
        check("voice leading: a lush chord's five voices move a few semitones each", ok, 0);
        check("voicings stay within two octaves", worst <= 24u, "widest %u", worst);
    }
    /* ---- playing */
    {
        uint8_t n[8], want[6];
        uint32_t nn, nw, changes = 0, ok = 1;
        int last = -1;
        fresh();
        host_preset(&trk[1], ENGI_SAMPLE, 0);       /* PSX PAD */
        trk[1].p[P_ATK] = 0;
        cg.play = CP_HOLD;
        cg.rate = 1;                                 /* a chord a bar */
        cg.num = 4;
        cg.var = 3;
        cg_poll();
        transport_req = 1;
        for (i = 0; i < 8u * BAR_BLOCKS(120); i++) {
            blocks(1);
            if (cgr.now != last) {
                changes++;
                last = cgr.now;
                cgr.nlead = cgr.nn;                   /* (the voicing the generator made for it) */
                nn = sounding(&trk[1], n);
                ok &= same_set(n, nn, cgr.notes, cgr.nn) || i == 0u;
            }
        }
        check("HOLD: the pad holds each chord a bar, eight bars: eight changes", changes == 8u, "%u", changes);
        nn = sounding(&trk[1], n);
        check("HOLD: the part's voices are the chord's notes", ok && nn == cgr.nn && same_set(n, nn, cgr.notes, cgr.nn), 0);
        transport_req = 2;
        blocks(8);
        nn = sounding(&trk[1], n);
        check("HOLD: STOP lets the chord go", nn == 0u && cgr.now < 0, 0);
        /* a node held, stopped: it plays */
        cgr.live = 1u << 2;
        blocks(2);
        nn = sounding(&trk[1], n);
        nw = cgr.nn;
        memcpy(want, cgr.notes, nw);
        check("a node's key held, stopped: its chord sounds", cgr.now == 2 && nn == nw && same_set(n, nn, want, nw), 0);
        cgr.live = 0;
        blocks(2);
        check("... let go: silence", sounding(&trk[1], n) == 0u, 0);
        /* STEPS: the track's steps strike the chord of the moment */
        fresh();
        host_preset(&trk[1], ENGI_SAMPLE, 5);       /* GRAND PNO */
        cg.play = CP_STEPS;
        cg.rate = 1;
        cg_poll();
        for (i = 0; i < 16u; i += 4u) {
            uint8_t c = 60;
            put_step(&trk[1], i, 1, &c, ST_NOTE, 0);
        }
        trk[1].seq_active = 1;
        transport_req = 1;
        blocks(4);                                   /* (inside step 1's gate) */
        nn = sounding(&trk[1], n);
        check("STEPS: a step with a note strikes the chord of the moment (not its note)", nn >= 3u && !same_set(n, nn, (const uint8_t[]){60}, 1), "%u notes", nn);
        transport_req = 2;
        blocks(8);
        /* BASS FOLLOW: the bass's notes move with the chord's root */
        fresh();
        cg.play = CP_HOLD;
        cg.rate = 1;
        cg.follow = 1;
        cg.num = 4;
        for (v = 1; v < 50u; v++) {                 /* a variant whose second chord is not on the tonic */
            cg.var = (uint16_t)v;
            cg_poll();
            if ((cgr.prog[1] & 15u) != 0u)
                break;
        }
        host_preset(&trk[0], ENGI_ANALOG, 24);      /* JUNGLE SUB */
        trk[0].p[P_SLEN] = 16;
        {
            uint8_t a2 = 45;                         /* A2 on every step 1 */
            put_step(&trk[0], 0, 1, &a2, ST_NOTE, 0);
        }
        trk[0].seq_active = 1;
        transport_req = 1;
        blocks(BAR_BLOCKS(120) + 4u);               /* into bar 2, inside step 1's gate */
        nn = sounding(&trk[0], n);
        {
            int32_t r = (int32_t)(cgr.prog[1] & 15u), d = r > 6 ? r - 12 : r;
            check("BASS FOLLOW: the bass's A2 moves to bar 2's chord root, the nearest way", nn >= 1u && n[0] == (uint8_t)(45 + d),
                  "played %u, the chord's root +%d", nn ? n[0] : 0, d);
        }
        cg.follow = 0;
        transport_req = 2;
        blocks(8);
    }
    /* ---- FEEL: the harmonic rhythm */
    {
        uint32_t d[CG_MAX], cyc, ok = 1;
        fresh();
        cg.num = 4;
        cg_set_rate(1);                                  /* (a bar: a half RATE is 4 eighths) */
        cg_set_feel(CF_EVEN);
        cyc = cg_durs(d);
        check("FEEL EVEN: 4 chords a bar each (4 bars)", cyc == 32u && d[0] == 8u && d[3] == 8u && cg_node_at8(8) == 1u, 0);
        cg_set_feel(CF_ACCEL);
        cyc = cg_durs(d);
        ok = cyc == 32u && cg_node_at8(11) == 0u && cg_node_at8(12) == 1u && cg_node_at8(24) == 2u && cg_node_at8(28) == 3u &&
             cg_node_at8(32) == 0u;
        check("FEEL ACCEL: 4 chords in 4 bars, 1.5 + 1.5 + 0.5 + 0.5", ok, 0);
        cg.num = 3;
        cg_set_feel(CF_TONIC);
        cyc = cg_durs(d);
        check("FEEL TONIC: 3 chords in 4 bars (not 3), the tonic 2 + 1 + 1",
              cyc == 32u && cg_node_at8(15) == 0u && cg_node_at8(16) == 1u && cg_node_at8(24) == 2u, 0);
        for (v = 2, ok = 1; v <= CG_MAX; v++) {           /* every count: a loop of 2, 4 or 8 RATEs */
            cg.num = (uint8_t)v;
            for (i = CF_TONIC; i < CF_N; i++) {
                cg_set_feel((int32_t)i);
                cyc = cg_durs(d) / 8u;
                ok &= cyc == 2u || cyc == 4u || cyc == 8u;
            }
        }
        check("FEEL TONIC / ACCEL / PUSH: the loop 2, 4 or 8 bars for 2..8 chords", ok, 0);
        cg.num = 4;
        cg_set_feel(CF_PUSH);
        cg.play = CP_HOLD;
        cg_poll();
        transport_req = 1;
        blocks(BAR_BLOCKS(120) * 3u / 2u - BAR_BLOCKS(120) / 8u + 4u);   /* (bar 1.5 less an eighth, a little in) */
        check("FEEL PUSH: the change an eighth early (the 2nd chord before bar 1.5)", cgr.now == 1, "now %d", cgr.now);
        transport_req = 2;
        blocks(8);
    }
    /* ---- the project */
    {
        static project_t q;
        fresh();
        cg.var = 123;
        cg.num = 5;
        cg.color = CC_LUSH;
        cg.mood = 77;
        cg.play = CP_STEPS;
        cg.rate = 2;
        cg.reg = -1;
        cg.follow = 0;
        cg_poll();
        cg_pin(3, 1);
        proj_capture(&q);
        cg_defaults();
        proj_apply(&q, 1);
        check("a project keeps the generator (VARIANT 123, 5 chords, LUSH, MOOD 77, STEPS, 2BAR, REG -1, a pin)",
              cg.var == 123 && cg.num == 5 && cg.color == CC_LUSH && cg.mood == 77 && cg.play == CP_STEPS && cg.rate == 2 &&
              cg.reg == -1 && !cg.follow && cg.pins == 8u, 0);
        q.cg.var = 5000;
        q.cg.num = 40;
        q.cg.color = 99;
        q.cg.reg = 9;
        q.cg.code[0] = 0xFF;
        proj_apply(&q, 1);
        check("... a damaged one clamped", cg.var < 1000u && cg.num <= CG_MAX && cg.color < CC_N && cg.reg == 2 && (cg.code[0] & 15u) < 12u, 0);
    }
    /* ---- the demo: a session (the BREAK in JUNGLE, the bass following, a PSX pad holding the chords) */
    {
        static const int8_t LINE[32] = {45, -1, -1, -1, -1, -1, 45, 0, 45, -1, -1, -1, -1, 0, 52, -1,
                                        45, -1, -1, -1, -1, -1, 48, 0, 45, -1, -1, -1, 52, -1, 50, -1};
        char path[512];
        FILE *w;
        uint32_t frames = (uint32_t)(8.0 * 4.0 * 60.0 / 170.0 * FS), fr, pk = 0;
        fresh();
        snprintf(path, sizeof path, "%s/session-chords.wav", dir);
        if (!(w = fopen(path, "wb")))
            return 1;
        song.g[G_BPM] = 170;
        song.g[G_DUCK] = GP[G_DUCK].def;
        TBRK->p[P_REV] = 16;
        brk_style(TBRK, BS_JUNGLE);
        host_preset(&trk[0], ENGI_ANALOG, 22);      /* REESE SUB */
        trk[0].p[P_SLEN] = 32;
        for (i = 0; i < 32u; i++) {
            uint8_t nt = (uint8_t)(LINE[i] > 0 ? LINE[i] : 0);
            put_step(&trk[0], i, LINE[i] > 0 ? 1u : 0u, &nt, LINE[i] > 0 ? ST_NOTE : LINE[i] < 0 ? ST_TIE : ST_REST, 0);
        }
        trk[0].seq_active = 1;
        host_preset(&trk[1], ENGI_SAMPLE, 0);       /* PSX PAD */
        trk[1].p[P_LEVEL] = 90;
        cg.play = CP_HOLD;
        cg.rate = 2;                                 /* a chord every 2 bars */
        cg.num = 4;
        cg.color = CC_9TH;
        cg.var = 11;
        cg.follow = 1;
        cg_poll();
        brkp_poll();
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
        printf("chords: demo session-chords.wav, 8 bars at 170 BPM (the BREAK, REESE SUB following, PSX PAD), peak %u\n", pk);
        for (i = 0; i < cg.num; i++) {
            char nm[8];
            cg_name(i, nm);
            printf("chords:   %u. %s\n", i + 1u, nm);
        }
        fails += pk < 2000u || pk > 32767u;
    }
    printf(fails ? "chords: %d FAILED\n" : "chords: all ok\n", fails);
    return fails != 0;
}
