// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The Transform gizmo's All mode (Shift+W; fenix on Discord's idea): move, rotate and scale handles at once. Where the
// handles sit, in arrow lengths from the pivot, and which one a point on screen picks. The look follows fenix's design
// (separated handles, clean rings and arrows, nothing piled on the centre): each axis is a thin shaft with its scale
// ball part way out and the move cone at its end, well inside the rings (hidden while the axis points at the viewer);
// the plane squares between the shafts; then the rings, the view ring, and outermost four even-scale tabs on the
// screen diagonals. Nothing overlaps at any zoom or UI scale. Pure arithmetic (no ImGui), so the priorities can be
// unit tested.
#pragma once

#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

namespace sf {

namespace gizmo_all {

constexpr float kShaftLo = 0.16f, kShaftHi = 0.66f; // the arrows (their heads go on, on screen; the rings are at 1)
constexpr float kHeadPx = 15.0f;
constexpr float kPlaneLo = 0.26f, kPlaneHi = 0.4f;  // the plane squares, between the arrows
constexpr float kBall = 0.42f;                      // the scale balls, on the shafts before the heads
constexpr float kBallMin = 0.6f;                    // ...shown while the axis is at least this long on screen
constexpr float kBallPx = 6.0f;                     // radius
constexpr float kViewRing = 1.3f;
constexpr float kTabs = 1.44f;                      // the even-scale tabs' radius
constexpr float kTabHalf = 0.21f;                   // radians each side of a diagonal
constexpr float kCentrePx = 12.0f;                  // the free-move centre
constexpr float kPickPx = 8.0f;

// The handles, numbered as App::GizmoHandle.
enum Handle {
    None, MoveX, MoveY, MoveZ, PlaneYZ, PlaneXZ, PlaneXY, MoveView, RotateX, RotateY, RotateZ, RotateView,
    ScaleX, ScaleY, ScaleZ, ScaleUniform
};

// The gizmo as the view shows it, in pixels (screen y grows downwards).
struct Screen {
    glm::vec2 centre{0.0f};
    float unit = 105.0f; // one arrow length on screen, face-on (the rings' radius)
    float scale = 1.0f;  // the UI scale
    struct Axis {
        bool arrow = false; // false: pointing at the viewer
        glm::vec2 from{0.0f}, tip{0.0f};
        float depth = 0.0f;      // toward the viewer: the arrow (and ball) in front draws over the others
        float planeDepth = 0.0f; // the same for the plane square
        bool ball = false;
        glm::vec2 ballAt{0.0f};
        bool plane = false; // the square in the plane across this axis (false: seen edge-on)
        glm::vec2 quad[4];
        std::vector<glm::vec2> ring; // the ring about this axis, closed
        std::vector<bool> ringFront; // per point: on the half facing the viewer
    } axis[3];
};

inline float segmentDistance(glm::vec2 p, glm::vec2 a, glm::vec2 b) {
    const glm::vec2 v = b - a;
    const float l2 = glm::dot(v, v);
    const float t = l2 > 0.0f ? std::clamp(glm::dot(p - a, v) / l2, 0.0f, 1.0f) : 0.0f;
    return glm::length(p - (a + v * t));
}

inline bool insideQuad(glm::vec2 p, const glm::vec2 q[4]) {
    bool in = false;
    for (int i = 0, j = 3; i < 4; j = i++)
        if ((q[i].y > p.y) != (q[j].y > p.y) && p.x < (q[j].x - q[i].x) * (p.y - q[i].y) / (q[j].y - q[i].y) + q[i].x)
            in = !in;
    return in;
}

// A point on even-scale tab k (0 upper right, then counter-clockwise).
inline glm::vec2 tabPoint(const Screen& s, int k) {
    const float t = 0.7853982f + float(k) * 1.5707963f, r = kTabs * s.unit;
    return s.centre + glm::vec2(r * std::cos(t), -r * std::sin(t));
}

// The gizmo at pivot c with axes f (columns), L world units to an arrow's length, as `project` (world point ->
// pixels, false behind the camera) shows it; toEye points at the viewer. An axis pointing at the viewer hides its
// arrow and its ball (they would sit on the centre).
template <class Project>
Screen layout(Project project, const glm::vec3& c, const glm::mat3& f, float L, const glm::vec3& toEye, float unit,
              float scale) {
    Screen out;
    out.unit = unit;
    out.scale = scale;
    if (!project(c, out.centre)) return out;
    for (int i = 0; i < 3; ++i) {
        Screen::Axis& a = out.axis[i];
        a.depth = glm::dot(f[i], toEye);
        a.planeDepth = glm::dot(f[(i + 1) % 3] + f[(i + 2) % 3], toEye);
        glm::vec2 hi, end;
        if (project(c + f[i] * (kShaftLo * L), a.from) && project(c + f[i] * (kShaftHi * L), hi)) {
            const float len = glm::length(hi - a.from);
            a.arrow = len >= 10.0f * scale;
            if (a.arrow) a.tip = hi + (hi - a.from) / len * (kHeadPx * scale);
        }
        a.ball = a.arrow && project(c + f[i] * L, end) && glm::length(end - out.centre) >= kBallMin * unit &&
                 project(c + f[i] * (kBall * L), a.ballAt);
        const glm::vec3 u = f[(i + 1) % 3], v = f[(i + 2) % 3];
        const float lo = kPlaneLo * L, top = kPlaneHi * L;
        a.plane = std::abs(glm::dot(f[i], toEye)) >= 0.15f && project(c + u * lo + v * lo, a.quad[0]) &&
                  project(c + u * top + v * lo, a.quad[1]) && project(c + u * top + v * top, a.quad[2]) &&
                  project(c + u * lo + v * top, a.quad[3]);
        for (int k = 0; k <= 64; ++k) {
            const float t = float(k) / 64.0f * 6.2831853f;
            const glm::vec3 r = u * std::cos(t) + v * std::sin(t);
            glm::vec2 p;
            if (!project(c + r * L, p)) continue;
            a.ring.push_back(p);
            a.ringFront.push_back(glm::dot(r, toEye) > -0.05f);
        }
    }
    return out;
}

// The handle under p, as the drawing stacks them: the centre, the scale balls and the even-scale tabs each win anywhere
// on themselves; then an arrow on its line; a plane square anywhere inside it; an arrow within the pick distance;
// then the nearest ring (back halves count further away). Where two arrows or two squares cover each other (their
// axes in line on screen) the one in front, drawn on top, is picked.
inline Handle pick(const Screen& s, glm::vec2 p) {
    const float pickPx = kPickPx * s.scale;
    const float r = glm::length(p - s.centre);
    if (r < kCentrePx * s.scale) return MoveView;
    int ball = -1; // two balls in line on screen: the one in front, drawn on top
    for (int i = 0; i < 3; ++i) {
        const Screen::Axis& a = s.axis[i];
        if (a.ball && glm::length(p - a.ballAt) < (kBallPx + 3.0f) * s.scale && (ball < 0 || a.depth > s.axis[ball].depth))
            ball = i;
    }
    if (ball >= 0) return Handle(ScaleX + ball);
    if (std::abs(r - kTabs * s.unit) < pickPx * 0.75f) {
        const float q = 1.5707963f, ang = std::atan2(s.centre.y - p.y, p.x - s.centre.x) - q * 0.5f;
        if (std::abs(ang - q * std::round(ang / q)) < kTabHalf + 0.05f) return ScaleUniform;
    }
    int arrow = -1;
    float arrowD = pickPx;
    for (int i = 0; i < 3; ++i) {
        if (!s.axis[i].arrow) continue;
        const float d = segmentDistance(p, s.axis[i].from, s.axis[i].tip);
        if (d < arrowD - 1.0f || (d < arrowD + 1.0f && arrow >= 0 && s.axis[i].depth > s.axis[arrow].depth)) {
            arrowD = std::min(d, arrowD);
            arrow = i;
        }
    }
    if (arrow >= 0 && arrowD < pickPx * 0.5f) return Handle(MoveX + arrow);
    int plane = -1;
    for (int i = 0; i < 3; ++i)
        if (s.axis[i].plane && insideQuad(p, s.axis[i].quad) && (plane < 0 || s.axis[i].planeDepth > s.axis[plane].planeDepth))
            plane = i;
    if (plane >= 0) return Handle(PlaneYZ + plane);
    if (arrow >= 0) return Handle(MoveX + arrow);
    Handle best = None;
    float bestD = pickPx;
    auto consider = [&](Handle h, float d) {
        if (d < bestD) {
            bestD = d;
            best = h;
        }
    };
    for (int i = 0; i < 3; ++i) {
        const Screen::Axis& a = s.axis[i];
        for (size_t k = 1; k < a.ring.size(); ++k)
            consider(Handle(RotateX + i), segmentDistance(p, a.ring[k - 1], a.ring[k]) + (a.ringFront[k] ? 0.0f : pickPx * 0.6f));
    }
    consider(RotateView, std::abs(r - kViewRing * s.unit));
    return best;
}

} // namespace gizmo_all

} // namespace sf
