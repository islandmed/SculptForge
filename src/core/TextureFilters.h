// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Texture filters (PBR texturing phase 2D; scaffolding only). Will run island-aware blur, sharpen, HSL, levels
// and warp over a TileImage (core/TileImage.h) without bleeding across UV seams.
#pragma once

#include <cstdint>

namespace sf {

enum class TextureFilterKind : uint8_t { Blur, Sharpen, Hsl, Levels, Warp };

} // namespace sf
