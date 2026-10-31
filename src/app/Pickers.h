// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// ZBrush-style pop-up pickers: brush grid (B; type letters to choose), stroke,
// alpha and material (M) grids, and the Draw Size / Strength sliders at the
// cursor (S / I). Also the thumbnail tile every grid and the LightBox draw.
#pragma once

#include <imgui.h>

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
// The VDM brush's maps (built-in and library), reused by Brush > Vector Displacement; picking one makes the VDM
// brush the brush. The Alpha picker shows them first while the VDM brush is the brush.
void drawVdmGrid(App& app, GridMode mode);

// One brush of the picker, the LightBox Brush tab and the shelves.
struct BrushItem {
    enum class Kind { Type, Paint, Recipe, File, Guides } kind = Kind::Type; // Guides: QRemesh Guides (PanelRetopo.cpp)
    int index = -1;   // BrushType (Type) or doc.brushes index (Recipe)
    std::string name; // shown under the tile
    std::string path; // .sfbrush (File)
    std::string description;
    std::string tag;        // "gallery", "studio", "unsaved"... (Recipe)
    std::string id() const; // stable across frames: "type:Clay", "recipe:Dragon Scales", "file:<path>"
};

// Shared by the pickers, the LightBox and (through ui ids) the shelves; they read App internals.
struct PickerUi {
    static std::vector<BrushItem> brushItems(App& app, bool libraryFiles);
    static bool isActive(const App& app, const BrushItem& item);
    static void select(App& app, const BrushItem& item);
    static ImTextureID thumbnail(App& app, const BrushItem& item);
    // Opens a slider under the mouse (ZBrush S / U / I / O): Draw Size, Z Intensity (Smooth's own while Shift is
    // held or Smooth is the brush), RGB Intensity or Focal Shift.
    enum class Slider { DrawSize, ZIntensity, RgbIntensity, FocalShift };
    static void sizePopup(App& app, Slider which);
    // The open slider's knob (its current value) in window coordinates, as last drawn; false when none is open.
    static bool sliderKnob(ImVec2& knob);
};

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
