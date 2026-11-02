# Building FMX

The build makes three files in `build/`:

| File | What |
| --- | --- |
| `felucca.bin` | the firmware app |
| `loader/ota.bin` | the update loader |
| `felucca.fwsc` | the installable package (app + loader) |

## Windows (WSL)

`tools/build_windows.py` makes the assets on Windows and compiles in a WSL distribution that has the
JieLi toolchain. It needs Python 3 with Pillow on Windows and the three SDK files (below):

```
python tools/build_windows.py --distro Ubuntu --toolchain /root/.jieli/toolchain --sdk build/deps/ac79
```

Then make the companion site and serve it (see *Companion app* below).

## Prerequisites (macOS)

- Python 3 with Pillow: `pip3 install Pillow`
- Docker Desktop. The JieLi toolchain is Linux x86-64 only; the build runs each tool in a
  `linux/amd64` `debian:bookworm-slim` container (Rosetta on Apple silicon). Keep the source
  tree in a folder Docker can share, e.g. under `/Users`.
- The JieLi Linux toolchain (clang 4.0.1 for pi32v2, from JieLi's package server):

  ```
  tools/get_toolchain.sh            # installs to ~/.jieli/toolchain
  ```

- The JieLi AC79 SDK (Apache-2.0). The package uses three of its files
  (`cpu/wl82/tools/uboot.boot`, `cfg_tool.bin`, `cfg/eq_cfg_hw.bin`); they are not part of this tree.

  ```
  git clone --depth 1 --branch AC79NN_SDK_V1.2.1_2023-12-13 \
      https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK.git ~/fw-AC79_AIoT_SDK
  ```

- Node.js (optional, for the web tests).

On Linux x86-64 the toolchain runs natively and Docker is not needed.

## Build

```
./build.sh
```

`JIELI_TOOLCHAIN` and `AC79_SDK` override the default locations
(`~/.jieli/toolchain`, `~/fw-AC79_AIoT_SDK`).

Without `--release` the version string is `FELUCCA_VERSION` in `firmware/src/ui.c` (now `FMX 1.0.0_beta`) and
the package `build/felucca.fwsc`. `./build.sh --release 1.0-beta` makes a release build: the package identity
becomes `FM-1_910` and the version string `1.0-BETA`; the package is `build/felucca-1.0-beta.fwsc`. `--release`
takes `X.Y` or `X.Y-suffix`, one digit each (the identity has room for two).

Build options (environment, `0` or `1`; defaults in `firmware/src/felucca.c`):

| Flag | Default | |
| --- | --- | --- |
| `FELUCCA_FLASH` | 1 | settings, presets and projects in flash |
| `FELUCCA_OTA` | 1 | update entry (needs `FELUCCA_FLASH`) |
| `FELUCCA_CDC` | 1 | USB serial console |
| `FELUCCA_UAC` | 1 | USB audio input: the master output, 44.1 kHz stereo (after Felucca 1.0) |
| `FELUCCA_UART` | 1 | TRS MIDI IN (the 3.5 mm jack) |
| `FELUCCA_BREAK` | 1 | the BREAK part (brkpart.c, ui_break.c, 2.6): X0X's breakbeat player (~14 KB of the app slot, ~3.5 KB of RAM; its loops are in the user slots) |
| `SLOOP_BREAK_LOOPS` | all (14) | how many of the BREAK's loops (`assets/breaks`) are built into the firmware; 0 leaves them to the user slots, as SLOOP did |
| `FELUCCA_SLICE` | 0 | the SLICE engine itself (eng_slice.c); the BREAK part does not need it |

## Browser emulator

The firmware's own sources, compiled to WebAssembly and run in an AudioWorklet against a simulated FM-1 (screen,
keys, knobs, LEDs, flash; not USB or the editor): `web/emu/sloop_web.c`, after Felucca's emulator and X0X's. It needs
Emscripten (`brew install emscripten`) and `build/gen` (`./build.sh` makes it; without the JieLi toolchain, run the
`tools/gen_*.py` of `tools/build.py` `generate()` by hand):

```
web/emu/build.sh                                                  # build/emu: sloop.wasm, worklet.js
python3 web/make_site.py build/felucca.fwsc dev build/site --emu build/emu
python3 tools/serve.py                       # open http://127.0.0.1:8792/#emulator and press "Switch on"
```

Its page is the companion app (`web/app.html`, below). `EMU_VERSION="FMX 1.0.0_beta"` sets the label. At the first switch-on the app puts the PIANO pack in USR1 and the demo set in the project slots; with *Load my sounds* it puts the sounds of the app's library (the ones you added or sent) in the emulator's sample pool. `web/emu/emu_test.mjs` runs the WebAssembly build in Node (`web/emu/build.sh` first). The page keeps the flash's storage sectors (projects, presets, the FM6 bank,
your samples, settings) in the browser.

## Companion app

One page for everything the FM-1 needs from a computer, in the LIVE screen's look: the emulator, the firmware (install, update, back to the stock V15) and the sounds on the FM-1 (the sample
pool's map and list, adding sounds: a file told apart as a BREAK loop, a LOOP for the CHOP's SLICE, a KIT hit or an
INSTRUMENT note, at 22, 16 or 11 kHz; reading sounds back; the set's songs and its backup; the packs). `web/app.html`
and its modules (`web/app.js`, `app_draw.js`, `app_emu.js`, `app_device.js`, `sounds.js`, with `pool.js`, `breaks.js`,
`slice.js`, `fm1ota.js`, `fm1pkg.js`); `web/make_site.py` publishes it as the site's index, the old pages' addresses
(`webapp/installer`, `breaks`, `slice`, `emu`) sending to its parts. The published site is the GitHub branch `gh-pages`: `tools/release_commit.sh` makes it
from the last build at the firmware's version, for GitHub Pages from `gh-pages`' root. `node web/test_app.mjs` runs its sounds and its FM-1
side against the pool's mock FM-1. `web/emu/native_check.c` builds the same file with `cc` and checks the boot, the
home screen and the BREAK screen as a hand drives it (steps, pins, pages, PLAY), the part's state in the settings (screens as PPM in the folder you give it).

## Samples

The CC0 instrument samples that the SAMPLE engine uses are in `assets/samples-cc0/`
(Versilian Studios, see `ATTRIBUTION.txt` there). `tools/fetch_cc0.py` downloads them
again from the source repositories. Without that folder the build still works and the
SAMPLE engine has only the generated drum kit.

## Tests

```
tests/run_tests.sh
```

Runs the host tests (flash storage, user presets, MIDI parser, update entry, update
loader, a DSP render, the 4-track mix, project formats, the SLICER, the regression suite,
the command-line installer) and, with Node.js, the web page tests. Run it after `./build.sh`
(it uses `build/` and needs `AC79_SDK` set as for the build).

The regression suite (`tests/regress.c`) renders every engine and preset and compares a
hash of each render with `tests/golden.txt`; it also checks levels, voices and the CPU
cost (`tests/cpu_baseline.txt`, `tests/target_budget.txt`). The host CPU cost is relative
to the idle + drums mix, counted by the kernel on macOS and under callgrind on Linux when
valgrind is installed (exact, about 45 s more); without either it is timed, a rough check.
After an intended change of the sound, `GOLDEN_UPDATE=1 sh tests/run_tests.sh` rewrites
the hashes; `BUDGET_UPDATE=1` does the same for the cost files.

## Install

The companion app installs the firmware (Chrome or Edge, see above). The `.fwsc` of each release is
on the GitHub releases page.

From the command line (needs `pip3 install mido python-rtmidi`):

```
python3 tools/fm1_install.py build/felucca.fwsc
python3 tools/fm1_install.py --info          # identity of the connected FM-1
```

Or, to install your own build from the companion app, make a local copy of the site and open it from `localhost`
(Web MIDI needs a secure context):

```
python3 web/make_site.py build/felucca.fwsc dev /tmp/fmx-site --emu build/emu
cd /tmp/fmx-site && python3 -m http.server 8000
# open http://localhost:8000/#firmware
```

Installing firmware is at your own risk. If an install fails and the FM-1 no longer
starts, recovery needs [FM-1-transporter](https://github.com/kurogedelic/FM-1-transporter).
