// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Alpha options (#76): how a brush's alpha turns on each dab. Follow stroke direction turns the alpha's top along
// the stroke, smoothed as Blender's Rake is (the direction is taken again only once the cursor has gone 20 pixels
// from where it was last taken); Angle turns it by a fixed amount and Angle jitter by a random one per dab. Tile
// (BrushAlpha::tileX / tileY) repeats the alpha inside the dab. Kept per brush (core/BrushSettings.h).
#pragma once

#include <glm/glm.hpp>

#include <cmath>
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
// A random turn (radians) up to `jitterDeg` either way, advancing `seed`; 0 when jitterDeg is 0.
float alphaJitterAngle(float jitterDeg, uint32_t& seed);
// A drag stamp's angle (Drag Stamp, Slide Stamp: one stamp per stroke, #136): the drag's direction on screen
// (window pixels, y down) when `followDrag`, plus Angle and the stroke's `jitter` (from alphaJitterAngle).
float alphaStampAngle(const AlphaOptions& o, bool followDrag, glm::vec2 drag, float jitter);

// Slide Stamp's turn (#175): while the rotate key is held mid-drag the stamp stays where it is and its alpha turns
// toward the cursor; let go, it slides on with that turn kept. The turn belongs to the stroke: it is never written
// to the brush's saved Angle.
class SlideStampTurn {
public:
    static constexpr float kMinPx = 3.0f; // a cursor nearer than this to the stamp gives no direction
    void reset() { turned_ = false; havePos_ = false; angle_ = 0.0f; }
    // The stamp's screen position for a cursor at `cursor` (window pixels, y down); `rotating`: the key is held.
    glm::vec2 position(bool rotating, glm::vec2 cursor) {
        if (!rotating || !havePos_) pos_ = cursor;
        havePos_ = true;
        return pos_;
    }
    // While rotating: the angle (radians, alphaStampAngle's convention) that points the alpha's top from the stamp
    // toward `cursor`, kept for the rest of the stroke. Not rotating: unchanged.
    void turn(bool rotating, glm::vec2 cursor) {
        if (!rotating || !havePos_) return;
        const glm::vec2 d = cursor - pos_;
        if (glm::dot(d, d) < kMinPx * kMinPx) return;
        angle_ = std::atan2(-d.x, -d.y);
        turned_ = true;
    }
    bool turned() const { return turned_; }
    float angle() const { return angle_; } // radians; only the stroke's own part (Angle and jitter come on top)

private:
    glm::vec2 pos_{0.0f};
    float angle_ = 0.0f;
    bool turned_ = false, havePos_ = false;
};

} // namespace sf
