#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
"""Build the SAMPLE engine's sample sets from WAV files into a C header.

Samples are stored as IMA ADPCM (4 bit). Roots come from the file names, loops
from the WAV 'smpl' chunk; the ADPCM state at the loop start is stored so loops
restart exactly.

Sources:
  assets/samples-cc0/   CC0 samples (CREDITS.txt there): PIANO (a grand), BASS, VIBES, HORNS, STRGS, FLUTE,
                        SCRCH (tools/gen_builtin_hiphop.py makes them; FLUTE and the KIT:
                        tools/fetch_cc0.py), and the acoustic drum kit of the GM map (KIT)
  gen_waves.py          Felucca's own drum sounds (the Hügelton Sample Pack): only SLICE's BREAK, or a
                        GM kit role the CC0 kit lacks
One SAMPLE preset is written per set (SET_PRESETS: its name and sound), plus EXTRA_PRESETS.
A file named ..._m<n>.wav has its root given (MIDI note n); else it comes from the note in the name.

With FELUCCA_SLICE=1 the SLICE engine's built-in BREAK (eng_slice.c) is rendered here
too: one bar of 16ths arranged from Felucca's own generated drums, stored after every set
and not one of the SAMPLE sets. Its slice table (decoder states on a 128-point grid, the hits as AUTO
slices) is written with it: SLC_BREAK_INIT.

The header is cached in build/gen_samples.cache under a hash of every input.
"""
import hashlib
import math
import os
import re
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import sampleio as sio  # noqa: E402
from sampleio import detect_hz, hz_to_midi, ima_encode, key_split, onset, peak, read_any_wav, resample  # noqa: E402

SRC = Path(__file__).resolve().parents[1]
GENDIR = SRC / "build" / "genwav"
CACHE = SRC / "build" / "gen_samples.cache"
CC0 = SRC / "assets" / "samples-cc0"
TR = 22050                                   # stored sample rate

# set -> kind ("oneshot" decaying, "sus" looped sustain, "piano" its attack then a steady loop that the
# envelope fades, "kit" one sample per key).
# The other slots are user sets loaded from the web editor.
CC0_SETS = []
# FMX: the CC0 instruments are sound packs now (tools/gen_packs.py), not built in: the room went to the BREAK loops
PACK_SETS = [("PIANO", "piano")]               # (1.0.0: HORNS, STRGS and SCRCH are no longer packs; the files stay in assets)
# FMX: the GM kit, the upright bass, the vibes and the flute are left out (the BREAK track has the drums); its own
# pads and the organ stab are synthesised (tools/gen_pads.py into GENPAD): "loop" = the file's own loop (_L<a>-<b>)
GENPAD = Path(__file__).resolve().parent.parent / "build" / "genpads"
GEN_SETS = [("PSXPAD", "loop"), ("CHOIR", "loop"), ("GLASS", "loop"), ("ORGAN", "oneshot"),
            # FMX 2026-10-26, the pads of docs/pads.md (16 / 11.025 kHz, as the files are). They come after the pool's
            # ids in SET (SMP_LATE0..): a project's user sound keeps its SET (SMP_NSETS + id) as it was
            ("SNDTRK", "loop"), ("GLSVOX", "loop"), ("CRYSTL", "loop"), ("UNIVRS", "loop"), ("TIBET", "loop"),
            ("STRTCH", "loop"), ("TINEEP", "loop"), ("MIRAGE", "loop")]
POOL_BEFORE = 4                              # the sets before the pool in SET (SMP_NSETS); the rest after it
POOL_N = 80                                  # eng_sample.c SMP_POOL_N (checked there)
# the CHOP track's kits (a hit per key) are sound packs now, not built in: tools/gen_packs.py makes them user slot
# images (RAVEHITS from gen_pads.py, the recorded ANIME from assets/kits, tools/prep_kit.py); the CHOP KIT preset
# plays the first user slot
KIT_DIR = SRC / "assets" / "kits"
KIT_SETS = []
MEASURED_TUNING = ()                         # sets recorded off A440 (measured near the named note)

# the preset of each set: name, {SET (filled in), TUNE, BITS, LOOP (filled in), CUT, INTP (1 = PSX), DRV, -}, env, mono,
# sends (DIST, CHORUS, DELAY, REVERB), more (track parameter, value) pairs (preset_t.x)
SET_PRESETS = {
    # FMX: the PlayStation-ish pads (slow attack, long release, a lot of reverb) and the rave organ stab
    "PSXPAD": ("PSX PAD", [0, 0, 0, 0, 110, 1, 0, 0], (70, 100, 120, 92), 0, (0, 40, 16, 64), ("P_LRATE", 30, "P_LD_PIT", 1)),
    "CHOIR": ("PSX CHOIR", [0, 0, 0, 0, 118, 1, 0, 0], (64, 100, 118, 90), 0, (0, 30, 12, 70), ()),
    "GLASS": ("PSX GLASS", [0, 0, 0, 0, 127, 1, 0, 0], (24, 96, 100, 88), 0, (0, 20, 24, 66), ()),
    "ORGAN": ("RAVE ORGAN", [0, 0, 0, 0, 120, 1, 18, 0], (0, 127, 127, 30), 0, (6, 0, 18, 22), ()),
}
# more presets on a set: (set, name, E[], env, mono, sends, more)
EXTRA_PRESETS = [
    ("PSXPAD", "PSX DARK", [0, 0, 40, 0, 70, 1, 0, 0], (90, 100, 124, 100), 0, (0, 50, 10, 76), ("P_LRATE", 18, "P_LD_PIT", 1)),
]
# FMX 2026-10-26: the pads of docs/pads.md, each one a sound of the scene's; after the pack presets (the presets before
# keep their numbers: TRK_DEF, projects). (set, name, E[], env, mono, sends, more)
LATE_PRESETS = [
    ("SNDTRK", "SOUNDTRK", [0, 0, 0, 0, 118, 1, 0, 0], (24, 100, 120, 84), 0, (0, 36, 14, 62), ("P_LRATE", 26, "P_LD_FLT", 6)),
    ("GLSVOX", "GLASS VOX", [0, 0, 0, 0, 124, 1, 0, 0], (44, 100, 118, 90), 0, (0, 44, 20, 72), ("P_LRATE", 20, "P_LD_PIT", 1)),
    ("CRYSTL", "CRYSTAL", [0, 0, 0, 0, 127, 1, 0, 0], (0, 100, 112, 86), 0, (0, 26, 26, 66), ()),
    ("UNIVRS", "UNIVERSE", [0, 0, 0, 0, 120, 1, 0, 0], (30, 100, 120, 82), 0, (0, 56, 18, 60), ()),
    ("TIBET", "TIBET", [0, 0, 0, 0, 127, 1, 0, 0], (4, 100, 118, 56), 0, (0, 10, 30, 58),
     ("P_LD_PIT", 1, "P_LRATE", 78, "P_LFADE", 70)),
    ("STRTCH", "STRETCH", [0, 0, 0, 0, 124, 1, 0, 0], (10, 100, 124, 66), 0, (0, 0, 22, 44), ()),
    ("CHOIR", "DARKSIDE", [0, -12, 30, 0, 96, 1, 20, 0], (84, 100, 122, 104), 0, (8, 20, 12, 84), ("P_LRATE", 8, "P_LD_PIT", 2)),
    ("MIRAGE", "DETROIT", [0, 0, 80, 0, 116, 0, 0, 0], (0, 100, 104, 44), 0, (0, 14, 16, 34), ("P_ED_FLT", 20)),
    ("TINEEP", "JAZZ EP", [0, 0, 0, 0, 127, 1, 0, 0], (0, 112, 36, 54), 0, (0, 22, 12, 34), ("P_LRATE", 70, "P_LD_AMP", 40)),
]
# presets on a sound pack (a user slot, found by the pack's name in its header; eng_sample.c smp_pack_slot): (pack, its kind,
# name, E[], env, mono, sends, more). "*KIT": the first slot that holds a kit, the CHOP track's (a hit per key: white key
# n plays hit n; one-shots, REL long, PSX)
PACK_PRESETS = [
    ("PIANO", "piano", "GRAND PNO", [0, 0, 0, 0, 127, 0, 0, 0], (0, 110, 0, 58), 0, (0, 6, 8, 24), ()),
    ("PIANO", "piano", "DUSTY PNO", [0, 0, 34, 0, 96, 0, 24, 0], (0, 98, 0, 52), 0, (0, 8, 10, 22), ()),
    ("PIANO", "piano", "LOFI KEYS", [0, 0, 45, 0, 78, 0, 30, 0], (0, 108, 0, 60), 0, (0, 30, 18, 34), ("P_LD_PIT", 1, "P_LRATE", 38)),
    ("*KIT", "kit", "CHOP KIT", [0, 0, 0, 0, 127, 1, 0, 0], (0, 127, 127, 120), 0, (0, 0, 10, 20), ()),
]

KIT_BASE = 53                     # F3, the lowest FM-1 key

NOTE = {"C": 0, "C#": 1, "D": 2, "D#": 3, "E": 4, "F": 5, "F#": 6, "G": 7, "G#": 8, "A": 9, "A#": 10, "B": 11}

# GM drum map (General MIDI percussion, channel 10): role -> [(lo, hi, root)].
# One sample can serve several GM notes; a range is pitched around its root.
GM_KIT = {
    "kick": [(35, 36, 36)], "rim": [(37, 37, 37)], "snare": [(38, 38, 38), (40, 40, 40)],
    "clap": [(39, 39, 39)], "chh": [(42, 42, 42), (44, 44, 44)], "ohh": [(46, 46, 46)],
    # (the toms skip the hi-hats' 42, 44, 46: a zone over them would win, the hats would play a tom)
    "tomlo": [(41, 41, 43), (43, 43, 43), (45, 45, 43)], "tomhi": [(47, 48, 48), (50, 50, 48)],
    "tom": [(41, 41, 47), (43, 43, 47), (45, 45, 47), (47, 48, 47), (50, 50, 47)],
    "crash": [(49, 49, 49), (52, 52, 52), (55, 55, 55), (57, 57, 57)],
    "ride": [(51, 51, 51), (53, 53, 53), (59, 59, 59)],
    "tamb": [(54, 54, 54)], "cowbell": [(56, 56, 56)], "conga": [(62, 64, 63)],
    "shaker": [(69, 70, 70), (82, 82, 82)], "claves": [(75, 75, 75)], "wood": [(76, 77, 76)],
}
GM_ROLE_WORDS = [("bassdrum", "kick"), ("kick", "kick"), ("snare", "snare"), ("hihat", "chh"), ("chat", "chh"),
                 ("ohat", "ohh"), ("clap", "clap"), ("tom lo", "tomlo"), ("tom hi", "tomhi"), ("tomlo", "tomlo"),
                 ("tomhi", "tomhi"), ("tom", "tom"),
                 ("rim", "rim"), ("cowbell", "cowbell"), ("tamb", "tamb"), ("shaker", "shaker"),
                 ("conga", "conga"), ("claves", "claves"), ("wood", "wood"), ("crash", "crash"), ("ride", "ride")]
GM_KEEP = {"crash": 0.6, "ride": 0.6, "ohh": 0.6}   # (SLOOP 2.5: the cymbals 1.0 -> 0.6 s, 12.8 KB of flash back;
                                                      # the first 0.44 s as recorded, then a fade: GM_FADE)
GM_RATE = {"chh": 32000, "ohh": 32000, "crash": 32000, "ride": 32000}   # the metal keeps its top (else TR)
# an acoustic kit from the CC0 recordings (assets/samples-cc0/KIT: VCSL): the concert bass drum and the toms
# get a shorter body (a kit's tuned-up, damped heads): their level falls as exp(-t / tau) after a hold
GM_SHAPE = {"kick": (0.03, 0.13), "tomlo": (0.04, 0.22), "tomhi": (0.04, 0.2)}
GM_FADE = {"crash": 0.16, "ride": 0.16}               # the cymbals fade out over their last 0.16 s (else 30 ms)
# the balance of the kit (dB under full scale): the kick and the snare up front, the metal and the hand
# percussion further back, as SLOOP's synthesised kits had them; the click's wood block stays
GM_GAIN = {"chh": -9, "ohh": -9, "crash": -8, "ride": -9, "shaker": -8, "tamb": -6, "conga": -4, "cowbell": -4,
           "claves": -4}

# SLICE's BREAK: (step, GM role, gain) on a bar of 16ths; the open hat is choked by the next hat
BREAK_BPM, BREAK_STEPS = 120, 16
# the BREAK part's standard set of loops (SLOOP 2.6): one bar each, in this order, in tests/fixtures/kan (KAB1 Amen break cuts,
# KAN Samples: licensed to the user, not published; only tests/brkpart_test.c uses them, when they are there).
# They do not fit in the app slot (~19 KB each, ~16 KB free): the web page "Break loops" and the emulator put them in the user slot
# BR1 (tools/upload_breaks.py --standard); SLOOP_BREAK_LOOPS=N builds the first N into the firmware too, where there is room
BRK_DIR = SRC / "assets" / "breaks"
BRK_LOOPS = [("KAB1_137_AmenBreak_Cut_02.wav", "AMEN 2"), ("KAB1_137_AmenBreak_Cut_03.wav", "AMEN 3"),
             ("KAB1_139_AmenBreak_Cut_04.wav", "AMEN 4")]
# FMX: the firmware's own loops (2026-10-20, the user's set): one or two bars each, as they are, at BRK_RATE; LOOP A..D
# start on the first four. Names: 7 characters at most (brkp_loop_name)
BRK_RATE = 16000
FMX_OWN = [("AMEN_A.wav", "AMEN A"), ("AMEN_B.wav", "AMEN B"), ("THNK.wav", "THINK"), ("FNKDR.wav", "FUNKY"),
           ("APCH.wav", "APACHE"), ("ACTN.wav", "ACTION"), ("DTD.wav", "DTD"), ("HLCP.wav", "HLCP"),
           ("HT_PNTS.wav", "HOTPNTS"), ("PLP_FC.wav", "PLP FC"), ("SCRP_A.wav", "SCRP A"), ("SCRP_B.wav", "SCRP B"),
           ("TRMN.wav", "TRMN"), ("WMN.wav", "WMN")]
FMX_LOOPS = FMX_OWN
KAN_DIR = SRC / "tests" / "fixtures" / "kan"       # (the KAN Samples cuts: local only, never published; tests/fixtures/README.md)
if os.environ.get("FMX_BREAK_SET") == "amen":       # tests/brkpart_test.c's fixture: AMEN 2, 3, 4 at 22.05 kHz, as SLOOP had
    BRK_RATE, FMX_LOOPS, BRK_DIR = TR, BRK_LOOPS, KAN_DIR   # them (tests/run_tests.sh builds it into build/gen-amen)
    CACHE = CACHE.with_suffix(".amen")
BREAK_HITS = [(0, "kick", 1.0), (0, "chh", 0.55), (2, "kick", 0.7), (2, "chh", 0.4), (4, "snare", 1.0),
              (4, "chh", 0.45), (6, "chh", 0.4), (7, "kick", 0.8), (8, "chh", 0.55), (9, "snare", 0.3),
              (10, "kick", 0.9), (10, "ohh", 0.4), (12, "snare", 1.0), (12, "chh", 0.45), (14, "chh", 0.4),
              (14, "snare", 0.35)]
SLC_GRID, SLC_AUTO = 128, 32                 # eng_slice.c slc_src_t

ENV = {"wave":(5, 80, 100, 50), "kit": (0, 127, 127, 60), "multi": (0, 85, 0, 75),
       "oneshot": (0, 127, 127, 70), "sus": (12, 80, 120, 60)}

_wavs = {}


def wav(path):
    """read_any_wav, once per file and run (the CC0 pass reads every file twice)"""
    if path not in _wavs:
        _wavs[path] = read_any_wav(path)
    return _wavs[path]


def name_note(name):
    """'_C#3' in a CC0 file name -> (octave, pitch class)"""
    m = re.search(r"_([A-G]#?)(-?\d)", name)
    return (int(m.group(2)), NOTE[m.group(1)]) if m else None


def cc0_entries(setname, kind, base=CC0):
    """-> [(file name, int16 samples at TR, loop or None, root)]"""
    files = sorted((base / setname).glob("*.wav"))
    out, conv = [], 1
    if kind == "loop":                               # (gen_pads.py: the root in cents, the loop as made, TR already)
        for p in files:
            sr, x = wav(p)
            c = re.search(r"_c(\d+)", p.name)
            lp = re.search(r"_L(\d+)-(\d+)", p.name)
            pk = peak(x)
            out.append((p.name, [int(v * 30000 / pk) for v in x], (int(lp.group(1)), int(lp.group(2))) if lp else None,
                        int(c.group(1)) / 100.0))
        return out
    if base in (GENPAD, KIT_DIR) and kind == "kit":  # (the CHOP kits: a hit per key from KIT_BASE, as made / prepared)
        for k, p in enumerate(files):
            sr, x = wav(p)
            n = len(x)
            fade = min(int(0.01 * TR), n // 4)
            for i in range(fade):
                x[n - fade + i] *= 1 - i / fade
            pk = peak(x)
            out.append((p.name, [int(v * 30000 / pk) for v in x], None, KIT_BASE + k))
        return out
    if base is GENPAD:                               # (gen_pads.py's one-shots: the root in cents)
        for p in files:
            sr, x = wav(p)
            n = len(x)
            fade = int(0.04 * sr)
            for i in range(fade):
                x[n - fade + i] *= 1 - i / fade
            pk = peak(x)
            out.append((p.name, [int(v * 30000 / pk) for v in x], None, int(re.search(r"_c(\d+)", p.name).group(1)) / 100.0))
        return out
    if kind != "kit" and not all(re.search(r"_m\d+\.wav$", p.name) for p in files):
        # octave convention of this set (C4 = 60 or C3 = 60): YIN votes, the names give the notes
        votes = {1: 0, 2: 0}
        for p in files:
            nn = name_note(p.name)
            if not nn:
                continue
            sr, x = wav(p)
            det = hz_to_midi(detect_hz(x[onset(x):], sr))
            for o in (1, 2):
                if abs(det - ((nn[0] + o) * 12 + nn[1])) < 1.0:
                    votes[o] += 1
        conv = 2 if votes[2] >= votes[1] else 1
    for k, p in enumerate(files):
        sr, x = wav(p)
        x = x[max(0, onset(x) - 16):]
        nn = name_note(p.name)
        given = re.search(r"_m(\d+)\.wav$", p.name)
        if given and kind != "kit":
            root = int(given.group(1))                  # (the file says it)
        elif kind == "kit":
            root = KIT_BASE + k
        elif nn:
            root = (nn[0] + conv) * 12 + nn[1]
            if setname in MEASURED_TUNING:              # tuned off A440: the measured pitch near the named note
                det = hz_to_midi(detect_hz(x, sr))
                if abs(det - root) < 1.0:
                    root = det
        else:
            root = hz_to_midi(detect_hz(x, sr))
        x = resample(x, sr, TR)
        keep = {"oneshot": 1.0, "sus": 0.95, "kit": 0.6, "piano": 1.0}[kind]
        x = x[:int(keep * TR)]
        n = len(x)
        if kind == "piano":                          # the attack, then a steady tone the envelope (DEC) fades:
            ls, le, xf, a0 = int(0.55 * TR), n - 1, int(0.08 * TR), int(0.30 * TR)   # long notes in 1 s
            w = int(0.02 * TR)
            env = [max(1e-6, (sum(v * v for v in x[max(0, i - w):i + w]) / (2 * w)) ** 0.5) for i in range(0, n, w)]
            ref = env[a0 // w]
            x = [v * (ref / env[min(len(env) - 1, i // w)] if i >= a0 else 1.0) for i, v in enumerate(x)]
            ls = best_loop_start(x, le, xf, int(0.45 * TR), int(0.65 * TR))   # (the tone in phase: no dip)
            for i in range(xf):
                a = i / xf
                x[le - xf + i] = x[le - xf + i] * (1 - a) + x[ls - xf + i] * a
            loop = (ls, le)
        elif kind == "sus":                          # crossfaded sustain loop in the steady part
            ls, le, xf = int(0.40 * TR), n - 1, int(0.06 * TR)
            for i in range(xf):
                a = i / xf
                x[le - xf + i] = x[le - xf + i] * (1 - a) + x[ls - xf + i] * a
            loop = (ls, le)
        else:
            fade = int(0.08 * TR)
            for i in range(fade):
                x[n - fade + i] *= 1 - i / fade
            loop = None
        pk = peak(x)
        out.append((p.name, [int(v * 30000 / pk) for v in x], loop, root))
    return out


def best_loop_start(x, le, xf, lo, hi):
    """the loop start in [lo, hi) whose last xf samples look most like the loop end's (correlation, on
    every 4th sample, every 2nd start: plain Python, no numpy): the crossfade then joins two copies of the
    same wave, not two phases that cancel"""
    tail = x[le - xf:le:4]
    tn = sum(v * v for v in tail) ** 0.5 + 1e-9
    best, bl = -2.0, (lo + hi) // 2
    for ls in range(lo, hi, 2):
        seg = x[ls - xf:ls:4]
        c = sum(a * b for a, b in zip(seg, tail)) / ((sum(v * v for v in seg) ** 0.5 + 1e-9) * tn)
        if c > best:
            best, bl = c, ls
    return bl


def gm_role(name):
    n = name.lower()
    return next((r for w, r in GM_ROLE_WORDS if w in n), None)


def gm_kit_sources(have_cc0):
    """role -> wav path: the CC0 acoustic kit (assets/samples-cc0/KIT); without it (and for SLICE's BREAK)
    Felucca's own synthesized drums (gen_waves.py) for the roles it lacks"""
    src = {}
    if have_cc0 and (CC0 / "KIT").exists():
        for p in sorted((CC0 / "KIT").glob("*.wav")):
            r = gm_role(p.name)
            if r and r not in src:
                src[r] = p
    for p in sorted(GENDIR.glob("D *.wav")):
        r = gm_role(p.stem)
        if r and r not in src:
            src[r] = p
    if "tomlo" in src or "tomhi" in src:
        src.pop("tom", None)
    return src


def gm_kit_entry(role, path, rate=TR):
    sr, x = wav(path)
    x = resample(x[max(0, onset(x) - 16):], sr, rate)
    x = x[:int(GM_KEEP.get(role, 0.45) * rate)]
    if role in GM_SHAPE:
        hold, tau = GM_SHAPE[role]
        h = int(hold * rate)
        x = [v * (1.0 if i < h else math.exp(-(i - h) / (tau * rate))) for i, v in enumerate(x)]
    pk = peak(x)
    end = len(x)                                    # drop the silent tail
    while end > 64 and abs(x[end - 1]) < 0.004 * pk:
        end -= 1
    x = x[:end]
    fade = min(len(x) // 4, int(GM_FADE.get(role, 0.03) * rate))
    for i in range(fade):
        x[len(x) - fade + i] *= 1 - i / fade
    g = 30000 / pk * 10 ** (GM_GAIN.get(role, 0) / 20) if path.parent.name == "KIT" else 30000 / pk
    return [int(v * g) for v in x]


def ima_states(data, positions):
    """IMA ADPCM decoder state before sample p for each p (ascending), packed as eng_slice.c reads it:
    (predictor & 0xFFFF) | index << 16 (segment 0)"""
    pred, idx, out, want = 0, 0, [], list(positions)
    for n in range(max(want) + 1 if want else 0):
        while want and want[0] == n:
            out.append((pred & 0xFFFF) | idx << 16)
            want.pop(0)
        code = (data[n >> 1] >> (4 * (n & 1))) & 15
        step = sio.IMA_STEP[idx]
        vd = step >> 3
        if code & 4:
            vd += step
        if code & 2:
            vd += step >> 1
        if code & 1:
            vd += step >> 2
        pred = max(-32768, min(32767, pred - vd if code & 8 else pred + vd))
        idx = max(0, min(88, idx + sio.IMA_IDX[code & 7]))
    return out


def break_loop():
    """SLICE's BREAK: BREAK_HITS from Felucca's generated drums at TR -> (int16 samples, hit positions)"""
    src = gm_kit_sources(False)                     # generated sounds only: the same on every build
    n = int(round(TR * 60 / BREAK_BPM * 4))
    x = [0.0] * n
    pos = [s * n // BREAK_STEPS for s in range(BREAK_STEPS + 1)]
    for step, role, gain in BREAK_HITS:
        smp = gm_kit_entry(role, src[role])
        if role == "ohh":                           # choked by the next hat
            nxt = min([s for s, r, _ in BREAK_HITS if r in ("chh", "ohh") and s > step] or [BREAK_STEPS])
            keep, fade = pos[nxt] - pos[step], int(0.004 * TR)
            smp = [v * min(1.0, (keep - i) / fade) for i, v in enumerate(smp[:keep])]
        for i, v in enumerate(smp):                 # the tails wrap round: a seamless loop
            x[(pos[step] + i) % n] += v * gain
    pk = peak(x)
    return [int(v * 30000 / pk) for v in x], sorted({pos[s] for s, _, _ in BREAK_HITS})


class Builder:
    def __init__(self):
        self.zones, self.sets, self.blob, self.kinds = [], [], bytearray(), {}
        self.brk = None
        self.loops = []                              # the BREAK part's built-in loops: (offset, samples, name)

    def break_loops(self):
        """the BREAK part's built-in loops (FMX_LOOPS), after every set: each file as it is (a bar or two), mono, BRK_RATE,
        peak 0.95, IMA ADPCM"""
        found = [(BRK_DIR / f, name) for f, name in FMX_LOOPS if (BRK_DIR / f).exists()]
        found = found[:break_loops_count()]            # SLOOP_BREAK_LOOPS=N: the first N; default all
        for path, name in found:
            sr, x = sio.read_any_wav(path)
            y = sio.resample(x, sr, BRK_RATE)
            pk = max(1e-9, max(abs(v) for v in y))
            smp = sio.to_int16([v * (0.95 / pk) for v in y])
            off, _ = self.add(smp, 0)
            self.loops.append((off, len(smp), name, int(round(BRK_RATE / 44100 * 65536))))

    def slice_break(self):
        """SLICE's BREAK, after every set; its slice table: decoder states at k * len / SLC_GRID, the hits"""
        x, hits = break_loop()
        off, _ = self.add(x, 0)
        n = len(x)
        data = self.blob[off:off + (n + 1) // 2]
        grid = ima_states(data, [k * n // SLC_GRID for k in range(SLC_GRID)])
        hits = hits[:SLC_AUTO]
        self.brk = dict(off=off, n=n, grid=grid, apos=hits, ast=ima_states(data, hits))

    def add(self, s, loop_start):
        """ADPCM-encode s into the blob (each sample starts on an even offset) -> (offset, state at loop_start)"""
        enc, st = ima_encode(s, loop_start)
        off = len(self.blob)
        self.blob += enc
        if len(self.blob) & 1:
            self.blob.append(0)
        return off, st

    def add_set(self, name, kind, entries):
        """entries: zone dicts with root16 (and key for a kit); assigns the key ranges"""
        if not entries:
            return
        if kind == "kit":
            for e in entries:
                e["lo"] = e["hi"] = e["key"]
        else:
            entries.sort(key=lambda e: e["root16"])
            for e, (lo, hi) in zip(entries, key_split([e["root16"] // 16 for e in entries])):
                e["lo"], e["hi"] = lo, hi
        self.sets.append((name, len(self.zones), len(entries)))
        self.zones += entries
        self.kinds[name] = kind

    def gm_kit(self, have_cc0):
        """one GM-mapped kit: the CC0 acoustic kit"""
        z0 = len(self.zones)
        for role, path in gm_kit_sources(have_cc0).items():
            rate = GM_RATE.get(role, TR) if path.parent.name == "KIT" else TR
            smp = gm_kit_entry(role, path, rate)
            off, st = self.add(smp, len(smp))
            for lo, hi, root in GM_KIT[role]:
                self.zones.append(dict(off=off, n=len(smp), ls=len(smp), le=len(smp), looped=False, sr=rate,
                                       root16=root * 16, pred=st[0], idx=st[1], lo=lo, hi=hi))
        self.sets.append(("PERC", z0, len(self.zones) - z0))
        self.kinds["PERC"] = "kit"

    def cc0_set(self, name, kind, base=CC0, rate=TR):
        entries = []
        for k, (_, s, loop, root) in enumerate(cc0_entries(name, kind, base)):
            ls, le = loop if loop else (len(s), len(s))
            off, st = self.add(s, ls)
            entries.append(dict(off=off, n=len(s), ls=ls, le=le, looped=bool(loop), sr=rate,
                                root16=int(round(root * 16)), pred=st[0], idx=st[1],
                                key=KIT_BASE + k if kind == "kit" else None))
        self.add_set(name, kind, entries)

    def header(self):
        zones, sets, blob = self.zones, self.sets, self.blob
        L = ["/* generated by tools/gen_samples.py: IMA ADPCM sample sets */", "#pragma once",
             "#include <stdint.h>", ""]
        L.append(f"static const uint8_t SMP_DATA[{max(1, len(blob))}] = {{")
        for i in range(0, len(blob), 32):
            L.append("    " + ",".join(map(str, blob[i:i + 32])) + ",")
        if not blob:
            L.append("    0,")
        L.append("};")
        L.append("static const smp_zone_t SMP_ZONES[] = {")
        for e in zones:
            rate = int(round(e["sr"] / 44100 * 65536))
            L.append(f"    {{{e['off']}, {e['n']}, {e['ls']}, {e['le']}, {rate}, {e['root16']}, {e['pred']}, "
                     f"{e['idx']}, {e['lo']}, {e['hi']}, {1 if e['looped'] else 0}}},")
        if not zones:
            L.append("    {0, 0, 0, 0, 65536, 960, 0, 0, 0, 127, 0},")
        L.append("};")
        L.append("static const smp_set_t SMP_SETS[] = {")
        for name, z0, nz in sets:
            L.append(f'    {{"{name}", {z0}, {nz}}},')
        if not sets:
            L.append('    {"NONE", 0, 1},')
        L.append("};")
        nb = min(POOL_BEFORE, len(sets))
        L.append(f"#define SMP_NSETS {max(1, nb)}                /* the sets before the pool in SET */")
        L.append(f"#define SMP_NLATE {len(sets) - nb}                /* the sets after it: SET SMP_LATE0 + j = SMP_SETS[SMP_NSETS + j] */")
        L.append(f"#define SMP_LATE0 {max(1, nb) + POOL_N}")
        L.append(f"#define SMP_GEN_POOL_N {POOL_N}")
        L.append("static const preset_t SMP_PRESET_TABLE[] = {")
        named = sets or [("NONE", 0, 0)]

        def preset(i, k, pname, e, env, mono, fx, more):
            e = list(e)
            e[0], e[3] = i, 0 if k == "kit" else 1     # (i may be an expression: a user slot)
            x = ", ".join(f"{more[j]} + 1, {more[j + 1]}" for j in range(0, len(more), 2))
            return (f'    {{"{pname}", {{{", ".join(map(str, e))}}}, {{{", ".join(map(str, env))}}}, 0, {mono}, '
                    f'FX({", ".join(map(str, fx))}){", .x = {" + x + "}" if x else ""}}},')
        def set_value(name):                        # (what SET holds for the set: after the pool from POOL_BEFORE on)
            j = next(j for j, (n, _, _) in enumerate(named) if n == name)
            return j if j < nb else POOL_N + j
        for i, (name, _, _) in enumerate(named):
            k = self.kinds.get(name, "wave")
            if i >= max(1, nb):
                continue                            # (their presets: LATE_PRESETS)
            if name in SET_PRESETS:
                L.append(preset(i, k, *SET_PRESETS[name]))
            else:
                a, d, s_, r = ENV[k]
                L.append(f'    {{"{name}", {{{i}, 0, 0, {0 if k == "kit" else 1}, 127, 0, 0, 0}}, {{{a}, {d}, {s_}, {r}}}, 0, 0}},')
        for setname, *rest in EXTRA_PRESETS:
            i = next((j for j, (n, _, _) in enumerate(named) if n == setname), None)
            if i is not None:
                L.append(preset(i, self.kinds.get(setname, "wave"), *rest))
        packs = ["0"] * (len(L) - L.index("static const preset_t SMP_PRESET_TABLE[] = {") - 1)
        for pack, kind, *rest in PACK_PRESETS:
            L.append(preset("SMP_NSETS", kind, *rest))
            packs.append(f'"{pack}"')
        for setname, *rest in LATE_PRESETS:
            if any(n == setname for n, _, _ in named):
                L.append(preset(set_value(setname), self.kinds.get(setname, "wave"), *rest))
                packs.append("0")
        L.append("};")
        L.append("#define SMP_PRESET_PACK_INIT " + ", ".join(packs))
        names = ", ".join(f'"{n}"' for n, _, _ in named[:max(1, nb)])
        late = ", ".join(f'"{n}"' for n, _, _ in named[max(1, nb):])
        L.append("#define SMP_SET_NAMES_INIT " + names)
        L.append("#define SMP_LATE_NAMES_INIT " + late)
        L += self.break_header()
        L += self.loops_header()
        return "\n".join(L) + "\n"

    def loops_header(self):
        """the BREAK part's built-in loops: BRK_NBUILTIN, BRK_BUILTIN_INIT {data index, samples}, BRK_BUILTIN_NAMES_INIT"""
        if not self.loops:
            return ["", "#define BRK_NBUILTIN 0                 /* (the loops are the user slots' only) */"]
        return ["", f"/* the BREAK part's built-in loops ({', '.join(n for _, _, n, _ in self.loops)}) */",
                f"#define BRK_NBUILTIN {len(self.loops)}",
                "#define BRK_BUILTIN_INIT {" + ", ".join(f"{{{o}, {n}, {r}}}" for o, n, _, r in self.loops) + "}",
                "#define BRK_BUILTIN_NAMES_INIT " + ", ".join(f'"{n}"' for _, _, n, _ in self.loops)]

    def break_header(self):
        """SLC_BREAK_INIT: an eng_slice.c slc_src_t (len, rate, nseg, nauto, seg[], grid[], apos[], ast[])"""
        b = self.brk
        if not b:
            return ["#define SLC_BREAK_INIT {0}"]
        rate = int(round(TR / 44100 * 65536))

        def lst(v, k):
            v = list(v) + [0] * (k - len(v))
            return ", \\\n    ".join(", ".join(map(str, v[i:i + 12])) for i in range(0, len(v), 12))
        return ["", f"/* SLICE's BREAK: one bar of {BREAK_STEPS} steps at {BREAK_BPM} BPM, {len(b['apos'])} hits */",
                f"#define SLC_BREAK_BPM {BREAK_BPM}", f"#define SLC_BREAK_STEPS {BREAK_STEPS}",
                f"#define SLC_BREAK_INIT {{{b['n']}, {rate}, 1, {len(b['apos'])}, {{{{{b['off']}, 0, {b['n']}}}}}, {{ \\",
                "    " + lst(b["grid"], SLC_GRID) + "}, { \\", "    " + lst(b["apos"], SLC_AUTO) + "}, { \\",
                "    " + lst(b["ast"], SLC_AUTO) + "}}"]

    def summary(self):
        brk = f", SLICE BREAK {self.brk['n']} samples" if self.brk else ""
        if self.loops:
            brk += f", {len(self.loops)} BREAK loops ({sum(n for _, n, _, _ in self.loops)} samples)"
        return f"samples: {len(self.sets)} sets, {len(self.zones)} zones, {len(self.blob)} B ADPCM{brk}"


def input_key(have_cc0):
    """hash of everything the header depends on"""
    h = hashlib.sha256()
    here = Path(__file__).resolve().parent
    for p in (here / "gen_samples.py", here / "sampleio.py", here / "gen_pads.py"):
        h.update(p.read_bytes())
    h.update(repr((sys.version_info[:2], have_cc0, slice_break_wanted(), break_loops_wanted(), break_loops_count())).encode())   # sum() differs across versions
    for f, _ in FMX_LOOPS:
        if (BRK_DIR / f).exists():
            h.update(f.encode() + b"\0" + hashlib.sha256((BRK_DIR / f).read_bytes()).digest())
    files = sorted(GENDIR.glob("*.wav")) + sorted(KIT_DIR.glob("*/*.wav"))
    if have_cc0:
        files += sorted(CC0.glob("*/*.wav"))
    for p in files:
        h.update(str(p.relative_to(SRC)).encode() + b"\0")
        h.update(hashlib.sha256(p.read_bytes()).digest())
    return h.hexdigest()


def slice_break_wanted():
    """SLICE's built-in loop (eng_slice.c SLC_BREAK_INIT, the engine's source 0): only when that engine is built"""
    return os.environ.get("FELUCCA_SLICE") == "1"


def break_loops_wanted():
    """the BREAK part's built-in loops (brkpart.c): built by default; FELUCCA_BREAK=0 leaves them out (~58 KB of flash)"""
    return os.environ.get("FELUCCA_BREAK", "1") != "0"


def break_loops_count():
    """SLOOP_BREAK_LOOPS: how many of the standard loops are built into the firmware (0..3, default 3, ~19 KB each:
    FMX has the room SLOOP 2.6 had not; 0 leaves them to the user slots, as SLOOP did)"""
    try:
        return max(0, min(len(FMX_LOOPS), int(os.environ.get("SLOOP_BREAK_LOOPS", str(len(FMX_LOOPS))))))
    except ValueError:
        return 0


def main(out):
    subprocess.run([sys.executable, str(Path(__file__).with_name("gen_waves.py")), str(GENDIR)], check=True)
    subprocess.run([sys.executable, str(Path(__file__).with_name("gen_pads.py")), str(GENPAD)], check=True)
    have_cc0 = CC0.exists() and any(CC0.glob("*/*.wav"))
    if not have_cc0:
        print("samples: no CC0 samples in assets/samples-cc0 - generated material only")
    key = input_key(have_cc0)
    try:
        ck, summary, text = CACHE.read_text().split("\n", 2)
        if ck == key:
            Path(out).write_text(text)
            print(summary + " (cached)")
            return
    except (OSError, ValueError):
        pass
    b = Builder()
    if have_cc0:
        for name, kind in CC0_SETS:
            if kind != "kit":                       # the CC0 KIT feeds the GM kit
                b.cc0_set(name, kind)
    for name, kind in GEN_SETS:                      # FMX's own (tools/gen_pads.py), at the rate of their files
        b.cc0_set(name, kind, GENPAD, wav(sorted((GENPAD / name).glob("*.wav"))[0])[0])
    for name, rate in KIT_SETS:                      # the recorded kits (tools/prep_kit.py)
        b.cc0_set(name, "kit", KIT_DIR, rate)
    if slice_break_wanted():                         # SLICE's BREAK loop: only for that engine
        b.slice_break()                             # (after the sets: their offsets stay as they were)
    if break_loops_wanted():                         # the BREAK part's loops
        b.break_loops()
    text = b.header()
    Path(out).write_text(text)
    CACHE.parent.mkdir(parents=True, exist_ok=True)
    CACHE.write_text(f"{key}\n{b.summary()}\n{text}")
    print(b.summary())


if __name__ == "__main__":
    main(sys.argv[1])
