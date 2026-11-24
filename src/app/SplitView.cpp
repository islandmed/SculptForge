// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Split view (#171): a second live view of the same scene beside or below the first, each pane with its own camera.
// The pane under the pointer is the working view: every ray, pick, orbit and frame goes through camera_ and
// canvasRect() as before, and moving the pointer into the other pane swaps the two cameras (never in the middle of a
// drag or stroke). A pane's lens is Free (perspective, orbits as any view) or a fixed axis view (Front, Right, Top:
// orthographic, its direction held every frame; pan and zoom still work). Both panes redraw every frame; while a
// stroke runs or the view moves, the other pane draws at a lower resolution (the floor tier: half) and is stretched
// into place (Renderer::drawView with RenderViewport::multi). Off by default; one key (Shift+V) turns it on and off.
#include "app/App.h"
#include "app/Demo.h"
#include "app/I18n.h"
#include "app/Palettes.h"
#include "app/Theme.h"
#include "app/Ui.h"
#include "core/HardwareProfile.h"

#include <IconsLucide.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace sf {

namespace {

constexpr float kSplitGap = 2.0f;          // unscaled window px between the panes: the shell colour shows through
constexpr float kSplitGrab = 8.0f;         // the divider's drag zone, unscaled px
constexpr float kSplitRatioMin = 0.2f, kSplitRatioMax = 0.8f;
constexpr const char* kLensLabels[4] = {"Free", "Front", "Right", "Top"};
constexpr const char* kLensIds[2][4] = {{"view.split_first_free", "view.split_first_front", "view.split_first_right", "view.split_first_top"},
                                        {"view.split_second_free", "view.split_second_front", "view.split_second_right", "view.split_second_top"}};
constexpr const char* kSplitPath = "View > Split view";

} // namespace

CanvasRect App::splitPaneRect(int pane) const {
    const CanvasRect c = wholeCanvasRect();
    if (!uiPrefs_.splitView) return c;
    pane = std::clamp(pane, 0, 1);
    const float gap = kSplitGap * uiScale_;
    const float ratio = std::clamp(uiPrefs_.splitRatio, kSplitRatioMin, kSplitRatioMax);
    // On whole framebuffer pixels, as wholeCanvasRect is: the drawn pane and the brush rays agree.
    int w = 0, h = 0, pw = 0, ph = 0;
    SDL_GetWindowSize(window_, &w, &h);
    float sx = 1.0f, sy = 1.0f;
    if (w > 0 && h > 0 && SDL_GetWindowSizeInPixels(window_, &pw, &ph) && pw > 0 && ph > 0) {
        sx = float(pw) / float(w);
        sy = float(ph) / float(h);
    }
    if (uiPrefs_.splitView == 1) {
        const float split = std::round((c.x + (c.w - gap) * ratio) * sx) / sx;
        if (pane == 0) return {c.x, c.y, std::max(split - c.x, 1.0f / sx), c.h};
        const float x1 = std::round((split + gap) * sx) / sx;
        return {x1, c.y, std::max(c.x + c.w - x1, 1.0f / sx), c.h};
    }
    const float split = std::round((c.y + (c.h - gap) * ratio) * sy) / sy;
    if (pane == 0) return {c.x, c.y, c.w, std::max(split - c.y, 1.0f / sy)};
    const float y1 = std::round((split + gap) * sy) / sy;
    return {c.x, y1, c.w, std::max(c.y + c.h - y1, 1.0f / sy)};
}

int App::splitPaneAt(float x, float y) const {
    if (!uiPrefs_.splitView) return wholeCanvasRect().contains(x, y) ? 0 : -1;
    for (int p = 0; p < 2; ++p)
        if (splitPaneRect(p).contains(x, y)) return p;
    return -1;
}

void App::splitActivate(int pane) {
    if (!uiPrefs_.splitView || pane == splitActive_ || pane < 0 || pane > 1) return;
    std::swap(camera_, otherCamera_);
    std::swap(autoOrtho_, otherAutoOrtho_);
    splitActive_ = pane;
    updateCameraAspect();
    perf_.lastView = camera_.view(); // (not a camera move: the frame stays still, antialiased, in full detail)
    redrawIn(0.0);
}

// A fixed lens: the same directions as view.front / view.right / view.top (Input.cpp), orthographic.
static void splitLensApply(Camera& c, int lens) {
    if (lens <= 0) return;
    c.yaw = glm::radians(lens == 2 ? 90.0f : 0.0f);
    c.pitch = glm::radians(lens == 3 ? -90.0f : 0.0f);
    c.roll = 0.0f;
    c.ortho = true;
}

// The other pane's camera from this one: the same view, turned to the pane's lens and framed on the scene (a Free
// lens keeps the view as it is).
static void splitOtherFrom(Camera& other, const Camera& from, int lens, const AABB& scene) {
    other = from;
    if (lens) {
        splitLensApply(other, lens);
        other.frame(scene);
    }
}

void App::splitSetMode(int mode) {
    mode = std::clamp(mode, 0, 2);
    if (mode == uiPrefs_.splitView) return;
    const bool wasOff = uiPrefs_.splitView == 0;
    uiPrefs_.splitView = mode;
    if (mode == 0) { // the pointer's view stays, on the whole canvas
        if (uiPrefs_.splitLens[splitActive_]) autoOrtho_ = camera_.ortho; // (a fixed lens's orthographic view turns back to perspective when orbited)
        splitActive_ = 0;
        updateCameraAspect();
        perf_.lastView = camera_.view();
        redrawIn(0.0);
        return;
    }
    if (wasOff) {
        // The view you had becomes the first pane; the other pane starts from the same view, turned to its lens and
        // framed on the scene (a Free lens keeps the view as it is: the same picture twice until you turn one).
        splitActive_ = 0;
        updateCameraAspect();
        const AABB scene = sceneBounds();
        splitOtherFrom(otherCamera_, camera_, uiPrefs_.splitLens[1], scene);
        otherAutoOrtho_ = false;
        splitOtherReady_ = scene.valid(); // (no scene yet, SF_SPLIT_VIEW at start: splitSync frames it once there is one)
    } else updateCameraAspect(); // beside <-> below: the panes change shape, the views stay
    perf_.lastView = camera_.view();
    redrawIn(0.0);
}

void App::splitSetLens(int pane, int lens) {
    pane = std::clamp(pane, 0, 1);
    lens = std::clamp(lens, 0, 3);
    uiPrefs_.splitLens[pane] = lens;
    if (!uiPrefs_.splitView) return;
    Camera& c = pane == splitActive_ ? camera_ : otherCamera_;
    bool& autoOrtho = pane == splitActive_ ? autoOrtho_ : otherAutoOrtho_;
    if (lens) splitLensApply(c, lens);
    else autoOrtho = c.ortho; // Free from an axis view: orthographic until it is turned, as the axis keys leave it
    redrawIn(0.0);
}

void App::splitSwap() {
    std::swap(uiPrefs_.splitLens[0], uiPrefs_.splitLens[1]);
    if (!uiPrefs_.splitView) return;
    std::swap(camera_, otherCamera_);
    std::swap(autoOrtho_, otherAutoOrtho_);
    updateCameraAspect();
    perf_.lastView = camera_.view();
    redrawIn(0.0);
}

void App::splitSync() {
    if (!uiPrefs_.splitView) return;
    if (!splitOtherReady_) { // the split was on in the saved preferences: the other pane starts once there is a scene
        const AABB scene = sceneBounds();
        if (!scene.valid()) return;
        updateCameraAspect();
        splitOtherFrom(otherCamera_, camera_, uiPrefs_.splitLens[1 - splitActive_], scene);
        otherAutoOrtho_ = false;
        splitOtherReady_ = true;
    }
    // A fixed lens holds its direction whatever turned the camera this frame (an orbit drag, the numpad, a space
    // mouse): pan and zoom went through, the turn is undone before the frame is drawn.
    if (uiPrefs_.splitLens[splitActive_]) splitLensApply(camera_, uiPrefs_.splitLens[splitActive_]);
    if (uiPrefs_.splitLens[1 - splitActive_]) splitLensApply(otherCamera_, uiPrefs_.splitLens[1 - splitActive_]);
    updateCameraAspect();
}

// The other pane's resolution while a stroke runs or the view moves: half on the floor tier, three quarters elsewhere
// (SF_SPLIT_RESOLUTION=25..100 overrides, for measuring); full once everything is still.
float App::splitSecondaryResolution() const {
    static const float busy = [] {
        if (const char* v = std::getenv("SF_SPLIT_RESOLUTION"); v && *v) return std::clamp(float(std::atof(v)) / 100.0f, 0.25f, 1.0f);
        return simulatedFloorTier() ? 0.5f : 0.75f;
    }();
    const bool stroking = doc_.sculptor.strokeActive() || strokeFinishing_ || !dabQueue_.empty();
    return (stroking || perf_.movingNow) ? busy : 1.0f;
}

void App::drawSceneViews(int fbW, int fbH, int samples) {
    if (!uiPrefs_.splitView) {
        perf_.samplesUsed = renderer_.drawView(camera_, fbW, fbH, renderSettings_, canvasViewport(fbW, fbH), samples);
        return;
    }
    // The other pane first (lighter while a stroke runs), then the pointer's pane over the rest of the frame, so the
    // renderer's per-frame state (the screen stroke, the focus split, what was drawn) is the working view's.
    Renderer::Viewport other = canvasViewportOf(splitPaneRect(1 - splitActive_), fbW, fbH);
    other.multi = other.secondary = true;
    const float mainScale = renderSettings_.resolutionScale;
    renderSettings_.resolutionScale = std::min(mainScale, splitSecondaryResolution());
    renderer_.drawView(otherCamera_, fbW, fbH, renderSettings_, other, samples);
    renderSettings_.resolutionScale = mainScale;
    Renderer::Viewport mine = canvasViewport(fbW, fbH);
    mine.multi = mine.keep = true;
    perf_.samplesUsed = renderer_.drawView(camera_, fbW, fbH, renderSettings_, mine, samples);
}

void App::drawSceneCapture(int w, int h) {
    if (!uiPrefs_.splitView) {
        renderer_.draw(camera_, w, h, renderSettings_, canvasViewport(w, h));
        return;
    }
    Renderer::Viewport other = canvasViewportOf(splitPaneRect(1 - splitActive_), w, h);
    other.multi = other.secondary = true;
    renderer_.draw(otherCamera_, w, h, renderSettings_, other);
    Renderer::Viewport mine = canvasViewport(w, h);
    mine.multi = mine.keep = true;
    renderer_.draw(camera_, w, h, renderSettings_, mine);
}

// The divider between the panes (drag it to share the canvas differently) and a small chip at each pane's top right
// naming its lens; the chip opens the pane's choices. Both are ImGui windows, so a press on them never reaches the
// canvas.
void App::drawSplitViewUi() {
    if (!uiPrefs_.splitView) return;
    const float s = uiScale_;
    const CanvasRect c = wholeCanvasRect();
    const CanvasRect a = splitPaneRect(0), b = splitPaneRect(1);
    const bool beside = uiPrefs_.splitView == 1;
    constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoSavedSettings |
                                       ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove |
                                       ImGuiWindowFlags_AlwaysAutoResize;
    // The divider's drag zone, over the gap and a few pixels either side.
    {
        const float grab = kSplitGrab * s;
        const ImVec2 pos = beside ? ImVec2(a.x + a.w - grab * 0.5f + kSplitGap * s * 0.5f, c.y) : ImVec2(c.x, a.y + a.h - grab * 0.5f + kSplitGap * s * 0.5f);
        const ImVec2 size = beside ? ImVec2(grab, c.h) : ImVec2(c.w, grab);
        ImGui::SetNextWindowPos(pos, ImGuiCond_Always);
        ImGui::SetNextWindowSize(size, ImGuiCond_Always);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(1, 1));
        if (ImGui::Begin("##splitdivider", nullptr, (flags & ~ImGuiWindowFlags_AlwaysAutoResize) | ImGuiWindowFlags_NoScrollbar)) {
            ImGui::InvisibleButton("##grab", size);
            const bool hot = ImGui::IsItemHovered() || ImGui::IsItemActive();
            if (hot) ImGui::SetMouseCursor(beside ? ImGuiMouseCursor_ResizeEW : ImGuiMouseCursor_ResizeNS);
            if (ImGui::IsItemActive()) {
                const ImVec2 m = ImGui::GetIO().MousePos;
                const float gap = kSplitGap * s;
                const float r = beside ? (m.x - c.x - gap * 0.5f) / std::max(c.w - gap, 1.0f) : (m.y - c.y - gap * 0.5f) / std::max(c.h - gap, 1.0f);
                uiPrefs_.splitRatio = std::clamp(r, kSplitRatioMin, kSplitRatioMax);
                updateCameraAspect();
                redrawIn(0.0);
            }
            if (hot) { // the gap lit while the pointer is on it
                ImDrawList* dl = ImGui::GetForegroundDrawList();
                const ImVec2 g0 = beside ? ImVec2(a.x + a.w, c.y) : ImVec2(c.x, a.y + a.h);
                const ImVec2 g1 = beside ? ImVec2(b.x, c.y + c.h) : ImVec2(c.x + c.w, b.y);
                dl->AddRectFilled(g0, g1, theme::u32(theme::accent));
            }
        }
        ImGui::End();
        ImGui::PopStyleVar(3);
    }
    // One chip per pane: its lens, and on a click the pane's choices.
    for (int p = 0; p < 2; ++p) {
        const CanvasRect r = p == 0 ? a : b;
        const float inset = (beside || p == 0) ? timelineTopInset_ : 0.0f; // (the strip at the canvas's top covers the top pane only)
        ImGui::SetNextWindowPos(ImVec2(r.x + r.w - 8.0f * s, r.y + inset + 8.0f * s), ImGuiCond_Always, ImVec2(1.0f, 0.0f));
        ImGui::SetNextWindowBgAlpha(0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        const char* name = p == 0 ? "##splitchip0" : "##splitchip1";
        if (ImGui::Begin(name, nullptr, flags)) {
            const bool active = p == splitActive_;
            const int lens = std::clamp(uiPrefs_.splitLens[p], 0, 3);
            char label[96];
            std::snprintf(label, sizeof label, "%s %s##lens", tr(kLensLabels[lens]), ICON_LC_CHEVRON_DOWN);
            ImGui::PushStyleColor(ImGuiCol_Button, active ? theme::toggleOn : ImVec4(theme::toggleOn.x, theme::toggleOn.y, theme::toggleOn.z, 0.55f));
            if (!active) ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
            if (ImGui::SmallButton(label)) ImGui::OpenPopup("##splitlens");
            if (!active) ImGui::PopStyleColor();
            ImGui::PopStyleColor();
            ui::HelpText("Split view", nullptr,
                         "This view's direction: Free turns like any view; Front, Right and Top hold that side (orthographic), pan and zoom "
                         "still work. The view under the pointer is the one you sculpt and turn in.",
                         kSplitPath);
            if (ImGui::BeginPopup("##splitlens")) {
                for (int l = 0; l < 4; ++l)
                    if (ImGui::MenuItem(tr(kLensLabels[l]), nullptr, lens == l)) splitSetLens(p, l);
                ImGui::Separator();
                if (ImGui::MenuItem(tr("Swap views"))) splitSwap();
                if (ImGui::MenuItem(tr(beside ? "Below" : "Beside"))) splitSetMode(beside ? 2 : 1);
                if (ImGui::MenuItem(tr("Single view"))) splitSetMode(0);
                ImGui::EndPopup();
            }
        }
        ImGui::End();
        ImGui::PopStyleVar(2);
    }
}

void App::drawSplitViewSection() {
    tray::GroupLabel(*this, "Split view");
    Demo::pushScope("Split view");
    ui::Segmented(*this, {"view.split_off", "view.split_beside", "view.split_below"});
    if (uiPrefs_.splitView) {
        ImGui::TextDisabled("%s", tr(uiPrefs_.splitView == 1 ? "Left view" : "Upper view"));
        ui::Segmented(*this, {kLensIds[0][0], kLensIds[0][1], kLensIds[0][2], kLensIds[0][3]});
        ImGui::TextDisabled("%s", tr(uiPrefs_.splitView == 1 ? "Right view" : "Lower view"));
        ui::Segmented(*this, {kLensIds[1][0], kLensIds[1][1], kLensIds[1][2], kLensIds[1][3]});
        ui::ActionButton(*this, "view.split_swap", ImVec2(-1, 0));
    }
    Demo::popScope();
}

void App::registerSplitViewActions() {
    auto add = [this](const char* id, const char* label, const char* shortcut, const char* description, std::function<void()> run,
                      std::function<bool()> checked = nullptr) {
        Action a;
        a.id = id;
        a.label = label;
        a.palette = kSplitPath;
        a.shortcut = shortcut;
        a.description = description;
        a.run = std::move(run);
        a.checked = std::move(checked);
        actions_.add(std::move(a));
    };
    add("view.split", "Split view", "Shift+V",
        "A second live view of the scene beside this one (View > Split view: beside or below, and each view's direction). The "
        "view under the pointer is the one you sculpt and turn in; the other follows every stroke. Again: one view.",
        [this] { splitSetMode(uiPrefs_.splitView ? 0 : 1); }, [this] { return uiPrefs_.splitView != 0; });
    add("view.split_off", "Off", "", "One view, the whole canvas.", [this] { splitSetMode(0); }, [this] { return uiPrefs_.splitView == 0; });
    add("view.split_beside", "Beside", "", "Two views side by side: left and right.", [this] { splitSetMode(1); },
        [this] { return uiPrefs_.splitView == 1; });
    add("view.split_below", "Below", "", "Two views one above the other.", [this] { splitSetMode(2); }, [this] { return uiPrefs_.splitView == 2; });
    add("view.split_swap", "Swap views", "", "The two views trade places.", [this] { splitSwap(); });
    static constexpr const char* lensHelp[4] = {
        "This view turns like any view (perspective, or orthographic with Numpad 5).",
        "This view looks at the model from the front (orthographic) and keeps that direction; pan and zoom still work.",
        "This view looks at the model's right side (orthographic) and keeps that direction; pan and zoom still work.",
        "This view looks down on the model (orthographic) and keeps that direction; pan and zoom still work."};
    for (int p = 0; p < 2; ++p)
        for (int l = 0; l < 4; ++l)
            add(kLensIds[p][l], kLensLabels[l], "", lensHelp[l], [this, p, l] { splitSetLens(p, l); },
                [this, p, l] { return uiPrefs_.splitLens[p] == l; });
}

} // namespace sf
