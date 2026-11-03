// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The axis gizmo in the canvas corner (Draw > Camera > Axes, #31): where its six ends sit on screen for a view,
// which end a click lands on, and the side view each end snaps to (#43, as in Blender); its size (Small / Medium /
// Large) and the View Figure's snap to the nearest side. Pure arithmetic (no ImGui), so it can be unit tested.
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
struct AxisGizmoView {
    const char* action;
    float yaw, pitch;
};
inline AxisGizmoView axisGizmoView(int axis, bool negative) {
    static constexpr AxisGizmoView views[6] = {{"view.right", 90.0f, 0.0f},  {"view.top", 0.0f, -90.0f},
                                               {"view.front", 0.0f, 0.0f},   {"view.left", -90.0f, 0.0f},
                                               {"view.bottom", 0.0f, 90.0f}, {"view.back", 180.0f, 0.0f}};
    return views[(negative ? 3 : 0) + (axis < 0 || axis > 2 ? 0 : axis)];
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

} // namespace sf
