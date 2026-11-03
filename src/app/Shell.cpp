// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The ZBrush-style shell (docs/ui-design.md §2, §3, §9): title strip, the
// alphabetical palette bar with its drop-downs, the four shelves around the
// canvas, the left and right trays with their dividers, customize-mode drag
// and drop, layout presets, and the actions behind all of it.
#include "app/App.h"

#include "app/Demo.h"
#include "app/Shell.h"
#include "app/Theme.h"
#include "app/LookSchemes.h"
#include "app/Ui.h"
#include "app/I18n.h"
#include "core/Matcap.h"
#include "core/Polypaint.h"
#include "core/Texture.h"

#include <IconsLucide.h>
#include <imgui_internal.h>

#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace sf {

namespace {

constexpr ImGuiWindowFlags kShellFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                         ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings |
                                         ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoFocusOnAppearing |
                                         ImGuiWindowFlags_NoNav;

const char* const kShelfWindows[kShelfCount] = {"##TopShelf", "##LeftShelf", "##RightShelf", "##BottomShelf"};

CanvasRect g_focus; // the tile under the pointer this frame (Outlines on: its border a faint step lighter)

bool sameRect(const CanvasRect& a, const CanvasRect& b) {
    return a.w > 0.0f && a.h > 0.0f && std::abs(a.x - b.x) < 0.5f && std::abs(a.y - b.y) < 0.5f && std::abs(a.w - b.w) < 0.5f &&
           std::abs(a.h - b.h) < 0.5f;
}

// A tiled look's frame around one region: a thin border, Blueprint's corner marks.
void tileFrame(ImDrawList* dl, const CanvasRect& r, bool focused) {
    // Outlines (Preferences > Interface > Look): off draws nothing; only on marks the tile under the pointer, and
    // only with a faint step (applyLook's focusBorder), never a thicker or accent border.
    if (!theme::look.tiled || theme::look.outlines == 0 || r.w <= 0.0f || r.h <= 0.0f) return;
    focused = focused && theme::look.outlines == 2;
    const ImFontAtlas* atlas = ImGui::GetIO().Fonts;
    const float s = atlas && !atlas->Fonts.empty() ? atlas->Fonts[0]->FontSize / 15.0f : 1.0f; // the UI scale
    const float t = std::max(1.0f, std::floor(s + 0.25f));
    const ImVec2 a(r.x, r.y), b(r.x + r.w, r.y + r.h);
    dl->PushClipRect(a, b, false);
    dl->AddRect(ImVec2(a.x + t * 0.5f, a.y + t * 0.5f), ImVec2(b.x - t * 0.5f, b.y - t * 0.5f),
                theme::u32(focused ? theme::focusBorder : theme::tileBorder), 0.0f, 0, t);
    if (theme::look.headers == theme::LookStyle::Headers::Grid) { // registration marks at the corners
        const float k = std::min(9.0f, std::min(r.w, r.h) * 0.3f);
        const ImU32 c = theme::u32(theme::accent, theme::look.outlines == 2 ? 0.75f : 0.35f);
        const float w = 2.0f * t;
        for (int i = 0; i < 4; ++i) {
            const float x = i & 1 ? b.x - w * 0.5f : a.x + w * 0.5f, y = i & 2 ? b.y - w * 0.5f : a.y + w * 0.5f;
            const float dx = i & 1 ? -k : k, dy = i & 2 ? -k : k;
            dl->AddLine(ImVec2(x, y), ImVec2(x + dx, y), c, w);
            dl->AddLine(ImVec2(x, y), ImVec2(x, y + dy), c, w);
        }
    }
    dl->PopClipRect();
}

// `frame` false: the caller draws the tile's frame itself, after the contents (shelves, #98).
bool beginShellWindow(const char* name, const CanvasRect& r, const ImVec4& bg, ImVec2 padding,
                      ImGuiWindowFlags extra = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse,
                      bool frame = true) {
    ImGui::SetNextWindowPos(ImVec2(r.x, r.y));
    ImGui::SetNextWindowSize(ImVec2(r.w, r.h));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, padding);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(1.0f, 1.0f)); // the title strip is only 24 px high
    ImGui::PushStyleColor(ImGuiCol_WindowBg, bg);
    const bool open = ImGui::Begin(name, nullptr, kShellFlags | extra);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(4);
    if (open && frame) tileFrame(ImGui::GetWindowDrawList(), r, sameRect(r, g_focus));
    return open;
}

std::string lower(std::string s) {
    for (auto& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string brushActionId(BrushType t) {
    std::string name = lower(brushName(t));
    name.erase(std::remove(name.begin(), name.end(), ' '), name.end());
    return "brush." + name;
}

std::string shortCount(size_t n) {
    char buf[32];
    if (n >= 1000000) std::snprintf(buf, sizeof buf, "%.2fM", double(n) / 1e6);
    else if (n >= 10000) std::snprintf(buf, sizeof buf, "%.1fk", double(n) / 1e3);
    else std::snprintf(buf, sizeof buf, "%zu", n);
    return buf;
}

// Two dashed outlines are how customize mode marks what can be rearranged.
void dashedRect(ImDrawList* dl, ImVec2 a, ImVec2 b, ImU32 col, float dash) {
    auto line = [&](ImVec2 p, ImVec2 q) {
        const float len = std::hypot(q.x - p.x, q.y - p.y);
        for (float t = 0.0f; t < len; t += dash * 2.0f) {
            const float u0 = t / len, u1 = std::min(len, t + dash) / len;
            dl->AddLine(ImVec2(p.x + (q.x - p.x) * u0, p.y + (q.y - p.y) * u0),
                        ImVec2(p.x + (q.x - p.x) * u1, p.y + (q.y - p.y) * u1), col);
        }
    };
    line(a, ImVec2(b.x, a.y));
    line(ImVec2(b.x, a.y), b);
    line(b, ImVec2(a.x, b.y));
    line(ImVec2(a.x, b.y), a);
}

// A shaded clay ball with a mark that hints at what the brush does: the placeholder until a rendered
// thumbnail is ready (ThumbnailCache).
void drawBrushIcon(ImDrawList* dl, ImVec2 c, float r, int type) {
    const ImVec4 clay(0.62f, 0.57f, 0.52f, 1.0f); // the Soft Clay look, while the rendered thumbnail is pending
    constexpr int kLayers = 14;
    for (int k = 0; k < kLayers; ++k) {
        const float t = float(k) / float(kLayers - 1);
        const float rr = r * (1.0f - t * 0.82f);
        const ImVec2 o(c.x - r * 0.32f * t, c.y - r * 0.34f * t);
        const float shade = 0.32f + 0.80f * std::pow(t, 0.8f);
        dl->AddCircleFilled(o, rr, theme::u32(ImVec4(clay.x * shade, clay.y * shade, clay.z * shade, 1.0f)), 32);
    }
    if (type < 0) return;
    const ImU32 hi = IM_COL32(245, 240, 232, 150), lo = IM_COL32(40, 36, 32, 150);
    auto curve = [&](float dy, ImU32 col, float thick) {
        ImVec2 pts[12];
        for (int i = 0; i < 12; ++i) {
            const float u = float(i) / 11.0f;
            pts[i] = ImVec2(c.x - r * 0.62f + r * 1.24f * u, c.y + dy + std::sin(u * 6.28f) * r * 0.16f);
        }
        dl->AddPolyline(pts, 12, col, 0, thick);
    };
    const float w = std::max(1.5f, r * 0.11f);
    switch (BrushType(type)) {
    case BrushType::Crease:
    case BrushType::DamStandard: // a groove: dark line with a lit lower lip
        curve(0.0f, lo, w);
        curve(w * 0.9f, hi, w * 0.5f);
        break;
    case BrushType::Smooth: dl->AddCircleFilled(ImVec2(c.x - r * 0.2f, c.y - r * 0.2f), r * 0.35f, IM_COL32(255, 240, 230, 40), 24); break;
    case BrushType::Move:
    case BrushType::SnakeHook:
    case BrushType::Nudge: {
        const ImVec2 a(c.x - r * 0.45f, c.y + r * 0.35f), b(c.x + r * 0.45f, c.y - r * 0.35f);
        dl->AddLine(a, b, hi, w);
        dl->AddTriangleFilled(b, ImVec2(b.x - r * 0.32f, b.y + r * 0.02f), ImVec2(b.x - r * 0.05f, b.y + r * 0.3f), hi);
        break;
    }
    case BrushType::Inflate: dl->AddCircle(c, r * 0.45f, hi, 24, w); break;
    case BrushType::KelvinGrab: { // a soft bulge with a drag arrow
        dl->AddCircle(c, r * 0.5f, hi, 32, w * 0.6f);
        const ImVec2 a(c.x - r * 0.1f, c.y + r * 0.1f), b(c.x + r * 0.5f, c.y - r * 0.5f);
        dl->AddLine(a, b, hi, w);
        dl->AddTriangleFilled(b, ImVec2(b.x - r * 0.3f, b.y + r * 0.02f), ImVec2(b.x - r * 0.02f, b.y + r * 0.3f), hi);
        break;
    }
    case BrushType::KelvinTwist: { // a swirl
        ImVec2 pts[24];
        for (int i = 0; i < 24; ++i) {
            const float t = float(i) / 23.0f, ang = t * 5.0f, rr = r * (0.1f + 0.45f * t);
            pts[i] = ImVec2(c.x + std::cos(ang) * rr, c.y - std::sin(ang) * rr);
        }
        dl->AddPolyline(pts, 24, hi, 0, w);
        break;
    }
    case BrushType::KelvinScale: // four arrows pointing in
        for (int k = 0; k < 4; ++k) {
            const float ang = 0.785f + 1.5708f * float(k);
            const ImVec2 o(c.x + std::cos(ang) * r * 0.6f, c.y + std::sin(ang) * r * 0.6f);
            const ImVec2 i(c.x + std::cos(ang) * r * 0.2f, c.y + std::sin(ang) * r * 0.2f);
            dl->AddLine(o, i, hi, w * 0.8f);
        }
        break;
    case BrushType::KelvinSnake: { // a tapering hook
        ImVec2 pts[12];
        for (int i = 0; i < 12; ++i) {
            const float t = float(i) / 11.0f;
            pts[i] = ImVec2(c.x - r * 0.4f + r * 0.8f * t, c.y + r * 0.35f - r * 0.75f * t * t);
        }
        for (int i = 0; i + 1 < 12; ++i) dl->AddLine(pts[i], pts[i + 1], hi, w * (1.4f - float(i) / 11.0f));
        break;
    }
    case BrushType::DetailClone: // a row of stamps
        for (int k = -1; k <= 1; ++k)
            dl->AddCircleFilled(ImVec2(c.x + r * 0.42f * float(k), c.y + r * 0.12f * float(k)), r * 0.16f, hi, 16);
        break;
    case BrushType::Pinch:
        dl->AddLine(ImVec2(c.x - r * 0.5f, c.y - r * 0.4f), ImVec2(c.x, c.y), hi, w);
        dl->AddLine(ImVec2(c.x + r * 0.5f, c.y - r * 0.4f), ImVec2(c.x, c.y), hi, w);
        break;
    case BrushType::Flatten:
    case BrushType::Trim:
        dl->AddLine(ImVec2(c.x - r * 0.55f, c.y - r * 0.25f), ImVec2(c.x + r * 0.55f, c.y - r * 0.25f), hi, w);
        dl->AddLine(ImVec2(c.x - r * 0.55f, c.y - r * 0.25f + w), ImVec2(c.x + r * 0.55f, c.y - r * 0.25f + w), lo, w * 0.6f);
        break;
    default: // raised strokes: lit crest with a shadow under it
        curve(w * 0.9f, lo, w * 0.8f);
        curve(0.0f, hi, w);
        break;
    }
}

// Glyph centred in a box, in `font` (or the current one).
// (A lone icon is centred by its ink: ui::CentredGlyph.)
void centredText(ImDrawList* dl, ImFont* font, ImVec2 a, ImVec2 b, ImU32 col, const char* text) {
    ui::CentredGlyph(dl, font ? font : ImGui::GetFont(), a, b, col, text);
}

// Text clipped to `width` with an ellipsis.
std::string elide(ImFont* font, const std::string& text, float width) {
    if (font->CalcTextSizeA(font->FontSize, FLT_MAX, 0.0f, text.c_str()).x <= width) return text;
    std::string s = text;
    while (!s.empty() && font->CalcTextSizeA(font->FontSize, FLT_MAX, 0.0f, (s + "...").c_str()).x > width) s.pop_back();
    return s + "...";
}

constexpr float kPointsReadoutW = 190.0f; // the ActiveTris / TotalTris readout at the right of the top shelf, unscaled

bool segmentPair(const std::string& a, const std::string& b) {
    auto group = [](const std::string& id) {
        if (id == "draw.sculpt" || id == "draw.paint") return 1;
        if (id == "tool.draw" || id == "tool.move" || id == "tool.scale" || id == "tool.rotate") return 5;
        if (id == "draw.zadd" || id == "draw.zsub") return 2;
        if (id.rfind("transform.symmetry_", 0) == 0) return 3;
        if (id.rfind("draw.pressure_", 0) == 0) return 4;
        return 0;
    };
    return group(a) != 0 && group(a) == group(b);
}

} // namespace

int brushTypeOfAction(std::string_view id) {
    for (int i = 0; i < int(BrushType::Count); ++i)
        if (id == brushActionId(BrushType(i))) return i;
    return -1;
}

// ---------------------------------------------------------------------------
// Button looks (ui::ActionButton)
// ---------------------------------------------------------------------------

bool drawShelfTile(App& app, const Action& a, ImVec2 size) {
    const bool on = a.checked && a.checked();
    const std::string label = trAction(a.id, a.label); // the interface language (I18n.h)
    const std::string id = label + "##" + a.id;
    const float s = app.uiScale_;
    const int type = brushTypeOfAction(a.id);
    const int look = type >= 0 || a.id == "draw.paint" ? 3 : 2; // picture tile, or icon over caption
    // Icon over a caption (right shelf) and thumbnail tiles (left and bottom shelves).
    if (size.x <= 0.0f) size.x = 44.0f * s;
    if (size.y <= 0.0f) size.y = 40.0f * s;
    const bool clicked = ImGui::InvisibleButton(id.c_str(), size);
    const bool hovered = ImGui::IsItemHovered(), held = ImGui::IsItemActive();
    const bool disabled = (ImGui::GetItemFlags() & ImGuiItemFlags_Disabled) != 0;
    const ImVec2 p0 = ImGui::GetItemRectMin(), p1 = ImGui::GetItemRectMax();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float alpha = disabled ? 0.4f : 1.0f;
    ImFont* caption = look == int(ui::Look::IconLabel) ? app.fontTiny_ : app.fontSmall_;
    const float capH = caption ? caption->FontSize + 2.0f * s : 14.0f * s;
    if (look == int(ui::Look::IconLabel)) {
        // Right-shelf toggles are options (Mask, Paint, PolyF): on is a raised grey with an accent bar down the
        // side that faces the canvas, off is quiet; only a mode action gets the accent fill.
        const bool mode = ui::IsModeAction(a.id);
        // Off: the fill eases into the accent-tinted hover (ui::DrawHover adds the look's ring).
        const float h = disabled ? 0.0f : ui::HoverAmount(ImGui::GetItemID(), hovered || ui::HoveredOrFocused(ImGui::GetItemID()));
        const ImVec4 rest = theme::frame, hot = theme::buttonHover;
        const ImVec4 eased(rest.x + (hot.x - rest.x) * h, rest.y + (hot.y - rest.y) * h, rest.z + (hot.z - rest.z) * h, 1.0f);
        const ImVec4 bg = on ? (mode ? (hovered ? theme::accentHover : theme::accent) : hovered ? theme::toggleOnHover : theme::toggleOn)
                             : held ? theme::buttonPressed : eased;
        dl->AddRectFilled(p0, p1, theme::u32(bg, alpha), theme::rounding(4.0f * s));
        if (on && !mode) ui::DrawOnMark(dl, p0, p1, true);
        // Two text levels only: actions and hovered items in text, options that are off in textOff (readable, so
        // off doesn't look disabled); disabled items are faded by alpha.
        const ImU32 fg = theme::u32(on ? (mode ? theme::textOnAccent : theme::textBright) : hovered || !a.checked ? theme::text : theme::textOff, alpha);
        centredText(dl, app.fontIcons_, p0, ImVec2(p1.x, p1.y - capH + 2.0f * s), fg, a.icon ? a.icon : "?");
        centredText(dl, caption, ImVec2(p0.x, p1.y - capH), ImVec2(p1.x, p1.y - 1.0f * s), fg,
                    elide(caption ? caption : ImGui::GetFont(), label, size.x - 2.0f * s).c_str());
        return clicked;
    }
    // Tile: a square picture (brush thumbnail or icon) with the name under it.
    const float pic = std::min(size.x, size.y - capH);
    const ImVec2 q0(p0.x + (size.x - pic) * 0.5f, p0.y), q1(q0.x + pic, q0.y + pic);
    ui::TileLiftScope lift; // the tile lifts on hover; the layout stays
    lift.lift = ui::BeginTileLift(p0, p1);
    dl->AddRectFilled(p0, p1, theme::u32(hovered ? theme::buttonHover : theme::tile, hovered ? 0.55f : 0.0f), theme::rounding(4.0f * s));
    ImTextureID tex = type >= 0 ? app.thumbnails().brush(type) : ImTextureID(0);
    if (a.id == "draw.paint") { // the Paint brush's own preview (Pickers.cpp)
        BrushItem paint;
        paint.kind = BrushItem::Kind::Paint;
        tex = PickerUi::thumbnail(app, paint);
    }
    if (tex) {
        dl->AddImageRounded(tex, ImVec2(q0.x + 2 * s, q0.y + 2 * s), ImVec2(q1.x - 2 * s, q1.y - 2 * s), ImVec2(0, 0),
                            ImVec2(1, 1), IM_COL32(255, 255, 255, int(255 * alpha)), theme::rounding(4.0f * s));
    } else if (type >= 0) {
        drawBrushIcon(dl, ImVec2((q0.x + q1.x) * 0.5f, (q0.y + q1.y) * 0.5f), pic * 0.36f, type);
    } else {
        dl->AddCircleFilled(ImVec2((q0.x + q1.x) * 0.5f, (q0.y + q1.y) * 0.5f), pic * 0.36f, theme::u32(theme::tile), 32);
        centredText(dl, app.fontIcons_, q0, q1, theme::u32(on ? theme::accent : theme::text, alpha), a.icon ? a.icon : "?");
    }
    if (on) dl->AddRect(p0, p1, theme::u32(theme::accent), theme::rounding(4.0f * s), 0, 2.0f * s);
    // The hotkey digit (1-9, 0) in the corner, as ZBrush shows brush shortcuts.
    const std::string key = app.actions().shortcutText(a.id);
    if (key.size() == 1 && caption)
        dl->AddText(caption, caption->FontSize, ImVec2(p0.x + 3 * s, p0.y + 1 * s), theme::u32(theme::textDim), key.c_str());
    centredText(dl, caption, ImVec2(p0.x, p1.y - capH), p1, theme::u32(on ? theme::accent : theme::text, alpha),
                elide(caption ? caption : ImGui::GetFont(), label, size.x).c_str());
    return clicked;
}

// ---------------------------------------------------------------------------
// Actions
// ---------------------------------------------------------------------------

void App::registerShellActions() {
    auto add = [this](Action a) { actions_.add(std::move(a)); };
    auto flag = [this](std::string id, std::string label, const char* icon, std::string palette, std::string shortcut,
                       bool* value, std::string description) {
        Action a;
        a.id = std::move(id);
        a.label = std::move(label);
        a.icon = icon;
        a.palette = std::move(palette);
        a.shortcut = std::move(shortcut);
        a.description = std::move(description);
        a.run = [value] { *value = !*value; };
        a.checked = [value] { return *value; };
        actions_.add(std::move(a));
    };

    // Brushes: 1-9 and 0 pick the first ten, as before.
    static const char* const kBrushHelp[] = {
        "Raises the surface along the stroke; Alt digs in.",
        "Builds up clay that fills the hollows first.",
        "Relaxes the surface. Hold Shift to smooth with any brush.",
        "Grabs the surface under the cursor and pulls it along.",
        "Swells the surface outward along its normals.",
        "Cuts a sharp crease; Alt raises a ridge.",
        "Presses the surface toward a plane.",
        "Pulls the surface toward the stroke to tighten edges.",
        "A sharp V-shaped crease with the sides pinched in (ZBrush DamStandard).",
        "Square, flat-topped clay strokes for blocking in forms.",
        "Pulls the surface out into horns and tendrils.",
        "Trim: shaves away everything above a plane.",
        "Raises the surface to one fixed height per stroke; overlaps never stack.",
        "Slides the surface sideways along the stroke.",
        "Vector displacement: one Drag Stamp stamps a whole shape with overhangs (horn, ear curl, scales, nostril, or "
        "your own from a VDM plane). Pick the map under Brush > Vector Displacement.",
        "Elastic move (Kelvinlets): drags the surface like real clay, with no falloff edge; Squash makes it bulge.",
        "Elastic twist: drag right to turn the surface clockwise around where you pressed, left to turn it back.",
        "Elastic pinch / swell: drag left to pinch the surface toward where you pressed, right to swell it.",
        "Elastic Tendril: pulls smooth, tapering horns and tendrils out along the stroke.",
        "Stamps the VDM brush's map along the stroke, each copy turned to follow it. Capture one with Capture from view.",
        "Pulls the surface back toward the morph target, a selection set's shape (Tool > Selection Sets > Morph "
        "Target); Alt pushes away. Works at any subdivision level.",
    };
    static_assert(std::size(kBrushHelp) == size_t(BrushType::Count));
    for (int i = 0; i < int(BrushType::Count); ++i) {
        const BrushType type = BrushType(i);
        Action a;
        a.id = brushActionId(type);
        a.label = brushLabel(type);
        a.icon = ICON_LC_BRUSH;
        a.palette = "Brush";
        a.shortcut = i < 9 ? std::to_string(i + 1) : i == 9 ? "0" : "";
        a.description = kBrushHelp[i];
        a.run = [this, type] {
            brush_ = type;
            doc_.activeBrush = -1;
            paintMode_ = false;
            guidesMode_ = false; // leaves the QRemesh Guides brush
            maskBrush_.on = false;
        };
        a.checked = [this, type] { return brush_ == type && doc_.activeBrush < 0 && !paintMode_ && !guidesMode_; };
        add(std::move(a));
    }
    add({"draw.sculpt", "Sculpt", nullptr,
         "Brushes change the shape (ZBrush Zadd/Zsub). Turn on Rgb to paint colour at the same time.", "Draw", "", "",
         [this] { paintMode_ = false; }, [this] { return !paintMode_; }, nullptr});
    add({"draw.paint", "Paint", ICON_LC_PAINTBRUSH,
         "Paint colour without sculpting (ZBrush Rgb on, Zadd off). Alt paints back to white, Shift still smooths.",
         "Draw", "P", "", [this] { paintMode_ = !paintMode_; }, [this] { return paintMode_; }, nullptr});
    add({"draw.zadd", "Add", nullptr, "Brushes add to the surface. Hold Alt to subtract for one stroke.", "Draw", "", "",
         [this] { invertToggle_ = false; }, [this] { return !invertToggle_; }, nullptr});
    add({"draw.zsub", "Sub", nullptr, "Brushes subtract from the surface. Hold Alt to add for one stroke.", "Draw", "", "",
         [this] { invertToggle_ = true; }, [this] { return invertToggle_; }, nullptr});
    flag("draw.rgb", "Rgb", nullptr, "Draw", "", &paintRgb_, "Sculpt brushes also paint the colour (ZBrush Rgb with Zadd).");
    flag("draw.front_faces", "Front", nullptr, "Brush", "", &frontFacesOnly_,
         "Front faces only: brushes leave the far side of thin parts alone.");
    flag("draw.connected_only", "Connected only", nullptr, "Brush", "", &uiPrefs_.connectedOnly,
         "Brushes only reach the surface joined to the point under the cursor, within the brush size: the other leg, "
         "the next finger or a lip across a gap stay as they are (Blender's Connected Only). Saved with the "
         "preferences and the project.");
    flag("draw.view_falloff", "View falloff", nullptr, "Brush", "", &uiPrefs_.viewFalloff,
         "The brush falloff is measured on screen: everything inside the brush circle is reached, through the model "
         "and out the back, as a cylinder along the view (Blender's Projected from View). Turn on Connected only too "
         "to keep it to one piece.");
    add({"draw.dynamic", "Dynamic", nullptr,
         "Dynamic topology (ZBrush's Sculptris Pro): brushes add triangles where they need detail and remove them "
         "where they don't, so you can pull out horns or fingers without stretching. Detail sets how small the "
         "triangles get; zoom in or shrink the brush for finer ones. Needs a mesh without subdivision levels; quads "
         "under the brush become triangles and UVs are dropped (Undo brings them back).",
         "Draw", "", "dyntopo", [this] { setDynamic(!doc_.sculptor.dyntopo.enabled); },
         [this] { return doc_.sculptor.dyntopo.enabled; }, nullptr});
    // Masking and visibility selection shapes (Tool > Masking, Tool > Visibility), named as in ZBrush.
    auto shape = [this](std::string id, std::string label, std::string palette, SelectShape* target, SelectShape value,
                        std::string description) {
        actions_.add({std::move(id), std::move(label), nullptr, std::move(description), std::move(palette), "", "",
                      [target, value] { *target = value; }, [target, value] { return *target == value; }, nullptr});
    };
    shape("mask.pen", "Mask Pen", "Tool > Masking", &maskShape_, SelectShape::Pen,
          "Ctrl+drag paints the mask with the brush; Ctrl+Alt+drag erases it. Ctrl+drag on the background clears it.");
    shape("mask.lasso", "Mask Lasso", "Tool > Masking", &maskShape_, SelectShape::Lasso,
          "Ctrl+drag draws a lasso and masks everything inside it; Ctrl+Alt+drag unmasks. The far side is included.");
    shape("mask.rect", "Mask Rectangle", "Tool > Masking", &maskShape_, SelectShape::Rect,
          "Ctrl+drag draws a rectangle and masks everything inside it; Ctrl+Alt+drag unmasks. The far side is included.");
    shape("select.rect", "Select Rectangle", "Tool > Visibility", &hideShape_, SelectShape::Rect,
          "Ctrl+Shift+drag a rectangle to keep what's inside and hide the rest; add Alt to hide the inside.");
    shape("select.lasso", "Select Lasso", "Tool > Visibility", &hideShape_, SelectShape::Lasso,
          "Ctrl+Shift+drag a lasso to keep what's inside and hide the rest; add Alt to hide the inside.");
    // Cut (PanelCut.cpp, core/Cut.h): the same Ctrl+Shift drag, cutting instead of hiding.
    shape("cut.line", "Cut Line", "Tool > Cut", &hideShape_, SelectShape::CutLine,
          "Ctrl+Shift+drag a line: the shaded side is cut (Alt: the other side). Away removes it through the whole "
          "Part and closes the cut flat; In steps it down by Depth; Add raises it.");
    shape("cut.lasso", "Cut Lasso", "Tool > Cut", &hideShape_, SelectShape::CutLasso,
          "Ctrl+Shift+drag a lasso: Away cuts a hole through the Part, In a pocket Depth deep, Add raises the shape.");
    shape("cut.rect", "Cut Rectangle", "Tool > Cut", &hideShape_, SelectShape::CutRect,
          "Ctrl+Shift+drag a rectangle: Away cuts a hole through the Part, In a pocket Depth deep, Add a block.");
    shape("cut.polyline", "Cut Polyline", "Tool > Cut", &hideShape_, SelectShape::CutPolyline,
          "Ctrl+Shift+click points: click the first point to cut the polygon, double-click or Enter to cut along an "
          "open bent line (Alt: the other side), Esc to drop it.");
    auto cutMode = [this](std::string id, std::string label, int mode, std::string description) {
        actions_.add({std::move(id), std::move(label), nullptr, std::move(description), "Tool > Cut", "", "",
                      [this, mode] { cutMode_ = mode; }, [this, mode] { return cutMode_ == mode; }, nullptr});
    };
    cutMode("cut.away", "Away", 0, "Remove what the shape covers, through the whole Part, and close the cut with a flat cap.");
    cutMode("cut.in", "In", 1, "Cut a flat-bottomed pocket Depth deep where the shape covers the Part.");
    cutMode("cut.add", "Add", 2, "Add the shape, extruded Depth out of the surface, joined to the Part.");
    flag("draw.pressure_strength", "Pressure > Strength", nullptr, "Draw > Tablet", "", &pressureStrength_,
         "Pen pressure scales the brush strength.");
    flag("draw.pressure_size", "Pressure > Size", nullptr, "Draw > Tablet", "", &pressureSize_,
         "Pen pressure scales the brush size.");
    flag("transform.symmetry_x", "X", nullptr, "Transform", "X", &symmetry_.x, "Mirror strokes across X (left and right).");
    flag("transform.symmetry_y", "Y", nullptr, "Transform", "", &symmetry_.y, "Mirror strokes across Y (top and bottom).");
    flag("transform.symmetry_z", "Z", nullptr, "Transform", "", &symmetry_.z, "Mirror strokes across Z (front and back).");
    flag("transform.symmetry_radial", "R", nullptr, "Transform", "", &symmetry_.radialEnabled,
         "Radial symmetry: repeat strokes around an axis (Radial Count copies, Transform palette).");
    flag("transform.symmetry_local", "Local", nullptr, "Transform", "", &symmetry_.local,
         "Local symmetry: mirror and turn strokes about the active Part's centre instead of the world origin.");
    for (int axis = 0; axis < 3; ++axis) { // the axis radial copies turn around
        Action a;
        a.id = std::string("transform.radial_axis_") + char('x' + axis);
        a.label = std::string(1, char('X' + axis)) + " axis";
        a.palette = "Transform";
        a.description = std::string("Radial symmetry turns its copies around the ") + char('X' + axis) +
                        (axis == 1 ? " axis (up, the default)." : " axis.");
        a.run = [this, axis] { symmetry_.radialAxis = axis; };
        a.checked = [this, axis] { return symmetry_.radialAxis == axis; };
        actions_.add(std::move(a));
    }
    registerAiActions(); // AI Brush, Generate 3D: only while AI is on (PanelAi.cpp)
    flag("ui.brush_gallery", "Community brushes", ICON_LC_GLOBE, "Brush", "", &showBrushGallery_,
         "Browse, search and install brushes other sculptors shared.");

    // View (right shelf, Render palette). Frame keeps F; its short label fits the right shelf.
    add({"view.frame", "Frame", ICON_LC_SCAN, "Fit the whole model in the view.", "Transform", "F", "",
         [this] { camera_.frame(sceneBounds()); }, nullptr, nullptr});
        flag("view.polyf", "Wire", ICON_LC_GRID_3X3, "Render", "", &renderSettings_.wireframe,
         "Wireframe: show the polygons of the active Part (Shift+F).");
    flag("view.polygroups", "Groups", ICON_LC_SHAPES, "Render", "", &renderSettings_.showPolygroups,
         "Colour each face group of the active Part.");
    flag("view.mask", "Mask", ICON_LC_DROPLET, "Render", "", &renderSettings_.showMask,
         "Show or hide the mask (masked areas drawn darker). Hiding it keeps the mask: masked areas stay protected "
         "from brushes until you clear it.");
    if (const Action* shown = actions_.find("view.mask")) { // say so, since a hidden mask still protects (#43)
        Action a = *shown;
        a.run = [this] {
            renderSettings_.showMask = !renderSettings_.showMask;
            status_ = renderSettings_.showMask ? "Mask shown" : "Mask hidden (still masking; Clear Mask removes it)";
        };
        actions_.add(std::move(a));
    }
    flag("view.polypaint", "Paint", ICON_LC_PALETTE, "Render", "", &renderSettings_.showPolypaint,
         "Show vertex paint colours (off: plain material).");
    // Shading (Material palette, Tool > Display): the scene's, and the active SubTool's own.
    // Viewport shading (Material > Viewport Shading): Lit Sphere or PBR.
    add({"view.shading_lit_sphere", "Lit Sphere", nullptr,
         "Lit Sphere shading (the default): the chosen material sphere or the studio lights, made for reading forms "
         "while sculpting.",
         "Material > Viewport Shading", "", "viewport_shading",
         [this] { runCommand("viewport_shading", {{"mode", std::string("lit_sphere")}}); },
         [this] { return !renderSettings_.pbr(); }, nullptr});
    add({"view.shading_pbr", "PBR", nullptr,
         "PBR shading: the texture's material channels (base colour, roughness, metallic, normal, height, AO, emissive) "
         "lit by an environment, as a game engine or renderer shows them. Meshes without a material use the roughness "
         "and metallic set here.",
         "Material > Viewport Shading", "", "viewport_shading",
         [this] { runCommand("viewport_shading", {{"mode", std::string("pbr")}}); },
         [this] { return renderSettings_.pbr(); }, nullptr});
    add({"material.smooth", "Smooth", nullptr,
         "Smooth shading (the default): normals blend across polygons, so the surface reads as one form.", "Material", "",
         "", [this] { runCommand("set_shading", {{"mode", std::string("smooth")}}); },
         [this] { return !renderSettings_.flatShading; }, nullptr});
    add({"material.flat", "Flat", nullptr,
         "Flat shading: every polygon shows its own face, so the mesh's structure and each stroke's effect on it read "
         "clearly (as in ZBrush). Parts set to Smooth or Flat keep their own. A preference: Store Config keeps it.",
         "Material", "", "", [this] { runCommand("set_shading", {{"mode", std::string("flat")}}); },
         [this] { return renderSettings_.flatShading; }, nullptr});
    // The view's background (#21; Preferences > Display, Render palette).
    add({"view.background_gradient", "Gradient", nullptr,
         "The view's background is the charcoal radial gradient (the default).", "Preferences > Display", "",
         "set_background", [this] { runCommand("set_background", {{"mode", std::string("gradient")}}); },
         [this] { return renderSettings_.gradientBackground; }, nullptr});
    add({"view.background_flat", "Flat Colour", nullptr,
         "The view's background is one solid colour (pick it below), without the gradient. A preference: Store "
         "Config keeps it; projects keep their own.",
         "Preferences > Display", "", "set_background",
         [this] { runCommand("set_background", {{"mode", std::string("flat")}}); },
         [this] { return !renderSettings_.gradientBackground; }, nullptr});
    auto subtoolShading = [this, &add](const char* id, const char* label, Shading mode, const char* description) {
        add({id, label, nullptr, description, "Material > This Part", "", "subtool_shading",
             [this, mode] { runCommand("subtool_shading", {{"mode", std::string(shadingName(mode))}}); },
             [this, mode] { return doc_.activeSubtoolInfo().shading == mode; }, nullptr});
    };
    subtoolShading("subtool.shading_default", "Scene", Shading::Default,
                   "The active Part follows the scene's shading (Smooth | Flat above).");
    subtoolShading("subtool.shading_smooth", "Smooth", Shading::Smooth,
                   "The active Part always shades smooth, whatever the scene's shading. Saved in the project.");
    subtoolShading("subtool.shading_flat", "Flat", Shading::Flat,
                   "The active Part always shades flat (faceted), whatever the scene's shading. Saved in the project.");
    flag("view.mesh", "Show Mesh", ICON_LC_BOX, "Render", "", &renderSettings_.showMesh, "Draw the sculpt.");
    add({"view.fullscreen", "Fullscreen", ICON_LC_MAXIMIZE, "Fill the screen with SculptForge (again: back to a window).",
         "Render", "F11", "",
         [this] { SDL_SetWindowFullscreen(window_, !(SDL_GetWindowFlags(window_) & SDL_WINDOW_FULLSCREEN)); },
         [this] { return (SDL_GetWindowFlags(window_) & SDL_WINDOW_FULLSCREEN) != 0; }, nullptr});
    add({"view.splats", "Splats", ICON_LC_SPARKLE, "Show the Gaussian splat capture.", "Render", "", "",
         [this] { renderSettings_.showSplats = !renderSettings_.showSplats; },
         [this] { return renderSettings_.showSplats; }, [this] { return !doc_.splats.empty(); }});
    add({"subtool.solo", "Solo", ICON_LC_SCAN_EYE, "Show only the active Part (again: show the others).",
         "Tool > Parts", "", "subtool_solo", [this] { runCommand("subtool_solo"); },
         [this] {
             int visible = 0;
             for (const auto& s : doc_.subtools) visible += s.visible ? 1 : 0;
             return doc_.subtoolCount() > 1 && visible == 1 && doc_.activeSubtoolInfo().visible;
         },
         [this] { return doc_.subtoolCount() > 1; }});
    add({"nav.move", "Move", ICON_LC_HAND, "Drag on this button to pan the view (or Alt+drag the background).",
         "Transform", "", "", nullptr, nullptr, nullptr});
    add({"nav.zoom", "Zoom", ICON_LC_ZOOM_IN, "Drag up and down on this button to zoom (or use the mouse wheel).",
         "Transform", "", "", nullptr, nullptr, nullptr});
    add({"nav.rotate", "Rotate", ICON_LC_ROTATE_3D, "Drag on this button to orbit the view (or drag the background).",
         "Transform", "", "", nullptr, nullptr, nullptr});

    // Files and editing (File / Edit palettes).
    add({"file.import", "Import", ICON_LC_IMPORT, "Add a model file as a new Part.", "Tool", "", "",
         [this] {
             static const SDL_DialogFileFilter filters[] = {
                 {"Models", "obj;fbx;glb;gltf;ply;stl;GoZ;goz;usda;usd;usdz"}, {"All files", "*"}};
             SDL_ShowOpenFileDialog(
                 [](void* user, const char* const* files, int) {
                     if (files && files[0]) static_cast<App*>(user)->queueFile(FileAction::AppendSubtool, files[0]);
                 },
                 this, window_, filters, 2, nullptr, false);
         },
         nullptr, nullptr});
    add({"file.export_obj", "Export OBJ...", nullptr, "Save the visible Parts as Wavefront OBJ.", "File", "", "",
         [this] { saveFileDialog(FileAction::SaveObj); }, nullptr, nullptr});
    add({"file.export_lods", "Export LOD Chain...", nullptr, "One GLB per level of detail.", "File", "", "",
         [this] { saveFileDialog(FileAction::ExportLods); }, nullptr, nullptr});
    add({"file.export_usd", "Export USD / USDZ...", nullptr, "OpenUSD for Apple, Omniverse and film pipelines.", "File",
         "", "", [this] { saveFileDialog(FileAction::ExportUsd); }, nullptr, nullptr});
    add({"file.export_fbx", "Export FBX for Unreal...", nullptr, "FBX sized and named for Unreal Engine, with LODs.",
         "File", "", "", [this] { saveFileDialog(FileAction::ExportUnreal); }, nullptr, nullptr});
    add({"file.save_splats", "Save Splats...", nullptr, "Save the Gaussian splats (PLY, glTF or USD).", "File", "", "",
         [this] { saveFileDialog(FileAction::SaveSplats); }, nullptr, [this] { return !doc_.splats.empty(); }});
    add({"file.quit", "Quit", ICON_LC_LOG_OUT, "Close SculptForge (asks to save changes first).", "File", "Ctrl+Q", "",
         [this] { requestQuit(); }, nullptr, nullptr});
    add({"geometry.remesh", "Voxel Remesh", nullptr,
         "Rebuild as an even, watertight surface (like Voxel Remesh): merges parts, fixes scans and generated models.",
         "Tool > Remesh", "", "remesh",
         [this] { startVoxelRemesh(); }, nullptr, [this] { return !voxelRemesh_; }});
    add({"geometry.to_splats", "Sculpt to Splats", nullptr, "Turn the sculpt into Gaussian splats.", "Splats", "",
         "mesh_to_splats", [this] { runCommand("mesh_to_splats"); }, nullptr, nullptr});
    add({"color.fill", "Fill", ICON_LC_PAINT_BUCKET, "Fill the unmasked area with the colour (FillObject).", "Color", "",
         "paint_fill", [this] { runCommand("paint_fill", {{"color", colorToHex(doc_.paintColor)}}); }, nullptr, nullptr});

    // The shell itself.
    add({"ui.toggle_shelves", "Shelves", ICON_LC_MAXIMIZE, "Hide or show the shelves and trays around the canvas.", "Preferences",
         "Tab", "",
         [this] {
             layout_.shelvesVisible = !layout_.shelvesVisible;
             updateCameraAspect();
         },
         [this] { return layout_.shelvesVisible; }, nullptr});
    add({"ui.menus", "Menus", ICON_LC_MENU, "Show or hide the palette bar under the title.", "Preferences", "", "",
         [this] {
             layout_.paletteBarVisible = !layout_.paletteBarVisible;
             noteLayoutChanged();
         },
         [this] { return layout_.paletteBarVisible; }, nullptr});
    add({"ui.tray.left", "Left tray", ICON_LC_PANEL_LEFT, "Open or close the left tray (or click its divider).",
         "Preferences", "", "",
         [this] {
             layout_.leftTrayOpen = !layout_.leftTrayOpen;
             noteLayoutChanged();
         },
         [this] { return layout_.leftTrayOpen; }, nullptr});
    add({"ui.tray.right", "Right tray", ICON_LC_PANEL_RIGHT, "Open or close the right tray (or click its divider).",
         "Preferences", "", "",
         [this] {
             layout_.rightTrayOpen = !layout_.rightTrayOpen;
             noteLayoutChanged();
         },
         [this] { return layout_.rightTrayOpen; }, nullptr});
    static const char* const kPresetHelp[] = {
        "Standard layout: Tool palette in the right tray, shelves around the canvas.",
        "Sculpt layout: both trays closed for the largest canvas.",
        "Wide layout: Brush, Stroke and Alpha in the left tray, Tool and Material in the right one.",
    };
    for (int i = 0; i < 3; ++i)
        add({"ui.preset." + std::to_string(i + 1), std::to_string(i + 1), nullptr, kPresetHelp[i], "Preferences > Layout", "",
             "", [this, i] { applyPreset(i); }, nullptr, nullptr});
    // Preferences > Interface > Look: the looks and the two layouts, also from F3 and scripts (ui_look).
    for (int i = 0; i < int(looks::LookId::Count); ++i) {
        const looks::LookId look = looks::LookId(i);
        add({std::string("ui.look.") + looks::lookKey(look), std::string("Look: ") + looks::lookName(look), ICON_LC_PALETTE,
             std::string("Switch the interface to the ") + looks::lookName(look) + " look (Preferences > Interface > Look).",
             "Preferences > Interface > Look", "", "",
             [this, i] {
                 uiPrefs_.look.look = i;
                 if (i != 0) setShellLayout(1); // the looks come with the SculptForge layout (Preferences can switch back)
                 lookDirty_ = true;
             },
             [this, i] { return uiPrefs_.look.look == i; }, nullptr});
    }
    add({"ui.layout.sculptforge", "SculptForge layout", ICON_LC_LAYOUT_DASHBOARD,
         "Menus by task (File, Edit, Sculpt, Paint, Mesh, View, Render, Help), a status bar and a one-row tool shelf.",
         "Preferences > Interface > Look", "", "", [this] { setShellLayout(1); lookDirty_ = true; },
         [this] { return uiPrefs_.shellLayout == 1; }, nullptr});
    add({"ui.layout.zbrush", "ZBrush-style layout", ICON_LC_LAYOUT_TEMPLATE,
         "The alphabetical palette bar and the two-row top shelf, as ZBrush users expect.", "Preferences > Interface > Look", "",
         "", [this] { setShellLayout(0); lookDirty_ = true; }, [this] { return uiPrefs_.shellLayout == 0; }, nullptr});
    commands_.add({"ui_look",
                   "Interface look and layout (Preferences > Interface > Look). Only changes how the interface looks.",
                   {{"look", ParamSpec::Type::String, "classic, forge, phosphor or blueprint (default: unchanged).", false},
                    {"scheme", ParamSpec::Type::String,
                     "Forge Terminal's colours: neutral, tokyo_night, catppuccin_mocha, gruvbox_dark, nord, rose_pine, kanagawa or everforest.",
                     false},
                    {"match_omarchy", ParamSpec::Type::Bool, "Forge Terminal: match the desktop theme (the current Omarchy theme's colours, Linux).", false},
                    {"phosphor", ParamSpec::Type::String, "Phosphor's colour: green or amber.", false},
                    {"effects", ParamSpec::Type::Bool, "Phosphor's scanlines and glow (on the interface only).", false},
                    {"outlines", ParamSpec::Type::String, "The tiled looks' panel borders: off, subtle (the default) or on.", false},
                    {"outline_opacity", ParamSpec::Type::Number, "How strong those borders are, 0-100 (default 100).", false},
                    {"layout", ParamSpec::Type::String, "sculptforge or zbrush (default: the look's, else unchanged).", false},
                    {"card", ParamSpec::Type::Bool, "Show the one-time \"SculptForge has a new look\" card (for tests).", false}},
                   [this](Document&, const Args& a) -> CommandResult {
                       theme::LookSettings& l = uiPrefs_.look;
                       int layoutKind = uiPrefs_.shellLayout;
                       if (a.contains("look")) {
                           const std::string key = argString(a, "look");
                           const looks::LookId id = looks::lookFromKey(key);
                           if (id == looks::LookId::Classic && key != "classic") return {false, "look must be classic, forge, phosphor or blueprint"};
                           l.look = int(id);
                           if (id != looks::LookId::Classic) layoutKind = 1;
                       }
                       if (a.contains("scheme")) {
                           const std::string key = argString(a, "scheme");
                           if (!looks::findScheme(key)) return {false, "Unknown scheme " + key};
                           l.scheme = key;
                       }
                       if (a.contains("match_omarchy")) l.matchOmarchy = argBool(a, "match_omarchy", false);
                       if (a.contains("phosphor")) l.amber = argString(a, "phosphor") == "amber";
                       if (a.contains("effects")) l.effects = argBool(a, "effects", false);
                       if (a.contains("outlines")) {
                           const std::string o = argString(a, "outlines");
                           if (o != "off" && o != "subtle" && o != "on") return {false, "outlines must be off, subtle or on"};
                           l.outlines = o == "off" ? 0 : o == "on" ? 2 : 1;
                       }
                       if (a.contains("outline_opacity"))
                           l.outlineOpacity = std::clamp(float(argNumber(a, "outline_opacity", 100.0)) / 100.0f, 0.0f, 1.0f);
                       if (a.contains("layout")) {
                           const std::string k = argString(a, "layout");
                           if (k != "sculptforge" && k != "zbrush") return {false, "layout must be sculptforge or zbrush"};
                           layoutKind = k == "sculptforge" ? 1 : 0;
                       }
                       setShellLayout(layoutKind);
                       lookDirty_ = true;
                       if (a.contains("card")) newLookCard_ = argBool(a, "card", false);
                       return {true, std::string("Look: ") + looks::lookName(looks::LookId(l.look)) +
                                         (layoutKind == 1 ? ", SculptForge layout" : ", ZBrush-style layout")};
                   }});

    int count = 0;
    const PaletteDef* list = palettes(count);
    for (int i = 0; i < count; ++i) {
        const std::string id = list[i].id;
        add({"ui.palette." + id, list[i].name, list[i].icon,
             std::string("Open the ") + list[i].name + " palette (click its tray icon to keep it in a tray).", "Palettes", "",
             "",
             [this, id] {
                 if (std::count(layout_.leftTray.begin(), layout_.leftTray.end(), id) +
                     std::count(layout_.rightTray.begin(), layout_.rightTray.end(), id)) {
                     flashPalette_ = id;
                     flashStart_ = ImGui::GetTime();
                     return;
                 }
                 int n = 0;
                 const PaletteDef* all = palettes(n);
                 float x = shell_.paletteBar.x + 8.0f * uiScale_;
                 for (int k = 0; k < n && all[k].id != id; ++k) // under its name in the bar
                     x += ImGui::CalcTextSize(all[k].name).x + 2.0f * 8.0f * uiScale_;
                 dropdown_ = id;
                 dropdownX_ = x;
                 openDropdown_ = true;
             },
             nullptr, nullptr});
    }
}

// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------

float App::topInset() const {
    // Laptop camera notches cover the top of a window that fills the screen (MacBooks since 2021).
    if (uiPrefs_.cameraNotch == 2 || hideUi_ || !options_.uiTest.empty() || !options_.recordPath.empty()) return 0.0f;
    // macOS (and any platform that knows) reports the safe area itself.
    SDL_Rect safe{};
    if (SDL_GetWindowSafeArea(window_, &safe) && safe.y > 0) return float(safe.y);
    // Linux on Apple laptops (Asahi): notched MacBooks are 16:10 plus the band beside the notch, so a display
    // taller than 16:10 has that band at its top. Only a window covering the whole display reaches it.
    static const bool appleLaptop = [] {
        std::ifstream f("/proc/device-tree/model", std::ios::binary);
        std::string model((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        return model.find("MacBook") != std::string::npos;
    }();
    SDL_Rect bounds{};
    int ww = 0, wh = 0;
    const SDL_DisplayID display = SDL_GetDisplayForWindow(window_);
    if (!display || !SDL_GetDisplayBounds(display, &bounds)) return 0.0f;
    SDL_GetWindowSize(window_, &ww, &wh);
    const float band = float(bounds.h) - float(bounds.w) * 10.0f / 16.0f;
    const bool coversDisplay = ww >= bounds.w && wh >= bounds.h;
    const bool notched = band > bounds.h * 0.02f && band < bounds.h * 0.06f;
    if (uiPrefs_.cameraNotch == 1) return notched ? band : 32.0f * uiScale_;
    return appleLaptop && notched && coversDisplay ? band : 0.0f;
}

ShellGeometry App::shellGeometry() const {
    int w = 0, h = 0;
    SDL_GetWindowSize(window_, &w, &h);
    return computeShell(layout_, float(w), float(h), uiScale_, topInset(), shellMetrics());
}

CanvasRect App::canvasRect() const {
    int w = 0, h = 0;
    SDL_GetWindowSize(window_, &w, &h);
    if (hideUi_) return {0.0f, 0.0f, float(std::max(w, 1)), float(std::max(h, 1))};
    CanvasRect c = computeShell(layout_, float(w), float(h), uiScale_, topInset(), shellMetrics()).canvas;
    // On whole framebuffer pixels, as canvasViewport draws it: with a fractional rect (HiDPI, UI scales) the view
    // was drawn up to half a pixel from where the rays for the brush were cast.
    int pw = 0, ph = 0;
    if (w > 0 && h > 0 && SDL_GetWindowSizeInPixels(window_, &pw, &ph) && pw > 0 && ph > 0) {
        const float sx = float(pw) / float(w), sy = float(ph) / float(h);
        const float x0 = std::round(c.x * sx) / sx, x1 = std::round((c.x + c.w) * sx) / sx;
        const float y0 = std::round(c.y * sy) / sy, y1 = std::round((c.y + c.h) * sy) / sy;
        c = {x0, y0, std::max(x1 - x0, 1.0f / sx), std::max(y1 - y0, 1.0f / sy)};
    }
    return c;
}

void App::canvasRay(float x, float y, glm::vec3& origin, glm::vec3& dir) const {
    const CanvasRect c = canvasRect();
    camera_.screenRay(x - c.x, y - c.y, c.w, c.h, origin, dir);
}

Renderer::Viewport App::canvasViewport(int fbWidth, int fbHeight) const {
    if (hideUi_) return {};
    int w = 0, h = 0;
    SDL_GetWindowSize(window_, &w, &h);
    if (w <= 0 || h <= 0) return {};
    const float sx = float(fbWidth) / float(w), sy = float(fbHeight) / float(h);
    const CanvasRect c = canvasRect();
    Renderer::Viewport v;
    v.x = int(std::lround(c.x * sx));
    v.w = std::max(1, int(std::lround((c.x + c.w) * sx)) - v.x);
    const int top = int(std::lround(c.y * sy)), bottom = int(std::lround((c.y + c.h) * sy));
    v.y = fbHeight - bottom;
    v.h = std::max(1, bottom - top);
    v.surround = glm::vec3(theme::shellBg.x, theme::shellBg.y, theme::shellBg.z);
    return v;
}

void App::updateCameraAspect() {
    const CanvasRect c = canvasRect();
    camera_.aspect = c.w / std::max(c.h, 1.0f);
}

// The one-time "Layout changed - Ctrl+Shift+I to keep it" hint comes from watchLayoutChanges (Config.cpp),
// which compares the layout with the stored one; shell edits only need to change layout_.
void App::noteLayoutChanged() {}

void App::applyPreset(int preset) {
    UiLayout l = standardLayout(uiPrefs_.shellLayout);
    l.customize = layout_.customize;
    l.consoleOpen = layout_.consoleOpen;
    l.consoleHeight = layout_.consoleHeight;
    l.lightboxOpen = layout_.lightboxOpen;
    l.lightboxRows = layout_.lightboxRows;
    l.lightboxTab = layout_.lightboxTab;
    l.lightboxBottom = layout_.lightboxBottom;
    l.leftHanded = layout_.leftHanded;
    static const char* const kNames[] = {"Standard", "Sculpt", "Wide"};
    if (preset == 1) {
        l.leftTrayOpen = l.rightTrayOpen = false;
    } else if (preset == 2) {
        l.leftTrayOpen = true;
        l.leftTray = {"brush", "stroke", "alpha"};
        l.rightTray = {"tool", "material"};
    }
    layout_ = std::move(l);
    collapsedPalettes_.clear();
    dropdown_.clear();
    status_ = std::string("Layout: ") + kNames[std::clamp(preset, 0, 2)] + " (Ctrl+Shift+I keeps it)";
    lastCommandOk_ = true;
}

void App::removePaletteFromTrays(const std::string& id) {
    for (auto* tray : {&layout_.leftTray, &layout_.rightTray}) tray->erase(std::remove(tray->begin(), tray->end(), id), tray->end());
}

void App::autoCollapseTray(bool right, const std::string& keep) {
    if (!(right ? uiPrefs_.autoCollapseRight : uiPrefs_.autoCollapseLeft)) return;
    for (const std::string& other : right ? layout_.rightTray : layout_.leftTray)
        if (other != keep && std::find(collapsedPalettes_.begin(), collapsedPalettes_.end(), other) == collapsedPalettes_.end())
            collapsedPalettes_.push_back(other);
}

void App::movePaletteToTray(const std::string& id, bool right) {
    removePaletteFromTrays(id);
    (right ? layout_.rightTray : layout_.leftTray).push_back(id);
    (right ? layout_.rightTrayOpen : layout_.leftTrayOpen) = true;
    collapsedPalettes_.erase(std::remove(collapsedPalettes_.begin(), collapsedPalettes_.end(), id), collapsedPalettes_.end());
    autoCollapseTray(right, id);
    if (dropdown_ == id) {
        dropdown_.clear();
        ImGui::ClosePopupsExceptModals();
    }
    flashPalette_ = id;
    flashStart_ = ImGui::GetTime();
    noteLayoutChanged();
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------

void App::drawCanvasFrame() {
    const CanvasRect c = shell_.canvas;
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    if (theme::look.tiled) { // the 3D view is a tile too: its border just outside it, in the gap
        const float t = std::max(1.0f, std::floor(uiScale_));
        const CanvasRect frame{c.x - t, c.y - t, c.w + 2.0f * t, c.h + 2.0f * t};
        tileFrame(dl, frame, sameRect(c, g_focus));
        return;
    }
    dl->AddRect(ImVec2(c.x - 1.0f, c.y - 1.0f), ImVec2(c.x + c.w + 1.0f, c.y + c.h + 1.0f), theme::u32(theme::canvasBorder));
}

void App::drawShell() {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    if (lookDirty_) { // Preferences > Interface > Look changed: applied here, outside any pushed style
        lookDirty_ = false;
        applyLook();
    }
    shell_ = computeShell(layout_, vp->Size.x, vp->Size.y, uiScale_, topInset(), shellMetrics());
    {
        const ImVec2 m = ImGui::GetIO().MousePos;
        g_focus = {};
        for (const CanvasRect* r : {&shell_.title, &shell_.paletteBar, &shell_.topShelf, &shell_.leftShelf, &shell_.rightShelf,
                                    &shell_.bottomShelf, &shell_.leftTray, &shell_.rightTray, &shell_.console, &shell_.canvas})
            if (r->w > 0.0f && r->h > 0.0f && r->contains(m.x, m.y)) g_focus = *r;
        focusRect_ = g_focus;
    }
    if (shell_.notchBand.h > 0.0f) // the band beside the camera: plain shell colour, so it reads as part of the bezel
        ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(0, 0), ImVec2(shell_.notchBand.w, shell_.notchBand.h),
                                                      theme::u32(theme::shellBg));
    // Splats arriving (file, command or AI agent) bring their palette into the right tray.
    if (doc_.splatsVersion != seenSplatsVersion_) {
        seenSplatsVersion_ = doc_.splatsVersion;
        if (!doc_.splats.empty() && std::count(layout_.leftTray.begin(), layout_.leftTray.end(), "splats") +
                                            std::count(layout_.rightTray.begin(), layout_.rightTray.end(), "splats") ==
                                        0)
            layout_.rightTray.push_back("splats");
    }
    if (status_ != statusShown_) {
        statusShown_ = status_;
        statusSince_ = ImGui::GetTime();
        if (!lastCommandOk_) redrawIn(6.05); // the error colour below goes after 6 s, idle or not
    }
    drawLookChrome();
    drawCanvasFrame();
    if (sculptForgeLayout()) drawStatusBar(shell_);
    else drawTitleStrip(shell_);
    if (layout_.paletteBarVisible && !sculptForgeLayout()) drawPaletteBar(shell_);
    if (shell_.shelves) {
        drawShelf(kShelfTop, shell_.topShelf);
        drawShelf(kShelfLeft, shell_.leftShelf);
        drawShelf(kShelfRight, shell_.rightShelf);
        drawShelf(kShelfBottom, shell_.bottomShelf);
        if (shell_.leftTray.w > 0.0f) drawTray(false, shell_.leftTray);
        if (shell_.rightTray.w > 0.0f) drawTray(true, shell_.rightTray);
        drawDivider(false, shell_.leftDivider);
        drawDivider(true, shell_.rightDivider);
    }
    if (sculptForgeLayout()) drawTaskMenu();
    else drawPaletteDropdown();
    finishShelfDrag();
}

void App::drawTitleStrip(const ShellGeometry& g) {
    const float s = uiScale_;
    if (!beginShellWindow("##TitleStrip", g.title, theme::shellBg, ImVec2(8.0f * s, 0.0f))) {
        ImGui::End();
        return;
    }
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float h = g.title.h;
    dl->AddLine(ImVec2(g.title.x, g.title.y + h - 1.0f), ImVec2(g.title.x + g.title.w, g.title.y + h - 1.0f),
                theme::u32(theme::border));
    const float textY = std::floor((h - ImGui::GetTextLineHeight()) * 0.5f);
    const float buttonH = h - 6.0f * s;
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0f * s, (buttonH - ImGui::GetFontSize()) * 0.5f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f * s, 0.0f));

    // App mark and name.
    ImGui::SetCursorPos(ImVec2(8.0f * s, textY));
    ImGui::TextColored(theme::brand, ICON_LC_BOX); // the brand mark keeps its orange in every look
    ImGui::SameLine(0.0f, 5.0f * s);
    if (fontBold_) ImGui::PushFont(fontBold_);
    ImGui::TextUnformatted("SculptForge");
    if (fontBold_) ImGui::PopFont();

    // Document name; click saves.
    ImGui::SameLine(0.0f, 16.0f * s);
    std::string name = doc_.path.empty() ? std::string(tr("Untitled"))
                                         : std::filesystem::path(std::u8string(doc_.path.begin(), doc_.path.end())).filename().string();
    if (doc_.dirty()) name += " *";
    ImGui::SetCursorPosY((h - buttonH) * 0.5f);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, theme::headerHover);
    if (ImGui::Button((name + "##docname").c_str(), ImVec2(0, buttonH))) saveProjectAs(false);
    ImGui::PopStyleColor(2);
    ui::HelpText("Project", "Ctrl+S", "Click to save the project. * means it has unsaved changes.", "Title");

    // Right-aligned controls: QuickSave, layout presets, Menus, search. Measured first.
    const ImGuiStyle& st = ImGui::GetStyle();
    auto buttonW = [&](const char* label) { return ImGui::CalcTextSize(label, nullptr, true).x + st.FramePadding.x * 2.0f; };
    const bool quickSave = actions_.find("file.quicksave") != nullptr;
    const float searchW = 150.0f * s;
    const float windowButtonW = buttonH + 6.0f * s; // minimise and close, at the far right like ZBrush's
    float rightW = searchW + buttonW(tr(ICON_LC_MENU " Menus")) + 3.0f * (buttonH + 2.0f * s) + 18.0f * s +
                   10.0f * s + 2.0f * windowButtonW + 2.0f * s;
    if (quickSave) rightW += buttonW(tr("Save Snapshot")) + 8.0f * s;
    const float rightX = g.title.w - 8.0f * s - rightW;

    // Info readout and the status message, elided to fit: secondary, so in the 12 px font.
    ImGui::SameLine(0.0f, 14.0f * s);
    const float infoX = ImGui::GetCursorPosX();
    if (fontSmall_) ImGui::PushFont(fontSmall_);
    ImGui::SetCursorPosY(std::floor((h - ImGui::GetTextLineHeight()) * 0.5f));
    size_t tris = 0, verts = 0;
    for (int i = 0; i < doc_.subtoolCount(); ++i) {
        tris += doc_.subtoolMesh(i).triangleCount();
        verts += doc_.subtoolMesh(i).vertexCount();
    }
    char info[256];
    std::snprintf(info, sizeof info, tr("Verts %s   Tris %s   Part %s (%d/%d)   Layer %s%s%s"),
                  shortCount(verts).c_str(), shortCount(tris).c_str(), doc_.activeSubtoolInfo().name.c_str(),
                  doc_.activeSubtool + 1, doc_.subtoolCount(),
                  doc_.activeLayer >= 0 && doc_.activeLayer < int(doc_.mesh.layers.size())
                      ? doc_.mesh.layers[size_t(doc_.activeLayer)].name.c_str()
                      : tr("Base"),
                  doc_.splats.empty() ? "" : tr("   Splats "), doc_.splats.empty() ? "" : shortCount(doc_.splats.size()).c_str());
    // The frame rate gets a fixed-width slot, refreshed twice a second: a number that changes width every frame
    // would make everything after it (the status message) jitter.
    if (ImGui::GetTime() - fpsShownAt_ > 0.5 || fpsShown_ < 0.0f) {
        fpsShown_ = ImGui::GetIO().Framerate;
        fpsShownAt_ = ImGui::GetTime();
    }
    char fps[32];
    std::snprintf(fps, sizeof fps, "%.0f fps", fpsShown_);
    const float fpsSlot = ImGui::CalcTextSize("0000 fps").x;
    const float avail = rightX - infoX - 16.0f * s;
    const float infoW = ImGui::CalcTextSize(info).x + 18.0f * s + fpsSlot;
    if (infoW < avail) {
        ImGui::TextDisabled("%s", info);
        ImGui::SameLine(0.0f, 18.0f * s);
        const float slotX = ImGui::GetCursorPosX();
        ImGui::SetCursorPosX(slotX + fpsSlot - ImGui::CalcTextSize(fps).x); // right-aligned in the slot
        ImGui::TextDisabled("%s", fps);
        ImGui::SameLine(0.0f, 0.0f);
        ImGui::SetCursorPosX(slotX + fpsSlot);
        const std::string msg = layout_.customize ? std::string(tr("Customize: Ctrl+Alt+drag to move or remove items")) : tr(status_);
        if (!msg.empty() && avail - infoW > 60.0f * s) {
            // The status message is what just happened, so it stands out from the readout: the regular font, in
            // the bright text colour (still elided to the space the small-font readout leaves).
            ImGui::SameLine(0.0f, 20.0f * s);
            if (fontSmall_) ImGui::PopFont();
            ImGui::SetCursorPosY(std::floor((h - ImGui::GetTextLineHeight()) * 0.5f));
            const bool error = !layout_.customize && !lastCommandOk_ && ImGui::GetTime() - statusSince_ < 6.0;
            ImGui::PushStyleColor(ImGuiCol_Text, layout_.customize ? theme::accent : error ? theme::danger : theme::textBright);
            ImGui::TextUnformatted(elide(ImGui::GetFont(), msg, avail - infoW - 20.0f * s).c_str());
            ImGui::PopStyleColor();
            if (ImGui::IsItemHovered() && msg.size() > 40) ImGui::SetTooltip("%s", msg.c_str());
            if (fontSmall_) ImGui::PushFont(fontSmall_);
        }
    } else {
        ImGui::TextDisabled("%s", elide(ImGui::GetFont(), info, std::max(0.0f, avail)).c_str());
    }
    if (fontSmall_) ImGui::PopFont();

    // QuickSave, the layout presets and Menus are small dark chips with readable labels: buttons, but quieter than
    // the shelves (Menus on is an option: raised grey with an underline). 1 2 3 are segments of one control.
    ImGui::SetCursorPos(ImVec2(rightX, (h - buttonH) * 0.5f));
    ImGui::PushStyleColor(ImGuiCol_Button, theme::toggleOff);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, theme::toggleOffHover);
    ImGui::PushStyleColor(ImGuiCol_Text, theme::textOff);
    if (quickSave) {
        ui::ActionButton(*this, "file.quicksave", ImVec2(0, buttonH));
        ImGui::SameLine(0.0f, 8.0f * s);
    }
    for (int i = 1; i <= 3; ++i) {
        ui::ActionButton(*this, ("ui.preset." + std::to_string(i)).c_str(), ImVec2(buttonH + 2.0f * s, buttonH));
        ui::HelpAction(*this, ("ui.preset." + std::to_string(i)).c_str());
        ImGui::SameLine(0.0f, i < 3 ? 1.0f * s : 4.0f * s); // 6 px in all, as rightW counts
    }
    ImGui::SameLine(0.0f, 8.0f * s);
    ui::ActionButton(*this, "ui.menus", ImVec2(0, buttonH), ui::Look::IconLabel);
    ImGui::PopStyleColor(3);
    ImGui::SameLine(0.0f, 8.0f * s);
    // Search: a field-looking button that opens the command palette (F3).
    {
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const bool clicked = ImGui::InvisibleButton("##search", ImVec2(searchW, buttonH));
        const bool hovered = ImGui::IsItemHovered();
        dl->AddRectFilled(p, ImVec2(p.x + searchW, p.y + buttonH), theme::u32(hovered ? theme::frameHover : theme::frame), theme::rounding(3.0f * s));
        const float ty = p.y + (buttonH - ImGui::GetFontSize()) * 0.5f;
        dl->AddText(ImVec2(p.x + 7.0f * s, ty), theme::u32(theme::textDim), tr(ICON_LC_SEARCH "  Search"));
        const ImVec2 kw = ImGui::CalcTextSize("F3");
        dl->AddText(ImVec2(p.x + searchW - kw.x - 7.0f * s, ty), theme::u32(theme::textDim, 0.8f), "F3");
        if (clicked) {
            if (const Action* a = actions_.find("ui.command_palette"); a && a->run) a->run();
        }
        ui::HelpText("Search", "F3", "Find any command, brush or material by name and run it.", "Title");
    }
    // Window controls: minimise, and close (asks to save changes first).
    ImGui::SameLine(0.0f, 10.0f * s);
    for (int k = 0; k < 2; ++k) {
        const bool close = k == 1;
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const bool clicked = ImGui::InvisibleButton(close ? "Close##window" : "Minimize##window", ImVec2(windowButtonW, buttonH));
        const bool hovered = ImGui::IsItemHovered();
        if (hovered)
            dl->AddRectFilled(p, ImVec2(p.x + windowButtonW, p.y + buttonH),
                              theme::u32(close ? theme::danger : theme::frameHover), theme::rounding(3.0f * s));
        const char* glyph = close ? ICON_LC_X : ICON_LC_MINUS;
        ui::CentredGlyph(dl, nullptr, p, ImVec2(p.x + windowButtonW, p.y + buttonH),
                         theme::u32(hovered && close ? theme::text : theme::textDim), glyph);
        if (close) {
            if (clicked) requestQuit();
            ui::HelpAction(*this, "file.quit");
        } else {
            if (clicked) SDL_MinimizeWindow(window_);
            ui::HelpText("Minimize", nullptr, "Minimize the SculptForge window.", "Title");
        }
        if (!close) ImGui::SameLine(0.0f, 2.0f * s);
    }
    ImGui::PopStyleVar(2);
    ImGui::End();
}

void App::drawPaletteBar(const ShellGeometry& g) {
    const float s = uiScale_;
    if (!beginShellWindow("##PaletteBar", g.paletteBar, theme::shellBg, ImVec2(8.0f * s, 0.0f))) {
        ImGui::End();
        return;
    }
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddLine(ImVec2(g.paletteBar.x, g.paletteBar.y + g.paletteBar.h - 1.0f),
                ImVec2(g.paletteBar.x + g.paletteBar.w, g.paletteBar.y + g.paletteBar.h - 1.0f), theme::u32(theme::border));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f * s, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.0f);
    int count = 0;
    const PaletteDef* list = palettes(count);
    // File and Edit first and Help last, where every other desktop app has them, the rest A to Z in between.
    // ZBrush purists can switch to strict alphabetical order in Preferences > Interface.
    std::vector<int> order;
    if (!uiPrefs_.alphabeticalPalettes)
        for (const char* first : {"file", "edit"})
            for (int i = 0; i < count; ++i)
                if (std::string_view(list[i].id) == first) order.push_back(i);
    int help = -1;
    for (int i = 0; i < count; ++i) {
        if (!uiPrefs_.alphabeticalPalettes && std::string_view(list[i].id) == "help") help = i; // last, as usual
        else if (std::find(order.begin(), order.end(), i) == order.end()) order.push_back(i);
    }
    if (help >= 0) order.push_back(help);
    for (size_t k = 0; k < order.size(); ++k) {
        const int i = int(k);
        const PaletteDef& p = list[order[k]];
        const bool inTray = std::count(layout_.leftTray.begin(), layout_.leftTray.end(), p.id) +
                                std::count(layout_.rightTray.begin(), layout_.rightTray.end(), p.id) >
                            0;
        const bool open = dropdown_ == p.id;
        ImGui::PushStyleColor(ImGuiCol_Button, open ? theme::header : ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, theme::headerHover);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, theme::header);
        ImGui::PushStyleColor(ImGuiCol_Text, inTray ? theme::accent : open ? theme::textHover : theme::text);
        const bool gap = !uiPrefs_.alphabeticalPalettes && (k == 2 || (help >= 0 && k + 1 == order.size()));
        if (i) ImGui::SameLine(0.0f, gap ? 10.0f * s : -1.0f); // small gaps after File, Edit and before Help
        const std::string label = std::string(tr(p.name)) + "##palette." + p.id;
        // While a drop-down is open, the click that closes it (ImGui swallows that one) opens another palette
        // when it lands on that palette's name, as in a menu bar; on the open palette's own name it just closes.
        const bool pressed = ImGui::Button(label.c_str(), ImVec2(0, g.paletteBar.h - 1.0f));
        const bool switchTo = !dropdown_.empty() && !open && ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
                              ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup);
        if (pressed || switchTo) {
            if (inTray) {
                flashPalette_ = p.id;
                flashStart_ = ImGui::GetTime();
            } else if (!open) {
                dropdown_ = p.id;
                dropdownX_ = ImGui::GetItemRectMin().x;
                openDropdown_ = true;
            }
        }
        ImGui::PopStyleColor(4);
        ui::HelpAction(*this, ("ui.palette." + std::string(p.id)).c_str());
    }
    ImGui::PopStyleVar(3);
    ImGui::End();
}

// ---------------------------------------------------------------------------
// Looks and the SculptForge layout (Preferences > Interface > Look)
// ---------------------------------------------------------------------------

ShellMetrics App::shellMetrics() const {
    const float gap = theme::look.tiled ? theme::look.gap : 0.0f;
    if (sculptForgeLayout()) return sculptForgeMetrics(gap);
    ShellMetrics m;
    m.gap = gap;
    if (gap > 0.0f) m.dividerW = 0.0f; // the gaps beside the trays are the dividers
    return m;
}

void App::setShellLayout(int layoutKind) {
    layoutKind = std::clamp(layoutKind, 0, 1);
    if (uiPrefs_.shellLayout == layoutKind) return;
    uiPrefs_.shellLayout = layoutKind;
    // The shelves change to that layout's standard ones; the trays, the console and the Library stay as they are.
    UiLayout l = standardLayout(layoutKind);
    l.customize = layout_.customize;
    l.leftTray = layout_.leftTray;
    l.rightTray = layout_.rightTray;
    l.leftTrayOpen = layout_.leftTrayOpen;
    l.rightTrayOpen = layout_.rightTrayOpen;
    l.leftTrayWidth = layout_.leftTrayWidth;
    l.rightTrayWidth = layout_.rightTrayWidth;
    l.openSubpalettes = layout_.openSubpalettes;
    l.consoleOpen = layout_.consoleOpen;
    l.consoleHeight = layout_.consoleHeight;
    l.lightboxOpen = layout_.lightboxOpen;
    l.lightboxRows = layout_.lightboxRows;
    l.lightboxTab = layout_.lightboxTab;
    l.lightboxBottom = layout_.lightboxBottom;
    l.leftHanded = layout_.leftHanded;
    l.shelfTiles = layout_.shelfTiles;
    layout_ = std::move(l);
    dropdown_.clear();
    updateCameraAspect();
}

void App::applyLook() {
    std::string message;
    // From a fresh style: the looks set colours and some sizes, and ScaleAllSizes on the style of an earlier call
    // would scale what they leave alone once more each time (the minimum window size grew until the Sculpt
    // Timeline strip covered the axis gizmo at 2400x1350).
    const float hoverDelay = ImGui::GetStyle().HoverDelayNormal; // Preferences > Interface (Ui.cpp)
    ImGui::GetStyle() = ImGuiStyle();
    ImGui::GetStyle().HoverDelayNormal = hoverDelay;
    theme::applyLook(uiPrefs_.look, &message);
    ImGui::GetStyle().ScaleAllSizes(uiScale_);
    const bool mono = theme::look.monoLabels || theme::look.monoAll;
    if (fontInterSmall_) {
        fontSmall_ = mono && fontMonoSmall_ ? fontMonoSmall_ : fontInterSmall_;
        fontTiny_ = mono && fontMonoTiny_ ? fontMonoTiny_ : fontInterTiny_;
        fontBold_ = mono && fontMonoBold_ ? fontMonoBold_ : fontInterBold_;
    }
    ImGui::GetIO().FontDefault = theme::look.monoAll && fontMono_ ? fontMono_ : nullptr; // nullptr: Inter, the first font
    ImVec4 centre, edge; // the 3D view's gradient background (a flat background the user picked stays as it is)
    theme::lookViewportGradient(centre, edge);
    renderSettings_.gradientCentre = glm::vec3(centre.x, centre.y, centre.z);
    renderSettings_.gradientEdge = glm::vec3(edge.x, edge.y, edge.z);
    if (!message.empty()) {
        status_ = message;
        lastCommandOk_ = false;
    }
    updateCameraAspect();
    redrawIn(0.0);
}

// The wallpaper in a tiled look's gaps, and Phosphor's optional effects. Nothing here ever covers the 3D view.
void App::drawLookChrome() {
    if (!theme::look.tiled) return;
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float W = vp->Size.x, H = vp->Size.y, s = uiScale_;
    const CanvasRect& c = shell_.canvas;
    ImDrawList* bg = ImGui::GetBackgroundDrawList();
    const ImU32 wall = theme::u32(theme::gapBg);
    // Four bands around the canvas.
    bg->AddRectFilled(ImVec2(0, 0), ImVec2(W, c.y), wall);
    bg->AddRectFilled(ImVec2(0, c.y + c.h), ImVec2(W, H), wall);
    bg->AddRectFilled(ImVec2(0, c.y), ImVec2(c.x, c.y + c.h), wall);
    bg->AddRectFilled(ImVec2(c.x + c.w, c.y), ImVec2(W, c.y + c.h), wall);
    if (theme::look.headers == theme::LookStyle::Headers::Grid) { // drafting paper in the gaps
        const float step = 12.0f * s;
        const ImU32 fine = theme::u32(theme::accent, 0.07f), major = theme::u32(theme::accent, 0.16f);
        auto outsideCanvas = [&](float x0, float y0, float x1, float y1) { return x1 <= c.x || x0 >= c.x + c.w || y1 <= c.y || y0 >= c.y + c.h; };
        for (int i = 0; float(i) * step < W; ++i) {
            const float x = std::floor(float(i) * step) + 0.5f;
            const ImU32 col = i % 5 == 0 ? major : fine;
            if (outsideCanvas(x, 0, x + 1, H)) bg->AddLine(ImVec2(x, 0), ImVec2(x, H), col);
            else {
                bg->AddLine(ImVec2(x, 0), ImVec2(x, c.y), col);
                bg->AddLine(ImVec2(x, c.y + c.h), ImVec2(x, H), col);
            }
        }
        for (int i = 0; float(i) * step < H; ++i) {
            const float y = std::floor(float(i) * step) + 0.5f;
            const ImU32 col = i % 5 == 0 ? major : fine;
            if (outsideCanvas(0, y, W, y + 1)) bg->AddLine(ImVec2(0, y), ImVec2(W, y), col);
            else {
                bg->AddLine(ImVec2(0, y), ImVec2(c.x, y), col);
                bg->AddLine(ImVec2(c.x + c.w, y), ImVec2(W, y), col);
            }
        }
    }
    if (theme::look.glow && theme::look.outlines == 2 && g_focus.w > 0.0f) { // a soft phosphor bloom around the focused tile, in the gap
        for (int k = 1; k <= 4; ++k) {
            const float d = float(k) * s;
            bg->AddRect(ImVec2(g_focus.x - d, g_focus.y - d), ImVec2(g_focus.x + g_focus.w + d, g_focus.y + g_focus.h + d),
                        theme::u32(theme::accent, 0.28f / float(k)), 0.0f, 0, 1.0f * s);
        }
    }
    if (theme::look.scanlines) { // over the chrome (shells, menus, pop-ups), never over the 3D view
        ImDrawList* fg = ImGui::GetForegroundDrawList();
        const float step = std::max(2.0f, std::floor(3.0f * s));
        const ImU32 line = IM_COL32(0, 0, 0, 46);
        for (float y = 0.5f; y < H; y += step) {
            if (y < c.y || y > c.y + c.h) fg->AddLine(ImVec2(0, y), ImVec2(W, y), line);
            else {
                fg->AddLine(ImVec2(0, y), ImVec2(c.x - 1.0f, y), line);
                fg->AddLine(ImVec2(c.x + c.w + 1.0f, y), ImVec2(W, y), line);
            }
        }
    }
}

namespace {

std::string upper(std::string s) {
    for (auto& c : s) c = char(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

struct StatusModule {
    const char* icon;
    std::string label; // "brush"
    std::string value; // "Clay"
    bool mode = false; // the mode block (filled with the accent)
};

} // namespace

void App::drawStatusBar(const ShellGeometry& g) {
    const float s = uiScale_;
    if (!beginShellWindow("##TitleStrip", g.title, theme::shellBg, ImVec2(0.0f, 0.0f))) {
        ImGui::End();
        return;
    }
    using Status = theme::LookStyle::Status;
    const Status style = theme::look.status == Status::Plain ? Status::Modules : theme::look.status;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 o(g.title.x, g.title.y);
    const float h = g.title.h;
    const float inset = theme::look.tiled ? std::max(1.0f, std::floor(s)) : 0.0f; // inside the tile border
    if (!theme::look.tiled)
        dl->AddLine(ImVec2(o.x, o.y + h - 1.0f), ImVec2(o.x + g.title.w, o.y + h - 1.0f), theme::u32(theme::border));
    if (theme::look.headers == theme::LookStyle::Headers::Grid) { // Blueprint: the bar is a drawing's title block
        const float step = 6.0f * s;
        for (float x = o.x + step; x < o.x + g.title.w; x += step)
            dl->AddLine(ImVec2(std::floor(x) + 0.5f, o.y + inset), ImVec2(std::floor(x) + 0.5f, o.y + h - inset), theme::u32(theme::accent, 0.035f));
    }
    // The bar's readouts in the look's small font: monospace in the looks, Inter in Classic.
    ImFont* monoSmall = theme::look.id != 0 && fontMonoSmall_ ? fontMonoSmall_ : fontSmall_ ? fontSmall_ : ImGui::GetFont();
    ImFont* bold = fontBold_ ? fontBold_ : ImGui::GetFont();
    auto textW = [](ImFont* f, const std::string& t) { return f->CalcTextSizeA(f->FontSize, FLT_MAX, 0.0f, t.c_str()).x; };
    auto textAt = [&](ImFont* f, float x, const ImVec4& col, const std::string& t) {
        dl->AddText(f, f->FontSize, ImVec2(std::floor(x), std::floor(o.y + (h - f->FontSize) * 0.5f)), theme::u32(col), t.c_str());
        return x + textW(f, t);
    };

    // Brand: the cube in the SculptForge orange, the name.
    float x = o.x + 12.0f * s;
    x = textAt(ImGui::GetFont(), x, theme::brand, ICON_LC_BOX) + 6.0f * s;
    const bool caps = style == Status::Terminal;
    x = textAt(bold, x, theme::textBright, caps ? "SCULPTFORGE" : "SculptForge");
    x += (style == Status::TitleBlock ? 14.0f : 18.0f) * s;
    if (style == Status::TitleBlock || style == Status::Terminal) {
        dl->AddLine(ImVec2(std::floor(x) + 0.5f, o.y + inset), ImVec2(std::floor(x) + 0.5f, o.y + h - inset), theme::u32(theme::tileBorder));
        x += 8.0f * s;
    }

    // The task menus.
    int menuCount = 0;
    const TaskMenuDef* menus = taskMenus(menuCount);
    const int openMenu = dropdown_.empty() ? -1 : taskMenuOf(dropdown_);
    ImFont* menuFont = style == Status::Modules ? ImGui::GetFont() : monoSmall;
    for (int i = 0; i < menuCount; ++i) {
        const TaskMenuDef& m = menus[i];
        const std::string shown = style == Status::Modules ? std::string(tr(m.name)) : upper(tr(m.name));
        const float w = textW(menuFont, shown) + 16.0f * s;
        ImGui::SetCursorScreenPos(ImVec2(x, o.y + inset));
        const std::string id = std::string(tr(m.name)) + "##menu." + m.id;
        const bool pressed = ImGui::InvisibleButton(id.c_str(), ImVec2(w, h - 2.0f * inset));
        const bool hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup);
        const bool open = openMenu == i;
        const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
        if (open || hovered) {
            if (style == Status::Terminal && open) dl->AddRectFilled(a, b, theme::u32(theme::accent)); // inverse video
            else dl->AddRectFilled(a, b, theme::u32(open ? theme::selection : theme::headerHover, open ? 1.0f : 0.7f));
        }
        if (open && style != Status::Terminal) dl->AddRectFilled(ImVec2(a.x, b.y - 2.0f * s), b, theme::u32(theme::accent));
        const ImVec4 col = open ? (style == Status::Terminal ? theme::textOnAccent : theme::textBright) : theme::text;
        textAt(menuFont, a.x + 8.0f * s, col, shown);
        ImGui::LogText("%s", tr(m.name));
        ui::HelpText(m.name, nullptr, "Open this menu. Its sections are the palettes of the ZBrush-style layout.", "Menus");
        // Click to open or close; while one is open, pointing at another menu switches to it, as in a menu bar.
        const bool switchTo = openMenu >= 0 && !open && hovered;
        if (pressed || switchTo) {
            if (open && pressed) {
                dropdown_.clear();
            } else {
                // The section shown last, else the first one; one that sits in a tray is already on screen, so the
                // menu opens on the next one (a menu whose sections are all in trays flashes the tray instead).
                auto inTray = [&](const std::string& id) {
                    return std::count(layout_.leftTray.begin(), layout_.leftTray.end(), id) +
                               std::count(layout_.rightTray.begin(), layout_.rightTray.end(), id) >
                           0;
                };
                std::string section = menuSection_[m.id];
                if (section.empty() || taskMenuOf(section) != i || inTray(section)) {
                    section.clear();
                    for (int k = 0; k < 6 && m.sections[k] && section.empty(); ++k)
                        if (!inTray(m.sections[k])) section = m.sections[k];
                }
                if (section.empty()) {
                    flashPalette_ = m.sections[0];
                    flashStart_ = ImGui::GetTime();
                } else {
                    dropdown_ = section;
                    menuX_ = a.x;
                    openDropdown_ = true;
                }
            }
        }
        x += w + (style == Status::Modules ? 2.0f : 0.0f) * s;
    }
    const float menusEnd = x + 12.0f * s;

    // Right side, measured first: window controls, search, the layout presets as workspaces, then the modules.
    const float buttonH = h - 2.0f * inset - 8.0f * s;
    const float winW = buttonH + 8.0f * s;
    const float searchW = 150.0f * s;
    float right = o.x + g.title.w - 6.0f * s - inset;
    const float closeX = right - 2.0f * winW - 2.0f * s;
    right = closeX - 10.0f * s;
    const float searchX = right - searchW;
    right = searchX - 10.0f * s;
    const float wsW = buttonH + 4.0f * s;
    const float presetsX = right - 3.0f * wsW - 2.0f * s;
    right = presetsX - 14.0f * s;

    // Modules: mode, brush, document, triangles, frame rate.
    std::vector<StatusModule> mods;
    {
        const char* mode = paintMode_ ? "Paint" : "Sculpt";
        for (const char* t : {"tool.move", "tool.scale", "tool.rotate"})
            if (const Action* a = actions_.find(t); a && a->checked && a->checked()) mode = a->label.c_str();
        mods.push_back({ICON_LC_SQUARE_TERMINAL, "mode", upper(tr(mode)), true});
        std::string brush = brushName(brush_);
        if (doc_.activeBrush >= 0 && doc_.activeBrush < int(doc_.brushes.size())) brush = doc_.brushes[size_t(doc_.activeBrush)].name;
        mods.push_back({ICON_LC_BRUSH, "brush", brush});
        std::string name = doc_.path.empty() ? std::string(tr("Untitled"))
                                             : std::filesystem::path(std::u8string(doc_.path.begin(), doc_.path.end())).filename().string();
        if (doc_.dirty()) name += " *";
        mods.push_back({ICON_LC_FILE, "file", name});
        size_t tris = 0;
        for (int i = 0; i < doc_.subtoolCount(); ++i) tris += doc_.subtoolMesh(i).triangleCount();
        mods.push_back({ICON_LC_TRIANGLE, "tris", shortCount(tris)});
        if (ImGui::GetTime() - fpsShownAt_ > 0.5 || fpsShown_ < 0.0f) {
            fpsShown_ = ImGui::GetIO().Framerate;
            fpsShownAt_ = ImGui::GetTime();
        }
        char fps[16];
        std::snprintf(fps, sizeof fps, "%3.0f", fpsShown_);
        mods.push_back({ICON_LC_ACTIVITY, "fps", fps});
        // View > Lock camera (#62, Shift+L): left-drags on the background start strokes instead of turning the view.
        if (uiPrefs_.airStrokes) mods.push_back({ICON_LC_LOCK, "camera", upper(tr("Camera locked")), true});
    }
    ImFont* labelFont = theme::look.id != 0 && fontMonoTiny_ ? fontMonoTiny_ : fontTiny_ ? fontTiny_ : monoSmall;
    auto moduleW = [&](const StatusModule& m) {
        switch (style) {
        case Status::Terminal:
            return m.mode ? textW(monoSmall, "[" + m.value + "]") + 10.0f * s : textW(monoSmall, m.label + ":" + m.value);
        case Status::TitleBlock:
            return textW(labelFont, upper(m.label)) + 6.0f * s + textW(m.mode ? monoSmall : monoSmall, m.value) + 20.0f * s;
        default:
            return m.mode ? textW(monoSmall, m.value) + 20.0f * s : textW(monoSmall, std::string(m.icon) + " " + m.value);
        }
    };
    const float sepW = style == Status::Terminal ? textW(monoSmall, " | ") : style == Status::TitleBlock ? 0.0f : 16.0f * s;
    // Drop the least important modules (from the file name inwards) until they fit beside the menus.
    std::vector<int> shown = {0, 1, 2, 3, 4};
    if (mods.size() > 5) shown.insert(shown.begin() + 1, 5); // Camera locked, beside the mode (never dropped)
    auto total = [&] {
        float w = 0.0f;
        for (size_t k = 0; k < shown.size(); ++k) w += moduleW(mods[size_t(shown[k])]) + (k ? sepW : 0.0f);
        return w;
    };
    for (int drop : {2, 4, 1})
        if (total() > right - menusEnd - 120.0f * s) shown.erase(std::remove(shown.begin(), shown.end(), drop), shown.end());
    float mx = right - total();
    const float modulesX = mx;
    for (size_t k = 0; k < shown.size(); ++k) {
        const StatusModule& m = mods[size_t(shown[k])];
        const float w = moduleW(m);
        ImGui::LogText("%s", m.value.c_str()); // (UI tests read the bar)
        if (k) {
            if (style == Status::Terminal) textAt(monoSmall, mx, theme::textDim, " | ");
            mx += sepW;
        }
        const float y0 = o.y + inset, y1 = o.y + h - inset;
        switch (style) {
        case Status::Terminal:
            if (m.mode) {
                dl->AddRectFilled(ImVec2(mx, y0 + 5.0f * s), ImVec2(mx + w, y1 - 5.0f * s), theme::u32(theme::accent));
                textAt(monoSmall, mx + 5.0f * s, theme::textOnAccent, "[" + m.value + "]");
            } else {
                const float lx = textAt(monoSmall, mx, theme::textDim, m.label + ":");
                textAt(monoSmall, lx, theme::text, m.value);
            }
            break;
        case Status::TitleBlock: {
            dl->AddLine(ImVec2(std::floor(mx) + 0.5f, y0), ImVec2(std::floor(mx) + 0.5f, y1), theme::u32(theme::tileBorder));
            const float lx = textAt(labelFont, mx + 10.0f * s, theme::textDim, upper(m.label)) + 6.0f * s;
            textAt(monoSmall, lx, m.mode ? theme::accent : theme::textBright, m.value);
            break;
        }
        default:
            if (m.mode) {
                dl->AddRectFilled(ImVec2(mx, y0 + 4.0f * s), ImVec2(mx + w, y1 - 4.0f * s), theme::u32(theme::accent));
                textAt(monoSmall, mx + 10.0f * s, theme::textOnAccent, m.value);
            } else {
                const float ix = textAt(monoSmall, mx, theme::accent, m.icon);
                textAt(monoSmall, ix + textW(monoSmall, " "), theme::text, m.value);
            }
        }
        mx += w;
    }
    if (style == Status::TitleBlock)
        dl->AddLine(ImVec2(std::floor(mx + 10.0f * s) + 0.5f, o.y + inset), ImVec2(std::floor(mx + 10.0f * s) + 0.5f, o.y + h - inset),
                    theme::u32(theme::tileBorder));

    // The status message between the menus and the modules: what just happened.
    {
        const std::string msg = layout_.customize ? std::string(tr("Customize: Ctrl+Alt+drag to move or remove items")) : tr(status_);
        const float avail = modulesX - menusEnd - 16.0f * s;
        if (!msg.empty() && avail > 60.0f * s) {
            const bool error = !layout_.customize && !lastCommandOk_ && ImGui::GetTime() - statusSince_ < 6.0;
            const std::string text = (style == Status::Terminal ? "> " : "") + msg;
            textAt(monoSmall, menusEnd, error ? theme::danger : theme::textDim, elide(monoSmall, text, avail));
        }
    }

    // Layout presets 1 2 3, drawn as workspaces.
    for (int i = 1; i <= 3; ++i) {
        const std::string id = "ui.preset." + std::to_string(i);
        ImGui::SetCursorScreenPos(ImVec2(presetsX + float(i - 1) * (wsW + 1.0f * s), o.y + (h - buttonH) * 0.5f));
        const Action* a = actions_.find(id);
        const bool clicked = ImGui::InvisibleButton((std::to_string(i) + "##" + id).c_str(), ImVec2(wsW, buttonH));
        const bool hovered = ImGui::IsItemHovered();
        const ImVec2 a0 = ImGui::GetItemRectMin(), a1 = ImGui::GetItemRectMax();
        if (hovered) dl->AddRectFilled(a0, a1, theme::u32(theme::headerHover));
        const std::string n = std::to_string(i);
        textAt(monoSmall, a0.x + (wsW - textW(monoSmall, n)) * 0.5f, hovered ? theme::textBright : theme::textDim, n);
        if (clicked && a && a->run) a->run();
        ui::HelpAction(*this, id.c_str());
    }

    // Search: opens the command palette (F3), which also finds every palette of the ZBrush-style layout.
    {
        ImGui::SetCursorScreenPos(ImVec2(searchX, o.y + (h - buttonH) * 0.5f));
        const bool clicked = ImGui::InvisibleButton("##search", ImVec2(searchW, buttonH));
        const bool hovered = ImGui::IsItemHovered();
        const ImVec2 p = ImGui::GetItemRectMin();
        dl->AddRectFilled(p, ImVec2(p.x + searchW, p.y + buttonH), theme::u32(hovered ? theme::frameHover : theme::frame));
        if (theme::look.tiled) dl->AddRect(p, ImVec2(p.x + searchW, p.y + buttonH), theme::u32(theme::tileBorder));
        const char* prompt = style == Status::Terminal ? "> search" : style == Status::TitleBlock ? ICON_LC_SEARCH " SEARCH" : ICON_LC_SEARCH " search";
        textAt(monoSmall, p.x + 8.0f * s, theme::textDim, prompt);
        textAt(monoSmall, p.x + searchW - textW(monoSmall, "F3") - 8.0f * s, theme::accent, "F3");
        if (clicked) {
            if (const Action* a = actions_.find("ui.command_palette"); a && a->run) a->run();
        }
        ui::HelpText("Search", "F3", "Find any command, palette, brush or material by name and run it.", "Title");
    }

    // Window controls: minimise, and close (asks to save changes first).
    for (int k = 0; k < 2; ++k) {
        const bool close = k == 1;
        ImGui::SetCursorScreenPos(ImVec2(closeX + float(k) * (winW + 2.0f * s), o.y + (h - buttonH) * 0.5f));
        const bool clicked = ImGui::InvisibleButton(close ? "Close##window" : "Minimize##window", ImVec2(winW, buttonH));
        const bool hovered = ImGui::IsItemHovered();
        const ImVec2 p = ImGui::GetItemRectMin();
        if (hovered) dl->AddRectFilled(p, ImVec2(p.x + winW, p.y + buttonH), theme::u32(close ? theme::danger : theme::frameHover));
        centredText(dl, nullptr, p, ImVec2(p.x + winW, p.y + buttonH), theme::u32(hovered && close ? theme::textOnAccent : theme::textDim),
                    close ? ICON_LC_X : ICON_LC_MINUS);
        if (close) {
            if (clicked) requestQuit();
            ui::HelpAction(*this, "file.quit");
        } else {
            if (clicked) SDL_MinimizeWindow(window_);
            ui::HelpText("Minimize", nullptr, "Minimize the SculptForge window.", "Title");
        }
    }
    ImGui::End();
}

// A task menu's drop-down: its sections (palettes) down the left, the chosen one beside them, under its header (whose
// tray icon still pins it to a tray). dropdown_ is the chosen palette, so ui.palette.* and F3 open it in its menu.
void App::drawTaskMenu() {
    if (openDropdown_) {
        ImGui::OpenPopup("##palettedropdown");
        openDropdown_ = false;
    }
    if (dropdown_.empty()) return;
    const int menuIndex = taskMenuOf(dropdown_);
    const PaletteDef* def = findPalette(dropdown_);
    if (menuIndex < 0 || !def) {
        dropdown_.clear();
        return;
    }
    int count = 0;
    const TaskMenuDef& menu = taskMenus(count)[menuIndex];
    menuSection_[menu.id] = dropdown_;
    int sections = 0;
    while (sections < 6 && menu.sections[sections]) ++sections;
    const float s = uiScale_;
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float listW = sections > 1 ? 168.0f * s : 0.0f, bodyW = 320.0f * s;
    const float pad = std::round(6.0f * s);
    const float w = listW + bodyW + 2.0f * pad + (sections > 1 ? pad : 0.0f);
    const float y = shell_.title.y + shell_.title.h + (theme::look.tiled ? 2.0f * s : 0.0f);
    float x = menuX_;
    if (x <= 0.0f) { // opened by F3 or a key: under its name in the status bar
        x = shell_.title.x + 150.0f * s;
        for (int i = 0; i < menuIndex; ++i) x += ImGui::CalcTextSize(taskMenus(count)[i].name).x + 18.0f * s;
    }
    ImGui::SetNextWindowPos(ImVec2(std::clamp(x, 4.0f * s, vp->Size.x - w - 4.0f * s), y));
    ImGui::SetNextWindowSizeConstraints(ImVec2(w, 0), ImVec2(w, vp->Size.y - y - 8.0f * s));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(pad, pad));
    const bool open = ImGui::BeginPopup("##palettedropdown");
    ImGui::PopStyleVar();
    if (!open) {
        dropdown_.clear();
        menuX_ = 0.0f;
        return;
    }
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (sections > 1) {
        ImGui::BeginGroup();
        ImFont* capFont = fontTiny_ ? fontTiny_ : ImGui::GetFont();
        ImGui::PushFont(capFont);
        ImGui::TextColored(theme::textDim, "%s", upper(tr(menu.name)).c_str());
        ImGui::PopFont();
        for (int k = 0; k < sections; ++k) {
            const PaletteDef* p = findPalette(menu.sections[k]);
            if (!p) continue;
            const std::string title = tr(menu.titles[k] ? menu.titles[k] : p->name);
            const bool current = dropdown_ == p->id;
            const bool inTray = std::count(layout_.leftTray.begin(), layout_.leftTray.end(), p->id) +
                                    std::count(layout_.rightTray.begin(), layout_.rightTray.end(), p->id) >
                                0;
            const float rowH = 28.0f * s;
            const bool clicked = ImGui::InvisibleButton((title + "##section." + p->id).c_str(), ImVec2(listW, rowH));
            const bool hovered = ImGui::IsItemHovered();
            const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
            if (current || hovered) dl->AddRectFilled(a, b, theme::u32(current ? theme::selection : theme::headerHover, current ? 1.0f : 0.6f));
            if (current) dl->AddRectFilled(a, ImVec2(a.x + 2.0f * s, b.y), theme::u32(theme::accent));
            const float ty = std::floor(a.y + (rowH - ImGui::GetFontSize()) * 0.5f);
            dl->AddText(ImVec2(a.x + 10.0f * s, ty), theme::u32(current ? theme::accent : theme::textDim), p->icon);
            dl->AddText(ImVec2(a.x + 34.0f * s, ty), theme::u32(current ? theme::textBright : theme::text), title.c_str());
            if (inTray) dl->AddText(ImVec2(b.x - 22.0f * s, ty), theme::u32(theme::textDim), ICON_LC_PANEL_RIGHT);
            if (clicked) {
                if (inTray) {
                    flashPalette_ = p->id;
                    flashStart_ = ImGui::GetTime();
                } else {
                    dropdown_ = p->id;
                }
            }
            ui::HelpText(title.c_str(), nullptr, inTray ? "This palette is in a tray: click to find it there." : "Show this section.",
                         tr(menu.name));
        }
        ImGui::EndGroup();
        const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
        const float lineX = std::floor(b.x + pad * 0.5f) + 0.5f;
        dl->AddLine(ImVec2(lineX, a.y), ImVec2(lineX, std::max(b.y, ImGui::GetWindowPos().y + ImGui::GetWindowHeight() - pad)),
                    theme::u32(theme::tileBorder));
        ImGui::SameLine(0.0f, pad);
    }
    ImGui::BeginGroup();
    def = findPalette(dropdown_); // a section click may have changed it
    if (def) {
        Demo::pushScope(def->name);
        if (def->scope) Demo::pushScope(def->scope);
        const std::string id = dropdown_;
        if (paletteHeader(id.c_str(), false, true)) drawPaletteBody(id);
        if (def->scope) Demo::popScope();
        Demo::popScope();
    }
    ImGui::EndGroup();
    if (ImGui::IsKeyPressed(ImGuiKey_Escape) && !ImGui::IsAnyItemActive()) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

bool App::paletteHeader(const char* id, bool inTray, bool right) {
    const PaletteDef* def = findPalette(id);
    if (!def) return false;
    const float s = uiScale_;
    const float h = 24.0f * s;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float w = ImGui::GetContentRegionAvail().x;
    const bool collapsed = inTray && std::count(collapsedPalettes_.begin(), collapsedPalettes_.end(), id) > 0;
    using Headers = theme::LookStyle::Headers;
    const Headers headers = theme::look.headers;
    if (headers != Headers::Ascii) dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), theme::u32(theme::header), theme::rounding(3.0f * s));
    if (headers == Headers::Grid) { // drafting grid in the header, a cyan base line
        const float step = 6.0f * s;
        const ImU32 g = theme::u32(theme::accent, 0.10f);
        for (float x = p.x + step; x < p.x + w; x += step) dl->AddLine(ImVec2(std::floor(x) + 0.5f, p.y), ImVec2(std::floor(x) + 0.5f, p.y + h), g);
        for (float y = p.y + step; y < p.y + h; y += step) dl->AddLine(ImVec2(p.x, std::floor(y) + 0.5f), ImVec2(p.x + w, std::floor(y) + 0.5f), g);
        dl->AddLine(ImVec2(p.x, p.y + h - 0.5f), ImVec2(p.x + w, p.y + h - 0.5f), theme::u32(theme::accent, 0.8f));
    } else if (headers == Headers::Bar) { // a terminal pane's title: the accent rule under it
        dl->AddLine(ImVec2(p.x, p.y + h - 0.5f), ImVec2(p.x + w, p.y + h - 0.5f), theme::u32(theme::accent, 0.7f));
    }

    // Tray icon: in a drop-down it moves the palette into the right tray (Shift: left) or can be dragged to one;
    // in a tray it sends the palette back to the bar.
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
    const bool iconClicked = ImGui::InvisibleButton("##totray", ImVec2(h, h));
    const bool iconHovered = ImGui::IsItemHovered();
    if (!inTray && ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceNoDisableHover)) {
        ImGui::SetDragDropPayload("SF_PALETTE", id, std::strlen(id) + 1);
        ImGui::Text(ICON_LC_PANEL_RIGHT "  %s", tr(def->name));
        ImGui::EndDragDropSource();
    }
    if (inTray) ui::HelpText("Back to the palette bar", nullptr, "Take this palette out of the tray.", tr(def->name));
    else
        ui::HelpText("Move to the tray", nullptr,
                     "Keep this palette in the right tray (Shift+click: left tray), or drag the icon onto a tray.", tr(def->name));
    centredText(dl, nullptr, p, ImVec2(p.x + h, p.y + h), theme::u32(iconHovered ? theme::accentHover : theme::textDim),
                inTray ? (right ? ICON_LC_PANEL_RIGHT : ICON_LC_PANEL_LEFT) : ICON_LC_PANEL_RIGHT);
    ImGui::SameLine();
    const bool titleClicked = ImGui::InvisibleButton("##title", ImVec2(std::max(1.0f, w - 2.0f * h), h));
    const bool titleHovered = ImGui::IsItemHovered();
    if (fontBold_) ImGui::PushFont(fontBold_);
    {
        std::string title = tr(def->name);
        if (theme::look.upperHeaders)
            for (auto& c : title) c = char(std::toupper(static_cast<unsigned char>(c)));
        if (headers == Headers::Ascii) title = "[ " + title + " ]";
        const ImVec2 tp(p.x + h + 2.0f * s, p.y + (h - ImGui::GetFontSize()) * 0.5f);
        const ImVec4 col = titleHovered && inTray ? theme::textHover : headers == Headers::Bar || headers == Headers::Ascii ? theme::accent : theme::text;
        dl->AddText(tp, theme::u32(col), title.c_str());
        if (headers == Headers::Ascii) { // [ TOOL ]--------------- up to the end icon
            const float x0 = tp.x + ImGui::CalcTextSize(title.c_str()).x + 4.0f * s, x1 = p.x + w - h - 2.0f * s;
            ImFont* f = ImGui::GetFont();
            const float cw = f->CalcTextSizeA(f->FontSize, FLT_MAX, 0.0f, "\xe2\x94\x80").x;
            std::string run;
            for (float x = x0; cw > 0.0f && x + cw <= x1; x += cw) run += "\xe2\x94\x80";
            dl->AddText(ImVec2(x0, tp.y), theme::u32(theme::accent, 0.55f), run.c_str());
        }
    }
    ImGui::LogText("%s", tr(def->name));
    if (fontBold_) ImGui::PopFont();
    ImGui::SameLine();
    const bool endClicked = ImGui::InvisibleButton(inTray ? "##collapse" : "##close", ImVec2(h, h));
    const bool endHovered = ImGui::IsItemHovered();
    centredText(dl, nullptr, ImVec2(p.x + w - h, p.y), ImVec2(p.x + w, p.y + h),
                theme::u32(endHovered ? theme::textHover : theme::textDim),
                inTray ? (collapsed ? ICON_LC_CHEVRON_RIGHT : ICON_LC_CHEVRON_DOWN) : ICON_LC_X);
    ImGui::PopStyleVar();
    ImGui::Dummy(ImVec2(0, 2.0f * s));

    if (iconClicked) {
        if (inTray) {
            removePaletteFromTrays(id);
            noteLayoutChanged();
            return false;
        }
        movePaletteToTray(id, (SDL_GetModState() & SDL_KMOD_SHIFT) == 0);
        return false;
    }
    if (inTray && (titleClicked || endClicked)) {
        if (collapsed) {
            collapsedPalettes_.erase(std::remove(collapsedPalettes_.begin(), collapsedPalettes_.end(), id), collapsedPalettes_.end());
            autoCollapseTray(right, id);
        } else {
            collapsedPalettes_.push_back(id);
        }
        return collapsed;
    }
    if (!inTray && endClicked) {
        dropdown_.clear();
        ImGui::CloseCurrentPopup();
        return false;
    }
    return !collapsed;
}

void App::drawPaletteBody(const std::string& id) {
    const PaletteDef* def = findPalette(id);
    if (!def) return;
    ImGui::PushID(def->id);
    (this->*(def->draw))();
    ImGui::PopID();
}

void App::drawPaletteDropdown() {
    if (openDropdown_) {
        ImGui::OpenPopup("##palettedropdown");
        openDropdown_ = false;
    }
    if (dropdown_.empty()) return;
    const PaletteDef* def = findPalette(dropdown_);
    if (!def) {
        dropdown_.clear();
        return;
    }
    const float s = uiScale_;
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float w = 300.0f * s;
    // Below the top shelf, not over it: the palette drops past the tool rows onto the canvas, so Draw | Move |
    // Sculpt and the sliders stay visible and usable while it is open (Tab hides the shelves: then under the bar).
    const CanvasRect& top = shell_.topShelf;
    const float y = top.h > 0.0f ? top.y + top.h + 2.0f * s : shell_.paletteBar.y + shell_.paletteBar.h;
    ImGui::SetNextWindowPos(ImVec2(std::clamp(dropdownX_, 4.0f * s, vp->Size.x - w - 4.0f * s), y));
    ImGui::SetNextWindowSizeConstraints(ImVec2(w, 0), ImVec2(w, vp->Size.y - y - 8.0f * s));
    // Whole-pixel padding: auto-fit truncates sizes, and a fraction of a pixel too little shows a scrollbar.
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(std::round(6.0f * s), std::round(6.0f * s)));
    const bool open = ImGui::BeginPopup("##palettedropdown");
    ImGui::PopStyleVar();
    if (!open) {
        dropdown_.clear();
        return;
    }
    Demo::pushScope(def->name);
    if (def->scope) Demo::pushScope(def->scope);
    const std::string id = dropdown_;
    if (paletteHeader(id.c_str(), false, true)) drawPaletteBody(id);
    if (def->scope) Demo::popScope();
    Demo::popScope();
    if (ImGui::IsKeyPressed(ImGuiKey_Escape) && !ImGui::IsAnyItemActive()) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

void App::drawTray(bool right, const CanvasRect& r) {
    const float s = uiScale_;
    std::vector<std::string>& list = right ? layout_.rightTray : layout_.leftTray;
    if (!beginShellWindow(right ? "##RightTray" : "##LeftTray", r, theme::panelBg, ImVec2(6.0f * s, 6.0f * s), 0)) {
        ImGui::End();
        return;
    }
    const std::vector<std::string> ids = list; // headers may move palettes out
    for (const std::string& id : ids) {
        const PaletteDef* def = findPalette(id);
        if (!def) continue;
        ImGui::PushID(id.c_str());
        Demo::pushScope(def->name);
        if (def->scope) Demo::pushScope(def->scope);
        const ImVec2 top = ImGui::GetCursorScreenPos();
        if (flashPalette_ == id) {
            const double t = ImGui::GetTime() - flashStart_;
            if (t < 0.05) ImGui::SetScrollHereY(0.0f);
            if (t < 0.9) {
                const float a = float(0.45 * (1.0 - t / 0.9));
                ImGui::GetWindowDrawList()->AddRectFilled(top, ImVec2(top.x + ImGui::GetContentRegionAvail().x, top.y + 24.0f * s),
                                                          theme::u32(theme::accent, a), theme::rounding(3.0f * s));
            } else {
                flashPalette_.clear();
            }
        }
        if (paletteHeader(id.c_str(), true, right)) {
            drawPaletteBody(id);
            ImGui::Dummy(ImVec2(0, 4.0f * s));
        }
        if (def->scope) Demo::popScope();
        Demo::popScope();
        ImGui::PopID();
    }
    if (list.empty()) {
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        const char* hint = "Click a palette's " ICON_LC_PANEL_RIGHT " icon\nor drag it here";
        const ImVec2 ts = ImGui::CalcTextSize(hint);
        ImGui::SetCursorPos(ImVec2((avail.x - ts.x) * 0.5f + 6.0f * s, std::max(ImGui::GetCursorPosY(), avail.y * 0.4f)));
        ImGui::TextDisabled("%s", hint);
    }
    // The whole tray accepts palettes dragged by their header icon.
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (ImGui::BeginDragDropTargetCustom(window->Rect(), ImGui::GetID("##traydrop"))) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SF_PALETTE"))
            movePaletteToTray(static_cast<const char*>(payload->Data), right);
        ImGui::EndDragDropTarget();
    }
    ImGui::End();
}

void App::drawDivider(bool right, const CanvasRect& r) {
    if (r.w <= 0.0f) return;
    const float s = uiScale_;
    if (!beginShellWindow(right ? "##RightDivider" : "##LeftDivider", r, theme::shellBg, ImVec2(0, 0))) {
        ImGui::End();
        return;
    }
    const bool open = right ? layout_.rightTrayOpen && !shell_.narrow : layout_.leftTrayOpen;
    ImGui::InvisibleButton(right ? "##rightdivider" : "##leftdivider", ImVec2(r.w, r.h));
    const bool hovered = ImGui::IsItemHovered(), active = ImGui::IsItemActive();
    ImGuiIO& io = ImGui::GetIO();
    if ((hovered || active) && open) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    if (active && open && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 3.0f * s) && io.MouseDelta.x != 0.0f) {
        float& width = right ? layout_.rightTrayWidth : layout_.leftTrayWidth;
        const float logicalW = io.DisplaySize.x / s;
        width = std::clamp(trayWidth(width, logicalW) + (right ? -io.MouseDelta.x : io.MouseDelta.x) / s, shell::kTrayMinW,
                           shell::kTrayMaxW);
        if (width == 300.0f) width = 300.5f; // a deliberate 300 isn't the "grow on wide screens" default
        noteLayoutChanged();
    }
    if (ImGui::IsItemDeactivated() && io.MouseDragMaxDistanceSqr[0] < 9.0f * s * s) {
        if (right && shell_.narrow) {
            status_ = "The window is too narrow for the tray - palettes open as drop-downs";
        } else {
            (right ? layout_.rightTrayOpen : layout_.leftTrayOpen) = !open;
            noteLayoutChanged();
        }
    }
    ui::HelpText(right ? "Right tray" : "Left tray", nullptr,
                 "Click to open or close the tray, drag to resize it. Palettes go in trays from their header icon.",
                 "Shell");
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float cx = std::floor(r.x + r.w * 0.5f), cy = r.y + r.h * 0.5f;
    const ImU32 grip = theme::u32(hovered || active ? theme::accent : theme::textDim, hovered || active ? 1.0f : 0.5f);
    dl->AddRectFilled(ImVec2(cx - 1.0f * s, cy - 16.0f * s), ImVec2(cx + 1.0f * s, cy + 16.0f * s), grip, theme::rounding(1.0f * s));
    ImGui::End();
}

// ---------------------------------------------------------------------------
// Shelves
// ---------------------------------------------------------------------------

void App::drawShelf(int shelf, const CanvasRect& r) {
    const float s = uiScale_;
    const bool vertical = shelf == kShelfLeft || shelf == kShelfRight;
    const ImVec2 pad = shelf == kShelfTop      ? ImVec2(6.0f * s, 4.0f * s)
                       : shelf == kShelfBottom ? ImVec2(6.0f * s, 3.0f * s)
                       : shelf == kShelfLeft   ? ImVec2(6.0f * s, 6.0f * s)
                                               : ImVec2(4.0f * s, 6.0f * s);
    if (!beginShellWindow(kShelfWindows[shelf], r, theme::shellBg, pad,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse, false)) {
        ImGui::End();
        return;
    }
    std::vector<std::string>& items = shelf == kShelfTop    ? layout_.topShelf
                                      : shelf == kShelfLeft  ? layout_.leftShelf
                                      : shelf == kShelfRight ? layout_.rightShelf
                                                             : layout_.bottomShelf;
    const std::vector<std::string> list = items; // items may be removed from their own context menu
    std::vector<ImRect> rects;
    std::vector<int> indices;
    if (shelf == kShelfBottom) {
        const int tiles = int(std::count_if(list.begin(), list.end(), [](const std::string& id) { return id != "sep" && id != "row"; }));
        const float avail = r.w - 2.0f * pad.x - 48.0f * s; // the [+] tile is narrower
        const shell::BottomShelfSize size = shell::bottomShelfSize(layout_.shelfTiles);
        bottomTileW_ = std::clamp(avail / float(std::max(tiles - 1, 1)) - 4.0f * s, size.tileMinW * s, size.tileMaxW * s);
    }
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f * s, 4.0f * s));
    bool lineStart = true;
    int prevShown = -1; // the last item drawn on this line (contextual items may be skipped)
    for (int i = 0; i < int(list.size()); ++i) {
        const std::string& item = list[size_t(i)];
        if (item == "row") {
            lineStart = true;
            continue;
        }
        if (!shelfItemShown(item)) continue;
        if (!vertical && !lineStart && prevShown >= 0) {
            const std::string& prev = list[size_t(prevShown)];
            ImGui::SameLine(0.0f, prev == "sep" || item == "sep" ? 0.0f : segmentPair(prev, item) ? 1.0f * s : 4.0f * s);
        }
        if (!vertical && item == "widget.points") { // right-aligned readout
            const float w = kPointsReadoutW * s;
            ImGui::SameLine(std::max(ImGui::GetCursorPosX(), ImGui::GetWindowContentRegionMax().x - w));
        }
        lineStart = false;
        prevShown = i;
        drawShelfItem(shelf, i, item, vertical);
        rects.push_back(ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax()));
        indices.push_back(i);
    }
    ImGui::PopStyleVar();

    ImGuiWindow* window = ImGui::GetCurrentWindow();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    // Bottom shelf: the wheel scrolls it sideways when it doesn't fit.
    if (shelf == kShelfBottom && ImGui::IsWindowHovered() && ImGui::GetIO().MouseWheel != 0.0f)
        ImGui::SetScrollX(ImGui::GetScrollX() - ImGui::GetIO().MouseWheel * 60.0f * s);
    if (layout_.customize)
        dashedRect(dl, ImVec2(r.x + 2.0f, r.y + 2.0f), ImVec2(r.x + r.w - 2.0f, r.y + r.h - 2.0f), theme::u32(theme::accent, 0.6f),
                   4.0f * s);
    // Customize: drop items between the shelf's items (an accent line shows where).
    const ImGuiPayload* payload = ImGui::GetDragDropPayload();
    if (payload && payload->IsDataType(ui::kUiItemPayload) && window->Rect().Contains(ImGui::GetIO().MousePos)) {
        const ImVec2 m = ImGui::GetIO().MousePos;
        int insert = int(list.size());
        ImVec2 a, b;
        for (size_t k = 0; k < rects.size(); ++k) {
            const ImRect& rc = rects[k];
            const bool before = vertical ? m.y < rc.GetCenter().y : (m.y < rc.Max.y + 2.0f * s && m.x < rc.GetCenter().x);
            if (before) {
                insert = indices[k];
                a = vertical ? ImVec2(rc.Min.x, rc.Min.y - 2.0f * s) : ImVec2(rc.Min.x - 2.0f * s, rc.Min.y);
                b = vertical ? ImVec2(rc.Max.x, rc.Min.y - 2.0f * s) : ImVec2(rc.Min.x - 2.0f * s, rc.Max.y);
                break;
            }
        }
        if (insert == int(list.size()) && !rects.empty()) {
            const ImRect& rc = rects.back();
            a = vertical ? ImVec2(rc.Min.x, rc.Max.y + 2.0f * s) : ImVec2(rc.Max.x + 2.0f * s, rc.Min.y);
            b = vertical ? ImVec2(rc.Max.x, rc.Max.y + 2.0f * s) : ImVec2(rc.Max.x + 2.0f * s, rc.Max.y);
        }
        if (ImGui::BeginDragDropTargetCustom(window->Rect(), ImGui::GetID("##shelfdrop"))) {
            ImGui::GetForegroundDrawList()->AddLine(a, b, theme::u32(theme::accent), 2.0f * s);
            if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload(ui::kUiItemPayload, ImGuiDragDropFlags_AcceptNoDrawDefaultRect)) {
                ui::UiItemPayload item;
                std::memcpy(&item, p->Data, std::min(size_t(p->DataSize), sizeof item));
                item.id[sizeof item.id - 1] = 0;
                std::vector<std::string>* from = item.fromShelf == kShelfTop      ? &layout_.topShelf
                                                 : item.fromShelf == kShelfLeft   ? &layout_.leftShelf
                                                 : item.fromShelf == kShelfRight  ? &layout_.rightShelf
                                                 : item.fromShelf == kShelfBottom ? &layout_.bottomShelf
                                                                                  : nullptr;
                if (from && item.fromIndex >= 0 && item.fromIndex < int(from->size()) && (*from)[size_t(item.fromIndex)] == item.id) {
                    from->erase(from->begin() + item.fromIndex);
                    if (from == &items && item.fromIndex < insert) --insert;
                }
                items.insert(items.begin() + std::clamp(insert, 0, int(items.size())), std::string(item.id));
                shelfDrag_.accepted = true;
                noteLayoutChanged();
            }
            ImGui::EndDragDropTarget();
        }
    }
    // The frame last, over the shelf's buttons: drawn first, the right shelf's tall buttons covered its sides and
    // the accent outline showed in pieces (#98).
    tileFrame(dl, r, sameRect(r, g_focus));
    ImGui::End();
}

// Contextual shelf items (docs/ui-design.md, "Contextual controls"): shown only when they apply, and always in
// Customize mode so they can still be moved or removed.
bool App::shelfItemShown(const std::string& item) const {
    if (layout_.customize) return true;
    if (item == "view.splats") return !doc_.splats.empty(); // only when there's a capture
    if (item == "draw.paint_vertex" || item == "draw.paint_texture") return hasTexture(doc_.mesh); // paint target
    if (item == "draw.new_texture") return !hasTexture(doc_.mesh) && (paintMode_ || paintRgb_ || autoUv_); // start one
    if (item == "widget.dyntopo_detail") return doc_.sculptor.dyntopo.enabled; // with Dynamic on
    return true;
}

void App::drawShelfItem(int shelf, int index, const std::string& item, bool vertical) {
    const float s = uiScale_;
    const float winW = ImGui::GetWindowWidth();
    const float padX = ImGui::GetStyle().WindowPadding.x;
    ImGui::PushID(index);
    auto centre = [&](float w) {
        if (vertical) ImGui::SetCursorPosX(std::max(padX, (winW - w) * 0.5f));
    };
    const float rowH = 26.0f * s;
    if (item == "sep") {
        if (vertical) {
            ImGui::Dummy(ImVec2(winW - 2.0f * padX, 4.0f * s));
            const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
            ImGui::GetWindowDrawList()->AddLine(ImVec2(a.x + 6.0f * s, (a.y + b.y) * 0.5f), ImVec2(b.x - 6.0f * s, (a.y + b.y) * 0.5f),
                                                theme::u32(theme::header));
        } else {
            ImGui::Dummy(ImVec2(13.0f * s, rowH));
            const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
            const float x = std::floor((a.x + b.x) * 0.5f);
            ImGui::GetWindowDrawList()->AddLine(ImVec2(x, a.y + 4.0f * s), ImVec2(x, b.y - 4.0f * s), theme::u32(theme::header));
        }
    } else if (item == "widget.draw_size" || item == "widget.strength") {
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f * s, (rowH - ImGui::GetFontSize()) * 0.5f));
        // 180 px each, narrower when the row is full (e.g. with the contextual Vertex / Texture buttons at 1600 px).
        const float avail = ImGui::GetContentRegionAvail().x - (item == "widget.draw_size" ? 4.0f * s : 0.0f);
        ImGui::SetNextItemWidth(vertical ? 180.0f * s
                                         : std::clamp(item == "widget.draw_size" ? avail * 0.5f : avail, 120.0f * s, 180.0f * s));
        if (item == "widget.draw_size") {
            drawSizeSlider("##drawsize", "Draw",
                           "Brush radius in screen pixels (0.5 to 800), and in mm on the surface under the cursor. [ and ] "
                           "change it by 10%, Shift+[ and Shift+] by 1 px, Ctrl+Shift+right-drag scrubs it finely; "
                           "Ctrl+click to type.");
        } else {
            drawZIntensitySlider("##strength");
        }
        ImGui::PopStyleVar();
    } else if (item == "widget.dyntopo_detail") {
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f * s, (rowH - ImGui::GetFontSize()) * 0.5f));
        ImGui::SetNextItemWidth((vertical ? 180.0f : 120.0f) * s);
        drawDetailSlider("##dyntopo_detail");
        ImGui::PopStyleVar();
    } else if (item == "widget.points") {
        size_t total = 0;
        uint64_t key = doc_.undo.revision() * 0x9E3779B97F4A7C15ull ^ uint64_t(doc_.activeSubtool) ^
                       (uint64_t(reinterpret_cast<uintptr_t>(doc_.mesh.hidden.data())) << 1);
        for (int i = 0; i < doc_.subtoolCount(); ++i) {
            total += doc_.subtoolMesh(i).triangleCount();
            key = key * 31 + doc_.subtoolMesh(i).triangleCount() * 2 + (doc_.subtools[size_t(i)].visible ? 1 : 0);
        }
        if (key != activeTrisKey_) { // hidden triangles only change through commands and undo
            activeTrisKey_ = key;
            activeTris_ = 0;
            for (int i = 0; i < doc_.subtoolCount(); ++i) {
                if (!doc_.subtools[size_t(i)].visible) continue;
                const Mesh& m = doc_.subtoolMesh(i);
                const size_t hidden = size_t(std::count_if(m.hidden.begin(), m.hidden.end(), [](uint8_t h) { return h != 0; }));
                activeTris_ += m.triangleCount() - std::min(hidden, m.triangleCount());
            }
        }
        const size_t active = activeTris_;
        // One line on the row's centre line, right-aligned to the shelf edge: "ActiveTris 20.5k   TotalTris 20.5k".
        ImGui::BeginGroup();
        if (fontSmall_) ImGui::PushFont(fontSmall_);
        const ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::Dummy(ImVec2(kPointsReadoutW * s, rowH));
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const std::string a = shortCount(active), t = shortCount(total);
        const float gap = 5.0f * s, groupGap = 14.0f * s;
        const float w = ImGui::CalcTextSize("ActiveTris").x + ImGui::CalcTextSize(a.c_str()).x + ImGui::CalcTextSize("TotalTris").x +
                        ImGui::CalcTextSize(t.c_str()).x + 2.0f * gap + groupGap;
        float x = std::floor(p.x + kPointsReadoutW * s - w);
        const float y = std::floor(p.y + (rowH - ImGui::GetTextLineHeight()) * 0.5f);
        auto put = [&](const char* text, const ImVec4& col, float after) {
            dl->AddText(ImVec2(x, y), theme::u32(col), text);
            x += ImGui::CalcTextSize(text).x + after;
        };
        put("ActiveTris", theme::textDim, gap);
        put(a.c_str(), theme::text, groupGap);
        put("TotalTris", theme::textDim, gap);
        put(t.c_str(), theme::text, 0.0f);
        ImGui::LogText("ActiveTris %s TotalTris %s", a.c_str(), t.c_str());
        if (fontSmall_) ImGui::PopFont();
        ImGui::EndGroup();
        ui::HelpText("ActiveTris / TotalTris", nullptr,
                     "Triangles you can see and sculpt (visible Parts, hidden parts left out) and in the whole scene.",
                     "Draw");
    } else if (item.rfind("widget.tile.", 0) == 0) {
        const float w = (shell_.narrow ? 60.0f : 76.0f) * s;
        const ImVec2 size(w, w + (fontSmall_ ? fontSmall_->FontSize : 12.0f) + 4.0f * s);
        centre(w);
        if (item == "widget.tile.brush") drawBrushTile("##tile.brush", size);
        else if (item == "widget.tile.stroke") drawStrokeTile("##tile.stroke", size);
        else if (item == "widget.tile.alpha") drawAlphaTile("##tile.alpha", size);
        else if (item == "widget.tile.material") drawMaterialTile("##tile.material", size);
        else ImGui::Dummy(size);
    } else if (item == "widget.color") {
        const float w = (shell_.narrow ? 60.0f : 76.0f) * s;
        centre(w);
        drawColorWidget(w);
    } else if (item == "widget.nav.move" || item == "widget.nav.zoom" || item == "widget.nav.rotate") {
        centre(44.0f * s);
        const int mode = item == "widget.nav.move" ? 0 : item == "widget.nav.zoom" ? 1 : 2;
        canvasNavButton(mode == 0 ? "nav.move" : mode == 1 ? "nav.zoom" : "nav.rotate",
                        mode == 0 ? ICON_LC_HAND : mode == 1 ? ICON_LC_ZOOM_IN : ICON_LC_ROTATE_3D,
                        mode == 0 ? "Move" : mode == 1 ? "Zoom" : "Rotate", mode);
    } else if (item == "widget.add_brush") {
        const ImVec2 size(48.0f * s, shell::bottomShelfSize(layout_.shelfTiles).tileH * s);
        if (ImGui::InvisibleButton("##addbrush", size)) // the brush picker in "add to shelf" mode (Pickers.cpp)
            openBrushPickerForShelf(ImVec2(ImGui::GetItemRectMin().x, ImGui::GetItemRectMin().y - 8.0f * s));
        const bool hovered = ImGui::IsItemHovered();
        const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float r = 15.0f * s;
        const ImVec2 c((a.x + b.x) * 0.5f, a.y + 4.0f * s + r);
        dl->AddCircle(c, r, theme::u32(hovered ? theme::accent : theme::textDim, hovered ? 1.0f : 0.6f), 32, 1.5f * s);
        centredText(dl, fontIcons_, ImVec2(c.x - r, c.y - r), ImVec2(c.x + r, c.y + r),
                    theme::u32(hovered ? theme::accent : theme::textDim), ICON_LC_PLUS);
        ui::HelpText("Add a brush", nullptr, "Put another brush (built-in or one of yours) on this shelf.", "Bottom shelf");
    } else if (item.rfind("brush.recipe.", 0) == 0 || item.rfind("recipe:", 0) == 0) {
        const std::string name = item.substr(item[0] == 'r' ? 7 : 13); // custom brush, by name
        int found = -1;
        for (int i = 0; i < int(doc_.brushes.size()); ++i)
            if (doc_.brushes[size_t(i)].name == name) found = i;
        const ImVec2 size(bottomTileW_, shell::bottomShelfSize(layout_.shelfTiles).tileH * s);
        const bool clicked = ImGui::InvisibleButton((name + "##" + item).c_str(), size);
        const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const bool on = found >= 0 && doc_.activeBrush == found && !paintMode_;
        if (ImGui::IsItemHovered()) dl->AddRectFilled(a, b, theme::u32(theme::buttonHover, 0.55f), theme::rounding(4.0f * s));
        const float capH = (fontSmall_ ? fontSmall_->FontSize : 12.0f) + 2.0f * s;
        const float pic = size.y - capH;
        ImTextureID tex = found >= 0 ? thumbs_.recipe(doc_.brushes[size_t(found)]) : ImTextureID(0);
        const ImVec2 c((a.x + b.x) * 0.5f, a.y + pic * 0.5f);
        if (tex) dl->AddImageRounded(tex, ImVec2(c.x - pic * 0.5f, a.y), ImVec2(c.x + pic * 0.5f, a.y + pic), ImVec2(0, 0), ImVec2(1, 1),
                                     IM_COL32_WHITE, theme::rounding(4.0f * s));
        else drawBrushIcon(dl, c, pic * 0.36f, int(BrushType::Standard));
        dl->AddText(fontSmall_, fontSmall_ ? fontSmall_->FontSize : 12.0f, ImVec2(a.x + 2.0f * s, a.y), theme::u32(theme::accent),
                    ICON_LC_SPARKLE);
        if (on) dl->AddRect(a, b, theme::u32(theme::accent), theme::rounding(4.0f * s), 0, 2.0f * s);
        centredText(dl, fontSmall_, ImVec2(a.x, b.y - capH), b, theme::u32(found < 0 ? theme::textDim : on ? theme::accent : theme::text),
                    elide(fontSmall_ ? fontSmall_ : ImGui::GetFont(), name, size.x).c_str());
        if (clicked && found >= 0) {
            doc_.activeBrush = found;
            paintMode_ = false;
        }
        ui::HelpText(name.c_str(), nullptr,
                     found >= 0 && !doc_.brushes[size_t(found)].description.empty() ? doc_.brushes[size_t(found)].description.c_str()
                                                                                   : "One of your custom brushes.",
                     "Bottom shelf");
    } else if (const Action* a = actions_.find(item == "brush.paint" ? std::string("draw.paint") : item)) {
        const ui::Look look = shelf != kShelfTop ? ui::Look::Tile : a->icon ? ui::Look::IconLabel : ui::Look::Label;
        const ImVec2 size = shelf == kShelfTop      ? ImVec2(0.0f, rowH)
                            : shelf == kShelfRight  ? ImVec2(44.0f * s, 40.0f * s)
                            : shelf == kShelfBottom ? ImVec2(bottomTileW_, shell::bottomShelfSize(layout_.shelfTiles).tileH * s)
                                                    : ImVec2(winW - 2.0f * padX, 60.0f * s);
        centre(size.x);
        // Top shelf hierarchy, by meaning (docs/ui-design.md §10). Every item is the full row height in the same
        // font, so both rows share one baseline. Mode groups (Draw | Move | Scale | Rotate, Sculpt | Paint) and
        // choices (Zadd | Zsub) are segments a step above the shelf: the current mode in the accent, the chosen
        // side of a choice a raised grey. Options (X, Rgb, Pressure...) are separate dark chips with a readable
        // label, raised with an accent underline when on. The rest (LightBox, AI Brush) are plain buttons.
        const bool segment = ui::IsModeAction(a->id) || ui::IsChoiceAction(a->id);
        const bool option = !segment && a->checked && a->id.rfind("ui.", 0) != 0;
        int colors = 0;
        if (shelf == kShelfTop) {
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2((option ? 8.0f : 10.0f) * s, (rowH - ImGui::GetFontSize()) * 0.5f));
            if (option) {
                ImGui::PushStyleColor(ImGuiCol_Button, theme::toggleOff);
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, theme::toggleOffHover);
                ImGui::PushStyleColor(ImGuiCol_Text, theme::textOff);
                colors = 3;
            } else if (segment) {
                ImGui::PushStyleColor(ImGuiCol_Button, theme::trough);
                colors = 1;
            }
        }
        ui::SetNextItemOrigin(shelf, index); // ActionButton is the Ctrl+Alt drag source, from this shelf slot
        ui::ActionButton(*this, a->id.c_str(), size, look);
        if (shelf == kShelfTop) {
            ImGui::PopStyleColor(colors);
            ImGui::PopStyleVar();
        }
    } else {
        ImGui::Dummy(ImVec2(0, 0)); // an id this version doesn't know: keep its place in the list
    }
    if (item != "sep" && ImGui::BeginPopupContextItem("##shelfitem")) {
        if (ImGui::MenuItem(tr("Remove from shelf"))) {
            auto& list = shelf == kShelfTop    ? layout_.topShelf
                         : shelf == kShelfLeft  ? layout_.leftShelf
                         : shelf == kShelfRight ? layout_.rightShelf
                                                : layout_.bottomShelf;
            if (index < int(list.size()) && list[size_t(index)] == item) list.erase(list.begin() + index);
            noteLayoutChanged();
        }
        if (shelf == kShelfBottom && ImGui::BeginMenu(tr("Tile size"))) { // also in Preferences > Interface
            static const char* kTiles[] = {"Small", "Medium", "Large"};
            for (int i = 0; i < 3; ++i)
                if (ImGui::MenuItem(tr(kTiles[i]), nullptr, layout_.shelfTiles == i)) layout_.shelfTiles = i;
            ImGui::EndMenu();
        }
        ImGui::EndPopup();
    }
    // Customize mode: widgets (sliders, tiles, readouts) are Ctrl+Alt drag sources too.
    if (item != "sep" && !actions_.find(item)) {
        ui::SetNextItemOrigin(shelf, index);
        ui::UiItemDragSource(*this, item.c_str(), item.c_str());
    }
    ImGui::PopID();
}

void App::setDynamic(bool on) {
    if (on) {
        const std::string why = dyntopoBlocker(doc_.mesh);
        if (!why.empty()) {
            status_ = why;
            return;
        }
    }
    doc_.sculptor.dyntopo.enabled = on;
    status_ = dyntopoSummary(doc_.sculptor.dyntopo);
}

void App::drawDetailSlider(const char* id) {
    float detail = doc_.sculptor.dyntopo.detail * 100.0f;
    if (ui::SliderInside(id, &detail, 0.0f, 100.0f, "Detail %.0f")) doc_.sculptor.dyntopo.detail = detail / 100.0f;
    ui::HelpText("Detail", nullptr,
                 doc_.sculptor.dyntopo.constantDetail
                     ? "Dynamic topology: how small the triangles get, one size for the whole mesh (0 coarse, 100 fine; "
                       "Constant detail is on in the Draw palette). Ctrl+click to type."
                     : "Dynamic topology: how small the triangles get, relative to the brush (0 coarse, 100 fine). Zoom "
                       "in or shrink the brush for finer triangles, as in Dynamic Topology. Ctrl+click to type.",
                 "Draw");
}

void App::finishShelfDrag() {
    const ImGuiPayload* payload = ImGui::GetDragDropPayload();
    if (payload && payload->IsDataType(ui::kUiItemPayload)) {
        ui::UiItemPayload item;
        std::memcpy(&item, payload->Data, std::min(size_t(payload->DataSize), sizeof item));
        shelfDrag_.active = true;
        shelfDrag_.shelf = item.fromShelf;
        shelfDrag_.index = item.fromIndex;
        return;
    }
    if (!shelfDrag_.active) return;
    // The drag ended: dropped outside every shelf, an item that came from a shelf is removed (ZBrush).
    if (!shelfDrag_.accepted && shelfDrag_.shelf >= 0 && shelfDrag_.shelf < kShelfCount) {
        auto& list = shelfDrag_.shelf == kShelfTop    ? layout_.topShelf
                     : shelfDrag_.shelf == kShelfLeft  ? layout_.leftShelf
                     : shelfDrag_.shelf == kShelfRight ? layout_.rightShelf
                                                       : layout_.bottomShelf;
        if (shelfDrag_.index >= 0 && shelfDrag_.index < int(list.size())) {
            status_ = "Removed " + list[size_t(shelfDrag_.index)] + " from the shelf";
            list.erase(list.begin() + shelfDrag_.index);
            noteLayoutChanged();
        }
    }
    shelfDrag_ = {};
}

// Right-shelf Move / Zoom / Rotate: drag on the button to navigate, as in ZBrush.
bool App::canvasNavButton(const char* id, const char* icon, const char* englishLabel, int mode) {
    const char* label = tr(englishLabel);
    const float s = uiScale_;
    const ImVec2 size(44.0f * s, 40.0f * s);
    ImGui::InvisibleButton((std::string(label) + "##" + id).c_str(), size);
    const bool hovered = ImGui::IsItemHovered(), active = ImGui::IsItemActive();
    if (active) {
        // The cursor's own steps (the pen's, or the mouse's), not ImGui's MouseDelta: the press itself could carry a
        // jump from wherever ImGui last saw the cursor, which threw the model off the canvas with a pen (#13).
        const ImVec2 now(mouseX_, mouseY_);
        const ImVec2 d = ImGui::IsItemActivated() ? ImVec2(0, 0) : ImVec2(now.x - navLast_.x, now.y - navLast_.y);
        navLast_ = now;
        const CanvasRect c = shell_.canvas;
        if (d.x != 0.0f || d.y != 0.0f) {
            if (mode == 0) camera_.pan(d.x, d.y, c.h);
            else if (mode == 1) camera_.zoom(-d.y * 0.05f);
            else camera_.orbit(d.x, d.y);
        }
        ImGui::SetMouseCursor(mode == 0 ? ImGuiMouseCursor_ResizeAll : mode == 1 ? ImGuiMouseCursor_ResizeNS : ImGuiMouseCursor_Hand);
        navDrag_ = mode;
    } else if (navDrag_ == mode) {
        navDrag_ = -1;
    }
    const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(a, b, theme::u32(active ? theme::accent : hovered ? theme::buttonHover : theme::frame), theme::rounding(4.0f * s));
    const float capH = (fontTiny_ ? fontTiny_->FontSize : 10.0f) + 2.0f * s;
    const ImU32 fg = theme::u32(active ? theme::textOnAccent : theme::text);
    centredText(dl, fontIcons_, a, ImVec2(b.x, b.y - capH + 2.0f * s), fg, icon);
    centredText(dl, fontTiny_, ImVec2(a.x, b.y - capH), ImVec2(b.x, b.y - 1.0f * s), fg, label);
    ui::HelpAction(*this, id);
    return active;
}

// ---------------------------------------------------------------------------
// Left shelf tiles
// ---------------------------------------------------------------------------

namespace {

// Tile frame: the picture square and the caption under it. Returns true when clicked.
// A left-shelf tile; it lifts on hover (`lift` ends when the caller has drawn the picture).
bool tileFrame(const char* id, ImVec2 size, ui::TileLift& lift, ImFont* font, const std::string& caption, float s, ImVec2& pic0,
               ImVec2& pic1) {
    const bool clicked = ImGui::InvisibleButton(id, size);
    const bool hovered = ImGui::IsItemHovered();
    const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float capH = (font ? font->FontSize : 12.0f) + 4.0f * s;
    pic0 = a;
    pic1 = ImVec2(b.x, b.y - capH);
    dl->AddRectFilled(pic0, pic1, theme::u32(theme::tile), theme::rounding(4.0f * s));
    if (hovered) dl->AddRectFilled(pic0, pic1, theme::u32(theme::buttonHover, 0.45f), theme::rounding(4.0f * s)); // (a faint fill, no border)
    centredText(dl, font, ImVec2(a.x, b.y - capH), b, theme::u32(hovered ? theme::text : theme::textDim),
                elide(font ? font : ImGui::GetFont(), caption, size.x).c_str());
    lift = ui::BeginTileLift(pic0, pic1); // the picture lifts, the caption stays
    dl->AddRectFilled(pic0, pic1, theme::u32(theme::tile), theme::rounding(4.0f * s));
    if (hovered) dl->AddRectFilled(pic0, pic1, theme::u32(theme::buttonHover, 0.45f), theme::rounding(4.0f * s)); // (a faint fill)
    ImGui::LogText("%s", caption.c_str());
    return clicked;
}

} // namespace

void App::drawHeldBrushIcon(ImDrawList* dl, ImVec2 a, ImVec2 b, const char* icon, bool select) {
    const ImVec2 c((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f);
    dl->AddCircleFilled(c, (b.x - a.x) * 0.36f, select ? IM_COL32(40, 90, 70, 255) : IM_COL32(90, 40, 40, 255), 32);
    centredText(dl, fontIcons_, a, b, IM_COL32(255, 255, 255, 230), icon);
}

void App::drawBrushTile(const char* id, ImVec2 size) {
    const float s = uiScale_;
    // ZBrush: Ctrl held shows the mask brush, Ctrl+Shift the selection brush; a click picks among those.
    if (const HeldBrushItem* held = heldBrushCurrent(heldBrush())) {
        ImVec2 a, b;
        ui::TileLiftScope lift;
        const bool clicked = tileFrame(id, size, lift.lift, fontSmall_, held->brush, s, a, b);
        drawHeldBrushIcon(ImGui::GetWindowDrawList(), a, b, held->icon, heldBrush() == HeldBrush::Select);
        if (clicked) openPicker(Picker::Brush, ImVec2(ImGui::GetItemRectMax().x + 4.0f * s, ImGui::GetItemRectMin().y));
        ui::HelpText("Brush", "B", "Ctrl held: the mask brush (Ctrl+Shift: the selection brush). Click, or press B with Ctrl "
                     "held, to pick Mask Pen, Mask Lasso or Mask Rectangle (Select Rectangle or Select Lasso).", "Left shelf");
        return;
    }
    const BrushRecipe* recipe = activeRecipe();
    const std::string name = guidesMode_ ? "Quad Remesh Guides" : paintMode_ ? "Paint" : recipe ? recipe->name : brushLabel(brush_);
    ImVec2 a, b;
    ui::TileLiftScope lift;
    const bool clicked = tileFrame(id, size, lift.lift, fontSmall_, name, s, a, b);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImTextureID tex = paintMode_ || guidesMode_ ? ImTextureID(0) : recipe ? thumbs_.recipe(*recipe) : thumbs_.brush(int(brush_));
    const ImVec2 c((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f);
    const float r = (b.x - a.x) * 0.36f;
    if (tex) {
        dl->AddImageRounded(tex, ImVec2(a.x + 2 * s, a.y + 2 * s), ImVec2(b.x - 2 * s, b.y - 2 * s), ImVec2(0, 0), ImVec2(1, 1),
                            IM_COL32_WHITE, theme::rounding(4.0f * s));
    } else if (guidesMode_) { // QRemesh Guides (PanelRetopo.cpp)
        dl->AddCircleFilled(c, r, theme::u32(theme::tile), 32);
        centredText(dl, fontIcons_, a, b, IM_COL32(255, 196, 64, 255), ICON_LC_SPLINE);
    } else if (paintMode_) {
        const glm::vec3 pc = linearToSrgb(doc_.paintColor);
        dl->AddCircleFilled(c, r, theme::u32(ImVec4(pc.x, pc.y, pc.z, 1.0f)), 32);
        centredText(dl, fontIcons_, a, b, IM_COL32(255, 255, 255, 220), ICON_LC_PAINTBRUSH);
    } else {
        drawBrushIcon(dl, c, r, recipe ? int(BrushType::Standard) : int(brush_));
        if (recipe) dl->AddText(fontSmall_, fontSmall_ ? fontSmall_->FontSize : 12.0f, ImVec2(a.x + 4 * s, a.y + 3 * s),
                                theme::u32(theme::accent), ICON_LC_SPARKLE);
    }
    if (clicked) openPicker(Picker::Brush, ImVec2(ImGui::GetItemRectMax().x + 4.0f * s, ImGui::GetItemRectMin().y));
    ui::HelpText("Brush", "B", "The active brush. Click for the brush picker; the bottom shelf holds favourites.", "Left shelf");
}

void App::drawStrokeIcon(ImDrawList* dl, ImVec2 a, ImVec2 b, StrokeType type) {
    const float s = uiScale_;
    const ImU32 col = IM_COL32(235, 235, 235, 230);
    const float w = b.x - a.x, h = b.y - a.y;
    auto at = [&](float u, float v) { return ImVec2(a.x + w * u, a.y + h * v); };
    switch (type) {
    case StrokeType::Dots: // ZBrush's dotted arc
        for (int i = 0; i < 7; ++i) {
            const float u = float(i) / 6.0f;
            dl->AddCircleFilled(at(0.18f + 0.64f * u, 0.30f + 0.42f * std::sin(u * 2.4f) * (1.0f - u * 0.35f)), 3.2f * s, col);
        }
        break;
    case StrokeType::DragRect:
        dl->AddRect(at(0.22f, 0.26f), at(0.78f, 0.74f), col, theme::rounding(2.0f * s), 0, 1.5f * s);
        dl->AddLine(at(0.3f, 0.66f), at(0.7f, 0.34f), col, 1.5f * s);
        dl->AddTriangleFilled(at(0.7f, 0.34f), at(0.6f, 0.36f), at(0.67f, 0.44f), col);
        break;
    case StrokeType::DragDot: // one dot slid along a path
        dl->AddCircleFilled(at(0.66f, 0.38f), 6.0f * s, col);
        dl->AddLine(at(0.24f, 0.70f), at(0.58f, 0.44f), IM_COL32(235, 235, 235, 120), 1.2f * s);
        dl->AddCircle(at(0.24f, 0.70f), 4.0f * s, IM_COL32(235, 235, 235, 120), 0, 1.2f * s);
        break;
    case StrokeType::Spray: {
        uint32_t seed = 7;
        for (int i = 0; i < 26; ++i) {
            seed = seed * 1664525u + 1013904223u;
            const float u = float((seed >> 8) & 0xffff) / 65535.0f;
            seed = seed * 1664525u + 1013904223u;
            const float v = float((seed >> 8) & 0xffff) / 65535.0f;
            seed = seed * 1664525u + 1013904223u;
            const float r = (1.0f + 2.5f * float((seed >> 8) & 0xff) / 255.0f) * s;
            dl->AddCircleFilled(at(0.2f + 0.6f * u, 0.22f + 0.56f * v), r, col);
        }
        break;
    }
    case StrokeType::LazyMouse: {
        ImVec2 pts[16];
        for (int i = 0; i < 16; ++i) {
            const float u = float(i) / 15.0f;
            pts[i] = at(0.16f + 0.62f * u, 0.62f - 0.3f * std::sin(u * 3.0f));
        }
        dl->AddPolyline(pts, 16, col, 0, 2.0f * s);
        dl->AddLine(pts[15], at(0.86f, 0.24f), IM_COL32(240, 136, 46, 220), 1.0f * s);
        dl->AddCircleFilled(at(0.86f, 0.24f), 3.0f * s, IM_COL32(240, 136, 46, 255));
        break;
    }
    default: break;
    }
}

void App::drawStrokeTile(const char* id, ImVec2 size) {
    const float s = uiScale_;
    if (const HeldBrushItem* held = heldBrushCurrent(heldBrush())) { // Ctrl / Ctrl+Shift: the mask's stroke
        ImVec2 a, b;
        ui::TileLiftScope lift;
        const bool clicked = tileFrame(id, size, lift.lift, fontSmall_, held->stroke, s, a, b);
        drawHeldBrushIcon(ImGui::GetWindowDrawList(), a, b, held->icon, heldBrush() == HeldBrush::Select);
        if (clicked) openPicker(Picker::Stroke, ImVec2(ImGui::GetItemRectMax().x + 4.0f * s, ImGui::GetItemRectMin().y));
        ui::HelpText("Stroke", nullptr, "Ctrl held: the mask's stroke, Dots (Mask Pen), Lasso or Rect (Ctrl+Shift: the "
                     "selection's). Click with Ctrl held to choose.", "Left shelf");
        return;
    }
    ImVec2 a, b;
    ui::TileLiftScope lift;
    const bool clicked = tileFrame(id, size, lift.lift, fontSmall_, strokeTypeName(strokeType_), s, a, b);
    drawStrokeIcon(ImGui::GetWindowDrawList(), a, b, strokeType_);
    if (clicked) openPicker(Picker::Stroke, ImVec2(ImGui::GetItemRectMax().x + 4.0f * s, ImGui::GetItemRectMin().y));
    ui::HelpText("Stroke", nullptr, "How dabs follow the cursor: Dots, Drag Stamp, Slide Stamp, Spray or Lazy Stroke. Click to choose.",
                 "Left shelf");
}

void App::drawAlphaTile(const char* id, ImVec2 size) {
    const float s = uiScale_;
    ImVec2 a, b;
    if (vdmBrushActive() && doc_.brushVdm) { // the VDM brush's 3D alpha (ZBrush shows VDMs in the alpha slot)
        ui::TileLiftScope lift;
        const bool clicked = tileFrame(id, size, lift.lift, fontSmall_, doc_.brushVdm->name, s, a, b);
        if (ImTextureID tex = thumbs_.vdm(doc_.brushVdm))
            ImGui::GetWindowDrawList()->AddImageRounded(tex, ImVec2(a.x + 2 * s, a.y + 2 * s), ImVec2(b.x - 2 * s, b.y - 2 * s),
                                                        ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, theme::rounding(4.0f * s));
        if (clicked) openPicker(Picker::Alpha, ImVec2(ImGui::GetItemRectMax().x + 4.0f * s, ImGui::GetItemRectMin().y));
        ui::HelpText("VDM", nullptr, "The VDM brush's vector displacement map. Click to pick another.", "Left shelf");
        return;
    }
    ui::TileLiftScope lift;
    const bool clicked = tileFrame(id, size, lift.lift, fontSmall_, doc_.brushAlpha.name, s, a, b);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImTextureID tex = thumbs_.alpha(doc_.brushAlpha);
    if (tex) {
        dl->AddImageRounded(tex, ImVec2(a.x + 2 * s, a.y + 2 * s), ImVec2(b.x - 2 * s, b.y - 2 * s), ImVec2(0, 0), ImVec2(1, 1),
                            IM_COL32_WHITE, theme::rounding(4.0f * s));
    } else {
        constexpr int kCells = 26;
        const float inset = 6.0f * s, cell = (b.x - a.x - 2.0f * inset) / kCells;
        dl->AddRectFilled(ImVec2(a.x + inset, a.y + inset), ImVec2(b.x - inset, b.y - inset), IM_COL32(0, 0, 0, 255), theme::rounding(3.0f * s));
        for (int y = 0; y < kCells; ++y)
            for (int x = 0; x < kCells; ++x) {
                const float u = (float(x) + 0.5f) / kCells * 2.0f - 1.0f, v = 1.0f - (float(y) + 0.5f) / kCells * 2.0f;
                const int g = int(std::clamp(doc_.brushAlpha.sample(u, v), 0.0f, 1.0f) * 255.0f);
                if (g < 4) continue;
                const ImVec2 p(a.x + inset + float(x) * cell, a.y + inset + float(y) * cell);
                dl->AddRectFilled(p, ImVec2(p.x + cell + 0.5f, p.y + cell + 0.5f), IM_COL32(g, g, g, 255));
            }
    }
    if (clicked) openPicker(Picker::Alpha, ImVec2(ImGui::GetItemRectMax().x + 4.0f * s, ImGui::GetItemRectMin().y));
    ui::HelpText("Alpha", nullptr, "The brush's stamp shape (white = full effect). Click for the alpha picker.", "Left shelf");
}

void App::drawMaterialTile(const char* id, ImVec2 size) {
    const float s = uiScale_;
    const int m = renderSettings_.matcap;
    ImVec2 a, b;
    ui::TileLiftScope lift;
    const bool clicked = tileFrame(id, size, lift.lift, fontSmall_, m < 0 ? std::string("Studio") : std::string(matcapName(m)), s, a, b);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 c((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f);
    const float r = (b.x - a.x) * 0.4f;
    if (ImTextureID thumb = thumbs_.material(m)) { // a rendered sphere on a transparent background
        dl->AddImage(thumb, ImVec2(a.x + 2 * s, a.y + 2 * s), ImVec2(b.x - 2 * s, b.y - 2 * s));
    } else if (const GLuint t = m >= 0 ? renderer_.matcapTexture(m) : 0) {
        // Until then the raw matcap (a sphere picture already; stored bottom-up).
        dl->AddImageRounded(ImTextureID(uintptr_t(t)), ImVec2(c.x - r, c.y - r), ImVec2(c.x + r, c.y + r), ImVec2(0, 1),
                            ImVec2(1, 0), IM_COL32_WHITE, r);
    } else {
        const glm::vec3 base = linearToSrgb(renderSettings_.baseColor);
        constexpr int kLayers = 16;
        for (int k = 0; k < kLayers; ++k) {
            const float t = float(k) / float(kLayers - 1);
            const float shade = 0.3f + 0.85f * std::pow(t, 0.7f);
            dl->AddCircleFilled(ImVec2(c.x - r * 0.3f * t, c.y - r * 0.32f * t), r * (1.0f - t * 0.85f),
                                theme::u32(ImVec4(std::min(1.0f, base.x * shade), std::min(1.0f, base.y * shade),
                                                  std::min(1.0f, base.z * shade), 1.0f)),
                                32);
        }
    }
    if (clicked) openPicker(Picker::Material, ImVec2(ImGui::GetItemRectMax().x + 4.0f * s, ImGui::GetItemRectMin().y));
    ui::HelpText("Material", "M", "How the model is shaded in the view. Click for the material picker.", "Left shelf");
}

// drawColorWidget (the left tray's colour field) is in Pickers.cpp.

} // namespace sf
