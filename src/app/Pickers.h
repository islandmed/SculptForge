// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// classic pop-up pickers: brush grid (B; type letters to choose), stroke,
// alpha and material (M) grids, and the Size / Strength sliders at the
// cursor (S / I). Also the thumbnail tile every grid and the Library draw.
#pragma once

#include <imgui.h>

#include <functional>
#include <string>
#include <vector>

namespace sf {

class App;

enum class Picker { Brush, Stroke, Alpha, Material };
enum class GridMode { Popup, Palette };

// Opens a picker with its top-left at `anchor` (beside a shelf tile). A negative anchor.x opens it
// under the mouse, as the keyboard shortcuts do.
void openPicker(Picker picker, ImVec2 anchor);
// The brush picker in "add to bottom shelf" mode (the shelf's [+] tile): picking adds instead of selecting.
void openBrushPickerForShelf(ImVec2 anchor);
bool pickerOpen(); // any picker or size/strength pop-up is showing
void drawPickers(App& app);
// The brush grid, reused by the Brush palette.
void drawBrushGrid(App& app, GridMode mode);
// The alpha and material grids, reused by the Alpha and Material palettes.
void drawAlphaGrid(App& app, GridMode mode);
void drawMaterialGrid(App& app, GridMode mode);
void drawStrokeGrid(App& app, GridMode mode);
// The right-click menu of a custom Lit Sphere (#124), inside an open pop-up: Rename..., Move to folder, Remove.
// `beforeRemove` runs just before it is removed (the picker puts its hover preview back).
void litsphereMenu(App& app, int index, const std::function<void()>& beforeRemove = {});
// The VDM brush's maps (built-in and library), reused by Brush > Vector Displacement; picking one makes the VDM
// brush the brush. The Alpha picker shows them first while the VDM brush is the brush.
void drawVdmGrid(App& app, GridMode mode);

// One brush of the picker, the Library Brush tab and the shelves.
struct BrushItem {
    enum class Kind { Type, Paint, Recipe, File, Guides } kind = Kind::Type; // Guides: QRemesh Guides (PanelRetopo.cpp)
    int index = -1;   // BrushType (Type) or doc.brushes index (Recipe)
    std::string name; // shown under the tile
    std::string path; // .sfbrush (File)
    std::string description;
    std::string tag;        // "gallery", "studio", "unsaved"... (Recipe)
    std::string pack;       // the brush pack it belongs to (a built-in or installed pack's recipe), or ""
    std::string id() const; // stable across frames: "type:Clay", "recipe:Dragon Scales", "file:<path>"
};

// Shared by the pickers, the Library and (through ui ids) the shelves; they read App internals.
struct PickerUi {
    static std::vector<BrushItem> brushItems(App& app, bool libraryFiles);
    static bool isActive(const App& app, const BrushItem& item);
    static void select(App& app, const BrushItem& item);
    static ImTextureID thumbnail(App& app, const BrushItem& item);
    // Opens a slider under the mouse (S / U / I / O): Size, Intensity (Smooth's own while Shift is
    // held or Smooth is the brush), Colour intensity or Falloff.
    enum class Slider { DrawSize, ZIntensity, RgbIntensity, FocalShift };
    static void sizePopup(App& app, Slider which);
    // The open slider's knob (its current value) in window coordinates, as last drawn; false when none is open.
    static bool sliderKnob(ImVec2& knob);
    // While Size (S) or Falloff (O) is open (#138): where it opened (window coordinates), which, and the value it is
    // setting: the number being typed (not applied before Enter), else the current one. Size and Falloff show a ring;
    // Intensity (U, I) is a disc inside it, 0..1. False otherwise.
    // True when the open slider is following the pointer: it has moved sideways from where the key opened it (and the
    // knob was not grabbed or a number typed). A click then keeps the value and starts nothing (App::handleEvent).
    static bool sliderFollowing(const App& app);
    static bool sliderPreview(const App& app, ImVec2& at, Slider& which, float& value);
};

// The sort menu of an asset list: a small "A-Z v" button that opens A-Z, Recent and (with `packs`) Pack. `pref`
// holds the core/ListSort.h id and is saved with the preferences. Returns true when the order changed.
bool SortMenu(const char* id, std::string& pref, bool packs = true);
float SortMenuWidth(); // room to leave for it beside a filter field

// Where a brush (BrushItem::id) is in the recently used list (0 = last used), -1 when not in it; the place of a
// built-in brush pack in the packs' order, -1 for other packs.
int brushRecentRank(const std::string& id);
int builtinBrushPackRank(const std::string& pack);

// Action id of a brush for the shelves ("brush.clay", "brush.paint", "brush.recipe.<name>").
std::string brushActionId(const BrushItem& item);
const char* brushTypeDescription(int brushType); // BrushType, or kPaintBrush
// Where a custom brush comes from, in words ("From the community gallery, ..."; PanelBrushLibrary.cpp).
std::string brushOriginText(const struct BrushRecipe& recipe);

// A thumbnail tile: the image (or `glyph` on a dark square while it loads), the label under it,
// accent outline when selected. `id` must be unique in the window. Returns true on click; check
// ImGui::IsItemHovered() / IsMouseDoubleClicked() after it for more.
bool AssetTile(const char* id, ImTextureID texture, const char* glyph, const std::string& label, float thumbSize,
               bool selected, const char* badge = nullptr, bool dim = false);
// Draws the 1.5x hover "glow" of the tile under the mouse (call once the grid is drawn).
void AssetTileHoverGlow();

} // namespace sf
