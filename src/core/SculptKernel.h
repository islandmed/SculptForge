// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The brushes whose offset depends only on where a vertex is (and the dab's frame on the surface): one formula
// shared by the per-dab path (Sculptor::dabSingle) and the fused batches of big brushes (SculptBatch.cpp), so the
// two can't drift apart.
#pragma once

#include "core/Sculpt.h"

#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>

namespace sf {

// Standard, Clay, Flatten, Crease, Pinch, Dam Standard and Trim: the offset of a point from its place alone.
// Fused batches take all but Trim (its crisp cut shows where a centre moved by a hair).
inline bool isPlaneKernelBrush(BrushType t) {
    return t == BrushType::Standard || t == BrushType::Clay || t == BrushType::Flatten || t == BrushType::Crease ||
           t == BrushType::Pinch || t == BrushType::DamStandard || t == BrushType::Trim;
}

struct PlaneKernel {
    BrushType type = BrushType::Standard;
    glm::vec3 center{0.0f}, viewDir{0.0f, 0.0f, -1.0f};
    glm::vec3 areaNormal{0.0f, 0.0f, 1.0f}, areaCenter{0.0f}, strokeDir{0.0f};
    float r = 0.1f, s = 0.5f, sign = 1.0f, focal = 0.0f;
    bool invert = false, viewFalloff = false;

    static glm::vec3 flat(const glm::vec3& v, const glm::vec3& n) { return v - n * glm::dot(v, n); } // onto the plane

    // The offset of a point x with falloff f there.
    glm::vec3 offsetWith(const glm::vec3& x, float f) const {
        glm::vec3 d(0.0f);
        switch (type) {
        case BrushType::Standard:
            d = areaNormal * (sign * s * r * 0.06f * f);
            break;
        case BrushType::Flatten: {
            const float h = glm::dot(x - areaCenter, areaNormal);
            d = -areaNormal * (h * s * f * (invert ? -0.5f : 1.0f));
            break;
        }
        case BrushType::Clay: {
            // Raise a plane slightly above the surface and pull everything below up to it.
            const glm::vec3 planePoint = areaCenter + areaNormal * (sign * r * 0.05f * s);
            const float h = glm::dot(x - planePoint, areaNormal);
            if (sign * h < 0.0f) d = -areaNormal * (h * f * clayPull(s));
            break;
        }
        case BrushType::Crease: {
            // Carves a sharp valley by default (raises a ridge when inverted).
            const float sharp = f * f;
            d = areaNormal * (-sign * s * r * 0.05f * sharp);
            d += flat(center - x, areaNormal) * (0.25f * s * f);
            break;
        }
        case BrushType::Pinch:
            d = flat(center - x, areaNormal) * (sign * 0.2f * s * f);
            break;
        case BrushType::DamStandard: {
            // Narrow, deep V-groove (a ridge when inverted); the sides are always
            // pinched in toward the stroke line, so the crease stays sharp.
            const float core = f * f * f * f;
            d = areaNormal * (-sign * s * r * 0.08f * core);
            glm::vec3 pull = flat(center - x, areaNormal);
            pull -= strokeDir * glm::dot(pull, strokeDir);
            d += pull * (0.3f * s * f);
            break;
        }
        case BrushType::Trim: {
            // Trim Dynamic: a plane just below the area cuts off whatever sticks out
            // above it (fills up to it when inverted). sqrt falloff for a crisp edge.
            const glm::vec3 planePoint = areaCenter - areaNormal * (sign * r * 0.02f * s);
            const float h = glm::dot(x - planePoint, areaNormal);
            if (sign * h > 0.0f) d = -areaNormal * (h * std::min(1.0f, std::sqrt(f) * (0.5f + s)));
            break;
        }
        default:
            break;
        }
        return d;
    }
    // ... with the round falloff (no alpha).
    glm::vec3 offset(const glm::vec3& x) const {
        return offsetWith(x, brushFalloff(brushDistance(x, center, viewDir, viewFalloff), r, focal));
    }
    // Inside the dab's reach (the falloff is 0 beyond it).
    bool reaches(const glm::vec3& x) const { return brushDistance(x, center, viewDir, viewFalloff) <= r; }

    // The offset at x (into d), and the surface's tangents t1, t2 there carried by the move: t + J t, J the
    // offset's derivative (worked out from the formulas above: one falloff slope, no extra offsets). Carried
    // through several dabs in turn, the moved surface's normal is cross(t1, t2) at the end (to first order in each
    // dab's slope, exact where it is constant). Not with viewFalloff. `k` = exp2(2 focal), the falloff's exponent.
    // False (d = 0, tangents as they were) outside the dab.
    bool offsetAndCarry(const glm::vec3& x, float k, glm::vec3& d, glm::vec3& t1, glm::vec3& t2) const {
        const glm::vec3 o = x - center;
        const float dist2 = glm::dot(o, o);
        if (dist2 >= r * r) {
            d = glm::vec3(0.0f);
            return false;
        }
        const float dist = std::sqrt(dist2);
        const float t = 1.0f - dist / r;
        const float g = focal != 0.0f ? std::pow(t, k) : t;
        const float f = g * g * (3.0f - 2.0f * g);
        d = offsetWith(x, f);
        // df/d(dist) = df/dg dg/dt dt/d(dist).
        const float dg = focal != 0.0f ? (t > 0.0f ? k * g / t : 0.0f) : 1.0f;
        const float slope = dist > 1e-12f ? -6.0f * g * (1.0f - g) * dg / r : 0.0f;
        const glm::vec3 grad = dist > 1e-12f ? o * (slope / dist) : glm::vec3(0.0f); // the falloff's gradient
        const auto carry = [&](const glm::vec3& tv) -> glm::vec3 { // J tv
            const float gf = glm::dot(grad, tv);
            const glm::vec3 tflat = tv - areaNormal * glm::dot(areaNormal, tv);
            switch (type) {
            case BrushType::Standard:
                return areaNormal * (sign * s * r * 0.06f * gf);
            case BrushType::Flatten: {
                const float h = glm::dot(x - areaCenter, areaNormal);
                return -areaNormal * (s * (invert ? -0.5f : 1.0f) * (f * glm::dot(areaNormal, tv) + h * gf));
            }
            case BrushType::Clay: {
                const glm::vec3 planePoint = areaCenter + areaNormal * (sign * r * 0.05f * s);
                const float h = glm::dot(x - planePoint, areaNormal);
                if (!(sign * h < 0.0f)) return glm::vec3(0.0f);
                return -areaNormal * (clayPull(s) * (f * glm::dot(areaNormal, tv) + h * gf));
            }
            case BrushType::Crease: {
                const glm::vec3 q = flat(center - x, areaNormal);
                return areaNormal * (-sign * s * r * 0.05f * 2.0f * f * gf) + (q * gf - tflat * f) * (0.25f * s);
            }
            case BrushType::Pinch: {
                const glm::vec3 q = flat(center - x, areaNormal);
                return (q * gf - tflat * f) * (sign * 0.2f * s);
            }
            case BrushType::DamStandard: {
                glm::vec3 q = flat(center - x, areaNormal);
                q -= strokeDir * glm::dot(q, strokeDir);
                const glm::vec3 dq = -(tflat - strokeDir * glm::dot(tflat, strokeDir));
                return areaNormal * (-sign * s * r * 0.08f * 4.0f * f * f * f * gf) + (q * gf + dq * f) * (0.3f * s);
            }
            case BrushType::Trim: {
                const glm::vec3 planePoint = areaCenter - areaNormal * (sign * r * 0.02f * s);
                const float h = glm::dot(x - planePoint, areaNormal);
                if (!(sign * h > 0.0f)) return glm::vec3(0.0f);
                const float sf = std::sqrt(f), w = sf * (0.5f + s);
                if (w >= 1.0f) return -areaNormal * glm::dot(areaNormal, tv);
                const float dw = sf > 1e-12f ? (0.5f + s) * 0.5f / sf * gf : 0.0f;
                return -areaNormal * (w * glm::dot(areaNormal, tv) + h * dw);
            }
            default:
                return glm::vec3(0.0f);
            }
        };
        t1 += carry(t1);
        t2 += carry(t2);
        return true;
    }
    // A tangent pair for the normal n (cross(t1, t2) = n), to carry through offsetAndCarry.
    static void tangents(const glm::vec3& n, glm::vec3& t1, glm::vec3& t2) {
        t1 = glm::normalize(std::abs(n.x) < 0.9f ? glm::cross(n, glm::vec3(1, 0, 0)) : glm::cross(n, glm::vec3(0, 1, 0)));
        t2 = glm::cross(n, t1);
    }
    // The normal the carried tangents give, or n where they folded over (or collapsed).
    static glm::vec3 carriedNormal(const glm::vec3& t1, const glm::vec3& t2, const glm::vec3& n) {
        const glm::vec3 m = glm::cross(t1, t2);
        const float len = glm::length(m);
        return len > 1e-20f && glm::dot(m, n) > 0.0f ? m / len : n;
    }
    // One dab: the offset at x and the normal n after it (offsetAndCarry with fresh tangents).
    glm::vec3 offsetAndNormal(const glm::vec3& x, const glm::vec3& n, float k, glm::vec3& d) const {
        glm::vec3 t1, t2;
        tangents(n, t1, t2);
        if (!offsetAndCarry(x, k, d, t1, t2)) return n;
        return carriedNormal(t1, t2, n);
    }
};

} // namespace sf
