// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// QRemesh 2 (core/QRemesh2.cpp, the default method): poles only where the cross field needs them (a sphere gets
// exactly eight), all quads at the target density, deterministic, exactly mirrored with symmetry, hard edges kept,
// the quality report, and the legacy method still there (method=legacy).
//
// SF_QREMESH_BENCH=<dir> runs the comparison against the legacy method (lion scan, the ICT head, a creature
// sculpted from a sphere, a hard-surface part; 2k / 5k / 10k quads), prints a Markdown table and writes each
// result as <dir>/<model>_<method>_<target>.obj for rendering. SF_BENCH_LIMITS=1 times QRemesh 2 on dense sculpts
// (1.3M, 5.2M and 21M triangles; SF_BENCH_SIZES=1,5,21 picks) with the peak memory of each run.
#include "TestHarness.h"

#include "core/Commands.h"
#include "core/Primitives.h"
#include "core/QRemesh.h"
#include "core/SurfaceQuery.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>

using namespace sf;

namespace {

float diag(const Mesh& m) { return glm::length(m.bounds().extent()); }

Mesh box(glm::vec3 size, glm::vec3 center, int resolution) {
    Mesh m = makeCube(resolution, 2.0f);
    for (glm::vec3& p : m.positions) p = center + p * 0.5f * size;
    m.recomputeAllNormals();
    return m;
}

Mesh combine(const std::vector<Mesh>& parts) {
    Mesh m;
    for (const Mesh& p : parts) {
        const uint32_t base = uint32_t(m.positions.size());
        m.positions.insert(m.positions.end(), p.positions.begin(), p.positions.end());
        for (const glm::uvec3& t : p.triangles) m.triangles.push_back(t + glm::uvec3(base));
    }
    m.rebuildTopology();
    m.recomputeAllNormals();
    return m;
}

void stroke(CommandRegistry& reg, Document& doc, const char* points, const char* brush, double radius, bool invert = false) {
    CHECK(reg.execute(doc, "stroke", {{"points", std::string(points)}, {"brush", std::string(brush)}, {"radius", radius},
                                      {"strength", 1.0}, {"invert", invert}})
              .ok);
}

// A sphere with ridges, a crease and a dent (as test_retopo.cpp), optionally divided.
Mesh sculptedSphere(int subdivide = 0) {
    CommandRegistry reg;
    Document doc;
    reg.execute(doc, "new_sphere", {{"subdivisions", 6.0}});
    stroke(reg, doc, "-0.6,0.3,2 0,0.5,2 0.6,0.3,2", "Standard", 0.18);
    stroke(reg, doc, "-0.5,-0.3,2 0.5,-0.4,2", "Crease", 0.12);
    stroke(reg, doc, "0.1,0,2 0.2,0.1,2", "Clay", 0.25, true);
    if (subdivide > 0) reg.execute(doc, "subdivide", {{"levels", double(subdivide)}});
    return doc.mesh;
}

// A creature blocked out from a sphere: a snout, brow ridges, horns, cheeks and a ridge down the back.
Mesh creature() {
    CommandRegistry reg;
    Document doc;
    reg.execute(doc, "new_sphere", {{"subdivisions", 6.0}});
    stroke(reg, doc, "0,-0.1,2 0,-0.15,2", "Inflate", 0.45);
    stroke(reg, doc, "-0.45,0.35,2 -0.1,0.45,2", "Standard", 0.14);
    stroke(reg, doc, "0.1,0.45,2 0.45,0.35,2", "Standard", 0.14);
    stroke(reg, doc, "-0.5,0.7,2 -0.6,0.95,2", "Standard", 0.12);
    stroke(reg, doc, "0.5,0.7,2 0.6,0.95,2", "Standard", 0.12);
    stroke(reg, doc, "-0.55,-0.15,2 -0.45,-0.25,2", "Clay", 0.2);
    stroke(reg, doc, "0.55,-0.15,2 0.45,-0.25,2", "Clay", 0.2);
    stroke(reg, doc, "-0.3,-0.45,2 0.3,-0.45,2", "Crease", 0.1);
    stroke(reg, doc, "0,0.9,-2 0,0.2,-2 0,-0.6,-2", "Standard", 0.16);
    return doc.mesh;
}

// A hard-surface part: a block with a thin fin and a boss (separate shells, as kitbashed).
Mesh hardSurface() {
    return combine({box({2.0f, 1.0f, 1.2f}, {0, 0, 0}, 24), box({1.4f, 0.6f, 0.08f}, {0, 0.8f, 0}, 32),
                    box({0.5f, 0.3f, 0.5f}, {0.6f, 0.0f, 0.75f}, 12)});
}

struct Run {
    Mesh out;
    QRemeshStats st;
    bool ok = false;
};

Run run(const Mesh& m, int target, QRemeshMethod method, int symmetry = -1) {
    QRemeshOptions o;
    o.method = method;
    o.targetQuads = target;
    o.symmetryAxis = symmetry;
    Run r;
    std::string err;
    r.ok = qremesh(m, o, r.out, err, {}, &r.st);
    if (!r.ok) std::printf("  QRemesh failed: %s\n", err.c_str());
    return r;
}

void writeObj(const Mesh& m, const std::string& path) {
    std::ofstream f(path);
    for (const glm::vec3& p : m.positions) f << "v " << p.x << ' ' << p.y << ' ' << p.z << '\n';
    for (uint32_t t = 0; t < m.triangleCount(); ++t) {
        glm::uvec4 fc;
        const int k = m.exportFace(t, fc);
        if (k == 4) f << "f " << fc.x + 1 << ' ' << fc.y + 1 << ' ' << fc.z + 1 << ' ' << fc.w + 1 << '\n';
        else if (k == 3) f << "f " << fc.x + 1 << ' ' << fc.y + 1 << ' ' << fc.z + 1 << '\n';
    }
}

// Peak resident memory since the last reset (Linux; 0 elsewhere).
size_t peakRssMb() {
    std::ifstream s("/proc/self/status");
    std::string line;
    while (std::getline(s, line))
        if (line.rfind("VmHWM:", 0) == 0) return size_t(std::atol(line.c_str() + 6)) / 1024;
    return 0;
}
void resetPeakRss() {
    std::ofstream c("/proc/self/clear_refs");
    c << "5";
}

} // namespace

TEST(qremesh2_sphere_has_eight_poles) {
    const Mesh sphere = makeIcosphere(5);
    const Run r = run(sphere, 2000, QRemeshMethod::V2);
    CHECK(r.ok);
    const TopologyReport t = topologyReport(r.out);
    std::printf("  sphere: %zu quads, %zu poles (%zu x3), corners %.1f deg, Hausdorff %.3f%%\n", r.st.quads, r.st.quality.poles,
                r.st.quality.valence[3], r.st.quality.angleDeviation, r.st.quality.hausdorff);
    CHECK(t.manifold() && t.boundaryEdges == 0 && t.triangles == 0);
    CHECK(std::abs(double(r.st.quads) / 2000.0 - 1.0) < 0.1);
    CHECK(r.st.quality.poles == 8 && r.st.quality.valence[3] == 8); // a cube's layout: eight valence-3 poles
    CHECK(r.st.quality.angleDeviation < 8.0f);
    CHECK(r.st.quality.measured && r.st.quality.hausdorff < 0.5f);
    CHECK(r.st.fallbackPieces == 0);
    // The legacy method on the same sphere: many more poles.
    const Run l = run(sphere, 2000, QRemeshMethod::Legacy);
    CHECK(l.ok);
    std::printf("  legacy: %zu poles, corners %.1f deg\n", l.st.quality.poles, l.st.quality.angleDeviation);
    CHECK(l.st.quality.poles > 4 * r.st.quality.poles);
}

TEST(qremesh2_is_deterministic) {
    const Mesh m = creature();
    const Run a = run(m, 3000, QRemeshMethod::V2), b = run(m, 3000, QRemeshMethod::V2);
    CHECK(a.ok && b.ok);
    CHECK(a.out.positions == b.out.positions && a.out.triangles == b.out.triangles);
}

TEST(qremesh2_sculpt_beats_legacy) {
    const Mesh m = sculptedSphere();
    const Run v2 = run(m, 4000, QRemeshMethod::V2), legacy = run(m, 4000, QRemeshMethod::Legacy);
    CHECK(v2.ok && legacy.ok);
    const QuadQuality &a = v2.st.quality, &b = legacy.st.quality;
    std::printf("  sculpt 4000: v2 %zu poles, %.1f deg, mean %.3f%% | legacy %zu poles, %.1f deg, mean %.3f%%\n", a.poles,
                a.angleDeviation, a.meanDistance, b.poles, b.angleDeviation, b.meanDistance);
    CHECK(topologyReport(v2.out).manifold());
    CHECK(a.poles * 3 < b.poles);
    CHECK(a.angleDeviation < b.angleDeviation + 1.0f); // about as square, with a twentieth of the poles
    CHECK(a.meanDistance <= b.meanDistance * 1.1f);
}

TEST(qremesh2_symmetry_is_exact) {
    const Mesh m = creature(); // symmetric about x = 0 (the strokes are mirrored)
    const Run r = run(m, 3000, QRemeshMethod::V2, 0);
    CHECK(r.ok && r.st.symmetryUsed);
    const TopologyReport t = topologyReport(r.out);
    CHECK(t.manifold() && t.boundaryEdges == 0);
    // Every vertex has its mirror image among the vertices.
    TriangleBVH points;
    std::vector<glm::uvec3> tri(r.out.vertexCount());
    for (uint32_t v = 0; v < r.out.vertexCount(); ++v) tri[v] = {v, v, v};
    points.build(r.out.positions, tri);
    float worst = 0.0f;
    for (const glm::vec3& p : r.out.positions) {
        SurfaceHit hit;
        points.closestPoint({-p.x, p.y, p.z}, 1.0f, hit);
        worst = std::max(worst, hit.distance);
    }
    std::printf("  symmetric creature: %zu quads, worst mirror mismatch %.2e of the size\n", r.st.quads, double(worst / diag(m)));
    CHECK(worst < 1e-4f * diag(m));
}

TEST(qremesh2_hard_edges_and_groups) {
    const Mesh m = hardSurface();
    const Run r = run(m, 3000, QRemeshMethod::V2);
    CHECK(r.ok);
    std::printf("  hard surface: %zu quads, %zu poles, Hausdorff %.3f%%, %zu legacy pieces\n", r.st.quads, r.st.quality.poles,
                r.st.quality.hausdorff, r.st.fallbackPieces);
    CHECK(topologyReport(r.out).manifold());
    CHECK(r.st.quality.hausdorff < 1.0f); // the corners and the fin stay sharp
    // Keep Groups on a cube: the result keeps the six groups, each quad in one.
    CommandRegistry reg;
    Document doc;
    doc.setMesh(makeCube(24));
    CHECK(reg.execute(doc, "polygroup_auto_by_normals", {{"angle", 40.0}}).ok);
    QRemeshOptions o;
    o.targetQuads = 1200;
    o.keepGroups = true;
    o.detectEdges = false;
    Mesh out;
    std::string err;
    QRemeshStats st;
    CHECK(qremesh(doc.mesh, o, out, err, {}, &st));
    CHECK(out.hasPolygroups());
    for (size_t t = 0; t + 1 < out.triangleCount(); t += 2) CHECK(out.polygroups[t] == out.polygroups[t + 1]);
    CHECK(st.quality.poles == 8); // a cube: one valence-3 pole per corner
}

TEST(qremesh2_command_method_and_report) {
    CommandRegistry reg;
    Document doc;
    reg.execute(doc, "new_sphere", {{"subdivisions", 4.0}});
    CommandResult r = reg.execute(doc, "qremesh", {{"target", 800.0}});
    CHECK(r.ok);
    std::printf("  %s\n", r.message.c_str());
    CHECK(r.message.find("Quality: 8 poles") != std::string::npos);
    CHECK(reg.execute(doc, "undo", {}).ok);
    r = reg.execute(doc, "qremesh", {{"target", 800.0}, {"method", std::string("legacy")}});
    CHECK(r.ok && r.message.find("legacy method") != std::string::npos);
    CHECK(!reg.execute(doc, "qremesh", {{"method", std::string("zremesher")}}).ok);
    r = reg.execute(doc, "qremesh", {{"target", 500.0}, {"quality", false}});
    CHECK(r.ok && r.message.find("Quality") == std::string::npos);
}

TEST(qremesh2_benchmark) {
    const char* dir = std::getenv("SF_QREMESH_BENCH");
    if (!dir) return;
    CommandRegistry reg;
    struct Model {
        std::string name;
        Mesh mesh;
        int symmetry;
    };
    std::vector<Model> models;
    {
        Document doc;
        std::string lion = __FILE__;
        lion = lion.substr(0, lion.rfind("tests")) + "assets/demo/lion-head.glb";
        if (reg.execute(doc, "open", {{"path", lion}, {"subdivisions", 0.0}}).ok) models.push_back({"lion", doc.mesh, -1});
    }
    {
        Document doc;
        if (reg.execute(doc, "new_head", {}).ok) models.push_back({"head", doc.subtoolMesh(0), 0});
    }
    models.push_back({"creature", creature(), 0});
    models.push_back({"hardsurface", hardSurface(), -1});
    models.push_back({"sculpt1m", sculptedSphere(2), -1});
    std::printf("\n| model | triangles | target | method | quads | time s | poles | x3/x5/other | corner dev deg | mean dist %% | Hausdorff %% |\n");
    std::printf("|---|---|---|---|---|---|---|---|---|---|---|\n");
    const char* only = std::getenv("SF_QREMESH_BENCH_MODELS"); // e.g. "lion,head"
    for (const Model& md : models)
        for (int target : {2000, 5000, 10000})
            for (QRemeshMethod method : {QRemeshMethod::Legacy, QRemeshMethod::V2}) {
                if (only && std::string(only).find(md.name) == std::string::npos) continue;
                if (method == QRemeshMethod::Legacy && std::getenv("SF_QREMESH_BENCH_V2_ONLY")) continue;
                const Run r = run(md.mesh, target, method, md.symmetry);
                if (!r.ok) continue;
                const QuadQuality& q = r.st.quality;
                const char* mname = method == QRemeshMethod::V2 ? "v2" : "legacy";
                std::printf("| %s | %zu | %d | %s | %zu | %.2f | %zu | %zu/%zu/%zu | %.1f | %.3f | %.2f |\n", md.name.c_str(),
                            md.mesh.triangleCount(), target, mname, r.st.quads + r.st.triangles, r.st.seconds, q.poles,
                            q.valence[3], q.valence[5], q.poles - q.valence[3] - q.valence[5], q.angleDeviation,
                            q.meanDistance, q.hausdorff);
                std::fflush(stdout);
                writeObj(r.out, std::string(dir) + "/" + md.name + "_" + mname + "_" + std::to_string(target) + ".obj");
            }
}

TEST(qremesh2_large_inputs) {
    if (!std::getenv("SF_BENCH_LIMITS")) return;
    std::vector<int> sizes{1, 5, 21};
    if (const char* s = std::getenv("SF_BENCH_SIZES")) {
        sizes.clear();
        std::stringstream ss(s);
        std::string item;
        while (std::getline(ss, item, ',')) sizes.push_back(std::atoi(item.c_str()));
    }
    std::printf("\n| input triangles | target | quads | time s | peak RSS MB (QRemesh) | poles | corner dev deg | mean dist %% | Hausdorff %% |\n");
    std::printf("|---|---|---|---|---|---|---|---|---|\n");
    for (int millions : sizes) {
        const int levels = millions >= 20 ? 4 : millions >= 5 ? 3 : 2; // 1.3M, 5.2M, 21M
        Mesh m = sculptedSphere(levels);
        m.clearQuads();
        for (int target : {2000, 5000, 10000}) {
            resetPeakRss();
            const size_t base = peakRssMb();
            const auto t0 = std::chrono::steady_clock::now();
            const Run r = run(m, target, QRemeshMethod::V2);
            const double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            CHECK(r.ok);
            CHECK(topologyReport(r.out).manifold());
            const QuadQuality& q = r.st.quality;
            std::printf("| %zu | %d | %zu | %.1f (%.1f + report) | %zu (+%zu over the sculpt) | %zu | %.1f | %.3f | %.2f |\n",
                        m.triangleCount(), target, r.st.quads, s, r.st.seconds, peakRssMb(), peakRssMb() - base, q.poles,
                        q.angleDeviation, q.meanDistance, q.hausdorff);
            std::fflush(stdout);
            CHECK(r.st.seconds < (m.triangleCount() > 10'000'000 ? 120.0 : 30.0));
        }
    }
}
