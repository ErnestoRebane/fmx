#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""A folder of one-shot WAVs -> a CHOP kit for the firmware (FMX): assets/kits/<NAME>/<NAME>_kNN_<stem>.wav, one hit
per key in the order of the file names, ready for tools/gen_samples.py (KIT_SETS).

Each file: mixed to mono; the silence before it cut (from 5 ms before the 10 ms window that first comes within 30 dB
of its loudest), its tail cut where it falls 45 dB under it, at most MAX seconds; a 2 ms fade in, a 20 ms fade out;
resampled to RATE (11025 Hz: the PlayStation's voice samples were often that, and the kit fits in the flash);
peak 0.95, 16-bit.

  tools/prep_kit.py SRC_DIR NAME [RATE] [MAX]
"""
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from sampleio import read_any_wav, resample  # noqa: E402

OUT = Path(__file__).resolve().parent.parent / "assets" / "kits"


def trim(x, sr, max_s):
    w = max(1, int(0.01 * sr))
    env = [(sum(v * v for v in x[i:i + w]) / w) ** 0.5 for i in range(0, max(1, len(x) - w), w)]
    pk = max(env) or 1.0
    on = next(i for i, e in enumerate(env) if e >= pk * 10 ** (-30 / 20))
    end = max(i for i, e in enumerate(env) if e >= pk * 10 ** (-45 / 20)) + 1
    a = max(0, on * w - int(0.005 * sr))
    b = min(len(x), end * w, a + int(max_s * sr))
    return x[a:b]


def fade(x, sr):
    n = len(x)
    fi, fo = min(n // 4, int(0.002 * sr)), min(n // 4, int(0.02 * sr))
    for i in range(fi):
        x[i] *= i / fi
    for i in range(fo):
        x[n - 1 - i] *= i / fo
    return x


def write(path, x, rate):
    pk = max(abs(v) for v in x) or 1.0
    data = b"".join(struct.pack("<h", int(round(v / pk * 0.95 * 32767))) for v in x)
    hdr = b"RIFF" + struct.pack("<I", 36 + len(data)) + b"WAVEfmt " + struct.pack("<IHHIIHH", 16, 1, 1, rate, rate * 2, 2, 16)
    path.write_bytes(hdr + b"data" + struct.pack("<I", len(data)) + data)


def main(src, name, rate=11025, max_s=1.6):
    files = sorted(Path(src).glob("*.wav"), key=lambda p: p.name)
    if not 1 <= len(files) <= 16:
        sys.exit(f"prep_kit: {len(files)} WAVs in {src}: a kit has 1..16 (a hit per key)")
    out = OUT / name
    out.mkdir(parents=True, exist_ok=True)
    for old in out.glob("*.wav"):
        old.unlink()
    total = 0.0
    for k, p in enumerate(files):
        sr, x = read_any_wav(p)
        x = fade(resample(trim(x, sr, max_s), sr, rate), rate)
        write(out / f"{name}_k{k:02d}_{p.stem}.wav", x, rate)
        total += len(x) / rate
        print(f"prep_kit: {k + 1:2d}. {p.name}: {len(x) / rate:.2f} s")
    print(f"prep_kit: {name}: {len(files)} hits, {total:.2f} s at {rate} Hz, about {int(total * rate / 2 / 1024)} KB of ADPCM")


if __name__ == "__main__":
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    main(sys.argv[1], sys.argv[2], *(int(sys.argv[3]),) if len(sys.argv) > 3 else (), *(float(sys.argv[4]),) if len(sys.argv) > 4 else ())
