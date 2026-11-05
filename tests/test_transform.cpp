// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Transforms (core/MeshTransform.h, the Transform gizmo and the `transform` command): exact results, masked parts with a
// smooth falloff, sculpt layers, symmetry, All Parts, attributes left alone and bit-for-bit undo.
#include "TestHarness.h"

#include "core/Commands.h"
#include "core/MeshTransform.h"
#include "core/Primitives.h"
#include "core/Project.h"
#include "core/Parts.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>

using namespace sf;

namespace {

const CommandRegistry& registry() {
    static CommandRegistry r;
    return r;
}

CommandResult run(Document& d, const std::string& name, const Args& a = {}) { return registry().execute(d, name, a); }

template <class V> bool sameBits(const std::vector<V>& a, const std::vector<V>& b) {
    return a.size() == b.size() && (a.empty() || std::memcmp(a.data(), b.data(), a.size() * sizeof(V)) == 0);
}

float maxDistance(const std::vector<glm::vec3>& a, const std::vector<glm::vec3>& b) {
    float d = 0.0f;
    for (size_t i = 0; i < a.size(); ++i) d = std::max(d, glm::length(a[i] - b[i]));
    return d;
}

// Mask = 1 where x < edge.
void maskBelowX(Mesh& m, float edge) {
    m.ensureMask();
    for (uint32_t v = 0; v < m.vertexCount(); ++v) m.mask[v] = m.positions[v].x < edge ? 1.0f : 0.0f;
}

// A capsule-like arm: a sphere stretched along X.
Mesh makeArm(int subdivisions = 4) {
    Mesh m = makeIcosphere(subdivisions, 1.0f);
    for (auto& p : m.positions) p *= glm::vec3(3.0f, 0.6f, 0.6f);
    m.recomputeAllNormals();
    return m;
}

} // namespace

TEST(transform_translate_is_exact) {
    Document d = makeDoc(makeIcosphere(3));
    const auto before = d.mesh.positions;
    CHECK(run(d, "transform", {{"translate", std::string("0.25,-1,3")}}).ok);
    bool exact = true;
    for (size_t v = 0; v < before.size(); ++v) exact = exact && d.mesh.positions[v] == before[v] + glm::vec3(0.25f, -1.0f, 3.0f);
    CHECK(exact);
    CHECK(d.undo.canUndo());
}

TEST(transform_matches_the_formula) {
    Document d = makeDoc(makeIcosphere(3));
    const auto before = d.mesh.positions;
    CHECK(run(d, "transform",
              {{"translate", std::string("1,2,3")}, {"rotate", std::string("30,-45,60")}, {"scale", std::string("2,1,0.5")},
               {"pivot", std::string("0.5,0,-0.25")}})
              .ok);
    // Independent: p' = pivot + t + Rz Ry Rx S (p - pivot), built from glm matrices.
    glm::mat4 r(1.0f);
    r = glm::rotate(r, glm::radians(60.0f), glm::vec3(0, 0, 1));
    r = glm::rotate(r, glm::radians(-45.0f), glm::vec3(0, 1, 0));
    r = glm::rotate(r, glm::radians(30.0f), glm::vec3(1, 0, 0));
    const glm::vec3 pivot(0.5f, 0.0f, -0.25f), t(1, 2, 3), s(2, 1, 0.5f);
    float err = 0.0f;
    for (size_t v = 0; v < before.size(); ++v) {
        const glm::vec3 want = pivot + t + glm::vec3(r * glm::vec4((before[v] - pivot) * s, 0.0f));
        err = std::max(err, glm::length(d.mesh.positions[v] - want));
    }
    CHECK(err < 1e-5f);
    // Normals follow (a sphere's normals are its directions; scaled non-uniformly they tilt accordingly).
    float dot = 1.0f;
    for (size_t v = 0; v < before.size(); ++v) dot = std::min(dot, glm::length(d.mesh.normals[v]));
    CHECK(dot > 0.999f);
}

TEST(transform_rotate_and_scale_about_centre) {
    Document d = makeDoc(makeIcosphere(3));
    for (auto& p : d.mesh.positions) p += glm::vec3(5, 0, 0);
    d.mesh.recomputeAllNormals();
    d.bvh.build(d.mesh);
    const auto before = d.mesh.positions;
    CHECK(run(d, "transform", {{"rotate", std::string("0,90,0")}}).ok); // about the centre (5,0,0)
    float err = 0.0f;
    for (size_t v = 0; v < before.size(); ++v) {
        const glm::vec3 q = before[v] - glm::vec3(5, 0, 0);
        err = std::max(err, glm::length(d.mesh.positions[v] - (glm::vec3(5, 0, 0) + glm::vec3(q.z, q.y, -q.x))));
    }
    CHECK(err < 1e-5f);
    CHECK(run(d, "transform", {{"scale", 2.0}}).ok);
    const AABB b = d.mesh.bounds();
    CHECK(std::abs(b.extent().x - 4.0f) < 1e-3f);
    CHECK(glm::length(b.center() - glm::vec3(5, 0, 0)) < 1e-3f);
}

TEST(transform_undo_restores_bit_for_bit) {
    Document d = makeDoc(makeArm());
    const auto pos = d.mesh.positions, nrm = d.mesh.normals;
    CHECK(run(d, "transform", {{"rotate", std::string("10,20,30")}, {"translate", std::string("0.1,0.2,0.3")}, {"scale", 1.5}}).ok);
    const auto moved = d.mesh.positions;
    CHECK(!sameBits(moved, pos));
    CHECK(run(d, "undo").ok);
    CHECK(sameBits(d.mesh.positions, pos));
    CHECK(sameBits(d.mesh.normals, nrm));
    CHECK(run(d, "redo").ok);
    CHECK(sameBits(d.mesh.positions, moved));
    // Masked, with a falloff: same.
    CHECK(run(d, "undo").ok);
    maskBelowX(d.mesh, 0.0f);
    CHECK(run(d, "transform", {{"rotate", std::string("0,0,40")}, {"pivot", std::string("0,0,0")}, {"falloff", 0.1}}).ok);
    CHECK(run(d, "undo").ok);
    CHECK(sameBits(d.mesh.positions, pos));
    CHECK(sameBits(d.mesh.normals, nrm));
}

TEST(transform_masked_part_only) {
    Document d = makeDoc(makeArm());
    maskBelowX(d.mesh, 0.0f);
    const auto before = d.mesh.positions;
    CHECK(run(d, "transform", {{"translate", std::string("0,1,0")}}).ok);
    bool still = true, full = true;
    for (size_t v = 0; v < before.size(); ++v) {
        if (d.mesh.mask[v] >= 1.0f) still = still && d.mesh.positions[v] == before[v];
        else full = full && d.mesh.positions[v] == before[v] + glm::vec3(0, 1, 0);
    }
    CHECK(still);
    CHECK(full);
    // masked=0 moves everything.
    CHECK(run(d, "transform", {{"translate", std::string("0,1,0")}, {"masked", false}}).ok);
    CHECK(d.mesh.positions[0].y > before[0].y + 0.5f);
    // Everything masked: nothing to move.
    std::fill(d.mesh.mask.begin(), d.mesh.mask.end(), 1.0f);
    CHECK(!run(d, "transform", {{"translate", std::string("0,1,0")}}).ok);
}

TEST(transform_partial_mask_moves_partly) {
    Document d = makeDoc(makeIcosphere(2));
    d.mesh.ensureMask(0.25f); // 75% weight everywhere
    const auto before = d.mesh.positions;
    CHECK(run(d, "transform", {{"translate", std::string("4,0,0")}}).ok);
    float err = 0.0f;
    for (size_t v = 0; v < before.size(); ++v) err = std::max(err, glm::length(d.mesh.positions[v] - before[v] - glm::vec3(3, 0, 0)));
    CHECK(err < 1e-2f); // weights are quantized to 1/4096
}

TEST(transform_falloff_is_smooth) {
    Mesh arm = makeArm(5);
    maskBelowX(arm, 0.0f);
    // Weights: a hard 0/1 step without falloff, a gradual ramp with it.
    auto steepest = [&](const std::vector<float>& w) {
        float s = 0.0f;
        for (uint32_t v = 0; v < arm.vertexCount(); ++v)
            for (uint32_t n : arm.neighbors(v)) s = std::max(s, std::abs(w[v] - w[n]));
        return s;
    };
    const auto hard = transformWeights(arm, true, 0);
    const auto soft = transformWeights(arm, true, 0.15f);
    CHECK(steepest(hard) == 1.0f);
    CHECK(steepest(soft) < 0.25f);
    for (float w : soft) CHECK(w >= 0.0f && w <= 1.0f);
    // Bend the arm 60 degrees at the "elbow": the falloff spreads the bend instead of tearing one row of edges.
    auto stretch = [&](float falloff) {
        Document d = makeDoc(arm);
        CHECK(run(d, "transform", {{"rotate", std::string("0,0,60")}, {"pivot", std::string("0,0,0")}, {"falloff", double(falloff)}}).ok);
        float worst = 0.0f;
        for (uint32_t v = 0; v < arm.vertexCount(); ++v)
            for (uint32_t n : arm.neighbors(v)) {
                const float a = glm::length(arm.positions[v] - arm.positions[n]);
                const float b = glm::length(d.mesh.positions[v] - d.mesh.positions[n]);
                worst = std::max(worst, b / std::max(a, 1e-6f));
            }
        // The far end turned the whole 60 degrees.
        const glm::vec3 tip = d.mesh.positions[size_t(std::max_element(arm.positions.begin(), arm.positions.end(),
                                                                          [](auto& p, auto& q) { return p.x < q.x; }) -
                                                         arm.positions.begin())];
        CHECK(std::abs(glm::degrees(std::atan2(tip.y, tip.x)) - 60.0f) < 1.0f);
        return worst;
    };
    const float hardStretch = stretch(0), softStretch = stretch(0.15f);
    std::printf("  edge stretch at a 60 degree bend: hard mask %.2fx, falloff 0.15 %.2fx\n", double(hardStretch),
                double(softStretch));
    // A 60 degree turn eased in over 0.15 of the size still shears the outside of a thick arm (about 2x), but the
    // hard mask's tearing step (10x) is gone.
    CHECK(softStretch < hardStretch / 3.0f);
}

TEST(transform_hidden_never_moves) {
    Document d = makeDoc(makeIcosphere(3));
    d.mesh.ensureHidden();
    for (uint32_t t = 0; t < d.mesh.triangleCount(); ++t) d.mesh.hidden[t] = d.mesh.positions[d.mesh.triangles[t].x].y > 0.0f;
    const auto before = d.mesh.positions;
    CHECK(run(d, "transform", {{"translate", std::string("0,0,1")}}).ok);
    bool ok = true;
    for (uint32_t v = 0; v < d.mesh.vertexCount(); ++v)
        if (d.mesh.vertexHidden(v)) ok = ok && d.mesh.positions[v] == before[v];
    CHECK(ok);
}

TEST(transform_sculpt_layers_move_with_the_surface) {
    Document d = makeDoc(makeIcosphere(3));
    Mesh& m = d.mesh;
    m.addLayer("Bump");
    for (uint32_t v = 0; v < m.vertexCount(); ++v) m.layers[0].delta[v] = m.normals[v] * 0.1f * float(v % 3);
    m.setLayerWeight(0, 0.7f);
    const auto pos = m.positions, base = m.base, delta = m.layers[0].delta;
    CHECK(run(d, "transform", {{"rotate", std::string("0,0,90")}, {"translate", std::string("1,0,0")}, {"pivot", std::string("origin")}}).ok);
    auto rot = [](const glm::vec3& p) { return glm::vec3(-p.y, p.x, p.z); };
    float err = 0.0f;
    for (size_t v = 0; v < pos.size(); ++v) err = std::max(err, glm::length(m.positions[v] - (rot(pos[v]) + glm::vec3(1, 0, 0))));
    CHECK(err < 1e-5f);
    // The layer still works: at weight 0 it's the transformed base.
    m.setLayerWeight(0, 0.0f);
    err = 0.0f;
    for (size_t v = 0; v < pos.size(); ++v) err = std::max(err, glm::length(m.positions[v] - (rot(base[v]) + glm::vec3(1, 0, 0))));
    CHECK(err < 1e-5f);
    m.setLayerWeight(0, 0.7f);
    CHECK(run(d, "undo").ok);
    CHECK(sameBits(m.base, base));
    CHECK(sameBits(m.layers[0].delta, delta));
}

// GitHub #133: with a sculpt layer active, Move / Scale / Rotate record into that layer like a stroke (masked or
// not): the base and other layers stay, the layer's weight dials the move in and out, undo restores it.
TEST(transform_records_into_the_active_layer) {
    Document d = makeDoc(makeArm());
    Mesh& m = d.mesh;
    CHECK(run(d, "layer_add", {{"name", std::string("Other")}}).ok);
    for (uint32_t v = 0; v < m.vertexCount(); ++v) m.layers[0].delta[v] = m.normals[v] * 0.05f;
    m.setLayerWeight(0, 0.5f);
    CHECK(run(d, "layer_add", {{"name", std::string("Pose")}}).ok);
    CHECK(d.activeLayer == 1);
    const auto pos = m.positions, base = m.base, other = m.layers[0].delta;

    // Unmasked rotate: the whole Part turns, all of it in the active layer.
    CHECK(run(d, "transform", {{"rotate", std::string("0,0,90")}, {"pivot", std::string("origin")}}).ok);
    auto rot = [](const glm::vec3& p) { return glm::vec3(-p.y, p.x, p.z); };
    float err = 0.0f;
    for (size_t v = 0; v < pos.size(); ++v) err = std::max(err, glm::length(m.positions[v] - rot(pos[v])));
    CHECK(err < 1e-5f);
    CHECK(sameBits(m.base, base));
    CHECK(sameBits(m.layers[0].delta, other));
    m.setLayerWeight(1, 0.0f); // the layer off: the shape as before
    CHECK(maxDistance(m.positions, pos) < 1e-5f);
    m.setLayerWeight(1, 0.5f); // half: halfway along each vertex's move
    err = 0.0f;
    for (size_t v = 0; v < pos.size(); ++v) err = std::max(err, glm::length(m.positions[v] - glm::mix(pos[v], rot(pos[v]), 0.5f)));
    CHECK(err < 1e-5f);
    m.setLayerWeight(1, 1.0f);
    CHECK(run(d, "undo").ok);
    CHECK(maxDistance(m.positions, pos) < 1e-6f);
    for (const glm::vec3& o : m.layers[1].delta) CHECK(o == glm::vec3(0.0f));

    // Masked (the issue: mask, invert, move): only the unmasked part moves, into the layer, at the layer's weight.
    maskBelowX(m, 0.0f);
    m.setLayerWeight(1, 0.25f);
    const auto pos2 = m.positions;
    CHECK(run(d, "transform", {{"translate", std::string("0,1,0")}}).ok);
    bool still = true, full = true;
    for (size_t v = 0; v < pos2.size(); ++v) {
        if (m.mask[v] >= 1.0f) still = still && m.positions[v] == pos2[v];
        else full = full && glm::length(m.positions[v] - (pos2[v] + glm::vec3(0, 1, 0))) < 1e-5f;
    }
    CHECK(still);
    CHECK(full);
    CHECK(sameBits(m.base, base));
    m.setLayerWeight(1, 0.0f);
    float e2 = 0.0f; // the layer off: base + the other layer at 0.5
    for (size_t v = 0; v < base.size(); ++v) e2 = std::max(e2, glm::length(m.positions[v] - (base[v] + 0.5f * other[v])));
    CHECK(e2 < 1e-5f);
    m.setLayerWeight(1, 0.25f);
    CHECK(run(d, "undo").ok);
    CHECK(maxDistance(m.positions, pos2) < 1e-6f);

    // Weight 0: refused, like a stroke.
    m.setLayerWeight(1, 0.0f);
    const auto off = m.positions;
    CHECK(!run(d, "transform", {{"translate", std::string("0,1,0")}}).ok);
    CHECK(sameBits(m.positions, off));

    // Base mesh active: the old behaviour (the base and every layer move).
    m.setLayerWeight(1, 1.0f);
    CHECK(run(d, "layer_set_active", {{"index", -1.0}}).ok);
    CHECK(run(d, "transform", {{"translate", std::string("0,1,0")}, {"masked", false}}).ok);
    CHECK(m.base[0] == base[0] + glm::vec3(0, 1, 0));
}

// The gizmo's path: a TransformSession recording into a layer, dragged (applied many times), cancelled, finished.
TEST(transform_session_into_a_layer) {
    Document d = makeDoc(makeIcosphere(3));
    Mesh& m = d.mesh;
    m.addLayer("Pose");
    m.setLayerWeight(0, 0.5f);
    d.syncBvh();
    const auto pos = m.positions, base = m.base;
    TransformSession s;
    CHECK(s.begin(m, {}, Symmetry{false, false, false}, glm::vec3(0.0f), 0));
    MeshTransform t;
    for (int k = 1; k <= 10; ++k) {
        t.rotate = eulerRotation(glm::vec3(0, 0, 9.0f * float(k)));
        t.scale = glm::vec3(1.0f + 0.05f * float(k));
        s.apply(m, t);
    }
    s.restore(m);
    CHECK(maxDistance(m.positions, pos) < 1e-6f);
    s.apply(m, t);
    float err = 0.0f;
    for (size_t v = 0; v < pos.size(); ++v) err = std::max(err, glm::length(m.positions[v] - t.apply(pos[v])));
    CHECK(err < 1e-4f);
    CHECK(sameBits(m.base, base));
    UndoRecord r = s.finish(m, d.bvh, "Gizmo Rotate");
    CHECK(r.layer == 0 && r.layerOffsets.empty());
    d.undo.push(std::move(r));
    CHECK(run(d, "undo").ok);
    CHECK(maxDistance(m.positions, pos) < 1e-6f);
    // Weight 0: nothing to do.
    m.setLayerWeight(0, 0.0f);
    CHECK(!s.begin(m, {}, Symmetry{false, false, false}, glm::vec3(0.0f), 0));
    CHECK(TransformSession::layerWeightZero(m, 0));
}

TEST(transform_leaves_topology_and_attributes) {
    Document d = makeDoc(makeCube(6));
    Mesh& m = d.mesh;
    m.uvs.resize(m.triangleCount() * 3);
    for (size_t i = 0; i < m.uvs.size(); ++i) m.uvs[i] = glm::vec2(float(i % 7) / 7.0f, float(i % 5) / 5.0f);
    m.ensureColors(glm::vec3(0.2f, 0.4f, 0.6f));
    const auto tris = m.triangles;
    const auto uvs = m.uvs;
    const auto quads = m.quadPartners;
    const auto groups = m.facegroups;
    const auto colors = m.colors;
    CHECK(m.quadCount() > 0);
    CHECK(run(d, "transform", {{"rotate", std::string("15,25,35")}, {"scale", 1.3}}).ok);
    CHECK(m.triangles == tris);
    CHECK(sameBits(m.uvs, uvs));
    CHECK(m.quadPartners == quads);
    CHECK(m.facegroups == groups);
    CHECK(sameBits(m.colors, colors));
}

TEST(transform_symmetry_mirrors_a_masked_pose) {
    // A bar along X with both ends free: rotating the right end at x=1 with X symmetry turns the left end the
    // mirrored way, so the result stays symmetric.
    Mesh bar = makeArm(4);
    bar.ensureMask();
    for (uint32_t v = 0; v < bar.vertexCount(); ++v) bar.mask[v] = std::abs(bar.positions[v].x) < 1.0f ? 1.0f : 0.0f;
    Document d = makeDoc(bar);
    CHECK(run(d, "transform", {{"rotate", std::string("0,0,30")}, {"pivot", std::string("1,0,0")}, {"symmetry", std::string("x")}}).ok);
    // Every vertex's mirror image (found on the original) ended up mirrored.
    float err = 0.0f;
    int pairs = 0;
    for (uint32_t v = 0; v < bar.vertexCount(); ++v) {
        const glm::vec3 p = bar.positions[v];
        if (p.x <= 1.0f) continue;
        for (uint32_t u = 0; u < bar.vertexCount(); ++u) {
            const glm::vec3 q = bar.positions[u];
            if (glm::length(q - glm::vec3(-p.x, p.y, p.z)) > 1e-4f) continue;
            const glm::vec3 a = d.mesh.positions[v], b = d.mesh.positions[u];
            err = std::max(err, glm::length(a - glm::vec3(-b.x, b.y, b.z)));
            ++pairs;
            break;
        }
    }
    CHECK(pairs > 10);
    CHECK(err < 1e-4f);
    // The right end went up, so the left one did too.
    CHECK(d.mesh.bounds().max.y > bar.bounds().max.y + 0.2f);
}

TEST(transform_all_parts) {
    Document d = makeDoc(makeIcosphere(2));
    d.addPart(makeCube(4), "Box", false);
    const auto a = d.partMesh(0).positions, b = d.partMesh(1).positions;
    CHECK(run(d, "transform", {{"translate", std::string("0,2,0")}, {"all", true}}).ok);
    CHECK(d.partMesh(0).positions[0] == a[0] + glm::vec3(0, 2, 0));
    CHECK(d.partMesh(1).positions[0] == b[0] + glm::vec3(0, 2, 0));
    // One undo step in each Part.
    CHECK(d.undo.canUndo());
    CHECK(d.parts[1].undo.canUndo());
    d.selectPart(1);
    CHECK(run(d, "undo").ok);
    CHECK(sameBits(d.mesh.positions, b));
    // Without all=1 only the active one moves.
    CHECK(run(d, "transform", {{"translate", std::string("1,0,0")}}).ok);
    CHECK(d.partMesh(0).positions[0] == a[0] + glm::vec3(0, 2, 0));
}

TEST(transform_session_never_accumulates) {
    // A drag applies many transforms to the same originals; the end result equals one direct transform.
    Mesh arm = makeArm(4);
    maskBelowX(arm, 0.0f);
    Document a = makeDoc(arm), b = makeDoc(arm);
    MeshTransform t;
    t.pivot = glm::vec3(0.0f);
    TransformSession s;
    const auto w = transformWeights(a.mesh, true, 0.1f);
    CHECK(s.begin(a.mesh, w, Symmetry{false, false, false}, t.pivot));
    for (int i = 1; i <= 40; ++i) {
        t.rotate = glm::angleAxis(glm::radians(float(i) * 2.0f), glm::vec3(0, 0, 1));
        t.translate = glm::vec3(0.01f * float(i), 0, 0);
        s.apply(a.mesh, t);
    }
    a.undo.push(s.finish(a.mesh, a.bvh, "Rotate"));
    CHECK(transformPart(b, 0, t, true, 0.1f, Symmetry{false, false, false}, "Rotate") > 0);
    CHECK(sameBits(a.mesh.positions, b.mesh.positions));
    CHECK(maxDistance(a.mesh.normals, b.mesh.normals) < 1e-5f);
    // An identity drag (click without moving) leaves no undo step and no change.
    Document c = makeDoc(arm);
    TransformSession idle;
    CHECK(idle.begin(c.mesh, {}, Symmetry{}, glm::vec3(0.0f)));
    idle.apply(c.mesh, MeshTransform{});
    CHECK(idle.finish(c.mesh, c.bvh, "Move").empty());
    CHECK(sameBits(c.mesh.positions, arm.positions));
}

TEST(transform_bvh_follows) {
    Document d = makeDoc(makeIcosphere(3));
    CHECK(run(d, "transform", {{"translate", std::string("10,0,0")}}).ok);
    RayHit hit;
    CHECK(d.bvh.raycast(d.mesh, glm::vec3(10, 0, 5), glm::vec3(0, 0, -1), hit));
    CHECK(std::abs(hit.position.z - 1.0f) < 0.05f);
    CHECK(!d.bvh.raycast(d.mesh, glm::vec3(0, 0, 5), glm::vec3(0, 0, -1), hit));
    CHECK(run(d, "undo").ok);
    CHECK(d.bvh.raycast(d.mesh, glm::vec3(0, 0, 5), glm::vec3(0, 0, -1), hit));
}

TEST(transform_command_errors) {
    Document d = makeDoc(makeIcosphere(2));
    CHECK(!run(d, "transform", {{"scale", 0.0}}).ok);
    CHECK(!run(d, "transform", {{"scale", std::string("1,-1,1")}}).ok);
    CHECK(!run(d, "transform", {{"pivot", std::string("nowhere")}, {"translate", std::string("1,0,0")}}).ok);
    CHECK(!run(d, "transform", {{"translate", std::string("1,0")}}).ok);
    CHECK(!run(d, "transform", {{"symmetry", std::string("w")}, {"translate", std::string("1,0,0")}}).ok);
    CHECK(run(d, "transform").ok); // nothing to do is fine
    CHECK(!d.undo.canUndo());
}

TEST(transform_euler_round_trip) {
    for (const glm::vec3 e : {glm::vec3(10, 20, 30), glm::vec3(-45, 80, 170), glm::vec3(0, 0, -90)}) {
        const glm::vec3 back = eulerDegrees(eulerRotation(e));
        CHECK(glm::length(back - e) < 1e-2f);
    }
}

TEST(transform_perf_million_triangles) {
    // 1.3M triangles: a whole-Part drag frame and a masked (posing) drag frame, plus begin / finish.
    Mesh m = makeIcosphere(8);
    maskBelowX(m, 0.3f);
    Document d = makeDoc(std::move(m));
    using clock = std::chrono::steady_clock;
    auto ms = [](clock::time_point a) { return std::chrono::duration<double, std::milli>(clock::now() - a).count(); };
    for (int masked = 0; masked < 2; ++masked) {
        auto t0 = clock::now();
        const auto w = transformWeights(d.mesh, masked != 0, masked ? 0.1f : 0.0f);
        TransformSession s;
        CHECK(s.begin(d.mesh, w, Symmetry{}, transformCenter(d.mesh, w)));
        const double begin = ms(t0);
        MeshTransform t;
        t.pivot = transformCenter(d.mesh, w);
        t0 = clock::now();
        const int frames = 10;
        for (int i = 1; i <= frames; ++i) {
            t.rotate = glm::angleAxis(glm::radians(float(i)), glm::vec3(0, 1, 0));
            s.apply(d.mesh, t);
        }
        const double frame = ms(t0) / frames;
        const size_t moving = s.movingVertices();
        t0 = clock::now();
        d.undo.push(s.finish(d.mesh, d.bvh, "Rotate"));
        const double finish = ms(t0);
        t0 = clock::now();
        CHECK(run(d, "undo").ok);
        const double undo = ms(t0);
        std::printf("  %zu tris, %s: begin %.1f ms, %.1f ms per drag frame (%zu verts), finish %.1f ms, undo %.1f ms\n",
                    d.mesh.triangleCount(), masked ? "masked + falloff 0.1" : "whole Part", begin, frame,
                    moving, finish, undo);
    }
}

TEST(transform_saved_in_projects) {
    // A posed Part with a sculpt layer and quads saves and opens as it is.
    Document d = makeDoc(makeCube(6));
    d.mesh.addLayer("Detail");
    for (uint32_t v = 0; v < d.mesh.vertexCount(); ++v) d.mesh.layers[0].delta[v] = d.mesh.normals[v] * 0.05f;
    d.mesh.setLayerWeight(0, 1.0f);
    maskBelowX(d.mesh, 0.0f);
    CHECK(run(d, "transform", {{"rotate", std::string("0,0,45")}, {"pivot", std::string("0,0,0")}, {"falloff", 0.2}}).ok);
    const std::string path = tempPath("sf_transform_test.sfproj");
    std::string err;
    CHECK(saveProject(path, d, err));
    Document e;
    CHECK(loadProject(path, e, err));
    CHECK(sameBits(e.mesh.positions, d.mesh.positions));
    CHECK(sameBits(e.mesh.base, d.mesh.base));
    CHECK(e.mesh.layers.size() == 1 && sameBits(e.mesh.layers[0].delta, d.mesh.layers[0].delta));
    CHECK(e.mesh.quadPartners == d.mesh.quadPartners);
    std::filesystem::remove(path);
}
