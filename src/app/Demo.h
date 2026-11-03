// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Scripted demos for recording product videos: a timeline of real input
// (mouse moves, clicks, brush strokes, typing) played into the running app,
// with captions and a visible cursor drawn on top. UI elements are found by
// their labels through Dear ImGui's test-engine hooks, so the demo clicks the
// real buttons rather than calling functions behind the UI's back.
//
//   sculptforge --demo sculpt --record sculpt.mp4 --size 1920x1080
//
// The same machinery runs UI tests: a .sfui script of clicks, typing, strokes
// and expectations against the real interface, with screenshots (UiTest.cpp).
//
//   sculptforge --ui-test tests/ui/brush_lab.sfui --ui-out shots/
#pragma once

#include <glm/glm.hpp>
#include <imgui.h>

#include <SDL3/SDL.h>

#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace sf {

class App;

class Demo {
public:
    explicit Demo(App& app) : app_(app) {}

    static std::vector<std::string> names();
    bool load(const std::string& name, std::string& error);

    void update(float dt);   // before the UI is built: plays the timeline
    void drawOverlay();      // inside the ImGui frame: captions and cursor
    bool finished() const { return step_ >= steps_.size(); }
    float clock() const { return clock_; } // seconds of the script so far (its own time, not the wall clock)
    int frameStride() const { return stride_; } // >1 while fast-forwarding through waits
    // The recording's virtual camera: the part of the window the video shows (its centre and zoom, 1 = all of it),
    // eased between the targets set by zoomTo/zoomToItem. Captions and the fade stay the same size on screen.
    void view(ImVec2& center, float& zoom) const;

    // Screen rect of the most recent UI item with this label (text before "##" also matches;
    // "Window/label" picks the item in one window).
    bool findItem(const std::string& label, ImVec2& center, bool* visible = nullptr, ImVec4* clip = nullptr) const;
    static void beginFrame(); // clears the item table (called before ImGui::NewFrame)
    // Items drawn between these are also found as "NAME/label" (palettes in the shell's trays and drop-downs,
    // whose ImGui window is the tray, keep their panel names: "SubTools/Append").
    static void pushScope(const char* name);
    static void popScope();
    static std::vector<std::string> similarItems(const std::string& label); // for test failure messages

    // UI tests (UiTest.cpp). Steps that fail stop the run; failures() lists why.
    bool loadScript(const std::string& path, std::string& error);
    bool testing() const { return testing_; }
    const std::vector<std::string>& failures() const { return failures_; }
    int stepsRun() const { return int(step_); }
    // Every piece of text drawn in the last frame (ImGui log capture), for expect_text.
    void setFrameText(std::string text) { frameText_ = std::move(text); }
    std::string shortcutsMarkdown() const; // dump_shortcuts: the default shortcuts as Markdown tables
    // A screenshot requested by a `shot` step: the frame loop saves the next frame there.
    std::string takeShotRequest() { return std::exchange(shotRequest_, std::string()); }
    // The last screenshot's pixels (RGBA, top row first), for the luma@X,Y values.
    void setLastShot(std::vector<uint8_t> rgba, int w, int h) {
        lastShot_ = std::move(rgba);
        lastShotW_ = w;
        lastShotH_ = h;
    }

private:
    using Step = std::function<bool(float t)>; // returns true when done; t = seconds since the step began

    // Timeline building blocks.
    void wait(float seconds);
    void caption(const std::string& title, const std::string& subtitle, float seconds, bool top = false);
    void moveTo(float x, float y, float seconds);
    void moveToItem(const std::string& label, float seconds = 0.6f);
    void click(const std::string& label);  // move to the item and click it
    void stroke(std::function<ImVec2(float u)> path, float seconds, bool invert = false);
    void orbit(float dx, float dy, float seconds); // right-drag on the background
    void type(char* buffer, size_t size, const std::string& text, float seconds);
    void run(std::function<void()> action);
    void waitUntil(std::function<bool()> done, float timeout, int stride);
    // Camera moves for the video (they start and the timeline carries on; `seconds` is the ease).
    void zoomTo(ImVec2 center, float zoom, float seconds);
    void zoomToItem(const std::string& label, float zoom, float seconds, ImVec2 offset = ImVec2(0, 0));
    void zoomTo(std::function<ImVec2()> where, float zoom, float seconds); // centre found when the move starts
    void zoomOut(float seconds) { zoomTo(ImVec2(-1, -1), 1.0f, seconds); }
    void fadeTo(float black, float seconds); // 1 = black (fade out), 0 = clear (fade in)

    // UI test steps.
    void fail(int line, const std::string& message);
    bool parseStep(int line, const std::vector<std::string>& words, std::string& error);
    void testClick(int line, const std::string& label, int button, bool doubleClick);
    // phase: 0 press and release, 1 press only (key_down), 2 release only (key_up).
    void testKey(int line, const std::string& combo, int phase = 0);
    void testKeyNow(SDL_Keycode key, SDL_Keymod mods);
    double query(const std::string& name, bool& known) const;

    void buildShowcase();
    void buildSculpt();
    void buildImageTo3D();
    void buildBrushLab();
    void buildGameReady();
    void buildSplats();

    // Input injection.
    void mouseMotion(float x, float y, SDL_MouseID which = 0); // which = SDL_PEN_MOUSEID: synthesized from a pen
    void mouseButton(bool down, int button, SDL_MouseID which = 0, int clicks = 1);
    // Pen events as SDL3 sends them (UiTest.cpp): the pen's own event, then the mouse event SDL synthesizes.
    void penProximity(bool in);
    void penMove(float x, float y);                 // PEN_MOTION (+ mouse motion), touching or hovering
    void penPressure(float pressure);               // PEN_AXIS pressure
    void penTouch(bool down, bool eraser);          // PEN_DOWN / PEN_UP (+ left button)
    void penButtonEvent(int button, bool down);     // PEN_BUTTON_DOWN / UP (+ SDL 3.4's mouse button)
    void penStroke(std::function<ImVec2(float u)> path, float p0, float p1, bool eraser, bool leave, float seconds,
                   std::function<void()> afterSample = {}); // afterSample: e.g. a driver's stray mouse event

    // World -> screen in window coordinates.
    ImVec2 project(const glm::vec3& p) const;
    // A point on the model, given as a fraction of its on-screen bounds (nudged until it hits the surface).
    ImVec2 onModel(float u, float v) const;

    App& app_;
    std::vector<Step> steps_;
    size_t step_ = 0;
    float stepTime_ = 0.0f;
    int stride_ = 1;
    ImVec2 mouse_{960, 540};
    bool buttonDown_ = false;
    bool buttonHeld_ = false; // orbit: right button pressed
    int dragCursor_ = -1;     // the system cursor at the end of the last button_drag, before letting go

    struct Caption {
        std::string title, subtitle;
        float start = 0.0f, duration = 0.0f;
        bool top = false; // upper third (when the lower one has UI to show)
    };
    std::vector<Caption> captions_;
    float clock_ = 0.0f;

    struct View {
        ImVec2 center{-1, -1}; // -1: the middle of the window
        float zoom = 1.0f;
    };
    struct Tween {
        View from, to;
        float start = 0.0f, duration = 0.0f;
    } view_;
    float fadeFrom_ = 0.0f, fadeTo_ = 0.0f, fadeStart_ = 0.0f, fadeDuration_ = 0.0f;
    float fade() const;
    ImVec2 viewCenter(const View& v) const;

    bool testing_ = false;
    std::vector<std::string> failures_;
    std::string frameText_, shotRequest_, shotDir_, scriptName_, lineText_;
    std::vector<uint8_t> lastShot_;
    int lastShotW_ = 0, lastShotH_ = 0;
    std::string scriptDir_; // the .sfui script's folder: relative `drop` paths, $SCRIPT_DIR in env steps
    std::string expandLibrary(std::string text) const; // $LIBRARY -> the LightBox library folder (UiTest.cpp)
    SDL_Keymod held_ = SDL_KMOD_NONE; // modifiers held by `hold` (Ctrl to mask, Ctrl+Shift to hide...)
    // Simulated pen (pen_* steps).
    SDL_PenID testPen_ = 1;        // tablet_plug gives the next pen a new id, as SDL does
    uint32_t testTabletMouse_ = 0; // the simulated tablet's mouse device (tablet_plug / tablet_unplug)
    bool penIn_ = false, penDown_ = false, penEraser_ = false;
    ImVec2 penAt_{960, 540}; // the simulated pen (mouse_ is SDL's shared cursor, which stray mouse events move too)
    SDL_PenInputFlags penState_ = 0;
    // The pen's mouse events as SDL 3.2 makes them (true), or none: SDL after it dropped a touching pen (#95,
    // sdl_pen_mouse / pen_lost). A Huion-style driver also sends mouse events of its own (pen_stream huion).
    bool sdlPenMouse_ = true;
    bool huionStream_ = false;
    static constexpr SDL_MouseID kDriverMouse = 7; // the driver's own mouse device
    // `snapshot` saves the active mesh; moved* values compare against it. `remember` keeps values for $NAME.
    std::vector<glm::vec3> basePositions_, baseNormals_;
    std::map<std::string, double> remembered_;
    // Simulated SpaceMouse (spacemouse* steps): an SDL virtual joystick with a SpaceMouse Compact's ids.
    SDL_JoystickID virtualSpaceMouse_ = 0;
    SDL_Joystick* virtualSpaceMouseHandle_ = nullptr;
    bool attachVirtualSpaceMouse();
    void detachVirtualSpaceMouse();
};

} // namespace sf
