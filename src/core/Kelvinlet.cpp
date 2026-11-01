// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Kelvinlet brushes (see Kelvinlet.h for the math) and the Detail Clone brush (VDM stamps along a stroke).
#include "core/Kelvinlet.h"

#include "core/Commands.h"
#include "core/Sculpt.h"
#include "core/ThreadPool.h"
#include "core/Vdm.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace sf {

float KelvinletSettings::poisson() const { return 0.49f * std::clamp(squash, 0.0f, 1.0f); }

// ---------------------------------------------------------------------------
// Kernels
// ---------------------------------------------------------------------------

void KelvinletKernel::grabTerms(float r2, float& A, float& B) const {
    A = B = 0.0f;
    for (int i = 0; i < scales; ++i) {
        const float e2 = eps[i] * eps[i];
        const float re2 = r2 + e2;
        const float inv = 1.0f / std::sqrt(re2);
        const float inv3 = inv * inv * inv;
        A += w[i] * ((1.0f - beta) * inv + 0.5f * e2 * inv3);
        B += w[i] * (beta * inv3);
    }
    A *= norm;
    B *= norm;
}

float KelvinletKernel::profile(float r2) const {
    float t = 0.0f;
    for (int i = 0; i < scales; ++i) {
        const float e2 = eps[i] * eps[i];
        const float re2 = r2 + e2;
        const float inv = 1.0f / std::sqrt(re2);
        const float inv3 = inv * inv * inv;
        t += w[i] * (inv3 + 1.5f * e2 * inv3 * inv * inv);
    }
    return t * norm;
}

float KelvinletKernel::envelope(float r) const {
    const float r2 = r * r;
    if (kind == KelvinletKind::Grab) {
        float A, B;
        grabTerms(r2, A, B);
        return std::max(std::abs(A), std::abs(A + B * r2));
    }
    return std::abs(profile(r2)) * r; // displacement per radian (or per unit log scale) at distance r
}

float KelvinletKernel::cutoff(float amount, float epsilon, float maxDist) const {
    if (!(amount > 0.0f) || !(maxDist > 0.0f)) return 0.0f;
    // Walk in from the far end: the first sample whose displacement reaches epsilon (the field is not
    // monotonic: multi-scale kernels dip slightly below zero before they fade).
    constexpr int kSteps = 96;
    for (int i = kSteps; i > 0; --i) {
        const float r = maxDist * float(i) / float(kSteps);
        if (envelope(r) * amount >= epsilon) return std::min(maxDist, r + maxDist / float(kSteps));
    }
    return maxDist / float(kSteps);
}

KelvinletKernel makeKelvinletKernel(KelvinletKind kind, float radius, const KelvinletSettings& settings, bool tight) {
    KelvinletKernel k;
    k.kind = kind;
    const float nu = settings.poisson();
    k.beta = 1.0f / (4.0f * (1.0f - nu));
    const float sharp = std::clamp(settings.sharpness, 0.0f, 1.0f);
    radius = std::max(radius, 1e-6f);
    if (kind == KelvinletKind::Grab && !tight) {
        // Tri-scale (1/r^5 far field). The weights cancel the 1/r and 1/r^3 terms of the far field.
        const float e1 = 0.75f * radius;
        k.scales = 3;
        k.eps[0] = e1;
        k.eps[1] = 1.5f * e1;
        k.eps[2] = 2.25f * e1;
    } else {
        // Bi-scale: Snake's narrower pull (1/r^3 far field), and Twist / Scale (whose single-scale field already
        // falls off as 1/r^3, so the pair falls off as 1/r^5).
        const float e1 = (kind == KelvinletKind::Grab ? 0.6f : 0.8f) * radius;
        k.scales = 2;
        k.eps[0] = e1;
        k.eps[1] = 1.5f * e1;
    }
    // Sharpness: a smaller inner scale gives the cusped, localized peak of the sharp Kelvinlets while the outer
    // scales keep the far field (the weights are recomputed, so the far-field terms still cancel).
    k.eps[0] *= 1.0f - 0.6f * sharp;
    if (k.scales == 3) {
        const float a = k.eps[0] * k.eps[0], b = k.eps[1] * k.eps[1], c = k.eps[2] * k.eps[2];
        k.w[0] = 1.0f;
        k.w[1] = -(c - a) / (c - b);
        k.w[2] = (b - a) / (c - b);
    } else {
        k.w[0] = 1.0f;
        k.w[1] = -1.0f;
        k.w[2] = 0.0f;
    }
    // Normalize: K(0) = I for Grab, t(0) = 1 for Twist and Scale.
    float at0 = 0.0f;
    for (int i = 0; i < k.scales; ++i)
        at0 += k.w[i] * (kind == KelvinletKind::Grab ? (1.5f - k.beta) / k.eps[i] : 2.5f / (k.eps[i] * k.eps[i] * k.eps[i]));
    k.norm = std::abs(at0) > 1e-30f ? 1.0f / at0 : 0.0f;
    return k;
}

glm::vec3 kelvinletGrab(const KelvinletKernel& k, const glm::vec3& r, const glm::vec3& delta) {
    float A, B;
    k.grabTerms(glm::dot(r, r), A, B);
    return A * delta + B * glm::dot(r, delta) * r;
}

glm::vec3 kelvinletTwist(const KelvinletKernel& k, const glm::vec3& r, const glm::vec3& axis, float angle) {
    const float theta = angle * k.profile(glm::dot(r, r));
    // Rodrigues: an exact rotation of this shell about the axis through the centre.
    const float c = std::cos(theta), s = std::sin(theta);
    const glm::vec3 rotated = r * c + glm::cross(axis, r) * s + axis * (glm::dot(axis, r) * (1.0f - c));
    return rotated - r;
}

glm::vec3 kelvinletScale(const KelvinletKernel& k, const glm::vec3& r, float logScale) {
    // Integrates the radial flow dr/dt = s t(r) r (midpoint steps): a pinch never folds a vertex past the centre.
    const float len0 = glm::length(r);
    if (len0 < 1e-12f) return glm::vec3(0.0f);
    const int steps = std::clamp(int(std::ceil(std::abs(logScale) / 0.2f)), 1, 8); // midpoint steps: plenty
    const float h = logScale / float(steps);
    float len = len0;
    for (int i = 0; i < steps; ++i) {
        const float mid = len * std::exp(0.5f * h * k.profile(len * len));
        len *= std::exp(h * k.profile(mid * mid));
    }
    return r * (len / len0 - 1.0f);
}

// ---------------------------------------------------------------------------
// The brushes
// ---------------------------------------------------------------------------

namespace {

KelvinletKind kindOf(BrushType type) {
    switch (type) {
    case BrushType::KelvinTwist: return KelvinletKind::Twist;
    case BrushType::KelvinScale: return KelvinletKind::Scale;
    default: return KelvinletKind::Grab;
    }
}

float window(float r, float cut) {
    // 1 inside 70% of the cutoff, then a smoothstep to 0 at the cutoff.
    const float t = std::clamp((r - 0.7f * cut) / (0.3f * cut), 0.0f, 1.0f);
    return 1.0f - t * t * (3.0f - 2.0f * t);
}

} // namespace

namespace {

// The kernel's radial terms tabulated over u = r^2 / cut^2 (they depend on |r| only), the window folded in: a
// lookup and a lerp per vertex instead of three square roots and divisions per scale.
struct RadialTable {
    static constexpr int kSize = 2048;
    float scale = 0.0f;      // kSize / cut^2
    std::vector<float> a, b; // Grab: window * A, window * B; Twist / Scale: profile, window
    void build(const KelvinletKernel& k, float cut) {
        a.resize(kSize + 2);
        b.resize(kSize + 2);
        scale = float(kSize) / (cut * cut);
        for (int i = 0; i <= kSize + 1; ++i) {
            const float r2 = float(std::min(i, kSize)) / scale;
            const float w = window(std::sqrt(r2), cut);
            if (k.kind == KelvinletKind::Grab) {
                float A, B;
                k.grabTerms(r2, A, B);
                a[size_t(i)] = A * w;
                b[size_t(i)] = B * w;
            } else {
                a[size_t(i)] = k.profile(r2);
                b[size_t(i)] = w;
            }
        }
    }
    // r2 < cut^2.
    void at(float r2, float& outA, float& outB) const {
        const float x = r2 * scale;
        const int i = std::min(int(x), kSize);
        const float t = x - float(i);
        outA = a[size_t(i)] + (a[size_t(i) + 1] - a[size_t(i)]) * t;
        outB = b[size_t(i)] + (b[size_t(i) + 1] - b[size_t(i)]) * t;
    }
};

} // namespace

void Sculptor::kelvinletSingle(Mesh& mesh, BVH& bvh, const KelvinletKernel& k, const KelvinletStep& s) {
    auto clock = std::chrono::steady_clock::now();
    float amount = 0.0f, moveScale = 0.0f; // the step's size, and about how far it moves the centre (world)
    switch (k.kind) {
    case KelvinletKind::Grab: amount = moveScale = glm::length(s.delta); break;
    case KelvinletKind::Twist: amount = std::abs(s.angle), moveScale = amount * s.radius; break;
    case KelvinletKind::Scale: amount = std::abs(s.logScale), moveScale = amount * s.radius; break;
    }
    if (!(amount > 0.0f)) return;
    // Adaptive cutoff: where this step's displacement falls below epsilon = 2% of its largest (the window takes the
    // rest smoothly to 0), so a slow drag of tiny steps still moves the surface and a fast one doesn't drag the whole
    // mesh into the pass.
    float peak = 0.0f;
    for (int i = 0; i <= 32; ++i) peak = std::max(peak, k.envelope(1.5f * s.radius * float(i) / 32.0f));
    const float epsilon = std::max(0.02f * amount * peak, 1e-7f * s.radius);
    const float cut = k.cutoff(amount, epsilon, 3.0f * s.radius);
    if (!(cut > 0.0f)) return;
    // Grab: the load point travels from `center` to `center + travel` in `substeps` pieces, each a linear Kelvinlet
    // applied to where the vertex is by then (forward Euler on the flow), all in one pass: one query, one move,
    // one normal and BVH update however far the cursor jumped since the last frame.
    const bool grab = k.kind == KelvinletKind::Grab;
    const glm::vec3 travel = grab ? (glm::length(s.travel) > 0.0f ? s.travel : s.delta) : glm::vec3(0.0f);
    const int n = grab ? std::clamp(s.substeps, 1, 64) : 1;
    const glm::vec3 mid = s.center + 0.5f * travel;
    brushVertices(mesh, bvh, mid, cut + 0.5f * glm::length(travel), verts_);
    if (verts_.empty()) return;
    const auto& pos = mesh.positions;
    if (s.frontFacesOnly) {
        const auto& nrm = mesh.normals;
        std::erase_if(verts_, [&](uint32_t v) { return glm::dot(nrm[v], s.viewDir) > 0.0f; });
        if (verts_.empty()) return;
    }
    RadialTable table;
    table.build(k, cut);
    const float cut2 = cut * cut;
    const glm::vec3 dStep = s.delta / float(n), cStep = travel / float(n);
    scratch_.resize(verts_.size());
    poolFor(verts_.size(), [&](size_t b, size_t e) {
        for (size_t i = b; i < e; ++i) {
            const glm::vec3 x0 = pos[verts_[i]];
            glm::vec3 x = x0;
            switch (k.kind) {
            case KelvinletKind::Grab: {
                glm::vec3 c = s.center;
                for (int j = 0; j < n; ++j, c += cStep) {
                    const glm::vec3 r = x - c;
                    const float r2 = glm::dot(r, r);
                    if (r2 >= cut2) continue;
                    float A, B;
                    table.at(r2, A, B);
                    x += A * dStep + B * glm::dot(r, dStep) * r;
                }
                break;
            }
            case KelvinletKind::Twist: {
                const glm::vec3 r = x - s.center;
                const float r2 = glm::dot(r, r);
                if (r2 >= cut2) break;
                float t, w;
                table.at(r2, t, w);
                const float theta = s.angle * t, co = std::cos(theta), si = std::sin(theta);
                const glm::vec3 rot = r * co + glm::cross(s.axis, r) * si + s.axis * (glm::dot(s.axis, r) * (1.0f - co));
                x += (rot - r) * w;
                break;
            }
            case KelvinletKind::Scale: {
                const glm::vec3 r = x - s.center;
                const float r2 = glm::dot(r, r);
                if (r2 >= cut2 || r2 < 1e-24f) break;
                // The radial flow dr/dt = s t(r) r in midpoint steps, t from the table (clamped at the cutoff).
                const int steps = std::clamp(int(std::ceil(amount / 0.2f)), 1, 8);
                const float h = s.logScale / float(steps);
                const float len0 = std::sqrt(r2), lim = cut2 * 0.999f;
                float len = len0, t, w;
                for (int j = 0; j < steps; ++j) {
                    table.at(std::min(len * len, lim), t, w);
                    const float midLen = len * std::exp(0.5f * h * t);
                    table.at(std::min(midLen * midLen, lim), t, w);
                    len *= std::exp(h * t);
                }
                table.at(r2, t, w);
                x += r * ((len / len0 - 1.0f) * w);
                break;
            }
            }
            scratch_[i] = x - x0;
        }
    }, 2048);
    // Only what moves noticeably this step goes on (undo, normals, the BVH): the far part of the region moves
    // less than 0.2% of the step, which adds up to less than 0.2% of the whole drag.
    const float keep = 2e-3f * moveScale, keep2 = keep * keep;
    const size_t total = verts_.size();
    size_t kept = 0;
    for (size_t i = 0; i < total; ++i)
        if (glm::dot(scratch_[i], scratch_[i]) > keep2) {
            verts_[kept] = verts_[i];
            scratch_[kept++] = scratch_[i];
        }
    verts_.resize(kept);
    scratch_.resize(kept);
    if (verts_.empty()) return;
    displaceAll(mesh, verts_, scratch_);
    if (kept * 4 > total) {
        finishDab(mesh, bvh, clock); // much of the region moved: refit and renormal the queried leaves
        return;
    }
    timings.kernelMs += lapMs(clock);
    bvh.refit(mesh, verts_);
    timings.bvhMs += lapMs(clock);
    ++timings.dabs;
    timings.vertices += verts_.size();
    dabVerts_.insert(dabVerts_.end(), verts_.begin(), verts_.end());
    kelvinRingNormals_ = true; // updateDabNormals: the moved vertices' rings, not every queried leaf
}

void Sculptor::kelvinletStep(Mesh& mesh, BVH& bvh, BrushType type, const KelvinletStep& step, Symmetry sym) {
    if (!active_ || !isKelvinletBrush(type) || type == BrushType::DetailClone) return;
    const KelvinletKernel k = makeKelvinletKernel(kindOf(type), step.radius, kelvinlet, type == BrushType::KelvinSnake);
    dabVerts_.clear();
    dabLeaves_.clear();
    for (const glm::vec3& m : mirrors(sym)) {
        KelvinletStep ms = step;
        ms.center *= m;
        ms.delta *= m;
        ms.travel *= m;
        ms.viewDir *= m;
        // The rotation axis is a pseudovector: a mirror flips it once more (det = -1 for an odd mirror count).
        ms.axis = step.axis * m * (m.x * m.y * m.z);
        kelvinletSingle(mesh, bvh, k, ms);
    }
    if (kelvinRingNormals_) dabLeaves_.clear(); // (a mirror that moved little: rings for every mirror's vertices)
    kelvinRingNormals_ = false;
    updateDabNormals(mesh, bvh);
}

// A plain dab of a Kelvinlet brush (strokes along a path, the Dots stroke, scripts): Grab and Snake follow the
// stroke (a click without motion pulls the surface toward the viewer), Twist turns and Scale pinches by an amount
// set by Z Intensity per dab (Alt reverses).
void Sculptor::dabKelvinlet(Mesh& mesh, BVH& bvh, BrushType type, const DabParams& p, Symmetry sym) {
    if (type == BrushType::DetailClone) {
        dabDetailClone(mesh, bvh, p, sym);
        return;
    }
    const float s = std::clamp(p.strength, 0.0f, 1.0f) * std::clamp(p.pressure, 0.0f, 1.0f);
    const float sign = p.invert ? -1.0f : 1.0f;
    KelvinletStep step;
    step.center = p.center;
    step.radius = p.radius;
    step.viewDir = p.viewDir;
    step.frontFacesOnly = p.frontFacesOnly;
    step.axis = glm::length(p.viewDir) > 1e-6f ? -glm::normalize(p.viewDir) : glm::vec3(0.0f, 0.0f, 1.0f);
    switch (type) {
    case BrushType::KelvinGrab:
    case BrushType::KelvinSnake: {
        if (type == BrushType::KelvinSnake) {
            // The multi-scale Snake: the pull narrows as it travels, so drawn-out shapes taper to a tip.
            if (snakeStroke_ != strokeId_) {
                snakeStroke_ = strokeId_;
                snakeTravel_ = 0.0f;
            }
            snakeTravel_ += glm::length(p.strokeDelta);
            step.radius = p.radius * std::max(0.4f, 1.0f / (1.0f + snakeTravel_ / (4.0f * p.radius)));
        }
        glm::vec3 delta = p.strokeDelta * std::min(1.0f, 2.0f * s);
        if (glm::length(p.strokeDelta) < 1e-9f) delta = step.axis * (sign * 0.05f * s * p.radius);
        // Small pieces: each one is a linear Kelvinlet, so large moves integrate the flow instead of shearing.
        step.delta = delta;
        step.travel = glm::length(p.strokeDelta) < 1e-9f ? delta : p.strokeDelta;
        step.substeps = std::clamp(int(std::ceil(glm::length(step.travel) / (0.4f * step.radius))), 1, 16);
        kelvinletStep(mesh, bvh, type, step, sym);
        return;
    }
    case BrushType::KelvinTwist: step.angle = sign * s * 0.15f; break;
    case BrushType::KelvinScale: step.logScale = -sign * s * 0.06f; break; // pinches; Alt swells
    default: return;
    }
    kelvinletStep(mesh, bvh, type, step, sym);
}

// ---------------------------------------------------------------------------
// Detail Clone: the VDM brush's map stamped along the stroke, each stamp turned to follow the stroke
// ---------------------------------------------------------------------------

void Sculptor::cloneStamp(Mesh& mesh, BVH& bvh, const DabParams& p, Symmetry sym) {
    dabVerts_.clear();
    dabLeaves_.clear();
    for (const glm::vec3& m : mirrors(sym)) {
        DabParams mp = p;
        mp.center *= m;
        mp.viewDir *= m;
        mp.strokeDelta *= m;
        mp.alphaUp *= m;
        dabVdmSingle(mesh, bvh, mp, m.x * m.y * m.z);
    }
    updateDabNormals(mesh, bvh);
}

void Sculptor::dabDetailClone(Mesh& mesh, BVH& bvh, const DabParams& p, Symmetry sym) {
    if (!active_) return;
    if (clone_.stroke != strokeId_) {
        clone_ = CloneState{};
        clone_.stroke = strokeId_;
    }
    const float r = p.radius;
    const float spacing = 2.0f * r * std::clamp(kelvinlet.cloneSpacing, 0.25f, 3.0f);
    auto stampAt = [&](DabParams q, const glm::vec3& dir) {
        if (kelvinlet.cloneFollowStroke && glm::length(dir) > 1e-9f) q.alphaUp = glm::normalize(dir);
        cloneStamp(mesh, bvh, q, sym);
        clone_.last = q.center;
        clone_.stamped = true;
    };
    if (!clone_.stamped) {
        if (!clone_.pending) { // the first stamp waits for the stroke's direction
            clone_.pending = true;
            clone_.first = p;
            return;
        }
        const glm::vec3 dir = p.center - clone_.first.center;
        if (glm::length(dir) < 0.02f * r) { // dabs in place (scripts, a Dots click held still): stamp as turned
            stampAt(clone_.first, glm::vec3(0.0f));
            clone_.pending = false;
            return;
        }
        if (glm::length(dir) < 0.25f * r) return; // not far enough yet to say which way the stroke goes
        stampAt(clone_.first, dir);
        clone_.pending = false;
    }
    const glm::vec3 d = p.center - clone_.last;
    if (glm::length(d) >= spacing) stampAt(p, d);
}

void Sculptor::flushDetailClone(Mesh& mesh, BVH& bvh, Symmetry sym) {
    if (!active_ || clone_.stroke != strokeId_ || !clone_.pending || clone_.stamped) return;
    DabParams q = clone_.first;
    cloneStamp(mesh, bvh, q, sym);
    clone_.pending = false;
    clone_.stamped = true;
}

void registerKelvinletCommands(CommandRegistry& registry) {
    using T = ParamSpec::Type;
    registry.add({"kelvinlet_settings",
                  "Set the Kelvinlet brushes' material (Brush > Kelvinlet & Capture): Squash (Poisson's ratio: 0 "
                  "rubbery, 1 incompressible clay that bulges), Sharpness, and Detail Clone's spacing. Unset values stay.",
                  {{"squash", T::Number, "0-1 (default 0.5).", false},
                   {"sharpness", T::Number, "0-1: a tighter, cusped peak (default 0).", false},
                   {"clone_spacing", T::Number, "Detail Clone: stamp spacing in stamp widths, 0.25-3 (default 1).", false},
                   {"clone_follow", T::Bool, "Detail Clone: stamps turn with the stroke (default true).", false}},
                  [](Document& doc, const Args& a) -> CommandResult {
                      KelvinletSettings& k = doc.sculptor.kelvinlet;
                      k.squash = float(std::clamp(argNumber(a, "squash", k.squash), 0.0, 1.0));
                      k.sharpness = float(std::clamp(argNumber(a, "sharpness", k.sharpness), 0.0, 1.0));
                      k.cloneSpacing = float(std::clamp(argNumber(a, "clone_spacing", k.cloneSpacing), 0.25, 3.0));
                      k.cloneFollowStroke = argBool(a, "clone_follow", k.cloneFollowStroke);
                      char buf[160];
                      std::snprintf(buf, sizeof buf, "Kelvinlet: Squash %.0f (Poisson's ratio %.2f), Sharpness %.0f, clone spacing %.2f",
                                    k.squash * 100.0f, k.poisson(), k.sharpness * 100.0f, k.cloneSpacing);
                      return {true, buf};
                  }});
}

} // namespace sf
