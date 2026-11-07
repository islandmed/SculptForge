// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/BVH.h"
#include "core/BrushAlpha.h"
#include "core/BrushRecipe.h"
#include "core/EdgeSelect.h"
#include "core/Json.h"
#include "core/Mesh.h"
#include "core/QRemeshGuides.h"
#include "core/Sculpt.h"
#include "core/Splats.h"
#include "core/Stencil.h"
#include "core/TextureChannels.h"
#include "core/Undo.h"
#include "core/Uv.h"
#include "core/Vdm.h"

#include <map>
#include <string>
#include <utility>

namespace sf {

// How a Part is shaded in the view: as the scene says (the Material palette's Flat shading), or always smooth
// or always flat (faceted: every polygon's own face normal). Display only; saved in projects.
enum class Shading : uint8_t { Default, Smooth, Flat };
const char* shadingName(Shading s);           // "default", "smooth", "flat"
bool parseShading(const std::string& name, Shading& out);

// What a Part is made of: its mesh, attributes, layers and undo history. Shared (reference counted) by the Parts
// of one link group (Linked Duplicate, #94): sculpting, painting or masking any of them edits this one mesh, and
// they share its history. `key` names it for the renderer (one GPU mesh per PartData): the id of the Part it
// was made for.
struct PartData {
    Mesh mesh;
    BVH bvh;
    UndoStack undo;
    int activeLayer = -1;
    std::string bridgeName;
    bool bvhDirty = false;
    uint64_t cleanRevision = 0;
    std::vector<QRemeshGuide> qremeshGuides; // QRemesh Guides (core/QRemeshGuides.h)
    uint32_t key = 0;
};

// One object in the scene, like a classic Part: a body, the eyes, the
// teeth, a sword. Each keeps its own mesh, attributes, layers and undo
// history (its PartData, shared with its linked copies). The active Part is "checked out" into Document::mesh /
// bvh / undo, so every tool and command keeps working on the active one; its PartData holds only the key until
// a Part with other data is selected.
struct Part {
    uint32_t id = 0; // stable for the session (renderer slots, UI selection)
    std::string name;
    bool visible = true;
    Shading shading = Shading::Default; // per-Part override of the scene's shading
    uint32_t folder = 0;                // the PartFolder it's in (its id), 0 = none
    // Where the mesh is in the scene: world = transform * mesh position. Identity unless the Part is a linked copy
    // moved on its own (or was one: Make Unique keeps it). A similarity (rotation, translation, uniform scale).
    glm::mat4 transform{1.0f};
    std::shared_ptr<PartData> data = std::make_shared<PartData>();
    Part() = default;
    Part(Part&&) = default;
    Part& operator=(Part&&) = default;
    Part(const Part&) = delete; // (a copy would share the data: a link; Document::linkedDuplicate makes those)
    Part& operator=(const Part&) = delete;
    bool moved() const { return transform != glm::mat4(1.0f); }
};

// A folder in the Parts list (#96): a name the list groups its Parts under, so they hide, show and move
// together. Display and organisation only; saved in projects (optional keys, older versions ignore them).
struct PartFolder {
    uint32_t id = 0; // stable for the session; Part::folder points here
    std::string name;
    bool collapsed = false; // the list shows only its header
};

// An undo step that added Part `id` at list position `at` (UndoRecord::part): `stashed` holds the Part
// while it's undone. With `isTransform` it instead moved Part `id` (a linked copy): `transform` holds the
// other transform (undo and redo swap it with the Part's).
struct PartStep {
    uint32_t id = 0;
    int at = 0;
    std::unique_ptr<Part> stashed;
    bool isTransform = false;
    glm::mat4 transform{1.0f};
};

// Extract's preview (core/ExtractMirror.h): Part `id`, made from the mask of Part `source` as it was at
// that Part's undo revision `revision`, shown until Accept turns it into an undoable step. id 0 = none.
struct ExtractPreview {
    uint32_t id = 0, source = 0;
    uint64_t revision = 0;
};

// Everything that makes up the open scene: the mesh, its acceleration
// structure and edit history. UI-independent so it can be driven by tests,
// scripts or an AI assistant through the command registry.
struct Document {
    // The active Part's working data (see Part).
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
    Stencil stencil;       // the image over the view the brushes work through (#155; stencil_load, core/Stencil.h)
    glm::vec3 paintColor{0.22f, 0.22f, 0.22f}; // current vertex paint colour (linear RGB; see set_color); a mid grey
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
    // Part name of a mesh that came  (Bridge), so sending it back updates that Part.
    std::string bridgeName;
    // The unit-less file (STL, OBJ, PLY; core/Units.h) the scene was opened from: numbers per metre in it (1000
    // for millimetres) and its format ("stl", "obj", "ply"). OBJ and PLY exports write in that unit, STL exports
    // too when it was an STL (else millimetres), so a part comes out at the size and numbers it went in with.
    // 0 / empty = no such file: each format's default (OBJ, PLY metres; STL millimetres).
    double unitlessScale = 0.0;
    std::string unitlessFormat;
    // File units per metre for an export to `format` ("stl", "obj", "ply") under the rules above.
    double unitlessExportScale(const std::string& format) const;
    // QRemesh Guides of the active Part: curves QRemesh's edge loops follow (core/QRemeshGuides.h). Not part
    // of undo (Delete Last / Clear remove them); editing them sets `modified`.
    std::vector<QRemeshGuide> qremeshGuides;
    // The UV Editor's selection on the active Part (core/Uv.h); dropped when another Part is picked or the
    // triangles change (UvSelection::valid). Not part of undo.
    UvSelection uvSelection;
    // Edges selected on the active Part's base level (core/EdgeSelect.h, Geometry > Edges); dropped like uvSelection.
    EdgeSelection edgeSelection;

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
    // Files the app keeps inside the project (the custom Lit Sphere the view uses: "lit spheres/<name>.png"), by name.
    // Saved under attachments/ and loaded back as they were; names are letters, digits, space . _ - and '/'.
    std::map<std::string, std::vector<uint8_t>> projectFiles;

    // Unsaved changes: any undoable edit (in any Part), splat edit or
    // `modified` since the last markClean() (save, open, new scene).
    bool dirty() const;
    void markClean();

    // --- Parts. There is always at least one; `parts[activePart]` is the
    // active one (its data lives in mesh / bvh / undo above). Adding, removing,
    // reordering and merging Parts is not undoable; it sets
    // `modified`.
    std::vector<Part> parts; // one "Sculpt" Part to start with
    Document();
    int activePart = 0;
    int partCount() const { return int(parts.size()); }
    Part& activePartInfo() { return parts[size_t(activePart)]; }
    const Part& activePartInfo() const { return parts[size_t(activePart)]; }
    // True when Part `i`'s data is the one checked out (the active Part, or a Part linked to it).
    bool checkedOut(int i) const { return parts[size_t(i)].data == parts[size_t(activePart)].data; }
    // The mesh, BVH (built if stale) and history of Part `i`, active or not (a linked copy's are its group's).
    Mesh& partMesh(int i) { return checkedOut(i) ? mesh : parts[size_t(i)].data->mesh; }
    const Mesh& partMesh(int i) const { return checkedOut(i) ? mesh : parts[size_t(i)].data->mesh; }
    BVH& partBvh(int i);
    UndoStack& partUndo(int i) { return checkedOut(i) ? undo : parts[size_t(i)].data->undo; }
    const UndoStack& partUndo(int i) const { return checkedOut(i) ? undo : parts[size_t(i)].data->undo; }
    // The renderer's key for Part `i`'s mesh (shared by linked copies: one GPU mesh).
    uint32_t meshKey(int i) const { return parts[size_t(i)].data->key; }
    // --- Linked Duplicates (#94): Parts sharing one mesh, each with its own transform.
    // How many Parts share Part `i`'s mesh (1 = not linked).
    int linkCount(int i) const;
    // Adds a Part sharing Part `i`'s mesh and history at `at` (-1 = end), placed by `transform`; not undoable, not
    // selected. Returns its index.
    int addLink(int i, std::string name, const glm::mat4& transform, int at = -1);
    // Adds a Part sharing Part `i`'s mesh and history, after it, offset by `offset` (world units), and makes it
    // active; one undoable step in the shared history. Returns its index.
    int linkedDuplicate(int i, const glm::vec3& offset);
    // Gives Part `i` its own copy of the mesh (a fresh history), breaking its link; it keeps its transform.
    // False when it isn't linked.
    bool makeUnique(int i);
    // Sets Part `i`'s transform as an undoable step in its history (`label`).
    void setPartTransform(int i, const glm::mat4& transform, const std::string& label);
    // Makes Part `i` the one tools and commands edit.
    void selectPart(int i);
    // Inserts a Part at `at` (-1 = end of the list), made active when `select`; returns its index. `built`:
    // the mesh's BVH, built already (moved from; null = built here).
    int addPart(Mesh m, std::string name, bool select = true, int at = -1, BVH* built = nullptr);
    // Replaces the active Part's mesh outside undo (its history is cleared), e.g. after a merge.
    void replaceActiveMesh(Mesh m);
    // Removes Part `i` (never the last one); the active one moves to a neighbour if needed.
    bool removePart(int i);
    // Moves Part `i` one place up (-1) or down (+1) in the list.
    void movePart(int i, int direction);
    // Index of the Part called `name` (exact match, first one), or -1.
    int findPart(const std::string& name) const;
    // Index of the Part with this id, or -1.
    int partIndex(uint32_t id) const;
    // Undoes or redoes a PartStep: takes the Part out of the list into the step, or puts it back. False
    // when there was nothing to do (the Part was deleted since, or it's the active or the last one).
    bool applyPartStep(PartStep& step);

    // The Extract preview waiting for Accept, if any (see ExtractPreview).
    ExtractPreview extractPreview;
    // True while the preview Part exists, its source is active and unchanged since.
    bool extractPreviewCurrent() const;
    // Removes the preview Part (if any); `staleOnly`: only when it's no longer current.
    void dropExtractPreview(bool staleOnly = false);

    // --- Part folders (#96). Not undoable either; changes set `modified`.
    std::vector<PartFolder> partFolders;
    // Adds a folder (its name made unique) and returns its id.
    uint32_t addPartFolder(const std::string& name);
    // Index of the folder with this id / name (exact match), or -1.
    int partFolderIndex(uint32_t id) const;
    int findPartFolder(const std::string& name) const;
    // Removes a folder; its Parts stay, outside any folder.
    void removePartFolder(uint32_t id);
    // Puts Part `i` into folder `id` (0 = out of any folder). Joining a folder moves the Part to just below the
    // folder's last Part, so a folder's Parts stay together in the list; returns the Part's new index.
    int setPartFolder(int i, uint32_t id);
    // A folder name not used yet ("Folder", "Folder 2", ...).
    std::string uniqueFolderName(const std::string& wanted) const;

    // A name not used by any Part yet ("Eye", "Eye 2", ...).
    std::string uniquePartName(const std::string& wanted) const;
    // Bounds of every visible Part (in the scene: transforms applied).
    AABB visibleBounds() const;
    // Bounds of Part `i` in the scene (its transform applied).
    AABB partBounds(int i) const;

    // Replaces the whole scene with one Part holding `m` and clears the edit history (`built`: as addPart).
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
    uint32_t nextPartId_ = 2;
    uint32_t nextFolderId_ = 1;
    uint32_t newPartId(Part& p); // gives `p` the next id (and its data that key when it has none)
};

} // namespace sf
