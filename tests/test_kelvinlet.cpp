// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Kelvinlet brushes (core/Kelvinlet.h): the kernels (normalization, multi-scale far-field cancellation,
// incompressibility, exact twist, pinch without folding), the brushes on meshes (symmetry, mask, undo, sculpt
// layers, adaptive cutoff, bulge with Squash), Detail Clone, and Alpha / VDM from the viewport
// (core/ViewCapture.h) with its commands. Timing checks run with SF_BENCH_LIMITS=1.
#include "TestHarness.h"

#include "core/Commands.h"
#include "core/Kelvinlet.h"
#include "core/PathStroke.h"
#include "core/Primitives.h"
#include "core/Sculpt.h"
#include "core/ThreadPool.h"
#include "core/Vdm.h"
#include "core/ViewCapture.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>

using namespace sf;

namespace {

bool run(Document& d, const std::string& name, const Args& args = {}) {
    static CommandRegistry registry;
    const CommandResult r = registry.execute(d, name, args);
    if (!r.ok) std::printf("  %s: %s\n", name.c_str(), r.message.c_str());
    return r.ok;
}

uint32_t nearest(const Mesh& m, const glm::vec3& p) {
    uint32_t best = 0;
    for (uint32_t v = 1; v < m.vertexCount(); ++v)
        if (glm::distance(m.positions[v], p) < glm::distance(m.positions[best], p)) best = v;
    return best;
}

KelvinletSettings squash(float s, float sharp = 0.0f) {
    KelvinletSettings k;
    k.squash = s;
    k.sharpness = sharp;
    return k;
}

// Numerical divergence of the grab field at r.
float divergence(const KelvinletKernel& k, const glm::vec3& r, const glm::vec3& f) {
    const float h = 1e-3f;
    float div = 0.0f;
    for (int a = 0; a < 3; ++a) {
        glm::vec3 e(0.0f);
        e[a] = h;
        div += (kelvinletGrab(k, r + e, f)[a] - kelvinletGrab(k, r - e, f)[a]) / (2.0f * h);
    }
    return div;
}

PathStroke pathAlongX(float x0, float x1, float radius, float y = 0.0f) {
    PathStroke st;
    st.points = {glm::vec3(x0, y, 2.0f), glm::vec3(x1, y, 2.0f)};
    st.view = glm::vec3(0.0f, 0.0f, -1.0f);
    st.radius = radius;
    st.strength = 0.5f;
    st.curve = false;
    return st;
}

} // namespace

TEST(kelvinlet_kernels_are_normalized_and_localized) {
    for (float sq : {0.0f, 0.5f, 1.0f})
        for (float sharp : {0.0f, 1.0f}) {
            const KelvinletSettings s = squash(sq, sharp);
            for (bool tight : {false, true}) {
                const KelvinletKernel k = makeKelvinletKernel(KelvinletKind::Grab, 0.2f, s, tight);
                // The grabbed point moves exactly with the cursor.
                const glm::vec3 d(0.03f, -0.01f, 0.02f);
                CHECK(glm::length(kelvinletGrab(k, glm::vec3(0.0f), d) - d) < 1e-5f);
                // The far-field terms cancel: sum w = 0 (1/r) and, for three scales, sum w e^2 = 0 (1/r^3).
                float sw = 0.0f, swe = 0.0f;
                for (int i = 0; i < k.scales; ++i) sw += k.w[i], swe += k.w[i] * k.eps[i] * k.eps[i];
                CHECK(std::abs(sw) < 1e-5f);
                if (k.scales == 3) CHECK(std::abs(swe) < 1e-5f * k.eps[2] * k.eps[2]);
                // About half way at half the radius (the Move brush's footprint), almost nothing at 3 radii.
                const float half = glm::length(kelvinletGrab(k, glm::vec3(0.1f, 0.0f, 0.0f), glm::vec3(0, 0, 1)));
                if (sharp == 0.0f && !tight) CHECK(half > 0.15f && half < 0.8f);
                CHECK(k.envelope(0.6f) < 0.03f);
                // The cutoff grows with the step and stays inside the search range.
                const float small = k.cutoff(0.001f, 0.0006f, 0.8f), big = k.cutoff(0.05f, 0.0006f, 0.8f);
                CHECK(small > 0.0f && small < big && big <= 0.8f);
            }
            for (KelvinletKind kind : {KelvinletKind::Twist, KelvinletKind::Scale}) {
                const KelvinletKernel k = makeKelvinletKernel(kind, 0.2f, s);
                CHECK(std::abs(k.profile(0.0f) - 1.0f) < 1e-5f);
                CHECK(k.profile(0.6f * 0.6f) < 0.01f);
            }
        }
    // Sharpness gives a tighter peak with the same far field.
    const KelvinletKernel soft = makeKelvinletKernel(KelvinletKind::Grab, 0.2f, squash(0.5f, 0.0f));
    const KelvinletKernel sharp = makeKelvinletKernel(KelvinletKind::Grab, 0.2f, squash(0.5f, 1.0f));
    CHECK(sharp.envelope(0.08f) < soft.envelope(0.08f));
}

TEST(kelvinlet_squash_is_poissons_ratio) {
    // Incompressible clay (Squash 1, nu = 0.49) is nearly divergence free; rubber (Squash 0) is not.
    const KelvinletKernel clay = makeKelvinletKernel(KelvinletKind::Grab, 0.2f, squash(1.0f));
    const KelvinletKernel rubber = makeKelvinletKernel(KelvinletKind::Grab, 0.2f, squash(0.0f));
    const glm::vec3 f(0.0f, 0.0f, 0.05f);
    float dc = 0.0f, dr = 0.0f;
    for (const glm::vec3 r : {glm::vec3(0.05f, 0.0f, 0.05f), glm::vec3(0.1f, 0.05f, -0.08f), glm::vec3(0.0f, 0.12f, 0.1f)}) {
        dc += std::abs(divergence(clay, r, f));
        dr += std::abs(divergence(rubber, r, f));
    }
    CHECK(dc < 0.1f * dr);
    // Pressing a flat sheet in: incompressible clay bulges up around the dent, more than rubber does.
    auto bulge = [](float sq) {
        Document d = makeDoc(makePlane(160, 2.0f));
        d.sculptor.kelvinlet.squash = sq;
        CHECK(d.sculptor.beginStroke(d.mesh, "KelvinGrab"));
        DabParams p;
        p.center = glm::vec3(0.0f);
        p.viewDir = glm::vec3(0.0f, -1.0f, 0.0f);
        p.radius = 0.25f;
        p.strength = 0.5f;
        p.strokeDelta = glm::vec3(0.0f, -0.15f, 0.0f);
        d.sculptor.dab(d.mesh, d.bvh, BrushType::KelvinGrab, p, Symmetry{false, false, false});
        d.undo.push(d.sculptor.endStroke());
        float up = 0.0f;
        for (const glm::vec3& q : d.mesh.positions) up = std::max(up, q.y);
        const float centre = d.mesh.positions[nearest(d.mesh, glm::vec3(0.0f, -0.15f, 0.0f))].y;
        CHECK(centre < -0.12f); // the thumb pushed the centre down with it
        return up;
    };
    const float clayUp = bulge(1.0f), rubberUp = bulge(0.0f);
    CHECK(clayUp > 0.0f);
    CHECK(clayUp > rubberUp * 1.3f);
}

TEST(kelvinlet_twist_is_a_rotation_and_scale_never_folds) {
    const KelvinletKernel tw = makeKelvinletKernel(KelvinletKind::Twist, 0.2f, KelvinletSettings{});
    const glm::vec3 axis(0.0f, 0.0f, 1.0f);
    for (const glm::vec3 r : {glm::vec3(0.05f, 0.0f, 0.0f), glm::vec3(0.1f, 0.1f, 0.03f), glm::vec3(0.3f, -0.2f, 0.1f)}) {
        const glm::vec3 moved = r + kelvinletTwist(tw, r, axis, 2.5f);
        CHECK(std::abs(glm::length(moved) - glm::length(r)) < 1e-5f); // stays on its shell
        CHECK(std::abs(moved.z - r.z) < 1e-6f);                       // and its height along the axis
    }
    // The centre turns by the full angle.
    const glm::vec3 nearCentre(1e-4f, 0.0f, 0.0f);
    const glm::vec3 m = nearCentre + kelvinletTwist(tw, nearCentre, axis, 0.5f);
    CHECK(std::abs(std::atan2(m.y, m.x) - 0.5f) < 1e-3f);
    // A hard pinch keeps the order of points along a ray (no folding through the centre).
    const KelvinletKernel sc = makeKelvinletKernel(KelvinletKind::Scale, 0.2f, KelvinletSettings{});
    float last = 0.0f;
    for (int i = 1; i <= 100; ++i) {
        const glm::vec3 r(0.006f * float(i), 0.0f, 0.0f);
        const float len = glm::length(r + kelvinletScale(sc, r, -4.0f));
        CHECK(len > last);
        last = len;
    }
    const glm::vec3 r(0.01f, 0.0f, 0.0f);
    CHECK(glm::length(r + kelvinletScale(sc, r, std::log(2.0f))) > 0.019f); // doubles at the centre
}

TEST(kelvinlet_grab_on_a_sphere_symmetry_mask_undo) {
    Document d = makeDoc(makeIcosphere(5, 1.0f));
    const auto start = d.mesh.positions;
    // Mask the -Y half.
    auto& mask = d.mesh.ensureMask(0.0f);
    for (uint32_t v = 0; v < d.mesh.vertexCount(); ++v)
        if (d.mesh.positions[v].y < -0.05f) mask[v] = 1.0f;
    PathStroke st = pathAlongX(0.4f, 0.75f, 0.25f, 0.3f);
    st.symmetry = Symmetry{true, false, false};
    std::string err;
    CHECK(strokePath(d, nullptr, BrushType::KelvinGrab, st, err) > 0);
    size_t moved = 0;
    float worstMirror = 0.0f, maskedMove = 0.0f, rimMove = 0.0f, maxMove = 0.0f;
    for (uint32_t v = 0; v < d.mesh.vertexCount(); ++v) {
        const glm::vec3 dv = d.mesh.positions[v] - start[v];
        maxMove = std::max(maxMove, glm::length(dv));
        if (glm::length(dv) > 1e-6f) ++moved;
        if (mask[v] == 1.0f) maskedMove = std::max(maskedMove, glm::length(dv));
        // At the edge of what moved (a moved vertex next to one that didn't), it moved next to nothing.
        if (mask[v] == 0.0f && glm::length(dv) > 0.0f)
            for (uint32_t n : d.mesh.neighbors(v))
                if (mask[n] == 0.0f && d.mesh.positions[n] == start[n]) rimMove = std::max(rimMove, glm::length(dv));
    }
    CHECK(moved > 200);
    CHECK(moved < d.mesh.vertexCount()); // the cutoff left the far side alone
    CHECK(maskedMove == 0.0f);
    CHECK(maxMove > 0.1f);
    // No hard falloff edge: the field fades to nothing before the cutoff.
    CHECK(rimMove < 0.01f * maxMove);
    // Symmetry: compare each vertex of the +X half with its start-mirror.
    {
        Mesh ref = makeIcosphere(5, 1.0f);
        BVH bvh;
        bvh.build(ref);
        int checked = 0;
        for (uint32_t v = 0; v < ref.vertexCount() && checked < 400; v += 7) {
            const glm::vec3 p = ref.positions[v];
            if (p.x < 0.2f) continue;
            std::vector<uint32_t> near;
            bvh.queryVertices(ref, glm::vec3(-p.x, p.y, p.z), 1e-4f, near);
            if (near.size() != 1) continue;
            const glm::vec3 a = d.mesh.positions[v] - p, b = d.mesh.positions[near[0]] - ref.positions[near[0]];
            worstMirror = std::max(worstMirror, glm::length(a - glm::vec3(-b.x, b.y, b.z)));
            ++checked;
        }
        CHECK(checked > 50);
        CHECK(worstMirror < 1e-4f);
    }
    CHECK(d.undo.canUndo());
    d.undo.undo(d.mesh, d.bvh);
    float back = 0.0f;
    for (uint32_t v = 0; v < d.mesh.vertexCount(); ++v) back = std::max(back, glm::distance(d.mesh.positions[v], start[v]));
    CHECK(back < 1e-6f);
}

TEST(kelvinlet_brushes_write_into_sculpt_layers) {
    Document d = makeDoc(makeIcosphere(4, 1.0f));
    CHECK(run(d, "layer_add", {{"name", std::string("Elastic")}}));
    const auto start = d.mesh.positions;
    for (BrushType type : {BrushType::KelvinGrab, BrushType::KelvinTwist, BrushType::KelvinScale, BrushType::KelvinSnake}) {
        std::string err;
        CHECK(strokePath(d, nullptr, type, pathAlongX(0.0f, 0.3f, 0.3f), err) > 0);
    }
    float layerSum = 0.0f;
    for (const glm::vec3& o : d.mesh.layers.back().delta) layerSum += glm::length(o);
    CHECK(layerSum > 0.1f);
    // Turning the layer off puts the base back.
    CHECK(run(d, "layer_set_weight", {{"index", 0.0}, {"weight", 0.0}}));
    float worst = 0.0f;
    for (uint32_t v = 0; v < d.mesh.vertexCount(); ++v) worst = std::max(worst, glm::distance(d.mesh.positions[v], start[v]));
    CHECK(worst < 1e-4f);
}

TEST(kelvinlet_twist_and_snake_strokes) {
    // Twist turns the surface around the view axis (clockwise for a drag to the right).
    Document d = makeDoc(makePlane(120, 2.0f));
    const auto start = d.mesh.positions;
    PathStroke st;
    st.points = {glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.2f, 1.0f, 0.0f)};
    st.view = glm::vec3(0.0f, -1.0f, 0.0f);
    st.radius = 0.4f;
    st.strength = 0.5f;
    st.curve = false;
    std::string err;
    CHECK(strokePath(d, nullptr, BrushType::KelvinTwist, st, err) > 0);
    const uint32_t v = nearest(d.mesh, glm::vec3(0.1f, 0.0f, 0.0f));
    const glm::vec3 a = start[v], b = d.mesh.positions[v];
    CHECK(std::abs(glm::length(b) - glm::length(a)) < 1e-3f); // a rotation in the sheet
    CHECK(std::abs(b.y) < 1e-6f);
    CHECK(glm::length(b - a) > 0.02f);
    // Scale: a drag to the left pinches toward the press point, to the right swells.
    for (float dir : {-1.0f, 1.0f}) {
        Document sc = makeDoc(makePlane(120, 2.0f));
        PathStroke ps = st;
        ps.points = {glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.2f * dir, 1.0f, 0.0f)};
        CHECK(strokePath(sc, nullptr, BrushType::KelvinScale, ps, err) > 0);
        const uint32_t q = nearest(makePlane(120, 2.0f), glm::vec3(0.1f, 0.0f, 0.05f));
        const float r0 = glm::length(start[q]), r1 = glm::length(sc.mesh.positions[q]);
        std::printf("  scale %+.0f: %.4f -> %.4f\n", dir, r0, r1);
        CHECK(dir < 0.0f ? r1 < r0 * 0.9f : r1 > r0 * 1.1f);
    }
    // Snake pulls a tapering horn straight out of a sphere.
    Document s = makeDoc(makeIcosphere(6, 1.0f));
    PathStroke pull;
    pull.points = {glm::vec3(0.0f, 0.0f, 2.0f), glm::vec3(0.0f, 0.0f, 2.0f)};
    // (headless Snake follows the path in the view plane: pull out to the right of a side view)
    pull.points = {glm::vec3(0.0f, 0.9f, 2.0f), glm::vec3(0.0f, 1.6f, 2.0f)};
    pull.view = glm::vec3(0.0f, 0.0f, -1.0f);
    pull.radius = 0.2f;
    pull.strength = 0.5f;
    pull.curve = false;
    CHECK(strokePath(s, nullptr, BrushType::KelvinSnake, pull, err) > 0);
    float top = 0.0f;
    for (const glm::vec3& p : s.mesh.positions) top = std::max(top, p.y);
    std::printf("  snake: tip at %.3f\n", top);
    CHECK(top > 1.3f); // pulled well out of the unit sphere
}

// A round dome (Z only), so each stamp is one peak.
std::shared_ptr<const VectorDisplacement> domeVdm() {
    VectorDisplacement v;
    v.name = "Dome";
    v.width = v.height = 64;
    v.vectors.resize(64 * 64);
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 64; ++x) {
            const float u = (float(x) + 0.5f) / 32.0f - 1.0f, w = (float(y) + 0.5f) / 32.0f - 1.0f;
            const float d2 = u * u + w * w;
            v.vectors[size_t(y) * 64 + size_t(x)] = glm::vec3(0.0f, 0.0f, d2 < 0.5f ? 0.3f * (1.0f - d2 / 0.5f) : 0.0f);
        }
    return std::make_shared<const VectorDisplacement>(std::move(v));
}

TEST(detail_clone_stamps_along_the_stroke) {
    Document d = makeDoc(makePlane(200, 2.0f));
    d.brushVdm = domeVdm();
    d.sculptor.kelvinlet.cloneSpacing = 1.0f;
    PathStroke st;
    st.points = {glm::vec3(-0.6f, 1.0f, 0.0f), glm::vec3(0.6f, 1.0f, 0.0f)};
    st.view = glm::vec3(0.0f, -1.0f, 0.0f);
    st.radius = 0.1f;
    st.strength = 0.5f;
    st.curve = false;
    std::string err;
    CHECK(strokePath(d, nullptr, BrushType::DetailClone, st, err) > 0);
    // Count the separate stamps: height peaks along x.
    constexpr int kBins = 120;
    float height[kBins] = {};
    for (const glm::vec3& p : d.mesh.positions) {
        const int b = std::clamp(int((p.x + 1.0f) / 2.0f * kBins), 0, kBins - 1);
        height[b] = std::max(height[b], p.y);
    }
    float top = 0.0f;
    for (float h : height) top = std::max(top, h);
    int peaks = 0;
    for (int i = 1; i + 1 < kBins; ++i)
        if (height[i] > 0.4f * top && height[i] >= height[i - 1] && height[i] > height[i + 1]) ++peaks;
    std::printf("  detail clone: %d stamps, top %.3f\n", peaks, top);
    CHECK(top > 0.02f);
    CHECK(peaks >= 5 && peaks <= 8); // 1.2 long, a stamp every 0.2
    // A click stamps once.
    Document c = makeDoc(makePlane(120, 2.0f));
    c.brushVdm = builtinVdm("horn");
    PathStroke click = st;
    click.points = {glm::vec3(0.0f, 1.0f, 0.0f)};
    click.radius = 0.3f;
    CHECK(strokePath(c, nullptr, BrushType::DetailClone, click, err) == 1);
    float h = 0.0f;
    for (const glm::vec3& p : c.mesh.positions) h = std::max(h, p.y);
    CHECK(h > 0.05f);
}

TEST(capture_height_alpha_from_view) {
    Document d = makeDoc(makePlane(160, 2.0f));
    // A bump in the middle.
    PathStroke st;
    st.points = {glm::vec3(0.0f, 1.0f, 0.0f)};
    st.view = glm::vec3(0.0f, -1.0f, 0.0f);
    st.radius = 0.3f;
    st.strength = 1.0f;
    std::string err;
    for (int i = 0; i < 4; ++i) CHECK(strokePath(d, nullptr, BrushType::Standard, st, err) == 1);
    d.syncBvh();
    const CaptureFrame f = makeCaptureFrame(glm::vec3(0.0f), glm::vec3(0.0f, -1.0f, 0.0f), glm::vec3(0.0f, 0.0f, -1.0f), 0.5f);
    CaptureOptions opt;
    opt.resolution = 128;
    opt.smoothing = 0.0f;
    BrushAlpha a;
    CaptureInfo info;
    CHECK(captureHeightAlpha(d.mesh, d.bvh, f, opt, a, err, &info));
    CHECK(a.width == 128 && a.height == 128);
    CHECK(a.sample(0.0f, 0.0f) > 0.9f);
    CHECK(a.sample(0.95f, 0.95f) < 0.05f);
    CHECK(info.depthRange > 0.01f);
    opt.invert = true;
    CHECK(captureHeightAlpha(d.mesh, d.bvh, f, opt, a, err));
    CHECK(a.sample(0.0f, 0.0f) < 0.1f);
    // Nothing under the square: an error.
    const CaptureFrame off = makeCaptureFrame(glm::vec3(5.0f, 0.0f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f), glm::vec3(0.0f, 0.0f, -1.0f), 0.2f);
    CHECK(!captureHeightAlpha(d.mesh, d.bvh, off, opt, a, err) && !err.empty());
    // The command makes it the brush alpha and saves the PNG.
    const std::string png = tempPath("sf_capture_alpha.png");
    std::filesystem::remove(png);
    CHECK(run(d, "capture_alpha", {{"point", std::string("0,1,0")}, {"view", std::string("0,-1,0")}, {"size", 0.5},
                                   {"path", png}, {"name", std::string("Bump")}}));
    CHECK(d.brushAlpha.name == "Bump" && !d.brushAlpha.empty());
    CHECK(std::filesystem::exists(png));
    Image img;
    CHECK(Image::load(png, img, err) && img.width == 256);
}

TEST(capture_vdm_from_view_round_trips_a_stamp) {
    // Stamp an asymmetric, leaning VDM on a grid, capture it from the top, stamp the capture on a fresh grid:
    // the same shape comes back, sideways lean included (fairing a grid with its rim held finds its start).
    auto source = builtinVdm("ear curl");
    Document a = makeDoc(makePlane(200, 2.0f));
    a.brushVdm = source;
    const Args stamp = {{"point", std::string("0,1,0")}, {"view", std::string("0,-1,0")}, {"radius", 0.5}};
    CHECK(run(a, "vdm_stamp", stamp));
    a.syncBvh();
    const CaptureFrame f = makeCaptureFrame(glm::vec3(0.0f), glm::vec3(0.0f, -1.0f, 0.0f), glm::vec3(0.0f, 0.0f, -1.0f), 0.5f);
    CaptureOptions opt;
    opt.resolution = 256;
    opt.smoothing = 1.0f;
    opt.edgeFade = 0.03f;
    VectorDisplacement captured;
    CaptureInfo info;
    std::string err;
    CHECK(captureVdmFromView(a.mesh, a.bvh, f, opt, captured, err, &info));
    CHECK(info.vertices > 1000);
    CHECK(info.maxOffset > 0.1f);
    Document b = makeDoc(makePlane(200, 2.0f));
    b.brushVdm = std::make_shared<const VectorDisplacement>(captured);
    CHECK(run(b, "vdm_stamp", stamp));
    double err2 = 0.0, ref2 = 0.0, lateral = 0.0;
    const Mesh flat = makePlane(200, 2.0f);
    for (uint32_t v = 0; v < flat.vertexCount(); ++v) {
        const glm::vec3 da = a.mesh.positions[v] - flat.positions[v], db = b.mesh.positions[v] - flat.positions[v];
        err2 += double(glm::dot(da - db, da - db));
        ref2 += double(glm::dot(da, da));
        lateral += double(da.x * da.x + da.z * da.z);
    }
    CHECK(lateral > 0.05 * ref2); // the source really leans sideways
    const double rel = std::sqrt(err2 / std::max(ref2, 1e-12));
    std::printf("  capture_vdm round trip: relative RMS error %.3f, %zu points, %d CG iterations\n", rel, info.vertices,
                info.iterations);
    CHECK(rel < 0.2);
    // The command adds it to the VDMs and makes it the brush's.
    const size_t before = a.vdms.size();
    CHECK(run(a, "capture_vdm", {{"point", std::string("0,1,0")}, {"view", std::string("0,-1,0")}, {"size", 0.5},
                                 {"name", std::string("Ear Copy")}, {"save", false}, {"resolution", 64.0}}));
    CHECK(a.vdms.size() == before + 1 && a.brushVdm && a.brushVdm->name == "Ear Copy");
}

TEST(capture_vdm_on_a_curved_surface_keeps_only_the_detail) {
    // A horn on a sphere: the sphere's dome stays in the base (the quadric), so the map is the horn alone.
    Document d = makeDoc(makeIcosphere(7, 1.0f));
    d.brushVdm = builtinVdm("horn");
    CHECK(run(d, "vdm_stamp", {{"point", std::string("0,0,2")}, {"view", std::string("0,0,-1")}, {"radius", 0.15}}));
    d.syncBvh();
    const CaptureFrame f = makeCaptureFrame(glm::vec3(0.0f, 0.0f, 1.0f), glm::vec3(0.0f, 0.0f, -1.0f), glm::vec3(0.0f, 1.0f, 0.0f), 0.4f);
    CaptureOptions opt;
    opt.resolution = 128;
    VectorDisplacement v;
    std::string err;
    CHECK(captureVdmFromView(d.mesh, d.bvh, f, opt, v, err));
    float centre = 0.0f, band = 0.0f;
    for (int y = 0; y < v.height; ++y)
        for (int x = 0; x < v.width; ++x) {
            const float u = (float(x) + 0.5f) / float(v.width) * 2.0f - 1.0f, w = 1.0f - (float(y) + 0.5f) / float(v.height) * 2.0f;
            const float m = glm::length(v.vectors[size_t(y) * size_t(v.width) + size_t(x)]);
            const float d2 = std::max(std::abs(u), std::abs(w));
            if (d2 < 0.3f) centre = std::max(centre, m);
            else if (d2 > 0.5f && d2 < 0.8f) band = std::max(band, m);
        }
    std::printf("  horn on a sphere: centre %.3f, outer band %.4f (radii)\n", centre, band);
    CHECK(centre > 0.2f);
    CHECK(band < 0.15f * centre); // (what is left: the sphere is not quite a quadric over so wide a square)
}

TEST(kelvinlet_is_deterministic_with_threads) {
    std::vector<glm::vec3> result[2];
    for (int pass = 0; pass < 2; ++pass) {
        ThreadPool::shared().setSerial(pass == 0);
        Document d = makeDoc(makeIcosphere(6, 1.0f));
        std::string err;
        for (BrushType t : {BrushType::KelvinGrab, BrushType::KelvinTwist, BrushType::KelvinScale, BrushType::KelvinSnake}) {
            PathStroke st = pathAlongX(-0.2f, 0.3f, 0.5f, 0.1f);
            st.symmetry = Symmetry{true, false, false};
            CHECK(strokePath(d, nullptr, t, st, err) > 0);
        }
        result[pass] = d.mesh.positions;
    }
    ThreadPool::shared().setSerial(false);
    CHECK(result[0] == result[1]);
}

TEST(kelvinlet_grab_dense_mesh_timing) {
    // ~2.6M triangles: one drag step of a large elastic grab. Limits only with SF_BENCH_LIMITS=1.
    Document d = makeDoc(makeIcosphere(8, 1.0f));
    d.syncBvh();
    CHECK(d.sculptor.beginStroke(d.mesh, "KelvinGrab"));
    KelvinletStep step;
    step.center = glm::vec3(0.0f, 0.0f, 1.0f);
    step.radius = 0.3f;
    step.delta = glm::vec3(0.01f, 0.005f, 0.0f);
    step.viewDir = glm::vec3(0.0f, 0.0f, -1.0f);
    double worst = 0.0, total = 0.0;
    for (int i = 0; i < 10; ++i) {
        const auto t0 = std::chrono::steady_clock::now();
        d.sculptor.kelvinletStep(d.mesh, d.bvh, BrushType::KelvinGrab, step, Symmetry{false, false, false});
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        worst = std::max(worst, ms);
        total += ms;
        step.center += step.delta;
    }
    const SculptTimings tm = d.sculptor.timings;
    std::printf("  (kernel %.1f, normals %.1f, BVH %.1f ms in all)\n", tm.kernelMs, tm.normalsMs, tm.bvhMs);
    d.undo.push(d.sculptor.endStroke());
    std::printf("  KelvinGrab r=0.3 on %u triangles: %.1f ms per step (worst %.1f), %llu vertices a step\n",
                d.mesh.triangleCount(), total / 10.0, worst,
                (unsigned long long)(d.sculptor.timings.vertices / std::max<uint64_t>(1, d.sculptor.timings.dabs)));
    if (std::getenv("SF_BENCH_LIMITS")) CHECK(total / 10.0 < 60.0);
}
