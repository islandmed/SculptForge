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
    subtools.emplace_back();
    subtools.back().id = 1;
    subtools.back().name = "Sculpt";
}

void Document::setMesh(Mesh m, std::string name, BVH* built) {
    mesh = std::move(m);
    mesh.topologyDirty = true;
    if (built) bvh = std::move(*built);
    else bvh.build(mesh);
    undo.clear();
    activeLayer = -1;
    bvhDirty = false;
    gozName.clear();
    qremeshGuides.clear();
    subtools.clear();
    subtools.emplace_back();
    subtools.back().id = nextSubtoolId_++;
    subtools.back().name = name.empty() ? "Sculpt" : std::move(name);
    activeSubtool = 0;
    extractPreview = {};
    markClean();
}

bool Document::dirty() const {
    if (modified || undo.revision() != cleanRevision_ || splatsVersion != cleanSplatsVersion_) return true;
    for (int i = 0; i < subtoolCount(); ++i)
        if (i != activeSubtool && subtools[size_t(i)].undo.revision() != subtools[size_t(i)].cleanRevision) return true;
    return false;
}

void Document::markClean() {
    modified = false;
    cleanRevision_ = undo.revision();
    cleanSplatsVersion_ = splatsVersion;
    for (int i = 0; i < subtoolCount(); ++i)
        if (i != activeSubtool) subtools[size_t(i)].cleanRevision = subtools[size_t(i)].undo.revision();
}

void Document::selectSubtool(int i) {
    if (i < 0 || i >= subtoolCount() || i == activeSubtool) return;
    uvSelection.clear();
    // Check the active SubTool back in, then check `i` out. Swapping keeps each
    // mesh's GPU dirty state, so the renderer doesn't re-upload anything.
    auto swapIn = [this](SubTool& s) {
        std::swap(mesh, s.mesh);
        std::swap(bvh, s.bvh);
        std::swap(undo, s.undo);
        std::swap(activeLayer, s.activeLayer);
        std::swap(gozName, s.gozName);
        std::swap(qremeshGuides, s.qremeshGuides);
        std::swap(bvhDirty, s.bvhDirty);
        std::swap(cleanRevision_, s.cleanRevision);
    };
    swapIn(subtools[size_t(activeSubtool)]);
    subdivReleaseCache(subtools[size_t(activeSubtool)].mesh); // only the active SubTool keeps cached levels
    activeSubtool = i;
    swapIn(subtools[size_t(i)]);
    if (bvh.nodeCount() == 0 && mesh.triangleCount()) bvhDirty = true; // built on first use
    syncBvh();
}

int Document::addSubtool(Mesh m, std::string name, bool select, int at, BVH* built) {
    SubTool s;
    s.id = nextSubtoolId_++;
    s.name = uniqueSubtoolName(name.empty() ? "Part" : name);
    s.mesh = std::move(m);
    s.mesh.topologyDirty = true;
    if (built) s.bvh = std::move(*built);
    else s.bvh.build(s.mesh);
    const int index = at < 0 || at > subtoolCount() ? subtoolCount() : at;
    subtools.insert(subtools.begin() + index, std::move(s));
    if (index <= activeSubtool) ++activeSubtool;
    modified = true;
    if (select) selectSubtool(index);
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

bool Document::removeSubtool(int i) {
    if (subtoolCount() <= 1 || i < 0 || i >= subtoolCount()) return false;
    if (i == activeSubtool) selectSubtool(i + 1 < subtoolCount() ? i + 1 : i - 1);
    subtools.erase(subtools.begin() + i);
    if (activeSubtool > i) --activeSubtool;
    modified = true;
    return true;
}

void Document::moveSubtool(int i, int direction) {
    const int j = i + (direction < 0 ? -1 : 1);
    if (i < 0 || j < 0 || i >= subtoolCount() || j >= subtoolCount()) return;
    std::swap(subtools[size_t(i)], subtools[size_t(j)]);
    if (activeSubtool == i) activeSubtool = j;
    else if (activeSubtool == j) activeSubtool = i;
    modified = true;
}

int Document::subtoolIndex(uint32_t id) const {
    for (int i = 0; i < subtoolCount(); ++i)
        if (subtools[size_t(i)].id == id) return i;
    return -1;
}

bool Document::applySubtoolStep(SubtoolStep& step) {
    if (step.stashed) { // redo: put it back where it was
        const int at = std::clamp(step.at, 0, subtoolCount());
        subtools.insert(subtools.begin() + at, std::move(*step.stashed));
        step.stashed.reset();
        if (at <= activeSubtool) ++activeSubtool;
        subtools[size_t(at)].mesh.topologyDirty = true;
        modified = true;
        return true;
    }
    const int i = subtoolIndex(step.id);
    if (i < 0 || i == activeSubtool || subtoolCount() <= 1) return false;
    step.at = i;
    step.stashed = std::make_unique<SubTool>(std::move(subtools[size_t(i)]));
    subtools.erase(subtools.begin() + i);
    if (activeSubtool > i) --activeSubtool;
    modified = true;
    return true;
}

bool Document::extractPreviewCurrent() const {
    if (!extractPreview.id) return false;
    const int i = subtoolIndex(extractPreview.id);
    return i >= 0 && i != activeSubtool && activeSubtoolInfo().id == extractPreview.source &&
           undo.revision() == extractPreview.revision;
}

void Document::dropExtractPreview(bool staleOnly) {
    if (!extractPreview.id || (staleOnly && extractPreviewCurrent())) return;
    const int i = subtoolIndex(extractPreview.id);
    extractPreview = {};
    if (i >= 0) removeSubtool(i);
}

int Document::findSubtool(const std::string& name) const {
    for (int i = 0; i < subtoolCount(); ++i)
        if (subtools[size_t(i)].name == name) return i;
    return -1;
}

std::string Document::uniqueSubtoolName(const std::string& wanted) const {
    if (findSubtool(wanted) < 0) return wanted;
    for (int n = 2;; ++n) {
        std::string candidate = wanted + " " + std::to_string(n);
        if (findSubtool(candidate) < 0) return candidate;
    }
}

AABB Document::visibleBounds() const {
    AABB b;
    for (int i = 0; i < subtoolCount(); ++i)
        if (subtools[size_t(i)].visible && subtoolMesh(i).vertexCount()) b.expand(subtoolMesh(i).bounds());
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

} // namespace sf
