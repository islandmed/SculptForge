// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// OpenUSD interchange without the OpenUSD SDK (keeps builds small and
// portable). Writes text USD (.usda) and USDZ packages (.usdz, for AR Quick
// Look / Vision Pro, Omniverse, Houdini Solaris, Blender, Maya, Unreal):
//  - the sculpt as a UsdGeomMesh with a UsdPreviewSurface clay material, UVs
//    as a face-varying primvars:st
//  - a Part's texture (core/Texture.h) as a PNG (inside a .usdz, else beside
//    the file) read by a UsdUVTexture in the mesh's own UsdPreviewSurface
//  - sculpt layers as UsdSkel blend shapes, current weights on a SkelAnimation
//  - Gaussian splats as a UsdVol ParticleField3DGaussianSplat prim (OpenUSD 26.03+)
//
// Reads .usda files and .usdz packages whose root layer is text: meshes (any
// polygon counts, xform ops, Z-up stages, texCoord2 UVs), blend shapes and
// Gaussian splats.
// Binary .usdc layers and external references are not resolved yet.
#pragma once

#include "core/Mesh.h"
#include "core/MorphAnimation.h"
#include "core/Splats.h"
#include "io/MeshObject.h"

#include <string>

namespace sf {

// Either pointer may be null (or empty) but not both. Format by extension.
bool saveUsd(const std::string& path, const Mesh* mesh, const SplatCloud* splats, const std::string& name,
             std::string& error);
// One Mesh prim per object (Part) under the root Xform `name`, each with its
// own blend shapes: a single object is the prim "Sculpt" as above, several are
// named after the objects (made valid and unique). `animation` (optional, e.g.
// a baked face take) adds time samples to each SkelAnimation's
// blendShapeWeights for the shapes it names, at its fps.
bool saveUsd(const std::string& path, const std::vector<MeshObjectRef>& meshes, const SplatCloud* splats,
             const std::string& name, std::string& error, const MorphAnimation* animation = nullptr);

// Fills whichever of `mesh` / `splats` the file contains (the other is left
// empty; every Mesh prim is merged). Fails if it contains neither.
// `warnings` (optional) gets what was skipped as malformed ("Edge creases of /Root/Mesh skipped: ..."; "; " between).
bool loadUsd(const std::string& path, Mesh& mesh, SplatCloud& splats, std::string& error, std::string* warnings = nullptr);
// The same with one object per Mesh prim, named after the prim (or its SkelRoot).
bool loadUsd(const std::string& path, std::vector<MeshObject>& meshes, SplatCloud& splats, std::string& error,
             std::string* warnings = nullptr);

// A valid USD prim name from arbitrary text ("Smile 2" -> "Smile_2").
std::string usdIdentifier(const std::string& text);

} // namespace sf
