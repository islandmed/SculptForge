// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

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
#include "core/Commands.h"
#include "core/Image.h"
#include "core/Document.h"
#include "core/Face.h"
#include "core/MeshTransform.h"
#include "core/PenPredictor.h"
#include "core/AutoUv.h"
#include "core/QRemesh.h"
#include "core/Stroke.h"
#include "core/StrokePath.h"
#include "core/SurfaceCursor.h"
#include "core/SpaceMouse.h"
#include "core/Tablet.h"
#include "core/UpdateCheck.h"
#include "core/ViewCapture.h"
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

    enum class FileAction { OpenObj, SaveObj, ExportGlb, ExportLods, SaveBrush, LoadBrush, OpenImage, ExportUsd, SaveSplats, ExportUnreal, SaveProject, AppendSubtool, ComfyWorkflow, LoadMatcap, InsertMesh, Count };
    struct DialogRequest {
        App* app;
        FileAction action;
    };
    // Thread-safe: called from native file dialog callbacks.
    void queueFile(FileAction action, std::string path);

private:
    enum class Drag { None, Stroke, Orbit, Pan, Zoom, Roll, MaskCanvas, SubToolClick, Select, Gizmo, Guide, Sculptable, ZSphere, Insert, Capture };

    void handleEvent(const SDL_Event& e);
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
    // The brush ring lying on the surface under the pointer (#25, Preferences > Input > Cursor follows surface);
    // false when the pointer is off the model (the flat screen ring is drawn instead).
    bool drawSurfaceCursor(ImDrawList* dl, float radiusPx, ImU32 col);
    std::vector<SurfaceRingPoint> cursorRing_, cursorInner_; // scratch, kept to avoid allocating per frame
    bool surfaceCursorShown_ = false; // the last cursor drawn lay on the surface (UI tests: surface_cursor)
    void updateTitle();

    int runRecording(); // also runs UI tests (options_.uiTest)
    int runUiTest(int width, int height, int fps);
    std::unique_ptr<Demo> demo_;
    float uiScale_ = 1.0f; // UI layout scale (fonts are rasterised at this scale)
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
        bool smoothKeepVolume = false; // Smooth keeps the volume (smoothKeepVolume_, or the alternate smooth)
        bool pressureSize = false, pressureStrength = true, invert = false, frontFacesOnly = false;
        bool mask = false, paint = false, sculpt = true, visible = true;
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

    std::string status_;
    bool lastCommandOk_ = true; // result of the last runCommand (UI tests check it)
    bool showHelp_ = false;
    bool showAbout_ = false;
    // Supporting SculptForge (Support.h): the app only ever opens kSupportUrl in the browser; nothing else changes.
    void openSupportPage();
    void drawSupportUi();               // the first-run welcome and the one reminder note
    void drawSupportersSection();       // Help > About: the names from docs/supporters.txt, as plain text
    bool showWelcome_ = false;          // the first start (no project.json yet), or Help > Welcome
    double sculptSeconds_ = 0.0;        // cumulative time in strokes (project.json)
    bool supportReminderDone_ = false;  // the reminder was shown (or turned off) once: never again (project.json)
    bool supportReminderOpen_ = false;
    int supportPageRequests_ = 0;       // UI tests: how often the page was asked for (only opened when interactive)

    // Results from asynchronous native file dialogs (called off the main thread).
    struct PendingFile {
        FileAction action;
        std::string path;
    };
    DialogRequest dialogRequests_[int(FileAction::Count)];

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
    bool remeshGenerated_ = true;
    int voxelResolution_ = 200;
    bool voxelKeepMasked_ = false; // Voxel Remesh's Keep Masked (#43)
    static constexpr const char* kKeepMaskedHelp =
        "Masked areas keep their triangles exactly as they are (their detail too); only the rest is rebuilt, and "
        "it's joined to them along the mask's edge so the model stays closed. Mask what to keep first (Ctrl). "
        "If the mask's edge is too ragged to join up, the status line says so and nothing changes.";
    bool keepTexture_ = true; // Keep Texture: remeshes carry a painted texture over (core/TextureTransfer.h)
    int remeshResolution_ = 256;
    std::future<ai::GeneratorResult> pendingGenerator_;
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
    float maskCanvasX_ = 0.0f, maskCanvasY_ = 0.0f;
    bool maskCanvasMoved_ = false;
    // Polygroups & visibility (PanelPolygroups.cpp): Ctrl+Shift click/drag.
    bool polygroupMouseDown(float x, float y, SDL_Keymod mod); // true if it took the click
    void polygroupMouseUp();
    void drawPolygroupsSection();
    // Screen selections (Selection.cpp): the rectangle or lasso dragged for
    // visibility (Ctrl+Shift, ZBrush SelectRect / SelectLasso) or for the mask
    // (Ctrl, MaskRect / MaskLasso). Pen is the mask's brush-stroke mode.
    enum class SelectShape { Pen, Lasso, Rect };
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
    Spray spray_;
    float sprayScatter_ = 1.0f;          // jitter radius as a fraction of brush size
    glm::vec2 dragAnchor_{0.0f};         // DragRect: where the click landed (window pixels)
    glm::vec3 dragAnchorHit_{0.0f};
    float dabRadiusScale_ = 1.0f, dabStrengthScale_ = 1.0f, dabAngle_ = 0.0f; // per-dab overrides
    bool alphaInvert_ = false;
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
    double pbrTestValue(const std::string& name) const; // UI test values (UiTest.cpp), NaN when not one of ours
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
    int zsphereResolution_ = 128;                         // Make Mesh resolution
    struct ZSphereDrag {
        ZSphereMode op = ZSphereMode::Draw; // what this drag does (a Draw click on a link then moves the new joint)
        int sphere = -1;               // the sphere being drawn, moved or scaled
        std::vector<ZSphere> start;    // the armature when the drag began
        Mesh before;                   // the SubTool's mesh then (the undo step's old state)
        glm::vec3 grab{0.0f};          // Move: the centre's offset from the grab plane's hit
        float x = 0.0f, y = 0.0f;      // where the press was
        bool changed = false;
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
    bool showWhatsNew_ = false; // the first start after an update opens the guide's What's New chapter
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
    float subtoolClickX_ = 0.0f, subtoolClickY_ = 0.0f;
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
    void drawLightBoxUi(); // every frame from drawUi: thumbnails, the LightBox, pickers, size / strength pop-ups
    // The LightBox / picker shortcuts (, B M S) until the action registry dispatches keys.
    bool lightBoxKey(SDL_Keycode key, SDL_Keymod mod);
    void quickSave();                  // file.quicksave: a timestamped project in <library>/QuickSave
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
    bool fullscreenAfterFirstFrame_ = false; // macOS: the saved full screen, entered once the window is on screen (#41)
    void drawAxisGizmo();
    std::array<AxisGizmoEnd, 6> axisGizmoLayout() const; // the gizmo's ends on screen now (AxisGizmo.h)
    bool axisGizmoMouseDown(float x, float y);          // a click on an end: that side view (#43)
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
    // ---- ZBrush feel (Input.cpp, docs/zbrush-parity.md) ----
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
    // ZBrush's alternate smooth (#40): a stroke begun with Shift held that lets go of Shift while still pressing
    // smooths the other way (keeping the volume, or plainly when Smooth keeps volume) for the rest of the stroke.
    bool strokeAltSmooth_ = false;
    float rgbIntensity_ = 1.0f;   // paint strength (ZBrush RGB Intensity, 100 by default)
    float focalShift_ = 0.0f;     // -1..1 (ZBrush Focal Shift -100..100)
    struct QuickMenu {
        bool open = false;
        ImVec2 at{0, 0};
        double openedAt = 0.0;  // seconds (a short press is a tap)
        bool keyHeld = false;   // Space is still down
        bool untilLeave = false; // Space let go over the menu: it closes when the pointer leaves it
        int openedFrame = -1;
    } quickMenu_;
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
    uint64_t frameKey() const;          // changes when a poller changed what the window shows
    double activeUntil_ = 0.0;          // seconds: keep drawing until then (after input: hover, tooltips)
    double redrawAt_ = 0.0;             // seconds (SDL clock): draw one frame then, asleep or not (0: none)
public:
    void redrawIn(double seconds);      // something on screen changes by itself later (an error colour fading)
private:
    // ---- Tablet (Tablet.cpp, core/Tablet.h, docs/tablet.md) ----
    // SDL turns pen tip contact into left mouse button events (SDL_PEN_MOUSEID); the pen's own events
    // add pressure, tilt, the eraser end and the side buttons, which are mapped in Preferences > Tablet.
    bool dropPenMouseEvent(const SDL_Event& e) const; // SDL's version-dependent clicks for pen buttons
    bool strayMouseMotion(const SDL_Event& e) const;  // a driver's mouse motion beside the pen's (Tablet.cpp)
    void keepImGuiOnPen();                            // after ImGui_ImplSDL3_NewFrame (Tablet.cpp)
    void mapPenPosition(SDL_Event& e);                // mouse mode: the pen moves the cursor relatively (Tablet.cpp)
    void handlePenEvent(const SDL_Event& e);
    void penButton(int button, bool down, float x, float y);
    void penLeft(); // out of proximity: ends what the pen was doing
    float strokePressure() const; // pressure after the global curve; 1 for the mouse
    SDL_Keymod inputMods() const; // the keyboard's modifiers plus those a pen button holds (Alt, Shift)
    bool erasing() const;         // the pen's eraser end is drawing
    PenAction eraserAction() const { return uiPrefs_.tablet.eraser; }
    void firstDab(float x, float y); // the stroke's first dab, once the pen reports a real pressure
    void drawTabletSection();         // Preferences > Tablet
    void detectPenName();
    PenPressureTracker penTracker_;
    PenCursor penCursor_; // where the pen puts the cursor (Preferences > Tablet > Mouse mode)
    bool penInProximity_ = false, penTouching_ = false, penEraser_ = false;
    Uint64 lastPenNs_ = 0; // when the last pen event arrived (see onMouseMove)
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
    void registerTimelineUiActions(); // view.timeline, timeline.timelapse, help.benchmark
    void startTimelapse(const std::string& path, int width, int height, float seconds, int fps);
    bool timelapseStep(double budgetSeconds); // renders frames for up to the budget; false when finished
    int runTimelapseCli();                    // sculptforge FILE --timelapse out.mp4
    void startProofMode();                    // Help > Benchmark, sculptforge --benchmark
    void proofBeforeSwap(int fbW, int fbH);   // the interactive loop, after drawing a frame, before showing it
    void drawProofUi();
    // ---- end Sculpt Timeline ----
};

} // namespace sf
