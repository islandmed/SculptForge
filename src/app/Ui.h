// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Widgets every panel uses, so Ctrl-hover help and UI customization come for free.
#pragma once

#include <imgui.h>

#include <initializer_list>
#include <string>
#include <string_view>

namespace sf {
class App;
}

namespace sf::ui {

enum class Look { Label, Icon, IconLabel, Tile };

// Runs the action on click; toggles draw with the accent when checked(); shows Ctrl-hover help;
// is a Ctrl+Alt drag source in customize mode. Ctrl+Alt+click (outside customize mode) assigns
// a shortcut, as in ZBrush. Returns true when the action ran.
bool ActionButton(App& app, const char* actionId, ImVec2 size = {}, Look look = Look::Label);
// How a toggle that is on looks (docs/ui-design.md §10). A mode action picks the current tool or mode of its
// group (Draw/Move/Scale/Rotate, Sculpt/Paint, a brush, MaskPen/MaskLasso...) and gets the accent fill; every
// other toggle is an option (Zadd, X, Pressure > Strength, Mask, Menus) and gets a raised grey with an accent
// underline, so the accent marks one thing per group.
bool IsModeAction(std::string_view actionId);
// One side of a secondary either/or pair (Zadd | Zsub): always drawn as segments, the chosen one a raised grey with a
// bright label and no underline, so it reads as a selector rather than as two on/off options.
bool IsChoiceAction(std::string_view actionId);
// The 2 px accent mark of an option that is on: along the bottom of the rect, or down its left side.
void DrawOnMark(ImDrawList* drawList, ImVec2 min, ImVec2 max, bool side = false);
// Adjacent toggle buttons that read as one control (Zadd | Zsub, X | Y | Z); width -1 fills the line.
void Segmented(App& app, std::initializer_list<const char*> actionIds, float width = -1.0f);
// ImGui::Checkbox with an outline round the box while it is unchecked, so an off box doesn't vanish on panelBg.
bool Checkbox(const char* label, bool* v);
// A one-icon button ("x", "-") with the glyph centred by its ink, not its advance and the icon font's baseline offset,
// which left Lucide's X low and to one side (#98). `id` is an ImGui id ("##lbclose").
bool GlyphButton(const char* id, const char* glyph, ImVec2 size);
// Draws a one-icon `glyph` centred by its ink in the rect a..b (a longer text is centred by its size).
void CentredGlyph(ImDrawList* drawList, ImFont* font, ImVec2 a, ImVec2 b, ImU32 col, const char* glyph);
// Ctrl-hover help for the last item.
void HelpText(const char* title, const char* shortcut, const char* description, const char* path = nullptr);
void HelpAction(App& app, const char* actionId);
// ZBrush-style slider with its label drawn inside the frame ("Draw Size 60"), a quiet fill and a thin neutral bar
// along the bottom (the accent only while dragging).
bool SliderInside(const char* id, float* v, float lo, float hi, const char* format, ImGuiSliderFlags flags = 0);

// Hover and keyboard focus (docs/ui-design.md §10): every labelled item gets the look's ring (an accent border,
// Phosphor's glow, Blueprint's cyan line, Classic's warm edge) that eases in over ~100 ms; tiles lift instead.
// Animations only keep the main loop drawing while they run (HoverAnimating); Reduce motion makes them instant
// and turns the lift's scaling off.
// Eased 0-1 for `id` heading to `on` (instant under Reduce motion). Items that were never on cost nothing.
float HoverAmount(ImGuiID id, bool on);
// The last item is hovered (or, last frame, was hovered: before the item is drawn) or has the keyboard focus.
bool HoveredOrFocused(ImGuiID id);
// The item `id` draws its own hover (tiles, the canvas): no ring for it this frame.
void NoHoverRing(ImGuiID id);
bool HoverAnimating();
// For UI tests: the rings and lifted tiles drawn in the last frame.
int HoverRingsShown();
int HoverLiftsShown();
bool ReduceMotion();
// A tile that lifts on hover: begin after its InvisibleButton (draws the shadow), draw the tile, then end (scales
// what was drawn by up to 1.05 about the centre and adds the glow ring). Draw-only: the layout never moves.
struct TileLift {
    ImDrawList* dl = nullptr;
    int vtx = 0;
    ImVec2 min, max;
    float t = 0.0f;
    bool clip = false;
};
TileLift BeginTileLift(ImVec2 min, ImVec2 max, float rounding = 4.0f);
void EndTileLift(const TileLift& lift, float rounding = 4.0f);
// Ends the lift when it goes out of scope (tiles drawn by several functions, or with early returns).
struct TileLiftScope {
    TileLift lift;
    ~TileLiftScope() { EndTileLift(lift); }
};
// Draws this frame's hover and focus rings; call once, after every window and before DrawHelp.
void DrawHover(App& app);
// From the ImGui item hooks (Demo.cpp): where this frame's candidates for a ring are.
void NoteItem(ImGuiContext* ctx, ImGuiID id, const ImVec2& min, const ImVec2& max);
void NoteItemLabel(ImGuiID id);

// Customize mode: shelves this payload ("SF_UIITEM") is dragged from and dropped on.
enum Shelf { ShelfNone = -1, ShelfTop, ShelfLeft, ShelfRight, ShelfBottom };
struct UiItemPayload {
    char id[96] = "";
    int fromShelf = ShelfNone;
    int fromIndex = -1;
};
inline constexpr const char* kUiItemPayload = "SF_UIITEM";
// Where the next ActionButton / UiItemDragSource sits (shelves call this before drawing an item).
void SetNextItemOrigin(int shelf, int index);
// Makes the last item a Ctrl+Alt drag source in customize mode (for widgets that aren't ActionButtons).
bool UiItemDragSource(App& app, const char* itemId, const char* label);

// Keyboard keycaps for a chord ("Ctrl+Shift+I", "F3 / Ctrl+Shift+P"), drawn inline as an item.
void Keycaps(const std::string& chord);
float KeycapsWidth(const std::string& chord);
float KeycapsAt(ImDrawList* drawList, ImVec2 pos, const std::string& chord); // no layout; returns the width
// A plain tooltip after the hover delay (icon-only buttons, clipped labels); suppressed while Ctrl is held.
void DelayedTooltip(const char* text);
bool CtrlHeld();
bool AltHeld();

// #100 (small screens, pens): a left-drag on a panel's empty space, or a middle-drag anywhere on it, scrolls it, as a
// finger scrolls a list. Call once a frame, after every window (before ImGui ends the frame).
void DragScrollPanels();
// For UI tests: drags that scrolled a panel so far, and that panel's scroll after the last one (pixels).
int DragScrolls();
float DragScrollY();

// Draws this frame's Ctrl-help box; call once, after every other window.
void DrawHelp(App& app);
// What the Ctrl-help box showed in the last frame ("" if none), for UI tests.
const std::string& HelpShown();

// Theme tokens shared by the help, palette and console (ui-spec section 10).
namespace color {
inline ImU32 accent = IM_COL32(0xf0, 0x88, 0x2e, 0xff);
inline ImU32 accentHover = IM_COL32(0xff, 0x9a, 0x45, 0xff);
inline ImU32 text = IM_COL32(0xd8, 0xd8, 0xd8, 0xff);
inline ImU32 textDim = IM_COL32(0x8c, 0x8c, 0x90, 0xff);
inline ImU32 danger = IM_COL32(0xe5, 0x60, 0x4a, 0xff);
inline ImU32 panelBg = IM_COL32(0x22, 0x22, 0x25, 0xff);
inline ImU32 shellBg = IM_COL32(0x1b, 0x1b, 0x1d, 0xff);
inline ImU32 header = IM_COL32(0x2b, 0x2b, 0x2f, 0xff);
inline ImU32 border = IM_COL32(0x11, 0x11, 0x13, 0xff);
} // namespace color

} // namespace sf::ui
