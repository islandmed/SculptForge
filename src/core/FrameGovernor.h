// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Adaptive frame governor: holds the 3D view at its target frame rate (60 fps, or 120 on a 120 Hz display) by
// adjusting how much the view draws, never what the scene holds. It reads measured frame times (CPU, and GPU
// through timer queries where the driver has them) and the triangles each frame drew, and moves the render
// triangle budget and the motion proxy threshold (core/HardwareProfile.h) with hysteresis:
//
//  - decisions are made on the median of a window of frames (one slow frame changes nothing);
//  - a dead band: frames between `lowFraction` and `highFraction` of the target frame time keep the budget;
//  - after a change, `settleFrames` frames are ignored while the new budget takes effect;
//  - raising the budget past the scene's size (which would switch the motion proxy off) needs a predicted frame
//    time well inside the target, so the view doesn't flip between the proxy and full detail.
//
// Only frames where the view moves count by default (still frames draw everything in full: the governor
// can't help them, and the motion proxy is what it controls in today's renderer). Micromesh will read the
// budget every frame (renderTriangleBudget()) for its continuous level of detail; see docs/performance.md.
#pragma once

#include <cstdint>
#include <vector>

namespace sf {

struct FrameSample {
    double cpuMs = 0.0;            // CPU time of the frame's work (input to swap, without waiting for vsync)
    double gpuMs = -1.0;           // GPU time of the 3D view (timer query); < 0 = unknown
    uint64_t drawnTriangles = 0;   // what the frame drew after culling and the proxy
    uint64_t sceneTriangles = 0;   // the visible scene in full detail (0 = unknown)
    bool moving = true;            // the view moved (orbit, pan, zoom, SpaceMouse...)
    bool limited = false;          // the frame drew less than full detail because of the budget (the proxy)
};

struct GovernorConfig {
    int targetFps = 60;
    double lowFraction = 0.6;   // below this share of the frame time: room to draw more
    double highFraction = 1.0;  // above: draw less
    double aimFraction = 0.8;   // where an adjustment aims
    int window = 20;            // frames per decision (their median)
    int settleFrames = 5;       // frames ignored after a change
    double maxStepDown = 0.5;   // the most one decision may cut the budget (factor)
    double maxStepUp = 1.25;    // the most one decision may raise it
    double maxFrameMs = 250.0;  // longer frames are hitches (loading, a dialog), not drawing: ignored
    uint64_t minBudget = 250'000, maxBudget = 2'000'000'000;
    uint64_t minProxyThreshold = 262'144; // the renderer draws smaller meshes in full anyway
    bool movingOnly = true;
};

class FrameGovernor {
public:
    enum class Action { Hold, Lowered, Raised };

    FrameGovernor() = default;
    explicit FrameGovernor(const GovernorConfig& config, uint64_t budget = 0) { reset(config, budget); }

    void reset(const GovernorConfig& config, uint64_t budget);
    void setTargetFps(int fps);
    // Feeds one frame; returns true when the budgets changed (read budget() / proxyThreshold()).
    bool addFrame(const FrameSample& frame);

    uint64_t budget() const { return budget_; }
    uint64_t proxyThreshold() const;
    double targetMs() const { return 1000.0 / double(config_.targetFps > 0 ? config_.targetFps : 60); }
    const GovernorConfig& config() const { return config_; }
    // The last decision's window: median frame cost (max of CPU and GPU) and median drawn triangles.
    double lastCostMs() const { return lastCost_; }
    uint64_t lastDrawn() const { return lastDrawn_; }
    Action lastAction() const { return lastAction_; }
    int adjustments() const { return adjustments_; }
    int decisions() const { return decisions_; }

private:
    void decide();
    GovernorConfig config_;
    uint64_t budget_ = 0;
    std::vector<double> costs_;
    std::vector<uint64_t> drawn_, scene_;
    int limitedFrames_ = 0;
    int settle_ = 0;
    double lastCost_ = 0.0;
    uint64_t lastDrawn_ = 0;
    Action lastAction_ = Action::Hold;
    int adjustments_ = 0, decisions_ = 0;
};

} // namespace sf
