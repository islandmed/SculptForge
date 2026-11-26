// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The PBR viewport's CPU side (core/EnvironmentMath.h): directions, SH9 irradiance against brute force, the split-sum
// BRDF table, GGX prefiltering, tone maps, the bundled environments, and the packed material planes the renderer
// uploads (pbrViewTexels).
#include "TestHarness.h"

#include "core/BakeImage.h"
#include "core/EnvironmentMath.h"
#include "core/Primitives.h"
#include "core/Texture.h"
#include "core/TextureChannels.h"

#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>

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

// --- Add HDRI...: the user's own files, the caps and the fuzz pass over the loader they go through ---

namespace {

std::string readAll(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(f), {});
}

std::string writeBytes(const char* name, const std::string& bytes) {
    const std::string path = tempPath(name);
    std::ofstream(path, std::ios::binary).write(bytes.data(), std::streamsize(bytes.size()));
    return path;
}

// What the environment list holds of the user's, undone after each test: the list is process-wide.
struct UserEnvironmentsReset {
    ~UserEnvironmentsReset() { setUserEnvironmentPaths({}); }
};

// Radiance header + flat scanlines of a W x H image (what the loader reads without run-length coding).
std::string flatHdr(int w, int h, const std::string& pixels = std::string()) {
    std::string out = "#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y " + std::to_string(h) + " +X " + std::to_string(w) + "\n";
    out += pixels.empty() ? std::string(size_t(w) * size_t(h) * 4, '\x80') : pixels;
    return out;
}

// A uncompressed scanline EXR header that claims w x h RGB float pixels; `data` bytes follow the header.
std::string exrClaiming(int w, int h, size_t data, int compression = 0) {
    auto u32 = [](uint32_t v) { return std::string(reinterpret_cast<const char*>(&v), 4); };
    auto attr = [&](const std::string& name, const std::string& type, const std::string& value) {
        return name + '\0' + type + '\0' + u32(uint32_t(value.size())) + value;
    };
    std::string chans;
    for (char c : std::string("BGR")) chans += std::string(1, c) + '\0' + u32(2) + u32(0) + u32(1) + u32(1);
    chans += '\0';
    std::string out = u32(20000630) + u32(2);
    out += attr("channels", "chlist", chans);
    out += attr("compression", "compression", std::string(1, char(compression)));
    out += attr("dataWindow", "box2i", u32(0) + u32(0) + u32(uint32_t(w - 1)) + u32(uint32_t(h - 1)));
    out += std::string(1, '\0');
    return out + std::string(data, '\0');
}

} // namespace

TEST(hdri_add_lists_remembers_and_removes) {
    UserEnvironmentsReset reset;
    const int base = environmentCount();
    std::string err;
    const std::string hdr = SF_TEST_DATA_DIR "/hdri/test_sky.hdr", exr = SF_TEST_DATA_DIR "/hdri/test_sky.exr";
    const int a = addEnvironmentFile(hdr, err);
    CHECK(a == base && environmentCount() == base + 1);
    CHECK(environments()[size_t(a)].user && environments()[size_t(a)].path == hdr && environments()[size_t(a)].label == "test_sky");
    CHECK(addEnvironmentFile(hdr, err) == a && environmentCount() == base + 1); // the same file: the same entry
    const int b = addEnvironmentFile(exr, err);
    CHECK(b == base + 1 && environments()[size_t(b)].label == "test_sky 2"); // (same stem: a name of its own)
    CHECK(findEnvironment("test_sky 2") == b && findEnvironment("TEST_SKY") == a);
    // Both load through the one reader and give lit, high-range images.
    for (int i : {a, b}) {
        EnvImage img;
        CHECK(loadEnvironment(i, img, err));
        CHECK(img.width == 64 && img.height == 32);
        float peak = 0.0f;
        for (const glm::vec3& t : img.texels) peak = std::max(peak, t.z);
        CHECK(peak > 5.0f);
    }
    EnvironmentLighting light;
    CHECK(buildEnvironmentLighting(a, light, err) && int(light.levels.size()) == kEnvLevels);
    // Saved as paths and restored: what hdri.json holds.
    const std::vector<std::string> saved = userEnvironmentPaths();
    CHECK(saved.size() == 2 && saved[0] == hdr && saved[1] == exr);
    const uint32_t rev = environmentsRevision();
    setUserEnvironmentPaths(saved);
    CHECK(environmentCount() == base + 2 && findEnvironment("test_sky 2") == base + 1 && environmentsRevision() != rev);
    // Remove: the later entry moves up, a built-in can't go.
    CHECK(!removeEnvironment(0) && !removeEnvironment(-1) && !removeEnvironment(environmentCount()));
    CHECK(removeEnvironment(a) && environmentCount() == base + 1 && environments()[size_t(base)].path == exr);
    CHECK(userEnvironmentPaths().size() == 1);
}

TEST(hdri_missing_file_stays_listed_and_never_throws) {
    UserEnvironmentsReset reset;
    const std::string gone = tempPath("sculptforge_gone_sky.hdr");
    std::filesystem::remove(gone);
    const int base = environmentCount();
    setUserEnvironmentPaths({gone, "", "notes.txt"}); // (a path from the preferences: not read when set; a non-HDR name is dropped)
    CHECK(environmentCount() == base + 1);
    CHECK(environmentFileMissing(base) && !environmentFileMissing(0) && !environmentFileMissing(2));
    EnvImage img;
    std::string err;
    CHECK(!loadEnvironment(base, img, err) && !err.empty());
    EnvironmentLighting light;
    CHECK(!buildEnvironmentLighting(base, light, err));
    std::string e2;
    CHECK(addEnvironmentFile(tempPath("sculptforge_never_there.hdr"), e2) == -1 && !e2.empty()); // (can't be added while it is not there)
    // The file comes back: it loads again, with no restart.
    const std::string path = writeBytes("sculptforge_gone_sky.hdr", flatHdr(32, 16));
    CHECK(!environmentFileMissing(base) && loadEnvironment(base, img, err) && img.width == 32);
    std::filesystem::remove(path);
}

TEST(hdri_caps_and_wrong_files_are_refused_from_the_header) {
    UserEnvironmentsReset reset;
    std::string err;
    int w = 0, h = 0;
    // Too wide or tall: refused before a pixel is decoded (the file holds no pixels at all).
    const std::string wide = writeBytes("sculptforge_wide.hdr", flatHdr(kMaxEnvironmentWidth + 1, 16, std::string(64, '\x80')));
    CHECK(!probeEnvironmentFile(wide, w, h, err) && err.find("smaller") != std::string::npos);
    EnvImage img;
    EnvironmentInfo wideInfo;
    wideInfo.path = wide;
    CHECK(!loadEnvironment(wideInfo, img, err) && !img.valid());
    CHECK(addEnvironmentFile(wide, err) == -1 && environmentCount() == int(environments().size()) && userEnvironmentPaths().empty());
    // At the limit passes the header check.
    const std::string edge = writeBytes("sculptforge_edge.hdr", flatHdr(kMaxEnvironmentWidth, kMaxEnvironmentHeight, std::string(16, '\x80')));
    CHECK(probeEnvironmentFile(edge, w, h, err) && w == kMaxEnvironmentWidth && h == kMaxEnvironmentHeight);
    // A 40-byte EXR that claims 65536 x 65536 floats (51 GB): refused, quickly, without allocating them.
    const auto t0 = std::chrono::steady_clock::now();
    const std::string huge = writeBytes("sculptforge_huge.exr", exrClaiming(65536, 65536, 64));
    CHECK(!probeEnvironmentFile(huge, w, h, err));
    int ch = 0;
    std::vector<float> px;
    CHECK(!loadExr(huge, w, h, ch, px, err) && px.empty());
    CHECK(!loadExr(writeBytes("sculptforge_big2.exr", exrClaiming(8192, 8192, 64)), w, h, ch, px, err, 1 << 20) && px.empty());
    CHECK(std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() < 2.0);
    // A hostile data window (-2^31 .. 2^31-1) can't overflow its way to a small size.
    {
        std::string e = exrClaiming(1, 1, 16);
        const size_t at = e.find("box2i");
        CHECK(at != std::string::npos);
        const int32_t win[4] = {INT32_MIN, 0, INT32_MAX, 0};
        std::memcpy(&e[at + 6 + 4], win, sizeof win);
        CHECK(!loadExr(writeBytes("sculptforge_win.exr", e), w, h, ch, px, err));
    }
    // A compressed EXR says so (the reader handles only uncompressed ones); other formats under an HDR name don't load.
    CHECK(!probeEnvironmentFile(writeBytes("sculptforge_zip.exr", exrClaiming(64, 32, 64, 3)), w, h, err) && err.find("compressed") != std::string::npos);
    const std::string png = writeBytes("sculptforge_notahdr.hdr", std::string("\x89PNG\r\n\x1a\n", 8) + std::string(64, '\0'));
    CHECK(!probeEnvironmentFile(png, w, h, err) && !err.empty());
    CHECK(!probeEnvironmentFile(writeBytes("sculptforge_pic.png", "x"), w, h, err) && err.find(".hdr") != std::string::npos);
    CHECK(!probeEnvironmentFile(writeBytes("sculptforge_empty.hdr", ""), w, h, err));
    for (const char* f : {"wide.hdr", "edge.hdr", "huge.exr", "big2.exr", "win.exr", "zip.exr", "notahdr.hdr", "pic.png", "empty.hdr"})
        std::filesystem::remove(tempPath((std::string("sculptforge_") + f).c_str()));
}

// Fuzz: the good files with random bytes changed, cut short, or grown, through the path a user-picked file takes
// (probe, then load). Each must fail with a message or load a bounded, valid image: never crash, never allocate
// beyond the caps, never hang. (Run under the sanitizers' CI job as well.)
TEST(hdri_loader_fuzz_damaged_files) {
    UserEnvironmentsReset reset;
    const std::string good[2] = {readAll(SF_TEST_DATA_DIR "/hdri/test_sky.hdr"), readAll(SF_TEST_DATA_DIR "/hdri/test_sky.exr")};
    CHECK(!good[0].empty() && !good[1].empty());
    std::mt19937 rng(20261126);
    int loaded = 0, refused = 0;
    const auto t0 = std::chrono::steady_clock::now();
    for (int round = 0; round < 3000; ++round) {
        const int which = round & 1;
        std::string bytes = good[which];
        const int kind = int(rng() % 4);
        if (kind == 0) { // flip a few bytes (the header's first 400 bytes more often: that is where the sizes are)
            for (int k = 0, n = 1 + int(rng() % 6); k < n; ++k) {
                const size_t span = (rng() & 1) ? std::min<size_t>(bytes.size(), 400) : bytes.size();
                bytes[rng() % span] = char(rng());
            }
        } else if (kind == 1) { // cut short
            bytes.resize(rng() % bytes.size());
        } else if (kind == 2) { // random bytes after a good start
            const size_t keep = rng() % std::min<size_t>(bytes.size(), 300);
            bytes.resize(keep);
            for (size_t k = 0, n = rng() % 200; k < n; ++k) bytes.push_back(char(rng()));
        } else { // a number in the header made huge
            const size_t at = rng() % std::min<size_t>(bytes.size(), 200);
            for (size_t k = 0; k < 4 && at + k < bytes.size(); ++k) bytes[at + k] = char(0xFF);
        }
        const std::string path = writeBytes(which ? "sculptforge_fuzz.exr" : "sculptforge_fuzz.hdr", bytes);
        EnvironmentInfo info;
        info.id = info.label = "fuzz";
        info.path = path;
        info.user = true;
        EnvImage img;
        std::string err;
        if (loadEnvironment(info, img, err)) {
            ++loaded;
            CHECK(img.valid() && img.width <= kMaxEnvironmentWidth && img.height <= kMaxEnvironmentHeight);
        } else {
            ++refused;
            CHECK(!err.empty());
        }
    }
    std::filesystem::remove(tempPath("sculptforge_fuzz.exr"));
    std::filesystem::remove(tempPath("sculptforge_fuzz.hdr"));
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::printf("  hdri fuzz: %d damaged files loaded, %d refused, %.1f s\n", loaded, refused, seconds);
    CHECK(refused > 500 && seconds < 60.0);
}

TEST(hdri_unusable_files_say_why) {
    UserEnvironmentsReset reset;
    const int base = environmentCount();
    // An EXR with a compression we don't read: listed (from the preferences), unusable with the compression named, refused by Add HDRI.
    const std::string piz = SF_TEST_DATA_DIR "/hdri/test_piz.exr", good = SF_TEST_DATA_DIR "/hdri/test_sky.exr";
    std::string err, why;
    CHECK(addEnvironmentFile(piz, err) == -1 && err.find("PIZ") != std::string::npos);
    CHECK(err.find(".hdr") != std::string::npos);
    setUserEnvironmentPaths({piz, good, tempPath("sculptforge_gone_x.hdr")});
    CHECK(environmentCount() == base + 3);
    CHECK(!environmentUsable(base, why) && why.find("PIZ") != std::string::npos && why.find(piz) == std::string::npos);
    CHECK(environmentUsable(base + 1, why));
    CHECK(!environmentUsable(base + 2, why) && why.find("missing") != std::string::npos);
    CHECK(environmentUsable(0, why) && environmentUsable(2, why)); // built-ins
    EnvImage img;
    CHECK(!loadEnvironment(base, img, err) && err.find("PIZ") != std::string::npos);
}
