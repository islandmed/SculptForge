// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Per-brush settings (#78): the app side of core/BrushSettings.h. A brush switch is noticed once per frame, outside
// strokes, so every way of picking a brush (shelf, pickers, keys, the Library, scripts, Ctrl-held brushes) behaves
// the same without each of them knowing about it.
#include "app/App.h"
#include "app/Ui.h"

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
    s.pressureSize = penSize_;
    s.pressureStrength = penStrength_;
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
    penSize_ = s.pressureSize;
    penStrength_ = s.pressureStrength;
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
    registerPenPressureCommand();
    shippedPenPressure();
    if (!brushSettingsOn_) return;
    std::string err;
    if (const std::string path = brushSettingsPath(); !path.empty() && !brushSettings_.load(path, err))
        SDL_Log("Brush settings: %s", err.c_str());
    if (BrushToolSettings s; brushSettings_.savedFor(brushSettingsKey_, s)) applyBrushSettings(s);
}

void App::syncBrushSettings() {
    if (drag_ != Drag::None || doc_.sculptor.strokeActive()) return;
    syncAlphaTile();
    const std::string key = currentBrushKey();
    if (key == brushSettingsKey_) return;
    if (!brushSettingsOn_) { // (demo videos, benchmarks: their scripts set every value, but a grab brush's pen is its own)
        brushSettingsKey_ = key;
        shippedPenPressure();
        return;
    }
    const bool firstUse = !brushSettings_.known(key);
    BrushToolSettings next = brushSettings_.switchBrush(brushSettingsKey_, key, liveBrushSettings());
    if (firstUse) recipeAlpha(next); // a brush-pack brush starts with its own alpha (or none), not the last brush's
    if (firstUse) recipePenPressure(next);
    applyBrushSettings(next);
    brushSettingsKey_ = key;
    syncAlphaTile();
}

void App::recipeAlpha(BrushToolSettings& s) const {
    if (doc_.activeBrush < 0 || doc_.activeBrush >= int(doc_.brushes.size())) return;
    const BrushRecipe& r = doc_.brushes[size_t(doc_.activeBrush)];
    const bool turns = r.alphaFollow || r.alphaAngle != 0.0f || r.alphaJitter != 0.0f; // (a shared brush's own turn, #136)
    if (r.alpha.empty() && r.origin.pack.empty() && !turns) return;
    if (!r.alpha.empty() || !r.origin.pack.empty()) {
        s.alphaName = r.alpha.empty() ? std::string("Round") : r.alpha;
        s.alphaPath.clear();
        s.alphaInvert = false;
        s.alphaOptions.tileX = s.alphaOptions.tileY = 1;
    }
    s.alphaOptions.followStroke = r.alphaFollow;
    s.alphaOptions.angle = r.alphaAngle;
    s.alphaOptions.jitter = r.alphaJitter;
}

void App::recipePenPressure(BrushToolSettings& s) const {
    if (doc_.activeBrush < 0 || doc_.activeBrush >= int(doc_.brushes.size())) return;
    const BrushRecipe& r = doc_.brushes[size_t(doc_.activeBrush)];
    s.pressureSize = r.pressureSize;
    s.pressureStrength = r.pressureStrength;
}

void App::shippedPenPressure() {
    BrushToolSettings s = BrushSettingsStore::shipped(brushSettingsKey_);
    recipePenPressure(s);
    penSize_ = s.pressureSize;
    penStrength_ = s.pressureStrength;
}

void App::setPenPressure(bool size, PenPressure p) {
    (size ? penSize_ : penStrength_) = p;
    if (BrushRecipe* r = doc_.activeBrush >= 0 && doc_.activeBrush < int(doc_.brushes.size())
                             ? &doc_.brushes[size_t(doc_.activeBrush)]
                             : nullptr;
        r && (size ? r->pressureSize : r->pressureStrength) != p) {
        (size ? r->pressureSize : r->pressureStrength) = p; // a custom brush keeps it in its .sfbrush too
        saveBrushEdits(doc_.activeBrush);
    }
    const char* state = p == PenPressure::On ? "on" : p == PenPressure::Off ? "off" : "as Preferences > Tablet";
    status_ = std::string("Pen pressure > ") + (size ? "Size" : "Strength") + " for " +
              (activeRecipe() ? activeRecipe()->name : std::string(brushName(brush_))) + ": " + state;
}

void App::drawPenPressureSwitches(const char* id) {
    ImGui::PushID(id);
    ImGui::TextDisabled("Pen pressure: %s", activeRecipe() ? activeRecipe()->name.c_str() : brushLabel(brush_));
    const float labelW = ImGui::CalcTextSize("Strength").x + ImGui::GetFontSize(); // (a clear gap before the segments)
    for (const bool size : {true, false}) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(size ? "Size" : "Strength");
        ImGui::SameLine(labelW);
        if (size) ui::Segmented(*this, {"brush.pen_size_global", "brush.pen_size_on", "brush.pen_size_off"});
        else ui::Segmented(*this, {"brush.pen_strength_global", "brush.pen_strength_on", "brush.pen_strength_off"});
    }
    ImGui::PopID();
}

void App::registerPenPressureCommand() {
    commands_.add({"brush_pen_pressure",
                   "The current brush's own pen pressure switches (kept per brush; Save Brush Settings keeps them): "
                   "'global' follows Preferences > Tablet's Pressure > Size / Strength, 'on' and 'off' override it "
                   "for this brush. Omitted switches stay as they are.",
                   {{"size", ParamSpec::Type::String, "Pressure > Size: global, on or off.", false},
                    {"strength", ParamSpec::Type::String, "Pressure > Strength: global, on or off.", false}},
                   [this](Document&, const Args& a) -> CommandResult {
                       for (const char* key : {"size", "strength"}) {
                           const auto it = a.find(key);
                           if (it == a.end()) continue;
                           const std::string v = argString(a, key, "");
                           if (v != "global" && v != "on" && v != "off")
                               return {false, std::string(key) + " must be global, on or off"};
                           setPenPressure(std::string(key) == "size", penPressureFromId(v));
                       }
                       return {true, std::string("Pen pressure: size ") + penPressureId(penSize_) + " (" +
                                         (pressureSizeOn() ? "on" : "off") + "), strength " +
                                         penPressureId(penStrength_) + " (" + (pressureStrengthOn() ? "on" : "off") + ")"};
                   }});
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
    if (BrushRecipe* r = doc_.activeBrush >= 0 && doc_.activeBrush < int(doc_.brushes.size())
                             ? &doc_.brushes[size_t(doc_.activeBrush)]
                             : nullptr;
        r && (r->alphaFollow != alphaOptions_.followStroke || r->alphaAngle != alphaOptions_.angle ||
              r->alphaJitter != alphaOptions_.jitter)) {
        r->alphaFollow = alphaOptions_.followStroke; // the brush's .sfbrush (and so its packs and codes) keeps them (#136)
        r->alphaAngle = alphaOptions_.angle;
        r->alphaJitter = alphaOptions_.jitter;
        r->clamp();
        saveBrushEdits(doc_.activeBrush);
        watchActiveBrush(); // (saved now: the autosave has nothing left to do)
    }
    std::string err;
    const std::string path = brushSettingsPath();
    if (!brushSettingsOn_ || path.empty()) status_ = "Brush settings kept for this session";
    else if (!brushSettings_.save(path, err)) status_ = err;
    else status_ = "Saved this brush's settings: it starts with them from now on";
}

void App::resetBrushSettings() {
    syncBrushSettings();
    const bool wasSaved = brushSettings_.hasSaved(brushSettingsKey_);
    BrushToolSettings shipped = brushSettings_.reset(brushSettingsKey_);
    recipeAlpha(shipped); // (a pack brush's own alpha is part of its defaults)
    recipePenPressure(shipped);
    applyBrushSettings(shipped);
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
