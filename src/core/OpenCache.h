// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The open cache: derived data a project saves beside a big mesh so opening it skips rebuilding them (the
// vertex-triangle lists and rings, the BVH's structure, a micro level's edge neighbours). Optional: projects
// without it open as before, and builds that don't know it skip it. Never trusted blindly:
// - its key is a hash of the triangles (and the vertex count) it was made from: another mesh, nothing is used;
// - each array carries a hash of its bytes: a damaged one, nothing is used;
// - each table is checked before it is taken (indices in range, a well-formed tree, ...: Mesh::adoptTopology,
//   BVH::adoptStructure, microAdoptEdgeNeighbors), so even a crafted file can't make the app read out of bounds.
// Any failure rebuilds everything, as without a cache.
#pragma once

#include "core/Mesh.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace sf {

constexpr int kOpenCacheVersion = 1;
// Meshes from this many triangles save one (about 45 bytes a triangle, 2-3x the mesh: worth it where rebuilding takes
// seconds). Tests lower it.
extern size_t openCacheMinTriangles;

// What the last loadProject took from a cache (tests, SF_OPEN_PROFILE).
struct OpenCacheUse {
    bool topology = false, bvh = false, microEdges = false;
};
extern OpenCacheUse lastOpenCacheUse;

// A 64-bit hash of `bytes` bytes, the same on every machine and thread count (blocks hashed in parallel, then
// combined in order).
uint64_t openCacheHash(const void* data, size_t bytes);
template <class T>
uint64_t openCacheHash(const std::vector<T>& v) {
    return openCacheHash(v.data(), v.size() * sizeof(T));
}
// The key a cache is made for: the triangles and the vertex count.
uint64_t openCacheKey(const Mesh& mesh);

// Takes saved edge neighbours for a micro level of `mesh` into `out` (MicroMesh::edgeNeighbor; `saved` is left
// empty) if each one is in range and really shares that edge; false and nothing changed otherwise.
bool microAdoptEdgeNeighbors(const Mesh& mesh, std::vector<uint32_t>& out, std::vector<uint32_t>& saved);

} // namespace sf
