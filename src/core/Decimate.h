// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/KeepMasked.h"
#include "core/Mesh.h"

#include <vector>

namespace sf {

// Quadric-error-metric edge-collapse decimation (Garland & Heckbert), with
// boundary preservation, normal-flip and manifold (link condition) checks.
// Collapses keep one endpoint, so output vertices are a subset of the input's
// and sculpt layers / blend shapes, mask and colours carry over exactly;
// surviving triangles keep their face group and hidden flag, and corner UVs
// follow the kept vertex. A decimated result is all triangles (quad pairs are
// dropped). keepMasked: masked vertices (mask >= kKeepMaskedThreshold) are never collapsed, so they and the
// triangles between them come through unchanged (#43). maxValence: a collapse that leaves the kept vertex more
// neighbours than this (and than it had) is skipped, so flat areas can't fan out into slivers; 0 = no cap (the
// A/B in nightly_decimate_valence_cap).
constexpr size_t kDecimateMaxValence = 16;
Mesh decimate(const Mesh& src, size_t targetTriangles, bool keepMasked = false,
              size_t maxValence = kDecimateMaxValence);

// LOD chain: lods[0] is a copy of `src`, each following level has
// `ratio` times the triangles of the previous one.
std::vector<Mesh> generateLods(const Mesh& src, int levels, float ratio);

} // namespace sf
