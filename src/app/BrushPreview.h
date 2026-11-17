// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The brush previews' sculpting (no GL): every built-in brush and recipe leaves its mark on the same clay ball with
// the code real strokes use (strokePath). ThumbnailCache renders the result; tests/test_brush_previews.cpp checks
// the shapes.
#pragma once

#include "core/AlphaOptions.h"
#include "core/BrushAlpha.h"
#include "core/BrushRecipe.h"
#include "core/BrushSettings.h"
#include "core/Mesh.h"
#include "core/Sculpt.h"
#include "core/Stroke.h"

#include <memory>

namespace sf {

class Document;

// Every brush preview is the same clay ball, sculpted on its front and shot from the same camera with the
// same material, so the shelf and the pickers read as one icon set.
constexpr float kBallRadius = 0.9f;
constexpr int kBallDetail = 6; // ~82k triangles: fine enough for crisp strokes at preview size

// One mesh of every visible Part of `doc` (positions, triangles and vertex paint), for a preview; null when empty.
std::unique_ptr<Mesh> sceneMesh(Document& doc);

// The alpha a brush is set to and how it strokes (#208): its thumbnail shows that alpha pressed into the ball in the
// brush's own stroke type, so brushes that differ only by alpha tell apart at a glance.
struct AlphaLook {
    BrushAlpha alpha;
    StrokeType stroke = StrokeType::Dots;
    AlphaOptions options;
};
// True when the brush's settings carry an alpha other than the plain round one.
bool hasAlphaLook(const BrushToolSettings& s);
// The look of those settings: a built-in alpha by name, or an image file (read here, so call it off the main
// thread). False when there is no alpha, or the file can't be read any more.
bool makeAlphaLook(const BrushToolSettings& s, AlphaLook& out);
// Whether `type`'s dabs use the brush alpha (Move, the grab brushes and Smooth do not).
bool brushTakesAlpha(BrushType type);

// The preview of a brush: a recipe when given, else the built-in `type` (a BrushType, or kPaintBrush). Strokes look
// along -z at the front of the ball. With a `look` (a recipe, or a built-in brush that takes an alpha) the mark is
// the alpha pressed in with the look's stroke type instead.
std::unique_ptr<Mesh> sculptBrushPreview(const BrushRecipe* recipe, int type, const AlphaLook* look = nullptr);

} // namespace sf
