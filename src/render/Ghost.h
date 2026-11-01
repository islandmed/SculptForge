// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Predictive stroke preview ("ghost"): the dabs of a stroke that the brush solver hasn't placed yet, drawn as a
// displacement of the geometry the GPU already holds, so the surface moves under the pointer the frame the
// pointer moves while the solver catches up on another thread (app/AsyncStroke.cpp).
//
// The mesh vertex shader (Renderer.cpp, GHOST) moves each vertex within a dab's radius as that brush's kernel
// would (core/Sculpt.cpp), dab after dab, from the uploaded position and normal, and bends the shading normal
// by the same moves evaluated at two nearby points of the tangent plane. What it can't know it estimates: each
// dab's surface point is where its screen ray meets the plane of the last placed dab, and the brush's area
// normal and centre are the last placed dab's. When the solver's result for a dab is uploaded, that dab leaves
// the ghost in the same frame, so the real surface replaces the preview without a jump wherever the estimate
// holds (exactly on a plane with Standard, test ghost_matches_standard_dab).
//
// ghostMove() is the shader's function on the CPU, for the tests. The preview never changes the mesh: the
// final geometry is the solver's, as without it.
#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

namespace sf {

// The brush kernels the preview can draw (the rest show no ghost: Smooth, Layer, Nudge, Move, Snake Hook,
// VDM, recipes, masking, painting).
enum class GhostKind : int32_t { None = 0, Standard, Inflate, Clay, ClayBuildup, Flatten, Crease, Pinch, DamStandard, Trim };

struct GhostDab {
    glm::vec3 center{0.0f}; // surface point (estimated)
    float radius = 0.0f;    // world units
    glm::vec3 normal{0.0f, 0.0f, 1.0f}; // the brush's area normal (estimated), mirrored with the dab
    float strength = 0.0f;  // strength x pressure, 0..1
    glm::vec3 areaCenter{0.0f}; // the brush's area centre (plane brushes), estimated
    float weight = 1.0f;        // < 1: a provisional dab at the predicted head, shown in part
    uint32_t mirror = 0;        // symmetry pass (0: the dab itself); the shader bounds each pass's dabs together
};

struct GhostStroke {
    GhostKind kind = GhostKind::None;
    bool invert = false;
    float focal = 0.0f;
    float edge = 0.0f; // the mesh's edge length under the brush: the preview's cavity shading (curvatureAt) scales with it
    // Grouped by mirror, each group in the order the solver will place its dabs (the solver does every mirror of
    // a dab before the next dab; the order only matters where mirrored dabs overlap, on the symmetry plane).
    std::vector<GhostDab> dabs;
    static constexpr size_t kMaxDabs = 64, kMaxMirrors = 8; // the shader's uniform arrays
    bool empty() const { return kind == GhostKind::None || dabs.empty(); }
};

// The ghost's kernel for a brush type (core/Sculpt.h BrushType as int, so this header needs no core).
GhostKind ghostKindForBrush(int brushType);

// brushFalloff (core/Sculpt.cpp), as the shader computes it.
float ghostFalloff(float dist, float radius, float focal);

// Where the preview draws a vertex at x with normal n (the shader's ghostMove).
glm::vec3 ghostMove(const GhostStroke& g, glm::vec3 x, const glm::vec3& n);

} // namespace sf
