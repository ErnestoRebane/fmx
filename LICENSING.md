# FMX licensing

FMX is built on [SLOOP](https://github.com/isod89/sloop-fm1) by isod89, which is built on Felucca by Leo
Kuroshita, Hügelton Instruments; the terms below are Felucca's, and FMX keeps them. FMX is free software. Its **code** is licensed under the GNU General
Public License, version 3 only (`GPL-3.0-only`, full text in `LICENSE`). Its **assets** are not part of
that licence: the drum sounds made by `tools/gen_waves.py` (the Hügelton Sample Pack) are
Copyright (C) 2026 Hügelton Instruments,
all rights reserved. Their licence terms will be published later. FMX's firmware does not contain the
Hügelton Sample Pack: its sampled drum kit is made of CC0 recordings (`assets/samples-cc0/KIT`), and
`gen_waves.py` only feeds the SLICE engine's demo loop, which FMX does not build.

Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments

## What is code (GPL-3.0-only)

Every file in this tree that carries an `SPDX-License-Identifier: GPL-3.0-only` header:

- the firmware: `firmware/` (app, HAL, update loader)
- the build script and tools: `build.sh`, `tools/`
- the web pages (the companion app, the editor) and their tests: `web/` (not the Fukiai font, below)
- the host tests: `tests/`

You may use, study, change and share it under the GPL. If you distribute FMX, or
firmware derived from it, you must also give your recipients its complete corresponding
source under the same licence. That includes devices that ship with modified FMX
inside.

## Additional permission (GPL-3.0 section 7)

As an additional permission under GPL-3.0 section 7, you may combine Felucca, or a work
based on it, with the Felucca Assets (above), and convey the combination.
This is allowed even though the Felucca Assets are not licensed under the GPL, provided
that:

- you follow the GPL for every part that is not a Felucca Asset; and
- you follow the terms published for the assets.

The Felucca Assets are data (wavetables, icons, sample data). They are not program
code. A firmware image built from the GPL sources with replacement assets, or with no
assets, is entirely governed by the GPL.

## Third-party material

| What | Licence | Where |
| --- | --- | --- |
| Instrument and drum samples (Versilian Studios VSCO-2 CE, VCSL; Sonic Pi: SCRCH) | CC0 1.0 | `assets/samples-cc0/`, provenance in `ATTRIBUTION.txt` there |
| Terminus font 8x16 (ter-u16n): the FM-1's screen, and the web editor's font (`tools/gen_webfont.py` makes it a TrueType font inlined in `web/editor.html`) | SIL OFL 1.1 | `assets/fonts/ter-u16n.bdf`, `assets/fonts/Terminus-LICENSE.txt` |
| The BREAK part's built-in loops (in the firmware, FMX): fourteen break cuts (`assets/breaks/AMEN_A.wav` … `WMN.wav`), the "Jungle Breaks" pack by OaSyntax on freesound.org (<https://freesound.org/people/OaSyntax/packs/5130/>, sounds 78881–78894; the file-to-sound list is in `assets/breaks/CREDITS.txt`), renamed and resampled. Credited on the device's ABOUT screen. The licence covers OaSyntax's cuts, not the commercial records the breaks were taken from | CC BY 3.0 | `assets/breaks/`, `assets/breaks/CREDITS.txt` |
| The CHOP kits RAVE HITS (synthesised by `tools/gen_pads.py`, GPL) and ANIME KIT (lemonjolly on freesound.org, `assets/kits/CREDITS.txt`): the host tests' fixtures only (`tests/fixtures/packs`), not shipped by the companion app | GPL-3.0 / CC BY 4.0 | `tests/fixtures/packs/`, `assets/kits/` |
| *Not in this repository:* SLOOP's standard break set, three Amen cuts from KAN Samples' "Amen Break Tribute". Its licence forbids sharing the files, so they are kept only on the machine of whoever bought the pack (`tests/fixtures/kan/`, ignored by git) for `tests/brkpart_test.c`; FMX does not ship them | KAN Samples licence (not redistributable) | `tests/fixtures/README.md` |
| Fukiai icon font (Hügelton Instruments), web editor only | MIT | `web/fukiai.ttf`, `web/FUKIAI-LICENSE.txt` |
| Mutable Instruments' Braids and Plaits (<https://github.com/pichenettes/eurorack>, (c) 2012, 2014 Emilie Gillet), ported to fixed point for FMX: SYNC's band-limited saw (Braids' `analog_oscillator.cc`), the CHORUS bus's ensemble (Plaits' `ensemble.h`), DUST's smoothed rate reducer (Plaits' `sample_rate_reducer.h`) | MIT | `firmware/src/eng_analog.c`, `firmware/src/fx.c`, `LICENSES/MIT-Mutable-Instruments.txt` |
| JieLi AC79 SDK: `uboot.boot`, `cfg_tool.bin`, `eq_cfg_hw.bin` are read from your SDK checkout at build time and placed in the package; no SDK files are in this tree | Apache-2.0 | <https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK> |

## Contributions

Contributions are welcome under GPL-3.0-only. By submitting one, you agree that it may be
combined with the Felucca Assets under the section 7 permission above.

## Trademarks

"Felucca" and "Hügelton Instruments" are names of Hügelton Instruments.

"M-VAVE" and "FM-1" are trademarks of their respective owners. FMX is independent
firmware that runs on FM-1 hardware. It is not affiliated with, endorsed by or supported
by those owners.

## Radio

FMX never enables the Bluetooth / Wi-Fi radio of the hardware.
