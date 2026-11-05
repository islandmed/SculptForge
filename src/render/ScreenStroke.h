// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Optimistic screen-space strokes (research, docs/design/core-200m.md §7b N1; SF_SCREEN_STROKE=1, off by default).
//
// The dabs of a stroke the solver hasn't placed yet (the same list the geometry preview draws, render/Ghost.h) are
// applied to the drawn view instead of to the geometry. After the view is resolved, two passes over the dabs' screen
// rectangle: a field (where each drawn pixel's surface point lands once the dabs move it, and its normal bent by the
// kernels' gradients), then the warp (for each pixel the drawn point that lands there, by fixed-point iteration over
// the field, re-shaded by the ratio of the Lit Sphere lookups at the bent and the original normal). Its cost is the
// brush's pixels, not the mesh. When the solver's result is uploaded those dabs leave the list, so the real surface
// takes over. Standard and Clay for now, Lit Sphere shading only; anything else draws the geometry preview as before.
#pragma once

#include "render/GL.h"
#include "render/Ghost.h"

#include <glm/glm.hpp>

namespace sf {

// How far past its radius a dab's pass reaches, as a multiple of the radius: the surface can move over the
// background beyond the kernel's edge by its displacement (Standard: up to 6% of the radius at full strength).
constexpr float kReach = 1.5f;

class ScreenStroke {
public:
    ~ScreenStroke();
    void release();
    // True for the strokes this pass can draw (the rest keep the geometry preview).
    static bool supports(const GhostStroke& g) {
        return (g.kind == GhostKind::Standard || g.kind == GhostKind::Clay) && !g.dabs.empty();
    }
    // Applies `g` to the view: reads the colour of `colorFbo` (single-sample) and the depth of `depthFbo` (the view's
    // target, any sample count), draws into `targetFbo` (may be `colorFbo`), all `width` x `height`. `view`, `proj`,
    // `viewport` as the frame drew them; `litsphere` the Lit Sphere texture, `lightRot` the shader's uLightRot.
    // `passRect` (x0, y0, x1, y1 pixels) limits the pass to that rectangle and the dabs that reach it (default: all
    // the dabs' rectangle). False when it couldn't (nothing drawn).
    bool apply(const GhostStroke& g, GLuint depthFbo, GLuint colorFbo, GLuint targetFbo, int width, int height,
               const glm::mat4& view, const glm::mat4& proj, const glm::vec4& viewport, GLuint litsphere,
               const glm::mat3& lightRot, const glm::ivec4* passRect = nullptr);

private:
    bool ensure(int width, int height);
    void releaseTargets();
    GLuint fieldProg_ = 0, warpProg_ = 0, vao_ = 0, fbo_ = 0, fieldFbo_ = 0, color_ = 0, depth_ = 0, field_ = 0;
    GLuint resolveFbo_ = 0, resolveDepth_ = 0; // a multisample view's depth, resolved in place
    int w_ = 0, h_ = 0, resolveW_ = 0, resolveH_ = 0;
    bool failed_ = false;
};

} // namespace sf
