// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Built-in MatCap ("material capture") materials. Each is a lit-sphere image
// generated procedurally here (no third-party images): the viewport looks up
// the colour by the view-space normal, so a whole material costs one texture
// fetch. Polypaint multiplies the matcap; cavity shading is applied on top.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace sf {

int matcapCount();
const char* matcapName(int index);
// Case-insensitive; also accepts names without spaces ("redwax"). -1 if unknown.
int findMatcap(const std::string& name);
// The material new documents (and thumbnails) start with: Soft Clay, a matte warm clay.
int defaultMatcap();

// How strongly the viewport darkens creases and lifts ridges on top of this matcap (the
// curvature term in the mesh shader); the Sculpt material leans on it more than the others.
struct MatcapCavity {
    float crease = 0.55f, ridge = 0.2f;
};
MatcapCavity matcapCavity(int index);

// `size` x `size` RGBA8 pixels, sRGB-encoded, rows bottom-up (OpenGL order):
// pixel (i, j) shows the sphere normal x = (i + 0.5) / size * 2 - 1,
// y = (j + 0.5) / size * 2 - 1. Pixels outside the disk repeat the rim.
std::vector<uint8_t> generateMatcap(int index, int size = 256);

} // namespace sf
