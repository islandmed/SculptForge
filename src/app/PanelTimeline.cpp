// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The Sculpt Timeline strip (docs/guide/timeline.md): every step of the history as a tick along the top (the
// default) or the bottom of the canvas. Drag the scrubber to see the model at any point, click a tick to go there; sculpting from an earlier
// point starts a branch and the old continuation stays as a lane above the track (click it to switch back).
// Right-click a stroke to re-tune its strength. Timelapse renders the history as a turntable video. The data
// side is core/Timeline.h; Help > Benchmark (proof mode) is in ProofMode.cpp.
#include "app/App.h"
#include "app/ShellQuote.h"
#include "app/Theme.h"
#include "app/Ui.h"
#include "app/I18n.h"

#include "core/Json.h"
#include "core/Timeline.h"

#include <IconsLucide.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace sf {

namespace {

std::filesystem::path fsPathOf(const std::string& p) { return std::filesystem::path(std::u8string(p.begin(), p.end())); }
std::string utf8Of(const std::filesystem::path& p) {
    const auto s = p.u8string();
    return std::string(s.begin(), s.end());
}

ImU32 kindColor(Timeline::Kind k, float alpha) {
    switch (k) {
    case Timeline::Kind::Stroke: return theme::u32(theme::rgb(0xc9c9cf), alpha);
    case Timeline::Kind::Mask: return theme::u32(theme::rgb(0x6aa2e8), alpha);
    case Timeline::Kind::Paint: return theme::u32(theme::rgb(0xd77fb3), alpha);
    case Timeline::Kind::Visibility: return theme::u32(theme::rgb(0x8fc07a), alpha);
    case Timeline::Kind::Topology: return theme::u32(theme::rgb(0x4fc3c0), alpha);
    default: return theme::u32(theme::textDim, alpha);
    }
}

const char* kindLabel(Timeline::Kind k) {
    switch (k) {
    case Timeline::Kind::Start: return "Start";
    case Timeline::Kind::Stroke: return "Stroke";
    case Timeline::Kind::Mask: return "Mask";
    case Timeline::Kind::Paint: return "Paint";
    case Timeline::Kind::Visibility: return "Visibility";
    case Timeline::Kind::Topology: return "Topology";
    default: return "Edit";
    }
}

std::string ago(double unixTime) {
    const double now = std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
    const double s = std::max(0.0, now - unixTime);
    char buf[64];
    if (s < 60) std::snprintf(buf, sizeof buf, "%.0f s ago", s);
    else if (s < 3600) std::snprintf(buf, sizeof buf, "%.0f min ago", s / 60);
    else std::snprintf(buf, sizeof buf, "%.1f h ago", s / 3600);
    return buf;
}

} // namespace

struct TimelapseJob {
    FILE* ffmpeg = nullptr;
    std::string path;
    int width = 1920, height = 1080, fps = 30;
    int frame = 0, frames = 0, hold0 = 0, hold1 = 0;
    size_t steps = 0, restore = 0;
    Camera camera;
    float yaw0 = 0.0f;
    std::vector<uint8_t> rgba;
    std::string error;
    bool cancelled = false;
};

struct TimelineUiState {
    bool loaded = false;
    bool visible = true;      // the strip is open (else only its tab shows)
    bool record = true;       // keep a timeline for the history
    bool saveHistory = false; // write <name>.sfhist with the project
    bool dragging = false;
    size_t pendingSeek = SIZE_MAX; // the scrubber's target, applied once a frame
    uint32_t retuneNode = Timeline::kNone;
    float retuneValue = 1.0f;
    std::string message;
    // Timelapse dialog
    bool timelapseDialog = false;
    int timelapseRes = 0; // 0 = 1080p, 1 = 4K
    float timelapseSeconds = 20.0f;
    char timelapsePath[1024] = "";
    std::unique_ptr<TimelapseJob> job;
};

namespace {

std::string prefsFile(const std::string& configDir) { return configDir.empty() ? std::string() : configDir + "timeline.json"; }

void loadPrefs(TimelineUiState& s, const std::string& configDir, bool deterministic) {
    s.loaded = true;
    if (deterministic) return;
    std::ifstream f(fsPathOf(prefsFile(configDir)));
    if (!f) return;
    std::stringstream ss;
    ss << f.rdbuf();
    Json j;
    std::string err;
    if (!Json::parse(ss.str(), j, err) || !j.isObject()) return;
    s.visible = j["visible"].asBool(s.visible);
    s.record = j["record"].asBool(s.record);
    s.saveHistory = j["save_history"].asBool(s.saveHistory);
}

void savePrefs(const TimelineUiState& s, const std::string& configDir, bool deterministic) {
    if (deterministic || configDir.empty()) return;
    Json j = Json::object();
    j.set("visible", s.visible);
    j.set("record", s.record);
    j.set("save_history", s.saveHistory);
    std::ofstream f(fsPathOf(prefsFile(configDir)));
    f << j.dump(true);
}

} // namespace

void App::registerTimelineUiActions() {
    auto add = [this](const char* id, const char* label, const char* icon, const char* palette, const char* description,
                      std::function<void()> run, std::function<bool()> checked = nullptr) {
        Action a;
        a.id = id;
        a.label = label;
        a.icon = icon;
        a.palette = palette;
        a.description = description;
        a.run = std::move(run);
        a.checked = std::move(checked);
        actions_.add(std::move(a));
    };
    auto ui = [this]() -> TimelineUiState& {
        if (!timelineUi_) timelineUi_ = std::make_shared<TimelineUiState>();
        return *timelineUi_;
    };
    // Before a file named on the command line opens: open_project reads its history into this scene.
    Timeline::historyDocument = &doc_;
    if (!ui().loaded) {
        loadPrefs(ui(), configDir_, uiConfigDeterministic_);
        Timeline::saveWithProject = ui().saveHistory;
    }
    add("view.timeline", "Sculpt Timeline", ICON_LC_HISTORY, "Edit",
        "Show or hide the history strip along the top or bottom of the canvas (Preferences > Interface > Timeline "
        "position): every stroke and operation as a tick. Drag "
        "to see the model at any point, click to go there; sculpting from an earlier point keeps the old "
        "continuation as a branch. Right-click a stroke to change its strength.",
        [this, ui] {
            ui().visible = !ui().visible;
            savePrefs(ui(), configDir_, uiConfigDeterministic_);
        },
        [ui] { return ui().visible; });
    add("view.timeline_top", "Top of viewport", nullptr, "Preferences > Interface",
        "Timeline position: the Sculpt Timeline strip runs along the top of the viewport, below the top shelf and away "
        "from the brushes on the bottom shelf (the default). Store Config keeps it.",
        [this] { uiPrefs_.timelineTop = true; }, [this] { return uiPrefs_.timelineTop; });
    add("view.timeline_bottom", "Bottom", nullptr, "Preferences > Interface",
        "Timeline position: the Sculpt Timeline strip runs along the bottom of the viewport, above the brush shelf. "
        "Store Config keeps it.",
        [this] { uiPrefs_.timelineTop = false; }, [this] { return !uiPrefs_.timelineTop; });
    add("timeline.timelapse", "Export timelapse...", ICON_LC_FILM, "Edit",
        "Render the history of the active Part as a turntable timelapse video (1080p or 4K MP4, no captions).",
        [ui] { ui().timelapseDialog = true; });
    add("help.benchmark", "Benchmark (proof mode)", ICON_LC_GAUGE, "Help",
        "Measure how responsive sculpting is on this computer: the standard scene (a 15 or 33 million triangle head; "
        "Clay small, medium and large, Move, Smooth and a 240 px brush) through the same code a pen uses, with a live "
        "display of frame times and how far the stroke trails the pointer. Writes a video and a JSON result you can "
        "share. Replaces the scene.",
        [this] { startProofMode(); });
}

void App::startTimelapse(const std::string& path, int width, int height, float seconds, int fps) {
    if (!timelineUi_) timelineUi_ = std::make_shared<TimelineUiState>();
    TimelineUiState& s = *timelineUi_;
    Timeline* t = Timeline::of(doc_.undo);
    if (!t || t->cursor() == 0) {
        s.message = "There is no history to make a timelapse of (sculpt something first, or open a project saved with its history)";
        status_ = s.message;
        return;
    }
    t->sync(doc_);
    auto job = std::make_unique<TimelapseJob>();
    job->path = path;
    job->width = width & ~1;
    job->height = height & ~1;
    job->fps = std::max(fps, 1);
    job->frames = std::max(job->fps * 2, int(std::lround(seconds * float(job->fps))));
    job->hold0 = std::min(job->frames / 6, job->fps * 6 / 10);
    job->hold1 = std::min(job->frames / 4, job->fps * 3 / 2);
    job->steps = t->cursor(); // the history up to what the view shows now
    job->restore = t->cursor();
    job->camera = camera_;
    job->camera.frame(sceneBounds()); // framed on the finished model
    job->camera.pitch = camera_.pitch;
    job->camera.distance *= 0.72f; // frame() leaves room for any view direction; a turntable can come closer
    job->camera.aspect = float(job->width) / float(std::max(job->height, 1));
    job->yaw0 = camera_.yaw;
    const std::string quoted = shellQuote(path); // (never a shell-active file name)
    if (quoted.empty()) {
        status_ = s.message = "Timelapse: choose a file name without \" % ! ^ & | < > in it";
        return;
    }
    const std::string cmd = "ffmpeg -loglevel error -y -f rawvideo -pix_fmt rgba -s " + std::to_string(job->width) + "x" +
                            std::to_string(job->height) + " -r " + std::to_string(job->fps) +
                            " -i - -c:v libx264 -preset medium -crf 18 -pix_fmt yuv420p -movflags +faststart " + quoted;
#if defined(_WIN32)
    job->ffmpeg = _popen(cmd.c_str(), "wb");
#else
    job->ffmpeg = popen(cmd.c_str(), "w");
#endif
    if (!job->ffmpeg) {
        status_ = s.message = "Timelapse needs ffmpeg (it couldn't be started)";
        return;
    }
    s.job = std::move(job);
    status_ = "Rendering the timelapse...";
}

bool App::timelapseStep(double budgetSeconds) {
    if (!timelineUi_ || !timelineUi_->job) return false;
    TimelapseJob& j = *timelineUi_->job;
    Timeline* t = Timeline::of(doc_.undo);
    const auto t0 = std::chrono::steady_clock::now();
    auto finish = [&](bool ok) {
        std::string err;
        if (t) t->seek(doc_, std::min(j.restore, t->path().size() - 1), &err); // back to where the user was
#if defined(_WIN32)
        const int status = _pclose(j.ffmpeg);
#else
        const int status = pclose(j.ffmpeg);
#endif
        if (j.cancelled) {
            std::error_code ec;
            std::filesystem::remove(fsPathOf(j.path), ec);
            status_ = "Timelapse cancelled";
        } else if (!ok || status != 0) {
            status_ = "The timelapse couldn't be written" + (j.error.empty() ? std::string() : ": " + j.error);
        } else {
            status_ = "Timelapse saved: " + j.path + " (" + std::to_string(j.frames) + " frames, " +
                      std::to_string(j.frames / j.fps) + " s)";
        }
        timelineUi_->message = status_;
        lastCommandOk_ = ok && status == 0 && !j.cancelled;
        timelineUi_->job.reset();
        return false;
    };
    if (!t || j.cancelled) return finish(false);
    RenderSettings settings = renderSettings_;
    settings.dimInactive = false;
    settings.motionProxy = false;
    while (j.frame < j.frames) {
        // The history plays between the two holds; the camera turns once around the model over the whole video.
        const int sweep = std::max(1, j.frames - j.hold0 - j.hold1);
        const double u = std::clamp(double(j.frame - j.hold0) / double(sweep), 0.0, 1.0);
        const size_t step = std::min(j.steps, size_t(std::lround(u * double(j.steps))));
        std::string err;
        if (step != t->cursor() && !t->seek(doc_, step, &err)) {
            j.error = err;
            return finish(false);
        }
        Camera cam = j.camera;
        cam.yaw = j.yaw0 + 6.2831853f * float(j.frame) / float(j.frames);
        syncScene();
        if (!renderer_.renderToImage(cam, j.width, j.height, settings, j.rgba, err)) {
            j.error = err;
            return finish(false);
        }
        for (size_t i = 3; i < j.rgba.size(); i += 4) j.rgba[i] = 255;
        if (std::fwrite(j.rgba.data(), 1, j.rgba.size(), j.ffmpeg) != j.rgba.size()) {
            j.error = "ffmpeg stopped";
            return finish(false);
        }
        ++j.frame;
        if (budgetSeconds > 0 && std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() > budgetSeconds)
            return true;
    }
    return finish(true);
}

int App::runTimelapseCli() {
    Timeline* t = Timeline::of(doc_.undo);
    if (!t) t = Timeline::attach(doc_).get();
    if (t->cursor() == 0 && t->path().size() > 1) t->seek(doc_, t->path().size() - 1);
    if (t->cursor() == 0) {
        std::fprintf(stderr, "No history to render: open a project saved with its history (<name>.sfhist beside it)\n");
        return 1;
    }
    std::printf("Timelapse: %zu steps, %dx%d, %.0f s\n", t->cursor(), options_.width, options_.height, double(options_.timelapseSeconds));
    std::fflush(stdout);
    startTimelapse(options_.timelapsePath, options_.width, options_.height, options_.timelapseSeconds, std::max(options_.fps, 1));
    if (!timelineUi_ || !timelineUi_->job) {
        std::fprintf(stderr, "%s\n", status_.c_str());
        return 1;
    }
    while (timelapseStep(0.0)) {
    }
    std::printf("%s\n", status_.c_str());
    return lastCommandOk_ ? 0 : 1;
}

void App::drawTimelineUi() {
    if (!timelineUi_) timelineUi_ = std::make_shared<TimelineUiState>();
    TimelineUiState& s = *timelineUi_;
    if (!s.loaded) {
        loadPrefs(s, configDir_, uiConfigDeterministic_);
        Timeline::saveWithProject = s.saveHistory;
        Timeline::historyDocument = &doc_;
    }
    if (proof_) drawProofUi();
    // Keep a timeline on the active SubTool's history (each SubTool's travels with its undo stack).
    Timeline* t = Timeline::of(doc_.undo);
    if (!t && s.record) t = Timeline::attach(doc_).get();
    if (!t) return;
    const bool stroking = doc_.sculptor.strokeActive() || drag_ == Drag::Stroke || !dabQueue_.empty();
    if (!stroking) t->sync(doc_);

    // A running timelapse: a few frames each app frame, under a modal with a progress bar.
    if (s.job) {
        ImGui::OpenPopup("Rendering timelapse");
        if (ImGui::BeginPopupModal("Rendering timelapse", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("%s", s.job->path.c_str());
            ImGui::ProgressBar(float(s.job->frame) / float(std::max(1, s.job->frames)), ImVec2(360 * uiScale_, 0));
            if (ImGui::Button(tr("Cancel"))) s.job->cancelled = true;
            ImGui::EndPopup();
        }
        activeUntil_ = std::max(activeUntil_, double(SDL_GetTicksNS()) * 1e-9 + 0.2);
        if (!timelapseStep(0.05)) t = Timeline::of(doc_.undo);
        if (!t) return;
    }
    if (s.timelapseDialog) {
        if (!s.timelapsePath[0]) {
            std::filesystem::path p;
            if (!doc_.path.empty()) p = fsPathOf(doc_.path).replace_extension().string() + "-timelapse.mp4";
            else if (const char* v = SDL_GetUserFolder(SDL_FOLDER_VIDEOS)) p = fsPathOf(v) / "sculptforge-timelapse.mp4";
            else p = "sculptforge-timelapse.mp4";
            std::snprintf(s.timelapsePath, sizeof s.timelapsePath, "%s", utf8Of(p).c_str());
        }
        ImGui::OpenPopup("Export timelapse");
        s.timelapseDialog = false;
    }
    if (ImGui::BeginPopupModal("Export timelapse", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped(tr("The history of the active Part (%zu steps up to the cursor) played as a turntable."), t->cursor());
        ImGui::RadioButton("1080p", &s.timelapseRes, 0);
        ImGui::SameLine();
        ImGui::RadioButton("4K", &s.timelapseRes, 1);
        ImGui::SetNextItemWidth(200 * uiScale_);
        ImGui::SliderFloat(tr("Length"), &s.timelapseSeconds, 5.0f, 90.0f, "%.0f s");
        ImGui::SetNextItemWidth(420 * uiScale_);
        ImGui::InputText("File", s.timelapsePath, sizeof s.timelapsePath);
        if (ImGui::Button(tr("Export"))) {
            const bool k4 = s.timelapseRes == 1;
            startTimelapse(s.timelapsePath, k4 ? 3840 : 1920, k4 ? 2160 : 1080, s.timelapseSeconds, 30);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(tr("Close"))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    // The strip: along the bottom of the canvas, or its top (Preferences > Interface > Timeline position, #63:
    // away from the brush shelf). At the top it sits below the top shelf (the canvas starts there) and what else
    // the canvas shows at its top (the axis gizmo, the update banner) moves down below it (timelineTopInset_).
    const CanvasRect c = canvasRect();
    const float sc = uiScale_;
    const bool top = uiPrefs_.timelineTop;
    timelineTopInset_ = 0.0f;
    timelineStripY_ = -1.0f;
    if (c.w < 200 || c.h < 150 || hideUi_) return;
    const float margin = 10 * sc;
    if (!s.visible) {
        if (top) ImGui::SetNextWindowPos(ImVec2(c.x + c.w * 0.5f, c.y + 4 * sc), ImGuiCond_Always, ImVec2(0.5f, 0.0f));
        else ImGui::SetNextWindowPos(ImVec2(c.x + c.w * 0.5f, c.y + c.h - 4 * sc), ImGuiCond_Always, ImVec2(0.5f, 1.0f));
        ImGui::SetNextWindowBgAlpha(0.85f);
        if (ImGui::Begin("##timeline_tab", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                                                        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                                                        ImGuiWindowFlags_NoNav)) {
            const std::string label = std::string(ICON_LC_HISTORY " Timeline  ") + std::to_string(t->cursor()) + "/" +
                                      std::to_string(t->path().size() - 1);
            if (ImGui::SmallButton(label.c_str())) {
                s.visible = true;
                savePrefs(s, configDir_, uiConfigDeterministic_);
            }
            ui::HelpText("Sculpt Timeline", nullptr, "Open the history strip.", "Edit");
            timelineStripY_ = ImGui::GetWindowPos().y;
            if (top) timelineTopInset_ = ImGui::GetWindowPos().y + ImGui::GetWindowSize().y - c.y;
        }
        ImGui::End();
        return;
    }
    const auto branches = t->branches();
    const int lanes = std::min<int>(3, int(branches.size()));
    const float laneH = 7 * sc, trackH = 26 * sc;
    const float height = 30 * sc + trackH + float(lanes) * (laneH + 3 * sc);
    const float stripY = top ? c.y + margin : c.y + c.h - margin - height;
    ImGui::SetNextWindowPos(ImVec2(c.x + margin, stripY), ImGuiCond_Always);
    timelineStripY_ = stripY;
    if (top) timelineTopInset_ = margin + height;
    ImGui::SetNextWindowSize(ImVec2(c.w - 2 * margin, height), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.88f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8 * sc, 5 * sc));
    const bool open = ImGui::Begin("##timeline", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings |
                                                              ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav |
                                                              ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();
    if (!open) {
        ImGui::End();
        return;
    }
    const auto& path = t->path();
    const size_t n = path.size();
    // Header row: what it is, where the cursor is, the controls.
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(ICON_LC_HISTORY);
    ImGui::SameLine();
    ImGui::Text(tr("Step %zu of %zu"), t->cursor(), n - 1);
    if (!branches.empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled("%s %zu branch%s", ICON_LC_GIT_BRANCH, branches.size(), branches.size() == 1 ? "" : "es");
    }
    if (const Timeline::Node& cur = t->node(t->cursorNode()); t->cursor() > 0) {
        ImGui::SameLine();
        ImGui::TextDisabled("  %s", cur.label.c_str());
    }
    if (!s.message.empty()) {
        ImGui::SameLine();
        ImGui::TextColored(theme::textDim, "  %s", s.message.c_str());
    }
    const float buttons = 250 * sc;
    ImGui::SameLine(ImGui::GetWindowWidth() - buttons);
    if (ImGui::Checkbox(tr("Save with project"), &s.saveHistory)) {
        Timeline::saveWithProject = s.saveHistory;
        savePrefs(s, configDir_, uiConfigDeterministic_);
    }
    ui::HelpText("Save with project", nullptr,
                 "Also save the history beside the project as <name>.sfhist (strokes, masks, vertex paint, visibility; "
                 "a topology change cuts it). Opening the project brings it back.",
                 "Edit > Sculpt Timeline");
    ImGui::SameLine();
    if (ImGui::SmallButton(ICON_LC_FILM)) s.timelapseDialog = true;
    ui::HelpText("Export timelapse", nullptr, "Render this history as a turntable timelapse video.", "Edit > Sculpt Timeline");
    ImGui::SameLine();
    if (ImGui::SmallButton(top ? ICON_LC_CHEVRON_UP : ICON_LC_CHEVRON_DOWN)) {
        s.visible = false;
        savePrefs(s, configDir_, uiConfigDeterministic_);
    }
    ui::HelpText("Hide", nullptr, "Fold the timeline into a tab.", "Edit > Sculpt Timeline");

    // Branch lanes, then the track.
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float x0 = origin.x + 6 * sc, x1 = origin.x + ImGui::GetContentRegionAvail().x - 6 * sc;
    size_t span = n > 1 ? n - 1 : 1; // steps the track shows: the active branch, or a longer other branch
    for (const Timeline::Branch& b : branches) span = std::max(span, b.from + b.length);
    const float step = (x1 - x0) / float(span);
    auto xOf = [&](double i) { return x0 + float(i) * step; };
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    float y = origin.y;
    uint32_t hovered = Timeline::kNone;
    for (int l = 0; l < lanes; ++l) {
        const Timeline::Branch& b = branches[branches.size() - 1 - size_t(l)]; // the newest on top
        const float ly = y + laneH * 0.5f;
        const float bx0 = xOf(double(b.from)), bx1 = std::min(x1, xOf(double(b.from + b.length)));
        dl->AddLine(ImVec2(bx0, y + laneH), ImVec2(bx0, ly), theme::u32(theme::textDim, 0.6f), 1.0f * sc);
        dl->AddLine(ImVec2(bx0, ly), ImVec2(bx1, ly), theme::u32(theme::textDim, 0.6f), 2.0f * sc);
        dl->AddCircleFilled(ImVec2(bx1, ly), 3.0f * sc, theme::u32(theme::textBright, 0.9f));
        ImGui::SetCursorScreenPos(ImVec2(bx0, y));
        ImGui::PushID(int(b.first));
        if (ImGui::InvisibleButton("##lane", ImVec2(std::max(8 * sc, bx1 - bx0 + 4 * sc), laneH))) {
            std::string e;
            if (!t->select(doc_, b.tip, &e)) s.message = e;
            else s.message = "Switched to the other branch (the one you left is a lane now)";
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Branch from step %zu: %zu step%s, ending with \"%s\"\nClick to make it the active branch", b.from,
                              b.length, b.length == 1 ? "" : "s", t->node(b.tip).label.c_str());
        ImGui::PopID();
        y += laneH + 3 * sc;
    }
    if (branches.size() > size_t(lanes)) {
        dl->AddText(ImVec2(x1 - 30 * sc, origin.y), theme::u32(theme::textDim), ("+" + std::to_string(branches.size() - size_t(lanes))).c_str());
    }
    const float ty0 = y, ty1 = y + trackH;
    dl->AddRectFilled(ImVec2(x0 - 4 * sc, ty0), ImVec2(x1 + 4 * sc, ty1), theme::u32(theme::frame, 0.5f), 3 * sc);
    dl->AddRectFilled(ImVec2(x0 - 4 * sc, ty0), ImVec2(xOf(double(n - 1)) + 4 * sc, ty1), theme::u32(theme::frame), 3 * sc);
    // Ticks: one per step (several per pixel collapse into one line), dimmer after the cursor.
    const size_t cursor = s.dragging && s.pendingSeek != SIZE_MAX ? s.pendingSeek : t->cursor();
    const size_t every = step >= 1.0f || n < 2 ? 1 : size_t(std::ceil(1.0f / step));
    for (size_t i = 1; i < n; i += every) {
        const Timeline::Node& node = t->node(path[i]);
        const float x = xOf(double(i));
        const float h = node.kind == Timeline::Kind::Topology ? 1.0f : node.kind == Timeline::Kind::Stroke ? 0.62f : 0.45f;
        const float alpha = i <= cursor ? 1.0f : 0.35f;
        dl->AddLine(ImVec2(x, ty1 - 3 * sc), ImVec2(x, ty1 - 3 * sc - (trackH - 6 * sc) * h), kindColor(node.kind, alpha),
                    std::clamp(step * 0.6f, 1.0f, 3.0f * sc));
        if (node.strength != 1.0f) dl->AddCircleFilled(ImVec2(x, ty0 + 3 * sc), 2.0f * sc, theme::u32(theme::accent, alpha));
        if (path[i] == s.retuneNode) dl->AddRect(ImVec2(x - 3 * sc, ty0 + 1), ImVec2(x + 3 * sc, ty1 - 1), theme::u32(theme::textBright), 2 * sc);
        if (t->hasCheckpoint(path[i])) dl->AddLine(ImVec2(x, ty1 - 2 * sc), ImVec2(x, ty1), theme::u32(theme::textDim, 0.7f), 1.0f);
    }
    // The scrubber.
    const float cx = xOf(double(cursor));
    dl->AddLine(ImVec2(cx, ty0 - 2 * sc), ImVec2(cx, ty1 + 2 * sc), theme::u32(theme::accent), 2.0f * sc);
    dl->AddTriangleFilled(ImVec2(cx - 5 * sc, ty0 - 3 * sc), ImVec2(cx + 5 * sc, ty0 - 3 * sc), ImVec2(cx, ty0 + 3 * sc), theme::u32(theme::accent));
    ImGui::SetCursorScreenPos(ImVec2(x0 - 4 * sc, ty0));
    ImGui::InvisibleButton("##track", ImVec2(std::max(8 * sc, xOf(double(n - 1)) - x0 + 8 * sc), trackH),
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    const bool trackHovered = ImGui::IsItemHovered();
    auto indexAt = [&](float mx) { return n > 1 ? size_t(std::clamp(std::lround((mx - x0) / std::max(step, 1e-6f)), 0L, long(n - 1))) : size_t(0); };
    if (trackHovered || ImGui::IsItemActive()) hovered = path[indexAt(mouse.x)];
    if (ImGui::IsItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        s.dragging = true;
        s.pendingSeek = indexAt(mouse.x);
    }
    if (s.dragging && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) s.dragging = false;
    if (ImGui::IsItemClicked(ImGuiMouseButton_Right) && hovered != Timeline::kNone) {
        s.retuneNode = hovered;
        s.retuneValue = t->node(hovered).strength;
        ImGui::OpenPopup("##retune");
    }
    if (trackHovered && hovered != Timeline::kNone && !ImGui::IsPopupOpen("##retune")) {
        const Timeline::Node& node = t->node(hovered);
        const int d = t->depth(hovered);
        if (node.parent == Timeline::kNone) ImGui::SetTooltip("%s", tr("Start of the history"));
        else
            ImGui::SetTooltip("Step %d: %s (%s)\n%u %s, %s%s\nClick or drag to show the model here; right-click to re-tune", d,
                              node.label.c_str(), kindLabel(node.kind), node.touched,
                              node.kind == Timeline::Kind::Visibility ? "triangles" : "vertices", ago(node.time).c_str(),
                              node.strength != 1.0f ? ("\nStrength " + std::to_string(int(std::lround(node.strength * 100))) + "%").c_str() : "");
    }
    // Apply the scrubber's target once a frame (coalesced while dragging).
    if (s.pendingSeek != SIZE_MAX && s.pendingSeek != t->cursor() && !stroking) {
        std::string e;
        if (!t->seek(doc_, s.pendingSeek, &e)) s.message = e;
        else s.message.clear();
        doc_.modified = true;
        updateTitle();
    }
    if (!s.dragging) s.pendingSeek = SIZE_MAX;
    // Re-tune pop-up.
    if (ImGui::BeginPopup("##retune")) {
        const uint32_t id = s.retuneNode;
        if (id < t->nodeCount() && t->node(id).alive) {
            const Timeline::Node& node = t->node(id);
            ImGui::Text(tr("Step %d: %s"), t->depth(id), node.label.c_str());
            std::string why;
            if (t->canRetune(id, &why)) {
                ImGui::SetNextItemWidth(220 * sc);
                if (ImGui::SliderFloat(tr("Strength"), &s.retuneValue, 0.0f, 2.0f, "%.2f")) {
                    std::string e;
                    if (!t->retune(doc_, id, s.retuneValue, &e)) s.message = e;
                    doc_.modified = true;
                }
                ui::HelpText("Strength", nullptr,
                             "How strongly this stroke shows: 1 = as sculpted, 0 = gone, 2 = twice as strong. Later strokes "
                             "are not redone: where they overlap this one they keep their own displacement on top.",
                             "Edit > Sculpt Timeline");
                if (ImGui::SmallButton(tr("Reset to 1"))) {
                    s.retuneValue = 1.0f;
                    t->retune(doc_, id, 1.0f);
                }
                ImGui::SameLine();
            } else {
                ImGui::TextDisabled("%s", why.c_str());
            }
            if (ImGui::SmallButton(tr("Show the model here"))) {
                std::string e;
                if (!t->select(doc_, id, &e)) s.message = e;
                ImGui::CloseCurrentPopup();
            }
            ImGui::TextDisabled("%s", tr("Sculpting from an earlier step starts a branch;\nthe later steps stay as a lane above."));
        }
        ImGui::EndPopup();
    } else if (s.retuneNode != Timeline::kNone && !ImGui::IsPopupOpen("##retune")) {
        s.retuneNode = Timeline::kNone;
    }
    ImGui::End();
}

} // namespace sf
