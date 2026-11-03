// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Proof mode (Help > Benchmark, sculptforge --benchmark; docs/benchmark.md): the meshes it sculpts on, and the
// hardware description written into its JSON result.
#pragma once

#include "core/Json.h"
#include "core/Mesh.h"

#include <functional>
#include <string>
#include <vector>

namespace sf {

// One mesh size proof mode can run on.
struct ProofMeshSize {
    double millions = 0;           // triangles, in millions
    std::string label;             // "33M", "300M micro-displaced"
    // Builds the mesh (about millions * 1e6 triangles; null = core's benchmark sphere, with subdivision levels).
    std::function<Mesh(size_t triangles)> build;
    double bytesPerTriangle = 150; // memory the run needs per triangle (mesh, BVH, levels, GPU buffers, history)
};

// The default sizes: 5M, 20M and 33M always; 100M when the free memory holds it with room to spare.
std::vector<ProofMeshSize> proofDefaultSizes(size_t availableBytes);
// Sizes added by other features (e.g. a 300M micro-displaced mesh): appended to the defaults when memory allows.
// Register at start-up: proofExtraSizes().push_back({300, "300M micro-displaced", builder, bytes});
std::vector<ProofMeshSize>& proofExtraSizes();

// The standard scenes (`sculptforge --benchmark standard-15m`, Help > Benchmark): a procedural head of 15M or 33M
// triangles (core makeBenchHead) and a fixed stroke set (Clay small / medium / large, Move, Smooth, a 240 px Clay
// brush, then an orbit). Names: "standard-15m", "standard-33m", "standard" (both). Empty for any other name.
std::vector<ProofMeshSize> proofStandardScene(const std::string& name);
inline constexpr const char* kProofStrokeSet = "standard/1"; // bump when the standard stroke set changes

// (Free memory now: availableMemoryBytes, core/MemoryGuard.h.)
size_t totalMemoryBytes();
// CPU model, logical cores, RAM, OS: the "hardware" object of the result (the GPU is added by the app).
Json describeHardware();

} // namespace sf
