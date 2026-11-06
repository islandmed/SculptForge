// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Benchmark takes (sculptforge --benchmark-script take.sfui; docs/benchmark.md): a UI test script's input (strokes,
// orbits, keys, clicks, waits) played in REAL time in the app's own interactive loop, so a clip shows the speed a
// person gets. Each frame the script is advanced by the wall-clock time since the last one (never a fixed step: a
// slow frame is a jump, as it is for a person); `stroke` sends its pointer samples at 240 a second. The proof mode
// HUD is drawn on top and the frames go to the same MP4 path (BenchVideo.h), and a CSV gets every frame's time and
// how far the stroke on screen trails the pointer, so every clip comes with its numbers.
//
// Local only: the take and the project it opens (a positional argument) are the only files read; steps that run
// commands, read files or change other settings are refused when the take loads, and shots and expectations are
// skipped (Demo::loadTake).
#include "app/App.h"
#include "app/BenchVideo.h"
#include "app/Demo.h"
#include "app/Theme.h"

#include "render/GL.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>

#ifndef SF_GIT_COMMIT
#define SF_GIT_COMMIT "unknown"
#endif

namespace sf {

namespace {

double nowSeconds() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

constexpr double kSettleSeconds = 1.0;    // the window and the first upload settle before the take starts
constexpr double kSettleGiveUp = 60.0;    // ... but a job that never finishes doesn't hold it up for ever
constexpr double kEndSeconds = 0.5;       // the last frames after the script, so the clip ends on the result

double percentile(std::vector<double> v, double q) {
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    return v[std::min(v.size() - 1, size_t(q * double(v.size())))];
}

std::string fileStamp() {
    const std::time_t t = std::time(nullptr);
    char buf[32];
    std::strftime(buf, sizeof buf, "%Y%m%d-%H%M%S", std::localtime(&t));
    return buf;
}

} // namespace

struct TakeState {
    enum class Stage { Settle, Play, Ending, Done };
    Stage stage = Stage::Settle;
    std::string name, csvPath, videoPath, gpu, error;
    bool video = true, vsync = false;
    double settleStart = 0, playStart = 0, lastUpdate = 0, lastFrame = 0, endAt = 0;
    struct Frame {
        double t, ms;
        bool stroking;
        double lagMs = -1, lagPx = -1; // -1: no dab on screen to measure
    };
    std::vector<Frame> frames;
    // The stroke under way: the script's pointer samples, and the dabs placed (strokeProbe_).
    struct Sample {
        double t;
        glm::vec2 p;
        double s; // path length so far
    };
    std::vector<Sample> samples;
    std::vector<glm::vec2> dabs;
    size_t dabFrom = 0; // the sample the newest dab was last matched to (the stroke only moves forward)
    bool inStroke = false;
    std::string hud[3];
    BenchVideo writer;
};

bool App::startTake() {
    auto t = std::make_shared<TakeState>();
    demo_ = std::make_unique<Demo>(*this);
    std::string error;
    if (!demo_->loadTake(options_.benchmarkScript, error)) {
        std::fprintf(stderr, "%s\n", error.c_str());
        demo_.reset();
        return false;
    }
    const std::filesystem::path script(std::u8string(options_.benchmarkScript.begin(), options_.benchmarkScript.end()));
    t->name = script.filename().string();
    t->video = !options_.benchmarkNoVideo;
    t->vsync = options_.benchmarkVsync;
    // The video where --benchmark-video says, the CSV where --benchmark-out says or beside the video; else both in
    // the current folder, named after the take.
    auto u8 = [](const std::filesystem::path& x) {
        const auto s = x.u8string();
        return std::string(s.begin(), s.end());
    };
    const std::string base = u8(std::filesystem::current_path() / ("sculptforge-take-" + script.stem().string() + "-" + fileStamp()));
    t->videoPath = !options_.benchmarkVideo.empty() ? options_.benchmarkVideo : base + ".mp4";
    if (!options_.benchmarkOut.empty()) t->csvPath = options_.benchmarkOut;
    else if (!options_.benchmarkVideo.empty())
        t->csvPath = u8(std::filesystem::path(std::u8string(t->videoPath.begin(), t->videoPath.end())).replace_extension(".csv"));
    else t->csvPath = base + ".csv";
    t->settleStart = nowSeconds();
    demo_->pointerHook = [this](float x, float y) {
        TakeState& s = *take_;
        if (s.stage != TakeState::Stage::Play || drag_ != Drag::Stroke) return;
        if (!s.inStroke) { // a new stroke: measured from its own first sample
            s.inStroke = true;
            s.samples.clear();
            s.dabs.clear();
            s.dabFrom = 0;
        }
        const glm::vec2 q(x, y);
        s.samples.push_back({nowSeconds(), q, s.samples.empty() ? 0.0 : s.samples.back().s + double(glm::length(q - s.samples.back().p))});
    };
    take_ = t;
    SDL_Log("Benchmark take %s: video %s, CSV %s", t->name.c_str(), t->video ? t->videoPath.c_str() : "(none)", t->csvPath.c_str());
    return true;
}

bool App::takeRunning() const { return take_ && take_->stage != TakeState::Stage::Done; }

void App::takeBeforeFrame() {
    TakeState& t = *take_;
    const double now = nowSeconds();
    if (t.stage == TakeState::Stage::Settle) {
        if (t.gpu.empty()) {
            if (libraryBrowser_.open()) libraryBrowser_.toggle(); // the canvas clear (it opens at start-up)
            if (!t.vsync) SDL_GL_SetSwapInterval(0);
            t.gpu = reinterpret_cast<const char*>(gl::GetString(GL_RENDERER));
        }
        t.hud[0] = t.name + "  -  getting ready";
        const bool settled = now - t.settleStart > kSettleSeconds && !backgroundBusy() && !hardwareCalibrating();
        if (!settled && now - t.settleStart < kSettleGiveUp) return;
        if (!settled) SDL_Log("Benchmark take: still busy after %.0f s; starting anyway", kSettleGiveUp);
        t.stage = TakeState::Stage::Play;
        t.playStart = t.lastUpdate = t.lastFrame = now;
        strokeProbe_ = &t.dabs;
    }
    if (t.stage == TakeState::Stage::Play) {
        demo_->update(float(now - t.lastUpdate)); // the wall clock's time since the last frame, however long
        t.lastUpdate = now;
        if (demo_->finished() && !doc_.sculptor.strokeActive() && dabQueue_.empty()) {
            t.stage = TakeState::Stage::Ending;
            t.endAt = now + kEndSeconds;
        }
    }
    Demo::beginFrame(); // (the items the script finds are this frame's)
}

void App::takeBeforeSwap(int fbW, int fbH) {
    TakeState& t = *take_;
    using Stage = TakeState::Stage;
    if (t.stage == Stage::Settle || t.stage == Stage::Done) return;
    const double now = nowSeconds();
    if (const bool* keys = SDL_GetKeyboardState(nullptr); keys && keys[SDL_SCANCODE_ESCAPE] && t.stage == Stage::Play) {
        t.error = "Stopped with Esc: the take is partial";
        t.stage = Stage::Ending;
        t.endAt = now;
    }
    if (t.video) { // the frame about to be shown, HUD included
        const int w = fbW & ~1, h = fbH & ~1;
        if (!t.writer.running() && !t.writer.start(t.videoPath, w, h)) {
            t.error = "The video couldn't be recorded (ffmpeg missing?)";
            t.video = false;
        }
        t.writer.grab(now, w, h);
    }

    TakeState::Frame f{now - t.playStart, (now - t.lastFrame) * 1000.0, drag_ == Drag::Stroke};
    t.lastFrame = now;
    if (!f.stroking && !doc_.sculptor.strokeActive()) t.inStroke = false;
    // How far the stroke on screen (the newest dab) trails the pointer (the newest sample): the sample nearest the
    // dab, searched forward from the last match (a path that crosses itself never matches an older pass).
    if (t.inStroke && !t.dabs.empty() && !t.samples.empty()) {
        const glm::vec2 d = t.dabs.back();
        size_t best = t.samples.size() - 1;
        float bestDist = 1e30f;
        for (size_t i = t.samples.size(); i-- > t.dabFrom && t.samples.size() - i < 2000;) {
            const float dist = glm::length(t.samples[i].p - d);
            if (dist < bestDist) bestDist = dist, best = i;
        }
        t.dabFrom = best;
        f.lagMs = (now - t.samples[best].t) * 1000.0;
        f.lagPx = t.samples.back().s - t.samples[best].s;
    }
    t.frames.push_back(f);

    // The HUD: the last second's frames.
    std::vector<double> ms, lag;
    double span = 0;
    for (size_t i = t.frames.size(); i-- > 0 && span < 1000.0;) {
        span += t.frames[i].ms;
        ms.push_back(t.frames[i].ms);
        if (t.frames[i].lagMs >= 0) lag.push_back(t.frames[i].lagMs);
    }
    char buf[256];
    std::snprintf(buf, sizeof buf, "%s  -  %.1f s, real time", t.name.c_str(), f.t);
    t.hud[0] = buf;
    std::snprintf(buf, sizeof buf, "frame %.1f ms (p95 %.1f, slowest %.1f over the last second)   %s", percentile(ms, 0.5),
                  percentile(ms, 0.95), percentile(ms, 1.0), t.vsync ? "vsync on" : "vsync off");
    t.hud[1] = buf;
    if (!lag.empty()) std::snprintf(buf, sizeof buf, "stroke trails pointer %.0f ms / %.0f px   %s", percentile(lag, 0.5), f.lagPx < 0 ? 0.0 : f.lagPx, t.gpu.c_str());
    else std::snprintf(buf, sizeof buf, "%s", t.gpu.c_str());
    t.hud[2] = buf;

    if (t.stage != Stage::Ending || now < t.endAt) return;
    // The end: the video closed, the CSV written, a summary, and the app quits.
    t.stage = Stage::Done;
    strokeProbe_ = nullptr;
    if (!t.vsync) SDL_GL_SetSwapInterval(1);
    if (t.writer.running() && !t.writer.finish()) t.error += (t.error.empty() ? "" : "; ") + std::string("the video couldn't be written");
    {
        std::ofstream csv(std::filesystem::path(std::u8string(t.csvPath.begin(), t.csvPath.end())));
        csv << "frame,t_s,frame_ms,stroking,lag_ms,lag_px\n";
        char line[160];
        for (size_t i = 0; i < t.frames.size(); ++i) {
            const TakeState::Frame& r = t.frames[i];
            std::snprintf(line, sizeof line, "%zu,%.4f,%.3f,%d,", i, r.t, r.ms, r.stroking ? 1 : 0);
            csv << line;
            if (r.lagMs >= 0) {
                std::snprintf(line, sizeof line, "%.2f,%.1f", r.lagMs, r.lagPx);
                csv << line;
            } else csv << ",";
            csv << "\n";
        }
        if (!csv) t.error += (t.error.empty() ? "" : "; ") + ("the CSV couldn't be written to " + t.csvPath);
    }
    ms.clear(), lag.clear(); // (the whole take now)
    double sum = 0;
    for (const TakeState::Frame& r : t.frames) {
        ms.push_back(r.ms);
        sum += r.ms;
        if (r.lagMs >= 0) lag.push_back(r.lagMs);
    }
    const double wall = now - t.playStart;
    std::printf("Take %s: %zu frames in %.2f s (script clock %.2f s; frame times add up to %.1f%% of the wall clock)\n",
                t.name.c_str(), t.frames.size(), wall, double(demo_->clock()), wall > 0 ? 100.0 * sum / 1000.0 / wall : 0.0);
    std::printf("  frame p50 %.1f ms, p95 %.1f, p99 %.1f, slowest %.1f\n", percentile(ms, 0.5), percentile(ms, 0.95),
                percentile(ms, 0.99), percentile(ms, 1.0));
    if (!lag.empty())
        std::printf("  stroke trails pointer p50 %.0f ms, p95 %.0f ms (%zu frames)\n", percentile(lag, 0.5), percentile(lag, 0.95), lag.size());
    std::printf("CSV: %s%s%s\n", t.csvPath.c_str(), t.video ? "\nVideo: " : "", t.video ? t.videoPath.c_str() : "");
    for (const std::string& failure : demo_->failures()) std::fprintf(stderr, "FAIL %s\n", failure.c_str());
    if (!t.error.empty()) std::fprintf(stderr, "Take: %s\n", t.error.c_str());
    std::fflush(stdout);
    takeExitCode_ = t.error.empty() && demo_->failures().empty() ? 0 : 1;
    running_ = false;
}

void App::drawTakeHud() {
    TakeState& t = *take_;
    if (t.stage == TakeState::Stage::Done) return;
    // As proof mode's (burned into the video: it is part of the frame).
    const float sc = uiScale_;
    const CanvasRect c = canvasRect();
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    ImFont* font = ImGui::GetFont();
    const float bigSize = ImGui::GetFontSize() * 1.35f, smallSize = ImGui::GetFontSize() * 1.05f;
    const std::string brand = std::string("SculptForge ") + SCULPTFORGE_VERSION + " (" + SF_GIT_COMMIT + ")  -  benchmark take";
    const char* lines[] = {brand.c_str(), t.hud[0].c_str(), t.hud[1].c_str(), t.hud[2].c_str()};
    float w = 0, h = 0;
    for (int i = 0; i < 4; ++i) {
        const ImVec2 s = font->CalcTextSizeA(i == 1 ? bigSize : smallSize, 1e9f, 0.0f, lines[i]);
        w = std::max(w, s.x);
        h += s.y + 3 * sc;
    }
    // Bottom left of the canvas: the top has the timeline strip and the axes, and a take's strokes are mostly in the
    // middle (above the timeline strip when that is at the bottom).
    const float bottom = timelineStripY_ > c.y && timelineTopInset_ <= 0.0f ? timelineStripY_ : c.y + c.h;
    const ImVec2 at(c.x + 14 * sc, bottom - h - 14 * sc);
    dl->AddRectFilled(ImVec2(at.x - 10 * sc, at.y - 8 * sc), ImVec2(at.x + w + 10 * sc, at.y + h + 6 * sc), IM_COL32(12, 12, 14, 215), 6 * sc);
    float y = at.y;
    for (int i = 0; i < 4; ++i) {
        const float size = i == 1 ? bigSize : smallSize;
        dl->AddText(font, size, ImVec2(at.x, y), i == 0 ? theme::u32(theme::textDim) : i == 1 ? theme::u32(theme::textBright) : theme::u32(theme::text), lines[i]);
        y += font->CalcTextSizeA(size, 1e9f, 0.0f, lines[i]).y + 3 * sc;
    }
    if (t.stage != TakeState::Stage::Settle) demo_->drawOverlay(); // the script's pointer, as a demo video shows it
}

} // namespace sf
