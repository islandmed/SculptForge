// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The stroke popup (Discord 11-02: "a popup for stroke type selection; the constant Ctrl+Shift / Alt combinations
// interrupt the thought process"). One key (stroke.popup: G, Z in the Blender style) or the left shelf's button
// opens a plain grid at the cursor, the current stroke under the pointer: the stroke types, then Add / Sub and
// the latched modifiers Smooth, Mask and Select, then the mask and Cut shapes (they latch Mask or Select with that
// shape: a plain drag masks or cuts with it, Discord 11-03). Hold the key, point, let go: picked (a pen hovering works the
// same). Tap the key: the grid stays open; a click on a stroke picks it and closes, a click on a modifier toggles
// it and leaves the grid open, a click beside it or Esc closes it. The click never reaches the model. The grid's
// geometry and the latch rules are in core/StrokePopup.h.
#include "app/App.h"
#include "app/Theme.h"
#include "app/Ui.h"

#include <IconsLucide.h>

#include <algorithm>
#include <cfloat>
#include <cstdio>
#include <iterator>
#include <string>
#include <string_view>

namespace sf {

namespace {

constexpr double kStrokePopupTapSeconds = 0.25; // let go sooner without pointing at an item: a tap, it stays open

struct ModifierLook {
    const char* label;
    const char* glyph;
    const char* help;
};
constexpr ModifierLook kModifiers[] = {
    {"Add", ICON_LC_CIRCLE_PLUS, "Brushes add to the surface."},
    {"Sub", ICON_LC_CIRCLE_MINUS, "Brushes subtract from the surface."},
    {"Smooth", ICON_LC_WAVES, "Strokes smooth, as with its key held, until you pick Smooth again or another brush."},
    {"Mask", ICON_LC_VENETIAN_MASK, "Strokes paint the mask, as with its key held, until you pick Mask again or another brush."},
    {"Select", ICON_LC_LASSO_SELECT,
     "Strokes draw the selection (hide) shape, as with its keys held, until you pick Select again or another brush."},
};
static_assert(std::size(kModifiers) == size_t(kStrokePopupModifiers));
// The shapes (Discord 11-03): what they latch is in the corner, the shape under the icon.
struct ShapeLook {
    const char* label;
    const char* glyph;
    const char* kind; // "Mask" or "Cut"
    const char* help;
};
constexpr ShapeLook kShapes[] = {
    {"Pen", ICON_LC_PEN_TOOL, "Mask", "Strokes paint the mask (Ctrl erases it), no key held, until you pick it again or another brush."},
    {"Lasso", ICON_LC_LASSO, "Mask", "A drag draws a lasso and masks inside it (Ctrl unmasks), no key held, until you pick it again."},
    {"Rect", ICON_LC_SQUARE_DASHED, "Mask", "A drag draws a rectangle and masks inside it (Ctrl unmasks), no key held, until you pick it again."},
    {"Circle", ICON_LC_CIRCLE_DASHED, "Mask", "A drag draws a circle from its centre and masks inside it (Ctrl unmasks), no key held, until you pick it again."},
    {"Line", ICON_LC_SLASH, "Cut", "A drag draws a Cut line (Alt: the other side), no key held, until you pick it again. Tool > Cut sets the mode."},
    {"Lasso", ICON_LC_SCISSORS, "Cut", "A drag draws a Cut lasso, no key held, until you pick it again. Tool > Cut sets the mode."},
    {"Rect", ICON_LC_SQUARE_DASHED, "Cut", "A drag draws a Cut rectangle, no key held, until you pick it again. Tool > Cut sets the mode."},
    {"Circle", ICON_LC_CIRCLE_DASHED, "Cut", "A drag draws a Cut circle from its centre (Alt at the press: from a corner), no key held, until you pick it again. Tool > Cut sets the mode."},
    {"Polyline", ICON_LC_WAYPOINTS, "Cut", "Clicks place a Cut polyline's points (the first point closes it, Enter ends it), no key held."},
};
static_assert(std::size(kShapes) == size_t(kStrokePopupShapes));

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
    a.description = "A grid at the cursor with the stroke types, the modifiers (Add / Sub, Smooth, Mask, Select) and "
                    "the mask and Cut shapes. Hold the key, point at one and let go; or tap the key and click. Smooth, "
                    "Mask, Select and the shapes stay on without holding Shift or Ctrl, until you pick them again or "
                    "another brush.";
    a.run = [this] { openStrokePopup(keyDown_ != SDLK_UNKNOWN, int(keyDown_)); };
    a.checked = [this] { return strokePopup_.open; };
    actions_.add(std::move(a)); // (its key depends on the input style: applyInputStyle in Input.cpp)
}

// The keys a modifier tile saves holding: the active input style's own (core/StrokePopup.h), not the Classic ones.
std::string App::strokeModifierKeysText(StrokeModifier m) const {
    // Blender with Emulate 3 Button Mouse: Alt is the middle button and masks nothing; the Mask brush's key does.
    if (m == StrokeModifier::Mask && blenderStyle() && uiPrefs_.emulate3Button) return actions_.shortcutText("mask.brush");
    return strokeModifierKeys(uiPrefs_.inputStyle, m);
}

StrokeLatch App::strokeLatch() const {
    const StrokeLatchState& l = strokeLatch_;
    if (l.latch == StrokeLatch::None) return StrokeLatch::None;
    const bool sameBrush = brush_ == l.brush && doc_.activeBrush == l.custom && paintMode_ == l.paint && !guidesMode_;
    return sameBrush && latchToolOk() ? l.latch : StrokeLatch::None;
}

// A latch is for sculpting strokes: choosing the Gizmo (any mode) or another tool that takes the drags ends it, for good
// (drawStrokePopup clears it), so a drag on a handle or a point never turns into a cut, a selection or a mask.
bool App::latchToolOk() const {
    return !gizmoActive() && !polyMode_ && !edgesMode_ && !tubeDraw_ && insertType_.empty() && !facePlacing_ &&
           !gradient_.armed && !capture_.armed && !stencil_.adjust && !forgesphereActive();
}

// The popup's Smooth / Mask / Select / shape tiles are sculpting strokes: the Gizmo and the tools that take the drags give way.
void App::leaveToolsForLatch() {
    if (gizmoActive()) setGizmoMode(GizmoMode::Off);
    if (polyMode_) setPolyMode(false);
    leaveDrawModes();
}

void App::setStrokeLatch(StrokeLatch latch) {
    strokeLatch_ = {latch, brush_, doc_.activeBrush, paintMode_};
    status_ = latch == StrokeLatch::None
                  ? std::string("Modifiers off: strokes sculpt")
                  : std::string(strokeLatchName(latch)) + " on: strokes " +
                        (latch == StrokeLatch::Smooth ? "smooth" : latch == StrokeLatch::Mask ? "mask" : "select") +
                        " until you pick " + strokeLatchName(latch) + " again or another brush";
    settleHideLatch();
}

void App::settleHideLatch() {
    if (!hideLatch_.active || strokeLatch() == StrokeLatch::Select) return;
    hideLatch_.active = false;
    if (hideShape_ != hideLatch_.latched) return; // a shape picked on the panel meanwhile stays
    hideShape_ = hideLatch_.before;
    cutPolyline_.clear(); // (points of a Cut Polyline the latch was placing)
}

App::SelectShape App::selectShapeOf(StrokeShape shape) {
    switch (shape) {
    case StrokeShape::MaskPen: return SelectShape::Pen;
    case StrokeShape::MaskLasso: return SelectShape::Lasso;
    case StrokeShape::MaskRect: return SelectShape::Rect;
    case StrokeShape::MaskCircle: return SelectShape::Circle;
    case StrokeShape::CutLine: return SelectShape::CutLine;
    case StrokeShape::CutLasso: return SelectShape::CutLasso;
    case StrokeShape::CutRect: return SelectShape::CutRect;
    case StrokeShape::CutCircle: return SelectShape::CutCircle;
    default: return SelectShape::CutPolyline;
    }
}

bool App::strokeShapeOn(StrokeShape shape) const {
    const StrokeLatch latch = strokeLatch();
    if (latch != strokeShapeLatch(shape)) return false;
    return (latch == StrokeLatch::Mask ? maskShape_ : hideShape_) == selectShapeOf(shape);
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
    if (strokePopupIsShape(item)) { // the shape, latched: a plain drag masks or cuts with it (Discord 11-03)
        leaveToolsForLatch();
        const StrokeShape shape = strokePopupShape(item);
        const bool off = strokeShapeOn(shape);
        const StrokeLatch latch = strokeShapeLatch(shape);
        if (latch == StrokeLatch::Select) {
            if (!hideLatch_.active) hideLatch_ = {true, hideShape_, hideShape_};
            hideLatch_.latched = selectShapeOf(shape);
        }
        (latch == StrokeLatch::Mask ? maskShape_ : hideShape_) = selectShapeOf(shape);
        setStrokeLatch(off ? StrokeLatch::None : latch);
        const ShapeLook& look = kShapes[int(shape)];
        if (!off)
            status_ = std::string(std::string_view(look.label) == "Rect" ? "Rectangle" : look.label) + " " +
                      (latch == StrokeLatch::Mask ? "mask" : "cut") + " on: " +
                      (latch == StrokeLatch::Mask ? "drags mask without Ctrl (Ctrl unmasks)" : "drags cut without Ctrl+Shift") +
                      " until you pick it again or another brush";
        if (!latchToolOk()) status_ = "Leave the Forge Spheres or Stencil tool first: a latched shape is for sculpting strokes";
        if (close) closeStrokePopup();
        return;
    }
    const StrokeModifier m = strokePopupModifier(item);
    if (m == StrokeModifier::Add || m == StrokeModifier::Sub) {
        invertToggle_ = m == StrokeModifier::Sub;
        status_ = invertToggle_ ? "Sub: brushes subtract (Alt adds for one stroke)"
                                : "Add: brushes add (Alt subtracts for one stroke)";
    } else {
        leaveToolsForLatch();
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
    // Another brush picked ends the latch for good: going back to the first brush doesn't bring it back.
    if (latch == StrokeLatch::None) strokeLatch_.latch = StrokeLatch::None;
    settleHideLatch();
    if (!strokePopup_.open) {
        if (latch == StrokeLatch::None) return;
        // A latched modifier shows beside the brush cursor: strokes do something other than sculpt.
        const ImGuiIO& io = ImGui::GetIO();
        std::string label = strokeLatchName(latch); // with the shape it draws: "Lasso Mask", "Cut Line"
        for (int i = 0; i < kStrokePopupShapes; ++i)
            if (StrokeShape(i) != StrokeShape::MaskPen && strokeShapeOn(StrokeShape(i)))
                label = std::string(kShapes[i].kind) + " " + kShapes[i].label;
        ImFont* font = fontSmall_ ? fontSmall_ : ImGui::GetFont();
        auto pill = [&](const char* text, ImVec2 a) {
            const ImVec2 ts = font->CalcTextSizeA(font->FontSize, FLT_MAX, 0.0f, text);
            const ImVec2 b(a.x + ts.x + 12.0f * s, a.y + ts.y + 6.0f * s);
            fg->AddRectFilled(a, b, theme::u32(theme::panelBg, 0.92f), (b.y - a.y) * 0.5f);
            fg->AddRect(a, b, theme::u32(theme::accent), (b.y - a.y) * 0.5f, 0, 1.0f * s);
            fg->AddText(font, font->FontSize, ImVec2(a.x + 6.0f * s, a.y + 3.0f * s), theme::u32(theme::textBright), text);
            return ts.x + 12.0f * s;
        };
        if (!io.WantCaptureMouse && canvasRect().contains(mouseX_, mouseY_) && radial_.menu < 0 && !quickMenu_.open)
            pill(label.c_str(), ImVec2(mouseX_ + 14.0f * s, mouseY_ + 14.0f * s));
        // Always in the corner of the canvas too, with the way out (users could not find it: Discord 11-26).
        if (radial_.menu < 0 && !quickMenu_.open) {
            char text[160];
            std::snprintf(text, sizeof text, tr("%s on  \xc2\xb7  Esc turns it off"), label.c_str());
            const CanvasRect c = canvasRect();
            const ImVec2 ts = font->CalcTextSizeA(font->FontSize, FLT_MAX, 0.0f, text);
            pill(text, ImVec2(c.x + (c.w - ts.x - 12.0f * s) * 0.5f, c.y + 44.0f * s)); // below the history strip
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
    auto header = [&](int section, const char* title, const char* note) {
        const float y = o.y + g.sectionTop(section);
        const float ty = y + (g.header - small->FontSize) * 0.5f - 1.0f * s;
        fg->AddText(small, small->FontSize, ImVec2(o.x + g.pad, ty), theme::u32(theme::textDim), title);
        const ImVec2 ns = tiny->CalcTextSizeA(tiny->FontSize, FLT_MAX, 0.0f, note);
        fg->AddText(tiny, tiny->FontSize, ImVec2(o.x + size.x - g.pad - ns.x, y + (g.header - tiny->FontSize) * 0.5f - 1.0f * s),
                    theme::u32(theme::textDim), note);
    };
    header(0, "Stroke", "Let go over one, or click  \xc2\xb7  Esc");
    header(1, "Modifiers", "Smooth, Mask, Select stay on");
    header(2, "Shapes", "Drag with no key held");

    const glm::vec2 is = g.itemSize();
    for (int item = 0; item < kStrokePopupItems; ++item) {
        const glm::vec2 m = g.itemMin(item);
        const ImVec2 a(o.x + m.x, o.y + m.y), b(a.x + is.x, a.y + is.y);
        const ImVec2 iconB(b.x, a.y + g.tile);
        const bool stroke = strokePopupIsStroke(item), shape = strokePopupIsShape(item);
        const StrokeModifier mod = stroke || shape ? StrokeModifier::Count : strokePopupModifier(item);
        const bool on = stroke ? strokeType_ == StrokeType(item)
                        : shape ? strokeShapeOn(strokePopupShape(item))
                        : mod == StrokeModifier::Add ? !invertToggle_
                        : mod == StrokeModifier::Sub ? invertToggle_
                                                     : latch == strokeModifierLatch(mod);
        const bool hot = item == strokePopup_.hover;
        const float r = theme::rounding(4.0f * s);
        fg->AddRectFilled(a, b, theme::u32(hot ? theme::buttonHover : on ? theme::toggleOn : theme::tile), r);
        fg->AddRect(a, b, theme::u32(hot || on ? theme::accent : theme::tileBorder, hot ? 1.0f : on ? 0.8f : 1.0f), r, 0,
                    (hot ? 2.0f : 1.0f) * s);
        const char* label = stroke ? strokeTypeName(StrokeType(item))
                            : shape ? kShapes[int(strokePopupShape(item))].label
                                    : kModifiers[int(mod)].label;
        if (stroke) {
            drawStrokeIcon(fg, a, iconB, StrokeType(item));
        } else {
            const char* glyph = shape ? kShapes[int(strokePopupShape(item))].glyph : kModifiers[int(mod)].glyph;
            // The keys a modifier stands for, or what a shape does (Mask, Cut), in the corner.
            const std::string keys = shape ? std::string() : strokeModifierKeysText(mod);
            const char* corner = shape ? kShapes[int(strokePopupShape(item))].kind : keys.c_str();
            const float fs = g.tile * 0.42f;
            const ImVec2 gs = ImGui::GetFont()->CalcTextSizeA(fs, FLT_MAX, 0.0f, glyph);
            fg->AddText(ImGui::GetFont(), fs, ImVec2(a.x + (g.tile - gs.x) * 0.5f, a.y + (g.tile - gs.y) * 0.5f + 2.0f * s),
                        theme::u32(on ? theme::accent : theme::text), glyph);
            if (*corner) {
                const ImVec2 ks = tiny->CalcTextSizeA(tiny->FontSize, FLT_MAX, 0.0f, corner);
                fg->AddText(tiny, tiny->FontSize, ImVec2(b.x - ks.x - 4.0f * s, a.y + 3.0f * s), theme::u32(theme::textDim),
                            corner);
            }
        }
        const ImVec2 ls = small->CalcTextSizeA(small->FontSize, FLT_MAX, 0.0f, label);
        fg->AddText(small, small->FontSize, ImVec2(a.x + (is.x - ls.x) * 0.5f, iconB.y + (g.label - ls.y) * 0.5f - 2.0f * s),
                    theme::u32(on || hot ? theme::textBright : theme::textOff), label);
    }
    // The hovered item's line of help under the panel (Ctrl-hover help doesn't reach a foreground drawing).
    if (strokePopup_.hover >= 0 && !strokePopupIsStroke(strokePopup_.hover)) {
        const char* help = strokePopupIsShape(strokePopup_.hover) ? kShapes[int(strokePopupShape(strokePopup_.hover))].help
                                                                 : kModifiers[int(strokePopupModifier(strokePopup_.hover))].help;
        const ImVec2 hs = small->CalcTextSizeA(small->FontSize, FLT_MAX, 0.0f, help);
        const ImVec2 a(o.x, o.y + size.y + 4.0f * s), b(o.x + hs.x + 14.0f * s, a.y + hs.y + 8.0f * s);
        fg->AddRectFilled(a, b, theme::u32(theme::panelBg, 0.95f), theme::rounding(4.0f * s));
        fg->AddText(small, small->FontSize, ImVec2(a.x + 7.0f * s, a.y + 4.0f * s), theme::u32(theme::text), help);
    }
}

} // namespace sf
