// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The orientation widget in the canvas corner (Draw > Camera > Corner gizmo > Orientation): a small cube that turns
// with the view. A click on one of its faces, edges or corners turns the camera to look from there with a short
// eased move, a drag on it orbits, a double-click frames the model. This header is the arithmetic (no ImGui): where
// the cube's corners land on screen, which face / edge / corner the pointer is over, the view each one stands for,
// and the easing of the camera's move; App.cpp draws it and runs the move. Our own look: dark tinted glass, the
// forge accent under the pointer, the views' names in our words. Unit tested (tests/test_orientation_widget.cpp).
#pragma once

#include "app/AxisGizmo.h"

#include <glm/glm.hpp>

#include <array>
#include <cmath>
#include <cstddef>
#include <utility>

namespace sf {

// Sizes in unscaled pixels at Medium, multiplied by the unit (UI scale x Gizmo size, as the axes): the cube's half
// edge on screen and the eye's distance from its centre in half edges (a mild perspective: the near face a little
// bigger, so it reads as a solid). The farthest corner stays inside the disc: sqrt(3) * 19 * 5 / 4 < 47.
constexpr float kOrientationWidgetHalf = 19.0f;
constexpr float kOrientationWidgetEye = 5.0f;
// On the cube's surface, a coordinate beyond this (of 1) counts as "at the edge": one such axis is a face, two an
// edge, three a corner. 0.62 leaves the face the middle 38% of its area and gives each edge strip and corner square
// about 9 px at Medium (6 at Small), comfortable for a mouse and workable for a pen; the hover glow shows the target
// before the click, and Gizmo size > Large grows all of them.
constexpr float kOrientationWidgetEdgeBand = 0.62f;
// A press that travels farther than this (unscaled pixels) is a drag (an orbit), not a click.
constexpr float kOrientationWidgetDragPx = 4.0f;
// The eased move's length in seconds.
constexpr float kOrientationWidgetMoveSeconds = 0.35f;

// A cube corner (world axes, +-1 each) as the view `rotation` shows it (the camera's view matrix's rotation part)
// around `centre`, with `half` pixels per half edge; screen y grows downwards. `depth` grows towards the viewer.
struct OrientationWidgetPoint {
    glm::vec2 at{0.0f};
    float depth = 0.0f;
};
inline OrientationWidgetPoint orientationWidgetProject(const glm::mat3& rotation, glm::vec2 centre, float half,
                                                       const glm::vec3& cubePoint) {
    const glm::vec3 v = rotation * cubePoint;
    const float k = kOrientationWidgetEye / std::max(kOrientationWidgetEye - v.z, 0.5f);
    return {centre + glm::vec2(v.x, -v.y) * (half * k), v.z};
}

// The 8 corners, index bits: 1 = +X, 2 = +Y, 4 = +Z.
inline glm::vec3 orientationWidgetCorner(int index) {
    return glm::vec3((index & 1) ? 1.0f : -1.0f, (index & 2) ? 1.0f : -1.0f, (index & 4) ? 1.0f : -1.0f);
}
inline std::array<OrientationWidgetPoint, 8> orientationWidgetCorners(const glm::mat3& rotation, glm::vec2 centre, float half) {
    std::array<OrientationWidgetPoint, 8> out;
    for (int i = 0; i < 8; ++i) out[size_t(i)] = orientationWidgetProject(rotation, centre, half, orientationWidgetCorner(i));
    return out;
}

// A face (axis 0 X, 1 Y, 2 Z; negative for the -side) faces the viewer when its normal leans out of the screen more
// than the perspective hides it.
inline bool orientationWidgetFaceVisible(const glm::mat3& rotation, int axis, bool negative) {
    glm::vec3 n(0.0f);
    n[axis] = negative ? -1.0f : 1.0f;
    return (rotation * n).z > 1.0f / kOrientationWidgetEye;
}

// What the pointer is over: a face (one axis set), an edge (two), a corner (three) or nothing (`axes` 0).
// `direction` has -1 / 0 / +1 per world axis: the direction the camera looks from after a click there.
struct OrientationWidgetPick {
    int axes = 0;
    glm::ivec3 direction{0};
    bool hit() const { return axes > 0; }
};

// The pointer (x, y) unprojected through the widget's own perspective onto the cube: the entry point of the ray
// from the eye through the pointer's spot on the plane of the cube's centre. The hit point's coordinates past the
// edge band name the face, edge or corner.
inline OrientationWidgetPick orientationWidgetPick(const glm::mat3& rotation, glm::vec2 centre, float half, float x, float y) {
    OrientationWidgetPick pick;
    if (half <= 0.0f) return pick;
    const glm::vec2 q((x - centre.x) / half, -(y - centre.y) / half); // on the centre plane, in half edges
    const glm::mat3 toCube = glm::transpose(rotation);                  // the rotation's inverse
    const glm::vec3 origin = toCube * glm::vec3(0.0f, 0.0f, kOrientationWidgetEye);
    const glm::vec3 dir = toCube * glm::vec3(q.x, q.y, -kOrientationWidgetEye);
    // Slab test against [-1, 1]^3.
    float tNear = -1e9f, tFar = 1e9f;
    for (int a = 0; a < 3; ++a) {
        if (std::abs(dir[a]) < 1e-7f) {
            if (std::abs(origin[a]) > 1.0f) return pick;
            continue;
        }
        float t0 = (-1.0f - origin[a]) / dir[a], t1 = (1.0f - origin[a]) / dir[a];
        if (t0 > t1) std::swap(t0, t1);
        tNear = std::max(tNear, t0);
        tFar = std::min(tFar, t1);
        if (tNear > tFar) return pick;
    }
    if (tNear < 0.0f) return pick; // (the eye inside the cube: never, the eye is 5 half edges out)
    const glm::vec3 p = origin + dir * tNear;
    for (int a = 0; a < 3; ++a) {
        if (std::abs(p[a]) < kOrientationWidgetEdgeBand) continue;
        pick.direction[a] = p[a] < 0.0f ? -1 : 1;
        ++pick.axes;
    }
    if (pick.axes == 0) { // (rounding on the face's own axis, |p| = 1 up to epsilon: never below the band)
        int best = 0;
        for (int a = 1; a < 3; ++a)
            if (std::abs(p[a]) > std::abs(p[best])) best = a;
        pick.direction[best] = p[best] < 0.0f ? -1 : 1;
        pick.axes = 1;
    }
    return pick;
}

// The view looking from `direction` at the centre: the camera's yaw and pitch in degrees (Camera: yaw about world
// Y, pitch about its X; roll 0). A face gives the six side views of Input.cpp's view actions exactly (Top: yaw 0,
// pitch -90, the front at the bottom of the screen, as Numpad 7).
struct OrientationWidgetView {
    float yaw = 0.0f, pitch = 0.0f;
};
inline OrientationWidgetView orientationWidgetView(const glm::ivec3& direction) {
    OrientationWidgetView v;
    const glm::vec3 d = glm::normalize(glm::vec3(direction));
    if (direction.x == 0 && direction.z == 0) {
        v.yaw = 0.0f;
        v.pitch = direction.y > 0 ? -90.0f : 90.0f;
        return v;
    }
    v.yaw = glm::degrees(std::atan2(d.x, d.z));
    v.pitch = glm::degrees(-std::asin(glm::clamp(d.y, -1.0f, 1.0f)));
    return v;
}

// The face a click on face `direction` turns to: the opposite one when the camera already looks from it (`forward`
// the camera's forward vector in world space): a second click on Front shows the back.
inline glm::ivec3 orientationWidgetClickTarget(const glm::ivec3& direction, int axes, const glm::vec3& forward) {
    if (axes != 1) return direction;
    const glm::vec3 d = glm::vec3(direction);
    return glm::dot(-forward, d) > 0.9995f ? -direction : direction;
}

// The short names (tr() keys) of the sides a direction leans to, by axis: Right / Left, Top / Bottom, Front / Rear.
// Looking from +Z is the front view, so +Z is "Front". ("Rear", not "Back": that word is the tour's button, "previous
// page" in some languages; the view itself is still "Back view".)
inline const char* orientationWidgetSideName(int axis, bool negative) {
    static constexpr const char* names[6] = {"Right", "Top", "Front", "Left", "Bottom", "Rear"};
    return names[(negative ? 3 : 0) + (axis < 0 || axis > 2 ? 0 : axis)];
}

// The easing of the camera's move: smoothstep, 0 at the start, 1 at the end, no overshoot.
inline float orientationWidgetEase(float t) {
    t = glm::clamp(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

// The shortest way round from `from` to `to` degrees, as a difference in (-180, 180].
inline float orientationWidgetYawDelta(float from, float to) {
    float d = std::fmod(to - from, 360.0f);
    if (d > 180.0f) d -= 360.0f;
    if (d <= -180.0f) d += 360.0f;
    return d;
}

} // namespace sf
