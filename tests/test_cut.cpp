// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Cut (#101, #91): every shape and mode on a sphere, a low-poly cube and a two-Part scene; watertight results,
// attributes, symmetry, perspective views, open meshes and undo. Also Mask Lasso "Front faces only" (#91.3).
#include "TestHarness.h"

#include "core/Commands.h"
#include "core/Cut.h"
#include "core/Masking.h"
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

double volume(const Mesh& m) {
    double v = 0.0;
    for (const glm::uvec3& t : m.triangles)
        v += glm::dot(glm::dvec3(m.positions[t.x]), glm::cross(glm::dvec3(m.positions[t.y]), glm::dvec3(m.positions[t.z])));
    return v / 6.0;
}

CommandResult cut(Document& d, const std::string& shape, const std::string& points, const std::string& mode,
                  Args extra = {}) {
    extra["shape"] = shape;
    extra["points"] = points;
    extra["mode"] = mode;
    return registry().execute(d, "cut", extra);
}

std::string matrixString(const glm::mat4& m) {
    std::string s;
    char buf[32];
    for (int i = 0; i < 16; ++i) {
        std::snprintf(buf, sizeof buf, "%s%.9g", i ? " " : "", double(m[i / 4][i % 4]));
        s += buf;
    }
    return s;
}

// Vertices inside an axis box of x, y (the front view's NDC is world x, y for a unit sphere) and above z.
size_t countIn(const Mesh& m, float x0, float x1, float y0, float y1, float zAbove = -1e9f) {
    size_t n = 0;
    for (const glm::vec3& p : m.positions)
        if (p.x > x0 && p.x < x1 && p.y > y0 && p.y < y1 && p.z > zAbove) ++n;
    return n;
}

const double kSphere = 4.0 / 3.0 * 3.14159265358979;

} // namespace

TEST(cut_command_registered) {
    CHECK(registry().find("cut") != nullptr);
    CHECK(registry().toolSchemaJson().find("\"cut\"") != std::string::npos);
    CutShape s;
    CutMode m;
    CHECK(parseCutShape("polyline", s) && s == CutShape::Polyline);
    CHECK(parseCutMode("add", m) && m == CutMode::Add);
    CHECK(!parseCutMode("trim", m));
}

TEST(cut_line_away_halves_sphere) {
    Document d = makeDoc(makeIcosphere(4));
    CHECK(isWatertight(d.mesh));
    const double v0 = volume(d.mesh);
    // A vertical line upwards: the left (-x) half goes, a flat cap closes the right half.
    const CommandResult r = cut(d, "line", "0.013 -2 0.013 2", "away");
    CHECK(r.ok);
    CHECK(isWatertight(d.mesh));
    size_t left = 0, onCap = 0;
    for (const glm::vec3& p : d.mesh.positions) {
        if (p.x < 0.013f - 1e-4f) ++left;
        if (std::abs(p.x - 0.013f) < 1e-4f) ++onCap;
    }
    CHECK(left == 0);
    CHECK(onCap > 20);
    const double v1 = volume(d.mesh);
    CHECK(v1 > 0.4 * v0 && v1 < 0.5 * v0); // a bit under half: the line is right of centre
    // The cap is a face group of its own.
    CHECK(d.mesh.hasPolygroups());
    // Flip keeps the other half.
    Document e = makeDoc(makeIcosphere(4));
    CHECK(cut(e, "line", "0.013 -2 0.013 2", "away", {{"flip", true}}).ok);
    CHECK(isWatertight(e.mesh));
    for (const glm::vec3& p : e.mesh.positions) CHECK(p.x < 0.013f + 1e-4f);
    CHECK(std::abs(volume(e.mesh) + v1 - v0) < 0.02 * v0);
    // Undo brings the sphere back.
    CHECK(registry().execute(d, "undo").ok);
    CHECK(std::abs(volume(d.mesh) - v0) < 1e-6);
}

TEST(cut_every_shape_away_through_sphere) {
    struct Case {
        const char* shape;
        const char* points;
        bool closed;
    } cases[] = {
        {"rect", "-0.31 -0.29 0.33 0.27", false},
        {"lasso", "-0.5 -0.47 0.52 -0.45 0.03 0.61", false},
        {"polyline", "-0.5 -0.47 0.52 -0.45 0.03 0.61", true},
    };
    for (const Case& c : cases) {
        Document d = makeDoc(makeIcosphere(4));
        const double v0 = volume(d.mesh);
        const CommandResult r = cut(d, c.shape, c.points, "away", {{"closed", c.closed}});
        CHECK(r.ok);
        CHECK(isWatertight(d.mesh)); // a tunnel through the sphere
        CHECK(volume(d.mesh) < 0.9 * v0);
        CHECK(countIn(d.mesh, -0.1f, 0.1f, -0.2f, 0.1f) == 0);
    }
    // Open polyline: a bent line, cutting the top-left away.
    Document d = makeDoc(makeIcosphere(4));
    CHECK(cut(d, "polyline", "-2 0.21  0.02 0.21  0.53 -2", "away").ok);
    CHECK(isWatertight(d.mesh));
    CHECK(countIn(d.mesh, -2.0f, -0.05f, 0.25f, 2.0f) == 0);
    CHECK(countIn(d.mesh, -2.0f, -0.05f, -2.0f, 0.15f) > 0);
}

TEST(cut_in_and_add_on_sphere) {
    // In: a flat-bottomed pocket 0.4 deep (depth 0.2 of the size 2) in the front.
    Document d = makeDoc(makeIcosphere(4));
    const double v0 = volume(d.mesh);
    CHECK(cut(d, "rect", "-0.31 -0.29 0.33 0.27", "in", {{"depth", 0.2}}).ok);
    CHECK(isWatertight(d.mesh));
    CHECK(volume(d.mesh) < v0 && volume(d.mesh) > 0.8 * v0);
    float bottom = 1e9f;
    for (const glm::vec3& p : d.mesh.positions)
        if (std::abs(p.x) < 0.4f && std::abs(p.y) < 0.4f && p.z > 0.3f) bottom = std::min(bottom, p.z); // floor corners
    CHECK(bottom > 0.5f && bottom < 0.62f); // the pocket floor: front (~0.95) minus 0.4
    CHECK(countIn(d.mesh, -0.2f, 0.2f, -0.2f, 0.2f, bottom + 0.01f) == 0);
    CHECK(countIn(d.mesh, -0.2f, 0.2f, -0.2f, 0.2f) > 0); // the back of the sphere is still there

    // Add: the rectangle extruded 0.4 towards the viewer, anchored in the sphere.
    Document a = makeDoc(makeIcosphere(4));
    CHECK(cut(a, "rect", "-0.31 -0.29 0.33 0.27", "add", {{"depth", 0.2}}).ok);
    CHECK(isWatertight(a.mesh));
    CHECK(volume(a.mesh) > v0 * 1.02);
    CHECK(a.mesh.bounds().max.z > 1.3f);
    for (const char* shape : {"lasso", "polyline"}) {
        Document b = makeDoc(makeIcosphere(4));
        CHECK(cut(b, shape, "-0.5 -0.47 0.52 -0.45 0.03 0.61", "add", {{"depth", 0.1}, {"closed", true}}).ok);
        CHECK(isWatertight(b.mesh));
        CHECK(volume(b.mesh) > v0);
        Document c = makeDoc(makeIcosphere(4));
        CHECK(cut(c, shape, "-0.5 -0.47 0.52 -0.45 0.03 0.61", "in", {{"depth", 0.1}, {"closed", true}}).ok);
        CHECK(isWatertight(c.mesh));
        CHECK(volume(c.mesh) < v0);
    }
    // Line In: a step cut off the front.
    Document l = makeDoc(makeIcosphere(4));
    CHECK(cut(l, "line", "0.013 -2 0.013 2", "in", {{"depth", 0.3}}).ok);
    CHECK(isWatertight(l.mesh));
    CHECK(volume(l.mesh) < v0);
}

TEST(cut_low_poly_cube_hole_inside_one_triangle) {
    // A small lasso inside one face of a 12-triangle cube: the cut's outline sits inside a triangle (a hole to bridge).
    Document d = makeDoc(makeCube(1));
    const size_t t0 = d.mesh.triangleCount();
    CHECK(cut(d, "lasso", "-0.21 -0.18 0.23 -0.2 0.27 0.22 -0.19 0.24", "in", {{"depth", 0.25}}).ok);
    CHECK(isWatertight(d.mesh));
    CHECK(d.mesh.triangleCount() > t0);
    CHECK(std::abs(volume(d.mesh) - (8.0 - 0.44 * 0.43 * 0.5)) < 0.1);
    Document a = makeDoc(makeCube(1));
    CHECK(cut(a, "rect", "-0.21 -0.18 0.23 0.24", "add", {{"depth", 0.25}}).ok);
    CHECK(isWatertight(a.mesh));
    CHECK(volume(a.mesh) > 8.0);
    Document w = makeDoc(makeCube(1));
    CHECK(cut(w, "rect", "-0.21 -0.18 0.23 0.24", "away").ok);
    CHECK(isWatertight(w.mesh));
    CHECK(std::abs(volume(w.mesh) - (8.0 - 2.0 * 0.44 * 0.42)) < 0.05);
}

TEST(cut_perspective_view_and_symmetry) {
    Document d = makeDoc(makeIcosphere(4));
    const glm::mat4 vp = glm::perspective(glm::radians(40.0f), 1.3f, 0.1f, 100.0f) *
                         glm::lookAt(glm::vec3(0.7f, 0.9f, 4.0f), glm::vec3(0.0f), glm::vec3(0, 1, 0));
    const double v0 = volume(d.mesh);
    CHECK(cut(d, "lasso", "-0.1 -0.1 0.12 -0.08 0.02 0.15", "away", {{"view_proj", matrixString(vp)}}).ok);
    CHECK(isWatertight(d.mesh));
    CHECK(volume(d.mesh) < v0);
    CHECK(cut(d, "rect", "-0.05 -0.4 0.05 0.4", "add", {{"view_proj", matrixString(vp)}, {"depth", 0.1}}).ok);
    CHECK(isWatertight(d.mesh));

    // X symmetry: a rectangle on the right also cuts its mirror image on the left.
    Document s = makeDoc(makeIcosphere(4));
    CHECK(cut(s, "rect", "0.31 -0.19 0.61 0.21", "away", {{"symmetry", std::string("x")}}).ok);
    CHECK(isWatertight(s.mesh));
    CHECK(countIn(s.mesh, 0.36f, 0.56f, -0.14f, 0.16f) == 0);
    CHECK(countIn(s.mesh, -0.56f, -0.36f, -0.14f, 0.16f) == 0);
    CHECK(countIn(s.mesh, -0.2f, 0.2f, -0.14f, 0.16f) > 0);
}

TEST(cut_two_part_scene_cuts_active_part_only) {
    Document d = makeDoc(makeIcosphere(4));
    CHECK(registry().execute(d, "subtool_append", {{"type", std::string("sphere")}}).ok);
    CHECK(d.subtools.size() == 2 && d.activeSubtool == 1);
    const Mesh other = d.subtoolMesh(0);
    const double v0 = volume(d.mesh);
    const AABB b = d.mesh.bounds();
    // The default view frames the active Part: a line through its middle.
    CHECK(cut(d, "line", "0.013 -2 0.013 2", "away").ok);
    CHECK(isWatertight(d.mesh));
    CHECK(volume(d.mesh) < 0.6 * v0);
    for (const glm::vec3& p : d.mesh.positions) CHECK(p.x > b.center().x - 0.01f * b.extent().x);
    // The other Part is untouched.
    CHECK(d.subtoolMesh(0).triangleCount() == other.triangleCount());
    CHECK(d.subtoolMesh(0).positions == other.positions);
    // Undo and redo.
    CHECK(registry().execute(d, "undo").ok);
    CHECK(std::abs(volume(d.mesh) - v0) < 1e-6);
    CHECK(registry().execute(d, "redo").ok);
    CHECK(volume(d.mesh) < 0.6 * v0);
    // Every mode on the other Part too.
    CHECK(registry().execute(d, "subtool_select", {{"index", 0.0}}).ok);
    for (const char* mode : {"away", "in", "add"}) {
        const size_t before = d.undo.undoCount();
        CHECK(cut(d, "rect", "-0.31 -0.29 0.33 0.27", mode, {{"depth", 0.15}}).ok);
        CHECK(isWatertight(d.mesh));
        CHECK(d.undo.undoCount() == before + 1);
    }
}

TEST(cut_attributes_misses_and_open_meshes) {
    Document d = makeDoc(makeIcosphere(4));
    d.mesh.ensureMask(1.0f);
    d.mesh.ensureColors(glm::vec3(0.2f, 0.4f, 0.6f));
    CHECK(cut(d, "line", "0.013 -2 0.013 2", "away").ok);
    size_t masked = 0;
    for (float m : d.mesh.mask) masked += m > 0.99f;
    CHECK(masked > d.mesh.vertexCount() / 2); // the kept surface keeps its mask; only the cap's inner corners are free
    for (const glm::vec3& c : d.mesh.colors) CHECK(glm::length(c - glm::vec3(0.2f, 0.4f, 0.6f)) < 1e-3f);

    // A shape off the Part changes nothing and adds no undo step.
    Document m = makeDoc(makeIcosphere(3));
    const size_t steps = m.undo.undoCount();
    const CommandResult miss = cut(m, "rect", "3 3 4 4", "away");
    CHECK(miss.ok && miss.message.find("misses") != std::string::npos);
    CHECK(m.undo.undoCount() == steps);
    // Covering all of it is refused.
    CHECK(!cut(m, "rect", "-5 -5 5 5", "away").ok);
    // Bad arguments.
    CHECK(!cut(m, "line", "0 0", "away").ok);
    CHECK(!cut(m, "lasso", "0 0 1 1", "away").ok);
    CHECK(!cut(m, "lasso", "0 0 1 1 0.5 0.5", "sideways").ok);

    // An open plane is trimmed without a cap; Add needs a closed Part.
    Document p = makeDoc(makePlane(16));
    const size_t t0 = p.mesh.triangleCount();
    const glm::mat4 top = glm::ortho(-1.0f, 1.0f, -1.0f, 1.0f, 0.1f, 10.0f) *
                          glm::lookAt(glm::vec3(0, 5, 0), glm::vec3(0.0f), glm::vec3(0, 0, -1));
    const AABB pb = p.mesh.bounds();
    const bool flat = pb.extent().y < 1e-4f; // makePlane lies in XZ
    const std::string view = flat ? matrixString(top) : std::string();
    Args a{{"view_proj", view}};
    if (view.empty()) a.erase("view_proj");
    CHECK(cut(p, "line", "0.013 -2 0.013 2", "away", a).ok);
    CHECK(p.mesh.triangleCount() < t0);
    CHECK(!isWatertight(p.mesh));
    CHECK(!cut(p, "rect", "-0.3 -0.3 0.3 0.3", "add", a).ok);
}

TEST(mask_lasso_front_faces_only) {
    Document d = makeDoc(makeIcosphere(3));
    // The whole sphere seen from the front: only the half facing the viewer (+Z) is masked.
    CHECK(registry().execute(d, "mask_rect", {{"x0", -2.0}, {"y0", -2.0}, {"x1", 2.0}, {"y1", 2.0}, {"front_faces_only", true}}).ok);
    size_t wrong = 0, masked = 0;
    for (uint32_t v = 0; v < d.mesh.vertexCount(); ++v) {
        const float z = d.mesh.normals[v].z;
        const bool m = d.mesh.mask[v] > 0.5f;
        masked += m;
        if (std::abs(z) > 0.05f && m != (z > 0.0f)) ++wrong;
    }
    CHECK(wrong == 0);
    CHECK(masked > 0 && masked < d.mesh.vertexCount());
    CHECK(registry().execute(d, "mask_clear").ok);
    CHECK(registry().execute(d, "mask_lasso", {{"points", std::string("-2 -2 2 -2 0 2")}, {"front_faces_only", true}}).ok);
    for (uint32_t v = 0; v < d.mesh.vertexCount(); ++v)
        if (d.mesh.normals[v].z < -0.05f) CHECK(d.mesh.mask[v] == 0.0f);
    // Without the option the back is masked too.
    CHECK(registry().execute(d, "mask_lasso", {{"points", std::string("-2 -2 2 -2 0 2")}}).ok);
    size_t back = 0;
    for (uint32_t v = 0; v < d.mesh.vertexCount(); ++v) back += d.mesh.normals[v].z < -0.5f && d.mesh.mask[v] > 0.5f;
    CHECK(back > 0);
}
