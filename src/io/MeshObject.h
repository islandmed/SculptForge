// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Named meshes for model files that hold several objects (OBJ `o` blocks,
// glTF nodes, FBX models, USD Mesh prims, Bridge object lists). Each object
// becomes one Part on import, and each visible Part one object on export.
#pragma once

#include "core/Mesh.h"

#include <string>
#include <vector>

namespace sf {

// An object read from a file.
struct MeshObject {
    std::string name; // as stored in the file (may be empty)
    Mesh mesh;
};

// An object to write: its name and a mesh owned by the caller.
struct MeshObjectRef {
    std::string name;
    const Mesh* mesh = nullptr;
};

} // namespace sf
