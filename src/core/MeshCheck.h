// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Merge by distance and Find Problems (Tool > Geometry, foirgairwolf on Discord). Weld merges the points closer than a
// distance; Find Problems counts what makes a mesh hard to sculpt, remesh, print or export, and each Fix changes
// only what its issue names (nothing is fixed while checking). Everything here works on a copy: the commands
// (weld_points, mesh_check, mesh_fix) replace the active Part's mesh as one undo step.
#pragma once

#include "core/Mesh.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace sf {

class CommandRegistry;

// --- Merge by distance ---
// For each vertex, the vertex it merges into (itself when it stays): vertices are taken in order and each one
// within `distance` of a vertex kept before it merges into the nearest such vertex, so the kept points end up at
// least `distance` apart and keep their positions. distance 0 merges exactly coincident points. `merged`
// (optional) receives how many vertices go.
std::vector<uint32_t> weldTargets(const Mesh& mesh, float distance, size_t* merged = nullptr);
// The mesh with those points merged: triangles that collapse (two corners on one point) are dropped and the
// rest keep their attributes (a merged point takes its target's mask, paint and layers).
Mesh weldByDistance(const Mesh& mesh, float distance, size_t* merged = nullptr);

// --- Find Problems ---
enum class MeshIssue : uint8_t {
    NonManifold, // edges shared by three or more triangles
    Holes,       // open boundaries: connected sets of edges with a triangle on one side only
    Duplicate,   // triangles on the same three points as an earlier one (either winding)
    Degenerate,  // triangles using one point twice
    ZeroArea,    // triangles on three points that are (all but) in one line
    Flipped,     // triangles wound against their neighbours, or closed pieces that are inside out
    Isolated,    // points no triangle uses
    Count
};
inline constexpr int kMeshIssueCount = int(MeshIssue::Count);
// "non_manifold", "holes", "duplicate", "degenerate", "zero_area", "flipped", "isolated" (commands, tests).
const char* meshIssueName(MeshIssue issue);
bool parseMeshIssue(const std::string& name, MeshIssue& out);

struct MeshReport {
    size_t count[kMeshIssueCount] = {}; // per MeshIssue: edges, loops, triangles or points
    size_t total() const;
    // "2 holes, 14 flipped triangles" (or "No problems found").
    std::string summary() const;
};
// Needs the mesh's topology (as every Part has).
MeshReport checkMesh(const Mesh& mesh);

// The mesh with one issue fixed, and how many elements changed in `fixed`:
//  - NonManifold: each triangle past the first two on such an edge gets its own copy of the edge's points (the
//    fin comes loose; nothing is deleted)
//  - Holes: capped as Fill Holes does (a patch per loop, in a face group of its own)
//  - Duplicate / Degenerate: those triangles are deleted
//  - ZeroArea: the shortest edge of each is collapsed (its points merged), which removes it and its neighbour
//  - Flipped: the minority winding of each piece is turned to match the rest; a closed piece wound inside out
//    is turned the right way out
//  - Isolated: the unused points are removed
// Returns false (and leaves `out` alone) when there was nothing to fix.
bool fixMesh(const Mesh& mesh, MeshIssue issue, Mesh& out, size_t& fixed);

// weld_points, mesh_check, mesh_fix.
void registerMeshCheckCommands(CommandRegistry& registry);

} // namespace sf
