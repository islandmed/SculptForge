// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/PolyInset.h"

#include "core/FaceGroups.h"
#include "core/PaintLayer.h"
#include "core/Uv.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>

namespace sf {

namespace {

constexpr uint32_t kNone = ~0u;
// A border vertex's offset per unit of thickness is at most this (a corner sharper than about 29 degrees would
// otherwise shoot its new vertex far across the face).
constexpr float kMaxMiter = 4.0f;
// The thickness stays this far below the one that collapses a piece's shortest inner edge.
constexpr float kClampFraction = 0.95f;

struct UnionFind {
    std::vector<uint32_t> parent;
    explicit UnionFind(size_t n) : parent(n) { std::iota(parent.begin(), parent.end(), 0u); }
    uint32_t find(uint32_t i) {
        while (parent[i] != i) {
            parent[i] = parent[parent[i]];
            i = parent[i];
        }
        return i;
    }
    void unite(uint32_t a, uint32_t b) {
        a = find(a);
        b = find(b);
        if (a != b) parent[std::max(a, b)] = std::min(a, b);
    }
};

glm::vec3 safeNormalize(const glm::vec3& v, const glm::vec3& fallback) {
    const float l = glm::length(v);
    return l > 1e-12f && std::isfinite(l) ? v / l : fallback;
}

glm::vec3 triangleAreaNormal(const Mesh& m, uint32_t t) {
    const glm::uvec3& tri = m.triangles[t];
    return glm::cross(m.positions[tri.y] - m.positions[tri.x], m.positions[tri.z] - m.positions[tri.x]);
}

// What insetFaces and insetMaxThickness share: the selection, its pieces, their borders and the offset of every
// new vertex per unit of thickness.
struct Analysis {
    std::string error;
    std::vector<uint32_t> selected;        // the selected triangles, ascending (quad partners included)
    std::vector<uint8_t> isSelected;       // per triangle
    std::vector<uint32_t> twin;            // cornerTwins
    std::vector<uint32_t> region;          // per triangle: its piece (kNone = not selected)
    uint32_t regionCount = 0;
    std::vector<uint8_t> borderCorner;     // per corner: its edge is a border of its piece
    std::vector<uint32_t> fanOfCorner;     // per corner of a selected triangle: its fan (kNone: the vertex stays)
    struct Fan {
        uint32_t vertex = 0, region = 0;
        glm::vec3 normal{0.0f}; // of the selected faces around the vertex in this fan
        glm::vec3 miter{0.0f};  // the new vertex's offset per unit of thickness
        glm::vec3 nIn{0.0f}, nOut{0.0f}; // (sums of the border edges' inward normals while building)
        glm::vec3 dIn{0.0f}, dOut{0.0f}; // (sums of their unit directions, for the cusp fallback)
    };
    std::vector<Fan> fans;
    std::vector<float> regionMax;          // per piece: the thickness that collapses its shortest border edge
    std::vector<uint8_t> regionHasBorder;
};

bool analyse(const Mesh& mesh, std::span<const uint32_t> triangles, bool individual, Analysis& a) {
    const uint32_t nt = uint32_t(mesh.triangleCount());
    a.isSelected.assign(nt, 0);
    for (uint32_t t : triangles) {
        if (t >= nt) continue;
        a.isSelected[t] = 1;
        const uint32_t u = mesh.quadPartnerAt(t);
        if (u < nt) a.isSelected[u] = 1;
    }
    for (uint32_t t = 0; t < nt; ++t)
        if (a.isSelected[t]) a.selected.push_back(t);
    if (a.selected.empty()) {
        a.error = "Select faces to inset";
        return false;
    }
    a.twin = cornerTwins(mesh);

    // Pieces: faces joined across shared edges (region mode), or each polygon by itself.
    UnionFind pieces(nt);
    for (uint32_t t : a.selected) {
        const uint32_t u = mesh.quadPartnerAt(t);
        if (u < nt) pieces.unite(t, u);
        if (individual) continue;
        for (int k = 0; k < 3; ++k) {
            const uint32_t tw = a.twin[3 * t + k];
            if (tw != kNoCorner && a.isSelected[tw / 3]) pieces.unite(t, tw / 3);
        }
    }
    a.region.assign(nt, kNone);
    {
        std::vector<uint32_t> rootId(nt, kNone);
        for (uint32_t t : a.selected) {
            const uint32_t r = pieces.find(t);
            if (rootId[r] == kNone) rootId[r] = a.regionCount++;
            a.region[t] = rootId[r];
        }
    }

    // Border corners: the edge has no selected face of the same piece on its other side (an open or non-manifold
    // edge, an unselected neighbour, or in individual mode any neighbour but the quad's other half).
    a.borderCorner.assign(size_t(nt) * 3, 0);
    for (uint32_t t : a.selected)
        for (int k = 0; k < 3; ++k) {
            const uint32_t c = 3 * t + k;
            const glm::uvec3& tri = mesh.triangles[t];
            if (tri[k] == tri[(k + 1) % 3]) continue; // a degenerate edge has no length to inset from
            const uint32_t tw = a.twin[c];
            a.borderCorner[c] = tw == kNoCorner || a.region[tw / 3] != a.region[t];
        }

    // Fans: the selected corners at a vertex, joined across the piece's own edges. A fan with a border edge gets
    // a new vertex.
    UnionFind fanSets(size_t(nt) * 3);
    for (uint32_t t : a.selected)
        for (int k = 0; k < 3; ++k) {
            const uint32_t c = 3 * t + k;
            if (a.borderCorner[c]) continue;
            const uint32_t tw = a.twin[c];
            if (tw == kNoCorner) continue;
            fanSets.unite(c, cornerNext(tw)); // the twin's corner at this corner's vertex
        }
    a.fanOfCorner.assign(size_t(nt) * 3, kNone);
    std::vector<uint32_t> fanOfRoot(size_t(nt) * 3, kNone);
    auto fanFor = [&](uint32_t c) -> Analysis::Fan& {
        const uint32_t r = fanSets.find(c);
        if (fanOfRoot[r] == kNone) {
            fanOfRoot[r] = uint32_t(a.fans.size());
            Analysis::Fan f;
            f.vertex = mesh.triangles[c / 3][c % 3];
            f.region = a.region[c / 3];
            a.fans.push_back(f);
        }
        return a.fans[fanOfRoot[r]];
    };
    for (uint32_t t : a.selected)
        for (int k = 0; k < 3; ++k) {
            const uint32_t c = 3 * t + k;
            if (!a.borderCorner[c]) continue;
            const glm::vec3 pa = mesh.positions[mesh.triangles[t][k]], pb = mesh.positions[mesh.triangles[t][(k + 1) % 3]];
            const glm::vec3 d = safeNormalize(pb - pa, glm::vec3(0.0f));
            fanFor(c).dOut += d;             // the edge leaves this fan's vertex
            fanFor(cornerNext(c)).dIn += d;  // ... and arrives at this one's
        }
    // Every selected corner at a vertex with a new vertex points at that fan.
    for (uint32_t t : a.selected)
        for (int k = 0; k < 3; ++k) {
            const uint32_t c = 3 * t + k;
            const uint32_t f = fanOfRoot[fanSets.find(c)];
            a.fanOfCorner[c] = f;
            if (f != kNone) a.fans[f].normal += triangleAreaNormal(mesh, t);
        }
    // Region normals (the fallback for a fan of degenerate triangles).
    std::vector<glm::vec3> regionNormal(a.regionCount, glm::vec3(0.0f));
    for (uint32_t t : a.selected) regionNormal[a.region[t]] += triangleAreaNormal(mesh, t);
    for (Analysis::Fan& f : a.fans)
        f.normal = safeNormalize(f.normal, safeNormalize(regionNormal[f.region], glm::vec3(0.0f, 0.0f, 1.0f)));
    // Inward normals of the border edges in each fan's tangent plane, then the miter.
    for (uint32_t t : a.selected)
        for (int k = 0; k < 3; ++k) {
            const uint32_t c = 3 * t + k;
            if (!a.borderCorner[c]) continue;
            const glm::vec3 pa = mesh.positions[mesh.triangles[t][k]], pb = mesh.positions[mesh.triangles[t][(k + 1) % 3]];
            Analysis::Fan& fa = a.fans[a.fanOfCorner[c]];
            Analysis::Fan& fb = a.fans[a.fanOfCorner[cornerNext(c)]];
            fa.nOut += safeNormalize(glm::cross(fa.normal, pb - pa), glm::vec3(0.0f));
            fb.nIn += safeNormalize(glm::cross(fb.normal, pb - pa), glm::vec3(0.0f));
        }
    for (Analysis::Fan& f : a.fans) {
        const glm::vec3 nIn = safeNormalize(f.nIn, glm::vec3(0.0f)), nOut = safeNormalize(f.nOut, glm::vec3(0.0f));
        const glm::vec3 s = nIn + nOut;
        const float s2 = glm::dot(s, s);
        glm::vec3 m;
        if (s2 > 4.0f / (kMaxMiter * kMaxMiter)) { // |m| = 2 / |s| <= kMaxMiter
            m = s * (2.0f / s2);                   // m . nIn = m . nOut = 1: both new edges a thickness in
        } else {
            // A cusp (the border doubles back): offset along the bisector of the two edges, as far as allowed.
            const glm::vec3 bisector = safeNormalize(-safeNormalize(f.dIn, glm::vec3(0.0f)) + safeNormalize(f.dOut, glm::vec3(0.0f)),
                                                     safeNormalize(s, nOut));
            m = bisector * kMaxMiter;
        }
        f.miter = m;
    }

    // The thickness at which each piece's shortest border edge collapses: its inner edge is (b + t mb) - (a + t ma).
    a.regionMax.assign(a.regionCount, std::numeric_limits<float>::infinity());
    a.regionHasBorder.assign(a.regionCount, 0);
    for (uint32_t t : a.selected)
        for (int k = 0; k < 3; ++k) {
            const uint32_t c = 3 * t + k;
            if (!a.borderCorner[c]) continue;
            const uint32_t r = a.region[t];
            a.regionHasBorder[r] = 1;
            const glm::vec3 pa = mesh.positions[mesh.triangles[t][k]], pb = mesh.positions[mesh.triangles[t][(k + 1) % 3]];
            const float len = glm::length(pb - pa);
            const glm::vec3 d = pb - pa;
            const glm::vec3 ma = a.fans[a.fanOfCorner[c]].miter, mb = a.fans[a.fanOfCorner[cornerNext(c)]].miter;
            const float closing = glm::dot(ma - mb, d) / std::max(len, 1e-20f);
            if (closing > 1e-6f) a.regionMax[r] = std::min(a.regionMax[r], len / closing);
        }
    bool any = false;
    for (uint8_t b : a.regionHasBorder) any |= b != 0;
    if (!any) {
        a.error = "The selection has no border to inset from (every face is surrounded by selected faces)";
        return false;
    }
    return true;
}

// Barycentric coordinates of `p` in the plane of triangle (p0, p1, p2); false when the triangle is degenerate.
bool barycentric(const glm::vec3& p0, const glm::vec3& p1, const glm::vec3& p2, const glm::vec3& p, glm::vec3& out) {
    const glm::dvec3 e0 = glm::dvec3(p1 - p0), e1 = glm::dvec3(p2 - p0), q = glm::dvec3(p - p0);
    const double d00 = glm::dot(e0, e0), d01 = glm::dot(e0, e1), d11 = glm::dot(e1, e1), d20 = glm::dot(q, e0),
                 d21 = glm::dot(q, e1);
    const double denom = d00 * d11 - d01 * d01;
    if (!(std::abs(denom) > 1e-24 * std::max(1.0, d00 * d11))) return false;
    const double v = (d11 * d20 - d01 * d21) / denom, w = (d00 * d21 - d01 * d20) / denom;
    out = glm::vec3(float(1.0 - v - w), float(v), float(w));
    return std::isfinite(out.x) && std::isfinite(out.y) && std::isfinite(out.z);
}

} // namespace

float insetMaxThickness(const Mesh& mesh, std::span<const uint32_t> triangles, bool individual) {
    Analysis a;
    if (!analyse(mesh, triangles, individual, a)) return 0.0f;
    float m = std::numeric_limits<float>::infinity();
    for (uint32_t r = 0; r < a.regionCount; ++r)
        if (a.regionHasBorder[r]) m = std::min(m, a.regionMax[r]);
    return std::isfinite(m) ? m : 0.0f;
}

InsetResult insetFaces(const Mesh& mesh, std::span<const uint32_t> triangles, const InsetOptions& options) {
    InsetResult res;
    Analysis a;
    if (!analyse(mesh, triangles, options.individual, a)) {
        res.error = a.error;
        return res;
    }
    const uint32_t nv0 = uint32_t(mesh.vertexCount());

    // The thickness per piece, clamped so that no inner face turns inside out.
    std::vector<float> thickness(a.regionCount, options.thickness);
    for (uint32_t r = 0; r < a.regionCount; ++r) {
        const float limit = a.regionMax[r] * kClampFraction;
        if (options.thickness > 0.0f && std::isfinite(a.regionMax[r]) && options.thickness > limit) {
            thickness[r] = limit;
            res.clamped = true;
        }
        if (a.regionHasBorder[r]) res.thickness = std::max(res.thickness, thickness[r]);
        if (a.regionHasBorder[r]) ++res.regions;
    }

    // New vertices: one per fan, offset along its miter and normal.
    std::vector<uint32_t> newVertexOfFan(a.fans.size(), kNone);
    std::vector<glm::vec3> newPositions;
    std::vector<uint32_t> source; // the vertex each new one came from
    for (size_t f = 0; f < a.fans.size(); ++f) {
        const Analysis::Fan& fan = a.fans[f];
        newVertexOfFan[f] = nv0 + uint32_t(newPositions.size());
        newPositions.push_back(mesh.positions[fan.vertex] + fan.miter * thickness[fan.region] + fan.normal * options.depth);
        source.push_back(fan.vertex);
    }
    res.newVertices = uint32_t(newPositions.size());

    Mesh out = mesh;
    const Mesh before = mesh.paintLayers.empty() ? Mesh() : mesh; // (copyPaintLayers reads the old layers)
    const uint32_t nv = nv0 + res.newVertices;
    out.positions.insert(out.positions.end(), newPositions.begin(), newPositions.end());
    if (!out.layers.empty()) {
        out.base.insert(out.base.end(), newPositions.begin(), newPositions.end());
        for (SculptLayer& l : out.layers) l.delta.resize(nv, glm::vec3(0.0f));
    }
    if (out.hasMask())
        for (uint32_t v : source) out.mask.push_back(mesh.mask[v]);
    if (out.hasColors())
        for (uint32_t v : source) out.colors.push_back(mesh.colors[v]);
    if (!before.paintLayers.empty()) {
        std::vector<uint32_t> kept(nv);
        std::iota(kept.begin(), kept.begin() + nv0, 0u);
        std::copy(source.begin(), source.end(), kept.begin() + nv0);
        copyPaintLayers(before, out, kept);
    }

    // The depth moves the whole inner shape: the vertices inside a piece (every face around them is the piece's) go
    // along their normal in place, as the new border vertices do.
    if (options.depth != 0.0f) {
        std::vector<glm::vec3> inner(nv0, glm::vec3(0.0f));
        std::vector<uint8_t> isInner(nv0, 0);
        for (uint32_t t : a.selected)
            for (int k = 0; k < 3; ++k)
                if (a.fanOfCorner[3 * t + k] == kNone) {
                    const uint32_t v = mesh.triangles[t][k];
                    isInner[v] = 1;
                    inner[v] += triangleAreaNormal(mesh, t);
                }
        for (uint32_t v = 0; v < nv0; ++v) {
            if (!isInner[v]) continue;
            const glm::vec3 d = safeNormalize(inner[v], glm::vec3(0.0f)) * options.depth;
            out.positions[v] += d;
            if (!out.layers.empty()) out.base[v] += d;
        }
    }

    // The inner faces: their border corners move to the new vertices; UVs follow inside the original triangle.
    std::vector<glm::vec2> newUv; // per corner of the original triangles (only the moved ones are used)
    if (mesh.hasUvs()) newUv = mesh.uvs;
    for (uint32_t t : a.selected) {
        const glm::uvec3 tri = mesh.triangles[t];
        for (int k = 0; k < 3; ++k) {
            const uint32_t c = 3 * t + k, f = a.fanOfCorner[c];
            if (f == kNone) continue;
            const uint32_t nvtx = newVertexOfFan[f];
            out.triangles[t][k] = nvtx;
            if (mesh.hasUvs()) {
                glm::vec3 b;
                if (barycentric(mesh.positions[tri.x], mesh.positions[tri.y], mesh.positions[tri.z], newPositions[nvtx - nv0], b))
                    newUv[c] = b.x * mesh.uvs[3 * t] + b.y * mesh.uvs[3 * t + 1] + b.z * mesh.uvs[3 * t + 2];
                out.uvs[c] = newUv[c];
            }
            if (mesh.hasUvFlags()) out.uvFlags[c] &= uint8_t(~kUvPin); // a derived UV is no pin
            if (a.borderCorner[c]) { // the edge keeps its seam and crease: they go with it to the rim
                if (mesh.hasUvFlags()) out.uvFlags[c] &= uint8_t(~kUvSeam);
                if (mesh.hasCreases()) out.creases[c] = 0.0f;
            }
        }
    }

    // The rim: one quad per border edge (a, b, b', a'), facing the way its face does.
    const bool groups = out.hasFaceGroups() || options.rimGroup;
    const uint32_t rimGroupId = options.rimGroup ? nextFaceGroupId(mesh) : 0;
    if (groups) out.ensureFaceGroups();
    out.ensureQuadPartners();
    for (uint32_t t : a.selected) {
        const glm::uvec3 tri = mesh.triangles[t];
        for (int k = 0; k < 3; ++k) {
            const uint32_t c = 3 * t + k;
            if (!a.borderCorner[c]) continue;
            const uint32_t n = cornerNext(c);
            const uint32_t va = tri[k], vb = tri[(k + 1) % 3];
            const uint32_t va2 = newVertexOfFan[a.fanOfCorner[c]], vb2 = newVertexOfFan[a.fanOfCorner[n]];
            const uint32_t t1 = uint32_t(out.triangles.size()), t2 = t1 + 1;
            out.triangles.emplace_back(va, vb, vb2);
            out.triangles.emplace_back(va, vb2, va2);
            out.pairQuad(t1, t2);
            res.rim.push_back(t1);
            res.rim.push_back(t2);
            if (groups) {
                const uint32_t g = options.rimGroup ? rimGroupId : mesh.facegroupAt(t);
                out.facegroups.push_back(g);
                out.facegroups.push_back(g);
            }
            if (out.hasHidden()) {
                out.hidden.push_back(0);
                out.hidden.push_back(0);
            }
            if (mesh.hasUvs()) {
                const glm::vec2 ua = mesh.uvs[c], ub = mesh.uvs[n], ua2 = newUv[c], ub2 = newUv[n];
                out.uvs.insert(out.uvs.end(), {ua, ub, ub2, ua, ub2, ua2});
            }
            if (mesh.hasUvFlags()) {
                const uint8_t fa = mesh.uvFlags[c], fb = mesh.uvFlags[n];
                // corner a->b: the old edge's seam, a's pin; corner at b: b's pin; the new corners nothing.
                out.uvFlags.insert(out.uvFlags.end(), {fa, uint8_t(fb & kUvPin), 0, uint8_t(fa & kUvPin), 0, 0});
            }
            if (mesh.hasCreases()) out.creases.insert(out.creases.end(), {mesh.creases[c], 0.0f, 0.0f, 0.0f, 0.0f, 0.0f});
        }
    }
    out.quadPartners.resize(out.triangles.size(), Mesh::kNoQuad);
    out.rebuildTopology();
    out.recomputeAllNormals();
    out.markAllDirty();
    out.facesDirty = true;

    res.ok = true;
    res.mesh = std::move(out);
    res.selection = std::move(a.selected);
    return res;
}

} // namespace sf
