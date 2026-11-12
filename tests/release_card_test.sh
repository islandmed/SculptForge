#!/usr/bin/env bash
# SculptForge - open source digital sculpting
# SPDX-License-Identifier: GPL-3.0-or-later
#
# scripts/release.sh release_card (ctest release_card): the CMO's release card (state/launch/X.Y.Z.card.jpg) is copied to
# website/media/release-X.Y.Z.jpg and staged with the marks cache when tools/check_marks_media.py passes; a missing card, or one
# the check flags, is a WARNING and leaves nothing staged; an already committed card is left alone. Never fails.
# Sandbox git repo with a stand-in check_marks_media.py.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
S=$(mktemp -d)
trap 'rm -rf "$S"' EXIT
fail() { echo "FAIL: $*" >&2; exit 1; }
mkdir -p "$S/launch" "$S/wt/website/media" "$S/wt/tools"
export SF_LAUNCH_DIR=$S/launch V=9.9.9 GIT_CONFIG_GLOBAL=/dev/null GIT_CONFIG_SYSTEM=/dev/null
cd "$S/wt"
git init -q .
git config user.email t@example.org
git config user.name t
# the stand-in check: fails when the card's first byte is X, else records the card in the cache
cat > tools/check_marks_media.py <<'PY'
import sys
card = open("website/media/release-9.9.9.jpg", "rb").read()
if card[:1] == b"X":
    open("tools/marks_media_cache.json", "w").write("polluted")
    print("website/media/release-9.9.9.jpg: shows a mark")
    sys.exit(1)
open("tools/marks_media_cache.json", "w").write('{"card": 1}')
print("no marks")
PY
echo '{}' > tools/marks_media_cache.json
git add -A && git commit -q -m base
ok() { printf '   ok: %s\n' "$*"; }
# shellcheck disable=SC1090
eval "$(sed -n '/^lockretry()/,/^}/p' "$ROOT/scripts/release.sh")"
eval "$(sed -n '/^release_card()/,/^}/p' "$ROOT/scripts/release.sh")"
staged() { git diff --cached --name-only | tr '\n' ' '; }

# 1. no card: a WARNING naming the file, nothing staged, no failure
out=$(release_card) || fail "a missing card failed"
grep -q "WARNING: no release card for 9.9.9 ($S/launch/9.9.9.card.jpg missing)" <<<"$out" || fail "no warning: $out"
[ -z "$(staged)" ] || fail "something staged without a card: $(staged)"

# 2. a clean card: copied, staged with the cache
printf 'JPEGDATA' > "$S/launch/9.9.9.card.jpg"
out=$(release_card) || fail "failed"
release_card >/dev/null || fail "second call failed"   # (in this shell: the staging happens here, not in the $(...) above)
[ "$(staged)" = "tools/marks_media_cache.json website/media/release-9.9.9.jpg " ] || fail "staged: $(staged)"
cmp -s website/media/release-9.9.9.jpg "$S/launch/9.9.9.card.jpg" || fail "the card was not copied"
grep -q "release card website/media/release-9.9.9.jpg checked and staged" <<<"$out" || fail "no ok line: $out"

# 3. committed: the next call leaves everything alone
git -c user.email=t@example.org -c user.name=t commit -q -m card
printf 'CHANGED' > "$S/launch/9.9.9.card.jpg"
release_card >/dev/null || fail "failed"
[ -z "$(staged)" ] || fail "a committed card was staged again: $(staged)"
[ "$(cat website/media/release-9.9.9.jpg)" = JPEGDATA ] || fail "a committed card was replaced"

# 4. a flagged card: WARNING, card unstaged and removed, the cache restored, no failure
git rm -q -f website/media/release-9.9.9.jpg && git commit -q -m rm
printf 'XFLAGGED' > "$S/launch/9.9.9.card.jpg"
out=$(release_card) || fail "a flagged card failed the release"
grep -q "WARNING: the release card is not clean" <<<"$out" || fail "no warning: $out"
[ -z "$(staged)" ] || fail "a flagged card stayed staged: $(staged)"
[ ! -e website/media/release-9.9.9.jpg ] || fail "a flagged card stayed on disk"
[ "$(cat tools/marks_media_cache.json)" = '{"card": 1}' ] || fail "the cache was not restored: $(cat tools/marks_media_cache.json)"
echo "release_card ok"
