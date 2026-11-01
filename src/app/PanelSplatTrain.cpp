// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Splats > Train from capture: a phone video or a folder of photos becomes a
// Gaussian splat in the viewport, sharpening while it trains. The job runs on
// a worker thread (ai/SplatTrainer.h: frames, COLMAP camera positions, Brush),
// loads each checkpoint there, and this file only swaps the newest one into
// the scene, so the view stays responsive (orbit while it trains). An overlay
// over the canvas shows the step, PSNR, elapsed and remaining time, Cancel and
// "Good enough - stop". Missing tools open a prompt that says what would be
// downloaded, from where, and how big it is (nothing downloads without a click).
#include "app/App.h"
#include "app/Ui.h"

#include "ai/SplatTrainer.h"
#include "core/SplatCapture.h"

#include <IconsLucide.h>

#include <atomic>
#include <cmath>
#include <filesystem>
#include <future>
#include <limits>
#include <mutex>

namespace sf {

namespace {

constexpr const char* kPath = "Splats > Train";
namespace fs = std::filesystem;

std::string utf8(const fs::path& p) {
    const std::u8string s = p.u8string();
    return std::string(s.begin(), s.end());
}
fs::path fsPath(const std::string& s) { return fs::path(std::u8string(s.begin(), s.end())); }

std::string clock(double seconds) {
    if (seconds < 0 || !std::isfinite(seconds)) return "--:--";
    const int s = int(seconds + 0.5);
    char buf[32];
    if (s >= 3600) std::snprintf(buf, sizeof buf, "%d:%02d:%02d", s / 3600, s / 60 % 60, s % 60);
    else std::snprintf(buf, sizeof buf, "%d:%02d", s / 60, s % 60);
    return buf;
}

std::string megabytes(long long bytes) { return std::to_string((bytes + 500'000) / 1'000'000) + " MB"; }

// Native dialog results (any thread).
enum class Pick { None, Video, Folder, Brush, Colmap };
std::mutex g_pickMutex;
std::vector<std::pair<Pick, std::string>> g_picked;
void SDLCALL onPicked(void* userdata, const char* const* files, int) {
    wakeMainLoop(); // a file dialog answered (maybe on another thread): redraw now
    if (!files || !files[0]) return;
    std::lock_guard lock(g_pickMutex);
    g_picked.emplace_back(Pick(reinterpret_cast<intptr_t>(userdata)), files[0]);
}
void* pickTag(Pick p) { return reinterpret_cast<void*>(intptr_t(p)); }

} // namespace

struct SplatTrainUi {
    ai::SplatTrainJob job;
    ai::SplatTrainSettings settings;
    std::string settingsPath; // "" = not saved (UI tests)
    bool active = false;      // started, not yet finished by pollSplatTrain
    int previews = 0;         // checkpoints shown in the viewport by this job
    std::string source;       // the capture being trained
    std::string preset = "fast";
    // Tools, looked up when the section is first drawn and after a download or a choice.
    bool toolsKnown = false;
    std::string brush, colmap, ffmpeg;
    // A start waiting for a tool: the download / choose prompt.
    bool prompt = false;
    bool promptOpened = false;
    Pick promptFor = Pick::Brush;
    std::string pendingSource, pendingOut;
    std::string promptError;
    // The running download.
    std::future<bool> download;
    std::shared_ptr<std::atomic<float>> downloadProgress = std::make_shared<std::atomic<float>>(0.0f);
    std::shared_ptr<std::atomic<bool>> downloadCancel = std::make_shared<std::atomic<bool>>(false);
    std::shared_ptr<std::string> downloadError = std::make_shared<std::string>();
    const ai::ToolDownload* downloading = nullptr;

    ~SplatTrainUi() { downloadCancel->store(true); } // quitting stops a download (the job stops itself)

    void findTools() {
        brush = ai::findBrush(settings);
        colmap = ai::findColmap(settings);
        ffmpeg = ai::findFfmpeg(settings);
        toolsKnown = true;
    }
    void save() {
        if (!settingsPath.empty()) settings.save(settingsPath);
    }
};

bool App::splatTrainBusy() const {
    return splatTrain_ && (splatTrain_->job.busy() || splatTrain_->active || splatTrain_->download.valid());
}

double App::splatTrainTestValue(const std::string& name) {
    const double none = std::numeric_limits<double>::quiet_NaN();
    if (!splatTrain_) return none;
    SplatTrainUi& s = *splatTrain_;
    if (name == "splat_training") return s.job.busy() || s.active ? 1.0 : 0.0;
    if (name == "splat_previews") return double(s.previews);
    if (name == "splat_train_prompt") return s.prompt ? 1.0 : 0.0;
    const ai::TrainStatus st = s.job.status();
    if (name == "splat_train_step") return double(st.step);
    if (name == "splat_train_stage") return double(int(st.stage)); // ai::TrainStage: 4 done, 5 failed, 6 cancelled
    if (name == "splat_train_psnr") return st.psnr;
    return none;
}

// Starts training or, when a tool is missing, opens the prompt for it.
static CommandResult startSplatTraining(SplatTrainUi& s, const std::string& source, const std::string& out,
                                        const std::string& preset) {
    if (s.job.busy() || s.active) return {false, "A splat is already training - Cancel it first"};
    s.findTools(); // the user may have installed one since
    ai::TrainRequest r;
    r.source = source;
    r.outDir = out;
    r.preset = preset.empty() ? s.preset : preset;
    r.brush = s.brush;
    r.colmap = s.colmap;
    r.ffmpeg = s.ffmpeg;
    std::string err;
    if (!s.job.start(r, err)) {
        const ai::TrainStatus st = s.job.status();
        if (st.missingTrainer || (st.missingPoses && ai::colmapDownload())) {
            s.prompt = true;
            s.promptOpened = false;
            s.promptFor = st.missingTrainer ? Pick::Brush : Pick::Colmap;
            s.pendingSource = source;
            s.pendingOut = out;
            s.promptError.clear();
            return {true, err + " - see the window that opened"};
        }
        if (st.missingPoses)
            err += ". Install COLMAP (Linux: your distribution's colmap package; macOS: brew install colmap) or "
                   "choose it in Splats > Train > Tools";
        return {false, err};
    }
    s.active = true;
    s.previews = 0;
    s.source = source;
    return {true, "Training a splat from " + utf8(fsPath(source).filename()) + " (" +
                      "the viewport updates as it learns)"};
}

void App::registerSplatTrainUi() {
    splatTrain_ = std::make_shared<SplatTrainUi>();
    SplatTrainUi& s = *splatTrain_;
    if (!uiConfigDeterministic_) { // UI tests never read or write the artist's settings
        s.settingsPath = ai::defaultSplatTrainSettingsPath();
        s.settings = ai::SplatTrainSettings::load(s.settingsPath);
    }
    s.preset = capture::trainPreset(s.settings.preset).id;

    using T = ParamSpec::Type;
    commands_.add({"splat_train",
                   "Train a Gaussian splat from a capture - a phone video, a folder of photos, a phone-link capture "
                   "folder, or a COLMAP / nerfstudio dataset - in the background. The splat appears in the viewport "
                   "after the first few hundred steps and sharpens as it trains; the result is saved next to the "
                   "capture and in the Library Splats tab. Uses Brush (downloaded on request), COLMAP for camera "
                   "positions and ffmpeg for video.",
                   {{"source", T::String, "The capture: a video file or a folder.", true},
                    {"out", T::String, "Folder for the result and work files. Default: next to the capture.", false},
                    {"preset", T::String, "fast (a quick preview, minutes) or quality (sharper, longer).", false}},
                   [this](Document&, const Args& a) -> CommandResult {
                       return startSplatTraining(*splatTrain_, argString(a, "source"), argString(a, "out"),
                                                 argString(a, "preset"));
                   }});
    commands_.add({"splat_train_cancel",
                   "Stop splat training. finish=true keeps the newest checkpoint as the result (Good enough - stop); "
                   "otherwise nothing is saved.",
                   {{"finish", T::Bool, "Keep what it has learned so far. Default false.", false}},
                   [this](Document&, const Args& a) -> CommandResult {
                       SplatTrainUi& st = *splatTrain_;
                       if (!st.job.busy()) return {true, "No splat is training"};
                       if (argBool(a, "finish", false)) {
                           st.job.finishNow();
                           return {true, "Stopping: keeping the newest checkpoint"};
                       }
                       st.job.cancel();
                       return {true, "Cancelling splat training"};
                   }});

    auto add = [this](const char* id, const char* label, const char* icon, std::function<void()> run,
                      std::function<bool()> enabled, const char* description, const char* command) {
        Action a;
        a.id = id;
        a.label = label;
        a.icon = icon;
        a.palette = kPath;
        a.run = std::move(run);
        a.enabled = std::move(enabled);
        a.description = description;
        a.command = command;
        actions_.add(std::move(a));
    };
    auto idle = [this] { return !splatTrainBusy(); };
    add("splats.train_video", "Video...", ICON_LC_VIDEO,
        [this] {
            static const SDL_DialogFileFilter filters[] = {{"Videos", "mp4;mov;m4v;webm;mkv;avi"}};
            SDL_ShowOpenFileDialog(onPicked, pickTag(Pick::Video), window_, filters, 1, nullptr, false);
        },
        idle,
        "Train a Gaussian splat from a video of an object (walk slowly all the way around it). The sharpest frame "
        "of each moment is used. The splat shows in the viewport after a few hundred steps and sharpens live.",
        "splat_train");
    add("splats.train_folder", "Photos Folder...", ICON_LC_FOLDER_OPEN,
        [this] { SDL_ShowOpenFolderDialog(onPicked, pickTag(Pick::Folder), window_, nullptr, false); }, idle,
        "Train a Gaussian splat from a folder: photos taken all around an object, a phone-link capture, or a COLMAP "
        "/ nerfstudio dataset (which already has camera positions).",
        "splat_train");
    add("splats.train_stop", "Good enough - stop", ICON_LC_CHECK,
        [this] { runCommand("splat_train_cancel", {{"finish", true}}); },
        [this] { return splatTrain_->job.busy(); },
        "Stop training now and keep what it has learned (the newest checkpoint) as the result.",
        "splat_train_cancel");
    add("splats.train_cancel", "Cancel", ICON_LC_X, [this] { runCommand("splat_train_cancel"); },
        [this] { return splatTrain_->job.busy(); }, "Stop training. Nothing is saved; the last preview stays in the viewport until you Clear the splats.", "splat_train_cancel");
    add("splats.train_choose_brush", "Choose Brush...", nullptr,
        [this] { SDL_ShowOpenFileDialog(onPicked, pickTag(Pick::Brush), window_, nullptr, 0, nullptr, false); }, nullptr,
        "Use a Brush trainer you already have (brush_app): for example one you built yourself with cargo on Linux "
        "ARM, where there is no ready-made download.",
        "");
    add("splats.train_choose_colmap", "Choose COLMAP...", nullptr,
        [this] { SDL_ShowOpenFileDialog(onPicked, pickTag(Pick::Colmap), window_, nullptr, 0, nullptr, false); },
        nullptr, "Use a COLMAP program you already have (it finds where each photo was taken).", "");
}

void App::pollSplatTrain() {
    if (!splatTrain_) return;
    SplatTrainUi& s = *splatTrain_;
    // Native dialogs.
    std::vector<std::pair<Pick, std::string>> picked;
    {
        std::lock_guard lock(g_pickMutex);
        picked.swap(g_picked);
    }
    for (auto& [kind, path] : picked) {
        if (kind == Pick::Video || kind == Pick::Folder) {
            runCommand("splat_train", {{"source", path}, {"preset", s.preset}});
        } else {
            (kind == Pick::Brush ? s.settings.brushPath : s.settings.colmapPath) = path;
            s.save();
            s.findTools();
            status_ = std::string(kind == Pick::Brush ? "Brush" : "COLMAP") + ": " + path;
            if (s.prompt && kind == s.promptFor) {
                s.prompt = false;
                runCommand("splat_train", {{"source", s.pendingSource}, {"out", s.pendingOut}});
            }
        }
    }
    // A download finishing.
    if (s.download.valid() && s.download.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        const bool ok = s.download.get();
        const char* tool = s.downloading ? s.downloading->tool : "tool";
        s.downloading = nullptr;
        s.findTools();
        if (ok) {
            status_ = std::string(tool) + " downloaded";
            s.prompt = false;
            runCommand("splat_train", {{"source", s.pendingSource}, {"out", s.pendingOut}});
        } else {
            s.promptError = *s.downloadError;
            status_ = *s.downloadError;
        }
    }
    if (!s.active) return;
    // The newest checkpoint: moved in, so swapping costs nothing here (the renderer uploads it next frame).
    if (std::shared_ptr<SplatCloud> cloud = s.job.takePreview()) {
        const bool first = s.previews == 0;
        if (!first) cloud->transform = doc_.splats.transform; // keep a Flip / Fit made while it trains
        cloud->name = utf8(fsPath(s.source).stem());
        doc_.splats = std::move(*cloud);
        doc_.splatsChanged();
        renderSettings_.showSplats = true;
        ++s.previews;
        if (first) {
            // A capture lands where the sculpt is (a new scene's sphere would hide it): show the splat alone; Show
            // mesh in the Splats palette brings the sculpt back.
            if (renderSettings_.showMesh) {
                renderSettings_.showMesh = false;
                status_ = "Showing the splat as it trains (Splats > Show mesh brings the sculpt back)";
            }
            camera_.frame(sceneBounds());
        }
    }
    if (s.job.busy()) return;
    s.active = false;
    const ai::TrainStatus st = s.job.status();
    if (st.stage == ai::TrainStage::Done) {
        status_ = "Splat trained: " + std::to_string(doc_.splats.size()) + " splats, saved to " + st.result;
        if (s.settings.copyToLibrary && !library_.user.empty() && !st.result.empty()) {
            std::error_code ec;
            const fs::path dir = fsPath(library_.user) / "Splats";
            fs::create_directories(dir, ec);
            fs::copy_file(fsPath(st.result), dir / fsPath(st.result).filename(), fs::copy_options::overwrite_existing, ec);
            if (!ec) libraryScanner_.invalidate();
        }
    } else if (st.stage == ai::TrainStage::Cancelled) {
        status_ = st.message;
    } else {
        status_ = "Splat training failed: " + st.error;
    }
}

void App::drawSplatTrainSection() {
    if (!splatTrain_) return;
    SplatTrainUi& s = *splatTrain_;
    if (!s.toolsKnown) s.findTools();
    ImGui::SeparatorText("Train from capture");
    const bool busy = splatTrainBusy();
    ImGui::BeginDisabled(busy);
    const auto& presets = capture::trainPresets();
    const float half = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
    for (size_t i = 0; i < presets.size(); ++i) {
        if (i) ImGui::SameLine();
        const bool on = s.preset == presets[i].id;
        if (on) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetColorU32(ui::color::accent));
        if (ImGui::Button(presets[i].label, ImVec2(half, 0))) {
            s.preset = presets[i].id;
            s.settings.preset = s.preset;
            s.save();
        }
        if (on) ImGui::PopStyleColor();
        ui::HelpText(presets[i].label, nullptr,
                     i == 0 ? "Fast preview: 5,000 steps at up to 1024 px, about 80 frames from a video. A usable "
                              "splat in minutes on a gaming GPU - good to check the capture."
                            : "Quality: 30,000 steps at up to 1920 px, up to 200 frames, full view-dependent colour. "
                              "Sharper; takes several times longer.",
                     kPath);
    }
    ImGui::EndDisabled();
    const float avail = ImGui::GetContentRegionAvail().x, gap = ImGui::GetStyle().ItemSpacing.x;
    ui::ActionButton(*this, "splats.train_video", ImVec2((avail - gap) * 0.5f, 0), ui::Look::IconLabel);
    ImGui::SameLine();
    ui::ActionButton(*this, "splats.train_folder", ImVec2((avail - gap) * 0.5f, 0), ui::Look::IconLabel);
    if (s.job.busy()) {
        ImGui::TextDisabled("Training - progress is over the viewport");
    } else if (s.brush.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextWrapped("Trainer: Brush is not installed yet (it is offered when you start).");
        ImGui::PopStyleColor();
    }
    if (ImGui::TreeNode("Tools##splattrain")) {
        auto row = [](const char* name, const std::string& path, const char* missing) {
            ImGui::TextUnformatted(name);
            ImGui::SameLine();
            if (path.empty()) ImGui::TextDisabled("%s", missing);
            else ImGui::TextDisabled("%s", utf8(fsPath(path).filename()).c_str());
            if (!path.empty() && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", path.c_str());
        };
        row("Brush", s.brush, "not installed");
        row("COLMAP", s.colmap, "not found (photos without positions need it)");
        row("ffmpeg", s.ffmpeg, "not found (videos need it)");
        ui::ActionButton(*this, "splats.train_choose_brush", ImVec2((avail - gap) * 0.5f, 0));
        ImGui::SameLine();
        ui::ActionButton(*this, "splats.train_choose_colmap", ImVec2(-1, 0));
        ImGui::TreePop();
    }
}

void App::drawSplatTrainOverlay() {
    if (!splatTrain_) return;
    SplatTrainUi& s = *splatTrain_;
    const CanvasRect c = canvasRect();
    const float scale = uiScale_;
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                                   ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                                   ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove;
    // The tool prompt: what is downloaded, from where, how big. Nothing happens without a click.
    if (s.prompt) {
        const ai::ToolDownload* d = s.promptFor == Pick::Brush ? ai::brushDownload() : ai::colmapDownload();
        const char* tool = s.promptFor == Pick::Brush ? "Brush" : "COLMAP";
        ImGui::SetNextWindowPos(ImVec2(c.x + c.w * 0.5f, c.y + c.h * 0.4f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(440 * scale, 0), ImGuiCond_Always);
        if (ImGui::Begin("##splat_tool_prompt", nullptr, flags & ~ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted(s.promptFor == Pick::Brush ? "Splat training needs Brush" : "Splat training needs COLMAP");
            ImGui::Spacing();
            if (s.promptFor == Pick::Brush)
                ImGui::TextWrapped("Brush is a free, open-source Gaussian splat trainer (Apache-2.0) that runs on your "
                                   "graphics card - NVIDIA, AMD, Intel or Apple, no CUDA needed. SculptForge runs it "
                                   "as a separate program.");
            else
                ImGui::TextWrapped("COLMAP (BSD licence) works out where each photo was taken, which training needs. "
                                   "SculptForge runs it as a separate program.");
            ImGui::Spacing();
            if (d) {
                ImGui::TextWrapped("Download %s %s (%s) from %s into %s. Its SHA-256 checksum is checked before it is "
                                   "used.",
                                   d->tool, d->version, megabytes(d->bytes).c_str(), d->url, ai::splatToolsDir().c_str());
            } else {
                ImGui::TextWrapped("There is no ready-made %s for this computer. %s", tool,
                                   s.promptFor == Pick::Brush
                                       ? "Build it with Rust (cargo build --release in a Brush checkout) and choose "
                                         "brush_app below, or set SCULPTFORGE_BRUSH."
                                       : "Install it (Linux: the colmap package; macOS: brew install colmap) or choose it.");
            }
            if (!s.promptError.empty()) ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.35f, 1), "%s", s.promptError.c_str());
            ImGui::Spacing();
            if (s.download.valid()) {
                ImGui::ProgressBar(s.downloadProgress->load(), ImVec2(-1, 0), "Downloading...");
                if (ImGui::Button("Stop download")) s.downloadCancel->store(true);
            } else {
                if (d) {
                    const std::string label = "Download " + std::string(d->tool) + " (" + megabytes(d->bytes) + ")";
                    if (ImGui::Button(label.c_str())) {
                        s.downloading = d;
                        s.downloadProgress->store(0.0f);
                        s.downloadCancel->store(false);
                        s.promptError.clear();
                        auto progress = s.downloadProgress;
                        auto cancel = s.downloadCancel;
                        auto error = s.downloadError;
                        s.download = std::async(std::launch::async, [d, progress, cancel, error] {
                            std::string exe;
                            return ai::downloadTool(*d, exe, *error, progress.get(), cancel.get());
                        });
                    }
                    ui::HelpText(label.c_str(), nullptr, "Download it once from its GitHub release; nothing else is sent.",
                                 kPath);
                    ImGui::SameLine();
                }
                ui::ActionButton(*this, s.promptFor == Pick::Brush ? "splats.train_choose_brush" : "splats.train_choose_colmap");
                ImGui::SameLine();
                if (ImGui::Button("Project page")) SDL_OpenURL(d ? d->home
                                                                : s.promptFor == Pick::Brush ? "https://github.com/ArthurBrussee/brush"
                                                                                             : "https://colmap.github.io/install.html");
                ImGui::SameLine();
                if (ImGui::Button("Not now")) s.prompt = false;
            }
        }
        ImGui::End();
    }

    if (!s.active) return;
    const ai::TrainStatus st = s.job.status();
    ImGui::SetNextWindowPos(ImVec2(c.x + c.w * 0.5f, c.y + c.h - 16.0f * scale), ImGuiCond_Always, ImVec2(0.5f, 1.0f));
    ImGui::SetNextWindowBgAlpha(0.92f);
    ImGui::SetNextWindowSize(ImVec2(400 * scale, 0), ImGuiCond_Always);
    if (ImGui::Begin("##splat_train_overlay", nullptr, flags & ~ImGuiWindowFlags_AlwaysAutoResize)) {
        const capture::TrainPreset& preset = capture::trainPreset(s.preset);
        ImGui::Text("Training splat - %s", preset.label);
        ImGui::SameLine();
        ImGui::TextDisabled("%s", utf8(fsPath(s.source).filename()).c_str());
        if (st.stage == ai::TrainStage::Training && st.step > 0) {
            char bar[64];
            std::snprintf(bar, sizeof bar, "%d / %d steps", st.step, st.totalSteps);
            ImGui::ProgressBar(st.totalSteps ? float(st.step) / float(st.totalSteps) : 0.0f, ImVec2(-1, 0), bar);
            std::string line;
            if (st.psnr > 0) {
                char p[64];
                std::snprintf(p, sizeof p, "PSNR %.1f dB (%s)", st.psnr, capture::psnrQuality(st.psnr));
                line = p;
            } else {
                line = "Quality: measured soon";
            }
            if (st.splats > 0) line += "   " + std::to_string((st.splats + 500) / 1000) + "k splats";
            ImGui::TextUnformatted(line.c_str());
            ImGui::TextDisabled("%s elapsed, about %s left", clock(st.elapsed).c_str(), clock(st.eta).c_str());
        } else {
            const float f = st.stageProgress;
            ImGui::ProgressBar(f >= 0 ? f : float(std::fmod(ImGui::GetTime() * 0.4, 1.0)), ImVec2(-1, 0),
                               st.message.c_str());
            const std::string photos = st.frames ? "  -  " + std::to_string(st.frames) + " photos" : std::string();
            ImGui::TextDisabled("%s elapsed%s", clock(st.elapsed).c_str(), photos.c_str());
        }
        const float avail = ImGui::GetContentRegionAvail().x, gap = ImGui::GetStyle().ItemSpacing.x;
        ImGui::BeginDisabled(st.stage != ai::TrainStage::Training || s.previews == 0);
        ui::ActionButton(*this, "splats.train_stop", ImVec2((avail - gap) * 0.6f, 0), ui::Look::IconLabel);
        ImGui::EndDisabled();
        ImGui::SameLine();
        ui::ActionButton(*this, "splats.train_cancel", ImVec2(-1, 0), ui::Look::IconLabel);
    }
    ImGui::End();
}

} // namespace sf
