// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The corner gizmo (Draw > Camera > Corner gizmo): the soft disc in the canvas corner with either the X / Y / Z axes
// (drawAxisGizmo, App.cpp) or the orientation widget, a small cube that turns with the view (OrientationWidget.h).
// Click one of the cube's faces, edges or corners to turn the camera to that view with a short eased move, drag it
// to orbit, double-click to frame the model. The Classic layout shows the axes by default, the SculptForge layout
// the widget; the preference picks either, or hides the corner. Our own look: dark tinted glass shaded by a fixed
// light, the forge accent under the pointer, the views' names in our words under the disc.
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

ImU32 mixColour(ImU32 a, ImU32 b, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    const int r = int(((a >> IM_COL32_R_SHIFT) & 255) * (1.0f - t) + ((b >> IM_COL32_R_SHIFT) & 255) * t + 0.5f);
    const int g = int(((a >> IM_COL32_G_SHIFT) & 255) * (1.0f - t) + ((b >> IM_COL32_G_SHIFT) & 255) * t + 0.5f);
    const int bl = int(((a >> IM_COL32_B_SHIFT) & 255) * (1.0f - t) + ((b >> IM_COL32_B_SHIFT) & 255) * t + 0.5f);
    return IM_COL32(r, g, bl, 255);
}

ImU32 scaleColour(ImU32 c, float k) {
    const int r = int(std::clamp(((c >> IM_COL32_R_SHIFT) & 255) * k, 0.0f, 255.0f));
    const int g = int(std::clamp(((c >> IM_COL32_G_SHIFT) & 255) * k, 0.0f, 255.0f));
    const int b = int(std::clamp(((c >> IM_COL32_B_SHIFT) & 255) * k, 0.0f, 255.0f));
    return IM_COL32(r, g, b, 255);
}

constexpr ImU32 kAxisColours[3] = {IM_COL32(236, 84, 92, 255), IM_COL32(124, 196, 64, 255), IM_COL32(64, 140, 236, 255)};
constexpr const char* kAxisNames[3] = {"X", "Y", "Z"};
constexpr ImU32 kAccent = IM_COL32(240, 136, 46, 255); // theme::accent, the forge ember

// The corner index (bit 1 +X, 2 +Y, 4 +Z) of a cube point by its signs.
int cornerIndex(float x, float y, float z) { return (x > 0.0f ? 1 : 0) | (y > 0.0f ? 2 : 0) | (z > 0.0f ? 4 : 0); }

// How wide (horizontal = true) or tall a convex quad is along the line through `at`: the room a label has there.
float quadChord(const ImVec2 quad[4], ImVec2 at, bool horizontal) {
    float lo = 1e9f, hi = -1e9f;
    for (int k = 0; k < 4; ++k) {
        const ImVec2& a = quad[k];
        const ImVec2& b = quad[(k + 1) % 4];
        const float a1 = horizontal ? a.y : a.x, b1 = horizontal ? b.y : b.x, line = horizontal ? at.y : at.x;
        if ((a1 <= line) == (b1 <= line)) continue; // the edge does not cross the line
        const float t = (line - a1) / (b1 - a1);
        const float x = horizontal ? a.x + (b.x - a.x) * t : a.y + (b.y - a.y) * t;
        lo = std::min(lo, x);
        hi = std::max(hi, x);
    }
    return hi > lo ? hi - lo : 0.0f;
}

// A side's name in the interface language.
std::string sideLabel(int axis, bool negative) { return tr(orientationWidgetSideName(axis, negative)); }

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

OrientationWidgetPick App::orientationWidgetHover(float x, float y) const {
    if (!uiPrefs_.showAxes || !orientationWidgetOn()) return {};
    return orientationWidgetPick(glm::mat3(camera_.view()), axisGizmoCentre(), kOrientationWidgetHalf * axisGizmoUnit(), x, y);
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
    // The target under the pointer lights up; a pressed one stays lit until the pointer drags it away.
    const bool free = !ImGui::GetIO().WantCaptureMouse && drag_ == Drag::None;
    OrientationWidgetPick hover;
    if (drag_ == Drag::NavWidget) hover = navPress_.pick;
    else if (free) hover = orientationWidgetHover(mouse.x, mouse.y);
    const bool onDisc = drag_ == Drag::NavWidget || (free && (hover.hit() || axisGizmoOnDisc(c, discR, mouse.x, mouse.y)));
    drawNavDisc(dl, c, discR, onDisc, light);

    // The locked axis (View > Orbit lock), end to end through the disc under the cube.
    const int lockAxis = navLockAxis();
    const glm::mat3 rot(camera_.view());
    if (lockAxis >= 0) {
        glm::vec3 axis(0.0f);
        axis[lockAxis] = 1.0f;
        const glm::vec3 d = rot * axis;
        const glm::vec2 s = glm::vec2(d.x, -d.y) * (discR - 3.0f * unit);
        dl->AddLine(ImVec2(c.x - s.x, c.y - s.y), ImVec2(c.x + s.x, c.y + s.y), withAlpha(kAxisColours[lockAxis], 0.55f), 1.5f * unit);
    }

    drawOrientationWidget(dl, c, discR, light, hover);

    // Under the disc: the side(s) under the pointer ("Right / Top / Front"), else the exact side view's name.
    std::string name;
    if (hover.hit()) {
        for (int a = 0; a < 3; ++a) {
            if (hover.direction[a] == 0) continue;
            if (!name.empty()) name += " / ";
            name += sideLabel(a, hover.direction[a] < 0);
        }
    } else if (const int aligned = axisGizmoAligned(axisGizmoLayout()); aligned >= 0) {
        name = tr(axisGizmoView(aligned % 3, aligned >= 3).label);
    }
    if (!name.empty()) drawNavName(dl, c, discR, name.c_str(), light);
    if (lockAxis >= 0) drawNavLockBadge(dl, c, discR, lockAxis, light);
}

void App::drawOrientationWidget(ImDrawList* dl, glm::vec2 c, float /*discRadius*/, bool light, const OrientationWidgetPick& hover) {
    // A cube of dark tinted glass: each face carries its axis family's colour (X red, Y green, Z blue, as the axes),
    // shaded by a fixed light from the upper left so it reads as a solid; hairline edges, dots at the corners; the
    // sides' names on the faces that have room for them. The face, edge or corner under the pointer glows in the
    // accent. Only what faces the viewer is drawn (a convex solid: no sorting needed).
    const float unit = axisGizmoUnit();
    const float half = kOrientationWidgetHalf * unit;
    const glm::mat3 rot(camera_.view());
    const auto corners = orientationWidgetCorners(rot, c, half);
    const glm::vec3 lightDir = glm::normalize(glm::vec3(-0.35f, 0.55f, 0.75f));
    const ImU32 glass = light ? IM_COL32(58, 60, 68, 255) : IM_COL32(50, 53, 60, 255);
    const ImU32 lineCol = light ? IM_COL32(20, 20, 24, 255) : IM_COL32(255, 255, 255, 255);
    ImFont* font = fontBold_ ? fontBold_ : ImGui::GetFont();
    const float fontSize = ImGui::GetFontSize() * 0.78f * std::min(1.0f, unit / std::max(uiScale_, 0.01f));

    bool faceVisible[6]; // index: axis + 3 * negative
    for (int a = 0; a < 3; ++a)
        for (int neg = 0; neg < 2; ++neg) faceVisible[a + 3 * neg] = orientationWidgetFaceVisible(rot, a, neg != 0);
    auto faceIsVisible = [&](int axis, float sign) { return faceVisible[axis + (sign < 0.0f ? 3 : 0)]; };

    // Faces.
    for (int a = 0; a < 3; ++a)
        for (int neg = 0; neg < 2; ++neg) {
            if (!faceVisible[a + 3 * neg]) continue;
            const float s = neg ? -1.0f : 1.0f;
            const int b = (a + 1) % 3, d = (a + 2) % 3;
            glm::vec3 n(0.0f);
            n[a] = s;
            ImVec2 quad[4];
            static constexpr float cyc[4][2] = {{-1.0f, -1.0f}, {1.0f, -1.0f}, {1.0f, 1.0f}, {-1.0f, 1.0f}};
            for (int k = 0; k < 4; ++k) {
                glm::vec3 p(0.0f);
                p[a] = s;
                p[b] = cyc[k][0];
                p[d] = cyc[k][1];
                const OrientationWidgetPoint& q = corners[size_t(cornerIndex(p.x, p.y, p.z))];
                quad[k] = ImVec2(q.at.x, q.at.y);
            }
            const glm::vec3 nv = rot * n;
            const float lit = 0.62f + 0.38f * std::max(0.0f, glm::dot(nv, lightDir));
            const bool isHover = hover.axes == 1 && hover.direction[a] == int(s) && hover.direction[b] == 0 && hover.direction[d] == 0;
            ImU32 col = scaleColour(mixColour(glass, kAxisColours[a], 0.34f), lit);
            if (isHover) col = mixColour(col, kAccent, 0.72f);
            dl->AddConvexPolyFilled(quad, 4, withAlpha(col, 0.94f));
            // The side's name, when it fits the face where it sits (the face's width and height through its centre on
            // screen, so a foreshortened face's label never runs into its neighbour's).
            if (nv.z > 0.45f) {
                const std::string label = sideLabel(a, neg != 0);
                const char* text = label.c_str();
                const ImVec2 ts = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, text);
                const OrientationWidgetPoint centre = orientationWidgetProject(rot, c, half, n);
                const ImVec2 at(centre.at.x, centre.at.y);
                if (ts.x <= 0.82f * quadChord(quad, at, true) && ts.y <= 0.9f * quadChord(quad, at, false)) {
                    const ImU32 textCol = isHover ? IM_COL32(16, 16, 20, 240) : withAlpha(IM_COL32(236, 236, 240, 255), 0.55f + 0.45f * lit);
                    dl->AddText(font, fontSize, ImVec2(std::round(centre.at.x - ts.x * 0.5f), std::round(centre.at.y - ts.y * 0.5f)), textCol, text);
                }
            }
        }

    // Edges: the 12 pairs of corners one bit apart, where a visible face borders them.
    for (int i = 0; i < 8; ++i)
        for (int bit = 0; bit < 3; ++bit) {
            if (i & (1 << bit)) continue; // (each edge once, from its - end)
            const int j = i | (1 << bit);
            const glm::vec3 p = orientationWidgetCorner(i);
            const int b = (bit + 1) % 3, d = (bit + 2) % 3;
            if (!faceIsVisible(b, p[b]) && !faceIsVisible(d, p[d])) continue;
            const ImVec2 p0(corners[size_t(i)].at.x, corners[size_t(i)].at.y), p1(corners[size_t(j)].at.x, corners[size_t(j)].at.y);
            const bool isHover = hover.axes == 2 && hover.direction[bit] == 0 && hover.direction[b] == int(p[b]) && hover.direction[d] == int(p[d]);
            if (isHover) {
                dl->AddLine(p0, p1, kAccent, 3.0f * unit);
                dl->AddCircleFilled(p0, 1.5f * unit, kAccent, 12);
                dl->AddCircleFilled(p1, 1.5f * unit, kAccent, 12);
            } else {
                dl->AddLine(p0, p1, withAlpha(lineCol, light ? 0.35f : 0.30f), 1.0f * unit);
            }
        }

    // Corners: a dot where a visible face has one; the one under the pointer glows.
    for (int i = 0; i < 8; ++i) {
        const glm::vec3 p = orientationWidgetCorner(i);
        if (!faceIsVisible(0, p.x) && !faceIsVisible(1, p.y) && !faceIsVisible(2, p.z)) continue;
        const ImVec2 at(corners[size_t(i)].at.x, corners[size_t(i)].at.y);
        const bool isHover = hover.axes == 3 && hover.direction == glm::ivec3(int(p.x), int(p.y), int(p.z));
        if (isHover) {
            dl->AddCircleFilled(at, 3.0f * unit, kAccent, 16);
            dl->AddCircle(at, 4.6f * unit, withAlpha(light ? IM_COL32(20, 20, 24, 255) : IM_COL32(255, 255, 255, 255), 0.85f), 16, 1.2f * unit);
        } else {
            dl->AddCircleFilled(at, 1.5f * unit, withAlpha(lineCol, 0.55f), 12);
        }
    }
}

bool App::cornerGizmoMouseDown(float x, float y) {
    // The orientation widget takes a plain press anywhere on its disc: a click (release without moving) turns to
    // the face, edge or corner under it, a double-click frames, a drag past the slop orbits (navWidgetMove). Alt /
    // Ctrl / Shift presses keep their meaning (pan, mask, roll...). Without the widget, the axes (axisGizmoMouseDown).
    if (uiPrefs_.showAxes && orientationWidgetOn()) {
        const bool plain = !((SDL_GetModState() | keyMods()) & (SDL_KMOD_SHIFT | SDL_KMOD_CTRL | SDL_KMOD_ALT | SDL_KMOD_GUI));
        if (plain && axisGizmoOnDisc(axisGizmoCentre(), kAxisGizmoDisc * axisGizmoUnit(), x, y)) {
            navPress_.pick = orientationWidgetHover(x, y);
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

void App::navWidgetRelease() {
    navWidgetClick(navPress_.pick, navPress_.clicks);
}

void App::navWidgetClick(const OrientationWidgetPick& pick, int clicks) {
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
    if (!pick.hit()) return; // the disc beside the cube: nothing (a drag there orbits)
    const glm::ivec3 target = orientationWidgetClickTarget(pick.direction, pick.axes, camera_.forward());
    const OrientationWidgetView v = orientationWidgetView(target);
    const bool face = pick.axes == 1;
    navLastClick_ = {true, camera_.ortho, autoOrtho_, camera_.yaw, camera_.pitch};
    if (!face && autoOrtho_) { // an edge or corner view is a turned view: back to perspective, as orbiting does
        camera_.ortho = false;
        autoOrtho_ = false;
    }
    startCameraMove(glm::radians(v.yaw), glm::radians(v.pitch), face);
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
        if (m.orthoAtEnd && !camera_.ortho) { // a face: orthographic, as the numpad views (turning the view undoes it)
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
