// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Named meshes for model files that hold several objects (OBJ `o` blocks,
// glTF nodes, FBX models, USD Mesh prims, Bridge object lists). Each object
// becomes one Part on import, and each visible Part one object on export.
#pragma once

#include "core/Mesh.h"

#include <memory>
#include <string>
#include <vector>

namespace sf {

// An object read from a file.
struct MeshObject {
    std::string name; // as stored in the file (may be empty)
    Mesh mesh;
};

// An object to write: its name and a mesh owned by the caller (or by `owned`, e.g. a copy placed in the scene).
// Exporters that know instancing (glTF, USD) may get `transform` (where the mesh goes in the scene) and
// `instanceOf` (the index of an earlier object with the same mesh: a linked copy, #94); the others always get
// placed meshes (identity, -1).
struct MeshObjectRef {
    std::string name;
    const Mesh* mesh = nullptr;
    glm::mat4 transform{1.0f};
    int instanceOf = -1;
    std::shared_ptr<const Mesh> owned{};
};

} // namespace sf
