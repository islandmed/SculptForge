// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Paint Layers sub-palette (Tool palette, next to the sculpt Layers): a
// Photoshop-style stack of polypaint layers for the active SubTool. Rows list
// the layers top first with Base at the bottom; click selects the layer the
// Paint brush writes into, double-click renames, the eye and lock toggle.
// Below: blend mode and opacity of the active layer, then New / Duplicate /
// Delete, Up / Down / Merge down / Flatten and Clear. Every change goes through
// the paint_layer_* commands (core/PaintLayer.cpp), so it is undoable and
// scriptable; the opacity slider previews live and commits on release. When
// the SubTool paints into its texture (Tool > Texture Map) the same palette
// shows and edits the texture's layers (the commands hand over to it).
#include "app/App.h"
#include "app/Ui.h"
#include "app/I18n.h"

#include "core/PaintLayer.h"
#include "core/Polypaint.h"
#include "core/Texture.h"

#include <IconsLucide.h>

#include <algorithm>
#include <cstdio>
#include <string>

namespace sf {

namespace {

constexpr int kPaintListRows = 7; // the list scrolls beyond this many layers
constexpr const char* kPath = "Tool > Paint Layers";

const char* blendShort(BlendMode m) {
    switch (m) {
    case BlendMode::Normal: return "";
    case BlendMode::Multiply: return "Mult";
    case BlendMode::Screen: return "Scrn";
    case BlendMode::Overlay: return "Over";
    case BlendMode::SoftLight: return "Soft";
    case BlendMode::Add: return "Add";
    case BlendMode::Subtract: return "Sub";
    case BlendMode::Darken: return "Dark";
    case BlendMode::Lighten: return "Light";
    case BlendMode::Difference: return "Diff";
    }
    return "";
}

// A clickable icon cell (eye / lock); returns true when clicked.
bool iconToggle(const char* id, const char* icon, bool on, const char* tip) {
    const ImGuiStyle& style = ImGui::GetStyle();
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const ImVec2 size(ImGui::GetContentRegionAvail().x, ImGui::GetTextLineHeight());
    const bool clicked = ImGui::InvisibleButton(id, size);
    const bool hovered = ImGui::IsItemHovered();
    if (hovered && !ui::CtrlHeld()) ImGui::SetTooltip("%s", tip);
    const ImU32 col = ImGui::GetColorU32(hovered ? style.Colors[ImGuiCol_ButtonHovered]
                                         : on    ? style.Colors[ImGuiCol_Text]
                                                 : style.Colors[ImGuiCol_TextDisabled]);
    const ImVec2 ts = ImGui::CalcTextSize(icon);
    ImGui::GetWindowDrawList()->AddText(ImVec2(pos.x + (size.x - ts.x) * 0.5f, pos.y + (size.y - ts.y) * 0.5f), col, icon);
    return clicked;
}

} // namespace

namespace {

// One row of the palette: a polypaint layer or a texture layer.
struct LayerRow {
    std::string name;
    BlendMode blend = BlendMode::Normal;
    float opacity = 1.0f;
    bool visible = true, locked = false;
};

// The stack the palette shows: the texture's layers when painting goes into the texture, else polypaint's.
std::vector<LayerRow> layerRows(const Mesh& m, bool texture, int& active) {
    std::vector<LayerRow> rows;
    if (texture) {
        for (const TextureLayer& l : m.texture->layers) rows.push_back({l.name, l.blend, l.opacity, l.visible, l.locked});
        active = m.texture->activeLayer;
    } else {
        for (const PaintLayer& l : m.paintLayers) rows.push_back({l.name, l.blend, l.opacity, l.visible, l.locked});
        active = m.activePaintLayer;
    }
    active = rows.empty() ? -1 : std::clamp(active, -1, int(rows.size()) - 1);
    return rows;
}

} // namespace

void App::drawPaintLayersContents() {
    const Mesh& mesh = doc_.mesh;
    const ImGuiStyle& style = ImGui::GetStyle();
    const bool texture = paintsTexture(mesh);
    int active = -1;
    const std::vector<LayerRow> rows = layerRows(mesh, texture, active);
    const int count = int(rows.size());
    if (hasTexture(mesh)) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%s", tr("Paint into"));
        ImGui::SameLine();
        ui::Segmented(*this, {"draw.paint_vertex", "draw.paint_texture"});
    }
    auto indexArg = [](int i) { return Args{{"index", double(i)}}; };

    // Rows, top layer first, Base last. Actions run after the table (they change the list).
    int selectIndex = -2, toggleVisible = -2, toggleLock = -2, renameIndex = -2, duplicateIndex = -2, deleteIndex = -2,
        mergeIndex = -2, clearIndex = -2;
    std::string renameTo;
    const float scale = uiScale_;
    const ImVec2 cellPad(6.0f * scale, 4.0f * scale);
    const float rowH = ImGui::GetTextLineHeight() + 2.0f * cellPad.y;
    // A texture with material channels shows a row of channel chips under each layer (PanelPbr.cpp).
    const bool chips = texture && hasMaterialChannels(*mesh.texture);
    Args chipArgs;
    const float listH = rowH * float(std::min(count * (chips ? 2 : 1) + 1, kPaintListRows + (chips ? 3 : 0))) + 2.0f;
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, cellPad);
    if (ImGui::BeginTable("##paintlayers", 4, ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter,
                          ImVec2(-1, listH))) {
        const float iconW = ImGui::GetTextLineHeight() * 1.2f;
        ImGui::TableSetupColumn("name", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("mode", ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize("Light 100%").x);
        ImGui::TableSetupColumn("lock", ImGuiTableColumnFlags_WidthFixed, iconW);
        ImGui::TableSetupColumn("eye", ImGuiTableColumnFlags_WidthFixed, iconW);
        for (int i = count - 1; i >= -1; --i) {
            const LayerRow* l = i >= 0 ? &rows[size_t(i)] : nullptr;
            const std::string name = l ? l->name : "Base";
            const bool isActive = i == active;
            const bool dim = l && !l->visible;
            ImGui::PushID(i);
            ImGui::TableNextRow(ImGuiTableRowFlags_None, rowH);
            ImGui::TableSetColumnIndex(0);
            if (renamingPaintLayer_ == i && l) {
                ImGui::SetNextItemWidth(-1);
                if (focusPaintRename_) {
                    ImGui::SetKeyboardFocusHere();
                    focusPaintRename_ = false;
                }
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(style.FramePadding.x, 0.0f));
                const bool enter = ImGui::InputText("##rename", paintRenameBuffer_, sizeof paintRenameBuffer_,
                                                    ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
                ImGui::PopStyleVar();
                if (enter || (ImGui::IsItemDeactivatedAfterEdit() && !ImGui::IsKeyPressed(ImGuiKey_Escape))) {
                    renameTo = paintRenameBuffer_;
                    renameIndex = i;
                    renamingPaintLayer_ = -2;
                } else if (ImGui::IsItemDeactivated()) {
                    renamingPaintLayer_ = -2;
                }
            } else {
                if (dim) ImGui::PushStyleColor(ImGuiCol_Text, style.Colors[ImGuiCol_TextDisabled]);
                if (ImGui::Selectable(name.c_str(), isActive,
                                      ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap |
                                          ImGuiSelectableFlags_AllowDoubleClick | ImGuiSelectableFlags_NoAutoClosePopups) &&
                    !isActive)
                    selectIndex = i;
                if (dim) ImGui::PopStyleColor();
                ui::HelpText(name.c_str(), nullptr,
                             l ? "A paint layer. Click to paint on it, double-click to rename, right-click for more. "
                                 "Layers composite bottom to top with their blend mode and opacity."
                             : texture ? "The base colour layer of the Part's texture. Always opaque; layers above it "
                                         "blend over it."
                               : "The base colour layer: the Part's vertex paint. Always opaque; layers above it blend "
                                 "over it. With only Base, vertex paint works exactly as before.",
                             kPath);
                if (l && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                    renamingPaintLayer_ = i;
                    focusPaintRename_ = true;
                    std::snprintf(paintRenameBuffer_, sizeof paintRenameBuffer_, "%s", name.c_str());
                }
                if (ImGui::BeginPopupContextItem("##row")) {
                    ImGui::TextDisabled("%s", name.c_str());
                    if (ImGui::MenuItem(tr("Rename"), nullptr, false, l != nullptr)) {
                        renamingPaintLayer_ = i;
                        focusPaintRename_ = true;
                        std::snprintf(paintRenameBuffer_, sizeof paintRenameBuffer_, "%s", name.c_str());
                    }
                    if (ImGui::MenuItem(tr("Duplicate"))) duplicateIndex = i;
                    if (ImGui::MenuItem(tr("Merge down"), nullptr, false, l != nullptr)) mergeIndex = i;
                    if (ImGui::MenuItem(tr("Clear"))) clearIndex = i;
                    if (ImGui::MenuItem(tr("Delete"), nullptr, false, l != nullptr)) deleteIndex = i;
                    ImGui::EndPopup();
                }
            }
            ImGui::TableSetColumnIndex(1);
            char mode[32] = "";
            if (l) std::snprintf(mode, sizeof mode, "%s %d%%", blendShort(l->blend), int(l->opacity * 100.0f + 0.5f));
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(mode).x);
            ImVec4 modeCol = style.Colors[isActive ? ImGuiCol_Text : ImGuiCol_TextDisabled];
            if (isActive) modeCol.w *= 0.75f;
            ImGui::TextColored(modeCol, "%s", mode);
            if (l) {
                ImGui::TableSetColumnIndex(2);
                if (iconToggle(("##lock " + name).c_str(), ICON_LC_LOCK, l->locked,
                               l->locked ? "Unlock (allow painting)" : "Lock against painting"))
                    toggleLock = i;
                ui::HelpText("Lock", nullptr, "Lock the layer: the Paint brush, fills, clears and merges leave it alone.", kPath);
                ImGui::TableSetColumnIndex(3);
                if (iconToggle(("##eye " + name).c_str(), l->visible ? ICON_LC_EYE : ICON_LC_EYE_OFF, l->visible,
                               l->visible ? "Hide layer" : "Show layer"))
                    toggleVisible = i;
                ui::HelpText("Visibility", nullptr, "Show or hide the layer. Hidden layers don't show or export.", kPath);
            }
            if (isActive && listedPaintLayer_ != i) {
                ImGui::SetScrollHereY(0.5f);
                listedPaintLayer_ = i;
            }
            ImGui::PopID();
            if (chips && l) {
                ImGui::TableNextRow(ImGuiTableRowFlags_None, rowH);
                ImGui::TableSetColumnIndex(0);
                ImGui::PushID(1000 + i);
                if (Args a = drawLayerChannelChips(i); !a.empty()) chipArgs = std::move(a);
                ImGui::PopID();
            }
        }
        ImGui::EndTable();
    }
    ImGui::PopStyleVar();
    if (!chipArgs.empty()) runCommand("texture_layer_channel", chipArgs);

    if (selectIndex >= -1) runCommand("paint_layer_select", indexArg(selectIndex));
    if (toggleVisible >= 0)
        runCommand("paint_layer_set", {{"index", double(toggleVisible)}, {"visible", !rows[size_t(toggleVisible)].visible}});
    if (toggleLock >= 0)
        runCommand("paint_layer_set", {{"index", double(toggleLock)}, {"locked", !rows[size_t(toggleLock)].locked}});
    if (renameIndex >= 0 && !renameTo.empty() && renameTo != rows[size_t(renameIndex)].name)
        runCommand("paint_layer_set", {{"index", double(renameIndex)}, {"name", renameTo}});
    if (duplicateIndex >= -1) runCommand("paint_layer_duplicate", indexArg(duplicateIndex));
    if (mergeIndex >= 0) runCommand("paint_layer_merge", indexArg(mergeIndex));
    if (clearIndex >= -1) runCommand("paint_layer_clear", indexArg(clearIndex));
    if (deleteIndex >= 0) runCommand("paint_layer_delete", indexArg(deleteIndex));

    // The active layer's blend mode and opacity (Base is always Normal, 100%). The commands above may have
    // changed the stack, so look again.
    int act = -1;
    const std::vector<LayerRow> now = layerRows(doc_.mesh, paintsTexture(doc_.mesh), act);
    const LayerRow* cur = act >= 0 ? &now[size_t(act)] : nullptr;
    const int stackSize = int(now.size());
    // Live opacity preview on the real layer, recomposited (the command records the change on release).
    auto setOpacity = [this, act](float value) {
        if (paintsTexture(doc_.mesh)) {
            PaintTexture& t = mutableTexture(doc_.mesh);
            t.layers[size_t(act)].opacity = value;
            compositeTextureAll(t);
        } else {
            doc_.mesh.paintLayers[size_t(act)].opacity = value;
            compositePaintAll(doc_.mesh);
        }
    };
    const float half = (ImGui::GetContentRegionAvail().x - style.ItemSpacing.x) * 0.5f;
    ImGui::BeginDisabled(!cur);
    ImGui::SetNextItemWidth(half);
    if (ImGui::BeginCombo("##blend", blendModeLabel(cur ? cur->blend : BlendMode::Normal))) {
        for (int m = 0; m < kBlendModeCount; ++m) {
            const BlendMode mode = BlendMode(m);
            if (ImGui::Selectable(blendModeLabel(mode), cur && cur->blend == mode) && cur && cur->blend != mode)
                runCommand("paint_layer_set", {{"index", double(act)}, {"blend", std::string(blendModeId(mode))}});
        }
        ImGui::EndCombo();
    }
    ui::HelpText("Blend mode", nullptr,
                 "How the active layer mixes with the layers below, with Photoshop's maths: Multiply darkens, Screen "
                 "lightens, Overlay adds contrast (50% grey changes nothing), Add brightens, ...",
                 kPath);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1);
    float opacity = cur ? cur->opacity * 100.0f : 100.0f;
    if (ui::SliderInside("##opacity", &opacity, 0.0f, 100.0f, "Opacity %.0f%%") && cur) {
        if (paintOpacityDrag_ < 0.0f) paintOpacityDrag_ = cur->opacity; // live preview; committed on release
        setOpacity(std::clamp(opacity / 100.0f, 0.0f, 1.0f));
    }
    if (ImGui::IsItemDeactivated() && paintOpacityDrag_ >= 0.0f && cur) {
        const float committed = std::clamp(opacity / 100.0f, 0.0f, 1.0f);
        setOpacity(paintOpacityDrag_); // so the command's undo step records the value before the drag
        paintOpacityDrag_ = -1.0f;
        runCommand("paint_layer_set", {{"index", double(act)}, {"opacity", double(committed)}});
    }
    ui::HelpText("Opacity", nullptr, "How strongly the active layer shows (0-100%). Painting keeps its own coverage.", kPath);
    ImGui::EndDisabled();

    const float third = (ImGui::GetContentRegionAvail().x - 2 * style.ItemSpacing.x) / 3.0f;
    ImGui::BeginDisabled(mesh.vertexCount() == 0);
    if (ImGui::Button(ICON_LC_PLUS " New", ImVec2(third, 0))) runCommand("paint_layer_add");
    ImGui::EndDisabled();
    ui::HelpText("New paint layer", nullptr, "Add a transparent paint layer above the active one and paint on it.", kPath);
    ImGui::SameLine();
    ImGui::BeginDisabled(mesh.vertexCount() == 0);
    if (ImGui::Button(tr("Duplicate##paint"), ImVec2(third, 0))) runCommand("paint_layer_duplicate");
    ImGui::EndDisabled();
    ui::HelpText("Duplicate", nullptr, "Copy the active layer (Base becomes an opaque layer) to a new layer above it.", kPath);
    ImGui::SameLine();
    ImGui::BeginDisabled(!cur);
    if (ImGui::Button(tr("Delete##paint"), ImVec2(third, 0))) runCommand("paint_layer_delete");
    ImGui::EndDisabled();
    ui::HelpText("Delete", nullptr, "Delete the active layer (undoable). Base can't be deleted.", kPath);

    const float arrow = ImGui::GetFrameHeight();
    const float rest = (ImGui::GetContentRegionAvail().x - 2 * arrow - 3 * style.ItemSpacing.x) * 0.5f;
    ImGui::BeginDisabled(!cur || act >= stackSize - 1);
    if (ImGui::ArrowButton("##paintup", ImGuiDir_Up)) runCommand("paint_layer_move", {{"direction", std::string("up")}});
    ImGui::EndDisabled();
    ui::HelpText("Move up", nullptr, "Move the active layer up the stack (it then covers the layer it passed).", kPath);
    ImGui::SameLine();
    ImGui::BeginDisabled(!cur || act <= 0);
    if (ImGui::ArrowButton("##paintdown", ImGuiDir_Down)) runCommand("paint_layer_move", {{"direction", std::string("down")}});
    ImGui::EndDisabled();
    ui::HelpText("Move down", nullptr, "Move the active layer down the stack (Base stays at the bottom).", kPath);
    ImGui::SameLine();
    ImGui::BeginDisabled(!cur);
    if (ImGui::Button(tr("Merge down##paint"), ImVec2(rest, 0))) runCommand("paint_layer_merge");
    ImGui::EndDisabled();
    ui::HelpText("Merge down", nullptr,
                 "Merge the active layer into the one below it (Base for the lowest layer), keeping how it looks.", kPath);
    ImGui::SameLine();
    ImGui::BeginDisabled(stackSize == 0);
    if (ImGui::Button(tr("Flatten##paint"), ImVec2(-1, 0))) runCommand("paint_layer_flatten");
    ImGui::EndDisabled();
    ui::HelpText("Flatten", nullptr, "Bake every visible layer into Base and remove the layers (hidden layers are discarded).",
                 kPath);

    ImGui::BeginDisabled(mesh.vertexCount() == 0 || (cur && cur->locked));
    if (ImGui::Button(cur ? "Clear layer" : "Clear Base", ImVec2(half, 0))) runCommand("paint_layer_clear");
    ImGui::EndDisabled();
    ui::HelpText("Clear", nullptr, "Clear the active layer: a layer becomes transparent, Base white. Undoable.", kPath);
    ImGui::SameLine();
    ImGui::BeginDisabled(mesh.vertexCount() == 0 || (cur && (cur->locked || !cur->visible)));
    if (ImGui::Button(tr("Fill layer"), ImVec2(-1, 0))) runCommand("paint_fill", {{"color", colorToHex(doc_.paintColor)}});
    ImGui::EndDisabled();
    ui::HelpText("Fill", nullptr, "Fill the active layer with the current colour (respects the mask, like Color > Fill).",
                 kPath);
    if (texture) drawFillLayerControls(); // (PanelFill.cpp)
}

} // namespace sf
