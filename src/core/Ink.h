// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Forge Ink: relief detail painted as strokes that stay editable.
//
// A stroke is a path (points on the surface, each with a width and a height), a cross-section profile and an
// operator. The relief at a surface point x is the strokes' contributions composed in order:
//
//     H(x) = op_n( ... op_1( 0, c_1(x) ) ..., c_n(x) ),   c_i(x) = h(t*) * profile(d / w(t*))
//
// with d the distance from x to the stroke's path, t* the closest point on it (a stroke is the max over its
// segments, so its own segments never pile up at the joints). The surface is split into tiles (a Micro level's
// base triangles, or a sheet's cells: InkSurface). Each tile keeps the strokes that can reach it, in order,
// grouped in RUNS of operators that commute (Add with Add, Max with Max, Min with Min), and the relief at the
// start of each run (a checkpoint). An edit then costs what it changes:
//   - in an Add run: checkpoint after the run += new - old contribution (no other stroke is evaluated);
//   - in a Max or Min run: that run is folded again from its checkpoint;
//   - then the runs above it are folded from their checkpoints;
//   - tiles the stroke can't reach (before or after the edit) are not touched.
// Per tile, the relief's bounds are composed from each stroke's closed-form bounds through the operators, so a
// pick or a cull can skip tiles before evaluating them.
#pragma once

#include "core/Mesh.h"

#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include <glm/glm.hpp>

namespace sf {

enum class InkOp : uint8_t { Add, Max, Min }; // Min carves: min(H, -c)
enum class InkProfile : uint8_t { Round, Smooth, Chisel, Flat };

struct InkPoint {
    glm::vec3 p{0.0f};
    float width = 0.01f;  // half-width (world units)
    float height = 0.002f; // world units along the surface normal; negative carves (with Add)
};

struct InkStroke {
    uint32_t id = 0;
    std::vector<InkPoint> path;
    InkProfile profile = InkProfile::Round;
    InkOp op = InkOp::Add;
    float opacity = 1.0f; // Add: scales the contribution
};

// The profile at s = d / w in [0, 1) (0 at the path, 0 at the edge: s >= 1).
float inkProfile(InkProfile p, float s);
// The stroke's contribution at x (0 outside its footprint).
float inkContribution(const InkStroke& s, const glm::vec3& x);
// The stroke's footprint bounds (its path's box grown by the widest width).
AABB inkStrokeBox(const InkStroke& s);
// Closed-form bounds of the stroke's contribution anywhere: [lo, hi].
void inkStrokeBounds(const InkStroke& s, float& lo, float& hi);

// Where a layer's relief lives: tiles of sample points, each with a position and a box.
class InkSurface {
public:
    virtual ~InkSurface() = default;
    virtual size_t tileCount() const = 0;
    // Tiles whose samples may lie in `box` (conservative).
    virtual void tilesIn(const AABB& box, std::vector<uint32_t>& out) const = 0;
    // The tile's sample positions (the surface the relief stands on).
    virtual void samples(uint32_t tile, std::vector<glm::vec3>& out) const = 0;
    // The relief of the tile changed: its new values, one per sample.
    virtual void write(uint32_t tile, std::span<const float> relief) = 0;
};

struct InkStats {
    size_t tiles = 0;         // tiles updated
    size_t evaluated = 0;     // stroke x sample contributions evaluated
    size_t samples = 0;       // samples written
};

// A layer of strokes over a surface. Strokes are kept in order (the order of creation unless moved).
class InkLayer {
public:
    explicit InkLayer(InkSurface& surface);
    ~InkLayer();

    // Appends a stroke on top; returns its id (assigned when s.id is 0).
    uint32_t add(InkStroke s, InkStats* stats = nullptr);
    // Replaces a stroke's path, profile, operator or opacity (its place in the order stays).
    bool edit(const InkStroke& s, InkStats* stats = nullptr);
    bool remove(uint32_t id, InkStats* stats = nullptr);
    const InkStroke* find(uint32_t id) const;
    size_t size() const;

    // Every tile again from nothing (the reference the incremental updates must match).
    void rebuild(InkStats* stats = nullptr);
    // The relief as the layer holds it (nullptr when no stroke reaches the tile).
    const std::vector<float>* relief(uint32_t tile) const;
    // Closed-form bounds of the tile's relief (0, 0 when no stroke reaches it).
    void bounds(uint32_t tile, float& lo, float& hi) const;
    // Bumped by every update of the tile (a bake records it: core/Ink.h inkRebake).
    uint64_t generation(uint32_t tile) const;
    size_t bytes() const;

private:
    struct Impl;
    std::unique_ptr<Impl> d_;
};

// Forge Ink on a Micro level: tiles = base triangles, samples = their grid points, the relief added to the
// displacement the sculpt had when the tile was first inked (kept, so strokes stay editable on top of it).
std::unique_ptr<InkSurface> inkMicroSurface(Mesh& mesh);

// A flat sheet: a grid of cols x rows points spaced `spacing` from `origin` along x and y, relief along z,
// tiles of tile x tile cells. Its mesh (one vertex per point) is built by inkSheetMesh and updated in place.
struct InkSheet {
    glm::vec3 origin{0.0f};
    uint32_t cols = 2, rows = 2, tile = 16;
    float spacing = 0.01f;
};
std::unique_ptr<InkSurface> inkSheetSurface(const InkSheet& sheet, std::vector<float>& heights);
Mesh inkSheetMesh(const InkSheet& sheet, std::span<const float> heights);
// Re-bakes the vertices of the tiles whose generation moved since `baked` (per tile; resized as needed) into a
// sheet mesh made by inkSheetMesh: positions and normals there only. Returns the tiles re-baked.
size_t inkRebake(const InkSheet& sheet, const InkLayer& layer, std::span<const float> heights, Mesh& mesh,
                 std::vector<uint64_t>& baked);
// The first hit of a ray on the sheet's relief: tiles whose box grown by their closed-form bounds the ray misses
// are skipped without evaluation. Returns false on a miss; `tilesTested` counts the tiles it looked inside.
bool inkSheetPick(const InkSheet& sheet, const InkLayer& layer, std::span<const float> heights, const glm::vec3& origin,
                  const glm::vec3& dir, glm::vec3& hit, size_t* tilesTested = nullptr);

} // namespace sf
