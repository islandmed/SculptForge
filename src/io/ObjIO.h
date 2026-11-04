// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/Mesh.h"
#include "io/MeshObject.h"

#include <string>
#include <vector>

namespace sf {

// Paths are UTF-8. Polygons are fan-triangulated; quads are kept paired
// (Mesh::quadPartners) and written back as quads. `vt` UVs are read into
// Mesh::uvs and written back (normals are ignored); a Part with a texture
// (core/Texture.h) is written with <stem>.mtl and a PNG beside the file
// (`map_Kd`, see io/TextureExport.h).
// "v x y z r g b" vertex colours are read into Mesh::colors and written back.
// `g` groups become face groups (when an object uses two or more), as classic
// writes them; `o` starts a new object, as Blender and classic write Parts.

// Reads every object into one mesh.
bool loadObj(const std::string& path, Mesh& out, std::string& error);
// Reads each `o` object as its own mesh (objects without faces are skipped).
// A file without `o` lines, or with one object, gives exactly what loadObj gives.
bool loadObj(const std::string& path, std::vector<MeshObject>& out, std::string& error);

bool saveObj(const std::string& path, const Mesh& mesh, std::string& error);
// Several objects are written as `o name` blocks; a single one as a plain mesh.
// Face groups (two or more) are written as `g` groups.
bool saveObj(const std::string& path, const std::vector<MeshObjectRef>& objects, std::string& error);

} // namespace sf
