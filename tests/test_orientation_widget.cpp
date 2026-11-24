// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The orientation widget (Draw > Camera > Corner gizmo > Orientation): its dots sit on the axes' ends and name the
// side each looks from, the move's easing never overshoots, the yaw takes the short way round, and the corner
// setting puts the disc at either top corner. Invariant: a dot's view is the numpad's view for that side (the ends
// and views are AxisGizmo.h's own, tested in test_axis_gizmo.cpp).
#include "TestHarness.h"

#include "app/OrientationWidget.h"
#include "render/Camera.h"

#include <glm/glm.hpp>

#include <cmath>
#include <string>

using namespace sf;

namespace {
bool about(float a, float b, float eps = 0.01f) { return std::abs(a - b) <= eps; }
} // namespace

TEST(orientation_widget_side_names_match_the_views) {
    // +X looks from the right, +Y from the top, +Z from the front; the negatives the other way.
    CHECK(std::string(orientationWidgetSideName(0, false)) == "Right");
    CHECK(std::string(orientationWidgetSideName(0, true)) == "Left");
    CHECK(std::string(orientationWidgetSideName(1, false)) == "Top");
    CHECK(std::string(orientationWidgetSideName(1, true)) == "Bottom");
    CHECK(std::string(orientationWidgetSideName(2, false)) == "Front");
    CHECK(std::string(orientationWidgetSideName(2, true)) == "Rear");
    for (int i = 0; i < 6; ++i) { // the same side the axes' view action names ("Right view" for "Right")
        const std::string side = orientationWidgetSideName(i % 3, i >= 3);
        const std::string view = axisGizmoView(i % 3, i >= 3).label;
        CHECK(side == "Rear" ? view == "Back view" : view == side + " view");
    }
}

TEST(orientation_widget_dots_shrink_with_depth) {
    AxisGizmoEnd near{glm::vec2(0.0f), 1.0f, 0, false}, far{glm::vec2(0.0f), -1.0f, 0, false};
    AxisGizmoEnd nearNeg{glm::vec2(0.0f), 1.0f, 0, true};
    CHECK(about(orientationWidgetDotRadius(near, 1.0f), kOrientationWidgetDot));
    CHECK(about(orientationWidgetDotRadius(far, 1.0f), kOrientationWidgetDot * 0.78f));
    CHECK(about(orientationWidgetDotRadius(nearNeg, 1.0f), kOrientationWidgetSmallDot));
    CHECK(about(orientationWidgetDotRadius(near, 2.0f), 2.0f * kOrientationWidgetDot)); // the unit scales it
    // The dots stay clear of each other and inside the disc: a dot's radius is well under the axes' length.
    CHECK(kOrientationWidgetDot + kOrientationWidgetBall < kAxisGizmoLength);
    CHECK(kAxisGizmoLength + kOrientationWidgetDot < kAxisGizmoDisc);
}

TEST(orientation_widget_ease_and_yaw_delta) {
    CHECK(orientationWidgetEase(0.0f) == 0.0f);
    CHECK(orientationWidgetEase(1.0f) == 1.0f);
    CHECK(about(orientationWidgetEase(0.5f), 0.5f));
    float last = 0.0f;
    for (int i = 1; i <= 20; ++i) { // monotonic, never past 1
        const float e = orientationWidgetEase(float(i) / 20.0f);
        CHECK(e >= last && e <= 1.0f);
        last = e;
    }
    CHECK(orientationWidgetEase(1.5f) == 1.0f);
    CHECK(orientationWidgetEase(-0.5f) == 0.0f);
    CHECK(about(orientationWidgetYawDelta(0.0f, 90.0f), 90.0f));
    CHECK(about(orientationWidgetYawDelta(170.0f, -170.0f), 20.0f)); // the short way round
    CHECK(about(orientationWidgetYawDelta(-170.0f, 170.0f), -20.0f));
    CHECK(about(orientationWidgetYawDelta(0.0f, 180.0f), 180.0f)); // a half turn goes the positive way
    CHECK(about(orientationWidgetYawDelta(0.0f, 0.0f), 0.0f));
}

TEST(orientation_widget_corner_setting_places_the_disc) {
    // A canvas from x 100, 800 wide; the disc's radius plus margin 57: top left at 157, top right at 843.
    CHECK(about(orientationWidgetCentreX(0, 100.0f, 800.0f, 57.0f), 157.0f));
    CHECK(about(orientationWidgetCentreX(1, 100.0f, 800.0f, 57.0f), 843.0f));
    CHECK(about(orientationWidgetCentreX(7, 100.0f, 800.0f, 57.0f), 157.0f)); // anything else: the left, as always
}
