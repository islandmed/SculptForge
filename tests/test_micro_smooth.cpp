// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Smooth on micro levels with a big brush (#220): a big Flatten then Smooth over it left a diamond lattice along the
// coarse micro triangles (the scale-adaptive stride). Geometric invariant (GeomInvariants.h, microUmbrella): the
// grid's edge umbrella after the strokes stays as low as the same strokes evaluated on every grid point.
#include "TestHarness.h"

#include "GeomInvariants.h"

#include "core/Benchmark.h"
#include "core/Micro.h"

#include <cmath>
#include <cstdio>

using namespace sf;

namespace {

DabParams dabAt(const glm::vec3& c, float r, float strength) {
    DabParams p;
    p.center = c;
    p.radius = r;
    p.strength = strength;
    p.viewDir = glm::vec3(0, 0, -1);
    return p;
}

// One stroke of `type`: `count` dabs from `from` to `to` on the sphere (the pen's travel in strokeDelta).
void strokeAcross(Document& doc, BrushType type, glm::vec3 from, glm::vec3 to, int count, float r, float s) {
    Symmetry sym;
    sym.x = false;
    CHECK(doc.sculptor.beginStroke(doc.mesh, brushName(type)));
    for (int k = 0; k < count; ++k) {
        const float u = count > 1 ? float(k) / float(count - 1) : 0.0f;
        DabParams p = dabAt(glm::normalize(from + (to - from) * u), r, s);
        p.strokeDelta = (to - from) / float(std::max(1, count - 1));
        doc.sculptor.dab(doc.mesh, doc.bvh, type, p, sym);
    }
    doc.undo.push(doc.sculptor.endStroke());
}

struct Result {
    geom::MicroUmbrella flat, smooth;
    uint32_t stride = 0;
};

// The #220 repro: a Micro 4 sphere, two big Flatten dabs side by side (held: several dabs each), then big Smooth
// strokes over them, a few passes.
Result flattenThenSmooth(BrushType smooth, bool adaptive) {
    Document doc = makeDoc(makeBenchSphere(20000));
    std::string err;
    CHECK(microDivide(doc.mesh, 4, err));
    doc.sculptor.microStroke().adaptive = adaptive;
    const float r = 0.4f;
    const glm::vec3 a(-0.15f, 0.0f, 1.0f), b(0.15f, 0.0f, 1.0f);
    const std::vector<glm::vec3> centers = {glm::normalize(a), glm::normalize(b)};
    const geom::MicroSnapshot before = geom::microSnapshot(doc.mesh, glm::vec3(0, 0, 1), 0.7f);
    strokeAcross(doc, BrushType::Flatten, a, a, 6, r, 0.5f);
    strokeAcross(doc, BrushType::Flatten, b, b, 6, r, 0.5f);
    Result res;
    res.flat = geom::microUmbrella(doc.mesh, before, geom::microSnapshot(doc.mesh, glm::vec3(0, 0, 1), 0.7f), centers, 0.8f * r);
    for (int pass = 0; pass < 4; ++pass) strokeAcross(doc, smooth, a, b, 12, r, 0.5f);
    res.stride = doc.sculptor.microStroke().lastStride;
    res.smooth = geom::microUmbrella(doc.mesh, before, geom::microSnapshot(doc.mesh, glm::vec3(0, 0, 1), 0.7f), centers, 0.8f * r);
    return res;
}

} // namespace

TEST(micro_big_smooth_after_flatten_leaves_no_lattice) {
    // (11-17, Smooth: stride 2 scored worst 1.49 / mean 0.088 micro edges with the change interpolated, against 0.016 /
    // 0.0037 on every point; 0.022 / 0.0042 with the weight interpolated and every point smoothed)
    for (BrushType type : {BrushType::Smooth, BrushType::SmoothPeaks}) {
        const Result full = flattenThenSmooth(type, false), coarse = flattenThenSmooth(type, true);
        std::printf("  #220 %s umbrella (micro edges) after Flatten: stride 1 worst %.4f mean %.4f, stride %u worst %.4f mean %.4f\n",
                    brushName(type), full.flat.worst, full.flat.mean, coarse.stride, coarse.flat.worst, coarse.flat.mean);
        std::printf("  #220 %s umbrella (micro edges) after it:      stride 1 worst %.4f mean %.4f, stride %u worst %.4f mean %.4f\n",
                    brushName(type), full.smooth.worst, full.smooth.mean, coarse.stride, coarse.smooth.worst, coarse.smooth.mean);
        CHECK(full.stride == 1);
        CHECK(coarse.stride > 1); // (the big brush runs scale-adaptive)
        CHECK(coarse.smooth.count > 1000);
        // The big brush leaves the grid as smooth as the same strokes on every point (a lattice scores its crease depth
        // on the points between the coarse ones).
        CHECK(coarse.smooth.mean <= 1.5f * full.smooth.mean + 0.002f);
        CHECK(coarse.smooth.worst <= 1.5f * full.smooth.worst + 0.01f);
    }
}

namespace {

// The displacement's rms over the front (the detail a Smooth takes down).
double frontDetail(const Document& doc) {
    const MicroMesh& mm = *doc.mesh.micro;
    double sum = 0.0;
    size_t count = 0;
    for (uint32_t t = 0; t < mm.baseTriangles; ++t) {
        const glm::uvec3 tr = doc.mesh.triangles[t];
        const glm::vec3 c = (doc.mesh.positions[tr.x] + doc.mesh.positions[tr.y] + doc.mesh.positions[tr.z]) / 3.0f;
        if (glm::distance(glm::normalize(c), glm::vec3(0, 0, 1)) > 0.2f) continue;
        for (uint32_t k = 0; k < mm.samples; ++k) {
            const double v = double(mm.disp[size_t(t) * mm.samples + k]) * mm.step;
            sum += v * v, ++count;
        }
    }
    return count ? std::sqrt(sum / double(count)) : 0.0;
}

// The share of fine detail (micro_noise) left after big Smooth strokes on a Micro level.
double detailKept(int level, bool adaptive, uint32_t* stride) {
    Document doc = makeDoc(makeBenchSphere(20000));
    std::string err;
    CHECK(microDivide(doc.mesh, level, err));
    microNoise(doc.mesh, 0.003f, 250.0f);
    doc.sculptor.microStroke().adaptive = adaptive;
    const double before = frontDetail(doc);
    for (int pass = 0; pass < 2; ++pass)
        strokeAcross(doc, BrushType::Smooth, glm::vec3(-0.15f, 0.0f, 1.0f), glm::vec3(0.15f, 0.0f, 1.0f), 12, 0.4f, 0.5f);
    *stride = doc.sculptor.microStroke().lastStride;
    return frontDetail(doc) / before;
}

} // namespace

TEST(micro_big_smooth_takes_detail_down_as_on_every_point) {
    // The other half of #220: the big Smooth takes fine detail down as much as the same strokes on every grid point
    // (a plain mesh's Smooth), and no more. (11-17: the interpolated coarse change left the detail as it was, 100-103%
    // kept on Micro 4 and 5; smoothing every point: ~91%, as on every point.)
    for (int level : {4, 5}) {
        uint32_t s1 = 0, s2 = 0;
        const double full = detailKept(level, false, &s1), coarse = detailKept(level, true, &s2);
        std::printf("  #220 Micro %d detail kept after big Smooth: stride 1 %.3f, stride %u %.3f\n", level, full, s2, coarse);
        CHECK(s1 == 1 && s2 > 1);
        CHECK(full < 0.97); // (the strokes do take detail down)
        CHECK(std::abs(coarse - full) <= 0.02);
    }
}
