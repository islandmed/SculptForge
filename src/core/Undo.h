// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/BVH.h"
#include "core/Dyntopo.h"
#include "core/Mesh.h"
#include "core/Micro.h"
#include "core/Multires.h"
#include "core/ThreadPool.h"
#include "core/TileImage.h"

#include <deque>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace sf {

// Sparse old values of one mesh array: values[i] belongs to element indices[i].
template <class T>
struct UndoChannel {
    std::vector<uint32_t> indices;
    std::vector<T> values;

    bool empty() const { return indices.empty(); }
    void add(uint32_t index, const T& value) {
        indices.push_back(index);
        values.push_back(value);
    }
    // Exchanges the stored values with `target`, so the channel then holds the inverse.
    void swapWith(std::vector<T>& target) {
        for (size_t i = 0; i < indices.size(); ++i) std::swap(target[indices[i]], values[i]);
    }
    // swapWith() in parallel, for channels that list each element once (positions: strokes, transforms).
    void swapWithUnique(std::vector<T>& target) {
        poolFor(indices.size(), [&](size_t b, size_t e) {
            for (size_t i = b; i < e; ++i) std::swap(target[indices[i]], values[i]);
        }, 1 << 14);
    }
    size_t bytes() const { return indices.size() * (sizeof(uint32_t) + sizeof(T)); }
};

// The paint-layer stack (core/PaintLayer.h) before a layer edit; applying swaps
// it with the mesh's, so it then holds the inverse. `propertiesOnly` steps
// (opacity, blend, visibility, lock, name) keep just the layer settings and
// the active layer; the others keep every layer's pixels too.
struct PaintStackUndo {
    bool propertiesOnly = false;
    std::vector<glm::vec3> colors, base;
    std::vector<PaintLayer> layers;
    int active = -1;

    static PaintStackUndo capture(const Mesh& mesh, bool propertiesOnly = false);
    void swapWith(Mesh& mesh);
    size_t bytes() const;
};

// Texels of one RGBA8 texture array (core/Texture.h) as 64x64 tiles: only the listed tiles are stored, each
// as its texels or, when they are all the same, as one RGBA value (a blank layer's tile costs 4 bytes).
struct PackedTexels {
    uint32_t id = 0;               // which array: TextureLayer::id, 0 = Base (the texture without layers)
    std::vector<uint32_t> tiles;   // tile index (row-major, sorted)
    std::vector<uint32_t> offsets; // where tile i's bytes start in `data`; offsets.back() == data.size()
    std::vector<uint8_t> data;
    size_t bytes() const { return data.size() + (tiles.size() + offsets.size()) * sizeof(uint32_t); }
};

// Old texels of the 64x64 tiles a texture-paint stroke touched in one layer (core/Texture.h);
// applying swaps them with the texture.
// Material channels (core/TextureChannels.h) are kept per (layer, channel) as the old tile pointers of the
// channel's TileImage: nothing is copied, the tiles are shared with the image until it overwrites them.
struct TextureChannelTiles {
    int layer = -1;      // -1 = Base
    uint8_t channel = 0; // Channel
    std::vector<uint32_t> tiles;
    std::vector<std::shared_ptr<TileImage::Tile>> old; // per tile; null = the image's fill
    size_t bytes() const;
};
struct TextureTileUndo {
    int layer = -1;
    PackedTexels texels; // base colour (no tiles when the stroke painted only material channels)
    std::vector<TextureChannelTiles> channels;
    void swapWith(Mesh& mesh);
    size_t bytes() const;
};

// The texture before a texture edit (create, delete, layer edits, conversions, fills). captureTexture
// (core/Texture.h) holds the whole old texture; commit() (finishTextureUndo) then keeps only what the edit
// changed: the layer settings, and per layer the tiles that differ from the texture after the edit (layers
// are matched by TextureLayer::id). Applying rebuilds that texture from the mesh's and swaps.
struct TextureStackUndo {
    // Uncommitted: the whole texture. Committed: its size, settings and layers without texels. Null = no texture.
    std::shared_ptr<PaintTexture> texture;
    std::vector<PackedTexels> planes; // committed: [0] = Base, then one per layer
    bool committed = false;
    bool paintToTexture = false;
    void commit(const Mesh& mesh); // `mesh` holds the texture after the edit
    void swapWith(Mesh& mesh);
    size_t bytes() const;
};

// The UVs and UV flags (seams, pins; core/Uv.h) before a UV edit. capture() holds the whole arrays; commit()
// (with the mesh after the edit) keeps only the corners that changed when that is smaller. Applying swaps them
// with the mesh's, so the record then holds the inverse.
struct UvUndo {
    bool sparse = false;
    std::vector<glm::vec2> uvs;      // whole: the old array (empty = the mesh had none)
    std::vector<uint8_t> flags;      // whole: the old flags (empty = none)
    UndoChannel<glm::vec2> uvCorners; // sparse: old UVs of changed corners
    UndoChannel<uint8_t> flagCorners; // sparse: old flags of changed corners
    static std::unique_ptr<UvUndo> capture(const Mesh& mesh);
    void commit(const Mesh& mesh);
    void swapWith(Mesh& mesh);
    size_t bytes() const;
};

// The Selection Sets list before a set was stored, renamed, deleted or picked as the Morph target
// (core/SelectionSets.h); applying swaps it with the mesh's. Sets are immutable and shared, so this holds pointers.
struct SelectionSetsUndo {
    std::shared_ptr<const SelectionSetList> list;
    size_t bytes() const; // what only this step keeps alive
};

// Adding or removing a whole SubTool (Extract's Accept; core/Document.h). Applied by the undo / redo commands,
// which see the whole scene: the record itself leaves the mesh alone.
struct SubtoolStep;

// "The active paint layer" for UndoRecord::captureColors.
inline constexpr int kActivePaintLayer = -2;

// One undoable step: any mix of sparse channels, or a full mesh snapshot
// (topology/layer-set changes; the snapshot holds every attribute).
//  - positions: the stroke's target array (positions, layer base or a layer's
//    offsets; see Mesh::layerTarget), selected by `layer`; layerOffsets: every
//    layer's offsets too (transforms). Each lists a vertex at most once.
//  - mask / colors: per-vertex mask and polypaint. `colors` holds the paint
//    target of `paintLayer` (Mesh::colors without paint layers, else Base for
//    -1 or that layer's colours, with its coverage in `paintAlpha`)
//  - paint: the whole paint-layer stack (layer add / delete / merge / settings)
//  - hidden / polygroups: per-triangle visibility and group ids
//  - textureTiles / texture: texture-paint tiles of one layer, or the whole texture
//  - subdiv: a subdivision level step or level edit (core/Multires.h), applied on its own
//  - topology: the slots a dynamic-topology stroke changed (core/Dyntopo.h), applied before the channels
//  - subtool: a SubTool added to the scene (Extract), taken out on undo and put back on redo
//  - micro: the grid tiles a stroke on a micro level changed, or a whole micro level (core/Micro.h)
//  - uvs: per triangle corner texture coordinates (restoring a selection set's UVs)
//  - sets: the Selection Sets list (core/SelectionSets.h)
// Record old values before changing them (ensureX() the attribute first);
// undo allocates a missing attribute so values can always be restored.
struct UndoRecord {
    std::string label;
    int layer = -1;
    UndoChannel<glm::vec3> positions;
    // Transforms (core/MeshTransform.h) move the base and every sculpt layer together: with `positions` on the
    // base (layer -1), layerOffsets[i] holds the old offsets of layer i for the same vertices.
    std::vector<UndoChannel<glm::vec3>> layerOffsets;
    UndoChannel<float> mask;
    UndoChannel<glm::vec3> colors;
    int paintLayer = -1;
    UndoChannel<float> paintAlpha;
    std::unique_ptr<PaintStackUndo> paint;
    UndoChannel<uint8_t> hidden;
    UndoChannel<uint32_t> polygroups;
    std::unique_ptr<TextureTileUndo> textureTiles;
    std::unique_ptr<TextureStackUndo> texture;
    std::unique_ptr<Mesh> mesh;
    std::unique_ptr<SubdivUndo> subdiv;
    std::unique_ptr<TopologyUndo> topology;
    std::shared_ptr<SubtoolStep> subtool; // (shared: SubtoolStep is defined in core/Document.h)
    std::unique_ptr<UvUndo> uv;           // UVs, seams and pins (UV Editor, core/Uv.h)
    std::unique_ptr<MicroUndo> micro;     // micro-displacement tiles of a stroke, or a micro level added / removed (core/Micro.h)
    UndoChannel<glm::vec2> uvs;           // per corner (index = triangle * 3 + corner)
    std::unique_ptr<SelectionSetsUndo> sets;

    bool empty() const {
        return positions.empty() && mask.empty() && colors.empty() && paintAlpha.empty() && hidden.empty() &&
               polygroups.empty() && !mesh && !paint && !textureTiles && !texture && !subdiv && !topology && !subtool && !micro &&
               uvs.empty() && !sets && !uv;
    }
    size_t approxBytes() const;

    // Convenience snapshots of whole attributes (or of `elements` only), e.g.
    // before "mask all", "invert mask", "fill color" or "hide group".
    static UndoRecord captureMask(const Mesh& mesh, std::string label, std::span<const uint32_t> verts = {});
    // captureColors records the paint target of `paintLayer` (default: the active paint layer).
    static UndoRecord captureColors(const Mesh& mesh, std::string label, std::span<const uint32_t> verts = {},
                                    int paintLayer = kActivePaintLayer);
    static UndoRecord captureHidden(const Mesh& mesh, std::string label, std::span<const uint32_t> tris = {});
    static UndoRecord capturePolygroups(const Mesh& mesh, std::string label, std::span<const uint32_t> tris = {});
};

// Watches an UndoStack: the Sculpt Timeline (core/Timeline.h) keeps its history tree in step with it. Held by the
// stack (UndoStack::listener), so it moves with it (each SubTool's history has its own).
struct UndoListener {
    virtual ~UndoListener() = default;
    // A step was pushed (it is the stack's last undo step); `discardedRedo` holds the redo steps the push is about
    // to drop (redo_.back() is the next redo) and may be moved from.
    virtual void undoPushed(UndoRecord& pushed, std::deque<UndoRecord>& discardedRedo) = 0;
    // Around UndoStack::undo / redo applying `r` to `mesh`.
    virtual void undoApplying(UndoRecord& r, Mesh& mesh, bool isUndo) = 0;
    virtual void undoApplied(UndoRecord& r, Mesh& mesh, bool isUndo) = 0;
    virtual void undoDroppedOldest() = 0; // the byte budget dropped the oldest undo step
    virtual void undoCleared() = 0;
};

class Timeline;

class UndoStack {
    friend class Timeline; // moves steps between undo and redo without applying them (core/Timeline.h)
public:
    UndoStack() = default;
    UndoStack(UndoStack&&) = default; // move-only (records own mesh snapshots)
    UndoStack& operator=(UndoStack&&) = default;
    UndoStack(const UndoStack&) = delete;
    UndoStack& operator=(const UndoStack&) = delete;
    void push(UndoRecord record);
    bool canUndo() const { return !undo_.empty(); }
    bool canRedo() const { return !redo_.empty(); }
    const std::string& undoLabel() const { return undo_.back().label; }
    const std::string& redoLabel() const { return redo_.back().label; }
    size_t undoCount() const { return undo_.size(); }
    // The step the next undo / redo applies (null if none): after undo() the undone step is lastRedo().
    UndoRecord* lastUndo() { return undo_.empty() ? nullptr : &undo_.back(); }
    UndoRecord* lastRedo() { return redo_.empty() ? nullptr : &redo_.back(); }
    // The undo step BACK places below the top (0 = the next undo), or null.
    const UndoRecord* undoStep(size_t back) const { return back < undo_.size() ? &undo_[undo_.size() - 1 - back] : nullptr; }

    // Both rebuild/refit the BVH as needed.
    bool undo(Mesh& mesh, BVH& bvh);
    bool redo(Mesh& mesh, BVH& bvh);
    void clear();
    // Bumped by every push, undo and redo (unsaved-changes tracking).
    uint64_t revision() const { return revision_; }
    // Memory the undo steps hold (UndoRecord::approxBytes), as counted against byteBudget.
    size_t undoBytes() const;

    size_t byteBudget = size_t(1) << 30; // 1 GiB
    std::shared_ptr<UndoListener> listener; // the Sculpt Timeline (core/Timeline.h), if one watches this history

private:
    static void apply(UndoRecord& r, Mesh& mesh, BVH& bvh);
    void enforceBudget();

    std::deque<UndoRecord> undo_, redo_;
    uint64_t revision_ = 0;
};

} // namespace sf
