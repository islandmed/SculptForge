// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Precision work, the app side of core/Precision.h: Steady Stroke with every stroke type (and its catch-up on
// release), World Size (Draw Size kept in model units, shown in mm), the precision drag (Shift+Alt mid-stroke),
// the size readout by the brush, and each brush's own pressure curve (Brush Lab, Preferences > Tablet).
#include "app/App.h"

#include "app/Theme.h"
#include "app/Ui.h"

#include <imgui_internal.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace sf {

// Shift+Alt, the keyboard's own (not a style's mapping): free mid-stroke in every input style. At the press it
// stays what it was (ZBrush: select and frame a Part; Blender: Alt+left navigates), so it is held once drawing.
bool App::precisionHeld() const {
    const SDL_Keymod keys = SDL_GetModState();
    return (keys & SDL_KMOD_SHIFT) && (keys & SDL_KMOD_ALT) && !(keys & (SDL_KMOD_CTRL | SDL_KMOD_GUI));
}

void App::steadyCatchUp() {
    if (!steadyOn() || !steadyCatchUp_ || drag_ != Drag::Stroke) return;
    if (glm::distance(lazyMouse_.point, steadyCursor_) < 0.5f) return;
    const float keep = lazyMouse_.radius;
    lazyMouse_.radius = 0.0f; // the string goes slack: the brush runs on to the pointer
    strokeTo(mouseX_, mouseY_);
    lazyMouse_.radius = keep;
}

// ---------------------------------------------------------------------------
// World Size and the Draw Size readout
// ---------------------------------------------------------------------------

void App::syncWorldSize() {
    if (drag_ == Drag::Stroke || doc_.sculptor.strokeActive()) return; // a stroke keeps the size it began with
    RayHit hit;
    const CanvasRect c = canvasRect();
    const bool inCanvas = mouseX_ >= c.x && mouseY_ >= c.y && mouseX_ < c.x + c.w && mouseY_ < c.y + c.h;
    if (inCanvas && pick(mouseX_, mouseY_, hit)) {
        cursorSurface_ = hit.position;
        haveCursorSurface_ = true;
    }
    // Off the model: at the surface point last under the cursor (so zooming there keeps the size), else the pivot.
    cursorWorldPerPx_ = camera_.worldPerPixel(haveCursorSurface_ ? cursorSurface_ : camera_.target, canvasHeight());
    const float wpp = cursorWorldPerPx_;
    if (!(wpp > 0.0f)) return;
    if (!worldSize_ || radiusPx_ != worldSyncedPx_ || worldRadius_ <= 0.0f) {
        worldRadius_ = radiusPx_ * wpp; // pixels set the size (a slider, a key, a brush switch)
    } else {
        radiusPx_ = clampDrawSize(worldRadius_ / wpp); // zoomed: the same size on the surface
    }
    worldSyncedPx_ = radiusPx_;
}

namespace {

std::filesystem::path utf8Path(const std::string& s) { return std::filesystem::path(std::u8string(s.begin(), s.end())); }

std::string mmText(float mm) {
    char buf[32];
    std::snprintf(buf, sizeof buf, mm < 1.0f ? "%.2f mm" : mm < 100.0f ? "%.1f mm" : "%.0f mm", double(mm));
    return buf;
}

float shownMm(float radiusPx, float worldRadius, bool worldSize, float wpp) {
    return (worldSize && worldRadius > 0.0f ? worldRadius : radiusPx * wpp) * kMmPerUnit;
}

} // namespace

std::string App::drawSizeLabel() const {
    std::string s = radiusPx_ < 10.0f ? "Draw Size %.1f px" : "Draw Size %.0f px";
    if (cursorWorldPerPx_ > 0.0f) s += "  " + mmText(shownMm(radiusPx_, worldRadius_, worldSize_, cursorWorldPerPx_));
    return s;
}

std::string App::drawSizeStatus() const {
    char buf[96];
    std::snprintf(buf, sizeof buf, radiusPx_ < 10.0f ? "Draw Size %.1f px" : "Draw Size %.0f px", double(radiusPx_));
    std::string s = buf;
    if (cursorWorldPerPx_ > 0.0f) s += " (" + mmText(shownMm(radiusPx_, worldRadius_, worldSize_, cursorWorldPerPx_)) + ")";
    return s;
}

void App::drawSizeSlider(const char* id, const char* helpPath, const char* help) {
    ui::SliderInside(id, &radiusPx_, kMinDrawSizePx, kMaxDrawSizePx, drawSizeLabel().c_str(), ImGuiSliderFlags_Logarithmic);
    ui::HelpText("Draw Size", "S", help, helpPath);
}

void App::drawPrecisionCursor(ImDrawList* dl, ImVec2 c, float r, ImU32 col) {
    const float s = uiScale_;
    if (drag_ == Drag::Stroke && steadyOn()) {
        // The string from the pointer to the lagging brush point, which is where the dabs land.
        const ImVec2 lp(lazyMouse_.point.x, lazyMouse_.point.y);
        const ImVec2 cp(steadyCursor_.x, steadyCursor_.y);
        dl->AddLine(cp, lp, IM_COL32(255, 255, 255, 150), 1.0f * s);
        dl->AddCircle(cp, std::max(lazyMouse_.radius, 2.0f), IM_COL32(255, 255, 255, 40), 48, 1.0f);
        dl->AddCircleFilled(lp, 3.0f * s, col);
    }
    if (drag_ == Drag::Stroke && (precisionActive_ || !precision_.caughtUp())) {
        // The precision drag: the brush (at a quarter of the pen's speed) and a line back to the pen.
        const ImVec2 bp(precision_.point.x, precision_.point.y);
        dl->AddLine(c, bp, IM_COL32(120, 220, 255, 140), 1.0f * s);
        dl->AddCircle(bp, std::max(r, 1.5f), IM_COL32(120, 220, 255, 230), 48, 1.5f);
        dl->AddCircleFilled(bp, 1.5f * s, IM_COL32(120, 220, 255, 255));
    }
    // A tiny brush: a cross at its centre (a 1 px ring is hard to see), and the size by the ring.
    if (r < 4.0f) {
        const float arm = 5.0f * s;
        dl->AddLine(ImVec2(c.x - arm, c.y), ImVec2(c.x - r - 1.0f, c.y), col, 1.0f);
        dl->AddLine(ImVec2(c.x + r + 1.0f, c.y), ImVec2(c.x + arm, c.y), col, 1.0f);
        dl->AddLine(ImVec2(c.x, c.y - arm), ImVec2(c.x, c.y - r - 1.0f), col, 1.0f);
        dl->AddLine(ImVec2(c.x, c.y + r + 1.0f), ImVec2(c.x, c.y + arm), col, 1.0f);
    }
    if (radiusPx_ != sizeReadoutPx_) {
        sizeReadoutPx_ = radiusPx_;
        sizeReadoutUntil_ = ImGui::GetTime() + 1.5;
    }
    if (drag_ == Drag::SizeScrub || worldSize_ || ImGui::GetTime() < sizeReadoutUntil_) {
        const std::string text = drawSizeStatus().substr(10); // without "Draw Size "
        const ImVec2 at(c.x + std::max(r, 6.0f * s) + 6.0f * s, c.y - ImGui::GetFontSize() * 0.5f);
        dl->AddText(ImVec2(at.x + 1, at.y + 1), IM_COL32(0, 0, 0, 160), text.c_str());
        dl->AddText(at, IM_COL32(240, 240, 240, 230), text.c_str());
    }
}

void App::drawPrecisionOptions() {
    ImGui::Spacing();
    ui::Checkbox("Steady Stroke##steady", &steadyStroke_);
    ui::HelpAction(*this, "stroke.lazy");
    if (steadyOn()) {
        ImGui::SetNextItemWidth(-1);
        ui::SliderInside("##lazy", &lazyMouse_.radius, 1.0f, 300.0f, "Steady radius %.0f px");
        ui::HelpText("Steady radius", nullptr, "Length of the string the brush trails on: longer = steadier lines.",
                     "Stroke");
        ui::Checkbox("Catch up on release##steady_catch_up", &steadyCatchUp_);
        ui::HelpAction(*this, "stroke.steady_catch_up");
    }
    ImGui::SetNextItemWidth(-1);
    float pi = precisionIntensity_ * 100.0f;
    if (ui::SliderInside("##precision_intensity", &pi, 1.0f, 100.0f, "Precision Intensity %.0f"))
        precisionIntensity_ = pi / 100.0f;
    ui::HelpText("Precision Intensity", "Shift+Alt",
                 "Hold Shift+Alt while drawing: the brush moves at a quarter of the pen's speed for careful placement, "
                 "and dabs at this intensity. Let go and it glides back under the pen.",
                 "Stroke");
}

// ---------------------------------------------------------------------------
// Each brush's pressure curve
// ---------------------------------------------------------------------------

std::string App::pressureCurveKey(BrushType type, const BrushRecipe* recipe) const {
    // The same keys as per-brush settings (#78, core/BrushSettings.h), so the curve can join them there.
    return recipe ? "custom:" + recipe->name : std::string("builtin:") + brushName(type);
}

std::string App::pressureCurveKey() const { return pressureCurveKey(brush_, activeRecipe()); }

PressurePoints& App::brushCurve() { return brushCurves_[pressureCurveKey()]; }

const PressurePoints* App::activeBrushCurve() const {
    const auto it = brushCurves_.find(pressureCurveKey());
    return it == brushCurves_.end() ? nullptr : &it->second;
}

std::string App::brushCurvesPath() const {
    return configDir_.empty() || uiConfigDeterministic_ ? std::string() : configDir_ + "brush-curves.json";
}

void App::brushCurvesChanged() {
    const std::string path = brushCurvesPath(); // (none in tests and recordings: they never touch your settings)
    if (path.empty()) return;
    Json j = Json::object();
    for (const auto& [key, curve] : brushCurves_)
        if (!curve.isLinear()) j.set(key, curve.toJson());
    std::ofstream f(utf8Path(path), std::ios::binary | std::ios::trunc);
    f << j.dump(true);
    if (!f) SDL_Log("Could not save %s", path.c_str());
}

void App::loadBrushCurves() {
    const std::string path = brushCurvesPath();
    std::error_code ec;
    if (path.empty() || !std::filesystem::is_regular_file(utf8Path(path), ec)) return;
    std::ifstream f(utf8Path(path), std::ios::binary);
    std::stringstream text;
    text << f.rdbuf();
    Json j;
    std::string err;
    if (!Json::parse(text.str(), j, err) || !j.isObject()) {
        SDL_Log("%s: %s", path.c_str(), err.c_str());
        return;
    }
    for (const auto& [key, value] : j.members()) brushCurves_[key] = PressurePoints::fromJson(value);
}

void App::drawPressureCurveEditor(const char* id, float width) {
    const float s = uiScale_;
    PressurePoints& curve = brushCurve();
    const float h = std::round(width * 0.62f);
    ImGui::PushID(id);
    ImGui::TextDisabled("%s", (std::string("Pressure curve: ") + (activeRecipe() ? activeRecipe()->name : brushName(brush_))).c_str());
    const ImVec2 g0 = ImGui::GetCursorScreenPos(), g1(g0.x + width, g0.y + h);
    ImGui::InvisibleButton("##curve", ImVec2(width, h));
    ui::HelpText("Brush pressure curve", nullptr,
                 "How pen pressure (left to right) becomes this brush's pressure (bottom to top), after the tablet's "
                 "curve. Each brush keeps its own. Drag a point; double-click the curve to add one (up to 5); "
                 "right-click a point to remove it.",
                 "Brush");
    const bool hovered = ImGui::IsItemHovered(), active = ImGui::IsItemActive();
    auto toScreen = [&](glm::vec2 p) { return ImVec2(g0.x + p.x * width, g1.y - p.y * h); };
    auto fromScreen = [&](ImVec2 m) {
        return glm::vec2(std::clamp((m.x - g0.x) / width, 0.0f, 1.0f), std::clamp((g1.y - m.y) / h, 0.0f, 1.0f));
    };
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    int near = -1;
    float best = 8.0f * s;
    for (int i = 0; i < curve.count; ++i) {
        const ImVec2 q = toScreen(curve.points[size_t(i)]);
        const float d = std::hypot(q.x - mouse.x, q.y - mouse.y);
        if (d < best) best = d, near = i;
    }
    bool changed = false, done = false;
    if (ImGui::IsItemActivated()) curveDragPoint_ = near;
    if (active && curveDragPoint_ >= 0 && curveDragPoint_ < curve.count) {
        glm::vec2 p = fromScreen(mouse);
        if (curveDragPoint_ == 0) p.x = 0.0f; // the ends keep their x
        if (curveDragPoint_ == curve.count - 1) p.x = 1.0f;
        curve.points[size_t(curveDragPoint_)] = p;
        changed = true;
    }
    if (ImGui::IsItemDeactivated() && curveDragPoint_ >= 0) {
        curveDragPoint_ = -1;
        curve.sort();
        done = true;
    }
    if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && near < 0) done = curve.addPoint(fromScreen(mouse).x);
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right) && near >= 0) done = curve.removePoint(near);
    if (changed && !active) curve.sort();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(g0, g1, IM_COL32(0x18, 0x18, 0x1a, 0xff), 3.0f * s);
    for (int i = 1; i < 4; ++i) {
        const float fx = g0.x + width * float(i) / 4.0f, fy = g0.y + h * float(i) / 4.0f;
        dl->AddLine(ImVec2(fx, g0.y), ImVec2(fx, g1.y), IM_COL32(255, 255, 255, 18));
        dl->AddLine(ImVec2(g0.x, fy), ImVec2(g1.x, fy), IM_COL32(255, 255, 255, 18));
    }
    dl->AddLine(ImVec2(g0.x, g1.y), ImVec2(g1.x, g0.y), IM_COL32(255, 255, 255, 40)); // linear reference
    PressurePoints shown = curve;
    shown.sort(); // (while a point is dragged past its neighbour)
    ImVec2 pts[65];
    for (int i = 0; i <= 64; ++i) {
        const float x = float(i) / 64.0f;
        pts[i] = toScreen({x, shown.apply(x)});
    }
    dl->AddPolyline(pts, 65, ui::color::accent, 0, 2.0f * s);
    for (int i = 0; i < curve.count; ++i)
        dl->AddCircleFilled(toScreen(curve.points[size_t(i)]), (i == near || i == curveDragPoint_ ? 5.0f : 4.0f) * s,
                            IM_COL32(255, 255, 255, 230));
    dl->AddRect(g0, g1, ui::color::border, 3.0f * s);

    const auto& presets = PressurePoints::presets();
    const float bw = (width - 3.0f * ImGui::GetStyle().ItemSpacing.x) / 4.0f;
    const char* on = curve.presetName();
    for (size_t i = 0; i < presets.size(); ++i) {
        if (i) ImGui::SameLine();
        const bool sel = on && std::string(on) == presets[i].name;
        if (sel) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::ColorConvertU32ToFloat4(ui::color::accent));
        if (ImGui::Button((std::string(presets[i].name) + "##brushcurve").c_str(), ImVec2(bw, 0))) {
            curve = presets[i].make();
            done = true;
        }
        if (sel) ImGui::PopStyleColor();
    }
    ImGui::PopID();
    if (done) {
        status_ = std::string("Pressure curve for ") + (activeRecipe() ? activeRecipe()->name : brushName(brush_)) + ": " +
                  (curve.presetName() ? curve.presetName() : "custom");
        brushCurvesChanged();
    }
}

} // namespace sf
