// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Fill layers: non-destructive texture layers that lay a material (colour and any material channels,
// core/TextureChannels.h) over the whole Part through a mask built from a stack of mask effects, evaluated from the
// Part's Mesh Maps (core/MeshMaps.h). They sit in the same layer stack as paint layers (PaintTexture::layers):
// a Fill layer is a TextureLayer with `fill` set, whose colour (`rgba`, coverage in alpha) and channel images are
// the evaluation's output, so compositing, the viewport, export and undo treat it like any layer. Its settings
// change at any time and the layer is evaluated again.
//
// The mask starts at 1 and each effect, top to bottom, combines its value with it (blend and opacity):
//   - Generator: Edge Wear (curvature: convex edges), Dirt (occlusion and creases), Gradient (along a direction in
//     the Part's box), Thickness (thin parts), Light Direction (surfaces facing a direction, e.g. dust on top),
//     each with a level, a softness and a noise break-up in object space;
//   - Noise: fractal noise in object space (from the position map), so it is seamless across UV seams;
//   - Paint: the mask painted on the layer: with a Fill layer active the Paint brush paints its mask
//     (TextureLayer::mask, white adds, Alt removes), and undo works as for any stroke;
//   - Levels (input / output range and gamma), Blur (a radius in texels, across covered texels only), Invert,
//     and Fill (a constant).
//
// Evaluation is per 64 x 64 tile on every core, so a change re-evaluates only the tiles it reaches: Mesh Maps
// updated after sculpting (live), a painted mask (updateFillLayers, once a frame), or a setting (every tile).
#pragma once

#include "core/TextureChannels.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace sf {

class CommandRegistry;
class Json;
struct Document;
class Mesh;
struct PaintTexture;
struct MeshMapSet;

enum class LayerKind : uint8_t { Paint, Fill, Folder, Filter };
enum class MaskEffectKind : uint8_t { Generator, Noise, Paint, Levels, Blur, Invert, Fill };
constexpr int kMaskEffectKindCount = 7;
enum class GeneratorKind : uint8_t { EdgeWear, Dirt, Gradient, Thickness, LightDirection };
constexpr int kGeneratorKindCount = 5;
enum class MaskBlend : uint8_t { Replace, Multiply, Add, Subtract, Min, Max, Screen };
constexpr int kMaskBlendCount = 7;

const char* maskEffectId(MaskEffectKind k);  // "generator", "noise", "paint", "levels", "blur", "invert", "fill"
const char* maskEffectLabel(MaskEffectKind k);
const char* generatorId(GeneratorKind g);    // "edge_wear", "dirt", "gradient", "thickness", "light_direction"
const char* generatorLabel(GeneratorKind g);
const char* maskBlendId(MaskBlend b);        // "replace", "multiply", "add", "subtract", "min", "max", "screen"
bool parseMaskEffect(const std::string& text, MaskEffectKind& out);
bool parseGenerator(const std::string& text, GeneratorKind& out);
bool parseMaskBlend(const std::string& text, MaskBlend& out);

struct MaskEffect {
    MaskEffectKind kind = MaskEffectKind::Generator;
    MaskBlend blend = MaskBlend::Multiply;
    float opacity = 1.0f;
    bool enabled = true;
    // Generator
    GeneratorKind generator = GeneratorKind::EdgeWear;
    float level = 0.5f;    // how much of the surface it covers (0 none .. 1 all)
    float softness = 0.15f;
    float breakup = 0.4f;  // noise amount
    glm::vec3 direction{0.0f, 1.0f, 0.0f}; // Gradient / Light Direction
    // Noise (and generators' break-up)
    float scale = 0.05f;   // feature size, x the Part's size
    int octaves = 4;
    uint32_t seed = 1;
    float contrast = 1.0f;
    // Levels
    float inLow = 0.0f, inHigh = 1.0f, gamma = 1.0f, outLow = 0.0f, outHigh = 1.0f;
    // Blur
    float radius = 4.0f; // texels
    // Fill
    float value = 1.0f;
    // Which Mesh Maps it reads (meshMapBit).
    uint32_t mapsNeeded() const;
};

struct FillLayer {
    BrushMaterial material;          // what the layer lays down (material.channels: which channels, BaseColor included)
    std::vector<MaskEffect> effects; // top to bottom
    uint64_t mapsRevision = 0;       // the Mesh Maps it was last evaluated from (0: never)
    bool needsMaps() const;
    uint32_t mapsNeeded() const;
    bool hasPaint() const;
    int blurPad() const; // texels a tile's evaluation reads around it
};

// --- Layers ---
// Adds a Fill layer above `above` (-2: on top) with `fill` and evaluates it; returns its index.
int addFillLayer(Mesh& mesh, PaintTexture& tex, std::string name, FillLayer fill, int above = -2);
bool isFillLayer(const PaintTexture& tex, int layer);
// Evaluates Fill layer `layer` in `tiles` (texture tiles, kTextureTile; every tile when null) and recomposites
// them. Reads tex.meshMaps (generators without them read neutral values).
void evaluateFillLayer(const Mesh& mesh, PaintTexture& tex, int layer, const std::vector<uint32_t>* tiles = nullptr);
// Every Fill layer (that `filter` accepts; all when null), then one composite of those tiles.
void evaluateFillLayers(const Mesh& mesh, PaintTexture& tex, const std::vector<uint32_t>* tiles = nullptr,
                        bool onlyPainted = false);
// The Mesh Maps the Fill layers read (meshMapBit), 0 when none.
uint32_t fillMapsNeeded(const PaintTexture& tex);
// Once a frame before drawing (the app): Fill layers whose mask is painted re-evaluate the tiles the texture
// changed in since the last call (a mask stroke, its undo). True when it evaluated something.
bool updateFillLayers(const Mesh& mesh, PaintTexture& tex);

// The mask of one effect stack at texel (x, y), for tests and the UI's preview.
float fillMaskAt(const Mesh& mesh, const PaintTexture& tex, int layer, int x, int y);

// --- Settings as JSON (projects, Material Recipes) ---
Json fillLayerJson(const FillLayer& fill);
bool fillLayerFromJson(const Json& j, FillLayer& out, std::string& error);
Json maskEffectJson(const MaskEffect& e);
bool maskEffectFromJson(const Json& j, MaskEffect& out, std::string& error);

// Mesh Maps for Fill layers: the app bakes them on a worker thread (setMeshMapRequester); without one (the CLI,
// scripts) they are baked here, at once. True when the maps are ready now.
using MeshMapRequester = std::function<void(Document&)>;
void setMeshMapRequester(MeshMapRequester requester);
bool ensureFillMeshMaps(Document& doc, std::string& error);

// fill_layer (add / set / effect / remove_effect / info / evaluate).
void registerLayerStackCommands(CommandRegistry& registry);

} // namespace sf
