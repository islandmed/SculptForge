// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Automatic hardware detection and calibration (docs/performance.md, "Hardware profile").
//
// What the computer is (HardwareInfo: the GPU's OpenGL strings and memory, system RAM, CPU threads, SIMD, OS)
// and how fast it measured (Calibration: triangle throughput, buffer upload bandwidth, brush dab cost, memory
// bandwidth) map to a profile tier with concrete settings. Two main tiers: Laptop / Budget (8-32 GB RAM, a 2-8 GB
// GPU or smaller Apple Silicon, thermal and battery limits) and Workstation (64 GB+ RAM with a 16 GB+ discrete GPU,
// or with a GPU sharing that memory, e.g. Apple Silicon Max / Ultra or a big APU, whose pool counts as two thirds of
// the RAM; and fast measurements either way), plus a Software safe mode for llvmpipe and friends. core/TierConfig.h turns the
// profile, the power source and the display into the knobs the renderer and brushes read. Settings:
// (PerformanceSettings: render triangle budget, motion proxy threshold, VRAM budget, ...). The mapping uses
// the measurements and memory sizes, never GPU names: names only tell unified from discrete memory and spot a
// software renderer (llvmpipe and friends).
//
// Everything is cached in <config dir>/hardware_profile.json and probed again when the GPU's renderer, vendor
// or driver (GL_VERSION) string or the app version changes. Nothing here uses the network.
//
// This file is the core half (no OpenGL): the types, the CPU probes and benchmarks, the mapping, the cache and
// the JSON. The app (app/Calibration.h) adds the OpenGL probe and GPU benchmarks; the frame governor
// (core/FrameGovernor.h) adjusts the render budgets at run time.
#pragma once

#include "core/Json.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace sf {

class CommandRegistry;

enum class GpuKind { Unknown, Discrete, Integrated, Unified, Software };
const char* gpuKindName(GpuKind kind); // "discrete", "integrated", "unified", "software", "unknown"
GpuKind gpuKindFromName(const std::string& name);

enum class ProfileTier { Software, Laptop, Workstation };
const char* tierName(ProfileTier tier); // "Software", "Laptop", "Workstation"
// Also accepts "llvmpipe" / "safe" (Software), "budget" (Laptop) and "monster" (Workstation), in any case.
bool tierFromName(const std::string& name, ProfileTier& out);

// What the computer is. GPU fields stay empty / 0 where no OpenGL context was probed (sculptforge-cli).
struct HardwareInfo {
    std::string glVendor, glRenderer, glVersion;
    GpuKind gpuKind = GpuKind::Unknown;
    uint64_t vramMB = 0;          // dedicated video memory; for unified memory, what the GPU may use; 0 = unknown
    uint64_t vramAvailableMB = 0; // free right now, where the driver says (NVX / ATI meminfo); 0 = unknown
    std::string vramSource;       // how vramMB was found, e.g. "GL_NVX_gpu_memory_info", "sysfs amdgpu", "none"
    int maxTextureSize = 0;       // GL_MAX_TEXTURE_SIZE
    bool timerQuery = false;      // GL timer queries work (GL_TIME_ELAPSED)
    // The machine.
    std::string os;               // e.g. "Linux 6.9.1 aarch64", "Windows 10.0.22631 x86_64", "macOS 15.1 arm64"
    std::string cpu;              // brand string or board model, "" if unknown
    int cpuThreads = 0;           // hardware threads
    uint64_t ramMB = 0;           // physical memory
    std::vector<std::string> simd; // what the CPU supports: "sse4.2", "avx2", "avx512f", "neon", "sve"
    std::string simdBuild;        // what this build was compiled to use, e.g. "sse2", "avx2", "neon"
    std::string appVersion;
};

// Fills the machine fields (os, cpu, cpuThreads, ramMB, simd, simdBuild, appVersion).
void probeSystem(HardwareInfo& info);
// Classifies the GPU from its OpenGL strings: Software (llvmpipe, softpipe, SwiftShader, Microsoft Basic Render,
// GDI Generic, Apple Software Renderer), Unified (Apple GPUs, including Asahi Linux), Integrated (Intel UHD / Iris /
// Xe, AMD APUs whose name has no model number) or Discrete (NVIDIA GeForce / Quadro / RTX, AMD Radeon RX / Pro,
// Intel Arc A / B), else Unknown. Only memory type: tiers come from measurements.
GpuKind classifyGpu(const std::string& vendor, const std::string& renderer);
// Linux: video memory from sysfs (amdgpu's mem_info_vram_total) for the card whose PCI vendor matches `glVendor`;
// 0 if none. `sysfsRoot` is for tests (default /sys/class/drm).
uint64_t sysfsVramMB(const std::string& glVendor, const std::string& sysfsRoot = "/sys/class/drm");

// What calibration measured. Numbers are medians of `runs` runs; 0 = not measured.
struct Calibration {
    double trianglesPerSecondM = 0.0; // million triangles per second, small triangles into an offscreen target
    double uploadGBps = 0.0;          // glBufferSubData of a large buffer, GB/s (10^9 bytes)
    double dabMs = 0.0;               // one Standard brush dab (X symmetry) on a generated 1M triangle sphere (0.2M quick)
    double dabVertices = 0.0;         // vertices one dab moved (both sides)
    double memoryGBps = 0.0;          // memcpy of a 64 MB buffer, bytes copied per second (GB/s)
    bool gpuTimer = false;            // GPU numbers came from timer queries (else glFinish around the CPU clock)
    int runs = 0;
    double durationMs = 0.0;          // the whole calibration
    bool skipped = false;             // the user skipped it: the profile comes from the probe alone
    bool cpuOnly = false;             // no OpenGL context (sculptforge-cli): GPU numbers are from the cache or 0
};

// The settings a profile gives. Overridable one by one (ProfileOverrides).
struct PerformanceSettings {
    uint64_t renderTriangleBudget = 0;   // triangles the 3D view may draw per frame at the target frame rate
    uint64_t motionProxyThreshold = 0;   // scenes with at least this many visible triangles draw the motion
                                         // proxy while the view moves (Fast Navigation)
    uint64_t vramBudgetMB = 0;           // GPU memory the scene's buffers should stay within
    uint64_t uploadBudgetMBPerFrame = 0; // vertex data to send to the GPU per frame before deferring the rest
    int maxTextureSize = 0;              // largest texture to create (paint textures, bakes)
    uint64_t maxSubdivisionTriangles = 0; // default limit for Divide (subdivision levels) on this machine
    uint64_t microTriangleLimit = 0;     // micro-displacement (micromesh): most micro-triangles to generate
    int threads = 0;                     // worker threads for brushes and other per-dab work
    int targetFps = 60;                  // the governor's target (60, or 120 on a 120 Hz display)
};

// Manual overrides from Preferences > Performance (0 = automatic). Kept across re-calibrations.
struct ProfileOverrides {
    std::string tier;                     // "" = automatic, else a tierName()
    uint64_t renderTriangleBudget = 0;
    uint64_t motionProxyThreshold = 0;
    uint64_t vramBudgetMB = 0;
    uint64_t uploadBudgetMBPerFrame = 0;
    int maxTextureSize = 0;
    uint64_t maxSubdivisionTriangles = 0;
    uint64_t microTriangleLimit = 0;
    int threads = 0;
    int targetFps = 0;
    bool governor = true;                 // the adaptive frame governor (off: the budgets stay as set)
    int draftBrush = 0;                   // TierConfig::draftBrushMode: 0 automatic, 1 always, 2 never
    bool any() const;
};

struct HardwareProfile {
    HardwareInfo hardware;
    Calibration calibration;
    ProfileTier tier = ProfileTier::Laptop;
    ProfileTier measuredTier = ProfileTier::Laptop; // before a tier override (what re-calibration sticks to)
    std::string reason;          // why this tier, in words (Preferences > Performance)
    PerformanceSettings settings; // the tier's settings with the overrides applied
    PerformanceSettings automatic; // before the overrides
    ProfileOverrides overrides;
};

// Maps what was detected and measured to a tier and its settings. Deterministic: the same inputs give the same
// profile. With nothing measured (skipped, or no GPU numbers) it falls back to the memory sizes and the probe's
// GPU kind, conservatively. `previousTier` (a re-calibration of the same GPU): a measurement within 15% of the
// boundary between that tier and its neighbour keeps that tier, so the profile doesn't flip between runs.
HardwareProfile mapProfile(const HardwareInfo& hardware, const Calibration& calibration,
                           const ProfileOverrides& overrides = {}, const ProfileTier* previousTier = nullptr);
// Applies `overrides` to `automatic` (and a tier override re-maps first). Used by mapProfile.
PerformanceSettings applyOverrides(const PerformanceSettings& automatic, const ProfileOverrides& overrides);
// The settings a tier gives for this hardware and calibration (mapProfile's second half).
PerformanceSettings settingsForTier(ProfileTier tier, const HardwareInfo& hardware, const Calibration& calibration);

// --- Cache (<config dir>/hardware_profile.json) ---
constexpr int kHardwareProfileCacheVersion = 1;
std::string hardwareProfilePath(); // <app data dir>/hardware_profile.json ("" if there is none)
// The profile's file contents (JSON object with "cache_version") and back. fromJson returns false for a file of
// another cache version or a broken one.
Json hardwareProfileToJson(const HardwareProfile& profile);
bool hardwareProfileFromJson(const Json& j, HardwareProfile& out);
bool loadHardwareProfile(const std::string& path, HardwareProfile& out);
bool saveHardwareProfile(const std::string& path, const HardwareProfile& profile, std::string& error);
// The cached profile no longer fits this computer: the GPU's vendor, renderer or driver string or the app version
// changed (or nothing is cached). `current` needs only the GL strings and appVersion.
bool needsRecalibration(const HardwareProfile* cached, const HardwareInfo& current);
// Why needsRecalibration said so, for the log ("first launch", "GPU changed", "driver changed", "new version").
std::string recalibrationReason(const HardwareProfile* cached, const HardwareInfo& current);

// --- JSON output (sculptforge --calibrate, the calibrate / performance_profile commands) ---
constexpr const char* kCalibrationSchema = "sculptforge.calibration/1";
// `tier`: the tier configuration to report (core/TierConfig.h); default: the profile's on an unknown power source
// and display.
struct TierConfig;
Json calibrationJson(const HardwareProfile& profile, const TierConfig* tier = nullptr);
// Checks `j` has every field of kCalibrationSchema with the right types; `error` names the first problem.
bool validateCalibrationJson(const Json& j, std::string& error);

// --- CPU benchmarks (deterministic work, median of `runs`) ---
double measureMemoryBandwidthGBps(int runs = 5);
// Standard brush dabs on a generated sphere of about `triangles` triangles; returns ms per dab (median of runs)
// and the vertices one dab moved.
double measureDabMs(size_t triangles, int runs, int dabsPerRun, double* verticesPerDab = nullptr);
// The CPU half of calibration: memory bandwidth and dab cost into `out` (GPU fields untouched).
void calibrateCpu(Calibration& out, bool quick = false);

// --- The profile in effect (read by the renderer, the governor and micromesh) ---
// Thread-safe snapshot of the current settings. Before any profile is applied, the defaults of a Laptop with
// nothing measured.
PerformanceSettings currentPerformanceSettings();
void setCurrentPerformanceSettings(const PerformanceSettings& settings);
// Bumped by every setCurrentPerformanceSettings (e.g. the set_performance_override command): the app re-reads the
// cached profile when it changes.
uint64_t performanceSettingsRevision();
// The governed render budget (the frame governor writes it every adjustment; renderers read it every frame).
// Micromesh (feat/micromesh) amplifies displacement only up to this many triangles per frame; see
// docs/performance.md, "Render triangle budget".
uint64_t renderTriangleBudget();
uint64_t motionProxyThreshold();
void setGovernedBudgets(uint64_t renderTriangles, uint64_t proxyThreshold);

// The app installs a full calibrator (with the GPU) for the calibrate command; sculptforge-cli has none and
// calibrates the CPU only (the GPU numbers come from the cache).
using GpuCalibrator = std::function<bool(HardwareProfile& out, std::string& error)>;
void setGpuCalibrator(GpuCalibrator calibrator);

// performance_profile, calibrate, set_performance_override (Commands.cpp registers them).
void registerHardwareProfileCommands(CommandRegistry& registry);

// Numbers for people: "12.5M", "3.2 GB".
std::string formatTriangles(uint64_t triangles);
std::string formatMegabytes(uint64_t mb);

} // namespace sf
