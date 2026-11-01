// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/Primitives.h"

#include <cmath>
#include <numbers>
#include <type_traits>
#include <unordered_map>

namespace sf {

namespace {

uint64_t edgeKey(uint32_t a, uint32_t b) {
    if (a > b) std::swap(a, b);
    return (uint64_t(a) << 32) | b;
}

void finalize(Mesh& m) {
    m.rebuildTopology();
    m.recomputeAllNormals();
}

} // namespace

Mesh makeIcosphere(int subdivisions, float radius) {
    const float t = (1.0f + std::sqrt(5.0f)) / 2.0f;
    Mesh m;
    m.positions = {{-1, t, 0}, {1, t, 0}, {-1, -t, 0}, {1, -t, 0}, {0, -1, t}, {0, 1, t},
                   {0, -1, -t}, {0, 1, -t}, {t, 0, -1}, {t, 0, 1}, {-t, 0, -1}, {-t, 0, 1}};
    m.triangles = {{0, 11, 5}, {0, 5, 1},  {0, 1, 7},   {0, 7, 10}, {0, 10, 11}, {1, 5, 9}, {5, 11, 4},
                   {11, 10, 2}, {10, 7, 6}, {7, 1, 8},   {3, 9, 4},  {3, 4, 2},   {3, 2, 6}, {3, 6, 8},
                   {3, 8, 9},  {4, 9, 5},  {2, 4, 11},  {6, 2, 10}, {8, 6, 7},   {9, 8, 1}};
    for (auto& p : m.positions) p = glm::normalize(p);

    for (int level = 0; level < subdivisions; ++level) {
        std::unordered_map<uint64_t, uint32_t> mids;
        mids.reserve(m.triangles.size() * 2);
        auto mid = [&](uint32_t a, uint32_t b) {
            auto [it, inserted] = mids.try_emplace(edgeKey(a, b), uint32_t(m.positions.size()));
            if (inserted) m.positions.push_back(glm::normalize(m.positions[a] + m.positions[b]));
            return it->second;
        };
        std::vector<glm::uvec3> tris;
        tris.reserve(m.triangles.size() * 4);
        for (const auto& tri : m.triangles) {
            uint32_t ab = mid(tri.x, tri.y), bc = mid(tri.y, tri.z), ca = mid(tri.z, tri.x);
            tris.push_back({tri.x, ab, ca});
            tris.push_back({tri.y, bc, ab});
            tris.push_back({tri.z, ca, bc});
            tris.push_back({ab, bc, ca});
        }
        m.triangles = std::move(tris);
    }
    for (auto& p : m.positions) p *= radius;
    finalize(m);
    return m;
}

Mesh makePlane(int resolution, float size) {
    Mesh m;
    const int n = std::max(resolution, 1);
    for (int j = 0; j <= n; ++j)
        for (int i = 0; i <= n; ++i)
            m.positions.emplace_back((float(i) / n - 0.5f) * size, 0.0f, (float(j) / n - 0.5f) * size);
    for (int j = 0; j < n; ++j) {
        for (int i = 0; i < n; ++i) {
            uint32_t a = j * (n + 1) + i, b = a + 1, c = a + (n + 1), d = c + 1;
            m.triangles.push_back({a, c, b});
            m.triangles.push_back({b, c, d});
            m.pairQuad(uint32_t(m.triangles.size() - 2), uint32_t(m.triangles.size() - 1));
        }
    }
    finalize(m);
    return m;
}

Mesh makeCube(int resolution, float size) {
    const int n = std::max(resolution, 1);
    const float h = size * 0.5f;
    Mesh raw;
    // Each side: a grid spanned by axes u, v from its corner, wound to face outwards.
    const glm::vec3 axes[6][3] = {{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}},  {{-1, 0, 0}, {0, 1, 0}, {0, 0, -1}},
                                  {{0, 1, 0}, {0, 0, 1}, {1, 0, 0}},  {{0, -1, 0}, {0, 0, 1}, {-1, 0, 0}},
                                  {{0, 0, 1}, {1, 0, 0}, {0, 1, 0}},  {{0, 0, -1}, {1, 0, 0}, {0, -1, 0}}};
    for (int f = 0; f < 6; ++f) {
        const glm::vec3 normal = axes[f][0], u = axes[f][2], v = axes[f][1];
        const uint32_t first = uint32_t(raw.positions.size());
        // (2i - n) / n: exactly opposite for i and n - i, so the sides' shared edges weld at every resolution
        // (2i / n - 1 left a few unwelded vertices at some, e.g. 13 and 26).
        for (int j = 0; j <= n; ++j)
            for (int i = 0; i <= n; ++i)
                raw.positions.push_back(h * (normal + float(2 * i - n) / float(n) * u + float(2 * j - n) / float(n) * v));
        for (int j = 0; j < n; ++j)
            for (int i = 0; i < n; ++i) {
                const uint32_t a = first + uint32_t(j * (n + 1) + i), b = a + 1, c = a + uint32_t(n + 1), d = c + 1;
                raw.triangles.push_back({a, c, b}); // (v x u) = outward normal on every side
                raw.triangles.push_back({b, c, d});
                raw.pairQuad(uint32_t(raw.triangles.size() - 2), uint32_t(raw.triangles.size() - 1));
                raw.polygroups.push_back(uint32_t(f + 1));
                raw.polygroups.push_back(uint32_t(f + 1));
            }
    }
    return weldImported(raw);
}

namespace {
// Flips every triangle when the closed mesh's signed volume is negative (so all primitives face outwards).
void faceOutwards(Mesh& m) {
    double vol = 0.0;
    for (const auto& t : m.triangles)
        vol += glm::dot(glm::dvec3(m.positions[t[0]]), glm::cross(glm::dvec3(m.positions[t[1]]), glm::dvec3(m.positions[t[2]])));
    if (vol < 0.0)
        for (auto& t : m.triangles) std::swap(t[1], t[2]);
}

// A surface of revolution about Y from a profile of (radius, height) points, bottom to top. A radius of 0 is a
// pole (a triangle fan); everything else is rings of quads. group[k] is the polygroup between points k and k+1.
Mesh revolve(const std::vector<glm::vec2>& profile, const std::vector<uint32_t>& group, int segments) {
    Mesh raw;
    const int n = std::max(segments, 3);
    std::vector<uint32_t> start(profile.size());
    for (size_t k = 0; k < profile.size(); ++k) {
        start[k] = uint32_t(raw.positions.size());
        const glm::vec2 q = profile[k];
        if (q.x <= 1e-6f) raw.positions.push_back({0.0f, q.y, 0.0f});
        else
            for (int i = 0; i < n; ++i) {
                const float a = 6.28318530718f * float(i) / float(n);
                raw.positions.push_back({q.x * std::cos(a), q.y, -q.x * std::sin(a)});
            }
    }
    for (size_t k = 0; k + 1 < profile.size(); ++k) {
        const bool pa = profile[k].x <= 1e-6f, pb = profile[k + 1].x <= 1e-6f;
        if (pa && pb) continue;
        for (int i = 0; i < n; ++i) {
            const uint32_t i1 = uint32_t((i + 1) % n);
            const uint32_t a = pa ? start[k] : start[k] + uint32_t(i), b = pa ? start[k] : start[k] + i1;
            const uint32_t c = pb ? start[k + 1] : start[k + 1] + uint32_t(i), d = pb ? start[k + 1] : start[k + 1] + i1;
            if (pa) raw.triangles.push_back({a, c, d});
            else if (pb) raw.triangles.push_back({a, c, b});
            else {
                raw.triangles.push_back({a, c, b});
                raw.triangles.push_back({b, c, d});
                raw.pairQuad(uint32_t(raw.triangles.size() - 2), uint32_t(raw.triangles.size() - 1));
                raw.polygroups.push_back(group[k]);
            }
            raw.polygroups.push_back(group[k]);
        }
    }
    faceOutwards(raw);
    return weldImported(raw);
}

// Profile points from a to b in `steps` even steps (b included, a not).
void addLine(std::vector<glm::vec2>& pr, std::vector<uint32_t>& gr, glm::vec2 b, int steps, uint32_t group) {
    const glm::vec2 a = pr.back();
    for (int i = 1; i <= steps; ++i) {
        pr.push_back(glm::mix(a, b, float(i) / float(steps)));
        gr.push_back(group);
    }
}
} // namespace

Mesh makeCylinder(int segments, float size) {
    const float r = size * 0.3f, h = size * 0.5f;
    std::vector<glm::vec2> pr{{0.0f, -h}};
    std::vector<uint32_t> gr;
    addLine(pr, gr, {r, -h}, 4, 1);
    addLine(pr, gr, {r, h}, std::max(4, segments / 2), 2);
    addLine(pr, gr, {0.0f, h}, 4, 3);
    return revolve(pr, gr, segments);
}

Mesh makeCone(int segments, float size) {
    const float r = size * 0.4f, h = size * 0.5f;
    std::vector<glm::vec2> pr{{0.0f, -h}};
    std::vector<uint32_t> gr;
    addLine(pr, gr, {r, -h}, 4, 1);
    addLine(pr, gr, {0.0f, h}, std::max(4, segments / 2), 2);
    return revolve(pr, gr, segments);
}

Mesh makeCapsule(int segments, float size) {
    const float r = size * 0.25f, h = size * 0.5f - r;
    const int arc = std::max(4, segments / 4);
    std::vector<glm::vec2> pr{{0.0f, -h - r}};
    std::vector<uint32_t> gr;
    for (int i = 1; i <= arc; ++i) {
        const float a = 1.5707963f * float(i) / float(arc);
        pr.push_back({r * std::sin(a), -h - r * std::cos(a)});
        gr.push_back(1);
    }
    addLine(pr, gr, {r, h}, std::max(4, segments / 3), 2);
    for (int i = 1; i <= arc; ++i) {
        const float a = 1.5707963f * float(i) / float(arc);
        pr.push_back({r * std::cos(a), h + r * std::sin(a)});
        gr.push_back(3);
    }
    pr.back().x = 0.0f;
    return revolve(pr, gr, segments);
}

Mesh makeTorus(int segments, float size) {
    const float tube = size * 0.15f, R = size * 0.5f - tube;
    const int n = std::max(segments, 6), m = std::max(segments / 2, 4);
    Mesh raw;
    for (int i = 0; i < n; ++i)
        for (int j = 0; j < m; ++j) {
            const float a = 6.28318530718f * float(i) / float(n), b = 6.28318530718f * float(j) / float(m);
            const float rr = R + tube * std::cos(b);
            raw.positions.push_back({rr * std::cos(a), tube * std::sin(b), -rr * std::sin(a)});
        }
    for (int i = 0; i < n; ++i)
        for (int j = 0; j < m; ++j) {
            const uint32_t a = uint32_t(i * m + j), b = uint32_t(i * m + (j + 1) % m);
            const uint32_t c = uint32_t(((i + 1) % n) * m + j), d = uint32_t(((i + 1) % n) * m + (j + 1) % m);
            raw.triangles.push_back({a, c, b});
            raw.triangles.push_back({b, c, d});
            raw.pairQuad(uint32_t(raw.triangles.size() - 2), uint32_t(raw.triangles.size() - 1));
            raw.polygroups.push_back(1);
            raw.polygroups.push_back(1);
        }
    faceOutwards(raw);
    return weldImported(raw);
}

Mesh makeQuadSphere(int resolution, float size) {
    Mesh m = makeCube(resolution, 2.0f);
    for (auto& p : m.positions) p = glm::normalize(p) * (size * 0.5f);
    m.recomputeAllNormals();
    return m;
}

Mesh loopSubdivide(const Mesh& src) {
    const uint32_t nv = static_cast<uint32_t>(src.vertexCount());

    // Edge -> (new vertex index, use count, opposite vertices).
    struct EdgeInfo {
        uint32_t index;
        uint32_t count;
        uint32_t opp[2];
    };
    std::unordered_map<uint64_t, EdgeInfo> edges;
    edges.reserve(src.triangles.size() * 2);
    uint32_t next = nv;
    for (const auto& t : src.triangles) {
        for (int e = 0; e < 3; ++e) {
            uint32_t a = t[e], b = t[(e + 1) % 3], c = t[(e + 2) % 3];
            auto [it, inserted] = edges.try_emplace(edgeKey(a, b), EdgeInfo{next, 0, {c, c}});
            if (inserted) ++next;
            if (it->second.count < 2) it->second.opp[it->second.count] = c;
            ++it->second.count;
        }
    }

    // Boundary neighbours per vertex (edges used by exactly one triangle).
    std::vector<std::vector<uint32_t>> boundaryNbrs(nv);
    for (const auto& [key, e] : edges) {
        if (e.count == 1) {
            uint32_t a = uint32_t(key >> 32), b = uint32_t(key & 0xffffffffu);
            boundaryNbrs[a].push_back(b);
            boundaryNbrs[b].push_back(a);
        }
    }

    // Loop subdivision is linear, so the same stencils apply to positions and
    // to sculpt-layer offsets, keeping layers consistent across levels.
    auto subdivideAttribute = [&](const std::vector<glm::vec3>& P) {
        std::vector<glm::vec3> out(next);
        for (uint32_t v = 0; v < nv; ++v) {
            const auto& bn = boundaryNbrs[v];
            if (bn.size() == 2) {
                out[v] = 0.75f * P[v] + 0.125f * (P[bn[0]] + P[bn[1]]);
            } else if (!bn.empty()) {
                out[v] = P[v]; // corner / non-manifold: keep fixed
            } else {
                auto ring = src.neighbors(v);
                const float n = static_cast<float>(ring.size());
                if (n < 2) {
                    out[v] = P[v];
                    continue;
                }
                float c = 0.375f + 0.25f * std::cos(2.0f * std::numbers::pi_v<float> / n);
                float beta = (0.625f - c * c) / n;
                glm::vec3 sum(0.0f);
                for (uint32_t u : ring) sum += P[u];
                out[v] = (1.0f - n * beta) * P[v] + beta * sum;
            }
        }
        for (const auto& [key, e] : edges) {
            uint32_t a = uint32_t(key >> 32), b = uint32_t(key & 0xffffffffu);
            if (e.count == 2)
                out[e.index] = 0.375f * (P[a] + P[b]) + 0.125f * (P[e.opp[0]] + P[e.opp[1]]);
            else
                out[e.index] = 0.5f * (P[a] + P[b]);
        }
        return out;
    };

    Mesh out;
    out.positions = subdivideAttribute(src.positions);
    if (!src.layers.empty()) {
        out.base = subdivideAttribute(src.base);
        for (const auto& l : src.layers) out.layers.push_back({l.name, subdivideAttribute(l.delta), l.weight});
    }

    // Mask and polypaint are interpolated linearly (old vertices keep their
    // value, edge vertices take the average) so painted borders stay crisp.
    auto interpolateLinear = [&](const auto& A) {
        std::remove_cvref_t<decltype(A)> result(next);
        for (uint32_t v = 0; v < nv; ++v) result[v] = A[v];
        for (const auto& [key, e] : edges) result[e.index] = (A[uint32_t(key >> 32)] + A[uint32_t(key & 0xffffffffu)]) * 0.5f;
        return result;
    };
    if (src.mask.size() == nv) out.mask = interpolateLinear(src.mask);
    if (src.colors.size() == nv) out.colors = interpolateLinear(src.colors);
    if (!src.paintLayers.empty()) {
        std::vector<glm::uvec3> midpoints; // new vertex, parent a, parent b
        midpoints.reserve(edges.size());
        for (const auto& [key, e] : edges) midpoints.push_back({e.index, uint32_t(key >> 32), uint32_t(key & 0xffffffffu)});
        subdividePaintLayers(src, out, next, midpoints);
    }

    const size_t nt = src.triangles.size();
    const bool groups = src.polygroups.size() == nt, hidden = src.hidden.size() == nt, uvs = src.uvs.size() == nt * 3;
    out.triangles.reserve(nt * 4);
    for (size_t ti = 0; ti < nt; ++ti) {
        const auto& t = src.triangles[ti];
        uint32_t ab = edges.at(edgeKey(t.x, t.y)).index;
        uint32_t bc = edges.at(edgeKey(t.y, t.z)).index;
        uint32_t ca = edges.at(edgeKey(t.z, t.x)).index;
        out.triangles.push_back({t.x, ab, ca});
        out.triangles.push_back({t.y, bc, ab});
        out.triangles.push_back({t.z, ca, bc});
        out.triangles.push_back({ab, bc, ca});
        // Children inherit their parent's group and visibility.
        if (groups) out.polygroups.insert(out.polygroups.end(), 4, src.polygroups[ti]);
        if (hidden) out.hidden.insert(out.hidden.end(), 4, src.hidden[ti]);
        if (uvs) {
            const glm::vec2 ua = src.uvs[ti * 3], ub = src.uvs[ti * 3 + 1], uc = src.uvs[ti * 3 + 2];
            const glm::vec2 uab = (ua + ub) * 0.5f, ubc = (ub + uc) * 0.5f, uca = (uc + ua) * 0.5f;
            out.uvs.insert(out.uvs.end(), {ua, uab, uca, ub, ubc, uab, uc, uca, ubc, uab, ubc, uca});
        }
    }
    // A quad (t, u) splits into four quads: each child at a diagonal corner pairs with the partner's child at the
    // same corner, and the child at the far corner with its own centre triangle (children 0..2 sit at corners
    // x, y, z; child 3 is the centre).
    if (src.quadPartners.size() == nt) {
        for (uint32_t ti = 0; ti < nt; ++ti) {
            const uint32_t u = src.quadPartners[ti];
            const int e = u == Mesh::kNoQuad ? -1 : src.quadDiagonal(ti, u);
            if (e < 0) continue;
            const glm::uvec3& t = src.triangles[ti];
            out.pairQuad(ti * 4 + uint32_t((e + 2) % 3), ti * 4 + 3);
            for (int c : {e, (e + 1) % 3})
                for (uint32_t k = 0; k < 3; ++k)
                    if (src.triangles[u][k] == t[c]) out.pairQuad(ti * 4 + uint32_t(c), u * 4 + k);
        }
    }
    if (uvs) { // split UVs keep the painted texture valid
        out.texture = src.texture;
        out.paintToTexture = src.paintToTexture;
    }
    finalize(out);
    return out;
}

} // namespace sf
