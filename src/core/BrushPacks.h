// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The built-in brush packs: seventy tuned recipes in seven named packs (Clay & Form, Hard Surface, Organic & Skin,
// Cloth & Folds, Damage & Wear, Ornament, Wood), made only from SculptForge's own recipe engine and procedural alphas.
// They ship with the app as built-in brushes (BrushSource::Builtin) with origin.pack set to the pack's name, so
// the brush lists can show and sort them by pack. Guide: docs/guide/brush-packs.md.
#pragma once

#include "core/BrushRecipe.h"

#include <string>
#include <vector>

namespace sf {

// The pack names, in the order the guide and the lists show them.
const std::vector<std::string>& builtinBrushPacks();
// One line on what a pack is for (empty for unknown names).
std::string builtinBrushPackHelp(const std::string& pack);
// Every pack brush (clamped, origin.pack set; origin.source left for builtinBrushRecipes to set).
std::vector<BrushRecipe> builtinPackRecipes();

} // namespace sf
