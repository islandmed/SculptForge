// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Tool > Selection Sets: store, restore, rename, delete and combine named snapshots of the active Part's mask,
// polygroups, vertex paint, visibility, UVs and shape; pick one as the Morph brush's target or morph toward it.
// Every button runs a selection_set_* command (core/SelectionSets.h), so each is one undo step.
#include "app/App.h"

#include "app/I18n.h"
#include "app/Ui.h"

#include "core/Multires.h"
#include "core/SelectionSets.h"

#include <cstdio>
#include <cstring>

namespace sf {

namespace {

struct ChannelToggle {
    uint32_t bit;
    const char* label; // shown, and the command's channel name
    const char* channel;
    const char* help;
};
constexpr ChannelToggle kToggles[] = {
    {kSetMask, "Mask##setch", "mask", "The mask (masked and partly masked vertices)."},
    {kSetPolygroups, "Groups##setch", "polygroups", "The face groups."},
    {kSetVisibility, "Visibility##setch", "visibility", "Which faces are shown and which are hidden."},
    {kSetPaint, "Vertex Paint##setch", "paint", "The vertex paint colours."},
    {kSetUvs, "UVs##setch", "uvs", "The UV layout (only when the Part has UVs)."},
    {kSetMorph, "Shape##setch", "morph", "The shape itself: a morph target for the Morph brush and Morph to Set."},
};

std::string channelArg(uint32_t channels) {
    std::string out;
    for (const auto& t : kToggles)
        if (channels & t.bit) out += (out.empty() ? "" : ",") + std::string(t.channel);
    return out;
}

const char* const kModes[] = {"Replace", "Union", "Intersect", "Subtract"};
const char* const kModeArgs[] = {"replace", "union", "intersect", "subtract"};

} // namespace

void App::drawSelectionSetsSection() {
    const SelectionSetList* list = doc_.mesh.selectionSets.get();
    const float avail = ImGui::GetContentRegionAvail().x, spacing = ImGui::GetStyle().ItemSpacing.x;
    const float half = (avail - spacing) * 0.5f, third = (avail - 2.0f * spacing) / 3.0f;

    // Store: a name and the channels the new set holds.
    ImGui::SetNextItemWidth(avail - third - spacing);
    ImGui::InputText("##setname", setNameBuffer_, sizeof setNameBuffer_);
    ui::HelpText("Set name", nullptr, "Name of the set Store makes; storing again with the same name replaces it.",
                 "Tool > Selection Sets");
    ImGui::SameLine();
    if (ImGui::Button(tr("Store##selsets"), ImVec2(third, 0))) {
        std::string name = setNameBuffer_;
        if (name.empty()) name = "Set";
        runCommand("selection_set_store", {{"name", name}, {"channels", channelArg(setChannels_ ? setChannels_ : kSetMask)}});
        if (lastCommandOk_) {
            selectedSet_ = name;
            // Suggest the next name ("Set 1" -> "Set 2").
            const size_t digits = name.find_last_not_of("0123456789");
            int n = digits + 1 < name.size() ? std::atoi(name.c_str() + digits + 1) + 1 : 2;
            const std::string stem = digits + 1 < name.size() ? name.substr(0, digits + 1) : name + " ";
            std::snprintf(setNameBuffer_, sizeof setNameBuffer_, "%s%d", stem.c_str(), n);
        }
    }
    ui::HelpText("Store", nullptr,
                 "Store the checked channels of this Part as a named set (one undo step). Sets restore at any "
                 "subdivision level and are saved in the project.",
                 "Tool > Selection Sets");
    for (size_t i = 0; i < std::size(kToggles); ++i) {
        if (i % 3) ImGui::SameLine();
        bool on = (setChannels_ & kToggles[i].bit) != 0;
        if (ImGui::Checkbox(tr(kToggles[i].label), &on)) setChannels_ = on ? setChannels_ | kToggles[i].bit : setChannels_ & ~kToggles[i].bit;
        const std::string title(kToggles[i].label, std::strchr(kToggles[i].label, '#'));
        ui::HelpText(title.c_str(), nullptr, kToggles[i].help, "Tool > Selection Sets");
    }
    if (ImGui::Button(tr("From Visible##selsets"), ImVec2(half, 0))) {
        runCommand("selection_set_from_visible");
        if (lastCommandOk_ && doc_.mesh.selectionSets) selectedSet_ = doc_.mesh.selectionSets->sets.back()->name;
    }
    ui::HelpText("From Visible", nullptr, "Quick set: store the faces shown right now (Visible, Visible 2, ...).",
                 "Tool > Selection Sets");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1);
    ImGui::Combo("##setmode", &setMode_, kModes, int(std::size(kModes)));
    ui::HelpText("Restore mode", nullptr,
                 "How Restore meets the current mask and visibility: Replace them, or Union, Intersect or Subtract the "
                 "set's. Combine uses the same mode (not Replace) to make a new set from two.",
                 "Tool > Selection Sets");

    // The sets.
    if (!list || list->sets.empty()) {
        ImGui::TextDisabled("%s", tr("No sets yet: Store saves one."));
        return;
    }
    if (list->find(selectedSet_) < 0) selectedSet_ = list->sets.front()->name;
    const float rowH = ImGui::GetTextLineHeightWithSpacing();
    const int rows = std::min<int>(int(list->sets.size()), 6);
    std::string clicked;
    if (ImGui::BeginChild("##setlist", ImVec2(-1, rowH * float(rows) + ImGui::GetStyle().FramePadding.y * 2.0f),
                          ImGuiChildFlags_Borders)) {
        for (size_t i = 0; i < list->sets.size(); ++i) {
            const SelectionSet& s = *list->sets[i];
            const bool fits = selectionSetFits(doc_.mesh, s);
            std::string label = s.name;
            if (s.name == list->morphTarget) label += "  [morph]";
            label += "##set" + std::to_string(i);
            if (!fits) ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
            if (ImGui::Selectable(label.c_str(), s.name == selectedSet_, ImGuiSelectableFlags_AllowDoubleClick)) {
                clicked = s.name;
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                    selectedSet_ = s.name;
                    runCommand("selection_set_restore", {{"name", s.name}, {"mode", std::string(kModeArgs[setMode_])}});
                }
            }
            if (!fits) ImGui::PopStyleColor();
            if (ImGui::IsItemHovered()) {
                std::string tip = setChannelNames(s.channels);
                if (subdivLevelCount(doc_.mesh)) tip += "\nStored at SDiv " + std::to_string(s.level + 1);
                if (!fits) tip += "\nDoesn't fit this mesh (its topology changed)";
                tip += "\nDouble-click to restore";
                ImGui::SetTooltip("%s", tip.c_str());
            }
        }
    }
    ImGui::EndChild();
    if (!clicked.empty() && clicked != selectedSet_) {
        selectedSet_ = clicked;
        renamingSet_ = false;
    }
    const int index = list->find(selectedSet_);
    const SelectionSet& sel = *list->sets[size_t(index)];
    const std::string name = sel.name; // (the list may change under the buttons below)

    if (ImGui::Button(tr("Restore##selsets"), ImVec2(third, 0)))
        runCommand("selection_set_restore", {{"name", name}, {"mode", std::string(kModeArgs[setMode_])}});
    ui::HelpText("Restore", nullptr,
                 "Put the selected set's channels back (not its shape: Morph to Set does that), carried to the current "
                 "subdivision level. One undo step.",
                 "Tool > Selection Sets");
    ImGui::SameLine();
    if (ImGui::Button(tr("Rename##selsets"), ImVec2(third, 0))) {
        renamingSet_ = true;
        std::snprintf(setRenameBuffer_, sizeof setRenameBuffer_, "%s", name.c_str());
    }
    ImGui::SameLine();
    if (ImGui::Button(tr("Delete##selsets"), ImVec2(third, 0))) {
        runCommand("selection_set_delete", {{"name", name}});
        return;
    }
    ui::HelpText("Delete", nullptr, "Delete the selected set (undo brings it back).", "Tool > Selection Sets");
    if (renamingSet_) {
        ImGui::SetNextItemWidth(avail - third - spacing);
        const bool enter = ImGui::InputText("##setrename", setRenameBuffer_, sizeof setRenameBuffer_,
                                            ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
        ImGui::SameLine();
        if (ImGui::Button(tr("OK##setrename"), ImVec2(third, 0)) || enter) {
            renamingSet_ = false;
            runCommand("selection_set_rename", {{"name", name}, {"to", std::string(setRenameBuffer_)}});
            if (lastCommandOk_) selectedSet_ = setRenameBuffer_;
            return;
        }
    }

    // Combine with another set (by mask and visibility) into a new one.
    if (list->sets.size() > 1) {
        if (list->find(combineWith_) < 0 || combineWith_ == name)
            for (const auto& s : list->sets)
                if (s->name != name) {
                    combineWith_ = s->name;
                    break;
                }
        ImGui::SetNextItemWidth(avail - third - spacing);
        if (ImGui::BeginCombo("##combinewith", combineWith_.c_str())) {
            for (const auto& s : list->sets)
                if (s->name != name && ImGui::Selectable(s->name.c_str(), s->name == combineWith_)) combineWith_ = s->name;
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(setMode_ == 0);
        if (ImGui::Button(tr("Combine##selsets"), ImVec2(third, 0)))
            runCommand("selection_set_combine", {{"a", name}, {"b", combineWith_}, {"op", std::string(kModeArgs[setMode_])}});
        ImGui::EndDisabled();
        ui::HelpText("Combine", nullptr,
                     "Make a new set from the selected one and this one by their mask and visibility, with the mode "
                     "above (Union, Intersect or Subtract).",
                     "Tool > Selection Sets");
    }

    // The shape: the Morph brush's target, and Morph to Set.
    if (sel.has(kSetMorph)) {
        const bool target = list->morphTarget == name;
        if (ImGui::Button(target ? tr("Clear Morph Target##selsets") : tr("Morph Target##selsets"), ImVec2(half, 0))) {
            runCommand("selection_set_morph_target", {{"name", target ? std::string() : name}});
            if (lastCommandOk_ && !target)
                if (const Action* a = actions_.find("brush.morph"); a && a->run) a->run(); // ready to brush toward it
        }
        ui::HelpText("Morph Target", nullptr,
                     "Make this set's shape the Morph brush's target and pick the Morph brush: strokes pull the surface "
                     "back toward the stored shape (Alt pushes away), at any subdivision level.",
                     "Tool > Selection Sets");
        ImGui::SameLine();
        if (ImGui::Button(tr("Morph to Set##selsets"), ImVec2(half, 0)))
            runCommand("selection_set_morph", {{"name", name}, {"amount", double(morphAmount_)}});
        ui::HelpText("Morph to Set", nullptr,
                     "Move the whole surface toward the set's shape by the amount below, within the mask. One undo step.",
                     "Tool > Selection Sets");
        ImGui::SetNextItemWidth(-1);
        ui::SliderInside("##morphamount", &morphAmount_, -1.0f, 1.0f, "Amount %.2f");
    } else {
        ImGui::TextDisabled("%s", tr("Store with Shape checked to morph toward a set."));
    }
}

} // namespace sf
