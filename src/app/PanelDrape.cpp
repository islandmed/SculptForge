// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Tool > Drape (core/Drape.h): the active Part simulated as cloth on a worker, shown live; at rest (or Keep) the
// result is one undo step, Reset puts the Part back. A stroke or a command while it runs keeps where it got to.
#include "app/App.h"
#include "app/Palettes.h"
#include "app/Ui.h"
#include "core/Units.h"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace sf {

struct App::DrapeJob {
    uint32_t part = 0;
    size_t vertices = 0;
    std::vector<glm::vec3> start; // the Part's positions before (world)
    DrapeScene scene;
    ClothSim sim;
    std::thread worker;
    std::atomic<bool> stop{false}, paused{false}, done{false};
    std::mutex lock;
    std::vector<glm::vec3> latest; // metres, the worker's last frame
    int frame = 0, shownFrame = -1;
    bool rested = false;
    float speed = 0.0f;
    std::chrono::steady_clock::time_point began = std::chrono::steady_clock::now();
    ~DrapeJob() {
        stop = true;
        if (worker.joinable()) worker.join();
    }
};

void App::startDrape() {
    if (drape_) return;
    if (doc_.sculptor.strokeActive()) endStroke();
    auto job = std::make_shared<DrapeJob>();
    std::string err;
    if (!drapeScene(doc_, drapeRequest_, job->scene, err)) {
        status_ = err;
        return;
    }
    job->part = doc_.activePartInfo().id;
    job->vertices = doc_.mesh.vertexCount();
    job->start = doc_.mesh.positions;
    if (!job->sim.init(job->scene.setup, job->scene.fabric)) {
        status_ = "Drape: the Part can't be simulated";
        return;
    }
    job->sim.setColliders(&job->scene.colliders);
    DrapeJob* j = job.get();
    const int maxFrames = drapeRequest_.maxFrames;
    job->worker = std::thread([j, maxFrames] {
        while (!j->stop.load()) {
            if (j->paused.load()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(15));
                continue;
            }
            const bool moving = j->sim.step();
            std::lock_guard g(j->lock);
            j->latest = j->sim.positions();
            ++j->frame;
            j->speed = j->sim.maxSpeed();
            if (!moving || j->frame >= maxFrames) {
                j->rested = !moving;
                break;
            }
        }
        j->done = true;
        wakeMainLoop();
    });
    drape_ = std::move(job);
    status_ = "Drape: simulating";
}

// Shows the worker's latest frame on the Part (no undo: Keep or rest records one step).
void App::pollDrape() {
    pollGarmentDrape(); // (Garments > Drape on the body, PanelGarmentDrape.cpp)
    if (!drape_) return;
    DrapeJob& j = *drape_;
    const bool here = doc_.activePartInfo().id == j.part && doc_.mesh.vertexCount() == j.vertices;
    if (!here) { // (the Part changed under it: drop the simulation, the Part keeps what it showed)
        drape_.reset();
        return;
    }
    {
        std::lock_guard g(j.lock);
        if (j.frame != j.shownFrame && j.latest.size() == j.vertices) {
            const float scale = j.scene.scale;
            for (size_t v = 0; v < j.vertices; ++v) doc_.mesh.positions[v] = j.latest[v] * scale;
            j.shownFrame = j.frame;
            doc_.mesh.recomputeAllNormals();
            doc_.mesh.markAllDirty();
            doc_.bvh.refitAll(doc_.mesh);
        }
    }
    char line[128];
    std::snprintf(line, sizeof line, "Drape: frame %d, %.1f s%s", j.frame,
                  std::chrono::duration<double>(std::chrono::steady_clock::now() - j.began).count(),
                  j.paused ? " (paused)" : "");
    status_ = line;
    if (j.done.load()) finishDrape(true);
    else wakeMainLoop(); // (keep drawing while it moves)
}

void App::finishDrape(bool keep) {
    if (!drape_) return;
    std::shared_ptr<DrapeJob> job = std::move(drape_);
    job->stop = true;
    if (job->worker.joinable()) job->worker.join();
    const bool here = doc_.activePartInfo().id == job->part && doc_.mesh.vertexCount() == job->vertices;
    if (!here) return;
    std::vector<glm::vec3> result;
    {
        std::lock_guard g(job->lock);
        result = job->latest.empty() ? job->scene.setup.positions : job->latest;
    }
    // The Part back to where it started, then the result as one step (or nothing: Reset).
    doc_.mesh.positions = job->start;
    doc_.mesh.recomputeAllNormals();
    doc_.mesh.markAllDirty();
    doc_.bvh.refitAll(doc_.mesh);
    if (!keep) {
        status_ = "Drape: reset";
        return;
    }
    std::string err;
    const size_t moved = applyDrape(doc_, result, job->scene.scale, err);
    char msg[160];
    std::snprintf(msg, sizeof msg, "Drape: %s after %d frames (%zu vertices; Ctrl+Z undoes it)",
                  job->rested ? "at rest" : "stopped", job->frame, moved);
    status_ = moved ? std::string(msg) : err;
}

void App::drawDrapeSection() {
    const char* help = "Tool > Drape";
    const bool running = drape_ != nullptr;
    ImGui::BeginDisabled(running);
    ImGui::SetNextItemWidth(-1);
    int fabric = int(drapeRequest_.fabric);
    const char* fabrics[] = {tr("Silk"), tr("Cotton"), tr("Denim"), tr("Leather")};
    if (ImGui::Combo("##drapefabric", &fabric, fabrics, int(FabricPreset::Count))) drapeRequest_.fabric = FabricPreset(fabric);
    ui::HelpText("Fabric", nullptr,
                 "How the cloth hangs: Silk flows into many fine folds, Cotton medium ones, Denim holds broad folds, "
                 "Leather barely folds.",
                 help);
    if (!drapeScaleTouched_ && !running && drapeScalePart_ != doc_.activePartInfo().id) {
        drapeScalePart_ = doc_.activePartInfo().id;
        drapeRequest_.scale = sceneUnitsPerMetre(doc_);
    }
    ImGui::SetNextItemWidth(-1);
    if (ui::SliderInside("##drapescale", &drapeRequest_.scale, 0.01f, 1000.0f, "Units per metre %.2f", ImGuiSliderFlags_Logarithmic))
        drapeScaleTouched_ = true;
    ui::HelpText("Units per metre", nullptr,
                 "How big the sculpt is in real life: 1 when it is modelled at real size (a SculptForge unit is a "
                 "metre). A body 2 units tall that should be a 1.8 m person: 1.11. Cloth drapes by real size. The value was guessed "
                 "from the size of the scene (centimetres above 20 units, millimetres above 600); move it to set it yourself.",
                 help);
    ImGui::Checkbox(tr("Pins from mask"), &drapeRequest_.useMask);
    ui::HelpText("Pins from mask", nullptr, "Masked areas hold still, like pins (a waistband, a collar).", help);
    ImGui::Checkbox(tr("Collide with other Parts##drape"), &drapeRequest_.collide);
    ui::HelpText("Collide with other Parts", nullptr,
                 "The cloth falls onto the other visible Parts and rests on them (a shirt on a body).", help);
    ImGui::EndDisabled();
    const float w = ImGui::GetContentRegionAvail().x;
    if (!running) {
        if (ImGui::Button(tr("Simulate##drape"), ImVec2(w, 0))) startDrape();
        ui::HelpText("Simulate", nullptr,
                     "Drops the active Part as cloth under gravity until it rests on the other Parts. It shows live; at rest "
                     "the result is kept as one undo step.",
                     help);
    } else {
        const float half = (w - ImGui::GetStyle().ItemSpacing.x * 2.0f) / 3.0f;
        const bool paused = drape_->paused.load();
        if (ImGui::Button(paused ? tr("Resume##drape") : tr("Pause##drape"), ImVec2(half, 0))) drape_->paused = !paused;
        ImGui::SameLine();
        if (ImGui::Button(tr("Keep##drape"), ImVec2(half, 0))) finishDrape(true);
        ImGui::SameLine();
        if (ImGui::Button(tr("Reset##drape"), ImVec2(half, 0))) finishDrape(false);
    }
}

} // namespace sf
