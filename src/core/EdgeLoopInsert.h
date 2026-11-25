// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Edge loop insertion (Polygon mode): new loops of edges across a ring of quads. The *ring* through an edge is the
// chain of quads entered through that edge and left through the opposite one, on until it comes back round, meets
// an open border, a triangle or a quad it already crossed (a Moebius strip's quads cannot all be wound the same way,
// so its fold is an open edge and its ring ends there). Insert splits every edge of the ring at the same
// parameters and connects the new vertices across each quad, so N loops turn each quad into N + 1 quads; a
// polygon the ring ends on (a triangle, or a quad the loop cannot pass) is fanned from its far corner so the mesh
// stays closed and manifold with no T-junction. Nothing else moves: untouched triangles keep their slots and their
// attributes, split polygons keep their face group and hidden flag, UVs, seams and creases are carried along the
// split edges (new interior edges get none), vertex paint, mask, sculpt layers and paint layers are interpolated.
//
// Pure functions on a Mesh and the EdgeTopology of its base polygons (core/EdgeSelect.h): nothing here touches
// the Document, undo or the GPU. The caller works on the lowest subdivision level (the one with edges to select);
// rebuildTopology() drops a subdivision stack or micro level that no longer fits the triangles.
#pragma once

#include "core/EdgeSelect.h"
#include "core/Mesh.h"

#include <cstdint>
#include <string>
#include <vector>

namespace sf {

class CommandRegistry;

// The ring of edges a loop cut through `seed` crosses (edgeRing), in order along the ring.
struct EdgeRing {
    std::vector<uint32_t> edges;   // the edges to split, in ring order (the seed among them)
    std::vector<uint32_t> left;    // per edge: its vertex on the ring's first rail (the seed's lower vertex side) ...
    std::vector<uint32_t> right;   // ... and on the other; consecutive edges share a quad between the two rails
    std::vector<uint32_t> quads;   // per consecutive pair (and, closed, last -> first): the quad between them (its lower triangle)
    // A polygon the ring ends on (kNoEdge = none: the ring ends on an open border there, or is closed). Its edge on the
    // ring is edges.front() (`startEnd`) or edges.back() (`finishEnd`); Insert fans it from its far corner.
    uint32_t startEnd = kNoEdge, finishEnd = kNoEdge;
    bool closed = false; // came back round to the seed: quads.size() == edges.size()
    bool empty() const { return edges.empty(); }
};

// The ring through edge `seed`; empty when the edge is out of range or cannot be split (a non-manifold edge).
EdgeRing edgeRing(const Mesh& mesh, const EdgeTopology& topo, uint32_t seed);

struct EdgeLoopOptions {
    int count = 1;      // loops to insert, 1..kMaxLoops, evenly spaced across each quad of the ring
    float slide = 0.0f; // -1..1: moves the loops towards the ring's first rail (< 0) or the other (> 0); 0 = centred
    static constexpr int kMaxLoops = 64;
};

struct EdgeLoopResult {
    // Per inserted loop: its new vertices, one per ring edge, in ring order (the edges of the loop run between
    // consecutive ones, and from the last back to the first when the ring is closed).
    std::vector<std::vector<uint32_t>> loops;
    std::vector<uint32_t> triangles; // the triangles written: the strips of the split quads and the fans (for selecting them)
    size_t quadsSplit = 0;           // quads of the ring turned into count + 1 quads
    size_t polygonsFanned = 0;       // polygons the ring ended on, fanned (0..2)
    bool closed = false;
};

// Where the loops would go (the preview): per loop, its points along the ring in order. Empty when there is no
// ring through `seed`. Does not change the mesh. The overload on a ring found before (edgeRing) is what a slider
// or a drag calls every frame: it costs only the ring's length.
std::vector<std::vector<glm::vec3>> edgeLoopPreview(const Mesh& mesh, const EdgeTopology& topo, uint32_t seed,
                                                    const EdgeLoopOptions& options);
std::vector<std::vector<glm::vec3>> edgeLoopPreview(const Mesh& mesh, const EdgeRing& ring, const EdgeLoopOptions& options);

// Inserts the loops into `mesh` (which gets its topology and normals rebuilt; `topo` is the one built from it
// before and is stale afterwards). Returns false with `error` when nothing can be inserted (no ring through the
// seed, bad count). The overload takes a ring found on this `topo` before (the one previewed).
bool insertEdgeLoops(Mesh& mesh, const EdgeTopology& topo, uint32_t seed, const EdgeLoopOptions& options,
                     EdgeLoopResult& result, std::string& error);
bool insertEdgeLoops(Mesh& mesh, const EdgeTopology& topo, const EdgeRing& ring, const EdgeLoopOptions& options,
                     EdgeLoopResult& result, std::string& error);

// Registers "edge_loop": inserts loops through the given or the selected edge of the active Part's base level, one
// undo step, and selects the new loops (core/EdgeSelect.h, Document::edgeSelection).
void registerEdgeLoopCommands(CommandRegistry& registry);

} // namespace sf
