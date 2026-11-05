// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Per-brush settings (#78): each brush keeps its own Size, Intensity, Falloff, Colour intensity, stroke and
// alpha. Picking another brush remembers the one left and brings back what the picked one had this session (or what
// Save Brush Settings stored for it, <app data>/brush-settings.json); a brush not used yet takes the current values,
// so nothing jumps the first time. Reset Brush to Default puts back the shipped values. Pen pressure (#122) is the
// exception to "nothing jumps": a brush not used yet starts with its own (grab brushes: Size off).
#pragma once

#include "core/AlphaOptions.h"
#include "core/Json.h"
#include "core/PenPressure.h"
#include "core/Stroke.h"

#include <map>
#include <string>

namespace sf {

struct BrushToolSettings {
    float radiusPx = 60.0f;     // Size (pixels)
    float strength = 0.5f;      // Intensity, 0..1
    float focal = 0.0f;         // Falloff, -1..1
    float rgbIntensity = 1.0f;  // paint strength, 0..1
    StrokeType stroke = StrokeType::Dots;
    float sprayScatter = 1.0f;  // Spray's scatter (fraction of the brush size)
    float lazyRadius = 40.0f;   // LazyMouse's string (pixels)
    // The alpha: a built-in by name (alphaPath empty), or an image file (the library's, a pack's, a loaded one).
    std::string alphaName = "Round";
    std::string alphaPath;
    bool alphaInvert = false;
    AlphaOptions alphaOptions; // Follow stroke direction, Angle, Angle jitter, Tile (#76)
    // Pen pressure > Size / Strength for this brush (#122): Use global (Preferences > Tablet), On or Off.
    PenPressure pressureSize = PenPressure::Global;
    PenPressure pressureStrength = PenPressure::Global;

    bool operator==(const BrushToolSettings&) const = default;
    void clamp();
    Json toJson() const;
    void applyJson(const Json& j); // lenient: missing keys keep their values, values are clamped
};

// The key a brush's settings are kept under: "builtin:<brushName>" or "custom:<recipe name>".
std::string brushSettingsKey(const std::string& builtinName, const std::string& customName = "");

class BrushSettingsStore {
public:
    // Preferences > Brushes > Share size and intensity across brushes: Size and Intensity stay as they are
    // when switching (the behaviour before 0.10.8); the rest still follows the brush.
    bool shareSizeIntensity = false;

    // A switch from `from` (whose live values are `current`) to `to`: what `to` uses now.
    BrushToolSettings switchBrush(const std::string& from, const std::string& to, const BrushToolSettings& current);
    // What `key` uses at startup (its saved settings) when it has any.
    bool savedFor(const std::string& key, BrushToolSettings& out) const;
    // Save Brush Settings: `key` starts with `s` from now on (call save(path) to write the file).
    void store(const std::string& key, const BrushToolSettings& s);
    // Reset Brush to Default: forgets `key`'s session and saved values; returns the shipped ones.
    BrushToolSettings reset(const std::string& key);
    // The Vector Displacement brush stamps with DragRect; grab brushes (Move, the Kelvinlet grab / twist / scale,
    // the tendrils) have Pressure > Size off, since a pen's press is nearly weightless (#122).
    static BrushToolSettings shipped(const std::string& key);
    bool hasSaved(const std::string& key) const { return saved_.count(key) != 0; }
    // Used this session or saved: switchBrush brings its own values back (otherwise it takes the current ones).
    bool known(const std::string& key) const { return session_.count(key) != 0 || saved_.count(key) != 0; }
    size_t savedCount() const { return saved_.size(); }

    Json toJson() const; // the saved settings and shareSizeIntensity (the session's values are not written)
    void applyJson(const Json& j);
    bool save(const std::string& path, std::string& error) const;
    bool load(const std::string& path, std::string& error); // a missing file is no error (nothing saved yet)

private:
    std::map<std::string, BrushToolSettings> session_, saved_;
};

} // namespace sf
