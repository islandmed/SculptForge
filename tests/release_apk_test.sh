#!/usr/bin/env bash
# SculptForge - open source digital sculpting
# SPDX-License-Identifier: GPL-3.0-or-later
#
# scripts/release.sh release_apk (ctest release_apk): a release's Android APKs (arm64-v8a + x86_64, signed with the permanent key,
# from the android.yml run of the tag) go onto the PRIVATE Insiders repository only: a beta's release, or a stable release's members-only one. Never a public repository; a missing or
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
  "run list") if [ -n "$GH_RUN" ]; then echo "$GH_RUN"; elif [ -e "$GH_DISPATCHED" ]; then echo "$GH_RUN_AFTER"; fi ;;
  "workflow run") [ -z "$GH_NODISPATCH" ] || exit 1; touch "$GH_DISPATCHED" ;;
  "run view") case "$*" in
      *jobs*) echo "${GH_WAITING:-0}" ;;
      *status*) if [ -n "$GH_WAITING" ]; then echo in_progress; else echo completed; fi ;;
      *) echo "$GH_CONC" ;; esac ;;
  "run download")
    [ -z "$GH_NODL" ] || exit 1
    while [ $# -gt 0 ]; do [ "$1" = -n ] && name=$2; [ "$1" = -D ] && dir=$2; shift; done
    [ "$name" = sculptforge-android-release ] || exit 1
    mkdir -p "$dir"; echo apk > "$dir/SculptForge-9.9.9-android-arm64-v8a.apk"; echo apk > "$dir/SculptForge-9.9.9-android-x86_64.apk" ;;
  "release upload") [ -z "$GH_UPFAIL" ] || exit 1; echo "$*" >> "$GH_UPLOADS" ;;
  "release view") case "$*" in *--json*) printf '%s' "$GH_IASSETS" ;; *) [ -n "$GH_IREL" ] || exit 1 ;; esac ;;
  "release create") [ -z "$GH_CREATEFAIL" ] || exit 1; echo "$*" >> "$GH_CREATED" ;;
esac
SH
chmod +x "$S/bin/gh"
# the pin check: a stand-in that passes unless GH_BADKEY is set (scripts/android-verify.sh itself is tested below)
printf '#!/bin/sh\n[ -z "$GH_BADKEY" ]\n' > "$S/verify"; chmod +x "$S/verify"; export SF_ANDROID_VERIFY=$S/verify
printf '#!/bin/sh\necho "$*" >> "$HERDR_LOG"\n' > "$S/bin/herdr"; chmod +x "$S/bin/herdr" # never the real sfmonitor
git init -q "$S/repo" && cd "$S/repo" && git -c user.name=t -c user.email=t@e commit -q --allow-empty -m beta && git tag v9.9.9-beta.3
export PATH=$S/bin:$PATH GH_LOG=$S/gh.log GH_UPLOADS=$S/uploads GH_CREATED=$S/created HERDR_LOG=$S/herdr.log GH_DISPATCHED=$S/dispatched SF_APK_POLLS=2 SF_APK_POLL_S=0 SF_APK_RUN_POLLS=2 SF_APK_RUN_S=0
REPO=pub/app INSIDERS_REPO=pri/insiders
V=9.9.9-beta.3 T=v9.9.9-beta.3 PRE=beta.3 RELEASE_REPO=pri/insiders
work=$S/work
sha256() { if command -v sha256sum >/dev/null; then sha256sum "$1" | cut -d' ' -f1; else shasum -a 256 "$1" | cut -d' ' -f1; fi; }
ok() { echo "ok: $*"; }
resume() { echo "resume-$1"; }
# shellcheck disable=SC1090
eval "$(sed -n '/^release_apk()/,/^}/p' "$ROOT/scripts/release.sh")" # not source <(...): macOS bash 3.2 cannot
reset() {
    rm -rf "$work/apk" "$S/gh.log" "$S/uploads" "$S/created" "$S/herdr.log" "$S/dispatched"
    : > "$S/gh.log"
    printf 'SHA256SUMS.txt\nSculptForge-9.9.9-linux.tar.gz\n' > "$work/assets"
    export GH_RUN=4242 GH_CONC=success GH_NODL= GH_UPFAIL= GH_IREL= GH_IASSETS= GH_CREATEFAIL= GH_RUN_AFTER= GH_NODISPATCH= GH_WAITING= GH_BADKEY=
}

# 1. the beta: both APKs and their sums, to the private repository only
reset; out=$(release_apk 2>&1) || fail "failed: $out"
[ -s "$S/uploads" ] || fail "nothing uploaded: $out"
grep -q -- '-R pri/insiders' "$S/uploads" || fail "not to the Insiders repository: $(cat "$S/uploads")"
grep -q 'arm64-v8a' "$S/uploads" && grep -q 'x86_64' "$S/uploads" && grep -q 'SHA256SUMS-android.txt' "$S/uploads" || fail "files: $(cat "$S/uploads")"
if grep -q 'pub/app' "$S/uploads"; then fail "touched the public repository"; fi
grep -qF 'android.yml' "$S/gh.log" || fail "did not look for the android.yml run"
grep -q 'SculptForge-9.9.9-android-arm64-v8a.apk$' "$work/apk/SHA256SUMS-android.txt" || fail "sums: $(cat "$work/apk/SHA256SUMS-android.txt")"

# 2. a beta that publishes to the public repository: nothing, no gh call at all
reset; RELEASE_REPO=pub/app; release_apk >/dev/null || fail "failed for the public repository"; RELEASE_REPO=pri/insiders
[ ! -s "$S/gh.log" ] || fail "gh called for a beta on the public repository"
# 2b. a stable release (Android stays members-only): its own release on the Insiders repository is made, the APKs go there,
# never to the public repository, and sfmonitor is asked to post in #members
reset; V=9.9.9 T=v9.9.9-beta.3 PRE= RELEASE_REPO=pub/app; out=$(release_apk 2>&1) || fail "failed for a stable release: $out"
grep -q -- '-R pri/insiders --title SculptForge 9.9.9 for Android (members)' "$S/created" || fail "no members' release made: $(cat "$S/created" 2>/dev/null)"
grep -q -- '-R pri/insiders' "$S/uploads" && grep -q 'SHA256SUMS-android.txt' "$S/uploads" || fail "stable: files: $(cat "$S/uploads" 2>/dev/null)"
if grep -q 'pub/app' "$S/uploads" "$S/created"; then fail "stable: touched the public repository"; fi
wait; grep -q 'sfmonitor post in #members' "$S/herdr.log" || fail "no #members post asked: $(cat "$S/herdr.log" 2>/dev/null)"
# 2c. the members' release exists with the APKs already: left alone; creating it fails: a warning, still success
reset; GH_IREL=1 GH_IASSETS='SculptForge-9.9.9-android-arm64-v8a.apk'; out=$(release_apk 2>&1) || fail "failed: $out"
[ ! -e "$S/created" ] && [ ! -e "$S/uploads" ] || fail "stable: re-made or re-uploaded"
reset; GH_CREATEFAIL=1; out=$(release_apk 2>&1) || fail "a failed create failed the release: $out"
echo "$out" | grep -q WARNING && [ ! -e "$S/uploads" ] || fail "stable create failure: $out"
V=9.9.9-beta.3 T=v9.9.9-beta.3 PRE=beta.3 RELEASE_REPO=pri/insiders

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

# 4. the run failed: warning, no upload
reset; GH_CONC=failure; out=$(release_apk 2>&1) || fail "failed with a red run"
echo "$out" | grep -q WARNING || fail "no warning for a red run"
[ ! -e "$S/uploads" ] || fail "uploaded from a red run"

# 4b. the signing job waits for the owner's approval: a warning that says so, no upload, no hang
reset; GH_WAITING=1; out=$(release_apk 2>&1) || fail "failed while waiting for the approval: $out"
echo "$out" | grep -q 'WARNING.*approval' && echo "$out" | grep -q 'resume-release' || fail "no approval warning: $out"
[ ! -e "$S/uploads" ] || fail "uploaded before the approval"
# 4c. the APKs are not signed with our key: nothing is uploaded, and it says so
reset; GH_BADKEY=1; out=$(release_apk 2>&1) || fail "a wrong key failed the whole release: $out"
echo "$out" | grep -q 'REFUSED.*not signed with the SculptForge Android key' || fail "no refusal for a wrong key: $out"
[ ! -e "$S/uploads" ] || fail "uploaded APKs signed with another key"

# 5. artifacts expired: warning, no upload
reset; GH_NODL=1; out=$(release_apk 2>&1) || fail "failed with expired artifacts"
echo "$out" | grep -q WARNING || fail "no warning for missing artifacts"
[ ! -e "$S/uploads" ] || fail "uploaded without any file"

# 6. the upload fails: warning, still success
reset; GH_UPFAIL=1; out=$(release_apk 2>&1) || fail "failed when the upload failed"
echo "$out" | grep -q 'WARNING.*upload' || fail "no warning for a failed upload: $out"

# 7. already there: left alone
reset; echo 'SculptForge-9.9.9-android-arm64-v8a.apk' >> "$work/assets"
release_apk >/dev/null || fail "failed when already uploaded"
[ ! -s "$S/gh.log" ] || fail "looked again though the APKs are on the release"

# 7b. scripts/android-verify.sh against a stand-in apksigner: our certificate passes; another one, an extra signer, an unsigned APK, no apksigner do not
cat > "$S/apksigner" <<'SH'
#!/bin/sh
[ "$1" = verify ] && [ "$2" = --print-certs ] || exit 9
case "$3" in
  *unsigned*) echo "DOES NOT VERIFY" >&2; exit 1 ;;
  *other*) echo "Signer #1 certificate SHA-256 digest: $(printf 'ab%.0s' $(seq 1 32))" ;;
  *two*) echo "Signer #1 certificate SHA-256 digest: $PIN"; echo "Signer #2 certificate SHA-256 digest: $(printf 'ab%.0s' $(seq 1 32))" ;;
  *) echo "Signer #1 certificate SHA-256 digest: $PIN" ;;
esac
SH
chmod +x "$S/apksigner"
export PIN; PIN=$(tr -d ' :\n' < "$ROOT/scripts/android-cert.sha256" | tr 'A-F' 'a-f')
[ "${#PIN}" -eq 64 ] || fail "scripts/android-cert.sha256 is not a SHA-256"
: > "$S/ours.apk"; : > "$S/other.apk"; : > "$S/two.apk"; : > "$S/unsigned.apk"
v() { APKSIGNER=$S/apksigner "$ROOT/scripts/android-verify.sh" "$@" 2>&1; }
v "$S/ours.apk" >/dev/null || fail "our key was refused"
v "$S/ours.apk" "$S/other.apk" >/dev/null && fail "another key passed"
v "$S/two.apk" >/dev/null && fail "an extra signer passed"
v "$S/unsigned.apk" >/dev/null && fail "an unsigned APK passed"
(PATH=/usr/bin:/bin ANDROID_HOME= APKSIGNER= "$ROOT/scripts/android-verify.sh" "$S/ours.apk" >/dev/null 2>&1) && fail "passed without apksigner"
{ v "$S/other.apk" || true; } | grep -q "NOT signed with the SculptForge Android key" || fail "no clear message on a mismatch"

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
