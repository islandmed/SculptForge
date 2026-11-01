// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/FrameGovernor.h"

#include <algorithm>
#include <cmath>

namespace sf {

namespace {
template <class T>
T medianOf(std::vector<T> v) {
    if (v.empty()) return T{};
    const size_t mid = v.size() / 2;
    std::nth_element(v.begin(), v.begin() + std::ptrdiff_t(mid), v.end());
    return v[mid];
}
} // namespace

void FrameGovernor::reset(const GovernorConfig& config, uint64_t budget) {
    config_ = config;
    if (config_.window < 1) config_.window = 1;
    budget_ = std::clamp(budget ? budget : config_.minBudget, config_.minBudget, std::max(config_.minBudget, config_.maxBudget));
    costs_.clear();
    drawn_.clear();
    scene_.clear();
    limitedFrames_ = 0;
    settle_ = 0;
    lastCost_ = 0.0;
    lastDrawn_ = 0;
    lastAction_ = Action::Hold;
    adjustments_ = decisions_ = 0;
}

void FrameGovernor::setTargetFps(int fps) {
    fps = std::clamp(fps, 15, 360);
    if (fps == config_.targetFps) return;
    config_.targetFps = fps;
    costs_.clear(); // measured against the old target
    drawn_.clear();
    scene_.clear();
    limitedFrames_ = 0;
}

uint64_t FrameGovernor::proxyThreshold() const { return std::max(budget_, config_.minProxyThreshold); }

bool FrameGovernor::addFrame(const FrameSample& f) {
    if (config_.movingOnly && !f.moving) return false;
    const double cost = std::max(f.cpuMs, f.gpuMs);
    if (!(cost > 0.0) || cost > config_.maxFrameMs) return false;
    if (settle_ > 0) {
        --settle_;
        return false;
    }
    costs_.push_back(cost);
    drawn_.push_back(f.drawnTriangles);
    scene_.push_back(f.sceneTriangles);
    if (f.limited) ++limitedFrames_;
    if (int(costs_.size()) < config_.window) return false;
    const uint64_t before = budget_;
    decide();
    costs_.clear();
    drawn_.clear();
    scene_.clear();
    limitedFrames_ = 0;
    if (budget_ != before) {
        settle_ = config_.settleFrames;
        ++adjustments_;
        return true;
    }
    return false;
}

void FrameGovernor::decide() {
    ++decisions_;
    const double T = targetMs();
    const double C = medianOf(costs_);
    const uint64_t D = medianOf(drawn_);
    const uint64_t S = medianOf(scene_);
    const bool limited = limitedFrames_ * 2 > int(costs_.size());
    lastCost_ = C;
    lastDrawn_ = D;
    lastAction_ = Action::Hold;
    const uint64_t lo = config_.minBudget, hi = std::max(config_.minBudget, config_.maxBudget);
    const double aim = T * config_.aimFraction;
    if (C > T * config_.highFraction) {
        // Too slow: draw less. From what was actually drawn when the scene is smaller than the budget (a budget far
        // above the scene would take several steps to have any effect).
        const double factor = std::clamp(aim / C, config_.maxStepDown, 0.9);
        const uint64_t base = D > 0 ? std::min(budget_, D) : budget_;
        const uint64_t next = std::clamp(uint64_t(double(base) * factor), lo, hi);
        if (next < budget_) {
            budget_ = next;
            lastAction_ = Action::Lowered;
        }
    } else if (C < T * config_.lowFraction) {
        // Room to spare: draw more, but only where the budget is what holds the view back.
        if (!limited && !(D > 0 && double(D) >= 0.9 * double(budget_))) return;
        const double factor = std::clamp(aim / C, 1.05, config_.maxStepUp);
        uint64_t next = std::clamp(uint64_t(double(budget_) * factor), lo, hi);
        if (limited && S > 0 && next >= S) {
            // Past the scene's size the view draws everything: predict that frame (cost grows with what's drawn)
            // and only go there if it stays well inside the target; else stay just under the scene.
            const double predicted = D > 0 ? C * double(S) / double(D) : 1e9;
            if (predicted > T * config_.highFraction * 0.9) next = std::max(budget_, S > 1 ? S - 1 : S);
        }
        if (next > budget_) {
            budget_ = next;
            lastAction_ = Action::Raised;
        }
    }
}

} // namespace sf
