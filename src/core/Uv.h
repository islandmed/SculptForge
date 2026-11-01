// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// UV editing (the UV Editor, app/PanelUvEditor.cpp, and the uv_* commands, core/UvCommands.cpp).
//
// UVs are stored per triangle corner (Mesh::uvs). Two corners of the same vertex are one *UV vertex* when an
// edge between their triangles is connected in UV space: both triangles have the same UVs at both ends of the
// edge and it isn't a marked seam (Mesh::uvFlags, kUvSeam). *Islands* are the triangles connected that way.
// Moving a UV vertex moves all of its corners, so islands stay connected while they are edited.
//
// Tiles follow the UDIM convention: tile 1001 is u, v in [0, 1), 1002 is u in [1, 2), ten tiles per row, so
// tile = 1001 + floor(u) + 10 * floor(v).
//
// Everything here works on plain arrays and runs on all cores, so it stays interactive on million-triangle
// meshes; nothing touches the GPU or the UI.
#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace sf {

class Mesh;

constexpr uint8_t kUvSeam = 1; // Mesh::uvFlags: the edge from this corner to the next is a marked seam
constexpr uint8_t kUvPin = 2;  // Mesh::uvFlags: this corner's UV is pinned (Unwrap keeps it)
constexpr uint32_t kNoCorner = ~0u;

inline uint32_t cornerNext(uint32_t c) { return c - c % 3 + (c % 3 + 1) % 3; }
inline uint32_t cornerPrev(uint32_t c) { return c - c % 3 + (c % 3 + 2) % 3; }

// UDIM tile number of a UV point, and the lower-left corner of a tile.
int udimTile(const glm::vec2& uv);
glm::vec2 udimOrigin(int tile);

// Per corner c (the edge from c to cornerNext(c)): the corner of the other triangle that starts the same edge
// the other way round, or kNoCorner on an open or non-manifold edge. Depends on the triangles only.
std::vector<uint32_t> cornerTwins(const Mesh& mesh);

// UV connectivity of a mesh with UVs (see the top of this file).
struct UvTopology {
    std::vector<uint32_t> twin;     // cornerTwins
    std::vector<uint32_t> uvVertex; // per corner: its UV vertex
    uint32_t uvVertexCount = 0;
    std::vector<uint32_t> island;   // per triangle: its island (0 .. islandCount - 1)
    uint32_t islandCount = 0;
    std::vector<uint32_t> islandOffsets, islandTriangles; // triangles of island i: [offsets[i], offsets[i + 1])
    std::vector<uint8_t> border;    // per corner: its edge is an island border (seam, open edge or UV split)
    bool connected(uint32_t corner) const { return !border[corner]; }
};
// `twin` may be passed in (cornerTwins of the same triangles) to save its computation.
UvTopology buildUvTopology(const Mesh& mesh, std::vector<uint32_t> twin = {});

// --- Measures ---

// Texel density: texels per unit of surface length for a `resolution`-texel texture (the UVs' square root of
// the area ratio). Per triangle (0 for degenerate ones), and per island (its area-weighted mean).
void uvTriangleDensity(const Mesh& mesh, int resolution, std::vector<float>& out);
std::vector<float> uvIslandDensity(const Mesh& mesh, const UvTopology& topo, int resolution);
// The texel density of the whole mesh (total UV area over total surface area).
float uvMeshDensity(const Mesh& mesh, int resolution);

// Stretch of each triangle's mapping from the surface to UV space, from the singular values s1 >= s2 of its
// Jacobian, measured against its island's mean scale: 0 = no stretch, 1 = twice or half as long in some
// direction (log2 of the worst of s1 and 1/s2 over the mean). Triangles folded over their island get -1.
void uvTriangleStretch(const Mesh& mesh, std::vector<float>& out);
// The same for one triangle, with each island's scale and sense computed once (uvStretchCache) so a drag can
// update the triangles it moves without measuring everything again.
struct UvStretchCache {
    std::vector<float> scale;      // per island
    std::vector<uint8_t> mirrored; // per island
};
UvStretchCache uvStretchCache(const Mesh& mesh, const UvTopology& topo);
float uvTriangleStretchAt(const Mesh& mesh, uint32_t triangle, const UvTopology& topo, const UvStretchCache& cache);
// Summary for the status line and the tests: area-weighted mean stretch, and the share of flipped triangles.
struct UvStretchStats {
    float mean = 0.0f, worst = 0.0f;
    float flipped = 0.0f; // fraction of the UV area
};
UvStretchStats uvStretchStats(const Mesh& mesh);
// Heat map colour (sRGB 0..1) of x in [-1, 1]: blue (-1, too few texels / compressed) through green (0) to red
// (1, too many / stretched).
glm::vec3 uvHeatColor(float x);

// --- Selection ---

enum class UvSelectMode { Vertex, Edge, Face, Island };
const char* uvSelectModeId(UvSelectMode mode); // "vertex", "edge", "face", "island"
bool parseUvSelectMode(const std::string& id, UvSelectMode& mode);

// What the UV Editor has selected, on the active mesh: faces (Face and Island modes) or corners (Vertex mode: the
// corners of the selected UV vertices; Edge mode: the corners that start the selected edges). Sizes that don't
// match the mesh mean nothing is selected.
struct UvSelection {
    UvSelectMode mode = UvSelectMode::Island;
    std::vector<uint8_t> faces;   // per triangle
    std::vector<uint8_t> corners; // per corner
    void clear() {
        std::vector<uint8_t>().swap(faces);
        std::vector<uint8_t>().swap(corners);
    }
    bool valid(const Mesh& mesh) const;
    size_t count() const; // selected faces or corners
    bool empty() const { return count() == 0; }
};

// The corners the selection moves: every corner of every UV vertex it touches (all corners of the selected
// faces in the face modes). Sorted.
std::vector<uint32_t> uvSelectedCorners(const Mesh& mesh, const UvTopology& topo, const UvSelection& sel);
// The islands the selection touches (all of them when `allIfEmpty` and nothing is selected), as flags.
std::vector<uint8_t> uvSelectedIslands(const Mesh& mesh, const UvTopology& topo, const UvSelection& sel, bool allIfEmpty);
// Grows face / corner flags so every element of a touched island is selected (Island mode), or converts the
// selection between modes (the elements of the selected ones).
void uvConvertSelection(const Mesh& mesh, const UvTopology& topo, UvSelection& sel, UvSelectMode to);
// Faces whose UV triangle lies in (or crosses, `touch`) the rectangle lo..hi; used by box select.
void uvFacesInRect(const Mesh& mesh, glm::vec2 lo, glm::vec2 hi, bool touch, std::vector<uint32_t>& out);

// --- Editing ---

// Moves `corners` by the affine map p -> m * (p - pivot) + pivot + offset.
void uvTransform(std::vector<glm::vec2>& uvs, const std::vector<uint32_t>& corners, const glm::mat2& m, glm::vec2 pivot,
                 glm::vec2 offset);
// Bounding box centre of `corners`.
glm::vec2 uvCenter(const std::vector<glm::vec2>& uvs, const std::vector<uint32_t>& corners);

// Marks (`on`) or clears the seam on the edges starting at `corners`, on both sides of each edge.
void uvSetSeams(Mesh& mesh, const std::vector<uint32_t>& twin, const std::vector<uint32_t>& corners, bool on);
// Seam presets: every polygroup border, every current island border, edges sharper than `degrees`.
size_t uvSeamsFromPolygroups(Mesh& mesh, const std::vector<uint32_t>& twin);
size_t uvSeamsFromIslands(Mesh& mesh, const UvTopology& topo);
size_t uvSeamsFromAngle(Mesh& mesh, const std::vector<uint32_t>& twin, float degrees);
size_t uvSeamCount(const Mesh& mesh); // marked edges (each counted once)
void uvClearSeams(Mesh& mesh);
// The corners on the border of the face set `faces` (edges whose other side isn't in it).
std::vector<uint32_t> uvFaceSetBorder(const Mesh& mesh, const std::vector<uint32_t>& twin, const std::vector<uint8_t>& faces);
// The shortest edge path from vertex `a` to vertex `b` over the surface, as the corners that start its edges
// (empty when they aren't connected). For marking seams by clicking along a loop in 3D.
std::vector<uint32_t> uvEdgePath(const Mesh& mesh, uint32_t a, uint32_t b);

// Pins (`on`) or unpins `corners`.
void uvSetPins(Mesh& mesh, const std::vector<uint32_t>& corners, bool on);

// Weld: every mesh vertex among `corners` gets one UV (the average of its corners there) and the edges between
// them lose their seams. Returns the vertices welded.
size_t uvWeld(Mesh& mesh, const UvTopology& topo, const std::vector<uint32_t>& corners);
// Split: the border of the selected faces (Face / Island modes), the selected edges (Edge mode) or the edges
// between selected vertices (Vertex mode) become seams, so the faces can move apart. Returns the edges cut.
size_t uvSplit(Mesh& mesh, const UvTopology& topo, const UvSelection& sel);
// Straighten: the selected UV vertices (an edge loop, say) line up on one horizontal or vertical line (whichever
// is closer to how they run), keeping their order along it. Returns the vertices moved.
size_t uvStraighten(Mesh& mesh, const UvTopology& topo, const std::vector<uint32_t>& corners);
// Flip the islands in `islands` in u (horizontal) or v about their centres.
void uvFlipIslands(Mesh& mesh, const UvTopology& topo, const std::vector<uint8_t>& islands, bool horizontal);

// Texel density: scales each island in `islands` about its centre to `density` texels per unit (at
// `resolution`). Returns the islands scaled.
size_t uvSetDensity(Mesh& mesh, const UvTopology& topo, const std::vector<uint8_t>& islands, float density, int resolution);

// Trim sheets: horizontal strips of the texture, each a band of v (in the tile 0..1). Islands assigned to a strip
// are turned lengthwise, scaled to the strip's height (less `margin` on each side, in UV units) and laid side
// by side along u from 0; with `fitWidth` the row is then squeezed or stretched in u to fill 0..1 exactly,
// otherwise it keeps its proportions (and may run past 1: trim textures repeat in u).
struct TrimStrip {
    float v0 = 0.0f, v1 = 0.25f;
    std::string name;
};
// "4" (four equal strips), "0.5,0.25,0.25" (heights from the top, normalised), or "0-0.5;0.5-0.75" (v ranges).
bool parseTrimStrips(const std::string& text, std::vector<TrimStrip>& out, std::string& error);
std::vector<TrimStrip> equalTrimStrips(int count);
size_t uvTrimIslands(Mesh& mesh, const UvTopology& topo, const std::vector<uint8_t>& islands, const TrimStrip& strip,
                     bool fitWidth, float margin);

// --- Packing (core/UvPack.cpp) ---

struct UvPackOptions {
    int resolution = 2048;    // texture size the margin is measured in
    float marginPx = 8.0f;    // texels between islands (and half of it at tile borders)
    bool rotate = true;       // turn islands to their smallest bounding box and allow 90-degree turns
    int tiles = 1;            // UDIM tiles to fill (1001, 1002, ... ten per row); ignored with `density`
    bool equalDensity = true; // scale islands to one texel density first (off: keep their relative sizes)
    float density = 0.0f;     // > 0: keep exactly this texel density (texels per unit) and use as many tiles as
                              // it takes (up to 100) instead of fitting `tiles`
    int firstTile = 1001;     // where packing starts
};
struct UvPackResult {
    size_t islands = 0;
    int tilesUsed = 0;
    float coverage = 0.0f; // UV area of the islands over the area of the tiles used
    float density = 0.0f;  // texel density after packing (the mean)
};
// Packs the islands in `islands` (all when empty) of `mesh` per `options`. Islands not packed stay where they
// are (and may then overlap the packed ones). False with `error` when nothing could be packed.
bool uvPack(Mesh& mesh, const UvTopology& topo, const std::vector<uint8_t>& islands, const UvPackOptions& options,
            UvPackResult& result, std::string& error);

// Lower-level packing of rectangles into unit tiles (exposed for the tests): sizes in, positions and 90-degree
// turns out; returns the tiles used (0 when something doesn't fit even alone). Rectangles already include
// their margin; `border` is kept free along each tile edge.
int packRectangles(const std::vector<glm::vec2>& sizes, int maxTiles, bool allowTurn, float border,
                   std::vector<glm::vec2>& positions, std::vector<uint8_t>& turned, std::vector<int>& tile);

// --- Unwrapping (core/UvUnwrap.cpp) ---

enum class UvUnwrapMethod {
    Auto,       // xatlas: finds its own seams (core/AutoUv.h)
    Conformal,  // LSCM: angle-preserving, cut along the marked seams
    Isometric,  // LSCM then as-rigid-as-possible: keeps lengths too (least stretch), along the marked seams
    Polygroups, // Isometric, cut along the polygroup borders as well as the marked seams
    Planar,     // each piece projected along its mean normal
    Unfold,     // the current islands flattened again in place (Isometric, honouring pins)
};
const char* uvUnwrapMethodId(UvUnwrapMethod m); // auto, conformal, isometric, polygroups, planar, unfold
const char* uvUnwrapMethodLabel(UvUnwrapMethod m);
bool parseUvUnwrapMethod(const std::string& id, UvUnwrapMethod& m);

struct UvUnwrapOptions {
    UvUnwrapMethod method = UvUnwrapMethod::Isometric;
    int resolution = 2048;
    int iterations = 10;          // as-rigid-as-possible steps (Isometric, Polygroups, Unfold)
    bool pack = true;             // pack the new islands (UvPackOptions below); off for Unfold (fitted in place)
    UvPackOptions packing;
    const std::vector<uint8_t>* faces = nullptr; // only these triangles (null: all); the rest keep their UVs and
                                                  // the new islands are fitted where the old ones were
};
struct UvUnwrapResult {
    std::vector<glm::vec2> uvs; // the mesh's new UVs (every corner)
    size_t islands = 0;         // pieces flattened
    size_t extraCuts = 0;       // edges cut automatically so closed or looped pieces could flatten
    UvPackResult packing;
};
using UvProgress = std::function<bool(float fraction)>; // false: cancel
// Unwraps `mesh` (its UVs, seams and pins are read; nothing is written). False with `error` on failure or cancel.
bool uvUnwrap(const Mesh& mesh, const UvUnwrapOptions& options, UvUnwrapResult& result, std::string& error,
              const UvProgress& progress = {});

// The flattening solvers on their own (for the tests): `positions` / `triangles` form one piece that must be
// a topological disk. Pinned vertices (`pins`, with their UVs in `uv`) stay; with fewer than two, two are chosen.
// `uv` holds a starting guess when its size matches. Returns false if the piece isn't a disk.
bool flattenConformal(const std::vector<glm::vec3>& positions, const std::vector<glm::uvec3>& triangles,
                      std::vector<glm::vec2>& uv, const std::vector<uint32_t>& pins = {});
bool flattenIsometric(const std::vector<glm::vec3>& positions, const std::vector<glm::uvec3>& triangles,
                      std::vector<glm::vec2>& uv, int iterations, const std::vector<uint32_t>& pins = {});

// --- Commands (core/UvCommands.cpp) ---

class CommandRegistry;
void registerUvCommands(CommandRegistry& registry); // uv_unwrap, uv_pack, uv_seam, uv_select, uv_transform, ...
// One line about a mesh's UVs (islands, seams, pins, density, tiles, stretch), as uv_info prints it.
std::string uvInfoText(const Mesh& mesh, int resolution);

} // namespace sf
