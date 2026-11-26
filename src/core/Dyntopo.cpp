// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/Dyntopo.h"
#include "core/SurfaceQuery.h"

#include "core/ThreadPool.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <chrono>
#include <cmath>
#include <limits>

namespace sf {

namespace {

constexpr uint32_t kNone = ~0u;

// Calls f(meshArray, storedArray) for every per-vertex array of `m` an undo keeps (normals are recomputed).
template <class F>
void zipVertexArrays(Mesh& m, TopologyVertexValues& v, F&& f) {
    f(m.positions, v.positions);
    f(m.mask, v.mask);
    f(m.colors, v.colors);
    f(m.base, v.base);
    f(m.paintBase, v.paintBase);
    v.deltas.resize(m.layers.size());
    for (size_t i = 0; i < m.layers.size(); ++i) f(m.layers[i].delta, v.deltas[i]);
    v.paintColors.resize(m.paintLayers.size());
    v.paintAlphas.resize(m.paintLayers.size());
    for (size_t i = 0; i < m.paintLayers.size(); ++i) {
        f(m.paintLayers[i].color, v.paintColors[i]);
        f(m.paintLayers[i].alpha, v.paintAlphas[i]);
    }
}

template <class F>
void zipTriangleArrays(Mesh& m, TopologyTriangleValues& t, F&& f) {
    f(m.triangles, t.triangles);
    f(m.facegroups, t.facegroups);
    f(m.quadPartners, t.quadPartners);
    f(m.hidden, t.hidden);
}

// Calls f(array) for every per-vertex array of `m` (normals included).
template <class F>
void forVertexArrays(Mesh& m, F&& f) {
    f(m.positions);
    f(m.normals);
    f(m.mask);
    f(m.colors);
    f(m.base);
    f(m.paintBase);
    for (auto& l : m.layers) f(l.delta);
    for (auto& l : m.paintLayers) {
        f(l.color);
        f(l.alpha);
    }
}

template <class F>
void forTriangleArrays(Mesh& m, F&& f) {
    f(m.triangles);
    f(m.facegroups);
    f(m.quadPartners);
    f(m.hidden);
}

// Exchanges the values of `a` at `slots` with `stored` (the values of another state at the slots below
// `count`, in slot order) and resizes `a` to `count`. Absent arrays stay absent.
template <class T>
void swapSlots(std::vector<T>& a, const std::vector<uint32_t>& slots, std::vector<T>& stored, size_t count) {
    if (a.empty()) return;
    std::vector<T> current;
    current.reserve(slots.size());
    for (uint32_t s : slots)
        if (s < a.size()) current.push_back(a[s]);
    a.resize(count);
    size_t k = 0;
    for (uint32_t s : slots)
        if (s < count && k < stored.size()) a[s] = stored[k++];
    stored.swap(current);
}

// The same for per-corner values (3 per slot).
void swapSlots3(std::vector<glm::vec2>& a, const std::vector<uint32_t>& slots, std::vector<glm::vec2>& stored, size_t count) {
    if (a.empty()) return;
    std::vector<glm::vec2> current;
    current.reserve(slots.size() * 3);
    for (uint32_t s : slots)
        if (size_t(s) * 3 + 2 < a.size()) current.insert(current.end(), a.begin() + std::ptrdiff_t(s) * 3, a.begin() + std::ptrdiff_t(s) * 3 + 3);
    a.resize(count * 3);
    size_t k = 0;
    for (uint32_t s : slots)
        if (s < count && k + 3 <= stored.size()) {
            std::copy_n(stored.begin() + std::ptrdiff_t(k), 3, a.begin() + std::ptrdiff_t(s) * 3);
            k += 3;
        }
    stored.swap(current);
}

template <class T>
size_t bytesOf(const std::vector<T>& v) {
    return v.size() * sizeof(T);
}

bool setBit(std::vector<uint64_t>& bits, uint32_t i) {
    if (size_t(i >> 6) >= bits.size()) bits.resize(size_t(i >> 6) + 1 + bits.size() / 4, 0);
    const uint64_t b = uint64_t(1) << (i & 63);
    if (bits[i >> 6] & b) return false;
    bits[i >> 6] |= b;
    return true;
}

float quality(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c) {
    // 1 for an equilateral triangle, 0 for a degenerate one.
    const float area2 = glm::length(glm::cross(b - a, c - a));
    const float sum = glm::dot(b - a, b - a) + glm::dot(c - b, c - b) + glm::dot(a - c, a - c);
    return sum > 0.0f ? 3.4641016f * area2 / sum : 0.0f; // 4 sqrt(3) * area / sum, area = area2 / 2
}

} // namespace

float dyntopoEdgeLength(const DyntopoSettings& s, float radius, float diagonal) {
    const float d = std::clamp(s.detail, 0.0f, 1.0f);
    if (s.constantDetail) return std::max(diagonal, 1e-6f) * 0.05f * std::pow(0.04f, d); // diag/20 .. diag/500
    return std::max(radius, 1e-7f) * 0.4f * std::pow(0.1f, d);                          // 0.4 .. 0.04 x the radius
}

std::string dyntopoBlocker(const Mesh& mesh) {
    if (mesh.multires)
        return "Dynamic topology needs a mesh without subdivision levels: Freeze Levels keeps the level shown and removes the others";
    if (mesh.triangleCount() == 0) return "The mesh has no triangles";
    if (!mesh.tube.empty()) return "This is a Tube Part: Bake it first (Tool > Tube > Bake)";
    return {};
}

size_t TopologyVertexValues::bytes() const {
    size_t n = bytesOf(positions) + bytesOf(colors) + bytesOf(base) + bytesOf(paintBase) + bytesOf(mask);
    for (const auto& d : deltas) n += bytesOf(d);
    for (const auto& d : paintColors) n += bytesOf(d);
    for (const auto& d : paintAlphas) n += bytesOf(d);
    return n;
}

size_t TopologyTriangleValues::bytes() const {
    return bytesOf(triangles) + bytesOf(facegroups) + bytesOf(quadPartners) + bytesOf(hidden) + bytesOf(uvs);
}

size_t TopologyUndo::bytes() const {
    return vertices.bytes() + triangles.bytes() + bytesOf(vertexSlots) + bytesOf(triangleSlots);
}

void TopologyUndo::swapWith(Mesh& mesh) {
    const uint32_t nv = uint32_t(mesh.vertexCount()), nt = uint32_t(mesh.triangleCount());
    zipVertexArrays(mesh, vertices, [&](auto& a, auto& stored) { swapSlots(a, vertexSlots, stored, vertexCount); });
    zipTriangleArrays(mesh, triangles, [&](auto& a, auto& stored) { swapSlots(a, triangleSlots, stored, triangleCount); });
    mesh.normals.resize(vertexCount, glm::vec3(0.0f, 1.0f, 0.0f));
    swapSlots3(mesh.uvs, triangleSlots, triangles.uvs, triangleCount);
    vertexCount = nv;
    triangleCount = nt;
}

// ---------------------------------------------------------------------------
// Recording and elements
// ---------------------------------------------------------------------------

void DynamicTopology::recordVertex(uint32_t v) {
    if (!setBit(vertRec_, v)) return;
    undo_->vertexSlots.push_back(v);
    recordedNow_.push_back(v);
    if (v < startVertices_)
        zipVertexArrays(*mesh_, undo_->vertices, [&](auto& a, auto& stored) {
            if (!a.empty()) stored.push_back(a[v]);
        });
}

void DynamicTopology::recordTriangle(uint32_t t) {
    if (!setBit(triRec_, t)) return;
    undo_->triangleSlots.push_back(t);
    if (t < startTriangles_)
        zipTriangleArrays(*mesh_, undo_->triangles, [&](auto& a, auto& stored) {
            if (!a.empty()) stored.push_back(a[t]);
        });
    if (t < startTriangles_ && !mesh_->uvs.empty())
        undo_->triangles.uvs.insert(undo_->triangles.uvs.end(), mesh_->uvs.begin() + std::ptrdiff_t(t) * 3, mesh_->uvs.begin() + std::ptrdiff_t(t) * 3 + 3);
}

void DynamicTopology::markVertex(uint32_t v) {
    if (v >= changedVFlag_.size()) changedVFlag_.resize(size_t(v) + 1, 0);
    if (changedVFlag_[v]) return;
    changedVFlag_[v] = 1;
    changedV_.push_back(v);
}

void DynamicTopology::markTriangle(uint32_t t) { changedT_.push_back(t); }

uint32_t DynamicTopology::newVertex() {
    Mesh& m = *mesh_;
    if (!freeV_.empty()) {
        const uint32_t v = freeV_.back();
        freeV_.pop_back();
        deadV_[v] = 0;
        return v; // (recorded when it died)
    }
    const uint32_t v = uint32_t(m.vertexCount());
    recordVertex(v);
    forVertexArrays(m, [&](auto& a) {
        if (!a.empty()) a.emplace_back();
    });
    m.normals.resize(m.positions.size(), glm::vec3(0.0f, 1.0f, 0.0f)); // (a mesh without normals gets them)
    m.addVertexSlot();
    deadV_.push_back(0);
    return v;
}

void DynamicTopology::interpolateVertex(uint32_t v, uint32_t a, uint32_t b, float t, const glm::vec3& position) {
    Mesh& m = *mesh_;
    saveVertexUvs(v);
    recordVertex(v);
    forVertexArrays(m, [&](auto& arr) {
        if (!arr.empty()) arr[v] = arr[a] + (arr[b] - arr[a]) * t;
    });
    m.positions[v] = position;
    const glm::vec3 n = m.normals[v];
    const float len = glm::length(n);
    m.normals[v] = len > 1e-20f ? n / len : glm::vec3(0.0f, 1.0f, 0.0f);
    markVertex(v);
}

void DynamicTopology::killVertex(uint32_t v) {
    recordVertex(v);
    deadV_[v] = 1;
    freeV_.push_back(v);
    mesh_->setVertexTriangles(v, {});
    mesh_->setNeighbors(v, {});
    for (size_t k = 0; k < mirrorMaps_.size(); ++k) {
        const uint32_t w = mirrorOf(k, v);
        if (w == kNone) continue;
        const size_t back = k < mirrorInverse_.size() ? mirrorInverse_[k] : k;
        if (mirrorOf(back, w) == v) mirrorMaps_[back].stamp[w] = 0;
        mirrorMaps_[k].stamp[v] = 0;
    }
}

void DynamicTopology::vtInsert(uint32_t v, uint32_t t) {
    const IndexRing r = mesh_->vertexTriangles(v);
    scratch_.assign(r.begin(), r.end());
    scratch_.insert(std::upper_bound(scratch_.begin(), scratch_.end(), t), t);
    mesh_->setVertexTriangles(v, scratch_);
}

void DynamicTopology::vtErase(uint32_t v, uint32_t t) {
    const IndexRing r = mesh_->vertexTriangles(v);
    scratch_.assign(r.begin(), r.end());
    auto it = std::lower_bound(scratch_.begin(), scratch_.end(), t);
    if (it == scratch_.end() || *it != t) return;
    scratch_.erase(it);
    mesh_->setVertexTriangles(v, scratch_);
}

void DynamicTopology::vtReplace(uint32_t v, uint32_t from, uint32_t to) {
    const IndexRing r = mesh_->vertexTriangles(v);
    scratch_.assign(r.begin(), r.end());
    auto it = std::lower_bound(scratch_.begin(), scratch_.end(), from);
    if (it != scratch_.end() && *it == from) scratch_.erase(it);
    scratch_.insert(std::upper_bound(scratch_.begin(), scratch_.end(), to), to);
    mesh_->setVertexTriangles(v, scratch_);
}

void DynamicTopology::unpairQuad(uint32_t t) {
    Mesh& m = *mesh_;
    if (m.quadPartners.empty() || t >= m.quadPartners.size()) return;
    const uint32_t p = m.quadPartners[t];
    if (p == Mesh::kNoQuad) return;
    recordTriangle(t);
    m.quadPartners[t] = Mesh::kNoQuad;
    if (p < m.quadPartners.size() && m.quadPartners[p] == t) {
        recordTriangle(p);
        m.quadPartners[p] = Mesh::kNoQuad;
        markTriangle(p); // its wireframe now shows the old diagonal
    }
}

uint32_t DynamicTopology::newTriangle(const glm::uvec3& tri, uint32_t like) {
    Mesh& m = *mesh_;
    uint32_t t;
    if (!freeT_.empty()) {
        t = freeT_.back();
        freeT_.pop_back();
        deadT_[t] = 0;
    } else {
        t = uint32_t(m.triangleCount());
        recordTriangle(t);
        forTriangleArrays(m, [&](auto& a) {
            if (!a.empty()) a.emplace_back();
        });
        if (!m.uvs.empty()) m.uvs.resize(m.uvs.size() + 3, glm::vec2(0.0f));
        if (!chartOf_.empty()) chartOf_.push_back(0);
        deadT_.push_back(0);
    }
    recordTriangle(t);
    m.triangles[t] = tri;
    if (!m.uvs.empty()) std::copy_n(m.uvs.begin() + std::ptrdiff_t(like) * 3, 3, m.uvs.begin() + std::ptrdiff_t(t) * 3); // (until transferUvs sets them)
    if (!chartOf_.empty()) chartOf_[t] = chartOf_[like];
    if (!m.facegroups.empty()) m.facegroups[t] = m.facegroups[like];
    if (!m.hidden.empty()) m.hidden[t] = m.hidden[like];
    if (!m.quadPartners.empty()) m.quadPartners[t] = Mesh::kNoQuad;
    for (int c = 0; c < 3; ++c) {
        vtInsert(tri[c], t);
        markVertex(tri[c]);
    }
    bvh_->insertTriangle(m, t, like);
    markTriangle(t);
    return t;
}

void DynamicTopology::setTriangle(uint32_t t, const glm::uvec3& tri) {
    Mesh& m = *mesh_;
    saveUv(t);
    recordTriangle(t);
    unpairQuad(t);
    const glm::uvec3 old = m.triangles[t];
    auto has = [](const glm::uvec3& x, uint32_t v) { return x.x == v || x.y == v || x.z == v; };
    for (int c = 0; c < 3; ++c) {
        if (!has(tri, old[c])) vtErase(old[c], t);
        markVertex(old[c]);
    }
    for (int c = 0; c < 3; ++c) {
        if (!has(old, tri[c])) vtInsert(tri[c], t);
        markVertex(tri[c]);
    }
    m.triangles[t] = tri;
    bvh_->touchTriangle(t);
    markTriangle(t);
}

void DynamicTopology::killTriangle(uint32_t t) {
    Mesh& m = *mesh_;
    saveUv(t);
    recordTriangle(t);
    unpairQuad(t);
    const glm::uvec3 tri = m.triangles[t];
    for (int c = 0; c < 3; ++c) {
        vtErase(tri[c], t);
        markVertex(tri[c]);
    }
    bvh_->removeTriangle(t);
    deadT_[t] = 1;
    freeT_.push_back(t);
    markTriangle(t);
}

// ---------------------------------------------------------------------------
// UV transfer
// ---------------------------------------------------------------------------

namespace {

// The point of triangle abc closest to p, as barycentric coordinates (Ericson, Real-Time Collision Detection).
glm::vec3 closestBary(const glm::vec3& p, const glm::vec3& a, const glm::vec3& b, const glm::vec3& c) {
    const glm::vec3 ab = b - a, ac = c - a, ap = p - a;
    const float d1 = glm::dot(ab, ap), d2 = glm::dot(ac, ap);
    if (d1 <= 0.0f && d2 <= 0.0f) return {1.0f, 0.0f, 0.0f};
    const glm::vec3 bp = p - b;
    const float d3 = glm::dot(ab, bp), d4 = glm::dot(ac, bp);
    if (d3 >= 0.0f && d4 <= d3) return {0.0f, 1.0f, 0.0f};
    const float vc = d1 * d4 - d3 * d2;
    if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f) {
        const float v = d1 / (d1 - d3);
        return {1.0f - v, v, 0.0f};
    }
    const glm::vec3 cp = p - c;
    const float d5 = glm::dot(ab, cp), d6 = glm::dot(ac, cp);
    if (d6 >= 0.0f && d5 <= d6) return {0.0f, 0.0f, 1.0f};
    const float vb = d5 * d2 - d1 * d6;
    if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f) {
        const float w = d2 / (d2 - d6);
        return {1.0f - w, 0.0f, w};
    }
    const float va = d3 * d6 - d5 * d4;
    if (va <= 0.0f && (d4 - d3) >= 0.0f && (d5 - d6) >= 0.0f) {
        const float w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
        return {0.0f, 1.0f - w, w};
    }
    const float denom = 1.0f / (va + vb + vc);
    const float v = vb * denom, w = vc * denom;
    return {1.0f - v - w, v, w};
}

uint32_t findRoot(std::vector<uint32_t>& parent, uint32_t x) {
    while (parent[x] != x) {
        parent[x] = parent[parent[x]];
        x = parent[x];
    }
    return x;
}

} // namespace


uint64_t DynamicTopology::hashUvs(const Mesh& m) {
    uint64_t h = 1469598103934665603ull ^ uint64_t(m.triangleCount());
    auto mix = [&](const void* data, size_t bytes) {
        const unsigned char* c = static_cast<const unsigned char*>(data);
        size_t i = 0;
        for (; i + 8 <= bytes; i += 8) {
            uint64_t w;
            std::memcpy(&w, c + i, 8);
            h = (h ^ w) * 0x9E3779B97F4A7C15ull;
            h ^= h >> 29;
        }
        for (; i < bytes; ++i) h = (h ^ c[i]) * 1099511628211ull;
    };
    mix(m.uvs.data(), m.uvs.size() * sizeof(glm::vec2));
    mix(m.triangles.data(), m.triangles.size() * sizeof(glm::uvec3));
    return h;
}

// At a stroke's start: the charts (triangles joined at a vertex where both have the same UV), a label per triangle slot.
void DynamicTopology::labelCharts() {
    Mesh& m = *mesh_;
    chartOf_.clear();
    if (m.uvs.empty()) return;
    const size_t nt = m.triangleCount();
    chartOf_.resize(nt);
    for (size_t t = 0; t < nt; ++t) chartOf_[t] = uint32_t(t);
    auto same = [](const glm::vec2& a, const glm::vec2& b) { return std::abs(a.x - b.x) <= 1e-6f && std::abs(a.y - b.y) <= 1e-6f; };
    for (uint32_t v = 0; v < m.vertexCount(); ++v) {
        glm::vec2 repUv[8];
        uint32_t repTri[8];
        int reps = 0;
        for (uint32_t t : m.vertexTriangles(v)) {
            const glm::uvec3& tri = m.triangles[t];
            const glm::vec2 uv = m.uvs[size_t(t) * 3 + (tri.x == v ? 0u : tri.y == v ? 1u : 2u)];
            int k = 0;
            while (k < reps && !same(repUv[k], uv)) ++k;
            if (k == reps) {
                if (reps < 8) repUv[reps] = uv, repTri[reps++] = t;
                continue;
            }
            const uint32_t a = findRoot(chartOf_, t), b = findRoot(chartOf_, repTri[k]);
            if (a != b) chartOf_[std::max(a, b)] = std::min(a, b);
        }
    }
    for (size_t t = 0; t < nt; ++t) chartOf_[t] = findRoot(chartOf_, uint32_t(t));
    // Which way each chart turns (its triangles' UV area against their winding: the same all over a chart, a chart is not folded).
    std::vector<int32_t> votes(nt, 0);
    for (size_t t = 0; t < nt; ++t) {
        const glm::vec2 e1 = m.uvs[t * 3 + 1] - m.uvs[t * 3], e2 = m.uvs[t * 3 + 2] - m.uvs[t * 3];
        const float turn = e1.x * e2.y - e1.y * e2.x;
        votes[chartOf_[t]] += turn > 0.0f ? 1 : turn < 0.0f ? -1 : 0;
    }
    chartSign_.assign(nt, 0);
    for (size_t t = 0; t < nt; ++t) chartSign_[t] = votes[t] > 0 ? 1 : votes[t] < 0 ? -1 : 0;
}

// At a stroke's start: the texels the charts cover (and one round them), 1024 x 1024. A transferred triangle that would sample
// outside them (long ones across a chart's notch or bend) is drawn in towards its middle until it does not.
void DynamicTopology::buildUvMask() {
    const Mesh& m = *mesh_;
    uvMask_.clear();
    if (m.uvs.empty()) return;
    uvMask_.assign(size_t(kUvMask) * kUvMask, 0);
    auto mark = [&](const glm::vec2& q) {
        const int x = int(q.x * float(kUvMask)), y = int(q.y * float(kUvMask));
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx)
                if (x + dx >= 0 && x + dx < kUvMask && y + dy >= 0 && y + dy < kUvMask) uvMask_[size_t(y + dy) * kUvMask + size_t(x + dx)] = 1;
    };
    for (uint32_t t = 0; t < m.triangleCount(); ++t) {
        const glm::vec2 a = m.uvs[size_t(t) * 3], e1 = m.uvs[size_t(t) * 3 + 1] - a, e2 = m.uvs[size_t(t) * 3 + 2] - a;
        const int steps = int(std::max(glm::length(e1), glm::length(e2)) * float(kUvMask)) + 1;
        if (steps <= 2) { // (a few texels: its corners and middle)
            mark(a), mark(a + e1), mark(a + e2), mark(a + (e1 + e2) * (1.0f / 3.0f));
            continue;
        }
        for (int i = 0; i <= steps; ++i)
            for (int j = 0; i + j <= steps; ++j) mark(a + e1 * (float(i) / float(steps)) + e2 * (float(j) / float(steps)));
    }
}

bool DynamicTopology::uvFits(const glm::vec2* u) const {
    const glm::vec2 centre = (u[0] + u[1] + u[2]) * (1.0f / 3.0f);
    constexpr int n = 16;
    for (int i = 0; i <= n; ++i)
        for (int j = 0; i + j <= n; ++j) {
            const glm::vec2 p = u[0] * (float(i) / float(n)) + u[1] * (float(j) / float(n)) + u[2] * (float(n - i - j) / float(n));
            const glm::vec2 q = centre + (p - centre) * 0.97f;
            const int x = int(q.x * float(kUvMask)), y = int(q.y * float(kUvMask));
            if (x < 0 || x >= kUvMask || y < 0 || y >= kUvMask || !uvMask_[size_t(y) * kUvMask + size_t(x)]) return false;
        }
    return true;
}

// Before triangle t changes (its corners, or where a corner is): its corners as they are, once a dab. Triangles
// this dab makes are not part of the old surface.
void DynamicTopology::saveUv(uint32_t t) {
    if (!uvOn_ || t >= dabTriangles_ || triStamp_[t] == dabStamp_) return;
    const Mesh& m = *mesh_;
    UvSnap& s = snap_;
    triStamp_[t] = dabStamp_;
    const uint32_t i = uint32_t(s.slot.size());
    triSaved_[t] = i;
    s.slot.push_back(t);
    const glm::uvec3 tri = m.triangles[t];
    s.ids.push_back(tri);
    s.chart.push_back(chartOf_[t]);
    for (int c = 0; c < 3; ++c) {
        const uint32_t v = tri[c];
        const uint32_t e = i * 3 + uint32_t(c);
        snapNext_.push_back(snapStamp_[v] == dabStamp_ ? snapHead_[v] : ~0u);
        snapHead_[v] = e;
        snapStamp_[v] = dabStamp_;
        s.pos.push_back(m.positions[v]);
        s.uv.push_back(m.uvs[size_t(t) * 3 + size_t(c)]);
    }
}

void DynamicTopology::saveVertexUvs(uint32_t v) {
    if (!uvOn_) return;
    for (uint32_t t : mesh_->vertexTriangles(v)) saveUv(t);
}

void DynamicTopology::beginUvDab() {
    uvOn_ = !mesh_->uvs.empty() && chartOf_.size() == mesh_->triangleCount();
    snap_.slot.clear();
    snap_.ids.clear();
    snap_.chart.clear();
    snap_.pos.clear();
    snap_.uv.clear();
    snapNext_.clear();
    if (!uvOn_) return;
    const Mesh& m = *mesh_;
    dabTriangles_ = uint32_t(m.triangleCount());
    if (snapStamp_.size() < m.vertexCount()) {
        snapStamp_.resize(m.vertexCount() + m.vertexCount() / 4, 0);
        snapHead_.resize(snapStamp_.size());
    }
    if (triStamp_.size() < m.triangleCount()) {
        triStamp_.resize(m.triangleCount() + m.triangleCount() / 4, 0);
        triSaved_.resize(triStamp_.size());
    }
    if (++dabStamp_ == 0) {
        std::fill(snapStamp_.begin(), snapStamp_.end(), 0);
        std::fill(triStamp_.begin(), triStamp_.end(), 0);
        dabStamp_ = 1;
    }
    if (++hitGen_ == 0) {
        std::fill(hitStamp_.begin(), hitStamp_.end(), 0);
        hitGen_ = 1;
    }
}

// The grid over the saved triangles (a dab saves the ones it changes, a few hundred).
void DynamicTopology::buildUvGrid() {
    UvSnap& s = snap_;
    const size_t n = s.slot.size();
    s.usable.assign(n, 1);
    glm::vec3 lo(1e30f), hi(-1e30f);
    float edgeSum = 0.0f;
    for (size_t i = 0; i < n; ++i) {
        for (int c = 0; c < 3; ++c) {
            lo = glm::min(lo, s.pos[i * 3 + size_t(c)]);
            hi = glm::max(hi, s.pos[i * 3 + size_t(c)]);
        }
        const glm::vec3 e1 = s.pos[i * 3 + 1] - s.pos[i * 3], e2 = s.pos[i * 3 + 2] - s.pos[i * 3];
        const glm::vec3 cr = glm::cross(e1, e2);
        const float l1 = glm::length(e1), l2 = glm::length(e2);
        if (!(glm::dot(cr, cr) > 1e-24f * std::max(1.0f, l1 * l1 * l2 * l2))) s.usable[i] = 0;
        edgeSum += l1;
    }
    const float edge = n ? edgeSum / float(n) : 1.0f;
    float cell = std::max(2.0f * edge, 1e-6f);
    const glm::vec3 ext = glm::max(hi - lo, glm::vec3(cell));
    while (ext.x / cell * (ext.y / cell) * (ext.z / cell) > 262144.0f) cell *= 1.5f;
    s.lo = lo;
    s.cell = glm::vec3(cell);
    s.dims = glm::max(glm::ivec3(1), glm::ivec3(glm::ceil(ext / cell)));
    const size_t cells = size_t(s.dims.x) * size_t(s.dims.y) * size_t(s.dims.z);
    s.head.assign(cells + 1, 0);
    auto range = [&](size_t i, glm::ivec3& a, glm::ivec3& b) {
        glm::vec3 mn = glm::min(s.pos[i * 3], glm::min(s.pos[i * 3 + 1], s.pos[i * 3 + 2]));
        glm::vec3 mx = glm::max(s.pos[i * 3], glm::max(s.pos[i * 3 + 1], s.pos[i * 3 + 2]));
        a = glm::clamp(glm::ivec3(glm::floor((mn - s.lo) / cell)), glm::ivec3(0), s.dims - 1);
        b = glm::clamp(glm::ivec3(glm::floor((mx - s.lo) / cell)), glm::ivec3(0), s.dims - 1);
    };
    auto cellOf = [&](int x, int y, int z) { return (size_t(z) * size_t(s.dims.y) + size_t(y)) * size_t(s.dims.x) + size_t(x); };
    glm::ivec3 a, b;
    for (size_t i = 0; i < n; ++i) {
        if (!s.usable[i]) continue;
        range(i, a, b);
        for (int z = a.z; z <= b.z; ++z)
            for (int y = a.y; y <= b.y; ++y)
                for (int x = a.x; x <= b.x; ++x) ++s.head[cellOf(x, y, z) + 1];
    }
    for (size_t c = 0; c < cells; ++c) s.head[c + 1] += s.head[c];
    s.items.resize(s.head[cells]);
    std::vector<uint32_t> fill(s.head.begin(), s.head.end() - 1);
    for (size_t i = 0; i < n; ++i) {
        if (!s.usable[i]) continue;
        range(i, a, b);
        for (int z = a.z; z <= b.z; ++z)
            for (int y = a.y; y <= b.y; ++y)
                for (int x = a.x; x <= b.x; ++x) s.items[fill[cellOf(x, y, z)]++] = uint32_t(i);
    }
}

// The snapshot triangle closest to p (in `chart` unless anyChart), p's closest point on it, and (anyChart) whether a
// triangle of another chart is about as close (within `tie`): the chart is then a coin toss, and the caller lets the
// triangle's other corners decide.
bool DynamicTopology::nearestUv(const glm::vec3& p, uint32_t chart, bool anyChart, float tie, UvHit& out) const {
    const UvSnap& s = snap_;
    const glm::vec3 rel = (p - s.lo) / s.cell;
    const glm::ivec3 c0 = glm::clamp(glm::ivec3(glm::floor(rel)), glm::ivec3(0), s.dims - 1);
    // How far p is from the walls of its own cell (a cell of shell r is at least (r - 1) cells and this far away).
    float wall = 0.0f;
    if (rel.x >= 0.0f && rel.y >= 0.0f && rel.z >= 0.0f && rel.x < float(s.dims.x) && rel.y < float(s.dims.y) && rel.z < float(s.dims.z)) {
        const glm::vec3 f = rel - glm::floor(rel);
        const glm::vec3 g = glm::min(f, glm::vec3(1.0f) - f);
        wall = std::min(g.x, std::min(g.y, g.z)) * s.cell.x;
    }
    float best = 1e30f, other = 1e30f; // (squared distances: the closest, and the closest in a chart other than its)
    uint32_t bestTri = ~0u, bestChart = ~0u;
    glm::vec3 bestBary(0.0f);
    const int maxR = std::max(s.dims.x, std::max(s.dims.y, s.dims.z));
    for (int r = 0; r <= maxR; ++r) {
        if (bestTri != ~0u) {
            const float reach = float(r - 1) * s.cell.x + wall; // (a cell of shell r is at least this far from p)
            const float limit = std::sqrt(best) + (anyChart ? tie : 0.0f); // (a triangle farther than this changes neither the answer nor "tied")
            if (reach > limit) break;
        }
        const int x0 = c0.x - r, x1 = c0.x + r, y0 = c0.y - r, y1 = c0.y + r, z0 = c0.z - r, z1 = c0.z + r;
        for (int z = std::max(z0, 0); z <= std::min(z1, s.dims.z - 1); ++z)
            for (int y = std::max(y0, 0); y <= std::min(y1, s.dims.y - 1); ++y)
                for (int x = std::max(x0, 0); x <= std::min(x1, s.dims.x - 1); ++x) {
                    if (r > 0 && x != x0 && x != x1 && y != y0 && y != y1 && z != z0 && z != z1) continue; // (inside: an earlier shell)
                    const size_t cell = (size_t(z) * size_t(s.dims.y) + size_t(y)) * size_t(s.dims.x) + size_t(x);
                    for (uint32_t k = s.head[cell]; k < s.head[cell + 1]; ++k) {
                        const uint32_t i = s.items[k];
                        if (!anyChart && s.chart[i] != chart) continue;
                        const glm::vec3 w = closestBary(p, s.pos[i * 3], s.pos[i * 3 + 1], s.pos[i * 3 + 2]);
                        const glm::vec3 q = s.pos[i * 3] * w.x + s.pos[i * 3 + 1] * w.y + s.pos[i * 3 + 2] * w.z - p;
                        const float d = glm::dot(q, q);
                        const uint32_t ch = s.chart[i];
                        if (d < best || (d == best && i < bestTri)) {
                            if (bestChart != ch && bestChart != ~0u) other = std::min(other, best);
                            best = d, bestTri = i, bestBary = w, bestChart = ch;
                        } else if (ch != bestChart)
                            other = std::min(other, d);
                    }
                }
    }
    if (bestTri == ~0u) return false;
    out.tri = bestTri;
    out.bary = bestBary;
    out.chart = bestChart;
    out.dist2 = best;
    const float lim = std::sqrt(best) + tie;
    out.tied = anyChart && other < 1e30f && other <= lim * lim;
    return true;
}

// After a dab's operations: every triangle that changed (or has a vertex that moved) takes its UVs from the surface
// as it was. A corner that did not move keeps the UV it had (in the right chart); one that is new or moved takes the UV of
// its closest point there. A triangle's corners all come from one chart (the one most of them are in, a corner that is a
// toss-up between charts not voting, else the one the triangle was in): the closest points in it.
void DynamicTopology::transferUvs() {
    Mesh& m = *mesh_;
    UvSnap& s = snap_;
    if (!uvOn_ || s.count() == 0) return;
    buildUvGrid();
    std::vector<uint32_t>& cand = uvCandidates_;
    cand.assign(s.slot.begin(), s.slot.end()); // (triangles that changed, or have a corner that moved)
    for (uint32_t t : changedT_)
        if (t >= dabTriangles_) cand.push_back(t); // (and the ones this dab made)
    std::sort(cand.begin(), cand.end());
    cand.erase(std::unique(cand.begin(), cand.end()), cand.end());
    if (hitStamp_.size() < m.vertexCount()) {
        hitStamp_.resize(m.vertexCount() + m.vertexCount() / 4, 0);
        hits_.resize(hitStamp_.size());
    }
    const float tie = 0.25f * meshEdge_;
    const float still = 1e-4f * meshEdge_; // (a vertex that moved less keeps its UV)
    auto uvAt = [&](uint32_t tri, const glm::vec3& w) { return s.uv[size_t(tri) * 3] * w.x + s.uv[size_t(tri) * 3 + 1] * w.y + s.uv[size_t(tri) * 3 + 2] * w.z; };
    // The map of snapshot triangle `tri` carried on past its edges to the point q (its plane's barycentric coordinates, not clamped).
    auto affineUv = [&](uint32_t tri, const glm::vec3& q, bool& ok) {
        const size_t base = size_t(tri) * 3;
        const glm::vec3 a0 = s.pos[base], b0 = s.pos[base + 1] - a0, c0 = s.pos[base + 2] - a0, d = q - a0;
        const float d00 = glm::dot(b0, b0), d01 = glm::dot(b0, c0), d11 = glm::dot(c0, c0), den = d00 * d11 - d01 * d01;
        ok = den > 1e-30f;
        if (!ok) return s.uv[base];
        const float d20 = glm::dot(d, b0), d21 = glm::dot(d, c0);
        const float v = (d11 * d20 - d01 * d21) / den, w = (d00 * d21 - d01 * d20) / den;
        return s.uv[base] + (s.uv[base + 1] - s.uv[base]) * v + (s.uv[base + 2] - s.uv[base]) * w;
    };
    for (uint32_t t : cand) {
        if (t >= m.triangleCount() || deadT_[t]) continue;
        const glm::uvec3 tri = m.triangles[t];
        glm::vec3 p[3];
        for (int c = 0; c < 3; ++c) p[c] = m.positions[tri[c]];
        // Unchanged (the same corners in the same places as in the snapshot): its UVs stay as they are.
        uint32_t oldChart = ~0u;
        if (t < dabTriangles_ && triStamp_[t] == dabStamp_) {
            const size_t i = triSaved_[t];
            if (s.ids[i] == tri && s.pos[i * 3] == p[0] && s.pos[i * 3 + 1] == p[1] && s.pos[i * 3 + 2] == p[2]) continue;
            oldChart = s.chart[i];
        }
        // Each corner: the UVs the snapshot has for that vertex where it has not moved (one per chart it is in), else its closest point.
        struct Corner {
            glm::vec2 uv[3];
            uint32_t chart[3];
            int n = 0;
            bool moved = true, tied = false, ok = true;
            UvHit hit;
        } cor[3];
        for (int c = 0; c < 3; ++c) {
            Corner& k = cor[c];
            const uint32_t v = tri[c];
            if (v < snapStamp_.size() && snapStamp_[v] == dabStamp_) {
                for (uint32_t e = snapHead_[v]; e != ~0u; e = snapNext_[e]) {
                    const glm::vec3 d = s.pos[e] - p[c];
                    if (glm::dot(d, d) > still * still) { k.n = 0; break; }
                    bool known = false;
                    const uint32_t ch = s.chart[e / 3];
                    for (int j = 0; j < k.n; ++j) known = known || k.chart[j] == ch;
                    if (!known && k.n < 3) k.uv[k.n] = s.uv[e], k.chart[k.n++] = ch;
                }
                k.moved = k.n == 0;
            }
            if (!k.moved) continue;
            if (hitStamp_[v] != hitGen_ || false) {
                hits_[v].ok = nearestUv(p[c], 0, true, tie, hits_[v].hit);
                hitStamp_[v] = hitGen_;
            }
            k.ok = hits_[v].ok;
            k.hit = hits_[v].hit;
            k.tied = k.hit.tied;
        }
        if (!cor[0].ok || !cor[1].ok || !cor[2].ok) continue; // (nothing near: it keeps what it has)
        // The chart: most votes (a corner with one chart, or a moved one that is not a toss-up).
        uint32_t votes[3], nv = 0;
        for (int c = 0; c < 3; ++c) {
            const Corner& k = cor[c];
            if (k.moved ? !k.tied : k.n == 1) votes[nv++] = k.moved ? k.hit.chart : k.chart[0];
        }
        uint32_t chosen = ~0u;
        for (uint32_t a = 0; a < nv && chosen == ~0u; ++a) {
            uint32_t count = 0;
            for (uint32_t b = 0; b < nv; ++b) count += votes[b] == votes[a];
            if (count * 2 > nv || (count == nv)) chosen = votes[a];
        }
        if (chosen == ~0u) chosen = oldChart != ~0u && nv != 1 ? oldChart : nv ? votes[0] : ~0u;
        if (chosen == ~0u) { // (every corner a toss-up or on a seam: the chart under the middle)
            UvHit mid;
            if (nearestUv((p[0] + p[1] + p[2]) * (1.0f / 3.0f), 0, true, 0.0f, mid)) chosen = mid.chart;
            else continue;
        }
        glm::vec2 uv[3], carried[3];
        bool hasCarried[3] = {false, false, false}, anyCarried = false;
        for (int c = 0; c < 3; ++c) {
            Corner& k = cor[c];
            bool got = false;
            if (k.moved) {
                if (k.hit.chart == chosen) uv[c] = uvAt(k.hit.tri, k.hit.bary), got = true;
            } else {
                for (int j = 0; j < k.n && !got; ++j)
                    if (k.chart[j] == chosen) uv[c] = k.uv[j], got = true;
            }
            if (got) continue;
            UvHit h;
            if (nearestUv(p[c], chosen, false, 0.0f, h)) { // (a corner the chart does not reach: its closest point, or the map carried on past the edge)
                uv[c] = uvAt(h.tri, h.bary);
                bool ok;
                const glm::vec2 out = affineUv(h.tri, p[c], ok);
                if (ok && out != uv[c]) carried[c] = out, hasCarried[c] = anyCarried = true;
            } else uv[c] = m.uvs[size_t(t) * 3 + size_t(c)];
        }
        if (anyCarried) { // (the paint goes on past the chart's edge where there is room for it, else stops at the edge)
            glm::vec2 trial[3];
            for (int c = 0; c < 3; ++c) trial[c] = hasCarried[c] ? carried[c] : uv[c];
            if (uvMask_.empty() || uvFits(trial)) std::copy(trial, trial + 3, uv);
        }
        // A triangle across a bend of its chart can come out turned over (its corners' closest points in the other order): it then
        // takes the affine map of the chart's triangle at its middle instead, which never turns.
        if (chosen < chartSign_.size() && chartSign_[chosen] != 0) {
            const glm::vec2 f1 = uv[1] - uv[0], f2 = uv[2] - uv[0];
            if (float(chartSign_[chosen]) * (f1.x * f2.y - f1.y * f2.x) < 0.0f) {
                UvHit mh;
                if (nearestUv((p[0] + p[1] + p[2]) * (1.0f / 3.0f), chosen, false, 0.0f, mh)) {
                    bool ok;
                    glm::vec2 q[3];
                    for (int c = 0; c < 3; ++c) q[c] = affineUv(mh.tri, p[c], ok);
                    if (ok)
                        for (int c = 0; c < 3; ++c) uv[c] = q[c];
                }
            }
        }
        if (!uvMask_.empty() && !uvFits(uv)) { // (a long triangle across a notch: drawn in towards the chart's point nearest its middle)
            glm::vec2 centre = (uv[0] + uv[1] + uv[2]) * (1.0f / 3.0f);
            UvHit h;
            if (nearestUv((p[0] + p[1] + p[2]) * (1.0f / 3.0f), chosen, false, 0.0f, h)) centre = uvAt(h.tri, h.bary);
            glm::vec2 shrunk[3];
            for (float f : {0.85f, 0.7f, 0.55f, 0.4f, 0.3f, 0.2f, 0.12f, 0.06f, 0.02f, 0.0f}) {
                for (int c = 0; c < 3; ++c) shrunk[c] = centre + (uv[c] - centre) * f;
                if (f == 0.0f || uvFits(shrunk)) break; // (a thin chart: the paint in a speck)
            }
            for (int c = 0; c < 3; ++c) uv[c] = shrunk[c];
        }
        recordTriangle(t);
        for (int c = 0; c < 3; ++c) m.uvs[size_t(t) * 3 + size_t(c)] = uv[c];
        chartOf_[t] = chosen;
        markTriangle(t);
    }
}

void DynamicTopology::moveTriangle(uint32_t from, uint32_t to) {
    Mesh& m = *mesh_;
    recordTriangle(to);
    recordTriangle(from);
    forTriangleArrays(m, [&](auto& a) {
        if (!a.empty()) a[to] = a[from];
    });
    if (!m.uvs.empty()) std::copy_n(m.uvs.begin() + std::ptrdiff_t(from) * 3, 3, m.uvs.begin() + std::ptrdiff_t(to) * 3);
    if (!chartOf_.empty()) chartOf_[to] = chartOf_[from];
    if (!m.quadPartners.empty()) {
        const uint32_t p = m.quadPartners[to];
        if (p != Mesh::kNoQuad && p < m.quadPartners.size() && m.quadPartners[p] == from) {
            recordTriangle(p);
            m.quadPartners[p] = to;
        }
    }
    const glm::uvec3 tri = m.triangles[to];
    for (int c = 0; c < 3; ++c) vtReplace(tri[c], from, to);
    bvh_->renumberTriangle(from, to);
    markTriangle(to);
    markTriangle(from);
}

void DynamicTopology::moveVertex(uint32_t from, uint32_t to) {
    Mesh& m = *mesh_;
    recordVertex(to);
    recordVertex(from);
    forVertexArrays(m, [&](auto& a) {
        if (!a.empty()) a[to] = a[from];
    });
    const IndexRing tr = m.vertexTriangles(from);
    scratch2_.assign(tr.begin(), tr.end());
    m.setVertexTriangles(to, scratch2_);
    for (uint32_t t : scratch2_) {
        recordTriangle(t);
        glm::uvec3& tri = m.triangles[t];
        for (int c = 0; c < 3; ++c) {
            if (tri[c] == from) tri[c] = to;
            else markVertex(tri[c]); // its ring names `from`
        }
        markTriangle(t);
    }
    m.setVertexTriangles(from, {});
    markVertex(to);
    moved_.emplace_back(from, to);
}

// Fills the slots this remesh freed with the elements at the end of the arrays, so the mesh has no dead
// entries; then drops the end.
void DynamicTopology::compact() {
    Mesh& m = *mesh_;
    std::sort(freeT_.begin(), freeT_.end());
    size_t nt = m.triangleCount();
    for (uint32_t t : freeT_) {
        while (nt > 0 && deadT_[nt - 1]) --nt;
        if (t >= nt) break;
        moveTriangle(uint32_t(nt - 1), t);
        deadT_[t] = 0;
        --nt;
    }
    forTriangleArrays(m, [&](auto& a) {
        if (!a.empty()) a.resize(nt);
    });
    if (!m.uvs.empty()) m.uvs.resize(nt * 3);
    if (!chartOf_.empty()) chartOf_.resize(nt);
    bvh_->resizeTriangles(nt);
    deadT_.resize(nt); // (every slot below nt is live again)
    freeT_.clear();

    std::sort(freeV_.begin(), freeV_.end());
    const size_t before = m.vertexCount();
    size_t nv = before;
    for (uint32_t v : freeV_) {
        while (nv > 0 && deadV_[nv - 1]) --nv;
        if (v >= nv) break;
        moveVertex(uint32_t(nv - 1), v);
        deadV_[v] = 0;
        --nv;
    }
    forVertexArrays(m, [&](auto& a) {
        if (!a.empty()) a.resize(nv);
    });
    for (size_t i = nv; i < before; ++i) m.removeLastVertexSlot();
    deadV_.resize(nv);
    freeV_.clear();
}

// ---------------------------------------------------------------------------
// Queries
// ---------------------------------------------------------------------------

int DynamicTopology::edgeTriangles(uint32_t a, uint32_t b, uint32_t* out) const {
    int n = 0;
    for (uint32_t t : mesh_->vertexTriangles(a)) {
        const glm::uvec3& tri = mesh_->triangles[t];
        if (tri.x == b || tri.y == b || tri.z == b) {
            if (n < 4) out[n] = t;
            ++n;
        }
    }
    return n;
}

void DynamicTopology::ring(uint32_t v, std::vector<uint32_t>& out) const {
    out.clear();
    for (uint32_t t : mesh_->vertexTriangles(v)) {
        const glm::uvec3& tri = mesh_->triangles[t];
        for (int c = 0; c < 3; ++c)
            if (tri[c] != v) out.push_back(tri[c]);
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
}

float DynamicTopology::len2(uint32_t a, uint32_t b) const {
    const glm::vec3 d = mesh_->positions[a] - mesh_->positions[b];
    return glm::dot(d, d);
}

// The edge passes within the brush radius of the dab's centre (with `mirrored`, or of one of its mirror images).
bool DynamicTopology::inside(uint32_t a, uint32_t b, bool mirrored) const {
    const glm::vec3 p = mesh_->positions[a], d = mesh_->positions[b] - p;
    const float dd = glm::dot(d, d);
    auto within = [&](const glm::vec3& c) {
        const float t = dd > 0.0f ? std::clamp(glm::dot(c - p, d) / dd, 0.0f, 1.0f) : 0.0f;
        const glm::vec3 q = p + d * t - c;
        return glm::dot(q, q) <= radius_ * radius_;
    };
    if (within(center_)) return true;
    if (!mirrored) return false;
    for (const SymCopy& s : mirrorSigns_)
        if (within(s.point(center_))) return true;
    return false;
}

// Operations are found on the brush's side and done on every side at once (orbit), which needs each edge's
// mirror images. A mesh whose sides aren't mirrored (QRemesh's quads, a model edited without symmetry) has none
// to find, and its other sides were left coarse while the brush moved them too (#23): an edge under a mirror
// image of the brush whose own images are missing is remeshed where it is, as the brush's side is.
bool DynamicTopology::candidate(uint32_t a, uint32_t b) const {
    if (inside(a, b)) return true;
    if (mirrorSigns_.empty() || !inside(a, b, true)) return false;
    orbit(a, b, orbitScratch_);
    return orbitIncomplete_;
}

// The triangles under the brush and under its mirror images (for the edges candidate() takes there), and the
// slivers under all of them: a brush can squash a triangle on one side only (overlapping mirrored dabs), and
// they must be repaired where they are.
void DynamicTopology::queryRegion() {
    bvh_->queryTriangles(*mesh_, center_, radius_, regionTris_);
    mirrorTris_.clear();
    slivers_.clear();
    const auto& p = mesh_->positions;
    auto addSlivers = [&](const std::vector<uint32_t>& tris) {
        for (uint32_t t : tris) {
            const glm::uvec3& tri = mesh_->triangles[t];
            if (quality(p[tri.x], p[tri.y], p[tri.z]) < 0.05f) slivers_.push_back(t);
        }
    };
    addSlivers(regionTris_);
    std::vector<uint32_t> more;
    for (const SymCopy& s : mirrorSigns_) {
        bvh_->queryTriangles(*mesh_, s.point(center_), radius_, more);
        addSlivers(more);
        mirrorTris_.insert(mirrorTris_.end(), more.begin(), more.end());
    }
    std::sort(mirrorTris_.begin(), mirrorTris_.end());
    mirrorTris_.erase(std::unique(mirrorTris_.begin(), mirrorTris_.end()), mirrorTris_.end());
    std::sort(slivers_.begin(), slivers_.end());
    slivers_.erase(std::unique(slivers_.begin(), slivers_.end()), slivers_.end());
}

// Left alone: fully masked, next to a hidden triangle, on a non-manifold edge, or without triangles.
bool DynamicTopology::locked(uint32_t v) const {
    const Mesh& m = *mesh_;
    if (deadV_[v]) return true;
    if (m.maskAt(v) >= 1.0f - 1e-4f) return true;
    const IndexRing tris = m.vertexTriangles(v);
    if (tris.empty()) return true;
    if (m.hasHidden())
        for (uint32_t t : tris)
            if (m.hidden[t]) return true;
    // Each neighbour shares one (boundary) or two triangles with v; more is non-manifold. (A small table for the
    // usual valences; sorting for the rare fan with more neighbours.)
    if (tris.size() <= 32) {
        uint32_t other[96], count[96];
        int n = 0;
        for (uint32_t t : tris) {
            const glm::uvec3& tri = m.triangles[t];
            for (int c = 0; c < 3; ++c) {
                if (tri[c] == v) continue;
                int i = 0;
                while (i < n && other[i] != tri[c]) ++i;
                if (i == n) {
                    other[n] = tri[c];
                    count[n++] = 0;
                }
                if (++count[i] > 2) return true;
            }
        }
        return false;
    }
    std::vector<uint32_t>& others = lockScratch_;
    others.clear();
    for (uint32_t t : tris) {
        const glm::uvec3& tri = m.triangles[t];
        for (int c = 0; c < 3; ++c)
            if (tri[c] != v) others.push_back(tri[c]);
    }
    std::sort(others.begin(), others.end());
    for (size_t i = 0; i + 2 < others.size(); ++i)
        if (others[i] == others[i + 2]) return true;
    return false;
}

bool DynamicTopology::planeVertex(uint32_t v) const {
    for (size_t k = 0; k < mirrorMaps_.size(); ++k)
        if (mirrorOf(k, v) == v) return true;
    return false;
}

// Boundary, face group border, non-manifold, or lying in a symmetry plane.
bool DynamicTopology::featureEdge(uint32_t a, uint32_t b) const {
    uint32_t tris[4];
    const int n = edgeTriangles(a, b, tris);
    if (n != 2) return true;
    if (mesh_->facegroupAt(tris[0]) != mesh_->facegroupAt(tris[1])) return true;
    for (size_t k = 0; k < mirrorMaps_.size(); ++k)
        if (mirrorOf(k, a) == a && mirrorOf(k, b) == b) return true;
    return false;
}

// The number of feature edges at v (the other ends of the first two in `other`), or 3 for a corner: a vertex
// in a symmetry plane is a corner unless exactly two of its edges lie in that plane.
int DynamicTopology::featureEdges(uint32_t v, uint32_t* other) const {
    ring(v, scratch_);
    int n = 0;
    for (uint32_t u : scratch_)
        if (featureEdge(v, u)) {
            if (n < 2) other[n] = u;
            ++n;
        }
    if (n == 0 && planeVertex(v)) return 3;
    return std::min(n, 3);
}

bool DynamicTopology::boundaryVertex(uint32_t v) const {
    ring(v, scratch2_);
    uint32_t tris[4];
    for (uint32_t u : scratch2_)
        if (edgeTriangles(v, u, tris) == 1) return true;
    return false;
}

glm::vec3 DynamicTopology::faceNormalAt(uint32_t v) const {
    glm::vec3 n(0.0f);
    const auto& p = mesh_->positions;
    for (uint32_t t : mesh_->vertexTriangles(v)) {
        const glm::uvec3& tri = mesh_->triangles[t];
        n += glm::cross(p[tri.y] - p[tri.x], p[tri.z] - p[tri.x]);
    }
    const float len = glm::length(n);
    return len > 1e-30f ? n / len : glm::vec3(0.0f);
}

// ---------------------------------------------------------------------------
// Symmetry
// ---------------------------------------------------------------------------

uint32_t DynamicTopology::mirrorOf(size_t k, uint32_t v) const {
    const MirrorMap& map = mirrorMaps_[k];
    return v < map.to.size() && map.stamp[v] == mirrorStamp_ ? map.to[v] : kNone;
}

void DynamicTopology::setMirror(size_t k, uint32_t v, uint32_t w) {
    MirrorMap& map = mirrorMaps_[k];
    const size_t need = size_t(std::max(v, w)) + 1;
    if (map.to.size() < need) {
        map.to.resize(need + need / 4, kNone);
        map.stamp.resize(need + need / 4, 0);
    }
    map.to[v] = w;
    map.stamp[v] = mirrorStamp_;
    // The inverse copy maps w back to v (the same map for a mirror).
    MirrorMap& back = mirrorMaps_[k < mirrorInverse_.size() ? mirrorInverse_[k] : k];
    if (back.to.size() < need) {
        back.to.resize(need + need / 4, kNone);
        back.stamp.resize(need + need / 4, 0);
    }
    back.to[w] = v;
    back.stamp[w] = mirrorStamp_;
}

// Pairs each vertex with the vertex at its mirror image (within eps_), for every mirror; only pairs that
// find each other both ways count. The match is loose because overlapping mirrored dabs near the plane don't
// move both sides exactly alike, but vertices are rarely closer than half an edge.
void DynamicTopology::matchMirrors(const std::vector<uint32_t>& verts) {
    const Mesh& m = *mesh_;
    // A grid of cells twice the tolerance (an open-addressing table of the occupied cells): a point's nearest
    // vertex within the tolerance is in one of the 2 x 2 x 2 cells around it.
    const float cell = std::max(eps_ * 2.0f, 1e-12f);
    auto cellOf = [cell](const glm::vec3& p) { return glm::ivec3(glm::floor(p / cell)); };
    auto keyOf = [](const glm::ivec3& q) {
        return (uint64_t(uint32_t(q.x) & 0x1FFFFF) << 42) | (uint64_t(uint32_t(q.y) & 0x1FFFFF) << 21) | uint64_t(uint32_t(q.z) & 0x1FFFFF);
    };
    // Each occupied cell's first vertex in the table, the rest chained through gridNext_.
    size_t slots = 16;
    while (slots < verts.size() * 2) slots *= 2;
    gridKeys_.assign(slots, ~uint64_t(0));
    gridHead_.resize(slots);
    gridNext_.resize(verts.size());
    auto slotOf = [&](uint64_t key) {
        size_t h = size_t((key * 0x9E3779B97F4A7C15ull) >> 20) & (slots - 1);
        while (gridKeys_[h] != ~uint64_t(0) && gridKeys_[h] != key) h = (h + 1) & (slots - 1);
        return h;
    };
    for (uint32_t i = 0; i < verts.size(); ++i) {
        const uint64_t key = keyOf(cellOf(m.positions[verts[i]]));
        const size_t h = slotOf(key);
        gridNext_[i] = gridKeys_[h] == key ? gridHead_[h] : kNone;
        gridKeys_[h] = key;
        gridHead_[h] = i;
    }
    auto nearest = [&](const glm::vec3& p) {
        uint32_t best = kNone;
        float bestD = eps_ * eps_;
        const glm::vec3 f = p / cell;
        const glm::ivec3 q = glm::ivec3(glm::floor(f));
        const glm::ivec3 step(f.x - float(q.x) < 0.5f ? -1 : 1, f.y - float(q.y) < 0.5f ? -1 : 1, f.z - float(q.z) < 0.5f ? -1 : 1);
        for (int c = 0; c < 8; ++c) {
            const glm::ivec3 at = q + glm::ivec3(c & 1 ? step.x : 0, c & 2 ? step.y : 0, c & 4 ? step.z : 0);
            const uint64_t key = keyOf(at);
            const size_t h = slotOf(key);
            if (gridKeys_[h] != key) continue;
            for (uint32_t i = gridHead_[h]; i != kNone; i = gridNext_[i]) {
                const uint32_t w = verts[i];
                const glm::vec3 d = m.positions[w] - p;
                const float dd = glm::dot(d, d);
                if (dd < bestD || (dd == bestD && best != kNone && w < best)) {
                    bestD = dd;
                    best = w;
                }
            }
        }
        return best;
    };
    // Each vertex's partner is worked out in parallel (the grid is only read), then taken in order, so the pairs are
    // those the one-by-one search finds.
    std::vector<uint32_t> partner(verts.size());
    for (size_t k = 0; k < mirrorSigns_.size(); ++k) {
        const SymCopy& s = mirrorSigns_[k];
        poolFor(verts.size(), [&](size_t b, size_t e) {
            for (size_t i = b; i < e; ++i) {
                const uint32_t v = verts[i];
                const uint32_t w = nearest(s.point(m.positions[v]));
                partner[i] = w != kNone && nearest(s.inversePoint(m.positions[w])) == v ? w : kNone;
            }
        });
        for (size_t i = 0; i < verts.size(); ++i) {
            const uint32_t v = verts[i];
            if (mirrorOf(k, v) != kNone || partner[i] == kNone) continue; // (its partner found it first)
            setMirror(k, v, partner[i]);
        }
    }
}

// The edge a-b and its distinct mirror images (each once, either direction).
void DynamicTopology::orbit(uint32_t a, uint32_t b, std::vector<std::pair<uint32_t, uint32_t>>& out) const {
    out.assign(1, {a, b});
    orbitIncomplete_ = false;
    uint32_t tris[4];
    for (size_t k = 0; k < mirrorMaps_.size(); ++k) {
        const uint32_t ma = mirrorOf(k, a), mb = mirrorOf(k, b);
        if (ma == kNone || mb == kNone || ma == mb || edgeTriangles(ma, mb, tris) == 0) {
            orbitIncomplete_ = true;
            continue;
        }
        bool seen = false;
        for (const auto& e : out) seen = seen || (e.first == ma && e.second == mb) || (e.first == mb && e.second == ma);
        if (!seen && edgeTriangles(ma, mb, tris) > 0) out.emplace_back(ma, mb);
    }
}

bool DynamicTopology::orbitSharesTriangle(const std::vector<std::pair<uint32_t, uint32_t>>& edges) const {
    uint32_t ti[4], tj[4];
    for (size_t i = 0; i < edges.size(); ++i) {
        const int ni = std::min(4, edgeTriangles(edges[i].first, edges[i].second, ti));
        for (size_t j = i + 1; j < edges.size(); ++j) {
            const int nj = std::min(4, edgeTriangles(edges[j].first, edges[j].second, tj));
            for (int x = 0; x < ni; ++x)
                for (int y = 0; y < nj; ++y)
                    if (ti[x] == tj[y]) return true;
            // Sharing a vertex is enough to make the order of the two operations matter.
            const auto& e = edges[i];
            const auto& f = edges[j];
            if (e.first == f.first || e.first == f.second || e.second == f.first || e.second == f.second) return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// Operations
// ---------------------------------------------------------------------------

// Splits edge a-b at its midpoint: each triangle on it becomes two. Returns the new vertex (kNone if the
// edge isn't a manifold one).
uint32_t DynamicTopology::split(uint32_t a, uint32_t b) {
    Mesh& m = *mesh_;
    uint32_t tris[4];
    const int n = edgeTriangles(a, b, tris);
    if (n < 1 || n > 2) return kNone;
    // On the curve the end normals describe (a PN-triangle edge's midpoint), not the chord: splitting a
    // coarse curved surface then keeps it round instead of showing its old flat facets. Kept within a quarter
    // of the edge, for creases.
    const glm::vec3 pa = m.positions[a], pb = m.positions[b];
    glm::vec3 bend(0.0f);
    if (m.normals.size() == m.vertexCount()) {
        const glm::vec3 na = m.normals[a], nb = m.normals[b], e = pb - pa;
        bend = (na * glm::dot(e, na) - nb * glm::dot(e, nb)) * -0.125f;
        const float len = glm::length(bend), limit = 0.25f * glm::length(e);
        if (!std::isfinite(len)) bend = glm::vec3(0.0f);
        else if (len > limit) bend *= limit / len;
    }
    const glm::vec3 mid = (pa + pb) * 0.5f + bend;
    // Splitting a triangle squashed flat only makes more flat ones (collapses and flips repair those).
    const float minArea = 1e-6f * target_ * target_;
    for (int i = 0; i < n; ++i) {
        const glm::uvec3& tri = m.triangles[tris[i]];
        uint32_t c = tri.x;
        for (int k = 0; k < 3; ++k)
            if (tri[k] != a && tri[k] != b) c = tri[k];
        const glm::vec3 pc = m.positions[c];
        if (glm::length(glm::cross(mid - pa, pc - pa)) < minArea || glm::length(glm::cross(pb - mid, pc - mid)) < minArea) return kNone;
    }
    const uint32_t v = newVertex();
    interpolateVertex(v, a, b, 0.5f, mid);
    if (!m.base.empty()) m.base[v] += bend; // (sculpt layers: positions stay base + offsets)
    for (int i = 0; i < n; ++i) {
        const uint32_t t = tris[i];
        glm::uvec3 tri = m.triangles[t];
        int k = 0;
        while (k < 3 && !((tri[k] == a && tri[(k + 1) % 3] == b) || (tri[k] == b && tri[(k + 1) % 3] == a))) ++k;
        if (k == 3) continue;
        const uint32_t x = tri[k], y = tri[(k + 1) % 3], c = tri[(k + 2) % 3];
        glm::uvec3 first = tri;
        first[(k + 1) % 3] = v; // (x, v, c) in t's own rotation
        setTriangle(t, first);
        newTriangle(glm::uvec3(v, y, c), t);
        (void)x;
    }
    ++stats.splits;
    ++ops_;
    return v;
}

void DynamicTopology::pushEdgesAround(uint32_t v, bool longer) {
    ring(v, scratch2_);
    for (uint32_t u : scratch2_) {
        if (deadV_[u] || !candidate(v, u)) continue;
        const float l = len2(v, u);
        if (longer ? l > splitLen2_ : l < collapseLen2_) {
            heap_.push_back({longer ? l : -l, std::min(u, v), std::max(u, v)});
            std::push_heap(heap_.begin(), heap_.end(), [](const Edge& x, const Edge& y) {
                return x.len2 != y.len2 ? x.len2 < y.len2 : x.a != y.a ? x.a > y.a : x.b > y.b;
            });
        }
    }
}

void DynamicTopology::splitOrbit(uint32_t a, uint32_t b, int depth) {
    std::vector<std::pair<uint32_t, uint32_t>> edges;
    orbit(a, b, edges);
    // Two images of the edge on one triangle (a triangle across the symmetry plane): split its third edge,
    // which crosses the plane, first, so the two images end up apart and the result stays mirrored.
    if (edges.size() > 1 && depth < 3) {
        uint32_t ti[4], tj[4];
        bool again = false;
        for (size_t i = 0; i < edges.size() && !again; ++i) {
            const int ni = std::min(4, edgeTriangles(edges[i].first, edges[i].second, ti));
            for (size_t j = i + 1; j < edges.size() && !again; ++j) {
                const int nj = std::min(4, edgeTriangles(edges[j].first, edges[j].second, tj));
                for (int x = 0; x < ni && !again; ++x)
                    for (int y = 0; y < nj && !again; ++y) {
                        if (ti[x] != tj[y]) continue;
                        const glm::uvec3 tri = mesh_->triangles[ti[x]];
                        // The corner on neither edge... the third edge joins the two corners other than the shared one.
                        uint32_t shared = kNone;
                        for (uint32_t c : {edges[i].first, edges[i].second})
                            if (c == edges[j].first || c == edges[j].second) shared = c;
                        if (shared == kNone) continue;
                        uint32_t p = kNone, q = kNone;
                        for (int c = 0; c < 3; ++c)
                            if (tri[c] != shared) (p == kNone ? p : q) = tri[c];
                        if (p != kNone && q != kNone && !locked(p) && !locked(q)) {
                            splitOrbit(p, q, depth + 1);
                            again = true;
                        }
                    }
            }
        }
        if (again) orbit(a, b, edges);
    }
    if (orbitIncomplete_ && !mirrorMaps_.empty()) ++stats.unpaired;
    std::vector<uint32_t> made(edges.size(), kNone);
    uint32_t tris[4];
    for (size_t i = 0; i < edges.size(); ++i) {
        const auto [x, y] = edges[i];
        const int n = edgeTriangles(x, y, tris);
        if (n < 1 || n > 2 || locked(x) || locked(y)) continue;
        made[i] = split(x, y);
    }
    // The new vertices are each other's mirror images as their edges were.
    for (size_t k = 0; k < mirrorMaps_.size(); ++k)
        for (size_t i = 0; i < edges.size(); ++i) {
            if (made[i] == kNone) continue;
            const uint32_t ma = mirrorOf(k, edges[i].first), mb = mirrorOf(k, edges[i].second);
            for (size_t j = 0; j < edges.size(); ++j)
                if (made[j] != kNone && ((edges[j].first == ma && edges[j].second == mb) ||
                                         (edges[j].first == mb && edges[j].second == ma)))
                    setMirror(k, made[i], made[j]);
        }
    for (uint32_t v : made)
        if (v != kNone) pushEdgesAround(v, true);
}

void DynamicTopology::splitPass() {
    heap_.clear();
    uint32_t tris[4];
    for (uint32_t t : regionTris_) {
        const glm::uvec3 tri = mesh_->triangles[t];
        for (int c = 0; c < 3; ++c) {
            const uint32_t a = tri[c], b = tri[(c + 1) % 3];
            // Each edge once: from the triangle where it runs upwards (a boundary edge from its only one).
            if (a > b && edgeTriangles(a, b, tris) != 1) continue;
            const float l = len2(a, b);
            if (l > splitLen2_ && inside(a, b)) heap_.push_back({l, std::min(a, b), std::max(a, b)});
        }
    }
    // Under the mirror images: only edges whose image on the brush's side is missing (the others are split
    // with it, by splitOrbit).
    for (uint32_t t : mirrorTris_) {
        const glm::uvec3 tri = mesh_->triangles[t];
        for (int c = 0; c < 3; ++c) {
            const uint32_t a = tri[c], b = tri[(c + 1) % 3];
            if (a > b && edgeTriangles(a, b, tris) != 1) continue;
            const float l = len2(a, b);
            if (l > splitLen2_ && !inside(a, b) && candidate(a, b)) heap_.push_back({l, std::min(a, b), std::max(a, b)});
        }
    }
    auto less = [](const Edge& x, const Edge& y) {
        return x.len2 != y.len2 ? x.len2 < y.len2 : x.a != y.a ? x.a > y.a : x.b > y.b;
    };
    std::make_heap(heap_.begin(), heap_.end(), less);
    size_t tries = 0; // (candidates that fail don't count as operations: bound them too, see kTriesPerOp)
    while (!heap_.empty() && ops_ < maxOps_ && tries++ < maxOps_ * kTriesPerOp) {
        std::pop_heap(heap_.begin(), heap_.end(), less);
        const Edge e = heap_.back();
        heap_.pop_back();
        if (deadV_[e.a] || deadV_[e.b]) continue;
        const int n = edgeTriangles(e.a, e.b, tris);
        if (n < 1 || n > 2 || len2(e.a, e.b) <= splitLen2_) continue;
        if (locked(e.a) || locked(e.b)) continue;
        splitOrbit(e.a, e.b, 0);
    }
}

// Which end of edge a-b goes and where the other one ends up: a free vertex (no feature edges) goes onto a
// feature vertex, or both meet at the midpoint; a vertex on a feature curve (two feature edges) only slides
// along it onto the next vertex of the curve; corners stay.
bool DynamicTopology::collapseTarget(uint32_t a, uint32_t b, uint32_t& keep, uint32_t& drop, glm::vec3& p) const {
    uint32_t oa[2], ob[2];
    const int fa = featureEdges(a, oa), fb = featureEdges(b, ob);
    const auto& pos = mesh_->positions;
    if (fa == 0 && fb == 0) {
        keep = std::min(a, b);
        drop = std::max(a, b);
        p = (pos[a] + pos[b]) * 0.5f;
        return true;
    }
    if (fa == 0) {
        keep = b, drop = a, p = pos[b];
        return true;
    }
    if (fb == 0) {
        keep = a, drop = b, p = pos[a];
        return true;
    }
    if (!featureEdge(a, b)) return false;
    if (fa == 2 && (oa[0] == b || oa[1] == b)) {
        keep = b, drop = a, p = pos[b];
        return true;
    }
    if (fb == 2 && (ob[0] == a || ob[1] == a)) {
        keep = a, drop = b, p = pos[a];
        return true;
    }
    return false;
}

// The topology half of canCollapse: the edge is a manifold one, and merging its ends neither pinches the surface nor
// leaves a vertex with fewer than three neighbours. (Leaves the ends' rings in ringA_ and ringB_.)
bool DynamicTopology::collapseKeepsTopology(uint32_t keep, uint32_t drop) const {
    const Mesh& m = *mesh_;
    uint32_t tris[4];
    const int n = edgeTriangles(keep, drop, tris);
    if (n < 1 || n > 2) return false;
    uint32_t opposite[2] = {kNone, kNone};
    for (int i = 0; i < n; ++i) {
        const glm::uvec3& tri = m.triangles[tris[i]];
        for (int c = 0; c < 3; ++c)
            if (tri[c] != keep && tri[c] != drop) opposite[i] = tri[c];
        if (opposite[i] == kNone) return false;
    }
    if (n == 2 && opposite[0] == opposite[1]) return false;
    std::vector<uint32_t>& rk = ringA_;
    std::vector<uint32_t>& rd = ringB_;
    ring(keep, rk);
    ring(drop, rd);
    // Link condition: the ends share exactly the neighbours across the edge's triangles (else the surface
    // would pinch).
    size_t common = 0;
    for (uint32_t u : rk)
        if (std::binary_search(rd.begin(), rd.end(), u)) {
            if (u != opposite[0] && u != opposite[1]) return false;
            ++common;
        }
    if (common != size_t(n)) return false;
    // Nobody left with fewer than three neighbours (no flat double triangles).
    if (rk.size() + rd.size() - size_t(n) - 2 < 3) return false;
    std::vector<uint32_t>& ro = ringC_;
    for (int i = 0; i < n; ++i) {
        ring(opposite[i], ro);
        if (ro.size() <= 3) return false;
    }
    if (n == 1 && (rk.size() < 3 || rd.size() < 3)) return false;
    return true;
}

bool DynamicTopology::canCollapse(uint32_t keep, uint32_t drop, const glm::vec3& p) const {
    if (!collapseKeepsTopology(keep, drop)) return false;
    const Mesh& m = *mesh_;
    uint32_t tris[4];
    const int n = edgeTriangles(keep, drop, tris);
    const std::vector<uint32_t>& rk = ringA_; // (the rings collapseKeepsTopology just listed)
    const std::vector<uint32_t>& rd = ringB_;
    // No long edge, no triangle turned over or made degenerate or much worse.
    const auto& pos = m.positions;
    // (Only what the collapse makes worse counts: merging two vertices at one point moves nothing.)
    for (uint32_t end : {keep, drop})
        for (uint32_t u : end == keep ? rk : rd)
            if (u != keep && u != drop) {
                const glm::vec3 d = pos[u] - p;
                const float l = glm::dot(d, d);
                if (l >= splitLen2_ && l > len2(u, end) * 1.0001f) return false;
            }
    const float minArea = 1e-4f * target_ * target_;
    for (uint32_t v : {keep, drop})
        for (uint32_t t : m.vertexTriangles(v)) {
            if (t == tris[0] || (n == 2 && t == tris[1])) continue;
            const glm::uvec3& tri = m.triangles[t];
            glm::vec3 q[3], o[3];
            for (int c = 0; c < 3; ++c) {
                o[c] = pos[tri[c]];
                q[c] = tri[c] == keep || tri[c] == drop ? p : o[c];
            }
            const glm::vec3 nOld = glm::cross(o[1] - o[0], o[2] - o[0]);
            const glm::vec3 nNew = glm::cross(q[1] - q[0], q[2] - q[0]);
            const float lNew = glm::length(nNew), lOld = glm::length(nOld);
            if (lNew < minArea && lNew < lOld * 0.999f) return false;
            // (A triangle already squashed flat has no direction to keep: it is being repaired, not turned over.)
            if (lOld >= minArea && glm::dot(nOld, nNew) <= 0.3f * lOld * lNew) return false;
            const float qNew = quality(q[0], q[1], q[2]);
            if (qNew < 0.1f && qNew < quality(o[0], o[1], o[2])) return false;
        }
    return true;
}

void DynamicTopology::collapse(uint32_t keep, uint32_t drop, const glm::vec3& p) {
    Mesh& m = *mesh_;
    uint32_t tris[4];
    const int n = edgeTriangles(keep, drop, tris);
    for (int i = 0; i < n && i < 2; ++i) killTriangle(tris[i]);
    const IndexRing around = m.vertexTriangles(drop);
    std::vector<uint32_t>& list = ringA_;
    list.assign(around.begin(), around.end());
    for (uint32_t t : list) {
        glm::uvec3 tri = m.triangles[t];
        for (int c = 0; c < 3; ++c)
            if (tri[c] == drop) tri[c] = keep;
        setTriangle(t, tri);
    }
    const bool midpoint = p != m.positions[keep];
    if (midpoint) interpolateVertex(keep, keep, drop, 0.5f, p);
    markVertex(keep);
    killVertex(drop);
    ++stats.collapses;
    ++ops_;
}

void DynamicTopology::collapsePass() {
    heap_.clear();
    uint32_t tris[4];
    for (uint32_t t : regionTris_) {
        if (refineOnly) break; // (refine only: slivers alone, below)
        if (t >= mesh_->triangleCount() || deadT_[t]) continue;
        const glm::uvec3 tri = mesh_->triangles[t];
        for (int c = 0; c < 3; ++c) {
            const uint32_t a = tri[c], b = tri[(c + 1) % 3];
            if (a > b && edgeTriangles(a, b, tris) != 1) continue;
            const float l = len2(a, b);
            if (l < collapseLen2_ && inside(a, b)) heap_.push_back({-l, std::min(a, b), std::max(a, b)});
        }
    }
    for (uint32_t t : mirrorTris_) { // (as in splitPass)
        if (refineOnly) break;
        if (t >= mesh_->triangleCount() || deadT_[t]) continue;
        const glm::uvec3 tri = mesh_->triangles[t];
        for (int c = 0; c < 3; ++c) {
            const uint32_t a = tri[c], b = tri[(c + 1) % 3];
            if (a > b && edgeTriangles(a, b, tris) != 1) continue;
            const float l = len2(a, b);
            if (l < collapseLen2_ && !inside(a, b) && candidate(a, b)) heap_.push_back({-l, std::min(a, b), std::max(a, b)});
        }
    }
    for (uint32_t t : slivers_) {
        if (t >= mesh_->triangleCount() || deadT_[t]) continue;
        const glm::uvec3 tri = mesh_->triangles[t];
        for (int c = 0; c < 3; ++c) {
            const uint32_t a = tri[c], b = tri[(c + 1) % 3];
            const float l = len2(a, b);
            if (l < collapseLen2_ && inside(a, b, true)) heap_.push_back({-l, std::min(a, b), std::max(a, b)});
        }
    }
    auto less = [](const Edge& x, const Edge& y) {
        return x.len2 != y.len2 ? x.len2 < y.len2 : x.a != y.a ? x.a > y.a : x.b > y.b;
    };
    std::make_heap(heap_.begin(), heap_.end(), less);
    std::vector<std::pair<uint32_t, uint32_t>> edges;
    struct Plan {
        uint32_t keep, drop;
        glm::vec3 p;
    };
    std::vector<Plan> plans;
    size_t tries = 0;
    while (!heap_.empty() && ops_ < maxOps_ && tries++ < maxOps_ * kTriesPerOp) {
        std::pop_heap(heap_.begin(), heap_.end(), less);
        const Edge e = heap_.back();
        heap_.pop_back();
        if (deadV_[e.a] || deadV_[e.b]) continue;
        const int n = edgeTriangles(e.a, e.b, tris);
        if (n < 1 || n > 2 || len2(e.a, e.b) >= collapseLen2_) continue;
        if (locked(e.a) || locked(e.b)) continue;
        ++stats.collapseTries;
        orbit(e.a, e.b, edges);
        if (edges.size() > 1 && orbitSharesTriangle(edges)) continue; // at the symmetry plane: leave it
        plans.clear();
        bool ok = true;
        for (const auto& [x, y] : edges) {
            Plan pl;
            if (locked(x) || locked(y) || !collapseTarget(x, y, pl.keep, pl.drop, pl.p) || !canCollapse(pl.keep, pl.drop, pl.p)) {
                ok = false;
                break;
            }
            plans.push_back(pl);
        }
        if (!ok) continue;
        if (orbitIncomplete_ && !mirrorMaps_.empty()) ++stats.unpaired;
        for (size_t i = 0; i < plans.size(); ++i) {
            const Plan& pl = plans[i];
            if (deadV_[pl.keep] || deadV_[pl.drop]) continue;
            if (i > 0 && !canCollapse(pl.keep, pl.drop, pl.p)) continue; // (the first changed its surroundings)
            collapse(pl.keep, pl.drop, pl.p);
            pushEdgesAround(pl.keep, false);
        }
    }
}

// A needle: a triangle whose height over its longest edge is under kNeedleWidth of the target edge length (a hair-thin
// strip: its two long edges run side by side). Even triangles are around 0.8 of the target, a sliver a repair pass
// still has to look at is above this.
bool DynamicTopology::needleTriangle(uint32_t t) const {
    const auto& p = mesh_->positions;
    const glm::uvec3& tri = mesh_->triangles[t];
    const glm::vec3 e0 = p[tri.y] - p[tri.x], e1 = p[tri.z] - p[tri.y], e2 = p[tri.x] - p[tri.z];
    const float longest2 = std::max({glm::dot(e0, e0), glm::dot(e1, e1), glm::dot(e2, e2)});
    const float area2 = glm::length(glm::cross(e0, -e2));
    // (height = area2 / longest edge)
    const float w = kNeedleWidth * target_;
    return area2 * area2 < w * w * longest2;
}

// Smooth with Dynamic on: a hair-thin strand (a Tendril pulled long and thin, then smoothed) has edges far longer than the
// collapse length, so the collapse pass never takes them, and smoothing a straight line of vertices moves nothing. A vertex
// whose every triangle is a needle is merged onto a neighbour (the one with the most ordinary triangles: the strand's base,
// which does not move), from the strand's end inwards, until it is gone. Anything with a solid triangle around it is left.
void DynamicTopology::dissolvePass() {
    std::vector<uint32_t> work;
    for (uint32_t t : regionTris_) {
        if (t >= mesh_->triangleCount() || deadT_[t] || !needleTriangle(t)) continue;
        const glm::uvec3 tri = mesh_->triangles[t];
        for (int c = 0; c < 3; ++c)
            if (inside(tri[c], tri[c])) work.push_back(tri[c]);
    }
    if (work.empty()) return;
    std::sort(work.begin(), work.end());
    work.erase(std::unique(work.begin(), work.end()), work.end());
    auto allNeedles = [&](uint32_t v) {
        for (uint32_t t : mesh_->vertexTriangles(v))
            if (!needleTriangle(t)) return false;
        return true;
    };
    std::vector<uint32_t> neighbours;
    std::vector<std::pair<uint32_t, uint32_t>> edges;
    size_t tries = 0;
    for (size_t i = 0; i < work.size() && ops_ < maxOps_ && tries < maxOps_ * kTriesPerOp; ++i) {
        const uint32_t v = work[i];
        if (deadV_[v] || locked(v) || !allNeedles(v)) continue;
        uint32_t other[2];
        if (featureEdges(v, other) != 0) continue;
        ++tries;
        ring(v, neighbours);
        // The neighbour with the most ordinary triangles first (ties: the lowest index, so the result is the same on any thread count).
        std::vector<std::pair<int, uint32_t>> order;
        for (uint32_t u : neighbours) {
            int solid = 0;
            for (uint32_t t : mesh_->vertexTriangles(u)) solid += needleTriangle(t) ? 0 : 1;
            order.emplace_back(-solid, u);
        }
        std::sort(order.begin(), order.end());
        for (const auto& [solid, u] : order) {
            if (locked(u) || !collapseKeepsTopology(u, v)) continue;
            orbit(v, u, edges);
            if (edges.size() > 1 && orbitSharesTriangle(edges)) break; // at the symmetry plane: leave it
            bool ok = true;
            for (const auto& [x, y] : edges)
                ok = ok && !deadV_[x] && !deadV_[y] && !locked(x) && !locked(y) && allNeedles(x) && collapseKeepsTopology(y, x);
            if (!ok) continue;
            if (orbitIncomplete_ && !mirrorMaps_.empty()) ++stats.unpaired;
            for (size_t k = 0; k < edges.size(); ++k) {
                const auto [x, y] = edges[k];
                if (deadV_[x] || deadV_[y] || (k > 0 && !collapseKeepsTopology(y, x))) continue; // (the first changed its surroundings)
                collapse(y, x, mesh_->positions[y]);
                ++stats.dissolved;
                ring(y, neighbours); // (what the strand's next vertex sees now)
                work.insert(work.end(), neighbours.begin(), neighbours.end());
            }
            break;
        }
    }
}

// Flipping a-b (between triangles a,b,c and b,a,d) to c-d: worth it when it brings the four valences closer
// to 6 (4 on a boundary) without folding or flattening the surface.
bool DynamicTopology::flipGain(uint32_t a, uint32_t b, uint32_t& c, uint32_t& d) const {
    const Mesh& m = *mesh_;
    uint32_t tris[4];
    if (edgeTriangles(a, b, tris) != 2 || featureEdge(a, b)) return false;
    c = d = kNone;
    for (int i = 0; i < 2; ++i) {
        const glm::uvec3& tri = m.triangles[tris[i]];
        int k = 0;
        while (k < 3 && tri[k] != a) ++k;
        if (k == 3) return false;
        const uint32_t next = tri[(k + 1) % 3], third = tri[(k + 2) % 3];
        if (next == b) c = third; // a -> b: the triangle a, b, c
        else d = next;            // b -> a: the triangle b, a, d
    }
    if (c == kNone || d == kNone || c == d) return false;
    uint32_t tmp[4];
    if (edgeTriangles(c, d, tmp) > 0) return false;
    const auto& p = m.positions;
    const glm::vec3 n1 = glm::cross(p[b] - p[a], p[c] - p[a]), n2 = glm::cross(p[a] - p[b], p[d] - p[b]);
    const float l1 = glm::length(n1), l2 = glm::length(n2);
    const glm::vec3 m1 = glm::cross(p[a] - p[c], p[d] - p[c]), m2 = glm::cross(p[b] - p[d], p[c] - p[d]);
    const float k1 = glm::length(m1), k2 = glm::length(m2);
    const float minArea = 1e-4f * target_ * target_;
    if (k1 < minArea || k2 < minArea) return false;
    const float qOld = std::min(quality(p[a], p[b], p[c]), quality(p[b], p[a], p[d]));
    const float qNew = std::min(quality(p[c], p[a], p[d]), quality(p[d], p[b], p[c]));
    const glm::vec3 nSum = n1 + n2;
    if (glm::dot(m1, nSum) <= 0.0f || glm::dot(m2, nSum) <= 0.0f) return false;
    // A sliver (a triangle squashed flat, its normal meaningless) is flipped whenever that makes both clearly
    // better, whatever the valences: collapses can't always reach it, and splitting it only makes more.
    if (qOld < 0.05f) {
        ring(a, ringC_);
        if (ringC_.size() <= 3) return false;
        ring(b, ringC_);
        if (ringC_.size() <= 3) return false;
        return qNew > 2.0f * qOld + 0.02f && glm::dot(m1, m2) > 0.5f * k1 * k2;
    }
    if (glm::dot(n1, n2) <= 0.94f * l1 * l2) return false; // a crease: flipping would change the shape
    if (glm::dot(m1, m2) <= 0.9f * k1 * k2) return false;
    std::vector<uint32_t>& r = ringC_;
    auto valence = [&](uint32_t v, int& target) {
        ring(v, r);
        target = boundaryVertex(v) ? 4 : 6;
        return int(r.size());
    };
    int ta, tb, tc, td;
    const int va = valence(a, ta), vb = valence(b, tb), vc = valence(c, tc), vd = valence(d, td);
    if (va <= 3 || vb <= 3) return false;
    auto sq = [](int x) { return x * x; };
    const int before = sq(va - ta) + sq(vb - tb) + sq(vc - tc) + sq(vd - td);
    const int after = sq(va - 1 - ta) + sq(vb - 1 - tb) + sq(vc + 1 - tc) + sq(vd + 1 - td);
    if (after >= before) return false;
    return qNew >= 0.5f * qOld;
}

void DynamicTopology::flip(uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
    uint32_t tris[4];
    if (edgeTriangles(a, b, tris) != 2) return;
    const Mesh& m = *mesh_;
    uint32_t t1 = tris[0], t2 = tris[1];
    // t1 holds a -> b.
    {
        const glm::uvec3& tri = m.triangles[t1];
        int k = 0;
        while (k < 3 && tri[k] != a) ++k;
        if (tri[(k + 1) % 3] != b) std::swap(t1, t2);
    }
    setTriangle(t1, glm::uvec3(c, a, d));
    setTriangle(t2, glm::uvec3(d, b, c));
    ++stats.flips;
    ++ops_;
}

void DynamicTopology::flipPass() {
    // The interior edges of the triangles around changed vertices inside the brush, in a fixed order.
    std::vector<std::pair<uint32_t, uint32_t>> candidates;
    for (uint32_t v : changedV_) {
        if (deadV_[v]) continue;
        for (uint32_t t : mesh_->vertexTriangles(v)) {
            const glm::uvec3& tri = mesh_->triangles[t];
            for (int c = 0; c < 3; ++c) {
                const uint32_t a = tri[c], b = tri[(c + 1) % 3];
                if (a < b && candidate(a, b)) candidates.emplace_back(a, b);
            }
        }
    }
    // And the longest edge of every sliver under the brush and its mirror images (a brush can squash triangles
    // flat too).
    for (uint32_t t : slivers_) {
        if (t >= mesh_->triangleCount() || deadT_[t]) continue;
        const glm::uvec3& tri = mesh_->triangles[t];
        const auto& p = mesh_->positions;
        if (quality(p[tri.x], p[tri.y], p[tri.z]) >= 0.05f) continue;
        int longest = 0;
        for (int c = 1; c < 3; ++c)
            if (len2(tri[c], tri[(c + 1) % 3]) > len2(tri[longest], tri[(longest + 1) % 3])) longest = c;
        const uint32_t a = tri[longest], b = tri[(longest + 1) % 3];
        candidates.emplace_back(std::min(a, b), std::max(a, b));
    }
    std::sort(candidates.begin(), candidates.end());
    candidates.erase(std::unique(candidates.begin(), candidates.end()), candidates.end());
    std::vector<std::pair<uint32_t, uint32_t>> edges;
    struct Plan {
        uint32_t a, b, c, d;
    };
    std::vector<Plan> plans;
    size_t tries = 0;
    for (const auto& [a0, b0] : candidates) {
        if (ops_ >= maxOps_ || tries++ >= maxOps_ * kTriesPerOp) break;
        if (deadV_[a0] || deadV_[b0] || locked(a0) || locked(b0)) continue;
        uint32_t c, d;
        if (!flipGain(a0, b0, c, d)) continue;
        orbit(a0, b0, edges);
        if (edges.size() > 1 && orbitSharesTriangle(edges)) continue;
        plans.clear();
        bool ok = true;
        for (const auto& [x, y] : edges) {
            Plan pl{x, y, 0, 0};
            if (locked(x) || locked(y) || !flipGain(x, y, pl.c, pl.d)) {
                ok = false;
                break;
            }
            plans.push_back(pl);
        }
        if (!ok) continue;
        if (orbitIncomplete_ && !mirrorMaps_.empty()) ++stats.unpaired;
        for (const Plan& pl : plans) flip(pl.a, pl.b, pl.c, pl.d);
    }
}

// Moves each changed vertex inside the brush halfway to the centre of its neighbours, along the surface
// (free vertices only), which evens out the triangles the splits and collapses made.
void DynamicTopology::relaxPass() {
    Mesh& m = *mesh_;
    std::vector<std::pair<uint32_t, glm::vec3>> moves;
    std::vector<uint32_t>& r = ringC_;
    uint32_t other[2];
    const float r2 = radius_ * radius_;
    for (uint32_t v : changedV_) {
        if (deadV_[v] || locked(v)) continue;
        if (refineOnly && v < startVertices_) continue; // (refine only: the vertices that were there stay put; the new ones settle)
        bool in = glm::dot(m.positions[v] - center_, m.positions[v] - center_) <= r2;
        for (const SymCopy& s : mirrorSigns_) {
            const glm::vec3 d0 = m.positions[v] - s.point(center_);
            in = in || glm::dot(d0, d0) <= r2;
        }
        if (!in) continue;
        if (featureEdges(v, other) != 0) continue;
        ring(v, r);
        if (r.size() < 3) continue;
        glm::vec3 avg(0.0f);
        for (uint32_t u : r) avg += m.positions[u];
        avg /= float(r.size());
        const glm::vec3 n = faceNormalAt(v);
        glm::vec3 d = avg - m.positions[v];
        d -= n * glm::dot(d, n);
        moves.emplace_back(v, d * 0.5f);
    }
    // Applied one by one, each only if no triangle around it turns over or goes flat.
    const float minArea = 1e-4f * target_ * target_;
    for (const auto& [v, d] : moves) {
        glm::vec3 to = m.positions[v] + d;
        if (refineOnly) {
            // Refine only: the vertex slides on the surface itself, not in its tangent plane: the target's closest point on
            // the fan around v (the fan is the surface there), so a refresh never lifts the new vertices off the sculpt.
            float best = std::numeric_limits<float>::max();
            glm::vec3 on = to;
            for (uint32_t t : m.vertexTriangles(v)) {
                const glm::uvec3& tri = m.triangles[t];
                glm::vec3 bary;
                const glm::vec3 p = closestPointOnTriangle(to, m.positions[tri.x], m.positions[tri.y], m.positions[tri.z], bary);
                const float d2 = glm::dot(p - to, p - to);
                if (d2 < best) best = d2, on = p;
            }
            to = on;
        }
        bool ok = true;
        for (uint32_t t : m.vertexTriangles(v)) {
            const glm::uvec3& tri = m.triangles[t];
            glm::vec3 o[3], q[3];
            for (int c = 0; c < 3; ++c) {
                o[c] = m.positions[tri[c]];
                q[c] = tri[c] == v ? to : o[c];
            }
            const glm::vec3 nOld = glm::cross(o[1] - o[0], o[2] - o[0]), nNew = glm::cross(q[1] - q[0], q[2] - q[0]);
            const float lNew = glm::length(nNew);
            if (lNew < minArea || glm::dot(nOld, nNew) <= 0.5f * glm::length(nOld) * lNew) {
                ok = false;
                break;
            }
        }
        if (!ok) continue;
        saveVertexUvs(v);
        recordVertex(v);
        m.positions[v] = to;
        if (!m.base.empty()) m.base[v] += d;
    }
}

void DynamicTopology::gatherRegion() {
    queryRegion();
    if (mirrorSigns_.empty()) return;
    // Every vertex of the triangles under each mirror image of the brush, to pair up.
    std::vector<uint32_t>& verts = regionVerts_;
    std::vector<uint32_t> tris;
    verts.clear();
    if (seen_.size() < mesh_->vertexCount()) seen_.resize(mesh_->vertexCount() + mesh_->vertexCount() / 4, 0);
    auto addCorners = [&](const std::vector<uint32_t>& ts) {
        for (uint32_t t : ts)
            for (int c = 0; c < 3; ++c) {
                const uint32_t v = mesh_->triangles[t][c];
                if (seen_[v] == mirrorStamp_) continue;
                seen_[v] = mirrorStamp_;
                verts.push_back(v);
            }
    };
    // With a margin around each image of the brush: the mirror images of the corners found under one must be
    // among those found under the others, though the sides never match exactly.
    // (With the region cut the margin is the mesh's own size: the target edge of a big brush would bring the disc back.)
    const float reach = radius_ * 1.25f + (cut_ ? std::min(target_, 4.0f * meshEdge_) : target_) * 2.0f;
    bvh_->queryTriangles(*mesh_, center_, reach, tris);
    addCorners(tris);
    for (const SymCopy& s : mirrorSigns_) {
        bvh_->queryTriangles(*mesh_, s.point(center_), reach, tris);
        addCorners(tris);
    }
    // Matched within a third of the edge length (the target, or the mesh's own where it is finer, so the
    // search grid's cells hold a vertex or two however fine the mesh is).
    std::vector<float> lengths;
    const size_t stride = std::max<size_t>(1, regionTris_.size() / 1024);
    for (size_t i = 0; i < regionTris_.size(); i += stride) {
        const glm::uvec3& tri = mesh_->triangles[regionTris_[i]];
        lengths.push_back(glm::distance(mesh_->positions[tri.x], mesh_->positions[tri.y]));
    }
    float typical = target_;
    if (!lengths.empty()) {
        std::nth_element(lengths.begin(), lengths.begin() + std::ptrdiff_t(lengths.size() / 2), lengths.end());
        typical = std::min(typical, lengths[lengths.size() / 2]);
    }
    eps_ = std::max(typical * 0.3f, 1e-7f);
    matchMirrors(verts);
}

// A dab remeshes at most maxRegionTriangles triangles, the ones nearest its centre: radius_ shrinks to hold them.
// Finding the region, pairing its mirror images and building the edge heaps cost time in proportion to it, and a
// dab does at most maxOps_ operations whatever its size, so a big brush over a dense Part (803k triangles under a
// brush as big as the head: 100-380 ms a dab, a stalled first stroke) spent most of every dab looking at triangles
// it would not touch. The rest of the brush's disc is remeshed by the dabs that follow, as the brush moves and
// as the operations the cap leaves undone come round again; the brush itself still moves the whole disc.
void DynamicTopology::limitRegion() {
    bvh_->queryTriangles(*mesh_, center_, radius_, regionTris_);
    const auto& p = mesh_->positions;
    // The mesh's own edge length under the brush (a sample): the margins below are in it, not in the target edge, which
    // a big brush makes many triangles long.
    float edge = 0.0f;
    size_t sampled = 0;
    for (size_t i = 0; i < regionTris_.size(); i += regionTris_.size() / 64 + 1, ++sampled) {
        const glm::uvec3& tri = mesh_->triangles[regionTris_[i]];
        edge += glm::distance(p[tri.x], p[tri.y]);
    }
    meshEdge_ = edge / float(std::max<size_t>(sampled, 1));
    cut_ = false;
    if (regionTris_.size() <= maxRegionTriangles) return;
    cut_ = true;
    std::vector<float>& d2 = capDist_;
    d2.clear();
    d2.reserve(regionTris_.size());
    for (uint32_t t : regionTris_) {
        const glm::uvec3& tri = mesh_->triangles[t];
        const glm::vec3 c = (p[tri.x] + p[tri.y] + p[tri.z]) * (1.0f / 3.0f) - center_;
        d2.push_back(glm::dot(c, c));
    }
    std::nth_element(d2.begin(), d2.begin() + std::ptrdiff_t(maxRegionTriangles), d2.end());
    const float nearest = std::sqrt(d2[maxRegionTriangles]) + 2.0f * meshEdge_; // (a triangle or two past the last centre kept)
    radius_ = std::min(radius_, nearest);
    ++stats.limited;
}

bool DynamicTopology::remesh(Mesh& mesh, BVH& bvh, const glm::vec3& center, float radius, float edgeLength,
                             std::span<const SymCopy> mirrors) {
    const auto clock = std::chrono::steady_clock::now();
    moved_.clear();
    recordedNow_.clear();
    if (!dyntopoBlocker(mesh).empty() || !(edgeLength > 0.0f) || !(radius > 0.0f)) return false;
    if (mesh_ && mesh_ != &mesh) return false; // (another mesh mid-stroke)
    if (!mesh.uvs.empty() && mesh.uvs.size() != mesh.triangleCount() * 3) return false; // (UVs that don't fit: leave it alone)
    if (!mesh_) {
        // A former subdivision level (its levels deleted) may still derive its adjacency from them: store it.
        if (mesh.implicitAdjacency()) mesh.rebuildTopology();
        if (!mesh.beginDynamicAdjacency()) return false;
        mesh_ = &mesh;
        bvh_ = &bvh;
        startVertices_ = uint32_t(mesh.vertexCount());
        startTriangles_ = uint32_t(mesh.triangleCount());
        undo_ = std::make_unique<TopologyUndo>();
        vertRec_.assign((mesh.vertexCount() + 63) / 64, 0);
        triRec_.assign((mesh.triangleCount() + 63) / 64, 0);
        deadV_.assign(mesh.vertexCount(), 0);
        deadT_.assign(mesh.triangleCount(), 0);
        changedVFlag_.assign(mesh.vertexCount(), 0);
        // The charts and the footprint are the ones the last stroke left while nothing else has touched the UVs (a footprint made
        // again each stroke would take in the last one's strays, and drawn-in triangles would split the charts).
        if (mesh.uvs.empty()) {
            std::vector<uint32_t>().swap(chartOf_);
            std::vector<uint8_t>().swap(uvMask_);
        } else if (chartOf_.size() != mesh.triangleCount() || uvMask_.empty() || uvHash_ != hashUvs(mesh)) {
            labelCharts();
            buildUvMask();
        }
    }
    center_ = center;
    radius_ = radius;
    target_ = edgeLength;
    // Refine only: an edge counts as stretched at 1.5 times the target (a surface dynamic topology made has edges up to
    // 4/3 of its target, and an even sphere's vary by a quarter: neither is "stretched"), else 4/3 as always.
    const float splitAt = refineOnly ? 1.5f : 4.0f / 3.0f;
    splitLen2_ = (edgeLength * splitAt) * (edgeLength * splitAt);
    collapseLen2_ = (edgeLength * 0.6f) * (edgeLength * 0.6f);
    mirrorSigns_.assign(mirrors.size() > 1 ? mirrors.begin() + 1 : mirrors.end(), mirrors.end());
    // Each copy's inverse (a mirror is its own; a radial turn's is the opposite turn), without the identity.
    mirrorInverse_.clear();
    if (!mirrorSigns_.empty()) {
        const std::vector<size_t> inv = symmetryInverses(mirrors);
        for (size_t k = 1; k < inv.size(); ++k) mirrorInverse_.push_back(inv[k] > 0 ? inv[k] - 1 : k - 1);
    }
    mirrorMaps_.resize(mirrorSigns_.size());
    if (++mirrorStamp_ == 0) { // (wrapped: forget every old entry)
        for (MirrorMap& map : mirrorMaps_) std::fill(map.stamp.begin(), map.stamp.end(), 0);
        mirrorStamp_ = 1;
    }
    ops_ = 0;
    maxOps_ = 6000; // (bounds a dab's time: a big brush over fine triangles coarsens them over several dabs)
    limitRegion(); // (and so does the region it looks at: the work follows what it does, not the brush size)
    beginUvDab();

    auto lap = std::chrono::steady_clock::now();
    auto phase = [&](double& into) {
        const auto now = std::chrono::steady_clock::now();
        into += std::chrono::duration<double, std::milli>(now - lap).count();
        lap = now;
    };
    gatherRegion();
    phase(stats.gatherMs);
    if (regionTris_.empty()) return false;
    stats.maxRegion = std::max(stats.maxRegion, regionTris_.size());
    splitPass();
    bvh.refitChanged(mesh);
    phase(stats.splitMs);
    queryRegion();
    phase(stats.gatherMs);
    collapsePass();
    if (dissolveNeedles) dissolvePass();
    phase(stats.collapseMs);
    flipPass();
    phase(stats.flipMs);
    relaxPass();
    transferUvs();
    const bool changed = !changedV_.empty() || !changedT_.empty();
    compact();
    stats.maxOps = std::max(stats.maxOps, ops_);
    phase(stats.relaxMs);

    // Rings and normals of everything that changed, the BVH's boxes, and what the renderer must patch.
    const uint32_t nv = uint32_t(mesh.vertexCount());
    for (uint32_t v : changedV_)
        if (v < changedVFlag_.size()) changedVFlag_[v] = 0;
    std::vector<std::pair<uint32_t, uint32_t>> moves(moved_.begin(), moved_.end());
    std::sort(moves.begin(), moves.end());
    std::vector<uint32_t> verts;
    verts.reserve(changedV_.size());
    for (uint32_t v : changedV_) {
        if (v < nv) {
            verts.push_back(v);
            continue;
        }
        // (Past the end now: moved down, or removed.)
        const auto it = std::lower_bound(moves.begin(), moves.end(), std::make_pair(v, 0u));
        if (it != moves.end() && it->first == v) verts.push_back(it->second);
    }
    std::sort(verts.begin(), verts.end());
    verts.erase(std::unique(verts.begin(), verts.end()), verts.end());
    changedV_.clear();
    changedVFlag_.resize(nv, 0);
    for (uint32_t v : verts) {
        ring(v, scratch2_);
        mesh.setNeighbors(v, scratch2_);
        mesh.markDirty(v);
        for (uint32_t t : mesh.vertexTriangles(v)) bvh.touchTriangle(t); // (moved vertices: relaxed, collapsed)
    }
    mesh.updateNormals(verts);
    bvh.refitChanged(mesh);
    mesh.changedTriangles.insert(mesh.changedTriangles.end(), changedT_.begin(), changedT_.end());
    changedT_.clear();
    phase(stats.finishMs);
    lastMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - clock).count();
    return changed;
}

std::unique_ptr<TopologyUndo> DynamicTopology::finish(Mesh& mesh, BVH& bvh) {
    if (!mesh_) return nullptr;
    mesh.endDynamicAdjacency();
    if (bvh.degraded()) bvh.build(mesh);
    std::unique_ptr<TopologyUndo> undo = std::move(undo_);
    undo->vertexCount = startVertices_;
    undo->triangleCount = startTriangles_;
    mesh_ = nullptr;
    bvh_ = nullptr;
    std::vector<uint64_t>().swap(vertRec_);
    std::vector<uint64_t>().swap(triRec_);
    std::vector<uint8_t>().swap(deadV_);
    std::vector<uint8_t>().swap(deadT_);
    std::vector<uint8_t>().swap(changedVFlag_);
    // (The mirror maps and the vertex stamps stay for the next stroke: their stamps make them empty, and
    // sized for the whole mesh they would cost a pass over it to make again.)
    std::vector<uint64_t>().swap(gridKeys_);
    std::vector<uint32_t>().swap(gridHead_);
    std::vector<uint32_t>().swap(gridNext_);
    std::vector<uint32_t>().swap(regionVerts_);
    if (!mesh.uvs.empty()) uvHash_ = hashUvs(mesh); // (chartOf_ and uvMask_ stay for the next stroke)
    uvOn_ = false;
    if (undo->vertexSlots.empty() && undo->triangleSlots.empty()) return nullptr;
    return undo;
}

} // namespace sf
