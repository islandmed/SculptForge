// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/Document.h"

#include "core/Multires.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <memory>

namespace sf {

Document::Document() {
    parts.emplace_back();
    parts.back().id = 1;
    parts.back().data->key = 1;
    parts.back().name = "Sculpt";
}

uint32_t Document::newPartId(Part& p) {
    p.id = nextPartId_++;
    if (!p.data->key) p.data->key = p.id;
    return p.id;
}

void Document::setMesh(Mesh m, std::string name, BVH* built) {
    mesh = std::move(m);
    mesh.topologyDirty = true;
    if (built) bvh = std::move(*built);
    else bvh.build(mesh);
    undo.clear();
    activeLayer = -1;
    bvhDirty = false;
    bridgeName.clear();
    unitlessScale = 0.0;
    unitlessFormat.clear();
    qremeshGuides.clear();
    partFolders.clear();
    parts.clear();
    parts.emplace_back();
    newPartId(parts.back());
    parts.back().name = name.empty() ? "Sculpt" : std::move(name);
    activePart = 0;
    extractPreview = {};
    markClean();
}

bool Document::dirty() const {
    if (modified || undo.revision() != cleanRevision_ || splatsVersion != cleanSplatsVersion_) return true;
    for (int i = 0; i < partCount(); ++i)
        if (!checkedOut(i) && parts[size_t(i)].data->undo.revision() != parts[size_t(i)].data->cleanRevision) return true;
    return false;
}

void Document::markClean() {
    modified = false;
    cleanRevision_ = undo.revision();
    cleanSplatsVersion_ = splatsVersion;
    for (int i = 0; i < partCount(); ++i)
        if (!checkedOut(i)) parts[size_t(i)].data->cleanRevision = parts[size_t(i)].data->undo.revision();
}

BVH& Document::partBvh(int i) {
    if (checkedOut(i)) {
        syncBvh();
        return bvh;
    }
    PartData& d = *parts[size_t(i)].data;
    if (d.bvhDirty || (d.bvh.nodeCount() == 0 && d.mesh.triangleCount())) {
        d.bvh.build(d.mesh);
        d.bvhDirty = false;
    }
    return d.bvh;
}

void Document::selectPart(int i) {
    if (i < 0 || i >= partCount() || i == activePart) return;
    if (checkedOut(i)) { // a linked copy of the active Part: its data is checked out already
        activePart = i;
        return;
    }
    uvSelection.clear();
    edgeSelection.clear();
    // Check the active Part back in, then check `i` out. Swapping keeps each
    // mesh's GPU dirty state, so the renderer doesn't re-upload anything.
    auto swapIn = [this](PartData& s) {
        std::swap(mesh, s.mesh);
        std::swap(bvh, s.bvh);
        std::swap(undo, s.undo);
        std::swap(activeLayer, s.activeLayer);
        std::swap(bridgeName, s.bridgeName);
        std::swap(qremeshGuides, s.qremeshGuides);
        std::swap(bvhDirty, s.bvhDirty);
        std::swap(cleanRevision_, s.cleanRevision);
    };
    swapIn(*parts[size_t(activePart)].data);
    subdivReleaseCache(parts[size_t(activePart)].data->mesh); // only the active Part keeps cached levels
    activePart = i;
    swapIn(*parts[size_t(i)].data);
    if (bvh.nodeCount() == 0 && mesh.triangleCount()) bvhDirty = true; // built on first use
    syncBvh();
}

int Document::addPart(Mesh m, std::string name, bool select, int at, BVH* built) {
    Part s;
    newPartId(s);
    s.name = uniquePartName(name.empty() ? "Part" : name);
    s.data->mesh = std::move(m);
    s.data->mesh.topologyDirty = true;
    if (built) s.data->bvh = std::move(*built);
    else s.data->bvh.build(s.data->mesh);
    const int index = at < 0 || at > partCount() ? partCount() : at;
    parts.insert(parts.begin() + index, std::move(s));
    if (index <= activePart) ++activePart;
    modified = true;
    if (select) selectPart(index);
    return index;
}

void Document::replaceActiveMesh(Mesh m) {
    mesh = std::move(m);
    mesh.topologyDirty = true;
    bvh.build(mesh);
    bvhDirty = false;
    undo.clear();
    cleanRevision_ = ~uint64_t(0); // unsaved until the next save
    activeLayer = -1;
    modified = true;
}

bool Document::removePart(int i) {
    if (partCount() <= 1 || i < 0 || i >= partCount()) return false;
    if (i == activePart) selectPart(i + 1 < partCount() ? i + 1 : i - 1);
    parts.erase(parts.begin() + i);
    if (activePart > i) --activePart;
    modified = true;
    return true;
}

void Document::movePart(int i, int direction) {
    const int j = i + (direction < 0 ? -1 : 1);
    if (i < 0 || j < 0 || i >= partCount() || j >= partCount()) return;
    std::swap(parts[size_t(i)], parts[size_t(j)]);
    if (activePart == i) activePart = j;
    else if (activePart == j) activePart = i;
    modified = true;
}

int Document::partIndex(uint32_t id) const {
    for (int i = 0; i < partCount(); ++i)
        if (parts[size_t(i)].id == id) return i;
    return -1;
}

bool Document::applyPartStep(PartStep& step) {
    if (step.isTransform) {
        const int i = partIndex(step.id);
        if (i < 0) return false;
        std::swap(parts[size_t(i)].transform, step.transform);
        modified = true;
        return true;
    }
    if (step.stashed) { // redo: put it back where it was
        const int at = std::clamp(step.at, 0, partCount());
        parts.insert(parts.begin() + at, std::move(*step.stashed));
        step.stashed.reset();
        if (at <= activePart) ++activePart;
        partMesh(at).topologyDirty = true;
        modified = true;
        return true;
    }
    const int i = partIndex(step.id);
    if (i < 0 || partCount() <= 1) return false;
    if (i == activePart) { // a linked copy (its history is its group's): another Part of the group takes over
        int other = -1;
        for (int k = 0; k < partCount() && other < 0; ++k)
            if (k != i && checkedOut(k)) other = k;
        if (other < 0) return false;
        selectPart(other);
    }
    step.at = i;
    step.stashed = std::make_unique<Part>(std::move(parts[size_t(i)]));
    parts.erase(parts.begin() + i);
    if (activePart > i) --activePart;
    modified = true;
    return true;
}

bool Document::extractPreviewCurrent() const {
    if (!extractPreview.id) return false;
    const int i = partIndex(extractPreview.id);
    return i >= 0 && i != activePart && activePartInfo().id == extractPreview.source &&
           undo.revision() == extractPreview.revision;
}

void Document::dropExtractPreview(bool staleOnly) {
    if (!extractPreview.id || (staleOnly && extractPreviewCurrent())) return;
    const int i = partIndex(extractPreview.id);
    extractPreview = {};
    if (i >= 0) removePart(i);
}

int Document::findPart(const std::string& name) const {
    for (int i = 0; i < partCount(); ++i)
        if (parts[size_t(i)].name == name) return i;
    return -1;
}

std::string Document::uniquePartName(const std::string& wanted) const {
    if (findPart(wanted) < 0) return wanted;
    for (int n = 2;; ++n) {
        std::string candidate = wanted + " " + std::to_string(n);
        if (findPart(candidate) < 0) return candidate;
    }
}

uint32_t Document::addPartFolder(const std::string& name) {
    PartFolder f;
    f.id = nextFolderId_++;
    f.name = uniqueFolderName(name.empty() ? "Folder" : name);
    partFolders.push_back(std::move(f));
    modified = true;
    return partFolders.back().id;
}

int Document::partFolderIndex(uint32_t id) const {
    for (size_t i = 0; i < partFolders.size(); ++i)
        if (partFolders[i].id == id) return int(i);
    return -1;
}

int Document::findPartFolder(const std::string& name) const {
    for (size_t i = 0; i < partFolders.size(); ++i)
        if (partFolders[i].name == name) return int(i);
    return -1;
}

void Document::removePartFolder(uint32_t id) {
    const int f = partFolderIndex(id);
    if (f < 0) return;
    for (Part& s : parts)
        if (s.folder == id) s.folder = 0;
    partFolders.erase(partFolders.begin() + f);
    modified = true;
}

int Document::setPartFolder(int i, uint32_t id) {
    if (i < 0 || i >= partCount() || (id && partFolderIndex(id) < 0)) return i;
    if (parts[size_t(i)].folder == id) return i;
    parts[size_t(i)].folder = id;
    modified = true;
    int last = -1; // the folder's last other Part
    for (int k = 0; k < partCount(); ++k)
        if (k != i && id && parts[size_t(k)].folder == id) last = k;
    if (last < 0) return i;
    const uint32_t activeId = activePartInfo().id;
    const int to = last < i ? last + 1 : last; // where it lands once taken out
    if (to < i) std::rotate(parts.begin() + to, parts.begin() + i, parts.begin() + i + 1);
    else if (to > i) std::rotate(parts.begin() + i, parts.begin() + i + 1, parts.begin() + to + 1);
    activePart = partIndex(activeId);
    return to;
}

std::string Document::uniqueFolderName(const std::string& wanted) const {
    if (findPartFolder(wanted) < 0) return wanted;
    for (int n = 2;; ++n) {
        std::string candidate = wanted + " " + std::to_string(n);
        if (findPartFolder(candidate) < 0) return candidate;
    }
}

AABB Document::partBounds(int i) const {
    const AABB local = partMesh(i).bounds();
    const glm::mat4& t = parts[size_t(i)].transform;
    if (!local.valid() || !parts[size_t(i)].moved()) return local;
    AABB b;
    for (int c = 0; c < 8; ++c)
        b.expand(glm::vec3(t * glm::vec4(c & 1 ? local.max.x : local.min.x, c & 2 ? local.max.y : local.min.y,
                                         c & 4 ? local.max.z : local.min.z, 1.0f)));
    return b;
}

AABB Document::visibleBounds() const {
    AABB b;
    for (int i = 0; i < partCount(); ++i)
        if (parts[size_t(i)].visible && partMesh(i).vertexCount()) b.expand(partBounds(i));
    return b;
}

int Document::linkCount(int i) const {
    int n = 0;
    for (const Part& p : parts) n += p.data == parts[size_t(i)].data ? 1 : 0;
    return n;
}

int Document::addLink(int i, std::string name, const glm::mat4& transform, int at) {
    Part s;
    s.data = parts[size_t(i)].data;
    newPartId(s);
    s.name = uniquePartName(name.empty() ? parts[size_t(i)].name : name);
    s.transform = transform;
    at = at < 0 || at > partCount() ? partCount() : at;
    parts.insert(parts.begin() + at, std::move(s));
    if (at <= activePart) ++activePart;
    modified = true;
    return at;
}

int Document::linkedDuplicate(int i, const glm::vec3& offset) {
    int first = i; // named after the group's first Part: "Body link", "Body link 2", ...
    for (int k = 0; k < partCount(); ++k)
        if (parts[size_t(k)].data == parts[size_t(i)].data) {
            first = k;
            break;
        }
    const int at = addLink(i, parts[size_t(first)].name + " link",
                           glm::translate(glm::mat4(1.0f), offset) * parts[size_t(i)].transform, i + 1);
    parts[size_t(at)].visible = parts[size_t(at - 1)].visible;
    parts[size_t(at)].shading = parts[size_t(at - 1)].shading;
    selectPart(at);
    // One step in the shared history: undo takes the copy away again (applyPartStep).
    UndoRecord r;
    r.label = "Linked Duplicate";
    r.part = std::make_shared<PartStep>();
    r.part->id = parts[size_t(at)].id;
    r.part->at = at;
    undo.push(std::move(r));
    modified = true;
    return at;
}

bool Document::makeUnique(int i) {
    if (i < 0 || i >= partCount() || linkCount(i) < 2) return false;
    auto own = std::make_shared<PartData>();
    own->key = nextPartId_++; // (a new GPU mesh: the group keeps its key)
    const bool active = i == activePart;
    const PartData& from = *parts[size_t(i)].data;
    own->mesh = checkedOut(i) ? mesh : from.mesh;
    own->mesh.topologyDirty = true;
    own->activeLayer = checkedOut(i) ? activeLayer : from.activeLayer;
    own->qremeshGuides = checkedOut(i) ? qremeshGuides : from.qremeshGuides;
    own->bvh.build(own->mesh);
    own->cleanRevision = ~uint64_t(0); // unsaved until the next save
    if (active) {
        // Check the group's data back in through another of its Parts, then this one's own copy out.
        int other = -1;
        for (int k = 0; k < partCount() && other < 0; ++k)
            if (k != i && checkedOut(k)) other = k;
        activePart = other;
        parts[size_t(i)].data = std::move(own);
        selectPart(i);
    } else {
        parts[size_t(i)].data = std::move(own);
    }
    modified = true;
    return true;
}

void Document::setPartTransform(int i, const glm::mat4& transform, const std::string& label) {
    if (i < 0 || i >= partCount() || parts[size_t(i)].transform == transform) return;
    UndoRecord r;
    r.label = label;
    r.part = std::make_shared<PartStep>();
    r.part->id = parts[size_t(i)].id;
    r.part->isTransform = true;
    r.part->transform = parts[size_t(i)].transform;
    parts[size_t(i)].transform = transform;
    partUndo(i).push(std::move(r));
    modified = true;
}

int Document::addBrush(BrushRecipe recipe) {
    recipe.clamp();
    for (size_t i = 0; i < brushes.size(); ++i) {
        if (brushes[i].name == recipe.name) {
            // A session recipe with a library brush's name is an edit of that brush: it keeps its origin.
            if (recipe.origin.source == BrushSource::Session) recipe.origin = brushes[i].origin;
            brushes[i] = std::move(recipe);
            return int(i);
        }
    }
    brushes.push_back(std::move(recipe));
    return int(brushes.size()) - 1;
}

const char* shadingName(Shading s) {
    switch (s) {
    case Shading::Smooth: return "smooth";
    case Shading::Flat: return "flat";
    default: return "default";
    }
}

bool parseShading(const std::string& name, Shading& out) {
    for (Shading s : {Shading::Default, Shading::Smooth, Shading::Flat}) {
        std::string lower = name;
        for (char& c : lower) c = char(std::tolower(static_cast<unsigned char>(c)));
        if (lower == shadingName(s)) {
            out = s;
            return true;
        }
    }
    return false;
}

std::string Document::assetName() const {
    if (path.empty()) return "Sculpt";
    auto stem = std::filesystem::path(std::u8string(path.begin(), path.end())).stem().u8string();
    return std::string(stem.begin(), stem.end());
}

void Document::replaceMeshUndoable(Mesh m, std::string label) {
    if (!m.selectionSets) m.selectionSets = mesh.selectionSets; // the Part keeps its sets (restoring checks they fit)
    if (m.multires && !subdivMatches(m)) m.multires.reset();
    if (mesh.multires && !m.multires) {
        // A tool rebuilt the mesh from the current level: the levels go, the undo step keeps them.
        if (const std::string note = subdivDroppedNote(mesh); !note.empty())
            notice += (notice.empty() ? "" : "; ") + note + " (undo restores them)";
        subdivReleaseCache(mesh);
    }
    UndoRecord r;
    r.label = std::move(label);
    r.mesh = std::make_unique<Mesh>(std::move(mesh));
    mesh = std::move(m);
    mesh.topologyDirty = true;
    bvh.build(mesh);
    bvhDirty = false;
    undo.push(std::move(r));
    modified = true;
    if (activeLayer >= static_cast<int>(mesh.layers.size())) activeLayer = -1;
}

double Document::unitlessExportScale(const std::string& format) const {
    if (format == "stl") return unitlessFormat == "stl" && unitlessScale > 0.0 ? unitlessScale : 1000.0;
    return unitlessScale > 0.0 ? unitlessScale : 1.0;
}

} // namespace sf
