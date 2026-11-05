#!/usr/bin/env bash
# SculptForge - open source digital sculpting
# SPDX-License-Identifier: GPL-3.0-or-later
#
# scripts/release.sh's beta path, run for real in a sandbox (ctest release_script_beta): a shared clone of this
# checkout (with its uncommitted changes), a bare repository as origin, and stand-ins for gh, curl, cmake and ctest,
# so nothing reaches GitHub, the website or the build. It releases 9.0.0-beta.1 (steps bump check preview status),
# then promotes it to 9.0.0 (bump check site) with the beta tag present, and checks what each one may and may not
# touch (docs/releasing.md, "Betas").
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
# CI containers check the repository out as another user ("dubious ownership"); HOME changes below, so not --global.
export GIT_CONFIG_COUNT=1 GIT_CONFIG_KEY_0=safe.directory GIT_CONFIG_VALUE_0='*'
S=$(mktemp -d)
trap 'rm -rf "$S"' EXIT
fail() { echo "FAIL: $*" >&2; exit 1; }
quiet() { "$@" >"$S/out.log" 2>&1 || { cat "$S/out.log" >&2; fail "$* exited $?"; }; }

# --- the sandbox -----------------------------------------------------------------------------------------------------
git clone -q --shared --no-checkout "$ROOT" "$S/repo"
cd "$S/repo"
git sparse-checkout set --no-cone /CMakeLists.txt /CHANGELOG.md /CITATION.cff /packaging/ /docs/ /tools/ /website/ \
    /scripts/ /changes/ /tests/ui/whats_new.sfui
git checkout -q -B main "$(git -C "$ROOT" rev-parse HEAD)"
# This checkout's uncommitted changes (the tree being tested), then one commit on main.
(cd "$ROOT" && git diff --name-only HEAD -- CMakeLists.txt CHANGELOG.md packaging docs tools website scripts changes tests/ui/whats_new.sfui) |
    while read -r f; do
        if [ -e "$ROOT/$f" ]; then mkdir -p "$(dirname "$f")" && cp "$ROOT/$f" "$f"; else rm -f "$f"; fi
    done
git config user.name test && git config user.email test@example.org
rm -f changes/[!R]*.md # this branch's own fragments: the test writes its own
mkdir -p changes
cat > changes/beta-test.md <<'EOF'
---
whats_new: **Beta test feature**: something new to try.
highlight: First highlight of the beta
highlight: Second highlight of the beta
highlight: Third highlight of the beta
added: **Beta test feature**: something new to try.
testers:
  - Windows, 150% scaling: open the beta test feature and try it.
---
EOF
echo '# expect_text "Version 9.0.0"' >> tests/ui/whats_new.sfui # words_ok wants the UI test to check the version
git add -A . && git commit -q -m "sandbox"
git init -q --bare "$S/origin.git"
git remote set-url origin "$S/origin.git"
# One root commit with the whole tree: CI's shallow checkout can't be pushed to a fresh bare repository.
git reset -q --soft "$(git commit-tree "HEAD^{tree}" -m sandbox)"
git push -q origin main

# Stand-ins: gh answers the few questions the tested steps ask; curl serves the sandbox's own site.
mkdir -p "$S/bin" "$S/home" "$S/build"
touch "$S/build/CTestTestfile.cmake"
cat > "$S/bin/gh" <<EOF
#!/usr/bin/env bash
case "\$*" in
  "auth status"*) echo "Logged in to github.com account islandmed" ;;
  "release view"*) echo SHA256SUMS.txt ;;
  "release download"*) exit 1 ;;
  *homebrew-tap*) printf '  version "0.10.10"\n' | base64 ;;
  "pr view 1800"*) echo "OPEN islandmed sculptforge-bump" ;;
  *omarchy-pkgs/contents*) printf "sha256sums=('%064d')\n" 0 | base64 ;;
  *) echo "gh stand-in: unexpected gh \$*" >&2; exit 1 ;;
esac
EOF
cat > "$S/bin/curl" <<EOF
#!/usr/bin/env bash
for a in "\$@"; do
  case "\$a" in
    https://sculptforge.app/latest.json*) git -C "$S/origin.git" show main:website/latest.json; exit ;;
    https://sculptforge.app/guide/whats-new.html*) git -C "$S/origin.git" show main:docs/guide/whats-new.md; exit ;;
  esac
done
echo "curl stand-in: unexpected curl \$*" >&2; exit 1
EOF
printf '#!/bin/sh\nexit 0\n' > "$S/bin/cmake"
printf '#!/bin/sh\nexit 0\n' > "$S/bin/ctest"
chmod +x "$S/bin/"*
export PATH="$S/bin:$PATH" HOME="$S/home" BUILD_DIR="$S/build" YES=1
unset HIGHLIGHTS

before() { git show "$1:$2"; }
stable_files="CITATION.cff packaging/rez/package.py packaging/linux/org.sculptforge.SculptForge.metainfo.xml website/index.html packaging/arch/PKGBUILD"

# --- usage ---------------------------------------------------------------------------------------------------------
for bad in 9.0.0-beta 9.0.0-beta.x 9.0.0-alpha.1 v9.0.0-beta.1 9.0; do
    if scripts/release.sh "$bad" --only status >/dev/null 2>&1; then fail "release.sh accepted '$bad'"; fi
done

# --- the beta ------------------------------------------------------------------------------------------------------
start=$(git rev-parse HEAD)
quiet scripts/release.sh 9.0.0-beta.1 --only bump
grep -q '^project(SculptForge VERSION 9.0.0 ' CMakeLists.txt || fail "beta bump: CMakeLists.txt is not at 9.0.0"
grep -qx 'set(SCULPTFORGE_PRERELEASE "beta.1")' CMakeLists.txt || fail "beta bump: no prerelease beta.1"
for f in $stable_files; do
    [ "$(before "$start" "$f")" = "$(cat "$f")" ] || fail "beta bump changed $f (a beta leaves it to the stable release)"
done
grep -q '^## \[9.0.0\]' CHANGELOG.md && fail "beta bump made a CHANGELOG [9.0.0] section"
grep -q 'Beta test feature' CHANGELOG.md || fail "beta bump: the fragment's entry is not in CHANGELOG [Unreleased]"
grep -q '^## Version 9.0.0$' docs/guide/whats-new.md || fail "beta bump: no What's New 9.0.0"
[ ! -e changes/beta-test.md ] || fail "beta bump: the fragment is still there"
grep -q 'open the beta test feature' release-testers-9.0.0-beta.1.md || fail "beta bump: no testers checklist"
[ "$(git rev-parse HEAD)" = "$(git -C "$S/origin.git" rev-parse main)" ] || fail "beta bump: main not pushed"

quiet scripts/release.sh 9.0.0-beta.1 --only check
grep -q 'CMakeLists.txt says 9.0.0 beta.1' "$S/out.log" || fail "beta check: $(cat "$S/out.log")"
quiet scripts/release.sh 9.0.0-beta.1 --only bump # again: nothing left to do
grep -q 'already says' "$S/out.log" || fail "beta bump is not idempotent: $(cat "$S/out.log")"

# A beta needs highlights for its feed entry: in What's New's block now (the fragment's went there).
quiet scripts/release.sh 9.0.0-beta.1 --only preview
python3 - <<'EOF' || fail "beta preview: wrong feed"
import json, subprocess
feed = json.loads(subprocess.run(["git", "show", "HEAD:website/latest.json"], capture_output=True, text=True).stdout)
old = json.loads(subprocess.run(["git", "show", "HEAD~1:website/latest.json"], capture_output=True, text=True).stdout)
p = feed["channels"]["preview"]
assert p["version"] == "9.0.0-beta.1", p
assert p["download"].endswith("/releases/tag/v9.0.0-beta.1"), p
assert p["highlights"][0] == "First highlight of the beta", p
assert feed["version"] == old["version"] and feed["highlights"] == old["highlights"], "the stable top level changed"
EOF
git diff --quiet HEAD~1 HEAD -- website/index.html || fail "beta preview touched the home page"
quiet scripts/release.sh 9.0.0-beta.1 --only status
grep -q 'release-testers-9.0.0-beta.1.md' "$S/out.log" || fail "beta status names no testers checklist"
grep -q 'scripts/release.sh 9.0.0 --yes' "$S/out.log" || fail "beta status doesn't say how to promote"
git tag v9.0.0-beta.1 "$(git rev-parse HEAD~1)" # what step tag would have pushed

# --- promoting it: the stable 9.0.0 with the beta tag present ------------------------------------------------------
# People give the version its tagline before the stable release (the home page card needs it).
python3 - <<'EOF'
p = "docs/guide/whats-new.md"
t = open(p, encoding="utf-8").read()
head = "## Version 9.0.0\n\n```features\ndate: unreleased\n"
assert head in t, "no 9.0.0 features block"
open(p, "w", encoding="utf-8").write(t.replace(head, head + "tagline: The beta, finished.\n", 1))
EOF
grep -q '^tagline: The beta, finished.$' docs/guide/whats-new.md || fail "test setup: couldn't add the tagline"
git commit -q -am "What's New 9.0.0 tagline" && git push -q origin main
quiet scripts/release.sh 9.0.0 --only bump
grep -qx 'set(SCULPTFORGE_PRERELEASE "")' CMakeLists.txt || fail "stable bump didn't clear the prerelease"
grep -q '^## \[9.0.0\] - ' CHANGELOG.md || fail "stable bump: no CHANGELOG [9.0.0]"
grep -qx 'version: 9.0.0' CITATION.cff || fail "stable bump: CITATION.cff not at 9.0.0"
grep -qF '<small>new in 9.0.0</small>' website/index.html || fail "stable bump: no home page card"
quiet scripts/release.sh 9.0.0 --only check
grep -q 'say 9.0.0 at' "$S/out.log" || fail "stable check after a beta: $(cat "$S/out.log")"
printf 'One\nTwo\nThree\n' >"$S/highlights.txt"
HIGHLIGHTS=$S/highlights.txt quiet scripts/release.sh 9.0.0 --only site
python3 - <<'EOF' || fail "stable site: wrong feed"
import json
feed = json.load(open("website/latest.json"))
assert feed["version"] == "9.0.0" and feed["highlights"] == ["One", "Two", "Three"], feed
assert "preview" not in feed.get("channels", {}), "the passed beta is still on the Preview channel"
EOF
echo "release.sh beta and promotion: ok"
