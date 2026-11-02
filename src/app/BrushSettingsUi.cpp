// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Per-brush settings (#78): the app side of core/BrushSettings.h. A brush switch is noticed once per frame, outside
// strokes, so every way of picking a brush (shelf, pickers, keys, the Library, scripts, Ctrl-held brushes) behaves
// the same without each of them knowing about it.
#include "app/App.h"

#include "core/BrushLibrary.h"
#include "core/Image.h"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <filesystem>

namespace sf {

namespace {

std::string lower(std::string s) {
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

} // namespace

std::string App::brushSettingsPath() const {
    const std::string dir = appDataDir();
    return dir.empty() ? std::string() : dir + "/brush-settings.json";
}

std::string App::currentBrushKey() const {
    const bool custom = doc_.activeBrush >= 0 && doc_.activeBrush < int(doc_.brushes.size());
    return brushSettingsKey(brushName(brush_), custom ? doc_.brushes[size_t(doc_.activeBrush)].name : std::string());
}

BrushToolSettings App::liveBrushSettings() const {
    BrushToolSettings s;
    s.radiusPx = radiusPx_;
    s.strength = strength_;
    s.focal = focalShift_;
    s.rgbIntensity = rgbIntensity_;
    s.stroke = strokeType_;
    s.sprayScatter = sprayScatter_;
    s.lazyRadius = lazyMouse_.radius;
    const BrushAlpha& a = doc_.brushAlpha;
    s.alphaName = a.empty() ? std::string("Round") : a.name;
    s.alphaPath = a.empty() ? std::string() : a.path;
    s.alphaInvert = !s.alphaPath.empty() && a.inverted;
    s.alphaOptions = alphaOptions_;
    return s;
}

void App::applyBrushSettings(const BrushToolSettings& in) {
    BrushToolSettings s = in;
    s.clamp();
    radiusPx_ = s.radiusPx;
    strength_ = s.strength;
    focalShift_ = s.focal;
    rgbIntensity_ = s.rgbIntensity;
    strokeType_ = s.stroke;
    sprayScatter_ = s.sprayScatter;
    lazyMouse_.radius = s.lazyRadius;
    alphaOptions_ = s.alphaOptions;
    // The alpha, loaded only when it differs; one that can't be loaded any more (a moved image) leaves the current.
    const BrushToolSettings live = liveBrushSettings();
    if (lower(live.alphaName) == lower(s.alphaName) && live.alphaPath == s.alphaPath && live.alphaInvert == s.alphaInvert)
        return;
    if (s.alphaPath.empty()) {
        BrushAlpha a;
        if (BrushAlpha::builtin(s.alphaName, a)) doc_.brushAlpha = std::move(a);
        return;
    }
    Image img;
    std::string err;
    if (!Image::load(s.alphaPath, img, err)) return;
    BrushAlpha a = BrushAlpha::fromImage(img, s.alphaName, s.alphaInvert);
    a.path = s.alphaPath;
    a.inverted = s.alphaInvert;
    doc_.brushAlpha = std::move(a);
}

void App::initBrushSettings() {
    const bool recording = !options_.recordPath.empty() && options_.uiTest.empty(); // demo videos
    brushSettingsOn_ = !recording && !options_.benchmark && options_.timelapsePath.empty() &&
                       options_.renderPath.empty() && options_.measureIdle <= 0.0f && options_.measureStroke <= 0.0f;
    brushSettingsKey_ = currentBrushKey();
    registerAlphaOptionsCommand();
    if (!brushSettingsOn_) return;
    std::string err;
    if (const std::string path = brushSettingsPath(); !path.empty() && !brushSettings_.load(path, err))
        SDL_Log("Brush settings: %s", err.c_str());
    if (BrushToolSettings s; brushSettings_.savedFor(brushSettingsKey_, s)) applyBrushSettings(s);
}

void App::syncBrushSettings() {
    if (drag_ != Drag::None || doc_.sculptor.strokeActive()) return;
    syncAlphaTile();
    if (!brushSettingsOn_) return;
    const std::string key = currentBrushKey();
    if (key == brushSettingsKey_) return;
    applyBrushSettings(brushSettings_.switchBrush(brushSettingsKey_, key, liveBrushSettings()));
    brushSettingsKey_ = key;
    syncAlphaTile();
}

void App::syncAlphaTile() { // Tile lives on the alpha (a new alpha starts at 1 x 1): the brush's option wins
    alphaOptions_.clamp();
    doc_.brushAlpha.tileX = alphaOptions_.tileX;
    doc_.brushAlpha.tileY = alphaOptions_.tileY;
}

void App::registerAlphaOptionsCommand() {
    using T = ParamSpec::Type;
    commands_.add({"brush_alpha_options",
                   "The brush's alpha options (Alpha palette; kept per brush): follow the stroke's direction, a fixed "
                   "angle, a random angle per dab, and tiling. Omitted options stay as they are.",
                   {{"follow_stroke", T::Bool, "Turn the alpha along the stroke (smoothed over 20 pixels).", false},
                    {"angle", T::Number, "Fixed angle in degrees, -180..180 (counter-clockwise).", false},
                    {"angle_jitter", T::Number, "Random turn per dab, up to this many degrees either way (0..180).", false},
                    {"tile_x", T::Number, "Repeat the alpha this many times across the dab (1..8).", false},
                    {"tile_y", T::Number, "Repeat the alpha this many times down the dab (1..8).", false}},
                   [this](Document&, const Args& a) -> CommandResult {
                       AlphaOptions& o = alphaOptions_;
                       o.followStroke = argBool(a, "follow_stroke", o.followStroke);
                       o.angle = float(argNumber(a, "angle", o.angle));
                       o.jitter = float(argNumber(a, "angle_jitter", o.jitter));
                       o.tileX = int(std::lround(argNumber(a, "tile_x", o.tileX)));
                       o.tileY = int(std::lround(argNumber(a, "tile_y", o.tileY)));
                       syncAlphaTile();
                       return {true, std::string("Alpha: ") + (o.followStroke ? "follows the stroke, " : "") + "angle " +
                                         std::to_string(int(std::lround(o.angle))) + ", jitter " +
                                         std::to_string(int(std::lround(o.jitter))) + ", tile " +
                                         std::to_string(o.tileX) + " x " + std::to_string(o.tileY)};
                   }});
}

void App::saveBrushSettings() {
    syncBrushSettings(); // (the live values belong to the brush picked now)
    brushSettings_.store(brushSettingsKey_, liveBrushSettings());
    std::string err;
    const std::string path = brushSettingsPath();
    if (!brushSettingsOn_ || path.empty()) status_ = "Brush settings kept for this session";
    else if (!brushSettings_.save(path, err)) status_ = err;
    else status_ = "Saved this brush's settings: it starts with them from now on";
}

void App::resetBrushSettings() {
    syncBrushSettings();
    const bool wasSaved = brushSettings_.hasSaved(brushSettingsKey_);
    applyBrushSettings(brushSettings_.reset(brushSettingsKey_));
    std::string err;
    if (wasSaved && brushSettingsOn_ && !brushSettingsPath().empty() && !brushSettings_.save(brushSettingsPath(), err))
        status_ = err;
    else
        status_ = "Brush reset to its default settings";
}

void App::setShareBrushSize(bool on) {
    brushSettings_.shareSizeIntensity = on;
    std::string err;
    if (brushSettingsOn_ && !brushSettingsPath().empty() && !brushSettings_.save(brushSettingsPath(), err)) status_ = err;
}

} // namespace sf
