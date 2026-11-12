// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/MemoryGuard.h"

#include "core/Mesh.h"
#include "core/Micro.h"
#include "core/Multires.h"
#include "core/Voxel.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <chrono>
#include <mutex>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#elif defined(__APPLE__)
#include <mach/mach.h>
#include <sys/sysctl.h>
#else
#include <fstream>
#include <unistd.h>
#endif

namespace sf {

namespace {

std::atomic<size_t> g_override{0};
std::atomic<size_t> g_totalOverride{0};
std::mutex g_cacheLock;
std::chrono::steady_clock::time_point g_cacheAt;
size_t g_cacheValue = 0;
bool g_cacheValid = false;

// A mesh in the app, per triangle: its arrays, adjacency, BVH, GPU buffers and the undo step's copy (measured: the
// 33M benchmark head peaks at 4.7 GB, 141 bytes a triangle).
constexpr double kMeshBytesPerTriangle = 150.0;
// A micro level, per micro triangle: about 0.6 grid points x (2 bytes + 1 of mask + 6 on the GPU) + undo.
constexpr double kMicroBytesPerTriangle = 6.0;

size_t systemAvailable() {
#if defined(_WIN32)
    MEMORYSTATUSEX m{};
    m.dwLength = sizeof m;
    return GlobalMemoryStatusEx(&m) ? size_t(m.ullAvailPhys) : 0;
#elif defined(__APPLE__)
    vm_statistics64_data_t vm{};
    mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
    if (host_statistics64(mach_host_self(), HOST_VM_INFO64, reinterpret_cast<host_info64_t>(&vm), &count) != KERN_SUCCESS) return 0;
    return size_t(vm.free_count + vm.inactive_count + vm.purgeable_count) * size_t(vm_page_size);
#else
    std::ifstream f("/proc/meminfo");
    std::string key, unit;
    size_t kb = 0;
    while (f >> key >> kb >> unit)
        if (key == "MemAvailable:") return kb * 1024;
    return 0;
#endif
}

size_t bytes(double b) { return b <= 0.0 ? 0 : b >= 1.8e19 ? size_t(-1) : size_t(b); }

} // namespace

size_t availableMemoryBytes() {
    if (const size_t o = g_override.load(std::memory_order_relaxed)) return o;
    if (const char* v = std::getenv("SF_AVAILABLE_MEMORY_MB"); v && *v) // (UI tests: a computer with this much free)
        if (const double mb = std::atof(v); mb > 0.0) return size_t(mb * 1048576.0);
    return systemAvailable();
}

size_t availableMemoryBytesCached() {
    if (g_override.load(std::memory_order_relaxed) || std::getenv("SF_AVAILABLE_MEMORY_MB")) return availableMemoryBytes();
    const auto now = std::chrono::steady_clock::now();
    std::lock_guard<std::mutex> lock(g_cacheLock);
    if (!g_cacheValid || now - g_cacheAt > std::chrono::seconds(1)) {
        g_cacheValue = systemAvailable();
        g_cacheAt = now;
        g_cacheValid = true;
    }
    return g_cacheValue;
}

void setAvailableMemoryOverride(size_t b) { g_override.store(b, std::memory_order_relaxed); }

size_t totalMemoryBytes() {
    if (const size_t o = g_totalOverride.load(std::memory_order_relaxed)) return o;
    if (const char* v = std::getenv("SF_TOTAL_MEMORY_MB"); v && *v)
        if (const double mb = std::atof(v); mb > 0.0) return size_t(mb * 1048576.0);
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

void setTotalMemoryOverride(size_t b) { g_totalOverride.store(b, std::memory_order_relaxed); }

size_t undoBudgetFor(size_t total, size_t available, size_t held) {
    if (available == 0) return kUndoBudgetUnknown;
    const size_t cap = total ? total / 4 : kUndoBudgetUnknown;
    const size_t margin = memoryMargin(0);
    // Below the margin the history gives back what is missing, down to the floor.
    const size_t room = available > margin ? held + (available - margin) : (held > margin - available ? held - (margin - available) : 0);
    return std::max(kUndoBudgetMin, std::min(cap, room));
}

size_t undoBudgetBytes(size_t held) { return undoBudgetFor(totalMemoryBytes(), availableMemoryBytesCached(), held); }

size_t memoryMargin(size_t need) { return std::max<size_t>(size_t(512) << 20, need / 10); }

MemoryCheck checkMemory(size_t need, size_t available) {
    MemoryCheck c;
    c.needBytes = need;
    c.availableBytes = available;
    c.enough = available == 0 || need == 0 || need + memoryMargin(need) <= available;
    return c;
}

std::string compactBytes(size_t b) {
    char buf[16];
    const double gb = double(b) / double(1ull << 30);
    if (gb < 0.995) std::snprintf(buf, sizeof buf, "%.0fM", double(b) / double(1u << 20));
    else std::snprintf(buf, sizeof buf, gb < 9.95 ? "%.1fG" : "%.0fG", gb);
    return buf;
}

std::string memoryText(size_t b) {
    std::string s = compactBytes(b);
    const char unit = s.back();
    s.pop_back();
    return s + (unit == 'G' ? " GB" : " MB");
}

std::string formatGigabytes(size_t b) {
    const double gb = double(b) / 1e9;
    char buf[32];
    if (gb >= 9.95) std::snprintf(buf, sizeof buf, "%.0f GB", gb);
    else {
        std::snprintf(buf, sizeof buf, "%.1f GB", gb);
        if (const std::string s = buf; s.size() > 5 && s.compare(s.size() - 5, 5, ".0 GB") == 0)
            return s.substr(0, s.size() - 5) + " GB";
    }
    return buf;
}

std::string benchmarkSkippedMessage(const std::string& label, const MemoryCheck& c) {
    return label + " skipped: needs ~" + formatGigabytes(c.needBytes) + " free, " + formatGigabytes(c.availableBytes) +
           " available. Close other apps and run again";
}

std::string commandShortMessage(const std::string& what, const MemoryCheck& c) {
    return what + " needs ~" + formatGigabytes(c.needBytes) + " free, " + formatGigabytes(c.availableBytes) + " available.";
}

size_t commandMemoryNeed(const Mesh& mesh, const std::string& name, const Args& args) {
    const double nt = double(mesh.triangleCount());
    if (nt <= 0.0) return 0;
    if (name == "subdivide") { // Loop: 4x a level
        const int levels = std::clamp(int(argNumber(args, "levels", 1)), 1, 4);
        return bytes(nt * std::pow(4.0, levels) * kMeshBytesPerTriangle);
    }
    if (name == "subdiv_divide") { // (the projection subdivDivide makes)
        const double quads = double(mesh.quadCount());
        const double projected = quads > 0.0 ? quads * 8.0 + (nt - 2.0 * quads) * 6.0 : nt * 4.0;
        return bytes(projected * double(kDivideBytesPerTriangle));
    }
    if (name == "micro_divide") { // (the level micro_divide picks)
        int level = int(argNumber(args, "level", 0));
        if (args.contains("triangles")) level = microLevelFor(mesh, size_t(std::max(1.0, argNumber(args, "triangles", 100)) * 1e6));
        if (level <= 0) level = microLevelFor(mesh, size_t(100e6));
        return bytes(nt * std::pow(4.0, level) * kMicroBytesPerTriangle);
    }
    if (name == "remesh") // the grid and the surface it makes (core/Voxel.h)
        return estimateVoxelRemesh(mesh, int(std::clamp(argNumber(args, "resolution", 200), double(kVoxelResolutionMin),
                                                        double(kVoxelResolutionMax))))
            .bytes;
    return 0;
}

} // namespace sf
