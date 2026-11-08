// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Tool > Garments > Drape on the body (core/GarmentDrape.h): Place panels, then click the body once per panel (it
// sits a little in front of the click, facing the view; Turn, Wrap and Gap adjust it); the placed panels show as a
// preview Part. Drape simulates the sewn pattern on a worker, shown live on the preview; Convert to Part turns the
// result into the Part "Garment" (one undo step). Placement is the user's alone: nothing is placed or sewn for them
// (docs/design/garments.md, "Do not build").
#include "app/App.h"
#include "app/Ui.h"
#include "core/GarmentDrape.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>

namespace sf {

using namespace garment;

namespace {
constexpr const char* kPath = "Tool > Garments";
constexpr const char* kHint = "Place each panel slightly in front of the body at its widest point, from the view that faces it: a front panel on the front of the chest, the back from behind, a sleeve on the upper arm from its side.";
} // namespace

struct App::GarmentDrapeJob {
    Pattern pat;
    GarmentDrapeScene scene;
    ClothSettings settings;
    ClothSim sim;
    std::thread worker;
    std::atomic<bool> stop{false}, paused{false}, done{false};
    std::mutex lock;
    std::vector<glm::vec3> latest; // metres, the worker's last frame
    int frame = 0, shownFrame = -1;
    bool rested = false;
    std::chrono::steady_clock::time_point began = std::chrono::steady_clock::now();
    ~GarmentDrapeJob() {
        stop = true;
        if (worker.joinable()) worker.join();
    }
};

namespace {

int placedCount(const Pattern& pat) {
    int n = 0;
    for (const Panel& p : pat.panels) n += p.place.placed ? 1 : 0;
    return n;
}

std::string vec3Text(glm::vec3 v) {
    char buf[96];
    std::snprintf(buf, sizeof buf, "%.6g,%.6g,%.6g", double(v.x), double(v.y), double(v.z));
    return buf;
}

} // namespace

// The preview Part shows the placed panels (seams open). Not an undo step: Convert to Part makes it one.
void App::garmentPreviewRefresh() {
    garmentPreviewDirty_ = false;
    if (garmentDrape_) return; // (the simulation owns the preview while it runs)
    Pattern pat;
    const bool any = documentPattern(doc_, pat) && placedCount(pat) > 0;
    if (any && garmentEditPanel_ >= 0 && garmentEditPanel_ < int(pat.panels.size()) && garmentEditing_)
        pat.panels[size_t(garmentEditPanel_)].place = garmentEdit_; // (a slider being dragged)
    GarmentMesh m;
    std::vector<glm::vec3> world;
    std::string error;
    Mesh mesh;
    if (any && triangulatePattern(pat, m, error)) {
        placedPositions(pat, m, world, error);
        mesh = previewMesh(m, world);
    }
    const int at = garmentPreview_ ? doc_.partIndex(garmentPreview_) : -1;
    if (mesh.triangleCount() == 0) {
        if (at >= 0 && at != doc_.activePart) doc_.removePart(at);
        garmentPreview_ = 0;
        garmentDrapeResult_.clear();
        return;
    }
    if (at >= 0 && at != doc_.activePart) {
        PartData& s = *doc_.parts[size_t(at)].data;
        s.mesh = std::move(mesh);
        s.mesh.topologyDirty = true;
        s.bvh.build(s.mesh);
        s.bvhDirty = false;
    } else {
        const int i = doc_.addPart(std::move(mesh), doc_.uniquePartName("Garment preview"), false);
        garmentPreview_ = doc_.parts[size_t(i)].id;
    }
    garmentDrapeResult_.clear(); // (placed again: the last drape no longer matches)
}

void App::dropGarmentPreview() {
    garmentDrape_.reset();
    const int at = garmentPreview_ ? doc_.partIndex(garmentPreview_) : -1;
    if (at >= 0 && at != doc_.activePart) doc_.removePart(at);
    garmentPreview_ = 0;
    garmentDrapeResult_.clear();
}

bool App::garmentPlaceMouseDown(float x, float y, SDL_Keymod mod) {
    if (garmentPlacing_ < 0 || (mod & (SDL_KMOD_ALT | SDL_KMOD_SHIFT | SDL_KMOD_CTRL | SDL_KMOD_GUI))) return false;
    Pattern pat;
    if (!documentPattern(doc_, pat) || garmentPlacing_ >= int(pat.panels.size())) {
        garmentPlacing_ = -1;
        return false;
    }
    // The nearest visible Part under the cursor, through the preview.
    glm::vec3 origin, dir;
    canvasRay(x, y, origin, dir);
    RayHit best;
    bool found = false;
    for (int i = 0; i < doc_.partCount(); ++i) {
        const Part& part = doc_.parts[size_t(i)];
        if (!part.visible || part.id == garmentPreview_) continue;
        const glm::mat4 inv = part.moved() ? glm::inverse(part.transform) : glm::mat4(1.0f);
        RayHit hit;
        if (!doc_.partBvh(i).raycast(doc_.partMesh(i), glm::vec3(inv * glm::vec4(origin, 1.0f)), glm::mat3(inv) * dir, hit))
            continue;
        if (part.moved()) hit.position = glm::vec3(part.transform * glm::vec4(hit.position, 1.0f));
        if (!found || hit.t < best.t) best = hit, found = true;
    }
    if (!found) return false; // (the background navigates)
    const int panel = garmentPlacing_;
    const glm::mat4 toWorld = glm::inverse(camera_.view());
    Args args{{"panel", std::to_string(panel + 1)},
              {"point", vec3Text(best.position)},
              {"facing", vec3Text(-camera_.forward())},
              {"up", vec3Text(glm::normalize(glm::vec3(toWorld[1])))}};
    runCommand("garment_place", args);
    if (!lastCommandOk_) return true;
    garmentEditPanel_ = panel;
    garmentEditing_ = false;
    documentPattern(doc_, pat);
    garmentEdit_ = pat.panels[size_t(panel)].place;
    // On to the next panel not placed yet (the user can pick any other in the list).
    garmentPlacing_ = -1;
    for (size_t k = 1; k <= pat.panels.size(); ++k) {
        const size_t i = (size_t(panel) + k) % pat.panels.size();
        if (!pat.panels[i].place.placed) {
            garmentPlacing_ = int(i);
            break;
        }
    }
    status_ = garmentPlacing_ < 0
                  ? "Every panel is placed: Drape sews and settles it"
                  : "Placed " + pat.panels[size_t(panel)].name + ". Turn the view to face the body where " +
                        pat.panels[size_t(garmentPlacing_)].name + " goes and click there";
    garmentPreviewRefresh();
    return true;
}

void App::startGarmentDrape() {
    if (garmentDrape_) return;
    if (doc_.sculptor.strokeActive()) endStroke();
    auto job = std::make_shared<GarmentDrapeJob>();
    std::string err;
    if (!documentPattern(doc_, job->pat)) return void(status_ = "Drape: no garment pattern");
    if (!garmentDrapeScene(doc_, job->pat, job->scene, err, garmentPreview_)) return void(status_ = "Drape: " + err);
    garmentPlacing_ = -1;
    garmentPreviewRefresh(); // (every panel placed: the preview's vertices are the particles, in order)
    const int at = doc_.partIndex(garmentPreview_);
    if (at < 0 || doc_.partMesh(at).vertexCount() != job->scene.setup.positions.size())
        return void(status_ = "Drape: the preview doesn't match the pattern");
    job->settings = garmentSettings(job->pat);
    if (!job->sim.init(job->scene.setup, job->scene.fabric, job->settings))
        return void(status_ = "Drape: the pattern can't be simulated");
    job->sim.setColliders(&job->scene.colliders);
    GarmentDrapeJob* j = job.get();
    job->worker = std::thread([j] {
        constexpr int kMaxFrames = 900;
        while (!j->stop.load()) {
            if (j->paused.load()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(15));
                continue;
            }
            const bool moving = garmentStep(j->sim, j->frame, j->settings);
            std::lock_guard g(j->lock);
            j->latest = j->sim.positions();
            ++j->frame;
            if (!moving || j->frame >= kMaxFrames) {
                j->rested = !moving;
                break;
            }
        }
        j->done = true;
        wakeMainLoop();
    });
    garmentDrape_ = std::move(job);
    garmentDrapeResult_.clear();
    status_ = "Drape: sewing and settling";
}

void App::pollGarmentDrape() {
    if (garmentPreviewDirty_ && !garmentDrape_) garmentPreviewRefresh();
    if (!garmentDrape_) return;
    GarmentDrapeJob& j = *garmentDrape_;
    const int at = doc_.partIndex(garmentPreview_);
    if (at < 0 || at == doc_.activePart || doc_.partMesh(at).vertexCount() != j.scene.setup.positions.size()) {
        garmentDrape_.reset(); // (the preview went away under it)
        return;
    }
    {
        std::lock_guard g(j.lock);
        if (j.frame != j.shownFrame && j.latest.size() == j.scene.setup.positions.size()) {
            Mesh& mesh = doc_.partMesh(at);
            for (size_t v = 0; v < j.latest.size(); ++v) mesh.positions[v] = j.latest[v] * j.scene.scale;
            j.shownFrame = j.frame;
            mesh.recomputeAllNormals();
            mesh.markAllDirty();
            doc_.partBvh(at).refitAll(mesh);
        }
    }
    char line[128];
    std::snprintf(line, sizeof line, "Drape: frame %d, %.1f s%s", j.frame,
                  std::chrono::duration<double>(std::chrono::steady_clock::now() - j.began).count(),
                  j.paused ? " (paused)" : "");
    status_ = line;
    if (j.done.load()) finishGarmentDrape();
    else wakeMainLoop();
}

// Stops the simulation where it is: the preview keeps that shape for Convert to Part.
void App::finishGarmentDrape() {
    if (!garmentDrape_) return;
    std::shared_ptr<GarmentDrapeJob> job = std::move(garmentDrape_);
    job->stop = true;
    if (job->worker.joinable()) job->worker.join();
    std::lock_guard g(job->lock);
    garmentDrapeResult_ = job->latest.empty() ? job->scene.setup.positions : job->latest;
    char msg[160];
    std::snprintf(msg, sizeof msg, "Drape: %s after %d frames. Convert to Part keeps it", job->rested ? "at rest" : "stopped",
                  job->frame);
    status_ = msg;
}

void App::convertGarment() {
    Pattern pat;
    GarmentMesh m;
    std::string err;
    if (garmentDrapeResult_.empty() || !documentPattern(doc_, pat) || !triangulatePattern(pat, m, err) ||
        m.vertexCount() != garmentDrapeResult_.size())
        return void(status_ = "Convert: drape the garment first");
    Mesh mesh = drapedMesh(pat, m, garmentDrapeResult_);
    if (mesh.triangleCount() == 0) return void(status_ = "Convert: the garment came out empty");
    const size_t tris = mesh.triangleCount();
    const int i = addGarmentPart(doc_, std::move(mesh), garmentPreview_);
    garmentPreview_ = 0;
    garmentDrapeResult_.clear();
    status_ = "Converted to the Part " + doc_.parts[size_t(i)].name + " (" + std::to_string(tris) +
              " triangles; Ctrl+Z undoes it)";
}

void App::drawGarmentDrapeSection() {
    Pattern pat;
    if (!documentPattern(doc_, pat) || pat.panels.empty()) {
        if (garmentPreview_ || garmentPlacing_ >= 0) {
            garmentPlacing_ = -1;
            dropGarmentPreview();
        }
        return;
    }
    ImGui::SeparatorText(tr("Drape on the body"));
    const bool running = garmentDrape_ != nullptr;
    const float w = ImGui::GetContentRegionAvail().x;
    ImGui::BeginDisabled(running);
    const bool placing = garmentPlacing_ >= 0;
    if (placing) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
    const bool pressed = ImGui::Button(tr("Place panels##garment"), ImVec2(w, 0));
    if (placing) ImGui::PopStyleColor();
    if (pressed) {
        if (placing) {
            garmentPlacing_ = -1;
        } else {
            garmentPlacing_ = 0;
            for (size_t i = 0; i < pat.panels.size(); ++i)
                if (!pat.panels[i].place.placed) {
                    garmentPlacing_ = int(i);
                    break;
                }
            status_ = "Click the body where " + pat.panels[size_t(garmentPlacing_)].name + " goes. " + kHint;
        }
    }
    ui::HelpText("Place panels", nullptr,
                 "Click the body once for each panel: it sits a little in front of the click, facing you, its top "
                 "up the screen. Place each panel slightly in front of the body at its widest point, from the view "
                 "that faces it: a front panel on the front of the chest, the back from behind, a sleeve on the upper "
                 "arm from its side. Cloth dropped from above settles behind overhangs like a chin or a beard.",
                 kPath);
    if (garmentPlacing_ >= 0 && garmentPlacing_ < int(pat.panels.size())) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(ImGui::GetStyleColorVec4(ImGuiCol_PlotHistogram), tr("Click the body for %s."),
                           pat.panels[size_t(garmentPlacing_)].name.c_str());
        ImGui::TextDisabled("%s", tr(kHint));
        ImGui::PopTextWrapPos();
    }
    // The panels: placed or not; a click picks one to place again (and to adjust).
    for (size_t i = 0; i < pat.panels.size(); ++i) {
        const Panel& p = pat.panels[i];
        ImGui::PushID(int(i));
        char label[160];
        std::snprintf(label, sizeof label, "%s %s", p.place.placed ? "\xe2\x9c\x93" : "  ", p.name.c_str());
        const bool chosen = int(i) == garmentPlacing_ || (garmentPlacing_ < 0 && int(i) == garmentEditPanel_);
        if (ImGui::Selectable(label, chosen)) {
            garmentPlacing_ = int(i);
            garmentEditPanel_ = int(i);
            garmentEditing_ = false;
            garmentEdit_ = p.place;
            status_ = "Click the body where " + p.name + " goes. " + kHint;
        }
        ImGui::PopID();
    }
    // The chosen placed panel's Turn, Wrap and Gap (the preview follows the slider; let go to keep it).
    if (garmentEditPanel_ >= 0 && garmentEditPanel_ < int(pat.panels.size()) && pat.panels[size_t(garmentEditPanel_)].place.placed) {
        if (!garmentEditing_) garmentEdit_ = pat.panels[size_t(garmentEditPanel_)].place;
        ImGui::TextDisabled(tr("%s on the body:"), pat.panels[size_t(garmentEditPanel_)].name.c_str());
        bool changed = false, done = false;
        auto slider = [&](const char* id, float* v, float lo, float hi, const char* format, const char* title, const char* help) {
            ImGui::SetNextItemWidth(-1);
            changed |= ui::SliderInside(id, v, lo, hi, format);
            done |= ImGui::IsItemDeactivatedAfterEdit();
            ui::HelpText(title, nullptr, help, kPath);
        };
        slider("##garmentturn", &garmentEdit_.turnDegrees, -180.0f, 180.0f, "Turn %.0f\xc2\xb0", "Turn",
               "Turns the panel in the view's plane: a sleeve's top toward the shoulder.");
        slider("##garmentwrap", &garmentEdit_.wrapDegrees, 0.0f, 360.0f, "Wrap %.0f\xc2\xb0", "Wrap",
               "Curls the panel back round the body: 0 flat, about 300 for a sleeve or a trouser leg (a panel sewn to "
               "itself starts at 300).");
        slider("##garmentgap", &garmentEdit_.gapMm, 0.0f, 200.0f, "Gap %.0f mm", "Gap",
               "How far in front of the click the panel starts. Keep it clear of the body.");
        if (changed) {
            garmentEditing_ = true;
            garmentPreviewDirty_ = true;
        }
        if (done) {
            garmentEditing_ = false;
            runCommand("garment_place", {{"panel", std::to_string(garmentEditPanel_ + 1)},
                                         {"turn", double(garmentEdit_.turnDegrees)},
                                         {"wrap", double(garmentEdit_.wrapDegrees)},
                                         {"gap_mm", double(garmentEdit_.gapMm)}});
        }
    }
    if (placedCount(pat) > 0) {
        float units = patternScale(pat);
        ImGui::SetNextItemWidth(-1);
        ui::SliderInside("##garmentunits", &units, 0.01f, 1000.0f, "Units per metre %.2f", ImGuiSliderFlags_Logarithmic);
        if (ImGui::IsItemDeactivatedAfterEdit())
            runCommand("garment_place", {{"panel", std::to_string(garmentEditPanel_ >= 0 ? garmentEditPanel_ + 1 : 1)},
                                         {"units_per_metre", double(units)}});
        ui::HelpText("Units per metre", nullptr,
                     "How big the body is in real life: 100 when it is modelled in centimetres (the starter meshes), 1 in "
                     "metres. The pattern is at real size, so this sets how big the garment is on the body.",
                     kPath);
    }
    ImGui::EndDisabled();
    const int placed = placedCount(pat);
    const bool all = placed == int(pat.panels.size());
    if (!running) {
        ImGui::BeginDisabled(!all);
        if (ImGui::Button(tr("Drape##garment"), ImVec2(w, 0))) startGarmentDrape();
        ImGui::EndDisabled();
        ui::HelpText("Drape", nullptr,
                     "Sews the placed panels together and lets the garment settle on the visible Parts, live. Place every "
                     "panel first.",
                     kPath);
        if (!all) ImGui::TextDisabled(tr("%d of %d panels placed"), placed, int(pat.panels.size()));
    } else {
        const float third = (w - ImGui::GetStyle().ItemSpacing.x * 2.0f) / 3.0f;
        const bool paused = garmentDrape_->paused.load();
        if (ImGui::Button(paused ? tr("Resume##garment") : tr("Pause##garment"), ImVec2(third, 0))) garmentDrape_->paused = !paused;
        ImGui::SameLine();
        if (ImGui::Button(tr("Stop##garment"), ImVec2(third, 0))) finishGarmentDrape();
        ImGui::SameLine();
        if (ImGui::Button(tr("Reset##garment"), ImVec2(third, 0))) {
            garmentDrape_.reset();
            garmentPreviewRefresh();
        }
    }
    if (!garmentDrapeResult_.empty() && !running) {
        if (ImGui::Button(tr("Convert to Part##garment"), ImVec2(w, 0))) convertGarment();
        ui::HelpText("Convert to Part", nullptr,
                     "Keeps the draped garment as a Part to sculpt: seams welded, UVs from the flat pattern, the pattern's "
                     "thickness. One undo step.",
                     kPath);
    }
}

double App::garmentDrapeTestValue(const std::string& name) {
    Pattern pat;
    const bool have = documentPattern(doc_, pat);
    if (name == "garment_placed") return have ? placedCount(pat) : 0;
    if (name == "garment_placing") return garmentPlacing_;
    if (name == "garment_drape_running") return garmentDrape_ ? 1 : 0;
    if (name == "garment_draped") return garmentDrapeResult_.empty() ? 0 : 1;
    if (name == "garment_preview") return garmentPreview_ && doc_.partIndex(garmentPreview_) >= 0 ? 1 : 0;
    if (name == "garment_units") return have ? patternScale(pat) : 0;
    return std::numeric_limits<double>::quiet_NaN();
}

} // namespace sf
