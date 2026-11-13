// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// View shadows and ambient occlusion (owner 11-13): one pass over the drawn view, after it is resolved, that darkens
// it by how hidden each point is from the light around it (ambient occlusion, sampled in a disc around the point in
// the view's depth) and from the key light (a ray marched towards the light through the same depth: cast shadows,
// the nose on the cheek and the brow over the eyes). Both are worked out at half resolution, blurred with a
// depth-aware filter (Softness), and multiplied into the colour at full resolution.
//
// Traced in the view's depth rather than drawn from the light into a shadow map: the key light turns with the
// view, so a shadow map would draw the whole scene a second time every frame the view moves (twice the 104M
// micro triangles of the benchmark); this costs the view's pixels, never the mesh. What it can't see (an occluder
// off screen or hidden behind another surface) casts nothing.
#pragma once

#include "render/GL.h"

#include <glm/glm.hpp>

namespace sf {

struct OcclusionParams {
    bool ao = false, shadows = false;
    float aoStrength = 0.65f;  // 0..1: how dark full occlusion gets
    float aoSoftness = 0.4f;   // 0..1: the blur's width
    float aoRadius = 0.1f;     // view-space units: how far around a point occluders count
    glm::vec3 light{0.0f, 0.0f, 1.0f}; // view space, towards the key light
    float shadowDarkness = 0.6f;       // 0..1: the share of the light a shadow takes away
    float shadowSoftness = 0.35f;      // 0..1: penumbra width
    float shadowLength = 0.5f;         // view-space units the ray marches
    float thickness = 0.1f;            // view-space units: how thick a surface in the depth is assumed to be
    float depthRange = 10.0f;          // view-space distance past everything drawn (packs the half-res depth)
};

class OcclusionPass {
public:
    ~OcclusionPass();
    void release();
    // Darkens the resolved view `resolveFbo` (single-sample colour, `width` x `height`) by `p`, reading the depth of
    // `drawFbo` (the view's target, any sample count). `proj` and `viewport` (x, y, w, h in target pixels) as the frame
    // drew them. False when it couldn't (unsupported here: the view stays as it was, and later calls return false).
    bool apply(const OcclusionParams& p, GLuint drawFbo, GLuint resolveFbo, int width, int height, const glm::mat4& proj,
               const glm::vec4& viewport);
    bool failed() const { return failed_; }

private:
    bool ensure(int width, int height);
    GLuint occProg_ = 0, blurProg_ = 0, compProg_ = 0, vao_ = 0;
    GLuint depthFbo_ = 0, depth_ = 0, colorFbo_ = 0, color_ = 0;
    GLuint halfFbo_[2] = {}, half_[2] = {};
    int w_ = 0, h_ = 0;
    bool failed_ = false;
};

} // namespace sf
