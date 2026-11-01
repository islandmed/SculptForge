// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Project IO for a texture's material channels (core/TextureChannels.h), kept out of core/Project.cpp so the
// project code only calls two hooks. Written under the texture's key "pbr" (project format 7):
//
//   "pbr": { "height_normal_strength": 4,
//            "channels": [ {"channel": "roughness", "base": <blob>}, ... ],      // enabled channels
//            "layers":   [ [ {"channel": "roughness", "enabled": true, "blend": "multiply",
//                             "opacity": 1, "image": <blob or absent when transparent>}, ... ], ... ] }
//
// "layers" has one entry per texture layer (same order as the texture's "layers"); BaseColor's entry holds only
// "enabled". A blob is a TileImage in a small sparse format ("sftiles", zlib-compressed): only the tiles that
// aren't the fill are stored, uniform tiles as one value, so unpainted channels cost a few bytes.
//
// Readers that don't know "pbr" (format 6) ignore it and load the base colour. A project with no material
// channels writes no "pbr" key and stays format 6 (textureNeedsFormat7).
#pragma once

#include "core/Json.h"
#include "core/TileImage.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace sf {

struct PaintTexture;

// The project format the texture's material data needs: 7 with material channels, else 6.
int textureFormatVersion(const PaintTexture& tex);

// Stores bytes as a project blob named `file` and returns its descriptor (Project.cpp's Writer::bytes).
using BlobWriter = std::function<Json(const std::string& file, const std::vector<uint8_t>& bytes)>;
// Reads the blob a descriptor names (at most `maxBytes`); false with the reader's error.
using BlobReader = std::function<bool(const Json& desc, const char* what, size_t maxBytes, std::vector<uint8_t>& out)>;

// The "pbr" object for the texture (null Json when it has no material channels). Blobs are named under `prefix`.
Json writeTextureChannels(const PaintTexture& tex, const std::string& prefix, const BlobWriter& write);
// Reads "pbr" into `tex` (already sized, with its layers). A missing key is fine; malformed data fails with `error`.
bool readTextureChannels(const Json& pbr, PaintTexture& tex, const BlobReader& read, std::string& error);

// The sparse tile encoding on its own (tests, tools).
std::vector<uint8_t> encodeTileImage(const TileImage& img);
// Decodes into `img`, which must already have the expected size and format; false when the bytes don't match.
bool decodeTileImage(const std::vector<uint8_t>& bytes, TileImage& img, std::string& error);

} // namespace sf
