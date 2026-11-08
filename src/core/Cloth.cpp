// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/Cloth.h"

#include "core/Mesh.h"
#include "core/SurfaceQuery.h"
#include "core/ThreadPool.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <cctype>
#include <unordered_map>

namespace sf {

namespace {

// Compliances in the presets are per metre of rest length (a short edge is stiffer, as a short piece of fabric
// is): alpha = compliance * rest.
const Fabric kFabrics[] = {
    //  id         stretch  compress bend   density friction damping thickness
    {"silk",       1e-6f,   2e-3f,   8.0f,  0.06f,  0.2f,    0.010f, 0.0005f},
    {"cotton",     2e-7f,   5e-4f,   1.5f,  0.15f,  0.4f,    0.015f, 0.001f},
    {"denim",      5e-8f,   1e-4f,   0.15f, 0.40f,  0.6f,    0.020f, 0.002f},
    {"leather",    1e-8f,   2e-5f,   0.01f, 0.80f,  0.7f,    0.030f, 0.003f},
};
const char* const kFabricLabels[] = {"Silk", "Cotton", "Denim", "Leather"};
static_assert(std::size(kFabrics) == size_t(FabricPreset::Count));

bool sameName(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) return false;
    return true;
}

constexpr uint32_t kKindShift = 30;
enum Kind : uint32_t { kStretch = 0, kBend = 1, kSeam = 2 };
constexpr int kSeamRampFrames = 30;
constexpr float kOmega = 1.0f; // (Gauss-Seidel over colours: no over-relaxation needed)
constexpr float kDrag = 0.3f;  // air drag, 1/s (wind)

} // namespace

const Fabric& fabricPreset(FabricPreset preset) {
    const size_t i = std::min(size_t(preset), std::size(kFabrics) - 1);
    return kFabrics[i];
}

const char* fabricLabel(FabricPreset preset) {
    const size_t i = std::min(size_t(preset), std::size(kFabricLabels) - 1);
    return kFabricLabels[i];
}

bool fabricFromName(std::string_view name, FabricPreset& out) {
    for (size_t i = 0; i < std::size(kFabrics); ++i)
        if (sameName(name, kFabrics[i].id) || sameName(name, kFabricLabels[i])) {
            out = FabricPreset(i);
            return true;
        }
    return false;
}

// --- Colliders ---

struct ClothColliders::Impl {
    std::vector<glm::vec3> positions;
    std::vector<glm::uvec3> triangles;
    std::vector<glm::vec3> normals; // per vertex, area-weighted (inside / outside near edges and corners)
    TriangleBVH bvh;
    bool built = false;
    // Sharp features: per vertex, the fastest turn of the normal toward a neighbour (radians per metre: 1 / the
    // tightest radius round it).
    std::vector<float> bend;
};

namespace {

uint64_t cellKey(const glm::ivec3& c) {
    // 21 bits per axis (cells of a centimetre reach +-10 km).
    return (uint64_t(uint32_t(c.x) & 0x1FFFFF) << 42) | (uint64_t(uint32_t(c.y) & 0x1FFFFF) << 21) |
           uint64_t(uint32_t(c.z) & 0x1FFFFF);
}

} // namespace

ClothColliders::ClothColliders() : impl_(std::make_unique<Impl>()) {}
ClothColliders::~ClothColliders() = default;
ClothColliders::ClothColliders(ClothColliders&&) noexcept = default;
ClothColliders& ClothColliders::operator=(ClothColliders&&) noexcept = default;

void ClothColliders::add(const Mesh& mesh, const glm::mat4& toHere) {
    std::vector<glm::vec3> placed;
    const bool moved = toHere != glm::mat4(1.0f);
    if (moved) {
        placed.resize(mesh.positions.size());
        for (size_t v = 0; v < placed.size(); ++v) placed[v] = glm::vec3(toHere * glm::vec4(mesh.positions[v], 1.0f));
    }
    std::span<const glm::vec3> positions = moved ? std::span<const glm::vec3>(placed) : std::span<const glm::vec3>(mesh.positions);
    if (!mesh.hasHidden()) {
        add(positions, mesh.triangles);
        return;
    }
    std::vector<glm::uvec3> shown;
    shown.reserve(mesh.triangles.size());
    for (size_t t = 0; t < mesh.triangles.size(); ++t)
        if (!mesh.triangleHidden(uint32_t(t))) shown.push_back(mesh.triangles[t]);
    add(positions, shown);
}

void ClothColliders::add(std::span<const glm::vec3> positions, std::span<const glm::uvec3> triangles) {
    const uint32_t base = uint32_t(impl_->positions.size());
    impl_->positions.insert(impl_->positions.end(), positions.begin(), positions.end());
    impl_->triangles.reserve(impl_->triangles.size() + triangles.size());
    for (const glm::uvec3& t : triangles) {
        if (t.x >= positions.size() || t.y >= positions.size() || t.z >= positions.size()) continue;
        impl_->triangles.push_back(t + glm::uvec3(base));
    }
    impl_->built = false;
}

void ClothColliders::build() const {
    Impl& m = *impl_;
    if (m.built) return;
    m.normals.assign(m.positions.size(), glm::vec3(0.0f));
    for (const glm::uvec3& t : m.triangles) {
        const glm::vec3 n = glm::cross(m.positions[t.y] - m.positions[t.x], m.positions[t.z] - m.positions[t.x]);
        m.normals[t.x] += n, m.normals[t.y] += n, m.normals[t.z] += n;
    }
    for (glm::vec3& n : m.normals) {
        const float l = glm::length(n);
        n = l > 1e-30f ? n / l : glm::vec3(0.0f, 1.0f, 0.0f);
    }
    m.bend.assign(m.positions.size(), 0.0f);
    for (const glm::uvec3& t : m.triangles)
        for (int k = 0; k < 3; ++k) {
            const uint32_t a = t[k], b = t[(k + 1) % 3];
            const float len = glm::distance(m.positions[a], m.positions[b]);
            if (len < 1e-12f) continue;
            const float turn = glm::distance(m.normals[a], m.normals[b]) / len; // (the chord: the angle, near enough)
            m.bend[a] = std::max(m.bend[a], turn), m.bend[b] = std::max(m.bend[b], turn);
        }
    m.bvh.build(m.positions, m.triangles);
    m.built = true;
}

void ClothColliders::sharpVertices(const glm::vec3& lo, const glm::vec3& hi, float radius,
                                   std::vector<uint32_t>& out) const {
    out.clear();
    const Impl& m = *impl_;
    const size_t n = m.positions.size();
    if (!m.built || n == 0 || radius <= 0.0f) return;
    const float minBend = 1.0f / radius;
    // Per block, joined in order.
    constexpr size_t kBlock = 1 << 16;
    std::vector<std::vector<uint32_t>> parts((n + kBlock - 1) / kBlock);
    poolFor(n, [&](size_t b, size_t e) {
        std::vector<uint32_t>& part = parts[b / kBlock];
        for (size_t i = b; i < e; ++i) {
            if (m.bend[i] < minBend) continue;
            const glm::vec3& p = m.positions[i];
            if (p.x >= lo.x && p.y >= lo.y && p.z >= lo.z && p.x <= hi.x && p.y <= hi.y && p.z <= hi.z)
                part.push_back(uint32_t(i));
        }
    }, kBlock);
    for (const auto& part : parts) out.insert(out.end(), part.begin(), part.end());
}

const glm::vec3& ClothColliders::vertex(uint32_t i) const { return impl_->positions[i]; }
const glm::vec3& ClothColliders::vertexNormal(uint32_t i) const { return impl_->normals[i]; }

bool ClothColliders::raycast(const glm::vec3& origin, const glm::vec3& dir, float maxT, glm::vec3& point,
                             glm::vec3& normal) const {
    const Impl& m = *impl_;
    if (!m.built || m.triangles.empty()) return false;
    SurfaceHit hit;
    if (!m.bvh.raycast(origin, dir, maxT, hit)) return false;
    point = hit.position;
    normal = m.bvh.faceNormal(hit.prim);
    return true;
}

bool ClothColliders::empty() const { return impl_->triangles.empty(); }
size_t ClothColliders::triangles() const { return impl_->triangles.size(); }

bool ClothColliders::closest(const glm::vec3& p, float maxDistance, glm::vec3& point, glm::vec3& normal) const {
    const Impl& m = *impl_;
    if (!m.built || m.triangles.empty()) return false;
    SurfaceHit hit;
    if (!m.bvh.closestPoint(p, maxDistance, hit)) return false;
    const glm::uvec3 t = m.triangles[hit.prim];
    glm::vec3 n = m.normals[t.x] * hit.bary.x + m.normals[t.y] * hit.bary.y + m.normals[t.z] * hit.bary.z;
    const float l = glm::length(n);
    normal = l > 1e-12f ? n / l : m.bvh.faceNormal(hit.prim);
    point = hit.position;
    return true;
}

// --- Solver ---

bool ClothSim::init(const ClothSetup& setup, const Fabric& fabric, const ClothSettings& settings) {
    *this = ClothSim{};
    const size_t n = setup.positions.size();
    if (n == 0 || setup.triangles.empty()) return false;
    if (!setup.rest.empty() && setup.rest.size() != n) return false;
    if (!setup.pin.empty() && setup.pin.size() != n) return false;
    for (const glm::uvec3& t : setup.triangles)
        if (t.x >= n || t.y >= n || t.z >= n) return false;
    for (const ClothSeam& s : setup.seams)
        if (s.a >= n || s.b >= n) return false;
    fabric_ = fabric;
    settings_ = settings;
    tris_ = setup.triangles;
    x_ = setup.positions;
    prev_ = x_;
    target_ = x_;
    v_.assign(n, glm::vec3(0.0f));
    dx_.assign(n, glm::vec3(0.0f));
    seamCompliance_ = setup.seamCompliance;

    // Masses: density x a third of each rest triangle's area.
    const std::vector<glm::vec3>& rest = setup.rest.empty() ? setup.positions : setup.rest;
    std::vector<float> mass(n, 0.0f);
    for (const glm::uvec3& t : tris_) {
        const float a = 0.5f * glm::length(glm::cross(rest[t.y] - rest[t.x], rest[t.z] - rest[t.x]));
        const float m = fabric.density * a / 3.0f;
        mass[t.x] += m, mass[t.y] += m, mass[t.z] += m;
    }
    invMass_.resize(n);
    pinW_.assign(n, 0.0f);
    for (size_t i = 0; i < n; ++i) {
        invMass_[i] = mass[i] > 1e-12f ? 1.0f / mass[i] : 0.0f; // (a particle on no triangle stays put)
    }
    w_ = invMass_;
    rest_ = rest;
    for (size_t i = 0; i < n; ++i) {
        if (!setup.pin.empty()) pin(uint32_t(i), setup.pin[i]);
    }
    buildConstraints(setup);
    double edges = 0.0;
    for (const Edge& e : stretch_) edges += e.rest;
    if (!stretch_.empty()) meanEdge_ = std::max(float(edges / double(stretch_.size())), 1e-4f);
    return true;
}

void ClothSim::setFabric(const Fabric& fabric) { fabric_ = fabric; }

void ClothSim::pin(uint32_t i, float weight) { pinTo(i, weight, i < x_.size() ? x_[i] : glm::vec3(0.0f)); }

void ClothSim::pinTo(uint32_t i, float weight, const glm::vec3& target) {
    if (i >= x_.size()) return;
    pinW_[i] = std::clamp(weight, 0.0f, 1.0f);
    target_[i] = target;
    w_[i] = pinW_[i] >= 1.0f ? 0.0f : invMass_[i]; // (a held particle doesn't give to its constraints)
    if (pinW_[i] >= 1.0f) x_[i] = target, v_[i] = glm::vec3(0.0f);
}

void ClothSim::setRestScale(uint32_t i, float scale) {
    if (i >= x_.size()) return;
    if (restScale_.empty()) {
        if (scale == 1.0f) return;
        restScale_.assign(x_.size(), 1.0f);
    }
    restScale_[i] = std::clamp(scale, 0.25f, 4.0f);
}

void ClothSim::buildConstraints(const ClothSetup& setup) {
    const std::vector<glm::vec3>& rest = setup.rest.empty() ? setup.positions : setup.rest;
    const size_t n = x_.size();
    // Edges with the corner opposite them in each triangle: sorted, so pairs of triangles sharing an edge meet.
    struct HalfEdge {
        uint32_t a, b, opposite;
    };
    std::vector<HalfEdge> half;
    half.reserve(tris_.size() * 3);
    for (const glm::uvec3& t : tris_)
        for (int k = 0; k < 3; ++k) {
            const uint32_t a = t[k], b = t[(k + 1) % 3], c = t[(k + 2) % 3];
            if (a != b) half.push_back({std::min(a, b), std::max(a, b), c});
        }
    std::sort(half.begin(), half.end(), [](const HalfEdge& l, const HalfEdge& r) {
        return l.a != r.a ? l.a < r.a : l.b != r.b ? l.b < r.b : l.opposite < r.opposite;
    });
    for (size_t i = 0; i < half.size();) {
        size_t j = i;
        while (j < half.size() && half[j].a == half[i].a && half[j].b == half[i].b) ++j;
        const uint32_t a = half[i].a, b = half[i].b;
        stretch_.push_back({a, b, glm::length(rest[a] - rest[b])});
        if (j - i == 2) { // (an interior edge between exactly two triangles: bend across it)
            const uint32_t c = half[i].opposite, d = half[i + 1].opposite;
            if (c != d) bend_.push_back({c, d, glm::length(rest[c] - rest[d])});
        }
        i = j;
    }
    for (const ClothSeam& s : setup.seams)
        if (s.a != s.b) seam_.push_back({s.a, s.b, 0.0f});

    // Colour every constraint so no two of one colour share a particle (greedy, in a fixed order: the same
    // colouring every time). Each colour then runs in parallel, the colours one after another (Gauss-Seidel).
    const size_t total = stretch_.size() + bend_.size() + seam_.size();
    std::vector<uint64_t> used(n, 0);
    std::vector<std::vector<uint32_t>> colours;
    std::vector<uint32_t> serial; // (the rare constraint that finds all 64 colours taken)
    auto place = [&](uint32_t id, const Edge& e) {
        const uint64_t taken = used[e.a] | used[e.b];
        if (taken == ~uint64_t(0)) {
            serial.push_back(id);
            return;
        }
        int c = 0;
        while (taken & (uint64_t(1) << c)) ++c;
        used[e.a] |= uint64_t(1) << c;
        used[e.b] |= uint64_t(1) << c;
        if (size_t(c) >= colours.size()) colours.resize(c + 1);
        colours[c].push_back(id);
    };
    for (uint32_t i = 0; i < stretch_.size(); ++i) place((kStretch << kKindShift) | i, stretch_[i]);
    for (uint32_t i = 0; i < bend_.size(); ++i) place((kBend << kKindShift) | i, bend_[i]);
    for (uint32_t i = 0; i < seam_.size(); ++i) place((kSeam << kKindShift) | i, seam_[i]);
    colourOffset_.assign(1, 0);
    order_.clear();
    order_.reserve(total);
    for (const auto& c : colours) {
        order_.insert(order_.end(), c.begin(), c.end());
        colourOffset_.push_back(uint32_t(order_.size()));
    }
    serialBegin_ = uint32_t(order_.size());
    order_.insert(order_.end(), serial.begin(), serial.end());
}

void ClothSim::project(uint32_t id, float h2inv) {
    const uint32_t kind = id >> kKindShift, i = id & ((1u << kKindShift) - 1);
    const Edge& e = kind == kStretch ? stretch_[i] : kind == kBend ? bend_[i] : seam_[i];
    const float wa = w_[e.a], wb = w_[e.b];
    const float w = wa + wb;
    if (w <= 0.0f) return;
    const glm::vec3 d = x_[e.a] - x_[e.b];
    const float l = glm::length(d);
    if (l < 1e-12f) return;
    const float rest = kind != kSeam && !restScale_.empty() ? e.rest * 0.5f * (restScale_[e.a] + restScale_[e.b]) : e.rest;
    const float c = l - rest;
    float alpha;
    if (kind == kStretch) alpha = (c > 0.0f ? fabric_.stretchCompliance : fabric_.compressCompliance) * rest;
    else if (kind == kBend) alpha = fabric_.bendCompliance * std::max(rest, 1e-6f);
    else alpha = seamAlpha_;
    const float dl = -c / (w + alpha * h2inv) * kOmega;
    const glm::vec3 g = d * (dl / l);
    x_[e.a] += wa * g;
    x_[e.b] -= wb * g;
}

void ClothSim::substep(float h) {
    const size_t n = x_.size();
    const glm::vec3 gravity = settings_.gravity, wind = settings_.wind;
    const float windDrag = glm::dot(wind, wind) > 0.0f ? kDrag : 0.0f;
    // Predict.
    poolFor(n, [&](size_t b, size_t e) {
        for (size_t i = b; i < e; ++i) {
            prev_[i] = x_[i];
            if (w_[i] <= 0.0f) continue;
            v_[i] += h * (gravity + windDrag * (wind - v_[i]));
            x_[i] += h * v_[i];
            // Soft pins pull toward their target before the constraints, so the fabric has the last word (a
            // driven region drags the cloth along instead of tearing it).
            if (const float pw = pinW_[i]; pw > 0.0f) x_[i] += (target_[i] - x_[i]) * (pw * pw);
        }
    }, 8192);
    // Constraints, one colour at a time.
    const float h2inv = 1.0f / (h * h);
    for (int it = 0; it < std::max(1, settings_.iterations); ++it) {
        for (size_t c = 0; c + 1 < colourOffset_.size(); ++c) {
            const uint32_t b0 = colourOffset_[c], e0 = colourOffset_[c + 1];
            poolFor(e0 - b0, [&](size_t b, size_t e) {
                for (size_t k = b; k < e; ++k) project(order_[b0 + k], h2inv);
            }, 4096);
        }
        for (size_t k = serialBegin_; k < order_.size(); ++k) project(order_[k], h2inv);
    }
    if (selfPairs_) separateSelf();
    if (!thin_.empty()) separateThin();
    // Pins (soft ones pull toward their target), contact, velocities.
    const float thickness = fabric_.thickness, friction = std::clamp(fabric_.friction, 0.0f, 1.0f);
    const float invH = 1.0f / h;
    poolFor(n, [&](size_t b, size_t e) {
        for (size_t i = b; i < e; ++i) {
            if (w_[i] <= 0.0f) {
                if (pinW_[i] >= 1.0f) x_[i] = target_[i];
                v_[i] = glm::vec3(0.0f);
                continue;
            }
            if (contact_[i]) {
                const glm::vec3 nrm = contactNormal_[i];
                const float sd = glm::dot(x_[i] - contactPoint_[i], nrm);
                if (sd < thickness) {
                    x_[i] += nrm * (thickness - sd);
                    // Friction: take back part of this substep's sliding along the surface.
                    glm::vec3 move = x_[i] - prev_[i];
                    move -= nrm * glm::dot(move, nrm);
                    x_[i] -= move * friction;
                }
            }
            v_[i] = (x_[i] - prev_[i]) * invH;
        }
    }, 8192);
}

void ClothSim::findContacts(float frame) {
    const size_t n = x_.size();
    contact_.assign(n, 0);
    contactPoint_.resize(n);
    contactNormal_.resize(n);
    if (!colliders_ || colliders_->empty()) return;
    const float thickness = fabric_.thickness;
    poolFor(n, [&](size_t b, size_t e) {
        for (size_t i = b; i < e; ++i) {
            if (w_[i] <= 0.0f) continue;
            // As far as the particle can travel this frame (gravity included), plus a margin.
            const float reach = thickness * 2.0f + (glm::length(v_[i]) + glm::length(settings_.gravity) * frame) * frame * 1.5f;
            glm::vec3 q, nrm;
            if (colliders_->closest(x_[i], reach, q, nrm)) {
                contact_[i] = 1;
                contactPoint_[i] = q;
                contactNormal_[i] = nrm;
            }
        }
    }, 1024);
}

// Thin features: collider vertices on features sharper than the particle spacing (ClothColliders::sharpVertices)
// within reach of a cloth triangle (inside its outline, near its plane) keep the side of it they are on, so a strand,
// a curl's ridge or a needle can't pass between particles. Smooth surfaces are left to the particles' own contacts. A pair seen last
// frame keeps its side out to an edge length off the plane; a new one takes the side of the vertex's nearest pair
// last frame when the triangles share a particle (a tip sliding across an edge), else the side it is on now (the
// reach covers a frame's travel, so that is before anything could cross).
void ClothSim::findThinPairs(float span) {
    constexpr uint32_t kSlots = 6;
    thinLast_.swap(thin_);
    thin_.clear();
    const size_t nt = tris_.size();
    thinLastAt_.assign(nt + 1, 0);
    for (const ThinPair& p : thinLast_) ++thinLastAt_[p.tri + 1];
    for (size_t t = 0; t < nt; ++t) thinLastAt_[t + 1] += thinLastAt_[t];
    const float thickness = fabric_.thickness;
    float fastest = 0.0f;
    glm::vec3 lo(std::numeric_limits<float>::max()), hi(-std::numeric_limits<float>::max());
    for (size_t i = 0; i < x_.size(); ++i) {
        fastest = std::max(fastest, glm::dot(v_[i], v_[i]));
        lo = glm::min(lo, x_[i]), hi = glm::max(hi, x_[i]);
    }
    const float travel = (std::sqrt(fastest) + glm::length(settings_.gravity) * span) * span * 1.5f;
    const float reach = std::clamp(thickness * 2.0f + travel, thickness * 2.0f, meanEdge_);
    // (held pairs reach an edge length off the cloth; the box is padded so a falling cloth keeps it a while)
    lo -= glm::vec3(meanEdge_), hi += glm::vec3(meanEdge_);
    if (glm::any(glm::lessThan(lo, thinLo_)) || glm::any(glm::greaterThan(hi, thinHi_))) {
        const glm::vec3 size = hi - lo;
        const glm::vec3 pad(std::max(size.x, std::max(size.y, size.z)) * 0.5f + meanEdge_ * 4.0f);
        thinLo_ = lo - pad, thinHi_ = hi + pad;
        colliders_->sharpVertices(thinLo_, thinHi_, meanEdge_ * 1.5f, thinVerts_);
        thinGrid_.resize(thinVerts_.size());
        for (size_t k = 0; k < thinVerts_.size(); ++k)
            thinGrid_[k] = {cellKey(glm::ivec3(glm::floor(colliders_->vertex(thinVerts_[k]) / meanEdge_))), thinVerts_[k]};
        std::sort(thinGrid_.begin(), thinGrid_.end());
    }
    if (thinGrid_.empty()) {
        thinNearest_.clear();
        return;
    }
    const float cell = meanEdge_;
    const auto& keyed = thinGrid_;
    const auto sharesParticle = [&](uint32_t s, uint32_t t) {
        const glm::uvec3 a = tris_[s], b = tris_[t];
        return glm::any(glm::equal(glm::uvec3(a.x), b)) || glm::any(glm::equal(glm::uvec3(a.y), b)) ||
               glm::any(glm::equal(glm::uvec3(a.z), b));
    };
    std::vector<ThinPair> slots(nt * kSlots, ThinPair{0, 0, 0.0f});
    poolFor(nt, [&](size_t b, size_t e) {
        std::vector<std::pair<float, ThinPair>> found;
        for (size_t t = b; t < e; ++t) {
            const glm::uvec3 tri = tris_[t];
            const glm::vec3 a = x_[tri.x], ab = x_[tri.y] - a, ac = x_[tri.z] - a;
            glm::vec3 n = glm::cross(ab, ac);
            const float area2 = glm::length(n);
            if (area2 < 1e-14f) continue;
            n /= area2;
            const float d00 = glm::dot(ab, ab), d01 = glm::dot(ab, ac), d11 = glm::dot(ac, ac);
            const float den = d00 * d11 - d01 * d01;
            if (std::abs(den) < 1e-20f) continue;
            // Where vertex v is against this triangle: off the plane (signed) and whether it is over the outline.
            const auto place = [&](uint32_t v, float& sd) {
                const glm::vec3 c = colliders_->vertex(v) - a;
                sd = glm::dot(c, n);
                const glm::vec3 q = c - n * sd;
                const float d20 = glm::dot(q, ab), d21 = glm::dot(q, ac);
                const float bv = (d11 * d20 - d01 * d21) / den, bw = (d00 * d21 - d01 * d20) / den;
                return bv >= -0.15f && bw >= -0.15f && bv + bw <= 1.15f;
            };
            found.clear();
            // Held pairs: out to an edge length.
            for (uint32_t k = thinLastAt_[t]; k < thinLastAt_[t + 1]; ++k) {
                float sd;
                const ThinPair& p = thinLast_[k];
                if (place(p.vertex, sd) && std::abs(sd) <= meanEdge_) found.push_back({p.side * sd, p});
            }
            const size_t held = found.size();
            const glm::vec3 tlo = glm::min(a, glm::min(x_[tri.y], x_[tri.z])) - glm::vec3(reach);
            const glm::vec3 thi = glm::max(a, glm::max(x_[tri.y], x_[tri.z])) + glm::vec3(reach);
            const glm::ivec3 c0(glm::floor(tlo / cell)), c1(glm::floor(thi / cell));
            if (glm::any(glm::greaterThan(c1 - c0, glm::ivec3(6)))) continue; // (a stretched-out triangle: skip)
            for (int z = c0.z; z <= c1.z; ++z)
                for (int y = c0.y; y <= c1.y; ++y)
                    for (int x = c0.x; x <= c1.x; ++x) {
                        const uint64_t key = cellKey({x, y, z});
                        auto it = std::lower_bound(keyed.begin(), keyed.end(), std::make_pair(key, 0u));
                        for (; it != keyed.end() && it->first == key; ++it) {
                            const uint32_t v = it->second;
                            bool known = false;
                            for (size_t h = 0; h < held && !known; ++h) known = found[h].second.vertex == v;
                            float sd;
                            if (known || !place(v, sd) || std::abs(sd) > reach || std::abs(sd) < 1e-7f) continue;
                            float side = sd > 0.0f ? 1.0f : -1.0f;
                            if (const auto nt0 = thinNearest_.find(v);
                                nt0 != thinNearest_.end() && sharesParticle(nt0->second.tri, uint32_t(t))) {
                                // (the neighbour's normal, flipped if this triangle faces the other way)
                                const glm::uvec3 o = tris_[nt0->second.tri];
                                const glm::vec3 on = glm::cross(x_[o.y] - x_[o.x], x_[o.z] - x_[o.x]);
                                side = glm::dot(on, n) >= 0.0f ? nt0->second.side : -nt0->second.side;
                            } else if (std::abs(sd) < thickness * 1.5f) {
                                // (on the plane: the cloth belongs outside the surface, along the vertex's normal)
                                const float facing = glm::dot(colliders_->vertexNormal(v), n);
                                if (std::abs(facing) > 0.2f) side = facing > 0.0f ? -1.0f : 1.0f;
                            }
                            found.push_back({side * sd, ThinPair{uint32_t(t), v, side}});
                        }
                    }
            // The kSlots deepest on their wrong side or nearest it (ties by vertex index): deterministic whatever
            // the grid's order.
            std::sort(found.begin(), found.end(), [](const auto& l, const auto& r) {
                return l.first != r.first ? l.first < r.first : l.second.vertex < r.second.vertex;
            });
            ThinPair* slot = &slots[t * kSlots];
            for (size_t k = 0; k < kSlots; ++k) slot[k] = k < found.size() ? found[k].second : ThinPair{0, 0, 0.0f};
        }
    }, 256);
    std::unordered_map<uint32_t, float> nearestD;
    thinNearest_.clear();
    for (const ThinPair& p : slots) {
        if (p.side == 0.0f) continue;
        thin_.push_back(p);
        const glm::uvec3 tri = tris_[p.tri];
        const glm::vec3 c = (x_[tri.x] + x_[tri.y] + x_[tri.z]) / 3.0f;
        const float d = glm::distance(c, colliders_->vertex(p.vertex));
        const auto [it, added] = nearestD.emplace(p.vertex, d);
        if (added || d < it->second) it->second = d, thinNearest_[p.vertex] = p;
    }
}

// Pushes each cloth triangle back to its side of the collider vertices it holds, `thickness` off (the vertex is
// fixed: the corners move by their share, as in a point-triangle constraint). Serial: deterministic.
void ClothSim::separateThin() {
    const size_t n = x_.size();
    const float thickness = fabric_.thickness;
    thinDx_.assign(n, glm::vec3(0.0f));
    thinCount_.assign(n, 0.0f);
    for (const ThinPair& p : thin_) {
        const glm::uvec3 tri = tris_[p.tri];
        const glm::vec3 a = x_[tri.x], ab = x_[tri.y] - a, ac = x_[tri.z] - a;
        glm::vec3 nrm = glm::cross(ab, ac);
        const float len = glm::length(nrm);
        if (len < 1e-14f) continue;
        nrm /= len;
        const glm::vec3 c = colliders_->vertex(p.vertex) - a;
        const float d = p.side * glm::dot(c, nrm);
        if (d >= thickness) continue;
        const glm::vec3 q = c - nrm * glm::dot(c, nrm);
        const float d00 = glm::dot(ab, ab), d01 = glm::dot(ab, ac), d11 = glm::dot(ac, ac);
        const float den = d00 * d11 - d01 * d01;
        if (std::abs(den) < 1e-20f) continue;
        const float d20 = glm::dot(q, ab), d21 = glm::dot(q, ac);
        const float bv = (d11 * d20 - d01 * d21) / den, bw = (d00 * d21 - d01 * d20) / den;
        if (bv < -0.25f || bw < -0.25f || bv + bw > 1.25f) continue; // (slid off: a neighbouring triangle has it)
        const float bary[3] = {std::clamp(1.0f - bv - bw, 0.0f, 1.0f), std::clamp(bv, 0.0f, 1.0f), std::clamp(bw, 0.0f, 1.0f)};
        const uint32_t ids[3] = {tri.x, tri.y, tri.z};
        float wsum = 0.0f;
        for (int k = 0; k < 3; ++k) wsum += w_[ids[k]] * bary[k] * bary[k];
        if (wsum <= 1e-12f) continue;
        // (at most a quarter of an edge a substep: a big push would turn into a big velocity)
        const glm::vec3 push = -p.side * nrm * (std::min(thickness - d, meanEdge_ * 0.25f) / wsum);
        for (int k = 0; k < 3; ++k) {
            if (w_[ids[k]] <= 0.0f || bary[k] <= 0.0f) continue;
            thinDx_[ids[k]] += push * (w_[ids[k]] * bary[k]);
            thinCount_[ids[k]] += 1.0f;
        }
    }
    const float friction = std::clamp(fabric_.friction, 0.0f, 1.0f);
    for (size_t i = 0; i < n; ++i)
        if (thinCount_[i] > 0.0f) {
            const glm::vec3 dx = thinDx_[i] / thinCount_[i];
            x_[i] += dx;
            // Friction against the feature, as at a contact: part of this substep's sliding across the push.
            const glm::vec3 nd = dx / std::max(glm::length(dx), 1e-12f);
            glm::vec3 move = x_[i] - prev_[i];
            move -= nd * glm::dot(move, nd);
            x_[i] -= move * friction;
        }
}

// Each particle's path through the frame against the colliders: one that crossed a face from its front ends up
// back in front of it (thickness off), its velocity into the surface removed. Catches what the frame's nearest
// contact missed: a second, thin feature on the way (a nose or a beard lock past the cheek it was resting on).
void ClothSim::sweepParticles() {
    const float thickness = fabric_.thickness;
    poolFor(x_.size(), [&](size_t b, size_t e) {
        for (size_t i = b; i < e; ++i) {
            if (w_[i] <= 0.0f || !contact_[i]) continue;
            glm::vec3 d = x_[i] - start_[i];
            const float len = glm::length(d);
            // (shorter than the way to the nearest surface at the start: it crossed nothing)
            if (len < 1e-6f || len < glm::distance(start_[i], contactPoint_[i])) continue;
            d /= len;
            glm::vec3 p, fn;
            if (!colliders_->raycast(start_[i], d, len, p, fn) || glm::dot(fn, d) >= 0.0f) continue;
            x_[i] = p + fn * thickness;
            const float vn = glm::dot(v_[i], fn);
            if (vn < 0.0f) v_[i] -= fn * vn;
        }
    }, 1024);
}

// Self-collision pairs for this frame: a hash grid of cells twice the reach, so every particle within reach of
// another is in one of the 2x2x2 cells nearest it. Each particle keeps its nearest kSelfSlots (ties by index).
void ClothSim::findSelfPairs(float frame) {
    const size_t n = x_.size();
    selfPairs_ = 0;
    const float d = settings_.selfDistance;
    if (d <= 0.0f) {
        selfNbr_.clear();
        return;
    }
    float fastest = 0.0f;
    for (const glm::vec3& v : v_) fastest = std::max(fastest, glm::dot(v, v));
    const float reach = d * 1.5f + std::min(std::sqrt(fastest) * frame, 4.0f * d);
    const float cell = 2.0f * reach, inv = 1.0f / cell;
    auto cellOf = [&](const glm::vec3& p) { return glm::ivec3(glm::floor(p * inv)); };
    auto key = [](glm::ivec3 c) {
        return (uint64_t(uint32_t(c.x) & 0x1fffff) << 42) | (uint64_t(uint32_t(c.y) & 0x1fffff) << 21) |
               uint64_t(uint32_t(c.z) & 0x1fffff);
    };
    std::vector<std::pair<uint64_t, uint32_t>> cells(n);
    for (size_t i = 0; i < n; ++i) cells[i] = {key(cellOf(x_[i])), uint32_t(i)};
    std::sort(cells.begin(), cells.end());
    std::unordered_map<uint64_t, std::pair<uint32_t, uint32_t>> range;
    range.reserve(n);
    for (uint32_t i = 0; i < n;) {
        uint32_t j = i;
        while (j < n && cells[j].first == cells[i].first) ++j;
        range.emplace(cells[i].first, std::make_pair(i, j));
        i = j;
    }
    selfNbr_.assign(n * kSelfSlots, ~0u);
    const float reach2 = reach * reach, skip2 = (2.5f * d) * (2.5f * d);
    std::vector<uint32_t> count(n, 0);
    poolFor(n, [&](size_t b, size_t e) {
        float best[kSelfSlots];
        for (size_t i = b; i < e; ++i) {
            const glm::vec3 p = x_[i], f = p * inv;
            const glm::ivec3 c = glm::ivec3(glm::floor(f));
            const glm::ivec3 o(f.x - float(c.x) < 0.5f ? -1 : 1, f.y - float(c.y) < 0.5f ? -1 : 1, f.z - float(c.z) < 0.5f ? -1 : 1);
            uint32_t* slot = &selfNbr_[i * kSelfSlots];
            uint32_t used = 0;
            for (int k = 0; k < 8; ++k) {
                const glm::ivec3 q = c + glm::ivec3(k & 1 ? o.x : 0, k & 2 ? o.y : 0, k & 4 ? o.z : 0);
                const auto it = range.find(key(q));
                if (it == range.end()) continue;
                for (uint32_t m = it->second.first; m < it->second.second; ++m) {
                    const uint32_t j = cells[m].second;
                    if (j == i) continue;
                    const glm::vec3 dv = x_[j] - p;
                    const float d2 = glm::dot(dv, dv);
                    if (d2 >= reach2) continue;
                    const glm::vec3 rv = rest_[j] - rest_[i];
                    if (glm::dot(rv, rv) < skip2) continue; // (neighbours in the fabric: the constraints space them)
                    // Insert by distance (then index), keeping the nearest kSelfSlots.
                    uint32_t at = used;
                    while (at > 0 && (best[at - 1] > d2 || (best[at - 1] == d2 && slot[at - 1] > j))) --at;
                    if (at >= kSelfSlots) continue;
                    for (uint32_t z = std::min(used, kSelfSlots - 1); z > at; --z) slot[z] = slot[z - 1], best[z] = best[z - 1];
                    slot[at] = j, best[at] = d2;
                    used = std::min(used + 1, kSelfSlots);
                }
            }
            count[i] = used;
        }
    }, 1024);
    for (uint32_t c : count) selfPairs_ += c;
}

// Pushes self-colliding particles apart to the self distance: each particle takes its mass share of every pair's
// overlap, read from the positions before the pass (Jacobi: the same on any thread count).
void ClothSim::separateSelf() {
    const size_t n = x_.size();
    const float d = settings_.selfDistance;
    poolFor(n, [&](size_t b, size_t e) {
        for (size_t i = b; i < e; ++i) {
            dx_[i] = glm::vec3(0.0f);
            const float wi = w_[i];
            if (wi <= 0.0f) continue;
            const uint32_t* slot = &selfNbr_[i * kSelfSlots];
            for (uint32_t k = 0; k < kSelfSlots && slot[k] != ~0u; ++k) {
                const uint32_t j = slot[k];
                const glm::vec3 dv = x_[i] - x_[j];
                const float l = glm::length(dv);
                if (l >= d || l < 1e-12f) continue;
                dx_[i] += dv * ((d - l) / l * wi / (wi + w_[j]) * 0.5f); // (relaxed by half: the substeps finish it)
            }
        }
    }, 4096);
    poolFor(n, [&](size_t b, size_t e) {
        for (size_t i = b; i < e; ++i) x_[i] += dx_[i];
    }, 8192);
}

bool ClothSim::step() {
    if (x_.empty()) return false;
    const int substeps = std::max(1, settings_.substeps);
    const float frame = std::max(1e-5f, settings_.frameSeconds);
    const float h = frame / float(substeps);
    // Seams close softly over the first frames, then hold at their compliance.
    const float ramp = std::min(1.0f, float(frame_) / float(kSeamRampFrames));
    seamAlpha_ = std::exp(std::log(1e-2f) * (1.0f - ramp) + std::log(std::max(seamCompliance_, 1e-9f)) * ramp);
    if (ramp >= 1.0f && seamCompliance_ <= 0.0f) seamAlpha_ = 0.0f;
    findContacts(frame);
    findSelfPairs(frame);
    // Thin pairs are looked for out to an edge length: a cloth moving farther than that in a frame looks again
    // within it, every `every` substeps.
    const bool thin = settings_.thinFeatures && colliders_ && !colliders_->empty();
    int every = substeps;
    if (thin) {
        start_ = x_;
        const float travel = (maxSpeed_ + glm::length(settings_.gravity) * frame) * frame * 1.5f;
        const int looks = std::clamp(int(std::ceil(travel / meanEdge_)), 1, substeps);
        every = (substeps + looks - 1) / looks;
        findThinPairs(h * float(every));
    } else {
        thin_.clear();
    }
    for (int s = 0; s < substeps; ++s) {
        if (thin && s > 0 && s % every == 0) findThinPairs(h * float(every));
        substep(h);
    }
    if (thin) sweepParticles();
    // Damping once per frame, then the rest test.
    const float keep = 1.0f - std::clamp(fabric_.damping, 0.0f, 1.0f);
    float fastest = 0.0f;
    for (size_t i = 0; i < x_.size(); ++i) {
        v_[i] *= keep;
        fastest = std::max(fastest, glm::dot(v_[i], v_[i]));
    }
    maxSpeed_ = std::sqrt(fastest);
    ++frame_;
    restCount_ = maxSpeed_ < settings_.restSpeed ? restCount_ + 1 : 0;
    return !atRest();
}

double ClothSim::kineticEnergy() const {
    double e = 0.0;
    for (size_t i = 0; i < x_.size(); ++i)
        if (invMass_[i] > 0.0f) e += 0.5 / double(invMass_[i]) * double(glm::dot(v_[i], v_[i]));
    return e;
}

float ClothSim::maxStrain() const {
    float worst = 0.0f;
    for (const Edge& e : stretch_)
        if (e.rest > 1e-9f) worst = std::max(worst, std::abs(glm::length(x_[e.a] - x_[e.b]) - e.rest) / e.rest);
    return worst;
}

DrapeResult drapeToRest(const ClothSetup& setup, const ClothColliders& colliders, const Fabric& fabric,
                        const DrapeOptions& options, const std::atomic<bool>* cancel) {
    const auto t0 = std::chrono::steady_clock::now();
    DrapeResult out;
    ClothSim sim;
    if (!sim.init(setup, fabric, options.settings)) return out;
    colliders.build(); // (no-op when built)
    sim.setColliders(&colliders);
    while (out.frames < options.maxFrames) {
        if (cancel && cancel->load(std::memory_order_relaxed)) break;
        const bool moving = sim.step();
        ++out.frames;
        if (options.progress && !options.progress(out.frames, sim.maxSpeed())) break;
        if (!moving) {
            out.rested = true;
            break;
        }
    }
    out.positions = sim.positions();
    out.milliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    return out;
}

} // namespace sf
