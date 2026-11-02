/* SPDX-License-Identifier: GPL-3.0-only */
/* A pad as the CHORDS track plays it: the song's session (song_audio_test.c: F minor, 170, the chord generator
 * holding its chords), the preset NAME on CHORDS, CHORDS alone, its sends as the preset has them; 16 bars.
 *   pads_render OUTDIR NAME..      (writes OUTDIR/NN-name.wav)
 * Built by tests/ensemble_proto.sh: build/host/sat_lib.c is song_audio_test.c's text, main renamed; -Itests finds what
 * it includes by relative path. */
#include "sat_lib.c"                            /* song_audio_test.c with its main renamed (tests/ensemble_proto.sh makes it) */

int main(int argc, char **argv)
{
    int a;
    if (argc < 3) {
        printf("pads_render OUTDIR NAME..\n");
        return 2;
    }
    for (a = 2; a < argc; a++) {
        char pth[512], nm[32];
        FILE *f;
        uint32_t n = 16u * 4u * 60u * FS / 170u, t, pi, e = ENGI_ANALOG, c, i;
        for (pi = 0; pi < ENG_ANALOG.npresets && !str_eq(ENG_ANALOG.presets[pi].name, argv[a]); pi++)
            ;
        if (pi == ENG_ANALOG.npresets) {
            e = ENGI_SAMPLE;
            for (pi = 0; pi < ENG_SAMPLE.npresets && !str_eq(ENG_SAMPLE.presets[pi].name, argv[a]); pi++)
                ;
            if (pi == ENG_SAMPLE.npresets) {
                printf("pads: no preset %s\n", argv[a]);
                return 1;
            }
        }
        session(0);
        host_preset(&trk[1], e, pi);
        trk[1].p[P_ROOT] = 5;                           /* (the session's F minor, after the preset) */
        trk[1].p[P_SCALE] = 2;
        cg_poll();
        for (i = 0; i < NTRK; i++)
            trk[i].p[P_MUTE] = (int16_t)(i != 1u);
        for (c = 0; argv[a][c] && c < 31u; c++)
            nm[c] = argv[a][c] == ' ' ? '-' : (char)(argv[a][c] | 0x20);
        nm[c] = 0;
        snprintf(pth, sizeof pth, "%s/%02d-%s.wav", argv[1], a - 1, nm);
        f = fopen(pth, "wb");
        if (!f)
            return 1;
        wav_hdr(f, (n / CTL) * CTL);
        transport_req = 1;
        for (t = 0; t + CTL <= n; t += CTL)
            block(f);
        fclose(f);
        transport_req = 2;
        block(0);
        block(0);
        printf("pads: %s\n", pth);
    }
    return 0;
}
