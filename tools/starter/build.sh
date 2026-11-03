#!/bin/sh
# SculptForge - open source digital sculpting
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Rebuilds assets/starter/*.sfproj.z (the starter meshes, core/StarterMeshes.h) from scratch:
#   tools/starter/build.sh <sculptforge-cli>
# Downloads the MakeHuman hm08 base mesh (CC0) at a pinned commit and checks its SHA-256 first
# (docs/starter-meshes.md), builds the OBJ cages, saves each as a project and compresses it.
set -eu
CLI=${1:?usage: tools/starter/build.sh <sculptforge-cli>}
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
WORK=${STARTER_WORK:-$ROOT/build/starter-meshes}
MH_COMMIT=a8bc2d54ff0ac92e78ff71431b1023eda42bf482
MH_SHA256=8e761e6624b8f54536409135d1636da63b32486a90d4897f84e121d144f6fb4c
mkdir -p "$WORK/makehuman" "$WORK/obj"
BASE="$WORK/makehuman/base.obj"
if [ ! -f "$BASE" ]; then
  curl -fsSL -o "$BASE" "https://raw.githubusercontent.com/makehumancommunity/makehuman/$MH_COMMIT/makehuman/data/3dobjs/base.obj"
fi
echo "$MH_SHA256  $BASE" | sha256sum -c -
python3 -I "$ROOT/tools/starter/build_starter_meshes.py" "$BASE" "$WORK/obj"
for obj in "$WORK"/obj/*.obj; do
  id=$(basename "$obj" .obj)
  "$CLI" -q -c "open_obj path=$obj; save_project path=$WORK/obj/$id.sfproj"
  python3 -I -c 'import sys, zlib; open(sys.argv[2], "wb").write(zlib.compress(open(sys.argv[1], "rb").read(), 9))' \
    "$WORK/obj/$id.sfproj" "$ROOT/assets/starter/$id.sfproj.z"
done
ls -l "$ROOT"/assets/starter/*.sfproj.z | awk '{ s += $5 } END { printf "assets/starter: %d files, %.2f MB\n", NR, s / 1e6 }'
