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
    bool compact = true;      // one row: the count, the track and icon buttons (Preferences > Interface > Compact timeline)
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

// Shortens `text` with "..." to fit `width` (whole UTF-8 characters).
std::string fitText(const std::string& text, float width) {
    if (ImGui::CalcTextSize(text.c_str()).x <= width) return text;
    std::string t = text;
    while (!t.empty() && ImGui::CalcTextSize((t + "...").c_str()).x > width) {
        while (!t.empty() && (static_cast<unsigned char>(t.back()) & 0xC0) == 0x80) t.pop_back();
        if (!t.empty()) t.pop_back();
    }
    return t.empty() ? std::string() : t + "...";
}

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
    s.compact = j["compact"].asBool(s.compact);
}

void savePrefs(const TimelineUiState& s, const std::string& configDir, bool deterministic) {
    if (deterministic || configDir.empty()) return;
    Json j = Json::object();
    j.set("visible", s.visible);
    j.set("record", s.record);
    j.set("save_history", s.saveHistory);
    j.set("compact", s.compact);
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
        "from the brushes on the bottom shelf (the default). Saved automatically.",
        [this] { uiPrefs_.timelineTop = true; }, [this] { return uiPrefs_.timelineTop; });
    add("view.timeline_bottom", "Bottom", nullptr, "Preferences > Interface",
        "Timeline position: the Sculpt Timeline strip runs along the bottom of the viewport, above the brush shelf. "
        "Saved automatically.",
        [this] { uiPrefs_.timelineTop = false; }, [this] { return !uiPrefs_.timelineTop; });
    add("view.timeline_compact", "Compact timeline", ICON_LC_FOLD_VERTICAL, "Preferences > Interface",
        "The Sculpt Timeline as one thin row: the step count, the track and icon buttons (the default). Off: the "
        "full strip, with a header row above the track. Saved automatically.",
        [this, ui] {
            ui().compact = !ui().compact;
            savePrefs(ui(), configDir_, uiConfigDeterministic_);
        },
        [ui] { return ui().compact; });
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

bool App::timelapseBusy() const { return timelineUi_ && timelineUi_->job; }

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
        if (!renderTimelapseFrame(cam, j.width, j.height, j.rgba, err)) {
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

// One frame as the timelapse renders it: the view's look, every Part at full brightness, the full mesh (never the
// view's coarse levels or proxy, which switch with how long the view has been still and with edits waiting for their
// levels: the history's seeks flipped dense meshes between the two from frame to frame).
bool App::renderTimelapseFrame(const Camera& cam, int width, int height, std::vector<uint8_t>& rgba, std::string& err) {
    RenderSettings settings = renderSettings_;
    settings.dimInactive = false;
    settings.motionProxy = false;
    settings.stroking = false;
    settings.fullDetail = true;
    syncScene();
    return renderer_.renderToImage(cam, width, height, settings, rgba, err);
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
    if (take_) drawTakeHud(); // (BenchTake.cpp)
    // Keep a timeline on the active Part's history (each Part's travels with its undo stack).
    Timeline* t = Timeline::of(doc_.undo);
    if (!t && s.record) t = Timeline::attach(doc_).get();
    if (!t) return;
    const bool stroking = doc_.sculptor.strokeActive() || drag_ == Drag::Stroke || !dabQueue_.empty();
    if (!stroking) t->sync(doc_);

    // A running timelapse: a few frames each app frame, under a modal with a progress bar.
    if (s.job) {
        ImGui::OpenPopup("###timelapse.rendering");
        if (ImGui::BeginPopupModal(tr("Rendering timelapse###timelapse.rendering"), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
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
        ImGui::OpenPopup("###timelapse.export");
        s.timelapseDialog = false;
    }
    if (ImGui::BeginPopupModal(tr("Export timelapse###timelapse.export"), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped(tr("The history of the active Part (%zu steps up to the cursor) played as a turntable."), t->cursor());
        ImGui::RadioButton("1080p", &s.timelapseRes, 0);
        ImGui::SameLine();
        ImGui::RadioButton("4K", &s.timelapseRes, 1);
        ImGui::SetNextItemWidth(200 * uiScale_);
        ImGui::SliderFloat(tr("Length"), &s.timelapseSeconds, 5.0f, 90.0f, "%.0f s");
        ImGui::SetNextItemWidth(420 * uiScale_);
        ImGui::InputText(tr("File"), s.timelapsePath, sizeof s.timelapsePath);
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
    CanvasRect c = canvasRect();
    const float sc = uiScale_;
    const bool top = uiPrefs_.timelineTop;
    // The open Library takes the canvas's top (or bottom) edge: the strip moves off it, so it never covers the
    // Library's tabs and close button (#80, a 720p screen at 150%).
    if (libraryBrowser_.open()) {
        const float lib = std::min(LibraryBrowser::height(layout_, sc), c.h);
        if (!layout_.libraryBottom) c.y += lib;
        c.h -= lib;
    }
    timelineTopInset_ = 0.0f;
    timelineStripY_ = -1.0f;
    timelineStripH_ = 0.0f;
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
            timelineStripH_ = ImGui::GetWindowSize().y;
            if (top) timelineTopInset_ = ImGui::GetWindowPos().y + ImGui::GetWindowSize().y - canvasRect().y;
        }
        ImGui::End();
        return;
    }
    const auto branches = t->branches();
    const int lanes = std::min<int>(3, int(branches.size()));
    // Compact (the default, @factoberplay): one row, the step count, the track and icon buttons, with
    // thin branch lanes over the track; the full strip has a header row above the track (Preferences > Interface).
    const bool compact = s.compact;
    const float laneH = (compact ? 4 : 7) * sc, laneGap = (compact ? 2 : 3) * sc, trackH = (compact ? 20 : 26) * sc;
    const float padY = (compact ? 3 : 5) * sc;
    const float height = compact ? 2 * padY + trackH + float(lanes) * (laneH + laneGap)
                                 : 30 * sc + trackH + float(lanes) * (laneH + laneGap);
    const float stripMargin = (compact ? 6 : 10) * sc;
    const float stripY = top ? c.y + stripMargin : c.y + c.h - stripMargin - height;
    ImGui::SetNextWindowPos(ImVec2(c.x + margin, stripY), ImGuiCond_Always);
    timelineStripY_ = stripY;
    timelineStripH_ = height;
    if (top) timelineTopInset_ = stripY + height - canvasRect().y;
    ImGui::SetNextWindowSize(ImVec2(c.w - 2 * margin, height), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.88f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2((compact ? 6 : 8) * sc, padY));
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
    timelineSpans_.clear(); // (UI tests check that nothing in the strip's row overlaps)
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float x0 = 0.0f, x1 = 0.0f, y = 0.0f;
    if (compact) {
        // The track row sits below the lanes; the count on its left, the buttons on its right, all centred on it.
        const ImVec2 wp = ImGui::GetWindowPos();
        const float padX = 6 * sc, rowY = wp.y + padY + float(lanes) * (laneH + laneGap), mid = rowY + trackH * 0.5f;
        const float right = wp.x + ImGui::GetWindowWidth() - padX;
        ImGui::SetCursorScreenPos(ImVec2(wp.x + padX, std::floor(mid - ImGui::GetTextLineHeight() * 0.5f)));
        ImGui::Text("%s %zu/%zu", ICON_LC_HISTORY, t->cursor(), n - 1);
        timelineSpans_.push_back({ImGui::GetItemRectMin().x, ImGui::GetItemRectMax().x});
        if (ImGui::IsItemHovered()) {
            std::string tip = std::string(tr("Sculpt Timeline")) + "\n";
            char buf[96];
            std::snprintf(buf, sizeof buf, tr("Step %zu of %zu"), t->cursor(), n - 1);
            tip += buf;
            if (t->cursor() > 0) tip += ": " + t->node(t->cursorNode()).label;
            if (!branches.empty()) {
                std::snprintf(buf, sizeof buf, tr(branches.size() == 1 ? "%zu branch" : "%zu branches"), branches.size());
                tip += std::string("\n") + buf;
            }
            ImGui::SetTooltip("%s", tip.c_str());
        }
        float left = ImGui::GetItemRectMax().x + 10 * sc;
        if (!s.message.empty()) { // what went wrong (or a branch switch): kept short so the track keeps its room
            const float w = std::min(ImGui::CalcTextSize(s.message.c_str()).x, (right - left) * 0.3f);
            ImGui::SetCursorScreenPos(ImVec2(left, std::floor(mid - ImGui::GetTextLineHeight() * 0.5f)));
            ImGui::PushClipRect(ImVec2(left, rowY), ImVec2(left + w, rowY + trackH), true);
            ImGui::TextColored(theme::textDim, "%s", s.message.c_str());
            ImGui::PopClipRect();
            if (ImGui::IsMouseHoveringRect(ImVec2(left, rowY), ImVec2(left + w, rowY + trackH)))
                ImGui::SetTooltip("%s", s.message.c_str());
            timelineSpans_.push_back({left, left + w});
            left += w + 10 * sc;
        }
        // Icon buttons, right to left: hide, full strip, timelapse, save with project (lit when on).
        const float b = trackH, gap = 2 * sc;
        float bx = right - 4 * b - 3 * gap;
        auto iconButton = [&](const char* id, const char* glyph, bool on) {
            ImGui::SetCursorScreenPos(ImVec2(bx, rowY));
            const bool clicked = ImGui::InvisibleButton(id, ImVec2(b, b));
            const ImVec2 a = ImGui::GetItemRectMin(), e = ImGui::GetItemRectMax();
            if (on || ImGui::IsItemHovered())
                dl->AddRectFilled(a, e, theme::u32(on ? theme::selection : theme::frameHover), theme::rounding(3 * sc));
            ui::CentredGlyph(dl, nullptr, a, e, theme::u32(on ? theme::accent : theme::text), glyph);
            bx += b + gap;
            return clicked;
        };
        if (iconButton("Save with project", ICON_LC_SAVE, s.saveHistory)) {
            s.saveHistory = !s.saveHistory;
            Timeline::saveWithProject = s.saveHistory;
            savePrefs(s, configDir_, uiConfigDeterministic_);
        }
        ui::HelpText(s.saveHistory ? "Save with project: on" : "Save with project: off", nullptr,
                     "Also save the history beside the project as <name>.sfhist (strokes, masks, vertex paint, visibility; "
                     "a topology change cuts it). Opening the project brings it back.",
                     "Edit > Sculpt Timeline");
        if (iconButton("##timelapse", ICON_LC_FILM, false)) s.timelapseDialog = true;
        ui::HelpText("Export timelapse", nullptr, "Render this history as a turntable timelapse video.", "Edit > Sculpt Timeline");
        if (iconButton("##timeline.full", ICON_LC_UNFOLD_VERTICAL, false)) {
            s.compact = false;
            savePrefs(s, configDir_, uiConfigDeterministic_);
        }
        ui::HelpText("Full timeline", nullptr,
                     "Show the timeline with its header row (Preferences > Interface > Compact timeline).",
                     "Edit > Sculpt Timeline");
        if (iconButton("##timeline.hide", top ? ICON_LC_CHEVRON_UP : ICON_LC_CHEVRON_DOWN, false)) {
            s.visible = false;
            savePrefs(s, configDir_, uiConfigDeterministic_);
        }
        ui::HelpText("Hide", nullptr, "Fold the timeline into a tab.", "Edit > Sculpt Timeline");
        timelineSpans_.push_back({right - 4 * b - 3 * gap, right});
        x0 = left + 6 * sc;
        x1 = right - 4 * b - 3 * gap - 12 * sc;
        y = wp.y + padY;
    } else {
        // Header row: what it is, where the cursor is, the controls. The controls are measured and right-aligned;
        // the text left of them is elided before it reaches them (a 1024 px window at 200 %), the step count last.
        const ImGuiStyle& st = ImGui::GetStyle();
        const float smallW = ImGui::CalcTextSize(ICON_LC_FILM).x + st.FramePadding.x * 2.0f; // SmallButton: same x padding
        const float checkW = ImGui::GetFrameHeight() + st.ItemInnerSpacing.x + ImGui::CalcTextSize(tr("Save with project")).x;
        const float controlsW = checkW + 3.0f * (smallW + st.ItemSpacing.x);
        const float controlsX = std::max(ImGui::GetCursorPosX(), ImGui::GetWindowWidth() - st.WindowPadding.x - controlsW);
        auto room = [&] { return controlsX - st.ItemSpacing.x - 8 * sc - (ImGui::GetItemRectMax().x - ImGui::GetWindowPos().x); };
        auto tail = [&](const std::string& text, const ImVec4* col) { // one more piece of the header, elided to the room left
            if (text.empty()) return;
            const float avail = room() - st.ItemSpacing.x;
            if (avail < ImGui::CalcTextSize("M...").x) return;
            const std::string shown = fitText(text, avail);
            ImGui::SameLine();
            if (col) ImGui::TextColored(*col, "%s", shown.c_str());
            else ImGui::TextDisabled("%s", shown.c_str());
            timelineSpans_.push_back({ImGui::GetItemRectMin().x, ImGui::GetItemRectMax().x});
        };
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(ICON_LC_HISTORY);
        timelineSpans_.push_back({ImGui::GetItemRectMin().x, ImGui::GetItemRectMax().x});
        {
            char stepText[64];
            std::snprintf(stepText, sizeof stepText, tr("Step %zu of %zu"), t->cursor(), n - 1);
            const std::string shown = fitText(stepText, std::max(0.0f, room() - st.ItemSpacing.x));
            if (!shown.empty()) {
                ImGui::SameLine();
                ImGui::TextUnformatted(shown.c_str());
                timelineSpans_.push_back({ImGui::GetItemRectMin().x, ImGui::GetItemRectMax().x});
            }
        }
        if (!branches.empty()) {
            char branchText[64];
            std::snprintf(branchText, sizeof branchText, tr(branches.size() == 1 ? "%s %zu branch" : "%s %zu branches"),
                          ICON_LC_GIT_BRANCH, branches.size());
            tail(branchText, nullptr);
        }
        if (const Timeline::Node& cur = t->node(t->cursorNode()); t->cursor() > 0) tail("  " + cur.label, nullptr);
        if (!s.message.empty()) tail("  " + s.message, &theme::textDim);
        ImGui::SameLine(controlsX);
        const float controlsStart = ImGui::GetCursorScreenPos().x;
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
        if (ImGui::SmallButton(ICON_LC_FOLD_VERTICAL "###timeline.compact")) {
            s.compact = true;
            savePrefs(s, configDir_, uiConfigDeterministic_);
        }
        ui::HelpText("Compact timeline", nullptr, "Fold the header row into the track's row: a thinner strip.",
                     "Edit > Sculpt Timeline");
        ImGui::SameLine();
        if (ImGui::SmallButton(top ? ICON_LC_CHEVRON_UP : ICON_LC_CHEVRON_DOWN)) {
            s.visible = false;
            savePrefs(s, configDir_, uiConfigDeterministic_);
        }
        ui::HelpText("Hide", nullptr, "Fold the timeline into a tab.", "Edit > Sculpt Timeline");
        timelineSpans_.push_back({controlsStart, ImGui::GetItemRectMax().x});
        // Branch lanes, then the track.
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        x0 = origin.x + 6 * sc;
        x1 = origin.x + ImGui::GetContentRegionAvail().x - 6 * sc;
        y = origin.y;
    }
    const float laneTop = y;
    size_t span = n > 1 ? n - 1 : 1; // steps the track shows: the active branch, or a longer other branch
    for (const Timeline::Branch& b : branches) span = std::max(span, b.from + b.length);
    const float step = (x1 - x0) / float(span);
    auto xOf = [&](double i) { return x0 + float(i) * step; };
    const ImVec2 mouse = ImGui::GetIO().MousePos;
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
        if (ImGui::InvisibleButton("##lane", ImVec2(std::max(8 * sc, bx1 - bx0 + 4 * sc), laneH + (compact ? laneGap : 0.0f)))) {
            std::string e;
            if (!t->select(doc_, b.tip, &e)) s.message = e;
            else s.message = "Switched to the other branch (the one you left is a lane now)";
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(tr(b.length == 1 ? "Branch from step %zu: %zu step, ending with \"%s\"\nClick to make it the active branch"
                                               : "Branch from step %zu: %zu steps, ending with \"%s\"\nClick to make it the active branch"),
                              b.from, b.length, t->node(b.tip).label.c_str());
        ImGui::PopID();
        y += laneH + laneGap;
    }
    if (branches.size() > size_t(lanes)) {
        dl->AddText(ImVec2(x1 - 30 * sc, laneTop), theme::u32(theme::textDim), ("+" + std::to_string(branches.size() - size_t(lanes))).c_str());
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
            ImGui::SetTooltip(tr("Step %d: %s (%s)\n%u %s, %s%s\nClick or drag to show the model here; right-click to re-tune"), d,
                              node.label.c_str(), kindLabel(node.kind), node.touched,
                              tr(node.kind == Timeline::Kind::Visibility ? "triangles" : "vertices"), ago(node.time).c_str(),
                              node.strength != 1.0f ? ("\n" + std::string(tr("Strength")) + " " + std::to_string(int(std::lround(node.strength * 100))) + "%").c_str() : "");
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
