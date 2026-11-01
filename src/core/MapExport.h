// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Map export: writes a texture's channels (core/TextureChannels.h) as the image files a game engine or DCC
// expects, driven by export presets (export_maps).
//
// A preset is a small JSON document (our own format; built-ins below, or any .json file):
//
//   { "id": "unreal", "label": "Unreal Engine", "file": "T_{name}_{map}", "normal": "directx",
//     "maps": [ {"map": "BC",  "channels": ["base_color.r", "base_color.g", "base_color.b", "opacity?"]},
//               {"map": "N",   "channels": ["normal.r", "normal.g", "normal.b"]},
//               {"map": "ORM", "channels": ["ao", "roughness", "metallic"]},
//               {"map": "H",   "channels": ["height"], "bits": 16, "optional": true} ] }
//
// Each map lists 1 (grey), 3 (RGB) or 4 (RGBA) sources. A source is a channel ("roughness", "ao", "height",
// "metallic", "opacity"), a component ("base_color.g", "normal.r", "emissive.b"), "smoothness" (1 - roughness),
// a constant ("0", "1", "0.5"), or "1-<source>". A trailing '?' drops that component when its channel isn't
// enabled (an RGBA map becomes RGB). "optional": true writes the map only when one of its channels is enabled.
// "normal": "directx" flips green (Y down); the normal is the effective one (Height's normal combined in).
// "bits": 16 writes a 16-bit PNG (height). Channels that aren't enabled export their default value.
//
// Built-in presets: unreal, unity_urp, unity_hdrp, gltf, godot, blender, usd.
#pragma once

#include "core/Image.h"

#include <string>
#include <vector>

namespace sf {

struct PaintTexture;
class CommandRegistry;
struct Document;

struct MapSource {
    enum class Kind { Channel, Constant } kind = Kind::Constant;
    int channel = 0;    // Channel
    int component = 0;  // 0..2
    bool invert = false; // 1 - value
    bool dropIfMissing = false;
    float constant = 0;
};

struct MapSpec {
    std::string map; // the file suffix ("BC", "ORM", "Normal")
    std::vector<MapSource> sources;
    int bits = 8;
    bool optional = false;
};

struct ExportPreset {
    std::string id, label, description;
    std::string file = "{name}_{map}"; // {name}: object name, {map}: MapSpec::map
    bool normalDirectX = false;
    std::vector<MapSpec> maps;
};

// Parses one source ("base_color.r", "1-roughness", "opacity?", "0.5"); false for an unknown one.
bool parseMapSource(const std::string& text, MapSource& out, std::string& error);
bool parseExportPreset(const std::string& json, ExportPreset& out, std::string& error);
const std::vector<ExportPreset>& builtinExportPresets();
// A built-in preset by id (any case; "godot" etc.), or a preset .json file.
bool findExportPreset(const std::string& idOrPath, ExportPreset& out, std::string& error);

// Whether a map is written for this texture (optional maps need one of their channels).
bool mapWanted(const PaintTexture& tex, const MapSpec& map);
// The map as an 8-bit image (top row = v 1, as image files are): grey, RGB or RGBA after '?' drops
// (Image is always RGBA; `channels` says how many are meaningful: 1, 3 or 4).
bool renderMap(const PaintTexture& tex, const MapSpec& map, bool normalDirectX, Image& out, int& channels);
// 16-bit version (top-down), `channels` 1, 3 or 4 values per pixel.
bool renderMap16(const PaintTexture& tex, const MapSpec& map, bool normalDirectX, std::vector<uint16_t>& out, int& channels);

// A map from a list of sources (as a preset's "channels") encoded as an 8-bit PNG (top row = v 1). The model
// exporters use it for their PBR material slots.
bool encodeMapPng(const PaintTexture& tex, const std::vector<std::string>& sources, bool normalDirectX, std::vector<uint8_t>& png);

// The PBR material slots a model exporter writes for a texture with material channels: "base_color" (opacity in
// alpha when painted), "normal" (OpenGL; written when Normal or Height is enabled), "orm" (glTF's occlusion /
// roughness / metallic), "roughness", "metallic", "occlusion", "emissive", "opacity". Empty for a texture without
// material channels. `packedOrm`: glTF packs roughness / metallic / occlusion into "orm" instead of separate maps.
std::vector<std::string> pbrSlots(const PaintTexture& tex, bool packedOrm);
// Sources of a slot (for encodeMapPng); empty for an unknown slot.
std::vector<std::string> pbrSlotSources(const std::string& slot);

// The file name (no folder, with .png) of `map` for an object called `name`.
std::string mapFileName(const ExportPreset& preset, const MapSpec& map, const std::string& name);

struct ExportedMap {
    std::string map, path;
};
// Writes every wanted map of `preset` into `folder` (created if missing). False with `error` on failure.
bool exportMaps(const PaintTexture& tex, const ExportPreset& preset, const std::string& folder, const std::string& name,
                std::vector<ExportedMap>& written, std::string& error);

// export_maps.
void registerMapExportCommands(CommandRegistry& registry);

} // namespace sf
