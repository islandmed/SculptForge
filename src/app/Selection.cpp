// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Screen selections: the rectangle and lasso drags shared by visibility
// (Ctrl+Shift, see PanelPolygroups.cpp) and masking (Ctrl with MaskLasso or
// MaskRect, see PanelMasking.cpp). This file tracks and draws the shape; the
// panels decide what a click or a finished drag does. The selection itself
// runs as hide_rect / hide_lasso / mask_rect / mask_lasso commands with the
// shape in normalised device coordinates and the current view.
#include "app/App.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace sf {

namespace {

constexpr float kClickSlopPx = 4.0f;    // movement that turns a click into a drag
constexpr float kLassoSpacingPx = 3.0f; // lasso points closer than this to the last one are skipped

std::string matrixString(const glm::mat4& m) {
    std::string s;
    char buf[32];
    for (int i = 0; i < 16; ++i) {
        std::snprintf(buf, sizeof buf, "%s%.9g", i ? " " : "", double(m[i / 4][i % 4]));
        s += buf;
    }
    return s;
}

// Fills a polygon with the even-odd rule, the inside test the selection uses, one pixel row at a
// time (ImGui's concave fill expects a simple polygon; a hand-drawn lasso often crosses itself).
void fillEvenOdd(ImDrawList* dl, const std::vector<ImVec2>& poly, ImU32 color) {
    if (poly.size() < 3) return;
    float top = poly[0].y, bottom = poly[0].y;
    for (const ImVec2& p : poly) {
        top = std::fmin(top, p.y);
        bottom = std::fmax(bottom, p.y);
    }
    std::vector<float> xs;
    for (float y = std::floor(top) + 0.5f; y < bottom; y += 1.0f) {
        xs.clear();
        for (size_t i = 0; i < poly.size(); ++i) {
            const ImVec2 a = poly[i], b = poly[(i + 1) % poly.size()];
            if ((a.y > y) != (b.y > y)) xs.push_back(a.x + (y - a.y) / (b.y - a.y) * (b.x - a.x));
        }
        std::sort(xs.begin(), xs.end());
        for (size_t i = 0; i + 1 < xs.size(); i += 2)
            dl->AddRectFilled(ImVec2(xs[i], y - 0.5f), ImVec2(xs[i + 1], y + 0.5f), color);
    }
}

} // namespace

void App::beginSelection(float x, float y, SelectTarget target, SelectShape shape) {
    selectTarget_ = target;
    selectShape_ = shape;
    selectPath_.assign(1, ImVec2(x, y));
    selectMoved_ = false;
    drag_ = Drag::Select;
}

void App::selectionMove(float x, float y) {
    const ImVec2 start = selectPath_.front();
    if (!selectMoved_ && std::abs(x - start.x) < kClickSlopPx && std::abs(y - start.y) < kClickSlopPx) return;
    selectMoved_ = true;
    if (selectShape_ != SelectShape::Lasso && selectShape_ != SelectShape::CutLasso) return; // a rectangle or line only needs the cursor
    const ImVec2 last = selectPath_.back();
    if (std::hypot(x - last.x, y - last.y) >= kLassoSpacingPx) selectPath_.push_back(ImVec2(x, y));
}

void App::selectionMouseUp() {
    if (isCutShape(selectShape_))
        cutMouseUp(); // PanelCut.cpp
    else if (selectTarget_ == SelectTarget::Hide)
        polygroupMouseUp();
    else
        maskSelectMouseUp();
    selectPath_.clear();
}

bool App::runSelection(const char* rectCommand, const char* lassoCommand, Args args) {
    const CanvasRect c = canvasRect();
    if (c.w <= 0.0f || c.h <= 0.0f || selectPath_.empty()) return false;
    // Window pixels -> normalised device coordinates (y up).
    auto ndc = [&](ImVec2 p) { return glm::vec2((p.x - c.x) / c.w * 2.0f - 1.0f, 1.0f - (p.y - c.y) / c.h * 2.0f); };
    args["view_proj"] = matrixString(camera_.projection() * camera_.view());
    if (selectShape_ == SelectShape::Lasso) {
        std::vector<ImVec2> path = selectPath_;
        path.push_back(ImVec2(mouseX_, mouseY_));
        if (path.size() < 3) return false;
        std::string points;
        char buf[48];
        for (const ImVec2& p : path) {
            const glm::vec2 q = ndc(p);
            std::snprintf(buf, sizeof buf, "%s%.5f %.5f", points.empty() ? "" : " ", double(q.x), double(q.y));
            points += buf;
        }
        args["points"] = points;
        runCommand(lassoCommand, args);
        return true;
    }
    const glm::vec2 a = ndc(selectPath_.front()), b = ndc(ImVec2(mouseX_, mouseY_));
    args["x0"] = double(a.x);
    args["y0"] = double(a.y);
    args["x1"] = double(b.x);
    args["y1"] = double(b.y);
    runCommand(rectCommand, args);
    return true;
}

void App::drawSelectionOverlay() {
    drawCutOverlay(); // the Cut shapes draw their own (PanelCut.cpp)
    if (drag_ != Drag::Select || !selectMoved_ || selectPath_.empty() || isCutShape(selectShape_)) return;
    // Visibility: green keeps the inside, red hides it (Alt). Mask: a dark veil masks, blue unmasks (Alt).
    const bool alt = (inputMods() & SDL_KMOD_ALT) != 0;
    ImU32 line, fill;
    if (selectTarget_ == SelectTarget::Hide) {
        line = alt ? IM_COL32(255, 110, 110, 230) : IM_COL32(120, 230, 140, 230);
        fill = alt ? IM_COL32(255, 110, 110, 40) : IM_COL32(120, 230, 140, 30);
    } else {
        line = alt ? IM_COL32(120, 200, 255, 230) : IM_COL32(230, 230, 230, 230);
        fill = alt ? IM_COL32(120, 200, 255, 35) : IM_COL32(0, 0, 0, 70);
    }
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const CanvasRect c = canvasRect();
    dl->PushClipRect(ImVec2(c.x, c.y), ImVec2(c.x + c.w, c.y + c.h), true);
    const ImVec2 start = selectPath_.front(), mouse(mouseX_, mouseY_);
    if (selectShape_ == SelectShape::Lasso) {
        std::vector<ImVec2> path = selectPath_;
        path.push_back(mouse);
        fillEvenOdd(dl, path, fill);
        dl->AddPolyline(path.data(), int(path.size()), line, ImDrawFlags_Closed, 1.5f);
    } else {
        const ImVec2 p0(std::fmin(start.x, mouse.x), std::fmin(start.y, mouse.y));
        const ImVec2 p1(std::fmax(start.x, mouse.x), std::fmax(start.y, mouse.y));
        dl->AddRectFilled(p0, p1, fill);
        dl->AddRect(p0, p1, line, 0.0f, 0, 1.5f);
    }
    dl->PopClipRect();
}

} // namespace sf
