// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Touch screens and trackpads (phoenix, Discord 11-05: pinch and two-finger rotate did nothing on macOS). SDL sends
// a finger event for each finger on a touch screen, and on macOS and Windows for each finger on a trackpad too; SDL
// 3.4 adds pinch events (a macOS trackpad's pinch). Two fingers that land over the 3D view navigate it:
//   touch screen: the point between them pans, spreading them zooms, twisting them rolls the view;
//   trackpad: spreading zooms and twisting rolls (two-finger scrolling stays the wheel, as before).
// One finger on a touch screen still sculpts through the mouse events SDL makes from it; its press waits kTouchHold
// (or until the finger moves) for a second finger, so starting a pinch never drops a dab. The pairing math is in
// core/TouchGesture.h. User guide: docs/guide/navigation.md.
#include "app/App.h"

#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>

namespace sf {

namespace {

constexpr float kTouchTwistStart = 0.17f; // radians (10 degrees) of twist before it rolls: a pinch alone doesn't
constexpr float kTouchPinchStart = 0.05f; // log scale a trackpad pinch must reach before its scroll is dropped
constexpr float kTrackpadAspect = 1.5f;   // a trackpad's width over its height (its coordinates are 0..1 each way;
                                          // Demo touch_pinch assumes the same)

double now() { return double(SDL_GetTicksNS()) * 1e-9; }

} // namespace

void App::touchEvent(const SDL_Event& e) {
#if SDL_VERSION_ATLEAST(3, 4, 0)
    if (e.type == SDL_EVENT_PINCH_BEGIN || e.type == SDL_EVENT_PINCH_UPDATE || e.type == SDL_EVENT_PINCH_END) {
        touchNativePinch_ = true;
        if (e.type == SDL_EVENT_PINCH_UPDATE && !ImGui::GetIO().WantCaptureMouse &&
            canvasRect().contains(mouseX_, mouseY_)) {
            camera_.zoom(pinchZoomSteps(e.pinch.scale));
            touchPinching_ = true;
        }
        if (e.type == SDL_EVENT_PINCH_END) touchPinching_ = false;
        return;
    }
#endif
    const SDL_TouchFingerEvent& f = e.tfinger;
    if (f.touchID == SDL_MOUSE_TOUCHID) return; // SDL's touches made from the mouse
    if (f.touchID == SDL_PEN_TOUCHID) { // ... and from the pen (off, SDL_HINT_PEN_TOUCH_EVENTS; a system SDL may not be)
        ++penDiag_.penTouchEvents;
        return;
    }
    if (e.type == SDL_EVENT_FINGER_DOWN && palmGuard()) return; // a palm (palmGuard): never part of a gesture
    if (e.type == SDL_EVENT_FINGER_UP || e.type == SDL_EVENT_FINGER_CANCELED) {
        touch_.up(f.fingerID);
        if (touch_.fingers() == 0) {
            touchTwisting_ = touchPinching_ = false;
            touchTwistSum_ = touchZoomSum_ = 0.0f;
            touchSwallowMouse_ = false; // (its mouse release came first; this one is for a release SDL never sent)
        }
        return;
    }
    glm::vec2 p;
    if (e.type == SDL_EVENT_FINGER_DOWN && touch_.fingers() == 0) {
        const SDL_TouchDeviceType t = f.touchID == kUiTestTrackpad ? SDL_TOUCH_DEVICE_INDIRECT_ABSOLUTE
                                                                  : SDL_GetTouchDeviceType(f.touchID);
        touchDirect_ = t != SDL_TOUCH_DEVICE_INDIRECT_ABSOLUTE && t != SDL_TOUCH_DEVICE_INDIRECT_RELATIVE;
    }
    if (touchDirect_) { // window coordinates, as the mouse's
        int w = 0, h = 0;
        SDL_GetWindowSize(window_, &w, &h);
        p = glm::vec2(f.x * float(w), f.y * float(h));
    } else {
        p = glm::vec2(f.x * kTrackpadAspect, f.y);
    }
    if (e.type == SDL_EVENT_FINGER_DOWN) {
        if (touch_.fingers() == 0) { // the first finger decides whether this touch is the view's
            const glm::vec2 at = touchDirect_ ? p : glm::vec2(mouseX_, mouseY_);
            touchNavigates_ = !ImGui::GetIO().WantCaptureMouse && canvasRect().contains(at.x, at.y);
        }
        const bool wasGesture = touch_.gesture();
        touch_.down(f.fingerID, p);
        if (touch_.gesture() && !wasGesture && touchNavigates_) {
            SDL_Log("touch: two-finger %s gesture", touchDirect_ ? "touch screen" : "trackpad");
            if (touchDirect_) {
                // The first finger's press never reaches the canvas; a stroke it already began ends here.
                touchPress_.held = false;
                touchSwallowMouse_ = true;
                if (drag_ != Drag::None) onMouseUp();
            }
        }
        return;
    }
    if (e.type == SDL_EVENT_FINGER_MOTION) {
        TwoFingerStep step;
        if (touch_.move(f.fingerID, p, step) && touchNavigates_) touchApply(step);
    }
}

void App::touchApply(const TwoFingerStep& step) {
    if (touchDirect_ && (step.pan.x != 0.0f || step.pan.y != 0.0f)) {
        const float h = canvasHeight();
        camera_.pan(step.pan.x, step.pan.y, h);
    }
    if (step.scale != 1.0f && (touchDirect_ || !touchNativePinch_)) {
        camera_.zoom(pinchZoomSteps(step.scale));
        touchZoomSum_ += std::log(step.scale);
    }
    // A twist rolls the view once it is clearly meant (a pinch's fingers turn a little on their own): then the
    // whole twist so far, so the view stays under the fingers.
    touchTwistSum_ += step.angle;
    float roll = 0.0f;
    if (touchTwisting_) roll = step.angle;
    else if (std::abs(touchTwistSum_) > kTouchTwistStart) {
        touchTwisting_ = true;
        roll = touchTwistSum_;
    }
    if (roll != 0.0f) {
        if (autoOrtho_) { // an axis view turned: back to perspective, as when orbiting with the mouse
            camera_.ortho = false;
            autoOrtho_ = false;
        }
        // Clockwise on screen (y down) turns the model clockwise, as the SpaceMouse's roll does.
        camera_.setOrientation(camera_.orientation() * glm::angleAxis(-roll, glm::vec3(0, 0, 1)));
        orbitStarted_ = false;
    }
    if (!touchDirect_ && (touchTwisting_ || std::abs(touchZoomSum_) > kTouchPinchStart)) touchPinching_ = true;
}

bool App::palmGuard() const {
#ifdef __ANDROID__
    return penInRange_ || now() < penNearUntil_;
#else
    return false; // (desktop touch screens with pens: once the Android one has testers' approval)
#endif
}

// A palm's touch: its mouse events never reach the canvas or the UI (App::event asks first), press to release.
bool App::palmMouseEvent(const SDL_Event& e) {
    if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.which == SDL_TOUCH_MOUSEID && palmGuard()) touchPalm_ = true;
    if (touchPalm_ && (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN || e.type == SDL_EVENT_MOUSE_MOTION ||
                       e.type == SDL_EVENT_MOUSE_BUTTON_UP)) {
        const SDL_MouseID which = e.type == SDL_EVENT_MOUSE_MOTION ? e.motion.which : e.button.which;
        if (which == SDL_TOUCH_MOUSEID) {
            if (e.type == SDL_EVENT_MOUSE_BUTTON_UP) touchPalm_ = false;
            return true;
        }
    }
    return false;
}

bool App::touchMouseEvent(const SDL_Event& e) {
    switch (e.type) {
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        if (e.button.which != SDL_TOUCH_MOUSEID || e.button.button != SDL_BUTTON_LEFT) return false;
        if (touchSwallowMouse_ || touch_.gesture()) return true;
        if (ImGui::GetIO().WantCaptureMouse || !canvasRect().contains(e.button.x, e.button.y)) return false;
        touchPress_ = {true, e.button.x, e.button.y, e.button.clicks, now()};
        mouseX_ = e.button.x;
        mouseY_ = e.button.y;
        redrawIn(kTouchHold); // (the loop wakes to let it through: touchTick)
        return true;
    case SDL_EVENT_MOUSE_MOTION:
        if (e.motion.which != SDL_TOUCH_MOUSEID) return false;
        if (touchSwallowMouse_ || touch_.gesture()) return true;
        if (touchPress_.held) {
            const float slop = 10.0f * uiScale_;
            if (std::hypot(e.motion.x - touchPress_.x, e.motion.y - touchPress_.y) < slop) return true;
            touchFlushPress(); // moving: a stroke, the motion follows
        }
        return false;
    case SDL_EVENT_MOUSE_BUTTON_UP:
        if (e.button.which != SDL_TOUCH_MOUSEID || e.button.button != SDL_BUTTON_LEFT) return false;
        if (touchSwallowMouse_) {
            touchSwallowMouse_ = false;
            if (drag_ != Drag::None) onMouseUp();
            return true;
        }
        if (touchPress_.held) touchFlushPress(); // a tap: one dab
        return false;
    default:
        return false;
    }
}

void App::touchFlushPress() {
    if (!touchPress_.held) return;
    touchPress_.held = false;
    lastInputPen_ = false;
    mouseClicks_ = touchPress_.clicks;
    onMouseDown(touchPress_.x, touchPress_.y, SDL_BUTTON_LEFT);
    mouseClicks_ = 1;
}

void App::touchTick() {
    if (touchPress_.held && now() - touchPress_.at >= kTouchHold) touchFlushPress();
}

} // namespace sf
