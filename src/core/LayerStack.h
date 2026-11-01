// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Layer stack (PBR texturing phase 2B; scaffolding only). Will add layer kinds beyond paint layers (Fill layer,
// Folder, Filter) and mask effects (paint, fill, generator, filter, geometry, levels, invert) over the material
// channels of core/TextureChannels.h, evaluated tile by tile with a cache, previewing at 1/4 resolution during
// slider drags. Paint layers with per-channel blend and opacity exist today (TextureLayer::channels); project IO
// for the stack goes through core/LayerStackIO.h.
#pragma once

#include <cstdint>

namespace sf {

enum class LayerKind : uint8_t { Paint, Fill, Folder, Filter };
enum class MaskEffectKind : uint8_t { Paint, Fill, Generator, Filter, Geometry, Levels, Invert };

} // namespace sf
