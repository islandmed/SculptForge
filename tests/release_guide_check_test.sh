#!/usr/bin/env bash
# SculptForge - open source digital sculpting
# SPDX-License-Identifier: GPL-3.0-or-later
#
# scripts/release.sh guide_ok (ctest release_guide_check): the guide check (tools/guide/build_site.py --check) runs before the
# bump and stops the stage with its message when the What's New tagline has a colon chain (0.14.0: CI's guide_site test found it
# an hour into the stage), writes only under --out (website/guide untouched), and passes on a good tagline.
# Sandbox: the real build_site.py on a copy of the guide sources.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
S=$(mktemp -d)
trap 'rm -rf "$S"' EXIT
fail() { echo "FAIL: $*" >&2; exit 1; }
W=$S/wt
mkdir -p "$W/tools" "$W/docs" "$W/website" "$S/work"
cp -r "$ROOT/tools/guide" "$W/tools/guide"
cp -r "$ROOT/docs/guide" "$W/docs/guide"
cp "$ROOT/CMakeLists.txt" "$W/"
cp "$ROOT/docs/supporters.txt" "$W/docs/"
cp "$ROOT/README.md" "$W/" 2>/dev/null || true
for f in index.html supporters.html support.html founding.json members-live.json; do cp "$ROOT/website/$f" "$W/website/" 2>/dev/null || true; done
cd "$W"
work=$S/work
die() { echo "STOPPED: $1"; echo "fix: $2"; exit 1; }
# shellcheck disable=SC1090
eval "$(sed -n '/^guide_ok()/,/^}/p' "$ROOT/scripts/release.sh")" # not source <(...): macOS bash 3.2 cannot
ver=$(grep -m1 '^## Version ' docs/guide/whats-new.md | sed 's/## Version //')
tag=$(awk -v v="## Version $ver" '$0==v{f=1;next} f&&/^tagline:/{print; exit}' docs/guide/whats-new.md)
[ -n "$tag" ] || fail "no tagline found in the copied whats-new.md"

# 1. the committed tagline passes; nothing is written outside --out
( guide_ok ) >"$S/out1" 2>&1 || fail "a good tagline stopped the release: $(cat "$S/out1")"
[ ! -e website/guide ] || fail "the check wrote website/guide"
[ ! -e "$work/guide-check" ] || fail "the check left $work/guide-check behind"

# 2. a colon chain in the newest tagline: stops, and says why
awk -v v="## Version $ver" '$0==v{f=1} f&&/^tagline:/&&!d{print "tagline: Splats: shows more."; d=1; next} {print}' docs/guide/whats-new.md >"$S/wn.md"
cp "$S/wn.md" docs/guide/whats-new.md
rc=0; ( guide_ok ) >"$S/out2" 2>&1 || rc=$?
[ $rc -ne 0 ] || fail "a tagline with a colon chain passed: $(cat "$S/out2")"
grep -q "^STOPPED: the guide check fails" "$S/out2" || fail "no stop message: $(cat "$S/out2")"
grep -q "tagline is the headline" "$S/out2" || fail "the check's own message is missing: $(cat "$S/out2")"
[ ! -e website/guide ] || fail "a failing check wrote website/guide"

# 3. and release.sh calls it from preflight (bump mode), right after the words check
grep -q '^    \[ "\$mode" != bump \] || guide_ok$' "$ROOT/scripts/release.sh" || fail "preflight does not call guide_ok for step bump"
echo "release_guide_check ok"
