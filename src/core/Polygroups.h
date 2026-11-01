// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Polygroups and visibility, ZBrush style. Grouping operations work on the
// visible triangles only (hidden ones keep their groups), so "isolate, then
// group" refines one part at a time. All functions here change the mesh
// directly; the commands registered by registerPolygroupCommands() wrap them
// with undo records.
#pragma once

#include "core/Mesh.h"
#include "core/Symmetry.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

namespace sf {

class CommandRegistry;

// Id above every group in use (1 on a mesh without polygroups, 0 on an empty mesh).
uint32_t nextPolygroupId(const Mesh& mesh);
// Number of distinct group ids among the visible triangles.
size_t visiblePolygroupCount(const Mesh& mesh);

// The functions below return proposed full-size polygroup / hidden arrays and
// leave the mesh alone; the commands write them with sparse undo records.

// Regions of visible triangles whose normals stay within `maxAngleDegrees` of
// the region's first triangle and are joined across edges (cube -> 6 groups).
std::vector<uint32_t> groupsByNormals(const Mesh& mesh, float maxAngleDegrees);
// One group per edge-connected piece of visible triangles.
std::vector<uint32_t> groupsByConnectivity(const Mesh& mesh);
// One group per UV island: visible triangles joined across edges whose UVs match at both ends (ZBrush UV Groups).
// Empty when the mesh has no UVs.
std::vector<uint32_t> groupsByUvs(const Mesh& mesh);
// Visible triangles whose average vertex mask is >= 0.5 move to a new group;
// `changed` (optional) receives how many did.
std::vector<uint32_t> groupsFromMask(const Mesh& mesh, size_t* changed = nullptr);
// Every visible triangle joins the lowest group id among them.
std::vector<uint32_t> groupsMergeVisible(const Mesh& mesh);

// Visibility proposals. They never hide triangles that are already hidden
// unless stated (invert), and return an empty vector when nothing would be
// visible afterwards (callers refuse that, as ZBrush does).
std::vector<uint8_t> hiddenHideGroup(const Mesh& mesh, uint32_t group);
std::vector<uint8_t> hiddenIsolateGroup(const Mesh& mesh, uint32_t group);
std::vector<uint8_t> hiddenInverted(const Mesh& mesh);
// Rectangle selection in normalised device coordinates (-1..1, y up) under
// `viewProj`: triangles whose centre projects inside stay visible and the rest
// are hidden, or with `hideInside` the inside is hidden instead. Triangles
// behind the camera count as outside. `mirrors` (Sculptor::copies of the symmetry): a triangle whose mirror
// image's or radial copy's centre projects inside counts as inside too (#51, #63); empty = none.
std::vector<uint8_t> hiddenByRect(const Mesh& mesh, const glm::mat4& viewProj, glm::vec2 rectMin, glm::vec2 rectMax,
                                  bool hideInside, const std::vector<SymCopy>& mirrors = {});
// The same with a lasso: a closed polygon of NDC points (even-odd inside test).
std::vector<uint8_t> hiddenByLasso(const Mesh& mesh, const glm::mat4& viewProj, const std::vector<glm::vec2>& lasso,
                                   bool hideInside, const std::vector<SymCopy>& mirrors = {});

// True when every triangle of `group` is visible and no other is.
bool polygroupIsolated(const Mesh& mesh, uint32_t group);

// Copy of `mesh` without its hidden triangles: unused vertices are removed and
// every attribute (mask, colours, polygroups, UVs, sculpt layers) compacted.
Mesh deleteHiddenTriangles(const Mesh& mesh);
// Copy of the triangles with keep[t] != 0, compacted the same way (hidden flags are kept).
Mesh extractTriangles(const Mesh& mesh, const std::vector<uint8_t>& keep);

// Adds the polygroup and visibility commands (polygroup_*, hide_polygroup,
// isolate_polygroup, show_all, invert_visibility, hide_rect, hide_lasso, delete_hidden).
void registerPolygroupCommands(CommandRegistry& registry);

} // namespace sf
