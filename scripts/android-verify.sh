#!/usr/bin/env bash
# SculptForge - open source digital sculpting
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Checks that every given APK is signed with our permanent Android key: the signing certificate's SHA-256 must equal the
# pin in scripts/android-cert.sha256 (the one place the fingerprint lives; android.yml's signing job and release.sh run this).
#
#   scripts/android-verify.sh APK...        exit 0: all match; 1: a mismatch, an unsigned APK or no apksigner
#
# apksigner: $APKSIGNER, else the newest build-tools under ANDROID_HOME, else PATH.
set -euo pipefail
[ $# -gt 0 ] || { echo "usage: android-verify.sh APK..." >&2; exit 2; }
here=$(cd "$(dirname "$0")" && pwd)
pin=$(tr -d ' :\n\r' < "$here/android-cert.sha256" | tr 'A-F' 'a-f')
[ "${#pin}" -eq 64 ] || { echo "android-verify: scripts/android-cert.sha256 is not a SHA-256" >&2; exit 1; }
signer=${APKSIGNER:-}
if [ -z "$signer" ] && [ -n "${ANDROID_HOME:-}" ] && [ -d "$ANDROID_HOME/build-tools" ]; then
  signer=$ANDROID_HOME/build-tools/$(ls "$ANDROID_HOME/build-tools" | sort -V | tail -1)/apksigner
fi
[ -n "$signer" ] || signer=$(command -v apksigner || true)
[ -n "$signer" ] && [ -x "$signer" ] || { echo "android-verify: no apksigner (set ANDROID_HOME or APKSIGNER)" >&2; exit 1; }
rc=0
for apk in "$@"; do
  out=$("$signer" verify --print-certs "$apk" 2>&1) || { echo "android-verify: $apk does not verify: $out" >&2; rc=1; continue; }
  got=$(printf '%s\n' "$out" | sed -n 's/^Signer #[0-9]* certificate SHA-256 digest: *//p' | tr -d ' :' | tr 'A-F' 'a-f')
  [ -n "$got" ] || { echo "android-verify: $apk: no signer certificate in apksigner's output" >&2; rc=1; continue; }
  # (every signer must be ours: an APK with an extra signer is not ours)
  if [ "$(printf '%s\n' "$got" | grep -vxc "$pin")" -ne 0 ]; then
    echo "android-verify: $apk is NOT signed with the SculptForge Android key (certificate $(printf '%s\n' "$got" | head -1), expected $pin)" >&2
    rc=1
  else
    echo "ok: $(basename "$apk") signed with the SculptForge Android key"
  fi
done
exit $rc
