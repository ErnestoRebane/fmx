# Test fixtures

- `packs/RAVEHITS.bin`, `packs/ANIME.bin`: the two CHOP kits the host tests play (`tools/gen_packs.py` makes them; RAVE HITS is
  synthesised by `tools/gen_pads.py`, ANIME KIT is lemonjolly's recordings, CC BY 4.0, `assets/kits/CREDITS.txt`). Since 1.0.0 the
  companion app does not ship them: it ships only `web/packs/PIANO.bin`.
- `breaks_cc.bin`: a user slot of three break loops (AMEN A, THINK, FUNKY from `assets/breaks`, OaSyntax's "Jungle Breaks",
  CC BY 3.0), cut to a bar each by `tools/upload_breaks.py`'s `cut`, for the emulator and web tests.
- `kan/` (**not in the repository**, `.gitignore`): three Amen cuts from KAN Samples' "Amen Break Tribute"
  (`KAB1_137_AmenBreak_Cut_02.wav`, `KAB1_137_AmenBreak_Cut_03.wav`, `KAB1_139_AmenBreak_Cut_04.wav`) and their slot image
  `breaks_standard.bin` (`tools/upload_breaks.py --standard-image`). Their licence forbids sharing the files, so they stay on
  the machine of whoever bought the pack. `tests/brkpart_test.c` (its fixture is SLOOP's AMEN 2, 3, 4) and one check of
  `web/test_breaks.mjs` run only when they are there; `tests/run_tests.sh` says SKIP otherwise.
