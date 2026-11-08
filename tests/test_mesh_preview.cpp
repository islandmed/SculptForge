// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Generate 3D's candidate pictures (core/MeshPreview.h): a shaded thumbnail and a flat black silhouette on a light
// background, drawn on the CPU, framed to the mesh; and the outline comparison that spots a service sending back the
// same shape twice. The generator stub (ai/ImageTo3DService.h) answers after the AI switch, never before it.
#include "TestHarness.h"

#include "ai/AiPolicy.h"
#include "ai/ImageTo3DService.h"
#include "core/MeshPreview.h"
#include "core/Primitives.h"

using namespace sf;

namespace {

uint8_t red(const Image& img, int x, int y) { return img.rgba[(size_t(y) * size_t(img.width) + size_t(x)) * 4]; }

} // namespace

TEST(mesh_preview_silhouette_is_black_on_light) {
    const Image s = renderMeshPreview(makeIcosphere(3, 1.0f), 64, PreviewStyle::Silhouette);
    CHECK(s.width == 64 && s.height == 64 && s.rgba.size() == size_t(64 * 64 * 4));
    CHECK(red(s, 32, 32) == 0);  // the middle: the shape, flat black
    CHECK(red(s, 1, 1) > 200);   // a corner: the light background
    // Framed with a margin: a sphere's disc covers about pi/4 of 0.88^2 of the picture.
    const float cover = silhouetteCoverage(s);
    CHECK(cover > 0.5f && cover < 0.7f);
    // Any size of model frames the same.
    Mesh big = makeIcosphere(3, 1.0f);
    for (auto& p : big.positions) p = p * 40.0f + glm::vec3(100.0f, -50.0f, 7.0f);
    CHECK(silhouetteDifference(s, renderMeshPreview(big, 64, PreviewStyle::Silhouette)) < 0.02f);
}

TEST(mesh_preview_shaded_is_lit_and_turns) {
    const Mesh cube = makeCube(4, 2.0f);
    const Image front = renderMeshPreview(cube, 64, PreviewStyle::Shaded);
    CHECK(red(front, 32, 32) > 60);           // lit clay
    CHECK(red(front, 1, 1) < 60);             // dark background
    // Turned 45 degrees a cube is wider than tall in the picture: framing shrinks it, so it covers less height.
    const Image s0 = renderMeshPreview(cube, 64, PreviewStyle::Silhouette, 0.0f);
    const Image s45 = renderMeshPreview(cube, 64, PreviewStyle::Silhouette, 45.0f);
    CHECK(silhouetteCoverage(s45) < silhouetteCoverage(s0));
    // An empty mesh: the bare background, no crash.
    const Image empty = renderMeshPreview(Mesh{}, 32, PreviewStyle::Silhouette);
    CHECK(empty.width == 32 && silhouetteCoverage(empty) == 0.0f);
}

TEST(mesh_preview_tells_shapes_apart) {
    const Image sphere = renderMeshPreview(makeIcosphere(3, 1.0f), 96, PreviewStyle::Silhouette);
    const Image torus = renderMeshPreview(makeTorus(32, 2.0f), 96, PreviewStyle::Silhouette);
    const Image sphere2 = renderMeshPreview(makeIcosphere(4, 1.0f), 96, PreviewStyle::Silhouette);
    CHECK(silhouetteDifference(sphere, sphere2) < 0.02f); // the same form, finer: the same outline
    CHECK(silhouetteDifference(sphere, torus) > 0.1f);
    CHECK(silhouetteDifference(sphere, renderMeshPreview(makeIcosphere(3, 1.0f), 48, PreviewStyle::Silhouette)) == 1.0f);
    // Hidden faces don't draw.
    Mesh half = makeIcosphere(3, 1.0f);
    half.hidden.assign(half.triangles.size(), 0);
    for (size_t t = 0; t < half.triangles.size(); ++t)
        if (half.positions[half.triangles[t].x].x > 0.0f) half.hidden[t] = 1;
    CHECK(silhouetteCoverage(renderMeshPreview(half, 96, PreviewStyle::Silhouette)) < silhouetteCoverage(sphere) * 0.8f);
}

TEST(generator_stub_waits_behind_the_ai_switch) {
    int calls = 0;
    ai::setGeneratorStubForTests([&](int candidate, const std::string& stem) {
        ++calls;
        ai::GeneratorResult r;
        r.ok = true;
        r.outputPath = stem + "_" + std::to_string(candidate) + ".obj";
        return r;
    });
    ai::AiSettings s;
    s.generatorService = "fal"; // a paid service: the stub answers, nothing is sent
    ai::GeneratorResult r = ai::generate3DFromText(s, "a fox", "stem");
    CHECK(r.ok && r.outputPath == "stem_0.obj" && calls == 1);
    ai::GenerationProgress third; // the stub answers by candidate, not by arrival (they run on parallel threads)
    third.candidate = 2;
    r = ai::generate3D(s, "missing.png", "stem", &third);
    CHECK(r.ok && r.outputPath == "stem_2.obj" && calls == 2);
    ai::setAiEnabled(false); // AI off: refused before the stub, as before any service
    r = ai::generate3D(s, "missing.png", "stem");
    CHECK(!r.ok && calls == 2);
    ai::setAiEnabled(true);
    ai::setGeneratorStubForTests({});
}
