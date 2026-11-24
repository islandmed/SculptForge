// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// View controls (#80, #91): X-Ray (the other Parts see-through, next to Solo), the 3D view's resolution scale
// (Preferences > Performance, Render), the light (Material > Light: a direction pad, intensity and ambient) and
// Save Render's size, shape and format. Each change runs a command (light, xray, view_resolution, save_render),
// so scripts and the console see the same thing.
#include "app/App.h"
#include "app/I18n.h"
#include "app/Ui.h"
#include "core/TierConfig.h"

#include <IconsLucide.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iterator>

namespace sf {

namespace {

constexpr const char* kLightPath = "Material > Light";

struct RenderAspect {
    const char* id; // the save_render argument and the action's suffix
    const char* label;
    float ratio; // width / height; 0 = the view's own
};
constexpr RenderAspect kAspects[] = {
    {"view", "View", 0.0f}, {"1:1", "1:1", 1.0f}, {"16:9", "16:9", 16.0f / 9.0f}, {"9:16", "9:16", 9.0f / 16.0f}, {"4:3", "4:3", 4.0f / 3.0f}};
constexpr int kAspectCount = int(sizeof kAspects / sizeof kAspects[0]);
// The longer side in pixels; 0 = twice the view's size (at most 4096), as Save Render always did.
constexpr int kRenderSizes[] = {0, 1080, 1920, 2048, 4096};

std::string aspectActionId(int i) {
    std::string id = kAspects[i].id;
    std::replace(id.begin(), id.end(), ':', 'x');
    return "render.aspect_" + id;
}

} // namespace

void App::registerViewUiCommands() {
    using T = ParamSpec::Type;
    commands_.add({"view_shadows",
                   "Shadows and ambient occlusion in the view (Preferences > Display, Material > Viewport Shading): the key "
                   "light's cast shadows and the darkening of creases and hollows, worked out from the drawn view. "
                   "In the PBR view, and in the Lit Sphere shading with lit_sphere on. Saved with the preferences.",
                   {{"ao", T::String, "on, off or auto (by the hardware tier; the default).", false},
                    {"shadows", T::String, "on, off or auto (by the hardware tier; the default).", false},
                    {"ao_strength", T::Number, "How dark the occlusion gets, 0 to 1 (default 0.8).", false},
                    {"ao_softness", T::Number, "How soft it is, 0 to 1 (default 0.4).", false},
                    {"shadow_softness", T::Number, "How soft the shadows' edges are, 0 to 1 (default 0.35).", false},
                    {"lit_sphere", T::Bool, "Also with Lit Sphere shading (default off).", false}},
                   [this](Document&, const Args& a) -> CommandResult {
                       auto parseMode = [&](const char* key, int& out) {
                           if (!a.contains(key)) return true;
                           const std::string m = argString(a, key);
                           if (m == "on" || m == "true" || m == "1") out = 1;
                           else if (m == "off" || m == "false" || m == "0") out = 0;
                           else if (m == "auto") out = -1;
                           else return false;
                           return true;
                       };
                       UiPrefs& p = uiPrefs_;
                       if (!parseMode("ao", p.viewAo) || !parseMode("shadows", p.viewShadows)) return {false, "ao and shadows are on, off or auto"};
                       p.aoStrength = std::clamp(float(argNumber(a, "ao_strength", p.aoStrength)), 0.0f, 1.0f);
                       p.aoSoftness = std::clamp(float(argNumber(a, "ao_softness", p.aoSoftness)), 0.0f, 1.0f);
                       p.shadowSoftness = std::clamp(float(argNumber(a, "shadow_softness", p.shadowSoftness)), 0.0f, 1.0f);
                       p.viewOcclusionLitSphere = argBool(a, "lit_sphere", p.viewOcclusionLitSphere);
                       applyViewEffects();
                       const RenderSettings& v = renderSettings_;
                       char buf[160];
                       std::snprintf(buf, sizeof buf, "View AO %s, shadows %s%s", v.ambientOcclusion ? "on" : "off",
                                     v.shadows ? "on" : "off", p.viewOcclusionLitSphere ? " (PBR and Lit Sphere)" : " (PBR)");
                       return {true, buf};
                   }});
    commands_.add({"light",
                   "The light of the Lit Sphere and studio shading (#80): turn it from where the material has it, set its "
                   "intensity, and lift the shadows with ambient light. Only changes what the view (and Save Render) "
                   "shows; saved in the project. (The PBR view has its own environment rotation and exposure.)",
                   {{"yaw", T::Number, "Degrees about the view's up axis, + to the right (-180 to 180; default 0).", false},
                    {"pitch", T::Number, "Degrees up (+) or down (-) (-90 to 90; default 0).", false},
                    {"intensity", T::Number, "Brightness, 0 to 3 (default 1).", false},
                    {"ambient", T::Number, "Ambient light that lifts the shadows, 0 to 1 (default 0).", false},
                    {"reset", T::Bool, "Back to the material's own light first.", false}},
                   [this](Document&, const Args& a) -> CommandResult {
                       RenderSettings& v = renderSettings_;
                       if (argBool(a, "reset", false)) {
                           const RenderSettings defaults;
                           v.lightYaw = defaults.lightYaw;
                           v.lightPitch = defaults.lightPitch;
                           v.lightIntensity = defaults.lightIntensity;
                           v.lightAmbient = defaults.lightAmbient;
                       }
                       double yaw = std::fmod(argNumber(a, "yaw", v.lightYaw), 360.0);
                       if (yaw > 180.0) yaw -= 360.0;
                       if (yaw < -180.0) yaw += 360.0;
                       v.lightYaw = float(yaw);
                       v.lightPitch = std::clamp(float(argNumber(a, "pitch", v.lightPitch)), -90.0f, 90.0f);
                       v.lightIntensity = std::clamp(float(argNumber(a, "intensity", v.lightIntensity)), 0.0f, 3.0f);
                       v.lightAmbient = std::clamp(float(argNumber(a, "ambient", v.lightAmbient)), 0.0f, 1.0f);
                       char buf[160];
                       std::snprintf(buf, sizeof buf, "Light: %.0f deg right, %.0f deg up, intensity %.2f, ambient %.2f",
                                     double(v.lightYaw), double(v.lightPitch), double(v.lightIntensity), double(v.lightAmbient));
                       return {true, buf};
                   }});
    commands_.add({"xray",
                   "X-Ray (#80, #91): draw the other visible Parts see-through so the active Part shows through them "
                   "(with only the active Part shown, it is see-through itself). Only changes the view.",
                   {{"on", T::Bool, "On or off (default: toggle).", false},
                    {"opacity", T::Number, "How solid the see-through Parts look, 0.05 to 1 (default 0.3).", false}},
                   [this](Document&, const Args& a) -> CommandResult {
                       RenderSettings& v = renderSettings_;
                       v.xray = argBool(a, "on", a.contains("opacity") ? v.xray : !v.xray);
                       v.xrayOpacity = std::clamp(float(argNumber(a, "opacity", v.xrayOpacity)), 0.05f, 1.0f);
                       return {true, v.xray ? "X-Ray on" : "X-Ray off"};
                   }});
    commands_.add({"view_resolution",
                   "The 3D view's resolution as a share of the window's (#80): 100 (full), 75 or 50. Lower draws a "
                   "quarter to half the pixels and scales the picture up: faster on slow or integrated GPUs. Saved "
                   "with the preferences. Save Render always renders at full resolution.",
                   {{"percent", T::Number, "100, 75 or 50.", true}},
                   [this](Document&, const Args& a) -> CommandResult {
                       const int p = int(std::lround(argNumber(a, "percent", 100.0)));
                       if (p != 100 && p != 75 && p != 50) return {false, "View resolution is 100, 75 or 50 (percent)"};
                       uiPrefs_.viewResolution = p;
                       return {true, "View resolution " + std::to_string(p) + "%"};
                   }});
    commands_.add({"save_render",
                   "Save Render: render the view without the interface to an image in your library (Library > Image), "
                   "or to `path`. Choose the size and the shape: the shape's picture shows at least what the view shows.",
                   {{"size", T::Number, "The longer side in pixels, 256 to 8192 (default: twice the view, at most 4096).", false},
                    {"aspect", T::String, "view (default), 1:1, 16:9, 9:16 or 4:3, or a width / height ratio such as 2.35.", false},
                    {"format", T::String, "png (default) or jpg.", false},
                    {"path", T::String, "Where to write it (default: a new file in <library>/Images).", false}},
                   [this](Document&, const Args& a) -> CommandResult {
                       RenderRequest r;
                       r.size = int(std::lround(argNumber(a, "size", 0.0)));
                       if (r.size != 0 && (r.size < 256 || r.size > 8192)) return {false, "Save Render: size is 256 to 8192 pixels"};
                       const std::string aspect = argString(a, "aspect", "view");
                       bool known = false;
                       for (const RenderAspect& k : kAspects)
                           if (aspect == k.id) {
                               r.aspect = k.ratio;
                               known = true;
                           }
                       if (!known) {
                           char* end = nullptr;
                           const double ratio = std::strtod(aspect.c_str(), &end);
                           if (end == aspect.c_str() || *end || !(ratio >= 0.1 && ratio <= 10.0))
                               return {false, "Save Render: aspect is view, 1:1, 16:9, 9:16, 4:3 or a ratio (0.1 to 10)"};
                           r.aspect = float(ratio);
                       }
                       const std::string format = argString(a, "format", "png");
                       if (format != "png" && format != "jpg" && format != "jpeg") return {false, "Save Render: format is png or jpg"};
                       r.jpg = format != "png";
                       r.path = argString(a, "path");
                       std::string message;
                       const bool ok = saveRender(r, message);
                       return {ok, message};
                   }});

    // View > Orbit lock (a request from foirgairwolf on Discord): which way a rotation drag turns the view.
    commands_.add({"orbit_lock",
                   "Orbit lock (View > Orbit lock): how a rotation drag turns the view. free: about the screen's axes "
                   "(it can roll over the top); y: a turntable, upright, about the vertical axis; x or z: about that "
                   "world axis only. The axis gizmo shows the lock; saved with the preferences.",
                   {{"axis", T::String, "free, x, y or z.", true}},
                   [this](Document&, const Args& a) -> CommandResult {
                       const int lock = parseOrbitLock(argString(a, "axis"));
                       if (lock < 0) return {false, "orbit_lock: axis is free, x, y or z"};
                       uiPrefs_.orbitLock = lock;
                       static constexpr const char* said[4] = {"Orbit lock: free", "Orbit lock: Y (turntable)",
                                                               "Orbit lock: X axis", "Orbit lock: Z axis"};
                       return {true, said[lock]};
                   }});

    auto add = [this](std::string id, std::string label, const char* icon, std::string description, std::string palette,
                      std::function<void()> run, std::function<bool()> checked, std::string shortcut = "") {
        actions_.add({std::move(id), std::move(label), icon, std::move(description), std::move(palette),
                      std::move(shortcut), "", std::move(run), std::move(checked), nullptr});
    };
    auto lockRun = [this](int lock) { return [this, lock] { runCommand("orbit_lock", {{"axis", std::string(orbitLockName(lock))}}); }; };
    auto lockOn = [this](int lock) { return [this, lock] { return uiPrefs_.orbitLock == lock; }; };
    add("view.orbit_lock_free", "Free", nullptr,
        "Rotation drags turn the view about the screen's axes and can roll it over the top (the "
        "default). Saved with the preferences.",
        "View > Orbit lock", lockRun(0), lockOn(0), "Alt+Shift+F");
    add("view.orbit_lock_y", "Y (turntable)", nullptr,
        "Rotation drags turn the view like a turntable: the model stays upright and turns about its "
        "vertical (Y) axis. Saved with the preferences.",
        "View > Orbit lock", lockRun(1), lockOn(1), "Alt+Shift+Y");
    add("view.orbit_lock_x", "X", nullptr,
        "Rotation drags turn the view about the world X axis only (red): from the front, an up or down drag tips "
        "the model towards you or away. Shift still snaps to a side view; the axis gizmo shows the lock. Saved with "
        "the preferences.",
        "View > Orbit lock", lockRun(2), lockOn(2), "Alt+Shift+X");
    add("view.orbit_lock_z", "Z", nullptr,
        "Rotation drags turn the view about the world Z axis only (blue): from the front, a drag around the middle "
        "turns the model like a wheel. Shift still snaps to a side view; the axis gizmo shows the lock. Saved with "
        "the preferences.",
        "View > Orbit lock", lockRun(3), lockOn(3), "Alt+Shift+Z");
    add("view.xray", "X-Ray", ICON_LC_SCAN, "See through the other Parts to the active one (again: solid). With only the "
        "active Part shown, it is see-through itself (#80, #91).", "Tool > Parts",
        [this] { runCommand("xray"); }, [this] { return renderSettings_.xray; });
    for (int p : {100, 75, 50})
        add("perf.res_" + std::to_string(p), std::to_string(p) + "%", nullptr,
            p == 100 ? "Draw the 3D view at the window's full resolution."
                     : "Draw the 3D view at " + std::to_string(p) + "% of the window's resolution and scale it up: " +
                           (p == 75 ? "about half the pixels" : "a quarter of the pixels") +
                           ", faster on slow GPUs, a little softer. Save Render stays at full resolution.",
            "Preferences > Performance", [this, p] { runCommand("view_resolution", {{"percent", double(p)}}); },
            [this, p] { return uiPrefs_.viewResolution == p; });
    for (int i = 0; i < kAspectCount; ++i)
        add(aspectActionId(i), kAspects[i].label, nullptr,
            i == 0 ? std::string("Save Render keeps the view's own shape.")
                   : std::string("Save Render makes a ") + kAspects[i].label + " picture.",
            "Material", [this, i] { renderAspect_ = i; }, [this, i] { return renderAspect_ == i; });
    add("render.format_png", "PNG", nullptr, "Save Render writes a PNG (lossless).", "Material", [this] { renderJpg_ = false; },
        [this] { return !renderJpg_; });
    add("render.format_jpg", "JPG", nullptr, "Save Render writes a JPG (smaller files, for sharing).", "Material",
        [this] { renderJpg_ = true; }, [this] { return renderJpg_; });
}

void App::runSaveRender() {
    Args a{{"aspect", std::string(kAspects[std::clamp(renderAspect_, 0, kAspectCount - 1)].id)},
           {"format", std::string(renderJpg_ ? "jpg" : "png")}};
    const int size = kRenderSizes[std::clamp(renderSize_, 0, int(std::size(kRenderSizes)) - 1)];
    if (size) a["size"] = double(size);
    runCommand("save_render", a);
}

// Material: Save Render with its size, shape and format.
void App::applyViewEffects() {
    const TierConfig tier = currentTierConfig();
#ifdef SF_GLES
    const bool headroom = false; // (phones and tablets: off unless chosen)
#else
    const bool headroom = tier.hwTier >= HwTier::Standard;
#endif
    RenderSettings& v = renderSettings_;
    v.ambientOcclusion = uiPrefs_.viewAo < 0 ? headroom : uiPrefs_.viewAo == 1;
    v.shadows = uiPrefs_.viewShadows < 0 ? headroom : uiPrefs_.viewShadows == 1;
    v.aoStrength = uiPrefs_.aoStrength;
    v.aoSoftness = uiPrefs_.aoSoftness;
    v.shadowSoftness = uiPrefs_.shadowSoftness;
    v.occlusionInLitSphere = uiPrefs_.viewOcclusionLitSphere;
    // While the view moves or a stroke runs: off where they would cost the frame, back when it is still.
    const bool busy = perf_.movingNow || doc_.sculptor.strokeActive() || strokeFinishing_;
    const double now = ImGui::GetTime();
    if (!busy) {
        viewEffectsDropped_ = false;
        viewEffectsBusyFrames_ = 0;
    } else {
        const double ms = (now - viewEffectsBusyAt_) * 1000.0;
        if (viewEffectsBusyFrames_ > 0) viewEffectsBusyMs_ = viewEffectsBusyFrames_ == 1 ? ms : 0.7 * viewEffectsBusyMs_ + 0.3 * ms;
        viewEffectsBusyAt_ = now;
        ++viewEffectsBusyFrames_;
        const double budgetMs = 1250.0 / double(std::clamp(tier.targetHz, 30, 60));
        if (tier.hwTier == HwTier::Floor || turbo_ || (viewEffectsBusyFrames_ > 4 && viewEffectsBusyMs_ > budgetMs))
            viewEffectsDropped_ = true;
    }
    if (viewEffectsDropped_) v.ambientOcclusion = v.shadows = false;
}

void App::drawViewEffectsControls(const char* idSuffix) {
    const char* path = "Preferences > Display";
    UiPrefs& p = uiPrefs_;
    auto mode = [&](const char* label, const char* key, int value, const char* help) {
        ImGui::SetNextItemWidth(-1);
        const char* names[] = {tr("Auto"), tr("Off"), tr("On")};
        const std::string preview = std::string(tr(label)) + ": " + names[value + 1];
        if (ImGui::BeginCombo(("##" + std::string(key) + idSuffix).c_str(), preview.c_str())) {
            for (int i = -1; i <= 1; ++i)
                if (ImGui::Selectable(names[i + 1], value == i))
                    runCommand("view_shadows", {{key, std::string(i < 0 ? "auto" : i ? "on" : "off")}});
            ImGui::EndCombo();
        }
        ui::HelpText(label, nullptr, help, path);
    };
    auto slider = [&](const char* label, const char* key, float* value, const char* fmt, const char* help) {
        ImGui::SetNextItemWidth(-1);
        ImGui::SliderFloat((std::string("##") + key + idSuffix).c_str(), value, 0.0f, 1.0f, tr(fmt));
        if (ImGui::IsItemDeactivatedAfterEdit()) runCommand("view_shadows", {{key, double(*value)}});
        else if (ImGui::IsItemActive()) applyViewEffects(); // (live while dragging)
        ui::HelpText(label, nullptr, help, path);
    };
    ImGui::TextDisabled("%s", tr("Shadows & AO"));
    mode("Shadows", "shadows", p.viewShadows,
         "Shadows cast by the key light: the nose on the cheek, the brow over the eyes. Auto: on when the graphics card "
         "has room for them (Standard tier and up). They step aside while you orbit or sculpt if the frame needs it.");
    if (renderSettings_.shadows || p.viewShadows == 1)
        slider("Shadow softness", "shadow_softness", &p.shadowSoftness, "Shadow softness %.2f", "How soft the shadows' edges are.");
    mode("Ambient occlusion", "ao", p.viewAo,
         "Creases and hollows darken where the light around the model can't reach. Auto: on when the graphics card has "
         "room for it (Standard tier and up).");
    if (renderSettings_.ambientOcclusion || p.viewAo == 1) {
        slider("AO strength", "ao_strength", &p.aoStrength, "AO strength %.2f", "How dark the occlusion gets.");
        slider("AO softness", "ao_softness", &p.aoSoftness, "AO softness %.2f", "How soft and wide the occlusion looks.");
    }
    bool lit = p.viewOcclusionLitSphere;
    if (ui::Checkbox((std::string(tr("With Lit Sphere shading")) + "##occlit" + idSuffix).c_str(), &lit))
        runCommand("view_shadows", {{"lit_sphere", lit}});
    ui::HelpText("With Lit Sphere shading", nullptr,
                 "Shadows and ambient occlusion with the Lit Sphere and studio shading too, not only in the PBR view.", path);
}

void App::drawSaveRenderControls() {
    ui::ActionButton(*this, "render.save_image", ImVec2(-1, 0), ui::Look::IconLabel);
    const int count = int(std::size(kRenderSizes));
    renderSize_ = std::clamp(renderSize_, 0, count - 1);
    auto sizeLabel = [](int s) { return s == 0 ? std::string("View x2") : std::to_string(s) + " px"; };
    ImGui::SetNextItemWidth(-1);
    if (ImGui::BeginCombo("##rendersize", (std::string(tr("Size")) + ": " + sizeLabel(kRenderSizes[renderSize_])).c_str())) {
        for (int i = 0; i < count; ++i)
            if (ImGui::Selectable(sizeLabel(kRenderSizes[i]).c_str(), i == renderSize_)) renderSize_ = i;
        ImGui::EndCombo();
    }
    ui::HelpText("Size", nullptr,
                 "Save Render's longer side: twice the view (at most 4096 pixels), or 1080, 1920, 2048 or 4096 pixels.",
                 "Material");
    ui::Segmented(*this, {"render.aspect_view", "render.aspect_1x1", "render.aspect_16x9"});
    ui::Segmented(*this, {"render.aspect_9x16", "render.aspect_4x3", "render.format_png", "render.format_jpg"});
}

// Material > Light: a pad to drag the light around the model, its intensity and ambient light.
void App::drawLightControls() {
    RenderSettings& v = renderSettings_;
    ImGui::Spacing();
    ImGui::TextDisabled("%s", tr("Light"));
    ImGui::BeginDisabled(v.pbr());
    const float s = uiScale_;
    const float side = std::floor(std::min(ImGui::GetFrameHeightWithSpacing() * 3.0f - ImGui::GetStyle().ItemSpacing.y,
                                           ImGui::GetContentRegionAvail().x * 0.4f));
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##lightpad", ImVec2(side, side));
    const bool held = ImGui::IsItemActive() && !v.pbr();
    const ImVec2 c(p0.x + side * 0.5f, p0.y + side * 0.5f);
    const float r = side * 0.5f - 2.0f * s;
    if (held) {
        const ImVec2 m = ImGui::GetIO().MousePos;
        float x = (m.x - c.x) / r, y = (c.y - m.y) / r;
        const float len = std::sqrt(x * x + y * y);
        if (len > 1.0f) {
            x /= len;
            y /= len;
        }
        const float z = std::sqrt(std::max(0.0f, 1.0f - x * x - y * y));
        v.lightYaw = glm::degrees(std::atan2(x, z));
        v.lightPitch = glm::degrees(std::asin(std::clamp(y, -1.0f, 1.0f)));
    }
    if (ImGui::IsItemDeactivated() && !v.pbr())
        runCommand("light", {{"yaw", double(v.lightYaw)}, {"pitch", double(v.lightPitch)}});
    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
        runCommand("light", {{"yaw", 0.0}, {"pitch", 0.0}});
    ui::HelpText("Light direction", nullptr,
                 "Drag on the ball to move the light around the model (the dot is the light); double-click puts it "
                 "back where the material has it. Lit Sphere and studio shading.",
                 kLightPath);
    // The pad: a ball lit from the light's direction (a few soft discs towards the light), and the light's dot.
    {
        const glm::mat3 rot = lightRotation(v.lightYaw, v.lightPitch);
        const glm::vec3 dir = rot * glm::vec3(0.0f, 0.0f, 1.0f);
        const ImVec2 dot(c.x + dir.x * r, c.y - dir.y * r);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddCircleFilled(c, r, IM_COL32(38, 38, 42, 255), 48);
        constexpr int kSteps = 10;
        for (int i = 1; i <= kSteps; ++i) {
            const float t = float(i) / float(kSteps);
            const float lift = dir.z < 0.0f ? 0.4f : 1.0f; // a light behind the model leaves it mostly dark
            const ImVec2 ci(c.x + (dot.x - c.x) * t * 0.7f, c.y + (dot.y - c.y) * t * 0.7f);
            const int g = int(44.0f + (205.0f - 44.0f) * t * t * lift);
            dl->AddCircleFilled(ci, r * (1.0f - 0.9f * t), IM_COL32(g, g, g + 4, 255), 40);
        }
        dl->AddCircle(c, r, IM_COL32(90, 90, 96, 255), 48, 1.0f * s);
        const ImU32 sun = held || ImGui::IsItemHovered() ? ui::color::accentHover : ui::color::accent;
        dl->AddCircleFilled(dot, 4.0f * s, sun, 16);
        dl->AddCircle(dot, 4.0f * s, IM_COL32(20, 20, 20, 255), 16, 1.0f * s);
    }
    ImGui::SameLine();
    ImGui::BeginGroup();
    auto slider = [&](const char* id, const char* key, float* value, float lo, float hi, const char* fmt, const char* title,
                      const char* help) {
        ImGui::SetNextItemWidth(-1);
        ui::SliderInside(id, value, lo, hi, fmt);
        if (ImGui::IsItemDeactivatedAfterEdit()) runCommand("light", {{key, double(*value)}});
        ui::HelpText(title, nullptr, help, kLightPath);
    };
    slider("##lightintensity", "intensity", &v.lightIntensity, 0.0f, 2.0f, tr("Intensity %.2f"), "Intensity",
           "How bright the light is: 1 is the material as made.");
    slider("##lightambient", "ambient", &v.lightAmbient, 0.0f, 1.0f, tr("Ambient %.2f"), "Ambient",
           "Even light from everywhere that lifts the shadows, so the dark side still reads.");
    if (ImGui::Button(tr("Reset##light"), ImVec2(-1, 0))) runCommand("light", {{"reset", true}});
    ui::HelpText("Reset", nullptr, "The material's own light again: direction, intensity and no ambient.", kLightPath);
    ImGui::EndGroup();
    ImGui::EndDisabled();
    if (v.pbr()) ImGui::TextDisabled("%s", tr("PBR: Viewport Shading's rotation and exposure"));
}

} // namespace sf
