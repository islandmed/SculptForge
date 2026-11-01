// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Vector displacement brushes (core/Vdm.h) in the app: Brush > Vector Displacement (the maps, loading EXRs,
// the VDM plane and Create VDM, Export EXR), their F3 actions, the DragDot stroke, and the DragRect stroke the
// VDM brush brings along while it is the brush (ZBrush's VDM brushes come set to DragRect).
#include "app/App.h"
#include "app/Ui.h"

#include <IconsLucide.h>

#include <algorithm>
#include <cmath>
#include <filesystem>

namespace sf {

namespace {

const SDL_DialogFileFilter kVdmFilters[] = {{"Vector displacement maps", "exr;sfvdm"}, {"All files", "*"}};
const SDL_DialogFileFilter kExrFilters[] = {{"OpenEXR", "exr"}};

void hint(const char* text) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("%s", text);
    ImGui::PopStyleColor();
}

void actionButton(App& app, const char* id, float width = -1.0f) {
    ui::ActionButton(app, id, ImVec2(width < 0.0f ? ImGui::GetContentRegionAvail().x : width, 0));
}

} // namespace

void SDLCALL App::onVdmChosen(void* userdata, const char* const* files, int) {
    wakeMainLoop(); // a file dialog answered (maybe on another thread): redraw now
    if (!files || !files[0]) return;
    auto* app = static_cast<App*>(userdata);
    std::lock_guard lock(app->vdmMutex_);
    app->pendingVdmPath_ = files[0];
}

void SDLCALL App::onVdmExportChosen(void* userdata, const char* const* files, int) {
    wakeMainLoop(); // a file dialog answered (maybe on another thread): redraw now
    if (!files || !files[0]) return;
    auto* app = static_cast<App*>(userdata);
    std::lock_guard lock(app->vdmMutex_);
    app->pendingVdmExport_ = files[0];
}

void App::registerVdmActions() {
    auto add = [this](const char* id, const char* label, const char* icon, std::function<void()> run,
                      std::function<bool()> enabled, const char* description, const char* command) {
        Action a;
        a.id = id;
        a.label = label;
        a.icon = icon;
        a.palette = "Brush > Vector Displacement";
        a.run = std::move(run);
        a.enabled = std::move(enabled);
        a.description = description;
        a.command = command;
        actions_.add(std::move(a));
    };
    add("vdm.load", "Load VDM...", ICON_LC_FOLDER_OPEN,
        [this] { SDL_ShowOpenFileDialog(onVdmChosen, this, window_, kVdmFilters, 2, nullptr, false); }, nullptr,
        "Load a vector displacement map for the VDM brush: a 32- or 16-bit OpenEXR from Blender's VDM Brush Baker (or "
        "any EXR whose RGB are X right, Y up, Z out of the surface, in brush radii) or a .sfvdm. It is kept in your "
        "brush library. You can also drop an .exr on the window.",
        "vdm_set");
    add("vdm.new_plane", "New VDM Plane", ICON_LC_GRID_3X3,
        [this] { runCommand("vdm_new_plane", {{"resolution", double(vdmPlaneRes_)}}); }, nullptr,
        "Add a flat VDM plane to sculpt a new brush on (ZBrush's VDM plane template): seen from the top, its border "
        "masked so it stays put; the other Parts are hidden. Sculpt anything (overhangs welcome), then Create VDM.",
        "vdm_new_plane");
    add("vdm.create", "Create VDM", ICON_LC_STAMP,
        [this] {
            runCommand("vdm_capture", {{"name", std::string(vdmName_)}, {"resolution", double(vdmCaptureSize_)}});
        },
        [this] { return doc_.mesh.hasUvs(); },
        "Turn the sculpted VDM plane into a vector displacement brush: how far every point moved from the flat grid "
        "becomes the map, saved in your brush library and picked as the VDM brush's map (ZBrush: Create brush from "
        "the VDM plane). Needs the VDM plane (its UVs record where each point started).",
        "vdm_capture");
    add("vdm.export", "Export EXR...", ICON_LC_DOWNLOAD,
        [this] { SDL_ShowSaveFileDialog(onVdmExportChosen, this, window_, kExrFilters, 1, nullptr); },
        [this] { return doc_.brushVdm != nullptr; },
        "Save the VDM brush's map as a 32-bit float OpenEXR, for Blender: add it as the texture of a Draw brush with "
        "Area Plane mapping and Vector Displacement on (Help > User Guide > Sculpting > Vector displacement brushes).",
        "vdm_export");
}

void App::pollVdm() {
    std::string load, save;
    {
        std::lock_guard lock(vdmMutex_);
        load.swap(pendingVdmPath_);
        save.swap(pendingVdmExport_);
    }
    if (!load.empty()) openPath(load); // vdm_set with the import options (App.cpp)
    if (!save.empty()) {
        if (std::filesystem::path(std::u8string(save.begin(), save.end())).extension().empty()) save += ".exr";
        runCommand("vdm_export", {{"path", save}});
    }
    // The VDM brush stamps with DragRect (or DragDot); the stroke picked before comes back with the next brush.
    const bool active = vdmBrushActive();
    if (active && !vdmWasActive_ && strokeType_ != StrokeType::DragRect && strokeType_ != StrokeType::DragDot) {
        vdmSavedStroke_ = strokeType_;
        strokeType_ = StrokeType::DragRect;
        vdmStrokeAuto_ = true;
    } else if (!active && vdmWasActive_) {
        if (vdmStrokeAuto_ && strokeType_ == StrokeType::DragRect) strokeType_ = vdmSavedStroke_;
        vdmStrokeAuto_ = false;
    }
    vdmWasActive_ = active;
}

void App::afterVdmCommand(const std::string& name, const CommandResult& r) {
    if (!r.ok) return;
    if (name == "vdm_set" || name == "vdm_capture") { // a new map: sculpt with it
        brush_ = BrushType::Vdm;
        doc_.activeBrush = -1;
        paintMode_ = false;
    } else if (name == "vdm_new_plane") { // sculpt it from the top, like the map sees it
        camera_.setOrientation(glm::angleAxis(-1.5707963f, glm::vec3(1.0f, 0.0f, 0.0f)));
        camera_.frame(doc_.mesh.bounds());
    }
}

// DragDot: one stamp of the Draw Size under the cursor; dragging slides it over the surface, and it stays where
// the button comes up.
void App::dragDotTo(float x, float y) {
    doc_.sculptor.restoreStroke(doc_.mesh, doc_.bvh);
    RayHit hit;
    if (!pick(x, y, hit)) return;
    const float pressure = strokePressure();
    DabParams p;
    p.center = hit.position;
    p.viewDir = camera_.forward();
    p.radius = radiusPx_ * (pressureSize_ ? std::max(pressure, 0.05f) : 1.0f) *
               camera_.worldPerPixel(hit.position, canvasHeight());
    p.strength = strokeBrush_ == BrushType::Smooth ? smoothStrength_ : strength_;
    p.pressure = pressureStrength_ ? pressure : 1.0f;
    p.invert = strokeInvert_;
    p.frontFacesOnly = frontFacesOnly_;
    p.connectedOnly = uiPrefs_.connectedOnly;
    p.viewFalloff = uiPrefs_.viewFalloff;
    p.focal = focalShift_;
    p.alpha = &doc_.brushAlpha;
    p.alphaUp = screenUpVector(0.0f);
    p.vdm = doc_.brushVdm.get();
    lastDabScreen_ = glm::vec2(x, y);
    if (strokePaint_) paintAt(p); // restoreStroke reverted the previous stamp's colours too
    if (!strokeSculpt_) return;
    if (const BrushRecipe* recipe = strokeRecipe()) doc_.sculptor.dabRecipe(doc_.mesh, doc_.bvh, *recipe, p, symmetry_);
    else doc_.sculptor.dab(doc_.mesh, doc_.bvh, strokeBrush_, p, symmetry_);
}

// Brush > Vector Displacement.
void App::drawVdmSection() {
    const float s = uiScale_;
    const std::shared_ptr<const VectorDisplacement>& vdm = doc_.brushVdm;
    // The brush's map, large, with its name and size.
    const float big = std::min(128.0f * s, ImGui::GetContentRegionAvail().x);
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (ImGui::GetContentRegionAvail().x - big) * 0.5f);
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(big, big));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p0, ImVec2(p0.x + big, p0.y + big), IM_COL32(42, 42, 46, 255), 4.0f * s);
    if (ImTextureID tex = thumbs_.vdm(vdm)) dl->AddImageRounded(tex, p0, ImVec2(p0.x + big, p0.y + big), ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, 4.0f * s);
    if (vdmBrushActive()) dl->AddRect(p0, ImVec2(p0.x + big, p0.y + big), ui::color::accent, 4.0f * s, 0, 2.0f * s);
    if (vdm) {
        const std::string info = vdm->name + "  (" + std::to_string(vdm->width) + " px)";
        const ImVec2 size = ImGui::CalcTextSize(info.c_str());
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (ImGui::GetContentRegionAvail().x - size.x) * 0.5f);
        ImGui::TextUnformatted(info.c_str());
    }
    if (!vdmBrushActive()) {
        actionButton(*this, "brush.vdm");
    }
    drawVdmGrid(*this, GridMode::Palette);

    ImGui::SeparatorText("Load");
    actionButton(*this, "vdm.load");
    ImGui::Checkbox("Swap Y/Z##vdm", &vdmImport_.swapYZ);
    ui::HelpText("Swap Y/Z", nullptr,
                 "For maps that store the surface normal in G instead of B (Y-up apps). Blender's maps and ours need "
                 "nothing; ZBrush maps depend on its Vector Displacement Flip options, so try this if a stamp comes "
                 "out lying on its side.",
                 "Brush > Vector Displacement");
    ImGui::SameLine();
    ImGui::Checkbox("Flip X##vdm", &vdmImport_.flipX);
    ui::HelpText("Flip X", nullptr, "Mirror the map's sideways (R) channel when loading: for a stamp that comes out mirrored.",
                 "Brush > Vector Displacement");
    ImGui::SameLine();
    ImGui::Checkbox("Flip Y##vdm", &vdmImport_.flipY);
    ui::HelpText("Flip Y", nullptr, "Mirror the map's up (G) channel when loading: for a stamp that leans the wrong way.",
                 "Brush > Vector Displacement");
    ImGui::Checkbox("Flip Z##vdm", &vdmImport_.flipZ);
    ui::HelpText("Flip Z", nullptr, "Negate the map's height (B) when loading: for a map that digs in where it should rise.",
                 "Brush > Vector Displacement");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1);
    ImGui::SliderFloat("##vdmscale", &vdmImport_.scale, 0.1f, 10.0f, "Scale %.2f", ImGuiSliderFlags_Logarithmic);
    ui::HelpText("Scale", nullptr,
                 "Multiply a loaded map's vectors: for maps saved in other units than the brush radius (1 = as stored).",
                 "Brush > Vector Displacement");

    ImGui::SeparatorText("Create");
    ImGui::SetNextItemWidth(-1);
    ImGui::SliderInt("##vdmplane", &vdmPlaneRes_, 64, 1024, "Plane Res %d");
    ui::HelpText("Plane Res", nullptr,
                 "Quads per side of a new VDM plane. 256 is a good start; divide it (Ctrl+D) for finer detail.",
                 "Brush > Vector Displacement");
    actionButton(*this, "vdm.new_plane");
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##vdmname", "Name of the new VDM", vdmName_, sizeof vdmName_);
    ui::HelpText("VDM name", nullptr, "What Create VDM calls the new map (a map of the same name is replaced).",
                 "Brush > Vector Displacement");
    ImGui::SetNextItemWidth(-1);
    ImGui::SliderInt("##vdmsize", &vdmCaptureSize_, 64, 1024, "Map Size %d px");
    ui::HelpText("Map Size", nullptr,
                 "Pixels a side of the map Create VDM makes. 256 holds most shapes; 512 for fine detail (Blender suggests "
                 "512 or less).",
                 "Brush > Vector Displacement");
    actionButton(*this, "vdm.create");
    actionButton(*this, "vdm.export");
    hint("Stamp with Drag Stamp (drag sets size and turn) or Slide Stamp. Intensity 50 presses the map at its own size; "
         "Alt digs it in. A VDM moves points sideways, so it needs a dense mesh: divide first, and stamp at the "
         "highest subdivision level.");
}

} // namespace sf
