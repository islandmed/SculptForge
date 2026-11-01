// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Material Recipes: a stack of Fill layers (core/LayerStack.h) saved as a small JSON file (.sfmaterial) that can
// be laid onto any Part with a texture. The built-in recipes are data files in assets/materials, embedded in the
// program: Worn Metal, Painted Edges, Dirt and Dust, Rusted Iron. Because their masks read the Part's Mesh Maps,
// the same recipe wears each model where it would wear: edges, crevices, surfaces facing up.
//
//   { "format": "sculptforge-material-recipe", "version": 1, "id": "worn_metal", "name": "Worn Metal",
//     "description": "...", "layers": [ { "name": "Steel", "fill": <fillLayerJson> }, ... ] }   // bottom to top
#pragma once

#include "core/Json.h"

#include <string>
#include <vector>

namespace sf {

class CommandRegistry;
struct Document;
struct PaintTexture;

constexpr const char* kMaterialRecipeExtension = ".sfmaterial";

struct MaterialRecipe {
    std::string id, name, description;
    Json layers; // array of {name, fill}
};

// The built-in recipes, in order.
const std::vector<MaterialRecipe>& builtinMaterialRecipes();
// By id or name (any case); null when unknown.
const MaterialRecipe* findMaterialRecipe(const std::string& text);
bool parseMaterialRecipe(const std::string& text, MaterialRecipe& out, std::string& error);
// The recipe of a texture's Fill layers (bottom to top), as file text.
std::string materialRecipeText(const PaintTexture& tex, const std::string& name);
// Adds the recipe's Fill layers on top of the active Part's layers (one undo step), turning on the channels it
// needs, and evaluates them (baking Mesh Maps first, or in the background in the app).
bool applyMaterialRecipe(Document& doc, const MaterialRecipe& recipe, std::string& message);

// material_recipe (apply / list / save).
void registerMaterialRecipeCommands(CommandRegistry& registry);

} // namespace sf
