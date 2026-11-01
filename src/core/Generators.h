// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Mask generators (PBR texturing phase 2C; scaffolding only). Will compute masks for Fill layers from mesh maps:
// Edge Wear, Dirt, Gradient, Thickness and Light Direction.
#pragma once

#include <cstdint>

namespace sf {

enum class GeneratorKind : uint8_t { EdgeWear, Dirt, Gradient, Thickness, LightDirection };

} // namespace sf
