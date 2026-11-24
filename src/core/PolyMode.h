// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Polygon mode (Tool > Polygon): the selection every polygon-modelling operation works on. A Part's polygons (quads
// as the user sees them, triangles otherwise) are picked as Points, Edges or Faces: click, Shift adds, Ctrl removes,
// a box drag, Grow / Shrink, by face group, as an edge loop or ring. The selection lives on the Document
// (Document::polySelection) like the edge selection: dropped when another Part is picked or the triangles change,
// not part of undo. The Transform gizmo moves it, and the operations (core/PolyOps.h: Extrude, Inset, Bevel, Edge
// loop, Span...) take it as their input and leave their result selected.
//
// Names and ids: a vertex is its index in Mesh::positions; an edge is its index in the EdgeTopology (core/
// EdgeSelect.h: a quad's diagonal is no edge); a face is a triangle index, and a quad is named by its lower
// triangle (edgePolygon): selecting either half selects both, counts count polygons.
//
// Polygon mode is for the low and medium-density meshes polygon modelling is done on: it refuses a Part with a Micro
// level, with subdivision levels (the operations change the base topology the levels are built on), a Forge Sphere
// or Tube Part before Bake, or more than kPolyModeMaxTriangles (polyModeRefusal says why, in the user's words).
#pragma once

#include "core/EdgeSelect.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace sf {

class CommandRegistry;
class Mesh;

enum class PolyElement : uint8_t { Points, Edges, Faces };
const char* polyElementName(PolyElement e); // "points", "edges", "faces"
bool parsePolyElement(const std::string& name, PolyElement& out);

// The densest Part Polygon mode takes (triangles). 2M triangles is a 1M-quad cage: well above anything modelled by hand,
// and the overlay, the box select and a whole-mesh undo step stay quick on the floor tier at that size.
constexpr size_t kPolyModeMaxTriangles = 2'000'000;
// Why `mesh` can't be edited in Polygon mode (a status line), or "" when it can.
std::string polyModeRefusal(const Mesh& mesh, size_t maxTriangles = kPolyModeMaxTriangles);

// How a selection step combines with what is selected: replace it, add to it, or remove from it.
enum class PolySelectMode : uint8_t { Replace, Add, Remove };

struct PolySelection {
    std::shared_ptr<const EdgeTopology> topo; // the polygons it was made on (null = nothing synced yet)
    PolyElement element = PolyElement::Faces;
    std::vector<uint8_t> points; // per vertex
    std::vector<uint8_t> edges;  // per edge of `topo`
    std::vector<uint8_t> faces;  // per triangle (both halves of a quad always agree)
    size_t pointCount = 0, edgeCount = 0, faceCount = 0; // faceCount counts polygons, not triangles
    uint64_t revision = 0; // changes with every change (the overlay rebuilds its lists)

    void clear();
    // The topology of `mesh`, (re)built when missing or the triangles changed (which drops the selection). `reuse`:
    // a topology built for the same mesh elsewhere (Document::edgeSelection's), taken instead of building another.
    const EdgeTopology& sync(const Mesh& mesh, const std::shared_ptr<const EdgeTopology>& reuse = nullptr);
    bool synced(const Mesh& mesh) const;

    // Switching what is picked converts the selection: faces -> their points / edges; points or edges -> the faces
    // (edges) all of whose points (edges) are selected.
    void setElement(const Mesh& mesh, PolyElement e);

    void setPoint(uint32_t v, bool on);
    void setEdge(uint32_t e, bool on);
    void setFace(const Mesh& mesh, uint32_t t, bool on); // t or its quad partner: both halves
    bool pointSelected(uint32_t v) const { return v < points.size() && points[v]; }
    bool edgeSelected(uint32_t e) const { return e < edges.size() && edges[e]; }
    bool faceSelected(uint32_t t) const { return t < faces.size() && faces[t]; }

    // Of the current element: how many are selected, and how many there are (faces: polygons).
    size_t count() const;
    size_t total(const Mesh& mesh) const;
    bool empty() const { return count() == 0; }

    // What the selection covers, whatever the element: the vertices of the selected elements; the triangles whose
    // corners are all covered (Faces: the selected triangles); the edges whose ends are both covered (Edges: the
    // selected edges). Sorted. These are what the gizmo moves and the operations take.
    std::vector<uint32_t> vertices(const Mesh& mesh) const;
    std::vector<uint32_t> triangles(const Mesh& mesh) const;
    std::vector<uint32_t> edgeList(const Mesh& mesh) const;
    // Per-vertex weights for a transform (core/MeshTransform.h): 1 on the covered vertices, 0 elsewhere. Empty when
    // nothing is selected.
    std::vector<float> weights(const Mesh& mesh) const;

private:
    void recount();
};

// "12 faces selected of 600" (quads counted as faces), "3 points selected of 482", "1 edge selected of 1,200".
std::string polySelectionText(const PolySelection& sel, const Mesh& mesh);

// Picking on the polygon a ray hit (triangle `t`, at `point` on it): the nearest vertex of that polygon; the nearest
// edge is edgeNearest (core/EdgeSelect.h); the face is `t` itself (setFace takes either half of a quad).
uint32_t polyNearestVertex(const Mesh& mesh, uint32_t t, const glm::vec3& point);

// Selection steps; each returns how many elements of the current kind it changed. Hidden triangles (and the
// vertices and edges only they touch) are never selected.
size_t polySelectAll(PolySelection& sel, const Mesh& mesh, bool on);
size_t polySelectInvert(PolySelection& sel, const Mesh& mesh);
// Grow: + every element sharing a vertex with a selected one. Shrink: - every selected element that touches an
// unselected one (so Shrink takes back what Grow added).
size_t polySelectGrow(PolySelection& sel, const Mesh& mesh);
size_t polySelectShrink(PolySelection& sel, const Mesh& mesh);
// The faces of face group `group` (in Faces; in Points / Edges: their points / edges).
size_t polySelectFaceGroup(PolySelection& sel, const Mesh& mesh, uint32_t group, PolySelectMode mode);
// The edge loop through `e` (edgeLoop) or the edge ring across the quads beside it (polyEdgeRing), in Edges; in
// Points / Faces: the loop's points / the ring's faces.
size_t polySelectEdgeLoop(PolySelection& sel, const Mesh& mesh, uint32_t e, PolySelectMode mode);
size_t polySelectEdgeRing(PolySelection& sel, const Mesh& mesh, uint32_t e, PolySelectMode mode);
// Box select in normalised device coordinates (-1..1, y up) under `viewProj`: points inside, edges with both ends
// inside, faces whose centre is inside. With `eye` (scene space) only elements on the side facing it count.
size_t polySelectRect(PolySelection& sel, const Mesh& mesh, const glm::mat4& viewProj, glm::vec2 lo, glm::vec2 hi,
                      PolySelectMode mode, const glm::vec3* eye = nullptr);

// The edge ring through `e`: across each quad beside it to the opposite edge, on until it comes back round or meets a
// triangle, an open edge or a face already crossed. Ordered along the ring; just `e` on a triangle.
std::vector<uint32_t> polyEdgeRing(const Mesh& mesh, const EdgeTopology& topo, uint32_t e);

// poly_mode (can the active Part enter, and its counts) and poly_select (core/PolyMode.cpp).
void registerPolySelectCommands(CommandRegistry& registry);

} // namespace sf
