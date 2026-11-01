// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Where the pointer will be a frame from now, for the predictive stroke preview (render/Ghost.h): the brush
// cursor and the head of the stroke are drawn there, so they sit under the pen when the frame reaches the screen
// instead of a frame behind it. Each new sample corrects the prediction (nothing predicted is ever sculpted).
//
// An alpha-beta-gamma filter over the samples (position, velocity, acceleration; the gains of a critically
// damped tracker), extrapolated with the acceleration damped, at most kMaxAhead seconds ahead. A pointer that
// stops, or one that hasn't sent a sample for a while, predicts where it is.
#pragma once

#include <glm/glm.hpp>

#include <algorithm>

namespace sf {

class PenPredictor {
public:
    static constexpr double kMaxAhead = 0.025; // seconds
    static constexpr double kStale = 0.05;     // no sample for this long: the pointer has stopped

    void reset(glm::vec2 p, double t) {
        pos_ = p;
        vel_ = acc_ = glm::vec2(0.0f);
        last_ = t;
        samples_ = 1;
    }

    void add(glm::vec2 p, double t) {
        if (samples_ == 0) {
            reset(p, t);
            return;
        }
        const double dt = t - last_;
        if (dt <= 1e-4) { // the same instant (several events in one batch): take the newest position
            pos_ = p;
            return;
        }
        if (dt > kStale) { // after a pause the old velocity says nothing
            vel_ = (p - pos_) / float(dt);
            acc_ = glm::vec2(0.0f);
            pos_ = p;
            last_ = t;
            ++samples_;
            return;
        }
        const float h = float(dt);
        // Predict to this sample's time, then correct by the residual.
        const glm::vec2 xp = pos_ + vel_ * h + 0.5f * acc_ * h * h;
        const glm::vec2 vp = vel_ + acc_ * h;
        const glm::vec2 r = p - xp;
        constexpr float a = 0.7f, b = 0.35f, g = 0.06f; // stable, responsive gains
        pos_ = xp + a * r;
        vel_ = vp + (b / h) * r;
        acc_ = acc_ + (2.0f * g / (h * h)) * r;
        // The first samples: velocity from the difference alone.
        if (samples_ == 1) {
            vel_ = r / h;
            acc_ = glm::vec2(0.0f);
            pos_ = p;
        }
        last_ = t;
        ++samples_;
    }

    // The pointer at time t (normally a frame after the last sample); `latest` is the newest sample, returned when
    // the filter has too little to go on.
    glm::vec2 predict(double t, glm::vec2 latest) const {
        if (samples_ < 3) return latest;
        const double since = t - last_;
        if (since > kStale || since < -kStale) return latest;
        const float h = float(std::clamp(since, 0.0, kMaxAhead));
        // From the newest sample (not the filtered position, which trails it a little), with the motion the
        // filter sees; the acceleration counts half, since it is the noisiest part.
        return latest + vel_ * h + 0.25f * acc_ * h * h;
    }

    glm::vec2 velocity() const { return vel_; }
    int samples() const { return samples_; }

private:
    glm::vec2 pos_{0.0f}, vel_{0.0f}, acc_{0.0f};
    double last_ = 0.0;
    int samples_ = 0;
};

} // namespace sf
