// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The Cloth brush (docs/design/cloth.md, phase 1): a stroke simulates the surface near it as fabric (core/Cloth.h).
//
// The simulated region is the vertices within `reach` brush radii of the dab centres (every symmetry copy), each
// with its stroke-start position as rest. It is rebuilt when the brush has moved half a radius from where it was
// built, growing along the stroke (the earlier region stays in while it fits the budget); between rebuilds the
// simulation keeps its velocities. The region's
// outer ring is pinned (its triangles reach outside it), masked vertices are pinned by their mask, so nothing
// outside moves and the mask holds. Each dab drives the vertices under the brush (Drag: along the stroke; Push:
// along the normal; Grab: from where they were at the stroke's start by the brush's whole travel; Expand / Pinch:
// along the surface from / to the centre; Inflate: grows their rest lengths and lifts them along their normals) as
// soft pins weighted by the falloff, runs `steps` frames, and writes the region back through moveVertices (undo,
// sculpt layers, normals, BVH). Soft body adds a weak pin toward the stroke-start shape everywhere, so moved cloth
// springs back; self-collision keeps the region's particles half an edge apart, so folds don't pass through.
//
// The brush is scale-free: whatever the mesh's size and density, the fabric is set from the region's mean edge
// length so a particle weighs about 1 and each preset's stretch, compression and bending keep the same balance
// (Drape uses real units instead: a Part's metres).
#include "core/Sculpt.h"

#include "core/Cloth.h"
#include "core/ThreadPool.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <unordered_map>

namespace sf {

namespace {

const char* const kModeNames[] = {"drag", "push", "settle", "grab", "expand", "pinch", "inflate"};
const char* const kModeLabels[] = {"Drag", "Push", "Settle", "Grab", "Expand", "Pinch", "Inflate"};
static_assert(std::size(kModeNames) == size_t(ClothBrushMode::Count) && std::size(kModeLabels) == size_t(ClothBrushMode::Count));

// Per preset, the brush's compliance balance: stretch, compression and bending against a unit particle mass at
// one substep (alpha / h^2 / w). Compression far softer than stretch so a pushed region buckles.
struct BrushFabric {
    float stretch, compress, bend, damping;
};
// (Bending stiff enough that a fold spans several edges, and compression soft enough that squeezed cloth first
// shortens a little: otherwise it crumpled into ripples a few edges wide, which read as noise, not fabric.)
const BrushFabric kBrushFabrics[] = {
    {1e-3f, 8.0f, 0.25f, 0.30f},   // silk: fine, many folds
    {1e-3f, 4.0f, 0.06f, 0.30f},   // cotton
    {1e-3f, 2.0f, 0.01f, 0.35f},   // denim: broad folds
    {1e-3f, 1.2f, 0.002f, 0.40f},  // leather: few, wide folds
};

constexpr float kFrame = 1.0f / 60.0f;
constexpr int kSubsteps = 8;
constexpr int kIterations = 4;

} // namespace

const char* clothModeName(ClothBrushMode mode) { return kModeNames[std::min(size_t(mode), std::size(kModeNames) - 1)]; }
const char* clothModeLabel(ClothBrushMode mode) { return kModeLabels[std::min(size_t(mode), std::size(kModeLabels) - 1)]; }
bool clothModeFromName(std::string_view name, ClothBrushMode& out) {
    for (size_t i = 0; i < std::size(kModeNames); ++i) {
        std::string_view a = kModeNames[i], b = kModeLabels[i];
        auto same = [&](std::string_view w) {
            if (w.size() != name.size()) return false;
            for (size_t k = 0; k < w.size(); ++k)
                if (std::tolower((unsigned char)w[k]) != std::tolower((unsigned char)name[k])) return false;
            return true;
        };
        if (same(a) || same(b)) {
            out = ClothBrushMode(i);
            return true;
        }
    }
    return false;
}

struct ClothStroke {
    uint64_t stroke = ~uint64_t(0);
    std::vector<uint32_t> verts;               // the region, increasing
    std::unordered_map<uint32_t, glm::vec3> rest; // stroke-start positions of every vertex a region held
    std::vector<uint32_t> local;               // per mesh vertex: index in `verts` + 1 (0 = outside)
    std::vector<float> basePin;                // per region vertex: the ring, the band inside it, the mask
    std::vector<glm::vec3> restLocal;          // per region vertex: its stroke-start position (the pins' target)
    std::vector<glm::vec3> builtAt;            // the dab centres (per symmetry copy) the region was built around
    float builtRadius = 0.0f, meanEdge = 0.0f;
    ClothSim sim;
    std::vector<uint32_t> moved;
    std::vector<glm::vec3> delta, start;
    std::vector<glm::vec3> driveMove;          // per region vertex, this dab's summed move (every symmetry copy)
    std::vector<float> driveWeight;
    std::vector<uint32_t> ringOffset, ring; // per region vertex, its neighbours in the region (CSR, from the edges)
    std::vector<glm::vec3> anchor;          // Grab: the first dab's centre per symmetry copy
    std::unordered_map<uint32_t, float> grow; // Inflate: per mesh vertex, its rest-length scale (1 = as at the start)
};

size_t Sculptor::clothRegion() const { return cloth_ ? cloth_->verts.size() : 0; }

void Sculptor::dabCloth(Mesh& mesh, BVH& bvh, const DabParams& p, Symmetry sym) {
    if (!active_ || mesh.micro || mesh.vertexCount() == 0) return; // (micro levels: no vertices to simulate)
    auto clock = std::chrono::steady_clock::now();
    if (!cloth_) cloth_ = std::make_shared<ClothStroke>();
    ClothStroke& cs = *cloth_;
    if (cs.stroke != strokeId_) {
        cs = ClothStroke{};
        cs.stroke = strokeId_;
    }
    const std::vector<SymCopy> mirrors = copies(sym);
    if (cs.anchor.size() != mirrors.size()) {
        cs.anchor.clear();
        for (const SymCopy& m : mirrors) cs.anchor.push_back(m.point(p.center));
    }
    const float r = std::max(p.radius, 1e-6f);
    const ClothBrushSettings& set = cloth;

    // (Re)build the region when the brush has moved on.
    bool rebuild = cs.verts.empty() || cs.builtAt.size() != mirrors.size() || std::abs(cs.builtRadius - r) > 0.25f * cs.builtRadius;
    for (size_t m = 0; !rebuild && m < mirrors.size(); ++m)
        rebuild = glm::distance(mirrors[m].point(p.center), cs.builtAt[m]) > 0.5f * r;
    if (rebuild) {
        float reach = r * std::clamp(set.reach, 1.5f, 4.0f);
        std::vector<uint32_t> region, found;
        for (int attempt = 0; attempt < 4; ++attempt) {
            region.clear();
            for (const SymCopy& m : mirrors) {
                brushVertices(mesh, bvh, m.point(p.center), reach, found);
                region.insert(region.end(), found.begin(), found.end());
            }
            if (set.mode == ClothBrushMode::Grab) // (what was grabbed stays in, wherever the brush has gone)
                for (const glm::vec3& a : cs.anchor) {
                    brushVertices(mesh, bvh, a, r * 1.05f, found);
                    region.insert(region.end(), found.begin(), found.end());
                }
            // The stroke's earlier region stays in (up to the budget): cloth the brush already moved keeps
            // simulating instead of freezing beside cloth that is still being dragged (that tore it).
            if (region.size() + cs.verts.size() <= std::max<size_t>(set.vertexBudget, 1000))
                region.insert(region.end(), cs.verts.begin(), cs.verts.end());
            std::sort(region.begin(), region.end());
            region.erase(std::unique(region.begin(), region.end()), region.end());
            const size_t budget = std::max<size_t>(set.vertexBudget, 1000);
            if (region.size() <= budget || reach <= r * 1.05f) break;
            reach = std::max(r * 1.05f, reach * std::sqrt(float(budget) / float(region.size())) * 0.95f);
        }
        if (region.size() < 3) return;
        cs.local.assign(mesh.vertexCount(), 0);
        for (size_t i = 0; i < region.size(); ++i) cs.local[region[i]] = uint32_t(i + 1);
        // Triangles with all three corners in the region; a corner of a triangle that reaches outside is the ring.
        ClothSetup setup;
        setup.positions.resize(region.size());
        setup.rest.resize(region.size());
        setup.pin.assign(region.size(), 0.0f);
        for (size_t i = 0; i < region.size(); ++i) {
            const uint32_t v = region[i];
            setup.positions[i] = mesh.positions[v];
            auto it = cs.rest.try_emplace(v, mesh.positions[v]).first; // (stroke start: outside, it never moved)
            setup.rest[i] = it->second;
            for (uint32_t t : mesh.vertexTriangles(v)) {
                if (mesh.triangleHidden(t)) continue;
                const glm::uvec3 tri = mesh.triangles[t];
                const uint32_t a = cs.local[tri.x], b = cs.local[tri.y], c = cs.local[tri.z];
                if (!a || !b || !c) {
                    setup.pin[i] = 1.0f;
                    continue;
                }
                if (std::min({tri.x, tri.y, tri.z}) == v) setup.triangles.push_back({a - 1, b - 1, c - 1});
            }
            setup.pin[i] = std::max(setup.pin[i], 1.0f - vertexInfluence(mesh, v));
        }
        if (setup.triangles.empty()) return;
        double edges = 0.0;
        for (const glm::uvec3& t : setup.triangles)
            edges += glm::length(setup.rest[t.x] - setup.rest[t.y]) + glm::length(setup.rest[t.y] - setup.rest[t.z]) +
                     glm::length(setup.rest[t.z] - setup.rest[t.x]);
        cs.meanEdge = std::max(1e-7f, float(edges / (3.0 * double(setup.triangles.size()))));
        const float L = cs.meanEdge, h = kFrame / float(kSubsteps);
        const BrushFabric& bf = kBrushFabrics[std::min(size_t(set.fabric), std::size(kBrushFabrics) - 1)];
        Fabric fabric = fabricPreset(set.fabric);
        fabric.density = 1.0f / (0.866f * L * L);       // a particle weighs about 1 (six triangles a third each)
        fabric.stretchCompliance = bf.stretch * h * h / L; // (alpha = compliance x rest)
        fabric.compressCompliance = bf.compress * h * h / L;
        fabric.bendCompliance = bf.bend * h * h / L;
        fabric.damping = bf.damping;
        fabric.thickness = 0.25f * L;
        ClothSettings settings;
        settings.frameSeconds = kFrame;
        settings.thinFeatures = false; // (per-dab cost: the brush keeps the plain contacts)
        settings.substeps = kSubsteps;
        settings.restFrames = 1 << 30; // (the brush never stops on rest)
        settings.gravity = glm::vec3(0.0f);
        cs.basePin = setup.pin;
        cs.restLocal = setup.rest;
        const std::vector<float> ringPin = setup.pin; // (before the band: the ring and the mask)
        cs.verts = std::move(region);
        if (!cs.sim.init(setup, fabric, settings)) {
            cs.verts.clear();
            return;
        }
        cs.ringOffset.assign(cs.verts.size() + 1, 0);
        for (const ClothSim::Edge& e : cs.sim.stretchEdges()) ++cs.ringOffset[e.a + 1], ++cs.ringOffset[e.b + 1];
        for (size_t i = 0; i < cs.verts.size(); ++i) cs.ringOffset[i + 1] += cs.ringOffset[i];
        cs.ring.resize(cs.ringOffset.back());
        {
            std::vector<uint32_t> fill(cs.ringOffset.begin(), cs.ringOffset.end() - 1);
            for (const ClothSim::Edge& e : cs.sim.stretchEdges()) cs.ring[fill[e.a]++] = e.b, cs.ring[fill[e.b]++] = e.a;
        }
        // A band inside the ring blends the solve back into the stroke-start shape: soft pins toward it, full at
        // the ring and fading inward over about a third of the reach (by edges from the ring), so the moved cloth
        // meets the untouched surface without a step.
        {
            const size_t nv = cs.verts.size();
            const uint32_t band = uint32_t(std::clamp(0.35f * reach / cs.meanEdge, 2.0f, 64.0f));
            std::vector<uint32_t> hops(nv, ~0u), queue;
            for (size_t i = 0; i < nv; ++i)
                if (ringPin[i] >= 1.0f && vertexInfluence(mesh, cs.verts[i]) > 0.0f) hops[i] = 0, queue.push_back(uint32_t(i));
            for (size_t q = 0; q < queue.size(); ++q) {
                const uint32_t i = queue[q];
                if (hops[i] + 1 >= band) continue;
                for (uint32_t k = cs.ringOffset[i]; k < cs.ringOffset[i + 1]; ++k)
                    if (const uint32_t j = cs.ring[k]; hops[j] == ~0u) hops[j] = hops[i] + 1, queue.push_back(j);
            }
            for (size_t i = 0; i < nv; ++i) {
                if (hops[i] == ~0u || hops[i] == 0) continue;
                const float t = 1.0f - float(hops[i]) / float(band);
                cs.basePin[i] = std::max(cs.basePin[i], t * t);
                cs.sim.pinTo(uint32_t(i), cs.basePin[i], cs.restLocal[i]);
            }
            // Soft body: a weak pull back to the stroke-start shape everywhere (0: the cloth stays where it's put).
            if (const float soft = 0.45f * std::clamp(set.softBody, 0.0f, 1.0f); soft > 0.0f)
                for (size_t i = 0; i < nv; ++i)
                    if (cs.basePin[i] < soft) cs.basePin[i] = soft, cs.sim.pinTo(uint32_t(i), soft, cs.restLocal[i]);
            for (size_t i = 0; i < nv; ++i)
                if (const auto g = cs.grow.find(cs.verts[i]); g != cs.grow.end()) cs.sim.setRestScale(uint32_t(i), g->second);
        }
        cs.builtAt.clear();
        for (const SymCopy& m : mirrors) cs.builtAt.push_back(m.point(p.center));
        cs.builtRadius = r;
    }
    const double build = lapMs(clock);
    timings.clothBuildMs += build;
    if (cs.verts.empty()) return;
    ClothSim& sim = cs.sim;
    sim.setColliders(clothColliders && !clothColliders->empty() ? clothColliders : nullptr);
    // Gravity in brush radii per second squared (scale-free, as the fabric): world down.
    const float g = set.mode == ClothBrushMode::Settle ? std::max(0.5f, set.gravity) : std::clamp(set.gravity, 0.0f, 1.0f);
    ClothSettings settings;
    settings.frameSeconds = kFrame;
    settings.thinFeatures = false; // (per-dab cost: the brush keeps the plain contacts)
    settings.substeps = kSubsteps;
    // Big regions take fewer passes and steps so a dab stays within the brush lag bars (3k vertices, 4 passes: ~5 ms).
    const size_t n = cs.verts.size();
    settings.iterations = n > 20000 ? 1 : n > 8000 ? 2 : kIterations;
    settings.restFrames = 1 << 30;
    settings.gravity = glm::vec3(0.0f, -g * 400.0f * r, 0.0f);
    settings.selfDistance = set.selfCollide ? 0.5f * cs.meanEdge : 0.0f;
    sim.setSettings(settings);

    // Drive the vertices under the brush (soft pins toward where the stroke puts them).
    const float s = std::clamp(p.strength, 0.0f, 1.0f) * std::clamp(p.pressure, 0.0f, 1.0f);
    const float sign = p.invert ? -1.0f : 1.0f;
    std::vector<uint32_t> driven;
    if (set.mode != ClothBrushMode::Settle) {
        const std::vector<glm::vec3>& x = sim.positions();
        // Symmetry copies add up, as every brush's do: each vertex sums what each copy's brush asks of it and takes
        // one pin (setting one pin per copy let the last copy win where they overlap, and the cloth crumpled along
        // the edge between them; giving each vertex to the nearest copy instead halved both grabs near the plane).
        const size_t nv = cs.verts.size();
        cs.driveMove.assign(nv, glm::vec3(0.0f));
        cs.driveWeight.assign(nv, 0.0f);
        const bool grab = set.mode == ClothBrushMode::Grab;
        for (size_t mi = 0; mi < mirrors.size(); ++mi) {
            const SymCopy& m = mirrors[mi];
            const glm::vec3 c = m.point(p.center), d = m.dir(p.strokeDelta);
            const glm::vec3 view = m.dir(p.viewDir);
            const glm::vec3 c0 = cs.anchor[mi], travel = c - c0;
            for (size_t i = 0; i < nv; ++i) {
                if (cs.basePin[i] >= 1.0f) continue;
                // Grab weighs each vertex by where it was against where the stroke began; the others by now.
                const float dist = grab ? brushDistance(cs.restLocal[i], c0, view, p.viewFalloff)
                                        : brushDistance(x[i], c, view, p.viewFalloff);
                if (dist >= r) continue;
                const float f = brushFalloff(dist, r, p.focal);
                if (f <= 0.0f) continue;
                const glm::vec3 nrm = mesh.normals.size() == mesh.vertexCount() ? mesh.normals[cs.verts[i]] : -view;
                glm::vec3 move(0.0f);
                switch (set.mode) {
                case ClothBrushMode::Drag: {
                    // Along the stroke, lifted a little off the surface: bunched fabric rises (it can't go into
                    // what is under it), which starts the folds even on a flat sheet.
                    const glm::vec3 along = d * std::min(1.0f, 2.0f * s);
                    move = along + nrm * (0.3f * glm::length(along) * sign);
                    break;
                }
                case ClothBrushMode::Grab: // (held where the brush puts it: f is already in the target)
                    move = (cs.restLocal[i] + travel - x[i]) / f;
                    break;
                case ClothBrushMode::Expand:
                case ClothBrushMode::Pinch: {
                    // Along the surface, from or to the centre (no closer than half the way there per dab). Gathered
                    // cloth also lifts a little, as Drag's does, so it buckles into folds instead of only squeezing
                    // flat; spread cloth pulls its folds out (on flat cloth Expand barely moves: fabric hardly stretches).
                    glm::vec3 out = x[i] - c;
                    out -= nrm * glm::dot(out, nrm);
                    const float l = glm::length(out);
                    if (l < 1e-9f * r) break;
                    const float dir = (set.mode == ClothBrushMode::Expand ? 1.0f : -1.0f) * sign;
                    const float amount = 0.15f * s * r;
                    if (dir > 0.0f) move = out / l * amount;
                    else move = out / l * -std::min(amount, 0.5f * l), move += nrm * (0.5f * glm::length(move));
                    break;
                }
                case ClothBrushMode::Inflate: {
                    // More fabric (its rest lengths grow) pushed out along the normals: it balloons, and folds
                    // where it can't; Alt takes fabric away and pulls in.
                    float& g = cs.grow.try_emplace(cs.verts[i], 1.0f).first->second;
                    g = std::clamp(g + sign * 0.02f * s * f, 0.5f, 2.0f);
                    sim.setRestScale(uint32_t(i), g);
                    move = nrm * (sign * 0.15f * s * r);
                    break;
                }
                default:
                    move = -nrm * (sign * 0.08f * s * r); // Push: into the surface; Alt pulls out
                }
                cs.driveMove[i] += move * f;
                cs.driveWeight[i] = std::max(cs.driveWeight[i], f);
            }
        }
        const float hold = grab ? 0.8f : 0.6f;
        for (size_t i = 0; i < nv; ++i) {
            if (cs.driveWeight[i] <= 0.0f || glm::dot(cs.driveMove[i], cs.driveMove[i]) <= 0.0f) continue;
            // (never fully held: the fabric's constraints have the last word, so the brush can't tear it)
            sim.pinTo(uint32_t(i), std::min(hold, cs.driveWeight[i] * (grab ? 1.0f : 0.8f)), x[i] + cs.driveMove[i]);
            driven.push_back(uint32_t(i));
        }
    }
    cs.start = sim.positions();
    int steps = std::clamp(set.steps, 1, 8);
    if (n > 20000) steps = std::max(1, steps / 2);
    {
        auto solve = std::chrono::steady_clock::now();
        for (int k = 0; k < steps; ++k) sim.step();
        timings.clothSolveMs += lapMs(solve);
    }
    for (uint32_t i : driven) sim.pinTo(i, cs.basePin[i], cs.restLocal[i]); // (the next dab drives afresh)
    // Low-pass the dab's motion: each free vertex moves half its own way and half its neighbours' average. Folds a
    // few edges wide pass; a ripple one edge wide (the solver's jitter where the cloth bunches hardest) is halved
    // every dab and never builds up.
    {
        std::vector<glm::vec3>& xm = sim.positionsMutable();
        cs.delta.resize(xm.size());
        for (size_t i = 0; i < xm.size(); ++i) cs.delta[i] = xm[i] - cs.start[i];
        poolFor(xm.size(), [&](size_t b, size_t e) {
            for (size_t i = b; i < e; ++i) {
                const uint32_t r0 = cs.ringOffset[i], r1 = cs.ringOffset[i + 1];
                if (cs.basePin[i] >= 1.0f || r1 - r0 < 3) continue;
                glm::vec3 mean(0.0f);
                for (uint32_t k = r0; k < r1; ++k) mean += cs.delta[cs.ring[k]];
                xm[i] = cs.start[i] + 0.5f * cs.delta[i] + 0.5f * mean / float(r1 - r0);
            }
        }, 8192);
    }
    const double kernel = build + lapMs(clock);

    // Write the region back.
    const std::vector<glm::vec3>& x = sim.positions();
    cs.moved.clear();
    cs.delta.clear();
    for (size_t i = 0; i < cs.verts.size(); ++i) {
        const uint32_t v = cs.verts[i];
        const glm::vec3 d = x[i] - mesh.positions[v];
        if (glm::dot(d, d) <= 0.0f) continue;
        cs.moved.push_back(v);
        cs.delta.push_back(d);
    }
    moveVertices(mesh, bvh, cs.moved, cs.delta);
    timings.kernelMs += kernel;
    timings.normalsMs += lapMs(clock);
    timings.vertices += cs.verts.size();
    ++timings.dabs;
}

} // namespace sf
