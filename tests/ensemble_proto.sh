#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# The chorus bus as an ensemble (fx.c FX_ENSEMBLE), a listening test before it goes in: every sound that sends to the
# chorus, alone, rendered with the two-tap chorus (old) and the ensemble (new), and measured: peak, RMS, the two sides'
# correlation (1 = mono, lower = wider). The pads play the song's chords on CHORDS (tests/pads_render.c); the leads
# play INST's melody on CHOP (song_audio_test leads, SOLO=1).
#   sh tests/ensemble_proto.sh [OUTDIR]      (default build/ensemble/listen)
set -eu
cd "$(dirname "$0")/.."
OUT=${1:-build/ensemble/listen}
rm -rf "$OUT"
mkdir -p build/host "$OUT/old" "$OUT/new"
sed 's/^int main(int argc, char \*\*argv)/int song_audio_main(int argc, char **argv)/' tests/song_audio_test.c > build/host/sat_lib.c
for v in 0 1; do
    d=old; [ $v = 1 ] && d=new
    cc -O2 -w -DFX_ENSEMBLE=$v -Ibuild/gen -Ifirmware/src -Ifirmware/hal -o build/host/song_audio_ens$v tests/song_audio_test.c -lm
    cc -O2 -w -DFX_ENSEMBLE=$v -Ibuild/host -Itests -Ibuild/gen -Ifirmware/src -Ifirmware/hal -o build/host/pads_ens$v \
        tests/pads_render.c -lm
    build/host/pads_ens$v "$OUT/$d" "WARM PAD" "ATMOS PAD" "DARK STR" "SINE PAD" "BLUE PAD" "PSX PAD" "PSX CHOIR" \
        "PSX GLASS" "PSX DARK" "SOUNDTRK" "GLASS VOX" "CRYSTAL" "UNIVERSE" "TIBET" "DARKSIDE" "DETROIT" "GRAND PNO" \
        "DUSTY PNO" "LOFI KEYS" "JAZZ EP" "GRAND PNO" > /dev/null
    mkdir -p "$OUT/$d/leads"
    SOLO=1 build/host/song_audio_ens$v leads "$OUT/$d/leads" "JUNO LEAD" "HOOVER LD" "HOOVER" "REESE" > /dev/null
    for f in "$OUT/$d"/leads/*.wav; do
        b=$(basename "$f")
        mv "$f" "$OUT/$d/lead-$b"
    done
    rmdir "$OUT/$d/leads"
done
python3 - "$OUT" <<'EOF'
import array, math, os, sys, wave
out = sys.argv[1]
def stats(p):
    w = wave.open(p); x = array.array('h', w.readframes(w.getnframes()))
    l, r = x[0::2], x[1::2]
    sl = sum(a * a for a in l); sr = sum(b * b for b in r); slr = sum(a * b for a, b in zip(l, r))
    rms = math.sqrt((sl + sr) / len(x))
    return max(abs(s) for s in x), 20 * math.log10(rms / 32768), slr / math.sqrt(sl * sr)
print("%-22s %22s   %22s" % ("", "old: peak  RMS   corr", "new: peak  RMS   corr"))
for f in sorted(os.listdir(out + "/old")):
    o, n = stats(out + "/old/" + f), stats(out + "/new/" + f)
    print("%-22s %8d %6.2f %5.2f   %8d %6.2f %5.2f   (RMS %+.2f dB)" % (f[:-4], *o, *n, n[1] - o[1]))
EOF
