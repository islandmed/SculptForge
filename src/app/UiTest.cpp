// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// UI test scripts (.sfui): drive the real interface the way a person does and
// check what it shows. One step per line, '#' starts a comment, quote words
// containing spaces. Items are found by their ImGui label ("Find models",
// "Brush Lab/Save settings" for the one in that window).
//
//   click LABEL / double_click LABEL / right_click LABEL / hover LABEL
//   type LABEL TEXT          click a text field and type into it
//   key COMBO                e.g. enter, escape, ctrl+z, ctrl+shift+z, 3, comma, backtick, tab, f3, space, minus,
//                            home, numpad1, ctrl+numpad7, numpad., numpadplus
//   hold MODS                ctrl / shift / alt / ctrl+shift ... or none (for strokes and drags)
//   stroke U0 V0 U1 V1 [S]   brush stroke across the model (fractions of its on-screen bounds)
//   drag X0 Y0 X1 Y1 [S] [release]  left-drag in window fractions (rectangle hide, sliders...); release:
//                            let go of the held modifiers halfway (ZBrush's Alt-drag, release Alt to zoom)
//   drag_item LABEL DX DY [S] left-drag from the item by DX DY pixels (scrubbing, dragging a tile out)
//   drag_path U0 V0 U1 V1 ... [keep] [S=seconds]   left-drag through the points (fractions of the model's
//                            on-screen bounds; outside 0..1 is the background) with the held modifiers: a lasso,
//                            or a rectangle from the first point to the last. keep: leave the button down
//                            (for a shot mid-drag); `release` lets go, with the modifiers held then
//   release                  release the left button
//   gizmo_drag HANDLE DX DY [S] [keep]  left-drag a Gizmo 3D handle by DX DY pixels with the held modifiers (alt: the
//                            gizmo only; shift: snapping). HANDLE: move_x move_y move_z plane_yz plane_xz plane_xy
//                            move_view rotate_x rotate_y rotate_z rotate_view scale_x scale_y scale_z scale_uniform;
//                            keep: leave the button down (a shot mid-drag), then `release`
//   gizmo_hover HANDLE       move the mouse onto a Gizmo 3D handle (it lights up, for screenshots)
//   drag_landmark N DX DY [S] left-drag Make sculptable's face landmark N (0-7, FaceLandmarks order) by DX DY pixels
//   orbit DX DY              right-drag on the background, in pixels (with the held modifiers)
//   button_drag middle|right X0 Y0 X1 Y1 [S]  drag with that button in window fractions (with the held modifiers)
//   expect_shortcut ACTION KEYS  the action's effective keys, e.g. expect_shortcut view.front Numpad 1
//   camera YAW PITCH         set the view's angles in degrees (0 0 = the front view), e.g. for guide screenshots
//   zoom FACTOR / look X Y Z  move the camera FACTOR times closer / turn it about (and look at) the point X Y Z
//   run COMMANDS             sculptforge-cli commands, e.g. run new_sphere subdivisions=4 ($SCRIPT_DIR: the script's folder)
//   drop FILE                as if FILE were dropped onto the window (relative to the script's folder)
//   env NAME VALUE           set an environment variable; $SCRIPT_DIR is the script's folder
//   copy SRC DST             copy a file or folder (SRC relative to the script's folder, DST to the working
//                            directory), e.g. to stage what another program would write (the Blender bridge)
//   wait SECONDS / wait_idle [TIMEOUT] (AI requests, thumbnails, update checks and other background jobs) / wait_for LABEL [TIMEOUT]
//   wait_value NAME OP N [TIMEOUT]  wait (real time, default 30 s) until expect_value NAME OP N would pass
//   expect LABEL / expect_not LABEL
//   expect_text TEXT / expect_no_text TEXT   any text drawn this frame contains TEXT
//   expect_status TEXT       the status bar message contains TEXT
//   expect_help TEXT         the Ctrl-hover help box shown this frame contains TEXT (hold ctrl, then hover)
//   clipboard TEXT           put TEXT on the clipboard (to test pasting, e.g. a brush code)
//   expect_clipboard TEXT    the clipboard contains TEXT (after a copy)
//   click_subtool NAME [U V] left-click SubTool NAME in the viewport (with the held modifiers, e.g.
//                            hold alt to select it); U V: fractions of its on-screen bounds
//   expect_value NAME OP N   triangles, vertices, layers, brushes, masked, hidden, groups, colored,
//                            can_undo, dirty, splats, show_polygroups, show_mask, show_mesh, wireframe,
//                            subtools (count), active_subtool (index, 0 = top), visible_subtools,
//                            edited_subtools (SubTools with undo history), user_brushes (brushes in the
//                            user library), library_files (.sfbrush files in it; UI tests get an empty
//                            one in --ui-out/brush-library), ui_config (1 once Store Config wrote ui.json),
//                            console_open, command_palette_open, customize, shelves_visible,
//                            right_tray_open, left_tray_open, lightbox_open, guide_open, guide_hits (search results), shelves (1 shown), left_tray / right_tray (palettes in
//                            the tray, -1 when closed); OP is == != < <= > >=
//                            moved (vertices moved since `snapshot`), moved_max / moved_sum (distance),
//                            moved_masked (masked vertices that moved),
//                            moved_out (summed movement along the snapshot's normals: > 0 out, < 0 in),
//                            pen_pressure (raw), brush_pressure (after the curve; 1 for the mouse),
//                            stroking (1 while a stroke is open), cursor_x / cursor_y (the brush cursor, pixels),
//                            camera_yaw / camera_pitch / camera_roll (degrees),
//                            camera_up (the view's up vector . world Y), camera_aligned (1 when looking along a world
//                            axis), camera_distance, camera_pan (target offset);
//                            N may be $NAME or $NAME*K for a value kept by `remember`
//                            paint_layers (paint layers above Base), active_paint_layer (-1 = Base),
//                            paint_mean_r / paint_mean_g / paint_mean_b (mean displayed polypaint, sRGB 0-255)
//                            uvs (1 when the SubTool has UVs), texture_size (0 = no texture), paint_texture (1 when
//                            painting goes into the texture), texture_layers, active_texture_layer,
//                            texture_painted (texels of the texture composite that aren't white),
//                            texture_mean_r / texture_mean_g / texture_mean_b (mean composite, sRGB 0-255),
//                            auto_uv_running, qremesh_running (QRemesh working in the background),
//                            qremesh_guides (guide curves on the active SubTool), qremesh_guide_loops (closed ones),
//                            guides_mode (1 while the QRemesh Guides brush is picked),
//                            quads (quad pairs in the active SubTool), quad_percent (quads among its polygons)
//                            mask_shape (0 MaskPen, 1 MaskLasso, 2 MaskRect), hide_shape (1 SelectLasso, 2 SelectRect)
//                            sdiv (the current subdivision level, 1 = base; 0 without levels), sdiv_levels (0 = none)
//                            antialiasing (Preferences > Performance: 1, 2, 4, 8), view_samples (what the interactive
//                            view draws with now: 1 while Fast Navigation moves a big scene), multithreading (1 on),
//                            last_stroke_dabs (dabs of the last finished stroke)
//                            dynamic (1 while dynamic topology is on), dynamic_detail (its Detail, 0-100)
//                            last_stroke_dabs (dabs of the last finished stroke), stroke_type (0 Dots, 1 DragRect,
//                            2 DragDot, 3 Spray, 4 LazyMouse), vdms (VDM brush maps), vdm_files (.sfvdm in the library)
//                            z_intensity, smooth_intensity, rgb_intensity (0-100), focal_shift (-100..100), draw_size,
//                            quickmenu_open, lazy_mouse, input_style (0 ZBrush, 1 SculptForge, 2 Blender), turntable,
//                            camera_ortho, mask_brush (Blender's Mask brush on), shortcut_conflicts (keys two actions
//                            claim), brush_type (BrushType
//                            index, -1 for a custom brush or Paint), last_dab_x / last_dab_y (window pixels), paint_r
//                            gizmo_mode (0 Draw, 1 Move, 2 Scale, 3 Rotate), gizmo_hover (handle under the cursor,
//                            0 = none, in gizmo_drag's order from 1), gizmo_falloff, pivot_x / pivot_y / pivot_z,
//                            center_x / center_y / center_z and size_x / size_y / size_z (active mesh bounds)
//                            bake_running, baked_maps (maps in the last bake), bake_size, bake_files (files it wrote),
//                            normal_map_preview (1 when the active SubTool shows its baked normal map)
//                            splat_training (1 while a splat trains), splat_previews (checkpoints shown), splat_train_step,
//                            splat_train_stage (4 done, 5 failed, 6 cancelled), splat_train_psnr, splat_train_prompt
//                            sculptable_running, sculptable_review, sculptable_landmarks (8 when placed),
//                            sculptable_placing (next landmark to click, -1), sculptable_crop, sculptable_crop_height,
//                            show_splats, landmark_x_N / landmark_y_N / landmark_z_N (face landmark N, mesh space)
//                            phone_running (1 while the phone link listens), phone_uploads (uploads under way),
//                            phone_captures (captures received), phone_face_frames, phone_face_fps, phone_recording,
//                            phone_take_frames, phone_ar_version (AR models published), phone_ar_busy
//   (mesh values are the active SubTool's)
//   snapshot                 remember the active mesh's vertices (for the moved* values)
//   remember VALUE NAME      keep VALUE's current value as $NAME
//   shot NAME                save a screenshot NAME.png (to --ui-out)
//   expect_guide FILE        the User Guide shows FILE ("masking.md", "index.md")
//   dump_shortcuts NAME      write every default shortcut as a Markdown table to --ui-out/NAME
// A scripted phone for File > Capture from phone (link/PhoneLink.h), over real HTTP to the link on 127.0.0.1:
//   phone_upload KIND FILE... send files as the capture page does (KIND splat / photo / ply; FILE relative to the
//                            script), in 64 KB chunks with one lost chunk resumed, then finish the capture
//   phone_face NAME=W ... [frames=N]   N face messages (default 5) with these ARKit weights
//   phone_request METHOD PATH STATUS [BODY]   any request with the session token; fails unless the answer has
//                            STATUS (e.g. phone_request POST /api/ar 200 {"scope":"subtool","size":20})
//                            (tools/guide/update_shortcuts.py puts it into docs/guide/shortcuts.md)
//
// Simulated graphics tablet: real SDL3 pen events, each followed by the mouse event SDL synthesizes
// from it (SDL_PEN_MOUSEID), in SDL's order. The touch comes before its first pressure sample, as
// with Windows Ink, and every frame carries 4 pen samples (a 240 Hz pen at 60 fps).
//   pen_stroke U0 V0 U1 V1 P0 P1 [eraser] [leave] [S]   pen stroke across the model with pressure P0 -> P1;
//                            eraser: the eraser end; leave: the pen leaves proximity before lifting
//   pen_drag_item LABEL DX DY P0 P1 [S]   pen stroke through the item's centre, DX DY pixels long
//   pen_button N down|up     side button N (1 lower, 2 upper, 3 third) with SDL 3.4's synthesized click
//   pen_hover U V [S]        move the hovering pen to a point on the model (drags the view while a
//                            button mapped to rotate / pan / right-click is held)
//   pen_pressure P           a pressure sample from the hovering pen (e.g. a stale value before a stroke)
//   pen_out                  the pen leaves proximity
//   mouse_move U V           a plain mouse (not the pen) moves to a point on the model, e.g. the stray mouse
//                            events Windows Ink sends beside the pen's
#include "app/App.h"

#ifndef SDL_PEN_INPUT_IN_PROXIMITY
#define SDL_PEN_INPUT_IN_PROXIMITY (1u << 31) // SDL 3.4's flag; older SDL 3 never sets it
#endif
#include "app/Demo.h"
#include "app/Ui.h"

#include "core/BrushLibrary.h"
#include "core/Multires.h"
#include "core/Script.h"
#include "core/ThreadPool.h"
#include "link/HttpServer.h"

#include <imgui_internal.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <future>
#include <map>
#include <memory>
#include <set>
#include <sstream>

namespace sf {

namespace {

// Splits a line into words; "double quoted" words may contain spaces.
std::vector<std::string> splitWords(const std::string& line) {
    std::vector<std::string> out;
    size_t i = 0;
    while (i < line.size()) {
        while (i < line.size() && std::isspace(static_cast<unsigned char>(line[i]))) ++i;
        if (i >= line.size() || line[i] == '#') break;
        std::string w;
        if (line[i] == '"') {
            ++i;
            while (i < line.size() && line[i] != '"') w += line[i++];
            ++i;
        } else {
            while (i < line.size() && !std::isspace(static_cast<unsigned char>(line[i]))) w += line[i++];
        }
        out.push_back(w);
    }
    return out;
}

// Everything after the first `skip` words, verbatim (for `run` and `type`).
std::string restOf(const std::string& line, int skip) {
    size_t i = 0;
    for (int k = 0; k < skip; ++k) {
        while (i < line.size() && std::isspace(static_cast<unsigned char>(line[i]))) ++i;
        if (i < line.size() && line[i] == '"') {
            i = line.find('"', i + 1);
            i = i == std::string::npos ? line.size() : i + 1;
        } else {
            while (i < line.size() && !std::isspace(static_cast<unsigned char>(line[i]))) ++i;
        }
    }
    while (i < line.size() && std::isspace(static_cast<unsigned char>(line[i]))) ++i;
    std::string rest = line.substr(i);
    if (rest.size() >= 2 && rest.front() == '"' && rest.back() == '"') rest = rest.substr(1, rest.size() - 2);
    return rest;
}

SDL_Keymod parseMods(const std::string& text, std::string* key = nullptr) {
    SDL_Keymod mods = SDL_KMOD_NONE;
    std::stringstream ss(text);
    std::string part, last;
    while (std::getline(ss, part, '+')) {
        std::string p = part;
        std::transform(p.begin(), p.end(), p.begin(), [](unsigned char c) { return char(std::tolower(c)); });
        if (p == "ctrl" || p == "control") mods |= SDL_KMOD_LCTRL;
        else if (p == "shift") mods |= SDL_KMOD_LSHIFT;
        else if (p == "alt") mods |= SDL_KMOD_LALT;
        else if (p == "cmd" || p == "gui" || p == "super") mods |= SDL_KMOD_LGUI;
        else last = part;
    }
    if (key) *key = last;
    return mods;
}

template <class T> bool busy(const std::future<T>& f) {
    return f.valid() && f.wait_for(std::chrono::seconds(0)) != std::future_status::ready;
}

bool compare(double a, const std::string& op, double b) {
    if (op == "==") return std::abs(a - b) < 1e-6;
    if (op == "!=") return std::abs(a - b) >= 1e-6;
    if (op == "<") return a < b;
    if (op == "<=") return a <= b;
    if (op == ">") return a > b;
    if (op == ">=") return a >= b;
    return false;
}

std::string notFound(const std::string& label) {
    std::string msg = "no UI item labelled \"" + label + "\"";
    const auto similar = Demo::similarItems(label);
    for (size_t i = 0; i < similar.size(); ++i) msg += (i ? ", \"" : " (similar: \"") + similar[i] + "\"";
    if (!similar.empty()) msg += ")";
    return msg;
}

std::string number(double v) {
    std::ostringstream ss;
    ss << v;
    return ss.str();
}

} // namespace

void Demo::fail(int line, const std::string& message) {
    failures_.push_back(scriptName_ + ":" + std::to_string(line) + ": " + message);
    SDL_Log("FAIL %s", failures_.back().c_str());
    if (!shotDir_.empty()) shotRequest_ = shotDir_ + "/FAIL-line-" + std::to_string(line) + ".png";
    step_ = steps_.size(); // stop: later steps depend on this one
    // The frame loop still renders one more frame, so the failure screenshot shows the state.
}

double Demo::query(const std::string& name, bool& known) const {
    const Mesh& m = app_.doc_.mesh;
    known = true;
    if (name == "triangles") return double(m.triangleCount());
    if (name == "vertices") return double(m.vertexCount());
    if (name == "layers") return double(m.layers.size());
    if (name == "brushes") return double(app_.doc_.brushes.size());
    if (name == "colored") return m.hasColors() ? 1.0 : 0.0;
    if (name == "paint_layers") return double(m.paintLayers.size());
    if (name == "active_paint_layer") return m.paintLayers.empty() ? -1.0 : double(m.activePaintLayer);
    if (name == "paint_mean_r" || name == "paint_mean_g" || name == "paint_mean_b") {
        if (!m.hasColors()) return 255.0;
        const int k = name.back() == 'r' ? 0 : name.back() == 'g' ? 1 : 2;
        double sum = 0.0;
        for (const glm::vec3& c : m.colors) sum += double(linearToSrgb(c)[k]);
        return 255.0 * sum / double(m.colors.size());
    }
    if (name == "uvs") return m.hasUvs() ? 1.0 : 0.0;
    if (name == "sdiv") return subdivLevelCount(m) ? double(subdivCurrentLevel(m) + 1) : 0.0;
    if (name == "sdiv_levels") return double(subdivLevelCount(m));
    if (name == "antialiasing") return double(app_.uiPrefs_.antialiasing);
    if (name == "view_samples") return double(app_.perf_.samplesWanted);
    if (name == "multithreading") return ThreadPool::shared().serial() ? 0.0 : 1.0;
    if (name == "last_stroke_dabs") return double(app_.perf_.lastStroke.dabs);
    if (name == "texture_size") return hasTexture(m) ? double(m.texture->width) : 0.0;
    if (name == "paint_texture") return paintsTexture(m) ? 1.0 : 0.0;
    if (name == "texture_layers") return hasTexture(m) ? double(m.texture->layers.size()) : 0.0;
    if (name == "active_texture_layer") return hasTexture(m) && !m.texture->layers.empty() ? double(m.texture->activeLayer) : -1.0;
    if (name == "auto_uv_running") return app_.autoUv_ ? 1.0 : 0.0;
    if (const double v = app_.bakeTestValue(name); !std::isnan(v)) return v;
    if (const double v = app_.splatTrainTestValue(name); !std::isnan(v)) return v; // splat_training, ...
    if (const double v = app_.sculptableTestValue(name); !std::isnan(v)) return v;
    if (name == "qremesh_running") return app_.qremesh_ ? 1.0 : 0.0;
    if (name == "qremesh_guides") return double(app_.doc_.qremeshGuides.size());
    if (name == "qremesh_guide_loops") {
        size_t n = 0;
        for (const QRemeshGuide& g : app_.doc_.qremeshGuides) n += g.closed;
        return double(n);
    }
    if (name == "guides_mode") return app_.guidesMode_ ? 1.0 : 0.0;
    if (name == "quads") return double(m.quadCount());
    if (name == "quad_percent")
        return m.triangleCount() ? 100.0 * double(m.quadCount()) / double(m.triangleCount() - m.quadCount()) : 0.0;
    if (name == "texture_painted" || name.rfind("texture_mean_", 0) == 0) {
        if (!hasTexture(m)) return 0.0;
        const std::vector<uint8_t>& px = m.texture->pixels;
        double painted = 0.0, sum[3] = {0, 0, 0};
        for (size_t i = 0; i < px.size(); i += 4) {
            if (px[i] != 255 || px[i + 1] != 255 || px[i + 2] != 255) painted += 1.0;
            for (int k = 0; k < 3; ++k) sum[k] += px[i + size_t(k)];
        }
        if (name == "texture_painted") return painted;
        const int k = name.back() == 'r' ? 0 : name.back() == 'g' ? 1 : 2;
        return sum[k] / double(px.size() / 4);
    }
    // ZBrush feel (Input.cpp): the intensities and Focal Shift as shown (0-100, -100..100).
    if (name == "z_intensity") return std::round(app_.strength_ * 100.0f);
    if (name == "smooth_intensity") return std::round(app_.smoothStrength_ * 100.0f);
    if (name == "rgb_intensity") return std::round(app_.rgbIntensity_ * 100.0f);
    if (name == "focal_shift") return std::round(app_.focalShift_ * 100.0f);
    if (name == "draw_size") return std::round(app_.radiusPx_);
    if (name == "quickmenu_open") return app_.quickMenu_.open ? 1.0 : 0.0;
    if (name == "lazy_mouse") return app_.strokeType_ == StrokeType::LazyMouse ? 1.0 : 0.0;
    if (name == "stroke_type") return double(int(app_.strokeType_)); // 0 Dots, 1 DragRect, 2 DragDot, 3 Spray, 4 LazyMouse
    if (name == "vdms") return double(app_.doc_.vdms.size());
    // Facial shapes (PanelFace.cpp): landmarks placed, placing them, a take playing, and face_<shape>: that layer's weight.
    if (name == "face_landmarks") return double(app_.faceLandmarks_.count());
    if (name == "face_placing") return app_.facePlacing_ ? 1.0 : 0.0;
    if (name == "face_take_playing") return app_.faceTakePlaying_ ? 1.0 : 0.0;
    if (name.rfind("face_", 0) == 0) {
        for (const auto& l : m.layers)
            if (l.name == name.substr(5)) return double(l.weight);
        return -1.0; // no such layer
    }
    if (name == "input_style") return double(app_.uiPrefs_.inputStyle);
    if (name == "turntable") return app_.uiPrefs_.turntable ? 1.0 : 0.0;
    if (name == "camera_ortho") return app_.camera_.ortho ? 1.0 : 0.0;
    if (name == "mask_brush") return app_.maskBrushActive() ? 1.0 : 0.0;
    if (name == "shortcut_conflicts") { // keys that two actions claim (only the first would ever run)
        int conflicts = 0;
        std::vector<Chord> seen;
        for (const Action& a : app_.actions_.all())
            for (const Chord& c : parseChords(app_.actions_.shortcutText(a.id))) {
                if (std::find(seen.begin(), seen.end(), c) != seen.end()) ++conflicts;
                else seen.push_back(c);
            }
        return conflicts;
    }
    if (name == "mask_shape") return double(int(app_.maskShape_)); // 0 MaskPen, 1 MaskLasso, 2 MaskRect
    if (name == "hide_shape") return double(int(app_.hideShape_)); // 1 SelectLasso, 2 SelectRect
    if (name.rfind("phone_", 0) == 0) {
        const bool on = app_.phone_ && app_.phone_->running();
        const link::PhoneLinkStatus st = on ? app_.phone_->status() : link::PhoneLinkStatus{};
        if (name == "phone_running") return on ? 1.0 : 0.0;
        if (name == "phone_uploads")
            return double(std::count_if(st.uploads.begin(), st.uploads.end(), [](const auto& u) { return !u.finished; }));
        if (name == "phone_captures") return double(st.capturesReceived);
        if (name == "phone_face_frames") return double(st.faceFrames);
        if (name == "phone_face_fps") return st.faceFps;
        if (name == "phone_recording") return st.recording ? 1.0 : 0.0;
        if (name == "phone_take_frames") return double(st.takeFrames);
        if (name == "phone_ar_version") return double(st.arVersion);
        if (name == "phone_ar_busy") return st.arBusy || app_.phoneBusy() ? 1.0 : 0.0;
    }
    if (name == "dynamic") return app_.doc_.sculptor.dyntopo.enabled ? 1.0 : 0.0;
    if (name == "dynamic_detail") return std::round(app_.doc_.sculptor.dyntopo.detail * 100.0f);
    if (name == "brush_type") return app_.doc_.activeBrush >= 0 || app_.paintMode_ ? -1.0 : double(int(app_.brush_));
    if (name == "last_dab_x") return app_.lastDabScreen_.x;
    if (name == "last_dab_y") return app_.lastDabScreen_.y;
    if (name == "paint_r") return std::round(255.0f * linearToSrgb(app_.doc_.paintColor).x);
    if (name == "gizmo_mode") return double(int(app_.gizmo_.mode));
    if (name == "gizmo_hover") return double(int(app_.gizmo_.hover));
    if (name == "gizmo_falloff") return app_.gizmo_.falloff;
    if (name == "pivot_x") return app_.gizmo_.pivot.x;
    if (name == "pivot_y") return app_.gizmo_.pivot.y;
    if (name == "pivot_z") return app_.gizmo_.pivot.z;
    if ((name.rfind("center_", 0) == 0 && name.size() == 8) || (name.rfind("size_", 0) == 0 && name.size() == 6)) {
        const AABB b = m.bounds();
        const int k = name.back() == 'x' ? 0 : name.back() == 'y' ? 1 : 2;
        return name[0] == 'c' ? b.center()[k] : b.extent()[k];
    }
    if (name == "can_undo") return app_.doc_.undo.canUndo() ? 1.0 : 0.0;
    if (name == "dirty") return app_.doc_.dirty() ? 1.0 : 0.0;
    if (name == "splats") return double(app_.doc_.splats.size());
    if (name == "show_polygroups") return app_.renderSettings_.showPolygroups ? 1.0 : 0.0;
    if (name == "show_mask") return app_.renderSettings_.showMask ? 1.0 : 0.0;
    if (name == "show_mesh") return app_.renderSettings_.showMesh ? 1.0 : 0.0;
    if (name == "wireframe") return app_.renderSettings_.wireframe ? 1.0 : 0.0;
    if (name == "shelves") return app_.layout_.shelvesVisible ? 1.0 : 0.0;
    if (name == "left_tray") return app_.layout_.leftTrayOpen ? double(app_.layout_.leftTray.size()) : -1.0;
    if (name == "right_tray") return app_.layout_.rightTrayOpen ? double(app_.layout_.rightTray.size()) : -1.0;
    if (name == "ui_config") {
        std::error_code ec;
        const std::string p = app_.uiConfigPath();
        return !p.empty() && std::filesystem::is_regular_file(std::filesystem::path(std::u8string(p.begin(), p.end())), ec) ? 1.0 : 0.0;
    }
    if (name == "console_open") return app_.layout_.consoleOpen ? 1.0 : 0.0;
    if (name == "command_palette_open") return app_.palette_.isOpen() ? 1.0 : 0.0;
    if (name == "customize") return app_.layout_.customize ? 1.0 : 0.0;
    if (name == "shelves_visible") return app_.layout_.shelvesVisible ? 1.0 : 0.0;
    if (name == "right_tray_open") return app_.layout_.rightTrayOpen ? 1.0 : 0.0;
    if (name == "left_tray_open") return app_.layout_.leftTrayOpen ? 1.0 : 0.0;
    if (name == "lightbox_open") return app_.lightbox_.open() ? 1.0 : 0.0;
    if (name == "guide_open") return app_.guide_.isOpen() ? 1.0 : 0.0;
    if (name == "guide_hits") return double(app_.guide_.searchHits());
    const Document& doc = app_.doc_;
    if (name == "subtools") return double(doc.subtoolCount());
    if (name == "active_subtool") return double(doc.activeSubtool);
    if (name == "visible_subtools")
        return double(std::count_if(doc.subtools.begin(), doc.subtools.end(), [](const SubTool& s) { return s.visible; }));
    if (name == "edited_subtools") { // SubTools with undo history (the active one's lives in doc.undo)
        int n = 0;
        for (int i = 0; i < doc.subtoolCount(); ++i)
            n += (i == doc.activeSubtool ? doc.undo.canUndo() : doc.subtools[size_t(i)].undo.canUndo()) ? 1 : 0;
        return double(n);
    }
    if (name == "user_brushes")
        return double(std::count_if(doc.brushes.begin(), doc.brushes.end(), [](const BrushRecipe& b) { return isUserLibraryBrush(b); }));
    if (name == "library_files" || name == "vdm_files") { // .sfbrush / .sfvdm files in the user library
        const char* ext = name == "vdm_files" ? ".sfvdm" : ".sfbrush";
        const std::string& dir = brushLibraryPaths().userDir;
        std::error_code ec;
        size_t n = 0;
        if (!dir.empty())
            for (std::filesystem::directory_iterator it(std::filesystem::path(std::u8string(dir.begin(), dir.end())), ec), end;
                 !ec && it != end; it.increment(ec))
                n += it->path().extension() == ext;
        return double(n);
    }
    if (name == "masked") {
        size_t n = 0;
        for (float v : m.mask) n += v > 0.5f;
        return double(n);
    }
    if (name == "hidden") {
        size_t n = 0;
        for (uint8_t h : m.hidden) n += h != 0;
        return double(n);
    }
    if (name == "moved_masked") { // masked vertices (mask >= 0.5) that moved since `snapshot`
        if (basePositions_.size() != m.vertexCount()) return -1.0;
        double count = 0.0;
        for (size_t v = 0; v < basePositions_.size(); ++v)
            count += m.maskAt(uint32_t(v)) >= 0.5f && glm::length(m.positions[v] - basePositions_[v]) > 1e-6f ? 1.0 : 0.0;
        return count;
    }
    if (name == "moved" || name == "moved_max" || name == "moved_sum" || name == "moved_out") {
        if (basePositions_.size() != m.vertexCount()) return -1.0; // no snapshot, or the topology changed
        double count = 0.0, maxD = 0.0, sum = 0.0, out = 0.0;
        for (size_t v = 0; v < basePositions_.size(); ++v) {
            const glm::vec3 d = m.positions[v] - basePositions_[v];
            const double len = glm::length(d);
            count += len > 1e-6 ? 1.0 : 0.0;
            maxD = std::max(maxD, len);
            sum += len;
            if (v < baseNormals_.size()) out += glm::dot(d, baseNormals_[v]);
        }
        return name == "moved" ? count : name == "moved_max" ? maxD : name == "moved_sum" ? sum : out;
    }
    if (name == "pen_pressure") return app_.penTracker_.raw();
    if (name == "brush_pressure") return app_.strokePressure();
    if (name == "stroking") return app_.drag_ == App::Drag::Stroke ? 1.0 : 0.0;
    if (name == "cursor_x") return app_.mouseX_;
    if (name == "cursor_y") return app_.mouseY_;
    if (name == "camera_yaw") return glm::degrees(app_.camera_.yaw);
    if (name == "camera_pitch") return glm::degrees(app_.camera_.pitch);
    if (name == "camera_roll") return glm::degrees(app_.camera_.roll);
    if (name == "camera_up") return (app_.camera_.orientation() * glm::vec3(0, 1, 0)).y;
    if (name == "camera_aligned") {
        const glm::vec3 f = glm::abs(app_.camera_.forward());
        return std::max({f.x, f.y, f.z}) > 0.99999f ? 1.0 : 0.0;
    }
    if (name == "camera_distance") return app_.camera_.distance;
    if (name == "camera_pan") return glm::length(app_.camera_.target);
    if (name == "groups") {
        std::set<uint32_t> ids(m.polygroups.begin(), m.polygroups.end());
        return double(std::max<size_t>(ids.size(), m.triangleCount() ? 1 : 0));
    }
    known = false;
    return 0.0;
}

void Demo::testClick(int line, const std::string& label, int button, bool doubleClick) {
    // Find the item (give the UI a moment to appear), glide there, press and release.
    steps_.push_back([this, line, label](float t) {
        ImVec2 to;
        bool visible = false;
        if (!findItem(label, to, &visible)) {
            if (t > 3.0f) fail(line, notFound(label));
            return t > 3.0f;
        }
        if (!visible) {
            // Scrolled out of its panel: wheel the panel towards it, as a person would.
            ImVec4 clip;
            findItem(label, to, &visible, &clip);
            if (clip.z > clip.x && clip.w > clip.y && t <= 2.0f) {
                mouseMotion((clip.x + clip.z) * 0.5f, (clip.y + clip.w) * 0.5f);
                SDL_Event e{};
                e.type = SDL_EVENT_MOUSE_WHEEL;
                e.wheel.windowID = SDL_GetWindowID(app_.window_);
                e.wheel.y = to.y > clip.w ? -1.0f : 1.0f;
                e.wheel.mouse_x = mouse_.x;
                e.wheel.mouse_y = mouse_.y;
                app_.handleEvent(e);
                return false;
            }
            // Off the screen, or the panel won't scroll: a person couldn't click it either.
            if (t > 1.0f) fail(line, "\"" + label + "\" is not visible (off screen or scrolled out of view)");
            return t > 1.0f;
        }
        mouseMotion(to.x, to.y);
        return true;
    });
    steps_.push_back([this, button, doubleClick](float t) {
        // A frame of hover first, so ImGui registers the item as hovered before the press.
        if (t == 0.0f) return false;
        mouseButton(true, button);
        mouseButton(false, button);
        if (doubleClick) {
            SDL_Event e{};
            e.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
            e.button.windowID = SDL_GetWindowID(app_.window_);
            e.button.button = Uint8(button);
            e.button.down = true;
            e.button.clicks = 2;
            e.button.x = mouse_.x;
            e.button.y = mouse_.y;
            app_.handleEvent(e);
            e.type = SDL_EVENT_MOUSE_BUTTON_UP;
            e.button.down = false;
            app_.handleEvent(e);
        }
        return true;
    });
    wait(0.1f);
}

void Demo::testKey(int line, const std::string& combo) {
    std::string keyName;
    const SDL_Keymod mods = parseMods(combo, &keyName);
    std::string lower = keyName;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    if (lower == "enter") keyName = "Return"; // SDL's name for it
    else if (lower == "esc") keyName = "Escape";
    else if (lower == "comma") keyName = ",";
    else if (lower == "backtick" || lower == "grave") keyName = "`";
    else if (lower == "tab") keyName = "Tab";
    else if (lower == "f3") keyName = "F3";
    else if (lower == "space") keyName = "Space";
    else if (lower == "minus") keyName = "-";
    else if (lower.rfind("numpad", 0) == 0) { // numpad1 .. numpad0, numpad., numpadplus, numpadminus
        const std::string k = lower.substr(6);
        keyName = "Keypad " + (k == "plus" ? std::string("+") : k == "minus" ? std::string("-") : k);
    }
    const SDL_Keycode key = SDL_GetKeyFromName(keyName.c_str());
    if (key == SDLK_UNKNOWN) {
        run([this, line, keyName] { fail(line, "unknown key \"" + keyName + "\""); });
        return;
    }
    steps_.push_back([this, mods, key](float) {
        const SDL_Keymod before = SDL_GetModState();
        SDL_SetModState(mods | held_);
        SDL_Event e{};
        e.type = SDL_EVENT_KEY_DOWN;
        e.key.windowID = SDL_GetWindowID(app_.window_);
        e.key.key = key;
        e.key.scancode = SDL_GetScancodeFromKey(key, nullptr);
        e.key.mod = mods | held_;
        e.key.down = true;
        app_.handleEvent(e);
        e.type = SDL_EVENT_KEY_UP;
        e.key.down = false;
        e.key.mod = held_; // the combo's modifiers are released with the key
        app_.handleEvent(e);
        SDL_SetModState(before);
        return true;
    });
    wait(0.1f);
}

bool Demo::parseStep(int line, const std::vector<std::string>& w, std::string& error) {
    const std::string& verb = w[0];
    auto need = [&](size_t n) {
        if (w.size() >= n + 1) return true;
        error = "'" + verb + "' needs " + std::to_string(n) + " argument" + (n == 1 ? "" : "s");
        return false;
    };
    auto num = [&](size_t i, float fallback) { return i < w.size() ? std::strtof(w[i].c_str(), nullptr) : fallback; };

    if (verb == "click" || verb == "double_click" || verb == "right_click") {
        if (!need(1)) return false;
        testClick(line, w[1], verb == "right_click" ? SDL_BUTTON_RIGHT : SDL_BUTTON_LEFT, verb == "double_click");
    } else if (verb == "hover") {
        if (!need(1)) return false;
        const std::string label = w[1];
        steps_.push_back([this, line, label](float t) {
            ImVec2 to;
            bool visible = false;
            ImVec4 clip;
            if (findItem(label, to, &visible, &clip)) {
                // Scrolled out of its panel: wheel the panel towards it first (as click does).
                if (!visible && clip.z > clip.x && clip.w > clip.y && t <= 2.0f) {
                    mouseMotion((clip.x + clip.z) * 0.5f, (clip.y + clip.w) * 0.5f);
                    SDL_Event e{};
                    e.type = SDL_EVENT_MOUSE_WHEEL;
                    e.wheel.windowID = SDL_GetWindowID(app_.window_);
                    e.wheel.y = to.y > clip.w ? -1.0f : 1.0f;
                    e.wheel.mouse_x = mouse_.x;
                    e.wheel.mouse_y = mouse_.y;
                    app_.handleEvent(e);
                    return false;
                }
                mouseMotion(to.x, to.y);
                return true;
            }
            if (t > 3.0f) fail(line, notFound(label));
            return t > 3.0f;
        });
        wait(0.6f); // tooltips appear after a short delay
    } else if (verb == "type") {
        if (!need(2)) return false;
        testClick(line, w[1], SDL_BUTTON_LEFT, false);
        auto text = std::make_shared<std::string>(restOf(lineText_, 2));
        steps_.push_back([this, text](float) {
            // Select what's there, then type over it like a person would.
            testKeyNow(SDLK_A, SDL_KMOD_LCTRL);
            SDL_Event e{};
            e.type = SDL_EVENT_TEXT_INPUT;
            e.text.windowID = SDL_GetWindowID(app_.window_);
            e.text.text = text->c_str();
            app_.handleEvent(e);
            return true;
        });
        wait(0.1f);
    } else if (verb == "key") {
        if (!need(1)) return false;
        testKey(line, w[1]);
    } else if (verb == "hold") {
        if (!need(1)) return false;
        const SDL_Keymod mods = w[1] == "none" ? SDL_KMOD_NONE : parseMods(w[1]);
        run([this, mods] {
            held_ = mods;
            SDL_SetModState(mods);
        });
    } else if (verb == "stroke") {
        if (!need(4)) return false;
        const float u0 = num(1, 0), v0 = num(2, 0), u1 = num(3, 0), v1 = num(4, 0), seconds = num(5, 0.6f);
        auto ends = std::make_shared<std::pair<ImVec2, ImVec2>>();
        run([this, ends, u0, v0, u1, v1] { *ends = {onModel(u0, v0), onModel(u1, v1)}; });
        stroke([ends](float u) {
            return ImVec2(ends->first.x + (ends->second.x - ends->first.x) * u,
                          ends->first.y + (ends->second.y - ends->first.y) * u);
        }, seconds);
        run([this] { SDL_SetModState(held_); });
        wait(0.1f);
    } else if (verb == "click_subtool") {
        if (!need(1)) return false;
        const std::string name = w[1];
        const float u = num(2, 0.5f), v = num(3, 0.5f);
        steps_.push_back([this, line, name, u, v](float t) {
            const int index = app_.doc_.findSubtool(name);
            if (index < 0) {
                fail(line, "no SubTool named \"" + name + "\"");
                return true;
            }
            // The point of its on-screen bounds nearest (U, V) where it's the front SubTool and no panel covers it.
            const AABB b = app_.doc_.subtoolMesh(index).bounds();
            ImVec2 lo(1e9f, 1e9f), hi(-1e9f, -1e9f);
            for (int i = 0; i < 8; ++i) {
                const ImVec2 p = project(glm::vec3((i & 1) ? b.max.x : b.min.x, (i & 2) ? b.max.y : b.min.y,
                                                   (i & 4) ? b.max.z : b.min.z));
                lo = ImVec2(std::min(lo.x, p.x), std::min(lo.y, p.y));
                hi = ImVec2(std::max(hi.x, p.x), std::max(hi.y, p.y));
            }
            const ImVec2 want(lo.x + (hi.x - lo.x) * u, lo.y + (hi.y - lo.y) * v);
            auto covered = [](ImVec2 q) {
                for (ImGuiWindow* w : ImGui::GetCurrentContext()->Windows)
                    if (w->WasActive && !w->Hidden && w->Rect().Contains(q)) return true;
                return false;
            };
            ImVec2 p;
            bool found = false;
            float best = 1e30f;
            constexpr int kGrid = 16;
            for (int gy = 0; gy <= kGrid; ++gy) {
                for (int gx = 0; gx <= kGrid; ++gx) {
                    const ImVec2 q(lo.x + (hi.x - lo.x) * float(gx) / kGrid, lo.y + (hi.y - lo.y) * float(gy) / kGrid);
                    const float d = (q.x - want.x) * (q.x - want.x) + (q.y - want.y) * (q.y - want.y);
                    if (d >= best || covered(q) || app_.pickSubtool(q.x, q.y) != index) continue;
                    p = q;
                    best = d;
                    found = true;
                }
            }
            if (!found) {
                fail(line, "SubTool \"" + name + "\" can't be clicked (hidden, covered or off screen)");
                return true;
            }
            if (t == 0.0f) { // hover for a frame first
                mouseMotion(p.x, p.y);
                return false;
            }
            mouseButton(true, SDL_BUTTON_LEFT);
            mouseButton(false, SDL_BUTTON_LEFT);
            return true;
        });
        wait(0.1f);
    } else if (verb == "drag") {
        if (!need(4)) return false;
        const float x0 = num(1, 0), y0 = num(2, 0), x1 = num(3, 0), y1 = num(4, 0), seconds = num(5, 0.4f);
        const bool release = w.size() > 6 && w[6] == "release";
        steps_.push_back([this, x0, y0, x1, y1, seconds, release](float t) {
            int ww = 0, wh = 0;
            SDL_GetWindowSize(app_.window_, &ww, &wh);
            const ImVec2 a(x0 * float(ww), y0 * float(wh)), b(x1 * float(ww), y1 * float(wh));
            if (t == 0.0f) {
                mouseMotion(a.x, a.y);
                return false;
            }
            if (!buttonDown_ && t < seconds) mouseButton(true, SDL_BUTTON_LEFT);
            const float u = std::min(t / seconds, 1.0f);
            if (release && u >= 0.5f && held_ != SDL_KMOD_NONE) {
                held_ = SDL_KMOD_NONE;
                SDL_SetModState(SDL_KMOD_NONE);
            }
            mouseMotion(a.x + (b.x - a.x) * u, a.y + (b.y - a.y) * u);
            if (t >= seconds) {
                mouseButton(false, SDL_BUTTON_LEFT);
                return true;
            }
            return false;
        });
        wait(0.1f);
    } else if (verb == "button_drag") { // button_drag middle|right X0 Y0 X1 Y1 [S]: navigation drags
        if (!need(5)) return false;
        const int button = w[1] == "middle" ? SDL_BUTTON_MIDDLE : w[1] == "right" ? SDL_BUTTON_RIGHT : 0;
        if (!button) {
            error = "button_drag needs middle or right";
            return false;
        }
        const float x0 = num(2, 0), y0 = num(3, 0), x1 = num(4, 0), y1 = num(5, 0), seconds = num(6, 0.4f);
        auto down = std::make_shared<bool>(false);
        steps_.push_back([this, button, down, x0, y0, x1, y1, seconds](float t) {
            int ww = 0, wh = 0;
            SDL_GetWindowSize(app_.window_, &ww, &wh);
            const ImVec2 a(x0 * float(ww), y0 * float(wh)), b(x1 * float(ww), y1 * float(wh));
            if (t == 0.0f) {
                mouseMotion(a.x, a.y);
                return false;
            }
            if (!*down) {
                SDL_SetModState(held_);
                mouseButton(true, button);
                *down = true;
            }
            const float u = std::min(t / seconds, 1.0f);
            mouseMotion(a.x + (b.x - a.x) * u, a.y + (b.y - a.y) * u);
            if (t < seconds) return false;
            mouseButton(false, button);
            return true;
        });
        wait(0.1f);
    } else if (verb == "expect_shortcut") { // expect_shortcut ACTION KEYS: the action's keys (none: no key)
        if (!need(1)) return false;
        const std::string id = w[1], want = restOf(lineText_, 2);
        run([this, line, id, want] {
            std::string got = app_.actions_.shortcutText(id);
            if (!app_.actions_.find(id)) fail(line, "no action " + id);
            else if (got != want) fail(line, id + " has \"" + got + "\", expected \"" + want + "\"");
        });
    } else if (verb == "drag_landmark") {
        if (!need(3)) return false;
        const int index = int(num(1, 0));
        const float dx = num(2, 0), dy = num(3, 0), seconds = num(4, 0.4f);
        auto start = std::make_shared<ImVec2>();
        steps_.push_back([this, line, index, dx, dy, seconds, start](float t) {
            if (t == 0.0f) {
                if (!app_.sculptableLandmarkScreen(index, *start)) {
                    fail(line, "face landmark " + std::to_string(index) + " is not shown (Make sculptable's review)");
                    return true;
                }
                mouseMotion(start->x, start->y);
                return false;
            }
            if (!buttonDown_ && t < seconds) mouseButton(true, SDL_BUTTON_LEFT);
            const float u = std::min(t / seconds, 1.0f);
            mouseMotion(start->x + dx * u, start->y + dy * u);
            if (t >= seconds) {
                mouseButton(false, SDL_BUTTON_LEFT);
                return true;
            }
            return false;
        });
        wait(0.1f);
    } else if (verb == "gizmo_drag" || verb == "gizmo_hover") {
        static const char* kHandles[] = {"move_x",    "move_y",   "move_z",   "plane_yz",    "plane_xz",
                                         "plane_xy",  "move_view", "rotate_x", "rotate_y",   "rotate_z",
                                         "rotate_view", "scale_x", "scale_y",  "scale_z",     "scale_uniform"};
        if (!need(verb == "gizmo_drag" ? 3 : 1)) return false;
        int index = -1;
        for (int i = 0; i < int(std::size(kHandles)); ++i)
            if (w[1] == kHandles[i]) index = i + 1;
        if (index < 0) {
            error = "unknown gizmo handle '" + w[1] + "'";
            return false;
        }
        const auto handle = App::GizmoHandle(index);
        const bool drag = verb == "gizmo_drag";
        const bool keep = drag && w.back() == "keep";
        const float dx = drag ? num(2, 0) : 0.0f, dy = drag ? num(3, 0) : 0.0f;
        const float seconds = drag && w.size() > 4 && w[4] != "keep" ? num(4, 0.5f) : 0.5f;
        auto from = std::make_shared<ImVec2>();
        const std::string name = w[1];
        steps_.push_back([this, line, handle, from, name](float t) {
            if (app_.gizmoHandleScreen(handle, *from)) {
                mouseMotion(from->x, from->y);
                return true;
            }
            if (t > 1.0f) fail(line, "gizmo handle " + name + " isn't on screen (is the gizmo in that mode?)");
            return t > 1.0f;
        });
        if (drag) {
            steps_.push_back([this, from, dx, dy, seconds, keep](float t) {
                if (t == 0.0f) return false; // a frame of hover first
                if (!buttonDown_ && t < seconds) mouseButton(true, SDL_BUTTON_LEFT);
                const float u = std::min(t / seconds, 1.0f);
                mouseMotion(from->x + dx * u, from->y + dy * u);
                if (t >= seconds) {
                    if (!keep) mouseButton(false, SDL_BUTTON_LEFT);
                    return true;
                }
                return false;
            });
        }
        wait(0.1f);
    } else if (verb == "drag_item") {
        if (!need(3)) return false;
        const std::string label = w[1];
        const float dx = num(2, 0), dy = num(3, 0), seconds = num(4, 0.4f);
        auto from = std::make_shared<ImVec2>();
        steps_.push_back([this, line, label, from](float t) {
            if (findItem(label, *from)) {
                mouseMotion(from->x, from->y);
                return true;
            }
            if (t > 3.0f) fail(line, notFound(label));
            return t > 3.0f;
        });
        steps_.push_back([this, from, dx, dy, seconds](float t) {
            if (t == 0.0f) return false; // a frame of hover first
            if (!buttonDown_ && t < seconds) mouseButton(true, SDL_BUTTON_LEFT);
            const float u = std::min(t / seconds, 1.0f);
            mouseMotion(from->x + dx * u, from->y + dy * u);
            if (t >= seconds) {
                mouseButton(false, SDL_BUTTON_LEFT);
                return true;
            }
            return false;
        });
        wait(0.1f);
    } else if (verb == "drag_path") {
        // Points first, then the optional words: "keep" and "S=seconds".
        std::vector<glm::vec2> uv;
        bool keep = false;
        float seconds = 0.6f;
        std::vector<float> nums;
        for (size_t i = 1; i < w.size(); ++i) {
            if (w[i] == "keep") keep = true;
            else if (w[i].rfind("S=", 0) == 0) seconds = std::strtof(w[i].c_str() + 2, nullptr);
            else nums.push_back(std::strtof(w[i].c_str(), nullptr));
        }
        if (nums.size() < 4 || nums.size() % 2) {
            error = "'drag_path' needs at least 2 points (U V pairs)";
            return false;
        }
        for (size_t i = 0; i + 1 < nums.size(); i += 2) uv.push_back({nums[i], nums[i + 1]});
        auto points = std::make_shared<std::vector<ImVec2>>();
        steps_.push_back([this, uv, points](float t) {
            // The model's on-screen bounds, without onModel's walk onto the surface: points may be on the background.
            const AABB b = app_.doc_.mesh.bounds();
            ImVec2 lo(1e9f, 1e9f), hi(-1e9f, -1e9f);
            for (int i = 0; i < 8; ++i) {
                const ImVec2 p = project(glm::vec3((i & 1) ? b.max.x : b.min.x, (i & 2) ? b.max.y : b.min.y,
                                                   (i & 4) ? b.max.z : b.min.z));
                lo = ImVec2(std::min(lo.x, p.x), std::min(lo.y, p.y));
                hi = ImVec2(std::max(hi.x, p.x), std::max(hi.y, p.y));
            }
            points->clear();
            for (const glm::vec2& q : uv) points->push_back(ImVec2(lo.x + (hi.x - lo.x) * q.x, lo.y + (hi.y - lo.y) * q.y));
            if (t == 0.0f) { // hover the start for a frame first
                mouseMotion(points->front().x, points->front().y);
                return false;
            }
            return true;
        });
        steps_.push_back([this, points, keep, seconds](float t) {
            if (!buttonDown_) {
                SDL_SetModState(held_);
                mouseButton(true, SDL_BUTTON_LEFT);
            }
            // Walk the polyline at constant speed.
            const std::vector<ImVec2>& p = *points;
            std::vector<float> at(p.size(), 0.0f);
            for (size_t i = 1; i < p.size(); ++i) at[i] = at[i - 1] + std::hypot(p[i].x - p[i - 1].x, p[i].y - p[i - 1].y);
            const float d = std::min(t / seconds, 1.0f) * at.back();
            size_t i = 1;
            while (i + 1 < p.size() && at[i] < d) ++i;
            const float u = at[i] > at[i - 1] ? std::clamp((d - at[i - 1]) / (at[i] - at[i - 1]), 0.0f, 1.0f) : 1.0f;
            mouseMotion(p[i - 1].x + (p[i].x - p[i - 1].x) * u, p[i - 1].y + (p[i].y - p[i - 1].y) * u);
            if (t < seconds) return false;
            if (!keep) mouseButton(false, SDL_BUTTON_LEFT);
            return true;
        });
        wait(0.1f);
    } else if (verb == "release") {
        run([this] {
            SDL_SetModState(held_);
            if (buttonDown_) mouseButton(false, SDL_BUTTON_LEFT);
        });
        wait(0.1f);
    } else if (verb == "orbit") {
        if (!need(2)) return false;
        orbit(num(1, 0), num(2, 0), 0.4f);
    } else if (verb == "camera") {
        if (!need(2)) return false;
        run([this, yaw = num(1, 0), pitch = num(2, 0)] {
            app_.camera_.yaw = glm::radians(float(yaw));
            app_.camera_.pitch = glm::radians(float(pitch));
            app_.camera_.roll = 0.0f;
        });
        wait(0.1f);
    } else if (verb == "zoom") { // zoom FACTOR: the camera's distance divided by FACTOR
        if (!need(1)) return false;
        run([this, f = num(1, 1)] { app_.camera_.distance /= std::max(float(f), 1e-3f); });
        wait(0.1f);
    } else if (verb == "look") { // look X Y Z: the camera turns about (and looks at) this point
        if (!need(3)) return false;
        run([this, x = num(1, 0), y = num(2, 0), z = num(3, 0)] { app_.camera_.target = glm::vec3(float(x), float(y), float(z)); });
        wait(0.1f);
    } else if (verb == "drop") {
        if (!need(1)) return false;
        auto file = std::filesystem::path(std::u8string(w[1].begin(), w[1].end()));
        if (file.is_relative()) file = std::filesystem::path(std::u8string(scriptDir_.begin(), scriptDir_.end())) / file;
        const auto u8 = file.lexically_normal().u8string();
        run([this, path = std::string(u8.begin(), u8.end())] { app_.openPathConfirmed(path); });
    } else if (verb == "copy") {
        if (!need(2)) return false;
        namespace fs = std::filesystem;
        fs::path src(std::u8string(w[1].begin(), w[1].end())), dst(std::u8string(w[2].begin(), w[2].end()));
        if (src.is_relative()) src = fs::path(std::u8string(scriptDir_.begin(), scriptDir_.end())) / src;
        run([src, dst] {
            std::error_code ec;
            if (dst.has_parent_path()) fs::create_directories(dst.parent_path(), ec);
            fs::copy(src, dst, fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
            if (ec) SDL_Log("copy %s: %s", src.string().c_str(), ec.message().c_str());
        });
    } else if (verb == "phone_upload" || verb == "phone_face" || verb == "phone_request") {
        if (!need(verb == "phone_request" ? 3 : verb == "phone_upload" ? 2 : 1)) return false;
        std::vector<std::string> args(w.begin() + 1, w.end());
        const std::string body = verb == "phone_request" ? restOf(lineText_, 4) : std::string();
        run([this, line, verb, args, body] {
            if (!app_.phone_ || !app_.phone_->running()) {
                fail(line, "the phone link isn't running (open File > Capture from phone first)");
                return;
            }
            const uint16_t port = app_.phone_->port();
            const std::vector<std::pair<std::string, std::string>> headers = {
                {"X-SF-Token", app_.phone_->token()},
                {"User-Agent", "Mozilla/5.0 (iPhone; CPU iPhone OS 18_0 like Mac OS X) AppleWebKit/605.1.15 Version/18.0 Mobile/15E148 Safari/604.1"}};
            auto call = [&](const std::string& method, const std::string& target, const std::string& data) {
                return link::httpLocalRequest("127.0.0.1", port, method, target, data, headers);
            };
            auto jsonOf = [](const std::string& text) {
                Json j;
                std::string err;
                Json::parse(text, j, err);
                return j;
            };
            if (verb == "phone_request") {
                const link::HttpClientResponse r = call(args[0], args[1], body);
                if (std::to_string(r.status) != args[2])
                    fail(line, args[0] + " " + args[1] + " answered " + std::to_string(r.status) + " " + r.body + r.error);
                return;
            }
            if (verb == "phone_face") {
                int frames = 5;
                std::string weights;
                for (const std::string& a : args) {
                    const size_t eq = a.find('=');
                    if (eq == std::string::npos) continue;
                    if (a.compare(0, eq, "frames") == 0) frames = std::atoi(a.c_str() + eq + 1);
                    else weights += (weights.empty() ? "\"" : ",\"") + a.substr(0, eq) + "\":" + a.substr(eq + 1);
                }
                for (int i = 0; i < frames; ++i) {
                    const std::string msg = "{\"type\":\"face\",\"t\":" + std::to_string(i * 33) + ",\"weights\":{" + weights +
                                            "},\"head\":[0,0.1,0,0.995]}";
                    const link::HttpClientResponse r = call("POST", "/api/face", msg);
                    if (r.status != 200) {
                        fail(line, "face frame refused: " + std::to_string(r.status) + " " + r.body + r.error);
                        return;
                    }
                }
                return;
            }
            // phone_upload KIND FILE...: the capture page's protocol, with a lost chunk to resume from.
            link::HttpClientResponse r = call("POST", "/api/capture", "{\"kind\":\"" + args[0] + "\"}");
            const std::string capture = jsonOf(r.body)["id"].asString();
            if (r.status != 200 || capture.empty()) {
                fail(line, "capture refused: " + std::to_string(r.status) + " " + r.body + r.error);
                return;
            }
            for (size_t f = 1; f < args.size(); ++f) {
                std::filesystem::path file(std::u8string(args[f].begin(), args[f].end()));
                if (file.is_relative()) file = std::filesystem::path(std::u8string(scriptDir_.begin(), scriptDir_.end())) / file;
                std::ifstream in(file, std::ios::binary);
                const std::string data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
                if (data.empty()) {
                    fail(line, "can't read " + args[f]);
                    return;
                }
                const auto name = file.filename().u8string();
                r = call("POST", "/api/capture/" + capture + "/file",
                         "{\"name\":\"" + std::string(name.begin(), name.end()) + "\",\"size\":" + std::to_string(data.size()) + "}");
                const std::string id = jsonOf(r.body)["id"].asString();
                if (r.status != 200 || id.empty()) {
                    fail(line, "file refused: " + std::to_string(r.status) + " " + r.body + r.error);
                    return;
                }
                const size_t chunk = 64 * 1024;
                size_t offset = 0;
                bool skipped = false;
                while (offset < data.size()) {
                    size_t at = offset;
                    if (!skipped && data.size() > 2 * chunk && offset >= chunk) { // this chunk "gets lost": send the next
                        skipped = true;
                        at = offset + chunk;
                    }
                    r = call("PUT", "/api/file/" + id + "?offset=" + std::to_string(at), data.substr(at, chunk));
                    const double received = jsonOf(r.body)["received"].asNumber(-1);
                    if ((r.status != 200 && r.status != 409) || received < 0) {
                        fail(line, "chunk refused: " + std::to_string(r.status) + " " + r.body + r.error);
                        return;
                    }
                    offset = size_t(received); // a 409 says where to resume
                }
            }
            r = call("POST", "/api/capture/" + capture + "/finish", "");
            if (r.status != 200) fail(line, "finish refused: " + std::to_string(r.status) + " " + r.body + r.error);
        });
    } else if (verb == "env") {
        if (!need(2)) return false;
        std::string value = restOf(lineText_, 2);
        for (size_t at = value.find("$SCRIPT_DIR"); at != std::string::npos; at = value.find("$SCRIPT_DIR", at))
            value.replace(at, 11, scriptDir_);
        run([name = w[1], value] {
#if defined(_WIN32)
            _putenv_s(name.c_str(), value.c_str());
#else
            setenv(name.c_str(), value.c_str(), 1);
#endif
        });
    } else if (verb == "run") {
        if (!need(1)) return false;
        std::vector<ScriptStatement> statements;
        std::string text = restOf(lineText_, 1);
        for (size_t at = text.find("$SCRIPT_DIR"); at != std::string::npos; at = text.find("$SCRIPT_DIR", at)) {
            text.replace(at, 11, scriptDir_);
            at += scriptDir_.size();
        }
        if (!parseScript(text, statements, error)) return false;
        run([this, line, statements] {
            for (const auto& st : statements) {
                app_.runCommand(st.command, st.args);
                if (!app_.lastCommandOk_) {
                    fail(line, st.command + " failed: " + app_.status_);
                    return;
                }
            }
        });
    } else if (verb == "wait") {
        if (!need(1)) return false;
        wait(num(1, 0));
    } else if (verb == "wait_idle") {
        const float timeout = num(1, 120.0f);
        // Idle for several frames in a row: work is often only requested by the frame after an input (a
        // double-clicked LightBox folder is listed when the strip next draws), and a single idle frame in
        // between let the test run on before it started (ui_lightbox failed that way under a loaded ctest -j).
        auto idleFrames = std::make_shared<int>(0);
        steps_.push_back([this, line, timeout, idleFrames](float t) {
            const bool idle = !busy(app_.pendingBrush_) && !busy(app_.pendingGenerator_) && !busy(app_.pendingModels_) &&
                              !busy(app_.pendingUnreal_) && !busy(app_.pendingComfyCheck_) && !app_.galleryBusy() &&
                              !app_.thumbs_.busy() && !app_.libraryScanner_.busy() && !app_.autoUv_ &&
                              !app_.bakeBusy() && !app_.qremesh_ && !app_.voxelRemesh_ && !app_.updateJob_ &&
                              !app_.splatTrainBusy() && !app_.sculptableBusy() && !app_.phoneBusy();
            *idleFrames = idle ? *idleFrames + 1 : 0;
            if (*idleFrames >= 4) return true;
            if (idle) return false;
            SDL_Delay(10); // real work runs in real time
            if (t > timeout) fail(line, "still busy after " + number(timeout) + " s");
            return t > timeout;
        });
        wait(0.2f); // let the app pick up the results
    } else if (verb == "wait_for") {
        if (!need(1)) return false;
        const std::string label = w[1];
        const float timeout = num(2, 10.0f);
        steps_.push_back([this, line, label, timeout](float t) {
            ImVec2 at;
            if (findItem(label, at)) return true;
            SDL_Delay(10);
            if (t > timeout) fail(line, "\"" + label + "\" did not appear within " + number(timeout) + " s");
            return t > timeout;
        });
    } else if (verb == "expect" || verb == "expect_not") {
        if (!need(1)) return false;
        const std::string label = w[1];
        const bool want = verb == "expect";
        // Expectations look at the frame drawn after the previous step settled.
        steps_.push_back([this, line, label, want](float t) {
            if (t == 0.0f) return false;
            ImVec2 at;
            if (findItem(label, at) != want)
                fail(line, want ? "expected UI item \"" + label + "\"" : "UI item \"" + label + "\" should not be shown");
            return true;
        });
    } else if (verb == "expect_text" || verb == "expect_no_text") {
        if (!need(1)) return false;
        const std::string text = restOf(lineText_, 1);
        const bool want = verb == "expect_text";
        steps_.push_back([this, line, text, want](float t) {
            if (t == 0.0f) return false;
            if ((frameText_.find(text) != std::string::npos) != want)
                fail(line, want ? "no text \"" + text + "\" on screen" : "text \"" + text + "\" is on screen");
            return true;
        });
    } else if (verb == "expect_status") {
        if (!need(1)) return false;
        const std::string text = restOf(lineText_, 1);
        run([this, line, text] {
            if (app_.status_.find(text) == std::string::npos)
                fail(line, "status is \"" + app_.status_ + "\", expected it to contain \"" + text + "\"");
        });
    } else if (verb == "expect_help") {
        if (!need(1)) return false;
        const std::string text = restOf(lineText_, 1);
        steps_.push_back([this, line, text](float t) {
            if (t == 0.0f) return false; // the frame drawn after the previous step
            const std::string& shown = ui::HelpShown();
            if (shown.empty()) fail(line, "no Ctrl-help box is shown (hold ctrl, then hover an item)");
            else if (shown.find(text) == std::string::npos)
                fail(line, "the Ctrl-help box says \"" + shown.substr(0, 120) + "\", expected \"" + text + "\"");
            return true;
        });
    } else if (verb == "clipboard") {
        if (!need(1)) return false;
        const std::string text = restOf(lineText_, 1);
        run([this, line, text] {
            if (!SDL_SetClipboardText(text.c_str())) fail(line, std::string("can't set the clipboard: ") + SDL_GetError());
        });
    } else if (verb == "expect_clipboard") {
        if (!need(1)) return false;
        const std::string text = restOf(lineText_, 1);
        run([this, line, text] {
            char* clip = SDL_GetClipboardText();
            const std::string have = clip ? clip : "";
            SDL_free(clip);
            if (have.find(text) == std::string::npos)
                fail(line, "clipboard is \"" + have.substr(0, 80) + "\", expected it to contain \"" + text + "\"");
        });
    } else if (verb == "expect_value") {
        if (!need(3)) return false;
        bool known = false;
        query(w[1], known);
        if (!known) {
            error = "unknown value '" + w[1] + "'";
            return false;
        }
        const std::string name = w[1], op = w[2], wantText = w[3];
        run([this, line, name, op, wantText] {
            double want = std::strtod(wantText.c_str(), nullptr);
            if (!wantText.empty() && wantText[0] == '$') { // $NAME or $NAME*K
                const size_t star = wantText.find('*');
                const std::string key = wantText.substr(1, star == std::string::npos ? std::string::npos : star - 1);
                const auto it = remembered_.find(key);
                if (it == remembered_.end()) {
                    fail(line, "nothing remembered as $" + key);
                    return;
                }
                want = it->second * (star == std::string::npos ? 1.0 : std::strtod(wantText.c_str() + star + 1, nullptr));
            }
            bool k = false;
            const double v = query(name, k);
            if (!compare(v, op, want)) fail(line, name + " is " + number(v) + ", expected " + op + " " + number(want) + " (" + wantText + ")");
        });
    } else if (verb == "wait_value") { // wait (in real time) until a value holds, e.g. a background job's progress
        if (!need(3)) return false;
        bool known = false;
        query(w[1], known);
        if (!known) {
            error = "unknown value '" + w[1] + "'";
            return false;
        }
        const std::string name = w[1], op = w[2];
        const double want = std::strtod(w[3].c_str(), nullptr);
        const float timeout = num(4, 30.0f);
        steps_.push_back([this, line, name, op, want, timeout](float t) {
            bool k = false;
            const double v = query(name, k);
            if (compare(v, op, want)) return true;
            SDL_Delay(10);
            if (t > timeout) fail(line, name + " is " + number(v) + ", still not " + op + " " + number(want) + " after " + number(timeout) + " s");
            return t > timeout;
        });
    } else if (verb == "expect_guide") {
        if (!need(1)) return false;
        const std::string file = w[1];
        steps_.push_back([this, line, file](float t) {
            if (t == 0.0f) return false;
            if (app_.guide_.currentFile() != file)
                fail(line, "the User Guide shows " + app_.guide_.currentFile() + ", expected " + file);
            return true;
        });
    } else if (verb == "dump_shortcuts") {
        if (!need(1)) return false;
        const std::string name = w[1];
        run([this, line, name] {
            std::ofstream f(std::filesystem::path(std::u8string(shotDir_.begin(), shotDir_.end())) / name, std::ios::binary);
            f << shortcutsMarkdown();
            if (!f) fail(line, "can't write " + name + " in " + shotDir_);
        });
    } else if (verb == "snapshot") {
        run([this] {
            basePositions_ = app_.doc_.mesh.positions;
            baseNormals_ = app_.doc_.mesh.normals;
        });
    } else if (verb == "remember") {
        if (!need(2)) return false;
        bool known = false;
        query(w[1], known);
        if (!known) {
            error = "unknown value '" + w[1] + "'";
            return false;
        }
        run([this, name = w[1], as = w[2]] {
            bool k = false;
            remembered_[as] = query(name, k);
            SDL_Log("remember %s = %s", as.c_str(), number(remembered_[as]).c_str());
        });
    } else if (verb == "pen_stroke") {
        if (!need(6)) return false;
        const float u0 = num(1, 0), v0 = num(2, 0), u1 = num(3, 0), v1 = num(4, 0), p0 = num(5, 1), p1 = num(6, 1);
        bool eraser = false, leave = false;
        float seconds = 0.6f;
        for (size_t i = 7; i < w.size(); ++i) {
            if (w[i] == "eraser") eraser = true;
            else if (w[i] == "leave") leave = true;
            else seconds = num(i, seconds);
        }
        auto ends = std::make_shared<std::pair<ImVec2, ImVec2>>();
        run([this, ends, u0, v0, u1, v1] { *ends = {onModel(u0, v0), onModel(u1, v1)}; });
        penStroke([ends](float u) {
            return ImVec2(ends->first.x + (ends->second.x - ends->first.x) * u, ends->first.y + (ends->second.y - ends->first.y) * u);
        }, p0, p1, eraser, leave, seconds);
        wait(0.1f);
    } else if (verb == "pen_drag_item") {
        if (!need(5)) return false;
        const std::string label = w[1];
        const float dx = num(2, 0), dy = num(3, 0), p0 = num(4, 1), p1 = num(5, 1), seconds = num(6, 0.6f);
        auto centre = std::make_shared<ImVec2>();
        steps_.push_back([this, line, label, centre](float t) {
            if (findItem(label, *centre)) return true;
            if (t > 3.0f) fail(line, notFound(label));
            return t > 3.0f;
        });
        penStroke([centre, dx, dy](float u) { return ImVec2(centre->x + dx * (u - 0.5f), centre->y + dy * (u - 0.5f)); }, p0, p1,
                  false, false, seconds);
        wait(0.1f);
    } else if (verb == "pen_button") {
        if (!need(2)) return false;
        const int button = int(num(1, 1));
        if (w[2] != "down" && w[2] != "up") {
            error = "pen_button needs down or up";
            return false;
        }
        run([this, button, down = w[2] == "down"] {
            if (!penIn_) penProximity(true);
            penButtonEvent(button, down);
        });
        wait(0.05f);
    } else if (verb == "pen_hover") {
        if (!need(2)) return false;
        const float u = num(1, 0.5f), v = num(2, 0.5f), seconds = num(3, 0.4f);
        auto from = std::make_shared<ImVec2>();
        auto to = std::make_shared<ImVec2>();
        steps_.push_back([this, from, to, u, v, seconds](float t) {
            if (t == 0.0f) {
                if (!penIn_) penProximity(true);
                *from = mouse_;
                *to = onModel(u, v);
            }
            const float k = std::min(t / seconds, 1.0f);
            penMove(from->x + (to->x - from->x) * k, from->y + (to->y - from->y) * k);
            return t >= seconds;
        });
        wait(0.05f);
    } else if (verb == "pen_pressure") {
        if (!need(1)) return false;
        run([this, p = num(1, 0)] {
            if (!penIn_) penProximity(true);
            penPressure(p);
        });
    } else if (verb == "mouse_move") {
        if (!need(2)) return false;
        run([this, u = num(1, 0.5f), v = num(2, 0.5f)] {
            const ImVec2 p = onModel(u, v);
            mouseMotion(p.x, p.y);
        });
        wait(0.05f);
    } else if (verb == "pen_out") {
        run([this] { penProximity(false); });
        wait(0.05f);
    } else if (verb == "shot") {
        if (!need(1)) return false;
        const std::string name = w[1];
        steps_.push_back([this, name](float t) {
            if (t == 0.0f) return false; // a settled frame
            if (!shotDir_.empty()) shotRequest_ = shotDir_ + "/" + name + ".png";
            return true;
        });
        wait(0.05f);
    } else {
        error = "unknown step '" + verb + "'";
        return false;
    }
    return true;
}

// Every default shortcut, grouped by palette like the Controls window (F1), as Markdown for the user guide.
std::string Demo::shortcutsMarkdown() const {
    auto keys = [](const std::string& chord) {
        std::string out;
        size_t start = 0;
        while (start <= chord.size()) {
            const size_t at = chord.find(" / ", start);
            const std::string part = chord.substr(start, at == std::string::npos ? std::string::npos : at - start);
            if (!out.empty()) out += " or ";
            const auto parts = chordKeys(part);
            for (size_t i = 0; i < parts.size(); ++i) out += (i ? "+<kbd>" : "<kbd>") + parts[i] + "</kbd>";
            if (at == std::string::npos) break;
            start = at + 3;
        }
        return out;
    };
    auto cell = [](std::string text) {
        for (size_t at = text.find('|'); at != std::string::npos; at = text.find('|', at + 2)) text.replace(at, 1, "\\|");
        return text;
    };
    std::map<std::string, std::vector<const Action*>> groups;
    for (const auto& a : app_.actions_.all()) {
        if (a.shortcut.empty()) continue; // the defaults, not this session's overrides
        const std::string top = a.palette.substr(0, a.palette.find(" > "));
        groups[top.empty() ? "General" : top].push_back(&a);
    }
    std::string md;
    for (auto& [group, list] : groups) {
        std::stable_sort(list.begin(), list.end(), [](const Action* x, const Action* y) { return x->label < y->label; });
        md += "### " + group + "\n\n| Action | Keys | What it does |\n|---|---|---|\n";
        for (const Action* a : list) md += "| " + cell(a->label) + " | " + keys(a->shortcut) + " | " + cell(a->description) + " |\n";
        md += "\n";
    }
    if (!md.empty()) md.pop_back();
    return md;
}

// ---------------------------------------------------------------- simulated pen

void Demo::penProximity(bool in) {
    SDL_Event e{};
    e.type = in ? SDL_EVENT_PEN_PROXIMITY_IN : SDL_EVENT_PEN_PROXIMITY_OUT;
    e.pproximity.windowID = SDL_GetWindowID(app_.window_);
    e.pproximity.which = kTestPen;
    penIn_ = in;
    if (!in) penDown_ = penEraser_ = false;
    penState_ = in ? SDL_PEN_INPUT_IN_PROXIMITY : 0; // leaving releases the tip and every button
    app_.handleEvent(e);
}

void Demo::penMove(float x, float y) {
    SDL_Event e{};
    e.type = SDL_EVENT_PEN_MOTION;
    e.pmotion.windowID = SDL_GetWindowID(app_.window_);
    e.pmotion.which = kTestPen;
    e.pmotion.pen_state = penState_;
    e.pmotion.x = x;
    e.pmotion.y = y;
    app_.handleEvent(e);
    mouseMotion(x, y, SDL_PEN_MOUSEID); // SDL moves the mouse with a touching or hovering pen
}

void Demo::penPressure(float pressure) {
    SDL_Event e{};
    e.type = SDL_EVENT_PEN_AXIS;
    e.paxis.windowID = SDL_GetWindowID(app_.window_);
    e.paxis.which = kTestPen;
    e.paxis.pen_state = penState_;
    e.paxis.x = mouse_.x;
    e.paxis.y = mouse_.y;
    e.paxis.axis = SDL_PEN_AXIS_PRESSURE;
    e.paxis.value = pressure;
    app_.handleEvent(e);
}

void Demo::penTouch(bool down, bool eraser) {
    penDown_ = down;
    penEraser_ = down && eraser;
    penState_ = (penState_ & ~(SDL_PEN_INPUT_DOWN | SDL_PEN_INPUT_ERASER_TIP)) | (down ? SDL_PEN_INPUT_DOWN : 0) |
                (penEraser_ ? SDL_PEN_INPUT_ERASER_TIP : 0);
    SDL_Event e{};
    e.type = down ? SDL_EVENT_PEN_DOWN : SDL_EVENT_PEN_UP;
    e.ptouch.windowID = SDL_GetWindowID(app_.window_);
    e.ptouch.which = kTestPen;
    e.ptouch.pen_state = penState_;
    e.ptouch.x = mouse_.x;
    e.ptouch.y = mouse_.y;
    e.ptouch.eraser = eraser;
    e.ptouch.down = down;
    app_.handleEvent(e);
    if (down) mouseMotion(mouse_.x, mouse_.y, SDL_PEN_MOUSEID); // SDL_SendPenTouch: motion, then the left button
    mouseButton(down, SDL_BUTTON_LEFT, SDL_PEN_MOUSEID);
}

void Demo::penButtonEvent(int button, bool down) {
    const SDL_PenInputFlags bit = SDL_PenInputFlags(1u << button);
    penState_ = down ? (penState_ | bit) : (penState_ & ~bit);
    SDL_Event e{};
    e.type = down ? SDL_EVENT_PEN_BUTTON_DOWN : SDL_EVENT_PEN_BUTTON_UP;
    e.pbutton.windowID = SDL_GetWindowID(app_.window_);
    e.pbutton.which = kTestPen;
    e.pbutton.pen_state = penState_;
    e.pbutton.x = mouse_.x;
    e.pbutton.y = mouse_.y;
    e.pbutton.button = Uint8(button);
    e.pbutton.down = down;
    app_.handleEvent(e);
    // SDL 3.4 also clicks a mouse button for it (1: right, 2: middle ...); the app must ignore that.
    static const int kMouse[] = {SDL_BUTTON_LEFT, SDL_BUTTON_RIGHT, SDL_BUTTON_MIDDLE, SDL_BUTTON_X1, SDL_BUTTON_X2};
    if (button >= 1 && button < 5) mouseButton(down, kMouse[button], SDL_PEN_MOUSEID);
}

void Demo::penStroke(std::function<ImVec2(float u)> path, float p0, float p1, bool eraser, bool leave, float seconds) {
    // Hover to the start, touch down, then 4 samples a frame along the path, then lift (or leave).
    auto from = std::make_shared<ImVec2>();
    steps_.push_back([this, path, from](float t) {
        if (t == 0.0f) {
            if (!penIn_) penProximity(true);
            *from = mouse_;
        }
        const ImVec2 p = path(0.0f);
        const float k = std::min(t / 0.25f, 1.0f), u = k * k * (3.0f - 2.0f * k);
        penMove(from->x + (p.x - from->x) * u, from->y + (p.y - from->y) * u);
        return t >= 0.25f;
    });
    auto last = std::make_shared<float>(-1.0f);
    steps_.push_back([this, path, p0, p1, eraser, leave, seconds, last](float t) {
        if (*last < 0.0f) {
            penTouch(true, eraser);
            penPressure(p0); // the first pressure sample comes after the touch
            *last = 0.0f;
            return false;
        }
        const float u = std::min(t / seconds, 1.0f);
        constexpr int kSamples = 4;
        for (int k = 1; k <= kSamples; ++k) {
            const float uu = *last + (u - *last) * float(k) / float(kSamples);
            penPressure(p0 + (p1 - p0) * uu);
            const ImVec2 p = path(uu);
            penMove(p.x, p.y);
        }
        *last = u;
        if (u < 1.0f) return false;
        if (leave) {
            penProximity(false); // out of range without lifting first
        } else {
            penTouch(false, eraser);
            penPressure(0.0f);
        }
        *last = -1.0f;
        return true;
    });
}

void Demo::testKeyNow(SDL_Keycode key, SDL_Keymod mods) {
    SDL_Event e{};
    e.type = SDL_EVENT_KEY_DOWN;
    e.key.windowID = SDL_GetWindowID(app_.window_);
    e.key.key = key;
    e.key.scancode = SDL_GetScancodeFromKey(key, nullptr);
    e.key.mod = mods;
    e.key.down = true;
    app_.handleEvent(e);
    e.type = SDL_EVENT_KEY_UP;
    e.key.down = false;
    e.key.mod = held_;
    app_.handleEvent(e);
}

bool Demo::loadScript(const std::string& path, std::string& error) {
    std::ifstream f(std::filesystem::path(std::u8string(path.begin(), path.end())));
    if (!f) {
        error = "Cannot read " + path;
        return false;
    }
    testing_ = true;
    scriptName_ = std::filesystem::path(std::u8string(path.begin(), path.end())).filename().string();
    {
        std::error_code ec;
        const auto dir = std::filesystem::absolute(std::filesystem::path(std::u8string(path.begin(), path.end())), ec).parent_path().u8string();
        scriptDir_.assign(dir.begin(), dir.end());
    }
    shotDir_ = app_.options_.uiOutDir;
    if (!shotDir_.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(std::filesystem::path(std::u8string(shotDir_.begin(), shotDir_.end())), ec);
    }
    mouse_ = ImVec2(float(app_.options_.width) * 0.5f, float(app_.options_.height) * 0.5f);
    wait(0.2f); // first frames: windows find their places
    std::string line;
    int n = 0;
    while (std::getline(f, line)) {
        ++n;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        auto words = splitWords(line);
        if (words.empty()) continue;
        lineText_ = line;
        std::string err;
        if (!parseStep(n, words, err)) {
            error = scriptName_ + ":" + std::to_string(n) + ": " + err;
            return false;
        }
    }
    wait(0.1f);
    return true;
}

} // namespace sf
