// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Edge loop insertion; see core/EdgeLoopInsert.h.
#include "core/EdgeLoopInsert.h"

#include "core/Commands.h"
#include "core/Document.h"
#include "core/Multires.h"
#include "core/PaintLayer.h"
#include "core/Uv.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <unordered_set>

namespace sf {

namespace {

constexpr float kMaxSlide = 0.98f; // a loop never lands on a rail (that would make zero-length edges)

// A polygon of the base mesh: its corners in winding order, each with the mesh corner that holds its attributes,
// and per edge i (v[i] -> v[i + 1]) the corner that starts it (whose uvFlags seam and crease belong to the edge).
struct Poly {
    int n = 0;
    uint32_t tri[2] = {Mesh::kNoQuad, Mesh::kNoQuad}; // its triangle slots (tri[1]: the quad's partner)
    uint32_t v[4] = {};
    uint32_t corner[4] = {};
    uint32_t edgeCorner[4] = {};
};

Poly polygonOf(const Mesh& m, uint32_t t) {
    Poly p;
    const glm::uvec3& tri = m.triangles[t];
    const uint32_t u = m.quadPartnerAt(t);
    const int e = u == Mesh::kNoQuad ? -1 : m.quadDiagonal(t, u);
    if (e < 0) {
        p.n = 3;
        p.tri[0] = t;
        for (int k = 0; k < 3; ++k) p.v[k] = tri[k], p.corner[k] = p.edgeCorner[k] = t * 3 + k;
        return p;
    }
    // The quad in winding order: t[e + 1], t[e + 2], t[e], then the partner's far corner (the partner holds the
    // diagonal the other way round, t[e + 1] -> t[e], so its order is v0, v2, far).
    p.n = 4;
    p.tri[0] = t;
    p.tri[1] = u;
    const int i0 = (e + 1) % 3, i1 = (e + 2) % 3;
    p.v[0] = tri[i0], p.corner[0] = p.edgeCorner[0] = t * 3 + i0;
    p.v[1] = tri[i1], p.corner[1] = p.edgeCorner[1] = t * 3 + i1;
    p.v[2] = tri[e], p.corner[2] = t * 3 + e;
    const glm::uvec3& ut = m.triangles[u];
    for (int k = 0; k < 3; ++k) {
        if (ut[k] == p.v[2]) p.edgeCorner[2] = u * 3 + k;
        else if (ut[k] != p.v[0]) p.v[3] = ut[k], p.corner[3] = p.edgeCorner[3] = u * 3 + k;
    }
    return p;
}

uint32_t polygonId(const Mesh& m, uint32_t t) {
    const uint32_t u = m.quadPartnerAt(t);
    return u == Mesh::kNoQuad ? t : std::min(t, u);
}

// The polygon across edge e from polygon `from` (kNoEdge: none, an open or non-manifold edge).
uint32_t across(const Mesh& m, const EdgeTopology& topo, uint32_t e, uint32_t from) {
    const uint32_t c = topo.edgeCorner[e], d = topo.twin[c];
    const uint32_t a = polygonId(m, c / 3);
    if (a != from) return a;
    return d == kNoCorner ? kNoEdge : polygonId(m, d / 3);
}

// An edge with no polygon across it that another edge of the same two vertices shares (three or more triangles on
// one edge): EdgeTopology names each of its corners as an open edge of its own. Not splittable.
bool nonManifold(const EdgeTopology& topo, uint32_t e) {
    if (!topo.open(e)) return false;
    const glm::uvec2 ends = topo.ends[e];
    for (uint32_t i = topo.vertexOffsets[ends.x]; i < topo.vertexOffsets[ends.x + 1]; ++i) {
        const uint32_t g = topo.vertexEdges[i];
        if (g != e && topo.ends[g] == ends) return true;
    }
    return false;
}

// The edge of `p` from a to b (either way), or -1.
int edgeOf(const Poly& p, uint32_t a, uint32_t b) {
    for (int i = 0; i < p.n; ++i) {
        const uint32_t x = p.v[i], y = p.v[(i + 1) % p.n];
        if ((x == a && y == b) || (x == b && y == a)) return i;
    }
    return -1;
}

std::vector<float> loopParams(const EdgeLoopOptions& o) {
    std::vector<float> t(size_t(o.count));
    const float s = std::clamp(o.slide, -kMaxSlide, kMaxSlide);
    for (int i = 0; i < o.count; ++i) {
        float x = float(i + 1) / float(o.count + 1);
        x = s > 0.0f ? x + s * (1.0f - x) : x * (1.0f + s);
        t[size_t(i)] = x;
    }
    return t;
}

bool validCount(const EdgeLoopOptions& o, std::string& error) {
    if (o.count >= 1 && o.count <= EdgeLoopOptions::kMaxLoops) return true;
    error = "Edge loop: the count must be between 1 and " + std::to_string(EdgeLoopOptions::kMaxLoops);
    return false;
}

// One walk from an edge of the ring, through the polygon on one side of it, on along the ring.
struct Walk {
    std::vector<uint32_t> edges, left, right, quads; // the edges beyond the start, and the quads between
    uint32_t end = kNoEdge;                          // the polygon the walk ended on (fanned), or none
    bool closed = false;
    bool blocked = false; // the start edge itself cannot be split (its polygon was crossed by the other walk)
};

} // namespace

EdgeRing edgeRing(const Mesh& m, const EdgeTopology& topo, uint32_t seed) {
    EdgeRing ring;
    if (seed >= topo.edgeCount() || topo.cornerEdge.size() != m.triangleCount() * 3 || nonManifold(topo, seed))
        return ring;
    const uint32_t seedLeft = topo.ends[seed].x, seedRight = topo.ends[seed].y;
    std::unordered_set<uint32_t> visitedQuads, visitedEdges{seed};

    auto walk = [&](uint32_t poly, Walk& w) {
        uint32_t l = seedLeft, r = seedRight;
        for (;;) {
            if (poly == kNoEdge) return; // the ring ends on an open border (that edge is still split)
            if (visitedQuads.count(poly)) {
                // Crossed already by the other walk: the edge we arrived through is a side of a split strip and
                // cannot be split too. Take it back; the quad before it is fanned instead.
                if (w.edges.empty()) {
                    w.blocked = true;
                    return;
                }
                w.edges.pop_back(), w.left.pop_back(), w.right.pop_back();
                w.end = w.quads.back();
                w.quads.pop_back();
                return;
            }
            const Poly p = polygonOf(m, poly);
            if (p.n == 3) {
                w.end = poly;
                return;
            }
            const int i = edgeOf(p, l, r);
            if (i < 0) return; // (cannot happen: the quad lies across the edge l-r)
            const int oi = (i + 2) % 4;
            const uint32_t next = topo.cornerEdge[p.edgeCorner[oi]];
            // Edge i runs v[i] -> v[i + 1]; v[i + 3] is the other end of the rail through v[i].
            const bool straight = p.v[i] == l;
            const uint32_t nl = straight ? p.v[(i + 3) % 4] : p.v[oi];
            const uint32_t nr = straight ? p.v[oi] : p.v[(i + 3) % 4];
            if (next == seed) {
                w.closed = true;
                w.quads.push_back(poly);
                visitedQuads.insert(poly);
                return;
            }
            if (next == kNoEdge || visitedEdges.count(next) || nonManifold(topo, next)) {
                // The loop cannot go on through `next`: this quad is where it ends (fanned, `next` stays whole).
                w.end = poly;
                visitedQuads.insert(poly);
                return;
            }
            visitedQuads.insert(poly);
            visitedEdges.insert(next);
            w.quads.push_back(poly);
            w.edges.push_back(next);
            w.left.push_back(nl);
            w.right.push_back(nr);
            l = nl, r = nr;
            poly = across(m, topo, next, poly);
        }
    };

    const uint32_t c = topo.edgeCorner[seed], d = topo.twin[c];
    Walk a, b;
    walk(polygonId(m, c / 3), a);
    if (a.closed) {
        ring.edges.push_back(seed), ring.left.push_back(seedLeft), ring.right.push_back(seedRight);
        ring.edges.insert(ring.edges.end(), a.edges.begin(), a.edges.end());
        ring.left.insert(ring.left.end(), a.left.begin(), a.left.end());
        ring.right.insert(ring.right.end(), a.right.begin(), a.right.end());
        ring.quads = a.quads;
        ring.closed = true;
        return ring;
    }
    walk(d == kNoCorner ? kNoEdge : polygonId(m, d / 3), b);
    if (b.blocked) {
        // The seed is a side of a strip the first walk split: it stays whole, the first walk's first quad is fanned.
        if (a.quads.empty()) return ring;
        ring.startEnd = a.quads.front();
        a.quads.erase(a.quads.begin());
    } else {
        ring.edges.assign(b.edges.rbegin(), b.edges.rend());
        ring.left.assign(b.left.rbegin(), b.left.rend());
        ring.right.assign(b.right.rbegin(), b.right.rend());
        ring.quads.assign(b.quads.rbegin(), b.quads.rend());
        ring.edges.push_back(seed), ring.left.push_back(seedLeft), ring.right.push_back(seedRight);
        ring.startEnd = b.end;
    }
    ring.edges.insert(ring.edges.end(), a.edges.begin(), a.edges.end());
    ring.left.insert(ring.left.end(), a.left.begin(), a.left.end());
    ring.right.insert(ring.right.end(), a.right.begin(), a.right.end());
    ring.quads.insert(ring.quads.end(), a.quads.begin(), a.quads.end());
    ring.finishEnd = a.end;
    return ring;
}

std::vector<std::vector<glm::vec3>> edgeLoopPreview(const Mesh& m, const EdgeRing& ring, const EdgeLoopOptions& options) {
    std::vector<std::vector<glm::vec3>> lines;
    std::string error;
    if (!validCount(options, error) || ring.empty()) return lines;
    const std::vector<float> t = loopParams(options);
    lines.resize(t.size());
    for (size_t i = 0; i < t.size(); ++i) {
        lines[i].reserve(ring.edges.size());
        for (size_t k = 0; k < ring.edges.size(); ++k)
            lines[i].push_back(glm::mix(m.positions[ring.left[k]], m.positions[ring.right[k]], t[i]));
    }
    return lines;
}

std::vector<std::vector<glm::vec3>> edgeLoopPreview(const Mesh& m, const EdgeTopology& topo, uint32_t seed,
                                                    const EdgeLoopOptions& options) {
    return edgeLoopPreview(m, edgeRing(m, topo, seed), options);
}

namespace {

// A corner of a new triangle: its vertex, UV and pin flag; an edge of one: its seam flag and crease.
struct NewCorner {
    uint32_t v = 0;
    glm::vec2 uv{0.0f};
    uint8_t pin = 0;
};
struct NewEdge {
    uint8_t seam = 0;
    float crease = 0.0f;
};

// Writes the new triangles of the split polygons into the slots those polygons held, then past the end.
class Writer {
public:
    Writer(Mesh& m, std::vector<uint32_t>& written)
        : m_(m), written_(written), uvs_(m.hasUvs()), flags_(m.hasUvFlags()), creases_(m.hasCreases()) {}

    // The next triangles come from polygon `p`: into its slots first.
    void begin(const Poly& p) {
        slots_.clear();
        for (uint32_t t : p.tri)
            if (t != Mesh::kNoQuad) slots_.push_back(t);
        group_ = m_.facegroupAt(p.tri[0]);
        hidden_ = m_.triangleHidden(p.tri[0]) ? 1 : 0;
    }

    uint32_t tri(const NewCorner c[3], const NewEdge e[3]) {
        uint32_t t;
        if (!slots_.empty()) {
            t = slots_.back();
            slots_.pop_back();
            if (m_.hasQuads()) m_.quadPartners[t] = Mesh::kNoQuad;
        } else {
            t = uint32_t(m_.triangles.size());
            m_.triangles.emplace_back();
            if (m_.hasFaceGroups()) m_.facegroups.push_back(group_);
            if (m_.hasHidden()) m_.hidden.push_back(hidden_);
            if (uvs_) m_.uvs.resize(m_.uvs.size() + 3);
            if (flags_) m_.uvFlags.resize(m_.uvFlags.size() + 3);
            if (creases_) m_.creases.resize(m_.creases.size() + 3);
            if (m_.hasQuads()) m_.quadPartners.push_back(Mesh::kNoQuad);
        }
        m_.triangles[t] = {c[0].v, c[1].v, c[2].v};
        written_.push_back(t);
        if (m_.hasFaceGroups()) m_.facegroups[t] = group_;
        if (m_.hasHidden()) m_.hidden[t] = hidden_;
        for (int k = 0; k < 3; ++k) {
            if (uvs_) m_.uvs[t * 3 + k] = c[k].uv;
            if (flags_) m_.uvFlags[t * 3 + k] = uint8_t(c[k].pin | e[k].seam);
            if (creases_) m_.creases[t * 3 + k] = e[k].crease;
        }
        return t;
    }

    // A quad q0 q1 q2 q3 (edges e[i] from q[i] to q[i + 1]) as two paired triangles, split along its shorter diagonal
    // (the flatter pair on a bent quad).
    void quad(const NewCorner q[4], const NewEdge e[4]) {
        const float d02 = glm::length(m_.positions[q[2].v] - m_.positions[q[0].v]);
        const float d13 = glm::length(m_.positions[q[3].v] - m_.positions[q[1].v]);
        const NewEdge none{};
        uint32_t t1, t2;
        if (d13 < d02) {
            const NewCorner c1[3] = {q[1], q[2], q[3]}, c2[3] = {q[1], q[3], q[0]};
            const NewEdge e1[3] = {e[1], e[2], none}, e2[3] = {none, e[3], e[0]};
            t1 = tri(c1, e1), t2 = tri(c2, e2);
        } else {
            const NewCorner c1[3] = {q[0], q[1], q[2]}, c2[3] = {q[0], q[2], q[3]};
            const NewEdge e1[3] = {e[0], e[1], none}, e2[3] = {none, e[2], e[3]};
            t1 = tri(c1, e1), t2 = tri(c2, e2);
        }
        m_.pairQuad(t1, t2);
    }

private:
    Mesh& m_;
    std::vector<uint32_t>& written_;
    bool uvs_, flags_, creases_;
    std::vector<uint32_t> slots_;
    uint32_t group_ = 0;
    uint8_t hidden_ = 0;
};

} // namespace

bool insertEdgeLoops(Mesh& m, const EdgeTopology& topo, uint32_t seed, const EdgeLoopOptions& options,
                     EdgeLoopResult& result, std::string& error) {
    result = EdgeLoopResult();
    if (!validCount(options, error)) return false;
    if (topo.cornerEdge.size() != m.triangleCount() * 3) {
        error = "Edge loop: the edge topology is out of date";
        return false;
    }
    const EdgeRing ring = edgeRing(m, topo, seed);
    if (ring.empty()) {
        error = seed < topo.edgeCount() ? "Edge loop: no loop can be inserted through this edge"
                                        : "Edge loop: no such edge";
        return false;
    }
    return insertEdgeLoops(m, topo, ring, options, result, error);
}

bool insertEdgeLoops(Mesh& m, const EdgeTopology& topo, const EdgeRing& ring, const EdgeLoopOptions& options,
                     EdgeLoopResult& result, std::string& error) {
    result = EdgeLoopResult();
    if (!validCount(options, error)) return false;
    if (topo.cornerEdge.size() != m.triangleCount() * 3) {
        error = "Edge loop: the edge topology is out of date";
        return false;
    }
    if (ring.empty() || ring.left.size() != ring.edges.size() || ring.right.size() != ring.edges.size() ||
        ring.quads.size() + (ring.closed ? 0 : 1) != ring.edges.size()) {
        error = "Edge loop: no ring to insert into";
        return false;
    }
    for (size_t k = 0; k < ring.edges.size(); ++k) {
        const uint32_t e = ring.edges[k], l = ring.left[k], r = ring.right[k];
        if (e >= topo.edgeCount() || topo.ends[e] != glm::uvec2(std::min(l, r), std::max(l, r))) {
            error = "Edge loop: the ring does not belong to this mesh";
            return false;
        }
    }
    for (uint32_t q : ring.quads)
        if (q >= m.triangleCount()) {
            error = "Edge loop: the ring does not belong to this mesh";
            return false;
        }
    const size_t n = size_t(options.count), ne = ring.edges.size();
    const std::vector<float> params = loopParams(options);

    // The new vertices: per ring edge, `n` of them from its left rail to its right, interpolated in everything the
    // mesh keeps per vertex.
    const bool hasLayers = !m.layers.empty();
    const bool hasPaint = !m.paintLayers.empty();
    auto addVertex = [&](uint32_t a, uint32_t b, float t) {
        const uint32_t v = uint32_t(m.positions.size());
        m.positions.push_back(glm::mix(m.positions[a], m.positions[b], t));
        if (m.normals.size() == v) m.normals.push_back(glm::vec3(0.0f));
        if (m.hasMask()) m.mask.push_back(glm::mix(m.mask[a], m.mask[b], t));
        if (m.hasColors()) m.colors.push_back(glm::mix(m.colors[a], m.colors[b], t));
        if (hasLayers) {
            m.base.push_back(glm::mix(m.base[a], m.base[b], t));
            for (SculptLayer& l : m.layers) l.delta.push_back(glm::mix(l.delta[a], l.delta[b], t));
        }
        if (hasPaint) {
            if (m.paintBase.size() == v) m.paintBase.push_back(glm::mix(m.paintBase[a], m.paintBase[b], t));
            for (PaintLayer& l : m.paintLayers) {
                if (l.color.size() == v) l.color.push_back(glm::mix(l.color[a], l.color[b], t));
                if (l.alpha.size() == v) l.alpha.push_back(glm::mix(l.alpha[a], l.alpha[b], t));
            }
        }
        return v;
    };
    std::vector<std::vector<uint32_t>> newV(ne); // per ring edge: its new vertices, left to right
    for (size_t k = 0; k < ne; ++k) {
        newV[k].reserve(n);
        for (float t : params) newV[k].push_back(addVertex(ring.left[k], ring.right[k], t));
    }
    result.loops.assign(n, {});
    for (size_t i = 0; i < n; ++i) {
        result.loops[i].reserve(ne);
        for (size_t k = 0; k < ne; ++k) result.loops[i].push_back(newV[k][i]);
    }
    std::unordered_map<uint32_t, uint32_t> ringIndex; // edge -> its index in the ring
    for (size_t k = 0; k < ne; ++k) ringIndex.emplace(ring.edges[k], uint32_t(k));

    const bool hasUvs = m.hasUvs(), hasFlags = m.hasUvFlags(), hasCreases = m.hasCreases();
    auto cornerOf = [&](const Poly& p, int i) {
        NewCorner c;
        c.v = p.v[i];
        if (hasUvs) c.uv = m.uvs[p.corner[i]];
        if (hasFlags) c.pin = uint8_t(m.uvFlags[p.corner[i]] & kUvPin);
        return c;
    };
    auto edgeAttr = [&](const Poly& p, int i) {
        NewEdge e;
        if (hasFlags) e.seam = uint8_t(m.uvFlags[p.edgeCorner[i]] & kUvSeam);
        if (hasCreases) e.crease = m.creases[p.edgeCorner[i]];
        return e;
    };
    // The new corners along edge i of `p` (from v[i] towards v[i + 1]), when that edge is on the ring.
    auto pointsOn = [&](const Poly& p, int i, std::vector<NewCorner>& out) {
        out.clear();
        const uint32_t e = topo.cornerEdge[p.edgeCorner[i]];
        const auto it = ringIndex.find(e);
        if (it == ringIndex.end()) return false;
        const uint32_t k = it->second;
        const bool fromLeft = p.v[i] == ring.left[k];
        const NewCorner a = cornerOf(p, i), b = cornerOf(p, (i + 1) % p.n);
        for (size_t j = 0; j < n; ++j) {
            const size_t jj = fromLeft ? j : n - 1 - j;
            const float t = fromLeft ? params[jj] : 1.0f - params[jj];
            NewCorner c;
            c.v = newV[k][jj];
            c.uv = glm::mix(a.uv, b.uv, t);
            out.push_back(c);
        }
        return true;
    };

    // Strip quads need pairs; a fan may make one on a mesh that had none.
    m.ensureQuadPartners();
    Writer w(m, result.triangles);
    std::vector<NewCorner> P, Q;

    // Each quad of the ring: the strip between its two ring edges.
    for (uint32_t q : ring.quads) {
        const Poly p = polygonOf(m, q);
        int i = -1;
        for (int k = 0; k < 4 && i < 0; ++k)
            if (ringIndex.count(topo.cornerEdge[p.edgeCorner[k]])) i = k;
        if (i < 0 || !pointsOn(p, i, P) || !pointsOn(p, (i + 2) % 4, Q)) continue; // (cannot happen)
        const int ib = (i + 1) % 4, ic = (i + 2) % 4, id = (i + 3) % 4;
        const NewCorner a = cornerOf(p, i), b = cornerOf(p, ib), c = cornerOf(p, ic), d = cornerOf(p, id);
        const NewEdge ab = edgeAttr(p, i), bc = edgeAttr(p, ib), cd = edgeAttr(p, ic), da = edgeAttr(p, id), none{};
        w.begin(p);
        // P runs a -> b, Q runs c -> d: P[j] faces Q[n - 1 - j].
        const NewCorner q1[4] = {a, P[0], Q[n - 1], d};
        const NewEdge e1[4] = {ab, none, cd, da};
        w.quad(q1, e1);
        for (size_t j = 0; j + 1 < n; ++j) {
            const NewCorner qm[4] = {P[j], P[j + 1], Q[n - 2 - j], Q[n - 1 - j]};
            const NewEdge em[4] = {ab, none, cd, none};
            w.quad(qm, em);
        }
        const NewCorner q2[4] = {P[n - 1], b, c, Q[0]};
        const NewEdge e2[4] = {ab, bc, cd, none};
        w.quad(q2, e2);
        ++result.quadsSplit;
    }

    // A polygon the ring ends on: its boundary with the new points on its split edges, fanned from a corner on no
    // split edge (any corner when every one is), consecutive fan triangles paired into a quad where their middle
    // corner is an original vertex (never across a straight angle on a split edge).
    auto fan = [&](uint32_t poly) {
        const Poly p = polygonOf(m, poly);
        std::vector<NewCorner> ring_;
        std::vector<NewEdge> edges_;
        std::vector<uint8_t> original, onSplit(size_t(p.n), 0);
        for (int i = 0; i < p.n; ++i) {
            const NewEdge e = edgeAttr(p, i);
            ring_.push_back(cornerOf(p, i)), original.push_back(1), edges_.push_back(e);
            if (pointsOn(p, i, P)) {
                onSplit[size_t(i)] = onSplit[size_t((i + 1) % p.n)] = 1;
                for (const NewCorner& c : P) ring_.push_back(c), original.push_back(0), edges_.push_back(e);
            }
        }
        int apex = -1;
        for (int i = 0; i < p.n && apex < 0; ++i)
            if (!onSplit[size_t(i)]) apex = i;
        if (apex < 0) apex = 0;
        // Rotate so the apex is first.
        size_t start = 0;
        for (size_t j = 0; j < ring_.size(); ++j)
            if (original[j] && ring_[j].v == p.v[apex]) start = j;
        std::rotate(ring_.begin(), ring_.begin() + std::ptrdiff_t(start), ring_.end());
        std::rotate(original.begin(), original.begin() + std::ptrdiff_t(start), original.end());
        std::rotate(edges_.begin(), edges_.begin() + std::ptrdiff_t(start), edges_.end());
        const size_t sz = ring_.size();
        const NewEdge none{};
        w.begin(p);
        for (size_t j = 1; j + 1 < sz;) {
            const bool pair = j + 2 < sz && original[j + 1];
            if (pair) {
                const NewCorner q[4] = {ring_[0], ring_[j], ring_[j + 1], ring_[j + 2]};
                const NewEdge e[4] = {j == 1 ? edges_[0] : none, edges_[j], edges_[j + 1],
                                      j + 3 == sz ? edges_[sz - 1] : none};
                w.quad(q, e);
                j += 2;
            } else {
                const NewCorner c[3] = {ring_[0], ring_[j], ring_[j + 1]};
                const NewEdge e[3] = {j == 1 ? edges_[0] : none, edges_[j], j + 2 == sz ? edges_[sz - 1] : none};
                w.tri(c, e);
                j += 1;
            }
        }
        ++result.polygonsFanned;
    };
    if (ring.startEnd != kNoEdge) fan(ring.startEnd);
    if (ring.finishEnd != kNoEdge && ring.finishEnd != ring.startEnd) fan(ring.finishEnd);

    result.closed = ring.closed;
    if (m.quadCount() == 0) m.clearQuads();
    m.rebuildTopology();
    m.recomputeAllNormals();
    return true;
}

namespace {

std::string count(size_t n, const char* what) { return std::to_string(n) + " " + what + (n == 1 ? "" : "s"); }

// The edge between vertices x and y of `topo`, or kNoEdge.
uint32_t edgeBetween(const EdgeTopology& topo, uint32_t x, uint32_t y) {
    for (uint32_t i = topo.vertexOffsets[x]; i < topo.vertexOffsets[x + 1]; ++i) {
        const uint32_t e = topo.vertexEdges[i];
        if (topo.ends[e].x == y || topo.ends[e].y == y) return e;
    }
    return kNoEdge;
}

} // namespace

void registerEdgeLoopCommands(CommandRegistry& reg) {
    using T = ParamSpec::Type;
    reg.add({"edge_loop",
             "Edge loop: insert `count` loops of new edges across the ring of quads through an edge of the active Part's "
             "base level (the edge given, or the first selected edge; edge_select picks edges), evenly spaced, `slide` "
             "moving them towards one side (-1..1, 0 = centred). The loop runs on until it comes back round or meets a "
             "border, a triangle or a quad it already crossed (a triangle at its end is split in two). Face groups, UVs, "
             "seams, creases, paint and mask are kept. One undo step; the new loops are selected afterwards.",
             {{"count", T::Number, "loops to insert, 1..64 (default 1)", false},
              {"slide", T::Number, "-1..1: towards one side of the ring or the other (default 0, centred)", false},
              {"edge", T::Number, "the edge to start from (default: the first selected edge)", false},
              {"a", T::Number, "with b: the edge between vertices a and b instead", false},
              {"b", T::Number, "with a: the other end vertex", false}},
             [](Document& doc, const Args& a) -> CommandResult {
                 const Mesh& m = doc.mesh;
                 if (m.triangleCount() == 0) return {false, "The active Part has no faces"};
                 if (!m.forgespheres.empty()) return {false, "Edge loop: make the Forge Spheres into a mesh first"};
                 if (!m.tube.empty()) return {false, "Edge loop: bake the Tube first"};
                 if (m.micro) return {false, "Edge loop: apply or remove the micro level first"};
                 if (m.multires)
                     return {false, "Edge loop: the Part has subdivision levels; delete them first (Geometry > Levels), "
                                    "then insert the loop and Divide again"};
                 EdgeSelection& sel = doc.edgeSelection;
                 const EdgeTopology& topo = sel.sync(m);
                 uint32_t seed = kNoEdge;
                 if (a.count("edge")) {
                     const double e = argNumber(a, "edge", -1);
                     seed = e >= 0 && e < double(topo.edgeCount()) ? uint32_t(e) : kNoEdge;
                     if (seed == kNoEdge) return {false, "Edge loop: no such edge"};
                 } else if (a.count("a") || a.count("b")) {
                     const double va = argNumber(a, "a", -1), vb = argNumber(a, "b", -1);
                     if (va >= 0 && vb >= 0 && va < double(m.vertexCount()) && vb < double(m.vertexCount()))
                         seed = edgeBetween(topo, uint32_t(va), uint32_t(vb));
                     if (seed == kNoEdge) return {false, "Edge loop: no edge joins vertices a and b"};
                 } else {
                     for (uint32_t e = 0; e < sel.selected.size() && seed == kNoEdge; ++e)
                         if (sel.selected[e]) seed = e;
                     if (seed == kNoEdge) return {false, "Edge loop: select an edge first (Edges mode: click one)"};
                 }
                 EdgeLoopOptions o;
                 o.count = int(std::clamp(argNumber(a, "count", 1.0), 1.0, double(EdgeLoopOptions::kMaxLoops)));
                 o.slide = float(std::clamp(argNumber(a, "slide", 0.0), -1.0, 1.0));
                 Mesh out = m;
                 EdgeLoopResult r;
                 std::string error;
                 if (!insertEdgeLoops(out, topo, seed, o, r, error)) return {false, error};
                 doc.replaceMeshUndoable(std::move(out), o.count == 1 ? "Edge loop" : "Edge loops");
                 // Select the new loops (the selection was dropped with the old triangles).
                 const EdgeTopology& after = sel.sync(doc.mesh);
                 size_t edges = 0;
                 for (const std::vector<uint32_t>& loop : r.loops) {
                     const size_t n = loop.size();
                     for (size_t k = 0; k + 1 < n || (r.closed && k < n && n > 1); ++k) {
                         const uint32_t e = edgeBetween(after, loop[k], loop[(k + 1) % n]);
                         if (e != kNoEdge) sel.set(e, true), ++edges;
                     }
                 }
                 std::string msg = "Edge loop: " + count(r.loops.size(), "loop") + " of " +
                                   count(r.loops.empty() ? 0 : r.loops[0].size(), "edge") + (r.closed ? " round" : "") +
                                   ", " + count(r.quadsSplit, "quad") + " split";
                 if (r.polygonsFanned) msg += ", ended on " + count(r.polygonsFanned, "triangle");
                 (void)edges;
                 return {true, msg};
             }});
}

} // namespace sf
