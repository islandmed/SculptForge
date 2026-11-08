// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Tool > Layers: the sculpt layer stack of the active Part (blend shapes). From the top: the strength for all
// layers, the list (Base mesh, then layer 0 down; click makes a layer the one strokes go into, Ctrl+click adds it
// to the selection, Shift+click selects a run, double-click renames, right-click for more; each row has its
// weight, Play and the eye), the active layer's weight, then Up / Down / Merge down / Duplicate / Invert, Bake /
// Delete for the selection with More (top, bottom, rename, all layers), and the name field with + Add. Every change
// goes through the layer_* commands (core/SculptLayers.cpp), one undo step each; sliders preview live and commit
// on release.
#include "app/App.h"
#include "app/Ui.h"
#include "app/I18n.h"

#include "core/SculptLayers.h"

#include <IconsLucide.h>

#include <algorithm>
#include <cstdio>
#include <string>

namespace sf {

namespace {

constexpr int kSculptListRows = 8; // the list scrolls beyond this many rows
constexpr const char* kLayersPath = "Tool > Layers";

} // namespace

std::vector<int> App::selectedSculptLayers() const {
    std::vector<int> out;
    const auto& layers = doc_.mesh.layers;
    for (int i = 0; i < int(layers.size()); ++i)
        if (sculptLayerSelection_.count(layers[size_t(i)].name)) out.push_back(i);
    if (out.empty() && doc_.activeLayer >= 0 && doc_.activeLayer < int(layers.size())) out.push_back(doc_.activeLayer);
    return out;
}

void App::drawLayersContents() {
    const float scale = uiScale_;
    const ImGuiStyle& style = ImGui::GetStyle();
    const Mesh& mesh = doc_.mesh;
    const int count = int(mesh.layers.size());
    const int active = doc_.activeLayer >= 0 && doc_.activeLayer < count ? doc_.activeLayer : -1;
    // The selection by name (names are unique per Part), so it follows a layer that moves; gone layers drop out.
    for (auto it = sculptLayerSelection_.begin(); it != sculptLayerSelection_.end();) {
        const bool exists = std::any_of(mesh.layers.begin(), mesh.layers.end(), [&](const SculptLayer& l) { return l.name == *it; });
        it = exists ? std::next(it) : sculptLayerSelection_.erase(it);
    }
    // The active layer is always selected (a script or the AI may have made another layer active).
    if (active >= 0 && !sculptLayerSelection_.count(mesh.layers[size_t(active)].name)) {
        sculptLayerSelection_.clear();
        sculptLayerSelection_.insert(mesh.layers[size_t(active)].name);
    }
    ImGui::TextDisabled("%s", tr("Sculpt expressions & correctives;\nexported as glTF morph targets."));

    // Live preview while a slider drags; the command records the value from before the drag on release.
    auto previewWeight = [this](int i, float w) {
        if (playingLayer_ == i) playingLayer_ = -1;
        setLayerWeight(i, w);
    };

    // Strength for all layers.
    if (count > 0) {
        float all = mesh.layerStrength;
        ImGui::SetNextItemWidth(-1);
        if (ui::SliderInside("##allstrength", &all, -1.0f, 1.0f, tr("All layers %.2f"), ImGuiSliderFlags_AlwaysClamp)) {
            if (allStrengthDrag_ < -1.5f) allStrengthDrag_ = mesh.layerStrength;
            setLayerStrength(all);
        }
        if (ImGui::IsItemDeactivated() && allStrengthDrag_ >= -1.5f) {
            const float committed = doc_.mesh.layerStrength;
            setLayerStrength(allStrengthDrag_);
            allStrengthDrag_ = -2.0f;
            runCommand("layer_strength", {{"strength", double(committed)}});
        }
        ui::HelpText("Strength for all layers", nullptr,
                     "One strength for every layer of this Part: 1 = as sculpted, 0 = all layers off, negative = "
                     "the opposite way. Each layer's own weight is scaled by it.",
                     kLayersPath);
    }

    // The list. Actions run after the table (they change the list).
    int selectIndex = -2, toggleVisible = -1, togglePlay = -1, renameIndex = -1, contextAction = 0, contextIndex = -1;
    std::string renameTo;
    const ImVec2 cellPad(6.0f * scale, 3.0f * scale);
    const float rowH = ImGui::GetTextLineHeight() + 2.0f * cellPad.y;
    const float listH = rowH * float(std::min(count + 1, kSculptListRows)) + 2.0f;
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, cellPad);
    if (ImGui::BeginTable("##sculptlayers", 4, ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter,
                          ImVec2(-1, listH))) {
        const float iconW = ImGui::GetTextLineHeight() * 1.2f;
        ImGui::TableSetupColumn("name", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("weight", ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize("-0.00").x);
        ImGui::TableSetupColumn("play", ImGuiTableColumnFlags_WidthFixed, iconW);
        ImGui::TableSetupColumn("eye", ImGuiTableColumnFlags_WidthFixed, iconW);
        // Selected rows besides the active one: a light accent tint, so the selection reads apart from the active row.
        const ImVec4 accent = style.Colors[ImGuiCol_CheckMark];
        const ImVec4 selectedTint = ImVec4(accent.x, accent.y, accent.z, 0.22f);
        for (int i = -1; i < count; ++i) {
            const SculptLayer* l = i >= 0 ? &mesh.layers[size_t(i)] : nullptr;
            const std::string name = l ? l->name : tr("Base mesh");
            const bool isActive = i == active;
            const bool selected = l && !isActive && sculptLayerSelection_.count(l->name) > 0;
            ImGui::PushID(i);
            ImGui::TableNextRow(ImGuiTableRowFlags_None, rowH);
            ImGui::TableSetColumnIndex(0);
            if (renamingSculptLayer_ == i && l) {
                ImGui::SetNextItemWidth(-1);
                if (focusSculptRename_) {
                    ImGui::SetKeyboardFocusHere();
                    focusSculptRename_ = false;
                }
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(style.FramePadding.x, 0.0f));
                const bool enter = ImGui::InputText("##rename", sculptRenameBuffer_, sizeof sculptRenameBuffer_,
                                                    ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
                ImGui::PopStyleVar();
                if (enter || (ImGui::IsItemDeactivatedAfterEdit() && !ImGui::IsKeyPressed(ImGuiKey_Escape))) {
                    renameTo = sculptRenameBuffer_;
                    renameIndex = i;
                    renamingSculptLayer_ = -2;
                } else if (ImGui::IsItemDeactivated()) {
                    renamingSculptLayer_ = -2;
                }
            } else {
                const bool dim = l && !l->visible;
                if (dim) ImGui::PushStyleColor(ImGuiCol_Text, style.Colors[ImGuiCol_TextDisabled]);
                if (selected) ImGui::PushStyleColor(ImGuiCol_Header, selectedTint);
                if (ImGui::Selectable(name.c_str(), isActive || selected,
                                      ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap |
                                          ImGuiSelectableFlags_AllowDoubleClick | ImGuiSelectableFlags_NoAutoClosePopups)) {
                    if (l && ui::CtrlHeld()) {
                        // Ctrl+click: in or out of the selection (the active layer stays in it: strokes go there).
                        if (isActive || !sculptLayerSelection_.erase(l->name)) sculptLayerSelection_.insert(l->name);
                    } else if (l && ui::ShiftHeld() && active >= 0) {
                        sculptLayerSelection_.clear();
                        for (int k = std::min(active, i); k <= std::max(active, i); ++k)
                            sculptLayerSelection_.insert(mesh.layers[size_t(k)].name);
                    } else {
                        sculptLayerSelection_.clear();
                        if (l) sculptLayerSelection_.insert(l->name);
                        if (!isActive) selectIndex = i;
                    }
                }
                if (selected) ImGui::PopStyleColor();
                if (dim) ImGui::PopStyleColor();
                if (l)
                    ui::HelpText(l->name.c_str(), nullptr,
                                 "Select to sculpt into this layer; Ctrl+click or Shift+click to select several for "
                                 "Bake or Delete; double-click to rename; right-click for more. With subdivision "
                                 "levels, record at any level: a layer keeps its own detail on every level and follows "
                                 "Lower Level / Higher Level and Add Level.",
                                 kLayersPath);
                else
                    ui::HelpText("Base mesh", nullptr, "Strokes go into the base shape, not a layer.", kLayersPath);
                if (l && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                    renamingSculptLayer_ = i;
                    focusSculptRename_ = true;
                    std::snprintf(sculptRenameBuffer_, sizeof sculptRenameBuffer_, "%s", l->name.c_str());
                }
                if (l && ImGui::BeginPopupContextItem("##row")) {
                    ImGui::TextDisabled("%s", l->name.c_str());
                    if (ImGui::MenuItem(tr("Rename"))) contextAction = 1;
                    if (ImGui::MenuItem(tr("Duplicate"))) contextAction = 2;
                    if (ImGui::MenuItem(tr("Merge down"), nullptr, false, i + 1 < count)) contextAction = 3;
                    if (ImGui::MenuItem(tr("Invert"))) contextAction = 4;
                    ImGui::Separator();
                    if (ImGui::MenuItem(tr("Move to top"), nullptr, false, i > 0)) contextAction = 5;
                    if (ImGui::MenuItem(tr("Move to bottom"), nullptr, false, i + 1 < count)) contextAction = 6;
                    ImGui::Separator();
                    if (ImGui::MenuItem(tr("Bake into base mesh"))) contextAction = 7;
                    if (ImGui::MenuItem(tr("Delete"))) contextAction = 8;
                    if (contextAction) contextIndex = i;
                    ImGui::EndPopup();
                }
            }
            if (l) {
                ImGui::TableSetColumnIndex(1);
                char w[16];
                std::snprintf(w, sizeof w, "%.2f", double(l->weight));
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(w).x);
                ImGui::TextColored(style.Colors[isActive ? ImGuiCol_Text : ImGuiCol_TextDisabled], "%s", w);
                ImGui::TableSetColumnIndex(2);
                const bool playing = playingLayer_ == i;
                if (ui::IconToggle(("##play " + l->name).c_str(), playing ? ICON_LC_SQUARE : ICON_LC_PLAY, playing,
                                   tr(playing ? "Stop" : "Play")))
                    togglePlay = i;
                ui::HelpText(playing ? "Stop" : "Play", nullptr,
                             "Animate this blend shape 0 -> 1 -> 0 (Stop sets it back to 1).", kLayersPath);
                ImGui::TableSetColumnIndex(3);
                if (ui::IconToggle(("##eye " + l->name).c_str(), l->visible ? ICON_LC_EYE : ICON_LC_EYE_OFF, l->visible,
                                   tr(l->visible ? "Hide layer" : "Show layer")))
                    toggleVisible = i;
                ui::HelpText("Visibility", nullptr,
                             "Show or hide the layer. A hidden layer keeps its weight and adds nothing (exports "
                             "write it at 0).",
                             kLayersPath);
            }
            if (isActive && listedSculptLayer_ != i) {
                ImGui::SetScrollHereY(0.5f);
                listedSculptLayer_ = i;
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    ImGui::PopStyleVar();

    auto indexArg = [](int i) { return Args{{"index", double(i)}}; };
    if (selectIndex >= -1) runCommand("layer_set_active", indexArg(selectIndex));
    if (togglePlay >= 0) {
        const bool playing = playingLayer_ == togglePlay;
        playingLayer_ = playing ? -1 : togglePlay;
        playTime_ = 0.0f;
        if (playing) setLayerWeight(togglePlay, 1.0f);
    }
    if (toggleVisible >= 0)
        runCommand("layer_set", {{"index", double(toggleVisible)}, {"visible", !mesh.layers[size_t(toggleVisible)].visible}});
    if (renameIndex >= 0 && !renameTo.empty() && renameTo != doc_.mesh.layers[size_t(renameIndex)].name)
        runCommand("layer_set", {{"index", double(renameIndex)}, {"name", renameTo}});
    auto startRename = [this](int i) {
        renamingSculptLayer_ = i;
        focusSculptRename_ = true;
        std::snprintf(sculptRenameBuffer_, sizeof sculptRenameBuffer_, "%s", doc_.mesh.layers[size_t(i)].name.c_str());
    };
    switch (contextAction) {
    case 1: startRename(contextIndex); break;
    case 2: runCommand("layer_duplicate", indexArg(contextIndex)); break;
    case 3: runCommand("layer_merge_down", indexArg(contextIndex)); break;
    case 4: runCommand("layer_invert", indexArg(contextIndex)); break;
    case 5: runCommand("layer_move", {{"index", double(contextIndex)}, {"direction", std::string("top")}}); break;
    case 6: runCommand("layer_move", {{"index", double(contextIndex)}, {"direction", std::string("bottom")}}); break;
    case 7: runCommand("layer_bake", indexArg(contextIndex)); break;
    case 8: runCommand("layer_remove_many", indexArg(contextIndex)); break;
    default: break;
    }

    // The active layer (the commands above may have changed the list, so look again).
    const int act = doc_.activeLayer >= 0 && doc_.activeLayer < int(doc_.mesh.layers.size()) ? doc_.activeLayer : -1;
    const int n = int(doc_.mesh.layers.size());
    const SculptLayer* cur = act >= 0 ? &doc_.mesh.layers[size_t(act)] : nullptr;
    ImGui::BeginDisabled(!cur);
    float w = cur ? cur->weight : 1.0f;
    ImGui::SetNextItemWidth(-1);
    if (ui::SliderInside("##weight", &w, -1.0f, 1.0f, tr("Weight %.2f"), ImGuiSliderFlags_AlwaysClamp) && cur) {
        if (weightDragFrom_ < -1.5f) weightDragFrom_ = cur->weight;
        previewWeight(act, w);
    }
    if (ImGui::IsItemDeactivated() && weightDragFrom_ >= -1.5f && cur) {
        const float committed = cur->weight;
        setLayerWeight(act, weightDragFrom_); // so the undo step records the weight before the drag
        weightDragFrom_ = -2.0f;
        runCommand("layer_set", {{"index", double(act)}, {"weight", double(committed)}});
    }
    ui::HelpText("Weight", nullptr,
                 "How strongly the active layer shows: 1 = as sculpted, 0 = off, negative = the opposite way (-1 to 1). "
                 "One weight for every subdivision level.",
                 kLayersPath);

    // Up / Down, then the layer tools.
    const float arrow = ImGui::GetFrameHeight();
    const float half2 = (ImGui::GetContentRegionAvail().x - 2 * arrow - 3 * style.ItemSpacing.x) * 0.5f;
    ImGui::BeginDisabled(!cur || act <= 0);
    if (ImGui::ArrowButton("##layerup", ImGuiDir_Up)) runCommand("layer_move", {{"index", double(act)}, {"direction", std::string("up")}});
    ImGui::EndDisabled();
    ui::HelpText("Move up", nullptr, "Move the active layer up the list (More has Move to top).", kLayersPath);
    ImGui::SameLine();
    ImGui::BeginDisabled(!cur || act >= n - 1);
    if (ImGui::ArrowButton("##layerdown", ImGuiDir_Down))
        runCommand("layer_move", {{"index", double(act)}, {"direction", std::string("down")}});
    ImGui::EndDisabled();
    ui::HelpText("Move down", nullptr, "Move the active layer down the list (More has Move to bottom).", kLayersPath);
    ImGui::SameLine();
    if (ImGui::Button(tr("Duplicate##layer"), ImVec2(half2, 0))) runCommand("layer_duplicate", indexArg(act));
    ui::HelpText("Duplicate", nullptr, "Copy the active layer, offsets and settings, to a new layer below it.", kLayersPath);
    ImGui::SameLine();
    ImGui::BeginDisabled(!cur || act >= n - 1);
    if (ImGui::Button(tr("Merge down"), ImVec2(-1, 0))) runCommand("layer_merge_down", indexArg(act));
    ImGui::EndDisabled();
    ui::HelpText("Merge down", nullptr,
                 "Merge the active layer into the one below it, keeping how both look (the result has weight 1).",
                 kLayersPath);
    ImGui::EndDisabled();

    // Bake / Delete the selection (or the active layer), More.
    const std::vector<int> sel = selectedSculptLayers();
    std::string list;
    for (int i : sel) list += (list.empty() ? "" : ",") + std::to_string(i);
    const Args selArgs{{"indices", list}};
    char bakeLabel[48], deleteLabel[48];
    if (sel.size() > 1) {
        std::snprintf(bakeLabel, sizeof bakeLabel, tr("Bake %d###layerbake"), int(sel.size()));
        std::snprintf(deleteLabel, sizeof deleteLabel, tr("Delete %d###layerdelete"), int(sel.size()));
    } else {
        std::snprintf(bakeLabel, sizeof bakeLabel, "%s###layerbake", tr("Bake"));
        std::snprintf(deleteLabel, sizeof deleteLabel, "%s###layerdelete", tr("Delete"));
    }
    const float moreW = ImGui::CalcTextSize(ICON_LC_ELLIPSIS).x + 2 * style.FramePadding.x + 8.0f * scale;
    const float third = (ImGui::GetContentRegionAvail().x - moreW - 3 * style.ItemSpacing.x) / 3.0f;
    ImGui::BeginDisabled(!cur);
    if (ImGui::Button(tr("Invert##layer"), ImVec2(third, 0))) runCommand("layer_invert", indexArg(act));
    ui::HelpText("Invert", nullptr, "Turn the active layer the opposite way: bumps become dents.", kLayersPath);
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(sel.empty());
    if (ImGui::Button(bakeLabel, ImVec2(third, 0))) runCommand("layer_bake", selArgs);
    ui::HelpText("Bake", nullptr,
                 "Bake the selected layers (or the active one) into the base mesh as they show now, then remove them. "
                 "The shape doesn't change; undo brings the layers back.",
                 kLayersPath);
    ImGui::SameLine();
    if (ImGui::Button(deleteLabel, ImVec2(third, 0))) runCommand("layer_remove_many", selArgs);
    ui::HelpText("Delete", nullptr, "Delete the selected layers (or the active one) on every subdivision level (undoable).",
                 kLayersPath);
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(n == 0);
    if (ImGui::Button(ICON_LC_ELLIPSIS "###layermore", ImVec2(-1, 0))) ImGui::OpenPopup("##layermenu");
    ImGui::EndDisabled();
    ui::HelpText("More", nullptr, "Move to top or bottom, rename, and bake or delete all layers.", kLayersPath);
    if (ImGui::BeginPopup("##layermenu")) {
        if (ImGui::MenuItem(tr("Move to top"), nullptr, false, cur && act > 0))
            runCommand("layer_move", {{"index", double(act)}, {"direction", std::string("top")}});
        if (ImGui::MenuItem(tr("Move to bottom"), nullptr, false, cur && act < n - 1))
            runCommand("layer_move", {{"index", double(act)}, {"direction", std::string("bottom")}});
        if (ImGui::MenuItem(tr("Rename"), nullptr, false, cur != nullptr)) startRename(act);
        ImGui::Separator();
        if (ImGui::MenuItem(tr("Bake all layers"), nullptr, false, n > 0)) runCommand("layer_bake", {{"indices", std::string("all")}});
        if (ImGui::MenuItem(tr("Delete all layers"), nullptr, false, n > 0))
            runCommand("layer_remove_many", {{"indices", std::string("all")}});
        ImGui::EndPopup();
    }

    // New layer.
    ImGui::SetNextItemWidth(-ImGui::CalcTextSize("+ Add").x - style.FramePadding.x * 2 - style.ItemSpacing.x);
    const bool enter = ImGui::InputText("##newlayer", newLayerName_, sizeof newLayerName_, ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    if (ImGui::Button(tr("+ Add")) || enter) {
        runCommand("layer_add", {{"name", std::string(newLayerName_)}});
        sculptLayerSelection_.clear();
    }
    ui::HelpText("+ Add", nullptr, "Add a sculpt layer with the name typed beside it; strokes go into it.", kLayersPath);
}

} // namespace sf
