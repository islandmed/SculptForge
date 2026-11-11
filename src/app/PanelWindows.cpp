// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Panels in windows of their own, for a second monitor or a pen display (docs/design/detach-panels.md; requested
// by Richard Throgmorton (MakIt3D), Founding Member #1). Each detached panel is an SDL window with its own ImGui
// context: the font atlas, the style, the GL context and the OpenGL3 renderer are the main window's, so the panel
// draws with the same App code the tray does and the main window's coordinates never change.
#include "app/App.h"

#include "app/Demo.h"
#include "app/I18n.h"
#include "app/Shell.h"
#include "app/Theme.h"
#include "app/Ui.h"
#include "app/UserData.h"
#include "core/Image.h"
#include "render/GL.h"

#include <IconsLucide.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_sdl3.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>

namespace sf {

namespace {

constexpr const char* kPalettePrefix = "palette.";
constexpr double kDragRedraw = 1.0 / 30.0; // panels' frame rate while a stroke or navigation drag runs

std::string panelsPath() {
    const std::string dir = userDataDir();
    return dir.empty() ? std::string() : dir + "panels.json";
}

const PaletteDef* paletteOfPanel(std::string_view id) {
    if (id.substr(0, std::strlen(kPalettePrefix)) != kPalettePrefix) return nullptr;
    return findPalette(id.substr(std::strlen(kPalettePrefix)));
}

bool knownPanel(std::string_view id) { return id == "guide" || id == "controls" || paletteOfPanel(id); }

// A panel window's header: its name and the button that puts it back in the main window.
constexpr float kHeaderH = 28.0f;

} // namespace

App::PanelWindow* App::findPanel(std::string_view id) {
    for (PanelWindow& p : panelWindows_)
        if (p.id == id) return &p;
    return nullptr;
}

bool App::panelDetached(std::string_view id) const {
    return std::any_of(panelWindows_.begin(), panelWindows_.end(), [&](const PanelWindow& p) { return p.id == id; });
}

// Windows of their own need a real window system: not the offscreen driver of UI tests, renders and recordings, nor
// the --ui-serve server's shared window. There a palette floats inside the main window instead.
bool App::panelWindowsAvailable() const {
    if (benchPanels_) return true;
    if (sharedWindow_ || !options_.uiTest.empty() || !options_.recordPath.empty() || !options_.demo.empty()) return false;
    const char* driver = SDL_GetCurrentVideoDriver();
    return driver && std::strcmp(driver, "offscreen") != 0 && std::strcmp(driver, "dummy") != 0 &&
           std::strcmp(driver, "evdev") != 0;
}

std::string App::panelTitle(const std::string& id) const {
    if (id == "guide") return tr("User Guide");
    if (id == "controls") return tr("Controls");
    if (const PaletteDef* def = paletteOfPanel(id)) return tr(paletteTitle(*def));
    return id;
}

void App::detachPanel(const std::string& id, bool restoring) {
    if (!knownPanel(id) || panelDetached(id)) {
        if (PanelWindow* p = findPanel(id); p && p->window) SDL_RaiseWindow(p->window);
        return;
    }
    // Never mid-stroke: a new window (and its focus) waits until the stroke or drag ends.
    if (drag_ != Drag::None || doc_.sculptor.strokeActive()) {
        pendingDetach_ = id;
        return;
    }
    const PaletteDef* def = paletteOfPanel(id);
    if (def && dropdown_ == def->id) dropdown_.clear();
    if (!panelWindowsAvailable()) {
        if (!def) { // the User Guide and Controls already float inside the window
            status_ = "Separate windows aren't available here";
            return;
        }
        panelWindows_.push_back({id});
        return;
    }
    const float s = uiScale_;
    PanelPlacement place;
    place.id = id;
    place.w = int((def ? 320.0f : id == "guide" ? 900.0f : 560.0f) * s);
    place.h = int((def ? 560.0f : 680.0f) * s);
    if (auto it = std::find_if(panelPlacements_.begin(), panelPlacements_.end(), [&](const PanelPlacement& p) { return p.id == id; });
        it != panelPlacements_.end()) {
        place = *it;
        int count = 0;
        std::vector<DisplayRect> displays;
        if (SDL_DisplayID* ids = SDL_GetDisplays(&count)) {
            for (int i = 0; i < count; ++i)
                if (SDL_Rect r; SDL_GetDisplayUsableBounds(ids[i], &r)) displays.push_back({r.x, r.y, r.w, r.h});
            SDL_free(ids);
        }
        if (!panelOnDisplays(place, displays)) place.placed = false; // its monitor has gone
    }
    // OpenGL like the main window (glWindowAttributes still holds), hidden until placed.
    SDL_Window* window = SDL_CreateWindow(("SculptForge - " + panelTitle(id)).c_str(), place.w, place.h,
                                          SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_HIDDEN);
    if (!window) {
        SDL_Log("Panel window failed: %s", SDL_GetError());
        if (def) panelWindows_.push_back({id}); // floats inside the main window instead
        else status_ = std::string("Couldn't open a window: ") + SDL_GetError();
        return;
    }
    // A child of the main window: minimised and stacked with it, never under it (Windows: an owned window).
    SDL_SetWindowParent(window, window_);
    if (place.placed) {
        SDL_SetWindowPosition(window, place.x, place.y);
    } else { // inside the main window's top right, ready to be dragged out (Wayland: the compositor decides)
        int mx = 0, my = 0, mw = 0, mh = 0;
        SDL_GetWindowPosition(window_, &mx, &my);
        SDL_GetWindowSize(window_, &mw, &mh);
        int open = 0; // each one more a step down and left, so a second panel doesn't hide the first
        for (const PanelWindow& q : panelWindows_) open += q.window ? 1 : 0;
        const int step = int(36.0f * s) * open;
        SDL_SetWindowPosition(window, mx + std::max(0, mw - place.w - int(340.0f * s) - step), my + int(120.0f * s) + step);
    }

    PanelWindow p;
    p.id = id;
    p.window = window;
    p.windowId = SDL_GetWindowID(window);
    ImGuiContext* main = ImGui::GetCurrentContext();
    ImGuiIO& mainIo = ImGui::GetIO();
    p.ctx = ImGui::CreateContext(mainIo.Fonts); // the main context's atlas: shared, not copied
    ImGui::SetCurrentContext(p.ctx);
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange; // updateMouseCursor sets the one system cursor
    ImGui_ImplSDL3_InitForOpenGL(window, glContext_);
    // The main context's OpenGL3 renderer (shader, font texture): borrowed, handed back in dockPanel.
    io.BackendRendererUserData = mainIo.BackendRendererUserData;
    io.BackendRendererName = mainIo.BackendRendererName;
    io.BackendFlags |= mainIo.BackendFlags & ImGuiBackendFlags_RendererHasVtxOffset;
    ImGui::SetCurrentContext(main);

    if (!restoring && id == "guide" && !guide_.isOpen()) guide_.open();
    if (!restoring && id == "controls") showHelp_ = true;
    // (The User Guide and Controls show once opened: drawPanelWindow.)
    if (def || !restoring) {
        SDL_SetHint(SDL_HINT_WINDOW_ACTIVATE_WHEN_SHOWN, restoring ? "0" : "1");
        SDL_ShowWindow(window);
        SDL_SetHint(SDL_HINT_WINDOW_ACTIVATE_WHEN_SHOWN, nullptr);
        p.shown = true;
    }
    panelWindows_.push_back(p);
    notePanelPlacement(panelWindows_.back());
    savePanelWindows();
    panelsDirty_ = true;
}

void App::dockPanel(const std::string& id) {
    auto it = std::find_if(panelWindows_.begin(), panelWindows_.end(), [&](const PanelWindow& p) { return p.id == id; });
    if (it == panelWindows_.end()) return;
    if (it->window) {
        notePanelPlacement(*it); // (where it was: the next detach opens it there)
        ImGuiContext* main = ImGui::GetCurrentContext();
        ImGui::SetCurrentContext(it->ctx);
        ImGuiIO& io = ImGui::GetIO();
        io.BackendRendererUserData = nullptr; // the main context's, not this one's to free
        io.BackendRendererName = nullptr;
        ImGui_ImplSDL3_Shutdown();
        ImGui::SetCurrentContext(main);
        ImGui::DestroyContext(it->ctx);
        SDL_DestroyWindow(it->window);
    }
    const bool own = it->window != nullptr;
    panelWindows_.erase(it);
    if (own) savePanelWindows();
}

void App::resetPanels() {
    while (!panelWindows_.empty()) dockPanel(panelWindows_.back().id);
    panelPlacements_.clear();
    pendingDetach_.clear();
    savePanelWindows();
}

void App::closePanelWindows() {
    for (const PanelWindow& p : panelWindows_)
        if (p.window) notePanelPlacement(p);
    savePanelWindows(); // with every window still open: what the next start restores
    while (!panelWindows_.empty()) { // (not dockPanel: it would write panels.json without each)
        PanelWindow& p = panelWindows_.back();
        if (p.window) {
            ImGuiContext* main = ImGui::GetCurrentContext();
            ImGui::SetCurrentContext(p.ctx);
            ImGui::GetIO().BackendRendererUserData = nullptr;
            ImGui::GetIO().BackendRendererName = nullptr;
            ImGui_ImplSDL3_Shutdown();
            ImGui::SetCurrentContext(main);
            ImGui::DestroyContext(p.ctx);
            SDL_DestroyWindow(p.window);
        }
        panelWindows_.pop_back();
    }
}

void App::notePanelPlacement(const PanelWindow& p) {
    if (!p.window) return;
    PanelPlacement place;
    place.id = p.id;
    SDL_GetWindowSize(p.window, &place.w, &place.h);
    // Wayland never says where a window is: the compositor places it, so nothing is stored.
    const char* driver = SDL_GetCurrentVideoDriver();
    place.placed = !(driver && std::strcmp(driver, "wayland") == 0) && SDL_GetWindowPosition(p.window, &place.x, &place.y);
    auto it = std::find_if(panelPlacements_.begin(), panelPlacements_.end(), [&](const PanelPlacement& q) { return q.id == p.id; });
    if (it != panelPlacements_.end()) *it = place;
    else panelPlacements_.push_back(place);
}

// panels.json: the open panel windows first (restored at the next start), then the closed ones' last places.
void App::savePanelWindows() {
    panelsSaveAt_ = 0.0;
    if (!rememberWindow_) return; // tests, renders, recordings, proof mode
    const std::string path = panelsPath();
    if (path.empty()) return;
    std::vector<PanelPlacement> list;
    for (const PanelWindow& p : panelWindows_)
        if (p.window)
            for (const PanelPlacement& q : panelPlacements_)
                if (q.id == p.id) list.push_back(q);
    const size_t open = list.size();
    for (const PanelPlacement& q : panelPlacements_)
        if (std::none_of(list.begin(), list.end(), [&](const PanelPlacement& o) { return o.id == q.id; })) list.push_back(q);
    Json j = panelPlacementsToJson(list);
    j.set("open", double(open));
    std::string err;
    if (!writeJsonAtomic(path, j, err)) SDL_Log("%s", err.c_str());
}

void App::restorePanelWindows() {
    // SF_BENCH_PANELS=palette.tool,palette.brush,controls: these in windows of their own, offscreen too, for the
    // frame-time A/B with 0, 1 and 3 panel windows (bench.sh quick SHA SHA with SHA_ENV).
    if (const char* bench = std::getenv("SF_BENCH_PANELS"); bench && *bench) {
        benchPanels_ = true;
        std::string list = bench;
        for (size_t at = 0; at <= list.size();) {
            const size_t comma = std::min(list.find(',', at), list.size());
            if (comma > at) detachPanel(list.substr(at, comma - at));
            at = comma + 1;
        }
        return;
    }
    if (!rememberWindow_ || !panelWindowsAvailable()) return;
    if (options_.resetLayout) { // --reset-layout: every panel in the main window, the windows' old places forgotten
        panelPlacements_.clear();
        savePanelWindows();
        return;
    }
    std::ifstream f(panelsPath());
    if (!f) return;
    Json j;
    std::string err;
    if (!Json::parse(std::string(std::istreambuf_iterator<char>(f), {}), j, err)) return;
    panelPlacements_ = panelPlacementsFromJson(j);
    const size_t open = std::min(panelPlacements_.size(), size_t(std::max(0.0, j["open"].asNumber(0.0))));
    std::vector<std::string> ids;
    for (size_t i = 0; i < open; ++i) ids.push_back(panelPlacements_[i].id);
    // Shown without taking the focus from the main window; the User Guide and Controls come back where they were,
    // closed until opened (F1, Help).
    for (const std::string& id : ids) detachPanel(id, true);
}

// Events for a panel window go to it first: its coordinates are its own, never the canvas's.
App::PanelEvent App::panelWindowEvent(const SDL_Event& e) {
    if (panelWindows_.empty()) return PanelEvent::None;
    SDL_Window* w = SDL_GetWindowFromEvent(&e);
    if (!w || w == window_) return PanelEvent::None;
    PanelWindow* p = nullptr;
    for (PanelWindow& q : panelWindows_)
        if (q.window == w) p = &q;
    if (!p) return PanelEvent::None;
    panelsDirty_ = true;
    switch (e.type) {
    case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
        // A palette goes back where it was; the User Guide and Controls close and stay on their monitor.
        if (p->id == "guide") {
            if (guide_.isOpen()) guide_.toggle();
        } else if (p->id == "controls") {
            showHelp_ = false;
        } else {
            dockPanel(std::string(p->id));
        }
        return PanelEvent::Main;
    case SDL_EVENT_WINDOW_MOVED:
    case SDL_EVENT_WINDOW_RESIZED:
        notePanelPlacement(*p);
        panelsSaveAt_ = double(SDL_GetTicksNS()) * 1e-9 + 0.5;
        redrawIn(0.6);
        return PanelEvent::PanelOnly;
    default: break;
    }
    ImGuiContext* main = ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(p->ctx);
    ImGuiIO& io = ImGui::GetIO();
    bool toMain = false;
    PanelEvent result = PanelEvent::PanelOnly;
    switch (e.type) {
    case SDL_EVENT_PEN_MOTION: // SDL's pen-to-mouse is off (#95): the pen is a mouse to the panel's ImGui
        io.AddMouseSourceEvent(ImGuiMouseSource_Pen);
        io.AddMousePosEvent(e.pmotion.x, e.pmotion.y);
        if (io.MouseDown[0]) result = PanelEvent::Main;
        break;
    case SDL_EVENT_PEN_DOWN:
    case SDL_EVENT_PEN_UP:
        io.AddMouseSourceEvent(ImGuiMouseSource_Pen);
        io.AddMousePosEvent(e.ptouch.x, e.ptouch.y);
        io.AddMouseButtonEvent(0, e.type == SDL_EVENT_PEN_DOWN);
        result = PanelEvent::Main;
        break;
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP:
        // Shortcuts work with a panel focused, unless it's typing into a field; a key's release always goes through.
        toMain = e.type == SDL_EVENT_KEY_UP || !io.WantTextInput;
        ImGui_ImplSDL3_ProcessEvent(&e);
        result = PanelEvent::Main;
        break;
    case SDL_EVENT_MOUSE_MOTION:
        ImGui_ImplSDL3_ProcessEvent(&e);
        if (e.motion.state) result = PanelEvent::Main; // a slider dragged: the view may show it (brush size)
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
    case SDL_EVENT_MOUSE_WHEEL:
    case SDL_EVENT_TEXT_INPUT:
        ImGui_ImplSDL3_ProcessEvent(&e);
        result = PanelEvent::Main;
        break;
    default:
        ImGui_ImplSDL3_ProcessEvent(&e); // focus, enter, leave, exposed...
        break;
    }
    ImGui::SetCurrentContext(main);
    if (toMain) {
        SDL_Event k = e;
        k.key.windowID = SDL_GetWindowID(window_);
        dispatchEvent(k);
    }
    panelsActiveUntil_ = double(SDL_GetTicksNS()) * 1e-9 + std::max(0.75, double(uiPrefs_.tooltipDelay) + 0.5);
    return result;
}

void App::drawPanelWindows(bool force) {
    const double now = double(SDL_GetTicksNS()) * 1e-9;
    if (!pendingDetach_.empty() && drag_ == Drag::None && !doc_.sculptor.strokeActive()) {
        const std::string id = std::move(pendingDetach_);
        pendingDetach_.clear();
        detachPanel(id);
    }
    if (panelsSaveAt_ > 0.0 && now >= panelsSaveAt_) savePanelWindows();
    bool any = false;
    for (const PanelWindow& p : panelWindows_) any = any || p.window;
    if (!any) {
        panelsDirty_ = false;
        return;
    }
    // While a stroke or the view moves, 30 frames a second are plenty for a panel.
    if (!force && (drag_ != Drag::None || doc_.sculptor.strokeActive()) && now - panelsDrawnAt_ < kDragRedraw) return;
    panelsDrawnAt_ = now;
    panelsDirty_ = false;
    for (size_t i = 0; i < panelWindows_.size(); ++i)
        if (panelWindows_[i].window) drawPanelWindow(panelWindows_[i]);
}

void App::drawPanelWindow(PanelWindow& p) {
    // The User Guide and Controls hide while closed and show again when opened (F1, Help), on their monitor.
    const bool visible = p.id == "guide" ? guide_.isOpen() : p.id == "controls" ? showHelp_ : true;
    if (visible != p.shown) {
        p.shown = visible;
        if (visible) {
            SDL_SetHint(SDL_HINT_WINDOW_ACTIVATE_WHEN_SHOWN, "0");
            SDL_ShowWindow(p.window);
            SDL_SetHint(SDL_HINT_WINDOW_ACTIVATE_WHEN_SHOWN, nullptr);
        } else {
            SDL_HideWindow(p.window);
        }
    }
    if (!visible) return;
    int fbW = 0, fbH = 0;
    SDL_GetWindowSizeInPixels(p.window, &fbW, &fbH);
    if (fbW <= 0 || fbH <= 0) return; // minimised
    ImGuiContext* main = ImGui::GetCurrentContext();
    const ImGuiStyle style = ImGui::GetStyle(); // (the look may have changed)
    ImFont* font = ImGui::GetIO().FontDefault;
    ImGui::SetCurrentContext(p.ctx);
    ImGui::GetStyle() = style;
    ImGui::GetIO().FontDefault = font;
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
    panelHost_ = &p;
    const float s = uiScale_;
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float headH = std::round(kHeaderH * s);
    constexpr ImGuiWindowFlags kFill = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                       ImGuiWindowFlags_NoBringToFrontOnFocus;
    // The header: the panel's name, and the button that puts it back in the main window.
    ImGui::SetNextWindowPos(vp->Pos);
    ImGui::SetNextWindowSize(ImVec2(vp->Size.x, headH));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f * s, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, theme::shellBg);
    bool dock = false;
    if (ImGui::Begin("##panelhead", nullptr, kFill | ImGuiWindowFlags_NoScrollbar)) {
        const float y = (headH - ImGui::GetFrameHeight()) * 0.5f;
        ImGui::SetCursorPosY(y);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(panelTitle(p.id).c_str());
        const char* label = tr(ICON_LC_PANEL_TOP "  Back in the main window");
        const float bw = ImGui::CalcTextSize(label).x + ImGui::GetStyle().FramePadding.x * 2.0f;
        ImGui::SameLine(std::max(ImGui::GetCursorPosX(), vp->Size.x - bw - 8.0f * s));
        ImGui::SetCursorPosY(y);
        dock = ImGui::SmallButton(label);
        ui::HelpText("Back in the main window", nullptr,
                     "Put this panel back in the main window. Preferences > Saving & Layouts > Reset panel windows puts all of them back.");
    }
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(3);
    if (p.id == "guide") {
        guide_.draw(*this); // (fillPanelHost places its window)
    } else if (p.id == "controls") {
        drawControlsWindow();
    } else if (const PaletteDef* def = paletteOfPanel(p.id)) {
        ImGui::SetNextWindowPos(ImVec2(vp->Pos.x, vp->Pos.y + headH));
        ImGui::SetNextWindowSize(ImVec2(vp->Size.x, std::max(1.0f, vp->Size.y - headH)));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(std::round(6.0f * s), std::round(6.0f * s)));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, theme::panelBg);
        if (ImGui::Begin("##panelbody", nullptr, kFill & ~ImGuiWindowFlags_NoScrollbar)) {
            Demo::pushScope(def->name);
            if (def->scope) Demo::pushScope(def->scope);
            drawPaletteBody(def->id);
            if (def->scope) Demo::popScope();
            Demo::popScope();
        }
        ImGui::End();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(2);
    }
    panelHost_ = nullptr;
    ImGui::Render();
    p.cursor = ImGui::GetMouseCursor();
    // Drawn with the one GL context; swap interval 0 here, so N panels never wait for N vertical blanks.
    int interval = 1;
    SDL_GL_GetSwapInterval(&interval);
    SDL_GL_MakeCurrent(p.window, glContext_);
    SDL_GL_SetSwapInterval(0);
    GLint boundFb = 0; // (the main frame may leave an offscreen framebuffer bound: the panel draws to its window)
    gl::GetIntegerv(0x8CA6 /* GL_FRAMEBUFFER_BINDING */, &boundFb);
    gl::BindFramebuffer(GL_FRAMEBUFFER, 0);
    gl::Viewport(0, 0, fbW, fbH);
    const ImVec4 bg = theme::panelBg;
    gl::ClearColor(bg.x, bg.y, bg.z, 1.0f);
    gl::Clear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    // SF_PANEL_SNAPSHOT=<prefix>: the panel's first frames as <prefix>-<id>.png (a desktop capture can't read a GL
    // window on an idle Windows console).
    if (const char* shot = std::getenv("SF_PANEL_SNAPSHOT"); shot && *shot && p.frames < 10) {
        Image img;
        img.width = fbW;
        img.height = fbH;
        img.rgba.resize(size_t(fbW) * size_t(fbH) * 4);
        gl::ReadPixels(0, 0, fbW, fbH, GL_RGBA, GL_UNSIGNED_BYTE, img.rgba.data());
        for (int y = 0; y < fbH / 2; ++y) // (GL rows are bottom-up)
            std::swap_ranges(img.rgba.begin() + std::ptrdiff_t(size_t(y) * fbW * 4),
                             img.rgba.begin() + std::ptrdiff_t(size_t(y + 1) * fbW * 4),
                             img.rgba.begin() + std::ptrdiff_t(size_t(fbH - 1 - y) * fbW * 4));
        std::string error;
        img.savePng(std::string(shot) + "-" + p.id + "-fb" + std::to_string(boundFb) + ".png", error);
    }
    ++p.frames;
    SDL_GL_SwapWindow(p.window);
    SDL_GL_MakeCurrent(window_, glContext_);
    SDL_GL_SetSwapInterval(interval);
    gl::BindFramebuffer(GL_FRAMEBUFFER, GLuint(boundFb));
    ImGui::SetCurrentContext(main);
    if (dock) dockPanel(std::string(p.id)); // (last: p goes with it)
}

void App::fillPanelHost(ImGuiWindowFlags& flags) {
    if (!panelHost_) return;
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float headH = std::round(kHeaderH * uiScale_);
    ImGui::SetNextWindowPos(ImVec2(vp->Pos.x, vp->Pos.y + headH), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(vp->Size.x, std::max(1.0f, vp->Size.y - headH)), ImGuiCond_Always);
    ImGui::SetNextWindowSizeConstraints(ImVec2(0.0f, 0.0f), ImVec2(FLT_MAX, FLT_MAX));
    flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
             ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoCollapse;
}

// No window system for windows of their own (offscreen tests): the palette floats inside the main window.
void App::drawFloatingPanels() {
    const float s = uiScale_;
    for (size_t i = 0; i < panelWindows_.size(); ++i) {
        if (panelWindows_[i].window) continue;
        const std::string id = panelWindows_[i].id;
        const PaletteDef* def = paletteOfPanel(id);
        if (!def) continue;
        const CanvasRect c = canvasRect();
        ImGui::SetNextWindowSize(ImVec2(320.0f * s, 480.0f * s), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos(ImVec2(c.x + c.w - 340.0f * s, c.y + 20.0f * s), ImGuiCond_FirstUseEver);
        bool open = true;
        const std::string title = panelTitle(id) + "###float." + id;
        if (ImGui::Begin(title.c_str(), &open, ImGuiWindowFlags_NoCollapse)) {
            Demo::pushScope(def->name);
            if (def->scope) Demo::pushScope(def->scope);
            drawPaletteBody(def->id);
            if (def->scope) Demo::popScope();
            Demo::popScope();
        }
        ImGui::End();
        if (!open) dockPanel(id);
    }
}

void App::registerPanelActions() {
    auto add = [this](const char* id, const char* label, const char* icon, const char* description, std::function<void()> run,
                      std::function<bool()> checked = nullptr) {
        Action a;
        a.id = id;
        a.label = label;
        a.icon = icon;
        a.palette = "Preferences > Saving & Layouts";
        a.description = description;
        a.run = std::move(run);
        a.checked = std::move(checked);
        actions_.add(std::move(a));
    };
    add("panel.detach_guide", "User Guide in its own window", ICON_LC_SQUARE_ARROW_OUT_UP_RIGHT,
        "Open the User Guide in a window of its own, to keep on another monitor. It opens there next time too.",
        [this] {
            if (panelDetached("guide")) dockPanel("guide");
            else detachPanel("guide");
        },
        [this] { return panelDetached("guide"); });
    add("panel.detach_controls", "Controls in its own window", ICON_LC_SQUARE_ARROW_OUT_UP_RIGHT,
        "Open Controls (every shortcut and gesture) in a window of its own, to keep on another monitor.",
        [this] {
            if (panelDetached("controls")) dockPanel("controls");
            else detachPanel("controls");
        },
        [this] { return panelDetached("controls"); });
    add("panel.reset", "Reset panel windows", ICON_LC_PANEL_TOP,
        "Put every panel that is in a window of its own back in the main window, and forget where those windows were.",
        [this] {
            resetPanels();
            status_ = "Every panel is back in the main window";
        });
}

} // namespace sf
