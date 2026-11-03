// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Map baking (core/Bake.h): a finely subdivided sphere with a bump, a dent and a groove baked onto a low
// sphere. The tangent-space normal map decodes (with the same MikkTSpace frame a game engine uses) to the
// high-poly's normals, displacement has the right sign and scale in the float data, the 16-bit PNG and the
// EXR, AO is darker in the groove, UV islands are padded, and the bake command writes its files. Also the
// 16-bit PNG / EXR writers, MikkTSpace on a plane, and the time of a 4K bake from a million triangles.
#include "TestHarness.h"

#include "core/AutoUv.h"
#include "core/Bake.h"
#include "core/BakeImage.h"
#include "core/Commands.h"
#include "core/Primitives.h"
#include "core/Tangents.h"
#include "core/Texture.h"
#include "link/UnrealLink.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>

using namespace sf;

namespace {

const glm::vec3 kBumpDir = glm::normalize(glm::vec3(0.0f, 0.0f, 1.0f));
const glm::vec3 kDentDir = glm::normalize(glm::vec3(0.0f, 0.0f, -1.0f));
constexpr float kBump = 0.15f, kDent = 0.10f, kGrooveY = 0.6f;

// Radius of the high-poly in direction `d` (unit): a sphere with a Gaussian bump towards +Z, a dent towards -Z
// and a narrow groove around the latitude y = 0.6.
float highRadius(const glm::vec3& d) {
    const float aBump = std::acos(std::clamp(glm::dot(d, kBumpDir), -1.0f, 1.0f));
    const float aDent = std::acos(std::clamp(glm::dot(d, kDentDir), -1.0f, 1.0f));
    return 1.0f + kBump * std::exp(-std::pow(aBump / 0.35f, 2.0f)) - kDent * std::exp(-std::pow(aDent / 0.35f, 2.0f)) -
           0.06f * std::exp(-std::pow((d.y - kGrooveY) / 0.03f, 2.0f));
}

Mesh highPoly(int level) {
    Mesh m = makeIcosphere(level, 1.0f);
    for (glm::vec3& p : m.positions) p = glm::normalize(p) * highRadius(glm::normalize(p));
    m.recomputeAllNormals();
    return m;
}

Mesh lowPoly() {
    Mesh m = makeIcosphere(3, 1.0f);
    AutoUvResult uv;
    std::string err;
    computeAutoUv(m.positions, m.triangles, 512, uv, err);
    m.uvs = uv.uvs;
    return m;
}

// Texel -> (low triangle, barycentrics) for the island texels of a w x w map.
struct TexelMap {
    std::vector<int32_t> tri;
    std::vector<glm::vec3> bary;
};
TexelMap texelMap(const Mesh& low, int w) {
    TexelMap tm;
    tm.tri.assign(size_t(w) * size_t(w), -1);
    tm.bary.resize(tm.tri.size());
    for (uint32_t t = 0; t < low.triangleCount(); ++t)
        rasterizeUvTriangle(low.uvs[t * 3], low.uvs[t * 3 + 1], low.uvs[t * 3 + 2], w, w, [&](int x, int y, float b0, float b1, float b2) {
            tm.tri[size_t(y) * size_t(w) + size_t(x)] = int32_t(t);
            tm.bary[size_t(y) * size_t(w) + size_t(x)] = glm::vec3(b0, b1, b2);
        });
    return tm;
}

glm::vec3 lowPointOf(const Mesh& low, uint32_t t, const glm::vec3& b, glm::vec3* normal = nullptr) {
    const glm::uvec3& tv = low.triangles[t];
    if (normal) *normal = glm::normalize(low.normals[tv.x] * b.x + low.normals[tv.y] * b.y + low.normals[tv.z] * b.z);
    return low.positions[tv.x] * b.x + low.positions[tv.y] * b.y + low.positions[tv.z] * b.z;
}

// The analytic high-poly normal in direction d (finite differences of the radius function).
glm::vec3 highNormal(const glm::vec3& d) {
    glm::vec3 t, b;
    t = glm::normalize(glm::cross(std::abs(d.y) < 0.9f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0), d));
    b = glm::cross(d, t);
    const float h = 1e-3f;
    auto P = [&](const glm::vec3& dir) { return glm::normalize(dir) * highRadius(glm::normalize(dir)); };
    const glm::vec3 du = P(d + t * h) - P(d - t * h), dv = P(d + b * h) - P(d - b * h);
    glm::vec3 n = glm::normalize(glm::cross(du, dv));
    return glm::dot(n, d) < 0 ? -n : n;
}

float angleDeg(const glm::vec3& a, const glm::vec3& b) {
    return std::acos(std::clamp(glm::dot(glm::normalize(a), glm::normalize(b)), -1.0f, 1.0f)) * 57.29578f;
}

} // namespace

TEST(bake_mikktspace_plane) {
    Mesh m = makePlane(4, 2.0f); // in the XZ plane, normal +Y
    m.uvs.resize(m.triangleCount() * 3);
    for (size_t t = 0; t < m.triangleCount(); ++t)
        for (int c = 0; c < 3; ++c) {
            const glm::vec3 p = m.positions[m.triangles[t][c]];
            m.uvs[t * 3 + size_t(c)] = glm::vec2(p.x * 0.5f + 0.5f, p.z * 0.5f + 0.5f);
        }
    const auto tan = computeTangents(m);
    CHECK(tan.size() == m.triangleCount() * 3);
    bool ok = true;
    for (size_t i = 0; i < tan.size(); ++i) {
        // u grows along +X; v along +Z, and cross(N=+Y, T=+X) = -Z, so the sign is -1.
        ok = ok && glm::length(glm::vec3(tan[i]) - glm::vec3(1, 0, 0)) < 1e-4f && tan[i].w == -1.0f;
    }
    CHECK(ok);
    Mesh noUv = makePlane(2);
    CHECK(computeTangents(noUv).empty());
}

TEST(bake_png16_and_exr_round_trip) {
    const int w = 37, h = 11;
    std::vector<uint16_t> px(size_t(w) * h);
    std::vector<float> f(size_t(w) * h * 3);
    for (size_t i = 0; i < px.size(); ++i) {
        px[i] = uint16_t((i * 2654435761u) >> 16);
        for (int c = 0; c < 3; ++c) f[i * 3 + size_t(c)] = float(i) * 0.25f - 30.0f + float(c) * 1e-3f;
    }
    std::string err;
    const std::string png = tempPath("sf_bake16.png"), exr = tempPath("sf_bake.exr");
    CHECK(savePng16(png, w, h, 1, px, err));
    int rw = 0, rh = 0, rc = 0;
    std::vector<uint16_t> back;
    CHECK(loadPng16(png, rw, rh, rc, back, err));
    CHECK(rw == w && rh == h && rc == 1 && back == px);
    CHECK(saveExr(exr, w, h, 3, f, err));
    std::vector<float> fb;
    CHECK(loadExr(exr, rw, rh, rc, fb, err));
    CHECK(rw == w && rh == h && rc == 3 && fb == f);
    std::filesystem::remove(png);
    std::filesystem::remove(exr);
}

TEST(bake_bumpy_sphere_maps) {
    const Mesh high = highPoly(6); // 82k triangles
    const Mesh low = lowPoly();
    CHECK(low.hasUvs());
    BakeSettings st;
    st.size = 256;
    st.maps = bakeBit(BakeMap::Normal) | bakeBit(BakeMap::ObjectNormal) | bakeBit(BakeMap::Displacement) |
              bakeBit(BakeMap::AmbientOcclusion) | bakeBit(BakeMap::Cavity);
    st.aoRays = 64;
    st.aoDistance = 0.1f;
    BakeResult r;
    std::string err;
    CHECK(bakeMaps(low, {&high}, st, r, err));
    std::printf("  %s\n", bakeSummary(r).c_str());
    CHECK(r.width == 256 && r.normal.size() == 256 * 256 * 3 && r.displacement.size() == 256 * 256);
    CHECK(r.missedTexels * 100 < r.islandTexels); // auto distances catch (almost) everything
    // Auto range covers the bump (outside) and the dent (inside).
    CHECK(r.front > kBump * 0.9f && r.rear > kDent * 0.9f);

    const TexelMap tm = texelMap(low, r.width);
    double worstFlat = 0, sumErr = 0;
    int n = 0, bumpTexels = 0, dentTexels = 0;
    float bumpTop = -1, dentBottom = 1;
    float aoGroove = 0, aoFlat = 0, cavGroove = 0, cavFlat = 0;
    int nGroove = 0, nFlat = 0;
    for (size_t i = 0; i < tm.tri.size(); ++i) {
        if (tm.tri[i] < 0 || r.coverage[i] != 1) continue;
        const int x = int(i % size_t(r.width)), y = int(i / size_t(r.width));
        // Skip texels on island borders: their value mixes samples of neighbouring triangles.
        bool interior = x > 0 && y > 0 && x + 1 < r.width && y + 1 < r.height;
        for (int dy = -1; dy <= 1 && interior; ++dy)
            for (int dx = -1; dx <= 1 && interior; ++dx) interior = tm.tri[size_t(y + dy) * size_t(r.width) + size_t(x + dx)] >= 0;
        if (!interior) continue;
        const uint32_t t = uint32_t(tm.tri[i]);
        glm::vec3 nLow;
        const glm::vec3 p = lowPointOf(low, t, tm.bary[i], &nLow);
        const glm::vec3 dir = glm::normalize(p);
        // Decode the tangent-space normal the way a shader does.
        const glm::vec3 b = tm.bary[i];
        const glm::vec4 &t0 = r.tangents[t * 3], &t1 = r.tangents[t * 3 + 1], &t2 = r.tangents[t * 3 + 2];
        glm::vec3 T = glm::vec3(t0) * b.x + glm::vec3(t1) * b.y + glm::vec3(t2) * b.z;
        T = glm::normalize(T - nLow * glm::dot(nLow, T));
        const glm::vec3 B = t0.w * glm::cross(nLow, T);
        const glm::vec3 tn = glm::vec3(r.normal[i * 3], r.normal[i * 3 + 1], r.normal[i * 3 + 2]) / 127.5f - 1.0f;
        const glm::vec3 world = glm::normalize(tn.x * T + tn.y * B + tn.z * nLow);
        // Where the low-poly's ray meets the high-poly: about the same direction from the centre.
        const float e = angleDeg(world, highNormal(dir));
        const float bumpA = std::acos(glm::dot(dir, kBumpDir)), dentA = std::acos(glm::dot(dir, kDentDir));
        if (std::abs(dir.y - kGrooveY) > 0.12f) { // the groove is narrower than a texel: leave it out
            sumErr += e;
            ++n;
            worstFlat = std::max(worstFlat, double(e));
        }
        const float d = r.displacement[i];
        if (bumpA < 0.05f) bumpTop = std::max(bumpTop, d), ++bumpTexels;
        if (dentA < 0.05f) dentBottom = std::min(dentBottom, d), ++dentTexels;
        // The bump's flank tilts the normal away from the bump's centre: check the decoded direction.
        if (bumpA > 0.25f && bumpA < 0.35f) {
            const glm::vec3 away = glm::normalize(dir - kBumpDir * glm::dot(dir, kBumpDir));
            CHECK(glm::dot(world - nLow, away) > 0.05f);
        }
        if (std::abs(dir.y - kGrooveY) < 0.02f) aoGroove += r.ao[i] / 255.0f, cavGroove += r.cavity[i] / 255.0f, ++nGroove;
        if (std::abs(dir.y - 0.0f) < 0.05f && bumpA > 0.8f && dentA > 0.8f)
            aoFlat += r.ao[i] / 255.0f, cavFlat += r.cavity[i] / 255.0f, ++nFlat;
    }
    const double meanErr = sumErr / std::max(n, 1);
    std::printf("  normal map: mean error %.2f deg, worst %.2f deg over %d texels\n", meanErr, worstFlat, n);
    CHECK(n > 1000);
    CHECK(meanErr < 2.0);
    CHECK(worstFlat < 12.0);
    // Displacement: + at the bump (by about its height), - at the dent.
    CHECK(bumpTexels > 0 && dentTexels > 0);
    std::printf("  displacement: bump %.4f (expect ~%.3f), dent %.4f (expect ~-%.3f), scale %.4f\n", bumpTop, kBump,
                dentBottom, kDent, r.displacementScale);
    CHECK(std::abs(bumpTop - kBump) < 0.02f);
    CHECK(std::abs(dentBottom + kDent) < 0.02f);
    CHECK(std::abs(r.displacementScale - std::max(std::abs(r.displacementMin), r.displacementMax)) < 1e-6f);
    // AO: darker in the groove than on the open sphere.
    CHECK(nGroove > 0 && nFlat > 0);
    aoGroove /= float(std::max(nGroove, 1));
    aoFlat /= float(std::max(nFlat, 1));
    std::printf("  AO: groove %.3f, open surface %.3f\n", aoGroove, aoFlat);
    CHECK(aoFlat > 0.95f);
    CHECK(aoGroove < aoFlat - 0.1f);
    // Cavity: mid-grey on the open sphere (its overall curvature isn't detail), dark in the groove.
    cavGroove /= float(std::max(nGroove, 1));
    cavFlat /= float(std::max(nFlat, 1));
    std::printf("  cavity: groove %.3f, open surface %.3f\n", cavGroove, cavFlat);
    CHECK(std::abs(cavFlat - 0.5f) < 0.1f);
    CHECK(cavGroove < cavFlat - 0.15f);

    // Padding: texels beyond the islands got filled with their neighbours' values.
    size_t padded = 0, adjacent = 0, paddedOk = 0;
    for (size_t i = 0; i < r.coverage.size(); ++i) {
        if (r.coverage[i] != 2) continue;
        ++padded;
        const int x = int(i % size_t(r.width)), y = int(i / size_t(r.width));
        bool touches = false, ok = false;
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx) {
                const int nx = x + dx, ny = y + dy;
                if (nx < 0 || ny < 0 || nx >= r.width || ny >= r.height) continue;
                const size_t j = size_t(ny) * size_t(r.width) + size_t(nx);
                if (r.coverage[j] != 1) continue;
                touches = true;
                const float d = std::abs(r.displacement[i] - r.displacement[j]);
                ok = ok || (d < 0.02f && std::abs(int(r.normal[i * 3 + 2]) - int(r.normal[j * 3 + 2])) < 24);
            }
        adjacent += touches;
        paddedOk += touches && ok;
    }
    std::printf("  padding: %zu texels, %zu next to an island, %zu of those continue it\n", padded, adjacent, paddedOk);
    CHECK(padded > adjacent && adjacent > 0);
    CHECK(paddedOk > adjacent * 95 / 100); // padding next to an island continues it
    // No padding: nothing beyond the islands, and the empty texels are a flat normal.
    BakeSettings st0 = st;
    st0.padding = 0;
    st0.maps = bakeBit(BakeMap::Normal);
    BakeResult r0;
    CHECK(bakeMaps(low, {&high}, st0, r0, err));
    size_t c2 = 0;
    for (uint8_t c : r0.coverage) c2 += c == 2;
    CHECK(c2 == 0);
    bool flat = true;
    for (size_t i = 0; i < r0.coverage.size(); ++i)
        if (!r0.coverage[i]) flat = flat && r0.normal[i * 3] == 128 && r0.normal[i * 3 + 1] == 128 && r0.normal[i * 3 + 2] == 255;
    CHECK(flat);
    // A wider padding reaches further.
    BakeSettings st16 = st0;
    st16.padding = 16;
    BakeResult r16;
    CHECK(bakeMaps(low, {&high}, st16, r16, err));
    size_t p16 = 0;
    for (uint8_t c : r16.coverage) p16 += c == 2;
    CHECK(p16 > padded);

    // DirectX convention flips green only.
    BakeSettings dx = st0;
    dx.flipGreen = true;
    BakeResult rdx;
    CHECK(bakeMaps(low, {&high}, dx, rdx, err));
    bool flipped = true;
    for (size_t i = 0; i < rdx.coverage.size(); ++i)
        if (rdx.coverage[i] == 1)
            flipped = flipped && rdx.normal[i * 3] == r0.normal[i * 3] && std::abs(int(rdx.normal[i * 3 + 1]) - (255 - int(r0.normal[i * 3 + 1]))) <= 1;
    CHECK(flipped);

    // Files: the 16-bit PNG decodes to the float displacement (mid-grey = 0), the EXR holds it exactly.
    const std::string dir = tempPath("sf_bake_maps");
    std::filesystem::remove_all(dir);
    std::vector<std::string> written;
    CHECK(saveBakeMaps(r, dir, "Low", true, written, err));
    CHECK(written.size() == 6); // normal, object normal, displacement png + exr, ao, cavity
    int w = 0, h = 0, c = 0;
    std::vector<uint16_t> d16;
    CHECK(loadPng16(dir + "/Low_displacement.png", w, h, c, d16, err));
    std::vector<float> dexr;
    int ew = 0, eh = 0, ec = 0;
    CHECK(loadExr(dir + "/Low_displacement.exr", ew, eh, ec, dexr, err));
    CHECK(w == 256 && h == 256 && c == 1 && ew == 256 && ec == 1);
    double worst16 = 0, worstExr = 0;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const float want = r.displacement[size_t(h - 1 - y) * size_t(w) + size_t(x)]; // files are top-down
            const float got = (float(d16[size_t(y) * size_t(w) + size_t(x)]) / 65535.0f - 0.5f) * 2.0f * r.displacementScale;
            worst16 = std::max(worst16, double(std::abs(got - want)));
            worstExr = std::max(worstExr, double(std::abs(dexr[size_t(y) * size_t(w) + size_t(x)] - want)));
        }
    CHECK(worst16 < r.displacementScale * 1e-4);
    CHECK(worstExr == 0.0);
    Image nm;
    CHECK(Image::load(dir + "/Low_normal.png", nm, err));
    CHECK(nm.width == 256);
    std::filesystem::remove_all(dir);
}

TEST(bake_errors) {
    const Mesh high = highPoly(3);
    Mesh noUv = makeIcosphere(2);
    BakeSettings st;
    st.size = 128;
    BakeResult r;
    std::string err;
    CHECK(!bakeMaps(noUv, {&high}, st, r, err));
    CHECK(err.find("UVs") != std::string::npos);
    const Mesh low = lowPoly();
    CHECK(!bakeMaps(low, {}, st, r, err));
    CHECK(!bakeMaps(low, {&low}, st, r, err)); // a mesh can't be its own source
    st.maps = bakeBit(BakeMap::Color);
    CHECK(!bakeMaps(low, {&high}, st, r, err)); // nothing to transfer
    uint32_t maps = 0;
    CHECK(parseBakeMaps("normal, AO,disp", maps, err));
    CHECK(maps == (bakeBit(BakeMap::Normal) | bakeBit(BakeMap::AmbientOcclusion) | bakeBit(BakeMap::Displacement)));
    CHECK(parseBakeMaps("all", maps, err) && maps == (1u << kBakeMapCount) - 1u);
    CHECK(!parseBakeMaps("normal,specular", maps, err));
    // Cancelling stops it.
    st.maps = bakeBit(BakeMap::Normal);
    CHECK(!bakeMaps(low, {&high}, st, r, err, [](float) { return false; }));
    CHECK(err == "Cancelled");
}

TEST(bake_color_transfer) {
    Mesh high = highPoly(4);
    high.ensureColors();
    for (size_t v = 0; v < high.vertexCount(); ++v) high.colors[v] = high.positions[v].x > 0 ? glm::vec3(1, 0, 0) : glm::vec3(0, 0, 1);
    const Mesh low = lowPoly();
    BakeSettings st;
    st.size = 128;
    st.maps = bakeBit(BakeMap::Color);
    BakeResult r;
    std::string err;
    CHECK(bakeMaps(low, {&high}, st, r, err));
    const TexelMap tm = texelMap(low, 128);
    int good = 0, total = 0;
    for (size_t i = 0; i < tm.tri.size(); ++i) {
        if (tm.tri[i] < 0) continue;
        const glm::vec3 p = lowPointOf(low, uint32_t(tm.tri[i]), tm.bary[i]);
        if (std::abs(p.x) < 0.2f) continue;
        ++total;
        const bool red = r.color[i * 3] > 200 && r.color[i * 3 + 2] < 50, blue = r.color[i * 3 + 2] > 200 && r.color[i * 3] < 50;
        good += p.x > 0 ? red : blue;
    }
    CHECK(total > 100 && good > total * 95 / 100);
}

TEST(bake_command) {
    Document doc = makeDoc(highPoly(4));
    doc.subtools[0].name = "High";
    doc.addSubtool(lowPoly(), "Low");
    CommandRegistry reg;
    const std::string dir = tempPath("sf_bake_cmd");
    std::filesystem::remove_all(dir);
    std::shared_ptr<const BakeResult> seen;
    size_t seenFiles = 0;
    setBakeListener([&](const Document&, std::shared_ptr<const BakeResult> r, const std::vector<std::string>& files) {
        seen = r;
        seenFiles = files.size();
    });
    CommandResult res = reg.execute(doc, "bake", {{"source", std::string("all")}, {"maps", std::string("normal,ao,displacement")},
                                                  {"size", 256.0}, {"out", dir}, {"ao_rays", 8.0}});
    std::printf("  %s\n", res.message.c_str());
    CHECK(res.ok);
    CHECK(res.message.find("mid-grey = 0") != std::string::npos);
    CHECK(seen && seen->width == 256 && seen->has(BakeMap::AmbientOcclusion));
    CHECK(seenFiles == 4);
    for (const char* f : {"Low_normal.png", "Low_ao.png", "Low_displacement.png", "Low_displacement.exr"})
        CHECK(std::filesystem::exists(std::filesystem::path(dir) / f));
    res = reg.execute(doc, "bake", {{"source", std::string("High")}, {"size", std::string("1k")}, {"out", dir}, {"name", std::string("x")},
                                    {"front", 0.05}, {"rear", std::string("0.05")}, {"samples", 1.0}});
    CHECK(res.ok);
    CHECK(std::filesystem::exists(std::filesystem::path(dir) / "x_normal.png"));
    CHECK(!reg.execute(doc, "bake", {{"source", std::string("Low")}, {"out", dir}}).ok);  // the active one
    CHECK(!reg.execute(doc, "bake", {{"source", std::string("Nope")}, {"out", dir}}).ok);
    CHECK(!reg.execute(doc, "bake", {{"size", 300.0}, {"out", dir}}).ok);
    CHECK(!reg.execute(doc, "bake", {{"green", std::string("purple")}, {"out", dir}}).ok);
    doc.subtools[0].visible = false; // nothing visible to bake from
    CHECK(!reg.execute(doc, "bake", {{"out", dir}}).ok);
    setBakeListener({});
    std::filesystem::remove_all(dir);
}

// The brief's timing: 4K maps from a source of more than a million triangles.
TEST(bake_perf_4k_from_1m_triangles) {
    const Mesh high = highPoly(8); // 1.31M triangles
    const Mesh low = lowPoly();
    BakeSettings st;
    st.size = 4096;
    st.maps = bakeBit(BakeMap::Normal) | bakeBit(BakeMap::Displacement);
    BakeResult r;
    std::string err;
    CHECK(bakeMaps(low, {&high}, st, r, err));
    std::printf("  4K normal + displacement, 2x2 samples, from %zu triangles: %.2f s (BVH %.2f s), %zu island texels\n",
                r.sourceTriangles, r.seconds, r.bvhSeconds, r.islandTexels);
    CHECK(r.sourceTriangles > 1000000);
    CHECK(r.seconds < timeLimit(60.0));
}

// Send to Unreal takes the baked normal map along: imported as T_<name>_N, set up as a normal map, green flipped
// for an OpenGL-style map; skipped for an editor on another computer.
TEST(bake_unreal_normal_map_import) {
    link::UnrealImport im;
    im.fbxPath = "/tmp/SM_Rock.fbx";
    im.assetName = "SM_Rock";
    im.normalMapPath = "/tmp/maps/Rock_normal.png";
    std::string py = link::unrealImportScript(im);
    CHECK(py.find("'/tmp/maps/Rock_normal.png'") != std::string::npos);
    CHECK(py.find("'T_Rock_N'") != std::string::npos);
    CHECK(py.find("TC_NORMALMAP") != std::string::npos && py.find("'srgb', False") != std::string::npos);
    CHECK(py.find("flip_green_channel', True") != std::string::npos);
    CHECK(py.find("SCULPTFORGE_TEXTURE") < py.find("SCULPTFORGE_IMPORTED")); // the mesh's line stays last
    im.normalMapDirectX = true;
    CHECK(link::unrealImportScript(im).find("flip_green_channel', False") != std::string::npos);
    im.downloadUrl = "http://10.0.0.5:5000/t/SM_Rock.fbx";
    CHECK(link::unrealImportScript(im).find("Rock_normal.png") == std::string::npos);
    im.downloadUrl.clear();
    im.normalMapPath.clear();
    CHECK(link::unrealImportScript(im).find("TC_NORMALMAP") == std::string::npos);
}
