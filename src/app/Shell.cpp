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
#include "app/Ui.h"
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

bool beginShellWindow(const char* name, const CanvasRect& r, const ImVec4& bg, ImVec2 padding,
                      ImGuiWindowFlags extra = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse) {
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
    const ImVec4 clay(0.80f, 0.55f, 0.45f, 1.0f);
    constexpr int kLayers = 14;
    for (int k = 0; k < kLayers; ++k) {
        const float t = float(k) / float(kLayers - 1);
        const float rr = r * (1.0f - t * 0.82f);
        const ImVec2 o(c.x - r * 0.32f * t, c.y - r * 0.34f * t);
        const float shade = 0.32f + 0.80f * std::pow(t, 0.8f);
        dl->AddCircleFilled(o, rr, theme::u32(ImVec4(clay.x * shade, clay.y * shade, clay.z * shade, 1.0f)), 32);
    }
    if (type < 0) return;
    const ImU32 hi = IM_COL32(255, 236, 222, 150), lo = IM_COL32(60, 30, 20, 150);
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
void centredText(ImDrawList* dl, ImFont* font, ImVec2 a, ImVec2 b, ImU32 col, const char* text) {
    if (!font) font = ImGui::GetFont();
    const float size = font->FontSize;
    const ImVec2 ts = font->CalcTextSizeA(size, FLT_MAX, 0.0f, text);
    dl->AddText(font, size, ImVec2(std::floor((a.x + b.x - ts.x) * 0.5f), std::floor((a.y + b.y - ts.y) * 0.5f)), col, text);
}

// Text clipped to `width` with an ellipsis.
std::string elide(ImFont* font, const std::string& text, float width) {
    if (font->CalcTextSizeA(font->FontSize, FLT_MAX, 0.0f, text.c_str()).x <= width) return text;
    std::string s = text;
    while (!s.empty() && font->CalcTextSizeA(font->FontSize, FLT_MAX, 0.0f, (s + "...").c_str()).x > width) s.pop_back();
    return s + "...";
}

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
    const std::string id = a.label + "##" + a.id;
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
        const ImVec4 bg = on ? (mode ? (hovered ? theme::accentHover : theme::accent) : hovered ? theme::toggleOnHover : theme::toggleOn)
                             : held ? theme::buttonPressed : hovered ? theme::buttonHover : theme::frame;
        dl->AddRectFilled(p0, p1, theme::u32(bg, alpha), 4.0f * s);
        if (on && !mode) ui::DrawOnMark(dl, p0, p1, true);
        const ImU32 fg = theme::u32(on ? (mode ? theme::textOnAccent : theme::textBright) : hovered || !a.checked ? theme::text : theme::textDim, alpha);
        centredText(dl, app.fontIcons_, p0, ImVec2(p1.x, p1.y - capH + 2.0f * s), fg, a.icon ? a.icon : "?");
        centredText(dl, caption, ImVec2(p0.x, p1.y - capH), ImVec2(p1.x, p1.y - 1.0f * s), fg,
                    elide(caption ? caption : ImGui::GetFont(), a.label, size.x - 2.0f * s).c_str());
        return clicked;
    }
    // Tile: a square picture (brush thumbnail or icon) with the name under it.
    const float pic = std::min(size.x, size.y - capH);
    const ImVec2 q0(p0.x + (size.x - pic) * 0.5f, p0.y), q1(q0.x + pic, q0.y + pic);
    dl->AddRectFilled(p0, p1, theme::u32(hovered ? theme::buttonHover : theme::tile, hovered ? 0.55f : 0.0f), 4.0f * s);
    ImTextureID tex = type >= 0 ? app.thumbnails().brush(type) : ImTextureID(0);
    if (a.id == "draw.paint") { // the Paint brush's own preview (Pickers.cpp)
        BrushItem paint;
        paint.kind = BrushItem::Kind::Paint;
        tex = PickerUi::thumbnail(app, paint);
    }
    if (tex) {
        dl->AddImageRounded(tex, ImVec2(q0.x + 2 * s, q0.y + 2 * s), ImVec2(q1.x - 2 * s, q1.y - 2 * s), ImVec2(0, 0),
                            ImVec2(1, 1), IM_COL32(255, 255, 255, int(255 * alpha)), 4.0f * s);
    } else if (type >= 0) {
        drawBrushIcon(dl, ImVec2((q0.x + q1.x) * 0.5f, (q0.y + q1.y) * 0.5f), pic * 0.36f, type);
    } else {
        dl->AddCircleFilled(ImVec2((q0.x + q1.x) * 0.5f, (q0.y + q1.y) * 0.5f), pic * 0.36f, theme::u32(theme::tile), 32);
        centredText(dl, app.fontIcons_, q0, q1, theme::u32(on ? theme::accent : theme::text, alpha), a.icon ? a.icon : "?");
    }
    if (on) dl->AddRect(p0, p1, theme::u32(theme::accent), 4.0f * s, 0, 2.0f * s);
    // The hotkey digit (1-9, 0) in the corner, as ZBrush shows brush shortcuts.
    const std::string key = app.actions().shortcutText(a.id);
    if (key.size() == 1 && caption)
        dl->AddText(caption, caption->FontSize, ImVec2(p0.x + 3 * s, p0.y + 1 * s), theme::u32(theme::textDim), key.c_str());
    centredText(dl, caption, ImVec2(p0.x, p1.y - capH), p1, theme::u32(on ? theme::accent : theme::text, alpha),
                elide(caption ? caption : ImGui::GetFont(), a.label, size.x).c_str());
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
        "Trim Dynamic: shaves away everything above a plane.",
        "Raises the surface to one fixed height per stroke; overlaps never stack.",
        "Slides the surface sideways along the stroke.",
        "Vector displacement: one DragRect stamps a whole shape with overhangs (horn, ear curl, scales, nostril, or "
        "your own from a VDM plane). Pick the map under Brush > Vector Displacement.",
    };
    static_assert(std::size(kBrushHelp) == size_t(BrushType::Count));
    for (int i = 0; i < int(BrushType::Count); ++i) {
        const BrushType type = BrushType(i);
        Action a;
        a.id = brushActionId(type);
        a.label = brushName(type);
        a.icon = ICON_LC_BRUSH;
        a.palette = "Brush";
        a.shortcut = i < 9 ? std::to_string(i + 1) : i == 9 ? "0" : "";
        a.description = kBrushHelp[i];
        a.run = [this, type] {
            brush_ = type;
            doc_.activeBrush = -1;
            paintMode_ = false;
            guidesMode_ = false; // leaves the QRemesh Guides brush
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
    add({"draw.zadd", "Zadd", nullptr, "Brushes add to the surface. Hold Alt to subtract for one stroke.", "Draw", "", "",
         [this] { invertToggle_ = false; }, [this] { return !invertToggle_; }, nullptr});
    add({"draw.zsub", "Zsub", nullptr, "Brushes subtract from the surface. Hold Alt to add for one stroke.", "Draw", "", "",
         [this] { invertToggle_ = true; }, [this] { return invertToggle_; }, nullptr});
    flag("draw.rgb", "Rgb", nullptr, "Draw", "", &paintRgb_, "Sculpt brushes also paint the colour (ZBrush Rgb with Zadd).");
    flag("draw.front_faces", "Front", nullptr, "Brush", "", &frontFacesOnly_,
         "Front faces only: brushes leave the far side of thin parts alone.");
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
    shape("mask.pen", "MaskPen", "Tool > Masking", &maskShape_, SelectShape::Pen,
          "Ctrl+drag paints the mask with the brush; Ctrl+Alt+drag erases it. Ctrl+drag on the background clears it.");
    shape("mask.lasso", "MaskLasso", "Tool > Masking", &maskShape_, SelectShape::Lasso,
          "Ctrl+drag draws a lasso and masks everything inside it; Ctrl+Alt+drag unmasks. The far side is included.");
    shape("mask.rect", "MaskRect", "Tool > Masking", &maskShape_, SelectShape::Rect,
          "Ctrl+drag draws a rectangle and masks everything inside it; Ctrl+Alt+drag unmasks. The far side is included.");
    shape("select.rect", "SelectRect", "Tool > Visibility", &hideShape_, SelectShape::Rect,
          "Ctrl+Shift+drag a rectangle to keep what's inside and hide the rest; add Alt to hide the inside.");
    shape("select.lasso", "SelectLasso", "Tool > Visibility", &hideShape_, SelectShape::Lasso,
          "Ctrl+Shift+drag a lasso to keep what's inside and hide the rest; add Alt to hide the inside.");
    flag("draw.pressure_strength", "Pressure > Strength", nullptr, "Draw > Tablet", "", &pressureStrength_,
         "Pen pressure scales the brush strength.");
    flag("draw.pressure_size", "Pressure > Size", nullptr, "Draw > Tablet", "", &pressureSize_,
         "Pen pressure scales the brush size.");
    flag("transform.symmetry_x", "X", nullptr, "Transform", "X", &symmetry_.x, "Mirror strokes across X (left and right).");
    flag("transform.symmetry_y", "Y", nullptr, "Transform", "", &symmetry_.y, "Mirror strokes across Y (top and bottom).");
    flag("transform.symmetry_z", "Z", nullptr, "Transform", "", &symmetry_.z, "Mirror strokes across Z (front and back).");
    flag("ui.brush_lab", "AI Brush", ICON_LC_SPARKLES, "Brush", "", &showBrushLab_,
         "Brush Lab: describe a brush in words and AI builds it; tweak the recipe with sliders.");
    flag("ui.generate3d", "Generate 3D", ICON_LC_WAND_SPARKLES, "Generate", "", &showImageTo3D_,
         "Turn an image or a description into a sculptable model (Hugging Face, fal.ai, Meshy, ComfyUI or offline).");
    flag("ui.brush_gallery", "Community brushes", ICON_LC_GLOBE, "Brush", "", &showBrushGallery_,
         "Browse, search and install brushes other sculptors shared.");

    // View (right shelf, Render palette). Frame keeps F; its short label fits the right shelf.
    add({"view.frame", "Frame", ICON_LC_SCAN, "Fit the whole model in the view.", "Transform", "F", "",
         [this] { camera_.frame(sceneBounds()); }, nullptr, nullptr});
        flag("view.polyf", "PolyF", ICON_LC_GRID_3X3, "Render", "", &renderSettings_.wireframe,
         "Polyframe: show the polygons of the active SubTool (Shift+F).");
    flag("view.polygroups", "Groups", ICON_LC_SHAPES, "Render", "", &renderSettings_.showPolygroups,
         "Colour each polygroup of the active SubTool.");
    flag("view.mask", "Mask", ICON_LC_DROPLET, "Render", "", &renderSettings_.showMask, "Darken masked areas.");
    flag("view.polypaint", "Paint", ICON_LC_PALETTE, "Render", "", &renderSettings_.showPolypaint,
         "Show polypaint colours (off: plain material).");
    flag("view.mesh", "Show Mesh", ICON_LC_BOX, "Render", "", &renderSettings_.showMesh, "Draw the sculpt.");
    add({"view.fullscreen", "Fullscreen", ICON_LC_MAXIMIZE, "Fill the screen with SculptForge (again: back to a window).",
         "Render", "F11", "",
         [this] { SDL_SetWindowFullscreen(window_, !(SDL_GetWindowFlags(window_) & SDL_WINDOW_FULLSCREEN)); },
         [this] { return (SDL_GetWindowFlags(window_) & SDL_WINDOW_FULLSCREEN) != 0; }, nullptr});
    add({"view.splats", "Splats", ICON_LC_SPARKLE, "Show the Gaussian splat capture.", "Render", "", "",
         [this] { renderSettings_.showSplats = !renderSettings_.showSplats; },
         [this] { return renderSettings_.showSplats; }, [this] { return !doc_.splats.empty(); }});
    add({"subtool.solo", "Solo", ICON_LC_SCAN_EYE, "Show only the active SubTool (again: show the others).",
         "Tool > SubTool", "", "subtool_solo", [this] { runCommand("subtool_solo"); },
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
    add({"file.import", "Import", ICON_LC_IMPORT, "Add a model file as a new SubTool.", "Tool", "", "",
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
    add({"file.export_obj", "Export OBJ...", nullptr, "Save the visible SubTools as Wavefront OBJ.", "File", "", "",
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
         "Rebuild as an even, watertight surface (like DynaMesh): merges parts, fixes scans and generated models.",
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
    return computeShell(layout_, float(w), float(h), uiScale_, topInset());
}

CanvasRect App::canvasRect() const {
    int w = 0, h = 0;
    SDL_GetWindowSize(window_, &w, &h);
    if (hideUi_) return {0.0f, 0.0f, float(std::max(w, 1)), float(std::max(h, 1))};
    return computeShell(layout_, float(w), float(h), uiScale_, topInset()).canvas;
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
    UiLayout l = standardLayout();
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
    dl->AddRect(ImVec2(c.x - 1.0f, c.y - 1.0f), ImVec2(c.x + c.w + 1.0f, c.y + c.h + 1.0f), theme::u32(theme::canvasBorder));
}

void App::drawShell() {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    shell_ = computeShell(layout_, vp->Size.x, vp->Size.y, uiScale_, topInset());
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
    }
    drawCanvasFrame();
    drawTitleStrip(shell_);
    if (layout_.paletteBarVisible) drawPaletteBar(shell_);
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
    drawPaletteDropdown();
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
    ImGui::TextColored(theme::accent, ICON_LC_BOX);
    ImGui::SameLine(0.0f, 5.0f * s);
    if (fontBold_) ImGui::PushFont(fontBold_);
    ImGui::TextUnformatted("SculptForge");
    if (fontBold_) ImGui::PopFont();

    // Document name; click saves.
    ImGui::SameLine(0.0f, 16.0f * s);
    std::string name = doc_.path.empty() ? std::string("Untitled")
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
    float rightW = searchW + buttonW(ICON_LC_MENU " Menus") + 3.0f * (buttonH + 2.0f * s) + 18.0f * s +
                   10.0f * s + 2.0f * windowButtonW + 2.0f * s;
    if (quickSave) rightW += buttonW("QuickSave") + 8.0f * s;
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
    std::snprintf(info, sizeof info, "Verts %s   Tris %s   SubTool %s (%d/%d)   Layer %s%s%s",
                  shortCount(verts).c_str(), shortCount(tris).c_str(), doc_.activeSubtoolInfo().name.c_str(),
                  doc_.activeSubtool + 1, doc_.subtoolCount(),
                  doc_.activeLayer >= 0 && doc_.activeLayer < int(doc_.mesh.layers.size())
                      ? doc_.mesh.layers[size_t(doc_.activeLayer)].name.c_str()
                      : "Base",
                  doc_.splats.empty() ? "" : "   Splats ", doc_.splats.empty() ? "" : shortCount(doc_.splats.size()).c_str());
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
        const std::string& msg = layout_.customize ? std::string("Customize: Ctrl+Alt+drag to move or remove items") : status_;
        if (!msg.empty() && avail - infoW > 60.0f * s) {
            ImGui::SameLine(0.0f, 20.0f * s);
            const bool error = !layout_.customize && !lastCommandOk_ && ImGui::GetTime() - statusSince_ < 6.0;
            ImGui::PushStyleColor(ImGuiCol_Text, layout_.customize ? theme::accent : error ? theme::danger : theme::text);
            ImGui::TextUnformatted(elide(ImGui::GetFont(), msg, avail - infoW - 20.0f * s).c_str());
            ImGui::PopStyleColor();
            if (ImGui::IsItemHovered() && msg.size() > 40) ImGui::SetTooltip("%s", msg.c_str());
        }
    } else {
        ImGui::TextDisabled("%s", elide(ImGui::GetFont(), info, std::max(0.0f, avail)).c_str());
    }
    if (fontSmall_) ImGui::PopFont();

    // QuickSave, the layout presets and Menus are quiet: no fill until hovered, dim labels, so the title strip
    // doesn't compete with the shelves (Menus on is an option: raised grey with an underline).
    ImGui::SetCursorPos(ImVec2(rightX, (h - buttonH) * 0.5f));
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, theme::headerHover);
    ImGui::PushStyleColor(ImGuiCol_Text, theme::textDim);
    if (quickSave) {
        ui::ActionButton(*this, "file.quicksave", ImVec2(0, buttonH));
        ImGui::SameLine(0.0f, 8.0f * s);
    }
    {
        // 1 2 3 read as one control on a trough.
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float segW = buttonH + 2.0f * s;
        dl->AddRectFilled(p, ImVec2(p.x + 3.0f * segW + 2.0f * 2.0f * s, p.y + buttonH), theme::u32(theme::trough), 3.0f * s);
    }
    for (int i = 1; i <= 3; ++i) {
        ui::ActionButton(*this, ("ui.preset." + std::to_string(i)).c_str(), ImVec2(buttonH + 2.0f * s, buttonH));
        ui::HelpAction(*this, ("ui.preset." + std::to_string(i)).c_str());
        ImGui::SameLine(0.0f, 2.0f * s);
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
        dl->AddRectFilled(p, ImVec2(p.x + searchW, p.y + buttonH), theme::u32(hovered ? theme::frameHover : theme::frame), 3.0f * s);
        const float ty = p.y + (buttonH - ImGui::GetFontSize()) * 0.5f;
        dl->AddText(ImVec2(p.x + 7.0f * s, ty), theme::u32(theme::textDim), ICON_LC_SEARCH "  Search");
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
                              theme::u32(close ? theme::danger : theme::frameHover), 3.0f * s);
        const char* glyph = close ? ICON_LC_X : ICON_LC_MINUS;
        const ImVec2 gs = ImGui::CalcTextSize(glyph);
        dl->AddText(ImVec2(p.x + (windowButtonW - gs.x) * 0.5f, p.y + (buttonH - ImGui::GetFontSize()) * 0.5f),
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
        ImGui::PushStyleColor(ImGuiCol_Text, inTray ? theme::accent : open ? theme::textOnAccent : theme::text);
        const bool gap = !uiPrefs_.alphabeticalPalettes && (k == 2 || (help >= 0 && k + 1 == order.size()));
        if (i) ImGui::SameLine(0.0f, gap ? 10.0f * s : -1.0f); // small gaps after File, Edit and before Help
        const std::string label = std::string(p.name) + "##palette." + p.id;
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

bool App::paletteHeader(const char* id, bool inTray, bool right) {
    const PaletteDef* def = findPalette(id);
    if (!def) return false;
    const float s = uiScale_;
    const float h = 24.0f * s;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float w = ImGui::GetContentRegionAvail().x;
    const bool collapsed = inTray && std::count(collapsedPalettes_.begin(), collapsedPalettes_.end(), id) > 0;
    dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), theme::u32(theme::header), 3.0f * s);

    // Tray icon: in a drop-down it moves the palette into the right tray (Shift: left) or can be dragged to one;
    // in a tray it sends the palette back to the bar.
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
    const bool iconClicked = ImGui::InvisibleButton("##totray", ImVec2(h, h));
    const bool iconHovered = ImGui::IsItemHovered();
    if (!inTray && ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceNoDisableHover)) {
        ImGui::SetDragDropPayload("SF_PALETTE", id, std::strlen(id) + 1);
        ImGui::Text(ICON_LC_PANEL_RIGHT "  %s", def->name);
        ImGui::EndDragDropSource();
    }
    if (inTray) ui::HelpText("Back to the palette bar", nullptr, "Take this palette out of the tray.", def->name);
    else
        ui::HelpText("Move to the tray", nullptr,
                     "Keep this palette in the right tray (Shift+click: left tray), or drag the icon onto a tray.", def->name);
    centredText(dl, nullptr, p, ImVec2(p.x + h, p.y + h), theme::u32(iconHovered ? theme::accentHover : theme::textDim),
                inTray ? (right ? ICON_LC_PANEL_RIGHT : ICON_LC_PANEL_LEFT) : ICON_LC_PANEL_RIGHT);
    ImGui::SameLine();
    const bool titleClicked = ImGui::InvisibleButton("##title", ImVec2(std::max(1.0f, w - 2.0f * h), h));
    const bool titleHovered = ImGui::IsItemHovered();
    if (fontBold_) ImGui::PushFont(fontBold_);
    dl->AddText(ImVec2(p.x + h + 2.0f * s, p.y + (h - ImGui::GetFontSize()) * 0.5f),
                theme::u32(titleHovered && inTray ? theme::textOnAccent : theme::text), def->name);
    ImGui::LogText("%s", def->name);
    if (fontBold_) ImGui::PopFont();
    ImGui::SameLine();
    const bool endClicked = ImGui::InvisibleButton(inTray ? "##collapse" : "##close", ImVec2(h, h));
    const bool endHovered = ImGui::IsItemHovered();
    centredText(dl, nullptr, ImVec2(p.x + w - h, p.y), ImVec2(p.x + w, p.y + h),
                theme::u32(endHovered ? theme::textOnAccent : theme::textDim),
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
    ImGui::SetNextWindowPos(ImVec2(std::clamp(dropdownX_, 4.0f * s, vp->Size.x - w - 4.0f * s),
                                   shell_.paletteBar.y + shell_.paletteBar.h));
    ImGui::SetNextWindowSizeConstraints(ImVec2(w, 0), ImVec2(w, vp->Size.y - 60.0f * s));
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
                                                          theme::u32(theme::accent, a), 3.0f * s);
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
    dl->AddRectFilled(ImVec2(cx - 1.0f * s, cy - 16.0f * s), ImVec2(cx + 1.0f * s, cy + 16.0f * s), grip, 1.0f * s);
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
    if (!beginShellWindow(kShelfWindows[shelf], r, theme::shellBg, pad)) {
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
        bottomTileW_ = std::clamp(avail / float(std::max(tiles - 1, 1)) - 4.0f * s, 44.0f * s, 58.0f * s);
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
            const float w = 170.0f * s;
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
            ui::SliderInside("##drawsize", &radiusPx_, 2.0f, 800.0f, "Draw Size %.0f", ImGuiSliderFlags_Logarithmic);
            ui::HelpText("Draw Size", "S", "Brush radius in screen pixels. [ and ] change it by 10%; Ctrl+click to type.", "Draw");
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
        ImGui::BeginGroup();
        if (fontSmall_) ImGui::PushFont(fontSmall_);
        const float lineH = ImGui::GetTextLineHeight();
        const ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::Dummy(ImVec2(170.0f * s, rowH));
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float y0 = p.y + (rowH - 2.0f * lineH) * 0.5f;
        dl->AddText(ImVec2(p.x, y0), theme::u32(theme::textDim), "ActiveTris");
        dl->AddText(ImVec2(p.x, y0 + lineH), theme::u32(theme::textDim), "TotalTris");
        const std::string a = shortCount(active), t = shortCount(total);
        dl->AddText(ImVec2(p.x + 70.0f * s, y0), theme::u32(theme::text), a.c_str());
        dl->AddText(ImVec2(p.x + 70.0f * s, y0 + lineH), theme::u32(theme::text), t.c_str());
        ImGui::LogText("ActiveTris %s TotalTris %s", a.c_str(), t.c_str());
        if (fontSmall_) ImGui::PopFont();
        ImGui::EndGroup();
        ui::HelpText("ActiveTris / TotalTris", nullptr,
                     "Triangles you can see and sculpt (visible SubTools, hidden parts left out) and in the whole scene.",
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
        const ImVec2 size(48.0f * s, 54.0f * s);
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
        const ImVec2 size(bottomTileW_, 54.0f * s);
        const bool clicked = ImGui::InvisibleButton((name + "##" + item).c_str(), size);
        const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const bool on = found >= 0 && doc_.activeBrush == found && !paintMode_;
        if (ImGui::IsItemHovered()) dl->AddRectFilled(a, b, theme::u32(theme::buttonHover, 0.55f), 4.0f * s);
        const float capH = (fontSmall_ ? fontSmall_->FontSize : 12.0f) + 2.0f * s;
        const float pic = size.y - capH;
        ImTextureID tex = found >= 0 ? thumbs_.recipe(doc_.brushes[size_t(found)]) : ImTextureID(0);
        const ImVec2 c((a.x + b.x) * 0.5f, a.y + pic * 0.5f);
        if (tex) dl->AddImageRounded(tex, ImVec2(c.x - pic * 0.5f, a.y), ImVec2(c.x + pic * 0.5f, a.y + pic), ImVec2(0, 0), ImVec2(1, 1),
                                     IM_COL32_WHITE, 4.0f * s);
        else drawBrushIcon(dl, c, pic * 0.36f, int(BrushType::Standard));
        dl->AddText(fontSmall_, fontSmall_ ? fontSmall_->FontSize : 12.0f, ImVec2(a.x + 2.0f * s, a.y), theme::u32(theme::accent),
                    ICON_LC_SPARKLE);
        if (on) dl->AddRect(a, b, theme::u32(theme::accent), 4.0f * s, 0, 2.0f * s);
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
                            : shelf == kShelfBottom ? ImVec2(bottomTileW_, 54.0f * s)
                                                    : ImVec2(winW - 2.0f * padX, 60.0f * s);
        centre(size.x);
        // Top shelf hierarchy: the mode groups (Draw | Move | Scale | Rotate, Sculpt | Paint) are full-size
        // segments on a dark trough with the current one in the accent; options (Zadd, X, Pressure...) are
        // smaller, dimmer and in the 12 px font; the rest (LightBox, AI Brush) are plain buttons.
        const bool mode = ui::IsModeAction(a->id);
        const bool option = !mode && a->checked && a->id.rfind("ui.", 0) != 0;
        ImVec2 itemSize = size;
        int colors = 0;
        bool small = false;
        if (shelf == kShelfTop) {
            if (option) {
                itemSize.y = rowH - 4.0f * s;
                ImGui::BeginGroup(); // centred in the row; the group keeps the next item on the row's top line
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2.0f * s);
                small = fontSmall_ != nullptr;
                if (small) ImGui::PushFont(fontSmall_);
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f * s, (itemSize.y - ImGui::GetFontSize()) * 0.5f));
                ImGui::PushStyleColor(ImGuiCol_Button, theme::frame);
                ImGui::PushStyleColor(ImGuiCol_Text, theme::textDim);
                colors = 2;
            } else {
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.0f * s, (rowH - ImGui::GetFontSize()) * 0.5f));
                if (mode) {
                    ImGui::PushStyleColor(ImGuiCol_Button, theme::trough);
                    colors = 1;
                }
            }
        }
        ui::SetNextItemOrigin(shelf, index); // ActionButton is the Ctrl+Alt drag source, from this shelf slot
        ui::ActionButton(*this, a->id.c_str(), itemSize, look);
        if (shelf == kShelfTop) {
            ImGui::PopStyleColor(colors);
            ImGui::PopStyleVar();
            if (small) ImGui::PopFont();
            if (option) ImGui::EndGroup();
        }
    } else {
        ImGui::Dummy(ImVec2(0, 0)); // an id this version doesn't know: keep its place in the list
    }
    if (item != "sep" && ImGui::BeginPopupContextItem("##shelfitem")) {
        if (ImGui::MenuItem("Remove from shelf")) {
            auto& list = shelf == kShelfTop    ? layout_.topShelf
                         : shelf == kShelfLeft  ? layout_.leftShelf
                         : shelf == kShelfRight ? layout_.rightShelf
                                                : layout_.bottomShelf;
            if (index < int(list.size()) && list[size_t(index)] == item) list.erase(list.begin() + index);
            noteLayoutChanged();
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
                       "in or shrink the brush for finer triangles, as in Sculptris Pro. Ctrl+click to type.",
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
bool App::canvasNavButton(const char* id, const char* icon, const char* label, int mode) {
    const float s = uiScale_;
    const ImVec2 size(44.0f * s, 40.0f * s);
    ImGui::InvisibleButton((std::string(label) + "##" + id).c_str(), size);
    const bool hovered = ImGui::IsItemHovered(), active = ImGui::IsItemActive();
    if (active) {
        const ImVec2 d = ImGui::GetIO().MouseDelta;
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
    dl->AddRectFilled(a, b, theme::u32(active ? theme::accent : hovered ? theme::buttonHover : theme::frame), 4.0f * s);
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
bool tileFrame(const char* id, ImVec2 size, ImFont* font, const std::string& caption, float s, ImVec2& pic0, ImVec2& pic1) {
    const bool clicked = ImGui::InvisibleButton(id, size);
    const bool hovered = ImGui::IsItemHovered();
    const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float capH = (font ? font->FontSize : 12.0f) + 4.0f * s;
    pic0 = a;
    pic1 = ImVec2(b.x, b.y - capH);
    dl->AddRectFilled(pic0, pic1, theme::u32(theme::tile), 4.0f * s);
    if (hovered) dl->AddRect(pic0, pic1, theme::u32(theme::accent, 0.8f), 4.0f * s, 0, 1.5f * s);
    centredText(dl, font, ImVec2(a.x, b.y - capH), b, theme::u32(hovered ? theme::text : theme::textDim),
                elide(font ? font : ImGui::GetFont(), caption, size.x).c_str());
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
        const bool clicked = tileFrame(id, size, fontSmall_, held->brush, s, a, b);
        drawHeldBrushIcon(ImGui::GetWindowDrawList(), a, b, held->icon, heldBrush() == HeldBrush::Select);
        if (clicked) openPicker(Picker::Brush, ImVec2(ImGui::GetItemRectMax().x + 4.0f * s, ImGui::GetItemRectMin().y));
        ui::HelpText("Brush", "B", "Ctrl held: the mask brush (Ctrl+Shift: the selection brush). Click, or press B with Ctrl "
                     "held, to pick MaskPen, MaskLasso or MaskRect (SelectRect or SelectLasso).", "Left shelf");
        return;
    }
    const BrushRecipe* recipe = activeRecipe();
    const std::string name = guidesMode_ ? "QRemesh Guides" : paintMode_ ? "Paint" : recipe ? recipe->name : brushName(brush_);
    ImVec2 a, b;
    const bool clicked = tileFrame(id, size, fontSmall_, name, s, a, b);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImTextureID tex = paintMode_ || guidesMode_ ? ImTextureID(0) : recipe ? thumbs_.recipe(*recipe) : thumbs_.brush(int(brush_));
    const ImVec2 c((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f);
    const float r = (b.x - a.x) * 0.36f;
    if (tex) {
        dl->AddImageRounded(tex, ImVec2(a.x + 2 * s, a.y + 2 * s), ImVec2(b.x - 2 * s, b.y - 2 * s), ImVec2(0, 0), ImVec2(1, 1),
                            IM_COL32_WHITE, 4.0f * s);
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

void App::drawStrokeTile(const char* id, ImVec2 size) {
    const float s = uiScale_;
    if (const HeldBrushItem* held = heldBrushCurrent(heldBrush())) { // Ctrl / Ctrl+Shift: the mask's stroke
        ImVec2 a, b;
        const bool clicked = tileFrame(id, size, fontSmall_, held->stroke, s, a, b);
        drawHeldBrushIcon(ImGui::GetWindowDrawList(), a, b, held->icon, heldBrush() == HeldBrush::Select);
        if (clicked) openPicker(Picker::Stroke, ImVec2(ImGui::GetItemRectMax().x + 4.0f * s, ImGui::GetItemRectMin().y));
        ui::HelpText("Stroke", nullptr, "Ctrl held: the mask's stroke, Dots (MaskPen), Lasso or Rect (Ctrl+Shift: the "
                     "selection's). Click with Ctrl held to choose.", "Left shelf");
        return;
    }
    ImVec2 a, b;
    const bool clicked = tileFrame(id, size, fontSmall_, strokeTypeName(strokeType_), s, a, b);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImU32 col = IM_COL32(235, 235, 235, 230);
    const float w = b.x - a.x, h = b.y - a.y;
    auto at = [&](float u, float v) { return ImVec2(a.x + w * u, a.y + h * v); };
    switch (strokeType_) {
    case StrokeType::Dots: // ZBrush's dotted arc
        for (int i = 0; i < 7; ++i) {
            const float u = float(i) / 6.0f;
            dl->AddCircleFilled(at(0.18f + 0.64f * u, 0.30f + 0.42f * std::sin(u * 2.4f) * (1.0f - u * 0.35f)), 3.2f * s, col);
        }
        break;
    case StrokeType::DragRect:
        dl->AddRect(at(0.22f, 0.26f), at(0.78f, 0.74f), col, 2.0f * s, 0, 1.5f * s);
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
    if (clicked) openPicker(Picker::Stroke, ImVec2(ImGui::GetItemRectMax().x + 4.0f * s, ImGui::GetItemRectMin().y));
    ui::HelpText("Stroke", nullptr, "How dabs follow the cursor: Dots, DragRect, DragDot, Spray or LazyMouse. Click to choose.",
                 "Left shelf");
}

void App::drawAlphaTile(const char* id, ImVec2 size) {
    const float s = uiScale_;
    ImVec2 a, b;
    if (vdmBrushActive() && doc_.brushVdm) { // the VDM brush's 3D alpha (ZBrush shows VDMs in the alpha slot)
        const bool clicked = tileFrame(id, size, fontSmall_, doc_.brushVdm->name, s, a, b);
        if (ImTextureID tex = thumbs_.vdm(doc_.brushVdm))
            ImGui::GetWindowDrawList()->AddImageRounded(tex, ImVec2(a.x + 2 * s, a.y + 2 * s), ImVec2(b.x - 2 * s, b.y - 2 * s),
                                                        ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, 4.0f * s);
        if (clicked) openPicker(Picker::Alpha, ImVec2(ImGui::GetItemRectMax().x + 4.0f * s, ImGui::GetItemRectMin().y));
        ui::HelpText("VDM", nullptr, "The VDM brush's vector displacement map. Click to pick another.", "Left shelf");
        return;
    }
    const bool clicked = tileFrame(id, size, fontSmall_, doc_.brushAlpha.name, s, a, b);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImTextureID tex = thumbs_.alpha(doc_.brushAlpha);
    if (tex) {
        dl->AddImageRounded(tex, ImVec2(a.x + 2 * s, a.y + 2 * s), ImVec2(b.x - 2 * s, b.y - 2 * s), ImVec2(0, 0), ImVec2(1, 1),
                            IM_COL32_WHITE, 4.0f * s);
    } else {
        constexpr int kCells = 26;
        const float inset = 6.0f * s, cell = (b.x - a.x - 2.0f * inset) / kCells;
        dl->AddRectFilled(ImVec2(a.x + inset, a.y + inset), ImVec2(b.x - inset, b.y - inset), IM_COL32(0, 0, 0, 255), 3.0f * s);
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
    const bool clicked = tileFrame(id, size, fontSmall_, m < 0 ? std::string("Studio") : std::string(matcapName(m)), s, a, b);
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
