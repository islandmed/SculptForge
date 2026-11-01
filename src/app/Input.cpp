// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The ZBrush feel (docs/zbrush-parity.md): ZBrush's keys that SculptForge lacked (U Z Intensity, I RGB
// Intensity, O Focal Shift, Space QuickMenu, L LazyMouse, V switch colour, Ctrl+A / Ctrl+Shift+A / Ctrl+I /
// Ctrl+H for the mask, Ctrl+W group masked), Preferences > Input with the ZBrush and SculptForge key styles,
// the QuickMenu itself and Shift-constrained straight strokes. The mouse gestures live with the rest of the
// input in App.cpp (roll, Ctrl+click blur / sharpen in PanelMasking.cpp).
//
// The Blender style (#12): its default keys come from Blender 4.2 - 5.x's own keymap (blender_default.py, Sculpt
// and 3D View), with the brush letters on SculptForge's nearest brush. The mouse side is in App.cpp
// (onMouseDown: middle-drag orbit, Alt+left-drag as the middle button) and blenderMods (Ctrl inverts).
#include "app/App.h"
#include "app/BlenderKeymap.h"
#include "app/Pickers.h"
#include "app/Ui.h"
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
    add("ui.strength_popup", "Z Intensity at cursor", ICON_LC_CIRCLE_DOT, "Draw", "U",
        "A Z Intensity slider under the cursor: drag it, or type a number (1-100) and press Enter. Hold Shift (or "
        "pick Smooth) to set Smooth's own Z Intensity.",
        [this] { PickerUi::sizePopup(*this, PickerUi::Slider::ZIntensity); });
    add("ui.rgb_popup", "RGB Intensity at cursor", ICON_LC_PALETTE, "Draw", "I",
        "An RGB Intensity slider under the cursor: how strongly painting colours the surface (1-100).",
        [this] { PickerUi::sizePopup(*this, PickerUi::Slider::RgbIntensity); });
    add("ui.focal_popup", "Focal Shift at cursor", ICON_LC_CIRCLE_DASHED, "Draw", "O",
        "A Focal Shift slider under the cursor: below 0 a broad, hard brush tip, above 0 a soft point (-100..100). The "
        "inner ring of the brush cursor shows it.",
        [this] { PickerUi::sizePopup(*this, PickerUi::Slider::FocalShift); });
    add("ui.quickmenu", "QuickMenu", ICON_LC_LAYOUT_GRID, "Help", "Space",
        "ZBrush's QuickMenu under the cursor: brush, stroke, alpha, material, colour, Draw Size, Focal Shift and the "
        "intensities. Hold Space and let go to close it, or tap Space to keep it open.",
        [this] { openQuickMenu(); }, [this] { return quickMenu_.open; });

    // Stroke and colour.
    add("stroke.lazy", "LazyMouse", ICON_LC_SPLINE, "Stroke", "L",
        "LazyMouse on or off: the brush trails the cursor on a string, for steady lines.",
        [this] { strokeType_ = strokeType_ == StrokeType::LazyMouse ? StrokeType::Dots : StrokeType::LazyMouse; },
        [this] { return strokeType_ == StrokeType::LazyMouse; });
    add("stroke.continuous", "Continuous", ICON_LC_REPEAT, "Stroke", "",
        "Continuous on or off: while you hold the brush still on the model it keeps adding, 30 dabs a second "
        "(build up a bump without moving). Not for Move, Snake Hook or the drag strokes.",
        [this] { strokeContinuous_ = !strokeContinuous_; }, [this] { return strokeContinuous_; });
    add("color.switch", "Switch Color", ICON_LC_ARROW_LEFT_RIGHT, "Color", "V",
        "Swap the main and the secondary colour.", [this] { std::swap(doc_.paintColor, secondaryColor_); });

    // Masking (ZBrush Tool > Masking keys).
    add("mask.all", "Mask All", ICON_LC_SQUARE, "Tool > Masking", "Ctrl+A", "Mask the whole visible SubTool.",
        [this] { runCommand("mask_all"); });
    add("mask.clear", "Clear Mask", ICON_LC_ERASER, "Tool > Masking", "Ctrl+Shift+A",
        "Unmask everything (also: Ctrl+drag on the background, away from the model).", [this] { runCommand("mask_clear"); });
    add("mask.invert", "Invert Mask", ICON_LC_FLIP_HORIZONTAL_2, "Tool > Masking", "Ctrl+I",
        "Mask what is unmasked and unmask what is masked (also: Ctrl+click the background).",
        [this] { runCommand("mask_invert"); });
    actions_.setDefaultShortcut("view.mask", "Ctrl+H"); // View Mask
    add("polygroup.group_masked", "Group Masked", ICON_LC_SHAPES, "Tool > Polygroups", "Ctrl+W",
        "Turn the masked part into a new polygroup and clear the mask (ZBrush Ctrl+W). With nothing masked, "
        "everything visible becomes one group.",
        [this] { groupMasked(); });
    add("polygroup.uv_groups", "Group by UVs", ICON_LC_GRID_2X2, "Tool > Polygroups", "",
        "One polygroup per UV island (ZBrush UV Groups): triangles that touch without a UV seam share a group. "
        "Only the visible part is regrouped. Needs UVs (Tool > Texture Map > Auto UV makes some).",
        [this] {
            runCommand("polygroup_uv");
            if (lastCommandOk_) renderSettings_.showPolygroups = true;
        });
    actions_.setDefaultShortcut("config.save", "Ctrl+Shift+Alt+I");

    // Preferences > Input: the key style.
    add("input.style_zbrush", "ZBrush", nullptr, "Preferences > Input", "",
        "ZBrush's keys and gestures: I is RGB Intensity, 9 is QuickSave, the number keys don't pick brushes, and "
        "Ctrl+drag on the background only clears the mask.",
        [this] { setInputStyle(0); }, [this] { return uiPrefs_.inputStyle == 0; });
    add("input.style_sculptforge", "SculptForge", nullptr, "Preferences > Input", "",
        "SculptForge's own keys from before 0.7: 1-9 and 0 pick the first ten brushes, I is Z Intensity (as is U), "
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
    add("input.right_click_menu", "Right click opens the QuickMenu", nullptr, "Preferences > Input", "",
        "A right click on the canvas, without dragging, opens the QuickMenu under the cursor, as in ZBrush (Space "
        "does too). A right drag still turns, pans or zooms the view. A pen button set to Right click works the same.",
        [this] { uiPrefs_.rightClickQuickMenu = !uiPrefs_.rightClickQuickMenu; },
        [this] { return uiPrefs_.rightClickQuickMenu; });
    add("input.space_camera", "Hold Space and drag to move the camera", nullptr, "Preferences > Input", "",
        "While Space is held, a drag on the canvas moves the camera: left-drag turns it, right- or middle-drag pans, "
        "and with Ctrl held too a drag zooms (#43). Off by default: in ZBrush, Space opens the QuickMenu (with this "
        "on, a right click still does). Typing a space in a text field is never taken.",
        [this] { uiPrefs_.spaceCamera = !uiPrefs_.spaceCamera; }, [this] { return uiPrefs_.spaceCamera; });
    add("input.surface_cursor", "Cursor follows surface", nullptr, "Preferences > Input", "",
        "The brush ring lies on the model under the pointer, turned to the surface and pressed onto its bumps, at "
        "the brush's real size there (as in SculptGL), with a short line along the surface's direction. With "
        "symmetry on, the mirrored ring shows too. Off the model, and with this off, the ring is a flat circle on "
        "the screen.",
        [this] { uiPrefs_.cursorFollowsSurface = !uiPrefs_.cursorFollowsSurface; },
        [this] { return uiPrefs_.cursorFollowsSurface; });
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
    add("visibility.hide_group", "Hide polygroup under cursor", ICON_LC_EYE_OFF, "Tool > Visibility", "",
        "Hide the polygroup under the cursor (Blender: hide the face set).", [this] { hideGroupUnderCursor(false); });
    add("visibility.isolate_group", "Isolate polygroup under cursor", ICON_LC_EYE, "Tool > Visibility", "",
        "Show only the polygroup under the cursor; again, with it isolated, shows everything (Blender: toggle "
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
    add("view.frame_selected", "Frame SubTool", "Numpad .", "Fit the active SubTool in the view (Blender: View Selected).",
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
    // with Ctrl and masks with the Mask brush; its Alt is the emulated middle button, so it means nothing here.
    SDL_Keymod out = keyboard & SDL_KMOD_SHIFT;
    const bool ctrl = (keyboard & (SDL_KMOD_CTRL | SDL_KMOD_GUI)) != 0;
    if (maskBrushActive()) {
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
    ImGui::TextDisabled("Style");
    ImGui::SameLine();
    drawInputStyleRow();
    if (blenderStyle()) {
        bool emulate = uiPrefs_.emulateNumpad;
        if (ImGui::Checkbox("Emulate Numpad", &emulate)) uiPrefs_.emulateNumpad = emulate;
        ui::HelpAction(*this, "input.emulate_numpad");
    }
    bool rightClickMenu = uiPrefs_.rightClickQuickMenu;
    if (ui::Checkbox("Right click opens the QuickMenu##input.right_click_menu", &rightClickMenu))
        uiPrefs_.rightClickQuickMenu = rightClickMenu;
    ui::HelpAction(*this, "input.right_click_menu");
    bool spaceCamera = uiPrefs_.spaceCamera;
    if (ui::Checkbox("Hold Space and drag to move the camera##input.space_camera", &spaceCamera))
        uiPrefs_.spaceCamera = spaceCamera;
    ui::HelpAction(*this, "input.space_camera");
    bool surfaceCursor = uiPrefs_.cursorFollowsSurface;
    if (ui::Checkbox("Cursor follows surface##input.surface_cursor", &surfaceCursor))
        uiPrefs_.cursorFollowsSurface = surfaceCursor;
    ui::HelpAction(*this, "input.surface_cursor");
    ui::ActionButton(*this, "input.import_blender", ImVec2(-FLT_MIN, 0), ui::Look::IconLabel);
    ImGui::TextDisabled("Rotation");
    ImGui::SameLine();
    int rotation = uiPrefs_.turntable ? 1 : 0;
    ImGui::SetNextItemWidth(-1);
    if (ImGui::Combo("##rotation_input", &rotation, "Free (ZBrush)\0Turntable\0")) uiPrefs_.turntable = rotation == 1;
    ui::HelpText("Rotation", nullptr,
                 "Free: the model turns about the screen's axes and can roll over the top, as in ZBrush. Turntable: it "
                 "stays upright, as in Blender or Maya.",
                 "Preferences > Input");
    drawSpaceMouseSection(); // SpaceMouse.cpp
}

void App::drawZIntensitySlider(const char* id) {
    const bool smooth = activeBrush() == BrushType::Smooth && drag_ != Drag::Stroke;
    float& value = smooth ? smoothStrength_ : strength_;
    float shown = value * 100.0f;
    if (ui::SliderInside(id, &shown, 1.0f, 100.0f, smooth ? "Smooth Z Intensity %.0f" : "Z Intensity %.0f"))
        value = std::clamp(shown / 100.0f, 0.01f, 1.0f);
    ui::HelpText(smooth ? "Smooth Z Intensity" : "Z Intensity", "U",
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
    if (straight_.shiftAtStart || !(inputMods() & SDL_KMOD_SHIFT)) {
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
    quickMenu_.open = true;
    quickMenu_.at = ImVec2(mouseX_, mouseY_);
    quickMenu_.openedAt = ImGui::GetTime();
    quickMenu_.keyHeld = true;
    quickMenu_.untilLeave = false;
    quickMenu_.openedFrame = ImGui::GetFrameCount();
}

void App::rightClickRelease() {
    if (!rightClick_.pending) return;
    rightClick_.pending = false;
    if (!uiPrefs_.rightClickQuickMenu) return;
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
    ImGuiWindow* w = ImGui::FindWindowByName("QuickMenu##quickmenu");
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
    if (ImGui::Begin("QuickMenu##quickmenu", nullptr,
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
        ui::SliderInside("##qm_drawsize", &radiusPx_, 2.0f, 800.0f, "Draw Size %.0f", ImGuiSliderFlags_Logarithmic);
        ui::HelpText("Draw Size", "S", "Brush radius in screen pixels.", "QuickMenu");
        float focal = focalShift_ * 100.0f;
        ImGui::SetNextItemWidth(sw);
        if (ui::SliderInside("##qm_focal", &focal, -100.0f, 100.0f, "Focal Shift %.0f")) focalShift_ = focal / 100.0f;
        ui::HelpText("Focal Shift", "O", "Below 0: a broad, hard tip. Above 0: a soft point.", "QuickMenu");
        const bool smooth = activeBrush() == BrushType::Smooth;
        float& zValue = smooth ? smoothStrength_ : strength_;
        float z = zValue * 100.0f;
        ImGui::SetNextItemWidth(sw);
        if (ui::SliderInside("##qm_z", &z, 1.0f, 100.0f, smooth ? "Smooth Z Intensity %.0f" : "Z Intensity %.0f"))
            zValue = z / 100.0f;
        ui::HelpText("Z Intensity", "U", "How strongly the brush changes the surface.", "QuickMenu");
        float rgb = rgbIntensity_ * 100.0f;
        ImGui::SetNextItemWidth(sw);
        if (ui::SliderInside("##qm_rgb", &rgb, 1.0f, 100.0f, "RGB Intensity %.0f")) rgbIntensity_ = rgb / 100.0f;
        ui::HelpText("RGB Intensity", "I", "How strongly painting colours the surface.", "QuickMenu");
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

} // namespace sf
