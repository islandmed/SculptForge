// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Material channels of a painted texture (PBR texture painting).
//
// A texture (core/Texture.h) always has its base colour: PaintTexture::pixels (and the colour of every
// TextureLayer). It can also carry material channels, each turned on separately (texture_channels):
//
//   Roughness, Metallic, AO, Opacity  1 x 8 bit       linear data
//   Height                            1 x 16 bit      0.5 = no displacement
//   Normal                            3 x 8 bit       tangent space, OpenGL convention (green up)
//   Emissive                          3 x 8 bit sRGB  colour
//
// Each enabled channel keeps a Base image (ChannelPlane::base) and, while the texture has layers, a composite
// (ChannelPlane::composite). Every TextureLayer has a LayerChannel per channel: its own image (value plus
// coverage in the last component), whether the layer affects that channel, a blend mode and an opacity.
// Images are TileImages (core/TileImage.h): sparse and copy-on-write, so a channel nobody painted costs no
// texel memory and an undo step keeps only the tiles a stroke changed.
//
// Compositing per texel and channel, bottom to top, as the colour does:
//     v = mix(v, blend(v, layer value), coverage * channel opacity * layer opacity)
// Blend modes are the ten of core/PaintLayer.h applied per component, plus NormalCombine for normals
// (reoriented normal mapping: the layer's detail is laid onto the normal below it instead of replacing it).
// A layer tile nobody painted is skipped, and a composite tile no layer covers shares the Base tile.
//
// Height also yields a normal (heightToNormal): the effective normal shown and exported is the Normal channel
// with the height's normal combined onto it (NormalCombine), at PaintTexture::heightNormalStrength.
#pragma once

#include "core/PaintLayer.h"
#include "core/TileImage.h"

#include <glm/glm.hpp>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace sf {

class Mesh;
struct PaintTexture;
class CommandRegistry;

enum class Channel : uint8_t { BaseColor, Roughness, Metallic, Normal, Height, AO, Emissive, Opacity };
constexpr int kChannelCount = 8;

struct ChannelFormat {
    const char* id;    // "base_color", "roughness", ... (commands, project files, export presets)
    const char* label; // "Base Color", "Roughness", ...
    const char* shortLabel; // two letters for layer rows: "BC", "R", "M", "N", "H", "AO", "E", "Op"
    int components;
    int bits;
    bool srgb;
    std::array<float, 4> defaultValue; // what an unpainted texel holds (0..1 per component)
};
const ChannelFormat& channelFormat(Channel c);
inline const ChannelFormat& channelFormat(int c) { return channelFormat(Channel(c)); }
// Accepts the id, the label, any case, and a few short forms ("color", "rough", "metal", "ao", "occlusion",
// "emission", "alpha", "bump").
bool parseChannel(const std::string& text, Channel& out);
inline uint32_t channelBit(Channel c) { return 1u << uint32_t(c); }

// Channel blend modes: the BlendMode values (0..kBlendModeCount-1), then NormalCombine.
constexpr uint8_t kBlendNormalCombine = uint8_t(kBlendModeCount);
constexpr int kChannelBlendCount = kBlendModeCount + 1;
const char* channelBlendId(uint8_t blend);    // "normal", ..., "normal_combine"
const char* channelBlendLabel(uint8_t blend); // "Normal", ..., "Normal Combine"
bool parseChannelBlend(const std::string& text, uint8_t& out);

// One channel of one TextureLayer.
struct LayerChannel {
    bool enabled = true;   // the layer affects this channel (BaseColor: the layer's colour shows)
    uint8_t blend = 0;     // channel blend (BaseColor uses TextureLayer::blend instead)
    float opacity = 1.0f;  // multiplies TextureLayer::opacity (BaseColor: unused)
    TileImage image;       // value + coverage (components + 1); empty until the texture enables the channel
};
using LayerChannels = std::array<LayerChannel, kChannelCount>;

// One material channel of a texture.
struct ChannelPlane {
    bool enabled = false;
    TileImage base;      // the Base layer (components of the channel's format)
    TileImage composite; // Base plus the layers (only while the texture has layers)
};
using ChannelPlanes = std::array<ChannelPlane, kChannelCount>; // [BaseColor] is unused: colour is `pixels`

// What the Paint brush lays down on each channel (paint_material). `channels` says which ones it writes;
// a channel is painted when the brush has it and the texture has it enabled (BaseColor always is).
struct BrushMaterial {
    glm::vec3 color{1.0f};    // linear RGB
    float roughness = 0.5f;
    float metallic = 0.0f;
    glm::vec3 normal{0, 0, 1}; // tangent-space direction (normalised when painted)
    float height = 0.5f;       // 0..1, 0.5 = flat
    float ao = 1.0f;
    glm::vec3 emissive{0.0f}; // linear RGB
    float opacity = 1.0f;
    uint32_t channels = 1u; // channelBit(...) of the channels the brush paints; BaseColor only by default
    // The brush value of channel c in 0..1 per component (Normal encoded 0..1, colours as stored: sRGB).
    std::array<float, 4> encoded(Channel c) const;
};

// --- Texture-level queries (PaintTexture in core/Texture.h) ---
bool channelEnabled(const PaintTexture& tex, Channel c); // BaseColor: always true
// Any material channel besides base colour is enabled (the texture is a PBR texture; projects then need format 7).
bool hasMaterialChannels(const PaintTexture& tex);
uint32_t enabledChannelMask(const PaintTexture& tex); // includes BaseColor
// The image shown and exported for channel c (the composite with layers, else Base). Not for BaseColor.
const TileImage& channelImage(const PaintTexture& tex, Channel c);
// Turns a channel on (its Base starts at the channel's default, every layer's channel transparent) or off
// (drops its images). Returns true when something changed. BaseColor can't be turned off.
bool setChannelEnabled(PaintTexture& tex, Channel c, bool on);
// Makes the layers' channel images match the enabled channels (after adding / duplicating layers).
void syncLayerChannels(PaintTexture& tex);

// Settings of channel c on layer `layer` (BaseColor maps onto TextureLayer::blend / opacity / visible).
uint8_t layerChannelBlend(const PaintTexture& tex, int layer, Channel c);
float layerChannelOpacity(const PaintTexture& tex, int layer, Channel c);

// --- Compositing ---
// Recomputes the composites of every enabled material channel in [x0, x1) x [y0, y1) (tile granular; no-op
// without layers). compositeTexture (core/Texture.h) calls it.
void compositeChannels(PaintTexture& tex, int x0, int y0, int x1, int y1);
void compositeChannel(PaintTexture& tex, Channel c, int x0, int y0, int x1, int y1);

// --- Normals ---
// Reoriented normal mapping: `detail` laid onto `base` (both unit tangent-space vectors).
glm::vec3 normalCombine(const glm::vec3& base, const glm::vec3& detail);
inline glm::vec3 decodeNormal(float r, float g, float b) { return glm::vec3(r, g, b) * 2.0f - 1.0f; }
// The normal of a height image at texel (x, y): central differences, `strength` = height units per texel
// slope scale (1 = a full 0..1 height step over one texel tilts 45 degrees at strength 1/1).
glm::vec3 heightNormalAt(const TileImage& height, int x, int y, float strength);
// The effective tangent-space normal at texel (x, y): the Normal channel (or flat) with the Height channel's
// normal combined onto it when Height is enabled.
glm::vec3 effectiveNormalAt(const PaintTexture& tex, int x, int y);

// --- Picking (C) and sampling ---
// The value of every enabled channel at texture coordinates uv (bilinear), into `m` (channels: the enabled ones).
void sampleMaterial(const PaintTexture& tex, const glm::vec2& uv, BrushMaterial& m);
// The same under a surface point of `triangle`.
bool sampleMaterialAt(const Mesh& mesh, uint32_t triangle, const glm::vec3& point, BrushMaterial& m);

// --- Solo channel view ---
// RGBA8 texels showing channel c (greyscale for scalar channels, the effective normal, the emissive colour)
// at texel rows [y0, y1) x [x0, x1) of `out` (width * height * 4, rows bottom-up).
void channelViewTexels(const PaintTexture& tex, Channel c, int x0, int y0, int x1, int y1, std::vector<uint8_t>& out);
// Brings PaintTexture::viewPixels up to date for showing channel c (BaseColor: stops showing a channel).
// Called once per frame before the renderer uploads: only the tiles changed since the last call are redone;
// a change of channel redoes everything and marks the texture dirty.
void syncChannelView(PaintTexture& tex, Channel c);

// --- PBR viewport ---
// Which packed planes the PBR viewport needs for this texture: bit 0 = ORM (AO, roughness, metallic, opacity;
// any of them enabled), bit 1 = normal (Normal or Height enabled), bit 2 = emissive.
uint32_t pbrViewPlanes(const PaintTexture& tex);
// The packed RGBA8 texels of [x0, x1) x [y0, y1) (clamped to the texture) for the renderer, each plane into its
// own rectangle-sized buffer (rows bottom-up, (x1 - x0) * 4 bytes per row; null = skip): ORM in linear units,
// the effective tangent-space normal (Normal with Height's normal combined), emissive as stored (sRGB).
void pbrViewTexels(const PaintTexture& tex, int x0, int y0, int x1, int y1, uint8_t* orm, uint8_t* normal,
                   uint8_t* emissive);

// --- Memory ---
// Bytes of material channel images (bases, composites and layer channels). Walks the tiles: call it only where
// the texture is edited (the main thread), never while a dab may run elsewhere; TileImage::liveBytes is the
// thread-safe total.
size_t channelBytes(const PaintTexture& tex);

// texture_channels, paint_material, channel_view, texture_layer_channel.
void registerTextureChannelCommands(CommandRegistry& registry);

} // namespace sf
