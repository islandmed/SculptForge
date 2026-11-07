// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Stencil (#155, docs/design/stencil.md): a reference image laid over the view that the brushes work through. It sits
// in screen space (the model turns under it); each dab weighs every vertex by the stencil at the point where the
// vertex shows on screen, times the brush's own falloff. Sculpt brushes take its brightness as their alpha, Vertex
// Paint its colours (or its brightness masking the paint colour). Outside the image (unless tiled) brushes do
// nothing. The image comes from the same loader as alphas (core/Image.h); the placement is kept relative to the
// canvas, so it stays put when the window changes size.
#pragma once

#include "core/Image.h"
#include "core/Symmetry.h"

#include <glm/glm.hpp>

#include <memory>
#include <string>

namespace sf {

class CommandRegistry;

struct Stencil {
    std::shared_ptr<const Image> image; // null = no stencil (straight from the file; inverted on sampling)
    std::string name, path;             // shown in the Stencil section; path empty for one made from an alpha
    // Placement. `center` in canvas fractions (0..1 across, 0..1 down); `size` the image's height as a fraction of the
    // canvas height (its width follows the image's aspect); `angle` radians, counter-clockwise as seen.
    glm::vec2 center{0.5f, 0.5f};
    float size = 0.8f;
    float angle = 0.0f;
    float opacity = 0.5f; // how strongly the overlay shows (the brushes always take the whole image)
    bool invert = false;  // the image's negative: black works, white leaves the surface alone
    bool tile = false;    // the image repeats across the whole view
    bool visible = true;  // hidden: drawn nowhere and the brushes ignore it
    bool colors = true;   // Vertex Paint lays the image's colours; off, its brightness masks the paint colour

    bool active() const { return visible && image && !image->empty(); }
    float aspect() const { return image && !image->empty() ? image->aspect() : 1.0f; }

    // Image coordinates (0..1, v down; tiled: any value, repeating) of a canvas point (fractions as `center`) on a
    // canvas `canvasAspect` (width / height) wide. False outside the image when it isn't tiled.
    bool imageCoord(glm::vec2 canvas, float canvasAspect, glm::vec2& uv) const;
    // The reverse: where image coordinates (u, v) land on the canvas (corners, handles).
    glm::vec2 canvasPoint(glm::vec2 uv, float canvasAspect) const;
    // At image coordinates: the brightness the sculpt brushes take (luminance x the pixel's alpha, inverted), and
    // the colour (linear) with how much of it Vertex Paint lays (.a: the pixel's alpha).
    float value(glm::vec2 uv) const;
    glm::vec4 color(glm::vec2 uv) const;

    // Makes a stencil from an image (`image` empty: clears it); placement kept.
    void set(Image image, std::string name, std::string path);
    // Centred, the image's height 80% of the canvas's, upright.
    void resetPlacement();
};

// A stencil as one dab sees it: the view it was made in, and the symmetry copy the dab is for (the mirror's
// vertices take the stencil at their mirror image's place on screen, so the pattern mirrors across).
struct StencilView {
    const Stencil* stencil = nullptr;
    glm::mat4 viewProj{1.0f};
    float canvasAspect = 1.0f;
    SymCopy copy; // identity: the stroke itself

    bool active() const { return stencil && stencil->active(); }
    // The canvas point (fractions) a world point shows at; false behind the camera.
    bool canvasAt(const glm::vec3& world, glm::vec2& canvas) const;
    // 0..1: how much a sculpt dab moves the vertex at `world` (0 off the image or behind the camera).
    float weight(const glm::vec3& world) const;
    // The colour (linear) and how much of it a paint dab lays at `world`: the image's own (colors) or the paint
    // colour masked by its brightness.
    glm::vec4 paint(const glm::vec3& world, const glm::vec3& paintColor) const;
};

// stencil_load, stencil_set, stencil_clear.
void registerStencilCommands(CommandRegistry& registry);

} // namespace sf
