// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The ZBrush feel (docs/guide/coming-from-zbrush.md): ZBrush's keys that SculptForge lacked (U Z Intensity, I RGB
// Intensity, O Focal Shift, Space QuickMenu, L LazyMouse, V switch colour, Ctrl+A / Ctrl+Shift+A / Ctrl+I /
// Ctrl+H for the mask, Ctrl+W group masked), Preferences > Input with the ZBrush and SculptForge key styles,
// the QuickMenu itself and Shift-constrained straight strokes. The mouse gestures live with the rest of the
// input in App.cpp (roll, Ctrl+click blur / sharpen in PanelMasking.cpp).
//
// The Blender style (#12): its default keys come from Blender 4.2 - 5.x's own keymap (blender_default.py, Sculpt
// and 3D View), with the brush letters on SculptForge's nearest brush. The mouse side is in App.cpp
// (onMouseDown: middle-drag orbit, Alt+left-drag as the middle button) and blenderMods (Ctrl inverts).
#include "app/App.h"
#include "app/AirStroke.h"
#include "app/BlenderKeymap.h"
#include "app/Pickers.h"
#include "app/Theme.h"
#include "app/Ui.h"
#include "app/I18n.h"
#include "core/Polygroups.h"

#include <IconsLucide.h>

#include <imgui_internal.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace sf {

namespace {

constexpr double kQuickMenuTapSeconds = 0.25; // Space released sooner: a tap, the QuickMenu stays open

bool ctrlDown(SDL_Keymod mod) { return (mod & (SDL_KMOD_CTRL | SDL_KMOD_GUI)) != 0; } // as App.cpp

bool anyMasked(const Mesh& mesh) {
    return std::any_of(mesh.mask.begin(), mesh.mask.end(), [](float m) { return m > 0.0f; });
}

// The brush's action id, as the shell registers it ("brush.claybuildup").
std::string brushId(BrushType t) {
    std::string id = "brush.";
    for (const char* c = brushName(t); *c; ++c)
        if (*c != ' ') id += char(std::tolower(static_cast<unsigned char>(*c)));
    return id;
}

} // namespace

void App::registerInputActions() {
    auto add = [this](const char* id, const char* label, const char* icon, const char* palette, const char* shortcut,
                      const char* description, std::function<void()> run, std::function<bool()> checked = nullptr,
                      std::function<bool()> enabled = nullptr) {
        Action a;
        a.id = id;
        a.label = label;
        a.icon = icon;
        a.palette = palette;
        a.shortcut = shortcut;
        a.description = description;
        a.run = std::move(run);
        a.checked = std::move(checked);
        a.enabled = std::move(enabled);
        actions_.add(std::move(a));
    };

    // The sliders at the cursor (S is Draw Size, in LightBox.cpp).
    add("ui.strength_popup", "Intensity at cursor", ICON_LC_CIRCLE_DOT, "Draw", "U",
        "A Intensity slider under the cursor: drag it, or type a number (1-100) and press Enter. Hold Shift (or "
        "pick Smooth) to set Smooth's own Intensity.",
        [this] { PickerUi::sizePopup(*this, PickerUi::Slider::ZIntensity); });
    add("ui.rgb_popup", "RGB Intensity at cursor", ICON_LC_PALETTE, "Draw", "I",
        "An RGB Intensity slider under the cursor: how strongly painting colours the surface (1-100).",
        [this] { PickerUi::sizePopup(*this, PickerUi::Slider::RgbIntensity); });
    add("ui.focal_popup", "Falloff at cursor", ICON_LC_CIRCLE_DASHED, "Draw", "O",
        "A Falloff slider under the cursor: below 0 a broad, hard brush tip, above 0 a soft point (-100..100). The "
        "inner ring of the brush cursor shows it.",
        [this] { PickerUi::sizePopup(*this, PickerUi::Slider::FocalShift); });
    add("ui.quickmenu", "Quick Menu", ICON_LC_LAYOUT_GRID, "Help", "Space",
        "ZBrush's QuickMenu under the cursor: brush, stroke, alpha, material, colour, Draw Size, Falloff and the "
        "intensities. Hold Space and let go to close it, or tap Space to keep it open.",
        [this] { openQuickMenu(); }, [this] { return quickMenu_.open; });

    // Stroke and colour.
    // (The id stays stroke.lazy so keys people set keep working; the Lazy Stroke type is Dots on the string.)
    add("stroke.lazy", "Steady Stroke", ICON_LC_SPLINE, "Stroke", "L",
        "Steady Stroke on or off, with every stroke type (Dots, Drag Stamp, Slide Stamp, Spray): the brush trails "
        "the cursor on a string and only moves once the string is taut, for steady lines without hand shake.",
        [this] {
            if (strokeType_ == StrokeType::LazyMouse) { // the Lazy Stroke type is on: L turns the string off
                strokeType_ = StrokeType::Dots;
                steadyStroke_ = false;
            } else {
                steadyStroke_ = !steadyStroke_;
            }
            status_ = steadyOn() ? "Steady Stroke on" : "Steady Stroke off";
        },
        [this] { return steadyOn(); });
    add("stroke.steady_catch_up", "Catch up on release", ICON_LC_SPLINE, "Stroke", "",
        "Steady Stroke: when you let go, the line runs on to where the cursor is, instead of stopping a string's "
        "length behind it.",
        [this] { steadyCatchUp_ = !steadyCatchUp_; }, [this] { return steadyCatchUp_; });
    add("brush.world_size", "World Size", ICON_LC_RULER, "Draw", "",
        "Keep the brush size in model units (mm) instead of screen pixels: it stays the same on the surface as you "
        "zoom in and out, for pores, wrinkles and trims of an exact width.",
        [this] {
            worldSize_ = !worldSize_;
            worldSyncedPx_ = -1.0f; // the size as it is now becomes the world size
            status_ = worldSize_ ? "World Size on: the brush keeps its size on the surface" : "World Size off";
        },
        [this] { return worldSize_; });
    add("brush.size_down_fine", "Smaller brush by 1 px", ICON_LC_MINUS, "Draw", "Shift+[",
        "Make the brush one pixel smaller (half a pixel below 2 px), down to 0.5 px.",
        [this] { radiusPx_ = drawSizeStep(radiusPx_, -1, true); });
    add("brush.size_up_fine", "Larger brush by 1 px", ICON_LC_PLUS, "Draw", "Shift+]",
        "Make the brush one pixel larger (half a pixel below 2 px).", [this] { radiusPx_ = drawSizeStep(radiusPx_, 1, true); });
    // One action per stroke type (U13): the Strokes radial menu (J) and the command palette pick them directly.
    {
        static const char* const keys[] = {"dots", "drag_stamp", "slide_stamp", "spray", "lazy"};
        static const char* const glyphs[] = {ICON_LC_ELLIPSIS, ICON_LC_SQUARE_DASHED_MOUSE_POINTER, ICON_LC_MOUSE_POINTER_2,
                                             ICON_LC_SPRAY_CAN, ICON_LC_SPLINE_POINTER};
        static const char* const help[] = {
            "Stroke type Dots: even dabs along the path, the classic brush stroke.",
            "Stroke type Drag Stamp: click and drag one stamp; the drag sets its size and rotation.",
            "Stroke type Slide Stamp: drag one stamp of the brush's Draw Size over the surface; it lands where you let go.",
            "Stroke type Spray: scattered dabs of random size and strength, for organic texture.",
            "Stroke type Lazy Stroke: the brush trails the cursor on a string, for smooth, steady lines.",
        };
        static_assert(std::size(keys) == size_t(StrokeType::Count));
        for (int i = 0; i < int(StrokeType::Count); ++i) {
            const StrokeType type = StrokeType(i);
            const std::string id = std::string("stroke.type.") + keys[i];
            add(id.c_str(), strokeTypeName(type), glyphs[i], "Stroke", "", help[i],
                [this, type] {
                    strokeType_ = type;
                    status_ = std::string("Stroke: ") + strokeTypeName(type);
                },
                [this, type] { return strokeType_ == type; });
        }
    }
    add("stroke.continuous", "Continuous", ICON_LC_REPEAT, "Stroke", "",
        "Continuous on or off: while you hold the brush still on the model it keeps adding, 30 dabs a second "
        "(build up a bump without moving). Not for Move, Tendril or the drag strokes.",
        [this] { strokeContinuous_ = !strokeContinuous_; }, [this] { return strokeContinuous_; });
    add("color.switch", "Switch Color", ICON_LC_ARROW_LEFT_RIGHT, "Color", "V",
        "Swap the main and the secondary colour.", [this] { std::swap(doc_.paintColor, secondaryColor_); });

    // Masking (ZBrush Tool > Masking keys).
    add("mask.all", "Mask All", ICON_LC_SQUARE, "Tool > Masking", "Ctrl+A", "Mask the whole visible Part.",
        [this] { runCommand("mask_all"); });
    add("mask.clear", "Clear Mask", ICON_LC_ERASER, "Tool > Masking", "Ctrl+Shift+A",
        "Unmask everything (also: Ctrl+drag on the background, away from the model).", [this] { runCommand("mask_clear"); });
    add("mask.invert", "Invert Mask", ICON_LC_FLIP_HORIZONTAL_2, "Tool > Masking", "Ctrl+I",
        "Mask what is unmasked and unmask what is masked (also: Ctrl+click the background).",
        [this] { runCommand("mask_invert"); });
    actions_.setDefaultShortcut("view.mask", "Ctrl+H"); // View Mask
    add("polygroup.group_masked", "Group Masked", ICON_LC_SHAPES, "Tool > Face Groups", "Ctrl+W",
        "Turn the masked part into a new face group and clear the mask (ZBrush Ctrl+W). With nothing masked, "
        "everything visible becomes one group.",
        [this] { groupMasked(); });
    add("polygroup.uv_groups", "Group by UVs", ICON_LC_GRID_2X2, "Tool > Face Groups", "",
        "One face group per UV island (ZBrush UV Groups): triangles that touch without a UV seam share a group. "
        "Only the visible part is regrouped. Needs UVs (Tool > Texture Map > Auto UV makes some).",
        [this] {
            runCommand("polygroup_uv");
            if (lastCommandOk_) renderSettings_.showPolygroups = true;
        });
    actions_.setDefaultShortcut("config.save", "Ctrl+Shift+Alt+I");

    // Preferences > Input: the key style.
    add("input.style_zbrush", "ZBrush-style", nullptr, "Preferences > Input", "",
        "Keys and gestures as in ZBrush, for hands that know it: I is RGB Intensity, 9 saves a snapshot, the number keys don't pick brushes, and "
        "Ctrl+drag on the background only clears the mask.",
        [this] { setInputStyle(0); }, [this] { return uiPrefs_.inputStyle == 0; });
    add("input.style_sculptforge", "SculptForge", nullptr, "Preferences > Input", "",
        "SculptForge's own keys (the default): 1-9 and 0 pick the first ten brushes, I is Intensity (as is U), "
        "and Ctrl+drag on the background zooms when nothing is masked.",
        [this] { setInputStyle(1); }, [this] { return uiPrefs_.inputStyle == 1; });
    add("input.style_blender", "Blender", nullptr, "Preferences > Input", "",
        "Blender's navigation and sculpt-mode keys: middle-drag orbits (Shift pans, Ctrl zooms), Alt+left-drag does "
        "the same for pens and trackpads, Ctrl inverts a stroke, Shift smooths, F / Shift+F set radius and strength, "
        "V S P I G C K pick brushes and M the mask brush, Home frames everything. Rotation switches to Turntable.",
        [this] { setInputStyle(2); }, [this] { return uiPrefs_.inputStyle == 2; });
    add("input.style_maya", "Maya", nullptr, "Preferences > Input", "",
        "Maya's view controls: Alt+left-drag orbits, Alt+middle-drag pans, Alt+right-drag zooms, anywhere on the "
        "canvas. Ctrl inverts a stroke, Ctrl+Alt masks (Ctrl+Alt+drag on the background clears the mask), Shift "
        "smooths, Ctrl+Shift hides and shows. ZBrush's keys stay for everything else.",
        [this] { setInputStyle(3); }, [this] { return uiPrefs_.inputStyle == 3; });
    add("input.emulate_numpad", "Emulate Numpad", nullptr, "Preferences > Input", "",
        "Blender style: the number keys above the letters work as the numpad (1 front, 3 right, 7 top, 5 "
        "orthographic...), for keyboards without one.",
        [this] { uiPrefs_.emulateNumpad = !uiPrefs_.emulateNumpad; }, [this] { return uiPrefs_.emulateNumpad; },
        [this] { return blenderStyle(); });
    add("input.right_click_menu", "Right click opens the Quick Menu", nullptr, "Preferences > Input", "",
        "A right click on the canvas, without dragging, opens the Quick Menu under the cursor, as in ZBrush (Space "
        "does too). A right drag still turns, pans or zooms the view. A pen button set to Right click works the same.",
        [this] { uiPrefs_.rightClickQuickMenu = !uiPrefs_.rightClickQuickMenu; },
        [this] { return uiPrefs_.rightClickQuickMenu; });
    add("input.space_camera", "Hold Space and drag to move the camera", nullptr, "Preferences > Input", "",
        "While Space is held, a drag on the canvas moves the camera: left-drag turns it, right- or middle-drag pans, "
        "and with Ctrl held too a drag zooms (#43). Off by default: in ZBrush, Space opens the Quick Menu (with this "
        "on, a right click still does). Typing a space in a text field is never taken.",
        [this] { uiPrefs_.spaceCamera = !uiPrefs_.spaceCamera; }, [this] { return uiPrefs_.spaceCamera; });
    add("input.emulate_3_button", "Emulate 3 Button Mouse", nullptr, "Preferences > Input", "",
        "Blender style: Alt+left-drag always turns the view (Shift pans, Ctrl zooms), as Blender's Emulate 3 Button "
        "Mouse preference does, for pens and trackpads. Off (as Blender's default keymap, 5.1 and later): Alt+left-drag "
        "on the model masks and Ctrl+Alt+left-drag unmasks; beside the model it still turns the view (#77).",
        [this] { uiPrefs_.emulate3Button = !uiPrefs_.emulate3Button; }, [this] { return uiPrefs_.emulate3Button; });
    add("input.air_strokes", "Start strokes off the model", nullptr, "Preferences > Input", "",
        "A left-drag that starts on the background begins a brush stroke instead of turning the view; the brush "
        "touches the model once the cursor reaches it (#62). Ctrl (mask) and Shift (smooth) work the same way, and "
        "a Ctrl+click on the background still inverts the mask. The view still turns with a right-drag or a pen "
        "button set to Right click, pans with Alt+drag or a middle-drag, and moves with Space held if Hold Space is "
        "on; with Maya keys, Alt+left-drag orbits, with Blender keys beside the model. Shift+L (View > Lock camera) "
        "turns it on and off.",
        [this] { uiPrefs_.airStrokes = !uiPrefs_.airStrokes; }, [this] { return uiPrefs_.airStrokes; });
    add("input.toggle_air_strokes", "Lock camera", nullptr, "View", "Shift+L",
        "Lock the camera for left-drags: a drag that starts on the background sculpts once it reaches the model "
        "instead of turning the view (Preferences > Input > Start strokes off the model, #62). The status bar shows "
        "CAMERA LOCKED while it is on. Right-drag or Alt+drag still move the view. Shift+L turns it on and off.",
        [this] {
            uiPrefs_.airStrokes = !uiPrefs_.airStrokes;
            status_ = uiPrefs_.airStrokes ? "Camera locked: drags start strokes; right-drag or Alt+drag to move the view"
                                          : "Camera unlocked: a drag on the background turns the view";
        },
        [this] { return uiPrefs_.airStrokes; });
    add("input.surface_cursor", "Cursor follows surface", nullptr, "Preferences > Cursor", "",
        "The brush ring lies on the model under the pointer: a circle at the brush's real size there, turned to the "
        "form of the surface (averaged over the brush, so fine bumps don't make it wobble), with a short line along "
        "the surface's direction. With symmetry on, a dot shows where each mirrored or radial copy lands. Off the "
        "model, and with this off, the ring is a flat circle on the screen.",
        [this] { uiPrefs_.cursorFollowsSurface = !uiPrefs_.cursorFollowsSurface; },
        [this] { return uiPrefs_.cursorFollowsSurface; });
    add("cursor.hide_ring_stroking", "Hide ring while stroking", nullptr, "Preferences > Cursor", "",
        "While a stroke runs, show only the dot where the brush touches (and the symmetry dots), not the ring: "
        "nothing between you and the surface you are shaping. Off by default: the ring shows the brush size "
        "throughout.",
        [this] { uiPrefs_.hideRingWhileStroking = !uiPrefs_.hideRingWhileStroking; },
        [this] { return uiPrefs_.hideRingWhileStroking; });
    add("input.import_blender", "Import Blender keymap...", ICON_LC_FILE_INPUT, "Preferences > Input", "",
        "Use the keys from a keymap you exported in Blender (Preferences > Keymap > Export, a .py file). Switches "
        "to the Blender style, then takes the navigation and sculpt keys SculptForge has an equivalent for. The "
        "file is only read, never run. Store Config keeps the result.",
        [this] { importBlenderKeymapDialog(); });

    // Blender style: the mask brush and Sculpt mode's visibility keys.
    add("mask.brush", "Mask brush", ICON_LC_PAINTBRUSH, "Tool > Masking", "",
        "Paint the mask with a plain left drag, Ctrl+drag to unmask (Blender's Mask brush). Press it again, or pick "
        "another brush, to go back.",
        [this] { toggleMaskBrush(); }, [this] { return maskBrushActive(); });
    add("visibility.hide_group", "Hide face group under cursor", ICON_LC_EYE_OFF, "Tool > Visibility", "",
        "Hide the face group under the cursor (Blender: hide the face set).", [this] { hideGroupUnderCursor(false); });
    add("visibility.isolate_group", "Isolate face group under cursor", ICON_LC_EYE, "Tool > Visibility", "",
        "Show only the face group under the cursor; again, with it isolated, shows everything (Blender: toggle "
        "visibility).",
        [this] { hideGroupUnderCursor(true); });
    add("visibility.show_all", "Show all", ICON_LC_EYE, "Tool > Visibility", "", "Show every hidden part of the mesh.",
        [this] { runCommand("show_all"); });

    commands_.add({"blender_keymap_import",
                   "Use the keys of a keymap exported from Blender (Preferences > Keymap > Export, a .py file): "
                   "switches to the Blender input style and takes the navigation and sculpt keys SculptForge has an "
                   "equivalent for. The file is read as text, never run.",
                   {{"path", ParamSpec::Type::String, "The exported .py file.", true}},
                   [this](Document&, const Args& a) -> CommandResult {
                       const bool ok = importBlenderKeymap(argString(a, "path"));
                       return {ok, status_};
                   }});
    registerViewActions();
    registerStrokePopupActions(); // StrokePopup.cpp
    applyInputStyle();
}

void App::registerViewActions() {
    auto add = [this](const char* id, const char* label, const char* shortcut, const char* description,
                      std::function<void()> run, std::function<bool()> checked = nullptr) {
        Action a;
        a.id = id;
        a.label = label;
        a.palette = "Transform";
        a.shortcut = shortcut;
        a.description = description;
        a.run = std::move(run);
        a.checked = std::move(checked);
        actions_.add(std::move(a));
    };
    // The numpad, as in Blender (every style: the keys are free otherwise). Axis views are orthographic until the
    // view is turned again (Blender's Auto Perspective).
    add("view.front", "Front view", "Numpad 1", "Look at the model from the front (orthographic).",
        [this] { viewAxis(0.0f, 0.0f); });
    add("view.back", "Back view", "Ctrl+Numpad 1", "Look at the model from behind (orthographic).",
        [this] { viewAxis(180.0f, 0.0f); });
    add("view.right", "Right view", "Numpad 3", "Look at the model's right side (orthographic).",
        [this] { viewAxis(90.0f, 0.0f); });
    add("view.left", "Left view", "Ctrl+Numpad 3", "Look at the model's left side (orthographic).",
        [this] { viewAxis(-90.0f, 0.0f); });
    add("view.top", "Top view", "Numpad 7", "Look down on the model (orthographic).", [this] { viewAxis(0.0f, -90.0f); });
    add("view.bottom", "Bottom view", "Ctrl+Numpad 7", "Look up at the model from below (orthographic).",
        [this] { viewAxis(0.0f, 90.0f); });
    // Draw > Camera: the corner gizmo's size (Small / Medium / Large, saved in the preferences).
    static constexpr const char* sizeIds[3] = {"view.gizmo_small", "view.gizmo_medium", "view.gizmo_large"};
    static constexpr const char* sizeLabels[3] = {"Small", "Medium", "Large"};
    for (int i = 0; i < 3; ++i)
        add(sizeIds[i], sizeLabels[i], "",
            "Gizmo size: how big the X / Y / Z axes and the View Figure are in the corner of the canvas. Grows with "
            "the UI scale.",
            [this, i] { uiPrefs_.axisGizmoSize = i; },
            [this, i] { return uiPrefs_.axisGizmoSize == i; });
    add("view.ortho", "Orthographic", "Numpad 5",
        "Switch between a perspective and an orthographic view (no foreshortening: sizes don't shrink with depth).",
        [this] {
            camera_.ortho = !camera_.ortho;
            autoOrtho_ = false;
        },
        [this] { return camera_.ortho; });
    add("view.orbit_left", "Orbit left", "Numpad 4", "Turn the view 15 degrees to the left around the model.",
        [this] { viewOrbitStep(-15.0f, 0.0f); });
    add("view.orbit_right", "Orbit right", "Numpad 6", "Turn the view 15 degrees to the right around the model.",
        [this] { viewOrbitStep(15.0f, 0.0f); });
    add("view.orbit_up", "Orbit up", "Numpad 8", "Turn the view 15 degrees up over the model.",
        [this] { viewOrbitStep(0.0f, -15.0f); });
    add("view.orbit_down", "Orbit down", "Numpad 2", "Turn the view 15 degrees down under the model.",
        [this] { viewOrbitStep(0.0f, 15.0f); });
    add("view.opposite", "Opposite side", "Numpad 9", "Look at the model from the other side.", [this] {
        camera_.setOrientation(camera_.orientation() * glm::angleAxis(glm::pi<float>(), glm::vec3(0, 1, 0)));
    });
    add("view.zoom_in", "Zoom in", "Numpad Plus", "Move the view closer.", [this] { camera_.zoom(1.0f); });
    add("view.zoom_out", "Zoom out", "Numpad Minus", "Move the view back.", [this] { camera_.zoom(-1.0f); });
    add("view.frame_selected", "Frame Part", "Numpad .", "Fit the active Part in the view (Blender: View Selected).",
        [this] { frameActiveSubtool(); });
}

void App::setInputStyle(int style) {
    uiPrefs_.inputStyle = std::clamp(style, 0, 3);
    if (blenderStyle()) uiPrefs_.turntable = true; // Blender orbits as a turntable; Preferences > Input changes it
    maskBrush_.on = false;
    applyInputStyle();
}

void App::applyInputStyle() {
    const int style = std::clamp(uiPrefs_.inputStyle, 0, 3) == 3 ? 0 : std::clamp(uiPrefs_.inputStyle, 0, 2); // Maya keeps ZBrush keys
    const bool sf = style == 1;
    // One row per action whose key depends on the style: ZBrush, SculptForge, Blender (Blender 4.2 - 5.x defaults).
    struct Keys {
        const char* id;
        const char* keys[3];
    };
    static const Keys kKeys[] = {
        {"file.quicksave", {"9", "", ""}},
        {"ui.strength_popup", {"U", "U / I", "Shift+F / U"}},
        {"ui.rgb_popup", {"I", "", ""}},
        {"ui.size_popup", {"S", "S", "F"}},
        {"view.frame", {"F", "F", "Home"}},
        {"view.wireframe", {"Shift+F", "Shift+F", "Shift+Z"}},
        {"color.switch", {"V", "V", "X"}},
        {"color.pick", {"C", "C", "Shift+X"}},
        {"transform.symmetry_x", {"X", "X", ""}},
        {"draw.paint", {"P", "P", ""}},
        {"ui.picker.material", {"M", "M", ""}},
        {"stroke.lazy", {"L", "L", "L / Shift+S"}},
        {"stroke.popup", {"G", "G", "Z"}}, // (Blender's G is its Move brush; J opens the Strokes radial menu)
        {"mask.clear", {"Ctrl+Shift+A", "Ctrl+Shift+A", "Alt+M / Ctrl+Shift+A"}},
        {"geometry.lower_res", {"Shift+D", "Shift+D", "Alt+1 / Shift+D"}},
        {"geometry.higher_res", {"D", "D", "Alt+2 / D"}},
        {"mask.brush", {"", "", "M"}},
        {"visibility.hide_group", {"", "", "H"}},
        {"visibility.isolate_group", {"", "", "Shift+H"}},
        {"visibility.show_all", {"", "", "Alt+H"}},
    };
    for (const Keys& k : kKeys) actions_.setDefaultShortcut(k.id, k.keys[style]);
    // Brushes: 1-9 and 0 in the SculptForge style; Blender's sculpt-mode letters in the Blender style.
    static const std::pair<BrushType, const char*> kBlenderBrushes[] = {
        {BrushType::Standard, "V"}, {BrushType::Smooth, "S"},        {BrushType::Pinch, "P"},
        {BrushType::Inflate, "I"},  {BrushType::Move, "G"},          {BrushType::Flatten, "Shift+T"},
        {BrushType::ClayBuildup, "C"}, {BrushType::Crease, "Shift+C"}, {BrushType::SnakeHook, "K"},
    };
    for (int i = 0; i < int(BrushType::Count); ++i) {
        std::string key = sf && i < 10 ? (i < 9 ? std::to_string(i + 1) : "0") : "";
        if (style == 2)
            for (const auto& [type, letter] : kBlenderBrushes)
                if (int(type) == i) key = letter;
        actions_.setDefaultShortcut(brushId(BrushType(i)), key);
    }
}

// ---------------------------------------------------------------------------
// Blender style
// ---------------------------------------------------------------------------

void App::viewAxis(float yawDegrees, float pitchDegrees) {
    camera_.yaw = glm::radians(yawDegrees);
    camera_.pitch = glm::radians(pitchDegrees);
    camera_.roll = 0.0f;
    if (!camera_.ortho) {
        camera_.ortho = true;
        autoOrtho_ = true;
    }
}

void App::viewOrbitStep(float yawDegrees, float pitchDegrees) {
    // Yaw about the world's up axis, pitch about the view's own right axis (Blender's numpad orbit).
    camera_.setOrientation(glm::angleAxis(glm::radians(yawDegrees), glm::vec3(0, 1, 0)) * camera_.orientation() *
                           glm::angleAxis(glm::radians(pitchDegrees), glm::vec3(1, 0, 0)));
    if (autoOrtho_) {
        camera_.ortho = false;
        autoOrtho_ = false;
    }
}

void App::frameActiveSubtool() {
    const AABB b = doc_.mesh.bounds();
    if (b.valid()) camera_.frame(b);
    else camera_.frame(sceneBounds());
}

SDL_Keymod App::blenderMods(SDL_Keymod keyboard) const {
    // What the sculpting code reads: Alt inverts, Ctrl masks, Shift smooths (the ZBrush meanings). Blender inverts
    // with Ctrl and masks with the Mask brush, or (5.1+, #77) with Alt held: Alt+drag masks, Ctrl+Alt+drag unmasks.
    // With Emulate 3 Button Mouse, Alt is the emulated middle button and means nothing here.
    SDL_Keymod out = keyboard & SDL_KMOD_SHIFT;
    const bool ctrl = (keyboard & (SDL_KMOD_CTRL | SDL_KMOD_GUI)) != 0;
    if ((keyboard & SDL_KMOD_ALT) && !uiPrefs_.emulate3Button) {
        out = SDL_KMOD_LCTRL; // the mask brush, as ZBrush's Ctrl (not with Shift: that would hide)
        if (ctrl) out = SDL_Keymod(out | SDL_KMOD_LALT); // ... unmasking, as ZBrush's Ctrl+Alt
    } else if (maskBrushActive()) {
        if (!(keyboard & SDL_KMOD_SHIFT)) out |= SDL_KMOD_LCTRL; // Shift still smooths the surface
        if (ctrl) out |= SDL_KMOD_LALT;                          // Ctrl unmasks
    } else if (ctrl) {
        out |= SDL_KMOD_LALT;
    }
    return out;
}

bool App::maskBrushActive() const {
    return maskBrush_.on && blenderStyle() && brush_ == maskBrush_.brush && doc_.activeBrush == maskBrush_.custom &&
           paintMode_ == maskBrush_.paint && !guidesMode_;
}

void App::toggleMaskBrush() {
    if (maskBrushActive()) {
        maskBrush_.on = false;
        status_ = "Mask brush off";
        return;
    }
    maskBrush_ = {true, brush_, doc_.activeBrush, paintMode_};
    status_ = blenderStyle() ? "Mask brush: drag to mask, Ctrl+drag to unmask (M again: back to your brush)"
                             : "The Mask brush is for the Blender style; here, hold Ctrl to mask";
}

void App::hideGroupUnderCursor(bool isolate) {
    doc_.syncBvh();
    RayHit hit;
    if (!pick(mouseX_, mouseY_, hit)) {
        if (isolate && doc_.mesh.hasHidden()) runCommand("show_all");
        return;
    }
    const uint32_t group = doc_.mesh.polygroupAt(hit.triangle);
    if (!isolate) runCommand("hide_polygroup", {{"group", double(group)}});
    else if (polygroupIsolated(doc_.mesh, group)) runCommand("show_all");
    else runCommand("isolate_polygroup", {{"group", double(group)}});
}

bool App::importBlenderKeymap(const std::string& path) {
    std::ifstream f(std::filesystem::path(std::u8string(path.begin(), path.end())), std::ios::binary);
    std::string text;
    if (f) text.assign(std::istreambuf_iterator<char>(f), {});
    if (!f && text.empty()) {
        status_ = "Could not read " + path;
        lastCommandOk_ = false;
        return false;
    }
    if (text.size() > 8u * 1024u * 1024u) {
        status_ = "That file is too big for a Blender keymap";
        lastCommandOk_ = false;
        return false;
    }
    BlenderKeymap keymap;
    std::string err;
    if (!parseBlenderKeymap(text, keymap, err)) {
        status_ = err;
        lastCommandOk_ = false;
        return false;
    }
    setInputStyle(2);
    std::vector<std::string> assigned;
    for (const BlenderKeyBinding& b : keymap.bindings) {
        if (!actions_.find(b.action)) continue;
        const bool first = std::find(assigned.begin(), assigned.end(), b.action) == assigned.end();
        actions_.assignShortcut(b.action, chordText(b.chord), !first);
        if (first) assigned.push_back(b.action);
    }
    status_ = "Blender keymap: " + std::to_string(keymap.bindings.size()) + " keys for " +
              std::to_string(assigned.size()) + " actions (" + std::to_string(keymap.skipped) +
              " Blender keys have no SculptForge equivalent). Store Config keeps them.";
    lastCommandOk_ = true;
    return true;
}

void App::drawInputStyleRow(float width) {
    ui::Segmented(*this, {"input.style_zbrush", "input.style_sculptforge", "input.style_blender", "input.style_maya"}, width);
}

void App::drawInputSection() {
    ImGui::TextDisabled("%s", tr("Style"));
    ImGui::SameLine();
    drawInputStyleRow();
    if (blenderStyle()) {
        bool emulate = uiPrefs_.emulateNumpad;
        if (ImGui::Checkbox(tr("Emulate Numpad"), &emulate)) uiPrefs_.emulateNumpad = emulate;
        ui::HelpAction(*this, "input.emulate_numpad");
    }
    bool rightClickMenu = uiPrefs_.rightClickQuickMenu;
    if (ui::Checkbox("Right click opens the Quick Menu##input.right_click_menu", &rightClickMenu))
        uiPrefs_.rightClickQuickMenu = rightClickMenu;
    ui::HelpAction(*this, "input.right_click_menu");
    bool spaceCamera = uiPrefs_.spaceCamera;
    if (ui::Checkbox("Hold Space and drag to move the camera##input.space_camera", &spaceCamera))
        uiPrefs_.spaceCamera = spaceCamera;
    ui::HelpAction(*this, "input.space_camera");
    if (blenderStyle()) {
        bool emulate = uiPrefs_.emulate3Button;
        if (ui::Checkbox("Emulate 3 Button Mouse##input.emulate_3_button", &emulate)) uiPrefs_.emulate3Button = emulate;
        ui::HelpAction(*this, "input.emulate_3_button");
    }
    drawPressureStatus(); // the pen's pressure, with the troubleshooting link (Tablet.cpp)
    if (mayaStyle()) {
        int zoom = uiPrefs_.mayaZoomHorizontal ? 0 : 1;
        ImGui::TextDisabled("%s", tr("Zoom drag"));
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-1);
        const std::string zooms = std::string(tr("Sideways (Maya)")) + '\0' + tr("Up and down") + '\0';
        if (ImGui::Combo("##input.maya_zoom", &zoom, zooms.c_str())) uiPrefs_.mayaZoomHorizontal = zoom == 0;
        ui::HelpText("Zoom drag", nullptr,
                     "Alt+right drag zooms. Sideways (the default, as Maya's own dolly): drag right to zoom in, left to "
                     "zoom out. Up and down: drag up to zoom in, as the SculptForge style.",
                     "Preferences > Input");
    }
    ui::Checkbox("Invert zoom##input.invert_zoom", &uiPrefs_.invertZoom);
    ui::HelpText("Invert zoom", nullptr, "Turn zoom drags around (the mouse wheel stays as it is).", "Preferences > Input");
    {
        ImGui::TextDisabled("%s", tr("Along the normal"));
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-1);
        const char* key = blenderStyle() || mayaStyle() ? "With Ctrl" : "With Alt";
        const std::string modes = std::string(tr(key)) + '\0' + tr("Always") + '\0' + tr("Never") + '\0';
        ImGui::Combo("##input.along_normal", &uiPrefs_.alongNormal, modes.c_str());
        ui::HelpText("Along the normal", nullptr,
                     "Move, Tendril, Elastic Tendril and KelvinGrab: with the invert key held as the stroke begins "
                     "(Alt; Ctrl in the Blender and Maya styles), the surface moves along the average normal under the "
                     "brush instead of across the screen, so it pulls straight out of the surface or pushes straight "
                     "in. Always: every stroke of those brushes does; Never: the key does nothing for them.",
                     "Preferences > Input");
    }
    bool airStrokes = uiPrefs_.airStrokes;
    if (ui::Checkbox("Start strokes off the model##input.air_strokes", &airStrokes)) uiPrefs_.airStrokes = airStrokes;
    ui::HelpAction(*this, "input.air_strokes");
    bool shareSize = brushSettingsStore().shareSizeIntensity;
    if (ui::Checkbox("Share size and intensity across brushes##brush.share_size", &shareSize)) setShareBrushSize(shareSize);
    ui::HelpAction(*this, "brush.share_size");
    ui::ActionButton(*this, "input.import_blender", ImVec2(-FLT_MIN, 0), ui::Look::IconLabel);
    drawPackKeymapRows(); // keymaps from installed packs (PanelPacks.cpp)
    ImGui::TextDisabled("%s", tr("Rotation"));
    ImGui::SameLine();
    int rotation = uiPrefs_.turntable ? 1 : 0;
    ImGui::SetNextItemWidth(-1);
    const std::string rotations = std::string(tr("Free (ZBrush)")) + '\0' + tr("Turntable") + '\0';
    if (ImGui::Combo("##rotation_input", &rotation, rotations.c_str())) uiPrefs_.turntable = rotation == 1;
    ui::HelpText("Rotation", nullptr,
                 "Free: the model turns about the screen's axes and can roll over the top, as in ZBrush. Turntable: it "
                 "stays upright, as in Blender or Maya.",
                 "Preferences > Input");
    drawRadialMenusSection(); // RadialMenu.cpp
    drawSpaceMouseSection();  // SpaceMouse.cpp
}

// Preferences > Cursor (#88): the system cursor over the view and the brush ring.
void App::drawCursorSection() {
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(tr("Viewport cursor"));
    const char* const kCursors[] = {"Crosshair##viewport_cursor", "Arrow##viewport_cursor"};
    for (int i = 0; i < 2; ++i) {
        ImGui::SameLine();
        if (ImGui::RadioButton(tr(kCursors[i]), uiPrefs_.viewportCursor == i)) uiPrefs_.viewportCursor = i;
        ui::HelpText("Viewport cursor", nullptr,
                     "The pointer over the 3D view: a crosshair (the default, as in most sculpting apps; it stays "
                     "while the view turns) or the arrow. Panels, menus and popups always show the arrow (or their "
                     "own resize and text cursors). Store Config keeps it.",
                     "Preferences > Cursor");
    }
    bool surfaceCursor = uiPrefs_.cursorFollowsSurface;
    if (ui::Checkbox("Cursor follows surface##input.surface_cursor", &surfaceCursor))
        uiPrefs_.cursorFollowsSurface = surfaceCursor;
    ui::HelpAction(*this, "input.surface_cursor");
    bool hideRing = uiPrefs_.hideRingWhileStroking;
    if (ui::Checkbox("Hide ring while stroking##cursor.hide_ring_stroking", &hideRing)) uiPrefs_.hideRingWhileStroking = hideRing;
    ui::HelpAction(*this, "cursor.hide_ring_stroking");
}

void App::drawZIntensitySlider(const char* id) {
    const bool smooth = activeBrush() == BrushType::Smooth && drag_ != Drag::Stroke;
    float& value = smooth ? smoothStrength_ : strength_;
    float shown = value * 100.0f;
    if (ui::SliderInside(id, &shown, 1.0f, 100.0f, smooth ? "Smooth Intensity %.0f" : "Intensity %.0f"))
        value = std::clamp(shown / 100.0f, 0.01f, 1.0f);
    ui::HelpText(smooth ? "Smooth Intensity" : "Intensity", "U",
                 smooth ? "How strongly Smooth works. Smooth keeps its own value, as in ZBrush: hold Shift to see and "
                          "set it. Ctrl+click to type."
                        : "How strongly each dab changes the surface (1-100). Holding Shift shows Smooth's own value. "
                          "Ctrl+click to type.",
                 "Draw");
}

// ---------------------------------------------------------------------------
// Ctrl+W
// ---------------------------------------------------------------------------

void App::groupMasked() {
    if (anyMasked(doc_.mesh)) {
        runCommand("polygroup_from_mask");
        if (lastCommandOk_) {
            const std::string message = status_;
            runCommand("mask_clear");
            status_ = message;
        }
    } else {
        runCommand("polygroup_merge_visible");
    }
    if (lastCommandOk_) renderSettings_.showPolygroups = true; // like the panel's buttons: show the result
}

// ---------------------------------------------------------------------------
// Straight strokes
// ---------------------------------------------------------------------------

glm::vec2 App::constrainStroke(float x, float y) {
    // (Shift+Alt is the precision drag, not a Shift line)
    if (straight_.shiftAtStart || !(inputMods() & SDL_KMOD_SHIFT) || precisionHeld()) {
        straight_.active = false;
        return {x, y};
    }
    if (!straight_.active) {
        straight_.active = true;
        straight_.origin = glm::vec2(lastX_, lastY_); // the line starts where the stroke is when Shift goes down
    }
    const glm::vec2 d = glm::vec2(x, y) - straight_.origin;
    if (glm::length(d) < 1e-3f) return straight_.origin;
    constexpr float kStep = 3.14159265f / 4.0f; // 45 degrees
    const float angle = std::round(std::atan2(d.y, d.x) / kStep) * kStep;
    const glm::vec2 dir(std::cos(angle), std::sin(angle));
    return straight_.origin + dir * std::max(0.0f, glm::dot(d, dir));
}

// ---------------------------------------------------------------------------
// QuickMenu (Space)
// ---------------------------------------------------------------------------

void App::openQuickMenu() {
    if (quickMenu_.open && !quickMenu_.keyHeld) { // tapped open, tapped again: close
        quickMenu_.open = false;
        return;
    }
    closeStrokePopup();
    quickMenu_.open = true;
    quickMenu_.at = ImVec2(mouseX_, mouseY_);
    quickMenu_.openedAt = ImGui::GetTime();
    quickMenu_.keyHeld = true;
    quickMenu_.untilLeave = false;
    quickMenu_.openedFrame = ImGui::GetFrameCount();
}

void App::rightClickRelease() {
    const bool click = rightClick_.pending;
    rightClick_.pending = false;
    if (zsMenu_.armed) { // on a Forge Sphere: its menu instead (PanelZSpheres.cpp)
        zsMenu_.armed = false;
        zsMenu_.open = click;
        return;
    }
    if (!click || !uiPrefs_.rightClickQuickMenu) return;
    // Opened as a tap of Space would: it stays until a click elsewhere or Esc.
    quickMenu_.open = true;
    quickMenu_.at = ImVec2(rightClick_.x, rightClick_.y);
    quickMenu_.openedAt = ImGui::GetTime();
    quickMenu_.keyHeld = false;
    quickMenu_.untilLeave = false;
    quickMenu_.openedFrame = ImGui::GetFrameCount();
}

void App::quickMenuKeyUp() {
    if (!quickMenu_.keyHeld) return;
    quickMenu_.keyHeld = false;
    if (!quickMenu_.open) return;
    // Held: let go to close, as in ZBrush, unless the pointer is on the menu (then it stays until it leaves).
    const bool tap = ImGui::GetTime() - quickMenu_.openedAt < kQuickMenuTapSeconds;
    ImGuiWindow* w = ImGui::FindWindowByName("Quick Menu##quickmenu");
    const bool over = w && ImGui::IsMouseHoveringRect(w->Pos, ImVec2(w->Pos.x + w->Size.x, w->Pos.y + w->Size.y), false);
    if (!tap && !over) quickMenu_.open = false;
    quickMenu_.untilLeave = !tap && over;
}

void App::drawQuickMenu() {
    if (!quickMenu_.open) return;
    if (pickerOpen() && ImGui::GetFrameCount() > quickMenu_.openedFrame + 1) { // a tile opened its picker
        quickMenu_.open = false;
        return;
    }
    const float s = uiScale_;
    const float tile = 56.0f * s;
    const float width = 4 * tile + 3 * ImGui::GetStyle().ItemSpacing.x + 150.0f * s;
    ImGui::SetNextWindowPos(ImVec2(quickMenu_.at.x - width * 0.5f, quickMenu_.at.y - tile * 0.5f), ImGuiCond_Appearing);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 8));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(37, 37, 40, 245));
    bool close = false;
    if (ImGui::Begin("Quick Menu##quickmenu", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                         ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing)) {
        ImGui::BeginGroup();
        drawBrushTile("##qm_brush", ImVec2(tile, tile));
        ImGui::SameLine();
        drawStrokeTile("##qm_stroke", ImVec2(tile, tile));
        ImGui::SameLine();
        drawAlphaTile("##qm_alpha", ImVec2(tile, tile));
        ImGui::SameLine();
        drawMaterialTile("##qm_material", ImVec2(tile, tile));
        ImGui::EndGroup();
        ImGui::SameLine();
        ImGui::BeginGroup();
        const float sw = 150.0f * s;
        ImGui::SetNextItemWidth(sw);
        drawSizeSlider("##qm_drawsize", "Quick Menu", "Brush radius in screen pixels (0.5 to 800), and in mm on the surface.");
        float focal = focalShift_ * 100.0f;
        ImGui::SetNextItemWidth(sw);
        if (ui::SliderInside("##qm_focal", &focal, -100.0f, 100.0f, "Falloff %.0f")) focalShift_ = focal / 100.0f;
        ui::HelpText("Falloff", "O", "Below 0: a broad, hard tip. Above 0: a soft point.", "Quick Menu");
        const bool smooth = activeBrush() == BrushType::Smooth;
        float& zValue = smooth ? smoothStrength_ : strength_;
        float z = zValue * 100.0f;
        ImGui::SetNextItemWidth(sw);
        if (ui::SliderInside("##qm_z", &z, 1.0f, 100.0f, smooth ? "Smooth Intensity %.0f" : "Intensity %.0f"))
            zValue = z / 100.0f;
        ui::HelpText("Intensity", "U", "How strongly the brush changes the surface.", "Quick Menu");
        float rgb = rgbIntensity_ * 100.0f;
        ImGui::SetNextItemWidth(sw);
        if (ui::SliderInside("##qm_rgb", &rgb, 1.0f, 100.0f, "RGB Intensity %.0f")) rgbIntensity_ = rgb / 100.0f;
        ui::HelpText("RGB Intensity", "I", "How strongly painting colours the surface.", "Quick Menu");
        ImGui::EndGroup();
        ui::Segmented(*this, {"draw.zadd", "draw.zsub"}, 2 * tile + ImGui::GetStyle().ItemSpacing.x);
        ImGui::SameLine();
        ui::Segmented(*this, {"draw.sculpt", "draw.paint"}, 2 * tile + ImGui::GetStyle().ItemSpacing.x);
        ImGui::SameLine();
        ui::ActionButton(*this, "draw.rgb");
        ImGui::SameLine();
        ui::ActionButton(*this, "transform.symmetry_x");
        const bool clicked = ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Right);
        const bool hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
        if (clicked && !hovered && ImGui::GetFrameCount() > quickMenu_.openedFrame + 1) close = true;
        // Space was let go while pointing at it: moving off it closes it.
        if (quickMenu_.untilLeave && !hovered && !ImGui::IsAnyItemActive()) close = true;
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) close = true;
    }
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
    if (close) quickMenu_.open = false;
}

// ---------------------------------------------------------------------------
// Start strokes off the model (#62)
// ---------------------------------------------------------------------------

// A left press on the background (no surface under it): with the preference on, the stroke waits until the drag
// reaches the model. Alt+drag still navigates and Ctrl+Shift still draws the hide / show shapes; the mask shapes
// (MaskLasso, MaskRect) and every other tool took the press before this.
bool App::airStrokeMouseDown(float x, float y, SDL_Keymod mod) {
    const SDL_Keymod keys = SDL_GetModState(); // the keyboard itself (blenderMods turns Ctrl into Alt)
    if (!airStrokeStarts(uiPrefs_.airStrokes, (keys & SDL_KMOD_ALT) != 0, ctrlDown(mod) && (mod & SDL_KMOD_SHIFT),
                         previewLod_ >= 0 || !insertType_.empty()))
        return false;
    drag_ = Drag::AirStroke;
    airMask_ = ctrlDown(mod) && !(mod & SDL_KMOD_SHIFT);
    airMoved_ = false;
    airX_ = x;
    airY_ = y;
    return true;
}

// The waiting stroke begins at the first point over the surface, exactly as a press there would: the stroke path,
// LazyMouse and the first dab start from that point, so nothing jumps from where the drag began.
void App::airStrokeTo(float x, float y) {
    if (!airMoved_ && std::hypot(x - airX_, y - airY_) >= 4.0f) airMoved_ = true;
    doc_.syncBvh();
    RayHit hit;
    if (!pick(x, y, hit)) return;
    drag_ = Drag::None;
    if (airMask_) {
        beginMaskStroke(x, y);
        if (drag_ == Drag::Stroke && maskClickPending_) { // (a Ctrl+press on the model waits to tell a click)
            maskClickPending_ = false;
            firstDab(x, y);
        }
    } else {
        beginStroke(x, y, hit);
        if (drag_ == Drag::Orbit) drag_ = Drag::None; // (the active layer's weight is 0: never turn the view)
    }
}

// Let go before reaching the model: nothing happens, except a Ctrl+click, which inverts the mask as before.
void App::airStrokeRelease() {
    if (airMask_ && !airMoved_) runCommand("mask_invert");
}

void App::drawAirStrokeIndicator() {
    if (!uiPrefs_.airStrokes) return;
    // (below a top timeline; Don't show again and Preferences > Hints hide it, U11)
    drawViewportHint("air_strokes", tr("Strokes start off the model"), theme::u32(theme::accent),
                     canvasRect().y + timelineTopInset_ + 8.0f * uiScale_);
}

} // namespace sf
