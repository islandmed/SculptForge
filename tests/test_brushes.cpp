// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// v0.2 brushes (DamStandard, ClayBuildup, SnakeHook, Trim, Layer, Nudge),
// alphas, stroke helpers (lazy mouse, spray, spacing) and brush_set_alpha.
#include "TestHarness.h"

#include "core/BrushAlpha.h"
#include "core/BrushRecipe.h"
#include "core/Commands.h"
#include "core/Primitives.h"
#include "core/Sculpt.h"
#include "core/Stroke.h"

#include <algorithm>
#include <cmath>
#include <filesystem>

using namespace sf;

namespace {

constexpr Symmetry kNoSymmetry{false, false, false};

// 64 x 64 quads over [-1, 1]^2 in XZ, normal +Y; there is a vertex at the origin.
Document planeDoc() { return makeDoc(makePlane(64, 2.0f)); }

uint32_t nearest(const Mesh& m, const glm::vec3& p) {
    uint32_t best = 0;
    for (uint32_t v = 1; v < m.vertexCount(); ++v)
        if (glm::distance(m.positions[v], p) < glm::distance(m.positions[best], p)) best = v;
    return best;
}

DabParams dabAt(glm::vec3 center, float radius = 0.3f, float strength = 0.5f) {
    DabParams p;
    p.center = center;
    p.viewDir = glm::vec3(0, -1, 0);
    p.radius = radius;
    p.strength = strength;
    return p;
}

// One stroke of `count` identical dabs.
void stroke(Document& d, BrushType type, const DabParams& p, Symmetry sym = kNoSymmetry, int count = 1) {
    d.sculptor.beginStroke(d.mesh, brushName(type));
    for (int i = 0; i < count; ++i) d.sculptor.dab(d.mesh, d.bvh, type, p, sym);
    d.undo.push(d.sculptor.endStroke());
}

glm::vec3 moved(const Document& d, const std::vector<glm::vec3>& before, uint32_t v) {
    return d.mesh.positions[v] - before[v];
}

} // namespace

TEST(brush_names_cover_new_types) {
    CHECK(std::string(brushName(BrushType::DamStandard)) == "DamStandard");
    CHECK(std::string(brushName(BrushType::ClayBuildup)) == "ClayBuildup");
    CHECK(std::string(brushName(BrushType::SnakeHook)) == "SnakeHook");
    CHECK(std::string(brushName(BrushType::Trim)) == "Trim");
    CHECK(std::string(brushName(BrushType::Layer)) == "Layer");
    CHECK(std::string(brushName(BrushType::Nudge)) == "Nudge");
    for (int i = 0; i < int(BrushType::Count); ++i) CHECK(std::string(brushName(BrushType(i))) != "?");
    // Original brushes keep their slots (hotkeys 1-8, saved settings).
    CHECK(int(BrushType::Standard) == 0 && int(BrushType::Pinch) == 7 && int(BrushType::DamStandard) == 8);
}

TEST(round_alpha_leaves_existing_brushes_unchanged) {
    for (BrushType type : {BrushType::Standard, BrushType::Clay, BrushType::Crease, BrushType::Pinch}) {
        Document a = planeDoc(), b = planeDoc();
        DabParams p = dabAt(glm::vec3(0.1f, 0.0f, 0.0f));
        stroke(a, type, p);
        BrushAlpha round = BrushAlpha::round();
        p.alpha = &round;
        p.strokeDelta = glm::vec3(0.02f, 0, 0);
        p.alphaUp = glm::vec3(0.3f, 0.2f, 0.9f);
        stroke(b, type, p);
        CHECK(a.mesh.positions == b.mesh.positions);
    }
    // Standard: centre raised by r * 0.06 * strength.
    Document d = planeDoc();
    stroke(d, BrushType::Standard, dabAt(glm::vec3(0.0f)));
    CHECK(std::abs(d.mesh.positions[nearest(d.mesh, glm::vec3(0))].y - 0.3f * 0.06f * 0.5f) < 1e-5f);
}

// ZBrush Focal Shift: 0 leaves the falloff (and so every brush) exactly as before; below 0 the tip is broader
// and harder, above 0 a softer point; the cursor's inner ring sits where the falloff is one half.
TEST(focal_shift_shapes_the_falloff) {
    for (float d : {0.0f, 0.1f, 0.33f, 0.5f, 0.77f, 1.0f}) {
        const float t = std::clamp(1.0f - d, 0.0f, 1.0f);
        CHECK(brushFalloff(d, 1.0f, 0.0f) == t * t * (3.0f - 2.0f * t));
        CHECK(brushFalloff(d, 1.0f, -0.6f) >= brushFalloff(d, 1.0f));
        CHECK(brushFalloff(d, 1.0f, 0.6f) <= brushFalloff(d, 1.0f));
    }
    CHECK(brushFalloff(0.0f, 1.0f, 0.8f) == 1.0f);
    CHECK(brushFalloff(1.0f, 1.0f, -0.8f) == 0.0f);
    for (float f : {-1.0f, -0.4f, 0.0f, 0.5f, 1.0f})
        CHECK(std::abs(brushFalloff(focalRingFraction(f), 1.0f, f) - 0.5f) < 1e-4f);
    CHECK(std::abs(focalRingFraction(0.0f) - 0.5f) < 1e-6f);
    CHECK(focalRingFraction(-0.5f) > 0.5f && focalRingFraction(0.5f) < 0.5f);

    // On a brush: focal 0 is bit-identical to no focal at all; a soft point raises less away from the centre.
    Document a = planeDoc(), b = planeDoc(), c = planeDoc();
    DabParams p = dabAt(glm::vec3(0.0f));
    stroke(a, BrushType::Standard, p);
    p.focal = 0.0f;
    stroke(b, BrushType::Standard, p);
    CHECK(a.mesh.positions == b.mesh.positions);
    p.focal = 0.7f;
    stroke(c, BrushType::Standard, p);
    const uint32_t off = nearest(c.mesh, glm::vec3(0.15f, 0.0f, 0.0f));
    CHECK(c.mesh.positions[off].y < a.mesh.positions[off].y);
}

TEST(dam_standard_carves_sharp_pinched_groove) {
    Document d = planeDoc();
    const auto before = d.mesh.positions;
    DabParams p = dabAt(glm::vec3(0.0f));
    p.strokeDelta = glm::vec3(0, 0, 0.05f); // stroke runs along +z
    stroke(d, BrushType::DamStandard, p);
    const uint32_t c = nearest(d.mesh, glm::vec3(0)), side = nearest(d.mesh, glm::vec3(0.15f, 0, 0));
    const uint32_t along = nearest(d.mesh, glm::vec3(0, 0, 0.15f));
    CHECK(moved(d, before, c).y < -0.005f);                         // carves inward
    const float sideDepth = -moved(d, before, side).y, centreDepth = -moved(d, before, c).y;
    CHECK(sideDepth >= 0.0f && sideDepth < 0.2f * centreDepth);     // sharp V (Standard would be ~0.5)
    CHECK(moved(d, before, side).x < -0.005f);                      // sides pinch toward the line
    CHECK(std::abs(moved(d, before, along).z) < 1e-5f);             // ...but not along it
    CHECK(std::abs(moved(d, before, along).x) < 1e-5f);

    Document r = planeDoc();
    p.invert = true;
    stroke(r, BrushType::DamStandard, p);
    CHECK(r.mesh.positions[c].y > 0.005f); // Alt raises a sharp ridge
}

TEST(clay_buildup_is_square_and_flat_topped) {
    Document d = planeDoc(), clay = planeDoc();
    const DabParams p = dabAt(glm::vec3(0.0f));
    stroke(d, BrushType::ClayBuildup, p);
    stroke(clay, BrushType::Clay, p);
    const uint32_t c = nearest(d.mesh, glm::vec3(0)), diag = nearest(d.mesh, glm::vec3(0.2f, 0, 0.2f));
    const uint32_t mid = nearest(d.mesh, glm::vec3(0.1f, 0, 0));
    const float hc = d.mesh.positions[c].y;
    CHECK(hc > 0.005f);
    CHECK(std::abs(d.mesh.positions[mid].y - hc) < 0.05f * hc); // flat top
    CHECK(d.mesh.positions[diag].y > 0.6f * hc);                // square: corners filled
    CHECK(clay.mesh.positions[diag].y < 0.3f * clay.mesh.positions[c].y); // round Clay is not
    // Builds up over strokes.
    stroke(d, BrushType::ClayBuildup, p);
    CHECK(d.mesh.positions[c].y > 1.5f * hc);
    // Nothing outside the rounded square moves.
    CHECK(d.mesh.positions[nearest(d.mesh, glm::vec3(0.45f, 0, 0))].y == 0.0f);
}

TEST(snake_hook_pulls_and_tapers) {
    Document d = planeDoc();
    const auto before = d.mesh.positions;
    DabParams p = dabAt(glm::vec3(0.0f));
    p.strokeDelta = glm::vec3(0, 0.05f, 0);
    stroke(d, BrushType::SnakeHook, p);
    const uint32_t c = nearest(d.mesh, glm::vec3(0)), side = nearest(d.mesh, glm::vec3(0.15f, 0, 0));
    CHECK(std::abs(moved(d, before, c).y - 0.05f) < 1e-4f); // the tip follows the stroke
    CHECK(moved(d, before, side).y > 0.0f && moved(d, before, side).y < 0.05f);
    CHECK(moved(d, before, side).x < 0.0f);                 // sides taper inward
    CHECK(d.mesh.positions[nearest(d.mesh, glm::vec3(0.5f, 0, 0))] == before[nearest(d.mesh, glm::vec3(0.5f, 0, 0))]);

    // Re-centred on the tip every step (as the app does), it pulls out a horn.
    Document h = planeDoc();
    h.sculptor.beginStroke(h.mesh, "SnakeHook");
    glm::vec3 tip(0.0f);
    for (int i = 0; i < 40; ++i) {
        DabParams s = dabAt(tip);
        s.strokeDelta = glm::vec3(0, 0.025f, 0);
        h.sculptor.dab(h.mesh, h.bvh, BrushType::SnakeHook, s, kNoSymmetry);
        tip += s.strokeDelta;
    }
    h.undo.push(h.sculptor.endStroke());
    float top = 0.0f, widthHigh = 0.0f, widthLow = 0.0f;
    for (const glm::vec3& v : h.mesh.positions) {
        top = std::max(top, v.y);
        const float r = std::sqrt(v.x * v.x + v.z * v.z);
        if (v.y > 0.6f) widthHigh = std::max(widthHigh, r);
        if (v.y > 0.1f && v.y < 0.3f) widthLow = std::max(widthLow, r);
    }
    CHECK(top > 0.8f);
    CHECK(widthHigh < widthLow); // tapers toward the tip
    CHECK(widthLow < 0.3f);
}

TEST(trim_cuts_flat_and_never_raises) {
    Document d = makeDoc(makeIcosphere(4));
    const auto before = d.mesh.positions;
    auto spreadNearAxis = [](const std::vector<glm::vec3>& pos) {
        float lo = 1e9f, hi = -1e9f;
        for (const glm::vec3& v : pos)
            if (v.y > 0 && v.x * v.x + v.z * v.z < 0.15f * 0.15f) {
                lo = std::min(lo, v.y);
                hi = std::max(hi, v.y);
            }
        return hi - lo;
    };
    stroke(d, BrushType::Trim, dabAt(glm::vec3(0, 1, 0), 0.4f, 1.0f), kNoSymmetry, 5);
    const uint32_t top = nearest(d.mesh, glm::vec3(0, 2, 0));
    CHECK(d.mesh.positions[top].y < before[top].y - 0.005f); // cut down
    CHECK(spreadNearAxis(d.mesh.positions) < 0.3f * spreadNearAxis(before)); // to a flat plane
    bool raised = false;
    for (uint32_t v = 0; v < d.mesh.vertexCount(); ++v) raised |= glm::length(d.mesh.positions[v]) > glm::length(before[v]) + 1e-6f;
    CHECK(!raised);
}

TEST(layer_brush_raises_constant_height_without_stacking) {
    Document d = planeDoc();
    const DabParams p = dabAt(glm::vec3(0.0f));
    const float height = 0.3f * 0.08f * 0.5f;
    stroke(d, BrushType::Layer, p, kNoSymmetry, 10);
    const uint32_t c = nearest(d.mesh, glm::vec3(0));
    CHECK(std::abs(d.mesh.positions[c].y - height) < 1e-5f); // ten dabs, one height

    // Overlapping dabs along a line: the result is the max, not the sum.
    Document l = planeDoc();
    l.sculptor.beginStroke(l.mesh, "Layer");
    for (float x = -0.2f; x <= 0.2001f; x += 0.05f)
        l.sculptor.dab(l.mesh, l.bvh, BrushType::Layer, dabAt(glm::vec3(x, 0, 0)), kNoSymmetry);
    l.undo.push(l.sculptor.endStroke());
    float maxY = 0.0f;
    for (const glm::vec3& v : l.mesh.positions) maxY = std::max(maxY, v.y);
    CHECK(maxY <= height + 1e-5f && maxY > 0.99f * height);

    // A new stroke adds another layer.
    stroke(d, BrushType::Layer, p);
    CHECK(std::abs(d.mesh.positions[c].y - 2.0f * height) < 0.02f * height); // along the (bumped) area normal
    // Undo restores the stroke exactly.
    d.undo.undo(d.mesh, d.bvh);
    CHECK(std::abs(d.mesh.positions[c].y - height) < 1e-5f);
}

TEST(nudge_slides_along_the_surface) {
    Document d = planeDoc();
    const auto before = d.mesh.positions;
    DabParams p = dabAt(glm::vec3(0.0f));
    p.strokeDelta = glm::vec3(0.05f, 0.04f, 0.0f); // the normal component is dropped
    stroke(d, BrushType::Nudge, p);
    const uint32_t c = nearest(d.mesh, glm::vec3(0));
    CHECK(moved(d, before, c).x > 0.02f);
    float maxDy = 0.0f;
    for (uint32_t v = 0; v < d.mesh.vertexCount(); ++v) maxDy = std::max(maxDy, std::abs(moved(d, before, v).y));
    CHECK(maxDy < 1e-6f);
    const uint32_t far = nearest(d.mesh, glm::vec3(0.5f, 0, 0));
    CHECK(d.mesh.positions[far] == before[far]);
}

TEST(new_brushes_respect_mask_and_symmetry) {
    const BrushType types[] = {BrushType::DamStandard, BrushType::ClayBuildup, BrushType::SnakeHook,
                               BrushType::Trim,        BrushType::Layer,       BrushType::Nudge};
    for (BrushType type : types) {
        DabParams p = dabAt(glm::vec3(0.5f, 0, 0), 0.3f, 1.0f);
        p.strokeDelta = glm::vec3(0.03f, 0.03f, 0.03f);
        if (type == BrushType::Trim) p.invert = true; // fill: a flat plane has nothing to cut

        // Symmetry X: the mirrored dab mirrors the displacement.
        Document s = planeDoc();
        const auto before = s.mesh.positions;
        stroke(s, type, p, Symmetry{true, false, false});
        const uint32_t right = nearest(s.mesh, glm::vec3(0.55f, 0, 0.05f));
        const uint32_t left = nearest(s.mesh, glm::vec3(-0.55f, 0, 0.05f));
        const glm::vec3 dr = moved(s, before, right), dl = moved(s, before, left);
        CHECK(glm::length(dr) > 1e-4f);
        CHECK(glm::length(dr - dl * glm::vec3(-1, 1, 1)) < 1e-5f);

        // Mask the left half: the mirrored dab leaves it alone.
        Document m = planeDoc();
        m.mesh.ensureMask();
        for (uint32_t v = 0; v < m.mesh.vertexCount(); ++v) m.mesh.mask[v] = m.mesh.positions[v].x < 0 ? 1.0f : 0.0f;
        stroke(m, type, p, Symmetry{true, false, false});
        bool leftMoved = false;
        for (uint32_t v = 0; v < m.mesh.vertexCount(); ++v)
            if (before[v].x < 0) leftMoved |= m.mesh.positions[v] != before[v];
        CHECK(!leftMoved);
        CHECK(glm::length(moved(m, before, right)) > 1e-4f);
    }
}

TEST(alpha_sampling) {
    const BrushAlpha round = BrushAlpha::round();
    CHECK(round.empty() && round.name == "Round");
    CHECK(std::abs(round.sample(0, 0) - 1.0f) < 1e-6f && round.sample(1.0f, 0) == 0.0f);
    CHECK(std::abs(round.sample(0.5f, 0) - 0.5f) < 1e-6f && round.sample(0, 1.5f) == 0.0f);

    const BrushAlpha square = BrushAlpha::square();
    CHECK(square.width == 128 && square.values.size() == 128u * 128u);
    CHECK(square.sample(0.7f, 0.7f) > 0.99f && square.sample(0.0f, 0.0f) > 0.99f);
    CHECK(square.sample(0.96f, 0.0f) < 0.5f && square.sample(1.2f, 0.0f) == 0.0f);

    const BrushAlpha noise = BrushAlpha::noise(), noise2 = BrushAlpha::noise();
    CHECK(noise.values == noise2.values); // deterministic
    float lo = 1.0f, hi = 0.0f;
    for (float v : noise.values) {
        lo = std::min(lo, v);
        hi = std::max(hi, v);
    }
    CHECK(lo >= 0.0f && hi <= 1.0f && hi - lo > 0.5f);

    // Image alphas: luminance x alpha, bilinear, row 0 at the top.
    Image img;
    img.width = 2;
    img.height = 2;
    img.rgba = {255, 255, 255, 255, 0, 0, 0, 255,   // top: white, black
                255, 255, 255, 0,   0, 0, 0, 255};  // bottom: transparent white, black
    const BrushAlpha a = BrushAlpha::fromImage(img, "test");
    CHECK(a.name == "test" && a.width == 2 && a.height == 2);
    CHECK(std::abs(a.sample(-0.9f, 0.9f) - 1.0f) < 1e-5f); // top-left
    CHECK(a.sample(0.9f, 0.9f) < 1e-5f);                  // top-right
    CHECK(a.sample(-0.9f, -0.9f) < 1e-5f);                // transparent counts as black
    CHECK(std::abs(a.sample(0.0f, 0.9f) - 0.5f) < 1e-5f); // bilinear between pixel centres
    const BrushAlpha inv = BrushAlpha::fromImage(img, "inv", true);
    CHECK(inv.sample(-0.9f, 0.9f) < 1e-5f && std::abs(inv.sample(0.9f, 0.9f) - 1.0f) < 1e-5f);
    CHECK(BrushAlpha::fromImage(Image{}, "x").empty());

    BrushAlpha out;
    CHECK(BrushAlpha::builtin("SQUARE", out) && out.name == "Square");
    CHECK(BrushAlpha::builtin("none", out) && out.empty());
    CHECK(!BrushAlpha::builtin("star", out));
}

TEST(alpha_shapes_and_orients_dabs) {
    // Square alpha fills the corners a round dab leaves untouched.
    Document round = planeDoc(), sq = planeDoc();
    stroke(round, BrushType::Standard, dabAt(glm::vec3(0)));
    BrushAlpha square = BrushAlpha::square();
    DabParams p = dabAt(glm::vec3(0));
    p.alpha = &square;
    p.focal = -1.0f; // (Focal Shift -100: the alpha shows hard to its edges; at 0 it rounds the corners, #60)
    stroke(sq, BrushType::Standard, p);
    p.focal = 0.0f;
    const uint32_t c = nearest(sq.mesh, glm::vec3(0)), diag = nearest(sq.mesh, glm::vec3(0.25f, 0, 0.25f));
    CHECK(sq.mesh.positions[diag].y > 0.9f * sq.mesh.positions[c].y);
    CHECK(round.mesh.positions[diag].y == 0.0f);

    // Half-white image: only the white side moves; alphaUp sets the orientation.
    Image img;
    img.width = 8;
    img.height = 8;
    img.rgba.resize(8 * 8 * 4, 255);
    for (int y = 0; y < 8; ++y)
        for (int x = 4; x < 8; ++x)
            for (int k = 0; k < 3; ++k) img.rgba[size_t((y * 8 + x) * 4 + k)] = 0;
    BrushAlpha half = BrushAlpha::fromImage(img, "half");
    Document h = planeDoc();
    p.alpha = &half;
    p.alphaUp = glm::vec3(0, 0, -1); // seen from above (+Y), image right = +x
    stroke(h, BrushType::Standard, p);
    CHECK(h.mesh.positions[nearest(h.mesh, glm::vec3(-0.12f, 0, 0))].y > 0.005f);
    CHECK(h.mesh.positions[nearest(h.mesh, glm::vec3(0.12f, 0, 0))].y == 0.0f);
    // Rotated 180 degrees, the other side moves.
    Document h2 = planeDoc();
    p.alphaUp = glm::vec3(0, 0, 1);
    stroke(h2, BrushType::Standard, p);
    CHECK(h2.mesh.positions[nearest(h2.mesh, glm::vec3(-0.12f, 0, 0))].y == 0.0f);
    CHECK(h2.mesh.positions[nearest(h2.mesh, glm::vec3(0.12f, 0, 0))].y > 0.005f);

    // Recipe brushes are modulated too.
    Document r = planeDoc();
    BrushRecipe recipe;
    recipe.name = "test";
    p.alphaUp = glm::vec3(0, 0, -1);
    r.sculptor.beginStroke(r.mesh, "test");
    r.sculptor.dabRecipe(r.mesh, r.bvh, recipe, p, kNoSymmetry);
    r.undo.push(r.sculptor.endStroke());
    CHECK(r.mesh.positions[nearest(r.mesh, glm::vec3(-0.12f, 0, 0))].y > 0.001f);
    CHECK(r.mesh.positions[nearest(r.mesh, glm::vec3(0.12f, 0, 0))].y == 0.0f);
}

// #60: Focal Shift shapes alpha brushes as in ZBrush. The alpha is multiplied by a round falloff reaching the
// stamp's corners: -100 shows the whole square, 0 rounds its corners into a soft mound, +100 leaves a small point.
TEST(focal_shift_shapes_alpha_dabs) {
    CHECK(alphaFalloff(0.0f, 1.0f, 0.0f) == 1.0f);
    for (float d : {0.0f, 0.5f, 1.0f, 1.3f, 1.414f}) CHECK(alphaFalloff(d, 1.0f, -1.0f) == 1.0f);
    CHECK(alphaFalloff(1.2f, 1.0f, 0.0f) < 0.1f && alphaFalloff(1.5f, 1.0f, 0.0f) == 0.0f);
    CHECK(alphaFalloff(0.5f, 1.0f, 1.0f) < 0.5f * alphaFalloff(0.5f, 1.0f, 0.0f));

    const BrushAlpha square = BrushAlpha::square();
    CHECK(square.stamp(0.7f, 0.7f, -1.0f) == square.sample(0.7f, 0.7f));
    CHECK(square.stamp(0.7f, 0.7f, 0.0f) < 0.5f * square.stamp(0.7f, 0.0f, 0.0f)); // rounded corners
    CHECK(square.stamp(1.2f, 0.0f, -1.0f) == 0.0f);
    const BrushAlpha round = BrushAlpha::round(); // (empty: the plain brush falloff, Focal Shift included)
    CHECK(round.stamp(0.4f, 0.0f, 0.5f) == brushFalloff(0.4f, 1.0f, 0.5f) && round.stamp(1.1f, 0.0f, -1.0f) == 0.0f);

    // A square-alpha dab: how much moves (footprint) and how much of the corner survives.
    struct Footprint { int moved = 0; float corner = 0.0f, centre = 0.0f; };
    auto dabWith = [&](BrushType type, float focal) {
        Document d = planeDoc();
        DabParams p = dabAt(glm::vec3(0));
        p.alpha = &square;
        p.alphaUp = glm::vec3(0, 0, -1);
        p.focal = focal;
        stroke(d, type, p);
        Footprint f;
        for (const glm::vec3& v : d.mesh.positions) f.moved += std::abs(v.y) > 1e-4f;
        f.corner = d.mesh.positions[nearest(d.mesh, glm::vec3(0.22f, 0, 0.22f))].y;
        f.centre = d.mesh.positions[nearest(d.mesh, glm::vec3(0))].y;
        return f;
    };
    for (BrushType type : {BrushType::Standard, BrushType::ClayBuildup}) {
        const Footprint hard = dabWith(type, -1.0f), soft = dabWith(type, 0.0f), point = dabWith(type, 1.0f);
        CHECK(hard.centre > 0.0f && soft.centre > 0.0f && point.centre > 0.0f);
        CHECK(hard.moved > soft.moved && soft.moved > point.moved);
        CHECK(hard.corner > 0.8f * hard.centre);  // the full square
        CHECK(soft.corner < 0.5f * soft.centre);  // corners rounded
        CHECK(point.corner < 0.05f * point.centre); // almost gone
        CHECK(point.moved < hard.moved / 2);
    }
}

// #60: Focal Shift reaches the built-in square ClayBuildup tip and custom (recipe) brushes; 0 keeps their own tip.
TEST(focal_shift_reaches_clay_buildup_and_recipes) {
    auto height = [&](float focal, bool recipe, const glm::vec3& at) {
        Document d = planeDoc();
        DabParams p = dabAt(glm::vec3(0));
        p.focal = focal;
        if (recipe) {
            BrushRecipe r;
            r.name = "test";
            d.sculptor.beginStroke(d.mesh, "test");
            d.sculptor.dabRecipe(d.mesh, d.bvh, r, p, kNoSymmetry);
            d.undo.push(d.sculptor.endStroke());
        } else {
            stroke(d, BrushType::ClayBuildup, p);
        }
        const uint32_t v = nearest(d.mesh, at);
        return d.mesh.positions[v].y / std::max(d.mesh.positions[nearest(d.mesh, glm::vec3(0))].y, 1e-9f);
    };
    for (bool recipe : {false, true}) {
        const glm::vec3 at = recipe ? glm::vec3(0.15f, 0, 0) : glm::vec3(0.27f, 0, 0); // (on the tip's soft edge)
        const float hard = height(-0.8f, recipe, at), plain = height(0.0f, recipe, at), soft = height(0.8f, recipe, at);
        CHECK(hard > plain + 0.02f && plain > soft + 0.02f);
    }
    // 0 is bit-identical to no Focal Shift at all (the brush's own tip).
    Document a = planeDoc(), b = planeDoc();
    DabParams p = dabAt(glm::vec3(0));
    stroke(a, BrushType::ClayBuildup, p);
    p.focal = 0.0f;
    stroke(b, BrushType::ClayBuildup, p);
    CHECK(a.mesh.positions == b.mesh.positions);
}

// #75: custom and Community (recipe) brushes show the whole alpha at Focal Shift -100, as the built-ins do (#60):
// a DragRect-style single stamp of a corner-reaching alpha with Clay Slab displaces the square's corners, not
// only the round tip inside the brush circle. At 0 the preset keeps its own round tip.
TEST(custom_brush_full_alpha_at_focal_minus_100) {
    BrushRecipe slab;
    bool found = false;
    for (const BrushRecipe& r : builtinBrushRecipes())
        if (r.name == "Clay Slab") slab = r, found = true;
    CHECK(found);
    BrushAlpha full;
    full.name = "Full";
    full.width = full.height = 8;
    full.values.assign(64, 1.0f); // (white to its corners)
    BrushAlpha bark;
    CHECK(BrushAlpha::builtin("bark", bark));

    const float radius = 0.3f;
    const glm::vec3 corner(0.26f, 0, 0.26f); // (0.37 out: past the brush circle, inside the stamp's square)
    auto stampWith = [&](const BrushAlpha& alpha, float focal) {
        Document d = planeDoc();
        DabParams p = dabAt(glm::vec3(0), radius);
        p.alpha = &alpha;
        p.alphaUp = glm::vec3(0, 0, -1);
        p.focal = focal;
        d.sculptor.beginStroke(d.mesh, slab.name);
        d.sculptor.dabRecipe(d.mesh, d.bvh, slab, p, kNoSymmetry);
        d.undo.push(d.sculptor.endStroke());
        return d;
    };
    // Movement summed over the stamp's four corner regions (outside the circle, inside the square).
    auto cornerSum = [&](const Document& d) {
        float sum = 0.0f;
        for (const glm::vec3& v : d.mesh.positions) {
            const float cheb = std::max(std::abs(v.x), std::abs(v.z));
            if (glm::length(glm::vec2(v.x, v.z)) > radius * 1.05f && cheb < radius * 0.98f) sum += std::abs(v.y);
        }
        return sum;
    };

    const Document hard = stampWith(full, -1.0f), soft = stampWith(full, 0.0f);
    const float centre = hard.mesh.positions[nearest(hard.mesh, glm::vec3(0))].y;
    CHECK(centre > 0.0f);
    for (glm::vec3 c : {corner, glm::vec3(-corner.x, 0, corner.z), glm::vec3(corner.x, 0, -corner.z), -corner})
        CHECK(hard.mesh.positions[nearest(hard.mesh, c)].y > 0.5f * centre); // the corner texels are displaced
    CHECK(soft.mesh.positions[nearest(soft.mesh, corner)].y == 0.0f); // (0: the preset's own round tip)
    CHECK(cornerSum(soft) == 0.0f && cornerSum(hard) > 0.0f);

    // Bark reaches its corners too; at -100 they move, at 0 they don't.
    CHECK(cornerSum(stampWith(bark, -1.0f)) > 0.0f && cornerSum(stampWith(bark, 0.0f)) == 0.0f);
}

TEST(brush_set_alpha_command) {
    CommandRegistry reg;
    Document d = planeDoc();
    CHECK(reg.find("brush_set_alpha") != nullptr);
    CHECK(d.brushAlpha.empty());
    CHECK(reg.execute(d, "brush_set_alpha", {{"name", std::string("square")}}).ok);
    CHECK(d.brushAlpha.name == "Square" && !d.brushAlpha.empty());
    CHECK(!reg.execute(d, "brush_set_alpha", {{"name", std::string("star")}}).ok);
    CHECK(d.brushAlpha.name == "Square");

    Image img;
    img.width = 16;
    img.height = 8;
    img.rgba.assign(16 * 8 * 4, 200);
    const std::string path = tempPath("sf_test_alpha.png");
    std::string err;
    CHECK(img.savePng(path, err));
    CHECK(reg.execute(d, "brush_set_alpha", {{"path", path}}).ok);
    CHECK(d.brushAlpha.name == "sf_test_alpha" && d.brushAlpha.width == 16 && d.brushAlpha.height == 8);
    CHECK(!reg.execute(d, "brush_set_alpha", {{"path", tempPath("sf_no_such_alpha.png")}}).ok);
    CHECK(reg.execute(d, "brush_set_alpha", {}).ok && d.brushAlpha.empty()); // back to round
    std::filesystem::remove(path);
}

TEST(restore_stroke_reverts_for_drag_rect) {
    Document d = planeDoc();
    const auto before = d.mesh.positions;
    d.sculptor.beginStroke(d.mesh, "DragRect");
    d.sculptor.dab(d.mesh, d.bvh, BrushType::Standard, dabAt(glm::vec3(0), 0.2f), kNoSymmetry);
    d.sculptor.restoreStroke(d.mesh, d.bvh);
    CHECK(d.mesh.positions == before);
    // Re-stamp bigger: only the final stamp remains, and undo goes back to the start.
    d.sculptor.dab(d.mesh, d.bvh, BrushType::Layer, dabAt(glm::vec3(0), 0.4f), kNoSymmetry);
    d.sculptor.restoreStroke(d.mesh, d.bvh);
    d.sculptor.dab(d.mesh, d.bvh, BrushType::Layer, dabAt(glm::vec3(0), 0.4f), kNoSymmetry);
    const uint32_t c = nearest(d.mesh, glm::vec3(0));
    CHECK(std::abs(d.mesh.positions[c].y - 0.4f * 0.08f * 0.5f) < 1e-5f); // Layer restarts after a restore
    d.undo.push(d.sculptor.endStroke());
    d.undo.undo(d.mesh, d.bvh);
    CHECK(d.mesh.positions == before);

    // With sculpt layers the restore goes through the layer offsets.
    Document l = planeDoc();
    l.mesh.addLayer("L");
    l.sculptor.beginStroke(l.mesh, "DragRect", 0);
    l.sculptor.dab(l.mesh, l.bvh, BrushType::Standard, dabAt(glm::vec3(0)), kNoSymmetry);
    CHECK(l.mesh.positions != before);
    l.sculptor.restoreStroke(l.mesh, l.bvh);
    l.sculptor.endStroke();
    bool same = true;
    for (uint32_t v = 0; v < l.mesh.vertexCount(); ++v) same &= glm::distance(l.mesh.positions[v], before[v]) < 1e-6f;
    CHECK(same);
}

TEST(lazy_mouse_smooths_jitter) {
    LazyMouse lazy;
    lazy.radius = 20.0f;
    lazy.reset(glm::vec2(0.0f));
    CHECK(lazy.follow(glm::vec2(10, 5)) == glm::vec2(0.0f)); // inside the string: no movement
    // A jittery horizontal line: +-6 px of vertical noise.
    float inLo = 1e9f, inHi = -1e9f, outLo = 1e9f, outHi = -1e9f;
    glm::vec2 cursor(0.0f), point(0.0f);
    for (int i = 0; i < 300; ++i) {
        cursor = glm::vec2(float(i) * 2.0f, (i % 2 ? 6.0f : -6.0f));
        point = lazy.follow(cursor);
        if (i < 100) continue;
        inLo = std::min(inLo, cursor.y);
        inHi = std::max(inHi, cursor.y);
        outLo = std::min(outLo, point.y);
        outHi = std::max(outHi, point.y);
    }
    CHECK(outHi - outLo < 0.25f * (inHi - inLo));
    CHECK(std::abs(glm::distance(point, cursor) - 20.0f) < 1e-3f); // trails by the radius
    CHECK(point.x < cursor.x);
}

TEST(spray_and_spacing_are_deterministic) {
    Spray a(7), b(7), c(8);
    bool same = true, differs = false, bounded = true;
    for (int i = 0; i < 200; ++i) {
        const SprayDab x = a.next(50.0f, 0.5f), y = b.next(50.0f, 0.5f), z = c.next(50.0f, 0.5f);
        same &= x.offset == y.offset && x.scale == y.scale && x.strength == y.strength && x.angle == y.angle;
        differs |= x.offset != z.offset;
        bounded &= glm::length(x.offset) <= 25.0f + 1e-4f && x.scale >= 0.5f && x.scale <= 1.0f &&
                   x.strength >= 0.5f && x.strength <= 1.0f;
    }
    CHECK(same && differs && bounded);
    Spray zero(0); // a zero seed still produces numbers
    CHECK(zero.uniform() > 0.0f);

    StrokeSpacer spacer;
    std::vector<glm::vec2> pts;
    spacer.reset(glm::vec2(0.0f));
    spacer.moveTo(glm::vec2(100, 0), 10.0f, pts);
    CHECK(pts.size() == 10 && std::abs(pts[0].x - 10.0f) < 1e-4f && std::abs(pts.back().x - 100.0f) < 1e-3f);
    pts.clear();
    spacer.moveTo(glm::vec2(105, 0), 10.0f, pts);
    CHECK(pts.empty());
    spacer.moveTo(glm::vec2(115, 0), 10.0f, pts);
    CHECK(pts.size() == 1 && std::abs(pts[0].x - 110.0f) < 1e-3f);
    CHECK(std::string(strokeTypeName(StrokeType::LazyMouse)) == "Lazy Stroke");
}

TEST(alpha_generated_builtins) {
    // #43: twelve patterns made in code (no image files), after Round, Square and Noise.
    const auto& all = BrushAlpha::builtins();
    CHECK(all.size() == 15);
    CHECK(all[0].empty() && all[0].name == "Round" && all[1].name == "Square" && all[2].name == "Noise");
    for (size_t i = 3; i < all.size(); ++i) {
        const BrushAlpha& a = all[i];
        CHECK(a.width == 128 && a.height == 128 && a.values.size() == 128u * 128u);
        float lo = 1.0f, hi = 0.0f, border = 0.0f;
        double sum = 0.0;
        for (int y = 0; y < a.height; ++y)
            for (int x = 0; x < a.width; ++x) {
                const float v = a.values[size_t(y) * size_t(a.width) + size_t(x)];
                lo = std::min(lo, v);
                hi = std::max(hi, v);
                sum += v;
                if (x == 0 || y == 0 || x == a.width - 1 || y == a.height - 1) border = std::max(border, v);
            }
        const double mean = sum / double(a.values.size());
        CHECK(lo >= 0.0f && hi <= 1.0f && hi > 0.5f); // a real stamp, in range
        CHECK(mean > 0.02 && mean < 0.8);             // a pattern, not a blank or a flat plateau
        CHECK(border < 0.05f);                        // fades out at the edge, so dabs leave no square
        if (border >= 0.05f || mean <= 0.02 || mean >= 0.8)
            std::printf("  %s: mean %.3f border %.3f\n", a.name.c_str(), mean, double(border));
        CHECK(!BrushAlpha::builtinHelp(a.name).empty());
        BrushAlpha byName;
        CHECK(BrushAlpha::builtin(a.name, byName) && byName.values == a.values);
        for (size_t j = 3; j < i; ++j) CHECK(all[j].values != a.values); // all different
    }
    // Names work in any case, with spaces, '_' or '-'.
    BrushAlpha out;
    CHECK(BrushAlpha::builtin("fabric_weave", out) && out.name == "Fabric Weave");
    CHECK(BrushAlpha::builtin("ROCK-STRATA", out) && out.name == "Rock Strata");
    CHECK(BrushAlpha::builtin("skinfolds", out) && out.name == "Skin Folds");
    // And through the command.
    Document d = planeDoc();
    CommandRegistry reg;
    CHECK(reg.execute(d, "brush_set_alpha", {{"name", std::string("hair strands")}}).ok);
    CHECK(d.brushAlpha.name == "Hair Strands");
    CommandResult bad = reg.execute(d, "brush_set_alpha", {{"name", std::string("star")}});
    CHECK(!bad.ok && bad.message.find("cracks") != std::string::npos);
}
