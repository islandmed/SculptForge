// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Nudge under View falloff (a member's report, 0.13.11: "sculpting on one side of the mesh also sculpts on the back"). View falloff
// reaches through the model as a cylinder along the view, and Nudge slid every vertex in it the same way, the far side of a thick
// part too. Like the other brushes that work the surface (gh#229: Standard, Inflate, Crease, Pinch), Nudge and Layer take only what
// lies within 1.5 radii behind the cursor along the view, fading to nothing at 3; Move / Grab still carry all of the circle.
// The case: a thick box (a boxy Part), a stroke across its front face, the far side seen after orbiting.

#include "TestHarness.h"

#include "core/Primitives.h"
#include "core/Sculpt.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace sf;

namespace {

constexpr float kRadius = 0.25f;

struct Moved {
    float front = 0.0f; // the largest move of a vertex on the front face
    float back = 0.0f;  // ... of one on the back face
    float far = 0.0f;   // ... of one more than 1.5 radii behind every dab's centre along the view
    float deep = 0.0f;  // ... of one more than 3 radii behind
    bool finite = true;
};

Moved strokeAcross(BrushType type, bool viewFalloff, bool frontFacesOnly, float thickness) {
    Mesh m = makeCube(48, 2.0f);
    for (glm::vec3& q : m.positions) q.z *= thickness * 0.5f; // (a box 2 wide and tall, `thickness` deep)
    m.recomputeAllNormals();
    Document d = makeDoc(std::move(m));
    const std::vector<glm::vec3> start = d.mesh.positions;
    const float front = thickness * 0.5f;
    d.sculptor.beginStroke(d.mesh, "Test");
    glm::vec3 last(0.0f);
    std::vector<float> depths;
    for (int i = 0; i <= 16; ++i) {
        const float x = -0.6f + 1.2f * float(i) / 16.0f;
        RayHit hit;
        if (!d.bvh.raycast(d.mesh, glm::vec3(x, 0.1f, 4.0f), glm::vec3(0.0f, 0.0f, -1.0f), hit)) continue;
        DabParams p;
        p.center = hit.position;
        p.pickNormal = glm::normalize(hit.normal);
        p.viewDir = glm::vec3(0.0f, 0.0f, -1.0f);
        p.radius = kRadius;
        p.strength = 0.6f;
        p.viewFalloff = viewFalloff;
        p.frontFacesOnly = frontFacesOnly;
        p.strokeDelta = i ? hit.position - last : glm::vec3(0.0f);
        last = hit.position;
        depths.push_back(hit.position.z);
        d.sculptor.dab(d.mesh, d.bvh, type, p, Symmetry{false, false, false});
    }
    d.undo.push(d.sculptor.endStroke());
    Moved out;
    for (size_t v = 0; v < start.size(); ++v) {
        const glm::vec3& q = d.mesh.positions[v];
        out.finite = out.finite && std::isfinite(q.x) && std::isfinite(q.y) && std::isfinite(q.z);
        const float moved = glm::distance(start[v], q);
        if (start[v].z > front - 0.1f) out.front = std::max(out.front, moved);
        if (start[v].z < -front + 0.1f) out.back = std::max(out.back, moved);
        float behind = 1e9f;
        for (float z : depths) behind = std::min(behind, std::abs(start[v].z - z));
        if (behind > 1.5f * kRadius) out.far = std::max(out.far, moved);
        if (behind > 3.0f * kRadius) out.deep = std::max(out.deep, moved);
    }
    return out;
}

const char* nameOf(BrushType t) { return t == BrushType::Nudge ? "Nudge" : "Layer"; }

} // namespace

TEST(nudge_view_falloff_leaves_the_far_side_of_a_thick_part) {
    for (BrushType t : {BrushType::Nudge, BrushType::Layer})
        for (bool vf : {false, true})
            for (bool ffo : {false, true}) {
                const Moved r = strokeAcross(t, vf, ffo, 1.6f);
                std::printf("  %-6s View falloff %d, front faces only %d: front moved %.4f, back face %.4f, far side (> 1.5 radii) %.5f, (> 3 radii) %.5f\n",
                            nameOf(t), int(vf), int(ffo), double(r.front), double(r.back), double(r.far), double(r.deep));
                CHECK(r.finite);
                CHECK(r.front > 0.01f); // (it still works the face)
                CHECK(r.deep == 0.0f);
                CHECK(r.far < 0.002f);
            }
}

TEST(nudge_view_falloff_still_reaches_through_a_thin_part) {
    // A plate under the brush (0.2 thick under a 0.25 brush): the far side is within the reach, so View falloff still works it.
    for (BrushType t : {BrushType::Nudge, BrushType::Layer}) {
        const Moved r = strokeAcross(t, true, false, 0.2f);
        std::printf("  %-6s thin plate, View falloff: front moved %.4f, back moved %.4f\n", nameOf(t), double(r.front), double(r.back));
        CHECK(r.finite);
        CHECK(r.back > 0.005f);
    }
}
