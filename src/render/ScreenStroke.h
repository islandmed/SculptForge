// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Optimistic screen-space strokes (research, docs/design/core-200m.md §7b N1; SF_SCREEN_STROKE=1, off by default).
//
// The dabs of a stroke the solver hasn't placed yet (the same list the geometry preview draws, render/Ghost.h) are
// applied to the drawn view instead of to the geometry: after the view is resolved, one pass over the dabs' screen
// rectangles finds for each pixel the drawn surface point whose displaced position lands there (inverse warp over the
// view's depth), bends its normal by the kernels' gradients and re-shades its colour by the ratio of the Lit Sphere
// lookups at the bent and the original normal. Its cost is the brush's pixels, not the mesh. When the solver's result
// is uploaded those dabs leave the list, so the real surface takes over. Standard and Clay for now, Lit Sphere
// shading only; anything else draws the geometry preview as before.
#pragma once

#include "render/GL.h"
#include "render/Ghost.h"

#include <glm/glm.hpp>

namespace sf {

class ScreenStroke {
public:
    ~ScreenStroke();
    void release();
    // True for the strokes this pass can draw (the rest keep the geometry preview).
    static bool supports(const GhostStroke& g) {
        return (g.kind == GhostKind::Standard || g.kind == GhostKind::Clay) && !g.dabs.empty();
    }
    // Applies `g` to the resolved view `resolveFbo` (single-sample colour) using the depth of `drawFbo` (the view's
    // target, any sample count), both `width` x `height`. `view`, `proj`, `viewport` as the frame drew them;
    // `litsphere` the Lit Sphere texture, `lightRot` the shader's uLightRot. False when it couldn't (nothing drawn).
    bool apply(const GhostStroke& g, GLuint drawFbo, GLuint resolveFbo, int width, int height, const glm::mat4& view,
               const glm::mat4& proj, const glm::vec4& viewport, GLuint litsphere, const glm::mat3& lightRot);

private:
    bool ensure(int width, int height);
    GLuint prog_ = 0, vao_ = 0, fbo_ = 0, color_ = 0, depth_ = 0;
    int w_ = 0, h_ = 0;
    bool failed_ = false;
};

} // namespace sf
