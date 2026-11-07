#!/bin/sh
# Checks that an AppDir (or an extracted AppImage) starts on 4, 16 and 64 KiB memory pages.
#
#   packaging/linux/check-page-size.sh APPDIR ARCH
#
# 1. No bundled jemalloc: it is built for one page size and aborts at start on bigger ones
#    ("<jemalloc>: Unsupported system page size"); glibc malloc has no such limit.
# 2. On aarch64 every ELF file's LOAD segments are aligned to 64 KiB: a library linked with
#    -z max-page-size=4096 cannot be mapped on 16 KiB (Asahi, Apple Silicon) or 64 KiB kernels.
#    x86_64 has 4 KiB pages only.
# That message on Asahi from the x86_64 AppImage is FEX-Emu's own jemalloc (run x86 programs in muvm there).
set -eu
[ $# -eq 2 ] || { echo "usage: $0 APPDIR ARCH" >&2; exit 2; }
APPDIR=$1
ARCH=$2
bad=0
for f in $(find "$APPDIR" -type f \( -path '*/bin/*' -o -name '*.so' -o -name '*.so.*' \)); do
  head -c 4 "$f" | grep -q 'ELF' || continue
  if grep -q -a '<jemalloc>' "$f"; then
    echo "$f: bundles jemalloc (aborts on other page sizes); use glibc malloc" >&2
    bad=1
  fi
  if [ "$ARCH" = aarch64 ]; then
    for a in $(readelf -lW "$f" | awk '$1 == "LOAD" {print $NF}' | sort -u); do
      if [ $((a)) -lt 65536 ]; then
        echo "$f: LOAD segment aligned to $a, needs 0x10000 for 16/64 KiB pages" >&2
        bad=1
      fi
    done
  fi
done
[ $bad -eq 0 ] || exit 1
echo "$APPDIR: starts on 4, 16 and 64 KiB pages"
