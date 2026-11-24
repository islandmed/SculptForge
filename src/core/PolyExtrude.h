// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Extrude (Polygon mode): pushes the selected polygons out along their normals and joins them to the surface
// with new side walls, or stands a strip of new polygons on selected open border edges. Pure functions on a
// mesh: the caller (the Polygon mode's op, core/PolyMode) owns preview, undo and the panel.
//
// Faces: the selection is a set of polygons (a quad, two triangles paired in Mesh::quadPartners, is one polygon:
// either half selects it). As a *region*, the connected selection moves as one piece: its inside vertices move,
// its border vertices split from the surface (the old vertex stays with the unselected neighbours) and a wall
// quad stands on every border edge. As *individual* polygons each one gets its own copies of its vertices and
// walls on every edge, so neighbouring selected polygons come apart with a groove between them. A closed mesh
// whose every polygon is selected has no border: it just inflates. Zero distance is allowed and gives
// zero-height walls (the Polygon mode's gizmo pulls them up afterwards). The inset moves the border vertices
// into the cap first (an offset of the border edges by that distance, mitred at corners, scaled back when a
// cap polygon would fold), so the walls slant inward.
//
// The result keeps every attribute it can: the caps keep their face groups, UVs, creases, mask and paint; new
// vertices copy their source vertex (mask, paint, sculpt and paint layers); the walls are quads with a face
// group of their own (or the cap's) and UVs laid out beside the cap edge at the cap's texel density. Hidden
// polygons never take part. Subdivision levels and Micro are dropped (the triangles changed), as every
// topology edit does; the Polygon mode refuses such Parts before it gets here.
//
// Topology once, positions per move: planExtrude() works out the new triangles, walls and offset directions
// for a selection (O(triangles)); buildExtrude() makes the mesh for one distance / inset, and moveExtrude()
// re-places only the moved vertices of that mesh for another distance / inset (the live slider), with
// nothing to rebuild but normals.
#pragma once

#include "core/Mesh.h"

#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace sf {

enum class ExtrudeAlong : uint8_t {
    Normals, // each cap vertex along the average normal of the selected polygons around it (a curved cap inflates)
    Region,  // every vertex of a connected region along the region's one average normal (the cap keeps its shape)
};
enum class ExtrudeSideGroup : uint8_t {
    New,     // the walls get one new face group (select them with one click afterwards)
    Inherit, // each wall takes its cap polygon's group
};

struct ExtrudeOptions {
    bool individual = false; // each polygon on its own instead of the connected region (see the top of this file)
    ExtrudeAlong along = ExtrudeAlong::Normals;
    // Along::Normals: where the cap bends, scale each vertex's move so every cap polygon still moves `distance`
    // along its own normal (an offset of the surface: two sides of a box come out the same amount), up to 3.3x at
    // sharp bends. Off: the plain averaged normal (a sharp ridge comes out less).
    bool even = true;
    ExtrudeSideGroup sideGroup = ExtrudeSideGroup::New;
};

// What the border edges of an open mesh are extruded along.
enum class EdgeExtrudeAlong : uint8_t {
    Outward, // in the surface, away from the polygon the edge belongs to (extends the surface; mitred at corners)
    Normals, // along the border vertices' normals (a wall standing on the surface)
    Vector,  // along EdgeExtrudeOptions::direction (a unit vector; the gizmo's axis)
};
struct EdgeExtrudeOptions {
    EdgeExtrudeAlong along = EdgeExtrudeAlong::Outward;
    glm::vec3 direction{0.0f, 1.0f, 0.0f};
    ExtrudeSideGroup sideGroup = ExtrudeSideGroup::New;
};

// The topology of one extrusion, independent of distance and inset.
struct ExtrudePlan {
    bool faces = true; // a face extrude (false: edges)
    // Per vertex of the result: the source vertex of `mesh` it copies (identity for the kept ones).
    std::vector<uint32_t> source;
    // The vertices that move (the caps' vertices, or the edge strip's new vertices), each with its offset
    // direction (unit, or mitred where `even` / an outward edge extrude asks) and its inset direction (in the cap,
    // mitred): p + inset * insetDir + distance * dir.
    struct Moved {
        uint32_t vertex;
        glm::vec3 dir;
        glm::vec3 insetDir;
    };
    std::vector<Moved> moved;
    std::vector<glm::uvec3> triangles; // the result's triangles (the kept ones first, in order, then the walls)
    std::vector<uint32_t> wallSource;  // per wall triangle (from `firstWall`): the cap/owner triangle its edge came from
    std::vector<uint8_t> wallFlip;     // per wall triangle: its edge runs the other way round in the owner (edges)
    std::vector<glm::uvec2> wallEdge;  // per wall triangle: the owner's edge (a -> b) the wall stands on
    size_t firstWall = 0;              // index of the first wall triangle
    std::vector<uint8_t> cap;          // per result triangle: a moved cap polygon (faces) / a new strip quad (edges)
    size_t polygons = 0;               // polygons extruded (faces) or edges extruded (edges)
    size_t walls = 0;                  // wall quads added
    size_t newVertices = 0;
    std::vector<glm::uvec2> newBorder; // edges: the strip's new open border edges, as result vertex pairs
    uint32_t wallGroup = 0;            // ExtrudeSideGroup::New: the group the walls get
    bool inheritGroup = false;
    std::string error;                 // why planning refused (empty on success)
    bool ok() const { return error.empty(); }
};

// Plans a face extrude of the polygons flagged in `selected` (one flag per triangle of `mesh`; a quad counts
// as selected when either half is). Refuses (plan.error) when nothing visible is selected.
ExtrudePlan planExtrude(const Mesh& mesh, std::span<const uint8_t> selected, const ExtrudeOptions& options);
// Plans an edge extrude of `edges` (pairs of vertex indices of `mesh`): only open border edges count (an edge of
// exactly one polygon); the others are skipped, and the plan refuses when none is left.
ExtrudePlan planEdgeExtrude(const Mesh& mesh, std::span<const glm::uvec2> edges, const EdgeExtrudeOptions& options);

// The mesh for `plan` at `distance` along the offset directions and `inset` into the caps (faces only; edges
// ignore it). Topology and normals are built; `plan.cap` is the new selection.
Mesh buildExtrude(const Mesh& mesh, const ExtrudePlan& plan, float distance, float inset = 0.0f);
// Re-places the moved vertices of `out` (a mesh buildExtrude made from `mesh` and `plan`) for another distance
// and inset, and recomputes the normals around them. Returns the inset actually applied (scaled back when a cap
// would fold over).
float moveExtrude(const Mesh& mesh, const ExtrudePlan& plan, float distance, float inset, Mesh& out);

// One call: plan + build. `status` gets "Extruded 12 polygons (24 walls)" or the reason it refused.
bool extrudeFaces(const Mesh& mesh, std::span<const uint8_t> selected, const ExtrudeOptions& options, float distance,
                  float inset, Mesh& out, std::vector<uint8_t>& newSelection, std::string& status);
bool extrudeEdges(const Mesh& mesh, std::span<const glm::uvec2> edges, const EdgeExtrudeOptions& options, float distance,
                  Mesh& out, std::vector<glm::uvec2>& newBorder, std::string& status);

// Completes `selected` to whole polygons (the partner of a selected quad half) and clears hidden triangles;
// returns how many polygons that is.
size_t completePolygonSelection(const Mesh& mesh, std::vector<uint8_t>& selected);

} // namespace sf
