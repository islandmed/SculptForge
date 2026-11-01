// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/BVH.h"
#include "core/BrushAlpha.h"
#include "core/BrushRecipe.h"
#include "core/Json.h"
#include "core/Mesh.h"
#include "core/QRemeshGuides.h"
#include "core/Sculpt.h"
#include "core/Splats.h"
#include "core/TextureChannels.h"
#include "core/Undo.h"
#include "core/Uv.h"
#include "core/Vdm.h"

#include <map>
#include <string>
#include <utility>

namespace sf {

// How a SubTool is shaded in the view: as the scene says (the Material palette's Flat shading), or always smooth
// or always flat (faceted: every polygon's own face normal). Display only; saved in projects.
enum class Shading : uint8_t { Default, Smooth, Flat };
const char* shadingName(Shading s);           // "default", "smooth", "flat"
bool parseShading(const std::string& name, Shading& out);

// One object in the scene, like a ZBrush SubTool: a body, the eyes, the
// teeth, a sword. Each keeps its own mesh, attributes, layers and undo
// history. The active SubTool is "checked out" into Document::mesh / bvh /
// undo, so every tool and command keeps working on the active one; its slot
// here holds only the name and flags until another SubTool is selected.
struct SubTool {
    uint32_t id = 0; // stable for the session (renderer slots, UI selection)
    std::string name;
    bool visible = true;
    Shading shading = Shading::Default; // per-SubTool override of the scene's shading
    // Inactive SubTools only (empty while active).
    Mesh mesh;
    BVH bvh;
    UndoStack undo;
    int activeLayer = -1;
    std::string gozName;
    bool bvhDirty = false;
    uint64_t cleanRevision = 0;
    std::vector<QRemeshGuide> qremeshGuides; // QRemesh Guides (core/QRemeshGuides.h)
};

// An undo step that added SubTool `id` at list position `at` (UndoRecord::subtool): `stashed` holds the SubTool
// while it's undone.
struct SubtoolStep {
    uint32_t id = 0;
    int at = 0;
    std::unique_ptr<SubTool> stashed;
};

// Extract's preview (core/ExtractMirror.h): SubTool `id`, made from the mask of SubTool `source` as it was at
// that SubTool's undo revision `revision`, shown until Accept turns it into an undoable step. id 0 = none.
struct ExtractPreview {
    uint32_t id = 0, source = 0;
    uint64_t revision = 0;
};

// Everything that makes up the open scene: the mesh, its acceleration
// structure and edit history. UI-independent so it can be driven by tests,
// scripts or an AI assistant through the command registry.
struct Document {
    // The active SubTool's working data (see SubTool).
    Mesh mesh;
    BVH bvh;
    UndoStack undo;
    Sculptor sculptor;
    std::string path; // empty until saved/loaded
    bool modified = false; // set by edits the undo stack doesn't see (e.g. layer weights)
    int activeLayer = -1; // sculpt layer strokes write into (-1 = base)
    // Recipe brush library (built-ins, studio and user library brushes, loaded .sfbrush files,
    // AI-made brushes; see core/BrushLibrary.h). Each recipe records its origin.
    std::vector<BrushRecipe> brushes = builtinBrushRecipes();
    int activeBrush = -1; // index into `brushes`, -1 = use a built-in brush type
    BrushAlpha brushAlpha; // stamp shaping every brush's falloff (empty = round); brush_set_alpha
    glm::vec3 paintColor{0.22f, 0.22f, 0.22f}; // current polypaint colour (linear RGB; see set_color); a mid grey
    // Material texture painting (core/TextureChannels.h): what the Paint brush lays on the material channels
    // (paint_material; its colour is paintColor), and the channel the view shows on its own (channel_view).
    BrushMaterial paintMaterial;
    Channel channelView = Channel::BaseColor;
    // Vector displacement maps for the VDM brush (core/Vdm.h): the built-ins, then the studio and user
    // library's .sfvdm files and any loaded this session. `brushVdm` is the one the VDM brush stamps.
    std::vector<std::shared_ptr<const VectorDisplacement>> vdms = builtinVdms();
    std::shared_ptr<const VectorDisplacement> brushVdm = builtinVdms().front();
    // Adds or replaces (by name) a recipe and returns its index. Replacing with a
    // BrushSource::Session recipe keeps the existing brush's origin.
    int addBrush(BrushRecipe recipe);
    // Gaussian splats shown alongside the mesh (a capture used as reference or
    // source, or the sculpt converted for splat viewers). Not part of undo.
    SplatCloud splats;
    uint64_t splatsVersion = 0; // bump after editing `splats` so views re-upload
    void splatsChanged() { ++splatsVersion; }
    bool bvhDirty = false; // set after whole-mesh deformations (e.g. layer weights)
    // SubTool name of a mesh that came from ZBrush (GoZ), so sending it back updates that SubTool.
    std::string gozName;
    // QRemesh Guides of the active SubTool: curves QRemesh's edge loops follow (core/QRemeshGuides.h). Not part
    // of undo (Delete Last / Clear remove them); editing them sets `modified`.
    std::vector<QRemeshGuide> qremeshGuides;
    // The UV Editor's selection on the active SubTool (core/Uv.h); dropped when another SubTool is picked or the
    // triangles change (UvSelection::valid). Not part of undo.
    UvSelection uvSelection;

    void syncBvh() {
        if (bvhDirty) bvh.build(mesh);
        bvhDirty = false;
    }
    // Asset name derived from the document path (for exports).
    std::string assetName() const;

    // App-owned state saved in projects (camera, render, brush, symmetry, ...):
    // the app fills it before saving and applies it after opening; the core
    // keeps it as loaded so CLI and scripts round-trip it unchanged.
    Json projectSettings = Json::object();
    // Files the app keeps inside the project (the custom MatCap the view uses: "matcaps/<name>.png"), by name.
    // Saved under attachments/ and loaded back as they were; names are letters, digits, space . _ - and '/'.
    std::map<std::string, std::vector<uint8_t>> projectFiles;

    // Unsaved changes: any undoable edit (in any SubTool), splat edit or
    // `modified` since the last markClean() (save, open, new scene).
    bool dirty() const;
    void markClean();

    // --- SubTools. There is always at least one; `subtools[activeSubtool]` is the
    // active one (its data lives in mesh / bvh / undo above). Adding, removing,
    // reordering and merging SubTools is not undoable (as in ZBrush); it sets
    // `modified`.
    std::vector<SubTool> subtools; // one "Sculpt" SubTool to start with
    Document();
    int activeSubtool = 0;
    int subtoolCount() const { return int(subtools.size()); }
    SubTool& activeSubtoolInfo() { return subtools[size_t(activeSubtool)]; }
    const SubTool& activeSubtoolInfo() const { return subtools[size_t(activeSubtool)]; }
    // The mesh of SubTool `i`, active or not.
    Mesh& subtoolMesh(int i) { return i == activeSubtool ? mesh : subtools[size_t(i)].mesh; }
    const Mesh& subtoolMesh(int i) const { return i == activeSubtool ? mesh : subtools[size_t(i)].mesh; }
    // Makes SubTool `i` the one tools and commands edit.
    void selectSubtool(int i);
    // Inserts a SubTool at `at` (-1 = end of the list), made active when `select`; returns its index. `built`:
    // the mesh's BVH, built already (moved from; null = built here).
    int addSubtool(Mesh m, std::string name, bool select = true, int at = -1, BVH* built = nullptr);
    // Replaces the active SubTool's mesh outside undo (its history is cleared), e.g. after a merge.
    void replaceActiveMesh(Mesh m);
    // Removes SubTool `i` (never the last one); the active one moves to a neighbour if needed.
    bool removeSubtool(int i);
    // Moves SubTool `i` one place up (-1) or down (+1) in the list.
    void moveSubtool(int i, int direction);
    // Index of the SubTool called `name` (exact match, first one), or -1.
    int findSubtool(const std::string& name) const;
    // Index of the SubTool with this id, or -1.
    int subtoolIndex(uint32_t id) const;
    // Undoes or redoes a SubtoolStep: takes the SubTool out of the list into the step, or puts it back. False
    // when there was nothing to do (the SubTool was deleted since, or it's the active or the last one).
    bool applySubtoolStep(SubtoolStep& step);

    // The Extract preview waiting for Accept, if any (see ExtractPreview).
    ExtractPreview extractPreview;
    // True while the preview SubTool exists, its source is active and unchanged since.
    bool extractPreviewCurrent() const;
    // Removes the preview SubTool (if any); `staleOnly`: only when it's no longer current.
    void dropExtractPreview(bool staleOnly = false);

    // A name not used by any SubTool yet ("Eye", "Eye 2", ...).
    std::string uniqueSubtoolName(const std::string& wanted) const;
    // Bounds of every visible SubTool.
    AABB visibleBounds() const;

    // Replaces the whole scene with one SubTool holding `m` and clears the edit history (`built`: as addSubtool).
    void setMesh(Mesh m, std::string name = "Sculpt", BVH* built = nullptr);
    // Replaces the mesh as an undoable topology change. A result without the old mesh's subdivision levels (a
    // remesh, decimate, ...) leaves a `notice` saying the levels were removed; undo brings them back.
    void replaceMeshUndoable(Mesh m, std::string label);

    // A note for the status line from the last operation, e.g. that subdivision levels were removed.
    // CommandRegistry::execute adds it to the command's message; other callers take it with takeNotice().
    std::string notice;
    std::string takeNotice() { return std::exchange(notice, {}); }

private:
    uint64_t cleanRevision_ = 0, cleanSplatsVersion_ = 0;
    uint32_t nextSubtoolId_ = 2;
};

} // namespace sf
