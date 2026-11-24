// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Span (core/PolySpan.h). The band between two borders is built from a monotone pairing of the two vertex
// sequences (A in hole order, B against it, so a face across an open edge runs the other way than the triangle
// already on it): walking both at once, at each step the side whose fraction along its border lags advances (a
// triangle), or both do when that keeps them closest (a quad). Rings for segments > 1 interpolate each span linearly;
// a span whose two ends are one vertex (the corners of a hole split in two halves) collapses its faces into a fan.
#include "core/PolySpan.h"

#include "core/FaceGroups.h"
#include "core/PaintLayer.h"
#include "core/PolyOps.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <unordered_map>
#include <unordered_set>

namespace sf {

namespace {

constexpr int kMaxSegments = 64;

uint64_t directedKey(uint32_t a, uint32_t b) { return (uint64_t(a) << 32) | uint64_t(b); }

// How many triangles use each directed edge of the mesh.
struct EdgeUses {
    std::unordered_map<uint64_t, uint32_t> count;
    explicit EdgeUses(const Mesh& mesh) {
        count.reserve(mesh.triangleCount() * 3);
        for (const glm::uvec3& t : mesh.triangles)
            for (int k = 0; k < 3; ++k) count[directedKey(t[k], t[(k + 1) % 3])] += 1;
    }
    uint32_t directed(uint32_t a, uint32_t b) const {
        auto it = count.find(directedKey(a, b));
        return it == count.end() ? 0u : it->second;
    }
    uint32_t undirected(uint32_t a, uint32_t b) const { return directed(a, b) + directed(b, a); }
};

bool fail(std::string& error, const char* what) {
    error = std::string("Span: ") + what;
    return false;
}

// Checks `chain` against the mesh's edges and turns it into hole order (every edge from vertex i to i+1 belongs to
// one triangle, in that direction). The chain may arrive in either order.
bool orientChain(const Mesh& mesh, const EdgeUses& uses, BorderChain chain, BorderChain& out, std::string& error) {
    const size_t n = chain.vertices.size();
    if (chain.closed ? n < 3 : n < 2) return fail(error, "a border needs at least two vertices (three in a loop)");
    std::unordered_set<uint32_t> seen;
    for (uint32_t v : chain.vertices) {
        if (v >= mesh.vertexCount()) return fail(error, "a border vertex is not in the Part");
        if (!seen.insert(v).second) return fail(error, "a border passes through one vertex twice");
    }
    const size_t edges = chain.closed ? n : n - 1;
    size_t forward = 0, backward = 0;
    for (size_t i = 0; i < edges; ++i) {
        const uint32_t a = chain.vertices[i], b = chain.vertices[(i + 1) % n];
        const uint32_t f = uses.directed(a, b), r = uses.directed(b, a);
        if (f + r == 0) return fail(error, "a border edge is not an edge of the Part");
        if (f + r > 1) return fail(error, "the borders must be open edges (faces on one side only)");
        f ? ++forward : ++backward;
    }
    if (forward && backward) return fail(error, "a border changes direction (its faces are wound inconsistently)");
    if (backward) std::reverse(chain.vertices.begin(), chain.vertices.end());
    out = std::move(chain);
    return true;
}

// One span of the pairing: a vertex of A and one of B (equal at a hole's split corners), and its ring vertices.
struct Span {
    uint32_t a, b;
    std::vector<uint32_t> ring; // segments + 1 vertices from a to b (a and b themselves at the ends)
};

struct Band {
    std::vector<Span> spans; // in order along the borders; consecutive spans bound one polygon
    std::vector<glm::vec3> newPositions;
    std::vector<uint32_t> newA, newB; // per new vertex: the span's ends
    std::vector<float> newT;          // per new vertex: its fraction from a to b
};

// The monotone pairing of two borders given each vertex's fraction along its border (tA, tB: nA + 1 and nB + 1
// entries from 0 to 1): each step advances the side that lags, or both when that keeps the two fractions closest.
std::vector<glm::uvec2> pairing(const std::vector<double>& tA, const std::vector<double>& tB) {
    const size_t nA = tA.size() - 1, nB = tB.size() - 1;
    std::vector<glm::uvec2> steps{{0, 0}};
    size_t i = 0, j = 0;
    while (i < nA || j < nB) {
        if (i < nA && j < nB) {
            const double both = std::abs(tA[i + 1] - tB[j + 1]);
            const double onlyA = std::abs(tA[i + 1] - tB[j]);
            const double onlyB = std::abs(tA[i] - tB[j + 1]);
            if (both <= onlyA + 1e-12 && both <= onlyB + 1e-12) {
                ++i;
                ++j;
            } else if (onlyA < onlyB) {
                ++i;
            } else {
                ++j;
            }
        } else if (i < nA) {
            ++i;
        } else {
            ++j;
        }
        steps.push_back({uint32_t(i), uint32_t(j)});
    }
    return steps;
}

// Fractions along a border: by vertex count when both borders have the same count (then every step is a quad,
// whatever the spacing), else by length along it (a dense stretch on one side pairs with the same stretch of the
// other, not with the same number of vertices).
std::vector<double> fractions(const Mesh& mesh, const std::vector<uint32_t>& v, bool byLength) {
    const size_t n = v.size() - 1;
    std::vector<double> t(n + 1, 0.0);
    if (!byLength) {
        for (size_t i = 0; i <= n; ++i) t[i] = double(i) / double(n);
        return t;
    }
    for (size_t i = 0; i < n; ++i) t[i + 1] = t[i] + double(glm::length(mesh.positions[v[i + 1]] - mesh.positions[v[i]]));
    const double total = t[n];
    if (total <= 0.0) {
        for (size_t i = 0; i <= n; ++i) t[i] = double(i) / double(n);
        return t;
    }
    for (double& x : t) x /= total;
    t[n] = 1.0;
    return t;
}

// Rotation of brev (B against hole order) that pairs it most closely with a: the start index whose summed squared
// span lengths (a[k] with brev at the same fraction) is least.
size_t bestRotation(const Mesh& mesh, const std::vector<uint32_t>& a, const std::vector<uint32_t>& brev) {
    const size_t n = a.size(), m = brev.size();
    size_t best = 0;
    double bestCost = std::numeric_limits<double>::max();
    for (size_t s = 0; s < m; ++s) {
        double cost = 0.0;
        for (size_t k = 0; k < n; ++k) {
            const size_t j = (s + (k * m + n / 2) / n) % m;
            const glm::vec3 d = mesh.positions[a[k]] - mesh.positions[brev[j]];
            cost += double(glm::dot(d, d));
        }
        if (cost < bestCost - 1e-12) {
            bestCost = cost;
            best = s;
        }
    }
    return best;
}

// Emits a polygon of up to four corners (consecutive duplicates collapse; fewer than three distinct corners is
// nothing) as triangles, pairing a quad's halves.
void emitPolygon(Mesh& mesh, const std::vector<glm::vec3>& newPositions, size_t nv0, uint32_t c0, uint32_t c1,
                 uint32_t c2, uint32_t c3, uint32_t group, SpanResult& result) {
    uint32_t c[4] = {c0, c1, c2, c3};
    uint32_t u[4];
    int n = 0;
    for (int k = 0; k < 4; ++k)
        if (n == 0 || c[k] != u[n - 1]) u[n++] = c[k];
    while (n > 1 && u[n - 1] == u[0]) --n;
    if (n < 3) return;
    auto pos = [&](uint32_t v) { return v < nv0 ? mesh.positions[v] : newPositions[v - nv0]; };
    const uint32_t t0 = uint32_t(mesh.triangles.size());
    if (n == 3) {
        mesh.triangles.emplace_back(u[0], u[1], u[2]);
        result.triangles.push_back(t0);
        ++result.tris;
        return;
    }
    // The shorter diagonal splits the quad (a twisted span folds less that way).
    const float d02 = glm::length(pos(u[0]) - pos(u[2])), d13 = glm::length(pos(u[1]) - pos(u[3]));
    if (d02 <= d13) {
        mesh.triangles.emplace_back(u[0], u[1], u[2]);
        mesh.triangles.emplace_back(u[0], u[2], u[3]);
    } else {
        mesh.triangles.emplace_back(u[1], u[2], u[3]);
        mesh.triangles.emplace_back(u[1], u[3], u[0]);
    }
    mesh.pairQuad(t0, t0 + 1);
    result.triangles.push_back(t0);
    result.triangles.push_back(t0 + 1);
    ++result.quads;
    (void)group;
}

// The band itself. `a` and `b` are in hole order; both closed or both open. `trusted`: the chains came from the
// mesh's own edges (spanFaces, where an outline edge on the Part's rim has no triangle left at all).
bool spanOriented(Mesh& mesh, const EdgeUses& uses, const BorderChain& a, const BorderChain& b,
                    const SpanOptions& options, SpanResult& result, std::string& error) {
    result = {};
    if (a.closed != b.closed) return fail(error, "one border is a closed loop and the other an open chain");
    const int segments = std::clamp(options.segments, 1, kMaxSegments);
    const bool closed = a.closed;
    // Shared vertices: none between loops; open chains may only meet at their ends (the corners of a hole's halves).
    std::vector<uint32_t> brev(b.vertices.rbegin(), b.vertices.rend());
    if (options.flip) std::reverse(brev.begin(), brev.end());
    {
        std::unordered_set<uint32_t> inA(a.vertices.begin(), a.vertices.end());
        for (size_t j = 0; j < brev.size(); ++j) {
            if (!inA.count(brev[j])) continue;
            const bool end = !closed && ((j == 0 && brev[j] == a.vertices.front()) ||
                                         (j + 1 == brev.size() && brev[j] == a.vertices.back()));
            if (!end) return fail(error, "the two borders share a vertex: pick borders that don't touch");
        }
    }
    const size_t nA = closed ? a.vertices.size() : a.vertices.size() - 1;
    const size_t nB = closed ? brev.size() : brev.size() - 1;
    std::vector<uint32_t> av(a.vertices), bv;
    if (closed) {
        size_t s = bestRotation(mesh, a.vertices, brev);
        const long long m = static_cast<long long>(nB);
        s = size_t(((static_cast<long long>(s) + options.twist) % m + m) % m);
        for (size_t j = 0; j <= nB; ++j) bv.push_back(brev[(s + j) % nB]);
        av.push_back(a.vertices.front());
    } else {
        bv = brev;
    }

    // Spans and their rings.
    const bool byLength = nA != nB;
    const std::vector<glm::uvec2> steps = pairing(fractions(mesh, av, byLength), fractions(mesh, bv, byLength));
    const size_t nv0 = mesh.vertexCount();
    Band band;
    std::unordered_map<uint64_t, size_t> spanIndex; // (vertex of A, vertex of B) -> span: a loop's last step is its first
    auto spanAt = [&](uint32_t i, uint32_t j) -> size_t {
        const uint64_t key = directedKey(av[i], bv[j]);
        auto it = spanIndex.find(key);
        if (it != spanIndex.end()) return it->second;
        Span s{av[i], bv[j], {}};
        s.ring.push_back(s.a);
        if (s.a != s.b) {
            for (int r = 1; r < segments; ++r) {
                const float t = float(r) / float(segments);
                s.ring.push_back(uint32_t(nv0 + band.newPositions.size()));
                band.newPositions.push_back(glm::mix(mesh.positions[s.a], mesh.positions[s.b], t));
                band.newA.push_back(s.a);
                band.newB.push_back(s.b);
                band.newT.push_back(t);
            }
        } else {
            for (int r = 1; r < segments; ++r) s.ring.push_back(s.a);
        }
        s.ring.push_back(s.b);
        band.spans.push_back(std::move(s));
        spanIndex[key] = band.spans.size() - 1;
        return band.spans.size() - 1;
    };
    for (const glm::uvec2& st : steps) spanAt(st.x, st.y);

    // Faces: between consecutive spans S and S', ring r to r + 1: (S'[r], S[r], S[r+1], S'[r+1]).
    const size_t nt0 = mesh.triangleCount();
    const uint32_t group = options.newGroup ? nextFaceGroupId(mesh) : 0u;
    for (size_t k = 0; k + 1 < steps.size(); ++k) {
        const Span& s = band.spans[spanIndex[directedKey(av[steps[k].x], bv[steps[k].y])]];
        const Span& n = band.spans[spanIndex[directedKey(av[steps[k + 1].x], bv[steps[k + 1].y])]];
        for (int r = 0; r < segments; ++r)
            emitPolygon(mesh, band.newPositions, nv0, n.ring[r], s.ring[r], s.ring[r + 1], n.ring[r + 1], group, result);
    }
    if (mesh.triangleCount() == nt0) {
        mesh.triangles.resize(nt0);
        return fail(error, "nothing to span (the borders collapse onto each other)");
    }

    // Manifold check before committing: with the new faces, no edge carries more than two triangles, and (unless
    // the second border was flipped by hand) none is run the same way twice.
    {
        std::unordered_map<uint64_t, uint32_t> added;
        for (size_t t = nt0; t < mesh.triangleCount(); ++t)
            for (int k = 0; k < 3; ++k) added[directedKey(mesh.triangles[t][k], mesh.triangles[t][(k + 1) % 3])] += 1;
        for (const auto& [key, c] : added) {
            const uint32_t x = uint32_t(key >> 32), y = uint32_t(key);
            const uint32_t fwd = c + uses.directed(x, y);
            auto rit = added.find(directedKey(y, x));
            const uint32_t back = (rit == added.end() ? 0u : rit->second) + uses.directed(y, x);
            if (fwd + back > 2 || (!options.flip && fwd > 1)) {
                mesh.triangles.resize(nt0);
                if (mesh.hasQuads()) mesh.quadPartners.resize(nt0);
                return fail(error, "the span would double an edge the Part already has (the borders are joined there "
                                   "already, or too close)");
            }
        }
    }

    // Attributes of the new vertices: interpolated between the span's ends.
    const size_t nv = nv0 + band.newPositions.size(), nt = mesh.triangleCount();
    const Mesh before = mesh.paintLayers.empty() ? Mesh() : mesh; // (copyPaintLayers reads the old layers)
    mesh.positions.insert(mesh.positions.end(), band.newPositions.begin(), band.newPositions.end());
    if (!mesh.layers.empty()) {
        for (size_t i = 0; i < band.newPositions.size(); ++i)
            mesh.base.push_back(glm::mix(mesh.base[band.newA[i]], mesh.base[band.newB[i]], band.newT[i]));
        for (SculptLayer& l : mesh.layers)
            for (size_t i = 0; i < band.newPositions.size(); ++i)
                l.delta.push_back(glm::mix(l.delta[band.newA[i]], l.delta[band.newB[i]], band.newT[i]));
    }
    if (mesh.hasMask())
        for (size_t i = 0; i < band.newPositions.size(); ++i)
            mesh.mask.push_back(glm::mix(mesh.mask[band.newA[i]], mesh.mask[band.newB[i]], band.newT[i]));
    if (mesh.hasColors())
        for (size_t i = 0; i < band.newPositions.size(); ++i)
            mesh.colors.push_back(glm::mix(mesh.colors[band.newA[i]], mesh.colors[band.newB[i]], band.newT[i]));
    if (!before.paintLayers.empty()) {
        std::vector<uint32_t> kept(nv);
        std::iota(kept.begin(), kept.begin() + std::ptrdiff_t(nv0), 0u);
        for (size_t i = 0; i < band.newPositions.size(); ++i)
            kept[nv0 + i] = band.newT[i] < 0.5f ? band.newA[i] : band.newB[i];
        copyPaintLayers(before, mesh, kept);
    }
    // Per-triangle attributes of the band.
    if (options.newGroup || mesh.hasFaceGroups()) {
        if (!mesh.hasFaceGroups()) mesh.facegroups.assign(nt0, 0u);
        mesh.facegroups.resize(nt, group);
    }
    if (mesh.hasHidden()) mesh.hidden.resize(nt, 0);
    if (mesh.hasUvs()) {
        // Each border vertex takes the mean UV of the corners around it; the rings interpolate.
        std::unordered_map<uint32_t, std::pair<glm::dvec2, int>> at;
        for (const Span& s : band.spans) {
            at[s.a];
            at[s.b];
        }
        for (size_t t = 0; t < nt0; ++t)
            for (int k = 0; k < 3; ++k) {
                auto it = at.find(mesh.triangles[t][k]);
                if (it == at.end()) continue;
                it->second.first += glm::dvec2(mesh.uvs[t * 3 + size_t(k)]);
                it->second.second += 1;
            }
        auto uvOf = [&](uint32_t v) -> glm::vec2 {
            if (v >= nv0) {
                const size_t i = v - nv0;
                const auto& pa = at[band.newA[i]];
                const auto& pb = at[band.newB[i]];
                const glm::vec2 ua = pa.second ? glm::vec2(pa.first / double(pa.second)) : glm::vec2(0.0f);
                const glm::vec2 ub = pb.second ? glm::vec2(pb.first / double(pb.second)) : glm::vec2(0.0f);
                return glm::mix(ua, ub, band.newT[i]);
            }
            const auto& p = at[v];
            return p.second ? glm::vec2(p.first / double(p.second)) : glm::vec2(0.0f);
        };
        mesh.uvs.resize(nt * 3);
        for (size_t t = nt0; t < nt; ++t)
            for (int k = 0; k < 3; ++k) mesh.uvs[t * 3 + size_t(k)] = uvOf(mesh.triangles[t][k]);
    }
    if (mesh.hasUvFlags()) mesh.uvFlags.resize(nt * 3, 0);
    if (mesh.hasCreases()) mesh.creases.resize(nt * 3, 0.0f);
    if (mesh.hasQuads()) mesh.quadPartners.resize(nt, Mesh::kNoQuad);
    if (options.finish) {
        mesh.rebuildTopology();
        mesh.recomputeAllNormals();
    }
    result.group = group;
    result.vertices.resize(band.newPositions.size());
    std::iota(result.vertices.begin(), result.vertices.end(), uint32_t(nv0));
    return true;
}

// Chains from a set of undirected edges: each vertex on at most two of them; closed where the walk returns.
bool chainsOf(std::vector<glm::uvec2> edges, std::vector<BorderChain>& out, std::string& error) {
    for (glm::uvec2& e : edges)
        if (e.x > e.y) std::swap(e.x, e.y);
    std::sort(edges.begin(), edges.end(), [](const glm::uvec2& p, const glm::uvec2& q) {
        return p.x != q.x ? p.x < q.x : p.y < q.y;
    });
    edges.erase(std::unique(edges.begin(), edges.end()), edges.end());
    std::unordered_map<uint32_t, std::vector<uint32_t>> adj;
    for (const glm::uvec2& e : edges) {
        if (e.x == e.y) return fail(error, "an edge joins a vertex to itself");
        adj[e.x].push_back(e.y);
        adj[e.y].push_back(e.x);
    }
    std::vector<uint32_t> order;
    for (const auto& [v, n] : adj) {
        if (n.size() > 2) return fail(error, "the edges branch: select borders that form loops or chains");
        order.push_back(v);
    }
    std::sort(order.begin(), order.end()); // (the same chains on every run)
    std::unordered_set<uint32_t> done;
    auto walk = [&](uint32_t start) {
        BorderChain c;
        uint32_t prev = ~0u, cur = start;
        while (true) {
            c.vertices.push_back(cur);
            done.insert(cur);
            const std::vector<uint32_t>& n = adj[cur];
            uint32_t next = ~0u;
            for (uint32_t w : n)
                if (w != prev) {
                    next = w;
                    break;
                }
            if (n.size() == 2 && n[0] == n[1]) next = ~0u; // (can't happen after unique; a guard)
            if (next == ~0u) break;
            if (next == start) {
                c.closed = true;
                break;
            }
            prev = cur;
            cur = next;
        }
        out.push_back(std::move(c));
    };
    for (uint32_t v : order) // open chains first, from their ends
        if (!done.count(v) && adj[v].size() == 1) walk(v);
    for (uint32_t v : order)
        if (!done.count(v)) walk(v);
    return true;
}

} // namespace

bool borderChains(const Mesh& mesh, std::span<const glm::uvec2> edges, std::vector<BorderChain>& out,
                  std::string& error) {
    out.clear();
    if (edges.empty()) return fail(error, "select edges on an open border first");
    std::vector<BorderChain> raw;
    if (!chainsOf(std::vector<glm::uvec2>(edges.begin(), edges.end()), raw, error)) return false;
    const EdgeUses uses(mesh);
    for (BorderChain& c : raw) {
        BorderChain oriented;
        if (!orientChain(mesh, uses, std::move(c), oriented, error)) return false;
        out.push_back(std::move(oriented));
    }
    return true;
}

bool faceOutline(const Mesh& mesh, std::span<const uint32_t> faces, std::vector<BorderChain>& out, std::string& error) {
    out.clear();
    std::unordered_set<uint32_t> set;
    for (uint32_t t : faces) {
        if (t >= mesh.triangleCount()) return fail(error, "a selected face is not in the Part");
        set.insert(t);
    }
    if (set.empty()) return fail(error, "select faces first");
    std::unordered_set<uint64_t> inside;
    for (uint32_t t : set)
        for (int k = 0; k < 3; ++k) inside.insert(directedKey(mesh.triangles[t][k], mesh.triangles[t][(k + 1) % 3]));
    std::unordered_map<uint32_t, uint32_t> next; // outline edge a -> b in the faces' winding
    for (uint32_t t : set)
        for (int k = 0; k < 3; ++k) {
            const uint32_t a = mesh.triangles[t][k], b = mesh.triangles[t][(k + 1) % 3];
            if (inside.count(directedKey(b, a))) continue;
            if (!next.emplace(a, b).second) return fail(error, "the outline of the faces branches (they touch at a point)");
        }
    std::vector<uint32_t> starts;
    for (const auto& [a, b] : next) starts.push_back(a);
    std::sort(starts.begin(), starts.end());
    std::unordered_set<uint32_t> done;
    for (uint32_t s : starts) {
        if (done.count(s)) continue;
        BorderChain c;
        uint32_t cur = s;
        while (true) {
            c.vertices.push_back(cur);
            done.insert(cur);
            auto it = next.find(cur);
            if (it == next.end()) return fail(error, "the outline of the faces is not closed");
            cur = it->second;
            if (cur == s) break;
            if (done.count(cur)) return fail(error, "the outline of the faces branches (they touch at a point)");
        }
        c.closed = true;
        out.push_back(std::move(c));
    }
    return true;
}

bool spanBorders(Mesh& mesh, const BorderChain& a, const BorderChain& b, const SpanOptions& options,
                   SpanResult& result, std::string& error) {
    result = {};
    if (mesh.triangleCount() == 0) return fail(error, "the Part is empty");
    const EdgeUses uses(mesh);
    BorderChain ha, hb;
    if (!orientChain(mesh, uses, a, ha, error) || !orientChain(mesh, uses, b, hb, error)) return false;
    return spanOriented(mesh, uses, ha, hb, options, result, error);
}

bool spanHole(Mesh& mesh, const BorderChain& loop, const SpanOptions& options, SpanResult& result,
                std::string& error) {
    result = {};
    if (mesh.triangleCount() == 0) return fail(error, "the Part is empty");
    if (!loop.closed) return fail(error, "closing a hole needs its whole border loop");
    const EdgeUses uses(mesh);
    BorderChain h;
    if (!orientChain(mesh, uses, loop, h, error)) return false;
    const size_t n = h.vertices.size();
    if (n < 4) return fail(error, "a hole of three edges is one face: use Fill Holes");
    // Split at (p, p + n/2): the pair whose two halves lie closest (a slot spans its width, not its length).
    size_t bestP = 0;
    double bestCost = std::numeric_limits<double>::max();
    const size_t half = n / 2;
    for (size_t p = 0; p < n; ++p) {
        const size_t q = (p + half) % n;
        const size_t nA = half, nB = n - half; // edges on each half
        double cost = 0.0;
        for (size_t k = 0; k <= nA; ++k) {
            const uint32_t va = h.vertices[(p + k) % n];
            const size_t j = (k * nB + nA / 2) / nA; // the matching fraction along the other half, from p backwards
            const uint32_t vb = h.vertices[(p + n - j) % n];
            const glm::vec3 d = mesh.positions[va] - mesh.positions[vb];
            cost += double(glm::dot(d, d));
        }
        (void)q;
        if (cost < bestCost - 1e-12) {
            bestCost = cost;
            bestP = p;
        }
    }
    BorderChain a, b; // both in hole order: a = p..q forward, b = q..p forward (round the rest of the loop)
    for (size_t k = 0; k <= half; ++k) a.vertices.push_back(h.vertices[(bestP + k) % n]);
    for (size_t k = half; k <= n; ++k) b.vertices.push_back(h.vertices[(bestP + k) % n]);
    SpanOptions o = options;
    o.twist = 0;
    o.flip = false;
    return spanOriented(mesh, uses, a, b, o, result, error);
}

bool spanEdges(Mesh& mesh, std::span<const glm::uvec2> edges, const SpanOptions& options, SpanResult& result,
                 std::string& error) {
    result = {};
    if (mesh.triangleCount() == 0) return fail(error, "the Part is empty");
    std::vector<BorderChain> chains;
    if (!borderChains(mesh, edges, chains, error)) return false;
    if (chains.size() == 1) {
        if (!chains[0].closed)
            return fail(error, "needs two borders: select one edge loop, Shift+double-click the other (one whole loop "
                               "closes its hole)");
        return spanHole(mesh, chains[0], options, result, error);
    }
    if (chains.size() != 2) {
        error = "Span: the edges make " + std::to_string(chains.size()) +
                " borders, it needs two: select one edge loop, Shift+double-click the other";
        return false;
    }
    const EdgeUses uses(mesh);
    return spanOriented(mesh, uses, chains[0], chains[1], options, result, error);
}

namespace {

// Removes the triangles with drop[t] != 0 in place, their now unused vertices with them, keeping the order of the
// rest and every attribute. `vertexMap` gets each old vertex's new index (~0u when gone).
void removeTriangles(Mesh& mesh, const std::vector<uint8_t>& drop, std::vector<uint32_t>& vertexMap) {
    const size_t nt = mesh.triangleCount(), nv = mesh.vertexCount();
    std::vector<uint32_t> triMap(nt, ~0u);
    std::vector<uint8_t> used(nv, 0);
    size_t keptT = 0;
    for (size_t t = 0; t < nt; ++t) {
        if (drop[t]) continue;
        triMap[t] = uint32_t(keptT++);
        for (int k = 0; k < 3; ++k) used[mesh.triangles[t][k]] = 1;
    }
    vertexMap.assign(nv, ~0u);
    size_t keptV = 0;
    for (size_t v = 0; v < nv; ++v)
        if (used[v]) vertexMap[v] = uint32_t(keptV++);
    const Mesh before = mesh.paintLayers.empty() ? Mesh() : mesh;
    std::vector<uint32_t> keptSource;
    keptSource.reserve(keptV);
    for (size_t v = 0; v < nv; ++v)
        if (used[v]) keptSource.push_back(uint32_t(v));

    auto compactV = [&](auto& arr) {
        if (arr.size() != nv) return;
        size_t w = 0;
        for (size_t v = 0; v < nv; ++v)
            if (used[v]) arr[w++] = arr[v];
        arr.resize(w);
    };
    compactV(mesh.positions);
    compactV(mesh.normals);
    compactV(mesh.mask);
    compactV(mesh.colors);
    compactV(mesh.base);
    for (SculptLayer& l : mesh.layers) compactV(l.delta);
    compactV(mesh.paintBase);
    for (PaintLayer& l : mesh.paintLayers) {
        compactV(l.color);
        compactV(l.alpha);
    }
    auto compactT = [&](auto& arr, size_t per) {
        if (arr.size() != nt * per) return;
        size_t w = 0;
        for (size_t t = 0; t < nt; ++t) {
            if (drop[t]) continue;
            for (size_t k = 0; k < per; ++k) arr[w++] = arr[t * per + k];
        }
        arr.resize(w);
    };
    std::vector<uint32_t> partners = mesh.quadPartners;
    compactT(mesh.triangles, 1);
    compactT(mesh.facegroups, 1);
    compactT(mesh.hidden, 1);
    compactT(mesh.uvs, 3);
    compactT(mesh.uvFlags, 3);
    compactT(mesh.creases, 3);
    for (glm::uvec3& t : mesh.triangles)
        for (int k = 0; k < 3; ++k) t[k] = vertexMap[t[k]];
    if (!partners.empty()) {
        mesh.quadPartners.clear();
        mesh.quadPartners.resize(keptT, Mesh::kNoQuad);
        for (size_t t = 0; t < nt; ++t) {
            const uint32_t u = partners[t];
            if (drop[t] || u == Mesh::kNoQuad || u >= nt || drop[u]) continue;
            mesh.quadPartners[triMap[t]] = triMap[u];
        }
    }
    if (!before.paintLayers.empty()) copyPaintLayers(before, mesh, keptSource);
}

} // namespace

bool spanFaces(Mesh& mesh, std::span<const uint32_t> faces, const SpanOptions& options, SpanResult& result,
                 std::string& error) {
    result = {};
    if (mesh.triangleCount() == 0) return fail(error, "the Part is empty");
    std::vector<uint8_t> selected(mesh.triangleCount(), 0);
    size_t count = 0;
    for (uint32_t t : faces) {
        if (t >= mesh.triangleCount()) return fail(error, "a selected face is not in the Part");
        if (!selected[t]) ++count;
        selected[t] = 1;
    }
    if (count == 0) return fail(error, "select faces on two sides first");
    if (count == mesh.triangleCount()) return fail(error, "the whole Part is selected: nothing would be left to span");
    // Two edge-connected patches.
    std::unordered_map<uint64_t, uint32_t> edgeTri; // undirected edge (min, max) -> a selected triangle on it
    std::vector<uint32_t> comp(mesh.triangleCount(), ~0u);
    std::vector<std::vector<uint32_t>> adj(mesh.triangleCount());
    for (size_t t = 0; t < mesh.triangleCount(); ++t) {
        if (!selected[t]) continue;
        for (int k = 0; k < 3; ++k) {
            uint32_t a = mesh.triangles[t][k], b = mesh.triangles[t][(k + 1) % 3];
            if (a > b) std::swap(a, b);
            auto [it, fresh] = edgeTri.emplace(directedKey(a, b), uint32_t(t));
            if (!fresh) {
                adj[t].push_back(it->second);
                adj[it->second].push_back(uint32_t(t));
            }
        }
    }
    std::vector<std::vector<uint32_t>> patches;
    for (size_t t = 0; t < mesh.triangleCount(); ++t) {
        if (!selected[t] || comp[t] != ~0u) continue;
        std::vector<uint32_t> patch, stack{uint32_t(t)};
        comp[t] = uint32_t(patches.size());
        while (!stack.empty()) {
            const uint32_t u = stack.back();
            stack.pop_back();
            patch.push_back(u);
            for (uint32_t w : adj[u])
                if (comp[w] == ~0u) {
                    comp[w] = uint32_t(patches.size());
                    stack.push_back(w);
                }
        }
        patches.push_back(std::move(patch));
    }
    if (patches.size() != 2) {
        error = patches.size() == 1 ? "Span: the faces are one piece: select faces on two sides (Shift+click adds)"
                                    : "Span: the faces make " + std::to_string(patches.size()) +
                                          " pieces, it needs two: select faces on two sides only";
        return false;
    }
    BorderChain outline[2];
    for (int p = 0; p < 2; ++p) {
        std::vector<BorderChain> chains;
        if (!faceOutline(mesh, patches[p], chains, error)) return false;
        if (chains.size() != 1) return fail(error, "each selection must have one outline (no holes inside it, no ring)");
        outline[p] = std::move(chains[0]);
        // Hole order is the opposite of the faces' winding.
        std::reverse(outline[p].vertices.begin(), outline[p].vertices.end());
    }
    // Remove the patches; the outlines move with the vertices.
    std::vector<uint32_t> vertexMap;
    removeTriangles(mesh, selected, vertexMap);
    for (BorderChain& c : outline)
        for (uint32_t& v : c.vertices) {
            v = vertexMap[v];
            if (v == ~0u) { // (an outline vertex is always on a kept face, unless the selection took a whole piece)
                return fail(error, "a selection covers a whole piece of the Part: nothing on the other side of its "
                                   "outline");
            }
        }
    result.removed = count;
    const EdgeUses uses(mesh);
    SpanResult r;
    const bool ok = spanOriented(mesh, uses, outline[0], outline[1], options, r, error);
    if (!ok) { // the removal is already done: the caller works on a copy, as every op does
        if (options.finish) {
            mesh.rebuildTopology();
            mesh.recomputeAllNormals();
        }
        return false;
    }
    r.removed = count;
    result = std::move(r);
    return true;
}

// --- Polygon mode op ---

bool polySpan(const PolyOpInput& in, PolyOpOutput& out, std::string& error) {
    SpanOptions o;
    o.segments = int(std::lround(argNumber(in.args, "segments", 1)));
    o.twist = int(std::lround(argNumber(in.args, "twist", 0)));
    o.flip = argBool(in.args, "flip", false);
    o.finish = false; // (the registry rebuilds topology and normals)
    out.mesh = in.mesh;
    SpanResult r;
    bool ok;
    if (in.element == PolyElement::Faces && !in.triangles.empty()) {
        ok = spanFaces(out.mesh, in.triangles, o, r, error);
    } else if (!in.edgePairs.empty()) {
        ok = spanEdges(out.mesh, in.edgePairs, o, r, error);
    } else {
        error = in.element == PolyElement::Points
                    ? "Span needs edges or faces: select the points of two border loops, or switch to Edges or Faces"
                    : "Span needs two borders: select one edge loop, Shift+double-click the other, or faces on two sides";
        return false;
    }
    if (!ok) return false;
    out.element = PolyElement::Faces;
    out.triangles = r.triangles;
    const size_t faces = r.quads + r.tris;
    out.message = "Spanned " + std::to_string(faces) + (faces == 1 ? " face" : " faces") +
                  (r.removed ? " between the two openings" : "");
    return true;
}

void registerPolySpanOp(PolyOpRegistry& reg) {
    PolyOp op;
    op.id = "span";
    op.label = "Span";
    op.description = "Span: join two edge loops or face selections with new faces. Two open borders (edge loops or "
                     "chains) get a band of quads between them; faces on two sides are removed and their openings "
                     "joined (a tunnel, a handle); one whole border loop is closed across its width. The band is a face "
                     "group of its own.";
    op.credit = "members' top wish (#225 and the ideas threads)";
    op.needs = PolyElement::Edges;
    op.anyElement = true;
    op.preview = true;
    op.params = {{"segments", "Segments", 1.0, 32.0, 1.0, "Rings of faces across the gap"},
                 {"twist", "Twist", -8.0, 8.0, 0.0, "Turns the pairing of two loops by this many vertices"}};
    PolyOpParam flip;
    flip.name = "flip";
    flip.label = "Flip";
    flip.help = "Pair the second border the other way round (for a piece that is wound inside out)";
    flip.isBool = true;
    op.params.push_back(flip);
    op.run = polySpan;
    reg.add(std::move(op));
}

} // namespace sf
