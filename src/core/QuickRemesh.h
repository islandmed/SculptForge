// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Tool > Remesh > Remesh after each stroke (#121): whether a stroke stretched the surface enough to remesh it.
// The stroke's undo step holds the moved vertices' old positions; each edge at a moved vertex is compared before
// and after the stroke.
#pragma once

#include "core/Mesh.h"

#include <glm/vec3.hpp>
#include <span>

namespace sf {

struct StrokeStretch {
    size_t edges = 0;     // edges at the moved vertices
    size_t stretched = 0; // of them, longer than kStretchRatio times their length before the stroke
    float worst = 1.0f;   // the largest new / old length ratio
};

inline constexpr float kStretchRatio = 1.6f;   // an edge this much longer than before the stroke is stretched
inline constexpr size_t kStretchedEdges = 6;   // this many stretched edges remesh (one stray edge doesn't)

// moved[i] is a vertex the stroke moved and before[i] its position before the stroke (UndoRecord::positions of a
// base-mesh stroke); the mesh holds the positions after it.
StrokeStretch strokeStretch(const Mesh& mesh, std::span<const uint32_t> moved, std::span<const glm::vec3> before);
inline bool stretchWantsRemesh(const StrokeStretch& s) { return s.stretched >= kStretchedEdges; }

// The Part's own detail: the median edge length of its triangles (every edge on a mesh up to ~100k triangles, else an
// even sample of them). 0 for a mesh without edges. The topology refresh gesture (Sculptor::refreshTopologyDab) remeshes
// stretched triangles back towards it when dynamic topology is off, so the refreshed area matches the rest of the Part.
float typicalEdgeLength(const Mesh& mesh);
// The same over `triangles` only (the surface around a brush: BVH::queryTriangles), so a dense face and a coarse body
// each refresh to their own detail. Empty or too few triangles (under 64): falls back to the whole mesh.
float typicalEdgeLength(const Mesh& mesh, std::span<const uint32_t> triangles);

} // namespace sf
