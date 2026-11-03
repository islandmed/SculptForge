// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The brush shelf previews (app/BrushPreview.cpp): every built-in brush sculpts its mark on the clay ball, and the
// marks keep their shape. Tendril and Elastic Tendril pull a long horn out past the silhouette (they used to show a
// tiny spike when the preview path ran off the ball), and no preview leaves torn, folded or degenerate triangles
// (Layer's pinched corners, Nudge's torn ridge).
#include "TestHarness.h"

#include "app/AssetLibrary.h"
#include "app/BrushPreview.h"
#include "core/Sculpt.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

using namespace sf;

namespace {

struct PreviewShape {
    float reach = 0.0f;   // how far the mesh sticks out past the ball's silhouette (in the xy view plane)
    float lift = 0.0f;    // the largest change of a vertex's distance from the centre (in or out)
    int degenerate = 0;   // triangles with (nearly) no area
    int folded = 0;       // neighbouring triangles turned back on each other (> 150 degrees apart)
};

PreviewShape measure(const Mesh& m) {
    PreviewShape s;
    for (const glm::vec3& p : m.positions) {
        s.reach = std::max(s.reach, glm::length(glm::vec2(p)) - kBallRadius);
        s.lift = std::max(s.lift, std::abs(glm::length(p) - kBallRadius));
    }
    std::vector<glm::vec3> normals(m.triangles.size());
    for (size_t t = 0; t < m.triangles.size(); ++t) {
        const glm::uvec3 tri = m.triangles[t];
        const glm::vec3 a = m.positions[tri.x], b = m.positions[tri.y], c = m.positions[tri.z];
        const glm::vec3 n = glm::cross(b - a, c - a);
        const float area2 = glm::length(n);
        const float longest = std::max({glm::length(b - a), glm::length(c - b), glm::length(a - c)});
        if (area2 < 1e-12f || longest <= 0.0f) {
            ++s.degenerate;
            continue;
        }
        normals[t] = n / area2;
    }
    // Folds: across each edge (found by its sorted vertex pair), the two triangles' normals.
    std::vector<std::pair<uint64_t, uint32_t>> edges;
    edges.reserve(m.triangles.size() * 3);
    for (size_t t = 0; t < m.triangles.size(); ++t)
        for (int k = 0; k < 3; ++k) {
            const uint32_t u = m.triangles[t][k], v = m.triangles[t][(k + 1) % 3];
            edges.emplace_back((uint64_t(std::min(u, v)) << 32) | std::max(u, v), uint32_t(t));
        }
    std::sort(edges.begin(), edges.end());
    for (size_t i = 0; i + 1 < edges.size(); ++i)
        if (edges[i].first == edges[i + 1].first &&
            glm::dot(normals[edges[i].second], normals[edges[i + 1].second]) < -0.866f)
            ++s.folded;
    return s;
}

} // namespace

TEST(brush_previews) {
    const bool verbose = std::getenv("SF_BRUSH_PREVIEW_METRICS") != nullptr;
    std::vector<int> brushes;
    for (int t = 0; t < int(BrushType::Count); ++t)
        if (BrushType(t) != BrushType::Morph) brushes.push_back(t); // (Morph needs a morph target; not on the shelf)
    brushes.push_back(kPaintBrush);
    for (int t : brushes) {
        const auto mesh = sculptBrushPreview(nullptr, t);
        CHECK(mesh != nullptr);
        if (!mesh) continue;
        const PreviewShape s = measure(*mesh);
        const char* name = t == kPaintBrush ? "Paint" : brushLabel(BrushType(t));
        if (verbose)
            std::printf("  %-16s reach %.3f lift %.3f degenerate %d folded %d\n", name, s.reach, s.lift, s.degenerate,
                        s.folded);
        CHECK(s.degenerate == 0);
        CHECK(s.folded == 0);
        if (s.degenerate || s.folded) std::printf("  (%s)\n", name);
        if (t != kPaintBrush) CHECK(s.lift > 0.01f); // every brush leaves a mark
    }
    // Tendril and Elastic Tendril pull a horn well past the silhouette, as a drag in the viewport does.
    for (BrushType t : {BrushType::SnakeHook, BrushType::KelvinSnake}) {
        const auto mesh = sculptBrushPreview(nullptr, int(t));
        CHECK(mesh && measure(*mesh).reach >= 0.25f);
    }
}
