// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Masking: Ctrl mask strokes, MaskLasso / MaskRect selections (the shape is
// tracked and drawn by Selection.cpp), the Ctrl + background gestures and the
// "Masking" section of the Tools panel. The operations themselves are the
// mask_* commands (core/Masking.cpp).
#include "app/App.h"
#include "app/Palettes.h"
#include "app/Pickers.h"
#include "app/Ui.h"
#include "app/I18n.h"

#include <IconsLucide.h>

#include <algorithm>
#include <cmath>
#include <string>

namespace sf {

namespace {

constexpr float kMaskDragThresholdPx = 4.0f; // movement that turns a Ctrl+click into a drag

bool ctrlDown(SDL_Keymod mod) { return (mod & (SDL_KMOD_CTRL | SDL_KMOD_GUI)) != 0; }

bool anyMasked(const Mesh& mesh) {
    return std::any_of(mesh.mask.begin(), mesh.mask.end(), [](float m) { return m > 0.0f; });
}

std::string symmetryAxes(const Symmetry& s) { return std::string(s.x ? "x" : "") + (s.y ? "y" : "") + (s.z ? "z" : ""); }

} // namespace

void App::refreshSymmetryPivot() {
    if (!symmetry_.local) return;
    if (doc_.sculptor.strokeActive() || solverBusy()) return; // (the centre holds still during a stroke)
    const uint64_t key = doc_.undo.revision() * 0x9E3779B97F4A7C15ull ^ (uint64_t(doc_.activeSubtool) << 40) ^
                         uint64_t(doc_.mesh.vertexCount()) ^ (uint64_t(doc_.subtools.size()) << 56);
    if (key == symmetryPivotKey_) return;
    symmetryPivotKey_ = key;
    symmetry_.pivot = symmetryPivot(doc_.mesh);
}

Args App::withSymmetryArgs(Args args) {
    refreshSymmetryPivot();
    args["symmetry"] = symmetryAxes(symmetry_);
    if (symmetry_.radialOn()) {
        args["radial"] = double(symmetry_.radial);
        args["radial_axis"] = std::string(1, char('x' + symmetry_.radialAxis));
    }
    if (symmetry_.local) args["local"] = true;
    return args;
}

void App::beginMaskStroke(float x, float y) {
    refreshSymmetryPivot();
    // Masks are not layered, so the stroke always targets the base (its weight is never 0).
    // Ctrl+Alt erases, and so does the pen's eraser end (set to Invert or Erase mask).
    strokeInvert_ = (inputMods() & SDL_KMOD_ALT) != 0 ||
                    (erasing() && (eraserAction() == PenAction::Invert || eraserAction() == PenAction::EraseMask));
    if (!doc_.sculptor.beginStroke(doc_.mesh, strokeInvert_ ? "Unmask" : "Mask", -1)) return;
    strokeMask_ = true;
    strokePaint_ = false; // mask strokes never paint or sculpt
    strokeSculpt_ = true;
    paintSnapshot_.reset();
    strokeBrush_ = BrushType::Standard;
    haveLastDab_ = false;
    drag_ = Drag::Stroke;
    lastX_ = x;
    lastY_ = y;
    strokePath_.reset(x, y);
    dabQueue_.clear();
    lazyMouse_.reset(glm::vec2(x, y));
    steadyCursor_ = glm::vec2(x, y);
    precision_.reset(glm::vec2(x, y));
    precisionActive_ = false;
    pickSmoother_.reset();
    strokeCurve_ = PressurePoints(); // (mask strokes: the tablet's curve only)
    // ZBrush: a Ctrl+click on the model blurs the mask (Ctrl+Alt+click sharpens it); only a drag paints. The
    // pen's eraser end set to Erase mask paints straight away.
    maskClickPending_ = ctrlDown(inputMods()) && !blenderStyle(); // (Blender's Mask brush dabs on a click)
    maskClickX_ = x;
    maskClickY_ = y;
    if (!maskClickPending_) firstDab(x, y);
}

void App::maskClickRelease() {
    maskClickPending_ = false;
    const bool sharpen = strokeInvert_;
    endStroke(); // nothing was dabbed: no undo step
    if (anyMasked(doc_.mesh)) runCommand(sharpen ? "mask_sharpen" : "mask_blur");
}

// ZBrush convention: Ctrl+drag on the background with MaskPen draws a mask rectangle (#48) that masks what it
// covers, and clears the mask when it covers none of the model. With nothing masked the SculptForge input style
// zooms instead, as it did before masking existed.
void App::maskCanvasMove(float x, float y) {
    if (maskCanvasMoved_) return;
    if (std::hypot(x - maskCanvasX_, y - maskCanvasY_) < kMaskDragThresholdPx) return;
    maskCanvasMoved_ = true;
    if (uiPrefs_.inputStyle == 1 && !anyMasked(doc_.mesh)) {
        drag_ = Drag::Zoom;
        return;
    }
    // (MaskLasso and MaskRect draw their own shape from the press on: maskSelectMouseDown.)
    beginSelection(maskCanvasX_, maskCanvasY_, SelectTarget::Mask, SelectShape::Rect);
    selectionMove(x, y);
}

void App::maskCanvasRelease() {
    if (!maskCanvasMoved_) runCommand("mask_invert"); // Ctrl+click on the background
}

// MaskLasso / MaskRect: Ctrl+drag draws the shape, starting on the model or the background.
bool App::maskSelectMouseDown(float x, float y, SDL_Keymod mod) {
    const bool ctrl = (mod & (SDL_KMOD_CTRL | SDL_KMOD_GUI)) != 0;
    if (!ctrl || (mod & SDL_KMOD_SHIFT) || maskShape_ == SelectShape::Pen || previewLod_ >= 0) return false;
    beginSelection(x, y, SelectTarget::Mask, maskShape_);
    return true;
}

void App::maskSelectMouseUp() {
    const bool alt = (inputMods() & SDL_KMOD_ALT) != 0;
    if (!selectMoved_) {
        // A click on the background inverts the mask, as with MaskPen; on the model it blurs the mask (Alt:
        // sharpens it), as with MaskPen and as in ZBrush whichever mask brush is picked (#51).
        doc_.syncBvh();
        RayHit hit;
        if (!pick(selectPath_.front().x, selectPath_.front().y, hit)) {
            if (!alt) runCommand("mask_invert");
        } else if (anyMasked(doc_.mesh) && !blenderStyle()) {
            runCommand(alt ? "mask_sharpen" : "mask_blur");
        }
        return;
    }
    // Mask the inside, or unmask it with Alt (Mask Strength sets how much), mirrored by the symmetry like a mask
    // stroke (#51). A shape that covers none of the model clears the mask instead (ZBrush).
    runSelection("mask_rect", "mask_lasso",
                 withSymmetryArgs({{"unmask", alt}, {"strength", double(maskStrength_)}, {"clear_outside", !alt},
                                   {"front_faces_only", maskFrontFacesOnly_}}));
}

App::HeldBrush App::heldBrush() const {
    const SDL_Keymod mod = inputMods();
    if (!ctrlDown(mod)) return HeldBrush::None;
    return (mod & SDL_KMOD_SHIFT) ? HeldBrush::Select : HeldBrush::Mask;
}

std::vector<App::HeldBrushItem> App::heldBrushItems(HeldBrush held) {
    if (held == HeldBrush::Mask)
        return {{"mask.pen", "Mask Pen", "Dots", ICON_LC_PEN_TOOL, SelectShape::Pen},
                {"mask.lasso", "Mask Lasso", "Lasso", ICON_LC_LASSO, SelectShape::Lasso},
                {"mask.rect", "Mask Rectangle", "Rect", ICON_LC_SQUARE_DASHED, SelectShape::Rect}};
    if (held == HeldBrush::Select)
        return {{"select.rect", "Select Rectangle", "Rect", ICON_LC_SQUARE_DASHED, SelectShape::Rect},
                {"select.lasso", "Select Lasso", "Lasso", ICON_LC_LASSO, SelectShape::Lasso},
                {"cut.line", "Cut Line", "Line", ICON_LC_SLASH, SelectShape::CutLine},
                {"cut.lasso", "Cut Lasso", "Lasso", ICON_LC_SCISSORS, SelectShape::CutLasso},
                {"cut.rect", "Cut Rectangle", "Rect", ICON_LC_SQUARE_DASHED, SelectShape::CutRect},
                {"cut.polyline", "Cut Polyline", "Polyline", ICON_LC_WAYPOINTS, SelectShape::CutPolyline}};
    return {};
}

const App::HeldBrushItem* App::heldBrushCurrent(HeldBrush held) const {
    static const std::vector<HeldBrushItem> mask = heldBrushItems(HeldBrush::Mask);
    static const std::vector<HeldBrushItem> select = heldBrushItems(HeldBrush::Select);
    const std::vector<HeldBrushItem>& items = held == HeldBrush::Mask ? mask : select;
    if (held == HeldBrush::None) return nullptr;
    const SelectShape shape = held == HeldBrush::Mask ? maskShape_ : hideShape_;
    for (const HeldBrushItem& it : items)
        if (it.shape == shape) return &it;
    return nullptr;
}

bool App::drawHeldBrushGrid(HeldBrush held, bool strokes) {
    const float tile = 64.0f * uiScale_;
    const HeldBrushItem* current = heldBrushCurrent(held);
    bool picked = false;
    int column = 0;
    bool cutRow = false;
    for (const HeldBrushItem& it : heldBrushItems(held)) {
        if (isCutShape(it.shape) != cutRow) { // the Cut shapes get a row of their own under the selections
            cutRow = true;
            column = 0;
        }
        if (column++ > 0) ImGui::SameLine(0.0f, 6.0f);
        const char* label = strokes ? it.stroke : it.brush;
        const std::string id = std::string(label) + "##held." + it.action;
        const Action* a = actions_.find(it.action);
        if (AssetTile(id.c_str(), ImTextureID(0), it.icon, label, tile, current && current->shape == it.shape) && a &&
            a->run) {
            actions_.touch(a->id);
            a->run();
            status_ = std::string("Brush: ") + it.brush;
            picked = true;
        }
        const std::string help = strokes ? std::string("Stroke of ") + it.brush + ". " + (a ? a->description : "")
                                         : (a ? a->description : std::string());
        ui::HelpText(label, nullptr, help.c_str(),
                     held == HeldBrush::Mask ? "Tool > Masking" : isCutShape(it.shape) ? "Tool > Cut" : "Tool > Visibility");
    }
    return picked;
}

void App::drawMaskingSection() {
    ImGui::Checkbox(tr("Show mask"), &renderSettings_.showMask);
    ImGui::SameLine();
    ImGui::TextDisabled("%s", tr(maskShape_ == SelectShape::Pen ? "(Ctrl paints, Ctrl+Alt erases)"
                                                             : "(Ctrl masks, Ctrl+Alt unmasks)"));
    ui::Segmented(*this, {"mask.pen", "mask.lasso", "mask.rect"});
    if (maskShape_ != SelectShape::Pen) { // #91.3
        ImGui::Checkbox(tr("Front faces only##mask"), &maskFrontFacesOnly_);
        ui::HelpText("Front faces only", nullptr,
                     tr("Mask Lasso and Mask Rectangle mask only what faces you: the far side and folds turned away "
                        "stay as they are."),
                     "Tool > Masking");
    }
    ImGui::SetNextItemWidth(-1);
    ui::SliderInside("##maskstrength", &maskStrength_, 0.01f, 1.0f, "Mask Strength %.2f");

    const ImGuiStyle& style = ImGui::GetStyle();
    const float third = (ImGui::GetContentRegionAvail().x - 2 * style.ItemSpacing.x) / 3.0f;
    const float half = (ImGui::GetContentRegionAvail().x - style.ItemSpacing.x) * 0.5f;
    if (ImGui::Button(tr("Clear##mask"), ImVec2(third, 0))) runCommand("mask_clear");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr("Unmask everything (Mask Pen: Ctrl + drag background)"));
    ImGui::SameLine();
    if (ImGui::Button(tr("Invert##mask"), ImVec2(third, 0))) runCommand("mask_invert");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr("Ctrl + click background"));
    ImGui::SameLine();
    if (ImGui::Button(tr("All##mask"), ImVec2(third, 0))) runCommand("mask_all");

    const Args passes = {{"iterations", double(maskIterations_)}};
    if (ImGui::Button(tr("Blur##mask"), ImVec2(half, 0))) runCommand("mask_blur", passes);
    ImGui::SameLine();
    if (ImGui::Button(tr("Sharpen##mask"), ImVec2(half, 0))) runCommand("mask_sharpen", passes);
    if (ImGui::Button(tr("Grow##mask"), ImVec2(half, 0))) runCommand("mask_grow", passes);
    ImGui::SameLine();
    if (ImGui::Button(tr("Shrink##mask"), ImVec2(half, 0))) runCommand("mask_shrink", passes);
    ImGui::SetNextItemWidth(-1);
    tray::SliderInt("##maskpasses", &maskIterations_, 1, 20, "Passes %d");

    if (ImGui::Button(tr("By cavity"), ImVec2(half, 0)))
        runCommand("mask_by_cavity", {{"strength", double(maskStrength_)}, {"threshold", double(cavityThreshold_)}});
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr("Mask creases and folds (replaces the mask; uses the mask strength)"));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1);
    ui::SliderInside("##cavitythreshold", &cavityThreshold_, -0.2f, 0.5f, "Threshold %.2f");
    if (ImGui::Button(tr("By face group"), ImVec2(half, 0)))
        runCommand("mask_by_polygroup", {{"group", double(maskGroup_)}});
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr("Mask one face group (replaces the mask)"));
    ImGui::SameLine();
    // The group id as a drag field the size of the button beside it (Ctrl+click or double-click types a number),
    // rather than an input with tiny - and + buttons.
    ImGui::SetNextItemWidth(-1);
    if (ImGui::DragInt("##maskgroup", &maskGroup_, 0.1f, 0, 1 << 30, tr("Group %d"), ImGuiSliderFlags_AlwaysClamp))
        maskGroup_ = std::max(0, maskGroup_);

    // Mask from the mesh border (#61): open edges, optionally polygroup borders, a few rings wide.
    if (ImGui::Button("By border##mask", ImVec2(half, 0)))
        runCommand("mask_border", {{"rings", double(maskBorderRings_)},
                                   {"polygroups", maskBorderGroups_},
                                   {"soft", maskBorderSoft_},
                                   {"strength", double(maskStrength_)}});
    ui::HelpText("By border", nullptr,
                 "Mask the open edges of the mesh and the edge rings inside them, so sculpting never moves the "
                 "border (Blender: Mask from mesh boundary). Replaces the mask and uses the mask strength. A closed "
                 "mesh has no open edges.",
                 "Tool > Masking");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1);
    tray::SliderInt("##maskborderrings", &maskBorderRings_, 1, 20, "Rings %d");
    ui::Checkbox("Face Group borders##maskborder", &maskBorderGroups_);
    ui::HelpText("Face Group borders", nullptr,
                 "By border also masks where face groups meet (ZBrush: Mask by polygroup border).", "Tool > Masking");
    ImGui::SameLine();
    ui::Checkbox("Soft##maskborder", &maskBorderSoft_);
    ui::HelpText("Soft", nullptr, "By border fades the mask from the border towards the innermost ring.",
                 "Tool > Masking");
}

} // namespace sf
