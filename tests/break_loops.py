#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""A slot image of break loops for tests/brkpart_test.c: three loops as the web page "Break loops" (web/breaks.js) and
tools/upload_breaks.py make them, each its own zone (roots 36 + i), cut from tests/slice_loop.py's WAV (two bars, 5 s,
96 BPM): its first bar (2.5 s: one bar at 96 BPM), its first quarter (1.25 s: no count of bars puts that within 80..180)
and its first three eighths (1.875 s: one bar at 128 BPM).
  tests/break_loops.py SLICE_LOOP_PREFIX OUT        reads SLICE_LOOP_PREFIX.wav, writes OUT.hdr / OUT.bin"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import sampleio as sio  # noqa: E402

ROOT_BASE = 36


def main(src, out):
    sr, x = sio.read_any_wav(Path(src + ".wav"))
    parts = [x[: len(x) // 2], x[: len(x) // 4], x[: len(x) * 3 // 8]]
    loops = []
    for p in parts:
        y = sio.resample(p, sr, sio.SLOT_RATE)
        pk = max(1e-9, max(abs(v) for v in y))
        loops.append(sio.to_int16([v * (0.95 / pk) for v in y]))
    hdr, data = sio.user_slot("BR2", [(s, ROOT_BASE + i, None, None) for i, s in enumerate(loops)])
    Path(out + ".hdr").write_bytes(hdr)
    Path(out + ".bin").write_bytes(data)
    print(f"break_loops: {len(loops)} loops, {[len(s) for s in loops]} samples, {len(data)} B")


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
