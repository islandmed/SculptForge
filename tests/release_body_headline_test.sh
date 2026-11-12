#!/usr/bin/env bash
# SculptForge - open source digital sculpting
# SPDX-License-Identifier: GPL-3.0-or-later
#
# scripts/release.sh release_body_headline (ctest release_body_headline): after the GitHub release exists, the launch headline
# (state/launch/X.Y.Z.json: line, optional lede and link) becomes the first line of its body. Skipped when the file or "line"
# is missing or the body already leads with it; never fails. Stand-in gh with a body file, sandbox launch dir.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
S=$(mktemp -d)
trap 'rm -rf "$S"' EXIT
fail() { echo "FAIL: $*" >&2; exit 1; }
mkdir -p "$S/bin" "$S/launch" "$S/work"
cat > "$S/bin/gh" <<'SH'
#!/bin/sh
# gh release view T -R repo --json body -q .body | gh release edit T -R repo --notes-file F
[ -n "$GH_FAIL" ] && exit 1
case "$2" in
  view) cat "$GH_BODY" ;;
  edit) while [ $# -gt 0 ]; do [ "$1" = --notes-file ] && cp "$2" "$GH_BODY"; shift; done; echo x >> "$GH_BODY.edits" ;;
esac
SH
chmod +x "$S/bin/gh"
export PATH=$S/bin:$PATH SF_LAUNCH_DIR=$S/launch V=9.9.9 BASE=9.9.9 T=v9.9.9 RELEASE_REPO=x/y GH_BODY=$S/body GH_FAIL=
work=$S/work
# shellcheck disable=SC1090
source <(sed -n '/^release_body_headline()/,/^}/p' "$ROOT/scripts/release.sh")
reset() { printf '## What changed\n\n- a thing\n' > "$S/body"; rm -f "$S/body.edits"; }

# 1. no file: untouched
reset; release_body_headline || fail "failed without a file"
[ ! -e "$S/body.edits" ] || fail "edited without a launch file"

# 2. line, lede and link: the line is prepended, utm added before the fragment
reset
echo '{"line": "Reference images: drawings in the view.", "lede": "View > Reference images keeps them.", "link": "https://sculptforge.app/guide/reference-images.html#front"}' > "$S/launch/9.9.9.json"
release_body_headline || fail "failed"
want='**Reference images: drawings in the view.** View > Reference images keeps them. [How it works](https://sculptforge.app/guide/reference-images.html?utm_source=github&utm_campaign=999#front)'
[ "$(head -n1 "$S/body")" = "$want" ] || fail "first line: $(head -n1 "$S/body")"
sed -n '2p' "$S/body" | grep -q '^$' || fail "no blank line after the headline"
grep -q '^## What changed' "$S/body" || fail "old body lost"

# 3. run again: idempotent (one edit only)
release_body_headline || fail "failed"
[ "$(wc -l < "$S/body.edits")" = 1 ] || fail "edited twice"

# 4. only a line: bold line alone; a link with a query gets &utm
reset; echo '{"line": "Only a line"}' > "$S/launch/9.9.9.json"
release_body_headline || fail "failed"
[ "$(head -n1 "$S/body")" = '**Only a line.**' ] || fail "line only: $(head -n1 "$S/body")"
reset; echo '{"line": "L", "link": "https://x.y/z?a=1"}' > "$S/launch/9.9.9.json"
release_body_headline || fail "failed"
head -n1 "$S/body" | grep -qF '(https://x.y/z?a=1&utm_source=github&utm_campaign=999)' || fail "query link: $(head -n1 "$S/body")"

# 5. a body the CMO already led with the headline: left alone
printf '**L.** by hand\n\nrest\n' > "$S/body"; rm -f "$S/body.edits"
release_body_headline || fail "failed"
[ ! -e "$S/body.edits" ] || fail "rewrote a body that already leads with the headline"

# 6. no "line", broken JSON, gh failing: nothing happens, never a failure
reset; echo '{"lede": "x"}' > "$S/launch/9.9.9.json"; release_body_headline || fail "no line failed"
echo 'not json' > "$S/launch/9.9.9.json"; release_body_headline || fail "broken JSON failed"
[ ! -e "$S/body.edits" ] || fail "edited without a usable line"
echo '{"line": "L"}' > "$S/launch/9.9.9.json"; GH_FAIL=1 release_body_headline || fail "gh failure failed the release"
echo "release_body_headline ok"
