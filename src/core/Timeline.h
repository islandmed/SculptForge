// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Sculpt Timeline: the edit history as a tree you can travel in (docs/guide/timeline.md).
//
// Every undo step is a node; the root is the state before the first one. The undo stack (core/Undo.h) holds the
// steps of one path through the tree, the active branch: its undo steps lead from the root to the cursor (the
// state the mesh shows), its redo steps on to the branch's tip. Undoing and then editing doesn't throw the old
// continuation away as a plain undo stack does: those steps stay in the tree as an inactive branch, and selecting
// any of its nodes makes it the active branch again.
//
// Moving the cursor (seek) reuses the stack's own sparse per-step deltas. A sculpt stroke on the base mesh (a
// "fast" step: positions only, no sculpt layers) also keeps the values its record doesn't hold (`other`: after
// an undo the record holds the new positions and `other` the old ones, and the other way round), so the
// timeline can move it between undo and redo by swapping two vectors and writing the positions it needs, and
// updates normals, the BVH and the GPU once for a whole run of steps instead of after each one. Every
// `checkpointEvery` fast steps it keeps a full copy of the positions (a checkpoint, within `checkpointBudget`):
// a long jump writes the nearest checkpoint at or before the target and replays at most a few steps forward,
// instead of walking every step in between, so seeking costs about the same wherever the target is. Other steps
// (dynamic topology, remeshes, masks, paint, SubTool changes) are walked with the undo stack's own undo / redo.
//
// Re-tune: a past stroke's strength can be changed (its delta scaled) without redoing the strokes after it. The
// change is added to the current shape and to every later step's stored positions where they overlap the
// stroke. That is exact where later strokes don't overlap it; where they do, they keep the displacement they
// made then on top of the new shape: a later stroke isn't simulated again (a Smooth over the area won't re-smooth
// it, a later Inflate keeps its old direction). Steps after the stroke that store whole meshes or topology make
// re-tuning it impossible (it reports why).
//
// The history can be saved beside a project as <name>.sfhist (saveHistory / loadHistory): sculpt strokes, masks,
// polypaint, visibility and polygroup steps; the tree, labels, times and strengths. Steps that can't be saved
// (topology changes, textures, layers) cut the saved history at the newest such step before the cursor.
#pragma once

#include "core/Undo.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace sf {

struct Document;

class Timeline : public UndoListener {
public:
    static constexpr uint32_t kNone = std::numeric_limits<uint32_t>::max();
    enum class Kind : uint8_t { Start, Stroke, Mask, Paint, Visibility, Topology, Other };

    struct Node {
        uint32_t parent = kNone;
        std::vector<uint32_t> children;
        uint32_t activeChild = kNone; // the child the branch continues with (the newest, or the last one visited)
        std::string label;
        double time = 0.0; // seconds since the Unix epoch when the step was made
        Kind kind = Kind::Other;
        uint32_t touched = 0;  // vertices (or triangles) the step changed
        glm::vec3 center{0.0f}; // bounds of the changed vertices after the step (fast steps; for overlap and the timelapse)
        float radius = 0.0f;
        float strength = 1.0f; // re-tune: how strongly the stroke shows (1 = as sculpted)
        bool alive = true;
        bool fast = false;     // `other` is valid (see the file comment)
        bool captured = false; // `other` / bounds were taken from the mesh after the step
        std::vector<glm::vec3> other;
        std::vector<glm::vec3> retuneDelta; // the stroke's delta at strength 1, kept from the first re-tune on
        std::unique_ptr<UndoRecord> stored; // a node on an inactive branch: its step, as a redo step
    };

    Timeline() = default;
    // Starts watching `doc.undo` (replacing any other listener): the tree is the stack's undo and redo steps.
    static std::shared_ptr<Timeline> attach(Document& doc);
    // The timeline watching `stack`, if any.
    static Timeline* of(UndoStack& stack);
    void bindStack(UndoStack& stack) { stack_ = &stack; } // after the stack moved (SubTools); Document calls do it

    // --- the tree
    const Node& node(uint32_t id) const { return nodes_[id]; }
    size_t nodeCount() const { return nodes_.size(); } // ids; some may be dead (!alive)
    uint32_t root() const { return path_.front(); }
    const std::vector<uint32_t>& path() const { return path_; } // the active branch, root first
    size_t cursor() const { return cursor_; }                   // index into path(): the state shown
    uint32_t cursorNode() const { return path_[cursor_]; }
    bool onPath(uint32_t id) const;
    int depth(uint32_t id) const; // steps from the root
    // Nodes not on the active branch, each with the path index of the node it branches from.
    struct Branch {
        uint32_t first;  // its first node (a child of path()[from])
        size_t from;
        size_t length;   // nodes along its own active children
        uint32_t tip;
    };
    std::vector<Branch> branches() const;
    size_t aliveCount() const;

    // --- travel (`doc.undo` must be the watched stack). Each returns false with `error` set when it can't.
    // Shows the state after path()[index].
    bool seek(Document& doc, size_t index, std::string* error = nullptr);
    // Makes the branch through `id` active and shows the state after it.
    bool select(Document& doc, uint32_t id, std::string* error = nullptr);
    // Sets how strongly stroke `id` (on the active branch) shows; see the file comment for the approximation.
    bool retune(Document& doc, uint32_t id, float strength, std::string* error = nullptr);
    bool canRetune(uint32_t id, std::string* why = nullptr) const;
    // Brings what the mesh shows into the tree (fast-step capture, checkpoints): call once a frame, and before
    // reading node data after edits.
    void sync(Document& doc);

    // --- settings and statistics
    // save_project also writes <name>.sfhist (the app's Preferences; off for the CLI and scripts).
    inline static bool saveWithProject = false;
    // The document open_project / save_project read and write the sidecar for (the app's scene; null = none),
    // so opening a project for a thumbnail or a scratch scene never reads a history.
    inline static const Document* historyDocument = nullptr;
    size_t checkpointEvery = 16;           // a checkpoint every this many path steps (doubles when over budget)
    size_t checkpointBudget = size_t(1) << 30; // bytes of checkpoints
    size_t branchBudget = size_t(3) << 29;     // 1.5 GiB of inactive branches and `other` copies
    size_t checkpointBytes() const;
    size_t extraBytes() const; // `other` copies and inactive branches
    size_t checkpointCount() const { return checkpoints_.size(); }
    bool hasCheckpoint(uint32_t id) const { return checkpoints_.count(id) != 0; }
    struct SeekStats {
        size_t walked = 0, batched = 0, replayed = 0; // steps undone / redone by the stack, swapped in batches, replayed from a checkpoint
        bool usedCheckpoint = false;
        double ms = 0.0;
    };
    const SeekStats& lastSeek() const { return lastSeek_; }

    // --- saving (see the file comment). Fails when nothing can be saved.
    bool save(std::vector<uint8_t>& out, const Mesh& mesh, std::string* error = nullptr) const;
    // Rebuilds the tree from save()'s bytes into `doc.undo` (whose mesh must be the one saved); false (and the
    // history left alone) when the bytes don't match the mesh.
    static bool load(Document& doc, const std::vector<uint8_t>& in, std::string* error = nullptr);

    // UndoListener
    void undoPushed(UndoRecord& pushed, std::deque<UndoRecord>& discardedRedo) override;
    void undoApplying(UndoRecord& r, Mesh& mesh, bool isUndo) override;
    void undoApplied(UndoRecord& r, Mesh& mesh, bool isUndo) override;
    void undoDroppedOldest() override;
    void undoCleared() override;

    // The step of node `id` (on the path: in the stack; else its stored record), or null for the root.
    UndoRecord* recordOf(uint32_t id);
    const UndoRecord* recordOf(uint32_t id) const { return const_cast<Timeline*>(this)->recordOf(id); }
    static Kind classify(const UndoRecord& r);
    static bool fastShape(const UndoRecord& r); // a step that can be fast (positions of the base only)

private:
    uint32_t newNode(uint32_t parent, const UndoRecord* r);
    void reset();
    void capture(Mesh& mesh); // the cursor node's `other` and bounds from the mesh (which shows it)
    void captureFrom(Node& n, const UndoRecord& r, Mesh& mesh); // the same, `r` being n's step in undo form
    void freeSubtree(uint32_t id);
    void maybeCheckpoint(const Mesh& mesh);
    void enforceBudgets();
    void walkTo(Document& doc, size_t index);
    bool jumpWithCheckpoint(Document& doc, size_t index);
    void flushBatch(Document& doc);

    std::vector<Node> nodes_;
    std::vector<uint32_t> path_{};
    size_t cursor_ = 0;
    UndoStack* stack_ = nullptr; // the watched stack (re-pointed by attach / every Document call)
    std::map<uint32_t, std::vector<glm::vec3>> checkpoints_; // node id -> positions after it
    size_t meshVertices_ = 0; // vertex count fast steps and checkpoints belong to
    std::vector<uint32_t> batch_; // vertices a batched run moved (flushBatch)
    std::vector<uint8_t> batchSeen_;
    SeekStats lastSeek_;
    double flushMs_ = 0.0;
};

// The sidecar file beside a project: "<dir>/<name>.sfhist" for "<dir>/<name>.sfp" (any extension).
std::string historySidecarPath(const std::string& projectPath);
// Writes every SubTool's history (those with a timeline) to the sidecar of `projectPath`; removes a stale one when
// there's nothing to save. Returns false with `error` on a write failure.
bool saveHistorySidecar(Document& doc, const std::string& projectPath, std::string* error = nullptr);
// Reads the sidecar of `projectPath`, if there is one, into the SubTools whose meshes it matches (attaching
// timelines). Returns how many SubTools got their history back.
int loadHistorySidecar(Document& doc, const std::string& projectPath, std::string* error = nullptr);

class CommandRegistry;
// timeline_info, timeline_seek, timeline_select, timeline_retune, timeline_save, timeline_load.
void registerTimelineCommands(CommandRegistry& registry);

} // namespace sf
