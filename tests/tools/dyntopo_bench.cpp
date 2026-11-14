// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// sf_dyntopo_bench: one Clay Block stroke with dynamic topology on a dense Part, as a first stroke in a session
// makes it (tests/test_dyntopo.cpp holds the invariants this tool measures against).
//   sf_dyntopo_bench [--tris N] [--radius R] [--detail D] [--dabs N] [--sym] [--big S] [--log]
// --tris: triangles of the Part (a quad sphere, unit radius: few = large triangles under a small brush, which the first
// dabs must refine; many = fine ones under a big brush, which they coarsen); --radius: the brush; --detail: 0..1 as the slider.
// Prints the slowest dab, the sum and the stroke-end time in ms; --log prints every dab.
#include "core/Document.h"
#include "core/Dyntopo.h"
#include "core/Parts.h"
#include "core/Primitives.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace sf;
using Clock = std::chrono::steady_clock;

static double msSince(Clock::time_point t) { return std::chrono::duration<double, std::milli>(Clock::now() - t).count(); }

int main(int argc, char** argv) {
    size_t tris = 800000;
    float radius = 0.1f, detail = 0.23f;
    int dabs = 40;
    bool log = false, symX = false;
    float bigSize = 0.0f;
    int groups = 0;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&]() { return i + 1 < argc ? std::atof(argv[++i]) : 0.0; };
        if (a == "--tris") tris = size_t(next());
        else if (a == "--radius") radius = float(next());
        else if (a == "--detail") detail = float(next());
        else if (a == "--dabs") dabs = int(next());
        else if (a == "--sym") symX = true;
        else if (a == "--big") bigSize = float(next());
        else if (a == "--groups") groups = int(next());
        else if (a == "--log") log = true;
    }
    Document doc;
    const int res = std::max(2, int(std::sqrt(double(tris) / 12.0)));
    doc.mesh = makeQuadSphere(res, 2.0f);
    doc.mesh.multires.reset();
    if (groups > 0) { // face groups in a grid of cells over the Part: many group borders (feature edges)
        doc.mesh.facegroups.resize(doc.mesh.triangleCount());
        for (size_t t = 0; t < doc.mesh.triangleCount(); ++t) {
            const glm::uvec3& tri = doc.mesh.triangles[t];
            const glm::vec3 c = (doc.mesh.positions[tri.x] + doc.mesh.positions[tri.y] + doc.mesh.positions[tri.z]) / 3.0f;
            doc.mesh.facegroups[t] = uint32_t(int((c.x + 2.0f) * float(groups) / 4.0f) + groups * int((c.y + 2.0f) * float(groups) / 4.0f)) + 1;
        }
    }
    if (bigSize > 0.0f) { // beside the dense Part, a plane of a few huge triangles (a flat area of a remeshed head), stroked instead
        Mesh plane = makePlane(2, bigSize);
        for (glm::vec3& p : plane.positions) p = glm::vec3(p.x, -p.z, 3.0f); // (facing the camera)
        for (glm::uvec3& t : plane.triangles) std::swap(t.y, t.z);
        std::vector<const Mesh*> both{&doc.mesh, &plane};
        doc.mesh = mergeMeshes(both);
    }
    doc.mesh.rebuildTopology();
    doc.bvh.build(doc.mesh);
    Mesh& m = doc.mesh;
    std::printf("Part: %zu triangles, brush radius %.3f, Detail %.0f%%\n", m.triangleCount(), radius, detail * 100.0f);
    doc.sculptor.dyntopo.enabled = true;
    doc.sculptor.dyntopo.detail = detail;
    doc.sculptor.beginStroke(m, "Clay Block");
    Symmetry sym{symX, false, false};
    double worst = 0.0, sum = 0.0;
    glm::vec3 last(0.0f);
    const float step = 0.15f * radius;
    for (int i = 0; i < dabs; ++i) {
        const float x = 0.15f + std::fmod(float(i) * step, 0.6f), y = 0.2f + 0.1f * std::sin(float(i) * 0.3f);
        RayHit hit;
        if (!doc.bvh.raycast(m, glm::vec3(x, y, 5.0f), glm::vec3(0, 0, -1), hit)) continue;
        if (bigSize > 0.0f) { // (the plane is nearer the camera than the sphere's top: the ray hits it)
            if (hit.position.z < 2.0f) continue;
        }
        DabParams p;
        p.center = hit.position;
        p.viewDir = glm::vec3(0, 0, -1);
        p.radius = radius;
        p.strength = 0.5f;
        p.strokeDelta = i ? hit.position - last : glm::vec3(0.0f);
        last = hit.position;
        const double remesh0 = doc.sculptor.timings.remeshMs;
        const auto t = Clock::now();
        doc.sculptor.dab(m, doc.bvh, BrushType::ClayBlock, p, sym);
        const double ms = msSince(t);
        worst = std::max(worst, ms);
        sum += ms;
        if (log) std::printf("dab %3d: %8.1f ms (remesh %8.1f) triangles %zu\n", i, ms, doc.sculptor.timings.remeshMs - remesh0, m.triangleCount());
    }
    const auto t = Clock::now();
    UndoRecord r = doc.sculptor.endStroke();
    const double endMs = msSince(t);
    std::printf("dabs: worst %.1f ms, total %.1f ms; stroke end %.1f ms; triangles after %zu\n", worst, sum, endMs, m.triangleCount());
    const DyntopoStats& st = doc.sculptor.dynamicTopology().stats;
    std::printf("collapses %zu of %zu tries; limited %zu; remesh phases (ms, summed): gather %.1f, split %.1f, collapse %.1f, flip %.1f, relax %.1f, finish %.1f\n", st.collapses, st.collapseTries, st.limited, st.gatherMs,
                st.splitMs, st.collapseMs, st.flipMs, st.relaxMs, st.finishMs);
    return 0;
}
