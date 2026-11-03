// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ai/AiPolicy.h"
#include "ai/BrushAssistant.h"
#include "app/Actions.h"
#include "app/AssetLibrary.h"
#include "app/AxisGizmo.h"
#include "app/CommandPalette.h"
#include "app/Config.h"
#include "app/Console.h"
#include "app/LightBox.h"
#include "app/Pickers.h"
#include "app/ShellGeometry.h"
#include "app/Support.h"
#include "app/SubtoolThumbnails.h"
#include "app/Thumbnails.h"
#include "app/UiLayout.h"
#include "app/UserGuide.h"
#include "ai/ComfyUI.h"
#include "ai/ImageTo3DService.h"
#include "core/BrushSettings.h"
#include "core/Commands.h"
#include "core/Image.h"
#include "core/Macro.h"
#include "core/Pack.h"
#include "core/Document.h"
#include "core/Face.h"
#include "core/GeneratedRemesh.h"
#include "core/MeshTransform.h"
#include "app/Wintab.h"
#include "core/PenPredictor.h"
#include "core/AutoUv.h"
#include "core/QRemesh.h"
#include "core/RadialMenu.h"
#include "core/Precision.h"
#include "core/StrokePopup.h"
#include "core/Stroke.h"
#include "core/StrokePath.h"
#include "core/SurfaceCursor.h"
#include "core/SpaceMouse.h"
#include "core/Tablet.h"
#include "core/UpdateCheck.h"
#include "core/ViewCapture.h"
#include "core/WhatsNew.h"
#include "core/Texture.h"
#include "io/ArExport.h"
#include "link/PhoneLink.h"
#include "link/Qr.h"
#include "render/Camera.h"
#include "render/Renderer.h"

#include <SDL3/SDL.h>
#include <imgui.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace sf {

struct AppOptions {
    std::string openPath;   // model or image to open at start
    bool blender = false;   // started by the Blender add-on: load what it sent (--blender)
    std::string script;     // commands to run after start-up (same language as sculptforge-cli)
    std::string renderPath; // headless: render a PNG and exit
    int width = 1024, height = 1024;
    float yawDegrees = -30.0f, pitchDegrees = -15.0f;
    float zoom = 1.0f; // headless: camera this many times closer than the framed view
    int turntable = 0; // headless: render N frames around the model (<path>_000.png ...)
    bool wireframe = false;
    bool polygroups = false; // headless: colour each polygroup
    bool hideMesh = false; // headless: show only the splats
    std::string demo;       // scripted demo to play (see Demo.h)
    std::string recordPath; // record the session to this .mp4 (needs ffmpeg) and exit
    std::string demoAsset;  // file the demo opens (image or model)
    std::string demoOutput; // where the demo saves its result (optional)
    int fps = 30;
    int supersample = 1; // recording: render this many times larger, then downsample (crisp text, room to zoom)
    std::string uiTest;   // run this .sfui UI test script headless and exit (see UiTest.cpp)
    std::string uiOutDir; // where UI tests save screenshots
    bool fullscreen = false; // start fullscreen (F11 toggles)
    float measureStroke = 0.0f; // --measure-stroke S: a scripted fast circle stroke, report its path and frame times
    float measureStrokeRadius = 0.0f; // --measure-stroke S,R: the brush radius in screen pixels
    float measureIdle = 0.0f; // --measure-idle S: after a settling pause, count the frames drawn in S idle seconds, exit
    // --timelapse out.mp4: render the opened project's history (its .sfhist) as a turntable timelapse and exit.
    std::string timelapsePath;
    float timelapseSeconds = 20.0f;
    // --benchmark: proof mode (ProofMode.cpp), then exit. Sizes in millions of triangles (empty: 5, 20, 33 and
    // 100 when memory allows); outputs default to the current folder.
    bool benchmark = false;
    std::string benchmarkScene; // --benchmark standard-15m | standard-33m | standard | standard-1b: a standard scene (ProofMode.h)
    std::string benchmarkSizes, benchmarkOut, benchmarkVideo;
    bool benchmarkNoVideo = false, benchmarkVsync = false;
    float benchmarkStrokeSeconds = 6.0f;
};

class Demo;

// Wakes the main loop from any thread (SDL_PushEvent): the app draws only when something changed (#14), so work
// that finishes off the main thread (a file dialog's answer, a phone message) calls this to be seen at once.
void wakeMainLoop();

class App {
    friend class Demo;
    // ZBrush-style shell modules (see ui-spec): they read and drive the app's state directly.
    friend class LightBox;
    friend class ThumbnailCache;
    friend void openPicker(Picker, ImVec2);
    friend void drawPickers(App&);
    friend void drawBrushGrid(App&, GridMode);

public:
    App();
    ~App();
    bool init(const AppOptions& options);
    int run(); // returns the process exit code
    // Redraw on demand (#14): the interactive loop sleeps in SDL_WaitEventTimeout until an event, a background
    // job or an animation needs a new frame. UI tests and recordings (runRecording) draw every frame as before.
    bool backgroundBusy() const; // a background job runs (also the UI tests' wait_idle)

    ActionRegistry& actions() { return actions_; }
    UiLayout& layout() { return layout_; }
    ThumbnailCache& thumbnails() { return thumbs_; }
    void shutdown();
    void requestUiScale(); // Preferences > Interface > UI scale changed: rebuilt live before the next frame (#87)

    enum class FileAction { OpenObj, SaveObj, ExportGlb, ExportLods, SaveBrush, LoadBrush, OpenImage, ExportUsd, SaveSplats, ExportUnreal, SaveProject, AppendSubtool, ComfyWorkflow, LoadMatcap, InsertMesh, Count };
    struct DialogRequest {
        App* app;
        FileAction action;
    };
    // Thread-safe: called from native file dialog callbacks.
    void queueFile(FileAction action, std::string path);

private:
    enum class Drag { None, Stroke, Orbit, Pan, Zoom, Roll, MaskCanvas, SubToolClick, Select, Gizmo, Guide, Sculptable, ZSphere, Insert, Capture,
                      AirStroke, UvTool, SizeScrub }; // AirStroke: a stroke begun off the model, waiting to reach it (#62)

    void handleEvent(const SDL_Event& e); // the pen's mouse events first (penMouseEvents), then dispatchEvent
    void dispatchEvent(const SDL_Event& e);
    void onMouseDown(float x, float y, Uint8 button);
    void onMouseMove(float x, float y, float dx, float dy, bool fromPen);
    void onMouseUp();
    bool blenderNavigationDown(Uint8 button); // Blender style: a press that navigates (Input.cpp's style)
    void orbitBy(float dx, float dy); // a rotation drag: free or turntable (Preferences), Shift snaps
    void rollBy(float x, float y, float dx, float dy); // Shift+click, release Shift, drag: turn about the view axis
    void onKey(SDL_Keycode key, SDL_Keymod mod);

    bool pick(float x, float y, RayHit& hit) const; // the active SubTool (what brushes work on)
    BrushType activeBrush() const;
    void beginStroke(float x, float y, const RayHit& hit);
    // Preferences > Input > Start strokes off the model (#62): a left press on the background waits as
    // Drag::AirStroke; the stroke begins where the drag first reaches the surface (airStrokeTo, Input.cpp).
    bool airStrokeMouseDown(float x, float y, SDL_Keymod mod);
    void airStrokeTo(float x, float y);
    void airStrokeRelease();
    void drawAirStrokeIndicator();
    bool airMask_ = false;            // the waiting stroke masks (Ctrl held at the press)
    bool airMoved_ = false;           // it moved past the click slop (a Ctrl+click still inverts the mask)
    float airX_ = 0.0f, airY_ = 0.0f; // where it was pressed
    void strokeTo(float x, float y);
    void dabAt(float x, float y, float pressure = -1.0f); // pressure < 0: strokePressure()
    void endStroke();
    // Dynamic topology (ZBrush's Sculptris Pro, core/Dyntopo.h): turns it on or off (not on a mesh with
    // subdivision levels), saying so in the status bar; the Detail slider.
    void setDynamic(bool on);
    void drawDetailSlider(const char* id);

    void runCommand(const std::string& name, const Args& args = {});
    void openFileDialog();
    void saveFileDialog(FileAction action);
    AABB sceneBounds() const; // mesh plus the main body of any splats
    bool onScreen(const AABB& box) const; // whole box inside the viewport
    void processPendingFiles();

    void setLayerWeight(int index, float weight);
    void animateLayers(float dt);
    void generateLods();
    void clearLods();
    Mesh& displayedMesh();
    void syncScene(); // uploads every SubTool (the active one as displayedMesh()) and the splats to the renderer

    void drawUi();
    void drawBrushLab();
    void drawImageTo3D();
    void drawComfyUi();
    std::string comfyDefaultWorkflow(bool text);
    void setComfyWorkflow(const std::string& path, bool text);
    bool loadReferenceImage(const std::string& path);
    void projectImageFromView();
    void startGenerator();
    void showHunyuanNoticeOnce(); // the licence notice, once ever (saved in ai.json)
    void drawAiPrefsSection();    // Preferences > AI: the Hugging Face token
    void pollGenerator();
    int renderHeadless();
    void openPath(const std::string& path);
    void drawRecipeEditor(BrushRecipe& recipe);
    void drawAiSettings();
    bool beginPanel(const char* name, bool* open, float width); // ImGui::Begin for auto-sized side panels
    void sendBrushRequest(const std::string& text);
    void pollBrushRequest();
    const BrushRecipe* activeRecipe() const;
    void drawBrushCursor();
    // The brush ring lying on the surface under the pointer (#25, Preferences > Cursor > Cursor follows surface);
    // false when the pointer is off the model (the flat screen ring is drawn instead). `ring` false: only the
    // centre and the symmetry copies' dots (Hide ring while stroking).
    bool drawSurfaceCursor(ImDrawList* dl, float radiusPx, ImU32 col, bool ring);
    std::vector<glm::vec3> cursorRing_, cursorInner_; // scratch, kept to avoid allocating per frame
    bool surfaceCursorShown_ = false; // the last cursor drawn lay on the surface (UI tests: surface_cursor)
    bool ringShown_ = true;           // the last cursor drawn had its ring (UI tests: cursor_ring)
    void updateTitle();

    int runRecording(); // also runs UI tests (options_.uiTest)
    int runUiTest(int width, int height, int fps);
    std::unique_ptr<Demo> demo_;
    float uiScale_ = 1.0f; // UI layout scale (fonts are rasterised at this scale)
    bool uiScaleDirty_ = false;          // the UI scale may have changed: applyUiScale before the next frame
    float displayScaleOverride_ = 0.0f;  // UI tests (display_scale): the display's content scale, 0 = the real one
    float targetUiScale() const;         // what uiScale_ should be now (Preferences, the display; Config.cpp)
    void applyUiScale();                 // rebuild fonts and style live when it changed (between frames)
    void buildUiFonts(float scale);
    ImFont* fontCaption_ = nullptr;
    ImFont* fontCaptionSub_ = nullptr;

    SDL_Window* window_ = nullptr;
    SDL_GLContext glContext_ = nullptr;
    bool running_ = true;
    std::string iniPath_;

    Document doc_;
    CommandRegistry commands_;
    Renderer renderer_;
    RenderSettings renderSettings_;
    Camera camera_;

    // Brush settings
    BrushType brush_ = BrushType::Standard;
    float radiusPx_ = 60.0f;
    float strength_ = 0.5f;
    Symmetry symmetry_;
    // Local symmetry's centre (symmetry_.pivot) follows the active Part: worked out again, outside strokes, when
    // the Part, its vertex count or the undo history changed (refreshSymmetryPivot, PanelMasking.cpp).
    uint64_t symmetryPivotKey_ = ~uint64_t(0);
    void refreshSymmetryPivot();
    // The symmetry arguments of the commands that follow the brushes' symmetry (symmetry, radial, radial_axis,
    // local), added to `args`.
    Args withSymmetryArgs(Args args);
    int symmetryCopiesDrawn_ = 0; // the cursor and its symmetry dots the last drawSurfaceCursor drew (UI tests: cursor_copies)
    bool invertToggle_ = false;
    bool frontFacesOnly_ = false;
    bool pressureStrength_ = true;
    bool pressureSize_ = false;

    // Input / stroke state
    Drag drag_ = Drag::None;
    // Rotation drags: the unsnapped view, so Shift (snap to an axis) can be pressed and released mid-drag.
    Camera orbitRaw_;
    bool orbitStarted_ = false;
    bool panFromAlt_ = false;       // a pan started with Alt: releasing Alt mid-drag zooms (ZBrush)
    bool forceTurntable_ = false;   // demos orbit like a turntable whatever the preference
    float mouseX_ = 0.0f, mouseY_ = 0.0f;
    float lastX_ = 0.0f, lastY_ = 0.0f;
    // Dabs are placed along the pointer's path (strokePath_), queued, and worked through for a limited time per
    // frame in the interactive loop, so a stroke on a huge mesh keeps its curve (core/StrokePath.h).
    StrokePath strokePath_;
    DabQueue dabQueue_;
    std::vector<PathDab> strokeScratch_;
    std::vector<glm::vec2>* strokeProbe_ = nullptr; // --measure-stroke: where each dab went (screen)
    std::vector<float>* strokeRadiusProbe_ = nullptr; // ... and its brush radius (px)
    double dabSeconds_ = 0.0;         // what a dab of this stroke costs (running average), for the backlog limit
    bool interactiveStrokes_ = false; // App::run(): budgeted dabs and curved long gaps; other loops dab at once
    void drainStrokeDabs(double budgetSeconds); // places queued dabs; budget <= 0: all of them

    // --- Strokes on heavy meshes (AsyncStroke.cpp) ---
    // A sculpt stroke's dabs are placed on a solver thread while the main loop keeps drawing at the display's rate:
    // each frame the finished batch of dabs is uploaded (packed on the solver thread, render/Renderer.h) and the
    // next batch starts, and the dabs still waiting are drawn as a GPU preview (the ghost, render/Ghost.h) ahead of
    // the solver, the stroke's head and the brush cursor where the pen will be a frame later (core/PenPredictor.h).
    // The dabs, their order and so the geometry are those of the synchronous path. Preferences > Performance >
    // Predictive strokes (SF_PREDICTIVE_STROKE=0 / 1 overrides it).
    struct StrokeSolver;
    // Everything a dab reads besides the mesh, the BVH and the sculptor (which the solver thread owns while it
    // runs): taken on the main thread, by value, for each batch (dabSettings), so brush size, strength,
    // symmetry, the brush, its alpha, recipe and VDM can change mid-stroke without the solver thread seeing a
    // half-written value (they take effect from the next batch, as they did from the next dab before). The
    // alpha and the recipe are the stroke's own copies (strokeAlpha_, strokeRecipe_), which nothing changes.
    // Never on the solver thread: the painted texture and PBR channels (Mesh::texture: its tiles, shown() and dirty
    // tiles are edited on the main thread only, and the Texture Map panel runs texture commands). Painting strokes
    // stay in the frame loop (asyncStrokeAllowed), and nothing a batch does (dabAt without paint, the sculptor,
    // prepareStrokeUpload, which refuses slots with UVs) reads mesh->texture.
    struct DabSettings {
        float radiusPx = 60.0f, strength = 0.5f, smoothStrength = 0.5f, focal = 0.0f, maskStrength = 1.0f;
        float sprayScatter = 1.0f;
        float worldRadius = 0.0f; // World Size: the brush radius in model units (0: Draw Size in pixels)
        PressurePoints pressureCurve; // the brush's own (after the tablet's global curve)
        bool smoothKeepVolume = false; // Smooth keeps the volume (smoothKeepVolume_, or the alternate smooth)
        bool smoothGroupBorders = false; // Smooth moves face group borders only (smoothGroupBorders_, #81)
        bool pressureSize = false, pressureStrength = true, invert = false, frontFacesOnly = false;
        bool connectedOnly = false, viewFalloff = false; // the falloff options (Brush palette)
        bool mask = false, paint = false, sculpt = true, visible = true;
        AlphaOptions alphaOptions; // Follow stroke direction, Angle, Angle jitter (#76; Tile is the alpha's own)
        BrushType brush = BrushType::Standard;
        StrokeType strokeType = StrokeType::Dots;
        Symmetry symmetry;
        const BrushRecipe* recipe = nullptr;
        const BrushAlpha* alpha = nullptr;
        const VectorDisplacement* vdm = nullptr;
        std::shared_ptr<const VectorDisplacement> vdmOwner; // (keeps the VDM alive for the batch)
    };
    DabSettings dabSettings() const; // the live values (AsyncStroke.cpp)
    std::shared_ptr<const BrushAlpha> strokeAlpha_;   // this stroke's alpha and recipe (predictive strokes)
    std::shared_ptr<const BrushRecipe> strokeRecipe_;
    struct DabView { // what a dab on the solver thread sees (the main thread's may move on)
        Camera camera;
        CanvasRect canvas;
        DabSettings settings;
        std::vector<glm::vec2>* probe = nullptr; // --measure-stroke: where its dabs went
        std::vector<float>* radiusProbe = nullptr; // ... and their brush radius in pixels (verify replays it)
    };
    std::unique_ptr<StrokeSolver> solver_;
    const DabView* dabView_ = nullptr; // set while the solver thread places dabs (dabAt reads it)
    bool asyncStroke_ = false;         // this stroke's dabs go to the solver thread
    bool strokeFinishing_ = false;     // released: the solver places the last dabs, the UI carries on
    bool predictiveStrokes() const;    // the preference (and its environment override)
    bool draftStrokes() const;         // Draft Strokes (SF_DRAFT_STROKE=0 / 1 overrides)
    bool backgroundDabs() const;       // a heavy mesh's dabs on the solver thread without the preview (SF_BACKGROUND_DABS)
    bool asyncStrokeAllowed() const;   // the stroke beginning now may use the solver thread
    bool solverBusy() const;
    void solverWait();                 // waits for the dabs in flight and commits them
    void pumpStroke();                 // per frame: commit what the solver finished, start its next batch
    void commitSolverBatch();
    void finishStrokeNow();            // the rest of endStroke: undo step, timings
    void buildGhost();                 // per frame: the dabs not yet on the GPU, for the renderer's preview
    bool pickWith(const DabView& view, float x, float y, RayHit& hit) const;
    // Where a screen ray meets the surface as the last dabs placed describe it (the ghost's estimate).
    bool ghostSurface(const glm::vec3& origin, const glm::vec3& dir, glm::vec3& at, glm::vec3& normal) const;
    PenPredictor penPredictor_;
    glm::vec2 pointerLatest_{0.0f};   // the newest pointer sample of the stroke (window pixels)
    double pointerLatestAt_ = 0.0;    // its time (seconds)
    double frameSeconds_ = 1.0 / 60.0; // running mean of the frame interval (how far ahead to predict)
    double prepSeconds_ = 0.0;         // running mean of what packing a batch's upload costs (the batch size)
    Sculptor::DabFrame ghostFrame_;    // the last placed dab's frame (the ghost's estimates start from it)
    float ghostEdge_ = 0.0f;           // mean edge length of the triangle the stroke began on (the preview's cavities)
    float ghostCurvature_ = 0.0f;      // the surface's curvature along the stroke (1 / radius, > 0 convex; 0 flat)
    glm::vec2 ghostHead_{0.0f};        // where the preview's head is on screen (--measure-stroke)
    bool ghostHeadValid_ = false;
    uint64_t commits_ = 0; // solver batches committed
    // --measure-stroke SF_MEASURE_POP=1: at each commit the view is drawn offscreen as it was (with the batch's
    // preview), as it is (the batch's result), and as it was without the batch's preview; the mean differences.
    bool popProbe_ = false;
    double popWith_ = 0.0, popWithout_ = 0.0, popWorst_ = 0.0;
    uint64_t popCommits_ = 0;
    glm::vec2 predictedPointer() const; // where the pen will be when this frame shows (the cursor, the head)
    static uint64_t meshStateHash(const Mesh& mesh); // --measure-stroke SF_MEASURE_VERIFY=1
    bool verifyStroke(uint64_t before, const std::vector<glm::vec2>& dabs, const std::vector<float>& radii);
    bool lastInputPen_ = false; // the last mouse event came from a pen (SDL_PEN_MOUSEID)
    bool penSeen_ = false;
    BrushType strokeBrush_ = BrushType::Standard;
    bool strokeInvert_ = false;
    glm::vec3 grabPlanePoint_{0.0f}, grabLast_{0.0f};
    bool grabPending_ = false; // Move: positions moved, normals and BVH not yet (queueGrab / flushGrab, once a frame)
    void queueGrab(const glm::vec3& delta);
    void flushGrab();
    // Along the normal (from the discussions): Move, Tendril, Elastic Tendril and KelvinGrab strokes that began with
    // the invert key held (Preferences > Input > Along the normal) slide along the average normal under the brush
    // at the press instead of the view plane. alongNormalTarget: where the cursor puts the grabbed point.
    bool strokeAlongNormal_ = false;
    glm::vec3 alongNormalDir_{0.0f, 0.0f, 1.0f};
    glm::vec3 alongNormalAnchor_{0.0f};
    float alongNormalStartY_ = 0.0f;
    bool alongNormalBrush(BrushType t) const;
    void beginAlongNormal(const RayHit& hit, float radius);
    glm::vec3 alongNormalTarget(float x, float y) const;

    std::string status_;
    bool lastCommandOk_ = true; // result of the last runCommand (UI tests check it)
    bool showHelp_ = false;
    bool showAbout_ = false;
    // Supporting SculptForge (Support.h): the app only ever opens the membership page or its Stripe checkout links in the
    // browser; nothing else changes.
    void openSupportPage();
    void openDiscordPage(); // kDiscordUrl in the browser (UI tests: the status bar only)
    void openSupportLink(const char* url); // one of the Support.h links (UI tests: the status bar only)
    void drawSupportUi();               // the first-run welcome and the one reminder note
    void drawSupportersSection();       // Help > About: the names from docs/supporters.txt, as plain text
    bool showWelcome_ = false;          // the first start (no project.json yet), or Help > Welcome
    // project.json "input_style": the key style an install started with (the welcome's "Coming from" choice, or
    // SculptForge for a new install), used while ui.json (Store Config) names none; -1: an install from before it,
    // which keeps the ZBrush-style keys it had.
    int startInputStyle_ = -1;
    // "SculptForge has a new look" (0.10.7, PanelSupport.cpp): once, for an install from before it that kept the
    // Classic look or the ZBrush-style layout. Done once either button or its close box is used.
    bool newLookCard_ = false;
    bool newLookCardDone_ = false;
    void chooseNewLook(bool tryIt);
    double sculptSeconds_ = 0.0;        // cumulative time in strokes (project.json)
    bool supportReminderDone_ = false;  // the reminder was shown (or turned off) once: never again (project.json)
    bool supportReminderOpen_ = false;
    int discordRequests_ = 0;           // UI tests: how often the Discord invite was asked for
    int supportPageRequests_ = 0;       // UI tests: how often the page was asked for (only opened when interactive)
    // The support card at launch (PanelSupportCard.cpp): decided once while starting, never mid-session.
    struct SupportCard; // its animation state and the background picture (loaded on a worker thread)
    struct SupportCardDelete { void operator()(SupportCard* c) const; }; // (defined where SupportCard is)
    std::unique_ptr<SupportCard, SupportCardDelete> supportCard_;
    SupportCardPrefs supportCardPrefs_;          // project.json
    SupportCardReason supportCardReason_ = SupportCardReason::None;
    int64_t supportCardNow_ = 0;                 // the clock when this start decided on the card ("Maybe later" counts from it)
    bool supportCardTestMode() const;            // tests, renders, demos, benchmarks, CI: never
    void launchSupportCard(bool firstRun, int64_t now, bool testMode); // shows it if supportCardDue
    void showSupportCard(SupportCardReason reason); // (also Preferences > Support > Show it now)
    void closeSupportCard(bool supporter);
    bool supportCardOpen() const;
    bool supportCardLoading() const; // its picture still decoding (backgroundBusy)
    bool supportCardEvent(const SDL_Event& e); // input while it's open goes to it, not the canvas or shortcuts
    void drawSupportCard();
    void drawSupportPrefs();                    // Preferences > Support
    bool welcomeAfterCard_ = false;             // the first-run welcome waits until the card closes
    // What's New (WhatsNewPopup.cpp): the newest version's feature cards from docs/guide/whats-new.md, once after an
    // update and from Help > What's New; each Try it runs the card's action (runWhatsNewAction).
    struct WhatsNewPopup;
    struct WhatsNewDelete { void operator()(WhatsNewPopup* p) const; };
    std::unique_ptr<WhatsNewPopup, WhatsNewDelete> whatsNew_;
    void showWhatsNew(); // the popup, or the guide's chapter when this version has no cards
    void closeWhatsNew();
    bool whatsNewOpen() const;
    const whatsnew::Version* whatsNewVersion() const; // what the popup shows (UI tests), null when closed
    bool whatsNewEvent(const SDL_Event& e);           // input while it's open goes to it
    void drawWhatsNew();
    // A card's action: look:new, card:membership, brush:NAME, guide:FILE[#ANCHOR] or an action id (help.benchmark).
    bool runWhatsNewAction(const std::string& action);
    int whatsNewTries_ = 0;          // UI tests: Try it clicks
    std::string whatsNewLastAction_; // UI tests: the last card's action

    // Results from asynchronous native file dialogs (called off the main thread).
    struct PendingFile {
        FileAction action;
        std::string path;
    };
    DialogRequest dialogRequests_[int(FileAction::Count)];

    // Preferences > AI (PanelAi.cpp, ai/AiPolicy.h): Enable AI features. Off removes the AI commands, the Brush Lab
    // and Generate 3D entries and closes their windows; saved in ai.json at once (not in tests). SF_AI / --no-ai lock it.
    void setAiFeatures(bool on);
    void registerAiActions(); // ui.brush_lab, ui.generate3d: only while AI is on
    void drawAiPrefs();       // the Preferences > AI section and the first-run question
    // Brush Lab (AI brush designer)
    bool showBrushLab_ = false; // floating; opened from the top shelf's AI Brush or the Brush palette
    bool hideUi_ = false;
    uint64_t seenSplatsVersion_ = 0;
    ai::AiSettings aiSettings_;
    std::string aiSettingsPath_;
    std::vector<ai::ChatMessage> chat_; // what the model sees (append-only, successful turns only)
    struct ChatLine {
        enum class Kind { User, Assistant, Error } kind;
        std::string text;
    };
    std::vector<ChatLine> chatLog_; // what the panel shows
    char chatInput_[1024] = "";
    bool refineCurrent_ = true;
    struct BrushReply {
        ai::BrushSuggestion suggestion;
        std::string raw;
    };
    std::future<BrushReply> pendingBrush_;
    struct ModelList {
        std::vector<std::string> models;
        std::string error;
    };
    std::future<ModelList> pendingModels_; // "Find models" on a local server
    ModelList localModels_;
    bool modelsChecked_ = false;
    float busyTime_ = 0.0f;
    bool scrollChat_ = false;
    // Image to 3D
    AppOptions options_;
    bool showImageTo3D_ = false;
    bool textMode_ = false;
    char textPrompt_[512] = "";
    Image refImage_;
    std::string refImagePath_;
    GLuint refTexture_ = 0;
    float inflateThickness_ = 1.0f;
    int inflateDetail_ = 160;
    float reliefDepth_ = 0.12f;
    int reliefDetail_ = 384;
    bool reliefInvert_ = false;
    float projectDepth_ = 0.02f;
    bool projectInvert_ = false;
    char generatorCommand_[1024] = "";
    char falKey_[256] = "", falModel_[256] = "", meshyKey_[256] = "", hfToken_[256] = "";
    // ComfyUI: "Test connection" result, bundled workflows, the chosen file's problem (UI format...)
    char comfyUrl_[256] = "";
    struct ComfyCheck {
        bool ok = false;
        std::string message;
    };
    std::future<ComfyCheck> pendingComfyCheck_;
    ComfyCheck comfyCheck_;
    bool comfyChecked_ = false;
    std::vector<ai::comfy::BundledWorkflow> comfyWorkflows_;
    bool comfyWorkflowsScanned_ = false;
    bool comfyPickingText_ = false;
    std::string comfyWorkflowError_;
    int generatedRemesh_ = int(GeneratedRemesh::Quad); // Generate 3D's Remesh: Quad (default), Voxel, Off
    int voxelResolution_ = 200;
    bool voxelKeepMasked_ = false; // Voxel Remesh's Keep Masked (#43)
    static constexpr const char* kKeepMaskedHelp =
        "Masked areas keep their triangles exactly as they are (their detail too); only the rest is rebuilt, and "
        "it's joined to them along the mask's edge so the model stays closed. Mask what to keep first (Ctrl). "
        "If the mask's edge is too ragged to join up, the status line says so and nothing changes.";
    bool keepTexture_ = true; // Keep Texture: remeshes carry a painted texture over (core/TextureTransfer.h)
    int remeshResolution_ = 256;
    // A generated model, made sculptable on the worker thread (Quad Remesh takes seconds).
    struct GeneratedModel {
        ai::GeneratorResult result;
        std::shared_ptr<Mesh> remeshed; // null: Remesh off, or it failed (remeshError)
        GeneratedRemeshStats stats;
        std::string remeshError;
    };
    std::future<GeneratedModel> pendingGenerator_;
    bool openHunyuanNotice_ = false;
    std::shared_ptr<ai::GenerationProgress> generatorProgress_;
    float generatorTime_ = 0.0f;

    glm::vec3 lastDabHit_{0.0f};
    bool haveLastDab_ = false;
    uint32_t jitterSeed_ = 1;

    // Sculpt layers / expression preview
    char newLayerName_[64] = "Smile";
    int playingLayer_ = -1;
    float playTime_ = 0.0f;

    // Game export / LOD preview
    int lodLevels_ = 4;
    float lodRatio_ = 0.5f;
    std::vector<Mesh> lods_;
    // Unreal Engine hand-off.
    char unrealDestination_[256] = "/Game/SculptForge";
    float unrealHeightCm_ = 0.0f; // 0 = 1 unit = 1 m
    bool unrealNetwork_ = false;  // editor on another computer
    std::future<std::string> pendingUnreal_; // resolves to the status message
    void sendToUnreal();
    void pollUnreal();
    int previewLod_ = -1;
    const Mesh* lastDisplayed_ = nullptr;
    std::mutex pendingMutex_;
    std::vector<PendingFile> pendingFiles_;

    // Masking (PanelMasking.cpp): Ctrl strokes paint mask, Ctrl+Alt erases;
    // Ctrl+click on the background inverts it, Ctrl+drag there clears it.
    // With MaskLasso / MaskRect, Ctrl+drag draws the shape instead.
    void drawMaskingSection();
    void beginMaskStroke(float x, float y);
    void maskCanvasMove(float x, float y);
    void maskCanvasRelease();
    bool maskSelectMouseDown(float x, float y, SDL_Keymod mod); // true if it took the click
    void maskSelectMouseUp();
    void maskClickRelease(); // Ctrl+click on the model without dragging: blur the mask (Ctrl+Alt: sharpen)
    bool strokeMask_ = false; // the current stroke edits the mask
    float maskStrength_ = 1.0f;
    int maskIterations_ = 1;
    float cavityThreshold_ = 0.02f;
    int maskGroup_ = 0;
    int maskBorderRings_ = 2;        // Tool > Masking > By border: width in edge rings (#61)
    bool maskBorderGroups_ = false;  // ... polygroup borders too
    bool maskBorderSoft_ = false;    // ... fading inwards
    float maskCanvasX_ = 0.0f, maskCanvasY_ = 0.0f;
    bool maskCanvasMoved_ = false;
    // Polygroups & visibility (PanelPolygroups.cpp): Ctrl+Shift click/drag.
    bool polygroupMouseDown(float x, float y, SDL_Keymod mod); // true if it took the click
    void polygroupMouseUp();
    void drawPolygroupsSection();
    // Cut (PanelCut.cpp, core/Cut.h): Ctrl+Shift with a Cut shape. Line, Lasso and Rectangle are dragged; Polyline
    // is clicked point by point (click the first point to close it, double-click or Enter to end it open, Esc drops it).
    void cutMouseUp();
    bool cutPolylineMouseDown(float x, float y, SDL_Keymod mod); // true if it took the click
    bool cutPolylineKey(SDL_Keycode key);                        // Enter / Esc while placing points
    void finishCutPolyline(bool closed);
    void runCut(const char* shape, const std::vector<ImVec2>& path, bool closed, bool flip);
    void drawCutOverlay();
    void drawCutSection();
    bool maskFrontFacesOnly_ = false; // Mask Lasso / Rectangle: only vertices facing the viewer (#91.3)
    int cutMode_ = 0;         // 0 Away, 1 In, 2 Add (core/Cut.h)
    float cutDepth_ = 0.1f;   // In / Add: fraction of the Part's size
    std::vector<ImVec2> cutPolyline_; // window pixels of the Polyline being placed
    // Screen selections (Selection.cpp): the rectangle or lasso dragged for
    // visibility (Ctrl+Shift, ZBrush SelectRect / SelectLasso) or for the mask
    // (Ctrl, MaskRect / MaskLasso). Pen is the mask's brush-stroke mode.
    // The Cut shapes share the Ctrl+Shift drag with SelectRect / SelectLasso (PanelCut.cpp).
    enum class SelectShape { Pen, Lasso, Rect, CutLine, CutLasso, CutRect, CutPolyline };
    static bool isCutShape(SelectShape s) { return s >= SelectShape::CutLine; }
    enum class SelectTarget { Hide, Mask };
    void beginSelection(float x, float y, SelectTarget target, SelectShape shape);
    void selectionMove(float x, float y);
    void selectionMouseUp();
    // Runs the rectangle or lasso command for the dragged shape with its view
    // (points in -1..1); false for a lasso too small to enclose anything.
    bool runSelection(const char* rectCommand, const char* lassoCommand, Args args);
    void drawSelectionOverlay();
    SelectShape maskShape_ = SelectShape::Pen;
    SelectShape hideShape_ = SelectShape::Rect;
    SelectTarget selectTarget_ = SelectTarget::Hide;
    SelectShape selectShape_ = SelectShape::Rect;
    // Window pixels: the start, then the lasso's points (a rectangle's other corner is the cursor).
    std::vector<ImVec2> selectPath_;
    bool selectMoved_ = false; // left the click slop: a drag, not a click
    float polygroupAngle_ = 30.0f;
    // visiblePolygroupCount is O(triangles): recomputed only when the key below changes.
    size_t visibleGroups_ = 0;
    std::array<uint64_t, 6> visibleGroupsKey_{};
    // Stroke types, alphas and the extra brushes (PanelBrushes.cpp).
    void snakeHookTo(float x, float y);
    void dragRectTo(float x, float y);
    glm::vec3 screenUpVector(float angle) const; // camera up rotated by `angle` around the view axis
    static void SDLCALL onAlphaChosen(void* userdata, const char* const* files, int filter);
    StrokeType strokeType_ = StrokeType::Dots;
    bool strokeContinuous_ = false; // Stroke > Continuous (#31): keep dabbing while the pointer rests
    double strokeMovedAt_ = 0.0, continuousDabAt_ = 0.0;
    LazyMouse lazyMouse_;
    // Precision work (Precision.cpp, core/Precision.h). Steady Stroke (L) puts every stroke type's brush on
    // lazyMouse_'s string, not only the Lazy Stroke type; Catch up finishes the line to the pointer on release.
    bool steadyStroke_ = false;
    bool steadyCatchUp_ = true;
    bool steadyOn() const { return steadyStroke_ || strokeType_ == StrokeType::LazyMouse; }
    glm::vec2 steadyCursor_{0.0f}; // where the pointer is (the string's far end) during a steady stroke
    void steadyCatchUp(); // on release: the brush point runs on to the pointer
    // World Size: Draw Size kept in model units, so the brush stays the same size on the surface as you zoom.
    bool worldSize_ = false;
    float worldRadius_ = 0.0f;   // model units (shown in mm)
    float worldSyncedPx_ = 0.0f; // radiusPx_ as worldSize sync last set it (a different value: the user set it)
    float cursorWorldPerPx_ = 0.0f; // at the surface under the pointer (0: not seen yet)
    void syncWorldSize();           // every frame, outside strokes
    std::string drawSizeLabel() const; // "Draw Size %.1f px  0.42 mm" (an ImGui slider format)
    std::string drawSizeStatus() const; // "Draw Size 3.5 px (0.42 mm)"
    void drawPrecisionCursor(ImDrawList* dl, ImVec2 cursor, float radius, ImU32 col);
    void drawPrecisionOptions(); // the Stroke picker: Steady Stroke, its radius and catch-up, Precision Intensity
    glm::vec3 cursorSurface_{0.0f}; // the surface point last under the cursor (World Size measures there)
    bool haveCursorSurface_ = false;
    float sizeReadoutPx_ = 0.0f;   // the size readout by the brush shows for a moment after a change
    double sizeReadoutUntil_ = 0.0;
    void drawSizeSlider(const char* id, const char* helpPath, const char* help);
    // Shift+Alt held mid-stroke: the brush moves at a quarter of the pen's speed and dabs at Precision Intensity.
    PrecisionDrag precision_;
    bool precisionActive_ = false;
    float precisionIntensity_ = 0.1f;
    bool precisionHeld() const;
    // Ctrl+Shift+right-drag: scrubs the Draw Size finely (Drag::SizeScrub).
    float sizeScrubStart_ = 0.0f, sizeScrubDx_ = 0.0f;
    // The surface pick under each dab, filtered along its ray so dense meshes don't wobble the stroke.
    PickSmoother pickSmoother_;
    struct DabAccuracy { // this stroke's dabs (UI tests): how far they land from the cursor, how much they wobble
        float maxErrorPx = 0.0f;
        double jitterRaw = 0.0, jitterFiltered = 0.0; // mean |second difference| of the depth, in brush radii
        int count = 0;
        float raw[2] = {0, 0}, filtered[2] = {0, 0};
        void add(float t, float f, float radius) {
            if (count >= 2 && radius > 0.0f) {
                jitterRaw += std::abs(t - 2.0f * raw[1] + raw[0]) / radius;
                jitterFiltered += std::abs(f - 2.0f * filtered[1] + filtered[0]) / radius;
            }
            raw[0] = raw[1], raw[1] = t, filtered[0] = filtered[1], filtered[1] = f;
            ++count;
        }
    } dabAccuracy_;
    // Each brush's own pressure curve (Brush Lab, Preferences > Tablet), keyed as per-brush settings are (#78:
    // "builtin:<brush>", "custom:<recipe>"); <app data>/brush-curves.json keeps them.
    std::map<std::string, PressurePoints> brushCurves_;
    std::string pressureCurveKey() const;
    std::string pressureCurveKey(BrushType type, const BrushRecipe* recipe) const;
    std::string brushCurvesPath() const; // empty in tests and recordings
    PressurePoints strokeCurve_;  // the curve of the brush this stroke draws with
    PressurePoints& brushCurve(); // the current brush's (linear until edited)
    const PressurePoints* activeBrushCurve() const;
    void brushCurvesChanged();
    void loadBrushCurves();
    void drawPressureCurveEditor(const char* id, float width);
    int curveDragPoint_ = -1;
    Spray spray_;
    float sprayScatter_ = 1.0f;          // jitter radius as a fraction of brush size
    glm::vec2 dragAnchor_{0.0f};         // DragRect: where the click landed (window pixels)
    glm::vec3 dragAnchorHit_{0.0f};
    float dabRadiusScale_ = 1.0f, dabStrengthScale_ = 1.0f, dabAngle_ = 0.0f; // per-dab overrides
    bool alphaInvert_ = false;
    // Alpha options (#76, core/AlphaOptions.h): the Alpha palette's Follow stroke direction, Angle, Angle jitter and
    // Tile, kept per brush (#78). The rake and the jitter's seed start again with each stroke (dabAt).
    AlphaOptions alphaOptions_;
    AlphaRake alphaRake_;
    uint32_t alphaSeed_ = 1;
    float lastDabAngle_ = 0.0f; // radians (UI tests: last_dab_angle)
    void drawAlphaOptions();
    std::mutex alphaMutex_;
    std::string pendingAlphaPath_; // set by the file dialog thread
    // Vector displacement brushes (PanelVdm.cpp, core/Vdm.h): the Brush palette's Vector Displacement section,
    // its file dialogs and actions, DragDot, and the DragRect stroke the VDM brush brings along (as ZBrush's do).
    void dragDotTo(float x, float y);
    bool vdmBrushActive() const { return brush_ == BrushType::Vdm && doc_.activeBrush < 0 && !paintMode_; }
    void drawVdmSection();
    void registerVdmActions();
    void pollVdm(); // every frame: file dialog results, and the stroke follows the brush
    void afterVdmCommand(const std::string& name, const CommandResult& r);
    // Facial shapes (PanelFace.cpp, core/Face.h): Tool > Facial Shapes (Generate, landmarks clicked on the canvas,
    // the Face test, takes played with the frame clock).
    void drawFaceContents();
    void registerFaceUiActions();
    void generateFaceShapesUi();
    bool hasFaceShapes() const;
    bool faceMouseDown(float x, float y, SDL_Keymod mod);
    void drawFaceOverlay();
    void animateFaceTake(float dt); // every frame (from animateLayers): file dialog results, take playback
    static void SDLCALL onFaceTakeChosen(void* userdata, const char* const* files, int filter);
    FaceFitLandmarks faceLandmarks_;
    bool facePlacing_ = false;
    int facePlaceIndex_ = 0;
    int faceSymmetry_ = 0; // 0 Auto, 1 On, 2 Off
    bool faceSubtools_ = true;
    float facePresetAmount_ = 1.0f;
    MorphAnimation faceTake_;
    std::string faceTakePath_;
    bool faceTakePlaying_ = false, faceTakeLoop_ = true;
    float faceTakeTime_ = 0.0f;
    std::mutex faceTakeMutex_;
    std::string pendingFaceTake_; // set by the file dialog thread
    // Kelvinlet brushes and Alpha / VDM from viewport (PanelKelvinlet.cpp, core/Kelvinlet.h, core/ViewCapture.h).
    bool kelvinStrokeBrush() const; // KelvinGrab / Twist / Scale: the drag drives the brush, not dabs
    void kelvinBegin(const RayHit& hit);
    void kelvinTo(float x, float y);
    void kelvinAdvance();  // applies a bounded part of the move the cursor asked for
    bool kelvinNextStep(KelvinletStep& step); // that part, as a step (false: nothing left)
    bool kelvinPump();     // predictive strokes: hands the next step to the solver thread
    void kelvinCatchUp();  // every frame: the rest of a long move, a frame at a time
    void kelvinFinish();   // the stroke ends: all of it
    void drawKelvinletSection();
    void registerKelvinletActions();
    bool captureMouseDown(float x, float y); // true while Capture from view is armed (it takes the drag)
    void captureMouseMove(float x, float y);
    void captureMouseUp();
    void drawCaptureOverlay(); // the square being dragged and the result window
    void drawCaptureWindow();
    void recapture();
    bool kelvinUiValue(const std::string& name, double& out) const; // UI test values (UiTest.cpp)
    struct CaptureUi {
        bool armed = false, circle = false, open = false, haveFrame = false;
        glm::vec2 anchor{0.0f}, current{0.0f};
        CaptureFrame frame;
        CaptureOptions options;
        BrushAlpha alpha;
        std::shared_ptr<const VectorDisplacement> vdm;
        CaptureInfo alphaInfo, vdmInfo;
        std::string error;
        char name[64] = "Captured";
    } capture_;
    glm::vec3 kelvinCenter_{0.0f};
    float kelvinRadius_ = 0.0f, kelvinLastX_ = 0.0f;
    glm::vec3 kelvinTarget_{0.0f}; // KelvinGrab: where the cursor wants the grabbed point
    float kelvinPending_ = 0.0f;   // Twist / Scale: the turn or scale still to apply
    float kelvinAmount_ = 1.0f;
    bool kelvinMovedThisFrame_ = false; // kelvinTo moved it since the last kelvinCatchUp
    static void SDLCALL onVdmChosen(void* userdata, const char* const* files, int filter);
    static void SDLCALL onVdmExportChosen(void* userdata, const char* const* files, int filter);
    friend void drawVdmGrid(App&, GridMode);
    std::mutex vdmMutex_;
    std::string pendingVdmPath_, pendingVdmExport_; // set by the file dialog threads
    VdmImportOptions vdmImport_;
    int vdmPlaneRes_ = 256, vdmCaptureSize_ = 256;
    char vdmName_[64] = "My VDM";
    bool vdmStrokeAuto_ = false;              // the VDM brush switched the stroke to DragRect
    StrokeType vdmSavedStroke_ = StrokeType::Dots;
    bool vdmWasActive_ = false;
    // Persistent brush library (PanelBrushLibrary.cpp, core/BrushLibrary.h): the Custom brushes list
    // with its right-click menu, paste (Ctrl+V), and saving Brush Lab edits a moment after the last change.
    void initBrushLibrary();
    void drawCustomBrushes();
    void drawBrushLibraryPopups();
    void pasteBrush();
    void installBrushFile(const std::string& path); // opened, dropped or chosen .sfbrush files
    void copyBrushCode(int index);
    void revealBrushFile(int index);
    void watchActiveBrush(); // the active brush as it is now counts as saved
    void flushBrushEdits();  // save recipe edits that are still waiting
    void autosaveBrush();    // per frame: saves recipe edits once they settle
    void saveBrushEdits(int index);
    struct BrushWatch {
        int index = -1;
        std::string name, json;
        bool pending = false;
        double changedAt = 0.0;
    } brushWatch_;
    enum class BrushPopup { None, Rename, Delete } brushPopup_ = BrushPopup::None;
    std::string brushPopupName_;   // the brush the popup is about
    char brushRenameBuf_[64] = "";
    std::string brushPopupError_; // why the last rename failed
    int listedBrush_ = -1;        // active brush when the list was last drawn (to scroll to a new one)
    std::string pendingRename_;    // new name typed in the recipe editor, applied after the panel is drawn
    // Polypaint and MatCap materials (PanelPolypaint.cpp)
    bool paintMode_ = false;  // Paint brush: colour only, no sculpting (ZBrush Rgb on, Zadd off)
    bool paintRgb_ = false;   // also paint the colour while sculpting (ZBrush Rgb + Zadd)
    bool strokePaint_ = false, strokeSculpt_ = true;
    void registerPaintUiCommands();
    std::string matcapFolder() const; // <library>/MatCaps, empty without a library
    void loadMatcapLibrary();         // custom MatCaps from the studio folders and matcapFolder() (start-up)
    void loadMatcapDialog();          // Material > Load MatCap...
    void rescanMatcapFolder();        // loads new / changed images in matcapFolder(), drops deleted ones
    void materialListShown();         // a material list is on screen: rescans when it has just opened
    std::map<std::string, std::pair<uintmax_t, int64_t>> matcapFolderSeen_; // path -> size, time at the last scan
    std::set<std::string> hiddenMatcapFiles_; // removed MatCaps whose file couldn't go to the trash
    int materialListFrame_ = -10;
    void setFlatShading(bool flat); // the scene's shading, and the preference for the next session
    // The view's background (#21): gradient or a flat display colour; also the preference for the next session.
    void setBackground(bool gradient, const glm::vec3& color);
    void drawBackgroundControls(const char* idSuffix); // Preferences > Display and the Render palette
    void drawShadingControls();     // Material palette and Tool > Display: Smooth | Flat, and the SubTool's own
    void drawViewportShadingControls(); // Material palette: Lit Sphere | PBR and the environment
    void paintAt(const DabParams& p);
    void pickColorUnderCursor();
    // First paint stroke on a mesh without polypaint: undo swaps this paint stack (no colours) back in, so the
    // attribute goes away again.
    std::unique_ptr<PaintStackUndo> paintSnapshot_;
    void beginPaintStroke();
    void finishPaintStroke(UndoRecord& record);
    TexturePainter texturePainter_; // strokes into the SubTool's texture (core/Texture.h)
    // Texture Map sub-palette (PanelTexture.cpp): texture painting on UVs, Auto UV, the UV view.
    void drawTextureMapContents();
    void registerTextureUiActions();
    // UV Editor (PanelUvEditor.cpp): the window with the 2D UV layout and its tools, the 3D Seam / Select tool, the
    // seams and colours on the model, a background unwrap. UvEditorUi (that file) does the work.
    friend struct UvEditorUi;
    void registerUvEditorActions();
    void drawUvEditor();
    void drawUvEditorOverlay(); // the marked seams on the model, the 3D tool's cursor
    void pollUvEditor();
    void uvEditorChanged(); // a uv_* command ran: the editor's caches are stale
    bool uvEditorKey(SDL_Keycode key, SDL_Keymod mod); // keys over the UV view (true: taken)
    bool uvToolActive() const;
    bool uvToolMouseDown(float x, float y, SDL_Keymod mod);
    void uvToolDragTo(float x, float y);
    void uvToolRelease();
    const MeshOverlay* uvEditorOverlay(uint32_t subtool); // what syncScene hands the renderer for the model
    bool uvEditorBusy() const;
    // UI test values: uv_editor_open, uv_islands, uv_seams, uv_selected, uv_select_mode, uv_pins, uv_tiles,
    // uv_stretch_x1000, uv_flipped_x1000, uv_density, uv_max_u, uv_unwrapping, uv_unwraps, uv_tool,
    // uv_view_triangles, uv_overlay; NaN for other names.
    double uvEditorTestValue(const std::string& name);
    std::shared_ptr<struct UvEditorState> uvEditor_;
    bool showUvEditor_ = false;
    void startAutoUv();
    void startNewTexture(); // New Texture on the top shelf / Color palette: Auto UV first when there are no UVs
    void pollAutoUv();
    struct AutoUvJob {
        std::future<bool> done;
        std::shared_ptr<std::atomic<float>> progress;
        std::shared_ptr<std::atomic<bool>> cancel;
        std::shared_ptr<AutoUvResult> result;
        std::shared_ptr<std::string> error;
        uint32_t subtool = 0;            // SubTool id it unwraps
        size_t triangles = 0;            // the mesh must still have this many triangles when it finishes
    };
    std::unique_ptr<AutoUvJob> autoUv_;
    bool textureAfterAutoUv_ = false;    // the running Auto UV came from New Texture: make the texture after it
    int textureSizeChoice_ = 1;          // 1K, 2K, 4K, 8K
    bool showUvView_ = false;            // Texture Map > Show UVs
    // Material channels (PanelPbr.cpp): channel toggles, the solo channel view and map export under Texture Map,
    // the brush material (per-channel values and swatches) in the Color palette, and per-channel chips on the
    // Paint Layers rows (enable, blend, opacity).
    void drawTextureChannelsContents();
    void drawBrushMaterialContents();
    // The channel chips of texture layer `layer` (a row under its name).
    Args drawLayerChannelChips(int layer); // the texture_layer_channel arguments to run (empty: nothing clicked)
    void registerPbrUiActions();
    double benchmarkDrawMs(int frames); // PanelPbr.cpp: UI tests' draw_ms
    double pbrTestValue(const std::string& name) const; // UI test values (UiTest.cpp), NaN when not one of ours
    // Fill layers, Material Recipes and Mesh Maps (PanelFill.cpp).
    struct MeshMapJob;
    std::shared_ptr<MeshMapJob> meshMapJob_; // a Mesh Maps bake on a worker thread (full, or the tiles near a change)
    bool meshMapsWanted_ = false;            // bake the active Part's Mesh Maps in full (Bake, or a Fill layer asked)
    uint64_t meshMapsRevision_ = 0;          // the undo revision the live update last looked at
    std::string meshMapStatus_;
    double lastMeshMapUpdateMs_ = 0.0;
    uint64_t meshMapUpdates_ = 0;
    glm::vec3 fillColorEdit_{1.0f};
    bool fillColorEditing_ = false;
    void updateMeshMaps();      // once a frame (syncScene): starts and lands Mesh Maps bakes
    bool meshMapsBusy() const;
    void drawFillLayerControls(); // Tool > Paint Layers, below the layer list
    double fillTestValue(const std::string& name) const;
    int exportPresetChoice_ = 0;
    // Dense textured meshes: while the Paint brush hovers, the texture painting caches for the area under it are
    // built ahead of the first dab (PanelPbr.cpp; TexturePainter::prewarm).
    void prewarmTexturePaint();
    glm::vec3 prewarmCenter_{1e30f};
    float prewarmRadius_ = 0.0f;
    // Bake sub-palette (PanelBake.cpp): normal / displacement / AO / cavity / colour maps from a high-poly onto
    // the active SubTool's UVs (core/Bake.h), on a worker thread, and the baked normal map shown on the model.
    void drawBakeContents();
    void registerBakeUiActions();
    void startBake();
    void pollBake();
    // The normal map to preview on SubTool `id` (null when none, it is turned off, or the mesh changed).
    const NormalMapView* bakeNormalMap(uint32_t id, const Mesh& mesh);
    bool bakeBusy() const;
    // The last bake's normal map PNG when its SubTool is shown and still fits it (Send to Unreal takes it along).
    std::string bakedNormalMapFile(bool& directX);
    // UI test values: bake_running, baked_maps (maps in the last bake), bake_size, bake_files, normal_map_preview
    // (1 when the active SubTool shows a baked normal map); NaN for other names.
    double bakeTestValue(const std::string& name);
    std::shared_ptr<struct BakeUiState> bake_;
    // Splats > Train from capture (PanelSplatTrain.cpp, ai/SplatTrainer.h): Brush trains a splat from a phone
    // capture in the background; each checkpoint replaces the splats in the viewport (live preview).
    void registerSplatTrainUi();
    void drawSplatTrainSection(); // top of the Splats palette
    void drawSplatTrainOverlay(); // progress over the canvas, the tool download prompt
    void pollSplatTrain();
    bool splatTrainBusy() const;
    // UI test values: splat_training, splat_previews, splat_train_prompt, splat_train_step, splat_train_stage,
    // splat_train_psnr; NaN for other names.
    double splatTrainTestValue(const std::string& name);
    std::shared_ptr<struct SplatTrainUi> splatTrain_;
    std::string lastExportFolder_; // where the last export went: the default folder for baked maps
    // Cached UV wireframe of the active SubTool for the UV view: segments in 0..1 UV space.
    std::vector<glm::vec2> uvLines_;
    uint64_t uvLinesKey_ = 0;
    // Preferences > Performance (PanelPerformance.cpp): antialiasing and fast navigation, multithreading, what the
    // last stroke cost per dab, and memory. viewSamples() picks this frame's 3D view samples.
    void registerPerformanceUiActions();
    void drawPerformanceSection();
    int viewSamples();
    void applyPerformancePrefs();
    void notePerformanceFrame(double syncMs); // after syncScene: upload time, GPU memory warnings
    struct PerfState {
        glm::mat4 lastView{0.0f};
        int framesStill = 0;          // frames since the view last moved
        bool fastNow = false;         // this frame draws without antialiasing (fast navigation)
        bool proxyNow = false;        // this frame draws big meshes' motion proxy (fast navigation)
        bool movingNow = false;       // the view moved in the last few frames (the frame governor counts these)
        size_t sceneTriangles = 0;    // visible triangles in full detail, last frame
        int samplesWanted = 0;        // viewSamples() for the last frame
        int samplesUsed = 0;          // what the renderer managed last frame (fewer when out of memory)
        double strokeSyncMs = 0.0;    // upload time summed over the open stroke's frames
        int strokeFrames = 0;
        SculptTimings lastStroke;     // the last finished stroke
        double lastStrokeSyncMs = 0.0;
        int lastStrokeFrames = 0;
        bool gpuOutOfMemory = false;
    } perf_;
    // Hardware profile (PanelPerformanceProfile.cpp; app/Calibration.h, core/HardwareProfile.h, core/FrameGovernor.h):
    // calibrates on first launch and when the GPU, driver or version changes; the profile section of Preferences >
    // Performance; the adaptive frame governor, fed every frame between FrameBegin and FrameEnd.
    void initHardwareProfile();
    void shutdownHardwareProfile();
    void hardwareProfileFrameBegin();
    void hardwareProfileFrameEnd(); // before the swap: feeds the governor, runs a calibration step
    void drawHardwareProfileOverlay(); // calibration progress over the first frames (Esc skips)
    bool hardwareCalibrating() const;  // a calibration is running (proof mode waits for it)
    void drawPerformanceProfileSection();
    void registerPerformanceProfileUiActions();
    bool hardwareProfileGoverns() const; // the profile's motion proxy threshold applies (else the fixed default)
    void applyHardwareProfile(const struct HardwareProfile& profile, bool announce);
    void finishCalibration();
    std::shared_ptr<struct HwProfileUi> hwProfile_;
    // Splats > Make sculptable (PanelSculptable.cpp, core/Sculptable.h): splat capture -> sculptable SubTool on a
    // worker thread; the crop box and the face landmark review in the viewport.
    void registerSculptableUiActions();
    void drawSculptableSection();
    void startSculptable(int stage); // 0 all, 1 the scan for review, 2 finish the reviewed scan
    void pollSculptable();
    bool sculptableBusy() const;
    Args sculptableArgs(int stage) const;
    void setSculptableCrop(bool on);
    void runSculptableGuides(Args args); // face_guides, keeping the landmarks it reports
    void drawSculptableOverlay();
    bool sculptableMouseDown(float x, float y); // true if it took the click (a landmark, a crop handle)
    void sculptableMouseMove(float x, float y);
    void sculptableMouseUp();
    bool sculptableReviewActive() const;
    bool sculptableHot() const; // the cursor is on a landmark dot or crop handle (no brush ring), or placing landmarks
    bool sculptableLandmarkScreen(int i, ImVec2& out) const; // UI tests: where landmark i is drawn
    double sculptableTestValue(const std::string& name);     // NaN for names it doesn't know
    std::shared_ptr<struct SculptableUiState> sculptable_;
    // Geometry > QRemesh (quad retopology on a worker thread) and SubTool > Project (ProjectAll); PanelRetopo.cpp.
    void registerRetopoUiActions();
    void drawQRemeshContents();
    void drawProjectContents();
    void startQRemesh();
    void drawKeepTextureCheckbox(const char* id, const char* path); // Keep Texture under Remesh and QRemesh (PanelTexture.cpp)
    // Voxel Remesh of a painted SubTool with Keep Texture: the remesh, Auto UV and texture transfer run on a
    // worker thread (PanelRetopo.cpp); otherwise Voxel Remesh is the plain remesh command.
    void startVoxelRemesh();
    void pollVoxelRemesh();
    struct VoxelRemeshJob {
        std::future<bool> done;
        std::shared_ptr<std::atomic<float>> progress;
        std::shared_ptr<std::atomic<bool>> cancel;
        std::shared_ptr<Mesh> result;
        std::shared_ptr<std::string> error, note;
        uint32_t subtool = 0;
        size_t triangles = 0;
        int resolution = 200;
        bool keepMasked = false;
    };
    std::unique_ptr<VoxelRemeshJob> voxelRemesh_;
    void pollQRemesh();
    struct QRemeshJob {
        std::future<bool> done;
        std::shared_ptr<std::atomic<float>> progress;
        std::shared_ptr<std::atomic<bool>> cancel;
        std::shared_ptr<Mesh> result;
        std::shared_ptr<QRemeshStats> stats;
        std::shared_ptr<std::string> error;
        uint32_t subtool = 0;  // SubTool id it remeshes
        size_t triangles = 0;  // the mesh must still have this many triangles when it finishes
        bool keepOriginal = false;
        Args args;             // the equivalent qremesh command, for the console
        std::shared_ptr<std::string> textureNote; // what happened to a painted texture (Keep Texture)
    };
    std::unique_ptr<QRemeshJob> qremesh_;
    struct RetopoSettings {
        int method = 0; // 0 = QRemesh 2 (QRemeshMethod::V2), 1 = Legacy
        int target = 5000;
        float adaptive = 0.5f;
        bool keepGroups = false, detectEdges = true, usePolypaint = false, symmetry = true, keepOriginal = false;
        bool keepMasked = false; // Keep Masked (#43): the masked area stays as it is (core/KeepMasked.h)
        float projectDist = 0.02f;
        int projectBlur = 0;
        bool projectMean = false, projectGeometry = true, projectPolypaint = true;
    } retopo_;
    // The last QRemesh's quality report (shown under the QRemesh controls while that SubTool is active).
    QRemeshStats lastQRemeshStats_;
    uint64_t lastQRemeshSubtool_ = 0;
    bool haveQRemeshStats_ = false;
    void drawQRemeshReport();
    // QRemesh Guides (PanelRetopo.cpp, core/QRemeshGuides.h): the "QRemesh Guides" brush draws curves on the
    // surface for QRemesh to follow. It is a mode beside the brushes: picking any brush (or the Gizmo) leaves it.
    void setGuidesMode(bool on);
    void syncGuidesMode();                                    // leaves the mode when another brush was picked
    bool guideMouseDown(float x, float y, SDL_Keymod mod);    // true if it took the click
    void guideDragTo(float x, float y);
    void guideRelease();
    void drawQRemeshGuides();                                 // the curves over the model, and the one being drawn
    void drawQRemeshGuideControls();                          // Geometry > QRemesh > Guides
    int guideAt(float x, float y) const;                      // the guide drawn under the cursor, or -1
    bool guidesMode_ = false;
    struct GuidesFrom { // the brush picked when the mode started
        BrushType brush = BrushType::Standard;
        int recipe = -1;
        bool paint = false;
    } guidesFrom_;
    std::vector<glm::vec3> guidePath_; // the curve being drawn (points on the surface)
    float guidePathLast_[2] = {0.0f, 0.0f};
    float curvesStrength_ = 50.0f;     // Curves Strength 0-100 (QRemeshOptions::guideStrength x 100)
    // Which guide points face the camera (drawn solid; the others faint), cached per view and guide set.
    std::vector<std::vector<uint8_t>> guideVisible_;
    uint64_t guideVisibleKey_ = 0;
    // ZSpheres (PanelZSpheres.cpp, core/ZSpheres.h): while a ZSphere SubTool is active, clicks on it draw, move,
    // scale or delete spheres instead of sculpting; each drag is one undo step.
    enum class ZSphereMode { Draw, Move, Scale };
    void registerZSphereUiActions();
    void drawZSpheresContents();                          // Tool > ZSpheres
    bool zsphereMouseDown(float x, float y, SDL_Keymod mod); // true if it took the click
    void zsphereDragTo(float x, float y);
    void zsphereRelease();
    void drawZSphereOverlay();                            // the hovered sphere's outline
    bool zsphereActive() const { return !doc_.mesh.zspheres.empty(); }
    int zsphereAt(float x, float y, bool* link = nullptr) const; // sphere (or link's child) under the cursor, or -1
    float worldPerPixel(const glm::vec3& at) const;       // world size of one canvas pixel at `at`'s depth
    ZSphereMode zsphereMode_ = ZSphereMode::Draw;
    bool zspherePlaceChild_ = false; // Draw: the drag moves the new sphere away from its parent (Ctrl does it too)
    int zsphereSelected_ = -1;      // the sphere last clicked or added (Delete Selected, the menu); -1 none
    // The right-click menu on a sphere: armed by a right press on one, opened if it was a click.
    struct ZSphereMenu {
        bool armed = false, open = false;
        int sphere = -1;
        bool link = false;
        float x = 0.0f, y = 0.0f;
    } zsMenu_;
    bool zsphereRightDown(float x, float y); // a right press on a sphere arms the menu
    float deformAmount_[4] = {50.0f, 50.0f, 50.0f, 50.0f}; // Tool > Deformation: Polish, Relax, Smooth, Balloon
    bool deformKeepVolume_ = false;
    float zoomDragAmount(float dx, float dy) const; // Drag::Zoom's step (App.cpp)
    void drawZSphereMenu();
    bool zsMenuShown_ = false; // the menu was drawn this frame (UI tests)
    // Delete, Insert (a joint on the link to its parent) or Set as root on sphere `i`, symmetric, one undo step.
    enum class ZSphereEdit { Delete, Insert, SetRoot };
    void zsphereEdit(ZSphereEdit edit, int i);
    int zsphereSelection() const; // zsphereSelected_ when it is still a sphere, else -1
    int zsphereResolution_ = 128;                         // Make Mesh resolution
    struct ZSphereDrag {
        ZSphereMode op = ZSphereMode::Draw; // what this drag does (a Draw click on a link then moves the new joint)
        int sphere = -1;               // the sphere being drawn, moved or scaled
        std::vector<ZSphere> start;    // the armature when the drag began
        Mesh before;                   // the SubTool's mesh then (the undo step's old state)
        glm::vec3 grab{0.0f};          // Move: the centre's offset from the grab plane's hit
        float x = 0.0f, y = 0.0f;      // where the press was
        bool changed = false;
        bool place = false;            // Draw: the new sphere follows the cursor (Drag places child, Ctrl)
        int added = -1;                // Draw: the new sphere's index
        const char* label = "";
    } zsDrag_;
    const BrushRecipe* strokeRecipe() const; // recipe the current stroke sculpts with (none for Smooth / paint only)
    // ZBrush GoZ bridge and GoZ/STL files (PanelGoz.cpp).
    void drawGozMenu();
    void pollGoz();
    void runGozCommand(const std::string& name, const Args& args = {});
    static void SDLCALL onGozFileChosen(void* userdata, const char* const* files, int filter);
    enum class GozFile { Import, Export, ExportStl, Count };
    struct GozDialog {
        App* app;
        GozFile kind;
    };
    GozDialog gozDialogs_[int(GozFile::Count)];
    std::vector<std::pair<GozFile, std::string>> gozPendingFiles_; // guarded by pendingMutex_
    char gozFolder_[512] = "";   // custom GoZ root (empty = the platform default)
    bool gozAutoReceive_ = false; // import whenever ZBrush rewrites GoZ_ObjectList.txt
    long long gozListTime_ = 0;   // last seen modification time of the object list
    Uint64 gozLastPollNs_ = 0;
    bool gozInitialised_ = false;
    // Blender bridge (PanelBlender.cpp, io/BlenderBridge.h): the add-on's exchange folder, polled once a second.
    void drawBlenderMenu();
    void pollBlender();
    void receiveFromBlender();
    bool blenderInitialised_ = false;
    bool blenderAutoReceive_ = true;   // load whatever Blender sends while SculptForge runs
    int64_t blenderSeenRevision_ = -1; // revision last loaded (or present at start, which is not loaded)
    Uint64 blenderLastPollNs_ = 0, blenderLastBeatNs_ = 0;
    // Maya bridge (PanelMaya.cpp, io/MayaBridge.h, #42): off until the user picks an exchange folder
    // (uiPrefs_.mayaFolder); with Get automatically on, that one folder's manifest is checked once a second.
    void drawMayaMenu();
    void pollMaya();
    void sendToMaya();
    void receiveFromMaya();
    void chooseMayaFolder();
    void setMayaFolder(const std::string& folder);
    int64_t mayaSeenRevision_ = -1;  // revision last loaded (or present when the folder was chosen / at start)
    long long mayaManifestStamp_ = 0; // its modification time and size then (the cheap check before reading it)
    bool mayaInitialised_ = false;
    Uint64 mayaLastPollNs_ = 0;

    // Phone link (PanelPhone.cpp, link/PhoneLink.h): File > Capture from phone. The server runs only while the
    // window is open; captures land in <library>/Captures and run the command for their kind; AR models are
    // exported on a worker; face frames drive face_weights.
    void initPhoneActions();
    void drawPhoneWindow();
    void pollPhone(); // every frame: events, face frames, AR re-exports
    void setPhoneLinkOpen(bool open);
    void handlePhoneCapture(const link::PhoneEvent& e);
    void startPhoneArExport();
    void drawFacePreview(ImVec2 size);
    bool phoneBusy() const; // an AR export is running (UI tests' wait_idle)
    std::unique_ptr<link::PhoneLink> phone_;
    bool showPhone_ = false;
    std::string phoneError_;          // why the link couldn't start
    std::string phoneQrText_;         // what phoneQr_ encodes
    link::QrCode phoneQr_;
    int phoneAddress_ = 0;            // which local address the QR code uses
    bool phoneAutoGenerate_ = true;   // a photo starts Image to 3D at once
    bool phoneLiveAr_ = true;         // re-export after edits while the phone views the model
    bool phoneMirror_ = false;        // swap left and right face shapes
    struct PhoneArJob {
        bool ok = false;
        ArExportResult result;
        std::string error;
    };
    std::future<PhoneArJob> phoneArJob_;
    bool phoneArScene_ = false;       // the last request: whole scene, size
    float phoneArSizeCm_ = 20.0f;
    bool phoneArRequested_ = false;   // the phone asked at least once this session
    uint64_t phoneArSignature_ = 0;   // the scene state the last export was made from
    double phoneArStartedAt_ = -1e9;
    link::FaceFrame phoneFace_;       // the newest face frame (preview)
    uint64_t phoneFaceSerial_ = 0;
    bool phoneFaceCommandMissing_ = false; // face_weights isn't registered (the facs feature isn't merged)
    std::vector<std::string> phoneCaptures_; // folders received this session, newest last
    // Projects, unsaved-changes safety, recent files and autosave (PanelProject.cpp).
    bool interactive() const; // false for --render, --record and demos: never prompt
    void initProject();       // end of init(): recent files, autosave restore offer
    void finishProject();     // clean exit: marks the session closed normally
    void tickProject(float dt);
    void saveProjectAs(bool forceDialog); // Ctrl+S (dialog only when needed) / Ctrl+Shift+S
    void saveProjectTo(const std::string& path);
    void storeProjectSettings(); // camera, render, brush and symmetry -> doc_.projectSettings
    void applyProjectSettings(); // and back, after opening a project
    void afterCommand(const std::string& name, const Args& args, const CommandResult& r);
    // Runs `action` now, or after the Save / Don't Save / Cancel prompt if there are unsaved changes.
    void confirmDiscard(std::function<void()> action);
    void requestQuit();
    void openPathConfirmed(const std::string& path); // prompts first if the file replaces the scene
    void drawProjectPopups();
    void drawFileMenuProjectItems();
    void rememberRecent(const std::string& path);
    void saveProjectPrefs();
    std::string prefDir_;
    std::vector<std::string> recentFiles_;
    std::function<void()> discardAction_;
    bool openUnsavedPopup_ = false;
    bool continueAfterSave_ = false; // run discardAction_ once the Save As dialog saved
    bool offerRestore_ = false;
    bool showWhatsNew_ = false; // the first start after an update shows What's New (after the membership card)
    // Update check (PanelProject.cpp): website/latest.json at most once a day, on a worker thread; a banner when a
    // newer version is out. Off with Preferences > Updates, SF_NO_UPDATE_CHECK, or -DSCULPTFORGE_UPDATE_CHECK=OFF
    // (package managers); SF_UPDATE_FEED points it at another URL or a local file (UI tests).
    struct UpdateJob {
        std::atomic<bool> done{false};
        bool manual = false;
        bool ok = false;
        UpdateInfo info;
        std::string error;
    };
    void startUpdateCheck(bool manual);
    void pollUpdateCheck();
    void drawUpdateBanner();
    void drawUpdatesSection(); // Preferences > Updates
    bool checkUpdates_ = true;
    int64_t updateCheckedAt_ = 0;   // unix time of the last automatic check
    std::string updateSkipped_;     // "Skip this version"
    std::shared_ptr<UpdateJob> updateJob_;
    std::optional<UpdateInfo> updateAvailable_; // shown as the banner until dismissed
    std::string updateStatus_;                  // the last check's result, for Preferences > Updates
    int autosaveMinutes_ = 5; // 0 = off
    float autosaveTimer_ = 0.0f;
    uint64_t autosaveRevision_ = ~uint64_t(0);
    bool autosaveWritten_ = false;
    std::string windowTitle_;
    // SubTools (PanelSubTools.cpp): the panel, and Alt+click in the viewport to select one.
    void drawSubToolPopups();
    // Index of the nearest visible SubTool under the cursor (-1: none); builds stale BVHs on the way.
    int pickSubtool(float x, float y, RayHit* hit = nullptr);
    void subtoolClickMove(float x, float y); // Alt press on another SubTool: dragging turns it into a stroke
    void subtoolClickRelease();              // ... and releasing without dragging selects it
    int subtoolClickTarget_ = -1;
    bool subtoolClickFrame_ = false; // Shift+Alt+click: frame the SubTool as well
    int mouseClicks_ = 1; // SDL's click count of the press being handled (3 = a triple-click, #43)
    bool spaceHeld_ = false; // Space is down with Preferences > Input > Hold Space to move the camera on (#43)
    float subtoolClickX_ = 0.0f, subtoolClickY_ = 0.0f;
    Uint64 subtoolClickNs_ = 0; // when the Alt press on another Part began (a long pen press isn't a click)
    void partSelected(const char* how);
    Uint64 clickClockNs() const; // the status bar names the new active Part and how it was chosen
    uint32_t renamingSubtool_ = 0; // SubTool id being renamed inline (0: none)
    uint32_t listedActiveSubtool_ = 0; // the list scrolls to the active SubTool when it changes
    // SubTool thumbnails (#43): rendered from each SubTool's GPU buffers while the SubTools panel shows, one per frame
    // at most, a change once it has settled (SubtoolThumbnails.h); GL textures by SubTool id.
    SubtoolThumbnailSchedule subtoolThumbSchedule_;
    std::unordered_map<uint32_t, unsigned> subtoolThumbs_;
    bool subtoolPanelShown_ = false; // drawn this frame
    void updateSubtoolThumbnails();  // after syncScene, GL current
    void clearSubtoolThumbnails();
    bool focusRename_ = false;
    char renameBuffer_[128] = "";
    // Not-undoable SubTool commands wait for a confirmation popup.
    struct SubToolConfirm {
        std::string title, message, button, command;
    };
    SubToolConfirm subtoolConfirm_;
    bool openSubtoolConfirm_ = false;
    // Split by polygroups needs more than one group; counted only when the active mesh changes.
    uint64_t splitGroupsKey_ = ~uint64_t(0);
    bool splitGroupsPossible_ = false;
    // Community brushes window (PanelBrushGallery.cpp): browse, search and install shared brushes.
    void drawBrushGallery(); // also finishes background downloads while the window is closed
    void refreshBrushGallery();
    void pollBrushGallery();
    bool galleryBusy() const; // UI tests: wait_idle
    static void destroyBrushGallery(struct BrushGalleryState* state);
    bool showBrushGallery_ = false;
    std::unique_ptr<struct BrushGalleryState, void (*)(struct BrushGalleryState*)> gallery_{nullptr, &destroyBrushGallery};

    // ---- Shell (A): layout, shelves, palettes, trays, theme ----
    float topInset() const;  // window px kept clear at the top (camera notch)
    bool hideBrushCursor_ = false; // demos: camera moves while the cursor is off screen
    float fpsShown_ = -1.0f; // title-strip frame rate, refreshed twice a second
    double fpsShownAt_ = 0.0;
public:
    // The part of the window the 3D view uses (logical px); the whole window while the UI is hidden.
    CanvasRect canvasRect() const;
    ShellGeometry shellGeometry() const; // every shell region for the current window (console rect, trays...)
    // Preferences > Interface > Look: applies uiPrefs_.look (colours, style, fonts, the viewport background).
    void applyLook();
    void drawLookSection(); // Preferences > Interface > Look (Config.cpp)
    void drawLookPicker();  // the first-run welcome's three look cards (PanelSupport.cpp)
    ImFont* smallFont() const { return fontSmall_; } // 12 px captions (shelf labels, tiles)
    ImFont* boldFont() const { return fontBold_; }   // SemiBold 15 (help titles)

private:
    UiLayout layout_ = standardLayout();
    friend bool drawShelfTile(App&, const Action&, ImVec2);
    friend const struct PaletteDef* palettes(int&);
    // Shell.cpp: title strip, palette bar, shelves, trays, dividers, drop-downs, customize drag and drop.
    void registerShellActions();
    void drawShell();
    void drawTitleStrip(const ShellGeometry& g);
    void drawPaletteBar(const ShellGeometry& g);
    // The SculptForge layout (Preferences > Interface > Look): the status bar with the task menus, and a task menu's
    // drop-down (its palettes as sections). Old palette ids keep working: a palette opens in its menu.
    void drawStatusBar(const ShellGeometry& g);
    void drawTaskMenu();
    void drawLookChrome();    // a look's gaps, tile borders and effects (Shell.cpp)
    ShellMetrics shellMetrics() const;
    bool sculptForgeLayout() const { return uiPrefs_.shellLayout == 1; }
    void setShellLayout(int layout); // swaps the shelves to that layout's standard ones
    bool lookDirty_ = false;   // uiPrefs_.look or the layout changed: applied at the start of the next shell
    CanvasRect focusRect_;     // the tile under the pointer (tiled looks highlight its border)
    float menuX_ = 0.0f;       // where the open task menu drops from
    std::map<std::string, std::string> menuSection_; // each task menu's last section (palette id)
    void drawShelf(int shelf, const CanvasRect& rect);
    void drawShelfItem(int shelf, int index, const std::string& item, bool vertical);
    bool shelfItemShown(const std::string& item) const;
    void drawTray(bool right, const CanvasRect& rect);
    void drawDivider(bool right, const CanvasRect& rect);
    void drawPaletteDropdown();
    bool paletteHeader(const char* id, bool inTray, bool right); // false: collapsed
    void drawPaletteBody(const std::string& id);
    void movePaletteToTray(const std::string& id, bool right);
    void removePaletteFromTrays(const std::string& id);
    void finishShelfDrag();
    void noteLayoutChanged();
    void applyPreset(int preset);
    void updateCameraAspect(); // camera_.aspect from the canvas
    // Ray through window point (x, y) of the canvas, and the canvas height for screen-size conversions.
    void canvasRay(float x, float y, glm::vec3& origin, glm::vec3& dir) const;
    float canvasHeight() const { return canvasRect().h; }
    Renderer::Viewport canvasViewport(int fbWidth, int fbHeight) const; // the canvas in framebuffer pixels
    void drawCanvasFrame();
    bool canvasNavButton(const char* id, const char* icon, const char* label, int mode); // right-shelf Move/Zoom/Rotate
    void drawBrushTile(const char* id, ImVec2 size);
    void drawStrokeTile(const char* id, ImVec2 size);
    void drawStrokeIcon(ImDrawList* dl, ImVec2 a, ImVec2 b, StrokeType type); // the shelf tile's picture
    void drawAlphaTile(const char* id, ImVec2 size);
    void drawMaterialTile(const char* id, ImVec2 size);
    void drawColorWidget(float width);
    // Palettes.cpp: every palette body, also used by the trays and drop-downs.
    bool subpalette(const char* id, const char* title); // Tool sub-palette header; true while open
    void drawPalette_Alpha();
    void drawPalette_Brush();
    void drawPalette_Color();
    void drawPalette_Draw();
    void drawPalette_Edit();
    void drawPalette_File();
    void drawPalette_Generate();
    void drawPalette_Help();
    void drawPalette_Material();
    void drawPalette_Preferences();
    void drawPalette_Render();
    void drawPalette_Script();
    void drawPalette_Splats();
    void drawPalette_Stroke();
    void drawPalette_Tool();
    void drawPalette_Transform();
    void drawRadialSymmetryControls(); // Transform > Symmetry: radial symmetry's toggle, count and axis (Palettes.cpp)
    void drawAlphaSection();
    void drawLayersContents();
    // Paint Layers sub-palette (PanelPaintLayers.cpp).
    void drawPaintLayersContents();
    int renamingPaintLayer_ = -2; // layer being renamed inline (-2 = none)
    bool focusPaintRename_ = false;
    char paintRenameBuffer_[64] = "";
    int listedPaintLayer_ = -2;       // active row last scrolled into view
    float paintOpacityDrag_ = -1.0f;  // opacity before the slider drag (-1 = not dragging)
    void drawGameExportContents();
    void drawVisibilitySection();
    // Tool > Selection Sets (PanelSelectionSets.cpp).
    void drawSelectionSetsSection();
    char setNameBuffer_[64] = "Set 1";
    uint32_t setChannels_ = 1 | 2 | 8; // kSetMask | kSetPolygroups | kSetVisibility (core/SelectionSets.h)
    std::string selectedSet_, combineWith_;
    int setMode_ = 0; // Replace, Union, Intersect, Subtract
    float morphAmount_ = 1.0f;
    bool renamingSet_ = false;
    char setRenameBuffer_[64] = "";
    void drawGeometrySection(); // PanelGeometry.cpp: Divide, SDiv, Lower / Higher Res, Del Lower / Higher, Reconstruct
    bool smt_ = true;           // Tool > Geometry > Smt: Divide smooths
    float sdivSlider_ = 1.0f;   // the SDiv slider while dragged (1-based)
    float microSlider_ = 4.0f;  // Tool > Geometry > Micro: the micro level the next Divide to micro makes
    bool sdivHeld_ = false;
    void drawSubToolsContents();
    // PanelExtract.cpp: Tool > SubTool > Extract, Tool > Geometry > Modify Topology (Mirror And Weld) and
    // Tool > Deformation (Mirror), as in ZBrush.
    void drawExtractContents();
    void drawModifyTopologyContents();
    void drawDeformationContents();
    void runExtract(); // Extract with the palette's settings (a preview; Accept keeps it)
    static std::string axesText(const bool axes[3]); // "x", "xz", "" (the axis argument of mirror commands)
    float extractThick_ = 0.015f;          // Thick: fraction of the SubTool's size
    int extractSmooth_ = 5;                // E Smt
    bool extractDouble_ = false;           // Double
    bool extractTCorne_ = false;           // TCorne
    bool extractTBorder_ = false;          // TBorder
    bool mirrorWeldAxes_[3] = {true, false, false}; // Mirror And Weld's X Y Z
    bool mirrorAxes_[3] = {true, false, false};     // Deformation > Mirror's X Y Z
    void pollAlphaFile();
    ShellGeometry shell_;      // this frame's regions (drawShell)
    ImFont* fontSmall_ = nullptr, *fontTiny_ = nullptr, *fontIcons_ = nullptr, *fontBold_ = nullptr;
    // JetBrains Mono (the looks' monospace) and the Inter fonts it stands in for while a look uses it.
    ImFont* fontRegular_ = nullptr, *fontMono_ = nullptr, *fontMonoSmall_ = nullptr, *fontMonoTiny_ = nullptr, *fontMonoBold_ = nullptr;
    ImFont* fontInterSmall_ = nullptr, *fontInterTiny_ = nullptr, *fontInterBold_ = nullptr;
    ImFont* fontCardTitle_ = nullptr, *fontCardHead_ = nullptr, *fontCardBody_ = nullptr; // the support card (Latin only)
    std::string dropdown_;     // palette open as a drop-down ("" = none)
    float dropdownX_ = 0.0f;
    bool openDropdown_ = false;
    float decimateRatio_ = 0.5f;
    bool decimateKeepMasked_ = false; // Decimate's Keep Masked (#43)
    std::string flashPalette_; // a tray palette that was clicked in the bar: scrolled to and flashed
    double flashStart_ = 0.0;
    std::vector<std::string> collapsedPalettes_; // tray palettes showing only their header
    void autoCollapseTray(bool right, const std::string& keep);
    glm::vec3 secondaryColor_{1.0f, 1.0f, 1.0f}; // the left shelf's second swatch (click swaps, "Alternate")
    uint64_t activeTrisKey_ = ~uint64_t(0); // the top shelf's ActiveTris, recounted when the scene changes
    size_t activeTris_ = 0;
    float bottomTileW_ = 58.0f; // bottom shelf tiles shrink (down to 44 px) before the shelf scrolls
    int navDrag_ = -1;          // right-shelf drag button being dragged (0 move, 1 zoom, 2 rotate)
    ImVec2 navLast_{0, 0};      // the cursor at its last frame (the drag moves by the cursor's own steps)
    std::string statusShown_;   // for colouring fresh errors in the title strip
    double statusSince_ = 0.0;
    struct ShelfDrag {
        bool active = false, accepted = false;
        int shelf = -1, index = -1;
    } shelfDrag_;
    // ---- end Shell (A) ----

    // ---- LightBox (B): LightBox, thumbnails, pickers ----
    LightBox lightbox_;
    ThumbnailCache thumbs_;
    friend struct PickerUi;
    friend void drawAlphaGrid(App&, GridMode);
    friend void drawMaterialGrid(App&, GridMode);
    friend void drawStrokeGrid(App&, GridMode);
    void initLightBox();   // end of init(): library folders, thumbnail cache, actions, project thumbnails
    void showStarterMeshes(); // the Library at Tool > Starter meshes (File > New from Starter Mesh, the welcome)
    void drawLightBoxUi(); // every frame from drawUi: thumbnails, the LightBox, pickers, size / strength pop-ups
    // The LightBox / picker shortcuts (, B M S) until the action registry dispatches keys.
    bool lightBoxKey(SDL_Keycode key, SDL_Keymod mod);
    void quickSave();                  // file.quicksave: a timestamped project in <library>/QuickSave
    void saveRender();                 // render.save_image: the view as a PNG in <library>/Images
    LibraryRoots library_;
    FolderScanner libraryScanner_;
    // ---- end LightBox (B) ----

    // ---- Help/Console/Config (C): actions, help, command palette, console, config ----
public:
    UiPrefs& uiPrefs() { return uiPrefs_; }
    const CommandRegistry& commands() const { return commands_; }
    ImFont* helpTitleFont() const { return fontHelpTitle_ ? fontHelpTitle_ : ImGui::GetFont(); }
    ImFont* consoleFont() const { return fontConsole_ ? fontConsole_ : ImGui::GetFont(); }
    // The next key press becomes ACTION's shortcut (Ctrl+Alt+click a button, or Preferences > Hotkeys).
    void beginShortcutCapture(const std::string& actionId);
    const std::string& capturingShortcut() const { return capturingShortcut_; }
    void openCommandPalette() { palette_.open(); }
    void setConsoleOpen(bool open);
    void consolePrefill(const std::string& text); // opens the console with TEXT typed
    // Store Config (Ctrl+Shift+I) / Restore Standard UI / Restore Custom UI / Save UI... / Load UI... (Ctrl+L)
    void storeConfig();
    void restoreStandardUi();
    void restoreCustomUi();
    void saveUiDialog();
    void loadUiDialog();
    void queueUiFile(int kind, std::string path); // file dialog callbacks (any thread)

private:
    friend class Console;
    friend class CommandPalette;
    friend void config::drawHotkeysSection(App&);
    friend void config::drawLightBoxSection(App&);
    void initUiConfig(bool deterministic); // reads ui.json before fonts are built (UI scale)
    void initActions();                    // the default actions and shortcuts, then every command as cmd.*
    void addHelpFonts(float scale);
    void drawHelpConsoleUi();              // console, command palette, Controls, shortcut capture, Ctrl help
    void drawControlsWindow();
    bool captureShortcutKey(SDL_Keycode key, SDL_Keymod mod);
    void consoleEcho(const std::string& name, const Args& args, const CommandResult& r);
    std::string uiConfigPath() const;
    // Window size, position, maximised and full screen, kept in window.json beside ui.json (#35). Not for tests,
    // renders, recordings or proof mode.
    struct WindowState { int w = 1600, h = 960, x = 0, y = 0; bool placed = false, maximized = false, fullscreen = false; };
    static WindowState loadWindowState();
    void saveWindowState() const;
    bool rememberWindow_ = false;
    void drawAxisGizmo();
    std::array<AxisGizmoEnd, 6> axisGizmoLayout() const; // the gizmo's ends on screen now (AxisGizmo.h)
    bool axisGizmoMouseDown(float x, float y);          // a click on an end: that side view (#43)
    float axisGizmoUnit() const;                        // UI scale times Draw > Camera > Gizmo size
    glm::vec2 viewFigureCentre() const;                 // Draw > Camera > View Figure: where it sits, how big
    float viewFigureRadius() const;
    void drawViewFigure();
    // Insert on the model (#34): the primitive chosen in Append > Insert; a drag on the surface places it.
    // "mesh" inserts your own mesh instead: insertMeshKey_ is "subtool" (a SubTool's name) or "file" (a path
    // chosen in the file dialog), insertMeshValue_ the name or path (insert_mesh).
    std::string insertType_;
    std::string insertMeshKey_, insertMeshValue_;
    glm::vec3 insertAt_{0.0f}, insertNormal_{0.0f, 1.0f, 0.0f};
    float insertX_ = 0.0f, insertY_ = 0.0f, insertPx_ = 0.0f;
    bool readUiConfig(const std::string& path, std::string& err);
    bool writeUiConfig(const std::string& path, std::string& err);
    std::string layoutKey() const; // the layout without per-session state, to notice edits
    void watchLayoutChanges();
    void saveRecentActions();
    CanvasRect consoleRect() const;
    CanvasRect paletteArea() const;
    ActionRegistry actions_;
    UiPrefs uiPrefs_;
    Console console_;
    CommandPalette palette_;
    std::string configDir_;           // <prefs>/ (ui.json, console_history.txt, recent_actions.txt)
    std::string capturingShortcut_;   // action id waiting for a key
    std::string layoutBaseline_;      // layoutKey() as last stored / loaded / restored
    bool layoutHintShown_ = false;
    int layoutCheckFrame_ = 0;
    bool uiConfigDeterministic_ = false; // UI tests, renders, recordings: standard layout, nothing read
    ImFont* fontHelpTitle_ = nullptr;
    ImFont* fontConsole_ = nullptr;
    std::string commandSource_; // shown before echoed commands, e.g. "AI" while the agent drives the app
    std::mutex uiFileMutex_;
    std::vector<std::pair<int, std::string>> uiFiles_; // (kind, path) from file dialogs
    // ---- end Help/Console/Config (C) ----

    // ---- User Guide (G): Help > User Guide, the embedded docs/guide reader ----
public:
    UserGuide& guide() { return guide_; }

private:
    friend class UserGuide; // reads the UI scale, writes the status line
    UserGuide guide_;
    // ---- end User Guide (G) ----
    // ---- Gizmo 3D (Gizmo.cpp, core/MeshTransform.h): W Move, E Scale, R Rotate, Q back to Draw ----
public:
    enum class GizmoMode { Off, Move, Scale, Rotate };
    enum class GizmoHandle {
        None,
        MoveX, MoveY, MoveZ,       // axis arrows
        PlaneYZ, PlaneXZ, PlaneXY, // plane squares (named by the plane they move in)
        MoveView,                  // the centre ring: free move in the view plane
        RotateX, RotateY, RotateZ, RotateView,
        ScaleX, ScaleY, ScaleZ, ScaleUniform,
        Count
    };

private:
    void registerGizmoActions();
    void setGizmoMode(GizmoMode mode);
    bool gizmoActive() const { return gizmo_.mode != GizmoMode::Off; }
    bool gizmoMouseDown(float x, float y, SDL_Keymod mod); // true if it took the click
    void gizmoDragTo(float x, float y);
    void gizmoRelease();
    void drawGizmo();          // the handles over the canvas, and the Gizmo 3D panel
    void drawGizmoPanel();
    void drawGizmoContents(bool compact); // Transform palette > Gizmo 3D and the floating panel
    void updateGizmoPivot();   // follows the moving part's centre until the pivot is placed
    void resetGizmoPivot();
    void snapGizmoPivot(const RayHit& hit, int subtool);
    void applyGizmoNumbers();  // the panel's Move / Rotate / Scale fields, as a transform command
    glm::mat3 gizmoFrame() const;
    float gizmoWorldSize() const; // world length of an axis arrow (constant on screen)
    bool gizmoProject(const glm::vec3& p, ImVec2& out) const; // false behind the camera
    GizmoHandle gizmoHandleAt(float x, float y) const;
    bool gizmoHandleScreen(GizmoHandle h, ImVec2& out) const; // a point to grab handle h (UI tests)
    std::vector<int> gizmoTargets() const; // SubTools a drag transforms (the active one, or All)
    void gizmoAfterUndo(bool redo);        // the gizmo goes back (or forward) with its own steps
    void rememberGizmoStep(const glm::vec3& before, const glm::mat3& frameBefore, bool surfaceBefore);
    struct GizmoState {
        GizmoMode mode = GizmoMode::Off;
        glm::vec3 pivot{0.0f};
        bool pivotPlaced = false;   // Alt+drag, snap or a drag moved it: stop following the centre
        bool orientSurface = false; // axes from the surface the pivot was snapped to (else world)
        glm::mat3 frame{1.0f};      // the surface axes (turn with rotations)
        bool useMask = true, all = false;
        float falloff = 0.0f;       // fraction of the size (core/MeshTransform.h)
        bool snapNext = false;      // "Pivot to Surface": the next click on the model places the pivot
        uint64_t centreKey = ~uint64_t(0), maskKey = 0;
        GizmoHandle hover = GizmoHandle::None;
        // The drag in progress.
        GizmoHandle handle = GizmoHandle::None;
        bool pivotOnly = false;     // Alt: move the pivot / turn the axes, not the model
        glm::vec3 startPivot{0.0f};
        glm::mat3 startFrame{1.0f};
        glm::vec3 grab{0.0f};       // where the drag started on the handle's line / plane
        glm::vec3 grabDir{0.0f};    // rotation: the start direction in the ring's plane
        ImVec2 startMouse{0, 0};
        float lastAngle = 0.0f, angle = 0.0f; // rotation: unwrapped, so a drag can go past 180 degrees
        bool tangent = false;                 // a ring seen edge-on turns with the drag along its tangent
        ImVec2 tangentDir{0, 0};
        glm::vec3 surfaceNormal{0, 1, 0};     // at the last snap (Surface orientation)
        bool pendingSnap = false;             // Alt+press on the model: a click (no drag) puts the pivot there
        RayHit snapHit;
        int snapSubtool = -1;
        MeshTransform current;
        std::vector<int> subtools;
        std::vector<TransformSession> sessions;
        std::string readout;        // shown beside the gizmo while dragging ("Rotate 45")
        // Where the gizmo was before and after each of its undo steps (active SubTool, by undo depth).
        struct Step {
            uint32_t subtool = 0;
            size_t depth = 0; // UndoStack::undoCount() right after the step
            glm::vec3 before{0.0f}, after{0.0f};
            glm::mat3 frameBefore{1.0f}, frameAfter{1.0f};
            bool surfaceBefore = false, surfaceAfter = false;
        };
        std::vector<Step> undoSteps, redoSteps;
        // Numeric entry (the panel): applied as one transform about the pivot.
        float move[3] = {0, 0, 0}, rotate[3] = {0, 0, 0}, scale = 100.0f;
    } gizmo_;
    // ---- end Gizmo 3D ----
    // ---- ZBrush feel (Input.cpp, docs/guide/coming-from-zbrush.md) ----
    // Preferences > Input (the ZBrush or SculptForge key style), the Space QuickMenu, the intensities and Focal
    // Shift (U / I / O), ZBrush's mask and polygroup keys, Shift-constrained straight strokes and the
    // Shift+click, release Shift, drag roll.
    void registerInputActions();
    void applyInputStyle(); // the style's default shortcuts (1-0 brushes or 9 QuickSave; I = RGB or Z Intensity)
    bool zbrushStyle() const { return uiPrefs_.inputStyle == 0; }
    bool blenderStyle() const { return uiPrefs_.inputStyle == 2; }
    bool mayaStyle() const { return uiPrefs_.inputStyle == 3; } // #32: Alt+left/middle/right orbit/pan/zoom
    bool mayaNavigationDown(Uint8 button);
    void setInputStyle(int style);
    // ---- Blender style (Input.cpp) ----
    // Middle-drag orbits (Shift pans, Ctrl zooms), Alt+left-drag emulates the middle button (pens), Ctrl inverts
    // a stroke, M toggles the mask brush, and Blender's sculpt-mode letters pick brushes. The numpad views work in
    // every style. Import Blender keymap reads a keymap exported from Blender (BlenderKeymap.h).
    void registerViewActions();
    void viewAxis(float yawDegrees, float pitchDegrees); // Numpad 1 / 3 / 7: orthographic until turned
    void viewOrbitStep(float yawDegrees, float pitchDegrees);
    void frameActiveSubtool();
    // Focus Detail (prototype, FocusDetailApp.cpp): open a .sfdetail file; once a frame, the camera and brush.
    void openFocusDetail(const std::string& path);
    void updateFocusDetail();
    glm::vec3 focusBrushPos_{0.0f}, focusBrushVel_{0.0f};
    float focusBrushRadius_ = 0.0f;
    double focusBrushTime_ = 0.0;
    bool focusBrushValid_ = false;
    SDL_Keymod blenderMods(SDL_Keymod keyboard) const; // Ctrl -> Alt (invert), the mask brush -> Ctrl (mask)
    bool maskBrushActive() const;
    void toggleMaskBrush();
    void hideGroupUnderCursor(bool isolate);
    void importBlenderKeymapDialog();
    bool importBlenderKeymap(const std::string& path);
    void drawInputStyleRow(float width = -1.0f); // the three styles (Preferences > Input, Controls)
    struct MaskBrush {
        bool on = false;
        BrushType brush = BrushType::Standard; // what was picked when M was pressed: picking another brush ends it
        int custom = -1;
        bool paint = false;
    } maskBrush_;
    bool autoOrtho_ = false; // an axis view switched to orthographic; orbiting goes back to perspective (Blender)
    // ZBrush: while Ctrl is held the brush is the mask brush, with Ctrl+Shift the selection (hide) brush. The shelf
    // tiles show it, and the Brush / Stroke pickers opened then pick among those brushes (PanelMasking.cpp).
    enum class HeldBrush { None, Mask, Select };
    HeldBrush heldBrush() const;
    struct HeldBrushItem {
        const char* action; // "mask.lasso": the Tool > Masking / Visibility button it stands for
        const char* brush;  // MaskLasso
        const char* stroke; // Lasso
        const char* icon;
        SelectShape shape;
    };
    static std::vector<HeldBrushItem> heldBrushItems(HeldBrush held);
    const HeldBrushItem* heldBrushCurrent(HeldBrush held) const; // the one picked now
    bool drawHeldBrushGrid(HeldBrush held, bool strokes); // the picker's grid; true when one was picked
    void drawHeldBrushIcon(ImDrawList* dl, ImVec2 a, ImVec2 b, const char* icon, bool select); // shelf tiles (Shell.cpp)
    void drawInputSection(); // Preferences > Input
    void drawCursorSection(); // Preferences > Cursor (Input.cpp)
    // Viewport hints (U11, Config.cpp): a note over the 3D view with Don't show again, `top` its top edge in
    // pixels; Preferences > Hints lists them. hintShown: not hidden by either.
    bool hintShown(const char* id) const;
    void drawViewportHint(const char* id, const char* text, ImU32 colour, float top);
    void drawHintsSection();
    // Z Intensity (0-100 on screen): Smooth's own while Shift is held or Smooth is the brush, as in ZBrush.
    void drawZIntensitySlider(const char* id);
    void openQuickMenu();
    void quickMenuKeyUp();   // Space released: a hold closes the menu, a tap leaves it open
    // A right click on the canvas (pressed and let go without moving past kRightClickSlop) opens the QuickMenu
    // there, as in ZBrush (#22); a right drag navigates as before. Preferences > Input > Right click opens the
    // QuickMenu (uiPrefs_.rightClickQuickMenu).
    void rightClickRelease();
    void drawQuickMenu();
    void groupMasked();      // Ctrl+W
    // Straight strokes: Shift pressed after a stroke started constrains it to a line from where Shift went down,
    // snapped to 45 degree steps on screen. Returns the point to draw towards.
    glm::vec2 constrainStroke(float x, float y);
    float smoothStrength_ = 0.5f; // Smooth's own Z Intensity (Shift+stroke, or the Smooth brush)
    bool smoothKeepVolume_ = false; // Brush > Smooth keeps volume: a Taubin smooth instead of the plain one (#40)
    bool smoothGroupBorders_ = false; // Brush > Smooth face group borders only: straightens the borders (#81)
    // ZBrush's alternate smooth (#40): a stroke begun with Shift held that lets go of Shift while still pressing
    // smooths the other way (keeping the volume, or plainly when Smooth keeps volume) for the rest of the stroke.
    bool strokeAltSmooth_ = false;
    float rgbIntensity_ = 1.0f;   // paint strength (ZBrush RGB Intensity, 100 by default)
    float focalShift_ = 0.0f;     // -1..1 (ZBrush Focal Shift -100..100)
    // Per-brush settings (#78, BrushSettingsUi.cpp, core/BrushSettings.h): each frame, a brush switch stores the
    // brush left and brings back the picked brush's own Draw Size, Z Intensity, Falloff, RGB Intensity, stroke and
    // alpha. Save Brush Settings writes them (<app data>/brush-settings.json) so they load at startup; Reset Brush
    // to Default puts back the shipped values. Off in demo videos and benchmarks (their scripts set every value).
public:
    std::string currentBrushKey() const; // "builtin:Clay", "custom:Clay Slab"
    BrushToolSettings liveBrushSettings() const;
    void applyBrushSettings(const BrushToolSettings& s);
    void saveBrushSettings();  // Brush > Save Brush Settings
    void resetBrushSettings(); // Brush > Reset Brush to Default
    void setShareBrushSize(bool on);
    const BrushSettingsStore& brushSettingsStore() const { return brushSettings_; }

private:
    void initBrushSettings();
    void syncBrushSettings(); // every frame, outside strokes
    void syncAlphaTile();     // the brush's Tile onto the current alpha
    void registerAlphaOptionsCommand(); // brush_alpha_options (#76)
    std::string brushSettingsPath() const;
    BrushSettingsStore brushSettings_;
    std::string brushSettingsKey_; // the brush the live values belong to
    bool brushSettingsOn_ = true;
    struct QuickMenu {
        bool open = false;
        ImVec2 at{0, 0};
        double openedAt = 0.0;  // seconds (a short press is a tap)
        bool keyHeld = false;   // Space is still down
        bool untilLeave = false; // Space let go over the menu: it closes when the pointer leaves it
        int openedFrame = -1;
    } quickMenu_;
    // ---- Packs and macros (PanelPacks.cpp, core/Pack.h, core/Macro.h, #20) ----
    // Library > Packs: install (a review screen first: what's inside, the license, the author), list, switch on and
    // off, remove, and New Pack. Pack items join the brush, alpha, Lit Sphere, Material Recipe, keymap, pie menu and
    // macro lists tagged with the pack's name. Macros: Record (the commands run between Start and Stop), the
    // editor (plain text, checked as you type), and one action per macro ("macro.<pack or user>.<name>") for F3, keys
    // and pie menus. A macro shows its whole command list and asks before it first runs (again after it changed).
public:
    void drawPacksTab(float height);                // Library > Packs (LightBox.cpp)
    double packsTestValue(const std::string& name) const; // NaN: not a pack value (UiTest.cpp)
    void packDialogAnswered(std::string path);      // thread-safe: a pack or macro file dialog's answer
    void reviewPackFile(const std::string& path);   // File > Install Pack, a dropped .sfpack: the review screen
    const std::vector<PackContentItem>& packAlphas() const { return packs_.alphas; }
    const std::vector<PackContentItem>& packMaterials() const { return packs_.materials; }

private:
    void registerPackActions();
    void refreshPacks();        // after an install, removal or switch: brushes, VDMs, Lit Spheres, recipes, macros
    void refreshMacroActions(); // one action per macro
    void loadPackMatcaps();
    void drawPacksUi();         // the review screen, New Pack, the macro confirmation and the macro editor
    void drawPackReview();
    void drawNewPack();
    void drawMacroConfirm();
    // Free memory before a big mesh (core/MemoryGuard.h): a command that would need more than is free asks first.
    // Returns true to go on now; false: the dialog is open and `proceed` runs on Continue.
    bool memoryAllows(const std::string& command, const Args& args, std::function<void()> proceed);
    void drawMemoryConfirm();
    struct MemoryConfirm {
        bool open = false;
        std::string what, message;
        std::function<void()> proceed;
    } memoryConfirm_;
    bool memoryConfirmed_ = false; // the next memoryAllows passes (Continue was pressed)
    void drawMacroEditor();
    void drawPackKeymapRows();  // Preferences > Input: the enabled packs' keymaps
    void pollPackDialogs();
    void openPackDialog(int kind, int pathKind = 0); // PackDialog, MacroPathKind
    void requestMacroRun(const Macro& m); // asks first unless this exact text was confirmed
    void startMacroRun(Macro m);          // asks for "?" files, then runs
    void runMacroNow();
    void toggleMacroRecording();
    void openMacroEditor(const std::string& name, const std::string& text, const std::string& path);
    bool applyPackKeymap(const std::string& file);
    void openNewPack();
    void writeNewPack(const std::string& path);
    std::string macroProjectDir() const; // the saved project's folder ("" when the scene isn't a saved project)
    enum PackDialog { kPackDialogNone, kPackDialogInstall, kPackDialogSave, kPackDialogMacro };
    struct NewPackItem {
        PackItemType type = PackItemType::Brush;
        std::string label, file, name; // name: the file name in the pack
        std::vector<uint8_t> data;     // made in memory (pie menus, the keymap, the current Fill layers)
        bool on = false;
    };
    struct PacksUi {
        std::vector<InstalledPack> installed;
        std::vector<PackContentItem> alphas, materials, keymaps, pieMenus;
        std::vector<int> matcaps; // Lit Sphere indices the packs added (removed again on a refresh)
        std::unique_ptr<PackReview> review;
        std::string reviewError, reviewPath;
        bool reviewOpen = false;
        std::string expanded; // the pack id whose details show
        bool newOpen = false;
        char id[65] = "", name[65] = "", version[33] = "1.0", author[65] = "", license[65] = "CC-BY-4.0";
        char description[1001] = "";
        bool insider = false;
        std::vector<NewPackItem> candidates;
        std::string newError;
        std::mutex mutex;          // the dialog answer arrives on another thread
        std::string answer;
        bool answered = false, answerCancelled = false;
        int dialog = kPackDialogNone; // what the open dialog is for
    } packs_;
    struct MacrosUi {
        MacroRecorder recorder;
        int runDepth = 0;          // > 0 while a macro runs: its commands aren't recorded or intercepted
        std::vector<Macro> list;   // the user's and the packs' (refreshMacroActions)
        bool confirmOpen = false;
        Macro confirm;
        bool editorOpen = false;
        char name[97] = "";
        std::vector<char> text = std::vector<char>(kMaxMacroFileSize + 1, '\0');
        std::string path;          // the file the editor saves to ("" = new)
        std::string check;         // the editor's validation line
        bool checkOk = false;
        std::string checkedText = "\x01";
        Macro running;             // waiting for its "?" files
        std::vector<MacroAsk> asks;
        std::vector<std::string> answers;
        bool waiting = false;
        int ran = -1;              // commands the last run ran (UI tests)
        bool lastOk = false, lastOneStep = false;
    } macros_;
    // ---- end Packs and macros ----
    // ---- Radial menus (RadialMenu.cpp, core/RadialMenu.h, #45) ----
    // Up to eight pie menus of up to eight actions each. A menu's key (action radial.menuN, set in the keymap like
    // any other) or its mouse / pen button, held: the pie shows at the cursor, a flick toward a slot and letting go
    // runs that action. Tapped: the pie stays open; click a slot (or beside the pie, or Esc, to close it). Edited in
    // Preferences > Input > Radial Menus; Store Config keeps them in ui.json ("radialMenus").
    enum class RadialHold { None, Key, Mouse, Pen };
    void registerRadialActions();
    void refreshRadialActions(); // the radial.menuN actions' labels follow the menus' names
    void applyRadialMenusJson(const Json& j); // ui.json's "radialMenus", checked against the registry
    void resetRadialMenus();
    static std::vector<RadialMenu> defaultRadialMenus();
    RadialVocabulary radialVocabulary() const; // what a menu read from a file may name
    bool radialSlotAction(std::string_view id) const; // an action a slot may run
    void openRadialMenu(int menu, RadialHold hold, int code);
    void closeRadialMenu() {
        radial_.menu = -1;
        radial_.hold = RadialHold::None;
    }
    void radialRelease();   // the key or button that holds the pie open was let go
    void runRadialSlot(int slot);
    bool radialMouseEvent(const SDL_Event& e); // true: the pie used the event (clicks while open, bound buttons)
    bool radialPenButton(int button, bool down, float x, float y);
    void radialKeyUp(SDL_Keycode key);
    int radialMenuForButton(RadialButton b) const; // -1: none
    PenAction penButtonAction(int button) const;   // Preferences > Tablet, unless a radial menu claims the button
    void drawRadialMenu();
    void drawRadialMenusSection(); // Preferences > Input > Radial Menus
    void exportRadialMenuDialog(int menu);
    void importRadialMenuDialog();
    bool exportRadialMenu(int menu, const std::string& path);
    bool importRadialMenu(const std::string& path);
    std::vector<RadialMenu> radialMenus_ = defaultRadialMenus();
    struct RadialOpen {
        int menu = -1;          // the open menu, or -1
        ImVec2 at{0, 0};        // its centre (the cursor when it opened)
        double openedAt = 0.0;  // seconds (a short press is a tap)
        RadialHold hold = RadialHold::None; // what holds it open; None: tapped open, stays until a click or Esc
        int code = 0;           // the held key (SDL_Keycode) or mouse / pen button
        int hover = -1;         // the slot the pointer points at
        int openedFrame = -1;
        int ran = -1;           // the slot run last (UI tests)
    } radial_;
    // ---- The stroke popup (StrokePopup.cpp, core/StrokePopup.h; Discord 11-02) ----
    // One key (stroke.popup, G; Z in the Blender style) or the left shelf's button opens a grid at the cursor: the
    // stroke types and the modifiers Add / Sub, Smooth, Mask, Select. Hold the key, point, let go: picked. Tap it:
    // the grid stays open for a click (a stroke closes it, a modifier toggles and leaves it open). Esc cancels.
    // Smooth, Mask and Select latch: strokes act as if Shift, Ctrl or Ctrl+Shift were held until the same item is
    // picked again or another brush is picked.
    void registerStrokePopupActions();
    void openStrokePopup(bool held, int key);
    void closeStrokePopup() { strokePopup_.open = false; }
    void pickStrokePopupItem(int item, bool close);
    bool strokePopupMouseEvent(const SDL_Event& e); // true: the popup used the event
    void strokePopupKeyUp(SDL_Keycode key);
    void drawStrokePopup();
    bool strokePopupItemScreen(int item, ImVec2& out) const; // an item's centre, window pixels (UI tests)
    StrokeLatch strokeLatch() const; // the latch in effect (None once another brush was picked)
    void setStrokeLatch(StrokeLatch latch);
    struct StrokePopupOpen {
        bool open = false;
        ImVec2 at{0, 0};       // the panel's top-left
        double openedAt = 0.0; // seconds (a short press is a tap)
        bool held = false;     // its key holds it open; false: tapped open, stays until a click or Esc
        int key = 0;           // the held key (SDL_Keycode)
        int hover = -1;        // the item under the pointer
        int start = -1;        // the item under the pointer when it opened (the current stroke, unless clamped)
        int picked = -1;       // the item picked last (UI tests)
    } strokePopup_;
    struct StrokeLatchState {
        StrokeLatch latch = StrokeLatch::None;
        BrushType brush = BrushType::Standard; // the brush it was latched with: picking another ends it
        int custom = -1;
        bool paint = false;
    } strokeLatch_;
    uint32_t strokePopupSwallowUp_ = 0; // mouse buttons whose press the popup took: their release is its too
    SDL_Keycode keyDown_ = SDLK_UNKNOWN; // the key onKey is handling: a radial menu's key holds the pie open
    uint32_t radialSwallowUp_ = 0;       // mouse buttons whose press the pie took: their release is its too
    std::optional<Json> radialPending_;  // radial menus read from ui.json before the actions existed
    struct RadialEdit {
        int menu = 0;            // Preferences: the menu being edited
        int selected = -1;       // the item whose label and icon show
        int pickSlot = -2;       // the slot whose action is being picked (-1: a new slot; -2: none)
        char query[96] = "";     // the action search
        bool focusSearch = false;
        std::string foundQuery = "\x01"; // the search's results, for this query and registry revision
        unsigned foundRevision = ~0u;
        std::vector<size_t> found;
        int exportMenu = -1;     // the menu the export dialog was opened for
    } radialEdit_;
    struct RightClick {
        bool pending = false; // a right press that hasn't moved past the slop: navigation waits
        float x = 0.0f, y = 0.0f;
        float dx = 0.0f, dy = 0.0f; // the motion since the press
    } rightClick_;
    struct StraightStroke {
        bool active = false;   // Shift went down mid-stroke
        bool shiftAtStart = false; // the stroke began with Shift (it smooths; Shift isn't a constraint then)
        glm::vec2 origin{0.0f};
    } straight_;
    bool rollPending_ = false; // Shift+press on the background: let go of Shift before moving to roll the view
    float rollStartX_ = 0.0f, rollStartY_ = 0.0f;
    glm::vec2 lastDabScreen_{0.0f}; // window pixels of the last dab (UI tests: last_dab_x / last_dab_y)
    bool maskClickPending_ = false; // Ctrl+press on the model: a click blurs (Alt: sharpens), a drag paints
    float maskClickX_ = 0.0f, maskClickY_ = 0.0f;
    // ---- end ZBrush feel ----
    // ---- Redraw on demand (App.cpp run()) ----
    bool wantsFrames(double now) const; // something animates or runs: keep drawing
    bool focusKeepsDrawing(double now) const; // the window has focus and was just used (not: the pointer is over it)
    static constexpr double kFocusedActiveSeconds = 2.0;
    bool windowFocused_ = false; // keyboard focus (SDL_EVENT_WINDOW_FOCUS_GAINED / _LOST)
    double lastInputAt_ = -1e9;  // seconds (SDL clock) of the last key, mouse or pen event
    uint64_t frameKey() const;          // changes when a poller changed what the window shows
    double activeUntil_ = 0.0;          // seconds: keep drawing until then (after input: hover, tooltips)
    double redrawAt_ = 0.0;             // seconds (SDL clock): draw one frame then, asleep or not (0: none)
public:
    void redrawIn(double seconds);      // something on screen changes by itself later (an error colour fading)
private:
    // ---- Tablet (Tablet.cpp, core/Tablet.h, docs/tablet.md) ----
    // The pen tip is a left mouse button (SDL_PEN_MOUSEID) for the canvas and the UI. The app makes those mouse
    // events from the pen's own (penMouseEvents), not SDL (#95); the pen's events add pressure, tilt, the eraser
    // end and the side buttons, which are mapped in Preferences > Tablet.
    bool dropPenMouseEvent(const SDL_Event& e) const; // SDL's own mouse events for the pen (Tablet.cpp)
    void penMouseEvents(const SDL_Event& pen, bool before); // the pen tip's mouse events, made from the pen's
    void sendPenMouse(SDL_EventType type, float x, float y, SDL_WindowID window);
    bool strayMouseMotion(const SDL_Event& e) const;  // a driver's mouse events beside the pen's (Tablet.cpp)
    void keepImGuiOnPen();                            // after ImGui_ImplSDL3_NewFrame (Tablet.cpp)
    bool penSoftCursor() const; // mouse mode with the pen: the arrow is drawn at the brush (Tablet.cpp)
    void mapPenPosition(SDL_Event& e);                // mouse mode: the pen moves the cursor relatively (Tablet.cpp)
    // The system cursor (#73): ImGui's (resize handles, text fields, links) over the panels, a crosshair over the
    // 3D view where a brush acts (Preferences > Cursor > Viewport cursor). Called after each ImGui::Render.
    void updateMouseCursor();
    bool viewportCursorWanted() const;
    bool frontRaycast(const glm::vec3& origin, const glm::vec3& dir, RayHit& hit) const; // as drawn (#68)
    static constexpr int kCursorCrosshair = 100, kCursorHidden = -1;
    int cursorKind_ = 0;          // what updateMouseCursor last set: an ImGuiMouseCursor, kCursorCrosshair or kCursorHidden
    SDL_Cursor* cursors_[16] = {}; // system cursors by SDL_SystemCursor, made when first needed
    int cursorShown_ = -2;         // (the kind on screen, so SDL_SetCursor runs only on a change; -2: none yet)
    void handlePenEvent(const SDL_Event& e);
    void penButton(int button, bool down, float x, float y);
    void penLeft(); // out of proximity: ends what the pen was doing
    float strokePressure() const; // pressure after the global curve; 1 for the mouse
    // The keyboard's modifiers plus those a pen button holds (Alt, Shift) and, with latch, the stroke popup's
    // latched modifier (Shift for Smooth, Ctrl for Mask, Ctrl+Shift for Select). Navigation reads it without the
    // latch: a latched Mask still lets a drag beside the model turn the view.
    SDL_Keymod inputMods(bool latch = true) const;
    SDL_Keymod keyMods() const;   // the keyboard's alone (in the input style's terms)
    bool erasing() const;         // the pen's eraser end is drawing
    PenAction eraserAction() const { return uiPrefs_.tablet.eraser; }
    void firstDab(float x, float y); // the stroke's first dab, once the pen reports a real pressure
    void drawTabletSection();         // Preferences > Tablet
    void detectPenName();
    void tabletDeviceEvent(const SDL_Event& e); // a tablet plugged in or out while running (TabletHotplug)
    void tabletNote(TabletHotplug::Note note, const std::string& name);
    const char* pressureStatus() const; // "Pressure: on" / "not detected" (Preferences > Input and > Tablet)
    void drawPressureStatus();          // ... with the troubleshooting link
    PenPressureTracker penTracker_;
    TabletHotplug tabletHotplug_;
    void wintabEvent(const SDL_Event& e); // a mouse event while the tablet driver reports pressure through Wintab
    WintabPressure wintab_;
    bool wintabPen_ = false;  // the mouse is the pen of a driver without Windows Ink (pressure from Wintab)
    bool wintabTip_ = false;
    Uint64 wintabNs_ = 0;     // the last Wintab packet
    bool penJump_ = false; // the pen just came into range: its first position doesn't move a drag (no jump)
    bool penMouseSending_ = false;  // dispatching a mouse event made from the pen (not SDL's)
    bool penMouseDown_ = false;     // the pen tip's left button is down (as made by penMouseEvents)
    SDL_PenID penMouseOwner_ = 0;   // the pen holding it
    float penMouseX_ = 0.0f, penMouseY_ = 0.0f; // the pen's last position (window pixels, before mouse mode)
    PenCursor penCursor_; // where the pen puts the cursor (Preferences > Tablet > Mouse mode)
    bool penInProximity_ = false, penTouching_ = false, penEraser_ = false;
    Uint64 lastPenNs_ = 0; // when the last pen event arrived (see onMouseMove)
    Uint64 lastPenTouchNs_ = 0; // when the pen tip last touched down or lifted (strayMouseMotion)
    uint32_t penButtons_ = 0; // bit N: pen button N is held
    int penNavButton_ = 0;    // the pen button driving a view drag (0: none)
    bool dragFromPen_ = false;
    bool firstDabPending_ = false;
    float lastStrokePressure_ = 1.0f; // pressure at the previous stroke sample (dabs in between interpolate)
    float penTiltX_ = 0.0f, penTiltY_ = 0.0f;
    bool penTiltSeen_ = false;
    SDL_PenID penId_ = 0;
    std::string penName_; // best guess at the device's name (empty: unknown)
    struct TabletTestSample {
        ImVec2 at; // relative to the test area
        float raw, pressure;
    };
    std::vector<TabletTestSample> tabletTest_; // Preferences > Tablet draw-test area
    // ---- end Tablet ----
    // ---- SpaceMouse (SpaceMouse.cpp, core/SpaceMouse.h, docs/guide/navigation.md) ----
    // A 3Dconnexion SpaceMouse read through SDL's joystick API (no 3Dconnexion SDK): its six axes move the camera
    // every frame, independently of the mouse and pen (a stroke held by the pen keeps going), its buttons run actions.
    void spaceMouseEvent(const SDL_Event& e); // hot-plug and buttons
    void spaceMouseScan();                    // take the first SpaceMouse SDL knows about, if none is open
    void spaceMouseClose();
    void updateSpaceMouse(float dt);          // once a frame: move the camera by the held axes
    bool spaceMouseMoving() const { return spaceMouseMoving_; } // keep frames coming while the cap is pushed
    bool spaceMouseDeflected() const; // the cap is pushed past the dead zone now (an axis event worth a frame)
    void drawSpaceMouseSection();             // Preferences > Input > SpaceMouse
    SDL_Joystick* spaceMouse_ = nullptr;
    SDL_JoystickID spaceMouseId_ = 0;
    std::string spaceMouseName_;
    int spaceMouseButtonCount_ = 0;
    std::array<float, kSmAxes> spaceMouseAxes_{}; // the last values read, -1 .. 1 (Preferences shows them)
    bool spaceMouseMoving_ = false;
    int spaceMouseLastButton_ = -1; // for Preferences: "Button N pressed"
    // ---- end SpaceMouse ----
    // ---- Sculpt Timeline (PanelTimeline.cpp, core/Timeline.h) and proof mode (ProofMode.cpp) ----
    // The history strip along the bottom of the canvas, the timelapse export, and Help > Benchmark: a scripted
    // stroke through the input handlers on 5M..100M+ meshes with a live HUD, an MP4 and a JSON result.
    std::shared_ptr<struct TimelineUiState> timelineUi_;
    std::shared_ptr<struct ProofState> proof_;
    void drawTimelineUi();            // once a frame from drawUi: keeps the timeline attached and draws the strip
    float timelineTopInset_ = 0.0f;   // how far down the canvas the strip (or its tab) reaches at the top; 0 at the bottom
    float timelineStripY_ = -1.0f;    // the strip's (or tab's) top edge in window pixels this frame (-1: not shown)
    void registerTimelineUiActions(); // view.timeline, timeline.timelapse, help.benchmark
    void startTimelapse(const std::string& path, int width, int height, float seconds, int fps);
    bool timelapseStep(double budgetSeconds); // renders frames for up to the budget; false when finished
    int runTimelapseCli();                    // sculptforge FILE --timelapse out.mp4
    void startProofMode();                    // Help > Benchmark, sculptforge --benchmark
    bool proofSkippedScenes() const;          // the benchmark skipped a scene: too little free memory
    void proofBeforeSwap(int fbW, int fbH);   // the interactive loop, after drawing a frame, before showing it
    void drawProofUi();
    // ---- end Sculpt Timeline ----
};

} // namespace sf
