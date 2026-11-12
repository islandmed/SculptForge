// R&D spike S6 (rnd/moved-vertex, never merged): sf_rnd_proxy_bench <project> [radius fraction] [dabs] [cells per radius...]
// runs one Standard stroke exactly (every dab on the dense mesh), then the same stroke on a coarse proxy (vertex clustering
// on a grid of cell h = r / cells: one vertex per occupied cell, at the mean) and propagates the proxy's displacement to
// the dense vertices once at the end (trilinear over the 8 surrounding cells, empty ones dropped). Prints per dab the
// vertices touched and ms, the dense pass, and the error at full resolution vs the exact stroke (max, p99.9, RMS / r).
#include "core/Document.h"
#include "core/Mesh.h"
#include "core/Project.h"
#include "core/Sculpt.h"
#include "core/ThreadPool.h"
#include "render/Ghost.h"
#include "../GeomInvariants.h"
#include <algorithm>
#include <atomic>
#include <tuple>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <unordered_map>
#include <vector>

using namespace sf;
using namespace sf::geom;

namespace sf {
extern std::vector<std::pair<glm::vec3, glm::vec3>>* gRndPlanes;
extern bool gRndPlanesUse;
extern size_t gRndPlaneAt;
extern const float* gRndAreaWeight; extern const glm::vec3 *gRndAreaNormalSum, *gRndAreaNormal0; }

namespace {

using Clock = std::chrono::steady_clock;
double msSince(Clock::time_point t) { return std::chrono::duration<double, std::milli>(Clock::now() - t).count(); }

size_t dirtyCount(const Mesh& m) {
    size_t n = 0;
    for (uint64_t w : m.dirtyBits) n += size_t(std::popcount(w));
    return n;
}

uint64_t cellKey(int64_t x, int64_t y, int64_t z, uint32_t bin = 0) {
    return uint64_t(x & 0xfffff) | uint64_t(y & 0xfffff) << 20 | uint64_t(z & 0xfffff) << 40 | uint64_t(bin) << 60;
}
// SF_RND_NBIN=1: cells split by the vertex normal's dominant axis and sign (6 bins), so the two sides of a thin part or a fold
// fall in different proxy vertices and the field doesn't carry one side's move to the other.
uint32_t normalBin(glm::vec3 n) {
    const glm::vec3 a = glm::abs(n);
    const int k = a.x >= a.y && a.x >= a.z ? 0 : a.y >= a.z ? 1 : 2;
    return uint32_t(k * 2 + (n[k] < 0.0f ? 1 : 0));
}

struct Stroke {
    glm::vec3 mid, dir, across, lo;
    float step, radius, depthSize, sideSize;
    int depth;
    // The dab i ray: from in front of the mesh along `dir` (the same rays in every mode).
    glm::vec3 origin(int i) const {
        glm::vec3 o = mid + across * (-sideSize * 0.1f + step * float(i));
        o[depth] = lo[depth] - depthSize;
        return o;
    }
};

struct Run {
    std::vector<glm::vec3> centers; // each dab's surface point
    std::vector<glm::vec3> areaNormals; // (probe: brushAreaNormal before the dab)
    double dabMs = 0.0;
    size_t touched = 0;
    int hits = 0;
};

Run stroke(Document& d, const Stroke& s, int dabs, const std::vector<glm::vec3>* given = nullptr) {
    Run r;
    d.sculptor.analyticNormals = false;
    d.sculptor.adaptiveDabs = false;
    const bool clay = std::getenv("SF_RND_BRUSH") && std::string(std::getenv("SF_RND_BRUSH")) == "clay"; // (the bench phases' brush)
    d.sculptor.beginStroke(d.mesh, clay ? "Clay" : "Standard");
    d.mesh.clearDirty();
    for (int i = 0; i < dabs; ++i) {
        RayHit h;
        if (!d.bvh.raycast(d.mesh, s.origin(i), s.dir, h)) continue;
        DabParams p;
        p.center = given && size_t(r.hits) < given->size() ? (*given)[size_t(r.hits)] : h.position; // (SF_RND_SAME_CENTERS: the exact stroke's)
        r.centers.push_back(p.center);
        r.areaNormals.push_back(brushAreaNormal(d.mesh, d.bvh, p.center, s.radius));
        ++r.hits;
        p.viewDir = s.dir;
        p.radius = s.radius;
        p.strength = clay ? 0.35f : 0.5f; // (the bench phase: Clay 0.35)
        p.strokeDelta = i == 0 ? glm::vec3(0.0f) : s.across * s.step;
        const auto t0 = Clock::now();
        d.sculptor.dab(d.mesh, d.bvh, clay ? BrushType::Clay : BrushType::Standard, p, Symmetry{false, false, false});
        r.dabMs += msSince(t0);
        r.touched += dirtyCount(d.mesh);
        d.mesh.clearDirty();
    }
    return r;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) return 2;
    const float frac = argc > 2 ? float(std::atof(argv[2])) : 0.15f;
    const int dabs = argc > 3 ? std::atoi(argv[3]) : 12;
    std::vector<float> cellsPerRadius;
    for (int a = 4; a < argc; ++a) cellsPerRadius.push_back(float(std::atof(argv[a])));
    if (cellsPerRadius.empty()) cellsPerRadius = {16.0f, 32.0f, 64.0f};

    Document d;
    std::string err;
    if (!loadProject(argv[1], d, err)) { std::printf("load: %s\n", err.c_str()); return 1; }
    const std::vector<glm::vec3> orig = d.mesh.positions, origN = d.mesh.normals;
    const bool nbin = std::getenv("SF_RND_NBIN") && std::getenv("SF_RND_NBIN")[0] == '1';
    const bool nbin2 = std::getenv("SF_RND_NBIN") && std::getenv("SF_RND_NBIN")[0] == '2';
    const size_t nv = orig.size();
    const AABB b = d.mesh.bounds();
    const glm::vec3 size = b.max - b.min;
    int up = 0, depth = 0;
    for (int k = 1; k < 3; ++k) { if (size[k] > size[up]) up = k; if (size[k] < size[depth]) depth = k; }
    const int side = 3 - up - depth;
    Stroke s;
    s.mid = (b.min + b.max) * 0.5f;
    s.dir = glm::vec3(0.0f), s.across = glm::vec3(0.0f);
    s.dir[depth] = 1.0f;
    s.across[side] = 1.0f;
    s.lo = b.min;
    s.radius = frac * size[up];
    s.step = s.radius * 0.15f;
    s.depthSize = size[depth];
    s.sideSize = size[side];
    s.depth = depth;

    // SF_RND_PICK=k: pick through the preview over a long Clay stroke. T = the mesh as drawn (every dab placed at once);
    // B = what the CPU holds (T as of the last batch of k dabs). Each dab's ray hits B, moved by the pending dabs' kernel
    // (render/Ghost.h ghostMove, the preview's own), then 0-2 refinements; error in px vs the ray's hit on T (r = 240 px).
    if (const char* pk = std::getenv("SF_RND_PICK")) {
        const int k = std::max(1, std::atoi(pk));
        const auto fresh = [&](Document& x) {
            Mesh m;
            m.positions = orig;
            m.triangles = d.mesh.triangles;
            m.rebuildTopology();
            m.normals = origN;
            x.setMesh(std::move(m));
        };
        Document T, B;
        fresh(T), fresh(B);
        T.sculptor.analyticNormals = false;
        T.sculptor.adaptiveDabs = false;
        T.sculptor.beginStroke(T.mesh, "Clay");
        std::vector<std::pair<glm::vec3, glm::vec3>> planes;
        GhostStroke g;
        g.kind = ghostKindForBrush(int(BrushType::Clay));
        const float px = 240.0f / s.radius;
        std::vector<float> err[3];
        int hits = 0;
        for (int i = 0; i < dabs; ++i) {
            const glm::vec3 o = s.origin(i);
            RayHit th, bh;
            if (!T.bvh.raycast(T.mesh, o, s.dir, th) || !B.bvh.raycast(B.mesh, o, s.dir, bh)) continue;
            // the pick: B's hit moved by the pending dabs; each refinement shifts the ray sideways by how far the moved point
            // landed off the true ray and hits B again (the drawn surface is B moved by the preview's kernel)
            const auto moved = [&](const RayHit& h) {
                glm::vec3 n = glm::normalize(h.normal);
                if (glm::dot(n, s.dir) > 0.0f) n = -n;
                return g.dabs.empty() ? h.position : ghostMove(g, h.position, n);
            };
            glm::vec3 pick = moved(bh), shifted = o;
            for (int step = 0; step < 3; ++step) {
                err[step].push_back(glm::distance(pick, th.position) * px);
                if (step == 2) break;
                const glm::vec3 off = pick - o, side = off - s.dir * glm::dot(off, s.dir);
                shifted -= side;
                RayHit rh;
                if (!B.bvh.raycast(B.mesh, shifted, s.dir, rh)) { for (int r2 = step + 1; r2 < 3; ++r2) err[r2].push_back(err[step].back()); break; }
                pick = moved(rh);
            }
            ++hits;
            DabParams p;
            p.center = pick;
            p.viewDir = s.dir;
            p.radius = s.radius;
            p.strength = 0.35f;
            p.strokeDelta = i == 0 ? glm::vec3(0.0f) : s.across * s.step;
            gRndPlanes = &planes, gRndPlanesUse = false;
            T.sculptor.dab(T.mesh, T.bvh, BrushType::Clay, p, Symmetry{false, false, false});
            gRndPlanes = nullptr;
            GhostDab gd;
            gd.center = pick;
            gd.radius = s.radius;
            gd.normal = planes.back().first;
            gd.areaCenter = planes.back().second;
            gd.strength = p.strength;
            g.dabs.push_back(gd);
            if (int(g.dabs.size()) >= k) { // the batch lands: B catches up, the preview empties
                B.mesh.positions = T.mesh.positions;
                B.mesh.normals = T.mesh.normals;
                B.bvh.refitAll(B.mesh);
                g.dabs.clear();
            }
        }
        for (int step = 0; step < 3; ++step) {
            auto& e = err[step];
            std::sort(e.begin(), e.end());
            const auto q = [&](double f) { return e.empty() ? 0.0f : e[std::min(e.size() - 1, size_t(f * double(e.size() - 1)))]; };
            std::printf("rnd pick: batch %d, %d dabs, %d refinements: pick error max %.2f p99 %.2f p50 %.2f px\n", k, hits, step,
                        double(q(1.0)), double(q(0.99)), double(q(0.5)));
        }
        return 0;
    }
    // The exact stroke: the reference.
    const Run ex = stroke(d, s, dabs);
    const std::vector<glm::vec3> exact = d.mesh.positions;
    std::printf("rnd proxy: %zu verts, threads %d, r %.4g, %d dabs | exact: %.0f verts touched/dab, %.1f ms/dab\n", nv,
                ThreadPool::shared().threads(), double(s.radius), ex.hits, double(ex.touched) / std::max(1, ex.hits),
                ex.dabMs / std::max(1, ex.hits));

    // The brush promises (tests/GeomInvariants.h) on the exact stroke: the proxy must not add to them.
    const float edge = meanEdge(d.mesh);
    const auto promises = [&](const char* what) {
        const EdgeReport er = edgeOutliers(d.mesh, orig, ex.centers, s.radius, edge);
        std::printf("  %s promises: flipped %zu, inverted %zu, non-finite %zu, edge spikes %d (worst %.2f edges, ring %d)\n", what,
                    flippedFaces(orig, d.mesh), invertedFaces(orig, d.mesh), nonFinite(d.mesh), er.spikes, double(er.worst), er.ring);
    };
    promises("exact");
    const std::vector<glm::vec3> exactKeep = d.mesh.positions;
    for (float cpr : cellsPerRadius) {
        const float h = s.radius / cpr;
        // Proxy: vertex clustering of the ORIGINAL surface.
        std::vector<glm::vec3> planeDense;
        const auto tb = Clock::now();
        std::unordered_map<uint64_t, uint32_t> cellOf;
        cellOf.reserve(nv / 4 + 1024);
        std::vector<uint32_t> proxyOf(nv);
        std::vector<glm::dvec3> sum;
        std::vector<uint32_t> count;
        // SF_RND_NBIN=2: a cell's two sides (vertex normal against the cell's mean normal) are separate proxy vertices.
        std::unordered_map<uint64_t, glm::vec3> cellNormal;
        const auto plainKey = [&](glm::vec3 x) {
            const glm::vec3 g = (x - b.min) / h;
            return cellKey(int64_t(std::floor(g.x)), int64_t(std::floor(g.y)), int64_t(std::floor(g.z)));
        };
        if (nbin2) {
            cellNormal.reserve(nv / 4 + 1024);
            for (size_t v = 0; v < nv; ++v) cellNormal[plainKey(orig[v])] += origN[v];
        }
        const auto side = [&](uint64_t plain, glm::vec3 n) -> uint32_t {
            if (!nbin2) return 0;
            const auto it = cellNormal.find(plain);
            return it != cellNormal.end() && glm::dot(it->second, n) < 0.0f ? 8 : 0;
        };
        for (size_t v = 0; v < nv; ++v) {
            const glm::vec3 g = (orig[v] - b.min) / h;
            const uint64_t pk = plainKey(orig[v]);
            const uint64_t k = nbin ? cellKey(int64_t(std::floor(g.x)), int64_t(std::floor(g.y)), int64_t(std::floor(g.z)), normalBin(origN[v]))
                                    : pk | uint64_t(side(pk, origN[v])) << 60;
            auto [it, fresh] = cellOf.try_emplace(k, uint32_t(sum.size()));
            if (fresh) sum.emplace_back(0.0), count.push_back(0);
            sum[it->second] += glm::dvec3(orig[v]);
            ++count[it->second];
            proxyOf[v] = it->second;
        }
        Mesh pm;
        pm.positions.resize(sum.size());
        for (size_t i = 0; i < sum.size(); ++i) pm.positions[i] = glm::vec3(sum[i] / double(count[i]));
        pm.triangles.reserve(d.mesh.triangles.size() / 8);
        for (const glm::uvec3& t : d.mesh.triangles) {
            const glm::uvec3 q(proxyOf[t.x], proxyOf[t.y], proxyOf[t.z]);
            if (q.x != q.y && q.y != q.z && q.x != q.z) pm.triangles.push_back(q);
        }
        std::sort(pm.triangles.begin(), pm.triangles.end(), [](glm::uvec3 a, glm::uvec3 c) {
            auto key = [](glm::uvec3 t) { // (the same triangle in any rotation)
                uint32_t m[3] = {t.x, t.y, t.z};
                std::sort(m, m + 3);
                return std::tuple(m[0], m[1], m[2]);
            };
            return key(a) < key(c);
        });
        pm.triangles.erase(std::unique(pm.triangles.begin(), pm.triangles.end(), [](glm::uvec3 a, glm::uvec3 c) {
            uint32_t x[3] = {a.x, a.y, a.z}, y[3] = {c.x, c.y, c.z};
            std::sort(x, x + 3), std::sort(y, y + 3);
            return x[0] == y[0] && x[1] == y[1] && x[2] == y[2];
        }), pm.triangles.end());
        const std::vector<glm::vec3> proxyStart = pm.positions;
        const size_t np = pm.positions.size(), ntp = pm.triangles.size();
        pm.rebuildTopology();
        pm.recomputeAllNormals();
        Document p;
        p.setMesh(std::move(pm));
        const double buildMs = msSince(tb);

        const bool same = std::getenv("SF_RND_SAME_CENTERS") != nullptr;
        std::vector<float> weight(count.begin(), count.end()); // SF_RND_AREA_WEIGHT=1: area sums weighted by the dense vertices per proxy vertex
        if (std::getenv("SF_RND_AREA_WEIGHT")) gRndAreaWeight = weight.data();
        // SF_RND_AREA_NORMAL=1: the area sums use the dense normals' sum per proxy vertex (turned with the proxy).
        std::vector<glm::vec3> denseNormalSum(np, glm::vec3(0.0f)), proxyNormal0 = p.mesh.normals;
        for (size_t v = 0; v < nv; ++v) denseNormalSum[proxyOf[v]] += origN[v];
        if (std::getenv("SF_RND_AREA_NORMAL")) gRndAreaNormalSum = denseNormalSum.data(), gRndAreaNormal0 = proxyNormal0.data();
        std::vector<std::pair<glm::vec3, glm::vec3>> planes;
        gRndPlanes = &planes, gRndPlanesUse = false;
        const Run pr = stroke(p, s, dabs, same ? &ex.centers : nullptr);
        gRndPlanes = nullptr;
        gRndAreaWeight = nullptr;
        gRndAreaNormalSum = gRndAreaNormal0 = nullptr;
        // SF_RND_PLANES=1: the plane-kernel path instead of the field: each dab's plane (area normal + centre) from the proxy,
        // the brush kernel evaluated per dense vertex (as the GPU preview does for plane brushes), from the original mesh.
        const bool planesMode = std::getenv("SF_RND_PLANES") != nullptr;
        if (planesMode) {
            Document q;
            {
                Mesh m;
                m.positions = orig;
                m.triangles = d.mesh.triangles;
                m.rebuildTopology();
                m.normals = origN;
                q.setMesh(std::move(m));
            }
            gRndPlanes = &planes, gRndPlanesUse = true, gRndPlaneAt = 0;
            const Run qr = stroke(q, s, dabs, &pr.centers);
            gRndPlanes = nullptr, gRndPlanesUse = false;
            std::printf("  plane-kernel path: %zu planes from the proxy, dense kernel %.0f verts/dab (would run once per batch)\n",
                        planes.size(), double(qr.touched) / std::max(1, qr.hits));
            planeDense = q.mesh.positions;
        }
        double centerOff = 0.0;
        for (size_t i = 0; i < std::min(pr.centers.size(), ex.centers.size()); ++i) centerOff = std::max(centerOff, double(glm::distance(pr.centers[i], ex.centers[i])));
        double angle = 0.0;
        for (size_t i = 0; i < std::min(pr.areaNormals.size(), ex.areaNormals.size()); ++i)
            angle = std::max(angle, double(std::acos(std::clamp(glm::dot(pr.areaNormals[i], ex.areaNormals[i]), -1.0f, 1.0f))) * 57.29578);
        std::printf("  dab centres vs exact: max %.2e r; area normals vs exact: max %.2f deg\n", centerOff / s.radius, angle);

        // The dense pass: every vertex whose own cell moved takes the trilinear field of the cell displacements.
        const auto tf = Clock::now();
        std::vector<glm::vec3> disp(np);
        std::vector<uint8_t> moved(np, 0);
        for (size_t i = 0; i < np; ++i) {
            disp[i] = p.mesh.positions[i] - proxyStart[i];
            moved[i] = disp[i] != glm::vec3(0.0f);
        }
        std::vector<glm::vec3> dense = planeDense.empty() ? orig : planeDense;
        const bool fieldOn = planeDense.empty();
        std::atomic<size_t> densePassed{0};
        poolFor(nv, [&](size_t v0, size_t v1) {
            size_t n = 0;
            for (size_t v = v0; v < v1; ++v) {
                if (!fieldOn || !moved[proxyOf[v]]) continue;
                ++n;
                const glm::vec3 g = (orig[v] - b.min) / h - 0.5f;
                const glm::vec3 g0 = glm::floor(g), f = g - g0;
                const uint32_t bin = nbin ? normalBin(origN[v]) : 0;
                glm::vec3 acc(0.0f);
                float w = 0.0f;
                for (int c = 0; c < 8; ++c) {
                    const int dx = c & 1, dy = (c >> 1) & 1, dz = (c >> 2) & 1;
                    const uint64_t pk = cellKey(int64_t(g0.x) + dx, int64_t(g0.y) + dy, int64_t(g0.z) + dz);
                    const auto it = cellOf.find(nbin ? cellKey(int64_t(g0.x) + dx, int64_t(g0.y) + dy, int64_t(g0.z) + dz, bin)
                                                     : pk | uint64_t(side(pk, origN[v])) << 60);
                    if (it == cellOf.end()) continue;
                    const float wc = (dx ? f.x : 1.0f - f.x) * (dy ? f.y : 1.0f - f.y) * (dz ? f.z : 1.0f - f.z);
                    acc += wc * disp[it->second];
                    w += wc;
                }
                if (w > 1e-6f) dense[v] += acc / w;
            }
            densePassed += n;
        });
        const double flushMs = msSince(tf);
        // SF_RND_FOLD_TAU=t: cells whose dense normals disagree (|sum n| / count < t: a fold, a thin part) take the exact path
        // (simulated: the exact stroke's positions for their vertices); their count per dab is the fallback's cost.
        size_t foldVerts = 0;
        if (const char* ft = std::getenv("SF_RND_FOLD_TAU")) {
            const float tau = float(std::atof(ft));
            std::vector<glm::vec3> nsum(np, glm::vec3(0.0f));
            for (size_t v = 0; v < nv; ++v) nsum[proxyOf[v]] += origN[v];
            for (size_t v = 0; v < nv; ++v) {
                const uint32_t c = proxyOf[v];
                if (glm::length(nsum[c]) >= tau * float(count[c])) continue;
                if (exact[v] == orig[v] && dense[v] == orig[v]) continue;
                dense[v] = exact[v];
                ++foldVerts;
            }
            std::printf("  fold fallback (tau %.2f): %zu verts exact over the stroke (%.0f per dab)\n", double(tau), foldVerts,
                        double(foldVerts) / std::max(1, pr.hits));
        }

        // Error at full resolution vs the exact stroke, over the vertices either moved.
        std::vector<float> e;
        e.reserve(nv / 8);
        double sq = 0.0;
        for (size_t v = 0; v < nv; ++v) {
            if (exact[v] == orig[v] && dense[v] == orig[v]) continue;
            const float dv = glm::distance(exact[v], dense[v]);
            e.push_back(dv);
            sq += double(dv) * dv;
        }
        { // where the big errors are: exact move vs propagated move, and the vertex's facing (normal . -ray)
            size_t big = 0, exactOnly = 0, denseOnly = 0, backFacing = 0;
            double exSum = 0.0, deSum = 0.0;
            for (size_t v = 0; v < nv; ++v) {
                const float dv = glm::distance(exact[v], dense[v]);
                if (dv < 0.01f * s.radius) continue;
                ++big;
                const float me = glm::distance(exact[v], orig[v]), md = glm::distance(dense[v], orig[v]);
                exSum += me, deSum += md;
                exactOnly += md < 0.1f * me;
                denseOnly += me < 0.1f * md;
                backFacing += glm::dot(origN[v], -s.dir) < 0.0f;
            }
            std::printf("  errors > 1%% r: %zu verts, mean exact move/r %.2e, propagated/r %.2e, exact-only %zu, propagated-only %zu, back-facing %zu\n",
                        big, big ? exSum / double(big) / s.radius : 0.0, big ? deSum / double(big) / s.radius : 0.0, exactOnly, denseOnly, backFacing);
        }
        std::sort(e.begin(), e.end());
        const auto at = [&](double q) { return e.empty() ? 0.0f : e[std::min(e.size() - 1, size_t(q * double(e.size() - 1)))]; };
        const double rr = double(s.radius);
        double exactMax = 0.0; // (the stroke's own size: the biggest exact move)
        for (size_t v = 0; v < nv; ++v) exactMax = std::max(exactMax, double(glm::distance(exact[v], orig[v])));
        std::printf("rnd proxy h=r/%.0f: proxy %zu verts %zu tris (build %.0f ms) | %.0f verts touched/dab, %.2f ms/dab (%d hits) |"
                    " dense pass %zu verts %.0f ms | error/r max %.2e p99.9 %.2e p99 %.2e RMS %.2e (n %zu; biggest exact move/r %.2e)\n",
                    double(cpr), np, ntp, buildMs, double(pr.touched) / std::max(1, pr.hits), pr.dabMs / std::max(1, pr.hits), pr.hits,
                    densePassed.load(), flushMs, at(1.0) / rr, at(0.999) / rr, at(0.99) / rr,
                    e.empty() ? 0.0 : std::sqrt(sq / double(e.size())) / rr, e.size(), exactMax / rr);
        { // In screen terms (CTO 11-12): r is the 240 px brush, viewed along the stroke's ray. Front-facing vertices only
            // (what the view can show): the error across the screen (px) and along the view (depth, as px of the same scale).
            const float pxPerUnit = 240.0f / s.radius;
            std::vector<float> across, along;
            for (size_t v = 0; v < nv; ++v) {
                if (exact[v] == orig[v] && dense[v] == orig[v]) continue;
                if (glm::dot(origN[v], -s.dir) <= 0.0f) continue;
                const glm::vec3 dv = dense[v] - exact[v];
                const float z = glm::dot(dv, s.dir);
                along.push_back(std::abs(z) * pxPerUnit);
                across.push_back(glm::length(dv - s.dir * z) * pxPerUnit);
            }
            std::sort(across.begin(), across.end()), std::sort(along.begin(), along.end());
            const auto q = [](const std::vector<float>& x, double f) { return x.empty() ? 0.0f : x[std::min(x.size() - 1, size_t(f * double(x.size() - 1)))]; };
            std::printf("  screen (240 px brush, front-facing %zu): across max %.2f p99.9 %.2f p99 %.2f px | depth max %.2f p99.9 %.2f px\n",
                        across.size(), double(q(across, 1.0)), double(q(across, 0.999)), double(q(across, 0.99)), double(q(along, 1.0)),
                        double(q(along, 0.999)));
        }
        d.mesh.positions = dense;
        promises("proxy");
        d.mesh.positions = exactKeep;
    }
}
