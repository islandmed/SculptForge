// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The Boolean blend (see BooleanBlend.h).

#include "core/BooleanBlend.h"

#include "core/SurfaceQuery.h"
#include "core/ThreadPool.h"

#include <algorithm>
#include <iterator>
#include <cmath>
#include <limits>
#include <unordered_map>
#include <unordered_set>

namespace sf {

double smoothMin(double a, double b, double k) {
    if (!(k > 0.0)) return std::min(a, b);
    const double h = std::max(k - std::abs(a - b), 0.0) / k;
    return std::min(a, b) - h * h * k * 0.25;
}

namespace {

using glm::vec3;

constexpr float kFar = std::numeric_limits<float>::infinity();

uint64_t edgeKey(uint32_t u, uint32_t v) { return (uint64_t(std::min(u, v)) << 32) | std::max(u, v); }

// Splits the marked edges of every triangle (1 -> 2, 2 -> 3, 3 -> 4 triangles), new vertices at the midpoints with their
// attributes interpolated; face groups and `side` are inherited.
void splitMarked(Mesh& m, std::vector<uint8_t>& side, const std::unordered_set<uint64_t>& marked) {
    const bool colors = m.hasColors(), uvs = m.hasUvs(), groups = m.hasFaceGroups();
    std::unordered_map<uint64_t, uint32_t> mid;
    mid.reserve(marked.size());
    auto midOf = [&](uint32_t u, uint32_t v) -> uint32_t {
        const uint64_t k = edgeKey(u, v);
        if (!marked.count(k)) return ~0u;
        auto [it, fresh] = mid.emplace(k, uint32_t(m.positions.size()));
        if (fresh) {
            m.positions.push_back((m.positions[u] + m.positions[v]) * 0.5f);
            if (colors) m.colors.push_back((m.colors[u] + m.colors[v]) * 0.5f);
        }
        return it->second;
    };
    std::vector<glm::uvec3> tris;
    std::vector<glm::vec2> uv;
    std::vector<uint32_t> fg;
    std::vector<uint8_t> sd;
    tris.reserve(m.triangles.size() + 3 * marked.size());
    if (uvs) uv.reserve(m.uvs.size() + 9 * marked.size());
    if (groups) fg.reserve(tris.capacity());
    sd.reserve(tris.capacity());
    for (size_t t = 0; t < m.triangles.size(); ++t) {
        const glm::uvec3 tri = m.triangles[t];
        const uint32_t v[3] = {tri.x, tri.y, tri.z};
        uint32_t e[3];
        int count = 0;
        for (int i = 0; i < 3; ++i) {
            e[i] = midOf(v[i], v[(i + 1) % 3]);
            if (e[i] != ~0u) ++count;
        }
        // Corner "ids" 0..2 are the triangle's corners, 3..5 the midpoints of edges 0..2: one emit for all cases.
        glm::vec2 cuv[6];
        if (uvs) {
            for (int i = 0; i < 3; ++i) cuv[i] = m.uvs[t * 3 + size_t(i)];
            for (int i = 0; i < 3; ++i) cuv[3 + i] = (cuv[i] + cuv[(i + 1) % 3]) * 0.5f;
        }
        auto emit = [&](int c0, int c1, int c2) {
            auto id = [&](int c) { return c < 3 ? v[c] : e[c - 3]; };
            tris.push_back({id(c0), id(c1), id(c2)});
            if (uvs) {
                uv.push_back(cuv[c0]);
                uv.push_back(cuv[c1]);
                uv.push_back(cuv[c2]);
            }
            if (groups) fg.push_back(m.facegroups[t]);
            sd.push_back(side[t]);
        };
        if (count == 0) {
            emit(0, 1, 2);
        } else if (count == 3) {
            emit(0, 3, 5);
            emit(3, 1, 4);
            emit(5, 4, 2);
            emit(3, 4, 5);
        } else if (count == 1) {
            int i = 0;
            while (e[i] == ~0u) ++i;
            const int j = (i + 1) % 3, k = (i + 2) % 3;
            emit(i, 3 + i, k);
            emit(3 + i, j, k);
        } else { // two edges i and i + 1 share corner j; the quad left is split along its shorter diagonal
            int i = 0;
            while (!(e[i] != ~0u && e[(i + 1) % 3] != ~0u)) ++i;
            const int j = (i + 1) % 3, k = (i + 2) % 3;
            emit(3 + i, j, 3 + j);
            const vec3 &pi = m.positions[v[i]], &pk = m.positions[v[k]];
            const vec3 &mi = m.positions[e[i]], &mj = m.positions[e[j]];
            if (glm::dot(pi - mj, pi - mj) <= glm::dot(pk - mi, pk - mi)) {
                emit(i, 3 + i, 3 + j);
                emit(i, 3 + j, k);
            } else {
                emit(i, 3 + i, k);
                emit(3 + i, 3 + j, k);
            }
        }
    }
    m.triangles = std::move(tris);
    if (uvs) m.uvs = std::move(uv);
    if (groups) m.facegroups = std::move(fg);
    side = std::move(sd);
}

} // namespace

void blendBooleanSeam(Mesh& m, std::vector<uint8_t>& side, const std::vector<uint32_t>& parent, const Mesh& a, const Mesh& b,
                      BooleanOp op, const std::vector<std::array<vec3, 2>>& seam, const BooleanBlendOptions& o,
                      BooleanBlendStats* stats) {
    const float r = float(o.radius);
    if (!(r > 0.0f) || seam.empty() || m.triangleCount() == 0) return;
    // 1. The seam, and the operands' surfaces near it (the only part the blend can feel).
    SegmentBVH seamBvh;
    AABB seamBox;
    {
        std::vector<vec3> sa, sb;
        sa.reserve(seam.size());
        sb.reserve(seam.size());
        for (const auto& s : seam) {
            sa.push_back(s[0]);
            sb.push_back(s[1]);
            seamBox.expand(s[0]);
            seamBox.expand(s[1]);
        }
        seamBvh.build(std::move(sa), std::move(sb));
    }
    // The surfaces the blend feels: not the operands as given but the parts of them that survive in the result: the
    // operands' own triangles (well shaped, unlike the cut's slivers along the rim, whose normals are noise) that a
    // result triangle near the seam came from. A piece the Boolean removed (a cutter's wall inside the base, the sphere
    // inside a slab, the cap beyond a cut) is no crease on the result and must not pull the band: with the whole
    // operands, boxes standing on one floor grew a ridge under the floor along the footprint, and a lens's flat face
    // sank towards the cap that was cut away. The set is grown across smooth edges only, as far as the band can probe: a
    // triangle cut by the seam counts whole, and the field must stay smooth for the band's width past the seam (the
    // parents alone end in a jagged rim, and the fillet's depth then rippled along it: 108 degrees on a lens's rim
    // instead of 12); across a sharp edge the growth would bring back a removed face (a cutter's wall under its own
    // top, inside the base) and the ridge it pulls.
    const float reach = 4.0f * r;
    TriangleBVH bvh[2];
    {
        const Mesh* src[2] = {&a, &b};
        std::vector<uint8_t> used[2] = {std::vector<uint8_t>(a.triangleCount(), 0), std::vector<uint8_t>(b.triangleCount(), 0)};
        std::vector<uint8_t> keep(m.triangleCount(), 0);
        poolFor(m.triangleCount(), [&](size_t b0, size_t e0) {
            SurfaceHit hit;
            for (size_t t = b0; t < e0; ++t) {
                const glm::uvec3 tri = m.triangles[t];
                const vec3 &p0 = m.positions[tri.x], &p1 = m.positions[tri.y], &p2 = m.positions[tri.z];
                const vec3 c = (p0 + p1 + p2) / 3.0f;
                const float rad = std::sqrt(std::max({glm::dot(p0 - c, p0 - c), glm::dot(p1 - c, p1 - c), glm::dot(p2 - c, p2 - c)}));
                const vec3 lo = seamBox.min - vec3(reach + rad), hi = seamBox.max + vec3(reach + rad);
                if (glm::any(glm::lessThan(c, lo)) || glm::any(glm::greaterThan(c, hi))) continue;
                if (seamBvh.closestPoint(c, reach + rad, hit)) keep[t] = 1;
            }
        }, 4096);
        for (size_t t = 0; t < keep.size(); ++t)
            if (keep[t] && parent[t] < used[side[t]].size()) used[side[t]][parent[t]] = 1;
        for (int s = 0; s < 2; ++s) {
            const Mesh& o = *src[s];
            std::vector<vec3> fn(o.triangleCount());
            for (size_t t = 0; t < o.triangleCount(); ++t) {
                const glm::uvec3 tri = o.triangles[t];
                const vec3 n = glm::cross(o.positions[tri.y] - o.positions[tri.x], o.positions[tri.z] - o.positions[tri.x]);
                const float l = glm::length(n);
                fn[t] = l > 0.0f ? n / l : vec3(0.0f);
            }
            std::unordered_map<uint64_t, std::array<uint32_t, 2>> across; // the two triangles on an edge
            across.reserve(o.triangleCount() * 3);
            for (size_t t = 0; t < o.triangleCount(); ++t)
                for (int i = 0; i < 3; ++i) {
                    auto [it, fresh] = across.emplace(edgeKey(o.triangles[t][i], o.triangles[t][(i + 1) % 3]), std::array<uint32_t, 2>{uint32_t(t), ~0u});
                    if (!fresh) it->second[1] = uint32_t(t);
                }
            // Grown as far as the band can probe (2r past the seam: a Newton step looks that far), not by a count of
            // rings: a count is a mesh's own distance (three rings of a dense cutter are a hair; three of a coarse
            // face's are a quarter of it), and where the piece ended short of that a wide fillet felt the piece's
            // jagged rim instead of the surface's own continuation (a sawtooth ring on a dish's rim following the
            // face's grid; spikes along a dense box's seam at the preview's coarser band: artist-qa 11-24).
            const float grow = 2.0f * r;
            std::vector<uint32_t> queue;
            for (size_t t = 0; t < used[s].size(); ++t)
                if (used[s][t]) queue.push_back(uint32_t(t));
            SurfaceHit hit;
            for (size_t qi = 0; qi < queue.size(); ++qi) {
                const uint32_t t = queue[qi];
                for (int i = 0; i < 3; ++i) {
                    const auto it = across.find(edgeKey(o.triangles[t][i], o.triangles[t][(i + 1) % 3]));
                    if (it == across.end()) continue;
                    const uint32_t u = it->second[0] == t ? it->second[1] : it->second[0];
                    if (u == ~0u || used[s][u] || !(glm::dot(fn[t], fn[u]) > 0.5f)) continue; // (a smooth edge: under 60 degrees)
                    const glm::uvec3 tu = o.triangles[u];
                    const vec3 &p0 = o.positions[tu.x], &p1 = o.positions[tu.y], &p2 = o.positions[tu.z];
                    const vec3 c = (p0 + p1 + p2) / 3.0f;
                    const float rad = std::sqrt(std::max({glm::dot(p0 - c, p0 - c), glm::dot(p1 - c, p1 - c), glm::dot(p2 - c, p2 - c)}));
                    if (!seamBvh.closestPoint(c, grow + rad, hit)) continue;
                    used[s][u] = 1;
                    queue.push_back(u);
                }
            }
            std::vector<glm::uvec3> part;
            for (size_t t = 0; t < used[s].size(); ++t)
                if (used[s][t]) part.push_back(o.triangles[t]);
            bvh[s].build(o.positions, part);
        }
    }
    if (bvh[0].empty() || bvh[1].empty()) return;
    // The normal at a closest point: the face's inside it, the two faces' mean on an edge, the angle-weighted mean at a
    // vertex (a pseudonormal). A result vertex on an operand's own sharp edge then moves along the bisector, as its
    // neighbours on either face nearly do; the face's normal alone would crease the fillet along that edge.
    struct Normals {
        std::vector<vec3> face, vertex;
        std::unordered_map<uint64_t, vec3> edge;
        std::unordered_map<uint64_t, std::array<uint32_t, 2>> edgeTris; // the two triangles on an edge (~0u: one)
        std::vector<uint32_t> vertStart, vertTris;                     // each vertex's triangles
        std::vector<uint8_t> open;                                     // vertex on an edge with one triangle (the piece's rim)
        std::vector<uint8_t> smooth;                                   // every triangle at the vertex within 60 degrees of its pseudonormal
    } normals[2];
    for (int s = 0; s < 2; ++s) {
        const auto& P = bvh[s].positions();
        const auto& T = bvh[s].triangles();
        Normals& n = normals[s];
        n.face.resize(T.size());
        n.vertex.assign(P.size(), vec3(0.0f));
        for (size_t t = 0; t < T.size(); ++t) {
            const glm::uvec3 tri = T[t];
            const vec3 fn = glm::cross(P[tri.y] - P[tri.x], P[tri.z] - P[tri.x]);
            const float l = glm::length(fn);
            n.face[t] = l > 0.0f ? fn / l : vec3(0.0f);
            for (int i = 0; i < 3; ++i) {
                const vec3 e1 = P[tri[(i + 1) % 3]] - P[tri[i]], e2 = P[tri[(i + 2) % 3]] - P[tri[i]];
                const float l1 = glm::length(e1), l2 = glm::length(e2);
                // (a zero-length edge has no angle; normalizing it would put NaN into every normal it touches)
                const float angle = l1 > 0.0f && l2 > 0.0f ? std::acos(glm::clamp(glm::dot(e1, e2) / (l1 * l2), -1.0f, 1.0f)) : 0.0f;
                n.vertex[tri[i]] += n.face[t] * angle;
                auto [it, fresh] = n.edge.emplace(edgeKey(tri[i], tri[(i + 1) % 3]), n.face[t]);
                if (!fresh) it->second += n.face[t];
                auto [et, efresh] = n.edgeTris.emplace(edgeKey(tri[i], tri[(i + 1) % 3]), std::array<uint32_t, 2>{uint32_t(t), ~0u});
                if (!efresh) et->second[1] = uint32_t(t);
            }
        }
        n.open.assign(P.size(), 0);
        for (const auto& [k, et] : n.edgeTris)
            if (et[1] == ~0u) n.open[uint32_t(k >> 32)] = n.open[uint32_t(k & 0xffffffffu)] = 1;
        n.vertStart.assign(P.size() + 1, 0);
        for (const glm::uvec3& tri : T)
            for (int i = 0; i < 3; ++i) ++n.vertStart[tri[i] + 1];
        for (size_t v = 0; v < P.size(); ++v) n.vertStart[v + 1] += n.vertStart[v];
        n.vertTris.resize(n.vertStart.back());
        {
            std::vector<uint32_t> fill(n.vertStart.begin(), n.vertStart.end() - 1);
            for (size_t t = 0; t < T.size(); ++t)
                for (int i = 0; i < 3; ++i) n.vertTris[fill[T[t][i]]++] = uint32_t(t);
        }
        for (vec3& v : n.vertex) {
            const float l = glm::length(v);
            if (l > 0.0f) v /= l;
        }
        for (auto& [k, e] : n.edge) {
            const float l = glm::length(e);
            if (l > 0.0f) e /= l;
        }
        n.smooth.assign(P.size(), 1);
        for (size_t v = 0; v < P.size(); ++v)
            for (uint32_t k = n.vertStart[v]; k < n.vertStart[v + 1]; ++k)
                if (glm::dot(n.face[n.vertTris[k]], n.vertex[v]) < 0.5f) n.smooth[v] = 0;
    }
    // The distance from `p` to operand s at its closest point `hit`, signed (positive outside), and its gradient. Inside
    // a face: the distance itself, along the face normal. At an edge or a vertex of the operand: the offset-planes
    // distance (the largest of the incident faces' plane distances: the offset surface with sharp corners), along that
    // face's normal, or along the pseudonormal (the faces' mean) when they tie: the fillet then pushes the faces out and
    // lets them meet sharply, rounding the seam's crease but not the operand's own edges (the true distance field would
    // round those too, with a radius of r / 4 that no triangle of the band can show: a chamfer, worse the finer the band).
    const float eps = 1e-5f * r;
    auto distanceAt = [&](int s, const vec3& p, const SurfaceHit& hit, float& u, vec3& g, vec3& np) {
        const auto& P = bvh[s].positions();
        const auto& T = bvh[s].triangles();
        const glm::uvec3 tri = T[hit.prim];
        const Normals& n = normals[s];
        constexpr float kOn = 1e-3f;
        int zero = 0, which = -1;
        for (int i = 0; i < 3; ++i)
            if (hit.bary[i] <= kOn) {
                ++zero;
                which = i;
            }
        const uint32_t* faces = nullptr;
        uint32_t count = 0;
        std::array<uint32_t, 2> pair{};
        vec3 pseudo = n.face[hit.prim];
        if (zero == 1) { // on the edge opposite corner `which`
            const uint64_t k = edgeKey(tri[(which + 1) % 3], tri[(which + 2) % 3]);
            const auto et = n.edgeTris.find(k);
            if (et != n.edgeTris.end() && et->second[1] != ~0u) {
                pair = et->second;
                faces = pair.data();
                count = 2;
                pseudo = n.edge.at(k);
            }
        } else if (zero >= 2) { // at a corner (an interior one: on the piece's rim the fan is open and the rule below is void)
            int c = 0;
            for (int i = 0; i < 3; ++i)
                if (hit.bary[i] > kOn) c = i;
            const uint32_t v = tri[c];
            if (!n.open[v]) {
                faces = n.vertTris.data() + n.vertStart[v];
                count = n.vertStart[v + 1] - n.vertStart[v];
                pseudo = n.vertex[v];
            }
        }
        // The surface's normal at the closest point, what the two surfaces' alignment (the vertex's radius) is read
        // from: the vertices' pseudonormals blended by the barycentrics where the surface is smooth there (a faceted
        // sphere's face normals swing a few degrees from facet to facet, and the radii read off them put neighbouring
        // band vertices on iso-surfaces a few percent apart: a serrated rim, artist-qa 11-24); the face's own normal
        // where a vertex sits on a sharp edge (the pseudonormal there is the edge's bisector, which no face has).
        np = vec3(0.0f);
        for (int i = 0; i < 3; ++i) np += hit.bary[i] * (n.smooth[tri[i]] ? n.vertex[tri[i]] : n.face[hit.prim]);
        {
            const float l = glm::length(np);
            np = l > 0.0f ? np / l : n.face[hit.prim];
        }
        if (count < 2) { // inside a face
            const vec3 fn = n.face[hit.prim];
            if (hit.distance <= eps) {
                u = 0.0f;
                g = fn;
            } else {
                const vec3 away = (p - hit.position) / hit.distance;
                const float side = glm::dot(away, fn) < 0.0f ? -1.0f : 1.0f;
                u = side * hit.distance;
                g = side * away;
            }
            return;
        }
        // The offset-planes rule holds for a point in front of every incident face (inside the feature's wedge): there
        // the largest plane distance is the offset surface's. Anywhere else (behind one of the planes, as beside an
        // edge) it is the plain distance along the way to the closest point.
        float best = -kFar, second = -kFar, least = kFar;
        vec3 bestN(0.0f);
        for (uint32_t k = 0; k < count; ++k) {
            const uint32_t t = faces[k];
            const vec3 fn = n.face[t];
            if (!(glm::dot(fn, fn) > 0.0f)) continue;
            const float d = glm::dot(p - P[T[t].x], fn);
            least = std::min(least, d);
            if (d > best) {
                second = best;
                best = d;
                bestN = fn;
            } else if (d > second) {
                second = d;
            }
        }
        if (best == -kFar || least < -eps) {
            if (hit.distance <= eps) {
                u = 0.0f;
                g = pseudo;
            } else {
                const vec3 away = (p - hit.position) / hit.distance;
                const float side = glm::dot(away, pseudo) < 0.0f ? -1.0f : 1.0f;
                u = side * hit.distance;
                g = side * away;
            }
            return;
        }
        u = best;
        g = best - second < 1e-3f * r ? pseudo : bestN;
    };

    const float target = 2.0f * r / float(std::max(o.segments, 1)); // (the band's edge length)
    // 1b. The cut's slivers: the seam has a point wherever an edge of either mesh crosses the other (as dense as the
    // denser mesh, and two points a hair apart where a grid line and a diagonal cross the other mesh near the same
    // spot), and the cut triangles fan out from the coarser mesh's vertices to all of them. Those slivers tilt at
    // random once the band moves (their facing is noise), and the vertex normals built from them put a dotted ring
    // along a fillet's rim (artist-qa 11-24). The seam is resampled first: a seam edge shorter than a third of the
    // band's edge collapses into one of its ends, as does a vertex a twentieth of it from a seam point; one collapse
    // per vertex per pass (else a run of short edges would collapse into one point), and only where the result stays
    // a manifold (the two ends share exactly the two corners across the edge). Welded results only: an unwelded
    // seam's twin copies would have to collapse alike.
    if (o.relax) {
        const float seamTol = 1e-6f * glm::length(seamBox.max - seamBox.min);
        std::vector<uint8_t> onSeam(m.vertexCount(), 0);
        poolFor(m.vertexCount(), [&](size_t b0, size_t e0) {
            SurfaceHit hit;
            for (size_t v = b0; v < e0; ++v) onSeam[v] = seamBvh.closestPoint(m.positions[v], seamTol, hit) ? 1 : 0;
        }, 4096);
        const float shortSeam = target / 3.0f, nearSeam = target / 20.0f;
        // A seam vertex where the seam turns (over 30 degrees between its two seam edges), or where three or more
        // meet, stays: it is the seam's shape (a box's corner, a junction), not a sample of it.
        std::vector<uint8_t> corner(m.vertexCount(), 0);
        {
            std::vector<std::array<uint32_t, 2>> along(m.vertexCount(), std::array<uint32_t, 2>{~0u, ~0u});
            std::vector<uint8_t> many(m.vertexCount(), 0);
            for (const glm::uvec3& tri : m.triangles)
                for (int i = 0; i < 3; ++i) {
                    const uint32_t a = tri[i], b = tri[(i + 1) % 3];
                    if (!onSeam[a] || !onSeam[b]) continue;
                    std::array<uint32_t, 2>& n = along[a];
                    if (n[0] == b || n[1] == b) continue;
                    if (n[0] == ~0u) n[0] = b;
                    else if (n[1] == ~0u) n[1] = b;
                    else many[a] = 1;
                }
            for (size_t v = 0; v < m.vertexCount(); ++v) {
                if (!onSeam[v]) continue;
                const std::array<uint32_t, 2>& n = along[v];
                if (many[v] || n[0] == ~0u || n[1] == ~0u) {
                    corner[v] = 1;
                    continue;
                }
                const vec3 d0 = m.positions[n[0]] - m.positions[v], d1 = m.positions[n[1]] - m.positions[v];
                const float l0 = glm::length(d0), l1 = glm::length(d1);
                if (!(l0 > 0.0f && l1 > 0.0f) || glm::dot(d0, d1) / (l0 * l1) > -0.866f) corner[v] = 1; // (a turn over 30 degrees)
            }
        }
        std::vector<uint32_t> vertStart(m.vertexCount() + 1, 0), vertTris;
        for (const glm::uvec3& tri : m.triangles)
            for (int i = 0; i < 3; ++i) ++vertStart[tri[i] + 1];
        for (size_t v = 0; v < m.vertexCount(); ++v) vertStart[v + 1] += vertStart[v];
        vertTris.resize(vertStart.back());
        {
            std::vector<uint32_t> fill(vertStart.begin(), vertStart.end() - 1);
            for (size_t t = 0; t < m.triangleCount(); ++t)
                for (int i = 0; i < 3; ++i) vertTris[fill[m.triangles[t][i]]++] = uint32_t(t);
        }
        std::unordered_map<uint32_t, std::vector<uint32_t>> grown; // a survivor's triangles, once it inherited some
        auto trisOf = [&](uint32_t v, std::vector<uint32_t>& out) {
            out.clear();
            if (const auto it = grown.find(v); it != grown.end()) out = it->second;
            else out.assign(vertTris.begin() + vertStart[v], vertTris.begin() + vertStart[v + 1]);
        };
        std::vector<uint8_t> dead(m.triangleCount(), 0), gone(m.vertexCount(), 0);
        std::vector<vec3> facing0(m.triangleCount()); // (as the cut left it: a collapse must keep every triangle facing so)
        for (size_t t = 0; t < m.triangleCount(); ++t) {
            const glm::uvec3 tri = m.triangles[t];
            facing0[t] = glm::cross(m.positions[tri.y] - m.positions[tri.x], m.positions[tri.z] - m.positions[tri.x]);
        }
        size_t collapsed = 0;
        for (int pass = 0; pass < 6; ++pass) {
            struct Short {
                float length;
                uint32_t u, v; // u goes, v stays
            };
            std::vector<Short> shorts;
            for (size_t t = 0; t < m.triangleCount(); ++t) {
                if (dead[t]) continue;
                const glm::uvec3 tri = m.triangles[t];
                for (int i = 0; i < 3; ++i) {
                    const uint32_t a = tri[i], b = tri[(i + 1) % 3];
                    if (!onSeam[a] && !onSeam[b]) continue;
                    if (a > b && onSeam[a] == onSeam[b]) continue; // (each seam-seam edge once; a seam-other edge once by its direction below)
                    const float l = glm::length(m.positions[a] - m.positions[b]);
                    if (onSeam[a] && onSeam[b]) {
                        if (l < shortSeam && !(corner[a] && corner[b])) shorts.push_back({l, corner[a] ? b : a, corner[a] ? a : b});
                    } else if (onSeam[b]) {
                        if (l < nearSeam) shorts.push_back({l, a, b});
                    }
                }
            }
            if (shorts.empty()) break;
            // (ties by vertex id: a grid's seam edges are all one length, and float noise differs between machines)
            std::sort(shorts.begin(), shorts.end(), [](const Short& x, const Short& y) {
                return x.length != y.length ? x.length < y.length : x.v != y.v ? x.v < y.v : x.u < y.u;
            });
            std::vector<uint8_t> touched(m.vertexCount(), 0);
            std::vector<uint32_t> tu, tv;
            size_t before = collapsed;
            // A collapse may turn none of the going vertex's other triangles over (a sliver whose far corner would
            // cross its base): then the other way round for a seam-seam edge, or not at all.
            auto flips = [&](uint32_t u, uint32_t v, const std::vector<uint32_t>& tris) {
                for (uint32_t t : tris) {
                    if (dead[t]) continue;
                    glm::uvec3 tri = m.triangles[t];
                    bool hasV = false;
                    for (int i = 0; i < 3; ++i) hasV |= tri[i] == v;
                    if (hasV) continue;
                    const vec3 before = glm::cross(m.positions[tri.y] - m.positions[tri.x], m.positions[tri.z] - m.positions[tri.x]);
                    for (int i = 0; i < 3; ++i)
                        if (tri[i] == u) tri[i] = v;
                    const vec3 after = glm::cross(m.positions[tri.y] - m.positions[tri.x], m.positions[tri.z] - m.positions[tri.x]);
                    // (against the cut's own facing too: a triangle an earlier collapse left near-degenerate has noise
                    // for a normal, and the next collapse could turn it over against the mesh around it)
                    if (!(glm::dot(before, after) > 0.0f) || !(glm::dot(facing0[t], after) > 0.0f)) return true;
                }
                return false;
            };
            for (Short e : shorts) {
                if (gone[e.u] || gone[e.v] || touched[e.u] || touched[e.v]) continue;
                trisOf(e.u, tu);
                trisOf(e.v, tv);
                if (flips(e.u, e.v, tu)) {
                    if (!onSeam[e.u] || corner[e.u] || flips(e.v, e.u, tv)) continue;
                    std::swap(e.u, e.v);
                    std::swap(tu, tv);
                }
                // The link condition: the two ends' other neighbours in common are exactly the corners across the edge.
                std::vector<uint32_t> nu, nv, apex;
                for (uint32_t t : tu) {
                    if (dead[t]) continue;
                    const glm::uvec3 tri = m.triangles[t];
                    bool hasV = false;
                    for (int i = 0; i < 3; ++i) hasV |= tri[i] == e.v;
                    for (int i = 0; i < 3; ++i) {
                        if (tri[i] == e.u || tri[i] == e.v) continue;
                        nu.push_back(tri[i]);
                        if (hasV) apex.push_back(tri[i]);
                    }
                }
                for (uint32_t t : tv) {
                    if (dead[t]) continue;
                    const glm::uvec3 tri = m.triangles[t];
                    for (int i = 0; i < 3; ++i)
                        if (tri[i] != e.u && tri[i] != e.v) nv.push_back(tri[i]);
                }
                std::sort(nu.begin(), nu.end());
                nu.erase(std::unique(nu.begin(), nu.end()), nu.end());
                std::sort(nv.begin(), nv.end());
                nv.erase(std::unique(nv.begin(), nv.end()), nv.end());
                std::sort(apex.begin(), apex.end());
                apex.erase(std::unique(apex.begin(), apex.end()), apex.end());
                std::vector<uint32_t> common;
                std::set_intersection(nu.begin(), nu.end(), nv.begin(), nv.end(), std::back_inserter(common));
                if (apex.size() != 2 || common != apex) continue;
                // Collapse: u's triangles go to v; those on the edge die.
                std::vector<uint32_t>& mine = grown[e.v];
                if (mine.empty()) mine = tv;
                for (uint32_t t : tu) {
                    if (dead[t]) continue;
                    glm::uvec3& tri = m.triangles[t];
                    bool hasV = false;
                    for (int i = 0; i < 3; ++i) hasV |= tri[i] == e.v;
                    if (hasV) {
                        dead[t] = 1;
                        continue;
                    }
                    for (int i = 0; i < 3; ++i)
                        if (tri[i] == e.u) tri[i] = e.v;
                    mine.push_back(t);
                }
                gone[e.u] = 1;
                touched[e.u] = touched[e.v] = 1;
                ++collapsed;
            }
            if (collapsed == before) break;
        }
        if (collapsed > 0) {
            // Compact: dead triangles out (with their corners' UVs, groups and side), gone vertices out (remapped).
            const bool colors = m.hasColors();
            const size_t oldNv = m.vertexCount();
            std::vector<uint32_t> remap(oldNv, ~0u);
            uint32_t next = 0;
            for (size_t v = 0; v < oldNv; ++v)
                if (!gone[v]) remap[v] = next++;
            auto compactVerts = [&](auto& arr) {
                if (arr.size() != oldNv) return;
                size_t w = 0;
                for (size_t v = 0; v < oldNv; ++v)
                    if (!gone[v]) arr[w++] = arr[v];
                arr.resize(w);
            };
            compactVerts(m.positions);
            compactVerts(m.normals);
            compactVerts(m.mask);
            if (colors) compactVerts(m.colors);
            const size_t oldNt = m.triangleCount();
            auto compactTris = [&](auto& arr, size_t per) {
                if (arr.size() != oldNt * per) return;
                size_t w = 0;
                for (size_t t = 0; t < oldNt; ++t) {
                    if (dead[t]) continue;
                    for (size_t i = 0; i < per; ++i) arr[w * per + i] = arr[t * per + i];
                    ++w;
                }
                arr.resize(w * per);
            };
            for (glm::uvec3& tri : m.triangles) tri = {remap[tri.x], remap[tri.y], remap[tri.z]}; // (dead ones carry ~0u: dropped next)
            compactTris(m.uvs, 3);
            compactTris(m.uvFlags, 3);
            compactTris(m.creases, 3);
            compactTris(m.facegroups, 1);
            compactTris(m.hidden, 1);
            compactTris(side, 1);
            compactTris(m.triangles, 1);
        }
        if (stats) stats->collapsed = collapsed;
    }

    // 2. Refinement: a triangle with a vertex within r + its longest edge of the other operand may carry part of the
    // fillet, so its edges longer than the target are split (conformally: the neighbour across a split edge splits too).
    const size_t before = m.triangleCount();
    std::vector<float> dist[2];   // per vertex: distance to each operand, kFar when beyond `queried`
    std::vector<float> queried;   // the reach it was looked up with
    std::vector<float> need;
    int passes = 0;
    for (; passes < 10; ++passes) {
        const size_t nv = m.vertexCount();
        dist[0].resize(nv, kFar);
        dist[1].resize(nv, kFar);
        queried.resize(nv, 0.0f);
        need.assign(nv, 0.0f);
        std::vector<float> longest(m.triangleCount());
        for (size_t t = 0; t < m.triangleCount(); ++t) {
            const glm::uvec3 tri = m.triangles[t];
            const vec3 &p0 = m.positions[tri.x], &p1 = m.positions[tri.y], &p2 = m.positions[tri.z];
            const float l = std::sqrt(std::max({glm::dot(p1 - p0, p1 - p0), glm::dot(p2 - p1, p2 - p1), glm::dot(p0 - p2, p0 - p2)}));
            longest[t] = l;
            if (l <= target) continue; // (nothing to split here: its vertices' distances don't matter)
            const float want = r + l;
            for (uint32_t v : {tri.x, tri.y, tri.z}) need[v] = std::max(need[v], want);
        }
        poolFor(nv, [&](size_t b0, size_t e0) {
            SurfaceHit hit;
            for (size_t v = b0; v < e0; ++v) {
                if (need[v] <= queried[v]) continue;
                for (int s = 0; s < 2; ++s)
                    dist[s][v] = bvh[s].closestPoint(m.positions[v], need[v], hit) ? hit.distance : kFar;
                queried[v] = need[v];
            }
        }, 2048);
        std::unordered_set<uint64_t> marked;
        for (size_t t = 0; t < m.triangleCount(); ++t) {
            const float l = longest[t];
            if (l <= target) continue;
            const glm::uvec3 tri = m.triangles[t];
            const int other = 1 - side[t];
            const float limit = r + l;
            if (!(dist[other][tri.x] < limit || dist[other][tri.y] < limit || dist[other][tri.z] < limit)) continue;
            const uint32_t v[3] = {tri.x, tri.y, tri.z};
            for (int i = 0; i < 3; ++i) {
                const vec3 d = m.positions[v[(i + 1) % 3]] - m.positions[v[i]];
                if (glm::dot(d, d) > target * target) marked.insert(edgeKey(v[i], v[(i + 1) % 3]));
            }
        }
        if (marked.empty() || m.triangleCount() + 3 * marked.size() > o.maxTriangles) break;
        splitMarked(m, side, marked);
    }

    // 3. Projection: every vertex within r of both operands sits where the fillet runs; Newton steps take it onto
    // F = smin(uA, uB, r) = 0 along F's gradient. At a surface the gradient of its distance is the face normal, pointing
    // away from the operand into the region the fillet lies in (outside both for a Union; inside the kept part of the
    // result for Subtract and Intersect: inside a, and outside b for Subtract, inside b for Intersect).
    // (the operands' triangles face out of each operand: the fillet lies outside both for a Union, inside a and outside
    // b for a Subtract, inside both for an Intersect)
    const float signOf[2] = {op == BooleanOp::Union ? 1.0f : -1.0f, op == BooleanOp::Intersect ? -1.0f : 1.0f};
    const size_t nv = m.vertexCount();
    std::vector<uint8_t> candidate(nv, 0);
    poolFor(nv, [&](size_t b0, size_t e0) {
        SurfaceHit hit;
        for (size_t v = b0; v < e0; ++v)
            candidate[v] = bvh[0].closestPoint(m.positions[v], r, hit) && bvh[1].closestPoint(m.positions[v], r, hit);
    }, 2048);
    std::vector<uint32_t> todo;
    for (uint32_t v = 0; v < nv; ++v)
        if (candidate[v]) todo.push_back(v);
    const float done = 1e-4f * r;
    // The distances to the two surfaces at `p`, signed (positive on the fillet's side), and their gradients.
    // (`nrm`, when given: the surfaces' normals at the closest points, signed the same way)
    auto distances = [&](const vec3& p, float u[2], vec3 g[2], SurfaceHit& hit, vec3* nrm = nullptr) {
        for (int s = 0; s < 2; ++s) {
            if (!bvh[s].closestPoint(p, 2.0f * r, hit)) {
                u[s] = 2.0f * r;
                g[s] = vec3(0.0f);
                if (nrm) nrm[s] = vec3(0.0f);
                continue;
            }
            float d;
            vec3 gd, np;
            distanceAt(s, p, hit, d, gd, np);
            u[s] = signOf[s] * d;
            g[s] = signOf[s] * gd;
            if (nrm) nrm[s] = signOf[s] * np;
        }
    };
    // Newton steps from `p` onto F = smin(uA, uB, k) = 0 (k: this vertex's radius); the result never ends farther than r
    // from `p0`, where the vertex started. A distance is negative once a step (or the relaxation) has carried the point
    // across a surface, so the next step brings it back instead of pushing it on.
    auto project = [&](vec3 p, const vec3& p0, float k, SurfaceHit& hit) {
        if (!(k > eps)) return p0; // (no radius here, a flat join: the vertex stays put, whatever the relaxation offers)
        for (int it = 0; it < 6; ++it) {
            float u[2];
            vec3 g[2];
            distances(p, u, g, hit);
            const float F = float(smoothMin(u[0], u[1], k));
            if (std::abs(F) < done) break;
            const float diff = std::abs(u[0] - u[1]);
            const float h = diff < k ? (k - diff) / k : 0.0f;
            const float wa = u[0] < u[1] ? 1.0f - 0.5f * h : u[0] > u[1] ? 0.5f * h : 0.5f;
            const vec3 grad = wa * g[0] + (1.0f - wa) * g[1];
            const float g2 = glm::dot(grad, grad);
            if (g2 < 1e-6f) break;
            vec3 step = grad * (-F / g2);
            const float sl = glm::length(step);
            if (sl > 0.5f * r) step *= 0.5f * r / sl;
            p += step;
            const vec3 d = p - p0;
            const float dl = glm::length(d);
            if (dl > r) p = p0 + d * (r / dl);
        }
        return p;
    };
    std::vector<vec3> start(todo.size());
    for (size_t i = 0; i < todo.size(); ++i) start[i] = m.positions[todo[i]];
    // Each vertex's radius: r where the surfaces cross at a right angle, fading to nothing where they run parallel
    // (boxes standing on one floor, a sphere resting on a slab): a flat join has no crease to round and stays flat. Read
    // from the surfaces' normals at the closest points, not from the directions to them: beside the rim of a coplanar
    // piece the direction is in the plane, the normals are the same.
    std::vector<float> kOf(todo.size());
    poolFor(todo.size(), [&](size_t b0, size_t e0) {
        SurfaceHit hit;
        for (size_t i = b0; i < e0; ++i) {
            float u[2];
            vec3 g[2], n[2];
            distances(start[i], u, g, hit, n);
            const float align = glm::clamp(glm::dot(n[0], n[1]), 0.0f, 1.0f);
            kOf[i] = r * (1.0f - align);
        }
    }, 256);
    // Evened out over the band (each radius halfway to the mean of its moved neighbours, three rounds): what the
    // closest points' normals still carry of the operands' facets, or of the cut's slivers, must not make a fillet
    // whose depth changes from vertex to vertex. (Not on an unwelded seam: the two copies of a seam vertex have
    // different neighbours, and must land on the same surface.)
    auto faceNormal = [&](const glm::uvec3& tri) {
        return glm::cross(m.positions[tri.y] - m.positions[tri.x], m.positions[tri.z] - m.positions[tri.x]);
    };
    std::vector<vec3> facing(m.triangleCount());
    for (size_t t = 0; t < m.triangleCount(); ++t) facing[t] = faceNormal(m.triangles[t]);
    poolFor(todo.size(), [&](size_t b0, size_t e0) {
        SurfaceHit hit;
        for (size_t i = b0; i < e0; ++i) m.positions[todo[i]] = project(start[i], start[i], kOf[i], hit);
    }, 256);
    // 4. Relaxation: the cut leaves slivers along the seam and the projection stretches them; each moved vertex slides
    // halfway to the mean of its neighbours and is put back onto the fillet, sixteen times (at a seam's corner the
    // moves of neighbouring vertices converge, and the rounds are what untangles the triangles there). Tangential only
    // in effect (the projection undoes the rest), so the surface stays where the fillet runs and the triangles even
    // out. The mean weighs each neighbour by the area of the two triangles on its edge: a vertex of a coarse face next
    // to a dense cutter has a fan of slivers to the seam's many points, and their plain mean dragged it onto the seam
    // (the band crushed to a line, and every sliver turned over: artist-qa 11-24). The slide is capped at a part of
    // the vertex's median edge (at the band's edge, where the refined triangles meet the coarse ones, a vertex pulled
    // towards its far neighbours would land across its near ones; the median, not the shortest: a sliver's one tiny
    // edge may not freeze the vertex that would mend it). A slide that still turns a triangle over (an uneven star at
    // the band's edge) is undone by the fold pass below.
    if (o.relax) {
        std::vector<uint32_t> slot(nv, ~0u);
        for (size_t i = 0; i < todo.size(); ++i) slot[todo[i]] = uint32_t(i);
        std::vector<vec3> sum(todo.size());
        std::vector<float> weight(todo.size());
        std::vector<uint32_t> count(todo.size());
        std::vector<vec3> next(todo.size());
        std::vector<uint32_t> offset(todo.size() + 1);
        std::vector<float> lengths;
        for (int round = 0; round < 16; ++round) {
            std::fill(sum.begin(), sum.end(), vec3(0.0f));
            std::fill(weight.begin(), weight.end(), 0.0f);
            std::fill(count.begin(), count.end(), 0u);
            for (const glm::uvec3& tri : m.triangles) {
                if (slot[tri.x] == ~0u && slot[tri.y] == ~0u && slot[tri.z] == ~0u) continue;
                const vec3 &p0 = m.positions[tri.x], &p1 = m.positions[tri.y], &p2 = m.positions[tri.z];
                const float area2 = glm::length(glm::cross(p1 - p0, p2 - p0));
                for (int i = 0; i < 3; ++i) {
                    const uint32_t v = tri[i];
                    if (slot[v] == ~0u) continue;
                    const vec3 &q1 = m.positions[tri[(i + 1) % 3]], &q2 = m.positions[tri[(i + 2) % 3]];
                    sum[slot[v]] += (q1 + q2) * area2; // (each of the two edges gets this triangle's area)
                    weight[slot[v]] += 2.0f * area2;
                    count[slot[v]] += 2;
                }
            }
            offset[0] = 0;
            for (size_t i = 0; i < todo.size(); ++i) offset[i + 1] = offset[i] + count[i];
            lengths.assign(offset.back(), 0.0f);
            {
                std::vector<uint32_t> fill(offset.begin(), offset.end() - 1);
                for (const glm::uvec3& tri : m.triangles)
                    for (int i = 0; i < 3; ++i) {
                        const uint32_t v = tri[i];
                        if (slot[v] == ~0u) continue;
                        lengths[fill[slot[v]]++] = glm::length(m.positions[tri[(i + 1) % 3]] - m.positions[v]);
                        lengths[fill[slot[v]]++] = glm::length(m.positions[tri[(i + 2) % 3]] - m.positions[v]);
                    }
            }
            poolFor(todo.size(), [&](size_t b0, size_t e0) {
                SurfaceHit hit;
                for (size_t i = b0; i < e0; ++i) {
                    const vec3 p = m.positions[todo[i]];
                    if (count[i] == 0 || !(weight[i] > 0.0f)) {
                        next[i] = p;
                        continue;
                    }
                    float* l = lengths.data() + offset[i];
                    std::nth_element(l, l + count[i] / 2, l + count[i]);
                    const float limit = 0.3f * l[count[i] / 2];
                    vec3 slide = (sum[i] / weight[i] - p) * 0.5f;
                    const float sl = glm::length(slide);
                    if (sl > limit) slide *= limit / std::max(sl, 1e-30f);
                    next[i] = project(p + slide, start[i], kOf[i], hit);
                }
            }, 256);
            for (size_t i = 0; i < todo.size(); ++i) m.positions[todo[i]] = next[i];
        }
    }
    // 5. No folds: a sliver of the cut whose vertex moved more than the sliver is high turns over, and a relaxation
    // slide can cross an edge of an uneven star. Any triangle facing against the way it faced before the projection
    // has a vertex pulled: four rounds towards its neighbours' mean, then halfway back to where it started and onto
    // the fillet again, and from the eighth round all its vertices back to where they started, until none is left
    // (at the start it faced as it did, so this ends). Triangles too small to see at the fillet's scale are
    // left alone: their facing is noise, and pinning a seam vertex for one would crease the fillet.
    if (o.relax) { // (not on an unwelded seam: it could pull one copy of a seam vertex back and not its twin, and open a crack)
        const float tiny = 2e-3f * r * r; // twice an area (as cross products come): a sliver 0.1 r long and 0.02 r wide
        std::vector<uint32_t> slot(nv, ~0u);
        for (size_t i = 0; i < todo.size(); ++i) slot[todo[i]] = uint32_t(i);
        // What each band triangle must keep facing: its own start facing, or, when that was too small to mean anything,
        // the sum of its edge neighbours' (as the cut left them: a fixed reference, so a pinned vertex cannot make its
        // neighbours fail in turn).
        std::vector<vec3> reference(m.triangleCount(), vec3(0.0f));
        {
            std::unordered_map<uint64_t, std::array<uint32_t, 2>> edgeTris;
            for (size_t t = 0; t < m.triangleCount(); ++t) {
                const glm::uvec3 tri = m.triangles[t];
                if (slot[tri.x] == ~0u && slot[tri.y] == ~0u && slot[tri.z] == ~0u) continue;
                reference[t] = facing[t];
                for (int i = 0; i < 3; ++i) {
                    auto [it, fresh] = edgeTris.emplace(edgeKey(tri[i], tri[(i + 1) % 3]), std::array<uint32_t, 2>{uint32_t(t), ~0u});
                    if (!fresh) it->second[1] = uint32_t(t);
                }
            }
            for (size_t t = 0; t < m.triangleCount(); ++t) {
                if (glm::length(facing[t]) >= tiny) continue;
                const glm::uvec3 tri = m.triangles[t];
                if (slot[tri.x] == ~0u && slot[tri.y] == ~0u && slot[tri.z] == ~0u) continue;
                vec3 sum(0.0f);
                for (int i = 0; i < 3; ++i) {
                    const auto it = edgeTris.find(edgeKey(tri[i], tri[(i + 1) % 3]));
                    if (it == edgeTris.end()) continue;
                    const uint32_t o = it->second[0] == t ? it->second[1] : it->second[0];
                    if (o != ~0u && glm::length(facing[o]) >= tiny) sum += facing[o];
                }
                reference[t] = sum; // (all neighbours tiny too: a zero reference, and the triangle is never a fold)
            }
        }
        std::vector<uint8_t> pinned(todo.size(), 0);
        std::vector<uint8_t> again(m.triangleCount(), 0); // (folded in the round before: then all its vertices)
        // The last rounds pin every vertex of a folded triangle where it started and look again, until none is left:
        // each such round pins vertices that were not pinned, so this ends, and a triangle with all its vertices at
        // their start faces as it did (a round that ends early never leaves a neighbour's fold behind).
        for (int round = 0; round < 64; ++round) {
            std::vector<uint8_t> pull(todo.size(), 0);
            std::vector<uint8_t> now(m.triangleCount(), 0);
            size_t folded = 0;
            for (size_t t = 0; t < m.triangleCount(); ++t) {
                const glm::uvec3 tri = m.triangles[t];
                if (slot[tri.x] == ~0u && slot[tri.y] == ~0u && slot[tri.z] == ~0u) continue;
                const vec3 n = faceNormal(tri);
                if (glm::length(n) < tiny) continue; // (nothing to see)
                // (a triangle too small at the start to have a facing of its own, a sliver of the cut, is held against
                // its neighbours' start facing: one that grew while it turned over would otherwise pass; the runner
                // folded four such at a band's edge that the laptop did not, float ties falling differently)
                const bool turned = glm::dot(n, reference[t]) < 0.0f;
                if (!turned) continue;
                ++folded;
                now[t] = 1;
                // (first the vertex that moved farthest: pulling all three left the other two folded against their
                // own neighbours, and the pulls then ran along the seam, one vertex per round: a torn edge; all three
                // once that alone did not mend it, as at the band's edge, where the one that slid across moved least)
                if (again[t] || round >= 7) {
                    for (uint32_t v : {tri.x, tri.y, tri.z})
                        if (slot[v] != ~0u) pull[slot[v]] = 1;
                    continue;
                }
                uint32_t worst = ~0u;
                float far = -1.0f;
                for (uint32_t v : {tri.x, tri.y, tri.z}) {
                    if (slot[v] == ~0u) continue;
                    const float d = glm::length(m.positions[v] - start[slot[v]]);
                    if (d > far) {
                        far = d;
                        worst = v;
                    }
                }
                if (worst != ~0u) pull[slot[worst]] = 1;
            }
            if (folded == 0) break;
            again.swap(now);
            if (round < 4) {
                // First towards the mean of the vertex's neighbours (where they are now: on the fillet, so the vertex
                // stays on it, and a slide that crossed an edge comes back inside its star); only then, when that did
                // not mend it (a seam corner, where the moves converge), back towards where it started.
                std::vector<vec3> sum(todo.size(), vec3(0.0f));
                std::vector<uint32_t> count(todo.size(), 0);
                for (const glm::uvec3& tri : m.triangles)
                    for (int i = 0; i < 3; ++i) {
                        const uint32_t v = tri[i];
                        if (slot[v] == ~0u || !pull[slot[v]]) continue;
                        sum[slot[v]] += m.positions[tri[(i + 1) % 3]] + m.positions[tri[(i + 2) % 3]];
                        count[slot[v]] += 2;
                    }
                for (size_t i = 0; i < todo.size(); ++i)
                    if (pull[i] && count[i] > 0) m.positions[todo[i]] = 0.5f * (m.positions[todo[i]] + sum[i] / float(count[i]));
                continue;
            }
            // (halfway back, then onto the fillet again from there: the fold came from a slide, and the projection
            // alone makes none; the last round leaves the vertex where it started, the one place sure to be folded
            // against nothing)
            size_t fresh = 0;
            for (size_t i = 0; i < todo.size(); ++i)
                if (pull[i] && round >= 7 && !pinned[i]) {
                    pinned[i] = 1;
                    ++fresh;
                }
            poolFor(todo.size(), [&](size_t b0, size_t e0) {
                SurfaceHit h;
                for (size_t i = b0; i < e0; ++i) {
                    if (!pull[i]) continue;
                    if (round < 7) m.positions[todo[i]] = project(start[i] + (m.positions[todo[i]] - start[i]) * 0.5f, start[i], kOf[i], h);
                    else m.positions[todo[i]] = start[i];
                }
            }, 256);
            if (round >= 7 && fresh == 0) break; // (every vertex of what is left is pinned already: nothing more to do)
        }
    }
    std::vector<float> moved(todo.size(), 0.0f);
    for (size_t i = 0; i < todo.size(); ++i) moved[i] = glm::length(m.positions[todo[i]] - start[i]);
    if (stats) {
        stats->refined = m.triangleCount() - before;
        stats->passes = passes;
        stats->moved = 0;
        stats->maxMove = 0.0;
        for (float d : moved) {
            if (d > 0.0f) ++stats->moved;
            stats->maxMove = std::max(stats->maxMove, double(d));
        }
    }
}

} // namespace sf
