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

// Memory the system can still hand out (Linux MemAvailable, macOS / Windows free + reclaimable); 0 = unknown.
size_t availableMemoryBytes();
size_t totalMemoryBytes();
// CPU model, logical cores, RAM, OS: the "hardware" object of the result (the GPU is added by the app).
Json describeHardware();

} // namespace sf
