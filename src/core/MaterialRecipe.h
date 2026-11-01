// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Material Recipes and Mask Recipes (PBR texturing phase 3; scaffolding only). Our own .sfmaterial files (ZIP +
// JSON) describing a layer stack, with original procedural recipes generated in code for a LightBox "Surfaces"
// tab. Today a brush material (core/TextureChannels.h BrushMaterial, paint_material) is the closest thing.
#pragma once

namespace sf {

constexpr const char* kMaterialRecipeExtension = ".sfmaterial";

} // namespace sf
