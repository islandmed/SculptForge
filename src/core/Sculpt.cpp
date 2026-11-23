// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/Sculpt.h"
#include "core/SculptKernel.h"

#include "core/AtomicRef.h"
#include "core/BrushAlpha.h"
#include "core/ThreadPool.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <iterator>
#include <limits>
#include <unordered_map>

#include "core/LegacyNames.h"

namespace sf {

const char* brushName(BrushType type) {
    switch (type) {
    case BrushType::Standard: return "Standard";
    case BrushType::Clay: return "Clay";
    case BrushType::Smooth: return "Smooth";
    case BrushType::Move: return "Move";
    case BrushType::Inflate: return "Inflate";
    case BrushType::Crease: return "Crease";
    case BrushType::Flatten: return "Flatten";
    case BrushType::Pinch: return "Pinch";
    case BrushType::SharpCrease: return "SharpCrease";
    case BrushType::ClayBlock: return "ClayBlock";
    case BrushType::SnakeHook: return "Tendril";
    case BrushType::Trim: return "Trim";
    case BrushType::Layer: return "Layer";
    case BrushType::Nudge: return "Nudge";
    case BrushType::Vdm: return "VDM";
    case BrushType::KelvinGrab: return "KelvinGrab";
    case BrushType::KelvinTwist: return "KelvinTwist";
    case BrushType::KelvinScale: return "KelvinScale";
    case BrushType::KelvinSnake: return "KelvinSnake";
    case BrushType::DetailClone: return "DetailClone";
    case BrushType::Morph: return "Morph";
    case BrushType::Cloth: return "Cloth";
    case BrushType::SmoothPeaks: return "SmoothPeaks";
    default: return "?";
    }
}

const char* brushLabel(BrushType type) {
    switch (type) {
    case BrushType::SharpCrease: return "Sharp Crease";
    case BrushType::SmoothPeaks: return "Smooth Peaks";
    case BrushType::ClayBlock: return "Clay Block";
    case BrushType::SnakeHook: return "Tendril";
    case BrushType::KelvinSnake: return "Elastic Tendril";
    default: return brushName(type);
    }
}

bool brushFromName(std::string_view name, BrushType& out) {
    auto squash = [](std::string_view s) {
        std::string r;
        for (unsigned char c : s)
            if (c != ' ' && c != '_' && c != '-' && c != '.') r += char(std::tolower(c));
        return r;
    };
    std::string want = squash(name);
    if (want.empty()) return false;
    want = legacy::modernize(want);
    for (const legacy::Rename& r : legacy::kBrushNames)
        if (r.old == want) want = squash(r.now);
    for (int i = 0; i < int(BrushType::Count); ++i)
        if (squash(brushName(BrushType(i))) == want || squash(brushLabel(BrushType(i))) == want) {
            out = BrushType(i);
            return true;
        }
    return false;
}

float brushFalloff(float dist, float radius, float focal) {
    float t = std::clamp(1.0f - dist / radius, 0.0f, 1.0f);
    if (focal != 0.0f) t = std::pow(t, std::exp2(2.0f * std::clamp(focal, -1.0f, 1.0f))); // x4 .. x1/4
    return t * t * (3.0f - 2.0f * t);
}

float focalRingFraction(float focal) {
    // smoothstep(t') = 0.5 at t' = 0.5, and t' = t^k.
    const float k = std::exp2(2.0f * std::clamp(focal, -1.0f, 1.0f));
    return 1.0f - std::pow(0.5f, 1.0f / k);
}

float alphaFalloff(float dist, float radius, float focal) {
    const float f = brushFalloff(dist, radius * kAlphaReach, focal);
    return focal < 0.0f ? f + (1.0f - f) * std::min(-focal, 1.0f) : f; // (-1: flat to the corners)
}

void brushVertices(const Mesh& mesh, const BVH& bvh, const glm::vec3& center, float radius, std::vector<uint32_t>& out) {
    bvh.queryVertices(mesh, center, radius, out);
    if (mesh.hasHidden()) std::erase_if(out, [&](uint32_t v) { return mesh.vertexHidden(v); });
}

glm::vec3 brushAreaNormal(const Mesh& mesh, const BVH& bvh, const glm::vec3& center, float radius, float focal) {
    std::vector<uint32_t> verts;
    brushVertices(mesh, bvh, center, radius, verts);
    glm::vec3 n(0.0f);
    for (uint32_t v : verts)
        if (v < mesh.normals.size()) n += mesh.normals[v] * brushFalloff(glm::distance(mesh.positions[v], center), radius, focal);
    const float l = glm::length(n);
    return l > 1e-20f ? n / l : glm::vec3(0.0f);
}

bool lineRayClosest(const glm::vec3& point, const glm::vec3& dir, const glm::vec3& origin, const glm::vec3& rayDir,
                    float& s, float minSine) {
    const float rl = glm::length(rayDir);
    if (!(rl > 1e-20f)) return false;
    const glm::vec3 r = rayDir / rl;
    const float b = glm::dot(dir, r);
    const float denom = 1.0f - b * b; // sin^2 of the angle between them
    if (!(denom > minSine * minSine)) return false;
    const glm::vec3 w = point - origin;
    s = (b * glm::dot(r, w) - glm::dot(dir, w)) / denom;
    return std::isfinite(s);
}

void keepConnected(const Mesh& mesh, const glm::vec3& seed, std::vector<uint32_t>& verts, ConnectedScratch& sc) {
    if (verts.empty()) return;
    const size_t n = mesh.vertexCount();
    if (sc.stamp.size() < n) sc.stamp.resize(n, 0);
    if (sc.epoch >= 0xfffffff0u) { // wrapped: start the stamps again
        std::fill(sc.stamp.begin(), sc.stamp.end(), 0u);
        sc.epoch = 0;
    }
    const uint32_t inside = ++sc.epoch, reached = ++sc.epoch;
    uint32_t start = verts[0];
    float best = std::numeric_limits<float>::max();
    for (uint32_t v : verts) {
        sc.stamp[v] = inside;
        const glm::vec3 d = mesh.positions[v] - seed;
        const float d2 = glm::dot(d, d);
        if (d2 < best) best = d2, start = v;
    }
    // Breadth first through the edges between vertices in the region.
    std::vector<uint32_t>& q = sc.queue;
    q.clear();
    q.push_back(start);
    sc.stamp[start] = reached;
    for (size_t head = 0; head < q.size(); ++head)
        for (uint32_t w : mesh.neighbors(q[head]))
            if (sc.stamp[w] == inside) {
                sc.stamp[w] = reached;
                q.push_back(w);
            }
    if (q.size() == verts.size()) return; // all of it is one piece
    std::erase_if(verts, [&](uint32_t v) { return sc.stamp[v] != reached; });
}

void brushRegion(const Mesh& mesh, const BVH& bvh, const glm::vec3& center, const glm::vec3& viewDir, float radius,
                 bool connectedOnly, bool viewFalloff, std::vector<uint32_t>& out, ConnectedScratch& scratch,
                 bool footprint) {
    if (footprint && !viewFalloff && mesh.normals.size() == mesh.vertexCount()) {
        bvh.queryVerticesCylinder(mesh, center, viewDir, radius, out, radius); // (depth: the reach, as below)
        const float r2 = radius * radius;
        // The sphere's own vertices, and the visible ones beyond it (not the part of the model far behind: the surface
        // follows the footprint, within the reach in depth too). Millions of candidates on a big stamp: flagged in parallel.
        static thread_local std::vector<uint8_t> keepStore;
        std::vector<uint8_t>& keep = keepStore; // (the workers write the caller's, not their own thread_local)
        keep.resize(out.size());
        const bool anyHidden = mesh.hasHidden();
        poolFor(out.size(), [&](size_t b, size_t e) {
            for (size_t i = b; i < e; ++i) {
                const uint32_t v = out[i];
                const glm::vec3 d = mesh.positions[v] - center;
                bool k = true;
                if (anyHidden && mesh.vertexHidden(v)) k = false;
                else if (glm::dot(d, d) > r2)
                    k = glm::dot(mesh.normals[v], viewDir) <= 0.0f && std::abs(glm::dot(d, viewDir)) <= radius;
                keep[i] = k;
            }
        }, 32768);
        size_t n = 0;
        for (size_t i = 0; i < out.size(); ++i)
            if (keep[i]) out[n++] = out[i];
        out.resize(n);
    } else if (viewFalloff) {
        bvh.queryVerticesCylinder(mesh, center, viewDir, radius, out);
        if (mesh.hasHidden()) std::erase_if(out, [&](uint32_t v) { return mesh.vertexHidden(v); });
    } else {
        brushVertices(mesh, bvh, center, radius, out);
    }
    if (connectedOnly) keepConnected(mesh, center, out, scratch);
}

namespace {

glm::vec3 projectOnPlane(const glm::vec3& v, const glm::vec3& n) { return v - n * glm::dot(v, n); }

// A micro level's dab takes the Stencil as its per-point pattern (the points are in copy `m`'s place); empty without one.
std::function<float(const glm::vec3&)> stencilTexture(const DabParams& p, const SymCopy& m) {
    if (!p.stencil.active()) return {};
    StencilView view = p.stencil;
    view.copy = m;
    return [view](const glm::vec3& x) { return view.weight(x); };
}

// Smooth's keep-volume steps (Sculptor::taubinShrink): Taubin's own values, pass band 1/lambda + 1/mu = 0.1.
constexpr float kTaubinLambda = 0.6307f, kTaubinMu = -0.6732f;

// --- Procedural 3D patterns (deterministic, so texture sticks to the surface) ---

uint32_t hash3(int x, int y, int z) {
    uint32_t h = uint32_t(x) * 0x8da6b343u ^ uint32_t(y) * 0xd8163841u ^ uint32_t(z) * 0xcb1ab31fu;
    h ^= h >> 13;
    h *= 0x5bd1e995u;
    h ^= h >> 15;
    return h;
}
float hashFloat(uint32_t h) { return float(h & 0xffffffu) / float(0xffffff); }

float valueNoise(const glm::vec3& p) {
    glm::vec3 i = glm::floor(p), f = p - i;
    glm::vec3 u = f * f * (3.0f - 2.0f * f);
    auto at = [&](int dx, int dy, int dz) {
        return hashFloat(hash3(int(i.x) + dx, int(i.y) + dy, int(i.z) + dz));
    };
    float x00 = glm::mix(at(0, 0, 0), at(1, 0, 0), u.x), x10 = glm::mix(at(0, 1, 0), at(1, 1, 0), u.x);
    float x01 = glm::mix(at(0, 0, 1), at(1, 0, 1), u.x), x11 = glm::mix(at(0, 1, 1), at(1, 1, 1), u.x);
    return glm::mix(glm::mix(x00, x10, u.y), glm::mix(x01, x11, u.y), u.z);
}

float fbm(const glm::vec3& p) {
    return (valueNoise(p) * 0.57f + valueNoise(p * 2.03f + 17.0f) * 0.29f + valueNoise(p * 4.01f + 31.0f) * 0.14f);
}

// Distances to the nearest and second-nearest jittered feature points.
void worley(const glm::vec3& p, float& f1, float& f2) {
    glm::vec3 c = glm::floor(p);
    f1 = f2 = 1e9f;
    for (int dz = -1; dz <= 1; ++dz)
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx) {
                int x = int(c.x) + dx, y = int(c.y) + dy, z = int(c.z) + dz;
                uint32_t h = hash3(x, y, z);
                glm::vec3 feature(float(x) + hashFloat(h), float(y) + hashFloat(h * 747796405u + 1u),
                                  float(z) + hashFloat(h * 2891336453u + 7u));
                float d = glm::distance(p, feature);
                if (d < f1) {
                    f2 = f1;
                    f1 = d;
                } else if (d < f2) {
                    f2 = d;
                }
            }
}

// Returns 0..1, where 1 means "full brush effect here".
float samplePattern(BrushRecipe::Pattern pattern, const glm::vec3& p) {
    switch (pattern) {
    case BrushRecipe::Pattern::Noise: return std::clamp(fbm(p) * 1.4f - 0.2f, 0.0f, 1.0f);
    case BrushRecipe::Pattern::Ridges: {
        float n = 1.0f - std::abs(2.0f * fbm(p) - 1.0f);
        return n * n * n;
    }
    case BrushRecipe::Pattern::Scales: {
        float f1, f2;
        worley(p, f1, f2);
        float t = std::clamp(f1 / std::max(f2, 1e-4f), 0.0f, 1.0f); // 0 at centre, 1 at the border
        return std::sqrt(std::max(0.0f, 1.0f - t * t));             // domed scale
    }
    case BrushRecipe::Pattern::Pores: {
        float f1, f2;
        worley(p, f1, f2);
        float t = std::clamp(f1 / 0.3f, 0.0f, 1.0f);
        return (1.0f - t) * (1.0f - t);
    }
    case BrushRecipe::Pattern::Cracks: {
        float f1, f2;
        worley(p, f1, f2);
        float t = std::clamp((f2 - f1) / 0.12f, 0.0f, 1.0f);
        return 1.0f - t * t * (3.0f - 2.0f * t);
    }
    default: return 1.0f;
    }
}

// The most a dragging dab (a recipe's Drag, Nudge) moves neighbours together or apart, as a share of their distance: past
// a half it squeezes the triangles at the steepest part of the falloff to a quarter of their area or less, the slivers the
// next dab turns over.
constexpr float kMaxDragSqueeze = 0.35f;
constexpr float kTendrilRingEdges = 2.0f; // Tendril with Dynamic on: its horn never pinches thinner than this many edges of radius
constexpr float kTendrilDepth = 0.5f;     // Tendril under View falloff: full strength to 0.75 radii of depth from the tip, none beyond 1.5
constexpr float kRelaxLambda = 0.6f; // (the share of the way to the middle of its neighbours a dragged vertex slides, per round)
constexpr int kRelaxRounds = 2;
constexpr size_t kLimitVerts = 12000; // (the flip limiter costs a look at every vertex of the region, a few per dab: only for a small brush's)
constexpr float kDragShare = 1.0f; // (overlapping copies' drags blend by nearness, over about the brush's radius)
constexpr float kRelaxStep = 0.1f; // (a dab that moves less than this share of the brush radius relaxes in proportion: slow strokes place many)

// Weighted sums over a brush region: per-block partials added in block order, so the result doesn't
// depend on the thread count (and a region of one block sums exactly like a plain loop). In double, so that the
// order a region's vertices come in (the two halves of a mirrored dab list theirs differently) doesn't show in the
// float it rounds to.
struct AreaSums {
    glm::dvec3 normal{0.0}, center{0.0};
    double weight = 0.0;
};
template <class F>
AreaSums areaSums(size_t n, F&& add) {
    constexpr size_t kBlock = 4096;
    std::vector<AreaSums> parts((n + kBlock - 1) / kBlock);
    poolFor(n, [&](size_t b, size_t e) {
        AreaSums& acc = parts[b / kBlock];
        for (size_t i = b; i < e; ++i) add(i, acc);
    }, kBlock);
    AreaSums total;
    for (const AreaSums& p : parts) {
        total.normal += p.normal;
        total.center += p.center;
        total.weight += p.weight;
    }
    return total;
}

} // namespace

std::vector<glm::vec3> Sculptor::mirrors(Symmetry sym) {
    std::vector<glm::vec3> out;
    for (int mask = 0; mask < 8; ++mask) {
        if ((mask & 1 && !sym.x) || (mask & 2 && !sym.y) || (mask & 4 && !sym.z)) continue;
        out.emplace_back(mask & 1 ? -1.0f : 1.0f, mask & 2 ? -1.0f : 1.0f, mask & 4 ? -1.0f : 1.0f);
    }
    return out;
}

std::vector<SymCopy> symmetryCopies(const Symmetry& sym) {
    const glm::vec3 pivot = sym.local ? sym.pivot : glm::vec3(0.0f);
    const int n = sym.radialOn() ? std::clamp(sym.radial, 1, Symmetry::kMaxRadial) : 1;
    const int axis = std::clamp(sym.radialAxis, 0, 2);
    std::vector<SymCopy> out;
    for (const glm::vec3& s : Sculptor::mirrors(sym))
        for (int k = 0; k < n; ++k) {
            // A turn of k/n about the axis (exact for the quarter and half turns, so duplicates compare equal).
            glm::mat3 r(1.0f);
            if (k > 0) {
                const double a = 2.0 * 3.14159265358979323846 * double(k) / double(n);
                float c = float(std::cos(a)), si = float(std::sin(a));
                if (4 * k % n == 0) { // a multiple of a quarter turn
                    c = std::round(c);
                    si = std::round(si);
                }
                const int u = (axis + 1) % 3, v = (axis + 2) % 3; // r turns u towards v
                r[u][u] = c;
                r[u][v] = si;
                r[v][u] = -si;
                r[v][v] = c;
            }
            SymCopy copy;
            copy.m = glm::mat3(glm::vec3(s.x, 0, 0), glm::vec3(0, s.y, 0), glm::vec3(0, 0, s.z)) * r;
            copy.pivot = pivot;
            bool repeated = false;
            for (const SymCopy& o : out) {
                float d = 0.0f;
                for (int i = 0; i < 3; ++i) d += glm::dot(o.m[i] - copy.m[i], o.m[i] - copy.m[i]);
                repeated = repeated || d < 1e-8f;
            }
            if (!repeated) out.push_back(copy);
        }
    return out;
}

glm::vec3 symmetryPivot(const Mesh& mesh) {
    const size_t n = mesh.positions.size();
    if (n == 0) return glm::vec3(0.0f);
    constexpr size_t kBlock = 1 << 16;
    std::vector<AABB> boxes((n + kBlock - 1) / kBlock);
    poolFor(n, [&](size_t b, size_t e) {
        AABB box;
        for (size_t i = b; i < e; ++i) box.expand(mesh.positions[i]);
        boxes[b / kBlock] = box;
    }, kBlock);
    AABB all;
    for (const AABB& b : boxes) {
        all.expand(b.min);
        all.expand(b.max);
    }
    return all.center();
}

std::vector<size_t> symmetryInverses(std::span<const SymCopy> copies) {
    std::vector<size_t> out(copies.size(), 0);
    for (size_t i = 0; i < copies.size(); ++i) {
        const glm::mat3 t = glm::transpose(copies[i].m); // (orthogonal: the inverse)
        float best = 1e30f;
        for (size_t j = 0; j < copies.size(); ++j) {
            float d = 0.0f;
            for (int c = 0; c < 3; ++c) d += glm::dot(copies[j].m[c] - t[c], copies[j].m[c] - t[c]);
            if (d < best) {
                best = d;
                out[i] = j;
            }
        }
    }
    return out;
}

namespace {

// Sets bit `v` (atomically: neighbouring blocks of a parallel pass may share a word); true if it was clear.
bool markBit(std::vector<uint64_t>& bits, uint32_t v) {
    const uint64_t bit = uint64_t(1) << (v & 63);
    AtomicRef<uint64_t> w(bits[v >> 6]);
    if (w.load(std::memory_order_relaxed) & bit) return false;
    return !(w.fetch_or(bit, std::memory_order_relaxed) & bit);
}

// Clears the bits of `indices` (what a stroke recorded), so the next stroke starts with an empty set.
void clearBits(std::vector<uint64_t>& bits, const std::vector<uint32_t>& indices) {
    poolFor(indices.size(), [&](size_t b, size_t e) {
        for (size_t i = b; i < e; ++i)
            if (indices[i] < bits.size() * 64) AtomicRef<uint64_t>(bits[indices[i] >> 6]).store(0, std::memory_order_relaxed);
    }, 1 << 14);
}

} // namespace

void Sculptor::clearRecorded() {
    clearBits(touched_, record_.positions.indices);
    clearBits(masked_, record_.mask.indices);
    clearBits(colored_, record_.colors.indices);
}

bool Sculptor::beginStroke(const Mesh& mesh, std::string label, int layer) {
    if (layer >= static_cast<int>(mesh.layers.size())) layer = -1;
    if (std::abs(mesh.layerTargetWeight(layer)) < 1e-3f) return false;
    clearRecorded(); // a stroke that never ended
    layer_ = layer;
    record_ = UndoRecord{};
    record_.label = std::move(label);
    record_.layer = layer;
    const size_t words = (mesh.vertexCount() + 63) / 64;
    if (touched_.size() != words) {
        touched_.assign(words, 0);
        masked_.assign(words, 0);
        colored_.assign(words, 0);
    }
    ++strokeId_;
    nonFiniteDabs = 0;
    draft_ = draftStrokes && !dyntopo.enabled;
    draftStrokes = false; // (asked for per stroke)
    draftLeaves_.clear();
    dropLazy(); // (a stroke that never ended)
    pending_.clear();
    lockedPlanes_.clear();
    lastRegion_ = 0;
    lastRegionRadius_ = 0.0f;
    strokeDiagonal_ = 0.0f;
    lastRemesh_ = false;
    topologyHeld_ = false;
    microMesh_ = nullptr;
    microBase_ = -1;
    inkKeep_ = keepEditable, inkOk_ = true, inkHead_ = false;
    inkAlphaFrom_ = nullptr;
    inkDabs_.clear();
    inkLast_ = 0;
    inkFixed_ = 0;
    inkMesh_ = nullptr;
    inkStep_ = mesh.micro ? mesh.micro->step : 0.0f;
    cageFree_.clear();
    cageTriMax_.clear();
    kelvinMaskBlocked_.v = false;
    active_ = true;
    return true;
}

bool Sculptor::dragSlideDefault() {
    static const bool on = [] {
        const char* e = std::getenv("SF_DRAG_SLIDE");
        return !(e && e[0] == '0');
    }();
    return on;
}

bool Sculptor::microBaseDefault() {
    static const bool on = [] {
        const char* e = std::getenv("SF_MICRO_BASE");
        return !(e && e[0] == '0');
    }();
    return on;
}

bool Sculptor::microBaseDab(const Mesh& mesh, BrushType type, const DabParams& p) {
    if (microBase_ < 0) { // (decided once a stroke: its dabs all go one way)
        const MicroMesh& mm = *mesh.micro;
        microBase_ = microBaseDabs && (type == BrushType::Standard || type == BrushType::Clay || type == BrushType::Inflate) &&
                             !mm.hasMask() && mesh.mask.empty() && !mesh.hasHidden() && !(p.alpha && !p.alpha->empty()) && !p.stencil.active() &&
                             mesh.layers.empty() && !dyntopo.enabled && mm.baseEdge > 0.0f &&
                             p.radius >= kMicroBaseSpan * mm.baseEdge
                         ? 1
                         : 0;
    }
    return microBase_ == 1;
}

void Sculptor::microBaseKernel(Mesh& mesh, BVH& bvh, BrushType type, const DabParams& p, Symmetry sym) {
    auto clock = std::chrono::steady_clock::now();
    const MicroMesh& mm = *mesh.micro;
    const uint32_t n = mm.n, S = mm.samples;
    const float r = p.radius, r2 = r * r;
    const float s = std::clamp(p.strength * std::clamp(p.pressure, 0.0f, 1.0f), 0.0f, 1.0f);
    const float sign = p.invert ? -1.0f : 1.0f;
    for (const SymCopy& m : copies(sym)) {
        const glm::vec3 c = m.point(p.center), view = m.dir(p.viewDir);
        // The corners within reach: base points whose displaced corner may be in the sphere.
        bvh.queryVertices(mesh, c, r + float(mm.rangeMax) * mm.step, baseVerts_);
        const size_t nv = baseVerts_.size();
        baseFall_.assign(nv, 0.0f);
        baseCorner_.resize(nv);
        baseNormal_.resize(nv);
        poolFor(nv, [&](size_t b, size_t e) {
            for (size_t i = b; i < e; ++i) {
                const uint32_t v = baseVerts_[i];
                const IndexRing ring = mesh.vertexTriangles(v);
                if (ring.empty()) continue;
                const uint32_t t = ring[0];
                const glm::uvec3 tr = mesh.triangles[t];
                const int k = tr.x == v ? 0 : tr.y == v ? 1 : 2;
                const int16_t d = mm.disp[size_t(t) * S + (k == 0 ? 0 : k == 1 ? MicroMesh::index(n, 0, n) : MicroMesh::index(0, n, n))];
                const glm::vec3 nn = mesh.normals[v];
                const float len = glm::length(nn);
                const glm::vec3 un = len > 1e-20f ? nn / len : glm::vec3(0.0f, 0.0f, 1.0f);
                const glm::vec3 q = mesh.positions[v] + un * (float(d) * mm.step);
                const float d2 = glm::dot(q - c, q - c);
                baseCorner_[i] = q, baseNormal_[i] = un;
                if (d2 < r2) baseFall_[i] = brushFalloff(std::sqrt(d2), r, p.focal);
            }
        }, 4096);
        // The brush plane (Standard, Clay): weighted normal and centre of the corners under the brush.
        glm::vec3 areaNormal = -view, areaCenter = c;
        if (type != BrushType::Inflate) {
            glm::dvec3 nsum(0.0), csum(0.0);
            double w = 0.0;
            for (size_t i = 0; i < nv; ++i)
                if (baseFall_[i] > 0.0f) {
                    nsum += glm::dvec3(baseNormal_[i] * baseFall_[i]);
                    csum += glm::dvec3(baseCorner_[i] * baseFall_[i]);
                    w += baseFall_[i];
                }
            if (w > 0.0) {
                areaCenter = glm::vec3(csum / w);
                const double len = glm::length(nsum);
                if (len > 1e-12) areaNormal = glm::vec3(nsum / len);
            }
        }
        // The plain mesh's kernels (SculptKernel.h: Standard and Clay along the plane's normal, Inflate along the
        // vertex's): the grid's own Clay moves each point along its normal by up to 3x the gap to the plane, which on
        // a scan's noisy normals and deep folds overshoots, dab after dab, into spikes hundreds of units long.
        PlaneKernel plane;
        plane.type = type;
        plane.center = c;
        plane.viewDir = view;
        plane.areaNormal = areaNormal;
        plane.areaCenter = areaCenter;
        plane.r = r;
        plane.s = s;
        plane.sign = sign;
        plane.focal = p.focal;
        plane.invert = p.invert;
        baseMoves_.assign(nv, glm::vec3(0.0f));
        poolFor(nv, [&](size_t b, size_t e) {
            for (size_t i = b; i < e; ++i) {
                const float f = baseFall_[i];
                if (f <= 0.0f) continue;
                baseMoves_[i] = type == BrushType::Inflate ? baseNormal_[i] * (sign * s * r * 0.05f * f) : plane.offsetWith(baseCorner_[i], f);
            }
        }, 4096);
        size_t k = 0;
        for (size_t i = 0; i < nv; ++i)
            if (baseMoves_[i] != glm::vec3(0.0f)) baseVerts_[k] = baseVerts_[i], baseMoves_[k++] = baseMoves_[i];
        baseVerts_.resize(k);
        baseMoves_.resize(k);
        timings.kernelMs += lapMs(clock);
        timings.vertices += k;
        moveVertices(mesh, bvh, baseVerts_, baseMoves_); // (the undo record, normals, BVH)
        timings.normalsMs += lapMs(clock);
    }
    ++timings.dabs;
}

bool Sculptor::microReady(Mesh& mesh) {
    if (!mesh.micro || !microMatches(mesh)) return false;
    if (!micro_.active() || microMesh_ != &mesh) {
        micro_.hashTiles = inkKeep_;
        micro_.begin(mesh);
        microMesh_ = &mesh;
        inkStep_ = mesh.micro->step;
    }
    return true;
}

void Sculptor::inkHeader(bool maskBrush, const BrushAlpha* alpha, int pattern, float patternScale, float patternAmount) {
    if (!inkKeep_ || !inkOk_) return;
    if (alpha && alpha->empty()) alpha = nullptr;
    if (pattern == 0 || patternAmount <= 0.0f) pattern = 0, patternScale = 0.3f, patternAmount = 0.0f;
    if (!inkHead_) {
        inkHead_ = true;
        ink_ = MicroInk{};
        ink_.maskBrush = maskBrush;
        ink_.adaptive = micro_.adaptive;
        ink_.pattern = pattern, ink_.patternScale = patternScale, ink_.patternAmount = patternAmount;
        inkAlphaFrom_ = alpha;
        if (alpha) { // (a snapshot: the brush's alpha may change after the stroke)
            if (!inkAlpha_ || inkAlpha_->width != alpha->width || inkAlpha_->height != alpha->height || inkAlpha_->tileX != alpha->tileX ||
                inkAlpha_->tileY != alpha->tileY || inkAlpha_->values != alpha->values)
                if (!(inkAlpha_ = BrushAlpha::shared(*alpha))) inkAlpha_ = std::make_shared<const BrushAlpha>(*alpha);
            ink_.alpha = inkAlpha_;
        }
        return;
    }
    // (one header a stroke: a dab that needs another one can't be kept)
    if (ink_.maskBrush != maskBrush || inkAlphaFrom_ != alpha || ink_.pattern != pattern || ink_.patternScale != patternScale ||
        ink_.patternAmount != patternAmount)
        inkOk_ = false;
}

void Sculptor::inkDab(MicroDab d) {
    if (!inkKeep_ || !inkOk_) return;
    d.seq = uint32_t(inkDabs_.size());
    inkDabs_.push_back(d);
}

void Sculptor::inkFinish(Mesh& mesh) {
    // Everything is fixed by a new topology, sculpt layers moving, or the grid's range growing (every value rescaled,
    // which no replay repeats exactly); base vertices moving (Move on the cage, big dabs' base kernel) fix the strokes
    // on the triangles whose corners or corner normals they changed.
    const bool everything = record_.topology || !record_.layerOffsets.empty() || !mesh.micro || mesh.micro->step != inkStep_;
    const bool baseMoved = everything || !record_.positions.empty() || microBase_ == 1;
    const bool keep = inkKeep_ && inkOk_ && inkHead_ && record_.micro && !baseMoved && !inkDabs_.empty();
    MicroHistory* h = microHistory(mesh, keep);
    inkFixed_ = 0;
    if (h) {
        std::vector<uint32_t> tiles;
        if (record_.micro) tiles = record_.micro->tris;
        if (!everything && !record_.positions.empty()) {
            std::vector<uint32_t> ring; // (moved vertices and their neighbours: their normals changed)
            for (uint32_t v : record_.positions.indices) {
                if (v >= mesh.vertexCount()) continue;
                ring.push_back(v);
                for (uint32_t u : mesh.neighbors(v)) ring.push_back(u);
            }
            std::sort(ring.begin(), ring.end());
            ring.erase(std::unique(ring.begin(), ring.end()), ring.end());
            for (uint32_t v : ring)
                for (uint32_t t : mesh.vertexTriangles(v)) tiles.push_back(t);
        }
        std::sort(tiles.begin(), tiles.end());
        tiles.erase(std::unique(tiles.begin(), tiles.end()), tiles.end());
        if (keep) inkLast_ = h->record(ink_, std::move(inkDabs_), record_.micro, std::move(micro_.tilePre), std::move(micro_.tilePost));
        else if (everything) inkFixed_ = h->bakeAll();
        else if (!tiles.empty()) inkFixed_ = h->bakeMeeting(tiles);
    }
    inkDabs_.clear();
    micro_.tilePre.clear(), micro_.tilePost.clear();
}

void Sculptor::remeshDab(Mesh& mesh, BVH& bvh, const DabParams& p, Symmetry sym, float scale, float reach) {
    if (!dyntopo.enabled || topologyHeld_) return;
    auto clock = std::chrono::steady_clock::now();
    if (dyntopo.constantDetail && strokeDiagonal_ <= 0.0f) strokeDiagonal_ = glm::length(mesh.bounds().extent());
    const float edge = dyntopoEdgeLength(dyntopo, p.radius, strokeDiagonal_) * scale;
    const std::vector<SymCopy> ms = copies(sym);
    // Over everything the brush can reach (square tips and alphas reach into their corners).
    dynTopo_.remesh(mesh, bvh, p.center, p.radius * reach, edge, ms);
    lastRemesh_ = true;
    lastCenter_ = p.center;
    lastRadius_ = p.radius * reach;
    lastEdge_ = edge;
    lastMirrors_ = ms;
    followRemesh(mesh);
    timings.remeshMs += lapMs(clock);
}

// After the brush: when it squashed a triangle flat (pinching or flattening a fold), the dab is remeshed
// again to repair it (rare: the triangles around the moved vertices are checked first).
void Sculptor::repairDab(Mesh& mesh, BVH& bvh) {
    if (!lastRemesh_ || !dynTopo_.active() || dynTopo_.strokeMesh() != &mesh) return;
    auto clock = std::chrono::steady_clock::now();
    const float flat = 1e-4f * lastEdge_ * lastEdge_;
    bool squashed = false;
    for (size_t i = 0; i < dabVerts_.size() && !squashed; ++i) {
        if (dabVerts_[i] >= mesh.vertexCount()) continue;
        for (uint32_t t : mesh.vertexTriangles(dabVerts_[i])) {
            const glm::uvec3& tri = mesh.triangles[t];
            const auto& pos = mesh.positions;
            if (glm::length(glm::cross(pos[tri.y] - pos[tri.x], pos[tri.z] - pos[tri.x])) < flat) {
                squashed = true;
                break;
            }
        }
    }
    if (squashed) {
        dynTopo_.remesh(mesh, bvh, lastCenter_, lastRadius_, lastEdge_, lastMirrors_);
        followRemesh(mesh);
    }
    timings.remeshMs += lapMs(clock);
}

// The stroke's per-vertex state follows a remesh.
void Sculptor::followRemesh(Mesh& mesh) {
    // The stroke's per-vertex state follows: the remesher's record holds the stroke-start state of every slot it
    // touched, so this stroke's own undo entries never name them; the Layer brush's heights move with their vertices.
    const size_t words = (mesh.vertexCount() + 63) / 64;
    if (touched_.size() < words) {
        touched_.resize(words, 0);
        masked_.resize(words, 0);
        colored_.resize(words, 0);
    }
    for (uint32_t v : dynTopo_.recordedVertices()) {
        if (size_t(v >> 6) >= touched_.size()) {
            touched_.resize(size_t(v >> 6) + 1, 0);
            masked_.resize(size_t(v >> 6) + 1, 0);
            colored_.resize(size_t(v >> 6) + 1, 0);
        }
        const uint64_t bit = uint64_t(1) << (v & 63);
        touched_[v >> 6] |= bit;
        masked_[v >> 6] |= bit;
        colored_[v >> 6] |= bit;
    }
    if (!layerStamp_.empty() && (layerStamp_.size() != mesh.vertexCount() || !dynTopo_.movedVertices().empty())) {
        const size_t nv = mesh.vertexCount();
        for (const auto& [from, to] : dynTopo_.movedVertices())
            if (from < layerStamp_.size() && to < layerStamp_.size()) {
                layerStamp_[to] = layerStamp_[from];
                layerDone_[to] = layerDone_[from];
                layerDir_[to] = layerDir_[from];
            }
        layerStamp_.resize(nv, 0);
        layerDone_.resize(nv, 0.0f);
        layerDir_.resize(nv, glm::vec3(0.0f));
    }
}

// This stroke's undo entries for slots the remesher recorded later hold their true stroke-start values (the
// remesher's copies are from when it first touched them): they move into its record.
void Sculptor::mergeTopologyUndo(TopologyUndo& topo, const Mesh& mesh) {
    std::unordered_map<uint32_t, uint32_t> at; // slot -> index into the record's stored values
    uint32_t k = 0;
    for (uint32_t v : topo.vertexSlots)
        if (v < topo.vertexCount) at.emplace(v, k++);
    auto merge = [&](auto& channel, auto&& store) {
        size_t out = 0;
        for (size_t i = 0; i < channel.indices.size(); ++i) {
            const auto it = at.find(channel.indices[i]);
            if (it != at.end()) {
                store(it->second, channel.values[i]);
                continue;
            }
            channel.indices[out] = channel.indices[i];
            channel.values[out++] = channel.values[i];
        }
        channel.indices.resize(out);
        channel.values.resize(out);
    };
    TopologyVertexValues& tv = topo.vertices;
    const int layer = record_.layer;
    // (Only entries from before the stroke's first remesh can be here: afterwards the remesher records first.)
    const size_t before = record_.positions.indices.size() + record_.colors.indices.size();
    merge(record_.positions, [&](uint32_t i, const glm::vec3& value) {
        std::vector<glm::vec3>& target = layer >= 0 && size_t(layer) < tv.deltas.size() ? tv.deltas[size_t(layer)]
                                         : mesh.layers.empty()                           ? tv.positions
                                                                                         : tv.base;
        if (i < target.size()) target[i] = value;
    });
    merge(record_.mask, [&](uint32_t i, float value) {
        if (i < tv.mask.size()) tv.mask[i] = value;
    });
    const int pl = record_.paintLayer;
    merge(record_.colors, [&](uint32_t i, const glm::vec3& value) {
        std::vector<glm::vec3>& target = mesh.paintLayers.empty() ? tv.colors
                                         : pl < 0 || size_t(pl) >= tv.paintColors.size() ? tv.paintBase
                                                                                         : tv.paintColors[size_t(pl)];
        if (i < target.size()) target[i] = value;
    });
    merge(record_.paintAlpha, [&](uint32_t i, float value) {
        if (pl >= 0 && size_t(pl) < tv.paintAlphas.size() && i < tv.paintAlphas[size_t(pl)].size()) tv.paintAlphas[size_t(pl)][i] = value;
    });
    if (before != record_.positions.indices.size() + record_.colors.indices.size() && (!mesh.layers.empty() || !mesh.paintLayers.empty()))
        for (const auto& [slot, k] : at) topo.reevaluate.push_back(slot);
}

// While the stroke remeshes, the remesher's record keeps every vertex's stroke-start state (exact, whatever
// slot it ends up in); otherwise this stroke's own channels do.
void Sculptor::touch(Mesh& mesh, uint32_t v) {
    if (!markBit(touched_, v)) return;
    if (dynTopo_.active()) dynTopo_.recordStrokeStart(v);
    else record_.positions.add(v, mesh.layerTarget(layer_)[v]);
}

void Sculptor::recordMask(const Mesh& mesh, uint32_t v) {
    if (!active_ || !markBit(masked_, v)) return;
    if (dynTopo_.active()) {
        dynTopo_.recordStrokeStart(v);
        return;
    }
    record_.mask.add(v, mesh.maskAt(v));
}

void Sculptor::recordColor(const Mesh& mesh, uint32_t v) {
    if (!active_ || !markBit(colored_, v)) return;
    if (dynTopo_.active()) {
        dynTopo_.recordStrokeStart(v);
        return;
    }
    // With paint layers the stroke paints the active layer (colour and coverage).
    const int layer = mesh.paintLayers.empty() ? -1 : mesh.activePaintLayer;
    record_.paintLayer = layer;
    if (mesh.paintLayers.empty()) {
        record_.colors.add(v, mesh.colorAt(v));
    } else if (layer < 0) {
        record_.colors.add(v, mesh.paintBase[v]);
    } else {
        record_.colors.add(v, mesh.paintLayers[size_t(layer)].color[v]);
        record_.paintAlpha.add(v, mesh.paintLayers[size_t(layer)].alpha[v]);
    }
}

// Moves a vertex and attributes the offset to the active layer. Masked and
// hidden vertices are attenuated here, once, for every brush.
void Sculptor::displace(Mesh& mesh, uint32_t v, const glm::vec3& d) {
    const float k = vertexInfluence(mesh, v);
    if (k <= 0.0f) return;
    touch(mesh, v);
    const glm::vec3 dk = d * k;
    mesh.positions[v] += dk;
    if (!mesh.layers.empty()) mesh.layerTarget(layer_)[v] += dk / mesh.layerTargetWeight(layer_);
}

// displace() for many vertices (each listed once), in parallel: the influences and which vertices the stroke
// moves for the first time, then their undo entries (in list order, at offsets counted per block) and the moves.
void Sculptor::moveVertices(Mesh& mesh, BVH& bvh, std::span<const uint32_t> verts, std::span<const glm::vec3> d) {
    if (!active_ || verts.empty()) return;
    displaceAll(mesh, verts, d, false);
    mesh.updateNormals(verts);
    if (verts.size() * 4 > mesh.vertexCount()) bvh.refitAll(mesh);
    else bvh.refit(mesh, verts);
}

bool Sculptor::kelvinNearMask(const Mesh& mesh, const BVH& bvh, const glm::vec3& center, float radius) {
    if (!mesh.micro || !(mesh.micro->hasMask() || mesh.hasMask()) || !microMaskNear(mesh, bvh, center, radius)) return false;
    kelvinMaskBlocked_.v.store(true, std::memory_order_relaxed);
    return true;
}

void Sculptor::cageFreeFor(const Mesh& mesh, std::span<const uint32_t> verts) {
    const MicroMesh& mm = *mesh.micro;
    if (cageFree_.size() != mesh.vertexCount()) cageFree_.assign(mesh.vertexCount(), -1.0f);
    if (cageTriMax_.size() != mesh.triangles.size()) cageTriMax_.assign(mesh.triangles.size(), -1);
    auto triMax = [&](uint32_t t) {
        if (cageTriMax_[t] >= 0) return int(cageTriMax_[t]);
        int worst = 0;
        if (mm.hasMask())
            worst = *std::max_element(mm.mask.begin() + std::ptrdiff_t(size_t(t) * mm.samples),
                                      mm.mask.begin() + std::ptrdiff_t(size_t(t + 1) * mm.samples));
        if (mesh.hasMask()) { // (the base mesh's own mask: a grid point is masked as much as its corners are, at most)
            const glm::uvec3 tr = mesh.triangles[t];
            worst = std::max(worst, int(std::max({mesh.maskAt(tr.x), mesh.maskAt(tr.y), mesh.maskAt(tr.z)}) * 255.0f + 0.5f));
        }
        cageTriMax_[t] = int16_t(worst);
        return worst;
    };
    // (serial: the triangles' maxima are shared between neighbouring vertices; a stroke's region is thousands of
    // base triangles of a few hundred points each)
    for (uint32_t v : verts) {
        if (cageFree_[v] >= 0.0f) continue;
        int worst = 0;
        auto ring = [&](uint32_t u) {
            for (uint32_t t : mesh.vertexTriangles(u)) worst = std::max(worst, triMax(t));
        };
        ring(v);
        for (uint32_t u : mesh.neighbors(v)) {
            if (worst == 255) break;
            ring(u);
        }
        cageFree_[v] = 1.0f - float(worst) / 255.0f;
    }
}

namespace {
template <class V> bool allFinite(const V& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
// View falloff reaches through the model as a cylinder; the brushes that pull to a plane leave what lies deeper along the
// view than the radius from the cursor (fading from 3/4 of it): a thin part (an ear, a horn) is still reached, the far
// side of a head or a sphere stays (gh#229: Clay pulled it through to the front's plane in shards).
float viewDepthFade(const glm::vec3& x, const glm::vec3& center, const glm::vec3& viewDir, float radius) {
    const float l = glm::length(viewDir);
    if (!(l > 1e-12f) || !(radius > 0.0f)) return 1.0f;
    const float depth = std::abs(glm::dot(x - center, viewDir)) / l;
    return std::clamp((1.0f - depth / radius) * 4.0f, 0.0f, 1.0f);
}
// The brushes that add, cut or smear the surface (Standard, Inflate, Crease, Pinch, Sharp Crease, Layer, Nudge) under View falloff leave what
// lies deeper along the view than 3 radii from the cursor (the far side of a head or a sphere, seen after orbiting; gh#229
// follow-up), full strength to 1.5 radii. A function of the place alone, so neighbouring points keep neighbouring weights, and
// a wide, smooth band: a gate on which way a vertex faces, or a steep one, tore underhangs (nose, lips, jaw over the neck) from
// the surface beside them, as these brushes move a vertex by about a radius.
float viewAdditiveFade(const glm::vec3& x, const glm::vec3& center, const glm::vec3& viewDir, float radius) {
    const float l = glm::length(viewDir);
    if (!(l > 1e-12f) || !(radius > 0.0f)) return 1.0f;
    const float t = std::clamp((std::abs(glm::dot(x - center, viewDir)) / l / radius - 1.5f) / 1.5f, 0.0f, 1.0f);
    return 1.0f - t * t * (3.0f - 2.0f * t);
}
} // namespace

void Sculptor::displaceAll(Mesh& mesh, std::span<const uint32_t> verts, std::span<const glm::vec3> d, bool influence,
                           const float* given) {
    const size_t n = verts.size();
    const bool cage = mesh.micro && (mesh.micro->hasMask() || mesh.hasMask()) && mesh.micro->baseTriangles == mesh.triangles.size();
    if (cage) cageFreeFor(mesh, verts);
    if (dynTopo_.active()) // the remesher's record keeps them (see touch); their bits then keep them out of ours
        for (uint32_t v : verts)
            if (markBit(touched_, v)) dynTopo_.recordStrokeStart(v);
    constexpr size_t kBlock = 4096;
    influence_.resize(n);
    firstTouch_.resize(n);
    blockFirst_.assign((n + kBlock - 1) / kBlock + 1, 0);
    // A non-finite offset (a NaN pressure, normal or tilt reaching a brush) drops its whole dab: one NaN vertex tears its
    // triangles into long shards across the view, and its undo and normals with it (gh#229). The app logs it.
    std::atomic<bool> bad{false};
    poolFor(n, [&](size_t b, size_t e) {
        uint32_t count = 0;
        bool finite = true;
        for (size_t i = b; i < e; ++i) {
            float k = given ? given[i] : influence ? vertexInfluence(mesh, verts[i]) : 1.0f;
            if (cage) k = std::min(k, cageFree_[verts[i]]); // (hidden faces stay: 0)
            influence_[i] = k;
            firstTouch_[i] = k > 0.0f && markBit(touched_, verts[i]);
            count += firstTouch_[i];
            finite = finite && allFinite(d[i]);
        }
        blockFirst_[b / kBlock + 1] = count;
        if (!finite) bad.store(true, std::memory_order_relaxed);
    }, kBlock);
    if (bad.load()) { // (the vertices first touched here go back to untouched: a later dab records their undo)
        ++nonFiniteDabs;
        for (size_t i = 0; i < n; ++i) // (rare: serial)
            if (firstTouch_[i]) touched_[verts[i] >> 6] &= ~(uint64_t(1) << (verts[i] & 63));
        return;
    }
    for (size_t i = 1; i < blockFirst_.size(); ++i) blockFirst_[i] += blockFirst_[i - 1];
    std::vector<glm::vec3>& target = mesh.layerTarget(layer_);
    UndoChannel<glm::vec3>& rec = record_.positions;
    const size_t base = rec.indices.size();
    rec.indices.resize(base + blockFirst_.back());
    rec.values.resize(base + blockFirst_.back());
    const bool layered = !mesh.layers.empty();
    const float weight = mesh.layerTargetWeight(layer_);
    poolFor(n, [&](size_t b, size_t e) {
        size_t r = base + blockFirst_[b / kBlock];
        for (size_t i = b; i < e; ++i) {
            const float k = influence_[i];
            if (k <= 0.0f) continue;
            const uint32_t v = verts[i];
            if (firstTouch_[i]) {
                rec.indices[r] = v;
                rec.values[r++] = target[v];
            }
            const glm::vec3 dk = d[i] * k;
            mesh.positions[v] += dk;
            if (layered) target[v] += dk / weight;
        }
    }, kBlock);
}

void Sculptor::flushLazy(Mesh& mesh, const BVH& bvh, std::span<const uint32_t> leaves) {
    if (leaves.empty()) return;
    const std::vector<uint32_t> keep = std::move(dabLeaves_);
    dabLeaves_.assign(leaves.begin(), leaves.end());
    const bool draft = draft_;
    draft_ = false; // (updateDabNormals in full)
    updateDabNormals(mesh, bvh);
    draft_ = draft;
    dabLeaves_ = keep;
}

void Sculptor::dropLazy() {
    for (size_t i = lazyHead_; i < lazyLeaves_.size(); ++i) lazyFlag_[lazyLeaves_[i]] = 0;
    for (uint32_t l : lastLazyLeaves_)
        if (l < lazyFlag_.size()) lazyFlag_[l] = 0;
    lazyLeaves_.clear();
    lazyHead_ = 0;
    lazyCount_ = 0;
    lastLazyLeaves_.clear();
}

void Sculptor::addLazyLeaves(const BVH& bvh, std::span<const uint32_t> leaves) {
    if (lazyFlag_.size() < bvh.nodeCount()) lazyFlag_.resize(bvh.nodeCount(), 0);
    for (uint32_t l : leaves) {
        if (lazyFlag_[l] == 0) {
            lazyLeaves_.push_back(l);
            ++lazyCount_;
        }
        lazyFlag_[l] = 2; // (moved by the batch being placed: catchUpBehind leaves it for now)
    }
}

// Takes up to `maxLeaves` pending leaves, oldest first, skipping (and keeping, at the back) those flagged 2 when
// `skipLast`.
static std::vector<uint32_t> takeLazy(std::vector<uint32_t>& list, size_t& head, std::vector<uint8_t>& flag, size_t& count,
                                      size_t maxLeaves, bool skipLast) {
    std::vector<uint32_t> out, kept;
    const size_t end = list.size();
    for (size_t i = head; i < end && out.size() < maxLeaves; ++i, ++head) {
        const uint32_t l = list[i];
        if (skipLast && flag[l] == 2) {
            kept.push_back(l);
            continue;
        }
        flag[l] = 0;
        out.push_back(l);
        --count;
    }
    list.insert(list.end(), kept.begin(), kept.end());
    if (head > 4096 && head * 2 > list.size()) { // (drop the taken front now and then)
        list.erase(list.begin(), list.begin() + std::ptrdiff_t(head));
        head = 0;
    }
    return out;
}

void Sculptor::catchUpNormals(Mesh& mesh, const BVH& bvh, size_t maxLeaves) {
    if (lazyCount_ == 0 || &mesh != lazyMesh_) return;
    const std::vector<uint32_t> leaves = takeLazy(lazyLeaves_, lazyHead_, lazyFlag_, lazyCount_, maxLeaves, false);
    flushLazy(mesh, bvh, leaves);
}

void Sculptor::catchUpBehind(Mesh& mesh, const BVH& bvh, double share) {
    if (lazyCount_ == 0 || &mesh != lazyMesh_) return;
    const size_t budget = size_t(double(lastLazyLeaves_.size()) * share);
    if (budget > 0) {
        const std::vector<uint32_t> leaves = takeLazy(lazyLeaves_, lazyHead_, lazyFlag_, lazyCount_, budget, true);
        flushLazy(mesh, bvh, leaves);
    }
    for (uint32_t l : lastLazyLeaves_) // (the next batch's own are what count as recent then)
        if (l < lazyFlag_.size() && lazyFlag_[l] == 2) lazyFlag_[l] = 1;
}

void Sculptor::flushDraft(Mesh& mesh, const BVH& bvh) {
    if (lazyCount_ > 0 && &mesh == lazyMesh_) {
        const std::vector<uint32_t> leaves = takeLazy(lazyLeaves_, lazyHead_, lazyFlag_, lazyCount_, SIZE_MAX, false);
        lastLazyLeaves_.clear();
        flushLazy(mesh, bvh, leaves);
    }
    if (!draft_ || draftLeaves_.empty()) return;
    std::sort(draftLeaves_.begin(), draftLeaves_.end());
    draftLeaves_.erase(std::unique(draftLeaves_.begin(), draftLeaves_.end()), draftLeaves_.end());
    dabLeaves_.swap(draftLeaves_);
    draft_ = false; // (updateDabNormals in full)
    updateDabNormals(mesh, bvh);
    draft_ = true;
    draftLeaves_.clear();
}

UndoRecord Sculptor::endStroke() {
    if (!pending_.empty() && pendingMesh_ && pendingBvh_) flushBatch(*pendingMesh_, *pendingBvh_); // (callers that didn't)
    pending_.clear();
    batching_ = false;
    if (lazyCount_ > 0 && lazyMesh_ && lazyBvh_) flushDraft(*lazyMesh_, *lazyBvh_); // (callers that didn't)
    dropLazy();
    lazyMesh_ = nullptr;
    lazyBvh_ = nullptr;
    if (dynTopo_.active()) {
        Mesh& mesh = *dynTopo_.strokeMesh();
        std::unique_ptr<TopologyUndo> topo = dynTopo_.finish(mesh, *dynTopo_.strokeBvh());
        if (topo) {
            mergeTopologyUndo(*topo, mesh);
            // The recorded bits of the slots its record kept (clearRecorded only knows this stroke's own lists).
            clearBits(touched_, topo->vertexSlots);
            clearBits(masked_, topo->vertexSlots);
            clearBits(colored_, topo->vertexSlots);
            record_.topology = std::move(topo);
        }
    }
    if (microBase_ == 1 && microMesh_) microRaiseBulge(*microMesh_); // (the base moved under it)
    if (micro_.active() && microMesh_) {
        record_.micro = micro_.end(*microMesh_);
        inkFinish(*microMesh_); // (Forge Ink: keeps the stroke editable, or bakes what it reached)
    } else if (inkMesh_ && inkMesh_->micro) {
        inkFinish(*inkMesh_); // (the stroke moved the base only: Move on the cage, Kelvinlet)
    }
    microMesh_ = nullptr;
    inkMesh_ = nullptr;
    clearRecorded();
    active_ = false;
    grabVerts_.clear();
    grabAll_.clear();
    grabStart_ = {}, grabStartTarget_ = {}, grabInfluence_ = {}, grabFirst_ = {}; // (a whole-head Move: ~100 MB)
    grabCaptured_ = false;
    return std::move(record_);
}

int groupBorderNeighbours(const Mesh& mesh, uint32_t v, uint32_t out[3]) {
    if (!mesh.hasFaceGroups()) return 0;
    int count = 0;
    const auto tris = mesh.vertexTriangles(v);
    for (uint32_t t : tris) {
        const glm::uvec3 tr = mesh.triangles[t];
        for (int k = 0; k < 3; ++k) {
            const uint32_t n = tr[k];
            if (n == v) continue;
            bool seen = false;
            for (int i = 0; i < count && !seen; ++i) seen = out[i] == n;
            if (seen) continue;
            // The edge v-n: a border when the triangles sharing it don't all have one group.
            const uint32_t g = mesh.facegroupAt(t);
            bool border = false;
            for (uint32_t u : tris) {
                if (u == t) continue;
                const glm::uvec3 o = mesh.triangles[u];
                if ((o.x == n || o.y == n || o.z == n) && mesh.facegroupAt(u) != g) border = true;
            }
            if (!border) continue;
            out[count++] = n;
            if (count == 3) return 3;
        }
    }
    return count;
}

void Sculptor::dab(Mesh& mesh, BVH& bvh, BrushType type, const DabParams& p, Symmetry sym) {
    if (!active_) return;
    if (mesh.micro) inkMesh_ = &mesh; // (Forge Ink: endStroke settles this mesh's kept strokes)
    // Morph: nothing without a target (Tool > Selection Sets > Morph Target); not on micro levels (no grid shapes).
    if (type == BrushType::Morph && (mesh.micro || !morphTarget(mesh))) return;
    if (type == BrushType::Cloth) { // ClothBrush.cpp (no remeshing: the simulation keeps its own topology)
        dabCloth(mesh, bvh, p, sym);
        return;
    }
    if (isKelvinletBrush(type)) { // Kelvinlet.cpp (no remeshing: they move the surface, as Move does)
        // On a micro level they move the base cage, which can't hold a mask finer than its triangles: held two rings out they
        // tear the surface at the mask's edge. Like the other sideways brushes (microBrushSupport 0) they leave a masked
        // micro level alone where the mask is.
        for (const SymCopy& m : copies(sym))
            if (kelvinNearMask(mesh, bvh, m.point(p.center), p.radius)) return;
        dabKelvinlet(mesh, bvh, type, p, sym);
        return;
    }
    // A micro level: a big dab moves the base below it (microBaseKernel), the grid takes the rest (core/MicroSculpt.cpp).
    if (microReady(mesh) && microBaseDab(mesh, type, p)) {
        if (!microBaseFuse) {
            microBaseKernel(mesh, bvh, type, p, sym);
            return;
        }
        // (the dabs left at the pen's lift: on through the plain path below, fused)
    } else if (microReady(mesh)) {
        const double k0 = micro_.findMs + micro_.kernelMs, s0 = micro_.stitchMs;
        const uint64_t pts = micro_.points;
        if (p.stencil.active()) inkOk_ = false; // (a Stencil is the view's: not kept)
        inkHeader(false, p.alpha, 0, 0.0f, 0.0f);
        for (const SymCopy& m : copies(sym)) {
            MicroDab d;
            d.type = type, d.center = m.point(p.center), d.viewDir = m.dir(p.viewDir), d.radius = p.radius;
            d.strength = p.strength * std::clamp(p.pressure, 0.0f, 1.0f), d.invert = p.invert, d.focal = p.focal;
            d.alphaUp = m.dir(p.alphaUp), d.strokeDir = m.dir(p.strokeDelta), d.copy = m, d.pxPerUnit = p.pxPerUnit;
            micro_.dab(mesh, bvh, type, d.center, d.viewDir, d.radius, d.strength, d.invert, d.focal, d.alphaUp, false, 0.0f, p.alpha,
                       stencilTexture(p, m), d.strokeDir, d.pxPerUnit);
            inkDab(d);
        }
        timings.kernelMs += micro_.findMs + micro_.kernelMs - k0;
        timings.normalsMs += micro_.stitchMs - s0;
        timings.vertices += micro_.points - pts;
        ++timings.dabs;
        return;
    }
    lastFusable_ = adaptiveDabs && isPlaneKernelBrush(type) && type != BrushType::Trim && !(p.alpha && !p.alpha->empty()) && !p.stencil.active() &&
                   !p.frontFacesOnly && !p.connectedOnly && !p.viewFalloff && !dyntopo.enabled;
    if (batching_ && batchable(mesh, type, p, sym)) { // a fused batch's (SculptBatch.cpp)
        pending_.push_back({type, p, sym});
        pendingMesh_ = &mesh;
        pendingBvh_ = &bvh;
        return;
    }
    if (!pending_.empty()) flushBatch(mesh, bvh);
    if (type != BrushType::Move && type != BrushType::Morph) { // (Morph needs the vertices the target has)
        const bool useAlpha = p.alpha && !p.alpha->empty(); // (the reach dabSingle gives the brush)
        const float reach = useAlpha ? kAlphaReach : type == BrushType::ClayBlock ? 1.3f : 1.0f;
        const float scale = type == BrushType::Smooth ? dyntopoSmoothScale(dyntopo) : 1.0f;
        if (scale > 0.0f) remeshDab(mesh, bvh, p, sym, scale, reach);
        else lastRemesh_ = false; // (Smooth only: no repair pass either)
    }
    dabVerts_.clear();
    dabLeaves_.clear();
    if (!batching_) lastLazyLeaves_.clear(); // (a batch's: catchUpBehind clears them after it)
    const std::vector<SymCopy> all = copies(sym);
    for (size_t ci = 0; ci < all.size(); ++ci) {
        const SymCopy& m = all[ci];
        frameWanted_ = m.identity(); // (the unmirrored pass records lastFrame)
        const DabParams mp = m.identity() ? p : transformed(p, m);
        // A copy that meets a later copy's brush hands it the moved surface's normals (the plane brushes' area plane
        // reads them): dabSingle carries them by the offset's derivative, as an adaptive dab does, and the exact ones
        // follow below with every copy's.
        earlyNormals_ = false;
        if (ci + 1 < all.size() && isPlaneKernelBrush(type))
            for (size_t cj = ci + 1; cj < all.size() && !earlyNormals_; ++cj)
                earlyNormals_ = glm::distance(mp.center, all[cj].point(p.center)) < 2.0f * p.radius;
        if (type == BrushType::Vdm) dabVdmSingle(mesh, bvh, mp, m.handedness());
        else dabSingle(mesh, bvh, type, mp);
        earlyNormals_ = false;
    }
    if (!dabVerts_.empty() || !dabLeaves_.empty() || lastLazyLeaves_.empty()) updateDabNormals(mesh, bvh);
    if (type != BrushType::Move) repairDab(mesh, bvh);
}

// Inflate along each vertex's own normal (#194). That swells a form, but where Crease or Pinch cut a groove the grid across
// its line is packed to a tenth of its spacing, and the packed vertices' normals lean toward each other, noisily: the
// steps close the gap within a dab or two, the triangles between them flip, and stacked strokes tear needles and stretched
// streaks into the surface. So a dab that reaches a packed grid (at least 32 vertices, and 0.2% of the dab's, whose shortest
// edge is under 2/3 of the dab's typical shortest one; a lone odd vertex on a remeshed surface, or the one ring of a tube's round cap, is not one) moves all its vertices
// along the brush's one area normal instead, which carries the whole groove alike; every other dab, the usual Inflate, is
// unchanged. One choice for the whole dab, not a per-vertex mix: neighbours on different rules were what tore.
//
// The edge length an unpacked vertex of `verts` has, as its shortest edge (an even sample of 64: the upper quartile of
// theirs, so a packed minority doesn't drag it down, and a grid that is long one way, the rings of a coarse tube, is
// no packed grid: every vertex in it has the same short edge).
static float typicalEdge(const Mesh& mesh, std::span<const uint32_t> verts) {
    const size_t stride = std::max<size_t>(1, verts.size() / 64);
    std::vector<float> shortest;
    for (size_t i = 0; i < verts.size(); i += stride) {
        float s = std::numeric_limits<float>::max();
        for (uint32_t u : mesh.polygonNeighbors(verts[i])) s = std::min(s, glm::distance(mesh.positions[u], mesh.positions[verts[i]]));
        if (s < std::numeric_limits<float>::max()) shortest.push_back(s);
    }
    if (shortest.empty()) return 0.0f;
    const auto q = shortest.begin() + std::ptrdiff_t(shortest.size() * 3 / 4);
    std::nth_element(shortest.begin(), q, shortest.end());
    return *q;
}
// 1 where v's shortest edge is at least 90% of `typical`, 0 at 65%.
static float unpackedShare(const Mesh& mesh, uint32_t v, float typical) {
    if (typical <= 0.0f) return 1.0f;
    float shortest = std::numeric_limits<float>::max();
    for (uint32_t u : mesh.polygonNeighbors(v)) shortest = std::min(shortest, glm::distance(mesh.positions[u], mesh.positions[v]));
    return std::clamp((shortest / typical - 0.65f) / 0.25f, 0.0f, 1.0f);
}

// Trim on a thin wall (members session 2026-11, phoenix: "Trim tears holes in thin walls"). A Trim dab cuts the skin the
// viewer sees down to its plane; it never reaches the wall behind it. Two things went wrong on a shell or a thin wing:
// the far wall, inside the brush's ball, fed the plane (the area normal and centre averaged both walls, so the plane
// ran through the middle of the wall or faced backwards) and took the cut itself, and dab after dab the skin sank past
// the far wall (no hole is left to see until it has crossed). Now only what faces the cut (the skin; the far wall and
// an underside face away and keep still, over a short ramp so no edge forms where the facing turns) is trimmed, and
// each vertex stops `kTrimKeep` of the brush radius short of the first surface below it along the cut: a wall thinner
// than that is left as it is. Only where the brush region is a thin sheet (trimRegionIsSheet): a solid form with pockets, a
// head with its nostrils and lips, is trimmed flat as ever. The BVH is the one the previous dab refit, so the walls are where they were.
static constexpr float kTrimKeep = 0.02f; // of the brush radius (Trim's own plane stands this far below the surface at full strength)
// `normals[i]`: verts[i]'s normal, read before the dab (the adaptive path rewrites the normals as it goes).
// The wall below a vertex is found along its own normal (a sheet's rim, whose edge runs along the wall, still finds the
// underside), and the room is the distance to that wall's plane along the cut.
static void keepTrimWalls(const Mesh& mesh, const BVH& bvh, const glm::vec3& areaNormal, float r, std::span<const uint32_t> verts,
                          std::span<const glm::vec3> normals, std::vector<glm::vec3>& d) {
    const float keep = kTrimKeep * r, eps = 1e-3f * r, shift = 2e-3f * r;
    poolFor(verts.size(), [&](size_t b, size_t e) {
        RayHit hit;
        for (size_t i = b; i < e; ++i) {
            const float len = glm::length(d[i]);
            if (len <= 0.0f) continue;
            const uint32_t v = verts[i];
            const glm::vec3 n = normals[i];
            const float w = std::clamp((glm::dot(n, areaNormal) + 0.1f) / 0.15f, 0.0f, 1.0f);
            if (w <= 0.0f) {
                d[i] = glm::vec3(0.0f);
                continue;
            }
            float allowed = len * w;
            // (a shell made by offsetting a surface has its vertices straight below its skin's: the ray starts a hair to either side,
            // so it lands inside a triangle, not on the shared vertex where a miss is possible; a sheet's rim, which a ray
            // to one side misses, is found from the other)
            glm::vec3 side = glm::cross(n, glm::vec3(0.267f, 0.534f, 0.802f));
            const float sideLen = glm::length(side);
            side = sideLen > 1e-6f ? side * (shift / sideLen) : glm::vec3(0.0f);
            for (const float sgn : {1.0f, -1.0f}) {
                if (!bvh.raycast(mesh, mesh.positions[v] - n * eps + side * sgn, -n, hit)) continue;
                // (only a wall the cut runs into, facing away down the cut: not a ridge's side, nor another surface's top)
                const float across = glm::dot(-areaNormal, hit.normal);
                if (across <= 0.3f) continue;
                const float room = glm::dot(hit.position - mesh.positions[v], hit.normal) / across;
                allowed = std::min(allowed, std::max(0.0f, room - keep));
            }
            d[i] *= allowed / len;
        }
    }, 256);
}

// Is the region under a Trim dab a thin sheet (a shell, a wing, a cloth-thin Part): most of what faces the viewer has a wall
// behind it, parallel to it, within `kTrimSheet` of the brush radius? Sampled, a few rays. A solid form with pockets (the
// nostrils and lips of a head) is not: Trim there stays what it was, a plane that takes everything above it.
static constexpr float kTrimSheet = 1.5f; // (a wing 0.12 thick at its middle under a brush of radius 0.1 is a sheet)
static bool trimRegionIsSheet(const Mesh& mesh, const BVH& bvh, std::span<const uint32_t> verts, const glm::vec3& center,
                              const glm::vec3& viewDir, float r) {
    constexpr size_t kSamples = 400; // (half of a thin Part's region is its far side, and the pen's disc is a part of the rest: ~60 rays)
    if (mesh.normals.size() != mesh.vertexCount() || verts.size() < 8) return false;
    const size_t stride = std::max<size_t>(1, verts.size() / kSamples);
    const float eps = 1e-3f * r;
    size_t seen = 0, thin = 0;
    RayHit hit;
    for (size_t i = 0; i < verts.size(); i += stride) {
        const uint32_t v = verts[i];
        const glm::vec3 n = mesh.normals[v];
        if (glm::dot(n, viewDir) >= 0.0f || glm::distance(mesh.positions[v], center) > 0.7f * r) continue; // (the visible skin near the pen)
        ++seen;
        glm::vec3 side = glm::cross(n, glm::vec3(0.267f, 0.534f, 0.802f));
        const float sideLen = glm::length(side);
        if (sideLen > 1e-6f) side *= 2e-3f * r / sideLen;
        for (const float sgn : {1.0f, -1.0f}) {
            if (!bvh.raycast(mesh, mesh.positions[v] - n * eps + side * sgn, -n, hit)) continue;
            if (glm::dot(hit.normal, -n) > 0.7f && hit.t + eps < kTrimSheet * r) {
                ++thin;
                break;
            }
        }
    }
    return seen >= 8 && 2 * thin > seen;
}

void Sculptor::dabSingle(Mesh& mesh, BVH& bvh, BrushType type, const DabParams& p) {
    auto clock = std::chrono::steady_clock::now();
    // Alphas and the square Clay Block tip reach into the corners of their square.
    const bool useAlpha = p.alpha && !p.alpha->empty();
    const bool useStencil = p.stencil.active();
    const float reach = useAlpha ? kAlphaReach : type == BrushType::ClayBlock ? 1.3f : 1.0f;
    brushRegion(mesh, bvh, p.center, p.viewDir, p.radius * reach, p.connectedOnly, p.viewFalloff, verts_, connected_,
                useAlpha && p.stampFootprint);
    if (verts_.empty()) return;
    StencilView stencil = p.stencil; // (averaged over the vertices' spacing where they are many pixels apart)
    if (useStencil) stencil.setSpacing(p.center, p.viewDir, p.radius * reach, verts_.size());

    auto& pos = mesh.positions;
    const auto& nrm = mesh.normals;

    if (p.frontFacesOnly) {
        std::erase_if(verts_, [&](uint32_t v) { return glm::dot(nrm[v], p.viewDir) > 0.0f; });
        if (verts_.empty()) return;
    }

    // Trim on a thin sheet: the wall behind the skin is protected (keepTrimWalls); on anything else it is the plane it always was.
    const bool trimSheet = type == BrushType::Trim && !p.invert && trimRegionIsSheet(mesh, bvh, verts_, p.center, p.viewDir, p.radius);
    // Scale-adaptive dabs (Sculpt.h, adaptiveDabs): a big region's plane from an even sample of its vertices.
    adaptiveDab_ = (adaptiveDabs || analyticNormals) && !dynTopo_.active() && !draft_ && verts_.size() >= kAdaptiveMinVertices &&
                   type != BrushType::Smooth && type != BrushType::SmoothPeaks && type != BrushType::Layer && type != BrushType::Morph &&
                   // (an alpha's fine pattern or Clay Block's square tip, which turn with the area normal, and the brushes that move along each vertex's own normal amplify a
                   // hair's difference in the normals the next dabs read)
                   type != BrushType::Nudge && type != BrushType::Inflate && type != BrushType::ClayBlock && !useAlpha &&
                   !trimSheet && // (its wall clamp, keepTrimWalls, is a ray per vertex: the carried normals can't follow it)
                   !p.stencil.active() && // (the stencil's detail, as an alpha's)
                   mesh.normals.size() == mesh.vertexCount();
    if (adaptiveDab_ && (mesh.mask.size() == mesh.vertexCount() || mesh.hasHidden())) {
        // A mask's edge is a step the adaptive normals don't see (they follow the brush, not the mask): regions
        // with any of it take the exact path.
        std::atomic<bool> masked{false};
        poolFor(verts_.size(), [&](size_t b, size_t e) {
            for (size_t i = b; i < e && !masked.load(std::memory_order_relaxed); ++i)
                if (vertexInfluence(mesh, verts_[i]) < 1.0f) masked.store(true, std::memory_order_relaxed);
        }, 65536);
        adaptiveDab_ = !masked.load();
    }
    // The normals a later mirror copy of this dab reads, carried now (earlyNormals_); not where the exact path's rings
    // matter (masks, an alpha, dynamic topology, the draft) or the brush isn't a plane brush.
    const bool analyticEarly = earlyNormals_ && !adaptiveDab_ && isPlaneKernelBrush(type) && !trimSheet && !p.viewFalloff && !useAlpha && !useStencil &&
                               !dynTopo_.active() && !draft_ && mesh.normals.size() == mesh.vertexCount() &&
                               mesh.mask.size() != mesh.vertexCount() && !mesh.hasHidden();
    earlyNormals_ = false;
    const size_t stride = adaptiveDab_ ? std::max<size_t>(1, verts_.size() / kAreaSamples) : 1;

    // Weighted area normal and centre of the brush region. View falloff reaches through the model as a cylinder, but the
    // plane is the surface's under the cursor (the ball around it): across the cylinder it was the mean of the front
    // and the back, inside the model, and Clay / Clay Block pulled the far side through to it in shards (gh#229).
    bool skinOnly = trimSheet; // (Trim on a thin sheet: the plane is the skin's, not the mean of its two walls)
    const auto sumsWithin = [&](bool ball) {
        return areaSums((verts_.size() + stride - 1) / stride, [&](size_t i, AreaSums& acc) {
            const uint32_t v = verts_[i * stride];
            float w = brushFalloff(ball ? glm::distance(pos[v], p.center) : brushDistance(p, pos[v]), p.radius, p.focal);
            if (skinOnly && glm::dot(nrm[v], p.viewDir) > 0.0f) w = 0.0f; // (the far wall of a thin Part, inside the ball)
            acc.normal += glm::dvec3(nrm[v]) * double(w);
            acc.center += glm::dvec3(pos[v]) * double(w);
            acc.weight += w;
        });
    };
    AreaSums sums = sumsWithin(p.viewFalloff);
    if (p.viewFalloff && sums.weight <= 0.0) sums = sumsWithin(false); // (no vertex near the cursor: big faces)
    if (skinOnly && sums.weight <= 0.0) { // (nothing faces the viewer here: the whole region, as before)
        skinOnly = false;
        sums = sumsWithin(p.viewFalloff);
        if (p.viewFalloff && sums.weight <= 0.0) sums = sumsWithin(false);
    }
    glm::vec3 areaNormal = glm::vec3(sums.normal), areaCenter = glm::vec3(sums.center);
    const float wsum = float(sums.weight);
    if (wsum <= 0.0f) return;
    if (!std::isfinite(sums.weight) || !allFinite(sums.normal) || !allFinite(sums.center)) { // (a NaN normal or position: gh#229)
        ++nonFiniteDabs;
        return;
    }
    areaCenter /= wsum;
    float nlen = glm::length(areaNormal);
    areaNormal = nlen > 1e-12f ? areaNormal / nlen : -p.viewDir;
    if (frameWanted_) {
        lastFrame = {p.center, areaNormal, areaCenter, true};
        frameWanted_ = false;
        lastRegion_ = verts_.size();
        lastRegionRadius_ = p.radius;
    }

    const float s = std::clamp(p.strength, 0.0f, 1.0f) * std::clamp(p.pressure, 0.0f, 1.0f);
    const float sign = p.invert ? -1.0f : 1.0f;
    const float r = p.radius;

    // Brush-local frame on the area plane (alpha orientation, square tips).
    glm::vec3 up = projectOnPlane(p.alphaUp, areaNormal);
    if (glm::length(up) < 1e-6f)
        up = projectOnPlane(std::abs(areaNormal.x) < 0.9f ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0), areaNormal);
    up = glm::normalize(up);
    const glm::vec3 right = glm::cross(up, areaNormal);
    const float strokeLen = glm::length(p.strokeDelta);
    const glm::vec3 strokeDir = strokeLen > 1e-12f ? p.strokeDelta / strokeLen : glm::vec3(0.0f);
    if (type == BrushType::Layer) prepareLayer(mesh, r * 0.08f * std::clamp(p.strength, 0.0f, 1.0f));
    const bool keepVolume = type == BrushType::Smooth && p.keepVolume && !p.groupBorders;
    if (keepVolume) taubinShrink(mesh);
    const std::vector<glm::vec3>* morph = type == BrushType::Morph ? morphTarget(mesh) : nullptr;
    if (type == BrushType::Morph && !morph) return;

    PlaneKernel plane;
    plane.type = type;
    plane.center = p.center;
    plane.viewDir = p.viewDir;
    plane.areaNormal = areaNormal;
    plane.areaCenter = areaCenter;
    plane.strokeDir = strokeDir;
    plane.r = r;
    plane.s = s;
    plane.sign = sign;
    plane.focal = p.focal;
    plane.invert = p.invert;
    plane.viewFalloff = p.viewFalloff;

    // Inflate swells along each vertex's own normal, unless the dab reaches a groove or a packed grid (see inflateAlongNormals).
    bool inflateAlongNormals = true;
    if (type == BrushType::Inflate) {
        const float typical = typicalEdge(mesh, verts_);
        // (an even sample of at most 4096 vertices: a packed cluster of 0.2% of a big dab still shows in it)
        const size_t stride = std::max<size_t>(1, verts_.size() / 4096), sampled = (verts_.size() + stride - 1) / stride;
        std::atomic<size_t> bad{0};
        poolFor(sampled, [&](size_t b, size_t e) {
            size_t n = 0;
            for (size_t i = b; i < e; ++i)
                if (unpackedShare(mesh, verts_[i * stride], typical) < 0.1f) ++n;
            bad.fetch_add(n, std::memory_order_relaxed);
        }, 512);
        inflateAlongNormals = bad.load() * stride < std::max<size_t>(32, verts_.size() / 500);
    }
    std::vector<glm::vec3> trimNormals;
    if (trimSheet) {
        trimNormals.resize(verts_.size());
        for (size_t i = 0; i < verts_.size(); ++i) trimNormals[i] = nrm[verts_[i]];
    }
    scratch_.resize(verts_.size());
    // Each vertex's offset depends only on the positions before the dab (and, for Layer, on its own state). The
    // offset of the i-th vertex v if it were at x (x = pos[v] for the dab itself; the adaptive normals ask for
    // points next to it, which only the brushes that read nothing but the place, and v's own normal, are asked).
    // The brushes that pull to their plane, under View falloff: only what lies within the radius of the plane and of the
    // cursor along the view (viewDepthFade): the cylinder's far side stays where it is (gh#229).
    const bool planeReach = p.viewFalloff && (type == BrushType::Clay || type == BrushType::ClayBlock ||
                                              type == BrushType::Flatten || type == BrushType::Trim);
    const bool depthReach = p.viewFalloff && (type == BrushType::Standard || type == BrushType::Inflate || type == BrushType::Crease ||
                                               type == BrushType::Pinch || type == BrushType::SharpCrease || type == BrushType::Layer ||
                                               type == BrushType::Nudge); // (Nudge smears the surface; it is not a grab)
    // Tendril under View falloff: the far side of a thick part stays put, so a pull does not drag front and back into a sheet.
    const bool tendrilReach = p.viewFalloff && type == BrushType::SnakeHook;
    // With Dynamic on, the horn's pinch stops at a radius its triangles can still round (about 2 edges; at 0 it thinned to a
    // thread under the remesher's edge length and folded into a flat sheet): the horn tapers, never to less than a ring of vertices.
    if (type == BrushType::SnakeHook && dyntopo.enabled && dyntopo.constantDetail && strokeDiagonal_ <= 0.0f)
        strokeDiagonal_ = glm::length(mesh.bounds().extent());
    const float tendrilRadius = type == BrushType::SnakeHook && dyntopo.enabled && !topologyHeld_
                                    ? kTendrilRingEdges * dyntopoEdgeLength(dyntopo, p.radius, strokeDiagonal_)
                                    : 0.0f;
    const auto offset = [&](size_t i, uint32_t v, const glm::vec3& x) -> glm::vec3 {
        float reachFade = 1.0f;
        if (depthReach || tendrilReach) {
            reachFade = viewAdditiveFade(x, p.center, p.viewDir, tendrilReach ? kTendrilDepth * r : r);
            if (reachFade <= 0.0f) return glm::vec3(0.0f);
        }
        if (planeReach) {
            reachFade = std::clamp((1.0f - std::abs(glm::dot(x - areaCenter, areaNormal)) / r) * 4.0f, 0.0f, 1.0f) *
                        viewDepthFade(x, p.center, p.viewDir, r);
            if (reachFade <= 0.0f) return glm::vec3(0.0f);
        }
        const glm::vec3 o = x - p.center;
        float f = useAlpha ? p.alpha->stamp(glm::dot(o, right) / r, glm::dot(o, up) / r, p.focal)
                           : brushFalloff(brushDistance(p, x), r, p.focal);
        if (useStencil && f > 0.0f) f *= stencil.weight(x);
        glm::vec3 d(0.0f);

        switch (type) {
        case BrushType::Standard: case BrushType::Flatten: case BrushType::Clay: case BrushType::Crease:
        case BrushType::Pinch: case BrushType::SharpCrease: case BrushType::Trim:
            d = plane.offsetWith(x, f); // (core/SculptKernel.h)
            break;
        case BrushType::Inflate:
            d = (inflateAlongNormals ? nrm[v] : areaNormal) * (sign * s * r * 0.05f * f);
            break;
        case BrushType::Smooth: case BrushType::SmoothPeaks: {
            if (p.groupBorders && type == BrushType::Smooth) { // along the border, on the tangent plane; junctions and inner vertices stay
                uint32_t nb[3];
                if (groupBorderNeighbours(mesh, v, nb) != 2) break;
                const glm::vec3 mid = 0.5f * (pos[nb[0]] + pos[nb[1]]);
                d = projectOnPlane(mid - x, nrm[v]) * std::min(1.0f, s * f);
                break;
            }
            auto ring = mesh.polygonNeighbors(v);
            if (ring.empty()) break;
            glm::vec3 avg(0.0f);
            if (keepVolume) {
                // The inflate half of the Taubin step, from the shrunk positions (neighbours outside the brush
                // didn't shrink).
                for (uint32_t n : ring) {
                    const auto at = std::lower_bound(shrunkIndex_.begin(), shrunkIndex_.end(), std::make_pair(n, 0u));
                    avg += at != shrunkIndex_.end() && at->first == n ? shrunk_[at->second] : pos[n];
                }
                avg /= static_cast<float>(ring.size());
                const glm::vec3 q = shrunk_[i];
                d = (q + kTaubinMu * (avg - q) - x) * std::min(1.0f, s * f);
                break;
            }
            for (uint32_t n : ring) avg += pos[n];
            avg /= static_cast<float>(ring.size());
            d = (avg - x) * std::min(1.0f, s * f);
            if (type == BrushType::SmoothPeaks) { // only down, along the normal: what sits above the average plane
                const float up = glm::dot(avg - x, nrm[v]);
                d = up < 0.0f ? nrm[v] * (up * std::min(1.0f, s * f)) : glm::vec3(0.0f);
            }
            break;
        }
        case BrushType::ClayBlock: {
            // Square tip (rounded corners) with a flat top: a raised plane that
            // everything below is pulled up to, so strokes build up in slabs.
            float w = f;
            if (!useAlpha) {
                const float cheb = std::max(std::abs(glm::dot(o, right)), std::abs(glm::dot(o, up))) / r;
                const float corner = std::clamp((1.3f - glm::length(o) / r) / 0.3f, 0.0f, 1.0f);
                const float edge = std::clamp((1.0f - cheb) / 0.35f, 0.0f, 1.0f);
                w = edge * edge * (3.0f - 2.0f * edge) * corner;
                // Falloff (#60) keeps this tip at 0: below, harder edges and a broader slab; above, it fades
                // toward a soft round point, as an alpha does.
                if (p.focal < 0.0f) w = std::pow(w, std::exp2(2.0f * std::max(p.focal, -1.0f)));
                else if (p.focal > 0.0f) {
                    const float k = std::min(p.focal, 1.0f);
                    w *= 1.0f - k + k * brushFalloff(glm::length(o), r * 1.3f, k);
                }
            }
            glm::vec3 planePoint = areaCenter + areaNormal * (sign * r * 0.08f * s);
            float h = glm::dot(x - planePoint, areaNormal);
            if (sign * h < 0.0f) d = -areaNormal * (h * w * clayPull(s));
            break;
        }
        case BrushType::SnakeHook: {
            // Drag the region along with the stroke; the sides pinch in, so pulled shapes taper.
            d = p.strokeDelta * f;
            glm::vec3 side = p.center - x;
            side -= strokeDir * glm::dot(side, strokeDir);
            if (tendrilRadius > 0.0f) { // (the pinch closes the gap down to tendrilRadius, no further)
                const float len = glm::length(side);
                side = len > tendrilRadius ? side * (1.0f - tendrilRadius / len) : glm::vec3(0.0f);
            }
            d += side * (0.1f * s * f * std::min(1.0f, strokeLen / (0.1f * r)));
            break;
        }
        case BrushType::Layer: {
            // Each vertex rises to the stroke's height x falloff once; later dabs
            // only add what is missing, so overlaps within a stroke never stack.
            if (layerStamp_[v] != layerEpoch_) {
                layerStamp_[v] = layerEpoch_;
                layerDone_[v] = 0.0f;
                layerDir_[v] = areaNormal;
            }
            // (View falloff's far-side fade is part of the height, not a factor on the step: a vertex that stood in the fade early
            // in the stroke would be marked done at its full height and never catch up, and neighbours would end at uneven heights.)
            const float target = sign * layerHeight_ * std::clamp(p.pressure, 0.0f, 1.0f) * f * reachFade;
            if (std::abs(target) > std::abs(layerDone_[v])) {
                d = layerDir_[v] * (target - layerDone_[v]);
                layerDone_[v] = target;
            }
            reachFade = 1.0f;
            break;
        }
        case BrushType::Nudge:
            // Slide the surface sideways along the stroke without lifting it.
            d = projectOnPlane(p.strokeDelta, areaNormal) * (f * std::min(1.0f, 0.4f + s));
            break;
        case BrushType::Morph:
            // Toward the morph target (away from it, by a quarter as much, with Alt).
            d = ((*morph)[v] - x) * (p.invert ? -0.25f * s * f : std::min(1.0f, s * f));
            break;
        default:
            break;
        }
        return d * reachFade;
    };
    if (!adaptiveDab_ && !analyticEarly) {
        poolFor(verts_.size(), [&](size_t begin, size_t end) {
            for (size_t i = begin; i < end; ++i) scratch_[i] = offset(i, verts_[i], pos[verts_[i]]);
        });
    } else {
        // The new normal from the displaced tangents: t + (offset(x + h t) - offset(x)) / h along two directions
        // on the surface (first order in the brush's slope, exact for a plane); the cavity term each vertex had
        // before the stroke moved it is kept for the GPU until the exact one catches up.
        if (curvature_.size() != mesh.vertexCount() || curvatureMesh_ != &mesh) {
            curvature_.assign(mesh.vertexCount(), 0.0f);
            curvatureFilled_ = false;
            curvatureMesh_ = &mesh;
        }
        if (curvatureStamp_.size() != mesh.vertexCount()) curvatureStamp_.assign(mesh.vertexCount(), 0);
        const uint32_t stamp = uint32_t(strokeId_);
        const float h = r * 1e-3f;
        // The plane brushes carry the normals by their offset's derivative (core/SculptKernel.h); the others by
        // finite differences.
        const bool analytic = isPlaneKernelBrush(type) && !p.viewFalloff;
        const float kexp = std::exp2(2.0f * std::clamp(p.focal, -1.0f, 1.0f));
        std::vector<glm::vec3>& normals = mesh.normals;
        poolFor(verts_.size(), [&](size_t begin, size_t end) {
            for (size_t i = begin; i < end; ++i) {
                const uint32_t v = verts_[i];
                const glm::vec3 x = pos[v];
                if (!analyticEarly && curvatureStamp_[v] != stamp) { // (an early dab's exact normals and cavity terms follow)
                    curvatureStamp_[v] = stamp;
                    curvature_[v] = kKeepCurvature; // (the GPU keeps its cavity term: no ring walk here)
                }
                const glm::vec3 n = normals[v];
                if (analytic) { // (no mask here: such regions take the exact path)
                    normals[v] = plane.offsetAndNormal(x, n, kexp, scratch_[i]);
                    continue;
                }
                const glm::vec3 d = offset(i, v, x);
                scratch_[i] = d;
                const float k = vertexInfluence(mesh, v);
                if (k <= 0.0f) continue;
                const glm::vec3 t1 = glm::normalize(std::abs(n.x) < 0.9f ? glm::cross(n, glm::vec3(1, 0, 0))
                                                                         : glm::cross(n, glm::vec3(0, 1, 0)));
                const glm::vec3 t2 = glm::cross(n, t1);
                const glm::vec3 a = t1 + (offset(i, v, x + t1 * h) - d) * (k / h);
                const glm::vec3 b = t2 + (offset(i, v, x + t2 * h) - d) * (k / h);
                const glm::vec3 m = glm::cross(a, b);
                const float len = glm::length(m);
                if (len > 1e-20f && glm::dot(m, n) > 0.0f) normals[v] = m / len;
            }
        });
    }

    if (trimSheet) keepTrimWalls(mesh, bvh, areaNormal, r, verts_, trimNormals, scratch_);
    // (Tendril is left as it was: the app pulls its tip under the cursor in many tiny steps, and a pulled horn folds into its own
    // pinch, smooth to the eye; relaxing it there made a crossing fold where it did not.)
    if (type == BrushType::Nudge && verts_.size() <= kLimitVerts) limitFolds(mesh, scratch_);
    displaceAll(mesh, verts_, scratch_);
    if (type == BrushType::Nudge) {
        std::vector<float> w(verts_.size());
        for (size_t i = 0; i < w.size(); ++i) w[i] = brushFalloff(brushDistance(p, pos[verts_[i]]), p.radius, p.focal);
        relaxDrag(mesh, w, kRelaxLambda * std::min(1.0f, strokeLen / (kRelaxStep * r)), kRelaxRounds);
    }
    finishDab(mesh, bvh, clock);
}

// A drag (Nudge, a recipe's Drag; Tendril too, but it is left alone, see dabSingle) pushes the surface ahead of the stroke
// together: the triangles there shrink dab after dab into slivers, and the next dab's falloff, whose gradient a sliver cannot
// follow, swings their normals about and turns them over (#198: a second stroke across a dragged area; #199: Nudge). Two
// things keep that from happening:
//  * relaxDrag spreads the bunched material out again after every dab, so slivers do not build up;
//  * limitFolds, for Nudge with a brush of up to kLimitVerts vertices (it costs a few looks at every vertex of the region a
//    dab: too much for a bigger brush, and for Palette Knife, which is only relaxed): checks each dab's offsets against the
//    triangles they move. One that would turn too far from the way it faces (14 degrees for a sliver, 41 for a sound
//    triangle, which a plane brush tilts a whole falloff ring by) or end under a quarter (a fiftieth) of its area has its
//    corners' offsets scaled back until it does not. A scaled corner moves less than its neighbours, which can squeeze
//    theirs in turn, so the check follows the limited vertices outward until the squeeze is gentle. A stroke that folds
//    nothing is never touched.
constexpr float kFoldKeepSliver = 0.25f, kFoldKeepSound = 0.02f; // (the area a dab leaves a triangle)
constexpr float kFoldCosSliver = 0.97f, kFoldCosSound = 0.75f;   // (14 and 41 degrees)
constexpr float kFoldSliver = 0.2f, kFoldSound = 0.6f;         // (triangle quality: 1 is equilateral)
constexpr int kFoldPasses = 200;

void Sculptor::limitFolds(const Mesh& mesh, std::vector<glm::vec3>& d) {
    if (dynTopo_.active() || mesh.triangles.empty() || verts_.empty()) return; // (the remesher repairs its own squashed triangles)
    // (In double: a sliver's shape is the difference of nearly equal positions, and single precision noise in it would make
    // the two halves of a mirrored stroke come out differently.)
    using dvec3 = glm::dvec3;
    const auto& pos = mesh.positions;
    const size_t n = verts_.size();
    if (foldSlot_.size() != mesh.vertexCount()) foldSlot_.assign(mesh.vertexCount(), ~0u);
    for (size_t i = 0; i < n; ++i) foldSlot_[verts_[i]] = uint32_t(i);
    foldScale_.resize(n);
    foldD_.assign(d.begin(), d.begin() + std::ptrdiff_t(n));
    const auto qualityOf = [](const dvec3& e1, const dvec3& e2, double area) { // (1 is equilateral)
        return 2.0 * 1.7320508075688772 * area / std::max(glm::dot(e1, e1) + glm::dot(e2, e2) + glm::dot(e2 - e1, e2 - e1), 1e-40);
    };
    const auto soften = [](double quality) { // (0 for a sliver, 1 for a sound triangle)
        const double t = std::clamp((quality - kFoldSliver) / (kFoldSound - kFoldSliver), 0.0, 1.0);
        return t * t * (3.0 - 2.0 * t);
    };
    // The share of its offset each vertex may keep so that none of its triangles is turned or squashed, the others' offsets as they are.
    const auto allowed = [&](size_t i) {
        double keep = 1.0;
        for (uint32_t t : mesh.vertexTriangles(verts_[i])) {
            const glm::uvec3 tri = mesh.triangles[t];
            const uint32_t c[3] = {tri.x, tri.y, tri.z};
            dvec3 dd[3];
            bool any = false;
            for (int k = 0; k < 3; ++k) {
                const uint32_t slot = foldSlot_[c[k]];
                dd[k] = slot == ~0u ? dvec3(0.0) : dvec3(foldD_[slot]);
                any = any || dd[k] != dvec3(0.0);
            }
            if (!any) continue;
            const dvec3 e1 = dvec3(pos[c[1]]) - dvec3(pos[c[0]]), e2 = dvec3(pos[c[2]]) - dvec3(pos[c[0]]);
            const dvec3 d1 = dd[1] - dd[0], d2 = dd[2] - dd[0];
            const dvec3 a0 = glm::cross(e1, e2);
            const double area = glm::length(a0);
            if (area < 1e-24) continue;
            const dvec3 nrm0 = a0 / area;
            // How far its normal may turn in a dab, and how much of its area it must keep, depend on its shape: a sound
            // triangle (quality 1 is equilateral) can be tilted a good way (a plane brush tilts a whole falloff ring) and
            // squeezed hard, a sliver's normal swings for a tiny move.
            const double soft = soften(qualityOf(e1, e2, area));
            const double cosLimit = glm::mix(double(kFoldCosSliver), double(kFoldCosSound), soft);
            const double keepArea = glm::mix(double(kFoldKeepSliver), double(kFoldKeepSound), soft);
            const auto fine = [&](double tau) {
                const dvec3 a = glm::cross(e1 + d1 * tau, e2 + d2 * tau);
                const double up = glm::dot(a, nrm0);
                return up >= cosLimit * glm::length(a) && up >= keepArea * area;
            };
            if (fine(1.0)) continue;
            double lo = 0.0, hi = 1.0;
            for (int q = 1; q <= 4; ++q) {
                if (fine(0.25 * q)) lo = 0.25 * q;
                else { hi = 0.25 * q; break; }
            }
            for (int q = 0; q < 14; ++q) {
                const double mid = 0.5 * (lo + hi);
                (fine(mid) ? lo : hi) = mid;
            }
            keep = std::min(keep, lo);
        }
        return float(keep);
    };
    // One quick look at every triangle of the region (in float, each once) finds the few the move could hurt: those that would
    // turn more than 14 degrees from the way they face or lose three quarters of their area, the strictest of the limits
    // below. Their corners are all pass 0 looks at (exactly, in double); later passes follow the ones it scaled back.
    std::vector<uint32_t> work;
    {
        const size_t block = 4096, blocks = (n + block - 1) / block;
        std::vector<std::vector<uint32_t>> risky(blocks);
        poolFor(n, [&](size_t b, size_t e) {
            std::vector<uint32_t>& out = risky[b / block];
            for (size_t i = b; i < e; ++i) {
                const uint32_t v = verts_[i];
                for (uint32_t t : mesh.vertexTriangles(v)) {
                    const glm::uvec3 tri = mesh.triangles[t];
                    const uint32_t c[3] = {tri.x, tri.y, tri.z};
                    uint32_t slot[3];
                    bool first = true; // (a triangle is judged at its lowest corner that is in the region)
                    for (int k = 0; k < 3; ++k) {
                        slot[k] = foldSlot_[c[k]];
                        if (slot[k] != ~0u && c[k] < v) first = false;
                    }
                    if (!first) continue;
                    const glm::vec3 d0 = slot[0] == ~0u ? glm::vec3(0.0f) : foldD_[slot[0]];
                    const glm::vec3 d1 = (slot[1] == ~0u ? glm::vec3(0.0f) : foldD_[slot[1]]) - d0;
                    const glm::vec3 d2 = (slot[2] == ~0u ? glm::vec3(0.0f) : foldD_[slot[2]]) - d0;
                    const glm::vec3 e1 = pos[c[1]] - pos[c[0]], e2 = pos[c[2]] - pos[c[0]];
                    const glm::vec3 a0 = glm::cross(e1, e2), a1 = glm::cross(e1 + d1, e2 + d2);
                    const float area = glm::length(a0);
                    if (area < 1e-20f) continue;
                    const float up = glm::dot(a1, a0) / area;
                    if (up >= kFoldCosSliver * glm::length(a1) && up >= kFoldKeepSliver * area) continue;
                    for (int k = 0; k < 3; ++k)
                        if (slot[k] != ~0u) out.push_back(slot[k]);
                }
            }
        }, block);
        std::vector<uint8_t> seen(n, 0);
        for (const auto& out : risky)
            for (uint32_t i : out)
                if (!seen[i]) seen[i] = 1, work.push_back(i);
    }
    std::vector<uint32_t> mark(n, ~0u);
    for (int pass = 0; pass < kFoldPasses && !work.empty(); ++pass) {
        poolFor(work.size(), [&](size_t b, size_t e) {
            for (size_t k = b; k < e; ++k) foldScale_[work[k]] = allowed(work[k]);
        }, 1024);
        std::vector<uint32_t> next;
        for (uint32_t i : work) {
            if (foldScale_[i] >= 1.0f) continue;
            foldD_[i] *= foldScale_[i];
            if (foldScale_[i] > 0.99999f) continue; // (a hair: not worth another look at its ring)
            for (uint32_t u : mesh.neighbors(verts_[i])) {
                const uint32_t slot = foldSlot_[u];
                if (slot != ~0u && mark[slot] != uint32_t(pass)) mark[slot] = uint32_t(pass), next.push_back(slot);
            }
        }
        work.swap(next);
    }
    for (size_t i = 0; i < n; ++i) d[i] = foldD_[i], foldSlot_[verts_[i]] = ~0u;
}

// After a dragging dab: each vertex of verts_ slides toward the middle of its neighbours along its own surface (never off it),
// by `lambda` times its weight, `rounds` times over. The material a drag pushes ahead bunches into thin triangles; this
// spreads it out again before the next dab (or the next stroke) can turn them over.
void Sculptor::relaxDrag(Mesh& mesh, const std::vector<float>& weight, float lambda, int rounds) {
    const auto& pos = mesh.positions;
    for (int round = 0; round < rounds; ++round) {
        poolFor(verts_.size(), [&](size_t b, size_t e) {
            for (size_t i = b; i < e; ++i) {
                const uint32_t v = verts_[i];
                scratch_[i] = glm::vec3(0.0f);
                auto ring = mesh.polygonNeighbors(v);
                if (ring.empty() || weight[i] <= 0.0f) continue;
                // (in double: the two halves of a mirrored stroke list a ring's vertices in another order)
                glm::dvec3 avg(0.0);
                for (uint32_t n : ring) avg += glm::dvec3(pos[n]);
                avg /= double(ring.size());
                // (along the surface by the normal it had before the dab: a few degrees off after it, which a slide does not mind)
                glm::dvec3 delta = avg - glm::dvec3(pos[v]);
                const glm::dvec3 normal(mesh.normals[v]);
                const double len = glm::length(normal);
                if (len > 1e-30) delta -= (normal / len) * glm::dot(delta, normal / len);
                scratch_[i] = glm::vec3(delta * double(lambda * weight[i]));
            }
        });
        displaceAll(mesh, verts_, scratch_);
    }
}

// A recipe's Drag (Palette Knife): the dab carries the shape, and the vertices stay spread as they were. Moving the vertices
// themselves, the tip's plateau travels with the stroke while the outside stays: the band between shears by the whole stroke's
// drag over its width (about 4 on a knife stroke of a brush and a half), into slivers a crossing stroke turns over (#198), and no
// relax of fixed triangles takes a shear out (a sheared lattice is already at its rings' middles). So the dab's other moves (its
// plane, along the plane's normal) are made first, by every copy, and then all the copies' drags at once (where copies overlap
// they blend, dabRecipeSingle, so a mirror's two halves don't drag the same clay twice or tear it across the plane). The vertices
// move with the drag, and then each goes back to over where the drag found it, seen along the dragged surface's normal at it, on
// the dragged triangles: a straight walk across them finds the one there, and the point in it is where the drag took the surface
// that was there (over a triangle the drag is the same mix of its corners'). Every vertex lands on the drag's own moved triangles,
// which the drag doesn't fold. Where that can't be followed for every vertex of the dab (a walk meets a triangle turned away, the
// surface pressed flat by a very big brush's plane), or going back would turn a triangle over, the whole dab keeps the vertices
// where the drag put them, as before (dragSlideFallbacks): all or none, as a vertex left behind among neighbours that went back
// folds the triangles between them.
namespace {
using dvec2 = glm::dvec2;
double cross2(const dvec2& a, const dvec2& b) { return a.x * b.y - a.y * b.x; }

// From `origin` (vertex v's place), triangle to triangle along the straight line to `target` (coordinates in the plane across
// n through it, basis e1 e2; each triangle seen flat along n, its corners at at(u)) until the triangle that holds it: that
// triangle and the target's barycentric coordinates in it. False where the way can't be followed (seen along n a triangle on
// the way turns away, an open edge).
template <class At>
bool walkTo(const Mesh& mesh, uint32_t v, const glm::dvec3& origin, const glm::dvec3& e1, const glm::dvec3& e2, const dvec2& target,
            const At& at, uint32_t& tri, double l[3]) {
    const auto flat = [&](uint32_t u) { const glm::dvec3 d = at(u) - origin; return dvec2(glm::dot(d, e1), glm::dot(d, e2)); };
    tri = ~0u;
    if (glm::dot(target, target) < 1e-30) { // (stays: v itself)
        for (uint32_t f : mesh.vertexTriangles(v)) {
            const glm::uvec3 c = mesh.triangles[f];
            tri = f, l[0] = c.x == v, l[1] = c.y == v, l[2] = c.z == v;
            return true;
        }
        return false;
    }
    for (uint32_t f : mesh.vertexTriangles(v)) { // (the corner of v's fan whose sector holds the way)
        const glm::uvec3 c = mesh.triangles[f];
        const int k = c.x == v ? 0 : c.y == v ? 1 : 2;
        const dvec2 fa = flat(c[(k + 1) % 3]), fb = flat(c[(k + 2) % 3]);
        if (cross2(fa, fb) > 0.0 && cross2(fa, target) >= 0.0 && cross2(target, fb) >= 0.0) { tri = f; break; }
    }
    if (tri == ~0u) return false;
    dvec2 p(0.0);
    uint32_t from = ~0u;
    for (int step = 0; step < 256; ++step) {
        const glm::uvec3 c = mesh.triangles[tri];
        const dvec2 a = flat(c.x), b = flat(c.y), d = flat(c.z);
        const double area = cross2(b - a, d - a);
        if (area <= 0.0) return false; // (seen along n it turns away: v's normal is no guide this far out)
        const double lq[3] = {cross2(d - b, target - b) / area, cross2(a - d, target - d) / area, 0.0};
        const double lq2 = 1.0 - lq[0] - lq[1];
        if (lq[0] >= -1e-12 && lq[1] >= -1e-12 && lq2 >= -1e-12) {
            l[0] = std::max(lq[0], 0.0), l[1] = std::max(lq[1], 0.0), l[2] = std::max(lq2, 0.0);
            const double sum = l[0] + l[1] + l[2];
            l[0] /= sum, l[1] /= sum, l[2] /= sum;
            return true;
        }
        // Out by the edge the line from p leaves through: the corner whose coordinate turns negative first.
        const double lp0 = cross2(d - b, p - b) / area, lp1 = cross2(a - d, p - d) / area;
        const double lp[3] = {lp0, lp1, 1.0 - lp0 - lp1}, lt[3] = {lq[0], lq[1], lq2};
        double best = 2.0;
        int k = -1;
        for (int j = 0; j < 3; ++j)
            if (lt[j] < 0.0) {
                const double from_ = std::max(0.0, lp[j]);
                const double t = from_ / (from_ - lt[j]);
                if (t < best) best = t, k = j;
            }
        if (k < 0) return false;
        p += (target - p) * best;
        const uint32_t ea = c[(k + 1) % 3], eb = c[(k + 2) % 3];
        uint32_t next = ~0u;
        for (uint32_t f : mesh.vertexTriangles(ea)) {
            if (f == tri) continue;
            const glm::uvec3 o = mesh.triangles[f];
            if (o.x == eb || o.y == eb || o.z == eb) { next = f; break; }
        }
        if (next == ~0u || next == from) return false;
        from = tri, tri = next;
    }
    return false;
}
} // namespace

void Sculptor::slideBack(Mesh& mesh, BVH& bvh) {
    auto clock = std::chrono::steady_clock::now();
    { // (each vertex once, every copy's drag added: where mirrored copies meet, both reached it)
        std::vector<size_t> order(slideVerts_.size());
        for (size_t i = 0; i < order.size(); ++i) order[i] = i;
        std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return slideVerts_[a] < slideVerts_[b]; });
        std::vector<uint32_t> vs;
        std::vector<glm::vec3> ds;
        vs.reserve(order.size()), ds.reserve(order.size());
        for (size_t i : order) {
            if (!vs.empty() && vs.back() == slideVerts_[i]) ds.back() += slideTo_[i];
            else vs.push_back(slideVerts_[i]), ds.push_back(slideTo_[i]);
        }
        slideVerts_.swap(vs);
        slideTo_.swap(ds);
    }
    const std::vector<uint32_t>& verts = slideVerts_;
    const auto& pos = mesh.positions;
    slideOld_.resize(verts.size());
    for (size_t i = 0; i < verts.size(); ++i) slideOld_[i] = pos[verts[i]];
    displaceAll(mesh, verts, slideTo_); // (the drag, the vertices moving)
    const auto now = [&](uint32_t u) { return glm::dvec3(pos[u]); };
    std::atomic<bool> lost{false};
    poolFor(verts.size(), [&](size_t b, size_t e) {
        for (size_t i = b; i < e && !lost.load(std::memory_order_relaxed); ++i) {
            const uint32_t v = verts[i];
            slideTo_[i] = glm::vec3(0.0f);
            const glm::dvec3 o(slideOld_[i]), x(pos[v]);
            if (o == x) continue;
            glm::dvec3 n(0.0); // (the dragged surface's own, at v: a big brush's plane turns it a long way in one dab)
            for (uint32_t f : mesh.vertexTriangles(v)) {
                const glm::uvec3 c = mesh.triangles[f];
                n += glm::cross(glm::dvec3(pos[c.y]) - glm::dvec3(pos[c.x]), glm::dvec3(pos[c.z]) - glm::dvec3(pos[c.x]));
            }
            const double nl = glm::length(n);
            if (nl < 1e-30) { lost.store(true, std::memory_order_relaxed); break; }
            const glm::dvec3 nn = n / nl;
            const glm::dvec3 e1 = glm::normalize(std::abs(nn.x) < 0.9 ? glm::cross(nn, glm::dvec3(1, 0, 0)) : glm::cross(nn, glm::dvec3(0, 1, 0)));
            const glm::dvec3 e2 = glm::cross(nn, e1);
            const auto flat = [&](const glm::dvec3& d) { return dvec2(glm::dot(d, e1), glm::dot(d, e2)); };
            // (the dragged triangles seen along n from v to over o: the one there holds the point the drag took there, exactly,
            // as the drag is the same mix of its corners' over a triangle)
            uint32_t tri = ~0u;
            double l[3];
            if (!walkTo(mesh, v, x, e1, e2, flat(o - x), now, tri, l)) {
                lost.store(true, std::memory_order_relaxed);
                break;
            }
            const glm::uvec3 c = mesh.triangles[tri];
            const glm::dvec3 at = glm::dvec3(pos[c.x]) * l[0] + glm::dvec3(pos[c.y]) * l[1] + glm::dvec3(pos[c.z]) * l[2];
            slideTo_[i] = glm::vec3(at - x);
        }
    });
    // All of the dab's vertices go back, or none: a vertex left where the drag put it among neighbours that went back folds the
    // triangles between them. The same if going back would turn a triangle over that the drag left facing its way (a big brush's
    // plane can press the surface flat against the way back). Such a dab moves the vertices with the clay, as before #198 (a
    // drag folds nothing by itself: it shears).
    bool keep = !lost.load();
    if (keep) {
        if (slideSlot_.size() != mesh.vertexCount()) slideSlot_.assign(mesh.vertexCount(), ~0u);
        for (size_t i = 0; i < verts.size(); ++i) slideSlot_[verts[i]] = uint32_t(i);
        std::atomic<bool> turned{false};
        const auto after = [&](uint32_t u) {
            const uint32_t k = slideSlot_[u];
            return glm::dvec3(pos[u]) + (k != ~0u ? glm::dvec3(slideTo_[k]) : glm::dvec3(0.0));
        };
        poolFor(verts.size(), [&](size_t b, size_t e) {
            for (size_t i = b; i < e && !turned.load(std::memory_order_relaxed); ++i) {
                const uint32_t v = verts[i];
                for (uint32_t f : mesh.vertexTriangles(v)) {
                    const glm::uvec3 c = mesh.triangles[f];
                    if (std::min({slideSlot_[c.x], slideSlot_[c.y], slideSlot_[c.z]}) != uint32_t(i)) continue; // (once each)
                    const glm::dvec3 was = glm::cross(glm::dvec3(pos[c.y]) - glm::dvec3(pos[c.x]), glm::dvec3(pos[c.z]) - glm::dvec3(pos[c.x]));
                    const glm::dvec3 will = glm::cross(after(c.y) - after(c.x), after(c.z) - after(c.x));
                    if (glm::dot(was, will) <= 0.0) { turned.store(true, std::memory_order_relaxed); break; }
                }
            }
        });
        for (uint32_t v : verts) slideSlot_[v] = ~0u;
        keep = !turned.load();
    }
    if (keep) displaceAll(mesh, verts, slideTo_);
    else ++dragSlideFallbacks;
    timings.kernelMs += lapMs(clock);
    bvh.refit(mesh, verts);
    timings.bvhMs += lapMs(clock);
}

// Taubin's lambda | mu smoothing ("A signal processing approach to fair surface design", 1995): a Laplacian step
// forward (lambda), then a slightly larger one back (mu < -lambda). Bumps (high frequencies) shrink in both steps;
// the large form (low frequencies) shrinks in the first and grows back in the second, so the volume stays.
void Sculptor::taubinShrink(const Mesh& mesh) {
    const auto& pos = mesh.positions;
    shrunk_.resize(verts_.size());
    shrunkIndex_.resize(verts_.size());
    for (size_t i = 0; i < verts_.size(); ++i) shrunkIndex_[i] = {verts_[i], uint32_t(i)};
    std::sort(shrunkIndex_.begin(), shrunkIndex_.end());
    poolFor(verts_.size(), [&](size_t begin, size_t end) {
        for (size_t i = begin; i < end; ++i) {
            const uint32_t v = verts_[i];
            auto ring = mesh.polygonNeighbors(v);
            shrunk_[i] = pos[v];
            if (ring.empty()) continue;
            glm::vec3 avg(0.0f);
            for (uint32_t n : ring) avg += pos[n];
            avg /= static_cast<float>(ring.size());
            shrunk_[i] += kTaubinLambda * (avg - pos[v]);
        }
    });
}

// The BVH after a mirror's dab moved (some of) the vertices its query returned, verts_ (the kernel ran since
// `clock`). The next mirror's query needs the new boxes; normals wait until every mirror moved its vertices
// (updateDabNormals), so where mirrors overlap they are updated once.
void Sculptor::finishDab(Mesh& mesh, BVH& bvh, std::chrono::steady_clock::time_point clock,
                         const std::vector<uint32_t>* moved, const std::vector<uint32_t>* refitOnly) {
    timings.kernelMs += lapMs(clock);
    if (refitOnly) bvh.refit(mesh, *refitOnly);
    else bvh.refitQueried(mesh);
    timings.bvhMs += lapMs(clock);
    ++timings.dabs;
    timings.vertices += verts_.size();
    const auto leaves = bvh.queriedLeaves(); // (the query that found verts_)
    if (adaptiveDab_) { // the normals are done; the exact ones later (catchUpNormals, flushDraft), the GPU's now
        adaptiveDab_ = false;
        lazyMesh_ = &mesh;
        lazyBvh_ = &bvh;
        addLazyLeaves(bvh, leaves);
        lastLazyLeaves_.insert(lastLazyLeaves_.end(), leaves.begin(), leaves.end());
        mesh.markDirtyAround(verts_.size(), 4096, [&](size_t b, size_t e, IndexBits& set) {
            for (size_t i = b; i < e; ++i) set.insertLocal(verts_[i]);
        });
        timings.normalsMs += lapMs(clock);
        return;
    }
    if (moved) { // (only these moved: their rings' normals, updateDabNormals' vertex path)
        dabVerts_.insert(dabVerts_.end(), moved->begin(), moved->end());
        return;
    }
    dabVerts_.insert(dabVerts_.end(), verts_.begin(), verts_.end()); // a vertex twice is fine: updateNormals dedups
    dabLeaves_.insert(dabLeaves_.end(), leaves.begin(), leaves.end());
}

// The normals that the dab's moves change: those of every corner of the triangles in the leaves the queries
// listed (each triangle with a moved vertex is in one), a set found faster than from each moved vertex's ring
// (implicit rings on levels made by Divide cost 7 ms a dab for 3M vertices). Dynamic topology changed the tree
// as it remeshed, so there the rings are used.
void Sculptor::updateDabNormals(Mesh& mesh, const BVH& bvh) {
    auto clock = std::chrono::steady_clock::now();
    if (curvature_.size() != mesh.vertexCount() || curvatureMesh_ != &mesh) {
        curvature_.assign(mesh.vertexCount(), 0.0f);
        curvatureFilled_ = false;
        curvatureMesh_ = &mesh;
    }
    if (draft_ && !dynTopo_.active() && !dabLeaves_.empty()) {
        // A draft: the vertices go up with the normals and cavity terms they had (filled in once for the mesh).
        if (!curvatureFilled_) {
            poolFor(mesh.vertexCount(), [&](size_t b, size_t e) {
                for (size_t v = b; v < e; ++v) curvature_[v] = mesh.curvatureAt(uint32_t(v));
            }, 8192);
            curvatureFilled_ = true;
        }
        mesh.markDirtyAround(dabLeaves_.size(), 64, [&](size_t b, size_t e, IndexBits& set) {
            for (size_t i = b; i < e; ++i)
                for (uint32_t t : bvh.leafTriangles(dabLeaves_[i])) {
                    const glm::uvec3& tri = mesh.triangles[t];
                    set.insertLocal(tri.x);
                    set.insertLocal(tri.y);
                    set.insertLocal(tri.z);
                }
        });
        draftLeaves_.insert(draftLeaves_.end(), dabLeaves_.begin(), dabLeaves_.end());
        dabLeaves_.clear();
        timings.normalsMs += lapMs(clock);
        return;
    }
    if (dynTopo_.active() || dabLeaves_.empty()) {
        mesh.updateNormals(dabVerts_, curvature_.data());
    } else {
        mesh.updateNormalsAround(dabLeaves_.size(), 64, [&](size_t b, size_t e, IndexBits& set) {
            for (size_t i = b; i < e; ++i)
                for (uint32_t t : bvh.leafTriangles(dabLeaves_[i])) {
                    const glm::uvec3& tri = mesh.triangles[t];
                    set.insertLocal(tri.x);
                    set.insertLocal(tri.y);
                    set.insertLocal(tri.z);
                }
        }, curvature_.data());
    }
    dabLeaves_.clear();
    timings.normalsMs += lapMs(clock);
}

void Sculptor::dabRecipe(Mesh& mesh, BVH& bvh, const BrushRecipe& recipe, const DabParams& p, Symmetry sym) {
    if (!active_) return;
    if (microReady(mesh)) {
        // A micro level: the grid takes a Standard dab with the recipe's depth (its sign carves or builds) and its
        // surface pattern (Dragon Scales, Skin Pores..., #120: they sculpted plain blobs there). Planes, pinch and
        // drag aren't grid brushes.
        const bool patterned = recipe.pattern != BrushRecipe::Pattern::None && recipe.patternAmount > 0.0f;
        const bool invert = p.invert != (recipe.depth < 0.0f);
        // (Standard's 0.08 r layer per full dab against the recipe's 0.12 r * depth)
        const float strength = p.strength * std::min(1.0f, std::abs(recipe.depth) * 1.5f);
        const float pressure = std::pow(std::clamp(p.pressure, 0.0f, 1.0f), recipe.pressureCurve);
        const double k0 = micro_.findMs + micro_.kernelMs, s0 = micro_.stitchMs;
        const uint64_t pts = micro_.points;
        if (p.stencil.active()) inkOk_ = false;
        inkHeader(false, p.alpha, patterned ? int(recipe.pattern) : 0, recipe.patternScale, recipe.patternAmount);
        for (const SymCopy& m : copies(sym)) {
            const auto stencil = stencilTexture(p, m);
            std::function<float(const glm::vec3&)> texture; // (each copy reads the pattern at its source dab's point)
            if (patterned) texture = microPatternTexture(int(recipe.pattern), recipe.patternScale, recipe.patternAmount, p.radius, m);
            std::function<float(const glm::vec3&)> both = texture;
            if (stencil && texture) both = [&texture, &stencil](const glm::vec3& x) { return texture(x) * stencil(x); };
            else if (stencil) both = stencil;
            MicroDab d;
            d.type = BrushType::Standard, d.center = m.point(p.center), d.viewDir = m.dir(p.viewDir), d.radius = p.radius;
            d.strength = strength * pressure, d.invert = invert, d.focal = p.focal, d.alphaUp = m.dir(p.alphaUp);
            d.copy = m, d.pxPerUnit = p.pxPerUnit;
            micro_.dab(mesh, bvh, d.type, d.center, d.viewDir, d.radius, d.strength, d.invert, d.focal, d.alphaUp, false, 0.0f, p.alpha, both, glm::vec3(0.0f), d.pxPerUnit);
            inkDab(d);
        }
        timings.kernelMs += micro_.findMs + micro_.kernelMs - k0;
        timings.normalsMs += micro_.stitchMs - s0;
        timings.vertices += micro_.points - pts;
        ++timings.dabs;
        return;
    }
    const bool useAlpha = p.alpha && !p.alpha->empty(); // (an alpha reaches its square's corners, #75)
    remeshDab(mesh, bvh, p, sym, 1.0f, useAlpha ? kAlphaReach : 1.0f);
    DabParams base = p;
    base.pressure = std::pow(std::clamp(p.pressure, 0.0f, 1.0f), recipe.pressureCurve);
    base.frontFacesOnly = p.frontFacesOnly || recipe.frontFacesOnly;
    base.connectedOnly = p.connectedOnly || recipe.connectedOnly;
    base.viewFalloff = p.viewFalloff || recipe.viewFalloff;
    dabVerts_.clear();
    dabLeaves_.clear();
    lastLazyLeaves_.clear();
    const std::vector<SymCopy> all = copies(sym);
    crispCopies_ = all; // (a crisp cut shares each vertex out among the copies: dabRecipeSingle)
    crispCopyCenters_.clear();
    for (const SymCopy& m : all) crispCopyCenters_.push_back(m.point(base.center));
    slideVerts_.clear();
    slideTo_.clear();
    for (size_t k = 0; k < all.size(); ++k) {
        const SymCopy& m = all[k];
        frameWanted_ = m.identity(); // (the unmirrored pass records lastFrame)
        const DabParams mp = m.identity() ? base : transformed(base, m);
        dabRecipeSingle(mesh, bvh, recipe, mp, k, m);
    }
    if (!slideVerts_.empty()) slideBack(mesh, bvh);
    if (!dabVerts_.empty() || !dabLeaves_.empty() || lastLazyLeaves_.empty()) updateDabNormals(mesh, bvh); // (as dab)
    repairDab(mesh, bvh);
}

void Sculptor::dabRecipeSingle(Mesh& mesh, BVH& bvh, const BrushRecipe& r, const DabParams& p, size_t copy,
                               const SymCopy& m) {
    auto clock = std::chrono::steady_clock::now();
    // An alpha (if any) modulates the recipe's tip, oriented on the view-facing plane. Falloff shifts the tip
    // (0 keeps the recipe's own Hardness / Tip Sharpness), and so the alpha with it. With an alpha the region
    // reaches the stamp's corners and, below 0, the round tip opens up toward 1 as alphaFalloff does, so at -100
    // the whole alpha shows (#75; the built-in brushes got this in #60).
    const bool useAlpha = p.alpha && !p.alpha->empty();
    brushRegion(mesh, bvh, p.center, p.viewDir, p.radius * (useAlpha ? kAlphaReach : 1.0f), p.connectedOnly,
                p.viewFalloff, verts_, connected_, useAlpha && p.stampFootprint);
    if (verts_.empty()) return;
    StencilView stencil = p.stencil;
    if (stencil.active()) stencil.setSpacing(p.center, p.viewDir, p.radius * (useAlpha ? kAlphaReach : 1.0f), verts_.size());
    const auto& pos = mesh.positions;
    const auto& nrm = mesh.normals;
    glm::vec3 aUp = projectOnPlane(p.alphaUp, p.viewDir);
    aUp = glm::length(aUp) > 1e-6f ? glm::normalize(aUp) : glm::vec3(0, 1, 0);
    // (a mirror swaps the handedness of a cross: the stamp reflects with the copy instead of turning, #197)
    const glm::vec3 aRight = glm::cross(aUp, -p.viewDir) * m.handedness();
    auto alphaWeight = [&](const glm::vec3& x) {
        const glm::vec3 o = x - p.center;
        return p.alpha->sample(glm::dot(o, aRight) / p.radius, glm::dot(o, aUp) / p.radius);
    };
    if (p.frontFacesOnly) {
        std::erase_if(verts_, [&](uint32_t v) { return glm::dot(nrm[v], p.viewDir) > 0.0f; });
        if (verts_.empty()) return;
    }
    // A locked Flatten cut never moves a point below its plane (Crisp Flatten only cuts), and once a stroke has cut
    // deep, as much of a dab's sphere lies under the facet as above it: those points leave the region here, before
    // their weights. (A 240 px stroke across a scan, its thin parts cut flat, took half as many dabs, each with 2.5M
    // points instead of 1.6M.) The region's own size still sets the edge, the batching and the normals' path.
    const size_t regionCount = verts_.size();
    if (r.crispEdge && r.planeLock && r.plane == BrushRecipe::Plane::Flatten && r.depth == 0.0f && r.pinch == 0.0f &&
        r.drag <= 0.0f && r.smooth <= 0.0f && copy < lockedPlanes_.size() && lockedPlanes_[copy].first != glm::vec3(0.0f)) {
        const auto [n, c] = lockedPlanes_[copy];
        crispKeep_.resize(verts_.size());
        poolFor(verts_.size(), [&](size_t b, size_t e) {
            for (size_t i = b; i < e; ++i) crispKeep_[i] = glm::dot(pos[verts_[i]] - c, n) >= 0.0f;
        }, 32768);
        size_t kept = 0;
        for (size_t i = 0; i < verts_.size(); ++i)
            if (crispKeep_[i]) verts_[kept++] = verts_[i];
        verts_.resize(kept);
        if (verts_.empty()) return;
    }

    const float rad = p.radius;
    const float s = std::clamp(p.strength, 0.0f, 1.0f) * p.pressure;
    const float sign = p.invert ? -1.0f : 1.0f;

    // Per-vertex weight: tip shape, cavity mask and pattern.
    weights_.resize(verts_.size());
    float edge = std::max(0.05f, 1.0f - r.hardness * 0.95f);
    float exponent = 1.0f + r.tipSharpness * 5.0f;
    if (r.crispEdge) {
        // Full strength out to a hard edge, anti-aliased over about 2.5 vertex spacings so the boundary stays a
        // clean line instead of stair-stepping along the triangles (the spacing from the region's vertex count: a
        // triangle mesh has about one vertex per 0.87 e^2 of surface, and the region covers about pi r^2).
        const float spacing = 1.9f / std::sqrt(float(regionCount));
        edge = std::clamp(2.5f * spacing, 0.03f, 0.5f);
        exponent = 1.0f;
    }
    const float patternFreq = 1.0f / std::max(rad * r.patternScale, 1e-6f);
    // Each copy reads the pattern where its source dab does (the mirror image of the surface), so the copies match.
    const bool copied = !m.identity();
    auto patternAt = [&](const glm::vec3& x) { return copied ? m.inversePoint(x) : x; };
    const float focalExp = std::exp2(2.0f * std::clamp(p.focal, -1.0f, 1.0f));
    // (View falloff: the plane from the surface under the cursor, as dabSingle's; gh#229)
    const bool ball = p.viewFalloff && r.plane != BrushRecipe::Plane::None;
    const AreaSums sums = areaSums(verts_.size(), [&](size_t i, AreaSums& acc) {
        const uint32_t v = verts_[i];
        float t = std::clamp((1.0f - brushDistance(p, pos[v]) / rad) / edge, 0.0f, 1.0f);
        if (p.focal != 0.0f) t = std::pow(t, focalExp); // (brushFalloff's)
        float w = t * t * (3.0f - 2.0f * t);
        if (exponent != 1.0f) w = std::pow(w, exponent); // (a pow a vertex is a third of a big dab's weights)
        if (useAlpha && p.focal < 0.0f) w += (1.0f - w) * std::min(-p.focal, 1.0f); // (alphaFalloff's)
        acc.normal += glm::dvec3(nrm[v]) * double(w);
        acc.center += glm::dvec3(pos[v]) * double(w);
        acc.weight += w;
        if (r.cavityMask != 0.0f) {
            auto ring = mesh.neighbors(v);
            if (!ring.empty()) {
                glm::vec3 avg(0.0f);
                float edgeLen = 0.0f;
                for (uint32_t n : ring) {
                    avg += pos[n];
                    edgeLen += glm::distance(pos[n], pos[v]);
                }
                avg /= float(ring.size());
                edgeLen /= float(ring.size());
                // > 0 in cavities (neighbours sit above the vertex), < 0 on ridges.
                float c = std::clamp(glm::dot(nrm[v], avg - pos[v]) / std::max(edgeLen, 1e-9f) * 6.0f, -1.0f, 1.0f);
                float m = r.cavityMask > 0.0f ? std::max(c, 0.0f) : std::max(-c, 0.0f);
                w *= glm::mix(1.0f, m, std::abs(r.cavityMask));
            }
        }
        if (r.pattern != BrushRecipe::Pattern::None && r.patternAmount > 0.0f)
            w *= glm::mix(1.0f, samplePattern(r.pattern, patternAt(pos[v]) * patternFreq), r.patternAmount);
        if (useAlpha) w *= alphaWeight(pos[v]);
        if (stencil.active() && w > 0.0f) w *= stencil.weight(pos[v]);
        weights_[i] = w;
    });
    AreaSums near;
    if (ball) // (the same profile over the distance in space; a cursor with no vertex near it: the cylinder's)
        near = areaSums(verts_.size(), [&](size_t i, AreaSums& acc) {
            const uint32_t v = verts_[i];
            float t = std::clamp((1.0f - glm::distance(pos[v], p.center) / rad) / edge, 0.0f, 1.0f);
            if (p.focal != 0.0f) t = std::pow(t, focalExp);
            const float w = t * t * (3.0f - 2.0f * t);
            acc.normal += glm::dvec3(nrm[v]) * double(w);
            acc.center += glm::dvec3(pos[v]) * double(w);
            acc.weight += w;
        });
    const AreaSums& plane = near.weight > 0.0 ? near : sums;
    glm::vec3 areaNormal = glm::vec3(plane.normal), areaCenter = glm::vec3(plane.center);
    const float wsum = float(plane.weight);
    if (wsum <= 0.0f) return;
    areaCenter /= wsum;
    float nlen = glm::length(areaNormal);
    areaNormal = nlen > 1e-12f ? areaNormal / nlen : -p.viewDir;
    float dabLevel = 0.0f; // Lock plane: how far this dab's own surface lies above the locked plane, in radii
    if (r.planeLock && r.plane != BrushRecipe::Plane::None) {
        // Lock plane: the first dab's plane for the whole stroke (each symmetry copy its own).
        if (copy >= lockedPlanes_.size()) lockedPlanes_.resize(copy + 1, {glm::vec3(0.0f), glm::vec3(0.0f)});
        auto& [n, c] = lockedPlanes_[copy];
        // (a crisp copy whose dab overlaps the first one's takes the mirror image of its plane: measured, the region was
        // already part cut by the first copy, and the two planes no longer met on the mirror)
        const bool mirrored = r.crispEdge && copy > 0 && copy < crispCopyCenters_.size() &&
                              lockedPlanes_[0].first != glm::vec3(0.0f) &&
                              glm::distance(crispCopyCenters_[copy], crispCopyCenters_[0]) < 2.0f * p.radius;
        if (n == glm::vec3(0.0f) && mirrored) n = m.dir(lockedPlanes_[0].first), c = m.point(lockedPlanes_[0].second);
        if (n == glm::vec3(0.0f)) n = areaNormal, c = areaCenter;
        else dabLevel = glm::dot(areaCenter - c, n) / p.radius, areaNormal = n, areaCenter = c;
    }
    if (frameWanted_) {
        lastFrame = {p.center, areaNormal, areaCenter, true};
        frameWanted_ = false;
        lastRegion_ = regionCount;
        lastRegionRadius_ = p.radius;
    }
    // Crisp edge: the plane pulls all the way from the default Intensity up, as Clay does (clayPull). Pen pressure
    // doesn't scale the pull: each dab cutting to a pressure-scaled depth left a terrace (a contour ridge) at its rim
    // across the facet, where the stroke has one plane (pressure still sizes the dab and the tip, if set to).
    const float planePull = r.crispEdge ? clayPull(std::clamp(p.strength, 0.0f, 1.0f)) : s;
    const bool crispMax = r.crispEdge && r.planeLock && (r.plane == BrushRecipe::Plane::Flatten ||
                                                         r.plane == BrushRecipe::Plane::Fill ||
                                                         r.plane == BrushRecipe::Plane::Scrape);
    // Symmetry: where the copies' dabs overlap (a stroke along the mirror plane, a big brush) both planes reached the same
    // vertices, each pulling its own way from the other's half-done cut, and the two sides slid across the mirror into each
    // other (folds along the seam, 351 of them for three strokes stacked in artist-qa's close-up). Each vertex is now cut by
    // the copy whose dab is nearest (the mirror plane between two mirrored copies, where their planes meet), and the copies
    // that overlap pull the same way, their normals' mean (it lies in the mirror plane), so no point crosses it. Apart, each
    // pulls straight along its own normal.
    glm::vec3 pullDir = areaNormal;
    const bool shared = crispMax && crispCopies_.size() > 1 && copy < crispCopies_.size();
    if (shared) {
        const glm::vec3 own = crispCopies_[copy].inverseDir(areaNormal); // (the source copy's normal)
        glm::vec3 sum(0.0f);
        for (size_t k = 0; k < crispCopies_.size(); ++k) {
            const float apart = glm::distance(crispCopyCenters_[copy], crispCopyCenters_[k]) / p.radius;
            const float t = std::clamp(2.0f - apart, 0.0f, 1.0f); // (all of it a radius apart or closer, none past two)
            const float w = k == copy ? 1.0f : t * t * (3.0f - 2.0f * t);
            sum += crispCopies_[k].dir(own) * w;
        }
        const float len = glm::length(sum);
        if (len > 1e-6f && glm::dot(sum / len, areaNormal) > 0.3f) pullDir = sum / len; // (planes far apart: each its own)
    }
    const float pullDot = glm::dot(pullDir, areaNormal);
    auto ownCopy = [&](const glm::vec3& x) { // (ties go to the first copy)
        if (!shared) return true;
        const float mine = glm::distance(x, crispCopyCenters_[copy]);
        for (size_t k = 0; k < crispCopyCenters_.size(); ++k) {
            const float other = glm::distance(x, crispCopyCenters_[k]);
            if (k != copy && (other < mine || (other == mine && k < copy))) return false;
        }
        return true;
    };
    const uint32_t stamp = uint32_t(strokeId_);
    if (crispMax && crispStamp_.size() != mesh.vertexCount()) {
        crispStamp_.resize(mesh.vertexCount(), 0);
        crispDone_.resize(mesh.vertexCount(), 0.0f);
        crispFacing_.resize(mesh.vertexCount(), 0.0f);
    }
    // A big region on a locked crisp plane takes the adaptive path's normals, as the built-in plane brushes do
    // (Sculpt.h, analyticNormals): a point the plane takes has its normal, and the band's turn toward it by the share
    // of the way they went; the exact normals follow behind the brush and when the stroke ends. Only for a pure plane
    // pull with no mask or hidden part (where the move is the plane's alone).
    bool lazyNormals = crispMax && analyticNormals && !dynTopo_.active() && !draft_ &&
                       regionCount >= kAdaptiveMinVertices && mesh.normals.size() == mesh.vertexCount() &&
                       r.depth == 0.0f && r.pinch == 0.0f && r.drag <= 0.0f && r.smooth <= 0.0f;
    if (lazyNormals && (mesh.mask.size() == mesh.vertexCount() || mesh.hasHidden())) { // (as dabSingle: masked regions only)
        std::atomic<bool> masked{false};
        poolFor(verts_.size(), [&](size_t b, size_t e) {
            for (size_t i = b; i < e && !masked.load(std::memory_order_relaxed); ++i)
                if (vertexInfluence(mesh, verts_[i]) < 1.0f) masked.store(true, std::memory_order_relaxed);
        }, 65536);
        lazyNormals = !masked.load();
    }
    if (lazyNormals) {
        if (curvature_.size() != mesh.vertexCount() || curvatureMesh_ != &mesh) {
            curvature_.assign(mesh.vertexCount(), 0.0f);
            curvatureFilled_ = false;
            curvatureMesh_ = &mesh;
        }
        if (curvatureStamp_.size() != mesh.vertexCount()) curvatureStamp_.assign(mesh.vertexCount(), 0);
    }

    glm::vec3 strokeDir = p.strokeDelta;
    float strokeLen = glm::length(strokeDir);
    strokeDir = strokeLen > 1e-12f ? strokeDir / strokeLen : glm::vec3(0.0f);

    // A drag moves a vertex by its stroke step times its weight, so neighbours at the steepest part of the tip's falloff
    // move apart or together by (step x slope) of their distance: a long step on a hard tip (a quarter of the brush on a
    // Palette Knife) squeezes the triangles there to a fifth of their area in one dab. Such a step drags less.
    float dragScale = 1.0f;
    if (r.drag > 0.0f) {
        const float slope = exponent * 1.5f / (edge * rad); // (the steepest rise of the weight with distance)
        dragScale = std::min(1.0f, kMaxDragSqueeze / std::max(r.drag * strokeLen * slope, 1e-12f));
    }
    const bool slide = r.drag > 0.0f && dragSlide && !dynTopo_.active() && !crispMax; // (the drag after the plane: slideBack)
    if (slide) dragPart_.assign(verts_.size(), glm::vec3(0.0f));
    scratch_.resize(verts_.size());
    poolFor(verts_.size(), [&](size_t begin, size_t end) {
    for (size_t i = begin; i < end; ++i) {
        const uint32_t v = verts_[i];
        const glm::vec3& x = pos[v];
        float f = weights_[i];
        if (!ownCopy(x)) { // (another copy's vertex: its dab is nearer)
            scratch_[i] = glm::vec3(0.0f);
            continue;
        }
        const float h = glm::dot(x - areaCenter, areaNormal);
        // Crisp edge on a locked plane: the height and facing this stroke found the vertex with (h is what is left
        // of that height), so every dab of the stroke asks the same of it.
        float h0 = h, facing = glm::dot(nrm[v], areaNormal);
        if (crispMax) {
            if (crispStamp_[v] != stamp) crispStamp_[v] = stamp, crispDone_[v] = 0.0f, crispFacing_[v] = facing;
            h0 = h / std::max(1.0f - crispDone_[v], 1e-3f);
            facing = crispFacing_[v];
        }
        // View falloff reaches through the model: a plane recipe leaves what lies a radius or more off its plane (the far
        // side), fading from 3/4 of it, instead of pulling it through to the plane in shards (gh#229).
        if (ball) f *= std::clamp((1.0f - std::abs(h0) / rad) * 4.0f, 0.0f, 1.0f) * viewDepthFade(x, p.center, p.viewDir, rad);
        if (r.crispEdge && r.plane != BrushRecipe::Plane::None && f > 0.0f) {
            // Crisp edge on a plane that follows the stroke: the surface turned away from the plane (the side of a box
            // past its edge, the far side of a thin part) fades out between 85 and 60 degrees off it when cut, so a
            // full pull doesn't fold it onto the plane as a flap (a 45 degree chamfer across a rounded edge still
            // cuts). Below the plane only a surface nearly parallel to it fills (dips, not the faces beside a chamfer:
            // those are 45 degrees off and filling them raised a lip along the facet), fading out between 37 and 23
            // degrees. (The surface beside a 45 degree chamfer stays clear of both, so the small turns its normals take
            // as their neighbours move don't speckle the pull.) On a locked plane a cut leaves no point partway: the
            // plane pulls every point straight along one way, so the surface keeps the way it faced across the plane,
            // and a steep wall or an undercut comes down flat onto the plane (a sliver lying in it). Held partway by
            // its facing, each point of a near-vertical wall stopped at its own height, a row of standing teeth and
            // shards (a member's crisp ridges, and the lip under an eye on a head).
            const bool cutAll = crispMax && h0 >= 0.0f;
            const float lo = h0 < 0.0f ? 0.8f : 0.1f;
            const float hi = h0 < 0.0f ? 0.92f : 0.5f;
            const float t = std::clamp((facing - lo) / (hi - lo), 0.0f, 1.0f);
            if (!cutAll) f *= t * t * (3.0f - 2.0f * t);
            // Where the tip's edge crosses material far from the plane (the ends of a cut through a ridge), the band
            // widens to three times the depth of the cut there, so the wall is a gentle slope instead of a cliff a few
            // vertices wide that the triangles can only show as ragged steps and spikes (a 45 degree wall still did, on a
            // dense head).
            const float depth = std::abs(h0) / rad;
            constexpr float kWall = 3.0f; // (a run of three times the drop: a wall of about 20 degrees, which a facet's edge shows clean)
            if (depth > edge) {
                const float u = std::clamp(1.0f - brushDistance(p, x) / rad, 0.0f, 1.0f);
                const float a = std::min(u / edge, 1.0f), b = std::min(u / std::min(depth * kWall, 1.0f), 1.0f);
                f *= a > 0.0f ? std::min((b * b * (3.0f - 2.0f * b)) / (a * a * (3.0f - 2.0f * a)), 1.0f) : 0.0f;
            }
        }
        glm::vec3 dir;
        switch (r.direction) {
        case BrushRecipe::Direction::Inflate: dir = nrm[v]; break;
        case BrushRecipe::Direction::View: dir = -p.viewDir; break;
        case BrushRecipe::Direction::Stroke: dir = strokeDir; break;
        default: dir = areaNormal; break;
        }
        // (View falloff, no plane: as dabSingle's Standard, the far side stays; the drag keeps the whole circle)
        const float depthFade = p.viewFalloff && r.plane == BrushRecipe::Plane::None &&
                                        (r.direction == BrushRecipe::Direction::Normal || r.direction == BrushRecipe::Direction::Inflate)
                                    ? viewAdditiveFade(x, p.center, p.viewDir, rad)
                                    : 1.0f;
        glm::vec3 d = dir * (sign * r.depth * 0.12f * rad * s * f * depthFade);

        // Crisp edge: a Flatten plane only cuts (a member's ask: it never raises or adds clay, at any depth; the
        // regular Flatten brush is the one that fills). A point below the plane stays where it is, so no dip is
        // filled and no scattered, half-filled points are left along the facet's edges. A Fill plane (a Brush Lab
        // recipe) still fills, only shallow dips (fading out from 10% to 30% of the radius deep), and only while the
        // dab is on the plane's level (5% to 15% of the radius under it), so it doesn't lay a ledge down a curve.
        float fill = 1.0f;
        if (r.crispEdge && h0 < 0.0f) {
            if (r.plane == BrushRecipe::Plane::Flatten) {
                fill = 0.0f;
            } else {
                const float t = std::clamp((-h0 / rad - 0.1f) / 0.2f, 0.0f, 1.0f);
                const float l = std::clamp((-dabLevel - 0.05f) / 0.1f, 0.0f, 1.0f);
                fill = (1.0f - t * t * (3.0f - 2.0f * t)) * (1.0f - l * l * (3.0f - 2.0f * l));
            }
        }
        if (crispMax) {
            // The vertex goes the strongest pull's share of the way from where this stroke found it: what is
            // missing of it (h is what is left of that height).
            const bool side = r.plane == BrushRecipe::Plane::Flatten || (r.plane == BrushRecipe::Plane::Fill) == (h0 < 0.0f);
            const float want = side ? std::clamp(planePull * f * fill * r.planeAmount, 0.0f, 1.0f) : 0.0f;
            const float done = crispDone_[v];
            if (want > done) {
                const float step = (want - done) / std::max(1.0f - done, 1e-6f);
                d -= pullDir * (h * step / pullDot);
                crispDone_[v] = want;
                if (lazyNormals) {
                    if (curvatureStamp_[v] != stamp) curvatureStamp_[v] = stamp, curvature_[v] = kKeepCurvature;
                    const glm::vec3 n = glm::mix(nrm[v], areaNormal, step);
                    const float len = glm::length(n);
                    if (len > 1e-12f) mesh.normals[v] = n / len;
                }
            }
        } else
        switch (r.plane) {
        case BrushRecipe::Plane::Clay: {
            float k;
            const glm::vec3 planePoint = PlaneKernel::clayPlaneAt(areaCenter, areaNormal, p.center, sign, rad * 0.06f * s, k);
            float hc = glm::dot(x - planePoint, areaNormal);
            if (sign * hc < 0.0f) d -= areaNormal * (hc * f * r.planeAmount * clayPull(s) * k);
            break;
        }
        case BrushRecipe::Plane::Flatten: d -= areaNormal * (h * planePull * f * fill * r.planeAmount); break;
        case BrushRecipe::Plane::Fill:
            if (h < 0.0f) d -= areaNormal * (h * planePull * f * fill * r.planeAmount);
            break;
        case BrushRecipe::Plane::Scrape:
            if (h > 0.0f) d -= areaNormal * (h * planePull * f * r.planeAmount);
            break;
        default: break;
        }
        if (r.pinch != 0.0f) d += projectOnPlane(p.center - x, areaNormal) * (r.pinch * 0.25f * s * f);
        if (r.drag > 0.0f) {
            glm::vec3 drag = p.strokeDelta * (dragScale * r.drag * f);
            if (slide && crispCopyCenters_.size() > 1 && copy < crispCopyCenters_.size()) {
                // (where copies' dabs overlap, each drags its share, softly by nearness: their drags blend instead of adding
                // up, the mirror's two ways cancel across it, and nothing is dragged twice as far along it)
                const float mine = glm::distance(x, crispCopyCenters_[copy]);
                float sum = 1.0f;
                for (size_t k = 0; k < crispCopyCenters_.size(); ++k) {
                    if (k == copy) continue;
                    const float apart = glm::distance(crispCopyCenters_[copy], crispCopyCenters_[k]) / rad;
                    const float t = std::clamp(2.0f - apart, 0.0f, 1.0f); // (all of it a radius apart or closer, none past two)
                    if (t > 0.0f) sum += t * t * (3.0f - 2.0f * t) * std::exp(-(glm::distance(x, crispCopyCenters_[k]) - mine) * kDragShare / rad);
                }
                drag /= sum;
            }
            if (slide) dragPart_[i] = drag; // (made after every copy's plane: slideBack)
            else d += drag;
        }
        scratch_[i] = d;
    }
    });
    if (crispMax) { // (most of a locked stroke's region is as far as this dab asks already: only the moves)
        crispMovedVerts_.clear();
        crispMoved_.clear();
        for (size_t i = 0; i < verts_.size(); ++i)
            if (scratch_[i] != glm::vec3(0.0f)) crispMovedVerts_.push_back(verts_[i]), crispMoved_.push_back(scratch_[i]);
        displaceAll(mesh, crispMovedVerts_, crispMoved_);
    } else {
        if (slide) {
            displaceAll(mesh, verts_, scratch_);
            for (size_t i = 0; i < verts_.size(); ++i)
                if (dragPart_[i] != glm::vec3(0.0f)) slideVerts_.push_back(verts_[i]), slideTo_.push_back(dragPart_[i]);
        } else {
            displaceAll(mesh, verts_, scratch_);
            if (r.drag > 0.0f) relaxDrag(mesh, weights_, kRelaxLambda * std::min(1.0f, strokeLen / (kRelaxStep * rad)), kRelaxRounds);
        }
    }

    if (crispMax) {
        // The points pulled onto the plane bunch up where they came from a curve (tiny triangles, speckled
        // shading): each slides toward its neighbours' middle along the plane only, so the facet's triangles even
        // out and every height stays as it is.
        poolFor(verts_.size(), [&](size_t begin, size_t end) {
            for (size_t i = begin; i < end; ++i) {
                const uint32_t v = verts_[i];
                // (only the points this dab moved: the rest of the facet was evened out by the dabs that took it)
                const bool movedNow = scratch_[i] != glm::vec3(0.0f);
                scratch_[i] = glm::vec3(0.0f);
                if (!movedNow) continue;
                // (only points inside the facet, the plane having taken them and their whole ring: one on its
                // border would slide over the half-pulled points of the edge's band and fold a triangle there)
                // (and with symmetry, only among this copy's own points: the other copy's side of the mirror isn't cut yet in
                // this dab, and the two sides would slide differently)
                auto taken = [&](uint32_t u) { return crispStamp_[u] == stamp && crispDone_[u] >= 0.99f && ownCopy(pos[u]); };
                if (!taken(v)) continue;
                auto ring = mesh.polygonNeighbors(v);
                if (ring.empty() || !std::all_of(ring.begin(), ring.end(), taken)) continue;
                glm::vec3 avg(0.0f);
                for (uint32_t n : ring) avg += pos[n];
                const glm::vec3 delta = avg / float(ring.size()) - pos[v];
                scratch_[i] = (delta - areaNormal * glm::dot(delta, areaNormal)) * 0.25f;
            }
        });
        // (only the points that slide: most of a dab's region doesn't, and displaceAll walks all it is given)
        crispSlideVerts_.clear();
        crispSlide_.clear();
        for (size_t i = 0; i < verts_.size(); ++i)
            if (scratch_[i] != glm::vec3(0.0f)) crispSlideVerts_.push_back(verts_[i]), crispSlide_.push_back(scratch_[i]);
        displaceAll(mesh, crispSlideVerts_, crispSlide_);
    }

    if (r.smooth > 0.0f) {
        poolFor(verts_.size(), [&](size_t begin, size_t end) {
            for (size_t i = begin; i < end; ++i) {
                auto ring = mesh.polygonNeighbors(verts_[i]);
                if (ring.empty()) {
                    scratch_[i] = glm::vec3(0.0f);
                    continue;
                }
                glm::vec3 avg(0.0f);
                for (uint32_t n : ring) avg += pos[n];
                avg /= float(ring.size());
                scratch_[i] = (avg - pos[verts_[i]]) * std::min(1.0f, r.smooth * weights_[i]);
            }
        });
        displaceAll(mesh, verts_, scratch_);
    }
    adaptiveDab_ = lazyNormals;
    // (a crisp dab's normals: around the points it moved, the relax's being among them, not its whole region; and
    // the BVH's leaves around them, when they are few: most of a deep cut's leaves hold points the dab left as they were)
    const bool fewMoved = crispMax && (crispMovedVerts_.size() + crispSlideVerts_.size()) * 4 < verts_.size();
    if (fewMoved) {
        crispRefit_.assign(crispMovedVerts_.begin(), crispMovedVerts_.end());
        crispRefit_.insert(crispRefit_.end(), crispSlideVerts_.begin(), crispSlideVerts_.end());
    }
    finishDab(mesh, bvh, clock, crispMax && !lazyNormals ? &crispMovedVerts_ : nullptr, fewMoved ? &crispRefit_ : nullptr);
}

void Sculptor::beginGrab(Mesh& mesh, BVH& bvh, const DabParams& p, Symmetry sym) {
    grabVerts_.clear();
    grabWeights_.clear();
    grabMirrors_.clear();
    grabAll_.clear();
    grabLeaves_.clear();
    grabCaptured_ = false;
    grabTotal_ = glm::vec3(0.0f);
    if (!active_) return;
    if (mesh.micro) inkMesh_ = &mesh;
    // A micro level (core/Micro.h): Move deforms the base cage below it (dual scale: the detail is relative to the
    // base, so it rides along in its local frame), unless asked to move the micro surface along its normals.
    // A mask nearby stays on the cage too: displaceAll holds every base vertex whose triangles (and their neighbours', for
    // the normals) carry it, so masked samples keep their bits. (Switching to the grid's along-the-normal rule there folded
    // the surface into leaf-like lumps wherever the base's forms are smaller than the drag: its normals fan out over them.)
    if (microMoveAlongNormals && microReady(mesh)) {
        for (const SymCopy& m : copies(sym)) {
            MicroDab d;
            d.op = MicroDab::Op::GrabBegin, d.type = BrushType::Move, d.center = m.point(p.center), d.radius = p.radius, d.focal = p.focal;
            d.copy = m, d.pxPerUnit = p.pxPerUnit;
            micro_.beginGrab(mesh, bvh, d.center, d.radius, d.focal, m.m);
            inkHeader(false, nullptr, 0, 0.0f, 0.0f);
            inkDab(d);
        }
        return;
    }
    for (const SymCopy& m : copies(sym)) {
        const glm::vec3 c = m.point(p.center), view = m.dir(p.viewDir);
        brushRegion(mesh, bvh, c, view, p.radius, p.connectedOnly, p.viewFalloff, verts_, connected_);
        for (uint32_t v : verts_) {
            float w = brushFalloff(brushDistance(mesh.positions[v], c, view, p.viewFalloff), p.radius, p.focal) *
                      std::clamp(p.strength * 2.0f, 0.0f, 1.0f);
            if (w <= 0.0f) continue;
            grabVerts_.push_back(v);
            grabWeights_.push_back(w);
        }
        grabMirrors_.push_back({grabVerts_.size(), m.m});
        const std::span<const uint32_t> leaves = bvh.queriedLeaves();
        grabLeaves_.insert(grabLeaves_.end(), leaves.begin(), leaves.end());
    }
    // Every vertex and leaf the mirrors move, once each (each mirror's list is sorted already).
    grabAll_ = grabVerts_;
    for (size_t i = 1; i < grabMirrors_.size(); ++i)
        std::inplace_merge(grabAll_.begin(), grabAll_.begin() + std::ptrdiff_t(grabMirrors_[i - 1].first),
                           grabAll_.begin() + std::ptrdiff_t(grabMirrors_[i].first));
    grabAll_.erase(std::unique(grabAll_.begin(), grabAll_.end()), grabAll_.end());
    if (grabMirrors_.size() > 1) {
        std::sort(grabLeaves_.begin(), grabLeaves_.end());
        grabLeaves_.erase(std::unique(grabLeaves_.begin(), grabLeaves_.end()), grabLeaves_.end());
    }
}

void Sculptor::grab(Mesh& mesh, BVH& bvh, const glm::vec3& delta) {
    grabMove(mesh, delta);
    grabFinish(mesh, bvh);
}

void Sculptor::grabMove(Mesh& mesh, const glm::vec3& delta) {
    if (micro_.active() && microMesh_ == &mesh) { // (a micro level moves and stitches in one go)
        const double k0 = micro_.kernelMs, s0 = micro_.stitchMs;
        micro_.grab(mesh, delta);
        MicroDab d;
        d.op = MicroDab::Op::Grab, d.type = BrushType::Move, d.delta = delta;
        inkDab(d);
        timings.kernelMs += micro_.kernelMs - k0;
        timings.normalsMs += micro_.stitchMs - s0;
        ++timings.dabs;
        return;
    }
    if (grabVerts_.empty()) return;
    auto clock = std::chrono::steady_clock::now();
    if (dynTopo_.active()) { // (the remesher records each touch: the moves one by one)
        // One mirror after another (a vertex may be in two), each in parallel.
        size_t begin = 0;
        for (const auto& [end, m] : grabMirrors_) {
            const glm::vec3 dm = m * delta;
            scratch_.resize(end - begin);
            poolFor(end - begin, [&](size_t b, size_t e) {
                for (size_t i = b; i < e; ++i) scratch_[i] = dm * grabWeights_[begin + i];
            });
            displaceAll(mesh, std::span(grabVerts_).subspan(begin, end - begin), scratch_);
            begin = end;
        }
    } else {
        // Each vertex from where it was at the press by its weight times the whole move so far (grabTotal_): one
        // call per pointer sample or one per frame (grabMoves) leaves the same bits, in time for the region alone.
        if (!grabCaptured_) captureGrab(mesh);
        grabTotal_ += delta;
        std::vector<glm::vec3>& target = mesh.layerTarget(layer_);
        const bool layered = !mesh.layers.empty();
        const float weight = mesh.layerTargetWeight(layer_);
        size_t begin = 0;
        for (const auto& [end, m] : grabMirrors_) { // one mirror after another: a vertex in two takes both, in order
            const glm::vec3 dm = m * grabTotal_;
            poolFor(end - begin, [&](size_t b, size_t e) {
                for (size_t i = begin + b; i < begin + e; ++i) {
                    const float k = grabInfluence_[i];
                    if (k <= 0.0f) continue;
                    const uint32_t v = grabVerts_[i];
                    const glm::vec3 dk = (dm * grabWeights_[i]) * k;
                    if (grabFirst_[i]) {
                        mesh.positions[v] = grabStart_[i] + dk;
                        if (layered) target[v] = grabStartTarget_[i] + dk / weight;
                    } else {
                        mesh.positions[v] += dk;
                        if (layered) target[v] += dk / weight;
                    }
                }
            }, 4096);
            begin = end;
        }
    }
    timings.kernelMs += lapMs(clock);
    ++timings.dabs;
    timings.vertices += grabAll_.size();
}

void Sculptor::captureGrab(Mesh& mesh) {
    grabCaptured_ = true;
    // The stroke's record of the region (displaceAll with no move: the start positions, each vertex's influence).
    const size_t n = grabVerts_.size();
    grabInfluence_.resize(n);
    size_t begin = 0;
    for (const auto& [end, m] : grabMirrors_) {
        scratch_.assign(end - begin, glm::vec3(0.0f));
        displaceAll(mesh, std::span(grabVerts_).subspan(begin, end - begin), scratch_);
        std::copy(influence_.begin(), influence_.begin() + std::ptrdiff_t(end - begin), grabInfluence_.begin() + std::ptrdiff_t(begin));
        begin = end;
    }
    const std::vector<glm::vec3>& target = mesh.layerTarget(layer_);
    grabStart_.resize(n);
    grabStartTarget_.resize(mesh.layers.empty() ? 0 : n);
    grabFirst_.assign(n, 1);
    std::vector<uint64_t> seen;
    if (grabMirrors_.size() > 1) seen.assign((mesh.vertexCount() + 63) / 64, 0);
    for (size_t i = 0; i < n; ++i) {
        const uint32_t v = grabVerts_[i];
        if (!seen.empty()) {
            grabFirst_[i] = !((seen[v >> 6] >> (v & 63)) & 1);
            seen[v >> 6] |= uint64_t(1) << (v & 63);
        }
        grabStart_[i] = mesh.positions[v];
        if (!grabStartTarget_.empty()) grabStartTarget_[i] = target[v];
    }
}

void Sculptor::grabMoves(Mesh& mesh, std::span<const glm::vec3> deltas) {
    if (deltas.empty()) return;
    if (grabVerts_.empty() || (micro_.active() && microMesh_ == &mesh) || dynTopo_.active()) {
        for (const glm::vec3& d : deltas) grabMove(mesh, d);
        return;
    }
    // The whole move so far, sample by sample as grabMove adds it, then the region once.
    for (size_t i = 0; i + 1 < deltas.size(); ++i) grabTotal_ += deltas[i];
    timings.dabs += deltas.size() - 1;
    grabMove(mesh, deltas.back());
}

void Sculptor::grabFinish(Mesh& mesh, BVH& bvh) {
    if (grabVerts_.empty() || (micro_.active() && microMesh_ == &mesh)) return;
    auto clock = std::chrono::steady_clock::now();
    mesh.updateNormals(grabAll_);
    timings.normalsMs += lapMs(clock);
    bvh.refitLeaves(mesh, grabLeaves_);
    timings.bvhMs += lapMs(clock);
}

// Puts every vertex the current stroke moved back to its stroke-start position.
void Sculptor::restoreStroke(Mesh& mesh, BVH& bvh) {
    if (!active_) return;
    if (micro_.active() && microMesh_ == &mesh) {
        micro_.restore(mesh);
        MicroDab d;
        d.op = MicroDab::Op::Restore;
        inkDab(d);
        return;
    }
    topologyHeld_ = true; // (positions come back, topology wouldn't)
    // Vertex paint laid down by the stroke so far (Paint brush / RGB while sculpting).
    if (!record_.colors.empty() && mesh.hasColors()) {
        const auto& ci = record_.colors.indices;
        const int layer = record_.paintLayer;
        PaintLayer* pl = layer >= 0 && layer < int(mesh.paintLayers.size()) ? &mesh.paintLayers[size_t(layer)] : nullptr;
        auto& target = mesh.paintLayers.empty() ? mesh.colors : pl ? pl->color : mesh.paintBase;
        for (size_t i = 0; i < ci.size(); ++i) target[ci[i]] = record_.colors.values[i];
        if (pl)
            for (size_t i = 0; i < record_.paintAlpha.indices.size(); ++i)
                pl->alpha[record_.paintAlpha.indices[i]] = record_.paintAlpha.values[i];
        compositePaint(mesh, ci); // marks dirty: paint-only strokes return before updateNormals marks anything
    }
    if (record_.positions.empty()) return;
    auto& target = mesh.layerTarget(layer_);
    const auto& idx = record_.positions.indices;
    for (size_t i = 0; i < idx.size(); ++i) target[idx[i]] = record_.positions.values[i];
    mesh.evaluate(idx);
    mesh.updateNormals(idx);
    bvh.refit(mesh, idx);
    // The Layer brush starts over too.
    if (layerEpoch_ != 0 && ++layerEpoch_ == 0) {
        std::fill(layerStamp_.begin(), layerStamp_.end(), 0);
        layerEpoch_ = 1;
    }
}

// Sizes the Layer brush state and starts a fresh height map for each new stroke.
void Sculptor::prepareLayer(const Mesh& mesh, float height) {
    if (layerStamp_.size() != mesh.vertexCount()) {
        layerStamp_.assign(mesh.vertexCount(), 0);
        layerDone_.assign(mesh.vertexCount(), 0.0f);
        layerDir_.assign(mesh.vertexCount(), glm::vec3(0.0f));
        layerEpoch_ = 0;
    }
    if (layerEpoch_ != 0 && layerSeenStroke_ == strokeId_) return;
    layerSeenStroke_ = strokeId_;
    layerHeight_ = height;
    if (++layerEpoch_ == 0) {
        std::fill(layerStamp_.begin(), layerStamp_.end(), 0);
        layerEpoch_ = 1;
    }
}

std::function<float(const glm::vec3&)> microPatternTexture(int pattern, float scale, float amount, float radius, const SymCopy& m) {
    const float freq = 1.0f / std::max(radius * scale, 1e-6f);
    const auto kind = BrushRecipe::Pattern(pattern);
    return [kind, freq, amount, m](const glm::vec3& x) { return glm::mix(1.0f, samplePattern(kind, m.inversePoint(x) * freq), amount); };
}

} // namespace sf
