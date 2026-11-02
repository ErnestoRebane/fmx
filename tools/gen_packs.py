#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""FMX sound packs: the sounds that used to be built in, as user slot images (header + ADPCM data, as in flash and as
the editor's backup stores a slot). A preset on a pack finds it by this name.
  PIANO     the CC0 grand in assets/samples-cc0 (GRAND / DUSTY PNO, LOFI KEYS): the one pack the companion ships,
            web/packs/PIANO.bin
  RAVEHITS  the rave stabs and hits tools/gen_pads.py synthesises (22050 Hz): a CHOP kit
  ANIME     the recorded kit in assets/kits/ANIME (tools/prep_kit.py, 11025 Hz; CC BY 4.0, assets/kits/CREDITS.txt)
            (RAVEHITS and ANIME: the host tests' kits only, in tests/fixtures/packs; not shipped since 1.0.0)

A kit is a hit per key from F3 (the lowest FM-1 key) up, as the CHOP track plays it. Run: tools/gen_packs.py [outdir]"""
import struct
import subprocess
import sys
import zlib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import gen_samples as gs  # noqa: E402
from sampleio import SLOT_DATA_OFF, SLOT_HDR_LEN, SLOT_MAX_DATA, SLOT_ZONES  # noqa: E402

SRC = Path(__file__).resolve().parents[1]
# (name, kind, its WAVs, rate): the CHOP kits, then the CC0 instruments that were built in (gen_samples.PACK_SETS)
PACKS = [("RAVEHITS", "kit", gs.GENPAD, 22050), ("ANIME", "kit", gs.KIT_DIR, 11025)] + \
        [(name, kind, gs.CC0, gs.TR) for name, kind in gs.PACK_SETS]


SHIPPED = {"PIANO"}                          # (the rest: the tests' fixtures)


def pack_slot(name, kind, base, rate):
    """-> the slot image of one set, built as gen_samples builds a set into the firmware (its zones, keys, loops)"""
    b = gs.Builder()
    b.cc0_set(name, kind, base, rate)
    zones = []
    for e in b.zones:
        ls, le = (e["ls"], min(e["le"], e["n"] - 1)) if e["looped"] else (0, e["n"] - 1)
        pred, idx = (e["pred"], e["idx"]) if e["looped"] else (0, 0)
        zones.append(struct.pack("<5I2h4B", e["off"], e["n"], ls, le, int(round(e["sr"] / 44100 * 65536)),
                                 e["root16"], pred, idx, e["lo"], e["hi"], 1 if e["looped"] else 0))
    data = bytes(b.blob)
    if not 1 <= len(zones) <= SLOT_ZONES or len(data) > SLOT_MAX_DATA:
        raise ValueError(f"{name}: {len(zones)} zones, {len(data)} B")
    hdr = struct.pack("<IHBB8sII8x", 0x504D5346, 1, len(zones), 0, name.encode()[:8].ljust(8, b"\0"), len(data),
                      zlib.crc32(data)) + b"".join(zones)
    return hdr.ljust(SLOT_HDR_LEN, b"\0").ljust(SLOT_DATA_OFF, b"\0") + data


def main(out):
    if not any(gs.GENPAD.glob("RAVEHITS/*.wav")):
        subprocess.run([sys.executable, str(Path(__file__).with_name("gen_pads.py")), str(gs.GENPAD)], check=True)
    for name, kind, base, rate in PACKS:
        img = pack_slot(name, kind, base, rate)
        d = out if name in SHIPPED else SRC / "tests" / "fixtures" / "packs"
        d.mkdir(parents=True, exist_ok=True)
        (d / f"{name}.bin").write_bytes(img)
        print(f"pack {name}: {img[6]} zones, {(len(img) - SLOT_DATA_OFF) / 1024:.1f} KiB")


if __name__ == "__main__":
    main(Path(sys.argv[1]) if len(sys.argv) > 1 else SRC / "web" / "packs")
