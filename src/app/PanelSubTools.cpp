// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// SubTools UI: the SubTools panel (list with visibility toggles, inline
// rename, append / duplicate / delete / reorder / solo / merge / split) and
// the ZBrush viewport gesture Alt+click to select a SubTool. Every action goes
// through the subtool_* commands (core/SubTools.cpp).
#include "app/App.h"
#include "app/Demo.h"
#include "app/Theme.h"
#include "app/I18n.h"
#include "core/Multires.h"
#include "render/GL.h"

#include <cctype>
#include <imgui_internal.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace sf {

namespace {

constexpr float kClickSlopPx = 4.0f; // movement that turns an Alt+click into a drag
constexpr Uint64 kPartClickNs = 500000000ull; // a pen pressed longer than this isn't an Alt+click
constexpr int kListRows = 6;         // the list scrolls beyond this many SubTools
// The SubTool list's colours; should become theme tokens (Theme.h).
// The recessed area behind the rows, a step below panelBg (the Classic look's #1c1c1f; other looks their shelf shade).
ImVec4 listWell() { return theme::look.id == 0 ? theme::rgb(0x1c1c1f) : theme::shellBg; }
constexpr ImVec4 kRowSelected = theme::rgb(0xffffff, 0.09f); // the active row: lighter neutral, plus the accent bar
constexpr ImVec4 kRowHover = theme::rgb(0xffffff, 0.11f);
constexpr ImVec4 kRowPressed = theme::rgb(0xffffff, 0.14f);
constexpr ImVec4 kThumbWell = theme::rgb(0x000000, 0.22f); // behind a SubTool thumbnail (#43)

const SDL_DialogFileFilter kAppendFilters[] = {
    {"Models and projects", "obj;fbx;glb;gltf;ply;stl;GoZ;goz;usda;usd;usdz;sfproj"}, {"All files", "*"}};
const SDL_DialogFileFilter kInsertFilters[] = {{"Meshes (OBJ, PLY, GLB)", "obj;ply;glb;gltf"}, {"All files", "*"}};

void SDLCALL onAppendChosen(void* userdata, const char* const* files, int) {
    wakeMainLoop(); // a file dialog answered (maybe on another thread): redraw now
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
    // A pen pressed this long was held down to sculpt, not clicked (and Windows Ink's press-and-hold).
    const bool held = (dragFromPen_ || wintabPen_) && clickClockNs() - subtoolClickNs_ > kPartClickNs;
    if (!held && subtoolClickTarget_ >= 0 && subtoolClickTarget_ < doc_.subtoolCount()) {
        if (subtoolClickTarget_ != doc_.activeSubtool) {
            runCommand("subtool_select", {{"index", double(subtoolClickTarget_)}});
            partSelected("Alt+click");
        }
        if (subtoolClickFrame_) camera_.frame(doc_.mesh.bounds()); // Shift+Alt+click: frame it too
    }
    subtoolClickTarget_ = -1;
    subtoolClickFrame_ = false;
}

// Every Part switch from the viewport or the list says which Part and how (an unexpected switch had users
// guessing which key they had hit).
Uint64 App::clickClockNs() const {
    return demo_ ? Uint64(double(demo_->clock()) * 1e9) : SDL_GetTicksNS(); // UI tests: the script's time
}

void App::partSelected(const char* how) {
    if (!lastCommandOk_) return;
    status_ = "Active Part: " + doc_.activeSubtoolInfo().name + " (" + how + ")";
}

void App::updateSubtoolThumbnails() {
    // #43: what each SubTool looks like now; the schedule picks at most one to render this frame, from the GPU
    // buffers syncScene just brought up to date (never a CPU render, never a re-upload: cheap on 30M+ meshes).
    std::vector<uint32_t> ids;
    ids.reserve(doc_.subtools.size());
    const double now = double(SDL_GetTicksNS()) * 1e-9; // (the main loop's clock, as redrawAt_)
    for (int i = 0; i < doc_.subtoolCount(); ++i) {
        const SubTool& s = doc_.subtools[size_t(i)];
        const Mesh& m = doc_.subtoolMesh(i);
        const UndoStack& undo = i == doc_.activeSubtool ? doc_.undo : s.undo;
        uint64_t key = undo.revision();
        for (uint64_t part : {uint64_t(m.triangleCount()), uint64_t(m.vertexCount()), uint64_t(s.visible),
                              uint64_t(s.shading), uint64_t(renderSettings_.matcap + 1), uint64_t(renderSettings_.showPolypaint)})
            key = key * 1099511628211ull ^ part;
        subtoolThumbSchedule_.observe(s.id, key, now);
        ids.push_back(s.id);
    }
    for (uint32_t gone : subtoolThumbSchedule_.keepOnly(ids))
        if (auto it = subtoolThumbs_.find(gone); it != subtoolThumbs_.end()) {
            gl::DeleteTextures(1, &it->second);
            subtoolThumbs_.erase(it);
        }
    const bool busy = drag_ != Drag::None || strokeFinishing_ || solverBusy() || doc_.sculptor.strokeActive();
    uint32_t id = 0;
    double wakeAt = 0.0;
    if (subtoolThumbSchedule_.due(now, busy, id, wakeAt)) {
        constexpr int kPixels = 96; // shown at about 34 px: crisp on HiDPI
        std::vector<uint8_t> rgba;
        std::string error;
        if (renderer_.renderSubtoolThumbnail(id, 30.0f, -15.0f, kPixels, renderSettings_, rgba, error)) {
            unsigned& tex = subtoolThumbs_[id];
            if (!tex) gl::GenTextures(1, &tex);
            gl::BindTexture(GL_TEXTURE_2D, tex);
            gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            gl::PixelStorei(GL_UNPACK_ALIGNMENT, 1);
            gl::TexImage2D(GL_TEXTURE_2D, 0, GLint(GL_RGBA8), kPixels, kPixels, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
            gl::BindTexture(GL_TEXTURE_2D, 0);
        }
        subtoolThumbSchedule_.rendered(id); // (one that couldn't render, e.g. hidden before its first upload, waits for a change)
        uint32_t next = 0;
        if (subtoolThumbSchedule_.due(now, busy, next, wakeAt)) wakeAt = now; // another waiting: the next frame
    }
    if (wakeAt > 0.0) redrawAt_ = redrawAt_ > 0.0 ? std::min(redrawAt_, wakeAt) : wakeAt;
}

void App::clearSubtoolThumbnails() {
    for (auto& [id, tex] : subtoolThumbs_) gl::DeleteTextures(1, &tex);
    subtoolThumbs_.clear();
    subtoolThumbSchedule_.keepOnly({});
}

void App::drawSubToolsContents() {
    subtoolPanelShown_ = true; // its thumbnails keep up while it shows (updateSubtoolThumbnails)
    const float scale = uiScale_;
    const int count = doc_.subtoolCount();
    const int active = doc_.activeSubtool;
    const ImGuiStyle& style = ImGui::GetStyle();

    // The list: eye, name, triangles. Row actions run after the loop (they change the list).
    int selectIndex = -1, toggleIndex = -1, deleteIndex = -1, duplicateIndex = -1, renameIndex = -1;
    std::string renameTo;
    const ImVec2 cellPad(6.0f * scale, 4.0f * scale);
    // Each row starts with the SubTool's thumbnail (#43); the text is centred on its height.
    const float thumb = 30.0f * scale, lineH = ImGui::GetTextLineHeight();
    const float innerH = std::max(lineH, thumb), textDy = (innerH - lineH) * 0.5f;
    const float rowH = innerH + 2.0f * cellPad.y;
    const float listH = rowH * float(std::min(count, kListRows)) + 2.0f;
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, cellPad);
    // The list sits in a recessed well, no outline. The active row is a lighter neutral with the accent only as the
    // 2 px bar at its left edge (docs/ui-design.md §10), so it doesn't read as a dimmed orange fill.
    {
        const ImVec2 w0 = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddRectFilled(w0, ImVec2(w0.x + ImGui::GetContentRegionAvail().x, w0.y + listH),
                                                  ImGui::ColorConvertFloat4ToU32(listWell()), style.FrameRounding);
    }
    ImGui::PushStyleColor(ImGuiCol_Header, kRowSelected);
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, kRowHover);
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, kRowPressed);
    if (ImGui::BeginTable("##subtools", 4, ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_PadOuterX, ImVec2(-1, listH))) {
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
            const ImVec2 thumbAt = ImGui::GetCursorScreenPos();
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + thumb + 6.0f * scale);
            if (renamingSubtool_ == s.id) {
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() + textDy);
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
                ImGui::PushStyleVar(ImGuiStyleVar_SelectableTextAlign, ImVec2(0.0f, 0.5f));
                if (ImGui::Selectable(s.name.c_str(), isActive,
                                      ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap |
                                          ImGuiSelectableFlags_AllowDoubleClick | ImGuiSelectableFlags_NoAutoClosePopups,
                                      ImVec2(0.0f, innerH)) &&
                    !isActive)
                    selectIndex = i;
                ImGui::PopStyleVar();
                if (!s.visible) ImGui::PopStyleColor();
                if (isActive) {
                    const ImVec2 r0 = ImGui::GetItemRectMin(), r1 = ImGui::GetItemRectMax();
                    const float x = ImGui::TableGetCellBgRect(ImGui::GetCurrentTable(), 0).Min.x;
                    ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(x, r0.y), ImVec2(x + 2.0f * scale, r1.y),
                                                              theme::u32(theme::accent));
                }
                if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                    renamingSubtool_ = s.id;
                    focusRename_ = true;
                    std::snprintf(renameBuffer_, sizeof renameBuffer_, "%s", s.name.c_str());
                }
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
                    ImGui::SetTooltip(tr("%s\n%zu vertices, %zu triangles%s%s\nClick to select, double-click to rename"),
                                      s.name.c_str(), doc_.subtoolMesh(i).vertexCount(), doc_.subtoolMesh(i).triangleCount(),
                                      subdivLevelCount(doc_.subtoolMesh(i)) ? ", " : "",
                                      subdivLevelText(doc_.subtoolMesh(i)).c_str());
                if (ImGui::BeginPopupContextItem("##row")) {
                    ImGui::TextDisabled("%s", s.name.c_str());
                    if (ImGui::MenuItem(tr("Rename"))) {
                        renamingSubtool_ = s.id;
                        focusRename_ = true;
                        std::snprintf(renameBuffer_, sizeof renameBuffer_, "%s", s.name.c_str());
                    }
                    if (ImGui::MenuItem(tr("Duplicate"), actions_.shortcutText("subtool.duplicate").c_str())) duplicateIndex = i;
                    if (ImGui::MenuItem(tr("Delete..."), nullptr, false, count > 1)) deleteIndex = i;
                    ImGui::EndPopup();
                }
                if (isActive && listedActiveSubtool_ != s.id) {
                    ImGui::SetScrollHereY(0.5f); // keep the active SubTool in view after Alt+click
                    listedActiveSubtool_ = s.id;
                }
            }
            {
                // The thumbnail, or a quiet placeholder until it's rendered (updateSubtoolThumbnails).
                ImDrawList* dl = ImGui::GetWindowDrawList();
                const ImVec2 t1(thumbAt.x + thumb, thumbAt.y + thumb);
                dl->AddRectFilled(thumbAt, t1, ImGui::ColorConvertFloat4ToU32(kThumbWell), theme::rounding(3.0f * scale));
                const auto tex = subtoolThumbs_.find(s.id);
                if (tex != subtoolThumbs_.end() && tex->second)
                    dl->AddImageRounded(ImTextureID(uintptr_t(tex->second)), thumbAt, t1, ImVec2(0, 0), ImVec2(1, 1),
                                        IM_COL32(255, 255, 255, s.visible ? 255 : 110), theme::rounding(3.0f * scale));
                else
                    dl->AddCircle(ImVec2(thumbAt.x + thumb * 0.5f, thumbAt.y + thumb * 0.5f), thumb * 0.28f,
                                  ImGui::GetColorU32(style.Colors[ImGuiCol_TextDisabled]), 0, 1.0f * scale);
            }
            ImGui::TableSetColumnIndex(1);
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + textDy);
            if (const std::string sdiv = subdivLevelText(doc_.subtoolMesh(i)); !sdiv.empty()) {
                ImVec4 col = style.Colors[isActive ? ImGuiCol_Text : ImGuiCol_TextDisabled];
                if (isActive) col.w *= 0.75f;
                ImGui::TextColored(col, "%s", sdiv.c_str());
            }
            ImGui::TableSetColumnIndex(2);
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + textDy);
            const std::string tris = shortCount(doc_.subtoolMesh(i).triangleCount());
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(tris.c_str()).x);
            ImVec4 trisCol = style.Colors[isActive ? ImGuiCol_Text : ImGuiCol_TextDisabled];
            if (isActive) trisCol.w *= 0.75f; // readable on the highlight
            ImGui::TextColored(trisCol, "%s", tris.c_str());
            ImGui::TableSetColumnIndex(3);
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + textDy);
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
    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar();

    if (renameIndex >= 0 && !renameTo.empty() && renameTo != doc_.subtools[size_t(renameIndex)].name)
        runCommand("subtool_rename", {{"index", double(renameIndex)}, {"to", renameTo}});
    if (selectIndex >= 0) {
        runCommand("subtool_select", {{"index", double(selectIndex)}});
        partSelected("Parts list");
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
        confirm("Delete Part?", "Delete \"" + doc_.activeSubtoolInfo().name + "\"? This can't be undone.", "Delete",
                "subtool_delete");
    }
    const float third = (ImGui::GetContentRegionAvail().x - 2 * style.ItemSpacing.x) / 3.0f;
    if (ImGui::Button(tr("Append"), ImVec2(third, 0))) ImGui::OpenPopup("##append");
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("%s", tr("Add a new Part beside the model"));
    if (ImGui::BeginPopup("##append")) {
        if (ImGui::MenuItem(tr("Sphere"))) runCommand("subtool_append", {{"type", std::string("sphere")}});
        if (ImGui::MenuItem(tr("Cube"))) runCommand("subtool_append", {{"type", std::string("cube")}});
        if (ImGui::MenuItem(tr("Plane"))) runCommand("subtool_append", {{"type", std::string("plane")}});
        for (const char* t : {"cylinder", "cone", "torus", "capsule", "icosphere", "quadsphere"}) { // #31
            std::string label = t;
            label[0] = char(std::toupper(static_cast<unsigned char>(label[0])));
            if (label == "Quadsphere") label = "Quad Sphere";
            if (ImGui::MenuItem(label.c_str())) runCommand("subtool_append", {{"type", std::string(t)}});
        }
        ImGui::Separator();
        if (ImGui::BeginMenu(tr("Insert on the model"))) { // #34: drag on the surface to place it
            for (const char* t : {"sphere", "cube", "cylinder", "cone", "torus", "capsule", "quadsphere"}) {
                std::string label = t;
                label[0] = char(std::toupper(static_cast<unsigned char>(label[0])));
                if (label == "Quadsphere") label = "Quad Sphere";
                if (ImGui::MenuItem((label + "##insert").c_str(), nullptr, insertType_ == t)) { // (Append's own Cone is beside it)
                    insertType_ = t;
                    status_ = "Insert " + label + ": drag on the model (the drag sets its size). Esc stops.";
                }
            }
            ImGui::Separator();
            // Your own mesh (#34): a copy of another SubTool, or a model file you choose.
            if (ImGui::BeginMenu(tr("Active Part's copy..."), count > 1)) {
                for (int i = 0; i < count; ++i) {
                    if (i == active) continue;
                    const std::string& name = doc_.subtools[size_t(i)].name;
                    const bool chosen = insertType_ == "mesh" && insertMeshKey_ == "subtool" && insertMeshValue_ == name;
                    if (ImGui::MenuItem((name + "##insertcopy" + std::to_string(i)).c_str(), nullptr, chosen)) {
                        insertType_ = "mesh";
                        insertMeshKey_ = "subtool";
                        insertMeshValue_ = name;
                        status_ = "Insert a copy of " + name + ": drag on the model (the drag sets its size). Esc stops.";
                    }
                }
                ImGui::EndMenu();
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_DelayNormal))
                ImGui::SetTooltip(count > 1 ? "Insert a copy of another Part into the active one"
                                            : "Append another Part first");
            if (ImGui::MenuItem(tr("From file...##insert")))
                SDL_ShowOpenFileDialog(onAppendChosen, &dialogRequests_[int(FileAction::InsertMesh)], window_,
                                       kInsertFilters, 2, nullptr, false);
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem(tr("From file...")))
            SDL_ShowOpenFileDialog(onAppendChosen, &dialogRequests_[int(FileAction::AppendSubtool)], window_,
                                   kAppendFilters, 2, nullptr, false);
        ImGui::EndPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button(tr("Duplicate"), ImVec2(third, 0))) runCommand("subtool_duplicate");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip(tr("Copy the active Part (%s)"), actions_.shortcutText("subtool.duplicate").c_str());
    ImGui::SameLine();
    ImGui::BeginDisabled(count < 2);
    if (ImGui::Button(tr("Delete..."), ImVec2(third, 0)))
        confirm("Delete Part?", "Delete \"" + activeName + "\"? This can't be undone.", "Delete", "subtool_delete");
    ImGui::EndDisabled();

    const float half2 = (ImGui::GetContentRegionAvail().x - style.ItemSpacing.x) * 0.5f;
    const float arrowW = ImGui::GetFrameHeight();
    ImGui::BeginDisabled(active <= 0);
    if (ImGui::ArrowButton("##up", ImGuiDir_Up)) runCommand("subtool_move", {{"direction", std::string("up")}});
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_DelayNormal))
        ImGui::SetTooltip("%s", tr("Move up the list"));
    ImGui::SameLine();
    ImGui::BeginDisabled(active >= count - 1);
    if (ImGui::ArrowButton("##down", ImGuiDir_Down)) runCommand("subtool_move", {{"direction", std::string("down")}});
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_DelayNormal))
        ImGui::SetTooltip("%s", tr("Move down the list"));
    ImGui::SameLine();
    const bool solo = count > 1 && visibleCount == 1 && doc_.activeSubtoolInfo().visible;
    ImGui::BeginDisabled(count < 2);
    if (solo) ImGui::PushStyleColor(ImGuiCol_Button, style.Colors[ImGuiCol_ButtonActive]);
    if (ImGui::Button(tr("Solo"), ImVec2(half2 - 2.0f * (arrowW + style.ItemSpacing.x), 0))) runCommand("subtool_solo");
    if (solo) ImGui::PopStyleColor();
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
        ImGui::SetTooltip(solo ? "Show the other Parts again" : "Show only the active Part");
    ImGui::SameLine();
    ImGui::BeginDisabled(visibleCount == count);
    if (ImGui::Button(tr("Unhide all"), ImVec2(half2, 0))) runCommand("subtool_show_all");
    ImGui::EndDisabled();

    const float half = (ImGui::GetContentRegionAvail().x - style.ItemSpacing.x) * 0.5f;
    ImGui::BeginDisabled(active >= count - 1);
    if (ImGui::Button(tr("Merge down..."), ImVec2(half, 0)))
        confirm("Merge Down?",
                "Merge \"" + doc_.subtools[size_t(std::min(active + 1, count - 1))].name + "\" into \"" + activeName +
                    "\"? This can't be undone,\nbut Split groups separates them again.",
                "Merge", "subtool_merge_down");
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_DelayNormal))
        ImGui::SetTooltip("%s", tr("Merge the active Part with the one below it"));
    ImGui::SameLine();
    ImGui::BeginDisabled(visibleCount < 2);
    if (ImGui::Button(tr("Merge visible..."), ImVec2(half, 0)))
        confirm("Merge Visible?",
                "Merge the " + std::to_string(visibleCount) + " visible Parts into one? This can't be undone,\n"
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
    if (ImGui::Button(tr("Split groups"), ImVec2(half, 0))) runCommand("subtool_split_groups");
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_DelayNormal))
        ImGui::SetTooltip("%s", tr("One Part per face group"));
    ImGui::SameLine();
    ImGui::BeginDisabled(doc_.mesh.triangleCount() == 0);
    if (ImGui::Button(tr("Split parts"), ImVec2(half, 0))) runCommand("subtool_split_parts");
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_DelayNormal))
        ImGui::SetTooltip("%s", tr("One Part per disconnected piece"));
    if (fontSmall_) ImGui::PushFont(fontSmall_);
    ImGui::TextDisabled("%s", tr("Alt+click a Part to select it"));
    if (fontSmall_) ImGui::PopFont();
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
    if (ImGui::Button(tr("Cancel"), ImVec2(w, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

} // namespace sf
