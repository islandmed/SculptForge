// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/MicroTiles.h"
#include "core/Micro.h"
#include "core/TierConfig.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

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

bool MicroTiles::enabled() { // (read at each stroke's start: a ui script's `env` step turns it on and off)
    const char* e = std::getenv("SF_MICRO_TILES");
    return e && e[0] == '1';
}

size_t MicroTiles::defaultResidentBudget() {
    if (const char* e = std::getenv("SF_MICRO_TILES_RESIDENT_MB"); e && *e) return size_t(std::max(0L, std::atol(e))) << 20;
    return size_t(currentTierConfig().residentCacheCapMB / 4) << 20;
}

void MicroTiles::resize(uint32_t baseTriangles) {
    byTri.resize(baseTriangles);
    maxDepth.resize(baseTriangles, 0);
}

size_t MicroTiles::bytes() const {
    size_t index = byTri.capacity() * sizeof(std::vector<Entry>);
    for (const auto& v : byTri) index += v.capacity() * sizeof(Entry);
    return pool.capacity() * sizeof(Tile) + (poolKey.capacity() * 8 + poolUse.capacity() * 4 + freeSlots.capacity() * 4) + arena.capacity() +
           parked.capacity() * sizeof(Parked) + freeParked.capacity() * 4 + index + maxDepth.capacity();
}

// --- the index ---
namespace {
struct ByPath {
    bool operator()(const MicroTiles::Entry& e, uint32_t k) const { return e.pathKey < k; }
};
} // namespace

const MicroTiles::Entry* MicroTiles::findEntry(uint64_t key) const {
    const uint32_t tri = keyTri(key);
    if (tri >= byTri.size()) return nullptr;
    const auto& v = byTri[tri];
    const uint32_t pk = uint32_t(key & 0xFFFFFFFFu);
    auto it = std::lower_bound(v.begin(), v.end(), pk, ByPath{});
    return it != v.end() && it->pathKey == pk ? &*it : nullptr;
}

MicroTiles::Entry* MicroTiles::findEntry(uint64_t key) { return const_cast<Entry*>(static_cast<const MicroTiles*>(this)->findEntry(key)); }

MicroTiles::Entry& MicroTiles::insertEntry(uint64_t key) {
    const uint32_t tri = keyTri(key);
    if (tri >= byTri.size()) resize(tri + 1);
    auto& v = byTri[tri];
    const uint32_t pk = uint32_t(key & 0xFFFFFFFFu);
    auto it = std::lower_bound(v.begin(), v.end(), pk, ByPath{});
    it = v.insert(it, Entry{pk, 0});
    maxDepth[tri] = uint8_t(std::max<int>(maxDepth[tri], keyDepth(key)));
    return *it;
}

uint32_t MicroTiles::newSlot(uint64_t key) {
    uint32_t s;
    if (!freeSlots.empty()) s = freeSlots.back(), freeSlots.pop_back(), pool[s] = Tile();
    else s = uint32_t(pool.size()), pool.emplace_back(), poolKey.push_back(0), poolUse.push_back(0);
    poolKey[s] = key;
    poolUse[s] = ++useStamp;
    return s;
}

MicroTiles::Tile* MicroTiles::resident(uint64_t key) {
    Entry* e = findEntry(key);
    if (!e) return nullptr;
    if (e->ref & kParked) { // back from the arena
        const uint32_t pi = e->ref & ~kParked;
        const uint32_t s = newSlot(key);
        unpackTile(arena.data() + parked[pi].offset, parked[pi].size, pool[s]);
        arenaGarbage += parked[pi].size;
        parked[pi] = Parked{};
        freeParked.push_back(pi);
        e->ref = s + 1;
        return &pool[s];
    }
    const uint32_t s = e->ref - 1;
    poolUse[s] = ++useStamp;
    return &pool[s];
}

MicroTiles::Tile* MicroTiles::obtain(uint64_t key, bool& born) {
    if (Tile* t = resident(key)) return born = false, t;
    const uint32_t s = newSlot(key);
    insertEntry(key).ref = s + 1; // (after newSlot: the pool may move, the entry doesn't hold a pointer)
    born = true;
    return &pool[s];
}

const MicroTiles::Tile* MicroTiles::read(uint64_t key, Tile& scratch) const {
    const Entry* e = findEntry(key);
    if (!e) return nullptr;
    if (e->ref & kParked) {
        const Parked& p = parked[e->ref & ~kParked];
        return unpackTile(arena.data() + p.offset, p.size, scratch) ? &scratch : nullptr;
    }
    return &pool[e->ref - 1];
}

void MicroTiles::release(uint64_t key) {
    const uint32_t tri = keyTri(key);
    if (tri >= byTri.size()) return;
    auto& v = byTri[tri];
    const uint32_t pk = uint32_t(key & 0xFFFFFFFFu);
    auto it = std::lower_bound(v.begin(), v.end(), pk, ByPath{});
    if (it == v.end() || it->pathKey != pk) return;
    if (it->ref & kParked) {
        const uint32_t pi = it->ref & ~kParked;
        arenaGarbage += parked[pi].size;
        parked[pi] = Parked{};
        freeParked.push_back(pi);
    } else {
        const uint32_t s = it->ref - 1;
        poolKey[s] = ~uint64_t(0);
        freeSlots.push_back(s);
    }
    v.erase(it);
    // (maxDepth stays an upper bound, as MicroMesh::triRange does)
}

void MicroTiles::residentAbove(uint32_t tri, int depth, uint32_t path) {
    for (int d = depth - 1; d >= 1; --d) resident(key(tri, d, path >> (2 * (depth - d))));
}

// --- parking ---
void MicroTiles::packTile(const Tile& t, std::vector<uint8_t>& out) {
    int lo = t.delta[0], hi = t.delta[0];
    for (int16_t v : t.delta) lo = std::min(lo, int(v)), hi = std::max(hi, int(v));
    uint32_t span = uint32_t(hi - lo), w = 0;
    while (span) ++w, span >>= 1;
    const size_t base = out.size();
    out.resize(base + 3 + (size_t(kSamples) * w + 7) / 8, 0);
    uint8_t* o = out.data() + base;
    o[0] = uint8_t(uint16_t(lo)), o[1] = uint8_t(uint16_t(lo) >> 8), o[2] = uint8_t(w);
    if (!w) return;
    uint64_t acc = 0;
    uint32_t have = 0;
    size_t pos = 3;
    for (int16_t v : t.delta) {
        acc |= uint64_t(uint32_t(int(v) - lo)) << have;
        have += w;
        while (have >= 8) o[pos++] = uint8_t(acc), acc >>= 8, have -= 8;
    }
    if (have) o[pos++] = uint8_t(acc);
}

bool MicroTiles::unpackTile(const uint8_t* data, size_t size, Tile& t) {
    if (size < 3) return false;
    const int lo = int16_t(uint16_t(data[0] | data[1] << 8));
    const uint32_t w = data[2];
    if (w > 16 || size < 3 + (size_t(kSamples) * w + 7) / 8) return false;
    if (!w) {
        t.delta.fill(int16_t(lo));
        return true;
    }
    uint64_t acc = 0;
    uint32_t have = 0;
    size_t pos = 3;
    const uint64_t mask = (uint64_t(1) << w) - 1;
    for (int16_t& v : t.delta) {
        while (have < w) acc |= uint64_t(data[pos++]) << have, have += 8;
        v = int16_t(lo + int(acc & mask));
        acc >>= w, have -= w;
    }
    return true;
}

void MicroTiles::parkSlot(Entry& e, uint32_t s) {
    uint32_t pi;
    if (!freeParked.empty()) pi = freeParked.back(), freeParked.pop_back();
    else pi = uint32_t(parked.size()), parked.emplace_back();
    parked[pi].offset = uint32_t(arena.size());
    packTile(pool[s], arena);
    parked[pi].size = uint32_t(arena.size() - parked[pi].offset);
    e.ref = kParked | pi;
    poolKey[s] = ~uint64_t(0);
    freeSlots.push_back(s);
    if (arenaGarbage > arena.size() / 2 && arena.size() > (1u << 20)) compactArena();
}

void MicroTiles::compactArena() {
    std::vector<uint8_t> fresh;
    fresh.reserve(arena.size() - arenaGarbage);
    for (auto& v : byTri)
        for (Entry& e : v)
            if (e.ref & kParked) {
                Parked& p = parked[e.ref & ~kParked];
                const uint32_t at = uint32_t(fresh.size());
                fresh.insert(fresh.end(), arena.begin() + p.offset, arena.begin() + p.offset + p.size);
                p.offset = at;
            }
    arena.swap(fresh);
    arenaGarbage = 0;
}

size_t MicroTiles::trim(size_t budget, uint32_t keepSince) {
    if (!budget) budget = residentBudget;
    if (!budget || residentBytes() <= budget) return 0;
    // The least recently used first: slots sorted by their use stamp.
    std::vector<std::pair<uint32_t, uint32_t>> order; // (use, slot)
    order.reserve(pool.size());
    for (uint32_t s = 0; s < pool.size(); ++s)
        if (poolKey[s] != ~uint64_t(0) && (keepSince == 0 || poolUse[s] < keepSince)) order.emplace_back(poolUse[s], s);
    std::sort(order.begin(), order.end());
    size_t done = 0;
    for (const auto& [use, s] : order) {
        if (residentBytes() <= budget) break;
        Entry* e = findEntry(poolKey[s]);
        if (!e || (e->ref & kParked) || e->ref - 1 != s) continue;
        parkSlot(*e, s);
        ++done;
    }
    if (freeSlots.size() > pool.size() / 2) compactPool();
    return done;
}

// The pool without its free slots (its capacity goes back to the allocator): resident tiles move to the front, the
// index follows.
void MicroTiles::compactPool() {
    std::vector<uint32_t> to(pool.size(), ~0u);
    uint32_t n = 0;
    for (uint32_t s = 0; s < pool.size(); ++s)
        if (poolKey[s] != ~uint64_t(0)) to[s] = n++;
    for (auto& v : byTri)
        for (Entry& e : v)
            if (!(e.ref & kParked)) e.ref = to[e.ref - 1] + 1;
    for (uint32_t s = 0; s < pool.size(); ++s)
        if (to[s] != ~0u && to[s] != s) pool[to[s]] = pool[s], poolKey[to[s]] = poolKey[s], poolUse[to[s]] = poolUse[s];
    pool.resize(n), poolKey.resize(n), poolUse.resize(n);
    pool.shrink_to_fit(), poolKey.shrink_to_fit(), poolUse.shrink_to_fit();
    freeSlots.clear();
    freeSlots.shrink_to_fit();
}

void MicroTiles::rescale(int div) {
    if (div <= 1) return;
    for (uint32_t s = 0; s < pool.size(); ++s)
        if (poolKey[s] != ~uint64_t(0))
            for (int16_t& v : pool[s].delta) v = int16_t(int(v) / div);
    Tile t;
    for (uint32_t tri = 0; tri < byTri.size(); ++tri)
        for (Entry& e : byTri[tri])
            if (e.ref & kParked) { // repacked in place of the old record (the arena grows by the new copy; compaction reclaims)
                Parked& p = parked[e.ref & ~kParked];
                if (!unpackTile(arena.data() + p.offset, p.size, t)) continue;
                for (int16_t& v : t.delta) v = int16_t(int(v) / div);
                arenaGarbage += p.size;
                p.offset = uint32_t(arena.size());
                packTile(t, arena);
                p.size = uint32_t(arena.size() - p.offset);
            }
    if (arenaGarbage > arena.size() / 2) compactArena();
}

// --- geometry ---
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

template <class Lookup> float MicroTiles::descend(uint32_t tri, const glm::vec3& w, int deepest, Lookup&& lookup) const {
    float h = 0.0f;
    TileFrame f;
    uint32_t path = 0;
    for (int d = 0; d <= deepest; ++d) {
        float a, b;
        f.coords(w, a, b);
        const int q = TileFrame::quadrantOf(a, b);
        f = f.child(q), path = path << 2 | uint32_t(q);
        if (d == 0) continue; // tree depth 1 = the root's own resolution: no tile there
        const Tile* t = lookup(key(tri, d, path));
        if (!t) continue; // no tile at this depth here = a zero delta (a dab makes tiles at its own depth only: no parents)
        f.coords(w, a, b);
        a = std::clamp(a, 0.0f, 1.0f), b = std::clamp(b, 0.0f, 1.0f - a);
        h += lerpGrid(t->delta.data(), kN, a, b);
    }
    return h;
}

float MicroTiles::deltaAt(uint32_t tri, const glm::vec3& w, int maxDepthWanted) const {
    int deepest = tri < maxDepth.size() ? maxDepth[tri] : 0;
    if (maxDepthWanted >= 0) deepest = std::min(deepest, maxDepthWanted);
    if (deepest <= 0 || !hasTiles(tri)) return 0.0f;
    thread_local Tile scratch;
    return descend(tri, w, deepest, [&](uint64_t k) { return read(k, scratch); });
}

float MicroTiles::heightAt(const MicroMesh& mm, uint32_t tri, const glm::vec3& w, int maxDepthWanted) const {
    return lerpGrid(mm.disp.data() + size_t(tri) * mm.samples, mm.n, w.y, w.z) + deltaAt(tri, w, maxDepthWanted);
}

int16_t MicroTiles::pageValue(uint32_t tri, uint32_t m, uint32_t i, uint32_t j) const { return pageRound(deltaAt(tri, pageWeights(m, i, j))); }

void MicroTiles::pageValues(uint32_t tri, uint32_t m, int16_t* out) const {
    const size_t count = size_t(m + 1) * (m + 2) / 2;
    const int deepest = tri < maxDepth.size() ? maxDepth[tri] : 0;
    if (deepest <= 0 || !hasTiles(tri)) {
        std::fill(out, out + count, int16_t(0));
        return;
    }
    // The triangle's tiles once (parked ones unpacked once, not per point), looked up by key as read() would.
    const std::vector<Entry>& entries = byTri[tri];
    thread_local std::vector<Tile> unpacked;
    thread_local std::vector<const Tile*> found;
    unpacked.resize(entries.size());
    found.assign(entries.size(), nullptr);
    for (size_t k = 0; k < entries.size(); ++k) found[k] = read(uint64_t(tri) << 32 | entries[k].pathKey, unpacked[k]);
    auto lookup = [&](uint64_t key) -> const Tile* {
        const uint32_t pk = uint32_t(key & 0xFFFFFFFFu);
        auto it = std::lower_bound(entries.begin(), entries.end(), pk, ByPath{});
        return it != entries.end() && it->pathKey == pk ? found[size_t(it - entries.begin())] : nullptr;
    };
    for (uint32_t j = 0; j <= m; ++j)
        for (uint32_t i = 0; i + j <= m; ++i) out[MicroMesh::index(i, j, m)] = pageRound(descend(tri, pageWeights(m, i, j), deepest, lookup));
}

} // namespace sf
