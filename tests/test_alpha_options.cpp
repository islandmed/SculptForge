// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// #76: alpha options (core/AlphaOptions.h): Follow stroke direction (Blender's Rake smoothing), Angle, Angle jitter
// and Tile, kept per brush (core/BrushSettings.h).
#include "TestHarness.h"

#include "core/AlphaOptions.h"
#include "core/BrushAlpha.h"
#include "core/BrushSettings.h"

#include <cmath>

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
