#!/usr/bin/env bash
# Builds SculptForge for Windows with llvm-mingw and runs the whole test suite (unit, Python-driven and UI
# tests) under Wine, so Windows-only breakage shows up before a push. Needs wine, cmake, curl and python3;
# the toolchain (pinned below) is downloaded once into ~/.cache/sculptforge.
# SPDX-License-Identifier: GPL-3.0-or-later
#
#   tools/wintest.sh                 build build-win<arch>, run every test
#   tools/wintest.sh -R ui_mask_lasso  extra arguments go to ctest
#   tools/wintest.sh --x64           build for x64 (on an ARM machine: build only, for tools/winvm/winvm.sh)
#
# The Windows architecture follows the host: ARM64 on an ARM machine (Wine runs ARM64 Windows programs
# natively; its x64 emulation is too unreliable for tests), x64 on an x86 machine. This complements CI's
# MSVC builds and the Windows VM (tools/winvm/winvm.sh): it catches Windows API, path and file-sharing
# issues, not MSVC-only compiler problems.
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
LLVM_MINGW=20261006
CACHE=${XDG_CACHE_HOME:-$HOME/.cache}/sculptforge

case "$(uname -m)" in
  aarch64|arm64) HOST=aarch64; TARGET=aarch64; ARCH=arm64; PROCESSOR=ARM64 ;;
  x86_64)        HOST=x86_64;  TARGET=x86_64;  ARCH=x64;   PROCESSOR=AMD64 ;;
  *) echo "wintest: unsupported host $(uname -m)" >&2; exit 1 ;;
esac
RUN_TESTS=1
if [ "${1:-}" = --x64 ]; then
  shift
  [ "$HOST" = x86_64 ] || RUN_TESTS=0
  TARGET=x86_64; ARCH=x64; PROCESSOR=AMD64
fi

[ $RUN_TESTS = 0 ] || command -v wine >/dev/null || { echo "wintest: install wine first" >&2; exit 1; }

TOOLCHAIN=$CACHE/llvm-mingw-$LLVM_MINGW-ucrt-ubuntu-22.04-$HOST
if [ ! -x "$TOOLCHAIN/bin/$TARGET-w64-mingw32-clang++" ]; then
  echo "wintest: downloading llvm-mingw $LLVM_MINGW"
  mkdir -p "$CACHE"
  curl -fsSL "https://github.com/mstorsjo/llvm-mingw/releases/download/$LLVM_MINGW/$(basename "$TOOLCHAIN").tar.xz" |
    tar -xJ -C "$CACHE"
fi

BUILD=$ROOT/build-win$ARCH
T=$TOOLCHAIN/bin/$TARGET-w64-mingw32
cmake -S "$ROOT" -B "$BUILD" -Wno-dev \
  -DCMAKE_SYSTEM_NAME=Windows -DCMAKE_SYSTEM_PROCESSOR=$PROCESSOR \
  -DCMAKE_C_COMPILER="$T-clang" -DCMAKE_CXX_COMPILER="$T-clang++" -DCMAKE_RC_COMPILER="$T-windres" \
  -DCMAKE_FIND_ROOT_PATH="$TOOLCHAIN/$TARGET-w64-mingw32" -DCMAKE_FIND_ROOT_PATH_MODE_PROGRAM=NEVER \
  -DCMAKE_FIND_ROOT_PATH_MODE_LIBRARY=ONLY -DCMAKE_FIND_ROOT_PATH_MODE_INCLUDE=ONLY \
  -DCMAKE_FIND_ROOT_PATH_MODE_PACKAGE=ONLY \
  -DCMAKE_EXE_LINKER_FLAGS=-static -DCMAKE_CROSSCOMPILING_EMULATOR=wine \
  -DCMAKE_BUILD_TYPE=Release -DSCULPTFORGE_USE_SYSTEM_SDL=OFF -DSCULPTFORGE_BUILD_PYTHON=OFF >/dev/null
cmake --build "$BUILD" --parallel
if [ $RUN_TESTS = 0 ]; then
  echo "wintest: built $BUILD; test it on Windows with: tools/winvm/winvm.sh test --build $BUILD"
  exit 0
fi

# A prefix of our own, so tests never touch the user's Wine setup.
export WINEPREFIX=$CACHE/wine-$ARCH WINEDEBUG=-all
[ -d "$WINEPREFIX" ] || wineboot -i >/dev/null 2>&1

RUN=(ctest --test-dir "$BUILD" --output-on-failure --parallel 4 "$@")
if [ -z "${DISPLAY:-}" ] && command -v xvfb-run >/dev/null; then
  RUN=(xvfb-run -a -s "-screen 0 1920x1080x24" "${RUN[@]}")
fi
"${RUN[@]}"
echo "wintest: UI screenshots in $BUILD/ui-shots"
