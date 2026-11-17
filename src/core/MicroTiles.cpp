// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/MicroTiles.h"
#include "core/Micro.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace sf {

TileFrame TileFrame::child(int q) const {
    TileFrame f;
    if (q == 0) f.o = o, f.u = u * 0.5f, f.v = v * 0.5f;
    else if (q == 1) f.o = o + u * 0.5f, f.u = u * 0.5f, f.v = v * 0.5f;
    else if (q == 2) f.o = o + v * 0.5f, f.u = u * 0.5f, f.v = v * 0.5f;
    else f.o = o + (u + v) * 0.5f, f.u = -u * 0.5f, f.v = -v * 0.5f; // the middle, inverted
    return f;
}

void TileFrame::coords(const glm::vec3& w, float& a, float& b) const {
    const glm::vec3 r = w - o;
    const float det = u.y * v.z - v.y * u.z;
    a = (r.y * v.z - v.y * r.z) / det;
    b = (u.y * r.z - r.y * u.z) / det;
}

TileFrame TileFrame::of(int depth, uint32_t path) {
    TileFrame f;
    for (int d = depth; d >= 0; --d) f = f.child(int(path >> (2 * d) & 3));
    return f;
}

bool MicroTiles::enabled() {
    static const bool on = [] {
        const char* e = std::getenv("SF_MICRO_TILES");
        return e && e[0] == '1';
    }();
    return on;
}

size_t MicroTiles::bytes() const {
    return pool.capacity() * sizeof(Tile) + slotOf.size() * (sizeof(uint64_t) + sizeof(uint32_t) + 40) + freeSlots.capacity() * sizeof(uint32_t) + maxDepth.capacity();
}

MicroTiles::Tile* MicroTiles::find(uint64_t key) {
    auto it = slotOf.find(key);
    return it == slotOf.end() ? nullptr : &pool[it->second - 1];
}

const MicroTiles::Tile* MicroTiles::find(uint64_t key) const {
    auto it = slotOf.find(key);
    return it == slotOf.end() ? nullptr : &pool[it->second - 1];
}

MicroTiles::Tile* MicroTiles::obtain(uint64_t key, bool& born) {
    auto it = slotOf.find(key);
    if (it != slotOf.end()) return born = false, &pool[it->second - 1];
    uint32_t s;
    if (!freeSlots.empty()) s = freeSlots.back(), freeSlots.pop_back(), pool[s] = Tile();
    else s = uint32_t(pool.size()), pool.emplace_back();
    slotOf.emplace(key, s + 1);
    const uint32_t tri = keyTri(key);
    if (tri < maxDepth.size()) maxDepth[tri] = uint8_t(std::max<int>(maxDepth[tri], keyDepth(key)));
    born = true;
    return &pool[s];
}

void MicroTiles::release(uint64_t key) {
    auto it = slotOf.find(key);
    if (it == slotOf.end()) return;
    freeSlots.push_back(it->second - 1);
    slotOf.erase(it);
    // (maxDepth stays an upper bound, as MicroMesh::triRange does)
}

int MicroTiles::depthFor(float rootCell, float r, float pxPerUnit, float minPx) {
    if (rootCell <= 0.0f || r <= 0.0f) return 0;
    int d = int(std::ceil(std::log2(float(kBrushSamples) * rootCell / r))); // the finest cell still <= r / 48
    if (pxPerUnit > 0.0f && minPx > 0.0f) // the view as a cap: a cell no smaller than minPx on the screen the dab came from
        d = std::min(d, int(std::floor(std::log2(rootCell * pxPerUnit / minPx))));
    return std::min(d, kMaxDepth);
}

// Linear in the grid cell holding (a, b) (a, b in [0, 1], a + b <= 1): exact at the grid points; for a point on an
// edge of the grid both sides compute the same value from the same two points (the third weight is exactly 0).
float MicroTiles::lerpGrid(const int16_t* g, uint32_t n, float a, float b) {
    const float fa = a * float(n), fb = b * float(n);
    uint32_t i = uint32_t(std::clamp(fa, 0.0f, float(n) - 1e-4f)), j = uint32_t(std::clamp(fb, 0.0f, float(n) - 1e-4f));
    if (i + j >= n) { // the far edge: step back into the last cell
        if (i > 0 && (j == 0 || fa - float(i) >= fb - float(j))) --i;
        else if (j > 0) --j;
    }
    const float x = fa - float(i), y = fb - float(j);
    const float g00 = g[MicroMesh::index(i, j, n)], g10 = g[MicroMesh::index(i + 1, j, n)], g01 = g[MicroMesh::index(i, j + 1, n)];
    if (x + y <= 1.0f || i + j + 1 >= n) return g00 + (g10 - g00) * x + (g01 - g00) * y;
    const float g11 = g[MicroMesh::index(i + 1, j + 1, n)]; // the cell's upper triangle: corners (1,0), (0,1), (1,1)
    return g11 + (g01 - g11) * (1.0f - x) + (g10 - g11) * (1.0f - y);
}

float MicroTiles::heightAt(const MicroMesh& mm, uint32_t tri, const glm::vec3& w, int maxDepthWanted) const {
    float h = lerpGrid(mm.disp.data() + size_t(tri) * mm.samples, mm.n, w.y, w.z);
    int deepest = tri < maxDepth.size() ? maxDepth[tri] : 0;
    if (maxDepthWanted >= 0) deepest = std::min(deepest, maxDepthWanted);
    if (deepest <= 0) return h;
    TileFrame f;
    uint32_t path = 0;
    for (int d = 0; d <= deepest; ++d) {
        float a, b;
        f.coords(w, a, b);
        const int q = TileFrame::quadrantOf(a, b);
        f = f.child(q), path = path << 2 | uint32_t(q);
        if (d == 0) continue; // tree depth 1 = the root's own resolution: no tile there
        const Tile* t = find(key(tri, d, path));
        if (!t) continue; // no tile at this depth here = a zero delta (a dab makes tiles at its own depth only: no parents)
        f.coords(w, a, b);
        a = std::clamp(a, 0.0f, 1.0f), b = std::clamp(b, 0.0f, 1.0f - a);
        h += lerpGrid(t->delta.data(), kN, a, b);
    }
    return h;
}

} // namespace sf
