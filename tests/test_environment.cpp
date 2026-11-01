// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The PBR viewport's CPU side (core/EnvironmentMath.h): directions, SH9 irradiance against brute force, the split-sum
// BRDF table, GGX prefiltering, tone maps, the bundled environments, and the packed material planes the renderer
// uploads (pbrViewTexels).
#include "TestHarness.h"

#include "core/EnvironmentMath.h"
#include "core/Primitives.h"
#include "core/Texture.h"
#include "core/TextureChannels.h"

#include <cmath>

using namespace sf;

namespace {

EnvImage uniformEnv(glm::vec3 c, int w = 64) {
    EnvImage e;
    e.width = w;
    e.height = w / 2;
    e.texels.assign(size_t(e.width) * size_t(e.height), c);
    return e;
}

// Bright sky above, dark ground below, and a hot spot: something with real low-frequency structure.
EnvImage skyEnv(int w = 128) {
    EnvImage e;
    e.width = w;
    e.height = w / 2;
    e.texels.resize(size_t(e.width) * size_t(e.height));
    for (int y = 0; y < e.height; ++y)
        for (int x = 0; x < e.width; ++x) {
            const glm::vec3 d = equirectDirection((x + 0.5f) / e.width, (y + 0.5f) / e.height);
            glm::vec3 c = d.y > 0 ? glm::vec3(0.4f, 0.6f, 1.0f) * (0.5f + d.y) : glm::vec3(0.15f, 0.12f, 0.1f);
            c += glm::vec3(3.0f) * std::pow(std::max(0.0f, glm::dot(d, glm::normalize(glm::vec3(0.5f, 0.6f, -0.6f)))), 8.0f);
            e.texels[size_t(y) * size_t(e.width) + size_t(x)] = c;
        }
    return e;
}

} // namespace

TEST(environment_directions_round_trip) {
    for (float u : {0.1f, 0.37f, 0.5f, 0.75f, 0.93f})
        for (float v : {0.05f, 0.3f, 0.5f, 0.8f}) {
            const glm::vec3 d = equirectDirection(u, v);
            CHECK(std::abs(glm::length(d) - 1.0f) < 1e-5f);
            const glm::vec2 uv = equirectCoords(d);
            CHECK(std::abs(uv.x - u) < 1e-4f && std::abs(uv.y - v) < 1e-4f);
        }
    // u = 0.5 looks down -Z, the top row is +Y, u = 0.75 is +X.
    CHECK(glm::distance(equirectDirection(0.5f, 0.5f), glm::vec3(0, 0, -1)) < 1e-5f);
    CHECK(equirectDirection(0.3f, 0.0f).y > 0.999f);
    CHECK(glm::distance(equirectDirection(0.75f, 0.5f), glm::vec3(1, 0, 0)) < 1e-5f);
}

TEST(environment_sh9_matches_brute_force_irradiance) {
    // A uniform environment of radiance L lights a white diffuse surface to L from every side.
    const SH9 flat = irradianceSH9(uniformEnv(glm::vec3(0.7f, 1.0f, 2.0f)));
    for (const glm::vec3& n : {glm::vec3(0, 1, 0), glm::vec3(1, 0, 0), glm::normalize(glm::vec3(-1, -2, 0.5f))}) {
        const glm::vec3 e = flat.eval(n);
        CHECK(std::abs(e.x - 0.7f) < 0.01f && std::abs(e.y - 1.0f) < 0.01f && std::abs(e.z - 2.0f) < 0.02f);
    }
    // With structure, nine coefficients stay within a few percent of the exact cosine integral (the known bound
    // for the order-2 approximation of the clamped cosine).
    const EnvImage sky = skyEnv();
    const SH9 sh = irradianceSH9(sky);
    for (const glm::vec3& n : {glm::vec3(0, 1, 0), glm::vec3(0, -1, 0), glm::normalize(glm::vec3(0.5f, 0.6f, -0.6f)),
                               glm::normalize(glm::vec3(-1, 0.2f, 0.3f)), glm::vec3(0, 0, 1)}) {
        const glm::vec3 exact = irradianceBruteForce(sky, n), approx = sh.eval(n);
        const float scale = std::max(glm::length(exact), 0.05f);
        CHECK(glm::length(exact - approx) / scale < 0.1f);
    }
}

TEST(environment_brdf_table_is_the_split_sum) {
    // Smooth and head-on, a mirror reflects all of F0: scale ~1, bias ~0.
    const glm::vec2 mirror = integrateBrdf(1.0f, 0.0f);
    CHECK(mirror.x > 0.95f && mirror.y < 0.02f);
    // Grazing on a smooth surface, Fresnel takes over: the bias grows.
    CHECK(integrateBrdf(0.05f, 0.05f).y > integrateBrdf(0.9f, 0.05f).y);
    // Directional albedo never exceeds 1, and roughness only loses energy (single scattering).
    for (float r : {0.1f, 0.4f, 0.7f, 1.0f})
        for (float nv : {0.1f, 0.5f, 1.0f}) {
            const glm::vec2 ab = integrateBrdf(nv, r);
            CHECK(ab.x >= 0.0f && ab.y >= 0.0f && ab.x + ab.y <= 1.001f);
        }
    CHECK(integrateBrdf(0.8f, 1.0f).x + integrateBrdf(0.8f, 1.0f).y < integrateBrdf(0.8f, 0.2f).x + integrateBrdf(0.8f, 0.2f).y);
    const std::vector<glm::vec2> lut = brdfLut(16);
    CHECK(lut.size() == 256);
    // Row 0 is smooth, the last column head-on.
    CHECK(lut[15].x > 0.9f);
}

TEST(environment_prefilter_keeps_energy_and_blurs) {
    // A uniform environment stays uniform at every roughness (the lobe is normalised).
    const std::vector<EnvImage> flat = prefilterEnvironment(uniformEnv(glm::vec3(1.5f), 128), 64);
    CHECK(int(flat.size()) == kEnvLevels);
    for (size_t k = 0; k < flat.size(); ++k) {
        CHECK(flat[k].width == std::max(4, kEnvBaseWidth >> k));
        for (const glm::vec3& t : flat[k].texels) CHECK(std::abs(t.x - 1.5f) < 0.02f);
    }
    // A small bright spot spreads out as roughness grows: the peak drops level by level.
    EnvImage spot = uniformEnv(glm::vec3(0.0f), 256);
    for (int y = 60; y < 68; ++y)
        for (int x = 124; x < 132; ++x) spot.texels[size_t(y) * 256 + size_t(x)] = glm::vec3(100.0f);
    const std::vector<EnvImage> blurred = prefilterEnvironment(spot, 64);
    float lastPeak = 1e30f;
    for (size_t k = 1; k < blurred.size(); ++k) {
        float peak = 0.0f;
        for (const glm::vec3& t : blurred[k].texels) peak = std::max(peak, t.x);
        CHECK(peak < lastPeak);
        lastPeak = peak;
    }
}

TEST(environment_tone_maps_are_monotonic_and_bounded) {
    for (int t = 0; t < kToneMapCount; ++t) {
        float last = -1.0f;
        for (float x = 0.0f; x < 40.0f; x = x * 1.3f + 0.01f) {
            const float y = toneMap(ToneMap(t), glm::vec3(x)).y;
            CHECK(y >= last - 1e-5f && y >= 0.0f && y <= 1.0f);
            last = y;
        }
        ToneMap parsed;
        CHECK(parseToneMap(toneMapId(ToneMap(t)), parsed) && parsed == ToneMap(t));
    }
    // Mid grey lands near the middle of the display range for the filmic curves.
    CHECK(toneMap(ToneMap::Aces, glm::vec3(0.18f)).y > 0.1f && toneMap(ToneMap::Aces, glm::vec3(0.18f)).y < 0.3f);
    CHECK(toneMap(ToneMap::AgX, glm::vec3(0.18f)).y > 0.1f && toneMap(ToneMap::AgX, glm::vec3(0.18f)).y < 0.35f);
}

TEST(environment_builtins_load_and_light) {
    CHECK(environmentCount() >= 4);
    CHECK(findEnvironment("studio") == 0 && findEnvironment("Outdoor Sky") == 3 && findEnvironment("2") == 2);
    CHECK(findEnvironment("nope") == -1);
    for (int i = 0; i < 4; ++i) {
        EnvImage e;
        std::string err;
        CHECK(loadEnvironment(i, e, err));
        CHECK(e.valid() && e.width == 2 * e.height);
        float peak = 0.0f;
        for (const glm::vec3& t : e.texels) peak = std::max(peak, std::max(t.x, std::max(t.y, t.z)));
        CHECK(peak > 1.0f); // high dynamic range: the light sources are brighter than white
    }
    EnvironmentLighting light;
    std::string err;
    for (int i = 3; i >= 0; --i) CHECK(buildEnvironmentLighting(i, light, err));
    CHECK(int(light.levels.size()) == kEnvLevels && light.levels[0].width == kEnvBaseWidth);
    // Lit from above more than from below (the generated studio).
    CHECK(glm::length(light.sh.eval(glm::vec3(0, 1, 0))) > glm::length(light.sh.eval(glm::vec3(0, -1, 0))));
    std::printf("  environment lighting built in %.0f ms\n", light.seconds * 1000.0);
}

TEST(pbr_view_packs_the_material_channels) {
    Mesh m = makePlane(8);
    m.uvs.resize(m.triangleCount() * 3);
    for (size_t t = 0; t < m.triangleCount(); ++t)
        for (int c = 0; c < 3; ++c) {
            const glm::vec3 p = m.positions[m.triangles[t][c]];
            m.uvs[t * 3 + size_t(c)] = glm::vec2(p.x * 0.5f + 0.5f, p.z * 0.5f + 0.5f);
        }
    m.texture = makeTexture(128, 128);
    PaintTexture& t = *m.texture;
    CHECK(pbrViewPlanes(t) == 0u);
    setChannelEnabled(t, Channel::Roughness, true);
    setChannelEnabled(t, Channel::Metallic, true);
    CHECK(pbrViewPlanes(t) == 1u);
    setChannelEnabled(t, Channel::Height, true);
    setChannelEnabled(t, Channel::Emissive, true);
    CHECK(pbrViewPlanes(t) == 7u);
    // Paint a metallic square and a height bump.
    for (int y = 40; y < 60; ++y)
        for (int x = 40; x < 60; ++x) {
            const float one = 1.0f, high = 0.9f;
            t.channels[size_t(Channel::Metallic)].base.setValues(x, y, &one);
            t.channels[size_t(Channel::Height)].base.setValues(x, y, &high);
        }
    std::vector<uint8_t> orm(32 * 32 * 4), nrm(32 * 32 * 4), em(32 * 32 * 4);
    pbrViewTexels(t, 32, 32, 64, 64, orm.data(), nrm.data(), em.data());
    auto at = [&](std::vector<uint8_t>& v, int x, int y, int c) { return int(v[(size_t(y - 32) * 32 + size_t(x - 32)) * 4 + size_t(c)]); };
    CHECK(at(orm, 50, 50, 2) == 255);                                  // metallic inside the square
    CHECK(at(orm, 35, 35, 2) == 0);                                    // and not outside
    CHECK(std::abs(at(orm, 35, 35, 1) - int(std::lround(channelFormat(Channel::Roughness).defaultValue[0] * 255))) <= 1);
    CHECK(at(orm, 35, 35, 0) == 255);                                  // AO off: unoccluded
    CHECK(at(nrm, 50, 50, 2) > 250);                                   // flat on top of the bump
    CHECK(at(nrm, 40, 50, 0) < 120);                                   // its left edge leans to -X
    CHECK(at(nrm, 59, 50, 0) > 136);                                   // its right edge to +X
    CHECK(at(em, 50, 50, 0) == 0 && at(em, 50, 50, 3) == 255);
}
