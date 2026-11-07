// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The Insert brush (@foirgairwolf's idea): Insert on the model (#34, Parts > Append > Insert on the model) as a brush
// on the bottom shelf. A drag on the surface places the shape it holds, on the surface point and along its normal,
// the drag's length its size and its direction its turn; each one is a new Part (or joins the active Part) in one
// undo step (insert_primitive / insert_mesh new_part=true, core/Parts.cpp). Its palette, open while it is on,
// picks the shape: a primitive, an item of a built-in insert set (Creature bones) or of your own set.
#include "app/App.h"
#include "app/I18n.h"
#include "app/Pickers.h"
#include "app/Ui.h"

#include <IconsLucide.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>
#include <vector>

namespace sf {

namespace {

// The primitives insert_primitive makes, in the palette's order ([ and ] step through them).
const char* const kInsertShapes[] = {"sphere", "cube", "cylinder", "cone", "torus", "capsule", "quadsphere"};

std::string shapeLabel(const std::string& type) {
    if (type == "quadsphere") return "Quad Sphere";
    std::string label = type;
    if (!label.empty()) label[0] = char(std::toupper(static_cast<unsigned char>(label[0])));
    return label;
}

} // namespace

void App::registerInsertBrushActions() {
    Action a;
    a.id = "brush.insert";
    a.label = "Insert";
    a.icon = ICON_LC_SHAPES;
    a.palette = "Brush";
    a.description = "The Insert brush: drag on the model to place a shape there, standing on the surface; the drag sets "
                    "its size and turn. Its palette picks a primitive or an insert set's piece (Creature bones, or your "
                    "own), and whether each one is a new Part or joins the active Part. One undo step each; Esc stops.";
    a.run = [this] { setInsertBrush(!insertBrush_.on); };
    a.checked = [this] { return insertBrush_.on; };
    actions_.add(std::move(a));
}

void App::setInsertBrush(bool on) {
    if (drag_ == Drag::Insert) return;
    insertBrush_.on = on;
    if (!on) {
        insertType_.clear();
        status_ = "Insert off";
        return;
    }
    // A mode beside the brushes: Paint, the QRemesh guides, the Gizmo and the mask brush make way.
    paintMode_ = false;
    if (guidesMode_) setGuidesMode(false);
    if (gizmoActive()) setGizmoMode(GizmoMode::Off);
    maskBrush_.on = false;
    if (insertBrush_.shape == "set" && !insertSet_.items.empty()) {
        insertType_ = "mesh";
        insertMeshKey_ = "set";
        insertMeshValue_ = insertSet_.path;
        selectInsertSetItem(insertSet_.index);
    } else {
        chooseInsertShape(insertBrush_.shape == "set" ? std::string("sphere") : insertBrush_.shape);
    }
}

void App::chooseInsertShape(const std::string& type) {
    insertBrush_.shape = type;
    insertType_ = type;
    status_ = "Insert " + shapeLabel(type) + ": drag on the model (the drag sets its size and turn); [ and ] change the "
              "shape. Esc stops.";
}

void App::stepInsertShape(int step) {
    const int n = int(std::size(kInsertShapes));
    int at = 0;
    for (int i = 0; i < n; ++i)
        if (insertType_ == kInsertShapes[i]) at = i;
    chooseInsertShape(kInsertShapes[((at + step) % n + n) % n]);
}

void App::syncInsertBrush() {
    if (!insertBrush_.on || drag_ == Drag::Insert) return;
    // Esc (or the set's window closing) cleared the shape; Paint, the guides or the Gizmo took over.
    if (insertType_.empty() || paintMode_ || guidesMode_ || gizmoActive()) {
        insertBrush_.on = false;
        insertType_.clear();
    } else if (insertType_ == "mesh" && insertMeshKey_ == "set") {
        insertBrush_.shape = "set";
    } else if (insertType_ != "mesh") {
        insertBrush_.shape = insertType_; // (Append > Insert on the model picked another primitive)
    }
}

// The palette: the primitives, each built-in set's pieces, your own set's, and New Part. Bottom left of the canvas,
// where the insert set's picker opens.
void App::drawInsertBrushPalette() {
    syncInsertBrush();
    if (!insertBrush_.on) return;
    const float s = uiScale_, tile = 56.0f * s;
    const CanvasRect c = canvasRect();
    ImGui::SetNextWindowPos(ImVec2(c.x + 12.0f * s, c.y + c.h - 12.0f * s), ImGuiCond_Appearing, ImVec2(0.0f, 1.0f));
    ImGui::SetNextWindowSizeConstraints(ImVec2(0, 0), ImVec2(c.w * 0.6f, c.h * 0.8f));
    bool open = true;
    const std::string title = std::string(tr("Insert")) + "###insertbrush";
    if (ImGui::Begin(title.c_str(), &open, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing)) {
        const float spacing = ImGui::GetStyle().ItemSpacing.x;
        constexpr int kPerRow = 7;
        int column = 0;
        auto next = [&] {
            if (column++ % kPerRow) ImGui::SameLine(0, spacing);
        };
        ImGui::TextDisabled("%s", tr("Shapes"));
        for (const char* type : kInsertShapes) {
            next();
            const std::string label = tr(shapeLabel(type).c_str());
            const std::string id = std::string("##insertshape_") + type;
            if (AssetTile(id.c_str(), thumbs_.insertShape(type), ICON_LC_SHAPES, label, tile, insertType_ == type))
                chooseInsertShape(type);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("%s", label.c_str());
        }
        // The built-in sets (listed once: their items come from inside the app), then your own set if one is chosen.
        const std::vector<BuiltinInsertSet>& sets = builtinInsertSets();
        if (insertBrush_.builtin.size() != sets.size()) {
            insertBrush_.builtin.assign(sets.size(), {});
            std::string error;
            for (size_t i = 0; i < sets.size(); ++i)
                listInsertSet(std::string(kBuiltinInsertSetPrefix) + sets[i].id, insertBrush_.builtin[i], error);
        }
        auto setTiles = [&](const std::string& path, const std::vector<InsertSetItem>& items, bool builtin, const char* key) {
            column = 0;
            const bool current = insertSetActive() && insertSet_.path == path;
            for (size_t i = 0; i < items.size(); ++i) {
                next();
                const InsertSetItem& it = items[i];
                const std::string label = builtin ? tr(it.name) : it.name; // a built-in set's names are ours to translate
                const std::string id = std::string("##") + key + std::to_string(i);
                if (AssetTile(id.c_str(), thumbs_.insertSetItem(it.file, it.object), ICON_LC_BOX, label, tile,
                              current && int(i) == insertSet_.index)) {
                    if (!current) chooseInsertSet(path);
                    selectInsertSetItem(int(i));
                }
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("%s", label.c_str());
            }
        };
        bool ownSet = insertSetActive();
        for (size_t i = 0; i < sets.size(); ++i) {
            const std::string path = std::string(kBuiltinInsertSetPrefix) + sets[i].id;
            ownSet = ownSet && insertSet_.path != path;
            if (insertBrush_.builtin[i].empty()) continue;
            ImGui::Spacing();
            ImGui::TextDisabled("%s", tr(sets[i].name));
            setTiles(path, insertBrush_.builtin[i], true, ("insertbuiltin" + std::to_string(i) + "_").c_str());
        }
        if (ownSet) {
            const auto p = std::filesystem::path(std::u8string(insertSet_.path.begin(), insertSet_.path.end())).lexically_normal();
            const std::string name = (p.has_filename() ? p.filename() : p.parent_path().filename()).string();
            ImGui::Spacing();
            ImGui::TextDisabled("%s  (%d)", name.c_str(), int(insertSet_.items.size()));
            setTiles(insertSet_.path, insertSet_.items, false, "insertown");
        }
        ImGui::Spacing();
        if (ImGui::SmallButton(tr("Your set from file..."))) openInsertSetDialog(false);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
            ImGui::SetTooltip("%s", tr("A model file with several objects: insert them one at a time"));
        ImGui::SameLine();
        if (ImGui::SmallButton(tr("Your set from folder..."))) openInsertSetDialog(true);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
            ImGui::SetTooltip("%s", tr("A folder of .obj, .ply or .glb files: insert them one at a time"));
        ui::Checkbox(tr("New Part"), &insertBrush_.newPart);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
            ImGui::SetTooltip("%s", tr("On: each shape is a Part of its own, beside the active one. Off: it joins the "
                                       "active Part as a new face group."));
        if (!insertBrush_.newPart) {
            ImGui::SameLine();
            ui::Checkbox(tr("Mask the rest"), &insertBrush_.maskRest);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
                ImGui::SetTooltip("%s", tr("Joining the Part: everything else is masked, so the brushes shape and blend "
                                           "only the new piece. Ctrl+drag on the background clears the mask."));
        }
        ImGui::TextDisabled("%s", tr("Drag on the model: the drag sets size and turn.   [ / ] previous / next   Esc stops"));
    }
    ImGui::End();
    if (!open) setInsertBrush(false);
}

} // namespace sf
