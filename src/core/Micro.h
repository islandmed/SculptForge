// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Micro-displacement levels: hundreds of millions of sculptable triangles without a mesh that size.
//
// Above the top subdivision level a SubTool can carry a micro level (Mesh::micro). The surface is then
//
//     micro point = S(t, i, j) + d(t, i, j) * N(t, i, j)
//
// for every base triangle t (the mesh itself: its current, highest subdivision level) and every point (i, j) of
// a regular grid of n = 2^level segments per base edge on it (n^2 micro triangles per base triangle):
//  - S is the base triangle's Phong-tessellated surface (Boubekeur & Alexa 2008, shape factor 3/4): the flat
//    point pulled towards the tangent planes of the three corners, which is smooth across base edges and needs
//    nothing but the base positions and normals;
//  - N is the interpolated base vertex normal (normalised);
//  - d is ONE scalar per grid point, stored as a 16-bit integer times `step` (world units).
// Micro triangles are never stored: positions are generated on the fly (brushes on the CPU, the renderer in its
// vertex shader). Memory is 2 bytes per grid point (+1 with a mask): 300 million micro triangles at level 4 on
// 1.2 million base triangles are about 360 MB, against roughly 30 GB as a plain mesh.
//
// Each base triangle keeps its whole grid, so the points on a base edge exist once per triangle beside it (and a
// corner once per triangle around it): about 20% extra at level 4 for grids that are contiguous per triangle (a
// brush, an undo tile and a GPU upload are one memcpy-able run each). After every edit the copies are made equal
// again ("stitching"), so the surface stays watertight. Positions on shared edges are computed from the base
// corners in ascending vertex-index order on the CPU and GPU alike, so both sides of an edge get bit-identical
// points even where the compiler fuses multiply-adds.
//
// Grid layout per triangle (corners c0, c1, c2 = triangle.x, .y, .z): point (i, j) with i, j >= 0, i + j <= n
// has barycentric weights ((n - i - j) / n, i / n, j / n); row j holds n + 1 - j points, so its index is
// j * (n + 1) - j * (j - 1) / 2 + i. Corner c0 = (0, 0), c1 = (n, 0), c2 = (0, n). Edge e runs from corner e to
// corner e + 1: e0 = (k, 0), e1 = (n - k, k), e2 = (0, n - k), k = 0..n.
//
// What a scalar displacement can't do: move points sideways. Brushes that slide the surface (Move's sideways
// part, Snake Hook, Nudge, VDM stamps with overhangs) work on real subdivision levels; on a micro level Pinch
// sharpens the height profile instead (see microBrushSupport).
#pragma once

#include "core/Mesh.h"
#include "core/MicroStore.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace sf {

class BVH;
class CommandRegistry;
class FocusDetail;
struct Document;
struct RayHit;
enum class BrushType;

struct MicroMesh {
    static constexpr int kMaxLevel = 7;       // 128 x 128 segments per base triangle (8,385 points)
    static constexpr float kPhong = 0.75f;    // Phong tessellation shape factor
    static constexpr uint32_t kNone = ~0u;

    int level = 0;            // n = 2^level segments per base edge
    uint32_t n = 1;
    uint32_t samples = 3;     // grid points per base triangle: (n + 1)(n + 2) / 2
    float step = 1e-6f;       // world units per integer unit of `disp`
    uint32_t baseTriangles = 0, baseVertices = 0;
    MicroStore disp;                // baseTriangles * samples (in memory or out of core: core/MicroStore.h)
    std::vector<uint8_t> mask;      // empty, or one per grid point (0 = free, 255 = masked)
    std::vector<uint16_t> triRange; // per base triangle: the largest |disp| (an upper bound; brushes find triangles with it)
    uint16_t rangeMax = 0;          // the largest triRange (an upper bound)
    float bulgeMax = 0.0f;          // the most S leaves a base triangle's plane (world units, an upper bound)
    float baseEdge = 0.0f;          // the base's average edge length

    // Base topology this level needs (derived, not saved; rebuilt when the base changes): per triangle edge the
    // neighbouring triangle (tri << 2 | its edge), or kNone on a boundary.
    std::vector<uint32_t> edgeNeighbor;

    // What changed since the renderer last looked: per base triangle a bit, or everything.
    std::vector<uint64_t> dirtyBits;
    bool allDirty = true;
    bool maskDirty = false; // the mask was added or removed (the renderer's shading changes everywhere)
    bool stroking = false;  // a stroke is running (TierConfig draft normals: the renderer shades less meanwhile)
    bool rescaled = false;  // `step` grew and every value was rescaled (same surface: the renderer re-sends the values only)
    uint64_t revision = 0;  // bumped by every edit
    // Which level this is (new on Divide / load; kept by copies, which hold the same values): the renderer keeps
    // its GPU copy while the serial stays.
    uint64_t serial = 0;
    static uint64_t newSerial();
    // Focus Detail (core/FocusDetail.h, prototype): set when the level streams from a detail file. Tiles outside
    // the focus hold a coarse grid; brushes and undo make the tiles they change full first (ensureFull).
    std::shared_ptr<FocusDetail> focus;

    size_t microTriangles() const { return size_t(baseTriangles) * n * n; }
    size_t microVertices() const; // unique grid points (shared edges and corners counted once)
    size_t bytes() const;
    bool hasMask() const { return !mask.empty(); }
    float rangeWorld() const { return 32767.0f * step; } // the largest |d| `step` can hold

    static uint32_t index(uint32_t i, uint32_t j, uint32_t n) { return j * (n + 1) - j * (j - 1) / 2 + i; }
    // The grid point of edge e at k (from corner e towards corner e + 1).
    static void edgePoint(int e, uint32_t k, uint32_t n, uint32_t& i, uint32_t& j) {
        if (e == 0) i = k, j = 0;
        else if (e == 1) i = n - k, j = k;
        else i = 0, j = n - k;
    }
    void markDirty(uint32_t tri) { dirtyBits[tri >> 6] |= uint64_t(1) << (tri & 63); }
    // Grows `step` (by powers of two, at least 4x) until +/-`need` world units fit, rescaling every value (each
    // loses its lowest bits). Returns the divisor applied (1 = it fitted already).
    int widen(float need);
    void markAllDirty() { allDirty = true; }
    void clearDirty();
    // Builds edgeNeighbor for `base` (if it doesn't fit already).
    void ensureTopology(const Mesh& base);
};

// --- The two-tier engine's vocabulary ---
// BaseCage: the coarse surface the brushes for big forms (Move, transforms) deform: the SubTool's mesh, its highest
// subdivision level. Its polygons are triangles (a quad of the cage is a triangle pair, one patch each).
using BaseCage = Mesh;
// MicroPatch: one base triangle's regular grid of scalar heights (n + 1)(n + 2) / 2, implicit topology (no index
// buffer anywhere), heights 16-bit in memory and packed to their range (0-16 bits) on disk and in undo steps.
struct MicroPatch {
    uint32_t id = 0;   // the base triangle
    uint32_t n = 0;    // segments per edge
    int16_t* heights = nullptr;
    uint8_t* mask = nullptr; // null without a mask
    size_t points() const { return size_t(n + 1) * (n + 2) / 2; }
};
MicroPatch microPatch(Mesh& m, uint32_t id);
// PatchPageTable: which patches are resident. In memory every patch is; out of core (MicroStore::mapped) a patch
// is resident when its pages are, cold ones live in the sparse file (untouched = a hole) and come back on touch;
// the operating system's page cache keeps the LRU order and trim() enforces TierConfig::residentCacheCapMB.
class PatchPageTable {
public:
    explicit PatchPageTable(MicroMesh& mm) : mm_(mm) {}
    bool resident(uint32_t patch) const;
    size_t residentPatches() const;
    size_t residentBytes() const { return mm_.disp.residentBytes(); }
    size_t trim(size_t maxBytes) { return mm_.disp.trim(maxBytes); }

private:
    MicroMesh& mm_;
};

// The base triangle's corners and normals, sorted by vertex index, for evaluating its grid points.
struct MicroTriangle {
    glm::vec3 p[3], nrm[3]; // in vertex-index order
    uint8_t slot[3];        // which corner (0..2 of the triangle) each is
    float invN = 1.0f;
    uint32_t n = 1;
    void set(const Mesh& base, uint32_t tri, uint32_t n);
    // The reference surface S and direction N at grid point (i, j).
    void eval(uint32_t i, uint32_t j, glm::vec3& surface, glm::vec3& normal) const;
    // ... and at barycentric weights of corners 0..2 (sum 1).
    void evalWeights(const float w[3], glm::vec3& surface, glm::vec3& normal) const;
    // The flat point (no Phong) at grid point (i, j): what displacement maps are measured from.
    glm::vec3 flat(uint32_t i, uint32_t j) const;
};

// --- Queries ---
inline bool hasMicro(const Mesh& m) { return m.micro != nullptr; }
int microLevel(const Mesh& m); // 0 = none
size_t microTriangleCount(const Mesh& m);
size_t microBytes(const Mesh& m);
// "Micro 4 (300.0M)" or "".
std::string microText(const Mesh& m);
// True when the micro level still fits the mesh (base triangle and vertex counts).
bool microMatches(const Mesh& m);
// The micro level to change in place: copied first when another Mesh (an undo snapshot, a copy) shares it.
MicroMesh& mutableMicro(Mesh& m);
// The most micro triangles this computer is allowed to make (RAM: about 4 bytes a grid point with the GPU's share).
size_t microMaxTriangles();
// The level that brings the mesh closest to `targetTriangles` micro triangles (1..kMaxLevel).
int microLevelFor(const Mesh& m, size_t targetTriangles);

// The micro surface point at grid point (i, j) of base triangle t (reads the stored displacement).
glm::vec3 microPoint(const Mesh& m, uint32_t tri, uint32_t i, uint32_t j);
// The micro surface at barycentric weights of base triangle t (displacement interpolated on the micro triangle
// holding the point), with its geometric normal.
glm::vec3 microPointAt(const Mesh& m, uint32_t tri, const float w[3], glm::vec3* normal = nullptr);

// --- Operations (no undo; see the commands) ---
// Adds a micro level of `level` (1..kMaxLevel) to the mesh: its current level must be the highest (or it has no
// subdivision levels). The surface starts as S (smooth, d = 0).
// `outOfCore`: 1 = keep the displacements in a memory-mapped file (core/MicroStore.h), 0 = in memory, -1 = decide
// (out of core when they'd take more than a quarter of this computer's memory, or SF_MICRO_OUT_OF_CORE=1).
bool microDivide(Mesh& m, int level, std::string& error, int outOfCore = -1);
// The -1 rule of microDivide for `points` grid points.
bool microWantsOutOfCore(size_t points);
// Removes the micro level.
void microDelete(Mesh& m);
// Gives a loaded mesh (with normals) a micro level from saved data: `disp` holds baseTriangles * samples values
// (`mask` that many or none). Works out the derived data (ranges, bulge, edge neighbours).
bool microAttach(Mesh& m, int level, float step, MicroStore disp, std::vector<uint8_t> mask, std::string& error);
// A regular mesh of the micro surface at micro level `level` (<= the micro level): shared points welded, the base
// triangles' UVs, polygroups and hidden flags carried to their micro triangles, mask from the micro mask. Fails
// when it would exceed `maxTriangles` (0 = what subdivision Divide allows on this computer).
bool microBakeMesh(const Mesh& m, int level, Mesh& out, std::string& error, size_t maxTriangles = 0);
// Displacement maps over the base UVs (needs UVs): `vector` = object-space offset of the micro surface from the
// flat base triangles (RGB), else the height along the interpolated normal from the flat base triangles.
// `format` "exr" (32-bit float, world units) or "png" (16-bit; scalar: 32768 = 0 and +/-`scaleOut` at white /
// black; vector: each channel likewise). Writes `path`.
bool microExportDisplacement(const Mesh& m, const std::string& path, int size, bool vector, const std::string& format,
                             float& scaleOut, std::string& error);
// Adds procedural detail (value noise of `frequency` cycles per unit, `amplitude` world units) over the whole
// micro surface, for demos and benchmarks.
void microNoise(Mesh& m, float amplitude, float frequency, uint32_t seed = 1);

// Ray against the micro surface: the base triangle hit (BVH) and its neighbourhood within the displacement range,
// at full micro resolution. hit.triangle is the base triangle; hit.position / normal are on the micro surface.
bool microRaycast(const Mesh& m, const BVH& bvh, const glm::vec3& origin, const glm::vec3& dir, RayHit& hit);
// bvh.raycast, or microRaycast on a mesh with a micro level: what brushes and the cursor pick.
bool surfaceRaycast(const Mesh& m, const BVH& bvh, const glm::vec3& origin, const glm::vec3& dir, RayHit& hit);

// --- Shading for the renderer: per grid point an octahedral normal (2 x 8 bits), curvature (8 bits,
// sign(c) sqrt|c| like the mesh shader's) and mask (8 bits), packed little-endian in a uint32. ---
class MicroShader {
public:
    // Recomputes what changes when base triangles `dirty` changed (all of them when `all`): the shading of those
    // and of every base triangle sharing a vertex with one (whose shared edges and corners average with them).
    // out(tri) gives where that triangle's `samples` values go (each written whole). `written` lists them.
    // `prepare`, if given, sees the triangles about to be written (before any out() call), once per batch.
    using Prepare = std::function<void(std::span<const uint32_t> tris)>;
    void shade(const Mesh& m, std::span<const uint32_t> dirty, bool all, std::vector<uint32_t>& written,
               const std::function<uint32_t*(uint32_t tri)>& out, const Prepare& prepare = {});

private:
    // shade() for the triangles `seeds` (grown by the triangles sharing a vertex when `grow`); appends to `written`.
    void shadeSet(const Mesh& m, std::span<const uint32_t> seeds, bool grow, std::vector<uint32_t>& written,
                  const std::function<uint32_t*(uint32_t tri)>& out, const Prepare& prepare);
    std::vector<uint32_t> seed_, grown_;
    std::vector<uint32_t> stamp_, slot_; // per base triangle: in a set of this call (stamp), its partials (slot)
    std::vector<uint32_t> vstamp_;       // per base vertex
    uint32_t epoch_ = 0;
    std::vector<uint32_t> ring_, ring2_;
    std::vector<float> partial_;         // per slot: 3n edge points x 6 floats
};

// --- Brushes (driven by Sculptor, which calls these when the mesh has a micro level) ---

// Whether a brush works on micro levels: 2 = as on a mesh, 1 = approximated (Pinch, Crease and Dam Standard
// sharpen instead of pulling sideways; Move moves along the normal), 0 = not at all.
int microBrushSupport(BrushType type);

struct BrushAlpha;
struct MicroUndo;
class MicroStroke {
public:
    void begin(Mesh& m);
    // One dab (one mirror) of a displacement brush, or the mask brush (`maskBrush`: raises the mask to 1, or
    // lowers it with invert). `alpha` (null/empty = round) shapes a displacement dab as on a mesh, its top edge
    // toward `alphaUp` (#92).
    void dab(Mesh& m, const BVH& bvh, BrushType type, const glm::vec3& center, const glm::vec3& viewDir, float radius,
             float strength, bool invert, float focal, const glm::vec3& alphaUp, bool maskBrush, float maskStrength,
             const BrushAlpha* alpha = nullptr);
    // Move: captures the points within the dab (with each mirror's signs), then moves them along their normals.
    void beginGrab(Mesh& m, const BVH& bvh, const glm::vec3& center, float radius, float focal, const glm::mat3& mirror);
    void grab(Mesh& m, const glm::vec3& delta);
    // Puts every grid point the stroke changed back (the stroke stays open).
    void restore(Mesh& m);
    // The stroke's undo step (null when nothing changed).
    std::unique_ptr<MicroUndo> end(Mesh& m);
    bool active() const { return active_; }
    // Scale-adaptive brushing: a dab is evaluated on grid points about radius / kBrushSamples apart (a power-of-two
    // stride of the micro grid) and interpolated in between, so its evaluation cost doesn't grow with the micro
    // level. Off: every grid point (tests compare).
    static constexpr int kBrushSamples = 48;
    bool adaptive = true;
    uint32_t lastStride = 1; // the stride the last dab used
    // Timings of the last dabs (ms): finding the triangles + saving undo tiles, the kernel, stitching.
    double findMs = 0, kernelMs = 0, stitchMs = 0;
    uint64_t points = 0; // grid points visited

private:
    void touch(MicroMesh& mm, std::span<const uint32_t> tris);
    void widen(MicroMesh& mm, float need); // MicroMesh::widen, keeping this stroke's saved tiles in step
    void stitch(const Mesh& m, MicroMesh& mm, std::span<const uint32_t> tris);
    bool active_ = false;
    float strokeStep_ = 0.0f;
    std::vector<uint32_t> tileOf_;        // per base triangle: its undo tile + 1 (0 = not yet touched)
    std::vector<uint32_t> tiles_;         // the touched base triangles, in order
    std::vector<int16_t> oldDisp_;        // tiles_.size() * samples
    std::vector<uint8_t> oldMask_;        // likewise, while the mask exists
    bool maskAdded_ = false;              // the stroke allocated the mask
    std::vector<uint32_t> cand_;          // this dab's base triangles
    std::vector<uint32_t> dabStamp_;      // per base triangle: == dabEpoch_ when in cand_
    uint32_t dabEpoch_ = 0;
    std::vector<uint32_t> vertStamp_;
    std::vector<uint32_t> corners_;
    float baseEdge_ = 0.0f; // the base's average edge length
    // Move: per mirror its triangles and, per triangle, weight and direction of its coarse grid points.
    struct GrabMirror {
        glm::mat3 mirror{1.0f};
        uint32_t stride = 1, firstTri = 0, tris = 0;
    };
    std::vector<GrabMirror> grabMirrors_;
    std::vector<uint32_t> grabTris_;
    std::vector<float> grabWeight_;
    std::vector<glm::vec3> grabNormal_;
};

// One micro step: the old grid points of the base triangles a stroke touched (in units of `step`), or a whole
// micro level swapped in or out (Divide to micro, Delete micro: `whole`, with `swapWhole`).
//
// Strokes keep a journal (`journal`): the touched tiles sorted by a Morton code of their centre (`keys`, so the
// journal of a region is contiguous and can be looked up), each as the stroke's change (new - old, stored as the
// difference to the point before it in its row, or below it for a row's first) packed with the tile codec (core/MicroStore.h): untouched points cost nothing, a smooth change a few bits
// per point, so a typical stroke's step is tens of KB instead of 2 bytes per point. `applied`: the change is in
// the mesh (undo subtracts it, redo adds it back).
struct MicroUndo {
    float step = 0.0f;
    uint32_t samples = 0;
    std::vector<uint32_t> tris;
    std::vector<int16_t> disp;       // whole-tile old values (no journal)
    bool journal = false;
    bool applied = true;
    std::vector<uint64_t> keys;      // journal: Morton code of each tile's centre, ascending
    std::vector<uint8_t> packed;     // journal: the tiles' changes (each point less its predecessor), packed
    std::vector<int16_t> firsts;     // journal: each tile's first change
    std::vector<uint8_t> mask;
    bool maskAdded = false; // the step allocated the mask: undo frees it again (redo brings it back)
    bool swapWhole = false;
    std::shared_ptr<MicroMesh> whole;
    void swapWith(Mesh& m);
    size_t bytes() const;
};

// --- Undoable document operations ---
bool microDivideUndoable(Document& doc, int level, std::string& message);
bool microDeleteUndoable(Document& doc, std::string& message);

// micro_divide, micro_delete, micro_info, micro_bake, micro_export_displacement, micro_noise.
void registerMicroCommands(CommandRegistry& registry);

} // namespace sf
