// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// #78: per-brush settings (core/BrushSettings.h). Each brush keeps its own size, intensity, falloff, stroke and alpha
// in the session; Save Brush Settings writes them for the next start; Reset Brush to Default gives the shipped values.
#include "TestHarness.h"

#include "core/BrushSettings.h"

#include <cstdio>
#include <filesystem>

using namespace sf;

TEST(brush_settings_switch_keeps_each_brush) {
    BrushSettingsStore store;
    const std::string standard = brushSettingsKey("Standard"), clay = brushSettingsKey("Clay");
    BrushToolSettings live;
    live.radiusPx = 120.0f;
    live.strength = 0.8f;
    live.alphaName = "Pores";
    // Clay hasn't been used: it starts from the current values.
    BrushToolSettings c = store.switchBrush(standard, clay, live);
    CHECK(c == live);
    c.radiusPx = 20.0f;
    c.focal = -0.5f;
    c.stroke = StrokeType::Spray;
    c.alphaName = "Round";
    const BrushToolSettings s = store.switchBrush(clay, standard, c);
    CHECK(s == live);
    const BrushToolSettings back = store.switchBrush(standard, clay, s);
    CHECK(back == c);
    // Custom brushes are kept by name, apart from the built-ins.
    CHECK(brushSettingsKey("Standard", "Clay Slab") == "custom:Clay Slab");
    CHECK(brushSettingsKey("Standard", "Clay Slab") != standard);
}

TEST(brush_settings_share_size_and_intensity) {
    BrushSettingsStore store;
    store.shareSizeIntensity = true;
    BrushToolSettings a;
    a.radiusPx = 30.0f;
    a.strength = 0.2f;
    a.alphaName = "Square";
    store.switchBrush("builtin:Standard", "builtin:Clay", a);
    BrushToolSettings b = a;
    b.alphaName = "Noise";
    b.radiusPx = 200.0f;
    b.strength = 0.9f;
    const BrushToolSettings back = store.switchBrush("builtin:Clay", "builtin:Standard", b);
    CHECK(back.radiusPx == 200.0f && back.strength == 0.9f); // size and intensity stay
    CHECK(back.alphaName == "Square");                       // the rest follows the brush
}

TEST(brush_settings_save_load_reset) {
    const std::string path = tempPath("sf_brush_settings_test.json");
    std::remove(path.c_str());
    BrushSettingsStore store;
    BrushToolSettings s;
    s.radiusPx = 42.0f;
    s.strength = 0.25f;
    s.focal = 0.5f;
    s.rgbIntensity = 0.3f;
    s.stroke = StrokeType::LazyMouse;
    s.lazyRadius = 80.0f;
    s.alphaName = "brick";
    s.alphaPath = "/alphas/brick.png";
    s.alphaInvert = true;
    store.store("custom:Clay Slab", s);
    store.shareSizeIntensity = true;
    std::string err;
    CHECK(store.save(path, err));

    BrushSettingsStore loaded;
    CHECK(loaded.load(path, err));
    CHECK(loaded.shareSizeIntensity);
    BrushToolSettings got;
    CHECK(loaded.savedFor("custom:Clay Slab", got));
    CHECK(got == s);
    CHECK(!loaded.savedFor("builtin:Clay", got));
    // A saved brush not used this session starts with its saved values.
    loaded.shareSizeIntensity = false;
    CHECK(loaded.switchBrush("builtin:Standard", "custom:Clay Slab", BrushToolSettings{}) == s);

    const BrushToolSettings def = loaded.reset("custom:Clay Slab");
    CHECK(def == BrushToolSettings{});
    CHECK(!loaded.hasSaved("custom:Clay Slab"));
    CHECK(BrushSettingsStore::shipped("builtin:Vdm").stroke == StrokeType::DragRect);
    // After the reset the session doesn't bring back the old values either.
    BrushToolSettings live;
    live.radiusPx = 99.0f;
    CHECK(loaded.switchBrush("builtin:Standard", "custom:Clay Slab", live).radiusPx == 99.0f);

    // Missing file: nothing saved, no error. Damaged values are clamped.
    BrushSettingsStore none;
    CHECK(none.load(path + ".missing", err) && none.savedCount() == 0);
    Json j;
    CHECK(Json::parse(R"({"brushes":{"builtin:Clay":{"draw_size":5000,"z_intensity":-3,"stroke":"nope"}}})", j, err));
    BrushSettingsStore bad;
    bad.applyJson(j);
    CHECK(bad.savedFor("builtin:Clay", got));
    CHECK(got.radiusPx == 800.0f && got.strength == 0.0f && got.stroke == StrokeType::Dots);
    std::remove(path.c_str());
}
