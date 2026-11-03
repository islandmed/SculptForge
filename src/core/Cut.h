// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Cut (#101, #91): one tool that cuts the active Part with a shape drawn on the screen. Shapes: Line (the
// half of the screen on one side of it), Lasso, Rectangle and Polyline (open: a bent line; closed: a polygon).
// Modes, each an exact boolean between the Part and the shape extruded along the view:
//   Away - removes everything the shape covers, through the whole Part, and closes the cut with a flat cap;
//   In   - boolean difference with the shape pushed `depth` into the surface (a flat-bottomed pocket);
//   Add  - boolean union with the shape extruded `depth` out of the surface (anchored halfway into the Part).
// The result is watertight when the Part is (open Parts are trimmed without a cap; Add needs a closed one).
// The boolean runs in normalised device coordinates, where the extrusion is a straight prism: intersection
// points are shared by the Part's triangles and the prism's faces, so no cracks; the Part's surface is kept
// exactly where it isn't cut, with its attributes interpolated onto the new vertices. The cap gets a face group
// of its own.
#pragma once

#include "core/Mesh.h"

#include <glm/glm.hpp>

#include <string>
#include <vector>

namespace sf {

class CommandRegistry;

enum class CutShape : uint8_t { Line, Lasso, Rect, Polyline };
enum class CutMode : uint8_t { Away, In, Add };
const char* cutShapeName(CutShape s); // "line", "lasso", "rect", "polyline"
const char* cutModeName(CutMode m);   // "away", "in", "add"
bool parseCutShape(const std::string& name, CutShape& out);
bool parseCutMode(const std::string& name, CutMode& out);

// The screen region (NDC, counter-clockwise, simple) a shape cuts. Line: two points, the side to the left of
// a -> b (`flip`: the right). Polyline: open (2+ points) cuts the left side like a bent line; `closed` (3+)
// cuts the inside. Lasso and Rectangle: the inside (a lasso that crosses itself keeps its biggest loop).
// False, saying why, for too few or degenerate points.
bool cutRegion(CutShape shape, const std::vector<glm::vec2>& points, bool flip, bool closed,
               std::vector<glm::dvec2>& region, std::string& error);

// Cuts `mesh` with `region` (from cutRegion) extruded along the view of `viewProj`. `depth` (In, Add) is a
// fraction of the mesh's largest side. False with `error` when nothing could be cut (the shape misses the mesh,
// the result would be empty, part of the mesh is behind the camera); `out` is the result otherwise.
bool cutMesh(const Mesh& mesh, const glm::mat4& viewProj, const std::vector<glm::dvec2>& region, CutMode mode,
             float depth, Mesh& out, std::string& error);

// Every edge of `mesh` is shared by exactly two triangles, wound opposite ways (a closed, manifold surface).
bool isWatertight(const Mesh& mesh);

// Registers "cut".
void registerCutCommands(CommandRegistry& registry);

} // namespace sf
