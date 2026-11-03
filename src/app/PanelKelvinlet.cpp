// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Kelvinlet brushes and Alpha / VDM from viewport in the app (core/Kelvinlet.h, core/ViewCapture.h):
//   - KelvinGrab drags the grabbed point in the view plane, as Move does; KelvinTwist and KelvinScale act where
//     the stroke began and a drag to the right turns clockwise / swells (to the left: back / pinches);
//     KelvinSnake goes through snakeHookTo (PanelBrushes.cpp) and Detail Clone through the normal dabs.
//   - Brush > Kelvinlet: Squash (Poisson's ratio), Sharpness, the Detail Clone spacing.
//   - Capture from view (Ctrl+Alt+C): drag a square (or circle) over the model; a window shows the captured
//     height alpha and VDM, their options, and Use / Save to library buttons.
#include "app/App.h"
#include "app/AsyncStroke.h"
#include "app/Ui.h"

#include "core/BrushLibrary.h"

#include <IconsLucide.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>

namespace sf {

namespace {

constexpr float kPi = 3.14159265f;

void hint(const char* text) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("%s", text);
    ImGui::PopStyleColor();
}

bool dragBrush(BrushType t) {
    return t == BrushType::KelvinGrab || t == BrushType::KelvinTwist || t == BrushType::KelvinScale;
}

std::string vec3Arg(const glm::vec3& v) {
    char buf[96];
    std::snprintf(buf, sizeof buf, "%.6g,%.6g,%.6g", v.x, v.y, v.z);
    return buf;
}

} // namespace

bool App::kelvinStrokeBrush() const { return dragBrush(strokeBrush_) && !strokeRecipe() && strokeSculpt_; }

// ---------------------------------------------------------------------------
// Strokes
// ---------------------------------------------------------------------------

void App::kelvinBegin(const RayHit& hit) {
    const float pressure = pressureSize_ ? std::max(strokePressure(), 0.05f) : 1.0f;
    kelvinCenter_ = hit.position;
    kelvinRadius_ = radiusPx_ * pressure * camera_.worldPerPixel(hit.position, canvasHeight());
    grabPlanePoint_ = grabLast_ = kelvinTarget_ = hit.position;
    kelvinPending_ = 0.0f;
    kelvinLastX_ = lastX_;
    lastDabScreen_ = glm::vec2(lastX_, lastY_);
}

void App::kelvinTo(float x, float y) {
    if (strokeProbe_) strokeProbe_->emplace_back(x, y); // --measure-stroke counts each drag step as a dab
    const float pressure = pressureStrength_ ? strokePressure() : 1.0f;
    kelvinAmount_ = std::min(1.0f, 2.0f * strength_) * pressure;
    if (strokeBrush_ == BrushType::KelvinGrab) {
        // The grabbed point follows the cursor in the plane through it facing the camera (as Move), or along the
        // normal (App::beginAlongNormal).
        glm::vec3 origin, dir;
        canvasRay(x, y, origin, dir);
        const glm::vec3 n = camera_.forward();
        const float denom = glm::dot(dir, n);
        if (strokeAlongNormal_) kelvinTarget_ = alongNormalTarget(x, y);
        else if (std::abs(denom) < 1e-6f) return;
        else kelvinTarget_ = origin + dir * (glm::dot(grabPlanePoint_ - origin, n) / denom);
    } else {
        // Twist / Scale: the drag's sideways distance in brush radii (Alt reverses).
        const float dx = (x - kelvinLastX_) / std::max(radiusPx_, 1.0f) * kelvinAmount_ * (strokeInvert_ ? -1.0f : 1.0f);
        kelvinPending_ += strokeBrush_ == BrushType::KelvinTwist ? -dx * 0.5f * kPi : dx * 0.6f;
    }
    kelvinLastX_ = x;
    lastDabScreen_ = glm::vec2(x, y);
    kelvinAdvance();
}

// Applies what the cursor asked for since the last move, at most a bounded amount per frame (about one brush radius
// of travel, a 0.3 rad turn or a 0.2 log-scale step): a fast drag over a dense mesh then costs a bounded frame and
// the rest comes in the next frames (kelvinCatchUp runs every frame, also when the pointer stopped).
bool App::kelvinNextStep(KelvinletStep& step) {
    step = KelvinletStep{};
    step.radius = kelvinRadius_;
    step.viewDir = camera_.forward();
    step.axis = -camera_.forward();
    step.frontFacesOnly = frontFacesOnly_;
    const float share = doc_.sculptor.kelvinletOverBudget() ? 0.5f : 1.0f;
    if (strokeBrush_ == BrushType::KelvinGrab) {
        glm::vec3 delta = kelvinTarget_ - grabLast_;
        const float len = glm::length(delta), most = share * kelvinRadius_;
        if (len < 1e-7f * kelvinRadius_) return false;
        if (len > most) delta *= most / len;
        step.center = grabLast_;
        step.delta = delta * kelvinAmount_;
        step.travel = delta;
        step.substeps = std::clamp(int(std::ceil(glm::length(delta) / (0.4f * kelvinRadius_))), 1, 3);
        grabLast_ += delta;
        return true;
    }
    const float most = share * (strokeBrush_ == BrushType::KelvinTwist ? 0.3f : 0.2f);
    if (std::abs(kelvinPending_) < 1e-6f) return false;
    const float now = std::clamp(kelvinPending_, -most, most);
    kelvinPending_ -= now;
    step.center = kelvinCenter_;
    if (strokeBrush_ == BrushType::KelvinTwist) step.angle = now;
    else step.logScale = now;
    return true;
}

// Predictive strokes (AsyncStroke.cpp): the next bounded move goes to the solver thread when it is free; the frame
// draws meanwhile. The finished batch's upload is committed first. False when there was nothing to hand over.
bool App::kelvinPump() {
    if (!solver_) solver_ = std::make_unique<StrokeSolver>(*this);
    if (solver_->busy()) return true;
    if (solver_->done) commitSolverBatch();
    StrokeSolver& s = *solver_;
    KelvinletStep step;
    const bool move = kelvinNextStep(step);
    const bool far = !move && doc_.sculptor.kelvinletFarPending();
    if (!move && !far) return false;
    s.batch.clear();
    s.kelvinSteps.clear();
    if (move) s.kelvinSteps.push_back(step);
    s.kelvinFar = far;
    s.kelvinBrush = strokeBrush_;
    s.kelvinSymmetry = symmetry_;
    s.finalize = false;
    s.view.camera = camera_;
    s.view.canvas = canvasRect();
    s.view.probe = nullptr;
    s.view.radiusProbe = nullptr;
    s.view.settings = dabSettings();
    s.uploadId = doc_.activeSubtoolInfo().id;
    renderer_.bindStrokeUpload(s.uploadId, s.upload);
    s.prepare = &displayedMesh() == &doc_.mesh;
    renderer_.setDraftNormals(doc_.sculptor.draftActive() ? s.uploadId : 0);
    s.start();
    return true;
}

void App::kelvinAdvance() {
    if (drag_ != Drag::Stroke || !kelvinStrokeBrush()) return;
    if (asyncStroke_) { // the solver thread does the moves (kelvinCatchUp hands them over each frame)
        kelvinPump();
        return;
    }
    KelvinletStep step;
    step.radius = kelvinRadius_;
    step.viewDir = camera_.forward();
    step.axis = -camera_.forward();
    step.frontFacesOnly = frontFacesOnly_;
    static const bool log = std::getenv("SF_KELVIN_LOG") != nullptr;
    const SculptTimings before = doc_.sculptor.timings;
    // Over budget last time (the vertices a move touched): smaller pieces per frame.
    const float share = doc_.sculptor.kelvinletOverBudget() ? 0.5f : 1.0f;
    if (strokeBrush_ == BrushType::KelvinGrab) {
        glm::vec3 delta = kelvinTarget_ - grabLast_;
        const float len = glm::length(delta), most = share * kelvinRadius_;
        if (len < 1e-7f * kelvinRadius_) return;
        if (len > most) delta *= most / len;
        // One pass integrates the move in pieces (the elastic flow, not one big shear).
        step.center = grabLast_;
        step.delta = delta * kelvinAmount_;
        step.travel = delta;
        step.substeps = std::clamp(int(std::ceil(glm::length(delta) / (0.4f * kelvinRadius_))), 1, 3);
        doc_.sculptor.kelvinletStep(doc_.mesh, doc_.bvh, strokeBrush_, step, symmetry_);
        grabLast_ += delta;
    } else {
        const float most = share * (strokeBrush_ == BrushType::KelvinTwist ? 0.3f : 0.2f);
        if (std::abs(kelvinPending_) < 1e-6f) return;
        const float now = std::clamp(kelvinPending_, -most, most);
        kelvinPending_ -= now;
        step.center = kelvinCenter_;
        // (Rotations about one axis compose exactly, and Scale integrates its own flow: one pass.)
        if (strokeBrush_ == BrushType::KelvinTwist) step.angle = now;
        else step.logScale = now;
        doc_.sculptor.kelvinletStep(doc_.mesh, doc_.bvh, strokeBrush_, step, symmetry_);
    }
    kelvinMovedThisFrame_ = true;
    if (log) {
        const SculptTimings& t = doc_.sculptor.timings;
        SDL_Log("kelvin: %llu vertices, kernel %.1f normals %.1f bvh %.1f ms (renderer sync so far %.0f ms in %llu frames)",
                (unsigned long long)(t.vertices - before.vertices), t.kernelMs - before.kernelMs,
                t.normalsMs - before.normalsMs, t.bvhMs - before.bvhMs, perf_.strokeSyncMs,
                (unsigned long long)perf_.strokeFrames);
    }
}

void App::kelvinCatchUp() {
    if (asyncStroke_ && kelvinStrokeBrush() && (drag_ == Drag::Stroke || strokeFinishing_)) {
        if (kelvinPump()) {
            redrawIn(0.0);
            return;
        }
        if (strokeFinishing_) { // released and everything applied: the stroke's end (a draft's normals too)
            solverWait();
            finishStrokeNow();
        }
        return;
    }
    if (drag_ != Drag::Stroke || !kelvinStrokeBrush()) return;
    if (kelvinMovedThisFrame_) { // the pointer already moved it this frame: one bounded move a frame
        kelvinMovedThisFrame_ = false;
        redrawIn(0.0);
        return;
    }
    const float left = strokeBrush_ == BrushType::KelvinGrab ? glm::length(kelvinTarget_ - grabLast_) / std::max(kelvinRadius_, 1e-9f)
                                                             : std::abs(kelvinPending_);
    if (left > 1e-6f) kelvinAdvance();
    else if (doc_.sculptor.kelvinletFarPending()) doc_.sculptor.flushKelvinletFar(doc_.mesh, doc_.bvh, false); // resting: the far field catches up
    else return;
    redrawIn(0.0); // more to do: the next frame comes even if the pointer rests
}

void App::kelvinFinish() {
    if (!kelvinStrokeBrush() || (drag_ != Drag::Stroke && !strokeFinishing_)) return;
    if (asyncStroke_) solverWait(); // (then the rest on this thread)
    for (int i = 0; i < 64; ++i) { // the rest of the move before the stroke ends
        const float left = strokeBrush_ == BrushType::KelvinGrab ? glm::length(kelvinTarget_ - grabLast_) / std::max(kelvinRadius_, 1e-9f)
                                                                 : std::abs(kelvinPending_);
        if (left < 1e-6f) break;
        KelvinletStep step;
        if (!kelvinNextStep(step)) break;
        doc_.sculptor.kelvinletStep(doc_.mesh, doc_.bvh, strokeBrush_, step, symmetry_);
    }
    doc_.sculptor.flushKelvinletFar(doc_.mesh, doc_.bvh, true);
}

// ---------------------------------------------------------------------------
// Brush > Kelvinlet
// ---------------------------------------------------------------------------

void App::drawKelvinletSection() {
    KelvinletSettings& k = doc_.sculptor.kelvinlet;
    const char* help = "Brush > Kelvinlet";
    ImGui::SetNextItemWidth(-1);
    float squash = k.squash * 100.0f;
    if (ui::SliderInside("##kelvinsquash", &squash, 0.0f, 100.0f, "Squash %.0f")) k.squash = squash / 100.0f;
    ui::HelpText("Squash", nullptr,
                 "How the Kelvinlet brushes' material keeps its volume (Poisson's ratio): 0 is rubbery (it stretches "
                 "without bulging), 100 is incompressible clay (pressing in bulges the surface up around the thumb, "
                 "pulling out draws the sides in).",
                 help);
    ImGui::SetNextItemWidth(-1);
    float sharp = k.sharpness * 100.0f;
    if (ui::SliderInside("##kelvinsharp", &sharp, 0.0f, 100.0f, "Sharpness %.0f")) k.sharpness = sharp / 100.0f;
    ui::HelpText("Sharpness", nullptr,
                 "A tighter, more pointed peak at the brush centre with the same soft reach (a smaller inner scale of "
                 "the multi-scale Kelvinlet): for pulling points and tips.",
                 help);
    ImGui::SetNextItemWidth(-1);
    ui::SliderInside("##clonespacing", &k.cloneSpacing, 0.25f, 3.0f, "Clone Spacing %.2f");
    ui::HelpText("Clone Spacing", nullptr,
                 "Detail Clone: the distance between stamps along the stroke, in stamp widths (1 = edge to edge).", help);
    ImGui::Checkbox("Clone follows stroke", &k.cloneFollowStroke);
    ui::HelpText("Clone follows stroke", nullptr,
                 "Detail Clone: each stamp's top turns with the stroke (a row of scales or rivets along a curve); off, "
                 "every stamp keeps the screen's up.",
                 help);
    ImGui::Dummy(ImVec2(0, 2.0f * uiScale_));
    ui::ActionButton(*this, "brush.capture_view", ImVec2(ImGui::GetContentRegionAvail().x, 0));
    hint("KelvinGrab moves like Move with elastic, volume-keeping falloff. KelvinTwist and KelvinScale act where you "
         "press: drag right to turn clockwise or swell, left to turn back or pinch. KelvinSnake pulls tapering horns. "
         "Detail Clone stamps the VDM brush's map along the stroke: capture one from the model with Capture from "
         "view.");
}

void App::registerKelvinletActions() {
    Action a;
    a.id = "brush.capture_view";
    a.label = "Capture from view";
    a.icon = ICON_LC_SCAN_LINE;
    a.palette = "Brush > Kelvinlet";
    a.shortcut = "Ctrl+Alt+C";
    a.description = "Alpha and VDM from the viewport: drag a square over any sculpted detail (Shift for a circle) and "
                    "capture it as a grayscale height alpha and as a vector displacement brush, ready to stamp with "
                    "the VDM brush or along strokes with Detail Clone, and to save in your library.";
    a.run = [this] {
        capture_.armed = !capture_.armed;
        status_ = capture_.armed ? "Capture: drag a square over the model (Shift: circle); Esc cancels" : "Capture off";
    };
    a.checked = [this] { return capture_.armed; };
    actions_.add(std::move(a));
}

// ---------------------------------------------------------------------------
// Capture from view
// ---------------------------------------------------------------------------

bool App::captureMouseDown(float x, float y) {
    if (!capture_.armed) return false;
    capture_.anchor = capture_.current = glm::vec2(x, y);
    capture_.circle = (inputMods(false) & SDL_KMOD_SHIFT) != 0;
    drag_ = Drag::Capture;
    return true;
}

void App::captureMouseMove(float x, float y) { capture_.current = glm::vec2(x, y); }

namespace {

// The square (centre, half side) in window pixels a capture drag describes: corner to corner, kept square;
// a circle is dragged from its centre.
void captureSquare(const glm::vec2& a, const glm::vec2& b, bool circle, glm::vec2& centre, float& half) {
    if (circle) {
        centre = a;
        half = glm::length(b - a);
        return;
    }
    const glm::vec2 d = b - a;
    half = 0.5f * std::max(std::abs(d.x), std::abs(d.y));
    centre = a + glm::vec2(d.x < 0.0f ? -half : half, d.y < 0.0f ? -half : half);
}

} // namespace

void App::captureMouseUp() {
    glm::vec2 centre;
    float half = 0.0f;
    captureSquare(capture_.anchor, capture_.current, capture_.circle, centre, half);
    capture_.armed = false;
    if (half < 4.0f) {
        status_ = "Capture: drag a larger square";
        return;
    }
    RayHit hit;
    if (!pick(centre.x, centre.y, hit)) {
        // The centre misses (a square over an edge): the nearest point of the model inside the square.
        bool found = false;
        for (int ring = 1; ring <= 6 && !found; ++ring)
            for (int k = 0; k < 8 * ring && !found; ++k) {
                const float ang = 2.0f * kPi * float(k) / float(8 * ring);
                const float r = half * float(ring) / 7.0f;
                found = pick(centre.x + r * std::cos(ang), centre.y + r * std::sin(ang), hit);
            }
        if (!found) {
            status_ = "Capture: the square must be over the model";
            return;
        }
        // Keep the square where it was drawn: the point on the plane through the hit under the centre.
        glm::vec3 origin, dir;
        canvasRay(centre.x, centre.y, origin, dir);
        const glm::vec3 n = camera_.forward();
        hit.position = origin + dir * (glm::dot(hit.position - origin, n) / glm::dot(dir, n));
    }
    const float halfWorld = half * camera_.worldPerPixel(hit.position, canvasHeight());
    capture_.frame = makeCaptureFrame(hit.position, camera_.forward(), screenUpVector(0.0f), halfWorld, capture_.circle);
    capture_.haveFrame = true;
    capture_.open = true;
    recapture();
}

void App::recapture() {
    if (!capture_.haveFrame) return;
    doc_.syncBvh();
    capture_.error.clear();
    std::string err;
    BrushAlpha alpha;
    CaptureInfo ai, vi;
    if (captureHeightAlpha(doc_.mesh, doc_.bvh, capture_.frame, capture_.options, alpha, err, &ai)) {
        alpha.name = capture_.name[0] ? capture_.name : "Captured";
        capture_.alpha = std::move(alpha);
        capture_.alphaInfo = ai;
    } else {
        capture_.error = err;
    }
    VectorDisplacement vdm;
    if (captureVdmFromView(doc_.mesh, doc_.bvh, capture_.frame, capture_.options, vdm, err, &vi)) {
        vdm.name = capture_.name[0] ? capture_.name : "Captured";
        capture_.vdm = std::make_shared<const VectorDisplacement>(std::move(vdm));
        capture_.vdmInfo = vi;
    } else if (capture_.error.empty()) {
        capture_.error = err;
    }
    status_ = capture_.error.empty() ? "Captured: use it as an alpha, a VDM brush or with Detail Clone"
                                     : "Capture: " + capture_.error;
}

void App::drawCaptureOverlay() {
    if (drag_ == Drag::Capture) {
        glm::vec2 c;
        float half = 0.0f;
        captureSquare(capture_.anchor, capture_.current, capture_.circle, c, half);
        ImDrawList* dl = ImGui::GetForegroundDrawList();
        const ImU32 col = ui::color::accent, fill = IM_COL32(255, 255, 255, 18);
        const float t = 1.5f * uiScale_;
        if (capture_.circle) {
            dl->AddCircleFilled(ImVec2(c.x, c.y), half, fill, 64);
            dl->AddCircle(ImVec2(c.x, c.y), half, col, 64, t);
        } else {
            dl->AddRectFilled(ImVec2(c.x - half, c.y - half), ImVec2(c.x + half, c.y + half), fill);
            dl->AddRect(ImVec2(c.x - half, c.y - half), ImVec2(c.x + half, c.y + half), col, 0.0f, 0, t);
        }
        dl->AddLine(ImVec2(c.x - 6 * uiScale_, c.y), ImVec2(c.x + 6 * uiScale_, c.y), col, t);
        dl->AddLine(ImVec2(c.x, c.y - 6 * uiScale_), ImVec2(c.x, c.y + 6 * uiScale_), col, t);
    }
    if (capture_.open) drawCaptureWindow();
}

void App::drawCaptureWindow() {
    const float s = uiScale_;
    ImGui::SetNextWindowSize(ImVec2(340.0f * s, 0.0f), ImGuiCond_Appearing);
    const CanvasRect cr = canvasRect();
    ImGui::SetNextWindowPos(ImVec2(cr.x + cr.w - 360.0f * s, cr.y + 60.0f * s), ImGuiCond_Appearing);
    if (!ImGui::Begin("Capture from view##capture", &capture_.open, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::End();
        return;
    }
    const float side = std::floor((ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    auto preview = [&](ImTextureID tex, const char* label) {
        ImGui::BeginGroup();
        const ImVec2 p0 = ImGui::GetCursorScreenPos();
        ImGui::Dummy(ImVec2(side, side));
        dl->AddRectFilled(p0, ImVec2(p0.x + side, p0.y + side), IM_COL32(30, 30, 34, 255), 4.0f * s);
        if (tex) dl->AddImageRounded(tex, p0, ImVec2(p0.x + side, p0.y + side), ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, 4.0f * s);
        ImGui::TextDisabled("%s", label);
        ImGui::EndGroup();
    };
    preview(capture_.alpha.empty() ? ImTextureID(0) : thumbs_.alpha(capture_.alpha), "Height alpha");
    ImGui::SameLine();
    preview(capture_.vdm ? thumbs_.vdm(capture_.vdm) : ImTextureID(0), "Vector displacement");
    if (!capture_.error.empty()) ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.4f, 1.0f), "%s", capture_.error.c_str());
    else
        ImGui::TextDisabled("%zu points, height %.3g, reach %.2f radii", capture_.vdmInfo.vertices,
                            capture_.alphaInfo.depthRange, capture_.vdmInfo.maxOffset);

    bool again = false;
    CaptureOptions& o = capture_.options;
    const char* help = "Capture from view";
    ImGui::SetNextItemWidth(-1);
    float smooth = o.smoothing * 100.0f;
    if (ui::SliderInside("##capsmooth", &smooth, 0.0f, 100.0f, "Base Smoothing %.0f")) o.smoothing = smooth / 100.0f;
    again |= ImGui::IsItemDeactivatedAfterEdit();
    ui::HelpText("Base Smoothing", nullptr,
                 "What counts as the detail: the capture is the surface minus a smoothed copy of itself. 0 measures "
                 "from a flat plane (the whole shape); higher takes away more of the broad form, so a detail on a "
                 "curved arm or cheek captures flat. The VDM keeps each point's sideways offset too (overhangs).",
                 help);
    ImGui::SetNextItemWidth(-1);
    float fade = o.edgeFade * 100.0f;
    if (ui::SliderInside("##capfade", &fade, 0.0f, 50.0f, "Edge Fade %.0f")) o.edgeFade = fade / 100.0f;
    again |= ImGui::IsItemDeactivatedAfterEdit();
    ui::HelpText("Edge Fade", nullptr, "Fades the capture out toward its border, so stamps blend into any surface.", help);
    static const int kRes[] = {128, 256, 512, 1024};
    int ri = 1;
    for (int i = 0; i < 4; ++i)
        if (kRes[i] == o.resolution) ri = i;
    ImGui::SetNextItemWidth(-1);
    if (ImGui::SliderInt("##capres", &ri, 0, 3, (std::to_string(kRes[ri]) + " px").c_str())) o.resolution = kRes[ri];
    again |= ImGui::IsItemDeactivatedAfterEdit();
    ui::HelpText("Resolution", nullptr, "Pixels a side of the alpha and the map (512 or less suits most details).", help);
    again |= ImGui::Checkbox("Fit plane", &o.fitPlane);
    ui::HelpText("Fit plane", nullptr,
                 "Height alpha: measure from a plane fitted to the surface (a tilted surface captures level) instead of "
                 "the screen's plane.",
                 help);
    ImGui::SameLine();
    again |= ImGui::Checkbox("Invert##capture", &o.invert);
    ui::HelpText("Invert", nullptr, "Height alpha: white = deepest (for carving brushes).", help);
    ImGui::SetNextItemWidth(-1);
    if (ImGui::InputTextWithHint("##capname", "Name", capture_.name, sizeof capture_.name)) {
        if (!capture_.alpha.empty()) capture_.alpha.name = capture_.name;
    }
    ui::HelpText("Name", nullptr, "What the alpha and the VDM are called in your library (the same name replaces).", help);
    if (again) recapture();

    const bool ok = capture_.error.empty() && capture_.vdm && !capture_.alpha.empty();
    const std::string name = capture_.name[0] ? capture_.name : "Captured";
    auto args = [&] {
        Args a;
        a["point"] = vec3Arg(capture_.frame.center);
        a["view"] = vec3Arg(capture_.frame.forward);
        a["up"] = vec3Arg(capture_.frame.up);
        a["size"] = double(capture_.frame.halfSize);
        a["circle"] = capture_.frame.circle;
        a["resolution"] = double(o.resolution);
        a["smoothing"] = double(o.smoothing);
        a["edge_fade"] = double(o.edgeFade);
        a["name"] = name;
        return a;
    };
    const float w2 = std::floor((ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f);
    ImGui::BeginDisabled(!ok);
    if (ImGui::Button("Use as Alpha##capture", ImVec2(w2, 0))) {
        Args a = args();
        a["fit_plane"] = o.fitPlane;
        a["invert"] = o.invert;
        runCommand("capture_alpha", a);
    }
    ui::HelpText("Use as Alpha", nullptr, "Make the height capture the brush alpha (it shapes every brush's stamp).", help);
    ImGui::SameLine();
    if (ImGui::Button("Use as VDM##capture", ImVec2(w2, 0))) {
        Args a = args();
        a["save"] = false;
        runCommand("capture_vdm", a);
        brush_ = BrushType::Vdm;
        doc_.activeBrush = -1;
        paintMode_ = false;
    }
    ui::HelpText("Use as VDM", nullptr, "Make the capture the VDM brush's map and pick the VDM brush (Drag Stamp stamps it).", help);
    if (ImGui::Button("Detail Clone##capture", ImVec2(w2, 0))) {
        Args a = args();
        a["save"] = false;
        runCommand("capture_vdm", a);
        brush_ = BrushType::DetailClone;
        doc_.activeBrush = -1;
        paintMode_ = false;
    }
    ui::HelpText("Detail Clone", nullptr, "Paint copies of the captured detail along your strokes, each turned to follow the stroke.", help);
    ImGui::SameLine();
    if (ImGui::Button("Save to Library##capture", ImVec2(w2, 0))) {
        // The VDM into the brush library, the alpha into the LightBox's Alphas folder.
        Args a = args();
        a["save"] = true;
        runCommand("capture_vdm", a);
        if (!library_.user.empty()) {
            Args b = args();
            b["fit_plane"] = o.fitPlane;
            b["invert"] = o.invert;
            std::string file = brushFileName(name);
            file = file.substr(0, file.size() - std::strlen(".sfbrush")) + ".png";
            b["path"] = library_.user + "/Alphas/" + file;
            runCommand("capture_alpha", b);
            libraryScanner_.invalidate();
        }
        status_ = "Saved '" + name + "' to your library (Library > Alpha, and the VDM picker)";
    }
    ui::HelpText("Save to Library", nullptr,
                 "Keep both: the alpha as a PNG in your Library Alphas folder, the VDM in your brush library (Alpha "
                 "picker, Brush > Vector Displacement and the Library's Brush tab), with thumbnails.",
                 help);
    ImGui::EndDisabled();
    if (ImGui::Button("Capture again##capture", ImVec2(-1, 0))) {
        capture_.armed = true;
        status_ = "Capture: drag a square over the model (Shift: circle)";
    }
    ImGui::End();
}

bool App::kelvinUiValue(const std::string& name, double& out) const {
    if (name == "capture_armed") return out = capture_.armed ? 1.0 : 0.0, true;
    if (name == "capture_open") return out = capture_.open ? 1.0 : 0.0, true;
    if (name == "capture_ok") return out = capture_.error.empty() && capture_.vdm ? 1.0 : 0.0, true;
    if (name == "capture_points") return out = double(capture_.vdmInfo.vertices), true;
    if (name == "capture_reach") return out = double(capture_.vdmInfo.maxOffset), true;
    if (name == "alpha_width") return out = double(doc_.brushAlpha.width), true;
    if (name == "kelvin_squash") return out = std::round(doc_.sculptor.kelvinlet.squash * 100.0f), true;
    return false;
}

} // namespace sf
