// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Stencil (#155, docs/design/stencil.md): the image over the view the brushes work through (core/Stencil.h). The
// Alpha palette's Stencil section loads it (an image file, or the brush's alpha) and sets it up; the overlay draws it
// under the panels, over the model. Adjust Stencil puts handles on it: drag inside to move it, a corner to scale it,
// the round handle to turn it; the arrow keys move it, - and = scale it, , and . turn it (Shift: finely). Enter or
// Esc leaves Adjust. It fades while the view moves (Hide while navigating), so the model shows as it turns under it.
#include "app/App.h"
#include "app/NativeDialog.h"
#include "app/I18n.h"
#include "app/Theme.h"
#include "app/Ui.h"

#include "render/GL.h"

#include <IconsLucide.h>

#include <algorithm>
#include <cmath>

namespace sf {

namespace {

constexpr float kCornerPx = 6.0f;  // corner handle half-size, logical px
constexpr float kRotatePx = 26.0f; // the turn handle's distance above the top edge
const SDL_DialogFileFilter kStencilFilters[] = {{"Images", "png;jpg;jpeg;bmp;tga;psd;gif"}, {"All files", "*"}};

glm::vec2 toWindow(const CanvasRect& c, glm::vec2 f) { return {c.x + f.x * c.w, c.y + f.y * c.h}; }
ImVec2 im(glm::vec2 p) { return {p.x, p.y}; }

} // namespace

void SDLCALL App::onStencilChosen(void* userdata, const char* const* files, int) {
    wakeMainLoop();
    if (!files || !files[0]) return;
    auto* app = static_cast<App*>(userdata);
    std::lock_guard lock(app->alphaMutex_);
    app->stencil_.pendingPath = files[0];
}

void App::pollStencilFile() {
    std::string path;
    {
        std::lock_guard lock(alphaMutex_);
        path.swap(stencil_.pendingPath);
    }
    if (!path.empty()) runCommand("stencil_load", {{"path", path}});
}

void App::registerStencilActions() {
    Action a;
    a.id = "stencil.load";
    a.label = "Load Stencil...";
    a.icon = ICON_LC_IMAGE;
    a.palette = "Alpha > Stencil";
    a.description = "Lay an image over the view and sculpt or paint through it: the sculpt brushes take its brightness "
                    "(white = full effect), Vertex Paint its colours, only where it covers the model.";
    a.command = "stencil_load";
    a.run = [this] { sf::dialogs::openFile(App::onStencilChosen, this, window_, kStencilFilters, 2, nullptr, false); };
    actions_.add(a);

    a = {};
    a.id = "stencil.show";
    a.label = "Show Stencil";
    a.icon = ICON_LC_EYE;
    a.palette = "Alpha > Stencil";
    a.description = "Hidden, the Stencil is off: the brushes work as if it weren't there.";
    a.command = "stencil_set";
    a.run = [this] { runCommand("stencil_set", {{"visible", !doc_.stencil.visible}}); };
    a.checked = [this] { return doc_.stencil.image && doc_.stencil.visible; };
    a.enabled = [this] { return doc_.stencil.image != nullptr; };
    actions_.add(a);

    a = {};
    a.id = "stencil.adjust";
    a.label = "Adjust Stencil";
    a.icon = ICON_LC_MOVE;
    a.palette = "Alpha > Stencil";
    a.description = "Handles on the Stencil: drag inside it to move it, a corner to scale it, the round handle to turn it "
                    "(Shift: 15 degree steps). Arrow keys move it, - and = scale it, comma and period turn it (Shift: "
                    "finely). Enter or Esc goes back to sculpting.";
    a.run = [this] {
        stencil_.adjust = !stencil_.adjust && doc_.stencil.active();
        status_ = stencil_.adjust ? "Adjust Stencil: drag to move, a corner to scale, the round handle to turn (Enter: done)"
                                  : "Adjust Stencil off";
    };
    a.checked = [this] { return stencil_.adjust; };
    a.enabled = [this] { return doc_.stencil.active(); };
    actions_.add(a);
}

// The handle under a window point: 0 inside (move), 1 a corner (scale), 2 the turn handle; -1 none.
int App::stencilHandleAt(float x, float y) const {
    const Stencil& st = doc_.stencil;
    const CanvasRect c = canvasRect();
    if (!st.active() || c.w <= 0.0f || c.h <= 0.0f) return -1;
    const float aspect = c.w / c.h, s = uiScale_;
    const glm::vec2 p(x, y);
    const glm::vec2 centre = toWindow(c, st.center);
    const glm::vec2 topMid = toWindow(c, st.canvasPoint({0.5f, 0.0f}, aspect));
    const glm::vec2 up = glm::length(topMid - centre) > 1e-3f ? glm::normalize(topMid - centre) : glm::vec2(0, -1);
    if (glm::distance(p, topMid + up * kRotatePx * s) <= (kCornerPx + 6.0f) * s) return 2;
    for (glm::vec2 uv : {glm::vec2(0, 0), glm::vec2(1, 0), glm::vec2(1, 1), glm::vec2(0, 1)})
        if (glm::distance(p, toWindow(c, st.canvasPoint(uv, aspect))) <= (kCornerPx + 6.0f) * s) return 1;
    Stencil once = st; // (the image itself, not its tiles)
    once.tile = false;
    glm::vec2 uv;
    return once.imageCoord({(x - c.x) / c.w, (y - c.y) / c.h}, aspect, uv) ? 0 : -1;
}

bool App::stencilMouseDown(float x, float y, SDL_Keymod mod) {
    if (!stencil_.adjust || (mod & (SDL_KMOD_CTRL | SDL_KMOD_SHIFT | SDL_KMOD_ALT | SDL_KMOD_GUI))) return false;
    if (!doc_.stencil.active()) {
        stencil_.adjust = false;
        return false;
    }
    stencil_.handle = stencilHandleAt(x, y);
    if (stencil_.handle < 0) return true; // (off it: nothing, while adjusting; Enter goes back to sculpting)
    stencil_.start = doc_.stencil;
    stencil_.startMouse = glm::vec2(x, y);
    drag_ = Drag::Stencil;
    return true;
}

void App::stencilMouseMove(float x, float y) {
    const CanvasRect c = canvasRect();
    if (c.w <= 0.0f || c.h <= 0.0f || stencil_.handle < 0) return;
    Stencil& st = doc_.stencil;
    const Stencil& s0 = stencil_.start;
    const glm::vec2 p(x, y), centre = toWindow(c, s0.center);
    if (stencil_.handle == 0) {
        st.center = s0.center + (p - stencil_.startMouse) / glm::vec2(c.w, c.h);
    } else if (stencil_.handle == 1) {
        const float d0 = glm::distance(stencil_.startMouse, centre), d1 = glm::distance(p, centre);
        if (d0 > 1.0f) st.size = std::clamp(s0.size * d1 / d0, 0.02f, 20.0f);
    } else {
        const glm::vec2 a = stencil_.startMouse - centre, b = p - centre;
        if (glm::length(a) > 1.0f && glm::length(b) > 1.0f) { // (y down: a turn seen counter-clockwise lowers atan2)
            float turn = std::atan2(a.y, a.x) - std::atan2(b.y, b.x);
            float deg = glm::degrees(s0.angle + turn);
            if (inputMods() & SDL_KMOD_SHIFT) deg = std::round(deg / 15.0f) * 15.0f; // (Shift: 15 degree steps)
            st.angle = glm::radians(std::remainder(deg, 360.0f));
        }
    }
}

void App::stencilMouseUp() { stencil_.handle = -1; }

bool App::stencilKey(SDL_Keycode key, SDL_Keymod mod) {
    if (!stencil_.adjust) return false;
    if (key == SDLK_RETURN || key == SDLK_KP_ENTER || key == SDLK_ESCAPE) {
        stencil_.adjust = false;
        status_ = "Adjust Stencil off";
        return true;
    }
    if (mod & (SDL_KMOD_CTRL | SDL_KMOD_ALT | SDL_KMOD_GUI)) return false;
    Stencil& st = doc_.stencil;
    const CanvasRect c = canvasRect();
    const bool fine = mod & SDL_KMOD_SHIFT;
    const float stepPx = (fine ? 1.0f : 10.0f) * uiScale_;
    const float stepX = c.w > 0.0f ? stepPx / c.w : 0.01f, stepY = c.h > 0.0f ? stepPx / c.h : 0.01f;
    switch (key) {
    case SDLK_LEFT: st.center.x -= stepX; return true;
    case SDLK_RIGHT: st.center.x += stepX; return true;
    case SDLK_UP: st.center.y -= stepY; return true;
    case SDLK_DOWN: st.center.y += stepY; return true;
    case SDLK_MINUS: case SDLK_KP_MINUS: st.size = std::max(st.size / (fine ? 1.01f : 1.1f), 0.02f); return true;
    case SDLK_EQUALS: case SDLK_KP_PLUS: st.size = std::min(st.size * (fine ? 1.01f : 1.1f), 20.0f); return true;
    case SDLK_COMMA: case SDLK_PERIOD: {
        const float deg = glm::degrees(st.angle) + (key == SDLK_COMMA ? 1.0f : -1.0f) * (fine ? 1.0f : 15.0f);
        st.angle = glm::radians(std::remainder(deg, 360.0f));
        return true;
    }
    default: return false;
    }
}

void App::drawStencilOverlay() {
    const Stencil& st = doc_.stencil;
    if (!st.active()) {
        stencil_.adjust = false;
        if (!st.image && stencil_.tex) { // removed: its texture goes too
            gl::DeleteTextures(1, &stencil_.tex);
            stencil_.tex = 0;
            stencil_.texImage = nullptr;
        }
        return;
    }
    const CanvasRect c = canvasRect();
    if (c.w <= 0.0f || c.h <= 0.0f) return;
    // The texture: the image as it shows (its negative when inverted), uploaded again only when either changes.
    if (stencil_.texImage != st.image.get() || stencil_.texInvert != st.invert || !stencil_.tex) {
        if (stencil_.tex) gl::DeleteTextures(1, &stencil_.tex);
        Image shown = *st.image;
        if (st.invert)
            for (size_t i = 0; i < shown.rgba.size(); i += 4)
                for (int k = 0; k < 3; ++k) shown.rgba[i + size_t(k)] = uint8_t(255 - shown.rgba[i + size_t(k)]);
        gl::GenTextures(1, &stencil_.tex);
        gl::BindTexture(GL_TEXTURE_2D, stencil_.tex);
        gl::PixelStorei(GL_UNPACK_ALIGNMENT, 1);
        gl::TexImage2D(GL_TEXTURE_2D, 0, GLint(GL_RGBA8), shown.width, shown.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, shown.rgba.data());
        gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GLint(GL_LINEAR));
        gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GLint(GL_LINEAR));
        gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GLint(GL_CLAMP_TO_EDGE));
        gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GLint(GL_CLAMP_TO_EDGE));
        gl::BindTexture(GL_TEXTURE_2D, 0);
        stencil_.texImage = st.image.get();
        stencil_.texInvert = st.invert;
    }
    const bool navigating = drag_ == Drag::Orbit || drag_ == Drag::Pan || drag_ == Drag::Zoom || drag_ == Drag::Roll;
    const float fade = navigating && stencil_.hideWhileNavigating ? 0.15f : 1.0f;
    const int alpha = int(std::clamp(st.opacity * fade, 0.0f, 1.0f) * 255.0f + 0.5f);
    const float aspect = c.w / c.h, s = uiScale_;
    ImDrawList* dl = ImGui::GetBackgroundDrawList(); // under the panels, over the model
    dl->PushClipRect(ImVec2(c.x, c.y), ImVec2(c.x + c.w, c.y + c.h), true);
    const ImTextureID tex = ImTextureID(uintptr_t(stencil_.tex));
    auto quad = [&](glm::vec2 o) { // the image moved by `o` whole images (tiles)
        const ImVec2 p0 = im(toWindow(c, st.canvasPoint(o + glm::vec2(0, 0), aspect)));
        const ImVec2 p1 = im(toWindow(c, st.canvasPoint(o + glm::vec2(1, 0), aspect)));
        const ImVec2 p2 = im(toWindow(c, st.canvasPoint(o + glm::vec2(1, 1), aspect)));
        const ImVec2 p3 = im(toWindow(c, st.canvasPoint(o + glm::vec2(0, 1), aspect)));
        dl->AddImageQuad(tex, p0, p1, p2, p3, ImVec2(0, 0), ImVec2(1, 0), ImVec2(1, 1), ImVec2(0, 1), IM_COL32(255, 255, 255, alpha));
    };
    if (alpha > 0) {
        if (!st.tile) {
            quad({0.0f, 0.0f});
        } else { // every repeat the canvas's corners reach (at most 40 x 40)
            glm::vec2 lo(1e9f), hi(-1e9f), uv;
            for (glm::vec2 k : {glm::vec2(0, 0), glm::vec2(1, 0), glm::vec2(1, 1), glm::vec2(0, 1)}) {
                st.imageCoord(k, aspect, uv);
                lo = glm::min(lo, uv);
                hi = glm::max(hi, uv);
            }
            const int i0 = std::max(int(std::floor(lo.x)), -20), i1 = std::min(int(std::floor(hi.x)), 20);
            const int j0 = std::max(int(std::floor(lo.y)), -20), j1 = std::min(int(std::floor(hi.y)), 20);
            for (int j = j0; j <= j1; ++j)
                for (int i = i0; i <= i1; ++i) quad({float(i), float(j)});
        }
    }
    // Its outline (always: a faint stencil still shows where it works) and, while adjusting, the handles.
    glm::vec2 corner[4];
    int k = 0;
    for (glm::vec2 uv : {glm::vec2(0, 0), glm::vec2(1, 0), glm::vec2(1, 1), glm::vec2(0, 1)})
        corner[k++] = toWindow(c, st.canvasPoint(uv, aspect));
    const ImU32 edge = stencil_.adjust ? theme::u32(theme::accent) : IM_COL32(255, 255, 255, 90);
    for (int i = 0; i < 4; ++i) {
        dl->AddLine(im(corner[i]), im(corner[(i + 1) % 4]), IM_COL32(0, 0, 0, 120), 3.0f * s);
        dl->AddLine(im(corner[i]), im(corner[(i + 1) % 4]), edge, 1.25f * s);
    }
    if (stencil_.adjust) {
        const int hot = stencil_.handle >= 0 ? stencil_.handle : stencilHandleAt(mouseX_, mouseY_);
        const float h = kCornerPx * s;
        for (const glm::vec2& p : corner) {
            dl->AddRectFilled(ImVec2(p.x - h - s, p.y - h - s), ImVec2(p.x + h + s, p.y + h + s), IM_COL32(0, 0, 0, 160));
            dl->AddRectFilled(ImVec2(p.x - h, p.y - h), ImVec2(p.x + h, p.y + h), hot == 1 ? theme::u32(theme::accent) : IM_COL32(240, 240, 240, 255));
        }
        const glm::vec2 centre = toWindow(c, st.center), topMid = 0.5f * (corner[0] + corner[1]);
        const glm::vec2 up = glm::length(topMid - centre) > 1e-3f ? glm::normalize(topMid - centre) : glm::vec2(0, -1);
        const glm::vec2 knob = topMid + up * kRotatePx * s;
        dl->AddLine(im(topMid), im(knob), edge, 1.25f * s);
        dl->AddCircleFilled(im(knob), h + 2.0f * s, IM_COL32(0, 0, 0, 160), 24);
        dl->AddCircleFilled(im(knob), h, hot == 2 ? theme::u32(theme::accent) : IM_COL32(240, 240, 240, 255), 24);
        // The centre: a small cross, lit while it moves.
        const ImU32 mid = hot == 0 ? theme::u32(theme::accent) : IM_COL32(240, 240, 240, 220);
        dl->AddLine(ImVec2(centre.x - 2 * h, centre.y), ImVec2(centre.x + 2 * h, centre.y), mid, 1.5f * s);
        dl->AddLine(ImVec2(centre.x, centre.y - 2 * h), ImVec2(centre.x, centre.y + 2 * h), mid, 1.5f * s);
    }
    dl->PopClipRect();
}

// Alpha palette: the Stencil section.
void App::drawStencilSection() {
    Stencil& st = doc_.stencil;
    const char* where = "Alpha > Stencil";
    ImGui::SeparatorText(tr("Stencil"));
    const float half = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
    if (ImGui::Button(tr(ICON_LC_IMAGE "  Load image...##stencil.load"), ImVec2(half, 0)))
        sf::dialogs::openFile(App::onStencilChosen, this, window_, kStencilFilters, 2, nullptr, false);
    ui::HelpText("Load image", nullptr,
                 "Lay an image over the view and sculpt or paint through it: the sculpt brushes take its brightness "
                 "(white = full effect), Vertex Paint its colours, only where it covers the model.",
                 where);
    ImGui::SameLine();
    ImGui::BeginDisabled(doc_.brushAlpha.empty());
    if (ImGui::Button(tr(ICON_LC_CIRCLE_DASHED "  Use alpha##stencil.use_alpha"), ImVec2(-1, 0)))
        runCommand("stencil_load", {{"alpha", true}});
    ImGui::EndDisabled();
    ui::HelpText("Use alpha", nullptr, "Make the brush's alpha (above, or one from the Library) the Stencil.", where);
    if (!st.image) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("%s", tr("Lay an image over the view, then sculpt or paint through it. Move it and repeat."));
        ImGui::PopTextWrapPos();
        return;
    }
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(st.name.c_str());
    ui::SameLineAt(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - ImGui::GetFrameHeight());
    if (ui::GlyphButton("##stencil.clear", ICON_LC_X, ImVec2(ImGui::GetFrameHeight(), ImGui::GetFrameHeight())))
        runCommand("stencil_clear");
    ui::HelpText("Remove", nullptr, "Take the Stencil off the view.", where);
    if (!st.image) return;

    // (The second of two columns. Placed with ui::SameLineAt: in the Sculpt menu's group, beside its section list,
    // SameLine(x) put Adjust, Tile and the Remove button past the panel's edge, #bug-reports 11-09.)
    const float column2 = ImGui::GetCursorPosX() + half + ImGui::GetStyle().ItemSpacing.x;
    if (ui::Checkbox(tr("Show##stencil.show"), &st.visible) && !st.visible) stencil_.adjust = false;
    ui::HelpText("Show", nullptr, "Hidden, the Stencil is off: the brushes work as if it weren't there.", where);
    ui::SameLineAt(column2);
    bool adjust = stencil_.adjust;
    ImGui::BeginDisabled(!st.visible);
    if (ui::Checkbox(tr("Adjust##stencil.adjust"), &adjust)) stencil_.adjust = adjust;
    ImGui::EndDisabled();
    ui::HelpText("Adjust", nullptr,
                 "Handles on the Stencil: drag inside it to move it, a corner to scale it, the round handle to turn it "
                 "(Shift: 15 degree steps). Arrow keys move it, - and = scale it, comma and period turn it (Shift: "
                 "finely). Enter or Esc goes back to sculpting.",
                 where);
    ui::Checkbox(tr("Invert##stencil.invert"), &st.invert);
    ui::HelpText("Invert", nullptr, "Use the image's negative: black works, white leaves the surface alone.", where);
    ui::SameLineAt(column2);
    ui::Checkbox(tr("Tile##stencil.tile"), &st.tile);
    ui::HelpText("Tile", nullptr, "Repeat the image across the whole view, for skin, fabric and other patterns.", where);
    ui::Checkbox(tr("Paint its colours##stencil.colors"), &st.colors);
    ui::HelpText("Paint its colours", nullptr,
                 "Vertex Paint lays the image's own colours. Off, its brightness masks the paint colour instead.", where);
    ui::Checkbox(tr("Hide while navigating##stencil.hide_nav"), &stencil_.hideWhileNavigating);
    ui::HelpText("Hide while navigating", nullptr,
                 "Fade the Stencil while the view turns, moves or zooms, so you can see the model under it.", where);
    ImGui::SetNextItemWidth(-1);
    ui::SliderInside("##stencil_opacity", &st.opacity, 0.0f, 1.0f, tr("Opacity %.2f"));
    ui::HelpText("Opacity", nullptr, "How strongly the Stencil shows. The brushes always take all of it.", where);
    ImGui::SetNextItemWidth(half);
    float sizePct = st.size * 100.0f;
    if (ui::SliderInside("##stencil_size", &sizePct, 2.0f, 400.0f, tr("Size %.0f%%"), ImGuiSliderFlags_Logarithmic))
        st.size = std::clamp(sizePct / 100.0f, 0.02f, 20.0f);
    ui::HelpText("Size", nullptr, "The Stencil's height, in percent of the view's height.", where);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1);
    float deg = glm::degrees(st.angle);
    if (ui::SliderInside("##stencil_angle", &deg, -180.0f, 180.0f, tr("Angle %.0f"))) st.angle = glm::radians(deg);
    ui::HelpText("Angle", nullptr, "Turn the Stencil, in degrees counter-clockwise; Ctrl+click to type.", where);
    if (ImGui::Button(tr("Reset placement##stencil.reset"), ImVec2(-1, 0))) runCommand("stencil_set", {{"reset", true}});
    ui::HelpText("Reset placement", nullptr, "Centre the Stencil again, upright, 80% of the view's height.", where);
}

bool App::stencilUiValue(const std::string& name, double& out) const {
    const Stencil& st = doc_.stencil;
    if (name == "stencil_active") out = st.active() ? 1.0 : 0.0;
    else if (name == "stencil_adjust") out = stencil_.adjust ? 1.0 : 0.0;
    else if (name == "stencil_tile") out = st.tile ? 1.0 : 0.0;
    else if (name == "stencil_x") out = std::round(double(st.center.x) * 1000.0); // x1000
    else if (name == "stencil_y") out = std::round(double(st.center.y) * 1000.0);
    else if (name == "stencil_size") out = std::round(double(st.size) * 1000.0);
    else if (name == "stencil_angle") out = std::round(double(glm::degrees(st.angle)));
    else return false;
    return true;
}

} // namespace sf
