// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/PolyInset.h"

#include "core/FaceGroups.h"
#include "core/PaintLayer.h"
#include "core/PolyOps.h"
#include "core/Uv.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <numeric>

namespace sf {

namespace {

constexpr uint32_t kNone = ~0u;
// A border vertex's offset per unit of thickness is at most this (a corner sharper than about 29 degrees would
// otherwise shoot its new vertex far across the face).
constexpr float kMaxMiter = 4.0f;
// The thickness stays this far below the one that collapses a piece.
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

bool finite(const glm::vec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }

glm::vec3 triangleAreaNormal(const Mesh& m, uint32_t t) {
    const glm::uvec3& tri = m.triangles[t];
    return glm::cross(m.positions[tri.y] - m.positions[tri.x], m.positions[tri.z] - m.positions[tri.x]);
}

// What insetFaces and insetMaxThickness share: the selection, its pieces, their borders as loops and the fans that
// get a new vertex.
struct Analysis {
    std::string error;
    std::vector<uint32_t> selected;    // the selected triangles, ascending (quad partners included)
    std::vector<uint8_t> isSelected;   // per triangle
    std::vector<uint32_t> twin;        // cornerTwins
    std::vector<uint32_t> region;      // per triangle: its piece (kNone = not selected)
    uint32_t regionCount = 0;
    std::vector<uint8_t> borderCorner; // per corner: its edge is a border of its piece
    std::vector<uint32_t> fanOfCorner; // per corner of a selected triangle: its fan (kNone: the vertex stays)
    struct Fan {
        uint32_t vertex = 0, region = 0;
        glm::vec3 normal{0.0f};           // of the selected faces around the vertex in this fan
        std::vector<uint32_t> outCorners; // the border edges leaving this fan's vertex (their corners)
    };
    std::vector<Fan> fans;
    // A border loop of a piece: edge i is the border corner `corners[i]`, from the fan of that corner to the fan of
    // the next corner of its triangle; closed when the last edge comes back to the first fan.
    struct Loop {
        uint32_t region = 0;
        std::vector<uint32_t> corners;
        bool closed = true;
    };
    std::vector<Loop> loops;
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

    // Border corners: the edge has no face of the same piece on its other side (an open or non-manifold edge, an
    // unselected neighbour, or in individual mode any neighbour but the quad's other half).
    a.borderCorner.assign(size_t(nt) * 3, 0);
    for (uint32_t t : a.selected)
        for (int k = 0; k < 3; ++k) {
            const uint32_t c = 3 * t + k;
            const glm::uvec3& tri = mesh.triangles[t];
            if (tri[k] == tri[(k + 1) % 3]) continue; // a degenerate edge has no length to inset from
            const uint32_t tw = a.twin[c];
            a.borderCorner[c] = tw == kNoCorner || a.region[tw / 3] != a.region[t];
        }

    // Fans: the selected corners at a vertex, joined across the piece's own edges. A fan with a border edge gets a
    // new vertex.
    UnionFind fanSets(size_t(nt) * 3);
    for (uint32_t t : a.selected)
        for (int k = 0; k < 3; ++k) {
            const uint32_t c = 3 * t + k;
            if (a.borderCorner[c]) continue;
            const uint32_t tw = a.twin[c];
            if (tw == kNoCorner) continue;
            fanSets.unite(c, cornerNext(tw)); // the twin's corner at this corner's vertex
        }
    std::vector<uint32_t> fanOfRoot(size_t(nt) * 3, kNone);
    auto fanFor = [&](uint32_t c) -> uint32_t {
        const uint32_t r = fanSets.find(c);
        if (fanOfRoot[r] == kNone) {
            fanOfRoot[r] = uint32_t(a.fans.size());
            Analysis::Fan f;
            f.vertex = mesh.triangles[c / 3][c % 3];
            f.region = a.region[c / 3];
            a.fans.push_back(std::move(f));
        }
        return fanOfRoot[r];
    };
    for (uint32_t t : a.selected)
        for (int k = 0; k < 3; ++k) {
            const uint32_t c = 3 * t + k;
            if (!a.borderCorner[c]) continue;
            a.fans[fanFor(c)].outCorners.push_back(c);
            fanFor(cornerNext(c));
        }
    a.fanOfCorner.assign(size_t(nt) * 3, kNone);
    for (uint32_t t : a.selected)
        for (int k = 0; k < 3; ++k) {
            const uint32_t c = 3 * t + k;
            const uint32_t f = fanOfRoot[fanSets.find(c)];
            a.fanOfCorner[c] = f;
            if (f != kNone) a.fans[f].normal += triangleAreaNormal(mesh, t);
        }
    std::vector<glm::vec3> regionNormal(a.regionCount, glm::vec3(0.0f));
    for (uint32_t t : a.selected) regionNormal[a.region[t]] += triangleAreaNormal(mesh, t);
    for (Analysis::Fan& f : a.fans)
        f.normal = safeNormalize(f.normal, safeNormalize(regionNormal[f.region], glm::vec3(0.0f, 0.0f, 1.0f)));

    // Border loops: follow each border edge to the fan it arrives at and on along an edge leaving that fan.
    std::vector<uint8_t> visited(size_t(nt) * 3, 0);
    a.regionHasBorder.assign(a.regionCount, 0);
    for (uint32_t t : a.selected)
        for (int k = 0; k < 3; ++k) {
            const uint32_t c0 = 3 * t + k;
            if (!a.borderCorner[c0] || visited[c0]) continue;
            Analysis::Loop loop;
            loop.region = a.region[t];
            uint32_t c = c0;
            while (true) {
                visited[c] = 1;
                loop.corners.push_back(c);
                const Analysis::Fan& end = a.fans[a.fanOfCorner[cornerNext(c)]];
                uint32_t next = kNone;
                for (uint32_t oc : end.outCorners)
                    if (oc == c0 || !visited[oc]) {
                        next = oc;
                        break;
                    }
                if (next == kNone) {
                    loop.closed = false;
                    break;
                }
                if (next == c0) break;
                c = next;
            }
            a.regionHasBorder[loop.region] = 1;
            a.loops.push_back(std::move(loop));
        }
    bool any = false;
    for (uint8_t b : a.regionHasBorder) any |= b != 0;
    if (!any) {
        a.error = "The selection has no border to inset from (every face is surrounded by selected faces)";
        return false;
    }
    return true;
}

// A loop's geometry: its nodes (the fans its edges start from) and edges.
struct LoopGeometry {
    std::vector<uint32_t> fan;   // per node
    std::vector<glm::vec3> P, F; // per node: the vertex, the fan normal
    std::vector<glm::vec3> D;    // per edge: unit direction (node i -> node i + 1)
    std::vector<float> L;        // per edge: length
    size_t nodes() const { return fan.size(); }
    size_t edges() const { return D.size(); }
    uint32_t endNode(size_t e) const { return uint32_t((e + 1) % nodes()); }
};

LoopGeometry loopGeometry(const Mesh& mesh, const Analysis& a, const Analysis::Loop& loop) {
    LoopGeometry g;
    auto addNode = [&](uint32_t fan) {
        g.fan.push_back(fan);
        g.P.push_back(mesh.positions[a.fans[fan].vertex]);
        g.F.push_back(a.fans[fan].normal);
    };
    for (uint32_t c : loop.corners) addNode(a.fanOfCorner[c]);
    if (!loop.closed) addNode(a.fanOfCorner[cornerNext(loop.corners.back())]); // the last edge arrives at a node of its own
    for (size_t e = 0; e < loop.corners.size(); ++e) {
        const glm::vec3 d = g.P[(e + 1) % g.nodes()] - g.P[e];
        g.L.push_back(glm::length(d));
        g.D.push_back(safeNormalize(d, glm::vec3(0.0f)));
    }
    return g;
}

// Where the offset lines of two border edges meet: the edges' lines moved `t` inward (n . (x - P) = t, in the plane
// through `C` with normal `F`), solved around `C`, the centre of the node(s) between them. Collinear edges give the
// plain offset; a cusp (the border doubles back) the bisector, as far as the miter cap allows.
glm::vec3 meetOffsetLines(const glm::vec3& Dp, const glm::vec3& Pp, const glm::vec3& Dk, const glm::vec3& Pk, float t,
                          const glm::vec3& C, const glm::vec3& F) {
    const glm::vec3 n1 = safeNormalize(glm::cross(F, Dp), glm::vec3(0.0f)), n2 = safeNormalize(glm::cross(F, Dk), glm::vec3(0.0f));
    const float t1 = t + glm::dot(n1, Pp - C), t2 = t + glm::dot(n2, Pk - C);
    const float c = glm::dot(n1, n2), den = 1.0f - c * c;
    glm::vec3 y;
    if (den > 1e-6f) {
        y = ((t1 - c * t2) / den) * n1 + ((t2 - c * t1) / den) * n2;
    } else if (c > 0.0f) {
        y = (0.5f * (t1 + t2) / (1.0f + c)) * (n1 + n2); // collinear: the plain offset
    } else {
        glm::vec3 b = safeNormalize(n1 + n2, glm::vec3(0.0f));
        if (glm::dot(b, b) < 0.5f) b = safeNormalize(Dk - Dp, n2);
        y = b * (kMaxMiter * t);
    }
    const float lim = kMaxMiter * std::max({std::abs(t), std::abs(t1), std::abs(t2)});
    const float ly = glm::length(y);
    if (ly > lim && ly > 0.0f) y *= lim / ly;
    return C + y;
}

// The offset of a loop by `t`: a position and normal per node. Nodes whose edge collapses merge, so the new border
// never folds back on itself (the vertices beside a corner flow into it, as on a straight skeleton). A closed loop
// that collapses to fewer than three edges returns false. `group[i]` is the node that node i merged into.
struct LoopOffset {
    std::vector<uint32_t> group;
    std::vector<glm::vec3> pos, normal; // per group root
};

bool offsetLoop(const LoopGeometry& g, bool closed, float t, LoopOffset& out) {
    const size_t n = g.nodes(), ne = g.edges();
    out.group.assign(n, 0);
    out.pos.assign(n, glm::vec3(0.0f));
    out.normal.assign(n, glm::vec3(0.0f));
    if (!closed) { // an open chain (non-manifold input): plain miters, no merging
        for (size_t i = 0; i < n; ++i) {
            out.group[i] = uint32_t(i);
            out.normal[i] = g.F[i];
            glm::vec3 nIn(0.0f), nOut(0.0f);
            if (i > 0) nIn = safeNormalize(glm::cross(g.F[i], g.D[i - 1]), glm::vec3(0.0f));
            if (i < ne) nOut = safeNormalize(glm::cross(g.F[i], g.D[i]), glm::vec3(0.0f));
            const glm::vec3 s = (i > 0 && i < ne) ? nIn + nOut : (i > 0 ? nIn : nOut) * 2.0f;
            const float s2 = glm::dot(s, s);
            const glm::vec3 m = s2 > 4.0f / (kMaxMiter * kMaxMiter) ? s * (2.0f / s2) : safeNormalize(s, nOut) * kMaxMiter;
            out.pos[i] = g.P[i] + m * t;
        }
        return true;
    }
    UnionFind groups(n);
    std::vector<uint8_t> alive(ne, 1);
    std::vector<uint32_t> S, count(n);
    std::vector<glm::vec3> x, C, F;
    for (size_t pass = 0; pass <= ne; ++pass) {
        S.clear();
        for (size_t e = 0; e < ne; ++e)
            if (alive[e]) S.push_back(uint32_t(e));
        if (S.size() < 3) return false;
        // Each group's centre and normal.
        C.assign(n, glm::vec3(0.0f));
        F.assign(n, glm::vec3(0.0f));
        std::fill(count.begin(), count.end(), 0u);
        for (size_t i = 0; i < n; ++i) {
            const uint32_t r = groups.find(uint32_t(i));
            C[r] += g.P[i];
            F[r] += g.F[i];
            ++count[r];
        }
        // The vertex between consecutive surviving edges.
        x.assign(S.size(), glm::vec3(0.0f));
        for (size_t j = 0; j < S.size(); ++j) {
            const uint32_t p = S[(j + S.size() - 1) % S.size()], k = S[j];
            const uint32_t r = groups.find(k); // the start node of edge k (== the end node of edge p)
            const glm::vec3 c = C[r] / float(count[r]);
            const glm::vec3 f = safeNormalize(F[r], g.F[k]);
            x[j] = meetOffsetLines(g.D[p], g.P[p], g.D[k], g.P[k], t, c, f);
            if (!finite(x[j])) x[j] = c;
            out.pos[r] = x[j];
            out.normal[r] = f;
        }
        // Edges whose offset has no length left collapse: their ends merge.
        bool merged = false;
        for (size_t j = 0; j < S.size(); ++j) {
            const uint32_t k = S[j];
            const float len = glm::dot(x[(j + 1) % S.size()] - x[j], g.D[k]);
            if (len > 1e-6f * std::max(g.L[k], 1e-30f)) continue;
            alive[k] = 0;
            groups.unite(k, g.endNode(k));
            merged = true;
        }
        if (!merged) break;
    }
    for (size_t i = 0; i < n; ++i) out.group[i] = groups.find(uint32_t(i));
    return true;
}

// The loops' geometry and which loops each piece has.
struct Loops {
    std::vector<LoopGeometry> geos;
    std::vector<uint8_t> closed;
    std::vector<std::vector<uint32_t>> ofPiece;
};

Loops buildLoops(const Mesh& mesh, const Analysis& a) {
    Loops l;
    l.ofPiece.resize(a.regionCount);
    for (size_t i = 0; i < a.loops.size(); ++i) {
        l.geos.push_back(loopGeometry(mesh, a, a.loops[i]));
        l.closed.push_back(a.loops[i].closed);
        l.ofPiece[a.loops[i].region].push_back(uint32_t(i));
    }
    return l;
}

// The offset of every loop of a piece at `t`, or false when a closed loop collapses.
bool offsetPiece(const Loops& loops, uint32_t piece, float t, std::vector<LoopOffset>& out) {
    for (uint32_t l : loops.ofPiece[piece])
        if (!offsetLoop(loops.geos[l], loops.closed[l] != 0, t, out[l])) return false;
    return true;
}

// The largest fraction of `t` (bisected) at which the piece still holds together.
float holdingFraction(const Loops& loops, uint32_t piece, float t, std::vector<LoopOffset>& scratch) {
    float lo = 0.0f, hi = 1.0f;
    for (int it = 0; it < 20; ++it) {
        const float mid = 0.5f * (lo + hi);
        if (offsetPiece(loops, piece, t * mid, scratch)) lo = mid;
        else hi = mid;
    }
    return lo;
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

// Interior vertices of the pieces (every face around them is the piece's) follow the border: their displacement
// is the harmonic interpolation of the new border's over the polygon edges of the inner faces (Jacobi-preconditioned
// conjugate gradients: a few hundred steps on a large cage, none on a single face), then kept in each vertex's
// tangent plane so the inner shape stays on the surface.
void relaxInterior(const Mesh& mesh, const Analysis& a, const std::vector<uint32_t>& vertexOfCorner, uint32_t nv0, std::vector<glm::vec3>& positions, std::vector<glm::vec3>& interiorNormal, std::vector<uint8_t>& isInterior) {
    isInterior.assign(nv0, 0);
    interiorNormal.assign(nv0, glm::vec3(0.0f));
    for (uint32_t t : a.selected)
        for (int k = 0; k < 3; ++k)
            if (a.fanOfCorner[3 * t + k] == kNone) {
                const uint32_t v = mesh.triangles[t][k];
                isInterior[v] = 1;
                interiorNormal[v] += triangleAreaNormal(mesh, t);
            }
    std::vector<uint32_t> unknown(nv0, kNone), verts;
    for (uint32_t v = 0; v < nv0; ++v)
        if (isInterior[v]) {
            unknown[v] = uint32_t(verts.size());
            verts.push_back(v);
        }
    if (verts.empty()) return;
    for (uint32_t v : verts) interiorNormal[v] = safeNormalize(interiorNormal[v], glm::vec3(0.0f));
    // The edges of the inner faces (a quad's diagonal is no edge), the known displacements on the right-hand side.
    const size_t m = verts.size();
    std::vector<std::vector<uint32_t>> adj(m);
    std::vector<glm::vec3> b(m, glm::vec3(0.0f));
    std::vector<float> degree(m, 0.0f);
    for (uint32_t t : a.selected) {
        const uint32_t u = mesh.quadPartnerAt(t);
        const int diag = u < mesh.triangleCount() ? mesh.quadDiagonal(t, u) : -1;
        const glm::uvec3& tri = mesh.triangles[t];
        for (int k = 0; k < 3; ++k) {
            if (k == diag) continue;
            const uint32_t va = vertexOfCorner[3 * t + k], vb = vertexOfCorner[cornerNext(3 * t + k)];
            if (va == vb) continue;
            for (int side = 0; side < 2; ++side) {
                const uint32_t p = side ? vb : va, q = side ? va : vb;
                const uint32_t qOriginal = side ? tri[k] : tri[(k + 1) % 3]; // (a merged border vertex: this edge's own end)
                if (p >= nv0 || unknown[p] == kNone) continue;
                const uint32_t i = unknown[p];
                degree[i] += 1.0f;
                if (q < nv0 && unknown[q] != kNone) adj[i].push_back(unknown[q]);
                else if (q >= nv0) b[i] += positions[q] - mesh.positions[qOriginal];
            }
        }
    }
    // CG on A d = b, A = degree - adjacency (symmetric positive definite while every unknown reaches the border).
    std::vector<glm::vec3> d(m, glm::vec3(0.0f)), r = b, z(m), p(m), Ap(m);
    auto applyA = [&](const std::vector<glm::vec3>& in, std::vector<glm::vec3>& out) {
        for (size_t i = 0; i < m; ++i) {
            glm::vec3 s = in[i] * degree[i];
            for (uint32_t j : adj[i]) s -= in[j];
            out[i] = s;
        }
    };
    auto dot = [&](const std::vector<glm::vec3>& x, const std::vector<glm::vec3>& y) {
        double s = 0.0;
        for (size_t i = 0; i < m; ++i) s += glm::dot(glm::dvec3(x[i]), glm::dvec3(y[i]));
        return s;
    };
    auto precondition = [&] {
        for (size_t i = 0; i < m; ++i) z[i] = degree[i] > 0.0f ? r[i] / degree[i] : glm::vec3(0.0f);
    };
    const double b2 = dot(b, b);
    if (!(b2 > 0.0)) return;
    precondition();
    p = z;
    double rz = dot(r, z);
    const int maxIterations = int(std::min<size_t>(4000, m + 10));
    for (int it = 0; it < maxIterations && rz > 0.0; ++it) {
        applyA(p, Ap);
        const double pAp = dot(p, Ap);
        if (!(pAp > 0.0)) break;
        const float alpha = float(rz / pAp);
        for (size_t i = 0; i < m; ++i) {
            d[i] += alpha * p[i];
            r[i] -= alpha * Ap[i];
        }
        if (dot(r, r) < 1e-14 * b2) break;
        precondition();
        const double rzNew = dot(r, z);
        const float beta = float(rzNew / rz);
        rz = rzNew;
        for (size_t i = 0; i < m; ++i) p[i] = z[i] + beta * p[i];
    }
    for (size_t i = 0; i < m; ++i) {
        const uint32_t v = verts[i];
        glm::vec3 delta = d[i];
        if (!finite(delta)) continue;
        delta -= interiorNormal[v] * glm::dot(delta, interiorNormal[v]); // stays on the surface
        positions[v] += delta;
    }
}

} // namespace

float insetMaxThickness(const Mesh& mesh, std::span<const uint32_t> triangles, bool individual) {
    Analysis a;
    if (!analyse(mesh, triangles, individual, a)) return 0.0f;
    const Loops loops = buildLoops(mesh, a);
    std::vector<LoopOffset> scratch(loops.geos.size());
    float best = std::numeric_limits<float>::infinity();
    for (uint32_t r = 0; r < a.regionCount; ++r) {
        if (loops.ofPiece[r].empty()) continue;
        AABB box;
        for (uint32_t l : loops.ofPiece[r])
            for (const glm::vec3& p : loops.geos[l].P) box.expand(p);
        const float span = glm::length(box.extent());
        if (!(span > 0.0f)) continue;
        best = std::min(best, span * holdingFraction(loops, r, span, scratch));
    }
    return std::isfinite(best) ? best : 0.0f;
}

InsetResult insetFaces(const Mesh& mesh, std::span<const uint32_t> triangles, const InsetOptions& options) {
    InsetResult res;
    Analysis a;
    if (!analyse(mesh, triangles, options.individual, a)) {
        res.error = a.error;
        return res;
    }
    const uint32_t nv0 = uint32_t(mesh.vertexCount()), nt0 = uint32_t(mesh.triangleCount());
    const Loops loops = buildLoops(mesh, a);

    // The border of every piece, offset by the thickness; a piece that cannot take it gets as much as it holds.
    std::vector<LoopOffset> offsets(loops.geos.size());
    for (uint32_t r = 0; r < a.regionCount; ++r) {
        if (loops.ofPiece[r].empty()) continue;
        float t = options.thickness;
        if (!offsetPiece(loops, r, t, offsets)) {
            t *= kClampFraction * holdingFraction(loops, r, t, offsets);
            offsetPiece(loops, r, t, offsets);
            res.clamped = true;
        }
        res.thickness = std::max(res.thickness, t);
        ++res.regions;
    }

    // New vertices: one per group of merged loop nodes (a fan is a node of one loop).
    std::vector<uint32_t> newVertexOfFan(a.fans.size(), kNone);
    std::vector<glm::vec3> newPositions, newNormals;
    std::vector<uint32_t> source; // the vertex each new one came from
    for (size_t l = 0; l < loops.geos.size(); ++l) {
        const LoopGeometry& g = loops.geos[l];
        const LoopOffset& o = offsets[l];
        std::vector<uint32_t> vertexOfRoot(g.nodes(), kNone);
        for (size_t i = 0; i < g.nodes(); ++i) {
            const uint32_t root = o.group[i];
            if (vertexOfRoot[root] == kNone) {
                vertexOfRoot[root] = nv0 + uint32_t(newPositions.size());
                newPositions.push_back(o.pos[root]);
                newNormals.push_back(o.normal[root]);
                source.push_back(a.fans[g.fan[i]].vertex);
            }
            if (newVertexOfFan[g.fan[i]] == kNone) newVertexOfFan[g.fan[i]] = vertexOfRoot[root];
        }
    }
    for (uint32_t f = 0; f < a.fans.size(); ++f) // (a fan no loop reached, non-manifold leftovers: stays put)
        if (newVertexOfFan[f] == kNone) {
            newVertexOfFan[f] = nv0 + uint32_t(newPositions.size());
            newPositions.push_back(mesh.positions[a.fans[f].vertex]);
            newNormals.push_back(a.fans[f].normal);
            source.push_back(a.fans[f].vertex);
        }
    res.newVertices = uint32_t(newPositions.size());

    // The inner faces' corners after the move, the interior following the border, then the depth.
    std::vector<uint32_t> vertexOfCorner(size_t(nt0) * 3, kNone);
    for (uint32_t t : a.selected)
        for (int k = 0; k < 3; ++k) {
            const uint32_t c = 3 * t + k, f = a.fanOfCorner[c];
            vertexOfCorner[c] = f == kNone ? mesh.triangles[t][k] : newVertexOfFan[f];
        }
    std::vector<glm::vec3> positions = mesh.positions;
    positions.insert(positions.end(), newPositions.begin(), newPositions.end());
    std::vector<glm::vec3> interiorNormal;
    std::vector<uint8_t> isInterior;
    relaxInterior(mesh, a, vertexOfCorner, nv0, positions, interiorNormal, isInterior);
    const std::vector<glm::vec3> flatPositions = positions; // (the UVs are derived before the depth)
    if (options.depth != 0.0f) {
        for (uint32_t v = 0; v < nv0; ++v)
            if (isInterior[v]) positions[v] += interiorNormal[v] * options.depth;
        for (uint32_t i = 0; i < res.newVertices; ++i) positions[nv0 + i] += newNormals[i] * options.depth;
    }

    // The mesh: vertices first.
    Mesh out = mesh;
    const Mesh before = mesh.paintLayers.empty() ? Mesh() : mesh; // (copyPaintLayers reads the old layers)
    const uint32_t nv = nv0 + res.newVertices;
    out.positions = positions;
    if (!out.layers.empty()) {
        for (uint32_t v = 0; v < nv0; ++v) out.base[v] += positions[v] - mesh.positions[v];
        out.base.insert(out.base.end(), positions.begin() + nv0, positions.end());
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

    // Triangles: the inner faces with their corners moved (UVs follow inside the original triangle; a face whose
    // corners merged is gone), everything else as it was, then the rim.
    const bool groups = mesh.hasFaceGroups() || options.rimGroup;
    const uint32_t rimGroupId = options.rimGroup ? nextFaceGroupId(mesh) : 0;
    out.triangles.clear();
    out.facegroups.clear();
    out.hidden.clear();
    out.uvs.clear();
    out.uvFlags.clear();
    out.creases.clear();
    out.clearQuads();
    std::vector<uint32_t> newIndex(nt0, kNone);
    std::vector<glm::vec2> newUv = mesh.uvs; // per original corner (the moved ones re-derived)
    auto pushTriangle = [&](const glm::uvec3& tri, uint32_t group, uint8_t hiddenFlag, const glm::vec2* uv, const uint8_t* flags, const float* creases) {
        out.triangles.push_back(tri);
        if (groups) out.facegroups.push_back(group);
        if (mesh.hasHidden()) out.hidden.push_back(hiddenFlag);
        if (mesh.hasUvs()) out.uvs.insert(out.uvs.end(), uv, uv + 3);
        if (mesh.hasUvFlags()) out.uvFlags.insert(out.uvFlags.end(), flags, flags + 3);
        if (mesh.hasCreases()) out.creases.insert(out.creases.end(), creases, creases + 3);
    };
    for (uint32_t t = 0; t < nt0; ++t) {
        const glm::uvec3 tri = mesh.triangles[t];
        const uint32_t group = groups ? mesh.facegroupAt(t) : 0;
        const uint8_t hiddenFlag = mesh.hasHidden() ? mesh.hidden[t] : 0;
        if (!a.isSelected[t]) {
            newIndex[t] = uint32_t(out.triangles.size());
            pushTriangle(tri, group, hiddenFlag, mesh.hasUvs() ? &mesh.uvs[3 * t] : nullptr, mesh.hasUvFlags() ? &mesh.uvFlags[3 * t] : nullptr,
                         mesh.hasCreases() ? &mesh.creases[3 * t] : nullptr);
            continue;
        }
        const glm::uvec3 moved(vertexOfCorner[3 * t], vertexOfCorner[3 * t + 1], vertexOfCorner[3 * t + 2]);
        if (moved.x == moved.y || moved.y == moved.z || moved.x == moved.z) continue; // collapsed into the border
        glm::vec2 uv[3] = {};
        uint8_t flags[3] = {0, 0, 0};
        float creases[3] = {0.0f, 0.0f, 0.0f};
        for (int k = 0; k < 3; ++k) {
            const uint32_t c = 3 * t + k;
            if (mesh.hasUvs()) {
                glm::vec3 bc;
                if (moved[k] != tri[k] &&
                    barycentric(mesh.positions[tri.x], mesh.positions[tri.y], mesh.positions[tri.z], flatPositions[moved[k]], bc))
                    newUv[c] = bc.x * mesh.uvs[3 * t] + bc.y * mesh.uvs[3 * t + 1] + bc.z * mesh.uvs[3 * t + 2];
                uv[k] = newUv[c];
            }
            if (mesh.hasUvFlags()) {
                flags[k] = mesh.uvFlags[c];
                if (moved[k] != tri[k]) flags[k] &= uint8_t(~kUvPin); // a derived UV is no pin
                if (a.borderCorner[c]) flags[k] &= uint8_t(~kUvSeam);  // the seam stays with the old edge, on the rim
            }
            if (mesh.hasCreases()) creases[k] = a.borderCorner[c] ? 0.0f : mesh.creases[c];
        }
        newIndex[t] = uint32_t(out.triangles.size());
        pushTriangle(moved, group, hiddenFlag, uv, flags, creases);
    }
    remapQuads(mesh, newIndex, out);
    out.ensureQuadPartners();
    for (uint32_t t : a.selected)
        if (newIndex[t] != kNone) res.selection.push_back(newIndex[t]);

    // The rim: one quad per border edge (a, b, b', a'), facing the way its face does; a triangle where a' and b'
    // merged.
    for (uint32_t t : a.selected) {
        const glm::uvec3 tri = mesh.triangles[t];
        for (int k = 0; k < 3; ++k) {
            const uint32_t c = 3 * t + k;
            if (!a.borderCorner[c]) continue;
            const uint32_t n = cornerNext(c);
            const uint32_t va = tri[k], vb = tri[(k + 1) % 3];
            const uint32_t va2 = newVertexOfFan[a.fanOfCorner[c]], vb2 = newVertexOfFan[a.fanOfCorner[n]];
            const uint32_t group = options.rimGroup ? rimGroupId : (groups ? mesh.facegroupAt(t) : 0);
            const glm::vec2 ua = mesh.hasUvs() ? mesh.uvs[c] : glm::vec2(0.0f), ub = mesh.hasUvs() ? mesh.uvs[n] : glm::vec2(0.0f);
            const glm::vec2 ua2 = mesh.hasUvs() ? newUv[c] : glm::vec2(0.0f), ub2 = mesh.hasUvs() ? newUv[n] : glm::vec2(0.0f);
            const uint8_t fa = mesh.hasUvFlags() ? mesh.uvFlags[c] : 0, fb = mesh.hasUvFlags() ? mesh.uvFlags[n] : 0;
            const float crease = mesh.hasCreases() ? mesh.creases[c] : 0.0f;
            const uint32_t t1 = uint32_t(out.triangles.size());
            if (va2 == vb2) {
                const glm::vec2 uv[3] = {ua, ub, ua2};
                const uint8_t flags[3] = {fa, uint8_t(fb & kUvPin), 0};
                const float creases[3] = {crease, 0.0f, 0.0f};
                pushTriangle(glm::uvec3(va, vb, va2), group, 0, uv, flags, creases);
                res.rim.push_back(t1);
                continue;
            }
            {
                const glm::vec2 uv[3] = {ua, ub, ub2};
                const uint8_t flags[3] = {fa, uint8_t(fb & kUvPin), 0}; // the old edge's seam and a's pin; b's pin
                const float creases[3] = {crease, 0.0f, 0.0f};
                pushTriangle(glm::uvec3(va, vb, vb2), group, 0, uv, flags, creases);
            }
            {
                const glm::vec2 uv[3] = {ua, ub2, ua2};
                const uint8_t flags[3] = {uint8_t(fa & kUvPin), 0, 0};
                const float creases[3] = {0.0f, 0.0f, 0.0f};
                pushTriangle(glm::uvec3(va, vb2, va2), group, 0, uv, flags, creases);
            }
            out.pairQuad(t1, t1 + 1);
            res.rim.push_back(t1);
            res.rim.push_back(t1 + 1);
        }
    }
    out.quadPartners.resize(out.triangles.size(), Mesh::kNoQuad);
    out.rebuildTopology();
    out.recomputeAllNormals();
    out.markAllDirty();
    out.facesDirty = true;

    res.ok = true;
    res.mesh = std::move(out);
    return res;
}

// --- The Polygon mode op ---

bool polyInset(const PolyOpInput& in, PolyOpOutput& out, std::string& error) {
    InsetOptions o;
    const float size = glm::length(in.mesh.bounds().extent());
    o.thickness = float(argNumber(in.args, "thickness", 0.05)) * size;
    o.depth = float(argNumber(in.args, "depth", 0.0)) * size;
    o.individual = argBool(in.args, "individual", false);
    o.rimGroup = argBool(in.args, "rim_group", false);
    InsetResult r = insetFaces(in.mesh, in.triangles, o);
    if (!r.ok) {
        error = r.error;
        return false;
    }
    out.mesh = std::move(r.mesh);
    out.element = PolyElement::Faces;
    out.triangles = std::move(r.selection);
    char buf[128];
    std::snprintf(buf, sizeof buf, "Inset %u %s%s", r.regions, r.regions == 1 ? "face" : "faces",
                  r.clamped ? " (thickness held to the faces' size)" : "");
    out.message = buf;
    return true;
}

void registerPolyInsetOp(PolyOpRegistry& reg) {
    PolyOp op;
    op.id = "inset";
    op.label = "Inset";
    op.description = "Shrink the selected faces inward by a thickness and move them in or out by a depth; a rim of quads "
                     "fills the gap and keeps the faces' groups.";
    op.credit = "#225 and the ideas threads (polygon modelling)";
    op.needs = PolyElement::Faces;
    op.preview = true;
    op.params = {{"thickness", "Thickness", 0.0, 0.5, 0.05, "How far the border moves in, as a fraction of the Part's size; held below the point where a face would turn inside out"},
                 {"depth", "Depth", -0.5, 0.5, 0.0, "How far the inner faces move along their normal, as a fraction of the Part's size (negative: into the surface)"},
                 {"individual", "Each face", 0.0, 1.0, 0.0, "Inset every face by itself instead of the selection as one shape", true},
                 {"rim_group", "Rim group", 0.0, 1.0, 0.0, "The rim joins one new face group instead of its faces' groups", true}};
    op.run = polyInset;
    reg.add(std::move(op));
}

} // namespace sf
