// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Make sculptable (core/Sculptable.h): splat capture -> watertight surface -> face-aware quads with colour.
#include "TestHarness.h"

#include "core/BVH.h"
#include "core/Commands.h"
#include "core/Multires.h"
#include "core/Primitives.h"
#include "core/QRemesh.h"
#include "core/Sculptable.h"
#include "core/Texture.h"
#include "core/ThreadPool.h"

#include <cmath>

using namespace sf;

namespace {

double volume(const Mesh& m) {
    double v = 0;
    for (const glm::uvec3& t : m.triangles)
        v += double(glm::dot(m.positions[t.x], glm::cross(m.positions[t.y], m.positions[t.z]))) / 6.0;
    return v;
}

bool watertight(const Mesh& m) {
    const TopologyReport r = topologyReport(m);
    return r.manifold() && r.boundaryEdges == 0 && m.triangleCount() > 0;
}

SplatCloud sphereCapture(size_t count, size_t floaters = 0, size_t interior = 0, bool openBottom = false) {
    Mesh s = makeIcosphere(4);
    std::vector<glm::vec3>& c = s.ensureColors();
    for (size_t v = 0; v < s.vertexCount(); ++v)
        c[v] = s.positions[v].x > 0.0f ? srgbToLinear(glm::vec3(0.9f, 0.1f, 0.1f)) : srgbToLinear(glm::vec3(0.1f, 0.2f, 0.9f));
    TestCaptureOptions o;
    o.count = count;
    o.floaters = floaters;
    o.interior = interior;
    o.openBottom = openBottom;
    return makeTestCapture(s, o);
}

glm::vec3 eyeCentre(const FaceLandmarks& lm, int side) { return 0.5f * (lm.eyeOuter[size_t(side)] + lm.eyeInner[size_t(side)]); }

// The closed guide whose centre (in x, y) is nearest `p`.
const QRemeshGuide* loopAround(const std::vector<QRemeshGuide>& guides, const glm::vec3& p, float maxCentreDistance) {
    const QRemeshGuide* best = nullptr;
    float bestD = maxCentreDistance;
    for (const QRemeshGuide& g : guides) {
        if (!g.closed) continue;
        glm::vec3 c(0.0f);
        for (const glm::vec3& q : g.points) c += q;
        c /= float(g.points.size());
        const float d = glm::length(glm::vec2(c - p));
        if (d < bestD) {
            bestD = d;
            best = &g;
        }
    }
    return best;
}

// Whether the 2D (x, y) polygon encloses p.
bool encloses(const QRemeshGuide& g, const glm::vec3& p) {
    bool in = false;
    const size_t n = g.points.size();
    for (size_t i = 0, j = n - 1; i < n; j = i++) {
        const glm::vec3 &a = g.points[i], &b = g.points[j];
        if ((a.y > p.y) != (b.y > p.y) && p.x < (b.x - a.x) * (p.y - a.y) / (b.y - a.y) + a.x) in = !in;
    }
    return in;
}

} // namespace

TEST(sculptable_surface_watertight) {
    const SplatCloud c = sphereCapture(60000);
    SplatSurfaceOptions o;
    o.resolution = 80;
    Mesh m;
    std::string err;
    SplatSurfaceStats st;
    CHECK(splatSurface(c, o, m, err, {}, &st));
    CHECK(watertight(m));
    CHECK(volume(m) > 0.0);
    CHECK(st.used == c.size() && st.pieces == 1);
    const AABB b = m.bounds();
    CHECK(std::abs(b.extent().x - 2.0f) < 0.12f && glm::length(b.center()) < 0.05f);
    // About the sphere's volume (4.19): a solid, not a hollow shell.
    CHECK(std::abs(volume(m) - 4.18879) < 0.4);
}

TEST(sculptable_floaters_and_crop) {
    const SplatCloud c = sphereCapture(60000, 20);
    SplatSurfaceOptions o;
    o.resolution = 80;
    o.keepFraction = 1.0f; // the grid takes in the floaters too; only the largest piece survives
    Mesh m;
    std::string err;
    SplatSurfaceStats st;
    CHECK(splatSurface(c, o, m, err, {}, &st));
    CHECK(st.pieces > 1 && st.floaterVoxels > 0);
    CHECK(watertight(m));
    const AABB b = m.bounds();
    // The floaters are 2.2 or more from the centre; the grid is coarse here (it spans them), so allow a voxel or two.
    const float slack = 1.0f + 2.0f * st.cell;
    CHECK(b.max.x < slack && b.min.x > -slack && b.max.y < slack && b.min.y > -slack);

    // Keep every piece: the floaters come back.
    o.keepAllParts = true;
    Mesh all;
    CHECK(splatSurface(c, o, all, err));
    CHECK(glm::length(all.bounds().extent()) > glm::length(b.extent()) * 1.3f);

    // A crop box around the right half: only splats inside count.
    o.keepAllParts = false;
    o.crop = true;
    o.cropBox.min = glm::vec3(0.2f, -1.5f, -1.5f);
    o.cropBox.max = glm::vec3(1.5f, 1.5f, 1.5f);
    Mesh cropped;
    CHECK(splatSurface(c, o, cropped, err, {}, &st));
    CHECK(st.cropped > 0 && watertight(cropped));
    CHECK(cropped.bounds().min.x > 0.1f);

    // Nothing in the box.
    o.cropBox.min = glm::vec3(5.0f);
    o.cropBox.max = glm::vec3(6.0f);
    CHECK(!splatSurface(c, o, cropped, err));
    CHECK(err.find("crop") != std::string::npos);
}

TEST(sculptable_inside_splats_dont_win) {
    // Trained captures fill the inside with splats. At fine Detail that inner cloud covers more voxels than the
    // thin surface shell; the surface must still be the one kept.
    const SplatCloud c = sphereCapture(60000, 0, 20000);
    SplatSurfaceOptions o;
    o.resolution = 200;
    Mesh m;
    std::string err;
    CHECK(splatSurface(c, o, m, err));
    CHECK(watertight(m));
    CHECK(std::abs(m.bounds().extent().x - 2.0f) < 0.08f && std::abs(volume(m) - 4.18879) < 0.4);
}

TEST(sculptable_fills_open_bottom) {
    // A subject standing on a table: no splats on its lowest part. Fill holes keeps it solid.
    const SplatCloud c = sphereCapture(60000, 0, 0, true);
    SplatSurfaceOptions o;
    o.resolution = 80;
    Mesh filled, hollow;
    std::string err;
    CHECK(splatSurface(c, o, filled, err));
    CHECK(watertight(filled));
    CHECK(volume(filled) > 3.0); // nearly the whole sphere (4.19) minus the cut cap
    o.fillHoles = false;
    CHECK(splatSurface(c, o, hollow, err));
    if (volume(hollow) >= 1.0) std::printf("  hollow volume %.3f, filled %.3f\n", volume(hollow), volume(filled));
    CHECK(volume(hollow) < 1.0); // without it the flood leaks in: only a thin shell
}

TEST(sculptable_thin_parts_survive) {
    // The mug's handle is a tube 0.15 across: at 160 voxels over 2.4 it is ~10 voxels thick and must stay one piece
    // with the body, its hole open (Fill holes only fills what is boxed in along 5 of the 6 axis directions).
    Mesh mug = makeTestObject(160);
    TestCaptureOptions co;
    co.count = 150000;
    const SplatCloud c = makeTestCapture(mug, co);
    SplatSurfaceOptions o;
    o.resolution = 160;
    Mesh m;
    std::string err;
    SplatSurfaceStats st;
    CHECK(splatSurface(c, o, m, err, {}, &st));
    const TopologyReport tr = topologyReport(m);
    std::printf("  mug: %zu boundary, %zu non-manifold edges, %zu non-manifold vertices, %d pieces\n", tr.boundaryEdges,
                tr.nonManifoldEdges, tr.nonManifoldVertices, st.pieces);
    CHECK(watertight(m));
    CHECK(m.bounds().max.x > 0.95f); // the handle's far side (1.0)
    // The handle is there and its hole stays open: a ray through the hole misses, one through the handle hits.
    BVH bvh;
    bvh.build(m);
    RayHit hit;
    CHECK(!bvh.raycast(m, glm::vec3(0.62f, 0.0f, -2.0f), glm::vec3(0, 0, 1), hit));
    CHECK(bvh.raycast(m, glm::vec3(0.93f, 0.0f, -2.0f), glm::vec3(0, 0, 1), hit));
    CHECK(bvh.raycast(m, glm::vec3(0.55f, 0.38f, -2.0f), glm::vec3(0, 0, 1), hit));
    // At least one handle (V - E + F <= 0). The crevice where the handle meets the body can be bridged by a
    // voxel, adding a tiny second tunnel, so genus 1 exactly isn't required.
    const long euler = long(m.vertexCount()) - long(m.triangleCount()) / 2;
    CHECK(euler <= 0 && euler >= -4);
}

TEST(sculptable_color_transfer) {
    // Red on +X, blue on -X, plus green junk inside the subject that must not bleed through.
    const SplatCloud c = sphereCapture(80000, 0, 4000);
    SplatSurfaceOptions o;
    o.resolution = 80;
    Mesh m;
    std::string err;
    SplatSurfaceStats st;
    CHECK(splatSurface(c, o, m, err, {}, &st));
    const SplatColorSampler sampler(c, 2.0f * st.cell, o.minOpacity);
    CHECK(colorFromSplats(m, sampler) == 0);
    const glm::vec3 red = srgbToLinear(glm::vec3(0.9f, 0.1f, 0.1f)), blue = srgbToLinear(glm::vec3(0.1f, 0.2f, 0.9f));
    double errRed = 0, errBlue = 0;
    size_t nr = 0, nb = 0;
    float maxGreen = 0.0f;
    for (size_t v = 0; v < m.vertexCount(); ++v) {
        const float x = m.positions[v].x;
        maxGreen = std::max(maxGreen, m.colors[v].y - std::max(m.colors[v].x, m.colors[v].z));
        if (x > 0.25f) {
            errRed += glm::length(m.colors[v] - red);
            ++nr;
        } else if (x < -0.25f) {
            errBlue += glm::length(m.colors[v] - blue);
            ++nb;
        }
    }
    CHECK(nr > 100 && nb > 100);
    CHECK(errRed / double(nr) < 0.03 && errBlue / double(nb) < 0.03);
    CHECK(maxGreen < 0.05f);
}

TEST(sculptable_face_landmarks) {
    FaceLandmarks truth;
    const Mesh head = makeTestHead(6, &truth);
    FaceLandmarks lm;
    std::string why;
    CHECK(detectFaceLandmarks(head, FaceFrame(), lm, why));
    if (!lm.valid) std::printf("  no face: %s\n", why.c_str());
    CHECK(lm.valid && lm.confidence > 0.3f);
    const float ied = glm::distance(eyeCentre(truth, 0), eyeCentre(truth, 1));
    // Each landmark within 15% of the eye distance of where it was designed (x, y as seen from the front).
    for (int i = 0; i < FaceLandmarks::kCount; ++i) {
        const float d = glm::length(glm::vec2(lm.point(i) - truth.point(i)));
        if (d > 0.15f * ied) std::printf("  landmark %s off by %.3f (eye distance %.3f)\n", FaceLandmarks::name(i), d, ied);
        CHECK(d < 0.15f * ied);
    }
    // A sphere is not a face.
    FaceLandmarks none;
    CHECK(!detectFaceLandmarks(makeIcosphere(5), FaceFrame(), none, why) && !why.empty());
    // The head looking along +X when told so.
    Mesh turned = head;
    for (glm::vec3& p : turned.positions) p = glm::vec3(p.z, p.y, -p.x);
    turned.recomputeAllNormals();
    FaceFrame side;
    side.front = glm::vec3(1, 0, 0);
    FaceLandmarks lt;
    CHECK(detectFaceLandmarks(turned, side, lt, why));
    CHECK(std::abs(lt.noseTip.y - truth.noseTip.y) < 0.15f * ied && lt.noseTip.x > 0.9f);
    // JSON round trip.
    FaceLandmarks back;
    CHECK(faceLandmarksFromJson(faceLandmarksJson(lm), back, why) && glm::distance(back.chin, lm.chin) < 1e-4f);
    CHECK(!faceLandmarksFromJson("{\"nose_tip\": [0, 0, 0]}", back, why));
}

TEST(sculptable_face_guides) {
    FaceLandmarks truth;
    const Mesh head = makeTestHead(6, &truth);
    BVH bvh;
    bvh.build(head);
    FaceLandmarks lm = truth;
    lm.eyeOuter[1].x += 0.03f; // a slightly lopsided placement is made symmetric
    symmetrizeFaceLandmarks(lm, FaceFrame(), head, bvh);
    CHECK(std::abs(lm.eyeOuter[0].x + lm.eyeOuter[1].x - 2.0f * lm.noseTip.x) < 1e-3f && std::abs(lm.noseTip.x - lm.chin.x) < 1e-3f);
    const std::vector<QRemeshGuide> guides = faceGuides(head, bvh, lm, FaceFrame(), 0.02f);
    size_t loops = 0;
    for (const QRemeshGuide& g : guides) loops += g.closed;
    CHECK(loops == 6); // eye + orbit on each side, lips, nasolabial loop
    const float ied = glm::distance(eyeCentre(lm, 0), eyeCentre(lm, 1));
    // A loop round each eye that encloses the eye's middle and both corners, and one round the mouth corners.
    for (int side = 0; side < 2; ++side) {
        const QRemeshGuide* eye = loopAround(guides, eyeCentre(lm, side), 0.1f * ied);
        CHECK(eye && encloses(*eye, eyeCentre(lm, side)) && encloses(*eye, lm.eyeOuter[size_t(side)]) &&
              encloses(*eye, lm.eyeInner[size_t(side)]));
    }
    const glm::vec3 mouth = 0.5f * (lm.mouthCorner[0] + lm.mouthCorner[1]);
    const QRemeshGuide* lips = loopAround(guides, mouth, 0.1f * ied);
    CHECK(lips && encloses(*lips, lm.mouthCorner[0]) && encloses(*lips, lm.mouthCorner[1]) && !encloses(*lips, lm.noseTip));
    // Symmetric: every guide's mirror image (x -> -x) is a guide too (or itself).
    for (const QRemeshGuide& g : guides) {
        float best = 1e9f;
        for (const QRemeshGuide& h : guides) {
            if (h.closed != g.closed) continue;
            double sum = 0;
            for (const glm::vec3& p : g.points) {
                float nearest = 1e9f;
                for (const glm::vec3& q : h.points) nearest = std::min(nearest, glm::distance(glm::vec3(-p.x, p.y, p.z), q));
                sum += nearest;
            }
            best = std::min(best, float(sum / double(g.points.size())));
        }
        CHECK(best < 0.02f * ied);
    }
    // Too coarse a density for the small eye rings: they are left out.
    size_t coarse = 0;
    for (const QRemeshGuide& g : faceGuides(head, bvh, lm, FaceFrame(), 0.12f)) coarse += g.closed;
    CHECK(coarse < loops);
}

TEST(sculptable_command_head) {
    Document d;
    CommandRegistry reg;
    CHECK(!reg.execute(d, "splat_to_mesh").ok); // nothing loaded
    CHECK(reg.execute(d, "splat_test_capture", {{"kind", std::string("head")}, {"count", 120000.0}}).ok);
    CHECK(d.splats.size() > 120000);
    const CommandResult r = reg.execute(d, "splat_to_mesh", {{"detail", 128.0}, {"target", 2500.0}});
    std::printf("  %s\n", r.message.c_str());
    CHECK(r.ok);
    CHECK(d.subtoolCount() == 3); // the starting sphere (hidden), the scan (hidden), the result
    CHECK(d.activeSubtool == 2 && d.activeSubtoolInfo().name == "Test Head");
    CHECK(!d.subtools[0].visible && !d.subtools[1].visible && d.subtools[1].name == "Test Head Scan");
    const TopologyReport t = topologyReport(d.mesh);
    CHECK(t.manifold() && t.boundaryEdges == 0 && t.quadFraction() > 0.97f);
    CHECK(t.faces > 1500 && t.faces < 4000);
    CHECK(d.mesh.hasColors());
    CHECK(d.qremeshGuides.size() >= 4); // the face was found and its loops used
    CHECK(r.message.find("face guides") != std::string::npos);
    // Skin-coloured where the cheek is, dark where the iris is.
    const glm::vec3 skin = srgbToLinear(glm::vec3(0.85f, 0.64f, 0.52f));
    size_t cheek = 0, good = 0;
    for (size_t v = 0; v < d.mesh.vertexCount(); ++v) {
        const glm::vec3 p = d.mesh.positions[v];
        if (glm::length(glm::vec2(p.x - 0.45f, p.y + 0.15f)) < 0.06f && p.z > 0.3f) {
            ++cheek;
            good += glm::length(d.mesh.colors[v] - skin) < 0.08f;
        }
    }
    CHECK(cheek > 0 && good * 10 >= cheek * 9);
}

TEST(sculptable_stages_and_landmarks) {
    Document d;
    CommandRegistry reg;
    CHECK(reg.execute(d, "splat_test_capture", {{"count", 100000.0}}).ok);
    const CommandResult scan = reg.execute(d, "splat_to_mesh", {{"detail", 112.0}, {"stage", std::string("scan")}, {"target", 2500.0}});
    CHECK(scan.ok && scan.message.find("landmarks {") != std::string::npos);
    CHECK(d.activeSubtoolInfo().name == "Test Head Scan" && d.mesh.hasColors() && !d.qremeshGuides.empty());
    // Nudge: face_guides with landmarks moved, then finish.
    const std::string json = scan.message.substr(scan.message.find('{'));
    FaceLandmarks lm;
    std::string err;
    CHECK(faceLandmarksFromJson(json, lm, err));
    lm.mouthCorner[0].x -= 0.03f;
    lm.mouthCorner[1].x += 0.03f;
    const CommandResult fg = reg.execute(d, "face_guides", {{"landmarks", faceLandmarksJson(lm)}, {"target", 2500.0}});
    CHECK(fg.ok && fg.message.find("loops") != std::string::npos);
    const size_t guides = d.qremeshGuides.size();
    CHECK(guides >= 4);
    const CommandResult fin = reg.execute(d, "splat_to_mesh", {{"stage", std::string("finish")}, {"target", 2500.0}});
    CHECK(fin.ok);
    CHECK(d.activeSubtoolInfo().name == "Test Head" && d.qremeshGuides.size() == guides);
    CHECK(!d.subtools[size_t(d.findSubtool("Test Head Scan"))].visible);
    CHECK(topologyReport(d.mesh).quadFraction() > 0.97f && d.mesh.hasColors());
    // Bad arguments.
    CHECK(!reg.execute(d, "splat_to_mesh", {{"preset", std::string("huge")}}).ok);
    CHECK(!reg.execute(d, "splat_to_mesh", {{"color", std::string("rainbow")}}).ok);
    CHECK(!reg.execute(d, "splat_to_mesh", {{"crop", std::string("1 2 3")}}).ok);
    CHECK(!reg.execute(d, "splat_to_mesh", {{"front", std::string("0 1 0")}}).ok); // parallel to up
}

TEST(sculptable_texture_and_levels) {
    Document d;
    CommandRegistry reg;
    CHECK(reg.execute(d, "splat_test_capture", {{"kind", std::string("object")}, {"count", 80000.0}, {"floaters", 6.0}}).ok);
    const CommandResult r = reg.execute(d, "splat_to_mesh", {{"detail", 96.0}, {"target", 800.0}, {"levels", 2.0},
                                                             {"color", std::string("texture")}, {"texture_size", 1024.0},
                                                             {"face_guides", std::string("off")}});
    std::printf("  %s\n", r.message.c_str());
    CHECK(r.ok);
    CHECK(subdivLevelCount(d.mesh) == 3);
    CHECK(hasTexture(d.mesh) && d.mesh.texture->width == 1024 && d.mesh.paintToTexture);
    CHECK(topologyReport(d.mesh).manifold());
    // The handle is orange in the texture: sample where a handle vertex is.
    size_t handle = 0, orange = 0;
    for (uint32_t t = 0; t < d.mesh.triangleCount(); t += 7) {
        const glm::vec3 p = d.mesh.positions[d.mesh.triangles[t].x];
        if (p.x < 0.85f || std::abs(p.y) > 0.2f) continue;
        ++handle;
        const glm::vec3 c = linearToSrgb(sampleTextureAt(d.mesh, t, p));
        orange += c.x > 0.75f && c.y > 0.35f && c.y < 0.75f && c.z < 0.3f;
    }
    std::printf("  handle samples %zu, orange %zu\n", handle, orange);
    CHECK(handle > 10 && orange * 10 >= handle * 8);
    // The top level follows the scan: no vertex further than 2 voxels from it.
    const Mesh& scan = d.subtoolMesh(d.findSubtool("Test Mug Scan"));
    BVH bvh;
    bvh.build(scan);
    std::vector<uint32_t> found;
    size_t away = 0;
    const float voxel = 2.4f / 96.0f;
    for (size_t v = 0; v < d.mesh.vertexCount(); v += 13) {
        bvh.queryVertices(scan, d.mesh.positions[v], 2.0f * voxel, found);
        away += found.empty();
    }
    CHECK(away * 100 < d.mesh.vertexCount() / 13);
}

TEST(sculptable_deterministic) {
    const SplatCloud c = makeTestCapture(makeTestHead(5), TestCaptureOptions{60000, 4, 2000, false, 3});
    SculptableOptions o;
    o.surface.resolution = 96;
    o.targetQuads = 1500;
    SculptableResult a, b;
    std::string err;
    CHECK(makeSculptableScan(c, o, a, err) && finishSculptable(c, o, a, err));
    ThreadPool::shared().setSerial(true);
    CHECK(makeSculptableScan(c, o, b, err) && finishSculptable(c, o, b, err));
    ThreadPool::shared().setSerial(false);
    CHECK(a.scan.positions == b.scan.positions && a.scan.colors == b.scan.colors);
    CHECK(a.mesh.positions == b.mesh.positions && a.mesh.triangles == b.mesh.triangles && a.mesh.colors == b.mesh.colors);
    CHECK(a.guides.size() == b.guides.size());
}
