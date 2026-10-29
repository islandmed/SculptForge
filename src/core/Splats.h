// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// 3D Gaussian splats: the capture format behind Polycam, Luma, Postshot,
// Scaniverse, KIRI and friends, and now a first-class type in OpenUSD 26.03
// (ParticleField3DGaussianSplat) and glTF (KHR_gaussian_splatting).
//
// SculptForge uses splats three ways:
//  - as a live reference: load a phone capture and sculpt next to/over it
//  - as a source: convert a capture into a watertight, sculptable mesh
//  - as an output: turn a sculpt into splats for splat-based viewers/engines
//
// Values are stored the way USD and glTF store them: linear scales (standard
// deviations), linear opacity, unit quaternions, and spherical-harmonic
// coefficients grouped per splat with the DC term first. PLY files (log
// scales, logit opacity) are converted on load and save.
#pragma once

#include "core/Mesh.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <string>
#include <vector>

namespace sf {

constexpr float kShC0 = 0.28209479177387814f; // band-0 spherical harmonic

struct SplatCloud {
    std::vector<glm::vec3> positions;
    std::vector<glm::quat> rotations;
    std::vector<glm::vec3> scales;
    std::vector<float> opacities;
    int shDegree = 0;
    std::vector<glm::vec3> sh; // shCount() coefficients per splat, DC first
    // Placement in the scene (rotation, translation and uniform scale). Kept
    // separate so exports to USD/glTF stay lossless; PLY export bakes it.
    glm::mat4 transform{1.0f};
    std::string name = "Splats";

    size_t size() const { return positions.size(); }
    bool empty() const { return positions.empty(); }
    int shCount() const { return (shDegree + 1) * (shDegree + 1); }
    void resize(size_t n, int degree);
    void clear() { *this = SplatCloud(); }

    glm::vec3 color(size_t i) const { return glm::vec3(0.5f) + kShC0 * sh[i * size_t(shCount())]; }
    void setColor(size_t i, const glm::vec3& rgb) { sh[i * size_t(shCount())] = (rgb - glm::vec3(0.5f)) / kShC0; }
    glm::vec3 worldPosition(size_t i) const { return glm::vec3(transform * glm::vec4(positions[i], 1.0f)); }
    // Bounds in world space; `keepFraction` < 1 ignores outliers per axis.
    AABB bounds(float keepFraction = 1.0f) const;
    // Empty string when all arrays agree in size.
    std::string validate() const;

    // Uniform scale and rotation of `transform`.
    float transformScale() const;
    glm::quat transformRotation() const;
    // Bakes `transform` into the splats. View-dependent colour (SH degree >= 1)
    // is dropped when the rotation is not identity, because it would point the
    // wrong way. Returns false if SH data was dropped.
    bool bakeTransform();
    // Removes splats by predicate; keeps every array in sync.
    template <class Pred> size_t removeIf(Pred pred);
};

// Drops faint and outlying splats (floaters). Returns how many were removed.
size_t cleanSplats(SplatCloud& cloud, float minOpacity, float keepFraction);

// Rotates the cloud's placement; `degrees` about X, then Y, then Z, around the cloud centre.
void rotateSplats(SplatCloud& cloud, const glm::vec3& degrees);
// Moves and scales the cloud so its main body is centred at the origin with
// radius ~1 (matching a new sphere).
void fitSplats(SplatCloud& cloud);

struct SplatToMeshOptions {
    int resolution = 192;      // voxels across the longest side
    float minOpacity = 0.25f;  // ignore fainter splats
    float keepFraction = 0.97f; // ignore positional outliers (per axis)
    int closeRadius = 2;       // seal holes up to this many voxels
    int smoothIterations = 6;
};
// Builds a watertight sculpting mesh from a capture (largest solid only).
Mesh splatsToMesh(const SplatCloud& cloud, const SplatToMeshOptions& options, std::string* error = nullptr);

struct MeshToSplatsOptions {
    size_t count = 250'000;
    glm::vec3 color{0.72f, 0.68f, 0.62f}; // Soft Clay grey
    bool shading = true;                  // bake soft light and cavity darkening into the colour
};
// Covers the surface with flat, oriented splats (surfels).
SplatCloud meshToSplats(const Mesh& mesh, const MeshToSplatsOptions& options);

template <class Pred> size_t SplatCloud::removeIf(Pred pred) {
    const size_t k = size_t(shCount());
    size_t w = 0;
    for (size_t i = 0; i < size(); ++i) {
        if (pred(i)) continue;
        if (w != i) {
            positions[w] = positions[i];
            rotations[w] = rotations[i];
            scales[w] = scales[i];
            opacities[w] = opacities[i];
            for (size_t c = 0; c < k; ++c) sh[w * k + c] = sh[i * k + c];
        }
        ++w;
    }
    const size_t removed = size() - w;
    positions.resize(w);
    rotations.resize(w);
    scales.resize(w);
    opacities.resize(w);
    sh.resize(w * k);
    return removed;
}

} // namespace sf
