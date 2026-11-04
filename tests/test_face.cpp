// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Facial shapes (core/Face.h): the ARKit names, the template, the fit (landmark error on a known head, with and
// without landmarks, on a reshaped and re-topologised head), deformation transfer on the template itself
// (near-identity), mirrored Left / Right pairs, Parts (eyes, teeth), multires, face_weights (exact, no undo,
// timing on a 500k head), takes, and export round trips that keep the morph targets, their names and the take.
#include "TestHarness.h"

#include "core/Commands.h"
#include "core/Decimate.h"
#include "core/Face.h"
#include "core/Multires.h"
#include "core/Primitives.h"
#include "core/Project.h"
#include "core/ThreadPool.h"
#include "io/BlenderBridge.h"
#include "io/FbxIO.h"
#include "io/GltfIO.h"
#include "io/UsdIO.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <sstream>

using namespace sf;

namespace {

const CommandRegistry& registry() {
    static CommandRegistry r;
    return r;
}

CommandResult run(Document& d, const std::string& name, const Args& a = {}) {
    CommandResult r = registry().execute(d, name, a);
    if (!r.ok) std::printf("  (%s: %s)\n", name.c_str(), r.message.c_str());
    return r;
}

// The template's skin (face, head and neck; no mouth or eye sockets) as a sculpt, in metres (the template is in
// centimetres), optionally reshaped by `warp` (template cm -> sculpt units).
template <class Warp>
Mesh templateHead(Warp warp) {
    const FaceTemplate& t = faceTemplate();
    Mesh m;
    for (uint32_t i = 0; i < t.skinEnd; ++i) m.positions.push_back(warp(t.neutral[i]));
    for (const auto& tri : t.triangles)
        if (tri.x < t.skinEnd && tri.y < t.skinEnd && tri.z < t.skinEnd) m.triangles.push_back(tri);
    m.rebuildTopology();
    m.recomputeAllNormals();
    return m;
}
Mesh templateHead() {
    return templateHead([](const glm::vec3& p) { return p * 0.01f; });
}

// A different person: wider face, longer jaw, bigger nose, smaller head, turned 8 degrees and moved; metres.
glm::vec3 otherPerson(const glm::vec3& p) {
    glm::vec3 q = p;
    q.x *= 1.1f;
    if (q.y < -2.0f) q.y = -2.0f + (q.y + 2.0f) * 1.12f;                           // longer lower face
    const float nose = std::exp(-(q.x * q.x + (q.y + 0.5f) * (q.y + 0.5f)) / 4.0f); // around the nose
    q.z += 0.6f * nose * std::max(q.z - 9.0f, 0.0f) / 4.0f;
    const glm::mat4 r = glm::rotate(glm::mat4(1.0f), glm::radians(8.0f), glm::vec3(0, 1, 0));
    return glm::vec3(r * glm::vec4(q * 0.0095f, 1.0f)) + glm::vec3(0.05f, 1.6f, -0.02f);
}

float maxLength(const std::vector<glm::vec3>& v) {
    float m = 0.0f;
    for (const auto& d : v) m = std::max(m, glm::length(d));
    return m;
}

const SculptLayer* findLayer(const Mesh& m, const std::string& name) {
    for (const auto& l : m.layers)
        if (l.name == name) return &l;
    return nullptr;
}

// The template's offsets of shape `name`, dense, scaled to `scale`.
std::vector<glm::vec3> templateShape(const std::string& name, size_t count, float scale) {
    const FaceTemplate& t = faceTemplate();
    std::vector<glm::vec3> d(count, glm::vec3(0.0f));
    const auto& s = t.shapes[size_t(arkitBlendShapeIndex(name))];
    for (size_t k = 0; k < s.index.size(); ++k)
        if (s.index[k] < count) d[s.index[k]] = s.offset[k] * scale;
    return d;
}

} // namespace

TEST(face_arkit_names_are_exact) {
    // Apple ARKit's ARFaceAnchor.BlendShapeLocation names (MediaPipe uses the same, without tongueOut), typed out
    // again here so a typo in the list can't hide.
    const char* expected[] = {
        "browDownLeft", "browDownRight", "browInnerUp", "browOuterUpLeft", "browOuterUpRight", "cheekPuff",
        "cheekSquintLeft", "cheekSquintRight", "eyeBlinkLeft", "eyeBlinkRight", "eyeLookDownLeft", "eyeLookDownRight",
        "eyeLookInLeft", "eyeLookInRight", "eyeLookOutLeft", "eyeLookOutRight", "eyeLookUpLeft", "eyeLookUpRight",
        "eyeSquintLeft", "eyeSquintRight", "eyeWideLeft", "eyeWideRight", "jawForward", "jawLeft", "jawOpen",
        "jawRight", "mouthClose", "mouthDimpleLeft", "mouthDimpleRight", "mouthFrownLeft", "mouthFrownRight",
        "mouthFunnel", "mouthLeft", "mouthLowerDownLeft", "mouthLowerDownRight", "mouthPressLeft", "mouthPressRight",
        "mouthPucker", "mouthRight", "mouthRollLower", "mouthRollUpper", "mouthShrugLower", "mouthShrugUpper",
        "mouthSmileLeft", "mouthSmileRight", "mouthStretchLeft", "mouthStretchRight", "mouthUpperUpLeft",
        "mouthUpperUpRight", "noseSneerLeft", "noseSneerRight", "tongueOut"};
    const auto& names = arkitBlendShapeNames();
    CHECK(names.size() == 52);
    CHECK(std::size(expected) == 52);
    for (size_t i = 0; i < names.size() && i < std::size(expected); ++i) CHECK(names[i] == expected[i]);
    CHECK(std::is_sorted(names.begin(), names.end()));
    CHECK(arkitMirrorName("eyeBlinkLeft") == "eyeBlinkRight");
    CHECK(arkitMirrorName("jawRight") == "jawLeft");
    CHECK(arkitMirrorName("jawOpen") == "jawOpen");
    for (const auto& n : names) CHECK(arkitBlendShapeIndex(arkitMirrorName(n)) >= 0);
    // The template has exactly these shapes, each moving something, in this order.
    const FaceTemplate& t = faceTemplate();
    CHECK(t.shapes.size() == 52);
    for (size_t i = 0; i < t.shapes.size() && i < names.size(); ++i) {
        CHECK(t.shapes[i].name == names[i]);
        CHECK(!t.shapes[i].index.empty());
    }
    CHECK(t.neutral.size() == 14062);
    CHECK(t.skinEnd == 11248);
}

TEST(face_template_shapes_look_right) {
    // Left is +X (the character's left), jawOpen drops the chin, blinks close the upper lid, the jaw shapes move
    // the lower teeth.
    const FaceTemplate& t = faceTemplate();
    auto centroid = [&](const std::string& n) {
        const auto& s = t.shapes[size_t(arkitBlendShapeIndex(n))];
        glm::vec3 c(0.0f), d(0.0f);
        float w = 0.0f;
        for (size_t k = 0; k < s.index.size(); ++k) {
            const float l = glm::length(s.offset[k]);
            c += l * t.neutral[s.index[k]];
            d += s.offset[k];
            w += l;
        }
        return std::make_pair(c / w, d / float(s.index.size()));
    };
    CHECK(centroid("eyeBlinkLeft").first.x > 2.0f);
    CHECK(centroid("eyeBlinkRight").first.x < -2.0f);
    CHECK(centroid("eyeBlinkLeft").second.y < 0.0f);
    CHECK(centroid("mouthSmileLeft").first.x > 1.0f);
    CHECK(centroid("jawOpen").second.y < -0.2f);
    CHECK(centroid("jawLeft").second.x > 0.05f); // towards the character's left
    const auto& jaw = t.shapes[size_t(arkitBlendShapeIndex("jawOpen"))];
    CHECK(std::abs(jaw.jawRotation.w) < 0.999f); // the lower teeth turn
    const auto& blink = t.shapes[size_t(arkitBlendShapeIndex("eyeBlinkLeft"))];
    CHECK(blink.jawTranslation == glm::vec3(0.0f));
    // Landmarks sit where they should.
    CHECK(t.neutral[faceLandmarkVertex(kEyeOuterLeft)].x > 3.0f);
    CHECK(t.neutral[faceLandmarkVertex(kEyeOuterRight)].x < -3.0f);
    CHECK(t.neutral[faceLandmarkVertex(kChin)].y < t.neutral[faceLandmarkVertex(kMouthLeft)].y);
}

TEST(face_transfer_on_the_template_is_near_identity) {
    // Deformation transfer onto the template itself gives the template's shapes back.
    const FaceTemplate& t = faceTemplate();
    FaceFit fit;
    fit.positions = t.neutral;
    std::vector<std::vector<glm::vec3>> off;
    transferFaceShapes(fit, off);
    CHECK(off.size() == 52);
    float worst = 0.0f;
    for (size_t k = 0; k < off.size(); ++k) {
        const auto ref = templateShape(t.shapes[k].name, t.neutral.size(), 1.0f);
        float err = 0.0f;
        for (size_t i = 0; i < ref.size(); ++i) err = std::max(err, glm::length(off[k][i] - ref[i]));
        worst = std::max(worst, err / std::max(maxLength(ref), 1e-6f));
    }
    std::printf("  transfer on the template: worst error %.2e of the shape's peak\n", double(worst));
    CHECK(worst < 1e-3f);

    // Through a similarity (scaled to metres, turned, moved): the same shapes, scaled and turned.
    fit.align.scale = 0.01f;
    fit.align.rotation = glm::mat3(glm::rotate(glm::mat4(1.0f), 0.3f, glm::vec3(0, 1, 0)));
    fit.align.translation = glm::vec3(1, 2, 3);
    for (size_t i = 0; i < t.neutral.size(); ++i) fit.positions[i] = fit.align.apply(t.neutral[i]);
    transferFaceShapes(fit, off);
    const size_t jaw = size_t(arkitBlendShapeIndex("jawOpen"));
    const auto ref = templateShape("jawOpen", t.neutral.size(), 1.0f);
    float err = 0.0f;
    for (size_t i = 0; i < ref.size(); ++i) err = std::max(err, glm::length(off[jaw][i] - fit.align.applyVector(ref[i])));
    CHECK(err < 1e-3f * 0.01f * maxLength(ref));
}

TEST(face_fit_finds_the_landmarks_on_a_known_head) {
    // The template head reshaped (another person), re-topologised (decimated to 60%) and moved: the automatic fit
    // puts each landmark within 3% of the head's height of where it really is; given landmarks, within 1%.
    const FaceTemplate& t = faceTemplate();
    Mesh head = decimate(templateHead(otherPerson), templateHead(otherPerson).triangleCount() * 6 / 10);
    const float height = 34.0f * 0.0095f;
    FaceFit fit;
    std::string error;
    CHECK(fitFaceTemplate(head.positions, head.triangles, nullptr, fit, error));
    float worst = 0.0f;
    for (int l = 0; l < kFaceLandmarkCount; ++l) {
        const glm::vec3 truth = otherPerson(t.neutral[faceLandmarkVertex(l)]);
        worst = std::max(worst, glm::distance(fit.landmarks.position[size_t(l)], truth));
    }
    std::printf("  automatic fit: worst landmark %.2f%% of the head, rms %.2f%%, coverage %.0f%%\n",
                double(worst / height * 100.0f), double(fit.rmsError / height * 100.0f), double(fit.coverage * 100.0f));
    CHECK(worst < 0.03f * height);
    CHECK(fit.rmsError < 0.01f * height);
    CHECK(fit.coverage > 0.8f);

    // With the landmarks placed (as the artist would click them, a little off): closer still.
    FaceFitLandmarks lm;
    for (int l = 0; l < kFaceLandmarkCount; ++l) {
        lm.position[size_t(l)] = otherPerson(t.neutral[faceLandmarkVertex(l)]) + glm::vec3(0.0005f, -0.0003f, 0.0f);
        lm.set[size_t(l)] = true;
    }
    FaceFit guided;
    CHECK(fitFaceTemplate(head.positions, head.triangles, &lm, guided, error));
    float worstGuided = 0.0f;
    for (int l = 0; l < kFaceLandmarkCount; ++l)
        worstGuided = std::max(worstGuided, glm::distance(guided.landmarks.position[size_t(l)],
                                                          otherPerson(t.neutral[faceLandmarkVertex(l)])));
    std::printf("  guided fit: worst landmark %.2f%% of the head\n", double(worstGuided / height * 100.0f));
    CHECK(worstGuided < 0.01f * height);

    // Too few landmarks is an error, not a bad fit.
    FaceFitLandmarks two;
    two.set[kNoseTip] = two.set[kChin] = true;
    CHECK(!fitFaceTemplate(head.positions, head.triangles, &two, guided, error));
    // A sphere has no face: the fit runs but finds little of the template on it.
    Mesh ball = makeIcosphere(4, 0.15f);
    FaceFit none;
    if (fitFaceTemplate(ball.positions, ball.triangles, nullptr, none, error)) CHECK(none.rmsError > 0.005f || none.coverage < 0.8f);
}

TEST(face_generate_on_the_template_matches_its_shapes) {
    // End to end on the template's own head: the generated layers are the template's shapes (in metres).
    Document doc = makeDoc(templateHead());
    const size_t nv = doc.mesh.vertexCount();
    const auto before = doc.mesh.positions;
    const size_t undoDepth = doc.undo.undoCount();
    CHECK(run(doc, "face_shapes_generate").ok);
    CHECK(doc.mesh.layers.size() == 52);
    CHECK(doc.undo.undoCount() == undoDepth + 1);
    for (size_t k = 0; k < doc.mesh.layers.size(); ++k) {
        CHECK(doc.mesh.layers[k].name == arkitBlendShapeNames()[k]);
        CHECK(doc.mesh.layers[k].weight == 0.0f);
    }
    CHECK(doc.mesh.positions == before); // all at weight 0: the sculpt looks the same
    for (const char* name : {"jawOpen", "eyeBlinkLeft", "mouthSmileRight", "browInnerUp"}) {
        const auto ref = templateShape(name, nv, 0.01f);
        const SculptLayer* l = findLayer(doc.mesh, name);
        CHECK(l != nullptr);
        if (!l) continue;
        float err = 0.0f;
        for (uint32_t i = 0; i < faceTemplate().faceEnd; ++i) err = std::max(err, glm::length(l->delta[i] - ref[i]));
        std::printf("  %s on the template: max error %.1f%% of its peak\n", name, double(err / maxLength(ref) * 100.0f));
        CHECK(err < 0.1f * maxLength(ref));
    }
    // Generating again replaces the 52 layers instead of adding more; other layers stay.
    CHECK(run(doc, "layer_add", {{"name", std::string("Corrective")}}).ok);
    CHECK(run(doc, "face_shapes_generate").ok);
    CHECK(doc.mesh.layers.size() == 53);
    CHECK(findLayer(doc.mesh, "Corrective") != nullptr);
}

TEST(face_symmetric_pairs_mirror) {
    // The template head is symmetric about X = 0: Left and Right shapes are exact mirror images, and centre shapes
    // are symmetric themselves, on a reshaped head too.
    Document doc = makeDoc(templateHead([](const glm::vec3& p) {
        glm::vec3 q = p * 0.0105f;
        q.y *= 1.08f; // a longer face, still symmetric
        return q;
    }));
    CHECK(run(doc, "face_shapes_generate").ok);
    const Mesh& m = doc.mesh;
    // Each vertex's mirror image.
    std::vector<uint32_t> mirror(m.vertexCount(), ~0u);
    for (uint32_t v = 0; v < m.vertexCount(); ++v) {
        const glm::vec3 q(-m.positions[v].x, m.positions[v].y, m.positions[v].z);
        float best = 1e9f;
        for (uint32_t u = 0; u < m.vertexCount(); ++u) {
            const float d = glm::distance(m.positions[u], q);
            if (d < best) best = d, mirror[v] = u;
        }
        if (best > 1e-5f) mirror[v] = ~0u;
        if (v > 3000) break; // (a sample is enough, and keeps this quadratic search quick)
    }
    auto flip = [](const glm::vec3& d) { return glm::vec3(-d.x, d.y, d.z); };
    float worstPair = 0.0f, worstCentre = 0.0f, peak = 0.0f;
    for (const char* name : {"eyeBlinkLeft", "mouthSmileLeft", "browOuterUpLeft", "jawLeft"}) {
        const SculptLayer* a = findLayer(m, name);
        const SculptLayer* b = findLayer(m, arkitMirrorName(name));
        CHECK(a && b);
        if (!a || !b) continue;
        peak = std::max(peak, maxLength(a->delta));
        for (uint32_t v = 0; v <= 3000 && v < m.vertexCount(); ++v)
            if (mirror[v] != ~0u) worstPair = std::max(worstPair, glm::length(a->delta[v] - flip(b->delta[mirror[v]])));
    }
    for (const char* name : {"jawOpen", "mouthPucker", "browInnerUp"}) {
        const SculptLayer* a = findLayer(m, name);
        for (uint32_t v = 0; a && v <= 3000 && v < m.vertexCount(); ++v)
            if (mirror[v] != ~0u) worstCentre = std::max(worstCentre, glm::length(a->delta[v] - flip(a->delta[mirror[v]])));
    }
    std::printf("  mirror error: pairs %.2e, centre %.2e (peak %.2e)\n", double(worstPair), double(worstCentre), double(peak));
    CHECK(worstPair < 1e-4f * peak);
    CHECK(worstCentre < 1e-4f * peak);
    // Symmetry off: still close (the template is symmetric) but not forced.
    CHECK(run(doc, "face_shapes_generate", {{"symmetry", std::string("off")}}).ok);
}

TEST(face_parts_eyes_and_teeth) {
    // Eyes turn about their centres for eyeLook*, the lower teeth follow jawOpen and the upper teeth don't, and
    // brows on the face follow the skin.
    const FaceTemplate& t = faceTemplate();
    Document doc = makeDoc(templateHead());
    auto at = [&](const glm::vec3& cm) { return cm * 0.01f; };
    // Eyeballs where the template's eyes are (between the corners, a little behind).
    Mesh eyes;
    for (int side = 0; side < 2; ++side) {
        const glm::vec3 c = 0.5f * (t.neutral[faceLandmarkVertex(side ? kEyeOuterLeft : kEyeOuterRight)] +
                                    t.neutral[faceLandmarkVertex(side ? kEyeInnerLeft : kEyeInnerRight)]) -
                            glm::vec3(0, 0, 1.0f);
        Mesh ball = makeIcosphere(3, 0.012f);
        const uint32_t first = uint32_t(eyes.positions.size());
        for (auto& p : ball.positions) eyes.positions.push_back(p + at(c));
        for (auto tri : ball.triangles) eyes.triangles.push_back(tri + glm::uvec3(first));
    }
    eyes.rebuildTopology();
    eyes.recomputeAllNormals();
    doc.addPart(eyes, "Eyes", false);
    // Teeth: an upper and a lower row of little boxes behind the lips.
    Mesh teeth;
    for (int row = 0; row < 2; ++row) {
        Mesh box = makeCube(1, 0.004f);
        for (int k = -3; k <= 3; ++k) {
            const glm::vec3 c = at(glm::vec3(float(k) * 0.6f, row ? t.upperTeethY : t.lowerTeethCentroid.y,
                                             t.lowerTeethCentroid.z + 0.8f - 0.05f * float(k * k)));
            const uint32_t first = uint32_t(teeth.positions.size());
            for (auto& p : box.positions) teeth.positions.push_back(p + c);
            for (auto tri : box.triangles) teeth.triangles.push_back(tri + glm::uvec3(first));
        }
    }
    teeth.rebuildTopology();
    teeth.recomputeAllNormals();
    doc.addPart(teeth, "Teeth", false);
    doc.addPart(makeIcosphere(2, 0.5f), "Far away ball", false);
    for (auto& p : doc.parts[3].mesh.positions) p += glm::vec3(3, 0, 0);
    CHECK(run(doc, "face_shapes_generate").ok);

    const Mesh& em = doc.parts[1].mesh;
    const SculptLayer* up = findLayer(em, "eyeLookUpLeft");
    const SculptLayer* out = findLayer(em, "eyeLookOutLeft");
    CHECK(up && out && em.layers.size() == 8);
    if (up && out) {
        // The front of the left eye (largest z among x > 0) goes up; the right eye doesn't move.
        uint32_t front = 0, rightFront = 0;
        for (uint32_t v = 0; v < em.vertexCount(); ++v) {
            if (em.positions[v].x > 0 && em.positions[v].z > em.positions[front].z) front = v;
            if (em.positions[v].x < 0 && (em.positions[rightFront].x >= 0 || em.positions[v].z > em.positions[rightFront].z))
                rightFront = v;
        }
        CHECK(em.positions[front].x > 0.0f);
        CHECK(up->delta[front].y > 0.003f);
        CHECK(out->delta[front].x > 0.003f); // looking out = towards the character's left
        CHECK(up->delta[rightFront] == glm::vec3(0.0f));
    }
    const Mesh& tm = doc.parts[2].mesh;
    const SculptLayer* jaw = findLayer(tm, "jawOpen");
    CHECK(jaw != nullptr);
    if (jaw) {
        float lowerMove = 0.0f, upperMove = 0.0f;
        for (uint32_t v = 0; v < tm.vertexCount(); ++v) {
            const bool lowerRow = v < tm.vertexCount() / 2;
            (lowerRow ? lowerMove : upperMove) = std::max(lowerRow ? lowerMove : upperMove, glm::length(jaw->delta[v]));
        }
        std::printf("  teeth on jawOpen: lower %.1f mm, upper %.1f mm\n", double(lowerMove * 1000), double(upperMove * 1000));
        CHECK(lowerMove > 0.005f);
        CHECK(upperMove == 0.0f);
    }
    CHECK(findLayer(tm, "eyeBlinkLeft") == nullptr); // teeth only get the jaw shapes
    CHECK(doc.parts[3].mesh.layers.empty());      // not on the face: left alone

    // face_weights drives every Part: the eyes turn with eyeLookUpLeft.
    const auto eyesBefore = doc.parts[1].mesh.positions;
    CHECK(run(doc, "face_weights", {{"json", std::string("{\"eyeLookUpLeft\":1,\"jawOpen\":0.5}")}}).ok);
    CHECK(doc.parts[1].mesh.positions != eyesBefore);
}

TEST(face_weights_fast_path_is_exact_and_not_undoable) {
    Document doc = makeDoc(templateHead());
    CHECK(run(doc, "face_shapes_generate").ok);
    const size_t undoDepth = doc.undo.undoCount();
    doc.markClean();
    CHECK(run(doc, "face_weights", {{"json", std::string("{\"jawOpen\":0.7,\"eyeBlinkLeft\":1,\"mouthSmileRight\":0.4}")}}).ok);
    CHECK(doc.undo.undoCount() == undoDepth);
    CHECK(!doc.dirty());
    CHECK(findLayer(doc.mesh, "jawOpen")->weight == 0.7f);
    // The same positions and normals as a full evaluation.
    Mesh full = doc.mesh;
    full.evaluateAll();
    full.recomputeAllNormals();
    CHECK(full.positions == doc.mesh.positions);
    float nerr = 0.0f;
    for (size_t i = 0; i < full.normals.size(); ++i) nerr = std::max(nerr, glm::length(full.normals[i] - doc.mesh.normals[i]));
    CHECK(nerr < 1e-5f);
    // Only what moved is marked for the GPU (not the whole mesh).
    doc.mesh.clearDirty();
    CHECK(run(doc, "face_weights", {{"json", std::string("{\"eyeBlinkLeft\":0.5}")}}).ok);
    size_t dirty = 0;
    for (uint32_t v = 0; v < doc.mesh.vertexCount(); ++v) dirty += doc.mesh.vertexDirty(v);
    CHECK(dirty > 0 && dirty < doc.mesh.vertexCount() / 3);
    // Presets set all 52; unknown names are reported; bad JSON is an error.
    CHECK(run(doc, "face_preset", {{"name", std::string("smile")}}).ok);
    CHECK(findLayer(doc.mesh, "jawOpen")->weight == 0.0f);
    CHECK(findLayer(doc.mesh, "mouthSmileLeft")->weight == 0.8f);
    CHECK(registry().execute(doc, "face_weights", {{"json", std::string("{\"notAShape\":1,\"jawOpen\":0.1}")}}).message.find("notAShape") !=
          std::string::npos);
    CHECK(!registry().execute(doc, "face_weights", {{"json", std::string("{jawOpen:1")}}).ok);
    CHECK(!registry().execute(doc, "face_preset", {{"name", std::string("Nope")}}).ok);
    // Without shapes it says what to do.
    Document plain = makeDoc(makeIcosphere(2));
    CHECK(registry().execute(plain, "face_weights", {{"json", std::string("{\"jawOpen\":1}")}}).message.find("Generate") !=
          std::string::npos);
    // After a stroke into a layer (new undo revision) the next call sees the layer's new vertices.
    SculptLayer& smile = doc.mesh.layers[size_t(arkitBlendShapeIndex("mouthSmileLeft"))];
    smile.delta[0] = glm::vec3(0.01f, 0, 0); // vertex 0 wasn't moved by the smile before
    UndoRecord rec;
    rec.label = "Stroke";
    doc.undo.push(std::move(rec));
    CHECK(run(doc, "face_weights", {{"json", std::string("{\"mouthSmileLeft\":0.2}")}}).ok);
    Mesh again = doc.mesh;
    again.evaluateAll();
    CHECK(again.positions == doc.mesh.positions);
}

TEST(face_weights_timing_on_a_500k_head) {
    // A 720k-vertex head (the template skin divided three times) with the 52 shapes: a live frame that changes
    // every weight costs a few milliseconds and touches only the face.
    Mesh base = templateHead();
    std::string error;
    for (int i = 0; i < 2; ++i) CHECK(subdivDivide(base, true, error));
    Mesh head = subdivLevelMesh(base, subdivCurrentLevel(base));
    head.multires.reset();
    Mesh big = head;
    CHECK(subdivDivide(big, true, error));
    head = subdivLevelMesh(big, 1);
    head.multires.reset();
    head.rebuildTopology();
    head.recomputeAllNormals();
    std::printf("  head: %zu vertices\n", head.vertexCount());
    CHECK(head.vertexCount() > 500000);
    Document doc = makeDoc(std::move(head));
    auto t0 = std::chrono::steady_clock::now();
    CHECK(run(doc, "face_shapes_generate").ok);
    const double genMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    const auto& names = arkitBlendShapeNames();
    double total = 0.0, worst = 0.0;
    size_t verts = 0;
    const int frames = 30;
    for (int f = 0; f < frames; ++f) {
        std::string json = "{";
        for (size_t k = 0; k < names.size(); ++k) {
            char buf[64];
            std::snprintf(buf, sizeof buf, "%s\"%s\":%.3f", k ? "," : "", names[k].c_str(),
                          0.5 + 0.5 * std::sin(0.3 * double(f) + double(k)));
            json += buf;
        }
        json += "}";
        doc.mesh.clearDirty();
        const auto s = std::chrono::steady_clock::now();
        CHECK(registry().execute(doc, "face_weights", {{"json", json}}).ok);
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - s).count();
        if (f > 0) { // the first call finds each layer's vertices
            total += ms;
            worst = std::max(worst, ms);
        }
        verts = 0;
        for (uint32_t v = 0; v < doc.mesh.vertexCount(); v += 7) verts += doc.mesh.vertexDirty(v);
    }
    const double avg = total / (frames - 1);
    std::printf("  generate %.0f ms; face_weights (all 52 changing): %.2f ms average, %.2f ms worst, ~%zu%% of the "
                "vertices re-uploaded\n",
                genMs, avg, worst, verts * 7 * 100 / doc.mesh.vertexCount());
    CHECK(verts * 7 < doc.mesh.vertexCount()); // the face, not the whole head
    if (std::getenv("SF_BENCH_LIMITS")) CHECK(avg < 15.0);
    Mesh full = doc.mesh;
    full.evaluateAll();
    CHECK(full.positions == doc.mesh.positions);
}

TEST(face_generation_is_deterministic_with_threads) {
    Mesh head = templateHead(otherPerson);
    Document a = makeDoc(head), b = makeDoc(head);
    ThreadPool::shared().setSerial(true);
    CHECK(run(a, "face_shapes_generate").ok);
    ThreadPool::shared().setSerial(false);
    CHECK(run(b, "face_shapes_generate").ok);
    CHECK(a.mesh.layers.size() == b.mesh.layers.size());
    bool same = true;
    for (size_t k = 0; k < a.mesh.layers.size() && k < b.mesh.layers.size(); ++k) same &= a.mesh.layers[k].delta == b.mesh.layers[k].delta;
    CHECK(same);
}

TEST(face_shapes_on_subdivision_levels) {
    // A head with 3 levels (11k, 45k, 180k vertices): the shapes are made on SDiv 2 (the lowest with 20k) and show
    // at the top level, where the top level's own detail is kept at weight 0.
    Document doc = makeDoc(templateHead());
    std::string msg;
    CHECK(divideUndoable(doc, true, msg));
    CHECK(divideUndoable(doc, true, msg));
    // Detail at the top: a bump on the forehead.
    for (uint32_t v = 0; v < doc.mesh.vertexCount(); ++v)
        if (doc.mesh.positions[v].y > 0.08f && doc.mesh.positions[v].z > 0.05f) doc.mesh.positions[v].z += 0.002f;
    doc.mesh.recomputeAllNormals();
    const auto detailed = doc.mesh.positions;
    const CommandResult r = run(doc, "face_shapes_generate");
    CHECK(r.ok);
    CHECK(r.message.find("SDiv 2") != std::string::npos);
    CHECK(subdivCurrentLevel(doc.mesh) == 2);
    CHECK(doc.mesh.layers.size() == 52);
    float detailErr = 0.0f;
    for (size_t v = 0; v < detailed.size(); ++v) detailErr = std::max(detailErr, glm::distance(detailed[v], doc.mesh.positions[v]));
    CHECK(detailErr < 1e-5f);
    const SculptLayer* jaw = findLayer(doc.mesh, "jawOpen");
    CHECK(jaw && maxLength(jaw->delta) > 0.02f);
    // Down to SDiv 1 and the layers are there too.
    CHECK(setSubdivLevelUndoable(doc, 0, msg));
    CHECK(doc.mesh.layers.size() == 52);
}

TEST(face_takes_parse_bake_and_export) {
    // A take as the phone link records it: a JSON array of face messages (ms timestamps); also {"frames":[...]}
    // and JSON Lines.
    const std::string take =
        "[{\"type\":\"face\",\"t\":1000,\"weights\":{\"jawOpen\":0,\"eyeBlinkLeft\":1},\"head\":[0,0,0,1]},"
        "{\"type\":\"face\",\"t\":1100,\"weights\":{\"jawOpen\":0.5}},"
        "{\"type\":\"hello\"},"
        "{\"type\":\"face\",\"t\":1200,\"weights\":{\"jawOpen\":1,\"_neutral\":0.2}}]";
    MorphAnimation a;
    std::string error;
    CHECK(parseFaceTake(take, a, error));
    CHECK(a.times.size() == 3);
    CHECK(a.channels.size() == 52);
    CHECK(std::abs(a.times[2] - 0.2f) < 1e-6f);
    CHECK(a.value(1, size_t(a.channel("jawOpen"))) == 0.5f);
    CHECK(a.value(0, size_t(a.channel("eyeBlinkLeft"))) == 1.0f);
    MorphAnimation b;
    CHECK(parseFaceTake("{\"frames\":[{\"t\":0,\"weights\":{\"jawOpen\":1}},{\"t\":40,\"weights\":{}}]}", b, error));
    CHECK(b.times.size() == 2);
    CHECK(parseFaceTake("{\"type\":\"face\",\"t\":5,\"weights\":{\"jawOpen\":0.1}}\n{\"type\":\"face\",\"t\":38,\"weights\":{\"jawOpen\":0.2}}\n", b, error));
    CHECK(b.times.size() == 2);
    CHECK(!parseFaceTake("[]", b, error));
    std::vector<std::pair<std::string, float>> w;
    sampleMorphAnimation(a, 0.05f, w);
    CHECK(std::abs(w[size_t(a.channel("jawOpen"))].second - 0.25f) < 1e-5f);
    const MorphAnimation r = resampleMorphAnimation(a, 30.0f);
    CHECK(r.times.size() == 7); // 0 .. 0.2 s at 30 fps

    // Bake into a document with the shapes, export GLB and USD, read them back.
    Document doc = makeDoc(templateHead());
    CHECK(run(doc, "face_shapes_generate").ok);
    const std::string takePath = tempPath("sf_face_take.json");
    {
        std::ofstream f(takePath, std::ios::binary);
        f << take;
    }
    CHECK(run(doc, "face_take_bake", {{"path", takePath}, {"fps", 30.0}}).ok);
    MorphAnimation baked;
    CHECK(bakedFaceAnimation(doc, baked));
    CHECK(baked.times.size() == 7);
    // It survives saving the project.
    const std::string proj = tempPath("sf_face_take.sfproj");
    CHECK(run(doc, "save_project", {{"path", proj}}).ok);
    Document loaded;
    CHECK(run(loaded, "open_project", {{"path", proj}}).ok);
    MorphAnimation back;
    CHECK(bakedFaceAnimation(loaded, back));
    CHECK(back.times.size() == 7 && back.channels == baked.channels);
    CHECK(loaded.mesh.layers.size() == 52);

    const std::string glb = tempPath("sf_face_take.glb");
    CHECK(run(doc, "export_glb", {{"path", glb}}).ok);
    std::ifstream gf(glb, std::ios::binary);
    std::string bytes((std::istreambuf_iterator<char>(gf)), std::istreambuf_iterator<char>());
    CHECK(bytes.find("\"animations\":[{\"name\":\"sf_face_take\"") != std::string::npos);
    CHECK(bytes.find("\"path\":\"weights\"") != std::string::npos);
    const std::string usda = tempPath("sf_face_take.usda");
    CHECK(run(doc, "export_usd", {{"path", usda}}).ok);
    std::ifstream uf(usda);
    std::stringstream us;
    us << uf.rdbuf();
    CHECK(us.str().find("blendShapeWeights.timeSamples") != std::string::npos);
    CHECK(us.str().find("timeCodesPerSecond = 30") != std::string::npos);
    // Cleared: plain exports again.
    CHECK(run(doc, "face_take_clear").ok);
    CHECK(!bakedFaceAnimation(doc, back));
}

TEST(face_export_round_trips_keep_morph_targets_and_names) {
    Document doc = makeDoc(templateHead());
    CHECK(run(doc, "face_shapes_generate").ok);
    const Mesh& m = doc.mesh;
    auto sameShapes = [&](const Mesh& got, float tol, const char* what) {
        CHECK(got.layers.size() == 52);
        bool names = got.layers.size() == 52, deltas = true;
        for (size_t k = 0; k < got.layers.size() && k < 52; ++k) {
            names &= got.layers[k].name == arkitBlendShapeNames()[k];
            deltas &= got.vertexCount() == m.vertexCount() &&
                      std::abs(maxLength(got.layers[k].delta) - maxLength(m.layers[k].delta)) <= tol;
        }
        if (!names || !deltas) std::printf("  %s: names %d, deltas %d\n", what, int(names), int(deltas));
        CHECK(names);
        CHECK(deltas);
    };
    std::string error;
    const std::string glb = tempPath("sf_face_rt.glb");
    CHECK(saveGlb(glb, m, "Head", error));
    Mesh g;
    CHECK(loadGltf(glb, g, error));
    sameShapes(g, 1e-6f, "glb");
    const std::string usda = tempPath("sf_face_rt.usda");
    CHECK(saveUsd(usda, &m, nullptr, "Head", error));
    Mesh u;
    SplatCloud sc;
    CHECK(loadUsd(usda, u, sc, error));
    sameShapes(u, 1e-5f, "usd");
    const std::string fbx = tempPath("sf_face_rt.fbx");
    FbxExportOptions fo;
    fo.scale = 100.0f; // (FBX is in centimetres; SculptForge reads it back in metres)
    CHECK(saveFbx(fbx, "Head", {&m}, fo, error));
    Mesh f;
    CHECK(loadFbx(fbx, f, error));
    sameShapes(f, 1e-5f, "fbx");

    // The Blender bridge sends the layers as shape keys beside the Bridge (base shape).
    const std::string root = tempPath("sf_face_bridge");
    std::filesystem::remove_all(root);
    std::string message;
    CHECK(blenderSend({{"Head", &m}}, root, message));
    BridgeManifest man;
    CHECK(readBridgeManifest(blenderOutbox(root) + "/manifest.json", man, error));
    CHECK(man.objects.size() == 1 && !man.objects[0].shapes.empty());
    if (!man.objects.empty() && !man.objects[0].shapes.empty()) {
        uint32_t nv = 0;
        std::vector<BridgeShape> shapes;
        CHECK(readBridgeShapes(blenderOutbox(root) + "/" + man.objects[0].shapes, nv, shapes, error));
        CHECK(nv == m.vertexCount());
        CHECK(shapes.size() == 52);
        for (size_t k = 0; k < shapes.size() && k < 52; ++k) CHECK(shapes[k].name == arkitBlendShapeNames()[k]);
        // Blender's axes: SculptForge's -Z offset (jaw back) is Blender's +Y.
        const auto& jaw = shapes[size_t(arkitBlendShapeIndex("jawOpen"))];
        const SculptLayer* l = findLayer(m, "jawOpen");
        if (!jaw.index.empty() && l) {
            const glm::vec3 sf = l->delta[jaw.index[0]];
            CHECK(std::abs(jaw.offset[0].x - sf.x) < 1e-6f && std::abs(jaw.offset[0].y + sf.z) < 1e-6f &&
                  std::abs(jaw.offset[0].z - sf.y) < 1e-6f);
        }
    }
    std::filesystem::remove_all(root);
}
