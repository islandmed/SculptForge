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
    ClothSettings settings; // the wind, changed while it runs (under lock; the worker applies it at its next frame)
    bool settingsChanged = false;
    // In wind the drape is watched, not waited for: cloth time at most as fast as real time (a flap a second, not
    // five); in gusts the cloth never rests, so it runs on (a minute of cloth time) for Keep to take the frame you like.
    bool watch = false;
    int baseFrames = 600, cap = 600;
    std::chrono::steady_clock::time_point began = std::chrono::steady_clock::now();
    void setSettings(const ClothSettings& s) {
        std::lock_guard g(lock);
        settings = s;
        settingsChanged = true;
        watch = glm::dot(s.wind, s.wind) > 0.0f;
        cap = watch && s.turbulence > 0.0f ? std::max(baseFrames, 3600) : baseFrames;
    }
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
    if (!job->sim.init(job->scene.setup, job->scene.fabric, drapeSettings(drapeRequest_, job->scene.meanEdge))) {
        status_ = "Drape: the Part can't be simulated";
        return;
    }
    job->sim.setColliders(&job->scene.colliders);
    job->baseFrames = job->cap = drapeRequest_.maxFrames;
    job->setSettings(drapeSettings(drapeRequest_, job->scene.meanEdge)); // (pacing and cap for this wind; applied at frame 0, harmless)
    DrapeJob* j = job.get();
    job->worker = std::thread([j] {
        auto due = std::chrono::steady_clock::now();
        while (!j->stop.load()) {
            if (j->paused.load()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(15));
                continue;
            }
            bool watch;
            {
                std::lock_guard g(j->lock);
                if (j->settingsChanged) j->sim.setSettings(j->settings), j->settingsChanged = false;
                watch = j->watch;
            }
            const bool moving = j->sim.step();
            {
                std::lock_guard g(j->lock);
                j->latest = j->sim.positions();
                ++j->frame;
                j->speed = j->sim.maxSpeed();
                if (!moving || j->frame >= j->cap) {
                    j->rested = !moving;
                    break;
                }
            }
            if (watch) { // (wind: real time, see DrapeJob::watch)
                due = std::max(due, std::chrono::steady_clock::now()) + std::chrono::microseconds(16667);
                std::this_thread::sleep_until(due);
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

int App::drapeFrame() const {
    if (!drape_) return 0;
    std::lock_guard g(drape_->lock);
    return drape_->frame;
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
    // Wind: live, also while the simulation runs (the worker takes the new settings at its next frame).
    bool windChanged = false;
    ImGui::SetNextItemWidth(-1);
    windChanged |= ui::SliderInside("##drapewind", &drapeRequest_.windSpeed, 0.0f, 20.0f, "Wind %.1f m/s");
    ui::HelpText("Wind", nullptr,
                 "How hard the air blows on the cloth, in metres per second: 0 none, 3 a breeze, 8 makes flags fly, 15 is hard to "
                 "walk in. It pushes on the cloth's face and skims along it. Change it while the simulation runs, too.",
                 help);
    if (drapeRequest_.windSpeed > 0.0f) {
        const float button = ImGui::CalcTextSize(tr("From view")).x + ImGui::GetStyle().FramePadding.x * 2.0f;
        ImGui::SetNextItemWidth(w - button - ImGui::GetStyle().ItemSpacing.x);
        windChanged |= ui::SliderInside("##drapeheading", &drapeRequest_.windHeading, 0.0f, 360.0f, "Toward %.0f deg");
        ui::HelpText("Toward", nullptr,
                     "Where the wind blows toward, as a heading: 0 is away from the front view, 90 to the right, 180 toward it.", help);
        ImGui::SameLine();
        if (ImGui::Button(tr("From view##drapewind"))) {
            const glm::vec3 f = camera_.forward();
            float heading = glm::degrees(std::atan2(f.x, -f.z));
            if (heading < 0.0f) heading += 360.0f;
            drapeRequest_.windHeading = heading;
            drapeRequest_.windLift = std::clamp(glm::degrees(std::asin(std::clamp(f.y, -1.0f, 1.0f))), -60.0f, 60.0f);
            windChanged = true;
        }
        ui::HelpText("From view", nullptr, "Points the wind away from you: it blows from the camera toward the model (heading and lift).", help);
        ImGui::SetNextItemWidth(-1);
        windChanged |= ui::SliderInside("##drapelift", &drapeRequest_.windLift, -60.0f, 60.0f, "Lift %.0f deg");
        ui::HelpText("Lift", nullptr, "Tilts the wind up from level (a skirt lifts) or down, in degrees.", help);
        float gusts = drapeRequest_.gusts * 100.0f;
        ImGui::SetNextItemWidth(-1);
        if (ui::SliderInside("##drapegusts", &gusts, 0.0f, 100.0f, "Gusts %.0f%%")) {
            drapeRequest_.gusts = gusts / 100.0f;
            windChanged = true;
        }
        ui::HelpText("Gusts", nullptr,
                     "How much the wind varies from place to place and moment to moment: the cloth ripples and flaps and never "
                     "quite settles, so press Keep at the frame you like (it stops by itself after a minute).",
                     help);
    }
    if (windChanged && running) drape_->setSettings(drapeSettings(drapeRequest_, drape_->scene.meanEdge));
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
    drawClothSettingsLink("drape"); // (PanelCloth.cpp)
}

} // namespace sf
