// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Hardware tiers (docs/performance.md, "Hardware tiers"; ~/Work/.sf/billion.md): what the detected machine can carry
// beyond the base experience, so the advanced levels (bigger Focus Detail caches, more micro triangles, 1B scenes,
// 16K textures) switch on by themselves where they fit and never where they don't.
//
//   Floor     every supported machine; the budgets are exactly what every machine got before tiers (0.11.12)
//   Standard  16 GB RAM, 6+ threads, a 4 GB+ graphics card or a GPU sharing the memory
//   High      32 GB RAM, 8+ threads, an 8 GB+ graphics card or 32 GB+ shared (Apple Pro / Max)
//   Extreme   64 GB RAM, 16+ threads, a 16 GB+ graphics card or 64 GB+ shared (Apple Max / Ultra, big APUs)
//
// The tier comes from what the probe found (memory sizes, GPU type, threads), capped by what calibration measured
// (a slow GPU or slow brushes never get the upper tiers). It is separate from the render profile (ProfileTier:
// Software / Laptop / Workstation, which sets the view's triangle budgets from measured speed and is unchanged).
// At run time a memory guard steps the tier in effect down one level at a time when free memory runs low, and back
// up once there is room again; nothing fails, big scenes fall back to coarser levels.
//
// Read the budgets with currentTierConfig().budgets (core/TierConfig.h). SF_HW_SIMULATE=floor|standard|high|extreme
// forces a tier (floor also keeps its old meaning: an integrated GPU's render budgets and 4 threads); every budget can
// be pinned with SF_TIER_<NAME> (e.g. SF_TIER_FOCUS_RESIDENT_MB=1024), names as in tierBudgetFields().
#pragma once

#include "core/Json.h"

#include <cstdint>
#include <string>
#include <vector>

namespace sf {

struct HardwareInfo;
struct Calibration;
enum class ProfileTier;

enum class HwTier { Floor = 0, Standard = 1, High = 2, Extreme = 3 };
constexpr int kHwTierCount = 4;
const char* hwTierName(HwTier tier);        // "Floor", "Standard", "High", "Extreme" (logs, JSON, SF_HW_SIMULATE)
const char* hwTierDisplayName(HwTier tier); // what people see: "Essential", "Standard", "High", "Extreme"
// Either name, any case.
bool hwTierFromName(const std::string& name, HwTier& out);
// SF_HW_SIMULATE=<tier>: the forced tier, if any.
bool simulatedHwTier(HwTier& out);

// The budgets a tier gives. Floor's are the values every machine used before tiers, so the Floor tier can't regress.
struct TierBudgets {
    // Scenes
    uint64_t residentTriangles = 300'000'000;  // triangles held in full detail; past it far parts use coarse levels
    uint64_t maxMicroTriangles = 300'000'000;  // micro triangles the tier holds before Add Micro asks (it then
                                               // streams with Focus Detail rather than failing)
    bool billionScenes = false;                // 1B-triangle scenes without asking (else opt-in)
    uint64_t optInMicroTriangles = 1'000'000'000; // the opt-in ceiling (past it: coarse levels and a message, no failure)
    uint64_t maxDenseTriangles = 30'000'000;   // one dense mesh past this: open works, a notice suggests micro levels
    uint64_t microResidentMB = 512;            // GPU: the compressed resident micro pyramid (billion-ideas); over it,
                                               // coarse levels + Focus Detail stream
    double derivNormalPx = 2.0;                // triangles under this many pixels shade from screen derivatives
    uint64_t waveletDabSamples = 64;           // a dab spanning more samples than this edits the coarse level only
    int gpuDabs = 0;                           // dabs as render passes (experimental): 0 off, 1 opt-in, 2 on
    // Focus Detail (core/FocusDetail.h)
    uint64_t focusTriangles = 300'000'000;     // micro levels past this stream from a .sfdetail file
    uint64_t focusResidentMB = 512;            // CPU: full-resolution tiles kept before the LRU demotes
    uint64_t focusGpuPoolMB = 384;             // GPU page pool for close-up full tiles
    double focusUpdateMs = 3.0;                // main-thread promotion work per frame (App::updateFocusDetail)
    double focusLookaheadSeconds = 0.25;       // brush prediction
    // Coarse LOD (microlod: Renderer::stillDetail / triangleBudget)
    double stillFrameMs = 100.0;               // GPU time a full-detail still frame may take before it stays coarse
    double stillDelaySeconds = 0.15;           // rest before full detail
    double motionBudgetFraction = 0.5;         // share of the triangle budget while the view moves
    bool coarseLevels = true;                  // coarse levels for dense bases (Renderer::stillDetail)
    // Strokes (core200m)
    uint64_t draftDabVertices = 0;             // dabs moving more vertices take the draft path (0 = never, as before)
    double strokeBacklogMs = 50.0;             // queued dab work before the stroke thins dabs (App kStrokeBacklogSeconds)
    double normalsCatchUpMs = 0.0;             // exact-normal catch-up per frame after a stroke (0 = as before: no cap)
    // Textures and threads
    int maxTextureSize = 8192;                 // largest paint / bake texture (also capped by the GPU and its memory)
    int threads = 0;                           // worker threads (0 = every hardware thread, up to the pool's 16)
};
TierBudgets budgetsForTier(HwTier tier);

// The budgets by name, for JSON, SF_TIER_<NAME> and set_performance_override ("focus_resident_mb", ...).
struct TierBudgetField {
    const char* name;   // snake case
    const char* what;   // plain words for people
    enum class Kind { U64, Double, Int, Bool } kind;
    size_t offset;
};
const std::vector<TierBudgetField>& tierBudgetFields();
double tierBudgetValue(const TierBudgets& b, const TierBudgetField& f);
void setTierBudgetValue(TierBudgets& b, const TierBudgetField& f, double value);
// SF_TIER_<NAME> pins (applied last, over the tier and the memory guard).
void applyTierBudgetEnv(TierBudgets& b);
Json tierBudgetsJson(const TierBudgets& b);

// Short GPU name for people: "NVIDIA GeForce RTX 3060/PCIe/SSE2" -> "RTX 3060", "Mesa Intel(R) UHD Graphics 620 (KBL
// GT2)" -> "Intel UHD Graphics 620", "Apple M1 Pro (G13S C0)" -> "Apple M1 Pro".
std::string shortGpuName(const std::string& renderer);

// What the OpenGL version string tells (macOS stops at 4.1; the renderer needs 3.3 and uses 4.4 buffer storage when
// there). Reported only: no tier depends on GL features (Apple GPUs would all be Floor).
struct GlFeatures {
    int major = 0, minor = 0;
    bool bufferStorage = false; // GL 4.4
    bool compute = false;       // GL 4.3 compute shaders
};
GlFeatures glFeatures(const std::string& glVersion);

// The detected tier and why, in words: "RTX 3060, 12 GB VRAM, 32 GB RAM, 12 threads" and, below the top,
// what the next tier needs ("Extreme needs a 16 GB graphics card and 64 GB RAM"). `renderTier` is the measured render
// profile: Software (a software renderer, or a GPU too slow for anything else) is always Floor.
struct HwTierDetection {
    HwTier tier = HwTier::Floor;
    std::string machine; // "RTX 3060, 12 GB VRAM, 32 GB RAM, 12 threads"
    std::string limit;   // what keeps it from the next tier ("" at Extreme)
};
HwTierDetection detectHwTier(const HardwareInfo& hw, const Calibration& c, ProfileTier renderTier);
// "RTX 3060, 12 GB VRAM, 32 GB RAM: High"
std::string tierSummary(const HwTierDetection& d, HwTier inEffect);
// What the tier turns on, in plain words, one line each (Preferences > Performance, the first-run card).
std::vector<std::string> tierTurnsOn(HwTier tier, const TierBudgets& b);

// --- Memory guard: steps the tier in effect down when free memory runs low, back up when it recovers ---
struct TierMemoryGuard {
    int stepDown = 0;      // levels below the detected tier
    double tightSince = -1.0, roomySince = -1.0, lastChange = -1e9;
    // Free memory now (MB; GPU 0 = unknown). Returns +1 when it stepped down, -1 when it stepped back up, else 0.
    // Tight: under max(1 GB, a tenth of RAM) free (or under 256 MB of free video memory) for 2 s. Roomy: over max(3 GB,
    // a quarter of RAM) for 60 s. At most one change per 10 s; never below Floor (`maxSteps`).
    int update(double nowSeconds, uint64_t freeMB, uint64_t totalMB, uint64_t gpuFreeMB, int maxSteps);
};
// (Free system memory: availableMemoryBytes(), core/MemoryGuard.h.)

// --- Compute: CPU or GPU, per task (Preferences > Performance) ---
enum class ComputeTask { BrushSolver, Normals, Remesh, Bake, FocusDecode, MicroShade };
constexpr int kComputeTaskCount = 6;
enum class ComputeChoice { Auto, Cpu, Gpu };
enum class ComputePath { Cpu, Gpu };
struct ComputeTaskInfo {
    ComputeTask task;
    const char* id;    // "brush_solver"
    const char* name;  // "Brush solver"
    bool gpuPath;      // this build has a GPU path for it
};
const ComputeTaskInfo& computeTaskInfo(ComputeTask task);
const char* computeChoiceName(ComputeChoice c); // "auto", "cpu", "gpu"
bool computeChoiceFromName(const std::string& name, ComputeChoice& out);
// SF_COMPUTE=cpu|gpu|auto, else `saved`.
ComputeChoice computeChoiceWithEnv(ComputeChoice saved);
// The path a task takes: CPU where there is no GPU path or it failed this session; GPU when chosen; Auto takes the
// GPU only off the Floor tier and only where it measured at least 20 % faster (gpuMs / cpuMs 0 = not measured: CPU).
ComputePath resolveCompute(ComputeTask task, ComputeChoice choice, HwTier tier, double cpuMs, double gpuMs);
// A GPU path that failed calls this (thread-safe): the task runs on the CPU for the rest of the session and the app
// says so once (takeComputeFailureMessage).
void reportGpuComputeFailure(ComputeTask task, const std::string& why);
bool gpuComputeFailed(ComputeTask task);
bool takeComputeFailureMessage(std::string& message);
void resetComputeFailures(); // (tests)

// --- Graphics cards (the GPU picker) ---
struct GpuAdapter {
    std::string id;     // stable: "pci-0000_01_00_0" (Linux), "luid-..." (Windows)
    std::string name;   // "NVIDIA GeForce RTX 3060" (pci.ids / the driver), else "NVIDIA GPU 10de:2504"
    uint32_t vendorId = 0, deviceId = 0;
    bool discrete = false;
    bool bootDisplay = false; // the one the desktop starts on (Linux boot_vga)
    uint64_t vramMB = 0;      // where known
};
// The machine's graphics cards (no OpenGL, no network). Linux: /sys/class/drm; Windows: DXGI; elsewhere empty.
std::vector<GpuAdapter> listGpus();
// Linux: the cards under `drmRoot` (tests pass a fake tree), named from a pci.ids file (`pciIds`, "" = none).
std::vector<GpuAdapter> listGpusSysfs(const std::string& drmRoot, const std::string& pciIds);
// "NVIDIA Corporation" / "GA106 [GeForce RTX 3060 Lite Hash Rate]" from a pci.ids text: the device's name in brackets
// when there is one, prefixed by the short vendor ("NVIDIA GeForce RTX 3060 Lite Hash Rate").
std::string pciIdsName(const std::string& pciIdsText, uint32_t vendor, uint32_t device);
// What Auto picks: the discrete card with the most memory (else the first discrete, else ""= the system's choice).
std::string autoGpuId(const std::vector<GpuAdapter>& gpus);
// Before the GL context: makes the next context use `id` ("" or "auto" = Auto) through the platform's own mechanism
// (Linux: Mesa DRI_PRIME / NVIDIA PRIME render offload; Windows: the per-app graphics preference Settings > Display >
// Graphics writes). Never overrides variables the user set. Returns what it did, for the log ("" = nothing).
std::string applyGpuChoice(const std::string& id, const std::vector<GpuAdapter>& gpus);
// Undoes what applyGpuChoice left behind outside this process (Windows: the per-app preference), after a start on
// the chosen card failed.
void clearGpuChoice();

} // namespace sf
