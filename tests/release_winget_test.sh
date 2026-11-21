#!/usr/bin/env bash
# SculptForge - open source digital sculpting
# SPDX-License-Identifier: GPL-3.0-or-later
#
# scripts/release.sh step_winget (ctest release_winget): the winget manifests of a release reach main (packaging/winget) and
# microsoft/winget-pkgs through our fork. While the first PR is open: one "Replace A with X" commit on its branch + a new title,
# nothing again on a second run. Once it merged: a branch sculptforge-X off upstream master with only the new version directory
# and a PR of its own (skipped when one exists or winget-pkgs has the version). SF_WINGET_DRY=1 changes nothing. Any error
# prints "winget: why" and the step still succeeds. Stand-in gh, local bare repositories for main, the fork and upstream.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
S=$(mktemp -d)
trap 'rm -rf "$S"' EXIT
fail() { echo "FAIL: $*" >&2; exit 1; }
export GIT_CONFIG_GLOBAL=/dev/null GIT_CONFIG_SYSTEM=/dev/null HOME=$S/home
mkdir -p "$S/bin" "$S/work" "$S/home" "$S/state"
G="git -c user.name=t -c user.email=t@e"
SHA=$(printf 'a1%.0s' $(seq 32))
WP=manifests/s/SculptForge/SculptForge

# main: packaging/winget at 0.13.10 and the renderer
git init -q --bare -b main "$S/origin.git"
git clone -q "$S/origin.git" "$S/seed" 2>/dev/null
mkdir -p "$S/seed/packaging" "$S/seed/tools/release"
cp -r "$ROOT/packaging/winget" "$S/seed/packaging/"; cp "$ROOT/tools/release/winget_manifest.py" "$S/seed/tools/release/"
(cd "$S/seed" && git add -A && $G commit -q -m seed && git push -q origin HEAD:main)
git clone -q "$S/origin.git" "$S/repo" 2>/dev/null
# the fork: master + the first PR's branch with the old version; upstream: master with other packages
git init -q --bare -b master "$S/fork.git"; git -C "$S/fork.git" config uploadpack.allowFilter true; git -C "$S/fork.git" config uploadpack.allowAnySHA1InWant true
git init -q --bare -b master "$S/up.git"; git -C "$S/up.git" config uploadpack.allowFilter true; git -C "$S/up.git" config uploadpack.allowAnySHA1InWant true
git clone -q "$S/up.git" "$S/upseed" 2>/dev/null
mkdir -p "$S/upseed/manifests/o/Other/Pkg/1.0" "$S/upseed/$WP/0.13.10"
echo x > "$S/upseed/manifests/o/Other/Pkg/1.0/a.yaml"; echo old > "$S/upseed/$WP/0.13.10/a.yaml"
(cd "$S/upseed" && git add -A && $G commit -q -m up && git push -q origin HEAD:master)
git clone -q "$S/up.git" "$S/forkseed" 2>/dev/null
(cd "$S/forkseed" && git remote add fork "$S/fork.git" && git push -q fork HEAD:master && rm -rf "$WP" && mkdir -p "$WP/0.9.3" && echo old > "$WP/0.9.3/a.yaml" &&
    git add -A && $G commit -q -m "first" && git push -q fork HEAD:sculptforge-0.9.2)
rm -rf "$S/forkseed" "$S/upseed" "$S/seed"

cat > "$S/bin/gh" <<'SH'
#!/bin/bash
echo "$*" >> "$GH_LOG"
st=$GH_STATE
case "$1 $2" in
  "release download") while [ $# -gt 0 ]; do [ "$1" = -D ] && dir=$2; shift; done; cp "$st/sums" "$dir/SHA256SUMS.txt" ;;
  "release view") echo "2031-02-03T01:41:38Z" ;;
  "pr view") printf '%s\t%s\t%s\n' "$(cat "$st/prstate")" sculptforge-0.9.2 "$(cat "$st/prtitle")" ;;
  "pr edit") while [ $# -gt 0 ]; do [ "$1" = --title ] && printf '%s' "$2" > "$st/prtitle"; shift; done ;;
  "pr list") cat "$st/prlist" 2>/dev/null || true ;;
  "pr create") echo "https://github.com/microsoft/winget-pkgs/pull/999" ;;
  "api repos/microsoft/winget-pkgs/contents/"*) [ -f "$st/upstream-has" ] ;;
esac
SH
chmod +x "$S/bin/gh"
export PATH=$S/bin:$PATH GH_LOG=$S/gh.log GH_STATE=$S/state
export SF_WINGET_DIR=$S/wg WINGET_FORK_URL=file://$S/fork.git WINGET_UPSTREAM_URL=file://$S/up.git
REPO=pub/app AUTHOR_NAME=islandmed AUTHOR_EMAIL=t@e work=$S/work
ok() { echo "ok: $*"; }
die() { echo "DIE: $*"; exit 1; }
resume() { echo "scripts/release.sh $V --from $1 --yes"; }
eval "$(sed -n '/^# lockretry CMD/,/^gitp()/p' "$ROOT/scripts/release.sh")" # not source <(...): macOS bash 3.2 cannot
eval "$(sed -n '/^# winget begin/,/^# winget end/p' "$ROOT/scripts/release.sh")"
cd "$S/repo"
setv() { V=$1 T=v$1; echo "$SHA  SculptForge-$1-windows-x86_64.exe" > "$GH_STATE/sums"; }
setpr() { printf '%s' "$1" > "$GH_STATE/prstate"; printf '%s' "$2" > "$GH_STATE/prtitle"; }
forkbranch() { git -C "$S/fork.git" log --format=%s "$1"; }
ncommits() { git -C "$S/fork.git" rev-list --count "$1"; } # (not wc -l: macOS pads it with spaces)
files() { git -C "$S/fork.git" ls-tree -r --name-only "$1" -- "$WP" | paste -sd' ' -; }
main_head() { git -C "$S/origin.git" rev-parse main; }
upper() { printf '%s' "$1" | tr a-f A-F; }

# 1. dry run: renders and diffs, touches nothing
setv 9.9.9; setpr OPEN "New package: SculptForge.SculptForge version 0.9.3"
before=$(main_head); fb=$(git -C "$S/fork.git" rev-parse sculptforge-0.9.2)
out=$(SF_WINGET_DRY=1 step_winget 2>&1) || fail "dry run: $out"
grep -q '^+PackageVersion: 9.9.9' <<<"$out" || fail "no diff of the new version: $out"
grep -q "InstallerSha256: $(upper "$SHA")" "$S/wg/dry/9.9.9/SculptForge.SculptForge.installer.yaml" || fail "dry manifests: no upper-case checksum"
[ "$(main_head)" = "$before" ] && [ "$(git -C "$S/fork.git" rev-parse sculptforge-0.9.2)" = "$fb" ] || fail "dry run pushed"
! grep -q 'pr edit' "$GH_LOG" || fail "dry run edited the PR"
[ ! -e "$S/wg/status-9.9.9.txt" ] || fail "dry run wrote a status"

# 2. the first PR is open: main gets the manifests, its branch one Replace commit, the PR the new title
out=$(step_winget 2>&1) || fail "open: $out"
[ "$(main_head)" != "$before" ] || fail "main didn't get packaging/winget at 9.9.9: $out"
git -C "$S/origin.git" show main:packaging/winget/SculptForge.SculptForge.yaml | grep -q 'PackageVersion: 9.9.9' || fail "main's manifest not at 9.9.9"
[ "$(forkbranch sculptforge-0.9.2 | head -1)" = "Replace 0.9.3 with 9.9.9" ] || fail "no replace commit: $(forkbranch sculptforge-0.9.2): $out"
[ "$(files sculptforge-0.9.2 | tr ' ' '\n' | sed "s#$WP/##; s#/.*##" | sort -u | paste -sd' ' -)" = 9.9.9 ] || fail "the branch's versions: $(files sculptforge-0.9.2)"
git -C "$S/fork.git" show "sculptforge-0.9.2:$WP/9.9.9/SculptForge.SculptForge.installer.yaml" | grep -q "InstallerSha256: $(upper "$SHA")" || fail "branch checksum"
[ "$(cat "$GH_STATE/prtitle")" = "New package: SculptForge.SculptForge version 9.9.9" ] || fail "title: $(cat "$GH_STATE/prtitle")"
grep -q 'OPEN' "$S/wg/status-9.9.9.txt" || fail "status: $(cat "$S/wg/status-9.9.9.txt")"
# again: nothing new anywhere
n=$(ncommits sculptforge-0.9.2); m=$(main_head); : > "$GH_LOG"
step_winget >/dev/null 2>&1 || fail "second run"
[ "$(ncommits sculptforge-0.9.2)" = "$n" ] && [ "$(main_head)" = "$m" ] && ! grep -q 'pr edit' "$GH_LOG" || fail "second run changed something"
git pull -q origin main 2>/dev/null

# 3. the first PR merged: a PR of its own for the next release
setv 9.9.10; setpr MERGED "New package: SculptForge.SculptForge version 9.9.9"; : > "$GH_LOG"
out=$(step_winget 2>&1) || fail "merged: $out"
[ "$(forkbranch sculptforge-9.9.10 | head -1)" = "New version: SculptForge.SculptForge version 9.9.10" ] || fail "no new branch: $out"
[ "$(files sculptforge-9.9.10)" = "$WP/0.13.10/a.yaml $WP/9.9.10/SculptForge.SculptForge.installer.yaml $WP/9.9.10/SculptForge.SculptForge.locale.en-US.yaml $WP/9.9.10/SculptForge.SculptForge.yaml" ] ||
    fail "the new branch's files: $(files sculptforge-9.9.10)"
[ "$(ncommits sculptforge-9.9.10)" = 2 ] || fail "the branch isn't upstream master + one commit: $(forkbranch sculptforge-9.9.10)"
grep -q -- '--head islandmed:sculptforge-9.9.10 --base master --title SculptForge.SculptForge version 9.9.10' "$GH_LOG" || fail "pr create: $(cat "$GH_LOG")"
grep -q 'pull/999' "$S/wg/status-9.9.10.txt" || fail "status: $(cat "$S/wg/status-9.9.10.txt")"
# a PR for it exists / winget-pkgs has it: no second PR
: > "$GH_LOG"; echo 777 > "$GH_STATE/prlist"
step_winget >/dev/null 2>&1
! grep -q 'pr create' "$GH_LOG" || fail "a second PR for 9.9.10"
grep -q '#777' "$S/wg/status-9.9.10.txt" || fail "status: $(cat "$S/wg/status-9.9.10.txt")"
rm "$GH_STATE/prlist"; touch "$GH_STATE/upstream-has"; : > "$GH_LOG"
step_winget >/dev/null 2>&1
! grep -q 'pr create' "$GH_LOG" || fail "a PR for a version winget-pkgs has"
grep -q 'winget-pkgs has 9.9.10' "$S/wg/status-9.9.10.txt" || fail "status: $(cat "$S/wg/status-9.9.10.txt")"
rm "$GH_STATE/upstream-has"

# 4. errors never stop the release: no checksum for the installer; a closed PR
setv 9.9.11; echo "$SHA  other.exe" > "$GH_STATE/sums"
out=$(step_winget 2>&1) || fail "a missing checksum stopped the step: $out"
grep -q '^winget: SculptForge-9.9.11-windows-x86_64.exe has no checksum' <<<"$out" && grep -q -- '--only winget' <<<"$out" || fail "message: $out"
grep -q 'NOT updated' "$S/wg/status-9.9.11.txt" || fail "status: $(cat "$S/wg/status-9.9.11.txt")"
setv 9.9.12; setpr CLOSED "x"
out=$(step_winget 2>&1) || fail "a closed PR stopped the step: $out"
grep -q 'is CLOSED' <<<"$out" || fail "closed message: $out"
echo "release_winget: ok"
