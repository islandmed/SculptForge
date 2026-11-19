// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Progress and cancel for a long operation that is run on behalf of an AI agent (the MCP server, cli/McpServer.h).
// The server installs an OpControl on the thread that runs the operation; the operation reports how far it is with
// opProgress() and polls opCancelled() at its cancel points. With no OpControl installed (the app, scripts, tests)
// both are no-ops, so core code can call them freely. A cancelled operation returns before it changes the scene.
#pragma once

#include <algorithm>
#include <atomic>
#include <functional>
#include <string>

namespace sf {

struct OpControl {
    // Called from the operation's thread with the fraction 0..1 of the whole call and a short status.
    std::function<void(float fraction, const std::string& message)> onProgress;
    // Set from another thread to ask the operation to stop at its next cancel point.
    std::atomic<bool> cancel{false};
    // True once an operation noticed `cancel` and stopped: the reply is then not sent (the client dropped the request).
    std::atomic<bool> honoured{false};
    // The window of the whole call that the operation now running owns (a batch gives each command its slice).
    float base = 0.0f, span = 1.0f;
};

inline OpControl*& currentOpControlSlot() {
    static thread_local OpControl* current = nullptr;
    return current;
}
inline OpControl* currentOpControl() { return currentOpControlSlot(); }

// Installs `control` for this thread until it goes out of scope (nesting restores the outer one).
class OpControlScope {
public:
    explicit OpControlScope(OpControl* control) : previous_(currentOpControlSlot()) { currentOpControlSlot() = control; }
    ~OpControlScope() { currentOpControlSlot() = previous_; }
    OpControlScope(const OpControlScope&) = delete;
    OpControlScope& operator=(const OpControlScope&) = delete;

private:
    OpControl* previous_;
};

// For the next stretch of work: its own 0..1 maps into [from, from + width] of what the caller has.
class OpProgressWindow {
public:
    OpProgressWindow(float from, float width) : control_(currentOpControl()) {
        if (!control_) return;
        base_ = control_->base;
        span_ = control_->span;
        control_->base = base_ + span_ * from;
        control_->span = span_ * width;
    }
    ~OpProgressWindow() {
        if (!control_) return;
        control_->base = base_;
        control_->span = span_;
    }
    OpProgressWindow(const OpProgressWindow&) = delete;
    OpProgressWindow& operator=(const OpProgressWindow&) = delete;

private:
    OpControl* control_;
    float base_ = 0.0f, span_ = 1.0f;
};

// How far the operation is (0..1 of its own work).
inline void opProgress(float fraction, const std::string& message = {}) {
    OpControl* c = currentOpControl();
    if (!c || !c->onProgress) return;
    c->onProgress(c->base + c->span * std::clamp(fraction, 0.0f, 1.0f), message);
}

// A cancel point: true when the caller asked to stop. The operation then returns without changing the scene.
inline bool opCancelled() {
    OpControl* c = currentOpControl();
    if (!c || !c->cancel.load(std::memory_order_relaxed)) return false;
    c->honoured.store(true, std::memory_order_relaxed);
    return true;
}

} // namespace sf
