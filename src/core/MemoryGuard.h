// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Free memory checks before work that builds a big mesh: a benchmark scene, Subdivide, Divide, a micro level, a
// voxel remesh. The hardware profile budgets from the computer's total memory; these look at what the system can
// hand out now (other programs hold the rest), so a 33M scene isn't built with 1.3 GB free.
#pragma once

#include "core/Commands.h"

#include <cstddef>
#include <string>
#include <vector>

namespace sf {

class Mesh;

// Memory the system can still hand out now: Linux MemAvailable, macOS free + inactive + purgeable pages, Windows
// available physical memory. 0 = unknown. The override (or SF_AVAILABLE_MEMORY_MB) replaces it, for tests.
size_t availableMemoryBytes();
// availableMemoryBytes() as it was at most a second ago: for work that asks on every step (the undo budget).
size_t availableMemoryBytesCached();
// Tests: pretend this much memory is free; 0 goes back to asking the system.
void setAvailableMemoryOverride(size_t bytes);

// The computer's physical memory (0 = unknown). SF_TOTAL_MEMORY_MB or the override replaces it, for tests and for
// checking an 8 GB machine on a bigger one.
size_t totalMemoryBytes();
void setTotalMemoryOverride(size_t bytes);

// The undo history's byte budget (UndoStack::budget): clamp(a quarter of the total memory, 256 MB, what the history
// holds now + the free memory beyond the margin; below the margin, what it holds minus the shortfall). The history's
// own bytes count as room because they are what the free figure already lost: without them a growing history would
// shrink its own budget. Free memory unknown (0) is
// the fixed 1 GiB the history had before; a total that is unknown caps at 1 GiB too. The floor wins over the cap.
inline constexpr size_t kUndoBudgetUnknown = size_t(1) << 30;
inline constexpr size_t kUndoBudgetMin = size_t(256) << 20;
size_t undoBudgetFor(size_t totalBytes, size_t availableBytes, size_t heldBytes);
// ... with the system's figures (free memory read at most once a second).
size_t undoBudgetBytes(size_t heldBytes);

// How far the system can grow its swap, which decides what a remesh may try with the person's go-ahead.
//   Fixed: Linux. Swap is what it is (RAM + SwapTotal).
//   PageFileSet: Windows with page file sizes set by the user: the maximum sizes (a paging file grows from its
//     initial size up to its maximum, 52 -> 60 -> 72 -> 84 GB), each held to its volume's free space.
//   PageFileManaged: Windows "system managed" (maximum 0): Windows grows it as it needs, so the volume's free space,
//     capped at 3x RAM.
//   DiskDynamic: macOS creates swap files on demand: the free disk space of the startup volume less kMacDiskReserve.
// (The current commit limit / swap size is NOT used: it only reflects where the file has grown so far.)
enum class SwapKind { Fixed, PageFileSet, PageFileManaged, DiskDynamic };
constexpr size_t kMacDiskReserve = size_t(10) << 30;
constexpr size_t kManagedPageFileRamMultiple = 3;

struct SwapCeiling {
    size_t bytes = 0;
    SwapKind kind = SwapKind::Fixed;
};
SwapCeiling linuxSwapCeiling(size_t swapTotalBytes);
SwapCeiling macSwapCeiling(size_t freeDiskBytes);
// The registry's PagingFiles entries ("C:\pagefile.sys 16384 65536", sizes in MB; "0 0" or no sizes = system managed;
// "?:\pagefile.sys" = on the system drive, Windows' choice), then what the caller found out about each one.
struct PageFileSetting {
    std::string path;
    unsigned long long minMB = 0, maxMB = 0; // maxMB 0: system managed
    size_t volumeFreeBytes = 0;              // free space on its volume now
    size_t currentBytes = 0;                 // the file's size now (it already holds that much of the volume)
};
std::vector<PageFileSetting> parsePagingFiles(const std::vector<std::string>& entries);
SwapCeiling windowsSwapCeiling(const std::vector<PageFileSetting>& files, size_t ramBytes);

// What the computer has: free RAM now, total RAM and the most swap it can grow to (above). 0 = unknown. Tests inject
// their own (setMemoryLimitsOverride), UI tests with SF_AVAILABLE_MEMORY_MB, SF_TOTAL_MEMORY_MB, SF_SWAP_MAX_MB and
// SF_SWAP_KIND (0 / 0 / "auto" / "auto" = the system's; kinds: fixed, pagefile, managed, disk).
struct MemoryLimits {
    size_t available = 0, total = 0, swapMax = 0;
    SwapKind swapKind = SwapKind::Fixed;
};
MemoryLimits memoryLimits();
void setMemoryLimitsOverride(const MemoryLimits& l, bool on = true);

// Room kept on top of a need: max(512 MB, a tenth of it).
size_t memoryMargin(size_t needBytes);

struct MemoryCheck {
    size_t needBytes = 0;      // what the work takes
    size_t availableBytes = 0; // free now (0: unknown)
    bool enough = true;        // need + margin fits (or free memory is unknown)
};
MemoryCheck checkMemory(size_t needBytes, size_t availableBytes);
inline MemoryCheck checkMemory(size_t needBytes) { return checkMemory(needBytes, availableMemoryBytes()); }

// Where a big job lands. Fits: runs now, as it always did. NeedsSwap: more than is free but no more than 90% of
// RAM + the most swap the system can grow to, so it can run if the system pages (slow); the person decides. TooBig: past even that (or the
// total isn't known): refused. needBytes 0 and unknown free memory are Fits.
enum class MemoryTier { Fits, NeedsSwap, TooBig };
constexpr double kSwapCeilingFraction = 0.9;
size_t memoryCeiling(const MemoryLimits& l); // (total RAM + the most swap) * 0.9
// "RAM 16 GB + page file maximum 64 GB, 90%: 72 GB": which ceiling was used, for the dialog and the refusal.
std::string memoryCeilingText(const MemoryLimits& l);
MemoryTier classifyMemory(size_t needBytes, const MemoryLimits& l);
inline MemoryTier classifyMemory(size_t needBytes) { return classifyMemory(needBytes, memoryLimits()); }

// "1.2G", "14G", "640M": the top bar's compact memory figures, in binary units like the system monitors (a 12 GB
// card is 12G, not 13G). memoryText: the same spelled out for tooltips, "1.2 GB", "640 MB".
std::string compactBytes(size_t bytes);
std::string memoryText(size_t bytes);
// "5 GB", "2.3 GB", "0.4 GB" (decimal gigabytes, as the system's own tools show them).
std::string formatGigabytes(size_t bytes);
// "33M skipped: needs ~5 GB free, 1.3 GB available. Close other apps and run again"
std::string benchmarkSkippedMessage(const std::string& label, const MemoryCheck& c);
// "Subdivide needs ~6.2 GB free, 1.3 GB available."
std::string commandShortMessage(const std::string& what, const MemoryCheck& c);

// The highest voxel remesh Resolution below `resolution` that fits the free memory (with its margin) for this mesh;
// 0 when even the lowest doesn't. For "lower N: fits at M".
int voxelResolutionThatFits(const Mesh& mesh, int resolution, const MemoryLimits& l);
// "Voxel Remesh at 4096 (about 120M triangles) needs ~14 GB, 9 GB is available and the computer can't hold it even
// with swap (~11 GB). Lower the Resolution to 2816 (it fits at that) or close other apps"
std::string voxelRemeshTooBigMessage(const Mesh& mesh, int resolution, const MemoryLimits& l);
// "This needs about 6 GB; 2 GB is free. It may use swap and be slow."
std::string memorySwapMessage(size_t needBytes, size_t availableBytes);

// About what a command would take for the mesh it builds (bytes), from `mesh` and its arguments; 0 when it isn't one
// of the big ones (subdivide, subdiv_divide, micro_divide; the voxel remesh has its own tiers, core/Voxel.h) or can't make the mesh at all.
size_t commandMemoryNeed(const Mesh& mesh, const std::string& name, const Args& args);

} // namespace sf
