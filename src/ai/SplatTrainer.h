// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Training a Gaussian splat from a phone capture, in the background, with a
// live preview. The heavy lifting is done by external, permissively licensed
// tools that SculptForge runs as separate processes (nothing is linked in):
//  - Brush (Apache-2.0, github.com/ArthurBrussee/brush): the trainer. WebGPU
//    via wgpu, so NVIDIA, AMD, Intel and Apple GPUs without CUDA. Downloaded on
//    request from its GitHub release (checksum pinned below) or a path you set.
//  - COLMAP (BSD-3-Clause, github.com/colmap/colmap): camera positions when the
//    capture has none (its global mapper is the former GLOMAP). Downloaded on
//    request on Windows/macOS; on Linux from the distribution or a path.
//  - ffmpeg (on PATH, or a path): frames from a video.
//
// The job: frames (video -> the sharpest frame of each stretch) -> camera
// positions (COLMAP, skipped for datasets and AR-posed phone captures) ->
// Brush, which writes a checkpoint every few hundred steps. The worker thread
// loads each new checkpoint, so the main thread only swaps it in.
#pragma once

#include "core/Commands.h"
#include "core/SplatCapture.h"
#include "core/Splats.h"

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace sf::ai {

// Tool paths and the preset, kept in <app data>/splat_train.json (shared by the app and sculptforge-cli).
struct SplatTrainSettings {
    std::string brushPath;  // "" = find it (environment, download, PATH)
    std::string colmapPath;
    std::string ffmpegPath;
    std::string preset = "fast";
    bool copyToLibrary = true; // also put the result in the Library Splats tab

    static SplatTrainSettings load(const std::string& path);
    bool save(const std::string& path) const;
};
std::string defaultSplatTrainSettingsPath();
std::string splatToolsDir(); // <app data>/tools

// A tool release SculptForge can download for this platform.
struct ToolDownload {
    const char* tool;    // "Brush", "COLMAP"
    const char* version; // "0.3.0"
    const char* url;
    const char* sha256;
    long long bytes;
    const char* exe;     // the program inside the archive, relative to its top folder
    const char* folder;  // where it goes under splatToolsDir()
    const char* licence;
    const char* home;    // project page shown in the prompt
};
const ToolDownload* brushDownload();  // nullptr: no prebuilt Brush for this platform (e.g. Linux on ARM)
const ToolDownload* colmapDownload(); // nullptr on Linux (install it from the distribution)

// Where the tools are: the SCULPTFORGE_BRUSH / SCULPTFORGE_COLMAP / SCULPTFORGE_FFMPEG environment variables,
// then the path in the settings, then a download in splatToolsDir(), then PATH. "" when missing.
std::string findBrush(const SplatTrainSettings& settings);
std::string findColmap(const SplatTrainSettings& settings);
std::string findFfmpeg(const SplatTrainSettings& settings);

// Downloads, checks the SHA-256 and unpacks (with the system's tar, which also reads .zip on Windows 10+).
// Blocking; run on a worker thread. `exePath` receives the unpacked program.
bool downloadTool(const ToolDownload& tool, std::string& exePath, std::string& error,
                  std::atomic<float>* progress = nullptr, std::atomic<bool>* cancel = nullptr);

enum class TrainStage { Idle, Frames, Poses, Training, Done, Failed, Cancelled };
const char* trainStageName(TrainStage stage);

struct TrainRequest {
    std::string source;  // a video, a folder of photos, a phone capture folder, or a COLMAP/nerfstudio dataset
    std::string outDir;  // "" = next to the capture (see defaultTrainOutDir)
    std::string preset = "fast";
    std::string brush, colmap, ffmpeg; // resolved tool paths
    bool evaluate = true;              // hold back views to measure PSNR
};
// <capture folder>/splat for a folder, <folder>/<video name>-splat for a video.
std::string defaultTrainOutDir(const std::string& source);

struct TrainStatus {
    TrainStage stage = TrainStage::Idle;
    std::string message;  // what it is doing now
    std::string error;    // why it failed
    bool missingTrainer = false, missingPoses = false, missingFfmpeg = false;
    int step = 0, totalSteps = 0;
    int splats = 0;
    double psnr = -1.0;   // dB on the held-back views, -1 before the first evaluation
    double elapsed = 0.0; // seconds since start
    double eta = -1.0;    // seconds left in training, -1 unknown
    float stageProgress = -1.0f; // 0..1 inside Frames / Poses, -1 unknown
    int frames = 0;              // photos used
    int previewVersion = 0;      // bumps with each loaded checkpoint
    int previewStep = -1;
    std::string result;   // the final .ply
    std::string outDir, logPath;
};

class SplatTrainJob {
public:
    SplatTrainJob() = default;
    ~SplatTrainJob(); // cancels and joins
    SplatTrainJob(const SplatTrainJob&) = delete;
    SplatTrainJob& operator=(const SplatTrainJob&) = delete;

    // Starts the worker; false (and `error`) when one is running or the source can't be used.
    bool start(const TrainRequest& request, std::string& error);
    void cancel();    // stop; the job ends Cancelled
    void finishNow(); // "Good enough": stop training and keep the newest checkpoint as the result
    bool busy() const { return running_.load(); }
    TrainStatus status() const;
    // The newest loaded checkpoint not taken yet, placed by the capture's framing (nullptr when none). Taking
    // hands it over, so the caller can move it into the scene without a copy.
    std::shared_ptr<SplatCloud> takePreview();
    void wait(); // joins the worker

    // Seconds between checks of the trainer's output (tests make it faster).
    double pollSeconds = 0.25;

private:
    void run(TrainRequest request);
    bool prepareFrames(const TrainRequest& r, capture::SourceKind kind, std::string& dataset, bool& needPoses);
    bool findPoses(const TrainRequest& r, const std::string& dataset);
    bool train(const TrainRequest& r, const std::string& dataset);
    bool loadCheckpoint(const std::string& path, int step);
    void fail(const std::string& error);
    template <class F> void update(F&& f) {
        std::lock_guard lock(mutex_);
        f(status_);
    }
    bool stopRequested() const { return cancel_.load() || finish_.load(); }

    std::thread worker_;
    std::atomic<bool> running_{false}, cancel_{false}, finish_{false};
    mutable std::mutex mutex_;
    TrainStatus status_;
    std::shared_ptr<SplatCloud> preview_;
    glm::mat4 framing_{1.0f};
    double startTime_ = 0.0;
};

// splat_train / splat_train_cancel for sculptforge-cli, Python and MCP: training blocks until it finishes and
// the result is loaded as the scene's splats. The app registers its own background versions (live preview).
void registerSplatTrainCommands(CommandRegistry& registry, std::function<SplatTrainSettings()> settings);

} // namespace sf::ai
