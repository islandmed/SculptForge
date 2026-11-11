// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/FingerMode.h"

#include <algorithm>
#include <cmath>

namespace sf {

namespace {
bool finite(glm::vec2 p) { return std::isfinite(p.x) && std::isfinite(p.y); }
} // namespace

FingerMode::Finger* FingerMode::find(uint64_t id) {
    for (Finger& f : fingers_)
        if (f.id == id) return &f;
    return nullptr;
}

bool FingerMode::movedPastSlop() const {
    for (const Finger& f : fingers_)
        if (glm::length(f.p - f.start) > tuning.slopPx) return true;
    return false;
}

glm::vec2 FingerMode::centre() const {
    glm::vec2 c(0.0f);
    for (const Finger& f : fingers_) c += f.p;
    return fingers_.empty() ? c : c / float(fingers_.size());
}

void FingerMode::reset() {
    state_ = State::Idle;
    fingers_.clear();
    pair_.clear();
    maxFingers_ = 0;
    scrubSent_ = 0;
}

FingerMode::Events FingerMode::down(uint64_t id, glm::vec2 p, double t, bool palm) {
    Events out;
    if (!finite(p) || find(id) || std::find(ignored_.begin(), ignored_.end(), id) != ignored_.end()) return out;
    if (palm || fingers_.size() >= TouchGesture::kMaxFingers || state_ == State::Scrub) { // (a third finger: the scrub goes on)
        ignored_.push_back(id);
        return out;
    }
    fingers_.push_back({id, p, p});
    maxFingers_ = std::max(maxFingers_, int(fingers_.size()));
    switch (state_) {
    case State::Idle:
        state_ = State::Pending;
        firstDown_ = t;
        break;
    case State::Pending: // a second finger before the first moved: a tap, a hold or navigation, never a stroke
        state_ = State::Multi;
        multiSince_ = t;
        pair_.clear();
        for (const Finger& f : fingers_) pair_.down(f.id, f.start);
        break;
    case State::Stroke: // the stroke so far stays; from here the fingers navigate
        out.push_back({FingerEvent::Kind::StrokeEnd, fingers_.front().p});
        state_ = State::Navigate;
        pair_.clear();
        for (const Finger& f : fingers_) pair_.down(f.id, f.p);
        break;
    case State::Multi:
    case State::Navigate:
        pair_.down(id, p);
        break;
    case State::Scrub: // (a third finger doesn't change the scrub)
    case State::Done:
        break;
    }
    return out;
}

FingerMode::Events FingerMode::move(uint64_t id, glm::vec2 p, double t) {
    Events out;
    Finger* f = finite(p) ? find(id) : nullptr;
    if (!f) return out;
    f->p = p;
    switch (state_) {
    case State::Pending:
        if (glm::length(p - f->start) > tuning.slopPx) {
            state_ = State::Stroke;
            out.push_back({FingerEvent::Kind::StrokeBegin, f->start});
            out.push_back({FingerEvent::Kind::StrokeMove, p});
        }
        break;
    case State::Stroke:
        out.push_back({FingerEvent::Kind::StrokeMove, p});
        break;
    case State::Multi:
        if (movedPastSlop()) {
            // Navigation from where the fingers landed, so the view catches up with the slop they moved first.
            state_ = State::Navigate;
            for (size_t i = 0; i < std::min<size_t>(2, fingers_.size()); ++i) {
                FingerEvent e{FingerEvent::Kind::Navigate};
                if (pair_.move(fingers_[i].id, fingers_[i].p, e.step)) out.push_back(e);
            }
            for (size_t i = 2; i < fingers_.size(); ++i) {
                TwoFingerStep ignored;
                pair_.move(fingers_[i].id, fingers_[i].p, ignored);
            }
        } else {
            out = tick(t);
        }
        break;
    case State::Navigate: {
        FingerEvent e{FingerEvent::Kind::Navigate};
        if (pair_.move(id, p, e.step)) out.push_back(e);
        break;
    }
    case State::Scrub: {
        const int steps = int((centre().x - scrubStartX_) / tuning.scrubStepPx); // (toward zero: a dead zone)
        if (steps != scrubSent_) {
            FingerEvent e{FingerEvent::Kind::ScrubStep};
            e.steps = steps - scrubSent_;
            scrubSent_ = steps;
            out.push_back(e);
        }
        break;
    }
    case State::Idle:
    case State::Done:
        break;
    }
    return out;
}

FingerMode::Events FingerMode::tick(double t) {
    Events out;
    if (state_ == State::Multi && maxFingers_ == 2 && fingers_.size() == 2 && !movedPastSlop() &&
        t - multiSince_ >= tuning.holdSeconds) {
        state_ = State::Scrub;
        scrubStartX_ = centre().x;
        scrubSent_ = 0;
        out.push_back({FingerEvent::Kind::ScrubBegin, centre()});
    }
    return out;
}

FingerMode::Events FingerMode::up(uint64_t id, glm::vec2 p, double t, bool palm) {
    Events out;
    if (const auto it = std::find(ignored_.begin(), ignored_.end(), id); it != ignored_.end()) {
        ignored_.erase(it);
        return out;
    }
    Finger* f = find(id);
    if (!f) return out;
    if (finite(p)) f->p = p;
    switch (state_) {
    case State::Stroke:
        out.push_back({FingerEvent::Kind::StrokeEnd, f->p});
        break;
    case State::Scrub:
        out.push_back({FingerEvent::Kind::ScrubEnd, centre()});
        state_ = State::Done;
        break;
    case State::Multi:
        if (movedPastSlop()) state_ = State::Done; // (moved before lifting: neither a tap nor navigation now)
        break;
    default:
        break;
    }
    pair_.up(id);
    const bool wasMulti = state_ == State::Multi;
    fingers_.erase(fingers_.begin() + (f - fingers_.data()));
    if (!fingers_.empty()) return out;
    if (wasMulti && !palm && t - firstDown_ <= tuning.tapSeconds) {
        if (maxFingers_ == 2) out.push_back({FingerEvent::Kind::Undo, p});
        else if (maxFingers_ == 3) out.push_back({FingerEvent::Kind::Redo, p});
    }
    reset();
    return out;
}

FingerMode::Events FingerMode::cancel() {
    Events out;
    if (state_ == State::Stroke) out.push_back({FingerEvent::Kind::StrokeEnd, fingers_.front().p});
    if (state_ == State::Scrub) out.push_back({FingerEvent::Kind::ScrubEnd, centre()});
    reset();
    ignored_.clear();
    return out;
}

} // namespace sf
