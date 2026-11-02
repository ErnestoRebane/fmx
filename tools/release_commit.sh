#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# FMX: the published branches, each one commit on top of its last (no history of live's, no work identity), by the
# published author:
#   release   live's tree without the working notes, the mock-ups, the agent skills and the banner's maker (PRIVATE)
#   gh-pages  the companion site (GitHub Pages from gh-pages' root), made by web/make_site.py from the last build
#             (build/felucca.fwsc, build/emu: sh tools/check.sh first) at the firmware's version
# A branch that already has its tree gets no commit. Nothing is pushed.
#   sh tools/release_commit.sh "MESSAGE" [FROM]      (FROM: default live)
#   then: git push github release:main gh-pages:gh-pages
cd "$(dirname "$0")/.." || exit 1
[ -n "$1" ] || { echo "release_commit: a message"; exit 2; }
FROM=${2:-live}
PRIVATE=".claude docs/v1-plan.md docs/ui-audit.md docs/style-audit.md docs/transitions.md docs/chop.md docs/fx-panel.md
docs/companion docs/live tools/banner.html"
VER=$(sed -n 's/^#define FELUCCA_VERSION "FMX \([^"]*\)".*/\1/p' firmware/src/ui.c)
[ -n "$VER" ] || { echo "release_commit: no FELUCCA_VERSION in firmware/src/ui.c"; exit 1; }
[ -f build/felucca.fwsc ] && [ -f build/emu/sloop.wasm ] || { echo "release_commit: no build (sh tools/check.sh)"; exit 1; }
IDX=$(mktemp "${TMPDIR:-/tmp}/fmx-release-index.XXXXXX") || exit 1
SITE=$(mktemp -d "${TMPDIR:-/tmp}/fmx-release-site.XXXXXX") || exit 1
trap 'rm -rf "$IDX" "$IDX.site" "$SITE"' EXIT

# BRANCH TREE MESSAGE: its next commit, by the published author
publish() {
    P=
    if git rev-parse -q --verify "refs/heads/$1" >/dev/null; then
        if [ "$2" = "$(git rev-parse "$1^{tree}")" ]; then
            echo "release_commit: $1 already has this tree"
            return 0
        fi
        P="-p $1"                                   # (no parent: the branch's first commit)
    fi
    C=$(GIT_AUTHOR_NAME=ErnestoRebane GIT_AUTHOR_EMAIL=ErnestoRebane@users.noreply.github.com \
        GIT_COMMITTER_NAME=ErnestoRebane GIT_COMMITTER_EMAIL=ErnestoRebane@users.noreply.github.com \
        git commit-tree "$2" $P -m "$3") || return 1
    git update-ref "refs/heads/$1" "$C" || return 1
    echo "$1: $(git log -1 --oneline "$1") (from $FROM $(git rev-parse --short "$FROM"))"
    git rev-parse -q --verify "$1~1" >/dev/null && git diff --stat "$1~1" "$1" | tail -1
    return 0
}

GIT_INDEX_FILE=$IDX git read-tree "$FROM^{tree}" || exit 1
for p in $PRIVATE; do
    GIT_INDEX_FILE=$IDX git rm -r -q --cached --ignore-unmatch -- "$p" || exit 1
done
TREE=$(GIT_INDEX_FILE=$IDX git write-tree) || exit 1
publish release "$TREE" "$1" || exit 1

python3 web/make_site.py build/felucca.fwsc "$VER" "$SITE" --emu build/emu || exit 1
: >"$SITE/.nojekyll"                                # the files as they are, no Jekyll
rm -f "$IDX"
(cd "$SITE" && find . -type f | sed 's|^\./||') | while IFS= read -r f; do
    printf '100644 %s\t%s\n' "$(git hash-object -w -- "$SITE/$f")" "$f"
done >"$IDX.site" || exit 1
GIT_INDEX_FILE=$IDX git update-index --add --index-info <"$IDX.site" || exit 1
TREE=$(GIT_INDEX_FILE=$IDX git write-tree) || exit 1
publish gh-pages "$TREE" "FMX $VER: the companion site" || exit 1
