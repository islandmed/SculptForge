// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Sculpt layer stack edits (Tool > Layers): reorder, rename, duplicate, merge down, invert, show / hide, bake into
// the base, delete. Each works on one mesh (the working mesh or one subdivision level: no topology or normals
// needed) and re-evaluates its positions; the layer_* commands run them on every level (subdivApplyLayerOp) as one
// undo step. The panel lists layer 0 on top: "up" is the index before, "down" the next one.
#pragma once

#include "core/Mesh.h"

#include <string>
#include <vector>

namespace sf {

class CommandRegistry;

// `wanted`, or "wanted 2", "wanted 3"... so no other layer (than `except`) has that name. Levels match layers by
// name, so names stay unique.
std::string uniqueSculptLayerName(const Mesh& m, const std::string& wanted, int except = -1);

void moveSculptLayer(Mesh& m, int from, int to);
void renameSculptLayer(Mesh& m, int i, const std::string& name);
// A copy of layer i (same offsets and settings) right below it (index i + 1), named `name`.
void duplicateSculptLayer(Mesh& m, int i, const std::string& name);
// Layer i into the one below it (i + 1), which keeps its name and shows both as they showed (weight 1, shown);
// layer i is removed, so the merged layer is then layer i.
void mergeSculptLayerDown(Mesh& m, int i);
// The layer's offsets the opposite way (its weight is kept).
void invertSculptLayer(Mesh& m, int i);
// Adds the layers marked in `which` to the base as they show now (weight, shown, strength for all layers), then
// removes them; the shape doesn't change.
void bakeSculptLayers(Mesh& m, const std::vector<bool>& which);
// Removes the marked layers and their offsets.
void removeSculptLayers(Mesh& m, const std::vector<bool>& which);

// layer_move, layer_rename, layer_set, layer_duplicate, layer_merge_down, layer_invert, layer_bake,
// layer_remove_many, layer_strength.
void registerSculptLayerCommands(CommandRegistry& registry);

} // namespace sf
