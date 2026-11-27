// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/PolyExtrude.h"

#include "core/FaceGroups.h"
#include "core/PaintLayer.h"
#include "core/PolyMode.h"
#include "core/PolyOps.h"
#include "core/Uv.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numeric>
#include <unordered_map>

namespace sf {

namespace {

constexpr float kMinMiter = 0.3f; // the mitre never scales an offset by more than 1 / kMinMiter (sharp corners stay put)

glm::vec3 triangleCross(const Mesh& m, uint32_t t) {
    const glm::uvec3& tri = m.triangles[t];
    return glm::cross(m.positions[tri.y] - m.positions[tri.x], m.positions[tri.z] - m.positions[tri.x]);
}

glm::vec3 safeNormalize(const glm::vec3& v, const glm::vec3& fallback) {
    const float len = glm::length(v);
    return len > 1e-12f ? v / len : fallback;
}

glm::vec3 vertexNormal(const Mesh& m, uint32_t v) {
    if (v < m.normals.size()) return safeNormalize(m.normals[v], glm::vec3(0.0f, 1.0f, 0.0f));
    return glm::vec3(0.0f, 1.0f, 0.0f);
}

bool degenerate(const glm::uvec3& t) { return t.x == t.y || t.y == t.z || t.x == t.z; }

// Mitred offset direction from the unit directions `dirs` meeting at a vertex (the edges' inward or outward
// normals, or the polygons' normals): their mean direction, scaled so each of them still moves by one unit
// (1 / cos of the half angle for two of them), capped at 1 / kMinMiter.
glm::vec3 mitre(const std::vector<glm::vec3>& dirs, const glm::vec3& fallback) {
    glm::vec3 sum(0.0f);
    for (const glm::vec3& d : dirs) sum += d;
    const glm::vec3 u = safeNormalize(sum, fallback);
    if (dirs.empty()) return u;
    float s = 0.0f;
    for (const glm::vec3& d : dirs) s += glm::dot(u, d);
    s /= float(dirs.size());
    return u / std::max(s, kMinMiter);
}

// The positions of `plan.moved` for distance and inset (a fraction of each vertex's piece, Moved::insetExtent), into
// `positions` (sized for the result).
void placeMoved(const Mesh& mesh, const ExtrudePlan& plan, float distance, float inset, std::vector<glm::vec3>& positions) {
    for (const ExtrudePlan::Moved& m : plan.moved)
        positions[m.vertex] = mesh.positions[plan.source[m.vertex]] + (inset * m.insetExtent) * m.insetDir + distance * m.dir;
}

// A border edge of a selected piece (a connected part of the selection, or one polygon when individual).
struct PieceEdge {
    uint32_t piece, a, b;
};

// Per piece: the smallest extent of its border loop(s), the width across the piece along its narrowest direction in
// its own plane (the plane of its area-weighted normal). The narrowest width of a shape is across one of its edges, so
// the candidates are the in-plane normals of the border edges, one per degree; a piece with no usable edge falls back
// to the smallest side of its border points' bounding box.
std::unordered_map<uint32_t, float> pieceExtents(const Mesh& mesh, const std::vector<glm::vec3>& cross,
                                                 const std::vector<uint32_t>& pieceOf, const std::vector<PieceEdge>& edges) {
    struct Acc {
        glm::vec3 normal{0.0f};
        std::vector<const PieceEdge*> edges;
    };
    std::unordered_map<uint32_t, Acc> pieces;
    for (uint32_t t = 0; t < pieceOf.size(); ++t)
        if (pieceOf[t] != ~0u) pieces[pieceOf[t]].normal += cross[t];
    for (const PieceEdge& e : edges) pieces[e.piece].edges.push_back(&e);
    std::unordered_map<uint32_t, float> extent;
    for (auto& [id, acc] : pieces) {
        if (acc.edges.empty()) continue;
        std::vector<glm::vec3> pts;
        pts.reserve(acc.edges.size());
        glm::vec3 lo(std::numeric_limits<float>::max()), hi(-std::numeric_limits<float>::max());
        for (const PieceEdge* e : acc.edges) {
            pts.push_back(mesh.positions[e->a]);
            lo = glm::min(lo, pts.back());
            hi = glm::max(hi, pts.back());
        }
        float best = std::numeric_limits<float>::max();
        const glm::vec3 n = safeNormalize(acc.normal, glm::vec3(0.0f));
        if (glm::dot(n, n) > 0.0f) {
            const glm::vec3 t1 = safeNormalize(glm::cross(n, std::abs(n.x) < 0.9f ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0)), glm::vec3(0, 0, 1));
            const glm::vec3 t2 = glm::cross(n, t1);
            std::array<glm::vec3, 180> dirs{};
            std::array<bool, 180> have{};
            for (const PieceEdge* e : acc.edges) {
                const glm::vec3 d = mesh.positions[e->b] - mesh.positions[e->a];
                const glm::vec3 across = safeNormalize(glm::cross(n, d), glm::vec3(0.0f));
                if (glm::dot(across, across) == 0.0f) continue;
                float angle = std::atan2(glm::dot(across, t2), glm::dot(across, t1));
                if (angle < 0.0f) angle += glm::pi<float>();
                if (angle >= glm::pi<float>()) angle -= glm::pi<float>();
                const size_t bin = std::min<size_t>(size_t(angle / glm::pi<float>() * 180.0f), 179);
                if (!have[bin]) {
                    have[bin] = true;
                    dirs[bin] = across;
                }
            }
            for (size_t i = 0; i < dirs.size(); ++i) {
                if (!have[i]) continue;
                float mn = std::numeric_limits<float>::max(), mx = -mn;
                for (const glm::vec3& p : pts) {
                    const float d = glm::dot(p, dirs[i]);
                    mn = std::min(mn, d);
                    mx = std::max(mx, d);
                }
                best = std::min(best, mx - mn);
            }
        }
        if (!(best > 1e-9f && best < std::numeric_limits<float>::max())) {
            const glm::vec3 size = hi - lo;
            best = std::numeric_limits<float>::max();
            for (int k = 0; k < 3; ++k)
                if (size[k] > 1e-9f) best = std::min(best, size[k]);
            if (best == std::numeric_limits<float>::max()) best = 0.0f;
        }
        extent[id] = best;
    }
    return extent;
}

// True when an inset too deep folds some cap polygon: it turns against its original normal, or one of its edges
// collapses or reverses (two border vertices crossed; a cap pushed through its own centre comes out turned by
// 180 degrees, still facing the same way, so the normal alone wouldn't tell).
bool capFolds(const Mesh& mesh, const ExtrudePlan& plan, const std::vector<glm::vec3>& positions) {
    const size_t nt = mesh.triangleCount();
    for (size_t t = 0; t < std::min(nt, plan.firstWall); ++t) {
        if (!plan.cap[t]) continue;
        const glm::uvec3& tri = plan.triangles[t];
        const glm::vec3 n = glm::cross(positions[tri.y] - positions[tri.x], positions[tri.z] - positions[tri.x]);
        const glm::vec3 n0 = triangleCross(mesh, uint32_t(t));
        if (glm::dot(n, n0) <= 0.0f && glm::dot(n0, n0) > 0.0f) return true;
        const glm::uvec3& old = mesh.triangles[t];
        for (int k = 0; k < 3; ++k) {
            const glm::vec3 e0 = mesh.positions[old[(k + 1) % 3]] - mesh.positions[old[k]];
            const glm::vec3 e1 = positions[tri[(k + 1) % 3]] - positions[tri[k]];
            if (glm::dot(e0, e1) <= 1e-3f * glm::dot(e0, e0)) return true;
        }
    }
    return false;
}

// Face groups, UVs, creases and quad pairs of the walls (the kept triangles are already copied).
void finishWallAttributes(const Mesh& mesh, const ExtrudePlan& plan, float distance, Mesh& out) {
    const size_t nt = plan.firstWall, nw = plan.triangles.size() - nt;
    if (nw == 0) return;
    // Face groups: one new group, or the owner polygon's.
    if (mesh.hasFaceGroups() || !plan.inheritGroup) {
        out.ensureFaceGroups(0);
        for (size_t i = 0; i < nw; ++i)
            out.facegroups[nt + i] = plan.inheritGroup ? mesh.facegroupAt(plan.wallSource[i]) : plan.wallGroup;
    }
    // UVs: the wall's bottom edge takes the owner's UVs of that edge, its top edge the same shifted sideways
    // (away from the owner's third corner) by the wall's height at the owner's texel density, so the strip is a
    // rectangle beside the cap edge; zero height (walls to be pulled up later) gets a square.
    if (mesh.hasUvs()) {
        for (size_t i = 0; i < nw; ++i) {
            const uint32_t owner = plan.wallSource[i];
            const glm::uvec2 e = plan.wallEdge[i];
            const glm::uvec3& otri = mesh.triangles[owner];
            int k = 0;
            while (k < 3 && !(otri[k] == e.x && otri[(k + 1) % 3] == e.y)) ++k;
            if (k == 3) continue;
            const glm::vec2 uvA = mesh.uvs[owner * 3 + k], uvB = mesh.uvs[owner * 3 + (k + 1) % 3],
                            uvC = mesh.uvs[owner * 3 + (k + 2) % 3];
            const float len3 = glm::length(mesh.positions[e.y] - mesh.positions[e.x]);
            const glm::vec2 along = uvB - uvA;
            glm::vec2 perp(-along.y, along.x);
            if (glm::dot(perp, uvC - uvA) > 0.0f) perp = -perp; // away from the owner polygon
            const float uvLen = glm::length(along);
            const float height = (std::abs(distance) > 1e-7f ? std::abs(distance) : len3) * (len3 > 1e-12f ? uvLen / len3 : 1.0f);
            if (uvLen > 1e-12f) perp = perp / uvLen * height;
            const glm::uvec3& wtri = plan.triangles[nt + i];
            for (int c = 0; c < 3; ++c) {
                const uint32_t v = wtri[c];
                const bool top = v >= mesh.vertexCount();
                const uint32_t src = plan.source[v];
                glm::vec2 uv = src == e.x ? uvA : uvB;
                if (top) uv += perp;
                out.uvs[(nt + i) * 3 + c] = uv;
            }
        }
    }
    // Quads: each wall is one quad of two triangles in order.
    for (size_t i = 0; i + 1 < nw; i += 2) out.pairQuad(uint32_t(nt + i), uint32_t(nt + i + 1));
}

// Creases live on both sides of an edge: give every wall corner the crease of its twin (the cap's edge above,
// the neighbour's edge below, nothing on the wall's own uprights).
void matchWallCreases(const ExtrudePlan& plan, Mesh& out) {
    if (!out.hasCreases()) return;
    const std::vector<uint32_t> twin = cornerTwins(out);
    for (size_t c = plan.firstWall * 3; c < out.creases.size(); ++c)
        out.creases[c] = twin[c] != kNoCorner ? out.creases[twin[c]] : 0.0f;
}

void copyVertexAttributes(const Mesh& mesh, const ExtrudePlan& plan, Mesh& out) {
    const std::vector<uint32_t>& src = plan.source;
    out.positions.resize(src.size());
    for (size_t v = 0; v < src.size(); ++v) out.positions[v] = mesh.positions[src[v]];
    if (mesh.hasMask()) {
        out.mask.resize(src.size());
        for (size_t v = 0; v < src.size(); ++v) out.mask[v] = mesh.mask[src[v]];
    }
    if (mesh.hasColors()) {
        out.colors.resize(src.size());
        for (size_t v = 0; v < src.size(); ++v) out.colors[v] = mesh.colors[src[v]];
    }
    if (!mesh.layers.empty()) {
        out.base.resize(src.size());
        for (size_t v = 0; v < src.size(); ++v) out.base[v] = mesh.base[src[v]];
        for (const SculptLayer& l : mesh.layers) {
            SculptLayer nl{l.name, {}, l.weight};
            nl.delta.resize(src.size());
            for (size_t v = 0; v < src.size(); ++v) nl.delta[v] = l.delta[src[v]];
            out.layers.push_back(std::move(nl));
        }
    }
    copyPaintLayers(mesh, out, src);
}

void copyTriangleAttributes(const Mesh& mesh, const ExtrudePlan& plan, Mesh& out) {
    const size_t nt = mesh.triangleCount(), total = plan.triangles.size();
    out.triangles = plan.triangles;
    if (mesh.hasFaceGroups()) {
        out.facegroups.assign(total, 0);
        std::copy(mesh.facegroups.begin(), mesh.facegroups.end(), out.facegroups.begin());
    }
    if (mesh.hasHidden()) {
        out.hidden.assign(total, 0);
        std::copy(mesh.hidden.begin(), mesh.hidden.end(), out.hidden.begin());
    }
    if (mesh.hasUvs()) {
        out.uvs.assign(total * 3, glm::vec2(0.0f));
        std::copy(mesh.uvs.begin(), mesh.uvs.end(), out.uvs.begin());
        out.texture = mesh.texture;
        out.paintToTexture = mesh.paintToTexture;
    }
    if (mesh.hasUvFlags()) {
        out.uvFlags.assign(total * 3, 0);
        std::copy(mesh.uvFlags.begin(), mesh.uvFlags.end(), out.uvFlags.begin());
    }
    if (mesh.hasCreases()) {
        out.creases.assign(total * 3, 0.0f);
        std::copy(mesh.creases.begin(), mesh.creases.end(), out.creases.begin());
    }
    if (mesh.hasQuads()) {
        out.quadPartners.assign(total, Mesh::kNoQuad);
        std::copy(mesh.quadPartners.begin(), mesh.quadPartners.begin() + std::ptrdiff_t(nt), out.quadPartners.begin());
    }
}

std::string countText(size_t n, const char* one, const char* many) {
    return std::to_string(n) + " " + (n == 1 ? one : many);
}

} // namespace

size_t completePolygonSelection(const Mesh& mesh, std::vector<uint8_t>& selected) {
    const size_t nt = mesh.triangleCount();
    selected.resize(nt, 0);
    for (uint32_t t = 0; t < nt; ++t)
        if (selected[t] && (mesh.triangleHidden(t) || degenerate(mesh.triangles[t]))) selected[t] = 0;
    for (uint32_t t = 0; t < nt; ++t) {
        if (!selected[t]) continue;
        const uint32_t u = mesh.quadPartnerAt(t);
        if (u != Mesh::kNoQuad && !mesh.triangleHidden(u) && !degenerate(mesh.triangles[u])) selected[u] = 1;
    }
    size_t polygons = 0;
    for (uint32_t t = 0; t < nt; ++t) {
        if (!selected[t]) continue;
        const uint32_t u = mesh.quadPartnerAt(t);
        polygons += u == Mesh::kNoQuad || !selected[u] || u > t;
    }
    return polygons;
}

ExtrudePlan planExtrude(const Mesh& mesh, std::span<const uint8_t> selected, const ExtrudeOptions& options) {
    ExtrudePlan plan;
    plan.faces = true;
    const size_t nt = mesh.triangleCount(), nv = mesh.vertexCount();
    if (nt == 0) {
        plan.error = "Extrude: the Part is empty";
        return plan;
    }
    std::vector<uint8_t> sel(selected.begin(), selected.begin() + std::ptrdiff_t(std::min(selected.size(), nt)));
    plan.polygons = completePolygonSelection(mesh, sel);
    if (plan.polygons == 0) {
        plan.error = "Extrude: select some faces first";
        return plan;
    }
    const std::vector<uint32_t> twin = cornerTwins(mesh);
    plan.source.resize(nv);
    std::iota(plan.source.begin(), plan.source.end(), 0u);
    plan.triangles = mesh.triangles;
    plan.cap = sel;
    plan.inheritGroup = options.sideGroup == ExtrudeSideGroup::Inherit;
    plan.wallGroup = nextFaceGroupId(mesh);

    // A boundary corner: a selected triangle's edge with no selected polygon across it (an open edge too).
    auto boundary = [&](uint32_t c) {
        const uint32_t tw = twin[c];
        return tw == kNoCorner || !sel[tw / 3];
    };
    // Triangle normals (unnormalised: twice the area times the normal), for the directions.
    std::vector<glm::vec3> cross(nt, glm::vec3(0.0f));
    for (uint32_t t = 0; t < nt; ++t)
        if (sel[t]) cross[t] = triangleCross(mesh, t);

    // Per copy of a vertex: the directions meeting there, to be mitred.
    struct Gather {
        std::vector<glm::vec3> normals; // the selected polygons' normals around it (Along::Normals)
        std::vector<glm::vec3> inward;  // the boundary edges' inward normals (the inset)
        glm::vec3 regionSum{0.0f};      // Along::Region: the sum of its regions' normals
    };

    if (!options.individual) {
        // Region: which cap vertices split from the surface. A vertex splits when a boundary edge ends at it or
        // some triangle around it is not selected (a wedge, or another fan at a pinched vertex).
        std::vector<uint32_t> selectedAround(nv, 0);
        std::vector<uint8_t> isCap(nv, 0), splits(nv, 0);
        for (uint32_t t = 0; t < nt; ++t) {
            if (!sel[t]) continue;
            for (int k = 0; k < 3; ++k) {
                const uint32_t v = mesh.triangles[t][k];
                isCap[v] = 1;
                ++selectedAround[v];
                if (boundary(t * 3 + uint32_t(k))) splits[v] = splits[mesh.triangles[t][(k + 1) % 3]] = 1;
            }
        }
        for (uint32_t v = 0; v < nv; ++v)
            if (isCap[v] && selectedAround[v] < mesh.vertexTriangles(v).size()) splits[v] = 1;

        // Regions (Along::Region): connected pieces of the selection.
        // (also what the inset is measured on, so they are always found.)
        std::vector<uint32_t> region(nt, ~0u);
        std::vector<glm::vec3> regionNormal;
        {
            std::vector<uint32_t> parent(nt);
            std::iota(parent.begin(), parent.end(), 0u);
            auto find = [&](uint32_t x) {
                while (parent[x] != x) x = parent[x] = parent[parent[x]];
                return x;
            };
            for (uint32_t t = 0; t < nt; ++t) {
                if (!sel[t]) continue;
                for (int k = 0; k < 3; ++k) {
                    const uint32_t tw = twin[t * 3 + uint32_t(k)];
                    if (tw != kNoCorner && sel[tw / 3]) {
                        const uint32_t a = find(t), b = find(tw / 3);
                        if (a != b) parent[std::max(a, b)] = std::min(a, b);
                    }
                }
            }
            std::unordered_map<uint32_t, uint32_t> ids;
            for (uint32_t t = 0; t < nt; ++t) {
                if (!sel[t]) continue;
                const uint32_t r = find(t);
                auto [it, inserted] = ids.try_emplace(r, uint32_t(regionNormal.size()));
                if (inserted) regionNormal.push_back(glm::vec3(0.0f));
                region[t] = it->second;
                regionNormal[it->second] += cross[t];
            }
            for (glm::vec3& n : regionNormal) n = safeNormalize(n, glm::vec3(0.0f));
        }

        // The copies: cap vertex v -> result vertex (itself, or a new one).
        std::vector<uint32_t> capIndex(nv, ~0u), capOrder(nv, ~0u);
        std::vector<Gather> gather; // per cap vertex, in vertex order
        for (uint32_t v = 0; v < nv; ++v) {
            if (!isCap[v]) continue;
            if (splits[v]) {
                capIndex[v] = uint32_t(plan.source.size());
                plan.source.push_back(v);
            } else {
                capIndex[v] = v;
            }
            capOrder[v] = uint32_t(gather.size());
            gather.emplace_back();
        }
        plan.newVertices = plan.source.size() - nv;
        for (uint32_t t = 0; t < nt; ++t) {
            if (!sel[t]) continue;
            const glm::vec3 n = safeNormalize(cross[t], glm::vec3(0.0f));
            for (int k = 0; k < 3; ++k) {
                const uint32_t v = mesh.triangles[t][k];
                Gather& g = gather[capOrder[v]];
                if (options.along == ExtrudeAlong::Region) g.regionSum += regionNormal[region[t]];
                else if (glm::dot(n, n) > 0.0f) g.normals.push_back(n);
                plan.triangles[t][k] = capIndex[v];
            }
        }
        // Boundary edges: the inset directions, then the walls.
        std::vector<PieceEdge> borderEdges;
        for (uint32_t t = 0; t < nt; ++t) {
            if (!sel[t]) continue;
            const glm::vec3 n = safeNormalize(cross[t], glm::vec3(0.0f));
            for (int k = 0; k < 3; ++k) {
                const uint32_t c = t * 3 + uint32_t(k);
                if (!boundary(c)) continue;
                const uint32_t a = mesh.triangles[t][k], b = mesh.triangles[t][(k + 1) % 3];
                const glm::vec3 inward = safeNormalize(glm::cross(n, mesh.positions[b] - mesh.positions[a]), glm::vec3(0.0f));
                if (glm::dot(inward, inward) > 0.0f) {
                    gather[capOrder[a]].inward.push_back(inward);
                    gather[capOrder[b]].inward.push_back(inward);
                }
                borderEdges.push_back({region[t], a, b});
                const uint32_t a2 = capIndex[a], b2 = capIndex[b];
                plan.triangles.push_back({a, b, b2});
                plan.triangles.push_back({a, b2, a2});
                for (int i = 0; i < 2; ++i) {
                    plan.wallSource.push_back(t);
                    plan.wallFlip.push_back(0);
                    plan.wallEdge.push_back({a, b});
                    plan.cap.push_back(0);
                }
                ++plan.walls;
            }
        }
        // The inset's unit: per cap vertex the smallest extent of the pieces whose border it is on.
        const std::unordered_map<uint32_t, float> extents = pieceExtents(mesh, cross, region, borderEdges);
        std::vector<float> vertexExtent(gather.size(), std::numeric_limits<float>::max());
        for (const PieceEdge& e : borderEdges) {
            const float x = extents.at(e.piece);
            for (uint32_t v : {e.a, e.b}) vertexExtent[capOrder[v]] = std::min(vertexExtent[capOrder[v]], x);
        }
        for (uint32_t v = 0; v < nv; ++v) {
            if (!isCap[v]) continue;
            const Gather& g = gather[capOrder[v]];
            ExtrudePlan::Moved m;
            m.insetExtent = vertexExtent[capOrder[v]] == std::numeric_limits<float>::max() ? 0.0f : vertexExtent[capOrder[v]];
            m.vertex = capIndex[v];
            const glm::vec3 fallback = vertexNormal(mesh, v);
            if (options.along == ExtrudeAlong::Region) m.dir = safeNormalize(g.regionSum, fallback);
            else if (g.normals.empty()) m.dir = fallback;
            else if (options.even) m.dir = mitre(g.normals, fallback);
            else {
                glm::vec3 sum(0.0f);
                for (const glm::vec3& n : g.normals) sum += n;
                m.dir = safeNormalize(sum, fallback);
            }
            m.insetDir = g.inward.empty() ? glm::vec3(0.0f) : mitre(g.inward, glm::vec3(0.0f));
            plan.moved.push_back(m);
        }
    } else {
        // Individual: every polygon gets its own copies of its vertices and a wall on each of its edges.
        std::vector<uint32_t> polygonOf(nt, ~0u); // the lower triangle of each selected polygon
        for (uint32_t t = 0; t < nt; ++t) {
            if (!sel[t]) continue;
            const uint32_t u = mesh.quadPartnerAt(t);
            polygonOf[t] = u != Mesh::kNoQuad && sel[u] ? std::min(t, u) : t;
        }
        // Copies per (polygon, vertex), made in triangle order so the result is the same on every run.
        std::unordered_map<uint64_t, uint32_t> copyOf;
        auto key = [](uint32_t p, uint32_t v) { return (uint64_t(p) << 32) | v; };
        std::vector<glm::vec3> polygonNormal(nt, glm::vec3(0.0f));
        for (uint32_t t = 0; t < nt; ++t)
            if (sel[t]) polygonNormal[polygonOf[t]] += cross[t];
        std::vector<Gather> gather; // per new vertex
        std::vector<uint32_t> copyPolygon; // per new vertex: the polygon (its lower triangle) it belongs to
        for (uint32_t t = 0; t < nt; ++t) {
            if (!sel[t]) continue;
            const uint32_t p = polygonOf[t];
            for (int k = 0; k < 3; ++k) {
                const uint32_t v = mesh.triangles[t][k];
                auto [it, inserted] = copyOf.try_emplace(key(p, v), uint32_t(plan.source.size()));
                if (inserted) {
                    plan.source.push_back(v);
                    copyPolygon.push_back(p);
                    gather.emplace_back();
                    gather.back().normals.push_back(safeNormalize(polygonNormal[p], vertexNormal(mesh, v)));
                }
                plan.triangles[t][k] = it->second;
            }
        }
        plan.newVertices = plan.source.size() - nv;
        std::vector<PieceEdge> borderEdges;
        for (uint32_t t = 0; t < nt; ++t) {
            if (!sel[t]) continue;
            const uint32_t p = polygonOf[t];
            const glm::vec3 n = safeNormalize(polygonNormal[p], glm::vec3(0.0f));
            for (int k = 0; k < 3; ++k) {
                const uint32_t c = t * 3 + uint32_t(k), tw = twin[c];
                if (tw != kNoCorner && polygonOf[tw / 3] == p) continue; // the quad's diagonal
                const uint32_t a = mesh.triangles[t][k], b = mesh.triangles[t][(k + 1) % 3];
                const uint32_t a2 = copyOf[key(p, a)], b2 = copyOf[key(p, b)];
                const glm::vec3 inward = safeNormalize(glm::cross(n, mesh.positions[b] - mesh.positions[a]), glm::vec3(0.0f));
                if (glm::dot(inward, inward) > 0.0f) {
                    gather[a2 - nv].inward.push_back(inward);
                    gather[b2 - nv].inward.push_back(inward);
                }
                borderEdges.push_back({p, a, b});
                plan.triangles.push_back({a, b, b2});
                plan.triangles.push_back({a, b2, a2});
                for (int i = 0; i < 2; ++i) {
                    plan.wallSource.push_back(t);
                    plan.wallFlip.push_back(0);
                    plan.wallEdge.push_back({a, b});
                    plan.cap.push_back(0);
                }
                ++plan.walls;
            }
        }
        // Each polygon is its own piece, so its copies share its extent.
        std::vector<uint32_t> pieceOfTriangle(nt, ~0u);
        for (uint32_t t = 0; t < nt; ++t)
            if (sel[t]) pieceOfTriangle[t] = polygonOf[t];
        const std::unordered_map<uint32_t, float> extents = pieceExtents(mesh, cross, pieceOfTriangle, borderEdges);
        for (size_t i = 0; i < gather.size(); ++i) {
            ExtrudePlan::Moved m;
            m.vertex = uint32_t(nv + i);
            if (const auto it = extents.find(copyPolygon[i]); it != extents.end()) m.insetExtent = it->second;
            m.dir = gather[i].normals.front();
            m.insetDir = gather[i].inward.empty() ? glm::vec3(0.0f) : mitre(gather[i].inward, glm::vec3(0.0f));
            plan.moved.push_back(m);
        }
    }
    plan.firstWall = nt;
    return plan;
}

ExtrudePlan planEdgeExtrude(const Mesh& mesh, std::span<const glm::uvec2> edges, const EdgeExtrudeOptions& options) {
    ExtrudePlan plan;
    plan.faces = false;
    const size_t nt = mesh.triangleCount(), nv = mesh.vertexCount();
    if (nt == 0) {
        plan.error = "Extrude: the Part is empty";
        return plan;
    }
    // The open edges: an undirected edge with exactly one visible corner on it.
    auto ukey = [](uint32_t a, uint32_t b) { return (uint64_t(std::min(a, b)) << 32) | std::max(a, b); };
    std::unordered_map<uint64_t, std::pair<uint32_t, uint32_t>> corners; // edge -> (a corner, how many)
    corners.reserve(nt * 3);
    for (uint32_t t = 0; t < nt; ++t) {
        const glm::uvec3& tri = mesh.triangles[t];
        if (degenerate(tri)) continue;
        for (int k = 0; k < 3; ++k) {
            auto& e = corners[ukey(tri[k], tri[(k + 1) % 3])];
            if (e.second++ == 0) e.first = t * 3 + uint32_t(k);
        }
    }
    std::vector<uint32_t> picked; // the corner of each open edge taken, in input order, once
    std::unordered_map<uint64_t, uint8_t> seen;
    for (const glm::uvec2& e : edges) {
        if (e.x >= nv || e.y >= nv || e.x == e.y) continue;
        const uint64_t k = ukey(e.x, e.y);
        if (seen[k]++) continue;
        auto it = corners.find(k);
        if (it == corners.end() || it->second.second != 1) continue;
        if (mesh.triangleHidden(it->second.first / 3)) continue;
        picked.push_back(it->second.first);
    }
    if (picked.empty()) {
        plan.error = edges.empty() ? "Extrude: select some open border edges first"
                                   : "Extrude: only open border edges can be extruded (none of the selected edges is one)";
        return plan;
    }
    std::sort(picked.begin(), picked.end()); // (the same result for any order of the input)
    plan.polygons = picked.size();
    plan.source.resize(nv);
    std::iota(plan.source.begin(), plan.source.end(), 0u);
    plan.triangles = mesh.triangles;
    plan.cap.assign(nt, 0);
    plan.inheritGroup = options.sideGroup == ExtrudeSideGroup::Inherit;
    plan.wallGroup = nextFaceGroupId(mesh);

    std::unordered_map<uint32_t, uint32_t> copyOf; // border vertex -> its new vertex
    std::vector<std::vector<glm::vec3>> outward;   // per new vertex: the outward normals of its edges
    auto copy = [&](uint32_t v) {
        auto [it, inserted] = copyOf.try_emplace(v, uint32_t(plan.source.size()));
        if (inserted) {
            plan.source.push_back(v);
            outward.emplace_back();
        }
        return it->second;
    };
    for (uint32_t c : picked) {
        const uint32_t t = c / 3;
        const glm::uvec3& tri = mesh.triangles[t];
        const uint32_t a = tri[c % 3], b = tri[(c % 3 + 1) % 3];
        const uint32_t a2 = copy(a), b2 = copy(b);
        const glm::vec3 n = safeNormalize(triangleCross(mesh, t), glm::vec3(0.0f));
        const glm::vec3 out = safeNormalize(glm::cross(mesh.positions[b] - mesh.positions[a], n), glm::vec3(0.0f));
        if (glm::dot(out, out) > 0.0f) {
            outward[a2 - nv].push_back(out);
            outward[b2 - nv].push_back(out);
        }
        // The strip quad shares the edge b -> a with the owner (which has a -> b).
        plan.triangles.push_back({b, a, a2});
        plan.triangles.push_back({b, a2, b2});
        for (int i = 0; i < 2; ++i) {
            plan.wallSource.push_back(t);
            plan.wallFlip.push_back(1);
            plan.wallEdge.push_back({a, b});
            plan.cap.push_back(1);
        }
        plan.newBorder.push_back({a2, b2});
        ++plan.walls;
    }
    plan.newVertices = plan.source.size() - nv;
    const glm::vec3 fixed = safeNormalize(options.direction, glm::vec3(0.0f, 1.0f, 0.0f));
    for (size_t i = 0; i < plan.newVertices; ++i) {
        const uint32_t v = plan.source[nv + i];
        ExtrudePlan::Moved m;
        m.vertex = uint32_t(nv + i);
        m.insetDir = glm::vec3(0.0f);
        switch (options.along) {
        case EdgeExtrudeAlong::Outward: m.dir = mitre(outward[i], vertexNormal(mesh, v)); break;
        case EdgeExtrudeAlong::Normals: m.dir = vertexNormal(mesh, v); break;
        case EdgeExtrudeAlong::Vector: m.dir = fixed; break;
        }
        plan.moved.push_back(m);
    }
    plan.firstWall = nt;
    return plan;
}

float moveExtrude(const Mesh& mesh, const ExtrudePlan& plan, float distance, float inset, Mesh& out) {
    if (!plan.ok() || out.positions.size() != plan.source.size()) return 0.0f;
    if (!plan.faces) inset = 0.0f;
    placeMoved(mesh, plan, distance, inset, out.positions);
    // An inset deeper than the cap is wide folds it over: halve until nothing folds (then none at all).
    for (int tries = 0; inset != 0.0f && capFolds(mesh, plan, out.positions); ++tries) {
        inset = tries < 8 ? inset * 0.5f : 0.0f;
        placeMoved(mesh, plan, distance, inset, out.positions);
    }
    if (!out.layers.empty()) {
        for (const ExtrudePlan::Moved& m : plan.moved) {
            glm::vec3 sum(0.0f);
            for (const SculptLayer& l : out.layers) sum += l.weight * l.delta[m.vertex];
            out.base[m.vertex] = out.positions[m.vertex] - sum;
        }
    }
    std::vector<uint32_t> verts;
    verts.reserve(plan.moved.size());
    for (const ExtrudePlan::Moved& m : plan.moved) verts.push_back(m.vertex);
    std::sort(verts.begin(), verts.end());
    if (!verts.empty()) out.updateNormals(verts);
    return inset;
}

Mesh buildExtrude(const Mesh& mesh, const ExtrudePlan& plan, float distance, float inset, bool finish) {
    Mesh out;
    if (!plan.ok()) return out;
    copyVertexAttributes(mesh, plan, out);
    copyTriangleAttributes(mesh, plan, out);
    finishWallAttributes(mesh, plan, distance, out);
    if (!plan.faces) inset = 0.0f;
    placeMoved(mesh, plan, distance, inset, out.positions);
    for (int tries = 0; inset != 0.0f && capFolds(mesh, plan, out.positions); ++tries) {
        inset = tries < 8 ? inset * 0.5f : 0.0f;
        placeMoved(mesh, plan, distance, inset, out.positions);
    }
    if (!out.layers.empty()) {
        for (const ExtrudePlan::Moved& m : plan.moved) {
            glm::vec3 sum(0.0f);
            for (const SculptLayer& l : out.layers) sum += l.weight * l.delta[m.vertex];
            out.base[m.vertex] = out.positions[m.vertex] - sum;
        }
    }
    matchWallCreases(plan, out);
    if (finish) {
        out.rebuildTopology();
        out.recomputeAllNormals();
    }
    return out;
}

bool extrudeFaces(const Mesh& mesh, std::span<const uint8_t> selected, const ExtrudeOptions& options, float distance,
                  float inset, Mesh& out, std::vector<uint8_t>& newSelection, std::string& status) {
    const ExtrudePlan plan = planExtrude(mesh, selected, options);
    if (!plan.ok()) {
        status = plan.error;
        return false;
    }
    out = buildExtrude(mesh, plan, distance, inset);
    newSelection = plan.cap;
    status = "Extruded " + countText(plan.polygons, "face", "faces");
    status += plan.walls ? " (" + countText(plan.walls, "wall", "walls") + ")" : " (no border: the whole surface moved)";
    return true;
}

bool extrudeEdges(const Mesh& mesh, std::span<const glm::uvec2> edges, const EdgeExtrudeOptions& options, float distance,
                  Mesh& out, std::vector<glm::uvec2>& newBorder, std::string& status) {
    const ExtrudePlan plan = planEdgeExtrude(mesh, edges, options);
    if (!plan.ok()) {
        status = plan.error;
        return false;
    }
    out = buildExtrude(mesh, plan, distance, 0.0f);
    newBorder = plan.newBorder;
    status = "Extruded " + countText(plan.polygons, "edge", "edges");
    return true;
}

// --- The Polygon mode op ---
namespace {

float partSize(const Mesh& m) {
    const AABB box = m.bounds();
    return box.valid() ? std::max(glm::length(box.extent()), 1e-6f) : 1.0f;
}

bool polyExtrude(const PolyOpInput& in, PolyOpOutput& out, std::string& error) {
    const float size = partSize(in.mesh);
    const float distance = float(argNumber(in.args, "distance", 0.1)) * size;
    if (in.element == PolyElement::Edges) {
        if (in.edgePairs.empty()) {
            error = "Extrude needs edges: select the open border edges to extrude";
            return false;
        }
        EdgeExtrudeOptions o;
        o.along = argBool(in.args, "normals", false) ? EdgeExtrudeAlong::Normals : EdgeExtrudeAlong::Outward;
        const ExtrudePlan plan = planEdgeExtrude(in.mesh, in.edgePairs, o);
        if (!plan.ok()) {
            error = plan.error;
            return false;
        }
        out.mesh = buildExtrude(in.mesh, plan, distance, 0.0f, false);
        out.element = PolyElement::Edges;
        out.edges = plan.newBorder;
        out.message = "Extruded " + countText(plan.polygons, "edge", "edges");
        return true;
    }
    if (in.element == PolyElement::Points || in.triangles.empty()) {
        error = "Extrude works on faces or edges: switch to Faces (or Edges for an open border) and select some";
        return false;
    }
    ExtrudeOptions o;
    o.individual = argBool(in.args, "individual", false);
    o.along = argBool(in.args, "flat", false) ? ExtrudeAlong::Region : ExtrudeAlong::Normals;
    o.even = argBool(in.args, "even", true);
    const ExtrudePlan plan = planExtrude(in.mesh, in.triangleFlags, o);
    if (!plan.ok()) {
        error = plan.error;
        return false;
    }
    const float inset = float(argNumber(in.args, "inset", 0.0)); // a share of the region's narrowest width, not of the Part
    out.mesh = buildExtrude(in.mesh, plan, distance, inset, false);
    out.element = PolyElement::Faces;
    for (uint32_t t = 0; t < plan.cap.size(); ++t)
        if (plan.cap[t]) out.triangles.push_back(t);
    out.message = "Extruded " + countText(plan.polygons, "face", "faces");
    if (plan.walls) out.message += " (" + countText(plan.walls, "wall", "walls") + ")";
    return true;
}

} // namespace

void registerPolyExtrudeOp(PolyOpRegistry& reg) {
    PolyOp op;
    op.id = "extrude";
    op.label = "Extrude";
    op.description = "Push the selected faces out along their normals (in, with a negative distance) with new side walls, "
                     "as one region or face by face; selected open border edges extrude into a strip along the surface.";
    op.credit = "#225 and the ideas threads";
    op.needs = PolyElement::Faces;
    op.anyElement = true;
    op.preview = true;
    op.params = {
        {"distance", "Distance", -0.5, 0.5, 0.1, "How far out, as a fraction of the Part's size (negative: in)."},
        {"inset", "Inset", 0.0, 0.25, 0.0, "Narrow the cap first by this much (a fraction of the selected region's narrowest width, per region), so the walls slant in."},
        {"individual", "Individual", 0.0, 1.0, 0.0, "Each face on its own, with a groove between neighbours, instead of the connected region.", true},
        {"even", "Even thickness", 0.0, 1.0, 1.0, "Where the selection bends, every face still moves the full distance along its own normal.", true},
        {"flat", "Keep shape", 0.0, 1.0, 0.0, "Move the whole region along one direction (its average normal) so it keeps its shape.", true},
        {"normals", "Edges along normals", 0.0, 1.0, 0.0, "Edges: the strip stands on the surface instead of extending it.", true},
    };
    op.run = polyExtrude;
    reg.add(std::move(op));
}

} // namespace sf
