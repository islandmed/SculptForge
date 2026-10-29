// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Polygroups and visibility UI: the Tools panel section and the ZBrush
// Ctrl+Shift viewport gestures (click to isolate / hide a group or show all,
// drag a rectangle or lasso to hide outside or, with Alt, inside it; the
// shape is tracked and drawn by Selection.cpp). Every action goes through
// the command registry (see core/Polygroups.cpp).
#include "app/App.h"

#include "app/Ui.h"

#include "core/Polygroups.h"

namespace sf {

namespace {

bool ctrlShiftDown(SDL_Keymod mod) {
    return (mod & (SDL_KMOD_CTRL | SDL_KMOD_GUI)) != 0 && (mod & SDL_KMOD_SHIFT) != 0;
}

} // namespace

bool App::polygroupMouseDown(float x, float y, SDL_Keymod mod) {
    if (!ctrlShiftDown(mod) || previewLod_ >= 0) return false;
    beginSelection(x, y, SelectTarget::Hide, hideShape_);
    return true;
}

void App::polygroupMouseUp() {
    const bool alt = (inputMods() & SDL_KMOD_ALT) != 0;
    if (!selectMoved_) {
        // Click, as in ZBrush: on a fully visible mesh show only the group under the cursor; on a partly
        // hidden one hide it (the group that is all that's left: invert the visibility). Alt always hides it,
        // the empty canvas shows everything.
        doc_.syncBvh();
        RayHit hit;
        if (!pick(selectPath_.front().x, selectPath_.front().y, hit)) {
            if (doc_.mesh.hasHidden()) runCommand("show_all");
            return;
        }
        const uint32_t group = doc_.mesh.polygroupAt(hit.triangle);
        if (polygroupIsolated(doc_.mesh, group)) runCommand("invert_visibility");
        else if (alt || doc_.mesh.hasHidden()) runCommand("hide_polygroup", {{"group", double(group)}});
        else runCommand("isolate_polygroup", {{"group", double(group)}});
        return;
    }
    // Drag: keep the inside of the rectangle or lasso, or with Alt hide it. A shape around nothing (dragged on
    // the background) reverses the visibility instead (ZBrush).
    if (runSelection("hide_rect", "hide_lasso", {{"inside", alt}}) && !lastCommandOk_ && !alt && doc_.mesh.hasHidden())
        runCommand("invert_visibility");
}

void App::drawPolygroupsSection() {
    ImGui::Checkbox("Show polygroups", &renderSettings_.showPolygroups);
    ImGui::SameLine();
    const Mesh& m = doc_.mesh;
    const std::array<uint64_t, 6> key = {doc_.undo.revision() + 1, uint64_t(doc_.modified), m.triangles.size(),
                                         m.polygroups.size(), uint64_t(reinterpret_cast<uintptr_t>(m.polygroups.data())),
                                         uint64_t(reinterpret_cast<uintptr_t>(m.hidden.data())) ^ m.hidden.size()};
    if (key != visibleGroupsKey_) {
        visibleGroupsKey_ = key;
        visibleGroups_ = visiblePolygroupCount(m);
    }
    ImGui::TextDisabled("%zu", visibleGroups_);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Visible polygroups");

    // Grouping buttons switch the polygroup colours on so the result is visible.
    auto group = [this](const char* command, const Args& args = {}) {
        runCommand(command, args);
        renderSettings_.showPolygroups = true;
    };
    const float half = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
    if (ImGui::Button("By normals", ImVec2(half, 0)))
        group("polygroup_auto_by_normals", {{"angle", double(polygroupAngle_)}});
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("New group wherever the surface turns more than the angle");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1);
    ui::SliderInside("##pgangle", &polygroupAngle_, 1.0f, 90.0f, "Angle %.0f deg");
    if (ImGui::Button("By pieces", ImVec2(half, 0))) group("polygroup_by_connectivity");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("One group per disconnected piece");
    ImGui::SameLine();
    if (ImGui::Button("From mask", ImVec2(half, 0))) group("polygroup_from_mask");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Masked triangles become a new group");
    if (ImGui::Button("Merge visible", ImVec2(half, 0))) group("polygroup_merge_visible");
    ImGui::SameLine();
    if (ImGui::Button("Clear groups", ImVec2(half, 0))) runCommand("polygroup_clear");
    ui::ActionButton(*this, "polygroup.uv_groups", ImVec2(half, 0));
}

void App::drawVisibilitySection() {
    ui::Segmented(*this, {"select.rect", "select.lasso"});
    const float half = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
    ImGui::BeginDisabled(!doc_.mesh.hasHidden());
    if (ImGui::Button("Show all", ImVec2(half, 0))) runCommand("show_all");
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Invert visible", ImVec2(half, 0))) runCommand("invert_visibility");
    ImGui::BeginDisabled(!doc_.mesh.hasHidden());
    if (ImGui::Button("Delete hidden", ImVec2(-1, 0))) runCommand("delete_hidden");
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("Ctrl+Shift+click a group to isolate it, +Alt to hide it, on the background to show all.\n"
                          "Ctrl+Shift+drag hides outside the rectangle or lasso (Alt: inside).");
}

} // namespace sf
