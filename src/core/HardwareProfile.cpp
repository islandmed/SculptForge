// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/HardwareProfile.h"

#include "core/Benchmark.h"
#include "core/BrushLibrary.h"
#include "core/Commands.h"
#include "core/Document.h"
#include "core/Sculpt.h"
#include "core/TierConfig.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <thread>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <sys/utsname.h>
#include <unistd.h>
#endif
#if defined(__APPLE__)
#include <sys/sysctl.h>
#endif
#if defined(__linux__) && (defined(__aarch64__) || defined(__arm__))
#include <sys/auxv.h>
#endif
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#define SF_X86 1
#if defined(_MSC_VER)
#include <intrin.h>
#else
#include <cpuid.h>
#endif
#endif

#ifndef SCULPTFORGE_VERSION
#define SCULPTFORGE_VERSION "dev"
#endif

namespace sf {

namespace {

using Clock = std::chrono::steady_clock;
double msSince(Clock::time_point t) { return std::chrono::duration<double, std::milli>(Clock::now() - t).count(); }

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return s;
}
bool has(const std::string& haystack, const char* needle) { return haystack.find(needle) != std::string::npos; }

std::string trim(std::string s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
    size_t i = 0;
    while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
    return s.substr(i);
}

std::string readFile(const std::string& path) {
    std::ifstream f(std::filesystem::path(std::u8string(path.begin(), path.end())), std::ios::binary);
    if (!f) return {};
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

double median(std::vector<double> v) {
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    const size_t n = v.size();
    return n % 2 ? v[n / 2] : 0.5 * (v[n / 2 - 1] + v[n / 2]);
}

constexpr uint64_t kM = 1'000'000;

uint64_t roundDownTo(uint64_t v, uint64_t step) { return step ? v / step * step : v; }

} // namespace

// ---------------------------------------------------------------------------
// Names
// ---------------------------------------------------------------------------

const char* gpuKindName(GpuKind kind) {
    switch (kind) {
    case GpuKind::Discrete: return "discrete";
    case GpuKind::Integrated: return "integrated";
    case GpuKind::Unified: return "unified";
    case GpuKind::Software: return "software";
    case GpuKind::Unknown: break;
    }
    return "unknown";
}

GpuKind gpuKindFromName(const std::string& name) {
    for (GpuKind k : {GpuKind::Discrete, GpuKind::Integrated, GpuKind::Unified, GpuKind::Software})
        if (name == gpuKindName(k)) return k;
    return GpuKind::Unknown;
}

const char* tierName(ProfileTier tier) {
    switch (tier) {
    case ProfileTier::Software: return "Software";
    case ProfileTier::Laptop: return "Laptop";
    case ProfileTier::Workstation: return "Workstation";
    }
    return "Laptop";
}

bool tierFromName(const std::string& name, ProfileTier& out) {
    const std::string n = lower(name);
    for (ProfileTier t : {ProfileTier::Software, ProfileTier::Laptop, ProfileTier::Workstation})
        if (n == lower(tierName(t))) {
            out = t;
            return true;
        }
    if (n == "llvmpipe" || n == "safe") {
        out = ProfileTier::Software;
        return true;
    }
    if (n == "budget" || n == "laptop/budget") {
        out = ProfileTier::Laptop;
        return true;
    }
    if (n == "monster") { // the 0.9.8 development builds' third tier
        out = ProfileTier::Workstation;
        return true;
    }
    return false;
}

std::string formatTriangles(uint64_t t) {
    char buf[32];
    if (t >= 1'000'000'000) std::snprintf(buf, sizeof buf, "%.2fB", double(t) / 1e9);
    else if (t >= kM) std::snprintf(buf, sizeof buf, "%.1fM", double(t) / 1e6);
    else std::snprintf(buf, sizeof buf, "%.0fk", double(t) / 1e3);
    return buf;
}

std::string formatMegabytes(uint64_t mb) {
    char buf[32];
    if (mb >= 1024) std::snprintf(buf, sizeof buf, "%.1f GB", double(mb) / 1024.0);
    else std::snprintf(buf, sizeof buf, "%llu MB", static_cast<unsigned long long>(mb));
    return buf;
}

// ---------------------------------------------------------------------------
// Probes
// ---------------------------------------------------------------------------

GpuKind classifyGpu(const std::string& vendorIn, const std::string& rendererIn) {
    const std::string v = lower(vendorIn), r = lower(rendererIn);
    if (r.empty() && v.empty()) return GpuKind::Unknown;
    for (const char* s : {"llvmpipe", "softpipe", "swiftshader", "software rasterizer", "gdi generic",
                          "microsoft basic render", "apple software renderer", "swrast", "lavapipe"})
        if (has(r, s)) return GpuKind::Software;
    // Apple GPUs share the system memory: macOS ("Apple M1 Pro") and Asahi Linux ("Apple M1 Pro (G13S C0)", "AGX").
    if (has(r, "apple m") || has(r, "apple a") || has(r, "agx") || (has(v, "apple") && !has(r, "radeon") && !has(r, "intel")))
        return GpuKind::Unified;
    if (has(v, "nvidia") || has(r, "geforce") || has(r, "quadro") || has(r, "rtx") || has(r, "tesla")) {
        if (has(r, "tegra") || has(r, "orin") || has(r, "xavier")) return GpuKind::Unified; // Jetson boards
        return GpuKind::Discrete;
    }
    if (has(v, "intel") || has(r, "intel")) {
        // Arc A / B series cards are discrete; "Intel(R) Arc(TM) Graphics" without a model is Meteor Lake's iGPU.
        if (has(r, "arc") && (has(r, "arc(tm) a") || has(r, "arc a") || has(r, "arc(tm) b") || has(r, "arc b") ||
                              has(r, "dg2") || has(r, "bmg")))
            return GpuKind::Discrete;
        return GpuKind::Integrated;
    }
    if (has(v, "amd") || has(v, "ati") || has(r, "radeon") || has(r, "amd")) {
        if (has(r, " rx ") || has(r, "rx ") || has(r, "radeon pro") || has(r, "firepro") || has(r, "instinct") ||
            has(r, "vega 56") || has(r, "vega 64") || has(r, "radeon vii"))
            return GpuKind::Discrete;
        // APUs: "AMD Radeon Graphics", "Radeon 780M", "Vega 8", RADV/radeonsi with an APU chip name.
        if (has(r, "radeon graphics") || has(r, "radeon(tm) graphics") || has(r, "vega 3") || has(r, "vega 6") ||
            has(r, "vega 8") || has(r, "vega 10") || has(r, "vega 11") || has(r, "renoir") || has(r, "cezanne") ||
            has(r, "rembrandt") || has(r, "phoenix") || has(r, "raphael") || has(r, "strix") || has(r, "van gogh") ||
            has(r, "gfx1103") || has(r, "gfx1150") || has(r, "gfx1035") || has(r, "gfx90c") || has(r, "760m") ||
            has(r, "780m") || has(r, "880m") || has(r, "890m") || has(r, "8060s") || has(r, "8050s"))
            return GpuKind::Integrated;
        return GpuKind::Discrete;
    }
    if (has(r, "mali") || has(r, "adreno") || has(r, "powervr") || has(r, "videocore") || has(r, "v3d") ||
        has(r, "immortalis"))
        return GpuKind::Integrated;
    if (has(r, "virgl") || has(r, "vmware") || has(r, "svga3d") || has(r, "parallels") || has(r, "virtio"))
        return GpuKind::Unknown; // virtual machines: the host's GPU, through a translation layer
    return GpuKind::Unknown;
}

uint64_t sysfsVramMB(const std::string& glVendor, const std::string& sysfsRoot) {
    const std::string v = lower(glVendor);
    std::string wantVendor;
    if (has(v, "amd") || has(v, "ati")) wantVendor = "0x1002";
    else if (has(v, "nvidia")) wantVendor = "0x10de";
    else if (has(v, "intel")) wantVendor = "0x8086";
    std::error_code ec;
    uint64_t best = 0;
    for (const auto& entry : std::filesystem::directory_iterator(sysfsRoot, ec)) {
        const std::string name = entry.path().filename().string();
        if (name.rfind("card", 0) != 0 || name.find('-') != std::string::npos) continue; // cardN, not cardN-HDMI-A-1
        const std::filesystem::path dev = entry.path() / "device";
        const std::string vendor = trim(readFile((dev / "vendor").string()));
        if (!wantVendor.empty() && vendor != wantVendor) continue;
        const std::string total = trim(readFile((dev / "mem_info_vram_total").string())); // amdgpu, bytes
        if (total.empty()) continue;
        const uint64_t mb = std::strtoull(total.c_str(), nullptr, 10) >> 20;
        best = std::max(best, mb);
    }
    return best;
}

void probeSystem(HardwareInfo& info) {
    info.appVersion = SCULPTFORGE_VERSION;
    info.cpuThreads = int(std::max(1u, std::thread::hardware_concurrency()));
    // --- RAM and OS ---
#if defined(_WIN32)
    MEMORYSTATUSEX ms{};
    ms.dwLength = sizeof ms;
    if (GlobalMemoryStatusEx(&ms)) info.ramMB = uint64_t(ms.ullTotalPhys) >> 20;
    std::string os = "Windows";
    using RtlGetVersionFn = LONG(WINAPI*)(PRTL_OSVERSIONINFOW);
    if (HMODULE ntdll = GetModuleHandleW(L"ntdll.dll")) {
        auto rtlGetVersion = reinterpret_cast<RtlGetVersionFn>(reinterpret_cast<void*>(GetProcAddress(ntdll, "RtlGetVersion")));
        RTL_OSVERSIONINFOW ver{};
        ver.dwOSVersionInfoSize = sizeof ver;
        if (rtlGetVersion && rtlGetVersion(&ver) == 0)
            os += " " + std::to_string(ver.dwMajorVersion) + "." + std::to_string(ver.dwMinorVersion) + "." +
                  std::to_string(ver.dwBuildNumber);
    }
    SYSTEM_INFO si{};
    GetNativeSystemInfo(&si);
    os += si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_ARM64 ? " arm64" : " x86_64";
    info.os = os;
#else
    const long pages = sysconf(_SC_PHYS_PAGES), pageSize = sysconf(_SC_PAGE_SIZE);
    if (pages > 0 && pageSize > 0) info.ramMB = (uint64_t(pages) * uint64_t(pageSize)) >> 20;
    struct utsname u {};
    if (uname(&u) == 0) info.os = std::string(u.sysname) + " " + u.release + " " + u.machine;
#if defined(__APPLE__)
    {
        uint64_t mem = 0;
        size_t len = sizeof mem;
        if (sysctlbyname("hw.memsize", &mem, &len, nullptr, 0) == 0) info.ramMB = mem >> 20;
        char ver[64] = {};
        len = sizeof ver;
        if (sysctlbyname("kern.osproductversion", ver, &len, nullptr, 0) == 0) info.os = std::string("macOS ") + ver + " " + u.machine;
        char brand[128] = {};
        len = sizeof brand;
        if (sysctlbyname("machdep.cpu.brand_string", brand, &len, nullptr, 0) == 0) info.cpu = trim(brand);
    }
#endif
#endif
    // --- CPU name and SIMD ---
    info.simd.clear();
#if defined(SF_X86)
    {
        unsigned regs[12] = {};
        bool ok = true;
        for (unsigned i = 0; i < 3 && ok; ++i) {
#if defined(_MSC_VER)
            int r[4];
            __cpuid(r, int(0x80000002u + i));
            std::memcpy(&regs[i * 4], r, sizeof r);
#else
            ok = __get_cpuid(0x80000002u + i, &regs[i * 4], &regs[i * 4 + 1], &regs[i * 4 + 2], &regs[i * 4 + 3]) != 0;
#endif
        }
        if (ok && info.cpu.empty()) {
            char brand[49] = {};
            std::memcpy(brand, regs, 48);
            info.cpu = trim(brand);
        }
    }
#if defined(_MSC_VER)
    {
        int r[4];
        __cpuid(r, 1);
        const bool sse42 = (r[2] >> 20) & 1, osxsave = (r[2] >> 27) & 1, avx = (r[2] >> 28) & 1;
        const bool ymm = osxsave && avx && ((_xgetbv(0) & 6) == 6);
        __cpuidex(r, 7, 0);
        if (sse42) info.simd.push_back("sse4.2");
        if (ymm && ((r[1] >> 5) & 1)) info.simd.push_back("avx2");
        if (ymm && ((r[1] >> 16) & 1) && ((_xgetbv(0) & 0xE6) == 0xE6)) info.simd.push_back("avx512f");
    }
#else
    __builtin_cpu_init();
    if (__builtin_cpu_supports("sse4.2")) info.simd.push_back("sse4.2");
    if (__builtin_cpu_supports("avx2")) info.simd.push_back("avx2");
    if (__builtin_cpu_supports("avx512f")) info.simd.push_back("avx512f");
#endif
#elif defined(__aarch64__) || defined(_M_ARM64)
    info.simd.push_back("neon"); // every ARM64 CPU has Advanced SIMD
#if defined(__linux__)
    if (getauxval(AT_HWCAP) & (1UL << 22)) info.simd.push_back("sve"); // HWCAP_SVE
#endif
#endif
#if defined(__linux__)
    if (info.cpu.empty()) {
        // ARM boards: the device tree names the machine ("Apple MacBook Pro (16-inch, M1 Pro, 2021)").
        std::string model = readFile("/proc/device-tree/model");
        model.erase(std::remove(model.begin(), model.end(), '\0'), model.end());
        info.cpu = trim(model);
    }
    if (info.cpu.empty()) {
        std::istringstream cpuinfo(readFile("/proc/cpuinfo"));
        for (std::string line; std::getline(cpuinfo, line);)
            if (line.rfind("model name", 0) == 0 || line.rfind("Model", 0) == 0) {
                if (const size_t colon = line.find(':'); colon != std::string::npos) info.cpu = trim(line.substr(colon + 1));
                break;
            }
    }
#endif
#if defined(__AVX512F__)
    info.simdBuild = "avx512f";
#elif defined(__AVX2__)
    info.simdBuild = "avx2";
#elif defined(__SSE4_2__)
    info.simdBuild = "sse4.2";
#elif defined(SF_X86)
    info.simdBuild = "sse2";
#elif defined(__ARM_NEON) || defined(_M_ARM64)
    info.simdBuild = "neon";
#else
    info.simdBuild = "scalar";
#endif
}

// ---------------------------------------------------------------------------
// Mapping
// ---------------------------------------------------------------------------

namespace {

// GPU memory the scene may use, in MB (0 = unknown).
uint64_t usableGpuMemoryMB(const HardwareInfo& hw) {
    switch (hw.gpuKind) {
    case GpuKind::Discrete: return hw.vramMB;
    case GpuKind::Unified:
        // Apple GPUs may use about two thirds of the memory (Metal's recommended working set). Drivers report
        // anything from that to all of the RAM (Asahi's GL_NVX_gpu_memory_info): never more than two thirds.
        if (hw.ramMB == 0) return hw.vramMB;
        return hw.vramMB ? std::min(hw.vramMB, hw.ramMB * 2 / 3) : hw.ramMB * 2 / 3;
    case GpuKind::Integrated: return std::max(hw.vramMB, hw.ramMB / 2); // the carve-out plus shared system memory
    case GpuKind::Software: return hw.ramMB / 4;
    case GpuKind::Unknown: break;
    }
    return hw.vramMB;
}

struct TierLimits {
    uint64_t budgetMin, budgetMax, budgetDefault; // render triangle budget
    uint64_t vramDefaultMB, uploadDefaultMB;
    int textureCap;
    uint64_t subdivCap, microCap;
};

TierLimits limits(ProfileTier t) {
    switch (t) {
    case ProfileTier::Software: return {250'000, 4 * kM, 1 * kM, 1024, 16, 4096, 20 * kM, 16 * kM};
    case ProfileTier::Laptop: return {2 * kM, 40 * kM, 8 * kM, 2048, 64, 8192, 120 * kM, 256 * kM};
    case ProfileTier::Workstation: return {8 * kM, 600 * kM, 40 * kM, 12288, 256, 16384, 2000 * kM, 4096 * kM};
    }
    return limits(ProfileTier::Laptop);
}

// Workstation: a discrete GPU with 16 GB or more and 64 GB or more of RAM (what the OS reports is a little less
// than the label: 16 GB cards say about 15.9 GB, 64 GB machines about 62.5 GB), which also measures fast.
constexpr uint64_t kWorkstationVramMB = 15 * 1024, kWorkstationRamMB = 60 * 1024;
// Thresholds on measured triangle throughput (million small triangles per second, trivial shading).
constexpr double kSoftwareBelowM = 60.0, kWorkstationFromM = 1000.0;

// A re-calibration of the same GPU keeps its previous tier when the new measurement lands within this factor of
// the boundary between the two (measurements vary a little from run to run, more on a busy machine).
constexpr double kStickyBand = 1.15;

ProfileTier tierFromMeasurements(const HardwareInfo& hw, const Calibration& c, const ProfileTier* previous, std::string& why) {
    char buf[256];
    if (hw.gpuKind == GpuKind::Software) {
        why = "a software renderer (" + hw.glRenderer + "): safe mode";
        return ProfileTier::Software;
    }
    const double tri = c.trianglesPerSecondM;
    if (tri > 0.0) {
        std::snprintf(buf, sizeof buf, "%.0fM triangles/s", tri);
        why = buf;
    } else {
        why = "not calibrated";
    }
    // Too slow for anything but safe mode, whatever the GPU says it is.
    if (tri > 0.0 && tri < kSoftwareBelowM) {
        if (previous && *previous == ProfileTier::Laptop && tri >= kSoftwareBelowM / kStickyBand) {
            why += ", kept Laptop (close to the boundary)";
            return ProfileTier::Laptop;
        }
        why += ", safe mode";
        return ProfileTier::Software;
    }
    // Workstation needs the memory and the measured speed: a 16 GB+ discrete GPU, or a GPU sharing the system memory
    // (Apple Silicon Max / Ultra, big APUs), whose pool is two thirds of the RAM; and 64 GB+ RAM either way.
    // Everything else is Laptop / Budget.
    const bool sharedMemory = hw.gpuKind == GpuKind::Unified || hw.gpuKind == GpuKind::Integrated;
    const uint64_t gpu = sharedMemory ? (hw.ramMB ? hw.ramMB * 2 / 3 : hw.vramMB) : usableGpuMemoryMB(hw);
    std::string missing;
    if (hw.gpuKind != GpuKind::Discrete && !sharedMemory) missing = std::string("GPU type ") + gpuKindName(hw.gpuKind);
    else if (gpu < kWorkstationVramMB) missing = formatMegabytes(gpu) + " of video memory";
    if (missing.empty() && hw.ramMB && hw.ramMB < kWorkstationRamMB) missing = formatMegabytes(hw.ramMB) + " RAM";
    if (missing.empty() && c.dabMs > 3.0) missing = "slow brushes";
    if (missing.empty() && c.memoryGBps > 0.0 && c.memoryGBps < 4.0) missing = "slow memory";
    if (missing.empty() && tri > 0.0 && tri < kWorkstationFromM) {
        if (previous && *previous == ProfileTier::Workstation && tri >= kWorkstationFromM / kStickyBand) {
            why += ", kept Workstation (close to the boundary)";
            return ProfileTier::Workstation;
        }
        missing = "GPU throughput";
    }
    if (missing.empty() && previous && *previous == ProfileTier::Laptop && tri > 0.0 && tri < kWorkstationFromM * kStickyBand) {
        why += ", kept Laptop (close to the boundary)";
        return ProfileTier::Laptop;
    }
    if (missing.empty()) return ProfileTier::Workstation;
    why += ", Laptop / Budget: " + missing;
    return ProfileTier::Laptop;
}

} // namespace

PerformanceSettings settingsForTier(ProfileTier tier, const HardwareInfo& hw, const Calibration& c) {
    const TierLimits L = limits(tier);
    PerformanceSettings s;
    s.targetFps = 60;
    // Render budget: half a 60 Hz frame of the measured throughput (the real shading, antialiasing and the UI cost
    // the rest); the frame governor refines it from real frames.
    uint64_t budget = L.budgetDefault;
    if (c.trianglesPerSecondM > 0.0) budget = uint64_t(c.trianglesPerSecondM * 1e6 / 60.0 * 0.5);
    s.renderTriangleBudget = std::clamp(budget, L.budgetMin, L.budgetMax);
    // While the view moves, scenes over the budget draw the motion proxy (a quarter of the triangles).
    s.motionProxyThreshold = std::max<uint64_t>(s.renderTriangleBudget, 262'144);
    // GPU memory budget: 80% of dedicated memory (the desktop and other programs keep the rest).
    const uint64_t gpu = usableGpuMemoryMB(hw);
    if (gpu) s.vramBudgetMB = hw.gpuKind == GpuKind::Discrete || hw.gpuKind == GpuKind::Unknown ? gpu * 4 / 5 : gpu;
    else s.vramBudgetMB = L.vramDefaultMB;
    // Uploads: a quarter of a 60 Hz frame at the measured bandwidth.
    if (c.uploadGBps > 0.0)
        s.uploadBudgetMBPerFrame = std::clamp<uint64_t>(uint64_t(c.uploadGBps * 1e9 / 60.0 * 0.25) >> 20, 4, 1024);
    else s.uploadBudgetMBPerFrame = L.uploadDefaultMB;
    s.maxTextureSize = L.textureCap;
    if (hw.maxTextureSize > 0) s.maxTextureSize = std::min(s.maxTextureSize, hw.maxTextureSize);
    if (s.vramBudgetMB < 2048) s.maxTextureSize = std::min(s.maxTextureSize, 8192);
    // Subdivision: about 100 bytes of system memory per triangle with its levels, BVH and undo (docs/performance.md),
    // half the RAM; and about 16 bytes of GPU memory per drawn triangle.
    const uint64_t byRam = hw.ramMB ? (hw.ramMB << 20) / 2 / 100 : L.subdivCap;
    const uint64_t byGpu = (s.vramBudgetMB << 20) / 16;
    s.maxSubdivisionTriangles = std::clamp<uint64_t>(roundDownTo(std::min({byRam, byGpu, L.subdivCap}), kM), 2 * kM, L.subdivCap);
    // Micro-displacement: about 8 bytes of GPU memory per micro-triangle, a quarter of the budget.
    s.microTriangleLimit = std::clamp<uint64_t>(roundDownTo((s.vramBudgetMB << 20) / 4 / 8, kM), 1 * kM, L.microCap);
    // Threads: every hardware thread (the pool's limit is 16); a software renderer draws on the same cores.
    const int threads = std::max(1, hw.cpuThreads ? hw.cpuThreads : int(std::thread::hardware_concurrency()));
    s.threads = std::clamp(tier == ProfileTier::Software ? threads / 2 : threads, 1, 16);
    return s;
}

PerformanceSettings applyOverrides(const PerformanceSettings& a, const ProfileOverrides& o) {
    PerformanceSettings s = a;
    if (o.renderTriangleBudget) s.renderTriangleBudget = o.renderTriangleBudget;
    if (o.motionProxyThreshold) s.motionProxyThreshold = o.motionProxyThreshold;
    else if (o.renderTriangleBudget) s.motionProxyThreshold = std::max<uint64_t>(o.renderTriangleBudget, 262'144);
    if (o.vramBudgetMB) s.vramBudgetMB = o.vramBudgetMB;
    if (o.uploadBudgetMBPerFrame) s.uploadBudgetMBPerFrame = o.uploadBudgetMBPerFrame;
    if (o.maxTextureSize) s.maxTextureSize = o.maxTextureSize;
    if (o.maxSubdivisionTriangles) s.maxSubdivisionTriangles = o.maxSubdivisionTriangles;
    if (o.microTriangleLimit) s.microTriangleLimit = o.microTriangleLimit;
    if (o.threads) s.threads = std::clamp(o.threads, 1, 64);
    if (o.targetFps) s.targetFps = std::clamp(o.targetFps, 15, 360);
    return s;
}

bool ProfileOverrides::any() const {
    return !tier.empty() || renderTriangleBudget || motionProxyThreshold || vramBudgetMB || uploadBudgetMBPerFrame ||
           maxTextureSize || maxSubdivisionTriangles || microTriangleLimit || threads || targetFps || !governor ||
           draftBrush;
}

HardwareProfile mapProfile(const HardwareInfo& hardware, const Calibration& calibration, const ProfileOverrides& overrides,
                           const ProfileTier* previousTier) {
    HardwareProfile p;
    p.hardware = hardware;
    p.calibration = calibration;
    p.overrides = overrides;
    std::string why;
    p.tier = tierFromMeasurements(hardware, calibration, previousTier, why);
    p.measuredTier = p.tier;
    ProfileTier forced;
    if (!overrides.tier.empty() && tierFromName(overrides.tier, forced) && forced != p.tier) {
        why += std::string("; set to ") + tierName(forced) + " by hand";
        p.tier = forced;
    }
    p.automatic = settingsForTier(p.tier, hardware, calibration);
    p.settings = applyOverrides(p.automatic, overrides);
    const uint64_t gpu = usableGpuMemoryMB(hardware);
    std::string gpuName = hardware.glRenderer.empty() ? std::string("no GPU probed") : hardware.glRenderer;
    // "Detected Apple M1 Pro (G13S C0), 21.3 GB unified -> Workstation (2100M triangles/s)"
    p.reason = "Detected " + gpuName + (gpu ? ", " + formatMegabytes(gpu) + " " + gpuKindName(hardware.gpuKind) : std::string()) +
               " -> " + tierName(p.tier) + " (" + why + ")";
    return p;
}

// ---------------------------------------------------------------------------
// Cache and JSON
// ---------------------------------------------------------------------------

namespace {

Json num(uint64_t v) { return Json(double(v)); }
uint64_t u64(const Json& j, uint64_t fallback = 0) {
    const double d = j.asNumber(double(fallback));
    return d > 0 ? uint64_t(d) : 0;
}

Json hardwareJson(const HardwareInfo& h) {
    Json j = Json::object();
    j.set("gl_vendor", h.glVendor);
    j.set("gl_renderer", h.glRenderer);
    j.set("gl_version", h.glVersion);
    j.set("gpu_kind", gpuKindName(h.gpuKind));
    j.set("vram_mb", num(h.vramMB));
    j.set("vram_available_mb", num(h.vramAvailableMB));
    j.set("vram_source", h.vramSource.empty() ? std::string("none") : h.vramSource);
    j.set("max_texture_size", h.maxTextureSize);
    j.set("timer_query", h.timerQuery);
    j.set("os", h.os);
    j.set("cpu", h.cpu);
    j.set("cpu_threads", h.cpuThreads);
    j.set("ram_mb", num(h.ramMB));
    Json simd = Json::array();
    for (const auto& s : h.simd) simd.push(s);
    j.set("simd", simd);
    j.set("simd_build", h.simdBuild);
    j.set("app_version", h.appVersion);
    return j;
}

void hardwareFromJson(const Json& j, HardwareInfo& h) {
    h.glVendor = j["gl_vendor"].asString();
    h.glRenderer = j["gl_renderer"].asString();
    h.glVersion = j["gl_version"].asString();
    h.gpuKind = gpuKindFromName(j["gpu_kind"].asString());
    h.vramMB = u64(j["vram_mb"]);
    h.vramAvailableMB = u64(j["vram_available_mb"]);
    h.vramSource = j["vram_source"].asString();
    h.maxTextureSize = int(j["max_texture_size"].asNumber());
    h.timerQuery = j["timer_query"].asBool();
    h.os = j["os"].asString();
    h.cpu = j["cpu"].asString();
    h.cpuThreads = int(j["cpu_threads"].asNumber());
    h.ramMB = u64(j["ram_mb"]);
    h.simd.clear();
    for (const Json& s : j["simd"].items()) h.simd.push_back(s.asString());
    h.simdBuild = j["simd_build"].asString();
    h.appVersion = j["app_version"].asString();
}

Json calibrationToJson(const Calibration& c) {
    Json j = Json::object();
    j.set("triangles_per_second_millions", c.trianglesPerSecondM);
    j.set("upload_gb_per_second", c.uploadGBps);
    j.set("dab_ms", c.dabMs);
    j.set("dab_vertices", c.dabVertices);
    j.set("memory_gb_per_second", c.memoryGBps);
    j.set("gpu_timer", c.gpuTimer);
    j.set("runs", c.runs);
    j.set("duration_ms", c.durationMs);
    j.set("skipped", c.skipped);
    j.set("cpu_only", c.cpuOnly);
    return j;
}

void calibrationFromJson(const Json& j, Calibration& c) {
    c.trianglesPerSecondM = j["triangles_per_second_millions"].asNumber();
    c.uploadGBps = j["upload_gb_per_second"].asNumber();
    c.dabMs = j["dab_ms"].asNumber();
    c.dabVertices = j["dab_vertices"].asNumber();
    c.memoryGBps = j["memory_gb_per_second"].asNumber();
    c.gpuTimer = j["gpu_timer"].asBool();
    c.runs = int(j["runs"].asNumber());
    c.durationMs = j["duration_ms"].asNumber();
    c.skipped = j["skipped"].asBool();
    c.cpuOnly = j["cpu_only"].asBool();
}

Json settingsJson(const PerformanceSettings& s) {
    Json j = Json::object();
    j.set("render_triangle_budget", num(s.renderTriangleBudget));
    j.set("motion_proxy_threshold", num(s.motionProxyThreshold));
    j.set("vram_budget_mb", num(s.vramBudgetMB));
    j.set("upload_budget_mb_per_frame", num(s.uploadBudgetMBPerFrame));
    j.set("max_texture_size", s.maxTextureSize);
    j.set("max_subdivision_triangles", num(s.maxSubdivisionTriangles));
    j.set("micro_triangle_limit", num(s.microTriangleLimit));
    j.set("threads", s.threads);
    j.set("target_fps", s.targetFps);
    return j;
}

Json overridesJson(const ProfileOverrides& o) {
    Json j = Json::object();
    if (!o.tier.empty()) j.set("tier", o.tier);
    if (o.renderTriangleBudget) j.set("render_triangle_budget", num(o.renderTriangleBudget));
    if (o.motionProxyThreshold) j.set("motion_proxy_threshold", num(o.motionProxyThreshold));
    if (o.vramBudgetMB) j.set("vram_budget_mb", num(o.vramBudgetMB));
    if (o.uploadBudgetMBPerFrame) j.set("upload_budget_mb_per_frame", num(o.uploadBudgetMBPerFrame));
    if (o.maxTextureSize) j.set("max_texture_size", o.maxTextureSize);
    if (o.maxSubdivisionTriangles) j.set("max_subdivision_triangles", num(o.maxSubdivisionTriangles));
    if (o.microTriangleLimit) j.set("micro_triangle_limit", num(o.microTriangleLimit));
    if (o.threads) j.set("threads", o.threads);
    if (o.targetFps) j.set("target_fps", o.targetFps);
    if (!o.governor) j.set("governor", false);
    if (o.draftBrush) j.set("draft_brush", o.draftBrush == 1 ? "on" : "off");
    return j;
}

void overridesFromJson(const Json& j, ProfileOverrides& o) {
    o = {};
    o.tier = j["tier"].asString();
    o.renderTriangleBudget = u64(j["render_triangle_budget"]);
    o.motionProxyThreshold = u64(j["motion_proxy_threshold"]);
    o.vramBudgetMB = u64(j["vram_budget_mb"]);
    o.uploadBudgetMBPerFrame = u64(j["upload_budget_mb_per_frame"]);
    o.maxTextureSize = int(j["max_texture_size"].asNumber());
    o.maxSubdivisionTriangles = u64(j["max_subdivision_triangles"]);
    o.microTriangleLimit = u64(j["micro_triangle_limit"]);
    o.threads = int(j["threads"].asNumber());
    o.targetFps = int(j["target_fps"].asNumber());
    o.governor = j["governor"].asBool(true);
    o.draftBrush = j["draft_brush"].asString() == "on" ? 1 : j["draft_brush"].asString() == "off" ? 2 : 0;
}

} // namespace

std::string hardwareProfilePath() {
    const std::string dir = appDataDir();
    if (dir.empty()) return {};
#if defined(_WIN32)
    return dir + "\\hardware_profile.json";
#else
    return dir + "/hardware_profile.json";
#endif
}

Json hardwareProfileToJson(const HardwareProfile& p) {
    Json j = Json::object();
    j.set("cache_version", kHardwareProfileCacheVersion);
    j.set("hardware", hardwareJson(p.hardware));
    j.set("calibration", calibrationToJson(p.calibration));
    j.set("tier", tierName(p.tier));
    j.set("measured_tier", tierName(p.measuredTier));
    j.set("settings", settingsJson(p.settings));
    j.set("overrides", overridesJson(p.overrides));
    return j;
}

bool hardwareProfileFromJson(const Json& j, HardwareProfile& out) {
    if (!j.isObject() || int(j["cache_version"].asNumber()) != kHardwareProfileCacheVersion) return false;
    if (!j["hardware"].isObject() || !j["calibration"].isObject()) return false;
    HardwareInfo h;
    Calibration c;
    ProfileOverrides o;
    hardwareFromJson(j["hardware"], h);
    calibrationFromJson(j["calibration"], c);
    if (j["overrides"].isObject()) overridesFromJson(j["overrides"], o);
    // The tier and settings are mapped again (not read back), so they always follow this version's mapping; the
    // stored tier only settles a measurement close to a boundary, as when it was saved.
    ProfileTier stored;
    const bool hasStored = tierFromName(j["measured_tier"].asString(), stored);
    out = mapProfile(h, c, o, hasStored ? &stored : nullptr);
    return true;
}

bool loadHardwareProfile(const std::string& path, HardwareProfile& out) {
    if (path.empty()) return false;
    const std::string text = readFile(path);
    if (text.empty()) return false;
    Json j;
    std::string error;
    return Json::parse(text, j, error) && hardwareProfileFromJson(j, out);
}

bool saveHardwareProfile(const std::string& path, const HardwareProfile& profile, std::string& error) {
    if (path.empty()) {
        error = "no settings folder";
        return false;
    }
    const std::filesystem::path p(std::u8string(path.begin(), path.end()));
    std::error_code ec;
    if (p.has_parent_path()) std::filesystem::create_directories(p.parent_path(), ec);
    const std::filesystem::path tmp = p.string() + ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) {
            error = "cannot write " + path;
            return false;
        }
        f << hardwareProfileToJson(profile).dump(true) << "\n";
        if (!f) {
            error = "cannot write " + path;
            return false;
        }
    }
    std::filesystem::rename(tmp, p, ec);
    if (ec) {
        error = "cannot replace " + path + ": " + ec.message();
        return false;
    }
    return true;
}

std::string recalibrationReason(const HardwareProfile* cached, const HardwareInfo& cur) {
    if (!cached) return "first launch";
    const HardwareInfo& old = cached->hardware;
    if (old.glVendor != cur.glVendor || old.glRenderer != cur.glRenderer) return "GPU changed";
    if (old.glVersion != cur.glVersion) return "driver changed";
    if (old.appVersion != cur.appVersion) return "new version";
    if (cached->calibration.cpuOnly && !cur.glRenderer.empty()) return "GPU not measured yet";
    return {};
}

bool needsRecalibration(const HardwareProfile* cached, const HardwareInfo& current) {
    return !recalibrationReason(cached, current).empty();
}

Json calibrationJson(const HardwareProfile& p, const TierConfig* tier) {
    Json j = Json::object();
    j.set("schema", kCalibrationSchema);
    j.set("app_version", p.hardware.appVersion);
    j.set("hardware", hardwareJson(p.hardware));
    j.set("measurements", calibrationToJson(p.calibration));
    j.set("profile", tierName(p.tier));
    j.set("reason", p.reason);
    j.set("settings", settingsJson(p.settings));
    j.set("automatic", settingsJson(p.automatic));
    j.set("overrides", overridesJson(p.overrides));
    j.set("tier_config", tierConfigJson(tier ? *tier : tierConfigFor(p, PowerSource::Unknown, 0)));
    j.set("network", false); // calibration never uses the network (no telemetry)
    return j;
}

bool validateCalibrationJson(const Json& j, std::string& error) {
    auto need = [&](const Json& obj, const char* where, const char* key, Json::Type type) {
        if (!error.empty()) return false;
        const Json& v = obj[key];
        if (v.type() != type) {
            error = std::string(where) + "." + key + " is missing or has the wrong type";
            return false;
        }
        return true;
    };
    error.clear();
    if (!j.isObject()) {
        error = "not a JSON object";
        return false;
    }
    using T = Json::Type;
    need(j, "$", "schema", T::String);
    if (error.empty() && j["schema"].asString() != kCalibrationSchema) error = "$.schema is not " + std::string(kCalibrationSchema);
    need(j, "$", "app_version", T::String);
    need(j, "$", "hardware", T::Object);
    need(j, "$", "measurements", T::Object);
    need(j, "$", "profile", T::String);
    need(j, "$", "reason", T::String);
    need(j, "$", "settings", T::Object);
    need(j, "$", "automatic", T::Object);
    need(j, "$", "overrides", T::Object);
    need(j, "$", "network", T::Bool);
    need(j, "$", "tier_config", T::Object);
    if (!error.empty()) return false;
    ProfileTier tier;
    if (!tierFromName(j["profile"].asString(), tier)) {
        error = "$.profile is not a tier name";
        return false;
    }
    if (j["network"].asBool()) {
        error = "$.network must be false";
        return false;
    }
    const Json& h = j["hardware"];
    for (const char* k : {"gl_vendor", "gl_renderer", "gl_version", "gpu_kind", "vram_source", "os", "cpu", "simd_build", "app_version"})
        need(h, "hardware", k, T::String);
    for (const char* k : {"vram_mb", "vram_available_mb", "max_texture_size", "cpu_threads", "ram_mb"}) need(h, "hardware", k, T::Number);
    need(h, "hardware", "timer_query", T::Bool);
    need(h, "hardware", "simd", T::Array);
    const Json& m = j["measurements"];
    for (const char* k : {"triangles_per_second_millions", "upload_gb_per_second", "dab_ms", "dab_vertices",
                          "memory_gb_per_second", "runs", "duration_ms"})
        need(m, "measurements", k, T::Number);
    for (const char* k : {"gpu_timer", "skipped", "cpu_only"}) need(m, "measurements", k, T::Bool);
    for (const char* which : {"settings", "automatic"}) {
        const Json& s = j[which];
        for (const char* k : {"render_triangle_budget", "motion_proxy_threshold", "vram_budget_mb", "upload_budget_mb_per_frame",
                              "max_texture_size", "max_subdivision_triangles", "micro_triangle_limit", "threads", "target_fps"})
            need(s, which, k, T::Number);
        if (error.empty() && s["render_triangle_budget"].asNumber() <= 0) error = std::string(which) + ".render_triangle_budget must be > 0";
        if (error.empty() && s["threads"].asNumber() < 1) error = std::string(which) + ".threads must be >= 1";
    }
    const Json& tc = j["tier_config"];
    need(tc, "tier_config", "tier", T::String);
    need(tc, "tier_config", "power", T::String);
    for (const char* k : {"resident_cache_cap_mb", "active_triangle_budget", "upload_budget_mb_per_frame", "target_hz"})
        need(tc, "tier_config", k, T::Number);
    for (const char* k : {"draft_brush_mode", "zero_copy_uma", "on_battery"}) need(tc, "tier_config", k, T::Bool);
    return error.empty();
}

// ---------------------------------------------------------------------------
// CPU benchmarks
// ---------------------------------------------------------------------------

double measureMemoryBandwidthGBps(int runs) {
    constexpr size_t kBytes = size_t(64) << 20;
    std::vector<uint8_t> a(kBytes, 1), b(kBytes, 0); // touched: the pages exist before timing
    std::vector<double> rates;
    for (int i = 0; i < std::max(1, runs) + 1; ++i) {
        a[size_t(i) * 4099 % kBytes] = uint8_t(i); // a change the copy can't skip
        const auto t = Clock::now();
        std::memcpy(b.data(), a.data(), kBytes);
        const double ms = msSince(t);
        if (i > 0 && ms > 0.0) rates.push_back(double(kBytes) / (ms * 1e-3) / 1e9); // the first run warms up
    }
    volatile uint8_t sink = b[kBytes / 2];
    (void)sink;
    return median(rates);
}

double measureDabMs(size_t triangles, int runs, int dabsPerRun, double* verticesPerDab) {
    Document doc;
    doc.setMesh(makeBenchSphere(triangles), "Calibration");
    Mesh& m = doc.mesh;
    Sculptor& sc = doc.sculptor;
    const float radius = 0.12f, step = 0.15f * radius;
    std::vector<double> perDab;
    double verts = 0.0;
    for (int run = 0; run < std::max(1, runs) + 1; ++run) { // run 0 warms the caches and the thread pool
        sc.beginStroke(m, "Calibration");
        sc.timings = {};
        Symmetry sym;
        glm::vec3 last(0.0f);
        int done = 0;
        double ms = 0.0;
        for (int i = 0; i < dabsPerRun; ++i) {
            const float x = 0.15f + std::fmod(float(i) * step, 0.6f), y = 0.2f + 0.1f * std::sin(float(i) * 0.3f);
            RayHit hit;
            if (!doc.bvh.raycast(m, glm::vec3(x, y, 5.0f), glm::vec3(0, 0, -1), hit)) continue;
            DabParams p;
            p.center = hit.position;
            p.viewDir = glm::vec3(0, 0, -1);
            p.radius = radius;
            p.strength = 0.3f;
            p.strokeDelta = i ? hit.position - last : glm::vec3(0.0f);
            last = hit.position;
            const auto t = Clock::now();
            sc.dab(m, doc.bvh, BrushType::Standard, p, sym);
            ms += msSince(t);
            m.clearDirty();
            ++done;
        }
        if (run > 0 && done > 0) {
            perDab.push_back(ms / double(done));
            verts = double(sc.timings.vertices) / double(done);
        }
        doc.undo.push(sc.endStroke());
        doc.undo.undo(m, doc.bvh); // the next run sculpts the same surface
        m.clearDirty();
    }
    if (verticesPerDab) *verticesPerDab = verts;
    return median(perDab);
}

void calibrateCpu(Calibration& out, bool quick) {
    out.memoryGBps = measureMemoryBandwidthGBps(quick ? 3 : 5);
    out.dabMs = measureDabMs(quick ? 200'000 : 1'000'000, quick ? 3 : 5, quick ? 12 : 20, &out.dabVertices);
    out.runs = std::max(out.runs, quick ? 3 : 5);
}

// ---------------------------------------------------------------------------
// The profile in effect
// ---------------------------------------------------------------------------

namespace {

std::mutex g_mutex;
PerformanceSettings& currentSettingsLocked() {
    static PerformanceSettings s = settingsForTier(ProfileTier::Laptop, HardwareInfo{}, Calibration{});
    return s;
}
std::atomic<uint64_t> g_budget{0}, g_proxy{0};
GpuCalibrator g_gpuCalibrator;
std::atomic<uint64_t> g_revision{0};

} // namespace

PerformanceSettings currentPerformanceSettings() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return currentSettingsLocked();
}

void setCurrentPerformanceSettings(const PerformanceSettings& s) {
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        currentSettingsLocked() = s;
    }
    setGovernedBudgets(s.renderTriangleBudget, s.motionProxyThreshold);
    g_revision.fetch_add(1, std::memory_order_relaxed);
}

uint64_t performanceSettingsRevision() { return g_revision.load(std::memory_order_relaxed); }

uint64_t renderTriangleBudget() {
    const uint64_t b = g_budget.load(std::memory_order_relaxed);
    return b ? b : currentPerformanceSettings().renderTriangleBudget;
}

uint64_t motionProxyThreshold() {
    const uint64_t t = g_proxy.load(std::memory_order_relaxed);
    return t ? t : currentPerformanceSettings().motionProxyThreshold;
}

void setGovernedBudgets(uint64_t renderTriangles, uint64_t proxyThreshold) {
    g_budget.store(renderTriangles, std::memory_order_relaxed);
    g_proxy.store(proxyThreshold, std::memory_order_relaxed);
}

void setGpuCalibrator(GpuCalibrator calibrator) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_gpuCalibrator = std::move(calibrator);
}

// ---------------------------------------------------------------------------
// Commands
// ---------------------------------------------------------------------------

namespace {

// The profile the commands work on: the cache file (the app keeps it current).
bool loadCurrent(HardwareProfile& p) { return loadHardwareProfile(hardwareProfilePath(), p); }

HardwareProfile uncalibrated() {
    HardwareInfo h;
    probeSystem(h);
    Calibration c;
    c.cpuOnly = true;
    return mapProfile(h, c);
}

bool setOverride(ProfileOverrides& o, const std::string& name, const std::string& text, double value, std::string& error) {
    const bool clear = text == "auto" || (text.empty() && value <= 0.0);
    const uint64_t u = clear ? 0 : uint64_t(std::max(0.0, value));
    if (name == "tier") {
        ProfileTier t;
        if (clear) o.tier.clear();
        else if (tierFromName(text, t)) o.tier = tierName(t);
        else {
            error = "tier must be Software, Laptop (or Budget), Workstation or auto";
            return false;
        }
    } else if (name == "render_triangle_budget") o.renderTriangleBudget = u;
    else if (name == "motion_proxy_threshold") o.motionProxyThreshold = u;
    else if (name == "vram_budget_mb") o.vramBudgetMB = u;
    else if (name == "upload_budget_mb_per_frame") o.uploadBudgetMBPerFrame = u;
    else if (name == "max_texture_size") o.maxTextureSize = int(u);
    else if (name == "max_subdivision_triangles") o.maxSubdivisionTriangles = u;
    else if (name == "micro_triangle_limit") o.microTriangleLimit = u;
    else if (name == "threads") o.threads = int(u);
    else if (name == "target_fps") o.targetFps = int(u);
    else if (name == "draft_brush") {
        if (clear) o.draftBrush = 0;
        else if (text == "on" || text == "true" || value == 1.0) o.draftBrush = 1;
        else if (text == "off" || text == "false" || (text.empty() && value == 2.0)) o.draftBrush = 2;
        else {
            error = "draft_brush must be on, off or auto";
            return false;
        }
    } else if (name == "governor") o.governor = clear ? true : value != 0.0 || text == "on" || text == "true";
    else {
        error = "unknown setting " + name;
        return false;
    }
    return true;
}

} // namespace

void registerHardwareProfileCommands(CommandRegistry& registry) {
    registry.add({"performance_profile",
                  "The hardware profile as JSON (schema sculptforge.calibration/1): the GPU, its memory, RAM, CPU threads "
                  "and SIMD, what calibration measured, the tier (Software safe mode, Laptop / Budget, Workstation) and its "
                  "settings (render triangle budget, motion proxy threshold, VRAM budget, ...). Nothing is sent anywhere.",
                  {},
                  [](Document&, const Args&) -> CommandResult {
                      HardwareProfile p;
                      if (!loadCurrent(p)) p = uncalibrated();
                      return {true, calibrationJson(p).dump(true)};
                  }});
    registry.add({"calibrate",
                  "Measure this computer again (about 2-3 seconds) and pick the performance profile: triangle throughput "
                  "and upload bandwidth on the GPU (in the app; sculptforge --calibrate from a shell), brush dab cost and "
                  "memory bandwidth on the CPU. Prints the result as JSON and saves it. Manual overrides are kept.",
                  {{"quick", ParamSpec::Type::Bool, "Fewer runs (about 1 second, less precise).", false}},
                  [](Document&, const Args& a) -> CommandResult {
                      const bool quick = argBool(a, "quick", false);
                      GpuCalibrator gpu;
                      {
                          std::lock_guard<std::mutex> lock(g_mutex);
                          gpu = g_gpuCalibrator;
                      }
                      HardwareProfile cached;
                      const bool hadCache = loadCurrent(cached);
                      HardwareProfile p;
                      std::string error;
                      if (gpu) {
                          if (!gpu(p, error)) return {false, error};
                      } else {
                          // No OpenGL here (sculptforge-cli): measure the CPU; keep the cached GPU strings and numbers.
                          const auto t = Clock::now();
                          HardwareInfo h;
                          probeSystem(h);
                          Calibration c;
                          if (hadCache) {
                              h.glVendor = cached.hardware.glVendor;
                              h.glRenderer = cached.hardware.glRenderer;
                              h.glVersion = cached.hardware.glVersion;
                              h.gpuKind = cached.hardware.gpuKind;
                              h.vramMB = cached.hardware.vramMB;
                              h.vramSource = cached.hardware.vramSource;
                              h.maxTextureSize = cached.hardware.maxTextureSize;
                              h.timerQuery = cached.hardware.timerQuery;
                              c.trianglesPerSecondM = cached.calibration.trianglesPerSecondM;
                              c.uploadGBps = cached.calibration.uploadGBps;
                              c.gpuTimer = cached.calibration.gpuTimer;
                          }
                          c.cpuOnly = c.trianglesPerSecondM <= 0.0;
                          calibrateCpu(c, quick);
                          c.durationMs = msSince(t);
                          const bool sameGpu = hadCache && !h.glRenderer.empty();
                          p = mapProfile(h, c, hadCache ? cached.overrides : ProfileOverrides{}, sameGpu ? &cached.measuredTier : nullptr);
                      }
                      if (hadCache && !gpu) p.overrides = cached.overrides;
                      saveHardwareProfile(hardwareProfilePath(), p, error); // best effort: the JSON is the answer
                      return {true, calibrationJson(p).dump(true)};
                  }});
    registry.add({"set_performance_override",
                  "Override one setting of the performance profile by hand (kept across re-calibrations), or give it "
                  "back to the profile with value=auto. Settings: tier (Software, Laptop, Workstation), "
                  "render_triangle_budget, motion_proxy_threshold, vram_budget_mb, upload_budget_mb_per_frame, "
                  "max_texture_size, max_subdivision_triangles, micro_triangle_limit, threads (next start), target_fps, "
                  "draft_brush (on / off: normals and cavity at pen release), governor (on / off).",
                  {{"name", ParamSpec::Type::String, "The setting.", true},
                   {"value", ParamSpec::Type::String, "A number, a tier name, on / off, or auto.", true}},
                  [](Document&, const Args& a) -> CommandResult {
                      HardwareProfile p;
                      if (!loadCurrent(p)) p = uncalibrated();
                      const std::string name = argString(a, "name");
                      std::string text = argString(a, "value");
                      double value = argNumber(a, "value", 0.0);
                      if (!text.empty() && text != "auto") {
                          char* end = nullptr;
                          const double d = std::strtod(text.c_str(), &end);
                          if (end && *end == '\0') {
                              value = d;
                              text.clear();
                          }
                      }
                      std::string error;
                      ProfileOverrides o = p.overrides;
                      if (!setOverride(o, name, text, value, error)) return {false, error};
                      p = mapProfile(p.hardware, p.calibration, o);
                      if (!saveHardwareProfile(hardwareProfilePath(), p, error)) return {false, error};
                      setCurrentPerformanceSettings(p.settings);
                      return {true, name + " = " + (text.empty() && value > 0 ? std::to_string(uint64_t(value)) : text.empty() ? "auto" : text) +
                                        "; " + p.reason};
                  }});
}

} // namespace sf
