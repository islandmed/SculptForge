// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/BrushSettings.h"
#include "core/Precision.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace sf {

namespace {

std::filesystem::path utf8Path(const std::string& s) { return std::filesystem::path(std::u8string(s.begin(), s.end())); }

// Stroke types by a stable id (the labels may change).
const char* const kStrokeIds[] = {"dots", "drag_rect", "drag_dot", "spray", "lazy_mouse"};
static_assert(std::size(kStrokeIds) == size_t(StrokeType::Count));

void readFloat(const Json& j, const char* key, float& v) {
    if (j[key].isNumber()) v = float(j[key].asNumber());
}

} // namespace

void BrushToolSettings::clamp() {
    radiusPx = clampDrawSize(radiusPx); // (0.5 px and up: core/Precision.h)
    strength = std::clamp(strength, 0.0f, 1.0f);
    focal = std::clamp(focal, -1.0f, 1.0f);
    rgbIntensity = std::clamp(rgbIntensity, 0.0f, 1.0f);
    sprayScatter = std::clamp(sprayScatter, 0.0f, 4.0f);
    lazyRadius = std::clamp(lazyRadius, 0.0f, 400.0f);
    if (int(stroke) < 0 || stroke >= StrokeType::Count) stroke = StrokeType::Dots;
    alphaOptions.clamp();
}

Json BrushToolSettings::toJson() const {
    Json j = Json::object();
    j.set("draw_size", double(radiusPx));
    j.set("z_intensity", double(strength));
    j.set("focal_shift", double(focal));
    j.set("rgb_intensity", double(rgbIntensity));
    j.set("stroke", kStrokeIds[int(stroke)]);
    j.set("spray_scatter", double(sprayScatter));
    j.set("lazy_radius", double(lazyRadius));
    j.set("alpha", alphaName);
    if (!alphaPath.empty()) j.set("alpha_path", alphaPath);
    if (alphaInvert) j.set("alpha_invert", true);
    const AlphaOptions& a = alphaOptions;
    if (a.followStroke) j.set("alpha_follow_stroke", true);
    if (a.angle != 0.0f) j.set("alpha_angle", double(a.angle));
    if (a.jitter != 0.0f) j.set("alpha_angle_jitter", double(a.jitter));
    if (a.tileX != 1 || a.tileY != 1) {
        j.set("alpha_tile_x", a.tileX);
        j.set("alpha_tile_y", a.tileY);
    }
    return j;
}

void BrushToolSettings::applyJson(const Json& j) {
    if (!j.isObject()) return;
    readFloat(j, "draw_size", radiusPx);
    readFloat(j, "z_intensity", strength);
    readFloat(j, "focal_shift", focal);
    readFloat(j, "rgb_intensity", rgbIntensity);
    readFloat(j, "spray_scatter", sprayScatter);
    readFloat(j, "lazy_radius", lazyRadius);
    for (int i = 0; i < int(StrokeType::Count); ++i)
        if (j["stroke"].asString() == kStrokeIds[i]) stroke = StrokeType(i);
    if (j["alpha"].isString()) {
        alphaName = j["alpha"].asString();
        alphaPath = j["alpha_path"].isString() ? j["alpha_path"].asString() : std::string();
        alphaInvert = j["alpha_invert"].asBool(false);
    }
    AlphaOptions& a = alphaOptions;
    a.followStroke = j["alpha_follow_stroke"].asBool(a.followStroke);
    readFloat(j, "alpha_angle", a.angle);
    readFloat(j, "alpha_angle_jitter", a.jitter);
    a.tileX = int(j["alpha_tile_x"].asNumber(a.tileX));
    a.tileY = int(j["alpha_tile_y"].asNumber(a.tileY));
    clamp();
}

std::string brushSettingsKey(const std::string& builtinName, const std::string& customName) {
    return customName.empty() ? "builtin:" + builtinName : "custom:" + customName;
}

BrushToolSettings BrushSettingsStore::switchBrush(const std::string& from, const std::string& to,
                                                  const BrushToolSettings& current) {
    if (!from.empty()) session_[from] = current;
    BrushToolSettings next = current; // a brush not used yet: the current values, nothing jumps
    if (auto it = session_.find(to); it != session_.end()) next = it->second;
    else if (auto s = saved_.find(to); s != saved_.end()) next = s->second;
    if (shareSizeIntensity) {
        next.radiusPx = current.radiusPx;
        next.strength = current.strength;
    }
    return next;
}

bool BrushSettingsStore::savedFor(const std::string& key, BrushToolSettings& out) const {
    const auto it = saved_.find(key);
    if (it == saved_.end()) return false;
    out = it->second;
    return true;
}

void BrushSettingsStore::store(const std::string& key, const BrushToolSettings& s) {
    saved_[key] = s;
    session_[key] = s;
}

BrushToolSettings BrushSettingsStore::reset(const std::string& key) {
    saved_.erase(key);
    session_.erase(key);
    return shipped(key);
}

BrushToolSettings BrushSettingsStore::shipped(const std::string& key) {
    BrushToolSettings s;
    if (key == "builtin:Vdm") s.stroke = StrokeType::DragRect; // stamps a VDM where you drag (PanelVdm.cpp)
    return s;
}

Json BrushSettingsStore::toJson() const {
    Json brushes = Json::object();
    for (const auto& [key, s] : saved_) brushes.set(key, s.toJson());
    Json j = Json::object();
    j.set("format", "sculptforge-brush-settings");
    j.set("version", 1);
    j.set("share_size_intensity", shareSizeIntensity);
    j.set("brushes", std::move(brushes));
    return j;
}

void BrushSettingsStore::applyJson(const Json& j) {
    shareSizeIntensity = j["share_size_intensity"].asBool(shareSizeIntensity);
    const Json& brushes = j["brushes"];
    if (!brushes.isObject()) return;
    for (const auto& [key, value] : brushes.members()) {
        if (!value.isObject()) continue;
        BrushToolSettings s;
        s.applyJson(value);
        saved_[key] = s;
    }
}

bool BrushSettingsStore::save(const std::string& path, std::string& error) const {
    std::error_code ec;
    std::filesystem::create_directories(utf8Path(path).parent_path(), ec);
    const std::string tmp = path + ".tmp";
    {
        std::ofstream f(utf8Path(tmp), std::ios::binary);
        if (!(f << toJson().dump(true) << "\n")) {
            error = "Cannot write " + path;
            return false;
        }
    }
    std::filesystem::rename(utf8Path(tmp), utf8Path(path), ec);
    if (ec) {
        error = "Cannot write " + path + ": " + ec.message();
        return false;
    }
    return true;
}

bool BrushSettingsStore::load(const std::string& path, std::string& error) {
    std::ifstream f(utf8Path(path), std::ios::binary);
    if (!f) return true; // nothing saved yet
    const std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    Json j;
    if (!Json::parse(text, j, error)) {
        error = path + ": " + error;
        return false;
    }
    applyJson(j);
    return true;
}

} // namespace sf
