// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The orientation widget in the canvas corner (Draw > Camera > Corner gizmo > Orientation): the world axes through a
// ball of dark glass, a dot at each of the six ends, no letters. A click on a dot turns the camera to look from that
// side with a short eased move (the dot facing you again: the opposite side), a drag on the widget orbits, a
// double-click frames the model. The ends' places, hits and views are the axes' own (AxisGizmo.h: axisGizmoEnds,
// axisGizmoHit, axisGizmoSnapEnd, axisGizmoView); this header adds what the widget needs on top: its sizes, the
// sides' names and the easing of the camera's move. App.cpp / CornerGizmo.cpp draw it and run the move. Unit tested
// (tests/test_orientation_widget.cpp).
#pragma once

#include "app/AxisGizmo.h"

#include <glm/glm.hpp>

#include <cmath>

namespace sf {

// Sizes in unscaled pixels at Medium, multiplied by the unit (UI scale x Gizmo size, as the axes): the glass ball's
// radius, a positive end's dot and a negative end's smaller dot (both at the nearest depth; the far ones shrink and
// fade as the axes' do, axisGizmoEndAlpha).
constexpr float kOrientationWidgetBall = 13.0f;
constexpr float kOrientationWidgetDot = 6.5f;
constexpr float kOrientationWidgetSmallDot = 4.5f;
// A press that travels farther than this (unscaled pixels) is a drag (an orbit), not a click.
constexpr float kOrientationWidgetDragPx = 4.0f;
// The eased move's length in seconds.
constexpr float kOrientationWidgetMoveSeconds = 0.35f;

// A dot's radius by its end's depth: full size pointing at the viewer, 78% pointing away (as the axes' ends).
inline float orientationWidgetDotRadius(const AxisGizmoEnd& e, float unit) {
    const float d = glm::clamp(e.depth, -1.0f, 1.0f);
    return (e.negative ? kOrientationWidgetSmallDot : kOrientationWidgetDot) * (0.89f + 0.11f * d) * unit;
}

// The short name (a tr() key) of the side an end looks from: +X Right, -X Left, +Y Top, -Y Bottom, +Z Front, -Z
// Rear ("Rear", not "Back": that key is the tour's button, "previous page" in some languages; the view itself is
// still "Back view").
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

// Draw > Camera > Corner gizmo > Position: 0 the top left of the canvas (where the axes have always been), 1 the top
// right. The disc's centre x for a canvas at `x`, `width` wide, `r` the disc's radius plus its margin.
inline float orientationWidgetCentreX(int corner, float x, float width, float r) { return corner == 1 ? x + width - r : x + r; }

} // namespace sf
