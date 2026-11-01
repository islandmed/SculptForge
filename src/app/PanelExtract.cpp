// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// ZBrush's Extract (Tool > SubTool), Mirror And Weld (Tool > Geometry > Modify Topology) and Mirror
// (Tool > Deformation). The work is done by the extract / mirror_and_weld / mirror commands (core/ExtractMirror.h).
#include "app/App.h"
#include "app/Demo.h"
#include "app/Palettes.h"
#include "app/Ui.h"

#include <string>

namespace sf {

std::string App::axesText(const bool axes[3]) {
    std::string s;
    for (int k = 0; k < 3; ++k)
        if (axes[k]) s += char('x' + k);
    return s;
}

void App::runExtract() {
    runCommand("extract", {{"thickness", double(extractThick_)},
                           {"smooth", double(extractSmooth_)},
                           {"double", extractDouble_},
                           {"tcorne", extractTCorne_},
                           {"tborder", extractTBorder_}});
}

void App::drawExtractContents() {
    Demo::pushScope("Extract");
    tray::GroupLabel(*this, "Extract");
    const float half = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
    ui::ActionButton(*this, "subtool.extract", ImVec2(half, 0));
    ui::HelpAction(*this, "subtool.extract");
    ImGui::SameLine();
    ui::ActionButton(*this, "subtool.extract_accept", ImVec2(-1, 0));
    ui::HelpAction(*this, "subtool.extract_accept");

    // The settings: changing one while the preview shows extracts again, so the preview follows.
    bool changed = false;
    ImGui::SetNextItemWidth(half);
    tray::SliderInt("##extract_smooth", &extractSmooth_, 0, 20, "E Smt %d");
    changed |= ImGui::IsItemDeactivatedAfterEdit();
    ui::HelpText("E Smt", nullptr,
                 "Smooths the extracted edge, which otherwise follows the mask's triangles in steps (0 = as masked).",
                 "Tool > Parts > Extract");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1);
    ui::SliderInside("##extract_thick", &extractThick_, -0.1f, 0.1f, "Thick %.3f");
    changed |= ImGui::IsItemDeactivatedAfterEdit();
    ui::HelpText("Thick", nullptr,
                 "Thickness of the shell, as a fraction of the Part's size (0.015 is 1.5%). Negative grows it "
                 "inward; 0 makes a single-sided surface.",
                 "Tool > Parts > Extract");
    ui::ActionButton(*this, "subtool.extract_double", ImVec2(half, 0));
    ui::HelpAction(*this, "subtool.extract_double");
    ImGui::SameLine();
    ui::Segmented(*this, {"subtool.extract_tcorne", "subtool.extract_tborder"}, -1.0f);
    if (changed && doc_.extractPreviewCurrent()) runExtract();
    Demo::popScope();
}

void App::drawModifyTopologyContents() {
    Demo::pushScope("Modify Topology");
    tray::GroupLabel(*this, "Modify Topology");
    const ImGuiStyle& style = ImGui::GetStyle();
    const float axisW = ImGui::GetFrameHeight() * 1.3f;
    ui::ActionButton(*this, "geometry.mirror_weld",
                     ImVec2(ImGui::GetContentRegionAvail().x - 3.0f * axisW - 2.0f - style.ItemSpacing.x, 0));
    ui::HelpAction(*this, "geometry.mirror_weld");
    ImGui::SameLine();
    ui::Segmented(*this, {"geometry.mirror_weld_x", "geometry.mirror_weld_y", "geometry.mirror_weld_z"}, -1.0f);
    Demo::popScope();
}

void App::drawDeformationContents() {
    const ImGuiStyle& style = ImGui::GetStyle();
    const float axisW = ImGui::GetFrameHeight() * 1.3f;
    ui::ActionButton(*this, "deformation.mirror",
                     ImVec2(ImGui::GetContentRegionAvail().x - 3.0f * axisW - 2.0f - style.ItemSpacing.x, 0));
    ui::HelpAction(*this, "deformation.mirror");
    ImGui::SameLine();
    ui::Segmented(*this, {"deformation.mirror_x", "deformation.mirror_y", "deformation.mirror_z"}, -1.0f);
    // Whole-mesh deformations (core/Deform.h, from the discussions): a button and its amount on one row.
    const float button = ImGui::CalcTextSize("Balloon").x + 2.0f * style.FramePadding.x + ImGui::GetFrameHeight();
    const struct {
        const char* action;
        float* amount;
        float lo;
        const char* slider;
    } rows[] = {{"deformation.polish", &deformAmount_[0], 0.0f, "##deform_polish_amount"},
                {"deformation.relax", &deformAmount_[1], 0.0f, "##deform_relax_amount"},
                {"deformation.smooth", &deformAmount_[2], 0.0f, "##deform_smooth_amount"},
                {"deformation.balloon", &deformAmount_[3], -100.0f, "##deform_balloon_amount"}};
    for (const auto& r : rows) {
        ui::ActionButton(*this, r.action, ImVec2(button, 0));
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-1);
        ui::SliderInside(r.slider, r.amount, r.lo, 100.0f, "%.0f");
    }
    ui::Checkbox("Smooth keeps volume##deformation.smooth_volume", &deformKeepVolume_);
    ui::HelpText("Smooth keeps volume", nullptr,
                 "Deformation > Smooth smooths without shrinking the form (Taubin smoothing) instead of the plain "
                 "smooth, which also shrinks it.",
                 "Tool > Deformation");
}

} // namespace sf
