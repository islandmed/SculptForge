// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/Image.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace sf {

class CommandRegistry;

// Grayscale stamp that shapes a brush's falloff ("alpha"). An empty
// alpha is the plain round smoothstep falloff, so brushes behave exactly as
// without one. Built-ins: Round (empty), Square, Noise and twelve patterns made
// in code (Pores, Cracks, Scales, Rivets, Stitches, Fabric Weave, Bark, Rock
// Strata, Wrinkles, Skin Folds, Hair Strands, Dot Grid; #43; Cobblestone, Leather
// Grain, Hex Plates, Knit, Bolt, Chips for the brush packs; the wood set; the Creature Skin set, core/CreatureSkin.h,
// made at 2048 px when a brush first needs one); anything else comes from an image's luminance (transparent pixels
// count as black).
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
    // What a dab uses: sample() x alphaFalloff (Falloff, -1..1; #60). Empty = brushFalloff with `focal`.
    float stamp(float u, float v, float focal) const;

    static BrushAlpha round();
    static BrushAlpha square();
    static BrushAlpha noise(uint32_t seed = 1);
    // Every built-in in the pickers' order (Round, Square, Noise, then the patterns), generated on first use.
    static const std::vector<BrushAlpha>& builtins();
    // A built-in by name, ignoring case, spaces, '_' and '-' ("none" = Round); false for unknown names. A Creature
    // Skin alpha comes at full size (made on the first call, then kept); builtins() holds only their tiles.
    static bool builtin(const std::string& name, BrushAlpha& out);
    // The pickers' copy (a Creature Skin alpha's 128 px tile; the others as made): for previews, never for strokes.
    static bool builtinPreview(const std::string& name, BrushAlpha& out);
    // A built-in's own spelling ("fabric_weave" -> "Fabric Weave"), empty for unknown names; makes nothing.
    static std::string builtinName(const std::string& name);
    // Starts making a Creature Skin alpha's full size in the background (once; a no-op for other names), so the pick
    // that follows finds it made: the pickers call it while the pointer rests on its tile.
    static void prefetch(const std::string& name);
    // The app's mode (off for scripts and tests): builtin() gives a Creature Skin alpha that isn't made yet as its tile
    // at once and makes the full one in the background; finishCreature swaps it in: when made (wait = false, each frame)
    // or now (wait = true, at a stroke's start, so no stroke uses a tile). True when `a` changed.
    static void setDeferCreature(bool defer);
    // The kept full-size Creature Skin alpha `a` is a copy of (untiled), to share instead of copying 16 MB (a stroke's
    // snapshot); null for anything else.
    static std::shared_ptr<const BrushAlpha> shared(const BrushAlpha& a);
    static bool finishCreature(BrushAlpha& a, bool wait);
    // The set a built-in belongs to in the lists: "Creature Skin", or empty for the classic ones.
    static std::string builtinGroup(const std::string& name);
    // A built-in pattern made at `size` x `size` (the Stencil's, which spans the view: 128 shows its pixels there).
    static bool builtinAt(const std::string& name, int size, BrushAlpha& out);
    // One line on what a built-in is for (empty for unknown names), and all their names for messages.
    static std::string builtinHelp(const std::string& name);
    static std::string builtinNames();
    // Luminance x alpha, downscaled to at most 512 px; `invert` swaps black and white.
    static BrushAlpha fromImage(const Image& image, std::string name, bool invert = false);
    // An image file as an alpha (path and inverted set), box-filtered to at most `maxSide` px: PNG (16-bit kept at
    // full precision), OpenEXR (half or float; values outside 0..1 are stretched to the image's own range), and JPG,
    // BMP, TGA, PSD, GIF. False with `error` when the file can't be read.
    static bool fromFile(const std::string& path, std::string name, bool invert, BrushAlpha& out, std::string& error,
                         int maxSide = 2048);
};

// Registers brush_set_alpha (and future brush-option commands) with the registry.
void registerBrushCommands(CommandRegistry& registry);

} // namespace sf
