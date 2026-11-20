// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The Creature Skin alphas (gh#228): hide wrinkles, scales, skin and nose pores and fine skin, made in code from
// periodic noise (no image files). Each is a height field over the whole -1..1 square that repeats seamlessly when
// tiled (every noise lattice spans whole periods across it). BrushAlpha makes them at kCreatureSize the first time a
// brush needs one, and at kCreaturePreview for the pickers' tiles.
#pragma once

#include <cstddef>

namespace sf {

constexpr int kCreatureSize = 2048;
constexpr int kCreaturePreview = 128;

struct CreatureAlpha {
    const char* name;
    const char* help;
    // The relief at x, y in 0..1 across the tile (y down, as in the image), any range: BrushAlpha stretches each
    // alpha to 0..1. White = what the brush moves most (the pores and creases for the carving brushes).
    float (*height)(float x, float y);
};

const CreatureAlpha* creatureAlphas(size_t& count);

} // namespace sf
