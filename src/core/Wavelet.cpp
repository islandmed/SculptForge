// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/Wavelet.h"

#include "core/BVH.h"
#include "core/Benchmark.h"
#include "core/HardwareProfile.h"
#include "core/Micro.h"
#include "core/Sculpt.h"
#include "core/ThreadPool.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <vector>

namespace sf {

namespace {

// One lifting stage: every point new at spacing s gets (forward) its height less the floor-average of its two
// parents on the spacing-2s lattice, or (inverse) that back. Modulo 2^16 both ways, so always exactly invertible.
void stage(int16_t* g, uint32_t n, uint32_t s, bool forward) {
    for (uint32_t j = 0; j <= n; j += s)
        for (uint32_t i = 0; i + j <= n; i += s) {
            const bool io = (i / s) & 1, jo = (j / s) & 1;
            if (!io && !jo) continue; // (on the coarser lattice)
            uint32_t ai, aj, bi, bj;
            if (io && !jo) ai = i - s, aj = j, bi = i + s, bj = j;      // along a row
            else if (!io && jo) ai = i, aj = j - s, bi = i, bj = j + s; // along a column
            else ai = i - s, aj = j + s, bi = i + s, bj = j - s;        // along the diagonal
            const int a = g[MicroMesh::index(ai, aj, n)], b = g[MicroMesh::index(bi, bj, n)];
            const int p = (a + b) >> 1; // floor
            int16_t& v = g[MicroMesh::index(i, j, n)];
            v = int16_t(uint16_t(uint16_t(v) + uint16_t(forward ? -p : p)));
        }
}

} // namespace

void waveletForward(int16_t* grid, uint32_t n) {
    for (uint32_t s = 1; s < n; s <<= 1) stage(grid, n, s, true);
}

void waveletInverse(int16_t* grid, uint32_t n) {
    for (uint32_t s = n >> 1; s >= 1; s >>= 1) stage(grid, n, s, false);
}

void waveletAddCoarse(int16_t* coeffs, uint32_t n, uint32_t s, const std::function<int(uint32_t, uint32_t, int)>& delta) {
    s = std::clamp<uint32_t>(s, 1, n);
    for (uint32_t t = n >> 1; t >= s && t >= 1; t >>= 1) stage(coeffs, n, t, false); // the lattice's heights
    for (uint32_t j = 0; j <= n; j += s)
        for (uint32_t i = 0; i + j <= n; i += s) {
            int16_t& v = coeffs[MicroMesh::index(i, j, n)];
            v = int16_t(std::clamp(int(v) + delta(i, j, int(v)), -32767, 32767));
        }
    for (uint32_t t = s; t < n; t <<= 1) stage(coeffs, n, t, true); // back to coefficients; finer ones untouched
}

int runWaveletBench(const std::string& outPath) {
    using Clock = std::chrono::steady_clock;
    auto ms = [](Clock::time_point t) { return std::chrono::duration<double, std::milli>(Clock::now() - t).count(); };
    if (simulatedFloorTier()) ThreadPool::setSharedThreadCount(4); // SF_HW_SIMULATE=floor: 4 cores
    std::fprintf(stderr, "core-200m N2: building a Micro 7 head...\n");
    Mesh m = makeCore200mMesh(20000, 7);
    if (!m.micro) {
        std::fprintf(stderr, "core-200m N2: no micro level\n");
        return 1;
    }
    BVH bvh;
    bvh.build(m);
    MicroMesh& mm = mutableMicro(m);
    mm.ensureTopology(m);
    const AABB box = m.bounds();
    const float height = box.max.y - box.min.y;
    // Room for the largest dab, so neither path rescales the heights (the comparison stays in one unit).
    mm.widen(float(mm.rangeMax) * mm.step + 0.5f * 0.2f * height * 0.06f * 1.5f);
    const uint32_t n = mm.n, S = mm.samples;
    const glm::vec3 c = (box.min + box.max) * 0.5f;
    RayHit hit;
    if (!surfaceRaycast(m, bvh, glm::vec3(c.x + 0.1f * height, c.y + 0.05f * height, box.max.z + height), glm::vec3(0, 0, -1), hit)) {
        std::fprintf(stderr, "core-200m N2: no surface in front\n");
        return 1;
    }
    std::ostringstream md;
    md << "## Wavelet (multi-scale) displacement (N2): " << double(mm.microTriangles()) / 1e6 << "M micro triangles, Micro "
       << mm.level << ", " << mm.baseTriangles << " patches, " << ThreadPool::shared().threads() << " threads\n\n"
       << "Standard, strength 0.5, one dab. Today = the adaptive micro brush (MicroStroke: kernel on a stride, written "
          "to every fine point, stitched). Wavelet = the kernel added to the coefficients of the lattice where the radius "
          "spans >= 64 samples (finer coefficients untouched); reconstruct = the inverse transform of the touched patches "
          "(the lazy push-up, needed only for what is drawn or picked). Error: |x - exact| over the touched points, exact "
          "= the brush evaluated at every fine point, as a share of its largest change.\n\n"
       << "| Radius (~px at a framing view) | patches | today: dab | wavelet: lattice | values written today / wavelet | "
          "wavelet: dab | reconstruct | wavelet error (mean / max) | today's error (mean / max) |\n|---|---:|---:|---:|---:|---:|---:|---:|---:|\n";
    std::vector<int16_t> original(mm.disp.begin(), mm.disp.end());
    for (float frac : {0.025f, 0.05f, 0.1f, 0.2f}) {
        const float r = frac * height;
        // Today's brush.
        std::copy(original.begin(), original.end(), mm.disp.begin());
        MicroStroke stroke;
        stroke.begin(m);
        const auto t0 = Clock::now();
        stroke.dab(m, bvh, BrushType::Standard, hit.position, glm::vec3(0, 0, -1), r, 0.5f, false, 0.0f, glm::vec3(0, 1, 0), false, 0.0f);
        const double todayMs = ms(t0);
        const uint64_t todayPoints = stroke.points;
        stroke.end(m);
        std::vector<int16_t> today(mm.disp.begin(), mm.disp.end());
        // The exact brush (every fine point evaluated): the reference both are measured against.
        std::copy(original.begin(), original.end(), mm.disp.begin());
        MicroStroke exactStroke;
        exactStroke.adaptive = false;
        exactStroke.begin(m);
        exactStroke.dab(m, bvh, BrushType::Standard, hit.position, glm::vec3(0, 0, -1), r, 0.5f, false, 0.0f, glm::vec3(0, 1, 0), false, 0.0f);
        exactStroke.end(m);
        std::vector<int16_t> exact(mm.disp.begin(), mm.disp.end());
        std::copy(original.begin(), original.end(), mm.disp.begin());
        // The patches the dab can reach (as the brush finds them: base triangles near the centre).
        std::vector<uint32_t> tris;
        bvh.queryTriangles(m, hit.position, r + mm.bulgeMax + float(mm.rangeMax) * mm.step, tris);
        // Stored as coefficients (the one-time transform, not part of a dab).
        for (uint32_t t : tris) waveletForward(mm.disp.data() + size_t(t) * S, n);
        uint32_t s = 1; // the coarsest lattice where the radius still spans >= 64 samples
        while (s < n / 2 && r / (mm.baseEdge * float(2 * s) / float(n)) >= 64.0f) s <<= 1;
        const float invStep = 1.0f / mm.step, amp = 0.5f * r * 0.06f;
        // The brush's area normal: the interpolated base normals under it, weighted by the falloff (as the brush's).
        glm::vec3 nsum(0.0f);
        for (uint32_t t : tris) {
            MicroTriangle T;
            T.set(m, t, n);
            for (uint32_t j = 0; j <= n; j += s)
                for (uint32_t i = 0; i + j <= n; i += s) {
                    glm::vec3 sp, nn;
                    T.eval(i, j, sp, nn);
                    const float dist = glm::length(sp - hit.position);
                    if (dist < r) nsum += nn * (1.0f - dist / r);
                }
        }
        const glm::vec3 an = glm::length(nsum) > 0 ? glm::normalize(nsum) : glm::normalize(hit.normal);
        std::atomic<uint64_t> written{0};
        const auto t1 = Clock::now();
        poolFor(tris.size(), [&](size_t b, size_t e) {
            uint64_t w = 0;
            for (size_t k = b; k < e; ++k) {
                MicroTriangle T;
                T.set(m, tris[k], n);
                waveletAddCoarse(mm.disp.data() + size_t(tris[k]) * S, n, s, [&](uint32_t i, uint32_t j, int h) {
                    glm::vec3 sp, nn;
                    T.eval(i, j, sp, nn);
                    const float dist = glm::length(sp + nn * (float(h) * mm.step) - hit.position);
                    if (dist >= r) return 0;
                    const float tt = 1.0f - dist / r;
                    ++w;
                    return int(std::lround(amp * tt * tt * (3.0f - 2.0f * tt) * glm::dot(an, nn) * invStep));
                });
            }
            written += w;
        }, 4);
        const double waveletMs = ms(t1);
        const auto t2 = Clock::now();
        poolFor(tris.size(), [&](size_t b, size_t e) {
            for (size_t k = b; k < e; ++k) waveletInverse(mm.disp.data() + size_t(tris[k]) * S, n);
        }, 4);
        const double reconstructMs = ms(t2);
        double sumW = 0, maxW = 0, sumT = 0, maxT = 0, maxChange = 0;
        size_t count = 0;
        for (uint32_t t : tris)
            for (uint32_t k = 0; k < S; ++k) {
                const size_t i = size_t(t) * S + k;
                maxChange = std::max(maxChange, double(std::abs(int(exact[i]) - int(original[i]))));
                const double dw = std::abs(int(mm.disp[i]) - int(exact[i])), dt = std::abs(int(today[i]) - int(exact[i]));
                sumW += dw, maxW = std::max(maxW, dw), sumT += dt, maxT = std::max(maxT, dt), ++count;
            }
        const double scale = 100.0 / std::max(maxChange, 1.0), per = 1.0 / double(std::max<size_t>(count, 1));
        char row[512];
        std::snprintf(row, sizeof row, "| %.3f (~%.0f px) | %zu | %.2f ms | spacing %u (%u per edge) | %.2fM / %.1fk | %.2f ms | %.1f ms | %.2f%% / %.1f%% | %.2f%% / %.1f%% |\n",
                      double(r), double(frac * 1080.0f), tris.size(), todayMs, s, n / s, double(todayPoints) / 1e6,
                      double(written.load()) / 1e3, waveletMs, reconstructMs, sumW * per * scale, maxW * scale, sumT * per * scale, maxT * scale);
        md << row;
        std::fprintf(stderr, "%s", row);
    }
    std::copy(original.begin(), original.end(), mm.disp.begin());
    md << "\nValues written today: grid points the brush visited (MicroStroke::points). The wavelet dab's cost follows "
          "the lattice points, so it falls (relative to today) as the brush grows.\n";
    if (outPath.empty()) std::printf("%s", md.str().c_str());
    else std::ofstream(outPath) << md.str();
    return 0;
}

} // namespace sf
