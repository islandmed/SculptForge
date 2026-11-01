// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/Uv.h"

#include "core/Mesh.h"
#include "core/ThreadPool.h"
#include "core/UvInternal.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <queue>
#include <sstream>

namespace sf {

using namespace uvdetail;

namespace uvdetail {

std::vector<glm::vec2> convexHull(std::vector<glm::vec2> pts) {
    std::sort(pts.begin(), pts.end(), [](glm::vec2 a, glm::vec2 b) { return a.x < b.x || (a.x == b.x && a.y < b.y); });
    pts.erase(std::unique(pts.begin(), pts.end()), pts.end());
    if (pts.size() < 3) return pts;
    std::vector<glm::vec2> h(pts.size() * 2);
    size_t k = 0;
    for (size_t i = 0; i < pts.size(); ++i) {
        while (k >= 2 && cross2(h[k - 1] - h[k - 2], pts[i] - h[k - 2]) <= 0.0f) --k;
        h[k++] = pts[i];
    }
    for (size_t i = pts.size() - 1, lo = k + 1; i-- > 0;) {
        while (k >= lo && cross2(h[k - 1] - h[k - 2], pts[i] - h[k - 2]) <= 0.0f) --k;
        h[k++] = pts[i];
    }
    h.resize(k - 1);
    return h;
}

float minAreaRectAngle(const std::vector<glm::vec2>& pts) {
    const std::vector<glm::vec2> h = convexHull(pts);
    if (h.size() < 3) {
        if (h.size() == 2) return -std::atan2(h[1].y - h[0].y, h[1].x - h[0].x);
        return 0.0f;
    }
    // Rotating calipers: the smallest box has a side on a hull edge.
    float bestArea = std::numeric_limits<float>::max(), best = 0.0f;
    for (size_t i = 0; i < h.size(); ++i) {
        const glm::vec2 e = h[(i + 1) % h.size()] - h[i];
        const float len = glm::length(e);
        if (len <= 0.0f) continue;
        const float c = e.x / len, s = -e.y / len; // rotation by -angle(e)
        glm::vec2 lo(std::numeric_limits<float>::max()), hi(std::numeric_limits<float>::lowest());
        for (const glm::vec2& p : h) {
            const glm::vec2 q = rotate(p, c, s);
            lo = glm::min(lo, q);
            hi = glm::max(hi, q);
        }
        const glm::vec2 d = hi - lo;
        const float area = d.x * d.y;
        if (area < bestArea * 0.9999f) {
            bestArea = area;
            best = std::atan2(s, c);
            if (d.y > d.x) best += 1.5707963f; // long side along u
        }
    }
    return best;
}

void islandCorners(const UvTopology& topo, std::vector<uint32_t>& offsets, std::vector<uint32_t>& corners) {
    offsets.assign(topo.islandCount + 1, 0);
    for (uint32_t i = 0; i < topo.islandCount; ++i)
        offsets[i + 1] = offsets[i] + 3 * (topo.islandOffsets[i + 1] - topo.islandOffsets[i]);
    corners.resize(offsets.back());
    poolFor(topo.islandCount, [&](size_t b, size_t e) {
        for (size_t i = b; i < e; ++i) {
            uint32_t at = offsets[i];
            for (uint32_t k = topo.islandOffsets[i]; k < topo.islandOffsets[i + 1]; ++k) {
                const uint32_t t = topo.islandTriangles[k];
                for (uint32_t j = 0; j < 3; ++j) corners[at++] = t * 3 + j;
            }
        }
    }, 64);
}

} // namespace uvdetail

int udimTile(const glm::vec2& uv) {
    const int u = std::clamp(int(std::floor(uv.x)), 0, 9);
    const int v = std::max(int(std::floor(uv.y)), 0);
    return 1001 + u + 10 * v;
}

glm::vec2 udimOrigin(int tile) {
    const int i = std::max(tile - 1001, 0);
    return {float(i % 10), float(i / 10)};
}

std::vector<uint32_t> cornerTwins(const Mesh& m) {
    const size_t nt = m.triangleCount(), nv = m.vertexCount();
    std::vector<uint32_t> twin(nt * 3, kNoCorner);
    if (nt == 0) return twin;
    // Corners bucketed by the lower vertex of their edge (a counting sort), then matched within each bucket.
    std::vector<uint32_t> offsets(nv + 1, 0);
    auto lower = [&](uint32_t c) {
        const glm::uvec3& t = m.triangles[c / 3];
        return std::min(t[c % 3], t[(c % 3 + 1) % 3]);
    };
    for (uint32_t c = 0; c < nt * 3; ++c) ++offsets[lower(c) + 1];
    for (size_t v = 0; v < nv; ++v) offsets[v + 1] += offsets[v];
    std::vector<uint32_t> fill(offsets.begin(), offsets.end() - 1), list(nt * 3);
    for (uint32_t c = 0; c < nt * 3; ++c) list[fill[lower(c)]++] = c;
    poolFor(nv, [&](size_t b, size_t e) {
        for (size_t v = b; v < e; ++v) {
            const uint32_t* first = list.data() + offsets[v];
            const uint32_t n = offsets[v + 1] - offsets[v];
            for (uint32_t i = 0; i < n; ++i) {
                const uint32_t c = first[i];
                const glm::uvec3& tc = m.triangles[c / 3];
                const uint32_t a = tc[c % 3], bb = tc[(c % 3 + 1) % 3];
                uint32_t match = kNoCorner, same = 0;
                for (uint32_t j = 0; j < n; ++j) {
                    if (j == i) continue;
                    const uint32_t d = first[j];
                    const glm::uvec3& td = m.triangles[d / 3];
                    const uint32_t da = td[d % 3], db = td[(d % 3 + 1) % 3];
                    if (da == bb && db == a) {
                        match = match == kNoCorner ? d : kNoCorner - 1; // two opposite: non-manifold
                    } else if (da == a && db == bb) {
                        ++same;
                    }
                }
                twin[c] = (same == 0 && match < kNoCorner - 1) ? match : kNoCorner;
            }
        }
    }, 1 << 12);
    // Keep only mutual pairs.
    for (uint32_t c = 0; c < nt * 3; ++c)
        if (twin[c] != kNoCorner && twin[twin[c]] != c) twin[c] = kNoCorner;
    return twin;
}

UvTopology buildUvTopology(const Mesh& m, std::vector<uint32_t> twin) {
    UvTopology topo;
    const size_t nt = m.triangleCount(), nc = nt * 3;
    if (twin.size() != nc) twin = cornerTwins(m);
    topo.twin = std::move(twin);
    topo.border.assign(nc, 1);
    const bool uvs = m.uvs.size() == nc;
    const bool flags = m.uvFlags.size() == nc;
    poolFor(nc, [&](size_t b, size_t e) {
        for (size_t c = b; c < e; ++c) {
            const uint32_t d = topo.twin[c];
            if (d == kNoCorner) continue;
            if (flags && ((m.uvFlags[c] | m.uvFlags[d]) & kUvSeam)) continue;
            if (uvs && (m.uvs[c] != m.uvs[cornerNext(d)] || m.uvs[cornerNext(uint32_t(c))] != m.uvs[d])) continue;
            topo.border[c] = 0;
        }
    }, 1 << 15);
    DisjointSets corners(nc), tris(nt);
    for (uint32_t c = 0; c < nc; ++c) {
        if (topo.border[c]) continue;
        const uint32_t d = topo.twin[c];
        if (d < c) continue;
        corners.unite(c, cornerNext(d));
        corners.unite(cornerNext(c), d);
        tris.unite(c / 3, d / 3);
    }
    topo.uvVertexCount = corners.labels(topo.uvVertex);
    topo.islandCount = tris.labels(topo.island);
    topo.islandOffsets.assign(topo.islandCount + 1, 0);
    for (uint32_t t = 0; t < nt; ++t) ++topo.islandOffsets[topo.island[t] + 1];
    for (uint32_t i = 0; i < topo.islandCount; ++i) topo.islandOffsets[i + 1] += topo.islandOffsets[i];
    std::vector<uint32_t> at(topo.islandOffsets.begin(), topo.islandOffsets.end() - 1);
    topo.islandTriangles.resize(nt);
    for (uint32_t t = 0; t < nt; ++t) topo.islandTriangles[at[topo.island[t]]++] = t;
    return topo;
}

// ---------------------------------------------------------------------------------------------------------------
// Measures

namespace {

float area3(const Mesh& m, uint32_t t) {
    const glm::uvec3& tri = m.triangles[t];
    return 0.5f * glm::length(glm::cross(m.positions[tri.y] - m.positions[tri.x], m.positions[tri.z] - m.positions[tri.x]));
}
float signedAreaUv(const Mesh& m, uint32_t t) {
    const glm::vec2* u = m.uvs.data() + size_t(t) * 3;
    return 0.5f * cross2(u[1] - u[0], u[2] - u[0]);
}

} // namespace

void uvTriangleDensity(const Mesh& m, int resolution, std::vector<float>& out) {
    const size_t nt = m.triangleCount();
    out.assign(nt, 0.0f);
    if (m.uvs.size() != nt * 3) return;
    poolFor(nt, [&](size_t b, size_t e) {
        for (size_t t = b; t < e; ++t) {
            const float a = area3(m, uint32_t(t));
            out[t] = a > 0.0f ? std::sqrt(std::abs(signedAreaUv(m, uint32_t(t))) / a) * float(resolution) : 0.0f;
        }
    }, 1 << 14);
}

std::vector<float> uvIslandDensity(const Mesh& m, const UvTopology& topo, int resolution) {
    std::vector<float> out(topo.islandCount, 0.0f);
    if (m.uvs.size() != m.triangleCount() * 3) return out;
    poolFor(topo.islandCount, [&](size_t b, size_t e) {
        for (size_t i = b; i < e; ++i) {
            double a = 0.0, u = 0.0;
            for (uint32_t k = topo.islandOffsets[i]; k < topo.islandOffsets[i + 1]; ++k) {
                const uint32_t t = topo.islandTriangles[k];
                a += area3(m, t);
                u += std::abs(signedAreaUv(m, t));
            }
            out[i] = a > 0.0 ? float(std::sqrt(u / a) * resolution) : 0.0f;
        }
    }, 64);
    return out;
}

float uvMeshDensity(const Mesh& m, int resolution) {
    const size_t nt = m.triangleCount();
    if (m.uvs.size() != nt * 3 || nt == 0) return 0.0f;
    double a = 0.0, u = 0.0;
    for (uint32_t t = 0; t < nt; ++t) {
        a += area3(m, t);
        u += std::abs(signedAreaUv(m, t));
    }
    return a > 0.0 ? float(std::sqrt(u / a) * resolution) : 0.0f;
}

namespace {

// Singular values (s1 >= s2) and orientation of triangle t's map from its surface to UV space.
bool triangleJacobian(const Mesh& m, uint32_t t, float& s1, float& s2, bool& flipped) {
    const glm::uvec3& tri = m.triangles[t];
    const glm::vec3 e1 = m.positions[tri.y] - m.positions[tri.x], e2 = m.positions[tri.z] - m.positions[tri.x];
    const float l1 = glm::length(e1);
    const glm::vec3 n = glm::cross(e1, e2);
    const float nl = glm::length(n);
    if (l1 <= 0.0f || nl <= 0.0f) return false;
    const glm::vec3 x = e1 / l1, y = glm::cross(n / nl, x);
    // Surface triangle in its plane: (0,0), (l1,0), (q2x,q2y).
    const float q2x = glm::dot(e2, x), q2y = glm::dot(e2, y);
    const glm::vec2* u = m.uvs.data() + size_t(t) * 3;
    const glm::vec2 d1 = u[1] - u[0], d2 = u[2] - u[0];
    // J * [l1 q2x; 0 q2y] = [d1 d2]  ->  J = [d1 d2] * inverse.
    if (std::abs(q2y) <= 0.0f) return false;
    const glm::vec2 c0 = d1 / l1;                         // J's first column
    const glm::vec2 c1 = (d2 - c0 * q2x) / q2y;           // J's second column
    const float a = c0.x, b = c1.x, c = c0.y, d = c1.y; // J = [a b; c d]
    const float E = (a + d) * 0.5f, F = (a - d) * 0.5f, G = (c + b) * 0.5f, H = (c - b) * 0.5f;
    const float Q = std::sqrt(E * E + H * H), R = std::sqrt(F * F + G * G);
    s1 = Q + R;
    s2 = std::abs(Q - R);
    flipped = (a * d - b * c) < 0.0f;
    if (!std::isfinite(s1) || !std::isfinite(s2)) return false; // (a sliver: no meaningful stretch)
    return true;
}

} // namespace

void uvTriangleStretch(const Mesh& m, std::vector<float>& out) {
    const size_t nt = m.triangleCount();
    out.assign(nt, 0.0f);
    if (m.uvs.size() != nt * 3 || nt == 0) return;
    // Measured against each island's own scale, so islands packed smaller or larger don't count as stretched; and
    // against its own sense, so an island laid out mirrored as a whole is fine (only triangles folded over the
    // rest of their island count as flipped).
    const UvTopology topo = buildUvTopology(m);
    std::vector<float> scale(topo.islandCount, 0.0f);
    std::vector<uint8_t> mirrored(topo.islandCount, 0);
    poolFor(topo.islandCount, [&](size_t b, size_t e) {
        for (size_t i = b; i < e; ++i) {
            double a = 0.0, u = 0.0, sense = 0.0;
            for (uint32_t k = topo.islandOffsets[i]; k < topo.islandOffsets[i + 1]; ++k) {
                const uint32_t t = topo.islandTriangles[k];
                const float su = signedAreaUv(m, t);
                a += area3(m, t);
                u += std::abs(su);
                sense += su;
            }
            scale[i] = a > 0.0 ? float(std::sqrt(u / a)) : 0.0f;
            mirrored[i] = sense < 0.0;
        }
    }, 64);
    poolFor(nt, [&](size_t b, size_t e) {
        for (size_t t = b; t < e; ++t) {
            float s1 = 0, s2 = 0;
            bool flipped = false;
            const uint32_t i = topo.island[t];
            const float s = scale[i];
            if (s <= 0.0f || !triangleJacobian(m, uint32_t(t), s1, s2, flipped)) continue;
            out[t] = flipped != bool(mirrored[i]) ? -1.0f
                                                  : std::log2(std::max(std::max(s1 / s, 1e-6f), s / std::max(s2, 1e-6f * s)));
        }
    }, 1 << 14);
}

UvStretchCache uvStretchCache(const Mesh& m, const UvTopology& topo) {
    UvStretchCache cache;
    cache.scale.assign(topo.islandCount, 0.0f);
    cache.mirrored.assign(topo.islandCount, 0);
    if (m.uvs.size() != m.triangleCount() * 3) return cache;
    poolFor(topo.islandCount, [&](size_t b, size_t e) {
        for (size_t i = b; i < e; ++i) {
            double a = 0.0, u = 0.0, sense = 0.0;
            for (uint32_t k = topo.islandOffsets[i]; k < topo.islandOffsets[i + 1]; ++k) {
                const uint32_t t = topo.islandTriangles[k];
                const float su = signedAreaUv(m, t);
                a += area3(m, t);
                u += std::abs(su);
                sense += su;
            }
            cache.scale[i] = a > 0.0 ? float(std::sqrt(u / a)) : 0.0f;
            cache.mirrored[i] = sense < 0.0;
        }
    }, 64);
    return cache;
}

float uvTriangleStretchAt(const Mesh& m, uint32_t t, const UvTopology& topo, const UvStretchCache& cache) {
    float s1 = 0, s2 = 0;
    bool flipped = false;
    const uint32_t i = topo.island[t];
    const float s = cache.scale[i];
    if (s <= 0.0f || !triangleJacobian(m, t, s1, s2, flipped)) return 0.0f;
    if (flipped != bool(cache.mirrored[i])) return -1.0f;
    return std::log2(std::max(std::max(s1 / s, 1e-6f), s / std::max(s2, 1e-6f * s)));
}

UvStretchStats uvStretchStats(const Mesh& m) {
    UvStretchStats st;
    std::vector<float> s;
    uvTriangleStretch(m, s);
    double wsum = 0.0, sum = 0.0, flipped = 0.0;
    for (uint32_t t = 0; t < s.size(); ++t) {
        const double w = std::abs(signedAreaUv(m, t));
        wsum += w;
        if (s[t] < 0.0f) {
            flipped += w;
            continue;
        }
        sum += w * s[t];
        st.worst = std::max(st.worst, s[t]);
    }
    if (wsum > 0.0) {
        st.mean = float(sum / wsum);
        st.flipped = float(flipped / wsum);
    }
    return st;
}

glm::vec3 uvHeatColor(float x) {
    x = std::clamp(x, -1.0f, 1.0f);
    const glm::vec3 blue(0.18f, 0.42f, 0.95f), green(0.25f, 0.78f, 0.42f), yellow(0.98f, 0.82f, 0.25f), red(0.93f, 0.26f, 0.22f);
    if (x < 0.0f) return glm::mix(green, blue, -x);
    if (x < 0.5f) return glm::mix(green, yellow, x * 2.0f);
    return glm::mix(yellow, red, (x - 0.5f) * 2.0f);
}

// ---------------------------------------------------------------------------------------------------------------
// Selection

const char* uvSelectModeId(UvSelectMode mode) {
    switch (mode) {
    case UvSelectMode::Vertex: return "vertex";
    case UvSelectMode::Edge: return "edge";
    case UvSelectMode::Face: return "face";
    case UvSelectMode::Island: return "island";
    }
    return "island";
}

bool parseUvSelectMode(const std::string& id, UvSelectMode& mode) {
    for (UvSelectMode m : {UvSelectMode::Vertex, UvSelectMode::Edge, UvSelectMode::Face, UvSelectMode::Island})
        if (id == uvSelectModeId(m) || id == std::string(uvSelectModeId(m)) + "s") {
            mode = m;
            return true;
        }
    return false;
}

bool UvSelection::valid(const Mesh& m) const {
    const bool faceMode = mode == UvSelectMode::Face || mode == UvSelectMode::Island;
    return faceMode ? faces.size() == m.triangleCount() : corners.size() == m.triangleCount() * 3;
}

size_t UvSelection::count() const {
    const bool faceMode = mode == UvSelectMode::Face || mode == UvSelectMode::Island;
    const std::vector<uint8_t>& v = faceMode ? faces : corners;
    return size_t(std::count_if(v.begin(), v.end(), [](uint8_t x) { return x != 0; }));
}

namespace {

// Every corner of every UV vertex marked in `hit` (per UV vertex).
std::vector<uint32_t> cornersOfUvVertices(const UvTopology& topo, const std::vector<uint8_t>& hit) {
    std::vector<uint32_t> out;
    for (uint32_t c = 0; c < topo.uvVertex.size(); ++c)
        if (hit[topo.uvVertex[c]]) out.push_back(c);
    return out;
}

} // namespace

std::vector<uint32_t> uvSelectedCorners(const Mesh& m, const UvTopology& topo, const UvSelection& sel) {
    const size_t nc = m.triangleCount() * 3;
    if (!sel.valid(m) || topo.uvVertex.size() != nc) return {};
    std::vector<uint8_t> hit(topo.uvVertexCount, 0);
    switch (sel.mode) {
    case UvSelectMode::Face:
    case UvSelectMode::Island:
        for (uint32_t t = 0; t < sel.faces.size(); ++t)
            if (sel.faces[t])
                for (uint32_t j = 0; j < 3; ++j) hit[topo.uvVertex[t * 3 + j]] = 1;
        break;
    case UvSelectMode::Vertex:
        for (uint32_t c = 0; c < nc; ++c)
            if (sel.corners[c]) hit[topo.uvVertex[c]] = 1;
        break;
    case UvSelectMode::Edge:
        for (uint32_t c = 0; c < nc; ++c)
            if (sel.corners[c]) hit[topo.uvVertex[c]] = hit[topo.uvVertex[cornerNext(c)]] = 1;
        break;
    }
    return cornersOfUvVertices(topo, hit);
}

std::vector<uint8_t> uvSelectedIslands(const Mesh& m, const UvTopology& topo, const UvSelection& sel, bool allIfEmpty) {
    std::vector<uint8_t> out(topo.islandCount, 0);
    if (sel.valid(m)) {
        if (sel.mode == UvSelectMode::Face || sel.mode == UvSelectMode::Island) {
            for (uint32_t t = 0; t < sel.faces.size(); ++t)
                if (sel.faces[t]) out[topo.island[t]] = 1;
        } else {
            for (uint32_t c = 0; c < sel.corners.size(); ++c)
                if (sel.corners[c]) out[topo.island[c / 3]] = 1;
        }
    }
    if (allIfEmpty && std::find(out.begin(), out.end(), uint8_t(1)) == out.end()) std::fill(out.begin(), out.end(), uint8_t(1));
    return out;
}

void uvConvertSelection(const Mesh& m, const UvTopology& topo, UvSelection& sel, UvSelectMode to) {
    const size_t nt = m.triangleCount(), nc = nt * 3;
    const bool fromFaces = sel.mode == UvSelectMode::Face || sel.mode == UvSelectMode::Island;
    const bool toFaces = to == UvSelectMode::Face || to == UvSelectMode::Island;
    if (!sel.valid(m)) {
        sel.clear();
        sel.mode = to;
        return;
    }
    if (fromFaces && toFaces) {
        if (to == UvSelectMode::Island) {
            const std::vector<uint8_t> isl = uvSelectedIslands(m, topo, sel, false);
            for (uint32_t t = 0; t < nt; ++t) sel.faces[t] = isl[topo.island[t]];
        }
        sel.mode = to;
        return;
    }
    if (fromFaces) { // faces -> their corners (vertex) or their edges (edge)
        std::vector<uint8_t> corners(nc, 0);
        if (to == UvSelectMode::Edge) {
            for (uint32_t t = 0; t < nt; ++t)
                if (sel.faces[t]) corners[t * 3] = corners[t * 3 + 1] = corners[t * 3 + 2] = 1;
        } else {
            for (uint32_t c : uvSelectedCorners(m, topo, sel)) corners[c] = 1;
        }
        sel.corners = std::move(corners);
        std::vector<uint8_t>().swap(sel.faces);
        sel.mode = to;
        return;
    }
    // Corners -> faces whose three UV vertices are all touched (edge mode: their edges' ends).
    std::vector<uint8_t> hit(topo.uvVertexCount, 0);
    for (uint32_t c = 0; c < nc; ++c)
        if (sel.corners[c]) {
            hit[topo.uvVertex[c]] = 1;
            if (sel.mode == UvSelectMode::Edge) hit[topo.uvVertex[cornerNext(c)]] = 1;
        }
    if (toFaces) {
        sel.faces.assign(nt, 0);
        for (uint32_t t = 0; t < nt; ++t)
            sel.faces[t] = hit[topo.uvVertex[t * 3]] && hit[topo.uvVertex[t * 3 + 1]] && hit[topo.uvVertex[t * 3 + 2]];
        std::vector<uint8_t>().swap(sel.corners);
        sel.mode = UvSelectMode::Face;
        if (to == UvSelectMode::Island) uvConvertSelection(m, topo, sel, to);
        return;
    }
    std::vector<uint8_t> corners(nc, 0);
    if (to == UvSelectMode::Vertex) {
        for (uint32_t c = 0; c < nc; ++c) corners[c] = hit[topo.uvVertex[c]];
    } else { // vertex -> edge: edges with both ends touched
        for (uint32_t c = 0; c < nc; ++c) corners[c] = hit[topo.uvVertex[c]] && hit[topo.uvVertex[cornerNext(c)]];
    }
    sel.corners = std::move(corners);
    sel.mode = to;
}

void uvFacesInRect(const Mesh& m, glm::vec2 lo, glm::vec2 hi, bool touch, std::vector<uint32_t>& out) {
    out.clear();
    const size_t nt = m.triangleCount();
    if (m.uvs.size() != nt * 3) return;
    const size_t block = 1 << 14, blocks = (nt + block - 1) / block;
    std::vector<std::vector<uint32_t>> parts(blocks);
    poolFor(nt, [&](size_t b, size_t e) {
        std::vector<uint32_t>& part = parts[b / block];
        for (size_t t = b; t < e; ++t) {
            const glm::vec2* u = m.uvs.data() + t * 3;
            if (m.triangleHidden(uint32_t(t))) continue;
            const glm::vec2 tlo = glm::min(u[0], glm::min(u[1], u[2])), thi = glm::max(u[0], glm::max(u[1], u[2]));
            const bool inside = touch ? (tlo.x <= hi.x && thi.x >= lo.x && tlo.y <= hi.y && thi.y >= lo.y)
                                      : (tlo.x >= lo.x && thi.x <= hi.x && tlo.y >= lo.y && thi.y <= hi.y);
            if (inside) part.push_back(uint32_t(t));
        }
    }, block);
    for (auto& p : parts) out.insert(out.end(), p.begin(), p.end());
}

// ---------------------------------------------------------------------------------------------------------------
// Editing

void uvTransform(std::vector<glm::vec2>& uvs, const std::vector<uint32_t>& corners, const glm::mat2& mat, glm::vec2 pivot,
                 glm::vec2 offset) {
    poolFor(corners.size(), [&](size_t b, size_t e) {
        for (size_t i = b; i < e; ++i) {
            glm::vec2& p = uvs[corners[i]];
            p = mat * (p - pivot) + pivot + offset;
        }
    }, 1 << 15);
}

glm::vec2 uvCenter(const std::vector<glm::vec2>& uvs, const std::vector<uint32_t>& corners) {
    if (corners.empty()) return glm::vec2(0.5f);
    glm::vec2 lo(std::numeric_limits<float>::max()), hi(std::numeric_limits<float>::lowest());
    for (uint32_t c : corners) {
        lo = glm::min(lo, uvs[c]);
        hi = glm::max(hi, uvs[c]);
    }
    return (lo + hi) * 0.5f;
}

namespace {
std::vector<uint8_t>& ensureFlags(Mesh& m) {
    if (m.uvFlags.size() != m.triangleCount() * 3) m.uvFlags.assign(m.triangleCount() * 3, 0);
    return m.uvFlags;
}
void dropEmptyFlags(Mesh& m) {
    if (std::all_of(m.uvFlags.begin(), m.uvFlags.end(), [](uint8_t f) { return f == 0; })) std::vector<uint8_t>().swap(m.uvFlags);
}
} // namespace

void uvSetSeams(Mesh& m, const std::vector<uint32_t>& twin, const std::vector<uint32_t>& corners, bool on) {
    std::vector<uint8_t>& f = ensureFlags(m);
    for (uint32_t c : corners) {
        if (c >= f.size()) continue;
        const uint32_t d = c < twin.size() ? twin[c] : kNoCorner;
        if (on) {
            f[c] |= kUvSeam;
            if (d != kNoCorner) f[d] |= kUvSeam;
        } else {
            f[c] &= uint8_t(~kUvSeam);
            if (d != kNoCorner) f[d] &= uint8_t(~kUvSeam);
        }
    }
    dropEmptyFlags(m);
    m.facesDirty = true;
}

size_t uvSeamsFromPolygroups(Mesh& m, const std::vector<uint32_t>& twin) {
    std::vector<uint32_t> cut;
    for (uint32_t c = 0; c < twin.size(); ++c)
        if (twin[c] != kNoCorner && twin[c] > c && m.polygroupAt(c / 3) != m.polygroupAt(twin[c] / 3)) cut.push_back(c);
    uvSetSeams(m, twin, cut, true);
    return cut.size();
}

size_t uvSeamsFromIslands(Mesh& m, const UvTopology& topo) {
    std::vector<uint32_t> cut;
    for (uint32_t c = 0; c < topo.twin.size(); ++c)
        if (topo.border[c] && topo.twin[c] != kNoCorner && topo.twin[c] > c) cut.push_back(c);
    uvSetSeams(m, topo.twin, cut, true);
    return cut.size();
}

size_t uvSeamsFromAngle(Mesh& m, const std::vector<uint32_t>& twin, float degrees) {
    const float cosLimit = std::cos(glm::radians(std::clamp(degrees, 0.0f, 180.0f)));
    auto normal = [&](uint32_t t) {
        const glm::uvec3& tri = m.triangles[t];
        const glm::vec3 n = glm::cross(m.positions[tri.y] - m.positions[tri.x], m.positions[tri.z] - m.positions[tri.x]);
        const float l = glm::length(n);
        return l > 0.0f ? n / l : glm::vec3(0.0f);
    };
    std::vector<uint32_t> cut;
    for (uint32_t c = 0; c < twin.size(); ++c)
        if (twin[c] != kNoCorner && twin[c] > c && glm::dot(normal(c / 3), normal(twin[c] / 3)) < cosLimit) cut.push_back(c);
    uvSetSeams(m, twin, cut, true);
    return cut.size();
}

size_t uvSeamCount(const Mesh& m) {
    if (m.uvFlags.size() != m.triangleCount() * 3) return 0;
    size_t n = 0;
    for (uint8_t f : m.uvFlags) n += (f & kUvSeam) != 0;
    return (n + 1) / 2; // (open edges are marked on one side only)
}

void uvClearSeams(Mesh& m) {
    if (m.uvFlags.empty()) return;
    for (uint8_t& f : m.uvFlags) f &= uint8_t(~kUvSeam);
    dropEmptyFlags(m);
    m.facesDirty = true;
}

std::vector<uint32_t> uvFaceSetBorder(const Mesh& m, const std::vector<uint32_t>& twin, const std::vector<uint8_t>& faces) {
    std::vector<uint32_t> out;
    if (faces.size() != m.triangleCount()) return out;
    for (uint32_t c = 0; c < twin.size(); ++c) {
        if (!faces[c / 3]) continue;
        const uint32_t d = twin[c];
        if (d != kNoCorner && !faces[d / 3]) out.push_back(c);
    }
    return out;
}

std::vector<uint32_t> uvEdgePath(const Mesh& m, uint32_t a, uint32_t b) {
    const size_t nv = m.vertexCount();
    if (a >= nv || b >= nv || a == b) return {};
    std::vector<float> dist(nv, std::numeric_limits<float>::max());
    std::vector<uint32_t> from(nv, ~0u);
    using Item = std::pair<float, uint32_t>;
    std::priority_queue<Item, std::vector<Item>, std::greater<>> queue;
    dist[a] = 0.0f;
    queue.push({0.0f, a});
    while (!queue.empty()) {
        const auto [d, v] = queue.top();
        queue.pop();
        if (d > dist[v]) continue;
        if (v == b) break;
        for (uint32_t w : m.neighbors(v)) {
            const float nd = d + glm::length(m.positions[w] - m.positions[v]);
            if (nd < dist[w]) {
                dist[w] = nd;
                from[w] = v;
                queue.push({nd, w});
            }
        }
    }
    if (from[b] == ~0u) return {};
    std::vector<uint32_t> out;
    for (uint32_t v = b; v != a; v = from[v]) {
        const uint32_t u = from[v];
        // A corner that starts the edge u -> v or v -> u.
        uint32_t corner = kNoCorner;
        for (uint32_t t : m.vertexTriangles(u)) {
            for (uint32_t j = 0; j < 3 && corner == kNoCorner; ++j) {
                const uint32_t x = m.triangles[t][j], y = m.triangles[t][(j + 1) % 3];
                if ((x == u && y == v) || (x == v && y == u)) corner = t * 3 + j;
            }
            if (corner != kNoCorner) break;
        }
        if (corner != kNoCorner) out.push_back(corner);
    }
    std::reverse(out.begin(), out.end());
    return out;
}

void uvSetPins(Mesh& m, const std::vector<uint32_t>& corners, bool on) {
    std::vector<uint8_t>& f = ensureFlags(m);
    for (uint32_t c : corners) {
        if (c >= f.size()) continue;
        if (on) f[c] |= kUvPin;
        else f[c] &= uint8_t(~kUvPin);
    }
    dropEmptyFlags(m);
}

size_t uvWeld(Mesh& m, const UvTopology& topo, const std::vector<uint32_t>& corners) {
    const size_t nc = m.triangleCount() * 3;
    if (m.uvs.size() != nc || corners.empty()) return 0;
    std::vector<uint8_t> inSet(nc, 0);
    for (uint32_t c : corners) inSet[c] = 1;
    auto vert = [&](uint32_t c) { return m.triangles[c / 3][c % 3]; };
    // Corners in the set meet across an edge whose both ends are in the set: those become one UV vertex.
    DisjointSets ds(nc);
    for (uint32_t c = 0; c < nc; ++c) {
        if (!topo.border[c] && topo.twin[c] != kNoCorner) { // already connected
            ds.unite(c, cornerNext(topo.twin[c]));
            ds.unite(cornerNext(c), topo.twin[c]);
        }
    }
    std::vector<uint32_t> welded; // edges to clear the seam of
    for (uint32_t c : corners) {
        const uint32_t d = topo.twin[c];
        if (d == kNoCorner) continue;
        const uint32_t n = cornerNext(c);
        // The edge c -> next(c) has both ends selected, and so does its twin's side.
        if (!inSet[n] || !inSet[d] || !inSet[cornerNext(d)]) continue;
        ds.unite(c, cornerNext(d));
        ds.unite(n, d);
        welded.push_back(c);
    }
    // Only the selected corners' groups are averaged.
    std::map<uint32_t, std::pair<glm::dvec2, uint32_t>> sums;
    for (uint32_t c : corners) sums[ds.find(c)];
    for (uint32_t c = 0; c < nc; ++c) {
        auto it = sums.find(ds.find(c));
        if (it == sums.end()) continue;
        auto& s = it->second;
        s.first += glm::dvec2(m.uvs[c]);
        ++s.second;
    }
    std::vector<uint32_t> verts;
    for (uint32_t c = 0; c < nc; ++c) {
        auto it = sums.find(ds.find(c));
        if (it == sums.end()) continue;
        m.uvs[c] = glm::vec2(it->second.first / double(it->second.second));
        if (inSet[c]) verts.push_back(vert(c));
    }
    std::sort(verts.begin(), verts.end());
    verts.erase(std::unique(verts.begin(), verts.end()), verts.end());
    if (!welded.empty() && m.uvFlags.size() == nc) uvSetSeams(m, topo.twin, welded, false);
    m.facesDirty = true;
    return verts.size();
}

size_t uvSplit(Mesh& m, const UvTopology& topo, const UvSelection& sel) {
    if (!sel.valid(m)) return 0;
    std::vector<uint32_t> cut;
    if (sel.mode == UvSelectMode::Edge) {
        for (uint32_t c = 0; c < sel.corners.size(); ++c)
            if (sel.corners[c] && topo.twin[c] != kNoCorner) cut.push_back(c);
    } else if (sel.mode == UvSelectMode::Vertex) {
        // Edges with both ends selected.
        for (uint32_t c = 0; c < sel.corners.size(); ++c)
            if (sel.corners[c] && sel.corners[cornerNext(c)] && topo.twin[c] != kNoCorner) cut.push_back(c);
    } else {
        cut = uvFaceSetBorder(m, topo.twin, sel.faces);
    }
    cut.erase(std::remove_if(cut.begin(), cut.end(), [&](uint32_t c) { return topo.border[c]; }), cut.end());
    if (cut.empty()) return 0;
    uvSetSeams(m, topo.twin, cut, true);
    return cut.size();
}

size_t uvStraighten(Mesh& m, const UvTopology& topo, const std::vector<uint32_t>& corners) {
    if (corners.size() < 2 || m.uvs.size() != m.triangleCount() * 3) return 0;
    std::vector<uint8_t> seen(topo.uvVertexCount, 0);
    std::vector<glm::vec2> pts;
    for (uint32_t c : corners)
        if (!seen[topo.uvVertex[c]]) {
            seen[topo.uvVertex[c]] = 1;
            pts.push_back(m.uvs[c]);
        }
    if (pts.size() < 2) return 0;
    glm::dvec2 mean(0.0);
    for (glm::vec2 p : pts) mean += glm::dvec2(p);
    mean /= double(pts.size());
    double sxx = 0, syy = 0;
    for (glm::vec2 p : pts) {
        const glm::dvec2 d = glm::dvec2(p) - mean;
        sxx += d.x * d.x;
        syy += d.y * d.y;
    }
    const bool horizontal = sxx >= syy;
    for (uint32_t c : corners) {
        if (horizontal) m.uvs[c].y = float(mean.y);
        else m.uvs[c].x = float(mean.x);
    }
    m.facesDirty = true;
    return pts.size();
}

void uvFlipIslands(Mesh& m, const UvTopology& topo, const std::vector<uint8_t>& islands, bool horizontal) {
    std::vector<uint32_t> offsets, corners;
    islandCorners(topo, offsets, corners);
    for (uint32_t i = 0; i < topo.islandCount; ++i) {
        if (!islands[i]) continue;
        std::vector<uint32_t> cs(corners.begin() + offsets[i], corners.begin() + offsets[i + 1]);
        const glm::vec2 c = uvCenter(m.uvs, cs);
        uvTransform(m.uvs, cs, horizontal ? glm::mat2(-1, 0, 0, 1) : glm::mat2(1, 0, 0, -1), c, glm::vec2(0.0f));
    }
    m.facesDirty = true;
}

size_t uvSetDensity(Mesh& m, const UvTopology& topo, const std::vector<uint8_t>& islands, float density, int resolution) {
    if (density <= 0.0f) return 0;
    const std::vector<float> current = uvIslandDensity(m, topo, resolution);
    std::vector<uint32_t> offsets, corners;
    islandCorners(topo, offsets, corners);
    size_t n = 0;
    for (uint32_t i = 0; i < topo.islandCount; ++i) {
        if (!islands[i] || current[i] <= 0.0f) continue;
        std::vector<uint32_t> cs(corners.begin() + offsets[i], corners.begin() + offsets[i + 1]);
        const float k = density / current[i];
        uvTransform(m.uvs, cs, glm::mat2(k), uvCenter(m.uvs, cs), glm::vec2(0.0f));
        ++n;
    }
    m.facesDirty = true;
    return n;
}

std::vector<TrimStrip> equalTrimStrips(int count) {
    count = std::clamp(count, 1, 64);
    std::vector<TrimStrip> out;
    for (int i = 0; i < count; ++i) {
        TrimStrip s;
        // Strip 1 at the top of the texture (v = 1), as trim sheets are drawn.
        s.v1 = 1.0f - float(i) / float(count);
        s.v0 = 1.0f - float(i + 1) / float(count);
        s.name = "Strip " + std::to_string(i + 1);
        out.push_back(s);
    }
    return out;
}

bool parseTrimStrips(const std::string& text, std::vector<TrimStrip>& out, std::string& error) {
    out.clear();
    std::string s;
    for (char ch : text)
        if (!std::isspace(static_cast<unsigned char>(ch))) s += ch;
    if (s.empty()) {
        error = "No trim strips given";
        return false;
    }
    try {
        if (s.find_first_of(",;-") == std::string::npos) {
            const int n = std::stoi(s);
            if (n < 1 || n > 64) throw std::out_of_range("count");
            out = equalTrimStrips(n);
            return true;
        }
        if (s.find('-') != std::string::npos) { // v ranges "a-b;c-d"
            std::stringstream ss(s);
            std::string part;
            while (std::getline(ss, part, ';')) {
                const size_t dash = part.find('-', 1);
                if (dash == std::string::npos) throw std::invalid_argument("range");
                TrimStrip st;
                st.v0 = std::stof(part.substr(0, dash));
                st.v1 = std::stof(part.substr(dash + 1));
                if (st.v1 < st.v0) std::swap(st.v0, st.v1);
                if (st.v0 < 0.0f || st.v1 > 1.0f || st.v1 - st.v0 <= 0.0f) throw std::out_of_range("range");
                st.name = "Strip " + std::to_string(out.size() + 1);
                out.push_back(st);
            }
        } else { // heights from the top "0.5,0.25,0.25"
            std::stringstream ss(s);
            std::string part;
            std::vector<float> h;
            while (std::getline(ss, part, ',')) {
                h.push_back(std::stof(part));
                if (h.back() <= 0.0f) throw std::out_of_range("height");
            }
            float total = 0.0f;
            for (float x : h) total += x;
            float top = 1.0f;
            for (float x : h) {
                TrimStrip st;
                st.v1 = top;
                top -= x / total;
                st.v0 = std::max(top, 0.0f);
                st.name = "Strip " + std::to_string(out.size() + 1);
                out.push_back(st);
            }
        }
    } catch (...) {
        error = "Trim strips: give a count (4), heights from the top (0.5,0.25,0.25) or v ranges (0-0.5;0.5-1)";
        out.clear();
        return false;
    }
    if (out.empty()) {
        error = "No trim strips given";
        return false;
    }
    return true;
}

size_t uvTrimIslands(Mesh& m, const UvTopology& topo, const std::vector<uint8_t>& islands, const TrimStrip& strip,
                     bool fitWidth, float margin) {
    if (m.uvs.size() != m.triangleCount() * 3) return 0;
    const float height = (strip.v1 - strip.v0) - 2.0f * margin;
    if (height <= 0.0f) return 0;
    std::vector<uint32_t> offsets, corners;
    islandCorners(topo, offsets, corners);
    std::vector<uint32_t> row; // every corner placed, for fitWidth
    float cursor = 0.0f;
    size_t n = 0;
    for (uint32_t i = 0; i < topo.islandCount; ++i) {
        if (!islands[i]) continue;
        std::vector<uint32_t> cs(corners.begin() + offsets[i], corners.begin() + offsets[i + 1]);
        std::vector<glm::vec2> pts;
        pts.reserve(cs.size());
        for (uint32_t c : cs) pts.push_back(m.uvs[c]);
        // Lengthwise along u.
        const float a = minAreaRectAngle(pts);
        const float ca = std::cos(a), sa = std::sin(a);
        glm::vec2 lo(std::numeric_limits<float>::max()), hi(std::numeric_limits<float>::lowest());
        for (glm::vec2& p : pts) {
            p = rotate(p, ca, sa);
            lo = glm::min(lo, p);
            hi = glm::max(hi, p);
        }
        const glm::vec2 size = hi - lo;
        if (size.y <= 0.0f || size.x <= 0.0f) continue;
        const float k = height / size.y;
        for (size_t j = 0; j < cs.size(); ++j)
            m.uvs[cs[j]] = glm::vec2(cursor + (pts[j].x - lo.x) * k, strip.v0 + margin + (pts[j].y - lo.y) * k);
        cursor += size.x * k + margin;
        row.insert(row.end(), cs.begin(), cs.end());
        ++n;
    }
    const float width = cursor - margin;
    if (fitWidth && width > 0.0f)
        for (uint32_t c : row) m.uvs[c].x /= width;
    m.facesDirty = true;
    return n;
}

} // namespace sf
