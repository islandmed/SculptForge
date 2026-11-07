// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/Backdrop.h"

#include "core/Commands.h"
#include "core/Document.h"
#include "core/FaceGroups.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numeric>
#include <unordered_map>

namespace sf {
namespace {

constexpr float kMaxThickness = 0.025f; // of the plane's width
constexpr float kMinCover = 0.7f;       // of the model's extent, along both plane directions
constexpr float kMaxRestArea = 0.8f;    // the rest's footprint, of the plane's
constexpr float kMinStandOut = 0.05f;   // how far the rest stands clear of the plane, of its width
constexpr float kMinOver = 0.5f;        // share of the rest's vertices over the plane (not over a hole in it)
constexpr int kGrid = 64;

struct UnionFind {
    std::vector<uint32_t> parent;
    explicit UnionFind(size_t n) : parent(n) { std::iota(parent.begin(), parent.end(), 0u); }
    uint32_t find(uint32_t x) {
        while (parent[x] != x) x = parent[x] = parent[parent[x]];
        return x;
    }
    void join(uint32_t a, uint32_t b) { parent[find(a)] = find(b); }
};

// Vertices at the same place share an id (generated files often split them along UV seams).
std::vector<uint32_t> weldedIds(const Mesh& mesh, float cell) {
    struct Key {
        int64_t x, y, z;
        bool operator==(const Key& o) const { return x == o.x && y == o.y && z == o.z; }
    };
    struct Hash {
        size_t operator()(const Key& k) const {
            return size_t(k.x * 73856093LL) ^ size_t(k.y * 19349663LL) ^ size_t(k.z * 83492791LL);
        }
    };
    std::unordered_map<Key, uint32_t, Hash> ids;
    ids.reserve(mesh.positions.size());
    std::vector<uint32_t> id(mesh.positions.size());
    for (size_t v = 0; v < id.size(); ++v) {
        const glm::vec3 q = glm::floor(mesh.positions[v] / cell + 0.5f);
        id[v] = ids.try_emplace(Key{int64_t(q.x), int64_t(q.y), int64_t(q.z)}, uint32_t(ids.size())).first->second;
    }
    return id;
}

// Groups of the triangles with in[t] != 0 joined through shared (welded) corners, largest area first.
std::vector<std::vector<uint32_t>> groups(const Mesh& mesh, const std::vector<uint32_t>& weld,
                                          const std::vector<uint8_t>& in) {
    UnionFind uf(mesh.positions.size());
    std::vector<uint32_t> first(mesh.positions.size(), ~0u);
    for (uint32_t v = 0; v < weld.size(); ++v) {
        uint32_t& f = first[weld[v]];
        if (f == ~0u) f = v;
        else uf.join(v, f);
    }
    for (size_t t = 0; t < mesh.triangles.size(); ++t) {
        if (!in[t]) continue;
        const glm::uvec3& tri = mesh.triangles[t];
        uf.join(tri.x, tri.y);
        uf.join(tri.x, tri.z);
    }
    std::unordered_map<uint32_t, size_t> index;
    std::vector<std::vector<uint32_t>> out;
    std::vector<float> area;
    for (uint32_t t = 0; t < mesh.triangles.size(); ++t) {
        if (!in[t]) continue;
        const glm::uvec3& tri = mesh.triangles[t];
        const auto [it, added] = index.try_emplace(uf.find(tri.x), out.size());
        if (added) {
            out.emplace_back();
            area.push_back(0.0f);
        }
        out[it->second].push_back(t);
        area[it->second] += glm::length(glm::cross(mesh.positions[tri.y] - mesh.positions[tri.x],
                                                   mesh.positions[tri.z] - mesh.positions[tri.x]));
    }
    std::vector<size_t> order(out.size());
    std::iota(order.begin(), order.end(), size_t(0));
    std::sort(order.begin(), order.end(), [&](size_t a, size_t b) { return area[a] > area[b]; });
    std::vector<std::vector<uint32_t>> sorted;
    sorted.reserve(out.size());
    for (size_t i : order) sorted.push_back(std::move(out[i]));
    return sorted;
}

// The direction a set of points varies least along (cyclic Jacobi on their covariance).
glm::vec3 flattestDirection(const Mesh& mesh, const std::vector<uint32_t>& tris) {
    glm::dvec3 mean(0.0);
    size_t n = 0;
    for (uint32_t t : tris)
        for (int c = 0; c < 3; ++c, ++n) mean += glm::dvec3(mesh.positions[mesh.triangles[t][c]]);
    mean /= double(std::max<size_t>(n, 1));
    double a[3][3] = {};
    for (uint32_t t : tris)
        for (int c = 0; c < 3; ++c) {
            const glm::dvec3 d = glm::dvec3(mesh.positions[mesh.triangles[t][c]]) - mean;
            for (int i = 0; i < 3; ++i)
                for (int j = 0; j < 3; ++j) a[i][j] += d[i] * d[j];
        }
    double v[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    for (int sweep = 0; sweep < 32; ++sweep) {
        const double off = a[0][1] * a[0][1] + a[0][2] * a[0][2] + a[1][2] * a[1][2];
        if (off < 1e-24) break;
        for (int p = 0; p < 2; ++p)
            for (int q = p + 1; q < 3; ++q) {
                if (std::abs(a[p][q]) < 1e-30) continue;
                const double theta = (a[q][q] - a[p][p]) / (2.0 * a[p][q]);
                const double t = (theta >= 0 ? 1.0 : -1.0) / (std::abs(theta) + std::sqrt(theta * theta + 1.0));
                const double c = 1.0 / std::sqrt(t * t + 1.0), s = t * c;
                for (int k = 0; k < 3; ++k) { // A = J^T A J
                    const double akp = a[k][p], akq = a[k][q];
                    a[k][p] = c * akp - s * akq;
                    a[k][q] = s * akp + c * akq;
                }
                for (int k = 0; k < 3; ++k) {
                    const double apk = a[p][k], aqk = a[q][k];
                    a[p][k] = c * apk - s * aqk;
                    a[q][k] = s * apk + c * aqk;
                }
                for (int k = 0; k < 3; ++k) {
                    const double vkp = v[k][p], vkq = v[k][q];
                    v[k][p] = c * vkp - s * vkq;
                    v[k][q] = s * vkp + c * vkq;
                }
            }
    }
    int least = 0;
    for (int i = 1; i < 3; ++i)
        if (a[i][i] < a[least][least]) least = i;
    return glm::normalize(glm::vec3(float(v[0][least]), float(v[1][least]), float(v[2][least])));
}

struct Range {
    float lo = std::numeric_limits<float>::max(), hi = std::numeric_limits<float>::lowest();
    void add(float x) {
        lo = std::min(lo, x);
        hi = std::max(hi, x);
    }
    float size() const { return hi > lo ? hi - lo : 0.0f; }
};

// Whether the triangles in `plane` (normal `n`) are a flat backdrop to everything else in `mesh`; `area` = theirs.
bool isBackdrop(const Mesh& mesh, const std::vector<uint8_t>& removed, const std::vector<uint32_t>& plane, glm::vec3 n,
                float& area) {
    const glm::vec3 u = glm::normalize(std::abs(n.x) < 0.9f ? glm::cross(n, glm::vec3(1, 0, 0)) : glm::cross(n, glm::vec3(0, 1, 0)));
    const glm::vec3 w = glm::cross(n, u);
    std::vector<uint8_t> inPlane(mesh.triangles.size(), 0);
    for (uint32_t t : plane) inPlane[t] = 1;
    // Vertices: 1 = of the plane, 2 = of the rest (kept or not yet removed), 3 = both.
    std::vector<uint8_t> role(mesh.positions.size(), 0);
    for (size_t t = 0; t < mesh.triangles.size(); ++t) {
        if (removed[t]) continue;
        for (int c = 0; c < 3; ++c) role[mesh.triangles[t][c]] |= inPlane[t] ? 1 : 2;
    }
    Range pn, pu, pw, rn, ru, rw, tu, tw;
    size_t restCount = 0;
    for (size_t v = 0; v < role.size(); ++v) {
        if (!role[v]) continue;
        const glm::vec3& p = mesh.positions[v];
        const float dn = glm::dot(p, n), du = glm::dot(p, u), dw = glm::dot(p, w);
        tu.add(du);
        tw.add(dw);
        if (role[v] & 1) {
            pn.add(dn);
            pu.add(du);
            pw.add(dw);
        }
        if (role[v] == 2) {
            rn.add(dn);
            ru.add(du);
            rw.add(dw);
            ++restCount;
        }
    }
    const float width = std::max(pu.size(), pw.size());
    if (restCount == 0 || width <= 0.0f) return false; // never the only part
    if (pn.size() > kMaxThickness * width) return false;
    if (pu.size() < kMinCover * tu.size() || pw.size() < kMinCover * tw.size()) return false;
    if (ru.size() * rw.size() > kMaxRestArea * pu.size() * pw.size()) return false;
    const float mid = 0.5f * (pn.lo + pn.hi);
    const float below = std::max(0.0f, mid - rn.lo), above = std::max(0.0f, rn.hi - mid);
    if (std::min(below, above) > 0.1f * std::max(below, above)) return false; // through the middle, not behind
    if (std::max(below, above) < std::max(4.0f * pn.size(), kMinStandOut * width)) return false;

    // Over the plane, not in its holes (a relief's raised parts sit where its flat field isn't).
    std::vector<uint8_t> cells(size_t(kGrid) * kGrid, 0);
    const float su = (kGrid - 1e-3f) / std::max(pu.size(), 1e-12f), sw = (kGrid - 1e-3f) / std::max(pw.size(), 1e-12f);
    auto cellOf = [&](float du, float dw, int& i, int& j) {
        i = int(std::floor((du - pu.lo) * su));
        j = int(std::floor((dw - pw.lo) * sw));
        return i >= 0 && j >= 0 && i < kGrid && j < kGrid;
    };
    for (uint32_t t : plane) {
        glm::vec2 q[3];
        for (int c = 0; c < 3; ++c) {
            const glm::vec3& p = mesh.positions[mesh.triangles[t][c]];
            q[c] = {(glm::dot(p, u) - pu.lo) * su, (glm::dot(p, w) - pw.lo) * sw};
            int i, j;
            if (cellOf(glm::dot(p, u), glm::dot(p, w), i, j)) cells[size_t(j) * kGrid + size_t(i)] = 1;
        }
        const float d = (q[1].x - q[0].x) * (q[2].y - q[0].y) - (q[2].x - q[0].x) * (q[1].y - q[0].y);
        if (std::abs(d) < 1e-12f) continue;
        const int i0 = std::max(0, int(std::floor(std::min({q[0].x, q[1].x, q[2].x})))),
                  i1 = std::min(kGrid - 1, int(std::ceil(std::max({q[0].x, q[1].x, q[2].x})))),
                  j0 = std::max(0, int(std::floor(std::min({q[0].y, q[1].y, q[2].y})))),
                  j1 = std::min(kGrid - 1, int(std::ceil(std::max({q[0].y, q[1].y, q[2].y}))));
        for (int j = j0; j <= j1; ++j)
            for (int i = i0; i <= i1; ++i) {
                const glm::vec2 c(float(i) + 0.5f, float(j) + 0.5f);
                const float b1 = ((c.x - q[0].x) * (q[2].y - q[0].y) - (q[2].x - q[0].x) * (c.y - q[0].y)) / d;
                const float b2 = ((q[1].x - q[0].x) * (c.y - q[0].y) - (c.x - q[0].x) * (q[1].y - q[0].y)) / d;
                if (b1 >= 0.0f && b2 >= 0.0f && b1 + b2 <= 1.0f) cells[size_t(j) * kGrid + size_t(i)] = 1;
            }
    }
    size_t over = 0;
    for (size_t v = 0; v < role.size(); ++v) {
        if (role[v] != 2) continue;
        int i, j;
        const glm::vec3& p = mesh.positions[v];
        if (cellOf(glm::dot(p, u), glm::dot(p, w), i, j) && cells[size_t(j) * kGrid + size_t(i)]) ++over;
    }
    if (float(over) < kMinOver * float(restCount)) return false;
    area = 0.0f;
    for (uint32_t t : plane) {
        const glm::uvec3& tri = mesh.triangles[t];
        area += glm::length(glm::cross(mesh.positions[tri.y] - mesh.positions[tri.x], mesh.positions[tri.z] - mesh.positions[tri.x]));
    }
    return true;
}

} // namespace

std::vector<uint8_t> flatBackdropTriangles(const Mesh& mesh, BackdropRemoval* found) {
    if (found) *found = {};
    if (mesh.triangles.size() < 2) return {};
    const AABB box = mesh.bounds();
    const glm::vec3 extent = box.max - box.min;
    const float largest = std::max({extent.x, extent.y, extent.z});
    if (!(largest > 0.0f) || !std::isfinite(largest)) return {};
    const std::vector<uint32_t> weld = weldedIds(mesh, largest * 1e-6f);
    std::vector<uint8_t> removed(mesh.triangles.size(), 0);
    BackdropRemoval result;
    for (int pass = 0; pass < 3; ++pass) { // a ground and a back wall, at most one more
        const std::vector<uint8_t> live = [&] {
            std::vector<uint8_t> l(removed.size());
            for (size_t t = 0; t < l.size(); ++t) l[t] = !removed[t];
            return l;
        }();
        struct Candidate {
            std::vector<uint32_t> tris;
            glm::vec3 normal;
        };
        std::vector<Candidate> candidates;
        // Separate pieces, flat in any direction.
        auto pieces = groups(mesh, weld, live);
        if (pieces.size() > 1)
            for (auto& p : pieces) candidates.push_back({std::move(p), {}});
        for (auto& c : candidates) c.normal = flattestDirection(mesh, c.tris);
        // Thin slabs at each side of the bounds (also joined to the subject).
        AABB liveBox;
        for (size_t t = 0; t < live.size(); ++t)
            if (live[t])
                for (int c = 0; c < 3; ++c) liveBox.expand(mesh.positions[mesh.triangles[t][c]]);
        const float slab = 0.02f * std::max({liveBox.max.x - liveBox.min.x, liveBox.max.y - liveBox.min.y,
                                             liveBox.max.z - liveBox.min.z});
        for (int axis = 0; axis < 3; ++axis)
            for (int side = 0; side < 2; ++side) {
                const float edge = side ? liveBox.max[axis] : liveBox.min[axis];
                std::vector<uint8_t> in(mesh.triangles.size(), 0);
                for (size_t t = 0; t < in.size(); ++t) {
                    if (!live[t]) continue;
                    const glm::uvec3& tri = mesh.triangles[t];
                    in[t] = std::abs(mesh.positions[tri.x][axis] - edge) <= slab &&
                            std::abs(mesh.positions[tri.y][axis] - edge) <= slab &&
                            std::abs(mesh.positions[tri.z][axis] - edge) <= slab;
                }
                auto slabs = groups(mesh, weld, in);
                if (slabs.size() > 3) slabs.resize(3);
                glm::vec3 n(0.0f);
                n[axis] = 1.0f;
                for (auto& s : slabs) candidates.push_back({std::move(s), n});
            }
        int best = -1;
        float bestArea = 0.0f;
        for (size_t i = 0; i < candidates.size(); ++i) {
            float area = 0.0f;
            if (isBackdrop(mesh, removed, candidates[i].tris, candidates[i].normal, area) && area > bestArea) {
                best = int(i);
                bestArea = area;
            }
        }
        if (best < 0) break;
        for (uint32_t t : candidates[size_t(best)].tris) removed[t] = 1;
        ++result.planes;
        result.triangles += candidates[size_t(best)].tris.size();
    }
    if (result.planes == 0) return {};
    if (found) *found = result;
    std::vector<uint8_t> keep(removed.size());
    for (size_t t = 0; t < keep.size(); ++t) keep[t] = !removed[t];
    return keep;
}

bool removeFlatBackdrop(Mesh& mesh, BackdropRemoval* removed) {
    BackdropRemoval found;
    const std::vector<uint8_t> keep = flatBackdropTriangles(mesh, &found);
    if (removed) *removed = found;
    if (keep.empty()) return false;
    mesh = extractTriangles(mesh, keep);
    mesh.recomputeAllNormals();
    return true;
}

std::string backdropRemovedNote(const BackdropRemoval& r) {
    return std::string(r.planes > 1 ? "Removed " + std::to_string(r.planes) + " flat background planes"
                                    : std::string("Removed a flat background plane")) +
           " (" + std::to_string(r.triangles) + " triangles)";
}

void registerBackdropCommands(CommandRegistry& registry) {
    registry.add(
        {"remove_flat_background",
         "Remove the flat plane an image's background often becomes in Image to 3D: a thin wall behind or ground under "
         "the subject, covering most of the model's bounds (Image to 3D does this itself unless remove_background=false). "
         "A relief or a coin on its own is kept. One undo step.",
         {},
         [](Document& doc, const Args&) -> CommandResult {
             if (doc.mesh.triangleCount() == 0) return {false, "The Part is empty"};
             if (doc.mesh.micro) return {false, "Remove flat background: apply or remove the micro level first"};
             Mesh m = doc.mesh;
             BackdropRemoval r;
             if (!removeFlatBackdrop(m, &r)) return {false, "No flat background plane found on the active Part"};
             doc.replaceMeshUndoable(std::move(m), "Remove Flat Background");
             return {true, backdropRemovedNote(r) + " - Ctrl+Z brings it back"};
         }});
}

} // namespace sf
