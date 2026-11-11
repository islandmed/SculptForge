// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Finger mode (the touch layout, app/TouchShell.h): what the fingers on the canvas mean when there is no pen in the
// hand. One finger sculpts once it has moved past a slop; two or more fingers always navigate (core/TouchGesture.h
// pairs them into a pan, a pinch and a twist). A quick tap of two fingers undoes, of three redoes; two fingers held
// still and then slid sideways scrub the undo history a step at a time. Touches that land while the pen is near the
// glass (App::palmGuard, passed in) are a palm: ignored from landing to lifting. Independent of SDL so it can be
// unit tested; positions are in pixels, times in seconds.
#pragma once

#include "core/TouchGesture.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

namespace sf {

struct FingerEvent {
    enum class Kind {
        StrokeBegin, // one finger moved past the slop: a stroke starts at `at` (where it landed), then...
        StrokeMove,  // ... follows the finger to `at`
        StrokeEnd,   // the finger lifted (or a second one landed: the stroke so far stays)
        Navigate,    // two or more fingers moved: `step`
        Undo,        // a two-finger tap
        Redo,        // a three-finger tap
        ScrubBegin,  // two fingers held still: the undo history scrub starts
        ScrubStep,   // ... `steps` more (> 0 forward, redo; < 0 back, undo) since the last ScrubStep
        ScrubEnd,    // ... the fingers lifted
    };
    Kind kind;
    glm::vec2 at{0.0f};
    TwoFingerStep step;
    int steps = 0;
};

struct FingerModeTuning {
    float slopPx = 12.0f;        // a finger moves this far before it sculpts or a tap stops being one (8 dp)
    double tapSeconds = 0.30;    // first finger down to last finger up, at most, for a tap
    double holdSeconds = 0.45;   // two fingers still this long: the history scrub
    float scrubStepPx = 36.0f;   // sideways travel per history step (24 dp)
};

class FingerMode {
public:
    using Events = std::vector<FingerEvent>;

    FingerModeTuning tuning;

    // `palm`: App::palmGuard() when the finger landed; such a finger is ignored until it lifts.
    Events down(uint64_t id, glm::vec2 p, double t, bool palm);
    Events move(uint64_t id, glm::vec2 p, double t);
    // `palm` as for down: a tap finished while the pen is near the glass neither undoes nor redoes.
    Events up(uint64_t id, glm::vec2 p, double t, bool palm);
    // Time passing without events: two fingers held still start the scrub.
    Events tick(double t);
    // The system took the touches away (SDL_EVENT_FINGER_CANCELED, the app leaving): a stroke ends, nothing else.
    Events cancel();

    int fingers() const { return int(fingers_.size()); }
    bool stroking() const { return state_ == State::Stroke; }
    bool scrubbing() const { return state_ == State::Scrub; }
    bool navigating() const { return state_ == State::Navigate; }
    bool idle() const { return state_ == State::Idle && ignored_.empty(); }
    int scrubSteps() const { return scrubSent_; } // history steps the open scrub has moved (< 0: back)

private:
    enum class State { Idle, Pending, Stroke, Multi, Navigate, Scrub, Done };
    struct Finger {
        uint64_t id;
        glm::vec2 start, p;
    };
    Finger* find(uint64_t id);
    bool movedPastSlop() const;
    glm::vec2 centre() const;
    void reset();

    State state_ = State::Idle;
    std::vector<Finger> fingers_;
    std::vector<uint64_t> ignored_; // palms, and fingers past the tenth
    TouchGesture pair_;
    double firstDown_ = 0.0, multiSince_ = 0.0;
    int maxFingers_ = 0;
    float scrubStartX_ = 0.0f;
    int scrubSent_ = 0;
};

} // namespace sf
