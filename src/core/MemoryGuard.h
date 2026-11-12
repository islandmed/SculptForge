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

// Room kept on top of a need: max(512 MB, a tenth of it).
size_t memoryMargin(size_t needBytes);

struct MemoryCheck {
    size_t needBytes = 0;      // what the work takes
    size_t availableBytes = 0; // free now (0: unknown)
    bool enough = true;        // need + margin fits (or free memory is unknown)
};
MemoryCheck checkMemory(size_t needBytes, size_t availableBytes);
inline MemoryCheck checkMemory(size_t needBytes) { return checkMemory(needBytes, availableMemoryBytes()); }

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

// About what a command would take for the mesh it builds (bytes), from `mesh` and its arguments; 0 when it isn't one
// of the big ones (subdivide, subdiv_divide, micro_divide, remesh) or can't make the mesh at all.
size_t commandMemoryNeed(const Mesh& mesh, const std::string& name, const Args& args);

} // namespace sf
