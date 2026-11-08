// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Edge selection on the base level (#108); see core/EdgeSelect.h.
#include "core/EdgeSelect.h"

#include "core/Commands.h"
#include "core/Document.h"
#include "core/Mesh.h"
#include "core/Multires.h"
#include "core/ThreadPool.h"
#include "core/Undo.h"
#include "core/Uv.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <unordered_set>

namespace sf {

namespace {

using T = ParamSpec::Type;

glm::vec3 triangleCross(const Mesh& m, uint32_t t) {
    const glm::uvec3& tri = m.triangles[t];
    return glm::cross(m.positions[tri.y] - m.positions[tri.x], m.positions[tri.z] - m.positions[tri.x]);
}

// The unit normal of triangle t's polygon (both halves of a quad, area weighted).
glm::vec3 polygonNormal(const Mesh& m, uint32_t t) {
    glm::vec3 n = triangleCross(m, t);
    const uint32_t p = m.quadPartnerAt(t);
    if (p != Mesh::kNoQuad) n += triangleCross(m, p);
    const float l = glm::length(n);
    return l > 0.0f ? n / l : glm::vec3(0.0f);
}

float segmentDistance(const glm::vec3& p, const glm::vec3& a, const glm::vec3& b) {
    const glm::vec3 ab = b - a;
    const float l2 = glm::dot(ab, ab);
    const float s = l2 > 0.0f ? std::clamp(glm::dot(p - a, ab) / l2, 0.0f, 1.0f) : 0.0f;
    return glm::length(a + ab * s - p);
}

// The polygons on either side of edge e (kNoEdge across an open edge).
glm::uvec2 edgePolygons(const Mesh& m, const EdgeTopology& topo, uint32_t e) {
    const uint32_t c = topo.edgeCorner[e], d = topo.twin[c];
    return {edgePolygon(m, c / 3), d == kNoCorner ? kNoEdge : edgePolygon(m, d / 3)};
}

uint32_t otherEnd(const EdgeTopology& topo, uint32_t e, uint32_t v) {
    const glm::uvec2 ends = topo.ends[e];
    return ends.x == v ? ends.y : ends.x;
}

// The edge the loop through `e` continues on past vertex v, or kNoEdge where it ends.
uint32_t loopNext(const Mesh& m, const EdgeTopology& topo, uint32_t e, uint32_t v) {
    const uint32_t* first = topo.vertexEdges.data() + topo.vertexOffsets[v];
    const uint32_t n = topo.vertexOffsets[v + 1] - topo.vertexOffsets[v];
    uint32_t next = kNoEdge;
    if (topo.open(e)) { // along the open border: the one other open edge at v
        for (uint32_t i = 0; i < n; ++i) {
            if (first[i] == e || !topo.open(first[i])) continue;
            if (next != kNoEdge) return kNoEdge; // a border meeting another: ambiguous
            next = first[i];
        }
        return next;
    }
    if (n != 4) return kNoEdge; // across quads: only through vertices with four edges
    const glm::uvec2 pe = edgePolygons(m, topo, e);
    for (uint32_t i = 0; i < n; ++i) {
        const uint32_t g = first[i];
        if (topo.open(g)) return kNoEdge;
        if (g == e) continue;
        const glm::uvec2 pg = edgePolygons(m, topo, g);
        if (pg.x == pe.x || pg.x == pe.y || pg.y == pe.x || pg.y == pe.y) continue; // a side of e's polygons
        if (next != kNoEdge) return kNoEdge;
        next = g;
    }
    return next;
}

std::string count(size_t n, const char* what) { return std::to_string(n) + " " + what + (n == 1 ? "" : "s"); }

} // namespace

size_t EdgeTopology::bytes() const {
    return (twin.size() + cornerEdge.size() + edgeCorner.size() + vertexOffsets.size() + vertexEdges.size()) * 4 +
           ends.size() * 8;
}

uint64_t edgeTopologyKey(const Mesh& m) {
    uint64_t h = 1469598103934665603ull;
    auto mix = [&h](uint64_t v) {
        for (int i = 0; i < 8; ++i) h = (h ^ ((v >> (i * 8)) & 0xffu)) * 1099511628211ull;
    };
    const size_t nt = m.triangleCount();
    mix(m.vertexCount());
    mix(nt);
    mix(m.quadPartners.size());
    mix(m.facegroups.size());
    const size_t stride = std::max<size_t>(1, nt / 4096);
    auto sample = [&](size_t t) {
        const glm::uvec3& tri = m.triangles[t];
        mix(uint64_t(tri.x) | uint64_t(tri.y) << 32);
        mix(tri.z);
        if (t < m.quadPartners.size()) mix(m.quadPartners[t]);
        if (t < m.facegroups.size()) mix(m.facegroups[t]);
    };
    for (size_t t = 0; t < nt; t += stride) sample(t);
    if (nt) sample(nt - 1);
    return h;
}

std::shared_ptr<const EdgeTopology> buildEdgeTopology(const Mesh& m) {
    auto topo = std::make_shared<EdgeTopology>();
    EdgeTopology& tp = *topo;
    tp.key = edgeTopologyKey(m);
    tp.twin = cornerTwins(m);
    const size_t nc = m.triangleCount() * 3, nv = m.vertexCount();
    tp.cornerEdge.assign(nc, kNoEdge);
    for (uint32_t c = 0; c < nc; ++c) {
        const uint32_t d = tp.twin[c];
        if (d != kNoCorner && d / 3 == m.quadPartnerAt(c / 3)) continue; // a quad's diagonal
        if (d != kNoCorner && d < c) continue;                            // named by its twin
        const glm::uvec3& t = m.triangles[c / 3];
        const uint32_t a = t[c % 3], b = t[(c % 3 + 1) % 3];
        tp.cornerEdge[c] = uint32_t(tp.edgeCorner.size());
        if (d != kNoCorner) tp.cornerEdge[d] = uint32_t(tp.edgeCorner.size());
        tp.edgeCorner.push_back(c);
        tp.ends.push_back({std::min(a, b), std::max(a, b)});
    }
    tp.vertexOffsets.assign(nv + 1, 0);
    for (const glm::uvec2& e : tp.ends) ++tp.vertexOffsets[e.x + 1], ++tp.vertexOffsets[e.y + 1];
    for (size_t v = 0; v < nv; ++v) tp.vertexOffsets[v + 1] += tp.vertexOffsets[v];
    tp.vertexEdges.resize(tp.ends.size() * 2);
    std::vector<uint32_t> fill(tp.vertexOffsets.begin(), tp.vertexOffsets.end() - 1);
    for (uint32_t e = 0; e < tp.ends.size(); ++e) {
        tp.vertexEdges[fill[tp.ends[e].x]++] = e;
        tp.vertexEdges[fill[tp.ends[e].y]++] = e;
    }
    return topo;
}

uint32_t edgePolygon(const Mesh& m, uint32_t t) {
    const uint32_t p = m.quadPartnerAt(t);
    return p == Mesh::kNoQuad ? t : std::min(t, p);
}

uint32_t edgeNearest(const Mesh& m, const EdgeTopology& topo, uint32_t t, const glm::vec3& point) {
    if (size_t(t) * 3 + 2 >= topo.cornerEdge.size()) return kNoEdge;
    uint32_t best = kNoEdge;
    float bestD = 0.0f;
    auto consider = [&](uint32_t tri) {
        for (uint32_t k = 0; k < 3; ++k) {
            const uint32_t c = tri * 3 + k, e = topo.cornerEdge[c];
            if (e == kNoEdge) continue;
            const float d = segmentDistance(point, m.positions[topo.ends[e].x], m.positions[topo.ends[e].y]);
            if (best == kNoEdge || d < bestD) best = e, bestD = d;
        }
    };
    consider(t);
    const uint32_t p = m.quadPartnerAt(t);
    if (p != Mesh::kNoQuad) consider(p);
    return best;
}

float edgeAngle(const Mesh& m, const EdgeTopology& topo, uint32_t e) {
    const uint32_t c = topo.edgeCorner[e], d = topo.twin[c];
    if (d == kNoCorner) return -1.0f;
    const float cosA = glm::dot(polygonNormal(m, c / 3), polygonNormal(m, d / 3));
    return glm::degrees(std::acos(std::clamp(cosA, -1.0f, 1.0f)));
}

std::vector<uint32_t> edgeLoop(const Mesh& m, const EdgeTopology& topo, uint32_t e) {
    if (e >= topo.edgeCount()) return {};
    std::unordered_set<uint32_t> seen{e};
    // Walks from e past vertex v; true when it came back round to e.
    auto walk = [&](uint32_t v, std::vector<uint32_t>& out) {
        uint32_t cur = e;
        for (;;) {
            const uint32_t next = loopNext(m, topo, cur, v);
            if (next == e) return true;
            if (next == kNoEdge || !seen.insert(next).second) return false;
            out.push_back(next);
            v = otherEnd(topo, next, v);
            cur = next;
        }
    };
    std::vector<uint32_t> forward, backward;
    if (!walk(topo.ends[e].y, forward)) walk(topo.ends[e].x, backward);
    std::vector<uint32_t> loop(backward.rbegin(), backward.rend());
    loop.push_back(e);
    loop.insert(loop.end(), forward.begin(), forward.end());
    return loop;
}

const EdgeTopology& EdgeSelection::sync(const Mesh& mesh) {
    if (!topo || topo->key != edgeTopologyKey(mesh) || topo->cornerEdge.size() != mesh.triangleCount() * 3) {
        topo = buildEdgeTopology(mesh);
        selected.assign(topo->edgeCount(), 0);
        count = 0;
        ++revision;
    }
    return *topo;
}

void EdgeSelection::recount() {
    count = size_t(std::count_if(selected.begin(), selected.end(), [](uint8_t s) { return s != 0; }));
}

size_t edgeSelectByAngle(const Mesh& m, EdgeSelection& sel, float degrees, bool within) {
    const EdgeTopology& topo = sel.sync(m);
    std::atomic<size_t> changed{0};
    poolFor(topo.edgeCount(), [&](size_t b, size_t e) {
        size_t n = 0;
        for (size_t i = b; i < e; ++i) {
            const bool sharp = edgeAngle(m, topo, uint32_t(i)) > degrees;
            const uint8_t on = sharp && (!within || sel.selected[i]);
            n += on != sel.selected[i];
            sel.selected[i] = on;
        }
        changed += n;
    });
    sel.recount();
    ++sel.revision;
    return changed;
}

size_t edgeSelectOpen(EdgeSelection& sel, bool on) {
    if (!sel.topo) return 0;
    const size_t before = sel.count;
    for (uint32_t e = 0; e < sel.topo->edgeCount(); ++e)
        if (sel.topo->open(e)) sel.set(e, on);
    return before > sel.count ? before - sel.count : sel.count - before;
}

size_t edgeSelectGrow(EdgeSelection& sel) {
    if (!sel.topo || sel.count == 0) return 0;
    const EdgeTopology& topo = *sel.topo;
    std::vector<uint8_t> vert(topo.vertexOffsets.size() - 1, 0);
    for (uint32_t e = 0; e < topo.edgeCount(); ++e)
        if (sel.selected[e]) vert[topo.ends[e].x] = vert[topo.ends[e].y] = 1;
    const size_t before = sel.count;
    for (uint32_t e = 0; e < topo.edgeCount(); ++e)
        if (vert[topo.ends[e].x] || vert[topo.ends[e].y]) sel.set(e, true);
    return sel.count - before;
}

size_t edgeSelectShrink(EdgeSelection& sel) {
    if (!sel.topo || sel.count == 0) return 0;
    const EdgeTopology& topo = *sel.topo;
    std::vector<uint32_t> degree(topo.vertexOffsets.size() - 1, 0);
    for (uint32_t e = 0; e < topo.edgeCount(); ++e)
        if (sel.selected[e]) ++degree[topo.ends[e].x], ++degree[topo.ends[e].y];
    const size_t before = sel.count;
    for (uint32_t e = 0; e < topo.edgeCount(); ++e)
        if (sel.selected[e] && (degree[topo.ends[e].x] == 1 || degree[topo.ends[e].y] == 1)) sel.set(e, false);
    return before - sel.count;
}

size_t edgeSelectAll(EdgeSelection& sel, bool on) {
    const size_t before = sel.count;
    std::fill(sel.selected.begin(), sel.selected.end(), uint8_t(on));
    sel.count = on ? sel.selected.size() : 0;
    ++sel.revision;
    return on ? sel.count - before : before;
}

size_t edgeSelectInvert(EdgeSelection& sel) {
    for (uint8_t& s : sel.selected) s = !s;
    sel.count = sel.selected.size() - sel.count;
    ++sel.revision;
    return sel.selected.size();
}

std::vector<uint32_t> edgeSelectedVertices(const EdgeSelection& sel) {
    std::vector<uint32_t> out;
    if (!sel.topo) return out;
    for (uint32_t e = 0; e < sel.selected.size(); ++e)
        if (sel.selected[e]) out.push_back(sel.topo->ends[e].x), out.push_back(sel.topo->ends[e].y);
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

std::string edgeSelectionText(const EdgeSelection& sel) {
    const size_t total = sel.topo ? sel.topo->edgeCount() : 0;
    return count(sel.count, "edge") + " selected of " + std::to_string(total);
}

float edgeCrease(const Mesh& m, const EdgeTopology& topo, uint32_t e) {
    if (m.creases.size() != m.triangleCount() * 3 || e >= topo.edgeCount()) return 0.0f;
    const uint32_t c = topo.edgeCorner[e], d = topo.twin[c];
    return std::max(m.creases[c], d == kNoCorner ? 0.0f : m.creases[d]);
}

size_t edgeSetCrease(Mesh& m, const EdgeTopology& topo, const std::vector<uint8_t>& edges, float value,
                     UndoChannel<float>* undo) {
    value = std::isfinite(value) ? std::clamp(value, 0.0f, 1.0f) : 0.0f;
    if (value == 0.0f && !m.hasCreases()) return 0;
    std::vector<float>& cr = m.ensureCreases();
    size_t changed = 0;
    auto set = [&](uint32_t c) {
        if (c == kNoCorner || cr[c] == value) return false;
        if (undo) undo->add(c, cr[c]);
        cr[c] = value;
        return true;
    };
    for (uint32_t e = 0; e < edges.size() && e < topo.edgeCount(); ++e) {
        if (!edges[e]) continue;
        const uint32_t c = topo.edgeCorner[e];
        const bool a = set(c), b = set(topo.twin[c]);
        changed += a || b;
    }
    if (value == 0.0f && std::all_of(cr.begin(), cr.end(), [](float f) { return f == 0.0f; })) m.clearCreases();
    return changed;
}

size_t edgeCreasedCount(const Mesh& m, const EdgeTopology& topo) {
    if (!m.hasCreases()) return 0;
    size_t n = 0;
    for (uint32_t e = 0; e < topo.edgeCount(); ++e) n += edgeCrease(m, topo, e) > 0.0f;
    return n;
}

void registerEdgeCommands(CommandRegistry& reg) {
    reg.add({"edge_select",
             "Select edges of the active Part's base level (what edge creases are set on). op: edge (edge `edge`, or the "
             "edge between vertices `a` and `b`), loop (the edge loop through that edge), angle (edges sharper than "
             "`degrees`, default 30; `within`: only among those selected now), open (the open border edges), grow, "
             "shrink, all, none, invert, info. edge / loop / open replace the selection unless `add` or `remove`.",
             {{"op", T::String, "edge, loop, angle, open, grow, shrink, all, none, invert or info", true},
              {"edge", T::Number, "edge / loop: an edge index (edge_select op=info counts them)", false},
              {"a", T::Number, "edge / loop: one end vertex", false},
              {"b", T::Number, "edge / loop: the other end vertex", false},
              {"degrees", T::Number, "angle: the least angle between the faces on either side (default 30)", false},
              {"within", T::Bool, "angle: only among the edges selected now (default false)", false},
              {"add", T::Bool, "edge / loop / open: add to the selection", false},
              {"remove", T::Bool, "edge / loop / open: remove from the selection", false}},
             [](Document& doc, const Args& a) -> CommandResult {
                 const Mesh& m = doc.mesh;
                 if (m.triangleCount() == 0) return {false, "The active Part has no faces"};
                 if (subdivCurrentLevel(m) > 0)
                     return {false, "Edges are selected on the base level (Level 1): step down with Lower Level first"};
                 EdgeSelection& sel = doc.edgeSelection;
                 const EdgeTopology& topo = sel.sync(m);
                 const std::string op = argString(a, "op");
                 const bool add = argBool(a, "add", false), remove = argBool(a, "remove", false);
                 auto edgeArg = [&]() -> uint32_t {
                     if (a.contains("edge")) {
                         const double e = argNumber(a, "edge", -1);
                         return e >= 0 && e < double(topo.edgeCount()) ? uint32_t(e) : kNoEdge;
                     }
                     const double va = argNumber(a, "a", -1), vb = argNumber(a, "b", -1);
                     if (va < 0 || vb < 0 || va >= double(m.vertexCount()) || vb >= double(m.vertexCount())) return kNoEdge;
                     const uint32_t x = uint32_t(va), y = uint32_t(vb);
                     for (uint32_t i = topo.vertexOffsets[x]; i < topo.vertexOffsets[x + 1]; ++i)
                         if (otherEnd(topo, topo.vertexEdges[i], x) == y) return topo.vertexEdges[i];
                     return kNoEdge;
                 };
                 auto apply = [&](const std::vector<uint32_t>& edges) {
                     if (!add && !remove) edgeSelectAll(sel, false);
                     for (uint32_t e : edges) sel.set(e, !remove);
                 };
                 if (op == "edge" || op == "loop") {
                     const uint32_t e = edgeArg();
                     if (e == kNoEdge) return {false, "No such edge (give edge=N, or a=V b=W for two vertices an edge joins)"};
                     const std::vector<uint32_t> edges = op == "loop" ? edgeLoop(m, topo, e) : std::vector<uint32_t>{e};
                     apply(edges);
                     return {true, (op == "loop" ? "Edge loop of " + count(edges.size(), "edge") + ": " : std::string()) +
                                       edgeSelectionText(sel)};
                 }
                 if (op == "angle") {
                     const float deg = float(std::clamp(argNumber(a, "degrees", 30.0), 0.0, 180.0));
                     edgeSelectByAngle(m, sel, deg, argBool(a, "within", false));
                     char head[64];
                     std::snprintf(head, sizeof head, "Edges sharper than %g degrees: ", double(deg));
                     return {true, head + edgeSelectionText(sel)};
                 }
                 if (op == "open") {
                     if (!add && !remove) edgeSelectAll(sel, false);
                     edgeSelectOpen(sel, !remove);
                     return {true, "Open edges: " + edgeSelectionText(sel)};
                 }
                 if (op == "grow") edgeSelectGrow(sel);
                 else if (op == "shrink") edgeSelectShrink(sel);
                 else if (op == "all") edgeSelectAll(sel, true);
                 else if (op == "none") edgeSelectAll(sel, false);
                 else if (op == "invert") edgeSelectInvert(sel);
                 else if (op != "info") return {false, "op must be edge, loop, angle, open, grow, shrink, all, none, invert or info"};
                 return {true, edgeSelectionText(sel)};
             }});
    reg.add({"edge_crease",
             "Crease the selected edges of the active Part's base level (edge_select picks them), for subdivision: "
             "value 1 is fully sharp, 0.5 half as sharp, 0 (or clear) removes the crease. `all`: every "
             "creased edge of the Part instead of the selection (with clear: removes every crease). One undo step.",
             {{"value", T::Number, "the crease, 0..1 (default 1)", false},
              {"clear", T::Bool, "remove the crease (value 0)", false},
              {"all", T::Bool, "every creased edge of the Part, not just the selected ones", false},
              {"info", T::Bool, "only report how many edges are creased", false}},
             [](Document& doc, const Args& a) -> CommandResult {
                 Mesh& m = doc.mesh;
                 if (m.triangleCount() == 0) return {false, "The active Part has no faces"};
                 if (subdivCurrentLevel(m) > 0)
                     return {false, "Creases are set on the base level (Level 1): step down with Lower Level first"};
                 EdgeSelection& sel = doc.edgeSelection;
                 const EdgeTopology& topo = sel.sync(m);
                 auto summary = [&] { return count(edgeCreasedCount(m, topo), "creased edge") + " of " +
                                             std::to_string(topo.edgeCount()); };
                 if (argBool(a, "info", false)) return {true, summary()};
                 const float value = argBool(a, "clear", false)
                                         ? 0.0f
                                         : float(std::clamp(argNumber(a, "value", 1.0), 0.0, 1.0));
                 std::vector<uint8_t> creased;
                 if (argBool(a, "all", false)) {
                     creased.assign(topo.edgeCount(), 0);
                     for (uint32_t e = 0; e < topo.edgeCount(); ++e) creased[e] = edgeCrease(m, topo, e) > 0.0f;
                 }
                 const std::vector<uint8_t>& edges = argBool(a, "all", false) ? creased : sel.selected;
                 if (std::find(edges.begin(), edges.end(), uint8_t(1)) == edges.end())
                     return {false, argBool(a, "all", false) ? "No edge is creased"
                                                             : "No edges selected (Tool > Geometry > Edges, or edge_select)"};
                 UndoRecord r;
                 r.label = value > 0.0f ? "Crease Edges" : "Uncrease Edges";
                 const size_t changed = edgeSetCrease(m, topo, edges, value, &r.creases);
                 if (changed == 0) return {true, "Nothing to change: " + summary()};
                 doc.undo.push(std::move(r));
                 char head[64];
                 if (value > 0.0f) std::snprintf(head, sizeof head, "Crease %g on ", double(value));
                 else std::snprintf(head, sizeof head, "Crease removed from ");
                 return {true, head + count(changed, "edge") + ": " + summary()};
             }});
}

} // namespace sf
