// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/Sculptable.h"

#include "core/AutoUv.h"
#include "core/BVH.h"
#include "core/Commands.h"
#include "core/Document.h"
#include "core/Json.h"
#include "core/Multires.h"
#include "core/Parallel.h"
#include "core/Primitives.h"
#include "core/Projection.h"
#include "core/Sculpt.h"
#include "core/Texture.h"
#include "core/Voxel.h"

#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <limits>
#include <random>
#include <sstream>

namespace sf {

namespace {

using glm::ivec3;
using glm::vec2;
using glm::vec3;
using Clock = std::chrono::steady_clock;

double secondsSince(Clock::time_point t) { return std::chrono::duration<double>(Clock::now() - t).count(); }

bool tick(const SculptableProgress& progress, float f) { return !progress || progress(std::clamp(f, 0.0f, 1.0f)); }

// A sub-range [a, b] of a progress callback.
SculptableProgress subProgress(const SculptableProgress& progress, float a, float b) {
    if (!progress) return {};
    return [progress, a, b](float f) { return progress(a + (b - a) * std::clamp(f, 0.0f, 1.0f)); };
}

// --- Voxel lines: [base + i * stride] for i < length, every line of the grid along `axis` ---
struct GridLines {
    ivec3 dims;
    int axis;
    size_t count() const {
        const int u = (axis + 1) % 3, v = (axis + 2) % 3;
        return size_t(dims[u]) * size_t(dims[v]);
    }
    size_t stride() const { return axis == 0 ? 1 : axis == 1 ? size_t(dims.x) : size_t(dims.x) * size_t(dims.y); }
    int length() const { return dims[axis]; }
    size_t base(size_t line) const {
        const int u = (axis + 1) % 3, v = (axis + 2) % 3;
        ivec3 p(0);
        p[u] = int(line % size_t(dims[u]));
        p[v] = int(line / size_t(dims[u]));
        return (size_t(p.z) * size_t(dims.y) + size_t(p.y)) * size_t(dims.x) + size_t(p.x);
    }
};

// Box dilation (max) or erosion (min) of a 0/1 grid with radius r, axis by axis; every line on its own, so the
// result is the same with any number of threads.
void morph(std::vector<uint8_t>& data, const ivec3& dims, int r, bool dilate) {
    if (r <= 0) return;
    for (int axis = 0; axis < 3; ++axis) {
        const GridLines lines{dims, axis};
        const size_t stride = lines.stride();
        const int len = lines.length();
        parallelFor(lines.count(), [&](size_t b, size_t e) {
            std::vector<uint8_t> in(static_cast<size_t>(len));
            for (size_t l = b; l < e; ++l) {
                const size_t base = lines.base(l);
                for (int i = 0; i < len; ++i) in[size_t(i)] = data[base + size_t(i) * stride];
                // Running count of set cells in the window [i - r, i + r].
                int count = 0;
                for (int i = 0; i < std::min(r, len); ++i) count += in[size_t(i)];
                for (int i = 0; i < len; ++i) {
                    if (i + r < len) count += in[size_t(i + r)];
                    if (i - r - 1 >= 0) count -= in[size_t(i - r - 1)];
                    const int window = std::min(i + r, len - 1) - std::max(i - r, 0) + 1;
                    data[base + size_t(i) * stride] = dilate ? uint8_t(count > 0) : uint8_t(count == window);
                }
            }
        }, 256);
    }
}

// Breadth-first flood over cells where `open` is set, from the seeds; marks `reached`.
void flood(const std::vector<uint8_t>& open, const ivec3& dims, std::vector<uint32_t>& stack, std::vector<uint8_t>& reached) {
    const int64_t sy = dims.x, sz = int64_t(dims.x) * dims.y;
    while (!stack.empty()) {
        const uint32_t i = stack.back();
        stack.pop_back();
        const int x = int(i % uint32_t(dims.x));
        const int y = int((i / uint32_t(dims.x)) % uint32_t(dims.y));
        const int z = int(i / uint32_t(sz));
        const int64_t offsets[6] = {-1, 1, -sy, sy, -sz, sz};
        const bool valid[6] = {x > 0, x + 1 < dims.x, y > 0, y + 1 < dims.y, z > 0, z + 1 < dims.z};
        for (int k = 0; k < 6; ++k) {
            if (!valid[k]) continue;
            const uint32_t j = uint32_t(int64_t(i) + offsets[k]);
            if (open[j] && !reached[j]) {
                reached[j] = 1;
                stack.push_back(j);
            }
        }
    }
}

// The same Gaussian blur as meshFromSolid (5 taps, sigma 1 voxel), line by line on all threads.
void blurField(std::vector<float>& f, const ivec3& dims) {
    float w[5], sum = 0.0f;
    for (int i = 0; i < 5; ++i) sum += w[i] = std::exp(-0.5f * float((i - 2) * (i - 2)));
    for (float& x : w) x /= sum;
    for (int axis = 0; axis < 3; ++axis) {
        const GridLines lines{dims, axis};
        const size_t stride = lines.stride();
        const int len = lines.length();
        parallelFor(lines.count(), [&](size_t b, size_t e) {
            std::vector<float> line(static_cast<size_t>(len));
            for (size_t l = b; l < e; ++l) {
                const size_t base = lines.base(l);
                for (int i = 0; i < len; ++i) line[size_t(i)] = f[base + size_t(i) * stride];
                for (int i = 0; i < len; ++i) {
                    float s = 0.0f;
                    for (int k = -2; k <= 2; ++k) s += w[k + 2] * line[size_t(std::clamp(i + k, 0, len - 1))];
                    f[base + size_t(i) * stride] = s;
                }
            }
        }, 256);
    }
}

// Pieces of the corner set `mask` of a voxel cube, joined along the cube's edges.
int cubePieces(unsigned mask) {
    int pieces = 0;
    unsigned seen = 0;
    for (int c = 0; c < 8; ++c) {
        if (!(mask >> c & 1u) || (seen >> c & 1u)) continue;
        ++pieces;
        unsigned frontier = 1u << c;
        seen |= frontier;
        while (frontier) {
            unsigned next = 0;
            for (int a = 0; a < 8; ++a)
                if (frontier >> a & 1u)
                    for (int bit = 1; bit < 8; bit <<= 1) {
                        const int b = a ^ bit;
                        if ((mask >> b & 1u) && !(seen >> b & 1u)) next |= 1u << b;
                    }
            seen |= next;
            frontier = next;
        }
    }
    return pieces;
}

// Surface nets puts one vertex in each cube the surface crosses, so a cube whose inside (or outside) corners
// fall apart into separate pieces (two corners touching only diagonally) joins two sheets in one vertex: a
// non-manifold vertex or edge. Such cubes are filled corner by corner (the outside corner nearest the iso
// value goes inside) until none is left; filling only ever adds, so it ends.
size_t fixAmbiguousCubes(std::vector<float>& f, const ivec3& dims, float iso) {
    static const std::array<uint8_t, 256> ambiguous = [] {
        std::array<uint8_t, 256> t{};
        for (unsigned m = 0; m < 256; ++m) t[m] = uint8_t(cubePieces(m) > 1 || cubePieces(~m & 255u) > 1);
        return t;
    }();
    const size_t sy = size_t(dims.x), sz = size_t(dims.x) * size_t(dims.y);
    auto corner = [&](size_t base, int k) { return base + size_t(k & 1) + size_t(k >> 1 & 1) * sy + size_t(k >> 2 & 1) * sz; };
    const float inside = std::nextafter(iso, std::numeric_limits<float>::max());
    size_t fixed = 0;
    for (int pass = 0; pass < 16; ++pass) {
        size_t changed = 0;
        for (int z = 0; z + 1 < dims.z; ++z)
            for (int y = 0; y + 1 < dims.y; ++y)
                for (int x = 0; x + 1 < dims.x; ++x) {
                    const size_t base = size_t(z) * sz + size_t(y) * sy + size_t(x);
                    for (;;) {
                        unsigned mask = 0;
                        for (int k = 0; k < 8; ++k) mask |= unsigned(f[corner(base, k)] > iso) << k;
                        if (mask == 0 || mask == 255 || !ambiguous[mask]) break;
                        int pick = -1;
                        for (int k = 0; k < 8; ++k)
                            if (!(mask >> k & 1u) && (pick < 0 || f[corner(base, k)] > f[corner(base, pick)])) pick = k;
                        f[corner(base, pick)] = inside;
                        ++changed;
                    }
                }
        fixed += changed;
        if (changed == 0) break;
    }
    return fixed;
}

// Drops the 6-connected pieces of the set cells smaller than `minFraction` of the largest one (1: keeps only the
// largest). Returns how many pieces there were; `removed` gets the number of cells dropped. `scratch` is a
// same-sized work array.
int dropSmallPieces(std::vector<uint8_t>& cells, const ivec3& dims, std::vector<uint8_t>& scratch, float minFraction,
                    size_t& removed) {
    const size_t n = cells.size();
    scratch.assign(n, 0);
    std::vector<uint32_t> stack;
    std::vector<std::pair<uint32_t, size_t>> pieces; // seed, size
    const int64_t sy = dims.x, sz = int64_t(dims.x) * dims.y;
    // Floods the piece holding `seed` over cells equal to `match`, setting each to `set` in `mark` (scratch to count
    // a piece, cells to clear one); returns its size.
    auto grow = [&](uint32_t seed, std::vector<uint8_t>& mark, uint8_t match, uint8_t set) {
        size_t count = 0;
        mark[seed] = set;
        stack.assign(1, seed);
        while (!stack.empty()) {
            const uint32_t i = stack.back();
            stack.pop_back();
            ++count;
            const int x = int(i % uint32_t(dims.x));
            const int y = int((i / uint32_t(dims.x)) % uint32_t(dims.y));
            const int z = int(i / uint32_t(sz));
            const int64_t offsets[6] = {-1, 1, -sy, sy, -sz, sz};
            const bool valid[6] = {x > 0, x + 1 < dims.x, y > 0, y + 1 < dims.y, z > 0, z + 1 < dims.z};
            for (int k = 0; k < 6; ++k) {
                if (!valid[k]) continue;
                const uint32_t j = uint32_t(int64_t(i) + offsets[k]);
                if (cells[j] && mark[j] == match) {
                    mark[j] = set;
                    stack.push_back(j);
                }
            }
        }
        return count;
    };
    for (size_t seed = 0; seed < n; ++seed)
        if (cells[seed] && !scratch[seed]) pieces.push_back({uint32_t(seed), grow(uint32_t(seed), scratch, 0, 1)});
    size_t largest = 0;
    for (const auto& p : pieces) largest = std::max(largest, p.second);
    removed = 0;
    bool keptLargest = false;
    for (const auto& [seed, count] : pieces) {
        // minFraction 1 keeps exactly one piece (the first of equally large ones).
        bool keep = double(count) >= double(minFraction) * double(largest);
        if (minFraction >= 1.0f) keep = count == largest && !keptLargest;
        keptLargest |= keep && count == largest;
        if (!keep) removed += grow(seed, cells, 1, 0);
    }
    return int(pieces.size());
}

struct SplatKernel {
    vec3 p{0.0f};
    float alpha = 0.0f;
    float inv[6] = {}; // inverse covariance xx, xy, xz, yy, yz, zz
    ivec3 lo{0}, hi{-1};
};

// Per-axis outlier-trimmed bounds of points.
AABB trimmedBounds(const std::vector<vec3>& pts, float keepFraction) {
    AABB b;
    if (pts.empty()) return b;
    if (keepFraction >= 1.0f) {
        for (const vec3& p : pts) b.expand(p);
        return b;
    }
    std::vector<float> axis(pts.size());
    const float tail = std::clamp((1.0f - keepFraction) * 0.5f, 0.0f, 0.49f);
    const size_t lo = size_t(tail * float(pts.size() - 1)), hi = pts.size() - 1 - lo;
    for (int a = 0; a < 3; ++a) {
        for (size_t i = 0; i < pts.size(); ++i) axis[i] = pts[i][a];
        std::nth_element(axis.begin(), axis.begin() + std::ptrdiff_t(lo), axis.end());
        b.min[a] = axis[lo];
        std::nth_element(axis.begin(), axis.begin() + std::ptrdiff_t(hi), axis.end());
        b.max[a] = axis[hi];
    }
    return b;
}

glm::quat rotationFromZ(const vec3& n) {
    const vec3 z(0.0f, 0.0f, 1.0f);
    const float d = glm::dot(z, n);
    if (d < -0.999999f) return glm::quat(0.0f, 1.0f, 0.0f, 0.0f);
    const vec3 c = glm::cross(z, n);
    return glm::normalize(glm::quat(1.0f + d, c.x, c.y, c.z));
}

bool inside(const AABB& b, const vec3& p) { return glm::all(glm::greaterThanEqual(p, b.min)) && glm::all(glm::lessThanEqual(p, b.max)); }

} // namespace

// ---------------------------------------------------------------------------------------------------------
// 1. Surface
// ---------------------------------------------------------------------------------------------------------

bool splatSurface(const SplatCloud& cloud, const SplatSurfaceOptions& o, Mesh& out, std::string& error,
                  const SculptableProgress& progress, SplatSurfaceStats* statsOut) {
    SplatSurfaceStats st;
    st.splats = cloud.size();
    auto fail = [&](const std::string& msg) {
        error = msg;
        if (statsOut) *statsOut = st;
        return false;
    };
    if (cloud.empty()) return fail("There are no splats to convert");
    if (const std::string bad = cloud.validate(); !bad.empty()) return fail(bad);
    const auto t0 = Clock::now();

    // Candidates: opaque enough and inside the crop box.
    std::vector<uint32_t> candidates;
    std::vector<vec3> candidatePos;
    candidates.reserve(cloud.size());
    for (size_t i = 0; i < cloud.size(); ++i) {
        if (!(cloud.opacities[i] >= o.minOpacity)) {
            ++st.faint;
            continue;
        }
        const vec3 p = cloud.worldPosition(i);
        if (o.crop && !inside(o.cropBox, p)) {
            ++st.cropped;
            continue;
        }
        candidates.push_back(uint32_t(i));
        candidatePos.push_back(p);
    }
    if (candidates.empty())
        return fail(st.cropped ? "No splats inside the crop box" : "No splats are opaque enough - lower the minimum opacity");

    // The percentile box only finds the far outliers: everything within a quarter of its size around it counts
    // (a nose tip or a raised hand is an extreme along its axis, not an outlier).
    AABB box = trimmedBounds(candidatePos, std::clamp(o.keepFraction, 0.5f, 1.0f));
    {
        const vec3 reach = 0.25f * box.extent();
        AABB wide = box;
        wide.min -= reach;
        wide.max += reach;
        AABB fit;
        for (const vec3& p : candidatePos)
            if (inside(wide, p)) fit.expand(p);
        if (fit.valid()) box = fit;
    }
    const float boxSize = std::max({box.extent().x, box.extent().y, box.extent().z});
    if (!(boxSize > 0.0f)) return fail("The splats have no extent");
    box.min -= box.extent() * 0.02f + vec3(1e-6f);
    box.max += box.extent() * 0.02f + vec3(1e-6f);

    VoxelGrid grid;
    const int closeRadius = std::clamp(o.closeRadius, 0, 8);
    if (!makeVoxelGrid(box, std::clamp(o.resolution, 32, 768), closeRadius + 4, grid, 600'000'000))
        return fail("Detail too high for this capture");
    st.dims = grid.dims;
    st.cell = grid.cell;
    if (!tick(progress, 0.02f)) return fail("Cancelled");

    // Each splat's Gaussian (variance widened by half a voxel so thin splats still register), its reach
    // clamped to 6 voxels.
    const float s = cloud.transformScale();
    const glm::quat r = cloud.transformRotation();
    const float widen = 0.25f * grid.cell * grid.cell;
    const float maxReach = 6.0f * grid.cell;
    std::vector<SplatKernel> kernels(candidates.size());
    std::vector<uint8_t> keep(candidates.size(), 0);
    parallelFor(candidates.size(), [&](size_t b, size_t e) {
        for (size_t k = b; k < e; ++k) {
            const size_t i = candidates[k];
            const vec3 p = candidatePos[k];
            if (!inside(box, p)) continue;
            const vec3 sigma = cloud.scales[i] * s;
            if (std::max({sigma.x, sigma.y, sigma.z}) > 0.25f * boxSize) continue; // background blobs
            const glm::mat3 R = glm::mat3_cast(glm::normalize(r * cloud.rotations[i]));
            glm::mat3 cov = R * glm::mat3(vec3(sigma.x * sigma.x, 0, 0), vec3(0, sigma.y * sigma.y, 0), vec3(0, 0, sigma.z * sigma.z)) *
                            glm::transpose(R);
            cov[0][0] += widen;
            cov[1][1] += widen;
            cov[2][2] += widen;
            const glm::mat3 inv = glm::inverse(cov);
            SplatKernel& sk = kernels[k];
            sk.p = p;
            sk.alpha = cloud.opacities[i];
            sk.inv[0] = inv[0][0];
            sk.inv[1] = inv[0][1];
            sk.inv[2] = inv[0][2];
            sk.inv[3] = inv[1][1];
            sk.inv[4] = inv[1][2];
            sk.inv[5] = inv[2][2];
            const vec3 reach = glm::min(3.0f * glm::sqrt(vec3(cov[0][0], cov[1][1], cov[2][2])), vec3(maxReach));
            sk.lo = glm::max(ivec3(glm::floor((p - reach - grid.origin) / grid.cell)), ivec3(0));
            sk.hi = glm::min(ivec3(glm::ceil((p + reach - grid.origin) / grid.cell)), grid.dims - 1);
            keep[k] = 1;
        }
    }, 4096);
    for (size_t k = 0; k < candidates.size(); ++k) {
        if (keep[k]) continue;
        if (!inside(box, candidatePos[k])) ++st.outliers;
        else ++st.huge;
    }
    std::vector<vec3>().swap(candidatePos);

    // Slabs of 4 z-layers, each with the splats that reach it in index order: threads take whole slabs, so
    // every voxel sums its splats in the same order whatever the thread count.
    constexpr int kSlab = 4;
    const int slabs = (grid.dims.z + kSlab - 1) / kSlab;
    std::vector<uint32_t> slabStart(size_t(slabs) + 1, 0);
    for (size_t k = 0; k < kernels.size(); ++k)
        if (keep[k])
            for (int sl = kernels[k].lo.z / kSlab; sl <= kernels[k].hi.z / kSlab; ++sl) ++slabStart[size_t(sl) + 1];
    for (int sl = 0; sl < slabs; ++sl) slabStart[size_t(sl) + 1] += slabStart[size_t(sl)];
    std::vector<uint32_t> slabSplats(slabStart.back()), fill(slabStart.begin(), slabStart.end() - 1);
    for (size_t k = 0; k < kernels.size(); ++k) {
        if (!keep[k]) continue;
        ++st.used;
        for (int sl = kernels[k].lo.z / kSlab; sl <= kernels[k].hi.z / kSlab; ++sl) slabSplats[fill[size_t(sl)]++] = uint32_t(k);
    }
    if (st.used == 0) return fail("No usable splats (all of them are huge background blobs or outliers)");

    std::atomic<int> slabsDone{0};
    std::atomic<bool> cancelled{false};
    parallelFor(size_t(slabs), [&](size_t b, size_t e) {
        for (size_t sl = b; sl < e; ++sl) {
            if (cancelled.load(std::memory_order_relaxed)) return;
            const int z0 = int(sl) * kSlab, z1 = std::min(z0 + kSlab, grid.dims.z) - 1;
            for (uint32_t j = slabStart[sl]; j < slabStart[sl + 1]; ++j) {
                const SplatKernel& sk = kernels[slabSplats[j]];
                for (int z = std::max(sk.lo.z, z0); z <= std::min(sk.hi.z, z1); ++z)
                    for (int y = sk.lo.y; y <= sk.hi.y; ++y)
                        for (int x = sk.lo.x; x <= sk.hi.x; ++x) {
                            const vec3 d = grid.position(x, y, z) - sk.p;
                            const float q = sk.inv[0] * d.x * d.x + sk.inv[3] * d.y * d.y + sk.inv[5] * d.z * d.z +
                                            2.0f * (sk.inv[1] * d.x * d.y + sk.inv[2] * d.x * d.z + sk.inv[4] * d.y * d.z);
                            if (q < 9.0f) grid.values[grid.index(x, y, z)] += sk.alpha * std::exp(-0.5f * q);
                        }
            }
            const int done = slabsDone.fetch_add(1) + 1;
            if (progress && (done & 7) == 0 && !progress(0.02f + 0.55f * float(done) / float(slabs))) cancelled = true;
        }
    }, 1);
    if (cancelled) return fail("Cancelled");
    std::vector<SplatKernel>().swap(kernels);
    std::vector<uint32_t>().swap(slabSplats);
    st.densitySeconds = secondsSince(t0);
    const auto t1 = Clock::now();

    // Shell -> closed -> holes filled -> outside flooded -> opened again -> largest piece.
    const ivec3 dims = grid.dims;
    const size_t n = grid.size();
    std::vector<uint8_t> wall(n);
    const float threshold = std::max(o.threshold, 1e-4f);
    for (size_t i = 0; i < n; ++i) wall[i] = grid.values[i] > threshold ? 1 : 0;
    std::vector<float>().swap(grid.values);
    morph(wall, dims, closeRadius, true);
    if (!tick(progress, 0.62f)) return fail("Cancelled");

    // Floaters go first: small shell pieces apart from the subject would box in the empty space around it. (Not
    // just the largest piece: splats inside the subject can make a bigger shell piece than its thin surface.)
    if (!o.keepAllParts) {
        std::vector<uint8_t> scratch;
        size_t removed = 0;
        st.pieces = dropSmallPieces(wall, dims, scratch, 0.02f, removed);
        st.floaterVoxels = removed;
    }
    std::vector<uint8_t> block = wall;
    if (o.fillHoles) {
        // Count the axis directions in which each empty cell sees the shell; 5 or 6 of 6 = enclosed.
        std::vector<uint8_t> seen(n, 0);
        for (int axis = 0; axis < 3; ++axis) {
            const GridLines lines{dims, axis};
            const size_t stride = lines.stride();
            const int len = lines.length();
            parallelFor(lines.count(), [&](size_t b, size_t e) {
                for (size_t l = b; l < e; ++l) {
                    const size_t base = lines.base(l);
                    bool hit = false;
                    for (int i = 0; i < len; ++i) {
                        const size_t c = base + size_t(i) * stride;
                        if (wall[c]) hit = true;
                        else if (hit) ++seen[c];
                    }
                    hit = false;
                    for (int i = len - 1; i >= 0; --i) {
                        const size_t c = base + size_t(i) * stride;
                        if (wall[c]) hit = true;
                        else if (hit) ++seen[c];
                    }
                }
            }, 256);
        }
        for (size_t i = 0; i < n; ++i)
            if (seen[i] >= 5) block[i] = 1;
    }
    std::vector<uint8_t>().swap(wall);
    if (!tick(progress, 0.66f)) return fail("Cancelled");

    std::vector<uint8_t> outside(n, 0), open(n);
    for (size_t i = 0; i < n; ++i) open[i] = block[i] ? 0 : 1;
    std::vector<uint8_t>().swap(block);
    std::vector<uint32_t> stack;
    for (int z = 0; z < dims.z; ++z)
        for (int y = 0; y < dims.y; ++y)
            for (int x = 0; x < dims.x; ++x) {
                const bool border = x == 0 || y == 0 || z == 0 || x == dims.x - 1 || y == dims.y - 1 || z == dims.z - 1;
                if (!border) continue;
                const size_t i = grid.index(x, y, z);
                if (open[i] && !outside[i]) {
                    outside[i] = 1;
                    stack.push_back(uint32_t(i));
                }
            }
    flood(open, dims, stack, outside);
    std::vector<uint8_t>& solid = open; // reuse
    for (size_t i = 0; i < n; ++i) solid[i] = outside[i] ? 0 : 1;
    morph(solid, dims, closeRadius, false);
    if (!tick(progress, 0.72f)) return fail("Cancelled");

    // Pieces: keep the largest (unless keepAllParts).
    std::vector<uint8_t>& visited = outside; // reuse as scratch
    if (!o.keepAllParts) {
        size_t removed = 0;
        st.pieces = std::max(st.pieces, dropSmallPieces(solid, dims, visited, 1.0f, removed));
        st.floaterVoxels += removed;
    }
    st.solidVoxels = size_t(std::count(solid.begin(), solid.end(), uint8_t(1)));
    if (st.solidVoxels == 0) return fail("No closed surface was found - try more Detail, a lower opacity or a bigger crop box");
    st.solidSeconds = secondsSince(t1);
    if (!tick(progress, 0.78f)) return fail("Cancelled");

    const auto t2 = Clock::now();
    {
        std::vector<float> field(solid.begin(), solid.end());
        std::vector<uint8_t>().swap(solid);
        std::vector<uint8_t>().swap(visited);
        blurField(field, dims);
        fixAmbiguousCubes(field, dims, 0.5f);
        out = meshFromField(grid, field, 0.5f, std::clamp(o.smoothIterations, 0, 50));
    }
    st.meshSeconds = secondsSince(t2);
    if (out.triangleCount() == 0) return fail("No closed surface was found - try a larger close radius");
    if (statsOut) *statsOut = st;
    return tick(progress, 1.0f) || fail("Cancelled");
}

// ---------------------------------------------------------------------------------------------------------
// 3. Colour
// ---------------------------------------------------------------------------------------------------------

SplatColorSampler::SplatColorSampler(const SplatCloud& cloud, float radius, float minOpacity, const AABB* crop) {
    radius_ = std::max(radius, 1e-6f);
    std::vector<vec3> pos, col;
    std::vector<float> alpha;
    pos.reserve(cloud.size());
    AABB b;
    for (size_t i = 0; i < cloud.size(); ++i) {
        if (!(cloud.opacities[i] >= minOpacity)) continue;
        const vec3 p = cloud.worldPosition(i);
        if (crop && !inside(*crop, p)) continue;
        pos.push_back(p);
        col.push_back(srgbToLinear(glm::clamp(cloud.color(i), vec3(0.0f), vec3(1.0f))));
        alpha.push_back(cloud.opacities[i]);
        b.expand(p);
    }
    if (pos.empty()) return;
    // At least about two splats across: with fine Detail the voxels get smaller than the splats, and a radius
    // of a voxel or two would average too few of them (mottled colour).
    {
        std::vector<float> sizes;
        const float s = cloud.transformScale();
        const size_t step = std::max<size_t>(1, cloud.size() / 20000);
        for (size_t i = 0; i < cloud.size(); i += step)
            if (cloud.opacities[i] >= minOpacity) sizes.push_back(std::max({cloud.scales[i].x, cloud.scales[i].y, cloud.scales[i].z}) * s);
        if (!sizes.empty()) {
            std::nth_element(sizes.begin(), sizes.begin() + std::ptrdiff_t(sizes.size() / 2), sizes.end());
            radius_ = std::max(radius_, 2.5f * sizes[sizes.size() / 2]);
        }
    }
    cell_ = radius_;
    const vec3 ext = b.extent() + vec3(1e-6f);
    // At most ~32M cells.
    while (double(std::ceil(ext.x / cell_) + 1) * double(std::ceil(ext.y / cell_) + 1) * double(std::ceil(ext.z / cell_) + 1) > 32e6)
        cell_ *= 1.5f;
    origin_ = b.min;
    dims_ = ivec3(glm::floor(ext / cell_)) + 1;
    const size_t cells = size_t(dims_.x) * size_t(dims_.y) * size_t(dims_.z);
    std::vector<uint32_t> cellOf(pos.size());
    cellStart_.assign(cells + 1, 0);
    for (size_t i = 0; i < pos.size(); ++i) {
        const ivec3 c = glm::clamp(ivec3((pos[i] - origin_) / cell_), ivec3(0), dims_ - 1);
        cellOf[i] = uint32_t((size_t(c.z) * size_t(dims_.y) + size_t(c.y)) * size_t(dims_.x) + size_t(c.x));
        ++cellStart_[size_t(cellOf[i]) + 1];
    }
    for (size_t c = 0; c < cells; ++c) cellStart_[c + 1] += cellStart_[c];
    std::vector<uint32_t> fill(cellStart_.begin(), cellStart_.end() - 1);
    pos_.resize(pos.size());
    color_.resize(pos.size());
    alpha_.resize(pos.size());
    for (size_t i = 0; i < pos.size(); ++i) {
        const uint32_t j = fill[cellOf[i]]++;
        pos_[j] = pos[i];
        color_[j] = col[i];
        alpha_[j] = alpha[i];
    }
}

glm::vec3 SplatColorSampler::gather(const vec3& p, const vec3& n, float radius, float& weight) const {
    const ivec3 lo = glm::max(ivec3(glm::floor((p - radius - origin_) / cell_)), ivec3(0));
    const ivec3 hi = glm::min(ivec3(glm::floor((p + radius - origin_) / cell_)), dims_ - 1);
    const float r2 = radius * radius;
    const float sigma2 = 0.25f * r2;      // Gaussian falloff: sigma = radius / 2
    const float behindStart = 0.15f * radius, behindScale = 0.3f * radius;
    vec3 sum(0.0f);
    weight = 0.0f;
    for (int z = lo.z; z <= hi.z; ++z)
        for (int y = lo.y; y <= hi.y; ++y)
            for (int x = lo.x; x <= hi.x; ++x) {
                const size_t c = (size_t(z) * size_t(dims_.y) + size_t(y)) * size_t(dims_.x) + size_t(x);
                for (uint32_t j = cellStart_[c]; j < cellStart_[c + 1]; ++j) {
                    const vec3 d = pos_[j] - p;
                    const float d2 = glm::dot(d, d);
                    if (d2 > r2) continue;
                    float w = alpha_[j] * std::exp(-0.5f * d2 / sigma2);
                    // Splats behind the surface (inside the subject) are mostly hidden in the capture: fade them.
                    const float behind = std::max(0.0f, -glm::dot(d, n) - behindStart);
                    if (behind > 0.0f) w *= std::exp(-(behind * behind) / (behindScale * behindScale));
                    sum += w * color_[j];
                    weight += w;
                }
            }
    return sum;
}

glm::vec3 SplatColorSampler::sample(const vec3& p, const vec3& n, bool* found) const {
    if (!pos_.empty()) {
        float radius = radius_;
        for (int attempt = 0; attempt < 3; ++attempt, radius *= 2.0f) {
            float w = 0.0f;
            const vec3 c = gather(p, n, radius, w);
            if (w > 1e-6f) {
                if (found) *found = true;
                return c / w;
            }
        }
    }
    if (found) *found = false;
    return vec3(0.5f);
}

size_t colorFromSplats(Mesh& mesh, const SplatColorSampler& sampler) {
    const size_t nv = mesh.vertexCount();
    if (mesh.normals.size() != nv) mesh.recomputeAllNormals();
    std::vector<glm::vec3>& colors = mesh.ensureColors();
    std::vector<uint8_t> found(nv, 0);
    parallelFor(nv, [&](size_t b, size_t e) {
        for (size_t v = b; v < e; ++v) {
            bool ok = false;
            colors[v] = sampler.sample(mesh.positions[v], mesh.normals[v], &ok);
            found[v] = ok ? 1 : 0;
        }
    }, 2048);
    const size_t missed = size_t(std::count(found.begin(), found.end(), uint8_t(0)));
    // Vertices with no splat near (a hole in the capture) take their coloured neighbours' mean, spreading inwards.
    for (int pass = 0; pass < 64 && missed > 0; ++pass) {
        std::vector<glm::vec3> sum(nv, glm::vec3(0.0f));
        std::vector<uint32_t> count(nv, 0);
        for (const glm::uvec3& t : mesh.triangles)
            for (int k = 0; k < 3; ++k) {
                const uint32_t a = t[k];
                if (found[a]) continue;
                for (int j = 1; j < 3; ++j) {
                    const uint32_t b = t[(k + j) % 3];
                    if (found[b] == 1) {
                        sum[a] += colors[b];
                        ++count[a];
                    }
                }
            }
        bool any = false;
        for (size_t v = 0; v < nv; ++v)
            if (!found[v] && count[v]) {
                colors[v] = sum[v] / float(count[v]);
                found[v] = 2; // coloured this pass: a source from the next pass on
                any = true;
            }
        for (uint8_t& f : found) f = f ? 1 : 0;
        if (!any) break;
    }
    mesh.markAllDirty();
    return missed;
}

void textureFromSplats(Mesh& mesh, const SplatColorSampler& sampler, int size) {
    if (!mesh.hasUvs()) return;
    if (mesh.normals.size() != mesh.vertexCount()) mesh.recomputeAllNormals();
    mesh.texture = makeTexture(size, size);
    mesh.paintToTexture = true;
    PaintTexture& tex = *mesh.texture;
    struct Texel {
        uint32_t tri;
        float b1, b2;
        uint32_t index;
    };
    std::vector<Texel> texels;
    bakeTexels(mesh, tex, [&](uint32_t t, float, float b1, float b2, int x, int y) {
        texels.push_back({t, b1, b2, uint32_t(size_t(y) * size_t(tex.width) + size_t(x))});
    }, false);
    parallelFor(texels.size(), [&](size_t b, size_t e) {
        for (size_t k = b; k < e; ++k) {
            const Texel& tx = texels[k];
            const glm::uvec3& t = mesh.triangles[tx.tri];
            const float b0 = 1.0f - tx.b1 - tx.b2;
            const vec3 p = mesh.positions[t.x] * b0 + mesh.positions[t.y] * tx.b1 + mesh.positions[t.z] * tx.b2;
            const vec3 nn = mesh.normals[t.x] * b0 + mesh.normals[t.y] * tx.b1 + mesh.normals[t.z] * tx.b2;
            const float len = glm::length(nn);
            const vec3 c = glm::clamp(linearToSrgb(sampler.sample(p, len > 0.0f ? nn / len : vec3(0, 0, 1))), vec3(0.0f), vec3(1.0f));
            uint8_t* px = &tex.pixels[size_t(tx.index) * 4];
            px[0] = uint8_t(std::lround(c.x * 255.0f));
            px[1] = uint8_t(std::lround(c.y * 255.0f));
            px[2] = uint8_t(std::lround(c.z * 255.0f));
            px[3] = 255;
        }
    }, 8192);
    tex.markAllDirty();
    mesh.facesDirty = true;
}

// ---------------------------------------------------------------------------------------------------------
// 2. Face-aware topology
// ---------------------------------------------------------------------------------------------------------

bool FaceFrame::orthonormalize() {
    if (!(glm::length(front) > 1e-6f) || !(glm::length(up) > 1e-6f)) return false;
    front = glm::normalize(front);
    vec3 u = up - glm::dot(up, front) * front;
    if (glm::length(u) < 1e-3f) return false;
    up = glm::normalize(u);
    return true;
}

const char* FaceLandmarks::name(int i) {
    static const char* names[kCount] = {"eye_outer_l", "eye_inner_l", "eye_inner_r", "eye_outer_r",
                                        "nose_tip",    "mouth_l",     "mouth_r",     "chin"};
    return i >= 0 && i < kCount ? names[i] : "";
}

glm::vec3& FaceLandmarks::point(int i) {
    switch (i) {
    case 0: return eyeOuter[0];
    case 1: return eyeInner[0];
    case 2: return eyeInner[1];
    case 3: return eyeOuter[1];
    case 4: return noseTip;
    case 5: return mouthCorner[0];
    case 6: return mouthCorner[1];
    default: return chin;
    }
}

const glm::vec3& FaceLandmarks::point(int i) const { return const_cast<FaceLandmarks*>(this)->point(i); }

namespace {

// The mesh seen from the front: per pixel the surface hit (orthographic rays along -front).
struct FaceImage {
    int w = 0, h = 0;
    float px = 1.0f;          // pixel size (world)
    float u0 = 0, v0 = 0;     // face coordinates of pixel (0, 0)'s centre
    float front = 0;          // where the rays start (along front)
    std::vector<float> depth; // along front; NaN = no hit
    std::vector<float> lum;   // luminance of the polypaint (1 without)
    std::vector<vec3> hit;
    bool valid(int x, int y) const { return x >= 0 && y >= 0 && x < w && y < h && !std::isnan(depth[size_t(y * w + x)]); }
    float d(int x, int y) const { return depth[size_t(y * w + x)]; }
};

vec2 faceUv(const vec3& p, const FaceFrame& f) { return {glm::dot(p, f.right()), glm::dot(p, f.up)}; }

// First hit along -front through face coordinates `uv`, from in front of everything.
bool castFront(const Mesh& mesh, const BVH& bvh, const FaceFrame& f, const vec2& uv, float startFront, vec3& out) {
    RayHit hit;
    const vec3 origin = f.right() * uv.x + f.up * uv.y + f.front * startFront;
    if (!bvh.raycast(mesh, origin, -f.front, hit)) return false;
    out = hit.position;
    return true;
}

float frontmost(const Mesh& mesh, const FaceFrame& f) {
    float m = -std::numeric_limits<float>::max(), lo = std::numeric_limits<float>::max();
    for (const vec3& p : mesh.positions) {
        m = std::max(m, glm::dot(p, f.front));
        lo = std::min(lo, glm::dot(p, f.front));
    }
    return m + 0.1f * std::max(m - lo, 1e-3f);
}

FaceImage renderFace(const Mesh& mesh, const BVH& bvh, const FaceFrame& f, int resolution) {
    FaceImage img;
    vec2 lo(std::numeric_limits<float>::max()), hi(std::numeric_limits<float>::lowest());
    for (const vec3& p : mesh.positions) {
        const vec2 uv = faceUv(p, f);
        lo = glm::min(lo, uv);
        hi = glm::max(hi, uv);
    }
    const vec2 ext = hi - lo;
    img.px = std::max(ext.x, ext.y) / float(resolution);
    if (!(img.px > 0.0f)) return img;
    img.w = int(std::ceil(ext.x / img.px)) + 1;
    img.h = int(std::ceil(ext.y / img.px)) + 1;
    img.u0 = lo.x;
    img.v0 = lo.y;
    img.front = frontmost(mesh, f);
    const size_t n = size_t(img.w) * size_t(img.h);
    img.depth.assign(n, std::nanf(""));
    img.lum.assign(n, 1.0f);
    img.hit.assign(n, vec3(0.0f));
    for (int y = 0; y < img.h; ++y)
        for (int x = 0; x < img.w; ++x) {
            RayHit hit;
            const vec3 origin = f.right() * (img.u0 + float(x) * img.px) + f.up * (img.v0 + float(y) * img.px) + f.front * img.front;
            if (!bvh.raycast(mesh, origin, -f.front, hit)) continue;
            const size_t i = size_t(y * img.w + x);
            img.depth[i] = glm::dot(hit.position, f.front);
            img.hit[i] = hit.position;
            if (mesh.hasColors()) {
                const glm::uvec3& t = mesh.triangles[hit.triangle];
                // Nearest-corner colour is enough at this resolution.
                const vec3& P0 = mesh.positions[t.x];
                const vec3& P1 = mesh.positions[t.y];
                const vec3& P2 = mesh.positions[t.z];
                const float d0 = glm::distance(P0, hit.position), d1 = glm::distance(P1, hit.position), d2 = glm::distance(P2, hit.position);
                const uint32_t v = d0 <= d1 && d0 <= d2 ? t.x : d1 <= d2 ? t.y : t.z;
                const vec3 c = mesh.colors[v];
                img.lum[i] = 0.2126f * c.x + 0.7152f * c.y + 0.0722f * c.z;
            }
        }
    return img;
}

// Normalised box blur of `src` over valid pixels (radius r, three passes ~ Gaussian).
std::vector<float> blurValid(const FaceImage& img, const std::vector<float>& src, int r) {
    const int w = img.w, h = img.h;
    std::vector<float> val(src.size()), wt(src.size());
    for (size_t i = 0; i < src.size(); ++i) {
        const bool ok = !std::isnan(img.depth[i]);
        val[i] = ok ? src[i] : 0.0f;
        wt[i] = ok ? 1.0f : 0.0f;
    }
    std::vector<float> tv(src.size()), tw(src.size());
    for (int pass = 0; pass < 3; ++pass)
        for (int axis = 0; axis < 2; ++axis) {
            const int len = axis == 0 ? w : h, lines = axis == 0 ? h : w;
            for (int l = 0; l < lines; ++l) {
                auto at = [&](int i) { return axis == 0 ? size_t(l * w + i) : size_t(i * w + l); };
                float sv = 0, sw = 0;
                for (int i = 0; i < std::min(r, len); ++i) {
                    sv += val[at(i)];
                    sw += wt[at(i)];
                }
                for (int i = 0; i < len; ++i) {
                    if (i + r < len) {
                        sv += val[at(i + r)];
                        sw += wt[at(i + r)];
                    }
                    if (i - r - 1 >= 0) {
                        sv -= val[at(i - r - 1)];
                        sw -= wt[at(i - r - 1)];
                    }
                    tv[at(i)] = sv;
                    tw[at(i)] = sw;
                }
            }
            val.swap(tv);
            wt.swap(tw);
        }
    std::vector<float> out(src.size(), std::nanf(""));
    for (size_t i = 0; i < src.size(); ++i)
        if (wt[i] > 1e-6f && !std::isnan(img.depth[i])) out[i] = val[i] / wt[i];
    return out;
}

} // namespace

bool detectFaceLandmarks(const Mesh& mesh, const FaceFrame& frameIn, FaceLandmarks& out, std::string& why) {
    out = FaceLandmarks();
    FaceFrame f = frameIn;
    if (!f.orthonormalize()) {
        why = "The face direction and up direction are parallel";
        return false;
    }
    if (mesh.triangleCount() == 0) {
        why = "The mesh is empty";
        return false;
    }
    BVH bvh;
    bvh.build(mesh);
    const FaceImage img = renderFace(mesh, bvh, f, 160);
    const int W = img.w, H = img.h;
    if (W < 16 || H < 16) {
        why = "The model is too thin to be a head";
        return false;
    }
    auto idx = [&](int x, int y) { return size_t(y * W + x); };

    // Protrusion: depth above its blurred surroundings (the nose, brow, lips and chin stand out; eye sockets and
    // the mouth line dip). Darkness from the polypaint helps with eyes and the mouth.
    const int R = std::max(3, int(std::lround(0.06f * float(std::max(W, H)))));
    const std::vector<float> smooth = blurValid(img, img.depth, R);
    std::vector<float> P(img.depth.size(), std::nanf(""));
    for (size_t i = 0; i < P.size(); ++i)
        if (!std::isnan(img.depth[i])) P[i] = img.depth[i] - smooth[i];
    const std::vector<float> Pb = blurValid(img, P, 1);
    const std::vector<float> Lb = blurValid(img, img.lum, 1);
    // Within R pixels of the silhouette the depth falls away and the relief means nothing: leave that rim out.
    std::vector<uint8_t> interior(P.size());
    for (size_t i = 0; i < P.size(); ++i) interior[i] = std::isnan(img.depth[i]) ? 0 : 1;
    for (int pass = 0; pass < R; ++pass) {
        std::vector<uint8_t> next = interior;
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x)
                if (interior[idx(x, y)] && (x == 0 || y == 0 || x == W - 1 || y == H - 1 || !interior[idx(x - 1, y)] ||
                                            !interior[idx(x + 1, y)] || !interior[idx(x, y - 1)] || !interior[idx(x, y + 1)]))
                    next[idx(x, y)] = 0;
        interior.swap(next);
    }
    auto pAt = [&](int x, int y) {
        return x >= 0 && y >= 0 && x < W && y < H && interior[idx(x, y)] && !std::isnan(Pb[idx(x, y)]) ? Pb[idx(x, y)] : std::nanf("");
    };
    auto lAt = [&](int x, int y) { return img.valid(x, y) ? Lb[idx(x, y)] : std::nanf(""); };

    // The face's middle: the column about which the depth is most mirror-symmetric.
    int mid = W / 2;
    {
        float best = std::numeric_limits<float>::max();
        const int span = std::max(4, W / 4);
        for (int c = W / 4; c <= 3 * W / 4; ++c) {
            double sum = 0;
            int count = 0;
            for (int y = 0; y < H; y += 2)
                for (int k = 1; k <= span; k += 2) {
                    if (!img.valid(c - k, y) || !img.valid(c + k, y)) continue;
                    sum += std::abs(img.d(c - k, y) - img.d(c + k, y));
                    ++count;
                }
            if (count < W) continue;
            const float score = float(sum / count);
            if (score < best) {
                best = score;
                mid = c;
            }
        }
    }

    // Head width (pixels) on a row: the run of valid pixels through the middle.
    auto rowWidth = [&](int y) {
        if (!img.valid(mid, y)) return 0;
        int a = mid, b = mid;
        while (img.valid(a - 1, y)) --a;
        while (img.valid(b + 1, y)) ++b;
        return b - a + 1;
    };
    auto midP = [&](int y) {
        float s = 0;
        int c = 0;
        for (int dx = -1; dx <= 1; ++dx)
            if (float p = pAt(mid + dx, y); !std::isnan(p)) {
                s += p;
                ++c;
            }
        return c ? s / float(c) : std::nanf("");
    };

    // Nose candidates: the strongest protrusions on the middle line.
    std::vector<std::pair<float, int>> noses;
    for (int y = 2; y < H - 2; ++y) {
        const float p = midP(y);
        if (std::isnan(p)) continue;
        bool peak = true;
        for (int dy = -2; dy <= 2 && peak; ++dy)
            if (dy != 0 && !std::isnan(midP(y + dy)) && midP(y + dy) > p) peak = false;
        if (peak && p > 0.0f) noses.push_back({p, y});
    }
    std::sort(noses.begin(), noses.end(), [](const auto& a, const auto& b) { return a.first > b.first || (a.first == b.first && a.second < b.second); });
    if (noses.size() > 4) noses.resize(4);
    const bool haveColor = mesh.hasColors();

    struct Candidate {
        float score = -std::numeric_limits<float>::max();
        int nose = 0, eyeY = 0, eyeD = 0, mouthY = 0, chinY = 0;
        float conf = 0;
    } best;
    for (const auto& [noseP, ny] : noses) {
        const int wh = rowWidth(ny);
        if (wh < 12) continue;
        const float whf = float(wh);
        // Eyes: a symmetric pair of dips (and darker spots) above the nose.
        Candidate c;
        c.nose = ny;
        float eyeScore = -std::numeric_limits<float>::max();
        const float meanL = lAt(mid, ny);
        for (int y = ny + int(0.10f * whf); y <= std::min(H - 1, ny + int(0.45f * whf)); ++y)
            for (int d = std::max(2, int(0.13f * whf)); d <= int(0.32f * whf); ++d) {
                const float pl = pAt(mid - d, y), pr = pAt(mid + d, y);
                if (std::isnan(pl) || std::isnan(pr)) continue;
                float s = -(pl + pr) - 0.5f * std::abs(pl - pr);
                if (haveColor) s += 0.02f * img.px * whf * (2.0f * meanL - lAt(mid - d, y) - lAt(mid + d, y));
                if (s > eyeScore) {
                    eyeScore = s;
                    c.eyeY = y;
                    c.eyeD = d;
                }
            }
        if (eyeScore == -std::numeric_limits<float>::max()) continue;
        const float ied = 2.0f * float(c.eyeD);
        // Mouth: the dip (and dark line) between the lips, below the nose.
        float mouthScore = -std::numeric_limits<float>::max();
        for (int y = ny - int(0.2f * ied); y >= std::max(0, ny - int(0.8f * ied)); --y) {
            float s = 0;
            int cnt = 0;
            for (int dx = -int(0.25f * ied); dx <= int(0.25f * ied); ++dx)
                if (float p = pAt(mid + dx, y); !std::isnan(p)) {
                    s -= p;
                    if (haveColor) s += 0.01f * img.px * whf * (meanL - lAt(mid + dx, y));
                    ++cnt;
                }
            if (cnt < int(0.25f * ied)) continue;
            s /= float(cnt);
            if (s > mouthScore) {
                mouthScore = s;
                c.mouthY = y;
            }
        }
        if (mouthScore == -std::numeric_limits<float>::max()) continue;
        // Chin: the protrusion below the mouth.
        float chinScore = -std::numeric_limits<float>::max();
        for (int y = c.mouthY - int(0.35f * ied); y >= std::max(0, c.mouthY - int(1.1f * ied)); --y) {
            const float p = midP(y);
            if (std::isnan(p)) continue;
            if (p > chinScore) {
                chinScore = p;
                c.chinY = y;
            }
        }
        if (chinScore == -std::numeric_limits<float>::max()) continue;
        // Confidence: a clear nose, eyes deeper than the nose is high, a clear mouth line, sensible proportions.
        const float relief = noseP / (img.px * whf);                  // nose height over head width
        const float eyeDepth = (noseP - 0.5f * (pAt(mid - c.eyeD, c.eyeY) + pAt(mid + c.eyeD, c.eyeY))) / (img.px * whf);
        const float noseToMouth = float(ny - c.mouthY) / ied, eyesToNose = float(c.eyeY - ny) / ied;
        const float propOk = (noseToMouth > 0.2f && noseToMouth < 0.8f ? 1.0f : 0.3f) *
                             (eyesToNose > 0.3f && eyesToNose < 1.1f ? 1.0f : 0.3f) *
                             (ied / whf > 0.3f && ied / whf < 0.62f ? 1.0f : 0.3f);
        c.conf = std::clamp(relief * 12.0f, 0.0f, 1.0f) * std::clamp(eyeDepth * 10.0f, 0.0f, 1.0f) * propOk;
        c.score = c.conf + 0.1f * relief;
        if (c.score > best.score) best = c;
    }
    if (best.score == -std::numeric_limits<float>::max() || best.conf < 0.15f) {
        why = "No face found from this direction (turn the view to face it, or place the landmarks by hand)";
        return false;
    }

    // Landmarks on the surface: eye corners a quarter of the eye distance either side of each eye's middle,
    // mouth corners 0.4 eye distances from the middle.
    const float ied = 2.0f * float(best.eyeD);
    auto at = [&](float x, int y, vec3& p) {
        const int xi = std::clamp(int(std::lround(x)), 0, W - 1);
        for (int r = 0; r < 4; ++r) // nearest valid pixel on the row
            for (int s : {xi - r, xi + r})
                if (img.valid(s, y)) {
                    p = img.hit[idx(s, y)];
                    return true;
                }
        return false;
    };
    bool ok = true;
    for (int side = 0; side < 2; ++side) {
        const float sgn = side == 0 ? -1.0f : 1.0f;
        const float ex = float(mid) + sgn * float(best.eyeD);
        ok &= at(ex + sgn * 0.25f * ied, best.eyeY, out.eyeOuter[size_t(side)]);
        ok &= at(ex - sgn * 0.25f * ied, best.eyeY, out.eyeInner[size_t(side)]);
        ok &= at(float(mid) + sgn * 0.4f * ied, best.mouthY, out.mouthCorner[size_t(side)]);
    }
    ok &= at(float(mid), best.nose, out.noseTip);
    ok &= at(float(mid), best.chinY, out.chin);
    if (!ok) {
        why = "The face is incomplete (holes where the eyes or mouth should be)";
        return false;
    }
    out.valid = true;
    out.confidence = best.conf;
    return true;
}

void symmetrizeFaceLandmarks(FaceLandmarks& lm, const FaceFrame& frameIn, const Mesh& mesh, const BVH& bvh) {
    FaceFrame f = frameIn;
    if (!f.orthonormalize() || !lm.valid) return;
    const vec3 right = f.right();
    // The middle plane: through the mean of the pair midpoints, nose and chin.
    float m = 0.5f * (glm::dot(lm.noseTip, right) + glm::dot(lm.chin, right));
    float pairs = 0.0f;
    for (int k = 0; k < 2; ++k) {
        const auto& pr = k == 0 ? lm.eyeOuter : lm.mouthCorner;
        pairs += 0.5f * (glm::dot(pr[0], right) + glm::dot(pr[1], right));
    }
    m = 0.5f * m + 0.25f * pairs;
    const float start = frontmost(mesh, f);
    auto snap = [&](vec3 p) {
        vec3 q;
        return castFront(mesh, bvh, f, faceUv(p, f), start, q) ? q : p;
    };
    auto mirror = [&](const vec3& p) { return p - 2.0f * (glm::dot(p, right) - m) * right; };
    auto pair = [&](std::array<vec3, 2>& pr) {
        // Side 0 is on -right; average it with the mirror of side 1.
        const vec3 a = 0.5f * (pr[0] + mirror(pr[1]));
        pr[0] = snap(a);
        pr[1] = snap(mirror(a));
    };
    pair(lm.eyeOuter);
    pair(lm.eyeInner);
    pair(lm.mouthCorner);
    lm.noseTip = snap(lm.noseTip - (glm::dot(lm.noseTip, right) - m) * right);
    lm.chin = snap(lm.chin - (glm::dot(lm.chin, right) - m) * right);
}

std::vector<QRemeshGuide> faceGuides(const Mesh& mesh, const BVH& bvh, const FaceLandmarks& lm, const FaceFrame& frameIn,
                                     float quadEdge) {
    std::vector<QRemeshGuide> guides;
    FaceFrame f = frameIn;
    if (!lm.valid || !f.orthonormalize() || mesh.triangleCount() == 0) return guides;
    const float start = frontmost(mesh, f);
    auto uv = [&](const vec3& p) { return faceUv(p, f); };
    const vec2 nose = uv(lm.noseTip), chin = uv(lm.chin);
    const float mid = 0.5f * (nose.x + chin.x);
    const vec2 mouthL = uv(lm.mouthCorner[0]), mouthR = uv(lm.mouthCorner[1]);
    const vec2 mouth = 0.5f * (mouthL + mouthR);
    const float mh = std::max(0.5f * glm::distance(mouthL, mouthR), 1e-6f); // half the mouth width
    const float minRing = 16.0f * std::max(quadEdge, 0.0f);                // shorter loops don't close cleanly

    // Curves are made for side 0 (-right) in face coordinates and mirrored about the middle.
    auto ellipse = [](vec2 c, float a, float b) {
        std::vector<vec2> pts;
        const int n = 48;
        for (int i = 0; i <= n; ++i) {
            const float t = 6.2831853f * float(i) / float(n);
            pts.push_back(c + vec2(a * std::cos(t), b * std::sin(t)));
        }
        return pts;
    };
    auto perimeter = [](const std::vector<vec2>& pts) {
        float l = 0;
        for (size_t i = 1; i < pts.size(); ++i) l += glm::distance(pts[i - 1], pts[i]);
        return l;
    };
    uint32_t stroke = 0;
    auto add = [&](const std::vector<vec2>& curve, bool mirrored) {
        if (curve.size() < 3 || perimeter(curve) < minRing) return;
        ++stroke;
        for (int side = 0; side < (mirrored ? 2 : 1); ++side) {
            std::vector<vec3> path;
            for (vec2 p : curve) {
                if (side == 1) p.x = 2.0f * mid - p.x;
                vec3 q;
                if (castFront(mesh, bvh, f, p, start, q)) path.push_back(q);
            }
            if (path.size() < 3) continue;
            const size_t before = guides.size();
            addQRemeshGuide(guides, mesh, bvh, path, Symmetry{false, false, false});
            for (size_t g = before; g < guides.size(); ++g) guides[g].stroke = stroke;
        }
    };

    // Eyes: a ring round the eye, and one round the orbit (kept clear of the nose's middle line).
    const vec2 eo = uv(lm.eyeOuter[0]), ei = uv(lm.eyeInner[0]);
    const vec2 eye = 0.5f * (eo + ei);
    const float ew = std::max(glm::distance(eo, ei), 1e-6f);
    const float toMid = std::abs(eye.x - mid);
    add(ellipse(eye + vec2(0.0f, 0.03f * ew), std::min(0.75f * ew, 0.8f * toMid), 0.48f * ew), true);
    const float orbitA = std::min(1.15f * ew, 0.92f * toMid);
    if (orbitA > 0.85f * ew) add(ellipse(eye + vec2(0.0f, 0.12f * ew), orbitA, 0.88f * ew), true);

    // Mouth: a ring round the lips (on the middle: its own mirror image).
    add(ellipse(mouth, 1.18f * mh, 0.55f * mh), false);
    // The nasolabial loop: from the nose wing down the fold past the mouth corner, under the lower lip above the
    // chin, up the other fold and across under the nose; the outer mouth loop of animation topology.
    {
        const float noseGap = std::max(nose.y - mouth.y, 1e-6f), chinGap = std::max(mouth.y - chin.y, 1e-6f);
        const vec2 wing(mid - 0.78f * mh, mouth.y + 0.72f * noseGap);
        const vec2 fold(mid - 1.55f * mh, mouth.y - 0.05f * chinGap);
        const vec2 low(mid - 1.1f * mh, mouth.y - 0.55f * chinGap);
        const vec2 bottom(mid, mouth.y - 0.62f * chinGap);
        const vec2 underNose(mid, mouth.y + 0.66f * noseGap);
        // Half the loop (side 0) as a smooth curve through these points, then mirrored onto the other side.
        const std::vector<vec2> keys{underNose, wing, fold, low, bottom};
        std::vector<vec2> half;
        for (size_t k = 0; k + 1 < keys.size(); ++k) {
            const vec2 p0 = keys[k == 0 ? 0 : k - 1], p1 = keys[k], p2 = keys[k + 1], p3 = keys[std::min(k + 2, keys.size() - 1)];
            for (int i = 0; i < 8; ++i) { // Catmull-Rom
                const float t = float(i) / 8.0f, t2 = t * t, t3 = t2 * t;
                half.push_back(0.5f * ((2.0f * p1) + (-p0 + p2) * t + (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2 +
                                       (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t3));
            }
        }
        half.push_back(bottom);
        std::vector<vec2> loop = half;
        for (size_t i = half.size() - 1; i-- > 1;) loop.push_back(vec2(2.0f * mid - half[i].x, half[i].y));
        loop.push_back(half.front());
        add(loop, false);
    }
    return guides;
}

std::string faceLandmarksJson(const FaceLandmarks& lm) {
    Json j = Json::object();
    for (int i = 0; i < FaceLandmarks::kCount; ++i) {
        Json p = Json::array();
        for (int k = 0; k < 3; ++k) p.push(double(lm.point(i)[k]));
        j.set(FaceLandmarks::name(i), p);
    }
    return j.dump();
}

bool faceLandmarksFromJson(const std::string& text, FaceLandmarks& lm, std::string& error) {
    Json j;
    if (!Json::parse(text, j, error)) return false;
    if (!j.isObject()) {
        error = "landmarks must be a JSON object";
        return false;
    }
    for (int i = 0; i < FaceLandmarks::kCount; ++i) {
        const Json& p = j[FaceLandmarks::name(i)];
        if (!p.isArray() || p.size() != 3) {
            error = std::string("landmarks: missing ") + FaceLandmarks::name(i) + " [x, y, z]";
            return false;
        }
        for (int k = 0; k < 3; ++k) lm.point(i)[k] = float(p[size_t(k)].asNumber());
    }
    lm.valid = true;
    lm.confidence = 1.0f;
    return true;
}

// ---------------------------------------------------------------------------------------------------------
// The whole job
// ---------------------------------------------------------------------------------------------------------

int sculptablePresetQuads(SculptablePreset p) {
    switch (p) {
    case SculptablePreset::Game: return 4000;
    case SculptablePreset::Film: return 12000;
    case SculptablePreset::SculptBase: return 2000;
    default: return 0;
    }
}

const char* sculptablePresetName(SculptablePreset p) {
    switch (p) {
    case SculptablePreset::Game: return "game";
    case SculptablePreset::Film: return "film";
    case SculptablePreset::SculptBase: return "sculpt";
    default: return "custom";
    }
}

bool parseSculptablePreset(const std::string& text, SculptablePreset& out) {
    for (SculptablePreset p : {SculptablePreset::Game, SculptablePreset::Film, SculptablePreset::SculptBase, SculptablePreset::Custom})
        if (text == sculptablePresetName(p)) {
            out = p;
            return true;
        }
    if (text == "sculpt_base" || text == "base") {
        out = SculptablePreset::SculptBase;
        return true;
    }
    return false;
}

std::string SculptableResult::report() const {
    char buf[512];
    const size_t faces = mesh.triangleCount() - mesh.quadCount();
    std::snprintf(buf, sizeof buf, "Made sculptable: %zu polygons%s from %zu splats in %.1f s (surface %.1f s)", faces,
                  subdivLevelCount(mesh) > 1 ? (", " + std::to_string(subdivLevelCount(mesh)) + " levels").c_str() : "",
                  surface.used, seconds, surface.densitySeconds + surface.solidSeconds + surface.meshSeconds);
    std::string s = buf;
    if (surface.floaterVoxels > 0) s += ", " + std::to_string(surface.pieces - 1) + " floaters dropped";
    if (!guides.empty()) {
        size_t loops = 0;
        for (const QRemeshGuide& g : guides) loops += g.closed;
        s += ", face guides: " + std::to_string(loops) + " loops";
    } else if (!faceNote.empty()) {
        s += " (" + faceNote + ")";
    }
    if (uncolored > 0) s += ", " + std::to_string(uncolored) + " vertices with no splats near took their neighbours' colour";
    return s;
}

namespace {

float meshArea(const Mesh& m) {
    double a = 0;
    for (const glm::uvec3& t : m.triangles)
        a += 0.5 * double(glm::length(glm::cross(m.positions[t.y] - m.positions[t.x], m.positions[t.z] - m.positions[t.x])));
    return float(a);
}

const AABB* cropOf(const SplatSurfaceOptions& o) { return o.crop ? &o.cropBox : nullptr; }

} // namespace

bool makeSculptableScan(const SplatCloud& cloud, const SculptableOptions& o, SculptableResult& res, std::string& error,
                        const SculptableProgress& progress) {
    const auto t0 = Clock::now();
    if (!splatSurface(cloud, o.surface, res.scan, error, subProgress(progress, 0.0f, 0.75f), &res.surface)) return false;
    const auto tc = Clock::now();
    const SplatColorSampler sampler(cloud, 2.0f * res.surface.cell, o.surface.minOpacity, cropOf(o.surface));
    colorFromSplats(res.scan, sampler);
    res.colorSeconds = secondsSince(tc);
    if (!tick(progress, 0.9f)) {
        error = "Cancelled";
        return false;
    }
    res.guides.clear();
    if (o.faceGuides) {
        FaceFrame f = o.frame;
        std::string why;
        if (res.landmarks.valid || detectFaceLandmarks(res.scan, f, res.landmarks, why)) {
            BVH bvh;
            bvh.build(res.scan);
            symmetrizeFaceLandmarks(res.landmarks, f, res.scan, bvh);
            const float edge = o.targetQuads > 0 ? std::sqrt(meshArea(res.scan) / float(o.targetQuads)) : 0.0f;
            res.guides = faceGuides(res.scan, bvh, res.landmarks, f, edge);
            res.faceNote = res.guides.empty() ? "face found, but its loops are too small for this polygon count" : "";
        } else {
            res.faceNote = why;
        }
    }
    res.seconds = secondsSince(t0);
    return tick(progress, 1.0f) || (error = "Cancelled", false);
}

bool finishSculptable(const SplatCloud& cloud, const SculptableOptions& o, SculptableResult& res, std::string& error,
                      const SculptableProgress& progress) {
    const auto t0 = Clock::now();
    if (res.scan.triangleCount() == 0) {
        error = "There is no scan to remesh";
        return false;
    }
    if (o.targetQuads > 0) {
        QRemeshOptions q;
        q.targetQuads = std::clamp(o.targetQuads, 16, 2'000'000);
        q.adaptive = std::clamp(o.adaptive, 0.0f, 1.0f);
        q.seed = o.seed;
        // The legacy method for now: on x86 QRemesh 2 flattened part of the test mug's thin handle (0.10.2 CI).
        q.method = QRemeshMethod::Legacy;
        q.guides = res.guides;
        if (o.symmetry) {
            // The world axis along the face's right (QRemesh mirrors across world axes); none if it is tilted.
            FaceFrame f = o.frame;
            if (f.orthonormalize()) {
                const vec3 r = glm::abs(f.right());
                const int axis = r.x >= r.y && r.x >= r.z ? 0 : r.y >= r.z ? 1 : 2;
                if (r[axis] > 0.98f) q.symmetryAxis = axis;
            }
        }
        if (!qremesh(res.scan, q, res.mesh, error, subProgress(progress, 0.0f, 0.7f), &res.qremesh)) return false;
    } else {
        res.mesh = res.scan;
        res.mesh.clearColors();
    }
    if (!tick(progress, 0.72f)) {
        error = "Cancelled";
        return false;
    }
    if (o.color == SculptableColor::Texture) {
        AutoUvResult uv;
        if (!computeAutoUv(res.mesh.positions, res.mesh.triangles, std::clamp(o.textureSize, 256, 8192), uv, error,
                           subProgress(progress, 0.72f, 0.8f)))
            return false;
        res.mesh.uvs = std::move(uv.uvs);
    }
    // Levels: divide, then project the scan's shape onto the top level.
    const auto tp = Clock::now();
    for (int k = 0; k < std::clamp(o.levels, 0, 6); ++k) {
        if (!subdivDivide(res.mesh, true, error)) return false;
        if (!tick(progress, 0.8f + 0.03f * float(k))) {
            error = "Cancelled";
            return false;
        }
    }
    if (o.levels > 0) {
        ProjectOptions po;
        po.polypaint = false;
        po.distance = 0.03f;
        std::vector<vec3> positions;
        ProjectStats ps;
        if (!computeProjection(res.mesh, {&res.scan}, po, positions, nullptr, ps, error)) return false;
        res.mesh.positions = std::move(positions);
        res.mesh.recomputeAllNormals();
    }
    res.projectSeconds = secondsSince(tp);
    if (!tick(progress, 0.9f)) {
        error = "Cancelled";
        return false;
    }
    const auto tc = Clock::now();
    res.mesh.clearColors();
    if (o.color != SculptableColor::None) {
        const SplatColorSampler sampler(cloud, 2.0f * std::max(res.surface.cell, 1e-6f), o.surface.minOpacity, cropOf(o.surface));
        if (o.color == SculptableColor::Texture) textureFromSplats(res.mesh, sampler, std::clamp(o.textureSize, 256, 8192));
        else res.uncolored = colorFromSplats(res.mesh, sampler);
    }
    if (o.levels > 0) subdivCommit(res.mesh);
    res.mesh.markAllDirty();
    res.colorSeconds += secondsSince(tc);
    res.seconds += secondsSince(t0);
    return tick(progress, 1.0f) || (error = "Cancelled", false);
}

namespace {

// An untouched starting sphere makes way for the scan (hidden, not deleted).
void hideStartingSphere(Document& doc) {
    if (doc.subtoolCount() == 1 && !doc.undo.canUndo() && doc.mesh.layers.empty() && !doc.mesh.hasColors())
        doc.activeSubtoolInfo().visible = false;
}

std::string captureName(const Document& doc) { return doc.splats.name.empty() ? std::string("Scan") : doc.splats.name; }

} // namespace

std::string applySculptable(Document& doc, SculptableResult& res, bool keepScan) {
    const std::string base = captureName(doc);
    hideStartingSphere(doc);
    if (keepScan) {
        const int s = doc.addSubtool(std::move(res.scan), doc.uniqueSubtoolName(base + " Scan"), false);
        doc.subtools[size_t(s)].visible = false;
        doc.subtools[size_t(s)].qremeshGuides = res.guides;
    }
    const std::string report = res.report();
    doc.addSubtool(std::move(res.mesh), doc.uniqueSubtoolName(base), true);
    doc.qremeshGuides = res.guides;
    doc.modified = true;
    return report;
}

std::string applySculptableScan(Document& doc, SculptableResult& res) {
    hideStartingSphere(doc);
    const size_t splats = res.surface.used;
    doc.addSubtool(std::move(res.scan), doc.uniqueSubtoolName(captureName(doc) + " Scan"), true);
    doc.qremeshGuides = res.guides;
    doc.modified = true;
    char buf[160];
    std::snprintf(buf, sizeof buf, "Scan: %zu triangles from %zu splats in %.1f s", doc.mesh.triangleCount(), splats, res.seconds);
    std::string msg = buf;
    if (res.landmarks.valid) {
        size_t loops = 0;
        for (const QRemeshGuide& g : res.guides) loops += g.closed;
        msg += "; face guides: " + std::to_string(loops) + " loops; landmarks " + faceLandmarksJson(res.landmarks);
    } else if (!res.faceNote.empty()) {
        msg += " (" + res.faceNote + ")";
    }
    return msg;
}

std::string applySculptableFinish(Document& doc, SculptableResult& res, int scanIndex, bool keepScan) {
    if (scanIndex < 0 || scanIndex >= doc.subtoolCount()) scanIndex = doc.activeSubtool;
    // "Head Scan" -> "Head", "Head Scan 2" -> "Head 2"; another name gets " Quads".
    const std::string name = doc.subtools[size_t(scanIndex)].name;
    std::string base = name + " Quads";
    if (const size_t at = name.rfind(" Scan"); at != std::string::npos && at > 0) {
        const std::string rest = name.substr(at + 5);
        if (rest.empty() || (rest.size() > 1 && rest[0] == ' ' && rest.find_first_not_of("0123456789", 1) == std::string::npos))
            base = name.substr(0, at) + rest;
    }
    const size_t faces = res.mesh.triangleCount() - res.mesh.quadCount();
    const int levels = subdivLevelCount(res.mesh);
    const uint32_t scanId = doc.subtools[size_t(scanIndex)].id;
    doc.addSubtool(std::move(res.mesh), doc.uniqueSubtoolName(base), true, scanIndex + 1);
    doc.qremeshGuides = res.guides;
    for (int i = 0; i < doc.subtoolCount(); ++i) {
        if (doc.subtools[size_t(i)].id != scanId) continue;
        if (keepScan) doc.subtools[size_t(i)].visible = false;
        else doc.removeSubtool(i);
        break;
    }
    doc.modified = true;
    char buf[200];
    std::snprintf(buf, sizeof buf, "Made sculptable: %zu polygons%s in %.1f s", faces,
                  levels > 1 ? (", " + std::to_string(levels) + " levels").c_str() : "", res.seconds);
    std::string msg = buf;
    if (!res.guides.empty()) msg += ", along " + std::to_string(res.guides.size()) + " face guides";
    return msg;
}

// ---------------------------------------------------------------------------------------------------------
// Test subjects
// ---------------------------------------------------------------------------------------------------------

namespace {

float gauss2(float x, float y, float cx, float cy, float sx, float sy) {
    const float dx = (x - cx) / sx, dy = (y - cy) / sy;
    return std::exp(-0.5f * (dx * dx + dy * dy));
}

// The head's design (face coordinates x right, y up, on the front).
constexpr float kHeadA = 0.8f, kHeadB = 1.2f, kHeadC = 0.95f; // ellipsoid semi-axes
constexpr float kEyeX = 0.33f, kEyeY = 0.10f, kEyeHalf = 0.13f;
constexpr float kNoseY = -0.32f, kMouthY = -0.58f, kMouthHalf = 0.26f, kChinY = -0.92f;

float headRelief(float x, float y) {
    float d = 0.0f;
    d += 0.20f * gauss2(x, y, 0.0f, kNoseY, 0.09f, 0.14f);         // nose
    d += 0.08f * gauss2(x, y, 0.0f, kEyeY - 0.08f, 0.07f, 0.14f);  // bridge
    for (float sx : {-1.0f, 1.0f}) {
        d -= 0.10f * gauss2(x, y, sx * kEyeX, kEyeY, 0.12f, 0.09f); // eye socket
        d += 0.04f * gauss2(x, y, sx * 0.30f, kEyeY + 0.17f, 0.16f, 0.05f); // brow
        d += 0.03f * gauss2(x, y, sx * 0.40f, -0.30f, 0.15f, 0.15f); // cheek
    }
    d += 0.05f * gauss2(x, y, 0.0f, kMouthY, 0.20f, 0.07f);      // lips
    d -= 0.04f * gauss2(x, y, 0.0f, kMouthY, 0.20f, 0.025f);     // the line between them
    d += 0.07f * gauss2(x, y, 0.0f, kChinY, 0.16f, 0.10f);       // chin
    return d;
}

glm::vec3 headColor(const vec3& p) {
    const vec3 skin = srgbToLinear(vec3(0.85f, 0.64f, 0.52f));
    const float x = p.x, y = p.y;
    if (y > 0.72f || (p.z < -0.15f && y > -0.2f)) return srgbToLinear(vec3(0.28f, 0.17f, 0.09f)); // hair
    if (p.z > 0.3f) {
        for (float sx : {-1.0f, 1.0f}) {
            const float ex = (x - sx * kEyeX) / 0.065f, ey = (y - kEyeY) / 0.045f;
            if (ex * ex + ey * ey < 1.0f) return srgbToLinear(vec3(0.12f, 0.08f, 0.06f)); // iris
            const float wx = (x - sx * kEyeX) / 0.12f, wy = (y - kEyeY) / 0.05f;
            if (wx * wx + wy * wy < 1.0f) return srgbToLinear(vec3(0.92f, 0.9f, 0.86f)); // white
        }
        const float lx = x / kMouthHalf;
        if (std::abs(lx) < 1.0f && std::abs(y - kMouthY) < 0.065f * (1.0f - lx * lx)) return srgbToLinear(vec3(0.66f, 0.3f, 0.3f));
    }
    return skin;
}

} // namespace

Mesh makeTestHead(int subdivisions, FaceLandmarks* landmarks) {
    Mesh m = makeIcosphere(std::clamp(subdivisions, 2, 7));
    for (vec3& p : m.positions) {
        const vec3 n = glm::normalize(p);
        vec3 q(n.x * kHeadA, n.y * kHeadB, n.z * kHeadC);
        const float front = std::clamp((n.z - 0.2f) / 0.4f, 0.0f, 1.0f);
        q.z += front * front * (3.0f - 2.0f * front) * headRelief(q.x, q.y);
        p = q;
    }
    m.rebuildTopology();
    m.recomputeAllNormals();
    std::vector<glm::vec3>& c = m.ensureColors();
    for (size_t v = 0; v < m.vertexCount(); ++v) c[v] = headColor(m.positions[v]);
    if (landmarks) {
        BVH bvh;
        bvh.build(m);
        FaceFrame f;
        const float start = frontmost(m, f);
        auto on = [&](float x, float y) {
            vec3 q(x, y, 0.0f);
            castFront(m, bvh, f, vec2(x, y), start, q);
            return q;
        };
        FaceLandmarks& lm = *landmarks;
        for (int side = 0; side < 2; ++side) {
            const float s = side == 0 ? -1.0f : 1.0f;
            lm.eyeOuter[size_t(side)] = on(s * (kEyeX + kEyeHalf), kEyeY);
            lm.eyeInner[size_t(side)] = on(s * (kEyeX - kEyeHalf), kEyeY);
            lm.mouthCorner[size_t(side)] = on(s * kMouthHalf, kMouthY);
        }
        lm.noseTip = on(0.0f, kNoseY);
        lm.chin = on(0.0f, kChinY);
        lm.valid = true;
        lm.confidence = 1.0f;
    }
    return m;
}

Mesh makeTestObject(int resolution) {
    // A rounded cylinder (the body) and a torus (the handle) as a distance field.
    AABB box;
    box.expand(vec3(-1.2f, -1.0f, -0.8f));
    box.expand(vec3(1.2f, 1.0f, 0.8f));
    VoxelGrid grid;
    makeVoxelGrid(box, std::clamp(resolution, 32, 384), 2, grid);
    std::vector<float> field(grid.size());
    for (int z = 0; z < grid.dims.z; ++z)
        for (int y = 0; y < grid.dims.y; ++y)
            for (int x = 0; x < grid.dims.x; ++x) {
                const vec3 p = grid.position(x, y, z);
                // Body: radius 0.6, half height 0.8, edges rounded by 0.1.
                const vec2 d = glm::abs(vec2(glm::length(vec2(p.x + 0.25f, p.z)), p.y)) - vec2(0.5f, 0.7f);
                const float body = std::min(std::max(d.x, d.y), 0.0f) + glm::length(glm::max(d, vec2(0.0f))) - 0.1f;
                // Handle: a torus in the XY plane on the right, tube radius 0.075.
                const vec2 t(glm::length(vec2(p.x - 0.55f, p.y)) - 0.38f, p.z);
                const float handle = glm::length(t) - 0.075f;
                field[grid.index(x, y, z)] = -std::min(body, handle);
            }
    Mesh m = meshFromField(grid, field, 0.0f, 2);
    std::vector<glm::vec3>& c = m.ensureColors();
    for (size_t v = 0; v < m.vertexCount(); ++v) {
        const vec3& p = m.positions[v];
        const bool handle = p.x > 0.3f && glm::length(vec2(glm::length(vec2(p.x - 0.55f, p.y)) - 0.38f, p.z)) < 0.11f;
        c[v] = handle ? srgbToLinear(vec3(0.95f, 0.55f, 0.1f))
               : std::fmod(p.y + 2.0f, 0.4f) < 0.2f ? srgbToLinear(vec3(0.15f, 0.35f, 0.75f))
                                                    : srgbToLinear(vec3(0.92f, 0.92f, 0.88f));
    }
    m.markAllDirty();
    return m;
}

SplatCloud makeTestCapture(const Mesh& mesh, const TestCaptureOptions& o) {
    SplatCloud cloud;
    cloud.name = "Capture";
    if (mesh.triangleCount() == 0 || o.count == 0) return cloud;
    const AABB b = mesh.bounds();
    const float cut = b.min.y + 0.15f * b.extent().y;
    std::vector<double> cumulative;
    cumulative.reserve(mesh.triangleCount());
    double total = 0;
    std::vector<vec3> normals(mesh.vertexCount(), vec3(0.0f));
    for (const glm::uvec3& t : mesh.triangles) {
        const vec3 n = glm::cross(mesh.positions[t.y] - mesh.positions[t.x], mesh.positions[t.z] - mesh.positions[t.x]);
        normals[t.x] += n;
        normals[t.y] += n;
        normals[t.z] += n;
        total += 0.5 * double(glm::length(n));
        cumulative.push_back(total);
    }
    const float spacing = float(std::sqrt(total / double(o.count)));
    std::mt19937 rng(o.seed);
    std::uniform_real_distribution<double> pick(0.0, total);
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    std::vector<vec3> pos, col, scale;
    std::vector<glm::quat> rot;
    std::vector<float> alpha;
    auto push = [&](const vec3& p, const glm::quat& r, const vec3& s, float a, const vec3& linearColor) {
        pos.push_back(p);
        rot.push_back(r);
        scale.push_back(s);
        alpha.push_back(a);
        col.push_back(glm::clamp(linearToSrgb(linearColor), vec3(0.0f), vec3(1.0f)));
    };
    for (size_t i = 0; i < o.count; ++i) {
        const size_t tri = size_t(std::upper_bound(cumulative.begin(), cumulative.end(), pick(rng)) - cumulative.begin());
        const glm::uvec3& t = mesh.triangles[std::min(tri, mesh.triangleCount() - 1)];
        float u = unit(rng), v = unit(rng);
        if (u + v > 1.0f) {
            u = 1.0f - u;
            v = 1.0f - v;
        }
        const float w = 1.0f - u - v;
        const vec3 p = w * mesh.positions[t.x] + u * mesh.positions[t.y] + v * mesh.positions[t.z];
        if (o.openBottom && p.y < cut) continue;
        const vec3 n = glm::normalize(w * normals[t.x] + u * normals[t.y] + v * normals[t.z] + vec3(1e-9f));
        const vec3 c = w * mesh.colorAt(t.x) + u * mesh.colorAt(t.y) + v * mesh.colorAt(t.z);
        push(p, rotationFromZ(n), vec3(spacing * 0.6f, spacing * 0.6f, spacing * 0.08f), 0.95f, c);
    }
    // Interior junk: wrongly coloured splats deep inside (trained captures fill the inside with whatever).
    const vec3 centre = b.center(), half = b.extent() * 0.5f;
    for (size_t i = 0; i < o.interior; ++i) {
        const vec3 p = centre + 0.45f * half * vec3(2.0f * unit(rng) - 1.0f, 2.0f * unit(rng) - 1.0f, 2.0f * unit(rng) - 1.0f);
        push(p, glm::quat(1, 0, 0, 0), vec3(spacing * 1.5f), 0.7f, vec3(0.0f, 1.0f, 0.0f));
    }
    // Floaters: little clusters around the subject.
    for (size_t f = 0; f < o.floaters; ++f) {
        vec3 dir(2.0f * unit(rng) - 1.0f, 2.0f * unit(rng) - 1.0f, 2.0f * unit(rng) - 1.0f);
        dir = glm::normalize(dir + vec3(1e-6f));
        const vec3 c = centre + dir * glm::length(half) * (1.3f + 0.4f * unit(rng));
        for (int k = 0; k < 40; ++k) {
            const vec3 p = c + 0.03f * glm::length(half) * vec3(unit(rng) - 0.5f, unit(rng) - 0.5f, unit(rng) - 0.5f);
            push(p, glm::quat(1, 0, 0, 0), vec3(spacing * 1.2f), 0.9f, vec3(0.8f, 0.8f, 0.8f));
        }
    }
    cloud.resize(pos.size(), 0);
    for (size_t i = 0; i < pos.size(); ++i) {
        cloud.positions[i] = pos[i];
        cloud.rotations[i] = rot[i];
        cloud.scales[i] = scale[i];
        cloud.opacities[i] = alpha[i];
        cloud.setColor(i, col[i]);
    }
    return cloud;
}

// ---------------------------------------------------------------------------------------------------------
// Commands
// ---------------------------------------------------------------------------------------------------------

namespace {

using T = ParamSpec::Type;

bool parseVec3(const std::string& s, vec3& out) {
    std::istringstream in(s);
    vec3 v;
    char sep = 0;
    for (int k = 0; k < 3; ++k) {
        if (!(in >> v[k])) return false;
        if (k < 2 && in.peek() == ',') in >> sep;
    }
    out = v;
    return true;
}

bool parseBox(const std::string& s, AABB& out) {
    std::string t = s;
    std::replace(t.begin(), t.end(), ',', ' ');
    std::istringstream in(t);
    float v[6];
    for (float& x : v)
        if (!(in >> x)) return false;
    out = AABB();
    out.expand(vec3(v[0], v[1], v[2]));
    out.expand(vec3(v[3], v[4], v[5]));
    return true;
}

} // namespace

bool sculptableOptionsFromArgs(const Args& a, SculptableOptions& o, std::string& error) {
    SplatSurfaceOptions& s = o.surface;
    s.resolution = int(argNumber(a, "detail", argNumber(a, "resolution", s.resolution)));
    s.minOpacity = float(argNumber(a, "min_opacity", s.minOpacity));
    s.keepFraction = float(argNumber(a, "keep", s.keepFraction));
    s.closeRadius = int(argNumber(a, "close", s.closeRadius));
    s.smoothIterations = int(argNumber(a, "smooth", s.smoothIterations));
    s.fillHoles = argBool(a, "fill_holes", s.fillHoles);
    s.keepAllParts = argBool(a, "keep_parts", s.keepAllParts);
    if (a.contains("crop")) {
        if (!parseBox(argString(a, "crop"), s.cropBox)) {
            error = "crop takes six numbers: min x y z, max x y z";
            return false;
        }
        s.crop = true;
    }
    SculptablePreset preset = SculptablePreset::Game;
    if (a.contains("preset") && !parseSculptablePreset(argString(a, "preset"), preset)) {
        error = "preset must be game, film or sculpt";
        return false;
    }
    o.targetQuads = sculptablePresetQuads(preset);
    if (preset == SculptablePreset::SculptBase) o.levels = 2;
    o.targetQuads = int(std::clamp(argNumber(a, "target", o.targetQuads), 0.0, 2'000'000.0));
    o.levels = int(std::clamp(argNumber(a, "levels", o.levels), 0.0, 6.0));
    o.adaptive = float(argNumber(a, "adaptive", o.adaptive));
    o.symmetry = argBool(a, "symmetry", o.symmetry);
    o.seed = uint32_t(std::max(argNumber(a, "seed", 1.0), 0.0));
    const std::string guides = argString(a, "face_guides", "auto");
    if (guides != "auto" && guides != "off") {
        error = "face_guides must be auto or off";
        return false;
    }
    o.faceGuides = guides == "auto";
    if (a.contains("front") && !parseVec3(argString(a, "front"), o.frame.front)) {
        error = "front takes three numbers";
        return false;
    }
    if (a.contains("up") && !parseVec3(argString(a, "up"), o.frame.up)) {
        error = "up takes three numbers";
        return false;
    }
    if (!o.frame.orthonormalize()) {
        error = "front and up must not be parallel";
        return false;
    }
    const std::string color = argString(a, "color", "polypaint");
    if (color == "polypaint") o.color = SculptableColor::Polypaint;
    else if (color == "texture") o.color = SculptableColor::Texture;
    else if (color == "none") o.color = SculptableColor::None;
    else {
        error = "color must be vertex paint, texture or none";
        return false;
    }
    o.textureSize = int(argNumber(a, "texture_size", o.textureSize));
    if (!validTextureSize(o.textureSize)) {
        error = "texture_size must be 1024, 2048, 4096 or 8192";
        return false;
    }
    return true;
}

void registerSculptableCommands(CommandRegistry& reg) {
    reg.add({"splat_to_mesh",
             "Make sculptable: turn the Gaussian-splat capture into a clean, sculptable Part with the capture's "
             "colours - a watertight surface (floaters dropped, holes filled), quad topology from Quad Remesh with face "
             "loops when it is a head, and vertex paint or a texture. The result is a new Part; the splats stay. "
             "stage=scan stops after the surface and face guides (a \"<name> Scan\" Part to check and nudge); "
             "stage=finish then remeshes the active scan Part along its guides.",
             {{"detail", T::Number, "Voxels across the subject (32-768). Default 256. (resolution: the same.)", false},
              {"preset", T::String, "Polygon budget: game (4000 quads, default), film (12000) or sculpt (2000 quads + 2 "
                                    "subdivision levels with the scan's detail).", false},
              {"target", T::Number, "Target quads instead of the preset's; 0 keeps the voxel surface (no Quad Remesh).", false},
              {"levels", T::Number, "Subdivision levels on top, with the scan's detail projected onto them (0-6).", false},
              {"face_guides", T::String, "auto (default): find a face and add edge loops round the eyes and mouth; off.", false},
              {"landmarks", T::String, "Face landmarks as JSON {\"eye_outer_l\": [x, y, z], ...} (see face_guides) "
                                       "instead of finding them.", false},
              {"front", T::String, "Direction the face looks, \"x y z\". Default \"0 0 1\" (towards the front view).", false},
              {"up", T::String, "Up direction of the head, \"x y z\". Default \"0 1 0\".", false},
              {"color", T::String, "vertex paint (default), texture (Auto UV + a texture) or none.", false},
              {"texture_size", T::Number, "1024, 2048 (default), 4096 or 8192.", false},
              {"crop", T::String, "Only splats inside this box count: \"minx miny minz maxx maxy maxz\".", false},
              {"min_opacity", T::Number, "Ignore splats fainter than this (0-1). Default 0.2.", false},
              {"keep", T::Number, "Fraction of splats to keep per axis when sizing the grid (0.5-1). Default 0.98.", false},
              {"close", T::Number, "Seal gaps up to this many voxels (0-8). Default 2.", false},
              {"fill_holes", T::Bool, "Fill big holes such as an open bottom (default true).", false},
              {"keep_parts", T::Bool, "Keep every solid piece, not just the largest (default false).", false},
              {"smooth", T::Number, "Smoothing passes of the surface (0-50). Default 6.", false},
              {"symmetry", T::Bool, "Symmetric topology across the face (default true).", false},
              {"keep_scan", T::Bool, "Keep the voxel surface as a hidden \"<name> Scan\" Part (default true).", false},
              {"stage", T::String, "all (default), scan or finish.", false},
              {"seed", T::Number, "Quad Remesh's random start (results are repeatable for a seed).", false}},
             [](Document& doc, const Args& a) -> CommandResult {
                 SculptableOptions o;
                 std::string err;
                 if (!sculptableOptionsFromArgs(a, o, err)) return {false, err};
                 const std::string stage = argString(a, "stage", "all");
                 if (stage != "all" && stage != "scan" && stage != "finish") return {false, "stage must be all, scan or finish"};
                 SculptableResult res;
                 if (a.contains("landmarks")) {
                     if (!faceLandmarksFromJson(argString(a, "landmarks"), res.landmarks, err)) return {false, err};
                     o.faceGuides = true;
                 }
                 if (stage == "finish") {
                     if (doc.mesh.triangleCount() == 0) return {false, "The active Part is empty"};
                     res.scan = doc.mesh;
                     res.guides = doc.qremeshGuides;
                     // The voxel size the scan was made with, for the colour radius.
                     const AABB b = doc.mesh.bounds();
                     res.surface.cell = std::max({b.extent().x, b.extent().y, b.extent().z}) / float(std::max(o.surface.resolution, 32));
                     if (doc.splats.empty() && o.color != SculptableColor::None) o.color = SculptableColor::None;
                     if (!finishSculptable(doc.splats, o, res, err)) return {false, err};
                     return {true, applySculptableFinish(doc, res, doc.activeSubtool, argBool(a, "keep_scan", true))};
                 }
                 if (doc.splats.empty()) return {false, "No splats loaded"};
                 if (!makeSculptableScan(doc.splats, o, res, err)) return {false, err};
                 if (stage == "scan") return {true, applySculptableScan(doc, res)};
                 if (!finishSculptable(doc.splats, o, res, err)) return {false, err};
                 return {true, applySculptable(doc, res, argBool(a, "keep_scan", true))};
             }});

    reg.add({"face_guides",
             "Find a face on the active Part (or take the landmarks given) and replace its Quad Remesh Guides with "
             "animation loops: rings round the eyes and the mouth and the nasolabial loop, mirrored across the face. "
             "The message ends with the landmarks as JSON.",
             {{"landmarks", T::String, "JSON {\"eye_outer_l\": [x, y, z], \"eye_inner_l\", \"eye_inner_r\", \"eye_outer_r\", "
                                       "\"nose_tip\", \"mouth_l\", \"mouth_r\", \"chin\"} (l: the -X side when facing the face).", false},
              {"front", T::String, "Direction the face looks, \"x y z\". Default \"0 0 1\".", false},
              {"up", T::String, "Up direction of the head. Default \"0 1 0\".", false},
              {"target", T::Number, "The quad count the guides are meant for (loops too small for it are left out). "
                                    "Default 4000.", false}},
             [](Document& doc, const Args& a) -> CommandResult {
                 if (doc.mesh.triangleCount() == 0) return {false, "The active Part is empty"};
                 FaceFrame f;
                 if (a.contains("front") && !parseVec3(argString(a, "front"), f.front)) return {false, "front takes three numbers"};
                 if (a.contains("up") && !parseVec3(argString(a, "up"), f.up)) return {false, "up takes three numbers"};
                 if (!f.orthonormalize()) return {false, "front and up must not be parallel"};
                 FaceLandmarks lm;
                 std::string err;
                 if (a.contains("landmarks")) {
                     if (!faceLandmarksFromJson(argString(a, "landmarks"), lm, err)) return {false, err};
                 } else if (!detectFaceLandmarks(doc.mesh, f, lm, err)) {
                     return {false, err};
                 }
                 doc.syncBvh();
                 symmetrizeFaceLandmarks(lm, f, doc.mesh, doc.bvh);
                 const float target = float(std::max(argNumber(a, "target", 4000.0), 16.0));
                 doc.qremeshGuides = faceGuides(doc.mesh, doc.bvh, lm, f, std::sqrt(meshArea(doc.mesh) / target));
                 doc.modified = true;
                 size_t loops = 0;
                 for (const QRemeshGuide& g : doc.qremeshGuides) loops += g.closed;
                 char buf[96];
                 std::snprintf(buf, sizeof buf, "Face guides: %zu loops (confidence %.2f); landmarks ", loops, double(lm.confidence));
                 return {true, buf + faceLandmarksJson(lm)};
             }});
    reg.add({"splat_test_capture",
             "Load a built-in test capture (no download): a stylised head or a mug with a thin handle, as Gaussian splats "
             "with colours, a few floaters and hidden interior splats like a real capture. For trying Make sculptable.",
             {{"kind", T::String, "head (default) or object.", false},
              {"count", T::Number, "Surface splats (10000-5000000). Default 200000.", false},
              {"floaters", T::Number, "Floating clusters around the subject. Default 12.", false},
              {"open_bottom", T::Bool, "Leave out the bottom (as a subject standing on a table). Default false.", false}},
             [](Document& doc, const Args& a) -> CommandResult {
                 const std::string kind = argString(a, "kind", "head");
                 if (kind != "head" && kind != "object") return {false, "kind must be head or object"};
                 TestCaptureOptions o;
                 o.count = size_t(std::clamp(argNumber(a, "count", 200000.0), 10000.0, 5000000.0));
                 o.floaters = size_t(std::clamp(argNumber(a, "floaters", 12.0), 0.0, 1000.0));
                 o.interior = o.count / 20;
                 o.openBottom = argBool(a, "open_bottom", false);
                 doc.splats = makeTestCapture(kind == "head" ? makeTestHead(6) : makeTestObject(), o);
                 doc.splats.name = kind == "head" ? "Test Head" : "Test Mug";
                 doc.splatsChanged();
                 return {true, "Loaded the test " + kind + " capture: " + std::to_string(doc.splats.size()) + " splats"};
             }});
}

} // namespace sf
