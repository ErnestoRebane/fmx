#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
"""Make the site (GitHub Pages): the companion app, one page for the emulator, the firmware and the sounds.

  index.html                  web/app.html with the package's metadata inlined
  *.js                        its modules: app.js, app_draw.js, app_emu.js, app_help.js, app_device.js, sounds.js and the libraries
                              they use (pool.js, breaks.js, slice.js, fm1ota.js, fm1pkg.js)
  fonts/                      Terminus (SIL OFL 1.1) and its licence
  packs/                      the sound packs (web/packs: tools/gen_packs.py), packs/melodies the MIDI tunes (melody.js)
  emu/                        the emulator: sloop.wasm from --emu DIR (web/emu/build.sh makes build/emu), worklet.js
  firmware/fmx-VER.fwsc       the package
  webapp/installer, breaks, slice, emu, editor   the old pages' addresses: each sends you to its part of the app
                              (the legacy editor, web/editor.html, is not published: it is SLOOP's)
  src/                        not touched

  web/make_site.py build/felucca-X.Y.fwsc X.Y OUT_DIR [--emu DIR]

The package identity (FM-1_9xx) is read from the package; the device reports it after the install, and the app
compares it with the one an FM-1 reports when it connects.
"""
import hashlib
import json
import re
import shutil
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
BLOCKS, BLK, KEEP = 20, 0x30, 0x2F
MODULES = ["app.js", "app_draw.js", "app_emu.js", "app_help.js", "app_device.js", "sounds.js", "melody.js", "pool.js", "breaks.js", "slice.js", "fm1ota.js", "fm1pkg.js"]
FONTS = ["terminus-16.woff", "terminus-16-bold.woff", "terminus-12.woff", "terminus-32-bold.woff"]
OLD = {"installer": "firmware", "breaks": "sounds", "slice": "sounds", "emu": "emulator", "editor": "top"}   # an old page -> its part of the app


def product_of(raw):
    """the package identity: one marker byte after each of the first 20 blocks (fm1pkg.js productOf)"""
    return "".join(chr((m - i - 1) & 0xFF) for i in range(BLOCKS) if (m := raw[i * BLK + KEEP]) != 0x7D)


def redirect(part):
    return (f'<!doctype html><meta charset="utf-8"><title>FMX</title>'
            f'<meta http-equiv="refresh" content="0; url=../../#{part}">'
            f'<a href="../../#{part}">FMX</a>\n')


def main(pkg, version, out, emu=None):
    pkg, out = Path(pkg), Path(out)
    raw = pkg.read_bytes()
    product = product_of(raw)
    if not re.fullmatch(r"FM-1_9\d\d", product):
        raise SystemExit(f"{pkg}: identity {product!r} is not a Felucca package (FM-1_9xx)")
    if b"FELUCCA-LOADER-1" not in raw:              # marker of firmware/loader
        raise SystemExit(f"{pkg}: no Felucca update loader in it")
    name = f"fmx-{re.sub(r'[^A-Za-z0-9.-]', '-', version)}.fwsc"
    meta = json.dumps({"version": version, "product": product, "pkg": "firmware/" + name,
                       "sha256": hashlib.sha256(raw).hexdigest()})   # the app checks the download against it
    html = (HERE / "app.html").read_text(encoding="utf-8")
    if html.count("/*META*/null") != 1:
        raise SystemExit("app.html must contain /*META*/null once; update make_site.py")
    html = html.replace("/*META*/null", meta)

    fw, fonts, packs, emu_out = out / "firmware", out / "fonts", out / "packs", out / "emu"
    for d in (fw, fonts, packs, emu_out):
        d.mkdir(parents=True, exist_ok=True)
    (out / "index.html").write_text(html, encoding="utf-8")
    for f in MODULES:
        shutil.copy(HERE / f, out / f)
    for f in FONTS:
        shutil.copy(HERE / "fonts" / f, fonts / f)
    lic = HERE.parent / "assets" / "fonts" / "Terminus-LICENSE.txt"
    if lic.exists():
        shutil.copy(lic, fonts / lic.name)
    for f in packs.glob("*.bin"):                    # (packs no longer shipped do not linger in an old copy)
        f.unlink()
    for f in (HERE / "packs").glob("*.bin"):
        shutil.copy(f, packs / f.name)
    if (HERE / "packs" / "melodies").is_dir():       # (INST's MIDI tunes: web/packs/melodies, its index.json)
        shutil.copytree(HERE / "packs" / "melodies", packs / "melodies", dirs_exist_ok=True)
    shutil.copy(HERE / "emu" / "worklet.js", emu_out / "worklet.js")
    wasm = Path(emu) / "sloop.wasm" if emu else None
    if wasm and wasm.exists():
        shutil.copy(wasm, emu_out / "sloop.wasm")
    else:
        print("site: no emulator (--emu DIR with web/emu/build.sh's sloop.wasm): the app's emulator won't switch on")
    for old in list(fw.glob("felucca-*.fwsc")) + list(fw.glob("sloop-*.fwsc")) + list(fw.glob("fmx-*.fwsc")):
        old.unlink()                                # one package: the current one
    shutil.copy(pkg, fw / name)
    for page, part in OLD.items():                  # the old pages: their files gone, their address kept
        d = out / "webapp" / page
        if d.exists():
            shutil.rmtree(d)
        d.mkdir(parents=True)
        (d / "index.html").write_text(redirect(part), encoding="utf-8")
    print(f"site: {out}: index.html ({len(html)} B, the app), emu/{'sloop.wasm' if wasm and wasm.exists() else '(none)'}, "
          f"firmware/{name} ({len(raw)} B, {product})")


if __name__ == "__main__":
    argv = sys.argv[1:]
    emu = None
    if "--emu" in argv:
        i = argv.index("--emu")
        emu = argv[i + 1] if i + 1 < len(argv) else sys.exit(__doc__)
        del argv[i:i + 2]
    if len(argv) != 3:
        sys.exit(__doc__)
    main(*argv, emu=emu)
