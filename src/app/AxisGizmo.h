// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The axis gizmo in the canvas corner (Draw > Camera > Axes, #31): where its six ends sit on screen for a view,
// which end a click lands on, and the side view each end snaps to (#43, as in Blender); a second click on the end
// already facing you snaps to the opposite side; how big and how faded each end is by its depth (the far ones
// smaller and fainter); the disc behind it (a drag there orbits); its size (Small / Medium / Large) and the View
// Figure's snap to the nearest side. Pure arithmetic (no ImGui), so it can be unit tested.
#pragma once

#include <glm/glm.hpp>

#include <array>
#include <cstddef>

namespace sf {

struct AxisGizmoEnd {
    glm::vec2 at{0.0f};   // on screen, pixels
    float depth = 0.0f;   // towards the viewer: > 0 points out of the screen
    int axis = 0;         // 0 X, 1 Y, 2 Z
    bool negative = false;
};

// The ends of the world axes as the view `rotation` (the camera's view matrix, rotation part) shows them,
// around `centre`, `length` pixels long; screen y grows downwards. Order: +X +Y +Z -X -Y -Z.
inline std::array<AxisGizmoEnd, 6> axisGizmoEnds(const glm::mat3& rotation, glm::vec2 centre, float length) {
    std::array<AxisGizmoEnd, 6> ends;
    for (int i = 0; i < 6; ++i) {
        glm::vec3 axis(0.0f);
        axis[i % 3] = i < 3 ? 1.0f : -1.0f;
        const glm::vec3 d = rotation * axis;
        ends[size_t(i)] = {centre + glm::vec2(d.x, -d.y) * length, d.z, i % 3, i >= 3};
    }
    return ends;
}

// The end under (x, y) within `radius` pixels, the one nearest the viewer when ends overlap; -1 for none.
inline int axisGizmoHit(const std::array<AxisGizmoEnd, 6>& ends, float x, float y, float radius) {
    int best = -1;
    for (int i = 0; i < 6; ++i) {
        const glm::vec2 d = ends[size_t(i)].at - glm::vec2(x, y);
        if (glm::dot(d, d) > radius * radius) continue;
        if (best < 0 || ends[size_t(i)].depth > ends[size_t(best)].depth) best = i;
    }
    return best;
}

// The view a click on an end snaps to: looking from that end towards the centre (+X: the right side, +Y: the
// top, +Z: the front). The action id and the camera's yaw and pitch in degrees, as Input.cpp's view actions.
// `label` names the view (the action's own label, translated with tr()) under the gizmo when the view is exactly it.
struct AxisGizmoView {
    const char* action;
    float yaw, pitch;
    const char* label;
};
inline AxisGizmoView axisGizmoView(int axis, bool negative) {
    static constexpr AxisGizmoView views[6] = {
        {"view.right", 90.0f, 0.0f, "Right view"},   {"view.top", 0.0f, -90.0f, "Top view"},
        {"view.front", 0.0f, 0.0f, "Front view"},    {"view.left", -90.0f, 0.0f, "Left view"},
        {"view.bottom", 0.0f, 90.0f, "Bottom view"}, {"view.back", 180.0f, 0.0f, "Back view"}};
    return views[(negative ? 3 : 0) + (axis < 0 || axis > 2 ? 0 : axis)];
}

// Sizes in unscaled pixels at Medium, multiplied by the unit (UI scale x Gizmo size, axisGizmoScale): the axes'
// length, a positive end's filled circle and a negative end's ring (both at the nearest depth), the click radius
// around an end and the disc behind the whole gizmo.
constexpr float kAxisGizmoLength = 34.0f;
constexpr float kAxisGizmoBall = 9.0f;
constexpr float kAxisGizmoRing = 6.0f;
constexpr float kAxisGizmoHitRadius = 10.0f;
constexpr float kAxisGizmoDisc = 47.0f;

// Depth cues: an end pointing straight at the viewer is full size and opaque; one pointing straight away is 78% of
// the size and 50% opaque, so front and back read at a glance.
inline float axisGizmoEndRadius(const AxisGizmoEnd& e, float unit) {
    const float d = glm::clamp(e.depth, -1.0f, 1.0f);
    return (e.negative ? kAxisGizmoRing : kAxisGizmoBall) * (0.89f + 0.11f * d) * unit;
}
inline float axisGizmoEndAlpha(float depth) { return 0.75f + 0.25f * glm::clamp(depth, -1.0f, 1.0f); }

// Within the disc behind the gizmo (`centre`, `radius` pixels): a press there that misses the ends orbits the view.
inline bool axisGizmoOnDisc(glm::vec2 centre, float radius, float x, float y) {
    const glm::vec2 d = glm::vec2(x, y) - centre;
    return glm::dot(d, d) <= radius * radius;
}

// The end a click on end `hit` snaps to: that end, or its opposite when the view already looks from it (a second
// click on Front shows the back).
inline int axisGizmoSnapEnd(const std::array<AxisGizmoEnd, 6>& ends, int hit) {
    if (hit < 0 || hit > 5) return -1;
    return ends[size_t(hit)].depth > 0.999f ? (hit + 3) % 6 : hit;
}

// Draw > Camera > Gizmo size: 0 Small, 1 Medium (the size before 0.10.8, the default), 2 Large. Multiplies the
// gizmo's length, balls and click radius on top of the UI scale.
inline float axisGizmoScale(int size) { return size <= 0 ? 0.65f : size == 1 ? 1.0f : 1.4f; }

// The end facing the viewer most: the side view nearest the current one (a click on the View Figure snaps there).
inline int axisGizmoFacing(const std::array<AxisGizmoEnd, 6>& ends) {
    int best = 0;
    for (int i = 1; i < 6; ++i)
        if (ends[size_t(i)].depth > ends[size_t(best)].depth) best = i;
    return best;
}

// The end the view looks exactly from (a side view: its name shows under the gizmo), or -1 between views.
inline int axisGizmoAligned(const std::array<AxisGizmoEnd, 6>& ends) {
    const int facing = axisGizmoFacing(ends);
    return ends[size_t(facing)].depth > 0.9995f ? facing : -1;
}

} // namespace sf
