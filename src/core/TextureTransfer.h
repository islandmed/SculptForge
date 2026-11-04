// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Texture transfer: keeps a Part's painted texture (core/Texture.h) when its UVs or its surface are rebuilt.
//
// - New UVs on the same triangles (Auto UV on a textured Part): every texel of the new layout is the same
//   surface point as before (same triangle, same barycentrics), so it reads the old texture at the old UVs.
//   Every layer is transferred, with its settings; the result is exact up to resampling (bilinear).
// - A new surface (voxel remesh, QRemesh): the result gets Auto UVs, and every texel takes the colour of the
//   closest point of the old surface. Every layer is transferred too.
// - Merged Parts: the merged mesh keeps each part's triangles in order, so each texel reads its part's
//   texture exactly (layers are flattened, as the parts' stacks differ).
//
// Texels are filled triangle by triangle on all threads; then the texels around each UV island are padded
// ring by ring from the island's edge (as baking does), so seams don't show. Texels no island reaches stay
// white (transparent on layers).
#pragma once

#include "core/AutoUv.h"
#include "core/Mesh.h"
#include "core/Texture.h"

#include <memory>
#include <string>
#include <vector>

namespace sf {

constexpr int kTransferPadding = 8; // texels padded around the new islands

struct TextureTransferStats {
    size_t texels = 0; // texels inside the new UV islands
    size_t missed = 0; // of those, texels that found no source (left blank, then padded)
    int charts = 0;    // UV islands made by Auto UV (0 when the UVs were given)
    double uvSeconds = 0, transferSeconds = 0;
};

// The texture of `mesh` laid out again on `newUvs` (per triangle corner, same triangles as `mesh`). Null when
// the mesh has no texture or `newUvs` doesn't fit.
std::shared_ptr<PaintTexture> retargetTexture(const Mesh& mesh, const std::vector<glm::vec2>& newUvs,
                                              TextureTransferStats* stats = nullptr);

// Gives `mesh` new UVs (Auto UV), moving its texture and layers onto them (retargetTexture) unless `keep` is
// false. Returns the status text to add (" (texture moved to the new UVs)"), empty without a texture.
std::string setUvsKeepingTexture(Mesh& mesh, std::vector<glm::vec2> uvs, bool keep = true);

// Gives `target` (which must have UVs) a texture of `source`'s size whose texels take the colour of the closest
// point on `source`'s surface (all of its layers). False with `error` when `source` has no texture.
bool transferTextureFromSurface(const Mesh& source, Mesh& target, std::string& error,
                                TextureTransferStats* stats = nullptr);

// After a rebuild of `source` into `target` (voxel remesh, QRemesh): Auto UVs for `target` (packed for the
// texture's size) and the texture projected from `source`. Does nothing (true) when `source` has no texture.
// False with `error` on failure or cancel (`target` keeps no UVs or texture then).
bool carryTexture(const Mesh& source, Mesh& target, std::string& error, TextureTransferStats* stats = nullptr,
                  const AutoUvProgress& progress = {});

// The status text for a rebuild that had to deal with a texture: what carryTexture did (", texture kept: ..."),
// or that the texture was dropped. Empty when `source` has no texture. Runs carryTexture when `keep`.
std::string carryTextureNote(const Mesh& source, Mesh& target, bool keep, const AutoUvProgress& progress = {});

// After mergeMeshes(parts) into `merged` (same triangles, in order): Auto UVs for `merged` and one texture
// holding each textured part's paint (composited). Does nothing (true) when no part has a texture.
bool carryMergedTextures(const std::vector<const Mesh*>& parts, Mesh& merged, std::string& error,
                         TextureTransferStats* stats = nullptr);
// The same as status text (", textures kept: ..." or that they were dropped); empty when no part has a texture.
std::string carryMergedTexturesNote(const std::vector<const Mesh*>& parts, Mesh& merged, bool keep);

} // namespace sf
