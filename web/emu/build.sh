#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
# FMX in the browser: the firmware's sources as the host tests build them (web/emu/sloop_web.c), compiled to
# WebAssembly with Emscripten, and the worklet that runs it; the companion app (web/app.html) is its page. After
# Felucca's web/emu/build.sh and X0X's (charlesvestal/fm1-x0x, GPL-3.0).
#   web/emu/build.sh   ->  build/emu/{sloop.wasm, worklet.js}
# Needs build/gen (the tables, the fonts, the samples, the logo: ./build.sh makes them; or, without the JieLi toolchain,
# `python3 tools/gen_*.py build/gen/...` as tools/build.py generate() does) and emcc (Emscripten).
# -ffp-contract=off: no fused multiply-adds, as the host build. EMU_OPT replaces -O2, EMU_CFLAGS adds flags
# (EMU_CFLAGS=-msimd128: let the compiler vectorise; every browser with AudioWorklet has wasm SIMD).
# The splash shows the version (FELUCCA_VERSION of ui.c, or EMU_VERSION).
set -e
cd "$(dirname "$0")/../.."
for h in felucca_tables felucca_samples felucca_font felucca_drummer; do
    [ -f build/gen/$h.h ] || { echo "emu: build/gen/$h.h is missing (./build.sh, or tools/gen_*.py: see tools/build.py generate())"; exit 1; }
done
command -v emcc >/dev/null 2>&1 || { echo "emu: no emcc (Emscripten)"; exit 1; }
VERSION=${EMU_VERSION:-$(sed -n 's/^#define FELUCCA_VERSION "\([^"]*\)".*/\1/p' firmware/src/ui.c)}   # EMU_VERSION: another label
mkdir -p build/emu
# shellcheck disable=SC2086
emcc ${EMU_OPT:--O2} -ffp-contract=off -std=gnu99 -w ${EMU_CFLAGS:-} \
    "-DFELUCCA_VERSION=\"$VERSION\"" -Ibuild/gen -Ifirmware/src -Ifirmware/hal \
    --no-entry -sSTANDALONE_WASM -sSTACK_SIZE=1048576 -sINITIAL_MEMORY=33554432 -sFILESYSTEM=0 \
    -o build/emu/sloop.wasm web/emu/sloop_web.c
cp web/emu/worklet.js build/emu/                      # the app (web/app.html, web/make_site.py --emu build/emu) runs them
echo "emu: build/emu ($(wc -c < build/emu/sloop.wasm | tr -d ' ') B wasm, $VERSION)"
