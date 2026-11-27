// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The corner gizmo (Draw > Camera > Corner gizmo): the soft disc in a top corner of the canvas with either the X / Y / Z
// axes (drawAxisGizmo, App.cpp) or the orientation widget: the same axes through a ball of dark glass, a dot at each
// end and no letters (OrientationWidget.h). Click a dot to turn the camera to look from that side with a short eased
// move (the dot facing you again: the opposite side), drag the widget to orbit, double-click to frame the model. The
// Classic layout shows the axes by default, the SculptForge layout the widget; the preference picks either, hides the
// corner, or moves it to the top right. Our own look: dark glass, the forge accent under the pointer, the sides'
// names in our words under the disc.
#include "app/App.h"
#include "app/I18n.h"
#include "app/Theme.h"
#include "app/Ui.h"

#include <IconsLucide.h>
#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cmath>
#include <string>

namespace sf {

namespace {

ImU32 withAlpha(ImU32 col, float a) {
    return (col & 0x00ffffffu) | (ImU32(std::clamp(a, 0.0f, 1.0f) * 255.0f + 0.5f) << 24);
}

constexpr ImU32 kAxisColours[3] = {IM_COL32(236, 84, 92, 255), IM_COL32(124, 196, 64, 255), IM_COL32(64, 140, 236, 255)};
constexpr const char* kAxisNames[3] = {"X", "Y", "Z"};
ImU32 kAccent() { return theme::ink(IM_COL32(240, 136, 46, 255)); } // theme::accent, the forge ember (white in Monochrome)

} // namespace

bool App::orientationWidgetOn() const {
    return uiPrefs_.cornerWidget < 0 ? uiPrefs_.shellLayout == 1 : uiPrefs_.cornerWidget == 1;
}

int App::navLockAxis() const {
    static constexpr int lockAxes[4] = {-1, 1, 0, 2}; // UiPrefs::orbitLock: free, Y, X, Z
    return lockAxes[std::clamp(forceTurntable_ ? 1 : uiPrefs_.orbitLock, 0, 3)];
}

void App::drawNavDisc(ImDrawList* dl, glm::vec2 c, float radius, bool underPointer, bool light) {
    // The disc: lighter than a dark background, darker than a light one; a hairline edge gives it a shape.
    const ImU32 tint = light ? IM_COL32(0, 0, 0, 0) : IM_COL32(255, 255, 255, 0);
    const ImVec2 centre(c.x, c.y);
    dl->AddCircleFilled(centre, radius, withAlpha(tint, underPointer ? 0.17f : light ? 0.08f : 0.07f), 48);
    dl->AddCircle(centre, radius, withAlpha(tint, underPointer ? 0.22f : 0.10f), 48, 1.0f * uiScale_);
}

void App::drawNavName(ImDrawList* dl, glm::vec2 c, float radius, const char* text, bool light) {
    // The view's name ("Front view", or the side under the pointer), small, under the disc.
    const float size = ImGui::GetFontSize() * 0.85f;
    const ImVec2 ts = ImGui::GetFont()->CalcTextSizeA(size, FLT_MAX, 0.0f, text);
    const ImVec2 at(std::round(c.x - ts.x * 0.5f), std::round(c.y + radius + 4.0f * uiScale_));
    dl->AddText(ImGui::GetFont(), size, ImVec2(at.x + 1.0f, at.y + 1.0f), light ? IM_COL32(255, 255, 255, 110) : IM_COL32(0, 0, 0, 150), text);
    dl->AddText(ImGui::GetFont(), size, at, light ? IM_COL32(30, 30, 34, 230) : IM_COL32(232, 232, 236, 220), text);
}

void App::drawNavLockBadge(ImDrawList* dl, glm::vec2 c, float radius, int lockAxis, bool light) {
    // The orbit lock's badge: a lock and the axis letter in its colour, on a pill at the disc's lower right.
    if (lockAxis < 0 || lockAxis > 2) return;
    ImFont* font = fontBold_ ? fontBold_ : ImGui::GetFont();
    const float size = ImGui::GetFontSize() * 0.8f;
    ImFont* f = ImGui::GetFont();
    const ImVec2 iconSize = f->CalcTextSizeA(size, FLT_MAX, 0.0f, ICON_LC_LOCK);
    const ImVec2 nameSize = font->CalcTextSizeA(size, FLT_MAX, 0.0f, kAxisNames[lockAxis]);
    const float gap = 2.0f * uiScale_, padX = 4.0f * uiScale_, padY = 1.0f * uiScale_;
    const float w = iconSize.x + gap + nameSize.x + 2.0f * padX, h = std::max(iconSize.y, nameSize.y) + 2.0f * padY;
    const ImVec2 a(std::round(c.x + radius * 0.62f), std::round(c.y + radius * 0.62f));
    dl->AddRectFilled(a, ImVec2(a.x + w, a.y + h), light ? IM_COL32(255, 255, 255, 200) : IM_COL32(20, 20, 24, 190), h * 0.5f);
    dl->AddRect(a, ImVec2(a.x + w, a.y + h), withAlpha(kAxisColours[lockAxis], 0.8f), h * 0.5f, 0, 1.0f * uiScale_);
    dl->AddText(f, size, ImVec2(a.x + padX, a.y + padY), light ? IM_COL32(30, 30, 34, 230) : IM_COL32(232, 232, 236, 230), ICON_LC_LOCK);
    dl->AddText(font, size, ImVec2(a.x + padX + iconSize.x + gap, a.y + padY), kAxisColours[lockAxis], kAxisNames[lockAxis]);
}

int App::orientationWidgetHover(float x, float y) const {
    if (!uiPrefs_.showAxes || !orientationWidgetOn()) return -1;
    return axisGizmoHit(axisGizmoLayout(), x, y, kAxisGizmoHitRadius * axisGizmoUnit());
}

void App::drawCornerGizmo() {
    if (!orientationWidgetOn()) {
        drawAxisGizmo();
        return;
    }
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    const float unit = axisGizmoUnit();
    const glm::vec2 c = axisGizmoCentre();
    const float discR = kAxisGizmoDisc * unit;
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const bool light = viewBackgroundLight();
    // The dot under the pointer lights up; a pressed one stays lit until the pointer drags it away.
    const bool free = !ImGui::GetIO().WantCaptureMouse && drag_ == Drag::None;
    int hover = -1;
    if (drag_ == Drag::NavWidget) hover = navPress_.end;
    else if (free) hover = orientationWidgetHover(mouse.x, mouse.y);
    const bool onDisc = drag_ == Drag::NavWidget || (free && (hover >= 0 || axisGizmoOnDisc(c, discR, mouse.x, mouse.y)));
    drawNavDisc(dl, c, discR, onDisc, light);
    const auto ends = axisGizmoLayout();
    const int lockAxis = navLockAxis();
    drawOrientationWidget(dl, c, discR, light, ends, hover, lockAxis);

    // Under the disc: the side under the pointer ("Right"), else the exact side view's name ("Right view").
    const char* name = nullptr;
    if (hover >= 0) name = tr(orientationWidgetSideName(ends[size_t(hover)].axis, ends[size_t(hover)].negative));
    else if (const int aligned = axisGizmoAligned(ends); aligned >= 0) name = tr(axisGizmoView(aligned % 3, aligned >= 3).label);
    if (name) drawNavName(dl, c, discR, name, light);
    if (lockAxis >= 0) drawNavLockBadge(dl, c, discR, lockAxis, light);
}

void App::drawOrientationWidget(ImDrawList* dl, glm::vec2 c, float discR, bool light, const std::array<AxisGizmoEnd, 6>& layout,
                                int hover, int lockAxis) {
    // The three axes through a ball of dark glass: the far halves and their dots first, then the ball (shaded from the
    // upper left, a soft highlight), then the near halves and dots on top, so the ball hides what runs behind it. A
    // positive end is a dot in its axis colour, a negative end a smaller, dimmer one; the far ends shrink and fade as
    // the axes' do. No letters: the side's name shows under the disc as the pointer rests on a dot. The dot under the
    // pointer glows in the accent with a bright ring.
    const float unit = axisGizmoUnit();
    const float ballR = kOrientationWidgetBall * unit;
    const ImVec2 centre(c.x, c.y);
    auto ends = layout;
    std::sort(ends.begin(), ends.end(), [](const AxisGizmoEnd& a, const AxisGizmoEnd& b) { return a.depth < b.depth; });
    const ImU32 ringCol = light ? IM_COL32(20, 20, 24, 220) : IM_COL32(255, 255, 255, 230);

    // The locked axis (View > Orbit lock), end to end through the disc, under everything.
    if (lockAxis >= 0)
        for (const AxisGizmoEnd& e : layout)
            if (e.axis == lockAxis && !e.negative) {
                const glm::vec2 d = (e.at - c) * ((discR - 3.0f * unit) / (kAxisGizmoLength * unit));
                dl->AddLine(ImVec2(c.x - d.x, c.y - d.y), ImVec2(c.x + d.x, c.y + d.y), withAlpha(kAxisColours[lockAxis], 0.55f), 1.5f * unit);
            }

    auto drawEnd = [&](const AxisGizmoEnd& e, bool near) {
        const ImVec2 tip(e.at.x, e.at.y);
        const float alpha = axisGizmoEndAlpha(e.depth) * (e.negative ? 0.75f : 1.0f);
        const float r = orientationWidgetDotRadius(e, unit);
        const ImU32 col = withAlpha(kAxisColours[e.axis], alpha);
        const bool isHover = hover >= 0 && layout[size_t(hover)].axis == e.axis && layout[size_t(hover)].negative == e.negative;
        // The rod from the ball's edge (near ends) or from the centre (far ends: the ball covers the rest) to the dot.
        const glm::vec2 d = e.at - c;
        const float len = glm::length(d);
        if (len > 1.0f) {
            const glm::vec2 from = near && len > ballR ? c + d * (ballR * 0.92f / len) : c;
            const glm::vec2 to = c + d * (std::max(len - r, 0.0f) / len);
            dl->AddLine(ImVec2(from.x, from.y), ImVec2(to.x, to.y), withAlpha(kAxisColours[e.axis], alpha * (near ? 0.9f : 0.6f)),
                        (e.negative ? 1.4f : 2.0f) * unit);
        }
        dl->AddCircleFilled(tip, r, isHover ? kAccent() : col, 24);
        if (!e.negative) // a glass highlight on the big dots
            dl->AddCircleFilled(ImVec2(tip.x - r * 0.3f, tip.y - r * 0.3f), r * 0.32f, IM_COL32(255, 255, 255, int(70 * alpha)), 12);
        if (isHover) dl->AddCircle(tip, r + 2.0f * unit, ringCol, 24, 1.5f * unit);
    };

    for (const AxisGizmoEnd& e : ends)
        if (e.depth < 0.0f) drawEnd(e, false);
    // The ball: dark glass with a rim and a highlight towards the light.
    const ImU32 glass = light ? IM_COL32(62, 64, 72, 235) : IM_COL32(44, 47, 54, 235);
    dl->AddCircleFilled(centre, ballR, glass, 40);
    dl->AddCircleFilled(ImVec2(c.x - ballR * 0.3f, c.y - ballR * 0.32f), ballR * 0.42f, IM_COL32(255, 255, 255, 34), 24);
    dl->AddCircle(centre, ballR, light ? IM_COL32(20, 20, 24, 90) : IM_COL32(255, 255, 255, 60), 40, 1.0f * uiScale_);
    for (const AxisGizmoEnd& e : ends)
        if (e.depth >= 0.0f) drawEnd(e, true);
}

bool App::cornerGizmoMouseDown(float x, float y) {
    // The orientation widget takes a plain press anywhere on its disc: a click (release without moving) on a dot turns
    // to that side, a double-click frames, a drag past the slop orbits (navWidgetMove). Alt / Ctrl / Shift presses keep
    // their meaning (pan, mask, roll...). Without the widget, the axes (axisGizmoMouseDown).
    if (uiPrefs_.showAxes && orientationWidgetOn()) {
        const bool plain = !((SDL_GetModState() | keyMods()) & (SDL_KMOD_SHIFT | SDL_KMOD_CTRL | SDL_KMOD_ALT | SDL_KMOD_GUI));
        const int end = orientationWidgetHover(x, y);
        if (plain && (end >= 0 || axisGizmoOnDisc(axisGizmoCentre(), kAxisGizmoDisc * axisGizmoUnit(), x, y))) {
            navPress_.end = end;
            navPress_.x = x;
            navPress_.y = y;
            navPress_.clicks = mouseClicks_;
            drag_ = Drag::NavWidget;
            rightClick_.pending = false;
            return true;
        }
    }
    return axisGizmoMouseDown(x, y);
}

void App::navWidgetMove(float x, float y, float /*dx*/, float /*dy*/, bool pen) {
    if (std::hypot(x - navPress_.x, y - navPress_.y) < kOrientationWidgetDragPx * uiScale_) return;
    // Past the slop: an orbit about the target from the press (as a drag on the axes' disc), no click on release.
    drag_ = Drag::Orbit;
    orbitPivotPicked_ = true; // (about the target: the disc is not a point on the model)
    cancelCameraMove();
    orbitBy(x, y, x - navPress_.x, y - navPress_.y, !pen);
}

void App::navWidgetRelease() { navWidgetClick(navPress_.end, navPress_.clicks); }

void App::navWidgetClick(int end, int clicks) {
    if (clicks >= 2) { // a double-click frames, and only frames: the first click's turn eases back to where the view was
        if (navLastClick_.valid) {
            navLastClick_.valid = false;
            camera_.ortho = navLastClick_.ortho;
            autoOrtho_ = navLastClick_.autoOrtho;
            startCameraMove(navLastClick_.yaw, navLastClick_.pitch, false);
        }
        if (const Action* a = actions_.find("view.frame"); a && a->run) a->run();
        return;
    }
    if (end < 0) return; // the disc beside the dots: nothing (a drag there orbits)
    const auto ends = axisGizmoLayout();
    const int target = axisGizmoSnapEnd(ends, end); // the dot facing you again: the opposite side
    if (target < 0) return;
    const AxisGizmoView v = axisGizmoView(ends[size_t(target)].axis, ends[size_t(target)].negative);
    navLastClick_ = {true, camera_.ortho, autoOrtho_, camera_.yaw, camera_.pitch};
    startCameraMove(glm::radians(v.yaw), glm::radians(v.pitch), true);
}

void App::startCameraMove(float yawRadians, float pitchRadians, bool orthoAtEnd) {
    CameraMove& m = cameraMove_;
    m.active = true;
    m.orthoAtEnd = orthoAtEnd;
    m.t = 0.0f;
    m.pitch0 = camera_.pitch;
    m.roll0 = camera_.roll;
    m.yaw1 = yawRadians;
    m.pitch1 = pitchRadians;
    // The short way round: the start is the current yaw, brought within a half turn of the end (the same view), so
    // the path never goes the long way; the end is set exactly.
    float delta = std::fmod(m.yaw1 - camera_.yaw, glm::two_pi<float>());
    if (delta > glm::pi<float>()) delta -= glm::two_pi<float>();
    if (delta <= -glm::pi<float>()) delta += glm::two_pi<float>();
    m.yaw0 = m.yaw1 - delta;
    m.setYaw = camera_.yaw;
    m.setPitch = camera_.pitch;
    m.setRoll = camera_.roll;
    if (uiPrefs_.reduceMotion) animateCameraMove(kOrientationWidgetMoveSeconds);
}

void App::animateCameraMove(float dt) {
    CameraMove& m = cameraMove_;
    if (!m.active) return;
    // Someone else turned the view (an orbit drag, a numpad step, a view action): the move ends where it is.
    if (camera_.yaw != m.setYaw || camera_.pitch != m.setPitch || camera_.roll != m.setRoll) {
        m.active = false;
        return;
    }
    m.t += std::max(dt, 0.0f);
    const float u = orientationWidgetEase(m.t / kOrientationWidgetMoveSeconds);
    if (m.t >= kOrientationWidgetMoveSeconds) {
        camera_.yaw = m.yaw1;
        camera_.pitch = m.pitch1;
        camera_.roll = 0.0f;
        m.active = false;
        if (m.orthoAtEnd && !camera_.ortho) { // a side view: orthographic, as the numpad views (turning the view undoes it)
            camera_.ortho = true;
            autoOrtho_ = true;
        }
    } else {
        camera_.yaw = glm::mix(m.yaw0, m.yaw1, u);
        camera_.pitch = glm::mix(m.pitch0, m.pitch1, u);
        camera_.roll = m.roll0 * (1.0f - u);
    }
    m.setYaw = camera_.yaw;
    m.setPitch = camera_.pitch;
    m.setRoll = camera_.roll;
}

} // namespace sf
