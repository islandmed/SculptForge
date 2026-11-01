// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/Undo.h"

#include "core/SelectionSets.h"

#include <utility>

namespace sf {

size_t UndoRecord::approxBytes() const {
    size_t bytes = positions.bytes() + mask.bytes() + colors.bytes() + paintAlpha.bytes() + hidden.bytes() +
                   polygroups.bytes() + (paint ? paint->bytes() : 0) +
                   (textureTiles ? textureTiles->bytes() : 0) + (texture ? texture->bytes() : 0) +
                   (subdiv ? subdiv->bytes() : 0) + (topology ? topology->bytes() : 0) +
                   (micro ? micro->bytes() : 0) + uvs.bytes() + (sets ? sets->bytes() : 0) + (uv ? uv->bytes() : 0);
    for (const auto& l : layerOffsets) bytes += l.bytes();
    if (mesh)
        bytes += mesh->vertexCount() * ((2 + (mesh->base.empty() ? 0 : 1 + mesh->layers.size())) * sizeof(glm::vec3) +
                                        8 * sizeof(uint32_t)) +
                 mesh->triangleCount() * (sizeof(glm::uvec3) + sizeof(glm::vec3) + 4 * sizeof(uint32_t)) +
                 mesh->mask.size() * sizeof(float) + mesh->colors.size() * sizeof(glm::vec3) +
                 mesh->polygroups.size() * sizeof(uint32_t) + mesh->hidden.size() + mesh->uvs.size() * sizeof(glm::vec2) +
                 mesh->quadPartners.size() * sizeof(uint32_t);
    if (mesh) {
        bytes += mesh->paintBase.size() * sizeof(glm::vec3);
        for (const auto& l : mesh->paintLayers) bytes += l.color.size() * sizeof(glm::vec3) + l.alpha.size() * sizeof(float);
    }
    return bytes;
}

namespace {

// Records `get(i)` for the listed elements, or for all `count` when `which` is empty.
template <class T, class Get>
void captureChannel(UndoChannel<T>& ch, size_t count, std::span<const uint32_t> which, Get get) {
    if (which.empty()) {
        ch.indices.resize(count);
        ch.values.resize(count);
        for (uint32_t i = 0; i < count; ++i) {
            ch.indices[i] = i;
            ch.values[i] = get(i);
        }
        return;
    }
    for (uint32_t i : which) ch.add(i, get(i));
}

} // namespace

UndoRecord UndoRecord::captureMask(const Mesh& mesh, std::string label, std::span<const uint32_t> verts) {
    UndoRecord r;
    r.label = std::move(label);
    captureChannel(r.mask, mesh.vertexCount(), verts, [&](uint32_t v) { return mesh.maskAt(v); });
    return r;
}

UndoRecord UndoRecord::captureColors(const Mesh& mesh, std::string label, std::span<const uint32_t> verts,
                                     int paintLayer) {
    UndoRecord r;
    r.label = std::move(label);
    if (mesh.paintLayers.empty()) {
        captureChannel(r.colors, mesh.vertexCount(), verts, [&](uint32_t v) { return mesh.colorAt(v); });
        return r;
    }
    if (paintLayer == kActivePaintLayer) paintLayer = mesh.activePaintLayer;
    if (paintLayer < -1 || paintLayer >= int(mesh.paintLayers.size())) paintLayer = -1;
    r.paintLayer = paintLayer;
    if (paintLayer < 0) {
        captureChannel(r.colors, mesh.vertexCount(), verts, [&](uint32_t v) { return mesh.paintBase[v]; });
        return r;
    }
    const PaintLayer& l = mesh.paintLayers[size_t(paintLayer)];
    captureChannel(r.colors, mesh.vertexCount(), verts, [&](uint32_t v) { return l.color[v]; });
    captureChannel(r.paintAlpha, mesh.vertexCount(), verts, [&](uint32_t v) { return l.alpha[v]; });
    return r;
}

PaintStackUndo PaintStackUndo::capture(const Mesh& mesh, bool propertiesOnly) {
    PaintStackUndo s;
    s.propertiesOnly = propertiesOnly;
    s.active = mesh.activePaintLayer;
    if (!propertiesOnly) {
        s.colors = mesh.colors;
        s.base = mesh.paintBase;
        s.layers = mesh.paintLayers;
        return s;
    }
    for (const PaintLayer& l : mesh.paintLayers) {
        PaintLayer p;
        p.name = l.name;
        p.opacity = l.opacity;
        p.blend = l.blend;
        p.visible = l.visible;
        p.locked = l.locked;
        s.layers.push_back(std::move(p));
    }
    return s;
}

void PaintStackUndo::swapWith(Mesh& mesh) {
    std::swap(active, mesh.activePaintLayer);
    if (!propertiesOnly) {
        // Painted <-> unpainted changes which vertex attributes the renderer uploads.
        if (colors.empty() != mesh.colors.empty()) mesh.topologyDirty = true;
        std::swap(colors, mesh.colors);
        std::swap(base, mesh.paintBase);
        std::swap(layers, mesh.paintLayers);
        mesh.markAllDirty();
        return;
    }
    for (size_t i = 0; i < layers.size() && i < mesh.paintLayers.size(); ++i) {
        PaintLayer& a = layers[i];
        PaintLayer& b = mesh.paintLayers[i];
        std::swap(a.name, b.name);
        std::swap(a.opacity, b.opacity);
        std::swap(a.blend, b.blend);
        std::swap(a.visible, b.visible);
        std::swap(a.locked, b.locked);
    }
    compositePaintAll(mesh);
}

size_t PaintStackUndo::bytes() const {
    size_t n = (colors.size() + base.size()) * sizeof(glm::vec3) + layers.size() * sizeof(PaintLayer);
    for (const auto& l : layers) n += l.color.size() * sizeof(glm::vec3) + l.alpha.size() * sizeof(float);
    return n;
}

UndoRecord UndoRecord::captureHidden(const Mesh& mesh, std::string label, std::span<const uint32_t> tris) {
    UndoRecord r;
    r.label = std::move(label);
    captureChannel(r.hidden, mesh.triangleCount(), tris, [&](uint32_t t) { return uint8_t(mesh.triangleHidden(t)); });
    return r;
}

UndoRecord UndoRecord::capturePolygroups(const Mesh& mesh, std::string label, std::span<const uint32_t> tris) {
    UndoRecord r;
    r.label = std::move(label);
    captureChannel(r.polygroups, mesh.triangleCount(), tris, [&](uint32_t t) { return mesh.polygroupAt(t); });
    return r;
}

void UndoStack::push(UndoRecord record) {
    if (record.empty()) return;
    undo_.push_back(std::move(record));
    if (listener) listener->undoPushed(undo_.back(), redo_);
    redo_.clear();
    ++revision_;
    enforceBudget();
}

// Swaps the record's state with the mesh, so the record now holds the inverse.
void UndoStack::apply(UndoRecord& r, Mesh& mesh, BVH& bvh) {
    if (r.subdiv) {
        applySubdivUndo(*r.subdiv, mesh, bvh);
        return;
    }
    if (r.micro) r.micro->swapWith(mesh); // (the base doesn't move: no BVH work)
    if (r.sets) std::swap(mesh.selectionSets, r.sets->list);
    if (r.mesh) {
        std::swap(*r.mesh, mesh);
        mesh.topologyDirty = true;
        bvh.build(mesh);
        return;
    }
    if (r.topology) {
        // A dynamic-topology stroke: its slots back, then what follows from them (the other channels name
        // slots it didn't touch).
        r.topology->swapWith(mesh);
        mesh.rebuildTopology();
        std::vector<uint32_t> restored;
        for (uint32_t v : r.topology->reevaluate)
            if (v < mesh.vertexCount()) restored.push_back(v);
        if (!restored.empty() && !mesh.layers.empty()) mesh.evaluate(restored);
        if (!restored.empty() && !mesh.paintLayers.empty()) compositePaint(mesh, restored);
        mesh.recomputeAllNormals();
        bvh.build(mesh);
    }
    if (!r.positions.empty()) {
        r.positions.swapWithUnique(mesh.layerTarget(r.layer));
        for (size_t i = 0; i < r.layerOffsets.size() && i < mesh.layers.size(); ++i)
            r.layerOffsets[i].swapWithUnique(mesh.layers[i].delta);
        mesh.evaluate(r.positions.indices);
        mesh.updateNormals(r.positions.indices);
        // Refits only grow boxes: after moving much of the mesh (a transform), tighten the whole tree instead.
        if (r.positions.indices.size() * 4 > mesh.vertexCount()) bvh.refitAll(mesh);
        else bvh.refit(mesh, r.positions.indices);
    }
    if (!r.mask.empty()) {
        r.mask.swapWith(mesh.ensureMask());
        for (uint32_t v : r.mask.indices) mesh.markDirty(v);
    }
    if (r.paint) r.paint->swapWith(mesh);
    if (r.uv) r.uv->swapWith(mesh);
    if (r.texture) r.texture->swapWith(mesh);
    if (r.textureTiles) r.textureTiles->swapWith(mesh);
    if (!r.colors.empty()) {
        const int layer = r.paintLayer;
        if (mesh.paintLayers.empty()) {
            r.colors.swapWith(mesh.ensureColors());
        } else if (layer < 0 || layer >= int(mesh.paintLayers.size())) {
            r.colors.swapWith(mesh.paintBase);
        } else {
            r.colors.swapWith(mesh.paintLayers[size_t(layer)].color);
            if (!r.paintAlpha.empty()) r.paintAlpha.swapWith(mesh.paintLayers[size_t(layer)].alpha);
        }
        compositePaint(mesh, r.colors.indices); // marks them dirty
    }
    // Picking reads `hidden` directly, so the BVH needs no update.
    if (!r.hidden.empty()) {
        r.hidden.swapWith(mesh.ensureHidden());
        mesh.facesDirty = true;
    }
    if (!r.polygroups.empty()) {
        r.polygroups.swapWith(mesh.ensurePolygroups());
        mesh.facesDirty = true;
    }
    if (!r.uvs.empty()) {
        if (mesh.uvs.size() != mesh.triangleCount() * 3) mesh.uvs.assign(mesh.triangleCount() * 3, glm::vec2(0.0f));
        r.uvs.swapWith(mesh.uvs);
        mesh.facesDirty = true;
        mesh.topologyDirty = true;
    }
}

size_t SelectionSetsUndo::bytes() const {
    if (!list) return 0;
    size_t total = 0;
    for (const auto& s : list->sets)
        if (s.use_count() == 1) total += s->bytes();
    return total;
}

std::unique_ptr<UvUndo> UvUndo::capture(const Mesh& mesh) {
    auto u = std::make_unique<UvUndo>();
    u->uvs = mesh.uvs;
    u->flags = mesh.uvFlags;
    return u;
}

void UvUndo::commit(const Mesh& mesh) {
    // Sparse when the arrays keep their sizes and under half of the corners changed.
    if (sparse || uvs.size() != mesh.uvs.size() || flags.size() != mesh.uvFlags.size()) return;
    UndoChannel<glm::vec2> cu;
    UndoChannel<uint8_t> cf;
    for (uint32_t c = 0; c < uvs.size(); ++c)
        if (uvs[c] != mesh.uvs[c]) cu.add(c, uvs[c]);
    for (uint32_t c = 0; c < flags.size(); ++c)
        if (flags[c] != mesh.uvFlags[c]) cf.add(c, flags[c]);
    if (cu.bytes() + cf.bytes() >= (uvs.size() * sizeof(glm::vec2) + flags.size()) / 2) return;
    uvCorners = std::move(cu);
    flagCorners = std::move(cf);
    std::vector<glm::vec2>().swap(uvs);
    std::vector<uint8_t>().swap(flags);
    sparse = true;
}

void UvUndo::swapWith(Mesh& mesh) {
    if (sparse) {
        if (mesh.uvs.size() >= mesh.triangleCount() * 3) uvCorners.swapWith(mesh.uvs);
        if (mesh.uvFlags.size() >= mesh.triangleCount() * 3) flagCorners.swapWith(mesh.uvFlags);
    } else {
        std::swap(uvs, mesh.uvs);
        std::swap(flags, mesh.uvFlags);
    }
    mesh.facesDirty = true; // the renderer's per-triangle UVs
}

size_t UvUndo::bytes() const {
    return uvs.size() * sizeof(glm::vec2) + flags.size() + uvCorners.bytes() + flagCorners.bytes();
}

bool UndoStack::undo(Mesh& mesh, BVH& bvh) {
    if (undo_.empty()) return false;
    UndoRecord r = std::move(undo_.back());
    undo_.pop_back();
    if (listener) listener->undoApplying(r, mesh, true);
    apply(r, mesh, bvh);
    redo_.push_back(std::move(r));
    if (listener) listener->undoApplied(redo_.back(), mesh, true);
    ++revision_;
    return true;
}

bool UndoStack::redo(Mesh& mesh, BVH& bvh) {
    if (redo_.empty()) return false;
    UndoRecord r = std::move(redo_.back());
    redo_.pop_back();
    if (listener) listener->undoApplying(r, mesh, false);
    apply(r, mesh, bvh);
    undo_.push_back(std::move(r));
    if (listener) listener->undoApplied(undo_.back(), mesh, false);
    ++revision_;
    return true;
}

void UndoStack::clear() {
    undo_.clear();
    redo_.clear();
    if (listener) listener->undoCleared();
}

size_t UndoStack::undoBytes() const {
    size_t total = 0;
    for (const auto& r : undo_) total += r.approxBytes();
    return total;
}

void UndoStack::enforceBudget() {
    size_t total = 0;
    for (const auto& r : undo_) total += r.approxBytes();
    while (undo_.size() > 1 && total > byteBudget) {
        total -= undo_.front().approxBytes();
        undo_.pop_front();
        if (listener) listener->undoDroppedOldest();
    }
}

} // namespace sf
