// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The Transform gizmo's All mode (app/GizmoAll.h): which handle a point picks where handles meet, and that from any
// view and at 100% and 200% UI scale every shown handle has a spot where it is the one picked (none hides another).
#include "TestHarness.h"

#include "app/GizmoAll.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

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
    // The X ring is seen edge-on here (a vertical bar): it is faded and picks nothing, wherever along the bar.
    for (float t : {0.3f, 0.6f, 0.85f, 1.0f}) CHECK(pick(front, front.centre + glm::vec2(0.0f, t * front.unit)) != RotateX);
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
                    bool ring = false; // (a ring seen edge-on is faded and picks nothing: the clarity tests below)
                    for (size_t k = 0; k < a.ring.size() && !ring; ++k) ring = pick(s, a.ring[k]) == Handle(RotateX + i);
                    CHECK(ring || a.ringPick == Screen::Axis::RingFaded);
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

// ---- Pick clarity (Idea by Fenix on Discord): edge-on rings and arrows at the viewer step back ------------------------

namespace {

// The views the sweep covers: front, side and top, then 30 more spread over the sphere.
std::vector<View> clarityViews(float scale) {
    std::vector<View> out{view(0.0f, 0.0f, scale), view(90.0f, 0.0f, scale), view(0.0f, 90.0f, scale)};
    for (int k = 0; k < 30; ++k) {
        const float yaw = std::fmod(37.0f * float(k) + 11.0f, 360.0f) - 180.0f; // 37 is coprime with 360: no repeats
        const float pitch = -80.0f + 160.0f * float((k * 7) % 30) / 29.0f;
        out.push_back(view(yaw, pitch, scale));
    }
    return out;
}

// Whether p is close enough to a handle other than `self` that its pick could fairly go there (that handle is drawn
// there too): arrows, balls, plane squares, the centre, the tabs, the view ring and the other rings' pickable parts.
bool nearOther(const Screen& s, glm::vec2 p, Handle self) {
    const float tol = kPickPx * s.scale + 2.0f;
    auto is = [&](Handle h) { return h == self; };
    if (!is(MoveView) && glm::length(p - s.centre) < kCentrePx * s.scale + tol) return true;
    for (int i = 0; i < 3; ++i) {
        const Screen::Axis& a = s.axis[i];
        if (a.arrow && !is(Handle(MoveX + i)) && segmentDistance(p, a.from, a.tip) < tol) return true;
        if (a.ball && !is(Handle(ScaleX + i)) && glm::length(p - a.ballAt) < (kBallPx + 3.0f) * s.scale + 2.0f) return true;
        if (a.plane && !is(Handle(PlaneYZ + i)) && insideQuad(p, a.quad)) return true;
        if (!is(Handle(RotateX + i)) && a.ringPick != Screen::Axis::RingFaded)
            for (size_t k = 1; k < a.ring.size(); ++k)
                if (ringPointPicks(s, a, a.ring[k]) && segmentDistance(p, a.ring[k - 1], a.ring[k]) < tol + kPickPx * s.scale * 0.6f)
                    return true;
    }
    for (int k = 0; k < 4; ++k)
        if (!is(ScaleUniform) && glm::length(p - tabPoint(s, k)) < tol + 10.0f * s.scale) return true;
    return !is(RotateView) && std::abs(glm::length(p - s.centre) - kViewRing * s.unit) < tol;
}

} // namespace

TEST(gizmo_all_pick_clarity_edge_on_rings_and_arrows_at_the_viewer_step_back) {
    int faded = 0, rims = 0, fadedArrows = 0;
    for (float scale : {1.0f, 2.0f})
        for (const View& v : clarityViews(scale)) {
            const Screen s = v.screen();
            for (int i = 0; i < 3; ++i) {
                const Screen::Axis& a = s.axis[i];
                const float sinEdge = std::abs(a.depth);
                const float edgeDeg = glm::degrees(std::asin(std::min(sinEdge, 1.0f)));
                CHECK((a.ringPick == Screen::Axis::RingFaded) == (edgeDeg < kRingFadeDeg));
                CHECK((a.ringPick == Screen::Axis::RingRim) == (edgeDeg >= kRingFadeDeg && edgeDeg < kRingRimDeg));
                // Rings: faded ones pick nothing; rim ones pick only beyond the heads.
                for (size_t k = 0; k < a.ring.size(); ++k) {
                    const bool ringHit = pick(s, a.ring[k]) == Handle(RotateX + i);
                    if (a.ringPick == Screen::Axis::RingFaded) CHECK(!ringHit);
                    // (the rim's edge picks within the pick distance of itself, as any handle does)
                    if (a.ringPick == Screen::Axis::RingRim && glm::length(a.ring[k] - s.centre) < kRimMin * s.unit - kPickPx * s.scale)
                        CHECK(!ringHit);
                    // No point of a visible ring picks anything else where nothing else is near.
                    if (a.ringPick != Screen::Axis::RingFaded && ringPointPicks(s, a, a.ring[k]) && !nearOther(s, a.ring[k], Handle(RotateX + i)))
                        CHECK(ringHit);
                }
                if (a.ringPick == Screen::Axis::RingFaded) ++faded;
                if (a.ringPick == Screen::Axis::RingRim) ++rims;
                // Arrows at the viewer: faded, with their ball, and picked nowhere along their line.
                if (std::abs(a.depth) > std::cos(glm::radians(kArrowFadeDeg))) {
                    CHECK(!a.arrow && !a.ball);
                    ++fadedArrows;
                    for (float t : {0.0f, 0.25f, 0.5f, 0.75f, 1.0f}) {
                        const Handle h = pick(s, lerp(a.from, a.tip, t));
                        CHECK(h != Handle(MoveX + i) && h != Handle(ScaleX + i));
                    }
                    CHECK(pick(s, a.ballAt) != Handle(ScaleX + i));
                } else {
                    CHECK(!a.arrowFaded);
                }
                // Visible arrows, balls and squares: no point on one that nothing else is near picks another handle.
                if (a.arrow)
                    for (float t : {0.1f, 0.3f, 0.5f, 0.7f, 0.9f}) {
                        const glm::vec2 p = lerp(a.from, a.tip, t);
                        if (!nearOther(s, p, Handle(MoveX + i))) CHECK(pick(s, p) == Handle(MoveX + i));
                    }
                if (a.ball && !nearOther(s, a.ballAt, Handle(ScaleX + i))) CHECK(pick(s, a.ballAt) == Handle(ScaleX + i));
                if (a.plane)
                    for (float u : {0.3f, 0.7f}) {
                        const glm::vec2 q = lerp(lerp(a.quad[0], a.quad[1], u), lerp(a.quad[3], a.quad[2], u), 0.5f);
                        if (!nearOther(s, q, Handle(PlaneYZ + i))) CHECK(pick(s, q) == Handle(PlaneYZ + i));
                    }
                // Every visible handle is still reachable: a ring (faded rings aside) has a point that picks it.
                if (a.ringPick != Screen::Axis::RingFaded) {
                    bool reach = false;
                    for (size_t k = 0; k < a.ring.size() && !reach; ++k) reach = pick(s, a.ring[k]) == Handle(RotateX + i);
                    CHECK(reach);
                }
                if (a.arrow) {
                    bool reach = false;
                    for (float t : {0.5f, 0.3f, 0.7f, 0.9f, 0.1f}) reach = reach || pick(s, lerp(a.from, a.tip, t)) == Handle(MoveX + i);
                    // (unless an arrow in front lies over it, as in the existing reachability test)
                    CHECK(reach || [&] {
                        const Handle h = pick(s, lerp(a.from, a.tip, 0.5f));
                        return (h >= MoveX && h <= MoveZ && s.axis[h - MoveX].depth > a.depth) || (h >= ScaleX && h <= ScaleZ);
                    }());
                }
            }
        }
    CHECK(faded > 0 && rims > 0 && fadedArrows > 0); // the sweep does reach each case
}

TEST(gizmo_all_pick_clarity_front_side_top_rings_are_bars_only_at_the_rim) {
    // The axis-aligned views are where people start: two rings are edge-on bars, each faded, and the arrows keep the
    // middle; the view ring turns about the viewer; the third axis points at the viewer and its arrow fades.
    for (const View& v : {view(0.0f, 0.0f), view(90.0f, 0.0f), view(0.0f, 90.0f)}) {
        const Screen s = v.screen();
        int edgeOn = 0, atViewer = 0;
        for (int i = 0; i < 3; ++i) {
            if (s.axis[i].ringPick == Screen::Axis::RingFaded) ++edgeOn;
            if (s.axis[i].arrowFaded) ++atViewer;
        }
        CHECK(edgeOn == 2 && atViewer == 1);
        // Pressing along the bars picks the arrows and balls there, never a faded ring.
        for (int i = 0; i < 3; ++i)
            if (s.axis[i].ringPick == Screen::Axis::RingFaded)
                for (float t : {-0.95f, -0.6f, -0.3f, 0.3f, 0.6f, 0.95f}) {
                    const glm::vec2 d = glm::normalize(s.axis[(i + 1) % 3].arrow ? s.axis[(i + 1) % 3].tip - s.centre : s.axis[(i + 2) % 3].tip - s.centre);
                    CHECK(pick(s, s.centre + d * (t * s.unit)) != Handle(RotateX + i));
                }
    }
}

TEST(gizmo_all_pick_clarity_thresholds_in_degrees) {
    // Turn the front view about the vertical by `yaw`: the X ring is `yaw` degrees from edge-on, the Z arrow `yaw`
    // degrees off the view direction (the numbers of the design: 15 and 35 for rings, 12 for arrows).
    auto at = [](float yaw) { return view(yaw, 0.0f).screen(); };
    CHECK(at(10.0f).axis[0].ringPick == Screen::Axis::RingFaded);
    CHECK(at(14.0f).axis[0].ringPick == Screen::Axis::RingFaded);
    CHECK(at(16.0f).axis[0].ringPick == Screen::Axis::RingRim);
    CHECK(at(34.0f).axis[0].ringPick == Screen::Axis::RingRim);
    CHECK(at(36.0f).axis[0].ringPick == Screen::Axis::RingFull);
    CHECK(at(10.0f).axis[2].arrowFaded && !at(10.0f).axis[2].arrow && !at(10.0f).axis[2].ball);
    CHECK(at(11.0f).axis[2].arrowFaded);
    CHECK(!at(13.0f).axis[2].arrowFaded);
    // The Z arrow at 13 degrees is short on screen (about 10 px): it may still be too short to show, never picked then.
    // A rim ring picks beyond the arrow heads only: at 25 degrees the X ring's ends do, its middle (under the arrows) does not.
    const Screen s = at(25.0f);
    const Screen::Axis& x = s.axis[0];
    CHECK(x.ringPick == Screen::Axis::RingRim);
    float far = 0.0f, near = 1e9f;
    for (const glm::vec2& p : x.ring) {
        const bool hit = pick(s, p) == RotateX;
        const float r = glm::length(p - s.centre) / s.unit;
        if (hit) near = std::min(near, r);
        if (r > 0.97f) far = std::max(far, hit ? 1.0f : 0.0f);
        if (r < 0.7f) CHECK(!hit);
    }
    CHECK(far == 1.0f);
    CHECK(near > 0.75f);
}
