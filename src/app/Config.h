// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// UI preferences and the configuration workflow. <prefs>/ui.json keeps the shortcut overrides, these preferences
// and the radial menus, saved a moment after each change (#117), and the layout as Save Layout last kept it.
// Reset Layout and Shortcuts goes back to the built-in ones, Revert to Saved Layout to the saved layout, and
// Export Interface / Import Interface (Ctrl+L) share all of it as a file.
#pragma once

#include "app/LookSchemes.h"
#include "app/Theme.h"
#include "core/Json.h"
#include "core/ModifierBrush.h"
#include "core/SpaceMouse.h"
#include "core/Tablet.h"

#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace sf {

class App;

struct UiPrefs {
    bool popupInfo = true;      // Ctrl+hover shows an item's description ("Ctrl+hover help")
    float helpOpacity = 0.97f;  // Ctrl-help box opacity ("Help box opacity")
    float tooltipDelay = 0.5f;  // plain hover tooltips on icon-only buttons and clipped labels
    bool reduceMotion = false;  // hover highlights change colour only: no easing, no lifting tiles (Ui.h)
    bool oneSubpalette = true;  // opening a sub-palette closes the others (Shift+click opens more)
    bool autoCollapseLeft = true, autoCollapseRight = false; // trays collapse palettes that would overflow
    float uiScale = 0.0f;       // 0 = follow the display; otherwise applied at the next start
    bool libraryAtStartup = true;
    bool startupSplash = true;  // the logo while a slow start-up runs (read early from ui.json: StartupSplash.h)
    std::string libraryFolder;  // "" = Documents/SculptForge
    bool alphabeticalPalettes = false; // strict A-Z palette bar (default: File, Edit first)
    // Preferences > Interface > Units (#148; core/Units.h ids): the unit sizes show in (brush ring, Part size, export
    // sizes, Measure), and the unit the numbers in OBJ and PLY files are read in (STL is millimetres by convention).
    std::string lengthUnit = "mm", fileUnit = "m";
    std::string language = "en"; // Preferences > Interface: the interface language, "en", "es", "zh-TW", "zh-CN" or "ja" (app/I18n.h)
    int cameraNotch = 0;        // keep the UI out of a laptop's camera notch: 0 automatic, 1 always, 2 never
    // View > Orbit lock (rotation): 0 free, about the screen axes; 1 turntable (Y up); 2 about world X only; 3 about
    // world Z only (Camera::orbitAbout). Saved as "orbitLock", and "turntable" for older versions.
    int orbitLock = 0;
    int inputStyle = 1;         // Preferences > Input: 0 classic, 1 SculptForge (the default for new installs:
                                // 1-0 brushes, I = Falloff...), 2 Blender (middle-drag orbit, numpad views,
                                // Blender's sculpt keys), 3 Maya (Input.cpp). The first-run welcome asks.
    bool inputStyleSaved = false; // ui.json named the style (not saved itself): an install from before 0.10.4
                                  // without it had the Classic sculpting keys and keeps them (PanelProject.cpp)
    // Classic sculpting keys: F frames the active Part, F twice within this many ms frames the whole scene (a scene
    // with one Part always frames it whole). 0: F always frames the whole scene.
    int frameDoubleTapMs = 400;
    bool emulateNumpad = false; // Blender style: the 1-9 and 0 keys above the letters act as the numpad (laptops)
    // The order of the asset lists (core/ListSort.h ids "az", "recent", "pack"): brushes, alphas, materials, Library.
    std::string brushSort = "az", alphaSort = "az", materialSort = "az", librarySort = "az";
    std::string brushSet; // the brush set last switched to (#143): its shelf comes back at the next start; "" = none
    bool rightClickQuickMenu = true; // a right click on the canvas (no drag) opens the QuickMenu (#22)
    ModifierBrushes modifierBrushes; // the Shift and Ctrl brushes (Shift+click / Ctrl+click a shelf brush)
    bool spaceCamera = false; // hold Space and drag to move the camera (#43; off: Space opens the QuickMenu)
    // Blender style (#77): off, as Blender 5.1+'s default keymap, Alt+left-drag on the model masks (Ctrl+Alt unmasks)
    // and turns the view only beside the model; on (Blender's Emulate 3 Button Mouse) it always turns the view.
    bool emulate3Button = false;
    // Zoom drags (#32): the Maya style zooms with a sideways drag (right = in), as Maya's own dolly; false makes it
    // vertical as the other styles. invertZoom turns every zoom drag around.
    bool mayaZoomHorizontal = true;
    bool invertZoom = false;
    float twoKeyWindow = 0.4f; // seconds for the second key of a two-key shortcut (0.35 to 1, Preferences > Input)
    float keyNavSpeed = 1.0f; // the pan and zoom keys' speed, times 0.1 to 5 (Preferences > Input)
    // The mouse wheel zooms toward the surface point under the pointer (#134) instead of the view's centre. On for
    // the Maya style (set with the style, and the default when ui.json doesn't say); the other styles keep the centre.
    bool wheelZoomToPointer = false;
    // Rotation drags that start over the model turn the view about the surface point under the pointer (and move the
    // target to its depth); over the background, about the target as before. Every input style. On for new installs;
    // a ui.json saved before it existed reads as off, so existing users keep the orbit they know.
    bool orbitAtPointer = true;
    // Mouse and trackpad rotation drags (every input style): the speed, times 0.1 to 2 (1 = as before; the pen has its
    // own View speed in Preferences > Tablet).
    float mouseOrbitSpeed = 1.0f;
    // Tool > Remesh (#121): Quick Remesh (Shift+R, or hold N and drag on the background, remeshes the active Part at
    // the Voxel Resolution) and Remesh after each stroke (a stroke that stretched the surface remeshes it).
    bool quickRemesh = false, remeshAfterStroke = false;
    // Along the normal (from the discussions): 0 = with the invert key (Alt; Ctrl in the Blender and Maya styles),
    // 1 = always, 2 = never. For Move, Tendril, Elastic Tendril and KelvinGrab.
    int alongNormal = 0;
    // Brush falloff options (from the discussions): Connected only, View falloff (App::connectedOnly_ / viewFalloff_).
    bool connectedOnly = false;
    bool viewFalloff = false;
    // Dynamic topology: how much Smooth remeshes, 0-1 (DyntopoSettings::smoothRemesh; 1 as before).
    float dyntopoSmoothRemesh = 1.0f;
    int slideRotateKey = 0;   // Slide Stamp (#175): the key that turns the alpha toward the cursor while sliding: 0 Shift, 1 Ctrl, 2 Alt, 3 off
    bool airStrokes = false;  // start strokes off the model (#62): a left-drag on the background sculpts once it
                              // reaches the surface instead of turning the view (off: it turns the view, classic)
    bool showAxes = true;              // Draw > Camera > Axes: the X / Y / Z gizmo in the canvas corner (#31)
    int axisGizmoSize = 1;             // Draw > Camera > Gizmo size: 0 Small, 1 Medium, 2 Large (axisGizmoScale)
    bool showViewFigure = false;       // Draw > Camera > View Figure: a small shaded head that turns with the view
    bool cursorFollowsSurface = true; // the brush ring lies on the surface under the pointer (#25)
    bool hideRingWhileStroking = false; // Preferences > Cursor (#88): only the centre dot while a stroke runs
    // Preferences > Hints (U11): the notes SculptForge shows over the 3D view. Each has Don't show again, which
    // adds its id here; showHints off hides them all.
    bool showHints = true;
    std::set<std::string> hiddenHints;
    // Preferences > Interface > Timeline position (#63): the Sculpt Timeline strip along the top of the viewport
    // (true, the default: clear of the brush shelf below it) or its bottom.
    bool timelineTop = true;
    // Preferences > Interface > Look: the colours and style of the interface (Theme.h; new installs get Forge
    // Terminal with Tokyo Night), and the shell's layout: 0 the Classic layout (the alphabetical palette bar,
    // two-row top shelf), 1 the SculptForge layout (the default: task menus File, Edit, Sculpt, Paint, Mesh, View,
    // Render, Help, a status bar, one-row shelf). Remembered in project.json too, on its own.
    theme::LookSettings look;
    int shellLayout = 1;
    bool lookSaved = false; // ui.json named the look or layout (not saved itself); one from before 0.10.7 didn't
    // Preferences > Cursor > Viewport cursor (#73): the system cursor over the 3D view, 0 = crosshair (the
    // default, as most sculpting apps), 1 = the arrow. Panels, menus and popups always show their own.
    int viewportCursor = 0;
    // Tool > Viewport Look > Backface culling (#68): skip the back faces of every Part (faster on dense meshes; see
    // through open meshes). Off by default; a preference, projects keep their own.
    bool backfaceCulling = false;
    // View > Display > Floor grid (#115): the grid on the ground plane (RenderSettings::floorGrid). Off by default.
    bool floorGrid = false;
    // UV Editor > Pack > Columns x Rows (#135): the UDIM grid the last pack filled, the default for new projects.
    int uvPackColumns = 1, uvPackRows = 1;
    // Preferences > Interface > Mask darkness (and Tool > Masking): how strongly masked areas show, 0-1
    // (RenderSettings::maskDarkness).
    float maskDarkness = 0.7f;
    // Preferences > Interface > Mask colour: maskColorCustom off = the darkening look; on = masked areas blend towards
    // maskColor (display values, sRGB) by the same amount (the opacity above).
    bool maskColorCustom = false;
    glm::vec3 maskColor{0.20f, 0.22f, 0.30f};
    bool confirmRemeshLevels = true; // Voxel/Quad Remesh and Decimate ask first on a Part with levels (#173)
    bool confirmSubdivide = true; // Subdivide (removes levels) asks first on a Part with levels (Don't ask again)
    TabletSettings tablet;      // Preferences > Tablet: pressure curve, eraser, pen buttons
    SpaceMouseSettings spaceMouse; // Preferences > Input > SpaceMouse
    // Preferences > Performance (PanelPerformance.cpp).
    int antialiasing = 4;       // samples per pixel in the 3D view: 1 (off), 2, 4 or 8
    bool fastNavigation = true; // big scenes draw without antialiasing while the view moves
    int viewResolution = 100;   // the 3D view's resolution, % of the window's (#80): 100, 75 or 50
    bool multithreading = true; // brushes, normals and BVH updates on every core (off: one thread)
    // Sculpt strokes solved on their own thread with a GPU preview of the dabs still to come, the cursor and the
    // stroke's head predicted a frame ahead (app/AsyncStroke.cpp). Off: the dabs are placed in the frame loop.
    bool predictiveStrokes = false; // experimental: not yet bit-identical to the serial path in every run
    // Draft strokes (with predictive strokes): dabs move the vertices only; the normals, cavities and culling
    // bounds of what they touched are worked out once, when the pen lifts (Sculptor::draftStrokes). Off by
    // default; meant for laptops and battery (a hardware tier will turn it on).
    bool draftStrokes = false;
    // Experimental (docs/design/perf-moonshots.md), off by default. Live Move: the GPU draws a Move drag and the
    // move is applied once when the pen lifts (SF_LIVE_MOVE=0 / 1 overrides). Background Preview: heavy meshes'
    // background dabs draw the predictive preview too (SF_BG_GHOST=0 / 1 overrides). Fast Upload: stroke uploads
    // go through persistently mapped staging buffers and a GPU copy (SF_PERSISTENT_UPLOAD=0 / 1 overrides).
    // Forge Deck prototype (PanelDeck.cpp), Preferences > Experimental: off by default. While off the File menu has
    // no entry for it and nothing about it runs; the Deck itself is off at every start even when this is on.
    bool forgeDeck = false;
    bool liveMove = false;
    bool backgroundPreview = false;
    bool fastUpload = false;
    // Fast big brushes (Sculptor::adaptiveDabs, core/SculptBatch.cpp): a big brush's dabs take their plane from a
    // sample of the region, carry the normals along with the move (the exact ones follow), and the dabs waiting
    // for a frame go through the vertices together. The result matches the dab-by-dab stroke to within about 2% of
    // how far it moved the surface (tests/test_beta_boost.cpp). Experimental, off by default (0.11 beta: not yet
    // faster on every machine).
    bool fastBigBrushes = false;
    // Dynamic topology (Draw palette): false = the detail follows the brush size on screen (dynamic topology);
    // true = one detail size for the whole mesh.
    bool dyntopoConstantDetail = false;
    bool flatShading = false; // Material palette: the scene's shading at startup (false = Smooth, the default)
    // Preferences > Display: the view's background (#21), a gradient or a flat display (sRGB) colour.
    bool gradientBackground = true;
    glm::vec3 backgroundColor{0.168f, 0.168f, 0.168f};
    // File > Maya (#42). Kept in <prefs>/maya.json, never in ui.json, so a shared UI layout can't point the bridge
    // at a folder: it is set only by the user's own pick (App::setMayaFolder).
    std::string mayaFolder;        // "" = the bridge is off
    bool mayaAutoReceive = false;  // Get automatically (on once a folder is chosen): check its manifest every 250 ms
    bool mayaLive = false;         // Live (opt-in): send the changed Parts by themselves after each stroke
};

Json toJson(const UiPrefs& prefs);
// Just the look and the layout ("look" and "layout" keys): ui.json's prefs, and project.json's "ui_look", which
// remembers them on its own. lookFromJson says whether `j` named either.
void lookToJson(const UiPrefs& prefs, Json& j);
bool lookFromJson(const Json& j, UiPrefs& prefs);
void fromJson(const Json& j, UiPrefs& prefs);
// UiPrefs::orbitLock as the orbit_lock command and ui.json name it: "free", "y", "x", "z" (-1: not one of them).
const char* orbitLockName(int lock);
int parseOrbitLock(std::string_view name);

namespace config {
// Preferences palette sub-palettes (the shell's Preferences palette calls these).
void drawConfigSection(App& app);    // Customize Shelves, Store / Restore / Save / Import Interface
void drawInterfaceSection(App& app); // UI scale, Ctrl+hover help, tooltips, sub-palettes, left-handed layout
void drawHotkeysSection(App& app);   // every action's shortcut, click to reassign
void drawLibraryBrowserSection(App& app);  // show at startup, rows, placement, library folder
// The Classic look's colours as a Palette (for its card; the look itself uses Theme.h's tokens).
looks::Palette classicPalette();
// A miniature of a look on its card, from a to b: its wallpaper, a bar with its accent, two tiles and `name` in its
// text colour. `layout` 0 adds the classic palette bar and two-row shelf, 1 the SculptForge menus and one-row
// shelf (the first-run look picker); -1 neither (Preferences > Interface > Look).
void drawLookMiniature(ImDrawList* dl, ImVec2 a, ImVec2 b, const looks::Palette& pal, const char* name, ImFont* font,
                       float s, int layout = -1);
} // namespace config

} // namespace sf
