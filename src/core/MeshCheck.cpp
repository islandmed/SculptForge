// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/MeshCheck.h"

#include "core/CloseHoles.h"
#include "core/Commands.h"
#include "core/FaceGroups.h"
#include "core/Parallel.h"

#include <algorithm>
#include <cmath>
#include <mutex>
#include <numeric>
#include <sstream>
#include <unordered_map>

namespace sf {

namespace {

constexpr uint32_t kNone = ~0u;

bool degenerate(const glm::uvec3& t) { return t.x == t.y || t.y == t.z || t.x == t.z; }

// Does triangle t run a -> b (rather than b -> a)? (t has both.)
bool runs(const glm::uvec3& t, uint32_t a, uint32_t b) {
    return (t.x == a && t.y == b) || (t.y == a && t.z == b) || (t.z == a && t.x == b);
}

bool has(const glm::uvec3& t, uint32_t v) { return t.x == v || t.y == v || t.z == v; }

// The non-degenerate triangles around `a` that also use `b`, ascending, into `out`.
void edgeTriangles(const Mesh& mesh, uint32_t a, uint32_t b, std::vector<uint32_t>& out) {
    out.clear();
    for (uint32_t t : mesh.vertexTriangles(a))
        if (has(mesh.triangles[t], b) && !degenerate(mesh.triangles[t])) out.push_back(t);
    std::sort(out.begin(), out.end());
}

// Zero area: three distinct points (all but) in one line, the triangle's height under a millionth of its longest edge.
bool zeroArea(const Mesh& mesh, const glm::uvec3& t) {
    const glm::vec3 a = mesh.positions[t.x], b = mesh.positions[t.y], c = mesh.positions[t.z];
    const float longest = std::max({glm::dot(b - a, b - a), glm::dot(c - b, c - b), glm::dot(a - c, a - c)});
    const float cross = glm::length(glm::cross(b - a, c - a));
    return cross <= 1e-6f * longest;
}

// The triangle earlier than t on the same three points, either winding.
bool duplicateOfEarlier(const Mesh& mesh, uint32_t t) {
    const glm::uvec3 a = mesh.triangles[t];
    for (uint32_t u : mesh.vertexTriangles(a.x)) {
        if (u >= t) continue;
        const glm::uvec3 b = mesh.triangles[u];
        if (has(b, a.y) && has(b, a.z) && !degenerate(b)) return true;
    }
    return false;
}

struct UnionFind {
    std::vector<uint32_t> parent;
    explicit UnionFind(size_t n) : parent(n) { std::iota(parent.begin(), parent.end(), 0u); }
    uint32_t find(uint32_t v) {
        while (parent[v] != v) v = parent[v] = parent[parent[v]];
        return v;
    }
    void unite(uint32_t a, uint32_t b) { // the lower index is the root
        a = find(a), b = find(b);
        if (a != b) parent[std::max(a, b)] = std::min(a, b);
    }
};

// Does the ray from o along +x (tilted off the axes, so it misses edges and points) cross triangle tri?
bool crossesX(const Mesh& mesh, const glm::uvec3& tri, const glm::dvec3& o) {
    static const glm::dvec3 dir = glm::normalize(glm::dvec3(1.0, 0.0031415926, 0.0027182818));
    const glm::dvec3 a(mesh.positions[tri.x]), e1 = glm::dvec3(mesh.positions[tri.y]) - a,
        e2 = glm::dvec3(mesh.positions[tri.z]) - a;
    const glm::dvec3 p = glm::cross(dir, e2);
    const double det = glm::dot(e1, p);
    if (std::abs(det) < 1e-300) return false;
    const glm::dvec3 s = o - a;
    const double u = glm::dot(s, p) / det;
    if (u < 0.0 || u > 1.0) return false;
    const glm::dvec3 q = glm::cross(s, e1);
    const double v = glm::dot(dir, q) / det;
    return v >= 0.0 && u + v <= 1.0 && glm::dot(e2, q) / det > 0.0;
}

// Which triangles to flip so each piece is wound one way (its majority), and closed pieces face outward. A closed
// piece inside another keeps its majority either way: a hollow's wall faces into the hollow, an eye in a head out.
std::vector<uint8_t> flipsNeeded(const Mesh& mesh, size_t& count) {
    const size_t nt = mesh.triangleCount();
    std::vector<uint8_t> flip(nt, 0), seen(nt, 0);
    std::vector<uint32_t> around;
    const glm::vec3 origin = mesh.bounds().valid() ? mesh.bounds().center() : glm::vec3(0.0f);
    struct Piece {
        std::vector<uint32_t> tris;
        bool closed = true, invert = false;
        double volume = 0.0; // as wound after the majority rule
        AABB box;
    };
    std::vector<Piece> pieces;
    count = 0;
    for (uint32_t seed = 0; seed < nt; ++seed) {
        if (seen[seed] || degenerate(mesh.triangles[seed]) || duplicateOfEarlier(mesh, seed)) continue;
        Piece pc;
        std::vector<uint32_t>& piece = pc.tris;
        piece.assign(1, seed);
        seen[seed] = 1;
        for (size_t i = 0; i < piece.size(); ++i) {
            const uint32_t t = piece[i];
            const glm::uvec3 tri = mesh.triangles[t];
            for (int k = 0; k < 3; ++k) {
                const uint32_t a = tri[k], b = tri[(k + 1) % 3];
                edgeTriangles(mesh, a, b, around);
                if (around.size() != 2) { // an open or non-manifold edge: no neighbour to agree with
                    pc.closed = false;
                    continue;
                }
                const uint32_t u = around[0] == t ? around[1] : around[0];
                if (seen[u]) continue;
                seen[u] = 1;
                // Consistent neighbours run the shared edge the other way: the same way means one of them is flipped.
                const bool sameWay = runs(mesh.triangles[u], a, b);
                flip[u] = uint8_t(flip[t] ^ (sameWay ? 1 : 0));
                piece.push_back(u);
            }
        }
        size_t ones = 0;
        for (uint32_t t : piece) ones += flip[t];
        pc.invert = ones * 2 > piece.size();
        if (pc.closed)
            for (uint32_t t : piece) {
                const glm::uvec3 tri = mesh.triangles[t];
                const glm::dvec3 a(mesh.positions[tri.x] - origin), b(mesh.positions[tri.y] - origin),
                    c(mesh.positions[tri.z] - origin);
                const double v = glm::dot(a, glm::cross(b, c));
                pc.volume += (flip[t] != 0) != pc.invert ? -v : v;
                for (int k = 0; k < 3; ++k) pc.box.expand(mesh.positions[tri[k]]);
            }
        pieces.push_back(std::move(pc));
    }
    // A closed piece is turned the right way out only when it isn't inside another (an even number of closed pieces
    // round it: a solid inside a hollow counts as outside). Only pieces whose box holds it can be round it.
    for (Piece& pc : pieces) {
        if (!pc.closed) continue;
        const glm::uvec3 tri = mesh.triangles[pc.tris[0]];
        const glm::dvec3 at = (glm::dvec3(mesh.positions[tri.x]) + glm::dvec3(mesh.positions[tri.y]) +
                               glm::dvec3(mesh.positions[tri.z])) / 3.0;
        const glm::vec3 p(at);
        size_t depth = 0;
        for (const Piece& other : pieces) {
            if (&other == &pc || !other.closed || glm::any(glm::lessThan(p, other.box.min)) ||
                glm::any(glm::greaterThan(p, other.box.max)))
                continue;
            size_t crossings = 0;
            for (uint32_t t : other.tris) crossings += crossesX(mesh, mesh.triangles[t], at);
            depth += crossings % 2;
        }
        if (depth % 2 == 0 && pc.volume < 0.0) pc.invert = !pc.invert;
    }
    for (const Piece& pc : pieces)
        for (uint32_t t : pc.tris) {
            if (pc.invert) flip[t] ^= 1;
            count += flip[t];
        }
    return flip;
}

// The first two triangles of a non-manifold edge stay joined across it: the first and the first one after it wound
// the other way (a consistent pair), else the second.
std::pair<uint32_t, uint32_t> keptPair(const Mesh& mesh, uint32_t a, uint32_t b, const std::vector<uint32_t>& around) {
    const bool first = runs(mesh.triangles[around[0]], a, b);
    for (size_t i = 1; i < around.size(); ++i)
        if (runs(mesh.triangles[around[i]], a, b) != first) return {around[0], around[i]};
    return {around[0], around[1]};
}

// The triangles of a non-manifold edge a b in the pairs that stay joined across it, the kept pair first. Taken in
// order round the edge, each triangle wound b -> a pairs with the next one, which closes the solid between them
// (two solids touching along an edge come apart whole, neither opened). When the edge's triangles don't
// alternate that way (an odd count, a wall inside a solid, mixed windings), only the kept pair.
void edgePairs(const Mesh& mesh, uint32_t a, uint32_t b, const std::vector<uint32_t>& around,
               std::vector<std::pair<uint32_t, uint32_t>>& pairs) {
    pairs.clear();
    const glm::dvec3 pa(mesh.positions[a]), axis = glm::dvec3(mesh.positions[b]) - pa;
    const double len2 = glm::dot(axis, axis);
    if (around.size() % 2 == 0 && len2 > 0.0 && std::isfinite(len2)) {
        // Angles round a -> b (counter-clockwise looking down the axis), measured from the first triangle's side.
        auto side = [&](uint32_t t) {
            const glm::uvec3 tri = mesh.triangles[t];
            const uint32_t c = tri.x != a && tri.x != b ? tri.x : tri.y != a && tri.y != b ? tri.y : tri.z;
            const glm::dvec3 d = glm::dvec3(mesh.positions[c]) - pa;
            return d - axis * (glm::dot(d, axis) / len2);
        };
        const glm::dvec3 x = side(around[0]), y = glm::cross(axis, x) / std::sqrt(len2);
        std::vector<std::pair<double, uint32_t>> order;
        for (uint32_t t : around) {
            const glm::dvec3 d = side(t);
            order.push_back({std::atan2(glm::dot(d, y), glm::dot(d, x)), t});
        }
        std::sort(order.begin(), order.end());
        const size_t n = order.size();
        for (size_t start = 0; start < 2 && pairs.empty(); ++start) {
            for (size_t i = start; i < n + start; i += 2) {
                const uint32_t t = order[i % n].second, u = order[(i + 1) % n].second;
                if (runs(mesh.triangles[t], a, b) || !runs(mesh.triangles[u], a, b)) {
                    pairs.clear();
                    break;
                }
                pairs.push_back({t, u});
            }
        }
        if (!pairs.empty()) {
            const auto kept = keptPair(mesh, a, b, around); // the same kept pair as before when it is one of them
            for (auto& p : pairs)
                if (p.first == kept.first || p.second == kept.first) std::swap(p, pairs.front());
            return;
        }
    }
    pairs.push_back(keptPair(mesh, a, b, around));
}

Mesh withoutTriangles(const Mesh& mesh, const std::vector<uint8_t>& drop) {
    std::vector<uint8_t> keep(drop.size());
    for (size_t t = 0; t < drop.size(); ++t) keep[t] = !drop[t];
    return extractTriangles(mesh, keep);
}

// Every triangle's corners through `target`; those that collapse are dropped.
Mesh remapped(const Mesh& mesh, const std::vector<uint32_t>& target) {
    std::vector<glm::uvec3> corners(mesh.triangles);
    std::vector<uint8_t> keep(corners.size());
    for (size_t t = 0; t < corners.size(); ++t) {
        for (int k = 0; k < 3; ++k) corners[t][k] = target[corners[t][k]];
        keep[t] = !degenerate(corners[t]);
    }
    TriangleRebuild how;
    how.corners = &corners;
    return rebuildTriangles(mesh, keep, how);
}

// The triangles around a point (ascending: `ring`) and which of them are joined (`join`, over places in `ring`).
struct Fans {
    std::vector<uint32_t> ring;
    UnionFind join{0};
    uint32_t at(uint32_t t) const { return uint32_t(std::lower_bound(ring.begin(), ring.end(), t) - ring.begin()); }
    bool together(uint32_t t, uint32_t u) { return join.find(at(t)) == join.find(at(u)); }
};

// The triangles around v joined across its ordinary edges (two triangles) only.
Fans ordinaryFans(const Mesh& mesh, uint32_t v) {
    Fans f;
    for (uint32_t t : mesh.vertexTriangles(v))
        if (!degenerate(mesh.triangles[t])) f.ring.push_back(t);
    std::sort(f.ring.begin(), f.ring.end());
    f.join = UnionFind(f.ring.size());
    std::vector<uint32_t> around;
    for (uint32_t w : mesh.neighbors(v)) {
        edgeTriangles(mesh, v, w, around);
        if (around.size() == 2) f.join.unite(f.at(around[0]), f.at(around[1]));
    }
    return f;
}

// The pairs that stay joined across the non-manifold edge a b, chosen once for both its points: two triangles wound
// opposite ways that are one sheet already round a or round b (two solids touching along the edge each stay
// closed), else the pairs round the edge (edgePairs).
void sheetPairs(const Mesh& mesh, uint32_t a, uint32_t b, const std::vector<uint32_t>& around, Fans& fa, Fans& fb,
                std::vector<std::pair<uint32_t, uint32_t>>& pairs) {
    pairs.clear();
    std::vector<uint8_t> used(around.size(), 0);
    for (size_t i = 0; i < around.size(); ++i)
        for (size_t j = i + 1; j < around.size() && !used[i]; ++j) {
            const uint32_t t = around[i], u = around[j];
            if (used[j] || runs(mesh.triangles[t], a, b) == runs(mesh.triangles[u], a, b)) continue;
            if (fa.together(t, u) || fb.together(t, u)) {
                pairs.push_back({t, u});
                used[i] = used[j] = 1;
            }
        }
    if (pairs.empty()) edgePairs(mesh, a, b, around, pairs);
}

// Loosens what the fans couldn't: each triangle past the kept pair of an edge still shared by three or more gets its
// own copies of the edge's two points. (A fan can wrap round to the kept pair's through ordinary edges, as where a
// solid is pinched to an edge it is joined round at both ends: splitting a point leaves the edge shared.) Returns how
// many edges.
size_t detachExtras(Mesh& mesh) {
    const size_t nv = mesh.vertexCount();
    std::vector<glm::uvec3> corners(mesh.triangles);
    std::vector<uint32_t> source(nv), around;
    std::iota(source.begin(), source.end(), 0u);
    size_t n = 0;
    for (uint32_t a = 0; a < nv; ++a)
        for (uint32_t b : mesh.neighbors(a)) {
            if (b <= a) continue;
            edgeTriangles(mesh, a, b, around);
            if (around.size() < 3) continue;
            ++n;
            const auto [p, q] = keptPair(mesh, a, b, around);
            for (uint32_t t : around) { // (the fans already kept every pair they could; what is left takes no risks)
                if (t == p || t == q) continue;
                for (int k = 0; k < 3; ++k) {
                    const uint32_t v = mesh.triangles[t][k];
                    if ((v == a || v == b) && corners[t][k] == v) {
                        corners[t][k] = uint32_t(source.size());
                        source.push_back(v);
                    }
                }
            }
        }
    if (!n) return 0;
    TriangleRebuild how;
    how.corners = &corners;
    how.source = &source;
    mesh = rebuildTriangles(mesh, std::vector<uint8_t>(mesh.triangleCount(), 1), how);
    return n;
}

uint64_t mix(uint64_t x) { // splitmix64's finaliser
    x ^= x >> 30;
    x *= 0xbf58476d1ce4e5b9ull;
    x ^= x >> 27;
    x *= 0x94d049bb133111ebull;
    return x ^ (x >> 31);
}

} // namespace

std::vector<uint32_t> weldTargets(const Mesh& mesh, float distance, size_t* merged, bool maskedOnly) {
    const size_t nv = mesh.vertexCount();
    std::vector<uint32_t> target(nv);
    std::iota(target.begin(), target.end(), 0u);
    size_t n = 0;
    const AABB box = mesh.bounds();
    const bool masked = maskedOnly && mesh.hasMask();
    if (nv > 1 && box.valid() && (masked || !maskedOnly)) {
        // A grid of cells at least twice `distance` wide holding the kept points: a point's matches are in its cell
        // and, along each axis, at most the one neighbour on the side it is near (8 cells at most). Kept points are
        // over `distance` apart, so a cell holds only a few of them, whatever the distance.
        const float d = std::max(0.0f, std::isfinite(distance) ? distance : 0.0f);
        const float cell = std::max({2.0f * d, glm::length(box.extent()) * 1e-6f, 1e-30f});
        constexpr int kBits = 21, kMax = (1 << kBits) - 1;
        auto coord = [&](float v, float lo) { return std::clamp(int((v - lo) / cell), 0, kMax); };
        auto key = [](int x, int y, int z) { return (uint64_t(x) << (2 * kBits)) | (uint64_t(y) << kBits) | uint64_t(z); };
        size_t cap = 16;
        while (cap < nv * 2) cap <<= 1;
        std::vector<uint64_t> keys(cap, ~0ull);
        std::vector<uint32_t> head(cap, kNone), next(nv, kNone);
        auto slot = [&](uint64_t k) {
            size_t i = mix(k) & (cap - 1);
            while (keys[i] != ~0ull && keys[i] != k) i = (i + 1) & (cap - 1);
            return i;
        };
        const float d2 = d * d;
        for (uint32_t v = 0; v < nv; ++v) {
            const glm::vec3 p = mesh.positions[v];
            if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)) continue; // stays, never matched
            if (masked && mesh.maskAt(v) < 0.5f) continue; // Masked only: an unmasked point stays, never matched
            const int cx = coord(p.x, box.min.x), cy = coord(p.y, box.min.y), cz = coord(p.z, box.min.z);
            // The neighbour cell each axis needs: -1 / +1 when p is within d of that face, else none (0).
            auto side = [&](float v, float lo, int c) {
                const float local = v - lo - float(c) * cell;
                return local <= d ? -1 : local >= cell - d ? 1 : 0;
            };
            const int sx = side(p.x, box.min.x, cx), sy = side(p.y, box.min.y, cy), sz = side(p.z, box.min.z, cz);
            uint32_t best = kNone;
            float bestD = d2;
            for (int dx = std::min(0, sx); dx <= std::max(0, sx); ++dx)
                for (int dy = std::min(0, sy); dy <= std::max(0, sy); ++dy)
                    for (int dz = std::min(0, sz); dz <= std::max(0, sz); ++dz) {
                        const int x = cx + dx, y = cy + dy, z = cz + dz;
                        if (x < 0 || y < 0 || z < 0 || x > kMax || y > kMax || z > kMax) continue;
                        const size_t i = slot(key(x, y, z));
                        for (uint32_t u = keys[i] == ~0ull ? kNone : head[i]; u != kNone; u = next[u]) {
                            const glm::vec3 e = mesh.positions[u] - p;
                            const float dd = glm::dot(e, e);
                            if (dd < bestD || (dd == bestD && (best == kNone || u < best))) {
                                best = u;
                                bestD = dd;
                            }
                        }
                    }
            if (best != kNone) {
                target[v] = best;
                ++n;
                continue;
            }
            const uint64_t k = key(cx, cy, cz);
            const size_t i = slot(k);
            keys[i] = k;
            next[v] = head[i];
            head[i] = v;
        }
    }
    if (merged) *merged = n;
    return target;
}

Mesh weldByDistance(const Mesh& mesh, float distance, size_t* merged, bool maskedOnly) {
    size_t n = 0;
    const std::vector<uint32_t> target = weldTargets(mesh, distance, &n, maskedOnly);
    if (merged) *merged = n;
    return n ? remapped(mesh, target) : mesh;
}

const char* meshIssueName(MeshIssue issue) {
    switch (issue) {
    case MeshIssue::NonManifold: return "non_manifold";
    case MeshIssue::Holes: return "holes";
    case MeshIssue::Duplicate: return "duplicate";
    case MeshIssue::Degenerate: return "degenerate";
    case MeshIssue::ZeroArea: return "zero_area";
    case MeshIssue::Flipped: return "flipped";
    case MeshIssue::Isolated: return "isolated";
    default: return "";
    }
}

bool parseMeshIssue(const std::string& name, MeshIssue& out) {
    for (int i = 0; i < kMeshIssueCount; ++i)
        if (name == meshIssueName(MeshIssue(i))) {
            out = MeshIssue(i);
            return true;
        }
    return false;
}

size_t MeshReport::total() const {
    size_t n = 0;
    for (size_t c : count) n += c;
    return n;
}

std::string MeshReport::summary() const {
    static const char* const what[kMeshIssueCount][2] = {
        {"non-manifold edge", "non-manifold edges"}, {"hole", "holes"},
        {"duplicate triangle", "duplicate triangles"}, {"degenerate triangle", "degenerate triangles"},
        {"zero-area triangle", "zero-area triangles"}, {"flipped triangle", "flipped triangles"},
        {"isolated point", "isolated points"}};
    std::string s;
    for (int i = 0; i < kMeshIssueCount; ++i) {
        if (!count[i]) continue;
        if (!s.empty()) s += ", ";
        s += std::to_string(count[i]) + " " + what[i][count[i] == 1 ? 0 : 1];
    }
    return s.empty() ? "No problems found" : s;
}

MeshReport checkMesh(const Mesh& mesh) {
    MeshReport r;
    auto& c = r.count;
    const size_t nv = mesh.vertexCount(), nt = mesh.triangleCount();
    // The per-triangle and per-point counts in parallel chunks, each with its own tally.
    std::mutex lock;
    parallelFor(nt, [&](size_t begin, size_t end) {
        size_t degen = 0, dup = 0, zero = 0;
        for (size_t t = begin; t < end; ++t) {
            const glm::uvec3 tri = mesh.triangles[t];
            if (degenerate(tri)) ++degen;
            else if (duplicateOfEarlier(mesh, uint32_t(t))) ++dup;
            else if (zeroArea(mesh, tri)) ++zero;
        }
        const std::lock_guard<std::mutex> hold(lock);
        c[int(MeshIssue::Degenerate)] += degen;
        c[int(MeshIssue::Duplicate)] += dup;
        c[int(MeshIssue::ZeroArea)] += zero;
    });
    // Edges: non-manifold ones counted, open ones (one triangle) gathered to count the loops they form.
    std::vector<std::pair<uint32_t, uint32_t>> open;
    parallelFor(nv, [&](size_t begin, size_t end) {
        size_t isolated = 0, nonManifold = 0;
        std::vector<uint32_t> around;
        std::vector<std::pair<uint32_t, uint32_t>> edges;
        for (size_t a = begin; a < end; ++a) {
            if (mesh.vertexTriangles(uint32_t(a)).size() == 0) {
                ++isolated;
                continue;
            }
            for (uint32_t b : mesh.neighbors(uint32_t(a))) {
                if (b <= a) continue;
                edgeTriangles(mesh, uint32_t(a), b, around);
                if (around.size() >= 3) ++nonManifold;
                else if (around.size() == 1) edges.push_back({uint32_t(a), b});
            }
        }
        const std::lock_guard<std::mutex> hold(lock);
        c[int(MeshIssue::Isolated)] += isolated;
        c[int(MeshIssue::NonManifold)] += nonManifold;
        open.insert(open.end(), edges.begin(), edges.end());
    });
    // Holes: the connected sets of open edges (two loops touching at one point count as one).
    if (!open.empty()) {
        std::vector<uint32_t> points;
        points.reserve(open.size() * 2);
        for (const auto& [a, b] : open) points.insert(points.end(), {a, b});
        std::sort(points.begin(), points.end());
        points.erase(std::unique(points.begin(), points.end()), points.end());
        auto index = [&](uint32_t v) { return uint32_t(std::lower_bound(points.begin(), points.end(), v) - points.begin()); };
        UnionFind loops(points.size());
        for (const auto& [a, b] : open) loops.unite(index(a), index(b));
        size_t n = 0;
        for (uint32_t i = 0; i < points.size(); ++i) n += loops.find(i) == i;
        c[int(MeshIssue::Holes)] = n;
    }
    flipsNeeded(mesh, c[int(MeshIssue::Flipped)]);
    return r;
}

bool fixMesh(const Mesh& mesh, MeshIssue issue, Mesh& out, size_t& fixed) {
    const size_t nv = mesh.vertexCount(), nt = mesh.triangleCount();
    fixed = 0;
    switch (issue) {
    case MeshIssue::Degenerate:
    case MeshIssue::Duplicate: {
        std::vector<uint8_t> drop(nt, 0);
        for (uint32_t t = 0; t < nt; ++t) {
            const bool d = degenerate(mesh.triangles[t]);
            drop[t] = issue == MeshIssue::Degenerate ? d : !d && duplicateOfEarlier(mesh, t);
            fixed += drop[t];
        }
        if (!fixed) return false;
        out = withoutTriangles(mesh, drop);
        return true;
    }
    case MeshIssue::ZeroArea: {
        UnionFind points(nv);
        for (uint32_t t = 0; t < nt; ++t) {
            const glm::uvec3 tri = mesh.triangles[t];
            if (degenerate(tri) || duplicateOfEarlier(mesh, t) || !zeroArea(mesh, tri)) continue;
            int shortest = 0;
            float best = INFINITY;
            for (int k = 0; k < 3; ++k) {
                const glm::vec3 e = mesh.positions[tri[(k + 1) % 3]] - mesh.positions[tri[k]];
                if (glm::dot(e, e) < best) best = glm::dot(e, e), shortest = k;
            }
            points.unite(tri[shortest], tri[(shortest + 1) % 3]);
            ++fixed;
        }
        if (!fixed) return false;
        std::vector<uint32_t> target(nv);
        for (uint32_t v = 0; v < nv; ++v) target[v] = points.find(v);
        out = remapped(mesh, target);
        return true;
    }
    case MeshIssue::NonManifold: {
        // Around each point of a non-manifold edge, its triangles fall into fans joined across ordinary edges, then
        // across each non-manifold edge by the pairs chosen for it (sheetPairs, the same at both its points); every
        // fan past the first gets a copy of the point. What stays shared after that (a solid pinched to an edge at
        // both ends), detachExtras loosens.
        std::vector<uint32_t> around;
        std::vector<std::pair<uint32_t, uint32_t>> edges; // the non-manifold ones, a < b
        for (uint32_t a = 0; a < nv; ++a)
            for (uint32_t b : mesh.neighbors(a)) {
                if (b <= a) continue;
                edgeTriangles(mesh, a, b, around);
                if (around.size() >= 3) edges.push_back({a, b});
            }
        fixed = edges.size();
        if (!fixed) return false;
        // The fans through ordinary edges alone, per point of those edges.
        std::unordered_map<uint32_t, Fans> fans;
        for (const auto& [a, b] : edges)
            for (uint32_t v : {a, b})
                if (!fans.count(v)) fans.emplace(v, ordinaryFans(mesh, v));
        std::vector<glm::uvec3> corners(mesh.triangles);
        std::vector<uint32_t> source(nv);
        std::iota(source.begin(), source.end(), 0u);
        std::unordered_map<uint32_t, std::vector<std::pair<uint32_t, uint32_t>>> joins; // per point: ring places
        std::vector<std::pair<uint32_t, uint32_t>> pairs;
        for (const auto& [a, b] : edges) {
            edgeTriangles(mesh, a, b, around);
            Fans &fa = fans.at(a), &fb = fans.at(b);
            sheetPairs(mesh, a, b, around, fa, fb, pairs);
            for (const auto& [p, q] : pairs) {
                joins[a].push_back({fa.at(p), fa.at(q)});
                joins[b].push_back({fb.at(p), fb.at(q)});
            }
        }
        std::vector<uint32_t> fanOf;
        for (auto& [v, f] : fans) {
            for (const auto& [p, q] : joins[v]) f.join.unite(p, q);
            fanOf.assign(f.ring.size(), kNone); // fan root -> the id its triangles use for v
            for (size_t i = 0; i < f.ring.size(); ++i) {
                const uint32_t root = f.join.find(uint32_t(i));
                if (root == 0) continue; // the first fan keeps the point
                if (fanOf[root] == kNone) {
                    fanOf[root] = uint32_t(source.size());
                    source.push_back(v);
                }
                glm::uvec3& c = corners[f.ring[i]];
                for (int k = 0; k < 3; ++k)
                    if (mesh.triangles[f.ring[i]][k] == v) c[k] = fanOf[root];
            }
        }
        TriangleRebuild how;
        how.corners = &corners;
        how.source = &source;
        out = rebuildTriangles(mesh, std::vector<uint8_t>(nt, 1), how);
        detachExtras(out);
        return true;
    }
    case MeshIssue::Flipped: {
        const std::vector<uint8_t> flip = flipsNeeded(mesh, fixed);
        if (!fixed) return false;
        TriangleRebuild how;
        how.flip = &flip;
        out = rebuildTriangles(mesh, std::vector<uint8_t>(nt, 1), how);
        return true;
    }
    case MeshIssue::Isolated: {
        for (uint32_t v = 0; v < nv; ++v) fixed += mesh.vertexTriangles(v).size() == 0;
        if (!fixed) return false;
        out = extractTriangles(mesh, std::vector<uint8_t>(nt, 1));
        return true;
    }
    case MeshIssue::Holes: {
        Mesh m = mesh;
        CloseHolesStats stats;
        std::string error;
        if (!closeHoles(m, CloseHolesOptions{}, stats, error) || stats.filled == 0) return false;
        // A cap is dropped (its hole stays) when it lands on other triangles or is wound against them: the edge of a
        // loose sheet or fin capped over itself makes copies of its triangles, not a closed piece.
        const size_t first = nt, added = m.triangleCount() - first;
        UnionFind caps(added);
        std::unordered_map<uint64_t, uint32_t> edgeCap;
        for (uint32_t i = 0; i < added; ++i) {
            const glm::uvec3 tri = m.triangles[first + i];
            for (int k = 0; k < 3; ++k) {
                const uint32_t a = std::min(tri[k], tri[(k + 1) % 3]), b = std::max(tri[k], tri[(k + 1) % 3]);
                const auto [it, fresh] = edgeCap.try_emplace((uint64_t(a) << 32) | b, i);
                if (!fresh) caps.unite(it->second, i);
            }
        }
        std::vector<uint8_t> bad(added, 0), drop(m.triangleCount(), 0);
        std::vector<uint32_t> around;
        size_t flips = 0;
        const std::vector<uint8_t> flip = flipsNeeded(m, flips);
        for (uint32_t i = 0; i < added; ++i) {
            const uint32_t t = uint32_t(first + i);
            const glm::uvec3 tri = m.triangles[t];
            bool lands = flip[t] || degenerate(tri) || duplicateOfEarlier(m, t) || zeroArea(m, tri);
            for (int k = 0; k < 3 && !lands; ++k) {
                edgeTriangles(m, tri[k], tri[(k + 1) % 3], around);
                lands = around.size() >= 3;
            }
            if (lands) bad[caps.find(i)] = 1;
        }
        size_t dropped = 0;
        for (uint32_t i = 0; i < added; ++i) {
            dropped += bad[i]; // (one per cap: its root)
            drop[first + i] = bad[caps.find(i)];
        }
        if (dropped >= stats.filled) return false;
        Mesh capped = dropped ? withoutTriangles(m, drop) : std::move(m);
        // (Capping one of two loops that touch can leave the other as two: never more holes than before.)
        if (dropped && checkMesh(capped).count[int(MeshIssue::Holes)] >= checkMesh(mesh).count[int(MeshIssue::Holes)])
            return false;
        fixed = stats.filled - dropped;
        out = std::move(capped);
        return true;
    }
    default: return false;
    }
}

namespace {

// Fix all's order: deletions first (they clear what the others would trip on), the collapse, then the splits, the
// winding (it needs the fins loose) and the points left unused.
constexpr MeshIssue kFixAllOrder[] = {MeshIssue::Degenerate, MeshIssue::Duplicate,   MeshIssue::ZeroArea,
                                      MeshIssue::NonManifold, MeshIssue::Flipped, MeshIssue::Isolated};

size_t withoutHoles(const MeshReport& r) { return r.total() - r.count[int(MeshIssue::Holes)]; }

} // namespace

bool fixMeshAll(const Mesh& mesh, Mesh& out, MeshReport& fixed, MeshReport* left) {
    fixed = {};
    MeshReport now = checkMesh(mesh);
    Mesh best;
    const Mesh* m = &mesh;
    for (int pass = 0; pass < kMaxFixPasses && withoutHoles(now) > 0; ++pass) {
        Mesh cur;
        const Mesh* p = m;
        MeshReport did;
        for (MeshIssue issue : kFixAllOrder) {
            Mesh next;
            size_t n = 0;
            if (!fixMesh(*p, issue, next, n)) continue;
            cur = std::move(next);
            p = &cur;
            did.count[int(issue)] += n;
        }
        if (p == m) break; // nothing changed
        const MeshReport after = checkMesh(*p);
        if (withoutHoles(after) >= withoutHoles(now)) break; // no better: keep the last pass that was
        for (int i = 0; i < kMeshIssueCount; ++i) fixed.count[i] += did.count[i];
        best = std::move(cur);
        m = &best;
        now = after;
    }
    if (left) *left = now;
    if (m == &mesh) return false;
    out = std::move(best);
    return true;
}

std::string unfixedSummary(const MeshReport& left, bool holesSkipped) {
    std::string s;
    auto add = [&](const std::string& part) { s += (s.empty() ? "" : "; ") + part; };
    for (int i = 0; i < kMeshIssueCount; ++i) {
        const size_t n = left.count[i];
        if (!n) continue;
        MeshReport one;
        one.count[i] = n;
        const std::string what = one.summary();
        switch (MeshIssue(i)) {
        case MeshIssue::Holes:
            add(holesSkipped ? what + " left open (Fix all doesn't cap holes: a mesh can be open on purpose; use the "
                                      "Holes row's Fix)"
                             : what + " couldn't be capped (the edge of a loose sheet or fin, or flipped or "
                                      "non-manifold triangles along it: Fix all first, then delete or close what's "
                                      "left by hand)");
            break;
        case MeshIssue::Flipped:
            add(what + " couldn't be turned (a one-sided, twisted piece can't be wound one way everywhere)");
            break;
        default: add(what + " couldn't be fixed (each fix brings them back: they need work by hand)"); break;
        }
    }
    return s;
}

void registerMeshCheckCommands(CommandRegistry& reg) {
    using T = ParamSpec::Type;

    reg.add({"weld_points",
             "Merge by distance: merge the points of the active Part closer than a distance (one undo step). "
             "Triangles that collapse are removed; each merged point takes the kept one's position and attributes.",
             {{"distance", T::Number, "Largest distance between points that merge, in scene units (0 = only "
                                      "points exactly on top of each other).", true},
              {"preview", T::Bool, "Only count the points that would merge; change nothing.", false},
              {"masked_only", T::Bool, "Only masked points merge (with each other); default false.", false}},
             [](Document& doc, const Args& a) -> CommandResult {
                 const double d = argNumber(a, "distance", 0.0);
                 if (!(d >= 0.0) || !std::isfinite(d)) return {false, "distance must be 0 or more"};
                 if (doc.mesh.vertexCount() == 0) return {false, "The mesh is empty"};
                 size_t n = 0;
                 const bool maskedOnly = argBool(a, "masked_only", false);
                 if (argBool(a, "preview", false)) {
                     weldTargets(doc.mesh, float(d), &n, maskedOnly);
                     return {true, std::to_string(n) + " points would merge"};
                 }
                 Mesh m = weldByDistance(doc.mesh, float(d), &n, maskedOnly);
                 if (n == 0)
                     return {false, maskedOnly && !doc.mesh.hasMask() ? "Masked only: nothing is masked"
                                                                      : "No points are that close"};
                 doc.replaceMeshUndoable(std::move(m), "Merge by Distance");
                 return {true, "Merged " + std::to_string(n) + " points, " + std::to_string(doc.mesh.vertexCount()) + " left"};
             }});

    reg.add({"mesh_check",
             "Check the active Part for non-manifold edges, holes, duplicate, degenerate, zero-area and flipped "
             "triangles and isolated points. Changes nothing (mesh_fix repairs).",
             {},
             [](Document& doc, const Args&) -> CommandResult {
                 const MeshReport r = checkMesh(doc.mesh);
                 std::string s = r.summary() + " (";
                 for (int i = 0; i < kMeshIssueCount; ++i)
                     s += std::string(i ? " " : "") + meshIssueName(MeshIssue(i)) + "=" + std::to_string(r.count[i]);
                 return {true, s + ")"};
             }});

    reg.add({"mesh_fix",
             "Fix one kind of problem mesh_check finds, as one undo step: non_manifold (loosen the extra triangles), "
             "holes (cap them), duplicate / degenerate (delete), zero_area (collapse), flipped (rewind), isolated "
             "(remove). all fixes everything except holes (an open mesh may be open on purpose).",
             {{"issue", T::String, "non_manifold, holes, duplicate, degenerate, zero_area, flipped, isolated or all.", true}},
             [](Document& doc, const Args& a) -> CommandResult {
                 const std::string name = argString(a, "issue");
                 Mesh out;
                 MeshReport fixed, left;
                 bool changed = false;
                 const bool all = name == "all";
                 if (all) {
                     changed = fixMeshAll(doc.mesh, out, fixed, &left);
                 } else if (MeshIssue one; parseMeshIssue(name, one)) {
                     size_t n = 0;
                     changed = fixMesh(doc.mesh, one, out, n);
                     fixed.count[int(one)] = n;
                     left.count[int(one)] = checkMesh(changed ? out : doc.mesh).count[int(one)];
                 } else {
                     return {false, "Unknown issue '" + name + "'"};
                 }
                 const std::string rest = unfixedSummary(left, all);
                 if (!changed) return {false, rest.empty() ? "Nothing to fix" : "Nothing fixed: " + rest};
                 doc.replaceMeshUndoable(std::move(out), "Fix Mesh");
                 return {true, "Fixed " + fixed.summary() + (rest.empty() ? "" : ". Left: " + rest)};
             }});
}

} // namespace sf
