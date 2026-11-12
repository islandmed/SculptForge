// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "render/Ghost.h"

#include "core/Sculpt.h"
#include "core/SculptKernel.h"

#include <algorithm>
#include <cmath>

namespace sf {

GhostKind ghostKindForBrush(int brushType) {
    // core/Sculpt.h BrushType order: Standard, Clay, Smooth, Move, Inflate, Crease, Flatten, Pinch, Sharp Crease,
    // Clay Block, SnakeHook, Trim, Layer, Nudge, Vdm.
    switch (brushType) {
    case 0: return GhostKind::Standard;
    case 1: return GhostKind::Clay;
    case 4: return GhostKind::Inflate;
    case 5: return GhostKind::Crease;
    case 6: return GhostKind::Flatten;
    case 7: return GhostKind::Pinch;
    case 8: return GhostKind::SharpCrease;
    case 9: return GhostKind::ClayBlock;
    case 11: return GhostKind::Trim;
    default: return GhostKind::None;
    }
}

float ghostFalloff(float dist, float radius, float focal) {
    float t = std::clamp(1.0f - dist / radius, 0.0f, 1.0f);
    if (focal != 0.0f) t = std::pow(t, std::exp2(2.0f * std::clamp(focal, -1.0f, 1.0f)));
    return t * t * (3.0f - 2.0f * t);
}

namespace {
constexpr float kClayBlockReach = 1.3f; // the square tip's corners reach this far (Sculptor::dabSingle's `reach`), in radii
glm::vec3 onPlane(const glm::vec3& v, const glm::vec3& n) { return v - n * glm::dot(v, n); }
} // namespace

// Keep in step with kMeshVS's ghostMove (Renderer.cpp) and the kernels in Sculptor::dabSingle.
glm::vec3 ghostMove(const GhostStroke& g, glm::vec3 x, const glm::vec3& n) {
    const float sign = g.invert ? -1.0f : 1.0f;
    if (g.kind == GhostKind::Move) { // Sculptor::grab: every mirror's weight from the grab's start position
        const glm::vec3 start = x;
        for (const GhostDab& d : g.dabs) {
            const float dist = glm::length(start - d.center);
            if (dist < d.radius) x += d.normal * (ghostFalloff(dist, d.radius, g.focal) * d.strength);
        }
        return x;
    }
    for (const GhostDab& d : g.dabs) {
        const glm::vec3 o = x - d.center;
        const float dist = glm::length(o);
        if (d.radius <= 0.0f || dist >= d.radius * (g.kind == GhostKind::ClayBlock ? kClayBlockReach : 1.0f)) continue;
        const float f = ghostFalloff(dist, d.radius, g.focal) * d.weight;
        const float s = d.strength, r = d.radius;
        const glm::vec3& an = d.normal;
        switch (g.kind) {
        case GhostKind::Standard: x += an * (sign * s * r * 0.06f * f); break;
        case GhostKind::Inflate: x += n * (sign * s * r * 0.05f * f); break;
        case GhostKind::Clay: {
            // The solver's own plane and pull (PlaneKernel::clayPlaneAt, clayPull), not copies of their constants.
            float k;
            const glm::vec3 planePoint = PlaneKernel::clayPlaneAt(d.areaCenter, an, d.center, sign, r * 0.05f * s, k);
            const float h = glm::dot(x - planePoint, an);
            if (sign * h < 0.0f) x -= an * (h * f * clayPull(s) * k);
            break;
        }
        case GhostKind::ClayBlock: {
            // The solver measures the square tip's edge in the brush's plane (the tip's frame: alphaUp on the plane) and its
            // corners in space. Without the frame (up = 0) the tip is round: the distance in the plane.
            float lateral = glm::length(onPlane(o, an));
            const glm::vec3 tipUp = onPlane(d.up, an);
            if (glm::length(tipUp) > 1e-6f) {
                const glm::vec3 u = glm::normalize(tipUp);
                lateral = std::max(std::abs(glm::dot(o, u)), std::abs(glm::dot(o, glm::cross(u, an))));
            }
            const float e = std::clamp((1.0f - lateral / r) / 0.35f, 0.0f, 1.0f);
            const float corner = std::clamp((kClayBlockReach - dist / r) / 0.3f, 0.0f, 1.0f);
            const float w = e * e * (3.0f - 2.0f * e) * corner * d.weight;
            const float h = glm::dot(x - (d.areaCenter + an * (sign * r * 0.08f * s)), an);
            if (sign * h < 0.0f) x -= an * (h * w * clayPull(s));
            break;
        }
        case GhostKind::Flatten: {
            const float h = glm::dot(x - d.areaCenter, an);
            x -= an * (h * s * f * (g.invert ? -0.5f : 1.0f));
            break;
        }
        case GhostKind::Crease:
            x += an * (-sign * s * r * 0.05f * f * f) + onPlane(d.center - x, an) * (0.25f * s * f);
            break;
        case GhostKind::Pinch: x += onPlane(d.center - x, an) * (sign * 0.2f * s * f); break;
        case GhostKind::SharpCrease:
            x += an * (-sign * s * r * 0.08f * f * f * f * f) + onPlane(d.center - x, an) * (0.3f * s * f);
            break;
        case GhostKind::Trim: {
            const float h = glm::dot(x - (d.areaCenter - an * (sign * r * 0.02f * s)), an);
            if (sign * h > 0.0f) x -= an * (h * std::min(1.0f, std::sqrt(f) * (0.5f + s)));
            break;
        }
        case GhostKind::Move: break; // (below: measured from the start position, not the moved one)
        case GhostKind::None: break;
        }
    }
    return x;
}

} // namespace sf
