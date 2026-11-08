// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// sf_remesh_bench: times every remesh path at the UI defaults on spheres of a given size (tests/perf, docs/testing-flow.md).
//   sf_remesh_bench [--runs N] [--tris 1000,10000,...] [--paths qr2,legacy,voxel] [--bumpy] [--shape sphere|cube|torus|horn|starter:<id>]
//                   [--target Q] [--voxel-res R]
// One CSV row per run on stdout: path, input triangles, run, total ms, the time between progress marks (stages), the
// output size and the Hausdorff / mean distance to the input (% of the bounding-box diagonal, quadQuality).
// SF_QREMESH_TIMING=1 adds Quad Remesh's own per-stage lines on stderr.
#include "core/Document.h"
#include "core/Primitives.h"
#include "core/QRemesh.h"
#include "core/StarterMeshes.h"
#include "core/Voxel.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <string>
#include <vector>

using namespace sf;

namespace {

using Clock = std::chrono::steady_clock;

double ms(Clock::time_point a, Clock::time_point b) { return std::chrono::duration<double, std::milli>(b - a).count(); }

// A quad sphere with about `tris` triangles (6 r^2 quads); bumpy: a few octaves of sine ridges, like a rough sculpt.
// cube: hard edges and corners (feature lines); torus: genus one; horn: long, flat and thin-tipped; starter:<id>: a starter mesh's biggest part (real
// sculpt forms: bust, heads, hand), divided until it has about `tris` triangles.
Mesh sphere(size_t tris, bool bumpy, const std::string& shape) {
    if (shape.rfind("starter:", 0) == 0) {
        const StarterMesh* starter = findStarterMesh(shape.substr(8));
        Mesh best;
        for (int levels = 0; starter && levels <= 5; ++levels) {
            Document d;
            std::string error;
            if (!loadStarterMesh(*starter, d, levels, error)) break;
            best = Mesh{};
            for (int i = 0; i < d.partCount(); ++i)
                if (d.partMesh(i).triangleCount() > best.triangleCount()) best = d.partMesh(i);
            if (double(best.triangleCount()) >= 0.7 * double(tris)) break;
        }
        return best;
    }
    const int r = std::max(2, int(std::lround(std::sqrt(double(tris) / 12.0))));
    if (shape == "cube") return makeCube(r, 2.0f);
    if (shape == "torus") {
        Mesh m = makeTorus(std::max(8, int(std::lround(std::sqrt(double(tris) / 2.0)))), 2.0f);
        while (m.triangleCount() * 4 <= tris) m = loopSubdivide(m);
        return m;
    }
    Mesh m = makeQuadSphere(r, 2.0f);
    if (shape == "horn") { // a thin curved horn, 2455 x 691 x 149 like Oslen's Part (Discord): tapering to a ~4 mm tip
        for (glm::vec3& p : m.positions) {
            const glm::vec3 n = glm::normalize(p);
            const float t = 0.5f * (n.y + 1.0f), taper = 1.0f - 0.95f * t;
            p = {600.0f * t * t + 345.0f * n.x * taper, 2455.0f * t, 75.0f * n.z * taper};
        }
        m.recomputeAllNormals();
        return m;
    }
    if (bumpy) {
        for (glm::vec3& p : m.positions) {
            const glm::vec3 n = glm::normalize(p);
            const float d = 0.06f * std::sin(7.0f * n.x) * std::sin(5.0f * n.y) + 0.02f * std::sin(23.0f * n.z + 3.0f * n.x) +
                            0.008f * std::sin(61.0f * n.y) * std::sin(47.0f * n.x);
            p = n * (1.0f + d);
        }
        m.recomputeAllNormals();
    }
    return m;
}

// FNV-1a over the result's positions and triangles: a speed change that must not change the result keeps it.
uint64_t meshHash(const Mesh& m) {
    uint64_t h = 1469598103934665603ull;
    auto add = [&](const void* p, size_t n) {
        for (size_t i = 0; i < n; ++i) h = (h ^ static_cast<const unsigned char*>(p)[i]) * 1099511628211ull;
    };
    add(m.positions.data(), m.positions.size() * sizeof(glm::vec3));
    add(m.triangles.data(), m.triangles.size() * sizeof(glm::uvec3));
    return h;
}

std::vector<long> parseList(const char* s) {
    std::vector<long> v;
    std::stringstream ss(s);
    std::string item;
    while (std::getline(ss, item, ',')) v.push_back(std::atol(item.c_str()));
    return v;
}

} // namespace

int main(int argc, char** argv) {
    int runs = 5, target = 5000, voxelRes = 200;
    bool bumpy = false;
    std::vector<long> sizes = {1000, 10000, 100000, 1000000};
    std::string paths = "qr2,legacy,voxel", shape = "sphere";
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        const bool more = i + 1 < argc;
        if (a == "--runs" && more) runs = std::atoi(argv[++i]);
        else if (a == "--tris" && more) sizes = parseList(argv[++i]);
        else if (a == "--paths" && more) paths = argv[++i];
        else if (a == "--target" && more) target = std::atoi(argv[++i]);
        else if (a == "--voxel-res" && more) voxelRes = std::atoi(argv[++i]);
        else if (a == "--bumpy") bumpy = true;
        else if (a == "--shape" && more) shape = argv[++i];
        else {
            std::fprintf(stderr, "usage: sf_remesh_bench [--runs N] [--tris a,b,c] [--paths qr2,legacy,voxel] [--bumpy] "
                                 "[--shape sphere|cube|torus|horn|starter:<id>] [--target Q] [--voxel-res R]\n");
            return 2;
        }
    }
    // A compact summary at the end (median, min, max per path and size), for logs that keep only the tail.
    std::vector<std::string> summary;
    std::printf("path,tris,run,total_ms,stages,out_tris,hausdorff_pct,mean_pct,poles,angle_dev,angle_p99,edge_cv,hash\n");
    for (long n : sizes) {
        const Mesh src = sphere(size_t(n), bumpy, shape);
        for (const char* path : {"qr2", "legacy", "voxel"}) {
            if (("," + paths + ",").find("," + std::string(path) + ",") == std::string::npos) continue;
            std::vector<double> totals;
            QuadQuality lastQuality;
            for (int run = 0; run < runs; ++run) {
                std::fprintf(stderr, "RUN %s %zu %d\n", path, src.triangleCount(), run);
                // Stage times: the gaps between progress marks (fraction@ms).
                std::string stages;
                const auto t0 = Clock::now();
                auto last = t0;
                float lastF = 0.0f;
                auto mark = [&](float f) {
                    const auto now = Clock::now();
                    if (f != lastF) {
                        char b[48];
                        std::snprintf(b, sizeof b, "%s%.2f:%.1f", stages.empty() ? "" : " ", f, ms(last, now));
                        stages += b;
                        last = now, lastF = f;
                    }
                    return true;
                };
                Mesh out;
                std::string error;
                QuadQuality q;
                bool ok;
                if (std::string(path) == "voxel") {
                    out = voxelRemesh(src, voxelRes, &error, mark);
                    ok = out.triangleCount() > 0;
                } else {
                    QRemeshOptions o; // the UI defaults (App::startQRemesh with retopo_ untouched)
                    o.method = std::string(path) == "legacy" ? QRemeshMethod::Legacy : QRemeshMethod::V2;
                    o.targetQuads = target;
                    QRemeshStats st;
                    ok = qremesh(src, o, out, error, mark, &st);
                    if (const char* dump = std::getenv("SF_BENCH_DUMP")) { // SF_BENCH_DUMP=<file>: the (last) result as OBJ
                        if (std::FILE* fo = std::fopen(dump, "w")) {
                            for (const glm::vec3& q : out.positions) std::fprintf(fo, "v %g %g %g\n", q.x, q.y, q.z);
                            for (const glm::uvec3& q : out.triangles) std::fprintf(fo, "f %u %u %u\n", q.x + 1, q.y + 1, q.z + 1);
                            std::fclose(fo);
                        }
                    }
                    q = st.quality;
                }
                const double total = ms(t0, Clock::now());
                if (!ok) {
                    std::fprintf(stderr, "%s %ld: %s\n", path, n, error.c_str());
                    continue;
                }
                if (!q.measured) q = quadQuality(out, &src); // (voxel: measured here, outside its time)
                std::printf("%s,%zu,%d,%.1f,%s,%zu,%.4f,%.4f,%zu,%.2f,%.2f,%.3f,%016llx\n", path, src.triangleCount(), run, total,
                            stages.c_str(), out.triangleCount(), q.hausdorff, q.meanDistance, q.poles, q.angleDeviation,
                            q.angleDeviationMax, q.edgeLengthCv, (unsigned long long)meshHash(out));
                std::fflush(stdout);
                totals.push_back(total);
                lastQuality = q;
            }
            if (totals.empty()) continue;
            std::sort(totals.begin(), totals.end());
            char b[160];
            std::snprintf(b, sizeof b, "SUMMARY %s %zu: median %.0f ms (%.0f..%.0f), Hausdorff %.4f%%, poles %zu", path,
                          src.triangleCount(), totals[totals.size() / 2], totals.front(), totals.back(),
                          lastQuality.hausdorff, lastQuality.poles);
            summary.push_back(b);
        }
    }
    for (const std::string& line : summary) std::printf("%s\n", line.c_str());
    return 0;
}
