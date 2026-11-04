// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/BrushPreview.h"

#include "app/AssetLibrary.h"
#include "core/Document.h"
#include "core/PathStroke.h"
#include "core/Primitives.h"
#include "core/Sculpt.h"

#include <algorithm>
#include <cmath>

namespace sf {

std::unique_ptr<Mesh> sceneMesh(Document& doc) {
    auto out = std::make_unique<Mesh>();
    bool colors = false;
    for (int i = 0; i < doc.subtoolCount(); ++i)
        colors |= doc.subtools[size_t(i)].visible &&
                  doc.subtoolMesh(i).colors.size() == doc.subtoolMesh(i).positions.size() &&
                  !doc.subtoolMesh(i).colors.empty();
    for (int i = 0; i < doc.subtoolCount(); ++i) {
        if (!doc.subtools[size_t(i)].visible) continue;
        const Mesh& m = doc.subtoolMesh(i);
        const uint32_t base = uint32_t(out->positions.size());
        out->positions.insert(out->positions.end(), m.positions.begin(), m.positions.end());
        if (colors) {
            if (m.colors.size() == m.positions.size())
                out->colors.insert(out->colors.end(), m.colors.begin(), m.colors.end());
            else
                out->colors.insert(out->colors.end(), m.positions.size(), glm::vec3(1.0f));
        }
        const bool hidden = m.hidden.size() == m.triangles.size();
        for (size_t t = 0; t < m.triangles.size(); ++t)
            if (!hidden || !m.hidden[t]) out->triangles.push_back(m.triangles[t] + glm::uvec3(base));
    }
    if (out->triangles.empty()) return nullptr;
    out->rebuildTopology();
    out->recomputeAllNormals();
    return out;
}


namespace {

// An S across the front of the ball, as stroke points in front of it (strokes look along -z).
std::vector<glm::vec3> frontCurve(float width = 1.0f) {
    std::vector<glm::vec3> pts;
    for (int i = 0; i <= 8; ++i) {
        const float u = float(i) / 8.0f;
        pts.emplace_back((-0.56f + 1.12f * u) * width, 0.24f * std::sin(u * 6.2831853f), 2.0f);
    }
    return pts;
}
std::vector<glm::vec3> line(glm::vec3 a, glm::vec3 b) { return {a, b}; }
// Points on a circle of radius `r` around (cx, cy) on the front of the ball, from angle a0 to a1 (radians).
std::vector<glm::vec3> arc(float cx, float cy, float r, float a0, float a1, int n = 24) {
    std::vector<glm::vec3> pts;
    for (int i = 0; i <= n; ++i) {
        const float a = a0 + (a1 - a0) * float(i) / float(n);
        pts.emplace_back(cx + r * std::cos(a), cy + r * std::sin(a), 2.0f);
    }
    return pts;
}

void stroke(Document& d, const BrushRecipe* recipe, BrushType type, std::vector<glm::vec3> pts, float radius,
            float strength, bool curve = true) {
    PathStroke st;
    st.points = std::move(pts);
    st.view = glm::vec3(0.0f, 0.0f, -1.0f);
    st.radius = radius;
    st.strength = strength;
    st.curve = curve;
    std::string err;
    strokePath(d, recipe, type, st, err);
}

} // namespace

// Sculpts the preview of a brush (a recipe when given, else `type`) with the same code real strokes use,
// on the shared ball (see kBallRadius).
std::unique_ptr<Mesh> sculptBrushPreview(const BrushRecipe* recipe, int type) {
    if (type == kPaintBrush) {
        // Polypaint: a coloured S on the ball; white elsewhere, so the material shows as on the others.
        Mesh m = makeIcosphere(kBallDetail, kBallRadius);
        m.ensureColors(glm::vec3(1.0f));
        for (uint32_t v = 0; v < m.vertexCount(); ++v) {
            const glm::vec3 p = m.positions[v];
            const float curve = 0.24f * std::sin((p.x / 0.56f + 1.0f) * 3.14159265f);
            const float d2 = std::abs(p.y - curve);
            if (p.z > 0.0f && std::abs(p.x) < 0.6f && d2 < 0.12f) {
                const float t = std::clamp((0.12f - d2) / 0.04f, 0.0f, 1.0f);
                m.colors[v] = glm::mix(m.colors[v], glm::vec3(0.78f, 0.16f, 0.06f), t);
            }
        }
        return std::make_unique<Mesh>(std::move(m));
    }
    Document d;
    d.setMesh(makeIcosphere(kBallDetail, kBallRadius));
    const float r = 0.17f;
    if (recipe) {
        stroke(d, recipe, BrushType::Standard, frontCurve(), r, 0.9f);
        return sceneMesh(d);
    }
    // Each brush gets a mark of its own shape that shows what it does, so the shelf tells them apart at a glance
    // (the same S everywhere left only a few pixels of difference between Standard, Clay, Inflate and Pinch).
    constexpr float kPi = 3.14159265f;
    const auto bt = BrushType(type);
    switch (bt) {
    case BrushType::Clay: // three parallel strips laid on like clay
        for (int i = -1; i <= 1; ++i) {
            const float dx = 0.3f * float(i);
            stroke(d, nullptr, bt, line({dx - 0.28f, -0.42f, 2.0f}, {dx + 0.12f, 0.42f, 2.0f}), 0.15f, 1.0f, false);
        }
        break;
    case BrushType::ClayBuildup: // a hash of square, flat-topped bars, built up in two directions
        for (float o : {-0.2f, 0.2f}) {
            stroke(d, nullptr, bt, line({-0.5f, o, 2.0f}, {0.5f, o, 2.0f}), 0.13f, 0.1f, false);
            stroke(d, nullptr, bt, line({o, -0.5f, 2.0f}, {o, 0.5f, 2.0f}), 0.13f, 0.1f, false);
        }
        break;
    case BrushType::DamStandard: // one deep, sharp diagonal cut
        stroke(d, nullptr, bt, line({-0.55f, 0.45f, 2.0f}, {0.55f, -0.45f, 2.0f}), 0.2f, 1.0f, false);
        stroke(d, nullptr, bt, line({-0.55f, 0.45f, 2.0f}, {0.55f, -0.45f, 2.0f}), 0.2f, 1.0f, false);
        break;
    case BrushType::Crease: // a curved groove, like a smile line
        stroke(d, nullptr, bt, arc(0.0f, 0.42f, 0.62f, kPi * 1.18f, kPi * 1.82f), 0.17f, 1.0f);
        stroke(d, nullptr, bt, arc(0.0f, 0.42f, 0.62f, kPi * 1.18f, kPi * 1.82f), 0.17f, 1.0f);
        break;
    case BrushType::Move: // the silhouette pulled out into a broad soft lobe on the upper left
        stroke(d, nullptr, bt, line({-0.5f, 0.45f, 2.0f}, {-0.78f, 0.72f, 2.0f}), 0.5f, 1.0f, false);
        break;
    case BrushType::SnakeHook:
        // A curling horn pulled out of the front into the tile's top right corner (the only room past the ball's
        // silhouette; the ball fills the tile's height). It drags in the view plane, as in the viewport.
        stroke(d, nullptr, bt, line({0.3f, 0.3f, 2.0f}, {0.86f, 0.86f, 2.0f}), 0.26f, 1.0f, false);
        break;
    case BrushType::Inflate: // a ring, puffed up
        stroke(d, nullptr, bt, arc(0.0f, 0.0f, 0.38f, 0.0f, 2.0f * kPi, 32), 0.2f, 1.0f);
        break;
    case BrushType::Pinch: // a raised chevron pinched to a sharp crest
        stroke(d, nullptr, BrushType::Standard, {{-0.5f, 0.35f, 2.0f}, {0.0f, -0.3f, 2.0f}, {0.5f, 0.35f, 2.0f}}, 0.22f, 1.0f, false);
        for (int pass = 0; pass < 2; ++pass)
            stroke(d, nullptr, bt, {{-0.5f, 0.35f, 2.0f}, {0.0f, -0.3f, 2.0f}, {0.5f, 0.35f, 2.0f}}, 0.22f, 1.0f, false);
        break;
    case BrushType::Smooth: { // a bumpy ridge, its right half smoothed away
        for (int i = 0; i < 7; ++i)
            for (int pass = 0; pass < 4; ++pass)
                stroke(d, nullptr, BrushType::Standard, {glm::vec3(-0.6f + 0.2f * float(i), 0.0f, 2.0f)}, 0.13f, 1.0f, false);
        for (int pass = 0; pass < 3; ++pass)
            stroke(d, nullptr, bt, line({0.12f, 0.0f, 2.0f}, {0.8f, 0.0f, 2.0f}), 0.26f, 1.0f, false);
        break;
    }
    case BrushType::Flatten: // a dome with a flat facet planed across its top
        for (int pass = 0; pass < 3; ++pass)
            stroke(d, nullptr, BrushType::Standard, {glm::vec3(0.0f, 0.0f, 2.0f)}, 0.5f, 1.0f, false);
        for (int pass = 0; pass < 3; ++pass)
            stroke(d, nullptr, bt, line({-0.15f, 0.05f, 2.0f}, {0.15f, -0.05f, 2.0f}), 0.32f, 1.0f, false);
        break;
    case BrushType::Trim: { // a ridge cut down to hard planes along its length
        const auto ridge = line({-0.6f, -0.35f, 2.0f}, {0.6f, 0.35f, 2.0f});
        stroke(d, nullptr, BrushType::Standard, ridge, 0.24f, 1.0f, false);
        stroke(d, nullptr, BrushType::Clay, ridge, 0.24f, 1.0f, false);
        stroke(d, nullptr, bt, line({-0.3f, -0.17f, 2.0f}, {0.3f, 0.17f, 2.0f}), 0.24f, 1.0f, false);
        break;
    }
    case BrushType::Nudge: { // an upright ridge slid sideways in the middle (broad and gentle: a narrow, strong
        // nudge across a sharp ridge tore it)
        const auto ridge = line({-0.1f, -0.6f, 2.0f}, {-0.1f, 0.6f, 2.0f});
        stroke(d, nullptr, BrushType::Standard, ridge, 0.22f, 1.0f, false);
        stroke(d, nullptr, bt, line({-0.15f, 0.0f, 2.0f}, {0.15f, 0.0f, 2.0f}), 0.36f, 0.3f, false);
        break;
    }
    case BrushType::Layer: // a ring raised to one flat height per stroke; two strokes, two steps (a square path's
        // corners were dabbed twice and pinched)
        for (int pass = 0; pass < 2; ++pass)
            stroke(d, nullptr, bt, arc(0.0f, 0.0f, 0.36f, 0.0f, 2.0f * kPi, 48), 0.24f, 1.0f);
        break;
    case BrushType::Vdm: // one stamp of the built-in Horn
        stroke(d, nullptr, bt, {glm::vec3(0.0f, -0.05f, 2.0f)}, 0.45f, 0.5f, false);
        break;
    case BrushType::KelvinGrab: // the silhouette pulled out into a smooth, bulging lobe
        d.sculptor.kelvinlet.squash = 1.0f;
        stroke(d, nullptr, bt, line({-0.45f, 0.45f, 2.0f}, {-0.8f, 0.8f, 2.0f}), 0.45f, 0.5f, false);
        break;
    case BrushType::KelvinTwist: // a raised bar twisted into an S
        stroke(d, nullptr, BrushType::Standard, line({-0.5f, 0.0f, 2.0f}, {0.5f, 0.0f, 2.0f}), 0.16f, 1.0f, false);
        stroke(d, nullptr, BrushType::Standard, line({-0.5f, 0.0f, 2.0f}, {0.5f, 0.0f, 2.0f}), 0.16f, 1.0f, false);
        stroke(d, nullptr, bt, line({0.0f, 0.0f, 2.0f}, {0.45f, 0.0f, 2.0f}), 0.45f, 0.5f, false);
        break;
    case BrushType::KelvinScale: // a ring of bumps pinched toward the middle
        for (int i = 0; i < 6; ++i) {
            const float a = float(i) * kPi / 3.0f;
            for (int pass = 0; pass < 3; ++pass)
                stroke(d, nullptr, BrushType::Standard, {glm::vec3(0.4f * std::cos(a), 0.4f * std::sin(a), 2.0f)}, 0.12f, 1.0f, false);
        }
        stroke(d, nullptr, bt, line({0.0f, 0.0f, 2.0f}, {-0.5f, 0.0f, 2.0f}), 0.4f, 0.5f, false);
        break;
    case BrushType::KelvinSnake: // a smooth tapering horn pulled out past the silhouette, into the bottom right corner
        stroke(d, nullptr, bt, line({0.25f, -0.2f, 2.0f}, {0.88f, -0.84f, 2.0f}), 0.26f, 0.7f, false);
        break;
    case BrushType::DetailClone: // a row of horns along a curve
        stroke(d, nullptr, bt, arc(0.0f, -0.25f, 0.55f, kPi * 0.15f, kPi * 0.85f), 0.12f, 0.5f);
        break;
    default: // Standard: a soft rounded S
        stroke(d, nullptr, bt, frontCurve(1.15f), 0.19f, 1.0f);
        break;
    }
    return sceneMesh(d);
}

} // namespace sf
