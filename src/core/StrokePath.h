// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// How input samples become dabs, for strokes that cannot keep up with the cursor (tens of millions of triangles).
//
// A dab on a huge mesh can cost more than a frame. The app only reads the pointer between dabs, and Windows (like
// the other window systems) hands over just the latest position when the program was busy. Placing every dab of a
// long segment before the next read made the segments longer still: the stroke collapsed into one straight line
// from where it began to where the pointer is now.
//
// So the work is split in two:
//   StrokePath  turns each input sample into the dabs between the previous sample and this one, at an even
//               spacing. With `smooth`, a long gap between samples is drawn as a curve through the last three
//               samples (a Hermite segment), not a chord, so a fast arc stays an arc.
//   DabQueue    holds those dabs; the main loop works through it for a limited time per frame and reads input and
//               draws in between. If the queue grows past a limit, every second waiting dab is dropped (a wider
//               spacing under load) so the brush stays near the pointer instead of minutes behind it.
//
// Plain floats, no UI or maths library, so it is tested without a window (tests/test_stroke_path.cpp).
#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <deque>
#include <vector>

namespace sf {

struct PathDab {
    float x = 0.0f, y = 0.0f; // screen pixels
    float pressure = 1.0f;
};

class StrokePath {
public:
    // Starts a path at (x, y). Also used when a stroke restarts its position (a pen that has not pressed yet).
    void reset(float x, float y) {
        x_ = x;
        y_ = y;
        px_ = x;
        py_ = y;
        havePrev_ = false;
        residual_ = 0.0f;
    }

    // Appends the dabs between the previous point and (x, y), `spacing` pixels apart (the leftover distance
    // carries over to the next call). Pressure runs from p0 at the previous point to p1 at (x, y).
    // smooth: a gap longer than two spacings is curved through the last three points. Otherwise it is straight.
    void addSample(float x, float y, float p0, float p1, float spacing, bool smooth, std::vector<PathDab>& out) {
        spacing = std::max(spacing, 1.0f);
        const float dx = x - x_, dy = y - y_;
        const float chord = std::sqrt(dx * dx + dy * dy);
        if (chord <= 1e-6f) return;

        int n = 1;
        float m1x = dx, m1y = dy; // Hermite tangents at the start and at the end of the segment
        float m2x = dx, m2y = dy;
        if (smooth && chord > 2.0f * spacing) {
            if (havePrev_) {
                // The path is taken to keep turning at the rate it just did (the next gap is the current one turned by
                // the last turn), and the tangents are the Catmull-Rom ones: half of the two gaps beside the point.
                // Nothing here waits for a later sample, so the dab never lags the pointer.
                const float cx = x_ - px_, cy = y_ - py_; // the previous gap
                const float clen = std::sqrt(cx * cx + cy * cy);
                if (clen > 1e-6f) {
                    const float cs = (cx * dx + cy * dy) / (clen * chord);
                    const float sn = (cx * dy - cy * dx) / (clen * chord);
                    const float nx = dx * cs - dy * sn, ny = dx * sn + dy * cs; // the predicted next gap
                    float t1x = 0.5f * (cx * chord / clen + dx), t1y = 0.5f * (cy * chord / clen + dy);
                    float t2x = 0.5f * (dx + nx), t2y = 0.5f * (dy + ny);
                    // a hairpin turn (a tangent points backwards) stays a corner: straight
                    if (t1x * dx + t1y * dy > 0.0f && t2x * dx + t2y * dy > 0.0f) {
                        limit(t1x, t1y, chord);
                        limit(t2x, t2y, chord);
                        m1x = t1x;
                        m1y = t1y;
                        m2x = t2x;
                        m2y = t2y;
                    }
                }
            }
            n = std::clamp(int(std::ceil(chord / (2.0f * spacing))), 1, kMaxSubdivisions);
        }

        // Walk the path (n straight pieces) placing dabs at even spacing.
        float ax = x_, ay = y_, at = 0.0f;
        float travelled = spacing - residual_;
        for (int i = 1; i <= n; ++i) {
            const float t = float(i) / float(n);
            float bx = x, by = y;
            if (i < n) { // a point on the Hermite curve; the last piece ends exactly on the sample
                const float t2 = t * t, t3 = t2 * t;
                const float h00 = 2 * t3 - 3 * t2 + 1, h10 = t3 - 2 * t2 + t, h01 = -2 * t3 + 3 * t2, h11 = t3 - t2;
                bx = h00 * x_ + h10 * m1x + h01 * x + h11 * m2x;
                by = h00 * y_ + h10 * m1y + h01 * y + h11 * m2y;
            }
            const float sx = bx - ax, sy = by - ay;
            const float len = std::sqrt(sx * sx + sy * sy);
            if (len > 1e-6f) {
                while (travelled <= len) {
                    const float u = travelled / len;
                    PathDab d;
                    d.x = ax + sx * u;
                    d.y = ay + sy * u;
                    const float tt = at + (t - at) * u;
                    d.pressure = p0 + (p1 - p0) * tt;
                    out.push_back(d);
                    travelled += spacing;
                }
                travelled -= len; // distance still to go to the next dab, from the end of this piece
            }
            ax = bx;
            ay = by;
            at = t;
        }
        residual_ = spacing - travelled; // distance since the last dab
        if (residual_ < 0.0f) residual_ = 0.0f;
        px_ = x_;
        py_ = y_;
        havePrev_ = true;
        x_ = x;
        y_ = y;
    }

    float lastX() const { return x_; }
    float lastY() const { return y_; }

private:
    static constexpr int kMaxSubdivisions = 64;
    static void limit(float& tx, float& ty, float maxLen) {
        const float len = std::sqrt(tx * tx + ty * ty);
        if (len > maxLen) {
            tx *= maxLen / len;
            ty *= maxLen / len;
        }
    }
    float x_ = 0.0f, y_ = 0.0f;   // the previous sample
    float px_ = 0.0f, py_ = 0.0f; // the one before it
    bool havePrev_ = false;
    float residual_ = 0.0f;
};

// How many dabs may wait to be placed before the queue is thinned: about `backlogSeconds` of work at what a dab
// costs now (dabSeconds, 0 = not measured yet), never fewer than `minDabs`; `fixed` >= 0 is a fixed limit
// instead (SF_STROKE_BACKLOG). A light mesh keeps hundreds of dabs (a fast small brush keeps its spacing); a heavy
// one only a few, so the brush stays near the pointer and releasing doesn't place a second of waiting dabs.
inline size_t strokeBacklogLimit(double dabSeconds, long fixed = -1, double backlogSeconds = 0.05, size_t minDabs = 4) {
    if (fixed >= 0) return size_t(fixed);
    size_t limit = minDabs;
    if (dabSeconds > 0.0) limit = std::max(limit, size_t(std::min(backlogSeconds / dabSeconds, 1e6)));
    return limit;
}

// Dabs waiting to be placed.
class DabQueue {
public:
    void push(const PathDab& d) { q_.push_back(d); }
    bool empty() const { return q_.empty(); }
    size_t size() const { return q_.size(); }
    void clear() { q_.clear(); }
    const std::deque<PathDab>& items() const { return q_; } // oldest first (the predictive preview draws them)
    PathDab pop() {
        PathDab d = q_.front();
        q_.pop_front();
        return d;
    }

    // Keeps the queue at `limit` dabs or fewer by dropping every second waiting dab (the newest always stays).
    // limit 0: never drop. Returns how many were dropped.
    size_t thin(size_t limit) {
        if (limit == 0) return 0;
        size_t dropped = 0;
        while (q_.size() > limit) {
            std::deque<PathDab> kept;
            for (size_t i = 0; i < q_.size(); ++i)
                if (i % 2 == 1 || i + 1 == q_.size()) kept.push_back(q_[i]);
            dropped += q_.size() - kept.size();
            if (kept.size() >= q_.size()) break; // (a queue of one)
            q_.swap(kept);
        }
        return dropped;
    }

private:
    std::deque<PathDab> q_;
};

} // namespace sf
