// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Tool > Geometry > Merge by Distance and Find Problems (foirgairwolf on Discord). The work is done by the weld_points,
// mesh_check and mesh_fix commands (core/MeshCheck.h); this is their panel and the weld's preview on the model.
#include "app/App.h"
#include "app/Demo.h"
#include "app/I18n.h"
#include "app/Palettes.h"
#include "app/Theme.h"
#include "app/Ui.h"

#include <algorithm>
#include <string>

namespace sf {

namespace {

// The count follows the slider live up to this many points; above it, when the slider is let go.
constexpr size_t kLiveWeldPoints = 1500000;
constexpr size_t kWeldPreviewPoints = 20000;
// The preview shows only while the distance is dragged or hovered (an open panel alone draws nothing: on a dense
// model the dots read as a rendering fault), stays a moment after (the overlay may draw before the panel in a
// frame), then fades out.
constexpr double kWeldPreviewHold = 0.1, kWeldPreviewFade = 0.3;

} // namespace

uint64_t App::meshCheckKey() const {
    const Mesh& m = doc_.mesh;
    return (uint64_t(doc_.activePartInfo().id) << 48) ^ (doc_.undo.revision() * 0x9E3779B97F4A7C15ull) ^
           (uint64_t(m.triangleCount()) << 20) ^ uint64_t(m.vertexCount()) ^ uint64_t(reinterpret_cast<uintptr_t>(m.positions.data()));
}

void App::runMeshCheck() {
    meshReport_ = checkMesh(doc_.mesh);
    meshReportKey_ = meshCheckKey() | 1; // (never 0: checked)
    meshReportScroll_ = 2; // (two frames: the first one's scroll is clamped to the size before the rows)
}

void App::drawMeshCheckContents() {
    const Mesh& m = doc_.mesh;
    const ImGuiStyle& style = ImGui::GetStyle();
    const float diag = m.vertexCount() ? glm::length(m.bounds().extent()) : 1.0f;
    const uint64_t key = meshCheckKey() | 1;

    Demo::pushScope("Merge by Distance");
    tray::GroupLabel(*this, "Merge by Distance");
    if (weldDistance_ < 0.0f) weldDistance_ = diag * 1e-4f; // a ten-thousandth of the Part's size
    const float hi = std::max(diag * 0.02f, weldDistance_);
    const float button = ImGui::CalcTextSize(tr("Merge")).x + 2.0f * style.FramePadding.x + ImGui::GetFrameHeight();
    ImGui::BeginDisabled(weldCount_ == 0 || weldKey_ != key || weldKeyDistance_ != weldDistance_);
    if (ImGui::Button(tr("Merge##weld_points"), ImVec2(button, 0))) {
        runCommand("weld_points", {{"distance", double(weldDistance_)}});
        weldKey_ = 0;
        meshReportScroll_ = 2; // Find Problems is next: its row comes into view (below the fold in a 1024 px drop-down)
    }
    ImGui::EndDisabled();
    ui::HelpText("Merge", nullptr,
                 "Merge the points of the active Part that are closer than the distance into one (an unwelded import, "
                 "seams left by another program). Triangles that collapse are removed. One undo step.",
                 "Tool > Geometry > Merge by Distance");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1);
    ui::SliderInside("##weld_distance", &weldDistance_, 0.0f, hi, "Distance %.5f", ImGuiSliderFlags_Logarithmic);
    const bool dragging = ImGui::IsItemActive();
    if (dragging || ui::HoveredOrFocused(ImGui::GetItemID())) weldUsedAt_ = ImGui::GetTime(); // (focused: by the keyboard)
    ui::HelpText("Distance", nullptr,
                 "Points this close (in scene units) or closer merge. 0 merges only points exactly on top of each "
                 "other. The points that would merge show on the model in orange.",
                 "Tool > Geometry > Merge by Distance");
    // The count (and the preview's points) for this mesh and distance: live while dragging on smaller meshes.
    if ((weldKey_ != key || weldKeyDistance_ != weldDistance_) && (!dragging || m.vertexCount() <= kLiveWeldPoints)) {
        std::vector<uint32_t> target = weldTargets(m, weldDistance_, &weldCount_);
        weldPoints_.clear();
        const size_t step = std::max<size_t>(1, weldCount_ / kWeldPreviewPoints);
        size_t seen = 0;
        for (uint32_t v = 0; v < target.size(); ++v)
            if (target[v] != v && seen++ % step == 0) weldPoints_.push_back(m.positions[v]);
        weldKey_ = key;
        weldKeyDistance_ = weldDistance_;
    }
    if (weldKey_ == key && weldKeyDistance_ == weldDistance_)
        ImGui::TextDisabled(weldCount_ == 1 ? tr("%zu point will merge") : tr("%zu points will merge"), weldCount_);
    else
        ImGui::TextDisabled("%s", tr("Let go of the slider to count"));
    Demo::popScope();

    Demo::pushScope("Find Problems");
    tray::GroupLabel(*this, "Find Problems");
    const float half = (ImGui::GetContentRegionAvail().x - style.ItemSpacing.x) * 0.5f;
    const bool current = meshReportKey_ == key;
    if (ImGui::Button(tr("Check##mesh_check"), ImVec2(half, 0))) runMeshCheck();
    ui::HelpText("Check", nullptr,
                 "Look for what makes a mesh hard to sculpt, remesh, print or export. Checking changes nothing; each "
                 "Fix changes only what its row names, as one undo step.",
                 "Tool > Geometry > Find Problems");
    ImGui::SameLine();
    size_t fixable = 0;
    for (int i = 0; i < kMeshIssueCount; ++i)
        if (MeshIssue(i) != MeshIssue::Holes) fixable += meshReport_.count[i];
    ImGui::BeginDisabled(!current || fixable == 0);
    if (ImGui::Button(tr("Fix all##mesh_fix"), ImVec2(-1, 0))) {
        runCommand("mesh_fix", {{"issue", std::string("all")}});
        runMeshCheck();
    }
    ImGui::EndDisabled();
    ui::HelpText("Fix all", nullptr,
                 "Fix every problem listed except holes, as one undo step. Holes stay open: a mesh can be open on "
                 "purpose (use their own Fix to cap them).",
                 "Tool > Geometry > Find Problems");
    if (meshReportKey_ == 0) {
        ImGui::TextDisabled("%s", tr("Not checked yet"));
    } else if (!current) {
        ImGui::TextDisabled("%s", tr("The mesh changed: check again"));
    } else if (meshReport_.total() == 0) {
        ImGui::TextColored(theme::text, "%s", tr("No problems found"));
    } else {
        // Per MeshIssue: the row's name and what it means / what its Fix does.
        const char* const rows[kMeshIssueCount][2] = {
            {tr("Non-manifold edges"), tr("Edges shared by three or more triangles. Fix gives each extra triangle its "
                                          "own copy of the edge, so it comes loose; nothing is deleted.")},
            {tr("Holes"), tr("Open boundaries. Fix caps each one with a patch, as Fill Holes does.")},
            {tr("Duplicate triangles"), tr("Triangles on the same three points as another. Fix deletes the copies.")},
            {tr("Degenerate triangles"), tr("Triangles that use one point twice. Fix deletes them.")},
            {tr("Zero-area triangles"), tr("Triangles whose three points are in one line. Fix collapses their shortest edge.")},
            {tr("Flipped triangles"), tr("Triangles wound against their neighbours, or whole closed pieces inside out. "
                                         "Fix turns them to match.")},
            {tr("Isolated points"), tr("Points no triangle uses. Fix removes them.")}};
        const float fixW = ImGui::CalcTextSize(tr("Fix")).x + 2.0f * style.FramePadding.x + ImGui::GetFrameHeight() * 0.5f;
        for (int i = 0; i < kMeshIssueCount; ++i) {
            if (!meshReport_.count[i]) continue;
            ImGui::PushID(i);
            ImGui::AlignTextToFramePadding();
            ImGui::Text("%s", rows[i][0]);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("%s", rows[i][1]);
            const std::string n = std::to_string(meshReport_.count[i]);
            ImGui::SameLine(ImGui::GetContentRegionMax().x - fixW - style.ItemSpacing.x - ImGui::CalcTextSize(n.c_str()).x);
            ImGui::TextColored(theme::accent, "%s", n.c_str());
            ImGui::SameLine(ImGui::GetContentRegionMax().x - fixW);
            if (ImGui::Button(tr("Fix"), ImVec2(fixW, 0))) {
                runCommand("mesh_fix", {{"issue", std::string(meshIssueName(MeshIssue(i)))}});
                runMeshCheck();
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("%s", rows[i][1]);
            ImGui::PopID();
        }
    }
    if (meshReportScroll_ > 0) { // Find Problems's last line (the report's, or "check again") above the bottom edge
        --meshReportScroll_;
        if (ImGui::GetCursorScreenPos().y > ImGui::GetWindowPos().y + ImGui::GetWindowHeight()) ImGui::SetScrollHereY(1.0f);
    }
    Demo::popScope();
}

void App::drawWeldPreviewOverlay() {
    weldPreviewDrawn_ = 0;
    const double since = ImGui::GetTime() - weldUsedAt_;
    if (since >= kWeldPreviewHold + kWeldPreviewFade || weldPoints_.empty() || weldKey_ != (meshCheckKey() | 1) || !doc_.activePartInfo().visible)
        return;
    redrawIn(0.0); // (fading out: the next frame comes even if the pointer rests)
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    const CanvasRect cr = canvasRect();
    dl->PushClipRect(ImVec2(cr.x, cr.y), ImVec2(cr.x + cr.w, cr.y + cr.h), true);
    const float alpha = float(std::clamp(1.0 - (since - kWeldPreviewHold) / kWeldPreviewFade, 0.0, 1.0));
    const ImU32 col = theme::u32(theme::accent, alpha);
    weldPreviewDrawn_ = weldPoints_.size();
    const float r = 2.0f * uiScale_;
    for (const glm::vec3& p : weldPoints_) {
        ImVec2 s;
        if (gizmoProject(p, s)) dl->AddCircleFilled(s, r, col, 6);
    }
    dl->PopClipRect();
}

} // namespace sf
