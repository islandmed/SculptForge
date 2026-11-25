// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// A deep, narrow slot cut round a sphere: a brush at its edge reaches across the gap. What the far wall (the surface
// facing away from the one under the cursor) does, as a share of what the near side does.
#include "TestHarness.h"

#include "GeomInvariants.h"
#include "core/Parts.h"

#include <chrono>
#include <cstdio>

using namespace sf;

namespace {

// A unit sphere with a slot of width `gap` and depth `depth` round its equator (y = 0): the band |y| < band of the
// sphere becomes the slot's two walls and its round floor, the rest is the sphere squeezed toward the poles.
Mesh slotSphere(int level, float gap = 0.04f, float depth = 0.35f, float band = 0.25f) {
    Mesh m = makeIcosphere(level);
    const float half = gap * 0.5f, rim = std::sqrt(1.0f - half * half), floorShare = 0.15f;
    for (glm::vec3& p : m.positions) {
        const float s = p.y < 0.0f ? -1.0f : 1.0f, ay = std::abs(p.y);
        glm::vec2 h(p.x, p.z);
        h = glm::length(h) > 1e-6f ? glm::normalize(h) : glm::vec2(1.0f, 0.0f);
        float y, rho;
        if (ay >= band) {
            y = s * (half + (ay - band) * (1.0f - half) / (1.0f - band));
            rho = std::sqrt(std::max(0.0f, 1.0f - y * y));
        } else {
            const float t = ay / band;
            if (t >= floorShare) { // the wall: straight down from the rim
                y = s * half;
                rho = rim - depth * (1.0f - t) / (1.0f - floorShare);
            } else { // the floor: a half circle
                const float a = t / floorShare * 1.5707963f;
                y = s * half * std::sin(a);
                rho = rim - depth - half * std::cos(a);
            }
        }
        p = glm::vec3(h.x * rho, y, h.y * rho);
    }
    m.recomputeAllNormals();
    return m;
}

// A unit sphere (base level 5, micro level 4) with the slot carved in the micro level alone: grid points within
// gap / 2 of y = 0 sink by `depth`, so the slot's walls lie inside base triangles that span both of them.
Mesh carvedMicroSlot(float gap = 0.04f, float depth = 0.12f) {
    Mesh m = makeIcosphere(5);
    std::string err;
    CHECK(microDivide(m, 4, err, 0));
    const MicroMesh& mm = *m.micro;
    const uint32_t n = mm.n, S = mm.samples;
    std::vector<int16_t> disp(size_t(mm.baseTriangles) * S, 0);
    const float step = depth / 8000.0f; // (headroom: a dab that widens the range is not kept for Forge Ink)
    for (uint32_t t = 0; t < mm.baseTriangles; ++t)
        for (uint32_t j = 0; j <= n; ++j)
            for (uint32_t i = 0; i + j <= n; ++i)
                if (std::abs(microPoint(m, t, i, j).y) < 0.5f * gap) disp[size_t(t) * S + MicroMesh::index(i, j, n)] = -8000;
    microDelete(m);
    CHECK(microAttach(m, 4, step, MicroStore(std::move(disp)), {}, err));
    return m;
}

struct Spill {
    float nearMax = 0.0f, farMax = 0.0f;
    float shear = 0.0f; // plain meshes: the largest umbrella of the displacement within the brush, in mean edges (a lip torn or streaked)
    float share() const { return nearMax > 0.0f ? farMax / nearMax : 0.0f; }
};

// One stroke along the slot's upper wall just inside the rim, seen from the front and a little above (the view the
// report shows), radius 0.2 (five gaps): how far the far (lower) wall moves against the surface under the cursor.
// `rim`: the cursor on the outer surface just above the slot (the report's view) instead of on its upper wall.
// `repick`: each dab's centre and pick normal are the ray hit on the surface as the stroke has left it (a cursor follows the
// surface a cut lowers; held where the surface was, it floats above it and may be nearer the far side of the gap).
Spill slotStroke(const Mesh& start, BrushType type, bool micro, const std::function<void(DabParams&)>& tweak = {}, bool rim = false,
                 bool repick = false) {
    geom::Fixture f;
    f.mesh = start;
    if (micro && !start.micro) {
        std::string err;
        CHECK(microDivide(f.mesh, 2, err, 0));
    }
    f.bvh.build(f.mesh);
    Document d = geom::docFrom(f);
    const std::vector<glm::vec3> before = d.mesh.positions;
    const float radius = 0.2f;
    const glm::vec3 view = glm::normalize(glm::vec3(0.0f, -0.35f, -1.0f));
    const float y0 = rim ? 0.035f : 0.02f;
    const float rho = rim ? std::sqrt(1.0f - y0 * y0) : std::sqrt(1.0f - 0.02f * 0.02f) - 0.03f;
    const glm::vec3 c0(0.0f, y0, rho);
    std::vector<glm::vec3> centers;
    geom::MicroSnapshot mb;
    if (micro) mb = geom::microSnapshot(d.mesh, glm::vec3(0, 0, 1), 0.5f);
    Symmetry none{false, false, false};
    CHECK(d.sculptor.beginStroke(d.mesh, brushName(type)));
    if (type == BrushType::Move) {
        DabParams p;
        p.center = c0, p.viewDir = view, p.radius = radius, p.strength = 0.5f;
        if (tweak) tweak(p);
        d.sculptor.beginGrab(d.mesh, d.bvh, p, none);
        d.sculptor.grab(d.mesh, d.bvh, glm::vec3(0.0f, -0.05f, 0.0f));
    } else {
        for (int i = 0; i < 12; ++i) {
            const float a = (float(i) - 5.5f) * 0.02f;
            DabParams p;
            p.center = glm::vec3(std::sin(a) * rho, y0, std::cos(a) * rho);
            p.viewDir = view, p.radius = radius, p.strength = 0.5f;
            p.pickNormal = rim ? glm::normalize(p.center) : glm::vec3(0, -1, 0);
            if (repick) {
                RayHit hit;
                const glm::vec3 n = p.pickNormal, o = p.center + n * (rim ? 0.15f : 0.015f);
                if (d.bvh.raycast(d.mesh, o, -n, hit)) p.center = hit.position, p.pickNormal = hit.normal;
            }
            centers.push_back(p.center);
            if (tweak) tweak(p);
            d.sculptor.dab(d.mesh, d.bvh, type, p, none);
        }
    }
    d.undo.push(d.sculptor.endStroke());
    Spill s;
    // The far wall: the slot's lower side inside it (its wall and the half of its floor below its middle, 1% of the sphere
    // in from the outer surface); near: the slot's upper side, all of it.
    const auto side = [&](const glm::vec3& x) -> float* {
        return x.y < -0.005f && glm::length(x) < 0.985f ? &s.farMax : x.y > 0.005f ? &s.nearMax : nullptr;
    };
    if (!micro) {
        for (uint32_t v = 0; v < d.mesh.vertexCount(); ++v) {
            if (glm::distance(before[v], c0) > radius * 1.5f) continue;
            if (float* slot = side(before[v])) *slot = std::max(*slot, glm::distance(before[v], d.mesh.positions[v]));
        }
        if (!centers.empty()) {
            const geom::EdgeReport e = geom::edgeOutliers(d.mesh, before, centers, radius, geom::meanEdge(start));
            s.shear = std::max(e.worst, e.interior);
        }
        return s;
    }
    const geom::MicroSnapshot ma = geom::microSnapshot(d.mesh, glm::vec3(0, 0, 1), 0.5f);
    for (size_t i = 0; i < mb.points.size(); ++i) {
        if (glm::distance(mb.points[i], c0) > radius * 1.5f) continue;
        if (float* slot = side(mb.points[i])) {
            const float mv = glm::distance(mb.points[i], ma.points[i]);
            *slot = std::max(*slot, mv);
        }
    }
    return s;
}

} // namespace

// What the slot does without the option (printed), and that Move still reaches through with it on.
TEST(surface_falloff_slot_repro) {
    const Mesh plain = slotSphere(7);
    const Mesh base = slotSphere(6);
    const Mesh carved = carvedMicroSlot();
    for (BrushType t : {BrushType::Standard, BrushType::Clay, BrushType::Smooth, BrushType::Inflate, BrushType::Move}) {
        const Spill a = slotStroke(plain, t, false);
        const Spill b = slotStroke(base, t, true);
        const Spill c = slotStroke(carved, t, true);
        std::printf("slot %-9s plain near %.4f far %.4f share %3.0f%% | micro near %.4f far %.4f share %3.0f%% | carved micro near %.4f "
                    "far %.4f share %3.0f%%\n",
                    brushName(t), a.nearMax, a.farMax, 100.0f * a.share(), b.nearMax, b.farMax, 100.0f * b.share(), c.nearMax, c.farMax,
                    100.0f * c.share());
    }
    const Spill move = slotStroke(plain, BrushType::Move, false, [](DabParams& p) { p.contactFacing = true; });
    CHECK(move.share() > 0.5f);
}

// Contact facing on: a stroke at the slot's edge (the cursor on its upper wall, or on the outer surface just above it)
// moves the far wall less than 5% of the near side, with no more shear at the lip (the displacement's umbrella) than
// the stroke without the option. On a plain mesh and a micro level over a base with the slot. A slot carved into the
// micro detail alone, shallower than the brush, is within reach along the surface: printed, not held (the guide says so).
TEST(surface_falloff_contact_facing_keeps_the_far_wall) {
    const Mesh plain = slotSphere(7);
    const Mesh base = slotSphere(6);
    const Mesh carved = carvedMicroSlot();
    const auto on = [](DabParams& p) { p.contactFacing = true; };
    for (bool rim : {false, true})
        for (BrushType t : {BrushType::Standard, BrushType::Clay, BrushType::Smooth, BrushType::Inflate}) {
            const Spill off = slotStroke(plain, t, false, {}, rim);
            for (int k = 0; k < 3; ++k) {
                const Spill a = slotStroke(k == 0 ? plain : k == 1 ? base : carved, t, k > 0, on, rim);
                std::printf("contact facing %s %-9s %s near %.4f far %.4f share %.1f%% shear %.2f (off %.2f)\n", rim ? "rim " : "wall",
                            brushName(t), k == 0 ? "plain" : k == 1 ? "micro" : "carved micro", a.nearMax, a.farMax, 100.0f * a.share(),
                            a.shear, off.shear);
                if (k == 2) continue;
                CHECK(a.nearMax > 0.0f || (k == 1 && t == BrushType::Smooth)); // (a micro level starts smooth: nothing to smooth)
                CHECK(a.share() < 0.05f);
                if (k == 0) CHECK(a.shear <= std::max(1.2f * off.shear, off.shear + 0.15f));
            }
        }
}

// Contact facing cuts by the depth of the fold: a slot shallower than the brush (an eye's lids round the eyeball, a mouth)
// moves as without the option, so Clay over it closes it as it always did, with no border where a cut would end.
TEST(surface_falloff_contact_facing_shallow_fold_moves_as_off) {
    const Mesh shallow = slotSphere(7, 0.04f, 0.06f);
    for (BrushType t : {BrushType::Clay, BrushType::Standard}) {
        for (bool rim : {false, true}) {
            geom::Fixture f;
            std::vector<glm::vec3> res[2], start;
            for (int on = 0; on < 2; ++on) {
                f.mesh = shallow;
                f.bvh.build(f.mesh);
                Document d = geom::docFrom(f);
                start = d.mesh.positions;
                const float y0 = rim ? 0.035f : 0.02f, rho = rim ? std::sqrt(1.0f - y0 * y0) : std::sqrt(1.0f - 0.0004f) - 0.03f;
                CHECK(d.sculptor.beginStroke(d.mesh, brushName(t)));
                for (int i = 0; i < 12; ++i) {
                    const float a = (float(i) - 5.5f) * 0.02f;
                    DabParams p;
                    p.center = glm::vec3(std::sin(a) * rho, y0, std::cos(a) * rho);
                    p.viewDir = glm::normalize(glm::vec3(0.0f, -0.35f, -1.0f)), p.radius = 0.2f, p.strength = 0.5f;
                    p.pickNormal = rim ? glm::normalize(p.center) : glm::vec3(0, -1, 0);
                    p.contactFacing = on == 1;
                    d.sculptor.dab(d.mesh, d.bvh, t, p, Symmetry{false, false, false});
                }
                d.undo.push(d.sculptor.endStroke());
                res[on] = d.mesh.positions;
            }
            float peak = 0.0f, worst = 0.0f;
            for (size_t v = 0; v < start.size(); ++v) {
                peak = std::max(peak, glm::distance(res[0][v], start[v]));
                worst = std::max(worst, glm::distance(res[0][v], res[1][v]));
            }
            std::printf("shallow slot %-9s %s peak %.5f on differs by %.5f\n", brushName(t), rim ? "rim " : "wall", peak, worst);
            CHECK(peak > 0.0f);
            CHECK(worst < 0.05f * peak);
        }
    }
}

// A separate piece within the brush (an eyeball modelled on its own, inside the lids): never round a fold, so its side
// facing as the lid does moves as without the option, or Clay over the eye lifts the lids off it.
TEST(surface_falloff_contact_facing_separate_piece_follows) {
    Mesh lid = makeIcosphere(6), eye = makeIcosphere(4);
    for (glm::vec3& q : eye.positions) q = q * 0.12f + glm::vec3(0.0f, 0.0f, 0.95f);
    eye.recomputeAllNormals();
    const uint32_t eyeFirst = lid.vertexCount();
    for (BrushType t : {BrushType::Clay, BrushType::Standard}) {
        std::vector<glm::vec3> res[2], start;
        for (int on = 0; on < 2; ++on) {
            geom::Fixture f;
            f.mesh = mergeMeshes({&lid, &eye});
            f.mesh.recomputeAllNormals();
            f.bvh.build(f.mesh);
            Document d = geom::docFrom(f);
            start = d.mesh.positions;
            CHECK(d.sculptor.beginStroke(d.mesh, brushName(t)));
            for (int i = 0; i < 12; ++i) {
                const float a = 0.1f + 0.2f * float(i) / 11.0f;
                DabParams p;
                p.center = glm::vec3(std::sin(a), 0.0f, std::cos(a));
                p.viewDir = glm::vec3(0, 0, -1), p.radius = 0.25f, p.strength = 0.5f, p.pickNormal = p.center;
                p.contactFacing = on == 1;
                d.sculptor.dab(d.mesh, d.bvh, t, p, Symmetry{false, false, false});
            }
            d.undo.push(d.sculptor.endStroke());
            res[on] = d.mesh.positions;
        }
        float peak = 0.0f, worst = 0.0f;
        for (uint32_t v = eyeFirst; v < res[0].size(); ++v) {
            if (glm::normalize(start[v] - glm::vec3(0.0f, 0.0f, 0.95f)).z < 0.9f) continue; // (the eyeball's front)
            peak = std::max(peak, glm::distance(res[0][v], start[v]));
            worst = std::max(worst, glm::distance(res[0][v], res[1][v]));
        }
        std::printf("separate piece %-9s eyeball front moves %.5f off, on differs by %.5f\n", brushName(t), peak, worst);
        CHECK(peak > 0.0f);
        // (it keeps up with the lid: a cut piece differs by all of its move; the option's exact normals, not the
        // stroke's deferred ones, account for the rest on so small a piece)
        CHECK(worst < 0.15f * peak);
    }
}

// Geometric invariants on the dense sphere (nothing turned away within the brush): Contact facing changes nothing an
// artist could see (under 1% of the stroke's move), and adds no spike or step at the falloff edge.
TEST(surface_falloff_contact_facing_dense_sphere_invariants) {
    const float edge = geom::meanEdge(geom::denseSphereFixture().mesh);
    for (BrushType t : {BrushType::Standard, BrushType::Clay, BrushType::Inflate}) {
        std::vector<glm::vec3> moved[2];
        geom::EdgeReport rep[2];
        float peak = 0.0f;
        for (int on = 0; on < 2; ++on) {
            Document d = geom::denseSphere();
            const std::vector<glm::vec3> before = d.mesh.positions;
            const auto strokes = geom::overlapStrokes(glm::vec3(0, 0, 1), 0.15f);
            CHECK(d.sculptor.beginStroke(d.mesh, brushName(t)));
            for (const auto& s : strokes)
                for (DabParams p : s) {
                    p.contactFacing = on == 1;
                    d.sculptor.dab(d.mesh, d.bvh, t, p, Symmetry{false, false, false});
                }
            d.undo.push(d.sculptor.endStroke());
            moved[on] = d.mesh.positions;
            rep[on] = geom::edgeOutliers(d.mesh, before, geom::centersOf(strokes), 0.15f, edge);
            if (on == 0) peak = geom::maxMove(before, d.mesh);
        }
        float diff = 0.0f;
        for (size_t v = 0; v < moved[0].size(); ++v) diff = std::max(diff, glm::distance(moved[0][v], moved[1][v]));
        std::printf("dense sphere %-9s peak %.5f diff %.6f spikes off %d on %d worst off %.3f on %.3f\n", brushName(t), peak, diff,
                    rep[0].spikes, rep[1].spikes, rep[0].worst, rep[1].worst);
        CHECK(diff < 0.01f * peak);
        CHECK(rep[1].spikes <= rep[0].spikes);
    }
}

// Forge Ink: a Contact facing stroke on a micro level keeps its hint with each dab, and replaying it (an edit with the
// same dabs) writes the same grid.
TEST(surface_falloff_contact_facing_micro_replays) {
    geom::Fixture f;
    f.mesh = carvedMicroSlot();
    f.bvh.build(f.mesh);
    Document d = geom::docFrom(f);
    d.sculptor.keepEditable = true;
    CHECK(d.sculptor.beginStroke(d.mesh, "Clay"));
    for (int i = 0; i < 6; ++i) {
        DabParams p;
        p.center = glm::vec3(0.02f * float(i) - 0.05f, 0.02f, 0.97f);
        p.viewDir = glm::vec3(0, 0, -1), p.radius = 0.2f, p.strength = 0.5f, p.pickNormal = glm::vec3(0, -1, 0);
        p.contactFacing = true;
        d.sculptor.dab(d.mesh, d.bvh, BrushType::Clay, p, Symmetry{false, false, false});
    }
    d.undo.push(d.sculptor.endStroke());
    const uint32_t id = d.sculptor.lastKeptStroke();
    CHECK(id != 0);
    MicroHistory* h = microHistory(d.mesh, false);
    CHECK(h != nullptr);
    if (!h || !id) return;
    std::vector<MicroDab> dabs = h->find(id)->dabs;
    CHECK(!dabs.empty() && glm::length(dabs[0].facingNormal) > 0.5f);
    const std::vector<int16_t> live = d.mesh.micro->disp.toVector();
    std::string why;
    CHECK(h->edit(d.mesh, d.bvh, id, dabs, nullptr, &why));
    CHECK(d.mesh.micro->disp.toVector() == live);
}

// The cost of a dab with Contact facing on against off (printed for the report; the bench gate holds the off path): a
// 1.3M-triangle sphere at two brush sizes, and a micro level.
TEST(surface_falloff_contact_facing_cost) {
    const auto perDab = [](Document& d, BrushType t, float radius, bool on) {
        CHECK(d.sculptor.beginStroke(d.mesh, brushName(t)));
        const auto start = std::chrono::steady_clock::now();
        const int dabs = 24;
        for (int i = 0; i < dabs; ++i) {
            const float a = 0.01f * float(i);
            DabParams p;
            p.center = glm::vec3(std::sin(a), 0.0f, std::cos(a));
            p.viewDir = glm::vec3(0, 0, -1), p.radius = radius, p.strength = 0.3f, p.contactFacing = on;
            d.sculptor.dab(d.mesh, d.bvh, t, p, Symmetry{false, false, false});
        }
        d.undo.push(d.sculptor.endStroke());
        return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count() / dabs;
    };
    for (BrushType t : {BrushType::Standard, BrushType::Clay})
        for (float radius : {0.1f, 0.4f, 0.8f}) { // (0.8: about a 240 px brush on a framed bust, a third of the sphere)
            double ms[2];
            for (int on = 0; on < 2; ++on) {
                Document d = geom::denseSphere();
                perDab(d, t, radius, on == 1); // (warm)
                Document e = geom::denseSphere();
                ms[on] = perDab(e, t, radius, on == 1);
            }
            std::printf("cost dense sphere %-9s r %.2f off %.3f ms on %.3f ms per dab\n", brushName(t), radius, ms[0], ms[1]);
        }
    geom::Fixture f;
    f.mesh = makeIcosphere(5);
    std::string err;
    CHECK(microDivide(f.mesh, 5, err, 0));
    f.bvh.build(f.mesh);
    for (BrushType t : {BrushType::Standard, BrushType::Clay})
        for (float radius : {0.05f, 0.2f}) {
            double ms[3]; // off, off on the grid's kernel (no base dabs: the path a dab with it on takes), on
            for (int k = 0; k < 3; ++k) {
                Document d = geom::docFrom(f);
                d.sculptor.microBaseDabs = k == 0;
                perDab(d, t, radius, k == 2);
                ms[k] = perDab(d, t, radius, k == 2);
            }
            std::printf("cost micro %-9s r %.2f off %.3f ms (grid %.3f ms) on %.3f ms per dab\n", brushName(t), radius, ms[0], ms[1], ms[2]);
        }
}

// ... and on a micro level (no fold anywhere): the grid moves as without the option (each point's share of the falloff
// is mixed from its base corners', exact there; a mix of the corners' distances cut whole triangles near the centre).
TEST(surface_falloff_contact_facing_micro_sphere_unchanged) {
    for (BrushType t : {BrushType::Standard, BrushType::Clay})
        for (float radius : {0.1f, 0.2f}) {
            std::vector<glm::vec3> res[2];
            geom::MicroSnapshot b0;
            for (int on = 0; on < 2; ++on) {
                geom::Fixture f;
                f.mesh = makeIcosphere(5);
                std::string err;
                CHECK(microDivide(f.mesh, 3, err, 0));
                f.bvh.build(f.mesh);
                Document d = geom::docFrom(f);
                d.sculptor.microBaseDabs = false; // (the grid's kernel both times)
                b0 = geom::microSnapshot(d.mesh, glm::vec3(0, 0, 1), 0.5f);
                CHECK(d.sculptor.beginStroke(d.mesh, brushName(t)));
                for (int i = 0; i < 12; ++i) {
                    const float a = (float(i) - 5.5f) * 0.03f;
                    DabParams p;
                    p.center = glm::vec3(std::sin(a), 0.0f, std::cos(a));
                    p.viewDir = glm::vec3(0, 0, -1), p.radius = radius, p.strength = 0.5f, p.pickNormal = p.center;
                    p.contactFacing = on == 1;
                    d.sculptor.dab(d.mesh, d.bvh, t, p, Symmetry{false, false, false});
                }
                d.undo.push(d.sculptor.endStroke());
                res[on] = geom::microSnapshot(d.mesh, glm::vec3(0, 0, 1), 0.5f).points;
            }
            float worst = 0.0f, peak = 0.0f;
            for (size_t i = 0; i < res[0].size(); ++i) {
                peak = std::max(peak, glm::distance(res[0][i], b0.points[i]));
                worst = std::max(worst, glm::distance(res[0][i], res[1][i]));
            }
            std::printf("micro sphere %-9s r %.2f peak %.4f diff %.5f\n", brushName(t), radius, peak, worst);
            CHECK(peak > 0.0f);
            CHECK(worst < 0.02f * peak);
        }
}

// Trim, Flatten and Clay along a slot's edge with Contact facing on: the far wall stays put, Trim as the others. A Trim stroke lowers
// the surface it cuts, and the flood that measures the way round started from the vertex nearest the cursor: held where the
// surface was (or a pick on a wall a gap's width away), that was a vertex of the far wall, which then took the cut (55% of the near
// side's). It now starts from the nearest vertex that faces the contact. The rim with Trim on a plain mesh is the fold rule's, not
// held (this slot is 1.75 radii deep, its far wall's rim within the partial weights; a slot past 3.1 radii keeps it): printed.
TEST(surface_falloff_contact_facing_far_wall_after_a_cut) {
    const Mesh plain = slotSphere(7);
    const Mesh base = slotSphere(6);
    const auto on = [](DabParams& p) { p.contactFacing = true; };
    for (BrushType t : {BrushType::Trim, BrushType::Flatten, BrushType::Clay})
        for (bool rim : {false, true})
            for (bool repick : {false, true})
                for (int k = 0; k < 2; ++k) {
                    const Spill a = slotStroke(k == 0 ? plain : base, t, k > 0, on, rim, repick);
                    std::printf("far wall after a cut %-7s %s %s %-5s near %.4f far %.4f share %.1f%%\n", brushName(t), rim ? "rim " : "wall",
                                repick ? "picked" : "held  ", k == 0 ? "plain" : "micro", a.nearMax, a.farMax, 100.0f * a.share());
                    CHECK(a.nearMax > 0.0f);
                    if (!(rim && t == BrushType::Trim && k == 0)) CHECK(a.share() < 0.05f);
                }
}
