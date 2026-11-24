// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Bevel (Polygon mode): cuts the selected polygon edges (or vertices) back by a width and fills the cut with a strip
// of `segments` faces across, shaped by a profile, so a hard edge becomes a chamfer or a rounded edge. Pure: the
// mesh goes in, a new mesh comes out (the caller makes the undo step).
//
// Edges: each selected edge is replaced by a strip of `segments` quads along it. The faces on either side are cut
// back by `width` (measured in the face, square to the edge); where a cut edge meets an unselected edge the cut
// slides along that edge instead. Where selected edges meet at a vertex the strips are joined: two edges by a
// mitre, three or more by a corner patch (a sphere piece on a cube corner). Where a bevel ends at a vertex, the
// face across the end takes the strip's profile (a cube's top face around one bevelled upright) or, when several
// faces meet there, the vertex stays and moves onto the end of the strip. Open (border) and non-manifold edges
// are left alone and counted in `skipped`.
// Vertices (`vertices` = true): each selected vertex is cut off `width` along every edge from it, and the cut is
// filled with one face (segments 1) or a rounded cap (more).
// Profile: 0.25 = a flat chamfer whatever the segments, 0.5 = round (a quarter circle on a square edge), 1 = nearly
// square (the strip hugs the original edge), below 0.25 = concave.
// limitWidth: the width is reduced so no two cuts meet or cross on an edge (a face can never turn inside out); off,
// each cut is still capped at half its edge, so the result stays finite.
// Attributes: faces keep their face group, hidden flag, UVs (new corners get the face's UVs interpolated), mask,
// vertex paint and sculpt layers; strips and patches interpolate between the faces they join and get a new face
// group (`newGroup`) or the lower of the groups they join. Creases and UV flags stay on untouched edges. The
// subdivision stack, micro level, selection sets, Forge Spheres and Tube shape do not survive a topology change and
// are not carried (the caller refuses those Parts).
#pragma once

#include "core/Mesh.h"

#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace sf {

struct PolyBevelOptions {
    float width = 0.1f;     // scene units: how far each face is cut back from the edge
    int segments = 1;       // faces across the strip (1 = a flat chamfer); 1..kPolyBevelMaxSegments
    float profile = 0.5f;   // 0..1, see above
    bool limitWidth = true; // reduce the width so no two cuts cross
    bool vertices = false;  // bevel the selected vertices instead of the selected edges
    bool newGroup = true;   // the bevel faces get a face group of their own
};
inline constexpr int kPolyBevelMaxSegments = 24;

struct PolyBevelStats {
    size_t edges = 0;          // edges bevelled
    size_t vertices = 0;       // vertices changed (both ends of every edge, or the selected vertices)
    size_t skipped = 0;        // selected edges left alone: open, non-manifold, unknown, or at a vertex that can't be rebuilt
    size_t trianglesAdded = 0; // bevel faces, as triangles
    float widthUsed = 0.0f;    // the width after limitWidth
};

struct PolyBevelResult {
    PolyBevelStats stats;
    std::vector<uint32_t> bevelTriangles; // the strips and corner patches: triangle indices in `out` (the new selection)
};

// `edges`: polygon edges as vertex pairs (either order; a quad's diagonal is no edge). `vertices`: the vertices to
// bevel when options.vertices (the edges are ignored then). Returns false with `error` and `out` untouched when
// nothing could be bevelled (empty selection, width <= 0, only open or non-manifold edges). `in` needs topology.
bool polyBevel(const Mesh& in, std::span<const glm::uvec2> edges, std::span<const uint32_t> vertices,
               const PolyBevelOptions& options, Mesh& out, PolyBevelResult& result, std::string& error);

// The Polygon mode op "bevel" (core/PolyOps.h): width as a share of the Part's size, segments, profile, limit,
// new_group; picked Points are cut off, picked Edges or Faces (their edges) are bevelled.
class PolyOpRegistry;
void registerPolyBevelOp(PolyOpRegistry& reg);

} // namespace sf
