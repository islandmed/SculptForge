// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Research (docs/design/core-200m.md §7b, N2): multi-scale displacement on micro patches.
//
// A patch's grid of 16-bit heights (core/Micro.h layout, n = 2^L segments) as interpolating-wavelet coefficients by
// integer lifting: a point that is new at spacing s (its coordinates multiples of s, not all of 2s) stores its height
// minus the floor-average of its two parents on the coarser edge through it (the points s away along that edge); the
// three corners store their heights. Arithmetic is modulo 2^16, so the transform is exactly invertible whatever the
// values. A coarse edit (waveletAddCoarse) changes only the points of the spacing-s lattice: the finer coefficients,
// the detail, ride along and the edit reaches the fine points as its linear interpolation when reconstructed.
#pragma once

#include <cstdint>
#include <functional>
#include <string>

namespace sf {

// In place, heights -> coefficients (finest spacing first) and back (coarsest first). n = 2^L, (n+1)(n+2)/2 values.
void waveletForward(int16_t* grid, uint32_t n);
void waveletInverse(int16_t* grid, uint32_t n);
// Coefficients of one patch: adds delta(i, j, height) (in units of the height step; i, j on the spacing-`s` lattice,
// `height` the point's current height) to the lattice points, touching only that lattice's coefficients
// ((n/s+1)(n/s+2)/2 points).
void waveletAddCoarse(int16_t* coeffs, uint32_t n, uint32_t s, const std::function<int(uint32_t i, uint32_t j, int height)>& delta);

// sculptforge --bench-wavelet / sculptforge-cli --bench-wavelet: a micro head (Micro 7), a Standard dab of growing
// radius on today's adaptive micro brush against the coarse wavelet edit; Markdown to `outPath` (or stdout).
int runWaveletBench(const std::string& outPath);

} // namespace sf
