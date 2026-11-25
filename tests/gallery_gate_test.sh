#!/usr/bin/env bash
# SculptForge - open source digital sculpting
# SPDX-License-Identifier: GPL-3.0-or-later
#
# tools/ci/gallery_gate.sh (ctest gallery_gate): T0 stops a branch whose diff touches tools/brush-gallery/repo/ while the public
# gallery repository differs ("the public gallery repo needs the same change first"), lets it through when they match, skips
# the lookup when the diff doesn't touch the path, and only warns when the public tree can't be read.
# Sandbox git repo with the real check_published.py; the public tree comes from SF_GALLERY_PUBLIC (a JSON file), no network.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
S=$(mktemp -d)
trap 'rm -rf "$S"' EXIT
fail() { echo "FAIL: $*" >&2; exit 1; }
W=$S/wt
mkdir -p "$W/tools/ci" "$W/tools/brush-gallery/repo/packs"
cp "$ROOT/tools/ci/gallery_gate.sh" "$W/tools/ci/"
cp "$ROOT/tools/brush-gallery/check_published.py" "$W/tools/brush-gallery/"
cp "$ROOT/tools/check_marks.py" "$W/tools/"
mkdir -p "$W/docs/legal" && cp "$ROOT/docs/legal/trademarks.md" "$W/docs/legal/"
cp -r "$ROOT/tools/brush-gallery/repo/." "$W/tools/brush-gallery/repo/" 2>/dev/null || true
cd "$W"
export GIT_CONFIG_GLOBAL=/dev/null GIT_CONFIG_SYSTEM=/dev/null
git init -q .
git config user.email t@example.org
git config user.name t
printf 'clean pack\n' > tools/brush-gallery/repo/packs/gate-test.txt
git add -A && git commit -q -m base
base=$(git rev-parse HEAD)
# the public tree = the base tree (what the public repository carries)
python3 -I tools/brush-gallery/check_published.py --tree-json | python3 -c 'import json,sys; print(json.dumps({t["path"]: t["sha"] for t in json.load(sys.stdin)["tree"]}))' > "$S/public.json"
export SF_GALLERY_PUBLIC=$S/public.json

# 1. a diff outside the gallery: no lookup (an unreadable public tree changes nothing), exit 0
echo x > unrelated.txt; git add -A; git commit -q -m unrelated
SF_GALLERY_PUBLIC=$S/missing.json bash tools/ci/gallery_gate.sh "$base" HEAD >"$S/o1" 2>&1 || fail "an unrelated diff was stopped: $(cat "$S/o1")"
[ ! -s "$S/o1" ] || fail "an unrelated diff printed: $(cat "$S/o1")"

# 2. the diff touches the gallery and the public tree still differs: stopped, with the message
printf 'changed pack\n' > tools/brush-gallery/repo/packs/gate-test.txt; git add -A; git commit -q -m gallery
rc=0; bash tools/ci/gallery_gate.sh "$base" HEAD >"$S/o2" 2>&1 || rc=$?
[ $rc -eq 1 ] || fail "a gallery change the public repo lacks passed (rc $rc): $(cat "$S/o2")"
grep -q "the public gallery repo needs the same change first" "$S/o2" || fail "no message: $(cat "$S/o2")"
grep -q "packs/gate-test.txt: differs from" "$S/o2" || fail "the differing file is not named: $(cat "$S/o2")"

# 3. the public repository carries it: passes
python3 -I tools/brush-gallery/check_published.py --tree-json | python3 -c 'import json,sys; print(json.dumps({t["path"]: t["sha"] for t in json.load(sys.stdin)["tree"]}))' > "$S/public.json"
bash tools/ci/gallery_gate.sh "$base" HEAD >"$S/o3" 2>&1 || fail "a matching gallery was stopped: $(cat "$S/o3")"
grep -q "matches the public gallery" "$S/o3" || fail "no ok line: $(cat "$S/o3")"

# 4. the public tree can't be read (network, rate limit): a warning, never a red T0
rc=0; SF_GALLERY_PUBLIC=$S/missing.json bash tools/ci/gallery_gate.sh "$base" HEAD >"$S/o4" 2>&1 || rc=$?
[ $rc -eq 0 ] || fail "an unreadable public tree stopped T0: $(cat "$S/o4")"
grep -q "^WARNING: gallery: could not compare" "$S/o4" || fail "no warning: $(cat "$S/o4")"

# 5. t0.sh runs it (before the build) and names the failure
grep -q 'bash tools/ci/gallery_gate.sh "\$base" "\$tip"' "$ROOT/tools/ci/t0.sh" || fail "t0.sh does not run the gate"
grep -q 'failed=gallery_published' "$ROOT/tools/ci/t0.sh" || fail "t0.sh does not name the failure"
echo "gallery_gate ok"
