// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/Decimate.h"

#include <algorithm>
#include <cmath>
#include <queue>
#include <unordered_map>

namespace sf {

namespace {

// Symmetric 4x4 quadric, upper triangle: xx xy xz xw yy yz yw zz zw ww.
struct Quadric {
    double q[10] = {};

    static Quadric plane(const glm::dvec3& n, double d, double w) {
        Quadric r;
        r.q[0] = w * n.x * n.x; r.q[1] = w * n.x * n.y; r.q[2] = w * n.x * n.z; r.q[3] = w * n.x * d;
        r.q[4] = w * n.y * n.y; r.q[5] = w * n.y * n.z; r.q[6] = w * n.y * d;
        r.q[7] = w * n.z * n.z; r.q[8] = w * n.z * d;
        r.q[9] = w * d * d;
        return r;
    }
    Quadric& operator+=(const Quadric& o) {
        for (int i = 0; i < 10; ++i) q[i] += o.q[i];
        return *this;
    }
    Quadric operator+(const Quadric& o) const {
        Quadric r = *this;
        return r += o;
    }
    double eval(const glm::vec3& p) const {
        const double x = p.x, y = p.y, z = p.z;
        return q[0] * x * x + 2 * q[1] * x * y + 2 * q[2] * x * z + 2 * q[3] * x + q[4] * y * y + 2 * q[5] * y * z +
               2 * q[6] * y + q[7] * z * z + 2 * q[8] * z + q[9];
    }
};

struct Candidate {
    double cost;
    uint32_t from, to;
    uint32_t fromVersion, toVersion;
    bool operator>(const Candidate& o) const { return cost > o.cost; }
};

uint64_t edgeKey(uint32_t a, uint32_t b) {
    if (a > b) std::swap(a, b);
    return (uint64_t(a) << 32) | b;
}

} // namespace

Mesh decimate(const Mesh& src, size_t targetTriangles, bool keepMasked) {
    const size_t nv = src.vertexCount();
    const size_t nt = src.triangleCount();
    targetTriangles = std::max<size_t>(targetTriangles, 4);
    if (nt <= targetTriangles) return src;

    const auto& P = src.positions;
    std::vector<glm::uvec3> F = src.triangles;
    std::vector<glm::vec2> FUV = src.uvs.size() == nt * 3 ? src.uvs : std::vector<glm::vec2>(); // per corner
    std::vector<char> faceAlive(nt, 1);
    std::vector<std::vector<uint32_t>> vFaces(nv);
    for (uint32_t f = 0; f < nt; ++f)
        for (int c = 0; c < 3; ++c) vFaces[F[f][c]].push_back(f);

    // Vertices on a UV seam (corners with different UVs) stay put: collapsing one would give the corners on
    // the other side of the seam UVs from the wrong island, scrambling a painted texture there. Other vertices
    // collapse onto a kept vertex, whose UV in the collapsing faces' island the moved corners then take (exact).
    std::vector<char> uvSeam(nv, 0);
    if (!FUV.empty()) {
        std::vector<glm::vec2> firstUv(nv, glm::vec2(-1e30f));
        for (uint32_t f = 0; f < nt; ++f)
            for (int c = 0; c < 3; ++c) {
                const uint32_t v = F[f][c];
                const glm::vec2 uv = FUV[size_t(f) * 3 + size_t(c)];
                if (firstUv[v].x == -1e30f) firstUv[v] = uv;
                else if (glm::any(glm::greaterThan(glm::abs(firstUv[v] - uv), glm::vec2(1e-6f)))) uvSeam[v] = 1;
            }
    }

    // Keep masked (#43): masked vertices are never collapsed away, so they and the triangles between them stay
    // exactly as they were (a collapse keeps the other endpoint where it is).
    std::vector<char> locked(nv, 0);
    if (keepMasked && src.hasMask())
        for (size_t v = 0; v < nv; ++v) locked[v] = src.mask[v] >= kKeepMaskedThreshold;

    // Area-weighted plane quadrics.
    std::vector<Quadric> Q(nv);
    std::vector<glm::dvec3> faceN(nt);
    for (uint32_t f = 0; f < nt; ++f) {
        glm::dvec3 a = P[F[f].x], b = P[F[f].y], c = P[F[f].z];
        glm::dvec3 n = glm::cross(b - a, c - a);
        double len = glm::length(n);
        if (len <= 0.0) continue;
        n /= len;
        faceN[f] = n;
        Quadric q = Quadric::plane(n, -glm::dot(n, a), len * 0.5);
        for (int c2 = 0; c2 < 3; ++c2) Q[F[f][c2]] += q;
    }

    // Boundary edges get strong perpendicular constraint planes.
    std::unordered_map<uint64_t, std::pair<uint32_t, uint32_t>> edgeUse; // key -> (count, face)
    edgeUse.reserve(nt * 2);
    for (uint32_t f = 0; f < nt; ++f) {
        for (int e = 0; e < 3; ++e) {
            auto& u = edgeUse[edgeKey(F[f][e], F[f][(e + 1) % 3])];
            ++u.first;
            u.second = f;
        }
    }
    for (const auto& [key, use] : edgeUse) {
        if (use.first != 1) continue;
        uint32_t a = uint32_t(key >> 32), b = uint32_t(key & 0xffffffffu);
        glm::dvec3 pa = P[a], pb = P[b];
        glm::dvec3 e = pb - pa;
        glm::dvec3 bn = glm::cross(e, faceN[use.second]);
        double len = glm::length(bn);
        if (len <= 0.0) continue;
        bn /= len;
        Quadric q = Quadric::plane(bn, -glm::dot(bn, pa), glm::dot(e, e) * 100.0);
        Q[a] += q;
        Q[b] += q;
    }

    std::vector<uint32_t> version(nv, 0);
    std::vector<char> vAlive(nv, 1);
    std::priority_queue<Candidate, std::vector<Candidate>, std::greater<>> heap;

    auto pushEdge = [&](uint32_t a, uint32_t b) {
        Quadric qs = Q[a] + Q[b];
        heap.push({qs.eval(P[b]), a, b, version[a], version[b]}); // a collapses onto b
        heap.push({qs.eval(P[a]), b, a, version[b], version[a]}); // b collapses onto a
    };
    for (const auto& [key, use] : edgeUse) pushEdge(uint32_t(key >> 32), uint32_t(key & 0xffffffffu));
    edgeUse.clear();

    std::vector<uint32_t> nbrU, nbrV, shared;
    auto gatherNeighbors = [&](uint32_t v, std::vector<uint32_t>& out) {
        out.clear();
        for (uint32_t f : vFaces[v]) {
            if (!faceAlive[f]) continue;
            for (int c = 0; c < 3; ++c)
                if (F[f][c] != v) out.push_back(F[f][c]);
        }
        std::sort(out.begin(), out.end());
        out.erase(std::unique(out.begin(), out.end()), out.end());
    };

    size_t liveFaces = nt;
    while (liveFaces > targetTriangles && !heap.empty()) {
        const Candidate c = heap.top();
        heap.pop();
        const uint32_t u = c.from, v = c.to;
        if (!vAlive[u] || !vAlive[v] || version[u] != c.fromVersion || version[v] != c.toVersion) continue;
        if (uvSeam[u] || locked[u]) continue;

        shared.clear();
        for (uint32_t f : vFaces[u]) {
            if (!faceAlive[f]) continue;
            if (F[f].x == v || F[f].y == v || F[f].z == v) shared.push_back(f);
        }
        if (shared.empty()) continue; // no longer an edge

        // Link condition: the edge's endpoints may share only the vertices
        // opposite the edge, otherwise collapsing makes the mesh non-manifold.
        gatherNeighbors(u, nbrU);
        gatherNeighbors(v, nbrV);
        size_t common = 0;
        for (size_t i = 0, j = 0; i < nbrU.size() && j < nbrV.size();) {
            if (nbrU[i] < nbrV[j]) ++i;
            else if (nbrU[i] > nbrV[j]) ++j;
            else ++common, ++i, ++j;
        }
        if (common != shared.size()) continue;

        // Reject collapses that flip or degenerate surrounding triangles.
        bool ok = true;
        for (uint32_t f : vFaces[u]) {
            if (!faceAlive[f] || std::find(shared.begin(), shared.end(), f) != shared.end()) continue;
            glm::uvec3 t = F[f];
            glm::vec3 before = glm::cross(P[t.y] - P[t.x], P[t.z] - P[t.x]);
            for (int k = 0; k < 3; ++k)
                if (t[k] == u) t[k] = v;
            glm::vec3 after = glm::cross(P[t.y] - P[t.x], P[t.z] - P[t.x]);
            float la = glm::length(after), lb = glm::length(before);
            if (la <= 1e-12f || (lb > 0.0f && glm::dot(after, before) < 0.2f * la * lb)) {
                ok = false;
                break;
            }
        }
        if (!ok) continue;

        // Corners that move from u to v take v's UV from a collapsed face (u is not on a seam, so all of its
        // faces lie in one UV island; skip the collapse if the collapsed faces disagree about v's UV).
        glm::vec2 uvOfV(0.0f);
        if (!FUV.empty()) {
            bool first = true;
            for (uint32_t f : shared)
                for (int k = 0; k < 3; ++k)
                    if (F[f][k] == v) {
                        const glm::vec2 uv = FUV[size_t(f) * 3 + size_t(k)];
                        if (!first && glm::any(glm::greaterThan(glm::abs(uv - uvOfV), glm::vec2(1e-6f)))) ok = false;
                        uvOfV = uv;
                        first = false;
                    }
            if (!ok) continue;
        }
        for (uint32_t f : shared) {
            faceAlive[f] = 0;
            --liveFaces;
        }
        for (uint32_t f : vFaces[u]) {
            if (!faceAlive[f]) continue;
            for (int k = 0; k < 3; ++k)
                if (F[f][k] == u) {
                    F[f][k] = v;
                    if (!FUV.empty()) FUV[size_t(f) * 3 + size_t(k)] = uvOfV;
                }
            vFaces[v].push_back(f);
        }
        vFaces[u].clear();
        vAlive[u] = 0;
        Q[v] += Q[u];
        ++version[v];
        std::erase_if(vFaces[v], [&](uint32_t f) { return !faceAlive[f]; });

        gatherNeighbors(v, nbrV);
        for (uint32_t w : nbrV) pushEdge(v, w);
    }

    // Compact into a new mesh, carrying every per-vertex array across.
    constexpr uint32_t kUnused = ~0u;
    std::vector<uint32_t> remap(nv, kUnused);
    std::vector<uint32_t> kept;
    Mesh out;
    out.triangles.reserve(liveFaces);
    for (uint32_t f = 0; f < nt; ++f) {
        if (!faceAlive[f]) continue;
        glm::uvec3 t;
        for (int k = 0; k < 3; ++k) {
            uint32_t& r = remap[F[f][k]];
            if (r == kUnused) {
                r = static_cast<uint32_t>(kept.size());
                kept.push_back(F[f][k]);
            }
            t[k] = r;
        }
        out.triangles.push_back(t);
        // Surviving faces keep their group, visibility and (updated) corner UVs.
        if (src.polygroups.size() == nt) out.polygroups.push_back(src.polygroups[f]);
        if (src.hidden.size() == nt) out.hidden.push_back(src.hidden[f]);
        if (!FUV.empty()) out.uvs.insert(out.uvs.end(), FUV.begin() + std::ptrdiff_t(f) * 3, FUV.begin() + std::ptrdiff_t(f) * 3 + 3);
    }
    out.positions.reserve(kept.size());
    for (uint32_t v : kept) out.positions.push_back(src.positions[v]);
    if (!FUV.empty()) { // the painted texture still fits the kept UVs (shared until one side paints)
        out.texture = src.texture;
        out.paintToTexture = src.paintToTexture;
    }
    if (src.mask.size() == nv)
        for (uint32_t v : kept) out.mask.push_back(src.mask[v]);
    if (src.colors.size() == nv)
        for (uint32_t v : kept) out.colors.push_back(src.colors[v]);
    if (!src.layers.empty()) {
        for (uint32_t v : kept) out.base.push_back(src.base[v]);
        for (const auto& l : src.layers) {
            SculptLayer nl{l.name, {}, l.weight};
            nl.delta.reserve(kept.size());
            for (uint32_t v : kept) nl.delta.push_back(l.delta[v]);
            out.layers.push_back(std::move(nl));
        }
    }
    copyPaintLayers(src, out, kept);
    out.rebuildTopology();
    out.recomputeAllNormals();
    return out;
}

std::vector<Mesh> generateLods(const Mesh& src, int levels, float ratio) {
    std::vector<Mesh> lods;
    levels = std::max(levels, 1);
    ratio = std::clamp(ratio, 0.01f, 0.99f);
    lods.push_back(src);
    for (int i = 1; i < levels; ++i) {
        size_t target = static_cast<size_t>(std::llround(double(lods.back().triangleCount()) * ratio));
        lods.push_back(decimate(lods.back(), target));
    }
    return lods;
}

} // namespace sf
