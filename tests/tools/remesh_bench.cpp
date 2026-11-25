// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// sf_remesh_bench: times every remesh path at the UI defaults on spheres of a given size (tests/perf,
// docs/testing-flow.md).
//   sf_remesh_bench [--runs N] [--tris 1000,10000,...] [--paths qr2,legacy,voxel] [--bumpy] [--shape
//   sphere|cube|torus|horn|creased|starter:<id>]
//                   [--target Q] [--voxel-res R] [--cancel-after MS] [--noise A] [--holes N] [--detail KIND]
//                   [--symmetry x|y|z]
// --noise A: every vertex moved along its normal by up to A x the bounding-box diagonal (scan or pore-like grain);
// --holes N: N round holes punched through the surface (open boundaries, as a sculpt with deleted faces).
// --detail KIND: a whole-surface detail_pass (pores, skin, wrinkles, scales, scars), as a finished sculpt has.
// --no-crease-snap: Voxel Remesh without its crease pass (A/B).
// --shape creased: a sphere with V-creases under two voxels wide at Resolution 200 (tests/CreaseProbe.h, gh#240; --tris
// is ignored); each voxel row is followed by a CREASE line: serration and distances along the creases.
// --cancel-after MS: Cancel each run MS ms after it starts (from another thread, as the app's button does) and print
// the Cancel latency (gh#226: how long the remesh takes to notice and return). One CSV row per run on stdout: path,
// input triangles, run, total ms, the time between progress marks (stages), the output size and the Hausdorff / mean
// distance to the input (% of the bounding-box diagonal, quadQuality). SF_QREMESH_TIMING=1 adds Quad Remesh's own
// per-stage lines on stderr.
#include "core/Commands.h"
#include "core/Document.h"
#include "core/Primitives.h"
#include "core/QRemesh.h"
#include "core/StarterMeshes.h"
#include "core/Voxel.h"

#include "../CreaseProbe.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#if defined(__unix__) || defined(__APPLE__)
#include <sys/resource.h>
#endif

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

// Awkward inputs (gh#226): grain along the normals and holes (seeded, so every run gets the same mesh).
Mesh roughen(Mesh m, float noise, int holes) {
    const float diag = glm::length(m.bounds().extent());
    uint32_t state = 12345u;
    auto rnd = [&] {
        state = state * 1664525u + 1013904223u;
        return float(state >> 8) / float(1u << 24);
    };
    if (noise > 0.0f) {
        m.recomputeAllNormals();
        for (size_t v = 0; v < m.positions.size(); ++v) m.positions[v] += m.normals[v] * (noise * diag * (2.0f * rnd() - 1.0f));
    }
    if (holes > 0) {
        std::vector<glm::vec3> centers;
        for (int h = 0; h < holes; ++h) centers.push_back(m.positions[size_t(rnd() * float(m.positions.size() - 1))]);
        const float r = 0.03f * diag;
        std::vector<glm::uvec3> kept;
        for (const glm::uvec3& t : m.triangles) {
            bool in = false;
            for (const glm::vec3& c : centers) in |= glm::length(m.positions[t.x] - c) < r;
            if (!in) kept.push_back(t);
        }
        Mesh cut; // positions and triangles only: the per-triangle attributes don't survive the cut
        cut.positions = std::move(m.positions);
        cut.triangles = std::move(kept);
        m = std::move(cut);
    }
    m.rebuildTopology();
    m.recomputeAllNormals();
    return m;
}

Mesh withDetail(const Mesh& m, const std::string& kind) {
    CommandRegistry reg;
    Document doc;
    doc.setMesh(m);
    const CommandResult r = reg.execute(doc, "detail_pass", {{"kind", kind}});
    if (!r.ok) std::fprintf(stderr, "detail_pass %s: %s\n", kind.c_str(), r.message.c_str());
    return doc.mesh;
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

// Peak resident memory of this process so far, in MB (the high-water mark: run one size per process for a
// per-size number). 0 where the platform has no getrusage.
double peakRssMb() {
#if defined(__unix__) || defined(__APPLE__)
    rusage ru{};
    if (getrusage(RUSAGE_SELF, &ru) == 0) {
#if defined(__APPLE__)
        return double(ru.ru_maxrss) / (1024.0 * 1024.0);
#else
        return double(ru.ru_maxrss) / 1024.0;
#endif
    }
#endif
    return 0.0;
}

// Poles (interior vertices of valence != 4) on the symmetry plane and within 3 mean edge lengths of it: with Symmetry
// the half is remeshed with the cut as a feature line and mirrored, and a weak seam shows up as poles along it.
size_t seamPoles(const Mesh& mesh, int axis, size_t* near) {
    if (near) *near = 0;
    if (axis < 0) return 0;
    std::vector<int> valence(mesh.vertexCount(), 0);
    std::vector<uint8_t> open(mesh.vertexCount(), 0);
    std::unordered_map<uint64_t, int> edges;
    double lsum = 0.0;
    for (uint32_t t = 0; t < mesh.triangleCount(); ++t) {
        glm::uvec4 f;
        const int n = mesh.exportFace(t, f);
        if (n < 3) continue;
        for (int k = 0; k < n; ++k) {
            const uint32_t a = f[k], b = f[(k + 1) % n];
            ++edges[a < b ? (uint64_t(a) << 32) | b : (uint64_t(b) << 32) | a];
        }
    }
    for (auto [key, uses] : edges) {
        const uint32_t a = uint32_t(key >> 32), b = uint32_t(key & 0xffffffffu);
        ++valence[a], ++valence[b];
        if (uses == 1) open[a] = open[b] = 1;
        lsum += double(glm::length(mesh.positions[a] - mesh.positions[b]));
    }
    const float meanEdge = edges.empty() ? 0.0f : float(lsum / double(edges.size()));
    if (std::getenv("SF_QREMESH_TIMING")) { // the valences on the plane (a seam vertex has 2 quads a side when all is well: 4)
        std::vector<size_t> hist(10, 0);
        for (uint32_t v = 0; v < mesh.vertexCount(); ++v)
            if (valence[v] > 0 && !open[v] && std::abs(mesh.positions[v][axis]) < 1e-3f * meanEdge) ++hist[size_t(std::min(valence[v], 9))];
        std::fprintf(stderr, "  seam valences:");
        for (size_t k = 2; k < hist.size(); ++k)
            if (hist[k]) std::fprintf(stderr, " %zu x%zu", k, hist[k]);
        std::fprintf(stderr, "\n");
    }
    size_t on = 0;
    for (uint32_t v = 0; v < mesh.vertexCount(); ++v) {
        if (valence[v] == 0 || open[v] || valence[v] == 4) continue;
        const float d = std::abs(mesh.positions[v][axis]);
        if (d < 1e-3f * meanEdge) ++on;
        else if (near && d < 3.0f * meanEdge) ++*near;
    }
    return on;
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
    int runs = 5, target = 5000, voxelRes = 200, cancelAfter = -1, holes = 0;
    float noise = 0.0f, adaptive = -1.0f; // (adaptive < 0: the option's default)
    std::string detail;
    int symmetry = -1;
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
        else if (a == "--no-crease-snap") setVoxelCreaseSnap(false);
        else if (a == "--shape" && more) shape = argv[++i];
        else if (a == "--cancel-after" && more) cancelAfter = std::atoi(argv[++i]);
        else if (a == "--noise" && more) noise = float(std::atof(argv[++i]));
        else if (a == "--adaptive" && more) adaptive = float(std::atof(argv[++i]));
        else if (a == "--holes" && more) holes = std::atoi(argv[++i]);
        else if (a == "--detail" && more)
            detail = argv[++i];
        else if (a == "--symmetry" && more)
            symmetry = argv[++i][0] - 'x'; // x, y or z (the app's Symmetry)
        else {
            std::fprintf(stderr,
                         "usage: sf_remesh_bench [--runs N] [--tris a,b,c] [--paths qr2,legacy,voxel] [--bumpy] "
                         "[--shape sphere|cube|torus|horn|creased|starter:<id>] [--target Q] [--voxel-res R] [--cancel-after "
                         "MS] [--noise A] [--holes N] [--detail KIND] [--symmetry x|y|z]\n");
            return 2;
        }
    }
    // A compact summary at the end (median, min, max per path and size), for logs that keep only the tail.
    std::vector<std::string> summary;
    std::printf("path,tris,run,total_ms,stages,out_tris,hausdorff_pct,mean_pct,poles,angle_dev,angle_p99,edge_cv,hash,max_gap_ms,seam_poles,near_seam_poles\n");
    const bool creased = shape == "creased";
    const crease::Fixture creases = creased ? crease::makeCreasedSphere() : crease::Fixture{};
    if (creased) sizes = {long(creases.mesh.triangleCount())};
    for (long n : sizes) {
        Mesh src = creased ? creases.mesh : noise > 0.0f || holes > 0 ? roughen(sphere(size_t(n), bumpy, shape), noise, holes)
                                             : sphere(size_t(n), bumpy, shape);
        if (!creased && !detail.empty()) src = withDetail(src, detail);
        if (const char* path =
                std::getenv("SF_BENCH_SOURCE")) { // SF_BENCH_SOURCE=<file>: the input as OBJ (to open it in the app)
            if (std::FILE* fo = std::fopen(path, "w")) {
                for (const glm::vec3& q : src.positions)
                    std::fprintf(fo, "v %g %g %g\n", q.x, q.y, q.z);
                for (const glm::uvec3& q : src.triangles)
                    std::fprintf(fo, "f %u %u %u\n", q.x + 1, q.y + 1, q.z + 1);
                std::fclose(fo);
            }
        }
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
                std::atomic<bool> cancel{false};
                Clock::time_point cancelAt{};
                std::thread canceller;
                if (cancelAfter >= 0)
                    canceller = std::thread([&] {
                        std::this_thread::sleep_for(std::chrono::milliseconds(cancelAfter));
                        cancelAt = Clock::now();
                        cancel = true;
                    });
                // The longest silence between two progress calls is the Cancel latency bound (the job notices a Cancel at
                // its next call). Marks at every 0.05 of progress keep the stage columns short now that a stage reports
                // its way through.
                auto lastCall = t0;
                double maxGap = 0.0;
                auto mark = [&](float f) {
                    if (cancel) return false;
                    const auto now = Clock::now();
                    maxGap = std::max(maxGap, ms(lastCall, now));
                    lastCall = now;
                    if (f != lastF && (std::floor(f * 20.0f + 1e-4f) != std::floor(lastF * 20.0f + 1e-4f) || f >= 1.0f)) {
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
                    if (adaptive >= 0.0f) o.adaptive = adaptive;
                    o.symmetryAxis = symmetry >= 0 && symmetry <= 2 ? symmetry : -1;
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
                if (canceller.joinable()) {
                    canceller.join();
                    std::printf("CANCEL %s %zu: asked at %.0f ms, returned %.0f ms later (%s); longest silence %.0f ms\n", path,
                                src.triangleCount(), ms(t0, cancelAt), total - ms(t0, cancelAt), ok ? "finished anyway" : error.c_str(),
                                maxGap);
                    std::fflush(stdout);
                    continue;
                }
                if (!ok) {
                    std::fprintf(stderr, "%s %ld: %s\n", path, n, error.c_str());
                    continue;
                }
                if (!q.measured) q = quadQuality(out, &src); // (voxel: measured here, outside its time)
                size_t nearSeam = 0;
                const size_t onSeam = std::string(path) == "voxel" ? 0 : seamPoles(out, symmetry, &nearSeam);
                std::printf("%s,%zu,%d,%.1f,%s,%zu,%.4f,%.4f,%zu,%.2f,%.2f,%.3f,%016llx,%.1f,%zu,%zu\n", path, src.triangleCount(), run, total,
                            stages.c_str(), out.triangleCount(), q.hausdorff, q.meanDistance, q.poles, q.angleDeviation,
                            q.angleDeviationMax, q.edgeLengthCv, (unsigned long long)meshHash(out), maxGap, onSeam, nearSeam);
                if (std::getenv("SF_BENCH_SELFX")) // SF_BENCH_SELFX=1: the result's self-intersections (tests/CreaseProbe.h)
                    std::printf("SELFX %s %zu: %zu self-intersections\n", path, src.triangleCount(), crease::selfIntersections(out));
                if (creased && std::string(path) == "voxel") {
                    const glm::vec3 ext = src.bounds().extent();
                    const crease::Measure cm = crease::measure(creases, out, std::max({ext.x, ext.y, ext.z}) / float(voxelRes));
                    std::printf("CREASE res %d: serration %.3f deg, normal error %.3f deg, Hausdorff %.3f, mean %.4f, bottom gap "
                                "%.3f voxels (%zu samples), wiggle %.3f voxels, depth cv %.3f, profile error %.3f voxels, %zu self-intersections\n",
                                voxelRes, cm.serrationDeg, cm.normalErrDeg, cm.hausdorff, cm.mean, cm.bottomGap, cm.samples,
                                cm.wiggleVox, cm.depthCv, cm.profileErr,
                                crease::selfIntersections(out));
                    if (const char* img = std::getenv("SF_CREASE_IMAGE")) { // SF_CREASE_IMAGE=<prefix>: close-ups, source and result
                        for (size_t g : {size_t(1), size_t(4)}) {
                            const std::string pre = std::string(img) + "-g" + std::to_string(g);
                            crease::renderPatch(creases, src, g, 0.6f, 0.25f, (pre + "-source.ppm").c_str());
                            crease::renderPatch(creases, out, g, 0.6f, 0.25f, (pre + "-result.ppm").c_str());
                        }
                    }
                }
                std::fflush(stdout);
                totals.push_back(total);
                lastQuality = q;
                std::fprintf(stderr, "PEAK_RSS %s %zu %d %.0f MB\n", path, src.triangleCount(), run, peakRssMb());
            }
            if (totals.empty()) continue;
            std::sort(totals.begin(), totals.end());
            char b[160];
            std::snprintf(b, sizeof b, "SUMMARY %s %zu: median %.0f ms (%.0f..%.0f), Hausdorff %.4f%%, poles %zu, peak RSS %.0f MB",
                          path, src.triangleCount(), totals[totals.size() / 2], totals.front(), totals.back(),
                          lastQuality.hausdorff, lastQuality.poles, peakRssMb());
            summary.push_back(b);
        }
    }
    for (const std::string& line : summary) std::printf("%s\n", line.c_str());
    return 0;
}
