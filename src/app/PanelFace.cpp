// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Tool > Facial Shapes (core/Face.h): Generate Facial Shapes (the 52 ARKit blend shapes), landmarks clicked on the
// canvas to guide the fit, the Face test (expression presets and a slider per shape), and recorded takes (play,
// bake for export).
#include "app/App.h"
#include "app/Demo.h"
#include "app/Palettes.h"
#include "app/Ui.h"
#include "core/Face.h"

#include <IconsLucide.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>

namespace sf {

namespace {

constexpr const char* kFacePath = "Tool > Facial Shapes";
const SDL_DialogFileFilter kTakeFilters[] = {{"Face takes (JSON)", "json;jsonl"}, {"All files", "*"}};

void dimText(const char* text) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("%s", text);
    ImGui::PopStyleColor();
}

bool ctrlHeld(SDL_Keymod mod) { return (mod & (SDL_KMOD_CTRL | SDL_KMOD_GUI)) != 0; }

void runAction(const ActionRegistry& actions, const char* id) {
    if (const Action* a = actions.find(id); a && a->run && (!a->enabled || a->enabled())) a->run();
}

void actionButton(App& app, const char* id, float width = -1.0f) {
    ui::ActionButton(app, id, ImVec2(width < 0.0f ? ImGui::GetContentRegionAvail().x : width, 0));
}

// The shapes grouped for the sliders.
struct ShapeGroup {
    const char* title;
    std::vector<const char*> prefixes;
};
const ShapeGroup kGroups[] = {{"Eyes", {"eye"}},        {"Brows", {"brow"}},       {"Jaw", {"jaw"}},
                              {"Mouth", {"mouth"}},      {"Cheeks & nose", {"cheek", "nose"}}, {"Tongue", {"tongue"}}};

} // namespace

void SDLCALL App::onFaceTakeChosen(void* userdata, const char* const* files, int) {
    if (!files || !files[0]) return;
    auto* app = static_cast<App*>(userdata);
    std::lock_guard lock(app->faceTakeMutex_);
    app->pendingFaceTake_ = files[0];
}

bool App::hasFaceShapes() const {
    for (const auto& l : doc_.mesh.layers)
        if (arkitBlendShapeIndex(l.name) >= 0) return true;
    return false;
}

void App::registerFaceUiActions() {
    auto add = [this](const char* id, const char* label, const char* icon, std::function<void()> run,
                      std::function<bool()> enabled, const char* description, const char* command) {
        Action a;
        a.id = id;
        a.label = label;
        a.icon = icon;
        a.palette = kFacePath;
        a.run = std::move(run);
        a.enabled = std::move(enabled);
        a.description = description;
        a.command = command;
        actions_.add(std::move(a));
    };
    add("face.generate", "Generate Facial Shapes", ICON_LC_SMILE, [this] { generateFaceShapesUi(); },
        [this] { return doc_.mesh.triangleCount() > 0; },
        "Give the active SubTool (a humanoid head, looking down +Z in the front view) the 52 Apple ARKit blend shapes "
        "as sculpt layers: eyeBlinkLeft, jawOpen, mouthSmileRight and the rest, fitted to this head's proportions. "
        "Then fix any of them by sculpting into its layer (Tool > Layers), test them with the presets below, export "
        "them as morph targets (GLB, USD, FBX, Blender) or drive them live from your phone. The landmarks you placed "
        "(at least 3) guide the fit; otherwise it finds the face by itself. Eyes, teeth and tongue SubTools (by name) "
        "and SubTools on the face get the shapes they need. Running it again replaces the 52 layers (undo brings back "
        "the old ones and the fixes sculpted into them).",
        "face_shapes_generate");
    add("face.landmarks", "Place Landmarks", ICON_LC_CROSSHAIR,
        [this] {
            facePlacing_ = !facePlacing_;
            if (facePlacing_) {
                facePlaceIndex_ = 0;
                while (facePlaceIndex_ < kFaceLandmarkCount && faceLandmarks_.set[size_t(facePlaceIndex_)]) ++facePlaceIndex_;
                if (facePlaceIndex_ >= kFaceLandmarkCount) facePlaceIndex_ = 0;
                status_ = std::string("Click the ") + faceLandmarkLabel(facePlaceIndex_) + " (the character's own side)";
            } else {
                status_ = std::to_string(faceLandmarks_.count()) + " landmarks placed";
            }
        },
        nullptr,
        "Guide the fit for heads it gets wrong (stylised proportions, a turned head, a bust with big shoulders): click "
        "the eye corners, the nose tip, the mouth corners and the chin on the sculpt, in the order the canvas asks. "
        "Right and left are the character's own (its right eye is on your left in the front view). Click a dot again "
        "later to move it. 3 are enough to start; all 8 fit best. Auto places them all for you to check.",
        "");
    add("face.landmarks_auto", "Auto Landmarks", ICON_LC_SCAN_FACE,
        [this] {
            std::vector<glm::uvec3> tris;
            for (uint32_t t = 0; t < doc_.mesh.triangleCount(); ++t)
                if (!doc_.mesh.triangleHidden(t)) tris.push_back(doc_.mesh.triangles[t]);
            FaceFit fit;
            std::string error;
            if (!fitFaceTemplate(doc_.mesh.basePositions(), tris, nullptr, fit, error)) {
                status_ = "Auto landmarks: " + error;
                return;
            }
            faceLandmarks_ = fit.landmarks;
            char buf[160];
            std::snprintf(buf, sizeof buf, "Landmarks found (%.0f%% of the template's face on the sculpt): check them, "
                                           "click Place Landmarks to move any",
                          double(fit.coverage * 100.0f));
            status_ = buf;
        },
        [this] { return doc_.mesh.triangleCount() > 0; },
        "Find the landmarks automatically (the same fit Generate makes without landmarks) and show them, so you can "
        "check where the eye and mouth corners landed and move any that are off before generating.",
        "face_landmarks_detect");
    add("face.landmarks_clear", "Clear Landmarks", ICON_LC_ERASER,
        [this] {
            faceLandmarks_ = FaceFitLandmarks{};
            facePlacing_ = false;
            status_ = "Landmarks cleared: Generate fits automatically";
        },
        [this] { return faceLandmarks_.count() > 0; }, "Remove the landmarks: Generate then finds the face by itself.", "");
    add("face.take_load", "Load Take...", ICON_LC_FOLDER_OPEN,
        [this] { SDL_ShowOpenFileDialog(onFaceTakeChosen, this, window_, kTakeFilters, 2, nullptr, false); }, nullptr,
        "Play a recorded face take on the facial shapes: the JSON frames the phone link records ({\"type\":\"face\", "
        "\"t\":ms, \"weights\":{...}} as an array, {\"frames\":[...]} or JSON Lines).",
        "face_take_play");
    add("face.take_play", "Play Take", ICON_LC_PLAY,
        [this] {
            faceTakePlaying_ = !faceTakePlaying_;
            if (faceTakePlaying_ && faceTakeTime_ >= faceTake_.duration()) faceTakeTime_ = 0.0f;
        },
        [this] { return !faceTake_.empty(); }, "Play or pause the loaded take (it loops while Loop is on).", "");
    add("face.take_bake", "Bake Take", ICON_LC_FILM,
        [this] { runCommand("face_take_bake", {{"path", faceTakePath_}}); },
        [this] { return !faceTakePath_.empty(); },
        "Bake the loaded take into keyframes at 30 fps: Export GLB writes them as a glTF animation of the morph target "
        "weights, Export USD as time samples (Blender, Maya, Unreal, Omniverse play them). Saved with the project.",
        "face_take_bake");
    add("face.take_clear", "Clear Baked Take", ICON_LC_TRASH_2, [this] { runCommand("face_take_clear"); },
        [this] {
            MorphAnimation a;
            return bakedFaceAnimation(doc_, a);
        },
        "Remove the baked take, so exports are a still pose again.", "face_take_clear");

    // Commands the app adds: playback needs the frame clock.
    commands_.add({"face_take_play",
                   "Plays a recorded face take (JSON frames from the phone link) on the facial shapes in the viewport, "
                   "looping. Needs the app (not the CLI).",
                   {{"path", ParamSpec::Type::String, "The take file.", true},
                    {"loop", ParamSpec::Type::Bool, "Loop (default true).", false}},
                   [this](Document&, const Args& a) -> CommandResult {
                       MorphAnimation take;
                       std::string error;
                       const std::string path = argString(a, "path");
                       if (!loadFaceTake(path, take, error)) return {false, error};
                       faceTake_ = std::move(take);
                       faceTakePath_ = path;
                       faceTakeTime_ = 0.0f;
                       faceTakeLoop_ = argBool(a, "loop", true);
                       faceTakePlaying_ = true;
                       char buf[96];
                       std::snprintf(buf, sizeof buf, " (%.1f s, %zu frames)", double(faceTake_.duration()), faceTake_.times.size());
                       return {true, "Playing take '" + faceTake_.name + "'" + buf};
                   }});
    commands_.add({"face_take_stop", "Stops the face take playing in the viewport.", {},
                   [this](Document&, const Args&) -> CommandResult {
                       faceTakePlaying_ = false;
                       return {true, "Take stopped"};
                   }});
}

void App::generateFaceShapesUi() {
    Args args;
    if (faceLandmarks_.count() >= 3) args["landmarks"] = faceLandmarks_.toJson().dump();
    args["symmetry"] = std::string(faceSymmetry_ == 1 ? "on" : faceSymmetry_ == 2 ? "off" : "auto");
    args["subtools"] = faceSubtools_;
    facePlacing_ = false;
    runCommand("face_shapes_generate", args);
}

void App::animateFaceTake(float dt) {
    std::string pending;
    {
        std::lock_guard lock(faceTakeMutex_);
        pending.swap(pendingFaceTake_);
    }
    if (!pending.empty()) runCommand("face_take_play", {{"path", pending}});
    if (!faceTakePlaying_ || faceTake_.empty()) return;
    faceTakeTime_ += dt;
    if (faceTakeTime_ > faceTake_.duration()) {
        if (faceTakeLoop_ && faceTake_.duration() > 0.0f) {
            faceTakeTime_ = std::fmod(faceTakeTime_, faceTake_.duration());
        } else {
            faceTakeTime_ = faceTake_.duration();
            faceTakePlaying_ = false;
        }
    }
    std::vector<std::pair<std::string, float>> w;
    sampleMorphAnimation(faceTake_, faceTakeTime_, w);
    applyFaceWeights(doc_, w);
}

bool App::faceMouseDown(float x, float y, SDL_Keymod mod) {
    if (!facePlacing_ || (mod & (SDL_KMOD_ALT | SDL_KMOD_SHIFT)) || ctrlHeld(mod)) return false;
    RayHit hit;
    if (previewLod_ >= 0 || !pick(x, y, hit)) return false; // the background navigates
    // Clicking near a placed dot picks that one up to move it; otherwise the next one is placed.
    const float grab = 10.0f * uiScale_;
    for (int i = 0; i < kFaceLandmarkCount; ++i) {
        ImVec2 sp;
        if (faceLandmarks_.set[size_t(i)] && i != facePlaceIndex_ && gizmoProject(faceLandmarks_.position[size_t(i)], sp) &&
            std::hypot(sp.x - x, sp.y - y) < grab) {
            facePlaceIndex_ = i;
            status_ = std::string("Click the new place for the ") + faceLandmarkLabel(i);
            return true;
        }
    }
    faceLandmarks_.position[size_t(facePlaceIndex_)] = hit.position;
    faceLandmarks_.set[size_t(facePlaceIndex_)] = true;
    int next = -1;
    for (int k = 1; k <= kFaceLandmarkCount; ++k) {
        const int i = (facePlaceIndex_ + k) % kFaceLandmarkCount;
        if (!faceLandmarks_.set[size_t(i)]) {
            next = i;
            break;
        }
    }
    if (next < 0) {
        facePlacing_ = false;
        status_ = "All 8 landmarks placed: Generate Facial Shapes uses them";
    } else {
        facePlaceIndex_ = next;
        status_ = std::to_string(faceLandmarks_.count()) + "/8 placed. Click the " + faceLandmarkLabel(next);
    }
    return true;
}

void App::drawFaceOverlay() {
    if (faceLandmarks_.count() == 0 && !facePlacing_) return;
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const CanvasRect cr = canvasRect();
    dl->PushClipRect(ImVec2(cr.x, cr.y), ImVec2(cr.x + cr.w, cr.y + cr.h), true);
    const float s = uiScale_;
    for (int i = 0; i < kFaceLandmarkCount; ++i) {
        if (!faceLandmarks_.set[size_t(i)]) continue;
        ImVec2 p;
        if (!gizmoProject(faceLandmarks_.position[size_t(i)], p)) continue;
        const bool current = facePlacing_ && i == facePlaceIndex_;
        dl->AddCircleFilled(p, 5.0f * s, current ? IM_COL32(255, 196, 64, 255) : IM_COL32(80, 200, 255, 255));
        dl->AddCircle(p, 5.0f * s, IM_COL32(0, 0, 0, 200), 0, 1.5f * s);
        char num[4];
        std::snprintf(num, sizeof num, "%d", i + 1);
        dl->AddText(ImVec2(p.x + 7.0f * s, p.y - 8.0f * s), IM_COL32(255, 255, 255, 230), num);
    }
    if (facePlacing_) {
        char banner[160];
        std::snprintf(banner, sizeof banner, "Landmark %d/8: click the %s  (the character's own side; its right is on "
                                             "your left from the front)",
                      facePlaceIndex_ + 1, faceLandmarkLabel(facePlaceIndex_));
        const ImVec2 size = ImGui::CalcTextSize(banner);
        const ImVec2 at(cr.x + (cr.w - size.x) * 0.5f, cr.y + 12.0f * s);
        dl->AddRectFilled(ImVec2(at.x - 8 * s, at.y - 4 * s), ImVec2(at.x + size.x + 8 * s, at.y + size.y + 4 * s),
                          IM_COL32(20, 20, 24, 210), 4.0f * s);
        dl->AddText(at, IM_COL32(255, 196, 64, 255), banner);
    }
    dl->PopClipRect();
}

void App::drawFaceContents() {
    const float s = uiScale_;
    dimText("The 52 Apple ARKit blend shapes for rigs and live face capture, fitted to this head.");
    actionButton(*this, "face.generate");
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.5f);
    const char* symmetry[] = {"Auto", "On", "Off"};
    ImGui::Combo("Symmetry##face", &faceSymmetry_, symmetry, 3);
    ui::HelpText("Symmetry", nullptr,
                 "Auto: Left and Right shapes are made exact mirror images when the sculpt is symmetric about X = 0. "
                 "On: always (asymmetric spots take the average). Off: each side as fitted.",
                 kFacePath);
    ImGui::Checkbox("SubTools too##face", &faceSubtools_);
    ui::HelpText("SubTools too", nullptr,
                 "Eyes (SubTools named Eye...) turn for the eyeLook shapes, Teeth / Tongue follow the jaw below the "
                 "bite (and tongueOut), and other SubTools on the face (brows, lashes, a beard) follow the skin.",
                 kFacePath);

    // Landmarks.
    tray::GroupLabel(*this, "Landmarks");
    const float half = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
    actionButton(*this, "face.landmarks", half);
    ImGui::SameLine();
    actionButton(*this, "face.landmarks_auto", half);
    char placed[64];
    std::snprintf(placed, sizeof placed, "%d/8 placed%s", faceLandmarks_.count(),
                  faceLandmarks_.count() >= 3 ? " (guiding the fit)" : faceLandmarks_.count() ? " (need 3)" : " (automatic fit)");
    ImGui::TextDisabled("%s", placed);
    if (faceLandmarks_.count()) {
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 40 * s);
        if (ImGui::SmallButton("Clear##facelm")) runAction(actions_, "face.landmarks_clear");
        ui::HelpAction(*this, "face.landmarks_clear");
    }

    // Face test.
    tray::GroupLabel(*this, "Face test");
    const bool shapes = hasFaceShapes();
    if (!shapes) dimText("Generate the shapes first.");
    ImGui::BeginDisabled(!shapes);
    const auto& presets = facePresets();
    const float cell = (ImGui::GetContentRegionAvail().x - 3 * ImGui::GetStyle().ItemSpacing.x) / 4.0f;
    for (size_t i = 0; i < presets.size(); ++i) {
        if (i % 4) ImGui::SameLine();
        if (ImGui::Button(presets[i].name, ImVec2(cell, 0)))
            runCommand("face_preset", {{"name", std::string(presets[i].name)}, {"amount", double(facePresetAmount_)}});
        ui::HelpText(presets[i].name, nullptr, presets[i].help, kFacePath);
    }
    ImGui::SetNextItemWidth(-1);
    ui::SliderInside("##faceamount", &facePresetAmount_, 0.0f, 1.0f, "preset amount %.2f");
    ui::HelpText("Preset amount", nullptr, "How strongly the presets above are applied (click a preset again to see it).",
                 kFacePath);
    // A slider per shape, by region.
    for (const ShapeGroup& g : kGroups) {
        if (!ImGui::TreeNode(g.title)) continue;
        for (auto& l : doc_.mesh.layers) {
            if (arkitBlendShapeIndex(l.name) < 0) continue;
            const bool mine = std::any_of(g.prefixes.begin(), g.prefixes.end(),
                                          [&](const char* p) { return l.name.rfind(p, 0) == 0; });
            if (!mine) continue;
            float w = l.weight;
            ImGui::SetNextItemWidth(-1);
            if (ui::SliderInside(("##" + l.name).c_str(), &w, 0.0f, 1.0f, (l.name + " %.2f").c_str())) {
                faceTakePlaying_ = false;
                applyFaceWeights(doc_, {{l.name, w}});
            }
            ui::HelpText(l.name.c_str(), nullptr,
                         "This ARKit shape's weight (it drives the same-named layer on every SubTool). To fix the shape, "
                         "select its layer in Tool > Layers, set the weight to 1 and sculpt.",
                         kFacePath);
        }
        ImGui::TreePop();
    }
    ImGui::EndDisabled();

    // Takes.
    tray::GroupLabel(*this, "Take");
    actionButton(*this, "face.take_load");
    if (!faceTake_.empty()) {
        ImGui::TextDisabled("%s: %.1f / %.1f s", faceTake_.name.c_str(), double(faceTakeTime_), double(faceTake_.duration()));
        if (ImGui::Button(faceTakePlaying_ ? "Pause##facetake" : "Play##facetake", ImVec2(half, 0))) runAction(actions_, "face.take_play");
        ui::HelpAction(*this, "face.take_play");
        ImGui::SameLine();
        ImGui::Checkbox("Loop##facetake", &faceTakeLoop_);
        ui::HelpText("Loop", nullptr, "Play the take again from the start when it ends.", kFacePath);
        actionButton(*this, "face.take_bake");
    }
    MorphAnimation baked;
    if (bakedFaceAnimation(doc_, baked)) {
        ImGui::TextDisabled("Baked: %s (%.1f s, exports with GLB / USD)", baked.name.c_str(), double(baked.duration()));
        actionButton(*this, "face.take_clear");
    }
}

} // namespace sf
