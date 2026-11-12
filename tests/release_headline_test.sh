#!/usr/bin/env bash
# SculptForge - open source digital sculpting
# SPDX-License-Identifier: GPL-3.0-or-later
#
# scripts/release.sh headline_check (ctest release_headline): --stage warns, never fails, when the CMO's launch headline
# (state/launch/X.Y.Z.json) is missing (and prompts the CMO through herdr) or its keywords are not all in HIGHLIGHTS line 1
# and the What's New tagline. Stand-in herdr, sandbox launch dir and whats-new.md.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
S=$(mktemp -d)
trap 'rm -rf "$S"' EXIT
fail() { echo "FAIL: $*" >&2; exit 1; }
mkdir -p "$S/bin" "$S/launch" "$S/wt/docs/guide"
printf '#!/bin/sh\necho "$*" >> "%s/herdr.log"\n' "$S" > "$S/bin/herdr"
chmod +x "$S/bin/herdr"
export PATH=$S/bin:$PATH SF_LAUNCH_DIR=$S/launch V=9.9.9 BASE=9.9.9
cd "$S/wt"
# shellcheck disable=SC1090
eval "$(sed -n '/^headline_check()/,/^}/p' "$ROOT/scripts/release.sh")" # not source <(...): macOS bash 3.2 cannot
printf '## Version 9.9.9\n\n```features\ndate: unreleased\ntagline: Reference images beside your sculpt.\nhighlight: x\n```\n\n## Version 9.9.8\n\ntagline: Older\n' > docs/guide/whats-new.md
HIGHLIGHTS=$S/hl.txt
printf 'Reference images: front and side drawings in the view\nsecond line\nthird line\n' > "$HIGHLIGHTS"

# 1. no headline file: a WARNING naming the file, the CMO is prompted, the stage goes on
out=$(headline_check) || fail "a missing headline failed the stage"
grep -q "WARNING: no launch headline for 9.9.9 ($S/launch/9.9.9.json missing)" <<<"$out" || fail "no warning: $out"
grep -q "^agent prompt cmo pick the 9.9.9 headline" "$S/herdr.log" || fail "the CMO was not prompted: $(cat "$S/herdr.log" 2>&1)"

# 2. every keyword present (case-insensitive) in both: silence, no prompt
: > "$S/herdr.log"
echo '{"line": "Reference images", "keywords": ["reference", "IMAGES"]}' > "$S/launch/9.9.9.json"
start=$(date +%s)
out=$(headline_check) || fail "failed"
[ $(($(date +%s) - start)) -le 2 ] || fail "headline_check took over 2 s"
[ -z "$out" ] || fail "a matching headline warned: $out"
[ ! -s "$S/herdr.log" ] || fail "the CMO was prompted although the headline exists"

# 3. a keyword missing from HIGHLIGHTS line 1 only (the tagline has it): named
echo '{"line": "Splats", "keywords": ["Reference", "sculpt"]}' > "$S/launch/9.9.9.json"
out=$(headline_check) || fail "failed"
grep -q 'keyword "sculpt" .* is missing from HIGHLIGHTS line 1$' <<<"$out" || fail "line 1 not named: $out"
grep -q 'keyword "Reference"' <<<"$out" && fail "a present keyword was reported: $out"

# 4. a keyword missing from the tagline only
printf 'Splats and Reference images: x\n' > "$HIGHLIGHTS"
echo '{"line": "Splats", "keywords": ["Splats"]}' > "$S/launch/9.9.9.json"
out=$(headline_check) || fail "failed"
grep -q 'keyword "Splats" .* is missing from the What.s New tagline$' <<<"$out" || fail "tagline not named: $out"

# 5. missing from both; and a broken JSON file: warnings, never a failure
echo '{"line": "Booleans", "keywords": ["Booleans"]}' > "$S/launch/9.9.9.json"
out=$(headline_check) || fail "failed"
grep -q 'is missing from HIGHLIGHTS line 1 and the What.s New tagline$' <<<"$out" || fail "both not named: $out"
echo 'not json' > "$S/launch/9.9.9.json"
out=$(headline_check) || fail "broken JSON failed the stage"
grep -q "not valid launch JSON" <<<"$out" || fail "no warning for broken JSON: $out"
echo "release_headline ok"
