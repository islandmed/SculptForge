#!/usr/bin/env bash
# SculptForge - the CPython the app's Python scripting ships with (SCULPTFORGE_PYTHON_RUNTIME, cmake/PythonRuntime.cmake).
# SPDX-License-Identifier: GPL-3.0-or-later
#
#   tools/ci/python-runtime.sh linux|windows|macos OUT_DIR    prints OUT_DIR/python, the runtime to configure with
#
# python-build-standalone's install_only_stripped CPython, pinned by hash (the release and version the VFX packages
# use). macOS gets one universal runtime: the arm64 and x86_64 builds merged file by file with lipo, its library
# named @rpath/libpython3.13.dylib so the host finds it in the bundle.
set -euo pipefail
platform=$1 out=$2
release=20251014 version=3.13.9
here=$(cd "$(dirname "$0")" && pwd)
fetch() { # triple sha256 -> the archive (tools/ci/fetch.sh keeps it by hash)
  "$here/fetch.sh" "https://github.com/astral-sh/python-build-standalone/releases/download/$release/cpython-$version%2B$release-$1-install_only_stripped.tar.gz" "$2"
}
unpack() { mkdir -p "$2" && tar xzf "$1" -C "$2"; } # -> $2/python
rm -rf "$out" && mkdir -p "$out"
case $platform in
  linux)
    case $(uname -m) in
      x86_64) unpack "$(fetch x86_64-unknown-linux-gnu c0ccda275948c79996e46993c2c5476ff5cca606dee530f1dea712179131b348)" "$out" ;;
      aarch64) unpack "$(fetch aarch64-unknown-linux-gnu baa3d107d17e4328448e30c3c9c83cca0eb41ca7a37c10982e14d46a5c3db07d)" "$out" ;;
      *) echo "python-runtime.sh: no runtime for $(uname -m)" >&2; exit 1 ;;
    esac ;;
  windows)
    unpack "$(fetch x86_64-pc-windows-msvc 76e0a9749c4deeb975a4b6b36d54be4e43f0c2a4c654bedab5d2e4d62dbc3006)" "$out" ;;
  macos)
    unpack "$(fetch aarch64-apple-darwin 52721745b0fa3196e4d0381fa5c06dda1d54343b90d49d90c3bba52d1171bd98)" "$out/arm64"
    unpack "$(fetch x86_64-apple-darwin 7c33b153a69c6255e6f2659cf39738f316b03969d6230d7bc47c73b7fde9a0d4)" "$out/x86_64"
    mv "$out/arm64/python" "$out/python"
    # Every Mach-O file of the arm64 tree becomes universal with its x86_64 twin (same layout in both builds).
    (cd "$out/python" && find . -type f \( -name '*.dylib' -o -name '*.so' -o -path './bin/*' \) -print0) |
      while IFS= read -r -d '' f; do
        twin="$out/x86_64/python/$f"
        if file "$out/python/$f" | grep -q 'Mach-O' && [ -f "$twin" ]; then
          lipo -create "$out/python/$f" "$twin" -output "$out/python/$f.universal" && mv "$out/python/$f.universal" "$out/python/$f"
        fi
      done
    lib=$(cd "$out/python/lib" && ls libpython3*.dylib | head -1)
    install_name_tool -id "@rpath/$lib" "$out/python/lib/$lib"
    rm -rf "$out/arm64" "$out/x86_64" ;;
  *) echo "usage: python-runtime.sh linux|windows|macos OUT_DIR" >&2; exit 2 ;;
esac
echo "$out/python"
