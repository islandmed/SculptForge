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

// Which triangles to flip so each piece is wound one way (its majority), and closed pieces face outward.
std::vector<uint8_t> flipsNeeded(const Mesh& mesh, size_t& count) {
    const size_t nt = mesh.triangleCount();
    std::vector<uint8_t> flip(nt, 0), seen(nt, 0);
    std::vector<uint32_t> piece, around;
    const glm::vec3 origin = mesh.bounds().valid() ? mesh.bounds().center() : glm::vec3(0.0f);
    count = 0;
    for (uint32_t seed = 0; seed < nt; ++seed) {
        if (seen[seed] || degenerate(mesh.triangles[seed]) || duplicateOfEarlier(mesh, seed)) continue;
        piece.assign(1, seed);
        seen[seed] = 1;
        bool closed = true;
        for (size_t i = 0; i < piece.size(); ++i) {
            const uint32_t t = piece[i];
            const glm::uvec3 tri = mesh.triangles[t];
            for (int k = 0; k < 3; ++k) {
                const uint32_t a = tri[k], b = tri[(k + 1) % 3];
                edgeTriangles(mesh, a, b, around);
                if (around.size() != 2) { // an open or non-manifold edge: no neighbour to agree with
                    closed = false;
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
        bool invert = ones * 2 > piece.size();
        if (closed) { // a closed piece wound inside out (negative volume) is turned the right way out as a whole
            double volume = 0.0;
            for (uint32_t t : piece) {
                const glm::uvec3 tri = mesh.triangles[t];
                const glm::dvec3 a(mesh.positions[tri.x] - origin), b(mesh.positions[tri.y] - origin),
                    c(mesh.positions[tri.z] - origin);
                const double v = glm::dot(a, glm::cross(b, c));
                volume += (flip[t] != 0) != invert ? -v : v;
            }
            if (volume < 0.0) invert = !invert;
        }
        for (uint32_t t : piece) {
            if (invert) flip[t] ^= 1;
            count += flip[t];
        }
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

uint64_t mix(uint64_t x) { // splitmix64's finaliser
    x ^= x >> 30;
    x *= 0xbf58476d1ce4e5b9ull;
    x ^= x >> 27;
    x *= 0x94d049bb133111ebull;
    return x ^ (x >> 31);
}

} // namespace

std::vector<uint32_t> weldTargets(const Mesh& mesh, float distance, size_t* merged) {
    const size_t nv = mesh.vertexCount();
    std::vector<uint32_t> target(nv);
    std::iota(target.begin(), target.end(), 0u);
    size_t n = 0;
    const AABB box = mesh.bounds();
    if (nv > 1 && box.valid()) {
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

Mesh weldByDistance(const Mesh& mesh, float distance, size_t* merged) {
    size_t n = 0;
    const std::vector<uint32_t> target = weldTargets(mesh, distance, &n);
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
        // Around each point of a non-manifold edge, its triangles fall into fans joined across ordinary edges (and
        // across each non-manifold edge only by its kept pair); every fan past the first gets a copy of the point.
        std::vector<uint8_t> onEdge(nv, 0);
        std::vector<uint32_t> around;
        for (uint32_t a = 0; a < nv; ++a)
            for (uint32_t b : mesh.neighbors(a)) {
                if (b <= a) continue;
                edgeTriangles(mesh, a, b, around);
                if (around.size() >= 3) {
                    onEdge[a] = onEdge[b] = 1;
                    ++fixed;
                }
            }
        if (!fixed) return false;
        std::vector<glm::uvec3> corners(mesh.triangles);
        std::vector<uint32_t> source(nv);
        std::iota(source.begin(), source.end(), 0u);
        std::vector<uint32_t> ring, fanOf;
        for (uint32_t v = 0; v < nv; ++v) {
            if (!onEdge[v]) continue;
            ring.clear();
            for (uint32_t t : mesh.vertexTriangles(v))
                if (!degenerate(mesh.triangles[t])) ring.push_back(t);
            std::sort(ring.begin(), ring.end());
            UnionFind fans(ring.size());
            auto at = [&](uint32_t t) { return uint32_t(std::lower_bound(ring.begin(), ring.end(), t) - ring.begin()); };
            for (uint32_t w : mesh.neighbors(v)) {
                edgeTriangles(mesh, v, w, around);
                if (around.size() == 2) fans.unite(at(around[0]), at(around[1]));
                else if (around.size() >= 3) {
                    const auto [p, q] = keptPair(mesh, std::min(v, w), std::max(v, w), around);
                    fans.unite(at(p), at(q));
                }
            }
            fanOf.assign(ring.size(), kNone); // fan root -> the id its triangles use for v
            for (size_t i = 0; i < ring.size(); ++i) {
                const uint32_t root = fans.find(uint32_t(i));
                if (root == 0) continue; // the first fan keeps the point
                if (fanOf[root] == kNone) {
                    fanOf[root] = uint32_t(source.size());
                    source.push_back(v);
                }
                glm::uvec3& c = corners[ring[i]];
                for (int k = 0; k < 3; ++k)
                    if (mesh.triangles[ring[i]][k] == v) c[k] = fanOf[root];
            }
        }
        TriangleRebuild how;
        how.corners = &corners;
        how.source = &source;
        out = rebuildTriangles(mesh, std::vector<uint8_t>(nt, 1), how);
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
        fixed = stats.filled;
        out = std::move(m);
        return true;
    }
    default: return false;
    }
}

void registerMeshCheckCommands(CommandRegistry& reg) {
    using T = ParamSpec::Type;

    reg.add({"weld_points",
             "Merge by distance: merge the points of the active Part closer than a distance (one undo step). "
             "Triangles that collapse are removed; each merged point takes the kept one's position and attributes.",
             {{"distance", T::Number, "Largest distance between points that merge, in scene units (0 = only "
                                      "points exactly on top of each other).", true},
              {"preview", T::Bool, "Only count the points that would merge; change nothing.", false}},
             [](Document& doc, const Args& a) -> CommandResult {
                 const double d = argNumber(a, "distance", 0.0);
                 if (!(d >= 0.0) || !std::isfinite(d)) return {false, "distance must be 0 or more"};
                 if (doc.mesh.vertexCount() == 0) return {false, "The mesh is empty"};
                 size_t n = 0;
                 if (argBool(a, "preview", false)) {
                     weldTargets(doc.mesh, float(d), &n);
                     return {true, std::to_string(n) + " points would merge"};
                 }
                 Mesh m = weldByDistance(doc.mesh, float(d), &n);
                 if (n == 0) return {false, "No points are that close"};
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
                 std::vector<MeshIssue> issues;
                 if (name == "all")
                     issues = {MeshIssue::Degenerate, MeshIssue::Duplicate, MeshIssue::ZeroArea, MeshIssue::NonManifold,
                               MeshIssue::Flipped, MeshIssue::Isolated};
                 else if (MeshIssue one; parseMeshIssue(name, one)) issues = {one};
                 else return {false, "Unknown issue '" + name + "'"};
                 Mesh current;
                 const Mesh* m = &doc.mesh;
                 std::string done;
                 for (MeshIssue issue : issues) {
                     Mesh next;
                     size_t fixed = 0;
                     if (!fixMesh(*m, issue, next, fixed)) continue;
                     current = std::move(next);
                     m = &current;
                     MeshReport one;
                     one.count[int(issue)] = fixed;
                     done += (done.empty() ? "" : ", ") + one.summary();
                 }
                 if (done.empty()) return {false, "Nothing to fix"};
                 doc.replaceMeshUndoable(std::move(current), "Fix Mesh");
                 return {true, "Fixed " + done};
             }});
}

} // namespace sf
