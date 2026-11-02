# The BREAK part's built-in loops

**FMX's own loops** (2026-10-20): `AMEN_A`, `AMEN_B`, `THNK`, `FNKDR`, `APCH`, `ACTN`, `DTD`, `HLCP`, `HT_PNTS`, `PLP_FC`, `SCRP_A`,
`SCRP_B`, `TRMN`, `WMN` (`.wav`), one bar each at ~160-170 BPM (`AMEN_B` and `HT_PNTS` two), supplied by the person who asked for
them as the firmware's loops. `tools/gen_samples.py` (`FMX_OWN`) builds all fourteen into the firmware at 16 kHz (`BRK_RATE`, IMA
ADPCM, ~179 KB); LOOP A..D start on the first four.

**Licence: CC BY 3.0.** They are the "Jungle Breaks" pack by OaSyntax on freesound.org; the sound for each file and the attribution are in [CREDITS.txt](CREDITS.txt), and the ABOUT screen credits OaSyntax. The licence covers OaSyntax's cuts, not the records the breaks come from; see [LICENSING.md](../../LICENSING.md). To replace them, use any WAV of a bar or two and change the names in `FMX_OWN` (7 characters at most) and the credits.

The **standard set** (SLOOP's AMEN 2, 3, 4, one-bar Amen cuts from KAN Samples' "Amen Break Tribute") is **not in this
repository**: its licence forbids sharing the files. It is only the fixture of `tests/brkpart_test.c` (`FMX_BREAK_SET=amen`), kept
locally in `tests/fixtures/kan/` (see [tests/fixtures/README.md](../../tests/fixtures/README.md)); the companion app does not ship it.
