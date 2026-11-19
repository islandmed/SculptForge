// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/MemoryGuard.h"

#include "core/HardwareProfile.h"
#include "core/Mesh.h"
#include "core/Micro.h"
#include "core/Multires.h"
#include "core/Voxel.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <chrono>
#include <cstring>
#include <mutex>
#include <set>

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
#if defined(__APPLE__)
#include <sys/mount.h>
#include <sys/sysctl.h>
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

struct SystemTotals {
    size_t total = 0;
    SwapCeiling swap;
};

#if defined(_WIN32)
std::string narrow(const std::wstring& w) {
    if (w.empty()) return {};
    std::string out(size_t(WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), nullptr, 0, nullptr, nullptr)), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), out.data(), int(out.size()), nullptr, nullptr);
    return out;
}

size_t freeBytesOnVolume(wchar_t drive) {
    const wchar_t root[] = {drive, L':', L'\\', 0};
    ULARGE_INTEGER freeToUser{};
    return GetDiskFreeSpaceExW(root, &freeToUser, nullptr, nullptr) ? size_t(freeToUser.QuadPart) : 0;
}

// The page file settings: HKLM\SYSTEM\CurrentControlSet\Control\Session Manager\Memory Management, PagingFiles.
bool readPageFiles(std::vector<PageFileSetting>& out) {
    DWORD size = 0;
    const wchar_t* key = L"SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Memory Management";
    if (RegGetValueW(HKEY_LOCAL_MACHINE, key, L"PagingFiles", RRF_RT_REG_MULTI_SZ, nullptr, nullptr, &size) != ERROR_SUCCESS) return false;
    std::vector<wchar_t> buf(size / sizeof(wchar_t) + 2, L'\0');
    if (RegGetValueW(HKEY_LOCAL_MACHINE, key, L"PagingFiles", RRF_RT_REG_MULTI_SZ, nullptr, buf.data(), &size) != ERROR_SUCCESS) return false;
    std::vector<std::string> entries;
    for (const wchar_t* p = buf.data(); *p; p += wcslen(p) + 1) entries.push_back(narrow(p));
    out = parsePagingFiles(entries);
    wchar_t sys[MAX_PATH] = {};
    const wchar_t systemDrive = GetSystemDirectoryW(sys, MAX_PATH) > 0 && sys[1] == L':' ? sys[0] : L'C';
    for (PageFileSetting& f : out) {
        const bool any = f.path.size() >= 2 && f.path[0] == '?'; // "?:\pagefile.sys": Windows picks the volume
        const wchar_t drive = any ? systemDrive : wchar_t(f.path.size() >= 2 ? f.path[0] : 'C');
        f.volumeFreeBytes = freeBytesOnVolume(drive);
        const std::wstring path = std::wstring(1, drive) + L":\\pagefile.sys";
        WIN32_FILE_ATTRIBUTE_DATA info{};
        if (!any && f.path.size() > 3) { // (a custom file name)
            const int n = MultiByteToWideChar(CP_UTF8, 0, f.path.c_str(), int(f.path.size()), nullptr, 0);
            std::wstring w(size_t(n), L'\0');
            MultiByteToWideChar(CP_UTF8, 0, f.path.c_str(), int(f.path.size()), w.data(), n);
            if (GetFileAttributesExW(w.c_str(), GetFileExInfoStandard, &info))
                f.currentBytes = size_t((ULONGLONG(info.nFileSizeHigh) << 32) | info.nFileSizeLow);
        } else if (GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &info)) {
            f.currentBytes = size_t((ULONGLONG(info.nFileSizeHigh) << 32) | info.nFileSizeLow);
        }
    }
    return true;
}
#endif

SystemTotals systemTotals() {
    SystemTotals t;
#if defined(_WIN32)
    MEMORYSTATUSEX m{};
    m.dwLength = sizeof m;
    size_t granted = 0; // what the commit limit already holds above RAM: the page file as it has grown so far
    if (GlobalMemoryStatusEx(&m)) {
        t.total = size_t(m.ullTotalPhys);
        granted = m.ullTotalPageFile > m.ullTotalPhys ? size_t(m.ullTotalPageFile - m.ullTotalPhys) : 0;
    }
    std::vector<PageFileSetting> files;
    if (readPageFiles(files)) t.swap = windowsSwapCeiling(files, t.total);
    else t.swap.kind = SwapKind::PageFileSet; // (settings unreadable: at least what has been granted)
    t.swap.bytes = std::max(t.swap.bytes, granted);
#elif defined(__APPLE__)
    uint64_t mem = 0;
    size_t len = sizeof mem;
    if (sysctlbyname("hw.memsize", &mem, &len, nullptr, 0) == 0) t.total = size_t(mem);
    struct statfs fs{};
    t.swap = macSwapCeiling(statfs("/", &fs) == 0 ? size_t(fs.f_bavail) * size_t(fs.f_bsize) : 0);
#else
    std::ifstream f("/proc/meminfo");
    std::string key, unit;
    size_t kb = 0, swapTotal = 0;
    while (f >> key >> kb >> unit) {
        if (key == "MemTotal:") t.total = kb * 1024;
        else if (key == "SwapTotal:") swapTotal = kb * 1024;
    }
    t.swap = linuxSwapCeiling(swapTotal);
#endif
    return t;
}

// (The registry and the disk don't need asking every frame.)
SystemTotals cachedSystemTotals() {
    static std::mutex mutex;
    static SystemTotals cached;
    static std::chrono::steady_clock::time_point at;
    static bool have = false;
    const std::lock_guard<std::mutex> lock(mutex);
    const auto now = std::chrono::steady_clock::now();
    if (!have || now - at > std::chrono::seconds(5)) {
        cached = systemTotals();
        at = now;
        have = true;
    }
    return cached;
}

std::mutex g_limitsMutex;
MemoryLimits g_limitsOverride;
bool g_limitsOverrideOn = false;

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

MemoryLimits memoryLimits() {
    {
        const std::lock_guard<std::mutex> lock(g_limitsMutex);
        if (g_limitsOverrideOn) return g_limitsOverride;
    }
    MemoryLimits l;
    l.available = availableMemoryBytes();
    const SystemTotals t = cachedSystemTotals();
    l.total = totalMemoryBytes(); // (the override, SF_TOTAL_MEMORY_MB or the system's)
    l.swapMax = t.swap.bytes;
    l.swapKind = t.swap.kind;
    const char* swap = std::getenv("SF_SWAP_MAX_MB"); // (UI tests: a number, 0 included; unset or "auto": the system's)
    if (swap && *swap && std::string(swap) != "auto") l.swapMax = size_t(std::max(0.0, std::atof(swap)) * 1048576.0);
    if (const char* k = std::getenv("SF_SWAP_KIND"); k && *k) {
        const std::string kind = k;
        if (kind == "fixed") l.swapKind = SwapKind::Fixed;
        else if (kind == "pagefile") l.swapKind = SwapKind::PageFileSet;
        else if (kind == "managed") l.swapKind = SwapKind::PageFileManaged;
        else if (kind == "disk") l.swapKind = SwapKind::DiskDynamic;
    }
    return l;
}

void setMemoryLimitsOverride(const MemoryLimits& l, bool on) {
    const std::lock_guard<std::mutex> lock(g_limitsMutex);
    g_limitsOverride = l;
    g_limitsOverrideOn = on;
}

size_t memoryMargin(size_t need) { return std::max<size_t>(size_t(512) << 20, need / 10); }

MemoryCheck checkMemory(size_t need, size_t available) {
    MemoryCheck c;
    c.needBytes = need;
    c.availableBytes = available;
    c.enough = available == 0 || need == 0 || need + memoryMargin(need) <= available;
    return c;
}

SwapCeiling linuxSwapCeiling(size_t swapTotal) { return {swapTotal, SwapKind::Fixed}; }

SwapCeiling macSwapCeiling(size_t freeDisk) {
    return {freeDisk > kMacDiskReserve ? freeDisk - kMacDiskReserve : 0, SwapKind::DiskDynamic};
}

std::vector<PageFileSetting> parsePagingFiles(const std::vector<std::string>& entries) {
    std::vector<PageFileSetting> out;
    auto number = [](const std::string& t, unsigned long long& v) {
        if (t.empty() || t.find_first_not_of("0123456789") != std::string::npos) return false;
        v = std::strtoull(t.c_str(), nullptr, 10);
        return true;
    };
    auto lastToken = [](std::string& s) { // cuts the last whitespace-separated token off s
        while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.pop_back();
        const size_t at = s.find_last_of(" \t");
        const std::string t = at == std::string::npos ? s : s.substr(at + 1);
        s.erase(at == std::string::npos ? 0 : at);
        return t;
    };
    for (std::string line : entries) {
        PageFileSetting f;
        std::string rest = line;
        unsigned long long mx = 0, mn = 0;
        const std::string maxTok = lastToken(rest);
        std::string rest2 = rest;
        const std::string minTok = lastToken(rest2);
        if (number(maxTok, mx) && number(minTok, mn) && !rest2.empty()) {
            f.minMB = mn, f.maxMB = mx, f.path = rest2;
        } else { // just a path: system managed
            f.path = line;
            while (!f.path.empty() && (f.path.back() == ' ' || f.path.back() == '\t')) f.path.pop_back();
        }
        if (!f.path.empty()) out.push_back(f);
    }
    return out;
}

SwapCeiling windowsSwapCeiling(const std::vector<PageFileSetting>& files, size_t ram) {
    SwapCeiling c;
    c.kind = SwapKind::PageFileSet;
    size_t configured = 0, managedRoom = 0;
    bool managed = false;
    std::set<char> managedDrives;
    for (const PageFileSetting& f : files) {
        const size_t room = f.volumeFreeBytes + f.currentBytes; // (the file's own size is already out of the volume's free space)
        if (f.maxMB > 0) {
            configured += std::min(size_t(f.maxMB) << 20, room);
        } else {
            managed = true;
            const char drive = f.path.empty() ? '?' : char(std::toupper(static_cast<unsigned char>(f.path[0])));
            if (managedDrives.insert(drive).second) managedRoom += room;
        }
    }
    const size_t managedCap = std::min(managedRoom, ram * kManagedPageFileRamMultiple);
    c.bytes = configured + managedCap;
    if (managed && configured == 0) c.kind = SwapKind::PageFileManaged;
    return c;
}

size_t memoryCeiling(const MemoryLimits& l) {
    return l.total == 0 ? 0 : bytes((double(l.total) + double(l.swapMax)) * kSwapCeilingFraction);
}

std::string memoryCeilingText(const MemoryLimits& l) {
    std::string what;
    switch (l.swapKind) {
    case SwapKind::Fixed: what = "swap " + memoryText(l.swapMax); break;
    case SwapKind::PageFileSet: what = "page file maximum " + memoryText(l.swapMax); break;
    case SwapKind::PageFileManaged: what = "page file Windows may grow (free disk space, at most 3x RAM) " + memoryText(l.swapMax); break;
    case SwapKind::DiskDynamic: what = "free disk space less " + memoryText(kMacDiskReserve) + " " + memoryText(l.swapMax); break;
    }
    return "RAM " + memoryText(l.total) + " + " + what + ", 90%: " + memoryText(memoryCeiling(l));
}

MemoryTier classifyMemory(size_t need, const MemoryLimits& l) {
    if (checkMemory(need, l.available).enough) return MemoryTier::Fits;
    return need <= memoryCeiling(l) ? MemoryTier::NeedsSwap : MemoryTier::TooBig;
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

int voxelResolutionThatFits(const Mesh& mesh, int resolution, const MemoryLimits& l) {
    const double area = surfaceArea(mesh);
    auto fits = [&](int r) { return checkMemory(estimateVoxelRemesh(mesh, r, area).bytes, l.available).enough; };
    int lo = kVoxelResolutionMin, hi = std::min(resolution - 1, kVoxelResolutionMax);
    if (hi < lo || !fits(lo)) return 0;
    while (lo < hi) { // (the need only grows with the resolution)
        const int mid = lo + (hi - lo + 1) / 2;
        if (fits(mid)) lo = mid;
        else hi = mid - 1;
    }
    return lo;
}

std::string voxelRemeshTooBigMessage(const Mesh& mesh, int resolution, const MemoryLimits& l) {
    const VoxelRemeshEstimate e = estimateVoxelRemesh(mesh, resolution);
    std::string s = "Voxel Remesh at " + std::to_string(resolution) + " (about " + formatTriangles(e.triangles) +
                    " triangles) needs ~" + memoryText(e.bytes) + "; " + memoryText(l.available) + " is free";
    if (const size_t ceiling = memoryCeiling(l); ceiling > 0) s += " and this computer can't hold it even with swap (limit " + memoryCeilingText(l) + ")";
    if (const int fit = voxelResolutionThatFits(mesh, resolution, l); fit > 0)
        s += ". Lower the Resolution to " + std::to_string(fit) + " (that fits) or close other apps";
    else s += ". Lower the Resolution or close other apps";
    return s;
}

std::string memorySwapMessage(size_t need, size_t available) {
    return "This needs about " + memoryText(need) + "; " + memoryText(available) + " is free. It may use swap and be slow.";
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
    return 0;
}

} // namespace sf
