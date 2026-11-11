// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// #76: alpha options (core/AlphaOptions.h): Follow stroke direction (Blender's Rake smoothing), Angle, Angle jitter
// and Tile, kept per brush (core/BrushSettings.h).
#include "TestHarness.h"

#include "core/AlphaOptions.h"
#include "core/BrushAlpha.h"
#include "core/BrushSettings.h"
#include "core/Image.h"

#include <cmath>
#include <filesystem>
#include <set>

using namespace sf;

namespace {
bool near(float a, float b, float eps = 1e-3f) { return std::abs(a - b) <= eps; }
// The direction the alpha's top points on screen for a dab angle (App::dabAt: (-sin a, cos a), y up).
glm::vec2 topOnScreen(float a) { return {-std::sin(a), -std::cos(a)}; } // window pixels, y down
} // namespace

TEST(alpha_rake_follows_stroke_direction) {
    AlphaRake rake;
    rake.reset({100, 100});
    CHECK(!rake.update({110, 100})); // under 20 px: not known yet
    CHECK(rake.update({125, 100}));  // moving right
    glm::vec2 top = topOnScreen(rake.angle());
    CHECK(near(top.x, 1.0f) && near(top.y, 0.0f));
    // Wobbles under 20 px don't turn it.
    const float before = rake.angle();
    rake.update({130, 108});
    CHECK(rake.angle() == before);
    // Turning down the screen: the top points down once the cursor has gone 20 px.
    rake.update({125, 160});
    top = topOnScreen(rake.angle());
    CHECK(near(top.x, 0.0f, 0.1f) && top.y > 0.99f);
    // Moving up the screen is the plain upright alpha.
    rake.reset({0, 0});
    rake.update({0, -40});
    CHECK(near(rake.angle(), 0.0f));
}

TEST(alpha_dab_angle_fixed_jitter_and_follow) {
    AlphaRake rake;
    rake.reset({0, 0});
    rake.update({40, 0}); // right: -90 degrees
    uint32_t seed = 7;
    AlphaOptions o;
    CHECK(alphaDabAngle(o, rake, seed) == 0.0f);
    o.angle = 30.0f;
    CHECK(near(alphaDabAngle(o, rake, seed), glm::radians(30.0f)));
    o.followStroke = true;
    CHECK(near(alphaDabAngle(o, rake, seed), glm::radians(30.0f - 90.0f)));
    o.followStroke = false;
    o.jitter = 20.0f;
    bool varied = false;
    float first = alphaDabAngle(o, rake, seed);
    for (int i = 0; i < 50; ++i) {
        const float a = alphaDabAngle(o, rake, seed);
        CHECK(a >= glm::radians(10.0f) - 1e-5f && a <= glm::radians(50.0f) + 1e-5f);
        varied = varied || !near(a, first);
    }
    CHECK(varied);
    // The same seed gives the same jitter (strokes repeat exactly, as verify replays them).
    uint32_t s1 = 0x9e3779b9u, s2 = 0x9e3779b9u;
    CHECK(alphaDabAngle(o, rake, s1) == alphaDabAngle(o, rake, s2));
}

TEST(alpha_tile_repeats_inside_the_stamp) {
    BrushAlpha a;
    a.name = "Half";
    a.width = a.height = 8;
    a.values.assign(64, 0.0f);
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 4; ++x) a.values[size_t(y * 8 + x)] = 1.0f; // left half white
    CHECK(a.sample(-0.5f, 0.0f) > 0.9f && a.sample(0.5f, 0.0f) < 0.1f);
    a.tileX = 2;
    // Two copies across: each tile's left half is white.
    CHECK(a.sample(-0.75f, 0.0f) > 0.9f && a.sample(-0.25f, 0.0f) < 0.1f);
    CHECK(a.sample(0.25f, 0.0f) > 0.9f && a.sample(0.75f, 0.0f) < 0.1f);
    CHECK(a.sample(1.5f, 0.0f) == 0.0f); // still nothing outside the stamp
    // The round (empty) alpha tiles too: four soft dots, each brightest at its own centre.
    BrushAlpha r;
    r.tileX = r.tileY = 2;
    CHECK(r.stamp(-0.5f, 0.5f, -1.0f) > 0.9f);
    CHECK(r.stamp(0.0f, 0.0f, -1.0f) < 0.1f);
    CHECK(BrushAlpha{}.stamp(0.0f, 0.0f, 0.0f) > 0.99f); // untiled: unchanged
}

TEST(alpha_options_saved_per_brush) {
    BrushToolSettings s;
    s.alphaOptions.followStroke = true;
    s.alphaOptions.angle = -45.0f;
    s.alphaOptions.jitter = 15.0f;
    s.alphaOptions.scaleJitter = 25.0f;
    s.alphaOptions.positionJitter = 40.0f;
    s.alphaOptions.tileX = 3;
    s.alphaOptions.tileY = 2;
    BrushToolSettings back;
    back.applyJson(s.toJson());
    CHECK(back == s);
    // Older files have none: the defaults. Out-of-range values are clamped.
    BrushToolSettings plain;
    plain.applyJson(BrushToolSettings{}.toJson());
    CHECK(plain.alphaOptions == AlphaOptions{});
    Json j;
    std::string err;
    CHECK(Json::parse(R"({"alpha_tile_x": 40, "alpha_angle": 900, "alpha_angle_jitter": -5})", j, err));
    BrushToolSettings c;
    c.applyJson(j);
    CHECK(c.alphaOptions.tileX == 8 && c.alphaOptions.angle == 180.0f && c.alphaOptions.jitter == 0.0f);
    // Switching brushes carries each brush's options.
    BrushSettingsStore store;
    store.switchBrush("builtin:Standard", "builtin:Clay", s);
    BrushToolSettings clay = s;
    clay.alphaOptions = {};
    CHECK(store.switchBrush("builtin:Clay", "builtin:Standard", clay).alphaOptions == s.alphaOptions);
}

// #136: a drag stamp's turn (Drag Stamp / Slide Stamp, alphas and VDMs): the drag's direction when it follows,
// Angle on top, and the stroke's jitter.
TEST(alpha_stamp_angle) {
    AlphaOptions o;
    const float right = glm::degrees(alphaStampAngle(o, true, glm::vec2(10.0f, 0.0f), 0.0f));
    CHECK(std::fabs(right + 90.0f) < 0.01f); // dragged right: the top points right (as the rake)
    CHECK(alphaStampAngle(o, false, glm::vec2(10.0f, 0.0f), 0.0f) == 0.0f);
    o.angle = 30.0f;
    CHECK(std::fabs(glm::degrees(alphaStampAngle(o, false, glm::vec2(10.0f, 0.0f), 0.0f)) - 30.0f) < 0.01f);
    CHECK(std::fabs(glm::degrees(alphaStampAngle(o, true, glm::vec2(10.0f, 0.0f), 0.0f)) + 60.0f) < 0.01f);
    CHECK(alphaStampAngle(o, true, glm::vec2(0.0f), 0.25f) == glm::radians(30.0f) + 0.25f); // no drag yet
    // Jitter: within the range, different stamp to stamp, none at 0.
    uint32_t seed = 7;
    CHECK(alphaJitterAngle(0.0f, seed) == 0.0f && seed == 7);
    const float a = alphaJitterAngle(20.0f, seed), b = alphaJitterAngle(20.0f, seed);
    CHECK(a != b);
    CHECK(std::fabs(a) <= glm::radians(20.0f) && std::fabs(b) <= glm::radians(20.0f));
    // NaN from a damaged file never reaches a stamp.
    AlphaOptions bad;
    bad.angle = std::nanf("");
    bad.jitter = std::nanf("");
    bad.clamp();
    CHECK(bad.angle == 0.0f && bad.jitter == 0.0f);
}

TEST(slide_stamp_turn_follows_the_cursor_while_the_key_is_held) {
    // #175: the rotate key held mid-drag keeps the stamp where it is and turns the alpha toward the cursor; let go, the
    // stamp slides on with the turn kept; it never touches the brush's saved Angle.
    AlphaOptions brush;
    brush.angle = 25.0f;
    const AlphaOptions before = brush;
    SlideStampTurn t;
    t.reset();
    // Sliding (key up): the stamp is under the cursor, no turn.
    glm::vec2 at = t.position(false, {100, 100});
    t.turn(false, {100, 100});
    CHECK(at == glm::vec2(100, 100) && !t.turned());
    at = t.position(false, {140, 100});
    CHECK(at == glm::vec2(140, 100) && !t.turned());
    // Key down: the stamp stays at (140, 100); the cursor to its right turns the alpha's top to the right...
    at = t.position(true, {200, 100});
    t.turn(true, {200, 100});
    CHECK(at == glm::vec2(140, 100) && t.turned());
    CHECK(std::fabs(t.angle() - alphaStampAngle(AlphaOptions{}, true, {60, 0}, 0.0f)) < 1e-6f); // (= Drag Stamp's own rule)
    // ...above it, upward on the screen (y down): the alpha's top at the top, angle 0; to the left, a quarter turn the other way.
    t.position(true, {140, 40});
    t.turn(true, {140, 40});
    CHECK(std::fabs(t.angle()) < 1e-6f);
    t.position(true, {80, 100});
    t.turn(true, {80, 100});
    CHECK(std::fabs(t.angle() - glm::radians(90.0f)) < 1e-5f);
    // A cursor on the stamp itself gives no direction: the turn stays.
    t.position(true, {140, 101});
    t.turn(true, {140, 101});
    CHECK(std::fabs(t.angle() - glm::radians(90.0f)) < 1e-5f);
    // Key up: the stamp follows the cursor again, turned as it was.
    at = t.position(false, {300, 220});
    t.turn(false, {300, 220});
    CHECK(at == glm::vec2(300, 220) && t.turned() && std::fabs(t.angle() - glm::radians(90.0f)) < 1e-5f);
    // A new stroke starts clean; the brush's Angle is what it was.
    t.reset();
    CHECK(!t.turned() && t.angle() == 0.0f);
    CHECK(brush == before);
}

// The alpha-pool workflow: Scale and Position jitter per dab.
TEST(alpha_scale_and_position_jitter) {
    AlphaOptions o;
    uint32_t seed = 7;
    AlphaDabJitter none = alphaDabJitter(o, seed);
    CHECK(none.scale == 1.0f && none.offset == glm::vec2(0.0f) && seed == 7); // off: nothing drawn, the seed untouched
    o.scaleJitter = 30.0f;
    o.positionJitter = 50.0f;
    float lo = 9.0f, hi = 0.0f, far = 0.0f;
    std::set<float> seen;
    for (int i = 0; i < 400; ++i) {
        const AlphaDabJitter j = alphaDabJitter(o, seed);
        lo = std::min(lo, j.scale);
        hi = std::max(hi, j.scale);
        far = std::max(far, glm::length(j.offset));
        seen.insert(j.scale);
    }
    CHECK(lo >= 0.7f - 1e-5f && hi <= 1.3f + 1e-5f && lo < 0.8f && hi > 1.2f); // spans the range, never outside
    CHECK(far <= 0.5f + 1e-5f && far > 0.4f);                                   // inside the disc, reaching out
    CHECK(seen.size() > 100);
    // Scale alone leaves the dab where it is; the same seed repeats the same jitter.
    AlphaOptions scaleOnly;
    scaleOnly.scaleJitter = 20.0f;
    uint32_t a = 99, b = 99;
    const AlphaDabJitter first = alphaDabJitter(scaleOnly, a), again = alphaDabJitter(scaleOnly, b);
    CHECK(first.offset == glm::vec2(0.0f) && first.scale == again.scale && a == b);
    // Damaged files are clamped.
    AlphaOptions bad;
    bad.scaleJitter = 500.0f;
    bad.positionJitter = std::nanf("");
    bad.clamp();
    CHECK(bad.scaleJitter == 90.0f && bad.positionJitter == 0.0f);
}

TEST(alpha_pool_from_folder_and_pick) {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "sculptforge_alpha_pool_test";
    fs::remove_all(dir);
    fs::create_directories(dir / "sub");
    auto plank = [&](const char* name, uint8_t grey) {
        Image img;
        img.width = img.height = 8;
        img.rgba.assign(8 * 8 * 4, grey);
        for (size_t i = 3; i < img.rgba.size(); i += 4) img.rgba[i] = 255;
        std::string err;
        CHECK(img.savePng((dir / name).string(), err));
    };
    plank("plank_b.png", 200);
    plank("plank_a.png", 100);
    plank("plank_c.png", 50);
    plank("sub/hidden.png", 10); // sub-folders are not part of the pool
    { std::FILE* f = std::fopen((dir / "notes.txt").string().c_str(), "w"); if (f) std::fclose(f); }
    std::string err;
    const auto pool = AlphaPool::fromFolder(dir.string(), err);
    CHECK(pool && pool->size() == 3);
    if (!pool) return;
    CHECK(pool->at(0)->name == "plank_a" && pool->at(1)->name == "plank_b" && pool->at(2)->name == "plank_c"); // A-Z
    CHECK(pool->contains(pool->at(1)->path) && !pool->contains("nope"));
    // Every member gets picked, and the same seed repeats the same sequence (a stroke is repeatable).
    uint32_t s1 = 12345, s2 = 12345;
    std::set<std::string> picked;
    for (int i = 0; i < 60; ++i) {
        const auto& x = pool->pick(s1);
        CHECK(x->name == pool->pick(s2)->name);
        picked.insert(x->name);
    }
    CHECK(picked.size() == 3);
    // Picked alphas keep the image's values (grey 100 -> 0.39).
    CHECK(near(pool->at(0)->values[0], 100.0f / 255.0f, 0.01f));
    // An empty folder says so; adding is a copy that dedupes; a one-alpha pool never uses the seed.
    fs::create_directories(dir / "empty");
    CHECK(!AlphaPool::fromFolder((dir / "empty").string(), err) && !err.empty());
    const auto more = pool->with(*pool->at(0));
    CHECK(more->size() == 3 && pool->size() == 3);
    BrushAlpha wood;
    CHECK(BrushAlpha::builtin("Procedural Wood Straight", wood));
    const auto four = pool->with(wood);
    CHECK(four->size() == 4 && pool->size() == 3 && four->contains("Procedural Wood Straight"));
    const auto one = AlphaPool{}.with(wood);
    uint32_t seed = 5;
    CHECK(one->pick(seed)->name == "Procedural Wood Straight" && seed == 5);
    fs::remove_all(dir);
}

TEST(procedural_wood_alphas_are_named_as_such) {
    for (const char* name : {"Procedural Wood Straight", "Procedural Wood Cathedral", "Procedural Wood Weathered"}) {
        BrushAlpha a;
        CHECK(BrushAlpha::builtin(name, a) && !a.empty());
        CHECK(BrushAlpha::builtinHelp(name).find("not a scan") != std::string::npos); // never passed off as a scan
    }
}
