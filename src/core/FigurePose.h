// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Figure Pose (docs/design/pose-all.md, request by @ABHITAR): pose every visible Part together through one light
// Pose Mesh, then apply the pose to the full-resolution Parts as one undo step.
//
// Begin merges the visible Parts, each decimated to its share of a triangle budget, into a temporary Part (the
// Pose Mesh, one face group per source Part) and hides the others. The Pose Mesh is posed with the Transform
// gizmo and the masking tools; each transform step keeps its transform and weights (UndoRecord::transformOp), so
// the pose is the Pose Mesh's undo history up to its current step. Apply binds every vertex of each source Part
// to the closest point on that Part's own rest-pose Pose Mesh triangles, interpolates each step's weights there,
// and runs the same transforms on the Part (a TransformSession per step): full-resolution vertices take the user's
// transforms at interpolated weights, they are never placed by the deformed Pose Mesh (design §7). Sculpt layers,
// higher subdivision levels and micro levels follow through their own local frames. Every changed Part gets one
// undo step, all sharing an UndoRecord::group id, which the undo and redo commands take as one step.
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <glm/glm.hpp>

namespace sf {

class CommandRegistry;
struct Document;

// A source Part and its range in the Pose Mesh.
struct FigurePoseSource {
    uint32_t part = 0; // Part::id
    uint32_t firstVertex = 0, vertexCount = 0;
    uint32_t firstTriangle = 0, triangleCount = 0;
    uint64_t revision = 0; // the Part's undo revision when the pose began (edited since = left out of Apply)
};

// The pose in progress (Document::figurePose).
struct FigurePoseState {
    uint32_t poseMesh = 0;  // Part::id of the Pose Mesh
    uint32_t returnTo = 0;  // the Part that was active before Begin
    std::vector<FigurePoseSource> sources;
    std::vector<glm::vec3> rest;                       // Pose Mesh positions at Begin
    std::vector<std::pair<uint32_t, bool>> visibility; // every Part's visibility before Begin
    double beginSeconds = 0.0;
};

constexpr size_t kFigurePoseDefaultTriangles = 300000;
constexpr size_t kFigurePoseMinTriangles = 50000, kFigurePoseMaxTriangles = 2000000;
constexpr size_t kFigurePosePartFloor = 2000; // the least any Part gets, so eyes and teeth keep their shape

struct FigurePoseResult {
    bool ok = false;
    std::string message;
    int parts = 0;         // Parts posed
    size_t vertices = 0;   // full-resolution vertices moved
    int steps = 0;         // transform steps replayed
    double bindSeconds = 0.0, replaySeconds = 0.0;
};

// True while a pose is in progress (its Pose Mesh still exists).
bool figurePoseActive(const Document& doc);
// True while a pose is in progress and the Pose Mesh is the active Part (its transform steps keep their op).
bool figurePoseEditing(const Document& doc);
// Builds the Pose Mesh from the visible Parts (`triangles`: the budget) and makes it the active Part.
FigurePoseResult figurePoseBegin(Document& doc, size_t triangles = kFigurePoseDefaultTriangles);
// Applies the pose to the source Parts (one grouped undo step), removes the Pose Mesh and shows the Parts again.
FigurePoseResult figurePoseApply(Document& doc);
// Removes the Pose Mesh and shows the Parts again; nothing changes.
void figurePoseCancel(Document& doc);

// After the undo / redo command applied `group`'s step on the active Part: the same for every other Part whose
// next step to undo (redo) has that id. Returns how many other Parts took it.
int undoGroupOnOtherParts(Document& doc, uint64_t group, bool isUndo);

// figure_pose_begin, figure_pose_apply, figure_pose_cancel, figure_pose_info.
void registerFigurePoseCommands(CommandRegistry& registry);

} // namespace sf
