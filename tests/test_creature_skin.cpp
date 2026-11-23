// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// gh#228: the Creature Skin alphas (core/CreatureSkin.h) and pack, and alpha files at full precision. The alphas are
// 2048 px, made once and kept, tile without a seam, and the pickers' list never makes the big ones; 16-bit PNG and
// OpenEXR alphas keep more than 256 levels (no 8-bit step); the pack's brushes keep their promise on a micro level
// (carving ones never raise a point, building ones never sink one, nothing NaN, a fully masked point never moves).
#include "GeomInvariants.h"

#include "core/BakeImage.h"
#include "core/Benchmark.h"
#include "core/BrushAlpha.h"
#include "core/BrushPacks.h"
#include "core/Commands.h"
#include "core/CreatureSkin.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>

// tinyexr's writer (third_party/tinyexr, linked into core): RGBA and compressed files, which core's saveExr doesn't write.
extern "C" int SaveEXR(const float* data, int width, int height, int components, int save_as_fp16, const char* filename,
                       const char** err);

using namespace sf;
using namespace sf::geom;

namespace {

double msSince(std::chrono::steady_clock::time_point t0) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

// Distinct values on a 16-bit scale.
size_t levels(const std::vector<float>& v) {
    std::vector<uint8_t> seen(65536, 0);
    size_t n = 0;
    for (float x : v) {
        uint8_t& s = seen[size_t(std::clamp(std::lround(x * 65535.0f), 0L, 65535L))];
        n += s == 0;
        s = 1;
    }
    return n;
}

// The step across the tile's wrap-around seam (last row to first, last column to first) over the largest step
// between any two neighbouring rows or columns inside: at most about 1 when the pattern repeats seamlessly (a real
// seam is a step no row inside has).
double seamRatio(const BrushAlpha& a) {
    const int n = a.width;
    auto at = [&](int x, int y) { return double(a.values[size_t(y) * size_t(n) + size_t(x)]); };
    auto rows = [&](int y0, int y1) {
        double s = 0.0;
        for (int x = 0; x < n; ++x) s += std::abs(at(x, y1) - at(x, y0));
        return s;
    };
    auto cols = [&](int x0, int x1) {
        double s = 0.0;
        for (int y = 0; y < n; ++y) s += std::abs(at(x1, y) - at(x0, y));
        return s;
    };
    double rowMax = 0.0, colMax = 0.0;
    for (int k = 0; k + 1 < n; ++k) rowMax = std::max(rowMax, rows(k, k + 1)), colMax = std::max(colMax, cols(k, k + 1));
    return std::max(rows(n - 1, 0) / std::max(rowMax, 1e-9), cols(n - 1, 0) / std::max(colMax, 1e-9));
}

} // namespace

TEST(creature_skin_alphas_full_size_made_once) {
    size_t count = 0;
    const CreatureAlpha* all = creatureAlphas(count);
    CHECK(count == 16);
    // Listing them (the pickers' tiles) makes only the small ones.
    for (size_t i = 0; i < count; ++i) {
        BrushAlpha tile;
        CHECK(BrushAlpha::builtinPreview(all[i].name, tile));
        CHECK(tile.width == kCreaturePreview);
        CHECK(BrushAlpha::builtinGroup(all[i].name) == "Creature Skin");
        CHECK(BrushAlpha::builtinName(all[i].name) == all[i].name);
        CHECK(!BrushAlpha::builtinHelp(all[i].name).empty());
    }
    CHECK(BrushAlpha::builtinGroup("Pores").empty());
    // A prefetch (the pickers' hover) makes the same alpha in the background; the pick then gets it.
    BrushAlpha::prefetch(all[count - 1].name);
    BrushAlpha::prefetch(all[count - 1].name); // (once)
    BrushAlpha::prefetch("Pores");             // (not a Creature Skin one: nothing)
    BrushAlpha fetched, direct;
    CHECK(BrushAlpha::builtin(all[count - 1].name, fetched));
    CHECK(BrushAlpha::builtinAt(all[count - 1].name, kCreatureSize, direct));
    CHECK(fetched.values == direct.values);
    // Every alpha's relief at 512 px (the range and pattern checks below hold at any size), four at full size: one of
    // each kind, Elephant Hide the slowest to make (all sixteen at 2048 took 24 s on a loaded test machine).
    double slowest = 0.0;
    std::string slowestName;
    for (size_t i = 0; i < count; ++i) {
        const std::string n = all[i].name;
        const bool full = n == "Elephant Hide" || n == "Snake Scales" || n == "Pores Fine" || n == "Skin Micro Grain";
        const auto t0 = std::chrono::steady_clock::now();
        BrushAlpha a;
        CHECK(full ? BrushAlpha::builtin(n, a) : BrushAlpha::builtinAt(n, 512, a));
        const double first = msSince(t0);
        if (full && first > slowest) slowest = first, slowestName = n;
        CHECK(a.width == (full ? 2048 : 512) && a.height == a.width && a.name == n);
        double sum = 0.0;
        float lo = 1.0f, hi = 0.0f;
        for (float v : a.values) {
            lo = std::min(lo, v);
            hi = std::max(hi, v);
            sum += v;
        }
        const double mean = sum / double(a.values.size());
        CHECK(lo >= 0.0f && hi <= 1.0f);
        CHECK(hi > 0.95f && lo < 0.05f);  // the whole range: a full-strength stamp
        CHECK(mean > 0.05 && mean < 0.85); // a pattern, not a flat plate
        CHECK(levels(a.values) > (full ? 4096u : 1024u)); // smooth relief, far past an 8-bit image's 256 levels
        if (!full) continue;
        // Kept: the second time is a copy, not a new one.
        const auto t1 = std::chrono::steady_clock::now();
        BrushAlpha again;
        CHECK(BrushAlpha::builtin(all[i].name, again));
        const double second = msSince(t1);
        CHECK(again.values == a.values);
        CHECK(second < std::max(first * 0.5, timeLimit(40.0)));
        std::printf("  %-20s made in %5.0f ms, then %4.1f ms; mean %.2f, %zu levels\n", all[i].name, first, second, mean,
                    levels(a.values));
    }
    std::printf("  slowest: %s %.0f ms\n", slowestName.c_str(), slowest);
    CHECK(slowest < timeLimit(1500.0)); // once per alpha, the first time a brush picks it
}

// The app's mode: a pick gets the tile at once (or the full alpha if it's made), and finishCreature makes it the full
// one before any stroke; strokePath does that itself, so a stroke right after a pick never stamps the tile.
TEST(creature_skin_deferred_pick_never_strokes_the_tile) {
    BrushAlpha::setDeferCreature(true);
    BrushAlpha a;
    CHECK(BrushAlpha::builtin("Pebble Scales", a));
    CHECK(a.width == kCreaturePreview ? a.pending : (a.width == kCreatureSize && !a.pending));
    a.pending = true; // (made already by an earlier test: act as if it weren't)
    a.tileX = 3;
    BrushAlpha::finishCreature(a, true);
    CHECK(a.width == kCreatureSize && a.tileX == 3 && a.name == "Pebble Scales");
    BrushAlpha direct;
    CHECK(BrushAlpha::builtinAt("Pebble Scales", kCreatureSize, direct) && direct.values == a.values);
    CHECK(!BrushAlpha::finishCreature(a, true)); // (already full)
    a.tileX = 1; // A stroke's snapshot shares the kept alpha; a tiled or other one is copied as before.
    const auto kept = BrushAlpha::shared(a);
    CHECK(kept && kept->values == a.values && BrushAlpha::shared(a) == kept);
    a.tileX = 2;
    CHECK(!BrushAlpha::shared(a));
    BrushAlpha classic;
    CHECK(BrushAlpha::builtin("Pores", classic) && !BrushAlpha::finishCreature(classic, true));
    // Through a path stroke: a pending tile becomes the full alpha first; a tile asked for as one (the brush previews)
    // stays a tile, so previews never make the 16 MB alphas.
    Document d = docFrom(denseSphereFixture());
    CHECK(BrushAlpha::builtinPreview("Pebble Scales", d.brushAlpha) && d.brushAlpha.width == kCreaturePreview);
    CHECK(!d.brushAlpha.pending && !BrushAlpha::finishCreature(d.brushAlpha, true));
    d.brushAlpha.pending = true; // (as a deferred pick leaves it)
    PathStroke s;
    s.points = {glm::vec3(-0.1f, 0.0f, 1.2f), glm::vec3(0.1f, 0.0f, 1.2f)};
    s.radius = 0.05f;
    std::string err;
    CHECK(strokePath(d, nullptr, BrushType::Standard, s, err) > 0);
    CHECK(d.brushAlpha.width == kCreatureSize);
    BrushAlpha::setDeferCreature(false);
}

TEST(creature_skin_alphas_tile_without_a_seam) {
    size_t count = 0;
    const CreatureAlpha* all = creatureAlphas(count);
    for (size_t i = 0; i < count; ++i) {
        BrushAlpha a;
        CHECK(BrushAlpha::builtinAt(all[i].name, 512, a) && a.width == 512);
        if (const char* dir = std::getenv("SF_DUMP_CREATURE_ALPHAS")) { // 16-bit PNGs to look at (artist-qa)
            BrushAlpha big;
            BrushAlpha::builtinAt(all[i].name, 1024, big);
            std::vector<uint16_t> px(big.values.size());
            for (size_t k = 0; k < px.size(); ++k) px[k] = uint16_t(std::lround(big.values[k] * 65535.0f));
            std::string err;
            savePng16(std::string(dir) + "/" + all[i].name + ".png", big.width, big.height, 1, px, err);
        }
        const double r = seamRatio(a);
        if (r > 1.05) std::printf("  %s: seam %.2f x the largest inside step\n", all[i].name, r);
        CHECK(r <= 1.05);
        // And the relief itself repeats: one tile over is the same surface.
        float off = 0.0f;
        for (int k = 0; k < 500; ++k) {
            const float x = std::fmod(float(k) * 0.618034f, 1.0f), y = std::fmod(float(k) * 0.414214f, 1.0f);
            const float h = all[i].height(x, y);
            off = std::max({off, std::abs(h - all[i].height(x + 1.0f, y)), std::abs(h - all[i].height(x, y + 1.0f))});
        }
        CHECK(off < 1e-3f);
    }
}

TEST(creature_skin_pack_brushes) {
    int n = 0;
    for (const BrushRecipe& r : builtinPackRecipes()) {
        if (r.origin.pack != "Creature Skin") continue;
        ++n;
        CHECK(!BrushAlpha::builtinGroup(r.alpha).empty()); // each stamps its own Creature Skin alpha
        CHECK(r.frontFacesOnly);
    }
    CHECK(n == 16);
    CHECK(!builtinBrushPackHelp("Creature Skin").empty());
}

// 16-bit PNG: a 4096-step ramp keeps thousands of levels (an 8-bit step would leave 256).
TEST(alpha_file_16bit_png_keeps_its_precision) {
    const int w = 4096, h = 4;
    std::vector<uint16_t> px(size_t(w) * h);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) px[size_t(y) * w + size_t(x)] = uint16_t(x * 65535 / (w - 1));
    const std::string path = tempPath("sf_alpha_ramp16.png");
    std::string err;
    CHECK(savePng16(path, w, h, 1, px, err));
    BrushAlpha a;
    CHECK(BrushAlpha::fromFile(path, "ramp", false, a, err, 8192));
    CHECK(a.width == w && a.path == path);
    std::printf("  16-bit PNG ramp: %zu levels\n", levels(a.values));
    CHECK(levels(a.values) > 256);
    CHECK(levels(a.values) > 3000);
    // Down to the 2048 px default: a box filter in floats, still far more than 256 levels, the ramp kept.
    BrushAlpha small;
    CHECK(BrushAlpha::fromFile(path, "ramp", true, small, err));
    CHECK(small.width == 2048 && small.inverted);
    CHECK(levels(small.values) > 1000);
    CHECK(small.values.front() > 0.99f && small.values[2047] < 0.01f); // (inverted)
    // Through brush_set_alpha, as Load image does.
    Document doc;
    CommandRegistry reg;
    CHECK(reg.execute(doc, "brush_set_alpha", {{"path", path}}).ok);
    CHECK(doc.brushAlpha.width == 2048 && levels(doc.brushAlpha.values) > 1000);
    std::filesystem::remove(path);
}

// OpenEXR: float height maps in their own range (here -2..3) come in stretched to 0..1 with every level kept; one
// grey channel or RGB.
TEST(alpha_file_exr_loads_at_full_precision) {
    const int w = 1024, h = 8;
    std::vector<float> grey(size_t(w) * h), rgb(size_t(w) * h * 3);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const float t = float(x) / float(w - 1);
            grey[size_t(y) * w + size_t(x)] = -2.0f + 5.0f * t;
            for (int c = 0; c < 3; ++c) rgb[(size_t(y) * w + size_t(x)) * 3 + size_t(c)] = 0.25f * t;
        }
    const std::string g = tempPath("sf_alpha_grey.exr"), c = tempPath("sf_alpha_rgb.exr");
    std::string err;
    CHECK(saveExr(g, w, h, 1, grey, err));
    CHECK(saveExr(c, w, h, 3, rgb, err));
    BrushAlpha a;
    CHECK(BrushAlpha::fromFile(g, "height", false, a, err));
    std::printf("  EXR grey -2..3: %zu levels, %.3f .. %.3f\n", levels(a.values), a.values.front(), a.values[size_t(w) - 1]);
    CHECK(a.width == w && a.height == h);
    CHECK(levels(a.values) > 1000);
    CHECK(a.values.front() < 1e-4f && a.values[size_t(w) - 1] > 0.9999f);
    CHECK(std::abs(a.values[size_t(w) / 2] - 0.5f) < 0.01f);
    BrushAlpha b; // in range already: kept as it is (0..0.25)
    CHECK(BrushAlpha::fromFile(c, "rgb", false, b, err));
    CHECK(levels(b.values) > 256 && std::abs(b.values[size_t(w) - 1] - 0.25f) < 1e-3f);
    std::filesystem::remove(g);
    std::filesystem::remove(c);
    BrushAlpha bad;
    CHECK(!BrushAlpha::fromFile(tempPath("sf_no_such_alpha.exr"), "x", false, bad, err) && !err.empty());
}

// The pack's promise on a micro level 4 (256 micro triangles per base one), where its detail lands: two strokes per
// brush, varied pressure, overlapping; a mask holds; carving brushes never raise a point, building ones never sink one.
TEST(creature_skin_pack_micro_level_invariants) {
    Mesh base = makeBenchSphere(6000);
    std::string err;
    CHECK(microDivide(base, 4, err));
    BVH bvh;
    bvh.build(base);
    int checked = 0;
    for (const BrushRecipe& r : builtinPackRecipes()) {
        if (r.origin.pack != "Creature Skin") continue;
        Mesh m = base;
        Document d;
        BVH b = bvh;
        d.setMesh(std::move(m), "Sculpt", &b);
        CHECK(BrushAlpha::builtinAt(r.alpha, 512, d.brushAlpha)); // (512 px: the direction of the moves is the point, and the 2048 px
                                                                // ones took 1.8 s each to make: 28 s for the sixteen)
        const MicroSnapshot start = microSnapshot(d.mesh, glm::vec3(0, 0, 1), 0.6f);
        for (int k = 0; k < 2; ++k) {
            PathStroke s;
            const float y = k == 0 ? 0.03f : -0.03f;
            for (int i = 0; i <= 8; ++i) s.points.push_back(glm::normalize(glm::vec3(-0.25f + 0.0625f * float(i), y, 1.0f)) * 1.2f);
            s.view = glm::vec3(0, 0, -1);
            s.radius = 0.08f;
            s.strength = 0.5f;
            s.pressure = k == 0 ? 0.2f : 1.0f;
            s.pressureEnd = k == 0 ? 1.0f : 0.2f;
            s.symmetry = Symmetry{false, false, false};
            CHECK(strokePath(d, &r, BrushType::Standard, s, err) > 0);
        }
        const MicroSnapshot after = microSnapshot(d.mesh, glm::vec3(0, 0, 1), 0.6f);
        float rise = 0.0f, sink = 0.0f;
        size_t bad = 0;
        for (size_t i = 0; i < start.points.size(); ++i) {
            const glm::vec3 p0 = start.points[i], p1 = after.points[i];
            bad += std::isfinite(p1.x + p1.y + p1.z) ? 0 : 1;
            const float along = glm::dot(p1 - p0, glm::normalize(p0)); // (the sphere's normal)
            rise = std::max(rise, along);
            sink = std::max(sink, -along);
        }
        std::printf("  %-20s rise %.2e sink %.2e\n", r.name.c_str(), rise, sink);
        CHECK(bad == 0);
        if (r.depth < 0.0f) {
            CHECK(sink > 1e-4f);         // it carved
            CHECK(rise <= 0.02f * sink); // and raised nothing (no rim, no spike)
        } else {
            CHECK(rise > 1e-4f);
            CHECK(sink <= 0.02f * rise);
        }
        ++checked;
    }
    CHECK(checked == 16);
}

// Loader edges (review of #228): an RGBA EXR height map below zero with transparent pixels stretches over the pixels
// that show and leaves the transparent ones at no effect; a size past 16384 is refused from the header, before the
// pixels are read; a box-filtered size rounds up, so the last column and row aren't dropped.
TEST(alpha_file_exr_edges) {
    const int w = 64, h = 4;
    std::vector<float> rgba(size_t(w) * h * 4);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            float* p = &rgba[(size_t(y) * w + size_t(x)) * 4];
            const float v = -3.0f + 2.0f * float(x) / float(w - 1); // -3 .. -1
            p[0] = p[1] = p[2] = v;
            p[3] = x < 8 ? 0.0f : 1.0f; // the first eight columns transparent
        }
    const std::string path = tempPath("sf_alpha_rgba_neg.exr");
    const char* err = nullptr;
    CHECK(SaveEXR(rgba.data(), w, h, 4, 1, path.c_str(), &err) == 0); // (half floats, tinyexr's default compression)
    BrushAlpha a;
    std::string error;
    CHECK(BrushAlpha::fromFile(path, "neg", false, a, error));
    CHECK(a.width == w);
    CHECK(a.values[0] == 0.0f && a.values[7] == 0.0f);     // transparent: no effect, not stretched to white
    CHECK(a.values[8] < 0.01f && a.values[size_t(w) - 1] > 0.99f); // the visible range is 0..1
    std::filesystem::remove(path);

    std::vector<float> wide(size_t(16400) * 3, 0.5f);
    const std::string big = tempPath("sf_alpha_wide.exr");
    CHECK(SaveEXR(wide.data(), 16400, 1, 3, 1, big.c_str(), &err) == 0);
    BrushAlpha b;
    CHECK(!BrushAlpha::fromFile(big, "wide", false, b, error) && error.find("16384") != std::string::npos);
    std::filesystem::remove(big);

    const int odd = 3001;
    std::vector<uint16_t> ramp(size_t(odd) * 2);
    for (int y = 0; y < 2; ++y)
        for (int x = 0; x < odd; ++x) ramp[size_t(y) * odd + size_t(x)] = x == odd - 1 ? 65535 : 0;
    const std::string png = tempPath("sf_alpha_odd16.png");
    CHECK(savePng16(png, odd, 2, 1, ramp, error));
    BrushAlpha c;
    CHECK(BrushAlpha::fromFile(png, "odd", false, c, error, 1000));
    CHECK(c.width == 751);                        // ceil(3001 / 4)
    CHECK(c.values[size_t(c.width) - 1] > 0.99f); // the last column is its own box, kept
    std::filesystem::remove(png);
}

// The promise behind the loader edges, on the dense sphere: a negative, partly transparent EXR height map as a carving
// alpha only carves (no vertex rises), the transparent part of the stamp moves nothing, and nothing goes NaN or flips.
TEST(exr_alpha_carve_invariant) {
    const int n = 128;
    std::vector<float> rgba(size_t(n) * n * 4);
    for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x) {
            float* p = &rgba[(size_t(y) * n + size_t(x)) * 4];
            const float v = -2.0f + 1.5f * std::sin(float(x) * 0.3f) * std::cos(float(y) * 0.3f); // -3.5 .. -0.5
            p[0] = p[1] = p[2] = v;
            p[3] = x < n / 2 ? 0.0f : 1.0f; // the left half of the stamp transparent
        }
    const std::string path = tempPath("sf_alpha_carve.exr");
    const char* err = nullptr;
    CHECK(SaveEXR(rgba.data(), n, n, 4, 0, path.c_str(), &err) == 0);
    Document d = denseSphere();
    std::string error;
    CHECK(BrushAlpha::fromFile(path, "carve", false, d.brushAlpha, error));
    std::filesystem::remove(path);
    const std::vector<glm::vec3> before = d.mesh.positions, normals = d.mesh.normals;
    BrushRecipe r; // a carving tip, as the pore brushes are
    r.depth = -0.5f;
    r.spacing = 0.3f;
    PathStroke s;
    s.points = {glm::vec3(0.0f, -0.1f, 1.2f), glm::vec3(0.0f, 0.1f, 1.2f)}; // straight up the view: the alpha upright
    s.radius = 0.12f;
    s.strength = 0.6f;
    s.symmetry = Symmetry{false, false, false};
    CHECK(strokePath(d, &r, BrushType::Standard, s, error) > 0);
    const float edge = meanEdge(d.mesh);
    const float rise = maxRise(before, d.mesh, normals), sink = maxSink(before, d.mesh, normals);
    std::printf("  EXR carve: sink %.2g rise %.2g (edge %.2g)\n", sink, rise, edge);
    CHECK(sink > 0.05f * edge);  // it carved
    CHECK(rise < 0.02f * edge);  // and raised nothing
    CHECK(nonFinite(d.mesh) == 0 && flippedFaces(before, d.mesh) == 0);
    // The transparent half (screen left of the stroke, x < -0.02 here) stays where it was.
    float leftMoved = 0.0f;
    for (size_t v = 0; v < before.size(); ++v)
        if (before[v].x < -0.02f && before[v].z > 0.8f) leftMoved = std::max(leftMoved, glm::distance(before[v], d.mesh.positions[v]));
    std::printf("  transparent half moved at most %.2g\n", leftMoved);
    CHECK(leftMoved < 0.02f * edge);
}
