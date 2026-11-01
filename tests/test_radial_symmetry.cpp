// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Radial and local symmetry (#63, core/Symmetry.h): the copies Sculptor::copies makes (turns, mirror + radial,
// a local pivot), their inverses, and that brushes, masks, vertex paint, visibility, insert and dynamic topology
// follow every copy, from C++ and through the commands.
#include "TestHarness.h"

#include "core/Commands.h"
#include "core/Masking.h"
#include "core/Polypaint.h"
#include "core/Primitives.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>

using namespace sf;

namespace {

const CommandRegistry& registry() {
    static CommandRegistry r;
    return r;
}

bool near(const glm::vec3& a, const glm::vec3& b, float eps = 1e-4f) { return glm::length(a - b) < eps; }

bool orthonormal(const glm::mat3& m) {
    const glm::mat3 t = glm::transpose(m) * m;
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            if (std::abs(t[i][j] - (i == j ? 1.0f : 0.0f)) > 1e-5f) return false;
    return true;
}

Symmetry radial(int n, int axis, bool x = false) {
    Symmetry s{x, false, false};
    s.radialEnabled = true;
    s.radial = n;
    s.radialAxis = axis;
    return s;
}

// The images of `p` under every copy, each distinct one once.
std::vector<glm::vec3> images(const Symmetry& s, const glm::vec3& p) {
    std::vector<glm::vec3> out;
    for (const SymCopy& c : Sculptor::copies(s)) {
        const glm::vec3 q = c.point(p);
        if (std::none_of(out.begin(), out.end(), [&](const glm::vec3& o) { return near(o, q); })) out.push_back(q);
    }
    return out;
}

float maxMoved(const Mesh& before, const Mesh& after, const glm::vec3& around, float radius) {
    float best = 0.0f;
    for (size_t v = 0; v < before.vertexCount(); ++v)
        if (glm::distance(before.positions[v], around) < radius)
            best = std::max(best, glm::distance(before.positions[v], after.positions[v]));
    return best;
}

} // namespace

TEST(radial_copies_count_and_order) {
    // Mirror-only symmetry is unchanged: the same copies, in the same order, as the mirror signs.
    const Symmetry xz{true, false, true};
    const std::vector<SymCopy> m = Sculptor::copies(xz);
    const std::vector<glm::vec3> signs = Sculptor::mirrors(xz);
    CHECK(m.size() == signs.size());
    for (size_t i = 0; i < m.size() && i < signs.size(); ++i)
        CHECK(near(m[i].point(glm::vec3(1, 2, 3)), glm::vec3(1, 2, 3) * signs[i]));
    // Radial off (or a count of 1): just the identity.
    Symmetry off{false, false, false};
    off.radial = 8;
    CHECK(Sculptor::copies(off).size() == 1);
    CHECK(Sculptor::copies(radial(1, 1)).size() == 1);
    // N copies; mirror + radial = 2N; the identity first; every copy a rotation or reflection.
    for (int n : {2, 3, 5, 8, 16, 64}) {
        const std::vector<SymCopy> r = Sculptor::copies(radial(n, 1));
        CHECK(int(r.size()) == n);
        CHECK(r[0].identity());
        for (const SymCopy& c : r) CHECK(orthonormal(c.m) && c.handedness() > 0.0f);
        const std::vector<SymCopy> mr = Sculptor::copies(radial(n, 1, true));
        CHECK(int(mr.size()) == 2 * n);
        int mirrored = 0;
        for (const SymCopy& c : mr) mirrored += c.handedness() < 0.0f;
        CHECK(mirrored == n);
    }
    // X and Z mirrors make a half turn about Y, which an even radial count already has: no duplicates.
    Symmetry xzr = radial(8, 1);
    xzr.x = xzr.z = true;
    CHECK(Sculptor::copies(xzr).size() == 16);
    Symmetry xzr3 = radial(3, 1);
    xzr3.x = xzr3.z = true;
    CHECK(Sculptor::copies(xzr3).size() == 12);
}

TEST(radial_transform_maths) {
    // N turns about each axis: the images of a point off the axis are N points evenly round it, at the same
    // height and distance, a turn of 2 pi / N apart; a point on the axis stays put.
    for (int axis = 0; axis < 3; ++axis)
        for (int n : {3, 4, 7, 12}) {
            glm::vec3 p(0.0f), up(0.0f);
            p[(axis + 1) % 3] = 1.0f;
            p[axis] = 0.25f;
            up[axis] = 1.0f;
            const std::vector<SymCopy> r = Sculptor::copies(radial(n, axis));
            std::vector<glm::vec3> q;
            for (const SymCopy& c : r) q.push_back(c.point(p));
            CHECK(int(images(radial(n, axis), p).size()) == n);
            for (const glm::vec3& v : q) {
                CHECK(std::abs(v[axis] - 0.25f) < 1e-5f);
                CHECK(std::abs(glm::length(v - up * 0.25f) - 1.0f) < 1e-5f);
            }
            // Copy k turns by k steps: the angle between copy 1's image and the original is one step.
            const float step = 2.0f * 3.14159265f / float(n);
            const glm::vec3 a = glm::normalize(p - up * 0.25f), b = glm::normalize(q[1] - up * 0.25f);
            CHECK(std::abs(std::acos(std::clamp(glm::dot(a, b), -1.0f, 1.0f)) - step) < 1e-4f);
            CHECK(near(r[1].point(up * 0.7f), up * 0.7f));
            // Directions turn with points; the inverse undoes a copy.
            for (const SymCopy& c : r) {
                CHECK(near(c.inversePoint(c.point(p)), p));
                CHECK(near(c.point(p) - c.point(glm::vec3(0.0f)), c.dir(p)));
            }
        }
    // Quarter turns are exact (duplicates compare equal; a 90 degree copy of X is exactly Z or -Z).
    const std::vector<SymCopy> four = Sculptor::copies(radial(4, 1));
    for (const SymCopy& c : four)
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j) CHECK(c.m[i][j] == std::round(c.m[i][j]));
    // Inverses: copy i then its inverse is the identity; a mirror is its own, a turn's is the opposite turn.
    Symmetry mr = radial(6, 2, true);
    const std::vector<SymCopy> all = Sculptor::copies(mr);
    const std::vector<size_t> inv = symmetryInverses(all);
    for (size_t i = 0; i < all.size(); ++i) {
        const glm::mat3 id = all[inv[i]].m * all[i].m;
        CHECK(near(id[0], glm::vec3(1, 0, 0)) && near(id[1], glm::vec3(0, 1, 0)) && near(id[2], glm::vec3(0, 0, 1)));
        if (all[i].handedness() < 0.0f) CHECK(inv[i] == i);
    }
    CHECK(inv[0] == 0);
    CHECK(inv[1] == 5); // a sixth of a turn undone by five sixths
}

TEST(radial_mirror_plus_radial) {
    // X mirror + 4 turns about Y: 8 images of a general point, all at its height and distance from the axis,
    // half of them mirror images (the point with x negated, turned).
    const glm::vec3 p(0.8f, 0.3f, 0.2f);
    const std::vector<glm::vec3> q = images(radial(4, 1, true), p);
    CHECK(q.size() == 8);
    const float r = std::hypot(p.x, p.z);
    for (const glm::vec3& v : q) CHECK(std::abs(v.y - p.y) < 1e-5f && std::abs(std::hypot(v.x, v.z) - r) < 1e-5f);
    CHECK(std::any_of(q.begin(), q.end(), [&](const glm::vec3& v) { return near(v, glm::vec3(-p.x, p.y, p.z)); }));
    // A point on the mirror plane: its mirror image is itself, so 4 distinct images.
    CHECK(images(radial(4, 1, true), glm::vec3(0.0f, 0.3f, 1.0f)).size() == 4);
    // Mirror and radial about the same axis: X mirror with turns about X.
    CHECK(images(radial(5, 0, true), glm::vec3(0.5f, 0.2f, 0.7f)).size() == 10);
}

TEST(radial_local_pivot) {
    // Local symmetry: the mirror plane and the radial axis go through the pivot, not the origin.
    Symmetry s{true, false, false};
    s.local = true;
    s.pivot = glm::vec3(5.0f, 1.0f, -2.0f);
    std::vector<SymCopy> c = Sculptor::copies(s);
    CHECK(c.size() == 2);
    CHECK(near(c[1].point(glm::vec3(6.0f, 3.0f, 4.0f)), glm::vec3(4.0f, 3.0f, 4.0f)));
    CHECK(near(c[1].dir(glm::vec3(1.0f, 0.0f, 0.0f)), glm::vec3(-1.0f, 0.0f, 0.0f)));
    // Without `local` the pivot is ignored.
    s.local = false;
    CHECK(near(Sculptor::copies(s)[1].point(glm::vec3(6.0f, 3.0f, 4.0f)), glm::vec3(-6.0f, 3.0f, 4.0f)));
    // Radial about the pivot: images circle it; the pivot itself stays.
    Symmetry r = radial(6, 1);
    r.local = true;
    r.pivot = glm::vec3(5.0f, 1.0f, -2.0f);
    for (const SymCopy& k : Sculptor::copies(r)) {
        CHECK(near(k.point(r.pivot), r.pivot));
        CHECK(std::abs(glm::distance(k.point(r.pivot + glm::vec3(2.0f, 0.5f, 0.0f)), r.pivot) -
                       glm::length(glm::vec3(2.0f, 0.5f, 0.0f))) < 1e-4f);
    }
    CHECK(!r.mirrorOnly());
    // The Part's centre: the middle of its bounding box.
    Mesh m = makeIcosphere(2, 1.0f);
    for (auto& p : m.positions) p += glm::vec3(3.0f, -1.0f, 0.5f);
    CHECK(near(symmetryPivot(m), glm::vec3(3.0f, -1.0f, 0.5f), 1e-3f));
}

TEST(radial_stroke_moves_every_copy) {
    // A Standard dab with 6 copies about Y moves the surface at each copy's centre, about equally; nothing
    // between them.
    Document d = makeDoc(makeIcosphere(5, 1.0f));
    const Mesh before = d.mesh;
    const Symmetry s = radial(6, 1);
    DabParams p;
    p.center = glm::normalize(glm::vec3(1.0f, 0.2f, 0.0f));
    p.viewDir = -p.center;
    p.radius = 0.2f;
    p.strength = 0.5f;
    CHECK(d.sculptor.beginStroke(d.mesh, "Standard"));
    d.sculptor.dab(d.mesh, d.bvh, BrushType::Standard, p, s);
    d.undo.push(d.sculptor.endStroke());
    float lo = 1e30f, hi = 0.0f;
    for (const SymCopy& c : Sculptor::copies(s)) {
        const float moved = maxMoved(before, d.mesh, c.point(p.center), p.radius);
        lo = std::min(lo, moved);
        hi = std::max(hi, moved);
        // Halfway to the next copy: untouched.
        const glm::vec3 between = Sculptor::copies(radial(12, 1))[1].point(c.point(p.center));
        CHECK(maxMoved(before, d.mesh, between, 0.05f) == 0.0f);
    }
    CHECK(lo > 0.0f);
    CHECK(hi < lo * 1.5f);
    // Undo gives back the stroke-start mesh.
    CHECK(d.undo.undo(d.mesh, d.bvh));
    float diff = 0.0f;
    for (size_t v = 0; v < before.vertexCount(); ++v) diff = std::max(diff, glm::distance(before.positions[v], d.mesh.positions[v]));
    CHECK(diff == 0.0f);
}

TEST(radial_mask_paint_and_grab) {
    // The mask brush, vertex paint and Move follow every copy too.
    Document d = makeDoc(makeIcosphere(4, 1.0f));
    const Symmetry s = radial(4, 1);
    DabParams p;
    p.center = glm::vec3(1.0f, 0.0f, 0.0f);
    p.viewDir = glm::vec3(-1.0f, 0.0f, 0.0f);
    p.radius = 0.25f;
    p.strength = 1.0f;
    CHECK(d.sculptor.beginStroke(d.mesh, "Mask"));
    d.sculptor.dabMask(d.mesh, d.bvh, p, s);
    d.undo.push(d.sculptor.endStroke());
    for (const SymCopy& c : Sculptor::copies(s)) {
        float m = 0.0f;
        for (size_t v = 0; v < d.mesh.vertexCount(); ++v)
            if (glm::distance(d.mesh.positions[v], c.point(p.center)) < 0.05f) m = std::max(m, d.mesh.maskAt(uint32_t(v)));
        CHECK(m > 0.5f);
    }
    d.mesh.clearMask();
    CHECK(d.sculptor.beginStroke(d.mesh, "Paint"));
    CHECK(paintDab(d.sculptor, d.mesh, d.bvh, p, glm::vec3(1.0f, 0.0f, 0.0f), s) > 0);
    d.undo.push(d.sculptor.endStroke());
    for (const SymCopy& c : Sculptor::copies(s)) {
        float red = 0.0f;
        for (size_t v = 0; v < d.mesh.vertexCount(); ++v)
            if (glm::distance(d.mesh.positions[v], c.point(p.center)) < 0.05f)
                red = std::max(red, d.mesh.colorAt(uint32_t(v)).x - d.mesh.colorAt(uint32_t(v)).y);
        CHECK(red > 0.5f);
    }
    // Move: each copy's region goes its own way (the drag turned with it).
    const Mesh before = d.mesh;
    CHECK(d.sculptor.beginStroke(d.mesh, "Move"));
    d.sculptor.beginGrab(d.mesh, d.bvh, p, s);
    d.sculptor.grab(d.mesh, d.bvh, glm::vec3(0.1f, 0.0f, 0.0f));
    d.undo.push(d.sculptor.endStroke());
    for (const SymCopy& c : Sculptor::copies(s)) {
        glm::vec3 moved(0.0f);
        for (size_t v = 0; v < d.mesh.vertexCount(); ++v)
            if (glm::distance(before.positions[v], c.point(p.center)) < 0.05f) moved += d.mesh.positions[v] - before.positions[v];
        CHECK(glm::dot(glm::normalize(moved), c.dir(glm::vec3(1.0f, 0.0f, 0.0f))) > 0.99f);
    }
}

TEST(radial_dynamic_topology_adds_detail_on_every_copy) {
    Document d = makeDoc(makeIcosphere(3, 1.0f));
    d.sculptor.dyntopo.enabled = true;
    d.sculptor.dyntopo.detail = 0.75f;
    const Symmetry s = radial(5, 1);
    const size_t before = d.mesh.triangleCount();
    auto trianglesNear = [&](const glm::vec3& c) {
        size_t n = 0;
        for (const glm::uvec3& t : d.mesh.triangles)
            n += glm::distance((d.mesh.positions[t.x] + d.mesh.positions[t.y] + d.mesh.positions[t.z]) / 3.0f, c) < 0.2f;
        return n;
    };
    const glm::vec3 at = glm::normalize(glm::vec3(1.0f, 0.3f, 0.2f));
    std::vector<size_t> start;
    for (const SymCopy& c : Sculptor::copies(s)) start.push_back(trianglesNear(c.point(at)));
    CHECK(d.sculptor.beginStroke(d.mesh, "Standard"));
    for (int i = 0; i < 6; ++i) {
        DabParams p;
        p.center = at;
        p.viewDir = -at;
        p.radius = 0.25f;
        p.strength = 0.3f;
        d.sculptor.dab(d.mesh, d.bvh, BrushType::Standard, p, s);
    }
    d.undo.push(d.sculptor.endStroke());
    CHECK(d.mesh.triangleCount() > before);
    size_t k = 0;
    for (const SymCopy& c : Sculptor::copies(s)) CHECK(trianglesNear(c.point(at)) > start[k++] * 2);
    // A rotationally symmetric mesh gets matching topology: a 16-sided cylinder (each side the same quads) with
    // 4 turns about its axis: every vertex's quarter-turn image is a vertex after the remesh, and every operation
    // found its images (the turns are not their own inverses, unlike mirrors).
    Document r = makeDoc(makeCylinder(16, 2.0f));
    r.sculptor.dyntopo.enabled = true;
    r.sculptor.dyntopo.detail = 0.8f;
    const Symmetry s4 = radial(4, 1);
    CHECK(r.sculptor.beginStroke(r.mesh, "Standard"));
    DabParams q;
    q.center = glm::vec3(1.0f, 0.1f, 0.15f);
    q.viewDir = glm::vec3(-1.0f, 0.0f, 0.0f);
    q.radius = 0.35f;
    q.strength = 0.3f;
    RayHit hit;
    if (r.bvh.raycast(r.mesh, glm::vec3(3.0f, 0.1f, 0.3f), glm::vec3(-1.0f, 0.0f, 0.0f), hit)) q.center = hit.position;
    const size_t cylBefore = r.mesh.triangleCount();
    for (int i = 0; i < 3; ++i) r.sculptor.dab(r.mesh, r.bvh, BrushType::Standard, q, s4);
    r.undo.push(r.sculptor.endStroke());
    CHECK(r.mesh.triangleCount() > cylBefore);
    size_t unmatched = 0;
    const SymCopy quarter = Sculptor::copies(s4)[1];
    for (const glm::vec3& v : r.mesh.positions) {
        const glm::vec3 w = quarter.point(v);
        bool found = false;
        for (const glm::vec3& u : r.mesh.positions) found = found || glm::distance(u, w) < 1e-3f;
        unmatched += !found;
    }
    CHECK(unmatched == 0);
    CHECK(r.sculptor.dynamicTopology().stats.unpaired == 0);
}

TEST(radial_commands) {
    // Python / CLI parity: stroke, mask_rect, hide_rect and insert_primitive take radial, radial_axis and local.
    for (const char* name : {"stroke", "vdm_stamp", "mask_rect", "mask_lasso", "hide_rect", "hide_lasso", "insert_primitive", "insert_mesh"}) {
        const Command* c = registry().find(name);
        CHECK(c != nullptr);
        if (!c) continue;
        int found = 0;
        for (const ParamSpec& p : c->params) found += p.name == "radial" || p.name == "radial_axis" || p.name == "local";
        CHECK(found == 3);
    }
    Document d;
    CHECK(registry().execute(d, "new_sphere", {{"subdivisions", 4.0}}).ok);
    const Mesh start = d.mesh;
    CHECK(registry().execute(d, "stroke", {{"points", std::string("0.9,0.2,0.3 0.9,0.25,0.3")}, {"view", std::string("-1,0,0")},
                                          {"radius", 0.15}, {"radial", 6.0}, {"radial_axis", std::string("y")}}).ok);
    // The stroke landed on the +X side and was copied round Y: something moved on the -X side too.
    float movedFar = 0.0f;
    for (size_t v = 0; v < start.vertexCount(); ++v)
        if (start.positions[v].x < -0.5f) movedFar = std::max(movedFar, glm::distance(start.positions[v], d.mesh.positions[v]));
    CHECK(movedFar > 0.0f);
    // Bad values are errors.
    CHECK(!registry().execute(d, "mask_rect", {{"radial", 1.5}}).ok);
    CHECK(!registry().execute(d, "mask_rect", {{"radial", 65.0}}).ok);
    CHECK(!registry().execute(d, "mask_rect", {{"radial", 4.0}, {"radial_axis", std::string("w")}}).ok);
    // mask_rect: a small rectangle at the right side of the front view masks 4 regions round Y.
    const Args rect = {{"x0", 0.55}, {"y0", -0.1}, {"x1", 0.75}, {"y1", 0.1}};
    CHECK(registry().execute(d, "mask_rect", rect).ok);
    size_t one = 0;
    for (size_t v = 0; v < d.mesh.vertexCount(); ++v) one += d.mesh.maskAt(uint32_t(v)) > 0.5f;
    CHECK(registry().execute(d, "mask_clear").ok);
    Args rr = rect;
    rr["radial"] = 4.0;
    CHECK(registry().execute(d, "mask_rect", rr).ok);
    size_t four = 0;
    for (size_t v = 0; v < d.mesh.vertexCount(); ++v) four += d.mesh.maskAt(uint32_t(v)) > 0.5f;
    CHECK(one > 0 && four > one * 2);
    CHECK(registry().execute(d, "mask_clear").ok);
    // hide_rect keeps the rectangle and its copies.
    Args hr = rect;
    hr["radial"] = 3.0;
    CHECK(registry().execute(d, "hide_rect", hr).ok);
    CHECK(d.mesh.hasHidden());
    CHECK(registry().execute(d, "show_all").ok);
    // insert_primitive: one cone per copy, one undo step.
    const size_t tris = d.mesh.triangleCount();
    const Args cone = {{"type", std::string("cone")}, {"x", 1.0}, {"y", 0.0}, {"z", 0.0}, {"nx", 1.0}, {"ny", 0.0}, {"nz", 0.0},
                       {"size", 0.2}, {"radial", 5.0}};
    const CommandResult r = registry().execute(d, "insert_primitive", cone);
    CHECK(r.ok);
    CHECK(r.message.find("5 cones (radial)") != std::string::npos);
    const size_t five = d.mesh.triangleCount() - tris;
    CHECK(registry().execute(d, "undo").ok);
    Args single = cone;
    single.erase("radial");
    CHECK(registry().execute(d, "insert_primitive", single).ok);
    CHECK(five == (d.mesh.triangleCount() - tris) * 5);
    CHECK(registry().execute(d, "undo").ok);
    // local: about the Part's own centre. Move the sphere off the origin; a local X mirror lands on the sphere.
    CHECK(registry().execute(d, "transform", {{"translate", std::string("3,0,0")}, {"all", true}}).ok);
    const Mesh moved = d.mesh;
    CHECK(registry().execute(d, "stroke", {{"points", std::string("3.9,0.2,0.3 3.9,0.25,0.3")}, {"view", std::string("-1,0,0")},
                                          {"radius", 0.15}, {"symmetry", std::string("x")}, {"local", true}}).ok);
    float movedMirror = 0.0f;
    for (size_t v = 0; v < moved.vertexCount(); ++v)
        if (moved.positions[v].x < 2.5f) movedMirror = std::max(movedMirror, glm::distance(moved.positions[v], d.mesh.positions[v]));
    CHECK(movedMirror > 0.0f);
}

TEST(radial_sixteen_copies_on_a_dense_mesh_stays_interactive) {
    // N = 16 copies on a dense sphere: the time per dab (all 16 copies) stays interactive. CI uses a 1.3M-triangle
    // sphere; SF_RADIAL_BENCH=1 measures the 20M one (and X only, 16 turns, 16 turns + X) for the record.
    const bool full = std::getenv("SF_RADIAL_BENCH") != nullptr;
    Document d;
    CHECK(registry().execute(d, "new_sphere", {{"triangles", full ? 20000000.0 : 1300000.0}}).ok);
    auto measure = [&](const Symmetry& s, float radius) {
        CHECK(d.sculptor.beginStroke(d.mesh, "Standard"));
        const int dabs = 30;
        double total = 0.0;
        for (int i = 0; i < dabs; ++i) {
            DabParams p;
            p.center = glm::normalize(glm::vec3(1.0f, 0.02f * float(i), 0.1f));
            p.viewDir = -p.center;
            p.radius = radius;
            p.strength = 0.4f;
            const auto t0 = std::chrono::steady_clock::now();
            d.sculptor.dab(d.mesh, d.bvh, BrushType::Standard, p, s);
            if (i > 0) total += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        }
        d.undo.push(d.sculptor.endStroke());
        if (full) {
            const SculptTimings& t = d.sculptor.timings;
            std::printf("    (totals: kernel %.0f ms, normals %.0f ms, bvh %.0f ms, %llu vertices)\n", t.kernelMs, t.normalsMs,
                        t.bvhMs, (unsigned long long)t.vertices);
            d.sculptor.timings = SculptTimings{};
        }
        return total / (dabs - 1);
    };
    const double ms = measure(radial(16, 1), 0.08f);
    std::printf("  radial 16 on %zu triangles, radius 0.08: %.2f ms per dab (all copies)\n", d.mesh.triangleCount(), ms);
    CHECK(ms < 250.0); // (a loaded CI machine; typically a few ms)
    if (full) {
        for (float radius : {0.08f, 0.2f}) {
            const double x = measure(Symmetry{true, false, false}, radius);
            const double r16 = measure(radial(16, 1), radius);
            const double rx = measure(radial(16, 1, true), radius);
            std::printf("  radius %.2f: X only %.2f ms, radial 16 %.2f ms, radial 16 + X (32 copies) %.2f ms per dab\n",
                        double(radius), x, r16, rx);
        }
    }
}
