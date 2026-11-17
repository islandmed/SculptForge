// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Adaptive Detail tiles, step 0 (the 10x scoreboard's axis 2, briefs/forge-ink-10x.md; design in the branch's REPORT):
// a standalone prototype of the TILE STORE, no product code, that answers the design critique's question "what breaks
// first at 10x on a shared-RAM laptop": tile memory, the undo ring, or re-resolving + uploading the pages a dab dirtied.
//
// Model: the bench head at micro level 5 as the dense ROOT (the core-200m scene's base and level: today's shipped
// storage). On top: per base triangle a sparse quadtree of DELTA tiles. A tile at depth d covers one sub-triangle of
// the base triangle at tree depth d + 1 (4^(d + 1) per base triangle; the root's n = 32 = 2 tiles of kTileN = 16 across,
// so depth 0 would be the root's own resolution: tiles start at depth 1, cell = root cell / 2^d) and holds a regular
// 16-segment grid (153 int16 points) of the DELTA from the linear interpolation of the next coarser level; untouched =
// no tile. A dab writes at ITS depth only: d = max(d_r, d_view), d_r from cell <= r / 48 (today's sampling spacing,
// critique D), d_view from cell <= 1 px at the view the dab was placed from (MicroDab::pxPerUnit); d <= 0 writes the
// root (today's level-5 write). A coarse dab over fine tiles therefore writes the coarse grid only: the deltas below
// ride along (the cost check in the table: the same dab over an untouched and over a fully refined area).
//
// Scenario (the spec in REPORT.md "M2 step 0"): passes of 30 strokes x 24 dabs, each pass a brush size and a view:
// 240 px at the whole-head view (the "finished surface": the dabs cover the head), then 60 px at 4x, 16x and 64x
// closer over one region (deeper tiles), then undo 10 strokes. Measured and printed as Markdown: tiles alive, MB
// (tiles / undo ring), peak RSS; per dab: tiles touched, points written, ms; the PAGE RE-RESOLVE of one dab = for the
// clusters (256 base triangles in Morton order, as MicroRenderer) a 1600x1000 floor view draws at 12 px per micro edge,
// the ms (4 threads) to sum root + delta chain into an absolute int16 grid at the drawn level for every base triangle
// the dab dirtied, and the bytes that upload would move; and a 4-thread LOD walk over all clusters (and, as the
// mega-poly spike's wall, over every base triangle). Bars: RSS < 4 GB with 1B-equivalent spacing in brushed areas (depth
// >= 2 on this root), re-resolve + upload of one dab < 16 ms, walk < 5 ms. The first bar missed names M2's priority.
//
//   sf_tiles_probe_bench [baseTriangles=195313] [level=5] [strokes=30] [zooms=1,4,16,64] [--px-rule=1.0] [--cover=D]
//
// --cover=D (the critique's "live document growth" wall, 11-16): after the passes, EVERY tile of depth D over the whole
// head is allocated (a finished pore pass everywhere), and the store's MB and RSS are printed: tiles win by sparsity,
// not compression (fully covered they cost ~1.2x a dense grid of the same spacing), so this is the number a tile
// budget (park / page out what the view doesn't show) has to be designed against.
#include "core/BVH.h"
#include "core/Benchmark.h"
#include "core/Micro.h"
#include "core/MicroStore.h"
#include "core/ThreadPool.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

using namespace sf;
using Clock = std::chrono::steady_clock;

namespace {

constexpr uint32_t kTileN = 16, kTileSamples = (kTileN + 1) * (kTileN + 2) / 2; // 153
constexpr int kMaxDepth = 12;
constexpr int kThreads = 4; // the floor tier's cores
constexpr float kViewH = 1000.0f, kViewFill = 0.8f; // a 1600x1000 floor view; the head fills 80 % of its height at zoom 1
constexpr float kDrawPx = 12.0f;                    // the renderer's target: micro edges this long on screen

double ms(Clock::time_point a) { return std::chrono::duration<double, std::milli>(Clock::now() - a).count(); }

// Runs job(b, e) over [0, count) on kThreads threads (a self-contained stand-in for the pool: no product code here).
template <class F> void parallel(size_t count, const F& job) {
    if (count == 0) return;
    const size_t chunk = std::max<size_t>(1, (count + kThreads * 4 - 1) / (kThreads * 4));
    std::atomic<size_t> next{0};
    auto worker = [&] {
        for (;;) {
            const size_t b = next.fetch_add(chunk);
            if (b >= count) return;
            job(b, std::min(count, b + chunk));
        }
    };
    std::vector<std::thread> ts;
    for (int t = 1; t < kThreads; ++t) ts.emplace_back(worker);
    worker();
    for (auto& t : ts) t.join();
}

// --- The triangle quadtree ---
// A sub-triangle of the base triangle is a path of quadrants: 0, 1, 2 = the corner triangles at corners 0, 1, 2; 3 = the
// middle one (inverted). Its barycentric frame: origin o and the two edge vectors u (towards corner 1), v (towards
// corner 2) in the base triangle's barycentric space (w = o + a u + b v, a + b <= 1 for a point (a, b) of the sub-triangle).
struct Frame {
    glm::vec3 o{1, 0, 0}, u{-1, 1, 0}, v{-1, 0, 1}; // the base triangle: o = corner 0
    Frame child(int q) const {
        Frame f;
        if (q == 0) f.o = o, f.u = u * 0.5f, f.v = v * 0.5f;
        else if (q == 1) f.o = o + u * 0.5f, f.u = u * 0.5f, f.v = v * 0.5f;
        else if (q == 2) f.o = o + v * 0.5f, f.u = u * 0.5f, f.v = v * 0.5f;
        else f.o = o + (u + v) * 0.5f, f.u = -u * 0.5f, f.v = -v * 0.5f; // the middle, inverted
        return f;
    }
    glm::vec3 at(float a, float b) const { return o + u * a + v * b; }
};

// A tile key: base triangle (32 bits), depth (4 bits), the quadrant path (2 bits per step, depth + 1 steps).
uint64_t tileKey(uint32_t tri, int depth, uint32_t path) { return uint64_t(tri) << 32 | uint64_t(depth) << 28 | path; }

struct Tile {
    std::array<int16_t, kTileSamples> delta{};
};

struct TileStore {
    std::unordered_map<uint64_t, uint32_t> slotOf; // key -> slot + 1
    std::vector<Tile> pool;
    std::vector<uint32_t> freeSlots;
    std::vector<uint8_t> maxDepth; // per base triangle: the deepest tile (0 = none)
    size_t alive() const { return slotOf.size(); }
    size_t bytes() const { // what the store holds: tiles + the index (~48 B per entry in an unordered_map) + the free list
        return pool.size() * sizeof(Tile) + slotOf.size() * (sizeof(uint64_t) + sizeof(uint32_t) + 40) + freeSlots.size() * 4;
    }
    Tile* find(uint64_t key) {
        auto it = slotOf.find(key);
        return it == slotOf.end() ? nullptr : &pool[it->second - 1];
    }
    // Returns the tile, creating it (born = true) when absent.
    Tile* obtain(uint64_t key, bool& born) {
        auto it = slotOf.find(key);
        if (it != slotOf.end()) return born = false, &pool[it->second - 1];
        uint32_t slot;
        if (!freeSlots.empty()) slot = freeSlots.back(), freeSlots.pop_back(), pool[slot] = Tile();
        else slot = uint32_t(pool.size()), pool.emplace_back();
        slotOf.emplace(key, slot + 1);
        born = true;
        return &pool[slot];
    }
    void release(uint64_t key) {
        auto it = slotOf.find(key);
        if (it == slotOf.end()) return;
        freeSlots.push_back(it->second - 1);
        slotOf.erase(it);
    }
};

// The absolute height (in root units of `step`) of the surface at barycentric w of base triangle `tri`, reading the
// root (linear within its cells) and the delta tiles down to `maxDepth` (each linear within its cells): what a
// resolved page sums. `path`/`frame` descend from the base triangle.
struct Resolver {
    const MicroMesh& mm;
    TileStore& store;
    static float lerpGrid(const int16_t* g, uint32_t n, float a, float b) { // a, b in [0, 1], a + b <= 1 (+eps)
        const float fa = a * float(n), fb = b * float(n);
        uint32_t i = uint32_t(std::min(fa, float(n) - 1e-4f)), j = uint32_t(std::min(fb, float(n) - 1e-4f));
        if (i + j >= n) { // the far edge: step back
            if (i > 0 && (j == 0 || fa - float(i) >= fb - float(j))) --i;
            else if (j > 0) --j;
        }
        const float x = fa - float(i), y = fb - float(j);
        const float g00 = g[MicroMesh::index(i, j, n)], g10 = g[MicroMesh::index(i + 1, j, n)], g01 = g[MicroMesh::index(i, j + 1, n)];
        if (x + y <= 1.0f || i + j + 1 >= n) return g00 + (g10 - g00) * x + (g01 - g00) * y;
        const float g11 = g[MicroMesh::index(i + 1, j + 1, n)];
        // the upper triangle of the cell: corners (1,0), (0,1), (1,1)
        return g11 + (g01 - g11) * (1.0f - x) + (g10 - g11) * (1.0f - y);
    }
    float at(uint32_t tri, const glm::vec3& w, int maxDepth) const {
        float h = lerpGrid(mm.disp.data() + size_t(tri) * mm.samples, mm.n, w.y, w.z);
        const int deepest = std::min<int>(maxDepth, store.maxDepth[tri]);
        if (deepest <= 0) return h;
        Frame f;
        uint32_t path = 0;
        for (int d = 0; d <= deepest; ++d) {
            // which quadrant holds w: solve w = o + a u + b v
            const glm::vec3 r = w - f.o;
            const float det = f.u.y * f.v.z - f.v.y * f.u.z;
            const float a = (r.y * f.v.z - f.v.y * r.z) / det, b = (f.u.y * r.z - r.y * f.u.z) / det;
            int q;
            if (a + b <= 0.5f) q = 0;
            else if (a >= 0.5f) q = 1;
            else if (b >= 0.5f) q = 2;
            else q = 3;
            f = f.child(q), path = path << 2 | uint32_t(q);
            if (d == 0) continue; // tree depth 1 = the root's resolution: no tile there
            const Tile* t = store.find(tileKey(tri, d, path));
            if (!t) continue; // no tile at this depth = a zero delta (dabs make tiles at their own depth only)
            const glm::vec3 r2 = w - f.o;
            const float det2 = f.u.y * f.v.z - f.v.y * f.u.z;
            const float a2 = std::clamp((r2.y * f.v.z - f.v.y * r2.z) / det2, 0.0f, 1.0f), b2 = std::clamp((f.u.y * r2.z - r2.y * f.u.z) / det2, 0.0f, 1.0f);
            h += lerpGrid(t->delta.data(), kTileN, a2, std::min(b2, 1.0f - a2));
        }
        return h;
    }
};

struct DabStats {
    size_t tilesTouched = 0, pointsWritten = 0, born = 0;
    double ms = 0;
    int depth = 0;
};

struct StrokeUndo { // one stroke's record, journal-packed as MicroUndo does (microPackTiles on the change)
    std::vector<uint64_t> bornKeys;             // tiles born in the stroke (undo frees them, redo would re-create them)
    std::vector<uint32_t> rootTris;             // root tiles changed
    std::vector<uint8_t> rootPacked;            // their change, packed (561 values per tile)
    std::vector<uint64_t> tileKeys;             // delta tiles changed that existed before
    std::vector<uint8_t> tilePacked;            // their change, packed (153 per tile)
    std::vector<int16_t> rootFirsts, tileFirsts; // each grid's first change (the journal predicts the rest from it)
    size_t bytes() const {
        return bornKeys.size() * 8 + rootTris.size() * 4 + rootPacked.size() + tileKeys.size() * 8 + tilePacked.size() + (rootFirsts.size() + tileFirsts.size()) * 2;
    }
};

// The change `now - old` of one grid (n segments) as MicroStroke::end journals it: each point's change less the change of
// the point before it in its row (a row's first: the row below's first), the very first kept in `first`.
void journalResiduals(const int16_t* now, const int16_t* old, uint32_t n, int16_t* out, int16_t& first) {
    uint32_t p = 0;
    for (uint32_t j = 0; j <= n; ++j)
        for (uint32_t i = 0; i + j <= n; ++i, ++p) {
            const int d = int(now[p]) - int(old[p]);
            int pred = 0;
            if (i > 0) pred = int(now[p - 1]) - int(old[p - 1]);
            else if (j > 0) {
                const uint32_t q = MicroMesh::index(0, j - 1, n);
                pred = int(now[q]) - int(old[q]);
            }
            if (p == 0) first = int16_t(d);
            out[p] = p == 0 ? int16_t(0) : int16_t(std::clamp(d - pred, -32767, 32767));
        }
}
// The inverse: the change from its residuals (in place).
void journalChanges(int16_t* r, uint32_t n, int16_t first) {
    uint32_t p = 0;
    for (uint32_t j = 0; j <= n; ++j)
        for (uint32_t i = 0; i + j <= n; ++i, ++p) {
            int pred = 0;
            if (i > 0) pred = r[p - 1];
            else if (j > 0) pred = r[MicroMesh::index(0, j - 1, n)];
            r[p] = p == 0 ? first : int16_t(int(r[p]) + pred);
        }
}

struct Brush { // a Standard-like dab: height += gain * falloff(dist / r) along the base normal, in units of step
    float r = 0, gain = 0;
    glm::vec3 c{0}, n{0, 0, 1};
    float falloff(float d2) const {
        const float x = d2 / (r * r);
        if (x >= 1.0f) return 0.0f;
        const float t = 1.0f - x;
        return t * t * (3.0f - 2.0f * t); // smoothstep in d^2
    }
};

struct Probe {
    const Mesh& m;
    MicroMesh& mm;
    BVH bvh;
    TileStore store;
    std::vector<glm::vec3> baseNormal; // per base triangle (flat: the probe's "Phong normal")
    float cellRoot = 0, H = 0;
    glm::vec3 lo{0}, hi{0};
    // a stroke in progress
    std::unordered_map<uint64_t, std::vector<int16_t>> oldTiles;  // first-touch copies of tiles that existed before
    std::unordered_map<uint32_t, std::vector<int16_t>> oldRoots;
    std::vector<uint64_t> bornKeys;
    std::vector<uint32_t> cand;

    explicit Probe(const Mesh& mesh) : m(mesh), mm(*mesh.micro) {
        bvh.build(m);
        store.maxDepth.assign(mm.baseTriangles, 0);
        baseNormal.resize(mm.baseTriangles);
        lo = glm::vec3(1e30f), hi = glm::vec3(-1e30f);
        for (const glm::vec3& p : m.positions) lo = glm::min(lo, p), hi = glm::max(hi, p);
        H = hi.y - lo.y;
        cellRoot = mm.baseEdge / float(mm.n);
        for (uint32_t t = 0; t < mm.baseTriangles; ++t) {
            const glm::uvec3 tr = m.triangles[t];
            baseNormal[t] = glm::normalize(glm::cross(m.positions[tr.y] - m.positions[tr.x], m.positions[tr.z] - m.positions[tr.x]));
        }
    }
    float pxPerUnit(float zoom) const { return kViewH * kViewFill * zoom / H; }
    // The depth a dab of radius r placed at `zoom` writes at (<= 0: the root).
    int depthFor(float r, float zoom, float pxRule) const {
        const int dr = int(std::ceil(std::log2(48.0f * cellRoot / r)));
        const int dv = pxRule > 0 ? int(std::ceil(std::log2(cellRoot * pxPerUnit(zoom) / pxRule))) : 0;
        return std::min(std::max(dr, dv), kMaxDepth);
    }
    glm::vec3 pos(uint32_t tri, const glm::vec3& w) const {
        const glm::uvec3 tr = m.triangles[tri];
        return m.positions[tr.x] * w.x + m.positions[tr.y] * w.y + m.positions[tr.z] * w.z;
    }
    // Every base triangle the brush reaches (the BVH query, as the product's brushes).
    void candidates(const Brush& b) {
        cand.clear();
        microQueryTriangles(m, bvh, b.c, b.r, cand);
    }
    // One dab. Returns its stats.
    DabStats dab(const Brush& b, int depth) {
        DabStats s;
        s.depth = depth;
        const auto t0 = Clock::now();
        candidates(b);
        if (depth <= 0) { // the root: today's write (every grid point of every triangle in reach)
            const uint32_t n = mm.n;
            std::atomic<size_t> written{0};
            for (uint32_t tri : cand)
                if (!oldRoots.count(tri)) {
                    const int16_t* g = mm.disp.data() + size_t(tri) * mm.samples;
                    oldRoots.emplace(tri, std::vector<int16_t>(g, g + mm.samples));
                }
            parallel(cand.size(), [&](size_t lo_, size_t hi_) {
                size_t w = 0;
                for (size_t k = lo_; k < hi_; ++k) {
                    const uint32_t tri = cand[k];
                    int16_t* g = mm.disp.data() + size_t(tri) * mm.samples;
                    const glm::vec3& nn = baseNormal[tri];
                    const float along = glm::dot(nn, b.n);
                    uint32_t p = 0;
                    for (uint32_t j = 0; j <= n; ++j)
                        for (uint32_t i = 0; i + j <= n; ++i, ++p) {
                            const glm::vec3 wb(1.0f - float(i + j) / float(n), float(i) / float(n), float(j) / float(n));
                            const glm::vec3 x = pos(tri, wb) + nn * (float(g[p]) * mm.step);
                            const float f = b.falloff(glm::dot(x - b.c, x - b.c));
                            if (f > 0.0f) g[p] = int16_t(std::clamp(float(g[p]) + b.gain * f * along, -32767.0f, 32767.0f));
                        }
                    w += mm.samples;
                }
                written += w;
            });
            s.tilesTouched = cand.size(), s.pointsWritten = written;
        } else {
            // Collect the tiles at `depth` the disc reaches: descend each candidate's quadtree, pruning sub-triangles whose
            // circumscribed sphere misses the brush.
            struct Target { uint32_t tri; uint32_t path; Frame f; };
            std::vector<Target> targets;
            for (uint32_t tri : cand) {
                struct Node { Frame f; uint32_t path; int d; };
                std::vector<Node> stack{{Frame{}, 0u, -1}};
                while (!stack.empty()) {
                    Node nd = stack.back();
                    stack.pop_back();
                    // the sub-triangle's corners (with the root height as its proxy: flat test, grown by the tile's reach)
                    const glm::vec3 c0 = pos(tri, nd.f.at(0, 0)), c1 = pos(tri, nd.f.at(1, 0)), c2 = pos(tri, nd.f.at(0, 1));
                    const glm::vec3 ctr = (c0 + c1 + c2) / 3.0f;
                    const float rad = std::sqrt(std::max({glm::dot(c0 - ctr, c0 - ctr), glm::dot(c1 - ctr, c1 - ctr), glm::dot(c2 - ctr, c2 - ctr)})) + mm.bulgeMax + mm.rangeMax * mm.step;
                    if (glm::distance(ctr, b.c) > b.r + rad) continue;
                    if (nd.d == depth) { targets.push_back({tri, nd.path, nd.f}); continue; }
                    for (int q = 0; q < 4; ++q) stack.push_back({nd.f.child(q), nd.path << 2 | uint32_t(q), nd.d + 1});
                }
            }
            // first-touch copies + births (serial: the store is one map), then the writes in parallel
            std::vector<uint32_t> slots(targets.size());
            for (size_t k = 0; k < targets.size(); ++k) {
                const uint64_t key = tileKey(targets[k].tri, depth, targets[k].path);
                bool born = false;
                Tile* t = store.obtain(key, born); // (may grow the pool: keep the slot, not the pointer)
                slots[k] = uint32_t(t - store.pool.data());
                if (born) bornKeys.push_back(key), ++s.born, store.maxDepth[targets[k].tri] = uint8_t(std::max<int>(store.maxDepth[targets[k].tri], depth));
                else if (!oldTiles.count(key)) oldTiles.emplace(key, std::vector<int16_t>(t->delta.begin(), t->delta.end()));
            }
            std::vector<Tile*> tiles(targets.size());
            for (size_t k = 0; k < targets.size(); ++k) tiles[k] = &store.pool[slots[k]];
            Resolver res{mm, store};
            std::atomic<size_t> written{0};
            parallel(targets.size(), [&](size_t lo_, size_t hi_) {
                size_t w = 0;
                for (size_t k = lo_; k < hi_; ++k) {
                    const Target& tg = targets[k];
                    Tile& t = *tiles[k];
                    const glm::vec3& nn = baseNormal[tg.tri];
                    const float along = glm::dot(nn, b.n);
                    uint32_t p = 0;
                    for (uint32_t j = 0; j <= kTileN; ++j)
                        for (uint32_t i = 0; i + j <= kTileN; ++i, ++p) {
                            const glm::vec3 wb = tg.f.at(float(i) / float(kTileN), float(j) / float(kTileN));
                            // the surface under the point: the chain above this tile (the tile's own delta is what we write)
                            const float h = res.at(tg.tri, wb, depth - 1) + float(t.delta[p]);
                            const glm::vec3 x = pos(tg.tri, wb) + nn * (h * mm.step);
                            const float f = b.falloff(glm::dot(x - b.c, x - b.c));
                            if (f > 0.0f) t.delta[p] = int16_t(std::clamp(float(t.delta[p]) + b.gain * f * along, -32767.0f, 32767.0f));
                        }
                    w += kTileSamples;
                }
                written += w;
            });
            s.tilesTouched = targets.size(), s.pointsWritten = written;
        }
        s.ms = ms(t0);
        return s;
    }
    // Ends the stroke: the undo record (change journal, packed), as MicroStroke::end.
    StrokeUndo end() {
        StrokeUndo u;
        u.bornKeys = std::move(bornKeys);
        bornKeys.clear();
        std::vector<int16_t> change(oldRoots.size() * mm.samples);
        for (auto& [tri, old] : oldRoots) {
            const size_t k = u.rootTris.size();
            int16_t first = 0;
            journalResiduals(mm.disp.data() + size_t(tri) * mm.samples, old.data(), mm.n, change.data() + k * mm.samples, first);
            u.rootTris.push_back(tri), u.rootFirsts.push_back(first);
        }
        if (!u.rootTris.empty()) microPackTiles(change.data(), u.rootTris.size(), mm.samples, u.rootPacked);
        change.assign(oldTiles.size() * kTileSamples, 0);
        for (auto& [key, old] : oldTiles) {
            const Tile* t = store.find(key);
            const size_t k = u.tileKeys.size();
            int16_t first = 0;
            journalResiduals(t->delta.data(), old.data(), kTileN, change.data() + k * kTileSamples, first);
            u.tileKeys.push_back(key), u.tileFirsts.push_back(first);
        }
        if (!u.tileKeys.empty()) microPackTiles(change.data(), u.tileKeys.size(), kTileSamples, u.tilePacked);
        oldRoots.clear(), oldTiles.clear();
        return u;
    }
    void undo(const StrokeUndo& u) {
        std::vector<int16_t> change;
        if (!u.rootTris.empty()) {
            change.assign(u.rootTris.size() * mm.samples, 0);
            microUnpackTiles(u.rootPacked.data(), u.rootPacked.size(), u.rootTris.size(), mm.samples, change.data());
            for (size_t k = 0; k < u.rootTris.size(); ++k) {
                journalChanges(change.data() + k * mm.samples, mm.n, u.rootFirsts[k]);
                int16_t* g = mm.disp.data() + size_t(u.rootTris[k]) * mm.samples;
                for (uint32_t p = 0; p < mm.samples; ++p) g[p] = int16_t(g[p] - change[k * mm.samples + p]);
            }
        }
        if (!u.tileKeys.empty()) {
            change.assign(u.tileKeys.size() * kTileSamples, 0);
            microUnpackTiles(u.tilePacked.data(), u.tilePacked.size(), u.tileKeys.size(), kTileSamples, change.data());
            for (size_t k = 0; k < u.tileKeys.size(); ++k) {
                journalChanges(change.data() + k * kTileSamples, kTileN, u.tileFirsts[k]);
                Tile* t = store.find(u.tileKeys[k]);
                if (!t) continue;
                for (uint32_t p = 0; p < kTileSamples; ++p) t->delta[p] = int16_t(t->delta[p] - change[k * kTileSamples + p]);
            }
        }
        for (uint64_t key : u.bornKeys) store.release(key);
        // (maxDepth stays an upper bound, as triRange does)
    }

    // --- The renderer's side ---
    std::vector<uint32_t> order; // base triangles in Morton order; clusters = runs of 256
    std::vector<uint32_t> clusterOf; // per base triangle
    void buildClusters() {
        const glm::vec3 ext = glm::max(hi - lo, glm::vec3(1e-20f));
        auto spread = [](uint32_t x) {
            uint64_t v = x & 0x1FFFFF;
            v = (v | v << 32) & 0x1F00000000FFFFull;
            v = (v | v << 16) & 0x1F0000FF0000FFull;
            v = (v | v << 8) & 0x100F00F00F00F00Full;
            v = (v | v << 4) & 0x10C30C30C30C30C3ull;
            v = (v | v << 2) & 0x1249249249249249ull;
            return v;
        };
        std::vector<std::pair<uint64_t, uint32_t>> keys(mm.baseTriangles);
        for (uint32_t t = 0; t < mm.baseTriangles; ++t) {
            const glm::uvec3 tr = m.triangles[t];
            const glm::vec3 c = ((m.positions[tr.x] + m.positions[tr.y] + m.positions[tr.z]) / 3.0f - lo) / ext;
            const glm::uvec3 q(glm::clamp(c, 0.0f, 1.0f) * 2097151.0f);
            keys[t] = {spread(q.x) | spread(q.y) << 1 | spread(q.z) << 2, t};
        }
        std::sort(keys.begin(), keys.end());
        order.resize(mm.baseTriangles), clusterOf.resize(mm.baseTriangles);
        for (size_t k = 0; k < keys.size(); ++k) order[k] = keys[k].second, clusterOf[keys[k].second] = uint32_t(k / 256);
    }
    // Of `dirty`, the triangles in the clusters a 1600x1000 view centred on `at` shows at `zoom` (a cluster shows when
    // any of its dirty triangles is within the view's half diagonal: pages are per cluster level).
    std::vector<uint32_t> inView(const std::vector<uint32_t>& dirty, const glm::vec3& at, float zoom, size_t& clusters) const {
        const float half = std::sqrt(800.0f * 800.0f + 500.0f * 500.0f) / pxPerUnit(zoom);
        std::vector<uint8_t> shown((mm.baseTriangles + 255) / 256, 0);
        for (uint32_t t : dirty) {
            const glm::uvec3 tr = m.triangles[t];
            const glm::vec3 c = (m.positions[tr.x] + m.positions[tr.y] + m.positions[tr.z]) / 3.0f;
            const float rad = std::max({glm::distance(c, m.positions[tr.x]), glm::distance(c, m.positions[tr.y]), glm::distance(c, m.positions[tr.z])});
            if (glm::distance(c, at) <= half + rad) shown[clusterOf[t]] = 1;
        }
        std::vector<uint32_t> out;
        for (uint32_t t : dirty)
            if (shown[clusterOf[t]]) out.push_back(t);
        clusters = size_t(std::count(shown.begin(), shown.end(), uint8_t(1)));
        return out;
    }
    // The level a base triangle is drawn at from `zoom` (the view at the model's centre, 12 px per micro edge).
    int drawLevel(float zoom) const {
        const float pxEdge = mm.baseEdge * pxPerUnit(zoom);
        const float ratio = pxEdge / kDrawPx;
        return std::clamp(ratio <= 1.0f ? 0 : int(std::ceil(std::log2(ratio))), 0, 8); // (16-bit patch indices cap at 8)
    }
    // Re-resolves the pages of the base triangles in `tris` at `level`: root + delta chain -> absolute int16 grid.
    // Returns ms; `bytes` = what the upload moves.
    double reresolve(const std::vector<uint32_t>& tris, int level, size_t& bytes, int maxDepth) {
        const uint32_t n = 1u << level, S = (n + 1) * (n + 2) / 2;
        std::vector<int16_t> pages(tris.size() * S);
        Resolver res{mm, store};
        const auto t0 = Clock::now();
        parallel(tris.size(), [&](size_t lo_, size_t hi_) {
            for (size_t k = lo_; k < hi_; ++k) {
                int16_t* g = pages.data() + k * S;
                uint32_t p = 0;
                for (uint32_t j = 0; j <= n; ++j)
                    for (uint32_t i = 0; i + j <= n; ++i, ++p) {
                        const glm::vec3 wb(1.0f - float(i + j) / float(n), float(i) / float(n), float(j) / float(n));
                        g[p] = int16_t(std::clamp(res.at(tris[k], wb, maxDepth), -32767.0f, 32767.0f));
                    }
            }
        });
        const double t = ms(t0);
        bytes = pages.size() * 2;
        return t;
    }
    // The LOD pass: per cluster (and per base triangle) the drawn level from the view; also reads the cluster's deepest
    // tile (what decides whether a page is needed). Returns ms.
    double walk(float zoom, bool perTriangle, size_t& drawnTris) {
        const float ppu = pxPerUnit(zoom);
        const glm::vec3 eye = (lo + hi) * 0.5f + glm::vec3(0, 0, 1) * (H / (kViewFill * zoom)); // a camera looking at the centre
        std::atomic<size_t> total{0};
        const auto t0 = Clock::now();
        if (perTriangle) {
            parallel(mm.baseTriangles, [&](size_t lo_, size_t hi_) {
                size_t tot = 0;
                for (size_t k = lo_; k < hi_; ++k) {
                    const uint32_t tri = order[k];
                    const glm::uvec3 tr = m.triangles[tri];
                    const glm::vec3 c = (m.positions[tr.x] + m.positions[tr.y] + m.positions[tr.z]) / 3.0f;
                    const float dist = std::max(glm::distance(eye, c), 1e-6f);
                    const float px = mm.baseEdge * ppu * (H / (kViewFill * zoom)) / dist;
                    const float ratio = px / kDrawPx;
                    const int l = std::clamp(ratio <= 1.0f ? 0 : int(std::ceil(std::log2(ratio))), 0, 8);
                    tot += size_t(1) << (2 * l);
                }
                total += tot;
            });
        } else {
            const size_t clusters = (mm.baseTriangles + 255) / 256;
            parallel(clusters, [&](size_t lo_, size_t hi_) {
                size_t tot = 0;
                for (size_t c = lo_; c < hi_; ++c) {
                    glm::vec3 ctr(0);
                    int deepest = 0;
                    const size_t b = c * 256, e = std::min<size_t>(b + 256, mm.baseTriangles);
                    for (size_t k = b; k < e; ++k) {
                        const glm::uvec3 tr = m.triangles[order[k]];
                        ctr += m.positions[tr.x];
                        deepest = std::max<int>(deepest, store.maxDepth[order[k]]);
                    }
                    ctr /= float(e - b);
                    const float dist = std::max(glm::distance(eye, ctr), 1e-6f);
                    const float px = mm.baseEdge * ppu * (H / (kViewFill * zoom)) / dist;
                    const float ratio = px / kDrawPx;
                    const int l = std::clamp(ratio <= 1.0f ? 0 : int(std::ceil(std::log2(ratio))), 0, 8);
                    tot += (e - b) << (2 * l);
                    (void)deepest;
                }
                total += tot;
            });
        }
        const double t = ms(t0);
        drawnTris = total;
        return t;
    }
};

// Dab centres: the whole-head pass = 30 bands of 24 dabs around the head (rays towards the axis); a region pass = 30
// parallel lines of 24 dabs in the tangent plane at `anchor`, spaced `spacing`, projected onto the surface.
std::vector<std::pair<glm::vec3, glm::vec3>> headPath(const Probe& p, int stroke, int strokes, int dabs) {
    std::vector<std::pair<glm::vec3, glm::vec3>> out;
    const float y = p.lo.y + p.H * (0.5f + float(stroke)) / float(strokes);
    const glm::vec3 c = (p.lo + p.hi) * 0.5f;
    const float R = glm::length(p.hi - p.lo);
    for (int k = 0; k < dabs; ++k) {
        const float a = 6.2831853f * float(k) / float(dabs);
        const glm::vec3 dir(-std::cos(a), 0, -std::sin(a));
        RayHit hit;
        if (p.bvh.raycast(p.m, glm::vec3(c.x, y, c.z) - dir * R, dir, hit)) out.emplace_back(hit.position, hit.normal);
    }
    return out;
}
std::vector<std::pair<glm::vec3, glm::vec3>> regionPath(const Probe& p, const glm::vec3& anchor, const glm::vec3& n, int stroke, int strokes, int dabs,
                                                        float spacing) {
    std::vector<std::pair<glm::vec3, glm::vec3>> out;
    glm::vec3 up = glm::normalize(glm::vec3(0, 1, 0) - n * n.y);
    const glm::vec3 right = glm::cross(up, n);
    const float r0 = (float(stroke) - 0.5f * float(strokes - 1)) * spacing;
    for (int k = 0; k < dabs; ++k) {
        const float c0 = (float(k) - 0.5f * float(dabs - 1)) * spacing;
        RayHit hit;
        const glm::vec3 o = anchor + right * c0 + up * r0 + n * (p.H * 0.1f);
        if (p.bvh.raycast(p.m, o, -n, hit)) out.emplace_back(hit.position, hit.normal);
    }
    return out;
}

} // namespace

int main(int argc, char** argv) {
    size_t baseTriangles = 195313;
    int level = 5, strokes = 30;
    std::vector<float> zooms = {1.0f, 4.0f, 16.0f, 64.0f};
    float pxRule = 1.0f;
    int cover = 0;
    int pos = 0;
    for (int i = 1; i < argc; ++i) {
        if (std::strncmp(argv[i], "--px-rule=", 10) == 0) pxRule = float(std::atof(argv[i] + 10));
        else if (std::strncmp(argv[i], "--cover=", 8) == 0) cover = std::atoi(argv[i] + 8);
        else if (pos == 0) baseTriangles = size_t(std::atol(argv[i])), ++pos;
        else if (pos == 1) level = std::atoi(argv[i]), ++pos;
        else if (pos == 2) strokes = std::atoi(argv[i]), ++pos;
        else if (pos == 3) {
            zooms.clear();
            for (const char* s = argv[i]; *s;) {
                zooms.push_back(float(std::atof(s)));
                while (*s && *s != ',') ++s;
                if (*s) ++s;
            }
            ++pos;
        }
    }
    ThreadPool::setSharedThreadCount(kThreads);
    const int dabs = 24;

    auto t0 = Clock::now();
    Mesh mesh = makeBenchHead(baseTriangles);
    std::string err;
    if (!microDivide(mesh, level, err, 0)) {
        std::fprintf(stderr, "tiles-probe: %s\n", err.c_str());
        return 1;
    }
    // a little relief so the root isn't flat (the core-200m scene's wrinkles)
    microNoise(mesh, 0.004f, 40.0f, 1);
    mesh.micro->bulgeMax = std::max(mesh.micro->bulgeMax, mesh.micro->rangeMax * mesh.micro->step);
    Probe p(mesh);
    p.buildClusters();
    const MicroMesh& mm = p.mm;
    const size_t rssRoot = residentBytes();
    std::printf("# Adaptive Detail tiles: step 0 probe (sf_tiles_probe_bench)\n\n");
    std::printf("Root: %zuM micro triangles (base %u, level %d, n %u, %.0f MB of int16 heights), head height H = %.3g, root cell = %.2e H, view 1600x1000 (head fills %.0f %% at zoom 1: root cell = %.2f px); %d threads; built in %.1f s; RSS %.0f MB.\n\n",
                mm.microTriangles() / 1000000, mm.baseTriangles, mm.level, mm.n, double(mm.bytes()) / 1e6, double(p.H), double(p.cellRoot / p.H),
                double(kViewFill * 100), double(p.cellRoot * p.pxPerUnit(1.0f)), kThreads, ms(t0) / 1000.0, double(rssRoot) / 1e6);
    std::printf("Tile = %u segments, %u int16 deltas (%zu B); depth d tile cell = root cell / 2^d; a dab's depth = max(cell <= r/48, cell <= %.2g px at placement).\n\n",
                kTileN, kTileSamples, sizeof(Tile), double(pxRule));

    // the region for the close passes: the most forward-facing high point (detail_bench's choice)
    uint32_t best = 0;
    float bestScore = -1e30f;
    for (uint32_t v = 0; v < mesh.positions.size(); ++v) {
        const glm::vec3 n = glm::normalize(mesh.normals[v]);
        const float score = n.z * 2.0f + (mesh.positions[v].z - p.lo.z) / p.H - std::abs(mesh.positions[v].x - 0.5f * (p.lo.x + p.hi.x)) / p.H;
        if (n.z > 0.9f && score > bestScore) bestScore = score, best = v;
    }
    const glm::vec3 anchor = mesh.positions[best], anchorN = glm::normalize(mesh.normals[best]);

    std::printf("| pass | brush px | zoom | r / H | r / root cell | depth | strokes x dabs | dab: tiles touched (median) | dab: points written (median) | dab ms p50 / p99 | tiles born | tiles alive after | tile MB | undo ring MB (pass / total) | peak RSS MB |\n");
    std::printf("|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|\n");
    std::vector<StrokeUndo> ring;
    size_t ringBytes = 0;
    struct PassInfo { float zoom, r; int depth; std::vector<uint32_t> lastDabTris; Brush lastBrush; };
    std::vector<PassInfo> passes;
    DabStats costBefore, costAfter; // the 240 px root dab at the anchor: before the close passes refine it and after
    Brush costBrush;
    for (size_t z = 0; z < zooms.size(); ++z) {
        const float zoom = zooms[z];
        const float brushPx = z == 0 ? 240.0f : 60.0f;
        const float r = brushPx / p.pxPerUnit(zoom); // the brush radius in world units at this view
        const int depth = p.depthFor(r, zoom, pxRule);
        std::vector<double> dabMs;
        std::vector<size_t> touched, written;
        size_t born = 0, passUndo = 0;
        PassInfo info{zoom, r, depth, {}, {}};
        for (int s = 0; s < strokes; ++s) {
            const auto path = z == 0 ? headPath(p, s, strokes, dabs) : regionPath(p, anchor, anchorN, s, strokes, dabs, r);
            for (const auto& [c, n] : path) {
                Brush b;
                b.r = r, b.c = c, b.n = n;
                b.gain = 0.02f * r / mm.step; // a dab raises 2 % of r at its centre (a stroke's overlaps: a few %)
                const DabStats st = p.dab(b, depth);
                dabMs.push_back(st.ms), touched.push_back(st.tilesTouched), written.push_back(st.pointsWritten), born += st.born;
                info.lastBrush = b, info.lastDabTris = p.cand;
            }
            StrokeUndo u = p.end();
            passUndo += u.bytes(), ringBytes += u.bytes();
            ring.push_back(std::move(u));
        }
        auto med = [](auto v) { std::sort(v.begin(), v.end()); return v.empty() ? typename decltype(v)::value_type{} : v[v.size() / 2]; };
        auto p99 = [](auto v) { std::sort(v.begin(), v.end()); return v.empty() ? typename decltype(v)::value_type{} : v[std::min(v.size() - 1, v.size() * 99 / 100)]; };
        std::printf("| %zu | %.0f | %gx | %.2e | %.1f | %s | %d x %d | %zu | %zu | %.1f / %.1f | %zu | %zu | %.1f | %.1f / %.1f | %.0f |\n", z + 1, double(brushPx), double(zoom),
                    double(r / p.H), double(r / p.cellRoot), depth <= 0 ? "root" : std::to_string(depth).c_str(), strokes, dabs, med(touched), med(written),
                    med(dabMs), p99(dabMs), born, p.store.alive(), double(p.store.bytes()) / 1e6, double(passUndo) / 1e6, double(ringBytes) / 1e6,
                    double(peakResidentBytes()) / 1e6);
        passes.push_back(std::move(info));
        if (z == 0) { // the cost check's "before": the region holds no tiles yet
            costBrush = passes[0].lastBrush, costBrush.c = anchor, costBrush.n = anchorN;
            for (int k = 0; k < 3; ++k) costBefore = p.dab(costBrush, passes[0].depth); // (warm)
            StrokeUndo u = p.end();
            p.undo(u);
        }
    }
    {
        for (int k = 0; k < 3; ++k) costAfter = p.dab(costBrush, passes[0].depth);
        StrokeUndo u = p.end();
        p.undo(u);
        std::printf("\nCost check (the same 240 px root dab at the region's centre): before the close passes %zu triangles / %zu points / %.1f ms; after them (tiles to depth %d below) %zu / %zu / %.1f ms.\n",
                    costBefore.tilesTouched, costBefore.pointsWritten, costBefore.ms, passes.back().depth, costAfter.tilesTouched, costAfter.pointsWritten, costAfter.ms);
    }

    // Page re-resolve + upload per pass: the triangles the pass's last dab dirtied, at the level the pass's view draws.
    std::printf("\n| pass | zoom | drawn level (12 px / micro edge) | deepest tile read | triangles dirtied by one dab | of them in view (clusters) | re-resolve ms (4 threads) | upload MB | bar < 16 ms |\n|---|---|---|---|---|---|---|---|---|\n");
    for (size_t z = 0; z < passes.size(); ++z) {
        const PassInfo& pi = passes[z];
        const int L = p.drawLevel(pi.zoom);
        const int maxDepth = std::max(0, L - mm.level); // tiles finer than the drawn level don't show
        size_t bytes = 0, clusters = 0;
        const std::vector<uint32_t> shown = p.inView(pi.lastDabTris, pi.lastBrush.c, pi.zoom, clusters);
        const double t = p.reresolve(shown, L, bytes, maxDepth);
        std::printf("| %zu | %gx | %d | %d | %zu | %zu (%zu) | %.1f | %.1f | %s |\n", z + 1, double(pi.zoom), L, maxDepth, size_t(pi.lastDabTris.size()), shown.size(), clusters, t,
                    double(bytes) / 1e6, t < 16.0 ? "ok" : "MISSED");
    }
    // Critique B's case: the view zoomed in until the tiles show (drawn level 6, 7, 8 = depth 1..3 chains under the root),
    // for the deepest pass's dab and for the first (240 px) dab's triangles.
    for (size_t z : {size_t(0), passes.size() - 1})
        for (int L = mm.level + 1; L <= std::min(8, mm.level + 3); ++L) {
            const PassInfo& pi = passes[z];
            const float zoom = kDrawPx / (mm.baseEdge / float(1u << L) * p.pxPerUnit(1.0f));
            size_t bytes = 0, clusters = 0;
            const std::vector<uint32_t> shown = p.inView(pi.lastDabTris, pi.lastBrush.c, zoom, clusters);
            const double t = p.reresolve(shown, L, bytes, L - mm.level);
            std::printf("| %zu (zoomed until drawn) | %.0fx | %d | %d | %zu | %zu (%zu) | %.1f | %.1f | %s |\n", z + 1, double(zoom), L, L - mm.level, size_t(pi.lastDabTris.size()),
                        shown.size(), clusters, t, double(bytes) / 1e6, t < 16.0 ? "ok" : "MISSED");
        }

    // The LOD walk.
    std::printf("\n| walk | zoom | items | ms (4 threads) | micro triangles the view would draw | bar < 5 ms |\n|---|---|---|---|---|---|\n");
    for (float zoom : {1.0f, 16.0f}) {
        size_t drawn = 0;
        std::vector<double> t;
        for (int k = 0; k < 5; ++k) t.push_back(p.walk(zoom, false, drawn));
        std::sort(t.begin(), t.end());
        std::printf("| per cluster (256 base triangles) | %gx | %zu | %.2f | %.1fM | %s |\n", double(zoom), size_t((mm.baseTriangles + 255) / 256), t[2], double(drawn) / 1e6, t[2] < 5.0 ? "ok" : "MISSED");
        t.clear();
        for (int k = 0; k < 5; ++k) t.push_back(p.walk(zoom, true, drawn));
        std::sort(t.begin(), t.end());
        std::printf("| per base triangle (the spike's wall) | %gx | %u | %.2f | %.1fM | %s |\n", double(zoom), mm.baseTriangles, t[2], double(drawn) / 1e6, t[2] < 5.0 ? "ok" : "MISSED");
    }

    // Undo 10 strokes.
    {
        const auto t1 = Clock::now();
        size_t freed = 0;
        for (int k = 0; k < 10 && !ring.empty(); ++k) {
            freed += ring.back().bornKeys.size();
            p.undo(ring.back());
            ringBytes -= ring.back().bytes();
            ring.pop_back();
        }
        std::printf("\nUndo 10 strokes: %.1f ms, %zu tiles freed, %zu alive, tile pool MB %.1f (slots stay for reuse), undo ring MB %.1f, RSS %.0f MB (peak %.0f).\n", ms(t1), freed, p.store.alive(),
                    double(p.store.bytes()) / 1e6, double(ringBytes) / 1e6, double(residentBytes()) / 1e6, double(peakResidentBytes()) / 1e6);
    }
    if (cover > 0) { // the whole head refined to `cover`: what a finished detail pass everywhere costs to hold
        const auto t2 = Clock::now();
        const size_t before = p.store.alive();
        const uint32_t tilesPerTri = 1u << (2 * (cover + 1));
        p.store.pool.reserve(p.store.pool.size() + size_t(mm.baseTriangles) * tilesPerTri); // (one allocation: the peak is the store, not a doubling)
        p.store.slotOf.reserve(p.store.slotOf.size() + size_t(mm.baseTriangles) * tilesPerTri);
        for (uint32_t tri = 0; tri < mm.baseTriangles; ++tri)
            for (uint32_t path = 0; path < tilesPerTri; ++path) {
                bool born = false;
                p.store.obtain(tileKey(tri, cover, path), born);
            }
        const size_t tiles = p.store.alive();
        const double poolMB = double(p.store.pool.size() * sizeof(Tile)) / 1e6, indexMB = double(p.store.slotOf.size() * 52) / 1e6;
        std::printf("\nFull coverage at depth %d (every tile of the head, %u per base triangle): %zu tiles (+%zu) in %.1f s; pool %.0f MB + index ~%.0f MB; RSS %.0f MB (peak %.0f); a dense grid of that spacing (%.1fB micro triangles) would be %.0f MB: bar < 4000 MB: %s.\n",
                    cover, tilesPerTri, tiles, tiles - before, ms(t2) / 1000.0, poolMB, indexMB, double(residentBytes()) / 1e6, double(peakResidentBytes()) / 1e6,
                    double(mm.microTriangles()) * std::pow(4.0, cover) / 1e9, double(mm.microTriangles()) * std::pow(4.0, cover) * 0.5 * 2 / 1e6,
                    peakResidentBytes() < size_t(4000) * 1000000 ? "ok" : "MISSED");
    }
    // 1B-equivalent spacing: the finest brushed cell as a uniform grid over the whole head.
    int deepest = 0;
    for (uint8_t d : p.store.maxDepth) deepest = std::max<int>(deepest, d);
    const double equivalent = double(mm.microTriangles()) * std::pow(4.0, deepest);
    std::printf("\nFinest brushed spacing: root cell / %d (depth %d) = %.2e H; uniform-grid equivalent %.2gB micro triangles (the root holds %.0fM); peak RSS %.0f MB, bar < 4000 MB: %s.\n",
                1 << deepest, deepest, double(p.cellRoot / float(1 << deepest) / p.H), equivalent / 1e9, double(mm.microTriangles()) / 1e6, double(peakResidentBytes()) / 1e6,
                peakResidentBytes() < size_t(4000) * 1000000 ? "ok" : "MISSED");
    return 0;
}
