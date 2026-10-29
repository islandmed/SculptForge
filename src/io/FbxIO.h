// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Binary FBX (7.4) export, written from the publicly documented file layout
// (no Autodesk SDK, so it can ship under the GPL). FBX is still what Unreal
// Engine imports most completely:
//  - the sculpt as a polygon mesh (quads where Mesh::quadPartners keeps them,
//    else triangles) with per-corner normals and UVs, smoothing and a material slot
//  - a SubTool's texture (core/Texture.h, taken from LOD0) as a PNG beside the
//    file, read by the object's own material (Texture/Video on DiffuseColor)
//  - sculpt layers as blend shapes (Unreal: morph targets on a skeletal mesh)
//  - a LOD chain as an FBX LOD group (Unreal: LOD0..N of one static mesh)
// Import uses ufbx (third_party/ufbx).
#pragma once

#include "core/Mesh.h"
#include "io/MeshObject.h"

#include <string>
#include <vector>

namespace sf {

struct FbxExportOptions {
    float scale = 1.0f;                  // applied to positions and layer offsets
    std::string materialName = "M_Clay"; // becomes the engine's material slot
    glm::vec3 color{0.72f, 0.68f, 0.62f}; // Soft Clay grey
    bool layers = true;                  // export sculpt layers as blend shapes (on LOD0)
};

// `lods[0]` is the full-resolution mesh. With more than one entry the meshes
// are grouped under an FBX LOD group named `name`. Units are centimetres
// (FBX default) and the scene is Y-up; importers convert to their own axes.
bool saveFbx(const std::string& path, const std::string& name, const std::vector<const Mesh*>& lods,
             const FbxExportOptions& options, std::string& error);

// One object (SubTool) of a multi-object export: its LOD chain (lods[0] = full resolution).
struct FbxObject {
    std::string name;
    std::vector<const Mesh*> lods;
};
// Every object becomes its own model under the scene root, named after it,
// with its own blend shapes. An object with several LODs becomes its own LOD
// group (`name`, children `name_LOD0..N`); Unreal's "Combine Meshes" then
// builds one static mesh whose LOD n holds every object's level n.
bool saveFbx(const std::string& path, const std::vector<FbxObject>& objects, const FbxExportOptions& options,
             std::string& error);

// Reads .fbx (binary or ASCII, any version) via ufbx: all visible meshes are
// merged in world space (Y-up, 1 unit = 1 m, matching saveFbx with scale 100),
// only LOD0 of LOD groups is taken, and blend shapes become sculpt layers.
// Polygons are triangulated; quads stay paired as Mesh::quadPartners. The
// first UV set is read into Mesh::uvs.
bool loadFbx(const std::string& path, Mesh& out, std::string& error);
// The same, but each visible mesh node is its own object, named after the
// node (or after its LOD group, for a LOD0 mesh).
bool loadFbx(const std::string& path, std::vector<MeshObject>& out, std::string& error);

} // namespace sf
