// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Mesh Maps (PBR texturing phase 2A; scaffolding only). Will bake the texture-space maps the layer stack reads:
// curvature, position, thickness, ID, world normal and bent normal, from the mesh itself, from the SubTool's own
// top subdivision level, from a name-matched high-poly SubTool (_high / _low) or through a cage. Maps will be
// fingerprinted (mesh, UVs, settings) so stale ones are detected after sculpting. Today's bakes: core/Bake.h.
#pragma once

#include <cstdint>

namespace sf {

enum class MeshMapKind : uint8_t { Curvature, Position, Thickness, Id, WorldNormal, BentNormal };

} // namespace sf
