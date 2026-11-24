// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Span (Polygon mode): joins two open borders of a Part with a band of polygons, or closes a hole by spanning its two
// opposite halves. The borders are two edge loops / chains on open edges, or two face selections whose faces are
// removed and whose outlines then make the borders (a tunnel between two walls, a neck between two shells, a strip
// across a slot). Options: how many rings of faces span the gap, a twist of the pairing on closed loops, and a manual
// flip of the second border.
//
// The op is pure geometry on a Mesh: it reads the winding of every border edge from the single triangle on it, so the
// band comes out wound with the surface and manifold (every border edge gains exactly its second face, no cross edge
// doubles an existing edge). Borders of unequal length are paired by their fraction along each loop: quads where both
// advance together, triangles spread evenly where one is longer. New faces get a face group of their own, no creases,
// UVs averaged from the corners around each border vertex and interpolated across (valid, not pretty: a span spans
// two UV islands); new vertices (segments > 1) interpolate position, mask, vertex paint and sculpt layers. The rest
// of the Part keeps its vertex and triangle order and values.
#pragma once

#include "core/Mesh.h"

#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace sf {

struct SpanOptions {
    int segments = 1;     // rings of faces across the gap (1..64)
    int twist = 0;        // closed loops: turns the pairing this many vertices along the second border
    bool flip = false;    // pair the second border the other way round (when a piece is wound inside out)
    bool newGroup = true; // the band gets a face group of its own (else group 0)
    bool finish = true;   // rebuild the mesh's topology and normals at the end (off: the caller does, as the ops registry)
};

struct SpanResult {
    std::vector<uint32_t> triangles; // the new triangles (indices into mesh.triangles): the op's new face selection
    std::vector<uint32_t> vertices;  // the new vertices (segments > 1)
    size_t quads = 0, tris = 0;      // polygons added
    uint32_t group = 0;              // their face group
    size_t removed = 0;              // spanFaces: triangles removed first
};

// A border as the op reads it: its vertices in order, `closed` when the last joins the first again.
struct BorderChain {
    std::vector<uint32_t> vertices;
    bool closed = false;
};

// The open edges of `mesh` among `edges` (undirected vertex pairs), as chains in hole order (the edge from vertex i to
// i+1 belongs to a triangle, so a new face across it runs the other way). False with `error` when an edge is not on
// an open border (two faces on it, or none) or the edges branch (a vertex on three of them).
bool borderChains(const Mesh& mesh, std::span<const glm::uvec2> edges, std::vector<BorderChain>& out, std::string& error);

// The outline of the triangles `faces` as chains wound like those triangles (the opposite of hole order): the edges
// with a face of the selection on one side only. False with `error` when the outline branches.
bool faceOutline(const Mesh& mesh, std::span<const uint32_t> faces, std::vector<BorderChain>& out, std::string& error);

// Spans from border `a` to border `b`, given in any order (each is checked against the mesh's open edges and turned into
// hole order first). Both closed, or both open chains; the band is wound with the surface on both sides.
bool spanBorders(Mesh& mesh, const BorderChain& a, const BorderChain& b, const SpanOptions& options,
                   SpanResult& result, std::string& error);

// Closes the single border loop `loop` by splitting it into two halves (at the pair of opposite vertices whose halves
// lie closest to each other, so a slot is spanned across its width) and spanning them: a grid of quads, triangles
// only where an odd count or the split corners need them.
bool spanHole(Mesh& mesh, const BorderChain& loop, const SpanOptions& options, SpanResult& result,
                std::string& error);

// Edge selection form: `edges` on open borders making two chains or loops (spanned) or one closed loop (closed as a
// hole). False with `error` otherwise.
bool spanEdges(Mesh& mesh, std::span<const glm::uvec2> edges, const SpanOptions& options, SpanResult& result,
                 std::string& error);

// Face selection form: `faces` (triangle indices) in two edge-connected patches, each with one outline. The patches
// are removed and their outlines spanned. Triangle indices in `result.triangles` refer to the mesh after the removal;
// the triangles kept keep their order.
bool spanFaces(Mesh& mesh, std::span<const uint32_t> faces, const SpanOptions& options, SpanResult& result,
                 std::string& error);

// Polygon mode's op (core/PolyOps.h): Faces picked = two face selections, Edges or Points picked = two borders or one
// closed loop. Parameters: segments, twist, flip.
struct PolyOpInput;
struct PolyOpOutput;
class PolyOpRegistry;
bool polySpan(const PolyOpInput& in, PolyOpOutput& out, std::string& error);
void registerPolySpanOp(PolyOpRegistry& reg);

} // namespace sf
