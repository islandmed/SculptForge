// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Line Gradient (#99): Color > Line Gradient, then a drag across the view paints a straight colour ramp from the
// paint colour to the End colour (or to clear) with paint_gradient (core/VertexPaint.h), into vertex paint or the
// texture. Its two ends stay on screen as handles: dragging one, or changing a colour, paints it again in place of
// the same undo step. Enter (or a camera move, or any other edit) keeps it; Esc takes it back.
#include "app/App.h"
#include "app/I18n.h"
#include "app/Ui.h"

#include "core/VertexPaint.h"

#include <IconsLucide.h>

#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace sf {

namespace {

constexpr float kHandleRadius = 7.0f;   // logical px; a little more is grabbable
constexpr float kMinLinePx = 6.0f;      // shorter drags are clicks

std::string matrixText(const glm::mat4& m) {
    std::string s;
    char buf[32];
    for (int i = 0; i < 16; ++i) {
        std::snprintf(buf, sizeof buf, "%s%.9g", i ? " " : "", double(m[i / 4][i % 4]));
        s += buf;
    }
    return s;
}

ImU32 displayColor(const glm::vec3& linear, int alpha = 255) {
    const glm::vec3 c = glm::clamp(linearToSrgb(linear), 0.0f, 1.0f);
    return IM_COL32(int(c.x * 255.0f + 0.5f), int(c.y * 255.0f + 0.5f), int(c.z * 255.0f + 0.5f), alpha);
}

} // namespace

void App::registerLineGradientActions() {
    Action a;
    a.id = "paint.line_gradient";
    a.label = "Line Gradient";
    a.icon = ICON_LC_BLEND;
    a.palette = "Color";
    a.description = "Drag a line across the model: the paint colour at its start runs into the End colour (or fades to "
                    "clear) at its end, in screen space, into vertex paint or the texture. Drag either end to adjust it; "
                    "Enter keeps it, Esc takes it back. Respects the mask; one undo step.";
    a.command = "paint_gradient";
    a.run = [this] {
        if (gradient_.armed) {
            gradient_.armed = false;
            endGradientEdit();
            status_ = "Line Gradient off";
        } else {
            gradient_.armed = true;
            status_ = "Line Gradient: drag across the model (Esc: off)";
        }
    };
    a.checked = [this] { return gradient_.armed; };
    a.enabled = [this] { return doc_.mesh.vertexCount() > 0; };
    actions_.add(std::move(a));
}

bool App::gradientMouseDown(float x, float y, SDL_Keymod mod) {
    // Ctrl, Shift and Alt keep their gestures (masking, smoothing, navigation) while it is on.
    if (!gradient_.armed || (mod & (SDL_KMOD_CTRL | SDL_KMOD_SHIFT | SDL_KMOD_ALT | SDL_KMOD_GUI))) return false;
    const glm::vec2 p(x, y);
    const float grab = (kHandleRadius + 5.0f) * uiScale_;
    gradient_.handle = 2;
    if (gradient_.placed) { // an end under the pointer moves; anywhere else starts a new gradient (the last one stays)
        const float da = glm::distance(p, gradient_.a), db = glm::distance(p, gradient_.b);
        if (std::min(da, db) <= grab) gradient_.handle = da <= db ? 0 : 1;
        else endGradientEdit();
    }
    if (gradient_.handle == 2) gradient_.a = gradient_.b = p;
    gradient_.grabOffset = gradient_.handle == 0 ? gradient_.a - p : gradient_.handle == 1 ? gradient_.b - p : glm::vec2(0.0f);
    drag_ = Drag::Gradient;
    return true;
}

void App::gradientMouseMove(float x, float y) {
    const glm::vec2 p = glm::vec2(x, y) + gradient_.grabOffset;
    if (gradient_.handle == 0) gradient_.a = p;
    else gradient_.b = p; // the end of a new line, or the end handle
}

void App::gradientMouseUp() {
    const int handle = gradient_.handle;
    gradient_.handle = -1;
    if (glm::distance(gradient_.a, gradient_.b) < kMinLinePx * uiScale_) {
        if (handle == 2) status_ = "Line Gradient: drag a line across the model";
        else applyGradient(); // an end dropped onto the other: put the last good one back
        return;
    }
    applyGradient();
}

void App::applyGradient() {
    const CanvasRect c = canvasRect();
    if (c.w <= 0.0f || c.h <= 0.0f) return;
    if (glm::distance(gradient_.a, gradient_.b) < kMinLinePx * uiScale_) {
        if (gradient_.placed) { // restore the ends it was painted with
            gradient_.a = gradient_.appliedA;
            gradient_.b = gradient_.appliedB;
        }
        return;
    }
    // A gradient still being adjusted replaces its own undo step (nothing else has been done since).
    if (gradient_.placed && doc_.undo.revision() == gradient_.revision) runCommand("undo");
    gradient_.placed = false;
    auto ndc = [&](glm::vec2 p) { return glm::vec2((p.x - c.x) / c.w * 2.0f - 1.0f, 1.0f - (p.y - c.y) / c.h * 2.0f); };
    const glm::vec2 a = ndc(gradient_.a), b = ndc(gradient_.b);
    const uint64_t before = doc_.undo.revision();
    runCommand("paint_gradient", {{"x0", double(a.x)}, {"y0", double(a.y)}, {"x1", double(b.x)}, {"y1", double(b.y)},
                                  {"color", colorToHex(doc_.paintColor)},
                                  {"to", gradient_.toClear ? std::string("clear") : colorToHex(gradient_.to)},
                                  {"aspect", double(c.w / c.h)},
                                  {"view_proj", matrixText(camera_.projection() * camera_.view())}});
    if (!lastCommandOk_ || doc_.undo.revision() == before) return;
    gradient_.placed = true;
    gradient_.revision = doc_.undo.revision();
    gradient_.view = camera_.view();
    gradient_.canvas = glm::vec4(c.x, c.y, c.w, c.h);
    gradient_.appliedA = gradient_.a;
    gradient_.appliedB = gradient_.b;
    gradient_.appliedFrom = doc_.paintColor;
    gradient_.appliedTo = gradient_.to;
    gradient_.appliedClear = gradient_.toClear;
    status_ = "Line Gradient: drag its ends to adjust; Enter keeps it, Esc takes it back";
}

void App::endGradientEdit() {
    gradient_.placed = false;
    gradient_.handle = -1;
}

bool App::gradientKey(SDL_Keycode key) {
    if (!gradient_.armed) return false;
    if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
        if (!gradient_.placed) return false;
        endGradientEdit();
        status_ = "Line Gradient kept";
        return true;
    }
    if (key != SDLK_ESCAPE) return false;
    if (gradient_.placed) {
        if (doc_.undo.revision() == gradient_.revision) runCommand("undo");
        endGradientEdit();
        status_ = "Line Gradient taken back";
    } else {
        gradient_.armed = false;
        status_ = "Line Gradient off";
    }
    return true;
}

void App::drawGradientOverlay() {
    if (!gradient_.armed) return;
    const CanvasRect c = canvasRect();
    if (gradient_.placed && gradient_.handle < 0) {
        // The view or the history moved on: the gradient stays as painted and its handles go.
        if (doc_.undo.revision() != gradient_.revision || camera_.view() != gradient_.view ||
            gradient_.canvas != glm::vec4(c.x, c.y, c.w, c.h))
            endGradientEdit();
        // A colour changed (the picker, a swatch, End, Clear) once the mouse is up: paint it again.
        else if (!ImGui::IsMouseDown(ImGuiMouseButton_Left) &&
                 (doc_.paintColor != gradient_.appliedFrom || gradient_.toClear != gradient_.appliedClear ||
                  (!gradient_.toClear && gradient_.to != gradient_.appliedTo)))
            applyGradient();
    }
    const bool dragging = drag_ == Drag::Gradient;
    if (!gradient_.placed && !dragging) return;
    if (dragging && gradient_.handle == 2 && glm::distance(gradient_.a, gradient_.b) < 1.0f) return;
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    dl->PushClipRect(ImVec2(c.x, c.y), ImVec2(c.x + c.w, c.y + c.h), true);
    const float s = uiScale_;
    const ImVec2 a(gradient_.a.x, gradient_.a.y), b(gradient_.b.x, gradient_.b.y);
    const glm::vec2 d = gradient_.b - gradient_.a;
    const float len = std::max(glm::length(d), 1e-3f);
    const glm::vec2 n = glm::vec2(-d.y, d.x) / len; // across the line
    // The ramp along the line, over a dark edge so it reads on any model and background.
    LineGradient g;
    g.from = doc_.paintColor;
    g.to = gradient_.to;
    g.toClear = gradient_.toClear;
    dl->AddLine(a, b, IM_COL32(0, 0, 0, 150), 6.0f * s);
    constexpr int kSegments = 24;
    for (int i = 0; i < kSegments; ++i) {
        const float t0 = float(i) / kSegments, t1 = float(i + 1) / kSegments;
        const glm::vec4 col = g.at(0.5f * (t0 + t1));
        const glm::vec2 p0 = gradient_.a + d * t0, p1 = gradient_.a + d * t1;
        dl->AddLine(ImVec2(p0.x, p0.y), ImVec2(p1.x, p1.y), displayColor(glm::vec3(col), int(60 + 195 * col.w)), 3.0f * s);
    }
    // Ticks across both ends: the bands run parallel to them.
    const float tick = 14.0f * s;
    for (const glm::vec2& e : {gradient_.a, gradient_.b}) {
        dl->AddLine(ImVec2(e.x - n.x * tick, e.y - n.y * tick), ImVec2(e.x + n.x * tick, e.y + n.y * tick), IM_COL32(0, 0, 0, 150), 3.5f * s);
        dl->AddLine(ImVec2(e.x - n.x * tick, e.y - n.y * tick), ImVec2(e.x + n.x * tick, e.y + n.y * tick), IM_COL32(255, 255, 255, 200), 1.5f * s);
    }
    // The handles: filled with their colour (the end hollow when it fades to clear), the one under the pointer lit.
    const glm::vec2 mouse(mouseX_, mouseY_);
    const float r = kHandleRadius * s, grab = (kHandleRadius + 5.0f) * s;
    for (int h = 0; h < 2; ++h) {
        const ImVec2 p = h == 0 ? a : b;
        const bool hot = gradient_.handle == h ||
                         (gradient_.handle < 0 && gradient_.placed && glm::distance(mouse, glm::vec2(p.x, p.y)) <= grab);
        dl->AddCircleFilled(p, r + 2.0f * s, IM_COL32(0, 0, 0, 160), 24);
        if (h == 1 && gradient_.toClear) {
            dl->AddCircleFilled(p, r, IM_COL32(40, 40, 44, 200), 24);
            dl->AddLine(ImVec2(p.x - r * 0.6f, p.y + r * 0.6f), ImVec2(p.x + r * 0.6f, p.y - r * 0.6f), IM_COL32(230, 90, 80, 255), 1.5f * s);
        } else {
            dl->AddCircleFilled(p, r, displayColor(h == 0 ? doc_.paintColor : gradient_.to), 24);
        }
        dl->AddCircle(p, r, hot ? ui::color::accent : IM_COL32(255, 255, 255, 230), 24, (hot ? 2.5f : 1.5f) * s);
    }
    dl->PopClipRect();
}

// Color palette: the Line Gradient button and, while it is on, its End colour.
void App::drawLineGradientControls() {
    ui::ActionButton(*this, "paint.line_gradient", ImVec2(-1, 0), ui::Look::IconLabel);
    if (!gradient_.armed) return;
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("%s", tr("End"));
    ImGui::SameLine();
    ImGui::BeginDisabled(gradient_.toClear);
    glm::vec3 srgb = linearToSrgb(gradient_.to);
    if (ImGui::ColorEdit3("##gradient_end", glm::value_ptr(srgb), ImGuiColorEditFlags_NoInputs))
        gradient_.to = srgbToLinear(srgb);
    ImGui::EndDisabled();
    ui::HelpText("End", nullptr, "The colour at the end of the line. The start is the paint colour above.", "Color > Line Gradient");
    ImGui::SameLine();
    ui::Checkbox(tr("Clear##gradient"), &gradient_.toClear);
    ui::HelpText("Clear", nullptr,
                 "Fade the paint colour out instead: the paint underneath shows through towards the end of the line.",
                 "Color > Line Gradient");
    ImGui::SameLine();
    ImGui::BeginDisabled(gradient_.toClear);
    if (ui::GlyphButton("##gradient_swap", ICON_LC_ARROW_LEFT_RIGHT, ImVec2(ImGui::GetFrameHeight(), ImGui::GetFrameHeight())))
        std::swap(doc_.paintColor, gradient_.to);
    ImGui::EndDisabled();
    ui::HelpText("Swap", nullptr, "Swap the paint colour and the End colour.", "Color > Line Gradient");
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("%s", gradient_.placed ? tr("Drag its ends to adjust. Enter keeps it, Esc takes it back.")
                                               : tr("Drag a line across the model."));
    ImGui::PopTextWrapPos();
}

bool App::gradientUiValue(const std::string& name, double& out) const {
    if (name == "gradient_armed") out = gradient_.armed ? 1.0 : 0.0;
    else if (name == "gradient_placed") out = gradient_.placed ? 1.0 : 0.0;
    else return false;
    return true;
}

} // namespace sf
