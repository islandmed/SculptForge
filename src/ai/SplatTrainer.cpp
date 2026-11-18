// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "ai/SplatTrainer.h"

#include "ai/Http.h"
#include "ai/Process.h"
#include "core/BrushLibrary.h"
#include "core/Commands.h"
#include "core/Json.h"
#include "core/Sha256.h"
#include "io/PlyIO.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace sf::ai {

namespace fs = std::filesystem;
using namespace sf::capture;

namespace {

fs::path u8path(const std::string& s) { return fs::path(std::u8string(s.begin(), s.end())); }
std::string pathString(const fs::path& p) {
    const auto s = p.u8string();
    return std::string(s.begin(), s.end());
}
double nowSeconds() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
void sleepSeconds(double s) { std::this_thread::sleep_for(std::chrono::milliseconds(int(s * 1000.0))); }
std::string envValue(const char* name) {
    const char* v = std::getenv(name);
    return v ? std::string(v) : std::string();
}
bool readFile(const std::string& path, std::string& out) {
    std::ifstream f(u8path(path), std::ios::binary);
    if (!f) return false;
    std::stringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}
// The last `lines` lines of a log, for error messages.
std::string logTail(const std::string& path, int lines) {
    std::string text;
    if (!readFile(path, text)) return {};
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) text.pop_back();
    size_t pos = text.size();
    for (int i = 0; i < lines && pos != std::string::npos && pos > 0; ++i) pos = text.rfind('\n', pos - 1);
    return pos == std::string::npos ? text : text.substr(pos + 1);
}
// "[12/80]" style progress anywhere in the text: the last one.
float bracketProgress(const std::string& text) {
    for (size_t p = text.rfind('['); p != std::string::npos; p = p ? text.rfind('[', p - 1) : std::string::npos) {
        int a = 0, b = 0;
        if (std::sscanf(text.c_str() + p, "[%d/%d]", &a, &b) == 2 && b > 0) return std::clamp(float(a) / float(b), 0.0f, 1.0f);
        if (p == 0) break;
    }
    return -1.0f;
}
std::string toolInDir(const fs::path& dir, const std::string& exe) {
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) return {};
    for (fs::recursive_directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
        if (it->is_regular_file(ec) && it->path().filename() == u8path(exe)) return pathString(it->path());
    return {};
}
std::string firstOnPath(std::initializer_list<const char*> names) {
    for (const char* n : names)
        if (std::string p = findOnPath(n); !p.empty()) return p;
    return {};
}
std::string captureName(const std::string& source) {
    fs::path p = u8path(source);
    while (!p.empty() && p.filename().empty()) p = p.parent_path(); // trailing separator
    std::error_code ec;
    const std::string name = pathString(fs::is_directory(p, ec) ? p.filename() : p.stem());
    return name.empty() ? std::string("capture") : name;
}

} // namespace

// --- Settings ---

std::string defaultSplatTrainSettingsPath() { return pathString(u8path(appDataDir()) / "splat_train.json"); }
std::string splatToolsDir() { return pathString(u8path(appDataDir()) / "tools"); }

SplatTrainSettings SplatTrainSettings::load(const std::string& path) {
    SplatTrainSettings s;
    std::string text, error;
    Json j;
    if (!readFile(path, text) || !Json::parse(text, j, error) || !j.isObject()) return s;
    if (j["brush"].isString()) s.brushPath = j["brush"].asString();
    if (j["colmap"].isString()) s.colmapPath = j["colmap"].asString();
    if (j["ffmpeg"].isString()) s.ffmpegPath = j["ffmpeg"].asString();
    if (j["preset"].isString()) s.preset = j["preset"].asString();
    s.copyToLibrary = j["copy_to_library"].asBool(true);
    return s;
}

bool SplatTrainSettings::save(const std::string& path) const {
    Json j = Json::object();
    j.set("brush", brushPath);
    j.set("colmap", colmapPath);
    j.set("ffmpeg", ffmpegPath);
    j.set("preset", preset);
    j.set("copy_to_library", copyToLibrary);
    std::error_code ec;
    fs::create_directories(u8path(path).parent_path(), ec);
    std::ofstream f(u8path(path), std::ios::binary);
    f << j.dump(true);
    return bool(f);
}

// --- Tools ---

const ToolDownload* brushDownload() {
    // Brush v0.3.0 (2025-09-14); hashes from the release's sha256.sum.
#if defined(_WIN32) && (defined(_M_X64) || defined(__x86_64__))
    static const ToolDownload d{"Brush", "0.3.0",
                                "https://github.com/ArthurBrussee/brush/releases/download/v0.3.0/brush-app-x86_64-pc-windows-msvc.zip",
                                "b68e3e9cf052d51bf3ee30776fa5a364de7f2ba13b58443128ff797bb7bcfcd6", 158791348, "brush_app.exe",
                                "brush-0.3.0", "Apache-2.0", "https://github.com/ArthurBrussee/brush"};
    return &d;
#elif defined(__APPLE__) && defined(__aarch64__)
    static const ToolDownload d{"Brush", "0.3.0",
                                "https://github.com/ArthurBrussee/brush/releases/download/v0.3.0/brush-app-aarch64-apple-darwin.tar.xz",
                                "65b2631398c839be3c1d4d7160fe2326389dec87830aac0710985e6690a1048c", 41447012, "brush_app",
                                "brush-0.3.0", "Apache-2.0", "https://github.com/ArthurBrussee/brush"};
    return &d;
#elif defined(__linux__) && defined(__x86_64__)
    static const ToolDownload d{"Brush", "0.3.0",
                                "https://github.com/ArthurBrussee/brush/releases/download/v0.3.0/brush-app-x86_64-unknown-linux-gnu.tar.xz",
                                "4f0f9a8785d1951c62df26aae247c02c5bba32b00f40b06df4e1c9b867399e20", 44242224, "brush_app",
                                "brush-0.3.0", "Apache-2.0", "https://github.com/ArthurBrussee/brush"};
    return &d;
#else
    return nullptr; // no prebuilt release (Linux on ARM, Intel Macs): build it with cargo and set the path
#endif
}

const ToolDownload* colmapDownload() {
#if defined(_WIN32) && (defined(_M_X64) || defined(__x86_64__))
    // COLMAP 4.2.1 (2026-09-29) without CUDA (runs everywhere); hash from GitHub's asset digest.
    static const ToolDownload d{"COLMAP", "4.2.1",
                                "https://github.com/colmap/colmap/releases/download/4.2.1/colmap-x64-windows-nocuda.zip",
                                "c493d88cb4a43f21afba42cb55bf786e5434a0420e73db0ba1454c5d8b3686ae", 127731595, "colmap.exe",
                                "colmap-4.2.1", "BSD-3-Clause", "https://github.com/colmap/colmap"};
    return &d;
#else
    return nullptr; // Linux: the distribution's colmap package; macOS: Homebrew (brew install colmap)
#endif
}

std::string findBrush(const SplatTrainSettings& settings) {
    if (std::string e = envValue("SCULPTFORGE_BRUSH"); !e.empty()) return findOnPath(e);
    if (!settings.brushPath.empty()) return findOnPath(settings.brushPath);
    if (const ToolDownload* d = brushDownload())
        if (std::string p = toolInDir(u8path(splatToolsDir()) / d->folder, d->exe); !p.empty()) return p;
    return firstOnPath({"brush_app", "brush"});
}

std::string findColmap(const SplatTrainSettings& settings) {
    if (std::string e = envValue("SCULPTFORGE_COLMAP"); !e.empty()) return findOnPath(e);
    if (!settings.colmapPath.empty()) return findOnPath(settings.colmapPath);
    if (const ToolDownload* d = colmapDownload())
        if (std::string p = toolInDir(u8path(splatToolsDir()) / d->folder, d->exe); !p.empty()) return p;
    return firstOnPath({"colmap"});
}

std::string findFfmpeg(const SplatTrainSettings& settings) {
    if (std::string e = envValue("SCULPTFORGE_FFMPEG"); !e.empty()) return findOnPath(e);
    if (!settings.ffmpegPath.empty()) return findOnPath(settings.ffmpegPath);
    return findFfmpegExe(); // (PATH, then Homebrew's, winget's ... folders: a desktop-launched app's PATH misses them)
}

bool downloadTool(const ToolDownload& tool, std::string& exePath, std::string& error, std::atomic<float>* progress,
                  std::atomic<bool>* cancel) {
    const fs::path dir = u8path(splatToolsDir()) / tool.folder;
    std::error_code ec;
    fs::create_directories(dir, ec);
    if (ec) {
        error = "Cannot create " + pathString(dir);
        return false;
    }
    std::string data;
    data.reserve(size_t(tool.bytes));
    const HttpResponse r = httpRequestStream("GET", tool.url, {}, {}, 1800, [&](const char* p, size_t n) {
        data.append(p, n);
        if (progress) progress->store(std::min(0.99f, float(double(data.size()) / double(tool.bytes))));
        return !(cancel && cancel->load());
    });
    if (cancel && cancel->load()) {
        error = "Download cancelled";
        return false;
    }
    if (r.status != 200) {
        error = "Downloading " + std::string(tool.tool) + " failed: " +
                (r.error.empty() ? "HTTP " + std::to_string(r.status) : r.error);
        return false;
    }
    if (sha256Hex(data) != tool.sha256) {
        error = std::string(tool.tool) + " download did not match its checksum (not installed)";
        return false;
    }
    const std::string name = std::string(tool.url).substr(std::string(tool.url).rfind('/') + 1);
    const fs::path archive = dir / u8path(name);
    {
        std::ofstream f(archive, std::ios::binary);
        f.write(data.data(), std::streamsize(data.size()));
        if (!f) {
            error = "Cannot write " + pathString(archive);
            return false;
        }
    }
    data = std::string();
    std::string output;
    const int code = runProcess("tar", {"-xf", pathString(archive), "-C", pathString(dir)}, output,
                                pathString(dir / "unpack"));
    fs::remove(archive, ec);
    fs::remove_all(dir / "unpack", ec);
    if (code != 0) {
        error = "Unpacking " + std::string(tool.tool) + " failed (tar): " + output;
        return false;
    }
    exePath = toolInDir(dir, tool.exe);
    if (exePath.empty()) {
        error = std::string(tool.tool) + " unpacked, but " + tool.exe + " is not in it";
        return false;
    }
#if !defined(_WIN32)
    fs::permissions(u8path(exePath), fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec,
                    fs::perm_options::add, ec);
#endif
    if (progress) progress->store(1.0f);
    return true;
}

// --- The job ---

const char* trainStageName(TrainStage stage) {
    switch (stage) {
    case TrainStage::Idle: return "idle";
    case TrainStage::Frames: return "frames";
    case TrainStage::Poses: return "poses";
    case TrainStage::Training: return "training";
    case TrainStage::Done: return "done";
    case TrainStage::Failed: return "failed";
    case TrainStage::Cancelled: return "cancelled";
    }
    return "?";
}

std::string defaultTrainOutDir(const std::string& source) {
    const fs::path p = u8path(source);
    std::error_code ec;
    if (fs::is_directory(p, ec)) return pathString(p / "splat");
    return pathString(p.parent_path() / u8path(captureName(source) + "-splat"));
}

SplatTrainJob::~SplatTrainJob() {
    cancel();
    wait();
}

void SplatTrainJob::wait() {
    if (worker_.joinable()) worker_.join();
}

void SplatTrainJob::cancel() { cancel_ = true; }
void SplatTrainJob::finishNow() { finish_ = true; }

TrainStatus SplatTrainJob::status() const {
    std::lock_guard lock(mutex_);
    TrainStatus s = status_;
    if (running_) s.elapsed = nowSeconds() - startTime_;
    return s;
}

std::shared_ptr<SplatCloud> SplatTrainJob::takePreview() {
    std::lock_guard lock(mutex_);
    return std::move(preview_);
}

void SplatTrainJob::fail(const std::string& error) {
    update([&](TrainStatus& s) {
        s.stage = TrainStage::Failed;
        s.error = error;
        s.message = error;
    });
}

bool SplatTrainJob::start(const TrainRequest& request, std::string& error) {
    if (running_) {
        error = "A splat is already training (cancel it first)";
        return false;
    }
    wait(); // the previous worker has ended; join it
    TrainStatus s;
    s.stage = TrainStage::Failed;
    const SourceKind kind = classifySource(request.source);
    bool posed = kind == SourceKind::NerfstudioDataset || kind == SourceKind::ColmapDataset;
    if (kind == SourceKind::Images) {
        CaptureInfo info;
        std::string ignored;
        const std::string json = pathString(u8path(request.source) / "capture.json");
        posed = readCaptureJson(json, info, ignored) && !info.poses.empty();
    }
    if (kind == SourceKind::Missing) error = "Capture not found: " + request.source;
    else if (kind == SourceKind::Unsupported)
        error = "Not a capture: give a video, a folder of photos, or a COLMAP / nerfstudio dataset";
    else if (request.brush.empty()) {
        error = "Brush (the splat trainer) is not installed";
        s.missingTrainer = true;
    } else if ((kind == SourceKind::Video || kind == SourceKind::VideoFolder) && request.ffmpeg.empty()) {
        error = "ffmpeg is needed to read frames from a video: install it, or train from a folder of photos";
        s.missingFfmpeg = true;
    } else if (!posed && request.colmap.empty()) {
        error = "COLMAP is needed to find the camera positions of the photos";
        s.missingPoses = true;
    }
    if (!error.empty()) {
        s.error = s.message = error;
        std::lock_guard lock(mutex_);
        status_ = s;
        return false;
    }
    cancel_ = false;
    finish_ = false;
    {
        std::lock_guard lock(mutex_);
        status_ = TrainStatus();
        status_.stage = TrainStage::Frames;
        status_.message = "Starting";
        status_.outDir = request.outDir.empty() ? defaultTrainOutDir(request.source) : request.outDir;
        preview_.reset();
    }
    framing_ = glm::mat4(1.0f);
    startTime_ = nowSeconds();
    running_ = true;
    TrainRequest r = request;
    if (r.outDir.empty()) r.outDir = defaultTrainOutDir(request.source);
    worker_ = std::thread([this, r] {
        run(r);
        update([&](TrainStatus& st) { st.elapsed = nowSeconds() - startTime_; });
        running_ = false;
    });
    return true;
}

void SplatTrainJob::run(TrainRequest r) {
    std::error_code ec;
    const fs::path out = u8path(r.outDir), work = out / "work";
    fs::create_directories(work, ec);
    if (ec) return fail("Cannot create " + pathString(work));
    const SourceKind kind = classifySource(r.source);
    std::string dataset;
    bool needPoses = false;
    if (!prepareFrames(r, kind, dataset, needPoses)) return;
    if (needPoses && !findPoses(r, dataset)) return;
    if (stopRequested()) {
        update([](TrainStatus& s) {
            s.stage = TrainStage::Cancelled;
            s.message = "Cancelled";
        });
        return;
    }
    train(r, dataset);
}

bool SplatTrainJob::prepareFrames(const TrainRequest& r, SourceKind kind, std::string& dataset, bool& needPoses) {
    const fs::path work = u8path(r.outDir) / "work";
    const TrainPreset& preset = trainPreset(r.preset);
    std::error_code ec;
    needPoses = false;
    if (kind == SourceKind::NerfstudioDataset || kind == SourceKind::ColmapDataset) {
        dataset = r.source;
        std::vector<CameraView> views;
        std::string err;
        const bool ok = kind == SourceKind::NerfstudioDataset
                            ? readTransformsJson(pathString(u8path(r.source) / "transforms.json"), views, err)
                            : readColmapImages(pathString(u8path(r.source) / "sparse" / "0"), views, err);
        if (ok) framing_ = orbitFraming(views);
        update([&](TrainStatus& s) { s.frames = int(views.size()); });
        return true;
    }
    const fs::path ds = work / "dataset", images = ds / "images";
    fs::remove_all(ds, ec);
    fs::create_directories(images, ec);
    dataset = pathString(ds);

    std::vector<std::string> frames; // candidate photos
    fs::path framesDir;
    CaptureInfo info;
    if (kind == SourceKind::Video || kind == SourceKind::VideoFolder) {
        const std::string video = kind == SourceKind::Video ? r.source : findVideo(r.source);
        framesDir = work / "frames";
        fs::remove_all(framesDir, ec);
        fs::create_directories(framesDir, ec);
        update([&](TrainStatus& s) {
            s.stage = TrainStage::Frames;
            s.message = "Reading frames from the video";
            s.stageProgress = -1.0f;
        });
        const std::string log = pathString(work / "ffmpeg.log");
        fs::remove(u8path(log), ec);
        ChildProcess ff;
        std::string err;
        // 5 frames a second, at most 1600 px (phones film 4K; Brush trains at <= 1920 anyway).
        if (!ff.start(r.ffmpeg,
                      {"-hide_banner", "-loglevel", "error", "-y", "-i", video, "-vf",
                       "fps=5,scale=1600:1600:force_original_aspect_ratio=decrease", "-q:v", "2",
                       pathString(framesDir / "%05d.jpg")},
                      log, {}, err)) {
            fail(err);
            return false;
        }
        while (ff.running()) {
            if (stopRequested()) {
                ff.kill();
                break;
            }
            const size_t n = listImages(pathString(framesDir)).size();
            update([&](TrainStatus& s) { s.message = "Reading frames from the video (" + std::to_string(n) + ")"; });
            sleepSeconds(pollSeconds);
        }
        if (stopRequested()) return true; // run() reports the cancel
        frames = listImages(pathString(framesDir));
        if (ff.exitCode() != 0 || frames.empty()) {
            fail("ffmpeg could not read the video: " + logTail(log, 3));
            return false;
        }
    } else { // photos
        const fs::path src = u8path(r.source);
        frames = listImages(r.source);
        if (frames.empty()) frames = listImages(pathString(src / "images"));
        std::string ignored;
        readCaptureJson(pathString(src / "capture.json"), info, ignored);
    }

    // The sharpest frame of each stretch (videos), or all photos up to twice the preset's frame count.
    const size_t keep = kind == SourceKind::Images ? size_t(preset.maxFrames) * 2 : size_t(preset.maxFrames);
    std::vector<size_t> chosen;
    const bool posedPhotos = !info.poses.empty();
    if (frames.size() > keep && !posedPhotos) {
        std::vector<double> scores(frames.size(), 0.0);
        for (size_t i = 0; i < frames.size(); ++i) {
            if (stopRequested()) return true;
            Image img;
            std::string err;
            if (Image::load(frames[i], img, err)) scores[i] = sharpness(img);
            update([&](TrainStatus& s) {
                s.message = "Picking the sharpest frames";
                s.stageProgress = float(i + 1) / float(frames.size());
            });
        }
        chosen = selectFrames(scores, keep);
    } else {
        for (size_t i = 0; i < frames.size(); ++i) chosen.push_back(i);
    }
    std::vector<std::string> names;
    for (size_t k = 0; k < chosen.size(); ++k) {
        const fs::path from = u8path(frames[chosen[k]]);
        // Video frames are renumbered; photos keep their names (AR poses refer to them).
        const fs::path name = framesDir.empty() ? from.filename() : u8path(std::to_string(100000 + k).substr(1) + ".jpg");
        if (!framesDir.empty()) fs::rename(from, images / name, ec);
        if (framesDir.empty() || ec) fs::copy_file(from, images / name, fs::copy_options::overwrite_existing, ec);
        if (ec) {
            fail("Cannot copy " + pathString(from) + " into the dataset");
            return false;
        }
        names.push_back(pathString(name));
    }
    if (!framesDir.empty()) fs::remove_all(framesDir, ec);
    update([&](TrainStatus& s) { s.frames = int(names.size()); });
    if (names.size() < 3) {
        fail("Only " + std::to_string(names.size()) + " usable photos: a splat needs many views (walk around the object)");
        return false;
    }

    if (posedPhotos) { // the phone's AR tracking already knows where each photo was taken
        std::vector<CameraView> views;
        for (CameraView v : info.poses) {
            const std::string base = pathString(u8path(v.file).filename());
            if (std::find(names.begin(), names.end(), base) == names.end()) continue;
            v.file = "images/" + base;
            views.push_back(std::move(v));
        }
        std::string err;
        if (views.size() >= 3 && writeTransformsJson(pathString(ds / "transforms.json"), views, err)) {
            framing_ = orbitFraming(views);
            return true;
        }
        if (r.colmap.empty()) {
            fail("The capture's camera poses don't match its photos, and COLMAP is not installed");
            return false;
        }
    }
    needPoses = true;
    return true;
}

bool SplatTrainJob::findPoses(const TrainRequest& r, const std::string& dataset) {
    const fs::path ds = u8path(dataset), work = u8path(r.outDir) / "work";
    const TrainPreset& preset = trainPreset(r.preset);
    std::error_code ec;
    update([&](TrainStatus& s) {
        s.stage = TrainStage::Poses;
        s.message = "Finding the camera positions (COLMAP)";
        s.stageProgress = -1.0f;
    });
    const std::string log = pathString(work / "colmap.log");
    fs::remove(u8path(log), ec);
    const std::vector<std::string> base = {"automatic_reconstructor", "--workspace_path", dataset,
                                           "--image_path", pathString(ds / "images"),
                                           "--data_type", "video", // frames in order: sequential matching
                                           "--quality", std::string(preset.steps > 10000 ? "medium" : "low"),
                                           "--single_camera", "1", "--camera_model", "PINHOLE", "--dense", "0"};
    // COLMAP 3.12+ has the global mapper (formerly GLOMAP, much faster); older versions reject the option, and a
    // build without a usable GPU may need --use_gpu 0.
    const std::vector<std::vector<std::string>> attempts = {{"--mapper", "global"}, {}, {"--use_gpu", "0"}};
    bool ok = false;
    for (const auto& extra : attempts) {
        fs::remove_all(ds / "sparse", ec);
        fs::remove(ds / "database.db", ec);
        std::vector<std::string> args = base;
        args.insert(args.end(), extra.begin(), extra.end());
        ChildProcess p;
        std::string err;
        {
            std::ofstream f(u8path(log), std::ios::app);
            f << "\n$ colmap";
            for (const auto& a : args) f << ' ' << a;
            f << '\n';
        }
        if (!p.start(r.colmap, args, log, {}, err)) {
            fail(err);
            return false;
        }
        while (p.running()) {
            if (stopRequested()) {
                p.kill();
                return true;
            }
            const float f = bracketProgress(logTail(log, 5));
            update([&](TrainStatus& s) { s.stageProgress = f; });
            sleepSeconds(pollSeconds);
        }
        // The model with the most photos goes to sparse/0, where Brush looks.
        int best = -1;
        size_t bestCount = 0;
        for (int k = 0; k < 16; ++k) {
            std::vector<CameraView> views;
            std::string ignored;
            if (readColmapImages(pathString(ds / "sparse" / std::to_string(k)), views, ignored) && views.size() > bestCount) {
                best = k;
                bestCount = views.size();
            }
        }
        if (p.exitCode() == 0 && best >= 0 && bestCount >= 3) {
            if (best != 0) {
                fs::rename(ds / "sparse" / "0", ds / "sparse" / "unused-0", ec);
                fs::rename(ds / "sparse" / std::to_string(best), ds / "sparse" / "0", ec);
            }
            ok = true;
            break;
        }
    }
    if (!ok) {
        fail("COLMAP could not place the cameras (too few overlapping views, blur, or a shiny/plain object). "
             "Log: " + log + "\n" + logTail(log, 3));
        return false;
    }
    std::vector<CameraView> views;
    std::string err;
    if (readColmapImages(pathString(ds / "sparse" / "0"), views, err)) framing_ = orbitFraming(views);
    update([&](TrainStatus& s) {
        s.message = "Placed " + std::to_string(views.size()) + " of " + std::to_string(s.frames) + " photos";
    });
    return true;
}

bool SplatTrainJob::loadCheckpoint(const std::string& path, int step) {
    Mesh mesh;
    auto cloud = std::make_shared<SplatCloud>();
    PlyContent content;
    std::string err;
    if (!loadPly(path, mesh, *cloud, content, err) || cloud->empty()) return false;
    cloud->transform = framing_;
    cloud->name = "Training";
    std::lock_guard lock(mutex_);
    preview_ = std::move(cloud);
    ++status_.previewVersion;
    status_.previewStep = step;
    status_.splats = int(preview_->size());
    return true;
}

bool SplatTrainJob::train(const TrainRequest& r, const std::string& dataset) {
    const fs::path work = u8path(r.outDir) / "work", checkpoints = work / "checkpoints";
    const TrainPreset& preset = trainPreset(r.preset);
    std::error_code ec;
    fs::remove_all(checkpoints, ec);
    fs::create_directories(checkpoints, ec);
    const std::string log = pathString(work / "brush.log");
    fs::remove(u8path(log), ec);
    update([&](TrainStatus& s) {
        s.stage = TrainStage::Training;
        s.message = "Starting Brush";
        s.totalSteps = preset.steps;
        s.stageProgress = -1.0f;
        s.logPath = log;
    });
    ChildProcess p;
    std::string err;
    if (!p.start(r.brush, brushArguments(dataset, pathString(checkpoints), preset, r.evaluate), log,
                 {{"RUST_LOG", "brush_cli=info,brush_process=info"}, {"NO_COLOR", "1"}}, err)) {
        fail(err);
        return false;
    }
    const double trainStart = nowSeconds();
    size_t logOffset = 0;
    std::string partial;
    int loadedStep = -1;
    bool sawError = false;
    auto readLog = [&] {
        std::ifstream f(u8path(log), std::ios::binary);
        if (!f) return;
        f.seekg(std::streamoff(logOffset));
        std::string chunk((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        logOffset += chunk.size();
        partial += chunk;
        size_t nl;
        while ((nl = partial.find('\n')) != std::string::npos) {
            const std::string line = partial.substr(0, nl);
            partial.erase(0, nl + 1);
            TrainerLog t;
            if (!parseTrainerLog(line, t)) continue;
            update([&](TrainStatus& s) {
                if (t.refineIter >= 0) s.step = std::max(s.step, t.refineIter);
                if (t.splats >= 0) s.splats = t.splats;
                if (t.evalIter >= 0) s.step = std::max(s.step, t.evalIter);
                if (t.psnr > 0) s.psnr = t.psnr;
            });
            sawError = sawError || t.error;
        }
    };
    // The newest checkpoint is loaded once its size holds still for a moment (Brush may still be writing it); a
    // file that doesn't parse yet is tried again on the next check.
    auto pollCheckpoints = [&](bool final) {
        int step = -1;
        const std::string path = latestCheckpoint(pathString(checkpoints), step);
        if (path.empty() || step <= loadedStep) return;
        update([&](TrainStatus& s) { s.step = std::max(s.step, step); });
        const uintmax_t size = fs::file_size(u8path(path), ec);
        if (ec || size == 0) return;
        if (!final) {
            sleepSeconds(std::min(0.1, pollSeconds));
            if (fs::file_size(u8path(path), ec) != size || ec) return;
        }
        if (!loadCheckpoint(path, step)) return;
        loadedStep = step;
        // Older checkpoints are no longer needed (a Quality run would otherwise leave gigabytes of them).
        for (fs::directory_iterator it(checkpoints, ec), end; !ec && it != end; it.increment(ec)) {
            const int s = checkpointStep(pathString(it->path().filename()));
            if (s >= 0 && s < step) fs::remove(it->path(), ec);
        }
    };
    while (p.running()) {
        if (stopRequested()) {
            p.kill();
            break;
        }
        readLog();
        pollCheckpoints(false);
        const double t = nowSeconds() - trainStart;
        update([&](TrainStatus& s) {
            s.message = s.step > 0 ? "Training" : "Loading the photos";
            if (s.step > 0) s.eta = t / double(s.step) * double(std::max(0, s.totalSteps - s.step));
            s.stageProgress = s.totalSteps > 0 ? float(s.step) / float(s.totalSteps) : -1.0f;
        });
        sleepSeconds(pollSeconds);
    }
    readLog();
    if (cancel_) {
        update([](TrainStatus& s) {
            s.stage = TrainStage::Cancelled;
            s.message = "Cancelled";
        });
        return false;
    }
    pollCheckpoints(true);
    if (!finish_ && (p.exitCode() != 0 || sawError)) {
        fail("Brush stopped with an error (exit " + std::to_string(p.exitCode()) + "). Log: " + log + "\n" +
             logTail(log, 4));
        return false;
    }
    int step = -1;
    const std::string last = latestCheckpoint(pathString(checkpoints), step);
    if (last.empty()) {
        if (finish_) {
            update([](TrainStatus& s) {
                s.stage = TrainStage::Cancelled;
                s.message = "Stopped before the first checkpoint";
            });
        } else {
            fail("Brush finished without writing a splat. Log: " + log);
        }
        return false;
    }
    const fs::path result = u8path(r.outDir) / u8path(captureName(r.source) + ".ply");
    fs::copy_file(u8path(last), result, fs::copy_options::overwrite_existing, ec);
    if (ec) {
        fail("Cannot write " + pathString(result));
        return false;
    }
    update([&](TrainStatus& s) {
        s.stage = TrainStage::Done;
        s.result = pathString(result);
        s.step = std::max(s.step, step);
        s.eta = 0.0;
        s.stageProgress = 1.0f;
        s.message = "Trained " + std::to_string(s.splats) + " splats in " + std::to_string(step) + " steps";
    });
    return true;
}

// --- Commands for sculptforge-cli / Python / MCP (blocking) ---

void registerSplatTrainCommands(CommandRegistry& registry, std::function<SplatTrainSettings()> settings) {
    using T = ParamSpec::Type;
    registry.add({"splat_train",
                  "Train a Gaussian splat from a capture: a video, a folder of photos, a phone capture folder or a "
                  "COLMAP / nerfstudio dataset. Uses Brush (and COLMAP for camera positions, ffmpeg for video). Here "
                  "it waits until training ends and loads the result; the app trains in the background with a live "
                  "preview.",
                  {{"source", T::String, "The capture (video file or folder).", true},
                   {"out", T::String, "Folder for the result and work files. Default: next to the capture.", false},
                   {"preset", T::String, "fast (a quick preview) or quality. Default: the saved preset.", false}},
                  [settings](Document& doc, const Args& a) -> CommandResult {
                      const SplatTrainSettings s = settings();
                      TrainRequest req;
                      req.source = argString(a, "source");
                      req.outDir = argString(a, "out");
                      req.preset = argString(a, "preset", s.preset);
                      req.brush = findBrush(s);
                      req.colmap = findColmap(s);
                      req.ffmpeg = findFfmpeg(s);
                      SplatTrainJob job;
                      std::string err;
                      if (!job.start(req, err)) return {false, err};
                      job.wait();
                      const TrainStatus st = job.status();
                      if (st.stage != TrainStage::Done) return {false, st.error.empty() ? st.message : st.error};
                      if (auto cloud = job.takePreview()) {
                          doc.splats = std::move(*cloud);
                          doc.splats.name = captureName(req.source);
                          doc.splatsChanged();
                      }
                      return {true, st.message + "; saved " + st.result};
                  }});
    registry.add({"splat_train_cancel", "Stop splat training (in the app; sculptforge-cli trains in the foreground).", {},
                  [](Document&, const Args&) -> CommandResult { return {true, "No splat is training"}; }});
}

} // namespace sf::ai
