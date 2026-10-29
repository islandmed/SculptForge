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
// Ctrl-hover help for the last item.
void HelpText(const char* title, const char* shortcut, const char* description, const char* path = nullptr);
void HelpAction(App& app, const char* actionId);
// ZBrush-style slider with its label drawn inside the frame ("Draw Size 60"), a quiet fill and a thin neutral bar
// along the bottom (the accent only while dragging).
bool SliderInside(const char* id, float* v, float lo, float hi, const char* format, ImGuiSliderFlags flags = 0);

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

// Draws this frame's Ctrl-help box; call once, after every other window.
void DrawHelp(App& app);
// What the Ctrl-help box showed in the last frame ("" if none), for UI tests.
const std::string& HelpShown();

// Theme tokens shared by the help, palette and console (ui-spec section 10).
namespace color {
inline constexpr ImU32 accent = IM_COL32(0xf0, 0x88, 0x2e, 0xff);
inline constexpr ImU32 accentHover = IM_COL32(0xff, 0x9a, 0x45, 0xff);
inline constexpr ImU32 text = IM_COL32(0xd8, 0xd8, 0xd8, 0xff);
inline constexpr ImU32 textDim = IM_COL32(0x8c, 0x8c, 0x90, 0xff);
inline constexpr ImU32 danger = IM_COL32(0xe5, 0x60, 0x4a, 0xff);
inline constexpr ImU32 panelBg = IM_COL32(0x22, 0x22, 0x25, 0xff);
inline constexpr ImU32 shellBg = IM_COL32(0x1b, 0x1b, 0x1d, 0xff);
inline constexpr ImU32 header = IM_COL32(0x2b, 0x2b, 0x2f, 0xff);
inline constexpr ImU32 border = IM_COL32(0x11, 0x11, 0x13, 0xff);
} // namespace color

} // namespace sf::ui
