#!/usr/bin/env bash
# SculptForge - open source digital sculpting
# SPDX-License-Identifier: GPL-3.0-or-later
#
# scripts/release.sh release_apk (ctest release_apk): a beta's Android APKs (arm64-v8a + x86_64 from the android.yml run of the
# beta's commit) go onto the PRIVATE Insiders release only. Nothing for a stable release or a public repository; a missing or
# failed run, a missing artifact or a failing upload is a warning and never fails the release; already uploaded: left alone.
# Stand-in gh and git repository in a sandbox.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
S=$(mktemp -d)
trap 'rm -rf "$S"' EXIT
fail() { echo "FAIL: $*" >&2; exit 1; }
mkdir -p "$S/bin" "$S/work"
cat > "$S/bin/gh" <<'SH'
#!/bin/sh
# gh run list ... | gh run view ID --json status|conclusion | gh run download ID -n NAME -D DIR | gh release upload T -R repo --clobber FILES
echo "$*" >> "$GH_LOG"
case "$1 $2" in
  "run list") echo "$GH_RUN" ;;
  "run view") case "$*" in *status*) echo completed ;; *) echo "$GH_CONC" ;; esac ;;
  "run download")
    [ -z "$GH_NODL" ] || exit 1
    while [ $# -gt 0 ]; do [ "$1" = -n ] && name=$2; [ "$1" = -D ] && dir=$2; shift; done
    mkdir -p "$dir"; echo apk > "$dir/SculptForge-9.9.9-android-${name#sculptforge-android-test-}-debug.apk" ;;
  "release upload") [ -z "$GH_UPFAIL" ] || exit 1; echo "$*" >> "$GH_UPLOADS" ;;
esac
SH
chmod +x "$S/bin/gh"
git init -q "$S/repo" && cd "$S/repo" && git -c user.name=t -c user.email=t@e commit -q --allow-empty -m beta && git tag v9.9.9-beta.3
export PATH=$S/bin:$PATH GH_LOG=$S/gh.log GH_UPLOADS=$S/uploads
REPO=pub/app INSIDERS_REPO=pri/insiders
V=9.9.9-beta.3 T=v9.9.9-beta.3 PRE=beta.3 RELEASE_REPO=pri/insiders
work=$S/work
sha256() { if command -v sha256sum >/dev/null; then sha256sum "$1" | cut -d' ' -f1; else shasum -a 256 "$1" | cut -d' ' -f1; fi; }
ok() { echo "ok: $*"; }
resume() { echo "resume-$1"; }
gh_watch() { return 0; }
# shellcheck disable=SC1090
eval "$(sed -n '/^release_apk()/,/^}/p' "$ROOT/scripts/release.sh")" # not source <(...): macOS bash 3.2 cannot
reset() {
    rm -rf "$work/apk" "$S/gh.log" "$S/uploads"
    : > "$S/gh.log"
    printf 'SHA256SUMS.txt\nSculptForge-9.9.9-linux.tar.gz\n' > "$work/assets"
    export GH_RUN=4242 GH_CONC=success GH_NODL= GH_UPFAIL=
}

# 1. the beta: both APKs and their sums, to the private repository only
reset; out=$(release_apk 2>&1) || fail "failed: $out"
[ -s "$S/uploads" ] || fail "nothing uploaded: $out"
grep -q -- '-R pri/insiders' "$S/uploads" || fail "not to the Insiders repository: $(cat "$S/uploads")"
grep -q 'arm64-v8a' "$S/uploads" && grep -q 'x86_64' "$S/uploads" && grep -q 'SHA256SUMS-android.txt' "$S/uploads" || fail "files: $(cat "$S/uploads")"
if grep -q 'pub/app' "$S/uploads"; then fail "touched the public repository"; fi
grep -qF 'android.yml' "$S/gh.log" || fail "did not look for the android.yml run"
grep -q 'SculptForge-9.9.9-android-arm64-v8a-debug.apk$' "$work/apk/SHA256SUMS-android.txt" || fail "sums: $(cat "$work/apk/SHA256SUMS-android.txt")"

# 2. a stable release, or a beta that publishes to the public repository: nothing, no gh call at all
reset; PRE=; release_apk >/dev/null || fail "failed for a stable release"; PRE=beta.3
[ ! -s "$S/gh.log" ] || fail "gh called for a stable release"
reset; RELEASE_REPO=pub/app; release_apk >/dev/null || fail "failed for the public repository"; RELEASE_REPO=pri/insiders
[ ! -s "$S/gh.log" ] || fail "gh called for a public release"

# 3. no run for the commit: a warning, no upload, success
reset; GH_RUN=; out=$(release_apk 2>&1) || fail "failed without a run"
echo "$out" | grep -q WARNING || fail "no warning without a run: $out"
[ ! -e "$S/uploads" ] || fail "uploaded without a run"

# 4. the run failed: warning, no upload
reset; GH_CONC=failure; out=$(release_apk 2>&1) || fail "failed with a red run"
echo "$out" | grep -q WARNING || fail "no warning for a red run"
[ ! -e "$S/uploads" ] || fail "uploaded from a red run"

# 5. artifacts expired: warning, no upload
reset; GH_NODL=1; out=$(release_apk 2>&1) || fail "failed with expired artifacts"
echo "$out" | grep -q WARNING || fail "no warning for missing artifacts"
[ ! -e "$S/uploads" ] || fail "uploaded without any file"

# 6. the upload fails: warning, still success
reset; GH_UPFAIL=1; out=$(release_apk 2>&1) || fail "failed when the upload failed"
echo "$out" | grep -q 'WARNING.*upload' || fail "no warning for a failed upload: $out"

# 7. already there: left alone
reset; echo 'SculptForge-9.9.9-android-arm64-v8a-debug.apk' >> "$work/assets"
release_apk >/dev/null || fail "failed when already uploaded"
[ ! -s "$S/gh.log" ] || fail "looked again though the APKs are on the release"

# 8. release_ready (just before release_apk): a beta's files carry the base version's names, which the desktop install
#    scripts' check would refuse; a beta skips that check (0.12.13-beta.1 stopped there), a stable release still runs it.
die() { echo "DIE: $*"; return 1; }
eval "$(sed -n '/^release_ready()/,/^}/p' "$ROOT/scripts/release.sh")"
cat > "$S/bin/gh" <<'SH'
#!/bin/sh
case "$1 $2" in "release view") printf 'SHA256SUMS.txt\nSculptForge-9.9.9-x86_64.AppImage\n' ;; esac
SH
out=$(cd "$ROOT" && release_ready 2>&1) || fail "a beta stopped in release_ready: $out"
out=$(cd "$ROOT" && PRE= V=9.9.9 release_ready 2>&1) && fail "a stable release with missing install assets passed: $out"
echo "$out" | grep -q 'DIE: install/install.sh' || fail "stable: not the install-asset check: $out"

# 9. the asset floor: a beta's 28 assets are a finished beta, a stable release still needs 30 (0.12.14-beta.1 stopped at 30)
eval "$(sed -n '/^min_assets()/p' "$ROOT/scripts/release.sh")"
[ "$(PRE=beta.1 min_assets)" -le 28 ] || fail "a beta's 28 assets are below its floor: $(PRE=beta.1 min_assets)"
[ "$(PRE= min_assets)" -eq 30 ] || fail "stable floor is not 30: $(PRE= min_assets)"
echo "release_apk: ok"
