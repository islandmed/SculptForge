// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
// Forge Ink on a Micro level (the core-200m head, in memory): edit ms per stroke vs the micro points it touches.
//   sf_ink_micro_bench [millions=200] [strokes=300] [edits=60] [width=0.02]
// width = a stroke's half-width in world units (the head is ~2 units tall). Prints p50/p95 edit ms, tiles and micro
// points per edit, ns per point, and the layer's memory.
#include "core/Benchmark.h"
#include "core/Ink.h"
#include "core/Micro.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <random>
#include <string>
#include <vector>

using namespace sf;
using Clock = std::chrono::steady_clock;

namespace {
double ms(Clock::time_point a) { return std::chrono::duration<double, std::milli>(Clock::now() - a).count(); }
double pct(std::vector<double> v, double f) {
    std::sort(v.begin(), v.end());
    return v.empty() ? 0.0 : v[size_t(f * double(v.size() - 1))];
}
size_t rssMB() {
    std::ifstream f("/proc/self/status");
    std::string line;
    while (std::getline(f, line))
        if (line.rfind("VmRSS:", 0) == 0) return size_t(std::atol(line.c_str() + 6)) / 1024;
    return 0;
}
} // namespace

int main(int argc, char** argv) {
    const int millions = argc > 1 ? std::atoi(argv[1]) : 200;
    const int nStrokes = argc > 2 ? std::atoi(argv[2]) : 300;
    const int nEdits = argc > 3 ? std::atoi(argv[3]) : 60;
    const float width = argc > 4 ? float(std::atof(argv[4])) : 0.02f;

    auto t0 = Clock::now();
    Mesh mesh = makeCore200mMesh(size_t(millions) * 1000000 / 1024, 5);
    if (!mesh.micro) { std::fprintf(stderr, "no micro level\n"); return 1; }
    const MicroMesh& mm = *mesh.micro;
    std::printf("ink-micro: %zuM micro triangles on %u base triangles (level %d), built in %.1f s, RSS %zu MB\n",
                mm.microTriangles() / 1000000, mm.baseTriangles, mm.level, ms(t0) / 1000.0, rssMB());
    const size_t rss0 = rssMB();

    t0 = Clock::now();
    auto surf = inkMicroSurface(mesh);
    InkLayer layer(*surf);
    const double gridMs = ms(t0);

    // Strokes along the surface: from a random base vertex, 6 points stepping along a tangent.
    std::mt19937 rng(17);
    std::uniform_real_distribution<float> U(0.0f, 1.0f);
    const auto stroke = [&]() {
        InkStroke s;
        const uint32_t v = uint32_t(U(rng) * float(mesh.positions.size())) % uint32_t(mesh.positions.size());
        const glm::vec3 n = glm::normalize(mesh.normals[v]);
        glm::vec3 t = glm::normalize(glm::cross(n, std::abs(n.x) < 0.9f ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0)));
        const float a = U(rng) * 6.283f;
        t = t * std::cos(a) + glm::cross(n, t) * std::sin(a);
        for (int k = 0; k < 6; ++k)
            s.path.push_back({mesh.positions[v] + t * (float(k) * width * 1.5f), width * (0.6f + 0.4f * U(rng)), width * 0.3f});
        return s;
    };
    std::vector<uint32_t> ids;
    t0 = Clock::now();
    for (int i = 0; i < nStrokes; ++i) ids.push_back(layer.add(stroke()));
    const double addMs = ms(t0);

    std::vector<double> editMs, nsPerPoint, tiles, points;
    for (int e = 0; e < nEdits; ++e) {
        InkStroke s = *layer.find(ids[size_t(U(rng) * float(ids.size())) % ids.size()]);
        const glm::vec3 d = (glm::vec3(U(rng), U(rng), U(rng)) - 0.5f) * (width * 2.0f);
        for (InkPoint& p : s.path) p.p += d, p.height *= 0.7f + 0.6f * U(rng);
        InkStats st;
        const auto t1 = Clock::now();
        layer.edit(s, &st);
        editMs.push_back(ms(t1));
        tiles.push_back(double(st.tiles)), points.push_back(double(st.samples));
        if (st.samples) nsPerPoint.push_back(editMs.back() * 1e6 / double(st.samples));
    }
    std::printf("ink-micro: grid %.0f ms, %d strokes added in %.0f ms (%.2f ms each), width %.3g\n", gridMs, nStrokes, addMs,
                addMs / nStrokes, double(width));
    std::printf("ink-micro: edit %.2f ms p50 / %.2f p95 / %.2f max | tiles %.0f p50 | micro points %.0f p50 (%.4f %% of %zu) | %.0f ns/point p50\n",
                pct(editMs, 0.5), pct(editMs, 0.95), pct(editMs, 1.0), pct(tiles, 0.5), pct(points, 0.5),
                100.0 * pct(points, 0.5) / double(size_t(mm.baseTriangles) * mm.samples), size_t(mm.baseTriangles) * mm.samples,
                pct(nsPerPoint, 0.5));
    std::printf("ink-micro: layer %.1f MB, RSS +%zu MB over the scene\n", double(layer.bytes()) / 1e6, rssMB() - rss0);
    return 0;
}
