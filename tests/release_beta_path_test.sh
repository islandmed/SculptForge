#!/usr/bin/env bash
# SculptForge - open source digital sculpting
# SPDX-License-Identifier: GPL-3.0-or-later
#
# scripts/release.sh step_release for a beta, whole (ctest release_beta_path): a published members' beta with the asset
# list a real beta has (tests/data/beta_release_assets.txt, from 0.12.14-beta.1) passes every check of the step (the
# install-asset check, the asset floor, the sums) and ends with its Android APKs and SHA256SUMS-android.txt uploaded to
# the private repository. 0.12.13-beta.1 and 0.12.14-beta.1 each stopped at a different check of this step and needed the
# APKs attached by hand; a new stop fails here first. The same list as a stable release still stops.
# Stand-in gh and git repository in a sandbox; the functions come from scripts/release.sh itself.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
S=$(mktemp -d)
trap 'rm -rf "$S"' EXIT
fail() { echo "FAIL: $*" >&2; exit 1; }
mkdir -p "$S/bin" "$S/work" "$S/launch"
cat > "$S/bin/gh" <<'SH'
#!/bin/sh
echo "$*" >> "$GH_LOG"
case "$1 $2" in
  "release view") cat "$GH_ASSETS" ;;
  "run list") echo 4242 ;;
  "run view") case "$*" in *status*) echo completed ;; *) echo success ;; esac ;;
  "run download")
    while [ $# -gt 0 ]; do [ "$1" = -n ] && name=$2; [ "$1" = -D ] && dir=$2; shift; done
    mkdir -p "$dir"; echo apk > "$dir/SculptForge-9.9.9-android-${name#sculptforge-android-test-}-test.apk" ;;
  "release upload") echo "$*" >> "$GH_UPLOADS" ;;
esac
SH
chmod +x "$S/bin/gh"
export PATH=$S/bin:$PATH GH_LOG=$S/gh.log GH_UPLOADS=$S/uploads GH_ASSETS=$ROOT/tests/data/beta_release_assets.txt
export SF_LAUNCH_DIR=$S/launch
REPO=pub/app INSIDERS_REPO=pri/insiders
work=$S/work
sha256() { if command -v sha256sum >/dev/null; then sha256sum "$1" | cut -d' ' -f1; else shasum -a 256 "$1" | cut -d' ' -f1; fi; }
ok() { echo "ok: $*"; }
die() { echo "DIE: $*"; exit 1; } # as release.sh: the step stops there (inside the $( ) below)
resume() { echo "resume-$1"; }
gh_watch() { return 0; }
infra_retry() { return 1; }
run_failed_where() { echo "?"; }
for f in step_release release_ready min_assets release_body_headline release_apk; do
    grep -q "^$f()" "$ROOT/scripts/release.sh" || continue # (step_release says if one it calls is missing)
    # shellcheck disable=SC1090
    eval "$(sed -n "/^$f()/,/^}/p" "$ROOT/scripts/release.sh")" # not source <(...): macOS bash 3.2 cannot
done
git init -q "$S/repo" && (cd "$S/repo" && git -c user.name=t -c user.email=t@e commit -q --allow-empty -m beta && git tag v9.9.9-beta.1)
tagsha=$(git -C "$S/repo" rev-parse v9.9.9-beta.1)
git() { if [ "$1 $2" = "rev-parse v9.9.9-beta.1^{commit}" ]; then echo "$tagsha"; else command git "$@"; fi; }

# 1. the beta, the whole step: no stop, both APKs and their sums to the private repository only
V=9.9.9-beta.1 T=v9.9.9-beta.1 BASE=9.9.9 PRE=beta.1 RELEASE_REPO=pri/insiders
out=$(cd "$ROOT" && step_release 2>&1) || fail "the beta stopped in step_release: $out"
[ -s "$S/uploads" ] || fail "no APKs uploaded: $out"
grep -q -- '-R pri/insiders' "$S/uploads" || fail "not to the Insiders repository: $(cat "$S/uploads")"
grep -q 'arm64-v8a' "$S/uploads" && grep -q 'x86_64' "$S/uploads" && grep -q 'SHA256SUMS-android.txt' "$S/uploads" ||
    fail "files: $(cat "$S/uploads")"
if grep -q 'pub/app' "$S/uploads"; then fail "touched the public repository"; fi

# 2. the same list as a stable release: stops (a stable release has the install names and more assets)
rm -f "$S/uploads"; rm -rf "$work/apk"
V=9.9.9 T=v9.9.9 PRE= RELEASE_REPO=pub/app
out=$(cd "$ROOT" && step_release 2>&1) && fail "a stable release with a beta's asset list passed: $out"
echo "$out" | grep -q '^DIE:' || fail "stable: no stop: $out"
[ ! -e "$S/uploads" ] || fail "uploaded for a stable release"
echo "release_beta_path: ok"
