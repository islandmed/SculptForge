// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// UI preferences and the ZBrush-style configuration workflow: Store Config
// (Ctrl+Shift+I) writes <prefs>/ui.json with the layout, shortcut overrides and
// these preferences; nothing is saved automatically. Restore Standard UI goes
// back to the built-in layout, Restore Custom UI to the stored one, and Save UI /
// Load UI (Ctrl+L) share a layout as a file.
#pragma once

#include "core/Json.h"
#include "core/SpaceMouse.h"
#include "core/Tablet.h"

#include <string>

namespace sf {

class App;

struct UiPrefs {
    bool popupInfo = true;      // Ctrl+hover shows an item's description (ZBrush "Popup Info")
    float helpOpacity = 0.97f;  // Ctrl-help box opacity (ZBrush "Quick Info Opacity")
    float tooltipDelay = 0.5f;  // plain hover tooltips on icon-only buttons and clipped labels
    bool oneSubpalette = true;  // opening a sub-palette closes the others (Shift+click opens more)
    bool autoCollapseLeft = true, autoCollapseRight = false; // trays collapse palettes that would overflow
    float uiScale = 0.0f;       // 0 = follow the display; otherwise applied at the next start
    bool lightboxAtStartup = true;
    std::string libraryFolder;  // "" = Documents/SculptForge
    bool alphabeticalPalettes = false; // strict A-Z palette bar like ZBrush (default: File, Edit first)
    int cameraNotch = 0;        // keep the UI out of a laptop's camera notch: 0 automatic, 1 always, 2 never
    bool turntable = false;     // rotation: false = free, about the screen axes (ZBrush); true = turntable (Y up)
    int inputStyle = 0;         // Preferences > Input: 0 ZBrush, 1 SculptForge (1-0 brushes, I = Z Intensity...),
                                // 2 Blender (middle-drag orbit, numpad views, Blender's sculpt keys; Input.cpp)
    bool emulateNumpad = false; // Blender style: the 1-9 and 0 keys above the letters act as the numpad (laptops)
    bool rightClickQuickMenu = true; // a right click on the canvas (no drag) opens the QuickMenu, as in ZBrush (#22)
    bool cursorFollowsSurface = true; // the brush ring lies on the surface under the pointer, as in SculptGL (#25)
    TabletSettings tablet;      // Preferences > Tablet: pressure curve, eraser, pen buttons
    SpaceMouseSettings spaceMouse; // Preferences > Input > SpaceMouse
    // Preferences > Performance (PanelPerformance.cpp).
    int antialiasing = 4;       // samples per pixel in the 3D view: 1 (off), 2, 4 or 8
    bool fastNavigation = true; // big scenes draw without antialiasing while the view moves
    bool multithreading = true; // brushes, normals and BVH updates on every core (off: one thread)
    // Sculpt strokes solved on their own thread with a GPU preview of the dabs still to come, the cursor and the
    // stroke's head predicted a frame ahead (app/AsyncStroke.cpp). Off: the dabs are placed in the frame loop.
    bool predictiveStrokes = false; // experimental in 0.10 (Preferences > Performance)
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
    glm::vec3 backgroundColor{0.165f, 0.165f, 0.173f};
};

Json toJson(const UiPrefs& prefs);
void fromJson(const Json& j, UiPrefs& prefs);

namespace config {
// Preferences palette sub-palettes (the shell's Preferences palette calls these).
void drawConfigSection(App& app);    // Enable Customize, Store / Restore / Save / Load UI
void drawInterfaceSection(App& app); // UI scale, Popup Info, tooltips, sub-palettes, left-handed layout
void drawHotkeysSection(App& app);   // every action's shortcut, click to reassign
void drawLightBoxSection(App& app);  // show at startup, rows, placement, library folder
} // namespace config

} // namespace sf
