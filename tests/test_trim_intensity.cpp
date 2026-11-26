// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Trim's Intensity: the gain a dab cuts with is proportional to Intensity below 50 (1 is a light touch: 0.02, 25 half
// of 50's) and what it always was at 50 and above (0.5 + Intensity: the default and anyone who raised it feel no change).
// Measured on the dense sphere (a stroke of 11 dabs up the front, radius 0.2) and on a micro level; the kernel's own
// numbers in PlaneKernel (the plan the brush ghost and the viewport preview share).
#include "TestHarness.h"

#include "GeomInvariants.h"
#include "core/MicroTiles.h"
#include "core/SculptKernel.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

using namespace sf;

namespace {

constexpr float kRadius = 0.2f;
const float kIntensities[] = {1, 5, 10, 25, 35, 50, 75, 100};

// Trim's gain as it was through v0.14.0 (the floor 0.5 at any Intensity).
float oldGain(float s) { return 0.5f + s; }

PlaneKernel trimKernel(float s) {
    PlaneKernel k;
    k.type = BrushType::Trim;
    k.center = glm::vec3(0.0f);
    k.areaCenter = glm::vec3(0.0f);
    k.areaNormal = glm::vec3(0, 0, 1);
    k.r = kRadius, k.s = s, k.sign = 1.0f;
    return k;
}

// The dabs of the stroke on the sphere: 0.03 rad apart up the front, centre on the surface, straight on, the brush's
// own normal the surface's there.
std::vector<DabParams> frontStroke(float intensity, float pressure = 1.0f, int count = 11, float radius = kRadius, float step = 0.03f) {
    std::vector<DabParams> dabs = geom::arc(glm::vec3(0, 0, 1), glm::vec3(0, 1, 0), count, step, radius, intensity / 100.0f, pressure, pressure);
    for (DabParams& p : dabs) p.pickNormal = p.center;
    return dabs;
}

struct Cut {
    float depth = 0.0f;    // the deepest cut, as a share of the brush radius (%)
    size_t moved = 0;      // vertices (or micro points) that moved
    float rise = 0.0f;     // the largest rise of any vertex above where it was (along the radius)
    float asymmetry = 0.0f; // X mirror: the largest distance between a vertex and its mirror's image
    geom::EdgeReport edge;
    uint64_t hash = 0;     // the end positions' bits (for the same-build before/after of the report)
};

Cut sphereStroke(float intensity, bool mirrorX = false, float pressure = 1.0f, float xShift = 0.0f) {
    Document d = geom::denseSphere();
    const std::vector<glm::vec3> before = d.mesh.positions;
    std::vector<DabParams> dabs = frontStroke(intensity, pressure);
    if (xShift != 0.0f)
        for (DabParams& p : dabs) {
            p.center = glm::normalize(p.center + glm::vec3(xShift, 0, 0));
            p.pickNormal = p.center, p.viewDir = -p.center;
        }
    std::vector<glm::vec3> centers;
    CHECK(d.sculptor.beginStroke(d.mesh, "Trim"));
    for (const DabParams& p : dabs) {
        centers.push_back(p.center);
        d.sculptor.dab(d.mesh, d.bvh, BrushType::Trim, p, Symmetry{mirrorX, false, false});
    }
    d.undo.push(d.sculptor.endStroke());
    Cut c;
    uint64_t h = 1469598103934665603ull;
    for (uint32_t v = 0; v < d.mesh.vertexCount(); ++v) {
        const float moved = glm::distance(before[v], d.mesh.positions[v]);
        c.depth = std::max(c.depth, moved / kRadius * 100.0f);
        c.moved += moved > 0.0f;
        c.rise = std::max(c.rise, glm::dot(d.mesh.positions[v] - before[v], glm::normalize(before[v])));
        uint32_t bits[3];
        std::memcpy(bits, &d.mesh.positions[v], sizeof bits);
        for (uint32_t b : bits) h = (h ^ b) * 1099511628211ull;
    }
    c.hash = h;
    if (mirrorX) {
        static const std::vector<uint32_t> map = geom::mirrorMapX(geom::denseSphereFixture().mesh);
        c.asymmetry = geom::symmetryError(d.mesh, map);
    }
    for (DabParams& p : dabs) centers.push_back(p.center);
    c.edge = geom::edgeOutliers(d.mesh, before, centers, kRadius, geom::meanEdge(geom::denseSphereFixture().mesh));
    return c;
}

} // namespace

// The kernel: Intensity 1 cuts a fiftieth of what a dab at the plane's height would at full gain; 25 half of 50's; 50 and above
// the old gain, to the last bit (the default and what anyone who raised it use).
TEST(trim_intensity_kernel_gain_proportional_below_50_and_unchanged_at_50_and_above) {
    const float h = 0.05f;
    for (float f : {1.0f, 0.5625f, 0.25f, 0.04f}) {
        const float sf = std::sqrt(f);
        for (float pct : {1.0f, 5.0f, 10.0f, 25.0f, 40.0f, 49.0f, 50.0f, 51.0f, 75.0f, 100.0f, 150.0f}) {
            const float s = pct / 100.0f;
            const PlaneKernel k = trimKernel(s);
            const glm::vec3 x(0.0f, 0.0f, h - kRadius * 0.02f * s); // (h above the plane)
            const glm::vec3 planePoint = k.areaCenter - k.areaNormal * (k.sign * k.r * 0.02f * s);
            const float hh = glm::dot(x - planePoint, k.areaNormal);
            const float cut = -k.offsetWith(x, f).z / hh;
            if (s >= 0.5f) {
                const float want = std::min(1.0f, sf * oldGain(s));
                CHECK(k.offsetWith(x, f).z == -(hh * want)); // bit for bit, as the formula was
            } else {
                const float want = sf * 2.0f * s;
                if (std::abs(cut - want) > 1e-4f)
                    std::printf("Intensity %.0f f %.2f: cut %.5f of the height, wanted %.5f\n", pct, f, cut, want);
                CHECK(std::abs(cut - want) < 1e-4f);
            }
        }
    }
}

// The carried tangents (the adaptive dab path) use the same gain: the offset's derivative is the numerical derivative of the offset.
TEST(trim_intensity_kernel_carry_matches_the_offset_slope) {
    for (float pct : {1.0f, 10.0f, 25.0f, 49.0f, 50.0f, 75.0f, 100.0f}) {
        const float s = pct / 100.0f;
        PlaneKernel k = trimKernel(s);
        k.center = glm::vec3(0.0f);
        const glm::vec3 x(0.06f, 0.03f, 0.05f);
        for (const glm::vec3 t : {glm::vec3(1, 0, 0), glm::vec3(0, 1, 0), glm::vec3(0.6f, 0.0f, 0.8f)}) {
            glm::vec3 d0, t1 = t, t2 = glm::vec3(0, 1, 0), d1, u1 = t, u2 = glm::vec3(0, 1, 0);
            CHECK(k.offsetAndCarry(x, 1.0f, d0, t1, t2));
            const float e = 1e-4f;
            CHECK(k.offsetAndCarry(x + t * e, 1.0f, d1, u1, u2));
            const glm::vec3 numeric = (d1 - d0) / e;           // J t
            const glm::vec3 carried = t1 - t;                  // J t, as the dab carries it
            const float err = glm::length(numeric - carried);
            if (err > 0.02f * std::max(0.05f, glm::length(numeric)))
                std::printf("Intensity %.0f: carried %.4f %.4f %.4f vs slope %.4f %.4f %.4f\n", pct, carried.x, carried.y, carried.z,
                            numeric.x, numeric.y, numeric.z);
            CHECK(err < 0.02f * std::max(0.05f, glm::length(numeric)));
        }
    }
}

// Intensity 1 is a light touch: at most a tenth of 50's depth on a stroke and on a single dab, and every step up cuts at
// least as deep and touches at least as much.
TEST(invariant_trim_intensity_one_is_a_light_touch_and_monotone) {
    Cut stroke[8];
    for (int i = 0; i < 8; ++i) {
        stroke[i] = sphereStroke(kIntensities[i]);
        std::printf("Trim stroke Intensity %3.0f: deepest %.2f%% of the radius, %zu moved, rise %.2g\n", kIntensities[i], stroke[i].depth,
                    stroke[i].moved, stroke[i].rise);
    }
    CHECK(stroke[0].depth > 0.0f);
    CHECK(stroke[0].depth <= 0.10f * stroke[5].depth);
    for (int i = 1; i < 8; ++i) {
        CHECK(stroke[i].depth >= stroke[i - 1].depth);
        CHECK(stroke[i].moved >= stroke[i - 1].moved);
    }
    // A single dab (the first of the stroke).
    float depth[8];
    size_t moved[8];
    for (int i = 0; i < 8; ++i) {
        Document d = geom::denseSphere();
        const std::vector<glm::vec3> before = d.mesh.positions;
        DabParams p = frontStroke(kIntensities[i], 1.0f, 1)[0];
        CHECK(d.sculptor.beginStroke(d.mesh, "Trim"));
        d.sculptor.dab(d.mesh, d.bvh, BrushType::Trim, p, Symmetry{false, false, false});
        d.undo.push(d.sculptor.endStroke());
        depth[i] = 0.0f, moved[i] = 0;
        for (uint32_t v = 0; v < d.mesh.vertexCount(); ++v) {
            const float m = glm::distance(before[v], d.mesh.positions[v]);
            depth[i] = std::max(depth[i], m / kRadius * 100.0f), moved[i] += m > 0.0f;
        }
        std::printf("Trim dab    Intensity %3.0f: deepest %.2f%% of the radius, %zu moved\n", kIntensities[i], depth[i], moved[i]);
    }
    CHECK(depth[0] > 0.0f);
    CHECK(depth[0] <= 0.10f * depth[5]);
    for (int i = 1; i < 8; ++i) {
        CHECK(depth[i] >= depth[i - 1]);
        CHECK(moved[i] >= moved[i - 1]);
    }
}

// Cut only, no spikes at the falloff edge, mirror symmetry: at the light Intensities and at 50.
TEST(invariant_trim_intensity_cut_only_clean_edge_and_symmetric) {
    for (float pct : {1.0f, 10.0f, 25.0f, 50.0f}) {
        const Cut c = sphereStroke(pct, true, 1.0f, 0.15f);
        std::printf("Trim Intensity %3.0f mirrored: rise %.2g, edge spikes %d (worst %.3f, inside %.3f), mirror error %.2g\n", pct, c.rise,
                    c.edge.spikes, c.edge.worst, c.edge.interior, c.asymmetry);
        CHECK(c.moved > 0);
        CHECK(c.rise <= 1e-6f);
        CHECK(c.edge.spikes == 0);
        // (the area's plane is read from a sample of the vertices, which is not mirror-symmetric: a few percent of the cut at most, as before)
        CHECK(c.asymmetry < 0.05f * c.depth / 100.0f * kRadius + 1e-5f);
    }
    // A pen's pressure multiplies Intensity the same way: pressure 0.02 at Intensity 50 is Intensity 1.
    const Cut pen = sphereStroke(50.0f, false, 0.02f), one = sphereStroke(1.0f);
    CHECK(pen.depth > 0.0f);
    CHECK(std::abs(pen.depth - one.depth) <= 0.05f * one.depth + 1e-4f);
}

// 50 and above: the stroke's positions, hashed (printed so a build before this change can be compared: the same bits).
TEST(trim_intensity_50_and_above_positions_hash) {
    for (float pct : {50.0f, 75.0f, 100.0f}) {
        const Cut c = sphereStroke(pct);
        std::printf("Trim Intensity %3.0f stroke positions hash %016llx\n", pct, (unsigned long long)c.hash);
        CHECK(c.moved > 0);
    }
}

namespace {
Document microSphere(int level) {
    Mesh m = makeIcosphere(5);
    std::string err;
    CHECK(microDivide(m, level, err, 0));
    geom::Fixture f;
    f.mesh = std::move(m);
    f.bvh.build(f.mesh);
    return geom::docFrom(f);
}
struct MicroCut {
    float depth = 0.0f;
    size_t moved = 0;
    uint64_t hash = 0;
};
MicroCut microStroke(float intensity, bool tiles) {
    Document d = microSphere(tiles ? 4 : 3);
    const float radius = tiles ? 0.02f : kRadius;
    if (tiles) {
        MicroMesh& mm = mutableMicro(d.mesh);
        mm.tiles = std::make_shared<MicroTiles>();
        mm.tiles->resize(mm.baseTriangles);
    }
    const geom::MicroSnapshot before = geom::microSnapshot(d.mesh, glm::vec3(0, 0, 1), 0.5f);
    std::vector<DabParams> dabs = frontStroke(intensity, 1.0f, tiles ? 5 : 11, radius, tiles ? 0.004f : 0.03f);
    CHECK(d.sculptor.beginStroke(d.mesh, "Trim"));
    for (const DabParams& p : dabs) d.sculptor.dab(d.mesh, d.bvh, BrushType::Trim, p, Symmetry{false, false, false});
    d.undo.push(d.sculptor.endStroke());
    MicroCut c;
    uint64_t h = 1469598103934665603ull;
    if (!tiles) {
        const geom::MicroSnapshot after = geom::microSnapshot(d.mesh, glm::vec3(0, 0, 1), 0.5f);
        CHECK(after.points.size() == before.points.size());
        for (size_t i = 0; i < before.points.size(); ++i) {
            const float m = glm::distance(before.points[i], after.points[i]);
            c.depth = std::max(c.depth, m / radius * 100.0f), c.moved += m > 0.0f;
            uint32_t bits[3];
            std::memcpy(bits, &after.points[i], sizeof bits);
            for (uint32_t b : bits) h = (h ^ b) * 1099511628211ull;
        }
    } else { // the tiles hold the change (the root grid is untouched by a dab this small)
        const MicroMesh& mm = *d.mesh.micro;
        mm.tiles->forEach([&](uint64_t, const MicroTiles::Tile& t) {
            for (int16_t v : t.delta) {
                c.depth = std::max(c.depth, std::abs(float(v)) * mm.step / radius * 100.0f), c.moved += v != 0;
                h = (h ^ uint16_t(v)) * 1099511628211ull;
            }
        });
    }
    c.hash = h;
    return c;
}
} // namespace

// The micro paths (the dense grid, and Adaptive Detail tiles for dabs finer than the grid) take the same gain.
TEST(invariant_trim_intensity_micro_level_is_proportional_below_50) {
    for (bool tiles : {false, true}) {
        MicroCut cut[8];
        for (int i = 0; i < 8; ++i) {
            cut[i] = microStroke(kIntensities[i], tiles);
            std::printf("Trim micro%s Intensity %3.0f: deepest %.3f%% of the radius, %zu points, hash %016llx\n", tiles ? " tiles" : "      ",
                        kIntensities[i], cut[i].depth, cut[i].moved, (unsigned long long)cut[i].hash);
        }
        CHECK(cut[5].depth > 0.0f);
        CHECK(cut[0].depth <= 0.10f * cut[5].depth);
        for (int i = 1; i < 8; ++i) {
            CHECK(cut[i].depth >= cut[i - 1].depth);
            CHECK(cut[i].moved >= cut[i - 1].moved);
        }
    }
}
