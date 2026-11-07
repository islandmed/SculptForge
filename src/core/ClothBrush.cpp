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
// along the normal) as soft pins weighted by the falloff, runs `steps` frames, and writes the region back through
// moveVertices (undo, sculpt layers, normals, BVH).
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

const char* const kModeNames[] = {"drag", "push", "settle"};
const char* const kModeLabels[] = {"Drag", "Push", "Settle"};

// Per preset, the brush's compliance balance: stretch, compression and bending against a unit particle mass at
// one substep (alpha / h^2 / w). Compression far softer than stretch so a pushed region buckles.
struct BrushFabric {
    float stretch, compress, bend, damping;
};
const BrushFabric kBrushFabrics[] = {
    {1e-3f, 2.0f, 4.0f, 0.30f},  // silk: fine, many folds
    {1e-3f, 1.0f, 1.0f, 0.30f},  // cotton
    {1e-3f, 0.5f, 0.15f, 0.35f}, // denim: broad folds
    {1e-3f, 0.3f, 0.03f, 0.40f}, // leather: few, wide folds
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
    std::vector<float> basePin;                // per region vertex: the ring and the mask
    std::vector<glm::vec3> builtAt;            // the dab centres (per symmetry copy) the region was built around
    float builtRadius = 0.0f, meanEdge = 0.0f;
    ClothSim sim;
    std::vector<uint32_t> moved;
    std::vector<glm::vec3> delta;
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
        settings.substeps = kSubsteps;
        settings.restFrames = 1 << 30; // (the brush never stops on rest)
        settings.gravity = glm::vec3(0.0f);
        cs.basePin = setup.pin;
        cs.verts = std::move(region);
        if (!cs.sim.init(setup, fabric, settings)) {
            cs.verts.clear();
            return;
        }
        cs.builtAt.clear();
        for (const SymCopy& m : mirrors) cs.builtAt.push_back(m.point(p.center));
        cs.builtRadius = r;
    }
    if (cs.verts.empty()) return;
    ClothSim& sim = cs.sim;
    sim.setColliders(clothColliders && !clothColliders->empty() ? clothColliders : nullptr);
    // Gravity in brush radii per second squared (scale-free, as the fabric): world down.
    const float g = set.mode == ClothBrushMode::Settle ? std::max(0.5f, set.gravity) : std::clamp(set.gravity, 0.0f, 1.0f);
    ClothSettings settings;
    settings.frameSeconds = kFrame;
    settings.substeps = kSubsteps;
    // Big regions take fewer passes and steps so a dab stays within the brush lag bars (3k vertices, 4 passes: ~5 ms).
    const size_t n = cs.verts.size();
    settings.iterations = n > 20000 ? 1 : n > 8000 ? 2 : kIterations;
    settings.restFrames = 1 << 30;
    settings.gravity = glm::vec3(0.0f, -g * 400.0f * r, 0.0f);
    sim.setSettings(settings);

    // Drive the vertices under the brush (soft pins toward where the stroke puts them).
    const float s = std::clamp(p.strength, 0.0f, 1.0f) * std::clamp(p.pressure, 0.0f, 1.0f);
    const float sign = p.invert ? -1.0f : 1.0f;
    std::vector<uint32_t> driven;
    if (set.mode != ClothBrushMode::Settle) {
        const std::vector<glm::vec3>& x = sim.positions();
        for (const SymCopy& m : mirrors) {
            const glm::vec3 c = m.point(p.center), d = m.dir(p.strokeDelta);
            const glm::vec3 view = m.dir(p.viewDir);
            for (size_t i = 0; i < cs.verts.size(); ++i) {
                if (cs.basePin[i] >= 1.0f) continue;
                const float dist = brushDistance(x[i], c, view, p.viewFalloff);
                if (dist >= r) continue;
                const float f = brushFalloff(dist, r, p.focal);
                if (f <= 0.0f) continue;
                const glm::vec3 nrm = mesh.normals.size() == mesh.vertexCount() ? mesh.normals[cs.verts[i]] : -view;
                glm::vec3 move;
                if (set.mode == ClothBrushMode::Drag) {
                    // Along the stroke, lifted a little off the surface: bunched fabric rises (it can't go into
                    // what is under it), which starts the folds even on a flat sheet.
                    const glm::vec3 along = d * std::min(1.0f, 2.0f * s);
                    move = along + nrm * (0.3f * glm::length(along) * sign);
                } else {
                    move = -nrm * (sign * 0.08f * s * r); // Push: into the surface; Alt pulls out
                }
                if (glm::dot(move, move) <= 0.0f) continue;
                // (never fully held: the fabric's constraints have the last word, so the brush can't tear it)
                sim.pinTo(uint32_t(i), std::min(0.6f, f * 0.8f), x[i] + move * f);
                driven.push_back(uint32_t(i));
            }
        }
    }
    int steps = std::clamp(set.steps, 1, 8);
    if (n > 20000) steps = std::max(1, steps / 2);
    for (int k = 0; k < steps; ++k) sim.step();
    for (uint32_t i : driven) sim.pin(i, cs.basePin[i]); // (the next dab drives afresh)
    const double kernel = lapMs(clock);

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
