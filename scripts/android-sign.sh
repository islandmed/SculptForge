#!/usr/bin/env bash
# SculptForge - open source digital sculpting
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Signs an unsigned release APK (scripts/android-build.sh, or CI's artifact) with our Android release key, on the
# release machine only. The key is made here the first time and never leaves it: never committed, never printed.
#
#   scripts/android-sign.sh SculptForge-X.Y.Z-android-unsigned.apk     -> SculptForge-X.Y.Z-android.apk beside it
#
# Needs Java 17 (keytool) and the SDK's build-tools (zipalign, apksigner) under ANDROID_HOME. The keystore password
# comes from SF_ANDROID_KEY_PASS, else the file ~/.config/sculptforge/android-release.pass (0600, made with the key).
set -euo pipefail
apk=${1:?usage: android-sign.sh APK}
: "${ANDROID_HOME:?ANDROID_HOME is not set (the Android SDK)}"
tools=$ANDROID_HOME/build-tools/$(ls "$ANDROID_HOME/build-tools" | sort -V | tail -1)
dir=${XDG_CONFIG_HOME:-$HOME/.config}/sculptforge
ks=$dir/android-release.jks pass=$dir/android-release.pass
umask 077
mkdir -p "$dir"
if [ ! -f "$ks" ]; then
  [ -f "$pass" ] || head -c 32 /dev/urandom | base64 | tr -d '/+=' >"$pass"
  keytool -genkeypair -keystore "$ks" -alias sculptforge -keyalg RSA -keysize 4096 -validity 10000 \
    -dname "CN=SculptForge, O=SculptForge" -storepass:file "$pass" -keypass:file "$pass" >/dev/null
  chmod 600 "$ks" "$pass"
  echo "Made the Android release key $ks (back it up with the others: losing it means no updates over old installs)"
fi
export SF_ANDROID_KEY_PASS=${SF_ANDROID_KEY_PASS:-$(cat "$pass")}
out=${apk%-unsigned.apk}.apk
[ "$out" != "$apk" ] || out=${apk%.apk}-signed.apk
"$tools/zipalign" -f -p 16 "$apk" "$out.aligned"
"$tools/apksigner" sign --ks "$ks" --ks-key-alias sculptforge --ks-pass env:SF_ANDROID_KEY_PASS \
  --key-pass env:SF_ANDROID_KEY_PASS --out "$out" "$out.aligned"
rm -f "$out.aligned" "$out.idsig"
"$tools/apksigner" verify "$out"
echo "$out"
