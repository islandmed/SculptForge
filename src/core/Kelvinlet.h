// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Kelvinlet brushes: physically based elastic deformation (de Goes & James, "Regularized Kelvinlets: Sculpting
// Brushes based on Fundamental Solutions of Elasticity", SIGGRAPH 2017; the localized falloff idea of "Sharp
// Kelvinlets", 2019). A brush is the closed-form displacement an infinite elastic solid makes when a regularized
// point load (a force, a twist or a uniform scale) acts on it, so the surface moves like clay pushed with a thumb:
// no falloff edge, and the material bulges out around the grab when it is incompressible.
//
// The regularized Kelvinlet for a force f at the origin, with a = 1/(4 pi mu), b = a / (4 (1 - nu)), r_e =
// sqrt(|r|^2 + e^2):
//     u(r) = [ ((a - b) / r_e + a e^2 / (2 r_e^3)) I + (b / r_e^3) r r^T ] f
// Its value at r = 0 is (3a - 2b) / (2e) f, so a "grab" that moves the brush centre by D uses f = 2e D / (3a - 2b);
// the result then depends only on Poisson's ratio nu (the Squash slider), not on the stiffness mu. A single
// Kelvinlet falls off as 1/r and reaches everywhere; the paper's multi-scale extrapolation combines several scales
// e_i with weights that cancel the far field (sum w_i = 0 cancels 1/r, sum w_i e_i^2 = 0 cancels 1/r^3), so the
// tri-scale brush falls off as 1/r^5 and can be cut off where it moves less than an epsilon.
//
// Twist (a torque) and Scale (a uniform pinch / expand) are the paper's affine loads:
//     u_twist(r) = -a (1/r_e^3 + 3 e^2 / (2 r_e^5)) (q x r),   u_scale(r) = (2b - a)(1/r_e^3 + 3 e^2 / (2 r_e^5)) s r
// We use the radial profile t(r) = (1/r_e^3 + 3e^2/(2 r_e^5)) normalized to 1 at the centre (bi-scale, so it falls
// off as 1/r^5): Twist turns every spherical shell around the centre by angle * t(r) (an exact rotation, which keeps
// volume, as twisting does in the paper), Scale integrates the radial flow dr/dt = s t(r) r.
//
// Each brush step evaluates only the vertices inside an adaptive cutoff radius: the distance past which this
// step's displacement is below epsilon, found from the kernel's envelope; a smooth window takes the last part of
// the field to zero, so there is never an edge.
#pragma once

#include <glm/glm.hpp>

#include <cstddef>

namespace sf {

class CommandRegistry;

// Brush > Kelvinlet (saved with the project's brush settings by the app).
struct KelvinletSettings {
    float squash = 0.5f;    // 0..1: Poisson's ratio nu = 0.49 * squash. 0 = rubbery (no bulge), 1 = incompressible clay
    float sharpness = 0.0f; // 0..1: blends toward a tighter, cusped kernel (smaller inner scale): pointed pulls
    float cloneSpacing = 1.0f; // Detail Clone: stamp spacing along the stroke, in stamp widths (0.25..3)
    bool cloneFollowStroke = true; // Detail Clone: the stamp's top turns with the stroke (else it keeps screen up)
    // Dense meshes: a move whose region holds more vertices than this tightens its cutoff (4% -> up to 10% of the
    // peak) and the app gives it a smaller part of the move per frame. 0 (the default): a third of the hardware
    // tier's active triangle budget (core/TierConfig.h: about 670k on a laptop, 2.7M+ on a workstation).
    size_t vertexBudget = 0;
    size_t budget() const;
    // Regions above this many vertices evaluate the far field on a coarse lattice and interpolate it.
    size_t proxyMinVertices = 150000;
    // Regions above this many vertices move only their near field (within the radius) at once and gather the far
    // field on a lattice that a sixth of the vertices take each frame (all of them when the stroke ends).
    size_t deferMinVertices = 400000;

    float poisson() const;
};

enum class KelvinletKind { Grab, Twist, Scale };

// The normalized kernel of one brush: scales e_i and weights w_i (multi-scale extrapolation).
struct KelvinletKernel {
    KelvinletKind kind = KelvinletKind::Grab;
    int scales = 3;
    float eps[3]{1.0f, 1.5f, 2.25f};
    float w[3]{1.0f, 0.0f, 0.0f};
    float beta = 0.25f;  // b / a = 1 / (4 (1 - nu))
    float norm = 1.0f;   // 1 / (unnormalized value at r = 0)

    // Grab: the 3x3 response at offset r is A(r) I + B(r) r r^T (A, B already normalized; K(0) = I).
    void grabTerms(float r2, float& A, float& B) const;
    // Twist / Scale: the radial profile t(r), 1 at the centre.
    float profile(float r2) const;
    // Largest response at distance r (the envelope used for the cutoff): |A| + |B| r^2 for Grab, t for the others.
    float envelope(float r) const;
    // The smallest distance past which envelope() * amount stays below `epsilon` (searched up to `maxDist`).
    float cutoff(float amount, float epsilon, float maxDist) const;
};

// The kernel for a brush of world radius `radius`: Grab is tri-scale (1/r^5 falloff), Twist and Scale bi-scale.
// The scales are chosen so the visible footprint is close to a Move brush of the same radius (a third to a half
// of the displacement at half the radius, about a tenth at the radius) with a soft tail out to ~2 radii. `tight` (the Snake) is the bi-scale grab: a
// narrower pull that tapers.
KelvinletKernel makeKelvinletKernel(KelvinletKind kind, float radius, const KelvinletSettings& settings, bool tight = false);

// Displacement of a point at offset `r` from the brush centre (pure functions, for tests and the brush).
glm::vec3 kelvinletGrab(const KelvinletKernel& k, const glm::vec3& r, const glm::vec3& delta);
glm::vec3 kelvinletTwist(const KelvinletKernel& k, const glm::vec3& r, const glm::vec3& axis, float angle);
glm::vec3 kelvinletScale(const KelvinletKernel& k, const glm::vec3& r, float logScale);

// One step of a Kelvinlet brush (Sculptor::kelvinlet): what moves this step.
struct KelvinletStep {
    glm::vec3 center{0.0f};   // where the load acts (the grabbed point; the twist / scale centre)
    float radius = 0.1f;      // world units: the brush's footprint
    glm::vec3 delta{0.0f};    // Grab / Snake: how far the surface at the centre moves this step
    glm::vec3 travel{0.0f};   // Grab / Snake: how far the load point itself moves (0: as far as delta)
    int substeps = 1;         // Grab / Snake: the move integrated in this many pieces, in one pass over the vertices
    glm::vec3 axis{0.0f, 0.0f, 1.0f}; // Twist: the rotation axis (unit)
    float angle = 0.0f;       // Twist: radians this step at the centre
    float logScale = 0.0f;    // Scale: ln(size factor) this step at the centre (< 0 pinches)
    glm::vec3 viewDir{0.0f, 0.0f, -1.0f};
    bool frontFacesOnly = false;
};

// kelvinlet_settings: Squash, Sharpness and the Detail Clone options from a script.
void registerKelvinletCommands(CommandRegistry& registry);

} // namespace sf
