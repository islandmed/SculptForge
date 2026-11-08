// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// the classic Extract (Tool > Part), Mirror And Weld (Tool > Geometry > Modify Topology) and Mirror
// (Tool > Deformation). The work is done by the extract / mirror_and_weld / mirror commands (core/ExtractMirror.h).
#include "app/App.h"
#include "app/Demo.h"
#include "app/I18n.h"
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
                           {"triangle_corners", extractTriCorners_},
                           {"triangle_border", extractTriBorder_}});
}

void App::drawExtractContents() {
    Demo::pushScope("Extract");
    tray::GroupLabel(*this, "Extract");
    const float half = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
    // Pairs share a row when both labels fit half of it; a narrow column (a small window, a big interface scale, a
    // longer translation) gives each its own row instead of cutting the words off.
    auto actionW = [this](const char* id) { // its label as the button shows it
        const Action* a = actions_.find(id);
        return a ? ui::ButtonWidth(trAction(a->id, a->label)) : 0.0f;
    };
    const bool masked = doc_.mesh.hasMask() || doc_.extractPreviewCurrent();
    const bool buttonsPair = actionW("part.extract") <= half &&
                             (masked ? actionW("part.extract_accept") : ImGui::CalcTextSize(tr("Mask first (Ctrl+drag)")).x) <= half;
    ui::ActionButton(*this, "part.extract", ImVec2(buttonsPair ? half : -1.0f, 0));
    ui::HelpAction(*this, "part.extract");
    if (buttonsPair) ImGui::SameLine();
    // Extract works on the mask: without one, say so where Accept would be (a review read the greyed-out buttons
    // as unclickable); the row keeps its height.
    if (!masked) {
        ImGui::AlignTextToFramePadding();
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("%s", tr("Mask first (Ctrl+drag)"));
        ImGui::PopTextWrapPos();
        ui::HelpText("Extract", nullptr, "Extract makes a shell from the masked area: mask it first (Ctrl+drag on the model).",
                     "Tool > Parts > Extract");
    } else {
        ui::ActionButton(*this, "part.extract_accept", ImVec2(-1, 0));
        ui::HelpAction(*this, "part.extract_accept");
    }

    // The settings: changing one while the preview shows extracts again, so the preview follows.
    bool changed = false;
    const bool slidersPair = ui::SliderWidth("Edge smooth %d", 20) <= half && ui::SliderWidth("Thickness %.3f", -0.1) <= half;
    ImGui::SetNextItemWidth(slidersPair ? half : -1.0f);
    tray::SliderInt("##extract_smooth", &extractSmooth_, 0, 20, "Edge smooth %d");
    changed |= ImGui::IsItemDeactivatedAfterEdit();
    ui::HelpText("Edge smooth", nullptr,
                 "Smooths the extracted edge, which otherwise follows the mask's triangles in steps (0 = as masked).",
                 "Tool > Parts > Extract");
    if (slidersPair) ImGui::SameLine();
    ImGui::SetNextItemWidth(-1);
    ui::SliderInside("##extract_thick", &extractThick_, -0.1f, 0.1f, "Thickness %.3f");
    changed |= ImGui::IsItemDeactivatedAfterEdit();
    ui::HelpText("Thickness", nullptr,
                 "Thickness of the shell, as a fraction of the Part's size (0.015 is 1.5%). Negative grows it "
                 "inward; 0 makes a single-sided surface.",
                 "Tool > Parts > Extract");
    // Both Sides, Corners, Border: one row, or as many as the column needs (Segmented wraps them).
    ui::Segmented(*this, {"part.extract_double", "part.extract_triangle_corners", "part.extract_triangle_border"}, -1.0f);
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
    drawDeformSize();  // PanelDeform.cpp
    drawDeformStack();
}

} // namespace sf
