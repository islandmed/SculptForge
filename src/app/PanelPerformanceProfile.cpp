// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The hardware profile in the app (docs/performance.md, "Hardware profile"):
//  - on first launch, and when the GPU, its driver or the app version changes, calibration runs over the first
//    frames (about 2 seconds, one short step per frame under a small overlay; Esc skips) and the result is cached
//    in the settings folder; later starts read the cache;
//  - Preferences > Performance shows what was detected and measured, the profile and its settings, the frame
//    governor's state, Re-calibrate and manual overrides;
//  - the adaptive frame governor (core/FrameGovernor.h) gets every drawn frame's CPU time and GPU time (timer
//    queries, read a few frames later) and moves the motion proxy threshold / render triangle budget.
// Tests, renders, recordings and the --measure-* runs never calibrate (their results must not depend on the
// machine), and keep the fixed 10 million triangle Fast Navigation threshold.
#include "app/App.h"
#include "app/Calibration.h"
#include "app/Ui.h"

#include "core/FrameGovernor.h"
#include "core/HardwareProfile.h"
#include "core/ThreadPool.h"
#include "core/TierConfig.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <deque>

namespace sf {

struct HwProfileUi {
    HardwareProfile profile;
    bool active = false;           // a profile is applied: its threshold governs Fast Navigation
    bool saved = false;            // the profile came from (or went to) the cache
    std::unique_ptr<Calibrator> calibrator;
    std::string calibrationReason; // "first launch", "GPU changed", ...
    bool announce = false;         // say what calibration found in the status bar
    int calibrationFrames = 0;     // frames shown since calibration started (the first shows the overlay)
    FrameGovernor governor;
    GpuFrameTimer timer;
    uint64_t frame = 0;
    double frameStartMs = 0.0;
    struct Pending {
        uint64_t frame;
        FrameSample sample;
    };
    std::deque<Pending> pending;   // frames waiting for their GPU time
    uint64_t revision = 0;         // performanceSettingsRevision() when last applied
    int displayHz = 0;
    PowerSource power = PowerSource::Unknown;
    double nextPowerCheck = 0.0;   // seconds (SDL ticks)
    TierConfig tier;               // what currentTierConfig() holds
    double lastCpuMs = 0.0, lastGpuMs = -1.0;
    bool overridesOpen = false;
};

namespace {

constexpr const char* kPath = "Preferences > Performance";

double nowMs() { return double(SDL_GetTicksNS()) * 1e-6; }

void dim(const std::string& text) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("%s", text.c_str());
    ImGui::PopStyleColor();
}

std::string fmt(const char* f, double v) {
    char buf[64];
    std::snprintf(buf, sizeof buf, f, v);
    return buf;
}

bool calibrationDisabled() {
    const char* v = std::getenv("SF_NO_CALIBRATION");
    return v && *v && *v != '0';
}

// Battery or mains (SDL_GetPowerInfo): a laptop on battery sends less per frame and drops to 60 Hz (core/TierConfig.h).
PowerSource powerSource() {
    switch (SDL_GetPowerInfo(nullptr, nullptr)) {
    case SDL_POWERSTATE_ON_BATTERY: return PowerSource::Battery;
    case SDL_POWERSTATE_NO_BATTERY:
    case SDL_POWERSTATE_CHARGING:
    case SDL_POWERSTATE_CHARGED: return PowerSource::AC;
    default: return PowerSource::Unknown;
    }
}

int displayRefreshHz(SDL_Window* window) {
    if (!window) return 0;
    const SDL_DisplayID display = SDL_GetDisplayForWindow(window);
    const SDL_DisplayMode* mode = display ? SDL_GetCurrentDisplayMode(display) : nullptr;
    return mode ? int(mode->refresh_rate + 0.5f) : 0;
}

} // namespace

bool App::hardwareProfileGoverns() const { return hwProfile_ && hwProfile_->active; }

void App::initHardwareProfile() {
    // Deterministic runs (UI tests, renders, recordings, demos) and the --measure-* runs keep the fixed defaults; a
    // UI test of this panel turns it on with SF_HW_PROFILE_IN_TESTS=1 (its settings folder is a temporary one).
    const char* inTests = std::getenv("SF_HW_PROFILE_IN_TESTS");
    const bool testThis = !options_.uiTest.empty() && inTests && std::string(inTests) == "1";
    if (!testThis && (uiConfigDeterministic_ || !options_.renderPath.empty() || !options_.recordPath.empty() ||
                      !options_.uiTest.empty() || options_.measureIdle > 0.0f || options_.measureStroke > 0.0f))
        return;
    hwProfile_ = std::make_shared<HwProfileUi>();
    HwProfileUi& hw = *hwProfile_;
    hw.displayHz = displayRefreshHz(window_);
    hw.power = powerSource();
    // The calibrate command (console, MCP) measures the GPU too while the app runs.
    setGpuCalibrator([this](HardwareProfile& out, std::string&) {
        HardwareProfile cached;
        const ProfileOverrides keep = loadHardwareProfile(hardwareProfilePath(), cached) ? cached.overrides : hwProfile_->profile.overrides;
        const bool same = cached.hardware.glRenderer == hwProfile_->profile.hardware.glRenderer;
        out = calibrateNow(false, keep, same ? &cached.measuredTier : nullptr);
        std::string error;
        saveHardwareProfile(hardwareProfilePath(), out, error);
        // Applied on the next frame (the command saved it; the revision check reloads it).
        hwProfile_->revision = 0;
        return true;
    });
    HardwareInfo current;
    probeHardware(current);
    HardwareProfile cached;
    const bool hasCache = loadHardwareProfile(hardwareProfilePath(), cached);
    hw.calibrationReason = recalibrationReason(hasCache ? &cached : nullptr, current);
    HardwareProfile start;
    if (hw.calibrationReason.empty()) {
        // The cache fits: no measuring. Only what changes between runs (free video memory) is refreshed.
        cached.hardware.vramAvailableMB = current.vramAvailableMB;
        start = cached;
        hw.saved = true;
    } else {
        // What the probe alone suggests (overrides kept): the profile while calibrating, or for good when
        // SF_NO_CALIBRATION=1.
        Calibration none;
        none.skipped = true;
        start = mapProfile(current, none, hasCache ? cached.overrides : ProfileOverrides{});
        if (!calibrationDisabled()) {
            SDL_Log("Hardware profile: calibrating (%s)", hw.calibrationReason.c_str());
            hw.calibrator = std::make_unique<Calibrator>(false);
            hw.announce = true;
        }
    }
    applyHardwareProfile(start, false);
    if (!hw.calibrator) SDL_Log("Hardware profile: %s", start.reason.c_str());
}

// Makes `p` the profile in effect: the process-wide settings and the governor's starting point; `announce` (after a
// calibration that wasn't asked for) says so in the status bar and adapts the preferences a profile implies.
void App::applyHardwareProfile(const HardwareProfile& p, bool announce) {
    HwProfileUi& hw = *hwProfile_;
    hw.profile = p;
    hw.active = true;
    // The tier's knobs for this power source and display; its target Hz is the governor's (60 / 120 / 144).
    hw.tier = tierConfigFor(p, hw.power, hw.displayHz);
    setCurrentTierConfig(hw.tier);
    PerformanceSettings s = p.settings;
    s.targetFps = hw.tier.targetHz;
    s.uploadBudgetMBPerFrame = hw.tier.uploadBudgetMBPerFrame;
    hw.profile.settings.targetFps = s.targetFps;
    setCurrentPerformanceSettings(s);
    GovernorConfig cfg;
    cfg.targetFps = s.targetFps;
    cfg.minBudget = std::max<uint64_t>(250'000, s.renderTriangleBudget / 8);
    cfg.maxBudget = std::min<uint64_t>(2'000'000'000, std::max<uint64_t>(s.renderTriangleBudget * 4, cfg.minBudget));
    hw.governor.reset(cfg, s.renderTriangleBudget);
    hw.revision = performanceSettingsRevision();
    if (!announce) return;
    // A software renderer draws every sample on the CPU: no antialiasing.
    if (p.tier == ProfileTier::Software && uiPrefs_.antialiasing > 1) uiPrefs_.antialiasing = 1;
    const std::string gpu = p.hardware.glRenderer.empty() ? std::string("this computer") : p.hardware.glRenderer;
    status_ = std::string(p.calibration.skipped ? "Calibration skipped: " : "Calibrated for this computer: ") + gpu + " -> " +
              tierName(p.tier) + " profile (Preferences > Performance)";
}

void App::finishCalibration() {
    HwProfileUi& hw = *hwProfile_;
    HardwareProfile cached;
    const ProfileOverrides keep = loadHardwareProfile(hardwareProfilePath(), cached) ? cached.overrides : hw.profile.overrides;
    const bool same = !cached.hardware.glRenderer.empty() && cached.hardware.glRenderer == hw.profile.hardware.glRenderer;
    const HardwareProfile p = hw.calibrator->profile(keep, same ? &cached.measuredTier : nullptr);
    hw.calibrator.reset();
    std::string error;
    hw.saved = saveHardwareProfile(hardwareProfilePath(), p, error);
    if (!hw.saved) SDL_Log("Hardware profile not saved: %s", error.c_str());
    SDL_Log("Hardware profile: %s (%.0f ms)", p.reason.c_str(), p.calibration.durationMs);
    applyHardwareProfile(p, hw.announce);
    hw.announce = false;
}

void App::shutdownHardwareProfile() {
    setGpuCalibrator(nullptr);
    if (hwProfile_) hwProfile_->timer.release();
    hwProfile_.reset();
}

void App::hardwareProfileFrameBegin() {
    if (!hwProfile_) return;
    HwProfileUi& hw = *hwProfile_;
    if (hw.calibrator) {
        // One step per frame, after the overlay has been on screen once.
        if (hw.calibrationFrames++ > 0 && !hw.calibrator->step()) finishCalibration();
        activeUntil_ = std::max(activeUntil_, double(SDL_GetTicksNS()) * 1e-9 + 0.3); // keep the frames coming
    }
    hw.frameStartMs = nowMs();
    if (!hw.calibrator) hw.timer.begin();
}

void App::hardwareProfileFrameEnd() {
    if (!hwProfile_) return;
    HwProfileUi& hw = *hwProfile_;
    const uint64_t frame = hw.frame++;
    if (hw.calibrator) return;
    hw.timer.end(frame);
    FrameSample sample;
    sample.cpuMs = nowMs() - hw.frameStartMs;
    sample.drawnTriangles = renderer_.lastDrawnTriangles();
    sample.sceneTriangles = perf_.sceneTriangles;
    sample.moving = perf_.movingNow;
    sample.limited = perf_.proxyNow;
    hw.lastCpuMs = sample.cpuMs;
    bool changed = false;
    if (hw.timer.available()) {
        hw.pending.push_back({frame, sample});
        uint64_t doneFrame = 0;
        double gpuMs = 0.0;
        while (hw.timer.poll(doneFrame, gpuMs)) {
            while (!hw.pending.empty() && hw.pending.front().frame < doneFrame) hw.pending.pop_front(); // lost
            if (!hw.pending.empty() && hw.pending.front().frame == doneFrame) {
                FrameSample s = hw.pending.front().sample;
                hw.pending.pop_front();
                s.gpuMs = gpuMs;
                hw.lastGpuMs = gpuMs;
                changed = hw.governor.addFrame(s) || changed;
            }
        }
        while (hw.pending.size() > 8) { // the GPU time never came: count the CPU time alone
            changed = hw.governor.addFrame(hw.pending.front().sample) || changed;
            hw.pending.pop_front();
        }
    } else {
        changed = hw.governor.addFrame(sample);
    }
    if (changed && hw.profile.overrides.governor && !hw.profile.overrides.motionProxyThreshold)
        setGovernedBudgets(hw.governor.budget(), hw.governor.proxyThreshold());
    // The display (the window moved to a 120 Hz screen) or the power source (unplugged) may change: every few
    // seconds, look; a change re-applies the profile with the new tier configuration.
    if (const double t = double(SDL_GetTicksNS()) * 1e-9; t >= hw.nextPowerCheck) {
        hw.nextPowerCheck = t + 5.0;
        const int hz = displayRefreshHz(window_);
        const PowerSource power = powerSource();
        if (hz != hw.displayHz || power != hw.power) {
            const bool battery = power == PowerSource::Battery, wasBattery = hw.power == PowerSource::Battery;
            hw.displayHz = hz;
            hw.power = power;
            const HardwareProfile p = hw.profile;
            applyHardwareProfile(p, false);
            if (battery != wasBattery)
                status_ = battery ? "On battery: lighter uploads, " + std::to_string(hw.tier.targetHz) + " Hz"
                                  : "On mains power: " + std::string(hw.tier.draftBrushMode ? "draft" : "continuous") + " brushes, " +
                                        std::to_string(hw.tier.targetHz) + " Hz";
        }
    }
    // The set_performance_override or calibrate command changed the profile: take it up.
    if (performanceSettingsRevision() != hw.revision) {
        HardwareProfile cached;
        if (loadHardwareProfile(hardwareProfilePath(), cached)) applyHardwareProfile(cached, false);
        else hw.revision = performanceSettingsRevision();
    }
}

bool App::hardwareCalibrating() const { return hwProfile_ && hwProfile_->calibrator; }

void App::drawHardwareProfileOverlay() {
    if (!hwProfile_ || !hwProfile_->calibrator) return;
    HwProfileUi& hw = *hwProfile_;
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + vp->WorkSize.y * 0.5f), ImGuiCond_Always,
                            ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(380.0f * uiScale_, 0.0f));
    ImGui::SetNextWindowFocus();
    if (ImGui::Begin("##calibration", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                         ImGuiWindowFlags_AlwaysAutoResize)) {
        if (fontBold_) ImGui::PushFont(fontBold_);
        ImGui::TextUnformatted("Getting to know this computer");
        if (fontBold_) ImGui::PopFont();
        dim("A two-second check of the GPU, the CPU and memory, so big scenes stay smooth. Once, then again only when "
            "the graphics card, its driver or SculptForge changes. Nothing leaves this computer.");
        ImGui::ProgressBar(hw.calibrator->progress(), ImVec2(-1.0f, 0.0f), hw.calibrator->stageName());
        const bool esc = ImGui::IsKeyPressed(ImGuiKey_Escape, false);
        if (ImGui::Button("Skip") || esc) {
            hw.calibrator->skip();
            finishCalibration();
        }
        ImGui::SameLine();
        ImGui::TextDisabled("Esc");
    }
    ImGui::End();
}

void App::registerPerformanceProfileUiActions() {
    {
        Action a;
        a.id = "perf.recalibrate";
        a.label = "Re-calibrate";
        a.palette = kPath;
        a.run = [this] {
            if (!hwProfile_) {
                status_ = "Calibration is off in this run (tests, renders and recordings use fixed settings)";
                return;
            }
            hwProfile_->calibrator = std::make_unique<Calibrator>(false);
            hwProfile_->calibrationFrames = 0;
            hwProfile_->announce = true;
            status_ = "Calibrating...";
        };
        a.description = "Measure this computer again (about 2 seconds): the GPU's triangle throughput and upload speed, "
                        "brush speed and memory bandwidth, then pick the performance profile. Your overrides are kept.";
        actions_.add(std::move(a));
    }
    {
        Action a;
        a.id = "perf.governor";
        a.label = "Frame Governor";
        a.palette = kPath;
        a.run = [this] {
            if (!hwProfile_) return;
            ProfileOverrides o = hwProfile_->profile.overrides;
            o.governor = !o.governor;
            const HardwareProfile p = mapProfile(hwProfile_->profile.hardware, hwProfile_->profile.calibration, o);
            std::string error;
            saveHardwareProfile(hardwareProfilePath(), p, error);
            applyHardwareProfile(p, false);
            status_ = o.governor ? "Frame governor on" : "Frame governor off (the profile's budgets stay as set)";
        };
        a.checked = [this] { return hwProfile_ && hwProfile_->profile.overrides.governor; };
        a.description = "Hold the view at the target frame rate (60 fps, or 120 on a 120 Hz display) by drawing big "
                        "scenes' lighter motion proxy sooner or later while the view moves. Only what you see changes, "
                        "never the model.";
        actions_.add(std::move(a));
    }
}

void App::drawPerformanceProfileSection() {
    ImGui::TextUnformatted("Hardware profile");
    ui::HelpText("Hardware profile", nullptr,
                 "SculptForge measures this computer on first launch (and when the graphics card, its driver or the "
                 "version changes) and picks settings to match: how many triangles the view draws per frame, when big "
                 "scenes switch to their lighter motion proxy, GPU memory and upload budgets, texture and subdivision "
                 "limits, threads. Nothing is sent anywhere.",
                 kPath);
    if (!hwProfile_) {
        dim("Off in this run: tests, renders and recordings use fixed settings.");
        ImGui::Spacing();
        return;
    }
    HwProfileUi& hw = *hwProfile_;
    if (hw.calibrator) {
        ImGui::ProgressBar(hw.calibrator->progress(), ImVec2(-1.0f, 0.0f), hw.calibrator->stageName());
        ImGui::Spacing();
        return;
    }
    const HardwareProfile& p = hw.profile;
    const HardwareInfo& h = p.hardware;
    const Calibration& c = p.calibration;
    const PerformanceSettings& s = p.settings;
    // "Detected Apple M1 Pro, 21.3 GB unified -> Workstation"
    // Shared memory shows what the GPU may use (the budget), not what the driver reports (often all of the RAM).
    const bool shared = h.gpuKind == GpuKind::Unified || h.gpuKind == GpuKind::Integrated;
    const std::string vram = shared      ? formatMegabytes(s.vramBudgetMB) + " shared"
                             : h.vramMB ? formatMegabytes(h.vramMB)
                                        : std::string("memory unknown");
    if (fontBold_) ImGui::PushFont(fontBold_);
    ImGui::TextWrapped("Detected %s, %s -> %s", h.glRenderer.empty() ? "an unknown GPU" : h.glRenderer.c_str(), vram.c_str(),
                       tierName(p.tier));
    if (fontBold_) ImGui::PopFont();
    // Why (the reason without the "Detected ... -> tier" the line above already says).
    std::string why = p.reason;
    if (const size_t arrow = why.find(" -> "); arrow != std::string::npos)
        if (const size_t open = why.find(" (", arrow); open != std::string::npos && why.back() == ')')
            why = why.substr(open + 2, why.size() - open - 3);
    dim(why);
    std::string measured;
    if (c.skipped) measured = "Not measured (skipped): settings from the GPU type and memory sizes.\n";
    else {
        measured += "Triangles " + fmt("%.0fM/s", c.trianglesPerSecondM) + (c.gpuTimer ? " (GPU timer)" : " (glFinish)") +
                    "   Upload " + fmt("%.1f GB/s", c.uploadGBps) + "\n";
        measured += "Brush dab " + fmt("%.2f ms", c.dabMs) + " (" + fmt("%.1fk", c.dabVertices / 1000.0) + " vertices)   Memory " +
                    fmt("%.1f GB/s", c.memoryGBps) + "\n";
    }
    std::string simd;
    for (const auto& x : h.simd) simd += (simd.empty() ? "" : ", ") + x;
    measured += h.cpu + (h.cpu.empty() ? "" : ", ") + std::to_string(h.cpuThreads) + " threads" + (simd.empty() ? "" : " (" + simd + ")") +
                ", " + formatMegabytes(h.ramMB) + " RAM\nVideo memory: " + (h.vramSource.empty() ? std::string("none") : h.vramSource) +
                (h.vramAvailableMB ? ", " + formatMegabytes(h.vramAvailableMB) + " free" : std::string()) + "\n" + h.os + ", OpenGL " +
                h.glVersion;
    ImGui::TextWrapped("%s", measured.c_str());
    ImGui::LogText("%s", measured.c_str());

    ImGui::Spacing();
    const uint64_t governed = renderTriangleBudget();
    std::string set = "View budget " + formatTriangles(s.renderTriangleBudget) + " triangles" +
                      (governed != s.renderTriangleBudget ? " (governed: " + formatTriangles(governed) + ")" : std::string()) +
                      "\nMotion proxy from " + formatTriangles(motionProxyThreshold()) + " triangles\nGPU memory budget " +
                      formatMegabytes(s.vramBudgetMB) + ", uploads " + std::to_string(s.uploadBudgetMBPerFrame) +
                      " MB per frame\nTextures up to " + std::to_string(s.maxTextureSize) + ", subdivision up to " +
                      formatTriangles(s.maxSubdivisionTriangles) + ", micro-triangles up to " + formatTriangles(s.microTriangleLimit) +
                      "\n" + std::to_string(s.threads) + " threads" +
                      (s.threads != ThreadPool::shared().threads() ? " (after a restart; now " + std::to_string(ThreadPool::shared().threads()) + ")" : std::string()) +
                      ", target " + std::to_string(s.targetFps) + " fps";
    const TierConfig& tc = hw.tier;
    set += "\n" + std::string(tc.draftBrushMode ? "Draft brushes (normals and cavity at pen release)" : "Continuous brushes") +
           ", active " + formatTriangles(tc.activeTriangleBudget) + " triangles, cache up to " + formatMegabytes(tc.residentCacheCapMB) +
           (tc.zeroCopyUMA ? ", zero-copy (shared memory)" : "") +
           (tc.onBattery() ? "\nOn battery" : tc.power == PowerSource::AC ? "\nOn mains power" : "");
    ImGui::TextWrapped("%s", set.c_str());
    ImGui::LogText("%s", set.c_str());

    bool governor = p.overrides.governor;
    if (ImGui::Checkbox("Frame Governor", &governor)) actions_.find("perf.governor")->run();
    ui::HelpAction(*this, "perf.governor");
    if (governor && hw.governor.decisions() > 0) {
        const char* action = hw.governor.lastAction() == FrameGovernor::Action::Lowered  ? "drawing less"
                             : hw.governor.lastAction() == FrameGovernor::Action::Raised ? "drawing more"
                                                                                          : "holding";
        dim("Moving view: " + fmt("%.1f ms", hw.governor.lastCostMs()) + " of " + fmt("%.1f ms", hw.governor.targetMs()) + " per frame (" +
            (hw.lastGpuMs >= 0.0 ? "GPU timer" : "CPU time") + "), " + action);
    } else if (governor) {
        dim("Learns from frames while the view moves.");
    }
    if (ImGui::Button("Re-calibrate")) actions_.find("perf.recalibrate")->run();
    ui::HelpAction(*this, "perf.recalibrate");

    // --- Manual overrides ---
    if (ImGui::TreeNode("Manual overrides")) {
        ProfileOverrides o = p.overrides;
        bool edited = false;
        const char* tiers[] = {"Tier: automatic", "Software (safe mode)", "Laptop / Budget", "Workstation"};
        int tierIndex = 0;
        ProfileTier forced;
        if (!o.tier.empty() && tierFromName(o.tier, forced)) tierIndex = int(forced) + 1;
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::Combo("##perf.tier", &tierIndex, tiers, 4)) {
            o.tier = tierIndex == 0 ? std::string() : std::string(tierName(ProfileTier(tierIndex - 1)));
            edited = true;
        }
        // Numbers: 0 = automatic (shown as the profile's value, dimmed hint).
        auto number = [&](const char* label, uint64_t& value, uint64_t automatic, double scale, const char* unit) {
            float v = float(double(value ? value : 0) / scale);
            ImGui::SetNextItemWidth(120.0f * uiScale_);
            const std::string hint = fmt("%.1f", double(automatic) / scale) + " " + unit + " (auto)";
            ImGui::PushID(label);
            if (ImGui::InputFloat("##v", &v, 0.0f, 0.0f, value ? "%.1f" : hint.c_str(), ImGuiInputTextFlags_EnterReturnsTrue)) {
                value = v > 0.0f ? uint64_t(double(v) * scale) : 0;
                edited = true;
            }
            ImGui::SameLine();
            ImGui::TextUnformatted(label);
            ImGui::PopID();
        };
        number("View budget (M triangles)", o.renderTriangleBudget, p.automatic.renderTriangleBudget, 1e6, "M");
        number("Motion proxy from (M triangles)", o.motionProxyThreshold, p.automatic.motionProxyThreshold, 1e6, "M");
        number("GPU memory budget (GB)", o.vramBudgetMB, p.automatic.vramBudgetMB, 1024.0, "GB");
        number("Uploads per frame (MB)", o.uploadBudgetMBPerFrame, p.automatic.uploadBudgetMBPerFrame, 1.0, "MB");
        number("Subdivision limit (M triangles)", o.maxSubdivisionTriangles, p.automatic.maxSubdivisionTriangles, 1e6, "M");
        {
            uint64_t threads = uint64_t(o.threads);
            number("Threads (next start)", threads, uint64_t(p.automatic.threads), 1.0, "");
            o.threads = int(threads);
        }
        const char* draftNames[] = {"Draft brushes: automatic", "Draft brushes: always", "Draft brushes: never"};
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::Combo("##perf.draft", &o.draftBrush, draftNames, 3)) edited = true;
        const char* fpsNames[] = {"Target: automatic", "30 fps", "60 fps", "120 fps", "144 fps"};
        const int fpsValues[] = {0, 30, 60, 120, 144};
        int fpsIndex = 0;
        for (int i = 0; i < 5; ++i)
            if (o.targetFps == fpsValues[i]) fpsIndex = i;
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::Combo("##perf.fps", &fpsIndex, fpsNames, 5)) {
            o.targetFps = fpsValues[fpsIndex];
            edited = true;
        }
        if (o.any() && ImGui::Button("Back to automatic")) {
            const bool governorWas = o.governor;
            o = ProfileOverrides{};
            o.governor = governorWas;
            edited = true;
        }
        if (edited) {
            const HardwareProfile next = mapProfile(h, c, o);
            std::string error;
            if (!saveHardwareProfile(hardwareProfilePath(), next, error)) status_ = "Not saved: " + error;
            applyHardwareProfile(next, false);
        }
        ImGui::TreePop();
    }
    ImGui::Separator();
    ImGui::Spacing();
}

} // namespace sf
