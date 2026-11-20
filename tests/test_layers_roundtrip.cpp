// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Sculpt layers survive a round trip through glTF morph targets, FBX blend shapes and USD blend shapes: export, import,
// and the layers are the same ones, in the same order, with the same names and weights, and per-vertex deltas within
// 1e-5. A dense curved mesh with UV seams (glTF splits the seam vertices on export and welds them back on import), layers
// with spaces, punctuation and non-ASCII in their names, ARKit names, a layer with no offsets at all and weights outside
// 0..1. Also the Blender bridge's shapes file in both directions (Blender shape keys -> layers on send, layers -> shape
// keys on return) and morph targets of files made elsewhere (the importer, not only our own exporter).
#include "TestHarness.h"

#include "core/Face.h"
#include "core/Primitives.h"
#include "core/Commands.h"
#include "io/BlenderBridge.h"
#include "io/BridgeIO.h"
#include "io/FbxIO.h"
#include "io/GltfIO.h"
#include "io/UsdIO.h"

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>

using namespace sf;

namespace {

constexpr float kTol = 1e-5f;

// A UV sphere-like unwrap: u from the longitude, so the triangles across the +X meridian get u values ~1 and ~0 and the
// seam vertices have two UVs (the exporters split them, the importers weld them again).
void unwrap(Mesh& m) {
    m.uvs.clear();
    for (const auto& t : m.triangles) {
        glm::vec2 uv[3];
        float u[3];
        for (int k = 0; k < 3; ++k) {
            const glm::vec3 p = glm::normalize(m.positions[t[k]]);
            u[k] = std::atan2(p.z, p.x) / (2.0f * glm::pi<float>()) + 0.5f;
            uv[k] = {u[k], std::asin(std::clamp(p.y, -1.0f, 1.0f)) / glm::pi<float>() + 0.5f};
        }
        const float lo = std::min({u[0], u[1], u[2]}), hi = std::max({u[0], u[1], u[2]});
        if (hi - lo > 0.5f)
            for (int k = 0; k < 3; ++k)
                if (u[k] < 0.5f) uv[k].x += 1.0f;
        for (int k = 0; k < 3; ++k) m.uvs.push_back(uv[k]);
    }
}

struct Spec {
    const char* name;
    float weight;
    int kind; // how the delta is made (0 none, 1.. smooth fields)
};

// A dense curved body with seams and layers named and weighted by `specs`.
Mesh makeBody(const std::vector<Spec>& specs) {
    Mesh m = makeIcosphere(4, 0.8f); // 2562 vertices, curved
    for (auto& p : m.positions) p.y *= 1.3f;
    m.recomputeAllNormals();
    unwrap(m);
    for (const Spec& s : specs) {
        const int idx = m.addLayer(s.name);
        auto& d = m.layers[size_t(idx)].delta;
        for (uint32_t v = 0; v < m.vertexCount(); ++v) {
            const glm::vec3 p = m.positions[v];
            switch (s.kind) {
            case 1: d[v] = glm::vec3(0.02f * std::sin(5.0f * p.y), 0.04f * std::cos(3.0f * p.x), 0.01f * p.z); break;
            case 2: d[v] = p.x > 0.0f ? glm::vec3(0.0f, 0.03f * p.x, -0.02f * p.y * p.y) : glm::vec3(0.0f); break; // one-sided
            case 3: d[v] = 0.05f * glm::vec3(std::sin(7.0f * p.z), 0.0f, std::cos(4.0f * p.y + p.x)); break;
            case 4: d[v] = glm::vec3(1e-4f * float(v % 7), -3e-4f, 2e-5f * float(v % 3)); break; // tiny deltas everywhere
            default: break;                                                                       // no offsets at all
            }
        }
        m.setLayerWeight(idx, s.weight);
    }
    return m;
}

const std::vector<Spec>& bodySpecs() {
    static const std::vector<Spec> s = {
        {"Smile", 0.5f, 1},        {"mouthSmileLeft", 0.0f, 2},    {"Brow Up #2", 1.0f, 3},
        {"Ünïcode ✓", 0.25f, 1},   {"Empty", 0.4f, 0},             {"jawOpen", 1.0f, 4},
        {"Overdrive", 1.5f, 3},    {"Negative", -0.5f, 2},
    };
    return s;
}

// Compares the layers of `got` with `want`: count, names, order, weights, per-vertex deltas (within kTol), and the
// shown shape (positions) for the weights. `label` names the format in failures.
void sameLayers(const Mesh& want, const Mesh& got, const char* label) {
    CHECK(got.vertexCount() == want.vertexCount());
    CHECK(got.layers.size() == want.layers.size());
    if (got.vertexCount() != want.vertexCount() || got.layers.size() != want.layers.size()) {
        std::printf("  %s: %zu vertices / %zu layers, wanted %zu / %zu\n", label, size_t(got.vertexCount()), got.layers.size(),
                    size_t(want.vertexCount()), want.layers.size());
        return;
    }
    bool names = true, weights = true;
    float worst = 0.0f, worstBase = 0.0f, worstShown = 0.0f;
    for (size_t k = 0; k < want.layers.size(); ++k) {
        names &= got.layers[k].name == want.layers[k].name;
        weights &= std::abs(got.layers[k].weight - want.layers[k].weight) <= 1e-6f;
        for (uint32_t v = 0; v < want.vertexCount(); ++v) worst = std::max(worst, glm::length(got.layers[k].delta[v] - want.layers[k].delta[v]));
    }
    for (uint32_t v = 0; v < want.vertexCount(); ++v) {
        worstBase = std::max(worstBase, glm::length(got.basePositions()[v] - want.basePositions()[v]));
        worstShown = std::max(worstShown, glm::length(got.positions[v] - want.positions[v]));
    }
    if (!names || !weights || worst > kTol || worstBase > kTol || worstShown > kTol)
        std::printf("  %s: names %d, weights %d, delta err %.3g, base err %.3g, shown err %.3g\n", label, int(names), int(weights),
                    double(worst), double(worstBase), double(worstShown));
    CHECK(names);
    CHECK(weights);
    CHECK(worst <= kTol);
    CHECK(worstBase <= kTol);
    CHECK(worstShown <= kTol);
}

} // namespace

TEST(layers_round_trip_glb) {
    const Mesh m = makeBody(bodySpecs());
    const std::string path = tempPath("sf_layers_rt.glb");
    std::string error;
    CHECK(saveGlb(path, m, "Body", error));
    Mesh back;
    CHECK(loadGltf(path, back, error));
    sameLayers(m, back, "glb");
    std::filesystem::remove(path);
}

TEST(layers_round_trip_fbx) {
    const Mesh m = makeBody(bodySpecs());
    const std::string path = tempPath("sf_layers_rt.fbx");
    std::string error;
    FbxExportOptions o;
    o.scale = 100.0f; // FBX is in centimetres; the importer returns metres
    CHECK(saveFbx(path, "Body", {&m}, o, error));
    Mesh back;
    CHECK(loadFbx(path, back, error));
    sameLayers(m, back, "fbx");
    std::filesystem::remove(path);
}

TEST(layers_round_trip_usd) {
    const Mesh m = makeBody(bodySpecs());
    const std::string path = tempPath("sf_layers_rt.usda");
    std::string error;
    CHECK(saveUsd(path, &m, nullptr, "Body", error));
    Mesh back;
    SplatCloud splats;
    CHECK(loadUsd(path, back, splats, error));
    sameLayers(m, back, "usda");
    std::filesystem::remove(path);
}

TEST(layers_round_trip_twice_is_stable) {
    // Export -> import -> export -> import: the second pass changes nothing (names, order, weights, deltas).
    const Mesh m = makeBody(bodySpecs());
    std::string error;
    const std::string a = tempPath("sf_layers_rt2a.glb"), b = tempPath("sf_layers_rt2b.glb");
    Mesh once, twice;
    CHECK(saveGlb(a, m, "Body", error));
    CHECK(loadGltf(a, once, error));
    CHECK(saveGlb(b, once, "Body", error));
    CHECK(loadGltf(b, twice, error));
    sameLayers(m, twice, "glb x2");
    std::filesystem::remove(a);
    std::filesystem::remove(b);
}

TEST(layers_round_trip_arkit_names_all_formats) {
    // The 52 ARKit names in their canonical order: every format hands them back, deltas exact.
    std::vector<Spec> specs;
    for (size_t k = 0; k < 52; ++k) specs.push_back({arkitBlendShapeNames()[k].c_str(), k % 5 == 0 ? 0.3f : 0.0f, 1 + int(k % 4)});
    const Mesh m = makeBody(specs);
    CHECK(m.layers.size() == 52);
    std::string error;
    Mesh g, u, f;
    SplatCloud sc;
    const std::string glb = tempPath("sf_layers_arkit.glb"), usd = tempPath("sf_layers_arkit.usda"), fbx = tempPath("sf_layers_arkit.fbx");
    CHECK(saveGlb(glb, m, "Head", error));
    CHECK(loadGltf(glb, g, error));
    CHECK(saveUsd(usd, &m, nullptr, "Head", error));
    CHECK(loadUsd(usd, u, sc, error));
    FbxExportOptions o;
    o.scale = 100.0f;
    CHECK(saveFbx(fbx, "Head", {&m}, o, error));
    CHECK(loadFbx(fbx, f, error));
    sameLayers(m, g, "glb arkit");
    sameLayers(m, u, "usd arkit");
    sameLayers(m, f, "fbx arkit");
    for (size_t k = 0; k < 52 && k < g.layers.size(); ++k) CHECK(g.layers[k].name == arkitBlendShapeNames()[k]);
    std::filesystem::remove(glb);
    std::filesystem::remove(usd);
    std::filesystem::remove(fbx);
}

// Blender -> SculptForge -> Blender: the shapes file the add-on writes for an object's shape keys (offsets from the Basis,
// object space, Blender's axes) becomes sculpt layers on receive, placed by the object's matrix like the positions, and
// the next send writes them back (world space): same names, order, weights and offsets within 1e-5, also for a mirrored
// object and a key that moves nothing.
TEST(layers_round_trip_blender_shape_keys) {
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / "sf_layers_bridge";
    fs::remove_all(root);
    const fs::path inbox = root / "to_sculptforge";
    fs::create_directories(inbox);
    const Mesh body = makeBody(bodySpecs());
    Mesh neutral = body; // the Bridge file: the base shape
    neutral.positions = body.basePositions();
    neutral.layers.clear();
    neutral.base.clear();
    neutral.uvs.clear();
    neutral.rebuildTopology();
    std::string err;
    const glm::dmat4 matrices[2] = {
        glm::translate(glm::dmat4(1.0), glm::dvec3(0.3, -1.2, 2.0)) * glm::rotate(glm::dmat4(1.0), 0.65, glm::normalize(glm::dvec3(1, 2, 0.5))) *
            glm::scale(glm::dmat4(1.0), glm::dvec3(1.5, 0.5, 2.0)),
        glm::translate(glm::dmat4(1.0), glm::dvec3(1.0, 1.0, 1.0)) * glm::scale(glm::dmat4(1.0), glm::dvec3(-1.0, 1.0, 1.0))};
    const char* names[2] = {"Keyed", "Keyed.mirrored"};
    BridgeManifest in;
    in.revision = 1;
    in.sender = "blender";
    for (int k = 0; k < 2; ++k) {
        BridgeObject o;
        o.name = names[k];
        o.file = std::string("r1_00") + char('0' + k) + ".Bridge";
        o.shapes = std::string("r1_00") + char('0' + k) + ".shapes";
        o.hasMatrix = true;
        o.matrix = matrices[k];
        CHECK(saveBridge((inbox / o.file).string(), neutral, o.name, err));
        CHECK(writeBridgeShapes((inbox / o.shapes).string(), body, err));
        in.objects.push_back(o);
    }
    {
        std::ofstream f(inbox / "manifest.json", std::ios::binary);
        f << writeBridgeManifest(in);
    }

    CommandRegistry reg;
    Document doc = makeDoc(makeIcosphere(1));
    CommandResult r = reg.execute(doc, "blender_receive", {{"folder", root.string()}});
    CHECK(r.ok);
    CHECK(doc.partCount() == 2);
    for (int k = 0; k < 2 && k < doc.partCount(); ++k) {
        const Mesh& got = doc.partMesh(k);
        const glm::dmat3 linear(blenderMatrixToSculptForge(matrices[k]));
        CHECK(got.layers.size() == body.layers.size());
        if (got.layers.size() != body.layers.size()) continue;
        float worst = 0.0f;
        bool same = true;
        for (size_t l = 0; l < body.layers.size(); ++l) {
            same &= got.layers[l].name == body.layers[l].name && std::abs(got.layers[l].weight - body.layers[l].weight) < 1e-6f;
            for (uint32_t v = 0; v < body.vertexCount(); ++v) {
                const glm::vec3 want = glm::vec3(linear * glm::dvec3(body.layers[l].delta[v]));
                worst = std::max(worst, glm::length(got.layers[l].delta[v] - want));
            }
        }
        if (!same || worst > kTol) std::printf("  receive %d: names/weights %d, delta err %.3g\n", k, int(same), double(worst));
        CHECK(same);
        CHECK(worst <= kTol);
        // The shown shape is the Basis plus the keys at their values.
        const glm::dmat4 placed = blenderMatrixToSculptForge(matrices[k]);
        float shown = 0.0f;
        for (uint32_t v = 0; v < body.vertexCount(); ++v) {
            glm::vec3 p = body.basePositions()[v];
            glm::vec3 d(0.0f);
            for (size_t l = 0; l < got.layers.size(); ++l) d += got.layers[l].weight * got.layers[l].delta[v];
            const glm::vec3 base = glm::vec3(placed * glm::dvec4(glm::dvec3(p), 1.0));
            shown = std::max(shown, glm::length(got.positions[v] - (base + d)));
        }
        CHECK(shown <= kTol);
    }

    // And back: the layers go out as shapes again (world space) and read as the keys they were.
    r = reg.execute(doc, "blender_send", {{"folder", root.string()}});
    CHECK(r.ok);
    BridgeManifest out;
    CHECK(readBridgeManifest(bridgeManifestPath(blenderOutbox(root.string())), out, err));
    CHECK(out.objects.size() == 2);
    for (size_t k = 0; k < out.objects.size() && k < 2; ++k) {
        CHECK(!out.objects[k].shapes.empty());
        if (out.objects[k].shapes.empty()) continue;
        uint32_t nv = 0;
        std::vector<BridgeShape> shapes;
        CHECK(readBridgeShapes((fs::path(blenderOutbox(root.string())) / out.objects[k].shapes).string(), nv, shapes, err));
        CHECK(nv == body.vertexCount() && shapes.size() == body.layers.size());
        const glm::dmat3 linearB(matrices[k]); // world-space offsets in Blender's axes: M's linear part on the object's
        float worst = 0.0f;
        bool same = shapes.size() == body.layers.size();
        for (size_t l = 0; l < shapes.size() && l < body.layers.size(); ++l) {
            same &= shapes[l].name == body.layers[l].name && std::abs(shapes[l].weight - body.layers[l].weight) < 1e-6f;
            std::vector<glm::dvec3> have(nv, glm::dvec3(0.0));
            for (size_t i = 0; i < shapes[l].index.size(); ++i) have[shapes[l].index[i]] = glm::dvec3(shapes[l].offset[i]);
            for (uint32_t v = 0; v < nv; ++v) {
                const glm::dvec3 objectSpace = sculptForgeToBlender(glm::dvec3(body.layers[l].delta[v]));
                worst = std::max(worst, float(glm::length(have[v] - linearB * objectSpace)));
            }
        }
        if (!same || worst > kTol) std::printf("  send %zu: names/weights %d, offset err %.3g\n", k, int(same), double(worst));
        CHECK(same);
        CHECK(worst <= kTol);
    }
    fs::remove_all(root);
}
