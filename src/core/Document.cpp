// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/Document.h"

#include "core/Multires.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <memory>

namespace sf {

Document::Document() {
    parts.emplace_back();
    parts.back().id = 1;
    parts.back().name = "Sculpt";
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
    parts.back().id = nextPartId_++;
    parts.back().name = name.empty() ? "Sculpt" : std::move(name);
    activePart = 0;
    extractPreview = {};
    markClean();
}

bool Document::dirty() const {
    if (modified || undo.revision() != cleanRevision_ || splatsVersion != cleanSplatsVersion_) return true;
    for (int i = 0; i < partCount(); ++i)
        if (i != activePart && parts[size_t(i)].undo.revision() != parts[size_t(i)].cleanRevision) return true;
    return false;
}

void Document::markClean() {
    modified = false;
    cleanRevision_ = undo.revision();
    cleanSplatsVersion_ = splatsVersion;
    for (int i = 0; i < partCount(); ++i)
        if (i != activePart) parts[size_t(i)].cleanRevision = parts[size_t(i)].undo.revision();
}

void Document::selectPart(int i) {
    if (i < 0 || i >= partCount() || i == activePart) return;
    uvSelection.clear();
    // Check the active Part back in, then check `i` out. Swapping keeps each
    // mesh's GPU dirty state, so the renderer doesn't re-upload anything.
    auto swapIn = [this](Part& s) {
        std::swap(mesh, s.mesh);
        std::swap(bvh, s.bvh);
        std::swap(undo, s.undo);
        std::swap(activeLayer, s.activeLayer);
        std::swap(bridgeName, s.bridgeName);
        std::swap(qremeshGuides, s.qremeshGuides);
        std::swap(bvhDirty, s.bvhDirty);
        std::swap(cleanRevision_, s.cleanRevision);
    };
    swapIn(parts[size_t(activePart)]);
    subdivReleaseCache(parts[size_t(activePart)].mesh); // only the active Part keeps cached levels
    activePart = i;
    swapIn(parts[size_t(i)]);
    if (bvh.nodeCount() == 0 && mesh.triangleCount()) bvhDirty = true; // built on first use
    syncBvh();
}

int Document::addPart(Mesh m, std::string name, bool select, int at, BVH* built) {
    Part s;
    s.id = nextPartId_++;
    s.name = uniquePartName(name.empty() ? "Part" : name);
    s.mesh = std::move(m);
    s.mesh.topologyDirty = true;
    if (built) s.bvh = std::move(*built);
    else s.bvh.build(s.mesh);
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
    if (step.stashed) { // redo: put it back where it was
        const int at = std::clamp(step.at, 0, partCount());
        parts.insert(parts.begin() + at, std::move(*step.stashed));
        step.stashed.reset();
        if (at <= activePart) ++activePart;
        parts[size_t(at)].mesh.topologyDirty = true;
        modified = true;
        return true;
    }
    const int i = partIndex(step.id);
    if (i < 0 || i == activePart || partCount() <= 1) return false;
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

AABB Document::visibleBounds() const {
    AABB b;
    for (int i = 0; i < partCount(); ++i)
        if (parts[size_t(i)].visible && partMesh(i).vertexCount()) b.expand(partMesh(i).bounds());
    return b;
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
