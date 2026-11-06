// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "render/Clusters.h"

#include "core/ThreadPool.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <map>
#include <tuple>
#include <vector>

namespace sf {

PositionGrid PositionGrid::around(const AABB& box) {
    PositionGrid g;
    if (!box.valid()) return g;
    const glm::vec3 e = box.extent();
    const float size = std::max({e.x, e.y, e.z});
    const float scale = std::max({std::abs(box.min.x), std::abs(box.min.y), std::abs(box.min.z), std::abs(box.max.x),
                                  std::abs(box.max.y), std::abs(box.max.z), 1e-3f});
    const float pad = std::max(size * 0.125f, scale * 1e-4f);
    g.origin = box.min - glm::vec3(pad);
    g.step = (size + 2.0f * pad) / float(kMax);
    return g;
}

bool PositionGrid::contains(const glm::vec3& p) const {
    const glm::vec3 q = (p - origin) / step;
    return q.x >= 0.0f && q.y >= 0.0f && q.z >= 0.0f && q.x <= float(kMax) && q.y <= float(kMax) && q.z <= float(kMax);
}

namespace {

uint32_t quantize(float v, float origin, float step, bool& inside) {
    const float q = std::round((v - origin) / step);
    if (!(q >= 0.0f && q <= float(PositionGrid::kMax))) { // NaN too
        inside = false;
        return q > 0.0f ? PositionGrid::kMax : 0u;
    }
    return uint32_t(q);
}

uint32_t snorm(float v, int bits) {
    const int range = (1 << (bits - 1)) - 1; // e.g. 11 bits: -1023..1023 stored as 1..2047, so 0 is exact
    const int q = int(std::lround(std::clamp(v, -1.0f, 1.0f) * float(range)));
    return uint32_t(q + range + 1);
}

float unsnorm(uint32_t q, int bits) {
    const int range = (1 << (bits - 1)) - 1;
    return float(int(q) - range - 1) / float(range);
}

} // namespace

bool packVertex(const PositionGrid& grid, const glm::vec3& p, const glm::vec3& n, float curvature, PackedVertex& out) {
    bool inside = true;
    const uint64_t x = quantize(p.x, grid.origin.x, grid.step, inside);
    const uint64_t y = quantize(p.y, grid.origin.y, grid.step, inside);
    const uint64_t z = quantize(p.z, grid.origin.z, grid.step, inside);
    const uint64_t bits = x | (y << 21) | (z << 42);
    out.pos[0] = uint32_t(bits);
    out.pos[1] = uint32_t(bits >> 32);
    // Octahedral normal: project onto |x| + |y| + |z| = 1, fold the lower half over the diagonals.
    const float l1 = std::abs(n.x) + std::abs(n.y) + std::abs(n.z);
    glm::vec2 e(0.0f);
    if (l1 > 0.0f && std::isfinite(l1)) {
        const glm::vec3 a = n / l1;
        e = glm::vec2(a.x, a.y);
        if (a.z < 0.0f)
            e = glm::vec2((1.0f - std::abs(a.y)) * (a.x >= 0.0f ? 1.0f : -1.0f), (1.0f - std::abs(a.x)) * (a.y >= 0.0f ? 1.0f : -1.0f));
    }
    const float c = std::isfinite(curvature) ? std::clamp(curvature / kCurvatureRange, -1.0f, 1.0f) : 0.0f;
    const float companded = c < 0.0f ? -std::sqrt(-c) : std::sqrt(c);
    out.normal = snorm(e.x, 11) | (snorm(e.y, 11) << 11) | (snorm(companded, 10) << 22);
    return inside;
}

glm::vec3 unpackPosition(const PositionGrid& grid, const PackedVertex& v) {
    const uint64_t bits = uint64_t(v.pos[0]) | (uint64_t(v.pos[1]) << 32);
    const glm::vec3 q(float(bits & 0x1FFFFF), float((bits >> 21) & 0x1FFFFF), float((bits >> 42) & 0x1FFFFF));
    return grid.origin + q * grid.step;
}

glm::vec3 unpackNormal(const PackedVertex& v) {
    const glm::vec2 e(unsnorm(v.normal & 2047u, 11), unsnorm((v.normal >> 11) & 2047u, 11));
    glm::vec3 n(e.x, e.y, 1.0f - std::abs(e.x) - std::abs(e.y));
    const float t = std::max(-n.z, 0.0f);
    n.x += n.x >= 0.0f ? -t : t;
    n.y += n.y >= 0.0f ? -t : t;
    return glm::normalize(n);
}

float unpackCurvature(const PackedVertex& v) {
    const float q = unsnorm(v.normal >> 22, 10);
    return q * std::abs(q) * kCurvatureRange;
}

uint32_t packColor(const glm::vec3& c) {
    static const std::vector<uint8_t> table = [] {
        std::vector<uint8_t> t(65536);
        for (size_t i = 0; i < t.size(); ++i) t[i] = uint8_t(std::lround(std::pow(double(i) / 65535.0, 1.0 / 2.2) * 255.0));
        return t;
    }();
    auto ch = [&](float v) { return uint32_t(table[size_t(std::clamp(v, 0.0f, 1.0f) * 65535.0f + 0.5f)]); };
    return ch(c.x) | (ch(c.y) << 8) | (ch(c.z) << 16) | (255u << 24);
}

ClusterBounds clusterBounds(const Mesh& mesh, const UploadOrder& order, uint32_t cluster) {
    ClusterBounds b;
    const size_t nv = mesh.vertexCount(), count = order.clusterTriangles(cluster);
    const auto& P = mesh.positions;
    thread_local std::vector<glm::vec3> normals; // unit face normals (zero: degenerate or invalid)
    normals.resize(count);
    AABB box;
    glm::vec3 sum(0.0f);
    for (uint32_t i = 0; i < count; ++i) {
        const glm::uvec3& tri = mesh.triangles[order.triangle(cluster, i)];
        glm::vec3& n = normals[i];
        n = glm::vec3(0.0f);
        if (tri.x >= nv || tri.y >= nv || tri.z >= nv) continue;
        const glm::vec3 a = P[tri.x], e1 = P[tri.y] - a, e2 = P[tri.z] - a;
        box.expand(a);
        box.expand(P[tri.y]);
        box.expand(P[tri.z]);
        const glm::dvec3 da(a), db(P[tri.y]), dc(P[tri.z]);
        b.volume += glm::dot(da, glm::cross(db, dc));
        const glm::vec3 c = glm::cross(e1, e2);
        const float l2 = glm::dot(c, c);
        if (l2 > 0.0f && std::isfinite(l2)) {
            n = c / std::sqrt(l2);
            sum += n;
        }
    }
    if (!box.valid()) return b;
    b.center = box.center();
    const float sl = glm::length(sum);
    const glm::vec3 axis = sl > 1e-6f ? sum / sl : glm::vec3(0.0f);
    float r2 = 0.0f, minDot = 1.0f;
    bool faces = false;
    for (uint32_t i = 0; i < count; ++i) {
        const glm::vec3& n = normals[i];
        const glm::uvec3& tri = mesh.triangles[order.triangle(cluster, i)];
        if (n.x == 0.0f && n.y == 0.0f && n.z == 0.0f) {
            if (tri.x >= nv || tri.y >= nv || tri.z >= nv) continue;
        } else {
            minDot = std::min(minDot, glm::dot(n, axis));
            faces = true;
        }
        for (int c = 0; c < 3; ++c) {
            const glm::vec3 d = P[tri[c]] - b.center;
            r2 = std::max(r2, glm::dot(d, d));
        }
    }
    // A little slack for float rounding and the GPU's quantised positions.
    b.radius = std::sqrt(r2) * 1.0001f + 1e-6f;
    if (faces && sl > 1e-6f && minDot > 0.0f) {
        b.axis = axis;
        b.coneAngle = std::acos(std::min(minDot, 1.0f));
    }
    return b;
}

namespace {

// Cell -> its representative: an open-addressing table cleared by a stamp (a cluster has at most 3 vertices
// per triangle, so 2^15 slots stay under half full).
struct CellTable {
    static constexpr uint32_t kBits = 15, kSize = 1u << kBits;
    std::vector<uint64_t> keys = std::vector<uint64_t>(kSize);
    std::vector<uint32_t> stamps = std::vector<uint32_t>(kSize, 0);
    std::vector<uint16_t> reps = std::vector<uint16_t>(kSize);
    std::vector<float> dists = std::vector<float>(kSize);
    uint32_t stamp = 0;
    void clear() {
        if (++stamp == 0) {
            std::fill(stamps.begin(), stamps.end(), 0);
            stamp = 1;
        }
    }
    uint32_t slot(uint64_t key, bool& found) {
        for (uint32_t h = uint32_t((key * 0x9E3779B97F4A7C15ull) >> (64 - kBits));; h = (h + 1) & (kSize - 1)) {
            if (stamps[h] != stamp) {
                found = false;
                return h;
            }
            if (keys[h] == key) {
                found = true;
                return h;
            }
        }
    }
};

} // namespace

bool clusterProxy(const Mesh& mesh, const UploadOrder& order, uint32_t c, size_t maxTriangles, std::vector<uint16_t>& out,
                  ClusterBounds& bounds) {
    out.clear();
    const uint32_t n = order.clusterTriangles(c);
    const UploadOrder::Cluster& cl = order.clusters()[c];
    const auto fromGpu = order.fromGpu();
    thread_local std::vector<uint16_t> local, rep;
    thread_local std::vector<uint8_t> pinned;
    local.resize(size_t(n) * 3);
    order.localTriangles(mesh, c, local.data());
    const uint32_t nl = cl.vertexCount;
    pinned.assign(nl, 0);
    AABB box;
    for (uint32_t l = 0; l < nl; ++l) {
        const uint32_t v = fromGpu[cl.firstVertex + l];
        pinned[l] = order.shared(v);
        box.expand(mesh.positions[v]);
    }
    auto pos = [&](uint32_t l) { return mesh.positions[fromGpu[cl.firstVertex + l]]; };
    const bool hidden = mesh.hidden.size() == mesh.triangleCount();
    double edges = 0.0;
    size_t counted = 0;
    for (uint32_t i = 0; i < n; i += 7) { // a sample is plenty
        const uint16_t* t = local.data() + size_t(i) * 3;
        edges += glm::distance(pos(t[0]), pos(t[1])) + glm::distance(pos(t[1]), pos(t[2]));
        counted += 2;
    }
    if (!counted || !box.valid()) return false;
    float cell = float(edges / double(counted)) * 2.5f;
    if (!(cell > 0.0f)) return false;
    thread_local CellTable cells;
    thread_local std::vector<uint32_t> cellOfVertex;
    cellOfVertex.resize(nl);
    rep.resize(nl);
    for (int attempt = 0; attempt < 4; ++attempt, cell *= 1.6f) {
        cells.clear();
        const float inv = 1.0f / cell;
        for (uint32_t l = 0; l < nl; ++l) {
            if (pinned[l]) continue;
            const glm::vec3 p = pos(l);
            const glm::vec3 q = glm::floor((p - box.min) * inv);
            const glm::vec3 centre = box.min + (q + 0.5f) * cell;
            const uint64_t key = uint64_t(uint32_t(q.x)) | (uint64_t(uint32_t(q.y)) << 21) | (uint64_t(uint32_t(q.z)) << 42);
            const float d = glm::distance(p, centre);
            bool found = false;
            const uint32_t h = cells.slot(key, found);
            if (!found) {
                cells.stamps[h] = cells.stamp;
                cells.keys[h] = key;
                cells.reps[h] = uint16_t(l);
                cells.dists[h] = d;
            } else if (d < cells.dists[h]) {
                cells.reps[h] = uint16_t(l);
                cells.dists[h] = d;
            }
            cellOfVertex[l] = h;
        }
        for (uint32_t l = 0; l < nl; ++l) rep[l] = pinned[l] ? uint16_t(l) : cells.reps[cellOfVertex[l]];
        out.clear();
        for (uint32_t i = 0; i < n; ++i) {
            if (hidden && mesh.hidden[order.triangle(c, i)]) continue;
            const uint16_t* t = local.data() + size_t(i) * 3;
            const uint16_t a = rep[t[0]], b = rep[t[1]], d = rep[t[2]];
            if (a == b || b == d || a == d) continue;
            out.insert(out.end(), {a, b, d});
        }
        if (out.size() / 3 <= maxTriangles) break;
    }
    if (out.size() / 3 > maxTriangles) {
        out.clear();
        return false;
    }
    // The proxy's own normal cone (its triangles lean differently from the cluster's).
    glm::vec3 sum(0.0f);
    float minDot = 1.0f;
    bool faces = false;
    thread_local std::vector<glm::vec3> normals;
    normals.resize(out.size() / 3);
    for (size_t i = 0; i < normals.size(); ++i) {
        const glm::vec3 p0 = pos(out[i * 3]);
        const glm::vec3 cr = glm::cross(pos(out[i * 3 + 1]) - p0, pos(out[i * 3 + 2]) - p0);
        const float l2 = glm::dot(cr, cr);
        normals[i] = l2 > 0.0f && std::isfinite(l2) ? cr / std::sqrt(l2) : glm::vec3(0.0f);
        sum += normals[i];
    }
    const float sl = glm::length(sum);
    for (const glm::vec3& nn : normals)
        if (nn != glm::vec3(0.0f) && sl > 1e-6f) {
            minDot = std::min(minDot, glm::dot(nn, sum / sl));
            faces = true;
        }
    bounds.axis = glm::vec3(0.0f, 0.0f, 1.0f);
    bounds.coneAngle = -1.0f;
    if (faces && minDot > 0.0f) {
        bounds.axis = sum / sl;
        bounds.coneAngle = std::acos(std::min(minDot, 1.0f));
    }
    return true;
}

namespace {

uint64_t packCell(glm::uvec3 q) { return uint64_t(q.x) | (uint64_t(q.y) << 21) | (uint64_t(q.z) << 42); }

uint64_t cellKey(const CoarseLevels& L, const glm::vec3& p, int level) {
    const float size = L.cell * float(1u << level);
    const glm::vec3 q = glm::clamp(glm::floor((p - L.origin) / size), glm::vec3(0.0f), glm::vec3(float(0x1FFFFF)));
    return packCell(glm::uvec3(q));
}

// Sorts keys in parallel blocks, merged pairwise.
void parallelSort(std::vector<uint64_t>& v) {
    const size_t blocks = 16, n = v.size(), step = (n + blocks - 1) / blocks;
    if (n < 65536) {
        std::sort(v.begin(), v.end());
        return;
    }
    auto at = [&](size_t i) { return v.begin() + std::ptrdiff_t(std::min(n, i)); };
    poolFor(blocks, [&](size_t b0, size_t b1) {
        for (size_t b = b0; b < b1; ++b) std::sort(at(b * step), at((b + 1) * step));
    }, 1, 1);
    for (size_t width = step; width < n; width *= 2)
        poolFor((n + 2 * width - 1) / (2 * width), [&](size_t p0, size_t p1) {
            for (size_t p = p0; p < p1; ++p) std::inplace_merge(at(p * 2 * width), at(p * 2 * width + width), at(p * 2 * width + 2 * width));
        }, 1, 1);
}

// Cluster c's triangles on the cells of `level` (degenerate and repeated ones dropped) and its error; false: a
// vertex is in no cell of the build.
bool clusterOnCells(const CoarseLevels& L, const Mesh& mesh, const UploadOrder& order, uint32_t c, int level,
                    std::vector<std::array<uint32_t, 3>>& tris, float& error) {
    const size_t nv = mesh.vertexCount(), nt = mesh.triangleCount();
    const bool hidden = mesh.hidden.size() == nt;
    tris.clear();
    error = 0.0f;
    const uint32_t n = order.clusterTriangles(c);
    for (uint32_t i = 0; i < n; ++i) {
        const uint32_t t = order.triangle(c, i);
        if (t >= nt || (hidden && mesh.hidden[t])) continue;
        const glm::uvec3 tri = mesh.triangles[t];
        if (tri.x >= nv || tri.y >= nv || tri.z >= nv) continue;
        std::array<uint32_t, 3> m;
        for (int k = 0; k < 3; ++k) {
            const glm::vec3& p = mesh.positions[tri[k]];
            m[size_t(k)] = L.cellOf(p, level);
            if (m[size_t(k)] == ~0u) return false;
            error = std::max(error, glm::distance(p, L.positions[m[size_t(k)]]));
        }
        if (m[0] == m[1] || m[1] == m[2] || m[0] == m[2]) continue;
        std::rotate(m.begin(), std::min_element(m.begin(), m.end()), m.end()); // (one rotation per triangle)
        tris.push_back(m);
    }
    std::sort(tris.begin(), tris.end());
    tris.erase(std::unique(tris.begin(), tris.end()), tris.end());
    return true;
}

uint32_t roomFor(size_t indices) { return uint32_t(indices + indices / 4 + 48); } // (an edit may add a few)

} // namespace

uint32_t CoarseLevels::cellOf(const glm::vec3& p, int level) const {
    const Level& l = levels[size_t(level)];
    const uint64_t key = cellKey(*this, p, level);
    const auto it = std::lower_bound(l.keys.begin(), l.keys.end(), key);
    if (it != l.keys.end() && *it == key) return l.base + uint32_t(it - l.keys.begin());
    const auto found = l.added.find(key);
    return found != l.added.end() ? found->second : ~0u;
}

uint32_t CoarseLevels::cellOrAdd(const glm::vec3& p, int level) {
    if (const uint32_t k = cellOf(p, level); k != ~0u) return k;
    const uint32_t k = uint32_t(positions.size());
    levels[size_t(level)].added.emplace(cellKey(*this, p, level), k);
    positions.push_back(p);
    normals.push_back(glm::vec3(0.0f, 0.0f, 1.0f));
    return k;
}

CoarseLevels buildCoarseLevels(const Mesh& mesh, const UploadOrder& order) {
    CoarseLevels L;
    const size_t nv = mesh.vertexCount(), nt = mesh.triangleCount();
    const uint32_t nc = uint32_t(order.clusters().size());
    L.clusters.resize(nc);
    if (nv == 0 || nt == 0 || nc == 0) return L;
    AABB box;
    for (const glm::vec3& p : mesh.positions) box.expand(p);
    double edges = 0.0;
    size_t counted = 0;
    for (size_t t = 0; t < nt; t += std::max<size_t>(1, nt / 200000)) {
        const glm::uvec3 tri = mesh.triangles[t];
        if (tri.x >= nv || tri.y >= nv || tri.z >= nv) continue;
        edges += glm::distance(mesh.positions[tri.x], mesh.positions[tri.y]) + glm::distance(mesh.positions[tri.y], mesh.positions[tri.z]);
        counted += 2;
    }
    if (!counted || !box.valid()) return L;
    L.cell = float(edges / double(counted)) * 4.0f;
    if (!(L.cell > 0.0f)) return L;
    // Room on the grid for the mesh to move a little (as PositionGrid's margin).
    L.origin = box.min - (box.max - box.min) * 0.125f - glm::vec3(L.cell);
    const bool hasNormals = mesh.normals.size() == nv;
    // The cells of each level: the sorted keys of the vertices' cells, each once.
    std::vector<uint32_t> cellOfVertex(nv);
    for (int level = 0; level < CoarseLevels::kMaxLevels; ++level) {
        CoarseLevels::Level& lv = L.levels.emplace_back();
        lv.base = uint32_t(L.positions.size());
        {
            std::vector<uint64_t> keys;
            if (level == 0) {
                keys.resize(nv);
                poolFor(nv, [&](size_t v0, size_t v1) {
                    for (size_t v = v0; v < v1; ++v) keys[v] = cellKey(L, mesh.positions[v], 0);
                });
            } else {
                keys.reserve(L.levels[size_t(level) - 1].keys.size());
                for (uint64_t k : L.levels[size_t(level) - 1].keys) // (nested: each axis index halves)
                    keys.push_back(packCell(glm::uvec3(uint32_t(k & 0x1FFFFF), uint32_t((k >> 21) & 0x1FFFFF), uint32_t(k >> 42)) / 2u));
            }
            parallelSort(keys);
            keys.erase(std::unique(keys.begin(), keys.end()), keys.end());
            lv.keys.assign(keys.begin(), keys.end()); // (exact size)
        }
        const size_t cells = lv.keys.size();
        poolFor(nv, [&](size_t v0, size_t v1) {
            for (size_t v = v0; v < v1; ++v) cellOfVertex[v] = L.cellOf(mesh.positions[v], level) - lv.base;
        });
        std::vector<glm::dvec3> sum(cells, glm::dvec3(0.0));
        std::vector<glm::vec3> nsum(cells, glm::vec3(0.0f));
        std::vector<uint32_t> weight(cells, 0);
        for (size_t v = 0; v < nv; ++v) {
            const uint32_t k = cellOfVertex[v];
            sum[k] += glm::dvec3(mesh.positions[v]);
            if (hasNormals) nsum[k] += mesh.normals[v];
            ++weight[k];
        }
        for (size_t k = 0; k < cells; ++k) {
            L.positions.push_back(glm::vec3(sum[k] / double(std::max(weight[k], 1u))));
            const float l = glm::length(nsum[k]);
            L.normals.push_back(l > 1e-12f ? nsum[k] / l : glm::vec3(0.0f, 0.0f, 1.0f));
        }
        if (cells < 64) break;
    }
    cellOfVertex = {};
    // Per cluster, its levels while they pay.
    std::vector<std::vector<uint32_t>> lists(nc);
    poolFor(nc, [&](size_t c0, size_t c1) {
        std::vector<std::array<uint32_t, 3>> tris;
        for (size_t c = c0; c < c1; ++c) {
            CoarseLevels::Lod& lod = L.clusters[c];
            size_t previous = order.clusterTriangles(uint32_t(c));
            for (int level = 0; level < int(L.levels.size()); ++level) {
                float error = 0.0f;
                if (!clusterOnCells(L, mesh, order, uint32_t(c), level, tris, error) || tris.empty() || tris.size() * 10 > previous * 8) break;
                lod.first[lod.levels] = uint32_t(lists[c].size());
                lod.count[lod.levels] = uint32_t(tris.size() * 3);
                lod.capacity[lod.levels] = roomFor(tris.size() * 3);
                lod.error[lod.levels] = std::max(error, lod.levels > 0 ? lod.error[lod.levels - 1] : 0.0f);
                ++lod.levels;
                for (const auto& m : tris) lists[c].insert(lists[c].end(), m.begin(), m.end());
                lists[c].resize(lod.first[lod.levels - 1] + lod.capacity[lod.levels - 1], 0);
                previous = tris.size();
                if (tris.size() < 8) break;
            }
            lists[c].shrink_to_fit();
        }
    }, 16, 1);
    size_t total = 0;
    for (const auto& l : lists) total += l.size();
    L.indices.reserve(total);
    for (uint32_t c = 0; c < nc; ++c) {
        const uint32_t at = uint32_t(L.indices.size());
        for (int l = 0; l < L.clusters[c].levels; ++l) L.clusters[c].first[l] += at;
        L.indices.insert(L.indices.end(), lists[c].begin(), lists[c].end());
        std::vector<uint32_t>().swap(lists[c]);
    }
    return L;
}

std::vector<uint32_t> updateCoarseLevels(CoarseLevels& L, const Mesh& mesh, const UploadOrder& order,
                                         const std::vector<ClusterBounds>& bounds, const std::vector<uint32_t>& dirty,
                                         std::vector<uint32_t>& points) {
    points.clear();
    std::vector<uint32_t> changed;
    if (L.levels.empty() || dirty.empty()) return changed;
    const auto fromGpu = order.fromGpu();
    const size_t nv = mesh.vertexCount();
    // The region the edit touched: the dirty clusters' vertices now, and the points of the cells their levels used.
    AABB reach;
    for (uint32_t c : dirty) {
        const auto& cl = order.clusters()[c];
        for (uint32_t l = 0; l < cl.vertexCount; ++l)
            if (const uint32_t v = fromGpu[cl.firstVertex + l]; v < nv) reach.expand(mesh.positions[v]);
        const CoarseLevels::Lod& lod = L.clusters[c];
        for (int level = 0; level < lod.levels; ++level)
            for (uint32_t i = lod.first[level]; i < lod.first[level] + lod.count[level]; ++i) reach.expand(L.positions[L.indices[i]]);
    }
    if (!reach.valid()) return changed;
    // Cells for the vertices an edit moved where the build had none (pushed out of the surface, most often).
    for (uint32_t c : dirty) {
        const auto& cl = order.clusters()[c];
        for (uint32_t l = 0; l < cl.vertexCount; ++l)
            if (const uint32_t v = fromGpu[cl.firstVertex + l]; v < nv)
                for (int level = 0; level < int(L.levels.size()); ++level) L.cellOrAdd(mesh.positions[v], level);
    }
    // The cells to recompute: at every level, each one whose box meets the region grown by a cell.
    std::vector<uint32_t> cells;
    AABB around = reach; // (the vertices that can be in those cells)
    for (int level = 0; level < int(L.levels.size()); ++level) {
        const CoarseLevels::Level& lv = L.levels[size_t(level)];
        const float size = L.cell * float(1u << level);
        const glm::vec3 lo = glm::clamp(glm::floor((reach.min - glm::vec3(size) - L.origin) / size), glm::vec3(0.0f), glm::vec3(float(0x1FFFFF)));
        const glm::vec3 hi = glm::clamp(glm::floor((reach.max + glm::vec3(size) - L.origin) / size), glm::vec3(0.0f), glm::vec3(float(0x1FFFFF)));
        around.expand(L.origin + lo * size);
        around.expand(L.origin + (hi + 1.0f) * size);
        for (uint32_t z = uint32_t(lo.z); z <= uint32_t(hi.z); ++z)
            for (uint32_t y = uint32_t(lo.y); y <= uint32_t(hi.y); ++y) {
                auto it = std::lower_bound(lv.keys.begin(), lv.keys.end(), packCell(glm::uvec3(uint32_t(lo.x), y, z)));
                const uint64_t end = packCell(glm::uvec3(uint32_t(hi.x), y, z));
                for (; it != lv.keys.end() && *it <= end; ++it) cells.push_back(lv.base + uint32_t(it - lv.keys.begin()));
            }
        for (const auto& [key, k] : lv.added) cells.push_back(k); // (few)
    }
    std::sort(cells.begin(), cells.end());
    if (cells.empty()) return changed;
    reach = around;
    // Every vertex in those cells: from the clusters whose bounds reach them (each mesh vertex once, as the build;
    // a seam vertex has a copy in each of its clusters).
    std::vector<uint32_t> verts;
    for (uint32_t c = 0; c < uint32_t(order.clusters().size()) && c < bounds.size(); ++c) {
        const ClusterBounds& b = bounds[c];
        if (b.radius < 0.0f) continue;
        const glm::vec3 nearest = glm::clamp(b.center, reach.min, reach.max);
        if (glm::distance(nearest, b.center) > b.radius) continue;
        const auto& cl = order.clusters()[c];
        for (uint32_t l = 0; l < cl.vertexCount; ++l)
            if (const uint32_t v = fromGpu[cl.firstVertex + l]; v < nv) verts.push_back(v);
    }
    std::sort(verts.begin(), verts.end());
    verts.erase(std::unique(verts.begin(), verts.end()), verts.end());
    std::vector<glm::dvec3> sum(cells.size(), glm::dvec3(0.0));
    std::vector<glm::vec3> nsum(cells.size(), glm::vec3(0.0f));
    std::vector<uint32_t> weight(cells.size(), 0);
    const bool hasNormals = mesh.normals.size() == nv;
    for (uint32_t v : verts)
        for (int level = 0; level < int(L.levels.size()); ++level) {
            const uint32_t k = L.cellOf(mesh.positions[v], level);
            const auto it = std::lower_bound(cells.begin(), cells.end(), k);
            if (k == ~0u || it == cells.end() || *it != k) continue;
            const size_t i = size_t(it - cells.begin());
            sum[i] += glm::dvec3(mesh.positions[v]);
            if (hasNormals) nsum[i] += mesh.normals[v];
            ++weight[i];
        }
    for (size_t i = 0; i < cells.size(); ++i) {
        if (weight[i] == 0) continue; // (an emptied cell: no triangle maps to it any more)
        L.positions[cells[i]] = glm::vec3(sum[i] / double(weight[i]));
        const float l = glm::length(nsum[i]);
        L.normals[cells[i]] = l > 1e-12f ? nsum[i] / l : glm::vec3(0.0f, 0.0f, 1.0f);
        points.push_back(cells[i]);
    }
    // The dirty clusters' levels on the cells, in their room.
    std::vector<std::array<uint32_t, 3>> tris;
    for (uint32_t c : dirty) {
        CoarseLevels::Lod& lod = L.clusters[c];
        bool ok = lod.levels > 0;
        for (int level = 0; level < lod.levels && ok; ++level) {
            float error = 0.0f;
            ok = clusterOnCells(L, mesh, order, c, level, tris, error) && tris.size() * 3 <= lod.capacity[level];
            if (!ok) break;
            std::fill(L.indices.begin() + lod.first[level], L.indices.begin() + lod.first[level] + lod.capacity[level], 0u);
            for (size_t i = 0; i < tris.size(); ++i)
                std::copy(tris[i].begin(), tris[i].end(), L.indices.begin() + lod.first[level] + i * 3);
            lod.count[level] = uint32_t(tris.size() * 3);
            lod.error[level] = std::max(error, level > 0 ? lod.error[level - 1] : 0.0f);
        }
        if (!ok) lod.levels = 0; // (drawn in full until the next build)
        changed.push_back(c);
    }
    return changed;
}

bool closedOriented(const Mesh& mesh) {
    const size_t nv = mesh.vertexCount(), nt = mesh.triangleCount();
    if (nt == 0) return false;
    // Directed edges out of and into vertex a (to the next and from the previous corner of each triangle).
    auto fan = [&](uint32_t a, std::vector<uint32_t>& outs, std::vector<uint32_t>& ins) {
        for (uint32_t t : mesh.vertexTriangles(a)) {
            if (t >= nt) continue;
            const glm::uvec3& tri = mesh.triangles[t];
            for (int c = 0; c < 3; ++c)
                if (tri[c] == a) {
                    outs.push_back(tri[(c + 1) % 3]);
                    ins.push_back(tri[(c + 2) % 3]);
                }
        }
    };
    // The vertices whose edges don't pair up: compared as sums of a 64-bit hash of the neighbour (no sorting;
    // equal multisets give equal sums, and unequal ones colliding is a 2^-64 chance).
    auto hash = [](uint64_t x) {
        x += 0x9E3779B97F4A7C15ull;
        x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
        x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
        return x ^ (x >> 31);
    };
    constexpr size_t kBlock = 1 << 14;
    std::vector<std::vector<uint32_t>> open((nv + kBlock - 1) / kBlock);
    std::atomic<size_t> openCount{0};
    const size_t limit = std::max<size_t>(64, nv / 100);
    poolFor(nv, [&](size_t v0, size_t v1) {
        std::vector<uint32_t>& list = open[v0 / kBlock];
        for (size_t a = v0; a < v1 && openCount.load(std::memory_order_relaxed) <= limit; ++a) {
            uint64_t sum = 0;
            for (uint32_t t : mesh.vertexTriangles(uint32_t(a))) {
                if (t >= nt) continue;
                const glm::uvec3& tri = mesh.triangles[t];
                for (int c = 0; c < 3; ++c)
                    if (tri[c] == a) sum += hash(tri[(c + 1) % 3]) - hash(tri[(c + 2) % 3]);
            }
            if (sum != 0) {
                list.push_back(uint32_t(a));
                openCount.fetch_add(1, std::memory_order_relaxed);
            }
        }
    }, kBlock);
    if (openCount.load() == 0) return true;
    if (openCount.load() > limit) return false;
    // A few: maybe a seam of unwelded vertices (the same position twice), which is closed all the same. Count
    // vertices at one position as one (the lowest id) and check each such group's edges together.
    struct Key {
        uint32_t x, y, z;
        bool operator<(const Key& o) const { return std::tie(x, y, z) < std::tie(o.x, o.y, o.z); }
    };
    auto keyOf = [&](uint32_t v) {
        Key k{};
        std::memcpy(&k.x, &mesh.positions[v].x, 4);
        std::memcpy(&k.y, &mesh.positions[v].y, 4);
        std::memcpy(&k.z, &mesh.positions[v].z, 4);
        return k;
    };
    std::map<Key, uint32_t> canonical;
    std::vector<uint32_t> outs, ins;
    auto note = [&](uint32_t v) {
        auto [it, added] = canonical.emplace(keyOf(v), v);
        if (!added) it->second = std::min(it->second, v);
    };
    for (const auto& list : open)
        for (uint32_t a : list) {
            note(a);
            outs.clear();
            ins.clear();
            fan(a, outs, ins);
            for (uint32_t u : outs) note(u);
            for (uint32_t u : ins) note(u);
        }
    std::map<uint32_t, std::pair<std::vector<uint32_t>, std::vector<uint32_t>>> groups;
    for (const auto& list : open)
        for (uint32_t a : list) {
            auto& [go, gi] = groups[canonical.at(keyOf(a))];
            outs.clear();
            ins.clear();
            fan(a, outs, ins);
            for (uint32_t u : outs) go.push_back(canonical.at(keyOf(u)));
            for (uint32_t u : ins) gi.push_back(canonical.at(keyOf(u)));
        }
    for (auto& [id, g] : groups) {
        std::sort(g.first.begin(), g.first.end());
        std::sort(g.second.begin(), g.second.end());
        if (g.first != g.second) return false;
    }
    return true;
}

CullView CullView::from(const glm::mat4& view, const glm::mat4& proj) {
    CullView v;
    const glm::mat4 m = proj * view;
    auto row = [&](int i) { return glm::vec4(m[0][i], m[1][i], m[2][i], m[3][i]); };
    const glm::vec4 r0 = row(0), r1 = row(1), r2 = row(2), r3 = row(3);
    const std::array<glm::vec4, 6> p = {r3 + r0, r3 - r0, r3 + r1, r3 - r1, r3 + r2, r3 - r2};
    for (int i = 0; i < 6; ++i) {
        const float l = glm::length(glm::vec3(p[size_t(i)]));
        v.planes[i] = l > 0.0f ? p[size_t(i)] / l : glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
    }
    const glm::mat4 inv = glm::inverse(view);
    v.eye = glm::vec3(inv[3]);
    v.forward = glm::normalize(glm::vec3(inv * glm::vec4(0.0f, 0.0f, -1.0f, 0.0f)));
    v.ortho = proj[3][3] == 1.0f && proj[2][3] == 0.0f;
    v.nearDistance = std::abs(proj[2][2] - 1.0f) > 1e-12f ? proj[3][2] / (proj[2][2] - 1.0f) : 0.0f;
    return v;
}

bool insideFrustum(const CullView& v, const ClusterBounds& b) {
    if (b.radius < 0.0f) return false;
    for (const glm::vec4& p : v.planes)
        if (glm::dot(glm::vec3(p), b.center) + p.w < -b.radius) return false;
    return true;
}

bool facingAway(const CullView& v, const ClusterBounds& b) {
    if (!v.backfaces || b.coneAngle < 0.0f) return false;
    const glm::vec3 d = b.center - v.eye;
    const float dist = glm::length(d);
    if (!(dist > b.radius * 1.001f)) return false;
    // Every normal is within coneAngle of the axis, every direction from the eye to the cluster within
    // asin(r / dist) of the centre's: faces point away when the three angles stay under 90 degrees.
    const float gamma = std::acos(std::clamp(glm::dot(d / dist, b.axis), -1.0f, 1.0f));
    const float beta = std::asin(std::min(1.0f, b.radius / dist));
    constexpr float kMargin = 0.02f; // radians
    return gamma + beta + b.coneAngle < 1.5707963f - kMargin;
}

} // namespace sf
