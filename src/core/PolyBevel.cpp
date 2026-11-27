// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Bevel (core/PolyBevel.h). The work is organised around the touched vertices: every end of a selected edge gets
// its fan of edges and faces in cyclic order, the new points around it (anchors: a cut sliding along an unselected
// edge, or the corner where two cuts meet in a face), then the strip ends (the profile between two anchors) and
// the patch that joins the strips. Faces are rebuilt from their corners: a corner at a touched vertex becomes the
// vertex's path through that face. Every new point carries weights over original vertices (for mask, paint and
// layers) and gets its UVs from the face it lies in.
#include "core/PolyBevel.h"

#include "core/EdgeSelect.h"
#include "core/FaceGroups.h"
#include "core/PolyOps.h"
#include "core/Uv.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <limits>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace sf {

namespace {

constexpr uint32_t kNone = ~0u;

// A polygon of the input: a triangle, or a quad pair in winding order. corner[i] is the triangle corner whose
// vertex is vert[i] and whose edge goes to vert[i + 1]; edge[i] is that edge of the EdgeTopology.
struct Poly {
    int n = 0;
    uint32_t tri[2] = {kNone, kNone};
    uint32_t vert[4] = {kNone, kNone, kNone, kNone};
    uint32_t corner[4] = {kNone, kNone, kNone, kNone};
    uint32_t edge[4] = {kNone, kNone, kNone, kNone};
    int indexOf(uint32_t v) const {
        for (int i = 0; i < n; ++i)
            if (vert[i] == v) return i;
        return -1;
    }
};

// The edges and faces around a vertex in cyclic order: faces[i] lies between edges[i] and edges[(i + 1) % n].
// An open fan has one virtual face (kNone) between its last and first (border) edge.
struct Fan {
    bool valid = false;
    std::vector<uint32_t> edges, faces;
};

// A new or kept vertex of the output.
struct Point {
    glm::vec3 pos{0.0f};
    std::vector<std::pair<uint32_t, float>> weights; // over input vertices
};

// Per touched vertex.
struct VertexWork {
    Fan fan;
    int k = 0;                      // selected edges in the fan
    std::vector<uint8_t> sel;       // per fan edge
    std::vector<float> slideK;      // per fan edge: the cut slides k * width along it (0 = no anchor there)
    std::vector<uint8_t> hasCorner; // per fan face: both of its edges are selected
    std::vector<glm::vec3> cornerDir; // per fan face: the corner anchor is v + width * cornerDir
    std::vector<uint32_t> slideId;    // per fan edge: the anchor's output vertex (kNone)
    std::vector<uint32_t> cornerId;   // per fan face
    std::vector<std::vector<uint32_t>> path; // per fan face: the vertex's corner in that face, in the face's order
    std::vector<uint32_t> loopFwd;    // the patch boundary in fan order (wound like the faces, so reversed on output)
    std::vector<std::vector<uint32_t>> profs, conns; // per selected edge in fan order: its forward profile, then the gap's points
};

// One end of a strip: the profile from the anchor on side a (where the edge runs tail -> head) to the one on
// side b, with an extra point (the kept vertex) possibly inserted after index `extraAfter`.
struct StripEnd {
    std::vector<uint32_t> ids;
    int extraAfter = -1;
};

struct Strip {
    uint32_t edge = kNone;
    uint32_t tail = kNone, head = kNone; // the edge runs tail -> head in polygon a
    uint32_t a = kNone, b = kNone;       // polygons on either side
    StripEnd end[2];                     // [0] at the tail, [1] at the head
};

glm::vec3 newellNormal(const std::vector<glm::vec3>& p) {
    glm::vec3 n(0.0f);
    for (size_t i = 0, m = p.size(); i < m; ++i) {
        const glm::vec3& a = p[i];
        const glm::vec3& b = p[(i + 1) % m];
        n += glm::cross(a, b);
    }
    return n;
}

// Ear clipping of a small polygon around its own normal. Fills `tris` with index triples into `p`; skips
// zero-area ears while a better one exists. Returns false when the polygon has no area at all.
bool triangulate(const std::vector<glm::vec3>& p, std::vector<std::array<int, 3>>& tris) {
    tris.clear();
    const size_t m = p.size();
    if (m < 3) return false;
    glm::vec3 n = newellNormal(p);
    const float len = glm::length(n);
    if (len < 1e-12f) return false;
    n /= len;
    const glm::vec3 ax = std::abs(n.x) < 0.9f ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);
    const glm::vec3 u = glm::normalize(glm::cross(n, ax)), v = glm::cross(n, u);
    std::vector<glm::vec2> q(m);
    for (size_t i = 0; i < m; ++i) q[i] = {glm::dot(p[i], u), glm::dot(p[i], v)};
    std::vector<int> idx(m);
    for (size_t i = 0; i < m; ++i) idx[i] = int(i);
    auto cross2 = [](glm::vec2 a, glm::vec2 b, glm::vec2 c) { return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x); };
    float scale = 0.0f;
    for (size_t i = 0; i < m; ++i) scale = std::max(scale, glm::length(q[i] - q[0]));
    const float areaEps = 1e-7f * scale * scale;
    while (idx.size() > 3) {
        const size_t c = idx.size();
        int best = -1;
        float bestScore = -1.0f;
        for (size_t i = 0; i < c; ++i) {
            const int ia = idx[(i + c - 1) % c], ib = idx[i], ic = idx[(i + 1) % c];
            const float area = cross2(q[ia], q[ib], q[ic]);
            if (area <= areaEps) continue; // reflex or flat corner
            bool empty = true;
            for (size_t j = 0; j < c && empty; ++j) {
                const int id = idx[j];
                if (id == ia || id == ib || id == ic) continue;
                if (q[id] == q[ia] || q[id] == q[ib] || q[id] == q[ic]) continue; // coincident points never block
                // inside or on an edge of the ear blocks it: a point on the edge would be left in no triangle
                if (cross2(q[ia], q[ib], q[id]) >= -areaEps && cross2(q[ib], q[ic], q[id]) >= -areaEps &&
                    cross2(q[ic], q[ia], q[id]) >= -areaEps)
                    empty = false;
            }
            if (!empty) continue;
            // Prefer well-shaped ears: area over the squared longest side.
            const float l = std::max({glm::dot(q[ib] - q[ia], q[ib] - q[ia]), glm::dot(q[ic] - q[ib], q[ic] - q[ib]), glm::dot(q[ia] - q[ic], q[ia] - q[ic])});
            const float score = area / std::max(l, 1e-20f);
            if (score > bestScore) bestScore = score, best = int(i);
        }
        if (best < 0) {
            // No clean ear (a degenerate or self-touching polygon): fan the rest from a corner with area.
            for (size_t i = 1; i + 1 < c; ++i)
                if (std::abs(cross2(q[idx[0]], q[idx[i]], q[idx[i + 1]])) > areaEps) tris.push_back({idx[0], idx[i], idx[i + 1]});
            return !tris.empty();
        }
        tris.push_back({idx[(best + c - 1) % c], idx[best], idx[(best + 1) % c]});
        idx.erase(idx.begin() + best);
    }
    if (std::abs(cross2(q[idx[0]], q[idx[1]], q[idx[2]])) > areaEps) tris.push_back({idx[0], idx[1], idx[2]});
    return !tris.empty();
}

// Barycentric weights of `p` (projected) over triangle abc; negative outside.
glm::vec3 barycentric(const glm::vec3& p, const glm::vec3& a, const glm::vec3& b, const glm::vec3& c) {
    const glm::vec3 v0 = b - a, v1 = c - a, v2 = p - a;
    const float d00 = glm::dot(v0, v0), d01 = glm::dot(v0, v1), d11 = glm::dot(v1, v1);
    const float d20 = glm::dot(v2, v0), d21 = glm::dot(v2, v1);
    const float den = d00 * d11 - d01 * d01;
    if (std::abs(den) < 1e-20f) return {1.0f, 0.0f, 0.0f};
    const float w1 = (d11 * d20 - d01 * d21) / den, w2 = (d00 * d21 - d01 * d20) / den;
    return {1.0f - w1 - w2, w1, w2};
}

// Superellipse profile parameter: exponent from the profile slider (0.25 flat, 0.5 round, 1 nearly square).
float profileExponent(float profile) { return std::pow(2.0f, 4.0f * std::clamp(profile, 0.0f, 1.0f) - 1.0f); }

// The point at parameter t (0 = A, 1 = B) of the profile between A and B around the corner E.
glm::vec3 profilePoint(const glm::vec3& A, const glm::vec3& B, const glm::vec3& E, float exponent, float t) {
    const glm::vec3 c = A + B - E;
    const float th = t * 1.5707963f;
    const float fx = std::pow(std::max(std::cos(th), 0.0f), 2.0f / exponent);
    const float fy = std::pow(std::max(std::sin(th), 0.0f), 2.0f / exponent);
    return c + (A - c) * fx + (B - c) * fy;
}

// Least-squares sphere through points; false when ill-conditioned or when the points don't lie on it.
bool fitSphere(const std::vector<glm::vec3>& pts, glm::vec3& centre, float& radius, float tolerance) {
    if (pts.size() < 4) return false;
    glm::dvec3 mean(0.0);
    for (const glm::vec3& p : pts) mean += glm::dvec3(p);
    mean /= double(pts.size());
    // Minimise sum (|p - c|^2 - r^2)^2 with p centred: linear in c (2 (p - mean) . c = |p - mean|^2 - mean of that).
    glm::dmat3 ata(0.0);
    glm::dvec3 atb(0.0);
    double meanSq = 0.0;
    std::vector<glm::dvec3> d(pts.size());
    for (size_t i = 0; i < pts.size(); ++i) d[i] = glm::dvec3(pts[i]) - mean, meanSq += glm::dot(d[i], d[i]);
    meanSq /= double(pts.size());
    for (const glm::dvec3& x : d) {
        const double rhs = 0.5 * (glm::dot(x, x) - meanSq);
        ata += glm::outerProduct(x, x);
        atb += x * rhs;
    }
    const double det = glm::determinant(ata);
    double scale = 0.0;
    for (const glm::dvec3& x : d) scale = std::max(scale, glm::dot(x, x));
    if (std::abs(det) < 1e-9 * scale * scale * scale) return false;
    const glm::dvec3 c = glm::inverse(ata) * atb;
    double r = 0.0;
    for (const glm::dvec3& x : d) r += glm::length(x - c);
    r /= double(pts.size());
    if (!(r > 0.0) || !std::isfinite(r)) return false;
    for (const glm::dvec3& x : d)
        if (std::abs(glm::length(x - c) - r) > tolerance * r) return false;
    centre = glm::vec3(mean + c);
    radius = float(r);
    return true;
}

class Bevel {
public:
    Bevel(const Mesh& in, const PolyBevelOptions& opt) : in_(in), opt_(opt) {}

    bool run(std::span<const glm::uvec2> edges, std::span<const uint32_t> vertices, Mesh& out, PolyBevelResult& result,
             std::string& error);

private:
    // --- input structure ---
    void buildPolygons();
    bool buildFan(uint32_t v, Fan& fan) const;
    // weights of a point over polygon p's corners
    std::array<float, 4> polygonWeights(uint32_t p, const glm::vec3& pos) const;
    glm::vec2 polygonUv(uint32_t p, const glm::vec3& pos) const;
    std::vector<std::pair<uint32_t, float>> polygonVertexWeights(uint32_t p, const glm::vec3& pos) const;

    // --- planning ---
    bool selectEdges(std::span<const glm::uvec2> edges, std::string& error);
    bool selectVertices(std::span<const uint32_t> vertices, std::string& error);
    void planVertex(uint32_t v, VertexWork& w);
    float widthLimit() const;

    // --- building ---
    uint32_t addPoint(const glm::vec3& pos, std::vector<std::pair<uint32_t, float>> weights);
    uint32_t blendPoint(uint32_t a, uint32_t b, float t);
    void makeAnchors();
    void makeStrips();
    void makeVertexPaths();
    void makePatches(uint32_t v, VertexWork& w);
    void makeVertexBevelPatch(uint32_t v, VertexWork& w);
    void rebuildFaces();
    void emitStripFaces();

    // Output polygons: ids, per-corner UVs, group, hidden, bevel flag.
    void emitPolygon(std::vector<uint32_t> ids, std::vector<glm::vec2> uvs, uint32_t group, bool hidden, bool bevel);
    void assemble(Mesh& out, PolyBevelResult& result);

    const Mesh& in_;
    PolyBevelOptions opt_;
    float width_ = 0.0f, exponent_ = 2.0f;
    int segments_ = 1;
    std::shared_ptr<const EdgeTopology> topo_;
    std::vector<Poly> polys_;
    std::vector<uint32_t> polyOf_; // per triangle
    std::vector<uint8_t> selected_; // per edge
    std::vector<uint32_t> stripOf_; // per edge: index into strips_ (kNone)
    std::vector<Strip> strips_;
    std::unordered_map<uint32_t, VertexWork> work_; // touched vertices
    std::vector<uint8_t> touchedPoly_;
    uint32_t newGroup_ = 0;

    // output
    std::vector<Point> points_; // index = output vertex before compaction; the first in_.vertexCount() are the inputs
    std::vector<uint8_t> pointUsed_;
    struct OutTri {
        glm::uvec3 v;
        glm::vec2 uv[3];
        uint32_t group;
        uint8_t hidden;
        uint8_t bevel;
        uint32_t partner; // index of its quad partner in outTris_ (kNone)
        uint32_t srcTri;  // the input triangle it copies (kNone for new / rebuilt)
    };
    std::vector<OutTri> outTris_;
    PolyBevelStats stats_;
};

// ---------------------------------------------------------------------------------------------------------------

void Bevel::buildPolygons() {
    const size_t nt = in_.triangleCount();
    polyOf_.assign(nt, kNone);
    for (uint32_t t = 0; t < nt; ++t) {
        if (polyOf_[t] != kNone) continue;
        const uint32_t u = in_.quadPartnerAt(t);
        Poly p;
        if (u == Mesh::kNoQuad || u >= nt) {
            p.n = 3;
            p.tri[0] = t;
            for (int k = 0; k < 3; ++k) p.vert[k] = in_.triangles[t][k], p.corner[k] = t * 3 + k;
        } else {
            const int e = in_.quadDiagonal(t, u);
            if (e < 0) { // not a real pair: treat as two triangles
                p.n = 3;
                p.tri[0] = t;
                for (int k = 0; k < 3; ++k) p.vert[k] = in_.triangles[t][k], p.corner[k] = t * 3 + k;
            } else {
                p.n = 4;
                p.tri[0] = t, p.tri[1] = u;
                const glm::uvec3& T = in_.triangles[t];
                const glm::uvec3& U = in_.triangles[u];
                const int e1 = (e + 1) % 3, e2 = (e + 2) % 3;
                p.vert[0] = T[e1], p.corner[0] = t * 3 + e1;
                p.vert[1] = T[e2], p.corner[1] = t * 3 + e2;
                p.vert[2] = T[e];
                int far = -1, atE = -1;
                for (int k = 0; k < 3; ++k) {
                    if (U[k] == T[e]) atE = k;
                    else if (U[k] != T[e1]) far = k;
                }
                p.corner[2] = u * 3 + atE;
                p.vert[3] = U[far], p.corner[3] = u * 3 + far;
                polyOf_[u] = uint32_t(polys_.size());
            }
        }
        for (int k = 0; k < p.n; ++k) p.edge[k] = topo_->cornerEdge[p.corner[k]];
        polyOf_[t] = uint32_t(polys_.size());
        polys_.push_back(p);
    }
}


bool Bevel::buildFan(uint32_t v, Fan& fan) const {
    fan = Fan{};
    // The polygons at v with their in/out edges.
    std::vector<uint32_t> ps;
    for (uint32_t t : in_.vertexTriangles(v)) {
        const uint32_t p = polyOf_[t];
        if (std::find(ps.begin(), ps.end(), p) == ps.end()) ps.push_back(p);
    }
    if (ps.empty()) return false;
    std::unordered_map<uint32_t, uint32_t> after, before; // edge -> the polygon after / before it around v
    std::unordered_map<uint32_t, std::pair<uint32_t, uint32_t>> inOut;
    for (uint32_t p : ps) {
        const Poly& P = polys_[p];
        const int i = P.indexOf(v);
        if (i < 0) return false;
        const uint32_t eIn = P.edge[(i + P.n - 1) % P.n], eOut = P.edge[i];
        if (eIn == kNoEdge || eOut == kNoEdge) return false;
        if (after.count(eIn) || before.count(eOut)) return false; // non-manifold
        after[eIn] = p, before[eOut] = p;
        inOut[p] = {eIn, eOut};
    }
    // Find the start: a polygon whose in edge has nothing before it (open fan), else any.
    uint32_t start = ps[0];
    bool open = false;
    for (uint32_t p : ps)
        if (!before.count(inOut[p].first)) start = p, open = true;
    std::vector<uint32_t> faces, edges;
    uint32_t p = start;
    for (size_t guard = 0; guard <= ps.size(); ++guard) {
        faces.push_back(p);
        edges.push_back(inOut[p].first);
        auto it = after.find(inOut[p].second);
        if (it == after.end()) { // ran out: an open fan ends here
            if (!open) return false;
            edges.push_back(inOut[p].second);
            break;
        }
        p = it->second;
        if (p == start) break;
    }
    if (faces.size() != ps.size()) return false; // two separate fans share v
    if (open) {
        if (edges.size() != faces.size() + 1) return false;
        faces.push_back(kNone); // the virtual face between the two border edges
    } else if (edges.size() != faces.size()) {
        return false;
    }
    fan.edges = std::move(edges);
    fan.faces = std::move(faces);
    fan.valid = true;
    return true;
}

std::array<float, 4> Bevel::polygonWeights(uint32_t p, const glm::vec3& pos) const {
    const Poly& P = polys_[p];
    std::array<float, 4> w{0.0f, 0.0f, 0.0f, 0.0f};
    auto score = [](const glm::vec3& b) { return std::min({b.x, b.y, b.z}); };
    if (P.n == 3) {
        const glm::vec3 b = barycentric(pos, in_.positions[P.vert[0]], in_.positions[P.vert[1]], in_.positions[P.vert[2]]);
        w = {b.x, b.y, b.z, 0.0f};
    } else {
        // the quad's two triangles: (0, 1, 2) and (0, 2, 3) in polygon order (the diagonal 0-2 is vert[0]-vert[2])
        const glm::vec3 b0 = barycentric(pos, in_.positions[P.vert[0]], in_.positions[P.vert[1]], in_.positions[P.vert[2]]);
        const glm::vec3 b1 = barycentric(pos, in_.positions[P.vert[0]], in_.positions[P.vert[2]], in_.positions[P.vert[3]]);
        if (score(b0) >= score(b1)) w = {b0.x, b0.y, b0.z, 0.0f};
        else w = {b1.x, 0.0f, b1.y, b1.z};
    }
    // Affine (the weights sum to 1, negative outside the face): a point just outside a face, as a strip's profile
    // points are, keeps linear attributes exact; the mask and colours are clamped when they are written.
    for (float& x : w)
        if (!std::isfinite(x)) return {1.0f, 0.0f, 0.0f, 0.0f};
    return w;
}

glm::vec2 Bevel::polygonUv(uint32_t p, const glm::vec3& pos) const {
    if (!in_.hasUvs()) return {0.0f, 0.0f};
    const Poly& P = polys_[p];
    const std::array<float, 4> w = polygonWeights(p, pos);
    glm::vec2 uv(0.0f);
    for (int i = 0; i < P.n; ++i) uv += w[i] * in_.uvs[P.corner[i]];
    return uv;
}

std::vector<std::pair<uint32_t, float>> Bevel::polygonVertexWeights(uint32_t p, const glm::vec3& pos) const {
    const Poly& P = polys_[p];
    const std::array<float, 4> w = polygonWeights(p, pos);
    std::vector<std::pair<uint32_t, float>> r;
    for (int i = 0; i < P.n; ++i)
        if (w[i] != 0.0f) r.push_back({P.vert[i], w[i]});
    return r;
}

// ---------------------------------------------------------------------------------------------------------------

bool Bevel::selectEdges(std::span<const glm::uvec2> edges, std::string& error) {
    const size_t nc = in_.triangleCount() * 3;
    selected_.assign(topo_->edgeCount(), 0);
    size_t asked = 0;
    for (const glm::uvec2& pair : edges) {
        const uint32_t a = std::min(pair.x, pair.y), b = std::max(pair.x, pair.y);
        if (a == b || b >= in_.vertexCount()) {
            ++stats_.skipped;
            continue;
        }
        uint32_t e = kNoEdge;
        for (uint32_t i = topo_->vertexOffsets[a]; i < topo_->vertexOffsets[a + 1]; ++i)
            if (topo_->ends[topo_->vertexEdges[i]] == glm::uvec2(a, b)) e = topo_->vertexEdges[i];
        if (e == kNoEdge || selected_[e]) {
            if (e == kNoEdge) ++stats_.skipped;
            continue;
        }
        ++asked;
        const uint32_t c = topo_->edgeCorner[e], d = topo_->twin[c];
        if (d >= nc) { // open or non-manifold
            ++stats_.skipped;
            continue;
        }
        selected_[e] = 1;
    }
    // Fans at every end; edges at a vertex whose fan can't be rebuilt are dropped.
    for (uint32_t e = 0; e < selected_.size(); ++e) {
        if (!selected_[e]) continue;
        for (uint32_t v : {topo_->ends[e].x, topo_->ends[e].y}) {
            if (work_.count(v)) continue;
            VertexWork w;
            buildFan(v, w.fan);
            if (w.fan.valid && w.fan.edges.size() < 3) w.fan.valid = false; // a two-edge pillow has no corner to cut
            work_.emplace(v, std::move(w));
        }
    }
    for (uint32_t e = 0; e < selected_.size(); ++e) {
        if (!selected_[e]) continue;
        if (!work_[topo_->ends[e].x].fan.valid || !work_[topo_->ends[e].y].fan.valid) {
            selected_[e] = 0;
            ++stats_.skipped;
        }
    }
    for (auto it = work_.begin(); it != work_.end();) {
        bool any = false;
        if (it->second.fan.valid)
            for (uint32_t e : it->second.fan.edges) any |= selected_[e] != 0;
        it = any ? std::next(it) : work_.erase(it);
    }
    if (work_.empty()) {
        error = asked == 0 ? "Nothing to bevel: select edges first" : "Nothing to bevel: the selected edges are open or non-manifold";
        return false;
    }
    return true;
}

bool Bevel::selectVertices(std::span<const uint32_t> vertices, std::string& error) {
    selected_.assign(topo_->edgeCount(), 0);
    for (uint32_t v : vertices) {
        if (v >= in_.vertexCount() || work_.count(v)) continue;
        VertexWork w;
        buildFan(v, w.fan);
        if (w.fan.valid && w.fan.edges.size() < 3) w.fan.valid = false;
        if (!w.fan.valid) {
            ++stats_.skipped;
            continue;
        }
        work_.emplace(v, std::move(w));
    }
    if (work_.empty()) {
        error = vertices.empty() ? "Nothing to bevel: select vertices first" : "Nothing to bevel: the selected vertices are non-manifold";
        return false;
    }
    return true;
}

// The edge directions, slide factors and corner directions around v (all per unit width).
void Bevel::planVertex(uint32_t v, VertexWork& w) {
    const Fan& fan = w.fan;
    const size_t n = fan.edges.size();
    w.sel.assign(n, 0);
    w.slideK.assign(n, 0.0f);
    w.hasCorner.assign(n, 0);
    w.cornerDir.assign(n, glm::vec3(0.0f));
    w.slideId.assign(n, kNone);
    w.cornerId.assign(n, kNone);
    w.path.assign(n, {});
    w.k = 0;
    for (size_t i = 0; i < n; ++i) w.k += (w.sel[i] = selected_[fan.edges[i]]) != 0;
    const glm::vec3 pv = in_.positions[v];
    auto dir = [&](uint32_t e) {
        const glm::uvec2 ends = topo_->ends[e];
        const glm::vec3 d = in_.positions[ends.x == v ? ends.y : ends.x] - pv;
        const float l = glm::length(d);
        return l > 1e-20f ? d / l : glm::vec3(0.0f);
    };
    auto sinBetween = [](const glm::vec3& a, const glm::vec3& b) { return std::max(glm::length(glm::cross(a, b)), 0.05f); };
    if (opt_.vertices) {
        for (size_t i = 0; i < n; ++i) w.slideK[i] = 1.0f;
        return;
    }
    for (size_t i = 0; i < n; ++i) { // face i between edges i and i + 1
        if (fan.faces[i] == kNone) continue;
        const size_t j = (i + 1) % n;
        const glm::vec3 da = dir(fan.edges[i]), db = dir(fan.edges[j]);
        const float s = sinBetween(da, db);
        if (w.sel[i] && w.sel[j]) {
            w.hasCorner[i] = 1;
            w.cornerDir[i] = (da + db) / s;
        } else if (w.sel[i]) {
            w.slideK[j] = std::max(w.slideK[j], 1.0f / s);
        } else if (w.sel[j]) {
            w.slideK[i] = std::max(w.slideK[i], 1.0f / s);
        }
    }
}

// Largest width at which no two cuts meet on an edge: per polygon edge, what its two ends consume per unit width.
float Bevel::widthLimit() const {
    float limit = std::numeric_limits<float>::max();
    for (uint32_t p = 0; p < polys_.size(); ++p) {
        if (!touchedPoly_[p]) continue;
        const Poly& P = polys_[p];
        for (int i = 0; i < P.n; ++i) {
            const uint32_t x = P.vert[i], y = P.vert[(i + 1) % P.n];
            const glm::vec3 d = in_.positions[y] - in_.positions[x];
            const float len = glm::length(d);
            if (len < 1e-12f) continue;
            const glm::vec3 dn = d / len;
            // what the corner at x takes along x -> y (its out edge) and the corner at y along y -> x (its in edge):
            // the anchor of its path on that side, per unit width, projected on the edge
            auto consume = [&](uint32_t vert, const glm::vec3& along, bool outSide) {
                auto it = work_.find(vert);
                if (it == work_.end()) return 0.0f;
                const VertexWork& w = it->second;
                const Fan& fan = w.fan;
                const size_t n = fan.edges.size();
                size_t f = 0;
                while (f < n && fan.faces[f] != p) ++f;
                if (f == n) return 0.0f;
                if (w.hasCorner[f]) return std::max(glm::dot(w.cornerDir[f], along), 0.0f);
                const size_t eIn = f, eOut = (f + 1) % n;
                size_t e;
                if (w.sel[eIn] && !w.sel[eOut]) e = eOut;
                else if (w.sel[eOut] && !w.sel[eIn]) e = eIn;
                else e = outSide ? eOut : eIn;
                if (w.slideK[e] <= 0.0f) return 0.0f;
                const glm::uvec2 ends = topo_->ends[fan.edges[e]];
                glm::vec3 de = in_.positions[ends.x == vert ? ends.y : ends.x] - in_.positions[vert];
                const float le = glm::length(de);
                if (le < 1e-12f) return 0.0f;
                return std::max(glm::dot(de / le * w.slideK[e], along), 0.0f);
            };
            const float c = consume(x, dn, true) + consume(y, -dn, false);
            if (c > 1e-9f) limit = std::min(limit, len / c);
        }
    }
    return limit;
}

// ---------------------------------------------------------------------------------------------------------------

uint32_t Bevel::addPoint(const glm::vec3& pos, std::vector<std::pair<uint32_t, float>> weights) {
    points_.push_back({pos, std::move(weights)});
    return uint32_t(points_.size() - 1);
}

uint32_t Bevel::blendPoint(uint32_t a, uint32_t b, float t) {
    const Point& A = points_[a];
    const Point& B = points_[b];
    std::vector<std::pair<uint32_t, float>> w;
    for (const auto& [v, x] : A.weights) w.push_back({v, x * (1.0f - t)});
    for (const auto& [v, x] : B.weights) w.push_back({v, x * t});
    return addPoint(glm::mix(A.pos, B.pos, t), std::move(w));
}

void Bevel::makeAnchors() {
    for (auto& [v, w] : work_) {
        const Fan& fan = w.fan;
        const size_t n = fan.edges.size();
        const glm::vec3 pv = in_.positions[v];
        for (size_t i = 0; i < n; ++i) {
            if (w.slideK[i] <= 0.0f) continue;
            const uint32_t e = fan.edges[i];
            const glm::uvec2 ends = topo_->ends[e];
            const uint32_t far = ends.x == v ? ends.y : ends.x;
            const glm::vec3 d = in_.positions[far] - pv;
            const float len = glm::length(d);
            float t = len > 1e-12f ? w.slideK[i] * width_ / len : 0.0f;
            if (!opt_.limitWidth) t = std::min(t, 0.49f);
            t = std::min(t, 0.999f);
            w.slideId[i] = addPoint(pv + d * t, {{v, 1.0f - t}, {far, t}});
        }
        for (size_t i = 0; i < n; ++i) {
            if (!w.hasCorner[i]) continue;
            glm::vec3 off = w.cornerDir[i] * width_;
            if (!opt_.limitWidth) { // keep the corner inside its face's edges
                const size_t j = (i + 1) % n;
                for (uint32_t e : {fan.edges[i], fan.edges[j]}) {
                    const glm::uvec2 ends = topo_->ends[e];
                    const glm::vec3 d = in_.positions[ends.x == v ? ends.y : ends.x] - pv;
                    const float len = glm::length(d);
                    if (len < 1e-12f) continue;
                    const float along = glm::dot(off, d / len);
                    if (along > 0.49f * len) off *= 0.49f * len / along;
                }
            }
            const glm::vec3 pos = pv + off;
            w.cornerId[i] = addPoint(pos, polygonVertexWeights(fan.faces[i], pos));
        }
    }
}

void Bevel::makeStrips() {
    const size_t nc = in_.triangleCount() * 3;
    stripOf_.assign(selected_.size(), kNone);
    for (uint32_t e = 0; e < selected_.size(); ++e) {
        if (!selected_[e]) continue;
        Strip s;
        s.edge = e;
        const uint32_t c = topo_->edgeCorner[e], d = topo_->twin[c];
        if (d >= nc) continue;
        s.a = polyOf_[c / 3], s.b = polyOf_[d / 3];
        s.tail = in_.triangles[c / 3][c % 3];
        s.head = in_.triangles[c / 3][(c % 3 + 1) % 3];
        for (int end = 0; end < 2; ++end) {
            const uint32_t x = end == 0 ? s.tail : s.head;
            VertexWork& w = work_[x];
            const Fan& fan = w.fan;
            const size_t n = fan.edges.size();
            size_t i = 0;
            while (i < n && fan.edges[i] != e) ++i;
            // the faces before (i - 1) and after (i) the edge in fan order; a is before at the tail, after at the head
            const size_t fBefore = (i + n - 1) % n, fAfter = i;
            auto anchorIn = [&](size_t f, size_t otherEdge) {
                return w.hasCorner[f] ? w.cornerId[f] : w.slideId[otherEdge];
            };
            const uint32_t idBefore = anchorIn(fBefore, (i + n - 1) % n), idAfter = anchorIn(fAfter, (i + 1) % n);
            const uint32_t idA = end == 0 ? idBefore : idAfter, idB = end == 0 ? idAfter : idBefore;
            StripEnd& se = s.end[end];
            se.ids.push_back(idA);
            const glm::vec3 A = points_[idA].pos, B = points_[idB].pos, pv = in_.positions[x];
            const glm::uvec2 ends = topo_->ends[e];
            glm::vec3 de = in_.positions[ends.x == x ? ends.y : ends.x] - pv;
            const float le = glm::length(de);
            de = le > 1e-12f ? de / le : glm::vec3(0.0f);
            const glm::vec3 E = pv + de * (0.5f * (glm::dot(A - pv, de) + glm::dot(B - pv, de)));
            for (int j = 1; j < segments_; ++j) {
                const float t = float(j) / float(segments_);
                const glm::vec3 pos = profilePoint(A, B, E, exponent_, t);
                // mask, paint and layers: from where the point lies over each face, blended across the strip
                std::vector<std::pair<uint32_t, float>> wts;
                for (const auto& [vv, ww] : polygonVertexWeights(s.a, pos)) wts.push_back({vv, ww * (1.0f - t)});
                for (const auto& [vv, ww] : polygonVertexWeights(s.b, pos)) wts.push_back({vv, ww * t});
                se.ids.push_back(addPoint(pos, std::move(wts)));
            }
            se.ids.push_back(idB);
        }
        stripOf_[e] = uint32_t(strips_.size());
        strips_.push_back(std::move(s));
        ++stats_.edges;
    }
}

// Per touched vertex: the paths through its faces and, with two or more selected edges, the patch.
void Bevel::makeVertexPaths() {
    for (auto& [v, w] : work_) {
        ++stats_.vertices;
        if (opt_.vertices) {
            makeVertexBevelPatch(v, w);
            continue;
        }
        const Fan& fan = w.fan;
        const size_t n = fan.edges.size();
        // the forward profile of the strip on fan edge i at this vertex: from the anchor in face i - 1 to the one in face i
        auto fwdProfile = [&](size_t i) {
            const Strip& s = strips_[stripOf_[fan.edges[i]]];
            const StripEnd& se = s.end[s.tail == v ? 0 : 1];
            std::vector<uint32_t> ids = se.ids;
            if (s.tail != v) std::reverse(ids.begin(), ids.end());
            return ids;
        };
        auto setEnd = [&](size_t i, const std::vector<uint32_t>& fwd, int extraAfterFwd) {
            Strip& s = strips_[stripOf_[fan.edges[i]]];
            StripEnd& se = s.end[s.tail == v ? 0 : 1];
            se.ids = fwd;
            se.extraAfter = extraAfterFwd;
            if (s.tail != v) {
                std::reverse(se.ids.begin(), se.ids.end());
                se.extraAfter = extraAfterFwd < 0 ? -1 : int(fwd.size()) - 3 - extraAfterFwd;
            }
        };
        if (w.k == 1) {
            size_t i = 0;
            while (i < n && !w.sel[i]) ++i;
            std::vector<uint32_t> prof = fwdProfile(i); // prof.back() = S_first (face i side), prof.front() = S_last
            const size_t m = n - 1;                      // unselected edges in the one gap
            // faces i (after the edge) .. i + m (before it, = i - 1); the back faces are i + 1 .. i + m - 1
            w.path[i] = {prof.back()};
            w.path[(i + m) % n] = {prof.front()};
            if (m == 2) {
                std::vector<uint32_t> r(prof.rbegin(), prof.rend());
                w.path[(i + 1) % n] = r;
            } else {
                // v stays, moved onto the strip's end (the nearest profile point, or a point inserted on the chord)
                const glm::vec3 pv = in_.positions[v];
                int nearest = 0;
                float bestD = std::numeric_limits<float>::max();
                for (size_t j = 0; j < prof.size(); ++j) {
                    const float d = glm::length(points_[prof[j]].pos - pv);
                    if (d < bestD) bestD = d, nearest = int(j);
                }
                const glm::vec3 S0 = points_[prof.front()].pos, S1 = points_[prof.back()].pos;
                const float chord = glm::length(S1 - S0);
                int idxV;
                // Insert the projection of v onto the nearest profile segment unless a profile point is already there.
                if (bestD <= 0.02f * chord) {
                    idxV = nearest;
                    points_[prof[idxV]].weights = {{v, 1.0f}}; // the kept vertex keeps its own paint and mask
                } else {
                    int seg = 0;
                    float segD = std::numeric_limits<float>::max(), segT = 0.5f;
                    for (size_t j = 0; j + 1 < prof.size(); ++j) {
                        const glm::vec3 a = points_[prof[j]].pos, b = points_[prof[j + 1]].pos, ab = b - a;
                        const float l2 = glm::dot(ab, ab);
                        const float t = l2 > 1e-20f ? std::clamp(glm::dot(pv - a, ab) / l2, 0.0f, 1.0f) : 0.5f;
                        const float d = glm::length(a + ab * t - pv);
                        if (d < segD) segD = d, seg = int(j), segT = t;
                    }
                    const uint32_t id = blendPoint(prof[seg], prof[seg + 1], segT);
                    points_[id].weights = {{v, 1.0f}};
                    prof.insert(prof.begin() + seg + 1, id);
                    idxV = seg + 1;
                    setEnd(i, prof, seg);
                }
                // G_first (face i + 1) takes the profile from S_first down to v', G_last (face i + m - 1) from v' to S_last.
                std::vector<uint32_t> first(prof.rbegin(), prof.rbegin() + (int(prof.size()) - idxV));
                std::vector<uint32_t> last(prof.rbegin() + (int(prof.size()) - 1 - idxV), prof.rend());
                for (size_t f = 1; f < m; ++f) w.path[(i + f) % n] = {prof[idxV]};
                w.path[(i + 1) % n] = first;
                w.path[(i + m - 1) % n] = last;
            }
            continue;
        }
        // k >= 2: faces with both edges selected take the corner, with one the slide; gaps decide the rest.
        for (size_t f = 0; f < n; ++f) {
            const size_t g = (f + 1) % n;
            if (w.hasCorner[f]) w.path[f] = {w.cornerId[f]};
            else if (w.sel[f]) w.path[f] = {w.slideId[g]};
            else if (w.sel[g]) w.path[f] = {w.slideId[f]};
        }
        // The forward loop: for each selected edge its profile, then its gap.
        std::vector<uint32_t> loop;
        std::vector<size_t> selIdx;
        for (size_t i = 0; i < n; ++i)
            if (w.sel[i]) selIdx.push_back(i);
        w.profs.clear(), w.conns.clear();
        for (size_t s = 0; s < selIdx.size(); ++s) {
            const size_t i = selIdx[s], next = selIdx[(s + 1) % selIdx.size()];
            const std::vector<uint32_t> prof = fwdProfile(i);
            w.profs.push_back(prof);
            w.conns.emplace_back();
            for (uint32_t id : prof) {
                if (!loop.empty() && loop.back() == id) continue;
                loop.push_back(id);
            }
            const size_t m = (next + n - i) % n - 1; // unselected edges between i and next
            if (m <= 1) continue;
            const size_t eFirst = (i + 1) % n, eLast = (next + n - 1) % n;
            const uint32_t sFirst = w.slideId[eFirst], sLast = w.slideId[eLast];
            if (m == 2) {
                w.path[(i + 1) % n] = {sFirst, sLast}; // the one back face loses its corner
            } else {
                // v stays: on the chord between the two slides, as the back faces' corner
                const glm::vec3 pv = in_.positions[v], a = points_[sFirst].pos, b = points_[sLast].pos, ab = b - a;
                const float l2 = glm::dot(ab, ab);
                const float t = l2 > 1e-20f ? std::clamp(glm::dot(pv - a, ab) / l2, 0.02f, 0.98f) : 0.5f;
                const uint32_t vp = blendPoint(sFirst, sLast, t);
                points_[vp].weights = {{v, 1.0f}};
                w.path[(i + 1) % n] = {sFirst, vp};
                for (size_t f = 2; f < m - 1; ++f) w.path[(i + f) % n] = {vp};
                w.path[(i + m - 1) % n] = {vp, sLast};
                loop.push_back(vp);
                w.conns.back().push_back(vp);
            }
        }
        if (loop.size() > 1 && loop.front() == loop.back()) loop.pop_back();
        w.loopFwd = std::move(loop);
        // Two strips running straight through (same anchors, same profile): they share their end points.
        if (w.k == 2 && w.conns[0].empty() && w.conns[1].empty()) {
            const std::vector<uint32_t>& p0 = w.profs[0];
            const std::vector<uint32_t>& p1 = w.profs[1];
            bool same = p0.front() == p1.back() && p0.back() == p1.front();
            for (size_t j = 0; same && j < p0.size(); ++j)
                same = glm::length(points_[p0[j]].pos - points_[p1[p0.size() - 1 - j]].pos) <= 0.25f * width_;
            if (same) {
                for (size_t j = 1; j + 1 < p0.size(); ++j) // (nearly the same curve: meet in the middle)
                    points_[p0[j]].pos = 0.5f * (points_[p0[j]].pos + points_[p1[p0.size() - 1 - j]].pos);
                std::vector<uint32_t> shared(p0.rbegin(), p0.rend());
                setEnd(selIdx[1], shared, -1);
                continue;
            }
        }
        makePatches(v, w);
    }
}

// The patch joining two or more strips at v, from the forward loop (reversed on output).
void Bevel::makePatches(uint32_t v, VertexWork& w) {
    const std::vector<uint32_t>& loop = w.loopFwd;
    const size_t L = loop.size();
    if (L < 3) return;
    const Fan& fan = w.fan;
    uint32_t group = newGroup_;
    bool hidden = true;
    if (!opt_.newGroup) group = kNone;
    for (uint32_t f : fan.faces) {
        if (f == kNone) continue;
        const uint32_t t = polys_[f].tri[0];
        if (!opt_.newGroup) group = std::min(group, in_.facegroupAt(t));
        hidden = hidden && in_.triangleHidden(t);
    }
    // UVs of the loop points: from the face each lies in or next to (good enough across the patch).
    auto uvOf = [&](uint32_t id) -> glm::vec2 {
        if (!in_.hasUvs()) return {0.0f, 0.0f};
        for (size_t f = 0; f < fan.faces.size(); ++f) {
            if (fan.faces[f] == kNone) continue;
            if (w.cornerId[f] == id) return polygonUv(fan.faces[f], points_[id].pos);
        }
        for (size_t e = 0; e < fan.edges.size(); ++e)
            if (w.slideId[e] == id && fan.faces[e] != kNone) return polygonUv(fan.faces[e], points_[id].pos);
        // a profile or inserted point: the nearest face at v
        uint32_t best = kNone;
        float bestD = std::numeric_limits<float>::max();
        for (uint32_t f : fan.faces) {
            if (f == kNone) continue;
            glm::vec3 c(0.0f);
            for (int i = 0; i < polys_[f].n; ++i) c += in_.positions[polys_[f].vert[i]];
            c /= float(polys_[f].n);
            const float d = glm::length(c - points_[id].pos);
            if (d < bestD) bestD = d, best = f;
        }
        return best == kNone ? glm::vec2(0.0f) : polygonUv(best, points_[id].pos);
    };
    auto emit = [&](std::vector<uint32_t> ids) { // a polygon in forward (face) order: reverse for the outward winding
        std::reverse(ids.begin(), ids.end());
        std::vector<glm::vec2> uvs;
        for (uint32_t id : ids) uvs.push_back(uvOf(id));
        emitPolygon(std::move(ids), std::move(uvs), group, hidden, true);
    };
    if (w.k == 2) {
        // Two profiles: pair them across (a mitre); the gaps' points ride along in the end polygons.
        const std::vector<uint32_t>& p0 = w.profs[0];
        const std::vector<uint32_t>& p1 = w.profs[1];
        const std::vector<uint32_t>& c0 = w.conns[0];
        const std::vector<uint32_t>& c1 = w.conns[1];
        const int S = segments_;
        if (int(p0.size()) != S + 1 || int(p1.size()) != S + 1) { // (never: both are full profiles)
            emit(loop);
            return;
        }
        for (int j = 0; j < S; ++j) {
            std::vector<uint32_t> poly = {p0[j], p0[j + 1]};
            if (j == S - 1) poly.insert(poly.end(), c0.begin(), c0.end());
            poly.push_back(p1[S - 1 - j]);
            poly.push_back(p1[S - j]);
            if (j == 0) poly.insert(poly.end(), c1.begin(), c1.end());
            emit(poly);
        }
        return;
    }
    // Three or more: rings towards a centre (a sphere piece where the loop lies on a sphere).
    const int R = segments_ / 2;
    if (R == 0) {
        emit(loop);
        return;
    }
    std::vector<glm::vec3> pts;
    for (uint32_t id : loop) pts.push_back(points_[id].pos);
    glm::vec3 G(0.0f);
    for (const glm::vec3& p : pts) G += p;
    G /= float(pts.size());
    glm::vec3 sc;
    float sr = 0.0f;
    const bool spherical = fitSphere(pts, sc, sr, 0.05f) && sr < 100.0f * width_;
    glm::vec3 C;
    if (spherical) {
        C = sc + sr * glm::normalize(G - sc);
    } else {
        const float bulge = std::clamp((opt_.profile - 0.25f) / 0.75f, 0.0f, 1.0f);
        C = G + (in_.positions[v] - G) * (bulge * 0.5f);
    }
    std::vector<std::pair<uint32_t, float>> cw;
    for (uint32_t id : loop)
        for (const auto& [vv, ww] : points_[id].weights) cw.push_back({vv, ww / float(L)});
    const uint32_t cid = addPoint(C, std::move(cw));
    std::vector<std::vector<uint32_t>> rings;
    rings.push_back(loop);
    for (int r = 1; r < R; ++r) {
        std::vector<uint32_t> ring(L);
        std::unordered_map<uint32_t, uint32_t> seen; // a loop point that occurs twice gets one ring point
        for (size_t i = 0; i < L; ++i) {
            auto it = seen.find(loop[i]);
            if (it != seen.end()) {
                ring[i] = it->second;
                continue;
            }
            const float t = float(r) / float(R);
            glm::vec3 pos = glm::mix(pts[i], C, t);
            if (spherical) pos = sc + sr * glm::normalize(pos - sc);
            const uint32_t id = blendPoint(loop[i], cid, t);
            points_[id].pos = pos;
            ring[i] = id, seen[loop[i]] = id;
        }
        rings.push_back(std::move(ring));
    }
    for (size_t r = 1; r < rings.size(); ++r)
        for (size_t i = 0; i < L; ++i) {
            const size_t j = (i + 1) % L;
            emit({rings[r - 1][i], rings[r - 1][j], rings[r][j], rings[r][i]});
        }
    const std::vector<uint32_t>& inner = rings.back();
    for (size_t i = 0; i < L; ++i) emit({inner[i], inner[(i + 1) % L], cid});
}

// Vertex bevel: every edge from v is cut `width` along, faces take both cuts, the cut is filled flat or as a cap.
void Bevel::makeVertexBevelPatch(uint32_t v, VertexWork& w) {
    const Fan& fan = w.fan;
    const size_t n = fan.edges.size();
    std::vector<uint32_t> loop;
    for (size_t f = 0; f < n; ++f) {
        const size_t g = (f + 1) % n;
        if (fan.faces[f] != kNone) w.path[f] = {w.slideId[f], w.slideId[g]};
    }
    for (size_t e = 0; e < n; ++e)
        if (w.slideId[e] != kNone) loop.push_back(w.slideId[e]);
    w.loopFwd = loop;
    const size_t L = loop.size();
    if (L < 3) return;
    // A border vertex is cut off and that is all: a patch would close a face across the open side.
    if (fan.faces.back() == kNone) return;
    uint32_t group = newGroup_;
    bool hidden = true;
    if (!opt_.newGroup) group = kNone;
    for (uint32_t f : fan.faces) {
        if (f == kNone) continue;
        const uint32_t t = polys_[f].tri[0];
        if (!opt_.newGroup) group = std::min(group, in_.facegroupAt(t));
        hidden = hidden && in_.triangleHidden(t);
    }
    auto uvOf = [&](uint32_t id) -> glm::vec2 {
        if (!in_.hasUvs()) return {0.0f, 0.0f};
        for (size_t e = 0; e < n; ++e)
            if (w.slideId[e] == id) return polygonUv(fan.faces[e] != kNone ? fan.faces[e] : fan.faces[(e + n - 1) % n], points_[id].pos);
        uint32_t f0 = kNone;
        for (uint32_t f : fan.faces)
            if (f != kNone) f0 = f;
        return f0 == kNone ? glm::vec2(0.0f) : polygonUv(f0, points_[id].pos);
    };
    auto emit = [&](std::vector<uint32_t> ids) {
        std::reverse(ids.begin(), ids.end());
        std::vector<glm::vec2> uvs;
        for (uint32_t id : ids) uvs.push_back(uvOf(id));
        emitPolygon(std::move(ids), std::move(uvs), group, hidden, true);
    };
    if (segments_ == 1) {
        emit(loop);
        return;
    }
    // A cap: rings between the anchors and a pole, shaped by the profile in the plane of each anchor and the axis.
    glm::vec3 c(0.0f);
    for (uint32_t id : loop) c += points_[id].pos;
    c /= float(L);
    const glm::vec3 pv = in_.positions[v];
    glm::vec3 axis = pv - c;
    const float h0 = glm::length(axis);
    axis = h0 > 1e-6f * width_ ? axis / h0 : glm::vec3(0.0f);
    const float h = h0 * std::clamp((opt_.profile - 0.25f) / 0.75f, 0.0f, 1.0f);
    std::vector<std::pair<uint32_t, float>> cw;
    for (uint32_t id : loop)
        for (const auto& [vv, ww] : points_[id].weights) cw.push_back({vv, ww / float(L)});
    const uint32_t pole = addPoint(c + axis * h, {{v, 1.0f}});
    std::vector<std::vector<uint32_t>> rings{loop};
    for (int r = 1; r < segments_; ++r) {
        const float t = float(r) / float(segments_), th = t * 1.5707963f;
        const float fx = std::pow(std::max(std::cos(th), 0.0f), 2.0f / exponent_);
        const float fy = std::pow(std::max(std::sin(th), 0.0f), 2.0f / exponent_);
        std::vector<uint32_t> ring(L);
        for (size_t i = 0; i < L; ++i) {
            const uint32_t id = blendPoint(loop[i], pole, t);
            points_[id].pos = c + (points_[loop[i]].pos - c) * fx + axis * (h * fy);
            ring[i] = id;
        }
        rings.push_back(std::move(ring));
    }
    for (size_t r = 1; r < rings.size(); ++r)
        for (size_t i = 0; i < L; ++i) {
            const size_t j = (i + 1) % L;
            emit({rings[r - 1][i], rings[r - 1][j], rings[r][j], rings[r][i]});
        }
    const std::vector<uint32_t>& inner = rings.back();
    for (size_t i = 0; i < L; ++i) emit({inner[i], inner[(i + 1) % L], pole});
}

void Bevel::rebuildFaces() {
    for (uint32_t p = 0; p < polys_.size(); ++p) {
        const Poly& P = polys_[p];
        if (!touchedPoly_[p]) {
            for (int k = 0; k < (P.n == 4 ? 2 : 1); ++k) {
                const uint32_t t = P.tri[k];
                OutTri o;
                o.v = in_.triangles[t];
                for (int i = 0; i < 3; ++i) o.uv[i] = in_.hasUvs() ? in_.uvs[t * 3 + i] : glm::vec2(0.0f);
                o.group = in_.facegroupAt(t);
                o.hidden = in_.triangleHidden(t);
                o.bevel = 0;
                o.partner = P.n == 4 ? uint32_t(outTris_.size() + (k == 0 ? 1 : -1)) : kNone;
                o.srcTri = t;
                outTris_.push_back(o);
            }
            continue;
        }
        std::vector<uint32_t> ids;
        std::vector<glm::vec2> uvs;
        for (int i = 0; i < P.n; ++i) {
            const uint32_t x = P.vert[i];
            auto it = work_.find(x);
            if (it == work_.end()) {
                ids.push_back(x);
                uvs.push_back(in_.hasUvs() ? in_.uvs[P.corner[i]] : glm::vec2(0.0f));
                continue;
            }
            const VertexWork& w = it->second;
            size_t f = 0;
            while (f < w.fan.faces.size() && w.fan.faces[f] != p) ++f;
            if (f == w.fan.faces.size() || w.path[f].empty()) { // (shouldn't happen) keep the vertex
                ids.push_back(x);
                uvs.push_back(in_.hasUvs() ? in_.uvs[P.corner[i]] : glm::vec2(0.0f));
                continue;
            }
            for (uint32_t id : w.path[f]) {
                ids.push_back(id);
                uvs.push_back(polygonUv(p, points_[id].pos));
            }
        }
        const uint32_t t0 = P.tri[0];
        emitPolygon(std::move(ids), std::move(uvs), in_.facegroupAt(t0), in_.triangleHidden(t0), false);
    }
}

void Bevel::emitStripFaces() {
    for (const Strip& s : strips_) {
        const uint32_t ta = polys_[s.a].tri[0], tb = polys_[s.b].tri[0];
        const uint32_t group = opt_.newGroup ? newGroup_ : std::min(in_.facegroupAt(ta), in_.facegroupAt(tb));
        const bool hidden = in_.triangleHidden(ta) && in_.triangleHidden(tb);
        // UVs along each end: from face a's UVs at the a anchor to face b's at the b anchor.
        auto endUvs = [&](const StripEnd& se) {
            std::vector<glm::vec2> uv(se.ids.size(), glm::vec2(0.0f));
            if (!in_.hasUvs()) return uv;
            // each point's UV over face a and over face b, blended across the strip (exact on a flat sheet)
            const int S = segments_;
            for (size_t i = 0; i < se.ids.size(); ++i) {
                float t;
                if (se.extraAfter < 0) t = float(i) / float(S);
                else if (int(i) <= se.extraAfter) t = float(i) / float(S);
                else if (int(i) == se.extraAfter + 1) t = (float(se.extraAfter) + 0.5f) / float(S);
                else t = float(i - 1) / float(S);
                const glm::vec3 pos = points_[se.ids[i]].pos;
                uv[i] = glm::mix(polygonUv(s.a, pos), polygonUv(s.b, pos), t);
            }
            return uv;
        };
        const StripEnd& ev = s.end[0];
        const StripEnd& eu = s.end[1];
        const std::vector<glm::vec2> uvV = endUvs(ev), uvU = endUvs(eu);
        // index of profile point j in an end (skipping the extra)
        auto at = [](const StripEnd& se, int j) { return se.extraAfter >= 0 && j > se.extraAfter ? j + 1 : j; };
        for (int j = 0; j < segments_; ++j) {
            std::vector<uint32_t> ids;
            std::vector<glm::vec2> uvs;
            auto push = [&](const StripEnd& se, const std::vector<glm::vec2>& uv, int idx) { ids.push_back(se.ids[idx]), uvs.push_back(uv[idx]); };
            push(eu, uvU, at(eu, j));
            push(ev, uvV, at(ev, j));
            if (ev.extraAfter == j) push(ev, uvV, j + 1);
            push(ev, uvV, at(ev, j + 1));
            push(eu, uvU, at(eu, j + 1));
            if (eu.extraAfter == j) push(eu, uvU, j + 1);
            emitPolygon(std::move(ids), std::move(uvs), group, hidden, true);
        }
    }
}

void Bevel::emitPolygon(std::vector<uint32_t> ids, std::vector<glm::vec2> uvs, uint32_t group, bool hidden, bool bevel) {
    // drop consecutive duplicates (shared anchors), including around the end
    std::vector<uint32_t> cid;
    std::vector<glm::vec2> cuv;
    for (size_t i = 0; i < ids.size(); ++i) {
        if (!cid.empty() && cid.back() == ids[i]) continue;
        cid.push_back(ids[i]), cuv.push_back(uvs[i]);
    }
    while (cid.size() > 1 && cid.front() == cid.back()) cid.pop_back(), cuv.pop_back();
    if (cid.size() < 3) return;
    std::vector<glm::vec3> pos;
    for (uint32_t id : cid) pos.push_back(points_[id].pos);
    std::vector<std::array<int, 3>> tris;
    if (!triangulate(pos, tris)) return;
    const size_t first = outTris_.size();
    for (const std::array<int, 3>& t : tris) {
        OutTri o;
        for (int k = 0; k < 3; ++k) o.v[k] = cid[t[k]], o.uv[k] = cuv[t[k]];
        o.group = group;
        o.hidden = hidden;
        o.bevel = bevel;
        o.partner = kNone;
        o.srcTri = kNone;
        outTris_.push_back(o);
    }
    if (tris.size() == 2) { // a quad: the two triangles share the diagonal
        outTris_[first].partner = uint32_t(first + 1);
        outTris_[first + 1].partner = uint32_t(first);
    }
    if (bevel) stats_.trianglesAdded += tris.size();
}

void Bevel::assemble(Mesh& out, PolyBevelResult& result) {
    // Compact the points: inputs that stay keep their order, new points follow.
    std::vector<uint32_t> remap(points_.size(), kNone);
    uint32_t count = 0;
    for (size_t i = 0; i < points_.size(); ++i)
        if (pointUsed_[i]) remap[i] = count++;
    out = Mesh();
    out.positions.resize(count);
    const bool colors = in_.hasColors(), mask = in_.hasMask(), layers = !in_.layers.empty();
    const bool paintLayers = !in_.paintLayers.empty();
    if (colors) out.colors.resize(count);
    if (mask) out.mask.resize(count);
    if (layers) {
        out.base.resize(count);
        out.layers.resize(in_.layers.size());
        for (size_t l = 0; l < in_.layers.size(); ++l) {
            out.layers[l].name = in_.layers[l].name;
            out.layers[l].weight = in_.layers[l].weight;
            out.layers[l].delta.resize(count);
        }
    }
    if (paintLayers) {
        out.paintBase.resize(count);
        out.paintLayers.resize(in_.paintLayers.size());
        for (size_t l = 0; l < in_.paintLayers.size(); ++l) {
            const PaintLayer& src = in_.paintLayers[l];
            PaintLayer& dst = out.paintLayers[l];
            dst.name = src.name, dst.opacity = src.opacity, dst.blend = src.blend, dst.visible = src.visible;
            dst.color.resize(count), dst.alpha.resize(count);
        }
        out.activePaintLayer = in_.activePaintLayer;
    }
    const size_t nin = in_.vertexCount();
    for (size_t i = 0; i < points_.size(); ++i) {
        const uint32_t o = remap[i];
        if (o == kNone) continue;
        const Point& P = points_[i];
        out.positions[o] = P.pos;
        if (i < nin) {
            if (colors) out.colors[o] = in_.colors[i];
            if (mask) out.mask[o] = in_.mask[i];
            if (layers) {
                out.base[o] = in_.base[i];
                for (size_t l = 0; l < in_.layers.size(); ++l) out.layers[l].delta[o] = in_.layers[l].delta[i];
            }
            if (paintLayers) {
                out.paintBase[o] = in_.paintBase[i];
                for (size_t l = 0; l < in_.paintLayers.size(); ++l)
                    out.paintLayers[l].color[o] = in_.paintLayers[l].color[i], out.paintLayers[l].alpha[o] = in_.paintLayers[l].alpha[i];
            }
            continue;
        }
        float sum = 0.0f;
        for (const auto& [v, w] : P.weights) sum += w;
        const float norm = sum > 0.0f ? 1.0f / sum : 0.0f;
        if (colors) {
            glm::vec3 c(0.0f);
            for (const auto& [v, w] : P.weights) c += in_.colors[v] * w;
            out.colors[o] = sum > 0.0f ? glm::clamp(c * norm, glm::vec3(0.0f), glm::vec3(1.0f)) : glm::vec3(1.0f);
        }
        if (mask) {
            float m = 0.0f;
            for (const auto& [v, w] : P.weights) m += in_.mask[v] * w;
            out.mask[o] = std::clamp(m * norm, 0.0f, 1.0f);
        }
        if (layers) {
            glm::vec3 sumDelta(0.0f);
            for (size_t l = 0; l < in_.layers.size(); ++l) {
                glm::vec3 d(0.0f);
                for (const auto& [v, w] : P.weights) d += in_.layers[l].delta[v] * w;
                d *= norm;
                out.layers[l].delta[o] = d;
                sumDelta += d * in_.layers[l].weight;
            }
            out.base[o] = P.pos - sumDelta; // positions == base + sum(weight * delta) holds for new points too
        }
        if (paintLayers) {
            glm::vec3 pb(0.0f);
            for (const auto& [v, w] : P.weights) pb += in_.paintBase[v] * w;
            out.paintBase[o] = glm::clamp(pb * norm, glm::vec3(0.0f), glm::vec3(1.0f));
            for (size_t l = 0; l < in_.paintLayers.size(); ++l) {
                glm::vec3 c(0.0f);
                float a = 0.0f;
                for (const auto& [v, w] : P.weights) c += in_.paintLayers[l].color[v] * w, a += in_.paintLayers[l].alpha[v] * w;
                out.paintLayers[l].color[o] = glm::clamp(c * norm, glm::vec3(0.0f), glm::vec3(1.0f));
                out.paintLayers[l].alpha[o] = std::clamp(a * norm, 0.0f, 1.0f);
            }
        }
    }
    // Triangles and their attributes.
    const size_t nt = outTris_.size();
    out.triangles.resize(nt);
    const bool groups = in_.hasFaceGroups() || opt_.newGroup;
    const bool hidden = in_.hasHidden(), uvs = in_.hasUvs(), uvFlags = in_.hasUvFlags(), creases = in_.hasCreases();
    if (groups) out.facegroups.resize(nt);
    if (hidden) out.hidden.resize(nt);
    if (uvs) out.uvs.resize(nt * 3);
    if (uvFlags) out.uvFlags.assign(nt * 3, 0);
    if (creases) out.creases.assign(nt * 3, 0.0f);
    bool anyQuad = false;
    for (size_t t = 0; t < nt; ++t) {
        const OutTri& o = outTris_[t];
        for (int k = 0; k < 3; ++k) out.triangles[t][k] = remap[o.v[k]];
        if (groups) out.facegroups[t] = o.group;
        if (hidden) out.hidden[t] = o.hidden;
        if (uvs)
            for (int k = 0; k < 3; ++k) out.uvs[t * 3 + k] = o.uv[k];
        if (o.srcTri != kNone) {
            if (uvFlags)
                for (int k = 0; k < 3; ++k) out.uvFlags[t * 3 + k] = in_.uvFlags[o.srcTri * 3 + k];
            if (creases)
                for (int k = 0; k < 3; ++k) out.creases[t * 3 + k] = in_.creases[o.srcTri * 3 + k];
        }
        anyQuad = anyQuad || o.partner != kNone;
    }
    if (anyQuad) {
        out.ensureQuadPartners();
        for (size_t t = 0; t < nt; ++t)
            if (outTris_[t].partner != kNone && outTris_[t].partner > t) out.pairQuad(uint32_t(t), outTris_[t].partner);
    }
    out.texture = in_.texture;
    out.paintToTexture = in_.paintToTexture;
    out.paintTarget = in_.paintTarget;
    out.rebuildTopology();
    out.recomputeAllNormals();
    result.bevelTriangles.clear();
    for (size_t t = 0; t < nt; ++t)
        if (outTris_[t].bevel) result.bevelTriangles.push_back(uint32_t(t));
    stats_.widthUsed = width_;
    result.stats = stats_;
}

bool Bevel::run(std::span<const glm::uvec2> edges, std::span<const uint32_t> vertices, Mesh& out, PolyBevelResult& result,
                std::string& error) {
    if (in_.triangleCount() == 0) {
        error = "Nothing to bevel: the Part is empty";
        return false;
    }
    if (!(opt_.width > 0.0f) || !std::isfinite(opt_.width)) {
        error = "Bevel width must be above zero";
        return false;
    }
    segments_ = std::clamp(opt_.segments, 1, kPolyBevelMaxSegments);
    exponent_ = profileExponent(opt_.profile);
    topo_ = buildEdgeTopology(in_);
    buildPolygons();
    if (opt_.vertices ? !selectVertices(vertices, error) : !selectEdges(edges, error)) return false;
    for (auto& [v, w] : work_) planVertex(v, w);
    touchedPoly_.assign(polys_.size(), 0);
    for (const auto& [v, w] : work_)
        for (uint32_t f : w.fan.faces)
            if (f != kNone) touchedPoly_[f] = 1;
    width_ = opt_.width;
    if (opt_.limitWidth) {
        const float limit = widthLimit();
        if (limit < width_) width_ = limit * 0.98f;
    }
    if (!(width_ > 0.0f)) {
        error = "Nothing to bevel: no room for a bevel on these edges";
        return false;
    }
    newGroup_ = nextFaceGroupId(in_);
    points_.resize(in_.vertexCount());
    for (size_t i = 0; i < in_.vertexCount(); ++i) points_[i].pos = in_.positions[i], points_[i].weights = {{uint32_t(i), 1.0f}};
    pointUsed_.assign(points_.size(), 0);
    makeAnchors();
    if (!opt_.vertices) makeStrips();
    makeVertexPaths();
    rebuildFaces();
    if (!opt_.vertices) emitStripFaces();
    pointUsed_.assign(points_.size(), 0);
    for (const OutTri& o : outTris_)
        for (int k = 0; k < 3; ++k) pointUsed_[o.v[k]] = 1;
    assemble(out, result);
    return true;
}

} // namespace

bool polyBevel(const Mesh& in, std::span<const glm::uvec2> edges, std::span<const uint32_t> vertices,
               const PolyBevelOptions& options, Mesh& out, PolyBevelResult& result, std::string& error) {
    Bevel b(in, options);
    Mesh tmp;
    PolyBevelResult r;
    if (!b.run(edges, vertices, tmp, r, error)) return false;
    out = std::move(tmp);
    result = std::move(r);
    return true;
}

// --- The Polygon mode op (core/PolyOps.h) ---------------------------------------------------------------------

namespace {

// Faces picked: the edges that bound the selection (one selected polygon beside them), so one side of a gridded cube
// rounds its outline and leaves the cells alone. A selection with no border (a closed Part, all faces) takes the sharp
// feature edges instead: the ones whose two polygons differ by more than kSharpDegrees.
constexpr float kSharpDegrees = 30.0f;

std::vector<glm::uvec2> faceBevelEdges(const PolyOpInput& in) {
    const EdgeTopology& topo = in.topo;
    auto selected = [&](uint32_t corner) { return corner != ~0u && in.triangleFlags[corner / 3] != 0; };
    std::vector<glm::uvec2> border, sharp;
    for (uint32_t e = 0; e < topo.edgeCount(); ++e) {
        const uint32_t c = topo.edgeCorner[e], d = topo.twin[c];
        const bool a = selected(c), b = selected(d);
        if (!a && !b) continue;
        if (a != b) border.push_back(topo.ends[e]);
        else if (edgeAngle(in.mesh, topo, e) > kSharpDegrees) sharp.push_back(topo.ends[e]);
    }
    return border.empty() ? sharp : border;
}

// Picked Points: the points are cut off. Picked Edges: those edges. Picked Faces: the selection's border (faceBevelEdges).
bool polyBevelOp(const PolyOpInput& in, PolyOpOutput& out, std::string& error) {
    const Mesh& m = in.mesh;
    const AABB box = m.bounds();
    const float diagonal = box.valid() ? glm::length(box.extent()) : 0.0f;
    if (!(diagonal > 0.0f)) {
        error = "Bevel needs a Part with some size";
        return false;
    }
    PolyBevelOptions o;
    o.width = float(argNumber(in.args, "width", 0.02)) * diagonal; // the slider is a share of the Part's size
    o.segments = int(std::lround(argNumber(in.args, "segments", 1.0)));
    o.profile = float(argNumber(in.args, "profile", 0.5));
    o.limitWidth = argBool(in.args, "limit", true);
    o.newGroup = argBool(in.args, "new_group", true);
    o.vertices = in.element == PolyElement::Points;
    std::vector<glm::uvec2> faceEdges;
    if (in.element == PolyElement::Faces) faceEdges = faceBevelEdges(in);
    const std::vector<glm::uvec2>& edges = in.element == PolyElement::Faces ? faceEdges : in.edgePairs;
    if (o.vertices ? in.vertices.empty() : edges.empty()) {
        if (in.element == PolyElement::Faces && !in.triangles.empty())
            error = "Bevel found no edge to round: the selection has no border and the Part has no sharp edges (select edges instead)";
        else
            error = o.vertices ? "Bevel needs points: click one, Shift+click adds (Tool > Polygon)"
                               : "Bevel needs edges: click one, Shift+click adds, or select faces for their border (Tool > Polygon)";
        return false;
    }
    PolyBevelResult r;
    if (!polyBevel(m, edges, in.vertices, o, out.mesh, r, error)) return false;
    out.element = PolyElement::Faces;
    out.triangles = r.bevelTriangles;
    char buf[160];
    if (o.vertices) std::snprintf(buf, sizeof buf, "Bevelled %zu %s", r.stats.vertices, r.stats.vertices == 1 ? "point" : "points");
    else std::snprintf(buf, sizeof buf, "Bevelled %zu %s", r.stats.edges, r.stats.edges == 1 ? "edge" : "edges");
    out.message = buf;
    if (r.stats.skipped) {
        std::snprintf(buf, sizeof buf, " (%zu left alone: open or non-manifold)", r.stats.skipped);
        out.message += buf;
    }
    return true;
}

} // namespace

void registerPolyBevelOp(PolyOpRegistry& reg) {
    PolyOp op;
    op.id = "bevel";
    op.label = "Bevel";
    op.description = "Round or chamfer the selected edges (or the border of the selected faces, or a closed Part's sharp edges; selected points are cut off): "
                     "a strip of faces across each edge, as wide as Width, with Segments faces shaped by the Profile.";
    op.credit = "@AbyssDragonModding (#225), members' ideas threads";
    op.needs = PolyElement::Edges;
    op.anyElement = true;
    op.preview = true;
    op.params = {
        {"width", "Width", 0.001, 0.2, 0.02, "How far each face is cut back from the edge, as a share of the Part's size."},
        {"segments", "Segments", 1.0, 12.0, 1.0, "Faces across the strip: 1 is a flat chamfer, more makes it round."},
        {"profile", "Profile", 0.0, 1.0, 0.5, "The strip's shape: 0.25 flat, 0.5 round, 1 nearly square, below 0.25 concave."},
        {"limit", "Limit width", 0.0, 1.0, 1.0, "Hold the width back so no two cuts meet or cross on an edge.", true},
        {"new_group", "New face group", 0.0, 1.0, 1.0, "The bevel's faces get a face group of their own (off: they join the faces beside them).", true},
    };
    op.run = polyBevelOp;
    reg.add(std::move(op));
}

} // namespace sf
