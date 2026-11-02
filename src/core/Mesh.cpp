// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/Mesh.h"
#include "core/Multires.h"
#include "core/Micro.h"
#include "core/AtomicRef.h"
#include "core/Prefetch.h"
#include "core/ThreadPool.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <tuple>
#include <unordered_map>

namespace sf {

// Implicit adjacency (Mesh::implicitAdjacency): the level's topology, plus, when every coarse polygon has the
// same number of sides (always so above the first Catmull-Clark level, and for Loop), where each coarse vertex's
// corners and each coarse edge's sides sit in the polygon lists, so a vertex's triangles take one or two reads.
struct ImplicitAdjacency {
    std::shared_ptr<const SubdivTopology> topo;
    uint32_t sides = 0; // 4 or 3 when every polygon has that many sides (polygon p's corners start at sides * p), else 0
    std::vector<uint32_t> cornerOffsets, corners; // per coarse vertex: its polygon corner slots, ascending (CSR)
    std::vector<glm::uvec2> edgeSides; // per coarse edge: the slot of its side in edgeFaces.x / .y (kNone)

    // The slot after `q` around its polygon.
    uint32_t next(uint32_t q) const { return sides == 4 ? (q & ~3u) | ((q + 1) & 3u) : q % 3 == 2 ? q - 2 : q + 1; }
    size_t bytes() const {
        return cornerOffsets.capacity() * 4 + corners.capacity() * 4 + edgeSides.capacity() * sizeof(glm::uvec2);
    }
};

namespace {

std::shared_ptr<const ImplicitAdjacency> buildImplicitAdjacency(std::shared_ptr<const SubdivTopology> topo) {
    auto a = std::make_shared<ImplicitAdjacency>();
    const SubdivTopology& tp = *topo;
    a->topo = std::move(topo);
    const size_t np = tp.polygonCount(), nc = tp.polyVerts.size();
    const uint32_t want = tp.catmullClark ? 4u : 3u;
    bool uniform = true;
    for (size_t p = 0; p < np && uniform; ++p) uniform = tp.polyOffsets[p] == want * p;
    uniform = uniform && tp.polyOffsets[np] == want * np;
    if (!uniform) return a;
    a->sides = want;
    a->cornerOffsets.assign(size_t(tp.coarseVertices) + 1, 0);
    for (size_t q = 0; q < nc; ++q) ++a->cornerOffsets[tp.polyVerts[q] + 1];
    for (size_t v = 0; v < tp.coarseVertices; ++v) a->cornerOffsets[v + 1] += a->cornerOffsets[v];
    a->corners.resize(nc);
    {
        std::vector<uint32_t> cursor(a->cornerOffsets.begin(), a->cornerOffsets.end() - 1);
        for (size_t q = 0; q < nc; ++q) a->corners[cursor[tp.polyVerts[q]]++] = uint32_t(q);
    }
    a->edgeSides.assign(tp.edges.size(), glm::uvec2(SubdivTopology::kNone));
    for (size_t q = 0; q < nc; ++q) {
        const uint32_t e = tp.polyEdges[q], p = uint32_t(q / want);
        if (tp.edgeFaces[e].x == p) a->edgeSides[e].x = uint32_t(q);
        else a->edgeSides[e].y = uint32_t(q);
    }
    return a;
}

} // namespace

static glm::vec3 faceNormal(const std::vector<glm::vec3>& p, const glm::uvec3& t) {
    // Unnormalised: magnitude is twice the triangle area, giving area weighting.
    return glm::cross(p[t.y] - p[t.x], p[t.z] - p[t.x]);
}

bool Mesh::vertexHidden(uint32_t v) const {
    if (hidden.empty()) return false;
    auto tris = vertexTriangles(v);
    if (tris.empty()) return false;
    for (uint32_t t : tris)
        if (!hidden[t]) return false;
    return true;
}

std::vector<float>& Mesh::ensureMask(float fill) {
    if (mask.size() != positions.size()) mask.assign(positions.size(), fill);
    return mask;
}

std::vector<glm::vec3>& Mesh::ensureColors(const glm::vec3& fill) {
    if (colors.size() != positions.size()) colors.assign(positions.size(), fill);
    return colors;
}

std::vector<uint32_t>& Mesh::ensurePolygroups(uint32_t fill) {
    if (polygroups.size() != triangles.size()) polygroups.assign(triangles.size(), fill);
    return polygroups;
}

std::vector<uint8_t>& Mesh::ensureHidden() {
    if (hidden.size() != triangles.size()) hidden.assign(triangles.size(), 0);
    return hidden;
}

std::vector<uint32_t>& Mesh::ensureQuadPartners() {
    if (quadPartners.size() != triangles.size()) quadPartners.assign(triangles.size(), kNoQuad);
    return quadPartners;
}

void Mesh::pairQuad(uint32_t t, uint32_t u) {
    // Grows (rather than resets) the array, so pairs can be recorded while triangles are being added.
    if (quadPartners.size() < triangles.size()) quadPartners.resize(triangles.size(), kNoQuad);
    quadPartners[t] = u;
    quadPartners[u] = t;
}

int Mesh::quadDiagonal(uint32_t t, uint32_t u) const {
    if (t == u || t >= triangles.size() || u >= triangles.size()) return -1;
    const glm::uvec3& a = triangles[t];
    const glm::uvec3& b = triangles[u];
    if (a.x == a.y || a.y == a.z || a.x == a.z || b.x == b.y || b.y == b.z || b.x == b.z) return -1;
    for (int e = 0; e < 3; ++e)
        for (int k = 0; k < 3; ++k)
            if (b[k] == a[(e + 1) % 3] && b[(k + 1) % 3] == a[e]) {
                const uint32_t far = b[(k + 2) % 3];
                return far == a[(e + 2) % 3] ? -1 : e; // the same triangle twice is no quad
            }
    return -1;
}

int Mesh::exportFace(uint32_t t, glm::uvec4& face) const {
    const glm::uvec3& a = triangles[t];
    const uint32_t u = quadPartnerAt(t);
    const int e = u == kNoQuad ? -1 : quadDiagonal(t, u);
    if (e < 0) {
        face = glm::uvec4(a, kNoQuad);
        return 3;
    }
    if (u < t) return 0;
    const glm::uvec3& b = triangles[u];
    uint32_t far = b.x;
    for (int k = 0; k < 3; ++k)
        if (b[k] != a[e] && b[k] != a[(e + 1) % 3]) far = b[k];
    face = glm::uvec4(a[(e + 1) % 3], a[(e + 2) % 3], a[e], far);
    return 4;
}

size_t Mesh::quadCount() const {
    size_t n = 0;
    for (uint32_t t = 0; t < quadPartners.size(); ++t) {
        const uint32_t u = quadPartnerAt(t);
        n += u != kNoQuad && u > t;
    }
    return n;
}

void Mesh::sanitizeAttributes() {
    if (!mask.empty() && mask.size() != positions.size()) clearMask();
    if (!colors.empty() && colors.size() != positions.size()) clearColors();
    if (!polygroups.empty() && polygroups.size() != triangles.size()) clearPolygroups();
    if (!hidden.empty() && hidden.size() != triangles.size()) clearHidden();
    if (!uvs.empty() && uvs.size() != triangles.size() * 3) std::vector<glm::vec2>().swap(uvs);
    if (!uvFlags.empty() && uvFlags.size() != triangles.size() * 3) std::vector<uint8_t>().swap(uvFlags);
    if (!quadPartners.empty() && quadPartners.size() != triangles.size()) clearQuads();
    // Quad pairs must be mutual and share an edge: unpair anything an edit or a malformed file left behind.
    bool anyQuad = false;
    for (uint32_t t = 0; t < quadPartners.size(); ++t) {
        const uint32_t u = quadPartners[t];
        if (u == kNoQuad) continue;
        if (u >= quadPartners.size() || quadPartners[u] != t || quadDiagonal(t, u) < 0) quadPartners[t] = kNoQuad;
        else anyQuad = true;
    }
    // A triangle unpaired above may still be named by its former partner, visited earlier.
    for (uint32_t t = 0; t < quadPartners.size(); ++t) {
        const uint32_t u = quadPartners[t];
        if (u != kNoQuad && quadPartners[u] != t) quadPartners[t] = kNoQuad;
    }
    if (!anyQuad) clearQuads();
    sanitizePaintLayers(*this);
    if (multires && !subdivMatches(*this)) multires.reset();
    if (micro && !microMatches(*this)) micro.reset();
}

void Mesh::rebuildTopology() {
    sanitizeAttributes();
    for (auto* v : {&adjEnds_, &adjCaps_, &vtEnds_, &vtCaps_}) std::vector<uint32_t>().swap(*v);
    const size_t nv = positions.size();
    finishTopology(); // (the normal set, normals and dirty bits first: they don't depend on the adjacency)

    // A level made by Divide: its adjacency follows from the level's topology.
    std::shared_ptr<const ImplicitAdjacency> before = std::move(implicit_); // reused if it is for the same level
    if (multires) {
        const std::shared_ptr<const SubdivTopology>& tp = multires->levels[size_t(multires->current)].topo;
        if (tp && subdivImplicitAdjacency(*tp, triangles))
            implicit_ = before && before->topo == tp ? std::move(before) : buildImplicitAdjacency(tp);
    }
    if (implicit_) {
        std::vector<uint32_t>().swap(adjOffsets_);
        std::vector<uint32_t>().swap(adjList_);
        std::vector<uint32_t>().swap(vtOffsets_);
        std::vector<uint32_t>().swap(vtList_);
        return;
    }

    vtOffsets_.assign(nv + 1, 0);
    for (const auto& t : triangles)
        for (int c = 0; c < 3; ++c) ++vtOffsets_[t[c] + 1];
    for (size_t i = 0; i < nv; ++i) vtOffsets_[i + 1] += vtOffsets_[i];
    vtList_.resize(vtOffsets_[nv]);
    {
        std::vector<uint32_t> cursor(vtOffsets_.begin(), vtOffsets_.end() - 1);
        for (uint32_t ti = 0; ti < triangles.size(); ++ti)
            for (int c = 0; c < 3; ++c) vtList_[cursor[triangles[ti][c]]++] = ti;
    }

    // Rings (sorted, unique) per vertex: sized in one parallel pass, written in a second.
    adjOffsets_.assign(nv + 1, 0);
    auto ring = [&](uint32_t v, std::vector<uint32_t>& out) {
        out.clear();
        for (uint32_t ti : vertexTriangles(v))
            for (int c = 0; c < 3; ++c)
                if (triangles[ti][c] != v) out.push_back(triangles[ti][c]);
        std::sort(out.begin(), out.end());
        out.erase(std::unique(out.begin(), out.end()), out.end());
    };
    poolFor(nv, [&](size_t b, size_t e) {
        std::vector<uint32_t> r;
        for (size_t v = b; v < e; ++v) {
            ring(uint32_t(v), r);
            adjOffsets_[v + 1] = uint32_t(r.size());
        }
    });
    for (size_t i = 0; i < nv; ++i) adjOffsets_[i + 1] += adjOffsets_[i];
    std::vector<uint32_t>().swap(adjList_); // exact size, no spare capacity
    adjList_.resize(adjOffsets_[nv]);
    poolFor(nv, [&](size_t b, size_t e) {
        std::vector<uint32_t> r;
        for (size_t v = b; v < e; ++v) {
            ring(uint32_t(v), r);
            std::copy(r.begin(), r.end(), adjList_.begin() + adjOffsets_[v]);
        }
    });
}

bool Mesh::beginDynamicAdjacency() {
    const size_t nv = positions.size();
    if (implicit_ || adjOffsets_.size() != nv + 1 || vtOffsets_.size() != nv + 1) return false;
    if (dynamicAdjacency()) return true;
    adjEnds_.assign(adjOffsets_.begin() + 1, adjOffsets_.end());
    vtEnds_.assign(vtOffsets_.begin() + 1, vtOffsets_.end());
    adjCaps_.resize(nv);
    vtCaps_.resize(nv);
    for (size_t v = 0; v < nv; ++v) {
        adjCaps_[v] = adjOffsets_[v + 1] - adjOffsets_[v];
        vtCaps_[v] = vtOffsets_[v + 1] - vtOffsets_[v];
    }
    return true;
}

void Mesh::endDynamicAdjacency() {
    if (!dynamicAdjacency()) return;
    const size_t nv = positions.size();
    auto pack = [nv](std::vector<uint32_t>& offsets, std::vector<uint32_t>& ends, std::vector<uint32_t>& caps,
                     std::vector<uint32_t>& list) {
        std::vector<uint32_t> packed;
        size_t total = 0;
        for (size_t v = 0; v < nv; ++v) total += ends[v] - offsets[v];
        packed.reserve(total + total / 16); // (room for the next stroke's first edits without copying it all)
        for (size_t v = 0; v < nv; ++v) {
            const uint32_t b = offsets[v], e = ends[v];
            offsets[v] = uint32_t(packed.size());
            packed.insert(packed.end(), list.begin() + b, list.begin() + e);
        }
        offsets.resize(nv + 1);
        offsets[nv] = uint32_t(packed.size());
        list.swap(packed);
        std::vector<uint32_t>().swap(ends);
        std::vector<uint32_t>().swap(caps);
    };
    pack(adjOffsets_, adjEnds_, adjCaps_, adjList_);
    pack(vtOffsets_, vtEnds_, vtCaps_, vtList_);
}

void Mesh::setList(std::vector<uint32_t>& offsets, std::vector<uint32_t>& ends, std::vector<uint32_t>& caps,
                   std::vector<uint32_t>& list, uint32_t v, std::span<const uint32_t> values) {
    const uint32_t n = uint32_t(values.size());
    if (n > caps[v]) { // no room: a new place at the end, with some to spare
        const uint32_t cap = std::max(n + 2, n + n / 2);
        offsets[v] = uint32_t(list.size());
        caps[v] = cap;
        list.resize(list.size() + cap);
    }
    std::copy(values.begin(), values.end(), list.begin() + offsets[v]);
    ends[v] = offsets[v] + n;
}

void Mesh::setVertexTriangles(uint32_t v, std::span<const uint32_t> tris) {
    setList(vtOffsets_, vtEnds_, vtCaps_, vtList_, v, tris);
}

void Mesh::setNeighbors(uint32_t v, std::span<const uint32_t> ring) {
    setList(adjOffsets_, adjEnds_, adjCaps_, adjList_, v, ring);
}

void Mesh::addVertexSlot() {
    // offsets[nv] is the spare last entry: it becomes the new vertex's (empty) list, and a new one follows.
    for (auto [offsets, ends, caps, list] : {std::tie(adjOffsets_, adjEnds_, adjCaps_, adjList_),
                                             std::tie(vtOffsets_, vtEnds_, vtCaps_, vtList_)}) {
        offsets.back() = uint32_t(list.size());
        offsets.push_back(uint32_t(list.size()));
        ends.push_back(uint32_t(list.size()));
        caps.push_back(0);
    }
}

void Mesh::removeLastVertexSlot() {
    for (auto [offsets, ends, caps] : {std::tie(adjOffsets_, adjEnds_, adjCaps_), std::tie(vtOffsets_, vtEnds_, vtCaps_)}) {
        offsets.pop_back();
        ends.pop_back();
        caps.pop_back();
    }
}

void Mesh::finishTopology() {
    const size_t nv = positions.size();
    normalBits_.resize(nv);
    normals.resize(nv);
    dirtyBlocks.assign((nv + kDirtyBlock - 1) / kDirtyBlock, 0);
    dirtyBits.assign(dirtyBlocks.size() * (kDirtyBlock / 64), 0);
    if (dirtyBegin < dirtyEnd) markDirtyRange(dirtyBegin, std::min(dirtyEnd, uint32_t(nv)));
    topologyDirty = true;
    facesDirty = true;
}

namespace {

inline void sortSmall(uint32_t* a, uint32_t n) {
    for (uint32_t i = 1; i < n; ++i) {
        const uint32_t x = a[i];
        uint32_t j = i;
        for (; j > 0 && a[j - 1] > x; --j) a[j] = a[j - 1];
        a[j] = x;
    }
}

// Four values in ascending order (a branchless sorting network, in registers: rings are sorted per lookup).
inline void sort4(uint32_t* out, uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
    const uint32_t ab0 = std::min(a, b), ab1 = std::max(a, b), cd0 = std::min(c, d), cd1 = std::max(c, d);
    const uint32_t lo = std::min(ab0, cd0), m0 = std::max(ab0, cd0), m1 = std::min(ab1, cd1), hi = std::max(ab1, cd1);
    out[0] = lo;
    out[1] = std::min(m0, m1);
    out[2] = std::max(m0, m1);
    out[3] = hi;
}

inline uint32_t uniqueSmall(uint32_t* a, uint32_t n) {
    uint32_t k = 0;
    for (uint32_t i = 0; i < n; ++i)
        if (k == 0 || a[k - 1] != a[i]) a[k++] = a[i];
    return k;
}

} // namespace

IndexRing Mesh::implicitTriangles(uint32_t v) const {
    // Fine vertices are the coarse vertices, then one per coarse edge, then one per polygon; each polygon's fine
    // triangles are laid out as generateFaces does (Catmull-Clark: corner i -> triangles 2i, 2i + 1 of the
    // polygon, the first holding side i's edge point, the second side i - 1's; Loop: corner i -> triangle i,
    // the edge points' triangle last).
    const ImplicitAdjacency& ia = *implicit_;
    const SubdivTopology& tp = *ia.topo;
    const uint32_t nv = tp.coarseVertices, ne = uint32_t(tp.edges.size());
    constexpr uint32_t kNone = SubdivTopology::kNone;
    IndexRing r;
    auto add = [&](size_t f) { r.buf_[r.size_++] = uint32_t(f); };
    if (ia.sides) { // every polygon has `sides` corners: slots straight from the indexes
        const bool cc = ia.sides == 4;
        if (v < nv) {
            for (uint32_t k = ia.cornerOffsets[v]; k < ia.cornerOffsets[v + 1]; ++k) {
                const uint32_t q = ia.corners[k];
                if (cc) {
                    add(2 * size_t(q));
                    add(2 * size_t(q) + 1);
                } else {
                    add(4 * size_t(q / 3) + q % 3);
                }
            }
        } else if (v < nv + ne) {
            const glm::uvec2 sides = ia.edgeSides[v - nv];
            if (cc && sides.y != kNone) {
                sort4(r.buf_, 2 * sides.x, 2 * ia.next(sides.x) + 1, 2 * sides.y, 2 * ia.next(sides.y) + 1);
                r.size_ = 4;
                return r;
            }
            for (uint32_t q : {sides.x, sides.y}) {
                if (q == kNone) continue;
                if (cc) {
                    add(2 * size_t(q));
                    add(2 * size_t(ia.next(q)) + 1);
                } else {
                    const size_t p = q / 3;
                    add(4 * p + q % 3);
                    add(4 * p + ia.next(q) % 3);
                    add(4 * p + 3);
                }
            }
            sortSmall(r.buf_, r.size_);
        } else {
            const size_t p = v - nv - ne;
            for (size_t t = 8 * p; t < 8 * p + 8; ++t) add(t);
        }
        return r;
    }
    if (v < nv) {
        uint32_t polys[IndexRing::kCapacity];
        uint32_t n = 0;
        for (uint32_t k = tp.vertEdgeOffsets[v]; k < tp.vertEdgeOffsets[v + 1]; ++k) {
            const glm::uvec2 f = tp.edgeFaces[tp.vertEdges[k]];
            polys[n++] = f.x;
            if (f.y != kNone) polys[n++] = f.y;
        }
        sortSmall(polys, n);
        n = uniqueSmall(polys, n);
        for (uint32_t k = 0; k < n; ++k) {
            const uint32_t p = polys[k], o = tp.polyOffsets[p], sides = tp.polyOffsets[p + 1] - o;
            uint32_t i = 0;
            while (i + 1 < sides && tp.polyVerts[o + i] != v) ++i;
            if (tp.catmullClark) {
                add(2 * size_t(o + i));
                add(2 * size_t(o + i) + 1);
            } else {
                add(4 * size_t(p) + i);
            }
        }
    } else if (v < nv + ne) {
        const uint32_t e = v - nv;
        const glm::uvec2 f = tp.edgeFaces[e];
        const uint32_t polys[2] = {std::min(f.x, f.y), f.y == kNone ? kNone : std::max(f.x, f.y)};
        for (uint32_t p : polys) {
            if (p == kNone) break;
            const uint32_t o = tp.polyOffsets[p], sides = tp.polyOffsets[p + 1] - o;
            uint32_t s = 0;
            while (s + 1 < sides && tp.polyEdges[o + s] != e) ++s;
            if (tp.catmullClark) {
                const size_t a = 2 * size_t(o + s), b = 2 * size_t(o + (s + 1) % sides) + 1;
                add(std::min(a, b));
                add(std::max(a, b));
            } else {
                const size_t a = 4 * size_t(p) + s, b = 4 * size_t(p) + (s + 1) % 3;
                add(std::min(a, b));
                add(std::max(a, b));
                add(4 * size_t(p) + 3);
            }
        }
    } else {
        const uint32_t p = v - nv - ne;
        for (size_t t = tp.firstFineTriangle(p), end = tp.firstFineTriangle(p + 1); t < end; ++t) add(t);
    }
    return r;
}

IndexRing Mesh::quadNeighbors(uint32_t v) const {
    const IndexRing ring = neighbors(v);
    if (ring.size() > IndexRing::kCapacity) return ring; // (a fan this wide is no quad mesh)
    // The far ends of the diagonals at v (at most one per triangle around it).
    uint32_t skip[IndexRing::kCapacity];
    uint32_t ns = 0;
    for (uint32_t t : vertexTriangles(v)) {
        const uint32_t u = quadPartnerAt(t);
        if (u == kNoQuad) continue;
        const int e = quadDiagonal(t, u);
        if (e < 0) continue;
        const glm::uvec3& tri = triangles[t];
        const uint32_t a = tri[e], b = tri[(e + 1) % 3];
        if (a == v || b == v) skip[ns++ % IndexRing::kCapacity] = a == v ? b : a;
    }
    if (ns == 0) return ring;
    IndexRing r;
    uint32_t n = 0;
    for (uint32_t w : ring) {
        bool diagonal = false;
        for (uint32_t i = 0; i < std::min<uint32_t>(ns, IndexRing::kCapacity) && !diagonal; ++i) diagonal = skip[i] == w;
        if (!diagonal) r.buf_[n++] = w;
    }
    r.size_ = n;
    return n ? r : ring;
}

IndexRing Mesh::implicitNeighbors(uint32_t v) const {
    // The ring follows from the same layout: Catmull-Clark joins a corner to the edge points of its edges and the
    // face points of its polygons, an edge point to the edge's ends and face points, and a face point to the
    // polygon's corners and edge points; Loop joins a corner to its edges' points, and an edge point to the
    // edge's ends and the other edge points of its two triangles. Corners < edge points < face points, so each
    // group is sorted on its own.
    const ImplicitAdjacency& ia = *implicit_;
    const SubdivTopology& tp = *ia.topo;
    const uint32_t nv = tp.coarseVertices, ne = uint32_t(tp.edges.size()), nf = nv + ne;
    constexpr uint32_t kNone = SubdivTopology::kNone;
    IndexRing r;
    uint32_t* out = r.buf_;
    uint32_t n = 0;
    if (v < nv) {
        const uint32_t k0 = tp.vertEdgeOffsets[v], k1 = tp.vertEdgeOffsets[v + 1];
        for (uint32_t k = k0; k < k1; ++k) out[n++] = nv + tp.vertEdges[k]; // ascending (listed in edge order)
        if (tp.catmullClark && ia.sides) { // one corner per polygon around v, ascending
            for (uint32_t k = ia.cornerOffsets[v]; k < ia.cornerOffsets[v + 1]; ++k) out[n++] = nf + ia.corners[k] / 4;
        } else if (tp.catmullClark) {
            // Each polygon is listed by both its edges at v: gathered apart (up to 2 x valence) and made unique
            // before joining the ring, which then holds valence + polygons <= 2 x valence (#16: gathered after
            // the edge points, a fan centre of 22 triangles needed 66 slots and overran the ring).
            uint32_t faces[IndexRing::kCapacity];
            uint32_t m = 0;
            for (uint32_t k = k0; k < k1; ++k) {
                const glm::uvec2 f = tp.edgeFaces[tp.vertEdges[k]];
                faces[m++] = nf + f.x;
                if (f.y != kNone) faces[m++] = nf + f.y;
            }
            sortSmall(faces, m);
            m = uniqueSmall(faces, m);
            std::copy(faces, faces + m, out + n);
            n += m;
        }
    } else if (v < nf) {
        const uint32_t e = v - nv;
        const glm::uvec2 ends = tp.edges[e], f = tp.edgeFaces[e];
        out[n++] = ends.x;
        out[n++] = ends.y;
        if (tp.catmullClark) {
            if (f.y == kNone) {
                out[n++] = nf + f.x;
            } else {
                out[n++] = nf + std::min(f.x, f.y);
                out[n++] = nf + std::max(f.x, f.y);
            }
        } else if (ia.sides) {
            uint32_t* others = out + n;
            uint32_t m = 0;
            const glm::uvec2 sides = ia.edgeSides[e];
            for (uint32_t q : {sides.x, sides.y}) {
                if (q == kNone) continue;
                const uint32_t q1 = ia.next(q);
                others[m++] = nv + tp.polyEdges[q1];
                others[m++] = nv + tp.polyEdges[ia.next(q1)];
            }
            sortSmall(others, m);
            n += uniqueSmall(others, m);
        } else {
            uint32_t* others = out + n;
            uint32_t m = 0;
            for (uint32_t p : {f.x, f.y}) {
                if (p == kNone) continue;
                const uint32_t o = tp.polyOffsets[p];
                for (uint32_t s = 0; s < 3; ++s)
                    if (tp.polyEdges[o + s] != e) others[m++] = nv + tp.polyEdges[o + s];
            }
            sortSmall(others, m);
            n += uniqueSmall(others, m);
        }
    } else {
        const uint32_t p = v - nf, o = ia.sides == 4 ? 4 * p : tp.polyOffsets[p], sides = ia.sides == 4 ? 4 : tp.polyOffsets[p + 1] - o;
        if (sides == 4) {
            const uint32_t* c = &tp.polyVerts[o];
            const uint32_t* e = &tp.polyEdges[o];
            sort4(out, c[0], c[1], c[2], c[3]);
            sort4(out + 4, nv + e[0], nv + e[1], nv + e[2], nv + e[3]);
            r.size_ = 8;
            return r;
        }
        for (uint32_t i = 0; i < sides; ++i) out[n++] = tp.polyVerts[o + i];
        sortSmall(out, sides);
        for (uint32_t i = 0; i < sides; ++i) out[n++] = nv + tp.polyEdges[o + i];
        sortSmall(out + sides, sides);
    }
    r.size_ = n;
    return r;
}

void Mesh::growDirtyBlocks(size_t blocks) {
    if (dirtyBlocks.size() < blocks) dirtyBlocks.resize(blocks, 0);
    if (dirtyBits.size() < blocks * (kDirtyBlock / 64)) dirtyBits.resize(blocks * (kDirtyBlock / 64), 0);
}

void Mesh::markDirtyRange(uint32_t begin, uint32_t end) {
    if (begin >= end) return;
    dirtyBegin = std::min(dirtyBegin, begin);
    dirtyEnd = std::max(dirtyEnd, end);
    const size_t last = (end - 1) >> kDirtyBlockShift;
    growDirtyBlocks(last + 1);
    std::fill(dirtyBlocks.begin() + (begin >> kDirtyBlockShift), dirtyBlocks.begin() + last + 1, uint8_t(1));
    // Bits: the partial words at both ends, whole words between.
    const size_t w0 = begin >> 6, w1 = (end - 1) >> 6;
    const uint64_t head = ~uint64_t(0) << (begin & 63), tail = ~uint64_t(0) >> (63 - ((end - 1) & 63));
    if (w0 == w1) {
        dirtyBits[w0] |= head & tail;
        return;
    }
    dirtyBits[w0] |= head;
    std::fill(dirtyBits.begin() + w0 + 1, dirtyBits.begin() + w1, ~uint64_t(0));
    dirtyBits[w1] |= tail;
}

void Mesh::clearDirty() {
    if (dirtyBegin < dirtyEnd) {
        constexpr size_t kWords = kDirtyBlock / 64;
        const size_t b = dirtyBegin >> kDirtyBlockShift, e = std::min(dirtyBlocks.size(), size_t((dirtyEnd - 1) >> kDirtyBlockShift) + 1);
        for (size_t i = b; i < e; ++i) {
            if (!dirtyBlocks[i]) continue;
            dirtyBlocks[i] = 0;
            std::fill(dirtyBits.begin() + std::ptrdiff_t(i * kWords), dirtyBits.begin() + std::ptrdiff_t((i + 1) * kWords), 0);
        }
    }
    dirtyBegin = std::numeric_limits<uint32_t>::max();
    dirtyEnd = 0;
    topologyDirty = false;
    facesDirty = false;
    changedTriangles.clear();
}

void Mesh::recomputeAllNormals() {
    // Area-weighted face normals summed in triangle order (each vertex's sum in the same order as
    // updateNormals adds them); the faces and the normalising run in parallel.
    const bool listed = vtOffsets_.size() == positions.size() + 1 && (!vtEnds_.empty() || vtList_.size() == triangles.size() * 3);
    if (implicit_ || listed) {
        // With the topology: each vertex sums its own triangles (listed in ascending order, so in the same order
        // as the serial sum below, and to the same bits), in parallel, without a face array (12 bytes a triangle).
        normals.resize(positions.size());
        poolFor(positions.size(), [&](size_t b, size_t e) {
            for (size_t v = b; v < e; ++v) {
                glm::vec3 sum(0.0f);
                for (uint32_t t : vertexTriangles(uint32_t(v))) sum += faceNormal(positions, triangles[t]);
                const float len = glm::length(sum);
                normals[v] = len > 1e-20f ? sum / len : glm::vec3(0, 1, 0);
            }
        }, 1 << 14);
        markAllDirty();
        return;
    }
    std::vector<glm::vec3> faces(triangles.size());
    poolFor(triangles.size(), [&](size_t b, size_t e) {
        for (size_t i = b; i < e; ++i) faces[i] = faceNormal(positions, triangles[i]);
    }, 1 << 16);
    normals.assign(positions.size(), glm::vec3(0.0f));
    for (size_t i = 0; i < triangles.size(); ++i)
        for (int c = 0; c < 3; ++c) normals[triangles[i][c]] += faces[i];
    poolFor(normals.size(), [&](size_t b, size_t e) {
        for (size_t v = b; v < e; ++v) {
            glm::vec3& n = normals[v];
            float len = glm::length(n);
            n = len > 1e-20f ? n / len : glm::vec3(0, 1, 0);
        }
    }, 1 << 16);
    markAllDirty();
}

void Mesh::updateNormals(std::span<const uint32_t> verts, float* curvature) {
    // The vertices whose normal changes: the moved ones and every neighbour, gathered as a set and listed in
    // index order; then each one's normal from its faces, in its triangle order (as recomputeAllNormals sums
    // them). The set, and every normal computed from it, doesn't depend on the thread count.
    if (!beginNormalSet()) return;
    normalBits_.insertParallel(verts.size(), 4096, [&](size_t b, size_t e, IndexBits& set) {
        for (size_t i = b; i < e; ++i) {
            const uint32_t v = verts[i];
            set.insertLocal(v);
            for (uint32_t u : neighbors(v)) set.insertLocal(u);
        }
    });
    finishNormals(curvature);
}

void Mesh::updateNormalsAround(size_t count, size_t grain, const std::function<void(size_t, size_t, IndexBits&)>& fill,
                               float* curvature) {
    if (!beginNormalSet()) return;
    normalBits_.insertParallel(count, grain, fill);
    finishNormals(curvature);
}

void Mesh::markDirtyAround(size_t count, size_t grain, const std::function<void(size_t, size_t, IndexBits&)>& fill) {
    if (!beginNormalSet()) return;
    normalBits_.insertParallel(count, grain, fill);
    finishNormals(nullptr, false);
}

float Mesh::curvatureAt(uint32_t v) const {
    if (v >= positions.size() || v >= normals.size()) return 0.0f;
    const glm::vec3 p = positions[v];
    glm::vec3 around(0.0f);
    float edges = 0.0f;
    uint32_t corners = 0;
    for (uint32_t ti : vertexTriangles(v)) {
        const glm::uvec3& t = triangles[ti];
        for (int k = 0; k < 3; ++k)
            if (t[k] != v) {
                const glm::vec3& q = positions[t[k]];
                around += q;
                edges += glm::distance(q, p);
                ++corners;
            }
    }
    return corners && edges > 0.0f ? glm::dot(normals[v], around - p * float(corners)) / edges : 0.0f;
}

bool Mesh::beginNormalSet() {
    const size_t nv = positions.size();
    if (!implicit_ && adjOffsets_.size() != nv + 1) return false; // no topology
    growDirtyBlocks((nv + kDirtyBlock - 1) / kDirtyBlock);
    if (normalBits_.capacity() < nv) normalBits_.resize(nv);
    return true;
}

void Mesh::finishNormals(float* curvature, bool computeNormals) {
    // Listing the set also marks it dirty: its words are laid out like dirtyBits, and each 2048-vertex dirty
    // block lies in one of its chunks (so no two threads write the same word).
    static_assert(IndexBits::kChunk % kDirtyBlock == 0);
    scratchVerts_.clear();
    normalBits_.take(scratchVerts_, [&](size_t word, uint64_t bits) {
        dirtyBits[word] |= bits;
        dirtyBlocks[(word * 64) >> kDirtyBlockShift] = 1;
    });
    if (scratchVerts_.empty()) return;
    dirtyBegin = std::min(dirtyBegin, scratchVerts_.front());
    dirtyEnd = std::max(dirtyEnd, scratchVerts_.back() + 1);
    if (!computeNormals) return;
    poolFor(scratchVerts_.size(), [&](size_t b, size_t e) {
        for (size_t i = b; i < e; ++i) {
            // The faces around later vertices are scattered over memory: ask for them ahead (their triangles
            // 16 vertices ahead, by 8 ahead those triangles' corners).
            if (i + 16 < e)
                for (uint32_t ti : vertexTriangles(scratchVerts_[i + 16])) prefetch(&triangles[ti]);
            if (i + 8 < e)
                for (uint32_t ti : vertexTriangles(scratchVerts_[i + 8])) {
                    const glm::uvec3& t = triangles[ti];
                    prefetch(&positions[t.x]);
                    prefetch(&positions[t.y]);
                    prefetch(&positions[t.z]);
                }
            const uint32_t v = scratchVerts_[i];
            glm::vec3 sum(0.0f);
            if (!curvature) {
                for (uint32_t ti : vertexTriangles(v)) sum += faceNormal(positions, triangles[ti]);
                const float len = glm::length(sum);
                if (len > 1e-20f) normals[v] = sum / len;
                continue;
            }
            // With the cavity term (curvatureAt) from the same corners.
            const glm::vec3 p = positions[v];
            glm::vec3 around(0.0f);
            float edges = 0.0f;
            uint32_t corners = 0;
            for (uint32_t ti : vertexTriangles(v)) {
                const glm::uvec3& t = triangles[ti];
                sum += faceNormal(positions, t);
                for (int k = 0; k < 3; ++k)
                    if (t[k] != v) {
                        const glm::vec3& q = positions[t[k]];
                        around += q;
                        edges += glm::distance(q, p);
                        ++corners;
                    }
            }
            const float len = glm::length(sum);
            if (len > 1e-20f) normals[v] = sum / len;
            curvature[v] = corners && edges > 0.0f ? glm::dot(normals[v], around - p * float(corners)) / edges : 0.0f;
        }
    }, 4096);
}

int Mesh::addLayer(std::string name) {
    if (layers.empty()) base = positions;
    layers.push_back({std::move(name), std::vector<glm::vec3>(positions.size(), glm::vec3(0.0f)), 1.0f});
    return static_cast<int>(layers.size()) - 1;
}

void Mesh::removeLayer(int index) {
    if (index < 0 || index >= static_cast<int>(layers.size())) return;
    layers.erase(layers.begin() + index);
    if (layers.empty()) {
        positions = base;
        base.clear();
    } else {
        evaluateAll();
    }
    recomputeAllNormals();
}

void Mesh::setLayerWeight(int index, float weight) {
    if (index < 0 || index >= static_cast<int>(layers.size())) return;
    layers[index].weight = weight;
    evaluateAll();
    recomputeAllNormals();
}

std::vector<glm::vec3>& Mesh::layerTarget(int layer) {
    if (layers.empty()) return positions;
    if (layer < 0 || layer >= static_cast<int>(layers.size())) return base;
    return layers[layer].delta;
}

float Mesh::layerTargetWeight(int layer) const {
    if (layers.empty() || layer < 0 || layer >= static_cast<int>(layers.size())) return 1.0f;
    return layers[layer].weight;
}

void Mesh::evaluate(std::span<const uint32_t> verts) {
    if (layers.empty()) return;
    for (uint32_t v : verts) {
        glm::vec3 p = base[v];
        for (const auto& l : layers) p += l.weight * l.delta[v];
        positions[v] = p;
    }
}

void Mesh::evaluateAll() {
    if (layers.empty()) return;
    for (size_t v = 0; v < positions.size(); ++v) {
        glm::vec3 p = base[v];
        for (const auto& l : layers) p += l.weight * l.delta[v];
        positions[v] = p;
    }
}

AABB Mesh::bounds() const {
    AABB b;
    for (const auto& p : positions) b.expand(p);
    return b;
}

size_t Mesh::memoryBytes() const {
    auto bytes = [](const auto& v) { return v.capacity() * sizeof(v[0]); };
    size_t n = bytes(positions) + bytes(normals) + bytes(triangles) + bytes(mask) + bytes(colors) + bytes(polygroups) +
               bytes(hidden) + bytes(uvs) + bytes(uvFlags) + bytes(quadPartners) + bytes(base) + bytes(paintBase) + bytes(adjOffsets_) +
               bytes(adjList_) + bytes(vtOffsets_) + bytes(vtList_) + bytes(adjEnds_) + bytes(adjCaps_) + bytes(vtEnds_) + bytes(vtCaps_) + normalBits_.memoryBytes() + bytes(scratchVerts_) + bytes(dirtyBlocks) + bytes(dirtyBits);
    for (const auto& l : layers) n += bytes(l.delta);
    for (const auto& l : paintLayers) n += bytes(l.color) + bytes(l.alpha);
    if (implicit_) n += implicit_->bytes(); // (the level topology itself is counted with the subdivision levels)
    return n;
}

static void laplacianStep(Mesh& m, float lambda, std::vector<glm::vec3>& next) {
    next.resize(m.vertexCount());
    for (uint32_t v = 0; v < m.vertexCount(); ++v) {
        auto ring = m.neighbors(v);
        if (ring.empty()) {
            next[v] = m.positions[v];
            continue;
        }
        glm::vec3 avg(0.0f);
        for (uint32_t n : ring) avg += m.positions[n];
        avg /= float(ring.size());
        next[v] = glm::mix(m.positions[v], avg, lambda);
    }
    m.positions.swap(next);
}

void taubinSmooth(Mesh& m, int iterations) {
    std::vector<glm::vec3> next;
    for (int it = 0; it < iterations; ++it) {
        laplacianStep(m, 0.5f, next);
        laplacianStep(m, -0.53f, next);
    }
}

Mesh weldImported(const Mesh& m, const std::vector<SculptLayer>& layers) {
    AABB box = m.bounds();
    const float tol = box.valid() ? std::max(glm::length(box.extent()) * 1e-6f, 1e-12f) : 1.0f;
    struct KeyHash {
        size_t operator()(const glm::ivec3& k) const {
            return size_t(k.x) * 73856093u ^ size_t(k.y) * 19349663u ^ size_t(k.z) * 83492791u;
        }
    };
    std::unordered_map<glm::ivec3, uint32_t, KeyHash> unique;
    unique.reserve(m.positions.size());
    std::vector<uint32_t> remap(m.positions.size());
    std::vector<uint32_t> kept;
    // Quantise relative to the box corner and clamp, so far-off, infinite or NaN coordinates from a malformed file
    // never overflow the int conversion.
    const glm::vec3 origin = box.valid() ? box.min : glm::vec3(0.0f);
    auto quantise = [&](float v) {
        return std::isfinite(v) ? int(std::clamp(std::round(v / tol), -2.0e9f, 2.0e9f)) : 0;
    };
    for (uint32_t v = 0; v < m.positions.size(); ++v) {
        const glm::vec3 d = m.positions[v] - origin;
        const glm::ivec3 key(quantise(d.x), quantise(d.y), quantise(d.z));
        auto [it, inserted] = unique.try_emplace(key, uint32_t(kept.size()));
        if (inserted) kept.push_back(v);
        remap[v] = it->second;
    }
    Mesh welded;
    for (uint32_t v : kept) welded.positions.push_back(m.positions[v]);
    const bool vMask = m.mask.size() == m.positions.size(), vColors = m.colors.size() == m.positions.size();
    for (uint32_t v : kept) {
        if (vMask) welded.mask.push_back(m.mask[v]);
        if (vColors) welded.colors.push_back(m.colors[v]);
    }
    const size_t nt = m.triangles.size();
    const bool tGroups = m.polygroups.size() == nt, tHidden = m.hidden.size() == nt, tUvs = m.uvs.size() == nt * 3;
    std::vector<uint32_t> newTriangle(m.hasQuads() ? nt : 0, Mesh::kNoQuad);
    for (size_t ti = 0; ti < nt; ++ti) {
        const auto& t = m.triangles[ti];
        glm::uvec3 nt3(remap[t.x], remap[t.y], remap[t.z]);
        if (nt3.x == nt3.y || nt3.y == nt3.z || nt3.x == nt3.z) continue;
        if (!newTriangle.empty()) newTriangle[ti] = uint32_t(welded.triangles.size());
        welded.triangles.push_back(nt3);
        if (tGroups) welded.polygroups.push_back(m.polygroups[ti]);
        if (tHidden) welded.hidden.push_back(m.hidden[ti]);
        if (tUvs) welded.uvs.insert(welded.uvs.end(), m.uvs.begin() + std::ptrdiff_t(ti * 3), m.uvs.begin() + std::ptrdiff_t(ti * 3 + 3));
    }
    if (!newTriangle.empty()) remapQuads(m, newTriangle, welded);
    if (!layers.empty()) {
        welded.base = welded.positions;
        for (const auto& src : layers) {
            SculptLayer layer{src.name, {}, src.weight};
            layer.delta.reserve(kept.size());
            for (uint32_t v : kept) layer.delta.push_back(v < src.delta.size() ? src.delta[v] : glm::vec3(0.0f));
            welded.layers.push_back(std::move(layer));
        }
        welded.evaluateAll();
    }
    welded.rebuildTopology();
    welded.recomputeAllNormals();
    return welded;
}

void remapQuads(const Mesh& src, const std::vector<uint32_t>& newIndex, Mesh& dst) {
    const size_t nt = src.triangleCount();
    if (src.quadPartners.size() != nt || newIndex.size() != nt) return;
    std::vector<uint32_t> partners(dst.triangleCount(), Mesh::kNoQuad);
    bool any = false;
    for (size_t t = 0; t < nt; ++t) {
        const uint32_t u = src.quadPartners[t], nt0 = newIndex[t];
        if (u >= nt || nt0 >= partners.size() || newIndex[u] >= partners.size()) continue;
        partners[nt0] = newIndex[u];
        any = true;
    }
    if (any) dst.quadPartners = std::move(partners);
    else dst.clearQuads();
}

void appendQuads(Mesh& dst, size_t firstTriangle, const Mesh& src) {
    const size_t st = src.triangleCount();
    const bool srcQuads = st > 0 && src.quadPartners.size() == st;
    if (!srcQuads && dst.quadPartners.empty()) return;
    dst.quadPartners.resize(firstTriangle, Mesh::kNoQuad);
    for (size_t t = 0; t < st; ++t) {
        const uint32_t u = srcQuads ? src.quadPartners[t] : Mesh::kNoQuad;
        dst.quadPartners.push_back(u == Mesh::kNoQuad ? u : uint32_t(u + firstTriangle));
    }
}

glm::vec3 srgbToLinear(const glm::vec3& c) {
    glm::vec3 out;
    for (int i = 0; i < 3; ++i) {
        const float x = std::clamp(c[i], 0.0f, 1.0f);
        out[i] = x <= 0.04045f ? x / 12.92f : std::pow((x + 0.055f) / 1.055f, 2.4f);
    }
    return out;
}

glm::vec3 linearToSrgb(const glm::vec3& c) {
    glm::vec3 out;
    for (int i = 0; i < 3; ++i) {
        const float x = std::clamp(c[i], 0.0f, 1.0f);
        out[i] = x <= 0.0031308f ? x * 12.92f : 1.055f * std::pow(x, 1.0f / 2.4f) - 0.055f;
    }
    return out;
}

void transferVertexAttributes(const Mesh& src, Mesh& dst) {
    const bool wantMask = src.mask.size() == src.positions.size() && !src.mask.empty();
    const bool wantColors = src.colors.size() == src.positions.size() && !src.colors.empty();
    if ((!wantMask && !wantColors) || dst.positions.empty()) return;

    // Uniform hash grid with about one source vertex per cell.
    const AABB box = src.bounds();
    const glm::vec3 ext = box.extent();
    const float longest = std::max({ext.x, ext.y, ext.z, 1e-12f});
    const float cell = std::max(longest / std::max(1.0f, std::cbrt(float(src.positions.size()))), longest * 1e-4f);
    constexpr int kOffset = 1 << 20;
    // Clamped so NaN, infinite or huge coordinates from a malformed file never overflow the int cast.
    auto cellOf = [&](const glm::vec3& p) {
        const glm::vec3 f = glm::floor((p - box.min) / cell);
        auto axis = [](float v) { return std::isfinite(v) ? int(std::clamp(v, -float(kOffset), float(kOffset))) : 0; };
        return glm::ivec3(axis(f.x), axis(f.y), axis(f.z));
    };
    auto key = [](const glm::ivec3& c) {
        return (uint64_t(uint32_t(c.x + kOffset) & 0x1fffffu) << 42) | (uint64_t(uint32_t(c.y + kOffset) & 0x1fffffu) << 21) |
               uint64_t(uint32_t(c.z + kOffset) & 0x1fffffu);
    };
    std::unordered_map<uint64_t, std::vector<uint32_t>> grid;
    grid.reserve(src.positions.size());
    for (uint32_t v = 0; v < src.positions.size(); ++v) grid[key(cellOf(src.positions[v]))].push_back(v);
    const int gridCells = int(std::ceil(longest / cell)) + 2;

    if (wantMask) dst.mask.assign(dst.positions.size(), 0.0f);
    if (wantColors) dst.colors.assign(dst.positions.size(), glm::vec3(1.0f));
    for (size_t i = 0; i < dst.positions.size(); ++i) {
        const glm::vec3 p = dst.positions[i];
        const glm::ivec3 c = cellOf(p);
        const glm::vec3 outside = glm::max(glm::max(box.min - p, p - box.max), glm::vec3(0.0f));
        const int maxRing = gridCells + int(std::ceil(glm::length(outside) / cell));
        float best = std::numeric_limits<float>::max();
        uint32_t bestV = 0;
        // Search shells of growing Chebyshev radius; stop once nothing closer can remain.
        for (int r = 0; r <= maxRing; ++r) {
            for (int dz = -r; dz <= r; ++dz)
                for (int dy = -r; dy <= r; ++dy)
                    for (int dx = -r; dx <= r; ++dx) {
                        if (std::max({std::abs(dx), std::abs(dy), std::abs(dz)}) != r) continue;
                        auto it = grid.find(key(c + glm::ivec3(dx, dy, dz)));
                        if (it == grid.end()) continue;
                        for (uint32_t v : it->second) {
                            const glm::vec3 d = src.positions[v] - p;
                            const float d2 = glm::dot(d, d);
                            if (d2 < best) {
                                best = d2;
                                bestV = v;
                            }
                        }
                    }
            if (best < std::numeric_limits<float>::max() && std::sqrt(best) <= float(r) * cell) break;
        }
        if (wantMask) dst.mask[i] = src.mask[bestV];
        if (wantColors) dst.colors[i] = src.colors[bestV];
    }
    dst.markAllDirty();
}

} // namespace sf
