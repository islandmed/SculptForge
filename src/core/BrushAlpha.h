// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/Image.h"

#include <cstdint>
#include <string>
#include <vector>

namespace sf {

class CommandRegistry;

// Grayscale stamp that shapes a brush's falloff (ZBrush "alpha"). An empty
// alpha is the plain round smoothstep falloff, so brushes behave exactly as
// without one. Built-ins: Round (empty), Square, Noise and twelve patterns made
// in code (Pores, Cracks, Scales, Rivets, Stitches, Fabric Weave, Bark, Rock
// Strata, Wrinkles, Skin Folds, Hair Strands, Dot Grid; #43); anything else comes
// from an image's luminance (transparent pixels count as black).
struct BrushAlpha {
    std::string name = "Round";
    int width = 0, height = 0;
    std::vector<float> values; // 0..1, row-major, row 0 at the top
    // An image alpha's file and whether it was inverted (brush_set_alpha path=...), so a brush's settings can
    // bring it back (core/BrushSettings.h). Empty for the built-ins.
    std::string path;
    bool inverted = false;
    // Tile (#76, core/AlphaOptions.h): the alpha repeated tileX x tileY times inside the stamp (1 x 1: once).
    int tileX = 1, tileY = 1;
    bool tiled() const { return tileX > 1 || tileY > 1; }

    bool empty() const { return values.empty(); }
    // u, v in brush-local -1..1 (v points up); 0 outside the stamp.
    float sample(float u, float v) const;
    // What a dab uses: sample() x alphaFalloff (Focal Shift, -1..1; #60). Empty = brushFalloff with `focal`.
    float stamp(float u, float v, float focal) const;

    static BrushAlpha round();
    static BrushAlpha square();
    static BrushAlpha noise(uint32_t seed = 1);
    // Every built-in in the pickers' order (Round, Square, Noise, then the patterns), generated on first use.
    static const std::vector<BrushAlpha>& builtins();
    // A built-in by name, ignoring case, spaces, '_' and '-' ("none" = Round); false for unknown names.
    static bool builtin(const std::string& name, BrushAlpha& out);
    // One line on what a built-in is for (empty for unknown names), and all their names for messages.
    static std::string builtinHelp(const std::string& name);
    static std::string builtinNames();
    // Luminance x alpha, downscaled to at most 512 px; `invert` swaps black and white.
    static BrushAlpha fromImage(const Image& image, std::string name, bool invert = false);
};

// Registers brush_set_alpha (and future brush-option commands) with the registry.
void registerBrushCommands(CommandRegistry& registry);

} // namespace sf
