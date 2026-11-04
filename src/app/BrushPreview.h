// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The brush previews' sculpting (no GL): every built-in brush and recipe leaves its mark on the same clay ball with
// the code real strokes use (strokePath). ThumbnailCache renders the result; tests/test_brush_previews.cpp checks
// the shapes.
#pragma once

#include "core/BrushRecipe.h"
#include "core/Mesh.h"

#include <memory>

namespace sf {

class Document;

// Every brush preview is the same clay ball, sculpted on its front and shot from the same camera with the
// same material, so the shelf and the pickers read as one icon set.
constexpr float kBallRadius = 0.9f;
constexpr int kBallDetail = 6; // ~82k triangles: fine enough for crisp strokes at preview size

// One mesh of every visible Part of `doc` (positions, triangles and vertex paint), for a preview; null when empty.
std::unique_ptr<Mesh> sceneMesh(Document& doc);

// The preview of a brush: a recipe when given, else the built-in `type` (a BrushType, or kPaintBrush). Strokes look
// along -z at the front of the ball.
std::unique_ptr<Mesh> sculptBrushPreview(const BrushRecipe* recipe, int type);

} // namespace sf
