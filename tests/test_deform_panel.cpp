// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The Deform panel's shape operations (core/Deform.h, docs/design/deform-panel.md, foirgairwolf on Discord): exact math on
// small meshes, axis combinations, Soft, the mask, the stack text and the one-undo-step stack command.
#include "TestHarness.h"

#include "core/Commands.h"
#include "core/Deform.h"
#include "core/Primitives.h"

#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>

using namespace sf;
using glm::vec3;

namespace {

DeformOptions opts(float amount, uint8_t axes, bool soft = false, float direction = 0.0f) {
    DeformOptions o;
    o.amount = amount;
    o.axes = axes;
    o.soft = soft;
    o.direction = direction;
    return o;
}

bool near(const vec3& a, const vec3& b, float eps = 1e-4f) { return glm::distance(a, b) < eps; }

// A cube of side 2 centred on the origin (pivot = origin, ext = 1 on every axis).
const Mesh& cube() {
    static const Mesh m = makeCube(6, 2.0f);
    return m;
}

} // namespace

TEST(deform_panel_transform_ops) {
    const Mesh& m = cube();
    // Move: amount percent of the half-size, on the chosen axes only.
    std::vector<vec3> P = deformPositions(m, DeformOp::Move, opts(50, DeformX | DeformZ));
    for (size_t i = 0; i < P.size(); ++i) CHECK(near(P[i], m.positions[i] + vec3(0.5f, 0.0f, 0.5f)));
    // Turn: degrees about the axis through the centre.
    P = deformPositions(m, DeformOp::Turn, opts(90, DeformY));
    const glm::quat q = glm::angleAxis(glm::radians(90.0f), vec3(0, 1, 0));
    for (size_t i = 0; i < P.size(); ++i) CHECK(near(P[i], q * m.positions[i]));
    // Scale: 100 doubles the chosen components.
    P = deformPositions(m, DeformOp::Scale, opts(100, DeformX | DeformY));
    for (size_t i = 0; i < P.size(); ++i) CHECK(near(P[i], m.positions[i] * vec3(2, 2, 1)));
    // Squash: half as tall, sqrt(2) as wide: the volume stays.
    P = deformPositions(m, DeformOp::Squash, opts(100, DeformY));
    const float w = std::sqrt(2.0f);
    for (size_t i = 0; i < P.size(); ++i) CHECK(near(P[i], m.positions[i] * vec3(w, 0.5f, w)));
}

TEST(deform_panel_bend_is_an_arc) {
    // A strip along X bends toward +Y: every vertex of the centre line lands on the circle of radius 2 ext / angle.
    Mesh m = makePlane(16, 2.0f); // y = 0
    const float angle = 90.0f, R = 2.0f / glm::radians(angle);
    const std::vector<vec3> P = deformPositions(m, DeformOp::Bend, opts(angle, DeformX));
    for (size_t i = 0; i < P.size(); ++i) {
        CHECK(std::abs(glm::distance(vec3(P[i].x, P[i].y, 0.0f), vec3(0.0f, R, 0.0f)) - R) < 1e-4f);
        CHECK(std::abs(P[i].z - m.positions[i].z) < 1e-6f); // across the bend, nothing changes
        // The ends turn by half the angle each: x = R sin 45, y = R (1 - cos 45).
        if (std::abs(m.positions[i].x - 1.0f) < 1e-5f)
            CHECK(near(vec3(P[i].x, P[i].y, 0), vec3(R * std::sin(glm::radians(45.0f)), R * (1 - std::cos(glm::radians(45.0f))), 0)));
    }
    // Direction 180 bends the other way; a negative angle is the same.
    const std::vector<vec3> D = deformPositions(m, DeformOp::Bend, opts(angle, DeformX, false, 180.0f));
    const std::vector<vec3> N = deformPositions(m, DeformOp::Bend, opts(-angle, DeformX));
    for (size_t i = 0; i < P.size(); ++i) {
        CHECK(near(D[i], vec3(P[i].x, -P[i].y, P[i].z)));
        CHECK(near(N[i], D[i]));
    }
    // Soft: the centre line keeps its length, the middle stays put, and the ends turn less steeply at the tips.
    const std::vector<vec3> S = deformPositions(m, DeformOp::Bend, opts(angle, DeformX, true));
    std::vector<std::pair<float, vec3>> row;
    for (size_t i = 0; i < S.size(); ++i)
        if (std::abs(m.positions[i].z) < 1e-5f) row.push_back({m.positions[i].x, S[i]});
    std::sort(row.begin(), row.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    float length = 0.0f;
    for (size_t i = 1; i < row.size(); ++i) length += glm::distance(row[i].second, row[i - 1].second);
    CHECK(std::abs(length - 2.0f) < 4e-3f); // chords of the 16 segments, a little under the arc
    CHECK(near(row[row.size() / 2].second, vec3(0.0f)));
}

TEST(deform_panel_twist_taper_shear_depth) {
    const Mesh& m = cube();
    // Twist 180 about Y: the top turns +90, the bottom -90, the middle stays.
    std::vector<vec3> P = deformPositions(m, DeformOp::Twist, opts(180, DeformY));
    for (size_t i = 0; i < P.size(); ++i) {
        const float y = m.positions[i].y;
        const glm::quat q = glm::angleAxis(glm::radians(90.0f * y), vec3(0, 1, 0));
        CHECK(near(P[i], q * m.positions[i]));
    }
    // Taper 50 along Y: the top 1.5 times as wide, the bottom half.
    P = deformPositions(m, DeformOp::Taper, opts(50, DeformY));
    for (size_t i = 0; i < P.size(); ++i) {
        const vec3 p = m.positions[i];
        const float f = 1.0f + 0.5f * p.y;
        CHECK(near(P[i], vec3(p.x * f, p.y, p.z * f)));
    }
    // Shear 100 along Y: leans 45 degrees toward +X (direction 0 for the Y axis).
    P = deformPositions(m, DeformOp::Shear, opts(100, DeformY));
    for (size_t i = 0; i < P.size(); ++i) CHECK(near(P[i], m.positions[i] + vec3(m.positions[i].y, 0, 0)));
    // Soft ends at the same place but moves the middle more steeply.
    P = deformPositions(m, DeformOp::Shear, opts(100, DeformY, true));
    for (size_t i = 0; i < P.size(); ++i) {
        const float y = m.positions[i].y;
        CHECK(near(P[i], m.positions[i] + vec3(y * (3 - y * y) * 0.5f, 0, 0)));
    }
    // Depth: the + end wider, the - end narrower, the axis itself unchanged.
    P = deformPositions(m, DeformOp::Depth, opts(40, DeformY));
    for (size_t i = 0; i < P.size(); ++i) {
        const vec3 p = m.positions[i];
        const float f = 1.0f / (1.0f - 0.2f * p.y);
        CHECK(near(P[i], vec3(p.x * f, p.y, p.z * f)));
    }
}

TEST(deform_panel_shape_ops) {
    // Round on a cube: every vertex at the same distance from the centre; on XZ only: a cylinder about Y.
    const Mesh& m = cube();
    std::vector<vec3> P = deformPositions(m, DeformOp::Round, opts(100, DeformX | DeformY | DeformZ));
    const float r = glm::length(P[0]);
    for (const vec3& p : P) CHECK(std::abs(glm::length(p) - r) < 1e-4f);
    P = deformPositions(m, DeformOp::Round, opts(100, DeformX | DeformZ));
    const float rc = glm::length(glm::vec2(P[0].x, P[0].z));
    for (size_t i = 0; i < P.size(); ++i) {
        // (the centres of the top and bottom faces sit on the axis: they have no direction to go and stay)
        if (glm::length(glm::vec2(m.positions[i].x, m.positions[i].z)) > 1e-6f)
            CHECK(std::abs(glm::length(glm::vec2(P[i].x, P[i].z)) - rc) < 1e-4f);
        CHECK(P[i].y == m.positions[i].y);
    }
    // Box on a sphere: the same max-norm everywhere.
    const Mesh s = makeIcosphere(3, 1.0f);
    P = deformPositions(s, DeformOp::Box, opts(100, DeformX | DeformY | DeformZ));
    auto maxNorm = [](const vec3& p) { return std::max(std::abs(p.x), std::max(std::abs(p.y), std::abs(p.z))); };
    for (const vec3& p : P) CHECK(std::abs(maxNorm(p) - maxNorm(P[0])) < 1e-4f);
    // Flatten 50 along Y cuts the top half off at the middle; -50 the bottom half; the rest stays.
    P = deformPositions(s, DeformOp::Flatten, opts(50, DeformY));
    for (size_t i = 0; i < P.size(); ++i)
        CHECK(near(P[i], vec3(s.positions[i].x, std::min(s.positions[i].y, 0.0f), s.positions[i].z), 1e-5f));
    P = deformPositions(s, DeformOp::Flatten, opts(-50, DeformY));
    for (size_t i = 0; i < P.size(); ++i) CHECK(P[i].y >= -1e-5f);
    // Soft rounds the cut's edge but stays under the plane.
    P = deformPositions(s, DeformOp::Flatten, opts(50, DeformY, true));
    for (const vec3& p : P) CHECK(p.y <= 1e-5f);
    // Sag on a flat plane: the far corners droop by half the radius, the middle stays.
    const Mesh plane = makePlane(8, 2.0f);
    P = deformPositions(plane, DeformOp::Sag, opts(100, DeformY));
    const float R = std::sqrt(2.0f);
    for (size_t i = 0; i < P.size(); ++i) {
        const vec3 p = plane.positions[i];
        const float d = glm::length(glm::vec2(p.x, p.z));
        CHECK(near(P[i], vec3(p.x, -0.5f * R * (d / R) * (d / R), p.z)));
    }
}

TEST(deform_panel_axes_mask_hidden_pivot) {
    const Mesh& c = cube();
    // Two axes run one after the other, X then Z.
    std::vector<vec3> both = deformPositions(c, DeformOp::Turn, opts(90, DeformX | DeformZ));
    const glm::quat q = glm::angleAxis(glm::radians(90.0f), vec3(0, 0, 1)) * glm::angleAxis(glm::radians(90.0f), vec3(1, 0, 0));
    for (size_t i = 0; i < both.size(); ++i) CHECK(near(both[i], q * c.positions[i]));
    // No axis: nothing.
    CHECK(deformPositions(c, DeformOp::Twist, opts(90, 0)) == c.positions);
    // The mask holds what it covers; half masked moves half; hidden stays; borders of an open plane move.
    Mesh m = c;
    std::vector<float>& mask = m.ensureMask();
    for (uint32_t v = 0; v < m.vertexCount(); ++v) mask[v] = m.positions[v].y > 0.5f ? 1.0f : m.positions[v].y < -0.5f ? 0.5f : 0.0f;
    std::vector<vec3> P = deformPositions(m, DeformOp::Move, opts(100, DeformX));
    for (uint32_t v = 0; v < m.vertexCount(); ++v) {
        const float expect = 1.0f - mask[v];
        CHECK(near(P[v], m.positions[v] + vec3(expect, 0, 0)));
    }
    DeformOptions o = opts(100, DeformX);
    o.useMask = false;
    P = deformPositions(m, DeformOp::Move, o);
    for (uint32_t v = 0; v < m.vertexCount(); ++v) CHECK(near(P[v], m.positions[v] + vec3(1, 0, 0)));
    const Mesh plane = makePlane(8, 2.0f);
    P = deformPositions(plane, DeformOp::Move, opts(100, DeformY | DeformX));
    for (size_t i = 0; i < P.size(); ++i) CHECK(P[i] != plane.positions[i]);
    // A pivot: Turn about (1,0,0) keeps that point.
    o = opts(90, DeformY);
    o.hasPivot = true;
    o.pivot = vec3(1, 0, 0);
    P = deformPositions(c, DeformOp::Turn, o);
    const glm::quat qy = glm::angleAxis(glm::radians(90.0f), vec3(0, 1, 0));
    for (size_t i = 0; i < P.size(); ++i) CHECK(near(P[i], vec3(1, 0, 0) + qy * (c.positions[i] - vec3(1, 0, 0))));
}

TEST(deform_panel_stack_text_and_command) {
    std::vector<DeformStep> steps;
    std::string err;
    CHECK(parseDeformStack("bend 40 y soft dir=90; Twist 90 XZ;deform_smooth 30 volume nomask", steps, err));
    CHECK(steps.size() == 3);
    CHECK(steps[0].op == DeformOp::Bend && steps[0].options.amount == 40.0f && steps[0].options.soft &&
          steps[0].options.direction == 90.0f && steps[0].options.axes == DeformY);
    CHECK(steps[1].op == DeformOp::Twist && steps[1].options.axes == (DeformX | DeformZ) && !steps[1].options.soft);
    CHECK(steps[2].op == DeformOp::Smooth && steps[2].options.keepVolume && !steps[2].options.useMask);
    CHECK(deformStackText(steps) == "bend 40 y soft dir=90; twist 90 xz; smooth 30 volume nomask");
    std::vector<DeformStep> again;
    CHECK(parseDeformStack(deformStackText(steps), again, err) && again.size() == 3);
    CHECK(!parseDeformStack("wobble 3", steps, err) && err.find("wobble") != std::string::npos);
    CHECK(!parseDeformStack("bend 30 q", steps, err));
    CHECK(!parseDeformStack(" ; ", steps, err));
    // Amounts clamp to the range; a missing amount takes the default.
    CHECK(parseDeformStack("turn 999; taper", steps, err) && steps[0].options.amount == 180.0f && steps[1].options.amount == 40.0f);
    for (int i = 0; i < kDeformOpCount; ++i) {
        DeformOp op;
        CHECK(deformFromName(deformName(DeformOp(i)), op) && op == DeformOp(i));
        CHECK(deformFromName(deformCommand(DeformOp(i)), op) && op == DeformOp(i));
    }

    // deform_stack: the steps in order, one undo step, the same as running them on positions.
    Document d = makeDoc(makeCube(6, 2.0f));
    const std::vector<vec3> before = d.mesh.positions;
    CHECK(parseDeformStack("taper 30 y; bend 60 y soft; turn 20 x", steps, err));
    const std::vector<vec3> expect = deformStackPositions(d.mesh, steps);
    CommandRegistry reg;
    const CommandResult r = reg.execute(d, "deform_stack", {{"ops", std::string("taper 30 y; bend 60 y soft; turn 20 x")}});
    CHECK(r.ok && r.message.find("3 steps") != std::string::npos);
    CHECK(d.undo.undoCount() == 1);
    for (size_t i = 0; i < expect.size(); ++i) CHECK(near(d.mesh.positions[i], expect[i]));
    CHECK(d.undo.undo(d.mesh, d.bvh));
    CHECK(d.mesh.positions == before);
    CHECK(!reg.execute(d, "deform_stack", {{"ops", std::string("bend 30 y")}, {"pivot", std::string("1,2")}}).ok);
    // Every operation's own command runs on its defaults; each is one undo step.
    for (int i = 0; i < kDeformOpCount; ++i) {
        const size_t n = d.undo.undoCount();
        const CommandResult ri = reg.execute(d, deformCommand(DeformOp(i)), {{"axes", std::string("xy")}});
        CHECK(ri.ok);
        CHECK(d.undo.undoCount() == n + 1);
    }
    CHECK(!reg.execute(d, "deform_bend", {{"axes", std::string("w")}}).ok);
    // Everything masked: an error, nothing recorded.
    d.mesh.ensureMask();
    std::fill(d.mesh.mask.begin(), d.mesh.mask.end(), 1.0f);
    const size_t n = d.undo.undoCount();
    CHECK(!reg.execute(d, "deform_stack", {{"ops", std::string("bend 30 y")}}).ok);
    CHECK(d.undo.undoCount() == n);
}

TEST(deform_panel_large_mesh_timing) {
    // SF_DEFORM_BENCH=1 (2.6M vertices) or 2 (10.5M): each shape operation applied to the whole Part.
    if (!std::getenv("SF_DEFORM_BENCH")) return;
    Document d = makeDoc(makeIcosphere(std::atoi(std::getenv("SF_DEFORM_BENCH")) > 1 ? 10 : 9, 1.0f));
    std::printf("bench mesh: %zu vertices\n", d.mesh.vertexCount());
    using clock = std::chrono::steady_clock;
    for (int i = int(DeformOp::Move); i < kDeformOpCount; ++i) {
        DeformOptions o;
        float lo, hi;
        deformRange(DeformOp(i), lo, hi, o.amount);
        o.soft = true;
        const auto t0 = clock::now();
        const std::vector<vec3> P = deformPositions(d.mesh, DeformOp(i), o);
        const double pos = std::chrono::duration<double, std::milli>(clock::now() - t0).count();
        std::string err;
        const auto t1 = clock::now();
        const size_t moved = deformActivePart(d, DeformOp(i), o, err);
        const double ms = std::chrono::duration<double, std::milli>(clock::now() - t1).count();
        std::printf("deform %s: positions %.0f ms, apply %zu vertices %.0f ms\n", deformName(DeformOp(i)), pos, moved, ms);
    }
}

TEST(deform_panel_size_in_scene_units) {
    // A 2 x 2 x 2 cube centred at (1, 0, 0): exact sizes about its centre, axes left out keep theirs, one undo step.
    Mesh cubeMesh = makeCube(4, 2.0f);
    for (vec3& p : cubeMesh.positions) p.x += 1.0f;
    Document d = makeDoc(cubeMesh);
    const std::vector<vec3> before = d.mesh.positions;
    CommandRegistry reg;
    CommandResult r = reg.execute(d, "deform_size", {{"x", 5.0}, {"z", 0.5}});
    CHECK(r.ok && r.message.find("Size 5 x 2 x 0.5") == 0);
    AABB b = d.mesh.bounds();
    CHECK(near(b.min, vec3(-1.5f, -1.0f, -0.25f)) && near(b.max, vec3(3.5f, 1.0f, 0.25f)));
    CHECK(d.undo.undoCount() == 1);
    CHECK(d.undo.undo(d.mesh, d.bvh) && d.mesh.positions == before);
    // uniform: the largest side to 4, the others in proportion.
    CHECK(reg.execute(d, "deform_size", {{"x", 4.0}, {"y", 2.0}}).ok);
    CHECK(reg.execute(d, "deform_size", {{"uniform", 8.0}}).ok);
    b = d.mesh.bounds();
    CHECK(near(b.max - b.min, vec3(8.0f, 4.0f, 4.0f)));
    // Nothing given, a negative size, or a flat side to stretch: errors, nothing recorded.
    const size_t n = d.undo.undoCount();
    CHECK(!reg.execute(d, "deform_size", {}).ok);
    CHECK(!reg.execute(d, "deform_size", {{"x", -1.0}}).ok);
    Document flat = makeDoc(makePlane(4, 2.0f));
    CHECK(!reg.execute(flat, "deform_size", {{"y", 1.0}}).ok);
    CHECK(reg.execute(flat, "deform_size", {{"uniform", 1.0}}).ok); // the flat side stays flat
    CHECK(d.undo.undoCount() == n);
}
