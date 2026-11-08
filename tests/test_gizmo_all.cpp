// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The Transform gizmo's All mode (app/GizmoAll.h): which handle a point picks where handles meet, and that from any
// view and at 100% and 200% UI scale every shown handle has a spot where it is the one picked (none hides another).
#include "TestHarness.h"

#include "app/GizmoAll.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <cmath>

using namespace sf;
using namespace sf::gizmo_all;

namespace {

// An orthographic view turned by yaw then pitch (degrees), the gizmo at (400, 300), `scale` the UI scale.
struct View {
    glm::mat3 rot;
    float unit, scale;
    glm::vec3 toEye() const { return glm::transpose(rot) * glm::vec3(0, 0, 1); }
    Screen screen(const glm::mat3& frame = glm::mat3(1.0f)) const {
        auto project = [this](const glm::vec3& p, glm::vec2& out) {
            const glm::vec3 v = rot * p;
            out = glm::vec2(400.0f + v.x * unit, 300.0f - v.y * unit);
            return true;
        };
        return layout(project, glm::vec3(0.0f), frame, 1.0f, toEye(), unit, scale);
    }
};

View view(float yaw, float pitch, float scale = 1.0f) {
    const glm::mat4 r = glm::rotate(glm::mat4(1.0f), glm::radians(pitch), glm::vec3(1, 0, 0)) *
                        glm::rotate(glm::mat4(1.0f), glm::radians(yaw), glm::vec3(0, 1, 0));
    return {glm::mat3(r), 105.0f * scale, scale};
}

glm::vec2 lerp(glm::vec2 a, glm::vec2 b, float t) { return a + (b - a) * t; }

} // namespace

TEST(gizmo_all_centre_ball_tab_and_ring_priorities) {
    const View v = view(30.0f, -25.0f);
    const Screen s = v.screen();
    // The centre is free move, though rings seen nearly edge-on cross it.
    CHECK(pick(s, s.centre) == MoveView);
    CHECK(pick(s, s.centre + glm::vec2(8.0f, -6.0f)) == MoveView);
    // The scale balls sit on the shafts, clear of the centre and short of the heads; anywhere on a ball (and a
    // little round it) is that ball, and the shaft on both sides of it moves.
    for (int i = 0; i < 3; ++i) {
        const Screen::Axis& a = s.axis[i];
        if (!a.ball) continue;
        CHECK(pick(s, a.ballAt) == Handle(ScaleX + i));
        CHECK(pick(s, a.ballAt + glm::vec2(5.5f, -5.5f)) == Handle(ScaleX + i));
        CHECK(glm::length(a.ballAt - s.centre) > (kCentrePx + kBallPx + 6.0f) * s.scale);
        CHECK(glm::length(a.tip - a.ballAt) > (kHeadPx + kBallPx + 4.0f) * s.scale);
        CHECK(pick(s, lerp(a.from, a.ballAt, 0.4f)) == Handle(MoveX + i));
        CHECK(pick(s, lerp(a.ballAt, a.tip, 0.6f)) == Handle(MoveX + i));
    }
    // The even-scale tabs on the diagonals; the same radius between them picks nothing.
    for (int k = 0; k < 4; ++k) CHECK(pick(s, tabPoint(s, k)) == ScaleUniform);
    CHECK(pick(s, s.centre + glm::vec2(0.0f, -kTabs * s.unit)) == None);
    // The view ring between the rings and the tabs.
    CHECK(pick(s, s.centre + glm::vec2(-kViewRing * s.unit, 0.0f)) == RotateView);
    // An arrow wins over a ring crossing it; off the arrow the ring is picked.
    for (int i = 0; i < 3; ++i)
        if (s.axis[i].arrow) CHECK(pick(s, lerp(s.axis[i].from, s.axis[i].tip, 0.85f)) == Handle(MoveX + i));
}

TEST(gizmo_all_ball_hides_when_its_axis_points_at_the_viewer) {
    const Screen front = view(0.0f, 0.0f).screen(); // Z points at the viewer
    CHECK(!front.axis[2].ball);
    CHECK(!front.axis[2].arrow);
    CHECK(front.axis[0].ball && front.axis[1].ball);
    // Along the hidden axis, a point near the centre is the centre, not a scale ball sitting on top of it.
    CHECK(pick(front, front.centre + glm::vec2(2.0f, 2.0f)) == MoveView);
    // A point on the X ring (seen edge-on, a vertical line) below the centre: that ring.
    CHECK(pick(front, front.centre + glm::vec2(0.0f, 0.85f * front.unit)) == RotateX);
    // Above the centre the Y arrow lies over the same line and wins (its ball too).
    CHECK(pick(front, front.centre + glm::vec2(0.0f, -0.3f * front.unit)) == MoveY);
    CHECK(pick(front, front.centre + glm::vec2(0.0f, -kBall * front.unit)) == ScaleY);
    // Between the arrow heads and the rings: clear air, then the ring.
    CHECK(glm::length(front.axis[0].tip - front.centre) < 0.84f * front.unit);
}

TEST(gizmo_all_every_handle_reachable_from_any_view_and_ui_scale) {
    // Handles must not hide each other: each one shown has a spot where it is picked.
    int views = 0;
    for (float scale : {1.0f, 2.0f})
        for (float yaw = -180.0f; yaw < 180.0f; yaw += 20.0f)
            for (float pitch = -80.0f; pitch <= 80.0f; pitch += 20.0f) {
                const View v = view(yaw, pitch, scale);
                const Screen s = v.screen();
                ++views;
                CHECK(pick(s, s.centre) == MoveView);
                bool tab = false;
                for (int k = 0; k < 4; ++k) tab = tab || pick(s, tabPoint(s, k)) == ScaleUniform;
                CHECK(tab);
                bool viewRing = false;
                for (int k = 0; k < 32 && !viewRing; ++k) {
                    const float t = float(k) / 32.0f * 6.2831853f;
                    viewRing = pick(s, s.centre + glm::vec2(std::cos(t), std::sin(t)) * (kViewRing * s.unit)) == RotateView;
                }
                CHECK(viewRing);
                for (int i = 0; i < 3; ++i) {
                    const Screen::Axis& a = s.axis[i];
                    if (a.ball) { // on top where it is, unless a ball in front lies over it
                        const Handle h = pick(s, a.ballAt);
                        CHECK(h == Handle(ScaleX + i) || (h >= ScaleX && h <= ScaleZ && s.axis[h - ScaleX].depth > a.depth));
                    }
                    if (a.arrow) { // picked on its line, unless an arrow in front lies over it (drawn on top)
                        bool arrow = false, covered = true;
                        for (float t : {0.5f, 0.3f, 0.7f, 0.9f}) {
                            const Handle h = pick(s, lerp(a.from, a.tip, t));
                            arrow = arrow || h == Handle(MoveX + i);
                            covered = covered && ((h >= MoveX && h <= MoveZ && s.axis[h - MoveX].depth > a.depth) ||
                                                  (h >= ScaleX && h <= ScaleZ));
                        }
                        CHECK(arrow || covered);
                    }
                    if (a.plane) { // somewhere inside it, unless arrows or a square in front cover it
                        bool plane = false, covered = true;
                        for (float u : {0.2f, 0.5f, 0.8f})
                            for (float w : {0.2f, 0.5f, 0.8f}) {
                                const glm::vec2 q = lerp(lerp(a.quad[0], a.quad[1], u), lerp(a.quad[3], a.quad[2], u), w);
                                const Handle h = pick(s, q);
                                plane = plane || h == Handle(PlaneYZ + i);
                                covered = covered && ((h >= MoveX && h <= MoveZ) || (h >= ScaleX && h <= ScaleZ) ||
                                                      (h >= PlaneYZ && h <= PlaneXY && s.axis[h - PlaneYZ].planeDepth > a.planeDepth));
                            }
                        CHECK(plane || covered);
                    }
                    bool ring = false;
                    for (size_t k = 0; k < a.ring.size() && !ring; ++k) ring = pick(s, a.ring[k]) == Handle(RotateX + i);
                    CHECK(ring);
                    // The arrow head and the ball stay well inside the rings, with a gap.
                    if (a.arrow) CHECK(glm::length(a.tip - s.centre) < s.unit * 0.84f);
                    if (a.ball) CHECK(glm::length(a.ballAt - s.centre) < s.unit * 0.5f);
                }
            }
    CHECK(views > 300);
}

TEST(gizmo_all_turned_axes) {
    // Surface orientation: the axes turned with the surface, the same rules hold.
    const glm::mat3 f = glm::mat3(glm::rotate(glm::mat4(1.0f), 0.7f, glm::normalize(glm::vec3(1, 2, 0.5f))));
    const Screen s = view(15.0f, -35.0f, 2.0f).screen(f);
    CHECK(pick(s, s.centre) == MoveView);
    for (int i = 0; i < 3; ++i)
        if (s.axis[i].ball) CHECK(pick(s, s.axis[i].ballAt) == Handle(ScaleX + i));
}
