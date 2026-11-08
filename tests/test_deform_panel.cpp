// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The Deform panel's shape operations (core/Deform.h, docs/design/deform-panel.md, foirgairwolf on Discord): exact math on
// small meshes, axis combinations, Soft, the mask, the stack text and the one-undo-step stack command.
#include "TestHarness.h"

#include "core/Commands.h"
#include "core/Deform.h"
#include "core/Primitives.h"
#include "core/SurfaceQuery.h"

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

// The distance from q to the nearest vertex of P (brute force).
static float nearestDistance(const std::vector<vec3>& P, const vec3& q) {
    float best = 1e30f;
    for (const vec3& p : P) best = std::min(best, glm::distance(p, q));
    return best;
}

TEST(deform_panel_noise_and_symmetrize) {
    // Noise (foirgairwolf on Discord): only along the chosen axis, bounded, the same every run, scale changes it.
    const Mesh& c = cube();
    std::vector<vec3> P = deformPositions(c, DeformOp::Noise, opts(100, DeformY));
    float most = 0.0f;
    for (size_t i = 0; i < P.size(); ++i) {
        CHECK(P[i].x == c.positions[i].x && P[i].z == c.positions[i].z);
        most = std::max(most, std::abs(P[i].y - c.positions[i].y));
    }
    CHECK(most > 0.01f && most < 0.05f * 2.0f * 1.6f); // up to 5% of the longest side (2) at 100, about
    CHECK(deformPositions(c, DeformOp::Noise, opts(100, DeformY)) == P);
    DeformOptions fine = opts(100, DeformY);
    fine.scale = 16.0f;
    CHECK(deformPositions(c, DeformOp::Noise, fine) != P);
    CHECK(deformPositions(c, DeformOp::Noise, opts(0, DeformY)) == c.positions);

    // Symmetrize: a sphere with a bulge on its +X side; the + side copied over makes it symmetric across X = 0
    // (the surface matches its mirror image both ways) and leaves the + side alone.
    Mesh s = makeIcosphere(4, 1.0f);
    for (vec3& p : s.positions)
        if (p.x > 0.2f) p *= 1.0f + 0.1f * glm::smoothstep(0.2f, 0.8f, p.x);
    auto asymmetry = [&](const std::vector<vec3>& Q) {
        Mesh a = s, b = s;
        a.positions = Q;
        b.positions = Q;
        for (vec3& p : b.positions) p.x = -p.x;
        return measureDeviation(a, b).max;
    };
    CHECK(asymmetry(s.positions) > 0.05f);
    P = deformPositions(s, DeformOp::Symmetrize, opts(100, DeformX));
    for (size_t i = 0; i < P.size(); ++i)
        if (s.positions[i].x >= 0.0f) CHECK(P[i] == s.positions[i]);
    CHECK(asymmetry(P) < 0.01f);
    float wide = 0.0f;
    for (const vec3& p : P) wide = std::max(wide, -p.x);
    CHECK(wide > 1.05f); // the bulge is on the - side now
    // A negative amount copies the - side: the bulge goes.
    P = deformPositions(s, DeformOp::Symmetrize, opts(-100, DeformX));
    for (size_t i = 0; i < P.size(); ++i)
        if (s.positions[i].x <= 0.0f) CHECK(P[i] == s.positions[i]);
    CHECK(asymmetry(P) < 0.01f);
    wide = 0.0f;
    for (const vec3& p : P) wide = std::max(wide, p.x);
    CHECK(wide < 1.01f);
    // Average: symmetric too, about half the bulge on each side.
    DeformOptions avg = opts(100, DeformX);
    avg.average = true;
    P = deformPositions(s, DeformOp::Symmetrize, avg);
    CHECK(asymmetry(P) < 0.01f);
    wide = 0.0f;
    for (const vec3& p : P) wide = std::max(wide, p.x);
    CHECK(wide > 1.02f && wide < 1.08f);
    // Half the amount: half way there.
    P = deformPositions(s, DeformOp::Symmetrize, opts(50, DeformX));
    CHECK(asymmetry(P) > 0.01f && asymmetry(P) < 0.7f * asymmetry(s.positions));
    // The mask holds what it covers.
    Mesh masked = s;
    std::vector<float>& mask = masked.ensureMask();
    for (uint32_t v = 0; v < masked.vertexCount(); ++v) mask[v] = masked.positions[v].y > 0.0f ? 1.0f : 0.0f;
    P = deformPositions(masked, DeformOp::Symmetrize, opts(-100, DeformX));
    for (uint32_t v = 0; v < masked.vertexCount(); ++v)
        if (mask[v] == 1.0f) CHECK(P[v] == masked.positions[v]);
    // Already symmetric: nothing moves.
    const Mesh sym = makeIcosphere(3, 1.0f);
    P = deformPositions(sym, DeformOp::Symmetrize, opts(100, DeformX | DeformZ));
    for (size_t i = 0; i < P.size(); ++i) CHECK(near(P[i], sym.positions[i], 1e-5f));

    // The stack text keeps scale= and average.
    std::vector<DeformStep> steps;
    std::string err;
    CHECK(parseDeformStack("noise 30 xz scale=6; symmetrize -50 x average; polish 40 edge=35", steps, err));
    CHECK(steps.size() == 3 && steps[0].options.scale == 6.0f && steps[1].options.average && steps[1].options.amount == -50.0f);
    CHECK(steps[2].options.edge == 35.0f);
    CHECK(deformStackText(steps) == "noise 30 xz scale=6; symmetrize -50 x average; polish 40 edge=35");
}

TEST(deform_panel_surface_polish) {
    // Surface Polish (Tool > Geometry, foirgairwolf on Discord): a noisy cube comes out with flatter faces and its
    // corners where they were; one undo step; amount 0 does nothing.
    Mesh noisy = makeCube(16, 2.0f);
    uint32_t seed = 12345u;
    for (vec3& p : noisy.positions) {
        seed = seed * 1664525u + 1013904223u;
        const float r = (float(seed >> 8) / float(1u << 24) - 0.5f) * 0.02f;
        // Along the face normal (the axis the point is furthest out on).
        int a = 0;
        for (int k = 1; k < 3; ++k)
            if (std::abs(p[k]) > std::abs(p[a])) a = k;
        if (std::abs(p[a]) > 0.999f) p[a] += r * (p[a] > 0.0f ? 1.0f : -1.0f);
    }
    auto roughness = [](const std::vector<vec3>& P) { // mean distance off the face planes, face interiors only
        double sum = 0.0;
        int n = 0;
        for (const vec3& p : P) {
            int a = 0;
            for (int k = 1; k < 3; ++k)
                if (std::abs(p[k]) > std::abs(p[a])) a = k;
            const float other = std::max(std::abs(p[(a + 1) % 3]), std::abs(p[(a + 2) % 3]));
            if (other > 0.8f) continue; // near an edge
            sum += std::abs(std::abs(p[a]) - 1.0f);
            ++n;
        }
        return sum / std::max(n, 1);
    };
    Document d = makeDoc(noisy);
    const std::vector<vec3> before = d.mesh.positions;
    CommandRegistry reg;
    const CommandResult r = reg.execute(d, "surface_polish", {{"amount", 1.0}});
    CHECK(r.ok && r.message.find("Surface Polish 1.00") == 0);
    CHECK(d.undo.undoCount() == 1);
    CHECK(roughness(d.mesh.positions) < 0.35 * roughness(before));
    // The corners stay sharp: the vertex nearest each corner is still near it.
    for (int k = 0; k < 8; ++k) {
        const vec3 corner((k & 1) ? 1.0f : -1.0f, (k & 2) ? 1.0f : -1.0f, (k & 4) ? 1.0f : -1.0f);
        CHECK(nearestDistance(d.mesh.positions, corner) < 0.04f);
    }
    CHECK(d.undo.undo(d.mesh, d.bvh) && d.mesh.positions == before);
    CHECK(reg.execute(d, "surface_polish", {{"amount", 0.0}}).ok);
    CHECK(d.undo.undoCount() == 0);
}

TEST(deform_panel_large_mesh_timing) {
    // SF_DEFORM_BENCH=1 (2.6M vertices) or 2 (10.5M): each shape operation applied to the whole Part.
    if (!std::getenv("SF_DEFORM_BENCH")) return;
    Document d = makeDoc(makeIcosphere(std::atoi(std::getenv("SF_DEFORM_BENCH")) > 1 ? 10 : 9, 1.0f));
    std::printf("bench mesh: %zu vertices\n", d.mesh.vertexCount());
    using clock = std::chrono::steady_clock;
    { // Symmetrize on the untouched sphere with one side pushed out (the usual case), copy and average.
        Mesh s = d.mesh;
        for (vec3& p : s.positions)
            if (p.x > 0.2f) p *= 1.05f;
        for (bool average : {false, true}) {
            DeformOptions o = opts(100, DeformX);
            o.average = average;
            const auto t0 = clock::now();
            const std::vector<vec3> P = deformPositions(s, DeformOp::Symmetrize, o);
            std::printf("deform Symmetrize%s on a sphere: positions %.0f ms\n", average ? " Average" : "",
                        std::chrono::duration<double, std::milli>(clock::now() - t0).count());
        }
        DeformOptions o; // Surface Polish at 1: Polish with 12 passes
        o.amount = 100.0f;
        o.passes = 12;
        const auto t0 = clock::now();
        const std::vector<vec3> P = deformPositions(s, DeformOp::Polish, o);
        std::printf("Surface Polish 1.0 on a sphere: positions %.0f ms\n",
                    std::chrono::duration<double, std::milli>(clock::now() - t0).count());
    }
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
