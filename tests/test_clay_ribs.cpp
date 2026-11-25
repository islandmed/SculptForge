// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Clay passes along the lip of a squeezed band (a deep slot round a sphere): the slab they build must be as smooth along the
// stroke as the dabs are, with no ribs (the 0.13.12 report: 4 passes built vertical ribs; the cause: a wall that leans away from
// the area normal is pulled toward the dab, its falloff rises and the next dab pulls it further, a runaway at the reach's edge).
// And an ordinary Clay stroke on an even dense mesh is what it was.
#include "TestHarness.h"

#include "GeomInvariants.h"

#include <algorithm>
#include <cstdio>
#include <map>
#include <cstring>

using namespace sf;

namespace {

// A unit sphere with a slot of width `gap` and depth `depth` round its equator (y = 0), as tests/test_surface_falloff.cpp's.
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
            if (t >= floorShare) {
                y = s * half;
                rho = rim - depth * (1.0f - t) / (1.0f - floorShare);
            } else {
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

struct Ribs {
    float worst = 0.0f, med = 0.0f;
    float spread = 0.0f; // the largest spread of the displacement among wall vertices at one radius (it should not depend on the azimuth)
    float peak = 0.0f; // the largest displacement along the stroke
    double dabMs = 0.0; // the time in Sculptor::dab, summed
    int dabs = 0;
    float rib = 0.0f;  // the largest departure of the displacement from its running mean over `window` dab spacings (a rib at the dab spacing)
    
};

// `passes` Clay (or `type`) strokes along an arc of the slot sphere at height y0, dabs `spacing` radii apart. Measured on the
// vertices of the slot's upper wall and floor within the stroke's reach, binned by azimuth: the displacement as a function of
// the angle should be smooth; a rib is a bin that differs from its neighbours.
Ribs slotPasses(const Mesh& start, BrushType type, int passes, float radius, float spacing, float y0, float strength = 0.5f, bool startWeights = true, bool dump = false, float gapOf = 0.04f, uint32_t budget = 0) {
    geom::Fixture f;
    f.mesh = start;
    f.bvh.build(f.mesh);
    Document d = geom::docFrom(f);
    d.sculptor.clayStartWeights = startWeights;
    if (budget) d.sculptor.clayStartBudget = budget;
    const std::vector<glm::vec3> before = d.mesh.positions;
    std::vector<glm::vec3> centers;
    glm::vec3 lastm(0.0f);
    Ribs r;
    const glm::vec3 view = glm::normalize(glm::vec3(0.0f, -0.35f, -1.0f));
    const float rho = std::sqrt(1.0f - y0 * y0);
    const float span = 1.0f; // radians each side of +z
    const float da = spacing * radius / rho;
    Symmetry none{false, false, false};
    for (int pass = 0; pass < passes; ++pass) {
        CHECK(d.sculptor.beginStroke(d.mesh, brushName(type)));
        glm::vec3 prev(0.0f);
        for (float a = -span; a <= span; a += da) {
            DabParams p;
            p.center = glm::vec3(std::sin(a) * rho, y0, std::cos(a) * rho);
            p.viewDir = view, p.radius = radius, p.strength = strength;
            p.alphaUp = glm::vec3(0.0f, 1.0f, 0.0f); // (the screen's up, as the app's)
            p.strokeDelta = prev == glm::vec3(0.0f) ? glm::vec3(0.0f) : p.center - prev;
            prev = p.center;
            centers.push_back(p.center);
            const auto t0 = std::chrono::steady_clock::now();
            d.sculptor.dab(d.mesh, d.bvh, type, p, none);
            r.dabMs += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count(), ++r.dabs;
            if (dump) {
                static uint32_t tv = UINT32_MAX;
                if (tv == UINT32_MAX) {
                    float best = 1e9f;
                    for (uint32_t v = 0; v < d.mesh.vertexCount(); ++v)
                        if (glm::distance(before[v], glm::vec3(0.0f, 0.1f, 0.825f)) < best) best = glm::distance(before[v], glm::vec3(0.0f, 0.1f, 0.825f)), tv = v;
                }
                const glm::vec3 m = d.mesh.positions[tv] - before[tv];
                const auto& fr = d.sculptor.lastFrame;
                if (glm::length(m - lastm) > 0.003f)
                    std::printf("pass %d a %.3f move |%.3f| (%.3f %.3f %.3f) frame n (%.2f %.2f %.2f) ac-c %.3f\n", pass, a, glm::length(m), m.x, m.y, m.z, fr.normal.x, fr.normal.y, fr.normal.z, glm::dot(fr.areaCenter - p.center, fr.normal)), lastm = m;
            }
        }
        if (startWeights && !budget && type == BrushType::Clay && radius > 0.1f && gapOf > 0.1f) {
            const size_t kept = d.sculptor.pinchStateBytes(); // (a leaning wall: kept, and only for the vertices that are deep: a quarter of 16 bytes a vertex at most)
            CHECK(kept > 0);
            CHECK(kept < d.mesh.vertexCount() * 4);
        }
        d.undo.push(d.sculptor.endStroke());
        CHECK(d.sculptor.pinchStateBytes() == 0);
    }
    // Over what the strokes reached (within the radius of a dab): peak = the largest move, rib = the 99th percentile of the
    // displacement's second difference along the stroke, worst = its largest.
    const float edge = geom::meanEdge(start);
    std::vector<float> um;
    int shown = 0;
    for (uint32_t v = 0; v < d.mesh.vertexCount(); ++v) {
        const glm::vec3 mv = d.mesh.positions[v] - before[v];
        float dmin = 1e30f;
        for (const glm::vec3& c : centers) dmin = std::min(dmin, glm::distance(before[v], c));
        if (dmin > radius || std::abs(std::atan2(before[v].x, before[v].z)) > span - 0.35f) continue; // (not the stroke's ends: a cap is a gradient)
        r.peak = std::max(r.peak, glm::length(mv));
        // Along the stroke (the strokes run round the y axis): the second difference of the displacement across the two ring
        // neighbours nearest the azimuthal direction, in mean edges. A radial ramp (Clay saturating toward the plane) has none; a rib does.
        const glm::vec3 az = glm::normalize(glm::vec3(before[v].z, 0.0f, -before[v].x) + glm::vec3(1e-9f));
        int up = -1, dn = -1;
        float upDot = 0.8f, dnDot = 0.8f;
        for (uint32_t u : d.mesh.neighbors(v)) {
            const glm::vec3 e = glm::normalize(before[u] - before[v]);
            const float c = glm::dot(e, az);
            if (c > upDot) upDot = c, up = int(u);
            if (-c > dnDot) dnDot = -c, dn = int(u);
        }
        if (up < 0 || dn < 0) continue;
        const glm::vec3 d2 = (d.mesh.positions[size_t(up)] - before[size_t(up)]) + (d.mesh.positions[size_t(dn)] - before[size_t(dn)]) - 2.0f * mv;
        um.push_back(glm::length(d2) / edge);
        if (dump && um.back() > 3.0f && ++shown <= 8) std::printf("  v%u before (%.3f %.3f %.3f) |move| %.3f second diff %.1f\n", v, before[v].x, before[v].y, before[v].z, glm::length(mv), um.back());
    }
    {
        // The strokes are the same at every azimuth inside their span: on the slot's flat upper wall the displacement is a function of the
        // distance from the axis alone, so vertices at one radius (bins of 0.0005) must move alike; a spread is a rib at the vertex pitch.
        const float half = 0.5f * gapOf;
        std::map<int, std::pair<float, float>> bins; // (min, max) of the move
        std::map<int, int> count;
        for (uint32_t v = 0; v < d.mesh.vertexCount(); ++v) {
            const glm::vec3& b = before[v];
            if (std::abs(b.y - half) > 1e-4f || std::abs(std::atan2(b.x, b.z)) > 0.45f) continue;
            const float len = glm::length(d.mesh.positions[v] - b), rr = glm::length(glm::vec2(b.x, b.z));
            const int k = int(rr / 0.0005f);
            auto it = bins.find(k);
            if (it == bins.end()) bins[k] = {len, len}; else it->second = {std::min(it->second.first, len), std::max(it->second.second, len)};
            ++count[k];
        }
        for (auto& [k, mm] : bins)
            if (count[k] >= 3) r.spread = std::max(r.spread, mm.second - mm.first);
    }
    if (!um.empty()) {
        std::sort(um.begin(), um.end());
        r.rib = um[size_t(float(um.size() - 1) * 0.99f)];
        r.worst = um.back();
        r.med = um[um.size() / 2];
    }
    return r;
}

} // namespace

// Round the slot, 4 Clay passes at two dab spacings: the displacement of the wall's vertices at one radius from the axis (the strokes
// are alike at every azimuth) may differ by a few percent of the peak, as Standard's does, not by the half it did (vertices
// pulled to the plane next to vertices left behind). Clay Block is printed, not held: its flat top snaps a vertex to the plane in
// one dab, a separate matter (the dab-edge folds on dense levels).
TEST(clay_ribs_slot) {
    for (float gap : {0.04f, 0.2f}) {
        const Mesh slot = slotSphere(7, gap);
        for (BrushType t : {BrushType::Clay, BrushType::ClayBlock, BrushType::Standard}) {
            for (float y0 : {0.035f, 0.02f}) {
                for (float sp : {0.1f, 0.25f}) {
                    const Ribs was = slotPasses(slot, t, 4, 0.2f, sp, y0, 0.5f, false, false, gap);
                    const Ribs now = slotPasses(slot, t, 4, 0.2f, sp, y0, 0.5f, true, false, gap);
                    std::printf("ribs gap %.2f %-9s y0 %.3f spacing %.2f: spread %.1f%% of peak was, %.1f%% now (peak %.3f)\n", gap, brushName(t), y0, sp,
                                100.0f * was.spread / was.peak, 100.0f * now.spread / now.peak, now.peak);
                    if (t == BrushType::Clay) CHECK(now.spread <= 0.12f * now.peak);
                    if (t == BrushType::Clay && gap > 0.1f && sp < 0.2f) CHECK(was.spread > 0.25f * was.peak); // (the test sees the runaway without the fix)
                }
            }
        }
    }
}

// A stroke that spends its budget of kept start positions (a rough mesh, a big brush) starts afresh, and with none where one dab alone is
// over it: every deep vertex of a dab is weighed alike, never a mix of kept and not. A budget under what a dab finds deep leaves the
// runaway; one a stroke keeps spending cuts it only in part (the history is lost at each restart).
TEST(clay_ribs_budget) {
    const Mesh slot = slotSphere(7, 0.2f);
    const Ribs off = slotPasses(slot, BrushType::Clay, 4, 0.2f, 0.1f, 0.035f, 0.5f, false, false, 0.2f);
    const Ribs none = slotPasses(slot, BrushType::Clay, 4, 0.2f, 0.1f, 0.035f, 0.5f, true, false, 0.2f, 40);
    const Ribs small = slotPasses(slot, BrushType::Clay, 4, 0.2f, 0.1f, 0.035f, 0.5f, true, false, 0.2f, 700);
    const Ribs full = slotPasses(slot, BrushType::Clay, 4, 0.2f, 0.1f, 0.035f, 0.5f, true, false, 0.2f);
    std::printf("budget: spread %.1f%% of peak with the weights off, %.1f%% with a budget of 40, %.1f%% of 700, %.1f%% of the default\n", 100.0f * off.spread / off.peak,
                100.0f * none.spread / none.peak, 100.0f * small.spread / small.peak, 100.0f * full.spread / full.peak);
    CHECK(none.spread > 0.25f * none.peak); // (a budget under what a dab finds deep: the runaway stays)
    CHECK(small.spread < off.spread);        // (a budget a stroke keeps spending: history is lost at each restart, so a part of the cure)
}

// The time a dab takes on the slot (printed, not held: a timing in a test run is noisy): without and with the start weights, the
// best of five strokes of 4 passes.
TEST(clay_ribs_dab_cost) {
    for (float gap : {0.04f, 0.2f}) {
        const Mesh slot = slotSphere(7, gap);
        double best[2] = {1e9, 1e9};
        for (int rep = 0; rep < 5; ++rep)
            for (int on = 0; on < 2; ++on) {
                const Ribs r = slotPasses(slot, BrushType::Clay, 4, 0.2f, 0.1f, 0.035f, 0.5f, on != 0, false, gap);
                best[on] = std::min(best[on], r.dabMs / r.dabs);
            }
        std::printf("dab cost gap %.2f Clay: %.3f ms without the start weights, %.3f ms with (%+.1f%%)\n", gap, best[0], best[1], 100.0 * (best[1] / best[0] - 1.0));
    }
}

// An even dense sphere: where Clay and Clay Block pull along the surface the weights are what they were (within 1% of the peak move).
TEST(clay_ribs_even_mesh_unchanged) {
    const geom::Fixture& fx = geom::denseSphereFixture();
    for (BrushType t : {BrushType::Clay, BrushType::ClayBlock}) {
        std::vector<glm::vec3> moved[2];
        for (int on = 0; on < 2; ++on) {
            Document d = geom::docFrom(fx);
            d.sculptor.clayStartWeights = on != 0;
            const std::vector<glm::vec3> before = d.mesh.positions;
            Symmetry none{false, false, false};
            CHECK(d.sculptor.beginStroke(d.mesh, brushName(t)));
            for (int i = 0; i < 16; ++i) {
                const float a = (float(i) - 7.5f) * 0.05f;
                DabParams p;
                p.center = glm::vec3(std::sin(a), 0.0f, std::cos(a));
                p.viewDir = glm::vec3(0.0f, 0.0f, -1.0f), p.radius = 0.2f, p.strength = 0.5f;
                d.sculptor.dab(d.mesh, d.bvh, t, p, none);
            }
            CHECK(d.sculptor.pinchStateBytes() == 0); // (an ordinary stroke keeps no start positions: no 16 bytes a vertex)
            d.undo.push(d.sculptor.endStroke());
            for (size_t v = 0; v < before.size(); ++v) moved[on].push_back(d.mesh.positions[v] - before[v]);
        }
        float peak = 0.0f, diff = 0.0f;
        for (size_t v = 0; v < moved[0].size(); ++v) {
            peak = std::max(peak, glm::length(moved[0][v]));
            diff = std::max(diff, glm::distance(moved[0][v], moved[1][v]));
        }
        std::printf("even sphere %-9s peak move %.4f, largest difference %.5f (%.2f%% of peak)\n", brushName(t), peak, diff, 100.0f * diff / peak);
        CHECK(peak > 0.0f);
        CHECK(diff <= 0.01f * peak);
    }
}
