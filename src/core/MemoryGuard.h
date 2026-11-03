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

struct Mesh;

// Memory the system can still hand out now: Linux MemAvailable, macOS free + inactive + purgeable pages, Windows
// available physical memory. 0 = unknown. The override (or SF_AVAILABLE_MEMORY_MB) replaces it, for tests.
size_t availableMemoryBytes();
// Tests: pretend this much memory is free; 0 goes back to asking the system.
void setAvailableMemoryOverride(size_t bytes);

// Room kept on top of a need: max(512 MB, a tenth of it).
size_t memoryMargin(size_t needBytes);

struct MemoryCheck {
    size_t needBytes = 0;      // what the work takes
    size_t availableBytes = 0; // free now (0: unknown)
    bool enough = true;        // need + margin fits (or free memory is unknown)
};
MemoryCheck checkMemory(size_t needBytes, size_t availableBytes);
inline MemoryCheck checkMemory(size_t needBytes) { return checkMemory(needBytes, availableMemoryBytes()); }

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
