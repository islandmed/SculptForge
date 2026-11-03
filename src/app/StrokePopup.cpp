// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The stroke popup (Discord 11-02: "a popup for stroke type selection; the constant Ctrl+Shift / Alt combinations
// interrupt the thought process"). One key (stroke.popup: G, Z in the Blender style) or the left shelf's button
// opens a plain grid at the cursor, the current stroke under the pointer: the stroke types, then Add / Sub and
// the latched modifiers Smooth, Mask and Select. Hold the key, point, let go: picked (a pen hovering works the
// same). Tap the key: the grid stays open; a click on a stroke picks it and closes, a click on a modifier toggles
// it and leaves the grid open, a click beside it or Esc closes it. The click never reaches the model. The grid's
// geometry and the latch rules are in core/StrokePopup.h.
#include "app/App.h"
#include "app/Theme.h"
#include "app/Ui.h"

#include <IconsLucide.h>

#include <algorithm>
#include <cfloat>
#include <iterator>
#include <string>

namespace sf {

namespace {

constexpr double kStrokePopupTapSeconds = 0.25; // let go sooner without pointing at an item: a tap, it stays open

struct ModifierLook {
    const char* label;
    const char* glyph;
    const char* keys; // what it saves holding (the ZBrush keys; the latches stand for them in every style)
    const char* help;
};
constexpr ModifierLook kModifiers[] = {
    {"Add", ICON_LC_CIRCLE_PLUS, "", "Brushes add to the surface."},
    {"Sub", ICON_LC_CIRCLE_MINUS, "Alt", "Brushes subtract from the surface."},
    {"Smooth", ICON_LC_WAVES, "Shift", "Strokes smooth, as with Shift held, until you pick Smooth again or another brush."},
    {"Mask", ICON_LC_VENETIAN_MASK, "Ctrl", "Strokes paint the mask, as with Ctrl held, until you pick Mask again or another brush."},
    {"Select", ICON_LC_LASSO_SELECT, "Ctrl+Shift",
     "Strokes draw the selection (hide) shape, as with Ctrl+Shift held, until you pick Select again or another brush."},
};
static_assert(std::size(kModifiers) == size_t(kStrokePopupModifiers));

StrokePopupGeometry popupGeometry(float s) {
    StrokePopupGeometry g;
    g.tile = 64.0f * s;
    g.label = 20.0f * s;
    g.gap = 6.0f * s;
    g.header = 22.0f * s;
    g.pad = 10.0f * s;
    g.columns = 5;
    return g;
}

} // namespace

void App::registerStrokePopupActions() {
    Action a;
    a.id = "stroke.popup";
    a.label = "Stroke popup";
    a.icon = ICON_LC_LAYOUT_GRID;
    a.palette = "Stroke";
    a.description = "A grid at the cursor with the stroke types and the modifiers (Add / Sub, Smooth, Mask, Select). "
                    "Hold the key, point at one and let go; or tap the key and click. Smooth, Mask and Select stay on "
                    "without holding Shift or Ctrl, until you pick them again or another brush.";
    a.run = [this] { openStrokePopup(keyDown_ != SDLK_UNKNOWN, int(keyDown_)); };
    a.checked = [this] { return strokePopup_.open; };
    actions_.add(std::move(a)); // (its key depends on the input style: applyInputStyle in Input.cpp)
}

StrokeLatch App::strokeLatch() const {
    const StrokeLatchState& l = strokeLatch_;
    if (l.latch == StrokeLatch::None) return StrokeLatch::None;
    const bool sameBrush = brush_ == l.brush && doc_.activeBrush == l.custom && paintMode_ == l.paint && !guidesMode_;
    return sameBrush ? l.latch : StrokeLatch::None;
}

void App::setStrokeLatch(StrokeLatch latch) {
    strokeLatch_ = {latch, brush_, doc_.activeBrush, paintMode_};
    status_ = latch == StrokeLatch::None
                  ? std::string("Modifiers off: strokes sculpt")
                  : std::string(strokeLatchName(latch)) + " on: strokes " +
                        (latch == StrokeLatch::Smooth ? "smooth" : latch == StrokeLatch::Mask ? "mask" : "select") +
                        " until you pick " + strokeLatchName(latch) + " again or another brush";
}

void App::openStrokePopup(bool held, int key) {
    if (strokePopup_.open && !strokePopup_.held) { // tapped open, its key (or the button) again: close
        closeStrokePopup();
        return;
    }
    if (drag_ != Drag::None) return; // not in the middle of a stroke or a view drag
    quickMenu_.open = false;
    closeRadialMenu();
    const StrokePopupGeometry g = popupGeometry(uiScale_);
    // The current stroke under the pointer (a quick tap and let go changes nothing), the whole grid on the canvas.
    const glm::vec2 cur = g.itemMin(int(strokeType_)) + g.itemSize() * 0.5f;
    const glm::vec2 size = g.size();
    const CanvasRect c = canvasRect();
    ImVec2 at(mouseX_ - cur.x, mouseY_ - cur.y);
    at.x = c.w > size.x ? std::clamp(at.x, c.x, c.x + c.w - size.x) : c.x;
    at.y = c.h > size.y ? std::clamp(at.y, c.y, c.y + c.h - size.y) : c.y;
    strokePopup_.open = true;
    strokePopup_.at = at;
    strokePopup_.openedAt = ImGui::GetTime();
    strokePopup_.held = held;
    strokePopup_.key = key;
    strokePopup_.hover = -1;
    strokePopup_.start = g.itemAt({mouseX_ - at.x, mouseY_ - at.y});
}

void App::pickStrokePopupItem(int item, bool close) {
    if (item < 0 || item >= kStrokePopupItems) return;
    strokePopup_.picked = item;
    if (strokePopupIsStroke(item)) {
        strokeType_ = StrokeType(item);
        status_ = std::string("Stroke: ") + strokeTypeName(strokeType_);
        closeStrokePopup();
        return;
    }
    const StrokeModifier m = strokePopupModifier(item);
    if (m == StrokeModifier::Add || m == StrokeModifier::Sub) {
        invertToggle_ = m == StrokeModifier::Sub;
        status_ = invertToggle_ ? "Sub: brushes subtract (Alt adds for one stroke)"
                                : "Add: brushes add (Alt subtracts for one stroke)";
    } else {
        setStrokeLatch(toggledStrokeLatch(strokeLatch(), strokeModifierLatch(m)));
    }
    if (close) closeStrokePopup();
}

void App::strokePopupKeyUp(SDL_Keycode key) {
    if (!strokePopup_.open || !strokePopup_.held || SDL_Keycode(strokePopup_.key) != key) return;
    const StrokePopupGeometry g = popupGeometry(uiScale_);
    const int item = g.itemAt({mouseX_ - strokePopup_.at.x, mouseY_ - strokePopup_.at.y});
    const bool tap = ImGui::GetTime() - strokePopup_.openedAt < kStrokePopupTapSeconds;
    // Let go quickly over the item it opened under (the current stroke, or another one where the grid had to move
    // to stay on the canvas) and it was a tap: it stays open for a click. Pointed at another item, or held a
    // moment, and let go: that item.
    if (item >= 0 && (!tap || item != strokePopup_.start)) {
        pickStrokePopupItem(item, true);
        return;
    }
    if (tap) {
        strokePopup_.held = false;
        return;
    }
    closeStrokePopup(); // held and let go beside the items: nothing
}

bool App::strokePopupMouseEvent(const SDL_Event& e) {
    if (e.type != SDL_EVENT_MOUSE_BUTTON_DOWN && e.type != SDL_EVENT_MOUSE_BUTTON_UP) return false;
    const int button = e.button.button;
    const uint32_t bit = button >= 0 && button < 32 ? 1u << button : 0u;
    if (e.type == SDL_EVENT_MOUSE_BUTTON_UP) {
        if (!(strokePopupSwallowUp_ & bit)) return false;
        strokePopupSwallowUp_ &= ~bit; // the press went to the popup: so does the release
        return true;
    }
    if (!strokePopup_.open) return false;
    // Open: a left click (mouse or pen tip) on an item picks it; inside the panel elsewhere does nothing; any
    // other click closes it. Nothing reaches the canvas or the panels under it.
    strokePopupSwallowUp_ |= bit;
    if (button == SDL_BUTTON_LEFT) {
        const StrokePopupGeometry g = popupGeometry(uiScale_);
        const glm::vec2 p(e.button.x - strokePopup_.at.x, e.button.y - strokePopup_.at.y);
        const int item = g.itemAt(p);
        if (item >= 0) {
            strokePopup_.held = false; // (its key's release must not pick again)
            pickStrokePopupItem(item, false);
            return true;
        }
        const glm::vec2 size = g.size();
        if (p.x >= 0.0f && p.y >= 0.0f && p.x < size.x && p.y < size.y) return true;
    }
    closeStrokePopup();
    return true;
}

bool App::strokePopupItemScreen(int item, ImVec2& out) const {
    if (!strokePopup_.open || item < 0 || item >= kStrokePopupItems) return false;
    const StrokePopupGeometry g = popupGeometry(uiScale_);
    const glm::vec2 c = g.itemMin(item) + g.itemSize() * 0.5f;
    out = ImVec2(strokePopup_.at.x + c.x, strokePopup_.at.y + c.y);
    return true;
}

void App::drawStrokePopup() {
    const float s = uiScale_;
    ImDrawList* fg = ImGui::GetForegroundDrawList();
    const StrokeLatch latch = strokeLatch();
    if (!strokePopup_.open) {
        // A latched modifier shows beside the brush cursor: strokes do something other than sculpt.
        const ImGuiIO& io = ImGui::GetIO();
        if (latch != StrokeLatch::None && !io.WantCaptureMouse && canvasRect().contains(mouseX_, mouseY_) &&
            radial_.menu < 0 && !quickMenu_.open) {
            ImFont* font = fontSmall_ ? fontSmall_ : ImGui::GetFont();
            const char* text = strokeLatchName(latch);
            const ImVec2 ts = font->CalcTextSizeA(font->FontSize, FLT_MAX, 0.0f, text);
            const ImVec2 a(mouseX_ + 14.0f * s, mouseY_ + 14.0f * s);
            const ImVec2 b(a.x + ts.x + 12.0f * s, a.y + ts.y + 6.0f * s);
            fg->AddRectFilled(a, b, theme::u32(theme::panelBg, 0.92f), (b.y - a.y) * 0.5f);
            fg->AddRect(a, b, theme::u32(theme::accent), (b.y - a.y) * 0.5f, 0, 1.0f * s);
            fg->AddText(font, font->FontSize, ImVec2(a.x + 6.0f * s, a.y + 3.0f * s), theme::u32(theme::textBright), text);
        }
        return;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        closeStrokePopup();
        return;
    }
    const StrokePopupGeometry g = popupGeometry(s);
    const ImVec2 o = strokePopup_.at;
    const glm::vec2 size = g.size();
    strokePopup_.hover = g.itemAt({mouseX_ - o.x, mouseY_ - o.y});
    const float round = theme::rounding(6.0f * s);
    fg->AddRectFilled(ImVec2(o.x + 2 * s, o.y + 3 * s), ImVec2(o.x + size.x + 2 * s, o.y + size.y + 3 * s),
                      IM_COL32(0, 0, 0, 90), round); // a soft shadow
    fg->AddRectFilled(o, ImVec2(o.x + size.x, o.y + size.y), theme::u32(theme::panelBg, 0.97f), round);
    fg->AddRect(o, ImVec2(o.x + size.x, o.y + size.y), theme::u32(theme::border), round, 0, 1.0f);

    ImFont* small = fontSmall_ ? fontSmall_ : ImGui::GetFont();
    ImFont* tiny = fontTiny_ ? fontTiny_ : small;
    auto header = [&](bool modifiers, const char* title, const char* note) {
        const float y = o.y + g.sectionTop(modifiers);
        const float ty = y + (g.header - small->FontSize) * 0.5f - 1.0f * s;
        fg->AddText(small, small->FontSize, ImVec2(o.x + g.pad, ty), theme::u32(theme::textDim), title);
        const ImVec2 ns = tiny->CalcTextSizeA(tiny->FontSize, FLT_MAX, 0.0f, note);
        fg->AddText(tiny, tiny->FontSize, ImVec2(o.x + size.x - g.pad - ns.x, y + (g.header - tiny->FontSize) * 0.5f - 1.0f * s),
                    theme::u32(theme::textDim), note);
    };
    header(false, "Stroke", "Let go over one, or click  \xc2\xb7  Esc");
    header(true, "Modifiers", "Smooth, Mask, Select stay on");

    const glm::vec2 is = g.itemSize();
    for (int item = 0; item < kStrokePopupItems; ++item) {
        const glm::vec2 m = g.itemMin(item);
        const ImVec2 a(o.x + m.x, o.y + m.y), b(a.x + is.x, a.y + is.y);
        const ImVec2 iconB(b.x, a.y + g.tile);
        const bool stroke = strokePopupIsStroke(item);
        const StrokeModifier mod = stroke ? StrokeModifier::Count : strokePopupModifier(item);
        const bool on = stroke ? strokeType_ == StrokeType(item)
                        : mod == StrokeModifier::Add ? !invertToggle_
                        : mod == StrokeModifier::Sub ? invertToggle_
                                                     : latch == strokeModifierLatch(mod);
        const bool hot = item == strokePopup_.hover;
        const float r = theme::rounding(4.0f * s);
        fg->AddRectFilled(a, b, theme::u32(hot ? theme::buttonHover : on ? theme::toggleOn : theme::tile), r);
        fg->AddRect(a, b, theme::u32(hot || on ? theme::accent : theme::tileBorder, hot ? 1.0f : on ? 0.8f : 1.0f), r, 0,
                    (hot ? 2.0f : 1.0f) * s);
        const char* label = stroke ? strokeTypeName(StrokeType(item)) : kModifiers[int(mod)].label;
        if (stroke) {
            drawStrokeIcon(fg, a, iconB, StrokeType(item));
        } else {
            const ModifierLook& look = kModifiers[int(mod)];
            const float fs = g.tile * 0.42f;
            const ImVec2 gs = ImGui::GetFont()->CalcTextSizeA(fs, FLT_MAX, 0.0f, look.glyph);
            fg->AddText(ImGui::GetFont(), fs, ImVec2(a.x + (g.tile - gs.x) * 0.5f, a.y + (g.tile - gs.y) * 0.5f + 2.0f * s),
                        theme::u32(on ? theme::accent : theme::text), look.glyph);
            if (*look.keys) { // the keys it stands for, in the corner
                const ImVec2 ks = tiny->CalcTextSizeA(tiny->FontSize, FLT_MAX, 0.0f, look.keys);
                fg->AddText(tiny, tiny->FontSize, ImVec2(b.x - ks.x - 4.0f * s, a.y + 3.0f * s), theme::u32(theme::textDim),
                            look.keys);
            }
        }
        const ImVec2 ls = small->CalcTextSizeA(small->FontSize, FLT_MAX, 0.0f, label);
        fg->AddText(small, small->FontSize, ImVec2(a.x + (is.x - ls.x) * 0.5f, iconB.y + (g.label - ls.y) * 0.5f - 2.0f * s),
                    theme::u32(on || hot ? theme::textBright : theme::textOff), label);
    }
    // The hovered item's line of help under the panel (Ctrl-hover help doesn't reach a foreground drawing).
    if (strokePopup_.hover >= 0 && !strokePopupIsStroke(strokePopup_.hover)) {
        const char* help = kModifiers[int(strokePopupModifier(strokePopup_.hover))].help;
        const ImVec2 hs = small->CalcTextSizeA(small->FontSize, FLT_MAX, 0.0f, help);
        const ImVec2 a(o.x, o.y + size.y + 4.0f * s), b(o.x + hs.x + 14.0f * s, a.y + hs.y + 8.0f * s);
        fg->AddRectFilled(a, b, theme::u32(theme::panelBg, 0.95f), theme::rounding(4.0f * s));
        fg->AddText(small, small->FontSize, ImVec2(a.x + 7.0f * s, a.y + 4.0f * s), theme::u32(theme::text), help);
    }
}

} // namespace sf
