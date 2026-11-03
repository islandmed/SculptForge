// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Cut (#101, #91; core/Cut.h): the Cut shapes sit in the Ctrl+Shift brush picker next to Select Rectangle and
// Select Lasso. Ctrl+Shift+drag a line, lasso or rectangle (Alt turns a line round); Cut Polyline places points
// with Ctrl+Shift+click. Tool > Cut picks the mode (Away, In, Add) and the depth. Each cut is one "cut" command
// with the shape in normalised device coordinates and the current view, mirrored by the symmetry.
#include "app/App.h"
#include "app/I18n.h"
#include "app/Ui.h"

#include <cmath>
#include <cstdio>

namespace sf {

namespace {

constexpr float kCloseRadiusPx = 9.0f; // a Polyline click this close to its first point closes it

const char* modeName(int mode) { return mode == 1 ? "in" : mode == 2 ? "add" : "away"; }

// Which side of a -> b a line (or the first segment of an open polyline) cuts: the left in NDC (y up), which is the
// left on the screen too; `flip` takes the right.
ImVec2 sideOf(ImVec2 a, ImVec2 b, bool flip) {
    const float dx = b.x - a.x, dy = b.y - a.y, len = std::max(std::hypot(dx, dy), 1e-3f);
    // Screen y runs down: the left of the drawing direction is (dy, -dx).
    return flip ? ImVec2(-dy / len, dx / len) : ImVec2(dy / len, -dx / len);
}

} // namespace

void App::runCut(const char* shape, const std::vector<ImVec2>& path, bool closed, bool flip) {
    const CanvasRect c = canvasRect();
    if (c.w <= 0.0f || c.h <= 0.0f || path.size() < 2) return;
    std::string points;
    char buf[48];
    for (const ImVec2& p : path) {
        std::snprintf(buf, sizeof buf, "%s%.5f %.5f", points.empty() ? "" : " ", double((p.x - c.x) / c.w * 2.0f - 1.0f),
                      double(1.0f - (p.y - c.y) / c.h * 2.0f));
        points += buf;
    }
    const glm::mat4 vp = camera_.projection() * camera_.view();
    std::string matrix;
    for (int i = 0; i < 16; ++i) {
        std::snprintf(buf, sizeof buf, "%s%.9g", i ? " " : "", double(vp[i / 4][i % 4]));
        matrix += buf;
    }
    runCommand("cut", withSymmetryArgs({{"shape", std::string(shape)},
                                        {"points", points},
                                        {"mode", std::string(modeName(cutMode_))},
                                        {"depth", double(cutDepth_)},
                                        {"closed", closed},
                                        {"flip", flip},
                                        {"view_proj", matrix}}));
}

void App::cutMouseUp() {
    if (!selectMoved_) return; // a click cuts nothing
    const bool alt = (inputMods() & SDL_KMOD_ALT) != 0;
    std::vector<ImVec2> path = selectPath_;
    const ImVec2 mouse(mouseX_, mouseY_);
    switch (selectShape_) {
    case SelectShape::CutLine: runCut("line", {path.front(), mouse}, false, alt); break;
    case SelectShape::CutRect: runCut("rect", {path.front(), mouse}, false, false); break;
    case SelectShape::CutLasso:
        path.push_back(mouse);
        if (path.size() >= 3) runCut("lasso", path, true, false);
        break;
    default: break;
    }
}

bool App::cutPolylineMouseDown(float x, float y, SDL_Keymod mod) {
    const bool ctrlShift = (mod & (SDL_KMOD_CTRL | SDL_KMOD_GUI)) && (mod & SDL_KMOD_SHIFT);
    if (cutPolyline_.empty() && (!ctrlShift || hideShape_ != SelectShape::CutPolyline || previewLod_ >= 0)) return false;
    if (cutPolyline_.size() >= 3 && std::hypot(x - cutPolyline_.front().x, y - cutPolyline_.front().y) < kCloseRadiusPx * uiScale_) {
        finishCutPolyline(true);
        return true;
    }
    if (!cutPolyline_.empty() && mouseClicks_ >= 2) { // double-click: end it open, at this point
        finishCutPolyline(false);
        return true;
    }
    if (cutPolyline_.empty() || std::hypot(x - cutPolyline_.back().x, y - cutPolyline_.back().y) > 2.0f)
        cutPolyline_.push_back(ImVec2(x, y));
    status_ = "Cut Polyline: click to add points, the first point to close, double-click or Enter to end, Esc to drop";
    return true;
}

bool App::cutPolylineKey(SDL_Keycode key) {
    if (cutPolyline_.empty()) return false;
    if (key == SDLK_ESCAPE) {
        cutPolyline_.clear();
        status_ = "Cut Polyline dropped";
        return true;
    }
    if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
        finishCutPolyline(false);
        return true;
    }
    return false;
}

void App::finishCutPolyline(bool closed) {
    std::vector<ImVec2> path;
    path.swap(cutPolyline_);
    if (path.size() < (closed ? 3u : 2u)) return;
    runCut("polyline", path, closed, (inputMods() & SDL_KMOD_ALT) != 0);
}

void App::drawCutOverlay() {
    const bool dragging = drag_ == Drag::Select && selectMoved_ && !selectPath_.empty() && isCutShape(selectShape_);
    if (!dragging && cutPolyline_.empty()) return;
    const bool alt = (inputMods() & SDL_KMOD_ALT) != 0;
    // Orange: what the cut takes away (Away, In) or adds (Add).
    const ImU32 line = cutMode_ == 2 ? IM_COL32(120, 220, 255, 235) : IM_COL32(255, 150, 70, 235);
    const ImU32 fill = cutMode_ == 2 ? IM_COL32(120, 220, 255, 45) : IM_COL32(255, 150, 70, 45);
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const CanvasRect c = canvasRect();
    dl->PushClipRect(ImVec2(c.x, c.y), ImVec2(c.x + c.w, c.y + c.h), true);
    const ImVec2 mouse(mouseX_, mouseY_);
    const float far = (c.w + c.h) * 2.0f;
    // A line or open polyline: the path, and the side it cuts shaded out to the canvas edge.
    auto openPath = [&](std::vector<ImVec2> path, bool flip) {
        if (path.size() < 2) return;
        const ImVec2 s = sideOf(path[0], path[1], flip), e = sideOf(path[path.size() - 2], path.back(), flip);
        const ImVec2 d0(path[1].x - path[0].x, path[1].y - path[0].y), d1(path.back().x - path[path.size() - 2].x,
                                                                         path.back().y - path[path.size() - 2].y);
        const float l0 = std::max(std::hypot(d0.x, d0.y), 1e-3f), l1 = std::max(std::hypot(d1.x, d1.y), 1e-3f);
        const ImVec2 start(path[0].x - d0.x / l0 * far, path[0].y - d0.y / l0 * far);
        const ImVec2 end(path.back().x + d1.x / l1 * far, path.back().y + d1.y / l1 * far);
        std::vector<ImVec2> poly{start};
        poly.insert(poly.end(), path.begin(), path.end());
        poly.push_back(end);
        poly.push_back(ImVec2(end.x + e.x * far, end.y + e.y * far));
        poly.push_back(ImVec2(start.x + s.x * far, start.y + s.y * far));
        dl->AddConcavePolyFilled(poly.data(), int(poly.size()), fill);
        dl->AddPolyline(path.data(), int(path.size()), line, ImDrawFlags_None, 2.0f);
    };
    if (!cutPolyline_.empty()) {
        std::vector<ImVec2> path = cutPolyline_;
        path.push_back(mouse);
        const bool closing = cutPolyline_.size() >= 3 &&
                             std::hypot(mouse.x - cutPolyline_.front().x, mouse.y - cutPolyline_.front().y) < kCloseRadiusPx * uiScale_;
        if (closing) {
            path.back() = cutPolyline_.front();
            dl->AddConcavePolyFilled(path.data(), int(path.size()), fill);
            dl->AddPolyline(path.data(), int(path.size()), line, ImDrawFlags_Closed, 2.0f);
        } else {
            openPath(path, alt);
        }
        for (const ImVec2& p : cutPolyline_) dl->AddCircleFilled(p, 3.5f * uiScale_, line);
        dl->AddCircle(cutPolyline_.front(), kCloseRadiusPx * uiScale_, line, 0, 1.0f);
    } else if (selectShape_ == SelectShape::CutLine) {
        openPath({selectPath_.front(), mouse}, alt);
    } else if (selectShape_ == SelectShape::CutRect) {
        const ImVec2 a = selectPath_.front();
        const ImVec2 p0(std::fmin(a.x, mouse.x), std::fmin(a.y, mouse.y)), p1(std::fmax(a.x, mouse.x), std::fmax(a.y, mouse.y));
        dl->AddRectFilled(p0, p1, fill);
        dl->AddRect(p0, p1, line, 0.0f, 0, 2.0f);
    } else {
        std::vector<ImVec2> path = selectPath_;
        path.push_back(mouse);
        dl->AddConcavePolyFilled(path.data(), int(path.size()), fill);
        dl->AddPolyline(path.data(), int(path.size()), line, ImDrawFlags_Closed, 2.0f);
    }
    dl->PopClipRect();
}

void App::drawCutSection() {
    ImGui::TextDisabled("%s", tr("Ctrl+Shift+drag a Cut shape"));
    ui::Segmented(*this, {"cut.line", "cut.lasso"}); // two rows: the names fit a narrow tray
    ui::Segmented(*this, {"cut.rect", "cut.polyline"});
    ui::Segmented(*this, {"cut.away", "cut.in", "cut.add"});
    if (cutMode_ != 0) {
        ImGui::SetNextItemWidth(-1);
        ui::SliderInside("##cutdepth", &cutDepth_, 0.01f, 1.0f, tr("Depth %.2f"));
        ui::HelpText("Depth", nullptr, tr("How deep Cut In goes, or how far Cut Add stands out, as a share of the Part's size."),
                     "Tool > Cut");
    }
}

} // namespace sf
