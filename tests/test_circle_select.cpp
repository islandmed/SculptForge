// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The Circle shape of Mask, Select and Cut (#213): the drag geometry (from the centre or a corner, Shift = round) and what
// the ellipse does on a dense curved mesh. The promises: a Circle Mask masks exactly the vertices whose view lies inside
// the ellipse (none outside, none missed), the ellipse is symmetric about its centre, a circle stays round under any
// drag direction, a Circle Select hides exactly the outside, and a Cut Circle removes the volume of the elliptic
// cylinder through the sphere, leaving a watertight mesh with nothing left inside the ellipse.
#include "TestHarness.h"

#include "core/Commands.h"
#include "core/Cut.h"
#include "core/Primitives.h"
#include "core/ScreenSelect.h"

#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <cstdio>

using namespace sf;

namespace {

const CommandRegistry& registry() {
    static CommandRegistry r;
    return r;
}

std::string pointsString(const std::vector<glm::vec2>& pts) {
    std::string s;
    char buf[48];
    for (const glm::vec2& p : pts) {
        std::snprintf(buf, sizeof buf, "%s%.6f %.6f", s.empty() ? "" : " ", double(p.x), double(p.y));
        s += buf;
    }
    return s;
}

double volume(const Mesh& m) {
    double v = 0.0;
    for (const glm::uvec3& t : m.triangles)
        v += glm::dot(glm::dvec3(m.positions[t.x]), glm::cross(glm::dvec3(m.positions[t.y]), glm::dvec3(m.positions[t.z])));
    return v / 6.0;
}

// Inside the ellipse by its equation (the exact shape the polygon approximates).
float ellipseValue(const DragEllipse& e, glm::vec2 p) {
    const glm::vec2 q = (p - e.centre) / e.radius;
    return q.x * q.x + q.y * q.y;
}

} // namespace

TEST(circle_drag_from_centre_and_corner) {
    // Centre: the press is the centre, the cursor sets each radius.
    DragEllipse e = dragEllipse({100, 100}, {160, 130}, false, false);
    CHECK(e.centre == glm::vec2(100, 100) && e.radius == glm::vec2(60, 30) && e.valid());
    // Corner: press and cursor are opposite corners of the box.
    e = dragEllipse({100, 100}, {160, 130}, true, false);
    CHECK(e.centre == glm::vec2(130, 115) && e.radius == glm::vec2(30, 15));
    // Whichever way it is dragged, the same box gives the same ellipse.
    const DragEllipse f = dragEllipse({160, 130}, {100, 100}, true, false);
    CHECK(f.centre == e.centre && f.radius == e.radius);
    CHECK(dragEllipse({100, 100}, {60, 70}, false, false).radius == glm::vec2(40, 30));
    // Nothing to draw before the cursor leaves the press (or on one axis only).
    CHECK(!dragEllipse({100, 100}, {100, 100}, false, false).valid());
    CHECK(!dragEllipse({100, 100}, {160, 100}, true, false).valid());
}

TEST(circle_round_stays_round_in_every_direction) {
    for (const bool corner : {false, true}) {
        for (int i = 0; i < 16; ++i) {
            const float a = float(i) / 16.0f * 6.2831853f;
            const glm::vec2 cursor = glm::vec2(200, 200) + glm::vec2(std::cos(a), std::sin(a)) * 83.0f;
            const DragEllipse e = dragEllipse({200, 200}, cursor, corner, true);
            CHECK(e.valid());
            CHECK(std::abs(e.radius.x - e.radius.y) < 1e-3f);
            if (!corner) { // from the centre the cursor is on the rim
                CHECK(std::abs(e.radius.x - 83.0f) < 1e-3f && e.centre == glm::vec2(200, 200));
            } else { // from a corner the circle sits inside the square toward the cursor, touching the press corner's sides
                const glm::vec2 side = glm::abs(cursor - glm::vec2(200, 200));
                const float longer = std::max(side.x, side.y);
                CHECK(std::abs(e.radius.x - longer * 0.5f) < 1e-3f);
                const glm::vec2 delta = cursor - glm::vec2(200, 200);
                const glm::vec2 toward(delta.x < 0.0f ? -1.0f : 1.0f, delta.y < 0.0f ? -1.0f : 1.0f); // (a zero axis grows toward +)
                CHECK(glm::length(e.centre - (glm::vec2(200, 200) + toward * longer * 0.5f)) < 1e-3f);
            }
        }
    }
}

TEST(circle_polygon_is_on_the_rim_and_symmetric) {
    const DragEllipse e{{3.0f, -2.0f}, {5.0f, 2.0f}};
    const std::vector<glm::vec2> poly = ellipsePolygon(e);
    CHECK(int(poly.size()) == kEllipseSegments);
    glm::vec2 sum(0.0f);
    float worst = 0.0f;
    for (size_t i = 0; i < poly.size(); ++i) {
        worst = std::max(worst, std::abs(ellipseValue(e, poly[i]) - 1.0f));
        sum += poly[i];
        // Point reflection through the centre maps the polygon onto itself.
        const glm::vec2 mirror = 2.0f * e.centre - poly[i];
        float nearest = 1e9f;
        for (const glm::vec2& q : poly) nearest = std::min(nearest, glm::length(q - mirror));
        CHECK(nearest < 1e-4f);
    }
    CHECK(worst < 1e-5f);
    CHECK(glm::length(sum / float(poly.size()) - e.centre) < 1e-4f);
    // Between its vertices the polygon stays within 0.03% of the radius of the true rim.
    ScreenPolygon p(poly);
    CHECK(p.valid());
    CHECK(p.contains(e.centre));
    CHECK(p.contains(e.centre + glm::vec2(4.995f, 0.0f)));
    CHECK(!p.contains(e.centre + glm::vec2(5.005f, 0.0f)));
    CHECK(p.contains(e.centre + glm::vec2(0.0f, 1.998f)));
    CHECK(!p.contains(e.centre + glm::vec2(0.0f, 2.003f)));
}

TEST(circle_mask_matches_the_ellipse_exactly) {
    const Mesh sphere = makeIcosphere(5);
    const glm::mat4 vp = frontViewProj(sphere);
    for (const DragEllipse& e : {DragEllipse{{0.0f, 0.0f}, {0.45f, 0.45f}}, DragEllipse{{0.1f, -0.05f}, {0.6f, 0.25f}},
                                 DragEllipse{{-0.2f, 0.15f}, {0.2f, 0.7f}}}) {
        Document d = makeDoc(sphere);
        CHECK(registry()
                  .execute(d, "mask_lasso", {{"points", pointsString(ellipsePolygon(e))}})
                  .ok);
        size_t inside = 0, wrong = 0;
        for (uint32_t v = 0; v < d.mesh.vertexCount(); ++v) {
            glm::vec2 ndc;
            CHECK(projectToNdc(vp, d.mesh.positions[v], ndc));
            const float value = ellipseValue(e, ndc);
            if (std::abs(value - 1.0f) < 0.01f) continue; // on the rim: the polygon's chord may fall either side
            const bool masked = d.mesh.mask[v] > 0.5f;
            inside += masked;
            if (masked != (value < 1.0f)) ++wrong;
        }
        std::printf("circle mask: %zu vertices masked, %zu wrong\n", inside, wrong);
        CHECK(wrong == 0);
        CHECK(inside > 50 && inside < d.mesh.vertexCount());
        // Alt unmasks exactly what the shape masked.
        CHECK(registry().execute(d, "mask_lasso", {{"points", pointsString(ellipsePolygon(e))}, {"unmask", true}}).ok);
        size_t left = 0;
        for (uint32_t v = 0; v < d.mesh.vertexCount(); ++v) left += d.mesh.mask[v] > 0.0f;
        CHECK(left == 0);
    }
}

TEST(circle_mask_is_symmetric_about_its_centre) {
    // A circle centred on the view's middle masks a pattern that mirrors left-right and up-down: every masked vertex's
    // mirror image in the view lies inside the circle too, and none that lie outside are masked.
    const Mesh sphere = makeIcosphere(5);
    const glm::mat4 vp = frontViewProj(sphere);
    Document d = makeDoc(sphere);
    const DragEllipse e = dragEllipse({0.0f, 0.0f}, {0.5f, 0.3f}, false, true);
    CHECK(registry().execute(d, "mask_lasso", {{"points", pointsString(ellipsePolygon(e))}, {"front_faces_only", true}}).ok);
    size_t masked = 0, escaped = 0;
    for (uint32_t v = 0; v < d.mesh.vertexCount(); ++v) {
        if (d.mesh.mask[v] < 0.5f) continue;
        ++masked;
        glm::vec2 ndc;
        CHECK(projectToNdc(vp, d.mesh.positions[v], ndc));
        for (const glm::vec2 m : {glm::vec2(-ndc.x, ndc.y), glm::vec2(ndc.x, -ndc.y), -ndc})
            if (ellipseValue(e, m) > 1.01f) ++escaped;
    }
    std::printf("circle mask symmetry: %zu masked, %zu mirror images outside\n", masked, escaped);
    CHECK(masked > 200);
    CHECK(escaped == 0);
}

TEST(circle_select_hides_the_outside) {
    const Mesh sphere = makeIcosphere(5);
    const glm::mat4 vp = frontViewProj(sphere);
    const DragEllipse e{{0.05f, 0.0f}, {0.5f, 0.35f}};
    Document d = makeDoc(sphere);
    CHECK(registry().execute(d, "hide_lasso", {{"points", pointsString(ellipsePolygon(e))}}).ok);
    CHECK(d.mesh.hasHidden());
    size_t wrong = 0, kept = 0;
    for (const glm::uvec3& t : d.mesh.triangles) {
        // A triangle is kept when its three corners are inside; one wholly outside the ellipse may not be.
        glm::vec2 a, b, c;
        projectToNdc(vp, d.mesh.positions[t.x], a);
        projectToNdc(vp, d.mesh.positions[t.y], b);
        projectToNdc(vp, d.mesh.positions[t.z], c);
        const uint32_t i = uint32_t(&t - d.mesh.triangles.data());
        const bool allOut = ellipseValue(e, a) > 1.02f && ellipseValue(e, b) > 1.02f && ellipseValue(e, c) > 1.02f;
        const bool hidden = d.mesh.hidden[i] != 0;
        kept += !hidden;
        if (!hidden && allOut) ++wrong;
    }
    CHECK(wrong == 0);
    CHECK(kept > 100);
}

TEST(circle_cut_removes_the_elliptic_cylinder) {
    const Mesh sphere = makeIcosphere(5);
    const double v0 = volume(sphere);
    for (const DragEllipse& e : {DragEllipse{{0.0f, 0.0f}, {0.4f, 0.4f}}, DragEllipse{{0.05f, 0.1f}, {0.55f, 0.3f}}}) {
        // The sphere inside the cylinder x'^2/a^2 + y'^2/b^2 < 1: the integral of the chord 2 sqrt(1 - x^2 - y^2).
        double expected = 0.0;
        constexpr int kN = 400;
        for (int i = 0; i < kN; ++i)
            for (int j = 0; j < kN; ++j) {
                const glm::vec2 p = e.centre + glm::vec2((float(i) + 0.5f) / kN * 2.0f - 1.0f, (float(j) + 0.5f) / kN * 2.0f - 1.0f) * e.radius;
                const float r2 = p.x * p.x + p.y * p.y;
                if (ellipseValue(e, p) < 1.0f && r2 < 1.0f)
                    expected += 2.0 * std::sqrt(1.0 - double(r2)) * double(e.radius.x) * double(e.radius.y) * 4.0 / double(kN * kN);
            }
        Document d = makeDoc(sphere);
        CHECK(registry()
                  .execute(d, "cut",
                           {{"shape", std::string("lasso")}, {"points", pointsString(ellipsePolygon(e))}, {"mode", std::string("away")}, {"closed", true}})
                  .ok);
        CHECK(isWatertight(d.mesh));
        const double removed = v0 - volume(d.mesh);
        std::printf("circle cut: removed %.4f, the cylinder holds %.4f\n", removed, expected);
        CHECK(std::abs(removed - expected) < 0.03 * expected);
        // Nothing of the sphere is left inside the ellipse (a margin for the rim's chord).
        const glm::mat4 vp = frontViewProj(sphere);
        size_t inside = 0;
        for (const glm::vec3& p : d.mesh.positions) {
            glm::vec2 ndc;
            if (projectToNdc(vp, p, ndc) && ellipseValue(e, ndc) < 0.97f) ++inside;
        }
        CHECK(inside == 0);
    }
}
