// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Pop-up pickers (brush, stroke, alpha and material pop-ups), the
// Size / Strength sliders at the cursor, and the thumbnail tile.
#include "app/Theme.h"
#include "app/NativeDialog.h"
#include "app/Pickers.h"

#include "app/App.h"
#include "app/I18n.h"
#include "app/UiServe.h"
#include "app/AssetLibrary.h"
#include "app/Ui.h"
#include "core/BrushLibrary.h"
#include "core/BrushPacks.h"
#include "core/ListSort.h"
#include "core/LitSphere.h"

#include <IconsLucide.h>

#include <imgui_internal.h>

#include <algorithm>
#include <filesystem>
#include <functional>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace sf {

namespace {

// Theme tokens (docs/ui-design.md section 10): the Classic look's values, or the current look's tokens.
ImU32 lookColor(ImU32 classic, const ImVec4& token, float alpha = 1.0f) {
    return theme::look.id == 0 ? classic : theme::u32(token, alpha);
}
ImU32 kAccent() { return lookColor(IM_COL32(240, 136, 46, 255), theme::accent); }
ImU32 kTileBg() { return lookColor(IM_COL32(42, 42, 46, 255), theme::tile); }
ImU32 kTileHover() { return lookColor(IM_COL32(54, 54, 59, 255), theme::headerHover); }
ImU32 kTileBorder() { return lookColor(IM_COL32(18, 18, 20, 255), theme::tileBorder); }
ImU32 kGlyph() { return lookColor(IM_COL32(140, 140, 144, 255), theme::textDim); }
ImU32 kText() { return lookColor(IM_COL32(216, 216, 216, 255), theme::text); }
ImU32 kTextDim() { return lookColor(IM_COL32(140, 140, 144, 255), theme::textDim); }
ImU32 kPanelBg() { return lookColor(IM_COL32(37, 37, 40, 252), theme::panelBg, 0.99f); } // #252528, pop-ups
// The left tray's colour field. These should become theme tokens (Theme.h) once the theme settles.
constexpr ImU32 kFieldEdge = IM_COL32(14, 14, 16, 255);       // 1 px around the SV square, hue strip and swatches
constexpr ImU32 kFieldHairline = IM_COL32(255, 255, 255, 28); // inner highlight that lifts a swatch off the tray
constexpr ImU32 kFieldHover = IM_COL32(255, 255, 255, 90);    // outline of the swatch under the mouse
constexpr ImU32 kMarkerLight = IM_COL32(255, 255, 255, 245);  // SV ring and hue notch
constexpr ImU32 kMarkerDark = IM_COL32(0, 0, 0, 150);         // their outline, readable on white
constexpr float kFieldGap = 4.0f;   // one gap between every part of the colour field (square, strip, swatches, Fill)
constexpr float kHueStripW = 10.0f; // wide enough to read as a control next to the square
constexpr float kSwatchH = 18.0f;
constexpr float kRecentH = 12.0f; // the recent colours' rows: thin

const SDL_DialogFileFilter kAlphaFilters[] = {{"Images", "png;jpg;jpeg;bmp;tga;psd;gif"}, {"All files", "*"}};

std::string lower(std::string s) {
    for (auto& c : s)
        c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
// Shortens `text` with "..." to fit `width`.
std::string elide(const std::string& text, float width) {
    if (ImGui::CalcTextSize(text.c_str()).x <= width) return text;
    std::string s = text;
    while (!s.empty() && ImGui::CalcTextSize((s + "...").c_str()).x > width) {
        // One whole UTF-8 character: its continuation bytes, then its lead byte.
        while (!s.empty() && (static_cast<unsigned char>(s.back()) & 0xC0) == 0x80) s.pop_back();
        if (!s.empty()) s.pop_back();
    }
    return s + "...";
}

struct Glow {
    int frame = -1;
    ImRect rect;
    ImTextureID tex = ImTextureID(0);
    std::string glyph;
};
Glow& glow() {
    static Glow g;
    return g;
}

// --- Picker state ---

struct State {
    bool open = false;
    Picker picker = Picker::Brush;
    ImVec2 anchor{0, 0};
    bool underMouse = false;
    int openedFrame = -1, closedFrame = -1;
    int keysFrom = 0; // frame from which typed keys count (not the key that opened it)
    bool addToShelf = false;
    // Opened with Ctrl (mask brushes) or Ctrl+Shift (selection brushes) held: App::HeldBrush.
    int held = 0;
    std::string typed; // brush picker: letters typed so far
    // Material hover preview: the material to go back to.
    bool previewing = false;
    int savedLitSphere = -1;
    // Size / Intensity / Colour intensity / Falloff at the cursor.
    int slider = -1; // PickerUi::Slider, -1 = closed
    bool sliderSmooth = false; // Intensity of the Smooth brush (Shift was held)
    ImVec2 sliderAt{0, 0};  // the cursor when it opened
    ImVec2 sliderPos{0, 0}; // the window's place, fixed once the knob is under sliderAt
    bool sliderPlaced = false;
    ImVec2 sliderKnob{0, 0};
    int sliderFrame = -1;
    std::string sliderTyped;
    bool sliderWasActive = false;
    // Recently used brushes (ids, newest first).
    std::vector<std::string> recent;
    std::string lastActive;
    // The same for alphas (names) and materials (lit sphere indices as text), for the lists' Recent order.
    std::vector<std::string> recentAlphas, recentMaterials;
    std::string lastAlpha, lastMaterial;
};

void touchRecent(std::vector<std::string>& list, std::string& last, const std::string& id) {
    if (id == last) return;
    if (!last.empty()) {
        list.erase(std::remove(list.begin(), list.end(), id), list.end());
        list.insert(list.begin(), id);
        if (list.size() > 12) list.resize(12);
    }
    last = id;
}
int recentRank(const std::vector<std::string>& list, const std::string& id) {
    const auto it = std::find(list.begin(), list.end(), id);
    return it == list.end() ? -1 : int(it - list.begin());
}

State& st() {
    static State s;
    return s;
}

void closePicker() {
    State& s = st();
    if (!s.open) return;
    s.open = false;
    s.closedFrame = ImGui::GetFrameCount();
    s.typed.clear();
}

// Lays tiles out left to right, `columns` per row.
void tileGrid(int& column, int columns) {
    if (column > 0 && column < columns) ImGui::SameLine(0.0f, 6.0f);
    if (column >= columns) column = 0;
    ++column;
}

int columnsFor(float tile, GridMode mode) {
    if (mode == GridMode::Popup) return 6;
    return std::max(1, int((ImGui::GetContentRegionAvail().x + 6.0f) / (tile + 6.0f)));
}

void sectionHeader(const char* text) {
    ImGui::Dummy(ImVec2(0, 2));
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::SeparatorText(tr(text));
    ImGui::PopStyleColor();
}

// Hover "help box": a large thumbnail and the description shows for palette items.
void quickInfo(ImTextureID tex, const char* glyph, const std::string& title, const std::string& text,
               const std::string& footer = {}) {
    if (!ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort | ImGuiHoveredFlags_NoSharedDelay) ||
        ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId) || ImGui::GetIO().KeyCtrl)
        return;
    // Beside the window the tile is in (right of it, else left), level with the tile: at the mouse it covered
    // the grid it describes, clipping the labels and the next rows.
    const float big = 192.0f * 0.75f;
    {
        constexpr float kCardW = 356.0f, kCardH = 300.0f, kGap = 6.0f; // the card's largest size, with padding
        const ImGuiWindow* host = ImGui::GetCurrentWindow()->RootWindow;
        const ImGuiViewport* vp = ImGui::GetMainViewport();
        ImVec2 pos(host->Pos.x + host->Size.x + kGap, ImGui::GetItemRectMin().y), pivot(0.0f, 0.0f);
        if (pos.x + kCardW > vp->Pos.x + vp->Size.x && host->Pos.x - kGap - kCardW >= vp->Pos.x) {
            pos.x = host->Pos.x - kGap;
            pivot.x = 1.0f;
        }
        pos.y = std::clamp(pos.y, vp->Pos.y + 4.0f, std::max(vp->Pos.y + 4.0f, vp->Pos.y + vp->Size.y - kCardH));
        ImGui::SetNextWindowPos(pos, ImGuiCond_Always, pivot);
    }
    ImGui::SetNextWindowSizeConstraints(ImVec2(0, 0), ImVec2(340, FLT_MAX));
    ImGui::BeginTooltip();
    if (tex)
        ImGui::Image(tex, ImVec2(big, big));
    else if (glyph) {
        ImGui::Dummy(ImVec2(big, big));
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), kTileBg(), theme::rounding(4.0f));
        const float fs = ImGui::GetFontSize() * 3.0f;
        const ImVec2 sz = ImGui::GetFont()->CalcTextSizeA(fs, FLT_MAX, 0, glyph);
        const ImVec2 c = ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax()).GetCenter();
        dl->AddText(ImGui::GetFont(), fs, ImVec2(c.x - sz.x * 0.5f, c.y - sz.y * 0.5f), kGlyph(), glyph);
    }
    ImGui::TextUnformatted(title.c_str());
    if (!text.empty()) {
        ImGui::PushTextWrapPos(330.0f);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.78f, 0.78f, 0.78f, 1.0f));
        ImGui::TextUnformatted(text.c_str());
        ImGui::PopStyleColor();
        ImGui::PopTextWrapPos();
    }
    if (!footer.empty()) ImGui::TextDisabled("%s", footer.c_str());
    ImGui::EndTooltip();
}

// Opens the picker's window at its anchor (or the mouse); returns false when it isn't showing.
bool beginPickerWindow(const char* title, float width) {
    State& s = st();
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImVec2 pos = s.anchor;
    if (s.underMouse) pos = ImVec2(s.anchor.x - 24.0f, s.anchor.y - 24.0f);
    ImGui::SetNextWindowPos(pos, ImGuiCond_Appearing);
    ImGui::SetNextWindowSizeConstraints(ImVec2(width, 0), ImVec2(width, vp->Size.y - 60.0f));
    if (s.openedFrame == ImGui::GetFrameCount()) ImGui::SetNextWindowFocus();
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, theme::rounding(6.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10, 8));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, kPanelBg());
    ImGui::PushStyleColor(ImGuiCol_Border, lookColor(IM_COL32(64, 64, 70, 255), theme::tileBorder));
    const bool visible = ImGui::Begin(title, nullptr,
                                      ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                          ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                          ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse);
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(2);
    // Keep it inside the window (it may have opened near an edge).
    ImGuiWindow* w = ImGui::GetCurrentWindow();
    ImVec2 p = w->Pos;
    p.x = std::clamp(p.x, vp->Pos.x + 4.0f, std::max(vp->Pos.x + 4.0f, vp->Pos.x + vp->Size.x - w->Size.x - 4.0f));
    p.y = std::clamp(p.y, vp->Pos.y + 4.0f, std::max(vp->Pos.y + 4.0f, vp->Pos.y + vp->Size.y - w->Size.y - 4.0f));
    if (p.x != w->Pos.x || p.y != w->Pos.y) ImGui::SetWindowPos(p);
    // A click outside closes it (context menus of its tiles are part of it).
    const bool clicked = ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Right);
    if (clicked && s.openedFrame != ImGui::GetFrameCount() &&
        !ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByPopup) &&
        !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId))
        closePicker();
    return visible;
}

void pickerTitle(const char* icon, const char* title, const char* hint) {
    ImGui::TextUnformatted(icon);
    ImGui::SameLine();
    ImGui::TextUnformatted(title);
    if (hint) {
        ImGui::SameLine();
        const float w = ImGui::CalcTextSize(hint).x;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, ImGui::GetContentRegionAvail().x - w));
        ImGui::TextDisabled("%s", hint);
    }
}

const BrushAlpha& builtinAlpha(int i) {
    const auto& all = BrushAlpha::builtins(); // Round, Square, Noise and the generated patterns (#43)
    return all[size_t(std::clamp(i, 0, int(all.size()) - 1))];
}

char paletteFilter[64] = ""; // the brush palette's filter field

} // namespace

// ---------------------------------------------------------------------------
// Tiles
// ---------------------------------------------------------------------------

bool AssetTile(const char* id, ImTextureID texture, const char* glyph, const std::string& label, float thumbSize,
               bool selected, const char* badge, bool dim) {
    const float labelH = ImGui::GetFontSize() + 4.0f;
    const ImVec2 size(thumbSize, thumbSize + labelH);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const bool clicked = ImGui::InvisibleButton(id, size);
    const bool hovered = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 t0 = p, t1(p.x + thumbSize, p.y + thumbSize);
    const ui::TileLift lift = ui::BeginTileLift(t0, t1); // the thumbnail lifts on hover; its label stays
    dl->AddRectFilled(t0, t1, hovered ? kTileHover() : kTileBg(), theme::rounding(4.0f));
    if (texture) {
        const float pad = 2.0f;
        dl->AddImageRounded(texture, ImVec2(t0.x + pad, t0.y + pad), ImVec2(t1.x - pad, t1.y - pad), ImVec2(0, 0),
                            ImVec2(1, 1), dim ? IM_COL32(255, 255, 255, 110) : IM_COL32_WHITE, theme::rounding(3.0f));
    } else if (glyph) {
        const float fs = std::round(thumbSize * 0.36f);
        const ImVec2 gs = ImGui::GetFont()->CalcTextSizeA(fs, FLT_MAX, 0, glyph);
        dl->AddText(ImGui::GetFont(), fs,
                    ImVec2(std::round((t0.x + t1.x - gs.x) * 0.5f), std::round((t0.y + t1.y - gs.y) * 0.5f)), kGlyph(),
                    glyph);
    }
    dl->AddRect(t0, t1,
                selected  ? kAccent()
                : hovered ? lookColor(IM_COL32(90, 90, 96, 255), theme::textDim)
                          : kTileBorder(),
                theme::rounding(4.0f), 0, selected ? 2.0f : 1.0f);
    if (badge && *badge) {
        // The orange key of the brush pop-up: press it to pick this tile.
        const float fs = ImGui::GetFontSize();
        const ImVec2 bs = ImGui::CalcTextSize(badge);
        const ImVec2 b1(t1.x - 3.0f, t0.y + 3.0f + fs + 2.0f), b0(b1.x - std::max(bs.x + 8.0f, fs + 2.0f), t0.y + 3.0f);
        dl->AddRectFilled(b0, b1, kAccent(), theme::rounding(3.0f));
        dl->AddText(ImVec2(std::round((b0.x + b1.x - bs.x) * 0.5f), b0.y + 1.0f), lookColor(IM_COL32(20, 20, 22, 255), theme::textOnAccent), badge);
        ImGui::LogText(" [%s]", badge);
    }
    ui::EndTileLift(lift);
    const std::string shown = elide(label, thumbSize);
    const ImVec2 ls = ImGui::CalcTextSize(shown.c_str());
    dl->AddText(ImVec2(std::round(p.x + (thumbSize - ls.x) * 0.5f), t1.y + 2.0f),
                selected ? IM_COL32(255, 255, 255, 255)
                : dim    ? kTextDim()
                         : kText(),
                shown.c_str());
    ImGui::LogText(" %s", label.c_str()); // drawn directly: tell the log (UI tests read it) too
    if (hovered) {
        Glow& g = glow();
        g.frame = ImGui::GetFrameCount();
        g.rect = ImRect(t0, t1);
        g.tex = texture;
        g.glyph = glyph ? glyph : "";
    }
    return clicked;
}

float SortMenuWidth() { // the widest Sort button and the gap before it
    const ImGuiStyle& st = ImGui::GetStyle();
    return ImGui::CalcTextSize(tr("Sort: Recent")).x + st.FramePadding.x * 2.0f + st.ItemSpacing.x + 2.0f;
}

int brushRecentRank(const std::string& id) { return recentRank(st().recent, id); }

int builtinBrushPackRank(const std::string& pack) {
    const auto& packs = builtinBrushPacks();
    const auto it = std::find(packs.begin(), packs.end(), pack);
    return it == packs.end() ? -1 : int(it - packs.begin());
}

bool SortMenu(const char* id, std::string& pref, bool packs) {
    const ListSort cur = listSortFromId(pref);
    char label[64];
    std::snprintf(label, sizeof label, tr("Sort: %s##%s"), tr(listSortLabel(cur)), id);
    if (ImGui::SmallButton(label)) ImGui::OpenPopup(id);
    ui::HelpText("Sort", nullptr,
                 packs ? "The order of this list: A-Z (the default), the most recently used first, or grouped by pack."
                       : "The order of this list: A-Z (the default) or the most recently changed first.",
                 nullptr);
    bool changed = false;
    if (ImGui::BeginPopup(id)) {
        for (int i = 0; i < int(ListSort::Count); ++i) {
            const ListSort m = ListSort(i);
            if (m == ListSort::Pack && !packs) continue;
            if (ImGui::MenuItem((std::string(listSortLabel(m)) + "##" + id).c_str(), nullptr, m == cur) && m != cur) {
                pref = listSortId(m);
                changed = true;
            }
        }
        ImGui::EndPopup();
    }
    return changed;
}

void AssetTileHoverGlow() {
    Glow& g = glow();
    if (g.frame != ImGui::GetFrameCount() || !g.tex || ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId)) return;
    if (ImGui::IsMouseDragging(ImGuiMouseButton_Left)) return;
    // the classic thumbnail glow: the hovered thumbnail pops out at 1.5x.
    const float s = g.rect.GetWidth() * 1.5f;
    const ImVec2 c = g.rect.GetCenter();
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImVec2 a(c.x - s * 0.5f, c.y - s * 0.5f);
    a.x = std::clamp(a.x, vp->Pos.x + 2.0f, vp->Pos.x + vp->Size.x - s - 2.0f);
    a.y = std::clamp(a.y, vp->Pos.y + 2.0f, vp->Pos.y + vp->Size.y - s - 2.0f);
    const ImVec2 b(a.x + s, a.y + s);
    // On the foreground list, drawn last: over the neighbouring tiles and every other window (#86: in the window's
    // own list, tiles in later child windows covered it).
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    dl->PushClipRect(vp->Pos, ImVec2(vp->Pos.x + vp->Size.x, vp->Pos.y + vp->Size.y), false);
    struct PopClip {
        ImDrawList* dl;
        ~PopClip() { dl->PopClipRect(); }
    } pop{dl};
    dl->AddRectFilled(ImVec2(a.x + 3, a.y + 5), ImVec2(b.x + 3, b.y + 5), IM_COL32(0, 0, 0, 90), theme::rounding(8.0f));
    dl->AddRectFilled(a, b, lookColor(IM_COL32(48, 48, 53, 255), theme::header), theme::rounding(6.0f));
    dl->AddImageRounded(g.tex, ImVec2(a.x + 3, a.y + 3), ImVec2(b.x - 3, b.y - 3), ImVec2(0, 0), ImVec2(1, 1),
                        IM_COL32_WHITE, theme::rounding(5.0f));
    dl->AddRect(a, b, lookColor(IM_COL32(240, 136, 46, 160), theme::accent, 0.63f), theme::rounding(6.0f), 0, 1.5f);
}

// ---------------------------------------------------------------------------
// Brushes
// ---------------------------------------------------------------------------

std::string BrushItem::id() const {
    switch (kind) {
    case Kind::Type:
        return std::string("type:") + brushName(BrushType(index));
    case Kind::Paint:
        return "paint";
    case Kind::Recipe:
        return "recipe:" + name;
    case Kind::File:
        return "file:" + path;
    case Kind::Guides:
        return "qremesh_guides";
    }
    return {};
}

std::string brushActionId(const BrushItem& item) {
    switch (item.kind) {
    case BrushItem::Kind::Type:
        return "brush." + lower(brushName(BrushType(item.index)));
    case BrushItem::Kind::Paint:
        return "brush.paint";
    case BrushItem::Kind::Recipe:
        return "brush.recipe." + item.name;
    case BrushItem::Kind::File:
        return "brush.file." + item.path;
    case BrushItem::Kind::Guides:
        return "brush.qremesh_guides";
    }
    return {};
}

const char* brushTypeDescription(int type) {
    if (type == kPaintBrush) return "Vertex Paint: paints the current colour onto the surface without sculpting (P).";
    switch (BrushType(type)) {
    case BrushType::Standard:
        return "Pushes the surface out along its normal, building soft, rounded form. Alt carves.";
    case BrushType::Clay:
        return "Builds up flat-bottomed layers of clay that fill dents first, for blocking in form.";
    case BrushType::Smooth:
        return "Relaxes the surface and evens out bumps. Hold Shift with any brush to smooth.";
    case BrushType::Move:
        return "Grabs the area under the brush and drags it with the cursor, for big shape changes.";
    case BrushType::Inflate:
        return "Swells the surface outwards in every direction, like blowing up a balloon.";
    case BrushType::Crease:
        return "Cuts a sharp groove (or ridge with Alt), pinching the sides together.";
    case BrushType::Flatten:
        return "Presses the surface onto a plane, levelling hills and valleys.";
    case BrushType::Pinch:
        return "Pulls the surface toward the stroke, sharpening edges and creases.";
    case BrushType::SharpCrease:
        return "A sharp V-shaped crease with the sides pinched in, for wrinkles and folds.";
    case BrushType::ClayBlock:
        return "Square, flat-topped clay strokes that stack up quickly, for bold blocking.";
    case BrushType::SnakeHook:
        return "Pulls the surface out along the stroke into horns, tentacles and spikes.";
    case BrushType::Trim:
        return "Trim: cuts away everything above a plane, for hard planar surfaces.";
    case BrushType::Layer:
        return "Raises the surface to one fixed height per stroke; overlapping passes never stack.";
    case BrushType::Nudge:
        return "Slides the surface sideways along the stroke without changing its height.";
    case BrushType::Vdm:
        return "Vector displacement: drag out one stamp of a whole 3D shape, overhangs and all (a horn, an ear, "
               "scales). Choose the map in Brush > Vector Displacement or the Alpha picker. Works best on dense meshes.";
    case BrushType::KelvinGrab:
        return "Elastic move from the physics of elasticity (Kelvinlets): the surface follows the cursor like real clay, "
               "fading out with no edge. Squash (Brush > Kelvinlet) makes it bulge like incompressible clay.";
    case BrushType::KelvinTwist:
        return "Elastic twist: press and drag right to turn the surface clockwise around that point (left turns it "
               "back); the turn fades smoothly, keeping the volume.";
    case BrushType::KelvinScale:
        return "Elastic pinch and swell: press and drag left to pinch the surface toward that point, right to swell it.";
    case BrushType::KelvinSnake:
        return "Elastic Tendril: pulls horns, tentacles and spikes that taper naturally as they stretch.";
    case BrushType::DetailClone:
        return "Paints copies of a captured detail along the stroke, each turned to follow it. Capture any detail on "
               "the model with Capture from view (Ctrl+Alt+C), or pick a VDM.";
    case BrushType::Morph:
        return "Pulls the surface back toward a stored shape: pick a selection set with Shape as the Morph Target in "
               "Tool > Selection Sets. Alt pushes away. Works at any subdivision level.";
    case BrushType::Cloth:
        return "Moves the surface like fabric: drag to pull it along and it folds where it bunches up; Push presses it "
               "in, Settle lets it sag and drape over other Parts. Mode and fabric in Brush > Cloth; the mask pins it.";
    default:
        return "";
    }
}

std::vector<BrushItem> PickerUi::brushItems(App& app, bool libraryFiles) {
    std::vector<BrushItem> items;
    for (int i = 0; i < int(BrushType::Count); ++i) {
        BrushItem b;
        b.kind = BrushItem::Kind::Type;
        b.index = i;
        b.name = brushLabel(BrushType(i));
        b.description = brushTypeDescription(i);
        items.push_back(std::move(b));
    }
    BrushItem paint;
    paint.kind = BrushItem::Kind::Paint;
    paint.name = "Paint";
    paint.description = brushTypeDescription(kPaintBrush);
    items.push_back(std::move(paint));
    BrushItem guides;
    guides.kind = BrushItem::Kind::Guides;
    guides.name = "Quad Remesh Guides";
    guides.description = "quad remesh Guides: draw curves on the model for Quad Remesh's edge loops to follow (a curve "
                         "that ends where it started makes a loop). Alt+click a curve to delete it.";
    items.push_back(std::move(guides));
    for (int i = 0; i < int(app.doc_.brushes.size()); ++i) {
        const BrushRecipe& r = app.doc_.brushes[size_t(i)];
        BrushItem b;
        b.kind = BrushItem::Kind::Recipe;
        b.index = i;
        b.name = r.name;
        b.path = r.origin.path;
        b.description = r.description;
        b.pack = r.origin.pack;
        switch (r.origin.source) {
        case BrushSource::Studio:
            b.tag = "studio";
            break;
        case BrushSource::Pack:
            b.tag = r.origin.pack;
            break;
        case BrushSource::Gallery:
            b.tag = "gallery";
            break;
        case BrushSource::Project:
            b.tag = "project";
            break;
        case BrushSource::Session:
            b.tag = "unsaved";
            break;
        default:
            break;
        }
        items.push_back(std::move(b));
    }
    if (libraryFiles && !app.library_.user.empty()) {
        // .sfbrush files in the Library library that aren't loaded yet.
        const std::string dir = app.library_.user + "/Brushes";
        if (const auto* files = app.libraryScanner_.get(dir, "brush", [](const std::string& p) {
                return isBrushFile(p) ? AssetKind::BrushFile : AssetKind::Folder;
            }))
            for (const AssetEntry& e : *files) {
                if (e.kind != AssetKind::BrushFile) continue;
                const bool loaded =
                    std::any_of(app.doc_.brushes.begin(), app.doc_.brushes.end(), [&](const BrushRecipe& r) {
                        return r.origin.path == e.path || typedName(r.name) == typedName(e.name);
                    });
                if (loaded) continue;
                BrushItem b;
                b.kind = BrushItem::Kind::File;
                b.name = e.name;
                b.path = e.path;
                b.description = "Brush file in your library: " + e.path;
                items.push_back(std::move(b));
            }
    }
    return items;
}

bool PickerUi::isActive(const App& app, const BrushItem& item) {
    switch (item.kind) {
    case BrushItem::Kind::Type:
        return !app.paintMode_ && !app.guidesMode_ && app.doc_.activeBrush < 0 && int(app.brush_) == item.index;
    case BrushItem::Kind::Paint:
        return app.paintMode_ && !app.guidesMode_;
    case BrushItem::Kind::Recipe:
        return !app.paintMode_ && !app.guidesMode_ && app.doc_.activeBrush == item.index;
    case BrushItem::Kind::File:
        return false;
    case BrushItem::Kind::Guides:
        return app.guidesMode_;
    }
    return false;
}

void PickerUi::select(App& app, const BrushItem& item) {
    if (item.kind != BrushItem::Kind::Guides) app.guidesMode_ = false;
    const std::string left = app.leaveDrawModes(); // Draw Tube and Append > Insert end, the same brush again too
    app.edgesMode_ = false; // picking any brush leaves Edges mode (PanelEdges.cpp)
    switch (item.kind) {
    case BrushItem::Kind::Type:
        app.brush_ = BrushType(item.index);
        app.doc_.activeBrush = -1;
        app.paintMode_ = false;
        app.status_ = std::string("Brush: ") + item.name;
        break;
    case BrushItem::Kind::Paint:
        app.paintMode_ = true;
        app.status_ = "Brush: Paint";
        break;
    case BrushItem::Kind::Recipe:
        if (item.index < 0 || item.index >= int(app.doc_.brushes.size())) return;
        app.doc_.activeBrush = item.index;
        app.paintMode_ = false;
        app.status_ = "Brush: " + item.name;
        break;
    case BrushItem::Kind::File:
        app.runCommand("brush_load", {{"path", item.path}});
        if (app.lastCommandOk_) app.paintMode_ = false;
        break;
    case BrushItem::Kind::Guides:
        app.setGuidesMode(true);
        break;
    }
    if (!left.empty()) app.status_ = "Left " + left + ". " + app.status_;
}

ImTextureID PickerUi::thumbnail(App& app, const BrushItem& item) {
    switch (item.kind) {
    case BrushItem::Kind::Type:
        return app.thumbs_.brush(item.index);
    case BrushItem::Kind::Paint:
        return app.thumbs_.brush(kPaintBrush);
    case BrushItem::Kind::Recipe:
        return item.index >= 0 && item.index < int(app.doc_.brushes.size())
                   ? app.thumbs_.recipe(app.doc_.brushes[size_t(item.index)])
                   : ImTextureID(0);
    case BrushItem::Kind::File:
    case BrushItem::Kind::Guides:
        return ImTextureID(0);
    }
    return ImTextureID(0);
}

void drawBrushGrid(App& app, GridMode mode) {
    State& s = st();
    const bool popup = mode == GridMode::Popup;
    const float tile = 64.0f * app.uiScale_;
    const std::vector<BrushItem> items = PickerUi::brushItems(app, true);

    if (!popup) {
        ImGui::SetNextItemWidth(-SortMenuWidth());
        ImGui::InputTextWithHint("##brushfilter", tr(ICON_LC_SEARCH "  Filter brushes"), paletteFilter,
                                 sizeof paletteFilter);
        ImGui::SameLine();
        SortMenu("brushsort", app.uiPrefs_.brushSort);
    }
    const std::string typed = popup ? s.typed : typedName(paletteFilter);
    std::vector<std::string> names;
    for (const auto& it : items)
        names.push_back(it.name);
    bool substring = false;
    std::vector<size_t> shown = filterByTyped(names, typed, &substring);
    // The chosen order (A-Z by default): the letter badges and Enter follow it.
    const ListSort sort = listSortFromId(app.uiPrefs_.brushSort);
    {
        std::vector<ListSortKey> keys;
        for (size_t i : shown)
            keys.push_back({items[i].name, items[i].pack, recentRank(s.recent, items[i].id()), builtinBrushPackRank(items[i].pack)});
        std::vector<size_t> sorted;
        for (size_t k : listSortOrder(keys, sort)) sorted.push_back(shown[k]);
        shown = std::move(sorted);
    }
    const auto badges = popup ? letterBadges(names, shown, typed) : std::vector<std::pair<char, size_t>>{};

    // Letter keys (pop-up only; the palette has a filter field instead).
    if (popup && ImGui::GetFrameCount() >= s.keysFrom) {
        for (int k = 0; k < 26; ++k) {
            if (!ImGui::IsKeyPressed(ImGuiKey(ImGuiKey_A + k), false)) continue;
            const char c = char('a' + k);
            if (auto b = std::find_if(badges.begin(), badges.end(), [c](const auto& x) { return x.first == c; });
                b != badges.end()) {
                const BrushItem pick = items[b->second];
                closePicker();
                PickerUi::select(app, pick);
                return;
            }
            s.typed += c;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Backspace) && !s.typed.empty()) s.typed.pop_back();
        if ((ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter)) && !shown.empty() &&
            !typed.empty()) {
            const BrushItem pick = items[shown.front()];
            closePicker();
            PickerUi::select(app, pick);
            return;
        }
    }

    if (popup) {
        // What was typed, or how to type.
        if (typed.empty()) {
            ImGui::TextDisabled("%s", tr("Type a letter to narrow the brushes, then press its orange key"));
            ImGui::SameLine();
            SortMenu("brushsortpop", app.uiPrefs_.brushSort);
        } else {
            std::string shownTyped = typed;
            for (auto& c : shownTyped)
                c = char(std::toupper(static_cast<unsigned char>(c)));
            ImGui::TextColored(ImVec4(0.94f, 0.53f, 0.18f, 1.0f), "%s", shownTyped.c_str());
            ImGui::SameLine();
            ImGui::TextDisabled("%s", tr(substring ? "(contains)  Backspace: undo a letter  Enter: first match"
                                          : "Backspace: undo a letter  Enter: first match"));
        }
    }

    // Letter index: A-Z chips with how many brushes start with each letter.
    {
        std::array<int, 26> counts{};
        for (const auto& it : items) {
            const std::string n = typedName(it.name);
            if (!n.empty() && n[0] >= 'a' && n[0] <= 'z') ++counts[size_t(n[0] - 'a')];
        }
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 1));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(3, 3));
        const float avail = ImGui::GetContentRegionAvail().x;
        float x = 0.0f;
        for (int k = 0; k < 26; ++k) {
            if (!counts[size_t(k)]) continue;
            char text[24];
            std::snprintf(text, sizeof text, "%c %d##chip", char('A' + k), counts[size_t(k)]);
            const float w = ImGui::CalcTextSize(text, std::strstr(text, "##")).x + 8.0f;
            if (x > 0.0f && x + w <= avail)
                ImGui::SameLine();
            else
                x = 0.0f;
            x += w + 3.0f;
            const bool on = !typed.empty() && typed[0] == char('a' + k);
            if (on) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::ColorConvertU32ToFloat4(kAccent()));
            if (ImGui::SmallButton(text)) {
                if (popup)
                    s.typed = std::string(1, char('a' + k));
                else
                    std::snprintf(paletteFilter, sizeof paletteFilter, "%c", char('a' + k));
            }
            if (on) ImGui::PopStyleColor();
            ui::HelpText("Letter index", nullptr,
                         "Brushes whose names start with this letter. Click to show only them.", "Brush");
        }
        ImGui::PopStyleVar(2);
    }

    const int columns = columnsFor(tile, mode);
    auto drawItem = [&](size_t i, int& column, const char* section) {
        const BrushItem& b = items[i];
        tileGrid(column, columns);
        char badge[2] = {0, 0};
        for (const auto& [c, idx] : badges)
            if (idx == i) badge[0] = char(std::toupper(static_cast<unsigned char>(c)));
        const std::string id = b.name + "##" + section + std::to_string(i);
        const ImTextureID tex = PickerUi::thumbnail(app, b);
        const char* glyph = b.kind == BrushItem::Kind::Paint    ? ICON_LC_PAINTBRUSH
                            : b.kind == BrushItem::Kind::Guides ? ICON_LC_SPLINE
                                                                : ICON_LC_BRUSH;
        const bool picked = AssetTile(id.c_str(), tex, glyph, b.name, tile, PickerUi::isActive(app, b),
                                      badge[0] ? badge : nullptr, b.kind == BrushItem::Kind::File);
        std::string footer = b.kind == BrushItem::Kind::Recipe
                                 ? brushOriginText(app.doc_.brushes[size_t(b.index)])
                             : b.kind == BrushItem::Kind::File   ? "Library file - pick to load it"
                             : b.kind == BrushItem::Kind::Guides ? "Tool > Geometry > Quad Remesh"
                                                                 : "Built-in brush";
        quickInfo(tex, glyph, b.name, b.description, footer);
        ui::HelpText(b.name.c_str(), nullptr, b.description.c_str(), "Brush");
        if (ImGui::BeginPopupContextItem()) {
            ImGui::TextDisabled("%s", b.name.c_str());
            ImGui::Separator();
            const std::string action = brushActionId(b);
            auto& shelf = app.layout_.bottomShelf;
            const bool onShelf = std::find(shelf.begin(), shelf.end(), action) != shelf.end();
            if (ImGui::MenuItem(tr("Add to bottom shelf"), nullptr, false, !onShelf && b.kind != BrushItem::Kind::File)) {
                shelf.insert(std::find(shelf.begin(), shelf.end(), "widget.add_brush"), action); // before [+]
                app.status_ = "Added " + b.name + " to the bottom shelf";
            }
            if (b.kind == BrushItem::Kind::Recipe) {
                // The same menu as the Brush palette's custom-brush list (PanelBrushLibrary.cpp).
                if (ImGui::MenuItem(tr("Copy brush code"))) app.copyBrushCode(b.index);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr("One line of text to share in chat, email or a forum"));
                if (ImGui::MenuItem(tr("Rename...##brush"))) {
                    app.brushPopup_ = App::BrushPopup::Rename;
                    app.brushPopupName_ = app.doc_.brushes[size_t(b.index)].name;
                    std::snprintf(app.brushRenameBuf_, sizeof app.brushRenameBuf_, "%s", app.brushPopupName_.c_str());
                }
                if (ImGui::MenuItem(tr("Save as .sfbrush..."))) {
                    app.doc_.activeBrush = b.index;
                    app.paintMode_ = false;
                    app.saveFileDialog(App::FileAction::SaveBrush);
                }
                const BrushRecipe& r = app.doc_.brushes[size_t(b.index)];
                if (ImGui::MenuItem(tr("Delete...##brush"), nullptr, false, isUserLibraryBrush(r))) {
                    app.brushPopup_ = App::BrushPopup::Delete;
                    app.brushPopupName_ = r.name;
                }
                if (ImGui::MenuItem(tr("Reveal in folder"), nullptr, false, !r.origin.path.empty()))
                    app.revealBrushFile(b.index);
            }
            ImGui::EndPopup();
        }
        if (picked) {
            if (popup && s.addToShelf) {
                auto& shelf = app.layout_.bottomShelf;
                const std::string action = brushActionId(b);
                if (std::find(shelf.begin(), shelf.end(), action) == shelf.end())
                    shelf.insert(std::find(shelf.begin(), shelf.end(), "widget.add_brush"), action); // before [+]
                app.status_ = "Added " + b.name + " to the bottom shelf";
            } else {
                PickerUi::select(app, b);
            }
            if (popup) closePicker();
        }
    };

    // Recent brushes first (the pop-up, before typing).
    if (popup && typed.empty() && !s.recent.empty()) {
        sectionHeader("Recent");
        int column = 0;
        for (const std::string& rid : s.recent)
            for (size_t i = 0; i < items.size(); ++i)
                if (items[i].id() == rid) {
                    drawItem(i, column, "recent");
                    break;
                }
    }
    // Sections: the built-in brushes, your own and other custom brushes, then the brush packs (one section per
    // pack when sorted by pack), then library files not loaded yet.
    struct Section {
        std::string title;
        std::function<bool(const BrushItem&)> match;
    };
    std::vector<Section> sections = {
        {"Built-in",
         [](const BrushItem& b) {
             return b.kind == BrushItem::Kind::Type || b.kind == BrushItem::Kind::Paint || b.kind == BrushItem::Kind::Guides;
         }},
        {"Custom", [](const BrushItem& b) { return b.kind == BrushItem::Kind::Recipe && b.pack.empty(); }},
    };
    if (sort == ListSort::Pack) {
        std::vector<std::string> packs;
        for (size_t i : shown)
            if (items[i].kind == BrushItem::Kind::Recipe && !items[i].pack.empty() &&
                std::find(packs.begin(), packs.end(), items[i].pack) == packs.end())
                packs.push_back(items[i].pack);
        for (const std::string& p : packs)
            sections.push_back({p, [p](const BrushItem& b) { return b.kind == BrushItem::Kind::Recipe && b.pack == p; }});
    } else {
        sections.push_back({"Brush packs", [](const BrushItem& b) { return b.kind == BrushItem::Kind::Recipe && !b.pack.empty(); }});
    }
    sections.push_back({"Library", [](const BrushItem& b) { return b.kind == BrushItem::Kind::File; }});
    if (popup) {
        // The grid scrolls inside the pop-up when there are many custom brushes.
        const float rowH = tile + ImGui::GetFontSize() + 4.0f + ImGui::GetStyle().ItemSpacing.y;
        ImGui::SetNextWindowSizeConstraints(
            ImVec2(0, 0), ImVec2(FLT_MAX, std::min(rowH * 6.5f, ImGui::GetMainViewport()->Size.y * 0.6f)));
        ImGui::BeginChild("##brushgrid", ImVec2(columns * (tile + 6.0f), 0), ImGuiChildFlags_AutoResizeY,
                          ImGuiWindowFlags_NoBackground);
    }
    for (const Section& sec : sections) {
        std::vector<size_t> in;
        for (size_t i : shown)
            if (sec.match(items[i])) in.push_back(i);
        if (in.empty()) continue;
        sectionHeader(sec.title.c_str());
        if (const std::string help = builtinBrushPackHelp(sec.title); !help.empty())
            ui::HelpText(sec.title.c_str(), nullptr, help.c_str(), "Brush packs");
        int column = 0;
        for (size_t i : in)
            drawItem(i, column, sec.title.c_str());
    }
    if (shown.empty()) ImGui::TextDisabled(tr("No brush matches \"%s\""), typed.c_str());
    if (popup) ImGui::EndChild();
    AssetTileHoverGlow();
}

// ---------------------------------------------------------------------------
// Stroke, alpha and material grids
// ---------------------------------------------------------------------------

void drawStrokeGrid(App& app, GridMode mode) {
    const float tile = 64.0f * app.uiScale_;
    static const char* const glyphs[] = {ICON_LC_ELLIPSIS, ICON_LC_SQUARE_DASHED_MOUSE_POINTER, ICON_LC_MOUSE_POINTER_2,
                                         ICON_LC_SPRAY_CAN, ICON_LC_SPLINE_POINTER};
    static const char* const help[] = {
        "Even dabs along the path: the classic brush stroke.",
        "Click and drag one stamp: the drag length sets its size and its direction the rotation (great with alphas).",
        "Click and drag one stamp of the brush's Size around the surface; it lands where you let go (great for VDMs).",
        "Scattered dabs of random size and strength, for organic texture.",
        "The brush trails the cursor on a string, for smooth, steady lines.",
    };
    int column = 0;
    const int columns = mode == GridMode::Popup ? 4 : columnsFor(tile, mode);
    for (int i = 0; i < int(StrokeType::Count); ++i) {
        tileGrid(column, columns);
        const std::string id = std::string(strokeTypeName(StrokeType(i))) + "##stroke";
        if (AssetTile(id.c_str(), ImTextureID(0), glyphs[i], strokeTypeName(StrokeType(i)), tile,
                      app.strokeType_ == StrokeType(i))) {
            app.strokeType_ = StrokeType(i);
            app.status_ = std::string("Stroke: ") + strokeTypeName(StrokeType(i));
        }
        quickInfo(ImTextureID(0), glyphs[i], strokeTypeName(StrokeType(i)), help[i]);
        ui::HelpText(strokeTypeName(StrokeType(i)), nullptr, help[i], "Stroke");
    }
    if (app.strokeType_ == StrokeType::Spray) {
        ImGui::SetNextItemWidth(-1);
        ImGui::SliderFloat("##scatter", &app.sprayScatter_, 0.0f, 2.0f, tr("Scatter %.2f"));
        ui::HelpText("Scatter", nullptr, "How far spray dabs land from the cursor, as a fraction of the brush size.",
                     "Stroke");
    }
    app.drawPrecisionOptions(); // Steady Stroke for every stroke type, Precision Intensity (Precision.cpp)
}

void drawVdmGrid(App& app, GridMode mode) {
    const float tile = 64.0f * app.uiScale_;
    const int columns = columnsFor(tile, mode);
    sectionHeader("Vector displacement");
    int column = 0;
    for (size_t i = 0; i < app.doc_.vdms.size(); ++i) {
        const std::shared_ptr<const VectorDisplacement> v = app.doc_.vdms[i];
        tileGrid(column, columns);
        const bool on = app.vdmBrushActive() && app.doc_.brushVdm == v;
        const ImTextureID tex = app.thumbs_.vdm(v);
        const std::string id = v->name + "##vdm" + std::to_string(i);
        if (AssetTile(id.c_str(), tex, ICON_LC_STAMP, v->name, tile, on)) {
            app.doc_.brushVdm = v;
            app.brush_ = BrushType::Vdm;
            app.doc_.activeBrush = -1;
            app.paintMode_ = false;
            app.status_ = "VDM: " + v->name;
            if (mode == GridMode::Popup) closePicker();
        }
        const std::string text = v->builtin ? "Built-in vector displacement map (CC0)." : "Vector displacement map from your library.";
        quickInfo(tex, ICON_LC_STAMP, v->name, text, v->path.empty() ? std::string() : v->path);
        ui::HelpText(v->name.c_str(), nullptr, "Stamp this vector displacement map with the VDM brush (Drag Stamp).",
                     "Brush > Vector Displacement");
        if (!v->builtin && ImGui::BeginPopupContextItem()) {
            ImGui::TextDisabled("%s", v->name.c_str());
            ImGui::Separator();
            if (ImGui::MenuItem(tr("Delete##vdm"))) app.runCommand("vdm_delete", {{"name", v->name}});
            ImGui::EndPopup();
        }
    }
    AssetTileHoverGlow();
}

void drawAlphaGrid(App& app, GridMode mode) {
    if (app.vdmBrushActive()) {
        drawVdmGrid(app, mode);
        ImGui::Dummy(ImVec2(0, 4));
    }
    State& s = st();
    const float tile = 64.0f * app.uiScale_;
    const BrushAlpha& current = app.doc_.brushAlpha;
    static char filter[64] = "";
    ImGui::SetNextItemWidth(mode == GridMode::Popup ? 200.0f * app.uiScale_ : -SortMenuWidth());
    ImGui::InputTextWithHint("##alphafilter", tr(ICON_LC_SEARCH "  Filter alphas"), filter, sizeof filter);
    ImGui::SameLine();
    SortMenu("alphasort", app.uiPrefs_.alphaSort);
    const int columns = columnsFor(tile, mode);

    // Every alpha in one list: the built-ins, the loaded image, the library's Alphas folder and the packs'.
    struct Entry {
        enum class Kind { Builtin, Current, File, Pack } kind;
        std::string name, help, path, pack, group;
        int builtin = -1;
    };
    std::vector<Entry> entries;
    for (int i = 0; i < int(BrushAlpha::builtins().size()); ++i) {
        const std::string& name = builtinAlpha(i).name;
        entries.push_back({Entry::Kind::Builtin, name, BrushAlpha::builtinHelp(name), "", "", "Built-in", i});
    }
    const bool custom = !current.empty() && !current.path.empty();
    if (custom)
        entries.push_back({Entry::Kind::Current, current.name, "The loaded image alpha (white = full effect).",
                           current.path, "", "Images"});
    if (!app.library_.user.empty()) {
        const std::string dir = app.library_.user + "/Alphas";
        if (const auto* list = app.libraryScanner_.get(dir, "alpha", [](const std::string& p) {
                return isImageFile(p) ? AssetKind::AlphaFile : AssetKind::Folder;
            }))
            for (const auto& e : *list)
                if (e.kind == AssetKind::AlphaFile && !(custom && lower(e.name) == lower(current.name)))
                    entries.push_back({Entry::Kind::File, e.name, "Image in your library's Alphas folder.", e.path, "",
                                       "Images"});
    }
    for (const PackContentItem& it : app.packAlphas()) // tagged with the pack's name
        entries.push_back({Entry::Kind::Pack, it.label(), "From the pack " + it.packName + ".", it.file, it.packName,
                           it.packName});

    const std::string f = lower(filter);
    std::vector<ListSortKey> keys;
    std::vector<size_t> kept;
    for (size_t i = 0; i < entries.size(); ++i) {
        if (!f.empty() && lower(entries[i].name).find(f) == std::string::npos) continue;
        kept.push_back(i);
        // Round is "no alpha": it stays first in every order (the pack order puts it with the built-ins).
        const Entry& e = entries[i];
        ListSortKey k{e.name, e.kind == Entry::Kind::Builtin ? std::string() : "\x01" + e.group,
                      recentRank(s.recentAlphas, lower(e.name))};
        if (e.builtin == 0) k.name = std::string(1, '\0');
        keys.push_back(std::move(k));
    }
    const ListSort sort = listSortFromId(app.uiPrefs_.alphaSort);
    std::vector<size_t> order;
    for (size_t k : listSortOrder(keys, sort)) order.push_back(kept[k]);
    if (sort == ListSort::Recent) { // Round first, then the recent ones
        const auto round = std::find_if(order.begin(), order.end(), [&](size_t i) { return entries[i].builtin == 0; });
        if (round != order.end()) std::rotate(order.begin(), round, round + 1);
    }

    int column = 0;
    std::string group = "\x02";
    if (sort != ListSort::Pack) sectionHeader("Alphas");
    for (size_t i : order) {
        const Entry& e = entries[i];
        if (sort == ListSort::Pack && e.group != group) {
            group = e.group;
            sectionHeader(group.c_str());
            column = 0;
        }
        tileGrid(column, columns);
        bool on = false;
        ImTextureID tex = ImTextureID(0);
        const char* glyph = ICON_LC_IMAGE;
        switch (e.kind) {
        case Entry::Kind::Builtin:
            on = (current.path.empty() && lower(current.name) == lower(e.name)) || (e.builtin == 0 && current.empty());
            tex = app.thumbs_.alpha(builtinAlpha(e.builtin));
            glyph = ICON_LC_CIRCLE_DASHED;
            break;
        case Entry::Kind::Current:
            on = true;
            tex = app.thumbs_.alpha(current);
            glyph = ICON_LC_CIRCLE_DASHED;
            break;
        default: tex = app.thumbs_.file(e.path); break;
        }
        const std::string id = e.name + "##alpha" + std::to_string(int(e.kind)) + e.path;
        if (AssetTile(id.c_str(), tex, glyph, e.name, tile, on)) {
            if (e.kind == Entry::Kind::Builtin) app.runCommand("brush_set_alpha", {{"name", e.name}});
            else if (e.kind != Entry::Kind::Current)
                app.runCommand("brush_set_alpha", {{"path", e.path}, {"invert", app.alphaInvert_}});
        }
        quickInfo(tex, glyph, e.name, e.help, e.path);
        ui::HelpText(e.name.c_str(), nullptr, e.help.c_str(), "Alpha");
    }
    if (order.empty()) ImGui::TextDisabled(tr("No alpha matches \"%s\""), filter);
    ImGui::Dummy(ImVec2(0, 2));
    if (ImGui::Button(tr(ICON_LC_FOLDER_OPEN "  Load image..."), ImVec2(mode == GridMode::Popup ? 0.0f : -1.0f, 0)))
        sf::dialogs::openFile(App::onAlphaChosen, &app, app.window_, kAlphaFilters, 2, nullptr, false);
    ui::HelpText("Load image", nullptr, "Use any grayscale image as the alpha: white = full effect, black = none.",
                 "Alpha");
    if (mode == GridMode::Popup) ImGui::SameLine();
    ImGui::Checkbox(tr("Invert loaded image"), &app.alphaInvert_);
    ui::HelpText("Invert loaded image", nullptr, "Load image alphas as their negative (black = full effect).", "Alpha");
    AssetTileHoverGlow();
}

void litsphereMenu(App& app, int index, const std::function<void()>& beforeRemove) {
    // One rename or new-folder field at a time, kept while the menu stays open.
    static int editing = -1;  // the Lit Sphere whose field shows
    static bool folderField = false; // the field is Move to folder > New folder... (else Rename)
    static bool focus = false;
    static char buffer[128] = {};
    static std::string error;
    if (ImGui::IsWindowAppearing()) editing = -1, error.clear();
    const std::string name = litsphereName(index); // a copy: a rename changes it
    ImGui::TextDisabled("%s", name.c_str());
    const CustomLitSphere* c = customLitSphere(index);
    if (c && !c->path.empty()) { // the file name (and its folder); the whole path is long
        const std::filesystem::path p(std::u8string(c->path.begin(), c->path.end()));
        const std::u8string file = (c->group.empty() ? p.filename() : p.parent_path().filename() / p.filename()).u8string();
        ImGui::TextDisabled("%s", reinterpret_cast<const char*>(file.c_str()));
    }
    ImGui::Separator();
    const std::string locked = app.litsphereLocked(index);
    const auto lockedTip = [&] {
        if (!locked.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s", locked.c_str());
    };
    // The inline field: Enter applies, Escape cancels; a refusal (a name clash...) shows under it and keeps it open.
    const auto field = [&](const char* id, bool forFolder) {
        if (editing != index || folderField != forFolder) return;
        if (focus) {
            ImGui::SetKeyboardFocusHere();
            focus = false;
        }
        ImGui::SetNextItemWidth(180.0f * app.uiScale_);
        if (ImGui::InputText(id, buffer, sizeof buffer, ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll)) {
            if (forFolder) app.runCommand("move_litsphere", {{"name", name}, {"folder", std::string(buffer)}});
            else app.runCommand("rename_litsphere", {{"name", name}, {"to", std::string(buffer)}});
            if (app.lastCommandOk_) {
                editing = -1;
                error.clear();
                ImGui::CloseCurrentPopup();
            } else {
                error = app.status_;
                focus = true;
            }
        }
        if (!error.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, theme::danger);
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 240.0f * app.uiScale_);
            ImGui::TextUnformatted(error.c_str());
            ImGui::PopTextWrapPos();
            ImGui::PopStyleColor();
        }
    };
    const auto openField = [&](bool forFolder) {
        editing = index;
        folderField = forFolder;
        focus = true;
        error.clear();
        std::snprintf(buffer, sizeof buffer, "%s", forFolder ? "" : name.c_str());
    };
    ImGui::BeginDisabled(!locked.empty());
    if (ImGui::Selectable(tr("Rename...##litsphere"), false, ImGuiSelectableFlags_NoAutoClosePopups)) openField(false);
    ImGui::EndDisabled();
    lockedTip();
    field("##litsphererename", false);
    ImGui::BeginDisabled(!locked.empty());
    const bool moveMenu = ImGui::BeginMenu(tr("Move to folder##litsphere"));
    ImGui::EndDisabled();
    lockedTip();
    if (moveMenu) {
        const std::string group = c ? c->group : std::string();
        if (ImGui::MenuItem(tr("Lit Spheres (no folder)##litspheremove"), nullptr, group.empty(), !group.empty()))
            app.runCommand("move_litsphere", {{"name", name}, {"folder", std::string()}});
        for (const std::string& sub : app.litsphereSubfolders())
            if (ImGui::MenuItem((sub + "##litspheremove").c_str(), nullptr, sub == group, sub != group))
                app.runCommand("move_litsphere", {{"name", name}, {"folder", sub}});
        ImGui::Separator();
        if (ImGui::Selectable(tr("New folder...##litsphere"), false, ImGuiSelectableFlags_NoAutoClosePopups)) openField(true);
        field("##litspherefolder", true);
        ImGui::EndMenu();
    }
    ImGui::Separator();
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) ImGui::CloseCurrentPopup(); // (also while typing: nothing applied)
    if (ImGui::MenuItem(tr("Remove##litsphere"))) {
        if (beforeRemove) beforeRemove();
        app.runCommand("remove_litsphere", {{"name", name}});
    }
}

void drawMaterialGrid(App& app, GridMode mode) {
    State& s = st();
    const bool popup = mode == GridMode::Popup;
    const float tile = 64.0f * app.uiScale_;
    const int columns = columnsFor(tile, mode);
    int column = 0;
    int hovered = -2;
    const int active = s.previewing ? s.savedLitSphere : app.renderSettings_.litsphere;
    app.materialListShown(); // just opened: pick up images added to the Lit Spheres folder meanwhile
    SortMenu("materialsort", app.uiPrefs_.materialSort, false);
    std::vector<int> list; // the Lit Spheres and Studio (-1), in the chosen order (A-Z by default)
    std::vector<ListSortKey> keys;
    for (int k = 0; k <= litsphereCount(); ++k) {
        const int i = k < litsphereCount() ? litsphereInListOrder(k) : -1;
        if (i >= 0 && !isLitSphereListed(i)) continue; // removed
        list.push_back(i);
        keys.push_back({i < 0 ? "Studio" : litsphereName(i), "", recentRank(s.recentMaterials, std::to_string(i))});
    }
    // The chosen order, then grouped (#124): ungrouped first, then each sub-folder of the Lit Spheres folder under its heading.
    std::vector<size_t> order = listSortOrder(keys, listSortFromId(app.uiPrefs_.materialSort));
    const std::vector<std::string> groups = litsphereGroups();
    const auto groupRank = [&](size_t o) {
        const CustomLitSphere* c = customLitSphere(list[o]);
        if (!c || c->group.empty()) return 0;
        return int(std::find(groups.begin(), groups.end(), c->group) - groups.begin()) + 1;
    };
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return groupRank(a) < groupRank(b); });
    int shownGroup = 0;
    for (size_t o : order) {
        const int i = list[o];
        if (const int g = groupRank(o); g != shownGroup) {
            shownGroup = g;
            sectionHeader(groups[size_t(g - 1)].c_str());
            column = 0;
        }
        tileGrid(column, columns);
        const char* name = i < 0 ? "Studio" : litsphereName(i);
        const ImTextureID tex = app.thumbs_.material(i);
        if (AssetTile((std::string(name) + "##mat").c_str(), tex, ICON_LC_SUN, name, tile, active == i)) {
            s.previewing = false;
            app.renderSettings_.litsphere = s.savedLitSphere = i; // runCommand below echoes it for scripts
            app.runCommand("set_material", {{"name", std::string(i < 0 ? "studio" : name)}});
            if (popup) closePicker();
        }
        if (ImGui::IsItemHovered()) hovered = i;
        ui::HelpText(name, nullptr,
                     i < 0                ? "Soft studio lighting with cavity shading, in the Base colour."
                     : i == defaultLitSphere() ? "The default: a matte warm clay with a clear key light and a soft "
                                              "rim, so strokes read without glare."
                     : i == 0 ? "Made for sculpting: a strong key light, crisp highlights and deep cavities so "
                                "every stroke reads."
                     : isCustomLitSphere(i) ? "Your own Lit Sphere. Right-click to rename it, move it to a folder or remove it."
                              : "Lit Sphere material: the whole lit look in one image.",
                     "Material");
        if (isCustomLitSphere(i) && ImGui::BeginPopupContextItem()) {
            litsphereMenu(app, i, [&] {
                if (popup && s.previewing) { // put the real material back before it can go
                    app.renderSettings_.litsphere = s.savedLitSphere;
                    s.previewing = false;
                }
            });
            ImGui::EndPopup();
        }
    }
    // the classic "Preview Material On Mesh": hovering a material shows it on the model until the pointer leaves.
    if (popup && st().open) {
        if (hovered >= -1) {
            if (!s.previewing) {
                s.previewing = true;
                s.savedLitSphere = app.renderSettings_.litsphere;
            }
            app.renderSettings_.litsphere = hovered;
        } else if (s.previewing) {
            app.renderSettings_.litsphere = s.savedLitSphere;
            s.previewing = false;
        }
    }
    AssetTileHoverGlow();
}

// ---------------------------------------------------------------------------
// The pop-ups
// ---------------------------------------------------------------------------

void openPicker(Picker picker, ImVec2 anchor) {
    State& s = st();
    // A click on the tile that opened it closes it (the click outside closed it this very frame).
    if ((s.open && s.picker == picker && s.openedFrame != ImGui::GetFrameCount()) ||
        (!s.open && s.picker == picker && s.closedFrame == ImGui::GetFrameCount())) {
        closePicker();
        return;
    }
    s.open = true;
    s.picker = picker;
    s.underMouse = anchor.x < 0.0f;
    s.anchor = s.underMouse ? ImGui::GetIO().MousePos : anchor;
    s.openedFrame = ImGui::GetFrameCount();
    // Opened by a key between frames: that key is still "pressed" in the next frame, so skip it.
    s.keysFrom = ImGui::GetFrameCount() + 2;
    s.typed.clear();
    s.addToShelf = false;
    const SDL_Keymod mods = SDL_GetModState();
    s.held = !(mods & (SDL_KMOD_CTRL | SDL_KMOD_GUI)) ? 0 : (mods & SDL_KMOD_SHIFT) ? 2 : 1;
    if (picker != Picker::Brush && picker != Picker::Stroke) s.held = 0;
}

void openBrushPickerForShelf(ImVec2 anchor) {
    openPicker(Picker::Brush, anchor);
    st().addToShelf = st().open;
    st().held = 0;
}

bool pickerOpen() { return st().open || st().slider >= 0; }

void PickerUi::sizePopup(App& app, Slider which) {
    State& s = st();
    s.slider = int(which);
    // Smooth's own Intensity when Smooth is the brush or Shift is held. Not Shift in the Blender style:
    // there Shift+F is the brush's Strength, as in Blender, and Shift has no smoothing meaning for a key.
    s.sliderSmooth = app.blenderStyle() ? app.brush_ == BrushType::Smooth : app.activeBrush() == BrushType::Smooth;
    // The brush cursor's own position (SDL's window coordinates, the brush ring's centre) rather than ImGui's
    // MousePos: on Windows ImGui's comes from the global cursor minus the window position while no button is held,
    // and pen drivers' stray mouse events move it too, so the slider opened away from the cursor (#9).
    s.sliderAt = ImVec2(app.mouseX_, app.mouseY_);
    s.sliderPlaced = false;
    s.sliderFrame = ImGui::GetFrameCount();
    s.sliderTyped.clear();
    s.sliderWasActive = false;
}

void drawPickers(App& app) {
    State& s = st();
    // Remember what was picked recently, from anywhere (picker, shelf, keys, scripts).
    {
        BrushItem active;
        if (app.guidesMode_)
            active.kind = BrushItem::Kind::Guides;
        else if (app.paintMode_)
            active.kind = BrushItem::Kind::Paint;
        else if (app.doc_.activeBrush >= 0 && app.doc_.activeBrush < int(app.doc_.brushes.size())) {
            active.kind = BrushItem::Kind::Recipe;
            active.name = app.doc_.brushes[size_t(app.doc_.activeBrush)].name;
        } else {
            active.kind = BrushItem::Kind::Type;
            active.index = int(app.brush_);
        }
        const std::string id = active.id();
        if (id != s.lastActive) {
            if (!s.lastActive.empty()) {
                s.recent.erase(std::remove(s.recent.begin(), s.recent.end(), id), s.recent.end());
                s.recent.insert(s.recent.begin(), id);
                if (s.recent.size() > 8) s.recent.resize(8);
            }
            s.lastActive = id;
        }
    }

    touchRecent(s.recentAlphas, s.lastAlpha, lower(app.doc_.brushAlpha.empty() ? std::string("round") : app.doc_.brushAlpha.name));
    if (!s.previewing) touchRecent(s.recentMaterials, s.lastMaterial, std::to_string(app.renderSettings_.litsphere));

    if (s.open) {
        // Pickers own the keyboard while open (letters choose brushes; Esc closes).
        ImGui::SetNextFrameWantCaptureKeyboard(true);
        if (ImGui::IsKeyPressed(ImGuiKey_Escape) && s.openedFrame != ImGui::GetFrameCount()) closePicker();
    }
    if (s.open) {
        const float scale = app.uiScale_;
        const float gridW = 6 * (64.0f * scale + 6.0f) + 20.0f + ImGui::GetStyle().ScrollbarSize;
        if (s.held != 0) { // Ctrl / Ctrl+Shift held: the mask or selection brushes (Brush and Stroke only)
            if (beginPickerWindow(tr(s.picker == Picker::Brush ? "Brushes##picker" : "Strokes##picker"),
                                  (s.held == 1 ? 3 : 4) * (64.0f * scale + 6.0f) + 20.0f)) { // Ctrl+Shift: 4 Cut shapes a row
                const bool strokes = s.picker == Picker::Stroke;
                pickerTitle(strokes ? ICON_LC_SPLINE : ICON_LC_BRUSH,
                            tr(s.held == 1 ? (strokes ? "Mask stroke (Ctrl)" : "Mask brushes (Ctrl)")
                                        : (strokes ? "Selection stroke (Ctrl+Shift)" : "Selection brushes (Ctrl+Shift)")),
                            nullptr);
                ImGui::Spacing();
                if (app.drawHeldBrushGrid(s.held == 1 ? App::HeldBrush::Mask : App::HeldBrush::Select, strokes))
                    closePicker();
            }
            ImGui::End();
        } else {
            switch (s.picker) {
            case Picker::Brush:
                if (beginPickerWindow(tr("Brushes##picker"), gridW)) {
                    pickerTitle(ICON_LC_BRUSH, tr(s.addToShelf ? "Add a brush to the shelf" : "Brushes"), "B");
                    drawBrushGrid(app, GridMode::Popup);
                }
                ImGui::End();
                break;
            case Picker::Stroke:
                if (beginPickerWindow(tr("Strokes##picker"), 4 * (64.0f * scale + 6.0f) + 20.0f)) {
                    pickerTitle(ICON_LC_SPLINE, tr("Stroke"), nullptr);
                    ImGui::Spacing();
                    drawStrokeGrid(app, GridMode::Popup);
                    // Continuous (#31) here too, beside the strokes, as in the Stroke palette (#43)
                    ImGui::Spacing();
                    ui::ActionButton(app, "stroke.continuous", ImVec2(-1.0f, 0.0f));
                }
                ImGui::End();
                break;
            case Picker::Alpha:
                if (beginPickerWindow(tr("Alphas##picker"), gridW)) {
                    pickerTitle(ICON_LC_CIRCLE_DASHED, tr("Alpha"), nullptr);
                    drawAlphaGrid(app, GridMode::Popup);
                }
                ImGui::End();
                break;
            case Picker::Material:
                if (beginPickerWindow(tr("Materials##picker"), gridW)) {
                    pickerTitle(ICON_LC_SUN, tr("Material"), "M");
                    ImGui::Spacing();
                    drawMaterialGrid(app, GridMode::Popup);
                }
                ImGui::End();
                break;
            }
        }
    }
    if (!s.open && s.previewing) { // closed without picking: put the material back
        app.renderSettings_.litsphere = s.savedLitSphere;
        s.previewing = false;
    }

    // Size (S), Intensity (U), Colour intensity (I) and Falloff (O) at the cursor.
    if (s.slider >= 0) {
        ImGui::SetNextFrameWantCaptureKeyboard(true);
        const auto which = PickerUi::Slider(s.slider);
        // The value as shown: Size in pixels, the intensities 0-100 and Falloff -100..100.
        struct Def {
            const char* name;
            const char* key;
            const char* help;
            float* value;
            float lo, hi, shownScale;
        };
        float* z = s.sliderSmooth ? &app.smoothStrength_ : &app.strength_;
        const Def d = which == PickerUi::Slider::DrawSize
                          ? Def{"Size", "S",
                                "Brush radius in screen pixels. Drag, or type a number and press Enter. [ and ] also "
                                "change it.",
                                &app.radiusPx_, kMinDrawSizePx, kMaxDrawSizePx, 1.0f}
                      : which == PickerUi::Slider::ZIntensity
                          ? Def{s.sliderSmooth ? "Smooth Intensity" : "Intensity", "U",
                                s.sliderSmooth ? "How strongly Smooth works (it keeps its own value: hold "
                                                 "Shift and press U to set it). Type a number and press Enter."
                                               : "How strongly the brush changes the surface (1-100). Drag, or type a "
                                                 "number and press Enter.",
                                z, 0.01f, 1.0f, 100.0f}
                      : which == PickerUi::Slider::RgbIntensity
                          ? Def{"Colour intensity", "I",
                                "How strongly painting colours the surface (1-100). Drag, or type a number and press "
                                "Enter.",
                                &app.rgbIntensity_, 0.01f, 1.0f, 100.0f}
                          : Def{"Falloff", "O",
                                "The brush falloff: below 0 a broad, hard tip, above 0 a soft point (-100..100). The "
                                "inner ring of the cursor shows it.",
                                &app.focalShift_, -1.0f, 1.0f, 100.0f};
        const float w = 200.0f * app.uiScale_;
        // As  the knob (the current value) opens right under the cursor, so pressing there drags it. The
        // first frame (hidden while the window sizes itself) measures where the knob is; the window then stays put.
        if (!s.sliderPlaced) s.sliderPos = ImVec2(s.sliderAt.x - w * 0.5f, s.sliderAt.y - ImGui::GetFrameHeight() * 0.5f);
        ImGui::SetNextWindowPos(s.sliderPos, ImGuiCond_Always);
        if (s.sliderFrame == ImGui::GetFrameCount()) ImGui::SetNextWindowFocus();
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, theme::rounding(6.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 8));
        ImGui::PushStyleColor(ImGuiCol_WindowBg, kPanelBg());
        bool close = false;
        if (ImGui::Begin((std::string(d.name) + "##cursor").c_str(), nullptr,
                         ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                             ImGuiWindowFlags_AlwaysAutoResize)) {
            const bool first = ImGui::GetFrameCount() < s.sliderFrame + 2; // the S / U / I / O key itself
            // Typing a number sets it (Enter applies, Esc cancels); dragging the slider works too.
            if (!first) {
                for (int k = 0; k <= 9; ++k)
                    if (ImGui::IsKeyPressed(ImGuiKey(ImGuiKey_0 + k), false) ||
                        ImGui::IsKeyPressed(ImGuiKey(ImGuiKey_Keypad0 + k), false))
                        s.sliderTyped += char('0' + k);
                if (ImGui::IsKeyPressed(ImGuiKey_Period, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadDecimal, false))
                    s.sliderTyped += '.';
                if ((ImGui::IsKeyPressed(ImGuiKey_Minus, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadSubtract, false)) &&
                    s.sliderTyped.empty() && d.lo < 0.0f)
                    s.sliderTyped += '-';
                if (ImGui::IsKeyPressed(ImGuiKey_Backspace) && !s.sliderTyped.empty()) s.sliderTyped.pop_back();
                if (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter)) {
                    if (!s.sliderTyped.empty()) {
                        float v = std::strtof(s.sliderTyped.c_str(), nullptr);
                        // Intensities: 0-100 (a fraction up to 1 still works).
                        if (which == PickerUi::Slider::ZIntensity || which == PickerUi::Slider::RgbIntensity)
                            v = v > 1.0f ? v / 100.0f : v;
                        else if (which == PickerUi::Slider::FocalShift)
                            v /= 100.0f;
                        *d.value = std::clamp(v, d.lo, d.hi);
                    }
                    close = true;
                }
                if (ImGui::IsKeyPressed(ImGuiKey_Escape)) close = true;
            }
            ImGui::SetNextItemWidth(w);
            char fmt[64];
            if (!s.sliderTyped.empty())
                std::snprintf(fmt, sizeof fmt, "%s %s_", d.name, s.sliderTyped.c_str());
            else if (which == PickerUi::Slider::DrawSize)
                std::snprintf(fmt, sizeof fmt, "%s", app.drawSizeLabel().c_str()); // (px and mm, Precision.cpp)
            else
                std::snprintf(fmt, sizeof fmt, "%s %%.0f", d.name);
            if (which == PickerUi::Slider::DrawSize) {
                ImGui::SliderFloat("##cursorvalue", d.value, d.lo, d.hi, fmt, ImGuiSliderFlags_Logarithmic);
            } else {
                float shown = *d.value * d.shownScale;
                if (ImGui::SliderFloat("##cursorvalue", &shown, d.lo * d.shownScale, d.hi * d.shownScale, fmt))
                    *d.value = std::clamp(shown / d.shownScale, d.lo, d.hi);
            }
            ImGui::LogText("%s %.0f", d.name, double(*d.value * d.shownScale));
            {
                // The knob's centre as ImGui's slider draws it (2 px padding, GrabMinSize wide; Size is logarithmic).
                const ImVec2 lo = ImGui::GetItemRectMin(), hi = ImGui::GetItemRectMax();
                const float grab = std::min(ImGui::GetStyle().GrabMinSize, hi.x - lo.x - 4.0f);
                const float v = std::clamp(*d.value, d.lo, d.hi);
                const float t = which == PickerUi::Slider::DrawSize ? std::log(v / d.lo) / std::log(d.hi / d.lo)
                                                                    : (v - d.lo) / (d.hi - d.lo);
                s.sliderKnob = ImVec2(lo.x + 2.0f + grab * 0.5f + t * (hi.x - lo.x - 4.0f - grab), (lo.y + hi.y) * 0.5f);
                if (!s.sliderPlaced) {
                    const ImVec2 win = ImGui::GetWindowPos(), size = ImGui::GetWindowSize(), view = ImGui::GetIO().DisplaySize;
                    s.sliderPos = ImVec2(std::clamp(s.sliderAt.x - (s.sliderKnob.x - win.x), 0.0f, std::max(0.0f, view.x - size.x)),
                                         std::clamp(s.sliderAt.y - (s.sliderKnob.y - win.y), 0.0f, std::max(0.0f, view.y - size.y)));
                    s.sliderPlaced = true;
                }
            }
            ui::HelpText(d.name, d.key, d.help, "Draw");
            const bool dragging = ImGui::IsItemActive();
            if (dragging)
                s.sliderWasActive = true;
            else if (s.sliderWasActive)
                close = true; // closes on release, as 
            // A click elsewhere closes it. (The press that grabs the knob is not one: ImGui reports the window as not
            // hovered while its own slider is active, and that used to close the slider as soon as it was dragged, #9.)
            const bool clicked =
                ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Right);
            if (clicked && !first && !dragging && !ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem))
                close = true;
        }
        ImGui::End();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(2);
        if (close) s.slider = -1;
    }
}

bool PickerUi::sliderPreview(const App& app, ImVec2& at, Slider& which, float& value) {
    const State& s = st();
    if (s.slider != int(Slider::DrawSize) && s.slider != int(Slider::FocalShift)) return false;
    which = Slider(s.slider);
    at = s.sliderAt;
    value = which == Slider::DrawSize ? app.radiusPx_ : app.focalShift_;
    if (!s.sliderTyped.empty() && s.sliderTyped != "-" && s.sliderTyped != ".") {
        const float v = std::strtof(s.sliderTyped.c_str(), nullptr); // as Enter would apply it
        value = which == Slider::DrawSize ? std::clamp(v, kMinDrawSizePx, kMaxDrawSizePx) : std::clamp(v / 100.0f, -1.0f, 1.0f);
    }
    return true;
}

bool PickerUi::sliderKnob(ImVec2& knob) {
    const State& s = st();
    if (s.slider < 0 || !s.sliderPlaced) return false;
    knob = s.sliderKnob;
    return true;
}

// ---------------------------------------------------------------------------
// The left tray's colour field: a saturation/value square, a slim hue strip and the main/second swatches. Drawn
// by hand rather than with ImGui::ColorPicker3 so its markers and corners match the rest of the shell.

namespace {

// Fills a rounded rect, then gives its vertices a linear gradient from `c0` at `p0` to `c1` at `p1` (colour and alpha;
// the anti-aliased fringe stays transparent).
void gradientRect(ImDrawList* dl, ImVec2 a, ImVec2 b, ImVec2 p0, ImVec2 p1, ImU32 c0, ImU32 c1, float rounding,
                  ImDrawFlags flags) {
    const int start = dl->VtxBuffer.Size;
    dl->AddRectFilled(a, b, IM_COL32_WHITE, rounding, flags);
    const ImVec2 d(p1.x - p0.x, p1.y - p0.y);
    const float len2 = std::max(d.x * d.x + d.y * d.y, 1e-6f);
    const ImVec4 f0 = ImGui::ColorConvertU32ToFloat4(c0), f1 = ImGui::ColorConvertU32ToFloat4(c1);
    for (int i = start; i < dl->VtxBuffer.Size; ++i) {
        ImDrawVert& v = dl->VtxBuffer[i];
        const float t = std::clamp(((v.pos.x - p0.x) * d.x + (v.pos.y - p0.y) * d.y) / len2, 0.0f, 1.0f);
        const float fringe = float((v.col >> IM_COL32_A_SHIFT) & 0xff) / 255.0f; // 0 on the anti-aliased edge
        v.col = ImGui::ColorConvertFloat4ToU32(ImVec4(f0.x + (f1.x - f0.x) * t, f0.y + (f1.y - f0.y) * t,
                                                      f0.z + (f1.z - f0.z) * t, (f0.w + (f1.w - f0.w) * t) * fringe));
    }
}

// The SV square's marker: a light ring with a dark outline inside and out, readable on any colour.
void ringMarker(ImDrawList* dl, ImVec2 c, float r, float s) {
    dl->AddCircle(c, r + 1.0f * s, kMarkerDark, 20, 1.0f * s);
    dl->AddCircle(c, r, kMarkerLight, 20, 1.5f * s);
    dl->AddCircle(c, r - 1.25f * s, kMarkerDark, 20, 0.75f * s);
}

// A colour swatch with a 1 px edge and an inner hairline.
void swatch(ImDrawList* dl, ImVec2 a, ImVec2 b, ImVec4 colour, float rounding, float s, bool hovered) {
    dl->AddRectFilled(a, b, ImGui::ColorConvertFloat4ToU32(colour), rounding);
    dl->AddRect(ImVec2(a.x + 1.0f * s, a.y + 1.0f * s), ImVec2(b.x - 1.0f * s, b.y - 1.0f * s),
                hovered ? kFieldHover : kFieldHairline, std::max(0.0f, rounding - 1.0f * s), 0, 1.0f * s);
    dl->AddRect(a, b, kFieldEdge, rounding, 0, 1.0f * s);
}

} // namespace

void App::drawColorWidget(float width) {
    const float s = uiScale_;
    const float rounding = 3.0f * s;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImGui::BeginGroup(); // the whole widget, one item for the shelf
    ImGui::BeginGroup(); // the field, one item for its help
    ImGui::PushID("##shelfcolor");
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));

    // Hue and saturation are kept between frames: a grey or black has none of its own, and dragging through one must
    // not lose the hue the user chose.
    glm::vec3 srgb = linearToSrgb(doc_.paintColor);
    ImGuiStorage* store = ImGui::GetStateStorage();
    const ImGuiID hueKey = ImGui::GetID("hue.kept"), satKey = ImGui::GetID("sat.kept");
    float h = 0.0f, sat = 0.0f, val = 0.0f;
    ImGui::ColorConvertRGBtoHSV(srgb.x, srgb.y, srgb.z, h, sat, val);
    if (sat <= 1e-4f || val <= 1e-4f) h = store->GetFloat(hueKey, h);
    if (val <= 1e-4f) sat = store->GetFloat(satKey, sat);

    const float stripW = kHueStripW * s, gap = kFieldGap * s;
    const float squareW = width - stripW - gap, squareH = squareW;
    bool changed = false;

    // Saturation across, value down.
    const ImVec2 sq = ImGui::GetCursorScreenPos();
    const ImVec2 sqMax(sq.x + squareW, sq.y + squareH);
    ImGui::InvisibleButton("sv", ImVec2(squareW, squareH));
    if (ImGui::IsItemActive()) {
        const ImVec2 m = ImGui::GetIO().MousePos;
        sat = std::clamp((m.x - sq.x) / squareW, 0.0f, 1.0f);
        val = 1.0f - std::clamp((m.y - sq.y) / squareH, 0.0f, 1.0f);
        changed = true;
    }
    if (ImGui::IsItemHovered() || ImGui::IsItemActive()) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    ImGui::SameLine(0.0f, gap * 0.5f);

    // Hue, red at the top. Its hit area takes the half of the gap beside it too.
    const ImVec2 hs(ImGui::GetCursorScreenPos().x + gap * 0.5f, ImGui::GetCursorScreenPos().y);
    const ImVec2 hsMax(hs.x + stripW, hs.y + squareH);
    ImGui::InvisibleButton("hue", ImVec2(stripW + gap * 0.5f, squareH));
    if (ImGui::IsItemActive()) {
        h = std::clamp((ImGui::GetIO().MousePos.y - hs.y) / squareH, 0.0f, 0.9999f);
        changed = true;
    }
    if (ImGui::IsItemHovered() || ImGui::IsItemActive()) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

    if (changed) {
        store->SetFloat(hueKey, h);
        store->SetFloat(satKey, sat);
        ImGui::ColorConvertHSVtoRGB(h, sat, val, srgb.x, srgb.y, srgb.z);
        doc_.paintColor = srgbToLinear(srgb);
    }

    // The square: white to the pure hue across, then clear to black down.
    float hr = 0, hg = 0, hb = 0;
    ImGui::ColorConvertHSVtoRGB(h, 1.0f, 1.0f, hr, hg, hb);
    gradientRect(dl, sq, sqMax, sq, ImVec2(sqMax.x, sq.y), IM_COL32_WHITE, ImGui::ColorConvertFloat4ToU32(ImVec4(hr, hg, hb, 1)),
                 rounding, ImDrawFlags_RoundCornersAll);
    gradientRect(dl, sq, sqMax, sq, ImVec2(sq.x, sqMax.y), IM_COL32(0, 0, 0, 0), IM_COL32_BLACK, rounding,
                 ImDrawFlags_RoundCornersAll);
    dl->AddRect(sq, sqMax, kFieldEdge, rounding, 0, 1.0f * s);
    ringMarker(dl, ImVec2(sq.x + sat * squareW, sq.y + (1.0f - val) * squareH), 4.0f * s, s);

    // The strip: six bands, the end ones rounded, and a thin bar across it that stays inside its ends.
    const float band = squareH / 6.0f;
    for (int i = 0; i < 6; ++i) {
        float r0, g0, b0, r1, g1, b1;
        ImGui::ColorConvertHSVtoRGB(float(i) / 6.0f, 1, 1, r0, g0, b0);
        ImGui::ColorConvertHSVtoRGB(float(i + 1) / 6.0f, 1, 1, r1, g1, b1);
        const ImVec2 a(hs.x, hs.y + band * float(i)), b(hsMax.x, i == 5 ? hsMax.y : hs.y + band * float(i + 1));
        gradientRect(dl, a, b, a, ImVec2(a.x, b.y), ImGui::ColorConvertFloat4ToU32(ImVec4(r0, g0, b0, 1)),
                     ImGui::ColorConvertFloat4ToU32(ImVec4(r1, g1, b1, 1)), stripW * 0.5f,
                     i == 0 ? ImDrawFlags_RoundCornersTop : i == 5 ? ImDrawFlags_RoundCornersBottom : ImDrawFlags_RoundCornersNone);
    }
    dl->AddRect(hs, hsMax, kFieldEdge, theme::rounding(stripW * 0.5f), 0, 1.0f * s);
    const float ny = std::clamp(hs.y + h * squareH, hs.y + 2.5f * s, hsMax.y - 2.5f * s);
    const ImVec2 na(hs.x - 1.0f * s, ny - 1.5f * s), nb(hsMax.x + 1.0f * s, ny + 1.5f * s);
    dl->AddRect(ImVec2(na.x - 1.0f * s, na.y - 1.0f * s), ImVec2(nb.x + 1.0f * s, nb.y + 1.0f * s), kMarkerDark, theme::rounding(2.0f * s), 0,
                1.0f * s);
    dl->AddRectFilled(na, nb, kMarkerLight, theme::rounding(1.5f * s));

    ImGui::PopStyleVar();
    ImGui::PopID();
    ImGui::EndGroup();
    ui::HelpText("Colour", "C", "The vertex paint colour (C over the model picks one).", "Left shelf");

    // Two swatches main and secondary colour: clicking either swaps them ("Alternate"). Equal halves of
    // the column, main on the left, the same gap as between the square and the strip.
    const float swH = kSwatchH * s;
    const float mainW = std::floor((width - gap) * 0.5f), secondW = width - gap - mainW;
    const glm::vec3 second = linearToSrgb(secondaryColor_);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(gap, gap));
    const ImVec2 pa = ImGui::GetCursorScreenPos();
    bool swap = ImGui::InvisibleButton("##primary", ImVec2(mainW, swH));
    swatch(dl, pa, ImVec2(pa.x + mainW, pa.y + swH), ImVec4(srgb.x, srgb.y, srgb.z, 1.0f), rounding, s, ImGui::IsItemHovered());
    ui::HelpText("Main colour", nullptr, "Click to swap with the second colour.", "Left shelf");
    ImGui::SameLine();
    const ImVec2 pb = ImGui::GetCursorScreenPos();
    swap |= ImGui::InvisibleButton("##secondary", ImVec2(secondW, swH));
    swatch(dl, pb, ImVec2(pb.x + secondW, pb.y + swH), ImVec4(second.x, second.y, second.z, 1.0f),
           rounding, s, ImGui::IsItemHovered());
    ui::HelpText("Second colour", nullptr, "Click to swap it with the main colour.", "Left shelf");
    if (swap) std::swap(doc_.paintColor, secondaryColor_);
    // The recent colours (Oslen on Discord): the last ones painted with, newest first, in rows of four (a second row
    // once there are more than four) across the column's width. Click one to paint with it.
    if (!recentColors_.empty()) {
        constexpr int kCols = 4;
        const float rw = std::floor((width - gap * float(kCols - 1)) / float(kCols)), rh = kRecentH * s;
        for (size_t i = 0; i < recentColors_.size(); ++i) {
            if (i % kCols != 0) ImGui::SameLine();
            const ImVec2 ra = ImGui::GetCursorScreenPos();
            const std::string id = "##recent" + std::to_string(i);
            const bool use = ImGui::InvisibleButton(id.c_str(), ImVec2(rw, rh));
            const glm::vec3 c = linearToSrgb(recentColors_[i]);
            swatch(dl, ra, ImVec2(ra.x + rw, ra.y + rh), ImVec4(c.x, c.y, c.z, 1.0f), theme::rounding(2.0f * s), s,
                   ImGui::IsItemHovered());
            ui::HelpText("Recent colour", nullptr, "A colour you painted with lately: click to paint with it again.", "Left shelf");
            if (use) doc_.paintColor = recentColors_[i];
        }
    }
    ui::ActionButton(*this, "color.fill", ImVec2(width, 0), ui::Look::IconLabel);
    ImGui::PopStyleVar();
    ui::HelpAction(*this, "color.fill");
    ImGui::EndGroup();
}

void resetPickerGlobals() {
    glow() = {};
    st() = {};
    paletteFilter[0] = 0;
}

} // namespace sf
