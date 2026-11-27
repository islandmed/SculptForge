#!/usr/bin/env bash
# SculptForge - open source digital sculpting
# SPDX-License-Identifier: GPL-3.0-or-later
#
# scripts/release.sh release_apk (ctest release_apk): Android APKs (arm64-v8a + x86_64 from the android.yml run of the tagged
# commit) go onto a beta's release on the PRIVATE Insiders repository only, and for a stable release onto BOTH its public release
# and its members' release there (owner 11-27: free for everyone, members first). Each target is counted afterwards (both APKs +
# SHA256SUMS-android.txt, else loud). A missing or failed run (after one rerun), a missing artifact or a failing upload is a
# loud warning plus a marker (release.sh exits 1 at its end) and never stops the train; already uploaded: left alone.
# Stand-in gh and git repository in a sandbox.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
S=$(mktemp -d)
trap 'rm -rf "$S"' EXIT
fail() { echo "FAIL: $*" >&2; exit 1; }
mkdir -p "$S/bin" "$S/work" "$S/rel"
cat > "$S/bin/gh" <<'SH'
#!/bin/sh
# gh run list ... | gh run view ID --json status|conclusion | gh run download ID -n NAME -D DIR
# gh release upload T -R repo --clobber FILES (records the file names as that release's assets) | gh release view T -R repo [--json assets]
echo "$*" >> "$GH_LOG"
repo=; for a in "$@"; do [ "$prev" = -R ] && repo=$(echo "$a" | tr / _); prev=$a; done
case "$1 $2" in
  "run list") if [ -n "$GH_RUN" ]; then echo "$GH_RUN"; elif [ -e "$GH_DISPATCHED" ]; then echo "$GH_RUN_AFTER"; fi ;;
  "workflow run") [ -z "$GH_NODISPATCH" ] || exit 1; touch "$GH_DISPATCHED" ;;
  "run view") case "$*" in *status*) echo completed ;; *) if [ -e "$GH_RERUN" ]; then echo success; else echo "$GH_CONC"; fi ;; esac ;;
  "run download")
    [ -z "$GH_NODL" ] || exit 1
    while [ $# -gt 0 ]; do [ "$1" = -n ] && name=$2; [ "$1" = -D ] && dir=$2; shift; done
    mkdir -p "$dir"; echo apk > "$dir/SculptForge-9.9.9-android-${name#sculptforge-android-test-}-debug.apk" ;;
  "release upload")
    case "$GH_UPFAIL" in "") ;; all) exit 1 ;; *) [ "$repo" = "$GH_UPFAIL" ] && exit 1 ;; esac
    echo "$*" >> "$GH_UPLOADS"
    seen=; for a in "$@"; do
      if [ -n "$seen" ]; then [ -z "$GH_UPSHORT" ] || [ "$(basename "$a")" != SHA256SUMS-android.txt ] || continue; basename "$a" >> "$GH_REL/$repo"; fi
      if [ "$a" = --clobber ]; then seen=1; fi
    done ;;
  "release view") case "$*" in
      *--json*) cat "$GH_REL/$repo" 2>/dev/null || true ;;
      *) [ -n "$GH_IREL" ] || [ "$repo" != pri_insiders ] || exit 1 ;;
    esac ;;
  "release create") [ -z "$GH_CREATEFAIL" ] || exit 1; echo "$*" >> "$GH_CREATED"; : > "$GH_REL/pri_insiders" ;;
esac
SH
chmod +x "$S/bin/gh"
printf '#!/bin/sh\necho "$*" >> "$HERDR_LOG"\n' > "$S/bin/herdr"; chmod +x "$S/bin/herdr" # never the real sfmonitor
git init -q "$S/repo" && cd "$S/repo" && git -c user.name=t -c user.email=t@e commit -q --allow-empty -m beta && git tag v9.9.9-beta.3
export PATH=$S/bin:$PATH GH_LOG=$S/gh.log GH_UPLOADS=$S/uploads GH_CREATED=$S/created HERDR_LOG=$S/herdr.log GH_DISPATCHED=$S/dispatched GH_RERUN=$S/rerun GH_REL=$S/rel SF_APK_POLLS=2 SF_APK_POLL_S=0
REPO=pub/app INSIDERS_REPO=pri/insiders
V=9.9.9-beta.3 T=v9.9.9-beta.3 PRE=beta.3 RELEASE_REPO=pri/insiders
work=$S/work
sha256() { if command -v sha256sum >/dev/null; then sha256sum "$1" | cut -d' ' -f1; else shasum -a 256 "$1" | cut -d' ' -f1; fi; }
ok() { echo "ok: $*"; }
red() { echo "$*" >&2; }
resume() { echo "resume-$1"; }
gh_watch() { return 0; }
# shellcheck disable=SC1090
eval "$(sed -n '/^apk_missing()/,/^}/p;/^apk_lacks()/,/^}/p;/^release_apk()/,/^}/p' "$ROOT/scripts/release.sh")" # not source <(...): macOS bash 3.2 cannot
APKS='SculptForge-9.9.9-android-arm64-v8a-debug.apk
SculptForge-9.9.9-android-x86_64-debug.apk
SHA256SUMS-android.txt'
reset() {
    rm -rf "$work/apk" "$work/apk-missing" "$S/rerun" "$S/rerun.log" "$S/gh.log" "$S/uploads" "$S/created" "$S/herdr.log" "$S/dispatched" "$S/rel"
    mkdir -p "$S/rel"; : > "$S/gh.log"
    printf 'SHA256SUMS.txt\nSculptForge-9.9.9-linux.tar.gz\n' > "$work/assets"
    export GH_RUN=4242 GH_CONC=success GH_NODL= GH_UPFAIL= GH_UPSHORT= GH_IREL= GH_CREATEFAIL= GH_RUN_AFTER= GH_NODISPATCH=
}
stable() { V=9.9.9 T=v9.9.9-beta.3 PRE= RELEASE_REPO=pub/app; }
beta() { V=9.9.9-beta.3 T=v9.9.9-beta.3 PRE=beta.3 RELEASE_REPO=pri/insiders; }
has() { grep -qx "$2" "$S/rel/$1" 2>/dev/null; }
complete() { has "$1" SculptForge-9.9.9-android-arm64-v8a-debug.apk && has "$1" SculptForge-9.9.9-android-x86_64-debug.apk && has "$1" SHA256SUMS-android.txt; }

# 1. the beta: both APKs and their sums, to the private repository only
reset; out=$(release_apk 2>&1) || fail "failed: $out"
[ -s "$S/uploads" ] || fail "nothing uploaded: $out"
grep -q -- '-R pri/insiders' "$S/uploads" || fail "not to the Insiders repository: $(cat "$S/uploads")"
grep -q 'arm64-v8a' "$S/uploads" && grep -q 'x86_64' "$S/uploads" && grep -q 'SHA256SUMS-android.txt' "$S/uploads" || fail "files: $(cat "$S/uploads")"
if grep -q 'pub/app' "$S/uploads"; then fail "a beta touched the public repository"; fi
grep -qF 'android.yml' "$S/gh.log" || fail "did not look for the android.yml run"
complete pri_insiders || fail "beta: Insiders release incomplete: $(cat "$S/rel/pri_insiders")"
grep -q 'SculptForge-9.9.9-android-arm64-v8a-debug.apk$' "$work/apk/SHA256SUMS-android.txt" || fail "sums: $(cat "$work/apk/SHA256SUMS-android.txt")"

# 2. a beta that publishes to the public repository: nothing, no gh call at all
reset; RELEASE_REPO=pub/app; release_apk >/dev/null || fail "failed for the public repository"; beta
[ ! -s "$S/gh.log" ] || fail "gh called for a beta on the public repository"
# 2b. a stable release (free for everyone, members first): the APKs and sums land on the public release AND on its own release on
# the Insiders repository (made when missing), and sfmonitor is asked to post in #members
reset; stable; out=$(release_apk 2>&1) || fail "failed for a stable release: $out"
grep -q -- '-R pri/insiders --title SculptForge 9.9.9 for Android (members)' "$S/created" || fail "no members' release made: $(cat "$S/created" 2>/dev/null)"
complete pub_app && complete pri_insiders || fail "stable: public: $(cat "$S/rel/pub_app" 2>/dev/null) | insiders: $(cat "$S/rel/pri_insiders" 2>/dev/null)"
[ ! -s "$work/apk-missing" ] || fail "stable: marker after a clean upload: $(cat "$work/apk-missing")"
wait; grep -q 'sfmonitor post in #members' "$S/herdr.log" || fail "no #members post asked: $(cat "$S/herdr.log" 2>/dev/null)"
# 2c. both releases have the APKs already: left alone (no run lookup, no upload, no post); only one has them: the other gets them
reset; stable; GH_IREL=1; printf '%s\n' "$APKS" | tee "$S/rel/pub_app" > "$S/rel/pri_insiders"
out=$(release_apk 2>&1) || fail "failed: $out"
[ ! -e "$S/created" ] && [ ! -e "$S/uploads" ] && ! grep -q 'run list' "$S/gh.log" || fail "stable: re-made or re-uploaded though both have them"
reset; stable; GH_IREL=1; printf '%s\n' "$APKS" > "$S/rel/pri_insiders"
out=$(release_apk 2>&1) || fail "failed: $out"
grep -q -- '-R pub/app' "$S/uploads" && ! grep -q -- '-R pri/insiders' "$S/uploads" && complete pub_app || fail "only the public release should have been filled: $(cat "$S/uploads" 2>/dev/null)"
# 2d. creating the members' release fails: loud, marked; the public release still gets its APKs
reset; stable; GH_CREATEFAIL=1; out=$(release_apk 2>&1) || fail "a failed create failed the release: $out"
echo "$out" | grep -q WARNING && [ -s "$work/apk-missing" ] || fail "stable create failure: $out"
complete pub_app || fail "public release not filled after a members' create failure"
# 2e. the upload to the public release fails: the members' release is still filled, the count names the public release, loud
reset; stable; GH_UPFAIL=pub_app; out=$(release_apk 2>&1) || fail "failed when the public upload failed: $out"
complete pri_insiders && ! complete pub_app || fail "public upload failure: wrong state: $(cat "$S/rel/pub_app" 2>/dev/null)"
echo "$out" | grep -q 'WARNING.*pub/app' && grep -q 'pub/app' "$work/apk-missing" || fail "no loud warning for the public release: $out"
# 2f. the count: an upload that worked but left a release short (no SHA256SUMS-android.txt on it) is caught by name
reset; stable; GH_UPSHORT=1; out=$(release_apk 2>&1) || fail "failed with a short release: $out"
echo "$out" | grep -q 'pub/app is missing SHA256SUMS-android.txt' && echo "$out" | grep -q 'pri/insiders is missing SHA256SUMS-android.txt' || fail "count did not name the short releases: $out"
[ -s "$work/apk-missing" ] || fail "short release: no marker"
beta

# 3. no run for the commit: android.yml is started on the tag and its APKs land (0.13.1 shipped an empty members' release)
reset; GH_RUN= GH_RUN_AFTER=5151; out=$(release_apk 2>&1) || fail "failed without a run: $out"
grep -q 'workflow run android.yml -R pub/app --ref v9.9.9-beta.3' "$S/gh.log" || fail "android.yml not started: $(cat "$S/gh.log")"
grep -q -- '-R pri/insiders' "$S/uploads" || fail "no APKs after starting the run: $out"
# 3b. no run appears even after starting one (or the start fails): a warning, no upload, success
reset; GH_RUN=; out=$(release_apk 2>&1) || fail "failed when no run appeared"
echo "$out" | grep -q WARNING || fail "no warning without a run: $out"
[ ! -e "$S/uploads" ] || fail "uploaded without a run"
reset; GH_RUN= GH_NODISPATCH=1; out=$(release_apk 2>&1) || fail "failed when the start failed"
echo "$out" | grep -q WARNING && [ ! -e "$S/uploads" ] || fail "start failure: $out"

# 4. the run failed (no infra_retry here): a loud warning, the marker release.sh turns into an exit code, no upload
reset; GH_CONC=failure; out=$(release_apk 2>&1) || fail "failed with a red run"
echo "$out" | grep -q WARNING || fail "no warning for a red run"
[ -s "$work/apk-missing" ] || fail "a red run left no apk-missing marker"
[ ! -e "$S/uploads" ] || fail "uploaded from a red run"
# 4b. a red run gets one rerun of its failed jobs (0.14.1, 0.14.2: Maven Central 429): when the rerun is green the APKs land, no marker
infra_retry() { echo "$1" >> "$S/rerun.log"; [ -z "$RERUN_FIXES" ] || touch "$S/rerun"; }
reset; GH_CONC=failure; RERUN_FIXES=1; out=$(release_apk 2>&1) || fail "failed with a red run that a rerun fixes: $out"
[ "$(cat "$S/rerun.log")" = 4242 ] || fail "the red run was not rerun once: $(cat "$S/rerun.log" 2>/dev/null)"
[ -s "$S/uploads" ] && [ ! -s "$work/apk-missing" ] || fail "rerun green: no upload or a marker: $out"
# 4c. still red after the rerun: loud, marked, no upload (the train exits 1 at its end)
reset; GH_CONC=failure; RERUN_FIXES=; out=$(release_apk 2>&1) || fail "failed with a red run after its rerun"
echo "$out" | grep -q 'WARNING.*after its rerun' && [ -s "$work/apk-missing" ] && [ ! -e "$S/uploads" ] || fail "red after rerun: $out"
# 4d. a cancelled run is never rerun
reset; GH_CONC=cancelled; RERUN_FIXES=1; out=$(release_apk 2>&1) || fail "failed with a cancelled run"
[ ! -e "$S/rerun.log" ] && [ -s "$work/apk-missing" ] || fail "cancelled run: $out"
unset -f infra_retry

# 5. artifacts expired: warning, no upload
reset; GH_NODL=1; out=$(release_apk 2>&1) || fail "failed with expired artifacts"
echo "$out" | grep -q WARNING || fail "no warning for missing artifacts"
[ ! -e "$S/uploads" ] || fail "uploaded without any file"

# 6. the upload fails: warning, still success
reset; GH_UPFAIL=all; out=$(release_apk 2>&1) || fail "failed when the upload failed"
echo "$out" | grep -q 'WARNING.*upload' || fail "no warning for a failed upload: $out"

# 7. already there: left alone
reset; printf '%s\n' "$APKS" > "$S/rel/pri_insiders"
release_apk >/dev/null || fail "failed when already uploaded"
[ ! -e "$S/uploads" ] && ! grep -q 'run list' "$S/gh.log" || fail "looked again though the APKs are on the release"

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
