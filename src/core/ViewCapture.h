// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Alpha and VDM from the viewport: drag a square (or circle) over any sculpted detail and capture it as
//   (a) a grayscale height alpha: the surface's height toward the viewer over the view-aligned (or fitted) plane,
//       normalized to 0..1, optionally only the fine detail (the smooth form subtracted);
//   (b) a vector displacement map (core/Vdm.h): the true 3D offset of every point from a smooth base surface,
//       in the base's own tangent frame, so overhangs and sideways leans come along. The base is the region
//       smoothed by implicit fairing (Desbrun et al. 1999: (I + lambda L) x = x0, its rim held), or the fitted
//       plane when Base Smoothing is 0.
// Either becomes a brush at once: the alpha shapes any brush, the VDM is stamped by the VDM brush or along a
// stroke by Detail Clone.
#pragma once

#include "core/BrushAlpha.h"
#include "core/Vdm.h"

#include <glm/glm.hpp>

#include <string>

namespace sf {

class BVH;
class CommandRegistry;
class Mesh;
struct Document;

// The captured square: centred on `center` (a point on the surface), seen along `forward`, with the screen's
// `right` and `up`; `halfSize` world units from the centre to an edge. Captures project along `forward`
// (orthographically), so the frame is the same in a perspective or orthographic view.
struct CaptureFrame {
    glm::vec3 center{0.0f};
    glm::vec3 forward{0.0f, 0.0f, -1.0f};
    glm::vec3 right{1.0f, 0.0f, 0.0f};
    glm::vec3 up{0.0f, 1.0f, 0.0f};
    float halfSize = 0.1f;
    bool circle = false; // capture the inscribed circle only (the rest fades to nothing)
};

struct CaptureOptions {
    int resolution = 256;     // pixels a side (32..1024)
    float edgeFade = 0.25f;   // fraction of the half size over which the capture fades to 0 at its border
    float smoothing = 0.5f;   // 0..1: how much smooth form the base takes away (0: plain plane; VDM and alpha)
    bool fitPlane = true;     // height alpha: relative to a plane fitted to the surface (removes tilt)
    bool invert = false;      // height alpha: white = deepest
};

// What a capture found (for the status line and tests).
struct CaptureInfo {
    size_t vertices = 0;      // region vertices (VDM)
    float depthRange = 0.0f;  // world height range the alpha's 0..1 spans
    float maxOffset = 0.0f;   // largest VDM vector, in brush radii
    int coverage = 0;         // texels the surface covered
    int iterations = 0;       // smoothing solver iterations
};

// A frame for the square around the surface point `center` seen along `forward` with the camera's `up`.
CaptureFrame makeCaptureFrame(const glm::vec3& center, const glm::vec3& forward, const glm::vec3& up, float halfSize,
                              bool circle = false);

bool captureHeightAlpha(const Mesh& mesh, const BVH& bvh, const CaptureFrame& frame, const CaptureOptions& opt,
                        BrushAlpha& out, std::string& error, CaptureInfo* info = nullptr);
bool captureVdmFromView(const Mesh& mesh, const BVH& bvh, const CaptureFrame& frame, const CaptureOptions& opt,
                        VectorDisplacement& out, std::string& error, CaptureInfo* info = nullptr);

// The alpha as an 8-bit grayscale PNG (what the Alphas folder keeps).
Image alphaToImage(const BrushAlpha& alpha);

// capture_alpha, capture_vdm: the same from a script (point + view + size).
void registerCaptureCommands(CommandRegistry& registry);

} // namespace sf
