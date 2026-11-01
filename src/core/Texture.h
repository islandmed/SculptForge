// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Texture painting on UV maps: an RGBA texture per SubTool (Mesh::texture), so
// the colour resolution comes from the texture instead of the mesh density.
//
// Storage is 8-bit sRGB (what PNGs hold); painting blends in linear like
// polypaint does. Rows are bottom-up (row 0 is v = 0, as OpenGL samples it);
// PNG export and import flip. A texture has the same Photoshop-style layer
// stack as polypaint (core/PaintLayer.h): without layers `pixels` is the paint;
// with layers `base` holds the Base layer, every TextureLayer its own colour
// with coverage in alpha, and `pixels` the composite (recomputed for the
// changed rectangle only).
//
// A dab rasterises, in UV space, only the triangles near the brush (BVH), gets
// each texel's 3D position from its barycentrics and applies polypaint's rules
// (falloff, alpha stamp, strength x pressure, mask, hidden faces, symmetry,
// and front faces only). Texels just outside the UV islands (kTextureGutter
// texels) take the colour of the nearest island texel, so seams don't show.
// Undo keeps the 64x64 tiles a stroke touched (one step per stroke), and whole-texture edits keep only the
// tiles they changed (TextureStackUndo); tiles of one colour cost one RGBA value.
//
// Mesh::texture is shared between copies of a mesh (undo snapshots, duplicated
// SubTools): edits go through mutableTexture(), which copies it first when
// anyone else holds it, and stack edits replace it (TextureStackUndo).
#pragma once

#include "core/BVH.h"
#include "core/Commands.h"
#include "core/Image.h"
#include "core/Mesh.h"
#include "core/PaintLayer.h"
#include "core/Sculpt.h"
#include "core/TextureChannels.h"

#include <cstdint>
#include <atomic>
#include <functional>
#include <mutex>
#include <span>
#include <memory>
#include <string>
#include <vector>

namespace sf {

struct FillLayer; // (core/LayerStack.h)


constexpr int kTextureTile = 64;       // undo tile size (texels)
constexpr int kTextureGutter = 4;      // texels of padding painted around UV islands
constexpr int kDefaultTextureSize = 2048;
// 1K, 2K, 4K or 8K.
bool validTextureSize(int size);

// A fresh TextureLayer::id.
uint32_t newTextureLayerId();

struct TextureLayer {
    uint32_t id = newTextureLayerId(); // identity across edits (undo matches layers by it); copies keep it
    std::string name;
    std::vector<uint8_t> rgba; // sRGB colour + coverage (alpha), rows bottom-up
    float opacity = 1.0f;
    BlendMode blend = BlendMode::Normal;
    bool visible = true;
    bool locked = false; // locked layers can't be painted, filled, cleared or merged
    LayerChannels channels; // material channels (core/TextureChannels.h); [BaseColor] only says whether colour shows
    // A Fill layer (core/LayerStack.h): its material and mask effects; `rgba` and the channel images are then the
    // evaluation's output. Copies of the layer (undo, duplicates) share it; edits replace it. Null: a paint layer.
    std::shared_ptr<FillLayer> fill;
    // A Fill layer's painted mask, RGBA8 with the mask in alpha: what painting with the layer active paints into
    // (PaintTexture::target), read by the layer's Paint mask effects. Empty for paint layers.
    std::vector<uint8_t> mask;
};

struct UvTileIndex;   // (Texture.cpp)
struct MeshMapSet;    // (core/MeshMaps.h)
struct TexelMapCache; // (Texture.cpp)

struct PaintTexture {
    int width = 0, height = 0;
    uint64_t serial = 0; // unique per texture object (makeTexture / copies), for GPU caches
    std::vector<uint8_t> pixels;     // RGBA8 sRGB composite (opaque): shown, exported, sampled
    std::vector<uint8_t> base;       // the Base layer while `layers` is not empty
    std::vector<TextureLayer> layers;
    int activeLayer = -1;            // the layer painting writes into (-1 = Base)
    // Material channels (core/TextureChannels.h): Roughness, Metallic, Normal, Height, AO, Emissive, Opacity.
    ChannelPlanes channels;
    float heightNormalStrength = 4.0f; // how steep the Height channel's normal is (heightNormalAt)
    // Solo channel view (syncChannelView): what the renderer uploads instead of `pixels` while viewChannel is
    // not BaseColor. A cache: not saved or undone.
    std::vector<uint8_t> viewPixels;
    Channel viewChannel = Channel::BaseColor;
    uint64_t viewRevision = 0;
    // Mesh Maps (core/MeshMaps.h): the Part's AO, curvature, thickness, ... on these UVs at this size, which Fill
    // layers read. Derived data, replaced as a whole when re-baked (copies and undo steps share it); not saved.
    std::shared_ptr<const MeshMapSet> meshMaps;
    uint64_t fillRevision = 0; // the revision updateFillLayers last looked at (core/LayerStack.h)
    // The RGBA8 texels the viewport shows: the composite, or the solo channel.
    const std::vector<uint8_t>& shown() const {
        return viewChannel != Channel::BaseColor && viewPixels.size() == pixels.size() ? viewPixels : pixels;
    }

    // GPU sync (see Renderer): `revision` counts changes; texels changed since `dirtyFrom` lie in
    // [dirtyMin, dirtyMax), inside the kTextureTile tiles flagged in `dirtyTiles`. A view that last uploaded
    // revision r >= dirtyFrom uploads only those tiles (two mirrored dabs far apart in UV space don't send
    // everything between them).
    uint64_t revision = 1, dirtyFrom = 1;
    glm::ivec2 dirtyMin{0}, dirtyMax{0};
    std::vector<uint8_t> dirtyTiles; // tilesX * tilesY flags (rows bottom-up), sized on first use
    void markDirty(int x0, int y0, int x1, int y1);
    void markAllDirty();
    bool tileDirty(int tx, int ty) const;
    // Called by a view after uploading: the rectangle starts again from here.
    void clearDirty();

    // Which texels some UV triangle covers (1 bit each; filled by ensureCoverage).
    std::vector<uint8_t> coverage;
    uint64_t coverageUvs = 0; // fingerprint of the Mesh::uvs the coverage was built for (0 = none)
    bool covered(int x, int y) const {
        const size_t i = size_t(y) * size_t(width) + size_t(x);
        return (coverage[i >> 3] >> (i & 7)) & 1;
    }

    // Texel tiles -> surface (the texture brush on dense meshes): the UV triangles binned per kTextureTile tile
    // (built once per UV layout and size, with `coverage`), and per tile, made when a brush first reaches it, the
    // triangle and barycentrics of every texel (island texels, and gutter texels with their nearest island
    // point). A dab then costs the texels it reaches, not the triangles under it. Caches: shared by copies, not
    // saved or undone.
    std::shared_ptr<const UvTileIndex> uvIndex;
    std::shared_ptr<TexelMapCache> texelMaps;

    size_t texelCount() const { return size_t(width) * size_t(height); }
    // The array painting writes into (layer -1 = Base: `pixels` without layers; a Fill layer: its mask).
    std::vector<uint8_t>& target(int layer);
    size_t bytes() const;
    // Without the material channel tiles (TileImage::liveBytes counts those, safe from any thread).
    size_t colourBytes() const;
};

// --- Texel maps (Mesh Maps and fill layers, core/MeshMaps.h, core/LayerStack.h) ---
// Texture tile `tile` (kTextureTile texels square; tiles row-major, rows bottom-up): every texel's triangle
// (kNoTexelTriangle where no UV island reaches) and barycentrics (b1, b2; b0 = 1 - b1 - b2), gutter texels mapped
// to their nearest island point. Built from the mesh's UVs (thread-safe: reads the mesh, the shared UV bins).
constexpr uint32_t kNoTexelTriangle = UINT32_MAX;
struct TexelTile {
    std::vector<uint32_t> tri;
    std::vector<glm::vec2> bary;
};
bool texelTile(const Mesh& mesh, int width, int height, uint32_t tile, TexelTile& out);
// The texture tiles triangle `tri` reaches with the gutter, inclusive; false when none.
bool triangleTiles(const Mesh& mesh, int width, int height, uint32_t tri, int& tx0, int& ty0, int& tx1, int& ty1);

// A fingerprint of the mesh's UVs (sampled; changes when the layout does), for caches keyed by UV layout.
uint64_t meshUvFingerprint(const Mesh& mesh);
// Bytes of the texture-painting caches shared by every texture on the same UVs (UV tile bins, coverage, texel
// maps of dense meshes): Preferences > Performance.
size_t texturePaintCacheBytes();
// Drops those caches (tests and benchmarks measure building them; textures keep theirs until repainted).
void clearTexturePaintCaches();

// Texels a view that is up to date except for the latest changes would upload now (benchmarks, tests).
size_t textureUploadTexels(const PaintTexture& tex);

// A new white texture.
std::shared_ptr<PaintTexture> makeTexture(int width, int height);
// A copy with its own serial.
std::shared_ptr<PaintTexture> cloneTexture(const PaintTexture& tex);

// --- Mesh helpers ---
// The mesh has a texture and UVs to show it with.
bool hasTexture(const Mesh& mesh);
// Paint (brush, fills, layers) goes into the texture: Mesh::paintToTexture and hasTexture.
bool paintsTexture(const Mesh& mesh);
// The mesh's texture for editing: copied first when another mesh (an undo snapshot, a duplicate) shares it.
PaintTexture& mutableTexture(Mesh& mesh);
// Builds `tex.coverage` for the mesh's UVs if it is missing or stale (the UVs changed).
void ensureCoverage(const Mesh& mesh, PaintTexture& tex, bool force = false);

// --- UV rasterisation ---
// Calls fn(x, y, b0, b1, b2) for every texel of a `width` x `height` texture whose centre lies inside the
// UV triangle (uv in 0..1, texel (x, y) centred at ((x + 0.5) / width, (y + 0.5) / height)); b are the
// barycentric weights of the corners. Exact fixed-point edge tests with a top-left rule: texels on an edge
// that two triangles share belong to exactly one of them. Texels outside the texture are skipped.
using TexelFn = std::function<void(int x, int y, float b0, float b1, float b2)>;
void rasterizeUvTriangle(const glm::vec2& uv0, const glm::vec2& uv1, const glm::vec2& uv2, int width, int height,
                         const TexelFn& fn);

// --- Compositing ---
// Recomputes `pixels` from Base and the visible layers inside [x0, x1) x [y0, y1) (no-op without layers).
void compositeTexture(PaintTexture& tex, int x0, int y0, int x1, int y1);
void compositeTextureAll(PaintTexture& tex);
// Linear RGB of the composite at texture coordinates `uv` (bilinear).
glm::vec3 sampleTexture(const PaintTexture& tex, const glm::vec2& uv);
// Texture colour under a surface point of triangle `triangle` (for the colour picker).
glm::vec3 sampleTextureAt(const Mesh& mesh, uint32_t triangle, const glm::vec3& point);

// --- Painting (brush) ---
// One stroke of the Paint brush into the texture. begin() picks the target layer; dab() paints texels;
// end() moves the undo tiles into the stroke's record. Within a stroke each texel keeps its coverage
// (repeated dabs build up as polypaint does) and is computed from its colour before the stroke, so 8-bit
// storage never stalls on small strengths.
class TexturePainter {
public:
    // False (and nothing starts) when the mesh doesn't paint into a texture or the target layer is locked/hidden.
    // `channels`: channelBit(...) of what the stroke may paint (BaseColor and/or material channels; those the
    // texture doesn't enable are dropped).
    bool begin(Mesh& mesh, uint32_t channels = 1u);
    uint32_t channels() const { return channels_; }
    bool active() const { return active_; }
    // Paints `color` (linear) under the brush, `p.invert` paints Base back to white / erases a layer.
    // Returns the number of texels changed.
    size_t dab(Mesh& mesh, const BVH& bvh, const DabParams& p, const glm::vec3& color, Symmetry sym);
    // The same with a material: every channel the brush has and the texture enables gets its value (BaseColor
    // only when `material.channels` has it), with the same falloff, coverage and undo.
    size_t dab(Mesh& mesh, const BVH& bvh, const DabParams& p, const BrushMaterial& material, Symmetry sym);
    // Ends the stroke: the touched tiles become `record.textureTiles` (nothing when no texel changed).
    void end(UndoRecord& record);

private:
    // A material channel the stroke paints in one tile: the tile before the stroke (kept for undo; null = the
    // image's fill) and the writable texels.
    struct ChannelTile {
        TileImage::TilePtr original;
        const uint8_t* origTexels = nullptr; // dense original texels, or one texel (origStride 0)
        int origStride = 0;                  // bytes between texels of `origTexels` (0: uniform)
        uint8_t origValue[8] = {};           // the original's texel when uniform / null
        uint8_t* texels = nullptr;           // the image's writable tile
    };
    struct Tile {
        std::vector<uint8_t> original; // the tile before the stroke (kTextureTile^2 RGBA)
        std::vector<float> coverage;   // stroke coverage per texel
        std::vector<uint32_t> stamp;   // last dab that wrote the texel
        std::array<ChannelTile, kChannelCount> channels; // [c] valid when the stroke paints channel c
    };
    // The images material channel c paints into (the target layer's or Base), null when the stroke doesn't paint c.
    TileImage* channelTarget(PaintTexture& tex, int c) const;
    uint32_t channels_ = 1u; // what the stroke paints (channelBit; BaseColor = bit 0)

public:
    // Which dab path to use: Auto (texel tiles on dense meshes: under 64 texels per triangle), Tiles or Triangles
    // (tests compare them).
    enum class Path { Auto, Tiles, Triangles };
    static void setPath(Path path);
    // Readies dense-mesh painting before a dab needs it: the UV bins and coverage (shared per UV layout), and the
    // texel maps of the tiles a dab of `radius` at each of `centers` would reach (with `bvh`). Paints nothing.
    void prewarm(Mesh& mesh, const BVH* bvh, std::span<const glm::vec3> centers, float radius);

private:
    // Dense meshes (texels outnumber what each triangle covers): the brush finds the texture tiles under it from
    // the BVH leaves (no per-triangle work), then paints every texel of those tiles from its cached triangle and
    // barycentrics (PaintTexture::texelMaps), tiles in parallel. Costs the texels reached, not the triangles.
    struct DabContext;
    bool useTilePath(const Mesh& mesh, PaintTexture& tex) const;
    template <class Paint> // paint(tile record, texel in tile, x, y, coverage)
    size_t dabTiles(const DabContext& ctx, const Paint& paint, bool composite,
                    std::vector<std::pair<glm::ivec2, glm::ivec2>>& rects);
    std::vector<uint32_t> touched_;     // tiles a dab reaches
    std::vector<uint8_t> touchedFlag_;  // per tile
    // The tile's stroke record, made on first touch (thread-safe).
    // Also makes the tile of every material channel the stroke paints writable.
    Tile& tile(uint32_t index, const std::vector<uint8_t>& target, int width, int height, PaintTexture& tex);
    struct DabTri { // a triangle under the brush (mirror `mirror`), with the texel box the dab can reach
        uint32_t tri = 0;
        int mirror = 0;
        int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    };

    bool active_ = false;
    int layer_ = -1;
    int tilesX_ = 0, tilesY_ = 0;
    std::vector<std::unique_ptr<Tile>> tiles_; // per tile (row-major), null until the stroke touches it
    std::unique_ptr<std::atomic<Tile*>[]> tilePtr_; // the same tiles, for lock-free lookups from the band threads
    std::mutex tileMutex_;                          // guards making a tile
    uint32_t dab_ = 0;
    std::vector<uint32_t> tris_;
    std::vector<DabTri> dabTris_;
    std::vector<std::vector<uint32_t>> bandTris_; // per 16-row band: indices into dabTris_, in serial order
};

// TexturePainter::prewarm without a stroke (the app calls it while the Paint brush hovers a dense textured mesh,
// so the first dab doesn't build what it needs).
void prewarmTexturePainting(Mesh& mesh, const BVH* bvh = nullptr, std::span<const glm::vec3> centers = {}, float radius = 0.0f);

// --- Whole-texture operations (callers record undo: TextureStackUndo or captureTexture) ---
// Calls fn(triangle, b0, b1, b2, x, y) for every texel inside a UV triangle plus the gutter around the
// islands (gutter texels get the nearest island point). Hidden triangles are skipped when `skipHidden`.
using BakeFn = std::function<void(uint32_t tri, float b0, float b1, float b2, int x, int y)>;
void bakeTexels(const Mesh& mesh, PaintTexture& tex, const BakeFn& fn, bool skipHidden = true);
// Blends `color` into the target layer of every texel with the weight interpolated from `vertexWeight`
// (per vertex 0..1). Returns the number of texels changed.
size_t textureFill(Mesh& mesh, const glm::vec3& color, const std::vector<float>& vertexWeight);
// Bakes the polypaint (Mesh::colors, composited) into the active texture layer (opaque).
void polypaintToTexture(Mesh& mesh);
// Sets the polypaint (active paint layer) of every vertex to the texture's colour at its UVs (seams average).
void textureToPolypaint(Mesh& mesh);
// The composite as a top-down image (for PNG export), or one layer (-1 = Base).
Image textureImage(const PaintTexture& tex);
// Replaces the Base (or the only) layer with `image` resized to the texture (nearest), as sRGB.
bool loadTextureImage(PaintTexture& tex, const Image& image, std::string& error);

// --- Texture layer stack (as the polypaint paint_layer_* functions) ---
int addTextureLayer(PaintTexture& tex, std::string name, int above = -2);
int duplicateTextureLayer(PaintTexture& tex, int layer);
bool removeTextureLayer(PaintTexture& tex, int layer);
bool moveTextureLayer(PaintTexture& tex, int layer, int direction);
bool mergeTextureLayerDown(PaintTexture& tex, int layer, std::string& error);
void flattenTextureLayers(PaintTexture& tex);
void clearTextureLayer(PaintTexture& tex, int layer);
std::string uniqueTextureLayerName(const PaintTexture& tex, const std::string& wanted);
int findTextureLayer(const PaintTexture& tex, const std::string& name);
bool textureLayerEditable(const PaintTexture& tex, int layer);

// --- Undo (TextureTileUndo / TextureStackUndo live in core/Undo.h) ---
// An undo step holding the mesh's current texture; then gives the mesh its own copy to change
// (copy-on-write), so call it right before a whole-texture edit, and finishTextureUndo after it.
UndoRecord captureTexture(Mesh& mesh, std::string label);
// Shrinks a captureTexture record to what the edit changed (the tiles that differ, settings), before pushing it.
void finishTextureUndo(UndoRecord& record, const Mesh& mesh);

// texture_create, texture_delete, texture_import, texture_export, texture_from_polypaint,
// polypaint_from_texture, paint_target, uv_auto (see core/AutoUv.h).
void registerTextureCommands(CommandRegistry& registry);
// paint_layer_<verb> on the texture's layers (PaintLayer.cpp hands over when the SubTool paints into its texture).
CommandResult textureLayerCommand(Document& doc, const std::string& verb, const Args& args);
} // namespace sf
