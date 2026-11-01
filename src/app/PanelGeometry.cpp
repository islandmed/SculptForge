// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Tool > Geometry: subdivision levels, as in ZBrush (Lower Res / Higher Res, SDiv, Del Lower / Del Higher,
// Reconstruct Subdiv, Divide with Smt). The levels themselves live in core/Multires.
#include "app/App.h"
#include "app/Ui.h"
#include "app/I18n.h"

#include "core/Micro.h"
#include "core/Multires.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace sf {

void App::drawGeometrySection() {
    const Mesh& m = doc_.mesh;
    const int levels = subdivLevelCount(m), current = subdivCurrentLevel(m);
    const ImGuiStyle& style = ImGui::GetStyle();
    const float half = (ImGui::GetContentRegionAvail().x - style.ItemSpacing.x) * 0.5f;

    ui::ActionButton(*this, "geometry.lower_res", ImVec2(half, 0));
    ImGui::SameLine();
    ui::ActionButton(*this, "geometry.higher_res", ImVec2(half, 0));

    // SDiv: the level slider (1 = the base cage). Applied when released, as one undo step.
    ImGui::BeginDisabled(levels < 2);
    if (!sdivHeld_) sdivSlider_ = float(current + 1);
    ImGui::SetNextItemWidth(-1);
    char format[32];
    std::snprintf(format, sizeof format, "SDiv %%.0f / %d", std::max(levels, 1));
    ui::SliderInside("##sdiv", &sdivSlider_, 1.0f, float(std::max(levels, 1)), format, ImGuiSliderFlags_AlwaysClamp);
    sdivHeld_ = ImGui::IsItemActive();
    sdivSlider_ = std::round(sdivSlider_);
    if (ImGui::IsItemDeactivatedAfterEdit() && int(sdivSlider_) - 1 != current)
        runCommand("subdiv_level", {{"level", double(sdivSlider_)}});
    ImGui::EndDisabled();
    ui::HelpText("SDiv", nullptr,
                 "The subdivision level to sculpt at: 1 is the base cage, the highest has the finest detail. "
                 "D / Shift+D step up / down. Detail is kept on every level.",
                 "Tool > Geometry");

    ui::ActionButton(*this, "geometry.del_lower", ImVec2(half, 0));
    ImGui::SameLine();
    ui::ActionButton(*this, "geometry.del_higher", ImVec2(half, 0));
    ui::ActionButton(*this, "geometry.reconstruct", ImVec2(-1, 0));

    // Divide with its Smt toggle beside it, as in ZBrush.
    const float smtW = ImGui::CalcTextSize("Smt").x + 2.0f * style.FramePadding.x + 6.0f * uiScale_;
    ui::ActionButton(*this, "geometry.divide", ImVec2(ImGui::GetContentRegionAvail().x - smtW - style.ItemSpacing.x, 0),
                     ui::Look::IconLabel);
    ImGui::SameLine();
    ui::ActionButton(*this, "geometry.smt", ImVec2(-1, 0));

    if (levels) {
        char info[96];
        std::snprintf(info, sizeof info, "SDiv %d of %d  -  base %zu, top %zu triangles", current + 1, levels,
                      subdivLevelTriangles(m, 0), subdivLevelTriangles(m, levels - 1));
        ImGui::TextDisabled("%s", info);
    } else {
        ImGui::TextDisabled("%s", tr("No subdivision levels - Divide adds them"));
    }

    // Micro: displacement levels above the highest level (core/Micro.h). One row: the level slider and Divide to
    // Micro (or Bake / Delete once there is one), so the section stays as tall as before.
    const bool micro = hasMicro(m);
    if (micro) microSlider_ = float(microLevel(m));
    const float sliderW = std::floor(ImGui::GetContentRegionAvail().x * 0.42f);
    ImGui::BeginDisabled(micro);
    ImGui::SetNextItemWidth(sliderW);
    {
        const size_t est = m.triangleCount() << (2 * int(microSlider_));
        char fmt[64];
        std::snprintf(fmt, sizeof fmt, "Micro %%.0f (%.0fM)", double(est) / 1e6);
        ui::SliderInside("##micro", &microSlider_, 1.0f, float(MicroMesh::kMaxLevel), fmt, ImGuiSliderFlags_AlwaysClamp);
        microSlider_ = std::round(microSlider_);
    }
    ImGui::EndDisabled();
    ui::HelpText("Micro", nullptr,
                 "Micro levels above the highest subdivision level: each 4x the triangles, stored as one 16-bit "
                 "displacement per point (no mesh), so 300 million triangles take about 0.4 GB and sculpt in real time. "
                 "Move reshapes the level below and the detail rides along.",
                 "Tool > Geometry");
    ImGui::SameLine();
    if (!micro) {
        ui::ActionButton(*this, "geometry.micro_divide", ImVec2(-1, 0));
    } else {
        const float w = (ImGui::GetContentRegionAvail().x - style.ItemSpacing.x) * 0.5f;
        ui::ActionButton(*this, "geometry.micro_bake", ImVec2(w, 0));
        ImGui::SameLine();
        ui::ActionButton(*this, "geometry.micro_delete", ImVec2(-1, 0));
        ui::ActionButton(*this, "geometry.micro_export", ImVec2(half, 0));
        ImGui::SameLine();
        ImGui::Checkbox(tr("Move along normals"), &doc_.sculptor.microMoveAlongNormals);
    }
    if (micro && m.micro) { // (a button above may have just removed it)
        const MicroMesh& mm = *m.micro;
        char info[128];
        std::snprintf(info, sizeof info, "Micro %d: %.1fM triangles, %.0f MB%s", mm.level, double(mm.microTriangles()) / 1e6,
                      double(mm.bytes()) / 1e6, mm.disp.mapped() ? ", out of core" : "");
        ImGui::TextDisabled("%s", info);
    }
    ImGui::Separator();
}

} // namespace sf
