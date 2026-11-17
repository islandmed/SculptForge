// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Forge Ink on a Micro level: every stroke stays editable.
//
// A stroke keeps the dabs it was made of and its undo tiles (the grids as they were before it). Editing or removing
// stroke k undoes, from the top down, the strokes above it whose tiles meet the changed region (the region grows by
// each one that does), puts k's old tiles back, then runs k's new dabs and those strokes again, in order, with the
// same kernel (MicroStroke). The result is exactly what running every stroke from the start would give, for every
// brush the dab kernel drives (surface-reading ones such as Smooth and Clay included). An edit costs about one
// stroke per overlapping stroke above it, so a stroke is baked (dabs and tiles dropped: no longer editable) once
// `bakeDepth` overlapping strokes sit on it; baking a stroke bakes the older ones it meets too, so an editable
// stroke never has a baked one above it.
#pragma once

#include "core/Mesh.h"

#include <cstdint>
#include <memory>
#include <vector>

#include <glm/glm.hpp>

namespace sf {

class BVH;
struct MicroUndo;
enum class BrushType;

struct MicroDab {
    BrushType type{};
    glm::vec3 center{0.0f}, viewDir{0.0f, 0.0f, -1.0f}, alphaUp{0.0f, 1.0f, 0.0f}, strokeDir{0.0f};
    float radius = 0.1f, strength = 0.5f, focal = 0.0f;
    bool invert = false;
};

struct MicroRecord {
    uint32_t id = 0;
    std::vector<MicroDab> dabs;
    std::unique_ptr<MicroUndo> undo; // the grids before it (null once baked, or when it changed nothing)
    std::vector<uint32_t> tiles;     // base triangles it changed, ascending
    uint32_t over = 0;               // later strokes whose tiles meet its tiles
    bool baked = false;
};

struct MicroReplayStats {
    size_t replayed = 0; // strokes run again (the edited one included)
    size_t tiles = 0;    // the changed region
};

class MicroHistory {
public:
    MicroHistory();
    ~MicroHistory();
    int bakeDepth = 20;

    // Brushes a stroke can be replayed for: the dab kernel's, not Move (a grab) or the mask brush.
    static bool replayable(BrushType type);
    // Runs the dabs as a new stroke on top and records it (tests, scripts; the app records its live stroke with record()).
    uint32_t add(Mesh& m, const BVH& bvh, std::vector<MicroDab> dabs);
    // Records a stroke already run on top: its dabs and the undo step MicroStroke::end gave.
    uint32_t record(std::vector<MicroDab> dabs, std::unique_ptr<MicroUndo> undo);
    // Replaces stroke `id`'s dabs. False when it is unknown or baked.
    bool edit(Mesh& m, const BVH& bvh, uint32_t id, std::vector<MicroDab> dabs, MicroReplayStats* stats = nullptr);
    bool remove(Mesh& m, const BVH& bvh, uint32_t id, MicroReplayStats* stats = nullptr);
    const MicroRecord* find(uint32_t id) const;
    size_t size() const { return recs_.size(); }
    size_t editable() const;
    size_t bytes() const;

private:
    // Undoes the strokes above recs_[k] that reach `region` (growing it), and k itself; returns their indices, bottom up.
    std::vector<size_t> unwind(Mesh& m, size_t k, std::vector<uint32_t>& region);
    void run(Mesh& m, const BVH& bvh, MicroRecord& rec);
    void landed(size_t k); // a stroke landed on top: count it on the strokes it meets, bake past the depth
    void bake(size_t k);
    std::vector<MicroRecord> recs_; // bottom (oldest) first
    uint32_t next_ = 1;
};

} // namespace sf
