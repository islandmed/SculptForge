#!/bin/sh
# SculptForge - open source digital sculpting
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Rebuilds assets/insert-sets/creature-bones.obj.z, the built-in "Creature bones" insert set (#154), from scratch:
#   tools/creature-bones/build.sh <sculptforge-cli> [piece names...]
# Every piece is designed here (pieces.py: distance fields from simple forms, no outside meshes or scans): meshed
# with surface nets (sdf.py, pure Python), quad-remeshed by SculptForge itself (qremesh), then packed as one
# multi-object OBJ (quads kept; glTF would split them into triangles) and zlib-compressed. Naming pieces re-meshes
# only those (the rest come from the last run in $BONES_WORK). Python 3 only, a few minutes.
set -eu
CLI=${1:?usage: tools/creature-bones/build.sh <sculptforge-cli> [piece names...]}
shift
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
WORK=${BONES_WORK:-$ROOT/build/creature-bones}
mkdir -p "$WORK/raw" "$WORK/quad" "$ROOT/assets/insert-sets"
python3 -I "$ROOT/tools/creature-bones/build.py" raw "$WORK" "$@"
python3 -I "$ROOT/tools/creature-bones/build.py" script "$WORK" > "$WORK/qremesh.txt"
"$CLI" -q -f "$WORK/qremesh.txt"
python3 -I "$ROOT/tools/creature-bones/build.py" pack "$WORK" "$ROOT/assets/insert-sets/creature-bones.obj.z"
