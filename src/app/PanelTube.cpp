// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Tool > Tube and the Tube tool in the viewport (core/Tube.h): draw clay tubes in the air like a 3D pen.
//   Draw Tube on: a left-drag anywhere (on the model or off it) draws a tube. Its points lie on a plane facing the
//     view: through the surface under the pen where the stroke starts (else the last stroke's end, else the pivot),
//     through the pivot, or at a fixed distance from the camera. Snap to surfaces puts points over a surface onto
//     it. Pen pressure thins the tube; the stroke is smoothed and simplified into the curve's control points.
//     The result is a new Tube Part (undo takes it away again) or, with Merge, part of the active Part.
//   A Tube Part's points show as dots: drag one to move it (in the view plane), Ctrl+drag to make the tube thicker
//     or thinner there, Alt+click to delete it. With Draw Tube on, the dots of every visible Tube Part show and a
//     press on one selects that Part. Bake makes the Part a plain mesh to sculpt.
// Alt+drag still navigates; every stroke and point drag is one undo step. Symmetry X draws the mirror copy too.
#include "app/App.h"
#include "app/Demo.h"
#include "app/Palettes.h"
#include "app/Theme.h"
#include "app/Ui.h"

#include "core/Tube.h"

#include <algorithm>
#include <cmath>

namespace sf {

namespace {

constexpr const char* kTubePath = "Tool > Tube";

bool ctrlDown(SDL_Keymod mod) { return (mod & (SDL_KMOD_CTRL | SDL_KMOD_GUI)) != 0; } // as App.cpp

glm::vec3 planeHit(const glm::vec3& o, const glm::vec3& d, const glm::vec3& p, const glm::vec3& n) {
    const float dn = glm::dot(d, n);
    const float t = std::abs(dn) > 1e-12f ? glm::dot(p - o, n) / dn : 0.0f;
    return o + d * std::max(t, 0.0f);
}

} // namespace

void App::registerTubeUiActions() {
    auto add = [this](const char* id, const char* label, std::function<void()> run, std::function<bool()> enabled,
                      std::function<bool()> checked, const char* description, const char* command = "") {
        Action a;
        a.id = id;
        a.label = label;
        a.palette = kTubePath;
        a.run = std::move(run);
        a.enabled = std::move(enabled);
        a.checked = std::move(checked);
        a.description = description;
        a.command = command;
        actions_.add(std::move(a));
    };
    add("tube.draw", "Draw Tube",
        [this] {
            tubeDraw_ = !tubeDraw_;
            status_ = tubeDraw_ ? "Tube: drag anywhere to draw. Esc or a brush leaves it" // (short: the top bar)
                                : "Tube: Draw Tube off";
        },
        nullptr, [this] { return tubeDraw_; },
        "Drag anywhere in the view to draw a clay tube in the air, like a 3D pen: on the model or off it. Pen "
        "pressure makes it thinner, Symmetry X draws the mirror copy. Alt+drag still moves the view.");
    const auto depth = [this](TubeDepth d) {
        return [this, d] {
            tubeDepth_ = d;
            if (d == TubeDepth::Fixed && tubeFixedDepth_ <= 0.0f) tubeFixedDepth_ = glm::distance(camera_.position(), camera_.target);
        };
    };
    add("tube.depth_surface", "Surface", depth(TubeDepth::Surface), nullptr, [this] { return tubeDepth_ == TubeDepth::Surface; },
        "Strokes are drawn at the depth of the surface under the pen where they start; started off the model, at "
        "the depth where the last stroke ended (or the pivot).");
    add("tube.depth_pivot", "Pivot", depth(TubeDepth::Pivot), nullptr, [this] { return tubeDepth_ == TubeDepth::Pivot; },
        "Strokes are drawn on the plane through the view's pivot, facing you.");
    add("tube.depth_fixed", "Fixed", depth(TubeDepth::Fixed), nullptr, [this] { return tubeDepth_ == TubeDepth::Fixed; },
        "Strokes are drawn at a fixed distance from the camera (Depth below); orbit to draw at another angle.");
    add("tube.snap", "Snap to surfaces", [this] { tubeSnap_ = !tubeSnap_; }, nullptr, [this] { return tubeSnap_; },
        "Where the pen is over a surface, the tube lies on it (half sunk in) instead of on the drawing plane.");
    add("tube.caps_round", "Rounded", [this] { tubeFlatCaps_ = false; if (tubeActive()) runCommand("tube_set", {{"caps", std::string("round")}}); },
        nullptr, [this] { return tubeActive() ? !doc_.mesh.tube.flatCaps : !tubeFlatCaps_; },
        "Round ends (for new tubes, and the active Tube Part).", "tube_set");
    add("tube.caps_flat", "Flat", [this] { tubeFlatCaps_ = true; if (tubeActive()) runCommand("tube_set", {{"caps", std::string("flat")}}); },
        nullptr, [this] { return tubeActive() ? doc_.mesh.tube.flatCaps : tubeFlatCaps_; },
        "Flat ends (for new tubes, and the active Tube Part).", "tube_set");
    add("tube.new_part", "New Part", [this] { tubeMerge_ = false; }, nullptr, [this] { return !tubeMerge_; },
        "Each stroke makes a Tube Part whose points stay editable until Bake.");
    add("tube.merge", "Merge", [this] { tubeMerge_ = true; }, nullptr, [this] { return tubeMerge_; },
        "Each stroke goes into the active Part, with even edge lengths close to that Part's own, ready for dynamic "
        "topology.");
    const auto selected = [this] { return tubeSelection() >= 0; };
    add("tube.delete_point", "Delete Point",
        [this] { if (const int i = tubeSelection(); i >= 0) runCommand("tube_delete_point", {{"index", double(i)}}); tubeSelected_ = -1; },
        selected, nullptr, "Delete the selected point (the one last dragged, in orange). Alt+click a point does the same.",
        "tube_delete_point");
    add("tube.insert_point", "Insert Point",
        [this] {
            const int i = tubeSelection();
            if (i < 0) return;
            const int at = size_t(i) + 1 < doc_.mesh.tube.points.size() ? i : i - 1;
            if (at >= 0) runCommand("tube_insert_point", {{"index", double(at)}});
            tubeSelected_ = at + 1;
        },
        selected, nullptr, "Add a point halfway between the selected point and the next one.", "tube_insert_point");
    add("tube.bake", "Bake",
        [this] {
            runCommand("tube_bake", {{"uniform", tubeBakeUniform_}});
            tubeSelected_ = -1;
        },
        [this] { return tubeActive(); }, nullptr,
        "Make the active Tube Part a plain mesh to sculpt. Its points can't be edited after this (undo brings them "
        "back).",
        "tube_bake");
}

// Draw Tube and Append > Insert take every left drag in the view, so picking a brush or a tool, or Esc, leaves them
// (GABO on Discord: "after entering the append mode or tube I cant use the other tools anymore").
std::string App::leaveDrawModes() {
    std::string left;
    if (tubeDraw_) {
        tubeDraw_ = false;
        left = "Draw Tube";
    }
    if (!insertType_.empty()) {
        insertType_.clear();
        left += left.empty() ? "Insert" : " and Insert";
    }
    const int back = tubeReturnPart_;
    tubeReturnPart_ = -1;
    if (tubeActive() && back >= 0 && back < doc_.partCount() && back != doc_.activePart &&
        doc_.partMesh(back).tube.empty()) { // a Tube Part isn't sculpted: back to the Part before it
        runCommand("part_select", {{"index", double(back)}});
        partSelected("leaving Tube");
        if (left.empty()) left = "the Tube Part";
    }
    return left;
}

std::string App::leaveDrawModesStatus() {
    std::string left = leaveDrawModes();
    if (!left.empty()) status_ = "Left " + left + ": brushes sculpt again";
    return left;
}

void App::drawTubeContents() {
    const ImGuiStyle& style = ImGui::GetStyle();
    const float full = ImGui::GetContentRegionAvail().x;
    ui::ActionButton(*this, "tube.draw", ImVec2(full, 0));
    tray::GroupLabel(*this, "Stroke");
    ImGui::SetNextItemWidth(full);
    ui::SliderInside("##tubesize", &tubeSizePx_, 2.0f, 120.0f, tr("Size %.0f"));
    ui::HelpText("Size", nullptr, "How thick new tubes are, as a radius on screen at the depth you draw at.", kTubePath);
    ImGui::SetNextItemWidth(full);
    ui::SliderInside("##tubepressure", &tubePressure_, 0.0f, 1.0f, tr("Pressure %.2f"));
    ui::HelpText("Pressure", nullptr, "How much a light touch of the pen thins the tube. 0: the same thickness all along.",
                 kTubePath);
    ImGui::SetNextItemWidth(full);
    ui::SliderInside("##tubesmooth", &tubeSmoothing_, 0.0f, 1.0f, tr("Smoothing %.2f"));
    ui::HelpText("Smoothing", nullptr, "How much the stroke's wobble is smoothed out before the tube is made.", kTubePath);
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("%s", tr("Depth"));
    ImGui::SameLine();
    ui::Segmented(*this, {"tube.depth_surface", "tube.depth_pivot", "tube.depth_fixed"});
    if (tubeDepth_ == TubeDepth::Fixed) {
        const float far = std::max(10.0f * glm::distance(camera_.position(), camera_.target), 1.0f);
        ImGui::SetNextItemWidth(full);
        ui::SliderInside("##tubedepth", &tubeFixedDepth_, 0.0f, far, tr("Distance %.2f"));
    }
    ui::ActionButton(*this, "tube.snap", ImVec2(full, 0));
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("%s", tr("Ends"));
    ImGui::SameLine();
    ui::Segmented(*this, {"tube.caps_round", "tube.caps_flat"});
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("%s", tr("Result"));
    ImGui::SameLine();
    ui::Segmented(*this, {"tube.new_part", "tube.merge"});
    if (!tubeActive()) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("%s", tr("Select a Tube Part to edit its points."));
        ImGui::PopTextWrapPos();
        return;
    }
    tray::GroupLabel(*this, "Edit");
    const TubeShape& t = doc_.mesh.tube;
    ImGui::TextDisabled(tr("%d points"), int(t.points.size()));
    if (const int sel = tubeSelection(); sel >= 0) {
        ImGui::SameLine();
        ImGui::TextDisabled(tr("(point %d selected)"), sel);
        // (applied as one undo step when the slider is let go)
        const float stored = t.points[size_t(sel)].radius;
        float r = tubeRadiusEdit_ > 0.0f && tubeRadiusEditPoint_ == sel ? tubeRadiusEdit_ : stored;
        ImGui::SetNextItemWidth(full);
        if (ui::SliderInside("##tubepointr", &r, 0.0f, 4.0f * stored, tr("Radius %.4g")) && r > 0.0f) {
            tubeRadiusEdit_ = r;
            tubeRadiusEditPoint_ = sel;
        }
        if (ImGui::IsItemDeactivatedAfterEdit() && tubeRadiusEdit_ > 0.0f) {
            runCommand("tube_set_point", {{"index", double(sel)}, {"radius", double(tubeRadiusEdit_)}});
            tubeRadiusEdit_ = -1.0f;
        }
    }
    const float half = (full - style.ItemSpacing.x) * 0.5f;
    ui::ActionButton(*this, "tube.delete_point", ImVec2(half, 0));
    ImGui::SameLine();
    ui::ActionButton(*this, "tube.insert_point", ImVec2(-1, 0));
    ui::ActionButton(*this, "tube.bake", ImVec2(half, 0));
    ImGui::SameLine();
    ui::Checkbox(tr("Even edges##tube_bake_uniform"), &tubeBakeUniform_);
    ui::HelpText("Even edges", nullptr,
                 "Bake re-sweeps the tube in triangles of one size (good for dynamic topology) instead of keeping its quads.",
                 kTubePath);
}

glm::vec3 App::tubeViewDir() const { return glm::normalize(camera_.target - camera_.position()); }

int App::tubeSelection() const {
    return tubeActive() && tubeSelected_ >= 0 && size_t(tubeSelected_) < doc_.mesh.tube.points.size() ? tubeSelected_ : -1;
}

int App::tubePointAt(float x, float y, int* part) const {
    const float reach = 9.0f * uiScale_;
    float best = reach * reach;
    int found = -1;
    for (int i = 0; i < doc_.partCount(); ++i) {
        if (i != doc_.activePart && !tubeDraw_) continue; // other Tube Parts' points show while drawing
        if (!doc_.parts[size_t(i)].visible) continue;
        const TubeShape& t = doc_.partMesh(i).tube;
        for (size_t k = 0; k < t.points.size(); ++k) {
            ImVec2 s;
            if (!gizmoProject(t.points[k].position, s)) continue;
            const float d2 = (s.x - x) * (s.x - x) + (s.y - y) * (s.y - y);
            if (d2 < best || (d2 == best && i == doc_.activePart)) {
                best = d2;
                found = int(k);
                if (part) *part = i;
            }
        }
    }
    return found;
}

bool App::tubeMouseDown(float x, float y, SDL_Keymod mod) {
    const SDL_Keymod keys = SDL_GetModState(); // the keyboard itself
    int part = -1;
    const int point = (tubeActive() || tubeDraw_) ? tubePointAt(x, y, &part) : -1;
    TubeDrag& d = tubeDrag_;
    if (point >= 0) {
        if (part != doc_.activePart) {
            if (!tubeActive()) tubeReturnPart_ = doc_.activePart; // a brush or Esc goes back to it
            runCommand("part_select", {{"index", double(part)}});
            partSelected("tube point");
        }
        if (!tubeMatchesMesh(doc_.mesh)) {
            status_ = "Tube: this Part was moved or changed since it was drawn; Bake keeps it as it is (or undo the change)";
            return true;
        }
        if (keys & SDL_KMOD_ALT) {
            if (doc_.mesh.tube.points.size() > 1) runCommand("tube_delete_point", {{"index", double(point)}});
            else status_ = "Tube: the last point stays (delete the Part instead)";
            tubeSelected_ = -1;
            return true;
        }
        d = {};
        d.point = point;
        d.resize = ctrlDown(mod);
        d.start = doc_.mesh.tube;
        d.before = doc_.mesh;
        d.x = x;
        d.y = y;
        glm::vec3 o, dir;
        canvasRay(x, y, o, dir);
        const glm::vec3 c = d.start.points[size_t(point)].position;
        d.grab = c - planeHit(o, dir, c, tubeViewDir());
        tubeSelected_ = point;
        drag_ = Drag::Tube;
        return true;
    }
    if (tubeDraw_) {
        if (keys & SDL_KMOD_ALT) return false; // navigates
        d = {};
        d.drawing = true;
        d.planeNormal = tubeViewDir();
        RayHit hit;
        const glm::vec3 eye = camera_.position();
        if (tubeDepth_ == TubeDepth::Fixed) {
            if (tubeFixedDepth_ <= 0.0f) tubeFixedDepth_ = glm::distance(eye, camera_.target);
            d.planePoint = eye + d.planeNormal * tubeFixedDepth_;
        } else if (tubeDepth_ == TubeDepth::Surface && pickPart(x, y, &hit) >= 0) {
            d.planePoint = hit.position;
        } else if (tubeDepth_ == TubeDepth::Surface && tubeHaveAnchor_ && glm::dot(tubeAnchor_ - eye, d.planeNormal) > 0.0f) {
            d.planePoint = tubeAnchor_;
        } else {
            d.planePoint = camera_.target;
        }
        const float wpp = worldPerPixel(d.planePoint);
        d.radius = std::max(tubeSizePx_ * (wpp > 0.0f ? wpp : 0.01f), 1e-5f);
        d.x = x;
        d.y = y;
        drag_ = Drag::Tube;
        tubeDragTo(x, y);
        return true;
    }
    if (tubeActive()) { // a Tube Part isn't sculpted until it is baked
        RayHit hit;
        if (!pick(x, y, hit)) return false; // the background navigates
        status_ = "Tube: drag a point to move it (Ctrl: thickness, Alt+click: delete); Bake to sculpt this Part, or "
                  "pick a brush to sculpt the Part before it";
        return true;
    }
    return false;
}

void App::tubeDragTo(float x, float y) {
    TubeDrag& d = tubeDrag_;
    glm::vec3 o, dir;
    canvasRay(x, y, o, dir);
    if (d.drawing) {
        const float pressure = std::clamp(strokePressure(), 0.0f, 1.0f);
        const float r = d.radius * (1.0f - tubePressure_ + tubePressure_ * std::max(pressure, 0.05f));
        glm::vec3 p = planeHit(o, dir, d.planePoint, d.planeNormal);
        RayHit hit;
        if (tubeSnap_ && pickPart(x, y, &hit) >= 0) p = hit.position + glm::normalize(hit.normal) * (0.5f * r);
        if (!d.samples.empty() && glm::distance(d.samples.back().position, p) < 0.1f * r) return;
        d.samples.push_back({p, r});
        const float wpp = worldPerPixel(p);
        d.screen.push_back(ImVec4(x, y, wpp > 0.0f ? r / wpp : tubeSizePx_, 0.0f));
        return;
    }
    if (d.point < 0) return;
    TubeShape s = d.start;
    TubePoint& p = s.points[size_t(d.point)];
    if (d.resize) {
        const float amount = ((x - d.x) - (y - d.y)) / (120.0f * uiScale_);
        p.radius = std::max(d.start.points[size_t(d.point)].radius * std::exp(amount), 1e-5f);
        status_ = "Tube: point " + std::to_string(d.point) + " radius " + std::to_string(p.radius).substr(0, 6);
    } else {
        p.position = planeHit(o, dir, p.position, tubeViewDir()) + d.grab;
    }
    doc_.mesh = tubeMesh(s, kTubePreviewTriangles); // (coarser while dragging a long or thin tube)
    doc_.mesh.topologyDirty = true;
    doc_.bvhDirty = true; // (built once when let go)
    tubeDrag_.preview = s;
    d.changed = true;
}

void App::tubeRelease() {
    TubeDrag& d = tubeDrag_;
    if (d.drawing) {
        TubeShape s;
        s.points = tubeFromStroke(d.samples, tubeSmoothing_);
        s.flatCaps = tubeFlatCaps_;
        s.mirrorX = symmetry_.x;
        if (!s.points.empty()) {
            int edited = -1;
            const CommandResult r = tubeAdd(doc_, s, tubeMerge_, "Tube", &edited);
            status_ = r.message;
            if (r.ok) {
                tubeAnchor_ = s.points.back().position;
                tubeHaveAnchor_ = true;
            }
        }
    } else if (d.changed) {
        doc_.mesh = std::move(d.before);
        doc_.replaceMeshUndoable(tubeMesh(d.preview), d.resize ? "Tube point size" : "Tube point"); // (full density)
        status_ = std::string("Tube: ") + (d.resize ? "point size" : "point moved") + "; " +
                  std::to_string(doc_.mesh.tube.points.size()) + " points";
    }
    d = {};
}

void App::drawTubeOverlay() {
    const CanvasRect cr = canvasRect();
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    const float s = uiScale_;
    dl->PushClipRect(ImVec2(cr.x, cr.y), ImVec2(cr.x + cr.w, cr.y + cr.h), true);
    const TubeDrag& d = tubeDrag_;
    if (drag_ == Drag::Tube && d.drawing) { // the trail: the tube's silhouette as the pen moves
        const ImU32 fill = IM_COL32(205, 170, 140, 150), edge = theme::u32(theme::accent);
        for (const ImVec4& p : d.screen) dl->AddCircleFilled(ImVec2(p.x, p.y), std::max(p.z, 1.0f), fill, 20);
        for (size_t i = 1; i < d.screen.size(); ++i)
            dl->AddLine(ImVec2(d.screen[i - 1].x, d.screen[i - 1].y), ImVec2(d.screen[i].x, d.screen[i].y), edge, 1.5f * s);
        dl->PopClipRect();
        return;
    }
    // The control points of the active Tube Part (and, while drawing tubes, of every visible one).
    const bool hoverOk = !ImGui::GetIO().WantCaptureMouse && cr.contains(mouseX_, mouseY_) && drag_ == Drag::None;
    int hoverPart = -1;
    const int hover = hoverOk ? tubePointAt(mouseX_, mouseY_, &hoverPart) : -1;
    for (int i = 0; i < doc_.partCount(); ++i) {
        if ((i != doc_.activePart && !tubeDraw_) || !doc_.parts[size_t(i)].visible) continue;
        const TubeShape& t = doc_.partMesh(i).tube;
        const bool active = i == doc_.activePart;
        ImVec2 prev;
        bool havePrev = false;
        for (size_t k = 0; k < t.points.size(); ++k) {
            ImVec2 p;
            if (!gizmoProject(t.points[k].position, p)) {
                havePrev = false;
                continue;
            }
            if (havePrev) dl->AddLine(prev, p, IM_COL32(120, 200, 255, active ? 110 : 60), 1.0f * s);
            prev = p;
            havePrev = true;
        }
        for (size_t k = 0; k < t.points.size(); ++k) {
            ImVec2 p;
            if (!gizmoProject(t.points[k].position, p)) continue;
            const bool sel = active && int(k) == tubeSelection();
            const bool hot = int(k) == hover && i == hoverPart;
            const ImU32 col = hot ? ((inputMods() & SDL_KMOD_ALT) ? IM_COL32(255, 90, 80, 240) : IM_COL32(120, 200, 255, 240))
                              : sel ? IM_COL32(255, 170, 60, 240)
                                    : IM_COL32(235, 235, 235, active ? 220 : 120);
            dl->AddCircleFilled(p, (hot || sel ? 4.5f : 3.5f) * s, col, 12);
            dl->AddCircle(p, (hot || sel ? 4.5f : 3.5f) * s, IM_COL32(20, 20, 20, 200), 12, 1.0f * s);
        }
    }
    // Draw Tube's cursor: how thick the tube will be.
    if (tubeDraw_ && hoverOk && hover < 0)
        dl->AddCircle(ImVec2(mouseX_, mouseY_), tubeSizePx_, theme::u32(theme::accent), 32, 1.5f * s);
    dl->PopClipRect();
}

} // namespace sf
