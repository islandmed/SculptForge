// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Detail per stroke (the 10x scoreboard's axis 3, briefs/forge-ink-10x.md): how fine a feature ONE dab resolves on a
// Micro level, and what it costs. A hard-edged alpha (a step: left half 1, right half 0) and a one-texel line (one
// column of a 512 px alpha) are stamped with Standard at focal -1 (no falloff softening) on a smooth bench head, and
// the height change is read along a transect through the dab centre (ray casts on the micro surface before and after,
// so the base's curvature cancels). Metrics, both in units of the brush radius r:
//   edge w10-90  the distance over which the step's height rises from 10 % to 90 % (an ideal step: 0)
//   line FWHM    the full width at half maximum of the one-texel line (the alpha's own line is r / 256 wide)
// and sharpness = r / w (how many such features fit across the brush). Two guards (artist-qa 11-17) so a sharper number
// can't hide what a close-up would show: the step is also stamped at 45 degrees to the grid, and across 9 transects
// along the edge the WOBBLE of the edge position (its peak-to-peak, / r: stair-stepping) and the OVERSHOOT at the step
// (the most the high side rises above its plateau, % of the step height: ringing) are reported. Beside them: the micro cell at the dab
// (baseEdge / n), the stride the adaptive kernel chose (points about r / kBrushSamples apart) and the kernel's ms,
// so "10x finer at the same cost" is one line of this table against another.
//
//   sf_detail_bench [baseTriangles=61035] [level=5] [radiiOverH=0.02,0.05,0.1,0.3] [--every-point]
//
// --every-point: MicroStroke::adaptive off (the grid itself is the only limit: the floor of today's metric).
#include "core/BVH.h"
#include "core/Benchmark.h"
#include "core/BrushAlpha.h"
#include "core/Micro.h"
#include "core/ThreadPool.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

using namespace sf;
using Clock = std::chrono::steady_clock;

namespace {

double ms(Clock::time_point a) { return std::chrono::duration<double, std::milli>(Clock::now() - a).count(); }

double median(std::vector<double> v) {
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

BrushAlpha stepAlpha(int size = 512) {
    BrushAlpha a;
    a.name = "Step", a.width = a.height = size;
    a.values.assign(size_t(size) * size, 0.0f);
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size / 2; ++x) a.values[size_t(y) * size + x] = 1.0f; // left half raised
    return a;
}

BrushAlpha lineAlpha(int size = 512) {
    BrushAlpha a;
    a.name = "Line", a.width = a.height = size;
    a.values.assign(size_t(size) * size, 0.0f);
    for (int y = 0; y < size; ++y) a.values[size_t(y) * size + size / 2] = 1.0f; // one column
    return a;
}

// Heights along the transect: the hit distance along a ray from above, per sample (NaN where the ray misses).
std::vector<double> transect(const Mesh& m, const BVH& bvh, const glm::vec3& center, const glm::vec3& right, const glm::vec3& n,
                             float r, int samples) {
    std::vector<double> t(size_t(samples), std::nan(""));
    for (int k = 0; k < samples; ++k) {
        const float x = (-1.2f + 2.4f * float(k) / float(samples - 1)) * r;
        RayHit hit;
        if (microRaycast(m, bvh, center + right * x + n * (3.0f * r), -n, hit)) t[size_t(k)] = hit.t;
    }
    return t;
}

struct Profile {
    double edgeW = -1, lineW = -1, amplitude = 0, x50 = 0, overshoot = 0; // x50: where the step crosses half; overshoot: fraction of the step
};

// The height change h(x) = before - after along the transect, and its edge / line widths (world units).
Profile analyse(const std::vector<double>& before, const std::vector<double>& after, float r, bool line) {
    const int N = int(before.size());
    std::vector<double> h(size_t(N), 0.0), x(size_t(N));
    for (int k = 0; k < N; ++k) {
        x[size_t(k)] = (-1.2 + 2.4 * double(k) / double(N - 1)) * r;
        h[size_t(k)] = std::isnan(before[size_t(k)]) || std::isnan(after[size_t(k)]) ? 0.0 : before[size_t(k)] - after[size_t(k)];
    }
    Profile p;
    if (!line) { // the step: high plateau on the left (x in [-0.6 r, -0.2 r]), low on the right ([0.2 r, 0.6 r])
        std::vector<double> hi, lo;
        for (int k = 0; k < N; ++k) {
            const double u = x[size_t(k)] / r;
            if (u > -0.6 && u < -0.2) hi.push_back(h[size_t(k)]);
            if (u > 0.2 && u < 0.6) lo.push_back(h[size_t(k)]);
        }
        const double H = median(hi), L = median(lo);
        p.amplitude = H - L;
        if (p.amplitude <= 0.0) return p;
        const double h90 = L + 0.9 * p.amplitude, h10 = L + 0.1 * p.amplitude;
        // From the left: the last sample still above 90 %, then the first below 10 % (the step falls left to right).
        const double h50 = L + 0.5 * p.amplitude;
        int a = -1, b = -1, c = -1;
        double over = 0.0;
        for (int k = 0; k < N; ++k)
            if (x[size_t(k)] > -0.6 * r && x[size_t(k)] < 0.6 * r) {
                if (h[size_t(k)] >= h90) a = k;
                if (c < 0 && a >= 0 && h[size_t(k)] <= h50) c = k;
                if (b < 0 && a >= 0 && h[size_t(k)] <= h10) b = k;
                if (x[size_t(k)] < -0.05 * r) over = std::max(over, h[size_t(k)] - H);
            }
        if (a >= 0 && b > a) p.edgeW = x[size_t(b)] - x[size_t(a)];
        if (c >= 0) p.x50 = x[size_t(c)];
        p.overshoot = over / p.amplitude;
    } else { // the line: a peak at the centre over a flat baseline (|x| > 0.3 r)
        std::vector<double> base;
        double peak = -1e30;
        for (int k = 0; k < N; ++k) {
            if (std::abs(x[size_t(k)]) > 0.3 * r) base.push_back(h[size_t(k)]);
            else peak = std::max(peak, h[size_t(k)]);
        }
        const double B = median(base);
        p.amplitude = peak - B;
        if (p.amplitude <= 0.0) return p;
        const double half = B + 0.5 * p.amplitude;
        int a = -1, b = -1;
        for (int k = 0; k < N; ++k)
            if (std::abs(x[size_t(k)]) <= 0.3 * r && h[size_t(k)] >= half) {
                if (a < 0) a = k;
                b = k;
            }
        if (a >= 0 && b >= a) p.lineW = x[size_t(b)] - x[size_t(a)] + 2.4 * r / double(N - 1);
    }
    return p;
}

} // namespace

int main(int argc, char** argv) {
    size_t baseTriangles = 61035;
    int level = 5;
    std::vector<float> radii = {0.02f, 0.05f, 0.1f, 0.3f};
    bool everyPoint = false;
    int pos = 0;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--every-point") == 0) everyPoint = true;
        else if (pos == 0) baseTriangles = size_t(std::atol(argv[i])), ++pos;
        else if (pos == 1) level = std::atoi(argv[i]), ++pos;
        else if (pos == 2) {
            radii.clear();
            for (const char* s = argv[i]; *s;) {
                radii.push_back(float(std::atof(s)));
                while (*s && *s != ',') ++s;
                if (*s) ++s;
            }
            ++pos;
        }
    }
    ThreadPool::setSharedThreadCount(4); // the floor tier's cores

    auto t0 = Clock::now();
    Mesh mesh = makeBenchHead(baseTriangles);
    std::string err;
    if (!microDivide(mesh, level, err, 0)) {
        std::fprintf(stderr, "detail-bench: %s\n", err.c_str());
        return 1;
    }
    BVH bvh;
    bvh.build(mesh);
    const MicroMesh& mm = *mesh.micro;
    glm::vec3 lo(1e30f), hi(-1e30f);
    for (const glm::vec3& p : mesh.positions) lo = glm::min(lo, p), hi = glm::max(hi, p);
    const float H = hi.y - lo.y; // the model's height: brush sizes and cells are given in it
    const float cell = mm.baseEdge / float(mm.n);
    std::printf("detail-bench: %zuM micro triangles (base %u, level %d, n %u), height %.3g, base edge %.3g = %.2e H, micro cell %.3g = %.2e H, built in %.1f s%s\n",
                mm.microTriangles() / 1000000, mm.baseTriangles, mm.level, mm.n, double(H), double(mm.baseEdge),
                double(mm.baseEdge / H), double(cell), double(cell / H), ms(t0) / 1000.0, everyPoint ? ", every grid point" : "");

    // The dab centre: the front-most vertex whose normal faces the viewer (the forehead / cheek plane).
    uint32_t best = 0;
    float bestScore = -1e30f;
    for (uint32_t v = 0; v < mesh.positions.size(); ++v) {
        const glm::vec3 n = glm::normalize(mesh.normals[v]);
        const float score = n.z * 2.0f + (mesh.positions[v].z - lo.z) / H - std::abs(mesh.positions[v].x - 0.5f * (lo.x + hi.x)) / H;
        if (n.z > 0.9f && score > bestScore) bestScore = score, best = v;
    }
    const glm::vec3 n = glm::normalize(mesh.normals[best]);
    glm::vec3 up = glm::normalize(glm::vec3(0, 1, 0) - n * n.y);
    const glm::vec3 right = glm::cross(up, n);
    const BrushAlpha step = stepAlpha(), line = lineAlpha();

    std::printf("\n| r / H | r / cell | stamp | stride | sample spacing / r | w / r (edge 10-90 %% or line FWHM) | sharpness r/w | edge wobble / r | overshoot %% | amplitude / r | kernel ms | find+undo ms | stitch ms | points |\n");
    std::printf("|---|---|---|---|---|---|---|---|---|---|---|---|---|---|\n");
    for (float rh : radii) {
        const float r = rh * H;
        const int N = 4001;
        for (int which = 0; which < 3; ++which) { // 0: step along the grid's up, 1: step at 45 degrees, 2: the line
            const bool isLine = which == 2;
            const float ang = which == 1 ? 0.7853982f : 0.0f;
            const glm::vec3 upR = glm::normalize(up * std::cos(ang) + right * std::sin(ang)); // the alpha's up (the edge runs along it)
            const glm::vec3 rightR = glm::cross(upR, n);                                       // the transect direction (across the edge)
            const glm::vec3 center = mesh.positions[best]; // (the same spot: each dab is undone below)
            // Transects across the edge, offset along it (the line: the centre one only).
            const int T = isLine ? 1 : 9;
            std::vector<std::vector<double>> before(size_t(T));
            for (int t = 0; t < T; ++t) {
                const float off = isLine ? 0.0f : (-0.3f + 0.6f * float(t) / float(T - 1)) * r;
                before[size_t(t)] = transect(mesh, bvh, center + upR * off, rightR, n, r, N);
            }
            MicroStroke st;
            st.adaptive = !everyPoint;
            st.begin(mesh);
            // One mirror, no mask: focal -1 keeps the alpha hard to its edges (Sculpt.h kAlphaReach).
            st.dab(mesh, bvh, BrushType::Standard, center, -n, r, 1.0f, false, -1.0f, upR, false, 0.0f, isLine ? &line : &step);
            const double kernel = st.kernelMs, find = st.findMs, stitch = st.stitchMs;
            const uint64_t points = st.points;
            const uint32_t stride = st.lastStride;
            std::unique_ptr<MicroUndo> undo = st.end(mesh);
            std::vector<double> widths, x50s, overs, amps;
            for (int t = 0; t < T; ++t) {
                const float off = isLine ? 0.0f : (-0.3f + 0.6f * float(t) / float(T - 1)) * r;
                const std::vector<double> after = transect(mesh, bvh, center + upR * off, rightR, n, r, N);
                const Profile p = analyse(before[size_t(t)], after, r, isLine);
                const double w = isLine ? p.lineW : p.edgeW;
                if (w > 0) widths.push_back(w), x50s.push_back(p.x50), overs.push_back(p.overshoot);
                amps.push_back(p.amplitude);
            }
            if (undo) undo->swapWith(mesh); // back to the smooth surface
            const double w = median(widths);
            double wobble = 0.0;
            if (x50s.size() > 1) wobble = *std::max_element(x50s.begin(), x50s.end()) - *std::min_element(x50s.begin(), x50s.end());
            const char* stamp = which == 0 ? "step 0 deg" : which == 1 ? "step 45 deg" : "line";
            std::printf("| %.3g | %.1f | %s | %u | %.4f | %s | %s | %s | %s | %.4f | %.2f | %.2f | %.2f | %llu |\n", double(rh), double(r / cell), stamp,
                        stride, double(cell * float(stride) / r), w > 0 ? std::to_string(w / r).substr(0, 6).c_str() : "n/a",
                        w > 0 ? std::to_string(int(std::lround(r / w))).c_str() : "n/a",
                        isLine ? "-" : std::to_string(wobble / r).substr(0, 6).c_str(),
                        isLine ? "-" : std::to_string(100.0 * median(overs)).substr(0, 5).c_str(), median(amps) / r, kernel, find, stitch,
                        (unsigned long long)points);
        }
    }
    std::printf("\nalpha texel = r / 256 (512 px over 2 r): the finest feature either alpha carries; a line FWHM of 0.0039 r is the alpha's own width.\n");
    return 0;
}
