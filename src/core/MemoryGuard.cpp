// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/MemoryGuard.h"

#include "core/Mesh.h"
#include "core/Micro.h"
#include "core/Multires.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#elif defined(__APPLE__)
#include <mach/mach.h>
#else
#include <fstream>
#endif

namespace sf {

namespace {

std::atomic<size_t> g_override{0};

// A mesh in the app, per triangle: its arrays, adjacency, BVH, GPU buffers and the undo step's copy (measured: the
// 33M benchmark head peaks at 4.7 GB, 141 bytes a triangle).
constexpr double kMeshBytesPerTriangle = 150.0;
// A micro level, per micro triangle: about 0.6 grid points x (2 bytes + 1 of mask + 6 on the GPU) + undo.
constexpr double kMicroBytesPerTriangle = 6.0;
// Voxel remesh, per grid cell: the distance field (float), the shell, wall, outside and solid masks, the smoothed
// field (float).
constexpr double kVoxelBytesPerCell = 11.0;

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

void setAvailableMemoryOverride(size_t b) { g_override.store(b, std::memory_order_relaxed); }

size_t memoryMargin(size_t need) { return std::max<size_t>(size_t(512) << 20, need / 10); }

MemoryCheck checkMemory(size_t need, size_t available) {
    MemoryCheck c;
    c.needBytes = need;
    c.availableBytes = available;
    c.enough = available == 0 || need == 0 || need + memoryMargin(need) <= available;
    return c;
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
    if (name == "remesh") { // the grid across the longest side, and about 6 R^2 triangles out
        const double r = std::clamp(argNumber(args, "resolution", 200), 32.0, 512.0);
        return bytes(r * r * r * kVoxelBytesPerCell + 6.0 * r * r * kMeshBytesPerTriangle);
    }
    return 0;
}

} // namespace sf
