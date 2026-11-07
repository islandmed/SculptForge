// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Managing sculpt layers (Tool > Layers; the layers themselves are Mesh::layers, core/Mesh.h): reorder, rename,
// Merge Down, Invert, weights from -1 to 1, the eye, one Strength for all, Bake (one or every shown layer) and
// Clear All. Each is one undo step and works on every subdivision level: settings (weight, eye, Strength) are
// shared by all levels; the rest changes every level exactly (subdivForEachLevel), so stepping levels afterwards
// shows the same result as before the change, with the change.
#pragma once

#include "core/Undo.h"

#include <string>
#include <vector>

namespace sf {

class CommandRegistry;
struct Document;

// Undoable operations on the active Part. Each returns false with `message` saying why when it can't run; on
// success `message` is for the status line.
bool moveSculptLayer(Document& doc, int from, int to, std::string& message);
// Moves several layers together (a multi-selection), keeping their order among themselves. `where`: "to" (the
// first of them lands at index `to`), "up" / "down" (one place each, as far as they can) or "top" / "bottom".
// `moved` (optional) gets their new indices. The active layer follows its layer.
bool moveSculptLayers(Document& doc, std::vector<int> layers, const std::string& where, int to, std::string& message,
                      std::vector<int>* moved = nullptr);
// A copy of the layer just above it ("Smile copy"), made active.
bool duplicateSculptLayer(Document& doc, int index, std::string& message);
bool renameSculptLayer(Document& doc, int index, const std::string& name, std::string& message);
bool mergeSculptLayerDown(Document& doc, int index, std::string& message);
bool invertSculptLayer(Document& doc, int index, std::string& message);
bool bakeSculptLayer(Document& doc, int index, std::string& message); // index -1: every shown layer
bool clearSculptLayers(Document& doc, std::string& message);
bool removeSculptLayer(Document& doc, int index, std::string& message);
// Settings: one undo step each, holding only the settings (LayerSettingsUndo).
bool setSculptLayerWeight(Document& doc, int index, float weight, std::string& message);
bool setSculptLayerVisible(Document& doc, int index, bool visible, std::string& message);
bool setSculptLayerStrength(Document& doc, float strength, std::string& message);
// For a drag (a weight or the Strength slider) that changed the mesh live: one undo step back to `before`.
void pushLayerSettingsUndo(Document& doc, LayerSettingsUndo before, std::string label);

// layer_add, layer_remove, layer_set_active, layer_set_weight, layer_set_visible, layer_strength, layer_move,
// layer_duplicate, layer_rename, layer_merge_down, layer_invert, layer_bake, layer_clear, layer_info.
void registerSculptLayerCommands(CommandRegistry& registry);

} // namespace sf
