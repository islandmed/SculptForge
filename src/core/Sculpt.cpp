// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/Sculpt.h"

#include "core/AtomicRef.h"
#include "core/BrushAlpha.h"
#include "core/ThreadPool.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <unordered_map>

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
    case BrushType::DamStandard: return "DamStandard";
    case BrushType::ClayBuildup: return "ClayBuildup";
    case BrushType::SnakeHook: return "SnakeHook";
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
    default: return "?";
    }
}

const char* brushLabel(BrushType type) {
    switch (type) {
    case BrushType::DamStandard: return "Sharp Crease";
    case BrushType::ClayBuildup: return "Clay Block";
    case BrushType::SnakeHook: return "Tendril";
    case BrushType::KelvinSnake: return "Elastic Tendril";
    default: return brushName(type);
    }
}

bool brushFromName(std::string_view name, BrushType& out) {
    auto squash = [](std::string_view s) {
        std::string r;
        for (unsigned char c : s)
            if (c != ' ' && c != '_' && c != '-') r += char(std::tolower(c));
        return r;
    };
    const std::string want = squash(name);
    if (want.empty()) return false;
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
                 bool connectedOnly, bool viewFalloff, std::vector<uint32_t>& out, ConnectedScratch& scratch) {
    if (viewFalloff) {
        bvh.queryVerticesCylinder(mesh, center, viewDir, radius, out);
        if (mesh.hasHidden()) std::erase_if(out, [&](uint32_t v) { return mesh.vertexHidden(v); });
    } else {
        brushVertices(mesh, bvh, center, radius, out);
    }
    if (connectedOnly) keepConnected(mesh, center, out, scratch);
}

namespace {

glm::vec3 projectOnPlane(const glm::vec3& v, const glm::vec3& n) { return v - n * glm::dot(v, n); }

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

// Weighted sums over a brush region: per-block partials added in block order, so the result doesn't
// depend on the thread count (and a region of one block sums exactly like a plain loop).
struct AreaSums {
    glm::vec3 normal{0.0f}, center{0.0f};
    float weight = 0.0f;
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
    draft_ = draftStrokes && !dyntopo.enabled;
    draftStrokes = false; // (asked for per stroke)
    draftLeaves_.clear();
    strokeDiagonal_ = 0.0f;
    lastRemesh_ = false;
    topologyHeld_ = false;
    microMesh_ = nullptr;
    active_ = true;
    return true;
}

bool Sculptor::microReady(Mesh& mesh) {
    if (!mesh.micro || !microMatches(mesh)) return false;
    if (!micro_.active() || microMesh_ != &mesh) {
        micro_.begin(mesh);
        microMesh_ = &mesh;
    }
    return true;
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

void Sculptor::displaceAll(Mesh& mesh, std::span<const uint32_t> verts, std::span<const glm::vec3> d, bool influence) {
    const size_t n = verts.size();
    if (dynTopo_.active()) // the remesher's record keeps them (see touch); their bits then keep them out of ours
        for (uint32_t v : verts)
            if (markBit(touched_, v)) dynTopo_.recordStrokeStart(v);
    constexpr size_t kBlock = 4096;
    influence_.resize(n);
    firstTouch_.resize(n);
    blockFirst_.assign((n + kBlock - 1) / kBlock + 1, 0);
    poolFor(n, [&](size_t b, size_t e) {
        uint32_t count = 0;
        for (size_t i = b; i < e; ++i) {
            const float k = influence ? vertexInfluence(mesh, verts[i]) : 1.0f;
            influence_[i] = k;
            firstTouch_[i] = k > 0.0f && markBit(touched_, verts[i]);
            count += firstTouch_[i];
        }
        blockFirst_[b / kBlock + 1] = count;
    }, kBlock);
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

void Sculptor::flushDraft(Mesh& mesh, const BVH& bvh) {
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
    if (micro_.active() && microMesh_) record_.micro = micro_.end(*microMesh_);
    microMesh_ = nullptr;
    clearRecorded();
    active_ = false;
    grabVerts_.clear();
    grabAll_.clear();
    return std::move(record_);
}

int groupBorderNeighbours(const Mesh& mesh, uint32_t v, uint32_t out[3]) {
    if (!mesh.hasPolygroups()) return 0;
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
            const uint32_t g = mesh.polygroupAt(t);
            bool border = false;
            for (uint32_t u : tris) {
                if (u == t) continue;
                const glm::uvec3 o = mesh.triangles[u];
                if ((o.x == n || o.y == n || o.z == n) && mesh.polygroupAt(u) != g) border = true;
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
    // Morph: nothing without a target (Tool > Selection Sets > Morph Target); not on micro levels (no grid shapes).
    if (type == BrushType::Morph && (mesh.micro || !morphTarget(mesh))) return;
    if (isKelvinletBrush(type)) { // Kelvinlet.cpp (no remeshing: they move the surface, as Move does)
        dabKelvinlet(mesh, bvh, type, p, sym);
        return;
    }
    if (microReady(mesh)) { // a micro level: the grid takes the dab (core/MicroSculpt.cpp)
        const double k0 = micro_.findMs + micro_.kernelMs, s0 = micro_.stitchMs;
        const uint64_t pts = micro_.points;
        for (const SymCopy& m : copies(sym))
            micro_.dab(mesh, bvh, type, m.point(p.center), m.dir(p.viewDir), p.radius, p.strength * std::clamp(p.pressure, 0.0f, 1.0f),
                       p.invert, p.focal, m.dir(p.alphaUp), false, 0.0f, p.alpha);
        timings.kernelMs += micro_.findMs + micro_.kernelMs - k0;
        timings.normalsMs += micro_.stitchMs - s0;
        timings.vertices += micro_.points - pts;
        ++timings.dabs;
        return;
    }
    if (type != BrushType::Move && type != BrushType::Morph) { // (Morph needs the vertices the target has)
        const bool useAlpha = p.alpha && !p.alpha->empty(); // (the reach dabSingle gives the brush)
        const float reach = useAlpha ? kAlphaReach : type == BrushType::ClayBuildup ? 1.3f : 1.0f;
        const float scale = type == BrushType::Smooth ? dyntopoSmoothScale(dyntopo) : 1.0f;
        if (scale > 0.0f) remeshDab(mesh, bvh, p, sym, scale, reach);
        else lastRemesh_ = false; // (Smooth only: no repair pass either)
    }
    dabVerts_.clear();
    dabLeaves_.clear();
    for (const SymCopy& m : copies(sym)) {
        frameWanted_ = m.identity(); // (the unmirrored pass records lastFrame)
        const DabParams mp = m.identity() ? p : transformed(p, m);
        if (type == BrushType::Vdm) dabVdmSingle(mesh, bvh, mp, m.handedness());
        else dabSingle(mesh, bvh, type, mp);
    }
    updateDabNormals(mesh, bvh);
    if (type != BrushType::Move) repairDab(mesh, bvh);
}

void Sculptor::dabSingle(Mesh& mesh, BVH& bvh, BrushType type, const DabParams& p) {
    auto clock = std::chrono::steady_clock::now();
    // Alphas and the square ClayBuildup tip reach into the corners of their square.
    const bool useAlpha = p.alpha && !p.alpha->empty();
    const float reach = useAlpha ? kAlphaReach : type == BrushType::ClayBuildup ? 1.3f : 1.0f;
    brushRegion(mesh, bvh, p.center, p.viewDir, p.radius * reach, p.connectedOnly, p.viewFalloff, verts_, connected_);
    if (verts_.empty()) return;

    auto& pos = mesh.positions;
    const auto& nrm = mesh.normals;

    if (p.frontFacesOnly) {
        std::erase_if(verts_, [&](uint32_t v) { return glm::dot(nrm[v], p.viewDir) > 0.0f; });
        if (verts_.empty()) return;
    }

    // Weighted area normal and centre of the brush region.
    const AreaSums sums = areaSums(verts_.size(), [&](size_t i, AreaSums& acc) {
        const uint32_t v = verts_[i];
        float w = brushFalloff(brushDistance(p, pos[v]), p.radius, p.focal);
        acc.normal += nrm[v] * w;
        acc.center += pos[v] * w;
        acc.weight += w;
    });
    glm::vec3 areaNormal = sums.normal, areaCenter = sums.center;
    const float wsum = sums.weight;
    if (wsum <= 0.0f) return;
    areaCenter /= wsum;
    float nlen = glm::length(areaNormal);
    areaNormal = nlen > 1e-12f ? areaNormal / nlen : -p.viewDir;
    if (frameWanted_) {
        lastFrame = {p.center, areaNormal, areaCenter, true};
        frameWanted_ = false;
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

    scratch_.resize(verts_.size());
    // Each vertex's offset depends only on the positions before the dab (and, for Layer, on its own state).
    poolFor(verts_.size(), [&](size_t begin, size_t end) {
    for (size_t i = begin; i < end; ++i) {
        const uint32_t v = verts_[i];
        const glm::vec3& x = pos[v];
        const glm::vec3 o = x - p.center;
        const float f = useAlpha ? p.alpha->stamp(glm::dot(o, right) / r, glm::dot(o, up) / r, p.focal)
                                 : brushFalloff(brushDistance(p, x), r, p.focal);
        glm::vec3 d(0.0f);

        switch (type) {
        case BrushType::Standard:
            d = areaNormal * (sign * s * r * 0.06f * f);
            break;
        case BrushType::Inflate:
            d = nrm[v] * (sign * s * r * 0.05f * f);
            break;
        case BrushType::Smooth: {
            if (p.groupBorders) { // along the border, on the tangent plane; junctions and inner vertices stay
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
            break;
        }
        case BrushType::Flatten: {
            float h = glm::dot(x - areaCenter, areaNormal);
            d = -areaNormal * (h * s * f * (p.invert ? -0.5f : 1.0f));
            break;
        }
        case BrushType::Clay: {
            // Raise a plane slightly above the surface and pull everything below up to it.
            glm::vec3 planePoint = areaCenter + areaNormal * (sign * r * 0.05f * s);
            float h = glm::dot(x - planePoint, areaNormal);
            if (sign * h < 0.0f) d = -areaNormal * (h * f * clayPull(s));
            break;
        }
        case BrushType::Crease: {
            // Carves a sharp valley by default (raises a ridge when inverted).
            float sharp = f * f;
            d = areaNormal * (-sign * s * r * 0.05f * sharp);
            d += projectOnPlane(p.center - x, areaNormal) * (0.25f * s * f);
            break;
        }
        case BrushType::Pinch:
            d = projectOnPlane(p.center - x, areaNormal) * (sign * 0.2f * s * f);
            break;
        case BrushType::DamStandard: {
            // Narrow, deep V-groove (a ridge when inverted); the sides are always
            // pinched in toward the stroke line, so the crease stays sharp.
            const float core = f * f * f * f;
            d = areaNormal * (-sign * s * r * 0.08f * core);
            glm::vec3 pull = projectOnPlane(p.center - x, areaNormal);
            pull -= strokeDir * glm::dot(pull, strokeDir);
            d += pull * (0.3f * s * f);
            break;
        }
        case BrushType::ClayBuildup: {
            // Square tip (rounded corners) with a flat top: a raised plane that
            // everything below is pulled up to, so strokes build up in slabs.
            float w = f;
            if (!useAlpha) {
                const float cheb = std::max(std::abs(glm::dot(o, right)), std::abs(glm::dot(o, up))) / r;
                const float corner = std::clamp((1.3f - glm::length(o) / r) / 0.3f, 0.0f, 1.0f);
                const float edge = std::clamp((1.0f - cheb) / 0.35f, 0.0f, 1.0f);
                w = edge * edge * (3.0f - 2.0f * edge) * corner;
                // Focal Shift (#60) keeps this tip at 0: below, harder edges and a broader slab; above, it fades
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
            d += side * (0.1f * s * f * std::min(1.0f, strokeLen / (0.1f * r)));
            break;
        }
        case BrushType::Trim: {
            // Trim Dynamic: a plane just below the area cuts off whatever sticks out
            // above it (fills up to it when inverted). sqrt falloff for a crisp edge.
            glm::vec3 planePoint = areaCenter - areaNormal * (sign * r * 0.02f * s);
            float h = glm::dot(x - planePoint, areaNormal);
            if (sign * h > 0.0f) d = -areaNormal * (h * std::min(1.0f, std::sqrt(f) * (0.5f + s)));
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
            const float target = sign * layerHeight_ * std::clamp(p.pressure, 0.0f, 1.0f) * f;
            if (std::abs(target) > std::abs(layerDone_[v])) {
                d = layerDir_[v] * (target - layerDone_[v]);
                layerDone_[v] = target;
            }
            break;
        }
        case BrushType::Nudge:
            // Slide the surface sideways along the stroke without lifting it.
            d = projectOnPlane(p.strokeDelta, nrm[v]) * (f * std::min(1.0f, 0.4f + s));
            break;
        case BrushType::Morph:
            // Toward the morph target (away from it, by a quarter as much, with Alt).
            d = ((*morph)[v] - x) * (p.invert ? -0.25f * s * f : std::min(1.0f, s * f));
            break;
        default:
            break;
        }
        scratch_[i] = d;
    }
    });

    displaceAll(mesh, verts_, scratch_);
    finishDab(mesh, bvh, clock);
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
void Sculptor::finishDab(Mesh& mesh, BVH& bvh, std::chrono::steady_clock::time_point clock) {
    timings.kernelMs += lapMs(clock);
    bvh.refitQueried(mesh);
    timings.bvhMs += lapMs(clock);
    ++timings.dabs;
    timings.vertices += verts_.size();
    dabVerts_.insert(dabVerts_.end(), verts_.begin(), verts_.end()); // a vertex twice is fine: updateNormals dedups
    const auto leaves = bvh.queriedLeaves(); // (the query that found verts_)
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
    if (mesh.micro && microMatches(mesh)) { // recipes aren't grid brushes: a micro level gets the Standard dab
        dab(mesh, bvh, BrushType::Standard, p, sym);
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
    for (const SymCopy& m : copies(sym)) {
        frameWanted_ = m.identity(); // (the unmirrored pass records lastFrame)
        const DabParams mp = m.identity() ? base : transformed(base, m);
        dabRecipeSingle(mesh, bvh, recipe, mp);
    }
    updateDabNormals(mesh, bvh);
    repairDab(mesh, bvh);
}

void Sculptor::dabRecipeSingle(Mesh& mesh, BVH& bvh, const BrushRecipe& r, const DabParams& p) {
    auto clock = std::chrono::steady_clock::now();
    // An alpha (if any) modulates the recipe's tip, oriented on the view-facing plane. Focal Shift shifts the tip
    // (0 keeps the recipe's own Hardness / Tip Sharpness), and so the alpha with it. With an alpha the region
    // reaches the stamp's corners and, below 0, the round tip opens up toward 1 as alphaFalloff does, so at -100
    // the whole alpha shows (#75; the built-in brushes got this in #60).
    const bool useAlpha = p.alpha && !p.alpha->empty();
    brushRegion(mesh, bvh, p.center, p.viewDir, p.radius * (useAlpha ? kAlphaReach : 1.0f), p.connectedOnly,
                p.viewFalloff, verts_, connected_);
    if (verts_.empty()) return;
    const auto& pos = mesh.positions;
    const auto& nrm = mesh.normals;
    glm::vec3 aUp = projectOnPlane(p.alphaUp, p.viewDir);
    aUp = glm::length(aUp) > 1e-6f ? glm::normalize(aUp) : glm::vec3(0, 1, 0);
    const glm::vec3 aRight = glm::cross(aUp, -p.viewDir);
    auto alphaWeight = [&](const glm::vec3& x) {
        const glm::vec3 o = x - p.center;
        return p.alpha->sample(glm::dot(o, aRight) / p.radius, glm::dot(o, aUp) / p.radius);
    };
    if (p.frontFacesOnly) {
        std::erase_if(verts_, [&](uint32_t v) { return glm::dot(nrm[v], p.viewDir) > 0.0f; });
        if (verts_.empty()) return;
    }

    const float rad = p.radius;
    const float s = std::clamp(p.strength, 0.0f, 1.0f) * p.pressure;
    const float sign = p.invert ? -1.0f : 1.0f;

    // Per-vertex weight: tip shape, cavity mask and pattern.
    weights_.resize(verts_.size());
    const float edge = std::max(0.05f, 1.0f - r.hardness * 0.95f);
    const float exponent = 1.0f + r.tipSharpness * 5.0f;
    const float patternFreq = 1.0f / std::max(rad * r.patternScale, 1e-6f);
    const AreaSums sums = areaSums(verts_.size(), [&](size_t i, AreaSums& acc) {
        const uint32_t v = verts_[i];
        float t = std::clamp((1.0f - brushDistance(p, pos[v]) / rad) / edge, 0.0f, 1.0f);
        if (p.focal != 0.0f) t = std::pow(t, std::exp2(2.0f * std::clamp(p.focal, -1.0f, 1.0f))); // (brushFalloff's)
        float w = std::pow(t * t * (3.0f - 2.0f * t), exponent);
        if (useAlpha && p.focal < 0.0f) w += (1.0f - w) * std::min(-p.focal, 1.0f); // (alphaFalloff's)
        acc.normal += nrm[v] * w;
        acc.center += pos[v] * w;
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
            w *= glm::mix(1.0f, samplePattern(r.pattern, pos[v] * patternFreq), r.patternAmount);
        if (useAlpha) w *= alphaWeight(pos[v]);
        weights_[i] = w;
    });
    glm::vec3 areaNormal = sums.normal, areaCenter = sums.center;
    const float wsum = sums.weight;
    if (wsum <= 0.0f) return;
    areaCenter /= wsum;
    float nlen = glm::length(areaNormal);
    areaNormal = nlen > 1e-12f ? areaNormal / nlen : -p.viewDir;
    if (frameWanted_) {
        lastFrame = {p.center, areaNormal, areaCenter, true};
        frameWanted_ = false;
    }

    glm::vec3 strokeDir = p.strokeDelta;
    float strokeLen = glm::length(strokeDir);
    strokeDir = strokeLen > 1e-12f ? strokeDir / strokeLen : glm::vec3(0.0f);

    scratch_.resize(verts_.size());
    poolFor(verts_.size(), [&](size_t begin, size_t end) {
    for (size_t i = begin; i < end; ++i) {
        const uint32_t v = verts_[i];
        const glm::vec3& x = pos[v];
        const float f = weights_[i];
        glm::vec3 dir;
        switch (r.direction) {
        case BrushRecipe::Direction::Inflate: dir = nrm[v]; break;
        case BrushRecipe::Direction::View: dir = -p.viewDir; break;
        case BrushRecipe::Direction::Stroke: dir = strokeDir; break;
        default: dir = areaNormal; break;
        }
        glm::vec3 d = dir * (sign * r.depth * 0.12f * rad * s * f);

        const float h = glm::dot(x - areaCenter, areaNormal);
        switch (r.plane) {
        case BrushRecipe::Plane::Clay: {
            glm::vec3 planePoint = areaCenter + areaNormal * (sign * rad * 0.06f * s);
            float hc = glm::dot(x - planePoint, areaNormal);
            if (sign * hc < 0.0f) d -= areaNormal * (hc * f * r.planeAmount * clayPull(s));
            break;
        }
        case BrushRecipe::Plane::Flatten: d -= areaNormal * (h * s * f * r.planeAmount); break;
        case BrushRecipe::Plane::Fill:
            if (h < 0.0f) d -= areaNormal * (h * s * f * r.planeAmount);
            break;
        case BrushRecipe::Plane::Scrape:
            if (h > 0.0f) d -= areaNormal * (h * s * f * r.planeAmount);
            break;
        default: break;
        }
        if (r.pinch != 0.0f) d += projectOnPlane(p.center - x, areaNormal) * (r.pinch * 0.25f * s * f);
        if (r.drag > 0.0f) d += p.strokeDelta * (r.drag * f);
        scratch_[i] = d;
    }
    });
    displaceAll(mesh, verts_, scratch_);

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
    finishDab(mesh, bvh, clock);
}

void Sculptor::beginGrab(Mesh& mesh, BVH& bvh, const DabParams& p, Symmetry sym) {
    grabVerts_.clear();
    grabWeights_.clear();
    grabMirrors_.clear();
    grabAll_.clear();
    grabLeaves_.clear();
    if (!active_) return;
    // A micro level (core/Micro.h): Move deforms the base cage below it (dual scale: the detail is relative to the
    // base, so it rides along in its local frame), unless asked to move the micro surface along its normals.
    if (microMoveAlongNormals && microReady(mesh)) {
        for (const SymCopy& m : copies(sym)) micro_.beginGrab(mesh, bvh, m.point(p.center), p.radius, p.focal, m.m);
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
        timings.kernelMs += micro_.kernelMs - k0;
        timings.normalsMs += micro_.stitchMs - s0;
        ++timings.dabs;
        return;
    }
    if (grabVerts_.empty()) return;
    auto clock = std::chrono::steady_clock::now();
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
    timings.kernelMs += lapMs(clock);
    ++timings.dabs;
    timings.vertices += grabAll_.size();
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
        return;
    }
    topologyHeld_ = true; // (positions come back, topology wouldn't)
    // Polypaint laid down by the stroke so far (Paint brush / RGB while sculpting).
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

} // namespace sf
