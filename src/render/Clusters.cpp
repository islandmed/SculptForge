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
