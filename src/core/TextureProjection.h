// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Texture projection (PBR texturing phase 3; scaffolding only). Will fill material channels by triplanar,
// planar, spherical or cylindrical projection, and paint through screen stencils and clone sources.
#pragma once

#include <cstdint>

namespace sf {

enum class ProjectionKind : uint8_t { Triplanar, Planar, Spherical, Cylindrical };

} // namespace sf
