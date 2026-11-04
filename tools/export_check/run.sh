#!/usr/bin/env bash
# Checks SculptForge's textured exports with other tools than our own loaders:
#   glTF/GLB  Khronos glTF validator (npm gltf-validator)
#   USD/USDZ  OpenUSD's validation framework (pip usd-core)
#   FBX       ufbx (third_party/ufbx) in strict mode: textures, materials, UVs
#   all       the texture is sampled the right way up: a checker texture is sampled through each format's UVs
#             (OBJ/USD/FBX: v up, glTF: v down) and compared with SculptForge's own sampling (a PLY of the
#             vertex paint made from the same texture).
# Usage: tools/export_check/run.sh BUILD_DIR [WORK_DIR]   (needs node/npm, cc, and uv or python3 -m venv)
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/../.." && pwd)"
build="$(cd "${1:?usage: run.sh BUILD_DIR [WORK_DIR]}" && pwd)"
work="${2:-$(mktemp -d)}"
mkdir -p "$work/tools" "$work/out"
cli="$build/sculptforge-cli"

cd "$work/tools"
[ -d node_modules/gltf-validator ] || { npm init -y >/dev/null; npm install --silent gltf-validator >/dev/null; }
if [ ! -x venv/bin/python ]; then
    if command -v uv >/dev/null; then uv venv -q venv && uv pip install -q --python venv/bin/python usd-core numpy pillow
    else python3 -m venv venv && venv/bin/pip install -q usd-core numpy pillow; fi
fi
cc -O2 -I "$root/third_party/ufbx" -o fbxdump "$here/fbxdump.c" "$root/third_party/ufbx/ufbx.c" -lm
py="$work/tools/venv/bin/python"

out="$work/out"
"$py" - "$out/checker.png" <<'PY'
import sys, colorsys
from PIL import Image
im = Image.new('RGB', (1024, 1024)); px = im.load()
for y in range(1024):
    for x in range(1024):
        i = (y // 256) * 4 + x // 256
        r, g, b = colorsys.hsv_to_rgb(i / 16.0, 1.0 if i % 2 == 0 else 0.6, 1.0 if i < 8 else 0.55)
        px[x, y] = (int(r * 255), int(g * 255), int(b * 255))
im.save(sys.argv[1])
PY
"$cli" -q -c "new_sphere subdivisions=4; uv_auto; texture_import path=$out/checker.png; vertexpaint_from_texture; \
export_ply path=$out/truth.ply; save_obj path=$out/s.obj; export_glb path=$out/s.glb; export_usd path=$out/s.usda; \
export_fbx path=$out/s.fbx; layer_add name=Bulge; part_append primitive=sphere; uv_auto; texture_create size=1024; \
export_glb path=$out/scene.glb; export_usd path=$out/scene.usdz; export_fbx path=$out/scene.fbx lods=2"

echo "== glTF validator";  for f in "$out"/s.glb "$out"/scene.glb; do NODE_PATH="$work/tools/node_modules" node "$here/validate_gltf.js" "$f" | head -6; done
echo "== OpenUSD";         for f in "$out"/s.usda "$out"/scene.usdz; do "$py" "$here/usdcheck.py" "$f" | head -1; done
echo "== ufbx (strict)";   ./fbxdump "$out/scene.fbx" 2>&1 >/dev/null | grep -E 'WARNING|mesh|video' || true
echo "== texture orientation (match % per format)"
"$py" "$here/orient.py" "$out" "$work/tools/fbxdump"
