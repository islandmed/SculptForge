// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Alpha options (#76): how a brush's alpha turns on each dab. Follow stroke direction turns the alpha's top along
// the stroke, smoothed as Blender's Rake is (the direction is taken again only once the cursor has gone 20 pixels
// from where it was last taken); Angle turns it by a fixed amount and Angle jitter by a random one per dab. Tile
// (BrushAlpha::tileX / tileY) repeats the alpha inside the dab. Kept per brush (core/BrushSettings.h).
#pragma once

#include <glm/glm.hpp>

#include <cstdint>

namespace sf {

struct AlphaOptions {
    bool followStroke = false;
    float angle = 0.0f;  // degrees, -180..180 (counter-clockwise on screen)
    float jitter = 0.0f; // degrees, 0..180: each dab turns by up to this much either way
    int tileX = 1, tileY = 1; // 1..8

    bool operator==(const AlphaOptions&) const = default;
    void clamp();
};

// Blender's Rake for the alpha: the stroke's direction on screen, taken again each time the cursor has moved
// kRakeStep pixels from the point it was last taken at (smaller wobbles don't turn the alpha).
class AlphaRake {
public:
    static constexpr float kRakeStep = 20.0f;
    void reset(glm::vec2 at);
    // A dab at `at` (window pixels, y down). True when the direction is known (the cursor has gone kRakeStep).
    bool update(glm::vec2 at);
    float angle() const { return angle_; } // radians, the dab angle that puts the alpha's top along the stroke
    bool known() const { return known_; }

private:
    glm::vec2 last_{0.0f};
    float angle_ = 0.0f;
    bool known_ = false;
};

// The dab's alpha angle (radians; App::dabAt's convention: 0 = the alpha's top at the top of the screen, counter-
// clockwise positive) from the options, the rake's direction and the per-stroke random `seed` (xorshift, advanced).
float alphaDabAngle(const AlphaOptions& o, const AlphaRake& rake, uint32_t& seed);

} // namespace sf
