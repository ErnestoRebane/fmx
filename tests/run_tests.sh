#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
# Host tests (no hardware). Run from the repo root after ./build.sh:
#   tests/run_tests.sh
#
# Regression suite (tests/regress.c, tests/target_budget.py; details at the top of regress.c):
#   golden renders  every engine x preset, the BREAK track, voice modes, FX sends, a 4-track mix: one hash
#                   each in tests/golden.txt. A change of the sound fails with the list of renders.
#   health          clipping, DC, peak level, voices free after the release, silence at the end.
#   CPU             cost / sample per preset and mix, relative to the idle + BREAK mix (tests/cpu_baseline.txt,
#                   +25 %; counted by the kernel or under callgrind, else timed at +35 %), ns printed;
#                   target: loop instructions of the render functions in build/felucca.dis
#                   (tests/target_budget.txt, +10 %; exact, static).
#   voices          the budget of 8, steal fades, MONO / LEGATO / UNISON keep their note,
#                   no hanging notes on any MIDI / key routing.
# BREAK (tests/brkpart_test.c): the BREAK track (firmware/src/brkpart.c, ui_break.c): see the top of that file.
# After an intended change of the sound: GOLDEN_UPDATE=1 sh tests/run_tests.sh, review the diff
# of tests/golden.txt, commit it with the change. After an intended change of the cost (or a new
# compiler): BUDGET_UPDATE=1 (rewrites cpu_baseline.txt and target_budget.txt). VERBOSE=1: every render.
set -e
export AC79_SDK="${AC79_SDK:-$HOME/fw-AC79_AIoT_SDK}"
cd "$(dirname "$0")/.."
OUT=build/host
mkdir -p "$OUT"
CC="${CC:-cc} -O1 -Wall -Wno-unused-function"
fail=0
run() { echo "== $1"; shift; "$@" || fail=1; }

[ -f build/felucca.fwsc ] || { echo "run ./build.sh first"; exit 1; }
# the generated tables (tools/build.py generate() makes them too; no Pillow needed)
mkdir -p build/gen
[ build/gen/felucca_tables.h -nt tools/gen_tables.py ] || python3 tools/gen_tables.py build/gen/felucca_tables.h
[ build/gen/felucca_drummer.h -nt tools/gen_drummer.py ] || python3 tools/gen_drummer.py build/gen/felucca_drummer.h   # (Pillow)

$CC -o "$OUT/storage_test" tests/storage_test.c
run "flash storage (A/B, torn writes)" "$OUT/storage_test"

$CC -o "$OUT/recovery_test" tests/recovery_test.c
run "application USB recovery and boot-loop guard" "$OUT/recovery_test"

$CC -o "$OUT/arranger_test" tests/arranger_test.c
run "song order, timing, repeats, roles, a jump" "$OUT/arranger_test"
$CC -O2 -w -Ibuild/gen -Ifirmware/src -o "$OUT/song_audio_test" tests/song_audio_test.c -lm
run "AUTO SONG: the sections (mutes, moves, the DROP 2 bass), on the bar, to the end, ENDLESS, a jump" "$OUT/song_audio_test" "$OUT/song-demo.wav"

$CC -O2 -w -Ibuild/gen -Ifirmware/src -o "$OUT/studio_break_test" tests/studio_break_test.c -lm
run "BREAK track studio: key / note maps, built-in loops, metronome, record arm, free take, hits recorded" "$OUT/studio_break_test" "$OUT/break-studio.wav"
$CC -O2 -w -Ibuild/gen -Ifirmware/src -o "$OUT/seq2_test" tests/seq2_test.c -lm
run "sequencer 2.0: no drift, ratchets (x2..x8), roll, erase / undo, ghost / hard, chords, mute / solo, nudge, locks (BREAK: CMPLX), fill, MIDI ch 10" "$OUT/seq2_test"

$CC -O2 -w -Ibuild/gen -Ifirmware/src -o "$OUT/punch_test" tests/punch_test.c -lm
run "moves (WASH, REWIND, ROLL, FILL, VERB, GAP): the mix effects bounded and dry after; FX-held keys the FX panel's" "$OUT/punch_test" "$OUT/moves.wav"
$CC -O2 -w -Ibuild/gen -Ifirmware/src -o "$OUT/q_test" tests/q_test.c -lm
run "Q: changes on the bar / the 4-bar phrase (presets, mutes, solo, CHORDS, the BASS phrase), at once at NOW or stopped" "$OUT/q_test"
$CC -O2 -w -Ibuild/gen -Ifirmware/src -o "$OUT/chopgen_test" tests/chopgen_test.c -lm
run "CHOP: the phrase (RAVE DARK TECH LIQUID VOCAL, off the snares), SLICE's bed + chops, the keys held, INST, SONG, TUNE, the project" "$OUT/chopgen_test"
$CC -O2 -w -Ibuild/gen -Ifirmware/src -Ifirmware/hal -o "$OUT/pool_test" tests/pool_test.c -lm
run "sample pool: entries anywhere in the free flash, as many as fit (ids, old slots, places, uploads, SET, loops)" "$OUT/pool_test"

$CC -O2 -w -Ibuild/gen -Ifirmware/src -Ifirmware/hal -o "$OUT/ui_pages_test" tests/ui_pages_test.c -lm
run "live UI: pages, layers (fx panel, steps, erase, roll, key, mix), holds, BREAK track and screen, REC, fuzz" "$OUT/ui_pages_test" "$OUT"
# no divide by 0 (the FM-1 runs with the div0 trap off, hal/fm1_irq.h: a real one would give a wrong value
# silently): the UI fuzz, the sequencer, the projects and a minute of random live use, with UBSan
UBSAN="${CC_UB:-cc} -O1 -w -fsanitize=integer-divide-by-zero -fno-sanitize-recover=integer-divide-by-zero -Ibuild/gen -Ifirmware/src -Ifirmware/hal"
$UBSAN -o "$OUT/ui_pages_ub" tests/ui_pages_test.c -lm && $UBSAN -o "$OUT/seq2_ub" tests/seq2_test.c -lm &&
    $UBSAN -o "$OUT/project_ub" tests/project_test.c -lm && $UBSAN -o "$OUT/soak_ub" tests/soak_test.c -lm || fail=1
mkdir -p "$OUT/ub"
run "no divide by zero (UBSan): UI fuzz, sequencer, projects, a minute of live use" \
    sh -c "'$OUT/ui_pages_ub' '$OUT/ub' >/dev/null && '$OUT/seq2_ub' >/dev/null && '$OUT/project_ub' >/dev/null && '$OUT/soak_ub' 1 >/dev/null && echo 'no divide by zero'"

$CC -O2 -w -Ibuild/gen -Ifirmware/src -o "$OUT/soak_test" tests/soak_test.c -lm
run "soak: ${SOAK_MIN:-10} minutes of random live use with the BREAK track (bounded, no hanging voices, idle after stop)" "$OUT/soak_test" "${SOAK_MIN:-10}"
$CC -O2 -w -Ibuild/gen -Ifirmware/src -Ifirmware/hal -o "$OUT/stress_test" tests/stress_test.c -lm
run "stress (2.4): FONT_L = FONT_S at 2x, hard random use of every 2.4 addition and the BREAK track, clean stop" "$OUT/stress_test" "${STRESS_FRAMES:-40000}"
# the same under AddressSanitizer + UBSan: any read or write out of bounds stops it (left shifts of negative values
# and the phases' wrap-around are left out: the DSP's two's complement idioms, as every compiler builds them)
ASAN="${CC_UB:-cc} -O1 -g -w -fsanitize=address,undefined -fno-sanitize=shift-base,signed-integer-overflow -fno-sanitize-recover=all -Ibuild/gen -Ifirmware/src -Ifirmware/hal"
if $ASAN -o "$OUT/stress_asan" tests/stress_test.c -lm 2>/dev/null; then
    run "stress under ASan + UBSan (no access out of bounds)" "$OUT/stress_asan" "${STRESS_ASAN_FRAMES:-15000}" 7
else
    echo "(stress under ASan: this compiler has no AddressSanitizer, skipped)"
fi

$CC -o "$OUT/upreset_test" tests/upreset_test.c
run "user presets (UP_PUT parser, bank round trip, versions)" "$OUT/upreset_test"

$CC -o "$OUT/midi_uart_test" tests/midi_uart_test.c
run "TRS MIDI parser" "$OUT/midi_uart_test"
$CC -Ifirmware/hal -o "$OUT/encoder_test" tests/encoder_test.c
run "knobs: one click = one step (slow, fast, pauses, bounce)" "$OUT/encoder_test"
HALF=$(sed -n 's/^#define HALF_FRAMES \([0-9]*\).*/\1/p' firmware/src/core.h)
$CC -DT_CDC=1 -DHALF_FRAMES=$HALF -o "$OUT/uac_test" tests/uac_test.c -lm
run "USB audio input: descriptors (with CDC), ring and packets" "$OUT/uac_test"
$CC -DT_CDC=0 -DHALF_FRAMES=$HALF -o "$OUT/uac_test_nocdc" tests/uac_test.c -lm
run "USB audio input: descriptors (without CDC), ring and packets" "$OUT/uac_test_nocdc"
$CC -DT_CDC=2 -DHALF_FRAMES=$HALF -o "$OUT/uac_test_seroff" tests/uac_test.c -lm
run "USB audio input: descriptors (CDC built in, menu USB SERIAL OFF), ring and packets" "$OUT/uac_test_seroff"
run "USB SERIAL OFF: the descriptors of a build without CDC, byte for byte" \
    sh -c "[ \"\$(UAC_DUMP=1 '$OUT/uac_test_seroff' | tail -n 2)\" = \"\$(UAC_DUMP=1 '$OUT/uac_test_nocdc' | tail -n 2)\" ] && echo same"
uac_in_app() { ${CC%% *} -E -Ibuild/gen -Ifirmware/hal -Ifirmware/src firmware/src/felucca.c 2>/dev/null | grep -q uac_service; }
run "USB audio input: built into the firmware (FELUCCA_UAC set before usb.c)" uac_in_app

$CC -o "$OUT/ota_test" tests/ota_test.c
run "M-UPGRADE entry" "$OUT/ota_test" build/felucca.fwsc

head -c 200000 build/felucca.bin > "$OUT/old_app.bin"
python3 tools/fm1pkg_make.py "$OUT/old_app.bin" build/loader/ota.bin "$OUT/old.fwsc" >/dev/null
$CC -o "$OUT/ldr_test" tests/ldr_test.c
run "update loader: other app -> this build" "$OUT/ldr_test" "$OUT/old.fwsc" build/felucca.fwsc

$CC -O2 -w -Ibuild/gen -Ifirmware/src -o "$OUT/hostsim" tests/hostsim.c -lm
$CC -O2 -w -Ibuild/gen -Ifirmware/src -o "$OUT/scale_test" tests/scale_test.c -lm
run "scales: white-key mapping and note lifecycle" "$OUT/scale_test"
run "DSP render (ANALOG preset 0)" "$OUT/hostsim" 0 0 1 "$OUT/render.wav"
mkdir -p build/tracks_demo
run "TRACKS: 4-track pattern with the BREAK track, live recording (lengths, swing), voice budget, engine switch, cost" env TRACKS=build/tracks_demo "$OUT/hostsim" 0 0 1 "$OUT/tracks.wav"
$CC -w -Ibuild/gen -Ifirmware/src -o "$OUT/project_test" tests/project_test.c -lm
run "project format SPB4 (and SPB3 read): capture / apply / import round trip (BREAK steps and generator, CHORDS, BASS generator), ranges clamped, SLOOP's FUN1..FUN5 refused" "$OUT/project_test"
$CC -O2 -w -Ibuild/gen -Ifirmware/src -o "$OUT/slicer_test" tests/slicer_test.c -lm
mkdir -p build/slicer_demo
run "SLICER: no clicks, timing, sync with the sequencer, STUT, cost, demos" "$OUT/slicer_test" build/slicer_demo
# BREAK (tests/brkpart_test.c): X0X's breakbeat player as track 4: the loops (AMEN 2, 3, 4 built in (the fixture, not
# FMX's own loops); the standard set in
# USR1, a user loop in USR2: tests/slice_loop.py, two bars at 96 BPM, put in a slot named BR1 by fm1_sample_upload.py build;
# three in USR3: tests/break_loops.py), the sequencer's clock, the BB Gen generator, the steps' own values, the sound, the
# live keys, loops B / C / D and the phrase, hits recorded, mute / solo, the project
python3 tests/slice_loop.py "$OUT/slice_loop" >/dev/null &&
    python3 tools/fm1_sample_upload.py build BR1 "$OUT/slice_loop" "$OUT/slice_loop.wav" >/dev/null
python3 tests/break_loops.py "$OUT/slice_loop" "$OUT/break_loops" >/dev/null
if [ -f tests/fixtures/kan/KAB1_137_AmenBreak_Cut_02.wav ] && [ -f tests/fixtures/kan/breaks_standard.bin ]; then
mkdir -p build/gen-amen &&                          # (its fixture: the firmware's headers, the samples with AMEN 2, 3, 4
    cp build/gen/*.h build/gen-amen/ &&             # as the built-in loops, as SLOOP had them)
    FMX_BREAK_SET=amen python3 tools/gen_samples.py build/gen-amen/felucca_samples.h >/dev/null
$CC -O2 -w -Ibuild/gen-amen -Ifirmware/src -Ifirmware/hal -o "$OUT/brkpart_test" tests/brkpart_test.c -lm
mkdir -p build/brkpart_demo
run "BREAK track: loops, clock, BB Gen generator, steps, sound, live keys, loops A..D, phrase, recording, project, demos" "$OUT/brkpart_test" "$OUT/slice_loop" "$OUT/break_loops" build/brkpart_demo
else                                                # (the KAN cuts are local only: tests/fixtures/README.md)
    echo "SKIP BREAK track (tests/brkpart_test.c): no tests/fixtures/kan (the AMEN 2, 3, 4 cuts are not published)"
    mkdir -p build/brkpart_demo
fi
# CHORDS (tests/chord_test.c): the generator of track 2 (firmware/src/chordgen.c): progressions, pins, voicings, HOLD,
# STEPS, live nodes, BASS FOLLOW, the project; a session demo
$CC -O2 -w -Ibuild/gen -Ifirmware/src -Ifirmware/hal -o "$OUT/chord_test" tests/chord_test.c -lm
run "CHORDS generator: progressions, pins, voicings, HOLD, STEPS, nodes, BASS FOLLOW, project, demo" "$OUT/chord_test" build/brkpart_demo
# the BASS track's phrase generator (tests/bassline_test.c, firmware/src/bassline.c): the phrase (root first, the key,
# MOVE, RHYTHM / KICKS, ANSWER, HOLD), the change as it plays (LOCK, CHANGE), the steps heard; a session demo
$CC -O2 -w -Ibuild/gen -Ifirmware/src -Ifirmware/hal -o "$OUT/bassline_test" tests/bassline_test.c -lm
run "BASS phrase generator: phrase, rhythms, answers, holds, change, playing, demo" "$OUT/bassline_test" build/brkpart_demo
# PS1 sound (tests/spu_test.c): the SPU reverb (fx.c: every preset bounded, its tail, DELAY's echo, stereo, DAMP) and
# INTP PSX (the Gaussian of eng_sample.c and brkpart.c); a demo of the presets
$CC -O2 -w -Ibuild/gen -Ifirmware/src -Ifirmware/hal -o "$OUT/spu_test" tests/spu_test.c -lm
run "PS1 sound: the SPU reverb's presets, INTP PSX on samples and the BREAK, demo" "$OUT/spu_test" build/brkpart_demo
# the browser emulator's sources built natively (web/emu/native_check.c): the BREAK screen driven through the panel
mkdir -p build/emu
$CC -O2 -ffp-contract=off -w -Ibuild/gen -Ifirmware/src -Ifirmware/hal -o build/emu/native_check web/emu/native_check.c -lm
run "emulator build (native): boot, the BREAK track's screen by hand (steps, pins, pages, PLAY, REC, a user loop), the autosaved project" build/emu/native_check build/emu
if command -v emcc >/dev/null 2>&1 && command -v node >/dev/null 2>&1; then
    EMU_VERSION="${EMU_VERSION:-SLOOP}" run "emulator build (WebAssembly): web/emu/build.sh, then the BREAK track (built-in and page loops), the saved sectors (slots, autosave), the app's library in its pool" \
        sh -c 'web/emu/build.sh >/dev/null && node web/emu/emu_test.mjs'
else
    echo "== skip the WebAssembly emulator test (no emcc or node)"
fi
$CC -O2 -w -Ibuild/gen -Ifirmware/src -o "$OUT/regress" tests/regress.c -lm
# the CPU budget: counted by the kernel on macOS; elsewhere under callgrind when valgrind is there (exact, ~45 s;
# SKIP_CPU_VALGRIND=1 to time instead, which is only a rough check)
if [ -z "$SKIP_CPU_VALGRIND" ] && command -v valgrind >/dev/null 2>&1; then export CPU_VALGRIND=1; fi
run "regression: golden renders, health, voices, CPU budget" "$OUT/regress" tests/golden.txt tests/cpu_baseline.txt

# the second core (after SLOOP 2.7.1): its host double (tests/cpu1_host.c, firmware/src/cpu1.h): inline, a thread, a
# slow thread, a lost job, under ThreadSanitizer; then the render split (fx.c mix_units) under every mode
$CC -w -Ifirmware/src -o "$OUT/cpu1_test" tests/cpu1_test.c
run "second core, host double: inline, deferred to the wait (CORES=1)" env CORES=1 "$OUT/cpu1_test"
run "second core, host double: a thread (CORES=2)" env CORES=2 "$OUT/cpu1_test"
run "second core, host double: a thread with random delays (CORES=2 CHAOS=7)" env CORES=2 CHAOS=7 "$OUT/cpu1_test"
run "second core, host double: a lost job, the hold and the fallback (CPU1_TIMEOUT_AT=777)" env CORES=2 CPU1_TIMEOUT_AT=777 "$OUT/cpu1_test"
TSAN="${CC_UB:-cc} -O1 -g -w -fsanitize=thread -Ifirmware/src"
if $TSAN -o "$OUT/cpu1_tsan" tests/cpu1_test.c 2>/dev/null; then
    run "second core, host double under ThreadSanitizer (CORES=2 CHAOS=3)" env CORES=2 CHAOS=3 "$OUT/cpu1_tsan"
else
    echo "(cpu1 under TSan: this compiler has no ThreadSanitizer, skipped)"
fi
run "the render split over the two cores: every mode of the second core's host double (tests/cpu1_modes.sh)" sh tests/cpu1_modes.sh

run "regression: target cost of the render loops" python3 tests/target_budget.py \
    build/felucca.dis tests/target_budget.txt

run "installer CLI (fm1_install.py) against a simulated FM-1" python3 tests/install_test.py

if command -v node >/dev/null 2>&1; then
    run "web pages: editor protocol, samples, packages, update protocol" node web/test_web.mjs
    run "break loops: the same slot images as tools/upload_breaks.py" node web/test_breaks.mjs
    run "slice loops: key, notes, slices, the slot and its SLC1 meta" node web/test_slice.mjs "$OUT/slice_slot.bin"
    run "sample pool, the browser's side: the directory, places, ids, uploads (web/pool.js)" node web/test_pool.mjs
    run "companion app: files told apart, entries at their rates, planned, sent, read back, the set backed up and restored" node web/test_app.mjs
    run "slice loops -> firmware: its slot image read by chopgen.c (meta, kit, TUNE, the bed)" "$OUT/chopgen_test" "$OUT/slice_slot.bin"
    run "melodies (INST MIDI): a MIDI file's line, its key, the bank, the 56-tune pack (web/melody.js)" node web/test_melody.mjs "$OUT/mel_bank.bin"
    run "melodies -> firmware: the pack's bank read by chopgen.c, every theme in the song's key" "$OUT/chopgen_test" --mel "$OUT/mel_bank.bin" 56
else
    echo "== skip web tests (no node)"
fi

[ $fail -eq 0 ] && echo "ALL HOST TESTS PASSED" || { echo "HOST TESTS FAILED"; exit 1; }
