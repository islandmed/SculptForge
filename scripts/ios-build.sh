#!/usr/bin/env bash
# SculptForge - open source digital sculpting
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Builds the iPad app (docs/ipad.md) on a Mac with Xcode (GitHub's macOS runners: .github/workflows/ios.yml).
#
#   scripts/ios-build.sh sim [--tests] [--out DIR]   the simulator app (Ninja, arm64): DIR/SculptForge.app; --tests: the core suite too
#   scripts/ios-build.sh device [--build N] [--signed] [--out DIR]
#                                                   the device archive (Xcode, arm64): DIR/SculptForge.xcarchive, unsigned
#                                                   unless --signed (scripts/ios-testflight.sh: Xcode's cloud signing with the
#                                                   App Store Connect key at $SF_ASC_KEY_PATH, $ASC_KEY_ID, $ASC_ISSUER_ID and
#                                                   $APPLE_TEAM_ID, which only the owner adds as secrets; never printed)
#
# --build N is the build number (CFBundleVersion; every TestFlight upload needs a higher one, CI passes its run number).
# Prints the .app's or the archive's path last.
set -euo pipefail
cd "$(dirname "$0")/.."

kind=${1:-}
shift || true
out= tests=OFF build=1 signed=
while [ $# -gt 0 ]; do
  case $1 in
    --tests) tests=ON ;;
    --out) out=$2; shift ;;
    --build) build=$2; shift ;;
    --signed) signed=1 ;;
    *) echo "unknown option $1" >&2; exit 2 ;;
  esac
  shift
done
common=(-DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_BUILD_TYPE=Release -DSCULPTFORGE_USE_SYSTEM_SDL=OFF
        -DSCULPTFORGE_IOS_BUILD="$build" -DCMAKE_CXX_SCAN_FOR_MODULES=OFF)

case $kind in
  sim)
    out=${out:-build-ios-sim}
    cmake -S . -B "$out" -G Ninja "${common[@]}" -DCMAKE_OSX_SYSROOT=iphonesimulator -DSCULPTFORGE_IOS_TESTS=$tests \
      ${CMAKE_CXX_COMPILER_LAUNCHER:+-DCMAKE_C_COMPILER_LAUNCHER=$CMAKE_CXX_COMPILER_LAUNCHER -DCMAKE_CXX_COMPILER_LAUNCHER=$CMAKE_CXX_COMPILER_LAUNCHER}
    targets=(sculptforge)
    [ $tests = ON ] && targets+=(sculptforge_tests)
    cmake --build "$out" --target "${targets[@]}" -- -k 0 # (all compile errors in one CI run)
    echo "$PWD/$out/SculptForge.app"
    ;;
  device)
    out=${out:-build-ios-device}
    # (Xcode: the asset catalog's icon, and the archive xcodebuild signs and exports; static libraries stay out of it)
    cmake -S . -B "$out/xcode" -G Xcode "${common[@]}" -DCMAKE_OSX_SYSROOT=iphoneos -DCMAKE_XCODE_GENERATE_SCHEME=ON \
      -DCMAKE_XCODE_ATTRIBUTE_SKIP_INSTALL=YES
    sign=(CODE_SIGNING_ALLOWED=NO CODE_SIGNING_REQUIRED=NO CODE_SIGN_IDENTITY=)
    if [ -n "$signed" ]; then
      : "${SF_ASC_KEY_PATH:?} ${ASC_KEY_ID:?} ${ASC_ISSUER_ID:?} ${APPLE_TEAM_ID:?}"
      sign=(-allowProvisioningUpdates -authenticationKeyPath "$SF_ASC_KEY_PATH" -authenticationKeyID "$ASC_KEY_ID"
            -authenticationKeyIssuerID "$ASC_ISSUER_ID" DEVELOPMENT_TEAM="$APPLE_TEAM_ID" CODE_SIGN_STYLE=Automatic)
    fi
    xcodebuild -project "$out/xcode/SculptForge.xcodeproj" -scheme sculptforge -configuration Release -sdk iphoneos \
      -destination 'generic/platform=iOS' -archivePath "$out/SculptForge.xcarchive" -quiet archive "${sign[@]}"
    app=$(find "$out/SculptForge.xcarchive/Products/Applications" -maxdepth 1 -name '*.app' -print -quit)
    [ -n "$app" ] || { echo "the archive has no app in Products/Applications" >&2; exit 1; }
    echo "$PWD/$out/SculptForge.xcarchive"
    ;;
  *) echo "usage: scripts/ios-build.sh sim|device [--tests] [--build N] [--out DIR]" >&2; exit 2 ;;
esac
