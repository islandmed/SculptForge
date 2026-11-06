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
    const uint64_t key = doc_.undo.revision() * 0x9E3779B97F4A7C15ull ^ (uint64_t(doc_.activePart) << 40) ^
                         uint64_t(doc_.mesh.vertexCount()) ^ (uint64_t(doc_.parts.size()) << 56);
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
    // a Ctrl+click on the model blurs the mask (Ctrl+Alt+click sharpens it); only a drag paints. The
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

// classic convention: Ctrl+drag on the background with MaskPen draws a mask rectangle (#48) that masks what it
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
        // sharpens it), as with MaskPen and whichever mask brush is picked (#51).
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
    // stroke (#51). A shape that covers none of the model clears the mask instead.
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
                {"mask.lasso", "Lasso Mask", "Lasso", ICON_LC_LASSO, SelectShape::Lasso},
                {"mask.rect", "Rectangle Mask", "Rect", ICON_LC_SQUARE_DASHED, SelectShape::Rect}};
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

namespace {

// The little pictures on the Masking panel's buttons: a clay ball (or the shape of the selection), the masked part
// dark as the viewport shows it, so each button shows what it does before it is pressed.
enum class MaskArt { Pen, Lasso, Rect, Cavity, Peaks, FaceGroup, Colour, Facing, Border, Grow, Shrink, Sharpen, Blur,
                     Invert, Clear, All };

constexpr ImU32 kClay = IM_COL32(196, 178, 160, 255), kClayShade = IM_COL32(150, 134, 120, 255),
                kMasked = IM_COL32(52, 50, 56, 255), kLine = IM_COL32(240, 136, 46, 255);

void drawBall(ImDrawList* dl, ImVec2 c, float r) {
    dl->AddCircleFilled(c, r, kClayShade, 40);
    dl->AddCircleFilled(ImVec2(c.x - r * 0.12f, c.y - r * 0.12f), r * 0.86f, kClay, 40);
}

void drawMaskArt(ImDrawList* dl, ImVec2 c, float r, MaskArt art) {
    const float t = std::max(1.5f, r * 0.09f); // line width
    switch (art) {
    case MaskArt::Pen: {
        drawBall(dl, c, r);
        ImVec2 pts[12];
        for (int i = 0; i < 12; ++i) {
            const float u = float(i) / 11.0f;
            pts[i] = ImVec2(c.x + (u - 0.5f) * r * 1.3f, c.y + std::sin(u * 6.2832f) * r * 0.3f);
        }
        dl->AddPolyline(pts, 12, kMasked, 0, r * 0.32f);
        break;
    }
    case MaskArt::Lasso: {
        drawBall(dl, c, r);
        ImVec2 pts[16];
        for (int i = 0; i < 16; ++i) {
            const float a = float(i) / 16.0f * 6.2832f;
            const float k = 0.55f + 0.12f * std::sin(a * 3.0f);
            pts[i] = ImVec2(c.x + std::cos(a) * r * k, c.y + std::sin(a) * r * k * 0.8f);
        }
        dl->AddConvexPolyFilled(pts, 16, kMasked);
        dl->AddPolyline(pts, 16, kLine, ImDrawFlags_Closed, t * 0.7f);
        break;
    }
    case MaskArt::Rect:
        drawBall(dl, c, r);
        dl->AddRectFilled(ImVec2(c.x - r * 0.55f, c.y - r * 0.4f), ImVec2(c.x + r * 0.55f, c.y + r * 0.4f), kMasked);
        dl->AddRect(ImVec2(c.x - r * 0.55f, c.y - r * 0.4f), ImVec2(c.x + r * 0.55f, c.y + r * 0.4f), kLine, 0, 0,
                    t * 0.7f);
        break;
    case MaskArt::Cavity: // the creases masked
        drawBall(dl, c, r);
        for (int k = -1; k <= 1; ++k) {
            ImVec2 pts[9];
            for (int i = 0; i < 9; ++i) {
                const float u = float(i) / 8.0f - 0.5f;
                pts[i] = ImVec2(c.x + u * r * 1.4f, c.y + float(k) * r * 0.42f + u * u * r * 0.6f);
            }
            dl->AddPolyline(pts, 9, kMasked, 0, t * 1.6f);
        }
        break;
    case MaskArt::Peaks: { // ridges masked: bumps whose tops are dark
        drawBall(dl, c, r);
        for (int k = 0; k < 3; ++k) {
            const ImVec2 b(c.x + (float(k) - 1.0f) * r * 0.55f, c.y + r * 0.35f);
            dl->AddTriangleFilled(ImVec2(b.x - r * 0.26f, b.y), ImVec2(b.x + r * 0.26f, b.y),
                                  ImVec2(b.x, b.y - r * 0.7f), kClayShade);
            dl->AddTriangleFilled(ImVec2(b.x - r * 0.1f, b.y - r * 0.43f), ImVec2(b.x + r * 0.1f, b.y - r * 0.43f),
                                  ImVec2(b.x, b.y - r * 0.7f), kMasked);
        }
        break;
    }
    case MaskArt::FaceGroup: // two groups, one masked
        dl->PathArcTo(c, r, 1.5708f, 4.7124f, 20);
        dl->PathFillConvex(IM_COL32(120, 170, 210, 255));
        dl->PathArcTo(c, r, -1.5708f, 1.5708f, 20);
        dl->PathFillConvex(kMasked);
        dl->AddLine(ImVec2(c.x, c.y - r), ImVec2(c.x, c.y + r), kLine, t * 0.6f);
        break;
    case MaskArt::Colour: // a painted patch masked
        drawBall(dl, c, r);
        dl->AddCircleFilled(ImVec2(c.x + r * 0.15f, c.y + r * 0.1f), r * 0.48f, IM_COL32(200, 60, 40, 255), 24);
        dl->AddCircleFilled(ImVec2(c.x + r * 0.15f, c.y + r * 0.1f), r * 0.48f, IM_COL32(52, 50, 56, 190), 24);
        dl->AddCircle(ImVec2(c.x + r * 0.15f, c.y + r * 0.1f), r * 0.48f, IM_COL32(200, 60, 40, 255), 24, t * 0.8f);
        break;
    case MaskArt::Facing: // what faces you masked, the sides free
        drawBall(dl, c, r);
        dl->AddCircleFilled(c, r * 0.58f, kMasked, 32);
        dl->AddLine(ImVec2(c.x, c.y + r * 0.95f), ImVec2(c.x, c.y + r * 0.2f), kLine, t * 0.7f);
        dl->AddTriangleFilled(ImVec2(c.x - r * 0.14f, c.y + r * 0.32f), ImVec2(c.x + r * 0.14f, c.y + r * 0.32f),
                              ImVec2(c.x, c.y + r * 0.12f), kLine);
        break;
    case MaskArt::Border: // an open disc, its rim masked
        dl->AddCircleFilled(c, r, kMasked, 40);
        dl->AddCircle(c, r, kClayShade, 40, t * 0.6f);
        dl->AddCircleFilled(c, r * 0.58f, kClay, 40);
        dl->AddCircle(c, r * 0.58f, kLine, 40, t * 0.5f);
        break;
    case MaskArt::Grow:
    case MaskArt::Shrink: {
        drawBall(dl, c, r);
        const bool grow = art == MaskArt::Grow;
        dl->AddCircleFilled(c, r * (grow ? 0.62f : 0.32f), kMasked, 32);
        dl->AddCircle(c, r * (grow ? 0.32f : 0.62f), kLine, 32, t * 0.6f);
        break;
    }
    case MaskArt::Sharpen:
    case MaskArt::Blur: {
        drawBall(dl, c, r);
        if (art == MaskArt::Sharpen) {
            dl->AddCircleFilled(c, r * 0.55f, kMasked, 32);
        } else {
            for (int i = 6; i >= 0; --i) // soft edge: stacked translucent rings
                dl->AddCircleFilled(c, r * (0.3f + 0.07f * float(i)), IM_COL32(52, 50, 56, 70), 32);
        }
        break;
    }
    case MaskArt::Invert:
        dl->AddCircleFilled(c, r, kMasked, 40);
        dl->AddCircle(c, r, kClayShade, 40, t * 0.6f);
        dl->AddCircleFilled(c, r * 0.55f, kClay, 32);
        dl->PathArcTo(c, r * 0.55f, -1.5708f, 1.5708f, 16);
        dl->PathFillConvex(kMasked);
        dl->PathArcTo(c, r, 1.5708f, 4.7124f, 20);
        dl->PathArcTo(c, r * 0.55f, 4.7124f, 1.5708f, 16);
        dl->PathStroke(kLine, 0, t * 0.5f);
        break;
    case MaskArt::Clear:
        drawBall(dl, c, r);
        break;
    case MaskArt::All:
        dl->AddCircleFilled(c, r, kMasked, 40);
        dl->AddCircle(c, r, kClayShade, 40, t * 0.6f); // (its outline: dark on a dark button)
        break;
    }
}

} // namespace

// A Masking button with its picture above the label (2 lines at most); the hint is the tooltip and the F1 help.
bool App::maskButton(const char* id, const char* hint, int art, float width, bool selected) {
    const std::string idText = tr(id);
    std::string label = idText.substr(0, idText.find("##"));
    const float lineH = ImGui::GetTextLineHeight();
    const float pic = std::min(width - 6.0f, 60.0f * uiScale_);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const bool clicked = ImGui::InvisibleButton(idText.c_str(), ImVec2(width, pic + lineH * 2.0f + 6.0f));
    const bool hovered = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 b0 = p, b1(p.x + width, p.y + pic + lineH * 2.0f + 6.0f);
    dl->AddRectFilled(b0, b1, ImGui::GetColorU32(hovered ? ImGuiCol_ButtonHovered : ImGuiCol_FrameBg),
                      ImGui::GetStyle().FrameRounding);
    if (selected) dl->AddRect(b0, b1, ImGui::GetColorU32(ImGuiCol_CheckMark), ImGui::GetStyle().FrameRounding, 0, 2.0f);
    const float r = pic * 0.42f;
    drawMaskArt(dl, ImVec2(p.x + width * 0.5f, p.y + 3.0f + pic * 0.5f), r, MaskArt(art));
    // The label, centred, wrapped onto a second line at a space when it doesn't fit.
    std::string line1 = label, line2;
    if (ImGui::CalcTextSize(label.c_str()).x > width - 4.0f)
        if (const size_t sp = label.rfind(' '); sp != std::string::npos) {
            line1 = label.substr(0, sp);
            line2 = label.substr(sp + 1);
        }
    float y = p.y + pic + 3.0f;
    for (const std::string* l : {&line1, &line2}) {
        if (l->empty()) continue;
        const float w = ImGui::CalcTextSize(l->c_str()).x;
        dl->PushClipRect(b0, b1, true);
        dl->AddText(ImVec2(std::round(p.x + std::max(2.0f, (width - w) * 0.5f)), y), ImGui::GetColorU32(ImGuiCol_Text),
                    l->c_str());
        dl->PopClipRect();
        y += lineH;
    }
    ImGui::LogText(" %s", label.c_str()); // drawn directly: tell the log (UI tests read it) too
    ui::HelpText(label.c_str(), nullptr, tr(hint), "Tool > Masking");
    return clicked;
}

void App::drawMaskingSection() {
    ImGui::Checkbox(tr("Show mask"), &renderSettings_.showMask);
    ImGui::SameLine();
    ImGui::TextDisabled("%s", tr(maskShape_ == SelectShape::Pen ? "(Ctrl paints, Ctrl+Alt erases)"
                                                             : "(Ctrl masks, Ctrl+Alt unmasks)"));
    const ImGuiStyle& style = ImGui::GetStyle();
    const float avail = ImGui::GetContentRegionAvail().x;
    const int columns = avail > 300.0f * uiScale_ ? 4 : 3;
    const float cell = (avail - float(columns - 1) * style.ItemSpacing.x) / float(columns);
    int column = 0;
    auto next = [&] {
        if (column++ % columns != 0) ImGui::SameLine();
    };
    auto group = [&](const char* title) {
        ImGui::Dummy(ImVec2(0, 2));
        ImGui::SeparatorText(tr(title));
        column = 0;
    };

    // Shapes: what Ctrl+drag draws.
    group("Shapes");
    struct Shape {
        const char* action;
        const char* id;
        MaskArt art;
        SelectShape shape;
        const char* hint;
    };
    const Shape shapes[] = {
        {"mask.pen", "Mask Pen##mask.pen", MaskArt::Pen, SelectShape::Pen,
         "Ctrl+drag paints the mask with the brush; Ctrl+Alt+drag erases it. Ctrl+click on the model blurs it."},
        {"mask.lasso", "Lasso Mask##mask.lasso", MaskArt::Lasso, SelectShape::Lasso,
         "Ctrl+drag draws a lasso (any free shape): what is inside gets masked (Ctrl+Alt: unmasked)."},
        {"mask.rect", "Rectangle Mask##mask.rect", MaskArt::Rect, SelectShape::Rect,
         "Ctrl+drag draws a rectangle: what is inside gets masked (Ctrl+Alt: unmasked)."},
    };
    for (const Shape& sh : shapes) {
        next();
        if (maskButton(sh.id, sh.hint, int(sh.art), cell, maskShape_ == sh.shape))
            if (const Action* a = actions_.find(sh.action); a && a->run) {
                actions_.touch(a->id);
                a->run();
            }
    }
    if (maskShape_ != SelectShape::Pen) { // #91.3
        ImGui::Checkbox(tr("Front faces only##mask"), &maskFrontFacesOnly_);
        ui::HelpText("Front faces only", nullptr,
                     tr("Lasso Mask and Rectangle Mask mask only what faces you: the far side and folds turned away "
                        "stay as they are."),
                     "Tool > Masking");
    }
    ImGui::SetNextItemWidth(-1);
    if (float k = uiPrefs_.maskDarkness; ui::SliderInside("##maskdarkness", &k, 0.0f, 1.0f, "Mask Darkness %.2f"))
        setMaskDarkness(k);
    ui::HelpText("Mask Darkness", nullptr, "How dark masked areas show (also in Preferences > Interface).",
                 "Tool > Masking");
    ImGui::SetNextItemWidth(-1);
    ui::SliderInside("##maskstrength", &maskStrength_, 0.01f, 1.0f, "Mask Strength %.2f");

    // Mask by: whole-mesh masks from the shape, the paint, the groups and the border. Each replaces the mask.
    group("Mask by");
    const float strength = maskStrength_;
    next();
    if (maskButton("By cavity##mask", "Mask creases and folds (replaces the mask; uses the mask strength)",
                   int(MaskArt::Cavity), cell))
        runCommand("mask_by_cavity", {{"strength", double(strength)}, {"threshold", double(cavityThreshold_)}});
    next();
    if (maskButton("By peaks##mask", "Mask ridges, peaks and sharp edges (replaces the mask; uses the mask strength)",
                   int(MaskArt::Peaks), cell))
        runCommand("mask_by_peaks", {{"strength", double(strength)}, {"threshold", double(cavityThreshold_)}});
    next();
    if (maskButton("By face group##mask", "Mask one face group (replaces the mask)", int(MaskArt::FaceGroup), cell))
        runCommand("mask_by_facegroup", {{"group", double(maskGroup_)}});
    next();
    if (maskButton("By colour##mask",
                   "Mask where the vertex paint is close to the main colour (the left shelf's swatch); Tolerance "
                   "sets how close.",
                   int(MaskArt::Colour), cell)) {
        const glm::vec3 c = linearToSrgb(doc_.paintColor);
        runCommand("mask_by_color", {{"r", double(c.x)},
                                     {"g", double(c.y)},
                                     {"b", double(c.z)},
                                     {"tolerance", double(maskColorTolerance_)},
                                     {"strength", double(strength)}});
    }
    next();
    if (maskButton("By facing##mask",
                   "Mask what faces the camera (or faces up or down; Facing below), within the angle: e.g. the front "
                   "of a face, or the top of a rock.",
                   int(MaskArt::Facing), cell)) {
        const glm::vec3 dir = maskFacingDir_ == 1 ? glm::vec3(0, 1, 0)
                              : maskFacingDir_ == 2 ? glm::vec3(0, -1, 0)
                                                    : -camera_.forward();
        runCommand("mask_by_facing", {{"x", double(dir.x)},
                                      {"y", double(dir.y)},
                                      {"z", double(dir.z)},
                                      {"angle", double(maskFacingAngle_)},
                                      {"strength", double(strength)}});
    }
    next();
    // Mask from the mesh border (#61): open edges, optionally face group borders, a few rings wide.
    if (maskButton("By border##mask",
                   "Mask the open edges of the mesh and the edge rings inside them, so sculpting never moves the "
                   "border. Replaces the mask and uses the mask strength. A closed mesh has no open edges.",
                   int(MaskArt::Border), cell))
        runCommand("mask_border", {{"rings", double(maskBorderRings_)},
                                   {"facegroups", maskBorderGroups_},
                                   {"soft", maskBorderSoft_},
                                   {"strength", double(strength)}});

    // Adjust the mask there is.
    group("Adjust");
    const Args passes = {{"iterations", double(maskIterations_)}};
    struct Op {
        const char* id;
        const char* hint;
        MaskArt art;
        const char* command;
        bool passes;
    };
    const Op ops[] = {
        {"Grow##mask", "Expand the masked region by one edge ring per pass.", MaskArt::Grow, "mask_grow", true},
        {"Shrink##mask", "Contract the masked region by one edge ring per pass.", MaskArt::Shrink, "mask_shrink", true},
        {"Sharpen##mask", "Tighten soft mask edges (Ctrl+Alt+click on the model).", MaskArt::Sharpen, "mask_sharpen",
         true},
        {"Blur##mask", "Soften the mask edges (Ctrl+click on the model).", MaskArt::Blur, "mask_blur", true},
        {"Invert##mask", "Swap masked and free (Ctrl + click background).", MaskArt::Invert, "mask_invert", false},
        {"Clear##mask", "Unmask everything (Mask Pen: Ctrl + drag background)", MaskArt::Clear, "mask_clear", false},
        {"All##mask", "Mask every visible vertex.", MaskArt::All, "mask_all", false},
    };
    for (const Op& op : ops) {
        next();
        if (maskButton(op.id, op.hint, int(op.art), cell)) runCommand(op.command, op.passes ? passes : Args{});
    }
    ImGui::SetNextItemWidth(-1);
    tray::SliderInt("##maskpasses", &maskIterations_, 1, 20, "Passes %d");

    // The options of the Mask by buttons.
    if (ImGui::TreeNodeEx(tr("Mask by options##mask"), ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_DefaultOpen)) {
        const float half = (ImGui::GetContentRegionAvail().x - style.ItemSpacing.x) * 0.5f;
        ImGui::SetNextItemWidth(-1);
        ui::SliderInside("##cavitythreshold", &cavityThreshold_, -0.2f, 0.5f, "Cavity / peaks threshold %.2f");
        ui::HelpText("Threshold", nullptr,
                     "How deep a crease (By cavity) or how sharp a ridge (By peaks) must be: 0 takes anything, higher "
                     "only the strongest.",
                     "Tool > Masking");
        // The group id as a drag field (Ctrl+click or double-click types a number).
        ImGui::SetNextItemWidth(-1);
        if (ImGui::DragInt("##maskgroup", &maskGroup_, 0.1f, 0, 1 << 30, tr("Face group %d"), ImGuiSliderFlags_AlwaysClamp))
            maskGroup_ = std::max(0, maskGroup_);
        ImGui::SetNextItemWidth(-1);
        ui::SliderInside("##maskcolortol", &maskColorTolerance_, 0.01f, 1.0f, "Colour tolerance %.2f");
        ImGui::SetNextItemWidth(half);
        const char* facing[] = {tr("Faces the camera"), tr("Faces up"), tr("Faces down")};
        ImGui::Combo("##maskfacing", &maskFacingDir_, facing, 3);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-1);
        ui::SliderInside("##maskfacingangle", &maskFacingAngle_, 5.0f, 180.0f, "Angle %.0f");
        ImGui::SetNextItemWidth(-1);
        tray::SliderInt("##maskborderrings", &maskBorderRings_, 1, 20, "Border rings %d");
        ui::Checkbox("Face Group borders##maskborder", &maskBorderGroups_);
        ui::HelpText("Face Group borders", nullptr, "By border also masks where face groups meet.", "Tool > Masking");
        ImGui::SameLine();
        ui::Checkbox("Soft##maskborder", &maskBorderSoft_);
        ui::HelpText("Soft", nullptr, "By border fades the mask from the border towards the innermost ring.",
                     "Tool > Masking");
        ImGui::TreePop();
    }
}

} // namespace sf
