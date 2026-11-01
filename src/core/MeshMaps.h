// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Mesh Maps: texture-space maps of a Part's own surface, baked on its UVs at the texture's size and kept with its
// texture (PaintTexture::meshMaps), for Fill layers to read (core/LayerStack.h): ambient occlusion, curvature,
// thickness, position, world normal and ID (polygroups). Files for other programs come from the bake command
// (core/Bake.h), which also bakes from other Parts, subdivision levels and micro levels, and to UDIM tiles.
//
// Maps are TileImages (64 x 64 tiles, the texture's tiles) and are baked tile by tile on every core:
//   - each texel's surface point and smooth normal come from its triangle (gutter texels from the nearest island
//     point, as painting pads islands), so maps line up with the paint texel for texel;
//   - AO: cosine-weighted rays from the point; occluders within settings.aoDistance (x the size) count;
//   - thickness: cosine-weighted rays into the surface, the mean distance to the far side over thicknessDistance;
//   - curvature: the mean curvature of the surface (meshMeanCurvature) times curvatureRadius: 0.5 is flat, brighter
//     is convex (edges, ridges), darker concave (creases);
//   - position: the point in the Part's bounding box (16-bit RGB); world normal: the smooth normal (RGB);
//   - ID: a flat colour per polygroup.
//
// Live updates (the app, App::updateMeshMaps): the set remembers the positions it was baked from. When the Part is
// sculpted, the changed region is found by comparing them (changedRegion), and only the tiles whose texels lie
// within reach of it are baked again (MeshMapUpdate), on a worker thread from a compact copy of the triangles near
// it; Fill layers then re-evaluate those tiles (core/LayerStack.h). Unchanged texels keep their values, so the
// result equals a full bake of the sculpted mesh (tested).
#pragma once

#include "core/Bake.h"
#include "core/Mesh.h"
#include "core/Texture.h"
#include "core/TileImage.h"

#include <glm/glm.hpp>

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace sf {

class CommandRegistry;
struct Document;
struct PaintTexture;

enum class MeshMapKind : uint8_t { AmbientOcclusion, Curvature, Thickness, Position, WorldNormal, Id };
constexpr int kMeshMapCount = 6;
constexpr uint32_t kAllMeshMaps = (1u << kMeshMapCount) - 1;
inline uint32_t meshMapBit(MeshMapKind k) { return 1u << uint32_t(k); }
const char* meshMapId(MeshMapKind k);    // "ao", "curvature", "thickness", "position", "world_normal", "id"
const char* meshMapLabel(MeshMapKind k); // "Ambient Occlusion", ...
bool parseMeshMap(const std::string& text, MeshMapKind& out);
// A comma list of ids, or "all".
bool parseMeshMaps(const std::string& list, uint32_t& kinds, std::string& error);

struct MeshMapSettings {
    int aoRays = 32;
    float aoDistance = 0.05f; // x the Part's bounding-box diagonal
    int thicknessRays = 16;
    float thicknessDistance = 0.1f;
    float curvatureRadius = 0.02f;
    bool operator==(const MeshMapSettings&) const = default;
};

struct MeshMapSet {
    int width = 0, height = 0;
    uint32_t kinds = 0; // baked maps (meshMapBit)
    MeshMapSettings settings;
    std::array<TileImage, kMeshMapCount> images;
    glm::vec3 boundsMin{0.0f}, boundsMax{1.0f}; // the box Position spans
    float diagonal = 1.0f;
    uint64_t uvKey = 0; // meshUvFingerprint of the UVs baked on
    size_t vertexCount = 0, triangleCount = 0;
    // The positions baked from (live updates compare against them); shared with copies.
    std::shared_ptr<const std::vector<glm::vec3>> positions;
    uint64_t revision = 1; // changes with every bake (Fill layers re-evaluate)
    bool live = true;      // bake the changed tiles again after sculpting
    double seconds = 0.0;  // the last bake's time
    size_t texelsBaked = 0;

    bool has(MeshMapKind k) const { return (kinds & meshMapBit(k)) != 0; }
    // Still describes `mesh` (same topology and UV layout) at this size.
    bool matches(const Mesh& mesh, int width, int height) const;
    size_t bytes() const;
    // Values at texel (x, y): scalar maps 0..1; Position 0..1 in the box; WorldNormal a unit vector; Id the colour.
    float value(MeshMapKind k, int x, int y) const;
    glm::vec3 vector(MeshMapKind k, int x, int y) const;
};

// Per-vertex mean curvature in 1 / length (> 0 convex): 2 (p - mean of the one-ring) . n over the ring's mean squared
// edge length, so a sphere of radius R gives 1 / R at any density; smoothed once over the ring.
std::vector<float> meshMeanCurvature(const Mesh& mesh);
// The ID map's colour for a key (a polygroup, a Part): bright, well spread hues (sRGB 0..1).
glm::vec3 idMapColor(uint32_t key);

// Bakes `kinds` of `mesh` (its UVs) at width x height into `out` (every tile). False + error when the mesh has no
// UVs; "Cancelled" when `progress` returns false.
bool bakeMeshMaps(const Mesh& mesh, int width, int height, uint32_t kinds, const MeshMapSettings& settings,
                  MeshMapSet& out, std::string& error, const BakeProgress& progress = {});

// --- Live updates ---
// The box around the vertices that moved since `set` was baked (empty box: none moved), and their indices.
AABB changedRegion(const MeshMapSet& set, const Mesh& mesh, std::vector<uint32_t>* moved = nullptr);

// One incremental re-bake: made on the main thread from the mesh (prepare), run anywhere (run: no access to the
// document), applied on the main thread (apply). Holds a compact copy of the triangles near the change.
struct MeshMapUpdate {
    std::vector<uint32_t> tiles;      // tiles to bake again
    std::vector<TexelTile> texels;    // per tile: texel -> local triangle (kNoTexelTriangle: keep the old value)
    Mesh local;                       // the triangles near the change (positions, normals, UVs, polygroups)
    std::vector<uint32_t> movedVertices;      // mesh vertices that moved, and where they are now
    std::vector<glm::vec3> movedPositions;
    std::shared_ptr<const MeshMapSet> base;   // the set it updates
    std::array<std::vector<TileImage::TilePtr>, kMeshMapCount> result; // per kind, per tile
    size_t texelsBaked = 0;
    double seconds = 0.0;
    bool ok = false;
};
// Readies an update for the change `region` (changedRegion); false when nothing needs baking.
bool prepareMeshMapUpdate(const Mesh& mesh, const std::shared_ptr<const MeshMapSet>& set, const AABB& region,
                          std::vector<uint32_t> moved, MeshMapUpdate& update);
void runMeshMapUpdate(MeshMapUpdate& update, const std::atomic<bool>* cancel = nullptr);
// A new set with the update's tiles and positions in it (the old one may be shared with undo steps and copies).
std::shared_ptr<MeshMapSet> applyMeshMapUpdate(const MeshMapUpdate& update);

// One line for people: which maps, size, memory, time.
std::string describeMeshMaps(const MeshMapSet& set);
// After the active Part's Mesh Maps changed (baked, cleared, updated): Fill layers that read them re-evaluate
// (core/LayerStack.cpp). Every tile when `tiles` is null.
void notifyMeshMapsChanged(Document& doc, Mesh& mesh, const std::vector<uint32_t>* tiles = nullptr);

// mesh_maps (bake / clear / info / live).
void registerMeshMapCommands(CommandRegistry& registry);

} // namespace sf
