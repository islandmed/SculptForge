#!/usr/bin/env bash
# SculptForge - open source digital sculpting
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Builds the Android APK (android/README.md): the app's native code with the NDK (libmain.so + libSDL3.so per ABI),
# then the Gradle project around it.
#
#   scripts/android-build.sh [--abi arm64-v8a] [--abi x86_64] [--debug] [--unsigned] [--out DIR]
#
# The APK is signed with this machine's debug key (a tester build that installs as it is), or with --unsigned left
# for scripts/android-sign.sh.
#
# Needs ANDROID_HOME (the SDK, with build-tools and platform 35), an NDK (ANDROID_NDK_HOME, else the newest in the
# SDK), Java 17, CMake 3.24+, Ninja and Gradle 8.9+ on PATH. GitHub's ubuntu-24.04 runners have all of them.
# Prints the APK's path last.
set -euo pipefail
# Projects never go to Google's cloud backup unless the user chooses it (an explicit in-app backup, later).
if grep -q 'android:allowBackup="true"\|fullBackupContent\|dataExtractionRules' android/app/src/main/AndroidManifest.xml; then
  echo "AndroidManifest.xml: allowBackup must be false, with no backup rules (users' projects stay on the device)" >&2; exit 1
fi
cd "$(dirname "$0")/.."

abis=() type=release out=build-android unsigned=
while [ $# -gt 0 ]; do
  case $1 in
    --abi) abis+=("$2"); shift ;;
    --debug) type=debug ;;
    --unsigned) unsigned=-PsfUnsigned=1 ;;
    --out) out=$2; shift ;;
    *) echo "unknown option $1" >&2; exit 2 ;;
  esac
  shift
done
[ ${#abis[@]} -gt 0 ] || abis=(arm64-v8a)

: "${ANDROID_HOME:?ANDROID_HOME is not set (the Android SDK)}"
ndk=${ANDROID_NDK_HOME:-${ANDROID_NDK_LATEST_HOME:-$(ls -d "$ANDROID_HOME"/ndk/* 2>/dev/null | sort -V | tail -1)}}
[ -f "$ndk/build/cmake/android.toolchain.cmake" ] || { echo "No Android NDK (set ANDROID_NDK_HOME)" >&2; exit 1; }
version=$(sed -nE 's/^project\(SculptForge VERSION ([0-9.]+).*/\1/p' CMakeLists.txt)
IFS=. read -r ma mi pa <<<"$version"
code=$((ma * 1000000 + mi * 1000 + pa)) # 0.12.6 -> 12006: a newer version installs over an older one

cmake_type=Release
[ $type = debug ] && cmake_type=RelWithDebInfo
jni=$PWD/$out/jniLibs
rm -rf "$jni"
sdl=
for abi in "${abis[@]}"; do
  b=$out/native-$abi
  # (no C++ modules here: CMake would scan with the system's clang-scan-deps, which can't read the NDK's PCHs)
  cmake -S . -B "$b" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$ndk/build/cmake/android.toolchain.cmake" \
    -DANDROID_ABI="$abi" -DANDROID_PLATFORM=android-28 -DANDROID_STL=c++_static \
    -DCMAKE_BUILD_TYPE=$cmake_type -DSCULPTFORGE_USE_SYSTEM_SDL=OFF -DSCULPTFORGE_PLUGINS=OFF \
    -DCMAKE_CXX_SCAN_FOR_MODULES=OFF \
    ${CMAKE_CXX_COMPILER_LAUNCHER:+-DCMAKE_C_COMPILER_LAUNCHER=$CMAKE_CXX_COMPILER_LAUNCHER -DCMAKE_CXX_COMPILER_LAUNCHER=$CMAKE_CXX_COMPILER_LAUNCHER}
  cmake --build "$b" --target sculptforge -- -k 0 # (all compile errors in one CI run)
  mkdir -p "$jni/$abi"
  cp "$b/libmain.so" "$jni/$abi/"
  cp "$(find "$b" -name libSDL3.so -print -quit)" "$jni/$abi/"
  # Release: no debug sections in the APK (RelWithDebInfo keeps them, for ndk-stack).
  if [ $type = release ]; then "$ndk"/toolchains/llvm/prebuilt/*/bin/llvm-strip --strip-unneeded "$jni/$abi"/*.so; fi
  sdl=$(sed -nE 's/^SDL3_SOURCE_DIR:STATIC=(.*)/\1/p' "$b/CMakeCache.txt") # (SDL's own project() sets it)
done
[ -d "$sdl/android-project/app/src/main/java" ] || { echo "SDL's Java sources not found in '$sdl'" >&2; exit 1; }

# The launcher icon: the cube app icon (assets/branding, tools/brand/make_icons.py).
res=$PWD/$out/res
rm -rf "$res"
for d in mdpi:48 hdpi:64 xhdpi:128 xxhdpi:128 xxxhdpi:256; do
  mkdir -p "$res/mipmap-${d%%:*}"
  cp "assets/branding/sculptforge-${d##*:}.png" "$res/mipmap-${d%%:*}/ic_launcher.png"
done

if ! command -v gradle >/dev/null; then # (pinned; checked against Gradle's published SHA-256)
  g=gradle-8.11.1
  if [ ! -x "$out/$g/bin/gradle" ]; then
    curl -fsSL --retry 3 -o "$out/$g-bin.zip" "https://services.gradle.org/distributions/$g-bin.zip"
    echo "$(curl -fsSL --retry 3 "https://services.gradle.org/distributions/$g-bin.zip.sha256")  $out/$g-bin.zip" | sha256sum -c -
    (cd "$out" && unzip -q -o "$g-bin.zip")
  fi
  PATH=$PWD/$out/$g/bin:$PATH
fi
task=assembleRelease
[ $type = debug ] && task=assembleDebug
(cd android && gradle --no-daemon -q "$task" -PsfJniLibs="$jni" -PsfRes="$res" \
  -PsfSdlJava="$sdl/android-project/app/src/main/java" -PsfVersion="$version" -PsfVersionCode="$code" $unsigned)
apk=$(find android/app/build/outputs/apk/$type -name '*.apk' -print -quit)
suffix=-test
[ $type = debug ] && suffix=-debug
[ -n "$unsigned" ] && suffix=-unsigned
dest=$out/SculptForge-$version-android-$(IFS=+; echo "${abis[*]}")$suffix.apk
cp "$apk" "$dest"
echo "$dest"
