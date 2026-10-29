// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// SubTools UI: the SubTools panel (list with visibility toggles, inline
// rename, append / duplicate / delete / reorder / solo / merge / split) and
// the ZBrush viewport gesture Alt+click to select a SubTool. Every action goes
// through the subtool_* commands (core/SubTools.cpp).
#include "app/App.h"
#include "app/Ui.h"
#include "core/Multires.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace sf {

namespace {

constexpr float kClickSlopPx = 4.0f; // movement that turns an Alt+click into a drag
constexpr int kListRows = 6;         // the list scrolls beyond this many SubTools

const SDL_DialogFileFilter kAppendFilters[] = {
    {"Models and projects", "obj;fbx;glb;gltf;ply;stl;GoZ;goz;usda;usd;usdz;sfproj"}, {"All files", "*"}};

void SDLCALL onAppendChosen(void* userdata, const char* const* files, int) {
    if (!files || !files[0]) return;
    auto* req = static_cast<App::DialogRequest*>(userdata);
    req->app->queueFile(req->action, files[0]);
}

// "960", "12.3k", "1.25M": triangle counts that fit a narrow column.
std::string shortCount(size_t n) {
    char buf[32];
    if (n < 1000) std::snprintf(buf, sizeof buf, "%zu", n);
    else if (n < 100000) std::snprintf(buf, sizeof buf, "%.1fk", double(n) / 1e3);
    else if (n < 1000000) std::snprintf(buf, sizeof buf, "%.0fk", double(n) / 1e3);
    else std::snprintf(buf, sizeof buf, "%.2fM", double(n) / 1e6);
    return buf;
}

// An eye: open (almond and pupil) or closed (lower lid and lashes).
void drawEye(ImDrawList* dl, ImVec2 c, float size, bool open, ImU32 col) {
    const float w = size * 0.5f, h = size * 0.42f, t = std::max(1.0f, size * 0.09f);
    const ImVec2 l(c.x - w, c.y), r(c.x + w, c.y);
    if (open) {
        dl->AddBezierQuadratic(l, ImVec2(c.x, c.y - h * 1.6f), r, col, t);
        dl->AddBezierQuadratic(l, ImVec2(c.x, c.y + h * 1.6f), r, col, t);
        dl->AddCircleFilled(c, size * 0.2f, col);
        return;
    }
    dl->AddBezierQuadratic(l, ImVec2(c.x, c.y + h * 1.3f), r, col, t);
    for (float u : {-0.55f, 0.0f, 0.55f}) {
        const ImVec2 a(c.x + u * w, c.y + h * 0.62f * (1.0f - u * u * 0.5f));
        dl->AddLine(a, ImVec2(a.x + u * size * 0.12f, a.y + size * 0.22f), col, t);
    }
}

} // namespace

int App::pickSubtool(float x, float y, RayHit* out) {
    glm::vec3 origin, dir;
    canvasRay(x, y, origin, dir);
    int best = -1;
    RayHit nearest;
    for (int i = 0; i < doc_.subtoolCount(); ++i) {
        SubTool& s = doc_.subtools[size_t(i)];
        if (!s.visible) continue;
        RayHit hit;
        bool found = false;
        if (i == doc_.activeSubtool) {
            doc_.syncBvh();
            found = doc_.bvh.raycast(doc_.mesh, origin, dir, hit);
        } else {
            if (s.bvhDirty || (s.bvh.nodeCount() == 0 && s.mesh.triangleCount())) {
                s.bvh.build(s.mesh);
                s.bvhDirty = false;
            }
            found = s.bvh.raycast(s.mesh, origin, dir, hit);
        }
        if (found && (best < 0 || hit.t < nearest.t)) {
            best = i;
            nearest = hit;
        }
    }
    if (out && best >= 0) *out = nearest;
    return best;
}

void App::subtoolClickMove(float x, float y) {
    if (std::hypot(x - subtoolClickX_, y - subtoolClickY_) < kClickSlopPx) return;
    // A drag, not a click: what an Alt+drag from here does on the active SubTool
    // (brushes ignore the others): an inverted stroke, or a pan off the model.
    drag_ = Drag::None;
    RayHit hit;
    if (previewLod_ < 0 && pick(subtoolClickX_, subtoolClickY_, hit)) {
        beginStroke(subtoolClickX_, subtoolClickY_, hit);
        if (drag_ == Drag::Stroke) strokeTo(x, y);
    } else {
        drag_ = Drag::Pan;
        camera_.pan(x - subtoolClickX_, y - subtoolClickY_, canvasHeight());
    }
}

void App::subtoolClickRelease() {
    if (subtoolClickTarget_ >= 0 && subtoolClickTarget_ < doc_.subtoolCount()) {
        if (subtoolClickTarget_ != doc_.activeSubtool) runCommand("subtool_select", {{"index", double(subtoolClickTarget_)}});
        if (subtoolClickFrame_) camera_.frame(doc_.mesh.bounds()); // Shift+Alt+click: frame it too
    }
    subtoolClickTarget_ = -1;
    subtoolClickFrame_ = false;
}

void App::drawSubToolsContents() {
    const float scale = uiScale_;
    const int count = doc_.subtoolCount();
    const int active = doc_.activeSubtool;
    const ImGuiStyle& style = ImGui::GetStyle();

    // The list: eye, name, triangles. Row actions run after the loop (they change the list).
    int selectIndex = -1, toggleIndex = -1, deleteIndex = -1, duplicateIndex = -1, renameIndex = -1;
    std::string renameTo;
    const ImVec2 cellPad(6.0f * scale, 4.0f * scale);
    const float rowH = ImGui::GetTextLineHeight() + 2.0f * cellPad.y;
    const float listH = rowH * float(std::min(count, kListRows)) + 2.0f;
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, cellPad);
    if (ImGui::BeginTable("##subtools", 4, ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter,
                          ImVec2(-1, listH))) {
        const float eyeW = ImGui::GetTextLineHeight() * 1.2f;
        ImGui::TableSetupColumn("name", ImGuiTableColumnFlags_WidthStretch);
        // Subdivision levels, as ZBrush lists them ("SDiv 3/5"); empty for meshes without levels.
        bool anyLevels = false;
        for (int i = 0; i < count && !anyLevels; ++i) anyLevels = subdivLevelCount(doc_.subtoolMesh(i)) > 0;
        ImGui::TableSetupColumn("sdiv", ImGuiTableColumnFlags_WidthFixed, anyLevels ? ImGui::CalcTextSize("SDiv 8/8").x : 0.0f);
        ImGui::TableSetupColumn("tris", ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize("0.00M").x);
        ImGui::TableSetupColumn("eye", ImGuiTableColumnFlags_WidthFixed, eyeW);
        for (int i = 0; i < count; ++i) {
            const SubTool& s = doc_.subtools[size_t(i)];
            const bool isActive = i == active;
            ImGui::PushID(int(s.id));
            ImGui::TableNextRow(ImGuiTableRowFlags_None, rowH);
            ImGui::TableSetColumnIndex(0);
            if (renamingSubtool_ == s.id) {
                ImGui::SetNextItemWidth(-1);
                if (focusRename_) {
                    ImGui::SetKeyboardFocusHere();
                    focusRename_ = false;
                }
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(style.FramePadding.x, 0.0f));
                const bool enter = ImGui::InputText("##rename", renameBuffer_, sizeof renameBuffer_,
                                                    ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
                ImGui::PopStyleVar();
                if (enter || (ImGui::IsItemDeactivatedAfterEdit() && !ImGui::IsKeyPressed(ImGuiKey_Escape))) {
                    renameTo = renameBuffer_;
                    renameIndex = i;
                    renamingSubtool_ = 0;
                } else if (ImGui::IsItemDeactivated()) {
                    renamingSubtool_ = 0; // Escape or clicked away without a change
                }
            } else {
                if (!s.visible) ImGui::PushStyleColor(ImGuiCol_Text, style.Colors[ImGuiCol_TextDisabled]);
                if (ImGui::Selectable(s.name.c_str(), isActive,
                                      ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap |
                                          ImGuiSelectableFlags_AllowDoubleClick | ImGuiSelectableFlags_NoAutoClosePopups) &&
                    !isActive)
                    selectIndex = i;
                if (isActive) // the selected row: a tint (ImGuiCol_Header) and a 2 px accent bar
                    ui::DrawOnMark(ImGui::GetWindowDrawList(), ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), true);
                if (!s.visible) ImGui::PopStyleColor();
                if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                    renamingSubtool_ = s.id;
                    focusRename_ = true;
                    std::snprintf(renameBuffer_, sizeof renameBuffer_, "%s", s.name.c_str());
                }
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
                    ImGui::SetTooltip("%s\n%zu vertices, %zu triangles%s%s\nClick to select, double-click to rename",
                                      s.name.c_str(), doc_.subtoolMesh(i).vertexCount(), doc_.subtoolMesh(i).triangleCount(),
                                      subdivLevelCount(doc_.subtoolMesh(i)) ? ", " : "",
                                      subdivLevelText(doc_.subtoolMesh(i)).c_str());
                if (ImGui::BeginPopupContextItem("##row")) {
                    ImGui::TextDisabled("%s", s.name.c_str());
                    if (ImGui::MenuItem("Rename")) {
                        renamingSubtool_ = s.id;
                        focusRename_ = true;
                        std::snprintf(renameBuffer_, sizeof renameBuffer_, "%s", s.name.c_str());
                    }
                    if (ImGui::MenuItem("Duplicate")) duplicateIndex = i;
                    if (ImGui::MenuItem("Delete...", nullptr, false, count > 1)) deleteIndex = i;
                    ImGui::EndPopup();
                }
                if (isActive && listedActiveSubtool_ != s.id) {
                    ImGui::SetScrollHereY(0.5f); // keep the active SubTool in view after Alt+click
                    listedActiveSubtool_ = s.id;
                }
            }
            ImGui::TableSetColumnIndex(1);
            if (const std::string sdiv = subdivLevelText(doc_.subtoolMesh(i)); !sdiv.empty()) {
                ImVec4 col = style.Colors[isActive ? ImGuiCol_Text : ImGuiCol_TextDisabled];
                if (isActive) col.w *= 0.75f;
                ImGui::TextColored(col, "%s", sdiv.c_str());
            }
            ImGui::TableSetColumnIndex(2);
            const std::string tris = shortCount(doc_.subtoolMesh(i).triangleCount());
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(tris.c_str()).x);
            ImVec4 trisCol = style.Colors[isActive ? ImGuiCol_Text : ImGuiCol_TextDisabled];
            if (isActive) trisCol.w *= 0.75f; // readable on the highlight
            ImGui::TextColored(trisCol, "%s", tris.c_str());
            ImGui::TableSetColumnIndex(3);
            const ImVec2 eyePos = ImGui::GetCursorScreenPos();
            const ImVec2 eyeSize(ImGui::GetContentRegionAvail().x, ImGui::GetTextLineHeight());
            if (ImGui::InvisibleButton(("##eye " + s.name).c_str(), eyeSize)) toggleIndex = i;
            const bool eyeHovered = ImGui::IsItemHovered();
            if (eyeHovered) ImGui::SetTooltip(s.visible ? "Hide %s" : "Show %s", s.name.c_str());
            const ImU32 eyeCol = ImGui::GetColorU32(eyeHovered    ? style.Colors[ImGuiCol_ButtonHovered]
                                                    : s.visible ? style.Colors[ImGuiCol_Text]
                                                                : style.Colors[ImGuiCol_TextDisabled]);
            drawEye(ImGui::GetWindowDrawList(), ImVec2(eyePos.x + eyeSize.x * 0.5f, eyePos.y + eyeSize.y * 0.5f),
                    ImGui::GetTextLineHeight() * 0.8f, s.visible, eyeCol);
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    ImGui::PopStyleVar();

    if (renameIndex >= 0 && !renameTo.empty() && renameTo != doc_.subtools[size_t(renameIndex)].name)
        runCommand("subtool_rename", {{"index", double(renameIndex)}, {"to", renameTo}});
    if (selectIndex >= 0) {
        runCommand("subtool_select", {{"index", double(selectIndex)}});
    }
    if (toggleIndex >= 0) runCommand("subtool_visibility", {{"index", double(toggleIndex)}});
    if (duplicateIndex >= 0) runCommand("subtool_duplicate", {{"index", double(duplicateIndex)}});

    // Buttons act on the active SubTool, as in ZBrush.
    const std::string activeName = doc_.activeSubtoolInfo().name;
    int visibleCount = 0;
    for (const auto& s : doc_.subtools) visibleCount += s.visible ? 1 : 0;
    auto confirm = [this](std::string title, std::string message, std::string button, std::string command) {
        subtoolConfirm_ = {std::move(title), std::move(message), std::move(button), std::move(command)};
        openSubtoolConfirm_ = true;
    };
    if (deleteIndex >= 0) {
        if (deleteIndex != doc_.activeSubtool) runCommand("subtool_select", {{"index", double(deleteIndex)}});
        confirm("Delete SubTool?", "Delete \"" + doc_.activeSubtoolInfo().name + "\"? This can't be undone.", "Delete",
                "subtool_delete");
    }
    const float third = (ImGui::GetContentRegionAvail().x - 2 * style.ItemSpacing.x) / 3.0f;
    if (ImGui::Button("Append", ImVec2(third, 0))) ImGui::OpenPopup("##append");
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("Add a new SubTool beside the model");
    if (ImGui::BeginPopup("##append")) {
        if (ImGui::MenuItem("Sphere")) runCommand("subtool_append", {{"type", std::string("sphere")}});
        if (ImGui::MenuItem("Cube")) runCommand("subtool_append", {{"type", std::string("cube")}});
        if (ImGui::MenuItem("Plane")) runCommand("subtool_append", {{"type", std::string("plane")}});
        ImGui::Separator();
        if (ImGui::MenuItem("From file..."))
            SDL_ShowOpenFileDialog(onAppendChosen, &dialogRequests_[int(FileAction::AppendSubtool)], window_,
                                   kAppendFilters, 2, nullptr, false);
        ImGui::EndPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Duplicate", ImVec2(third, 0))) runCommand("subtool_duplicate");
    ImGui::SameLine();
    ImGui::BeginDisabled(count < 2);
    if (ImGui::Button("Delete...", ImVec2(third, 0)))
        confirm("Delete SubTool?", "Delete \"" + activeName + "\"? This can't be undone.", "Delete", "subtool_delete");
    ImGui::EndDisabled();

    ImGui::BeginDisabled(active <= 0);
    if (ImGui::ArrowButton("##up", ImGuiDir_Up)) runCommand("subtool_move", {{"direction", std::string("up")}});
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_DelayNormal))
        ImGui::SetTooltip("Move up the list");
    ImGui::SameLine();
    ImGui::BeginDisabled(active >= count - 1);
    if (ImGui::ArrowButton("##down", ImGuiDir_Down)) runCommand("subtool_move", {{"direction", std::string("down")}});
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_DelayNormal))
        ImGui::SetTooltip("Move down the list");
    ImGui::SameLine();
    const float half2 = (ImGui::GetContentRegionAvail().x - style.ItemSpacing.x) * 0.5f;
    const bool solo = count > 1 && visibleCount == 1 && doc_.activeSubtoolInfo().visible;
    ImGui::BeginDisabled(count < 2);
    if (solo) ImGui::PushStyleColor(ImGuiCol_Button, style.Colors[ImGuiCol_ButtonActive]);
    if (ImGui::Button("Solo", ImVec2(half2, 0))) runCommand("subtool_solo");
    if (solo) ImGui::PopStyleColor();
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
        ImGui::SetTooltip(solo ? "Show the other SubTools again" : "Show only the active SubTool");
    ImGui::SameLine();
    ImGui::BeginDisabled(visibleCount == count);
    if (ImGui::Button("Unhide all", ImVec2(half2, 0))) runCommand("subtool_show_all");
    ImGui::EndDisabled();

    const float half = (ImGui::GetContentRegionAvail().x - style.ItemSpacing.x) * 0.5f;
    ImGui::BeginDisabled(active >= count - 1);
    if (ImGui::Button("Merge down...", ImVec2(half, 0)))
        confirm("Merge Down?",
                "Merge \"" + doc_.subtools[size_t(std::min(active + 1, count - 1))].name + "\" into \"" + activeName +
                    "\"? This can't be undone,\nbut Split groups separates them again.",
                "Merge", "subtool_merge_down");
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_DelayNormal))
        ImGui::SetTooltip("Merge the active SubTool with the one below it");
    ImGui::SameLine();
    ImGui::BeginDisabled(visibleCount < 2);
    if (ImGui::Button("Merge visible...", ImVec2(half, 0)))
        confirm("Merge Visible?",
                "Merge the " + std::to_string(visibleCount) + " visible SubTools into one? This can't be undone,\n"
                "but Split groups separates them again.",
                "Merge", "subtool_merge_visible");
    ImGui::EndDisabled();

    // More than one polygroup? Counted only when the active mesh changes.
    const uint64_t key = (uint64_t(doc_.activeSubtoolInfo().id) << 40) ^ (doc_.undo.revision() * 0x9E3779B97F4A7C15ull) ^
                         uint64_t(doc_.mesh.triangleCount());
    if (key != splitGroupsKey_) {
        splitGroupsKey_ = key;
        const auto& g = doc_.mesh.polygroups;
        splitGroupsPossible_ = g.size() == doc_.mesh.triangleCount() &&
                               std::any_of(g.begin(), g.end(), [&](uint32_t id) { return id != g.front(); });
    }
    ImGui::BeginDisabled(!splitGroupsPossible_);
    if (ImGui::Button("Split groups", ImVec2(half, 0))) runCommand("subtool_split_groups");
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_DelayNormal))
        ImGui::SetTooltip("One SubTool per polygroup");
    ImGui::SameLine();
    ImGui::BeginDisabled(doc_.mesh.triangleCount() == 0);
    if (ImGui::Button("Split parts", ImVec2(half, 0))) runCommand("subtool_split_parts");
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_DelayNormal))
        ImGui::SetTooltip("One SubTool per disconnected piece");
    ImGui::TextDisabled("Alt+click a SubTool to select it");
}

void App::drawSubToolPopups() {
    const std::string popup = subtoolConfirm_.title + "###" + subtoolConfirm_.command;
    if (openSubtoolConfirm_) {
        ImGui::OpenPopup(popup.c_str());
        openSubtoolConfirm_ = false;
    }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::BeginPopupModal(popup.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
    ImGui::TextUnformatted(subtoolConfirm_.message.c_str());
    ImGui::Spacing();
    const float w = 110.0f * uiScale_;
    if (ImGui::Button(subtoolConfirm_.button.c_str(), ImVec2(w, 0)) || ImGui::IsKeyPressed(ImGuiKey_Enter)) {
        ImGui::CloseCurrentPopup();
        runCommand(subtoolConfirm_.command);
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(w, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

} // namespace sf
