// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// #78: per-brush settings (core/BrushSettings.h). Each brush keeps its own size, intensity, falloff, stroke and alpha
// in the session; Save Brush Settings writes them for the next start; Reset Brush to Default gives the shipped values.
#include "TestHarness.h"

#include "core/BrushRecipe.h"
#include "core/BrushSettings.h"
#include "core/Sculpt.h"

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
    // The VDM brush (by its real key: the brush is named "VDM") ships with Drag Stamp, turned by the drag (#136).
    const std::string vdmKey = brushSettingsKey(brushName(BrushType::Vdm));
    CHECK(BrushSettingsStore::shipped(vdmKey).stroke == StrokeType::DragRect);
    CHECK(BrushSettingsStore::shipped(vdmKey).alphaOptions.followStroke);
    {
        // ...also on first use after a brush with it off, and in files saved before the VDM had the option.
        BrushSettingsStore fresh;
        BrushToolSettings clay;
        CHECK(fresh.switchBrush("builtin:Clay", vdmKey, clay).alphaOptions.followStroke);
        Json old;
        std::string e;
        CHECK(Json::parse(R"({"brushes": {"builtin:VDM": {"stroke": "drag_dot"}}})", old, e));
        fresh.applyJson(old);
        BrushToolSettings got;
        CHECK(fresh.savedFor(vdmKey, got) && got.alphaOptions.followStroke && got.stroke == StrokeType::DragDot);
        // Turned off and saved, it stays off.
        got.alphaOptions.followStroke = false;
        fresh.store(vdmKey, got);
        BrushSettingsStore again;
        again.applyJson(fresh.toJson());
        CHECK(again.savedFor(vdmKey, got) && !got.alphaOptions.followStroke);
    }
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

// #122: each brush's own Pen pressure > Size / Strength (Use global / On / Off), grab brushes with Size off.
TEST(brush_settings_pen_pressure_per_brush) {
    CHECK(penPressureOn(PenPressure::Global, true) && !penPressureOn(PenPressure::Global, false));
    CHECK(penPressureOn(PenPressure::On, false) && !penPressureOn(PenPressure::Off, true));
    for (const char* grab : {"Move", "KelvinGrab", "KelvinTwist", "KelvinScale", "Tendril", "KelvinSnake"}) {
        const BrushToolSettings s = BrushSettingsStore::shipped(brushSettingsKey(grab));
        CHECK(s.pressureSize == PenPressure::Off);
        CHECK(s.pressureStrength == PenPressure::Global);
    }
    CHECK(BrushSettingsStore::shipped("builtin:Clay").pressureSize == PenPressure::Global);
    CHECK(BrushSettingsStore::shipped("builtin:Nudge").pressureSize == PenPressure::Global);
    CHECK(BrushSettingsStore::shipped("custom:Clay Slab").pressureSize == PenPressure::Global);

    // A brush not used yet takes the current values, except its own pen pressure.
    BrushSettingsStore store;
    BrushToolSettings live;
    live.radiusPx = 77.0f;
    live.pressureStrength = PenPressure::Off;
    BrushToolSettings move = store.switchBrush("builtin:Clay", "builtin:Move", live);
    CHECK(move.radiusPx == 77.0f);
    CHECK(move.pressureSize == PenPressure::Off && move.pressureStrength == PenPressure::Global);
    move.pressureSize = PenPressure::On; // the artist's choice is kept for the brush
    const BrushToolSettings clay = store.switchBrush("builtin:Move", "builtin:Clay", move);
    CHECK(clay.pressureStrength == PenPressure::Off && clay.pressureSize == PenPressure::Global);
    CHECK(store.switchBrush("builtin:Clay", "builtin:Move", clay).pressureSize == PenPressure::On);

    // Saved and loaded; a file from before 0.11.1 (no pen pressure) loads as Use global, a grab brush with Size off.
    std::string err;
    Json j;
    CHECK(Json::parse(R"({"brushes":{"builtin:Clay":{"draw_size":50},"builtin:Move":{"draw_size":50},
                          "builtin:Standard":{"pen_pressure_size":"on","pen_pressure_strength":"off"}}})",
                      j, err));
    BrushSettingsStore old;
    old.applyJson(j);
    BrushToolSettings got;
    CHECK(old.savedFor("builtin:Clay", got) && got.pressureSize == PenPressure::Global &&
          got.pressureStrength == PenPressure::Global);
    CHECK(old.savedFor("builtin:Move", got) && got.pressureSize == PenPressure::Off);
    CHECK(old.savedFor("builtin:Standard", got) && got.pressureSize == PenPressure::On &&
          got.pressureStrength == PenPressure::Off);
    BrushToolSettings s;
    s.pressureSize = PenPressure::Global; // an explicit Use global on Move survives a save
    store.store("builtin:Move", s);
    BrushSettingsStore again;
    again.applyJson(store.toJson());
    CHECK(again.savedFor("builtin:Move", got) && got.pressureSize == PenPressure::Global);

    // A reset gives the brush's shipped pen pressure back.
    CHECK(again.reset("builtin:Move").pressureSize == PenPressure::Off);
}

// #122: a custom brush carries its pen pressure in its .sfbrush; older files (and Use global) write nothing.
TEST(brush_recipe_pen_pressure) {
    BrushRecipe r;
    CHECK(r.toJson()["penPressureSize"].isNull() && r.toJson()["penPressureStrength"].isNull());
    r.pressureSize = PenPressure::Off;
    r.pressureStrength = PenPressure::On;
    const BrushRecipe back = BrushRecipe::fromJson(r.toJson());
    CHECK(back.pressureSize == PenPressure::Off && back.pressureStrength == PenPressure::On);
    Json j;
    std::string err;
    CHECK(Json::parse(R"({"name":"Old","depth":0.3,"penPressureSize":"sideways"})", j, err));
    const BrushRecipe old = BrushRecipe::fromJson(j);
    CHECK(old.pressureSize == PenPressure::Global && old.pressureStrength == PenPressure::Global);
}
