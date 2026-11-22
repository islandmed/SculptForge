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

#include "GeomInvariants.h"
#include "core/StarterMeshes.h"

TEST(layer_view_falloff_ear_with_x_symmetry_lifts_both_ears_alike_and_tears_nothing) {
    // artist-qa (36e3c6f4): Layer from the side on a head's ear, X symmetry, View falloff. The far-side fade is part of Layer's height
    // (its "done" height is kept per vertex), so a vertex that stood in the fade early in a stroke still reaches its full height, and the
    // mirrored ear comes out as the stroked one: the same vertices, the same heights, no flipped triangle or fold on either.
    const StarterMesh* head = findStarterMesh("human-head");
    CHECK(head != nullptr);
    if (!head) return;
    for (float rad : {0.04f, 0.06f})
    for (float eyeDist : {1e6f, 3.0f})
    for (BrushType t : {BrushType::Layer})
        for (bool vf : {false, true}) {
            Document d;
            std::string error;
            CHECK(loadStarterMesh(*head, d, 2, error));
            glm::vec3 lo(1e9f), hi(-1e9f);
            for (const glm::vec3& q : d.mesh.positions) lo = glm::min(lo, q), hi = glm::max(hi, q);
            const float height = hi.y - lo.y;
            glm::vec3 ear(-1e9f);
            for (int iy = 0; iy < 40; ++iy)
                for (int iz = 0; iz < 40; ++iz) {
                    const glm::vec3 from(hi.x + 1.0f, lo.y + height * (0.4f + 0.3f * float(iy) / 39.0f), lo.z + (hi.z - lo.z) * float(iz) / 39.0f);
                    RayHit hit;
                    if (d.bvh.raycast(d.mesh, from, glm::vec3(-1.0f, 0.0f, 0.0f), hit) && hit.position.x > ear.x) ear = hit.position;
                }
            const glm::vec3 eye(ear.x + eyeDist * height, 0.5f * (lo.y + hi.y), 0.5f * (lo.z + hi.z));
            const std::vector<glm::vec3> start = d.mesh.positions;
            d.sculptor.beginStroke(d.mesh, "Test");
            glm::vec3 last(0.0f);
            for (int i = 0; i <= 40; ++i) {
                const float u = i <= 20 ? float(i) / 20.0f : float(40 - i) / 20.0f; // there and back
                const glm::vec3 target(ear.x, ear.y + 0.08f * height * (u - 0.5f), ear.z);
                const glm::vec3 dir = glm::normalize(target - eye);
                RayHit hit;
                if (!d.bvh.raycast(d.mesh, eye, dir, hit)) continue;
                DabParams p;
                p.center = hit.position;
                p.pickNormal = glm::normalize(hit.normal);
                p.viewDir = dir;
                p.radius = rad * height;
                p.strength = 0.6f;
                p.viewFalloff = vf;
                p.strokeDelta = i ? hit.position - last : glm::vec3(0.0f);
                last = hit.position;
                d.sculptor.dab(d.mesh, d.bvh, t, p, Symmetry{true, false, false});
            }
            d.undo.push(d.sculptor.endStroke());
            size_t near = 0, far = 0;
            float nearMax = 0.0f, farMax = 0.0f;
            for (size_t v = 0; v < start.size(); ++v) {
                const float mv = glm::distance(start[v], d.mesh.positions[v]);
                if (mv > 0.0f) (start[v].x > 0.0f ? near : far) += 1;
                float& m = start[v].x > 0.0f ? nearMax : farMax;
                m = std::max(m, mv);
            }
            std::printf("  r %.2f eye %.0f %-5s vf %d: moved near %zu (max %.4f), mirrored %zu (max %.4f); flipped %zu, folds %zu\n",
                        double(rad), double(eyeDist), t == BrushType::Layer ? "Layer" : "Nudge", int(vf), near, double(nearMax), far,
                        double(farMax), geom::flippedFaces(start, d.mesh), geom::foldedFaces(start, d.mesh));
            CHECK(near > 0);
            CHECK(far + 8 >= near && near + 8 >= far);
            CHECK(std::abs(nearMax - farMax) < 0.002f);
            CHECK(geom::flippedFaces(start, d.mesh) == 0);
            CHECK(geom::foldedFaces(start, d.mesh) == 0);
        }
}
