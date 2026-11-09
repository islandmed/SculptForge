// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Voxel remesh above resolution 512: a dense grid would be resolution^3 samples (4096: ~70 billion), but every step
// of the remesh only looks a few cells from the surface. Here the grid is stored in 8^3 bricks within reach of the
// surface; every other brick is uniformly inside or outside and is one flag. Each step mirrors the dense one in
// Voxel.cpp sample for sample (tests compare the two), so the result is the same surface, at a memory cost that goes
// with the surface area.
#include "core/VoxelSparse.h"

#include "core/AtomicRef.h"
#include "core/MemoryGuard.h"
#include "core/ThreadPool.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <new>
#include <utility>

namespace sf {

namespace {

constexpr int kB = 8, kB3 = kB * kB * kB;

// Per-sample flags (one byte per stored sample).
constexpr uint8_t kShell = 1, kWall = 2, kOutside = 4, kSolid = 8, kNearOutside = 16, kInside = 32, kReached = 64;

struct Bricks {
    glm::ivec3 dims{0}; // samples
    glm::ivec3 bd{0};   // bricks
    std::vector<int32_t> slot;    // per brick: its storage (x kB3 samples), -1 = uniform
    std::vector<glm::ivec3> at;   // per stored brick: its brick coordinates
    std::vector<uint8_t> out;     // per brick: a uniform brick that is outside

    size_t brick(int bx, int by, int bz) const { return (size_t(bz) * size_t(bd.y) + size_t(by)) * size_t(bd.x) + size_t(bx); }
    size_t brickOf(int x, int y, int z) const { return brick(x >> 3, y >> 3, z >> 3); }
    static int local(int x, int y, int z) { return ((z & 7) << 6) | ((y & 7) << 3) | (x & 7); }
    bool inGrid(int x, int y, int z) const { return x >= 0 && y >= 0 && z >= 0 && x < dims.x && y < dims.y && z < dims.z; }
    bool inGrid(const glm::ivec3& p) const { return inGrid(p.x, p.y, p.z); }
    // Storage index of an in-grid sample, or -1 in a uniform brick.
    int64_t find(int x, int y, int z) const {
        const int32_t s = slot[brickOf(x, y, z)];
        return s < 0 ? -1 : int64_t(s) * kB3 + local(x, y, z);
    }
    glm::ivec3 sample(size_t storage) const {
        const int l = int(storage % kB3);
        return at[storage / kB3] * kB + glm::ivec3(l & 7, (l >> 3) & 7, l >> 6);
    }
};

// Running max (dilate) or min (erode) over a box of +-radius samples, as Voxel.cpp's morph: `src(flags)` per stored
// sample, `uniform(brick)` for the others; off the grid counts as nothing (as the dense morph's clamped ranges). The
// result goes to bit `dst` of every in-grid stored sample.
template <class Src, class Uniform>
void morph(const Bricks& g, std::vector<uint8_t>& flags, Src src, Uniform uniform, uint8_t dst, int radius, bool dilate) {
    std::vector<uint8_t> result(flags.size());
    const int P = kB + 2 * radius;
    const uint8_t none = dilate ? 0 : 1; // off the grid
    poolFor(g.at.size(), [&](size_t b0, size_t b1) {
        std::vector<uint8_t> block(size_t(P * P * P)), tmp(block.size());
        for (size_t s = b0; s < b1; ++s) {
            // The brick and `radius` samples around it, a neighbour brick at a time.
            const glm::ivec3 bc = g.at[s];
            for (int nz = -1; nz <= 1; ++nz)
                for (int ny = -1; ny <= 1; ++ny)
                    for (int nx = -1; nx <= 1; ++nx) {
                        const glm::ivec3 n(nx, ny, nz), nb = bc + n;
                        glm::ivec3 lo, hi; // the part of the block (padded coordinates) that this neighbour covers
                        for (int a = 0; a < 3; ++a) {
                            lo[a] = n[a] < 0 ? 0 : n[a] == 0 ? radius : radius + kB;
                            hi[a] = n[a] < 0 ? radius : n[a] == 0 ? radius + kB : P;
                        }
                        const glm::ivec3 origin = bc * kB - radius; // grid position of block (0,0,0)
                        int32_t slot = -1;
                        uint8_t fill = none;
                        if (glm::all(glm::greaterThanEqual(nb, glm::ivec3(0))) && glm::all(glm::lessThan(nb, g.bd))) {
                            slot = g.slot[g.brick(nb.x, nb.y, nb.z)];
                            if (slot < 0) fill = uniform(g.brick(nb.x, nb.y, nb.z)) ? 1 : 0;
                        }
                        const bool edge = glm::any(glm::greaterThan((nb + 1) * kB, g.dims)); // part of it is off the grid
                        for (int z = lo.z; z < hi.z; ++z)
                            for (int y = lo.y; y < hi.y; ++y) {
                                uint8_t* out = &block[size_t((z * P + y) * P)];
                                const int gy = origin.y + y, gz = origin.z + z;
                                if (slot < 0 && !edge) {
                                    std::fill(out + lo.x, out + hi.x, fill);
                                    continue;
                                }
                                const uint8_t* in = slot < 0 ? nullptr : &flags[size_t(slot) * kB3 + size_t(((gz & 7) << 6) | ((gy & 7) << 3))];
                                for (int x = lo.x; x < hi.x; ++x) {
                                    const int gx = origin.x + x;
                                    out[x] = edge && !g.inGrid(gx, gy, gz) ? none : !in ? fill : src(in[gx & 7]) ? 1 : 0;
                                }
                            }
                    }
            // Separable running max / min, as Voxel.cpp's morph: x over all padded rows, then y, then z.
            auto pass = [&](const std::vector<uint8_t>& in, std::vector<uint8_t>& o, int stride, int x0, int x1, int y0, int y1,
                            int z0, int z1) {
                for (int z = z0; z < z1; ++z)
                    for (int y = y0; y < y1; ++y)
                        for (int x = x0; x < x1; ++x) {
                            const size_t c = size_t((z * P + y) * P + x);
                            uint8_t r = in[c];
                            for (int k = 1; k <= radius; ++k) {
                                const uint8_t a = in[c - size_t(k * stride)], b = in[c + size_t(k * stride)];
                                r = dilate ? std::max({r, a, b}) : std::min({r, a, b});
                            }
                            o[c] = r;
                        }
            };
            const int r0 = radius, r1 = radius + kB;
            pass(block, tmp, 1, r0, r1, 0, P, 0, P);
            pass(tmp, block, P, r0, r1, r0, r1, 0, P);
            pass(block, tmp, P * P, r0, r1, r0, r1, r0, r1);
            for (int l = 0; l < kB3; ++l)
                result[s * kB3 + size_t(l)] = tmp[size_t((((l >> 6) + radius) * P + ((l >> 3) & 7) + radius) * P + (l & 7) + radius)];
        }
    }, 16);
    poolFor(g.at.size(), [&](size_t b0, size_t b1) {
        for (size_t s = b0; s < b1; ++s) {
            const bool edge = glm::any(glm::greaterThan((g.at[s] + 1) * kB, g.dims));
            for (size_t i = s * kB3; i < (s + 1) * kB3; ++i)
                if (!edge || g.inGrid(g.sample(i))) flags[i] = uint8_t(result[i] ? flags[i] | dst : flags[i] & ~dst);
        }
    }, 64);
}

} // namespace

Mesh sparseVoxelSurface(const Mesh& mesh, int resolution, int padding, float band, bool checkClosed, std::string* error,
                        const std::function<bool(float)>& step) {
    auto fail = [&](const std::string& why) {
        if (error) *error = why;
        return Mesh();
    };
    const AABB box = mesh.bounds();
    if (!box.valid()) return fail("The mesh is empty");
    resolution = std::max(resolution, 4);
    // The grid as makeVoxelGrid lays it out.
    const glm::vec3 ext = glm::max(box.extent(), glm::vec3(1e-6f));
    const float cell = std::max({ext.x, ext.y, ext.z}) / float(resolution);
    Bricks g;
    g.dims = glm::ivec3(glm::ceil(ext / cell)) + 1 + 2 * padding;
    const glm::vec3 origin = box.center() - glm::vec3(g.dims - 1) * 0.5f * cell;
    g.bd = (g.dims + kB - 1) / kB;
    const size_t bricks = size_t(g.bd.x) * size_t(g.bd.y) * size_t(g.bd.z);
    const auto& P = mesh.positions;
    const float inv = 1.0f / cell;
    const uint32_t triangles = uint32_t(mesh.triangleCount());
    auto localTri = [&](uint32_t t, glm::vec3& a, glm::vec3& b, glm::vec3& c) {
        const glm::uvec3 tri = mesh.triangles[t];
        a = (P[tri.x] - origin) * inv, b = (P[tri.y] - origin) * inv, c = (P[tri.z] - origin) * inv;
    };

    // 1. Bricks within reach: every sample within `reach` cells of a triangle is in a stored brick, which covers the
    // distance band and the few cells the gap closing, erosion and sign probes look past it.
    const float reach = band + 4.0f;
    {
        const size_t need = bricks * 6;
        if (const MemoryCheck c = checkMemory(need); !c.enough)
            return fail("Voxel Remesh at " + std::to_string(resolution) + " needs ~" + formatGigabytes(c.needBytes + memoryMargin(c.needBytes)) +
                        " free, " + formatGigabytes(c.availableBytes) + " available. Lower the Resolution or close other apps");
    }
    std::vector<uint8_t> mark(bricks, 0);
    auto markRange = [&](glm::vec3 lo, glm::vec3 hi) {
        const glm::ivec3 l = glm::clamp(glm::ivec3(glm::floor(lo)), glm::ivec3(0), g.dims - 1) / kB;
        const glm::ivec3 h = glm::clamp(glm::ivec3(glm::ceil(hi)), glm::ivec3(0), g.dims - 1) / kB;
        for (int z = l.z; z <= h.z; ++z)
            for (int y = l.y; y <= h.y; ++y)
                for (int x = l.x; x <= h.x; ++x) AtomicRef<uint8_t>(mark[g.brick(x, y, z)]).store(1, std::memory_order_relaxed);
    };
    poolFor(triangles, [&](size_t b0, size_t e0) {
        for (size_t t = b0; t < e0; ++t) {
            glm::vec3 a, b, c;
            localTri(uint32_t(t), a, b, c);
            if (const glm::vec3 sum = a + b + c; !std::isfinite(sum.x) || !std::isfinite(sum.y) || !std::isfinite(sum.z)) continue;
            const float longest = std::max({glm::distance(a, b), glm::distance(b, c), glm::distance(c, a)});
            if (longest <= float(kB)) {
                markRange(glm::min(a, glm::min(b, c)) - reach, glm::max(a, glm::max(b, c)) + reach);
                continue;
            }
            // A big triangle: points at most 4 cells apart, each covering the reach plus the 4 to its neighbours.
            const int steps = int(std::ceil(longest / 4.0f));
            for (int i = 0; i <= steps; ++i)
                for (int j = 0; i + j <= steps; ++j) {
                    const glm::vec3 q = a + (b - a) * (float(i) / float(steps)) + (c - a) * (float(j) / float(steps));
                    markRange(q - (reach + 4.0f), q + (reach + 4.0f));
                }
        }
    }, 1024);
    g.slot.assign(bricks, -1);
    for (size_t i = 0; i < bricks; ++i)
        if (mark[i]) {
            g.slot[i] = int32_t(g.at.size());
            const int bx = int(i % size_t(g.bd.x)), by = int((i / size_t(g.bd.x)) % size_t(g.bd.y)), bz = int(i / (size_t(g.bd.x) * size_t(g.bd.y)));
            g.at.emplace_back(bx, by, bz);
        }
    std::vector<uint8_t>().swap(mark);
    const size_t stored = g.at.size() * kB3;
    {
        // Flags, morph scratch, distance and nearest triangle per stored sample, the surface it makes.
        const size_t need = stored * 11 + size_t(double(stored) * 0.6);
        if (const MemoryCheck c = checkMemory(need); !c.enough)
            return fail("Voxel Remesh at " + std::to_string(resolution) + " needs ~" + formatGigabytes(c.needBytes + memoryMargin(c.needBytes)) +
                        " free, " + formatGigabytes(c.availableBytes) + " available. Lower the Resolution or close other apps");
    }
    g.out.assign(bricks, 0);
    if (!step(0.05f)) return {};

    // 2. The shell (as rasterizeMeshShell), closed by one cell (wall), the outside flooded from the grid border.
    std::vector<uint8_t> flags(stored, 0);
    for (const glm::uvec3& tri : mesh.triangles) {
        const glm::vec3 a = (P[tri.x] - origin) / cell, b = (P[tri.y] - origin) / cell, c = (P[tri.z] - origin) / cell;
        const float longest = std::max({glm::distance(a, b), glm::distance(b, c), glm::distance(c, a)});
        const int steps = std::max(1, int(std::ceil(longest * 2.0f)));
        for (int i = 0; i <= steps; ++i)
            for (int j = 0; i + j <= steps; ++j) {
                const float u = float(i) / float(steps), v = float(j) / float(steps);
                const glm::ivec3 s(glm::round(a + (b - a) * u + (c - a) * v));
                if (!g.inGrid(s)) continue;
                if (const int64_t k = g.find(s.x, s.y, s.z); k >= 0) flags[size_t(k)] |= kShell;
            }
    }
    morph(g, flags, [](uint8_t f) { return (f & kShell) != 0; }, [](size_t) { return false; }, kWall, 1, true);
    {
        // Brick by brick: a stored brick fills from its samples next to the outside; a brick that gains outside samples
        // on a face wakes the brick beyond it (a uniform one is then all outside: nothing in it blocks).
        auto outsideAt = [&](const glm::ivec3& p) {
            if (!g.inGrid(p)) return true;
            const int64_t i = g.find(p.x, p.y, p.z);
            return i < 0 ? g.out[g.brickOf(p.x, p.y, p.z)] != 0 : (flags[size_t(i)] & kOutside) != 0;
        };
        std::vector<uint32_t> queue;
        std::vector<uint8_t> queued(bricks, 0); // the faces (bit k: across dirs[k]) a woken brick looks across
        auto wake = [&](size_t b, uint8_t faces) {
            if (!queued[b]) queue.push_back(uint32_t(b));
            queued[b] |= faces;
        };
        for (int z = 0; z < g.bd.z; ++z)
            for (int y = 0; y < g.bd.y; ++y)
                for (int x = 0; x < g.bd.x; ++x)
                    if (x == 0 || y == 0 || z == 0 || x == g.bd.x - 1 || y == g.bd.y - 1 || z == g.bd.z - 1) {
                        const size_t b = g.brick(x, y, z);
                        if (g.slot[b] < 0) g.out[b] = 1;
                        wake(b, 0x3f);
                    }
        const glm::ivec3 dirs[6] = {{-1, 0, 0}, {1, 0, 0}, {0, -1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}};
        std::vector<uint16_t> stack;
        while (!queue.empty()) {
            const size_t b = queue.back();
            queue.pop_back();
            const uint8_t faces = std::exchange(queued[b], uint8_t(0));
            const glm::ivec3 bc(int(b % size_t(g.bd.x)), int((b / size_t(g.bd.x)) % size_t(g.bd.y)), int(b / (size_t(g.bd.x) * size_t(g.bd.y))));
            const int32_t s = g.slot[b];
            bool face[6] = {};
            if (s < 0) {
                for (bool& f : face) f = true;
            } else {
                uint8_t* f = &flags[size_t(s) * kB3];
                const glm::ivec3 base = bc * kB;
                const bool edge = glm::any(glm::greaterThan(base + kB, g.dims)); // (part of it is off the grid)
                auto open = [&](int l) {
                    return !(f[l] & (kWall | kOutside)) && (!edge || g.inGrid(base + glm::ivec3(l & 7, (l >> 3) & 7, l >> 6)));
                };
                auto claim = [&](int l) {
                    f[l] |= kOutside;
                    stack.push_back(uint16_t(l));
                    const int x = l & 7, y = (l >> 3) & 7, z = l >> 6;
                    face[0] |= x == 0, face[1] |= x == 7, face[2] |= y == 0, face[3] |= y == 7, face[4] |= z == 0, face[5] |= z == 7;
                };
                // The brick's own outside is closed (filled when it last ran), so new outside only comes in across the
                // faces whose neighbour gained some: each sample on them looks at the sample beyond. On the grid's far
                // edge the face is the last sample on the grid, whose neighbour beyond is off it (outside).
                const glm::ivec3 last = glm::min(g.dims - 1 - base, glm::ivec3(kB - 1));
                for (int k = 0; k < 6; ++k) {
                    if (!(faces & (1 << k))) continue;
                    const int axis = k / 2, u = (axis + 1) % 3, v = (axis + 2) % 3;
                    for (int j = 0; j < kB * kB; ++j) {
                        glm::ivec3 c(0);
                        c[axis] = (k & 1) ? last[axis] : 0;
                        c[u] = j & 7;
                        c[v] = j >> 3;
                        if (c[u] > last[u] || c[v] > last[v]) continue;
                        const int l = Bricks::local(c.x, c.y, c.z);
                        if (open(l) && outsideAt(base + c + dirs[k])) claim(l);
                    }
                }
                while (!stack.empty()) {
                    const int l = stack.back();
                    stack.pop_back();
                    const int x = l & 7, y = (l >> 3) & 7, z = l >> 6;
                    if (x > 0 && open(l - 1)) claim(l - 1);
                    if (x < 7 && open(l + 1)) claim(l + 1);
                    if (y > 0 && open(l - 8)) claim(l - 8);
                    if (y < 7 && open(l + 8)) claim(l + 8);
                    if (z > 0 && open(l - 64)) claim(l - 64);
                    if (z < 7 && open(l + 64)) claim(l + 64);
                }
            }
            for (int k = 0; k < 6; ++k) {
                if (!face[k]) continue;
                const glm::ivec3 n = bc + dirs[k];
                if (glm::any(glm::lessThan(n, glm::ivec3(0))) || glm::any(glm::greaterThanEqual(n, g.bd))) continue;
                const size_t nb = g.brick(n.x, n.y, n.z);
                if (g.slot[nb] >= 0) wake(nb, uint8_t(1 << (k ^ 1))); // (its face towards this brick)
                else if (!g.out[nb]) {
                    g.out[nb] = 1;
                    wake(nb, 0x3f);
                }
            }
        }
    }
    // Solid: not outside, eroded by the cell the wall added.
    morph(g, flags, [](uint8_t f) { return !(f & kOutside); }, [&](size_t b) { return !g.out[b]; }, kSolid, 1, false);
    if (!step(0.25f)) return {};
    if (checkClosed) { // as voxelRemesh's check on the dense grid
        double volume = 0.0;
        for (const glm::uvec3& t : mesh.triangles)
            volume += glm::dot(glm::dvec3(P[t.x]), glm::cross(glm::dvec3(P[t.y]), glm::dvec3(P[t.z])));
        const double cells = std::abs(volume) / 6.0 / (double(cell) * cell * cell);
        std::vector<double> part((g.at.size() + 255) / 256, 0.0);
        poolFor(g.at.size(), [&](size_t b0, size_t b1) {
            double n = 0.0;
            for (size_t i = b0 * kB3; i < b1 * kB3; ++i)
                if ((flags[i] & kSolid) && g.inGrid(g.sample(i))) n += 1.0;
            part[b0 / 256] = n;
        }, 256);
        double inside = 0.0;
        for (double n : part) inside += n;
        for (int z = 0; z < g.bd.z; ++z)
            for (int y = 0; y < g.bd.y; ++y)
                for (int x = 0; x < g.bd.x; ++x) {
                    const size_t b = g.brick(x, y, z);
                    if (g.slot[b] >= 0 || g.out[b]) continue;
                    const glm::ivec3 n = glm::min(g.dims - glm::ivec3(x, y, z) * kB, glm::ivec3(kB));
                    inside += double(n.x) * n.y * n.z;
                }
        if (cells > 64.0 && inside < 0.5 * cells)
            return fail("Remesh: the Part has open borders that couldn't be capped, so it isn't a closed solid and the "
                        "remesh would be full of holes; the Part is unchanged. Close it first (Tool > Geometry > Fill Holes)");
    }

    // 3. Exact distance within the band, per stored brick, from the triangles that reach it (in index order, as
    // insideDistanceField's slabs).
    std::vector<float> dist2(stored, band * band);
    std::vector<int32_t> nearest(stored, -1);
    {
        auto range = [&](uint32_t t, glm::vec3& a, glm::vec3& b, glm::vec3& c, glm::ivec3& lo, glm::ivec3& hi) {
            localTri(t, a, b, c);
            lo = glm::max(glm::ivec3(glm::floor(glm::min(a, glm::min(b, c)) - band)), glm::ivec3(0));
            hi = glm::min(glm::ivec3(glm::ceil(glm::max(a, glm::max(b, c)) + band)), g.dims - 1);
        };
        std::vector<uint32_t> start(g.at.size() + 1, 0);
        auto each = [&](uint32_t t, auto&& fn) {
            glm::vec3 a, b, c;
            glm::ivec3 lo, hi;
            range(t, a, b, c, lo, hi);
            if (lo.x > hi.x || lo.y > hi.y || lo.z > hi.z) return; // (NaN, or off the grid)
            lo /= kB, hi /= kB;
            for (int z = lo.z; z <= hi.z; ++z)
                for (int y = lo.y; y <= hi.y; ++y)
                    for (int x = lo.x; x <= hi.x; ++x)
                        if (const int32_t s = g.slot[g.brick(x, y, z)]; s >= 0) fn(uint32_t(s));
        };
        for (uint32_t t = 0; t < triangles; ++t) each(t, [&](uint32_t s) { ++start[s + 1]; });
        for (size_t s = 0; s < g.at.size(); ++s) start[s + 1] += start[s];
        std::vector<uint32_t> list(start.back());
        {
            std::vector<uint32_t> cursor(start.begin(), start.end() - 1);
            for (uint32_t t = 0; t < triangles; ++t) each(t, [&](uint32_t s) { list[cursor[s]++] = t; });
        }
        // In chunks, so a cancel stops within a few seconds even at 4096.
        const size_t chunks = 20;
        for (size_t chunk = 0; chunk < chunks; ++chunk) {
            const size_t c0 = g.at.size() * chunk / chunks, c1 = g.at.size() * (chunk + 1) / chunks;
            poolFor(c1 - c0, [&](size_t sb, size_t se) {
                for (size_t s = c0 + sb; s < c0 + se; ++s) {
                    const glm::ivec3 b0 = g.at[s] * kB, b1 = glm::min(b0 + kB, g.dims) - 1;
                    for (uint32_t k = start[s]; k < start[s + 1]; ++k) {
                        const uint32_t t = list[k];
                        glm::vec3 a, b, c;
                        glm::ivec3 lo, hi;
                        range(t, a, b, c, lo, hi);
                        lo = glm::max(lo, b0), hi = glm::min(hi, b1);
                        const glm::vec3 centre = (a + b + c) / 3.0f;
                        const float radius = std::sqrt(std::max({glm::dot(a - centre, a - centre), glm::dot(b - centre, b - centre),
                                                                 glm::dot(c - centre, c - centre)})) + 1e-3f;
                        const float within = radius + band;
                        const float within2 = within * within;
                        for (int z = lo.z; z <= hi.z; ++z) {
                            const float dz = float(z) - centre.z, rz = within2 - dz * dz;
                            if (rz < 0.0f) continue;
                            for (int y = lo.y; y <= hi.y; ++y) {
                                const float dy = float(y) - centre.y, ry = rz - dy * dy;
                                if (ry < 0.0f) continue;
                                const float half = std::sqrt(ry);
                                const int x0 = std::max(lo.x, int(std::ceil(centre.x - half)));
                                const int x1 = std::min(hi.x, int(std::floor(centre.x + half)));
                                for (int x = x0; x <= x1; ++x) {
                                    const glm::vec3 p(x, y, z);
                                    const size_t i = s * kB3 + size_t(Bricks::local(x, y, z));
                                    const float far = std::sqrt(dist2[i]) + radius;
                                    if (glm::dot(p - centre, p - centre) > far * far) continue;
                                    const glm::vec3 d = p - closestOnTriangle(p, a, b, c);
                                    const float d2 = glm::dot(d, d);
                                    if (d2 < dist2[i]) {
                                        dist2[i] = d2;
                                        nearest[i] = int32_t(t);
                                    }
                                }
                            }
                        }
                    }
                }
            }, 4);
            if (!step(0.25f + 0.3f * float(chunk + 1) / float(chunks))) return {};
        }
    }

    // 4. Sign, as insideDistanceField: near the outside the nearest triangle's side decides; a surface buried in
    // another part doesn't count; outside pockets that can't reach the real outside are inside.
    morph(g, flags, [](uint8_t f) { return !(f & kSolid); }, [&](size_t b) { return g.out[b] != 0; }, kNearOutside, 2, true);
    auto solidAt = [&](const glm::ivec3& p) {
        const int64_t i = g.find(p.x, p.y, p.z);
        return i < 0 ? !g.out[g.brickOf(p.x, p.y, p.z)] : (flags[size_t(i)] & kSolid) != 0;
    };
    poolFor(stored, [&](size_t b, size_t e) {
        for (size_t i = b; i < e; ++i) {
            const glm::ivec3 s = g.sample(i);
            if (!g.inGrid(s)) continue;
            bool inside;
            if (!(flags[i] & kSolid)) inside = false;
            else if (!(flags[i] & kNearOutside) || nearest[i] < 0) inside = true;
            else {
                const glm::uvec3 tri = mesh.triangles[size_t(nearest[i])];
                const glm::vec3 nrm = glm::cross(P[tri.y] - P[tri.x], P[tri.z] - P[tri.x]);
                const glm::vec3 p = origin + glm::vec3(s) * cell;
                const glm::vec3 onSurface = closestOnTriangle(p, P[tri.x], P[tri.y], P[tri.z]);
                const float len = glm::length(nrm);
                const glm::ivec3 probe(glm::round((onSurface + nrm * (2.5f * cell / std::max(len, 1e-20f)) - origin) / cell));
                const bool buried = len > 0.0f && g.inGrid(probe) && solidAt(probe);
                inside = buried || glm::dot(p - onSurface, nrm) < 0.0f;
            }
            if (inside) flags[i] |= kInside;
            if (!(flags[i] & kSolid)) flags[i] |= kReached;
        }
    }, 1 << 14);
    {
        auto storageAt = [&](const glm::ivec3& p) { return g.inGrid(p) ? g.find(p.x, p.y, p.z) : int64_t(-1); };
        const glm::ivec3 dirs[6] = {{-1, 0, 0}, {1, 0, 0}, {0, -1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}};
        std::vector<std::vector<uint64_t>> seeds((g.at.size() + 255) / 256);
        poolFor(g.at.size(), [&](size_t b0, size_t b1) {
            std::vector<uint64_t>& out = seeds[b0 / 256];
            for (size_t i = b0 * kB3; i < b1 * kB3; ++i) {
                if (flags[i] & kSolid) continue;
                const glm::ivec3 p = g.sample(i);
                if (!g.inGrid(p)) continue;
                for (const glm::ivec3& d : dirs)
                    if (const int64_t j = storageAt(p + d); j >= 0 && (flags[size_t(j)] & kSolid) && !(flags[size_t(j)] & kInside)) {
                        out.push_back(i);
                        break;
                    }
            }
        }, 256);
        std::vector<uint64_t> stack;
        for (auto& v : seeds) {
            stack.insert(stack.end(), v.begin(), v.end());
            std::vector<uint64_t>().swap(v);
        }
        while (!stack.empty()) {
            const glm::ivec3 p = g.sample(size_t(stack.back()));
            stack.pop_back();
            for (const glm::ivec3& d : dirs)
                if (const int64_t j = storageAt(p + d); j >= 0 && !(flags[size_t(j)] & (kReached | kInside))) {
                    flags[size_t(j)] |= kReached;
                    stack.push_back(uint64_t(j));
                }
        }
    }
    std::vector<float>& f = dist2; // the signed distance, in place
    poolFor(stored, [&](size_t b, size_t e) {
        for (size_t i = b; i < e; ++i) {
            const float d = std::sqrt(dist2[i]);
            f[i] = (flags[i] & kInside) || !(flags[i] & kReached) ? d : -d;
        }
    }, 1 << 16);
    std::vector<uint8_t>().swap(flags);
    if (!step(0.6f)) return {};

    // 5. Surface nets, as meshFromField, over the stored bricks only (a uniform brick has no crossing in or next to
    // it), in the same z, y, x order, so vertices and triangles come out numbered as the dense grid numbers them.
    const float iso = 0.0f;
    auto fAt = [&](int x, int y, int z) {
        const int64_t i = g.find(x, y, z);
        return i < 0 ? (g.out[g.brickOf(x, y, z)] ? -band : band) : f[size_t(i)];
    };
    // Stored bricks per row of bricks along x (by, bz), in x order.
    std::vector<uint32_t> rowStart(size_t(g.bd.y) * size_t(g.bd.z) + 1, 0);
    for (const glm::ivec3& b : g.at) ++rowStart[size_t(b.z) * size_t(g.bd.y) + size_t(b.y) + 1];
    for (size_t r = 0; r + 1 < rowStart.size(); ++r) rowStart[r + 1] += rowStart[r];
    // (g.at is in brick order: z, y, x, so a row's bricks are consecutive and its slots run from rowStart[row].)
    auto row = [&](int y, int z) { return size_t(z >> 3) * size_t(g.bd.y) + size_t(y >> 3); };
    std::vector<int32_t>& cellVertex = nearest;
    std::fill(cellVertex.begin(), cellVertex.end(), -1);
    const glm::ivec3 cells = g.dims - 1;
    if (glm::any(glm::lessThan(cells, glm::ivec3(1)))) return {};
    std::vector<std::vector<glm::vec3>> slicePositions(size_t(cells.z));
    poolFor(size_t(cells.z), [&](size_t zb, size_t ze) {
        for (int z = int(zb); z < int(ze); ++z) {
            std::vector<glm::vec3>& out = slicePositions[size_t(z)];
            for (int y = 0; y < cells.y; ++y) {
                const size_t r = row(y, z);
                for (uint32_t s = rowStart[r]; s < rowStart[r + 1]; ++s) {
                    const int bx = g.at[s].x * kB;
                    for (int x = bx; x < std::min(bx + kB, cells.x); ++x) {
                        std::array<float, 8> c;
                        int inside = 0;
                        for (int k = 0; k < 8; ++k) {
                            c[size_t(k)] = fAt(x + (k & 1), y + ((k >> 1) & 1), z + ((k >> 2) & 1));
                            inside += c[size_t(k)] > iso;
                        }
                        if (inside == 0 || inside == 8) continue;
                        glm::vec3 sum(0.0f);
                        int crossings = 0;
                        for (int a = 0; a < 8; ++a)
                            for (int bit = 1; bit < 8; bit <<= 1) {
                                if (a & bit) continue;
                                const int b = a | bit;
                                const float fa = c[size_t(a)], fb = c[size_t(b)];
                                if ((fa > iso) == (fb > iso)) continue;
                                const float t = (iso - fa) / (fb - fa);
                                const glm::vec3 pa(a & 1, (a >> 1) & 1, (a >> 2) & 1);
                                const glm::vec3 pb(b & 1, (b >> 1) & 1, (b >> 2) & 1);
                                sum += glm::mix(pa, pb, t);
                                ++crossings;
                            }
                        cellVertex[s * kB3 + size_t(Bricks::local(x, y, z))] = int32_t(out.size());
                        out.push_back(origin + glm::vec3(x, y, z) * cell + sum / float(crossings) * cell);
                    }
                }
            }
        }
    }, 1);
    std::vector<size_t> firstVertex(size_t(cells.z) + 1, 0);
    for (size_t z = 0; z < size_t(cells.z); ++z) firstVertex[z + 1] = firstVertex[z] + slicePositions[z].size();
    if (firstVertex.back() >= size_t(INT32_MAX)) return fail("Voxel Remesh: the surface would have over 2 billion points; lower the Resolution");
    Mesh m;
    m.positions.resize(firstVertex.back());
    poolFor(size_t(cells.z), [&](size_t zb, size_t ze) {
        for (size_t z = zb; z < ze; ++z) {
            std::copy(slicePositions[z].begin(), slicePositions[z].end(), m.positions.begin() + ptrdiff_t(firstVertex[z]));
            std::vector<glm::vec3>().swap(slicePositions[z]);
            if (firstVertex[z] == 0) continue;
            for (int y = 0; y < cells.y; ++y) {
                const size_t r = row(y, int(z));
                for (uint32_t s = rowStart[r]; s < rowStart[r + 1]; ++s) {
                    const int bx = g.at[s].x * kB;
                    for (int x = bx; x < std::min(bx + kB, cells.x); ++x)
                        if (int32_t& v = cellVertex[s * kB3 + size_t(Bricks::local(x, y, int(z)))]; v >= 0) v += int32_t(firstVertex[z]);
                }
            }
        }
    }, 1);
    auto vertexAt = [&](const glm::ivec3& c) {
        const int64_t i = g.find(c.x, c.y, c.z);
        return i < 0 ? int32_t(-1) : cellVertex[size_t(i)];
    };
    std::vector<std::vector<glm::uvec3>> sliceTriangles(size_t(g.dims.z));
    poolFor(size_t(g.dims.z), [&](size_t zb, size_t ze) {
        for (int z = int(zb); z < int(ze); ++z) {
            std::vector<glm::uvec3>& out = sliceTriangles[size_t(z)];
            for (int y = 0; y < g.dims.y; ++y) {
                const size_t r = row(y, z);
                for (uint32_t s = rowStart[r]; s < rowStart[r + 1]; ++s) {
                    const int bx = g.at[s].x * kB;
                    for (int x = bx; x < std::min(bx + kB, g.dims.x); ++x) {
                        const glm::ivec3 p(x, y, z);
                        const bool in0 = f[s * kB3 + size_t(Bricks::local(x, y, z))] > iso;
                        for (int axis = 0; axis < 3; ++axis) {
                            glm::ivec3 q = p;
                            q[axis] += 1;
                            if (q[axis] >= g.dims[axis]) continue;
                            const bool in1 = fAt(q.x, q.y, q.z) > iso;
                            if (in0 == in1) continue;
                            const int u = (axis + 1) % 3, v = (axis + 2) % 3;
                            if (p[u] < 1 || p[v] < 1 || p[u] > cells[u] - 1 || p[v] > cells[v] - 1 || p[axis] > cells[axis] - 1)
                                continue;
                            std::array<int32_t, 4> quad;
                            const int du[4] = {-1, 0, 0, -1}, dv[4] = {-1, -1, 0, 0};
                            bool ok = true;
                            for (int k = 0; k < 4; ++k) {
                                glm::ivec3 c = p;
                                c[u] += du[k];
                                c[v] += dv[k];
                                quad[size_t(k)] = vertexAt(c);
                                ok &= quad[size_t(k)] >= 0;
                            }
                            if (!ok) continue;
                            if (!in0) std::swap(quad[1], quad[3]); // outward normal must point to the outside sample
                            const auto& Q = m.positions;
                            const bool diag02 = glm::distance(Q[size_t(quad[0])], Q[size_t(quad[2])]) <=
                                                glm::distance(Q[size_t(quad[1])], Q[size_t(quad[3])]);
                            const glm::uvec4 qq(quad[0], quad[1], quad[2], quad[3]);
                            if (diag02) {
                                out.push_back({qq.x, qq.y, qq.z});
                                out.push_back({qq.x, qq.z, qq.w});
                            } else {
                                out.push_back({qq.x, qq.y, qq.w});
                                out.push_back({qq.y, qq.z, qq.w});
                            }
                        }
                    }
                }
            }
        }
    }, 1);
    std::vector<float>().swap(dist2);
    std::vector<int32_t>().swap(nearest);
    std::vector<int32_t>().swap(g.slot);
    size_t triangleTotal = 0;
    for (const auto& t : sliceTriangles) triangleTotal += t.size();
    m.triangles.reserve(triangleTotal);
    for (auto& t : sliceTriangles) {
        m.triangles.insert(m.triangles.end(), t.begin(), t.end());
        std::vector<glm::uvec3>().swap(t);
    }
    for (uint32_t t = 0; t + 1 < m.triangles.size(); t += 2) m.pairQuad(t, t + 1);
    m.rebuildTopology();
    taubinSmooth(m, 2);
    m.recomputeAllNormals();
    return m;
}

} // namespace sf
