// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Geometry > QRemesh and Part > Project: the classic QRemesh and ProjectAll. QRemesh (core/QRemesh.h) runs
// on a worker thread with a progress bar and Cancel, then replaces the Part as one undo step (optionally
// keeping the original as a hidden Part below it). ProjectAll (core/Projection.h) is quick, so it runs
// as the project_all command. The console shows both as script lines. The QRemesh Guides brush (the classic
// QRemesh Guides, core/QRemeshGuides.h) draws the curves QRemesh follows.
#include "app/App.h"
#include "app/Demo.h"
#include "app/Palettes.h"
#include "app/Ui.h"
#include "app/I18n.h"
#include "app/Theme.h"

#include "core/HardwareProfile.h"
#include "core/KeepMasked.h"
#include "core/MemoryGuard.h"
#include "core/QuickRemesh.h"
#include "core/TextureTransfer.h"
#include "core/Voxel.h"

#include <IconsLucide.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <thread>

namespace sf {

namespace {

constexpr const char* kQRemeshPath = "Tool > Geometry > Quad Remesh";
constexpr const char* kProjectPath = "Tool > Parts > Project";

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
    add("geometry.qremesh", "Quad Remesh", kQRemeshPath, [this] { startQRemesh(); },
        [this] { return !qremesh_ && doc_.mesh.triangleCount() > 0; },
        "Automatic quad retopology: rebuilds the Part as clean, all-quad topology whose "
        "edge loops follow the forms, close to Target Quads. Runs in the background (Cancel stops it); undoable. "
        "Then divide it and use Part > Project > Project Detail to bring the detail back.",
        "qremesh");
    {
        Action a;
        a.id = "brush.qremesh_guides";
        a.label = "Quad Remesh Guides";
        a.icon = ICON_LC_SPLINE;
        a.palette = kQRemeshPath;
        a.run = [this] { setGuidesMode(!guidesMode_); };
        a.checked = [this] { return guidesMode_; };
        a.description = "The Quad Remesh Guides brush: drag on the model to draw a curve for "
                        "Quad Remesh's edge loops to follow, around an eye, the mouth or a joint. A curve that ends where "
                        "it started becomes a loop. Symmetry mirrors it; Alt+click a curve to delete it. Pick any "
                        "brush to sculpt again.";
        actions_.add(std::move(a));
    }
    add("geometry.qremesh_guides_delete_last", "Delete Last", kQRemeshPath,
        [this] { runCommand("qremesh_guide_delete_last"); }, [this] { return !doc_.qremeshGuides.empty(); },
        "Delete the last Quad Remesh guide curve drawn (with its mirror copies).", "qremesh_guide_delete_last");
    add("geometry.qremesh_guides_clear", "Clear", kQRemeshPath, [this] { runCommand("qremesh_guides_clear"); },
        [this] { return !doc_.qremeshGuides.empty(); }, "Delete every Quad Remesh guide curve of this Part.",
        "qremesh_guides_clear");
    add("part.project_all", "Project Detail", kProjectPath, [this] {
            runCommand("project_all", {{"distance", double(retopo_.projectDist)}, {"mean", retopo_.projectMean},
                                       {"blur", double(retopo_.projectBlur)}, {"geometry", retopo_.projectGeometry},
                                       {"vertexpaint", retopo_.projectVertexPaint}});
        },
        [this] {
            for (int i = 0; i < doc_.partCount(); ++i)
                if (i != doc_.activePart && doc_.parts[size_t(i)].visible) return true;
            return false;
        },
        "Project the shape and vertex paint of every other visible Part onto this one: each vertex moves along its "
        "normal to the nearest surface within Dist. The mask holds vertices back. Typical use: Quad Remesh a copy, divide "
        "it a few times, show the original, Project Detail.",
        "project_all");
}

namespace {

// Keep Masked (#43): the triangles to keep, or empty when it's off or nothing is masked. `allMasked`: there's
// nothing left to remesh.
std::vector<uint8_t> keptForRemesh(const Mesh& mesh, bool keepMasked, bool& allMasked) {
    allMasked = false;
    std::vector<uint8_t> keep = keepMasked ? keepMaskedTriangles(mesh) : std::vector<uint8_t>();
    if (std::find(keep.begin(), keep.end(), 1) == keep.end()) return {};
    allMasked = std::find(keep.begin(), keep.end(), 0) == keep.end();
    return keep;
}

} // namespace

void App::startQRemesh() {
    if (qremesh_ || doc_.mesh.triangleCount() == 0) return;
    if (!unhideAllows("Quad Remesh", [this] { startQRemesh(); })) return; // #163
    if (!levelsAllow("Quad Remesh", [this] { startQRemesh(); })) return;  // #173
    if (doc_.sculptor.strokeActive()) endStroke();
    bool allMasked = false;
    std::vector<uint8_t> keep = keptForRemesh(doc_.mesh, retopo_.keepMasked, allMasked);
    if (allMasked) {
        status_ = "Quad Remesh: everything is masked, so nothing would change (Keep Masked)";
        return;
    }
    QRemeshOptions o;
    o.method = retopo_.method == 1 ? QRemeshMethod::Legacy : QRemeshMethod::V2;
    o.targetQuads = std::clamp(retopo_.target, 16, 2'000'000);
    o.adaptive = retopo_.adaptive;
    o.keepGroups = retopo_.keepGroups;
    o.detectEdges = retopo_.detectEdges;
    o.useVertexPaint = retopo_.useVertexPaint;
    o.symmetryAxis = retopo_.symmetry ? symmetryAxis(symmetry_) : -1;
    o.guides = doc_.qremeshGuides;
    o.guideStrength = std::clamp(curvesStrength_, 0.0f, 100.0f) / 100.0f;

    auto job = std::make_unique<QRemeshJob>();
    job->progress = std::make_shared<std::atomic<float>>(0.0f);
    job->cancel = std::make_shared<std::atomic<bool>>(false);
    job->result = std::make_shared<Mesh>();
    job->stats = std::make_shared<QRemeshStats>();
    job->error = std::make_shared<std::string>();
    job->part = doc_.activePartInfo().id;
    job->triangles = doc_.mesh.triangleCount();
    job->keepOriginal = retopo_.keepOriginal;
    job->args = {{"target", double(o.targetQuads)}, {"adaptive", double(o.adaptive)}};
    if (o.method == QRemeshMethod::Legacy) job->args["method"] = std::string("legacy");
    if (o.keepGroups) job->args["keep_groups"] = true;
    if (!o.detectEdges) job->args["detect_edges"] = false;
    if (o.useVertexPaint) job->args["use_vertexpaint"] = true;
    if (o.symmetryAxis >= 0) job->args["symmetry"] = std::string(1, char('x' + o.symmetryAxis));
    if (job->keepOriginal) job->args["keep_original"] = true;
    if (!keep.empty()) job->args["keep_masked"] = true;
    if (!o.guides.empty() && std::abs(curvesStrength_ - 50.0f) > 0.01f) job->args["curves_strength"] = double(curvesStrength_);
    const bool keepTexture = keepTexture_;
    if (!keepTexture && hasTexture(doc_.mesh)) job->args["keep_texture"] = false;
    job->textureNote = std::make_shared<std::string>();

    std::promise<bool> promise;
    job->done = promise.get_future();
    std::thread([promise = std::move(promise), source = std::make_shared<Mesh>(doc_.mesh), o, progress = job->progress,
                 cancel = job->cancel, result = job->result, stats = job->stats, error = job->error,
                 note = job->textureNote, keepTexture, keep = std::move(keep)]() mutable {
        // With a texture to keep, QRemesh takes the first 80% of the bar and Auto UV + transfer the rest.
        const float share = hasTexture(*source) && keepTexture ? 0.8f : 1.0f;
        // Keep Masked: the unmasked part is remeshed on its own, then the masked part is joined back to it.
        bool ok = qremesh(keep.empty() ? *source : unmaskedPart(*source, keep), o, *result, *error, [&](float f) {
            progress->store(f * share);
            return !cancel->load();
        }, stats.get());
        if (ok && !keep.empty()) {
            Mesh stitched;
            ok = stitchKeptTriangles(*source, keep, *result, stitched, *error);
            if (ok) *result = std::move(stitched);
            else *error = "Quad Remesh (Keep Masked): " + *error;
        }
        if (ok) *note = carryTextureNote(*source, *result, keepTexture, [&](float f) {
            progress->store(share + (1.0f - share) * f);
            return !cancel->load();
        });
        if (ok && cancel->load()) {
            ok = false;
            *error = "Quad Remesh cancelled";
        }
        promise.set_value(ok);
    }).detach();
    qremesh_ = std::move(job);
    status_ = "Quad Remesh: " + std::to_string(faceCount(doc_.mesh)) + " polygons to about " + std::to_string(o.targetQuads) +
              " quads...";
}

const VoxelRemeshEstimate& App::voxelEstimate() {
    VoxelEstimateCache& c = voxelEstimate_;
    const uint32_t part = doc_.activePartInfo().id;
    const uint64_t revision = doc_.undo.revision();
    const size_t triangles = doc_.mesh.triangleCount();
    // The area once per change of the Part (not per dab while a dynamic-topology stroke adds triangles).
    const bool stale = c.part != part || c.revision != revision || c.triangles != triangles;
    if (stale && !(doc_.sculptor.strokeActive() && c.part == part)) {
        c.part = part, c.revision = revision, c.triangles = triangles;
        c.area = surfaceArea(doc_.mesh);
        c.resolution = 0;
    }
    if (c.resolution != voxelResolution_) {
        c.resolution = voxelResolution_;
        c.estimate = estimateVoxelRemesh(doc_.mesh, voxelResolution_, c.area);
    }
    return c.estimate;
}

void App::drawVoxelResolution(bool compact) {
    voxelResolution_ = std::clamp(voxelResolution_, kVoxelResolutionMin, kVoxelResolutionMax);
    // Two thirds of the travel for 32-512 (the everyday range keeps its precision), a sixth each for 528-2048 and
    // 2112-4096. Ctrl+click types a number.
    if (!compact) ImGui::SetNextItemWidth(-1);
    // Customize Shelves, Ctrl+Alt: the slider is drawn inert and a button over it takes the press, so the slider can be
    // dragged onto a shelf (widget.voxel_resolution) instead of moving its value or the palette under it.
    const bool grab = !compact && layout_.customize && ui::CtrlHeld() && ui::AltHeld();
    if (grab) ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
    float t = voxelSliderFromResolution(voxelResolution_);
    std::string label = tr("Resolution %d");
    if (const size_t at = label.find("%d"); at != std::string::npos) label.replace(at, 2, std::to_string(voxelResolution_));
    const int before = voxelResolution_;
    if (ui::SliderInside("##voxres", &t, 0.0f, 1.0f, label.c_str(), ImGuiSliderFlags_NoInput)) voxelResolution_ = voxelResolutionFromSlider(t);
    if (grab) {
        ImGui::PopItemFlag();
        const ImVec2 lo = ImGui::GetItemRectMin(), hi = ImGui::GetItemRectMax();
        ImGui::SetCursorScreenPos(lo);
        ImGui::InvisibleButton("##voxresgrab", ImVec2(std::max(hi.x - lo.x, 1.0f), std::max(hi.y - lo.y, 1.0f)));
        // The press is taken by hand (as Shell.cpp's grab): over the inert slider it would otherwise fall through to the
        // palette's window move, and the drag source (which needs an active item) never began.
        ImGuiContext& g = *ImGui::GetCurrentContext();
        if (ImGui::IsMouseClicked(0) && ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(lo, hi) && g.ActiveId == 0) {
            ImGui::SetActiveID(ImGui::GetItemID(), ImGui::GetCurrentWindow());
            g.ActiveIdMouseButton = 0;
        }
        ui::UiItemDragSource(*this, "widget.voxel_resolution", "Voxel Resolution");
    } else if (ImGui::IsItemHovered() && ui::CtrlHeld() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        voxelResolution_ = before; // (the click doesn't move it)
        ImGui::ClearActiveID();
        voxelResolutionTyped_ = before;
        ImGui::OpenPopup("##voxrestype");
    }
    ui::HelpText("Resolution", nullptr,
                 "Voxels across the model, 32 to 4096: higher keeps more detail and makes more triangles (about 4x "
                 "for each doubling). The line under it estimates the result. Above 512 a remesh can make tens of "
                 "millions of triangles and take minutes; it asks once first, and won't start if it can't fit in "
                 "the free memory. Ctrl+click to type a value.",
                 "Tool > Remesh");
    if (ImGui::BeginPopup("##voxrestype")) {
        ImGui::SetNextItemWidth(120.0f * uiScale_);
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        ImGui::InputInt(tr("Resolution##voxrestype"), &voxelResolutionTyped_, 0, 0);
        if (ImGui::IsItemDeactivatedAfterEdit() || ImGui::IsKeyPressed(ImGuiKey_Enter)) {
            voxelResolution_ = std::clamp(voxelResolutionTyped_, kVoxelResolutionMin, kVoxelResolutionMax);
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    if (voxelResolution_ <= kVoxelDenseMax) voxelHighResOk_ = false; // (asks again on the next trip above 512)
    if (compact || doc_.mesh.triangleCount() == 0) return;
    const VoxelRemeshEstimate& e = voxelEstimate();
    const std::string tris = formatTriangles(e.triangles);
    char line[256];
    bool fits = true;
    if (voxelResolution_ <= kVoxelDenseMax) {
        std::snprintf(line, sizeof line, tr("About %s triangles"), tris.c_str());
    } else {
        const MemoryCheck c = checkMemory(e.bytes);
        fits = c.enough;
        if (fits) std::snprintf(line, sizeof line, tr("About %s triangles, ~%s of memory"), tris.c_str(), memoryText(e.bytes).c_str());
        else
            std::snprintf(line, sizeof line, tr("About %s triangles, needs ~%s, %s free"), tris.c_str(), memoryText(e.bytes).c_str(),
                          memoryText(c.availableBytes).c_str());
    }
    if (smallFont()) ImGui::PushFont(smallFont());
    ImGui::TextColored(fits ? theme::textDim : theme::danger, "%s", line);
    if (smallFont()) ImGui::PopFont();
    ui::HelpText("Estimate", nullptr,
                 "About how many triangles Voxel Remesh makes at this Resolution, from the Part's surface area (fine "
                 "detail smaller than a voxel counts, so a dense scan comes out lower). Above 512 also the memory the "
                 "remesh needs; in red when that is more than is free now.",
                 "Tool > Remesh");
}

void App::drawVoxelHighResConfirm() {
    if (!voxelHighResConfirm_.open) return;
    const float s = uiScale_;
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + vp->WorkSize.y * 0.5f),
                            ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    bool open = true;
    if (!ImGui::Begin(tr("High resolution remesh"), &open,
                      ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::End();
        if (!open) voxelHighResConfirm_ = {};
        return;
    }
    const VoxelRemeshEstimate& e = voxelEstimate();
    const MemoryCheck c = checkMemory(e.bytes);
    ImGui::PushTextWrapPos(460 * s);
    char what[512];
    std::snprintf(what, sizeof what, tr("Voxel Remesh at resolution %d makes about %s triangles and needs about %s of memory (%s free)."),
                  voxelResolution_, formatTriangles(e.triangles).c_str(), memoryText(e.bytes).c_str(), memoryText(c.availableBytes).c_str());
    ImGui::Text("%s", what);
    ImGui::TextWrapped("%s", tr("It can take a few minutes; Cancel in Tool > Remesh stops it and the Part stays as it is. "
                                "This won't ask again until the Resolution goes back to 512 or less."));
    // Only for a heavy result: tens of millions of triangles, or most of the free memory.
    if (e.triangles > 10'000'000 || (c.availableBytes > 0 && e.bytes > c.availableBytes / 2))
        ImGui::TextWrapped("%s", tr("Sculpting that many triangles needs a fast computer: Decimate or a lower Resolution makes it lighter."));
    ImGui::PopTextWrapPos();
    bool go = false;
    if (ImGui::Button(tr("Remesh##voxelhighres"), ImVec2(140 * s, 0))) go = true;
    ImGui::SameLine();
    if (ImGui::Button(tr("Cancel##voxelhighres"), ImVec2(120 * s, 0))) open = false;
    ImGui::End();
    if (go) {
        const RemeshTrigger trigger = voxelHighResConfirm_.trigger;
        voxelHighResConfirm_ = {};
        voxelHighResOk_ = true;
        startVoxelRemesh(trigger);
    } else if (!open) {
        voxelHighResConfirm_ = {};
        status_ = "Voxel Remesh cancelled";
    }
}

void App::startVoxelRemesh(RemeshTrigger trigger) {
    if (voxelRemesh_) return;
    // #163: the button asks on a partly hidden Part (Quick Remesh and Remesh after each stroke just go).
    if (!unhideAllows("Voxel Remesh", [this, trigger] { startVoxelRemesh(trigger); }, trigger != RemeshTrigger::Button))
        return;
    voxelResolution_ = std::clamp(voxelResolution_, kVoxelResolutionMin, kVoxelResolutionMax);
    if (!levelsAllow("Voxel Remesh", [this, trigger] { startVoxelRemesh(trigger); }, trigger != RemeshTrigger::Button)) return;
    Args args{{"resolution", double(voxelResolution_)}};
    if (voxelKeepMasked_) args["keep_masked"] = true;
    if (voxelResolution_ > kVoxelDenseMax) {
        // Never past the free memory up there (it would take the whole computer down); asked once per trip above 512.
        const VoxelRemeshEstimate& e = voxelEstimate();
        if (const MemoryCheck c = checkMemory(e.bytes); !c.enough) {
            status_ = "Voxel Remesh at " + std::to_string(voxelResolution_) + " needs ~" + memoryText(e.bytes) + " free, " +
                      memoryText(c.availableBytes) + " available. Lower the Resolution or close other apps"; // (as the panel)
            lastCommandOk_ = false;
            return;
        }
        if (!voxelHighResOk_) {
            voxelHighResConfirm_ = {true, trigger};
            status_ = "Voxel Remesh at " + std::to_string(voxelResolution_) + ": about " + formatTriangles(e.triangles) +
                      " triangles. Remesh or cancel in the dialog";
            return;
        }
    } else {
        voxelHighResOk_ = false;
        if (!memoryAllows("remesh", args, [this, trigger] { startVoxelRemesh(trigger); })) return;
    }
    // Always in the background with progress, so the UI never freezes (keeping a texture adds Auto UV of the
    // result, seconds to minutes); the result lands as the remesh command's would (pollVoxelRemesh).
    if (doc_.sculptor.strokeActive()) endStroke();
    auto job = std::make_unique<VoxelRemeshJob>();
    job->trigger = trigger;
    job->revision = doc_.undo.revision();
    job->progress = std::make_shared<std::atomic<float>>(0.0f);
    job->cancel = std::make_shared<std::atomic<bool>>(false);
    job->result = std::make_shared<Mesh>();
    job->error = std::make_shared<std::string>();
    job->note = std::make_shared<std::string>();
    job->part = doc_.activePartInfo().id;
    job->triangles = doc_.mesh.triangleCount();
    job->resolution = voxelResolution_;
    bool allMasked = false;
    std::vector<uint8_t> keep = keptForRemesh(doc_.mesh, voxelKeepMasked_, allMasked);
    if (allMasked) {
        status_ = "Remesh: everything is masked, so nothing would change (Keep Masked)";
        return;
    }
    job->keepMasked = !keep.empty();
    job->keepTexture = keepTexture_;
    const bool texture = hasTexture(doc_.mesh) && keepTexture_;
    std::promise<bool> promise;
    job->done = promise.get_future();
    std::thread([promise = std::move(promise), source = std::make_shared<Mesh>(doc_.mesh), res = job->resolution,
                 progress = job->progress, cancel = job->cancel, result = job->result, error = job->error,
                 note = job->note, keep = std::move(keep), texture, keepTexture = keepTexture_]() mutable {
        progress->store(0.02f);
        const float remeshShare = texture ? 0.13f : 0.9f; // the rest: Keep Masked, then the texture's Auto UV
        *result = voxelRemesh(*source, res, error.get(), [&](float f) {
            progress->store(0.02f + remeshShare * f);
            return !cancel->load();
        });
        bool ok = result->triangleCount() > 0;
        if (ok && !keep.empty()) { // Keep Masked: the masked part goes back in, joined to the new surface
            Mesh stitched;
            std::string keepNote;
            ok = stitchKeptTriangles(*source, keep, *result, stitched, *error, &keepNote);
            if (ok) *result = std::move(stitched);
            else *error = "Remesh (Keep Masked): " + *error;
            if (ok && !keepNote.empty()) *note = "; Keep masked: " + keepNote;
        }
        progress->store(texture ? 0.15f : 0.98f);
        if (ok) *note = carryTextureNote(*source, *result, keepTexture, [&](float f) {
            progress->store(0.15f + 0.85f * f);
            return !cancel->load();
        }) + *note;
        if (ok && cancel->load()) {
            ok = false;
            *error = "Voxel Remesh cancelled";
        }
        promise.set_value(ok);
    }).detach();
    const int res = job->resolution;
    autoRemeshDue_ = false; // (this remesh covers every stroke so far)
    voxelRemesh_ = std::move(job);
    status_ = trigger == RemeshTrigger::AfterStroke ? "Remesh after stroke: the stroke stretched the surface, remeshing at " +
                                                          std::to_string(res) + "..."
              : trigger == RemeshTrigger::Quick ? "Quick Remesh: remeshing at resolution " + std::to_string(res) + "..."
              : texture                          ? "Voxel Remesh: remeshing and keeping the texture..."
                                                 : "Voxel Remesh: remeshing...";
}

void App::pollVoxelRemesh() {
    // Remesh after each stroke: a stretching stroke asks; the remesh starts once no stroke or job is in the way.
    if (autoRemeshDue_ && !voxelRemesh_ && !jobWaitsForStroke()) {
        autoRemeshDue_ = false;
        quickRemesh(RemeshTrigger::AfterStroke);
    }
    if (!voxelRemesh_) return;
    const char* name = voxelRemesh_->trigger == RemeshTrigger::Quick         ? "Quick Remesh: "
                       : voxelRemesh_->trigger == RemeshTrigger::AfterStroke ? "Remesh after stroke: "
                                                                             : "Voxel Remesh: ";
    if (voxelRemesh_->done.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
        status_ = name + std::to_string(int(voxelRemesh_->progress->load() * 100.0f)) + "%";
        return;
    }
    if (jobWaitsForStroke()) return; // (the result lands when the stroke ends)
    const std::unique_ptr<VoxelRemeshJob> job = std::move(voxelRemesh_);
    if (!job->done.get()) {
        status_ = *job->error;
        return;
    }
    if (job->trigger != RemeshTrigger::Button && doc_.undo.revision() != job->revision) {
        // Sculpted (or undone) while it ran: landing would throw that work away, so it remeshes the Part as it is now.
        status_ = std::string(name) + "the Part changed while it was remeshed, remeshing again...";
        startVoxelRemesh(job->trigger);
        return;
    }
    int index = -1;
    for (int i = 0; i < doc_.partCount(); ++i)
        if (doc_.parts[size_t(i)].id == job->part) index = i;
    if (index < 0 || doc_.partMesh(index).triangleCount() != job->triangles) {
        status_ = "Voxel Remesh skipped: the Part changed while it was being remeshed";
        return;
    }
    const JobPart part(*this, index);
    doc_.replaceMeshUndoable(std::move(*job->result), "Remesh", true); // as the remesh command
    CommandResult r{true, std::string(job->trigger == RemeshTrigger::Quick         ? "Quick Remesh: remeshed"
                                      : job->trigger == RemeshTrigger::AfterStroke ? "Remeshed after the stroke"
                                                                                   : "Remeshed") +
                              " to " + std::to_string(doc_.mesh.triangleCount()) + " triangles" + *job->note};
    if (job->trigger != RemeshTrigger::Button) r.message += " (resolution " + std::to_string(job->resolution) + "; " +
                                                          actions_.shortcutText("edit.undo") + " undoes it)";
    if (const std::string notice = doc_.takeNotice(); !notice.empty()) r.message += ". " + notice; // levels dropped
    status_ = r.message;
    lastCommandOk_ = true;
    clearLods();
    playingLayer_ = -1;
    uvLinesKey_ = 0;
    updateTitle();
    Args echo{{"resolution", double(job->resolution)}};
    if (job->keepMasked) echo["keep_masked"] = true;
    if (!job->keepTexture && job->note->find("texture") != std::string::npos) echo["keep_texture"] = false;
    consoleEcho("remesh", echo, r);
}

// ---------------------------------------------------------------------------
// Quick Remesh and Remesh after each stroke (#121)
// ---------------------------------------------------------------------------

void App::quickRemesh(RemeshTrigger trigger) {
    const char* name = trigger == RemeshTrigger::AfterStroke ? "Remesh after stroke: " : "Quick Remesh: ";
    if (voxelRemesh_) {
        if (trigger == RemeshTrigger::Quick) status_ = std::string(name) + "already remeshing (Tool > Remesh > Cancel stops it)";
        return; // (a stroke made meanwhile: the running quick remesh starts again when it finishes)
    }
    if (doc_.mesh.triangleCount() == 0) {
        status_ = std::string(name) + "the active Part has no surface to remesh";
        return;
    }
    if (!doc_.activePartInfo().visible) {
        status_ = std::string(name) + "the active Part is hidden";
        return;
    }
    startVoxelRemesh(trigger);
}

void App::strokeEndedForRemesh(const UndoRecord& stroke) {
    lastStrokeStretched_ = 0;
    if (!uiPrefs_.remeshAfterStroke || stroke.positions.empty() || stroke.layer >= 0 || stroke.topology || stroke.mesh ||
        stroke.micro || stroke.subdiv)
        return;
    // Not where a remesh would throw work away unasked: subdivision levels, sculpt layers, a painted texture.
    if (doc_.mesh.multires || !doc_.mesh.layers.empty() || (hasTexture(doc_.mesh) && keepTexture_)) return;
    const StrokeStretch s = strokeStretch(doc_.mesh, stroke.positions.indices, stroke.positions.values);
    lastStrokeStretched_ = s.stretched;
    if (stretchWantsRemesh(s)) autoRemeshDue_ = true; // pollVoxelRemesh starts it once nothing is in the way
}

void App::quickRemeshKey(SDL_Keycode key, SDL_Keymod mods, bool down) {
    if (!down) {
        if (key == quickRemeshDragKey_) quickRemeshDragKey_ = SDLK_UNKNOWN;
        return;
    }
    const Action* a = actions_.actionForChord(chordFromEvent(key, mods));
    if (a && a->id == "geometry.quick_remesh_drag" && uiPrefs_.quickRemesh) quickRemeshDragKey_ = key;
}

// With the drag key held (N by default, Preferences > Hotkeys), a press on the background (no surface under it)
// remeshes when it's let go. On the model the press stays a stroke. The key is unbound in every input style, so
// no empty-canvas drag of any style (turn, pan, zoom, mask and hide shapes) changes.
bool App::quickRemeshDragDown(float x, float y) {
    if (quickRemeshDragKey_ == SDLK_UNKNOWN || !uiPrefs_.quickRemesh) return false;
    RayHit hit;
    if (previewLod_ < 0 && pick(x, y, hit)) return false;
    drag_ = Drag::QuickRemesh;
    status_ = "Quick Remesh: let go to remesh the active Part";
    return true;
}

void App::drawQuickRemeshContents() {
    tray::GroupLabel(*this, "Quick Remesh");
    bool quick = uiPrefs_.quickRemesh;
    if (ImGui::Checkbox(tr("Quick Remesh##remesh.quick"), &quick)) uiPrefs_.quickRemesh = quick;
    const std::string key = actions_.shortcutText("geometry.quick_remesh");
    const std::string drag = actions_.shortcutText("geometry.quick_remesh_drag");
    const std::string help =
        "When on, " + (key.empty() ? std::string("the Quick Remesh key (Preferences > Hotkeys)") : key) +
        " remeshes the active Part at the Resolution above, in the background (progress and Cancel here)" +
        (drag.empty() ? std::string(".") : "; so does holding " + drag + " and clicking or dragging on the background.") +
        " Keep Masked and Keep Texture apply. Both keys change in Preferences > Hotkeys.";
    ui::HelpText("Quick Remesh", nullptr, help.c_str(), "Tool > Remesh");
    bool after = uiPrefs_.remeshAfterStroke;
    if (ImGui::Checkbox(tr("Remesh after each stroke##remesh.after_stroke"), &after)) uiPrefs_.remeshAfterStroke = after;
    ui::HelpText("Remesh after each stroke", nullptr,
                 "After a stroke that stretched the surface (edges pulled to more than 1.6 times their length), the "
                 "active Part is remeshed at the Resolution above in the background. Sculpting never waits: a stroke "
                 "made meanwhile remeshes again. Each remesh is one undo step. Not on a Part with subdivision levels, "
                 "sculpt layers or a painted texture.",
                 "Tool > Remesh");
}

void App::pollQRemesh() {
    pollVoxelRemesh(); // polled with QRemesh, its sibling rebuild job
    if (!qremesh_) return;
    if (qremesh_->done.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
        status_ = "Quad Remesh: " + std::to_string(int(qremesh_->progress->load() * 100.0f)) + "%";
        return;
    }
    if (jobWaitsForStroke()) return; // (the result lands when the stroke ends)
    const std::unique_ptr<QRemeshJob> job = std::move(qremesh_);
    if (!job->done.get()) {
        status_ = *job->error;
        return;
    }
    // The Part may be inactive now, or changed topology meanwhile.
    int index = -1;
    for (int i = 0; i < doc_.partCount(); ++i)
        if (doc_.parts[size_t(i)].id == job->part) index = i;
    if (index < 0 || doc_.partMesh(index).triangleCount() != job->triangles) {
        status_ = "Quad Remesh skipped: the Part changed while it was being remeshed";
        return;
    }
    const JobPart part(*this, index);
    lastQRemeshStats_ = *job->stats;
    haveQRemeshStats_ = true;
    CommandResult r{true, applyQRemesh(doc_, std::move(*job->result), job->keepOriginal, *job->stats) + *job->textureNote};
    lastQRemeshPart_ = doc_.activePartInfo().id;
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
    tray::GroupLabel(*this, "Quad Remesh");
    const float full = ImGui::GetContentRegionAvail().x;
    const float half = (full - style.ItemSpacing.x) * 0.5f;
    const float third = (full - 2.0f * style.ItemSpacing.x) / 3.0f;

    if (qremesh_) {
        ImGui::ProgressBar(qremesh_->progress->load(), ImVec2(half, 0), tr("Remeshing..."));
        ui::HelpText("Quad Remesh", nullptr, "Remeshing in the background. The result replaces the Part when it's done.",
                     kQRemeshPath);
        ImGui::SameLine();
        if (ImGui::Button(tr("Cancel"), ImVec2(-1, 0))) qremesh_->cancel->store(true);
        ui::HelpText("Cancel", nullptr, "Stop Quad Remesh; the Part stays as it is.", kQRemeshPath);
    } else {
        ui::ActionButton(*this, "geometry.qremesh", ImVec2(half, 0));
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled(tr("%zu polygons"), faceCount(doc_.mesh));
    }

    {
        const char* methods[] = {tr("Quad Remesh 2"), tr("Legacy")};
        ImGui::SetNextItemWidth(-1);
        ImGui::Combo("##qmethod", &retopo_.method, methods, 2);
        ui::HelpText("Method", nullptr,
                     "Quad Remesh 2 (default): a curvature-aligned layout solved as one integer grid over the whole model, "
                     "so poles appear only where the form needs them and edge loops run unbroken; open guide curves "
                     "become exact edge loops. Legacy: the method of SculptForge 0.9 (more poles, a split look).",
                     kQRemeshPath);
    }
    ImGui::SetNextItemWidth(-1);
    tray::SliderInt("##qtarget", &retopo_.target, 100, 200000, "Target Quads %d", ImGuiSliderFlags_Logarithmic);
    ui::HelpText("Target Quads", nullptr,
                 "About how many quads Quad Remesh makes (Ctrl+click to type a number). 50% / 100% / 200% set it from "
                 "the Part's current polygon count.",
                 kQRemeshPath);
    const size_t faces = faceCount(doc_.mesh);
    auto setTarget = [&](double k) { retopo_.target = int(std::clamp(double(faces) * k, 100.0, 2'000'000.0)); };
    if (ImGui::Button(tr("50%"), ImVec2(third, 0))) setTarget(0.5);
    ui::HelpText("50%", nullptr, "Target half the Part's current polygon count.", kQRemeshPath);
    ImGui::SameLine();
    if (ImGui::Button(tr("100%"), ImVec2(third, 0))) setTarget(1.0);
    ui::HelpText("100%", nullptr, "Target the Part's current polygon count (to clean up topology without changing its density).",
                 kQRemeshPath);
    ImGui::SameLine();
    if (ImGui::Button(tr("200%"), ImVec2(-1, 0))) setTarget(2.0);
    ui::HelpText("200%", nullptr, "Target twice the Part's current polygon count.", kQRemeshPath);

    ImGui::SetNextItemWidth(-1);
    ui::SliderInside("##qadaptive", &retopo_.adaptive, 0.0f, 1.0f, "Size Variation %.2f");
    ui::HelpText("Size Variation", nullptr,
                 "0: quads all about the same size. Higher: smaller quads where the surface curves, bigger ones on "
                 "flat areas (the count stays near the target).",
                 kQRemeshPath);

    ImGui::Checkbox(tr("Follow Face Groups"), &retopo_.keepGroups);
    ui::HelpText("Follow Face Groups", nullptr,
                 "Edge loops follow the face group borders exactly and the result keeps the face groups. Paint groups "
                 "where you want loops (around the eyes, the mouth) to guide the flow.",
                 kQRemeshPath);
    // The right column, inside the section indent, when the left label leaves room (else its own row).
    if (ImGui::GetItemRectMax().x + style.ItemSpacing.x <= ImGui::GetWindowPos().x + x0 + half + style.ItemSpacing.x) {
        ImGui::SameLine();
        ImGui::SetCursorPosX(x0 + half + style.ItemSpacing.x);
    }
    ImGui::Checkbox(tr("Keep Hard Edges"), &retopo_.detectEdges);
    ui::HelpText("Keep Hard Edges", nullptr, "Hard edges get an edge loop, so they stay sharp (Quad Remesh 2: faces meeting at more than about 60 degrees along a line at least a few quads long; legacy: more than 50 degrees).",
                 kQRemeshPath);
    ImGui::Checkbox(tr("Use Vertex Paint"), &retopo_.useVertexPaint);
    ui::HelpText("Use Vertex Paint", nullptr,
                 "Paint where you want density: red areas get up to 4 times the quads, blue areas down to a quarter, "
                 "white is unchanged.",
                 kQRemeshPath);
    ImGui::SameLine();
    ImGui::SetCursorPosX(x0 + half + style.ItemSpacing.x); // the right column, inside the section indent
    ImGui::Checkbox(tr("Symmetry"), &retopo_.symmetry);
    const char axis = char('X' + symmetryAxis(symmetry_));
    const std::string symHelp = std::string("Mirror the topology across ") + axis +
                                " (the Transform palette's symmetry axis), welded on the seam. The mirror plane is the "
                                "model's own, so a model away from the world centre works too; a model that isn't "
                                "mirror-symmetric is remeshed without symmetry (the status line says so).";
    ui::HelpText("Symmetry", nullptr, symHelp.c_str(), kQRemeshPath);
    ImGui::Checkbox(tr("Keep Original"), &retopo_.keepOriginal);
    ui::HelpText("Keep Original", nullptr,
                 "Keep the Part as it was, hidden, right below the new one (\"<name> Original\"), ready to "
                 "Project Detail its detail back. Off: replace it (undo brings it back).",
                 kQRemeshPath);
    ImGui::SameLine();
    ImGui::SetCursorPosX(x0 + half + style.ItemSpacing.x); // the right column, inside the section indent
    ImGui::Checkbox(tr("Keep Masked##qremesh"), &retopo_.keepMasked);
    ui::HelpText("Keep Masked", nullptr, kKeepMaskedHelp, kQRemeshPath);
    if (hasTexture(doc_.mesh)) // only matters for a painted Part
        drawKeepTextureCheckbox("Keep Texture##qremesh", kQRemeshPath);
    drawQRemeshGuideControls();
    drawQRemeshReport();
}

void App::drawQRemeshReport() {
    if (!haveQRemeshStats_ || lastQRemeshPart_ != doc_.activePartInfo().id) return;
    const QRemeshStats& st = lastQRemeshStats_;
    const QuadQuality& q = st.quality;
    if (q.quads == 0) return;
    tray::GroupLabel(*this, "Quality Report");
    ImGui::TextDisabled(tr("%s: %zu quads in %.1f s"), st.method == QRemeshMethod::Legacy ? "Legacy" : "Quad Remesh 2", q.quads,
                        st.seconds);
    ImGui::TextDisabled(tr("Poles: %zu  (3 edges %zu, 5 edges %zu, other %zu)"), q.poles, q.valence[3], q.valence[5],
                        q.poles - q.valence[3] - q.valence[5]);
    ImGui::TextDisabled(tr("Corners off square: %.1f deg mean, %.0f deg worst 1%%"), double(q.angleDeviation),
                        double(q.angleDeviationMax));
    if (q.measured)
        ImGui::TextDisabled(tr("Distance to the sculpt: %.3f%% mean, %.2f%% max"), double(q.meanDistance), double(q.hausdorff));
    if (st.fallbackPieces) ImGui::TextDisabled(tr("%zu piece(s) used the legacy method"), st.fallbackPieces);
    ui::HelpText("Quality Report", nullptr,
                 "How clean the new topology is: poles are vertices with other than four edges (fewer is cleaner), "
                 "corners off square measure how far quad corners are from 90 degrees, and the distance is how far the "
                 "new surface strays from the sculpt (as a percentage of its size; Project Detail brings fine detail back).",
                 kQRemeshPath);
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
    if (doc_.qremeshGuides.empty()) ImGui::TextDisabled("%s", tr("no curves"));
    else ImGui::TextDisabled(tr("%zu curves (%zu loops)"), doc_.qremeshGuides.size(), loops);
    ui::ActionButton(*this, "geometry.qremesh_guides_delete_last", ImVec2(half, 0));
    ImGui::SameLine();
    ui::ActionButton(*this, "geometry.qremesh_guides_clear", ImVec2(-1, 0));
    ImGui::SetNextItemWidth(-1);
    ui::SliderInside("##qcurves", &curvesStrength_, 0.0f, 100.0f, "Guide Strength %.0f");
    ui::HelpText("Guide Strength", nullptr,
                 "How far around each guide curve the edge flow follows it: 0 = only the edge loop on the curve, 100 = "
                 "about six quads to each side. The loop on the curve is always made.",
                 kQRemeshPath);
}

// Esc, Draw / Move / Scale / Rotate and any other brush leave the QRemesh Guides brush, with the status saying so (mode matrix).
std::string App::leaveGuides() {
    if (!guidesMode_) return "";
    guidesMode_ = false;
    guidePath_.clear();
    return "Quad Remesh Guides";
}

void App::setGuidesMode(bool on) {
    if (drag_ == Drag::Guide) return;
    guidesMode_ = on;
    guidePath_.clear();
    if (on) {
        guidesFrom_ = {brush_, doc_.activeBrush, paintMode_};
        if (gizmoActive()) setGizmoMode(GizmoMode::Off);
        status_ = "Quad Remesh Guides: drag on the model to draw a curve for Quad Remesh to follow (end where you started for a "
                  "loop); Alt+click a curve deletes it";
    } else {
        status_ = "Draw";
    }
}

void App::syncGuidesMode() {
    // Picking a brush (picker, shelf, keys, Library), Paint or the Gizmo leaves the guides brush.
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
            if (!partProject(guide.points[i % n], p)) {
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
        status_ = "Quad Remesh guide deleted; " + std::to_string(guides.size()) + " left";
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
    // Stroke smoothing: the curve follows the pointer at a distance (a lazy mouse), so a hand's or a pen's jitter
    // doesn't make it wobble; points every 4 px.
    const float dx = x - guidePathLast_[0], dy = y - guidePathLast_[1], d = std::hypot(dx, dy);
    const float lag = 6.0f * uiScale_;
    if (d < lag + 4.0f * uiScale_) return;
    const float k = (d - lag) / d;
    const float sx = guidePathLast_[0] + dx * k, sy = guidePathLast_[1] + dy * k;
    RayHit hit;
    if (!pick(sx, sy, hit)) return; // off the model: the curve continues where it comes back
    guidePath_.push_back(hit.position);
    guidePathLast_[0] = sx;
    guidePathLast_[1] = sy;
}

namespace {

// Corner cutting (Chaikin), twice: the screen polyline of a guide drawn as a smooth curve, not 2 px segments.
void smoothPolyline(std::vector<ImVec2>& pts, bool closed) {
    for (int pass = 0; pass < 2 && pts.size() >= 3; ++pass) {
        std::vector<ImVec2> out;
        out.reserve(pts.size() * 2 + 2);
        const size_t n = pts.size(), segs = closed ? n : n - 1;
        if (!closed) out.push_back(pts[0]);
        for (size_t i = 0; i < segs; ++i) {
            const ImVec2 a = pts[i], b = pts[(i + 1) % n];
            out.push_back(ImVec2(a.x * 0.75f + b.x * 0.25f, a.y * 0.75f + b.y * 0.25f));
            out.push_back(ImVec2(a.x * 0.25f + b.x * 0.75f, a.y * 0.25f + b.y * 0.75f));
        }
        if (!closed) out.push_back(pts[n - 1]);
        pts.swap(out);
    }
}

// A guide line: a dark outline under the colour, so it reads on light and dark models alike.
void guideLine(ImDrawList* dl, std::vector<ImVec2>& pts, bool closed, ImU32 col, float width, bool outline) {
    if (pts.size() < 2) return;
    smoothPolyline(pts, closed);
    const ImDrawFlags flags = closed ? ImDrawFlags_Closed : ImDrawFlags_None;
    if (outline) dl->AddPolyline(pts.data(), int(pts.size()), IM_COL32(12, 12, 16, (col >> IM_COL32_A_SHIFT) * 3 / 4), flags, width + 2.0f);
    dl->AddPolyline(pts.data(), int(pts.size()), col, flags, width);
}

} // namespace

void App::guideRelease() {
    std::vector<glm::vec3> path = std::move(guidePath_);
    guidePath_.clear();
    doc_.syncBvh();
    const int added = path.size() >= 2 ? addQRemeshGuide(doc_.qremeshGuides, doc_.mesh, doc_.bvh, path, symmetry_) : 0;
    if (added == 0) {
        status_ = "Quad Remesh Guides: the curve is too short (drag across the model)";
        return;
    }
    doc_.modified = true;
    const QRemeshGuide& g = doc_.qremeshGuides[doc_.qremeshGuides.size() - size_t(added)];
    status_ = std::string("Quad Remesh guide added (") + (g.closed ? "a loop" : "a curve") + (added > 1 ? ", mirrored" : "") +
              "); " + std::to_string(doc_.qremeshGuides.size()) + " curves. Run Quad Remesh to use them";
}

void App::drawQRemeshGuides() {
    syncGuidesMode();
    const std::vector<QRemeshGuide>& guides = doc_.qremeshGuides;
    if ((guides.empty() && guidePath_.empty()) || !doc_.activePartInfo().visible) return;
    ImDrawList* dl = ImGui::GetBackgroundDrawList(); // under the panels, over the model
    const CanvasRect cr = canvasRect();
    dl->PushClipRect(ImVec2(cr.x, cr.y), ImVec2(cr.x + cr.w, cr.y + cr.h), true);
    const float s = uiScale_;

    // Which points the model hides: a ray from the eye to each one (refreshed when the view or the guides change;
    // at most ~2000 rays a frame, the points between them follow their neighbours).
    {
        const glm::mat4 vp = partCamera().projection() * partCamera().view();
        uint64_t key = 1469598103934665603ull;
        auto mix = [&](const void* data, size_t bytes) {
            const auto* b = static_cast<const unsigned char*>(data);
            for (size_t i = 0; i < bytes; ++i) key = (key ^ b[i]) * 1099511628211ull;
        };
        mix(&vp, sizeof vp);
        mix(&cr, sizeof cr);
        const uint64_t rev = doc_.undo.revision(), sub = doc_.activePartInfo().id;
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
                    if (!partProject(pts[i], sp)) {
                        vis[i] = 0;
                        continue;
                    }
                    glm::vec3 origin, dir;
                    partRay(sp.x, sp.y, origin, dir);
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
        const float width = (guidesMode_ ? 2.5f : 1.6f) * s;
        // Runs of points with the same visibility, each drawn as one smooth polyline.
        std::vector<ImVec2> run;
        bool runVis = false;
        auto flush = [&](bool closed) {
            if (runVis) guideLine(dl, run, closed, front, width, true);
            else guideLine(dl, run, closed, back, 1.25f * s, false);
            run.clear();
        };
        bool whole = guide.closed;
        for (size_t i = 0; i < n; ++i) {
            ImVec2 p;
            const bool on = partProject(guide.points[i], p);
            const bool vis = on && guideVisible_[g][i] != 0;
            if (!on || (!run.empty() && vis != runVis)) {
                whole = false;
                if (!run.empty() && on) {
                    const ImVec2 keep = run.back();
                    flush(false);
                    run.push_back(keep); // the next run starts where this one ended
                } else {
                    flush(false);
                }
            }
            if (!on) continue;
            runVis = vis;
            run.push_back(p);
        }
        if (guide.closed && !whole && !run.empty() && n > 0) { // close the loop back to the first point
            ImVec2 p0;
            if (partProject(guide.points[0], p0)) run.push_back(p0);
        }
        flush(whole);
    }
    // The curve being drawn, with its mirror images.
    if (drag_ == Drag::Guide && guidePath_.size() >= 2)
        for (const glm::vec3& m : Sculptor::mirrors(symmetry_)) {
            const bool own = m == glm::vec3(1.0f);
            std::vector<ImVec2> pts;
            for (const glm::vec3& q : guidePath_) {
                ImVec2 a;
                if (partProject(q * m, a)) pts.push_back(a);
            }
            if (drag_ == Drag::Guide && own) pts.push_back(ImVec2(mouseX_, mouseY_)); // up to the pointer (the lazy lag)
            guideLine(dl, pts, false, own ? IM_COL32(255, 230, 140, 255) : IM_COL32(255, 230, 140, 140), (own ? 2.5f : 1.5f) * s,
                      own);
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
    // Each pair shares a row when both labels fit half of it, else each takes its own row (no words cut off).
    const bool detailPair = ui::ButtonWidth("Project Detail") <= half && ui::SliderWidth("Dist %.3f", 0.2) <= half;
    ui::ActionButton(*this, "part.project_all", ImVec2(detailPair ? half : -1.0f, 0));
    if (detailPair) ImGui::SameLine();
    ImGui::SetNextItemWidth(-1);
    ui::SliderInside("##pdist", &retopo_.projectDist, 0.001f, 0.2f, "Dist %.3f", ImGuiSliderFlags_Logarithmic);
    ui::HelpText("Dist", nullptr,
                 "How far each vertex looks for the source surface, along its normal, as a fraction of the Part's "
                 "size. Raise it if parts don't pick up the detail; lower it where surfaces are close together.",
                 kProjectPath);
    const bool blurPair = ui::SliderWidth("Blur %d", 10) <= half && ui::CheckboxWidth("Mean") <= half;
    ImGui::SetNextItemWidth(blurPair ? half : -1.0f);
    tray::SliderInt("##pblur", &retopo_.projectBlur, 0, 10, "Blur %d");
    ui::HelpText("Blur", nullptr, "Smooth the projected result this many times (softens noise from the source).",
                 kProjectPath);
    if (blurPair) ImGui::SameLine();
    ui::Checkbox("Mean", &retopo_.projectMean);
    ui::HelpText("Mean", nullptr,
                 "Where the source is found on both sides of a vertex, settle halfway between instead of on the nearest.",
                 kProjectPath);
    const bool whatPair = ui::CheckboxWidth("Geometry") <= half && ui::CheckboxWidth("Vertex Paint") <= half;
    ui::Checkbox("Geometry", &retopo_.projectGeometry);
    ui::HelpText("Geometry", nullptr, "Project the shape (into the active sculpt layer).", kProjectPath);
    if (whatPair) {
        ImGui::SameLine();
        ImGui::SetCursorPosX(x0 + half + style.ItemSpacing.x); // the right column, inside the section indent
    }
    ui::Checkbox("Vertex Paint", &retopo_.projectVertexPaint);
    ui::HelpText("Vertex Paint", nullptr, "Project the vertex paint (into the active paint layer) when a source has any.",
                 kProjectPath);
}

} // namespace sf
