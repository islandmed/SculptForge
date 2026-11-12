// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The scene's Boolean: every shown Part whose Boolean (Part::boolean) is Add, Subtract or Intersect, combined in list
// order (core/MeshBoolean.h). The first one is the base; each next one is added to, subtracted from or intersected
// with what the ones above it made. The view previews the result (app/BooleanPreview.cpp); Make Boolean Mesh writes
// it into a new Part (the sources stay as they are, hidden) as one undo step.
#pragma once

#include "core/Commands.h"
#include "core/Document.h"
#include "core/MeshBoolean.h"

#include <string>
#include <vector>

namespace sf {

struct BooleanSceneOptions : BooleanOptions {
    bool keepGroups = true; // each source keeps its face groups (apart from the others'); off: one group per source
    bool cutGroup = true;   // the faces a Subtract or Intersect Part leaves on the result get a new group per Part
};

// One Part's mesh as it takes part: where it is in the scene, with the face groups the options give it.
struct BooleanOperand {
    Mesh mesh;
    PartBoolean mode = PartBoolean::Add;
    uint32_t partId = 0;
    std::string name;
};

// The Parts that take part, in list order: shown, Boolean not None, with triangles.
std::vector<int> booleanPartIndices(const Document& doc);

// Copies of those Parts' meshes for combineOperands (positions and triangles, plus face groups, vertex paint and UVs
// unless `light`: the live preview needs only the shape). Called on the thread that owns the document.
std::vector<BooleanOperand> booleanOperands(const Document& doc, bool light = false);

// Combines the operands in order. False with `error` for fewer than two, or when a step fails (named after its Part).
// `info`: the last step's, with `voxel` set when any step used the voxel path.
bool combineOperands(std::vector<BooleanOperand> operands, const BooleanSceneOptions& options, Mesh& out,
                     std::string& error, BooleanInfo* info = nullptr);

// Make Boolean Mesh: the scene's Boolean as a new Part below the last source, made active, its sources hidden (one
// undo step). Returns the status line.
CommandResult makeBooleanMesh(Document& doc, const BooleanSceneOptions& options);

// part_boolean, boolean_parts, make_boolean_mesh.
void registerBooleanCommands(CommandRegistry& registry);

} // namespace sf
