// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Edge selection on the base level (#108): the edges of a Part's polygons (a quad's diagonal is no edge), picked
// one by one, as loops, along open borders or by the angle between the faces on either side. The selection is
// what edge creases (for subdivision) are set on. It lives on the Document (Document::edgeSelection) like the UV
// Editor's selection: dropped when another Part is picked or the triangles change, and not part of undo.
//
// Only the lowest subdivision level has edges to select: the levels above are made from it, so creases belong
// there, and the dense levels never pay for the edge lists (a 1M-vertex base builds them in about 0.1 s, once).
#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace sf {

class CommandRegistry;
class Mesh;
template <class T>
struct UndoChannel;

constexpr uint32_t kNoEdge = ~0u;

// The polygon edges of a mesh. An edge is named by the triangle corner that starts it (the edge from corner c to
// the next corner of its triangle, as Mesh::uvFlags seams are), the lower of the two corners on an inner edge.
struct EdgeTopology {
    uint64_t key = 0;                    // edgeTopologyKey of the mesh it was built from
    std::vector<uint32_t> twin;          // per corner: cornerTwins (core/Uv.h)
    std::vector<uint32_t> cornerEdge;    // per corner: its edge, kNoEdge on a quad's diagonal
    std::vector<uint32_t> edgeCorner;    // per edge: the corner that names it
    std::vector<glm::uvec2> ends;        // per edge: its vertices (x < y)
    std::vector<uint32_t> vertexOffsets, vertexEdges; // per vertex: its edges (CSR)
    size_t edgeCount() const { return edgeCorner.size(); }
    // An open edge (one polygon) or a non-manifold one: no polygon across it.
    bool open(uint32_t e) const { return twin[edgeCorner[e]] == ~0u; }
    size_t bytes() const;
};

// A cheap fingerprint of a mesh's polygons (counts, a sample of the triangles, quad pairs and face groups), checked
// every frame to notice that the triangles changed.
uint64_t edgeTopologyKey(const Mesh& mesh);
std::shared_ptr<const EdgeTopology> buildEdgeTopology(const Mesh& mesh);

// The polygon of triangle t (its lower triangle when it is half of a quad).
uint32_t edgePolygon(const Mesh& mesh, uint32_t t);
// The edge of triangle `t`'s polygon nearest `point` (a point on it, e.g. where a ray hit it).
uint32_t edgeNearest(const Mesh& mesh, const EdgeTopology& topo, uint32_t t, const glm::vec3& point);
// The angle between the polygons on either side of edge e, in degrees (0 = flat), or -1 on an open edge.
float edgeAngle(const Mesh& mesh, const EdgeTopology& topo, uint32_t e);
// The edge loop through e: across quads, on through every vertex with four edges and no open ones, until it
// comes back round or meets another vertex; an open edge follows the open border instead. Ordered along the loop.
std::vector<uint32_t> edgeLoop(const Mesh& mesh, const EdgeTopology& topo, uint32_t e);

// The selection (one flag per edge of `topo`).
struct EdgeSelection {
    std::shared_ptr<const EdgeTopology> topo;
    std::vector<uint8_t> selected;
    size_t count = 0;
    uint64_t revision = 0; // changes with every change of the selection (the overlay redraws its list)
    void clear() {
        ++revision;
        topo.reset();
        std::vector<uint8_t>().swap(selected);
        count = 0;
    }
    // The topology of `mesh`, (re)built when it is missing or the triangles changed (which drops the selection).
    const EdgeTopology& sync(const Mesh& mesh);
    void set(uint32_t e, bool on) {
        if (e >= selected.size() || bool(selected[e]) == on) return;
        selected[e] = on;
        on ? ++count : --count;
        ++revision;
    }
    void recount();
};

// Selection operations; each returns how many edges it changed.
// Edges sharper than `degrees` (the angle between their polygons); `within`: only among the edges selected now
// (the others are dropped). Replaces the selection.
size_t edgeSelectByAngle(const Mesh& mesh, EdgeSelection& sel, float degrees, bool within);
size_t edgeSelectOpen(EdgeSelection& sel, bool on);                 // every open (border) edge
size_t edgeSelectGrow(EdgeSelection& sel);                          // + every edge touching a selected one
// - the selected edges at the selection's loose ends (an end no other selected edge meets), so Shrink takes back
// what Grow added to a loop; closed loops keep every edge.
size_t edgeSelectShrink(EdgeSelection& sel);
size_t edgeSelectAll(EdgeSelection& sel, bool on);
size_t edgeSelectInvert(EdgeSelection& sel);
// Vertices of the selected edges, sorted (for the overlay and tests).
std::vector<uint32_t> edgeSelectedVertices(const EdgeSelection& sel);

// The status line for the selection: "12 edges selected of 1,920".
std::string edgeSelectionText(const EdgeSelection& sel);

// Creases: 0..1 per edge (1 = fully sharp), kept in Mesh::creases on both corners of the edge; subdivision keeps
// creased edges sharp. The crease of edge e (the larger of its two sides).
float edgeCrease(const Mesh& mesh, const EdgeTopology& topo, uint32_t e);
// Sets the crease of every edge flagged in `edges` (one flag per edge of `topo`) to `value` (0 clears), recording
// the old value of each corner it changes in `undo` (when given). Frees Mesh::creases once nothing is creased.
// Returns how many edges changed.
size_t edgeSetCrease(Mesh& mesh, const EdgeTopology& topo, const std::vector<uint8_t>& edges, float value,
                     UndoChannel<float>* undo);
// How many edges have a crease.
size_t edgeCreasedCount(const Mesh& mesh, const EdgeTopology& topo);

// edge_select and edge_crease (core/EdgeSelect.cpp).
void registerEdgeCommands(CommandRegistry& registry);

} // namespace sf
