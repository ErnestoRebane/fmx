#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Upload break loops to SLOOP's BREAK part: user slots of the FM-1, named BR1.., packed with as many loops as fit (each
loop is one zone; ~80 KiB = ~7 s of audio a slot). On the device they follow the built-in loops in LOOP A / LOOP B as BR1,
BR2, ... (the firmware numbers the loops of every slot named BR..., straight through, in order). After X0X's
tools/upload_breaks.py (charlesvestal/fm1-x0x, GPL-3.0): the slot format and the upload protocol are Felucca's.

  tools/upload_breaks.py [--bars] [--dry-run OUTDIR] [--slots 1,2,3,4] [--name BR] FILE.wav ...
  tools/upload_breaks.py --standard [...]            the standard set first: tests/fixtures/kan (AMEN 2, 3, 4), one slot
  tools/upload_breaks.py --standard-image OUT.bin    write the standard set's slot image (512-byte header area + data, as
                                                     in flash): tests/fixtures/kan/breaks_standard.bin (local only)

  --bars     cut one bar from each file (bar count guessed from its length: the one that puts its tempo nearest 120 BPM
             inside 80-180); without it, files are taken whole
  --dry-run  build the slot images (OUTDIR/slotN.hdr / .bin) and print the plan; no device
  --slots    the slots to fill (default 1,2,3,4): they are ERASED and rewritten, whatever they hold now

Files are packed in the order given, filling the first slot, then the next. A loop that does not fit in the remaining slots is
reported and skipped. Uses Felucca's encoder (tools/sampleio.py) and upload protocol (tools/fm1_sample_upload.py).
The web page web/breaks.html does the same from a browser, and keeps the loops already on the FM-1."""
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import sampleio as sio  # noqa: E402

ROOT_BASE = 36                                  # zone i gets root 36 + i: sampleio sorts zones by root
STANDARD = Path(__file__).resolve().parents[1] / "tests" / "fixtures" / "kan"   # (local only: tests/fixtures/README.md)


def standard_files():
    """the standard set, in its order (tools/gen_samples.py BRK_LOOPS): (label, path)"""
    import gen_samples as gs
    return [(name.replace(" ", ""), STANDARD / f) for f, name in gs.BRK_LOOPS]


def bars_of(seconds):
    """the count of bars (1, 2 or 4) that puts the loop's tempo nearest 170 BPM inside 80..180 (FMX: a jungle firmware,
    2.8 s is 2 bars at 170, not 1 at 85; X0X took the one nearest 120); 1 when none does"""
    best = None
    for b in (1, 2, 4):
        bpm = b * 240.0 / seconds
        if 80 <= bpm <= 180 and (best is None or abs(bpm - 170) < abs(best[1] - 170)):
            best = (b, bpm)
    return best or (1, 240.0 / seconds)


def cut(path, bars):
    sr, x = sio.read_any_wav(path)
    if bars:
        n, _ = bars_of(len(x) / sr)
        x = x[: int(round(len(x) / n))]
    y = sio.resample(x, sr, sio.SLOT_RATE)
    pk = max(1e-9, max(abs(v) for v in y))
    return sio.to_int16([v * (0.95 / pk) for v in y])


def pack(loops, nslots):
    """loops: [(label, int16 samples)] -> slots: [[(label, samples)]], skipped labels"""
    slots, cur, used, skipped = [], [], 0, []
    for label, s in loops:
        need = (len(s) + 1) // 2
        if need > sio.SLOT_MAX_DATA:
            skipped.append(label)
            continue
        if used + need > sio.SLOT_MAX_DATA or len(cur) >= sio.SLOT_ZONES:
            if cur:
                slots.append(cur)
            cur, used = [], 0
        if len(slots) >= nslots:                # every slot is full: the rest do not fit
            skipped.append(label)
            continue
        cur.append((label, s))
        used += need
    if cur:
        slots.append(cur)
    return slots, skipped


def main():
    a = sys.argv[1:]
    if not a or a[0] in ("-h", "--help"):
        sys.exit(__doc__)
    bars = dry = None
    slot_ids, name, files = [1, 2, 3, 4], "BR", []
    while a:
        o = a.pop(0)
        if o == "--bars":
            bars = True
        elif o == "--dry-run":
            dry = Path(a.pop(0))
        elif o == "--slots":
            slot_ids = [int(v) for v in a.pop(0).split(",")]
        elif o == "--name":
            name = a.pop(0)
        elif o == "--standard":
            files = standard_files() + files
        elif o == "--standard-image":
            out = Path(a.pop(0))
            loops = [cut(pth, True) for _, pth in standard_files()]
            hdr, data = sio.user_slot("BR1", [(s, ROOT_BASE + z, None, None) for z, s in enumerate(loops)])
            out.write_bytes(bytes(hdr) + b"\0" * (sio.SLOT_DATA_OFF - len(hdr)) + bytes(data))
            print(f"standard set: {len(loops)} loops, {len(data)} B of data -> {out}")
            return
        else:
            files.append((Path(o).stem.upper()[:6], Path(o)))
    loops = [(lbl, cut(p, bars)) for lbl, p in files]
    slots, skipped = pack(loops, len(slot_ids))
    snames = [f"{name[:2]}{slot_ids[i]}" for i in range(len(slots))]   # the device needs "BR" first
    for i, sl in enumerate(slots):
        names = ", ".join(f"{snames[i]}.{z + 1}={lbl}" if len(sl) > 1 else f"{snames[i]}={lbl}" for z, (lbl, _) in enumerate(sl))
        kib = sum((len(s) + 1) // 2 for _, s in sl) / 1024
        print(f"USR{slot_ids[i]}: {len(sl)} loop(s), {kib:.1f} KiB: {names}")
    if skipped:
        print("did not fit (skipped):", ", ".join(skipped))
    images = [sio.user_slot(snames[i], [(s, ROOT_BASE + z, None, None) for z, (_, s) in enumerate(sl)])
              for i, sl in enumerate(slots)]
    if dry:
        dry.mkdir(parents=True, exist_ok=True)
        for i, (hdr, data) in enumerate(images):
            (dry / f"slot{slot_ids[i]}.hdr").write_bytes(hdr)
            (dry / f"slot{slot_ids[i]}.bin").write_bytes(data)
        print(f"dry run: images in {dry}")
        return
    import fm1_sample_upload as up
    link = up.Link()
    for i, (hdr, data) in enumerate(images):
        slot = slot_ids[i] - 1
        if link.req(11, [slot])[1]:
            sys.exit(f"USR{slot + 1}: begin failed")
        t0 = time.time()
        for off in range(0, len(data), 256):
            adr = sio.SLOT_DATA_OFF + off
            for attempt in range(3):                # a SysEx frame is now and then lost on the USB link;
                try:                                # writing the same bytes again is harmless
                    r = link.req(12, [slot, adr & 0x7F, (adr >> 7) & 0x7F, (adr >> 14) & 0x7F] + up.pack7(data[off:off + 256]))
                    break
                except TimeoutError:
                    if attempt == 2:
                        raise
            if r[4]:
                sys.exit(f"USR{slot + 1}: write at {adr:#x} failed ({r[4]})")
            print(f"\r  USR{slot + 1}: {min(off + 256, len(data))} / {len(data)}", end="", flush=True)
        r = link.req(13, [slot] + up.pack7(hdr), 5)
        print(f"\n  {time.time() - t0:.1f} s,", "ok" if r[1] == 0 else f"FAILED ({r[1]}, {up.RC.get(r[1], '?')})")


if __name__ == "__main__":
    main()
