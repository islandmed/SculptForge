// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Small pictures of a mesh drawn on the CPU, for Generate 3D's candidates (any thread, no GPU): a shaded thumbnail
// and a silhouette, flat black on a light background, which shows the strength of a form better than shading does.
// Orthographic, framed to the mesh's bounds, Y up, looking down -Z from the front, turned by `yaw` about Y and
// tilted by `pitch` (degrees). Hidden faces are skipped.
#pragma once

#include "core/Image.h"
#include "core/Mesh.h"

namespace sf {

enum class PreviewStyle { Shaded, Silhouette };

// A `size` x `size` RGBA picture (2x supersampled). An empty mesh gives the bare background.
Image renderMeshPreview(const Mesh& mesh, int size, PreviewStyle style, float yawDegrees = 0.0f, float pitchDegrees = 0.0f);

// The fraction of the picture a silhouette covers (its dark pixels), 0..1.
float silhouetteCoverage(const Image& silhouette);
// How different two silhouettes of the same size are: dark in one but not the other, over dark in either (0 = the
// same outline, 1 = no overlap). 1 when the sizes differ.
float silhouetteDifference(const Image& a, const Image& b);

} // namespace sf
