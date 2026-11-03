// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// UI preferences and the ZBrush-style configuration workflow: Store Config
// (Ctrl+Shift+I) writes <prefs>/ui.json with the layout, shortcut overrides and
// these preferences; nothing is saved automatically. Restore Standard UI goes
// back to the built-in layout, Restore Custom UI to the stored one, and Save UI /
// Load UI (Ctrl+L) share a layout as a file.
#pragma once

#include "app/LookSchemes.h"
#include "app/Theme.h"
#include "core/Json.h"
#include "core/SpaceMouse.h"
#include "core/Tablet.h"

#include <set>
#include <string>
#include <vector>

namespace sf {

class App;

struct UiPrefs {
    bool popupInfo = true;      // Ctrl+hover shows an item's description (ZBrush "Popup Info")
    float helpOpacity = 0.97f;  // Ctrl-help box opacity (ZBrush "Quick Info Opacity")
    float tooltipDelay = 0.5f;  // plain hover tooltips on icon-only buttons and clipped labels
    bool reduceMotion = false;  // hover highlights change colour only: no easing, no lifting tiles (Ui.h)
    bool oneSubpalette = true;  // opening a sub-palette closes the others (Shift+click opens more)
    bool autoCollapseLeft = true, autoCollapseRight = false; // trays collapse palettes that would overflow
    float uiScale = 0.0f;       // 0 = follow the display; otherwise applied at the next start
    bool lightboxAtStartup = true;
    std::string libraryFolder;  // "" = Documents/SculptForge
    bool alphabeticalPalettes = false; // strict A-Z palette bar like ZBrush (default: File, Edit first)
    std::string language = "en"; // Preferences > Interface: the interface language, "en" or "es" (app/I18n.h, #44)
    int cameraNotch = 0;        // keep the UI out of a laptop's camera notch: 0 automatic, 1 always, 2 never
    bool turntable = false;     // rotation: false = free, about the screen axes (ZBrush); true = turntable (Y up)
    int inputStyle = 1;         // Preferences > Input: 0 ZBrush-style, 1 SculptForge (the default for new installs:
                                // 1-0 brushes, I = Intensity...), 2 Blender (middle-drag orbit, numpad views,
                                // Blender's sculpt keys), 3 Maya (Input.cpp). The first-run welcome asks.
    bool inputStyleSaved = false; // ui.json named the style (not saved itself): an install from before 0.10.4
                                  // without it had the ZBrush-style keys and keeps them (PanelProject.cpp)
    bool emulateNumpad = false; // Blender style: the 1-9 and 0 keys above the letters act as the numpad (laptops)
    bool rightClickQuickMenu = true; // a right click on the canvas (no drag) opens the QuickMenu, as in ZBrush (#22)
    bool spaceCamera = false; // hold Space and drag to move the camera (#43; off: Space opens the QuickMenu)
    // Blender style (#77): off, as Blender 5.1+'s default keymap, Alt+left-drag on the model masks (Ctrl+Alt unmasks)
    // and turns the view only beside the model; on (Blender's Emulate 3 Button Mouse) it always turns the view.
    bool emulate3Button = false;
    // Zoom drags (#32): the Maya style zooms with a sideways drag (right = in), as Maya's own dolly; false makes it
    // vertical as the other styles. invertZoom turns every zoom drag around.
    bool mayaZoomHorizontal = true;
    bool invertZoom = false;
    // Along the normal (from the discussions): 0 = with the invert key (Alt; Ctrl in the Blender and Maya styles),
    // 1 = always, 2 = never. For Move, Tendril, Elastic Tendril and KelvinGrab.
    int alongNormal = 0;
    // Brush falloff options (from the discussions): Connected only, View falloff (App::connectedOnly_ / viewFalloff_).
    bool connectedOnly = false;
    bool viewFalloff = false;
    // Dynamic topology: how much Smooth remeshes, 0-1 (DyntopoSettings::smoothRemesh; 1 as before).
    float dyntopoSmoothRemesh = 1.0f;
    bool airStrokes = false;  // start strokes off the model (#62): a left-drag on the background sculpts once it
                              // reaches the surface instead of turning the view (off: it turns the view, ZBrush)
    bool showAxes = true;              // Draw > Camera > Axes: the X / Y / Z gizmo in the canvas corner (#31)
    int axisGizmoSize = 1;             // Draw > Camera > Gizmo size: 0 Small, 1 Medium, 2 Large (axisGizmoScale)
    bool showViewFigure = false;       // Draw > Camera > View Figure: a small shaded head that turns with the view
    bool cursorFollowsSurface = true; // the brush ring lies on the surface under the pointer, as in SculptGL (#25)
    bool hideRingWhileStroking = false; // Preferences > Cursor (#88): only the centre dot while a stroke runs
    // Preferences > Hints (U11): the notes SculptForge shows over the 3D view. Each has Don't show again, which
    // adds its id here; showHints off hides them all.
    bool showHints = true;
    std::set<std::string> hiddenHints;
    // Preferences > Interface > Timeline position (#63): the Sculpt Timeline strip along the top of the viewport
    // (true, the default: clear of the brush shelf below it) or its bottom.
    bool timelineTop = true;
    // Preferences > Interface > Look: the colours and style of the interface (Theme.h; new installs get Forge
    // Terminal with Tokyo Night), and the shell's layout: 0 the ZBrush-style layout (the alphabetical palette bar,
    // two-row top shelf), 1 the SculptForge layout (the default: task menus File, Edit, Sculpt, Paint, Mesh, View,
    // Render, Help, a status bar, one-row shelf). Remembered in project.json too, without Store Config.
    theme::LookSettings look;
    int shellLayout = 1;
    bool lookSaved = false; // ui.json named the look or layout (not saved itself); one from before 0.10.7 didn't
    // Preferences > Cursor > Viewport cursor (#73): the system cursor over the 3D view, 0 = crosshair (the
    // default, as most sculpting apps), 1 = the arrow. Panels, menus and popups always show their own.
    int viewportCursor = 0;
    // Tool > Display > Backface culling (#68): skip the back faces of every Part (faster on dense meshes; see
    // through open meshes). Off by default; a preference, projects keep their own.
    bool backfaceCulling = false;
    TabletSettings tablet;      // Preferences > Tablet: pressure curve, eraser, pen buttons
    SpaceMouseSettings spaceMouse; // Preferences > Input > SpaceMouse
    // Preferences > Performance (PanelPerformance.cpp).
    int antialiasing = 4;       // samples per pixel in the 3D view: 1 (off), 2, 4 or 8
    bool fastNavigation = true; // big scenes draw without antialiasing while the view moves
    bool multithreading = true; // brushes, normals and BVH updates on every core (off: one thread)
    // Sculpt strokes solved on their own thread with a GPU preview of the dabs still to come, the cursor and the
    // stroke's head predicted a frame ahead (app/AsyncStroke.cpp). Off: the dabs are placed in the frame loop.
    bool predictiveStrokes = false; // experimental: not yet bit-identical to the serial path in every run
    // Draft strokes (with predictive strokes): dabs move the vertices only; the normals, cavities and culling
    // bounds of what they touched are worked out once, when the pen lifts (Sculptor::draftStrokes). Off by
    // default; meant for laptops and battery (a hardware tier will turn it on).
    bool draftStrokes = false;
    // Dynamic topology (Draw palette): false = the detail follows the brush size on screen (Sculptris Pro);
    // true = one detail size for the whole mesh.
    bool dyntopoConstantDetail = false;
    bool flatShading = false; // Material palette: the scene's shading at startup (false = Smooth, the default)
    // Preferences > Display: the view's background (#21), a gradient or a flat display (sRGB) colour.
    bool gradientBackground = true;
    glm::vec3 backgroundColor{0.168f, 0.168f, 0.168f};
    // File > Maya (#42). Kept in <prefs>/maya.json, never in ui.json, so a shared UI layout can't point the bridge
    // at a folder: it is set only by the user's own pick (App::setMayaFolder).
    std::string mayaFolder;        // "" = the bridge is off
    bool mayaAutoReceive = false;  // Get automatically: check that folder's manifest once a second
};

Json toJson(const UiPrefs& prefs);
// Just the look and the layout ("look" and "layout" keys): ui.json's prefs, and project.json's "ui_look", which
// remembers them without Store Config. lookFromJson says whether `j` named either.
void lookToJson(const UiPrefs& prefs, Json& j);
bool lookFromJson(const Json& j, UiPrefs& prefs);
void fromJson(const Json& j, UiPrefs& prefs);

namespace config {
// Preferences palette sub-palettes (the shell's Preferences palette calls these).
void drawConfigSection(App& app);    // Enable Customize, Store / Restore / Save / Load UI
void drawInterfaceSection(App& app); // UI scale, Popup Info, tooltips, sub-palettes, left-handed layout
void drawHotkeysSection(App& app);   // every action's shortcut, click to reassign
void drawLightBoxSection(App& app);  // show at startup, rows, placement, library folder
// The Classic look's colours as a Palette (for its card; the look itself uses Theme.h's tokens).
looks::Palette classicPalette();
// A miniature of a look on its card, from a to b: its wallpaper, a bar with its accent, two tiles and `name` in its
// text colour. `layout` 0 adds the ZBrush-style palette bar and two-row shelf, 1 the SculptForge menus and one-row
// shelf (the first-run look picker); -1 neither (Preferences > Interface > Look).
void drawLookMiniature(ImDrawList* dl, ImVec2 a, ImVec2 b, const looks::Palette& pal, const char* name, ImFont* font,
                       float s, int layout = -1);
} // namespace config

} // namespace sf
