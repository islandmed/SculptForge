// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Paint layers: a Photoshop-style stack of vertex paint layers per mesh (Part).
//
// Without paint layers Mesh::colors is the vertex paint, as it always was. Once a
// layer is added, Mesh::paintBase holds the bottom ("Base") layer, every
// PaintLayer holds its own colour plus per-vertex coverage (alpha), and
// Mesh::colors is the composited result that the viewport, exporters and
// colour picking read. The stack composites bottom to top in display (sRGB)
// space with Photoshop's blend maths:
//     c = mix(c, blend(c, layer.color), layer.alpha[v] * layer.opacity)
// so 50% grey on an Overlay layer leaves the colour unchanged, as in Photoshop.
//
// Painting (paintVertex, used by the Paint brush, fills and cavity paint)
// writes into the active layer (Mesh::activePaintLayer, -1 = Base) and
// recomposites just the vertices it touched, so it stays fast on 2M-vertex
// meshes. Layer edits that change the whole stack (opacity, blend mode,
// visibility, merge...) recomposite every vertex, in parallel.
//
// Paint layers are independent of sculpt layers (Mesh::layers): sculpt layers
// hold shape offsets, paint layers hold colour; each has its own list and its
// own active layer.
#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace sf {

class Mesh;
class CommandRegistry;

enum class BlendMode : uint8_t {
    Normal,
    Multiply,
    Screen,
    Overlay,
    SoftLight,
    Add, // Photoshop's Linear Dodge (Add)
    Subtract,
    Darken,
    Lighten,
    Difference,
};
constexpr int kBlendModeCount = 10;

// "normal", "multiply", "soft_light", ... (as scripts and project files spell them).
const char* blendModeId(BlendMode mode);
// "Normal", "Multiply", "Soft Light", ... (for the UI).
const char* blendModeLabel(BlendMode mode);
// Accepts the id, the label, any case, spaces / dashes / underscores optional, and "linear_dodge".
bool parseBlendMode(const std::string& text, BlendMode& mode);

// Photoshop blend of `source` over `backdrop`, both display (sRGB) colours in 0..1.
glm::vec3 blendColors(BlendMode mode, const glm::vec3& backdrop, const glm::vec3& source);

struct PaintLayer {
    std::string name;
    std::vector<glm::vec3> color; // per vertex, linear RGB (meaningful where alpha > 0)
    std::vector<float> alpha;     // per vertex coverage 0..1 (0 = transparent)
    float opacity = 1.0f;
    BlendMode blend = BlendMode::Normal;
    bool visible = true;
    bool locked = false; // locked layers can't be painted, filled, cleared or merged
};

// --- Stack queries ---
bool hasPaintLayers(const Mesh& mesh);
int paintLayerCount(const Mesh& mesh); // layers above Base (0 = plain vertex paint)
// The composited colour of vertex `v` (linear RGB) from Base and the visible layers.
glm::vec3 compositePaintVertex(const Mesh& mesh, uint32_t v);
// Recomposites Mesh::colors for `verts` (and marks them dirty), or for every vertex.
void compositePaint(Mesh& mesh, std::span<const uint32_t> verts);
void compositePaintAll(Mesh& mesh);
// True when `layer` (-1 = Base) is valid and not locked.
bool paintLayerEditable(const Mesh& mesh, int layer);

// --- Painting into the active layer ---
// Blends `color` into the active paint layer at `v` with weight `w` (0..1) and
// recomposites the vertex. On Base (or without layers) this mixes the colour as
// the Paint brush always did; on a layer it lays paint over the layer's own
// coverage ("source over"). `erase` paints Base back to white and erases a
// layer's coverage (Alt with the Paint brush). Does nothing on a locked layer.
// The caller records undo first (Sculptor::recordColor or capturePaintTarget).
void paintVertex(Mesh& mesh, uint32_t v, const glm::vec3& color, float w, bool erase = false);

// --- Stack edits (callers push undo records; see Undo.h PaintStackUndo) ---
// Inserts a transparent layer above `above` (-1 = Base; default: the top) and makes it active.
// Creates the stack (Base = today's vertex paint, or white) when the mesh has none. Returns its index.
int addPaintLayer(Mesh& mesh, std::string name, int above = -2);
// Copies layer `layer` (-1 = Base, as an opaque layer) to a new layer above it; returns its index.
int duplicatePaintLayer(Mesh& mesh, int layer);
// Deletes a layer (not Base); with the last one gone the mesh is plain vertex paint again.
bool removePaintLayer(Mesh& mesh, int layer);
// Moves a layer one place up (+1) or down (-1); Base stays at the bottom.
bool movePaintLayer(Mesh& mesh, int layer, int direction);
// Merges `layer` into the one below it (Base for layer 0), Photoshop style.
// Exact onto Base; onto a layer the result takes the lower layer's blend mode.
bool mergePaintLayerDown(Mesh& mesh, int layer, std::string& error);
// Bakes every visible layer into Base and removes the stack (hidden layers are discarded).
void flattenPaintLayers(Mesh& mesh);
// Clears a layer: a layer becomes transparent, Base becomes white. Recomposites.
void clearPaintLayer(Mesh& mesh, int layer);
// A name not used by any layer yet ("Layer 2", "Dirt copy", ...).
std::string uniquePaintLayerName(const Mesh& mesh, const std::string& wanted);
// Index of the layer called `name` ("Base" = -1); -2 when there is none.
int findPaintLayer(const Mesh& mesh, const std::string& name);

// --- Topology helpers ---
// Carries `src`'s paint stack over to `dst`, whose vertex i came from src vertex kept[i]
// (extract, split, decimate). `dst.colors` must already hold the composited colours.
void copyPaintLayers(const Mesh& src, Mesh& dst, std::span<const uint32_t> kept);
// The same for Loop subdivision: dst has `vertexCount` vertices, the first ones copied from src,
// and each midpoint (new vertex, parent a, parent b) averaging its parents (premultiplied by alpha).
void subdividePaintLayers(const Mesh& src, Mesh& dst, size_t vertexCount, std::span<const glm::uvec3> midpoints);
// Drops the stack when its arrays no longer match the vertex count (keeping Mesh::colors,
// the flattened look), e.g. after a topology change that doesn't know about paint layers.
void sanitizePaintLayers(Mesh& mesh);

// paint_layer_add, paint_layer_set, paint_layer_select, paint_layer_duplicate, paint_layer_delete,
// paint_layer_move, paint_layer_merge, paint_layer_flatten, paint_layer_clear, paint_layer_list.
void registerPaintLayerCommands(CommandRegistry& registry);

} // namespace sf
