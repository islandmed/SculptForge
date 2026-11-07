// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Parts UI: the Parts panel (list with visibility toggles, inline
// rename, folders (#96), append / duplicate / delete / reorder / solo / merge / split) and
// the classic viewport gesture Alt+click to select a Part. Every action goes
// through the part_* commands (core/Parts.cpp).
#include "app/App.h"
#include "io/BridgeProtocol.h"
#include "app/Demo.h"
#include "app/Theme.h"
#include "app/I18n.h"
#include "core/Multires.h"
#include "render/GL.h"

#include <IconsLucide.h>
#include <cctype>
#include <cstring>
#include <imgui_internal.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace sf {

namespace {

constexpr float kClickSlopPx = 4.0f; // movement that turns an Alt+click into a drag
constexpr Uint64 kPartClickNs = 500000000ull; // a pen pressed longer than this isn't an Alt+click
constexpr int kListRows = 6;         // the list scrolls beyond this many Parts
// The Part list's colours; should become theme tokens (Theme.h).
// The recessed area behind the rows, a step below panelBg (the Classic look's #1c1c1f; other looks their shelf shade).
ImVec4 listWell() { return theme::look.id == 0 ? theme::rgb(0x1c1c1f) : theme::shellBg; }
constexpr ImVec4 kRowSelected = theme::rgb(0xffffff, 0.09f); // the active row: lighter neutral, plus the accent bar
constexpr ImVec4 kRowHover = theme::rgb(0xffffff, 0.11f);
constexpr ImVec4 kRowPressed = theme::rgb(0xffffff, 0.14f);
constexpr ImVec4 kThumbWell = theme::rgb(0x000000, 0.22f); // behind a Part thumbnail (#43)

const SDL_DialogFileFilter kAppendFilters[] = {
    {"Models and projects", "obj;fbx;glb;gltf;ply;stl;" SF_BRIDGE_MESH_EXTS ";usda;usd;usdz;sfproj"}, {"All files", "*"}};
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

int App::pickPart(float x, float y, RayHit* out) {
    glm::vec3 origin, dir;
    canvasRay(x, y, origin, dir);
    int best = -1;
    RayHit nearest;
    for (int i = 0; i < doc_.partCount(); ++i) {
        Part& s = doc_.parts[size_t(i)];
        if (!s.visible) continue;
        RayHit hit;
        bool found = false;
        if (i == doc_.activePart) {
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

void App::partClickMove(float x, float y) {
    if (std::hypot(x - partClickX_, y - partClickY_) < kClickSlopPx) return;
    // A drag, not a click: what an Alt+drag from here does on the active Part
    // (brushes ignore the others): an inverted stroke, or a pan off the model.
    drag_ = Drag::None;
    RayHit hit;
    if (previewLod_ < 0 && pick(partClickX_, partClickY_, hit)) {
        beginStroke(partClickX_, partClickY_, hit);
        if (drag_ == Drag::Stroke) strokeTo(x, y);
    } else {
        drag_ = Drag::Pan;
        camera_.pan(x - partClickX_, y - partClickY_, canvasHeight());
    }
}

void App::partClickRelease() {
    // A pen pressed this long was held down to sculpt, not clicked (and Windows Ink's press-and-hold).
    const bool held = (dragFromPen_ || wintabPen_) && clickClockNs() - partClickNs_ > kPartClickNs;
    if (!held && partClickTarget_ >= 0 && partClickTarget_ < doc_.partCount()) {
        if (partClickTarget_ != doc_.activePart) {
            runCommand("part_select", {{"index", double(partClickTarget_)}});
            partSelected("Alt+click");
        }
        if (partClickFrame_) camera_.frame(doc_.mesh.bounds()); // Shift+Alt+click: frame it too
    }
    partClickTarget_ = -1;
    partClickFrame_ = false;
}

// Every Part switch from the viewport or the list says which Part and how (an unexpected switch had users
// guessing which key they had hit).
Uint64 App::clickClockNs() const {
    return demo_ ? Uint64(double(demo_->clock()) * 1e9) : SDL_GetTicksNS(); // UI tests: the script's time
}

void App::partSelected(const char* how) {
    if (!lastCommandOk_) return;
    status_ = "Active Part: " + doc_.activePartInfo().name + " (" + how + ")";
}

// The Part-under-cursor key (Alt+Q in every input style, like Blender's Alt+Q; Discord user Ziel): the visible
// Part under the pointer becomes the one sculpted. The camera stays, and each Part keeps its own mask.
void App::sculptPartUnderCursor() {
    if (drag_ != Drag::None) return; // mid-stroke or mid-drag: switching would split it across two Parts
    const bool overCanvas = !ImGui::GetIO().WantCaptureMouse && canvasRect().contains(mouseX_, mouseY_);
    const int target = overCanvas ? pickPart(mouseX_, mouseY_) : -1;
    if (target < 0) {
        status_ = tr("No Part under the cursor");
        return;
    }
    if (target != doc_.activePart) {
        runCommand("part_select", {{"index", double(target)}});
        if (!lastCommandOk_) return;
    }
    char line[256];
    std::snprintf(line, sizeof line, tr("Sculpting: %s"), doc_.activePartInfo().name.c_str());
    status_ = line;
}

App::JobPart::JobPart(App& a, int index) : app(a) {
    if (index < 0 || index == app.doc_.activePart) return;
    back = app.doc_.activePartInfo().id;
    switched = true;
    app.doc_.selectPart(index);
}

App::JobPart::~JobPart() {
    if (!switched) return;
    const std::string done = app.doc_.activePartInfo().name;
    const int i = app.doc_.partIndex(back);
    if (i < 0 || i == app.doc_.activePart) return;
    app.doc_.selectPart(i);
    app.status_ += " (on " + done + "; " + app.doc_.activePartInfo().name + " stays the active Part)";
}

bool App::jobWaitsForStroke() const { return drag_ == Drag::Stroke || doc_.sculptor.strokeActive() || !dabQueue_.empty(); }

void App::updatePartThumbnails() {
    // #43: what each Part looks like now; the schedule picks at most one to render this frame, from the GPU
    // buffers syncScene just brought up to date (never a CPU render, never a re-upload: cheap on 30M+ meshes).
    std::vector<uint32_t> ids;
    ids.reserve(doc_.parts.size());
    const double now = double(SDL_GetTicksNS()) * 1e-9; // (the main loop's clock, as redrawAt_)
    for (int i = 0; i < doc_.partCount(); ++i) {
        const Part& s = doc_.parts[size_t(i)];
        const Mesh& m = doc_.partMesh(i);
        const UndoStack& undo = i == doc_.activePart ? doc_.undo : s.undo;
        uint64_t key = undo.revision();
        for (uint64_t part : {uint64_t(m.triangleCount()), uint64_t(m.vertexCount()), uint64_t(s.visible),
                              uint64_t(s.shading), uint64_t(renderSettings_.litsphere + 1), uint64_t(renderSettings_.showVertexPaint)})
            key = key * 1099511628211ull ^ part;
        partThumbSchedule_.observe(s.id, key, now);
        ids.push_back(s.id);
    }
    for (uint32_t gone : partThumbSchedule_.keepOnly(ids))
        if (auto it = partThumbs_.find(gone); it != partThumbs_.end()) {
            gl::DeleteTextures(1, &it->second);
            partThumbs_.erase(it);
        }
    const bool busy = drag_ != Drag::None || strokeFinishing_ || solverBusy() || doc_.sculptor.strokeActive();
    uint32_t id = 0;
    double wakeAt = 0.0;
    if (partThumbSchedule_.due(now, busy, id, wakeAt)) {
        constexpr int kPixels = 96; // shown at about 34 px: crisp on HiDPI
        std::vector<uint8_t> rgba;
        std::string error;
        if (renderer_.renderPartThumbnail(id, 30.0f, -15.0f, kPixels, renderSettings_, rgba, error)) {
            unsigned& tex = partThumbs_[id];
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
        partThumbSchedule_.rendered(id); // (one that couldn't render, e.g. hidden before its first upload, waits for a change)
        uint32_t next = 0;
        if (partThumbSchedule_.due(now, busy, next, wakeAt)) wakeAt = now; // another waiting: the next frame
    }
    if (wakeAt > 0.0) redrawAt_ = redrawAt_ > 0.0 ? std::min(redrawAt_, wakeAt) : wakeAt;
}

void App::clearPartThumbnails() {
    for (auto& [id, tex] : partThumbs_) gl::DeleteTextures(1, &tex);
    partThumbs_.clear();
    partThumbSchedule_.keepOnly({});
}

void App::drawPartsContents() {
    partPanelShown_ = true; // its thumbnails keep up while it shows (updatePartThumbnails)
    const float scale = uiScale_;
    const int count = doc_.partCount();
    const int active = doc_.activePart;
    const ImGuiStyle& style = ImGui::GetStyle();

    // The list: eye, name, triangles. Row actions run after the loop (they change the list).
    int selectIndex = -1, toggleIndex = -1, deleteIndex = -1, duplicateIndex = -1, renameIndex = -1;
    std::string renameTo;
    // Folders (#96): pending actions on one, by name; a Part put into a folder ("" = out of its folder).
    PartFolderActions fa;
    int newFolderPart = -1;
    // What the list shows, top to bottom: each folder's header where its first Part is, then (open) its Parts;
    // empty folders last.
    struct Row {
        int part = -1, folder = -1; // folder: the header (part < 0) or the folder this Part is in
    };
    std::vector<Row> rows;
    {
        std::vector<char> listed(doc_.partFolders.size(), 0);
        for (int i = 0; i < count; ++i) {
            const int f = doc_.partFolderIndex(doc_.parts[size_t(i)].folder);
            if (f < 0) {
                rows.push_back({i, -1});
                continue;
            }
            if (listed[size_t(f)]) continue;
            listed[size_t(f)] = 1;
            rows.push_back({-1, f});
            if (doc_.partFolders[size_t(f)].collapsed) continue;
            for (int k = i; k < count; ++k)
                if (doc_.parts[size_t(k)].folder == doc_.partFolders[size_t(f)].id) rows.push_back({k, f});
        }
        for (size_t f = 0; f < listed.size(); ++f)
            if (!listed[f]) rows.push_back({-1, int(f)});
    }
    const ImVec2 cellPad(6.0f * scale, 4.0f * scale);
    // Each row starts with the Part's thumbnail (#43); the text is centred on its height.
    const float thumb = 30.0f * scale, lineH = ImGui::GetTextLineHeight();
    const float innerH = std::max(lineH, thumb), textDy = (innerH - lineH) * 0.5f;
    const float rowH = innerH + 2.0f * cellPad.y;
    const float folderH = lineH + 2.0f * cellPad.y + 2.0f * scale;
    const float indent = 14.0f * scale; // a folder's Parts, under its header
    float listH = 2.0f;
    for (size_t r = 0; r < rows.size() && r < size_t(kListRows); ++r) listH += rows[r].part < 0 ? folderH : rowH;
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
    if (ImGui::BeginTable("##parts", 4, ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_PadOuterX, ImVec2(-1, listH))) {
        const float eyeW = ImGui::GetTextLineHeight() * 1.2f;
        ImGui::TableSetupColumn("name", ImGuiTableColumnFlags_WidthStretch);
        // Subdivision levels lists them ("SDiv 3/5"); empty for meshes without levels.
        bool anyLevels = false;
        for (int i = 0; i < count && !anyLevels; ++i) anyLevels = subdivLevelCount(doc_.partMesh(i)) > 0;
        ImGui::TableSetupColumn("sdiv", ImGuiTableColumnFlags_WidthFixed, anyLevels ? ImGui::CalcTextSize("SDiv 8/8").x : 0.0f);
        ImGui::TableSetupColumn("tris", ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize("0.00M").x);
        ImGui::TableSetupColumn("eye", ImGuiTableColumnFlags_WidthFixed, eyeW);
        // A Part dropped on a row: into that folder, or (on a Part outside folders) out of its folder.
        for (const Row& row : rows) {
            if (row.part < 0) {
                drawPartFolderRow(row.folder, folderH, cellPad, fa);
                continue;
            }
            const int i = row.part;
            const Part& s = doc_.parts[size_t(i)];
            const bool isActive = i == active;
            ImGui::PushID(int(s.id));
            ImGui::TableNextRow(ImGuiTableRowFlags_None, rowH);
            ImGui::TableSetColumnIndex(0);
            if (row.folder >= 0) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + indent);
            const ImVec2 thumbAt = ImGui::GetCursorScreenPos();
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + thumb + 6.0f * scale);
            if (renamingPart_ == s.id) {
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
                    renamingPart_ = 0;
                } else if (ImGui::IsItemDeactivated()) {
                    renamingPart_ = 0; // Escape or clicked away without a change
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
                if (ImGui::BeginDragDropSource()) { // #96: drag a Part onto a folder (or onto a Part outside folders)
                    ImGui::SetDragDropPayload("SF_PART", &s.id, sizeof s.id);
                    ImGui::Text(tr("Move %s"), s.name.c_str());
                    ImGui::EndDragDropSource();
                }
                {
                    const int f = doc_.partFolderIndex(s.folder);
                    partDropTarget(f >= 0 ? doc_.partFolders[size_t(f)].name : std::string(), fa);
                }
                if (isActive) {
                    const ImVec2 r0 = ImGui::GetItemRectMin(), r1 = ImGui::GetItemRectMax();
                    const float x = ImGui::TableGetCellBgRect(ImGui::GetCurrentTable(), 0).Min.x;
                    ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(x, r0.y), ImVec2(x + 2.0f * scale, r1.y),
                                                              theme::u32(theme::accent));
                }
                if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                    renamingPart_ = s.id;
                    focusRename_ = true;
                    std::snprintf(renameBuffer_, sizeof renameBuffer_, "%s", s.name.c_str());
                }
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
                    ImGui::SetTooltip(tr("%s\n%zu vertices, %zu triangles%s%s\nClick to select, double-click to rename"),
                                      s.name.c_str(), doc_.partMesh(i).vertexCount(), doc_.partMesh(i).triangleCount(),
                                      subdivLevelCount(doc_.partMesh(i)) ? ", " : "",
                                      subdivLevelText(doc_.partMesh(i)).c_str());
                if (ImGui::BeginPopupContextItem("##row")) {
                    ImGui::TextDisabled("%s", s.name.c_str());
                    if (ImGui::MenuItem(tr("Rename"))) {
                        renamingPart_ = s.id;
                        focusRename_ = true;
                        std::snprintf(renameBuffer_, sizeof renameBuffer_, "%s", s.name.c_str());
                    }
                    if (ImGui::MenuItem(tr("Duplicate"), actions_.shortcutText("part.duplicate").c_str())) duplicateIndex = i;
                    if (ImGui::BeginMenu(tr("Folder"))) {
                        if (ImGui::MenuItem(tr("New folder"))) newFolderPart = i;
                        if (!doc_.partFolders.empty()) ImGui::Separator();
                        for (const PartFolder& f : doc_.partFolders)
                            if (ImGui::MenuItem((f.name + "##to folder").c_str(), nullptr, s.folder == f.id) && s.folder != f.id) {
                                fa.part = i;
                                fa.partTo = f.name;
                            }
                        if (s.folder) {
                            ImGui::Separator();
                            if (ImGui::MenuItem(tr("Out of the folder"))) {
                                fa.part = i;
                                fa.partTo.clear();
                            }
                        }
                        ImGui::EndMenu();
                    }
                    if (ImGui::MenuItem(tr("Delete..."), nullptr, false, count > 1)) deleteIndex = i;
                    ImGui::EndPopup();
                }
                if (isActive && listedActivePart_ != s.id) {
                    ImGui::SetScrollHereY(0.5f); // keep the active Part in view after Alt+click
                    listedActivePart_ = s.id;
                }
            }
            {
                // The thumbnail, or a quiet placeholder until it's rendered (updatePartThumbnails).
                ImDrawList* dl = ImGui::GetWindowDrawList();
                const ImVec2 t1(thumbAt.x + thumb, thumbAt.y + thumb);
                dl->AddRectFilled(thumbAt, t1, ImGui::ColorConvertFloat4ToU32(kThumbWell), theme::rounding(3.0f * scale));
                const auto tex = partThumbs_.find(s.id);
                if (tex != partThumbs_.end() && tex->second)
                    dl->AddImageRounded(ImTextureID(uintptr_t(tex->second)), thumbAt, t1, ImVec2(0, 0), ImVec2(1, 1),
                                        IM_COL32(255, 255, 255, s.visible ? 255 : 110), theme::rounding(3.0f * scale));
                else
                    dl->AddCircle(ImVec2(thumbAt.x + thumb * 0.5f, thumbAt.y + thumb * 0.5f), thumb * 0.28f,
                                  ImGui::GetColorU32(style.Colors[ImGuiCol_TextDisabled]), 0, 1.0f * scale);
            }
            ImGui::TableSetColumnIndex(1);
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + textDy);
            if (const std::string sdiv = subdivLevelText(doc_.partMesh(i)); !sdiv.empty()) {
                ImVec4 col = style.Colors[isActive ? ImGuiCol_Text : ImGuiCol_TextDisabled];
                if (isActive) col.w *= 0.75f;
                ImGui::TextColored(col, "%s", sdiv.c_str());
            }
            ImGui::TableSetColumnIndex(2);
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + textDy);
            const std::string tris = shortCount(doc_.partMesh(i).triangleCount());
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
            if (eyeHovered) ImGui::SetTooltip(tr(s.visible ? "Hide %s" : "Show %s"), s.name.c_str());
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

    if (renameIndex >= 0 && !renameTo.empty() && renameTo != doc_.parts[size_t(renameIndex)].name)
        runCommand("part_rename", {{"index", double(renameIndex)}, {"to", renameTo}});
    if (selectIndex >= 0) {
        runCommand("part_select", {{"index", double(selectIndex)}});
        partSelected("Parts list");
    }
    if (toggleIndex >= 0) runCommand("part_visibility", {{"index", double(toggleIndex)}});
    if (duplicateIndex >= 0) runCommand("part_duplicate", {{"index", double(duplicateIndex)}});
    if (newFolderPart >= 0) {
        runCommand("part_folder_new", {{"index", double(newFolderPart)}});
        startFolderRename();
    }
    if (fa.part >= 0 && fa.partTo != partFolderName(doc_.parts[size_t(fa.part)].folder))
        runCommand("part_set_folder", {{"index", double(fa.part)}, {"folder", fa.partTo}});
    if (!fa.toggle.empty()) runCommand("part_folder_visibility", {{"folder", fa.toggle}});
    if (!fa.collapse.empty()) runCommand("part_folder_collapse", {{"folder", fa.collapse}});
    if (!fa.rename.empty() && !fa.renameTo.empty() && fa.renameTo != fa.rename)
        runCommand("part_folder_rename", {{"folder", fa.rename}, {"to", fa.renameTo}});
    if (!fa.remove.empty()) runCommand("part_folder_delete", {{"folder", fa.remove}});
    if (!fa.gizmo.empty()) {
        // Move the whole folder: one of its Parts active, the gizmo's Folder option on, Move.
        const int f = doc_.findPartFolder(fa.gizmo);
        if (f >= 0 && doc_.activePartInfo().folder != doc_.partFolders[size_t(f)].id)
            for (int i = 0; i < doc_.partCount(); ++i)
                if (doc_.parts[size_t(i)].folder == doc_.partFolders[size_t(f)].id) {
                    runCommand("part_select", {{"index", double(i)}});
                    break;
                }
        gizmo_.folder = true;
        setGizmoMode(GizmoMode::Move);
        status_ = "Move " + fa.gizmo + ": drag the gizmo (its visible Parts move together; W again: back to Draw)";
    }

    // Buttons act on the active Part.
    const std::string activeName = doc_.activePartInfo().name;
    int visibleCount = 0;
    for (const auto& s : doc_.parts) visibleCount += s.visible ? 1 : 0;
    auto confirm = [this](std::string title, std::string message, std::string button, std::string command) {
        partConfirm_ = {std::move(title), std::move(message), std::move(button), std::move(command)};
        openPartConfirm_ = true;
    };
    if (deleteIndex >= 0) {
        if (deleteIndex != doc_.activePart) runCommand("part_select", {{"index", double(deleteIndex)}});
        confirm("Delete Part?", "Delete \"" + doc_.activePartInfo().name + "\"? This can't be undone.", "Delete",
                "part_delete");
    }
    const float third = (ImGui::GetContentRegionAvail().x - 2 * style.ItemSpacing.x) / 3.0f;
    if (ImGui::Button(tr("Append"), ImVec2(third, 0))) ImGui::OpenPopup("##append");
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("%s", tr("Add a new Part beside the model"));
    if (ImGui::BeginPopup("##append")) {
        if (ImGui::MenuItem(tr("Sphere"))) runCommand("part_append", {{"type", std::string("sphere")}});
        if (ImGui::MenuItem(tr("Cube"))) runCommand("part_append", {{"type", std::string("cube")}});
        if (ImGui::MenuItem(tr("Plane"))) runCommand("part_append", {{"type", std::string("plane")}});
        for (const char* t : {"cylinder", "cone", "torus", "capsule", "icosphere", "quadsphere"}) { // #31
            std::string label = t;
            label[0] = char(std::toupper(static_cast<unsigned char>(label[0])));
            if (label == "Quadsphere") label = "Quad Sphere";
            if (ImGui::MenuItem(label.c_str())) runCommand("part_append", {{"type", std::string(t)}});
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
            // Your own mesh (#34): a copy of another Part, or a model file you choose.
            if (ImGui::BeginMenu(tr("Active Part's copy..."), count > 1)) {
                for (int i = 0; i < count; ++i) {
                    if (i == active) continue;
                    const std::string& name = doc_.parts[size_t(i)].name;
                    const bool chosen = insertType_ == "mesh" && insertMeshKey_ == "part" && insertMeshValue_ == name;
                    if (ImGui::MenuItem((name + "##insertcopy" + std::to_string(i)).c_str(), nullptr, chosen)) {
                        insertType_ = "mesh";
                        insertMeshKey_ = "part";
                        insertMeshValue_ = name;
                        status_ = "Insert a copy of " + name + ": drag on the model (the drag sets its size). Esc stops.";
                    }
                }
                ImGui::EndMenu();
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_DelayNormal))
                ImGui::SetTooltip(tr(count > 1 ? "Insert a copy of another Part into the active one"
                                            : "Append another Part first"));
            if (ImGui::MenuItem(tr("From file...##insert")))
                SDL_ShowOpenFileDialog(onAppendChosen, &dialogRequests_[int(FileAction::InsertMesh)], window_,
                                       kInsertFilters, 2, nullptr, false);
            // Insert sets (kitbash sets): a file with several objects, or a folder of model files; a picker of
            // their thumbnails, [ and ] step through them.
            ImGui::Separator();
            if (ImGui::MenuItem(tr("Insert set from file...")))
                SDL_ShowOpenFileDialog(onAppendChosen, &dialogRequests_[int(FileAction::InsertSet)], window_,
                                       kInsertFilters, 2, nullptr, false);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
                ImGui::SetTooltip("%s", tr("A model file with several objects: insert them one at a time"));
            if (ImGui::MenuItem(tr("Insert set from folder...")))
                SDL_ShowOpenFolderDialog(onAppendChosen, &dialogRequests_[int(FileAction::InsertSet)], window_, nullptr, false);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
                ImGui::SetTooltip("%s", tr("A folder of .obj, .ply or .glb files: insert them one at a time"));
            // The built-in sets (core/Parts.h): no file to pick.
            for (const BuiltinInsertSet& b : builtinInsertSets()) {
                const std::string path = std::string(kBuiltinInsertSetPrefix) + b.id;
                if (ImGui::MenuItem(tr(b.name), nullptr, insertSetActive() && insertSet_.path == path)) chooseInsertSet(path);
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
                    ImGui::SetTooltip("%s", tr("Original creature bones to build on: skull, jaw, teeth, horns, spine, "
                                               "ribs, limb and wing bones, claws and tail"));
            }
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem(tr("From file...")))
            SDL_ShowOpenFileDialog(onAppendChosen, &dialogRequests_[int(FileAction::AppendPart)], window_,
                                   kAppendFilters, 2, nullptr, false);
        ImGui::EndPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button(tr("Duplicate"), ImVec2(third, 0))) runCommand("part_duplicate");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip(tr("Copy the active Part (%s)"), actions_.shortcutText("part.duplicate").c_str());
    ImGui::SameLine();
    ImGui::BeginDisabled(count < 2);
    if (ImGui::Button(tr("Delete..."), ImVec2(third, 0)))
        confirm("Delete Part?", "Delete \"" + activeName + "\"? This can't be undone.", "Delete", "part_delete");
    ImGui::EndDisabled();

    const float arrowW = ImGui::GetFrameHeight();
    ImGui::BeginDisabled(active <= 0);
    if (ImGui::ArrowButton("##up", ImGuiDir_Up)) runCommand("part_move", {{"direction", std::string("up")}});
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_DelayNormal))
        ImGui::SetTooltip("%s", tr("Move up the list"));
    ImGui::SameLine();
    ImGui::BeginDisabled(active >= count - 1);
    if (ImGui::ArrowButton("##down", ImGuiDir_Down)) runCommand("part_move", {{"direction", std::string("down")}});
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_DelayNormal))
        ImGui::SetTooltip("%s", tr("Move down the list"));
    ImGui::SameLine();
    if (ImGui::Button(ICON_LC_FOLDER_PLUS "###part_folder_new", ImVec2(arrowW, 0))) {
        runCommand("part_folder_new");
        startFolderRename();
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
        ImGui::SetTooltip("%s", tr("New folder with the active Part (drag Parts onto a folder to add them)"));
    ImGui::SameLine();
    // Solo and X-Ray side by side (#80), then Unhide all.
    // Solo and X-Ray as wide as their words (never clipped, at any UI scale); Unhide all takes the rest.
    const float pad = 2.0f * style.FramePadding.x + 4.0f * uiScale_;
    const float soloW = std::ceil(ImGui::CalcTextSize(tr("Solo")).x + pad), xrayW = std::ceil(ImGui::CalcTextSize(tr("X-Ray")).x + pad);
    const bool solo = count > 1 && visibleCount == 1 && doc_.activePartInfo().visible;
    ImGui::BeginDisabled(count < 2);
    if (solo) ImGui::PushStyleColor(ImGuiCol_Button, style.Colors[ImGuiCol_ButtonActive]);
    if (ImGui::Button(tr("Solo"), ImVec2(soloW, 0))) runCommand("part_solo");
    if (solo) ImGui::PopStyleColor();
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
        ImGui::SetTooltip(tr(solo ? "Show the other Parts again" : "Show only the active Part"));
    ImGui::SameLine();
    const bool xray = renderSettings_.xray;
    if (xray) ImGui::PushStyleColor(ImGuiCol_Button, style.Colors[ImGuiCol_ButtonActive]);
    if (ImGui::Button(tr("X-Ray"), ImVec2(xrayW, 0))) runCommand("xray");
    if (xray) ImGui::PopStyleColor();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
        ImGui::SetTooltip("%s", xray ? tr("Draw the other Parts solid again") : tr("See through the other Parts to the active one"));
    ImGui::SameLine();
    ImGui::BeginDisabled(visibleCount == count);
    if (ImGui::Button(tr("Unhide all"), ImVec2(-1, 0))) runCommand("part_show_all");
    ImGui::EndDisabled();

    const float half = (ImGui::GetContentRegionAvail().x - style.ItemSpacing.x) * 0.5f;
    ImGui::BeginDisabled(active >= count - 1);
    if (ImGui::Button(tr("Merge down..."), ImVec2(half, 0)))
        confirm("Merge Down?",
                "Merge \"" + doc_.parts[size_t(std::min(active + 1, count - 1))].name + "\" into \"" + activeName +
                    "\"? This can't be undone,\nbut Split groups separates them again.",
                "Merge", "part_merge_down");
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_DelayNormal))
        ImGui::SetTooltip("%s", tr("Merge the active Part with the one below it"));
    ImGui::SameLine();
    ImGui::BeginDisabled(visibleCount < 2);
    if (ImGui::Button(tr("Merge visible..."), ImVec2(half, 0)))
        confirm("Merge Visible?",
                "Merge the " + std::to_string(visibleCount) + " visible Parts into one? This can't be undone,\n"
                "but Split groups separates them again.",
                "Merge", "part_merge_visible");
    ImGui::EndDisabled();

    // More than one face group? Counted only when the active mesh changes.
    const uint64_t key = (uint64_t(doc_.activePartInfo().id) << 40) ^ (doc_.undo.revision() * 0x9E3779B97F4A7C15ull) ^
                         uint64_t(doc_.mesh.triangleCount());
    if (key != splitGroupsKey_) {
        splitGroupsKey_ = key;
        const auto& g = doc_.mesh.facegroups;
        splitGroupsPossible_ = g.size() == doc_.mesh.triangleCount() &&
                               std::any_of(g.begin(), g.end(), [&](uint32_t id) { return id != g.front(); });
    }
    ImGui::BeginDisabled(!splitGroupsPossible_);
    if (ImGui::Button(tr("Split groups"), ImVec2(half, 0))) runCommand("part_split_groups");
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_DelayNormal))
        ImGui::SetTooltip("%s", tr("One Part per face group"));
    ImGui::SameLine();
    ImGui::BeginDisabled(doc_.mesh.triangleCount() == 0);
    if (ImGui::Button(tr("Split parts"), ImVec2(half, 0))) runCommand("part_split_parts");
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_DelayNormal))
        ImGui::SetTooltip("%s", tr("One Part per disconnected piece"));
    if (fontSmall_) ImGui::PushFont(fontSmall_);
    ImGui::TextDisabled("%s", tr("Alt+click a Part to select it"));
    if (fontSmall_) ImGui::PopFont();
}

std::string App::partFolderName(uint32_t id) const {
    const int f = doc_.partFolderIndex(id);
    return f >= 0 ? doc_.partFolders[size_t(f)].name : std::string();
}

void App::startFolderRename() {
    if (!lastCommandOk_ || doc_.partFolders.empty()) return;
    const PartFolder& f = doc_.partFolders.back(); // (the one just made)
    renamingFolder_ = f.id;
    focusRename_ = true;
    std::snprintf(renameBuffer_, sizeof renameBuffer_, "%s", f.name.c_str());
}

// A folder's header row (#96): chevron, folder, name, its triangles and an eye for all its Parts. Click folds or
// opens it, double-click renames, right-click for the rest; actions come back by the folder's name.
void App::partDropTarget(const std::string& folder, PartFolderActions& fa) {
    if (!ImGui::BeginDragDropTarget()) return;
    if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("SF_PART"); p && p->DataSize == sizeof(uint32_t)) {
        uint32_t id = 0;
        std::memcpy(&id, p->Data, sizeof id);
        fa.part = doc_.partIndex(id);
        fa.partTo = folder;
    }
    ImGui::EndDragDropTarget();
}

void App::drawPartFolderRow(int fi, float rowH, ImVec2 cellPad, PartFolderActions& fa) {
    const PartFolder& f = doc_.partFolders[size_t(fi)];
    const ImGuiStyle& style = ImGui::GetStyle();
    const float scale = uiScale_;
    int members = 0, shown = 0;
    bool hasActive = false;
    size_t tris = 0;
    for (int i = 0; i < doc_.partCount(); ++i) {
        if (doc_.parts[size_t(i)].folder != f.id) continue;
        ++members;
        shown += doc_.parts[size_t(i)].visible ? 1 : 0;
        hasActive |= i == doc_.activePart;
        tris += doc_.partMesh(i).triangleCount();
    }
    const bool visible = shown > 0;
    const float textDy = (rowH - 2.0f * cellPad.y - ImGui::GetTextLineHeight()) * 0.5f;
    ImGui::PushID(int(f.id) ^ 0x46000000);
    ImGui::TableNextRow(ImGuiTableRowFlags_None, rowH);
    ImGui::TableSetColumnIndex(0);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + textDy);
    const ImVec2 at = ImGui::GetCursorScreenPos();
    const float iconW = ImGui::CalcTextSize(ICON_LC_FOLDER).x;
    const float textX = iconW * 2.0f + 8.0f * scale; // chevron, folder, then the name
    if (renamingFolder_ == f.id) {
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + textX);
        ImGui::SetNextItemWidth(-1);
        if (focusRename_) {
            ImGui::SetKeyboardFocusHere();
            focusRename_ = false;
        }
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(style.FramePadding.x, 0.0f));
        const bool enter = ImGui::InputText("##rename folder", renameBuffer_, sizeof renameBuffer_,
                                            ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
        ImGui::PopStyleVar();
        if (enter || (ImGui::IsItemDeactivatedAfterEdit() && !ImGui::IsKeyPressed(ImGuiKey_Escape))) {
            fa.rename = f.name;
            fa.renameTo = renameBuffer_;
            renamingFolder_ = 0;
        } else if (ImGui::IsItemDeactivated()) {
            renamingFolder_ = 0;
        }
    } else {
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + textX);
        if (!visible) ImGui::PushStyleColor(ImGuiCol_Text, style.Colors[ImGuiCol_TextDisabled]);
        if (ImGui::Selectable((f.name + "##folder").c_str(), false,
                              ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap |
                                  ImGuiSelectableFlags_AllowDoubleClick | ImGuiSelectableFlags_NoAutoClosePopups)) {
            fa.collapse = f.name; // (a double-click's second click folds it back)
            if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) { // the header's own (not its eye's)
                renamingFolder_ = f.id;
                focusRename_ = true;
                std::snprintf(renameBuffer_, sizeof renameBuffer_, "%s", f.name.c_str());
            }
        }
        if (!visible) ImGui::PopStyleColor();
        partDropTarget(f.name, fa);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
            ImGui::SetTooltip("%s (%d)\n%s", f.name.c_str(), members,
                              f.collapsed ? tr("Click to open, double-click to rename, drag Parts onto it")
                                          : tr("Click to fold, double-click to rename, drag Parts onto it"));
        if (ImGui::BeginPopupContextItem("##folder row")) {
            ImGui::TextDisabled("%s", f.name.c_str());
            if (ImGui::MenuItem(tr("Rename"))) {
                renamingFolder_ = f.id;
                focusRename_ = true;
                std::snprintf(renameBuffer_, sizeof renameBuffer_, "%s", f.name.c_str());
            }
            if (ImGui::MenuItem(visible ? tr("Hide its Parts") : tr("Show its Parts"), nullptr, false, members > 0))
                fa.toggle = f.name;
            if (ImGui::MenuItem(tr("Move with the gizmo"), nullptr, false, shown > 0)) fa.gizmo = f.name;
            ImGui::Separator();
            if (ImGui::MenuItem(tr("Remove folder (keep its Parts)"))) fa.remove = f.name;
            ImGui::EndPopup();
        }
        if (hasActive && f.collapsed) { // the active Part is folded away in here: the accent bar says so
            const ImVec2 r0 = ImGui::GetItemRectMin(), r1 = ImGui::GetItemRectMax();
            const float x = ImGui::TableGetCellBgRect(ImGui::GetCurrentTable(), 0).Min.x;
            ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(x, r0.y), ImVec2(x + 2.0f * scale, r1.y), theme::u32(theme::accent));
        }
    }
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImU32 dim = ImGui::GetColorU32(style.Colors[ImGuiCol_TextDisabled]);
        const ImU32 text = ImGui::GetColorU32(style.Colors[visible ? ImGuiCol_Text : ImGuiCol_TextDisabled]);
        dl->AddText(at, dim, f.collapsed ? ICON_LC_CHEVRON_RIGHT : ICON_LC_CHEVRON_DOWN);
        dl->AddText(ImVec2(at.x + iconW + 4.0f * scale, at.y), text, f.collapsed ? ICON_LC_FOLDER : ICON_LC_FOLDER_OPEN);
    }
    ImGui::TableSetColumnIndex(2);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + textDy);
    if (members) {
        const std::string t = shortCount(tris);
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(t.c_str()).x);
        ImGui::TextDisabled("%s", t.c_str());
    }
    ImGui::TableSetColumnIndex(3);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + textDy);
    const ImVec2 eyePos = ImGui::GetCursorScreenPos();
    const ImVec2 eyeSize(ImGui::GetContentRegionAvail().x, ImGui::GetTextLineHeight());
    ImGui::BeginDisabled(members == 0);
    if (ImGui::InvisibleButton(("##eye folder " + f.name).c_str(), eyeSize)) fa.toggle = f.name;
    ImGui::EndDisabled();
    const bool eyeHovered = ImGui::IsItemHovered();
    if (eyeHovered) ImGui::SetTooltip(tr(visible ? "Hide the Parts in %s" : "Show the Parts in %s"), f.name.c_str());
    const ImU32 eyeCol = ImGui::GetColorU32(eyeHovered ? style.Colors[ImGuiCol_ButtonHovered]
                                            : visible  ? style.Colors[ImGuiCol_Text]
                                                       : style.Colors[ImGuiCol_TextDisabled]);
    if (members)
        drawEye(ImGui::GetWindowDrawList(), ImVec2(eyePos.x + eyeSize.x * 0.5f, eyePos.y + eyeSize.y * 0.5f),
                ImGui::GetTextLineHeight() * 0.8f, visible, eyeCol);
    ImGui::PopID();
}

void App::chooseInsertSet(const std::string& path) {
    InsertSetState set;
    std::string error;
    if (!listInsertSet(path, set.items, error)) {
        status_ = error;
        return;
    }
    set.path = path;
    insertSet_ = std::move(set);
    insertType_ = "mesh";
    insertMeshKey_ = "set";
    insertMeshValue_ = path;
    selectInsertSetItem(0);
}

void App::selectInsertSetItem(int index) {
    const int n = int(insertSet_.items.size());
    if (!n) return;
    insertSet_.index = ((index % n) + n) % n;
    status_ = "Insert " + insertSet_.items[size_t(insertSet_.index)].name + " (" + std::to_string(insertSet_.index + 1) +
              " of " + std::to_string(n) + "): drag on the model; [ and ] step through the set. Esc stops.";
}

// The insert set's picker: a thumbnail per item, the one a drag inserts outlined; open while inserting from a set.
void App::drawInsertSetPicker() {
    if (!insertSetActive()) return;
    const float s = uiScale_, tile = 72.0f * s;
    const CanvasRect c = canvasRect();
    // Bottom left of the canvas: clear of the timeline strip and the view figure at the top.
    ImGui::SetNextWindowPos(ImVec2(c.x + 12.0f * s, c.y + c.h - 12.0f * s), ImGuiCond_Appearing, ImVec2(0.0f, 1.0f));
    ImGui::SetNextWindowSizeConstraints(ImVec2(tile * 2.5f, 0), ImVec2(tile * 5.6f, c.h * 0.7f));
    bool open = true;
    const std::string title = std::string(tr("Insert set")) + "###insertset";
    if (ImGui::Begin(title.c_str(), &open, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing)) {
        const auto set = std::filesystem::path(std::u8string(insertSet_.path.begin(), insertSet_.path.end())).lexically_normal();
        std::string name = (set.has_filename() ? set.filename() : set.parent_path().filename()).string();
        const BuiltinInsertSet* builtin = nullptr;
        for (const BuiltinInsertSet& b : builtinInsertSets())
            if (insertSet_.path == std::string(kBuiltinInsertSetPrefix) + b.id) builtin = &b;
        if (builtin) name = tr(builtin->name);
        ImGui::TextDisabled("%s  (%d)", name.c_str(), int(insertSet_.items.size()));
        const float spacing = ImGui::GetStyle().ItemSpacing.x;
        const int perRow = std::clamp(int(insertSet_.items.size()), 1, 5);
        for (size_t i = 0; i < insertSet_.items.size(); ++i) {
            const InsertSetItem& it = insertSet_.items[i];
            if (i % size_t(perRow)) ImGui::SameLine(0, spacing);
            const std::string id = "##insertset" + std::to_string(i);
            const std::string label = builtin ? tr(it.name) : it.name; // a built-in set's names are ours to translate
            if (AssetTile(id.c_str(), thumbs_.insertSetItem(it.file, it.object), ICON_LC_BOX, label, tile,
                          int(i) == insertSet_.index))
                selectInsertSetItem(int(i));
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("%s", label.c_str());
        }
        AssetTileHoverGlow();
        ImGui::TextDisabled("%s", tr("[ / ] previous / next   Esc stops"));
    }
    ImGui::End();
    if (!open) {
        insertType_.clear();
        status_ = "Insert off";
    }
}

void App::drawPartPopups() {
    drawInsertSetPicker();
    const std::string popup = partConfirm_.title + "###" + partConfirm_.command;
    if (openPartConfirm_) {
        ImGui::OpenPopup(popup.c_str());
        openPartConfirm_ = false;
    }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::BeginPopupModal(popup.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
    ImGui::TextUnformatted(partConfirm_.message.c_str());
    ImGui::Spacing();
    const float w = 110.0f * uiScale_;
    if (ImGui::Button(partConfirm_.button.c_str(), ImVec2(w, 0)) || ImGui::IsKeyPressed(ImGuiKey_Enter)) {
        ImGui::CloseCurrentPopup();
        runCommand(partConfirm_.command);
    }
    ImGui::SameLine();
    if (ImGui::Button(tr("Cancel"), ImVec2(w, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

} // namespace sf
