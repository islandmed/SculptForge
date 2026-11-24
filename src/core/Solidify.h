// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Solidify (Tool > Parts, beside Extract): gives a flat, single-sided Part real thickness. The surface keeps its
// vertices, faces, face groups, UVs and paint; a copy of it, pushed along the normals and facing the other way,
// becomes the back; a rim of quads joins the two along every open edge, so the result is one closed mesh.
// Extract with Thickness 0 makes a flat surface first; Solidify gives it the thickness later, in place.
#pragma once

#include "core/Mesh.h"

#include <string>

namespace sf {

class CommandRegistry;

struct SolidifyOptions {
    float thickness = 0.03f; // as a fraction of the Part's largest side; > 0 grows behind the surface (along -normal),
                             // < 0 in front of it
    bool bothSides = false;  // centre the thickness on the surface (half each way)
};

// The solidified mesh, or an empty mesh with `error` when `mesh` can't be solidified (closed already, no faces,
// a thickness of 0). Front faces keep their indices, groups and attributes; the back gets the same groups; the rim
// gets a new face group (one above the highest used).
Mesh solidifyMesh(const Mesh& mesh, const SolidifyOptions& options, std::string& error);
// Why the active Part can't be solidified now (levels, Micro, a Tube or Forge Spheres, layers), or "".
std::string solidifyBlocker(const Mesh& mesh);

// solidify thickness= both=
void registerSolidifyCommands(CommandRegistry& registry);

} // namespace sf
