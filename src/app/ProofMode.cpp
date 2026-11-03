// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Proof mode (Help > Benchmark, sculptforge --benchmark; docs/benchmark.md).
//
// For each mesh size it builds a sphere with subdivision levels the way a sculptor gets one, then, in the
// app's own interactive loop, plays a scripted pen stroke into the same handlers mouse and pen events call
// (App::onMouseDown / onMouseMove / onMouseUp: the dab queue, the time budget, the renderer, all as when you
// sculpt), at 240 pointer samples a second, with a normal and a large brush, and turns the view. Every frame it
// records the frame time and how far the stroke on screen trails the pointer: in pixels along the path, and
// in milliseconds from the pointer sample the newest drawn dab belongs to until the frame showing it is handed
// to the display (SwapWindow; the display's own scan-out and the compositor come on top). A HUD shows the
// numbers live; the frames go, HUD included, to an MP4 (read back through pixel buffers, encoded by ffmpeg on
// its own thread), and a JSON result with the hardware and version is written at the end.
#include "app/ProofMode.h"

#include "core/FocusDetail.h"
#include "core/Micro.h"

#include "app/App.h"
#include "app/ShellQuote.h"
#include "app/Theme.h"

#include "core/Benchmark.h"
#include "core/MemoryGuard.h"
#include "core/Primitives.h"
#include "core/Timeline.h"

#include "render/GL.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <deque>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <thread>

#if defined(_WIN32)
#include <windows.h>
#elif defined(__APPLE__)
#include <sys/sysctl.h>
#include <sys/utsname.h>
#include <mach/mach.h>
#else
#include <sys/utsname.h>
#include <unistd.h>
#endif

#ifndef SF_GIT_COMMIT
#define SF_GIT_COMMIT "unknown"
#endif

namespace sf {

// ---------------------------------------------------------------------------------------------------------------
// Sizes and hardware

std::vector<ProofMeshSize>& proofExtraSizes() {
    static std::vector<ProofMeshSize> extra;
    return extra;
}

std::vector<ProofMeshSize> proofDefaultSizes(size_t available) {
    std::vector<ProofMeshSize> sizes{{5, "5M", nullptr, 150}, {20, "20M", nullptr, 150}, {33, "33M", nullptr, 150}};
    auto fits = [&](const ProofMeshSize& s) {
        return available > 0 && s.millions * 1e6 * s.bytesPerTriangle * 1.25 < double(available);
    };
    if (ProofMeshSize bigSize{100, "100M", nullptr, 150}; fits(bigSize)) sizes.push_back(bigSize);
    for (const ProofMeshSize& s : proofExtraSizes())
        if (fits(s)) sizes.push_back(s);
    return sizes;
}

std::vector<ProofMeshSize> proofStandardScene(const std::string& name) {
    // ~150 bytes a triangle like the spheres; the head is built the same way (a cube's quads, divided).
    const ProofMeshSize s15{15, "15M", [](size_t t) { return makeBenchHead(t); }, 150};
    const ProofMeshSize s33{33, "33M", [](size_t t) { return makeBenchHead(t); }, 150};
    if (name == "standard-15m") return {s15};
    if (name == "standard-33m") return {s33};
    if (name == "standard") return {s15, s33};
    if (name == "standard-1b") { // Focus Detail (prototype): ~1.02 billion micro triangles from a detail file
        const ProofMeshSize s1b{1019, "1B Focus Detail", [](size_t) {
                                    Mesh m;
                                    std::string err;
                                    const std::string path = focusStandard1bPath(err);
                                    if (path.empty() || !FocusDetail::open(path, m, err)) { // (main checked the asset first)
                                        std::fprintf(stderr, "standard-1b: %s\n", err.c_str());
                                        return makeBenchHead(size_t(1) << 20);
                                    }
                                    return m;
                                }, 5};
        return {s1b};
    }
    return {};
}

size_t totalMemoryBytes() {
#if defined(_WIN32)
    MEMORYSTATUSEX m{};
    m.dwLength = sizeof m;
    return GlobalMemoryStatusEx(&m) ? size_t(m.ullTotalPhys) : 0;
#elif defined(__APPLE__)
    uint64_t bytes = 0;
    size_t len = sizeof bytes;
    return sysctlbyname("hw.memsize", &bytes, &len, nullptr, 0) == 0 ? size_t(bytes) : 0;
#else
    const long pages = sysconf(_SC_PHYS_PAGES), page = sysconf(_SC_PAGE_SIZE);
    return pages > 0 && page > 0 ? size_t(pages) * size_t(page) : 0;
#endif
}

namespace {

std::string trim(std::string s) {
    while (!s.empty() && (s.back() == '\n' || s.back() == ' ' || s.back() == '\0' || s.back() == '\r')) s.pop_back();
    size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
    return s.substr(i);
}

std::string cpuModel() {
#if defined(_WIN32)
    char buf[256] = {};
    DWORD size = sizeof buf;
    if (RegGetValueA(HKEY_LOCAL_MACHINE, "HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", "ProcessorNameString",
                     RRF_RT_REG_SZ, nullptr, buf, &size) == ERROR_SUCCESS)
        return trim(buf);
    return "unknown";
#elif defined(__APPLE__)
    char buf[256] = {};
    size_t len = sizeof buf;
    if (sysctlbyname("machdep.cpu.brand_string", buf, &len, nullptr, 0) == 0) return trim(buf);
    return "unknown";
#else
    std::ifstream f("/proc/cpuinfo");
    for (std::string line; std::getline(f, line);) {
        if (line.rfind("model name", 0) == 0 || line.rfind("Hardware", 0) == 0) {
            const size_t colon = line.find(':');
            if (colon != std::string::npos) return trim(line.substr(colon + 1));
        }
    }
    // ARM boards (Apple silicon under Asahi, Raspberry Pi...): the device tree names the machine.
    std::ifstream dt("/proc/device-tree/model");
    std::string model((std::istreambuf_iterator<char>(dt)), std::istreambuf_iterator<char>());
    return model.empty() ? "unknown" : trim(model);
#endif
}

std::string osName() {
#if defined(_WIN32)
    return "Windows";
#else
    utsname u{};
    if (uname(&u) != 0) return "unknown";
    std::string s = std::string(u.sysname) + " " + u.release + " " + u.machine;
#if !defined(__APPLE__)
    std::ifstream f("/etc/os-release");
    for (std::string line; std::getline(f, line);)
        if (line.rfind("PRETTY_NAME=", 0) == 0) {
            std::string v = line.substr(12);
            v.erase(std::remove(v.begin(), v.end(), '"'), v.end());
            s = v + " (" + s + ")";
        }
#endif
    return s;
#endif
}

} // namespace

Json describeHardware() {
    Json h = Json::object();
    h.set("cpu", cpuModel());
    h.set("logical_cores", int(std::thread::hardware_concurrency()));
    h.set("ram_gb", std::round(double(totalMemoryBytes()) / double(1u << 30) * 10.0) / 10.0);
    h.set("os", osName());
    return h;
}

// ---------------------------------------------------------------------------------------------------------------
// The run

namespace {

using Clock = std::chrono::steady_clock;
double nowSeconds() { return std::chrono::duration<double>(Clock::now().time_since_epoch()).count(); }

constexpr GLenum kPixelPackBuffer = 0x88EB, kStreamRead = 0x88E1, kMapReadBit = 0x0001;
constexpr double kPointerHz = 240.0;
constexpr int kVideoFps = 30;

struct Percentiles {
    double p50 = 0, p95 = 0, p99 = 0, max = 0, mean = 0;
};
Percentiles percentiles(std::vector<double> v) {
    Percentiles p;
    if (v.empty()) return p;
    std::sort(v.begin(), v.end());
    auto at = [&](double q) { return v[std::min(v.size() - 1, size_t(q * double(v.size())))]; };
    p.p50 = at(0.5), p.p95 = at(0.95), p.p99 = at(0.99), p.max = v.back();
    for (double x : v) p.mean += x;
    p.mean /= double(v.size());
    return p;
}
// The longest the app went without showing a frame in a phase: its slowest frame, or lifting the pen.
double worstStallMs(const std::vector<double>& frameMs, double releaseMs) {
    double w = releaseMs;
    for (double f : frameMs) w = std::max(w, f);
    return w;
}
Json percentilesJson(const Percentiles& p) {
    Json j = Json::object();
    auto r = [](double x) { return std::round(x * 100.0) / 100.0; };
    j.set("p50", r(p.p50));
    j.set("p95", r(p.p95));
    j.set("p99", r(p.p99));
    j.set("max", r(p.max));
    j.set("mean", r(p.mean));
    return j;
}

// Frames to ffmpeg on a thread of its own, at a constant rate (frames repeat while the app is slower).
class VideoWriter {
public:
    bool start(const std::string& path, int w, int h) {
        w_ = w, h_ = h;
        // Bottom-up rows from glReadPixels: flipped; scaled to at most 1920 wide to keep the encoder light.
        const std::string quoted = shellQuote(path); // (never a shell-active file name)
        if (quoted.empty()) return false;
        const std::string cmd = "ffmpeg -loglevel error -y -f rawvideo -pix_fmt rgba -s " + std::to_string(w) + "x" +
                                std::to_string(h) + " -r " + std::to_string(kVideoFps) +
                                " -i - -vf \"vflip,scale='min(1920,iw)':-2\" -c:v libx264 -preset veryfast -crf 20 "
                                "-pix_fmt yuv420p -movflags +faststart " + quoted;
#if defined(_WIN32)
        ff_ = _popen(cmd.c_str(), "wb");
#else
        ff_ = popen(cmd.c_str(), "w");
#endif
        if (!ff_) return false;
        thread_ = std::thread([this] { loop(); });
        return true;
    }
    // Takes the frame shown at `t` (seconds); drops it when the encoder is behind.
    void push(double t, std::vector<uint8_t>&& rgba) {
        std::lock_guard<std::mutex> lock(m_);
        if (queue_.size() >= 4) return;
        queue_.push_back({t, std::move(rgba)});
        cv_.notify_one();
    }
    bool finish() {
        if (!ff_) return false;
        {
            std::lock_guard<std::mutex> lock(m_);
            stop_ = true;
            cv_.notify_one();
        }
        thread_.join();
#if defined(_WIN32)
        const int status = _pclose(ff_);
#else
        const int status = pclose(ff_);
#endif
        ff_ = nullptr;
        return status == 0 && written_ > 0;
    }
    int width() const { return w_; }
    int height() const { return h_; }
    bool running() const { return ff_ != nullptr; }
    ~VideoWriter() {
        if (ff_) finish();
    }

private:
    void loop() {
        std::vector<uint8_t> last;
        double next = -1.0;
        for (;;) {
            std::pair<double, std::vector<uint8_t>> f;
            {
                std::unique_lock<std::mutex> lock(m_);
                cv_.wait(lock, [&] { return stop_ || !queue_.empty(); });
                if (queue_.empty()) break;
                f = std::move(queue_.front());
                queue_.pop_front();
            }
            if (next < 0) next = f.first;
            // Slots up to this frame's time show the previous frame; then this one.
            while (!last.empty() && next + 1.0 / kVideoFps <= f.first) {
                std::fwrite(last.data(), 1, last.size(), ff_);
                ++written_;
                next += 1.0 / kVideoFps;
            }
            if (next <= f.first + 1e-9 || last.empty()) {
                std::fwrite(f.second.data(), 1, f.second.size(), ff_);
                ++written_;
                next += 1.0 / kVideoFps;
            }
            last = std::move(f.second);
        }
    }
    FILE* ff_ = nullptr;
    int w_ = 0, h_ = 0;
    std::thread thread_;
    std::mutex m_;
    std::condition_variable cv_;
    std::deque<std::pair<double, std::vector<uint8_t>>> queue_;
    bool stop_ = false;
    long written_ = 0;
};

struct PhaseSpec {
    const char* name;
    float radiusPx; // 0 = an orbit, no stroke
    double seconds;
    BrushType brush = BrushType::Standard;
    float strength = 0.35f;
    float pathScale = 1.0f; // the figure eight's size (Move drags the surface along: a smaller one)
};

// The standard scenes' stroke set (kProofStrokeSet): the same figure eight with each brush in turn, on one mesh.
std::vector<PhaseSpec> standardPhases(double seconds) {
    return {{"clay small", 25.0f, seconds, BrushType::Clay, 0.35f},
            {"clay medium", 60.0f, seconds, BrushType::Clay, 0.35f},
            {"clay large", 120.0f, seconds, BrushType::Clay, 0.35f},
            {"move", 80.0f, seconds, BrushType::Move, 0.25f, 0.4f},
            {"smooth", 60.0f, seconds, BrushType::Smooth, 0.5f},
            {"240 px brush", 240.0f, seconds, BrushType::Clay, 0.35f},
            {"orbit", 0.0f, 3.0}};
}

const char* proofBrushName(BrushType b) {
    switch (b) {
    case BrushType::Clay: return "Clay";
    case BrushType::Move: return "Move";
    case BrushType::Smooth: return "Smooth";
    default: return "Standard";
    }
}

// The graphics memory free now in KB, from the driver (GL_NVX_gpu_memory_info, else GL_ATI_meminfo); -1 = the
// driver doesn't say. Unified-memory GPUs (Apple, integrated) report system memory, shared with everything else.
long long driverFreeVramKb() {
    GLint v[4] = {-1, -1, -1, -1};
    while (gl::GetError() != GL_NO_ERROR) {}
    gl::GetIntegerv(0x9049, v); // GPU_MEMORY_INFO_CURRENT_AVAILABLE_VIDMEM_NVX
    if (gl::GetError() == GL_NO_ERROR && v[0] >= 0) return v[0];
    v[0] = -1;
    gl::GetIntegerv(0x87FB, v); // VBO_FREE_MEMORY_ATI
    if (gl::GetError() == GL_NO_ERROR && v[0] >= 0) return v[0];
    return -1;
}

struct PhaseResult {
    std::string name;
    std::string brush;
    float radiusPx = 0;
    double seconds = 0, pathPx = 0, releaseMs = 0; // releaseMs: lifting the pen (the dabs still queued, the undo step)
    double releaseDoneMs = 0; // until the stroke has ended (predictive strokes: the solver thread's last dabs on screen)
    size_t dabs = 0, samples = 0;
    std::vector<double> frameMs, lagMs, lagPx;
    std::vector<double> previewLagPx; // the predictive preview's head behind the pointer (the surface's without it)
};

struct SizeResult {
    std::string label;
    size_t triangles = 0, vertices = 0;
    double buildSeconds = 0;
    size_t ramBytes = 0, peakRamBytes = 0;
    size_t gpuBufferBytes = 0;   // the most the renderer's vertex and index buffers held (documentMemory().gpu)
    long long vramUsedKb = -1;   // the most the driver's free graphics memory dropped from before the build (-1: unknown)
    long long vramFreeBeforeKb = -1;
    std::vector<PhaseResult> phases;
    MemoryCheck memory;  // free memory before the build
    std::string skipped; // not built: too little free memory (benchmarkSkippedMessage)
};

} // namespace

struct ProofState {
    enum class Stage { Intro, Announce, Build, Settle, Phase, Finish, Results };
    Stage stage = Stage::Intro;
    bool cli = false, video = true, vsync = false;
    std::string scene; // "standard-15m", "standard-33m", "standard", or empty (the sphere sizes)
    int sceneChoice = 0; // the Intro's choice: 0 standard-15m, 1 standard-33m, 2 standard, 3 spheres
    double strokeSeconds = 6.0;
    std::string jsonPath, videoPath;
    std::vector<ProofMeshSize> sizes;
    size_t sizeIndex = 0, phaseIndex = 0;
    std::vector<PhaseSpec> phases;
    std::vector<SizeResult> results;
    double stageStart = 0, lastFrame = 0, lastSample = 0;
    // The stroke
    float cx = 0, cy = 0, R = 0;
    struct Sample {
        double t;
        glm::vec2 p;
        double s; // path length so far
    };
    std::vector<Sample> samples;
    std::vector<glm::vec2> dabs;
    size_t sent = 1; // samples handed to onMouseMove (the first went to onMouseDown)
    // The samples the newest dab / preview head were last matched to: the stroke only moves forward, so the
    // next match is searched from there on (the figure eight crosses itself; an older pass is never the match).
    size_t dabFrom = 0, previewFrom = 0;
    bool pressed = false;
    bool releasing = false; // let go: the phase ends with the stroke (predictive strokes finish after the release)
    double releaseAt = 0;
    // Live HUD values
    std::string hudTitle, hudLine2, hudLine3;
    std::string error;
    // Video
    VideoWriter writer;
    GLuint pbo[3] = {0, 0, 0};
    double pboTime[3] = {0, 0, 0};
    int pboW = 0, pboH = 0, pboNext = 0, pboFilled = 0;
    Json hardware;
    std::string gpu;
    bool written = false;
    // The scene before, restored by the results window's Close.
    float radiusBefore = 60, strengthBefore = 0.5f;
    BrushType brushBefore = BrushType::Standard;
};

namespace {

glm::vec2 strokePoint(const ProofState& p, double u) {
    // A figure eight across the front of the model, three times round.
    const double phi = 2.0 * 3.14159265358979 * 3.0 * u;
    return {p.cx + p.R * 1.25f * float(std::sin(phi)), p.cy + p.R * 0.75f * float(std::sin(2.0 * phi))};
}

std::string isoDate() {
    const std::time_t t = std::time(nullptr);
    char buf[32];
    std::strftime(buf, sizeof buf, "%Y-%m-%dT%H:%M:%SZ", std::gmtime(&t));
    return buf;
}

std::string fileStamp() {
    const std::time_t t = std::time(nullptr);
    char buf[32];
    std::strftime(buf, sizeof buf, "%Y%m%d-%H%M%S", std::localtime(&t));
    return buf;
}

std::vector<ProofMeshSize> parseSizes(const std::string& list) {
    std::vector<ProofMeshSize> out;
    std::stringstream ss(list);
    for (std::string item; std::getline(ss, item, ',');) {
        const double m = std::atof(item.c_str());
        if (m <= 0) continue;
        char label[32];
        std::snprintf(label, sizeof label, "%gM", m);
        out.push_back({m, label, nullptr, 150});
    }
    return out;
}

} // namespace

bool App::proofSkippedScenes() const {
    if (!proof_) return false;
    return std::any_of(proof_->results.begin(), proof_->results.end(), [](const SizeResult& r) { return !r.skipped.empty(); });
}

void App::startProofMode() {
    if (proof_ && proof_->stage != ProofState::Stage::Results && proof_->stage != ProofState::Stage::Intro) return;
    auto p = std::make_shared<ProofState>();
    p->cli = options_.benchmark;
    p->video = !options_.benchmarkNoVideo;
    p->vsync = options_.benchmarkVsync;
    p->strokeSeconds = std::max(1.0f, options_.benchmarkStrokeSeconds);
    // The CLI's scene, or for Help > Benchmark the standard 15M scene (the Intro offers the others).
    p->scene = options_.benchmarkScene;
    if (p->scene.empty() && !p->cli && options_.benchmarkSizes.empty()) p->scene = "standard-15m";
    p->sceneChoice = p->scene == "standard-33m" ? 1 : p->scene == "standard" ? 2 : p->scene.empty() ? 3 : 0;
    p->sizes = !p->scene.empty() ? proofStandardScene(p->scene)
               : options_.benchmarkSizes.empty() ? proofDefaultSizes(availableMemoryBytes()) : parseSizes(options_.benchmarkSizes);
    if (p->sizes.empty()) p->sizes = proofDefaultSizes(availableMemoryBytes());
    const std::string stamp = fileStamp();
    std::filesystem::path dir = std::filesystem::current_path();
    if (!p->cli && !configDir_.empty()) dir = std::filesystem::path(std::u8string(configDir_.begin(), configDir_.end())) / "benchmarks";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    auto u8 = [](const std::filesystem::path& x) {
        const auto s = x.u8string();
        return std::string(s.begin(), s.end());
    };
    p->jsonPath = !options_.benchmarkOut.empty() ? options_.benchmarkOut : u8(dir / ("sculptforge-benchmark-" + stamp + ".json"));
    p->videoPath = !options_.benchmarkVideo.empty() ? options_.benchmarkVideo : u8(dir / ("sculptforge-benchmark-" + stamp + ".mp4"));
    if (!p->scene.empty()) p->phases = standardPhases(p->strokeSeconds);
    else p->phases = {{"stroke", 60.0f, p->strokeSeconds}, {"large brush stroke", 240.0f, p->strokeSeconds}, {"orbit", 0.0f, 3.0}};
    p->stage = p->cli ? ProofState::Stage::Announce : ProofState::Stage::Intro;
    p->radiusBefore = radiusPx_;
    p->strengthBefore = strength_;
    p->brushBefore = brush_;
    proof_ = p;
}

void App::drawProofUi() {
    ProofState& p = *proof_;
    const float sc = uiScale_;
    if (p.stage == ProofState::Stage::Intro) {
        ImGui::OpenPopup("Benchmark (proof mode)");
        if (ImGui::BeginPopupModal("Benchmark (proof mode)", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::PushTextWrapPos(520 * sc);
            ImGui::TextWrapped("Proof mode measures how responsive sculpting is on this computer. For each mesh size it "
                               "builds a sphere with subdivision levels, then plays a scripted pen stroke through the "
                               "same code your pen goes through (240 samples a second), with a normal and a large brush, "
                               "and turns the view.");
            ImGui::Spacing();
            static const char* const kScenes[] = {"Standard 15M: a 15 million triangle head",
                                                  "Standard 33M: a 33 million triangle head",
                                                  "Standard 15M and 33M", "Spheres: 5M, 20M, 33M (100M with the memory)"};
            ImGui::TextUnformatted("Scene");
            for (int i = 0; i < 4; ++i)
                if (ImGui::RadioButton(kScenes[i], p.sceneChoice == i)) {
                    p.sceneChoice = i;
                    p.scene = i == 0 ? "standard-15m" : i == 1 ? "standard-33m" : i == 2 ? "standard" : "";
                    p.sizes = p.scene.empty() ? proofDefaultSizes(availableMemoryBytes()) : proofStandardScene(p.scene);
                    if (!p.scene.empty()) p.phases = standardPhases(p.strokeSeconds);
                    else p.phases = {{"stroke", 60.0f, p.strokeSeconds}, {"large brush stroke", 240.0f, p.strokeSeconds}, {"orbit", 0.0f, 3.0}};
                }
            if (!p.scene.empty())
                ImGui::TextDisabled("Clay at 25, 60 and 120 px, Move, Smooth, a 240 px brush, then an orbit: the scene the "
                                    "website's table uses (same as sculptforge --benchmark %s)", p.scene.c_str());
            { // What each scene needs free (a scene without it is skipped), against what is free now.
                const size_t free = availableMemoryBytes();
                std::string need, shortOf;
                for (const ProofMeshSize& s : p.sizes) {
                    const MemoryCheck c = checkMemory(size_t(s.millions * 1e6 * s.bytesPerTriangle), free);
                    const std::string gb = formatGigabytes(c.needBytes + memoryMargin(c.needBytes));
                    need += (need.empty() ? "" : ", ") + s.label + " about " + gb;
                    if (!c.enough) shortOf += (shortOf.empty() ? "" : ", ") + s.label;
                }
                ImGui::TextDisabled("%s of memory free now (%s)", free ? formatGigabytes(free).c_str() : "Unknown amount",
                                    need.c_str());
                if (!shortOf.empty())
                    ImGui::TextColored(theme::danger, "Not enough free memory for %s: it will be skipped. Close other apps first.",
                                       shortOf.c_str());
            }
            ImGui::Spacing();
            ImGui::TextWrapped("A live display shows the frame rate, frame times (median, 95th, 99th percentile), how far "
                               "the stroke trails the pointer, and memory. The result is saved as JSON beside a video "
                               "of the run:");
            ImGui::TextDisabled("%s", p.jsonPath.c_str());
            ImGui::Checkbox("Record a video of the run (needs ffmpeg)", &p.video);
            ImGui::Checkbox("Keep vertical sync on (frame rate capped at the display's)", &p.vsync);
            ImGui::Spacing();
            ImGui::TextColored(theme::accent, "The current scene is replaced. Save your work first.");
            ImGui::TextDisabled("Press Esc to stop the run.");
            ImGui::PopTextWrapPos();
            if (ImGui::Button("Start")) {
                p.stage = ProofState::Stage::Announce;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) {
                ImGui::CloseCurrentPopup();
                proof_.reset();
            }
            ImGui::EndPopup();
        }
        return;
    }
    if (p.stage == ProofState::Stage::Results) {
        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        bool open = true;
        if (ImGui::Begin("Benchmark results", &open, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse)) {
            if (!p.error.empty()) ImGui::TextColored(theme::danger, "%s", p.error.c_str());
            if (ImGui::BeginTable("##proof", 8, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
                for (const char* h : {"Mesh", "Phase", "fps", "frame p50", "frame p95", "frame p99", "worst stall", "trails pointer p50 / p95"})
                    ImGui::TableSetupColumn(h);
                ImGui::TableHeadersRow();
                for (const SizeResult& r : p.results)
                    for (const PhaseResult& ph : r.phases) {
                        const Percentiles f = percentiles(ph.frameMs), l = percentiles(ph.lagMs), lp = percentiles(ph.lagPx);
                        ImGui::TableNextRow();
                        ImGui::TableNextColumn();
                        ImGui::Text("%s", r.label.c_str());
                        ImGui::TableNextColumn();
                        ImGui::Text("%s", ph.name.c_str());
                        ImGui::TableNextColumn();
                        ImGui::Text("%.0f", ph.seconds > 0 ? double(ph.frameMs.size()) / ph.seconds : 0.0);
                        ImGui::TableNextColumn();
                        ImGui::Text("%.1f ms", f.p50);
                        ImGui::TableNextColumn();
                        ImGui::Text("%.1f ms", f.p95);
                        ImGui::TableNextColumn();
                        ImGui::Text("%.1f ms", f.p99);
                        ImGui::TableNextColumn();
                        ImGui::Text("%.0f ms", worstStallMs(ph.frameMs, ph.releaseMs));
                        ImGui::TableNextColumn();
                        if (ph.lagMs.empty()) ImGui::TextDisabled("-");
                        else ImGui::Text("%.0f / %.0f ms, %.0f / %.0f px", l.p50, l.p95, lp.p50, lp.p95);
                    }
                ImGui::EndTable();
            }
            ImGui::TextDisabled("Result: %s", p.jsonPath.c_str());
            if (p.video) ImGui::TextDisabled("Video: %s", p.videoPath.c_str());
            if (ImGui::Button("Open folder")) {
                const std::string dir = std::filesystem::path(p.jsonPath).parent_path().string();
                SDL_OpenURL(("file://" + dir).c_str());
            }
            ImGui::SameLine();
            if (ImGui::Button("How to share results")) SDL_OpenURL("https://sculptforge.app/benchmarks/");
            ImGui::SameLine();
            if (ImGui::Button("Close")) open = false;
        }
        ImGui::End();
        if (!open) {
            radiusPx_ = p.radiusBefore;
            strength_ = p.strengthBefore;
            brush_ = p.brushBefore;
            proof_.reset();
        }
        return;
    }
    // The HUD, top left of the canvas (burned into the video: it is part of the frame).
    const CanvasRect c = canvasRect();
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    ImFont* font = ImGui::GetFont();
    const float bigSize = ImGui::GetFontSize() * 1.35f, smallSize = ImGui::GetFontSize() * 1.05f;
    const ImVec2 at(c.x + 14 * sc, c.y + 14 * sc);
    const std::string brand = std::string("SculptForge ") + SCULPTFORGE_VERSION + " (" + SF_GIT_COMMIT + ")  -  proof mode";
    const char* lines[] = {brand.c_str(), p.hudTitle.c_str(), p.hudLine2.c_str(), p.hudLine3.c_str()};
    float w = 0, h = 0;
    for (int i = 0; i < 4; ++i) {
        const ImVec2 s = font->CalcTextSizeA(i == 1 ? bigSize : smallSize, 1e9f, 0.0f, lines[i]);
        w = std::max(w, s.x);
        h += s.y + 3 * sc;
    }
    dl->AddRectFilled(ImVec2(at.x - 10 * sc, at.y - 8 * sc), ImVec2(at.x + w + 10 * sc, at.y + h + 6 * sc), IM_COL32(12, 12, 14, 215), 6 * sc);
    float y = at.y;
    for (int i = 0; i < 4; ++i) {
        const float size = i == 1 ? bigSize : smallSize;
        dl->AddText(font, size, ImVec2(at.x, y), i == 0 ? theme::u32(theme::textDim) : i == 1 ? theme::u32(theme::textBright) : theme::u32(theme::text), lines[i]);
        y += font->CalcTextSizeA(size, 1e9f, 0.0f, lines[i]).y + 3 * sc;
    }
    if (p.pressed && !p.samples.empty()) { // where the pointer is: a smallSize cross the stroke follows
        const glm::vec2 q = p.samples.back().p;
        dl->AddLine(ImVec2(q.x - 7 * sc, q.y), ImVec2(q.x + 7 * sc, q.y), theme::u32(theme::accent), 2 * sc);
        dl->AddLine(ImVec2(q.x, q.y - 7 * sc), ImVec2(q.x, q.y + 7 * sc), theme::u32(theme::accent), 2 * sc);
    }
}

void App::proofBeforeSwap(int fbW, int fbH) {
    ProofState& p = *proof_;
    using Stage = ProofState::Stage;
    if (p.stage == Stage::Intro || p.stage == Stage::Results) return;
    const double now = nowSeconds();
    activeUntil_ = std::max(activeUntil_, double(SDL_GetTicksNS()) * 1e-9 + 0.5); // keep the loop drawing
    // Esc stops the run (the real pointer is kept out of the scripted stroke, the keyboard is not).
    if (const bool* keys = SDL_GetKeyboardState(nullptr); keys && keys[SDL_SCANCODE_ESCAPE] && p.stage != Stage::Finish) {
        p.error = "Stopped with Esc: the results are partial";
        if (p.pressed) onMouseUp();
        p.pressed = false;
        strokeProbe_ = nullptr;
        p.stage = Stage::Finish;
    }

    // The frame about to be shown: into the video (asynchronously, through a ring of pixel buffers).
    if (p.video && p.stage != Stage::Announce && p.stage != Stage::Build) {
        const int w = fbW & ~1, h = fbH & ~1;
        if (!p.writer.running() && p.error.find("video") == std::string::npos) {
            if (p.writer.start(p.videoPath, w, h)) {
                p.pboW = w, p.pboH = h;
                gl::GenBuffers(3, p.pbo);
                for (GLuint b : p.pbo) {
                    gl::BindBuffer(kPixelPackBuffer, b);
                    gl::BufferData(kPixelPackBuffer, GLsizeiptr(size_t(w) * size_t(h) * 4), nullptr, kStreamRead);
                }
                gl::BindBuffer(kPixelPackBuffer, 0);
            } else {
                p.error = "The video couldn't be recorded (ffmpeg missing?)";
                p.video = false;
            }
        }
        if (p.writer.running() && w == p.pboW && h == p.pboH) {
            const int slot = p.pboNext;
            if (p.pboFilled >= 3) { // the oldest read is done by now: hand it to the encoder
                gl::BindBuffer(kPixelPackBuffer, p.pbo[slot]);
                if (const void* data = gl::MapBufferRange(kPixelPackBuffer, 0, GLsizeiptr(size_t(w) * size_t(h) * 4), kMapReadBit)) {
                    std::vector<uint8_t> rgba(size_t(w) * size_t(h) * 4);
                    std::memcpy(rgba.data(), data, rgba.size());
                    gl::UnmapBuffer(kPixelPackBuffer);
                    p.writer.push(p.pboTime[slot], std::move(rgba));
                }
            }
            gl::BindFramebuffer(GL_READ_FRAMEBUFFER, 0);
            gl::BindBuffer(kPixelPackBuffer, p.pbo[slot]);
            gl::PixelStorei(GL_PACK_ALIGNMENT, 1);
            gl::ReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
            gl::BindBuffer(kPixelPackBuffer, 0);
            p.pboTime[slot] = now;
            p.pboNext = (slot + 1) % 3;
            p.pboFilled = std::min(p.pboFilled + 1, 3);
        }
    }

    const double frameMs = (now - p.lastFrame) * 1000.0;
    p.lastFrame = now;
    const CanvasRect c = canvasRect();
    auto beginPhase = [&] {
        const PhaseSpec& spec = p.phases[p.phaseIndex];
        SizeResult& r = p.results.back();
        r.phases.push_back({});
        r.phases.back().name = spec.name;
        r.phases.back().radiusPx = spec.radiusPx;
        if (spec.radiusPx > 0) r.phases.back().brush = proofBrushName(spec.brush);
        p.stageStart = p.lastFrame = nowSeconds(); // (not counting what the last phase's release cost)
        p.samples.clear();
        p.dabs.clear();
        p.sent = 1;
        p.dabFrom = p.previewFrom = 0;
        if (spec.radiusPx > 0) {
            brush_ = spec.brush;
            doc_.activeBrush = -1;
            paintMode_ = false;
            radiusPx_ = spec.radiusPx;
            strength_ = spec.strength;
            p.cx = c.x + c.w * 0.5f;
            p.cy = c.y + c.h * 0.5f;
            p.R = 0.26f * std::min(c.w, c.h) * spec.pathScale;
            const glm::vec2 q = strokePoint(p, 0.0);
            p.samples.push_back({now, q, 0.0});
            p.lastSample = now;
            strokeProbe_ = &p.dabs;
            onMouseDown(q.x, q.y, SDL_BUTTON_LEFT);
            p.pressed = drag_ == Drag::Stroke;
            if (!p.pressed) {
                strokeProbe_ = nullptr;
                p.error = "No stroke began on the " + r.label + " mesh (nothing under the pointer)";
            }
        }
        p.stage = Stage::Phase;
    };
    auto nextPhase = [&] {
        SizeResult& r = p.results.back();
        if (r.phases.back().seconds == 0) r.phases.back().seconds = now - p.stageStart; // (a stroke set it at its release)
        r.gpuBufferBytes = std::max(r.gpuBufferBytes, documentMemory(doc_).gpu);
        if (r.vramFreeBeforeKb >= 0)
            if (const long long freeKb = driverFreeVramKb(); freeKb >= 0)
                r.vramUsedKb = std::max(r.vramUsedKb, std::max(0LL, r.vramFreeBeforeKb - freeKb));
        ++p.phaseIndex;
        if (p.phaseIndex < p.phases.size()) {
            beginPhase();
            return;
        }
        r.ramBytes = residentBytes();
        r.peakRamBytes = peakResidentBytes();
        // SF_BENCH_MESH_OUT=prefix: the mesh's positions after every stroke (prefix-15M.bin: float x, y, z per
        // vertex), to compare runs. (SF_STROKE_BACKLOG=0 places every dab, but the scene still differs a little from
        // run to run on either stroke path; --measure-stroke with SF_MEASURE_VERIFY=1 compares the same dabs exactly.)
        if (const char* out = std::getenv("SF_BENCH_MESH_OUT")) {
            std::ofstream f(std::string(out) + "-" + r.label + ".bin", std::ios::binary);
            f.write(reinterpret_cast<const char*>(doc_.mesh.positions.data()), std::streamsize(doc_.mesh.positions.size() * sizeof(glm::vec3)));
        }
        ++p.sizeIndex;
        p.stage = p.sizeIndex < p.sizes.size() ? Stage::Announce : Stage::Finish;
    };

    switch (p.stage) {
    case Stage::Announce: { // show "Building ..." for one frame, then build
        if (hardwareCalibrating()) { // (first launch: measure this computer first, not while the benchmark runs)
            p.hudTitle = "Waiting for the hardware check to finish...";
            break;
        }
        if (p.sizeIndex == 0 && p.results.empty()) {
            if (lightbox_.open()) lightbox_.toggle(); // the canvas clear (it opens at start-up)
            if (!p.vsync) SDL_GL_SetSwapInterval(0);
            p.gpu = std::string(reinterpret_cast<const char*>(gl::GetString(GL_RENDERER))) + ", OpenGL " +
                    reinterpret_cast<const char*>(gl::GetString(GL_VERSION));
            p.hardware = describeHardware();
            p.hardware.set("gpu", p.gpu);
            int ww = 0, wh = 0;
            SDL_GetWindowSize(window_, &ww, &wh);
            Json win = Json::array();
            win.push(fbW);
            win.push(fbH);
            p.hardware.set("framebuffer", win);
            p.hardware.set("ui_scale", double(uiScale_));
        }
        const ProofMeshSize& s = p.sizes[p.sizeIndex];
        p.hudTitle = "Building a " + s.label + " triangle mesh...";
        p.hudLine2 = "mesh " + std::to_string(p.sizeIndex + 1) + " of " + std::to_string(p.sizes.size());
        p.hudLine3 = p.gpu.empty() ? std::string() : p.gpu;
        p.stage = Stage::Build;
        break;
    }
    case Stage::Build: {
        const ProofMeshSize& s = p.sizes[p.sizeIndex];
        doc_.setMesh(makeIcosphere(1)); // free the last mesh (and its history) first
        gl::Finish();
        // Free memory now, not the computer's total: with other programs holding the rest a 33M scene ran a system
        // out of memory. Too little: this scene is skipped (and said so), the next one tried.
        const MemoryCheck memory = checkMemory(size_t(s.millions * 1e6 * s.bytesPerTriangle));
        if (!memory.enough) {
            SizeResult r;
            r.label = s.label;
            r.memory = memory;
            r.skipped = benchmarkSkippedMessage(s.label, memory);
            SDL_Log("Benchmark: %s", r.skipped.c_str());
            p.error += (p.error.empty() ? "" : "; ") + r.skipped;
            p.results.push_back(r);
            p.hudTitle = r.skipped;
            ++p.sizeIndex;
            p.stage = p.sizeIndex < p.sizes.size() ? Stage::Announce : Stage::Finish;
            break;
        }
        const long long vramBefore = driverFreeVramKb();
        resetPeakResidentBytes();
        const double t0 = nowSeconds();
        const size_t tris = size_t(s.millions * 1e6);
        Mesh m = s.build ? s.build(tris) : makeBenchSphere(tris);
        SizeResult r;
        r.label = s.label;
        r.triangles = m.micro && m.micro->focus ? microTriangleCount(m) : m.triangleCount(); // (Focus Detail: its micro triangles)
        r.vertices = m.vertexCount();
        doc_.setMesh(std::move(m), "Benchmark");
        r.buildSeconds = nowSeconds() - t0;
        r.vramFreeBeforeKb = vramBefore;
        p.results.push_back(r);
        camera_.frame(sceneBounds());
        camera_.yaw = 0.0f;
        camera_.pitch = 0.0f;
        p.stageStart = nowSeconds();
        p.phaseIndex = 0;
        p.stage = Stage::Settle;
        char buf[160];
        std::snprintf(buf, sizeof buf, "%s triangles (%.1fM vertices), built in %.1f s", r.label.c_str(), double(r.vertices) / 1e6, r.buildSeconds);
        p.hudTitle = buf;
        p.hudLine2 = "settling (the first upload to the GPU)";
        break;
    }
    case Stage::Settle:
        if (now - p.stageStart > 1.0 && !backgroundBusy()) beginPhase();
        break;
    case Stage::Phase: {
        const PhaseSpec& spec = p.phases[p.phaseIndex];
        PhaseResult& ph = p.results.back().phases.back();
        if (p.releasing) { // (frames while the last dabs land aren't the stroke's)
            if (doc_.sculptor.strokeActive()) break;
            p.releasing = false;
            ph.releaseDoneMs = (nowSeconds() - p.releaseAt) * 1000.0;
            strokeProbe_ = nullptr;
            ph.dabs = p.dabs.size();
            nextPhase();
            break;
        }
        if (ph.frameMs.size() < 1000000 && now - p.stageStart > 0.0) ph.frameMs.push_back(frameMs);
        const double u = std::min(1.0, (now - p.stageStart) / spec.seconds);
        if (spec.radiusPx <= 0) { // orbit: the view turns, as a drag would
            camera_.yaw = float(6.2831853 * u);
            perf_.framesStill = 0;
        } else if (p.pressed) {
            // The pointer samples since the last frame (240 a second), in the order a tablet sends them.
            while (p.lastSample + 1.0 / kPointerHz <= now) {
                p.lastSample += 1.0 / kPointerHz;
                const double su = std::min(1.0, (p.lastSample - p.stageStart) / spec.seconds);
                const glm::vec2 q = strokePoint(p, su);
                p.samples.push_back({p.lastSample, q, p.samples.back().s + double(glm::length(q - p.samples.back().p))});
                ++ph.samples;
            }
            // How far the stroke on screen (the newest dab in this frame) trails the pointer (the newest sample).
            if (!p.dabs.empty()) {
                const glm::vec2 d = p.dabs.back();
                size_t best = p.samples.size() - 1;
                float bestDist = 1e30f;
                for (size_t i = p.samples.size(); i-- > p.dabFrom && p.samples.size() - i < 2000;) {
                    const float dist = glm::length(p.samples[i].p - d);
                    if (dist < bestDist) bestDist = dist, best = i;
                }
                p.dabFrom = best;
                ph.lagMs.push_back((now - p.samples[best].t) * 1000.0);
                ph.lagPx.push_back(p.samples.back().s - p.samples[best].s);
                // The same for what is on screen ahead of the solved surface: the predictive preview's head.
                const glm::vec2 h = ghostHeadValid_ ? ghostHead_ : d;
                size_t nearest = p.samples.size() - 1;
                float nearDist = 1e30f;
                for (size_t i = p.samples.size(); i-- > p.previewFrom && p.samples.size() - i < 2000;) {
                    const float dist = glm::length(p.samples[i].p - h);
                    if (dist < nearDist) nearDist = dist, nearest = i;
                }
                p.previewFrom = nearest;
                ph.previewLagPx.push_back(p.samples.back().s - p.samples[nearest].s);
            }
            // ... then the new samples go to the input handler, as the next frame's events would.
            for (; p.sent < p.samples.size(); ++p.sent)
                onMouseMove(p.samples[p.sent].p.x, p.samples[p.sent].p.y, 0.0f, 0.0f, false);
            if (u >= 1.0) {
                const double up = nowSeconds();
                onMouseUp(); // places the dabs still waiting, ends the stroke (undo step, timeline)
                ph.releaseMs = (nowSeconds() - up) * 1000.0;
                p.pressed = false;
                ph.pathPx = p.samples.back().s;
                ph.seconds = now - p.stageStart;
                p.releasing = true; // (the phase ends when the stroke has: see the top of this stage)
                p.releaseAt = up;
                break;
            }
        } else {
            nextPhase(); // a stroke that couldn't begin
            break;
        }
        if (u >= 1.0 && spec.radiusPx <= 0) {
            nextPhase();
            break;
        }
        // Live numbers over the last second.
        std::vector<double> recentF, recentL, recentP;
        double span = 0;
        for (size_t i = ph.frameMs.size(); i-- > 0 && span < 1000.0;) {
            span += ph.frameMs[i];
            recentF.push_back(ph.frameMs[i]);
        }
        for (size_t i = ph.lagMs.size(); i-- > 0 && recentL.size() < recentF.size();) {
            recentL.push_back(ph.lagMs[i]);
            recentP.push_back(ph.lagPx[i]);
        }
        const Percentiles f = percentiles(recentF), l = percentiles(recentL), lp = percentiles(recentP);
        const SizeResult& r = p.results.back();
        char buf[256];
        std::snprintf(buf, sizeof buf, "%s triangles  -  %s%s", r.label.c_str(), spec.name,
                      spec.radiusPx > 0 ? (" (" + std::to_string(int(spec.radiusPx)) + " px brush)").c_str() : "");
        p.hudTitle = buf;
        std::snprintf(buf, sizeof buf, "%.0f fps   frame %.1f / %.1f / %.1f ms (p50 / p95 / p99)", span > 0 ? 1000.0 * double(recentF.size()) / span : 0.0,
                      f.p50, f.p95, f.p99);
        p.hudLine2 = buf;
        if (spec.radiusPx > 0)
            std::snprintf(buf, sizeof buf, "stroke trails pointer %.0f ms / %.0f px (p95 %.0f ms)   RAM %.1f GB   %s", l.p50, lp.p50, l.p95,
                          double(residentBytes()) / double(1u << 30), p.vsync ? "vsync on" : "vsync off");
        else
            std::snprintf(buf, sizeof buf, "orbiting   RAM %.1f GB   %s", double(residentBytes()) / double(1u << 30), p.vsync ? "vsync on" : "vsync off");
        p.hudLine3 = buf;
        break;
    }
    case Stage::Finish: {
        if (!p.vsync) SDL_GL_SetSwapInterval(1);
        if (p.writer.running()) {
            if (!p.writer.finish()) p.error += (p.error.empty() ? "" : "; ") + std::string("the video couldn't be written");
            gl::DeleteBuffers(3, p.pbo);
        }
        Json root = Json::object();
        root.set("schema", "sculptforge-benchmark/1");
        Json app = Json::object();
        app.set("version", SCULPTFORGE_VERSION);
        app.set("commit", SF_GIT_COMMIT);
        root.set("sculptforge", app);
        root.set("date", isoDate());
        root.set("hardware", p.hardware);
        Json settings = Json::object();
        settings.set("vsync", p.vsync);
        settings.set("video", p.video);
        settings.set("pointer_hz", kPointerHz);
        settings.set("stroke_seconds", p.strokeSeconds);
        if (!p.scene.empty()) {
            settings.set("scene", p.scene);
            settings.set("stroke_set", kProofStrokeSet);
        }
        settings.set("input", "scripted pointer samples into App::onMouseMove (the mouse / pen handler)");
        settings.set("lag", "pointer sample time to SwapWindow of the frame showing its dab (display scan-out not included)");
        root.set("settings", settings);
        Json runs = Json::array(), skipped = Json::array();
        for (const SizeResult& r : p.results) {
            if (!r.skipped.empty()) {
                Json k = Json::object();
                k.set("label", r.label);
                k.set("need_mb", double(r.memory.needBytes >> 20));
                k.set("available_mb", double(r.memory.availableBytes >> 20));
                k.set("message", r.skipped);
                skipped.push(k);
                continue;
            }
            Json run = Json::object();
            run.set("label", r.label);
            run.set("triangles", double(r.triangles));
            run.set("vertices", double(r.vertices));
            run.set("build_seconds", std::round(r.buildSeconds * 100) / 100);
            run.set("ram_mb", double(r.ramBytes >> 20));
            run.set("peak_ram_mb", double(r.peakRamBytes >> 20));
            run.set("gpu_buffers_mb", double(r.gpuBufferBytes >> 20));
            if (r.vramUsedKb > 0) run.set("vram_used_mb", double(r.vramUsedKb >> 10)); // (0: the driver's number never moved, as on unified memory)
            double stall = 0;
            for (const PhaseResult& ph : r.phases) stall = std::max(stall, worstStallMs(ph.frameMs, ph.releaseMs));
            run.set("worst_stall_ms", std::round(stall * 10) / 10);
            Json phases = Json::array();
            for (const PhaseResult& ph : r.phases) {
                Json j = Json::object();
                j.set("name", ph.name);
                if (!ph.brush.empty()) j.set("brush", ph.brush);
                if (ph.radiusPx > 0) j.set("brush_radius_px", double(ph.radiusPx));
                j.set("worst_stall_ms", std::round(worstStallMs(ph.frameMs, ph.releaseMs) * 10) / 10);
                j.set("seconds", std::round(ph.seconds * 100) / 100);
                j.set("frames", double(ph.frameMs.size()));
                j.set("fps", ph.seconds > 0 ? std::round(double(ph.frameMs.size()) / ph.seconds * 10) / 10 : 0.0);
                j.set("frame_ms", percentilesJson(percentiles(ph.frameMs)));
                if (ph.radiusPx > 0) {
                    if (!ph.lagMs.empty()) { // (Move drags the surface without dabs: no stroke to trail the pointer)
                        j.set("lag_ms", percentilesJson(percentiles(ph.lagMs)));
                        j.set("lag_px", percentilesJson(percentiles(ph.lagPx)));
                    }
                    j.set("dabs", double(ph.dabs));
                    j.set("pointer_samples", double(ph.samples));
                    j.set("path_px", std::round(ph.pathPx));
                    j.set("release_ms", std::round(ph.releaseMs * 10) / 10);
                    j.set("release_done_ms", std::round(ph.releaseDoneMs * 10) / 10);
                    j.set("preview_lag_px", percentilesJson(percentiles(ph.previewLagPx)));
                    j.set("pointer_speed_px_s", ph.seconds > 0 ? std::round(ph.pathPx / ph.seconds) : 0.0);
                }
                phases.push(j);
            }
            run.set("phases", phases);
            runs.push(run);
        }
        root.set("runs", runs);
        if (skipped.size() > 0) root.set("skipped", skipped); // (a result with skipped scenes isn't published)
        if (p.video) root.set("video", std::filesystem::path(p.videoPath).filename().string());
        if (!p.error.empty()) root.set("notes", p.error);
        {
            std::ofstream f(std::filesystem::path(std::u8string(p.jsonPath.begin(), p.jsonPath.end())));
            f << root.dump(true) << "\n";
            if (!f) p.error += (p.error.empty() ? "" : "; ") + ("couldn't write " + p.jsonPath);
        }
        // A summary on the terminal too.
        std::printf("SculptForge %s (%s) proof mode%s%s on %s | %s\n", SCULPTFORGE_VERSION, SF_GIT_COMMIT,
                    p.scene.empty() ? "" : ", scene ", p.scene.c_str(), root["hardware"]["cpu"].asString().c_str(), p.gpu.c_str());
        for (const SizeResult& r : p.results) {
            if (!r.skipped.empty()) {
                std::printf("  %s\n", r.skipped.c_str());
                continue;
            }
            std::printf("  %-5s peak RAM %zu MB, GPU buffers %zu MB%s, worst stall %.0f ms\n", r.label.c_str(), r.peakRamBytes >> 20,
                        r.gpuBufferBytes >> 20, r.vramUsedKb > 0 ? (", driver VRAM used " + std::to_string(r.vramUsedKb >> 10) + " MB").c_str() : "",
                        [&] { double w = 0; for (const PhaseResult& ph : r.phases) w = std::max(w, worstStallMs(ph.frameMs, ph.releaseMs)); return w; }());
            for (const PhaseResult& ph : r.phases) {
                const Percentiles f = percentiles(ph.frameMs), l = percentiles(ph.lagMs), lp = percentiles(ph.lagPx);
                std::printf("  %-5s %-19s %6.1f fps  frame p50 %5.1f p95 %5.1f p99 %5.1f ms", r.label.c_str(), ph.name.c_str(),
                            ph.seconds > 0 ? double(ph.frameMs.size()) / ph.seconds : 0.0, f.p50, f.p95, f.p99);
                std::printf("  stall %5.0f ms", worstStallMs(ph.frameMs, ph.releaseMs));
                if (ph.radiusPx > 0)
                    std::printf("  trails %4.0f ms / %4.0f px (p95 %4.0f ms / %4.0f px), preview %4.0f px, release %.0f ms (done %.0f ms)",
                                l.p50, lp.p50, l.p95, lp.p95, percentiles(ph.previewLagPx).p50, ph.releaseMs, ph.releaseDoneMs);
                std::printf("\n");
            }
        }
        std::printf("Result: %s%s%s\n", p.jsonPath.c_str(), p.video ? "\nVideo: " : "", p.video ? p.videoPath.c_str() : "");
        if (!p.error.empty()) std::printf("Note: %s\n", p.error.c_str());
        std::fflush(stdout);
        p.stage = Stage::Results;
        if (p.cli) running_ = false;
        status_ = "Benchmark finished: " + p.jsonPath;
        break;
    }
    default:
        break;
    }
}

} // namespace sf
