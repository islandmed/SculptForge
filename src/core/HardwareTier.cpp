// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/HardwareTier.h"

#include "core/HardwareProfile.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dxgi.h>
#endif

namespace sf {

namespace {

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return s;
}

std::string readText(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

std::string trim(std::string s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
    size_t i = 0;
    while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
    return s.substr(i);
}

bool has(const std::string& haystack, const char* needle) { return haystack.find(needle) != std::string::npos; }

void eraseAll(std::string& s, const char* what) {
    for (size_t at; (at = s.find(what)) != std::string::npos;) s.erase(at, std::strlen(what));
}

constexpr uint64_t kM = 1'000'000;

// What the OS reports is a little under the label: a 16 GB machine says about 15.5 GB, a 12 GB card 11.8 GB.
constexpr uint64_t gb(double label) { return uint64_t(label * 1024.0 * 0.94); }

} // namespace

// ---------------------------------------------------------------------------
// Names
// ---------------------------------------------------------------------------

const char* hwTierName(HwTier t) {
    switch (t) {
    case HwTier::Floor: return "Floor";
    case HwTier::Standard: return "Standard";
    case HwTier::High: return "High";
    case HwTier::Extreme: return "Extreme";
    }
    return "Floor";
}

const char* hwTierDisplayName(HwTier t) {
    // The base tier is named for what it is, the experience tuned for the machine; never "low".
    return t == HwTier::Floor ? "Essential" : hwTierName(t);
}

bool hwTierFromName(const std::string& name, HwTier& out) {
    const std::string n = lower(trim(name));
    for (int i = 0; i < kHwTierCount; ++i)
        if (n == lower(hwTierName(HwTier(i))) || n == lower(hwTierDisplayName(HwTier(i)))) {
            out = HwTier(i);
            return true;
        }
    return false;
}

bool simulatedHwTier(HwTier& out) {
    const char* v = std::getenv("SF_HW_SIMULATE");
    return v && *v && hwTierFromName(v, out);
}

// ---------------------------------------------------------------------------
// Budgets
// ---------------------------------------------------------------------------

TierBudgets budgetsForTier(HwTier t) {
    TierBudgets b; // Floor: the values before tiers
    switch (t) {
    case HwTier::Floor: break;
    case HwTier::Standard:
        b.residentTriangles = 500 * kM;
        b.maxMicroTriangles = 500 * kM;
        b.optInMicroTriangles = 2000 * kM;
        b.maxDenseTriangles = 60 * kM;
        b.microResidentMB = 1024;
        b.focusTriangles = 400 * kM;
        b.focusResidentMB = 1024;
        b.focusGpuPoolMB = 512;
        b.focusUpdateMs = 4.0;
        break;
    case HwTier::High:
        b.residentTriangles = 1000 * kM;
        b.maxMicroTriangles = 1000 * kM;
        b.billionScenes = true;
        b.optInMicroTriangles = 8000 * kM;
        b.maxDenseTriangles = 120 * kM;
        b.microResidentMB = 3072;
        b.derivNormalPx = 1.5;
        b.waveletDabSamples = 96;
        b.gpuDabs = 1;
        b.focusTriangles = 600 * kM;
        b.focusResidentMB = 2048;
        b.focusGpuPoolMB = 1024;
        b.focusUpdateMs = 4.0;
        b.maxTextureSize = 16384;
        break;
    case HwTier::Extreme:
        b.residentTriangles = 2000 * kM;
        b.maxMicroTriangles = 2000 * kM;
        b.billionScenes = true;
        b.optInMicroTriangles = 16000 * kM;
        b.maxDenseTriangles = 250 * kM;
        b.microResidentMB = 8192;
        b.derivNormalPx = 1.0;
        b.waveletDabSamples = 128;
        b.gpuDabs = 1;
        b.focusTriangles = 1000 * kM;
        b.focusResidentMB = 4096;
        b.focusGpuPoolMB = 2048;
        b.focusUpdateMs = 6.0;
        b.maxTextureSize = 16384;
        break;
    }
    return b;
}

#define SF_FIELD(member, kind, what) {#member, what, TierBudgetField::Kind::kind, offsetof(TierBudgets, member)}

const std::vector<TierBudgetField>& tierBudgetFields() {
    // Snake-case names are made from the members below at first use (focusResidentMB -> focus_resident_mb).
    static const std::vector<TierBudgetField> fields = [] {
        std::vector<TierBudgetField> f = {
            SF_FIELD(residentTriangles, U64, "triangles held in full detail"),
            SF_FIELD(maxMicroTriangles, U64, "micro triangles held without asking"),
            SF_FIELD(billionScenes, Bool, "1B-triangle scenes without asking"),
            SF_FIELD(optInMicroTriangles, U64, "micro triangles when you opt in"),
            SF_FIELD(maxDenseTriangles, U64, "one dense mesh before a hint"),
            SF_FIELD(microResidentMB, U64, "GPU memory for resident micro detail (MB)"),
            SF_FIELD(derivNormalPx, Double, "pixel size for derivative shading"),
            SF_FIELD(waveletDabSamples, U64, "dab samples before coarse-level edits"),
            SF_FIELD(gpuDabs, Int, "GPU dabs (0 off, 1 opt-in, 2 on)"),
            SF_FIELD(focusTriangles, U64, "micro triangles before Focus Detail streams"),
            SF_FIELD(focusResidentMB, U64, "Focus Detail tiles in memory (MB)"),
            SF_FIELD(focusGpuPoolMB, U64, "Focus Detail GPU pool (MB)"),
            SF_FIELD(focusUpdateMs, Double, "Focus Detail work per frame (ms)"),
            SF_FIELD(focusLookaheadSeconds, Double, "Focus Detail brush look-ahead (s)"),
            SF_FIELD(stillFrameMs, Double, "still frame GPU time before preview detail (ms)"),
            SF_FIELD(stillDelaySeconds, Double, "rest before full detail (s)"),
            SF_FIELD(motionBudgetFraction, Double, "triangle budget share while moving"),
            SF_FIELD(coarseLevels, Bool, "coarse levels for dense bases"),
            SF_FIELD(draftDabVertices, U64, "vertices per dab before draft dabs (0 never)"),
            SF_FIELD(strokeBacklogMs, Double, "queued stroke work (ms)"),
            SF_FIELD(normalsCatchUpMs, Double, "normal catch-up per frame (ms, 0 no cap)"),
            SF_FIELD(maxTextureSize, Int, "largest texture"),
            SF_FIELD(threads, Int, "worker threads (0 all)"),
        };
        static std::vector<std::string> names; // (stable storage for the converted names)
        names.reserve(f.size());
        for (auto& x : f) {
            std::string s;
            const std::string in = x.name;
            for (size_t i = 0; i < in.size(); ++i) {
                const char c = in[i];
                const bool upper = std::isupper(static_cast<unsigned char>(c));
                const bool prevLower = i > 0 && !std::isupper(static_cast<unsigned char>(in[i - 1]));
                if (upper && i > 0 && (prevLower || (i + 1 < in.size() && std::islower(static_cast<unsigned char>(in[i + 1])))))
                    s += '_';
                s += char(std::tolower(static_cast<unsigned char>(c)));
            }
            names.push_back(s);
            x.name = names.back().c_str();
        }
        return f;
    }();
    return fields;
}

#undef SF_FIELD

double tierBudgetValue(const TierBudgets& b, const TierBudgetField& f) {
    const char* p = reinterpret_cast<const char*>(&b) + f.offset;
    switch (f.kind) {
    case TierBudgetField::Kind::U64: return double(*reinterpret_cast<const uint64_t*>(p));
    case TierBudgetField::Kind::Double: return *reinterpret_cast<const double*>(p);
    case TierBudgetField::Kind::Int: return double(*reinterpret_cast<const int*>(p));
    case TierBudgetField::Kind::Bool: return *reinterpret_cast<const bool*>(p) ? 1.0 : 0.0;
    }
    return 0.0;
}

void setTierBudgetValue(TierBudgets& b, const TierBudgetField& f, double v) {
    char* p = reinterpret_cast<char*>(&b) + f.offset;
    switch (f.kind) {
    case TierBudgetField::Kind::U64: *reinterpret_cast<uint64_t*>(p) = v > 0.0 ? uint64_t(v) : 0; break;
    case TierBudgetField::Kind::Double: *reinterpret_cast<double*>(p) = std::max(0.0, v); break;
    case TierBudgetField::Kind::Int: *reinterpret_cast<int*>(p) = std::max(0, int(v)); break;
    case TierBudgetField::Kind::Bool: *reinterpret_cast<bool*>(p) = v != 0.0; break;
    }
}

void applyTierBudgetEnv(TierBudgets& b) {
    for (const TierBudgetField& f : tierBudgetFields()) {
        std::string env = "SF_TIER_";
        for (const char* c = f.name; *c; ++c) env += char(std::toupper(static_cast<unsigned char>(*c)));
        const char* v = std::getenv(env.c_str());
        if (!v || !*v) continue;
        char* end = nullptr;
        const double d = std::strtod(v, &end);
        if (end != v) setTierBudgetValue(b, f, d);
        else if (f.kind == TierBudgetField::Kind::Bool) setTierBudgetValue(b, f, lower(v) == "on" || lower(v) == "true" ? 1.0 : 0.0);
    }
}

Json tierBudgetsJson(const TierBudgets& b) {
    Json j = Json::object();
    for (const TierBudgetField& f : tierBudgetFields()) {
        if (f.kind == TierBudgetField::Kind::Bool) j.set(f.name, tierBudgetValue(b, f) != 0.0);
        else j.set(f.name, tierBudgetValue(b, f));
    }
    return j;
}

// ---------------------------------------------------------------------------
// Detection
// ---------------------------------------------------------------------------

std::string shortGpuName(const std::string& renderer) {
    std::string s = renderer;
    // Mesa's layered drivers name the real GPU in brackets: "D3D12 (NVIDIA GeForce RTX 3060)" (WSL),
    // "zink Vulkan 1.3(NVIDIA GeForce RTX 3060 (NVIDIA_PROPRIETARY))".
    if (s.rfind("D3D12 (", 0) == 0 || s.rfind("zink ", 0) == 0) {
        const size_t open = s.find('(');
        if (open != std::string::npos && open + 1 < s.size()) {
            s.erase(0, open + 1);
            if (!s.empty() && s.back() == ')') s.pop_back();
        }
    }
    // Driver suffixes: "/PCIe/SSE2", Mesa's " (radeonsi, navi23, LLVM 15.0.7, DRM 3.54, 6.5.0)", "(KBL GT2)".
    if (const size_t slash = s.find('/'); slash != std::string::npos) s.resize(slash);
    if (const size_t paren = s.find(" ("); paren != std::string::npos) s.resize(paren);
    eraseAll(s, "(R)");
    eraseAll(s, "(TM)");
    eraseAll(s, "(tm)");
    if (s.rfind("Mesa ", 0) == 0) s.erase(0, 5);
    if (s.rfind("NVIDIA GeForce ", 0) == 0) s.erase(0, 15);
    else if (s.rfind("NVIDIA ", 0) == 0) s.erase(0, 7);
    if (s.rfind("AMD Radeon RX", 0) == 0) s.erase(0, 4);
    std::string out;
    for (char c : s)
        if (!(c == ' ' && (out.empty() || out.back() == ' '))) out += c;
    out = trim(out);
    return out.empty() ? trim(renderer) : out;
}

GlFeatures glFeatures(const std::string& v) {
    GlFeatures f;
    size_t i = 0;
    while (i < v.size() && !std::isdigit(static_cast<unsigned char>(v[i]))) ++i;
    if (i < v.size() && std::sscanf(v.c_str() + i, "%d.%d", &f.major, &f.minor) == 2) {
        const int n = f.major * 10 + f.minor;
        f.compute = n >= 43;
        f.bufferStorage = n >= 44;
    } else {
        f.major = f.minor = 0;
    }
    return f;
}

namespace {

struct TierNeed {
    uint64_t ramMB;
    int threads;
    uint64_t discreteMB;  // a graphics card with this much memory, or ...
    uint64_t sharedRamMB; // ... a GPU sharing this much RAM (Apple, Asahi, big APUs)
    double triangles;     // measured million triangles / s (when measured)
    const char* words;    // what the tier needs, for "needs ..."
};

// Standard, High, Extreme (Floor needs nothing).
const TierNeed kNeeds[3] = {
    {gb(16), 6, gb(4), gb(16), 120.0, "16 GB RAM, 6 threads and a 4 GB graphics card"},
    {gb(32), 8, gb(8), gb(32), 300.0, "32 GB RAM, 8 threads and an 8 GB graphics card (or 32 GB shared)"},
    {gb(64), 16, gb(16), gb(64), 1000.0, "64 GB RAM, 16 threads and a 16 GB graphics card (or 64 GB shared)"},
};

// Why `hw` falls short of `need` ("" = it doesn't).
std::string shortOf(const HardwareInfo& hw, const Calibration& c, const TierNeed& need, int level) {
    if (hw.ramMB && hw.ramMB < need.ramMB) return "RAM";
    if (hw.cpuThreads && hw.cpuThreads < need.threads) return "threads";
    switch (hw.gpuKind) {
    case GpuKind::Discrete:
        if (hw.vramMB && hw.vramMB < need.discreteMB) return "video memory";
        break;
    case GpuKind::Unified:
        if (hw.ramMB < need.sharedRamMB) return "shared memory";
        break;
    case GpuKind::Integrated:
        // Integrated GPUs (Intel, AMD APUs) share the RAM but draw slower: up to Standard, or High for a big APU with
        // 64 GB or more (Strix Halo, measured fast).
        if (level >= 1 && hw.ramMB < kNeeds[2].sharedRamMB) return "integrated graphics";
        if (level >= 2) return "integrated graphics";
        break;
    case GpuKind::Unknown: return "unknown graphics"; // (no GPU probed: stay on the base tier)
    case GpuKind::Software: return "software rendering";
    }
    if (c.trianglesPerSecondM > 0.0 && c.trianglesPerSecondM < need.triangles) return "measured GPU speed";
    if (c.dabMs > 0.0 && c.dabMs > (level >= 1 ? 2.0 : 4.0)) return "measured brush speed";
    return {};
}

// "RAM" -> "this computer has less RAM", for the panel's limit line.
std::string shortfallWords(const std::string& why) {
    if (why == "threads") return "this computer has fewer threads";
    if (why == "RAM" || why == "video memory" || why == "shared memory") return "this computer has less " + why;
    if (why == "integrated graphics") return "this computer draws with integrated graphics";
    if (why == "unknown graphics") return "the graphics card could not be identified";
    if (why == "measured GPU speed") return "the graphics card measured slower";
    if (why == "measured brush speed") return "brushes measured slower";
    return why;
}

} // namespace

HwTierDetection detectHwTier(const HardwareInfo& hw, const Calibration& c, ProfileTier renderTier) {
    HwTierDetection d;
    char buf[160];
    const std::string gpu = hw.glRenderer.empty() ? std::string() : shortGpuName(hw.glRenderer);
    std::string mem;
    if (hw.gpuKind == GpuKind::Discrete && hw.vramMB) {
        std::snprintf(buf, sizeof buf, "%.0f GB VRAM", double(hw.vramMB) / 1024.0 + 0.3);
        mem = buf;
    } else if (hw.gpuKind == GpuKind::Unified || hw.gpuKind == GpuKind::Integrated) {
        mem = "shared memory";
    }
    std::snprintf(buf, sizeof buf, "%.0f GB RAM", double(hw.ramMB) / 1024.0 + 0.3);
    for (const std::string& part : {gpu, mem, hw.ramMB ? std::string(buf) : std::string(),
                                    hw.cpuThreads ? std::to_string(hw.cpuThreads) + " threads" : std::string()})
        if (!part.empty()) d.machine += (d.machine.empty() ? "" : ", ") + part;
    if (d.machine.empty()) d.machine = "this computer";
    if (renderTier == ProfileTier::Software || hw.gpuKind == GpuKind::Software) {
        d.tier = HwTier::Floor;
        d.limit = "software rendering";
        return d;
    }
    int level = 0;
    while (level < 3) {
        const std::string why = shortOf(hw, c, kNeeds[level], level);
        if (!why.empty()) {
            d.limit = std::string(hwTierDisplayName(HwTier(level + 1))) + " needs " + kNeeds[level].words + " (" + shortfallWords(why) + ")";
            break;
        }
        ++level;
    }
    d.tier = HwTier(level);
    return d;
}

std::string tierSummary(const HwTierDetection& d, HwTier inEffect) {
    return d.machine + ": " + hwTierDisplayName(inEffect);
}

std::vector<std::string> tierTurnsOn(HwTier tier, const TierBudgets& b) {
    // Only what this build really does with the tier (the other budgets go to their readers as they land).
    std::vector<std::string> out;
    if (tier == HwTier::Floor) {
        out.push_back("Settings tuned for this machine, so sculpting stays smooth");
        out.push_back("Focus Detail keeps " + formatMegabytes(b.focusResidentMB) + " of full-resolution detail close by");
    } else {
        out.push_back("Focus Detail keeps " + formatMegabytes(b.focusResidentMB) + " of full-resolution detail close by, and streams it in faster");
    }
    if (b.maxTextureSize >= 16384) out.push_back("16K paint and bake textures");
    out.push_back("Free memory is watched: if it gets tight, the tier steps down instead of running out");
    return out;
}

// ---------------------------------------------------------------------------
// Memory guard
// ---------------------------------------------------------------------------

int TierMemoryGuard::update(double now, uint64_t freeMB, uint64_t totalMB, uint64_t gpuFreeMB, int maxSteps) {
    if (freeMB == 0 && gpuFreeMB == 0) return 0; // nothing known
    const uint64_t tightBelow = std::max<uint64_t>(1024, totalMB / 10);
    const uint64_t roomyAbove = std::max<uint64_t>(3072, totalMB / 4);
    const bool tight = (freeMB && freeMB < tightBelow) || (gpuFreeMB && gpuFreeMB < 256);
    const bool roomy = (!freeMB || freeMB > roomyAbove) && (!gpuFreeMB || gpuFreeMB > 1024);
    tightSince = tight ? (tightSince < 0.0 ? now : tightSince) : -1.0;
    roomySince = roomy ? (roomySince < 0.0 ? now : roomySince) : -1.0;
    if (now - lastChange < 10.0) return 0;
    if (tight && now - tightSince >= 2.0 && stepDown < maxSteps) {
        ++stepDown;
        lastChange = now;
        tightSince = now;
        return 1;
    }
    if (roomy && stepDown > 0 && now - roomySince >= 60.0) {
        --stepDown;
        lastChange = now;
        roomySince = now;
        return -1;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// Compute
// ---------------------------------------------------------------------------

namespace {

// Which tasks have a GPU path in this build. None yet: micro shading's (perf/micro-gpu-shade) is parked, measured
// slower on the floor tier; the research track has GPU dabs. They flip here when a path lands.
const ComputeTaskInfo kTasks[kComputeTaskCount] = {
    {ComputeTask::BrushSolver, "brush_solver", "Brush solver", false},
    {ComputeTask::Normals, "normals", "Normals", false},
    {ComputeTask::Remesh, "remesh", "Remesh", false},
    {ComputeTask::Bake, "bake", "Bake", false},
    {ComputeTask::FocusDecode, "focus_decode", "Focus Detail decode", false},
    {ComputeTask::MicroShade, "micro_shade", "Micro detail shading", false},
};

std::mutex g_computeMutex;
bool g_failed[kComputeTaskCount] = {};
std::string g_failureMessage;

} // namespace

const ComputeTaskInfo& computeTaskInfo(ComputeTask t) { return kTasks[std::clamp(int(t), 0, kComputeTaskCount - 1)]; }

const char* computeChoiceName(ComputeChoice c) {
    switch (c) {
    case ComputeChoice::Cpu: return "cpu";
    case ComputeChoice::Gpu: return "gpu";
    case ComputeChoice::Auto: break;
    }
    return "auto";
}

bool computeChoiceFromName(const std::string& name, ComputeChoice& out) {
    const std::string n = lower(trim(name));
    if (n == "auto" || n.empty()) out = ComputeChoice::Auto;
    else if (n == "cpu") out = ComputeChoice::Cpu;
    else if (n == "gpu") out = ComputeChoice::Gpu;
    else return false;
    return true;
}

ComputeChoice computeChoiceWithEnv(ComputeChoice saved) {
    ComputeChoice c;
    const char* v = std::getenv("SF_COMPUTE");
    return v && *v && computeChoiceFromName(v, c) ? c : saved;
}

ComputePath resolveCompute(ComputeTask task, ComputeChoice choice, HwTier tier, double cpuMs, double gpuMs) {
    if (!computeTaskInfo(task).gpuPath || gpuComputeFailed(task) || choice == ComputeChoice::Cpu) return ComputePath::Cpu;
    if (choice == ComputeChoice::Gpu) return ComputePath::Gpu;
    // Auto: the Floor tier stays on the CPU (as fast as before); elsewhere the GPU only where it measured faster.
    if (tier == HwTier::Floor || !(cpuMs > 0.0) || !(gpuMs > 0.0)) return ComputePath::Cpu;
    return gpuMs < cpuMs * 0.8 ? ComputePath::Gpu : ComputePath::Cpu;
}

void reportGpuComputeFailure(ComputeTask task, const std::string& why) {
    std::lock_guard<std::mutex> lock(g_computeMutex);
    const int i = std::clamp(int(task), 0, kComputeTaskCount - 1);
    if (g_failed[i]) return;
    g_failed[i] = true;
    g_failureMessage = std::string(kTasks[i].name) + " switched to the CPU: the GPU path failed (" + why + ")";
}

bool gpuComputeFailed(ComputeTask task) {
    std::lock_guard<std::mutex> lock(g_computeMutex);
    return g_failed[std::clamp(int(task), 0, kComputeTaskCount - 1)];
}

bool takeComputeFailureMessage(std::string& message) {
    std::lock_guard<std::mutex> lock(g_computeMutex);
    if (g_failureMessage.empty()) return false;
    message = std::move(g_failureMessage);
    g_failureMessage.clear();
    return true;
}

void resetComputeFailures() {
    std::lock_guard<std::mutex> lock(g_computeMutex);
    for (bool& f : g_failed) f = false;
    g_failureMessage.clear();
}

// ---------------------------------------------------------------------------
// Graphics cards
// ---------------------------------------------------------------------------

std::string pciIdsName(const std::string& text, uint32_t vendor, uint32_t device) {
    char v[8], d[8];
    std::snprintf(v, sizeof v, "%04x", vendor);
    std::snprintf(d, sizeof d, "%04x", device);
    std::istringstream in(text);
    std::string line, vendorName;
    bool inVendor = false;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        if (line[0] != '\t') {
            if (inVendor) break; // past our vendor's devices
            if (line.compare(0, 4, v) == 0 && line.size() > 6) {
                inVendor = true;
                vendorName = trim(line.substr(4));
            }
            continue;
        }
        if (!inVendor || line.size() < 7 || line[1] == '\t') continue;
        if (line.compare(1, 4, d) != 0) continue;
        std::string name = trim(line.substr(5));
        if (const size_t a = name.find('['), b = name.rfind(']'); a != std::string::npos && b != std::string::npos && b > a)
            name = name.substr(a + 1, b - a - 1);
        // "NVIDIA Corporation" -> "NVIDIA", "Advanced Micro Devices, Inc. [AMD/ATI]" -> "AMD", "Intel Corporation" -> "Intel"
        std::string shortVendor = vendorName;
        if (has(lower(vendorName), "nvidia")) shortVendor = "NVIDIA";
        else if (has(lower(vendorName), "advanced micro") || has(lower(vendorName), "amd")) shortVendor = "AMD";
        else if (has(lower(vendorName), "intel")) shortVendor = "Intel";
        if (name.rfind(shortVendor, 0) == 0) return name;
        return shortVendor + " " + name;
    }
    return {};
}

std::vector<GpuAdapter> listGpusSysfs(const std::string& root, const std::string& pciIdsPath) {
    std::vector<GpuAdapter> out;
    std::error_code ec;
    if (!std::filesystem::is_directory(root, ec)) return out;
    std::vector<std::filesystem::path> cards;
    for (const auto& e : std::filesystem::directory_iterator(root, ec)) {
        const std::string n = e.path().filename().string();
        if (n.rfind("card", 0) == 0 && n.find('-') == std::string::npos) cards.push_back(e.path());
    }
    std::sort(cards.begin(), cards.end());
    const std::string ids = pciIdsPath.empty() ? std::string() : readText(pciIdsPath);
    for (const auto& card : cards) {
        const auto dev = card / "device";
        const std::string cls = trim(readText((dev / "class").string()));
        if (cls.rfind("0x03", 0) != 0) continue; // display controllers only
        GpuAdapter g;
        g.vendorId = uint32_t(std::strtoul(trim(readText((dev / "vendor").string())).c_str(), nullptr, 16));
        g.deviceId = uint32_t(std::strtoul(trim(readText((dev / "device").string())).c_str(), nullptr, 16));
        g.bootDisplay = trim(readText((dev / "boot_vga").string())) == "1";
        if (const std::string vram = trim(readText((dev / "mem_info_vram_total").string())); !vram.empty())
            g.vramMB = std::strtoull(vram.c_str(), nullptr, 10) >> 20;
        // The PCI address: the device link's target ("../../../0000:01:00.0").
        std::string address = std::filesystem::read_symlink(dev, ec).filename().string();
        if (ec || address.empty()) address = trim(readText((dev / "uevent").string()));
        if (const size_t at = address.find("PCI_SLOT_NAME="); at != std::string::npos) {
            address = address.substr(at + 14);
            address = address.substr(0, address.find('\n'));
        }
        std::string id = "pci-" + address;
        std::replace(id.begin(), id.end(), ':', '_');
        std::replace(id.begin(), id.end(), '.', '_');
        g.id = id;
        g.name = ids.empty() ? std::string() : pciIdsName(ids, g.vendorId, g.deviceId);
        char fallback[64];
        const char* vendor = g.vendorId == 0x10de ? "NVIDIA" : g.vendorId == 0x1002 ? "AMD" : g.vendorId == 0x8086 ? "Intel" : "GPU";
        std::snprintf(fallback, sizeof fallback, "%s GPU %04x:%04x", vendor, g.vendorId, g.deviceId);
        if (g.name.empty()) g.name = fallback;
        // Discrete: NVIDIA always (Tegra has no PCI card); AMD / Intel when they report their own video memory over
        // 2 GB (APUs and iGPUs carve out 512 MB or less) or the name says so.
        const std::string ln = lower(g.name);
        if (g.vendorId == 0x10de) g.discrete = true;
        else if (g.vendorId == 0x1002) g.discrete = g.vramMB >= 2048 || has(ln, " rx ") || has(ln, "radeon pro");
        else if (g.vendorId == 0x8086) g.discrete = has(ln, "arc a") || has(ln, "arc b") || has(ln, "dg2") || has(ln, "battlemage");
        out.push_back(g);
    }
    return out;
}

#if defined(_WIN32)
namespace {
std::vector<GpuAdapter> listGpusDxgi() {
    std::vector<GpuAdapter> out;
    IDXGIFactory1* factory = nullptr;
    if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&factory))) || !factory) return out;
    IDXGIAdapter1* adapter = nullptr;
    for (UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i) {
        DXGI_ADAPTER_DESC1 d{};
        if (SUCCEEDED(adapter->GetDesc1(&d)) && !(d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) {
            GpuAdapter g;
            char name[256] = {};
            WideCharToMultiByte(CP_UTF8, 0, d.Description, -1, name, sizeof name - 1, nullptr, nullptr);
            g.name = trim(name);
            g.vendorId = d.VendorId;
            g.deviceId = d.DeviceId;
            g.vramMB = uint64_t(d.DedicatedVideoMemory) >> 20;
            g.discrete = g.vramMB >= 2048 || d.VendorId == 0x10de;
            g.bootDisplay = i == 0;
            char id[64];
            std::snprintf(id, sizeof id, "pci-%04x_%04x", d.VendorId, d.DeviceId);
            g.id = id;
            if (std::none_of(out.begin(), out.end(), [&](const GpuAdapter& o) { return o.id == g.id; })) out.push_back(g);
        }
        adapter->Release();
    }
    factory->Release();
    return out;
}
} // namespace
#endif

std::vector<GpuAdapter> listGpus() {
#if defined(_WIN32)
    return listGpusDxgi();
#elif defined(__linux__)
    std::string ids;
    for (const char* p : {"/usr/share/hwdata/pci.ids", "/usr/share/misc/pci.ids", "/usr/share/pci.ids"})
        if (std::filesystem::exists(p)) {
            ids = p;
            break;
        }
    return listGpusSysfs("/sys/class/drm", ids);
#else
    return {}; // macOS picks the graphics card itself
#endif
}

std::string autoGpuId(const std::vector<GpuAdapter>& gpus) {
    const GpuAdapter* best = nullptr;
    for (const GpuAdapter& g : gpus)
        if (g.discrete && (!best || g.vramMB > best->vramMB)) best = &g;
    return best ? best->id : std::string();
}

#if defined(__linux__)
namespace {
bool userSet(const char* name) {
    const char* v = std::getenv(name);
    return v && *v;
}
void setEnv(const char* name, const std::string& value) { setenv(name, value.c_str(), 0); }
} // namespace
#endif

std::string applyGpuChoice(const std::string& choice, const std::vector<GpuAdapter>& gpus) {
    const bool automatic = choice.empty() || choice == "auto";
    const std::string id = automatic ? autoGpuId(gpus) : choice;
    const auto it = std::find_if(gpus.begin(), gpus.end(), [&](const GpuAdapter& g) { return g.id == id; });
    if (it == gpus.end()) return {};
    // One card, or the card the desktop already runs on: nothing to do.
    if (gpus.size() < 2 || it->bootDisplay) return {};
#if defined(_WIN32)
    // Windows' own per-app preference (what Settings > System > Display > Graphics writes): high performance for a
    // discrete card, power saving for the integrated one. Read when the process creates its first GL / D3D device.
    wchar_t exe[MAX_PATH];
    if (!GetModuleFileNameW(nullptr, exe, MAX_PATH)) return {};
    HKEY key;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\DirectX\\UserGpuPreferences", 0, nullptr, 0, KEY_SET_VALUE,
                        nullptr, &key, nullptr) != ERROR_SUCCESS)
        return {};
    const wchar_t* value = it->discrete ? L"GpuPreference=2;" : L"GpuPreference=1;";
    RegSetValueExW(key, exe, 0, REG_SZ, reinterpret_cast<const BYTE*>(value), DWORD((wcslen(value) + 1) * sizeof(wchar_t)));
    RegCloseKey(key);
    return std::string("Windows graphics preference: ") + (it->discrete ? "high performance" : "power saving") + " (" + it->name + ")";
#elif defined(__linux__)
    if (it->vendorId == 0x10de) {
        // NVIDIA's PRIME render offload (the proprietary driver): documented, per process.
        if (userSet("__NV_PRIME_RENDER_OFFLOAD") || userSet("__GLX_VENDOR_LIBRARY_NAME") || userSet("DRI_PRIME")) return {};
        if (!std::filesystem::exists("/proc/driver/nvidia")) {
            // nouveau: Mesa's DRI_PRIME takes the card's PCI tag.
            setEnv("DRI_PRIME", it->id);
            return "DRI_PRIME=" + it->id + " (" + it->name + ")";
        }
        setEnv("__NV_PRIME_RENDER_OFFLOAD", "1");
        setEnv("__GLX_VENDOR_LIBRARY_NAME", "nvidia");
        setEnv("__VK_LAYER_NV_optimus", "NVIDIA_only");
        return "NVIDIA PRIME render offload (" + it->name + ")";
    }
    if (userSet("DRI_PRIME")) return {};
    setEnv("DRI_PRIME", it->id); // Mesa: "pci-0000_03_00_0"
    return "DRI_PRIME=" + it->id + " (" + it->name + ")";
#else
    return {};
#endif
}

void clearGpuChoice() {
#if defined(_WIN32)
    wchar_t exe[MAX_PATH];
    if (!GetModuleFileNameW(nullptr, exe, MAX_PATH)) return;
    RegDeleteKeyValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\DirectX\\UserGpuPreferences", exe);
#endif
}

} // namespace sf
