// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Strokes on heavy meshes (App.h): the solver thread, the per-frame hand-over, the ghost and the pen prediction.
//
// One frame of a stroke on the main thread: events queue dabs (strokeTo); pumpStroke commits the batch the
// solver finished (its packed vertices go up with glBufferSubData, its dabs leave the ghost) and hands it the
// dabs waiting; buildGhost gives the renderer every dab not on the GPU yet; the frame draws and swaps while the
// solver works. The mesh, its BVH and the sculptor belong to the solver while it is busy: nothing on the main
// thread reads or writes them then (syncScene and the pollers wait, and every path that edits the document
// ends the stroke first, which waits for the solver).
#include "app/AsyncStroke.h"

#include "core/Micro.h"
#include "core/PerfTrace.h"
#include "core/Sculpt.h"
#include "core/TierConfig.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>

namespace sf {

namespace {

constexpr double kBatchSeconds = 0.033; // a batch's dabs: about this much work (the surface updates this often)
// Background dabs (no preview to cover a batch): batches of about a frame's share of work, as the frame loop places.
constexpr double kBackgroundBatchSeconds = 0.010;
// A fused batch (Fast Big Brushes) takes as many dabs as about this much work: a batch shows only when it is done,
// so on slow cores a long batch is lag, while on fast ones the dabs share the vertex walk, refit and upload.
constexpr double kFusedBatchSeconds = 0.05;
// Meshes from this many vertices place a stroke's dabs on the solver thread even without predictive strokes: one
// 240 px dab there moves millions of vertices (100-300 ms at 33M triangles), more than a frame can hold.
constexpr size_t kBackgroundDabVertices = 2'000'000;

} // namespace

App::StrokeSolver::StrokeSolver(App& a) : app(a) { thread_ = std::thread([this] { loop(); }); }

App::StrokeSolver::~StrokeSolver() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        quit_ = true;
    }
    wake_.notify_all();
    if (thread_.joinable()) thread_.join();
}

void App::StrokeSolver::start() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        pending_ = true;
        done = false;
        busy_.store(true, std::memory_order_release);
    }
    wake_.notify_one();
}

void App::StrokeSolver::wait() {
    // The main thread waits here for the stroke thread (it must: the mesh is the stroke's until it is done). A
    // wait of seconds is a frozen window (phoenix, Discord 11-05): say so in the log. (`batch` is only read there.)
    std::unique_lock<std::mutex> lock(mutex_);
    const auto start = std::chrono::steady_clock::now();
    while (!idle_.wait_for(lock, std::chrono::seconds(2), [this] { return !busy_.load(std::memory_order_acquire); }))
        SDL_Log("stroke: the main thread has waited %.0f s for the stroke thread (a batch of %zu dabs)",
                std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count(), batch.size());
}

bool App::StrokeSolver::waitFor(double seconds) {
    std::unique_lock<std::mutex> lock(mutex_);
    return idle_.wait_for(lock, std::chrono::duration<double>(seconds), [this] { return !busy_.load(std::memory_order_acquire); });
}

void App::StrokeSolver::loop() {
    for (;;) {
        {
            std::unique_lock<std::mutex> lock(mutex_);
            wake_.wait(lock, [this] { return pending_ || quit_; });
            if (quit_) return;
            pending_ = false;
        }
        probe.clear();
        radiusProbe.clear();
        double spent = 0.0;
        const SculptTimings before = app.doc_.sculptor.timings;
        app.dabView_ = &view;
        if (finalize) { // a draft stroke's end: every normal and cavity term it touched
            const auto t0 = std::chrono::steady_clock::now();
            app.doc_.sculptor.flushDraft(app.doc_.mesh, app.doc_.bvh);
            spent += std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        }
        app.doc_.sculptor.beginBatch(); // (a big brush's dabs go through the vertices together: core/SculptBatch.cpp)
        for (const PathDab& d : batch) {
            const auto t0 = std::chrono::steady_clock::now();
            app.dabAt(d.x, d.y, d.pressure);
            spent += std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        }
        {
            const auto t0 = std::chrono::steady_clock::now();
            app.doc_.sculptor.endBatch(app.doc_.mesh, app.doc_.bvh);
            // Nothing more waiting: the exact normals behind the brush catch up (a frame's share of them).
            if (catchUp && !finalize) app.doc_.sculptor.catchUpNormals(app.doc_.mesh, app.doc_.bvh, catchUpLeaves);
            // A share of the exact normals the brush has left behind, every batch: lifting the pen then waits for
            // the last stretch only (they cost a tenth of what the dab-by-dab normals did over the stroke).
            else if (!finalize) app.doc_.sculptor.catchUpBehind(app.doc_.mesh, app.doc_.bvh, 0.35);
            spent += std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        }
        app.dabView_ = nullptr;
        if (!kelvinSteps.empty() || kelvinFar) { // a Kelvinlet stroke's moves (PanelKelvinlet.cpp)
            const auto t0 = std::chrono::steady_clock::now();
            for (const KelvinletStep& step : kelvinSteps)
                app.doc_.sculptor.kelvinletStep(app.doc_.mesh, app.doc_.bvh, kelvinBrush, step, kelvinSymmetry);
            if (kelvinSteps.empty() && kelvinFar) app.doc_.sculptor.flushKelvinletFar(app.doc_.mesh, app.doc_.bvh, false);
            spent += std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            kelvinSteps.clear();
            kelvinFar = false;
        }
        dabSeconds = batch.empty() ? 0.0 : spent / double(batch.size());
        const auto prepStart = std::chrono::steady_clock::now();
        uploadReady = prepare && app.renderer_.prepareStrokeUpload(uploadId, app.doc_.mesh, upload, app.doc_.sculptor.strokeCurvature());
        if (finalize && uploadReady) app.renderer_.refineStrokeBoundsInto(uploadId, app.doc_.mesh, upload);
        prepSeconds = finalize ? 0.0 : std::chrono::duration<double>(std::chrono::steady_clock::now() - prepStart).count();
        static const bool log = std::getenv("SF_STROKE_LOG") != nullptr;
        if (log) {
            const SculptTimings& t = app.doc_.sculptor.timings;
            SDL_Log("solver: %zu dabs %.1f ms (kernel %.1f, normals %.1f, BVH %.1f), upload packed in %.1f ms (vertices %.1f, "
                    "bounds %.1f), %.1f MB",
                    batch.size(), spent * 1000.0, t.kernelMs - before.kernelMs, t.normalsMs - before.normalsMs,
                    t.bvhMs - before.bvhMs,
                    std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - prepStart).count(),
                    upload.packMs, upload.boundsMs, double(upload.bytes()) / 1e6);
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            done = true;
            busy_.store(false, std::memory_order_release);
        }
        idle_.notify_all();
        wakeMainLoop();
    }
}

// ---------------------------------------------------------------------------

bool App::predictiveStrokes() const {
    static const int env = [] {
        const char* v = std::getenv("SF_PREDICTIVE_STROKE");
        return v && *v ? (*v == '0' ? 0 : 1) : -1;
    }();
    return env >= 0 ? env == 1 : uiPrefs_.predictiveStrokes;
}

bool App::fastBigBrushes() const {
    static const int env = [] {
        const char* v = std::getenv("SF_ADAPTIVE_DABS");
        return v && *v ? (*v == '0' ? 0 : 1) : -1;
    }();
    return env >= 0 ? env == 1 : uiPrefs_.fastBigBrushes;
}

bool App::draftStrokes() const {
    static const int env = [] {
        const char* v = std::getenv("SF_DRAFT_STROKE");
        return v && *v ? (*v == '0' ? 0 : 1) : -1;
    }();
    // The hardware profile's tier decides (draft on laptops and on battery, core/TierConfig.h); the preference
    // turns it on whatever the tier.
    return predictiveStrokes() && (env >= 0 ? env == 1 : uiPrefs_.draftStrokes || currentTierConfig().draftBrushMode);
}

bool App::backgroundDabs() const {
    static const int env = [] {
        const char* v = std::getenv("SF_BACKGROUND_DABS");
        return v && *v ? (*v == '0' ? 0 : 1) : -1;
    }();
    // The same dabs in the same order as the frame loop places them, only on the solver thread: the frame keeps
    // drawing what the GPU has while a big dab is worked out, and letting go doesn't wait for the dabs queued.
    return env >= 0 ? env == 1 : doc_.mesh.vertexCount() >= kBackgroundDabVertices;
}

bool App::previewStrokes() const {
    // Background Preview (Preferences > Performance, experimental; SF_BG_GHOST=0 / 1 overrides): background dabs
    // draw the predictive preview too (it never changes the mesh: the solver's dabs are the same), and the frame
    // doesn't wait for them.
    static const int env = [] {
        const char* v = std::getenv("SF_BG_GHOST");
        return v && *v ? (*v == '0' ? 0 : 1) : -1;
    }();
    return predictiveStrokes() || ((env >= 0 ? env == 1 : uiPrefs_.backgroundPreview) && backgroundDabs());
}

bool App::asyncStrokeAllowed() const {
    // Sculpt strokes of queued dabs only: Move, Snake Hook and the drag strokes work in strokeTo itself; painting,
    // masking and dynamic topology (which changes the layout the renderer packs for) stay in the frame loop.
    const bool recipe = strokeRecipe() != nullptr;
    return interactiveStrokes_ && (predictiveStrokes() || backgroundDabs()) && strokeSculpt_ && !strokePaint_ && !strokeMask_ &&
           !doc_.sculptor.dyntopo.enabled && previewLod_ < 0 && !faceTakePlaying_ &&
           !doc_.mesh.micro && // (a micro level is sculpted and drawn by core/MicroSculpt and the MicroRenderer)
           (recipe || (strokeBrush_ != BrushType::Move && strokeBrush_ != BrushType::SnakeHook)) &&
           strokeType_ != StrokeType::DragRect && strokeType_ != StrokeType::DragDot;
}

bool App::solverBusy() const { return solver_ && solver_->busy(); }

void App::solverWait() {
    if (!solver_) return;
    solver_->wait();
    if (solver_->done) commitSolverBatch();
}

void App::commitSolverBatch() {
    StrokeSolver& s = *solver_;
    s.done = false;
    ++commits_;
    // SF_MEASURE_POP: the view before the commit, with and without the batch's preview.
    std::vector<uint8_t> withPreview, withoutPreview, after;
    const CanvasRect canvas = canvasRect();
    const float scale = std::min(1.0f, 640.0f / std::max(canvas.w, canvas.h));
    const int pw = std::max(1, int(canvas.w * scale)), ph = std::max(1, int(canvas.h * scale));
    auto render = [&](std::vector<uint8_t>& out) {
        std::string error;
        Camera cam = camera_;
        cam.aspect = float(pw) / float(ph);
        renderer_.renderToImage(cam, pw, ph, renderSettings_, out, error);
    };
    const bool probe = popProbe_ && !s.batch.empty();
    if (probe) {
        buildGhost();
        render(withPreview);
        std::vector<PathDab> batch;
        batch.swap(s.batch);
        buildGhost();
        render(withoutPreview);
        batch.swap(s.batch);
    }
    if (s.uploadReady) {
        const auto t0 = std::chrono::steady_clock::now();
        renderer_.commitStrokeUpload(s.upload);
        s.uploadReady = false;
        static const bool log = std::getenv("SF_STROKE_LOG") != nullptr;
        if (log) SDL_Log("commit: %.1f ms", std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
    } else {
        syncScene(); // (the regular sync: a new grid, or a mesh the two-part upload doesn't handle)
    }
    if (strokeProbe_) strokeProbe_->insert(strokeProbe_->end(), s.probe.begin(), s.probe.end());
    if (strokeRadiusProbe_) strokeRadiusProbe_->insert(strokeRadiusProbe_->end(), s.radiusProbe.begin(), s.radiusProbe.end());
    if (s.prepSeconds > 0.0) prepSeconds_ = prepSeconds_ > 0.0 ? prepSeconds_ * 0.7 + s.prepSeconds * 0.3 : s.prepSeconds;
    if (!s.batch.empty()) {
        lastDabScreen_ = glm::vec2(s.batch.back().x, s.batch.back().y);
        if (s.dabSeconds > 0.0) dabSeconds_ = dabSeconds_ > 0.0 ? dabSeconds_ * 0.8 + s.dabSeconds * 0.2 : s.dabSeconds;
        // The fused batch size, toward kFusedBatchSeconds of work: scaled by how far this batch was from it (down at once, up
        // at most 2x a batch, kept fractional so it climbs back from 1 where fusing pays). It settles where a batch costs
        // the budget; on cores where one dab alone costs more, at one dab a batch.
        if (s.dabSeconds > 0.0 && doc_.sculptor.fusesBatches()) {
            const double size = double(s.batch.size()), from = size >= std::floor(fusedDabs_) ? fusedDabs_ : size;
            fusedDabs_ = std::clamp(from * std::clamp(kFusedBatchSeconds / (s.dabSeconds * size), 0.0, 2.0), 1.0, 64.0);
        }
    }
    if (doc_.sculptor.lastFrame.valid) {
        // The surface's curvature along the stroke (signed, 1 / radius; > 0 convex), from how the area normal
        // turned since the last frame, smoothed over the batches; small turns count as flat.
        const Sculptor::DabFrame& f = doc_.sculptor.lastFrame;
        const glm::vec3 d = f.center - ghostFrame_.center;
        const float dist = glm::length(d);
        const float angle = std::acos(std::clamp(glm::dot(glm::normalize(f.normal), glm::normalize(ghostFrame_.normal)), -1.0f, 1.0f));
        if (dist > 1e-6f) {
            float k = angle > glm::radians(0.5f) ? angle / dist : 0.0f;
            if (glm::dot(d, f.normal - ghostFrame_.normal) < 0.0f) k = -k; // normals converging: concave
            ghostCurvature_ = ghostCurvature_ == 0.0f ? k : ghostCurvature_ * 0.5f + k * 0.5f;
        }
        ghostFrame_ = f;
    }
    s.batch.clear();
    if (probe) {
        buildGhost();
        render(after);
        auto diff = [&](const std::vector<uint8_t>& a) {
            uint64_t sum = 0;
            for (size_t i = 1; i < a.size() && i < after.size(); i += 4) sum += uint64_t(std::abs(int(a[i]) - int(after[i])));
            return double(sum) / double(std::max<size_t>(1, after.size() / 4));
        };
        const double with = diff(withPreview);
        popWith_ += with;
        popWithout_ += diff(withoutPreview);
        popWorst_ = std::max(popWorst_, with);
        ++popCommits_;
    }
}

void App::pumpStroke() {
    if (!asyncStroke_) return;
    if (kelvinStrokeBrush()) return; // (Kelvinlet strokes hand their moves over in kelvinCatchUp)
    if (!solver_) solver_ = std::make_unique<StrokeSolver>(*this);
    if (solver_->busy()) return;
    if (solver_->done) commitSolverBatch();
    if (dabQueue_.empty() && strokeFinishing_ && !solver_->finalize) {
        // Released: the stroke's end work on the solver thread too (a draft's normals and cavities, the exact
        // culling bounds of what it moved), then the end.
        StrokeSolver& s = *solver_;
        s.batch.clear();
        s.finalize = true;
        s.catchUp = false;
        s.view.camera = partCamera();
        s.view.canvas = canvasRect();
        s.view.probe = nullptr;
        s.view.radiusProbe = nullptr;
        s.view.settings = dabSettings();
        s.uploadId = doc_.meshKey(doc_.activePart);
        renderer_.setPersistentUpload(uiPrefs_.fastUpload);
    renderer_.bindStrokeUpload(s.uploadId, s.upload); // (the slot, found here: the thread doesn't walk the renderer's list)
        s.prepare = &displayedMesh() == &doc_.mesh;
        s.start();
        return;
    }
    if (dabQueue_.empty()) {
        if (strokeFinishing_) {
            finishStrokeNow(); // released, and every dab is on the GPU
            return;
        }
        // The pen rests (no dabs for a tenth of a second): the exact normals behind the brush (Fast Big Brushes)
        // catch up meanwhile. (Not while dabs keep coming: a catch-up batch would hold the next one back.)
        if (!doc_.sculptor.normalsPending() || double(SDL_GetTicksNS()) * 1e-9 - lastDabsQueued_ < 0.1) return;
        StrokeSolver& s = *solver_;
        s.batch.clear();
        s.finalize = false;
        s.catchUp = true;
        s.view.camera = partCamera();
        s.view.canvas = canvasRect();
        s.view.probe = nullptr;
        s.view.radiusProbe = nullptr;
        s.view.settings = dabSettings();
        s.uploadId = doc_.meshKey(doc_.activePart);
        renderer_.bindStrokeUpload(s.uploadId, s.upload);
        s.prepare = &displayedMesh() == &doc_.mesh;
        s.start();
        return;
    }
    // The next batch, oldest first: about kBatchSeconds of work, or as much as packing its upload costs (so the
    // packing, per batch, stays at most about half the work), at least one dab; once the pen has lifted, every
    // dab left (the preview shows them all: only the end result is waited for), with the stroke's end work.
    StrokeSolver& s = *solver_;
    // (Background dabs: about a frame of work, which the solver does while the frame draws: at least as many dabs
    // a frame as the frame loop placed, however long the frame.)
    const double target = std::max(predictiveStrokes() ? kBatchSeconds : std::max(kBackgroundBatchSeconds, frameSeconds_ * 0.8),
                                   prepSeconds_);
    size_t n = dabSeconds_ > 0.0 ? size_t(std::clamp(target / dabSeconds_, 1.0, 64.0)) : 1;
    // A big brush's batch goes through the vertices together (Fast Big Brushes): about kFusedBatchSeconds of
    // dabs at once (every dab left once the pen has lifted).
    if (doc_.sculptor.fusesBatches()) n = size_t(fusedDabs_);
    if (strokeFinishing_) n = 64;
    s.batch.clear();
    while (!dabQueue_.empty() && s.batch.size() < n) s.batch.push_back(dabQueue_.pop());
    s.finalize = strokeFinishing_ && dabQueue_.empty();
    s.catchUp = false; // (dabs are coming: the exact normals wait for a pause or the release)
    lastDabsQueued_ = double(SDL_GetTicksNS()) * 1e-9;
    s.view.camera = partCamera();
    s.view.canvas = canvasRect();
    s.view.probe = strokeProbe_ ? &s.probe : nullptr;
    s.view.radiusProbe = strokeProbe_ ? &s.radiusProbe : nullptr;
    s.view.settings = dabSettings();
    s.uploadId = doc_.activePartInfo().id;
    renderer_.setPersistentUpload(uiPrefs_.fastUpload);
    renderer_.bindStrokeUpload(s.uploadId, s.upload); // (the slot, found here: the thread doesn't walk the renderer's list)
    s.prepare = &displayedMesh() == &doc_.mesh; // (a previewed LOD shows instead: nothing to pack)
    renderer_.setDraftNormals(doc_.sculptor.draftActive() ? s.uploadId : 0);
    s.start();
    // Background dabs: a batch is waited for here up to a frame's share of work, so a light one shows in this frame
    // as in the frame loop's own path; a heavier one goes on while the frame draws, and the next frame commits it.
    // (How often the batch isn't done in time: a stroke whose solver is the bottleneck. The frame loop then stops
    // overlapping the GPU with it: see App::run.)
    if (!previewStrokes() && dabSeconds_ > 0.0) {
        const bool inTime = s.waitFor(kBackgroundBatchSeconds * 1.25);
        solverLateShare_ += 0.1 * ((inTime ? 0.0 : 1.0) - solverLateShare_);
        // (On above a fifth late, off below one in twenty: Lucy 28M's ~35% stays on between its late runs.)
        if (solverLateShare_ > 0.2) solverBound_ = true;
        else if (solverLateShare_ < 0.05) solverBound_ = false;
        if (inTime && s.done) commitSolverBatch();
    }
}

bool App::liveMoveEnabled() const {
    static const int env = [] {
        const char* v = std::getenv("SF_LIVE_MOVE");
        return v && *v ? (*v == '0' ? 0 : 1) : -1;
    }();
    return env >= 0 ? env == 1 : uiPrefs_.liveMove;
}

void App::commitLiveMove() {
    if (!liveMove_.active) return;
    liveMove_.active = false;
    // One grab with the whole move: the weights are the grab's own, so this is the sum of the per-frame grabs.
    if (glm::dot(liveMove_.delta, liveMove_.delta) > 0.0f) doc_.sculptor.grab(doc_.mesh, doc_.bvh, liveMove_.delta);
    renderer_.setGhost(0, GhostStroke());
}

void App::finishStrokeNow() {
    commitLiveMove();
    if (solver_) solver_->finalize = false;
    doc_.sculptor.flushDraft(doc_.mesh, doc_.bvh); // (a draft stroke ended here rather than on the solver thread)
    renderer_.setDraftNormals(0);
    if (asyncStroke_) renderer_.refineStrokeBounds(doc_.meshKey(doc_.activePart), displayedMesh()); // (the quick ones)
    strokeFinishing_ = false;
    asyncStroke_ = false;
    renderer_.setGhost(0, GhostStroke());
    ghostHeadValid_ = false;
    strokeMask_ = false;
    maskClickPending_ = false;
    firstDabPending_ = false;
    // Preferences > Performance shows what the stroke cost (and Help > Record a performance trace writes it).
    const bool traceStroke = doc_.sculptor.timings.dabs > 0 && perftrace::on();
    if (doc_.sculptor.timings.dabs > 0) {
        perf_.lastStroke = doc_.sculptor.timings;
        perf_.lastStrokeSyncMs = perf_.strokeSyncMs;
        perf_.lastStrokeFrames = perf_.strokeFrames;
    }
    doc_.sculptor.timings = {};
    perf_.strokeSyncMs = 0.0;
    perf_.strokeFrames = 0;
    UndoRecord r = doc_.sculptor.endStroke();
    if (traceStroke) tracePerfStroke(r.label);
    finishPaintStroke(r);
    if (r.topology && !r.topology->uvs.empty())
        status_ = "Dynamic topology removed the UVs of the remeshed Part (Undo brings them back)";
    strokeEndedForRemesh(r); // Tool > Remesh > Remesh after each stroke (#121): stretched enough to remesh?
    if (!r.empty()) {
        doc_.undo.push(std::move(r));
        doc_.modified = true;
        updateTitle();
    }
}

App::DabSettings App::dabSettings() const {
    DabSettings st;
    st.radiusPx = radiusPx_;
    st.worldRadius = worldSize_ && worldRadius_ > 0.0f ? worldRadius_ : 0.0f;
    st.pressureCurve = strokeCurve_;
    st.strength = precisionActive_ ? precisionIntensity_ : strength_; // Shift+Alt mid-stroke (Precision.cpp)
    st.smoothStrength = smoothStrength_;
    st.smoothKeepVolume = smoothKeepVolume_ != strokeAltSmooth_;
    st.smoothGroupBorders = smoothGroupBorders_;
    st.focal = focalShift_;
    st.maskStrength = maskStrength_;
    st.sprayScatter = sprayScatter_;
    st.alphaOptions = alphaOptions_;
    st.pressureSize = pressureSizeOn();
    st.pressureStrength = pressureStrengthOn();
    st.invert = strokeInvert_;
    st.frontFacesOnly = frontFacesOnly_;
    st.connectedOnly = uiPrefs_.connectedOnly;
    st.viewFalloff = uiPrefs_.viewFalloff;
    st.mask = strokeMask_;
    st.paint = strokePaint_;
    st.sculpt = strokeSculpt_;
    st.visible = doc_.activePartInfo().visible;
    st.brush = strokeBrush_;
    st.strokeType = strokeType_;
    st.symmetry = symmetry_;
    // A predictive stroke works from its own copies of the recipe and alpha (made when it began).
    st.recipe = asyncStroke_ ? strokeRecipe_.get() : strokeRecipe();
    st.alpha = asyncStroke_ ? strokeAlpha_.get() : &doc_.brushAlpha; // (null: round)
    st.stencil = doc_.stencil;
    st.vdmOwner = doc_.brushVdm;
    st.vdm = st.vdmOwner.get();
    return st;
}

bool App::pickWith(const DabView& view, float x, float y, RayHit& hit) const {
    if (!view.settings.visible) return false;
    glm::vec3 origin, dir;
    view.camera.screenRay(x - view.canvas.x, y - view.canvas.y, view.canvas.w, view.canvas.h, origin, dir);
    return frontRaycast(origin, dir, hit);
}

glm::vec2 App::predictedPointer() const {
    if (!previewStrokes() || drag_ != Drag::Stroke || !asyncStroke_) return glm::vec2(mouseX_, mouseY_);
    // To when this frame reaches the screen: about a frame after the newest sample.
    const double ahead = std::clamp(frameSeconds_, 0.004, PenPredictor::kMaxAhead);
    return penPredictor_.predict(pointerLatestAt_ + ahead, pointerLatest_);
}

bool App::ghostSurface(const glm::vec3& origin, const glm::vec3& dir, glm::vec3& at, glm::vec3& normal) const {
    const glm::vec3 n0 = glm::normalize(ghostFrame_.normal);
    const float denom = glm::dot(dir, n0);
    if (std::abs(denom) < 1e-4f) return false;
    const float tp = glm::dot(ghostFrame_.center - origin, n0) / denom; // the tangent plane
    if (tp <= 0.0f) return false;
    at = origin + dir * tp;
    normal = n0;
    if (ghostCurvature_ == 0.0f) return true;
    // The sphere: centre one radius behind (convex) or in front of (concave) the last dab, along its normal.
    const float R = 1.0f / std::abs(ghostCurvature_), side = ghostCurvature_ > 0.0f ? 1.0f : -1.0f;
    const glm::vec3 C = ghostFrame_.center - n0 * (side * R);
    const glm::vec3 oc = origin - C;
    const float b = glm::dot(oc, dir), cc = glm::dot(oc, oc) - R * R, disc = b * b - cc;
    if (disc < 0.0f) return true; // (the ray misses it: the plane)
    const float sq = std::sqrt(disc);
    const float t = std::abs(-b - sq - tp) < std::abs(-b + sq - tp) ? -b - sq : -b + sq; // the root nearer the plane's
    if (t <= 0.0f) return true;
    at = origin + dir * t;
    normal = glm::normalize(at - C) * side;
    return true;
}

void App::buildGhost() {
    ghostHeadValid_ = false;
    renderer_.setScreenStroke(GhostStroke()); // (set again below when this frame's dabs draw on the screen)
    const uint32_t id = doc_.meshKey(doc_.activePart); // (the renderer's slot: linked Parts share it)
    if (liveMove_.active) { // Live Move: one "dab" per mirror, the whole move so far
        GhostStroke g;
        g.kind = GhostKind::Move;
        g.focal = focalShift_;
        std::vector<SymCopy> mirrors = Sculptor::copies(symmetry_);
        if (mirrors.size() > GhostStroke::kMaxMirrors) mirrors.resize(GhostStroke::kMaxMirrors);
        for (uint32_t m = 0; m < mirrors.size(); ++m) {
            GhostDab d;
            d.center = mirrors[m].point(liveMove_.center);
            d.radius = liveMove_.radius;
            d.normal = mirrors[m].m * liveMove_.delta;
            d.strength = liveMove_.scale;
            d.areaCenter = d.center;
            d.mirror = m;
            g.dabs.push_back(d);
        }
        renderer_.setGhost(id, g);
        return;
    }
    static const bool noGhost = [] { // SF_GHOST=0: the solver thread without the preview (for comparison)
        const char* v = std::getenv("SF_GHOST");
        return v && *v == '0';
    }();
    if (!asyncStroke_ || !solver_ || !previewStrokes() || noGhost) {
        renderer_.setGhost(id, GhostStroke());
        return;
    }
    GhostStroke g;
    g.kind = strokeRecipe() ? GhostKind::None : ghostKindForBrush(int(strokeBrush_));
    g.invert = strokeInvert_;
    g.focal = focalShift_;
    g.edge = ghostEdge_;
    // Where the dabs not on the GPU yet are on screen: those in flight, those waiting, and the head.
    struct Screen {
        float x, y, pressure, weight;
    };
    std::vector<Screen> pending;
    for (const PathDab& d : solver_->batch) pending.push_back({d.x, d.y, d.pressure, 1.0f});
    for (const PathDab& d : dabQueue_.items()) pending.push_back({d.x, d.y, d.pressure, 1.0f});
    // The head: from the last dab to where the pen will be when this frame shows (not for strokes that move the
    // pointer's path: Steady Stroke, the precision drag, a Shift line, jitter or spray), in part, as the dabs the pen is about to lay.
    const float spacing = dabSpacingPx(radiusPx_, strokeRecipe() ? strokeRecipe()->spacing : 0.15f);
    if (drag_ == Drag::Stroke && !steadyOn() && !precisionActive_ && precision_.caughtUp() &&
        strokeType_ != StrokeType::Spray && !straight_.active &&
        !(strokeRecipe() && strokeRecipe()->jitter > 0.0f)) {
        const glm::vec2 head = predictedPointer();
        const glm::vec2 from = !pending.empty() ? glm::vec2(pending.back().x, pending.back().y) : lastDabScreen_;
        const float gap = glm::distance(head, from);
        const bool moving = glm::length(penPredictor_.velocity()) > 20.0f; // px / s
        if (moving && gap > 0.2f * spacing)
            pending.push_back({head.x, head.y, lastStrokePressure_, std::min(1.0f, gap / spacing)});
    }
    if (!pending.empty()) {
        ghostHead_ = glm::vec2(pending.back().x, pending.back().y);
        ghostHeadValid_ = true;
    }
    if (g.kind == GhostKind::None || pending.empty() || !ghostFrame_.valid) {
        renderer_.setGhost(id, GhostStroke());
        return;
    }
    // Each dab's surface point and normal: where its ray meets the surface as the last dabs placed describe it, a
    // sphere through the last one with the curvature the last two show (their normals turn by an angle over the
    // distance between them), or its tangent plane where they don't turn. The solver's result replaces the
    // estimate when it arrives.
    const CanvasRect c = canvasRect();
    const glm::vec3 n0 = glm::normalize(ghostFrame_.normal);
    const float depth = glm::dot(ghostFrame_.center - ghostFrame_.areaCenter, n0); // area centre below the surface
    std::vector<SymCopy> mirrors = Sculptor::copies(symmetry_);
    if (mirrors.size() > GhostStroke::kMaxMirrors) mirrors.resize(GhostStroke::kMaxMirrors); // (the shader's groups)
    const size_t perMirror = GhostStroke::kMaxDabs / std::max<size_t>(1, mirrors.size());
    std::vector<GhostDab> base;
    const Camera cam = partCamera();
    for (const Screen& sd : pending) {
        if (base.size() >= perMirror) break;
        glm::vec3 origin, dir, n;
        cam.screenRay(sd.x - c.x, sd.y - c.y, c.w, c.h, origin, dir);
        GhostDab d;
        if (!ghostSurface(origin, dir, d.center, n)) continue;
        d.radius = (worldSize_ && worldRadius_ > 0.0f ? worldRadius_ : radiusPx_ * cam.worldPerPixel(d.center, c.h)) *
                   (pressureSizeOn() ? std::max(sd.pressure, 0.05f) : 1.0f);
        d.normal = n;
        d.strength = std::clamp((strokeBrush_ == BrushType::Smooth ? smoothStrength_ : strength_), 0.0f, 1.0f) *
                     std::clamp(pressureStrengthOn() ? sd.pressure : 1.0f, 0.0f, 1.0f);
        d.areaCenter = d.center - n * depth;
        d.weight = sd.weight;
        base.push_back(d);
    }
    for (uint32_t m = 0; m < mirrors.size(); ++m)
        for (const GhostDab& b : base) {
            GhostDab d = b;
            d.center = mirrors[m].point(b.center);
            d.normal = mirrors[m].dir(b.normal);
            d.areaCenter = mirrors[m].point(b.areaCenter);
            d.mirror = m;
            g.dabs.push_back(d);
        }
    // Research (render/ScreenStroke.h; SF_SCREEN_STROKE=1): the pending dabs drawn on the view, not the geometry.
    static const bool screenStroke = [] {
        const char* v = std::getenv("SF_SCREEN_STROKE");
        return v && *v == '1';
    }();
    if (screenStroke && ScreenStroke::supports(g) && renderSettings_.litsphere >= 0 && !renderSettings_.pbr()) {
        renderer_.setScreenStroke(g);
        renderer_.setGhost(id, GhostStroke());
        return;
    }
    renderer_.setGhost(id, g);
}

// --measure-stroke with SF_MEASURE_VERIFY=1: the stroke just drawn must come out of the frame loop's own path the
// same, bit for bit, and its undo must give back the mesh it started from. Undoes it, checks the mesh, places the
// same dabs (the probe's screen points, the mouse's pressure) with the solver thread off, and compares.
uint64_t App::meshStateHash(const Mesh& mesh) {
    uint64_t h = 1469598103934665603ull;
    auto add = [&](const void* data, size_t bytes) {
        const auto* p = static_cast<const uint8_t*>(data);
        for (size_t i = 0; i < bytes; ++i) h = (h ^ p[i]) * 1099511628211ull;
    };
    add(mesh.positions.data(), mesh.positions.size() * sizeof(glm::vec3));
    add(mesh.normals.data(), mesh.normals.size() * sizeof(glm::vec3));
    return h;
}

static uint64_t positionsHash(const Mesh& mesh) {
    uint64_t h = 1469598103934665603ull;
    const auto* p = reinterpret_cast<const uint8_t*>(mesh.positions.data());
    for (size_t i = 0; i < mesh.positions.size() * sizeof(glm::vec3); ++i) h = (h ^ p[i]) * 1099511628211ull;
    return h;
}

bool App::verifyStroke(uint64_t before, const std::vector<glm::vec2>& dabs, const std::vector<float>& radii) {
    const uint64_t after = meshStateHash(doc_.mesh);
    const uint64_t afterPositions = positionsHash(doc_.mesh);
    const std::vector<glm::vec3> afterNormals = doc_.mesh.normals;
    runCommand("undo", {});
    const uint64_t undone = meshStateHash(doc_.mesh);
    const bool undoOk = undone == before;
    // The same dabs, placed here (as with predictive strokes off).
    const bool wasAsync = asyncStroke_;
    asyncStroke_ = false;
    haveLastDab_ = false;
    const auto place = [&] {
        doc_.syncBvh();
        haveLastDab_ = false;
        doc_.sculptor.draftStrokes = draftStrokes(); // (the measured stroke is one the solver thread takes)
        doc_.sculptor.beginStroke(doc_.mesh, brushLabel(strokeBrush_), doc_.activeLayer);
        const float radius = radiusPx_;
        for (size_t i = 0; i < dabs.size(); ++i) {
            if (i < radii.size()) radiusPx_ = radii[i]; // (the size each dab had: it may change mid-stroke)
            dabAt(dabs[i].x, dabs[i].y, 1.0f);
        }
        radiusPx_ = radius;
        doc_.sculptor.flushDraft(doc_.mesh, doc_.bvh);
        doc_.undo.push(doc_.sculptor.endStroke());
        return meshStateHash(doc_.mesh);
    };
    const uint64_t replay = place();
    const bool samePositions = positionsHash(doc_.mesh) == afterPositions;
    size_t normalDiffs = 0;
    float maxNormalDiff = 0.0f;
    for (size_t i = 0; i < afterNormals.size() && i < doc_.mesh.normals.size(); ++i)
        if (afterNormals[i] != doc_.mesh.normals[i]) {
            ++normalDiffs;
            maxNormalDiff = std::max(maxNormalDiff, glm::length(afterNormals[i] - doc_.mesh.normals[i]));
        }
    std::printf("verify: positions %s; %zu normals differ (largest %.3g)\n", samePositions ? "identical" : "DIFFERENT",
                normalDiffs, double(maxNormalDiff));
    // And once more: the frame-loop path against itself (anything here that isn't deterministic shows).
    runCommand("undo", {});
    const uint64_t again = place();
    asyncStroke_ = wasAsync;
    const bool same = replay == after;
    std::printf("verify: undo %s; %zu dabs replayed without the solver thread: %s (replayed twice: %s)\n",
                undoOk ? "restores the mesh" : "DIFFERS", dabs.size(), same ? "identical" : "DIFFERENT",
                again == replay ? "identical" : "DIFFERENT");
    std::fflush(stdout);
    return undoOk && same;
}

} // namespace sf

