// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Polygon mode's selection; see core/PolyMode.h.
#include "core/PolyMode.h"

#include "core/EdgeLoopInsert.h"

#include "core/Commands.h"
#include "core/Document.h"
#include "core/Mesh.h"
#include "core/Micro.h"
#include "core/Multires.h"
#include "core/ScreenSelect.h"
#include "core/Uv.h"

#include <algorithm>
#include <cstdio>
#include <unordered_set>

namespace sf {

namespace {

using T = ParamSpec::Type;

std::string withCommas(size_t n) {
    std::string digits = std::to_string(n);
    for (int i = int(digits.size()) - 3; i > 0; i -= 3) digits.insert(size_t(i), 1, ',');
    return digits;
}

std::string shortCount(size_t n) { // 1.3K, 2.0M
    char buf[32];
    if (n >= 1000000) std::snprintf(buf, sizeof buf, "%.1fM", double(n) / 1e6);
    else if (n >= 1000) std::snprintf(buf, sizeof buf, "%.1fK", double(n) / 1e3);
    else std::snprintf(buf, sizeof buf, "%zu", n);
    return buf;
}

// The polygon of triangle t (its lower half for a quad) and both of its triangles.
uint32_t polygonOf(const Mesh& m, uint32_t t) { return edgePolygon(m, t); }
int polygonTriangles(const Mesh& m, uint32_t t, uint32_t out[2]) {
    out[0] = t;
    const uint32_t p = m.quadPartnerAt(t);
    if (p == Mesh::kNoQuad) return 1;
    out[1] = p;
    return 2;
}
// The polygon's edges (3 or 4, kNoEdge-free).
int polygonEdges(const Mesh& m, const EdgeTopology& topo, uint32_t t, uint32_t out[4]) {
    uint32_t tris[2];
    const int n = polygonTriangles(m, t, tris);
    int k = 0;
    for (int i = 0; i < n; ++i)
        for (uint32_t c = tris[i] * 3; c < tris[i] * 3 + 3; ++c)
            if (topo.cornerEdge[c] != kNoEdge && k < 4) out[k++] = topo.cornerEdge[c];
    return k;
}
// The polygon's vertices (3 or 4, in winding order).
int polygonVertices(const Mesh& m, uint32_t t, uint32_t out[4]) {
    glm::uvec4 f;
    int n = m.exportFace(polygonOf(m, t), f);
    if (n == 0) n = m.exportFace(m.quadPartnerAt(t), f); // (exportFace writes a quad from one half only)
    for (int i = 0; i < n; ++i) out[i] = f[i];
    return n;
}
// The polygons on either side of edge e (kNoEdge across an open edge).
glm::uvec2 edgeSides(const Mesh& m, const EdgeTopology& topo, uint32_t e) {
    const uint32_t c = topo.edgeCorner[e], d = topo.twin[c];
    return {polygonOf(m, c / 3), d == kNoCorner ? kNoEdge : polygonOf(m, d / 3)};
}
glm::vec3 polygonCentre(const Mesh& m, uint32_t t) {
    uint32_t vs[4];
    const int n = polygonVertices(m, t, vs);
    glm::vec3 c(0.0f);
    for (int i = 0; i < n; ++i) c += m.positions[vs[i]];
    return c / float(std::max(n, 1));
}
glm::vec3 polygonNormal(const Mesh& m, uint32_t t) {
    uint32_t tris[2];
    const int n = polygonTriangles(m, t, tris);
    glm::vec3 nrm(0.0f);
    for (int i = 0; i < n; ++i) {
        const glm::uvec3& tri = m.triangles[tris[i]];
        nrm += glm::cross(m.positions[tri.y] - m.positions[tri.x], m.positions[tri.z] - m.positions[tri.x]);
    }
    return nrm;
}
bool edgeVisible(const Mesh& m, const EdgeTopology& topo, uint32_t e) {
    const glm::uvec2 s = edgeSides(m, topo, e);
    return !m.triangleHidden(s.x) || (s.y != kNoEdge && !m.triangleHidden(s.y));
}
bool pointVisible(const Mesh& m, uint32_t v) { return !m.vertexHidden(v); }

void sortUnique(std::vector<uint32_t>& v) {
    std::sort(v.begin(), v.end());
    v.erase(std::unique(v.begin(), v.end()), v.end());
}

// The faces a set of triangle flags means for the current element: Faces: those; Points: their vertices; Edges:
// their edges. Applies with `mode`; returns the elements changed.
size_t applyFaceSet(PolySelection& sel, const Mesh& m, const std::vector<uint8_t>& faceSet, PolySelectMode mode) {
    const EdgeTopology& topo = *sel.topo;
    const bool on = mode != PolySelectMode::Remove;
    size_t changed = 0;
    if (mode == PolySelectMode::Replace) changed += polySelectAll(sel, m, false);
    auto flipPoint = [&](uint32_t v) {
        if (sel.pointSelected(v) != on && pointVisible(m, v)) sel.setPoint(v, on), ++changed;
    };
    auto flipEdge = [&](uint32_t e) {
        if (sel.edgeSelected(e) != on && edgeVisible(m, topo, e)) sel.setEdge(e, on), ++changed;
    };
    for (uint32_t t = 0; t < faceSet.size() && t < m.triangleCount(); ++t) {
        if (!faceSet[t] || m.triangleHidden(t)) continue;
        if (sel.element == PolyElement::Faces) {
            if (polygonOf(m, t) != t) continue; // (counted once per polygon)
            if (sel.faceSelected(t) != on) sel.setFace(m, t, on), ++changed;
        } else if (sel.element == PolyElement::Points) {
            const glm::uvec3& tri = m.triangles[t];
            flipPoint(tri.x), flipPoint(tri.y), flipPoint(tri.z);
        } else {
            for (uint32_t c = t * 3; c < t * 3 + 3; ++c)
                if (topo.cornerEdge[c] != kNoEdge) flipEdge(topo.cornerEdge[c]);
        }
    }
    return changed;
}

// The same for a list of edges: Edges: those; Points: their ends; Faces: the polygons beside them.
size_t applyEdgeList(PolySelection& sel, const Mesh& m, const std::vector<uint32_t>& list, PolySelectMode mode) {
    const EdgeTopology& topo = *sel.topo;
    const bool on = mode != PolySelectMode::Remove;
    size_t changed = 0;
    if (mode == PolySelectMode::Replace) changed += polySelectAll(sel, m, false);
    for (uint32_t e : list) {
        if (e >= topo.edgeCount() || !edgeVisible(m, topo, e)) continue;
        if (sel.element == PolyElement::Edges) {
            if (sel.edgeSelected(e) != on) sel.setEdge(e, on), ++changed;
        } else if (sel.element == PolyElement::Points) {
            for (uint32_t v : {topo.ends[e].x, topo.ends[e].y})
                if (sel.pointSelected(v) != on && pointVisible(m, v)) sel.setPoint(v, on), ++changed;
        } else {
            const glm::uvec2 s = edgeSides(m, topo, e);
            for (uint32_t t : {s.x, s.y})
                if (t != kNoEdge && !m.triangleHidden(t) && sel.faceSelected(t) != on) sel.setFace(m, t, on), ++changed;
        }
    }
    return changed;
}

bool parseVec3(const std::string& s, glm::vec3& out) {
    float x, y, z;
    if (std::sscanf(s.c_str(), "%f %f %f", &x, &y, &z) != 3 && std::sscanf(s.c_str(), "%f,%f,%f", &x, &y, &z) != 3) return false;
    out = {x, y, z};
    return true;
}

} // namespace

const char* polyElementName(PolyElement e) {
    switch (e) {
    case PolyElement::Points: return "points";
    case PolyElement::Edges: return "edges";
    case PolyElement::Faces: return "faces";
    }
    return "faces";
}

bool parsePolyElement(const std::string& name, PolyElement& out) {
    if (name == "points" || name == "point" || name == "vertices" || name == "vertex") out = PolyElement::Points;
    else if (name == "edges" || name == "edge") out = PolyElement::Edges;
    else if (name == "faces" || name == "face" || name == "polygons" || name == "polygon") out = PolyElement::Faces;
    else return false;
    return true;
}

std::string polyModeRefusal(const Mesh& m, size_t maxTriangles) {
    if (m.triangleCount() == 0) return "The active Part has no faces";
    if (!m.forgespheres.empty()) return "Polygon mode needs a mesh: Make Mesh from the Forge Spheres first (Tool > Forge Spheres)";
    if (!m.tube.empty()) return "Polygon mode needs a mesh: Bake the Tube first (Tool > Tube)";
    if (hasMicro(m)) return "Polygon mode works on a Part without a Micro level: remove it first (Tool > Geometry > Micro Detail)";
    if (subdivLevelCount(m) > 0)
        return "Polygon mode works on a Part without subdivision levels (its operations rebuild the cage the levels are made from): "
               "Tool > Geometry > Remove Above first";
    if (m.triangleCount() > maxTriangles)
        return "Polygon mode works up to " + shortCount(maxTriangles) + " triangles; this Part has " + shortCount(m.triangleCount()) +
               ": Decimate or Quad Remesh it first (Tool > Remesh)";
    return {};
}

// ---------------------------------------------------------------------------
// PolySelection
// ---------------------------------------------------------------------------

void PolySelection::clear() {
    ++revision;
    topo.reset();
    std::vector<uint8_t>().swap(points);
    std::vector<uint8_t>().swap(edges);
    std::vector<uint8_t>().swap(faces);
    pointCount = edgeCount = faceCount = 0;
}

bool PolySelection::synced(const Mesh& m) const {
    return topo && topo->key == edgeTopologyKey(m) && topo->cornerEdge.size() == m.triangleCount() * 3 &&
           topo->vertexOffsets.size() == m.vertexCount() + 1;
}

const EdgeTopology& PolySelection::sync(const Mesh& m, const std::shared_ptr<const EdgeTopology>& reuse) {
    if (synced(m)) return *topo;
    clear();
    if (reuse && reuse->key == edgeTopologyKey(m) && reuse->cornerEdge.size() == m.triangleCount() * 3 &&
        reuse->vertexOffsets.size() == m.vertexCount() + 1)
        topo = reuse;
    else
        topo = buildEdgeTopology(m);
    points.assign(m.vertexCount(), 0);
    edges.assign(topo->edgeCount(), 0);
    faces.assign(m.triangleCount(), 0);
    return *topo;
}

void PolySelection::recount() {
    pointCount = size_t(std::count(points.begin(), points.end(), uint8_t(1)));
    edgeCount = size_t(std::count(edges.begin(), edges.end(), uint8_t(1)));
    faceCount = 0;
    for (uint32_t t = 0; t < faces.size(); ++t) faceCount += faces[t] != 0; // (fixed up by setElement's caller for quads)
}

void PolySelection::setPoint(uint32_t v, bool on) {
    if (v >= points.size() || bool(points[v]) == on) return;
    points[v] = on;
    on ? ++pointCount : --pointCount;
    ++revision;
}

void PolySelection::setEdge(uint32_t e, bool on) {
    if (e >= edges.size() || bool(edges[e]) == on) return;
    edges[e] = on;
    on ? ++edgeCount : --edgeCount;
    ++revision;
}

void PolySelection::setFace(const Mesh& m, uint32_t t, bool on) {
    if (t >= faces.size()) return;
    uint32_t tris[2];
    const int n = polygonTriangles(m, t, tris);
    bool changed = false;
    for (int i = 0; i < n; ++i)
        if (tris[i] < faces.size() && bool(faces[tris[i]]) != on) faces[tris[i]] = on, changed = true;
    if (!changed) return;
    on ? ++faceCount : --faceCount;
    ++revision;
}

size_t PolySelection::count() const {
    switch (element) {
    case PolyElement::Points: return pointCount;
    case PolyElement::Edges: return edgeCount;
    case PolyElement::Faces: return faceCount;
    }
    return 0;
}

size_t PolySelection::total(const Mesh& m) const {
    switch (element) {
    case PolyElement::Points: return m.vertexCount();
    case PolyElement::Edges: return topo ? topo->edgeCount() : 0;
    case PolyElement::Faces: return m.triangleCount() - m.quadCount();
    }
    return 0;
}

std::vector<uint32_t> PolySelection::vertices(const Mesh& m) const {
    std::vector<uint32_t> out;
    if (!topo) return out;
    if (element == PolyElement::Points) {
        for (uint32_t v = 0; v < points.size(); ++v)
            if (points[v]) out.push_back(v);
        return out;
    }
    if (element == PolyElement::Edges) {
        for (uint32_t e = 0; e < edges.size(); ++e)
            if (edges[e]) out.push_back(topo->ends[e].x), out.push_back(topo->ends[e].y);
    } else {
        for (uint32_t t = 0; t < faces.size() && t < m.triangleCount(); ++t)
            if (faces[t]) {
                const glm::uvec3& tri = m.triangles[t];
                out.push_back(tri.x), out.push_back(tri.y), out.push_back(tri.z);
            }
    }
    sortUnique(out);
    return out;
}

std::vector<uint32_t> PolySelection::triangles(const Mesh& m) const {
    std::vector<uint32_t> out;
    if (!topo) return out;
    if (element == PolyElement::Faces) {
        for (uint32_t t = 0; t < faces.size() && t < m.triangleCount(); ++t)
            if (faces[t]) out.push_back(t);
        return out;
    }
    for (uint32_t t = 0; t < m.triangleCount(); ++t) {
        if (m.triangleHidden(t)) continue;
        bool all = true;
        if (element == PolyElement::Points) {
            const glm::uvec3& tri = m.triangles[t];
            all = pointSelected(tri.x) && pointSelected(tri.y) && pointSelected(tri.z);
            // A quad: both halves (its far corners too)
            const uint32_t p = m.quadPartnerAt(t);
            if (all && p != Mesh::kNoQuad) {
                const glm::uvec3& q = m.triangles[p];
                all = pointSelected(q.x) && pointSelected(q.y) && pointSelected(q.z);
            }
        } else {
            uint32_t es[4];
            const int n = polygonEdges(m, *topo, t, es);
            for (int i = 0; i < n && all; ++i) all = edgeSelected(es[i]);
            all = all && n > 0;
        }
        if (all) out.push_back(t);
    }
    return out;
}

std::vector<uint32_t> PolySelection::edgeList(const Mesh& m) const {
    std::vector<uint32_t> out;
    if (!topo) return out;
    if (element == PolyElement::Edges) {
        for (uint32_t e = 0; e < edges.size(); ++e)
            if (edges[e]) out.push_back(e);
        return out;
    }
    if (element == PolyElement::Points) {
        for (uint32_t e = 0; e < topo->edgeCount(); ++e)
            if (pointSelected(topo->ends[e].x) && pointSelected(topo->ends[e].y) && edgeVisible(m, *topo, e)) out.push_back(e);
        return out;
    }
    for (uint32_t t = 0; t < faces.size() && t < m.triangleCount(); ++t) {
        if (!faces[t]) continue;
        for (uint32_t c = t * 3; c < t * 3 + 3; ++c)
            if (topo->cornerEdge[c] != kNoEdge) out.push_back(topo->cornerEdge[c]);
    }
    sortUnique(out);
    return out;
}

std::vector<float> PolySelection::weights(const Mesh& m) const {
    const std::vector<uint32_t> vs = vertices(m);
    std::vector<float> w;
    if (vs.empty()) return w;
    w.assign(m.vertexCount(), 0.0f);
    for (uint32_t v : vs)
        if (v < w.size() && !m.vertexHidden(v)) w[v] = 1.0f;
    return w;
}

void PolySelection::setElement(const Mesh& m, PolyElement e) {
    if (!topo) {
        element = e;
        return;
    }
    if (e == element) return;
    const PolyElement from = element;
    // The selection as the new kind: what the old one covers, fully.
    std::vector<uint8_t> np(points.size(), 0), ne(edges.size(), 0), nf(faces.size(), 0);
    if (e == PolyElement::Points) {
        for (uint32_t v : vertices(m)) np[v] = 1;
    } else if (e == PolyElement::Edges) {
        for (uint32_t ed : edgeList(m)) ne[ed] = 1;
    } else {
        for (uint32_t t : triangles(m)) nf[t] = 1;
    }
    (void)from;
    points.swap(np);
    edges.swap(ne);
    faces.swap(nf);
    element = e;
    // Counts: quads count once.
    pointCount = size_t(std::count(points.begin(), points.end(), uint8_t(1)));
    edgeCount = size_t(std::count(edges.begin(), edges.end(), uint8_t(1)));
    faceCount = 0;
    for (uint32_t t = 0; t < faces.size(); ++t) {
        if (!faces[t]) continue;
        const uint32_t p = m.quadPartnerAt(t);
        if (p != Mesh::kNoQuad && !faces[p]) faces[p] = 1; // (a half on its own: the whole quad)
        if (polygonOf(m, t) == t) ++faceCount;
    }
    ++revision;
}

std::string polySelectionText(const PolySelection& sel, const Mesh& m) {
    const size_t n = sel.count(), total = sel.total(m);
    const char* what = sel.element == PolyElement::Points ? "point" : sel.element == PolyElement::Edges ? "edge" : "face";
    return withCommas(n) + " " + what + (n == 1 ? "" : "s") + " selected of " + withCommas(total);
}

uint32_t polyNearestVertex(const Mesh& m, uint32_t t, const glm::vec3& point) {
    if (t >= m.triangleCount()) return ~0u;
    uint32_t vs[4];
    const int n = polygonVertices(m, t, vs);
    uint32_t best = ~0u;
    float bestD = 0.0f;
    for (int i = 0; i < n; ++i) {
        const float d = glm::length(m.positions[vs[i]] - point);
        if (best == ~0u || d < bestD) best = vs[i], bestD = d;
    }
    return best;
}

// ---------------------------------------------------------------------------
// Selection steps
// ---------------------------------------------------------------------------

size_t polySelectAll(PolySelection& sel, const Mesh& m, bool on) {
    if (!sel.topo) return 0;
    size_t changed = 0;
    switch (sel.element) {
    case PolyElement::Points:
        for (uint32_t v = 0; v < sel.points.size(); ++v)
            if (sel.pointSelected(v) != on && (!on || pointVisible(m, v))) sel.setPoint(v, on), ++changed;
        break;
    case PolyElement::Edges:
        for (uint32_t e = 0; e < sel.edges.size(); ++e)
            if (sel.edgeSelected(e) != on && (!on || edgeVisible(m, *sel.topo, e))) sel.setEdge(e, on), ++changed;
        break;
    case PolyElement::Faces:
        for (uint32_t t = 0; t < sel.faces.size(); ++t)
            if (polygonOf(m, t) == t && sel.faceSelected(t) != on && (!on || !m.triangleHidden(t))) sel.setFace(m, t, on), ++changed;
        break;
    }
    return changed;
}

size_t polySelectInvert(PolySelection& sel, const Mesh& m) {
    if (!sel.topo) return 0;
    size_t changed = 0;
    switch (sel.element) {
    case PolyElement::Points:
        for (uint32_t v = 0; v < sel.points.size(); ++v)
            if (pointVisible(m, v)) sel.setPoint(v, !sel.pointSelected(v)), ++changed;
        break;
    case PolyElement::Edges:
        for (uint32_t e = 0; e < sel.edges.size(); ++e)
            if (edgeVisible(m, *sel.topo, e)) sel.setEdge(e, !sel.edgeSelected(e)), ++changed;
        break;
    case PolyElement::Faces:
        for (uint32_t t = 0; t < sel.faces.size(); ++t)
            if (polygonOf(m, t) == t && !m.triangleHidden(t)) sel.setFace(m, t, !sel.faceSelected(t)), ++changed;
        break;
    }
    return changed;
}

size_t polySelectGrow(PolySelection& sel, const Mesh& m) {
    if (!sel.topo) return 0;
    const EdgeTopology& topo = *sel.topo;
    std::vector<uint32_t> add;
    switch (sel.element) {
    case PolyElement::Points:
        for (uint32_t v = 0; v < sel.points.size(); ++v)
            if (sel.points[v])
                for (uint32_t u : m.polygonNeighbors(v))
                    if (!sel.pointSelected(u) && pointVisible(m, u)) add.push_back(u);
        sortUnique(add);
        for (uint32_t u : add) sel.setPoint(u, true);
        return add.size();
    case PolyElement::Edges:
        for (uint32_t e = 0; e < sel.edges.size(); ++e)
            if (sel.edges[e])
                for (uint32_t v : {topo.ends[e].x, topo.ends[e].y})
                    for (uint32_t i = topo.vertexOffsets[v]; i < topo.vertexOffsets[v + 1]; ++i) {
                        const uint32_t g = topo.vertexEdges[i];
                        if (!sel.edgeSelected(g) && edgeVisible(m, topo, g)) add.push_back(g);
                    }
        sortUnique(add);
        for (uint32_t g : add) sel.setEdge(g, true);
        return add.size();
    case PolyElement::Faces:
        for (uint32_t t = 0; t < sel.faces.size(); ++t)
            if (sel.faces[t]) {
                const glm::uvec3& tri = m.triangles[t];
                for (uint32_t v : {tri.x, tri.y, tri.z})
                    for (uint32_t u : m.vertexTriangles(v))
                        if (!sel.faceSelected(u) && !m.triangleHidden(u)) add.push_back(polygonOf(m, u));
            }
        sortUnique(add);
        size_t n = 0;
        for (uint32_t t : add)
            if (!sel.faceSelected(t)) sel.setFace(m, t, true), ++n;
        return n;
    }
    return 0;
}

size_t polySelectShrink(PolySelection& sel, const Mesh& m) {
    if (!sel.topo) return 0;
    const EdgeTopology& topo = *sel.topo;
    std::vector<uint32_t> drop;
    switch (sel.element) {
    case PolyElement::Points:
        for (uint32_t v = 0; v < sel.points.size(); ++v) {
            if (!sel.points[v]) continue;
            for (uint32_t u : m.polygonNeighbors(v))
                if (!sel.pointSelected(u) && pointVisible(m, u)) {
                    drop.push_back(v);
                    break;
                }
        }
        for (uint32_t v : drop) sel.setPoint(v, false);
        return drop.size();
    case PolyElement::Edges: // the loose ends: an end no other selected edge meets (closed loops keep every edge)
        for (uint32_t e = 0; e < sel.edges.size(); ++e) {
            if (!sel.edges[e]) continue;
            for (uint32_t v : {topo.ends[e].x, topo.ends[e].y}) {
                bool met = false;
                for (uint32_t i = topo.vertexOffsets[v]; i < topo.vertexOffsets[v + 1] && !met; ++i)
                    met = topo.vertexEdges[i] != e && sel.edgeSelected(topo.vertexEdges[i]);
                if (!met) {
                    drop.push_back(e);
                    break;
                }
            }
        }
        for (uint32_t e : drop) sel.setEdge(e, false);
        return drop.size();
    case PolyElement::Faces:
        for (uint32_t t = 0; t < sel.faces.size(); ++t) {
            if (!sel.faces[t] || polygonOf(m, t) != t) continue;
            uint32_t vs[4];
            const int n = polygonVertices(m, t, vs);
            bool border = false;
            for (int i = 0; i < n && !border; ++i)
                for (uint32_t u : m.vertexTriangles(vs[i]))
                    if (!sel.faceSelected(u) && !m.triangleHidden(u)) {
                        border = true;
                        break;
                    }
            if (border) drop.push_back(t);
        }
        for (uint32_t t : drop) sel.setFace(m, t, false);
        return drop.size();
    }
    return 0;
}

size_t polySelectFaceGroup(PolySelection& sel, const Mesh& m, uint32_t group, PolySelectMode mode) {
    if (!sel.topo) return 0;
    std::vector<uint8_t> set(m.triangleCount(), 0);
    for (uint32_t t = 0; t < m.triangleCount(); ++t) set[t] = m.facegroupAt(t) == group;
    return applyFaceSet(sel, m, set, mode);
}

size_t polySelectEdgeLoop(PolySelection& sel, const Mesh& m, uint32_t e, PolySelectMode mode) {
    if (!sel.topo || e >= sel.topo->edgeCount()) return 0;
    return applyEdgeList(sel, m, edgeLoop(m, *sel.topo, e), mode);
}

size_t polySelectEdgeRing(PolySelection& sel, const Mesh& m, uint32_t e, PolySelectMode mode) {
    if (!sel.topo || e >= sel.topo->edgeCount()) return 0;
    return applyEdgeList(sel, m, polyEdgeRing(m, *sel.topo, e), mode);
}

std::vector<uint32_t> polyEdgeRing(const Mesh& m, const EdgeTopology& topo, uint32_t e) {
    if (e >= topo.edgeCount()) return {};
    // The walker Edge loop cuts with (core/EdgeLoopInsert.h), so Ring shows exactly what the op would split; an edge
    // no ring can be made through (shared by three faces) is just itself.
    std::vector<uint32_t> ring = edgeRing(m, topo, e).edges;
    if (ring.empty()) ring.push_back(e);
    return ring;
}

size_t polySelectRect(PolySelection& sel, const Mesh& m, const glm::mat4& viewProj, glm::vec2 lo, glm::vec2 hi,
                      PolySelectMode mode, const glm::vec3* eye) {
    if (!sel.topo) return 0;
    const EdgeTopology& topo = *sel.topo;
    const glm::vec2 a = glm::min(lo, hi), b = glm::max(lo, hi);
    auto inside = [&](const glm::vec3& p) {
        glm::vec2 n;
        return projectToNdc(viewProj, p, n) && n.x >= a.x && n.x <= b.x && n.y >= a.y && n.y <= b.y;
    };
    auto facing = [&](const glm::vec3& normal, const glm::vec3& p) { return !eye || glm::dot(normal, *eye - p) > 0.0f; };
    auto polygonFacing = [&](uint32_t t) { return !eye || facing(polygonNormal(m, t), polygonCentre(m, t)); };
    const bool on = mode != PolySelectMode::Remove;
    size_t changed = 0;
    if (mode == PolySelectMode::Replace) changed += polySelectAll(sel, m, false);
    switch (sel.element) {
    case PolyElement::Points:
        for (uint32_t v = 0; v < sel.points.size(); ++v) {
            if (sel.pointSelected(v) == on || !pointVisible(m, v) || !inside(m.positions[v])) continue;
            if (eye && v < m.normals.size() && !facing(m.normals[v], m.positions[v])) continue;
            sel.setPoint(v, on), ++changed;
        }
        break;
    case PolyElement::Edges:
        for (uint32_t e = 0; e < sel.edges.size(); ++e) {
            if (sel.edgeSelected(e) == on || !edgeVisible(m, topo, e)) continue;
            if (!inside(m.positions[topo.ends[e].x]) || !inside(m.positions[topo.ends[e].y])) continue;
            if (eye) {
                const glm::uvec2 s = edgeSides(m, topo, e);
                if (!polygonFacing(s.x) && (s.y == kNoEdge || !polygonFacing(s.y))) continue;
            }
            sel.setEdge(e, on), ++changed;
        }
        break;
    case PolyElement::Faces:
        for (uint32_t t = 0; t < sel.faces.size(); ++t) {
            if (polygonOf(m, t) != t || sel.faceSelected(t) == on || m.triangleHidden(t)) continue;
            if (!inside(polygonCentre(m, t)) || !polygonFacing(t)) continue;
            sel.setFace(m, t, on), ++changed;
        }
        break;
    }
    return changed;
}

// ---------------------------------------------------------------------------
// Commands
// ---------------------------------------------------------------------------

void registerPolySelectCommands(CommandRegistry& reg) {
    reg.add({"poly_mode",
             "Polygon mode (Tool > Polygon): whether the active Part can be edited in it and what it has; `element` picks "
             "what poly_select works on (points, edges or faces).",
             {{"element", T::String, "points, edges or faces: what is selected from now on (the selection converts)", false}},
             [](Document& doc, const Args& a) -> CommandResult {
                 const Mesh& m = doc.mesh;
                 const std::string why = polyModeRefusal(m);
                 if (!why.empty()) return {false, why};
                 PolySelection& sel = doc.polySelection;
                 const EdgeTopology& topo = sel.sync(m, doc.edgeSelection.topo);
                 if (a.contains("element")) {
                     PolyElement e;
                     if (!parsePolyElement(argString(a, "element"), e)) return {false, "element must be points, edges or faces"};
                     sel.setElement(m, e);
                 }
                 return {true, "Polygon mode: " + withCommas(m.triangleCount() - m.quadCount()) + " faces, " +
                                   withCommas(topo.edgeCount()) + " edges, " + withCommas(m.vertexCount()) + " points; " +
                                   polySelectionText(sel, m)};
             }});
    reg.add({"poly_select",
             "Select points, edges or faces of the active Part in Polygon mode (poly_mode element=... picks which). op: "
             "point / edge / face (element `index`: a vertex, an edge of the Part's edge list, or a triangle), loop / ring "
             "(the edge loop or edge ring through edge `index`), group (face group `group`), rect (x0 y0 x1 y1 in -1..1 "
             "under `view_proj`; `eye` x y z: only elements facing it), grow, shrink, all, none, invert, info. point / edge / "
             "face / loop / ring / group / rect replace the selection unless `add` or `remove`.",
             {{"op", T::String, "point, edge, face, loop, ring, group, rect, grow, shrink, all, none, invert or info", true},
              {"index", T::Number, "point / edge / face / loop / ring: the element", false},
              {"group", T::Number, "group: the face group id", false},
              {"x0", T::Number, "rect: left (-1..1)", false},
              {"y0", T::Number, "rect: bottom (-1..1)", false},
              {"x1", T::Number, "rect: right (-1..1)", false},
              {"y1", T::Number, "rect: top (-1..1)", false},
              {"view_proj", T::String, "rect: 16 numbers, the column-major view-projection matrix (default: the front view)", false},
              {"eye", T::String, "rect: the camera position \"x y z\": only elements facing it count", false},
              {"add", T::Bool, "add to the selection", false},
              {"remove", T::Bool, "remove from the selection", false}},
             [](Document& doc, const Args& a) -> CommandResult {
                 const Mesh& m = doc.mesh;
                 const std::string why = polyModeRefusal(m);
                 if (!why.empty()) return {false, why};
                 PolySelection& sel = doc.polySelection;
                 const EdgeTopology& topo = sel.sync(m, doc.edgeSelection.topo);
                 const std::string op = argString(a, "op");
                 const PolySelectMode mode = argBool(a, "remove", false) ? PolySelectMode::Remove
                                             : argBool(a, "add", false)  ? PolySelectMode::Add
                                                                         : PolySelectMode::Replace;
                 const bool on = mode != PolySelectMode::Remove;
                 auto done = [&](const std::string& head = {}) { return CommandResult{true, head + polySelectionText(sel, m)}; };
                 const double index = argNumber(a, "index", -1);
                 if (op == "point" || op == "edge" || op == "face") {
                     const size_t limit = op == "point" ? m.vertexCount() : op == "edge" ? topo.edgeCount() : m.triangleCount();
                     if (index < 0 || index >= double(limit)) return {false, "No such " + op + " (index=N, 0 to " + std::to_string(limit) + ")"};
                     const uint32_t i = uint32_t(index);
                     if (mode == PolySelectMode::Replace) polySelectAll(sel, m, false);
                     if (op == "point") {
                         if (sel.element != PolyElement::Points) sel.setElement(m, PolyElement::Points);
                         if (pointVisible(m, i)) sel.setPoint(i, on);
                     } else if (op == "edge") {
                         if (sel.element != PolyElement::Edges) sel.setElement(m, PolyElement::Edges);
                         if (edgeVisible(m, topo, i)) sel.setEdge(i, on);
                     } else {
                         if (sel.element != PolyElement::Faces) sel.setElement(m, PolyElement::Faces);
                         if (!m.triangleHidden(i)) sel.setFace(m, i, on);
                     }
                     return done();
                 }
                 if (op == "loop" || op == "ring") {
                     if (index < 0 || index >= double(topo.edgeCount())) return {false, "No such edge (index=N)"};
                     const size_t n = op == "loop" ? polySelectEdgeLoop(sel, m, uint32_t(index), mode)
                                                   : polySelectEdgeRing(sel, m, uint32_t(index), mode);
                     return done((op == "loop" ? "Edge loop: " : "Edge ring: ") + std::to_string(n) + " changed; ");
                 }
                 if (op == "group") {
                     const double g = argNumber(a, "group", -1);
                     if (g < 0) return {false, "group=N names the face group"};
                     polySelectFaceGroup(sel, m, uint32_t(g), mode);
                     return done("Face group " + std::to_string(uint32_t(g)) + ": ");
                 }
                 if (op == "rect") {
                     glm::mat4 vp;
                     std::string error;
                     if (!viewProjArg(m, a, vp, error)) return {false, error};
                     glm::vec3 eye;
                     const bool haveEye = a.contains("eye") && parseVec3(argString(a, "eye"), eye);
                     const glm::vec2 lo(float(argNumber(a, "x0", -1)), float(argNumber(a, "y0", -1)));
                     const glm::vec2 hi(float(argNumber(a, "x1", 1)), float(argNumber(a, "y1", 1)));
                     polySelectRect(sel, m, vp, lo, hi, mode, haveEye ? &eye : nullptr);
                     return done();
                 }
                 if (op == "grow") polySelectGrow(sel, m);
                 else if (op == "shrink") polySelectShrink(sel, m);
                 else if (op == "all") polySelectAll(sel, m, true);
                 else if (op == "none") polySelectAll(sel, m, false);
                 else if (op == "invert") polySelectInvert(sel, m);
                 else if (op != "info")
                     return {false, "op must be point, edge, face, loop, ring, group, rect, grow, shrink, all, none, invert or info"};
                 return done();
             }});
}

} // namespace sf
