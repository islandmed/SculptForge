// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/Voxel.h"
#include "core/AtomicRef.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <numeric>
#include <unordered_map>
#include <stdexcept>
#include <unordered_map>

#include "core/CloseHoles.h"
#include "core/HardwareProfile.h"
#include "core/MemoryGuard.h"
#include "core/SurfaceQuery.h"
#include "core/ThreadPool.h"
#include "core/VoxelSparse.h"

namespace sf {

namespace {

// Cancel and progress inside a stage. Voxel Remesh used to ask the caller only between its stages, so a Cancel waited
// for the whole stage (seconds on a dense sculpt: a member cancelled slow remeshes and worked around them). The two
// stages whose work grows with the input (the shell rasterisation and the narrow-band distance field) now run in rounds
// of the thread pool, and the caller's progress runs on this thread between rounds: Cancel lands within one round and the
// bar moves through the stage. Nothing a round does depends on where the rounds are cut (the rasteriser stores the same 1
// everywhere; each distance-field slab visits its triangles in index order, round by round), so the result is the same
// as in one go. The serial flood fills ask every 64k pops.
using StepFn = std::function<bool(float)>; // the job's fraction so far; false = stop
struct StagePoll {
    const StepFn* step = nullptr; // (null: never asked)
    float from = 0.0f, to = 0.0f;
    // k of n rounds done: reports from + (to - from) k / n; false when the caller said stop.
    bool at(size_t k, size_t n) const {
        return !step || !*step || (*step)(from + (to - from) * float(k) / float(std::max<size_t>(n, 1)));
    }
};
constexpr size_t kStageRounds = 64; // at most (a round is a barrier with an idle tail: about 400k triangles each)

// Running max (dilate) or min (erode) over a box of +-radius cells, one axis at a time (separable). Rows along x,
// whole rows for y and whole slices for z, so every pass reads memory in order; the slices run in parallel.
bool morph(std::vector<uint8_t>& data, const glm::ivec3& dims, int radius, bool dilate, const std::function<bool()>& stop = {}) {
    if (radius <= 0) return true;
    const size_t X = size_t(dims.x), Y = size_t(dims.y), Z = size_t(dims.z), S = X * Y;
    std::vector<uint8_t> tmp(data.size());
    auto combine = [dilate](uint8_t* out, const uint8_t* in, size_t count) {
        for (size_t i = 0; i < count; ++i) out[i] = dilate ? std::max(out[i], in[i]) : std::min(out[i], in[i]);
    };
    poolFor(Y * Z, [&](size_t b, size_t e) { // x: data -> tmp
        for (size_t row = b; row < e; ++row) {
            const uint8_t* in = &data[row * X];
            uint8_t* out = &tmp[row * X];
            for (int x = 0; x < dims.x; ++x) {
                uint8_t r = in[x];
                for (int k = std::max(0, x - radius); k <= std::min(dims.x - 1, x + radius); ++k)
                    r = dilate ? std::max(r, in[k]) : std::min(r, in[k]);
                out[x] = r;
            }
        }
    }, 64);
    if (stop && !stop()) return false;
    poolFor(Z, [&](size_t b, size_t e) { // y: tmp -> data
        for (size_t z = b; z < e; ++z)
            for (int y = 0; y < dims.y; ++y) {
                uint8_t* out = &data[z * S + size_t(y) * X];
                std::copy_n(&tmp[z * S + size_t(y) * X], X, out);
                for (int k = std::max(0, y - radius); k <= std::min(dims.y - 1, y + radius); ++k)
                    if (k != y) combine(out, &tmp[z * S + size_t(k) * X], X);
            }
    }, 1);
    if (stop && !stop()) return false;
    poolFor(Z, [&](size_t b, size_t e) { // z: data -> tmp
        for (size_t z = b; z < e; ++z) {
            std::copy_n(&data[z * S], S, &tmp[z * S]);
            for (int k = std::max(0, int(z) - radius); k <= std::min(dims.z - 1, int(z) + radius); ++k)
                if (k != int(z)) combine(&tmp[z * S], &data[size_t(k) * S], S);
        }
    }, 1);
    data.swap(tmp);
    return true;
}

// Labels 6-connected regions where data[i] == value, starting from `seeds` (already marked in `reached`). Span fill:
// each popped sample claims its whole x run, and only the first sample of each run beside it is pushed.
// `stop` (optional) is asked every 64k pops; false leaves the fill where it is and returns false.
bool floodFill(const std::vector<uint8_t>& data, uint8_t value, const glm::ivec3& dims, std::vector<uint32_t>& stack,
               std::vector<uint8_t>& reached, const std::function<bool()>& stop = {}) {
    const size_t X = size_t(dims.x), S = X * size_t(dims.y);
    auto open = [&](size_t j) { return !reached[j] && data[j] == value; };
    size_t pops = 0;
    while (!stack.empty()) {
        if (stop && (++pops & 0xffff) == 0 && !stop()) return false;
        const size_t i = stack.back();
        stack.pop_back();
        const size_t row = i - i % X; // the row's first sample
        size_t lo = i, hi = i;
        while (lo > row && open(lo - 1)) reached[--lo] = 1;
        while (hi + 1 < row + X && open(hi + 1)) reached[++hi] = 1;
        const size_t y = (i / X) % size_t(dims.y), z = i / S;
        auto scan = [&](bool inRange, size_t offset, bool up) {
            if (!inRange) return;
            bool inRun = false;
            for (size_t k = lo; k <= hi; ++k) {
                const size_t j = up ? k + offset : k - offset;
                const bool o = open(j);
                if (o && !inRun) {
                    reached[j] = 1;
                    stack.push_back(uint32_t(j));
                }
                inRun = o;
            }
        };
        scan(y > 0, X, false);
        scan(y + 1 < size_t(dims.y), X, true);
        scan(z > 0, S, false);
        scan(z + 1 < size_t(dims.z), S, true);
    }
    return true;
}

void blurAxis(std::vector<float>& f, const glm::ivec3& dims, int axis) {
    static const std::array<float, 5> w = [] {
        std::array<float, 5> k{};
        float sum = 0.0f;
        for (int i = 0; i < 5; ++i) sum += k[size_t(i)] = std::exp(-0.5f * float((i - 2) * (i - 2)));
        for (float& x : k) x /= sum;
        return k;
    }();
    const glm::ivec3 step(1, dims.x, dims.x * dims.y);
    const int n = dims[axis];
    const int u = (axis + 1) % 3, v = (axis + 2) % 3;
    std::vector<float> line(static_cast<size_t>(n));
    for (int b = 0; b < dims[v]; ++b)
        for (int a = 0; a < dims[u]; ++a) {
            const size_t start = size_t(a) * size_t(step[u]) + size_t(b) * size_t(step[v]);
            for (int i = 0; i < n; ++i) line[size_t(i)] = f[start + size_t(i) * size_t(step[axis])];
            for (int i = 0; i < n; ++i) {
                float s = 0.0f;
                for (int k = -2; k <= 2; ++k) s += w[size_t(k + 2)] * line[size_t(std::clamp(i + k, 0, n - 1))];
                f[start + size_t(i) * size_t(step[axis])] = s;
            }
        }
}

// An edge with one triangle (undirected, so flipped triangles don't hide it). Edges bucketed by their lower vertex (a
// counting sort), then each vertex's few edges sorted on their own, in parallel: no sort of every edge at once.
bool hasOpenEdges(const Mesh& mesh) {
    const size_t nv = mesh.positions.size();
    std::vector<uint32_t> off(nv + 1, 0);
    for (const glm::uvec3& t : mesh.triangles)
        for (int k = 0; k < 3; ++k) ++off[size_t(std::min(t[k], t[(k + 1) % 3])) + 1];
    for (size_t v = 0; v < nv; ++v) off[v + 1] += off[v];
    std::vector<uint32_t> other(off[nv]);
    {
        std::vector<uint32_t> fill(off.begin(), off.end() - 1);
        for (const glm::uvec3& t : mesh.triangles)
            for (int k = 0; k < 3; ++k) {
                const uint32_t a = t[k], b = t[(k + 1) % 3];
                other[fill[std::min(a, b)]++] = std::max(a, b);
            }
    }
    std::atomic<bool> open{false};
    poolFor(nv, [&](size_t b0, size_t e0) {
        for (size_t v = b0; v < e0 && !open.load(std::memory_order_relaxed); ++v) {
            const auto first = other.begin() + long(off[v]), last = other.begin() + long(off[v + 1]);
            std::sort(first, last);
            for (auto i = first; i != last;) {
                auto j = i + 1;
                while (j != last && *j == *i) ++j;
                if (j - i == 1) {
                    open.store(true, std::memory_order_relaxed);
                    break;
                }
                i = j;
            }
        }
    }, 16384);
    return open.load();
}

// Drops the pieces of a surface-nets mesh (quads as triangle pairs t, t + 1) with fewer than `maxTriangles`
// triangles, when a bigger one is left: single-voxel specks where a sign flips next to a cap.
void dropSpecks(Mesh& m, size_t maxTriangles) {
    std::vector<uint32_t> parent(m.vertexCount());
    std::iota(parent.begin(), parent.end(), 0u);
    auto find = [&](uint32_t a) {
        while (parent[a] != a) a = parent[a] = parent[parent[a]];
        return a;
    };
    for (const glm::uvec3& t : m.triangles) {
        parent[find(t.y)] = find(t.x);
        parent[find(t.z)] = find(t.x);
    }
    std::vector<uint32_t> size(m.vertexCount(), 0);
    for (const glm::uvec3& t : m.triangles) ++size[find(t.x)];
    if (std::count_if(size.begin(), size.end(), [&](uint32_t c) { return c >= maxTriangles; }) == 0) return;
    std::vector<glm::uvec3> kept;
    kept.reserve(m.triangles.size());
    for (const glm::uvec3& t : m.triangles)
        if (size[find(t.x)] >= maxTriangles) kept.push_back(t);
    if (kept.size() == m.triangles.size()) return;
    std::vector<uint32_t> remap(m.vertexCount(), ~0u);
    std::vector<glm::vec3> positions;
    for (glm::uvec3& t : kept)
        for (int k = 0; k < 3; ++k) {
            uint32_t& r = remap[t[k]];
            if (r == ~0u) {
                r = uint32_t(positions.size());
                positions.push_back(m.positions[t[k]]);
            }
            t[k] = r;
        }
    Mesh out;
    out.positions = std::move(positions);
    out.triangles = std::move(kept);
    for (uint32_t t = 0; t + 1 < out.triangles.size(); t += 2) out.pairQuad(t, t + 1); // (a piece drops whole quads)
    out.rebuildTopology();
    out.recomputeAllNormals();
    m = std::move(out);
}


} // namespace

// The stages with Cancel and progress inside (StagePoll above); Voxel.h's plain versions call these with no poll.
std::vector<uint8_t> solidifyShellPolled(const VoxelGrid& shell, float threshold, int closeRadius, bool keepAllParts,
                                         const StagePoll& poll, bool& stopped);
bool rasterizeMeshShellPolled(const Mesh& mesh, VoxelGrid& grid, const StagePoll& poll);
Mesh meshFromFieldPolled(const VoxelGrid& grid, const std::vector<float>& f, float iso, int smoothIterations, const StagePoll& poll,
                         bool& stopped);
// `nearestOut`: each sample's nearest triangle (-1: none measured), for snapCreases.
std::vector<float> insideDistanceFieldPolled(const Mesh& mesh, const VoxelGrid& grid, const std::vector<uint8_t>& solid,
                                             float band, const StagePoll& poll, bool& stopped,
                                             std::vector<int32_t>* nearestOut = nullptr, size_t* sealedOpened = nullptr);

glm::vec3 closestOnTriangle(const glm::vec3& p, const glm::vec3& a, const glm::vec3& b, const glm::vec3& c);

namespace {
bool g_creaseSnap = true; // (tests: setVoxelCreaseSnap)

// Segment pq through the inside of triangle abc (Moller-Trumbore; touching an edge or a corner doesn't count).
bool segmentCrossesTriangle(const glm::vec3& p, const glm::vec3& q, const glm::vec3& a, const glm::vec3& b, const glm::vec3& c) {
    const glm::vec3 d = q - p, e1 = b - a, e2 = c - a, h = glm::cross(d, e2);
    const float det = glm::dot(e1, h);
    if (std::abs(det) < 1e-20f) return false;
    const float f = 1.0f / det;
    const glm::vec3 s = p - a;
    const float u = f * glm::dot(s, h);
    if (u <= 1e-6f || u >= 1.0f - 1e-6f) return false;
    const glm::vec3 k = glm::cross(s, e1);
    const float v = f * glm::dot(d, k);
    if (v <= 1e-6f || u + v >= 1.0f - 1e-6f) return false;
    const float t = f * glm::dot(e2, k);
    return t > 1e-6f && t < 1.0f - 1e-6f;
}
} // namespace

void setVoxelCreaseSnap(bool on) { g_creaseSnap = on; }

// Creases under two voxels wide (gh#240: wrinkles on a decimated head came out stair-stepped). Surface nets puts each
// vertex at the mean of its cell's edge crossings, interpolated linearly between samples a voxel apart, so a groove
// narrower than two voxels is seen or missed depending on where the samples fall: along a crease that runs across the
// grid it comes and goes (its depth swung +-45% at the default Resolution, its flanks tilted 44 degrees off).
// The distance field already knows each sample's nearest source triangle (the Hermite data of dual contouring: Ju et
// al. 2002; Kobbelt et al. 2001). After the smoothing, a vertex at a crease (its cell's nearest triangles turn more
// than kFlat, and it sits more than kOffMin off the source) moves along its own normal to where the planes of the
// nearest triangles of the 27 samples around it say the surface is (least squares, the nearer planes weighing more),
// with a little smoothing between rounds so neighbours agree. Moving along the normal only, vertices don't slide past
// each other. The move fades in up to kSharp and kOffFull and is at most a cell; one that doesn't end nearer the source
// or would turn a face over goes back. Each touched quad's diagonal is then picked again, along the crease rather than
// across it, and a move that makes two faces cross is halved until they don't. Everything else is not touched (smooth
// surfaces and fine bumps come out byte-identical), the voxel topology and the quads stay, and it reads only what the
// distance field kept: no search structure.
// Returns whether a quad's diagonal changed (the caller rebuilds the topology).
template <class NearestAt>
bool snapCreases(Mesh& m, const Mesh& source, const VoxelGrid& grid, const NearestAt& nearestAt, CreaseReport* report = nullptr) {
    const size_t nv = m.positions.size();
    if (nv == 0 || m.normals.size() != nv) return false;
    const float cell = grid.cell, inv = 1.0f / cell;
    // A crease: its cell's nearest triangles turn more than 35 degrees (full move from 70); fine smooth bumps turn
    // less within a cell (the bumpy bench spheres: none past 35).
    const float kFlat = std::cos(glm::radians(35.0f)), kHalfFlat = std::cos(glm::radians(17.5f)), kSharp = std::cos(glm::radians(70.0f));
    constexpr int kRounds = 5;
    constexpr float kLambda = 0.3f, kReach = 1.0f; // (smoothing between rounds; planes within a cell count)
    constexpr float kOffMin = 0.1f, kOffFull = 0.2f; // (cells off the source: untouched below, full move above) // (cells off the source: untouched below, full move above)
    const auto& S = source.positions;
    auto inGrid = [&](const glm::ivec3& s) {
        return glm::all(glm::greaterThanEqual(s, glm::ivec3(0))) && glm::all(glm::lessThan(s, grid.dims));
    };
    static const bool timing = std::getenv("SF_VOXEL_TIMING") != nullptr;
    auto clock0 = std::chrono::steady_clock::now();
    auto lap = [&](const char* what) {
        if (!timing) return;
        const auto now = std::chrono::steady_clock::now();
        std::fprintf(stderr, "    creases %-14s %6.1f ms\n", what, std::chrono::duration<double, std::milli>(now - clock0).count());
        clock0 = now;
    };
    const std::vector<glm::vec3> original = m.positions;
    // The vertices at a crease (the nearest triangles of their cell's corners turn), with how much they move (fade),
    // found once; then the planes they move onto (the nearest triangles of the 27 samples around them).
    constexpr int kMax = 27;
    std::vector<float> fadeOf(nv, 0.0f);
    poolFor(nv, [&](size_t b, size_t e) {
        std::array<int32_t, 8> tris;
        std::array<glm::vec3, 8> normals;
        for (size_t v = b; v < e; ++v) {
            const glm::ivec3 c(glm::floor((original[v] - grid.origin) * inv));
            int count = 0;
            bool spread = false; // (all within half of kFlat of the first: no pair can be past kFlat)
            for (int k = 0; k < 8; ++k) {
                const glm::ivec3 s = c + glm::ivec3(k & 1, (k >> 1) & 1, (k >> 2) & 1);
                if (!inGrid(s)) continue;
                const int32_t t = nearestAt(s);
                if (t < 0 || std::find(tris.begin(), tris.begin() + count, t) != tris.begin() + count) continue;
                const glm::uvec3 tri = source.triangles[size_t(t)];
                const glm::vec3 n = glm::cross(S[tri.y] - S[tri.x], S[tri.z] - S[tri.x]);
                const float l = glm::length(n);
                if (!(l > 0.0f)) continue;
                tris[size_t(count)] = t, normals[size_t(count)] = n / l;
                spread |= glm::dot(normals[size_t(count)], normals[0]) < kHalfFlat;
                ++count;
            }
            if (!spread) continue;
            float minDot = 1.0f;
            for (int i = 0; i < count; ++i)
                for (int j = i + 1; j < count; ++j) minDot = std::min(minDot, glm::dot(normals[size_t(i)], normals[size_t(j)]));
            if (minDot >= kFlat) continue;
            // Smooth detail that only turns fast (fine bumps) the surface nets already follow: only a vertex that
            // missed the source counts.
            float off = 1e30f;
            for (int i = 0; i < count; ++i) {
                const glm::uvec3 tri = source.triangles[size_t(tris[size_t(i)])];
                off = std::min(off, glm::length(closestOnTriangle(original[v], S[tri.x], S[tri.y], S[tri.z]) - original[v]));
            }
            fadeOf[v] = std::clamp((kFlat - minDot) / (kFlat - kSharp), 0.0f, 1.0f) *
                        std::clamp((off * inv - kOffMin) / (kOffFull - kOffMin), 0.0f, 1.0f);
        }
    }, 1024);
    std::vector<uint32_t> crease;
    for (uint32_t v = 0; v < nv; ++v)
        if (fadeOf[v] > 0.0f) crease.push_back(v);
    std::vector<uint8_t> changed(nv, 0);
    lap("find");
    if (timing) std::fprintf(stderr, "    creases: %zu of %zu vertices\n", crease.size(), nv);
    if (crease.empty()) return false;
    for (uint32_t v : crease) changed[v] = 1;
    std::vector<int32_t> trisOf(crease.size() * kMax, -1); // (each crease vertex's distinct nearest triangles)
    poolFor(crease.size(), [&](size_t b, size_t e) {
        for (size_t i = b; i < e; ++i) {
            int32_t* tris = &trisOf[i * kMax];
            const glm::ivec3 c(glm::round((original[crease[i]] - grid.origin) * inv));
            int count = 0;
            for (int k = 0; k < kMax; ++k) {
                const glm::ivec3 s = c + glm::ivec3(k % 3 - 1, (k / 3) % 3 - 1, k / 9 - 1);
                if (!inGrid(s)) continue;
                const int32_t t = nearestAt(s);
                if (t >= 0 && std::find(tris, tris + count, t) == tris + count) tris[count++] = t;
            }
        }
    }, 256);
    // A crease's width at x (scene units; -1: can't tell) and how opposed its flanks are (`flanks`: the lowest dot of two
    // of its triangles' normals), from crease vertex i's triangles: the flanks (the most opposed pair) and the surface
    // around it (the triangle facing most like `around`) meet in the crease's bottom line, h below that surface, and open
    // to h (cot a1 + cot a2) across it (a1, a2: each flank's tilt from the surface).
    auto measureCrease = [&](size_t i, const glm::vec3& x, const glm::vec3& around, float& flanks) {
        const int32_t* tris = &trisOf[i * kMax];
        std::array<glm::vec3, kMax> q, n;
        int count = 0;
        for (int k = 0; k < kMax && tris[k] >= 0; ++k) {
            const glm::uvec3 tri = source.triangles[size_t(tris[k])];
            const glm::vec3 nt = glm::cross(S[tri.y] - S[tri.x], S[tri.z] - S[tri.x]);
            if (!(glm::length(nt) > 0.0f)) continue;
            q[size_t(count)] = closestOnTriangle(x, S[tri.x], S[tri.y], S[tri.z]), n[size_t(count)] = glm::normalize(nt);
            ++count;
        }
        int f1 = -1, f2 = -1;
        flanks = 1.0f;
        for (int a = 0; a < count; ++a)
            for (int c = a + 1; c < count; ++c)
                if (const float d = glm::dot(n[size_t(a)], n[size_t(c)]); d < flanks) flanks = d, f1 = a, f2 = c;
        if (f1 < 0 || flanks >= kFlat || !(glm::length(around) > 0.0f)) return -1.0f;
        const glm::vec3 up = glm::normalize(around);
        int top = -1;
        float facing = -2.0f;
        for (int k = 0; k < count; ++k)
            if (k != f1 && k != f2 && glm::dot(n[size_t(k)], up) > facing) facing = glm::dot(n[size_t(k)], up), top = k;
        if (top < 0 || facing < kFlat) return -1.0f;
        const glm::vec3 n1 = n[size_t(f1)], n2 = n[size_t(f2)], n0 = n[size_t(top)];
        const float b1 = glm::dot(n1, q[size_t(f1)] - x), b2 = glm::dot(n2, q[size_t(f2)] - x), c12 = glm::dot(n1, n2);
        const float det = 1.0f - c12 * c12; // (the bottom line's point nearest x: x + l1 n1 + l2 n2 on both planes)
        if (!(det > 1e-4f)) return -1.0f;
        const float l1 = (b1 - c12 * b2) / det, l2 = (b2 - c12 * b1) / det;
        const float h = std::abs(glm::dot(n0, x + l1 * n1 + l2 * n2 - q[size_t(top)]));
        auto cot = [&](const glm::vec3& f) {
            const float c = std::clamp(glm::dot(f, n0), -1.0f, 0.999f);
            return std::max(c, 0.0f) / std::sqrt(1.0f - c * c);
        };
        const float w = h * (cot(n1) + cot(n2));
        return w > 0.0f && w <= 4.0f * cell ? w : -1.0f;
    };
    // Creases no vertex can follow are left as the voxels made them: a slit (walls facing each other, more than 120
    // degrees apart: closed lips, a fold) or a crease under a voxel wide. Snapping there pulls single vertices onto one
    // wall or the other: shards and notches (artist-qa 11-25, a lip line).
    poolFor(crease.size(), [&](size_t b, size_t e) {
        for (size_t i = b; i < e; ++i) {
            float flanks = 1.0f;
            const float w = measureCrease(i, original[crease[i]], m.normals[crease[i]], flanks);
            if (flanks < -0.5f || (w >= 0.0f && w < cell)) changed[crease[i]] = 0, fadeOf[crease[i]] = 0.0f;
        }
    }, 256);
    std::vector<glm::vec3> cur(original), moved(original), sum(nv);
    std::vector<int> degree(nv, 0);
    std::vector<uint32_t> around; // the faces at a crease vertex
    for (uint32_t t = 0; t < m.triangles.size(); ++t)
        if (changed[m.triangles[t].x] | changed[m.triangles[t].y] | changed[m.triangles[t].z]) around.push_back(t);
    for (int round = 0; round < kRounds; ++round) {
        if (round > 0) { // the crease vertices a little toward their neighbours, then onto the planes again
            for (uint32_t v : crease) sum[v] = glm::vec3(0.0f), degree[v] = 0;
            for (uint32_t f : around)
                for (int k = 0; k < 3; ++k) {
                    const glm::uvec3 t = m.triangles[f];
                    const uint32_t i = t[k], j = t[(k + 1) % 3];
                    if (changed[i]) sum[i] += moved[j], ++degree[i];
                    if (changed[j]) sum[j] += moved[i], ++degree[j];
                }
            for (uint32_t v : crease)
                cur[v] = degree[v] ? moved[v] + kLambda * (sum[v] / float(degree[v]) - moved[v]) : moved[v];
        }
        poolFor(crease.size(), [&](size_t b, size_t e) {
            for (size_t i = b; i < e; ++i) {
                const uint32_t v = crease[i];
                if (!changed[v]) continue; // (left alone: a slit or too fine)
                const glm::vec3 x = cur[v], axis = glm::normalize(m.normals[v]);
                double num = 0.0, den = 0.0;
                for (int k = 0; k < kMax && trisOf[i * kMax + size_t(k)] >= 0; ++k) {
                    const glm::uvec3 tri = source.triangles[size_t(trisOf[i * kMax + size_t(k)])];
                    const glm::vec3 nt = glm::cross(S[tri.y] - S[tri.x], S[tri.z] - S[tri.x]);
                    const float l = glm::length(nt);
                    if (!(l > 0.0f)) continue;
                    const glm::vec3 n = nt / l, q = closestOnTriangle(x, S[tri.x], S[tri.y], S[tri.z]);
                    const float dq = glm::length(q - x);
                    if (dq > kReach * cell) continue;
                    const double w = 1.0 / ((double(dq * inv) + 0.25) * (double(dq * inv) + 0.25)), a = glm::dot(n, axis);
                    num += w * a * double(glm::dot(n, q - x)), den += w * a * a;
                }
                moved[v] = den > 1e-12 ? x + axis * (std::clamp(float(num / den), -cell, cell) * fadeOf[v]) : x;
            }
        }, 256);
    }
    // A move that doesn't bring the vertex nearer the source (a plane that only fits a curve nearby) goes back.
    poolFor(crease.size(), [&](size_t b, size_t e) {
        for (size_t i = b; i < e; ++i) {
            const uint32_t v = crease[i];
            float before = 1e30f, after = 1e30f;
            for (int k = 0; k < kMax && trisOf[i * kMax + size_t(k)] >= 0; ++k) {
                const glm::uvec3 tri = source.triangles[size_t(trisOf[i * kMax + size_t(k)])];
                before = std::min(before, glm::length(closestOnTriangle(original[v], S[tri.x], S[tri.y], S[tri.z]) - original[v]));
                after = std::min(after, glm::length(closestOnTriangle(moved[v], S[tri.x], S[tri.y], S[tri.z]) - moved[v]));
            }
            if (!(after < before)) moved[v] = original[v], changed[v] = 0;
        }
    }, 256);
    lap("snap");
    // No face turned over and no new fold (two faces across an edge more than 120 degrees apart: a tooth on the
    // crease's shoulder): the moves around one go halfway back, then all the way (a few rounds: undoing one can't
    // make another).
    auto faceNormal = [&](const std::vector<glm::vec3>& p, uint32_t t) {
        const glm::uvec3 f = m.triangles[t];
        return glm::cross(p[f.y] - p[f.x], p[f.z] - p[f.x]);
    };
    auto folded = [&](const glm::vec3& a, const glm::vec3& b) { return glm::dot(a, b) < -0.5f * glm::length(a) * glm::length(b); };
    for (int round = 0; round < 6; ++round) {
        std::vector<uint8_t> undo(nv, 0);
        std::atomic<bool> any{false};
        poolFor(crease.size(), [&](size_t b, size_t e) {
            for (size_t i = b; i < e; ++i) {
                const uint32_t v = crease[i];
                if (!changed[v]) continue;
                const IndexRing around = m.vertexTriangles(v);
                bool bad = false;
                for (auto it = around.begin(); it != around.end() && !bad; ++it) {
                    const glm::vec3 n0 = faceNormal(original, *it), n1 = faceNormal(moved, *it);
                    bad = glm::dot(n0, n1) <= 0.05f * glm::length(n0) * glm::length(n1);
                    const glm::uvec3 f = m.triangles[*it];
                    for (int k = 0; k < 3 && !bad; ++k) { // the face across each of its edges
                        const uint32_t a = f[k], c = f[(k + 1) % 3];
                        for (uint32_t u : m.vertexTriangles(a)) {
                            const glm::uvec3 g = m.triangles[u];
                            if (u == *it || (g.x != c && g.y != c && g.z != c)) continue;
                            bad = folded(n1, faceNormal(moved, u)) && !folded(n0, faceNormal(original, u));
                            break;
                        }
                    }
                }
                if (bad) undo[v] = 1, any.store(true, std::memory_order_relaxed);
            }
        }, 256);
        if (!any) break;
        for (uint32_t v : crease)
            if (undo[v]) {
                moved[v] = round < 3 ? 0.5f * (moved[v] + original[v]) : original[v];
                if (round >= 3) changed[v] = 0;
            }
    }
    m.positions = std::move(moved);
    lap("flips");

    // Each touched quad's diagonal again: the one whose middle lies nearer the source (along the crease, not across
    // it), when neither half turns against the quad.
    auto sourceDistance = [&](const glm::vec3& p) {
        const glm::ivec3 c(glm::floor((p - grid.origin) * inv));
        float best = 1e30f;
        for (int k = 0; k < 8; ++k) {
            const glm::ivec3 s = c + glm::ivec3(k & 1, (k >> 1) & 1, (k >> 2) & 1);
            if (!inGrid(s)) continue;
            const int32_t t = nearestAt(s);
            if (t < 0) continue;
            const glm::uvec3 tri = source.triangles[size_t(t)];
            best = std::min(best, glm::length(closestOnTriangle(p, S[tri.x], S[tri.y], S[tri.z]) - p));
        }
        return best;
    };
    // (decided first, from the triangles as they are, then applied)
    const auto& P = m.positions;
    const size_t quads = m.triangles.size() / 2;
    std::vector<uint8_t> flip(quads, 0);
    auto across = [&](uint32_t x, uint32_t y, uint32_t skip) { // the face beyond edge xy, other than quad `skip`'s
        for (uint32_t u : m.vertexTriangles(x)) {
            const glm::uvec3 g = m.triangles[u];
            if (u / 2 != skip && (g.x == y || g.y == y || g.z == y)) return int64_t(u);
        }
        return int64_t(-1);
    };
    poolFor(quads, [&](size_t b, size_t e) {
        for (size_t q = b; q < e; ++q) {
            const glm::uvec3 t1 = m.triangles[2 * q], t2 = m.triangles[2 * q + 1];
            if (!(changed[t1.x] | changed[t1.y] | changed[t1.z] | changed[t2.x] | changed[t2.y] | changed[t2.z])) continue;
            int o = -1; // t1's corner that t2 lacks, and t2's that t1 lacks: the quad is o1 s1 o2 s2, diagonal s1-s2
            uint32_t o2 = ~0u;
            for (int k = 0; k < 3; ++k) {
                if (t1[k] != t2.x && t1[k] != t2.y && t1[k] != t2.z) o = k;
                if (t2[k] != t1.x && t2[k] != t1.y && t2[k] != t1.z) o2 = t2[k];
            }
            if (o < 0 || o2 == ~0u) continue;
            const uint32_t o1 = t1[o], s1 = t1[(o + 1) % 3], s2 = t1[(o + 2) % 3];
            const glm::vec3 quadN = glm::cross(P[o2] - P[o1], P[s2] - P[s1]);
            const glm::vec3 a = glm::cross(P[s1] - P[o1], P[o2] - P[o1]), c = glm::cross(P[o2] - P[o1], P[s2] - P[o1]);
            if (glm::dot(a, quadN) <= 0.0f || glm::dot(c, quadN) <= 0.0f || folded(a, c)) continue;
            if (sourceDistance(0.5f * (P[o1] + P[o2])) + 0.02f * cell >= sourceDistance(0.5f * (P[s1] + P[s2]))) continue;
            // No new fold with the faces around the quad: each outer edge's new half against the face beyond it.
            const glm::vec3 old1 = faceNormal(P, uint32_t(2 * q)), old2 = faceNormal(P, uint32_t(2 * q + 1));
            bool ok = true;
            const std::array<std::array<uint32_t, 2>, 4> edges = {{{o1, s1}, {s1, o2}, {o2, s2}, {s2, o1}}};
            for (int k = 0; k < 4 && ok; ++k) {
                const int64_t u = across(edges[size_t(k)][0], edges[size_t(k)][1], uint32_t(q));
                if (u < 0) continue;
                const glm::vec3 n = faceNormal(P, uint32_t(u)), now = k < 2 ? a : c, was = k == 0 || k == 3 ? old1 : old2;
                ok = !folded(now, n) || folded(was, n);
            }
            flip[q] = ok;
        }
    }, 1024);
    bool rediagonalised = false;
    for (size_t q = 0; q < quads; ++q)
        if (flip[q]) {
            glm::uvec3& t1 = m.triangles[2 * q];
            const glm::uvec3 t2 = m.triangles[2 * q + 1];
            int o = 0;
            uint32_t o2 = 0;
            for (int k = 0; k < 3; ++k) {
                if (t1[k] != t2.x && t1[k] != t2.y && t1[k] != t2.z) o = k;
                if (t2[k] != t1.x && t2[k] != t1.y && t2[k] != t1.z) o2 = t2[k];
            }
            const uint32_t o1 = t1[o], s1 = t1[(o + 1) % 3], s2 = t1[(o + 2) % 3];
            t1 = glm::uvec3(o1, s1, o2), m.triangles[2 * q + 1] = glm::uvec3(o1, o2, s2);
            rediagonalised = true;
        }
    lap("diagonals");

    // No new self-intersection: the faces within two rings of a moved vertex, bucketed by the cells their boxes touch
    // (sorted), are tested in pairs that share no corner and include a moved one, each pair once (in the first cell of
    // its boxes' overlap); crossings the surface already had are left alone. A crossing pair's moved corners go halfway
    // back, then all the way, and the next round looks again around those only.
    std::vector<uint8_t> seed(changed);
    std::vector<std::pair<uint64_t, uint32_t>> buckets;
    std::vector<glm::ivec3> lo(m.triangles.size()), hi(m.triangles.size());
    auto cellKey = [](const glm::ivec3& c) {
        return (uint64_t(uint32_t(c.x) & 0x1fffffu) << 42) | (uint64_t(uint32_t(c.y) & 0x1fffffu) << 21) | uint64_t(uint32_t(c.z) & 0x1fffffu);
    };
    for (int round = 0; round < 8; ++round) {
        std::vector<uint8_t> ring(seed);
        for (int r = 0; r < 2; ++r) {
            std::vector<uint8_t> next(ring);
            for (const glm::uvec3& t : m.triangles)
                if (ring[t.x] | ring[t.y] | ring[t.z]) next[t.x] = next[t.y] = next[t.z] = 1;
            ring.swap(next);
        }
        buckets.clear();
        for (uint32_t t = 0; t < m.triangles.size(); ++t) {
            const glm::uvec3 f = m.triangles[t];
            if (!(ring[f.x] | ring[f.y] | ring[f.z])) continue;
            const glm::vec3 a = P[f.x], b = P[f.y], c = P[f.z];
            lo[t] = glm::ivec3(glm::floor((glm::min(a, glm::min(b, c)) - grid.origin) * inv));
            hi[t] = glm::ivec3(glm::floor((glm::max(a, glm::max(b, c)) - grid.origin) * inv));
            for (int z = lo[t].z; z <= hi[t].z; ++z)
                for (int y = lo[t].y; y <= hi[t].y; ++y)
                    for (int x = lo[t].x; x <= hi[t].x; ++x) buckets.push_back({cellKey({x, y, z}), t});
        }
        if (buckets.empty()) break;
        std::sort(buckets.begin(), buckets.end());
        std::vector<size_t> starts;
        for (size_t i = 0; i < buckets.size(); ++i)
            if (i == 0 || buckets[i].first != buckets[i - 1].first) starts.push_back(i);
        starts.push_back(buckets.size());
        std::vector<uint8_t> back(nv, 0);
        std::atomic<bool> any{false};
        poolFor(starts.size() - 1, [&](size_t sb, size_t se) {
            for (size_t s0 = sb; s0 < se; ++s0)
                for (size_t i = starts[s0]; i < starts[s0 + 1]; ++i)
                    for (size_t j = i + 1; j < starts[s0 + 1]; ++j) {
                        const uint32_t ti = buckets[i].second, tj = buckets[j].second;
                        const glm::uvec3 f = m.triangles[ti], g = m.triangles[tj];
                        if (!(changed[f.x] | changed[f.y] | changed[f.z] | changed[g.x] | changed[g.y] | changed[g.z])) continue;
                        const glm::ivec3 olo = glm::max(lo[ti], lo[tj]), ohi = glm::min(hi[ti], hi[tj]);
                        if (glm::any(glm::greaterThan(olo, ohi)) || cellKey(olo) != buckets[i].first) continue;
                        bool shared = false;
                        for (int u = 0; u < 3; ++u)
                            for (int w = 0; w < 3; ++w) shared |= f[u] == g[w];
                        if (shared) continue;
                        bool hit = false;
                        for (int u = 0; u < 3 && !hit; ++u)
                            hit = segmentCrossesTriangle(P[f[u]], P[f[(u + 1) % 3]], P[g.x], P[g.y], P[g.z]) ||
                                  segmentCrossesTriangle(P[g[u]], P[g[(u + 1) % 3]], P[f.x], P[f.y], P[f.z]);
                        if (!hit) continue;
                        for (uint32_t v : {f.x, f.y, f.z, g.x, g.y, g.z})
                            if (changed[v]) AtomicRef<uint8_t>(back[v]).store(1, std::memory_order_relaxed);
                        any.store(true, std::memory_order_relaxed);
                    }
        }, 64);
        if (!any) break;
        for (size_t v = 0; v < nv; ++v)
            if (back[v]) {
                m.positions[v] = round < 5 ? 0.5f * (m.positions[v] + original[v]) : original[v];
                if (round >= 5) changed[v] = 0;
            }
        seed.swap(back);
    }
    lap("crossings");
    if (report) {
        // The crease vertices whose result still faces away from the source: a crease finer than the voxels, with no
        // vertex inside it to bend the surface. Its width there: the gap between the closest points on its two most
        // opposed flanks.
        const float kLost = std::cos(glm::radians(30.0f));
        std::vector<float> width(crease.size(), -1.0f);
        poolFor(crease.size(), [&](size_t b, size_t e) {
            for (size_t i = b; i < e; ++i) {
                const glm::vec3 x = m.positions[crease[i]];
                const int32_t* tris = &trisOf[i * kMax];
                std::array<glm::vec3, kMax> q, n;
                int count = 0;
                for (int k = 0; k < kMax && tris[k] >= 0; ++k) {
                    const glm::uvec3 tri = source.triangles[size_t(tris[k])];
                    const glm::vec3 nt = glm::cross(S[tri.y] - S[tri.x], S[tri.z] - S[tri.x]);
                    if (!(glm::length(nt) > 0.0f)) continue;
                    q[size_t(count)] = closestOnTriangle(x, S[tri.x], S[tri.y], S[tri.z]), n[size_t(count)] = glm::normalize(nt);
                    ++count;
                }
                // Lost: the result there faces more than 30 degrees away from the source triangle under it.
                int under = -1;
                float best = 1e30f;
                for (int k = 0; k < count; ++k)
                    if (const float d = glm::length(q[size_t(k)] - x); d < best) best = d, under = k;
                glm::vec3 nr(0.0f);
                for (uint32_t u : m.vertexTriangles(crease[i])) { // (the faces still at it: a diagonal may have moved)
                    const glm::uvec3 f = m.triangles[u];
                    if (f.x == crease[i] || f.y == crease[i] || f.z == crease[i]) nr += faceNormal(m.positions, u);
                }
                if (under < 0 || !(glm::length(nr) > 0.0f) || glm::dot(glm::normalize(nr), n[size_t(under)]) >= kLost) continue;
                float flanks = 1.0f;
                const float w = measureCrease(i, x, nr, flanks);
                if (w < 0.0f) continue;
                width[i] = w;
            }
        }, 256);
        std::vector<float> fine;
        for (float w : width)
            if (w >= 0.0f) fine.push_back(w);
        report->creases = crease.size(), report->fine = fine.size();
        if (!fine.empty()) {
            std::nth_element(fine.begin(), fine.begin() + ptrdiff_t(fine.size() / 2), fine.end());
            report->fineWidth = fine[fine.size() / 2];
        }
        if (timing) std::fprintf(stderr, "    creases: %zu fine, width %.3f voxels\n", report->fine, report->fineWidth * inv);
    }
    return rediagonalised;
}

bool voxelCreaseSnap() { return g_creaseSnap; }

bool snapVoxelCreases(Mesh& m, const Mesh& source, const glm::vec3& origin, float cell, const glm::ivec3& dims,
                      const std::function<int32_t(const glm::ivec3&)>& nearestAt, CreaseReport* report) {
    VoxelGrid grid; // (the layout only: no values)
    grid.origin = origin, grid.cell = cell, grid.dims = dims;
    return snapCreases(m, source, grid, nearestAt, report);
}

// Closest point to `p` on triangle abc (Ericson, Real-Time Collision Detection 5.1.5).
glm::vec3 closestOnTriangle(const glm::vec3& p, const glm::vec3& a, const glm::vec3& b, const glm::vec3& c) {
    const glm::vec3 ab = b - a, ac = c - a, ap = p - a;
    const float d1 = glm::dot(ab, ap), d2 = glm::dot(ac, ap);
    if (d1 <= 0.0f && d2 <= 0.0f) return a;
    const glm::vec3 bp = p - b;
    const float d3 = glm::dot(ab, bp), d4 = glm::dot(ac, bp);
    if (d3 >= 0.0f && d4 <= d3) return b;
    const float vc = d1 * d4 - d3 * d2;
    if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f) return a + ab * (d1 / (d1 - d3));
    const glm::vec3 cp = p - c;
    const float d5 = glm::dot(ab, cp), d6 = glm::dot(ac, cp);
    if (d6 >= 0.0f && d5 <= d6) return c;
    const float vb = d5 * d2 - d1 * d6;
    if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f) return a + ac * (d2 / (d2 - d6));
    const float va = d3 * d6 - d5 * d4;
    if (va <= 0.0f && (d4 - d3) >= 0.0f && (d5 - d6) >= 0.0f) return b + (c - b) * ((d4 - d3) / ((d4 - d3) + (d5 - d6)));
    const float denom = 1.0f / (va + vb + vc);
    return a + ab * (vb * denom) + ac * (vc * denom);
}

float sideOfTriangle(const Mesh& mesh, uint32_t t, const glm::vec3& p) {
    const auto& P = mesh.positions;
    const glm::uvec3 tri = mesh.triangles[t];
    const glm::vec3 nrm = glm::cross(P[tri.y] - P[tri.x], P[tri.z] - P[tri.x]);
    return glm::dot(p - closestOnTriangle(p, P[tri.x], P[tri.y], P[tri.z]), nrm);
}

bool makeVoxelGrid(const AABB& box, int resolution, int padding, VoxelGrid& grid, size_t maxCells) {
    if (!box.valid()) return false;
    resolution = std::max(resolution, 4);
    const glm::vec3 ext = glm::max(box.extent(), glm::vec3(1e-6f));
    const float cell = std::max({ext.x, ext.y, ext.z}) / float(resolution);
    glm::ivec3 dims = glm::ivec3(glm::ceil(ext / cell)) + 1 + 2 * padding;
    if (double(dims.x) * dims.y * dims.z > double(maxCells)) return false;
    grid.dims = dims;
    grid.cell = cell;
    grid.origin = box.center() - glm::vec3(dims - 1) * 0.5f * cell;
    grid.values.assign(grid.size(), 0.0f);
    return true;
}

std::vector<uint8_t> solidifyShell(const VoxelGrid& shell, float threshold, int closeRadius, bool keepAllParts) {
    bool stopped = false;
    return solidifyShellPolled(shell, threshold, closeRadius, keepAllParts, {}, stopped);
}

std::vector<uint8_t> solidifyShellPolled(const VoxelGrid& shell, float threshold, int closeRadius, bool keepAllParts,
                                         const StagePoll& poll, bool& stopped) {
    const glm::ivec3 dims = shell.dims;
    const size_t n = shell.size();
    stopped = false;
    std::vector<uint8_t> wall(n);
    poolFor(n, [&](size_t b, size_t e) {
        for (size_t i = b; i < e; ++i) wall[i] = shell.values[i] > threshold ? 1 : 0;
    }, 1 << 16);
    if (!morph(wall, dims, closeRadius, true, [&] { return poll.at(1, 4); })) return stopped = true, std::vector<uint8_t>{};
    if (!poll.at(1, 4)) return stopped = true, std::vector<uint8_t>{};

    // Flood the empty space from the grid border; whatever it can't reach is inside.
    std::vector<uint8_t> outside(n, 0);
    std::vector<uint32_t> stack;
    for (int z = 0; z < dims.z; ++z)
        for (int y = 0; y < dims.y; ++y)
            for (int x = 0; x < dims.x; ++x) {
                const bool border = x == 0 || y == 0 || z == 0 || x == dims.x - 1 || y == dims.y - 1 || z == dims.z - 1;
                const size_t i = shell.index(x, y, z);
                if (border && !wall[i] && !outside[i]) {
                    outside[i] = 1;
                    stack.push_back(uint32_t(i));
                }
            }
    if (!floodFill(wall, 0, dims, stack, outside, [&] { return poll.at(2, 4); })) return stopped = true, std::vector<uint8_t>{};
    if (!poll.at(3, 4)) return stopped = true, std::vector<uint8_t>{};

    std::vector<uint8_t> solid(n);
    poolFor(n, [&](size_t b, size_t e) {
        for (size_t i = b; i < e; ++i) solid[i] = outside[i] ? 0 : 1;
    }, 1 << 16);
    if (!morph(solid, dims, closeRadius, false, [&] { return poll.at(3, 4); })) return stopped = true, std::vector<uint8_t>{}; // undo the gap-closing dilation

    if (!keepAllParts) {
        // Keep the largest connected piece (captures are full of floaters).
        std::vector<uint32_t> label(n, 0);
        const int64_t sy = dims.x, sz = int64_t(dims.x) * dims.y;
        uint32_t current = 0, bestLabel = 0;
        size_t bestCount = 0;
        for (size_t seed = 0; seed < n; ++seed) {
            if (!solid[seed] || label[seed]) continue;
            label[seed] = ++current;
            stack.assign(1, uint32_t(seed));
            size_t count = 0;
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
                    if (solid[j] && !label[j]) {
                        label[j] = current;
                        stack.push_back(j);
                    }
                }
            }
            if (count > bestCount) {
                bestCount = count;
                bestLabel = current;
            }
        }
        for (size_t i = 0; i < n; ++i) solid[i] = label[i] == bestLabel && bestLabel ? 1 : 0;
    }
    return solid;
}

Mesh meshFromSolid(const VoxelGrid& grid, const std::vector<uint8_t>& solid, int smoothIterations) {
    std::vector<float> f(solid.begin(), solid.end());
    for (int axis = 0; axis < 3; ++axis) blurAxis(f, grid.dims, axis);
    return meshFromField(grid, f, 0.5f, smoothIterations);
}

Mesh meshFromField(const VoxelGrid& grid, const std::vector<float>& f, float iso, int smoothIterations) {
    bool stopped = false;
    return meshFromFieldPolled(grid, f, iso, smoothIterations, {}, stopped);
}

Mesh meshFromFieldPolled(const VoxelGrid& grid, const std::vector<float>& f, float iso, int smoothIterations, const StagePoll& poll,
                         bool& stopped) {
    stopped = false;
    auto stop = [&] { return stopped = true, Mesh{}; };
    const glm::ivec3 dims = grid.dims;
    const glm::ivec3 cells = dims - 1;
    if (glm::any(glm::lessThan(cells, glm::ivec3(1)))) return {};
    const size_t cellSlice = size_t(cells.x) * size_t(cells.y);
    std::vector<int32_t> cellVertex(cellSlice * size_t(cells.z), -1);
    auto cellIndex = [&](int x, int y, int z) { return (size_t(z) * size_t(cells.y) + size_t(y)) * size_t(cells.x) + size_t(x); };

    // One vertex per cell that the surface passes through (surface nets). Slices run in parallel, each numbering its
    // own vertices; the slices' counts then give the global numbers, in the same z, y, x order as a serial scan.
    std::vector<std::vector<glm::vec3>> slicePositions(size_t(cells.z));
    poolFor(size_t(cells.z), [&](size_t zb, size_t ze) {
        for (int z = int(zb); z < int(ze); ++z) {
            std::vector<glm::vec3>& out = slicePositions[size_t(z)];
            for (int y = 0; y < cells.y; ++y)
                for (int x = 0; x < cells.x; ++x) {
                    std::array<float, 8> c;
                    int inside = 0;
                    for (int k = 0; k < 8; ++k) {
                        c[size_t(k)] = f[grid.index(x + (k & 1), y + ((k >> 1) & 1), z + ((k >> 2) & 1))];
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
                    cellVertex[cellIndex(x, y, z)] = int32_t(out.size());
                    out.push_back(grid.position(x, y, z) + sum / float(crossings) * grid.cell);
                }
        }
    }, 1);
    if (!poll.at(1, 5)) return stop();
    std::vector<size_t> firstVertex(size_t(cells.z) + 1, 0);
    for (size_t z = 0; z < size_t(cells.z); ++z) firstVertex[z + 1] = firstVertex[z] + slicePositions[z].size();
    Mesh m;
    m.positions.resize(firstVertex.back());
    poolFor(size_t(cells.z), [&](size_t zb, size_t ze) {
        for (size_t z = zb; z < ze; ++z) {
            std::copy(slicePositions[z].begin(), slicePositions[z].end(), m.positions.begin() + ptrdiff_t(firstVertex[z]));
            std::vector<glm::vec3>().swap(slicePositions[z]);
            if (firstVertex[z] == 0) continue;
            for (size_t i = z * cellSlice; i < (z + 1) * cellSlice; ++i)
                if (cellVertex[i] >= 0) cellVertex[i] += int32_t(firstVertex[z]);
        }
    }, 1);

    // One quad per grid edge with a sign change, joining the four cells around it; slices in parallel, joined in order.
    std::vector<std::vector<glm::uvec3>> sliceTriangles(size_t(dims.z));
    poolFor(size_t(dims.z), [&](size_t zb, size_t ze) {
        for (int z = int(zb); z < int(ze); ++z) {
            std::vector<glm::uvec3>& out = sliceTriangles[size_t(z)];
            for (int y = 0; y < dims.y; ++y)
                for (int x = 0; x < dims.x; ++x) {
                    const glm::ivec3 p(x, y, z);
                    const bool in0 = f[grid.index(x, y, z)] > iso;
                    for (int axis = 0; axis < 3; ++axis) {
                        glm::ivec3 q = p;
                        q[axis] += 1;
                        if (q[axis] >= dims[axis]) continue;
                        const bool in1 = f[grid.index(q.x, q.y, q.z)] > iso;
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
                            quad[size_t(k)] = cellVertex[cellIndex(c.x, c.y, c.z)];
                            ok &= quad[size_t(k)] >= 0;
                        }
                        if (!ok) continue;
                        if (!in0) std::swap(quad[1], quad[3]); // outward normal must point to the outside sample
                        const auto& P = m.positions;
                        const bool diag02 = glm::distance(P[size_t(quad[0])], P[size_t(quad[2])]) <=
                                            glm::distance(P[size_t(quad[1])], P[size_t(quad[3])]);
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
    }, 1);
    if (!poll.at(2, 5)) return stop();
    size_t triangleTotal = 0;
    for (const auto& t : sliceTriangles) triangleTotal += t.size();
    m.triangles.reserve(triangleTotal);
    for (auto& t : sliceTriangles) {
        m.triangles.insert(m.triangles.end(), t.begin(), t.end());
        std::vector<glm::uvec3>().swap(t);
    }
    // Surface nets makes quads: keep each one (Mesh::quadPartners): an even all-quad surface.
    for (uint32_t t = 0; t + 1 < m.triangles.size(); t += 2) m.pairQuad(t, t + 1);
    m.rebuildTopology();
    if (!poll.at(3, 5)) return stop();
    for (int it = 0; it < smoothIterations; ++it) {
        taubinSmooth(m, 1);
        if (!poll.at(3, 5)) return stop();
    }
    m.recomputeAllNormals();
    return m;
}

void rasterizeMeshShell(const Mesh& mesh, VoxelGrid& grid) {
    rasterizeMeshShellPolled(mesh, grid, {});
}

bool rasterizeMeshShellPolled(const Mesh& mesh, VoxelGrid& grid, const StagePoll& poll) {
    const auto& P = mesh.positions;
    // Triangles in parallel: every write stores the same 1, so the grid doesn't depend on the order (relaxed atomic
    // stores, as two triangles may mark the same cell). In rounds, Cancel asked between them (StagePoll).
    const size_t n = mesh.triangles.size();
    const size_t rounds = std::clamp<size_t>(n / 400000, 1, kStageRounds);
    for (size_t r = 0; r < rounds; ++r) {
    const size_t t0 = n * r / rounds, t1 = n * (r + 1) / rounds;
    poolFor(t1 - t0, [&](size_t tb, size_t te) {
        for (size_t ti = t0 + tb; ti < t0 + te; ++ti) {
            const glm::uvec3 t = mesh.triangles[ti];
            const glm::vec3 a = (P[t.x] - grid.origin) / grid.cell;
            const glm::vec3 b = (P[t.y] - grid.origin) / grid.cell;
            const glm::vec3 c = (P[t.z] - grid.origin) / grid.cell;
            const float longest = std::max({glm::distance(a, b), glm::distance(b, c), glm::distance(c, a)});
            const int steps = std::max(1, int(std::ceil(longest * 2.0f)));
            for (int i = 0; i <= steps; ++i)
                for (int j = 0; i + j <= steps; ++j) {
                    const float u = float(i) / float(steps), v = float(j) / float(steps);
                    const glm::ivec3 s(glm::round(a + (b - a) * u + (c - a) * v));
                    if (glm::any(glm::lessThan(s, glm::ivec3(0))) || glm::any(glm::greaterThanEqual(s, grid.dims))) continue;
                    AtomicRef<float>(grid.values[grid.index(s.x, s.y, s.z)]).store(1.0f, std::memory_order_relaxed);
                }
        }
    }, 8192);
    if (!poll.at(r + 1, rounds)) return false;
    }
    return true;
}

std::vector<float> insideDistanceField(const Mesh& mesh, const VoxelGrid& grid, const std::vector<uint8_t>& solid, float band) {
    bool stopped = false;
    return insideDistanceFieldPolled(mesh, grid, solid, band, {}, stopped);
}

std::vector<float> insideDistanceFieldPolled(const Mesh& mesh, const VoxelGrid& grid, const std::vector<uint8_t>& solid,
                                             float band, const StagePoll& poll, bool& stopped, std::vector<int32_t>* nearestOut,
                                             size_t* sealedOpened) {
    stopped = false;
    auto stop = [&] { return stopped = true, std::vector<float>{}; };
    static const bool timing = std::getenv("SF_VOXEL_TIMING") != nullptr;
    auto clock0 = std::chrono::steady_clock::now();
    auto lap = [&](const char* what) {
        if (!timing) return;
        const auto now = std::chrono::steady_clock::now();
        std::fprintf(stderr, "    distance field %-14s %6.1f ms\n", what, std::chrono::duration<double, std::milli>(now - clock0).count());
        clock0 = now;
    };
    // Exact distance (in cells) to the nearest triangle, with the nearest triangle's index, for every sample the
    // surface needs; the rest of the band keeps `band`. Ties go to the lowest triangle index, so the result doesn't
    // depend on the order of work.
    //
    // Which samples need it: the surface comes out of the sign changes between neighbouring samples (meshFromField
    // reads the two values of each straddling pair, 1 cell apart: the NEAR samples, within 1 sample of the other
    // state), and the sign of a solid sample is decided by its nearest triangle within 2 samples of the outside
    // (below: the FAR samples, solid and 2 from the outside). A straddling pair may then also include a solid sample
    // 3 from the outside, next to one the sign test turned: those few get their value after the sign pass. The rest
    // of the band used to be measured too, for nothing (half of this stage).
    //
    // How: triangles no bigger than a cell (tens per cell on a dense sculpt) are binned by the cell of their centre,
    // and each near and far sample gathers from the cells around it, nearest first, stopping once no cell can hold
    // anything nearer. The work goes with the number of those samples (about a million at resolution 200, whatever
    // the input) and the few dozen triangles around each, not with every triangle times every sample within 3 cells
    // of it (113 per triangle at 5M, 565M visits: this stage was 83% of a 5M remesh). Bigger triangles (a coarse or
    // mixed mesh) visit the samples around them as before, slab by slab in index order.
    const glm::ivec3 dims = grid.dims;
    const size_t n = grid.size();
    std::vector<float> dist2(n, band * band);
    std::vector<int32_t> nearest(n, -1);
    const auto& P = mesh.positions;
    const float inv = 1.0f / grid.cell;
    const uint32_t triangles = uint32_t(mesh.triangleCount());
    constexpr float kSmall = 1.0f; // (bounding radius in cells) at most: gathered; bigger: splatted
    constexpr uint8_t kNear = 1, kFar = 2, kFar3 = 4;
    auto local = [&](uint32_t t, glm::vec3& a, glm::vec3& b, glm::vec3& c, glm::ivec3& lo, glm::ivec3& hi) {
        const glm::uvec3 tri = mesh.triangles[t];
        a = (P[tri.x] - grid.origin) * inv, b = (P[tri.y] - grid.origin) * inv, c = (P[tri.z] - grid.origin) * inv;
        lo = glm::max(glm::ivec3(glm::floor(glm::min(a, glm::min(b, c)) - band)), glm::ivec3(0));
        hi = glm::min(glm::ivec3(glm::ceil(glm::max(a, glm::max(b, c)) + band)), dims - 1);
    };
    auto sphere = [](const glm::vec3& a, const glm::vec3& b, const glm::vec3& c, glm::vec3& centre, float& radius) {
        centre = (a + b + c) / 3.0f;
        radius = std::sqrt(std::max({glm::dot(a - centre, a - centre), glm::dot(b - centre, b - centre),
                                     glm::dot(c - centre, c - centre)})) + 1e-3f;
    };
    const glm::ivec3 cells = dims - 1;
    const size_t cellCount = size_t(cells.x) * size_t(cells.y) * size_t(cells.z);
    auto cellIndex = [&](const glm::ivec3& c) { return (size_t(c.z) * size_t(cells.y) + size_t(c.y)) * size_t(cells.x) + size_t(c.x); };
    // Whether the gather pays: its cost is about a quarter second whatever the input (the samples around the surface),
    // the splat's grows with the triangles (0.6 us each): a wash at 500k on a quiet laptop and a loss on a loaded one
    // (interleaved A/B 11-23), a clear win from 2M (1.3x) to 5M (1.5-2.7x), so it takes a million small triangles; and
    // its per-cell arrays must fit (16M cells: resolution 250). Otherwise every triangle splats, as before, no masks built.
    size_t smallCount = 0;
    {
        std::vector<uint32_t> blockSmall(((triangles + (1 << 16) - 1) >> 16) + 1, 0);
        poolFor(triangles, [&](size_t b0, size_t e0) {
            uint32_t count = 0;
            for (size_t t = b0; t < e0; ++t) {
                glm::vec3 a, b, c, centre;
                glm::ivec3 lo, hi;
                float radius;
                local(uint32_t(t), a, b, c, lo, hi);
                sphere(a, b, c, centre, radius);
                const glm::ivec3 cc(glm::floor(centre));
                count += lo.z <= hi.z && radius <= kSmall && glm::all(glm::greaterThanEqual(cc, glm::ivec3(0))) && glm::all(glm::lessThan(cc, cells));
            }
            blockSmall[b0 >> 16] = count;
        }, 1 << 16);
        for (uint32_t c : blockSmall) smallCount += c;
    }
    const bool useGather = smallCount >= 1000000 && cellCount <= (size_t(16) << 20);
    if (!useGather) smallCount = 0;
    if (timing) std::fprintf(stderr, "    distance field: %zu of %u triangles small; gather %s\n", smallCount, triangles, useGather ? "on" : "off");

    // Dilations of the outside by 1, 2 (kept: the sign pass) and 3 samples, and of the solid by 1. Without the gather
    // every sample within band is measured (the splat), as before: the masks are the sign pass's alone.
    std::vector<uint8_t> want(n), nearOutside(n), nearSolid(n);
    poolFor(n, [&](size_t b, size_t e) {
        for (size_t i = b; i < e; ++i) nearOutside[i] = solid[i] ? 0 : 1, nearSolid[i] = solid[i] ? 1 : 0;
    }, 1 << 16);
    if (!useGather) {
        std::vector<uint8_t>().swap(nearSolid);
        std::vector<uint8_t>().swap(want); // (empty: the splat measures every sample within band)
        if (!morph(nearOutside, dims, 2, true, [&] { return poll.at(2, 100); })) return stop(); // within 2 of the outside
    } else {
    if (!morph(nearOutside, dims, 1, true, [&] { return poll.at(1, 100); })) return stop(); // within 1 of the outside
    if (!morph(nearSolid, dims, 1, true, [&] { return poll.at(1, 100); })) return stop();   // within 1 of the solid
    poolFor(n, [&](size_t b, size_t e) {
        for (size_t i = b; i < e; ++i) want[i] = solid[i] ? (nearOutside[i] ? kNear : 0) : (nearSolid[i] ? kNear : 0);
    }, 1 << 16);
    std::vector<uint8_t>().swap(nearSolid);
    if (!morph(nearOutside, dims, 1, true, [&] { return poll.at(2, 100); })) return stop(); // within 2
    poolFor(n, [&](size_t b, size_t e) {
        for (size_t i = b; i < e; ++i)
            if (solid[i] && nearOutside[i] && !want[i]) want[i] = kFar;
    }, 1 << 16);
    {
        std::vector<uint8_t> far3 = nearOutside;
        if (!morph(far3, dims, 1, true, [&] { return poll.at(3, 100); })) return stop(); // within 3
        poolFor(n, [&](size_t b, size_t e) {
            for (size_t i = b; i < e; ++i)
                if (solid[i] && far3[i] && !want[i]) want[i] = kFar3;
        }, 1 << 16);
    }
    }
    lap("masks");
    if (!poll.at(4, 100)) return stop();

    // The tuple (distance, index) that both paths minimise.
    auto better = [&](float d2, uint32_t t, size_t i) {
        if (d2 < dist2[i] || (d2 == dist2[i] && int32_t(t) < nearest[i])) {
            dist2[i] = d2;
            nearest[i] = int32_t(t);
        }
    };

    // Bounding spheres; small triangles counted into the cell of their centre, big ones into z-slabs.
    struct Rec { // a small triangle, stored with the sphere test's inputs so the gather reads memory in order
        glm::vec3 centre;
        float radius;
        uint32_t t;
    };
    constexpr int kSlab = 4;
    const size_t slabs = size_t((dims.z + kSlab - 1) / kSlab);
    std::vector<uint32_t> cellOf(triangles); // (~0u: big, or none)
    std::vector<glm::ivec2> slabRange(triangles, glm::ivec2(1, 0)); // big triangles: the splat
    std::vector<std::atomic<uint32_t>> cellCountA(useGather ? cellCount + 1 : 1);
    for (auto& c : cellCountA) c.store(0, std::memory_order_relaxed);
    poolFor(triangles, [&](size_t b0, size_t e0) {
        for (size_t t = b0; t < e0; ++t) {
            glm::vec3 a, b, c, centre;
            glm::ivec3 lo, hi;
            float radius;
            local(uint32_t(t), a, b, c, lo, hi);
            sphere(a, b, c, centre, radius);
            cellOf[t] = ~0u;
            if (!(lo.z <= hi.z)) continue; // (NaN: none)
            const glm::ivec3 cc(glm::floor(centre));
            if (useGather && radius <= kSmall && glm::all(glm::greaterThanEqual(cc, glm::ivec3(0))) && glm::all(glm::lessThan(cc, cells))) {
                cellOf[t] = uint32_t(cellIndex(cc));
                cellCountA[cellOf[t] + 1].fetch_add(1, std::memory_order_relaxed);
            } else {
                slabRange[t] = glm::ivec2(lo.z / kSlab, hi.z / kSlab);
            }
        }
    }, 1 << 14);
    std::vector<uint32_t> cellStart(useGather ? cellCount + 1 : 1);
    for (size_t c = 0; c + 1 < cellStart.size(); ++c) cellStart[c + 1] = cellStart[c] + cellCountA[c + 1].load(std::memory_order_relaxed);
    smallCount = cellStart.back();
    std::vector<float> cellRadius(smallCount ? cellCount : 0, 0.0f); // the biggest radius in each cell
    float maxRadius = 0.0f;
    std::vector<Rec> recs(smallCount);
    if (useGather) {
        std::vector<std::atomic<uint32_t>>& cursor = cellCountA; // reused: the next free slot of each cell
        for (size_t c = 0; c < cellCount; ++c) cursor[c].store(cellStart[c], std::memory_order_relaxed);
        std::vector<float> blockMax(((triangles + (1 << 14) - 1) >> 14) + 1, 0.0f);
        poolFor(triangles, [&](size_t b0, size_t e0) {
            float bm = 0.0f;
            for (size_t t = b0; t < e0; ++t) {
                if (cellOf[t] == ~0u) continue;
                glm::vec3 a, b, c, centre;
                glm::ivec3 lo, hi;
                float radius;
                local(uint32_t(t), a, b, c, lo, hi);
                sphere(a, b, c, centre, radius);
                recs[cursor[cellOf[t]].fetch_add(1, std::memory_order_relaxed)] = {centre, radius, uint32_t(t)};
                AtomicRef<float> cr(cellRadius[cellOf[t]]);
                float old = cr.load(std::memory_order_relaxed);
                while (old < radius && !cr.compare_exchange_weak(old, radius, std::memory_order_relaxed)) {}
                bm = std::max(bm, radius);
            }
            blockMax[b0 >> 14] = bm;
        }, 1 << 14);
        for (float r : blockMax) maxRadius = std::max(maxRadius, r);
    }
    std::vector<uint32_t>().swap(cellOf);
    std::vector<std::atomic<uint32_t>>().swap(cellCountA);
    lap("binning");
    if (!poll.at(8, 100)) return stop();

    // 1. The splat: each big triangle visits the near and far samples within band of it, slab by slab (each slab its
    // own samples, its triangles in index order), in rounds with Cancel asked between them.
    std::vector<uint32_t> slabStart(slabs + 1, 0);
    for (const glm::ivec2& r : slabRange)
        for (int s = r.x; s <= r.y; ++s) ++slabStart[size_t(s) + 1];
    for (size_t s = 0; s < slabs; ++s) slabStart[s + 1] += slabStart[s];
    std::vector<uint32_t> slabTriangles(slabStart.back());
    {
        std::vector<uint32_t> cursor(slabStart.begin(), slabStart.end() - 1);
        for (uint32_t t = 0; t < triangles; ++t)
            for (int s = slabRange[t].x; s <= slabRange[t].y; ++s) slabTriangles[cursor[size_t(s)]++] = t;
    }
    std::vector<glm::ivec2>().swap(slabRange);
    const size_t bigRounds = std::clamp<size_t>(slabTriangles.size() / 400000, 1, kStageRounds);
    const size_t bigShare = smallCount ? 40 : 84; // of the 100 progress steps (no small triangles: no gather)
    for (size_t r = 0; r < bigRounds && !slabTriangles.empty(); ++r) {
        poolFor(slabs, [&](size_t sb, size_t se) {
            for (size_t s = sb; s < se; ++s) {
                const int z0 = int(s) * kSlab, z1 = std::min(dims.z, z0 + kSlab);
                const uint32_t count = slabStart[s + 1] - slabStart[s];
                const uint32_t k0 = slabStart[s] + uint32_t(size_t(count) * r / bigRounds);
                const uint32_t k1 = slabStart[s] + uint32_t(size_t(count) * (r + 1) / bigRounds);
                for (uint32_t k = k0; k < k1; ++k) {
                    const uint32_t t = slabTriangles[k];
                    glm::vec3 a, b, c, centre;
                    glm::ivec3 lo, hi;
                    float radius;
                    local(t, a, b, c, lo, hi);
                    sphere(a, b, c, centre, radius);
                    // Only samples within band of the triangle's bounding sphere can be within band of the triangle.
                    const float reach = radius + band;
                    const float reach2 = reach * reach;
                    for (int z = std::max(lo.z, z0); z <= std::min(hi.z, z1 - 1); ++z) {
                        const float dz = float(z) - centre.z, rz = reach2 - dz * dz;
                        if (rz < 0.0f) continue;
                        for (int y = lo.y; y <= hi.y; ++y) {
                            const float dy = float(y) - centre.y, ry = rz - dy * dy;
                            if (ry < 0.0f) continue;
                            const float half = std::sqrt(ry);
                            const int x0 = std::max(lo.x, int(std::ceil(centre.x - half)));
                            const int x1 = std::min(hi.x, int(std::floor(centre.x + half)));
                            for (int x = x0; x <= x1; ++x) {
                                const size_t i = grid.index(x, y, z);
                                if (useGather && !want[i]) continue;
                                const glm::vec3 p(x, y, z);
                                // Skip when even the sphere's near side is no closer than the best so far.
                                const float far = std::sqrt(dist2[i]) + radius;
                                if (glm::dot(p - centre, p - centre) > far * far) continue;
                                const glm::vec3 d = p - closestOnTriangle(p, a, b, c);
                                better(glm::dot(d, d), t, i);
                            }
                        }
                    }
                }
            }
        }, 1);
        if (!poll.at(8 + bigShare * (r + 1) / bigRounds, 100)) return stop();
    }
    std::vector<uint32_t>().swap(slabTriangles);
    std::vector<uint32_t>().swap(slabStart);
    lap("splat");

    // 2. The gather: each near and far sample collects the small triangles from the cells around it, nearest first. The
    // sample (x, y, z) is the corner of cell (x, y, z); cell (x + dx, ..) is at least dx (dx >= 0) or -dx - 1 (dx < 0)
    // away per axis.
    struct Off {
        glm::ivec3 d;
        float minDist;
    };
    std::vector<Off> offsets;
    std::atomic<uint64_t> statSamples{0}, statCells{0}, statSphere{0}, statClosest{0}; // (SF_VOXEL_TIMING)
    auto gather = [&](int x, int y, int z, size_t i, float bound) {
        const glm::vec3 p(x, y, z);
        float bestD = std::min(std::sqrt(dist2[i]), bound); // (the bound only prunes: a true upper bound of the distance)
        uint32_t nCells = 0, nSphere = 0, nClosest = 0;
        for (const Off& o : offsets) {
            if (o.minDist - maxRadius > bestD) break; // (nothing nearer in this or any later cell)
            const glm::ivec3 c(x + o.d.x, y + o.d.y, z + o.d.z);
            if (glm::any(glm::lessThan(c, glm::ivec3(0))) || glm::any(glm::greaterThanEqual(c, cells))) continue;
            const size_t ci = cellIndex(c);
            ++nCells;
            if (o.minDist - cellRadius[ci] > bestD) continue;
            for (uint32_t k = cellStart[ci]; k < cellStart[ci + 1]; ++k) {
                const Rec& rec = recs[k];
                const glm::vec3 q = p - rec.centre;
                const float far = bestD + rec.radius;
                ++nSphere;
                if (glm::dot(q, q) > far * far) continue;
                ++nClosest;
                const glm::uvec3 tri = mesh.triangles[rec.t];
                const glm::vec3 d = p - closestOnTriangle(p, (P[tri.x] - grid.origin) * inv, (P[tri.y] - grid.origin) * inv,
                                                          (P[tri.z] - grid.origin) * inv);
                const float d2 = glm::dot(d, d);
                if (d2 < dist2[i]) bestD = std::sqrt(d2);
                better(d2, rec.t, i);
            }
        }
        if (timing) statSamples.fetch_add(1, std::memory_order_relaxed), statCells.fetch_add(nCells, std::memory_order_relaxed),
            statSphere.fetch_add(nSphere, std::memory_order_relaxed), statClosest.fetch_add(nClosest, std::memory_order_relaxed);
    };
    auto statLap = [&](const char* what) {
        if (!timing) return;
        std::fprintf(stderr, "    gather %-12s %8llu samples: %.1f cells, %.1f sphere tests, %.1f closest-point tests each\n", what,
                     (unsigned long long)statSamples.load(), double(statCells.load()) / std::max<double>(1, double(statSamples.load())),
                     double(statSphere.load()) / std::max<double>(1, double(statSamples.load())),
                     double(statClosest.load()) / std::max<double>(1, double(statSamples.load())));
        statSamples = 0, statCells = 0, statSphere = 0, statClosest = 0;
        lap(what);
    };
    if (smallCount) {
        const int K = int(std::ceil(band + maxRadius)) + 1;
        for (int dz = -K; dz < K; ++dz)
            for (int dy = -K; dy < K; ++dy)
                for (int dx = -K; dx < K; ++dx) {
                    auto axis = [](int d) { return float(d >= 0 ? d : -d - 1); };
                    const glm::vec3 m(axis(dx), axis(dy), axis(dz));
                    const float md = std::sqrt(glm::dot(m, m));
                    if (md - maxRadius > band) continue; // (can't hold anything within band)
                    offsets.push_back({{dx, dy, dz}, md});
                }
        std::stable_sort(offsets.begin(), offsets.end(), [](const Off& a, const Off& b) { return a.minDist < b.minDist; });
        const size_t sliceRounds = std::clamp<size_t>(size_t(dims.z) / 48, 1, 8); // (a slice round is tens of ms)
        for (size_t r = 0; r < sliceRounds; ++r) {
            const size_t za = size_t(dims.z) * r / sliceRounds, zb = size_t(dims.z) * (r + 1) / sliceRounds;
            poolFor(zb - za, [&](size_t b0, size_t e0) {
                for (int z = int(za + b0); z < int(za + e0); ++z)
                    for (int y = 0; y < dims.y; ++y)
                        for (int x = 0; x < dims.x; ++x) {
                            const size_t i = grid.index(x, y, z);
                            if (want[i] & kNear) gather(x, y, z, i, band);
                        }
            }, 1);
            if (!poll.at(8 + bigShare + (84 - bigShare) * (r + 1) / (2 * sliceRounds), 100)) return stop();
        }
        statLap("near");
        // Far samples, bounded by their measured neighbours: the surface is within d(q) + 1 of p when it is within d(q)
        // of a neighbour q one cell away, so the scan stops that much sooner (a far sample found its triangle 2-3 cells
        // out, through hundreds of empty cells).
        const size_t X1 = size_t(dims.x), S1 = X1 * size_t(dims.y);
        for (size_t r = 0; r < sliceRounds; ++r) {
            const size_t za = size_t(dims.z) * r / sliceRounds, zb = size_t(dims.z) * (r + 1) / sliceRounds;
            poolFor(zb - za, [&](size_t b0, size_t e0) {
                for (int z = int(za + b0); z < int(za + e0); ++z)
                    for (int y = 0; y < dims.y; ++y)
                        for (int x = 0; x < dims.x; ++x) {
                            const size_t i = grid.index(x, y, z);
                            if (want[i] != kFar) continue;
                            float bound = band;
                            auto look = [&](bool inRange, size_t j) {
                                if (inRange && (want[j] & kNear)) bound = std::min(bound, std::sqrt(dist2[j]) + 1.0f);
                            };
                            look(x > 0, i - 1), look(x + 1 < dims.x, i + 1), look(y > 0, i - X1);
                            look(y + 1 < dims.y, i + X1), look(z > 0, i - S1), look(z + 1 < dims.z, i + S1);
                            gather(x, y, z, i, bound);
                        }
            }, 1);
            if (!poll.at(8 + bigShare + (84 - bigShare) * (sliceRounds + r + 1) / (2 * sliceRounds), 100)) return stop();
        }
        statLap("far");
        if (timing) std::fprintf(stderr, "    gather offsets %zu (K %d, max radius %.2f)\n", offsets.size(), K, maxRadius);
    }
    lap("gather");

    // Sign: the flood fill is certain about the outside and about the deep inside; next to the surface (nearOutside:
    // within 2 samples, any axis, of an outside sample) the nearest triangle's side decides, which keeps sub-voxel
    // detail instead of voxel stair-steps.
    // A crease narrower than about 4 samples is sealed by the wall closing (the walls are thickened by a sample each and
    // the flood never gets in): its air counts as solid, and the sign of the nearest triangle only reached 2 samples down
    // from its mouth, so the rest filled up and the surface between the two came out as speckled sheets (gh#240). The
    // nearest triangle decides for every solid sample within kSealed of the surface too. A surface buried inside another
    // part must still count as buried there: its front side is solid, as a sealed gap is; the difference is that a gap's
    // far wall is within kHug of the probe and has the sample on its outer side as well.
    constexpr float kSealed = 2.5f, kHug = 2.0f, kBack = -0.3f, kProbe = 2.5f, kGap = 1.25f;
    // A crease's far wall: the surface nearest the probe (within kHug cells) has this sample on its outer side too, faces back at
    // this sample's own surface (`n`: its unit normal; normals more than ~107 degrees apart: neighbouring triangles of one smooth
    // surface, a pole's fan, a terrace step are never a crease's wall), and the gap between the two walls (kProbe along the normal
    // from the near wall, less or plus the probe's distance to the far wall, by its side of it) is at least kGap cells: a closed
    // slit (the lips) is narrower than the grid can hold, and opening it only chips its edge. `pw`: the probe's position.
    auto outsideFarWall = [&](size_t probeIndex, const glm::vec3& p, const glm::vec3& pw, const glm::vec3& n) {
        if (nearest[probeIndex] < 0 || dist2[probeIndex] >= kHug * kHug) return false;
        const glm::uvec3 t = mesh.triangles[size_t(nearest[probeIndex])];
        const glm::vec3 m = glm::cross(P[t.y] - P[t.x], P[t.z] - P[t.x]);
        const float l = glm::length(m);
        if (!(l > 0.0f) || glm::dot(m / l, n) >= kBack || sideOfTriangle(mesh, uint32_t(nearest[probeIndex]), p) <= 0.0f) return false;
        const glm::vec3 d = pw - closestOnTriangle(pw, P[t.x], P[t.y], P[t.z]);
        const float e = glm::length(d) / grid.cell;
        const float gap = glm::dot(d, m) > 0.0f ? kProbe + e : kProbe - e; // (the probe in the gap, or past the far wall)
        return gap >= kGap;
    };
    std::vector<uint8_t> inside(n);
    std::atomic<size_t> opened{0};
    poolFor(size_t(dims.z), [&](size_t zb, size_t ze) {
        size_t openedHere = 0;
        for (int z = int(zb); z < int(ze); ++z)
            for (int y = 0; y < dims.y; ++y)
                for (int x = 0; x < dims.x; ++x) {
                    const size_t i = grid.index(x, y, z);
                    const bool sealed = solid[i] && !nearOutside[i] && nearest[i] >= 0 && dist2[i] <= kSealed * kSealed;
                    if (!solid[i]) inside[i] = 0;
                    else if (!(nearOutside[i] || sealed) || nearest[i] < 0) inside[i] = 1;
                    else {
                        const glm::uvec3 tri = mesh.triangles[size_t(nearest[i])];
                        const glm::vec3 nrm = glm::cross(P[tri.y] - P[tri.x], P[tri.z] - P[tri.x]);
                        const glm::vec3 p = grid.position(x, y, z);
                        const glm::vec3 onSurface = closestOnTriangle(p, P[tri.x], P[tri.y], P[tri.z]);
                        // A surface buried inside another part (solid just outside it too) doesn't bound the solid.
                        const float len = glm::length(nrm);
                        const glm::vec3 probeAt = onSurface + nrm * (kProbe * grid.cell / std::max(len, 1e-20f));
                        const glm::ivec3 probe(glm::round((probeAt - grid.origin) / grid.cell));
                        const bool buried = len > 0.0f && glm::all(glm::greaterThanEqual(probe, glm::ivec3(0))) &&
                                            glm::all(glm::lessThan(probe, dims)) && solid[grid.index(probe.x, probe.y, probe.z)] &&
                                            !outsideFarWall(grid.index(probe.x, probe.y, probe.z), p, probeAt, nrm / len);
                        inside[i] = buried || glm::dot(p - onSurface, nrm) < 0.0f;
                        openedHere += sealed && !inside[i];
                    }
                }
        opened.fetch_add(openedHere, std::memory_order_relaxed);
    }, 1);
    if (sealedOpened) *sealedOpened = opened.load();
    std::vector<uint8_t>().swap(nearOutside);
    // The late gather: a solid sample 3 from the outside next to one the sign test turned may be half of a straddling
    // pair, so it needs its value too (the small triangles' part; the big ones splatted it already). Rare.
    if (smallCount) {
        const size_t X0 = size_t(dims.x), S0 = X0 * size_t(dims.y);
        poolFor(size_t(dims.z), [&](size_t zb, size_t ze) {
            for (int z = int(zb); z < int(ze); ++z)
                for (int y = 0; y < dims.y; ++y)
                    for (int x = 0; x < dims.x; ++x) {
                        const size_t i = grid.index(x, y, z);
                        if (!(want[i] & kFar3)) continue;
                        auto turned = [&](bool inRange, size_t j) { return inRange && solid[j] && !inside[j]; };
                        if (turned(x > 0, i - 1) || turned(x + 1 < dims.x, i + 1) || turned(y > 0, i - X0) ||
                            turned(y + 1 < dims.y, i + X0) || turned(z > 0, i - S0) || turned(z + 1 < dims.z, i + S0))
                            gather(x, y, z, i, band);
                    }
        }, 1);
    }
    std::vector<Rec>().swap(recs);
    std::vector<uint32_t>().swap(cellStart);
    std::vector<float>().swap(cellRadius);
    std::vector<uint8_t>().swap(want);
    lap("sign + late");
    if (!poll.at(94, 100)) return stop();
    // Surfaces buried inside the solid (overlapping parts) make small "outside" pockets: anything outside that
    // can't reach the real outside is inside, so merged parts stay one closed surface. The flood starts from the
    // outside samples that touch an outside-signed solid sample (the rest of the outside has nowhere to go).
    std::vector<uint8_t> reached(n);
    poolFor(n, [&](size_t b, size_t e) {
        for (size_t i = b; i < e; ++i) reached[i] = solid[i] ? 0 : 1;
    }, 1 << 16);
    const size_t X = size_t(dims.x), S = X * size_t(dims.y);
    std::vector<std::vector<uint32_t>> sliceSeeds(size_t(dims.z));
    poolFor(size_t(dims.z), [&](size_t zb, size_t ze) {
        for (int z = int(zb); z < int(ze); ++z)
            for (int y = 0; y < dims.y; ++y)
                for (int x = 0; x < dims.x; ++x) {
                    const size_t i = grid.index(x, y, z);
                    if (solid[i]) continue;
                    auto pocket = [&](bool inRange, size_t j) { return inRange && solid[j] && !inside[j]; };
                    if (pocket(x > 0, i - 1) || pocket(x + 1 < dims.x, i + 1) || pocket(y > 0, i - X) ||
                        pocket(y + 1 < dims.y, i + X) || pocket(z > 0, i - S) || pocket(z + 1 < dims.z, i + S))
                        sliceSeeds[size_t(z)].push_back(uint32_t(i));
                }
    }, 1);
    std::vector<uint32_t> stack;
    for (const auto& seeds : sliceSeeds) stack.insert(stack.end(), seeds.begin(), seeds.end());
    if (!floodFill(inside, 0, dims, stack, reached, [&] { return poll.at(96, 100); })) return stop();
    std::vector<float> f(n);
    poolFor(n, [&](size_t b, size_t e) {
        for (size_t i = b; i < e; ++i) {
            const float d = std::sqrt(dist2[i]);
            f[i] = inside[i] || !reached[i] ? d : -d;
        }
    }, 1 << 16);
    lap("pockets + f");
    if (nearestOut) *nearestOut = std::move(nearest);
    return f;
}

namespace {
int g_sparseAbove = kVoxelDenseMax;
} // namespace

void setVoxelSparseAbove(int resolution) { g_sparseAbove = resolution; }

int voxelResolutionFromSlider(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    const float a = 2.0f / 3.0f, b = 5.0f / 6.0f;
    if (t <= a) return int(std::lround(32.0f + (t / a) * float(kVoxelDenseMax - 32)));
    if (t <= b) return std::clamp(int(std::lround((512.0f + (t - a) / (b - a) * 1536.0f) / 16.0f)) * 16, 528, 2048);
    return std::clamp(int(std::lround((2048.0f + (t - b) / (1.0f - b) * 2048.0f) / 64.0f)) * 64, 2112, kVoxelResolutionMax);
}

float voxelSliderFromResolution(int r) {
    r = std::clamp(r, kVoxelResolutionMin, kVoxelResolutionMax);
    const float a = 2.0f / 3.0f, b = 5.0f / 6.0f;
    if (r <= kVoxelDenseMax) return float(r - 32) / float(kVoxelDenseMax - 32) * a;
    if (r <= 2048) return a + float(r - 512) / 1536.0f * (b - a);
    return b + float(r - 2048) / 2048.0f * (1.0f - b);
}

double surfaceArea(const Mesh& mesh) {
    std::vector<double> part((mesh.triangleCount() + 65535) / 65536, 0.0);
    poolFor(mesh.triangleCount(), [&](size_t b, size_t e) {
        double sum = 0.0;
        for (size_t t = b; t < e; ++t) {
            const glm::uvec3 tri = mesh.triangles[t];
            const glm::vec3& a = mesh.positions[tri.x];
            const float area = glm::length(glm::cross(mesh.positions[tri.y] - a, mesh.positions[tri.z] - a));
            if (std::isfinite(area)) sum += 0.5 * double(area);
        }
        part[b / 65536] = sum;
    }, 65536);
    double total = 0.0;
    for (double v : part) total += v;
    return total;
}

VoxelRemeshEstimate estimateVoxelRemesh(const Mesh& mesh, int resolution, double area) {
    VoxelRemeshEstimate e;
    const AABB box = mesh.bounds();
    if (!box.valid() || mesh.triangleCount() == 0) return e;
    resolution = std::clamp(resolution, kVoxelResolutionMin, kVoxelResolutionMax);
    if (area < 0.0) area = surfaceArea(mesh);
    const glm::vec3 ext = glm::max(box.extent(), glm::vec3(1e-6f));
    const double cell = double(std::max({ext.x, ext.y, ext.z})) / resolution;
    // Surface nets: a quad (2 triangles) per grid edge the surface crosses, about 1.5 edges per cell of area. Detail
    // finer than a cell counts in the area but not in the result, so a dense scan comes out somewhat lower.
    const double cellsOfArea = area / (cell * cell);
    e.triangles = size_t(3.0 * cellsOfArea);
    // The grid: dense up to 512 (about 18 bytes a sample at its peak); above it the bricks near the surface (about 26
    // samples deep per cell of area, 15 bytes each with the nearest triangles the crease pass keeps). Then the result in
    // the app (as the other big commands count it).
    double grid;
    if (resolution <= g_sparseAbove) {
        const glm::dvec3 dims = glm::ceil(glm::dvec3(ext) / cell) + 9.0;
        grid = dims.x * dims.y * dims.z * 18.0;
    } else {
        const glm::dvec3 bricks = glm::ceil((glm::ceil(glm::dvec3(ext) / cell) + 9.0) / 8.0);
        grid = cellsOfArea * 26.0 * 15.0 + bricks.x * bricks.y * bricks.z * 6.0;
    }
    e.bytes = size_t(grid + double(e.triangles) * kVoxelResultBytesPerTriangle);
    return e;
}

namespace {
// The Resolution hint (gh#240): when the crease pass lost at least 64 crease vertices, and 1 in 20 of them (not one stray
// crease: most of a head's are eyelids, lips and ears, which come through), the Resolution that makes those creases'
// typical width two voxels, rounded up to the slider's step, if it fits the free memory; else none.
// Only creases one to two voxels wide (median): thinner ones on a real sculpt are mostly the facets of a decimated
// surface, which keep turning up finer at every Resolution, so no number would be honest for them.
void noteFineCreases(const Mesh& mesh, int resolution, const CreaseReport& r, VoxelRemeshNotes* notes) {
    if (!notes || r.fine < 64 || r.fine * 20 < r.creases || !(r.fineWidth > 0.0f)) return;
    const glm::vec3 ext = mesh.bounds().extent();
    const double longest = double(std::max({ext.x, ext.y, ext.z}));
    const double voxels = double(r.fineWidth) * double(resolution) / longest;
    if (voxels < 1.0 || voxels >= 2.0) return;
    double want = std::ceil(2.0 * longest / double(r.fineWidth));
    const int stepSize = want > 2048 ? 64 : want > 512 ? 16 : 8;
    want = std::ceil(want / stepSize) * stepSize;
    if (want <= double(resolution) || want > double(kVoxelResolutionMax)) return;
    const VoxelRemeshEstimate e = estimateVoxelRemesh(mesh, int(want));
    if (classifyMemory(e.bytes, memoryLimits()) != MemoryTier::Fits) return;
    notes->creaseResolution = int(want);
    notes->creaseTriangles = e.triangles;
}

Mesh voxelRemeshImpl(const Mesh& source, int resolution, std::string* error, const std::function<bool(float)>& progress,
                     bool allowSwap, bool keepGroups, VoxelRemeshNotes* notes) {
    VoxelGrid grid;
    // An open Part (a starter head's neck, an extracted shell): the flood fill would pour in through the opening and
    // leave only a thin, holed shell. Its borders are capped first, on a copy of just the positions and triangles.
    Mesh capped;
    const bool open = hasOpenEdges(source);
    if (open) {
        capped.positions = source.positions;
        capped.triangles = source.triangles;
        CloseHolesOptions o;
        o.refine = false; // (the voxels smooth the cap; no new vertices to carry attributes to)
        o.newGroup = false;
        CloseHolesStats stats;
        std::string ignored;
        closeHoles(capped, o, stats, ignored);
    }
    const Mesh& mesh = open ? capped : source;
    auto step = [&](float f) {
        if (!progress || progress(f)) return true;
        if (error) *error = "Voxel Remesh cancelled";
        return false;
    };
    resolution = std::min(resolution, kVoxelResolutionMax);
    if (mesh.triangleCount() > 0) {
        // One check for every Resolution, up front, from the surface area (estimateVoxelRemesh): the grid and the surface it
        // makes. Past the free memory the remesh doesn't start unless the person agreed to swap (the panel asks; the brick
        // count is checked again once known).
        const VoxelRemeshEstimate e = estimateVoxelRemesh(mesh, resolution);
        const MemoryLimits limits = memoryLimits();
        const MemoryTier tier = classifyMemory(e.bytes, limits);
        if (tier == MemoryTier::TooBig) {
            if (error) *error = voxelRemeshTooBigMessage(mesh, resolution, limits);
            return {};
        }
        if (tier == MemoryTier::NeedsSwap && !allowSwap) {
            if (error)
                *error = "Voxel Remesh at " + std::to_string(resolution) + " needs ~" + memoryText(e.bytes) + "; " + memoryText(limits.available) +
                         " is free. Tool > Remesh asks to use swap (slow). Lower the Resolution to avoid it";
            return {};
        }
    }
    CreaseReport creases;
    if (resolution > g_sparseAbove && mesh.triangleCount() > 0) {
        // The brick grid (VoxelSparse.cpp): the same steps, memory going with the surface instead of the volume.
        std::string why;
        Mesh out;
        try {
            out = sparseVoxelSurface(mesh, resolution, 4, 3.0f, open, &why, step, allowSwap, &creases);
        } catch (const std::bad_alloc&) {
            why = "Voxel Remesh at " + std::to_string(resolution) + " ran out of memory; the Part is unchanged. Lower the Resolution";
        }
        if (out.triangleCount() == 0) {
            if (error && !why.empty()) *error = why;
            else if (error && error->empty()) *error = "Remesh produced no surface";
            return {};
        }
        if (open || creases.sealedOpened > 0) dropSpecks(out, 64);
        noteFineCreases(mesh, resolution, creases, notes);
        if (!step(0.9f)) return {};
        transferVertexAttributes(source, out);
        if (keepGroups) transferFaceGroups(source, out);
        step(1.0f);
        return out;
    }
    if (mesh.triangleCount() == 0 || !makeVoxelGrid(mesh.bounds(), resolution, 4, grid)) {
        if (error) *error = mesh.triangleCount() == 0 ? "The mesh is empty" : "Resolution too high for this mesh";
        return {};
    }
    const StepFn stepFn = step;
    bool stopped = false;
    if (!rasterizeMeshShellPolled(mesh, grid, {&stepFn, 0.02f, 0.1f})) return {};
    if (!step(0.1f)) return {};
    std::vector<uint8_t> solid = solidifyShellPolled(grid, 0.5f, 1, true, {&stepFn, 0.1f, 0.25f}, stopped);
    if (stopped) return {};
    std::vector<float>().swap(grid.values);
    if (!step(0.25f)) return {};
    if (open) {
        // Still open after capping (a border cut by non-manifold or flipped edges, a cap that folds): the outside
        // reached the inside and the solid is little more than the shell. Nothing is better than a surface full of holes.
        double volume = 0.0;
        for (const glm::uvec3& t : mesh.triangles)
            volume += glm::dot(glm::dvec3(mesh.positions[t.x]),
                               glm::cross(glm::dvec3(mesh.positions[t.y]), glm::dvec3(mesh.positions[t.z])));
        const double cells = std::abs(volume) / 6.0 / (double(grid.cell) * grid.cell * grid.cell);
        const size_t inside = size_t(std::count(solid.begin(), solid.end(), uint8_t(1)));
        if (cells > 64.0 && double(inside) < 0.5 * cells) {
            if (error)
                *error = "Remesh: the Part has open borders that couldn't be capped, so it isn't a closed solid and the "
                         "remesh would be full of holes; the Part is unchanged. Close it first (Tool > Geometry > Fill Holes)";
            return {};
        }
    }
    // Mesh the signed distance to the original surface (positive inside), not the blocky occupancy: the result
    // lands on the original surface with no voxel terracing.
    std::vector<int32_t> nearest;
    size_t sealedOpened = 0;
    const std::vector<float> field = insideDistanceFieldPolled(mesh, grid, solid, 3.0f, {&stepFn, 0.25f, 0.6f}, stopped,
                                                               g_creaseSnap ? &nearest : nullptr, &sealedOpened);
    if (stopped) return {};
    std::vector<uint8_t>().swap(solid);
    if (!step(0.6f)) return {};
    Mesh out = meshFromFieldPolled(grid, field, 0.0f, 2, {&stepFn, 0.6f, 0.85f}, stopped);
    if (stopped) return {};
    if (g_creaseSnap) {
        if (snapCreases(out, mesh, grid, [&](const glm::ivec3& s) { return nearest[grid.index(s.x, s.y, s.z)]; }, &creases))
            out.rebuildTopology();
        out.recomputeAllNormals();
        noteFineCreases(mesh, resolution, creases, notes);
    }
    // (a few voxels: never detail at a remesh resolution. Where the sign test opened a sealed crease, the tip of a groove
    // under a voxel wide can leave one-voxel pieces of its own.)
    if (open || sealedOpened > 0) dropSpecks(out, 64);
    if (out.triangleCount() == 0 && error) *error = "Remesh produced no surface";
    if (!step(0.9f)) return {};
    // Mask and vertex paint follow the nearest original vertex; hidden flags and UVs don't survive a rebuild (everything
    // becomes visible group 0), and face groups only with Keep Face Groups (the nearest original triangle's).
    // The original's quads don't either: the new surface is all quads of its own.
    transferVertexAttributes(source, out);
    if (keepGroups) transferFaceGroups(source, out);
    step(1.0f);
    return out;
}
} // namespace

void transferFaceGroups(const Mesh& source, Mesh& dst) {
    if (!source.hasFaceGroups() || source.facegroups.size() != source.triangleCount() || dst.triangleCount() == 0 ||
        source.triangleCount() == 0)
        return;
    TriangleBVH bvh;
    bvh.build(source.positions, source.triangles);
    if (bvh.empty()) return;
    // Polygons: a quad pair counts once (its first triangle leads; the partner follows it).
    const size_t nt = dst.triangleCount();
    std::vector<uint32_t> lead(nt); // per triangle: itself, or the quad partner that leads it
    std::vector<uint32_t> polys;    // the leading triangles
    polys.reserve(nt);
    const size_t nq = std::min(dst.quadPartners.size(), nt);
    for (uint32_t t = 0; t < nt; ++t) {
        const uint32_t u = t < nq ? dst.quadPartners[t] : Mesh::kNoQuad;
        if (u < nq && u < t && dst.quadPartners[u] == t) {
            lead[t] = u;
            continue;
        }
        lead[t] = t;
        polys.push_back(t);
    }
    auto centre = [&](uint32_t t) {
        const glm::uvec3 a = dst.triangles[t];
        glm::vec3 c = dst.positions[a.x] + dst.positions[a.y] + dst.positions[a.z];
        const uint32_t u = t < nq ? dst.quadPartners[t] : Mesh::kNoQuad;
        if (u < nt && lead[u] == t) { // the quad's four corners once each
            const glm::uvec3 b = dst.triangles[u];
            for (int k = 0; k < 3; ++k)
                if (b[k] != a.x && b[k] != a.y && b[k] != a.z) return (c + dst.positions[b[k]]) * 0.25f;
        }
        return c / 3.0f;
    };
    const float reach = glm::length(source.bounds().extent()) + glm::length(dst.bounds().extent());
    std::vector<uint32_t>& groups = dst.ensureFaceGroups(0u);
    poolFor(polys.size(), [&](size_t b, size_t e) {
        for (size_t i = b; i < e; ++i) {
            const uint32_t t = polys[i];
            SurfaceHit hit;
            groups[t] = bvh.closestPoint(centre(t), reach, hit) ? source.facegroups[hit.prim] : 0u;
        }
    });
    // Edge neighbours of each polygon (across its outer edges; a quad's diagonal joins its own halves): every edge with
    // its polygon, sorted, so equal edges sit together (no hash map: a 5M-triangle result would be 15M node allocations).
    struct EdgeRef {
        uint64_t key;
        uint32_t poly;
    };
    std::vector<EdgeRef> edges;
    edges.reserve(nt * 3);
    for (uint32_t t = 0; t < nt; ++t) {
        const glm::uvec3 tri = dst.triangles[t];
        for (int c = 0; c < 3; ++c) {
            const uint32_t a = std::min(tri[c], tri[(c + 1) % 3]), b = std::max(tri[c], tri[(c + 1) % 3]);
            edges.push_back({(uint64_t(a) << 32) | b, lead[t]});
        }
    }
    std::sort(edges.begin(), edges.end(), [](const EdgeRef& x, const EdgeRef& y) { return x.key != y.key ? x.key < y.key : x.poly < y.poly; });
    std::vector<std::array<uint32_t, 4>> nb(nt, {Mesh::kNoQuad, Mesh::kNoQuad, Mesh::kNoQuad, Mesh::kNoQuad});
    std::vector<uint8_t> nbCount(nt, 0);
    auto add = [&](uint32_t p, uint32_t q) { // (once per pair: two quads can share two edges around a valence-2 vertex)
        for (uint8_t k = 0; k < nbCount[p]; ++k)
            if (nb[p][k] == q) return;
        if (nbCount[p] < 4) nb[p][nbCount[p]++] = q;
    };
    for (size_t i = 0; i < edges.size();) {
        size_t j = i;
        while (j < edges.size() && edges[j].key == edges[i].key) ++j;
        for (size_t x = i; x < j; ++x)
            for (size_t y = x + 1; y < j; ++y)
                if (edges[x].poly != edges[y].poly) add(edges[x].poly, edges[y].poly), add(edges[y].poly, edges[x].poly);
        i = j;
    }
    std::vector<EdgeRef>().swap(edges);
    // Cleanup: a polygon whose group differs from every neighbour's, with the neighbours agreeing, takes theirs. Two
    // passes catch a 2-polygon speck; a real sliver of a group (a ring one polygon wide) has neighbours of its own group.
    for (int pass = 0; pass < 2; ++pass) {
        std::vector<std::pair<uint32_t, uint32_t>> changes;
        for (uint32_t t : polys) {
            const uint8_t n = nbCount[t];
            if (n < 3) continue;
            const uint32_t g = groups[t];
            uint32_t other = groups[nb[t][0]];
            bool allOther = other != g;
            for (uint8_t k = 1; k < n && allOther; ++k) allOther = groups[nb[t][k]] == other;
            if (allOther) changes.emplace_back(t, other);
        }
        if (changes.empty()) break;
        for (const auto& [t, g] : changes) groups[t] = g;
    }
    for (uint32_t t = 0; t < nt; ++t)
        if (lead[t] != t) groups[t] = groups[lead[t]];
}

std::string voxelRemeshNote(const VoxelRemeshNotes& notes) {
    if (notes.creaseResolution <= 0) return {};
    const double t = double(notes.creaseTriangles);
    char tris[32];
    if (t >= 1e6) std::snprintf(tris, sizeof tris, "%.1fM", t / 1e6);
    else std::snprintf(tris, sizeof tris, "%.0fk", std::max(1.0, t / 1e3));
    return "Resolution " + std::to_string(notes.creaseResolution) + " keeps fine creases (about " + tris + " triangles). ";
}

Mesh voxelRemesh(const Mesh& source, int resolution, std::string* error, const std::function<bool(float)>& progress,
                 bool allowSwap, bool keepGroups, VoxelRemeshNotes* notes) {
    if (notes) *notes = {};
    try {
        return voxelRemeshImpl(source, resolution, error, progress, allowSwap, keepGroups, notes);
    } catch (const std::bad_alloc&) { // (the grid, the bricks or the surface didn't fit after all: nothing half made)
    } catch (const std::length_error&) {
    }
    if (notes) *notes = {};
    if (error) *error = "Voxel Remesh at " + std::to_string(resolution) + " ran out of memory; the Part is unchanged. Lower the Resolution or close other apps";
    return {};
}

} // namespace sf
