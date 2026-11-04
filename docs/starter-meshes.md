# Starter meshes: provenance

The starter meshes (File > New from Starter Mesh, Library > Tool > Starter meshes, `new_starter_mesh`;
src/core/StarterMeshes.h) are the base cages in `assets/starter/*.sfproj.z`: SculptForge projects, zlib-compressed,
embedded in the program. They open with subdivision levels added (Divide), so the files stay small (0.8 MB for the
set; the budget is 15 MB, checked by `starter_meshes_set_and_budget`).

**Licensing rule:** only meshes made by our own scripts, or CC0 / public-domain sources recorded here with URL,
licence and date. Never ZBrush assets or output, nothing scraped.

## Sources

| Mesh | Source | Licence | How it is made |
|---|---|---|---|
| Human Head | MakeHuman hm08 base mesh | CC0 1.0 | The body above the neck |
| Bust | MakeHuman hm08 base mesh | CC0 1.0 | Above the mid chest; arms cut a third of the way down the upper arm |
| Full Body | MakeHuman hm08 base mesh | CC0 1.0 | The whole "body" group (helpers and joints dropped), A-pose |
| Hand | MakeHuman hm08 base mesh | CC0 1.0 | The left hand past the wrist, stood up (fingers +Y, palm +Z) |
| Foot | MakeHuman hm08 base mesh | CC0 1.0 | The left foot below 9 cm over the ankle |
| Ear | MakeHuman hm08 base mesh | CC0 1.0 | The left ear and the skin round it, turned to face +Z |
| Stylised Head | MakeHuman hm08 base mesh | CC0 1.0 | The Human Head through a scripted space warp (cranium, eyes, nose, jaw) |
| Torso | MakeHuman hm08 base mesh | CC0 1.0 | Neck to upper thighs, arms off at the shoulder |
| Eye | SculptForge (generated) | CC0 1.0 | Surface of revolution on cube topology with a cornea bulge |
| Creature Head | SculptForge (generated) | CC0 1.0 | Box grids projected onto ellipsoids and warped (skull, jaw); revolved, bent horns |
| Rounded Box, Cylinder, Capsule, Ring | SculptForge (generated) | CC0 1.0 | Rounded box from a box grid; revolved profiles on cube topology; a swept rounded rectangle |

**MakeHuman hm08 base mesh**: https://github.com/makehumancommunity/makehuman/blob/a8bc2d54ff0ac92e78ff71431b1023eda42bf482/makehuman/data/3dobjs/base.obj,
SHA-256 `8e761e6624b8f54536409135d1636da63b32486a90d4897f84e121d144f6fb4c`, downloaded 2026-11-01. Licence
verified that day: the file's header says "This asset was explicitly released as CC0 in september 2020", and the
repository's [LICENSE.md](https://github.com/makehumancommunity/makehuman/blob/master/LICENSE.md) (section C:
"The base mesh and proxies ... have been released under CC0 1.0 Universal", full text in LICENSE.ASSETS.md). Only
the mesh is used; MakeHuman's AGPL code is not. Credited in THIRD_PARTY_NOTICES.md.

## Conventions

Centimetres, Y up, looking down +Z, the character's left is +X. Centred on X and Z (Hand, Foot and Ear too, though
they are one-sided, not symmetric), lowest point at Y = 0. All quads; edge-manifold; closed except where a
piece was cut from the body. The symmetric meshes are exactly mirror-symmetric (MakeHuman's mesh is; the generated
ones are built symmetric). `tests/test_starter_meshes.cpp` checks all of this for every mesh, and
`tests/ui/starter_meshes.sfui` opens one from the Library.

## Rebuilding

    tools/starter/build.sh build/sculptforge-cli

downloads base.obj at the pinned commit, checks its SHA-256, runs `tools/starter/build_starter_meshes.py`
(deterministic), saves each OBJ as a project with sculptforge-cli and compresses it into assets/starter. A new mesh
also needs its line in `starterMeshes()` (src/core/StarterMeshes.cpp) and in the `embed_files` list in
CMakeLists.txt.
