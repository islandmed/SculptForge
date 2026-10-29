// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Geometry > QRemesh and SubTool > Project: ZBrush's ZRemesher and ProjectAll. QRemesh (core/QRemesh.h) runs
// on a worker thread with a progress bar and Cancel, then replaces the SubTool as one undo step (optionally
// keeping the original as a hidden SubTool below it). ProjectAll (core/Projection.h) is quick, so it runs
// as the project_all command. The console shows both as script lines. The QRemesh Guides brush (ZBrush's
// ZRemesher Guides, core/QRemeshGuides.h) draws the curves QRemesh follows.
#include "app/App.h"
#include "app/Demo.h"
#include "app/Palettes.h"
#include "app/Ui.h"

#include "core/TextureTransfer.h"
#include "core/Voxel.h"

#include <IconsLucide.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <thread>

namespace sf {

namespace {

constexpr const char* kQRemeshPath = "Tool > Geometry > QRemesh";
constexpr const char* kProjectPath = "Tool > SubTool > Project";

// Faces as a modeller counts them: quads once, plus loose triangles.
size_t faceCount(const Mesh& m) { return m.triangleCount() - m.quadCount(); }

int symmetryAxis(const Symmetry& s) { return s.y && !s.x ? 1 : s.z && !s.x && !s.y ? 2 : 0; }

bool ctrlHeld(SDL_Keymod mod) { return (mod & (SDL_KMOD_CTRL | SDL_KMOD_GUI)) != 0; }

// Distance from p to the segment ab, on screen.
float segmentDistance(ImVec2 p, ImVec2 a, ImVec2 b) {
    const float dx = b.x - a.x, dy = b.y - a.y, l2 = dx * dx + dy * dy;
    const float t = l2 > 0.0f ? std::clamp(((p.x - a.x) * dx + (p.y - a.y) * dy) / l2, 0.0f, 1.0f) : 0.0f;
    return std::hypot(a.x + dx * t - p.x, a.y + dy * t - p.y);
}

} // namespace

void App::registerRetopoUiActions() {
    auto add = [this](const char* id, const char* label, const char* palette, std::function<void()> run,
                      std::function<bool()> enabled, const char* description, const char* command) {
        Action a;
        a.id = id;
        a.label = label;
        a.palette = palette;
        a.run = std::move(run);
        a.enabled = std::move(enabled);
        a.description = description;
        a.command = command;
        actions_.add(std::move(a));
    };
    add("geometry.qremesh", "QRemesh", kQRemeshPath, [this] { startQRemesh(); },
        [this] { return !qremesh_ && doc_.mesh.triangleCount() > 0; },
        "Automatic quad retopology, like ZBrush's ZRemesher: rebuilds the SubTool as clean, all-quad topology whose "
        "edge loops follow the forms, close to Target Polygons. Runs in the background (Cancel stops it); undoable. "
        "Then divide it and use SubTool > Project > ProjectAll to bring the detail back.",
        "qremesh");
    {
        Action a;
        a.id = "brush.qremesh_guides";
        a.label = "QRemesh Guides";
        a.icon = ICON_LC_SPLINE;
        a.palette = kQRemeshPath;
        a.run = [this] { setGuidesMode(!guidesMode_); };
        a.checked = [this] { return guidesMode_; };
        a.description = "The QRemesh Guides brush (ZBrush's ZRemesher Guides): drag on the model to draw a curve for "
                        "QRemesh's edge loops to follow, around an eye, the mouth or a joint. A curve that ends where "
                        "it started becomes a loop. Symmetry mirrors it; Alt+click a curve to delete it. Pick any "
                        "brush to sculpt again.";
        actions_.add(std::move(a));
    }
    add("geometry.qremesh_guides_delete_last", "Delete Last", kQRemeshPath,
        [this] { runCommand("qremesh_guide_delete_last"); }, [this] { return !doc_.qremeshGuides.empty(); },
        "Delete the last QRemesh guide curve drawn (with its mirror copies).", "qremesh_guide_delete_last");
    add("geometry.qremesh_guides_clear", "Clear", kQRemeshPath, [this] { runCommand("qremesh_guides_clear"); },
        [this] { return !doc_.qremeshGuides.empty(); }, "Delete every QRemesh guide curve of this SubTool.",
        "qremesh_guides_clear");
    add("subtool.project_all", "ProjectAll", kProjectPath, [this] {
            runCommand("project_all", {{"distance", double(retopo_.projectDist)}, {"mean", retopo_.projectMean},
                                       {"blur", double(retopo_.projectBlur)}, {"geometry", retopo_.projectGeometry},
                                       {"polypaint", retopo_.projectPolypaint}});
        },
        [this] {
            for (int i = 0; i < doc_.subtoolCount(); ++i)
                if (i != doc_.activeSubtool && doc_.subtools[size_t(i)].visible) return true;
            return false;
        },
        "Project the shape and polypaint of every other visible SubTool onto this one: each vertex moves along its "
        "normal to the nearest surface within Dist. The mask holds vertices back. Typical use: QRemesh a copy, divide "
        "it a few times, show the original, ProjectAll.",
        "project_all");
}

void App::startQRemesh() {
    if (qremesh_ || doc_.mesh.triangleCount() == 0) return;
    if (doc_.sculptor.strokeActive()) endStroke();
    QRemeshOptions o;
    o.targetQuads = std::clamp(retopo_.target, 16, 2'000'000);
    o.adaptive = retopo_.adaptive;
    o.keepGroups = retopo_.keepGroups;
    o.detectEdges = retopo_.detectEdges;
    o.usePolypaint = retopo_.usePolypaint;
    o.symmetryAxis = retopo_.symmetry ? symmetryAxis(symmetry_) : -1;
    o.guides = doc_.qremeshGuides;
    o.guideStrength = std::clamp(curvesStrength_, 0.0f, 100.0f) / 100.0f;

    auto job = std::make_unique<QRemeshJob>();
    job->progress = std::make_shared<std::atomic<float>>(0.0f);
    job->cancel = std::make_shared<std::atomic<bool>>(false);
    job->result = std::make_shared<Mesh>();
    job->stats = std::make_shared<QRemeshStats>();
    job->error = std::make_shared<std::string>();
    job->subtool = doc_.activeSubtoolInfo().id;
    job->triangles = doc_.mesh.triangleCount();
    job->keepOriginal = retopo_.keepOriginal;
    job->args = {{"target", double(o.targetQuads)}, {"adaptive", double(o.adaptive)}};
    if (o.keepGroups) job->args["keep_groups"] = true;
    if (!o.detectEdges) job->args["detect_edges"] = false;
    if (o.usePolypaint) job->args["use_polypaint"] = true;
    if (o.symmetryAxis >= 0) job->args["symmetry"] = std::string(1, char('x' + o.symmetryAxis));
    if (job->keepOriginal) job->args["keep_original"] = true;
    if (!o.guides.empty() && std::abs(curvesStrength_ - 50.0f) > 0.01f) job->args["curves_strength"] = double(curvesStrength_);
    const bool keepTexture = keepTexture_;
    if (!keepTexture && hasTexture(doc_.mesh)) job->args["keep_texture"] = false;
    job->textureNote = std::make_shared<std::string>();

    std::promise<bool> promise;
    job->done = promise.get_future();
    std::thread([promise = std::move(promise), source = std::make_shared<Mesh>(doc_.mesh), o, progress = job->progress,
                 cancel = job->cancel, result = job->result, stats = job->stats, error = job->error,
                 note = job->textureNote, keepTexture]() mutable {
        // With a texture to keep, QRemesh takes the first 80% of the bar and Auto UV + transfer the rest.
        const float share = hasTexture(*source) && keepTexture ? 0.8f : 1.0f;
        bool ok = qremesh(*source, o, *result, *error, [&](float f) {
            progress->store(f * share);
            return !cancel->load();
        }, stats.get());
        if (ok) *note = carryTextureNote(*source, *result, keepTexture, [&](float f) {
            progress->store(share + (1.0f - share) * f);
            return !cancel->load();
        });
        if (ok && cancel->load()) {
            ok = false;
            *error = "QRemesh cancelled";
        }
        promise.set_value(ok);
    }).detach();
    qremesh_ = std::move(job);
    status_ = "QRemesh: " + std::to_string(faceCount(doc_.mesh)) + " polygons to about " + std::to_string(o.targetQuads) +
              " quads...";
}

void App::startVoxelRemesh() {
    if (voxelRemesh_) return;
    Args args{{"resolution", double(voxelResolution_)}};
    if (!hasTexture(doc_.mesh) || !keepTexture_) {
        if (hasTexture(doc_.mesh)) args["keep_texture"] = false; // Keep Texture off
        runCommand("remesh", args);
        return;
    }
    // Keeping a texture adds Auto UV of the result, which takes seconds to minutes: run it all in the background.
    if (doc_.sculptor.strokeActive()) endStroke();
    auto job = std::make_unique<VoxelRemeshJob>();
    job->progress = std::make_shared<std::atomic<float>>(0.0f);
    job->cancel = std::make_shared<std::atomic<bool>>(false);
    job->result = std::make_shared<Mesh>();
    job->error = std::make_shared<std::string>();
    job->note = std::make_shared<std::string>();
    job->subtool = doc_.activeSubtoolInfo().id;
    job->triangles = doc_.mesh.triangleCount();
    job->resolution = std::clamp(voxelResolution_, 32, 512);
    std::promise<bool> promise;
    job->done = promise.get_future();
    std::thread([promise = std::move(promise), source = std::make_shared<Mesh>(doc_.mesh), res = job->resolution,
                 progress = job->progress, cancel = job->cancel, result = job->result, error = job->error,
                 note = job->note]() mutable {
        progress->store(0.02f);
        *result = voxelRemesh(*source, res, error.get());
        bool ok = result->triangleCount() > 0;
        progress->store(0.15f);
        if (ok) *note = carryTextureNote(*source, *result, true, [&](float f) {
            progress->store(0.15f + 0.85f * f);
            return !cancel->load();
        });
        if (ok && cancel->load()) {
            ok = false;
            *error = "Voxel Remesh cancelled";
        }
        promise.set_value(ok);
    }).detach();
    voxelRemesh_ = std::move(job);
    status_ = "Voxel Remesh: remeshing and keeping the texture...";
}

void App::pollVoxelRemesh() {
    if (!voxelRemesh_) return;
    if (voxelRemesh_->done.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
        status_ = "Voxel Remesh (keeping the texture): " + std::to_string(int(voxelRemesh_->progress->load() * 100.0f)) + "%";
        return;
    }
    const std::unique_ptr<VoxelRemeshJob> job = std::move(voxelRemesh_);
    if (!job->done.get()) {
        status_ = *job->error;
        return;
    }
    int index = -1;
    for (int i = 0; i < doc_.subtoolCount(); ++i)
        if (doc_.subtools[size_t(i)].id == job->subtool) index = i;
    if (index < 0 || doc_.subtoolMesh(index).triangleCount() != job->triangles) {
        status_ = "Voxel Remesh skipped: the SubTool changed while it was being remeshed";
        return;
    }
    if (index != doc_.activeSubtool) doc_.selectSubtool(index);
    if (doc_.sculptor.strokeActive()) endStroke();
    doc_.replaceMeshUndoable(std::move(*job->result), "Remesh"); // as the remesh command
    CommandResult r{true, "Remeshed to " + std::to_string(doc_.mesh.triangleCount()) + " triangles" + *job->note};
    if (const std::string notice = doc_.takeNotice(); !notice.empty()) r.message += ". " + notice; // levels dropped
    status_ = r.message;
    lastCommandOk_ = true;
    clearLods();
    playingLayer_ = -1;
    uvLinesKey_ = 0;
    updateTitle();
    consoleEcho("remesh", {{"resolution", double(job->resolution)}}, r);
}

void App::pollQRemesh() {
    pollVoxelRemesh(); // polled with QRemesh, its sibling rebuild job
    if (!qremesh_) return;
    if (qremesh_->done.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
        status_ = "QRemesh: " + std::to_string(int(qremesh_->progress->load() * 100.0f)) + "%";
        return;
    }
    const std::unique_ptr<QRemeshJob> job = std::move(qremesh_);
    if (!job->done.get()) {
        status_ = *job->error;
        return;
    }
    // The SubTool may be inactive now, or changed topology meanwhile.
    int index = -1;
    for (int i = 0; i < doc_.subtoolCount(); ++i)
        if (doc_.subtools[size_t(i)].id == job->subtool) index = i;
    if (index < 0 || doc_.subtoolMesh(index).triangleCount() != job->triangles) {
        status_ = "QRemesh skipped: the SubTool changed while it was being remeshed";
        return;
    }
    if (index != doc_.activeSubtool) doc_.selectSubtool(index);
    if (doc_.sculptor.strokeActive()) endStroke();
    CommandResult r{true, applyQRemesh(doc_, std::move(*job->result), job->keepOriginal, *job->stats) + *job->textureNote};
    if (const std::string notice = doc_.takeNotice(); !notice.empty()) r.message += ". " + notice; // levels dropped
    status_ = r.message;
    lastCommandOk_ = true;
    clearLods();
    playingLayer_ = -1;
    updateTitle();
    consoleEcho("qremesh", job->args, r);
}

void App::drawQRemeshContents() {
    const ImGuiStyle& style = ImGui::GetStyle();
    const float x0 = ImGui::GetCursorPosX(); // line start, inside the section indent
    tray::GroupLabel(*this, "QRemesh");
    const float full = ImGui::GetContentRegionAvail().x;
    const float half = (full - style.ItemSpacing.x) * 0.5f;
    const float third = (full - 2.0f * style.ItemSpacing.x) / 3.0f;

    if (qremesh_) {
        ImGui::ProgressBar(qremesh_->progress->load(), ImVec2(half, 0), "Remeshing...");
        ui::HelpText("QRemesh", nullptr, "Remeshing in the background. The result replaces the SubTool when it's done.",
                     kQRemeshPath);
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(-1, 0))) qremesh_->cancel->store(true);
        ui::HelpText("Cancel", nullptr, "Stop QRemesh; the SubTool stays as it is.", kQRemeshPath);
    } else {
        ui::ActionButton(*this, "geometry.qremesh", ImVec2(half, 0));
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%zu polygons", faceCount(doc_.mesh));
    }

    ImGui::SetNextItemWidth(-1);
    tray::SliderInt("##qtarget", &retopo_.target, 100, 200000, "Target Polygons %d", ImGuiSliderFlags_Logarithmic);
    ui::HelpText("Target Polygons", nullptr,
                 "About how many quads QRemesh makes (Ctrl+click to type a number). Half / Same / Double set it from "
                 "the SubTool's current polygon count.",
                 kQRemeshPath);
    const size_t faces = faceCount(doc_.mesh);
    auto setTarget = [&](double k) { retopo_.target = int(std::clamp(double(faces) * k, 100.0, 2'000'000.0)); };
    if (ImGui::Button("Half", ImVec2(third, 0))) setTarget(0.5);
    ui::HelpText("Half", nullptr, "Target half the SubTool's current polygon count.", kQRemeshPath);
    ImGui::SameLine();
    if (ImGui::Button("Same", ImVec2(third, 0))) setTarget(1.0);
    ui::HelpText("Same", nullptr, "Target the SubTool's current polygon count (to clean up topology without changing its density).",
                 kQRemeshPath);
    ImGui::SameLine();
    if (ImGui::Button("Double", ImVec2(-1, 0))) setTarget(2.0);
    ui::HelpText("Double", nullptr, "Target twice the SubTool's current polygon count.", kQRemeshPath);

    ImGui::SetNextItemWidth(-1);
    ui::SliderInside("##qadaptive", &retopo_.adaptive, 0.0f, 1.0f, "Adaptive Size %.2f");
    ui::HelpText("Adaptive Size", nullptr,
                 "0: quads all about the same size. Higher: smaller quads where the surface curves, bigger ones on "
                 "flat areas (the count stays near the target).",
                 kQRemeshPath);

    ImGui::Checkbox("Keep Groups", &retopo_.keepGroups);
    ui::HelpText("Keep Groups", nullptr,
                 "Edge loops follow the polygroup borders exactly and the result keeps the polygroups. Paint groups "
                 "where you want loops (around the eyes, the mouth) to guide the flow.",
                 kQRemeshPath);
    ImGui::SameLine();
    ImGui::SetCursorPosX(x0 + half + style.ItemSpacing.x); // the right column, inside the section indent
    ImGui::Checkbox("Detect Edges", &retopo_.detectEdges);
    ui::HelpText("Detect Edges", nullptr, "Hard edges (faces meeting at more than 50 degrees) get an edge loop, so they stay sharp.",
                 kQRemeshPath);
    ImGui::Checkbox("Use Polypaint", &retopo_.usePolypaint);
    ui::HelpText("Use Polypaint", nullptr,
                 "Paint where you want density: red areas get up to 4 times the quads, blue areas down to a quarter, "
                 "white is unchanged.",
                 kQRemeshPath);
    ImGui::SameLine();
    ImGui::SetCursorPosX(x0 + half + style.ItemSpacing.x); // the right column, inside the section indent
    ImGui::Checkbox("Symmetry", &retopo_.symmetry);
    const char axis = char('X' + symmetryAxis(symmetry_));
    const std::string symHelp = std::string("Mirror the topology across ") + axis +
                                " (the Transform palette's symmetry axis), welded on the seam. The mirror plane is the "
                                "model's own, so a model away from the world centre works too; a model that isn't "
                                "mirror-symmetric is remeshed without symmetry (the status line says so).";
    ui::HelpText("Symmetry", nullptr, symHelp.c_str(), kQRemeshPath);
    ImGui::Checkbox("Keep Original", &retopo_.keepOriginal);
    ui::HelpText("Keep Original", nullptr,
                 "Keep the SubTool as it was, hidden, right below the new one (\"<name> Original\"), ready to "
                 "ProjectAll its detail back. Off: replace it, as ZBrush does (undo brings it back).",
                 kQRemeshPath);
    if (hasTexture(doc_.mesh)) { // only matters for a painted SubTool
        ImGui::SameLine();
        ImGui::SetCursorPosX(x0 + half + style.ItemSpacing.x); // the right column, inside the section indent
        drawKeepTextureCheckbox("Keep Texture##qremesh", kQRemeshPath);
    }
    drawQRemeshGuideControls();
}

// ---------------------------------------------------------------------------
// QRemesh Guides
// ---------------------------------------------------------------------------

void App::drawQRemeshGuideControls() {
    const ImGuiStyle& style = ImGui::GetStyle();
    const float full = ImGui::GetContentRegionAvail().x;
    const float half = (full - style.ItemSpacing.x) * 0.5f;
    tray::GroupLabel(*this, "Guides");
    ui::ActionButton(*this, "brush.qremesh_guides", ImVec2(half, 0));
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    size_t loops = 0;
    for (const QRemeshGuide& g : doc_.qremeshGuides) loops += g.closed;
    if (doc_.qremeshGuides.empty()) ImGui::TextDisabled("no curves");
    else ImGui::TextDisabled("%zu curves (%zu loops)", doc_.qremeshGuides.size(), loops);
    ui::ActionButton(*this, "geometry.qremesh_guides_delete_last", ImVec2(half, 0));
    ImGui::SameLine();
    ui::ActionButton(*this, "geometry.qremesh_guides_clear", ImVec2(-1, 0));
    ImGui::SetNextItemWidth(-1);
    ui::SliderInside("##qcurves", &curvesStrength_, 0.0f, 100.0f, "Curves Strength %.0f");
    ui::HelpText("Curves Strength", nullptr,
                 "How far around each guide curve the edge flow follows it: 0 = only the edge loop on the curve, 100 = "
                 "about six quads to each side (ZBrush's Curves Strength). The loop on the curve is always made.",
                 kQRemeshPath);
}

void App::setGuidesMode(bool on) {
    if (drag_ == Drag::Guide) return;
    guidesMode_ = on;
    guidePath_.clear();
    if (on) {
        guidesFrom_ = {brush_, doc_.activeBrush, paintMode_};
        if (gizmoActive()) setGizmoMode(GizmoMode::Off);
        status_ = "QRemesh Guides: drag on the model to draw a curve for QRemesh to follow (end where you started for a "
                  "loop); Alt+click a curve deletes it";
    } else {
        status_ = "Draw";
    }
}

void App::syncGuidesMode() {
    // Picking a brush (picker, shelf, keys, LightBox), Paint or the Gizmo leaves the guides brush.
    if (guidesMode_ && drag_ != Drag::Guide &&
        (brush_ != guidesFrom_.brush || doc_.activeBrush != guidesFrom_.recipe || paintMode_ != guidesFrom_.paint ||
         gizmoActive()))
        setGuidesMode(false);
}

int App::guideAt(float x, float y) const {
    int best = -1;
    float bestD = 10.0f * uiScale_;
    for (size_t g = 0; g < doc_.qremeshGuides.size(); ++g) {
        const QRemeshGuide& guide = doc_.qremeshGuides[g];
        const size_t n = guide.points.size();
        ImVec2 prev;
        bool havePrev = false;
        for (size_t i = 0; i <= n; ++i) {
            if (i == n && !guide.closed) break;
            ImVec2 p;
            if (!gizmoProject(guide.points[i % n], p)) {
                havePrev = false;
                continue;
            }
            if (havePrev) {
                const float d = segmentDistance(ImVec2(x, y), prev, p);
                if (d < bestD) bestD = d, best = int(g);
            }
            prev = p;
            havePrev = true;
        }
    }
    return best;
}

bool App::guideMouseDown(float x, float y, SDL_Keymod mod) {
    if (!guidesMode_ || ctrlHeld(mod) || (mod & SDL_KMOD_SHIFT)) return false; // Ctrl masks, Shift smooths as usual
    if (mod & SDL_KMOD_ALT) { // delete the curve under the cursor (and its mirror copies)
        const int g = guideAt(x, y);
        if (g < 0) return false;
        const uint32_t stroke = doc_.qremeshGuides[size_t(g)].stroke;
        auto& guides = doc_.qremeshGuides;
        guides.erase(std::remove_if(guides.begin(), guides.end(), [&](const QRemeshGuide& q) { return q.stroke == stroke; }),
                     guides.end());
        doc_.modified = true;
        status_ = "QRemesh guide deleted; " + std::to_string(guides.size()) + " left";
        return true;
    }
    RayHit hit;
    if (previewLod_ >= 0 || !pick(x, y, hit)) return false; // the background navigates
    guidePath_ = {hit.position};
    guidePathLast_[0] = x;
    guidePathLast_[1] = y;
    drag_ = Drag::Guide;
    return true;
}

void App::guideDragTo(float x, float y) {
    if (std::hypot(x - guidePathLast_[0], y - guidePathLast_[1]) < 2.0f * uiScale_) return;
    RayHit hit;
    if (!pick(x, y, hit)) return; // off the model: the curve continues where it comes back
    guidePath_.push_back(hit.position);
    guidePathLast_[0] = x;
    guidePathLast_[1] = y;
}

void App::guideRelease() {
    std::vector<glm::vec3> path = std::move(guidePath_);
    guidePath_.clear();
    doc_.syncBvh();
    const int added = path.size() >= 2 ? addQRemeshGuide(doc_.qremeshGuides, doc_.mesh, doc_.bvh, path, symmetry_) : 0;
    if (added == 0) {
        status_ = "QRemesh Guides: the curve is too short (drag across the model)";
        return;
    }
    doc_.modified = true;
    const QRemeshGuide& g = doc_.qremeshGuides[doc_.qremeshGuides.size() - size_t(added)];
    status_ = std::string("QRemesh guide added (") + (g.closed ? "a loop" : "a curve") + (added > 1 ? ", mirrored" : "") +
              "); " + std::to_string(doc_.qremeshGuides.size()) + " curves. Run QRemesh to use them";
}

void App::drawQRemeshGuides() {
    syncGuidesMode();
    const std::vector<QRemeshGuide>& guides = doc_.qremeshGuides;
    if ((guides.empty() && guidePath_.empty()) || !doc_.activeSubtoolInfo().visible) return;
    ImDrawList* dl = ImGui::GetBackgroundDrawList(); // under the panels, over the model
    const CanvasRect cr = canvasRect();
    dl->PushClipRect(ImVec2(cr.x, cr.y), ImVec2(cr.x + cr.w, cr.y + cr.h), true);
    const float s = uiScale_;

    // Which points the model hides: a ray from the eye to each one (refreshed when the view or the guides change;
    // at most ~2000 rays a frame, the points between them follow their neighbours).
    {
        const glm::mat4 vp = camera_.projection() * camera_.view();
        uint64_t key = 1469598103934665603ull;
        auto mix = [&](const void* data, size_t bytes) {
            const auto* b = static_cast<const unsigned char*>(data);
            for (size_t i = 0; i < bytes; ++i) key = (key ^ b[i]) * 1099511628211ull;
        };
        mix(&vp, sizeof vp);
        mix(&cr, sizeof cr);
        const uint64_t rev = doc_.undo.revision(), sub = doc_.activeSubtoolInfo().id;
        mix(&rev, sizeof rev);
        mix(&sub, sizeof sub);
        size_t points = 0;
        for (const QRemeshGuide& g : guides) {
            points += g.points.size();
            if (!g.points.empty()) mix(&g.points.back(), sizeof(glm::vec3));
        }
        mix(&points, sizeof points);
        if (key != guideVisibleKey_ || guideVisible_.size() != guides.size()) {
            guideVisibleKey_ = key;
            guideVisible_.assign(guides.size(), {});
            doc_.syncBvh();
            const float eps = 0.01f * glm::length(doc_.mesh.bounds().extent());
            const size_t stride = std::max<size_t>(1, points / 2000);
            for (size_t g = 0; g < guides.size(); ++g) {
                const auto& pts = guides[g].points;
                auto& vis = guideVisible_[g];
                vis.assign(pts.size(), 1);
                for (size_t i = 0; i < pts.size(); i += stride) {
                    ImVec2 sp;
                    if (!gizmoProject(pts[i], sp)) {
                        vis[i] = 0;
                        continue;
                    }
                    glm::vec3 origin, dir;
                    canvasRay(sp.x, sp.y, origin, dir);
                    const float t = glm::dot(pts[i] - origin, dir) / std::max(glm::dot(dir, dir), 1e-20f);
                    RayHit hit;
                    vis[i] = !doc_.bvh.raycast(doc_.mesh, origin, dir, hit) || hit.t > t - eps / std::sqrt(glm::dot(dir, dir));
                }
                for (size_t i = 0; i < pts.size(); ++i)
                    if (i % stride) vis[i] = vis[i - i % stride];
            }
        }
    }

    const ImVec2 mouse(mouseX_, mouseY_);
    const bool altHover = guidesMode_ && drag_ == Drag::None && (inputMods() & SDL_KMOD_ALT) &&
                          !ImGui::GetIO().WantCaptureMouse && cr.contains(mouse.x, mouse.y);
    const int doomed = altHover ? guideAt(mouse.x, mouse.y) : -1;
    const uint32_t doomedStroke = doomed >= 0 ? guides[size_t(doomed)].stroke : 0;
    for (size_t g = 0; g < guides.size(); ++g) {
        const QRemeshGuide& guide = guides[g];
        const size_t n = guide.points.size();
        const bool dead = doomed >= 0 && guide.stroke == doomedStroke;
        // Solid while the brush is picked; thin and faint otherwise, so the edge loop QRemesh put there shows.
        const ImU32 front = dead ? IM_COL32(255, 90, 80, 255) : IM_COL32(255, 196, 64, guidesMode_ ? 255 : 110);
        const ImU32 back = dead ? IM_COL32(255, 90, 80, 70) : IM_COL32(255, 196, 64, guidesMode_ ? 55 : 30);
        const float width = guidesMode_ ? 2.5f : 1.25f;
        ImVec2 prev;
        bool havePrev = false, prevVis = false;
        for (size_t i = 0; i <= n; ++i) {
            if (i == n && !guide.closed) break;
            ImVec2 p;
            if (!gizmoProject(guide.points[i % n], p)) {
                havePrev = false;
                continue;
            }
            const bool vis = guideVisible_[g][i % n] != 0;
            if (havePrev) {
                const bool both = vis && prevVis;
                dl->AddLine(prev, p, both ? front : back, (both ? width : 1.25f) * s);
            }
            prev = p;
            prevVis = vis;
            havePrev = true;
        }
    }
    // The curve being drawn, with its mirror images.
    if (drag_ == Drag::Guide && guidePath_.size() >= 2)
        for (const glm::vec3& m : Sculptor::mirrors(symmetry_)) {
            const bool own = m == glm::vec3(1.0f);
            for (size_t i = 1; i < guidePath_.size(); ++i) {
                ImVec2 a, b;
                if (gizmoProject(guidePath_[i - 1] * m, a) && gizmoProject(guidePath_[i] * m, b))
                    dl->AddLine(a, b, own ? IM_COL32(255, 230, 140, 255) : IM_COL32(255, 230, 140, 120), (own ? 2.5f : 1.5f) * s);
            }
        }
    // The guides brush's cursor: a small ring with a dot.
    if (guidesMode_ && !ImGui::GetIO().WantCaptureMouse && cr.contains(mouse.x, mouse.y)) {
        dl->AddCircle(mouse, 7.0f * s, IM_COL32(255, 196, 64, 230), 20, 1.5f * s);
        dl->AddCircleFilled(mouse, 1.8f * s, IM_COL32(255, 196, 64, 255));
    }
    dl->PopClipRect();
}

void App::drawProjectContents() {
    const ImGuiStyle& style = ImGui::GetStyle();
    const float x0 = ImGui::GetCursorPosX(); // line start, inside the section indent
    tray::GroupLabel(*this, "Project");
    const float half = (ImGui::GetContentRegionAvail().x - style.ItemSpacing.x) * 0.5f;
    ui::ActionButton(*this, "subtool.project_all", ImVec2(half, 0));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1);
    ui::SliderInside("##pdist", &retopo_.projectDist, 0.001f, 0.2f, "Dist %.3f", ImGuiSliderFlags_Logarithmic);
    ui::HelpText("Dist", nullptr,
                 "How far each vertex looks for the source surface, along its normal, as a fraction of the SubTool's "
                 "size. Raise it if parts don't pick up the detail; lower it where surfaces are close together.",
                 kProjectPath);
    ImGui::SetNextItemWidth(half);
    tray::SliderInt("##pblur", &retopo_.projectBlur, 0, 10, "PA Blur %d");
    ui::HelpText("PA Blur", nullptr, "Smooth the projected result this many times (softens noise from the source).",
                 kProjectPath);
    ImGui::SameLine();
    ImGui::Checkbox("Mean", &retopo_.projectMean);
    ui::HelpText("Mean", nullptr,
                 "Where the source is found on both sides of a vertex, settle halfway between instead of on the nearest.",
                 kProjectPath);
    ImGui::Checkbox("Geometry", &retopo_.projectGeometry);
    ui::HelpText("Geometry", nullptr, "Project the shape (into the active sculpt layer).", kProjectPath);
    ImGui::SameLine();
    ImGui::SetCursorPosX(x0 + half + style.ItemSpacing.x); // the right column, inside the section indent
    ImGui::Checkbox("Polypaint", &retopo_.projectPolypaint);
    ui::HelpText("Polypaint", nullptr, "Project the polypaint (into the active paint layer) when a source has any.",
                 kProjectPath);
}

} // namespace sf
