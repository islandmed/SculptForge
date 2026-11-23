// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/Scatter.h"

#include "core/Commands.h"
#include "core/Parallel.h"
#include "core/Parts.h"
#include "core/Primitives.h"
#include "core/SurfaceQuery.h"
#include "core/Undo.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <queue>
#include <unordered_map>

namespace sf {

using glm::vec3;

namespace {

constexpr float kPi = 3.14159265358979f;
// Fewer copies than this relax on one thread: starting threads twice a pass costs more than it saves.
constexpr size_t kParallelCopies = 4096;

// Random numbers that are the same on every platform and standard library (a seed gives the same scatter everywhere).
struct Random {
    uint64_t state;
    explicit Random(uint64_t seed) : state(seed * 0x9E3779B97F4A7C15ull + 0x2545F4914F6CDD1Dull) {}
    uint64_t next() {
        uint64_t z = (state += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }
    float unit() { return float(next() >> 40) * (1.0f / 16777216.0f); } // [0, 1)
};

double msSince(std::chrono::steady_clock::time_point t0) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

vec3 safeNormal(const vec3& v, const vec3& fallback) {
    const float l = glm::length(v);
    return l > 1e-12f && std::isfinite(l) ? v / l : fallback;
}

// A unit vector at right angles to n.
vec3 anyTangent(const vec3& n) {
    const vec3 ref = std::abs(n.x) < 0.9f ? vec3(1, 0, 0) : vec3(0, 0, 1);
    return glm::normalize(ref - n * glm::dot(ref, n));
}

glm::mat3 axisAngle(const vec3& axis, float angle) {
    const float c = std::cos(angle), s = std::sin(angle), t = 1.0f - c;
    const vec3& a = axis;
    // Column-major: columns are the images of x, y, z.
    return glm::mat3(t * a.x * a.x + c, t * a.x * a.y + s * a.z, t * a.x * a.z - s * a.y,
                     t * a.x * a.y - s * a.z, t * a.y * a.y + c, t * a.y * a.z + s * a.x,
                     t * a.x * a.z + s * a.y, t * a.y * a.z - s * a.x, t * a.z * a.z + c);
}

// The surface the copies sit on (only the triangles they may sit on) and its smooth normals.
struct Surface {
    TriangleBVH bvh;
    std::vector<vec3> normals; // per vertex of bvh.positions() (empty: face normals)
    std::vector<uint32_t> source; // bvh triangle -> target triangle

    vec3 normalAt(const SurfaceHit& h) const {
        const glm::uvec3 t = bvh.triangles()[h.prim];
        const vec3 face = bvh.faceNormal(h.prim);
        if (normals.empty()) return face;
        return safeNormal(h.bary.x * normals[t.x] + h.bary.y * normals[t.y] + h.bary.z * normals[t.z], face);
    }
};

// A copy while it is placed: the surface point it hangs from, its random draws, and what follows from them.
struct Item {
    vec3 anchor, normal, tangent; // tangent: carried along as the copy slides (its random turn stays put)
    uint32_t prim = 0;
    float scale = 1.0f, turn = 0.0f, tiltAxis = 0.0f, tilt = 0.0f;
    // Derived (derive()):
    glm::mat3 rotation{1.0f};
    vec3 centre{0.0f};
    float radius = 0.0f; // bounding sphere
};

void derive(Item& c, const vec3& half, const ScatterSettings& s) {
    const vec3 up = s.align ? c.normal : vec3(0, 1, 0);
    const vec3 x = s.align ? c.tangent : vec3(1, 0, 0);
    const glm::mat3 frame(x, up, glm::cross(x, up));
    glm::mat3 local = axisAngle(vec3(0, 1, 0), c.turn);
    if (c.tilt != 0.0f) local = local * axisAngle(vec3(std::cos(c.tiltAxis), 0.0f, std::sin(c.tiltAxis)), c.tilt);
    c.rotation = frame * local;
    float h = 0.0f; // the copy's half height along the surface normal
    for (int k = 0; k < 3; ++k) h += std::abs(glm::dot(c.rotation[k], c.normal)) * half[k] * c.scale;
    c.centre = c.anchor + c.normal * (2.0f * s.offset * h);
    c.radius = (s.pieceRadius > 0.0f ? s.pieceRadius : glm::length(half)) * c.scale;
}

// Whether a and b touch (closer than the gap); if so `push` is how far a should move to clear b, half the overlap.
bool contact(const vec3& ca, const glm::mat3& ra, float sa, float rada, const vec3& cb, const glm::mat3& rb, float sb,
             float radb, const vec3& half, const ScatterSettings& s, vec3* push) {
    const float gap = s.spacing * (rada + radb);
    const vec3 d = ca - cb;
    const float dist2 = glm::dot(d, d), reach = rada + radb + gap;
    if (dist2 >= reach * reach) return false; // the spheres are apart
    const float dist = std::sqrt(dist2);
    const vec3 spherePush = (dist > 1e-12f ? d / dist : vec3(0)) * (0.5f * (reach - dist));
    if (s.fit == ScatterSettings::Fit::Round) {
        if (push) *push = spherePush;
        return true;
    }
    // The boxes: separating axis test over the 15 axes (3 + 3 face normals, 9 edge pairs), with the gap added to the
    // overlap on each. Apart on any axis: the boxes, so the pieces, are apart. Else the axis with the least overlap is
    // the box's way out; the shorter of that and the spheres' is the push.
    const vec3 ea = half * sa, eb = half * sb;
    float best = std::numeric_limits<float>::max();
    vec3 bestAxis(0.0f);
    auto test = [&](vec3 axis) {
        const float l = glm::length(axis);
        if (l < 1e-6f) return true; // parallel edges: covered by the face axes
        axis /= l;
        const float pa = ea.x * std::abs(glm::dot(ra[0], axis)) + ea.y * std::abs(glm::dot(ra[1], axis)) + ea.z * std::abs(glm::dot(ra[2], axis));
        const float pb = eb.x * std::abs(glm::dot(rb[0], axis)) + eb.y * std::abs(glm::dot(rb[1], axis)) + eb.z * std::abs(glm::dot(rb[2], axis));
        const float t = glm::dot(d, axis);
        const float overlap = pa + pb + gap - std::abs(t);
        if (overlap <= 0.0f) return false;
        if (overlap < best) best = overlap, bestAxis = t >= 0.0f ? axis : -axis;
        return true;
    };
    for (int k = 0; k < 3; ++k)
        if (!test(ra[k]) || !test(rb[k])) return false;
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            if (!test(glm::cross(ra[i], rb[j]))) return false;
    if (push) {
        const vec3 boxPush = bestAxis * (0.5f * best);
        *push = glm::dot(boxPush, boxPush) <= glm::dot(spherePush, spherePush) ? boxPush : spherePush;
    }
    return true;
}

bool contact(const Item& a, const Item& b, const vec3& half, const ScatterSettings& s, vec3* push) {
    return contact(a.centre, a.rotation, a.scale, a.radius, b.centre, b.rotation, b.scale, b.radius, half, s, push);
}

// Copies sorted by grid cell, for "every copy near this one".
class Grid {
public:
    void build(const std::vector<Item>& items, float cell) {
        cell_ = std::max(cell, 1e-9f);
        lo_ = vec3(std::numeric_limits<float>::max());
        for (const Item& c : items) lo_ = glm::min(lo_, c.centre);
        cells_.resize(items.size());
        for (size_t i = 0; i < items.size(); ++i) cells_[i] = {key(cellOf(items[i].centre)), uint32_t(i)};
        std::sort(cells_.begin(), cells_.end());
    }
    template <class F>
    void near(const vec3& p, F&& fn) const {
        const glm::ivec3 c = cellOf(p);
        for (int dz = -1; dz <= 1; ++dz)
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx) {
                    const glm::ivec3 n = c + glm::ivec3(dx, dy, dz);
                    if (n.x < 0 || n.y < 0 || n.z < 0) continue;
                    const uint64_t k = key(n);
                    auto it = std::lower_bound(cells_.begin(), cells_.end(), std::pair<uint64_t, uint32_t>{k, 0});
                    for (; it != cells_.end() && it->first == k; ++it) fn(it->second);
                }
    }

private:
    glm::ivec3 cellOf(const vec3& p) const {
        const vec3 f = (p - lo_) / cell_ + 1.0f; // one spare cell below, so a neighbour is never negative
        return glm::ivec3(std::clamp(f.x, 0.0f, 2.0e6f), std::clamp(f.y, 0.0f, 2.0e6f), std::clamp(f.z, 0.0f, 2.0e6f));
    }
    static uint64_t key(const glm::ivec3& c) { return (uint64_t(c.x) << 42) | (uint64_t(c.y) << 21) | uint64_t(c.z); }
    float cell_ = 1.0f;
    vec3 lo_{0.0f};
    std::vector<std::pair<uint64_t, uint32_t>> cells_;
};

// A direction in the tangent plane of n that is not tied to the scene's axes, from two copy numbers.
vec3 hashTangent(const vec3& n, uint32_t i, uint32_t j) {
    Random r(uint64_t(i) * 2654435761ull + j);
    const float a = 2.0f * kPi * r.unit();
    const vec3 t = anyTangent(n), b = glm::cross(n, t);
    return std::cos(a) * t + std::sin(a) * b;
}

} // namespace

bool scatterTouching(const ScatterCopy& a, const ScatterCopy& b, const vec3& half, const ScatterSettings& settings) {
    const float l = settings.pieceRadius > 0.0f ? settings.pieceRadius : glm::length(half);
    return contact(a.position, a.rotation, a.scale, l * a.scale, b.position, b.rotation, b.scale, l * b.scale, half,
                   settings, nullptr);
}

std::vector<ScatterCopy> scatterPlace(const Mesh& target, const vec3& half, const ScatterSettings& s,
                                      ScatterStats* stats, std::string* error) {
    const auto t0 = std::chrono::steady_clock::now();
    ScatterStats local;
    ScatterStats& st = stats ? *stats : local;
    st = {};
    st.asked = size_t(std::max(0, s.count));
    auto fail = [&](const char* why) {
        if (error) *error = why;
        return std::vector<ScatterCopy>{};
    };
    if (s.count <= 0) return fail("Scatter: the count must be at least 1");

    // The triangles copies may sit on: shown ones, and with `masked` only those masked (on average) at least half.
    Surface surf;
    std::vector<glm::uvec3> tris;
    std::vector<double> cdf;
    double area = 0.0;
    const bool smooth = target.normals.size() == target.positions.size();
    for (uint32_t t = 0; t < uint32_t(target.triangles.size()); ++t) {
        if (target.triangleHidden(t)) continue;
        const glm::uvec3 f = target.triangles[t];
        if (s.masked && (target.maskAt(f.x) + target.maskAt(f.y) + target.maskAt(f.z)) < 1.5f) continue;
        const double a = 0.5 * glm::length(glm::cross(target.positions[f.y] - target.positions[f.x],
                                                       target.positions[f.z] - target.positions[f.x]));
        if (!(a > 0.0)) continue;
        tris.push_back(f);
        surf.source.push_back(t);
        area += a;
        cdf.push_back(area);
    }
    if (tris.empty())
        return fail(s.masked ? "Scatter: nothing is masked on that Part (mask where the copies go, or scatter over all of it)"
                             : "Scatter: that Part has no visible surface");
    if (smooth) surf.normals = target.normals;
    surf.bvh.build(target.positions, tris);

    Random rng(s.seed);
    const float lo = std::min(s.scaleMin, s.scaleMax), hi = std::max(s.scaleMin, s.scaleMax);
    const auto draw = [&](Item& c) {
        const double pick = double(rng.unit()) * area;
        const uint32_t k = uint32_t(std::min<size_t>(size_t(std::upper_bound(cdf.begin(), cdf.end(), pick) - cdf.begin()), tris.size() - 1));
        float u = rng.unit(), v = rng.unit();
        if (u + v > 1.0f) u = 1.0f - u, v = 1.0f - v;
        SurfaceHit h;
        h.prim = k;
        h.bary = vec3(1.0f - u - v, u, v);
        const glm::uvec3 f = tris[k];
        c.anchor = h.bary.x * target.positions[f.x] + h.bary.y * target.positions[f.y] + h.bary.z * target.positions[f.z];
        c.normal = surf.normalAt(h);
        c.tangent = anyTangent(c.normal);
        c.prim = k;
        c.scale = lo + (hi - lo) * rng.unit();
        c.turn = glm::radians(s.turn) * (2.0f * rng.unit() - 1.0f);
        c.tiltAxis = 2.0f * kPi * rng.unit();
        c.tilt = glm::radians(s.tilt) * rng.unit();
        derive(c, half, s);
    };
    std::vector<Item> items;
    items.reserve(size_t(s.count));
    // The rest of the surface is reached by the copies' largest bounding sphere plus the gap: one grid cell.
    const float rmax = (s.pieceRadius > 0.0f ? s.pieceRadius : glm::length(half)) * hi;
    const float cell = 2.0f * rmax * (1.0f + std::max(0.0f, s.spacing));
    if (!s.keepApart) {
        items.resize(size_t(s.count));
        for (Item& c : items) draw(c);
    } else {
        // Keep apart starts from an even spread: candidates in turn, each kept only when it touches none kept so far
        // (dart throwing), up to several candidates per copy. Copies still missing then come from the candidates
        // turned away, and the relax below makes room for them (or leaves them out). Random copies would start in
        // clumps with gaps between, which pushing apart alone never fills.
        std::unordered_map<uint64_t, std::vector<uint32_t>> cells;
        const vec3 origin = target.bounds().min - vec3(cell);
        const auto cellOf = [&](const vec3& p) {
            const glm::ivec3 c = glm::ivec3(glm::clamp((p - origin) / cell, vec3(0.0f), vec3(2.0e6f)));
            return c;
        };
        const auto key = [](const glm::ivec3& c) { return (uint64_t(c.x) << 42) | (uint64_t(c.y) << 21) | uint64_t(c.z); };
        std::vector<Item> turnedAway;
        const size_t tries = size_t(s.count) * 6;
        for (size_t t = 0; t < tries && items.size() < size_t(s.count); ++t) {
            Item c;
            draw(c);
            const glm::ivec3 at = cellOf(c.centre);
            bool clear = true;
            for (int dz = -1; dz <= 1 && clear; ++dz)
                for (int dy = -1; dy <= 1 && clear; ++dy)
                    for (int dx = -1; dx <= 1 && clear; ++dx) {
                        const glm::ivec3 n = at + glm::ivec3(dx, dy, dz);
                        if (n.x < 0 || n.y < 0 || n.z < 0) continue;
                        auto it = cells.find(key(n));
                        if (it == cells.end()) continue;
                        for (uint32_t j : it->second)
                            if (contact(c, items[j], half, s, nullptr)) {
                                clear = false;
                                break;
                            }
                    }
            if (!clear) {
                if (turnedAway.size() < size_t(s.count)) turnedAway.push_back(c);
                continue;
            }
            cells[key(at)].push_back(uint32_t(items.size()));
            items.push_back(c);
        }
        // A random start that is full keeps about 55-75% of the copies a close packing holds: more than about 70%
        // again of what it kept never fits, so those are left out at once instead of after a long relax.
        const size_t kept = items.size(), room = kept + kept * 7 / 10;
        const size_t want = std::min<size_t>(size_t(s.count), room);
        for (size_t t = 0; items.size() < want && t < turnedAway.size(); ++t) items.push_back(turnedAway[t]);
        while (items.size() < want) { // (only when every candidate was kept or turned away and none are left)
            Item c;
            draw(c);
            items.push_back(c);
        }
        st.dropped = size_t(s.count) - items.size();
    }
    st.placeMs = msSince(t0);

    if (s.keepApart && items.size() > 1) {
        const auto t1 = std::chrono::steady_clock::now();
        const int passes = s.iterations > 0 ? s.iterations : 600;
        std::vector<vec3> moves(items.size());
        std::vector<int> touching(items.size());
        Grid grid;
        size_t bestTouching = std::numeric_limits<size_t>::max();
        int since = 0;
        for (int pass = 0; pass < passes; ++pass) {
            grid.build(items, cell);
            std::atomic<size_t> count{0};
            parallelFor(items.size(), [&](size_t b, size_t e) {
                size_t n = 0;
                for (size_t i = b; i < e; ++i) {
                    const Item& a = items[i];
                    vec3 sum(0.0f);
                    int hits = 0;
                    grid.near(a.centre, [&](uint32_t j) {
                        vec3 push;
                        if (j == i || !contact(a, items[j], half, s, &push)) return;
                        // Slide along the surface: the part of the push that leaves it does nothing once the copy
                        // goes back onto it, so a push mostly along the normal goes sideways instead.
                        const float len = glm::length(push);
                        vec3 slide = push - a.normal * glm::dot(push, a.normal);
                        if (glm::length(slide) < 0.25f * len) {
                            vec3 away = a.centre - items[j].centre;
                            away -= a.normal * glm::dot(away, a.normal);
                            const float al = glm::length(away);
                            slide = (al > 1e-6f * a.radius ? away / al : hashTangent(a.normal, uint32_t(std::min<size_t>(i, j)), uint32_t(std::max<size_t>(i, j))) * (i < j ? 1.0f : -1.0f)) * len;
                        }
                        sum += slide * 1.02f + safeNormal(slide, vec3(0)) * (1e-3f * a.radius);
                        ++hits;
                    });
                    moves[i] = hits ? sum / std::sqrt(float(hits)) : vec3(0.0f);
                    touching[i] = hits;
                    n += hits ? 1 : 0;
                }
                count += n;
            }, kParallelCopies);
            st.passes = pass + 1;
            const size_t now = count.load();
            if (now == 0) break;
            // Stuck (no room left): leave out a few of the most crowded copies, and the relax lets the others spread
            // into the room they leave, so the copies that stay cover the surface evenly instead of leaving holes
            // where a crowd was thinned at the end.
            if (now < bestTouching) bestTouching = now, since = 0;
            else if (++since >= 25) {
                std::vector<uint32_t> crowded;
                for (size_t i = 0; i < items.size(); ++i)
                    if (touching[i]) crowded.push_back(uint32_t(i));
                std::stable_sort(crowded.begin(), crowded.end(), [&](uint32_t a, uint32_t b) { return touching[a] > touching[b]; });
                crowded.resize(std::max<size_t>(1, crowded.size() / 20));
                std::vector<uint8_t> out(items.size(), 0);
                for (uint32_t i : crowded) out[i] = 1;
                size_t w = 0;
                for (size_t i = 0; i < items.size(); ++i)
                    if (!out[i]) items[w] = items[i], touching[w] = touching[i], moves[w] = moves[i], ++w;
                items.resize(w), touching.resize(w), moves.resize(w);
                st.dropped += crowded.size();
                bestTouching = std::numeric_limits<size_t>::max(), since = 0;
                continue;
            }
            parallelFor(items.size(), [&](size_t b, size_t e) {
                for (size_t i = b; i < e; ++i) {
                    if (!touching[i]) continue;
                    Item& c = items[i];
                    const vec3 want = c.anchor + moves[i];
                    SurfaceHit h;
                    if (!surf.bvh.closestPoint(want, std::numeric_limits<float>::max(), h)) continue;
                    c.anchor = h.position;
                    c.prim = h.prim;
                    const vec3 n = surf.normalAt(h);
                    // Carry the tangent onto the new normal, so the copy keeps its turn as it slides.
                    c.tangent = safeNormal(c.tangent - n * glm::dot(c.tangent, n), anyTangent(n));
                    c.normal = n;
                    derive(c, half, s);
                }
            }, kParallelCopies);
        }
        // Whatever still touches has no room: leave out the copies with the most contacts first until none touch.
        grid.build(items, cell);
        std::vector<std::vector<uint32_t>> nbr(items.size());
        parallelFor(items.size(), [&](size_t b, size_t e) {
            for (size_t i = b; i < e; ++i)
                grid.near(items[i].centre, [&](uint32_t j) {
                    if (j != i && contact(items[i], items[j], half, s, nullptr)) nbr[i].push_back(j);
                });
        }, kParallelCopies);
        std::vector<int> degree(items.size());
        std::priority_queue<std::pair<int, uint32_t>> queue;
        for (size_t i = 0; i < items.size(); ++i) {
            degree[i] = int(nbr[i].size());
            if (degree[i] > 0) queue.push({degree[i], uint32_t(i)});
        }
        std::vector<uint8_t> gone(items.size(), 0);
        while (!queue.empty()) {
            const auto [d, i] = queue.top();
            queue.pop();
            if (gone[i] || d != degree[i] || d == 0) continue;
            gone[i] = 1;
            ++st.dropped;
            for (uint32_t j : nbr[i])
                if (!gone[j] && --degree[j] > 0) queue.push({degree[j], j});
        }
        if (st.dropped) {
            size_t w = 0;
            for (size_t i = 0; i < items.size(); ++i)
                if (!gone[i]) items[w++] = items[i];
            items.resize(w);
        }
        st.relaxMs = msSince(t1);
    }

    std::vector<ScatterCopy> out(items.size());
    for (size_t i = 0; i < items.size(); ++i) {
        const Item& c = items[i];
        out[i] = {c.centre, c.anchor, c.normal, c.rotation, c.scale, surf.source[c.prim]};
    }
    st.placed = out.size();
    return out;
}

Mesh scatterPieceMesh(const Mesh& source, bool flat, glm::mat3* axes) {
    glm::vec3 lo(std::numeric_limits<float>::max()), hi(std::numeric_limits<float>::lowest());
    for (const vec3& p : source.positions) lo = glm::min(lo, p), hi = glm::max(hi, p);
    const vec3 centre = 0.5f * (lo + hi), size = hi - lo;
    // Columns: the Part's own directions that become the piece's x, y (up) and z; right-handed.
    glm::mat3 a(1.0f);
    if (flat) {
        const int k = size.x <= size.y && size.x <= size.z ? 0 : (size.z < size.y ? 2 : 1);
        if (k == 0) a = glm::mat3(vec3(0, 1, 0), vec3(1, 0, 0), vec3(0, 0, -1));
        if (k == 2) a = glm::mat3(vec3(1, 0, 0), vec3(0, 0, 1), vec3(0, -1, 0));
    }
    if (axes) *axes = a;
    const glm::mat3 to = glm::transpose(a);
    Mesh m;
    m.positions.reserve(source.positions.size());
    for (const vec3& p : source.positions) m.positions.push_back(to * (p - centre));
    m.triangles = source.triangles;
    if (source.hasUvs()) m.uvs = source.uvs;
    if (source.hasColors()) m.colors = source.colors;
    if (source.hasQuads()) m.quadPartners = source.quadPartners;
    m.rebuildTopology();
    m.recomputeAllNormals();
    return m;
}

Mesh scatterJoin(const Mesh& piece, const std::vector<ScatterCopy>& copies) {
    Mesh m;
    const size_t nv = piece.positions.size(), nt = piece.triangles.size(), n = copies.size();
    m.positions.resize(nv * n);
    m.triangles.resize(nt * n);
    m.facegroups.resize(nt * n);
    if (piece.hasUvs()) m.uvs.resize(piece.uvs.size() * n);
    if (piece.hasColors()) m.colors.resize(nv * n);
    if (piece.hasQuads()) m.quadPartners.resize(nt * n);
    parallelFor(n, [&](size_t b, size_t e) {
        for (size_t c = b; c < e; ++c) {
            const ScatterCopy& k = copies[c];
            const glm::mat3 r = k.rotation * k.scale;
            for (size_t v = 0; v < nv; ++v) m.positions[c * nv + v] = k.position + r * piece.positions[v];
            const uint32_t off = uint32_t(c * nv);
            for (size_t t = 0; t < nt; ++t) {
                m.triangles[c * nt + t] = piece.triangles[t] + glm::uvec3(off);
                m.facegroups[c * nt + t] = uint32_t(c + 1); // (1-based, as new groups elsewhere)
                if (piece.hasQuads()) {
                    const uint32_t q = piece.quadPartners[t];
                    m.quadPartners[c * nt + t] = q == Mesh::kNoQuad ? q : q + uint32_t(c * nt);
                }
            }
            if (piece.hasUvs()) std::copy(piece.uvs.begin(), piece.uvs.end(), m.uvs.begin() + ptrdiff_t(c * piece.uvs.size()));
            if (piece.hasColors()) std::copy(piece.colors.begin(), piece.colors.end(), m.colors.begin() + ptrdiff_t(c * nv));
        }
    }, 256);
    m.rebuildTopology();
    m.recomputeAllNormals();
    m.topologyDirty = true;
    return m;
}

namespace {

// A Part by name or by index in part_list (-1: none).
int partArg(const Document& doc, const std::string& which) {
    if (which.empty()) return -1;
    if (const int i = doc.findPart(which); i >= 0) return i;
    char* end = nullptr;
    const long v = std::strtol(which.c_str(), &end, 10);
    if (end != which.c_str() && *end == 0 && v >= 0 && v < long(doc.parts.size())) return int(v);
    return -1;
}

// The built-in pieces: few triangles each, since there are many copies. Longest side 1.
bool shapePiece(const std::string& shape, Mesh& m) {
    if (shape == "pebble") {
        m = makeIcosphere(2, 0.5f);
        for (vec3& p : m.positions) p *= vec3(1.0f, 0.5f, 0.75f);
    } else if (shape == "disc") { // a scale, a coin, a sequin
        m = makeCylinder(16, 1.0f);
        for (vec3& p : m.positions) p.y *= 0.2f;
    } else if (shape == "hex") { // a plate or a reptile scale: six sides, its top bevelled in
        m = makeCylinder(6, 1.0f);
        for (vec3& p : m.positions) {
            p.y *= 0.25f;
            if (p.y > 0.0f) p.x *= 1.0f - 1.4f * p.y, p.z *= 1.0f - 1.4f * p.y; // (top 0.125: 82% as wide)
        }
    } else if (shape == "sphere") m = makeIcosphere(2, 0.5f);
    else if (shape == "cube") m = makeCube(1, 1.0f);
    else if (shape == "cone") m = makeCone(16, 1.0f);
    else if (shape == "cylinder") m = makeCylinder(16, 1.0f);
    else if (shape == "capsule") m = makeCapsule(12, 1.0f);
    else if (shape == "torus") m = makeTorus(16, 1.0f);
    else return false;
    m.recomputeAllNormals();
    return true;
}

std::string shapeTitle(std::string shape) {
    if (!shape.empty()) shape[0] = char(std::toupper(static_cast<unsigned char>(shape[0])));
    return shape;
}

} // namespace

void registerScatterCommands(CommandRegistry& reg) {
    using T = ParamSpec::Type;
    reg.add({"scatter",
             "Scatter copies of a Part (or a built-in shape) over another Part's surface, or only its masked area, "
             "each at a random size and turn. keep_apart=true relaxes them over the surface until no two overlap "
             "(copies with no room left are left out, and the result says how many). One new Part (or one Part per "
             "copy with separate=true); one undo step.",
             {{"part", T::String, "The Part to scatter (name or index from part_list); or use shape.", false},
              {"shape", T::String, "A built-in piece instead of a Part: pebble, disc, hex, sphere, cube, cone, cylinder, capsule, torus.", false},
              {"size", T::Number, "shape only: its longest side, as a fraction of the surface Part's size (default 0.04).", false},
              {"target", T::String, "The Part to scatter over (name or index; default the active Part).", false},
              {"count", T::Number, "How many copies (1 to 20000, default 200).", false},
              {"scale_min", T::Number, "Smallest random size of a copy (default 1).", false},
              {"scale_max", T::Number, "Largest random size of a copy (default scale_min).", false},
              {"turn", T::Number, "Random turn about the up side, up to this many degrees either way (0 to 180, default 180).", false},
              {"tilt", T::Number, "Random tilt off the surface normal, up to this many degrees (0 to 90, default 0).", false},
              {"align", T::Bool, "Up side along the surface normal (default true); false keeps every copy upright (+Y).", false},
              {"flat", T::Bool, "The piece's shortest side is its up side, so flat pieces lie flat (default false: its own +Y).", false},
              {"offset", T::Number, "Along the normal in copy heights (-1 to 1): 0 centred on the surface (default), 0.5 resting on it.", false},
              {"seed", T::Number, "Random seed (default 1): the same seed gives the same scatter.", false},
              {"masked", T::Bool, "Only the masked area of the surface (default false).", false},
              {"keep_apart", T::Bool, "Relax the copies until none overlap (default false).", false},
              {"spacing", T::Number, "keep_apart: the gap between copies in copy sizes (0 = just touching, the default; up to 2).", false},
              {"fit", T::String, "keep_apart: 'tight' (the copies' boxes, the default) or 'round' (their bounding spheres, looser).", false},
              {"separate", T::Bool, "One Part per copy (at most 500) instead of one joined Part (default false).", false},
              {"name", T::String, "Name for the new Part (default '<piece> Scatter').", false}},
             [](Document& doc, const Args& a) -> CommandResult {
                 const auto t0 = std::chrono::steady_clock::now();
                 const std::string targetName = argString(a, "target");
                 const int target = targetName.empty() ? doc.activePart : partArg(doc, targetName);
                 if (target < 0) return {false, "Scatter: no Part '" + targetName + "' to scatter over"};
                 const std::string shape = argString(a, "shape");
                 const std::string which = argString(a, "part", a.contains("part") && std::holds_alternative<double>(a.at("part"))
                                                                     ? std::to_string(int(argNumber(a, "part", -1)))
                                                                     : std::string());
                 if (shape.empty() == which.empty()) return {false, "Scatter: give the piece as part= (a Part) or shape= (a built-in shape)"};
                 Mesh scratchTarget, scratchSource, own;
                 const Mesh& surface = partPlacedMesh(doc, target, scratchTarget);
                 std::string pieceName;
                 const Mesh* piece = nullptr;
                 if (!which.empty()) {
                     const int source = partArg(doc, which);
                     if (source < 0) return {false, "Scatter: no Part '" + which + "' to scatter"};
                     if (source == target) return {false, "Scatter: pick another Part than the surface itself"};
                     piece = &partPlacedMesh(doc, source, scratchSource);
                     pieceName = doc.parts[size_t(source)].name;
                 } else {
                     if (!shapePiece(shape, own)) return {false, "Scatter: unknown shape '" + shape + "' (pebble, disc, hex, sphere, cube, cone, cylinder, capsule, torus)"};
                     const AABB b = surface.bounds();
                     const float size = float(argNumber(a, "size", 0.04));
                     if (!(size > 0.0f && size <= 2.0f) || !b.valid()) return {false, "Scatter: size must be above 0 and at most 2"};
                     const float k = size * glm::length(b.extent());
                     for (vec3& p : own.positions) p *= k;
                     piece = &own;
                     pieceName = shapeTitle(shape);
                 }
                 if (piece->triangleCount() == 0) return {false, "Scatter: that Part is empty"};

                 ScatterSettings s;
                 const double count = argNumber(a, "count", 200);
                 if (!(count >= 1 && count <= kScatterMaxCopies)) return {false, "Scatter: count must be from 1 to " + std::to_string(kScatterMaxCopies)};
                 s.count = int(std::lround(count));
                 s.scaleMin = float(argNumber(a, "scale_min", 1.0));
                 s.scaleMax = float(argNumber(a, "scale_max", s.scaleMin));
                 if (!(s.scaleMin > 0.0f && s.scaleMax > 0.0f && s.scaleMin <= 100.0f && s.scaleMax <= 100.0f))
                     return {false, "Scatter: scale_min and scale_max must be above 0 (and at most 100)"};
                 s.turn = float(argNumber(a, "turn", 180.0));
                 s.tilt = float(argNumber(a, "tilt", 0.0));
                 if (!(s.turn >= 0.0f && s.turn <= 180.0f && s.tilt >= 0.0f && s.tilt <= 90.0f))
                     return {false, "Scatter: turn must be from 0 to 180 degrees, tilt from 0 to 90"};
                 s.align = argBool(a, "align", true);
                 s.flat = argBool(a, "flat", false);
                 s.offset = float(argNumber(a, "offset", 0.0));
                 if (!(s.offset >= -1.0f && s.offset <= 1.0f)) return {false, "Scatter: offset must be from -1 to 1"};
                 const double seed = argNumber(a, "seed", 1.0);
                 if (!(seed >= 0.0 && seed < 4294967296.0)) return {false, "Scatter: seed must be a whole number from 0"};
                 s.seed = uint32_t(seed);
                 s.masked = argBool(a, "masked", false);
                 s.keepApart = argBool(a, "keep_apart", false);
                 s.spacing = float(argNumber(a, "spacing", 0.0));
                 if (!(s.spacing >= 0.0f && s.spacing <= 2.0f)) return {false, "Scatter: spacing must be from 0 to 2"};
                 const std::string fit = argString(a, "fit", "tight");
                 if (fit != "tight" && fit != "round") return {false, "Scatter: fit must be 'tight' or 'round'"};
                 s.fit = fit == "round" ? ScatterSettings::Fit::Round : ScatterSettings::Fit::Tight;
                 const bool separate = argBool(a, "separate", false);
                 if (separate && s.count > kScatterMaxSeparate)
                     return {false, "Scatter: separate Parts go up to " + std::to_string(kScatterMaxSeparate) + " copies; join them, or fewer copies"};
                 if (size_t(s.count) * piece->triangleCount() > kScatterMaxTriangles)
                     return {false, "Scatter: " + std::to_string(s.count) + " copies would be over 4 million triangles; use a lighter piece or fewer copies"};

                 const Mesh shaped = scatterPieceMesh(*piece, s.flat);
                 const AABB pb = shaped.bounds();
                 const vec3 half = 0.5f * pb.extent();
                 if (!(glm::length(half) > 1e-9f)) return {false, "Scatter: that piece has no size"};
                 for (const vec3& p : shaped.positions) s.pieceRadius = std::max(s.pieceRadius, glm::length(p - pb.center()));
                 ScatterStats stats;
                 std::string error;
                 const std::vector<ScatterCopy> copies = scatterPlace(surface, half, s, &stats, &error);
                 if (copies.empty()) return {false, error.empty() ? "Scatter: no room for any copy" : error};

                 const std::string base = argString(a, "name", pieceName + " Scatter");
                 size_t tris = 0;
                 std::string made;
                 if (!separate) {
                     Mesh joined = scatterJoin(shaped, copies);
                     tris = joined.triangleCount();
                     made = doc.uniquePartName(base);
                     const int at = doc.addPart(std::move(joined), made, false, target + 1);
                     UndoRecord r; // undo takes the new Part out again
                     r.label = "Scatter";
                     r.part = std::make_shared<PartStep>();
                     r.part->id = doc.parts[size_t(at)].id;
                     r.part->at = at;
                     doc.undo.push(std::move(r));
                 } else {
                     UndoBatchScope batch; // every Part's step carries this id: the undo command takes them together
                     for (size_t c = 0; c < copies.size(); ++c) {
                         Mesh one = scatterJoin(shaped, {copies[c]});
                         tris += one.triangleCount();
                         const std::string name = doc.uniquePartName(base + " " + std::to_string(c + 1));
                         if (c == 0) made = name;
                         const int at = doc.addPart(std::move(one), name, false, target + 1 + int(c));
                         UndoRecord r;
                         r.label = "Scatter";
                         r.part = std::make_shared<PartStep>();
                         r.part->id = doc.parts[size_t(at)].id;
                         r.part->at = at;
                         doc.undo.push(std::move(r));
                     }
                 }
                 doc.modified = true;
                 char timing[96];
                 std::snprintf(timing, sizeof timing, "%.0f ms", msSince(t0));
                 std::string out = "Scattered " + std::to_string(copies.size()) + " copies of \"" + pieceName + "\" as \"" + made + "\"" +
                                   (separate ? " (one Part each)" : "") + ": " + std::to_string(tris) + " triangles";
                 if (s.keepApart)
                     out += std::string(", kept apart in ") + std::to_string(stats.passes) + " passes" +
                            (stats.dropped ? "; " + std::to_string(stats.dropped) + " had no room and were left out (fewer copies, smaller ones or less spacing fit them all)" : "");
                 return {true, out + " (" + timing + ")"};
             }});
}

} // namespace sf
