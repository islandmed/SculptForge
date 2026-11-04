// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Research (docs/design/core-200m.md §7b, N1): optimistic screen-space strokes, measured offline.
//
// sculptforge --bench-optimistic [--bench-sizes 5,15,33] [--bench-out FILE] [--bench-shots DIR]
// For each size: the bench head drawn into a colour + depth canvas (1920x1080); a 240 px Standard stroke placed from
// the canvas alone (dab centres and area normals from its depth); each dab applied to the canvas by one GPU pass over
// its screen rectangle (re-shaded by the Lit Sphere ratio at the bent normal), timed; then the same dabs sculpted on
// the real mesh by the Standard brush (timed: the latency the canvas hides) and drawn ("truth"). Reports the pass cost
// against mesh size and the "pop" (optimistic image vs truth) against doing nothing (canvas before vs truth).
#pragma once

#include "core/Benchmark.h"

#include <string>
#include <vector>

namespace sf {

struct OptimisticBenchOptions {
    std::vector<double> sizesMillions{5, 15, 33};
    float radiusPx = 240.0f;
    int dabs = 30;
    std::string outPath;  // Markdown report (default: stdout)
    std::string shotsDir; // PNGs before / optimistic / truth per size (default: none)
};

// True when argv[i] was one of the options above (advancing i past its value); `run` set by --bench-optimistic.
bool parseOptimisticBenchArg(int argc, char** argv, int& i, bool& run, OptimisticBenchOptions& o);
int runOptimisticBench(const OptimisticBenchOptions& o);

} // namespace sf
