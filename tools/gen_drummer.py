#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The DRUMMER visualiser's sprites (FMX, firmware/src/ui_vis.c) to a C header.

assets/drummer/*.png: 240 x 240 line drawings on transparency (the FMX author's). A pixel is ink where its alpha is
over half. Each frame is cropped to its ink and coded with a binary range coder (LZMA's: 32 bits, 12-bit
probabilities) under a static context model: the 10 pixels already coded around it (two rows up, four to the left)
pick one of 1024 probabilities, trained here on every frame and stored with them (DRUM_P0: P(no ink), 8 bits).
~8.6 KB for the 20 frames, where 1 bpp would take 82 KB. The decoder (ui_vis.c drum_decode) is a few dozen lines.
Each frame also carries the CRC-32 of its pixels (1 bpp, rows of whole bytes, MSB first): tests/drummer_test.c
decodes every frame and checks it.

  tools/gen_drummer.py OUT.h
"""
import sys
import zlib
from pathlib import Path

from PIL import Image

SRC = Path(__file__).resolve().parents[1] / "assets" / "drummer"
# the frames in the firmware's order (ui_vis.c DF_*)
FRAMES = ["logo", "kick_off", "kick_hit", "both_off", "both_hit", "head_off", "head_hit", "alter_l", "alter_r",
          "lsnare_lh", "lsnare_rh", "rsnare_lh", "rsnare_rh", "lcym_lh", "lcym_rh", "rcym_lh", "rcym_rh",
          "nodrums_1", "nodrums_2", "nodrums_3"]
W = 240
NB = [(-1, 0), (-2, 0), (-3, 0), (-4, 0), (-2, -1), (-1, -1), (0, -1), (1, -1), (2, -1), (-1, -2)]   # MSB first
NCTX = 1 << len(NB)


def load(name):
    im = Image.open(SRC / f"{name}.png").convert("RGBA")
    a = im.getchannel("A").tobytes()
    assert im.size == (W, W), f"{name}: {im.size}, not {W} x {W}"
    ink = [1 if v > 127 else 0 for v in a]
    xs = [i % W for i, v in enumerate(ink) if v]
    ys = [i // W for i, v in enumerate(ink) if v]
    x0, y0, x1, y1 = min(xs), min(ys), max(xs) + 1, max(ys) + 1
    rows = [ink[y * W + x0:y * W + x1] for y in range(y0, y1)]
    return x0, y0, x1 - x0, y1 - y0, rows


def contexts(rows, w, h):
    """(context, bit) for every pixel, in coding order"""
    out = []
    for y in range(h):
        for x in range(w):
            c = 0
            for dx, dy in NB:
                xx, yy = x + dx, y + dy
                c = (c << 1) | (rows[yy][xx] if 0 <= xx < w and yy >= 0 else 0)
            out.append((c, rows[y][x]))
    return out


class Enc:                                          # LZMA's range encoder
    def __init__(self):
        self.low, self.range, self.cache, self.csize, self.out = 0, 0xFFFFFFFF, 0, 1, bytearray()

    def shift(self):
        if self.low < 0xFF000000 or self.low >= 1 << 32:
            carry, t = self.low >> 32, self.cache
            while True:
                self.out.append((t + carry) & 0xFF)
                t = 0xFF
                self.csize -= 1
                if not self.csize:
                    break
            self.cache = (self.low >> 24) & 0xFF
        self.csize += 1
        self.low = (self.low & 0x00FFFFFF) << 8

    def bit(self, b, p0):
        bound = (self.range >> 12) * p0
        if b:
            self.low += bound
            self.range -= bound
        else:
            self.range = bound
        while self.range < 1 << 24:
            self.range <<= 8
            self.shift()

    def done(self):
        for _ in range(5):
            self.shift()
        return bytes(self.out)


def decode(data, w, h, p0):                         # the firmware's, for the self-check
    pos, rng, code = 5, 0xFFFFFFFF, int.from_bytes(data[1:5], "big")
    rows = [[0] * w for _ in range(h)]
    for y in range(h):
        for x in range(w):
            c = 0
            for dx, dy in NB:
                xx, yy = x + dx, y + dy
                c = (c << 1) | (rows[yy][xx] if 0 <= xx < w and yy >= 0 else 0)
            bound = (rng >> 12) * p0[c]
            if code < bound:
                rng = bound
            else:
                code -= bound
                rng -= bound
                rows[y][x] = 1
            while rng < 1 << 24:
                rng = (rng << 8) & 0xFFFFFFFF
                code = ((code << 8) | (data[pos] if pos < len(data) else 0)) & 0xFFFFFFFF
                pos += 1
    return rows


def pack(rows, w):
    out = bytearray()
    for r in rows:
        for x in range(0, w, 8):
            v = 0
            for k in range(8):
                v = (v << 1) | (r[x + k] if x + k < w else 0)
            out.append(v)
    return bytes(out)


def main(out):
    frames = [(n, *load(n)) for n in FRAMES]
    n0, n1 = [0] * NCTX, [0] * NCTX                   # the model: every frame's statistics
    for _, x, y, w, h, rows in frames:
        for c, b in contexts(rows, w, h):
            (n1 if b else n0)[c] += 1
    p8 = [min(255, max(1, round(256 * (a + 0.4) / (a + b + 0.8)))) for a, b in zip(n0, n1)]   # P(0), 8 bits
    p0 = [(v << 4) | 8 for v in p8]                    # (12 bits for the coder)
    data, recs = bytearray(), []
    for name, x, y, w, h, rows in frames:
        e = Enc()
        for c, b in contexts(rows, w, h):
            e.bit(b, p0[c])
        blob = e.done()
        assert decode(blob, w, h, p0) == rows, f"{name}: the decoder disagrees"
        recs.append((name, x, y, w, h, len(data), len(blob), zlib.crc32(pack(rows, w))))
        data += blob
    assert len(data) < 65536
    size = lambda r: ((r[3] + 7) // 8) * r[4]           # (1 bpp: rows of whole bytes)
    fixed = sum(size(r) for r in recs[:3])               # the logo and the kick's two: decoded once
    pose = max(size(r) for r in recs[3:])                # a pose: decoded when it changes
    L = ["/* generated by tools/gen_drummer.py from the PNGs of assets/drummer */", "#pragma once", "#include <stdint.h>",
         f"#define DRUM_NF {len(frames)}", f"#define DRUM_NCTX {NCTX}",
         f"#define DRUM_FIXED {fixed}   /* bytes: frames 0..2 (the logo, the kick off / hit) at 1 bpp */",
         f"#define DRUM_POSE_MAX {pose}   /* bytes: the largest of the others */",
         "typedef struct { uint8_t x, y, w, h; uint16_t off, len; uint32_t crc; } drum_frame_t;",
         f"static const uint8_t DRUM_P0[{NCTX}] = {{"]
    for i in range(0, NCTX, 32):
        L.append("    " + ", ".join(map(str, p8[i:i + 32])) + ",")
    L += ["};", f"static const drum_frame_t DRUM_F[{len(frames)}] = {{"]
    for name, x, y, w, h, off, ln, crc in recs:
        L.append(f"    {{{x}, {y}, {w}, {h}, {off}, {ln}, 0x{crc:08x}u}},   /* {name} */")
    L += ["};", f"static const uint8_t DRUM_DATA[{len(data)}] = {{"]
    for i in range(0, len(data), 24):
        L.append("    " + ", ".join(f"0x{b:02x}" for b in data[i:i + 24]) + ",")
    L += ["};", ""]
    Path(out).write_text("\n".join(L))
    print(f"drummer: {len(frames)} frames, {len(data)} B + {NCTX} B model")


if __name__ == "__main__":
    main(sys.argv[1])
