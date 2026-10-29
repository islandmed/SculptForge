// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Splats > Make sculptable (core/Sculptable.h): one click from a Gaussian-splat capture to a clean, sculptable
// SubTool with the capture's colours. The options (Detail, polygon preset, Face loops, colour, crop box) sit in
// the Splats palette; the work runs on a worker thread with a progress bar and Cancel.
//
// With Review on, it stops after the scan: the face landmarks show as dots on the model with the guide loops
// QRemesh will follow. Drag a dot to move it (its mirror follows), or Place by hand when no face was found;
// Finish remeshes along the guides. The crop box is drawn in the viewport; drag its face handles. The console
// shows each run as splat_to_mesh / face_guides script lines.
#include "app/App.h"
#include "app/Demo.h"
#include "app/Palettes.h"
#include "app/Ui.h"

#include "core/Sculptable.h"

#include <IconsLucide.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <thread>

namespace sf {

namespace {

constexpr const char* kPath = "Splats > Make sculptable";

const char* const kPresetLabels[3] = {"Game", "Film", "Sculpt"};
const SculptablePreset kPresets[3] = {SculptablePreset::Game, SculptablePreset::Film, SculptablePreset::SculptBase};

// What to click next when placing landmarks by hand (index order of FaceLandmarks::point).
const char* const kLandmarkPrompts[FaceLandmarks::kCount] = {
    "the outer corner of the eye on the left of the screen", "the inner corner of that eye",
    "the inner corner of the other eye",                      "its outer corner",
    "the tip of the nose",                                    "the mouth corner on the left",
    "the other mouth corner",                                 "the chin"};

std::string vecText(const glm::vec3& v) {
    char buf[96];
    std::snprintf(buf, sizeof buf, "%.4g %.4g %.4g", double(v.x), double(v.y), double(v.z));
    return buf;
}

// The nearest world axis when within ~40 degrees, so a roughly front view gives an exact front.
glm::vec3 snapAxis(const glm::vec3& v) {
    const glm::vec3 a = glm::abs(v);
    const int k = a.x >= a.y && a.x >= a.z ? 0 : a.y >= a.z ? 1 : 2;
    if (a[k] < 0.76f) return glm::normalize(v);
    glm::vec3 out(0.0f);
    out[k] = v[k] > 0.0f ? 1.0f : -1.0f;
    return out;
}

} // namespace

struct SculptableUiState {
    // Options (the Splats palette).
    int detail = 256;
    int preset = 0; // kPresets
    bool faceLoops = true;
    bool review = false;
    int color = 0; // 0 polypaint, 1 texture
    bool keepScan = true;
    bool crop = false;
    AABB box;
    float minOpacity = 0.2f;
    int close = 2;
    bool fillHoles = true;
    bool keepParts = false;

    // The running job.
    struct Job {
        int stage = 0; // 0 all, 1 scan, 2 finish
        std::future<bool> done;
        std::shared_ptr<std::atomic<float>> progress;
        std::shared_ptr<std::atomic<bool>> cancel;
        std::shared_ptr<SculptableResult> result;
        std::shared_ptr<std::string> error;
        Args args;
        uint32_t scanId = 0;
        size_t scanTriangles = 0;
    };
    std::unique_ptr<Job> job;

    // Review: the scan SubTool, its landmarks and the frame they were found in.
    bool reviewing = false;
    uint32_t scanId = 0;
    FaceLandmarks landmarks;
    FaceFrame frame;
    int placing = -1;     // placing by hand: the landmark the next click sets (-1: not placing)
    int dragLandmark = -1;
    int dragHandle = -1;  // crop box face handle: axis * 2 + (0 min, 1 max)
    ImVec2 dragStart{0, 0};
    AABB dragBox;
};

bool App::sculptableBusy() const { return sculptable_ && sculptable_->job; }

void App::registerSculptableUiActions() {
    if (!sculptable_) sculptable_ = std::make_shared<SculptableUiState>();
    auto add = [this](const char* id, const char* label, std::function<void()> run, std::function<bool()> enabled,
                      std::function<bool()> checked, const char* description, const char* command) {
        Action a;
        a.id = id;
        a.label = label;
        a.palette = kPath;
        a.run = std::move(run);
        a.enabled = std::move(enabled);
        a.checked = std::move(checked);
        a.description = description;
        if (command) a.command = command;
        actions_.add(std::move(a));
    };
    add("splats.make_sculptable", "Make sculptable", [this] { startSculptable(sculptable_->review ? 1 : 0); },
        [this] { return !doc_.splats.empty() && !sculptableBusy(); }, nullptr,
        "Turn the capture into a clean, sculptable SubTool with its colours, in one go: a watertight surface (floaters "
        "dropped, holes filled), quad topology at the chosen polygon budget with edge loops round the eyes and mouth "
        "when it is a face, and polypaint or a texture. The splats stay (hidden). Runs in the background; Cancel stops it.",
        "splat_to_mesh");
    add("splats.sculptable_finish", "Finish", [this] { startSculptable(2); },
        [this] { return sculptable_->reviewing && !sculptableBusy(); }, nullptr,
        "QRemesh the scan along the face guides you checked, then colour it: the sculptable SubTool appears next to "
        "the scan (which is hidden).",
        nullptr);
    add("splats.sculptable_place", "Place by hand",
        [this] {
            sculptable_->placing = 0;
            status_ = std::string("Click ") + kLandmarkPrompts[0];
        },
        [this] { return sculptable_->reviewing && !sculptableBusy(); }, [this] { return sculptable_->placing >= 0; },
        "Place the face landmarks yourself, one click each: the eye corners, the nose tip, the mouth corners and the "
        "chin (the panel says which is next). For faces the automatic search misses.",
        nullptr);
    add("splats.sculptable_crop", "Crop Box", [this] { setSculptableCrop(!sculptable_->crop); },
        [this] { return !doc_.splats.empty(); }, [this] { return sculptable_->crop; },
        "Only the splats inside the box count: drag the box's face handles in the viewport to cut away the ground, "
        "the background or a stand.",
        nullptr);
}

void App::setSculptableCrop(bool on) {
    SculptableUiState& s = *sculptable_;
    s.crop = on && !doc_.splats.empty();
    if (s.crop && !s.box.valid()) {
        s.box = doc_.splats.bounds(0.98f);
        const glm::vec3 pad = s.box.extent() * 0.05f;
        s.box.min -= pad;
        s.box.max += pad;
    }
    status_ = s.crop ? "Crop Box: drag its face handles; only splats inside count" : "Crop Box off";
}

Args App::sculptableArgs(int stage) const {
    const SculptableUiState& s = *sculptable_;
    Args a{{"detail", double(s.detail)}, {"preset", std::string(sculptablePresetName(kPresets[s.preset]))}};
    if (!s.faceLoops) a["face_guides"] = std::string("off");
    else {
        // The face is looked for from the current view (snapped to the nearest axis view).
        const glm::vec3 front = snapAxis(-camera_.forward());
        glm::vec3 up = snapAxis(camera_.orientation() * glm::vec3(0, 1, 0));
        if (std::abs(glm::dot(front, up)) > 0.5f) up = std::abs(front.y) > 0.9f ? glm::vec3(0, 0, -1) : glm::vec3(0, 1, 0);
        if (front != glm::vec3(0, 0, 1)) a["front"] = vecText(front);
        if (up != glm::vec3(0, 1, 0)) a["up"] = vecText(up);
    }
    if (s.color == 1) a["color"] = std::string("texture");
    if (!s.keepScan) a["keep_scan"] = false;
    if (s.crop && s.box.valid()) a["crop"] = vecText(s.box.min) + " " + vecText(s.box.max);
    if (std::abs(s.minOpacity - 0.2f) > 1e-4f) a["min_opacity"] = double(s.minOpacity);
    if (s.close != 2) a["close"] = double(s.close);
    if (!s.fillHoles) a["fill_holes"] = false;
    if (s.keepParts) a["keep_parts"] = true;
    if (stage == 1) a["stage"] = std::string("scan");
    if (stage == 2) a["stage"] = std::string("finish");
    return a;
}

void App::startSculptable(int stage) {
    SculptableUiState& s = *sculptable_;
    if (s.job) return;
    if (doc_.sculptor.strokeActive()) endStroke();
    Args args = sculptableArgs(stage);
    if (stage == 2) { // the frame the face was found in, whatever the view is now
        args.erase("front");
        args.erase("up");
        if (s.frame.front != glm::vec3(0, 0, 1)) args["front"] = vecText(s.frame.front);
        if (s.frame.up != glm::vec3(0, 1, 0)) args["up"] = vecText(s.frame.up);
    }
    SculptableOptions o;
    std::string err;
    if (!sculptableOptionsFromArgs(args, o, err)) {
        status_ = err;
        return;
    }
    auto job = std::make_unique<SculptableUiState::Job>();
    job->stage = stage;
    job->progress = std::make_shared<std::atomic<float>>(0.0f);
    job->cancel = std::make_shared<std::atomic<bool>>(false);
    job->result = std::make_shared<SculptableResult>();
    job->error = std::make_shared<std::string>();
    if (stage == 2) {
        // Remesh the scan SubTool under review (its guides as they are now).
        int index = -1;
        for (int i = 0; i < doc_.subtoolCount(); ++i)
            if (doc_.subtools[size_t(i)].id == s.scanId) index = i;
        if (index < 0) {
            status_ = "The scan SubTool is gone; Make sculptable again";
            s.reviewing = false;
            return;
        }
        if (index != doc_.activeSubtool) doc_.selectSubtool(index);
        job->result->scan = doc_.mesh;
        job->result->guides = doc_.qremeshGuides;
        job->result->landmarks = s.landmarks;
        const AABB b = doc_.mesh.bounds();
        job->result->surface.cell = std::max({b.extent().x, b.extent().y, b.extent().z}) / float(std::max(o.surface.resolution, 32));
        job->scanId = s.scanId;
        job->scanTriangles = doc_.mesh.triangleCount();
        s.placing = -1;
        // The console line carries the landmarks, so the script repeats what was reviewed.
        if (s.landmarks.valid) args["landmarks"] = faceLandmarksJson(s.landmarks);
    } else if (doc_.splats.empty()) {
        status_ = "No splats loaded";
        return;
    }
    job->args = args;
    if (stage != 2) s.frame = o.frame;
    std::promise<bool> promise;
    job->done = promise.get_future();
    std::thread([promise = std::move(promise), cloud = std::make_shared<SplatCloud>(doc_.splats), o, stage,
                 progress = job->progress, cancel = job->cancel, result = job->result, error = job->error]() mutable {
        auto report = [&](float a, float b) {
            return SculptableProgress([&, a, b](float f) {
                progress->store(a + (b - a) * f);
                return !cancel->load();
            });
        };
        bool ok = true;
        if (stage == 0) ok = makeSculptableScan(*cloud, o, *result, *error, report(0.0f, 0.45f)) &&
                             finishSculptable(*cloud, o, *result, *error, report(0.45f, 1.0f));
        else if (stage == 1) ok = makeSculptableScan(*cloud, o, *result, *error, report(0.0f, 1.0f));
        else ok = finishSculptable(*cloud, o, *result, *error, report(0.0f, 1.0f));
        if (cancel->load()) {
            ok = false;
            *error = "Make sculptable cancelled";
        }
        promise.set_value(ok);
    }).detach();
    s.job = std::move(job);
    status_ = stage == 2 ? "Make sculptable: remeshing along the face guides..." : "Make sculptable: building the surface...";
}

void App::pollSculptable() {
    if (!sculptable_ || !sculptable_->job) return;
    SculptableUiState& s = *sculptable_;
    if (s.job->done.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
        status_ = "Make sculptable: " + std::to_string(int(s.job->progress->load() * 100.0f)) + "%";
        return;
    }
    const std::unique_ptr<SculptableUiState::Job> job = std::move(s.job);
    if (!job->done.get()) {
        status_ = *job->error;
        lastCommandOk_ = false;
        return;
    }
    if (doc_.sculptor.strokeActive()) endStroke();
    CommandResult r{true, {}};
    SculptableResult& res = *job->result;
    if (job->stage == 0) {
        r.message = applySculptable(doc_, res, s.keepScan);
        s.reviewing = false;
    } else if (job->stage == 1) {
        s.landmarks = res.landmarks;
        r.message = applySculptableScan(doc_, res);
        s.reviewing = true;
        s.scanId = doc_.activeSubtoolInfo().id;
        if (!s.landmarks.valid && s.faceLoops) {
            s.placing = 0;
            r.message += ". Place the landmarks by hand: click " + std::string(kLandmarkPrompts[0]);
        }
    } else {
        int index = -1;
        for (int i = 0; i < doc_.subtoolCount(); ++i)
            if (doc_.subtools[size_t(i)].id == job->scanId) index = i;
        if (index < 0 || doc_.subtoolMesh(index).triangleCount() != job->scanTriangles) {
            status_ = "Make sculptable skipped: the scan SubTool changed meanwhile";
            return;
        }
        r.message = applySculptableFinish(doc_, res, index, s.keepScan);
        s.reviewing = false;
    }
    renderSettings_.showSplats = false; // the splats stay, hidden (Splats > Show brings them back)
    status_ = r.message;
    lastCommandOk_ = true;
    clearLods();
    playingLayer_ = -1;
    if (!onScreen(doc_.mesh.bounds())) camera_.frame(sceneBounds());
    updateTitle();
    consoleEcho("splat_to_mesh", job->args, r);
}

void App::drawSculptableSection() {
    if (!sculptable_) sculptable_ = std::make_shared<SculptableUiState>();
    SculptableUiState& s = *sculptable_;
    const ImGuiStyle& style = ImGui::GetStyle();
    const float x0 = ImGui::GetCursorPosX(); // line start, inside the section indent
    tray::GroupLabel(*this, "From scan");
    Demo::pushScope("Make sculptable"); // UI tests: "Make sculptable/<label>"
    struct PopScope {
        ~PopScope() { Demo::popScope(); }
    } popScope;
    const float full = ImGui::GetContentRegionAvail().x;
    const float half = (full - style.ItemSpacing.x) * 0.5f;
    const float third = (full - 2.0f * style.ItemSpacing.x) / 3.0f;

    if (s.job) {
        ImGui::ProgressBar(s.job->progress->load(), ImVec2(half, 0), s.job->stage == 2 ? "Remeshing..." : "Working...");
        ui::HelpText("Make sculptable", nullptr, "Working in the background; the new SubTool appears when it's done.", kPath);
        ImGui::SameLine();
        if (ImGui::Button("Cancel##sculptable", ImVec2(-1, 0))) s.job->cancel->store(true);
        ui::HelpText("Cancel", nullptr, "Stop Make sculptable; nothing changes.", kPath);
    } else if (s.reviewing) {
        ui::ActionButton(*this, "splats.sculptable_finish", ImVec2(half, 0));
        ImGui::SameLine();
        if (ImGui::Button("Back##sculptable", ImVec2(-1, 0))) {
            s.reviewing = false;
            s.placing = -1;
            status_ = "Review closed; the scan SubTool stays";
        }
        ui::HelpText("Back", nullptr, "Leave the review without remeshing (the scan SubTool stays as it is).", kPath);
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        if (s.placing >= 0) ImGui::TextWrapped("Click %s.", kLandmarkPrompts[s.placing]);
        else if (s.landmarks.valid) ImGui::TextWrapped("Drag the dots to move the landmarks (the other side follows). The lines are the loops QRemesh will follow.");
        else ImGui::TextWrapped("No face found: Place by hand, or Finish without face loops.");
        ImGui::PopStyleColor();
        ui::ActionButton(*this, "splats.sculptable_place", ImVec2(half, 0));
        ImGui::SameLine();
        if (ImGui::Button("Find again##sculptable", ImVec2(-1, 0))) {
            Args a{{"target", double(sculptablePresetQuads(kPresets[s.preset]))}};
            const Args view = sculptableArgs(0);
            for (const char* k : {"front", "up"})
                if (view.contains(k)) a[k] = view.at(k);
            runSculptableGuides(a);
        }
        ui::HelpText("Find again", nullptr, "Look for the face again from the current view (turn the view to face it first).", kPath);
    } else {
        ImGui::BeginDisabled(doc_.splats.empty());
        ui::ActionButton(*this, "splats.make_sculptable", ImVec2(-1, 0));
        ImGui::EndDisabled();
    }

    ImGui::BeginDisabled(s.job != nullptr || s.reviewing);
    ImGui::SetNextItemWidth(-1);
    tray::SliderInt("##sculptdetail", &s.detail, 96, 512, "Detail %d");
    ui::HelpText("Detail", nullptr,
                 "How finely the surface follows the capture: voxels across the subject. Higher keeps thinner parts and "
                 "more detail but takes longer (about 5 s at 384 for a million splats).",
                 kPath);
    for (int i = 0; i < 3; ++i) {
        if (i) ImGui::SameLine();
        const bool on = s.preset == i;
        if (on) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        if (ImGui::Button((std::string(kPresetLabels[i]) + "##sculptpreset").c_str(), ImVec2(i == 2 ? -1.0f : third, 0))) s.preset = i;
        if (on) ImGui::PopStyleColor();
        static const char* help[3] = {
            "Game: about 4,000 quads, light enough for real-time characters and props.",
            "Film: about 12,000 quads, for close-ups and smooth deformation.",
            "Sculpt: a 2,000-quad base with 2 subdivision levels, the scan's detail projected onto the top level, "
            "ready to sculpt with levels."};
        ui::HelpText(kPresetLabels[i], nullptr, help[i], kPath);
    }
    ImGui::Checkbox("Face loops", &s.faceLoops);
    ui::HelpText("Face loops", nullptr,
                 "When the capture is a head, find the eyes, nose, mouth and chin as seen from the current view and "
                 "give QRemesh edge loops round the eyes and mouth and along the nasolabial folds, mirrored across "
                 "the face (animation topology). Turn the view to face it first.",
                 kPath);
    ImGui::SameLine();
    ImGui::SetCursorPosX(x0 + half + style.ItemSpacing.x); // the right column, inside the section indent
    ImGui::BeginDisabled(!s.faceLoops);
    ImGui::Checkbox("Review##sculptable", &s.review);
    ImGui::EndDisabled();
    ui::HelpText("Review", nullptr,
                 "Stop after the scan to check the face landmarks and the loops: drag a dot to move it, or place them "
                 "by hand, then Finish.",
                 kPath);
    ImGui::RadioButton("Polypaint##sculptcolor", &s.color, 0);
    ui::HelpText("Polypaint", nullptr, "The capture's colours go into the vertices (polypaint): more polygons, more colour detail.", kPath);
    ImGui::SameLine();
    ImGui::SetCursorPosX(x0 + half + style.ItemSpacing.x); // the right column, inside the section indent
    ImGui::RadioButton("Texture##sculptcolor", &s.color, 1);
    ui::HelpText("Texture", nullptr,
                 "Auto UV the result and bake the capture's colours into a 2K texture (sharp colour on a light mesh, "
                 "for games and exports).",
                 kPath);
    ImGui::Checkbox("Keep scan", &s.keepScan);
    ui::HelpText("Keep scan", nullptr,
                 "Keep the dense voxel surface as a hidden \"<name> Scan\" SubTool, to ProjectAll more detail from later.",
                 kPath);
    ImGui::SameLine();
    ImGui::SetCursorPosX(x0 + half + style.ItemSpacing.x); // the right column, inside the section indent
    ui::ActionButton(*this, "splats.sculptable_crop", ImVec2(-1, 0));
    if (ImGui::TreeNode("More##sculptable")) {
        ImGui::SetNextItemWidth(-1);
        ui::SliderInside("##sculptopacity", &s.minOpacity, 0.0f, 0.9f, "Min Opacity %.2f");
        ui::HelpText("Min Opacity", nullptr, "Splats fainter than this are ignored (haze and floaters are usually faint).", kPath);
        ImGui::SetNextItemWidth(-1);
        tray::SliderInt("##sculptclose", &s.close, 0, 8, "Close Gaps %d");
        ui::HelpText("Close Gaps", nullptr, "Seal gaps up to this many voxels between splats.", kPath);
        ImGui::Checkbox("Fill holes", &s.fillHoles);
        ui::HelpText("Fill holes", nullptr,
                     "Fill big holes such as the open bottom of a subject standing on a table (space boxed in on five "
                     "sides counts as inside). Turn off for open cavities like a cup.",
                     kPath);
        ImGui::SameLine();
        ImGui::SetCursorPosX(x0 + half + style.ItemSpacing.x); // the right column, inside the section indent
        ImGui::Checkbox("All pieces", &s.keepParts);
        ui::HelpText("All pieces", nullptr, "Keep every solid piece instead of only the largest (floaters come back too).", kPath);
        ImGui::TreePop();
    }
    ImGui::EndDisabled();
}

void App::runSculptableGuides(Args args) {
    runCommand("face_guides", args);
    const std::string& m = status_;
    const size_t at = m.find("landmarks {");
    if (lastCommandOk_ && at != std::string::npos) {
        std::string err;
        FaceLandmarks lm;
        if (faceLandmarksFromJson(m.substr(at + 10), lm, err)) sculptable_->landmarks = lm;
    }
    if (args.contains("front") || !args.contains("landmarks")) {
        FaceFrame f;
        glm::vec3 v;
        if (args.contains("front") && std::sscanf(std::get<std::string>(args.at("front")).c_str(), "%f %f %f", &v.x, &v.y, &v.z) == 3) f.front = v;
        if (args.contains("up") && std::sscanf(std::get<std::string>(args.at("up")).c_str(), "%f %f %f", &v.x, &v.y, &v.z) == 3) f.up = v;
        if (f.orthonormalize()) sculptable_->frame = f;
    }
}

namespace {

void boxCorners(const AABB& b, glm::vec3 out[8]) {
    for (int k = 0; k < 8; ++k) out[k] = glm::vec3(k & 1 ? b.max.x : b.min.x, k & 2 ? b.max.y : b.min.y, k & 4 ? b.max.z : b.min.z);
}

glm::vec3 faceCentre(const AABB& b, int handle) {
    glm::vec3 c = b.center();
    const int axis = handle / 2;
    c[axis] = handle % 2 ? b.max[axis] : b.min[axis];
    return c;
}

} // namespace

bool App::sculptableLandmarkScreen(int i, ImVec2& out) const {
    if (!sculptable_ || !sculptable_->reviewing || !sculptable_->landmarks.valid || i < 0 || i >= FaceLandmarks::kCount) return false;
    return gizmoProject(sculptable_->landmarks.point(i), out);
}

bool App::sculptableReviewActive() const {
    return sculptable_ && sculptable_->reviewing && doc_.activeSubtoolInfo().id == sculptable_->scanId;
}

bool App::sculptableHot() const {
    if (!sculptable_) return false;
    const SculptableUiState& s = *sculptable_;
    if (s.dragLandmark >= 0 || s.dragHandle >= 0 || (sculptableReviewActive() && s.placing >= 0)) return true;
    const float reach = 9.0f * uiScale_;
    ImVec2 q;
    if (sculptableReviewActive() && s.landmarks.valid)
        for (int i = 0; i < FaceLandmarks::kCount; ++i)
            if (gizmoProject(s.landmarks.point(i), q) && std::hypot(q.x - mouseX_, q.y - mouseY_) < reach) return true;
    if (s.crop && s.box.valid() && renderSettings_.showSplats && !doc_.splats.empty())
        for (int h = 0; h < 6; ++h)
            if (gizmoProject(faceCentre(s.box, h), q) && std::hypot(q.x - mouseX_, q.y - mouseY_) < reach) return true;
    return false;
}

void App::drawSculptableOverlay() {
    if (!sculptable_) return;
    SculptableUiState& s = *sculptable_;
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    const CanvasRect cr = canvasRect();
    dl->PushClipRect(ImVec2(cr.x, cr.y), ImVec2(cr.x + cr.w, cr.y + cr.h), true);
    const float u = uiScale_;
    if (s.crop && s.box.valid() && !doc_.splats.empty() && renderSettings_.showSplats) {
        glm::vec3 c[8];
        boxCorners(s.box, c);
        ImVec2 p[8];
        bool ok = true;
        for (int k = 0; k < 8; ++k) ok &= gizmoProject(c[k], p[k]);
        if (ok) {
            const ImU32 col = IM_COL32(255, 200, 80, 220);
            for (int k = 0; k < 8; ++k)
                for (int bit = 1; bit < 8; bit <<= 1)
                    if (!(k & bit)) dl->AddLine(p[k], p[k | bit], col, 1.5f * u);
            for (int h = 0; h < 6; ++h) {
                ImVec2 q;
                if (!gizmoProject(faceCentre(s.box, h), q)) continue;
                const bool hot = s.dragHandle == h || (s.dragHandle < 0 && std::hypot(q.x - mouseX_, q.y - mouseY_) < 9.0f * u);
                dl->AddCircleFilled(q, (hot ? 7.0f : 5.5f) * u, hot ? IM_COL32(255, 240, 160, 255) : col);
                dl->AddCircle(q, (hot ? 7.0f : 5.5f) * u, IM_COL32(40, 30, 10, 255), 16, 1.0f * u);
            }
        }
    }
    if (sculptableReviewActive() && s.landmarks.valid) {
        for (int i = 0; i < FaceLandmarks::kCount; ++i) {
            ImVec2 q;
            if (!gizmoProject(s.landmarks.point(i), q)) continue;
            const bool hot = s.dragLandmark == i || (s.dragLandmark < 0 && std::hypot(q.x - mouseX_, q.y - mouseY_) < 9.0f * u);
            dl->AddCircleFilled(q, (hot ? 7.0f : 5.0f) * u, hot ? IM_COL32(255, 255, 255, 255) : IM_COL32(90, 220, 255, 255));
            dl->AddCircle(q, (hot ? 7.0f : 5.0f) * u, IM_COL32(10, 30, 40, 255), 16, 1.0f * u);
        }
    }
    if (sculptableReviewActive() && s.placing >= 0) {
        // The landmarks placed so far, and the prompt by the cursor.
        for (int i = 0; i < s.placing; ++i) {
            ImVec2 q;
            if (gizmoProject(s.landmarks.point(i), q)) dl->AddCircleFilled(q, 5.0f * u, IM_COL32(90, 220, 255, 255));
        }
        if (cr.contains(mouseX_, mouseY_))
            dl->AddText(ImVec2(mouseX_ + 14.0f * u, mouseY_ + 10.0f * u), IM_COL32(255, 255, 255, 230), kLandmarkPrompts[s.placing]);
    }
    dl->PopClipRect();
}

bool App::sculptableMouseDown(float x, float y) {
    if (!sculptable_ || sculptable_->job) return false;
    SculptableUiState& s = *sculptable_;
    const float reach = 9.0f * uiScale_;
    if (sculptableReviewActive() && s.placing >= 0) {
        RayHit hit;
        if (!pick(x, y, hit)) return false; // the background navigates
        s.landmarks.point(s.placing) = hit.position;
        if (++s.placing < FaceLandmarks::kCount) {
            status_ = std::string("Click ") + kLandmarkPrompts[s.placing];
        } else {
            s.placing = -1;
            s.landmarks.valid = true;
            runSculptableGuides({{"landmarks", faceLandmarksJson(s.landmarks)},
                                 {"target", double(sculptablePresetQuads(kPresets[s.preset]))}});
        }
        drag_ = Drag::Sculptable; // the click is taken
        return true;
    }
    if (sculptableReviewActive() && s.landmarks.valid)
        for (int i = 0; i < FaceLandmarks::kCount; ++i) {
            ImVec2 q;
            if (gizmoProject(s.landmarks.point(i), q) && std::hypot(q.x - x, q.y - y) < reach) {
                s.dragLandmark = i;
                drag_ = Drag::Sculptable;
                return true;
            }
        }
    if (s.crop && s.box.valid() && renderSettings_.showSplats && !doc_.splats.empty())
        for (int h = 0; h < 6; ++h) {
            ImVec2 q;
            if (gizmoProject(faceCentre(s.box, h), q) && std::hypot(q.x - x, q.y - y) < reach) {
                s.dragHandle = h;
                s.dragStart = ImVec2(x, y);
                s.dragBox = s.box;
                drag_ = Drag::Sculptable;
                return true;
            }
        }
    return false;
}

void App::sculptableMouseMove(float x, float y) {
    SculptableUiState& s = *sculptable_;
    if (s.dragLandmark >= 0) {
        RayHit hit;
        if (pick(x, y, hit)) s.landmarks.point(s.dragLandmark) = hit.position;
        return;
    }
    if (s.dragHandle >= 0) {
        // Move the face along its axis by the drag's length along the axis as seen on screen.
        const int axis = s.dragHandle / 2;
        const glm::vec3 p0 = faceCentre(s.dragBox, s.dragHandle);
        const float len = 0.1f * glm::length(s.dragBox.extent());
        glm::vec3 p1 = p0;
        p1[axis] += len;
        ImVec2 a, b;
        if (!gizmoProject(p0, a) || !gizmoProject(p1, b)) return;
        const glm::vec2 d(b.x - a.x, b.y - a.y);
        const float d2 = glm::dot(d, d);
        if (d2 < 1.0f) return; // the axis points at the viewer: drag another handle
        const float t = glm::dot(glm::vec2(x - s.dragStart.x, y - s.dragStart.y), d) / d2 * len;
        AABB box = s.dragBox;
        const float minSize = 0.02f * glm::length(s.dragBox.extent());
        if (s.dragHandle % 2) box.max[axis] = std::max(s.dragBox.max[axis] + t, box.min[axis] + minSize);
        else box.min[axis] = std::min(s.dragBox.min[axis] + t, box.max[axis] - minSize);
        s.box = box;
    }
}

void App::sculptableMouseUp() {
    SculptableUiState& s = *sculptable_;
    if (s.dragLandmark >= 0) {
        // The partner on the other side follows (mirrored across the face's middle), then the guides are rebuilt.
        const int i = s.dragLandmark;
        s.dragLandmark = -1;
        static const int partner[FaceLandmarks::kCount] = {3, 2, 1, 0, -1, 6, 5, -1};
        FaceFrame f = s.frame;
        if (partner[i] >= 0 && f.orthonormalize()) {
            const glm::vec3 r = f.right();
            const float mid = 0.5f * (glm::dot(s.landmarks.noseTip, r) + glm::dot(s.landmarks.chin, r));
            const glm::vec3 p = s.landmarks.point(i);
            s.landmarks.point(partner[i]) = p - 2.0f * (glm::dot(p, r) - mid) * r;
        }
        Args a{{"landmarks", faceLandmarksJson(s.landmarks)}, {"target", double(sculptablePresetQuads(kPresets[s.preset]))}};
        if (f.front != glm::vec3(0, 0, 1)) a["front"] = vecText(f.front);
        if (f.up != glm::vec3(0, 1, 0)) a["up"] = vecText(f.up);
        runSculptableGuides(a);
    }
    if (s.dragHandle >= 0) {
        s.dragHandle = -1;
        char buf[160];
        std::snprintf(buf, sizeof buf, "Crop Box %.3g x %.3g x %.3g", double(s.box.extent().x), double(s.box.extent().y),
                      double(s.box.extent().z));
        status_ = buf;
    }
}

double App::sculptableTestValue(const std::string& name) {
    if (!sculptable_) sculptable_ = std::make_shared<SculptableUiState>();
    const SculptableUiState& s = *sculptable_;
    if (name == "sculptable_running") return s.job ? 1.0 : 0.0;
    if (name == "sculptable_review") return s.reviewing ? 1.0 : 0.0;
    if (name == "sculptable_landmarks") return s.landmarks.valid ? double(FaceLandmarks::kCount) : s.placing >= 0 ? double(s.placing) : 0.0;
    if (name == "sculptable_placing") return double(s.placing);
    if (name == "sculptable_crop") return s.crop ? 1.0 : 0.0;
    if (name == "sculptable_crop_height") return s.crop && s.box.valid() ? double(s.box.extent().y) : 0.0;
    if (name == "show_splats") return renderSettings_.showSplats ? 1.0 : 0.0;
    if (name.rfind("landmark_", 0) == 0 && name.size() > 11) { // landmark_x_4 / landmark_y_4 / landmark_z_4: mesh space
        const int axis = name.size() > 9 ? name[9] - 'x' : -1;
        const int i = std::atoi(name.c_str() + std::min<size_t>(name.size(), 11));
        if (axis >= 0 && axis < 3 && i >= 0 && i < FaceLandmarks::kCount) return double(s.landmarks.point(i)[axis]);
    }
    return std::nan("");
}

} // namespace sf
