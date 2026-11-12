// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Two-finger navigation on a touch screen or a trackpad (phoenix, Discord 11-05): which fingers are down and how
// a pair of them moved since the last event, as a pan, a pinch and a twist. Independent of SDL so it can be unit
// tested; the app side (finger and pinch events to the camera, one finger still sculpting) is src/app/Touch.cpp.
#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

namespace sf {

// One move of a pair of fingers, in the coordinates the fingers were given in.
struct TwoFingerStep {
    glm::vec2 pan{0.0f}; // how far the point between the two fingers moved
    float scale = 1.0f;  // new distance between them / old distance (> 1: spread apart, zoom in)
    float angle = 0.0f;  // radians the line between them turned, -pi .. pi (> 0: clockwise with y down)
};

class TouchGesture {
public:
    static constexpr size_t kMaxFingers = 10;

    // A finger landed (an id already down moves there instead).
    void down(uint64_t id, glm::vec2 p);
    // A finger moved. True when two or more are down and the first two (in landing order) moved as a pair: `step`
    // says how. Unknown ids and non-finite positions are ignored.
    bool move(uint64_t id, glm::vec2 p, TwoFingerStep& step);
    void up(uint64_t id);
    void clear();
    int fingers() const { return int(fingers_.size()); }
    // Two or more fingers have been down at once since the first one landed (until all are lifted): the touch is a
    // gesture, not a stroke.
    bool gesture() const { return gesture_; }

private:
    struct Finger {
        uint64_t id;
        glm::vec2 p;
    };
    std::vector<Finger> fingers_;
    bool gesture_ = false;
};

// Camera::zoom steps (x0.88 of the distance each) for a pinch that spread the fingers by `scale`: the model grows
// on screen by about the same factor. 0 for a scale that isn't finite and positive.
float pinchZoomSteps(float scale);

// Palm rejection: the hand holding a pen rests on the screen. A finger that lands while the pen is in range, or within
// kAfterPen seconds of its leaving, is a palm, not a gesture. Seconds are any steady clock's.
class PalmGuard {
public:
    static constexpr double kAfterPen = 0.3;
    double afterPen = kAfterPen; // (a UI test stretches it: its frames run faster than the clock)
    void pen(bool inRange, double now) { inRange_ = inRange; nearUntil_ = now + afterPen; } // any pen event
    bool active(double now) const { return inRange_ || now < nearUntil_; }

private:
    bool inRange_ = false;
    double nearUntil_ = 0.0;
};

} // namespace sf
