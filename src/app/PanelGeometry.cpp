// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Tool > Geometry, grouped by what you do: Levels (the Level slider, Lower / Higher Level, Add Level with Smooth,
// Rebuild Lower Levels, Remove Below / Above), Edges (PanelEdges.cpp), Micro Detail, Resolution (Decimate, Quad
// Remesh, Subdivide), Cleanup (Fill Holes, Merge by Distance, Find Problems) and Symmetry (Make Symmetric). The levels
// themselves live in core/Multires.
#include "app/App.h"
#include "app/Demo.h"
#include "app/Palettes.h"
#include "app/Ui.h"
#include "app/I18n.h"

#include "core/Micro.h"
#include "core/Multires.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace sf {

namespace {
// 27648 -> "27.6K", 1769472 -> "1.77M": the Levels line fits the tray at 1440 px and 200% (it clipped with full counts).
std::string triangleCount(size_t n) {
    char text[32];
    if (n < 10000) std::snprintf(text, sizeof text, "%zu", n);
    else if (n < 1000000) std::snprintf(text, sizeof text, "%.1fK", double(n) / 1e3);
    else std::snprintf(text, sizeof text, "%.2fM", double(n) / 1e6);
    return text;
}
} // namespace

void App::drawGeometrySection() {
    const Mesh& m = doc_.mesh;
    const int levels = subdivLevelCount(m), current = subdivCurrentLevel(m);
    const ImGuiStyle& style = ImGui::GetStyle();
    const float half = (ImGui::GetContentRegionAvail().x - style.ItemSpacing.x) * 0.5f;

    // Grouped by what you do (fix/ip-labels): Levels, Edges, Micro Detail, Resolution, Cleanup, Symmetry.
    tray::GroupLabel(*this, "Levels");
    // Level: the slider (1 = the base cage). Applied when released, as one undo step.
    ImGui::BeginDisabled(levels < 2);
    if (!sdivHeld_) sdivSlider_ = float(current + 1);
    ImGui::SetNextItemWidth(-1);
    char format[32];
    std::snprintf(format, sizeof format, "Level %%.0f / %d", std::max(levels, 1));
    ui::SliderInside("##level", &sdivSlider_, 1.0f, float(std::max(levels, 1)), format, ImGuiSliderFlags_AlwaysClamp);
    sdivHeld_ = ImGui::IsItemActive();
    sdivSlider_ = std::round(sdivSlider_);
    if (ImGui::IsItemDeactivatedAfterEdit() && int(sdivSlider_) - 1 != current)
        runCommand("subdiv_level", {{"level", double(sdivSlider_)}});
    ImGui::EndDisabled();
    ui::HelpText("Level", nullptr,
                 "The subdivision level to sculpt at: 1 is the base cage, the highest has the finest detail. "
                 "D / Shift+D step up / down. Detail is kept on every level.",
                 "Tool > Geometry");

    ui::ActionButton(*this, "geometry.lower_res", ImVec2(half, 0));
    ImGui::SameLine();
    ui::ActionButton(*this, "geometry.higher_res", ImVec2(half, 0));

    // Add Level with its Smooth toggle beside it.
    const float smoothW = ImGui::CalcTextSize(tr("Smooth")).x + 2.0f * style.FramePadding.x + 6.0f * uiScale_;
    ui::ActionButton(*this, "geometry.divide", ImVec2(ImGui::GetContentRegionAvail().x - smoothW - style.ItemSpacing.x, 0),
                     ui::Look::IconLabel);
    ImGui::SameLine();
    ui::ActionButton(*this, "geometry.smt", ImVec2(-1, 0));
    ui::ActionButton(*this, "geometry.reconstruct", ImVec2(-1, 0));
    // The two that throw levels away come last (each asks first).
    ui::ActionButton(*this, "geometry.del_lower", ImVec2(half, 0));
    ImGui::SameLine();
    ui::ActionButton(*this, "geometry.del_higher", ImVec2(half, 0));

    if (levels) {
        char info[96];
        std::snprintf(info, sizeof info, "Level %d of %d - base %s, top %s tris", current + 1, levels,
                      triangleCount(subdivLevelTriangles(m, 0)).c_str(),
                      triangleCount(subdivLevelTriangles(m, levels - 1)).c_str());
        ImGui::TextDisabled("%s", info);
    } else {
        ImGui::TextDisabled("%s", tr("No subdivision levels - Add Level adds them"));
    }
    drawEdgesSection(); // Edges: Edges mode, Select by Angle (PanelEdges.cpp)

    // Micro: displacement levels above the highest level (core/Micro.h), in a group of its own: Add Level and Remove
    // Below / Remove Above change the levels, Micro only adds detail on top (a tester mixed them up side by side).
    ImGui::Spacing();
    tray::GroupLabel(*this, "Micro Detail");
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

    // Resolution: these change the mesh itself and keep no levels.
    ImGui::Spacing();
    tray::GroupLabel(*this, "Resolution (no levels)");
    if (ImGui::Button(tr("Decimate"), ImVec2(half, 0))) {
        auto decimate = [this] {
            Args args{{"ratio", double(decimateRatio_)}};
            if (decimateKeepMasked_) args["keep_masked"] = true;
            runCommand("decimate", args);
        };
        if (unhideAllows("Decimate", decimate)) decimate(); // #163
    }
    ui::HelpText("Decimate", nullptr, "Reduce the triangle count while keeping the shape. Sculpt layers are kept.",
                 "Tool > Geometry");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1);
    ui::SliderInside("##decimate", &decimateRatio_, 0.05f, 0.95f, "Keep %.2f");
    ui::HelpText("Keep", nullptr, "Fraction of the triangles Decimate keeps.", "Tool > Geometry");
    ImGui::Checkbox(tr("Keep Masked##decimate"), &decimateKeepMasked_);
    ui::HelpText("Keep Masked", nullptr,
                 "Decimate leaves masked vertices where they are, so masked areas keep their triangles exactly; "
                 "the rest is decimated around them.",
                 "Tool > Geometry");
    ui::ActionButton(*this, "geometry.subdivide", ImVec2(-1, 0)); // (its own row: the long label needs the width)
    Demo::pushScope("QRemesh");
    drawQRemeshContents(); // quad retopology (PanelRetopo.cpp)
    Demo::popScope();

    ImGui::Spacing();
    tray::GroupLabel(*this, "Cleanup");
    ui::ActionButton(*this, "geometry.close_holes", ImVec2(-1, 0), ui::Look::IconLabel);
    drawMeshCheckContents(); // Merge by Distance, Find Problems (PanelMeshCheck.cpp)

    drawSymmetryContents(); // Make Symmetric (PanelExtract.cpp)
}

} // namespace sf
