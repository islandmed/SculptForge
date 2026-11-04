// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/Wavelet.h"

#ifdef _WIN32
#include <windows.h>
#endif

#include "core/BVH.h"
#include "core/Benchmark.h"
#include "core/HardwareProfile.h"
#include "core/Micro.h"
#include "core/Sculpt.h"
#include "core/ThreadPool.h"
#include "core/TierConfig.h"

#include <algorithm>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <thread>
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

void waveletToLattice(int16_t* coeffs, uint32_t n, uint32_t s) {
    for (uint32_t t = n >> 1; t >= s && t >= 1; t >>= 1) stage(coeffs, n, t, false);
}

void waveletFromLattice(int16_t* grid, uint32_t n, uint32_t s) {
    for (uint32_t t = s; t < n; t <<= 1) stage(grid, n, t, true);
}

void waveletHeightsToLattice(int16_t* heights, uint32_t n, uint32_t s) {
    for (uint32_t t = 1; t < s && t < n; t <<= 1) stage(heights, n, t, true);
}

void waveletAddCoarse(int16_t* coeffs, uint32_t n, uint32_t s, const std::function<int(uint32_t, uint32_t, int)>& delta) {
    s = std::clamp<uint32_t>(s, 1, n);
    waveletToLattice(coeffs, n, s); // the lattice's heights
    for (uint32_t j = 0; j <= n; j += s)
        for (uint32_t i = 0; i + j <= n; i += s) {
            int16_t& v = coeffs[MicroMesh::index(i, j, n)];
            v = int16_t(std::clamp(int(v) + delta(i, j, int(v)), -32767, 32767));
        }
    waveletFromLattice(coeffs, n, s); // back to coefficients; finer ones untouched
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

int runWaveletStrokeBench(const std::string& outPath) {
    using Clock = std::chrono::steady_clock;
    auto ms = [](Clock::time_point a, Clock::time_point b) { return std::chrono::duration<double, std::milli>(b - a).count(); };
    if (simulatedFloorTier()) ThreadPool::setSharedThreadCount(4); // SF_HW_SIMULATE=floor: 4 cores
    // Windows A/B (research): SF_BENCH_THREADS=N threads (1: no pool, the codegen alone); SF_BENCH_TIMER1=1 asks
    // Windows for a 1 ms timer (timeBeginPeriod, winmm) so the frame loop's sleeps don't round up to a 15.6 ms tick.
    if (const char* e = std::getenv("SF_BENCH_THREADS")) ThreadPool::setSharedThreadCount(std::atoi(e));
    bool timer1 = false;
#ifdef _WIN32
    if (const char* e = std::getenv("SF_BENCH_TIMER1"); e && e[0] == '1')
        if (HMODULE winmm = LoadLibraryA("winmm.dll"))
            if (auto begin = reinterpret_cast<unsigned(WINAPI*)(unsigned)>(GetProcAddress(winmm, "timeBeginPeriod"))) timer1 = begin(1) == 0;
#endif
    // How much a requested 1 ms sleep really takes (the timer's granularity), median of 20.
    std::vector<double> naps;
    for (int i = 0; i < 20; ++i) {
        const auto a = Clock::now();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        naps.push_back(ms(a, Clock::now()));
    }
    std::sort(naps.begin(), naps.end());
    const char* spin = std::getenv("SF_POOL_SPIN_US");
    // SF_BENCH_SHADE_ORDER=fifo: the shading queue takes the patches the brush has left first (oldest first), then
    // this frame's; default: this frame's first (MicroRenderer::shadePending today).
    const char* order = std::getenv("SF_BENCH_SHADE_ORDER");
    const bool fifoOrder = order && std::string(order) == "fifo";
    std::fprintf(stderr, "core-200m N2 stroke: building a Micro 7 head...\n");
    Mesh m = makeCore200mMesh(20000, 7);
    if (!m.micro) return 1;
    BVH bvh;
    bvh.build(m);
    MicroMesh& mm = mutableMicro(m);
    mm.ensureTopology(m);
    const AABB box = m.bounds();
    const float height = box.max.y - box.min.y;
    // Room for the largest stroke up front, so no run rescales the heights (every run in one unit).
    mm.widen(float(mm.rangeMax) * mm.step + 0.2f * height * 0.06f * 0.5f * 12.0f);
    const std::vector<int16_t> original(mm.disp.begin(), mm.disp.end());
    const std::vector<uint16_t> originalRange = mm.triRange;
    const uint16_t originalRangeMax = mm.rangeMax;
    const float step0 = mm.step;
    auto reset = [&] {
        std::copy(original.begin(), original.end(), mm.disp.begin());
        mm.triRange = originalRange;
        mm.rangeMax = originalRangeMax;
        mm.clearDirty();
    };
    const glm::vec3 c = (box.min + box.max) * 0.5f;
    MicroShader shader;
    std::vector<uint32_t> written, dirty;
    auto scratch = [&](uint32_t) -> uint32_t* {
        thread_local std::vector<uint32_t> tile;
        tile.resize(mm.samples);
        return tile.data();
    };
    shader.shade(m, {}, true, written, scratch); // (the first full pass, as the renderer's)
    mm.clearDirty();
    // The renderer's shading budget while a stroke runs (MicroRenderer::sync: seeds whose ring triples them). The CLI
    // has no hardware profile, so the app's on a Laptop-tier machine (TierConfig: 1M points, draft brushes off; the
    // floor simulation keeps it), or SF_BENCH_SHADE_BUDGET points.
    TierConfig tier = currentTierConfig();
    tier.shadeBudgetPoints = 1u << 20;
    tier.draftBrushMode = false;
    if (const char* e = std::getenv("SF_BENCH_SHADE_BUDGET")) tier.shadeBudgetPoints = std::strtoull(e, nullptr, 10);
    const size_t budget = size_t(tier.shadeBudgetPoints) / (tier.draftBrushMode ? 4 : 1);
    const size_t limit = std::max<size_t>(1, budget / (size_t(mm.samples) * 3));
    constexpr int kDabs = 30;
    constexpr double kPenMs = 1000.0 / 120.0, kFrameMs = 1000.0 / 60.0, kBudgetMs = 10.0;
    std::ostringstream md;
    md << "## Wavelet strokes (N2 in MicroStroke): " << double(mm.microTriangles()) / 1e6 << "M micro triangles, Micro " << mm.level
       << ", " << ThreadPool::shared().threads() << " threads, shading budget " << budget << " points a frame ("
       << limit << " patches + ring" << (tier.draftBrushMode ? ", draft" : "") << "); environment: SF_HW_SIMULATE="
       << (simulatedFloorTier() ? "floor" : "-") << ", 1 ms timer " << (timer1 ? "on" : "off") << ", SF_POOL_SPIN_US="
       << (spin ? spin : "-") << ", shading order " << (fifoOrder ? "fifo" : "newest") << ", a 1 ms sleep takes " << naps[10] << " ms\n\n"
       << "Standard, strength 0.5, " << kDabs << " dabs 0.1 radius apart across the face; the pen delivers a dab every "
       << kPenMs << " ms; a frame places the dabs that have arrived (pick + dab, at least one, until 10 ms), rebuilds the "
          "changed patches (wavelet), shades up to the renderer's budget of what changed (MicroShader, MicroRenderer::sync's queue: the rest waits) and "
          "waits for the next 60 Hz tick. Lag (committed) = a dab's arrival to the end of the frame whose heights hold it; "
          "pending = patches whose shading still lags at release, catch-up = shading them all then. Error against the exact brush (every "
          "fine point) as a share of the stroke's height. Runs interleaved: off, on, off, on.\n\n"
       << "| Radius | brush | lattice | dabs / frame | dab (pick + dab) | rebuild / frame | shading / frame | stroke end | stroke total | "
          "lag p50 / p99 | pending, catch-up | error mean / max |\n|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|\n";
    for (float frac : {0.025f, 0.05f, 0.1f, 0.2f}) {
        const float r = frac * height;
        // The path: picks on the untouched surface (every run then places the same dabs; each still picks for real).
        std::vector<glm::vec3> centers, origins;
        for (int i = 0; i < kDabs; ++i) {
            const glm::vec3 o(c.x - 1.5f * r + 0.1f * r * float(i), c.y + 0.05f * height + 0.1f * r * std::sin(float(i) * 0.3f), box.max.z + height);
            RayHit hit;
            if (surfaceRaycast(m, bvh, o, glm::vec3(0, 0, -1), hit)) centers.push_back(hit.position), origins.push_back(o);
        }
        auto stroke = [&](MicroStroke& st) {
            st.begin(m);
            for (const glm::vec3& p : centers)
                st.dab(m, bvh, BrushType::Standard, p, glm::vec3(0, 0, -1), r, 0.5f, false, 0.0f, glm::vec3(0, 1, 0), false, 0.0f);
            st.end(m);
        };
        reset();
        MicroStroke exactStroke;
        exactStroke.adaptive = false;
        exactStroke.wavelet = false;
        stroke(exactStroke);
        const std::vector<int16_t> exact(mm.disp.begin(), mm.disp.end());
        for (int run = 0; run < 4; ++run) {
            const bool on = run & 1;
            reset();
            MicroStroke st;
            st.wavelet = on;
            st.begin(m);
            std::vector<double> lags;
            std::vector<uint8_t> pending(mm.baseTriangles, 0);
            std::vector<uint32_t> lastDirty(mm.baseTriangles, 0), fifo;
            std::vector<uint32_t> seeds;
            size_t pendingCount = 0;
            double dabMs = 0, rebuildMs = 0, shadeMs = 0;
            int frames = 0;
            size_t next = 0;
            const auto t0 = Clock::now();
            while (next < centers.size()) {
                const auto frame = Clock::now();
                std::vector<double> arrivals;
                while (next < centers.size()) {
                    const double arrival = double(next) * kPenMs;
                    if (arrival > ms(t0, Clock::now()) && !arrivals.empty()) break;
                    if (arrival > ms(t0, Clock::now())) std::this_thread::sleep_until(t0 + std::chrono::microseconds(int64_t(arrival * 1000.0)));
                    const auto d0 = Clock::now();
                    RayHit hit;
                    surfaceRaycast(m, bvh, origins[next], glm::vec3(0, 0, -1), hit); // (the pick, as the app's; reads the heights)
                    st.dab(m, bvh, BrushType::Standard, centers[next], glm::vec3(0, 0, -1), r, 0.5f, false, 0.0f, glm::vec3(0, 1, 0), false, 0.0f);
                    dabMs += ms(d0, Clock::now());
                    arrivals.push_back(arrival);
                    ++next;
                    if (ms(frame, Clock::now()) >= kBudgetMs) break;
                }
                const double f0 = st.flushMs;
                st.flush(m);
                rebuildMs += st.flushMs - f0;
                const auto s0 = Clock::now();
                // As MicroRenderer::sync: this frame's changed patches join the pending ones; the shading budget's
                // worth of them (this frame's first) is shaded with its ring, the rest waits.
                dirty.clear();
                for (size_t w = 0; w < mm.dirtyBits.size(); ++w)
                    for (uint64_t bits = mm.dirtyBits[w]; bits; bits &= bits - 1) dirty.push_back(uint32_t(w * 64 + size_t(std::countr_zero(bits))));
                mm.clearDirty();
                for (uint32_t t : dirty) {
                    if (!pending[t]) pending[t] = 1, ++pendingCount, fifo.push_back(t);
                    lastDirty[t] = uint32_t(frames + 1);
                }
                seeds.clear();
                if (fifoOrder) { // the patches the brush has left first, oldest first; then this frame's
                    size_t keep = 0;
                    for (size_t k = 0; k < fifo.size(); ++k) {
                        const uint32_t t = fifo[k];
                        if (!pending[t]) continue; // (shaded meanwhile)
                        if (seeds.size() < limit && lastDirty[t] != uint32_t(frames + 1)) seeds.push_back(t);
                        else fifo[keep++] = t;
                    }
                    fifo.resize(keep);
                    for (size_t k = 0; k < dirty.size() && seeds.size() < limit; ++k) seeds.push_back(dirty[k]);
                } else {
                    seeds.assign(dirty.begin(), dirty.end());
                    for (size_t t = 0; t < pending.size() && seeds.size() < limit; ++t)
                        if (pending[t]) seeds.push_back(uint32_t(t));
                }
                std::sort(seeds.begin(), seeds.end());
                seeds.erase(std::unique(seeds.begin(), seeds.end()), seeds.end());
                if (seeds.size() > limit) seeds.resize(limit);
                if (!seeds.empty()) shader.shade(m, seeds, false, written, scratch);
                for (uint32_t t : written)
                    if (pending[t]) pending[t] = 0, --pendingCount;
                shadeMs += ms(s0, Clock::now());
                const double shown = ms(t0, Clock::now());
                for (double a : arrivals) lags.push_back(shown - a); // (committed: its heights are in this frame)
                ++frames;
                const auto tick = frame + std::chrono::microseconds(int64_t(kFrameMs * 1000.0));
                if (Clock::now() < tick && next < centers.size()) std::this_thread::sleep_until(tick);
            }
            const auto e0 = Clock::now();
            st.end(m);
            const double endMs = ms(e0, Clock::now()), totalMs = ms(t0, Clock::now());
            // Release: the shading still pending, all at once (what the picture waits for after the pen lifts).
            const size_t pendingAtEnd = pendingCount;
            const auto c0 = Clock::now();
            seeds.clear();
            for (size_t t = 0; t < pending.size(); ++t)
                if (pending[t]) seeds.push_back(uint32_t(t));
            if (!seeds.empty()) shader.shade(m, seeds, false, written, scratch);
            mm.clearDirty();
            const double catchUpMs = ms(c0, Clock::now());
            std::sort(lags.begin(), lags.end());
            double sum = 0, worst = 0, h = 0;
            size_t count = 0;
            for (size_t k = 0; k < exact.size(); ++k) {
                const double dc = std::abs(int(exact[k]) - int(original[k]));
                h = std::max(h, dc);
                const double dw = std::abs(int(mm.disp[k]) - int(exact[k]));
                if (dc == 0 && dw == 0) continue;
                sum += dw, worst = std::max(worst, dw), ++count;
            }
            const double scale = 100.0 / std::max(h, 1.0);
            char row[512];
            std::snprintf(row, sizeof row, "| %.0f px | %s | %u (%llu wavelet dabs) | %.1f | %.2f ms | %.2f ms | %.2f ms | %.1f ms | %.0f ms | %.0f / %.0f ms | %zu, %.0f ms | %.3f%% / %.2f%% |\n",
                          double(frac * 1080.0f), on ? "wavelet" : "today", st.lastStride, (unsigned long long)st.waveletDabs, double(centers.size()) / double(std::max(frames, 1)),
                          dabMs / double(std::max<size_t>(centers.size(), 1)), rebuildMs / double(std::max(frames, 1)),
                          shadeMs / double(std::max(frames, 1)), endMs, totalMs, lags.empty() ? 0.0 : lags[lags.size() / 2],
                          lags.empty() ? 0.0 : lags[std::min(lags.size() - 1, size_t(double(lags.size()) * 0.99))], pendingAtEnd, catchUpMs, sum / double(std::max<size_t>(count, 1)) * scale, worst * scale);
            md << row;
            std::fprintf(stderr, "%s", row);
        }
    }
    reset();
    if (mm.step != step0) md << "\n(The heights were rescaled during a run: the error column is not comparable.)\n";
    if (outPath.empty()) std::printf("%s", md.str().c_str());
    else std::ofstream(outPath) << md.str();
    return 0;
}

} // namespace sf
