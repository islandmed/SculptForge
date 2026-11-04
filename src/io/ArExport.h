// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// "View in AR" for the phone link (link/PhoneLink.h): the sculpt as a phone-friendly model standing on a real
// desk. The visible parts are reduced to a budget of triangles (vertex clustering first for very big meshes, then
// quadric decimation), polypaint and textures are baked into one texture on new UVs (AR Quick Look ignores vertex
// colours), and the model is scaled to a chosen size with its base at y = 0 and centred, so it stands on the
// detected surface. Written as GLB (Android Scene Viewer, WebXR viewers) and USDZ (iPhone and iPad AR Quick Look),
// in metres.
#pragma once

#include "core/Mesh.h"

#include <cstdint>
#include <string>
#include <vector>

namespace sf {

struct ArExportOptions {
    size_t maxTriangles = 80000;  // phones show this smoothly; the files stay a few MB
    float sizeMeters = 0.20f;     // the model's largest side on the desk
    int textureSize = 1024;
    std::string name = "SculptForge";
};

struct ArExportResult {
    std::string glb, usdz; // file contents
    size_t triangles = 0;
    bool colored = false;  // polypaint or a texture was baked
    std::string summary;   // "62,410 triangles, vertex paint, 20 cm"
};

// `meshes`: the parts to show (null entries are skipped); hidden triangles are left out.
bool exportArModel(const std::vector<const Mesh*>& meshes, const ArExportOptions& options, ArExportResult& out,
                   std::string& error);

// Pieces exposed for tests.
// Each corner's colour (sRGB 0..1) on new per-corner `uvs` baked into a size x size texture (rows bottom-up),
// with the islands padded by a few texels. `cornerColors` has 3 per triangle.
std::vector<uint8_t> bakeCornerColors(const std::vector<glm::uvec3>& triangles, const std::vector<glm::vec2>& uvs,
                                      const std::vector<glm::vec3>& cornerColors, int size);

} // namespace sf
