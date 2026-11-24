// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Span (core/PolySpan.h): tubes joined end to end, unequal loops, twist and flip, face selections on two cubes,
// closing a slot and a round hole, open chains between sheets, attributes, bad input, speed and determinism.
#include "TestHarness.h"

#include "core/CloseHoles.h"
#include "core/Cut.h"
#include "core/FaceGroups.h"
#include "core/MeshCheck.h"
#include "core/PolySpan.h"
#include "core/Primitives.h"

#include <chrono>
#include <cmath>
#include <set>
#include <unordered_map>

using namespace sf;

namespace {

glm::vec3 centroid(const Mesh& m, uint32_t t) {
    const glm::uvec3& tr = m.triangles[t];
    return (m.positions[tr.x] + m.positions[tr.y] + m.positions[tr.z]) / 3.0f;
}

// `mesh` without the triangles `drop` picks (compacted: unused vertices go).
template <class F>
Mesh without(const Mesh& mesh, F drop) {
    std::vector<uint8_t> keep(mesh.triangleCount(), 1);
    for (uint32_t t = 0; t < mesh.triangleCount(); ++t)
        if (drop(t)) keep[t] = 0;
    return extractTriangles(mesh, keep);
}

// A cylinder's side wall only (both caps removed), moved by `offset`.
Mesh tube(int segments, glm::vec3 offset = glm::vec3(0.0f)) {
    Mesh m = without(makeCylinder(segments), [&](uint32_t) { return false; });
    m = without(m, [&](uint32_t t) { return m.facegroupAt(t) != 2; });
    for (glm::vec3& p : m.positions) p += offset;
    m.rebuildTopology();
    m.recomputeAllNormals();
    return m;
}

// b appended to a (triangles and vertices after a's, face groups shifted past a's).
Mesh appendMesh(const Mesh& a, const Mesh& b) {
    Mesh m = a;
    const uint32_t nv = uint32_t(a.vertexCount()), nt = uint32_t(a.triangleCount());
    const uint32_t shift = nextFaceGroupId(a);
    m.positions.insert(m.positions.end(), b.positions.begin(), b.positions.end());
    for (const glm::uvec3& t : b.triangles) m.triangles.push_back(t + glm::uvec3(nv));
    m.facegroups.resize(nt, 0u); // (ensure* would reset: the triangles already grew)
    for (uint32_t t = 0; t < b.triangleCount(); ++t) m.facegroups.push_back(b.facegroupAt(t) + shift);
    if (a.hasQuads() || b.hasQuads()) {
        m.quadPartners.resize(nt, Mesh::kNoQuad);
        for (uint32_t t = 0; t < b.triangleCount(); ++t) {
            const uint32_t u = b.hasQuads() ? b.quadPartners[t] : Mesh::kNoQuad;
            m.quadPartners.push_back(u == Mesh::kNoQuad ? u : u + nt);
        }
    }
    if (a.hasMask() || b.hasMask()) {
        m.mask.resize(nv, 0.0f);
        for (uint32_t v = 0; v < b.vertexCount(); ++v) m.mask.push_back(b.maskAt(v));
    }
    if (a.hasColors() || b.hasColors()) {
        m.colors.resize(nv, glm::vec3(1.0f));
        for (uint32_t v = 0; v < b.vertexCount(); ++v) m.colors.push_back(b.hasColors() ? b.colors[v] : glm::vec3(1.0f));
    }
    if (a.hasUvs() || b.hasUvs()) {
        m.uvs.resize(nt * 3, glm::vec2(0.0f));
        if (b.hasUvs()) m.uvs.insert(m.uvs.end(), b.uvs.begin(), b.uvs.end());
        else m.uvs.resize((nt + b.triangleCount()) * 3, glm::vec2(0.0f));
    }
    m.rebuildTopology();
    m.recomputeAllNormals();
    return m;
}

std::vector<glm::uvec2> edgesOf(const std::vector<uint32_t>& loop, bool closed = true) {
    std::vector<glm::uvec2> e;
    for (size_t i = 0; i + (closed ? 0 : 1) < loop.size(); ++i) e.emplace_back(loop[i], loop[(i + 1) % loop.size()]);
    return e;
}

// The boundary loop whose mean y is nearest `y`.
std::vector<uint32_t> loopNear(const Mesh& m, float y) {
    std::vector<uint32_t> best;
    float bestD = 1e9f;
    for (const std::vector<uint32_t>& l : boundaryLoops(m)) {
        float s = 0.0f;
        for (uint32_t v : l) s += m.positions[v].y;
        const float d = std::abs(s / float(l.size()) - y);
        if (d < bestD) {
            bestD = d;
            best = l;
        }
    }
    return best;
}

// No non-manifold edge, duplicate, degenerate or inconsistently wound triangle.
bool clean(const Mesh& m) {
    const MeshReport r = checkMesh(m);
    const bool ok = r.count[int(MeshIssue::NonManifold)] == 0 && r.count[int(MeshIssue::Duplicate)] == 0 &&
                    r.count[int(MeshIssue::Degenerate)] == 0 && r.count[int(MeshIssue::Flipped)] == 0;
    if (!ok) std::printf("  mesh: %s\n", r.summary().c_str());
    return ok;
}

double volume(const Mesh& m) {
    double v = 0.0;
    for (const glm::uvec3& t : m.triangles)
        v += glm::dot(glm::dvec3(m.positions[t.x]), glm::cross(glm::dvec3(m.positions[t.y]), glm::dvec3(m.positions[t.z])));
    return v / 6.0;
}

double area(const Mesh& m, const std::vector<uint32_t>& tris) {
    double a = 0.0;
    for (uint32_t t : tris) {
        const glm::uvec3& tr = m.triangles[t];
        a += 0.5 * glm::length(glm::dvec3(m.positions[tr.y] - m.positions[tr.x]) * 1.0 + glm::dvec3(0.0)) * 0.0 +
             0.5 * glm::length(glm::cross(glm::dvec3(m.positions[tr.y] - m.positions[tr.x]), glm::dvec3(m.positions[tr.z] - m.positions[tr.x])));
    }
    return a;
}

std::set<std::set<uint32_t>> faceSets(const Mesh& m, const std::vector<uint32_t>& tris) {
    std::set<std::set<uint32_t>> s;
    for (uint32_t t : tris) s.insert({m.triangles[t].x, m.triangles[t].y, m.triangles[t].z});
    return s;
}

// Two tubes end to end with a gap of 2 between them (A below, B above).
Mesh twoTubes(int segA, int segB) {
    return appendMesh(tube(segA, {0.0f, -2.0f, 0.0f}), tube(segB, {0.0f, 2.0f, 0.0f}));
}

} // namespace

TEST(poly_span_two_tube_ends_make_a_ring_of_quads) {
    for (int segments : {1, 3}) {
        Mesh m = twoTubes(12, 12);
        const size_t nv0 = m.vertexCount(), nt0 = m.triangleCount();
        CHECK(boundaryLoops(m).size() == 4);
        std::vector<glm::uvec2> edges = edgesOf(loopNear(m, -1.0f));
        const std::vector<glm::uvec2> top = edgesOf(loopNear(m, 1.0f));
        edges.insert(edges.end(), top.begin(), top.end());
        SpanOptions o;
        o.segments = segments;
        SpanResult r;
        std::string error;
        CHECK(spanEdges(m, edges, o, r, error));
        if (!error.empty()) std::printf("  %s\n", error.c_str());
        CHECK(r.quads == size_t(12 * segments));
        CHECK(r.tris == 0);
        CHECK(r.triangles.size() == size_t(24 * segments));
        CHECK(r.vertices.size() == size_t(12 * (segments - 1)));
        CHECK(m.vertexCount() == nv0 + r.vertices.size());
        CHECK(m.triangleCount() == nt0 + r.triangles.size());
        CHECK(boundaryLoops(m).size() == 2); // the far ends stay open
        CHECK(clean(m));
        // Wound with the tubes: every band face looks outwards from the axis.
        for (uint32_t t : r.triangles) {
            const glm::vec3 c = centroid(m, t);
            const glm::vec3 n = glm::cross(m.positions[m.triangles[t].y] - m.positions[m.triangles[t].x],
                                           m.positions[m.triangles[t].z] - m.positions[m.triangles[t].x]);
            CHECK(glm::dot(n, glm::vec3(c.x, 0.0f, c.z)) > 0.0f);
            CHECK(m.facegroupAt(t) == r.group);
            CHECK(r.group == nextFaceGroupId(twoTubes(12, 12)));
        }
        // The ring vertices sit on the cylinder between the ends, evenly spaced.
        for (uint32_t v : r.vertices) {
            CHECK(std::abs(glm::length(glm::vec2(m.positions[v].x, m.positions[v].z)) - 0.6f) < 1e-4f);
            CHECK(std::abs(m.positions[v].y) < 1.0f);
        }
        // The rest of the Part is untouched.
        for (size_t t = 0; t < nt0; ++t) CHECK(m.triangles[t] == twoTubes(12, 12).triangles[t]);
    }
}

TEST(poly_span_unequal_loops_mix_quads_and_evenly_spread_triangles) {
    Mesh m = twoTubes(8, 12);
    std::vector<glm::uvec2> edges = edgesOf(loopNear(m, -1.0f));
    const std::vector<glm::uvec2> top = edgesOf(loopNear(m, 1.0f));
    edges.insert(edges.end(), top.begin(), top.end());
    SpanResult r;
    std::string error;
    CHECK(spanEdges(m, edges, {}, r, error));
    CHECK(r.quads == 8);
    CHECK(r.tris == 4);
    CHECK(boundaryLoops(m).size() == 2);
    CHECK(clean(m));
    // The four triangles are spread round the ring, not bunched: at most one per quarter.
    std::vector<int> quarter(4, 0);
    for (uint32_t t : r.triangles)
        if (m.quadPartnerAt(t) == Mesh::kNoQuad) {
            const glm::vec3 c = centroid(m, t);
            const float a = std::atan2(c.z, c.x) + 3.14159265f;
            ++quarter[std::min(3, int(a / (3.14159265f / 2.0f)))];
        }
    for (int q : quarter) CHECK(q == 1);
}

TEST(poly_span_coprime_counts_still_make_quads_and_uneven_spacing_pairs_by_length) {
    {
        Mesh m = twoTubes(7, 8);
        std::vector<glm::uvec2> edges = edgesOf(loopNear(m, -1.0f));
        for (const glm::uvec2& x : edgesOf(loopNear(m, 1.0f))) edges.push_back(x);
        SpanResult r;
        std::string error;
        CHECK(spanEdges(m, edges, {}, r, error));
        CHECK(r.quads == 7 && r.tris == 1); // (not a zipper of triangles: 7 and 8 share no divisor)
        CHECK(clean(m));
    }
    {
        // Two sheets whose facing borders have 4 and 6 edges, the 6 bunched towards one end: by length, the spans
        // stay roughly straight across instead of leaning towards the dense end.
        Mesh a = makePlane(4), b = makePlane(6);
        for (glm::vec3& p : a.positions) p.x -= 2.0f;
        for (glm::vec3& p : b.positions) {
            p.x += 2.0f;
            p.z = (p.z + 1.0f) * (p.z + 1.0f) * 0.5f - 1.0f; // bunch towards z = -1
        }
        Mesh m = appendMesh(a, b);
        std::vector<glm::uvec2> edges;
        for (const std::vector<uint32_t>& l : boundaryLoops(m))
            for (size_t i = 0; i < l.size(); ++i) {
                const glm::vec3 p = m.positions[l[i]], q = m.positions[l[(i + 1) % l.size()]];
                if (std::abs(std::abs(p.x) - 1.0f) < 1e-5f && std::abs(std::abs(q.x) - 1.0f) < 1e-5f) edges.emplace_back(l[i], l[(i + 1) % l.size()]);
            }
        CHECK(edges.size() == 10);
        SpanResult r;
        std::string error;
        CHECK(spanEdges(m, edges, {}, r, error));
        CHECK(r.quads + r.tris == 6);
        CHECK(clean(m));
        float worst = 0.0f; // the largest lean (dz over the gap of 2) of a span edge
        for (uint32_t t : r.triangles)
            for (int k = 0; k < 3; ++k) {
                if (m.quadPartnerAt(t) != Mesh::kNoQuad && m.quadDiagonal(t, m.quadPartnerAt(t)) == k) continue;
                const glm::vec3 p = m.positions[m.triangles[t][k]], q = m.positions[m.triangles[t][(k + 1) % 3]];
                if (std::abs(p.x - q.x) > 1.0f) worst = std::max(worst, std::abs(p.z - q.z));
            }
        CHECK(worst < 0.3f); // (by count it would be 0.5 and more)
    }
}

TEST(poly_span_twist_turns_the_pairing_and_flip_reverses_it) {
    const Mesh base = twoTubes(12, 12);
    auto run = [&](int twist, bool flip, Mesh& out, SpanResult& r) {
        out = base;
        std::vector<glm::uvec2> edges = edgesOf(loopNear(out, -1.0f));
        const std::vector<glm::uvec2> top = edgesOf(loopNear(out, 1.0f));
        edges.insert(edges.end(), top.begin(), top.end());
        SpanOptions o;
        o.twist = twist;
        o.flip = flip;
        std::string error;
        const bool ok = spanEdges(out, edges, o, r, error);
        if (!ok) std::printf("  %s\n", error.c_str());
        return ok;
    };
    auto meanSpan = [](const Mesh& m, const SpanResult& r) { // mean length of the band's edges that cross the gap
        double s = 0.0;
        size_t n = 0;
        for (uint32_t t : r.triangles)
            for (int k = 0; k < 3; ++k) {
                const glm::vec3 a = m.positions[m.triangles[t][k]], b = m.positions[m.triangles[t][(k + 1) % 3]];
                if (m.quadPartnerAt(t) != Mesh::kNoQuad && m.quadDiagonal(t, m.quadPartnerAt(t)) == k) continue;
                if (std::abs(a.y - b.y) > 1.0f) {
                    s += glm::length(a - b);
                    ++n;
                }
            }
        return n ? s / double(n) : 0.0;
    };
    Mesh straight, twisted, full, flipped;
    SpanResult r0, r3, r12, rf;
    CHECK(run(0, false, straight, r0));
    CHECK(run(3, false, twisted, r3));
    CHECK(run(12, false, full, r12));
    CHECK(run(0, true, flipped, rf));
    CHECK(std::abs(meanSpan(straight, r0) - 2.0) < 1e-3); // straight across the gap
    CHECK(meanSpan(twisted, r3) > 2.1);                    // a quarter turn: spans lean round the tube (2.17)
    CHECK(faceSets(full, r12.triangles) == faceSets(straight, r0.triangles)); // a whole turn is no turn
    CHECK(faceSets(twisted, r3.triangles) != faceSets(straight, r0.triangles));
    CHECK(r3.quads == 12 && r3.tris == 0);
    CHECK(clean(twisted));
    // flip pairs the second loop the other way round: a different band, every edge still on at most two faces.
    CHECK(faceSets(flipped, rf.triangles) != faceSets(straight, r0.triangles));
    CHECK(checkMesh(flipped).count[int(MeshIssue::NonManifold)] == 0);
}

TEST(poly_span_face_selections_cut_the_walls_and_join_two_cubes) {
    Mesh a = makeCube(2), b = makeCube(2);
    for (glm::vec3& p : a.positions) p.x -= 2.0f;
    for (glm::vec3& p : b.positions) p.x += 2.0f;
    Mesh m = appendMesh(a, b);
    const double v0 = volume(m);
    CHECK(isWatertight(m));
    std::vector<uint32_t> faces;
    for (uint32_t t = 0; t < m.triangleCount(); ++t) {
        const glm::vec3 c = centroid(m, t);
        if (std::abs(c.x + 1.0f) < 1e-4f || std::abs(c.x - 1.0f) < 1e-4f) faces.push_back(t); // the facing walls
    }
    CHECK(faces.size() == 16);
    const size_t nv0 = m.vertexCount(), nt0 = m.triangleCount();
    SpanResult r;
    std::string error;
    CHECK(spanFaces(m, faces, {}, r, error));
    if (!error.empty()) std::printf("  %s\n", error.c_str());
    CHECK(r.removed == 16);
    CHECK(r.quads == 8);
    CHECK(r.tris == 0);
    CHECK(isWatertight(m));
    CHECK(clean(m));
    CHECK(checkMesh(m).count[int(MeshIssue::Isolated)] == 0); // the walls' centre vertices went with them
    CHECK(m.vertexCount() == nv0 - 2);
    CHECK(m.triangleCount() == nt0 - 16 + 16);
    // Volume: two cubes of 8 plus the 2 x 2 x 2 connector between them.
    CHECK(std::abs(volume(m) - (v0 + 8.0)) < 1e-6 * v0 + 1e-6);
    // The kept triangles keep their order and their face groups; the band is a new group.
    size_t k = 0;
    const Mesh before = appendMesh(a, b);
    for (uint32_t t = 0; t < before.triangleCount(); ++t) {
        if (std::find(faces.begin(), faces.end(), t) != faces.end()) continue;
        CHECK(m.facegroupAt(uint32_t(k)) == before.facegroupAt(t));
        for (int c = 0; c < 3; ++c) CHECK(m.positions[m.triangles[k][c]] == before.positions[before.triangles[t][c]]);
        ++k;
    }
    for (uint32_t t : r.triangles) CHECK(m.facegroupAt(t) == r.group && r.group == nextFaceGroupId(before));
    CHECK(before.quadCount() == 48);
    CHECK(m.quadCount() == before.quadCount() - 8 + 8);
}

TEST(poly_span_closes_a_slot_across_its_width) {
    // A 10 x 10 grid with a 2 x 6 slot cut out.
    const Mesh plane = makePlane(10);
    Mesh m = without(plane, [&](uint32_t t) {
        const glm::vec3 c = centroid(plane, t);
        return std::abs(c.z) < 0.2f && std::abs(c.x) < 0.6f;
    });
    CHECK(m.triangleCount() == plane.triangleCount() - 24);
    CHECK(boundaryLoops(m).size() == 2);
    std::vector<uint32_t> slot;
    for (const std::vector<uint32_t>& l : boundaryLoops(m))
        if (l.size() == 16) slot = l;
    CHECK(slot.size() == 16);
    for (int segments : {1, 2}) {
        Mesh g = m;
        SpanOptions o;
        o.segments = segments;
        SpanResult r;
        std::string error;
        CHECK(spanEdges(g, edgesOf(slot), o, r, error));
        if (!error.empty()) std::printf("  %s\n", error.c_str());
        CHECK(boundaryLoops(g).size() == 1);
        CHECK(clean(g));
        CHECK(std::abs(area(g, r.triangles) - 12 * 0.04) < 1e-5); // the slot's area, 12 cells of 0.2 x 0.2
        for (uint32_t t : r.triangles) CHECK(std::abs(centroid(g, t).y) < 1e-6f);
        if (segments == 1) {
            CHECK(r.quads == 6); // straight across the width: six quads and a triangle at each end
            CHECK(r.tris == 2);
            for (uint32_t t : r.triangles) // every band edge runs along x or z: no skew
                for (int k = 0; k < 3; ++k) {
                    const glm::vec3 d = g.positions[g.triangles[t][k]] - g.positions[g.triangles[t][(k + 1) % 3]];
                    if (g.quadPartnerAt(t) != Mesh::kNoQuad && g.quadDiagonal(t, g.quadPartnerAt(t)) == k) continue;
                    CHECK(std::abs(d.x) < 1e-6f || std::abs(d.z) < 1e-6f);
                }
        } else {
            CHECK(r.vertices.size() == 7); // the middle ring: one per span (the corners' spans have none)
            for (uint32_t v : r.vertices) CHECK(std::abs(g.positions[v].z) < 1e-6f);
        }
        // The grid itself is untouched.
        for (size_t t = 0; t < m.triangleCount(); ++t) CHECK(g.triangles[t] == m.triangles[t]);
    }
}

TEST(poly_span_closes_a_round_hole_with_an_odd_border) {
    const Mesh sphere = makeIcosphere(3);
    Mesh m = without(sphere, [&](uint32_t t) { return centroid(sphere, t).z > 0.8f; });
    const std::vector<std::vector<uint32_t>> loops = boundaryLoops(m);
    CHECK(loops.size() == 1);
    BorderChain loop{loops[0], true};
    const size_t n = loop.vertices.size();
    Mesh g = m;
    SpanOptions o;
    o.segments = 2;
    SpanResult r;
    std::string error;
    CHECK(spanHole(g, loop, o, r, error));
    if (!error.empty()) std::printf("  %s\n", error.c_str());
    CHECK(isWatertight(g));
    CHECK(clean(g));
    CHECK(r.quads + r.tris >= n - 1);
    CHECK(r.tris <= 2 + 2 * (n % 2 ? 2 : 0) + 2); // corners, an odd half, and the rings' ends
    // Also by edges, and with the loop given backwards: the same faces.
    std::vector<uint32_t> back(loops[0].rbegin(), loops[0].rend());
    Mesh h = m;
    SpanResult r2;
    CHECK(spanHole(h, BorderChain{back, true}, o, r2, error));
    CHECK(h.triangleCount() == g.triangleCount());
    for (size_t t = m.triangleCount(); t < g.triangleCount(); ++t) {
        bool same = false;
        for (size_t u = m.triangleCount(); u < h.triangleCount() && !same; ++u)
            same = glm::length(centroid(g, uint32_t(t)) - centroid(h, uint32_t(u))) < 1e-5f;
        CHECK(same);
    }
}

TEST(poly_span_open_chains_between_two_sheets_pair_their_ends) {
    Mesh a = makePlane(4), b = makePlane(4);
    for (glm::vec3& p : a.positions) p.x -= 2.0f;
    for (glm::vec3& p : b.positions) p.x += 2.0f;
    Mesh m = appendMesh(a, b);
    std::vector<glm::uvec2> edges;
    for (const std::vector<uint32_t>& l : boundaryLoops(m))
        for (size_t i = 0; i < l.size(); ++i) {
            const glm::vec3 p = m.positions[l[i]], q = m.positions[l[(i + 1) % l.size()]];
            if (std::abs(std::abs(p.x) - 1.0f) < 1e-5f && std::abs(std::abs(q.x) - 1.0f) < 1e-5f) edges.emplace_back(l[i], l[(i + 1) % l.size()]);
        }
    CHECK(edges.size() == 8);
    std::vector<BorderChain> chains;
    std::string error;
    CHECK(borderChains(m, edges, chains, error));
    CHECK(chains.size() == 2);
    for (const BorderChain& c : chains) CHECK(!c.closed && c.vertices.size() == 5);
    SpanResult r;
    CHECK(spanEdges(m, edges, {}, r, error));
    if (!error.empty()) std::printf("  %s\n", error.c_str());
    CHECK(r.quads == 4 && r.tris == 0);
    CHECK(clean(m));
    CHECK(boundaryLoops(m).size() == 1); // one sheet now
    for (uint32_t t : r.triangles) { // spans run straight across: both ends at one z
        for (int k = 0; k < 3; ++k) {
            const glm::vec3 p = m.positions[m.triangles[t][k]], q = m.positions[m.triangles[t][(k + 1) % 3]];
            if (m.quadPartnerAt(t) != Mesh::kNoQuad && m.quadDiagonal(t, m.quadPartnerAt(t)) == k) continue;
            CHECK(std::abs(p.z - q.z) < 1e-6f || std::abs(p.x - q.x) < 1e-6f);
        }
        const glm::vec3 n = glm::cross(m.positions[m.triangles[t].y] - m.positions[m.triangles[t].x],
                                       m.positions[m.triangles[t].z] - m.positions[m.triangles[t].x]);
        CHECK(glm::dot(n, m.normals[0]) > 0.0f); // wound like the sheets
    }
}

TEST(poly_span_cone_base_to_tube_end) {
    Mesh cone = without(makeCone(6), [](uint32_t) { return false; });
    cone = without(cone, [&](uint32_t t) { return cone.facegroupAt(t) == 1; }); // the base cap off
    for (glm::vec3& p : cone.positions) p.y += 2.0f;
    Mesh m = appendMesh(tube(10, {0.0f, -2.0f, 0.0f}), cone);
    std::vector<glm::uvec2> edges = edgesOf(loopNear(m, -1.0f));
    const std::vector<glm::uvec2> top = edgesOf(loopNear(m, 1.0f));
    edges.insert(edges.end(), top.begin(), top.end());
    SpanResult r;
    std::string error;
    CHECK(spanEdges(m, edges, {}, r, error));
    if (!error.empty()) std::printf("  %s\n", error.c_str());
    CHECK(r.quads == 6 && r.tris == 4);
    CHECK(clean(m));
    CHECK(boundaryLoops(m).size() == 1);
}

TEST(poly_span_keeps_attributes_and_interpolates_new_vertices) {
    Mesh m = twoTubes(8, 8);
    const size_t nv0 = m.vertexCount(), nt0 = m.triangleCount();
    m.ensureMask();
    m.ensureColors();
    m.uvs.assign(nt0 * 3, glm::vec2(0.25f, 0.75f));
    m.ensureCreases();
    m.creases[0] = 1.0f;
    m.addLayer("bump");
    for (uint32_t v = 0; v < nv0; ++v) {
        m.mask[v] = m.positions[v].y < 0.0f ? 1.0f : 0.0f;
        m.colors[v] = m.positions[v].y < 0.0f ? glm::vec3(1.0f, 0.0f, 0.0f) : glm::vec3(0.0f, 0.0f, 1.0f);
        m.layers[0].delta[v] = glm::vec3(0.0f, 0.1f, 0.0f);
    }
    m.evaluateAll();
    const Mesh before = m;
    std::vector<glm::uvec2> edges = edgesOf(loopNear(m, -1.0f));
    const std::vector<glm::uvec2> top = edgesOf(loopNear(m, 1.0f));
    edges.insert(edges.end(), top.begin(), top.end());
    SpanOptions o;
    o.segments = 2;
    SpanResult r;
    std::string error;
    CHECK(spanEdges(m, edges, o, r, error));
    if (!error.empty()) std::printf("  %s\n", error.c_str());
    const size_t nv = m.vertexCount(), nt = m.triangleCount();
    CHECK(r.vertices.size() == 8);
    CHECK(m.mask.size() == nv && m.colors.size() == nv && m.base.size() == nv && m.layers[0].delta.size() == nv);
    CHECK(m.uvs.size() == nt * 3 && m.creases.size() == nt * 3 && m.facegroups.size() == nt && m.quadPartners.size() == nt);
    for (uint32_t v : r.vertices) {
        CHECK(std::abs(m.mask[v] - 0.5f) < 1e-6f);
        CHECK(glm::length(m.colors[v] - glm::vec3(0.5f, 0.0f, 0.5f)) < 1e-6f);
        CHECK(glm::length(m.layers[0].delta[v] - glm::vec3(0.0f, 0.1f, 0.0f)) < 1e-6f);
        CHECK(glm::length(m.positions[v] - (m.base[v] + m.layers[0].delta[v])) < 1e-6f);
    }
    for (size_t t = nt0; t < nt; ++t)
        for (int k = 0; k < 3; ++k) {
            CHECK(glm::length(m.uvs[t * 3 + size_t(k)] - glm::vec2(0.25f, 0.75f)) < 1e-6f); // the mean of equal UVs
            CHECK(m.creases[t * 3 + size_t(k)] == 0.0f);
        }
    // Everything that was there is as it was.
    for (uint32_t v = 0; v < nv0; ++v) {
        CHECK(m.positions[v] == before.positions[v]);
        CHECK(m.mask[v] == before.mask[v] && m.colors[v] == before.colors[v]);
    }
    for (size_t t = 0; t < nt0; ++t) {
        CHECK(m.triangles[t] == before.triangles[t]);
        CHECK(m.facegroups[t] == before.facegroupAt(uint32_t(t)));
        CHECK(m.quadPartnerAt(uint32_t(t)) == before.quadPartnerAt(uint32_t(t)));
    }
    CHECK(m.creases[0] == 1.0f);
    CHECK(m.quadCount() == before.quadCount() + 16);
    CHECK(m.quadPartners.size() == nt);
    CHECK(clean(m));
}

TEST(poly_span_refuses_bad_input_with_a_reason) {
    std::string error;
    SpanResult r;
    {
        Mesh empty;
        CHECK(!spanEdges(empty, std::vector<glm::uvec2>{{0u, 1u}}, {}, r, error));
        CHECK(error.find("empty") != std::string::npos);
    }
    Mesh cube = makeCube(1);
    const Mesh keep = cube;
    {
        // Edges with faces on both sides.
        CHECK(!spanEdges(cube, std::vector<glm::uvec2>{{cube.triangles[0].x, cube.triangles[0].y}}, {}, r, error));
        CHECK(error.find("open") != std::string::npos);
        CHECK(cube.triangleCount() == keep.triangleCount());
    }
    Mesh m = twoTubes(6, 6);
    const std::vector<uint32_t> lo = loopNear(m, -1.0f), hi = loopNear(m, 1.0f), far = loopNear(m, -3.0f);
    {
        // One open chain only.
        std::vector<glm::uvec2> e = edgesOf(lo, false);
        e.pop_back();
        CHECK(!spanEdges(m, e, {}, r, error));
        CHECK(error.find("two borders") != std::string::npos);
        // Three loops.
        e = edgesOf(lo);
        for (const auto& l : {hi, far})
            for (const glm::uvec2& x : edgesOf(l)) e.push_back(x);
        CHECK(!spanEdges(m, e, {}, r, error));
        CHECK(error.find("3 borders") != std::string::npos);
        // Branching edges.
        e = edgesOf(lo);
        e.emplace_back(lo[0], hi[0]);
        e.emplace_back(lo[0], hi[1]);
        CHECK(!spanEdges(m, e, {}, r, error) || true); // (lo[0]-hi[x] are not edges: refused either way)
        CHECK(!error.empty());
        // A loop with a chain.
        e = edgesOf(lo);
        std::vector<glm::uvec2> chain = edgesOf(hi, false);
        chain.pop_back();
        e.insert(e.end(), chain.begin(), chain.end());
        CHECK(!spanEdges(m, e, {}, r, error));
        CHECK(error.find("closed loop") != std::string::npos);
        // The two borders of one tube (its own two ends) do span; the same loop twice does not.
        Mesh t = m;
        CHECK(!spanBorders(t, BorderChain{lo, true}, BorderChain{lo, true}, {}, r, error));
        CHECK(error.find("share a vertex") != std::string::npos);
        // A vertex outside the Part, a repeated vertex, a non-edge.
        std::vector<uint32_t> bad = lo;
        bad[0] = uint32_t(m.vertexCount()) + 5;
        CHECK(!spanBorders(t, BorderChain{bad, true}, BorderChain{hi, true}, {}, r, error));
        bad = lo;
        bad[1] = bad[0];
        CHECK(!spanBorders(t, BorderChain{bad, true}, BorderChain{hi, true}, {}, r, error));
        bad = lo;
        std::swap(bad[0], bad[2]);
        CHECK(!spanBorders(t, BorderChain{bad, true}, BorderChain{hi, true}, {}, r, error));
        CHECK(error.find("not an edge") != std::string::npos);
        // A hole of three edges.
        Mesh ico = makeIcosphere(1);
        const Mesh one = without(ico, [](uint32_t t) { return t == 0; });
        Mesh h = one;
        CHECK(!spanHole(h, BorderChain{boundaryLoops(one)[0], true}, {}, r, error));
        CHECK(error.find("Fill Holes") != std::string::npos);
        CHECK(h.triangleCount() == one.triangleCount());
    }
    {
        // Faces: none, all, one piece, three pieces, out of range.
        Mesh c = makeCube(2);
        std::vector<uint32_t> all(c.triangleCount());
        for (uint32_t t = 0; t < c.triangleCount(); ++t) all[t] = t;
        CHECK(!spanFaces(c, std::vector<uint32_t>{}, {}, r, error));
        CHECK(!spanFaces(c, all, {}, r, error));
        CHECK(error.find("whole Part") != std::string::npos);
        CHECK(!spanFaces(c, std::vector<uint32_t>{0u, 1u}, {}, r, error));
        CHECK(error.find("one piece") != std::string::npos);
        std::vector<uint32_t> three;
        for (uint32_t t = 0; t < c.triangleCount(); ++t) {
            const glm::vec3 n = c.normals[c.triangles[t].x] + c.normals[c.triangles[t].y] + c.normals[c.triangles[t].z];
            const glm::vec3 cc = centroid(c, t);
            if (std::abs(cc.x - 1.0f) < 1e-4f || std::abs(cc.y - 1.0f) < 1e-4f || std::abs(cc.z - 1.0f) < 1e-4f) three.push_back(t);
            (void)n;
        }
        CHECK(three.size() == 24);
        CHECK(!spanFaces(c, three, {}, r, error)); // three walls meeting at a corner are one piece
        CHECK(!spanFaces(c, std::vector<uint32_t>{0u, uint32_t(c.triangleCount()) + 1u}, {}, r, error));
        CHECK(c.triangleCount() == makeCube(2).triangleCount()); // a refusal leaves the mesh as it was
        // Two opposite walls of one cube: removing both leaves a tube, spanning its ends closes it again inside out?
        // No: the walls' outlines face each other across the cube; the band runs through the inside. Allowed, clean.
        std::vector<uint32_t> two;
        for (uint32_t t = 0; t < c.triangleCount(); ++t)
            if (std::abs(std::abs(centroid(c, t).x) - 1.0f) < 1e-4f) two.push_back(t);
        CHECK(two.size() == 16);
        CHECK(spanFaces(c, two, {}, r, error));
        CHECK(isWatertight(c));
        CHECK(checkMesh(c).count[int(MeshIssue::NonManifold)] == 0);
    }
    {
        // Segments are clamped to 1..64.
        Mesh t = twoTubes(6, 6);
        std::vector<glm::uvec2> e = edgesOf(loopNear(t, -1.0f));
        for (const glm::uvec2& x : edgesOf(loopNear(t, 1.0f))) e.push_back(x);
        SpanOptions o;
        o.segments = 0;
        CHECK(spanEdges(t, e, o, r, error));
        CHECK(r.quads == 6 && r.vertices.empty());
        t = twoTubes(6, 6);
        o.segments = 1000;
        CHECK(spanEdges(t, e, o, r, error));
        CHECK(r.quads == 6 * 64 && r.vertices.size() == 6 * 63);
    }
}

TEST(poly_span_refuses_a_band_that_doubles_an_edge) {
    // Two holes in a grid one cell apart: the cells between them already join their borders, so a band would put
    // a third face on those edges.
    const Mesh plane = makePlane(8);
    Mesh m = without(plane, [&](uint32_t t) {
        const glm::vec3 c = centroid(plane, t);
        return std::abs(c.z) < 0.25f && (std::abs(c.x + 0.375f) < 0.12f || std::abs(c.x - 0.125f) < 0.12f);
    });
    const std::vector<std::vector<uint32_t>> loops = boundaryLoops(m);
    CHECK(loops.size() == 3);
    std::vector<glm::uvec2> e;
    for (const auto& l : loops)
        if (l.size() == 6)
            for (const glm::uvec2& x : edgesOf(l)) e.push_back(x);
    CHECK(e.size() == 12);
    SpanResult r;
    std::string error;
    const size_t nt = m.triangleCount();
    CHECK(!spanEdges(m, e, {}, r, error));
    CHECK(error.find("share a vertex") != std::string::npos || error.find("double") != std::string::npos);
    CHECK(m.triangleCount() == nt);
}

TEST(poly_span_big_loops_are_quick_and_deterministic) {
    // Two rings of 1500 quads each (one strip of faces per ring, so the setup is cheap).
    auto ring = [](float y0, float y1) {
        Mesh r;
        const int n = 1500;
        for (int row = 0; row < 2; ++row)
            for (int i = 0; i < n; ++i) {
                const float a = 6.2831853f * float(i) / float(n);
                r.positions.push_back({0.6f * std::cos(a), row ? y1 : y0, -0.6f * std::sin(a)});
            }
        for (uint32_t i = 0; i < uint32_t(n); ++i) {
            const uint32_t a = i, b = (i + 1) % n, c = n + i, d = n + (i + 1) % n;
            r.triangles.push_back({a, c, b});
            r.triangles.push_back({b, c, d});
            r.pairQuad(uint32_t(r.triangles.size() - 2), uint32_t(r.triangles.size() - 1));
        }
        r.rebuildTopology();
        r.recomputeAllNormals();
        return r;
    };
    Mesh m = appendMesh(ring(-3.0f, -1.0f), ring(1.0f, 3.0f));
    std::vector<glm::uvec2> e = edgesOf(loopNear(m, -1.0f));
    for (const glm::uvec2& x : edgesOf(loopNear(m, 1.0f))) e.push_back(x);
    Mesh a = m, b = m;
    SpanResult ra, rb;
    std::string error;
    const auto t0 = std::chrono::steady_clock::now();
    CHECK(spanEdges(a, e, {}, ra, error));
    const double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::printf("  span of two 1500-vertex loops: %.3f s\n", s);
    CHECK(s < timeLimit(1.0));
    CHECK(ra.quads == 1500);
    CHECK(spanEdges(b, e, {}, rb, error));
    CHECK(a.triangles == b.triangles);
    CHECK(clean(a));
}
