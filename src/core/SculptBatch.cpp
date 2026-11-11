// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Fused batches of big dabs (Sculptor::beginBatch, Sculpt.h). A 240 px dab on a 15M triangle head reaches a few
// million vertices, and the dab-by-dab path visits each of them (query, offset, undo entry, normals, BVH) once per
// dab, then the GPU gets them once per batch: the stroke trailed the pen by the 3-5 dabs waiting. Here the waiting
// dabs go through the vertices together:
//  1. per copy of the symmetry, one query of a sphere around all of the batch's dabs (plus what they could move
//     into it), so each vertex is found once;
//  2. dab by dab, on an even sample of those vertices (moved along as the dabs go): the area plane each dab
//     takes, from the sample as the dabs before it left it, and its centre, picked as the batch began, moved
//     along by the dabs before it (a point on the surface goes where a vertex there would);
//  3. one pass over the vertices: each goes through the dabs in order, taking the offset each gives where the
//     vertex is by then (so it lands where the dab-by-dab stroke puts it, give or take the sampled plane), and
//     its normal goes along (the adaptive normals, Sculpt.cpp);
//  4. the undo entries, the BVH, the GPU's dirty marks and the exact normals' catch-up list, once.
#include "core/Sculpt.h"
#include "core/BrushAlpha.h"
#include "core/SculptKernel.h"
#include "core/IndexBits.h"
#include "core/ThreadPool.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <iterator>
#include <limits>

namespace sf {

bool Sculptor::batchable(const Mesh& mesh, BrushType type, const DabParams& p, const Symmetry& sym) const {
    if (!adaptiveDabs || !isPlaneKernelBrush(type) || type == BrushType::Trim || dyntopo.enabled || draft_ || (mesh.micro && !(microBase_ == 1 && microBaseFuse)))
        return false;
    if ((p.alpha && !p.alpha->empty()) || p.stencil.active() || p.frontFacesOnly || p.connectedOnly || p.viewFalloff) return false;
    if (mesh.normals.size() != mesh.vertexCount()) return false;
    if (!pending_.empty()) {
        const PendingDab& f = pending_.front();
        if (f.type != type || f.sym.x != sym.x || f.sym.y != sym.y || f.sym.z != sym.z || f.sym.radialOn() != sym.radialOn() ||
            f.sym.radial != sym.radial || f.sym.radialAxis != sym.radialAxis || f.sym.local != sym.local ||
            f.sym.pivot != sym.pivot)
            return false;
        if (pending_.size() >= 64 || f.p.focal != p.focal) return false;
    }
    // Big enough: the last region, scaled to this radius (the first dab of a stroke is placed as it comes).
    if (lastRegionRadius_ <= 0.0f || lastRegion_ == 0) return false;
    const float k = p.radius / lastRegionRadius_;
    return double(lastRegion_) * double(k * k) >= double(kAdaptiveMinVertices);
}

void Sculptor::endBatch(Mesh& mesh, BVH& bvh) {
    if (!pending_.empty()) flushBatch(mesh, bvh);
    batching_ = false;
}

namespace {

// A dab's frame on the surface from points with normals (the dab-by-dab path's areaSums, over a sample).
struct Frame {
    glm::vec3 normal{0.0f}, center{0.0f};
    float weight = 0.0f;
};

// A copy of the symmetry's share of the batch: its dabs' indices (in order), the sphere around them, the vertices
// in it (increasing), the BVH leaves holding them, and the sample.
struct Group {
    std::vector<size_t> passes;
    glm::vec3 center{0.0f};
    float radius = 0.0f;
    std::vector<uint32_t> verts, leaves, sample;
    bool merged = false; // holds more than one copy's query (its leaves may repeat)
    std::vector<glm::vec3> samplePos, sampleNrm;
};

} // namespace

void Sculptor::flushBatch(Mesh& mesh, BVH& bvh) {
    std::vector<PendingDab> dabs;
    dabs.swap(pending_);
    const auto placeOneByOne = [&] {
        const bool was = batching_;
        batching_ = false;
        for (const PendingDab& d : dabs) dab(mesh, bvh, d.type, d.p, d.sym);
        batching_ = was;
    };
    if (dabs.size() < 2) { // (nothing to fuse)
        placeOneByOne();
        return;
    }
    auto clock = std::chrono::steady_clock::now();
    const BrushType type = dabs.front().type;
    const std::vector<SymCopy> cs = copies(dabs.front().sym);

    // The passes, dab by dab and copy by copy (the dab-by-dab path's order), and a group per copy.
    struct Pass {
        DabParams p;
        size_t group = 0, dab = 0; // (dab: the pending dab it is a copy of)
        bool identity = false, valid = false;
        PlaneKernel k;
    };
    std::vector<Pass> passes;
    std::vector<Group> groups(cs.size());
    float maxRadius = 0.0f;
    for (size_t di = 0; di < dabs.size(); ++di)
        for (size_t c = 0; c < cs.size(); ++c) {
            const PendingDab& d = dabs[di];
            Pass ps;
            ps.dab = di;
            ps.p = cs[c].identity() ? d.p : transformed(d.p, cs[c]);
            ps.group = c;
            ps.identity = cs[c].identity();
            groups[c].passes.push_back(passes.size());
            passes.push_back(ps);
            maxRadius = std::max(maxRadius, d.p.radius);
        }
    // What a vertex outside a dab's sphere could be moved into it by the dabs before it (near the rim, where
    // their falloff is all but 0): a twentieth of the radius.
    const float margin = 0.05f * maxRadius;
    // Per copy: the capsule from its first dab's centre to its last one's, wide enough for every dab's sphere.
    struct Capsule {
        glm::vec3 a, b;
        float radius;
    };
    std::vector<Capsule> caps(groups.size());
    for (size_t gi = 0; gi < groups.size(); ++gi) {
        Group& g = groups[gi];
        const glm::vec3 a = passes[g.passes.front()].p.center, b = passes[g.passes.back()].p.center;
        const glm::vec3 ab = b - a;
        const float len2 = glm::dot(ab, ab);
        float reach = 0.0f;
        for (size_t j : g.passes) {
            const glm::vec3 d = passes[j].p.center - a;
            const float t = len2 > 0.0f ? std::clamp(glm::dot(d, ab) / len2, 0.0f, 1.0f) : 0.0f;
            reach = std::max(reach, glm::length(d - ab * t) + passes[j].p.radius);
        }
        caps[gi] = {a, b, reach + margin};
        g.center = (a + b) * 0.5f;
        g.radius = 0.5f * std::sqrt(len2) + reach + margin; // (a sphere around the capsule: which copies meet)
        bvh.queryVerticesCapsule(mesh, a, b, reach + margin, g.verts);
        if (mesh.hasHidden()) // (brushVertices: no fully hidden vertices; such regions go one by one below)
            std::erase_if(g.verts, [&](uint32_t v) { return mesh.vertexHidden(v); });
        const auto leaves = bvh.queriedLeaves();
        g.leaves.assign(leaves.begin(), leaves.end());
    }
    // Copies whose capsules meet (a stroke over the mirror plane) share vertices, which go through both copies'
    // dabs in the dab-by-dab order: such copies become one group (every pass of it is tried on each vertex).
    const auto meet = [&](size_t x, size_t y) {
        if (glm::distance(groups[x].center, groups[y].center) >= groups[x].radius + groups[y].radius) return false;
        // segment to segment, sampled finely enough for a conservative answer
        float best = std::numeric_limits<float>::max();
        for (int i = 0; i <= 16; ++i) {
            const glm::vec3 p = glm::mix(caps[x].a, caps[x].b, float(i) / 16.0f);
            const glm::vec3 ab = caps[y].b - caps[y].a;
            const float l2 = glm::dot(ab, ab);
            const float t = l2 > 0.0f ? std::clamp(glm::dot(p - caps[y].a, ab) / l2, 0.0f, 1.0f) : 0.0f;
            best = std::min(best, glm::distance(p, caps[y].a + ab * t));
        }
        return best < caps[x].radius + caps[y].radius + glm::distance(caps[x].a, caps[x].b) / 16.0f;
    };
    IndexBits merged;
    for (size_t x = 0; x < groups.size(); ++x)
        for (size_t y = x + 1; y < groups.size();) {
            if (!meet(x, y)) {
                ++y;
                continue;
            }
            Group& gx = groups[x];
            Group& gy = groups[y];
            if (merged.capacity() < mesh.vertexCount()) merged.resize(mesh.vertexCount());
            for (const std::vector<uint32_t>* list : {&gx.verts, &gy.verts})
                merged.insertParallel(list->size(), 16384, [&](size_t b, size_t e, IndexBits& set) {
                    for (size_t i = b; i < e; ++i) set.insertLocal((*list)[i]);
                });
            gx.verts.clear();
            merged.take(gx.verts);
            gx.leaves.insert(gx.leaves.end(), gy.leaves.begin(), gy.leaves.end()); // (deduplicated below)
            gx.merged = true;
            gx.passes.insert(gx.passes.end(), gy.passes.begin(), gy.passes.end());
            std::sort(gx.passes.begin(), gx.passes.end());
            gx.radius = std::max(gx.radius, glm::distance(gx.center, gy.center) + gy.radius);
            caps[x].radius = std::max(caps[x].radius, caps[y].radius + glm::distance(caps[x].a, caps[y].a)); // (covers both)
            groups.erase(groups.begin() + std::ptrdiff_t(y));
            caps.erase(caps.begin() + std::ptrdiff_t(y));
            y = x + 1; // (the grown group may now meet one passed over)
        }
    for (Group& g : groups) { // leaves listed by more than one copy's query: once
        if (!g.merged) continue;
        IndexBits bits;
        bits.resize(bvh.nodeCount());
        bits.insertParallel(g.leaves.size(), 16384, [&](size_t b, size_t e, IndexBits& set) {
            for (size_t i = b; i < e; ++i) set.insertLocal(g.leaves[i]);
        });
        g.leaves.clear();
        bits.take(g.leaves);
    }
    for (Group& g : groups)
        for (size_t j : g.passes) passes[j].group = size_t(&g - groups.data());
    // A mask or hidden faces in reach: their edges are steps the adaptive normals don't see (the dab-by-dab path).
    if (mesh.mask.size() == mesh.vertexCount() || mesh.hasHidden()) {
        std::atomic<bool> masked{false};
        for (const Group& g : groups)
            poolFor(g.verts.size(), [&](size_t b, size_t e) {
                for (size_t i = b; i < e && !masked.load(std::memory_order_relaxed); ++i)
                    if (vertexInfluence(mesh, g.verts[i]) < 1.0f) masked.store(true, std::memory_order_relaxed);
            }, 65536);
        if (masked.load()) {
            placeOneByOne();
            return;
        }
    }

    // 2. Dab by dab on the samples: each one's centre and area plane, then its move of the samples.
    const auto& pos = mesh.positions;
    std::vector<glm::vec3>& normals = mesh.normals;
    for (Group& g : groups) {
        // About kAreaSamples in each dab's own sphere (lastRegion_: the size of one, both copies' if they met).
        const size_t inDab = std::max<size_t>(1, lastRegion_ * (g.passes.size() > dabs.size() ? 2 : 1));
        const size_t stride = std::max<size_t>(1, inDab / kAreaSamples);
        g.sample.clear();
        for (size_t i = 0; i < g.verts.size(); i += stride) g.sample.push_back(g.verts[i]);
        g.samplePos.resize(g.sample.size());
        g.sampleNrm.resize(g.sample.size());
        for (size_t i = 0; i < g.sample.size(); ++i) {
            g.samplePos[i] = pos[g.sample[i]];
            g.sampleNrm[i] = normals[g.sample[i]];
        }
    }
    constexpr size_t kBlock = 32768; // (few jobs: waking a 12-thread pool costs more than these do)
    std::vector<Frame> parts;
    for (size_t j = 0; j < passes.size(); ++j) {
        Pass& ps = passes[j];
        Group& g = groups[ps.group];
        // The centre: where the pick's ray meets the surface the dabs before it left. The picked point goes where
        // they move it (with its normal, from the last dab's plane), and back onto the ray through the tangent
        // plane there.
        const glm::vec3 c0 = ps.p.center;
        glm::vec3 c = c0, n = ps.p.pickNormal;
        bool moved = false;
        for (size_t i : g.passes) {
            if (i >= j) break;
            if (!passes[i].valid || passes[i].dab == ps.dab) continue; // (a copy's centre is its dab's mirrored pick: the other copies of the dab don't move it)
            const PlaneKernel& k = passes[i].k;
            if (glm::length(n) == 0.0f) n = k.areaNormal;
            if (!k.reaches(c)) continue;
            glm::vec3 d;
            n = k.offsetAndNormal(c, n, std::exp2(2.0f * std::clamp(k.focal, -1.0f, 1.0f)), d);
            c += d;
            moved = true;
        }
        if (moved) {
            const glm::vec3 ray = glm::length(ps.p.pickDir) > 0.0f ? glm::normalize(ps.p.pickDir) : glm::normalize(ps.p.viewDir);
            const float across = glm::dot(ray, n);
            if (std::abs(across) > 0.2f) c = c0 + ray * (glm::dot(c - c0, n) / across);
        }
        ps.p.center = c;
        const DabParams& p = ps.p;
        const size_t ns = g.sample.size();
        parts.assign((ns + kBlock - 1) / kBlock, Frame{});
        poolFor(ns, [&](size_t b, size_t e) {
            Frame& acc = parts[b / kBlock];
            for (size_t i = b; i < e; ++i) {
                const glm::vec3& x = g.samplePos[i];
                const float dist = brushDistance(p, x);
                if (dist > p.radius) continue;
                const float w = brushFalloff(dist, p.radius, p.focal);
                acc.normal += g.sampleNrm[i] * w;
                acc.center += x * w;
                acc.weight += w;
            }
        }, kBlock);
        Frame sum;
        for (const Frame& f : parts) {
            sum.normal += f.normal;
            sum.center += f.center;
            sum.weight += f.weight;
        }
        if (sum.weight <= 0.0f) continue; // (the dab-by-dab path: a dab that reaches nothing does nothing)
        const float nlen = glm::length(sum.normal);
        PlaneKernel& k = ps.k;
        k.type = type;
        k.center = p.center;
        k.viewDir = p.viewDir;
        k.areaNormal = nlen > 1e-12f ? sum.normal / nlen : -p.viewDir;
        k.areaCenter = sum.center / sum.weight;
        const float strokeLen = glm::length(p.strokeDelta);
        k.strokeDir = strokeLen > 1e-12f ? p.strokeDelta / strokeLen : glm::vec3(0.0f);
        k.r = p.radius;
        k.s = std::clamp(p.strength, 0.0f, 1.0f) * std::clamp(p.pressure, 0.0f, 1.0f);
        k.sign = p.invert ? -1.0f : 1.0f;
        k.focal = p.focal;
        k.invert = p.invert;
        k.viewFalloff = p.viewFalloff;
        ps.valid = true;
        if (ps.identity) lastFrame = {p.center, k.areaNormal, k.areaCenter, true};
        const float kexp = std::exp2(2.0f * std::clamp(k.focal, -1.0f, 1.0f));
        poolFor(ns, [&](size_t b, size_t e) {
            for (size_t i = b; i < e; ++i) {
                const glm::vec3 x = g.samplePos[i];
                if (!k.reaches(x)) continue;
                glm::vec3 d;
                g.sampleNrm[i] = k.offsetAndNormal(x, g.sampleNrm[i], kexp, d);
                g.samplePos[i] = x + d;
            }
        }, kBlock);
        ++timings.dabs;
    }

    // 3. Every vertex through its dabs; 4. the undo entries and the moves (displaceAll), once.
    if (curvature_.size() != mesh.vertexCount() || curvatureMesh_ != &mesh) {
        curvature_.assign(mesh.vertexCount(), 0.0f);
        curvatureFilled_ = false;
        curvatureMesh_ = &mesh;
    }
    if (curvatureStamp_.size() != mesh.vertexCount()) curvatureStamp_.assign(mesh.vertexCount(), 0);
    const uint32_t stamp = uint32_t(strokeId_);
    for (Group& g : groups) {
        const size_t n = g.verts.size();
        scratch_.resize(n);
        givenInfluence_.resize(n);
        std::vector<const PlaneKernel*> ks;
        for (size_t j : g.passes)
            if (passes[j].valid) ks.push_back(&passes[j].k);
        const float kexp = ks.empty() ? 1.0f : std::exp2(2.0f * std::clamp(ks.front()->focal, -1.0f, 1.0f)); // (one brush)
        if (ks.empty()) continue;
        poolFor(n, [&](size_t b, size_t e) {
            for (size_t i = b; i < e; ++i) {
                const uint32_t v = g.verts[i];
                const glm::vec3 x0 = pos[v];
                glm::vec3 x = x0, t1, t2;
                bool reached = false;
                for (const PlaneKernel* k : ks) {
                    if (!reached) { // (the first dab that reaches it: its cavity term from before, its tangents)
                        const glm::vec3 o = x - k->center;
                        if (glm::dot(o, o) >= k->r * k->r) continue;
                        if (curvatureStamp_[v] != stamp) {
                            curvatureStamp_[v] = stamp;
                            curvature_[v] = kKeepCurvature;
                        }
                        PlaneKernel::tangents(normals[v], t1, t2);
                        reached = true;
                    }
                    glm::vec3 d;
                    if (k->offsetAndCarry(x, kexp, d, t1, t2)) x += d;
                }
                scratch_[i] = x - x0;
                givenInfluence_[i] = reached ? 1.0f : 0.0f;
                if (reached) normals[v] = PlaneKernel::carriedNormal(t1, t2, normals[v]);
            }
        }, 2048);
        displaceAll(mesh, g.verts, scratch_, false, givenInfluence_.data());
        timings.kernelMs += lapMs(clock);
        bvh.refitLeaves(mesh, g.leaves);
        timings.bvhMs += lapMs(clock);
        timings.vertices += n;
        addLazyLeaves(bvh, g.leaves);
        lastLazyLeaves_.insert(lastLazyLeaves_.end(), g.leaves.begin(), g.leaves.end());
        // The GPU's dirty marks of what moved, in parallel straight into the mesh's bits: the list is increasing,
        // so each stretch of it starts and ends at a dirty block's edge and no two threads share a word.
        size_t first = 0, last = n;
        while (first < n && givenInfluence_[first] <= 0.0f) ++first;
        while (last > first && givenInfluence_[last - 1] <= 0.0f) --last;
        if (first < last) {
            const std::vector<uint32_t>& vs = g.verts;
            mesh.growDirtyBlocks((size_t(vs[last - 1]) >> Mesh::kDirtyBlockShift) + 1);
            mesh.dirtyBegin = std::min(mesh.dirtyBegin, vs[first]);
            mesh.dirtyEnd = std::max(mesh.dirtyEnd, vs[last - 1] + 1);
            const auto block = [&](size_t i) { return vs[i] >> Mesh::kDirtyBlockShift; };
            poolFor(n, [&](size_t b, size_t e) {
                size_t i = b, end = e;
                if (b > 0)
                    while (i < n && block(i) == block(b - 1)) ++i; // (the stretch before has that block)
                while (end < n && block(end) == block(end - 1)) ++end;
                for (; i < end; ++i)
                    if (givenInfluence_[i] > 0.0f) {
                        const uint32_t v = vs[i];
                        mesh.dirtyBlocks[v >> Mesh::kDirtyBlockShift] = 1;
                        mesh.dirtyBits[v >> 6] |= uint64_t(1) << (v & 63);
                    }
            }, 16384);
        }
        timings.normalsMs += lapMs(clock);
    }
    lazyMesh_ = &mesh;
    lazyBvh_ = &bvh;
}

} // namespace sf
