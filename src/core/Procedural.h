// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Procedural textures (PBR texturing phase 2C; scaffolding only). Noise will be evaluated in object space
// through the position mesh map (core/MeshMaps.h), so it is seamless across UV seams.
#pragma once

#include <cstdint>

namespace sf {

enum class NoiseKind : uint8_t { Value, Gradient, Cellular, Fractal };

} // namespace sf
