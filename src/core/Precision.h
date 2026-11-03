// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Precision work (pores, wrinkles, jewellery, hard-surface trims): the pieces of the brush that are not tied to a
// window, so they can be tested without one. Steady Stroke itself is LazyMouse in core/Stroke.h (a point on a
// string); here are the precision drag (Shift+Alt mid-stroke: the brush moves at a quarter of the pen's speed),
// fine Draw Size steps, dab spacing for tiny brushes, the filter that keeps the surface pick from wobbling on dense
// meshes, and the point-based pressure curve each brush keeps (Brush Lab and Preferences > Tablet).
#pragma once

#include "core/Json.h"

#include <glm/glm.hpp>

#include <array>
#include <string>

namespace sf {

// Draw Size (the brush radius in screen pixels) limits.
constexpr float kMinDrawSizePx = 0.5f;
constexpr float kMaxDrawSizePx = 800.0f;
// Model units are metres (STL export writes 1000 mm per unit), so World Size shows millimetres.
constexpr float kMmPerUnit = 1000.0f;

float clampDrawSize(float px);
// [ / ]: 10% smaller / larger. Shift+[ / Shift+]: one pixel (half a pixel below 2 px).
float drawSizeStep(float px, int direction, bool fine);
// Ctrl+Shift+right-drag: dx pixels sideways scrub the size finely (about 1.5x per 100 px, never past the limits).
float drawSizeScrub(float px, float dx);

// The distance between dabs, in screen pixels, for a brush of radius `radiusPx` and a recipe's spacing (a fraction
// of the radius). Normally at least a pixel; tiny brushes (below a few px) space by half their radius so a stroke
// with a 0.5 px brush stays one continuous line.
float dabSpacingPx(float radiusPx, float spacingFraction);

// Shift+Alt held mid-stroke: the brush point moves at `speed` times the pen's motion, for careful placement. When
// the keys are let go the point catches up with the pen as it moves on (twice as fast as the pen), never jumping.
struct PrecisionDrag {
    float speed = 0.25f;
    glm::vec2 point{0.0f};

    void reset(glm::vec2 p) {
        point = cursor_ = p;
    }
    glm::vec2 follow(glm::vec2 cursor, bool slow);
    bool caughtUp() const { return glm::length(cursor_ - point) < 0.01f; }

private:
    glm::vec2 cursor_{0.0f};
};

// The surface point under a brush jitters in depth on dense meshes (every tiny bump moves the ray's hit), so the
// dabs wobble. A small filter on the hit's distance along its ray keeps the brush steady. It follows the distance's
// trend (alpha-beta: a slope, as across a curved surface, gives no lag) and smooths only the jitter about it; a jump
// larger than `jump` (a real step, a silhouette) is taken at once.
class PickSmoother {
public:
    float amount = 0.5f; // 0: off; how much of each sample's deviation from the trend is held back
    void reset() { have_ = false; }
    float filter(float distance, float jump);

private:
    float last_ = 0.0f, velocity_ = 0.0f;
    bool have_ = false;
};

// Pen pressure -> brush pressure by 2 to 5 points (0,y0) .. (1,yN) joined by a smooth monotone curve (no overshoot,
// so pressing harder never gives less). Each brush keeps its own (after the tablet's global curve).
struct PressurePoints {
    static constexpr int kMaxPoints = 5;
    std::array<glm::vec2, kMaxPoints> points{glm::vec2(0.0f, 0.0f), glm::vec2(1.0f, 1.0f)};
    int count = 2;

    float apply(float pressure) const;
    bool isLinear() const;
    void sort();  // by x, the ends pinned to x = 0 and x = 1, y clamped to 0..1
    bool addPoint(float x); // a point on the curve at x (false: full, or too close to another)
    bool removePoint(int i); // not an end
    bool operator==(const PressurePoints& o) const;

    Json toJson() const;
    static PressurePoints fromJson(const Json& j); // lenient: a bad value gives the linear curve

    static PressurePoints linear();
    static PressurePoints soft();       // a light touch already gives a lot
    static PressurePoints firm();       // press harder for a strong stroke
    static PressurePoints fineDetail(); // a long, gentle start: light pressure stays light, for pores and fine lines
    struct Preset {
        const char* name;
        PressurePoints (*make)();
    };
    static const std::array<Preset, 4>& presets(); // Soft, Linear, Firm, Fine detail
    const char* presetName() const; // the preset this curve is, or nullptr
};

} // namespace sf
