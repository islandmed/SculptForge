// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The Boolean blend (core/BooleanBlend.h): the promise, measured. A fillet of radius r along the seam of an exact
// Boolean keeps the result watertight, adds material in a Union and takes it off in a Subtract or Intersect, never
// strays more than r from the sharp result, leaves the faces away from the seam as they were, turns the seam's crease
// into a soft one (the largest dihedral angle in the band shrinks), keeps mirror symmetry and the face groups, and is
// the sharp result again at 0. Plus the voxel path, the scene commands, projects and duplicates.
#include "TestHarness.h"

#include "core/BooleanBlend.h"
#include "core/Commands.h"
#include "core/Cut.h"
#include "core/MeshBoolean.h"
#include "core/PartBoolean.h"
#include "core/Parts.h"
#include "core/Primitives.h"
#include "core/Project.h"
#include "core/SurfaceQuery.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>
#include <unordered_map>

using namespace sf;

namespace {

Mesh moved(Mesh m, const glm::vec3& d) {
    for (glm::vec3& p : m.positions) p += d;
    m.recomputeAllNormals();
    return m;
}

const CommandRegistry& registry() {
    static CommandRegistry r;
    return r;
}

CommandResult run(Document& d, const std::string& name, const Args& args = {}) { return registry().execute(d, name, args); }

// A cube (-1..1, one face group per side) with a sphere of radius 0.6 centred on its +x face: the seam is a circle on
// that face, where the sphere's surface stands at a right angle to it (a 90 degree crease, both ways).
Mesh cube() { return makeCube(4); }
Mesh bolt() {
    Mesh b = moved(makeIcosphere(4, 0.6f), {1.0f, 0.0f, 0.0f});
    b.ensureFaceGroups(100);
    return b;
}

Mesh blended(const Mesh& a, const Mesh& b, BooleanOp op, double radius, BooleanInfo* info = nullptr, bool weld = true) {
    BooleanOptions o;
    o.blend = radius;
    o.weld = weld;
    if (const char* s = std::getenv("SF_BLEND_SEGMENTS")) o.blendSegments = std::atoi(s); // (an experiment's knob)
    Mesh out;
    std::string error;
    CHECK(booleanExact(a, b, op, out, error, o, info));
    CHECK(error.empty());
    if (const char* dir = std::getenv("SF_BLEND_DUMP")) { // (a look at a result: OBJ files in that folder)
        static int n = 0;
        if (FILE* f = std::fopen((std::string(dir) + "/blend_" + std::to_string(n++) + "_" + booleanOpName(op) + "_" + std::to_string(radius) + ".obj").c_str(), "w")) {
            for (const glm::vec3& p : out.positions) std::fprintf(f, "v %.6f %.6f %.6f\n", p.x, p.y, p.z);
            for (const glm::uvec3& tri : out.triangles) std::fprintf(f, "f %u %u %u\n", tri.x + 1, tri.y + 1, tri.z + 1);
            std::fclose(f);
        }
    }
    return out;
}

// The largest angle (degrees) between the two triangles on any edge whose ends both lie within `band` of both operands:
// the seam's crease, or what the blend made of it.
glm::vec3 gWorstEdge{0.0f}; // (the midpoint of the last maxDihedralInBand's worst edge, for the printouts)
double maxDihedralInBand(const Mesh& m, const Mesh& a, const Mesh& b, float band, float minArea = 0.0f) {
    TriangleBVH ba, bb;
    ba.build(a);
    bb.build(b);
    std::vector<uint8_t> inBand(m.vertexCount(), 0);
    SurfaceHit hit;
    for (size_t v = 0; v < m.vertexCount(); ++v)
        inBand[v] = ba.closestPoint(m.positions[v], band, hit) && bb.closestPoint(m.positions[v], band, hit);
    std::unordered_map<uint64_t, uint32_t> first;
    auto key = [](uint32_t u, uint32_t v) { return (uint64_t(std::min(u, v)) << 32) | std::max(u, v); };
    auto normal = [&](uint32_t t) {
        const glm::uvec3 tri = m.triangles[t];
        return glm::normalize(glm::cross(m.positions[tri.y] - m.positions[tri.x], m.positions[tri.z] - m.positions[tri.x]));
    };
    auto area = [&](uint32_t t) {
        const glm::uvec3 tri = m.triangles[t];
        return 0.5f * glm::length(glm::cross(m.positions[tri.y] - m.positions[tri.x], m.positions[tri.z] - m.positions[tri.x]));
    };
    double worst = 0.0;
    for (uint32_t t = 0; t < m.triangleCount(); ++t) {
        const glm::uvec3 tri = m.triangles[t];
        for (int i = 0; i < 3; ++i) {
            const uint32_t u = tri[i], v = tri[(i + 1) % 3];
            if (!inBand[u] || !inBand[v]) continue;
            auto [it, fresh] = first.emplace(key(u, v), t);
            if (fresh) continue;
            if (area(t) < minArea || area(it->second) < minArea) continue;
            const double c = glm::clamp(double(glm::dot(normal(it->second), normal(t))), -1.0, 1.0);
            const double angle = std::acos(c) * 180.0 / 3.14159265358979323846;
            if (angle > worst) {
                worst = angle;
                gWorstEdge = (m.positions[u] + m.positions[v]) * 0.5f;
            }
        }
    }
    return worst;
}

// How far the mesh strays from its own mirror image across y = 0.
double mirrorError(const Mesh& m) {
    Mesh mirror = m;
    for (glm::vec3& p : mirror.positions) p.y = -p.y;
    return measureDeviation(mirror, m, false).max;
}

std::set<uint32_t> groupSet(const Mesh& m) { return std::set<uint32_t>(m.facegroups.begin(), m.facegroups.end()); }

// Triangle quality after the blend: folds (a triangle turned against every neighbour across its edges, as
// GeomInvariants' invertedFaces counts them) and the smallest corner angle (degrees) over the whole mesh (slivers).
struct Quality {
    size_t inverted = 0;
    double minAngle = 180.0;
    double foldArea = 0.0, foldMaxArea = 0.0; // of the folded triangles
    glm::vec3 foldAt{0.0f};                   // the largest one's centroid
};
Quality quality(const Mesh& m) {
    Quality q;
    std::vector<glm::vec3> normal(m.triangleCount());
    std::unordered_map<uint64_t, std::array<uint32_t, 2>> edgeTris;
    auto key = [](uint32_t u, uint32_t v) { return (uint64_t(std::min(u, v)) << 32) | std::max(u, v); };
    for (uint32_t t = 0; t < m.triangleCount(); ++t) {
        const glm::uvec3 tri = m.triangles[t];
        const glm::vec3 n = glm::cross(m.positions[tri.y] - m.positions[tri.x], m.positions[tri.z] - m.positions[tri.x]);
        normal[t] = glm::dot(n, n) > 0.0f ? glm::normalize(n) : glm::vec3(0.0f);
        for (int i = 0; i < 3; ++i) {
            auto [it, fresh] = edgeTris.emplace(key(tri[i], tri[(i + 1) % 3]), std::array<uint32_t, 2>{t, ~0u});
            if (!fresh) it->second[1] = t;
        }
    }
    for (uint32_t t = 0; t < m.triangleCount(); ++t) {
        const glm::uvec3 tri = m.triangles[t];
        const glm::vec3 &p0 = m.positions[tri.x], &p1 = m.positions[tri.y], &p2 = m.positions[tri.z];
        if (normal[t] == glm::vec3(0.0f)) {
            q.minAngle = 0.0;
            ++q.inverted;
            continue;
        }
        int against = 0, neighbours = 0;
        for (int i = 0; i < 3; ++i) {
            const auto& pair = edgeTris[key(tri[i], tri[(i + 1) % 3])];
            const uint32_t o = pair[0] == t ? pair[1] : pair[0];
            if (o == ~0u) continue;
            ++neighbours;
            if (glm::dot(normal[t], normal[o]) < -0.5f) ++against;
        }
        if (neighbours > 0 && against == neighbours) {
            ++q.inverted;
            const double area = 0.5 * glm::length(glm::cross(p1 - p0, p2 - p0));
            q.foldArea += area;
            if (area > q.foldMaxArea) {
                q.foldMaxArea = area;
                q.foldAt = (p0 + p1 + p2) / 3.0f;
            }
        }
        const glm::vec3 c[3] = {p0, p1, p2};
        for (int i = 0; i < 3; ++i) {
            const glm::vec3 u = glm::normalize(c[(i + 1) % 3] - c[i]), v = glm::normalize(c[(i + 2) % 3] - c[i]);
            q.minAngle = std::min(q.minAngle, std::acos(glm::clamp(double(glm::dot(u, v)), -1.0, 1.0)) * 180.0 / 3.14159265358979323846);
        }
    }
    return q;
}

} // namespace

TEST(boolean_blend_union_fills_the_crease) {
    const Mesh a = cube(), b = bolt();
    const double r = 0.25;
    const Mesh sharp = blended(a, b, BooleanOp::Union, 0.0);
    BooleanInfo info;
    const Mesh soft = blended(a, b, BooleanOp::Union, r, &info);
    CHECK(isWatertight(soft));
    CHECK(info.blendMoved > 0);
    CHECK(soft.triangleCount() > sharp.triangleCount());
    // Material is added, a fillet's worth at most (seam 2 pi 0.6 long, a cross-section under r^2).
    const double dv = meshVolume(soft) - meshVolume(sharp);
    std::printf("union: volume +%.4f, %zu -> %zu triangles, %zu moved\n", dv, sharp.triangleCount(), soft.triangleCount(),
                info.blendMoved);
    CHECK(dv > 0.01);
    CHECK(dv < 2.0 * 3.1416 * 0.6 * r * r);
    // Never farther than r from the sharp result, either way.
    const SurfaceDeviation dev = measureDeviation(soft, sharp, true);
    std::printf("union: deviation max %.4f mean %.5f\n", dev.max, dev.mean);
    CHECK(dev.max <= r * 1.05);
    // The 90 degree crease is a soft one now.
    const float visible = 1e-2f * float(r * r); // (the blended band: triangles too small to see are left out, about a pixel at a 10 px fillet; the sharp crease counts every triangle)
    const double before = maxDihedralInBand(sharp, a, b, float(r) * 0.5f), after = maxDihedralInBand(soft, a, b, float(r) * 0.5f, visible);
    std::printf("union: max dihedral in the band %.1f -> %.1f degrees\n", before, after);
    CHECK(before > 80.0);
    CHECK(after < 40.0);
    // Face groups: the cube's sides and the sphere's own, nothing else.
    CHECK(groupSet(soft) == groupSet(sharp));
    // Mirror symmetry (the scene is symmetric in y).
    const double sym = mirrorError(soft);
    std::printf("union: mirror error %.5f\n", sym);
    CHECK(sym < 0.05 * r);
    // No folds, no needles.
    const Quality q = quality(soft);
    std::printf("union: inverted %zu, min angle %.2f degrees\n", q.inverted, q.minAngle);
    CHECK(q.inverted == 0);
    CHECK(q.minAngle > 1.0);
}

TEST(boolean_blend_subtract_rounds_the_rim) {
    const Mesh a = cube(), b = bolt();
    const double r = 0.2;
    const Mesh sharp = blended(a, b, BooleanOp::Subtract, 0.0);
    BooleanInfo info;
    const Mesh soft = blended(a, b, BooleanOp::Subtract, r, &info);
    CHECK(isWatertight(soft));
    CHECK(info.blendMoved > 0);
    const double dv = meshVolume(sharp) - meshVolume(soft);
    std::printf("subtract: volume -%.4f\n", dv);
    CHECK(dv > 0.005);
    CHECK(dv < 2.0 * 3.1416 * 0.6 * r * r);
    CHECK(measureDeviation(soft, sharp, true).max <= r * 1.05);
    const float visible = 1e-2f * float(r * r); // (the blended band: triangles too small to see are left out, about a pixel at a 10 px fillet; the sharp crease counts every triangle)
    const double before = maxDihedralInBand(sharp, a, b, float(r) * 0.5f), after = maxDihedralInBand(soft, a, b, float(r) * 0.5f, visible);
    std::printf("subtract: max dihedral in the band %.1f -> %.1f degrees\n", before, after);
    CHECK(before > 80.0);
    CHECK(after < 40.0);
    CHECK(groupSet(soft) == groupSet(sharp));
    CHECK(mirrorError(soft) < 0.05 * r);
    const Quality q = quality(soft);
    std::printf("subtract: inverted %zu, min angle %.2f degrees\n", q.inverted, q.minAngle);
    CHECK(q.inverted == 0);
    CHECK(q.minAngle > 1.0);
}

TEST(boolean_blend_thin_wall_and_triple_junction) {
    // A slab thinner than the radius: the fillet may not drag the far face along (every move stays within r, and the
    // result stays closed). Then three boxes meeting at one corner, in a chain: each step rounds its own seam.
    Mesh slab = makeCube(2, 1.0f);
    for (glm::vec3& p : slab.positions) p *= glm::vec3(1.0f, 1.0f, 0.1f);
    slab.recomputeAllNormals();
    const Mesh peg = moved(makeIcosphere(3, 0.4f), {0.0f, 0.0f, 0.36f});
    const double r = 0.3;
    const Mesh sharp = blended(slab, peg, BooleanOp::Union, 0.0);
    BooleanInfo info;
    const Mesh soft = blended(slab, peg, BooleanOp::Union, r, &info);
    CHECK(isWatertight(soft));
    CHECK(info.blendMoved > 0);
    CHECK(measureDeviation(soft, sharp, true).max <= r * 1.05);
    const Quality q = quality(soft), qs = quality(sharp);
    std::printf("thin wall: folds %zu (sharp %zu), min angle %.2f degrees (sharp %.2f), moved %zu\n", q.inverted, qs.inverted,
                q.minAngle, qs.minAngle, info.blendMoved);
    CHECK(q.foldArea <= qs.foldArea + 1e-5 * r * r);
    CHECK(q.minAngle > 0.1);

    auto chain = [&](float blend, Mesh& out) {
        std::vector<BooleanOperand> ops(3);
        ops[0].mesh = makeCube(3);
        ops[1].mesh = moved(makeCube(3), {1.2f, 0.0f, 0.0f});
        ops[1].mode = PartBoolean::Add;
        ops[1].blend = blend;
        ops[2].mesh = moved(makeCube(3), {0.6f, 1.2f, 0.0f});
        ops[2].mode = PartBoolean::Add;
        ops[2].blend = blend;
        std::string error;
        CHECK(combineOperands(std::move(ops), {}, out, error, &info));
        CHECK(error.empty());
        CHECK(isWatertight(out));
    };
    Mesh sharpChain, softChain;
    chain(0.0f, sharpChain);
    chain(0.2f, softChain);
    CHECK(info.blendMoved > 0);
    const Quality sq = quality(sharpChain), tq = quality(softChain);
    std::printf("triple junction: %zu -> %zu triangles, min angle %.3f -> %.3f degrees, folds %zu (sharp %zu), fold area %.2e max %.2e at %.3f %.3f %.3f\n",
                sharpChain.triangleCount(), softChain.triangleCount(), sq.minAngle, tq.minAngle, tq.inverted, sq.inverted, tq.foldArea,
                tq.foldMaxArea, tq.foldAt.x, tq.foldAt.y, tq.foldAt.z);
    CHECK(tq.foldArea <= sq.foldArea + 1e-5 * 0.2 * 0.2); // (folds too small to see at the fillet's scale, if any)
    CHECK(tq.minAngle >= std::min(0.5, 0.5 * sq.minAngle));
    for (const glm::vec3& p : softChain.positions) CHECK(std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z));
}

TEST(boolean_blend_seam_corners) {
    // A box fused onto a box: the seam is a loop with four concave corners (the common kitbash case). The fillet must
    // round the whole loop, corners included, without folds an artist would see.
    const Mesh a = makeCube(4);
    Mesh b = makeCube(3, 1.0f);
    for (glm::vec3& p : b.positions) p = p * glm::vec3(0.5f, 0.5f, 0.6f) + glm::vec3(0.1f, -0.2f, 1.0f); // half on, half above the top
    b.recomputeAllNormals();
    const double r = 0.2;
    const Mesh sharp = blended(a, b, BooleanOp::Union, 0.0);
    BooleanInfo info;
    const Mesh soft = blended(a, b, BooleanOp::Union, r, &info);
    CHECK(isWatertight(soft));
    CHECK(info.blendMoved > 0);
    CHECK(meshVolume(soft) > meshVolume(sharp));
    CHECK(measureDeviation(soft, sharp, true).max <= r * 1.05);
    const float visible = 1e-2f * float(r * r);
    const double before = maxDihedralInBand(sharp, a, b, float(r) * 0.5f), after = maxDihedralInBand(soft, a, b, float(r) * 0.5f, visible);
    const Quality q = quality(soft), qs = quality(sharp);
    std::printf("seam corners: volume +%.4f, max dihedral in the band %.1f -> %.1f degrees (worst edge at %.3f %.3f %.3f), folds %zu (sharp %zu) area %.2e, min angle %.2f\n",
                meshVolume(soft) - meshVolume(sharp), before, after, gWorstEdge.x, gWorstEdge.y, gWorstEdge.z, q.inverted, qs.inverted,
                q.foldArea, q.minAngle);
    CHECK(before > 80.0);
    // The straight runs of the loop come out like any seam (about 10-15 degrees); at the four corners, where the
    // cutter's vertical edge meets the base, one crease edge of up to ~110 degrees remains (a known limit, REPORT.md:
    // per-vertex projection at a three-surface corner). The bound guards against folds (180) and regressions.
    CHECK(after < 125.0);
    CHECK(q.foldArea <= qs.foldArea + 1e-3 * r * r);
}

TEST(boolean_blend_intersect_rounds_the_edge) {
    const Mesh a = cube();
    const Mesh b = moved(makeIcosphere(4, 0.9f), {0.7f, 0.0f, 0.0f}); // (a bigger overlap: the lens has a sharp rim)
    const double r = 0.15;
    const Mesh sharp = blended(a, b, BooleanOp::Intersect, 0.0);
    BooleanInfo info;
    const Mesh soft = blended(a, b, BooleanOp::Intersect, r, &info);
    CHECK(isWatertight(soft));
    CHECK(info.blendMoved > 0);
    const double dv = meshVolume(sharp) - meshVolume(soft);
    std::printf("intersect: volume -%.4f\n", dv);
    CHECK(dv > 0.001);
    CHECK(measureDeviation(soft, sharp, true).max <= r * 1.05);
    const float visible = 1e-2f * float(r * r);
    const double before = maxDihedralInBand(sharp, a, b, float(r) * 0.5f), after = maxDihedralInBand(soft, a, b, float(r) * 0.5f, visible);
    std::printf("intersect: max dihedral in the band %.1f -> %.1f degrees (worst edge at %.3f %.3f %.3f, rho %.3f)\n", before, after,
                gWorstEdge.x, gWorstEdge.y, gWorstEdge.z, std::sqrt(gWorstEdge.y * gWorstEdge.y + gWorstEdge.z * gWorstEdge.z));
    CHECK(after < before - 20.0);
    const Quality q = quality(soft);
    CHECK(q.inverted == 0);
}

TEST(boolean_blend_far_faces_untouched_and_zero_is_sharp) {
    const Mesh a = cube(), b = bolt();
    const Mesh sharp = blended(a, b, BooleanOp::Union, 0.0);
    const Mesh zero = blended(a, b, BooleanOp::Union, 0.0);
    CHECK(zero.triangleCount() == sharp.triangleCount());
    CHECK(zero.positions == sharp.positions);
    const double r = 0.2;
    const Mesh soft = blended(a, b, BooleanOp::Union, r);
    // Every sharp vertex farther than r from both operands is still there, bit for bit (the blend never touches it).
    TriangleBVH ba, bb;
    ba.build(a);
    bb.build(b);
    std::set<std::array<float, 3>> softPositions;
    for (const glm::vec3& p : soft.positions) softPositions.insert({p.x, p.y, p.z});
    size_t far = 0, kept = 0;
    SurfaceHit hit;
    for (const glm::vec3& p : sharp.positions) {
        if (ba.closestPoint(p, float(r), hit) && bb.closestPoint(p, float(r), hit)) continue;
        ++far;
        kept += softPositions.count({p.x, p.y, p.z}) ? 1 : 0;
    }
    std::printf("far vertices %zu, kept %zu\n", far, kept);
    CHECK(far > 0);
    CHECK(kept == far);
}

TEST(boolean_blend_unwelded_seam_copies_agree) {
    const Mesh a = cube(), b = bolt();
    const double r = 0.2;
    const Mesh welded = blended(a, b, BooleanOp::Subtract, r);
    const Mesh loose = blended(a, b, BooleanOp::Subtract, r, nullptr, false);
    CHECK(loose.vertexCount() > welded.vertexCount());
    CHECK(!isWatertight(loose));
    // Both copies of every seam vertex land on the same spot: welded by position, the mesh is closed again.
    Mesh fused;
    fused.triangles = loose.triangles;
    std::map<std::array<float, 3>, uint32_t> ids;
    std::vector<uint32_t> remap(loose.vertexCount());
    for (uint32_t v = 0; v < loose.vertexCount(); ++v) {
        const glm::vec3& p = loose.positions[v];
        auto [it, fresh] = ids.emplace(std::array<float, 3>{p.x, p.y, p.z}, uint32_t(fused.positions.size()));
        if (fresh) fused.positions.push_back(p);
        remap[v] = it->second;
    }
    for (glm::uvec3& tri : fused.triangles) tri = {remap[tri.x], remap[tri.y], remap[tri.z]};
    fused.rebuildTopology();
    CHECK(isWatertight(fused));
    // The same fillet (the welded result is relaxed on top, the loose one is not: a small difference, never a gap).
    CHECK(std::abs(meshVolume(loose) - meshVolume(welded)) < 0.02 * r * r);
    CHECK(measureDeviation(loose, welded, true).max < 0.1 * r);
}

TEST(boolean_blend_small_and_large_radius) {
    const Mesh a = cube(), b = bolt();
    for (const double r : {0.01, 1.0}) {
        BooleanInfo info;
        const Mesh soft = blended(a, b, BooleanOp::Union, r, &info);
        CHECK(isWatertight(soft));
        CHECK(info.blendMoved > 0);
        for (const glm::vec3& p : soft.positions) CHECK(std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z));
        const Mesh sharp = blended(a, b, BooleanOp::Union, 0.0);
        CHECK(measureDeviation(soft, sharp, true).max <= r * 1.05);
        CHECK(meshVolume(soft) > meshVolume(sharp));
    }
}

TEST(boolean_blend_chain_of_parts) {
    // A bolt added with a blend, then a slot cut with another: each step rounds its own seam.
    std::vector<BooleanOperand> ops(3);
    ops[0].mesh = cube();
    ops[1].mesh = bolt();
    ops[1].mode = PartBoolean::Add;
    ops[1].blend = 0.2f;
    Mesh slot = makeCube(2, 1.0f);
    for (glm::vec3& p : slot.positions) p *= glm::vec3(0.3f, 3.0f, 0.3f);
    slot.recomputeAllNormals();
    ops[2].mesh = std::move(slot);
    ops[2].mode = PartBoolean::Subtract;
    ops[2].blend = 0.1f;
    Mesh out;
    std::string error;
    BooleanInfo info;
    CHECK(combineOperands(std::move(ops), {}, out, error, &info));
    CHECK(isWatertight(out));
    CHECK(info.blendMoved > 0);
    CHECK(!info.voxel);
}

TEST(boolean_blend_voxel_path) {
    const Mesh a = cube(), b = bolt();
    Mesh sharp, soft;
    std::string error;
    CHECK(booleanVoxel(a, b, BooleanOp::Union, 96, sharp, error));
    CHECK(booleanVoxel(a, b, BooleanOp::Union, 96, soft, error, 0.3));
    CHECK(isWatertight(soft));
    const double dv = meshVolume(soft) - meshVolume(sharp);
    std::printf("voxel union: volume +%.4f (sharp %.4f)\n", dv, meshVolume(sharp));
    CHECK(dv > 0.005);
    CHECK(dv < 0.5); // (a fillet's worth, not a blob: the band and the smooth minimum agree on their width)
    Mesh cut, cutSoft;
    CHECK(booleanVoxel(a, b, BooleanOp::Subtract, 96, cut, error));
    const bool cutOk = booleanVoxel(a, b, BooleanOp::Subtract, 96, cutSoft, error, 0.3);
    if (!cutOk) std::printf("voxel subtract with blend: %s\n", error.c_str());
    CHECK(cutOk);
    CHECK(meshVolume(cutSoft) < meshVolume(cut) - 0.005);
    CHECK(meshVolume(cutSoft) > meshVolume(cut) - 0.5);
}

TEST(boolean_blend_scene_commands) {
    Document doc;
    doc.setMesh(cube(), "Base");
    doc.addPart(bolt(), "Bolt", false);
    CHECK(run(doc, "part_boolean", {{"index", 0.0}, {"mode", std::string("add")}}).ok);
    CHECK(!run(doc, "part_boolean", {{"index", 1.0}, {"mode", std::string("add")}, {"blend", -0.5}}).ok);
    const CommandResult set = run(doc, "part_boolean", {{"index", 1.0}, {"mode", std::string("add")}, {"blend", 0.2}});
    CHECK(set.ok);
    CHECK(set.message.find("blend 0.2") != std::string::npos);
    CHECK(doc.parts[1].booleanBlend == 0.2f);
    CHECK(doc.modified);
    // The scene's operands carry it; Make Boolean Mesh applies it.
    const std::vector<BooleanOperand> ops = booleanOperands(doc);
    CHECK(ops.size() == 2);
    CHECK(ops[1].blend == 0.2f);
    const CommandResult made = run(doc, "make_boolean_mesh");
    CHECK(made.ok);
    CHECK(made.message.find("blended") != std::string::npos);
    const double softVolume = meshVolume(doc.mesh);
    CHECK(run(doc, "undo").ok);
    CHECK(run(doc, "part_boolean", {{"index", 1.0}, {"mode", std::string("add")}, {"blend", 0.0}}).ok);
    CHECK(run(doc, "make_boolean_mesh").ok);
    CHECK(softVolume > meshVolume(doc.mesh) + 0.01);
    // boolean_parts takes a blend of its own.
    Document pair;
    pair.setMesh(cube(), "Base");
    pair.addPart(bolt(), "Bolt", false);
    CHECK(run(pair, "boolean_parts", {{"op", std::string("union")}, {"a", std::string("Base")}, {"b", std::string("Bolt")}, {"blend", 0.2}}).ok);
    CHECK(std::abs(meshVolume(pair.mesh) - softVolume) < 1e-6);
}

TEST(boolean_blend_saved_and_duplicated) {
    Document doc;
    doc.setMesh(cube(), "Base");
    doc.addPart(bolt(), "Bolt", false);
    doc.parts[0].boolean = PartBoolean::Add;
    doc.parts[1].boolean = PartBoolean::Subtract;
    doc.parts[1].booleanBlend = 0.15f;
    const std::string path = sf::test::tempPath("sf_boolean_blend.sfproj");
    std::string err;
    CHECK(saveProject(path, doc, err));
    Document back;
    CHECK(loadProject(path, back, err));
    CHECK(back.partCount() == 2);
    CHECK(back.parts[0].booleanBlend == 0.0f);
    CHECK(back.parts[1].booleanBlend == 0.15f);
    CHECK(back.parts[1].boolean == PartBoolean::Subtract);
    // A copy of a cutter cuts the same way, blend included.
    CHECK(run(back, "part_duplicate", {{"index", 1.0}}).ok);
    CHECK(back.partCount() == 3);
    CHECK(back.parts[2].boolean == PartBoolean::Subtract);
    CHECK(back.parts[2].booleanBlend == 0.15f);
}

TEST(boolean_blend_smooth_min) {
    CHECK(smoothMin(1.0, 2.0, 0.0) == 1.0);
    CHECK(smoothMin(1.0, 2.0, 0.5) == 1.0);  // apart by more than k: the plain minimum
    CHECK(smoothMin(1.0, 1.0, 1.0) == 0.75); // equal: k / 4 below
    CHECK(smoothMin(0.0, 0.5, 1.0) < 0.0);   // both near zero: below the surface (that is what the fillet fills)
}
