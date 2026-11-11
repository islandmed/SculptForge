// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Alpha options (#76): how a brush's alpha turns on each dab. Follow stroke direction turns the alpha's top along
// the stroke, smoothed as Blender's Rake is (the direction is taken again only once the cursor has gone 20 pixels
// from where it was last taken); Angle turns it by a fixed amount and Angle jitter by a random one per dab. Scale
// jitter resizes and Position jitter moves each dab a little (the alpha-pool workflow: detail that does not repeat),
// and an AlphaPool picks the alpha of each dab at random. Tile (BrushAlpha::tileX / tileY) repeats the alpha inside
// the dab. Kept per brush (core/BrushSettings.h), except the pool.
#pragma once

#include "core/BrushAlpha.h"

#include <glm/glm.hpp>

#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace sf {

struct AlphaOptions {
    bool followStroke = false;
    float angle = 0.0f;  // degrees, -180..180 (counter-clockwise on screen)
    float jitter = 0.0f; // degrees, 0..180: each dab turns by up to this much either way
    float scaleJitter = 0.0f;    // percent, 0..90: each dab is this much bigger or smaller at most
    float positionJitter = 0.0f; // percent of the dab's radius, 0..100: each dab moves this far at most
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
// Scale and Position jitter of one dab: `scale` multiplies the radius (1 - s .. 1 + s), `offset` is in dab radii on
// the screen (x right, y up; inside the disc of radius positionJitter / 100). Advances `seed` only for the jitter
// that is on, so brushes without it draw exactly as before.
struct AlphaDabJitter {
    float scale = 1.0f;
    glm::vec2 offset{0.0f};
};
AlphaDabJitter alphaDabJitter(const AlphaOptions& o, uint32_t& seed);

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

// A pool of alphas a brush draws from at random (a folder of scans, or a few picked alphas): each dab, or each
// Drag Stamp, takes one. Immutable once made, so a stroke keeps the pool it began with while the artist edits the next.
class AlphaPool {
public:
    static constexpr size_t kMax = 32; // 512 px floats each: about 1 MB apiece
    size_t size() const { return items_.size(); }
    bool empty() const { return items_.empty(); }
    const std::shared_ptr<const BrushAlpha>& at(size_t i) const { return items_[i]; }
    // The alpha for the next dab; advances `seed` (xorshift) only when there is a choice.
    const std::shared_ptr<const BrushAlpha>& pick(uint32_t& seed) const;
    bool contains(const std::string& key) const; // by image path, or the built-in's name
    // A copy with `a` added (its path, else its name, is the key; adding one already in is a no-op).
    std::shared_ptr<const AlphaPool> with(BrushAlpha a) const;
    // The image files of `dir` (png, jpg, ...; not its sub-folders) A-Z, up to kMax, loaded as alphas. Files that do not
    // load are skipped; `error` says why when none loaded. Null when the folder gives nothing.
    static std::shared_ptr<const AlphaPool> fromFolder(const std::string& dir, std::string& error);

private:
    std::vector<std::shared_ptr<const BrushAlpha>> items_;
};

} // namespace sf
