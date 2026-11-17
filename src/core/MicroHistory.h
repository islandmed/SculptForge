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
//
// The default sculpt path (Sculptor::keepEditable): the live stroke runs as always and records each dab as it is
// placed (MicroDab, in order: `seq`), with what the whole stroke shares in its header (MicroInk: alpha, a recipe's
// pattern, the mask brush, adaptive sampling). Nothing solved is kept: a dab record is complete without the surface,
// so a solve can be deferred or batched and give the same bytes. The undo tiles are the app's own undo step for the
// stroke (UndoRecord::micro, shared): one memory budget. When the app's history drops a step (its byte budget, a redo
// branch), the stroke is baked or forgotten (sync). Edits and removals are undo steps of their own (MicroInkStep).
//
// What the history can't replay bakes what it reaches (Sculptor::endStroke): strokes that move the base (big dabs'
// base kernel, Move on the cage, Kelvinlet, transforms), Stencil strokes. Writes from outside the history are caught
// before any edit: every stroke keeps a hash of each tile it changed as it found it and as it left it (with the base
// triangle's corners), and an edit goes ahead only when those chain up to what the tiles hold now (else it bakes).
#pragma once

#include "core/Mesh.h"
#include "core/Symmetry.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <glm/glm.hpp>

namespace sf {

class BVH;
struct BrushAlpha;
struct MicroUndo;
enum class BrushType;

// What every dab of a stroke shares (the stroke header). A snapshot: changing the brush later can't alter a replay.
struct MicroInk {
    bool maskBrush = false; // the mask brush (raises the micro mask; dabs carry their maskStrength)
    bool adaptive = true;   // MicroStroke::adaptive
    std::shared_ptr<const BrushAlpha> alpha; // null = round
    // A recipe's surface pattern (BrushRecipe::Pattern, 0 = none), rebuilt by microPatternTexture (core/Sculpt.h),
    // the same function the live stroke uses. No jitter or random state is read by the kernel.
    int pattern = 0;
    float patternScale = 0.3f, patternAmount = 0.0f;
    bool operator==(const MicroInk&) const = default;
};

// One dab of one mirror, as placed (pressure applied). Move records its grab and each drag step instead.
struct MicroDab {
    enum class Op : uint8_t { Dab, GrabBegin, Grab, Restore };
    uint32_t seq = 0; // order within the stroke (the solver runs strictly in seq)
    Op op = Op::Dab;
    BrushType type{};
    glm::vec3 center{0.0f}, viewDir{0.0f, 0.0f, -1.0f}, alphaUp{0.0f, 1.0f, 0.0f}, strokeDir{0.0f};
    float radius = 0.1f, strength = 0.5f, focal = 0.0f;
    float maskStrength = 0.0f; // the mask brush's
    bool invert = false;
    SymCopy copy;              // the mirror (a recipe's pattern reads at the source dab's point; Move's signs)
    glm::vec3 delta{0.0f};     // Grab: the drag since the grab began
    float pxPerUnit = 0.0f;    // screen pixels per world unit at the centre when placed (DabParams::pxPerUnit; 0 = unknown)
};

struct MicroRecord {
    uint32_t id = 0;
    MicroInk ink;
    std::vector<MicroDab> dabs;
    std::shared_ptr<MicroUndo> undo; // the grids before it (null once baked, or when it changed nothing)
    std::vector<uint32_t> tiles;     // base triangles it changed, ascending
    std::vector<uint64_t> pre, post; // per tile: its hash as the stroke found it and left it (microTileHash)
    uint32_t over = 0;               // later strokes whose tiles meet its tiles
    bool baked = false;
    bool shared = false;             // `undo` is also the app's undo step (record())
    bool applied() const;            // in the mesh (its undo step isn't undone)
};

struct MicroReplayStats {
    size_t replayed = 0; // strokes run again (the edited one included)
    size_t tiles = 0;    // the changed region
};

// A tile's contents for MicroRecord::pre/post: its grid, its mask (none = zeros) and its base triangle's corners.
uint64_t microTileHash(const Mesh& m, uint32_t tri, const int16_t* disp, const uint8_t* mask);

class MicroHistory {
public:
    MicroHistory();
    ~MicroHistory();
    int bakeDepth = 20;

    // Brushes a stroke can be replayed for: the dab kernel's, not Move (a grab) or the mask brush.
    static bool replayable(BrushType type);
    // Runs the dabs as a new stroke on top and records it (tests, scripts; the app records its live stroke with record()).
    uint32_t add(Mesh& m, const BVH& bvh, std::vector<MicroDab> dabs, MicroInk ink = {});
    // Records a stroke already run on top: its header, dabs, the undo step MicroStroke::end gave (shared with the
    // app's undo step) and its tile hashes (MicroStroke::tilePre / tilePost, in the undo step's tile order).
    uint32_t record(MicroInk ink, std::vector<MicroDab> dabs, std::shared_ptr<MicroUndo> undo, std::vector<uint64_t> pre,
                    std::vector<uint64_t> post);
    // Replaces stroke `id`'s dabs (on success `dabs` holds the old ones: an undo step swaps them back). False when it
    // is unknown, baked, or something outside the history wrote where it would replay (`why` says; it is baked then).
    bool edit(Mesh& m, const BVH& bvh, uint32_t id, std::vector<MicroDab>& dabs, MicroReplayStats* stats = nullptr,
              std::string* why = nullptr);
    // Takes stroke `id` out; `removed` gets its record and where it was, for reinsert() (undo).
    bool remove(Mesh& m, const BVH& bvh, uint32_t id, MicroReplayStats* stats = nullptr, std::string* why = nullptr,
                std::unique_ptr<MicroRecord>* removed = nullptr, size_t* at = nullptr);
    // Puts a removed stroke back at `at` (its old place) and runs it and the strokes above it that it reaches again.
    bool reinsert(Mesh& m, const BVH& bvh, std::unique_ptr<MicroRecord> rec, size_t at, std::string* why = nullptr);
    // A stroke the history doesn't hold changed `tiles` (ascending; empty = everything): bakes what it meets. Both
    // return how many editable strokes they baked.
    size_t bakeMeeting(const std::vector<uint32_t>& tiles);
    size_t bakeAll();
    // Follows the app's undo history: a stroke whose step it dropped is baked (applied) or forgotten (a dropped redo).
    void sync();
    const MicroRecord* find(uint32_t id) const;
    const std::vector<MicroRecord>& records() const { return recs_; }
    size_t size() const { return recs_.size(); }
    size_t editable() const;
    // What the history holds beyond the app's undo steps (dabs, hashes; unshared undo tiles too).
    size_t bytes() const;

private:
    // Undoes the strokes above recs_[k] that reach `region` (growing it), and k itself; returns their indices, bottom up.
    std::vector<size_t> unwind(Mesh& m, size_t k, std::vector<uint32_t>& region);
    // Before an unwind: what the chain's tiles hold chains up from the strokes' hashes to the mesh (no outside writes).
    bool consistent(const Mesh& m, size_t k, std::vector<uint32_t> region) const;
    void run(Mesh& m, const BVH& bvh, MicroRecord& rec);
    void landed(size_t k); // a stroke landed on top: count it on the strokes it meets, bake past the depth
    void bake(size_t k);
    void release();        // drops what baked strokes held
    size_t indexOf(uint32_t id) const;
    std::vector<uint32_t> reach(const Mesh& m, const BVH& bvh, const std::vector<MicroDab>& dabs, const MicroInk& ink) const;
    std::vector<MicroRecord> recs_; // bottom (oldest) first
    uint32_t next_ = 1;
};

// An edit or removal of a recorded stroke, as an undo step (UndoRecord::ink): applying it swaps the stroke's dabs
// (Edit) or takes it out / puts it back (Remove).
struct MicroInkStep {
    enum class Op : uint8_t { Edit, Remove };
    Op op = Op::Edit;
    uint32_t id = 0;
    std::vector<MicroDab> dabs;          // Edit: the other dab list
    std::unique_ptr<MicroRecord> removed; // Remove: the record while it is out (undo puts it back)
    size_t at = 0;
    bool out = true;                     // Remove: the stroke is out now
    // Applies the step to `mesh` (its micro level's history); false (and `why`) when the history can't.
    bool swapWith(Mesh& mesh, const BVH& bvh, std::string* why = nullptr);
    size_t bytes() const;
};

// The history of `mesh`'s micro level (made on first use), or null without one.
MicroHistory* microHistory(Mesh& mesh, bool create);

} // namespace sf
