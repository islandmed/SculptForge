// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The one-surface look (polyflow on Discord, "Alternate UI"; App.h): with the SculptForge layout, the top bar is
// the window's caption as well. It holds the menus button, the file and its saved state, the rooms (Sculpt, Paint,
// UV, Model), the current brush, undo and redo, the stats chip and the window buttons; the empty parts of the bar
// move the window, its edges resize it (SDL_SetWindowHitTest). The bars, shelves and trays are one flat colour
// (Theme.cpp applyOneSurface), the viewport a rounded opening in it, and the triangles, memory and frame times wait
// behind the stats chip. Where the desktop draws its own title bar anyway (tiling compositors, macOS, Preferences >
// Interface > System title bar) the bar stays a plain top bar without window buttons. Preferences > Interface >
// Look > Classic look brings back the status bar; shelves, trays, menus and every key are the same in both.
#include "app/App.h"

#include "app/ForgeTokens.h"
#include "app/I18n.h"
#include "app/Shell.h"
#include "app/Theme.h"
#include "app/Ui.h"
#include "core/MemoryGuard.h"
#include "core/Micro.h"

#include <IconsLucide.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>

namespace sf {

namespace {

constexpr ImGuiWindowFlags kBarFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                                       ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus |
                                       ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav |
                                       ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
constexpr ImGuiWindowFlags kCardFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                        ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing |
                                        ImGuiWindowFlags_NoNav;

std::string shortCount(size_t n) { // 1234 -> "1.2k", 1402118 -> "1.4M"
    char b[32];
    if (n >= 1000000) std::snprintf(b, sizeof b, "%.1fM", double(n) / 1e6);
    else if (n >= 1000) std::snprintf(b, sizeof b, "%.1fk", double(n) / 1e3);
    else std::snprintf(b, sizeof b, "%zu", n);
    return b;
}

std::string groupedCount(size_t n) { // 1402118 -> "1,402,118"
    std::string d = std::to_string(n), out;
    for (size_t i = 0; i < d.size(); ++i) {
        if (i && (d.size() - i) % 3 == 0) out += ',';
        out += d[i];
    }
    return out;
}

float textW(ImFont* f, const std::string& t) { return f->CalcTextSizeA(f->FontSize, FLT_MAX, 0.0f, t.c_str()).x; }

std::string elideTo(ImFont* f, const std::string& text, float width) {
    if (textW(f, text) <= width) return text;
    std::string t = text;
    while (!t.empty() && textW(f, t + "...") > width) t.pop_back();
    return t.empty() ? std::string() : t + "...";
}

// A tiling compositor or window manager draws no title bars (or its own): the bar stays a plain top bar there.
bool tilingDesktop() {
#if defined(__linux__) || defined(__FreeBSD__)
    for (const char* v : {"HYPRLAND_INSTANCE_SIGNATURE", "SWAYSOCK", "I3SOCK", "NIRI_SOCKET"})
        if (const char* e = std::getenv(v); e && *e) return true;
    if (const char* d = std::getenv("XDG_CURRENT_DESKTOP")) {
        std::string desk(d);
        for (char& c : desk) c = char(std::tolower(static_cast<unsigned char>(c)));
        for (const char* t : {"hyprland", "sway", "i3", "river", "niri", "bspwm", "dwm", "awesome", "qtile", "xmonad", "herbstluftwm"})
            if (desk.find(t) != std::string::npos) return true;
    }
#endif
    return false;
}

// SF_CAPTION=1: the bar is the caption even on a screenless run (UI tests and shots of the window buttons);
// SF_CAPTION=0: never (the system title bar).
int captionOverride() {
    const char* e = std::getenv("SF_CAPTION");
    return e && *e == '1' ? 1 : e && *e == '0' ? 0 : -1;
}

} // namespace

// The caption's hit test (App.h CaptionHit). SDL calls it inside the event pump, on the main thread: on Windows for
// every pointer move over the window (WM_NCHITTEST), elsewhere on presses and moves.
SDL_HitTestResult SDLCALL captionHitTest(SDL_Window* window, const SDL_Point* p, void* data) {
    App::CaptionHit& c = static_cast<App*>(data)->captionHit_;
    int w = 0, h = 0;
    SDL_GetWindowSize(window, &w, &h);
    const float x = float(p->x), y = float(p->y), e = c.edge;
    const SDL_WindowFlags flags = SDL_GetWindowFlags(window);
    if (flags & SDL_WINDOW_FULLSCREEN) return SDL_HITTEST_NORMAL; // full screen: nothing moves or resizes the window
    const bool fixed = (flags & SDL_WINDOW_MAXIMIZED) != 0;
    if (c.resizable && !fixed) { // the window's edges; the corners a little larger. The top edge is thin: it is the bar.
        const float corner = 2.0f * e;
        const bool left = x < e, right = x >= float(w) - e, top = y < e * 0.6f, bottom = y >= float(h) - e;
        const bool nearLeft = x < corner, nearRight = x >= float(w) - corner, nearTop = y < corner, nearBottom = y >= float(h) - corner;
        if ((top && nearLeft) || (left && nearTop)) return SDL_HITTEST_RESIZE_TOPLEFT;
        if ((top && nearRight) || (right && nearTop)) return SDL_HITTEST_RESIZE_TOPRIGHT;
        if ((bottom && nearLeft) || (left && nearBottom)) return SDL_HITTEST_RESIZE_BOTTOMLEFT;
        if ((bottom && nearRight) || (right && nearBottom)) return SDL_HITTEST_RESIZE_BOTTOMRIGHT;
        if (top) return SDL_HITTEST_RESIZE_TOP;
        if (bottom) return SDL_HITTEST_RESIZE_BOTTOM;
        if (left) return SDL_HITTEST_RESIZE_LEFT;
        if (right) return SDL_HITTEST_RESIZE_RIGHT;
    }
    if (y < c.barTop || y >= c.barTop + c.barH || x < c.left || x >= c.right) return SDL_HITTEST_NORMAL;
    for (const auto& [a, b] : c.spans)
        if (x >= a && x < b) return SDL_HITTEST_NORMAL; // a button, tab or chip of the bar
#if !defined(_WIN32)
    // A double press on the empty bar maximises (Windows does that itself for a caption). Elsewhere SDL asks on
    // moves as well, so a press is the button going down between two calls.
    const bool down = (SDL_GetGlobalMouseState(nullptr, nullptr) & SDL_BUTTON_LMASK) != 0;
    if (down && !c.buttonWasDown) {
        const uint64_t now = SDL_GetTicksNS();
        if (c.lastPressNs && now - c.lastPressNs < 400000000ull && std::abs(x - c.lastPressX) < 6.0f && std::abs(y - c.lastPressY) < 6.0f) {
            c.doubleClick = true;
            c.lastPressNs = 0;
        } else {
            c.lastPressNs = now;
            c.lastPressX = x;
            c.lastPressY = y;
        }
    }
    c.buttonWasDown = down;
#endif
    return SDL_HITTEST_DRAGGABLE;
}

void App::syncWindowCaption() {
    if (!window_) return;
    static const bool tiling = tilingDesktop();
    static const int fromEnv = captionOverride();
    const int forced = captionForce_ >= 0 ? captionForce_ : fromEnv;
    const char* driver = SDL_GetCurrentVideoDriver();
    const bool noScreen = driver && (std::strcmp(driver, "offscreen") == 0 || std::strcmp(driver, "dummy") == 0);
#if defined(__APPLE__)
    const bool system = true; // the traffic lights stay in the system's title bar (a unified title bar needs a Mac to test)
#else
    const bool system = false;
#endif
    bool want = oneSurface() && !uiPrefs_.systemTitleBar && !tiling && !system && !noScreen && !sharedWindow_;
    if (forced >= 0) want = forced == 1 && oneSurface() && !uiPrefs_.systemTitleBar;
    if (want == captionOwned_ && captionTried_) return;
    captionTried_ = true;
    captionOwned_ = want;
    if (noScreen) return; // (the forced caption of a test: drawn, nothing to tell the window system)
    SDL_SetWindowBordered(window_, !want);
    if (!SDL_SetWindowHitTest(window_, want ? captionHitTest : nullptr, want ? this : nullptr) && want) {
        SDL_Log("The top bar can't move the window here (%s): the system title bar stays", SDL_GetError());
        SDL_SetWindowBordered(window_, true);
        captionOwned_ = false;
    }
}

void App::noteFrameTime(float seconds) {
    const double now = ImGui::GetTime();
    // A frame after the app sat idle (it draws only when something changes) is waiting, not work: left out.
    if (seconds > 0.0f && seconds < 0.75f) {
        frameWorst_ = std::max(frameWorst_, seconds);
        frameWindowSum_ += seconds;
        ++frameWindowCount_;
    }
    if (now - frameWindowAt_ >= 1.0) {
        if (frameWindowCount_ > 0) {
            frameWorstShown_ = frameWorst_;
            frameMeanShown_ = frameWindowSum_ / float(frameWindowCount_);
        }
        frameWorst_ = frameWindowSum_ = 0.0f;
        frameWindowCount_ = 0;
        frameWindowAt_ = now;
    }
    if (captionHit_.doubleClick) {
        captionHit_.doubleClick = false;
        if (SDL_GetWindowFlags(window_) & SDL_WINDOW_MAXIMIZED) SDL_RestoreWindow(window_);
        else SDL_MaximizeWindow(window_);
    }
}

// ---------------------------------------------------------------------------
// Actions: the rooms, Classic look
// ---------------------------------------------------------------------------

void App::registerOneSurfaceActions() {
    auto add = [this](const char* id, const char* label, const char* icon, const char* description, std::function<void()> run,
                      std::function<bool()> checked) {
        Action a;
        a.id = id;
        a.label = label;
        a.icon = icon;
        a.palette = "Interface";
        a.description = description;
        a.run = std::move(run);
        a.checked = std::move(checked);
        actions_.add(std::move(a));
    };
    auto closeUv = [this] {
        if (showUvEditor_)
            if (const Action* a = actions_.find("ui.uv_editor"); a && a->run) a->run();
    };
    add("room.sculpt", "Sculpt", ICON_LC_BRUSH, "The Sculpt room: brushes change the shape.",
        [this, closeUv] {
            paintMode_ = false;
            closeUv();
        },
        [this] { return !paintMode_ && !showUvEditor_; });
    add("room.paint", "Paint", ICON_LC_PAINTBRUSH, "The Paint room: brushes paint colour on the same model, without sculpting.",
        [this, closeUv] {
            paintMode_ = true;
            closeUv();
        },
        [this] { return paintMode_ && !showUvEditor_; });
    add("room.uv", "UV", ICON_LC_GRID_2X2, "The UV room: the UV Editor beside the model (Alt+U).",
        [this] {
            if (!showUvEditor_)
                if (const Action* a = actions_.find("ui.uv_editor"); a && a->run) a->run();
        },
        [this] { return showUvEditor_; });
    add("room.model", "Model", ICON_LC_BOX, "The Model room: Parts, geometry, Forge Spheres and remeshing (the Mesh menu).",
        [this] {
            int count = 0;
            const TaskMenuDef* menus = taskMenus(count);
            for (int i = 0; i < count; ++i)
                if (std::strcmp(menus[i].id, "mesh") == 0) openTaskMenuAt(i, 0.0f);
        },
        [this] {
            int count = 0;
            const int open = dropdown_.empty() ? -1 : taskMenuOf(dropdown_);
            return open >= 0 && std::strcmp(taskMenus(count)[open].id, "mesh") == 0;
        });
    add("ui.classic_look", "Classic look", ICON_LC_PANEL_TOP,
        "The status bar, tiles and system title bar as before the one-surface look. Shelves, trays, menus and keys are "
        "the same in both.",
        [this] {
            uiPrefs_.oneSurface = !uiPrefs_.oneSurface;
            if (uiPrefs_.oneSurface && !sculptForgeLayout()) setShellLayout(1); // (the one surface is the SculptForge layout's)
            oneSurfaceToast_ = false;
            lookDirty_ = true;
            updateCameraAspect();
        },
        [this] { return !oneSurface(); });
    add("ui.system_title_bar", "System title bar", ICON_LC_PANEL_TOP,
        "Keep the system's own title bar and window buttons above the one-surface top bar.",
        [this] { uiPrefs_.systemTitleBar = !uiPrefs_.systemTitleBar; }, [this] { return uiPrefs_.systemTitleBar; });
}

void App::openToolSection(const char* subpalette) {
    auto& open = layout_.openSubpalettes;
    if (uiPrefs_.oneSubpalette)
        open.erase(std::remove_if(open.begin(), open.end(), [](const std::string& o) { return o.rfind("tool.", 0) == 0; }), open.end());
    if (std::find(open.begin(), open.end(), subpalette) == open.end()) open.push_back(subpalette);
    flashSubpalette_ = subpalette;
    flashSubpaletteStart_ = ImGui::GetTime();
    if (const Action* a = actions_.find("ui.palette.tool"); a && a->run) a->run();
}

// ---------------------------------------------------------------------------
// The top bar
// ---------------------------------------------------------------------------

void App::drawOneSurfaceBar(const ShellGeometry& g) {
    const float s = uiScale_;
    ImGui::SetNextWindowPos(ImVec2(g.title.x, g.title.y));
    ImGui::SetNextWindowSize(ImVec2(g.title.w, g.title.h));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(1.0f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, theme::shellBg);
    const bool open = ImGui::Begin("##TitleStrip", nullptr, kBarFlags);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(4);
    if (!open) {
        ImGui::End();
        return;
    }
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 o(g.title.x, g.title.y);
    const float h = g.title.h, W = g.title.w;
    const float bh = std::round(h - 10.0f * s); // buttons: 34 px at 100 %
    const float by = std::floor(o.y + (h - bh) * 0.5f);
    ImFont* font = ImGui::GetFont();
    ImFont* bold = fontBold_ ? fontBold_ : font;
    ImFont* small = fontSmall_ ? fontSmall_ : font;
    const ImU32 ink = theme::u32(theme::text), dim = theme::u32(theme::textDim), hoverBg = IM_COL32(255, 255, 255, 15),
                onBg = IM_COL32(255, 255, 255, 18);
    auto textAt = [&](ImFont* f, float x, ImU32 col, const std::string& t) {
        dl->AddText(f, f->FontSize, ImVec2(std::floor(x), std::floor(o.y + (h - f->FontSize) * 0.5f)), col, t.c_str());
        return x + textW(f, t);
    };
    auto button = [&](const char* id, float x, float w) { // an invisible button over the bar's height band
        ImGui::SetCursorScreenPos(ImVec2(x, by));
        const bool clicked = ImGui::InvisibleButton(id, ImVec2(std::max(w, 1.0f), bh));
        barSpans_.push_back({x, x + w});
        return clicked;
    };
    auto runAction = [&](const char* id) {
        if (const Action* a = actions_.find(id); a && a->run) {
            actions_.touch(a->id);
            a->run();
        }
    };
    barSpans_.clear();
    const bool winButtons = captionOwned_;

    // ---- Right group, measured from the right: window buttons, search, stats chip, redo, undo.
    float right = o.x + W - (winButtons ? 0.0f : 8.0f * s);
    const float winW = 46.0f * s;
    const float winX = right - (winButtons ? 3.0f * winW : 0.0f);
    if (winButtons) right = winX - 6.0f * s;
    const float iconW = 36.0f * s;
    const float searchX = right - iconW;
    right = searchX - 6.0f * s;
    if (ImGui::GetTime() - statsChipAt_ > 0.5 || statsChipAt_ < 0.0) { // twice a second: never a per-frame count
        statsChipAt_ = ImGui::GetTime();
        size_t tris = 0;
        for (int i = 0; i < doc_.partCount(); ++i) tris += doc_.partMesh(i).triangleCount();
        size_t micro = 0;
        for (int i = 0; i < doc_.partCount(); ++i)
            if (doc_.parts[size_t(i)].visible) micro += microTriangleCount(doc_.partMesh(i));
        char fps[32];
        std::snprintf(fps, sizeof fps, "%.0f %s", double(ImGui::GetIO().Framerate), tr("fps"));
        statsChip_ = shortCount(tris + micro) + "  " + fps;
    }
    const float chipW = textW(small, statsChip_) + 30.0f * s;
    const float chipX = right - chipW;
    right = chipX - 8.0f * s;
    const float redoX = right - iconW, undoX = redoX - iconW - 2.0f * s;
    right = undoX - 10.0f * s;
    const bool mayaLink = mayaLink_ != nullptr;
    const std::string linkLabel = std::string(ICON_LC_RADIO "  ") + tr("Live link on");
    const float linkW = textW(small, linkLabel) + 20.0f * s;
    const float linkX = right - linkW;
    if (mayaLink) right = linkX - 8.0f * s;

    // ---- Left group: the menus button, the file.
    float x = o.x + 8.0f * s;
    const float menusX = x;
    x += iconW + 4.0f * s;
    std::string name = doc_.path.empty() ? std::string(tr("Untitled"))
                                         : std::filesystem::path(std::u8string(doc_.path.begin(), doc_.path.end())).stem().string();
    const bool unsaved = doc_.dirty() || doc_.path.empty();
    const std::string state = doc_.path.empty() ? tr("Not saved yet") : doc_.dirty() ? tr("Unsaved changes") : tr("Saved");
    const float stateW = 12.0f * s + textW(small, state);
    const float fileMax = std::max(60.0f * s, std::min(260.0f * s, (W - 700.0f * s) * 0.35f));
    name = elideTo(bold, name, fileMax);
    const bool showState = W > 900.0f * s;
    const float fileW = 10.0f * s + textW(bold, name) + (showState ? 10.0f * s + stateW : 0.0f) + 24.0f * s;
    const float fileX = x;
    x += fileW;
    const float leftEnd = x + 10.0f * s;

    // ---- Centre: the rooms, a rule, the brush. Centred on the window where they fit, else in the free space; a
    // narrow window drops the room names (icons stay), then the brush's values, then the brush.
    struct Room {
        const char* action;
        const char* label;
        ImU32 hue;
    };
    const Room rooms[] = {{"room.sculpt", "Sculpt", tokens::kRoomSculpt},
                          {"room.paint", "Paint", tokens::kRoomPaint},
                          {"room.uv", "UV", tokens::kRoomUv},
                          {"room.model", "Model", tokens::kRoomModel}};
    const BrushRecipe* recipe = activeRecipe();
    std::string brush = guidesMode_ ? std::string("Quad Remesh Guides") : paintMode_ ? std::string(tr("Paint"))
                                                                         : recipe ? recipe->name : std::string(brushLabel(brush_));
    char values[48];
    std::snprintf(values, sizeof values, "%.0f  ·  %.0f%%", double(radiusPx_), double(intensityValue(activeBrush() == BrushType::Smooth)) * 100.0);
    auto centreWidth = [&](bool labels, bool brushValues, bool brushChip) {
        float w = 0.0f;
        for (const Room& r : rooms) w += (labels ? 24.0f * s + 16.0f * s + textW(font, tr(r.label)) : iconW) + 4.0f * s;
        if (brushChip) w += 21.0f * s + 30.0f * s + textW(bold, brush) + (brushValues ? 8.0f * s + textW(small, values) : 0.0f) + 12.0f * s;
        return w;
    };
    const float freeW = right - leftEnd;
    bool labels = true, brushValues = true, brushChip = true;
    if (centreWidth(labels, brushValues, brushChip) > freeW) brushValues = false;
    if (centreWidth(labels, brushValues, brushChip) > freeW) labels = false;
    if (centreWidth(labels, brushValues, brushChip) > freeW) brushChip = false;
    const float cw = centreWidth(labels, brushValues, brushChip);
    float cx = std::floor(o.x + (W - cw) * 0.5f);
    cx = std::clamp(cx, leftEnd, std::max(leftEnd, right - cw));
    const bool centre = cw <= freeW;

    // The menus button: every task menu, and the layout presets.
    {
        const bool clicked = button("Menus##os.menus", menusX, iconW);
        const bool hovered = ImGui::IsItemHovered();
        const bool menuOpen = !dropdown_.empty() || ImGui::IsPopupOpen("##os.menus.list");
        if (hovered || menuOpen) dl->AddRectFilled(ImVec2(menusX, by), ImVec2(menusX + iconW, by + bh), menuOpen ? onBg : hoverBg, 8.0f * s);
        ui::CentredGlyph(dl, nullptr, ImVec2(menusX, by), ImVec2(menusX + iconW, by + bh), menuOpen ? ink : dim, ICON_LC_MENU);
        ui::HelpText("Menus", nullptr, "Every menu: File, Edit, Sculpt, Paint, Mesh, View, Render and Help, and the layout presets.", "Top bar");
        if (clicked) ImGui::OpenPopup("##os.menus.list");
        ImGui::SetNextWindowPos(ImVec2(menusX, o.y + h));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6.0f * s, 6.0f * s));
        if (ImGui::BeginPopup("##os.menus.list")) {
            int count = 0;
            const TaskMenuDef* menus = taskMenus(count);
            const int openMenu = dropdown_.empty() ? -1 : taskMenuOf(dropdown_);
            for (int i = 0; i < count; ++i) {
                const std::string label = std::string(tr(menus[i].name)) + "##menu." + menus[i].id;
                if (ImGui::Selectable(label.c_str(), openMenu == i, 0, ImVec2(200.0f * s, 26.0f * s))) openTaskMenuAt(i, menusX + 214.0f * s);
                ImGui::LogText("%s", tr(menus[i].name));
            }
            ImGui::Separator();
            ImGui::TextDisabled("%s", tr("Layout"));
            for (int i = 1; i <= 3; ++i) {
                ImGui::SameLine();
                ui::ActionButton(*this, ("ui.preset." + std::to_string(i)).c_str(), ImVec2(30.0f * s, 0.0f));
            }
            ImGui::EndPopup();
        }
        ImGui::PopStyleVar();
    }

    // The file: its name and saved state; a click opens File.
    {
        const bool clicked = button("##os.file", fileX, fileW);
        const bool hovered = ImGui::IsItemHovered();
        if (hovered) dl->AddRectFilled(ImVec2(fileX, by), ImVec2(fileX + fileW, by + bh), hoverBg, 8.0f * s);
        float tx = textAt(bold, fileX + 10.0f * s, theme::u32(theme::textBright), name);
        ImGui::LogText("%s", name.c_str());
        if (showState) {
            tx += 10.0f * s;
            const float cy = std::floor(o.y + h * 0.5f) + 0.5f;
            dl->AddCircleFilled(ImVec2(tx + 4.0f * s, cy), 3.5f * s, unsaved ? theme::u32(theme::textDim) : tokens::kOk, 12);
            tx = textAt(small, tx + 12.0f * s, dim, state);
            ImGui::LogText("%s", state.c_str());
        }
        ui::CentredGlyph(dl, nullptr, ImVec2(fileX + fileW - 24.0f * s, by), ImVec2(fileX + fileW - 4.0f * s, by + bh), dim, ICON_LC_CHEVRON_DOWN);
        ui::HelpText("Project", "Ctrl+S", "The project and whether it is saved. Click for the File menu.", "Top bar");
        if (clicked) openTaskMenuAt(0, fileX);
    }

    // The rooms.
    if (centre) {
        float rx = cx;
        for (const Room& r : rooms) {
            const Action* a = actions_.find(r.action);
            const bool on = a && a->checked && a->checked();
            const std::string label = tr(r.label);
            const float w = labels ? 24.0f * s + 16.0f * s + textW(font, label) : iconW;
            const bool clicked = button((label + "##" + r.action).c_str(), rx, w);
            const bool hovered = ImGui::IsItemHovered();
            if (on || hovered) dl->AddRectFilled(ImVec2(rx, by), ImVec2(rx + w, by + bh), on ? onBg : hoverBg, 8.0f * s);
            const float ix = labels ? rx + 12.0f * s : rx + (w - 16.0f * s) * 0.5f;
            const float cy = std::floor(o.y + h * 0.5f);
            if (std::strcmp(r.action, "room.sculpt") == 0) { // our own clay ball, in the room's hue
                dl->AddCircleFilled(ImVec2(ix + 8.0f * s, cy), 7.0f * s, r.hue, 24);
                dl->AddCircleFilled(ImVec2(ix + 6.0f * s, cy - 2.5f * s), 2.5f * s, IM_COL32(255, 255, 255, 110), 12);
            } else {
                const char* glyph = a && a->icon ? a->icon : "";
                dl->AddText(font, font->FontSize, ImVec2(ix, std::floor(o.y + (h - font->FontSize) * 0.5f)), r.hue, glyph);
            }
            if (labels) textAt(on ? bold : font, ix + 24.0f * s, on ? theme::u32(theme::textBright) : dim, label);
            ImGui::LogText("%s", label.c_str());
            if (a) ui::HelpAction(*this, r.action);
            if (clicked) {
                if (std::strcmp(r.action, "room.model") == 0) {
                    int count = 0;
                    const TaskMenuDef* menus = taskMenus(count);
                    for (int i = 0; i < count; ++i)
                        if (std::strcmp(menus[i].id, "mesh") == 0) {
                            if (on) dropdown_.clear();
                            else openTaskMenuAt(i, rx);
                        }
                } else {
                    runAction(r.action);
                }
            }
            rx += w + 4.0f * s;
        }
        // The brush: its picture, name, size and strength; a click opens the brush picker.
        if (brushChip) {
            const float ruleX = std::floor(rx + 8.0f * s) + 0.5f;
            dl->AddLine(ImVec2(ruleX, o.y + h * 0.5f - 10.0f * s), ImVec2(ruleX, o.y + h * 0.5f + 10.0f * s), tokens::kLine2, 1.0f);
            const float bx = rx + 17.0f * s;
            const float bw = 30.0f * s + textW(bold, brush) + (brushValues ? 8.0f * s + textW(small, values) : 0.0f) + 12.0f * s;
            const bool clicked = button("##os.brush", bx, bw);
            const bool hovered = ImGui::IsItemHovered();
            if (hovered) dl->AddRectFilled(ImVec2(bx, by), ImVec2(bx + bw, by + bh), hoverBg, 8.0f * s);
            const ImVec2 c(bx + 16.0f * s, std::floor(o.y + h * 0.5f));
            ImTextureID tex = paintMode_ || guidesMode_ ? ImTextureID(0) : recipe ? thumbs_.recipe(*recipe) : thumbs_.brush(int(brush_));
            if (tex) dl->AddImageRounded(tex, ImVec2(c.x - 12.0f * s, c.y - 12.0f * s), ImVec2(c.x + 12.0f * s, c.y + 12.0f * s), ImVec2(0, 0),
                                         ImVec2(1, 1), IM_COL32_WHITE, 12.0f * s);
            else dl->AddCircleFilled(c, 11.0f * s, theme::u32(theme::button), 24);
            float tx = textAt(bold, bx + 32.0f * s, ink, brush);
            if (brushValues) textAt(small, tx + 8.0f * s, dim, values);
            ImGui::LogText("%s", brush.c_str());
            ui::HelpText("Brush", "B", "The brush, its size and strength. Click for the brush picker.", "Top bar");
            if (clicked) openPicker(Picker::Brush, ImVec2(bx, o.y + h + 4.0f * s));
        }
    }

    // The Maya Live link, while it is on.
    if (mayaLink) {
        const bool clicked = button((std::string(tr("Live link on")) + "##mayalink.chip").c_str(), linkX, linkW);
        const bool hovered = ImGui::IsItemHovered();
        dl->AddRectFilled(ImVec2(linkX, by + 4.0f * s), ImVec2(linkX + linkW, by + bh - 4.0f * s),
                          theme::u32(hovered ? theme::accentHover : theme::accent), 8.0f * s);
        textAt(small, linkX + 10.0f * s, tokens::kOnForge, linkLabel);
        if (clicked) stopMayaLink();
        ui::HelpText("Live link on", nullptr, "Click to turn the Maya Live link off.", "Top bar");
    }

    // Undo, redo.
    for (int k = 0; k < 2; ++k) {
        const char* id = k ? "edit.redo" : "edit.undo";
        const float bx = k ? redoX : undoX;
        const Action* a = actions_.find(id);
        const bool can = !a || !a->enabled || a->enabled();
        const bool clicked = button(k ? "Redo##os.redo" : "Undo##os.undo", bx, iconW);
        const bool hovered = ImGui::IsItemHovered();
        if (hovered && can) dl->AddRectFilled(ImVec2(bx, by), ImVec2(bx + iconW, by + bh), hoverBg, 8.0f * s);
        ui::CentredGlyph(dl, nullptr, ImVec2(bx, by), ImVec2(bx + iconW, by + bh), can ? ink : theme::u32(theme::textDim, 0.5f),
                         k ? ICON_LC_REDO_2 : ICON_LC_UNDO_2);
        ui::HelpAction(*this, id);
        if (clicked && can) runAction(id);
    }

    // The stats chip: hover for the card, click to pin it.
    {
        const bool clicked = button("##os.stats", chipX, chipW);
        const bool hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup);
        const ImVec2 a(chipX, by + 3.0f * s), b(chipX + chipW, by + bh - 3.0f * s);
        dl->AddRectFilled(a, b, statsPinned_ || hovered ? IM_COL32(255, 255, 255, 22) : IM_COL32(255, 255, 255, 13), 8.0f * s);
        const float tx = textAt(small, chipX + 10.0f * s, dim, statsChip_);
        ui::CentredGlyph(dl, nullptr, ImVec2(tx, by), ImVec2(chipX + chipW - 4.0f * s, by + bh), dim,
                         statsPinned_ ? ICON_LC_PIN : ICON_LC_CHEVRON_DOWN);
        ImGui::LogText("%s", statsChip_.c_str());
        if (clicked) statsPinned_ = !statsPinned_;
        if (!ImGui::GetIO().KeyCtrl && hovered && !statsPinned_) ImGui::SetItemTooltip("%s", tr("Click to keep the stats open"));
        ImGui::End(); // (the card is a window of its own, over the canvas)
        drawOneSurfaceStats(a, b, hovered);
        ImGui::Begin("##TitleStrip", nullptr, kBarFlags);
    }

    // Search: the command palette (F3).
    {
        const bool clicked = button("##search", searchX, iconW);
        const bool hovered = ImGui::IsItemHovered();
        if (hovered) dl->AddRectFilled(ImVec2(searchX, by), ImVec2(searchX + iconW, by + bh), hoverBg, 8.0f * s);
        ui::CentredGlyph(dl, nullptr, ImVec2(searchX, by), ImVec2(searchX + iconW, by + bh), dim, ICON_LC_SEARCH);
        ui::HelpText("Search", "F3", "Find any command, palette, brush or material by name and run it.", "Top bar");
        if (clicked) runAction("ui.command_palette");
    }

    // Window buttons: minimise, maximise or restore, close (asks to save changes first). Drawn as thin lines.
    if (winButtons) {
        const bool maximized = (SDL_GetWindowFlags(window_) & SDL_WINDOW_MAXIMIZED) != 0;
        for (int k = 0; k < 3; ++k) {
            const float bx = winX + float(k) * winW;
            static const char* const ids[] = {"Minimize##window", "Maximize##window", "Close##window"};
            ImGui::SetCursorScreenPos(ImVec2(bx, o.y));
            const bool clicked = ImGui::InvisibleButton(ids[k], ImVec2(winW, h));
            barSpans_.push_back({bx, bx + winW});
            const bool hovered = ImGui::IsItemHovered();
            if (hovered) dl->AddRectFilled(ImVec2(bx, o.y), ImVec2(bx + winW, o.y + h), k == 2 ? IM_COL32(0xe8, 0x11, 0x23, 0xff) : hoverBg);
            const ImU32 col = hovered && k == 2 ? IM_COL32_WHITE : dim;
            const ImVec2 c(std::floor(bx + winW * 0.5f) + 0.5f, std::floor(o.y + h * 0.5f) + 0.5f);
            const float r = 5.0f * s, t = std::max(1.0f, 1.0f * s);
            if (k == 0) {
                dl->AddLine(ImVec2(c.x - r, c.y), ImVec2(c.x + r, c.y), col, t);
            } else if (k == 1) {
                if (maximized) { // restore: two overlapping squares
                    dl->AddRect(ImVec2(c.x - r, c.y - r + 2.0f * s), ImVec2(c.x + r - 2.0f * s, c.y + r), col, 0.0f, 0, t);
                    dl->AddLine(ImVec2(c.x - r + 2.0f * s, c.y - r), ImVec2(c.x + r, c.y - r), col, t);
                    dl->AddLine(ImVec2(c.x + r, c.y - r), ImVec2(c.x + r, c.y + r - 2.0f * s), col, t);
                } else {
                    dl->AddRect(ImVec2(c.x - r, c.y - r), ImVec2(c.x + r, c.y + r), col, 0.0f, 0, t);
                }
            } else {
                dl->AddLine(ImVec2(c.x - r, c.y - r), ImVec2(c.x + r, c.y + r), col, t);
                dl->AddLine(ImVec2(c.x + r, c.y - r), ImVec2(c.x - r, c.y + r), col, t);
            }
            if (k == 0) {
                if (clicked) SDL_MinimizeWindow(window_);
                ui::HelpText("Minimize", nullptr, "Minimize the SculptForge window.", "Top bar");
            } else if (k == 1) {
                if (clicked) maximized ? SDL_RestoreWindow(window_) : SDL_MaximizeWindow(window_);
                ui::HelpText(maximized ? "Restore" : "Maximize", nullptr, "Fill the screen with the window, or bring it back to its size. "
                             "Double-click the empty top bar does the same.", "Top bar");
            } else {
                if (clicked) requestQuit();
                ui::HelpAction(*this, "file.quit");
            }
        }
    }
    ImGui::End();

    // What the hit test needs: the bar's band and its widgets.
    captionHit_.barTop = o.y;
    captionHit_.barH = h;
    captionHit_.left = o.x;
    captionHit_.right = o.x + W;
    captionHit_.edge = std::max(4.0f, 5.0f * s);
    captionHit_.resizable = true;
    captionHit_.spans = barSpans_;
}

// The stats card: under the chip while the pointer is on either, or pinned.
void App::drawOneSurfaceStats(ImVec2 chipMin, ImVec2 chipMax, bool chipHovered) {
    if (!chipHovered && !statsPinned_ && !statsHover_) return;
    const float s = uiScale_;
    const float w = 290.0f * s;
    ImGui::SetNextWindowPos(ImVec2(chipMax.x - w + 30.0f * s, chipMax.y + 6.0f * s));
    ImGui::SetNextWindowSize(ImVec2(w, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14.0f * s, 10.0f * s));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10.0f * s);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f); // (a hairline: the card sits over the tray)
    ImGui::PushStyleColor(ImGuiCol_WindowBg, theme::shellBg);
    bool cardHovered = false;
    if (ImGui::Begin("##os.statscard", nullptr, kCardFlags & ~ImGuiWindowFlags_AlwaysAutoResize)) {
        cardHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
        sampleMemoryShown();
        const MemoryShown& mem = memShown_;
        size_t tris = 0, verts = 0, micro = 0;
        for (int i = 0; i < doc_.partCount(); ++i) {
            tris += doc_.partMesh(i).triangleCount();
            verts += doc_.partMesh(i).vertexCount();
            if (doc_.parts[size_t(i)].visible) micro += microTriangleCount(doc_.partMesh(i));
        }
        auto row = [&](const char* label, const std::string& value) {
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("%s", tr(label));
            ImGui::SameLine(ImGui::GetContentRegionMax().x - ImGui::CalcTextSize(value.c_str()).x);
            ImGui::TextUnformatted(value.c_str());
            ImGui::LogText("%s %s", tr(label), value.c_str());
        };
        { // the pin, top right
            const ImVec2 p = ImGui::GetCursorScreenPos();
            ImGui::TextDisabled("%s", tr("Stats"));
            ImGui::SameLine(ImGui::GetContentRegionMax().x - 24.0f * s);
            if (ImGui::SmallButton(statsPinned_ ? ICON_LC_PIN_OFF "##os.stats.pin" : ICON_LC_PIN "##os.stats.pin")) statsPinned_ = !statsPinned_;
            ui::HelpText(statsPinned_ ? "Unpin" : "Pin to keep it open", nullptr, "Keep the stats open, or let them close again.", "Stats");
            (void)p;
        }
        row("Triangles", groupedCount(tris));
        if (micro) row("Micro triangles", groupedCount(micro));
        row("Vertices", groupedCount(verts));
        if (mem.appBytes) row("Memory", compactBytes(mem.appBytes) + (mem.freeBytes ? " / " + compactBytes(mem.freeBytes) + " " + tr("free") : std::string()));
        if (mem.gpuTotalMB > mem.gpuFreeMB) row("Graphics memory", compactBytes(size_t(mem.gpuTotalMB - mem.gpuFreeMB) << 20));
        {
            char f[64];
            std::snprintf(f, sizeof f, "%.1f ms  ·  %s %.0f ms", double(frameMeanShown_) * 1000.0, tr("slowest"), double(frameWorstShown_) * 1000.0);
            row("Frame", f);
        }
        row("Parts  ·  undo steps", std::to_string(doc_.partCount()) + "  ·  " + std::to_string(doc_.undo.undoCount()));
        const ShownVersion v = appShownVersion();
        row("Version", v.number + (v.tag.empty() ? std::string() : " " + v.tag));
        if (const std::string tier = tierChipLabel(); !tier.empty()) {
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("%s", tr("Performance tier"));
            ImGui::SameLine(ImGui::GetContentRegionMax().x - ImGui::CalcTextSize(tr(tier.c_str())).x - ImGui::GetStyle().FramePadding.x * 2.0f);
            if (ImGui::SmallButton((std::string(tr(tier.c_str())) + "##os.tier").c_str())) openPerformancePrefs();
            ui::HelpText("Performance tier", nullptr, "The hardware tier SculptForge runs at on this computer. Click to change it.", "Stats");
        }
        if (const Action* turbo = actions_.find("view.turbo")) {
            bool on = turbo->checked && turbo->checked();
            if (ui::Checkbox("Turbo##os.turbo", &on) && turbo->run) {
                actions_.touch(turbo->id);
                turbo->run();
            }
            ui::HelpAction(*this, "view.turbo");
        }
        if (!statsPinned_) redrawIn(0.5); // (the numbers move while it is open)
    }
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(3);
    statsHover_ = chipHovered || cardHovered;
    (void)chipMin;
}

// The viewport as a rounded opening in the surface: its four corners filled with the panel colour.
void App::drawOneSurfaceCorners() {
    const CanvasRect c = shell_.canvas;
    if (c.w <= 0.0f || c.h <= 0.0f) return;
    const float r = 10.0f * uiScale_;
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    const ImU32 col = theme::u32(theme::shellBg);
    const ImVec2 corners[4] = {{c.x, c.y}, {c.x + c.w, c.y}, {c.x + c.w, c.y + c.h}, {c.x, c.y + c.h}};
    const ImVec2 centres[4] = {{c.x + r, c.y + r}, {c.x + c.w - r, c.y + r}, {c.x + c.w - r, c.y + c.h - r}, {c.x + r, c.y + c.h - r}};
    const float pi = 3.14159265f;
    const float start[4] = {pi, 1.5f * pi, 0.0f, 0.5f * pi};
    for (int k = 0; k < 4; ++k) {
        constexpr int kSteps = 8;
        ImVec2 pts[kSteps + 2];
        pts[0] = corners[k];
        for (int i = 0; i <= kSteps; ++i) {
            const float a = start[k] + 0.5f * pi * float(i) / float(kSteps);
            pts[i + 1] = ImVec2(centres[k].x + std::cos(a) * r, centres[k].y + std::sin(a) * r);
        }
        dl->AddConcavePolyFilled(pts, kSteps + 2, col);
    }
}

// What just happened ("Created sphere", an error): a short line over the bottom of the canvas, gone after 3 s (an
// error after 6 s), fading in its last half second. The top bar stays the file, rooms, brush, undo, stats and window.
void App::drawOneSurfaceStatus() {
    const bool customize = layout_.customize;
    const std::string msg = customize ? std::string(tr("Customize: Ctrl+Alt+drag to move or remove items")) : tr(status_);
    if (msg.empty()) return;
    const bool error = !customize && !lastCommandOk_;
    const double life = customize ? 1e9 : error ? 6.0 : 3.0, age = ImGui::GetTime() - statusSince_;
    if (age >= life) return;
    const float alpha = float(std::clamp((life - age) / 0.5, 0.0, 1.0));
    redrawIn(alpha < 1.0f ? 1.0 / 30.0 : life - age - 0.5); // (the fade needs frames; idle until it starts)
    const float s = uiScale_;
    const CanvasRect& c = shell_.canvas;
    const float lift = oneSurfaceToast_ ? 56.0f * s : 0.0f; // above the "new look" line while that shows
    ImGui::SetNextWindowPos(ImVec2(c.x + c.w * 0.5f, c.y + c.h - 16.0f * s - lift), ImGuiCond_Always, ImVec2(0.5f, 1.0f));
    ImGui::SetNextWindowBgAlpha(0.92f * alpha);
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14.0f * s, 7.0f * s));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10.0f * s);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, theme::shellBg);
    if (ImGui::Begin("##os.status", nullptr, kCardFlags | ImGuiWindowFlags_NoInputs)) {
        const std::string shown = elideTo(ImGui::GetFont(), msg, std::max(120.0f * s, c.w * 0.6f));
        ImGui::TextColored(error ? theme::danger : customize ? theme::accent : theme::text, "%s", shown.c_str());
        ImGui::LogText("%s", shown.c_str());
    }
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(4);
}

// After the update: one line, once, over the bottom of the viewport.
void App::drawOneSurfaceToast() {
    if (!oneSurfaceToast_) return;
    if (!oneSurface()) {
        oneSurfaceToast_ = false;
        return;
    }
    if (oneSurfaceToastAt_ < 0.0) oneSurfaceToastAt_ = ImGui::GetTime();
    if (ImGui::GetTime() - oneSurfaceToastAt_ > 20.0) {
        oneSurfaceToast_ = false;
        return;
    }
    redrawIn(20.05 - (ImGui::GetTime() - oneSurfaceToastAt_));
    const float s = uiScale_;
    const CanvasRect& c = shell_.canvas;
    ImGui::SetNextWindowPos(ImVec2(c.x + c.w * 0.5f, c.y + c.h - 16.0f * s), ImGuiCond_Always, ImVec2(0.5f, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14.0f * s, 8.0f * s));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10.0f * s);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, theme::shellBg);
    if (ImGui::Begin("##os.toast", nullptr, kCardFlags)) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(tr("New look: switch back any time"));
        ImGui::SameLine(0.0f, 14.0f * s);
        if (ImGui::Button(tr("Classic look##os.toast"))) {
            if (const Action* a = actions_.find("ui.classic_look"); a && a->run) a->run();
        }
        ui::HelpText("Classic look", nullptr, "Back to the status bar and tiles. Preferences > Interface > Look switches again.", "New look");
        ImGui::SameLine(0.0f, 6.0f * s);
        if (ImGui::Button(tr("Keep it##os.toast"))) oneSurfaceToast_ = false;
    }
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
}

// The QuickMenu's tool rows (one surface): what used to need a trip to the menus, at the pointer.
void App::drawQuickMenuTools() {
    const float s = uiScale_;
    struct Row {
        const char* label;
        const char* shortcutOf; // an action id whose shortcut to show, or nullptr
        int kind;               // 0 run the action, 1 at the menu's point, 2 a Tool section
        const char* what;
    };
    static const Row rows[] = {
        {"Select Part", "part.sculpt_under_cursor", 1, "part"},
        {"Hide face group", "visibility.hide_group", 1, "hide"},
        {"Isolate face group", "visibility.isolate_group", 1, "isolate"},
        {"Frame", "view.frame", 0, "view.frame"},
        {"Mask", nullptr, 2, "tool.masking"},
        {"Face Groups", nullptr, 2, "tool.facegroups"},
        {"Remesh", nullptr, 2, "tool.remesh"},
        {"Search all tools...", "ui.command_palette", 0, "ui.command_palette"},
    };
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float rowW = 210.0f * s, rowH = 28.0f * s;
    for (size_t i = 0; i < std::size(rows); ++i) {
        const Row& r = rows[i];
        if (i % 2) ImGui::SameLine();
        const std::string label = tr(r.label);
        const bool clicked = ImGui::InvisibleButton((label + "##qm.tool." + std::to_string(i)).c_str(), ImVec2(rowW, rowH));
        const bool hovered = ImGui::IsItemHovered();
        const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
        if (hovered) dl->AddRectFilled(a, b, IM_COL32(255, 255, 255, 15), 7.0f * s);
        const float ty = std::floor(a.y + (rowH - ImGui::GetFontSize()) * 0.5f);
        dl->AddText(ImVec2(a.x + 10.0f * s, ty), theme::u32(theme::text), label.c_str());
        std::string right = r.kind == 2 ? std::string(ICON_LC_CHEVRON_RIGHT) : r.shortcutOf ? actions_.shortcutText(r.shortcutOf) : std::string();
        if (const size_t slash = right.find(" / "); slash != std::string::npos) right.resize(slash); // the first chord
        if (!right.empty()) dl->AddText(ImVec2(b.x - 10.0f * s - ImGui::CalcTextSize(right.c_str()).x, ty), theme::u32(theme::textDim), right.c_str());
        ImGui::LogText("%s", label.c_str());
        if (r.shortcutOf && r.kind != 2) ui::HelpAction(*this, r.shortcutOf);
        if (!clicked) continue;
        quickMenu_.open = false;
        if (r.kind == 0) {
            if (const Action* act = actions_.find(r.what); act && act->run) {
                actions_.touch(act->id);
                act->run();
            }
        } else if (r.kind == 1) { // where the menu was opened: the model under it
            const float mx = mouseX_, my = mouseY_;
            mouseX_ = quickMenu_.at.x;
            mouseY_ = quickMenu_.at.y;
            if (std::strcmp(r.what, "part") == 0) sculptPartUnderCursor();
            else hideGroupUnderCursor(std::strcmp(r.what, "isolate") == 0);
            mouseX_ = mx;
            mouseY_ = my;
        } else {
            openToolSection(r.what);
        }
    }
}

} // namespace sf
