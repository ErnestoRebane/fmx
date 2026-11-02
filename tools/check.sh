#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# FMX: everything a change needs checked, in one go, and a short summary (the full logs: build/check/*.log).
#   1 target   the firmware for the FM-1 (colima, build.sh): its image, RAM and pool (colima stopped after, if this
#              started it)
#   2 tests    tests/run_tests.sh, SOAK_MIN (default 2): the host tests, with the target's render budgets on that build
#   3 emu      the browser emulator (web/emu/build.sh) and its test (emu_test.mjs)
#   4 site     web/make_site.py with the emulator in it; the package the app serves is the build's (SHA)
#   and whether the site's server answers on 8792 (it is not started here).
#   sh tools/check.sh [--quick] [--no-target] [--no-site]
#     --quick      the host tests only (SOAK_MIN=1 unless set)
#     --no-target  no colima / target build (the tests' budgets read the last build)
#     --no-site    no emulator, no site
# Exit status 0: all of it passed.
cd "$(dirname "$0")/.." || exit 1
LOG=build/check
mkdir -p "$LOG"
TARGET=1 SITE=1 QUICK=0
for a in "$@"; do
    case "$a" in
        --quick) QUICK=1 TARGET=0 SITE=0 ;;
        --no-target) TARGET=0 ;;
        --no-site) SITE=0 ;;
        *) echo "check: $a? (--quick --no-target --no-site)"; exit 2 ;;
    esac
done
[ "$QUICK" = 1 ] && : "${SOAK_MIN:=1}"
: "${SOAK_MIN:=2}"
export SOAK_MIN
fails=0
SUM=""
say() { SUM="$SUM$1
"; }
# a step: its name, a command; the log kept, a few lines of what failed shown
step() {
    n=$1
    shift
    if "$@" >"$LOG/$n.log" 2>&1; then
        return 0
    fi
    fails=$((fails + 1))
    say "FAIL $n (build/check/$n.log):"
    say "$(grep -E 'FAIL|rror|Assertion|failed' "$LOG/$n.log" | grep -v ' ok$' | head -12 | sed 's/^/       /')"
    return 1
}

started=0
if [ "$TARGET" = 1 ]; then
    if ! colima status >/dev/null 2>&1; then
        colima start --vm-type vz --vz-rosetta --cpu 4 --memory 4 --disk 20 --mount "$HOME:w" >"$LOG/colima.log" 2>&1 && started=1
    fi
    if step target sh build.sh; then
        say "ok   target  $(grep -E 'ok +image' "$LOG/target.log" | sed 's/.*ok *//')"
    fi
    [ "$started" = 1 ] && colima stop >/dev/null 2>&1
fi

if step tests sh tests/run_tests.sh; then
    say "ok   tests   all host tests (SOAK_MIN=$SOAK_MIN)"
fi
over=$(grep -E '^target: .*budget' "$LOG/tests.log" | grep -v ' ok$')
[ -n "$over" ] && say "     budget  $over"

if [ "$SITE" = 1 ]; then
    if step emu sh -c 'sh web/emu/build.sh && node web/emu/emu_test.mjs'; then
        say "ok   emu     $(grep -E '^emu: build/emu' "$LOG/emu.log" | sed 's/^emu: //')"
    fi
    if step site python3 web/make_site.py build/felucca.fwsc 0.1 build/site --emu build/emu; then
        a=$(shasum build/felucca.fwsc | cut -c1-12)
        b=$(cat build/site/firmware/*.fwsc | shasum | cut -c1-12)
        if [ "$a" = "$b" ]; then
            say "ok   site    the app serves this build ($a)"
        else
            fails=$((fails + 1))
            say "FAIL site    the app's package ($b) is not this build ($a)"
        fi
    fi
    if curl -s -o /dev/null --max-time 2 http://127.0.0.1:8792/; then
        say "     server  up: http://127.0.0.1:8792/"
    else
        say "     server  down (python3 tools/serve.py: build/site on 8792, no caching)"
    fi
fi

printf '%s' "$SUM"
[ "$fails" = 0 ] && echo "CHECK PASSED" || { echo "CHECK FAILED ($fails)"; exit 1; }
