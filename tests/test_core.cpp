// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Unit tests for the core library, IO, AI request plumbing and links.
#include "TestHarness.h"

#include "ai/BrushAssistant.h"
#include "ai/GradioClient.h"
#include "ai/ImageTo3DService.h"
#include "cli/McpServer.h"
#include "core/BrushRecipe.h"
#include "core/Commands.h"
#include "core/Json.h"
#include "core/Decimate.h"
#include "core/ImageTo3D.h"
#include "core/Script.h"
#include "io/GltfIO.h"
#include "core/Primitives.h"
#include "core/Sculpt.h"
#include "io/ObjIO.h"
#include "io/FbxIO.h"
#include "io/PlyIO.h"
#include "core/EnginePresets.h"
#include "link/UnrealLink.h"
#include "io/UsdIO.h"
#include "io/Zip.h"
#include "core/Splats.h"
#include "core/Voxel.h"

#include <cmath>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <glm/gtc/matrix_transform.hpp>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <string>
#include <vector>

using namespace sf;

TEST(icosphere_counts) {
    Mesh m = makeIcosphere(3);
    CHECK(m.triangleCount() == 20u * 64u);
    // Closed genus-0 surface: V - E + F = 2 with E = 3F/2.
    CHECK(long(m.vertexCount()) - long(m.triangleCount() * 3 / 2) + long(m.triangleCount()) == 2);
    for (const auto& p : m.positions) CHECK(std::abs(glm::length(p) - 1.0f) < 1e-5f);
    for (uint32_t v = 0; v < m.vertexCount(); ++v) CHECK(glm::dot(m.normals[v], m.positions[v]) > 0.99f);
}

TEST(bvh_raycast_and_query) {
    Mesh m = makeIcosphere(4);
    BVH bvh;
    bvh.build(m);
    RayHit hit;
    CHECK(bvh.raycast(m, glm::vec3(0, 0, 5), glm::vec3(0, 0, -1), hit));
    CHECK(std::abs(hit.position.z - 1.0f) < 0.01f);
    CHECK(!bvh.raycast(m, glm::vec3(0, 3, 5), glm::vec3(0, 0, -1), hit));

    std::vector<uint32_t> found;
    glm::vec3 c(0, 0, 1);
    bvh.queryVertices(m, c, 0.3f, found);
    size_t brute = 0;
    for (const auto& p : m.positions) brute += glm::distance(p, c) <= 0.3f;
    CHECK(found.size() == brute);
    CHECK(brute > 0);
}

TEST(standard_brush_raises_surface_with_symmetry) {
    Document d = makeDoc(makeIcosphere(4));
    DabParams p;
    p.center = glm::normalize(glm::vec3(0.5f, 0.2f, 0.8f));
    p.viewDir = -p.center;
    p.radius = 0.3f;
    p.strength = 1.0f;
    d.sculptor.beginStroke(d.mesh, "Standard");
    d.sculptor.dab(d.mesh, d.bvh, BrushType::Standard, p, Symmetry{true, false, false});
    UndoRecord r = d.sculptor.endStroke();
    CHECK(!r.empty());

    float maxR = 0, maxMirroredR = 0;
    for (const auto& v : d.mesh.positions) {
        if (v.x > 0.2f) maxR = std::max(maxR, glm::length(v));
        if (v.x < -0.2f) maxMirroredR = std::max(maxMirroredR, glm::length(v));
    }
    CHECK(maxR > 1.01f);
    CHECK(std::abs(maxR - maxMirroredR) < 1e-4f);

    // BVH must still find the raised surface.
    RayHit hit;
    CHECK(d.bvh.raycast(d.mesh, p.center * 5.0f, -p.center, hit));
    CHECK(glm::length(hit.position) > 1.005f);
}

TEST(undo_redo_restores_positions) {
    Document d = makeDoc(makeIcosphere(3));
    const std::vector<glm::vec3> original = d.mesh.positions;
    DabParams p;
    p.center = glm::vec3(0, 0, 1);
    p.viewDir = glm::vec3(0, 0, -1);
    p.radius = 0.5f;
    for (BrushType t : {BrushType::Standard, BrushType::Clay, BrushType::Inflate, BrushType::Crease}) {
        d.sculptor.beginStroke(d.mesh, brushName(t));
        d.sculptor.dab(d.mesh, d.bvh, t, p, Symmetry{});
        d.undo.push(d.sculptor.endStroke());
    }
    const std::vector<glm::vec3> sculpted = d.mesh.positions;
    CHECK(sculpted != original);
    while (d.undo.canUndo()) d.undo.undo(d.mesh, d.bvh);
    CHECK(d.mesh.positions == original);
    while (d.undo.canRedo()) d.undo.redo(d.mesh, d.bvh);
    CHECK(d.mesh.positions == sculpted);
}

TEST(smooth_reduces_noise) {
    Document d = makeDoc(makeIcosphere(4));
    for (size_t i = 0; i < d.mesh.vertexCount(); ++i)
        d.mesh.positions[i] *= 1.0f + 0.05f * float((i * 2654435761u) % 7) / 7.0f;
    d.mesh.recomputeAllNormals();
    d.bvh.build(d.mesh);
    // Roughness: mean distance of each vertex from its neighbours' average.
    auto spread = [&] {
        float sum = 0;
        int n = 0;
        for (uint32_t v = 0; v < d.mesh.vertexCount(); ++v) {
            if (d.mesh.positions[v].z < 0.95f) continue;
            glm::vec3 avg(0.0f);
            for (uint32_t u : d.mesh.neighbors(v)) avg += d.mesh.positions[u];
            avg /= float(d.mesh.neighbors(v).size());
            sum += glm::distance(avg, d.mesh.positions[v]);
            ++n;
        }
        return sum / float(n);
    };
    float before = spread();
    DabParams p;
    p.center = glm::vec3(0, 0, 1);
    p.radius = 0.6f;
    p.strength = 1.0f;
    d.sculptor.beginStroke(d.mesh, "Smooth");
    for (int i = 0; i < 5; ++i) d.sculptor.dab(d.mesh, d.bvh, BrushType::Smooth, p, Symmetry{false, false, false});
    d.sculptor.endStroke();
    CHECK(spread() < before * 0.5f);
}

TEST(smooth_keep_volume_keeps_volume) {
    // #40: the plain (Laplacian) Smooth shrinks a sphere; with keepVolume (a Taubin step) it barely changes the
    // volume, yet still removes bumps.
    auto volume = [](const Mesh& m) {
        double v = 0.0;
        for (const glm::uvec3& t : m.triangles)
            v += double(glm::dot(m.positions[t.x], glm::cross(m.positions[t.y], m.positions[t.z]))) / 6.0;
        return v;
    };
    auto spread = [](const Mesh& m) { // roughness: mean distance of each vertex from its neighbours' average
        double sum = 0.0;
        for (uint32_t v = 0; v < m.vertexCount(); ++v) {
            glm::vec3 avg(0.0f);
            for (uint32_t u : m.neighbors(v)) avg += m.positions[u];
            avg /= float(m.neighbors(v).size());
            sum += glm::distance(avg, m.positions[v]);
        }
        return sum / double(m.vertexCount());
    };
    auto smoothed = [&](bool keepVolume, bool noisy, double& volumeBefore, double& spreadBefore) {
        Document d = makeDoc(makeIcosphere(4));
        if (noisy)
            for (size_t i = 0; i < d.mesh.vertexCount(); ++i)
                d.mesh.positions[i] *= 1.0f + 0.04f * (float((i * 2654435761u) % 7) / 7.0f - 0.5f);
        d.mesh.recomputeAllNormals();
        d.bvh.build(d.mesh);
        volumeBefore = volume(d.mesh);
        spreadBefore = spread(d.mesh);
        DabParams p;
        p.radius = 3.0f; // the whole sphere, at full strength in the middle
        p.strength = 1.0f;
        p.keepVolume = keepVolume;
        d.sculptor.beginStroke(d.mesh, "Smooth");
        for (int i = 0; i < 20; ++i) {
            p.center = glm::vec3(0, 0, i % 2 ? -1.0f : 1.0f);
            p.viewDir = -p.center;
            d.sculptor.dab(d.mesh, d.bvh, BrushType::Smooth, p, Symmetry{false, false, false});
        }
        d.sculptor.endStroke();
        return d.mesh;
    };
    double v0 = 0.0, s0 = 0.0;
    const Mesh plain = smoothed(false, false, v0, s0);
    const double plainLoss = (v0 - volume(plain)) / v0;
    const Mesh kept = smoothed(true, false, v0, s0);
    const double keptChange = std::abs(volume(kept) - v0) / v0;
    std::printf("  sphere volume change: plain %.2f%%, keep volume %.2f%%\n", 100.0 * plainLoss, 100.0 * keptChange);
    CHECK(plainLoss > 0.02);               // the Laplacian shrinks the sphere noticeably
    CHECK(keptChange < plainLoss * 0.25);  // the Taubin step keeps (nearly) all of it
    // It still smooths: the bumps of a noisy sphere mostly go.
    const Mesh noisy = smoothed(true, true, v0, s0);
    CHECK(spread(noisy) < s0 * 0.7);
    CHECK(std::abs(volume(noisy) - v0) / v0 < 0.02);
}

TEST(move_brush_drags_region) {
    Document d = makeDoc(makeIcosphere(4));
    DabParams p;
    p.center = glm::vec3(0, 0, 1);
    p.radius = 0.4f;
    p.strength = 1.0f;
    d.sculptor.beginStroke(d.mesh, "Move");
    d.sculptor.beginGrab(d.mesh, d.bvh, p, Symmetry{false, false, false});
    d.sculptor.grab(d.mesh, d.bvh, glm::vec3(0, 0, 0.5f));
    d.sculptor.endStroke();
    float maxZ = 0;
    for (const auto& v : d.mesh.positions) maxZ = std::max(maxZ, v.z);
    CHECK(std::abs(maxZ - 1.5f) < 0.01f);
}

TEST(loop_subdivision) {
    Mesh m = makeIcosphere(2);
    Mesh s = loopSubdivide(m);
    CHECK(s.triangleCount() == m.triangleCount() * 4);
    CHECK(s.vertexCount() == m.vertexCount() + m.triangleCount() * 3 / 2);
    for (const auto& p : s.positions) CHECK(glm::length(p) > 0.9f && glm::length(p) < 1.01f);

    Mesh plane = makePlane(4);
    Mesh sp = loopSubdivide(plane);
    CHECK(sp.triangleCount() == plane.triangleCount() * 4);
    for (const auto& p : sp.positions) CHECK(std::abs(p.y) < 1e-6f); // stays flat
    // Corners are kept in place.
    AABB b = sp.bounds();
    CHECK(std::abs(b.min.x + 1.0f) < 1e-5f && std::abs(b.max.z - 1.0f) < 1e-5f);
}

TEST(obj_roundtrip) {
    const std::string in = tempPath("sculptforge_test_in.obj");
    {
        std::ofstream f(in);
        f << "# quad + negative indices\nv 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\nvt 0 0\n"
             "f 1/1 2/1 3/1 4/1\nv 2 0 0\nf -4 -1 -3\n";
    }
    Mesh m;
    std::string err;
    CHECK(loadObj(in, m, err));
    CHECK(m.vertexCount() == 5);
    CHECK(m.triangleCount() == 3);

    const std::string out = tempPath("sculptforge_test_out.obj");
    CHECK(saveObj(out, m, err));
    Mesh back;
    CHECK(loadObj(out, back, err));
    CHECK(back.positions == m.positions);
    CHECK(back.triangles == m.triangles);

    CHECK(!loadObj(tempPath("does_not_exist_sculptforge.obj"), back, err));
    {
        std::ofstream f(in);
        f << "v 0 0 0\nf 1 2 3\n";
    }
    CHECK(!loadObj(in, back, err));
    std::filesystem::remove(in);
    std::filesystem::remove(out);
}

TEST(commands_and_tool_schema) {
    CommandRegistry reg;
    Document d;
    CHECK(reg.execute(d, "new_sphere", {{"subdivisions", 2.0}}).ok);
    CHECK(d.mesh.triangleCount() == 320);
    CHECK(reg.execute(d, "subdivide").ok);
    CHECK(d.mesh.triangleCount() == 1280);
    CHECK(reg.execute(d, "undo").ok);
    CHECK(d.mesh.triangleCount() == 320);
    CHECK(reg.execute(d, "redo").ok);
    CHECK(d.mesh.triangleCount() == 1280);
    CHECK(!reg.execute(d, "open_obj").ok); // missing required path
    CHECK(!reg.execute(d, "no_such_command").ok);

    std::string schema = reg.toolSchemaJson();
    CHECK(schema.front() == '[' && schema.back() == ']');
    CHECK(schema.find("\"name\":\"subdivide\"") != std::string::npos);
    CHECK(schema.find("\"required\":[\"path\"]") != std::string::npos);
}

TEST(sculpt_layers_blend_and_undo) {
    Document d = makeDoc(makeIcosphere(3));
    const std::vector<glm::vec3> original = d.mesh.positions;
    int smile = d.mesh.addLayer("Smile");
    DabParams p;
    p.center = glm::vec3(0, 0, 1);
    p.viewDir = glm::vec3(0, 0, -1);
    p.radius = 0.5f;
    p.strength = 1.0f;
    CHECK(d.sculptor.beginStroke(d.mesh, "Standard", smile));
    d.sculptor.dab(d.mesh, d.bvh, BrushType::Standard, p, Symmetry{false, false, false});
    d.undo.push(d.sculptor.endStroke());
    const std::vector<glm::vec3> sculpted = d.mesh.positions;
    CHECK(sculpted != original);
    CHECK(d.mesh.base == original); // stroke went into the layer, not the base

    d.mesh.setLayerWeight(smile, 0.0f);
    for (size_t i = 0; i < original.size(); ++i) CHECK(glm::distance(d.mesh.positions[i], original[i]) < 1e-6f);
    d.mesh.setLayerWeight(smile, 0.5f);
    for (size_t i = 0; i < original.size(); ++i)
        CHECK(glm::distance(d.mesh.positions[i], (original[i] + sculpted[i]) * 0.5f) < 1e-5f);

    // Undo after changing the weight removes exactly the stroke's offsets.
    d.undo.undo(d.mesh, d.bvh);
    for (size_t i = 0; i < original.size(); ++i) CHECK(glm::distance(d.mesh.positions[i], original[i]) < 1e-6f);

    // A layer at weight 0 refuses strokes.
    d.mesh.setLayerWeight(smile, 0.0f);
    CHECK(!d.sculptor.beginStroke(d.mesh, "Standard", smile));
}

TEST(layers_survive_subdivision) {
    Mesh m = makeIcosphere(2);
    int l = m.addLayer("Bulge");
    for (size_t v = 0; v < m.vertexCount(); ++v) m.layers[l].delta[v] = m.positions[v] * 0.1f;
    m.setLayerWeight(l, 0.7f);
    Mesh s = loopSubdivide(m);
    CHECK(s.layers.size() == 1 && s.layers[0].weight == 0.7f);
    for (size_t v = 0; v < s.vertexCount(); ++v)
        CHECK(glm::distance(s.positions[v], s.base[v] + 0.7f * s.layers[0].delta[v]) < 1e-5f);
}

TEST(decimation_and_lods) {
    Mesh m = makeIcosphere(5); // 20480 tris
    Mesh d = decimate(m, 2000);
    CHECK(d.triangleCount() <= 2000 && d.triangleCount() > 1800);
    // Still closed and manifold: V - E + F = 2.
    CHECK(long(d.vertexCount()) - long(d.triangleCount() * 3 / 2) + long(d.triangleCount()) == 2);
    for (const auto& p : d.positions) CHECK(std::abs(glm::length(p) - 1.0f) < 1e-4f); // subset of sphere verts
    float minR = 1e9f;
    for (const auto& t : d.triangles)
        minR = std::min(minR, glm::length((d.positions[t.x] + d.positions[t.y] + d.positions[t.z]) / 3.0f));
    CHECK(minR > 0.95f); // shape preserved

    m.addLayer("Blink");
    auto lods = generateLods(m, 4, 0.5f);
    CHECK(lods.size() == 4);
    CHECK(lods[3].triangleCount() <= lods[0].triangleCount() / 8 + 8);
    for (const auto& lod : lods) CHECK(lod.layers.size() == 1 && lod.layers[0].delta.size() == lod.vertexCount());

    Mesh plane = makePlane(32);
    Mesh dp = decimate(plane, 200);
    AABB b = dp.bounds();
    CHECK(std::abs(b.min.x + 1.0f) < 1e-5f && std::abs(b.max.x - 1.0f) < 1e-5f); // boundary kept
}

TEST(glb_export) {
    Mesh m = makeIcosphere(1);
    int l = m.addLayer("Smile");
    m.layers[l].delta[0] = glm::vec3(0, 0.1f, 0);
    const std::string path = tempPath("sculptforge_test.glb");
    std::string err;
    CHECK(saveGlb(path, m, "Head", err));
    std::ifstream f(path, std::ios::binary);
    std::string data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    CHECK(data.size() > 20 && data.substr(0, 4) == "glTF");
    uint32_t total = 0, jsonLen = 0;
    std::memcpy(&total, data.data() + 8, 4);
    std::memcpy(&jsonLen, data.data() + 12, 4);
    CHECK(total == data.size());
    CHECK(jsonLen % 4 == 0);
    std::string json = data.substr(20, jsonLen);
    CHECK(json.find("\"targets\"") != std::string::npos);
    CHECK(json.find("\"targetNames\":[\"Smile\"]") != std::string::npos);
    f.close();
    std::filesystem::remove(path);
}

TEST(script_windows_paths) {
    std::vector<ScriptStatement> st;
    std::string err;
    const std::string windowsPath = R"(open path="C:\Users\me\sculpts\head.obj")";
    CHECK(parseScript(windowsPath, st, err));
    CHECK(st.size() == 1 && std::get<std::string>(st[0].args["path"]) == "C:\\Users\\me\\sculpts\\head.obj");
    const std::string quoted = R"(brush_ai prompt="say \"hi\"")";
    CHECK(parseScript(quoted, st, err) && std::get<std::string>(st[0].args["prompt"]) == "say \"hi\"");
}

TEST(script_format_round_trip) {
    // formatStatement is parseScript's inverse: the console echoes every command this way.
    CHECK(formatStatement("subdivide", {}) == "subdivide");
    CHECK(formatStatement("remesh", {{"resolution", 200.0}}) == "remesh resolution=200");
    CHECK(formatStatement("layer_add", {{"name", std::string("Big Smile")}}) == "layer_add name=\"Big Smile\"");
    const Args args = {{"name", std::string("Big \"Smile\" C:\\x")}, {"ratio", 0.1}, {"third", 1.0 / 3.0},
                       {"flag", true}, {"off", false}, {"count", -42.0}, {"word", std::string("plain")},
                       {"numeric_text", std::string("12")}, {"bool_text", std::string("true")},
                       {"empty", std::string()}, {"semi", std::string("a;b#c")}, {"big", 1.5e20}};
    std::vector<ScriptStatement> st;
    std::string err;
    const std::string text = formatStatement("brush_create", args) + "; " + formatStatement("subdivide", {{"levels", 2.0}});
    CHECK(parseScript(text, st, err));
    CHECK(st.size() == 2 && st[0].command == "brush_create" && st[0].args == args);
    CHECK(st.size() == 2 && st[1].command == "subdivide" && std::get<double>(st[1].args["levels"]) == 2.0);
}

TEST(script_parser_and_pipeline) {
    std::vector<ScriptStatement> st;
    std::string err;
    CHECK(parseScript("new_sphere subdivisions=2; layer_add name=\"Big Smile\"\n# comment\ndecimate ratio=0.5", st, err));
    CHECK(st.size() == 3);
    CHECK(st[0].command == "new_sphere" && std::get<double>(st[0].args["subdivisions"]) == 2.0);
    CHECK(std::get<std::string>(st[1].args["name"]) == "Big Smile");
    CHECK(!parseScript("subdivide oops", st, err));

    CommandRegistry reg;
    Document d;
    CHECK(parseScript("new_sphere subdivisions=4; layer_add name=Smile; decimate ratio=0.25", st, err));
    for (const auto& s : st) CHECK(reg.execute(d, s.command, s.args).ok);
    CHECK(d.mesh.layers.size() == 1);
    CHECK(d.mesh.triangleCount() <= 1280);
    const std::string base = tempPath("sculptforge_lod.glb");
    CHECK(reg.execute(d, "export_lods", {{"path", base}, {"levels", 3.0}}).ok);
    for (int i = 0; i < 3; ++i) {
        auto p = std::filesystem::temp_directory_path() / ("sculptforge_lod_LOD" + std::to_string(i) + ".glb");
        CHECK(std::filesystem::exists(p));
        std::filesystem::remove(p);
    }
}

TEST(json_roundtrip) {
    Json j;
    std::string err;
    const std::string text = "{\"a\":[1,2.5,-3e2],\"b\":{\"c\":\"x\\\"y\xc3\xa9\\n\"},\"d\":true,\"e\":null}";
    CHECK(Json::parse(text, j, err));
    CHECK(j["a"].size() == 3 && j["a"][1].asNumber() == 2.5 && j["a"][2].asNumber() == -300.0);
    CHECK(j["b"]["c"].asString() == "x\"y\xc3\xa9\n");
    CHECK(j["d"].asBool() && j["e"].isNull() && j["missing"].isNull());
    Json back;
    CHECK(Json::parse(j.dump(), back, err));
    CHECK(back.dump() == j.dump());
    CHECK(!Json::parse("{\"a\":}", back, err));
    CHECK(!Json::parse("[1,2", back, err));
    CHECK(!Json::parse("{} trailing", back, err));
}

TEST(brush_recipe_roundtrip_and_clamp) {
    auto presets = builtinBrushRecipes();
    CHECK(presets.size() >= 6);
    for (const auto& r : presets) {
        BrushRecipe back = BrushRecipe::fromJson(r.toJson());
        CHECK(back.toJson().dump() == r.toJson().dump());
    }
    Json wild;
    std::string err;
    CHECK(Json::parse(R"({"name":"Wild","depth":7,"pattern":"scales","plane":"bogus","spacing":-1})", wild, err));
    BrushRecipe r = BrushRecipe::fromJson(wild);
    CHECK(r.depth == 1.0f && r.spacing == 0.02f);
    CHECK(r.pattern == BrushRecipe::Pattern::Scales && r.plane == BrushRecipe::Plane::None);

    const std::string path = tempPath("sculptforge_test.sfbrush");
    CHECK(presets[2].save(path, err));
    BrushRecipe loaded;
    CHECK(BrushRecipe::load(path, loaded, err));
    CHECK(loaded.name == presets[2].name && loaded.pattern == presets[2].pattern);
    std::filesystem::remove(path);

    // Schema lists every parameter as required (structured-output compatible).
    Json schema = recipeJsonSchema();
    CHECK(schema["required"].size() == recipeParams().size() + 2);
    CHECK(schema["additionalProperties"].isBool() && !schema["additionalProperties"].asBool());
}

TEST(recipe_brushes_sculpt) {
    for (const auto& recipe : builtinBrushRecipes()) {
        Document d = makeDoc(makeIcosphere(5));
        const auto before = d.mesh.positions;
        DabParams p;
        p.center = glm::vec3(0, 0, 1);
        p.viewDir = glm::vec3(0, 0, -1);
        p.radius = 0.4f;
        p.strength = 1.0f;
        p.strokeDelta = glm::vec3(0.05f, 0, 0);
        CHECK(d.sculptor.beginStroke(d.mesh, recipe.name));
        for (int i = 0; i < 3; ++i) d.sculptor.dabRecipe(d.mesh, d.bvh, recipe, p, Symmetry{false, false, false});
        UndoRecord rec = d.sculptor.endStroke();
        CHECK(!rec.empty());
        bool finite = true;
        float moved = 0;
        for (size_t i = 0; i < before.size(); ++i) {
            finite = finite && std::isfinite(d.mesh.positions[i].x) && std::isfinite(d.mesh.positions[i].y);
            moved = std::max(moved, glm::distance(before[i], d.mesh.positions[i]));
        }
        CHECK(finite);
        if (moved <= 1e-5f) std::printf("  recipe '%s' did not move the surface\n", recipe.name.c_str());
        CHECK(moved > 1e-5f);
    }
    // Carving vs building: depth sign controls direction.
    BrushRecipe carve;
    carve.depth = -1.0f;
    Document d = makeDoc(makeIcosphere(4));
    DabParams p;
    p.center = glm::vec3(0, 0, 1);
    p.viewDir = glm::vec3(0, 0, -1);
    p.radius = 0.4f;
    p.strength = 1.0f;
    d.sculptor.beginStroke(d.mesh, "Carve");
    d.sculptor.dabRecipe(d.mesh, d.bvh, carve, p, Symmetry{false, false, false});
    d.sculptor.endStroke();
    float minR = 2.0f;
    for (const auto& v : d.mesh.positions) minR = std::min(minR, glm::length(v));
    CHECK(minR < 0.99f);
}

TEST(brush_commands) {
    CommandRegistry reg;
    Document d;
    CHECK(reg.execute(d, "brush_create", {{"name", std::string("Lizard Skin")}, {"pattern", std::string("scales")},
                                          {"patternAmount", 0.8}, {"depth", 0.3}}).ok);
    CHECK(d.activeBrush >= 0 && d.brushes[d.activeBrush].name == "Lizard Skin");
    CHECK(d.brushes[d.activeBrush].pattern == BrushRecipe::Pattern::Scales);
    const std::string path = tempPath("sculptforge_lizard.sfbrush");
    CHECK(reg.execute(d, "brush_save", {{"path", path}}).ok);
    Document d2;
    CHECK(reg.execute(d2, "brush_load", {{"path", path}}).ok);
    CHECK(d2.brushes[d2.activeBrush].patternAmount == 0.8f);
    std::filesystem::remove(path);
    CHECK(reg.toolSchemaJson().find("\"tipSharpness\"") != std::string::npos);
}

TEST(ai_request_and_response_parsing) {
    using namespace sf::ai;
    AiSettings anth = AiSettings::defaults(AiSettings::Provider::Anthropic);
    anth.apiKey = "test-key";
    BrushRecipe current = builtinBrushRecipes()[0];
    std::vector<ChatMessage> history = {composeUserMessage("make it sharper", &current)};
    std::string body = buildRequestBody(anth, history);
    Json req;
    std::string err;
    CHECK(Json::parse(body, req, err));
    CHECK(req["model"].asString() == "claude-opus-5-5");
    CHECK(req["output_config"]["format"]["type"].asString() == "json_schema");
    CHECK(req["output_config"]["effort"].asString() == "low");
    CHECK(req["fallbacks"].asString() == "default");
    CHECK(req["messages"][0]["content"].asString().find("Wrinkle") != std::string::npos);
    auto headers = requestHeaders(anth);
    CHECK(std::find(headers.begin(), headers.end(), "x-api-key: test-key") != headers.end());
    CHECK(std::find(headers.begin(), headers.end(), "anthropic-version: 2023-06-01") != headers.end());

    // Anthropic response with an (empty) thinking block followed by the JSON text.
    Json resp = Json::object();
    resp.set("stop_reason", "end_turn");
    Json content = Json::array();
    Json thinking = Json::object();
    thinking.set("type", "thinking");
    thinking.set("thinking", "");
    content.push(thinking);
    Json text = Json::object();
    text.set("type", "text");
    text.set("text", R"({"reply":"Sharper now.","brush":{"name":"Wrinkle","depth":-0.7,"tipSharpness":0.9}})");
    content.push(text);
    resp.set("content", content);
    std::string raw;
    BrushSuggestion s = parseResponse(anth, 200, resp.dump(), &raw);
    CHECK(s.ok && s.reply == "Sharper now." && s.recipe.tipSharpness == 0.9f && s.recipe.depth == -0.7f);
    CHECK(raw.find("Sharper") != std::string::npos);

    resp.set("stop_reason", "refusal");
    CHECK(!parseResponse(anth, 200, resp.dump()).ok);
    CHECK(parseResponse(anth, 401, R"({"type":"error","error":{"message":"invalid x-api-key"}})").error.find("rejected") !=
          std::string::npos);

    // OpenAI-compatible (e.g. Ollama) reply wrapped in a code fence.
    AiSettings local = AiSettings::defaults(AiSettings::Provider::OpenAICompatible);
    Json req2;
    CHECK(Json::parse(buildRequestBody(local, history), req2, err));
    CHECK(req2["messages"][0]["role"].asString() == "system");
    std::string fenced = "Here you go:\n```json\n{\"reply\":\"Gritty!\",\"brush\":{\"name\":\"Grit\",\"pattern\":\"noise\","
                         "\"patternAmount\":0.9}}\n```";
    Json oresp = Json::object();
    Json choices = Json::array();
    Json choice = Json::object();
    Json msg = Json::object();
    msg.set("content", fenced);
    choice.set("message", msg);
    choices.push(choice);
    oresp.set("choices", choices);
    BrushSuggestion s2 = parseResponse(local, 200, oresp.dump());
    CHECK(s2.ok && s2.recipe.name == "Grit" && s2.recipe.pattern == BrushRecipe::Pattern::Noise);
}

TEST(ai_free_provider_is_the_default) {
    using namespace sf::ai;
    AiSettings s;
    CHECK(s.provider == AiSettings::Provider::Free);
    AiSettings free = AiSettings::defaults(AiSettings::Provider::Free);
    free.apiKey = "ignored";
    CHECK(free.endpoint.find("pollinations") != std::string::npos);
    auto headers = requestHeaders(free);
    CHECK(headers.size() == 1); // no key is ever sent
    Json req;
    std::string err;
    CHECK(Json::parse(buildRequestBody(free, {composeUserMessage("dry cracked mud", nullptr)}), req, err));
    CHECK(req["response_format"]["type"].asString() == "json_object");
    CHECK(req["messages"][0]["role"].asString() == "system");

    // Saved settings round-trip; key-less Anthropic settings from older versions become Free.
    CHECK(AiSettings::fromJson(free.toJson()).provider == AiSettings::Provider::Free);
    Json missing = Json::object();
    CHECK(AiSettings::fromJson(missing).provider == AiSettings::Provider::Free);
    if (!std::getenv("ANTHROPIC_API_KEY")) {
        AiSettings old = AiSettings::defaults(AiSettings::Provider::Anthropic);
        AiSettings migrated = AiSettings::fromJson(old.toJson());
        CHECK(migrated.provider == AiSettings::Provider::Free && migrated.model == "openai");
    }
    AiSettings keyed = AiSettings::defaults(AiSettings::Provider::Anthropic);
    keyed.apiKey = "k";
    CHECK(AiSettings::fromJson(keyed.toJson()).provider == AiSettings::Provider::Anthropic);
    AiSettings ollama = AiSettings::defaults(AiSettings::Provider::OpenAICompatible);
    ollama.model = "qwen3:4b-instruct";
    AiSettings back = AiSettings::fromJson(ollama.toJson());
    CHECK(back.provider == AiSettings::Provider::OpenAICompatible && back.model == "qwen3:4b-instruct");
    CHECK(modelsUrl("http://localhost:11434/v1/chat/completions") == "http://localhost:11434/v1/models");
    CHECK(modelsUrl("http://localhost:1234/v1/") == "http://localhost:1234/v1/models");
}

namespace {

Image makeImage(int w, int h, const std::function<glm::vec4(float, float)>& f) {
    Image img;
    img.width = w;
    img.height = h;
    img.rgba.resize(size_t(w) * size_t(h) * 4);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            glm::vec4 c = glm::clamp(f((x + 0.5f) / w, (y + 0.5f) / h), 0.0f, 1.0f) * 255.0f;
            for (int k = 0; k < 4; ++k) img.rgba[(size_t(y) * w + x) * 4 + k] = uint8_t(c[k] + 0.5f);
        }
    return img;
}

// Minimal 32-bit BMP writer so command tests can load real files.
void writeBmp(const std::string& path, const Image& img) {
    std::ofstream f(path, std::ios::binary);
    auto u32 = [&](uint32_t v) { f.write(reinterpret_cast<const char*>(&v), 4); };
    auto u16 = [&](uint16_t v) { f.write(reinterpret_cast<const char*>(&v), 2); };
    const uint32_t dataSize = uint32_t(img.width * img.height * 4);
    f.write("BM", 2);
    u32(54 + dataSize); u32(0); u32(54);
    u32(40); u32(uint32_t(img.width)); u32(uint32_t(-img.height)); u16(1); u16(32); u32(0); u32(dataSize);
    u32(2835); u32(2835); u32(0); u32(0);
    for (size_t i = 0; i < img.rgba.size(); i += 4) {
        const char px[4] = {char(img.rgba[i + 2]), char(img.rgba[i + 1]), char(img.rgba[i]), char(255)};
        f.write(px, 4);
    }
}

} // namespace

TEST(image_relief) {
    Image ramp = makeImage(64, 32, [](float u, float) { return glm::vec4(u, u, u, 1.0f); });
    ReliefOptions o;
    o.resolution = 64;
    Mesh m = makeImageRelief(ramp, o);
    AABB b = m.bounds();
    CHECK(std::abs(b.extent().x - 2.0f) < 1e-4f && std::abs(b.extent().y - 1.0f) < 1e-4f); // keeps aspect
    float leftZ = 0, rightZ = 0;
    for (const auto& p : m.positions) {
        if (p.x < -0.95f) leftZ = std::max(leftZ, p.z);
        if (p.x > 0.95f) rightZ = std::max(rightZ, p.z);
    }
    CHECK(rightZ > leftZ + 0.1f); // light side is higher
}

TEST(image_inflate_silhouette) {
    // A disk on a transparent background inflates into a closed, symmetric, ball-like shape.
    Image disk = makeImage(96, 96, [](float u, float v) {
        float d = glm::length(glm::vec2(u, v) - 0.5f);
        return glm::vec4(0.2f, 0.2f, 0.2f, d < 0.4f ? 1.0f : 0.0f);
    });
    InflateOptions o;
    o.resolution = 64;
    o.subdivisions = 0;
    std::string err;
    Mesh m = inflateSilhouette(disk, o, &err);
    CHECK(m.triangleCount() > 100);
    {
        std::map<std::pair<uint32_t, uint32_t>, int> edges;
        for (const auto& t : m.triangles)
            for (int k = 0; k < 3; ++k) ++edges[std::minmax(t[k], t[(k + 1) % 3])];
        bool manifold = true;
        for (const auto& [e, n] : edges) manifold = manifold && n == 2;
        CHECK(manifold);                                                                   // closed, no pinches
        CHECK(long(m.vertexCount()) - long(edges.size()) + long(m.triangleCount()) == 2); // sphere topology
    }
    AABB b = m.bounds();
    CHECK(b.extent().z > b.extent().x * 0.6f);              // round, not flat
    CHECK(std::abs(b.max.z + b.min.z) < 0.02f * b.extent().z); // front/back symmetric

    // Dark shape on a white page (no transparency) also works.
    Image sketch = makeImage(80, 60, [](float u, float v) {
        bool in = std::abs(u - 0.5f) < 0.3f && std::abs(v - 0.5f) < 0.2f;
        return in ? glm::vec4(0, 0, 0, 1) : glm::vec4(1, 1, 1, 1);
    });
    CHECK(inflateSilhouette(sketch, o, &err).triangleCount() > 0);
    Image blank = makeImage(32, 32, [](float, float) { return glm::vec4(1); });
    CHECK(inflateSilhouette(blank, o, &err).triangleCount() == 0 && !err.empty());
}

TEST(image_projection) {
    Mesh m = makeIcosphere(4);
    const auto before = m.positions;
    Image white = makeImage(16, 16, [](float, float) { return glm::vec4(1); });
    glm::vec3 eye(0, 0, 5);
    glm::mat4 vp = glm::perspective(glm::radians(35.0f), 1.0f, 0.1f, 20.0f) *
                   glm::lookAt(eye, glm::vec3(0), glm::vec3(0, 1, 0));
    ProjectOptions o;
    o.depth = 0.1f;
    size_t n = projectImage(m, -1, white, vp, 1.0f, eye, o);
    CHECK(n > 0);
    bool backUntouched = true, frontRaised = false;
    for (size_t i = 0; i < before.size(); ++i) {
        if (before[i].z < -0.1f) backUntouched = backUntouched && m.positions[i] == before[i];
        if (before[i].z > 0.95f) frontRaised = frontRaised || glm::length(m.positions[i]) > 1.05f;
    }
    CHECK(backUntouched && frontRaised);
}

TEST(gltf_roundtrip_with_layers) {
    Mesh m = makeIcosphere(3);
    int l = m.addLayer("Smile");
    for (size_t v = 0; v < m.vertexCount(); ++v) m.layers[l].delta[v] = glm::vec3(0, m.positions[v].y * 0.1f, 0);
    m.setLayerWeight(l, 0.25f);
    const std::string path = tempPath("sculptforge_roundtrip.glb");
    std::string err;
    CHECK(saveGlb(path, m, "Head", err));
    Mesh back;
    CHECK(loadGltf(path, back, err));
    CHECK(back.vertexCount() == m.vertexCount() && back.triangleCount() == m.triangleCount());
    CHECK(back.layers.size() == 1 && back.layers[0].name == "Smile" && std::abs(back.layers[0].weight - 0.25f) < 1e-6f);
    for (size_t v = 0; v < m.vertexCount(); ++v) CHECK(glm::distance(back.positions[v], m.positions[v]) < 1e-5f);
    std::filesystem::remove(path);

    CommandRegistry reg;
    Document d;
    CHECK(reg.execute(d, "new_sphere", {{"subdivisions", 2.0}}).ok);
    CHECK(reg.execute(d, "export_glb", {{"path", path}}).ok);
    CHECK(reg.execute(d, "open", {{"path", path}}).ok);
    CHECK(d.mesh.triangleCount() == 320);
    std::filesystem::remove(path);
}

TEST(image_commands) {
    const std::string bmp = tempPath("sculptforge_sketch.bmp");
    writeBmp(bmp, makeImage(64, 64, [](float u, float v) {
        float d = glm::length(glm::vec2(u, v) - 0.5f);
        return d < 0.35f ? glm::vec4(0.1f, 0.1f, 0.1f, 1) : glm::vec4(1, 1, 1, 1);
    }));
    CommandRegistry reg;
    Document d;
    CommandResult r = reg.execute(d, "image_inflate", {{"path", bmp}});
    CHECK(r.ok);
    if (!r.ok) std::printf("  %s\n", r.message.c_str());
    CHECK(d.mesh.triangleCount() > 500);
    CHECK(reg.execute(d, "image_project", {{"path", bmp}, {"depth", 0.02}}).ok);
    CHECK(reg.execute(d, "undo").ok);
    CHECK(reg.execute(d, "image_relief", {{"path", bmp}, {"resolution", 64.0}}).ok);
    CHECK(d.mesh.triangleCount() == 64u * 64u * 2u);
    CHECK(!reg.execute(d, "image_relief", {{"path", tempPath("missing_sculptforge.png")}}).ok);
    std::filesystem::remove(bmp);
}

// --- Voxel remesh, splats, PLY, USD, glTF splats, MCP, image-to-3D services ---

bool isClosedManifold(const Mesh& m) {
    std::map<std::pair<uint32_t, uint32_t>, int> edges;
    for (const auto& t : m.triangles)
        for (int k = 0; k < 3; ++k) ++edges[std::minmax(t[k], t[(k + 1) % 3])];
    for (const auto& [e, n] : edges)
        if (n != 2) return false;
    return !edges.empty();
}

double signedVolume(const Mesh& m) {
    double v = 0.0;
    for (const auto& t : m.triangles)
        v += double(glm::dot(m.positions[t.x], glm::cross(m.positions[t.y], m.positions[t.z]))) / 6.0;
    return v;
}

TEST(voxel_remesh) {
    Mesh sphere = makeIcosphere(4);
    std::string err;
    Mesh r = voxelRemesh(sphere, 64, &err);
    CHECK(r.triangleCount() > 1000);
    CHECK(isClosedManifold(r));
    const double vol = signedVolume(r);
    CHECK(vol > 0.0);                                  // faces point outwards
    CHECK(std::abs(vol - 4.18879) < 0.04 * 4.18879);   // unit-sphere volume preserved within 4%
    float worst = 0.0f;
    for (const auto& p : r.positions) worst = std::max(worst, std::abs(glm::length(p) - 1.0f));
    CHECK(worst < 0.03f);

    // Two overlapping spheres merge into one closed surface.
    Mesh a = makeIcosphere(3), b = makeIcosphere(3), both;
    for (auto& p : b.positions) p += glm::vec3(1.2f, 0, 0);
    both.positions = a.positions;
    both.positions.insert(both.positions.end(), b.positions.begin(), b.positions.end());
    both.triangles = a.triangles;
    for (auto t : b.triangles) both.triangles.push_back(t + glm::uvec3(uint32_t(a.vertexCount())));
    both.rebuildTopology();
    Mesh merged = voxelRemesh(both, 80, &err);
    CHECK(isClosedManifold(merged));
    CHECK(long(merged.vertexCount()) - long(merged.triangleCount() * 3 / 2) + long(merged.triangleCount()) == 2); // one piece

    CommandRegistry reg;
    Document d;
    CHECK(reg.execute(d, "new_sphere", {{"subdivisions", 3.0}}).ok);
    CHECK(reg.execute(d, "remesh", {{"resolution", 48.0}}).ok);
    CHECK(reg.execute(d, "undo").ok && d.mesh.triangleCount() == 1280);
    CHECK(!voxelRemesh(Mesh(), 64, &err).triangleCount() && !err.empty());
}

TEST(splats_mesh_conversions) {
    Mesh sphere = makeIcosphere(4);
    MeshToSplatsOptions o;
    o.count = 40000;
    SplatCloud c = meshToSplats(sphere, o);
    CHECK(c.size() == 40000 && c.validate().empty());
    bool onSurface = true, flat = true;
    for (size_t i = 0; i < c.size(); ++i) {
        onSurface = onSurface && std::abs(glm::length(c.positions[i]) - 1.0f) < 0.01f;
        const glm::vec3 n = glm::mat3_cast(c.rotations[i])[2]; // thin axis
        flat = flat && std::abs(glm::dot(n, glm::normalize(c.positions[i]))) > 0.98f && c.scales[i].z < c.scales[i].x * 0.2f;
    }
    CHECK(onSurface && flat);

    std::string err;
    SplatToMeshOptions so;
    so.resolution = 64;
    Mesh back = splatsToMesh(c, so, &err);
    CHECK(back.triangleCount() > 1000);
    CHECK(isClosedManifold(back));
    CHECK(signedVolume(back) > 0.0);
    const AABB b = back.bounds();
    CHECK(std::abs(b.extent().x - 2.0f) < 0.15f && std::abs(b.center().y) < 0.05f);

    // Floaters are dropped: only the largest solid survives.
    SplatCloud noisy = c;
    noisy.positions.push_back(glm::vec3(0, 3, 0));
    noisy.rotations.push_back(glm::quat(1, 0, 0, 0));
    noisy.scales.push_back(glm::vec3(0.1f));
    noisy.opacities.push_back(1.0f);
    noisy.sh.push_back(glm::vec3(0));
    so.keepFraction = 1.0f;
    CHECK(splatsToMesh(noisy, so, &err).bounds().max.y < 1.2f);

    // Placement: rotate/fit keep the data, bake applies it.
    SplatCloud t = c;
    t.transform = glm::translate(glm::mat4(1.0f), glm::vec3(5, 0, 0)) * glm::scale(glm::mat4(1.0f), glm::vec3(3.0f));
    fitSplats(t);
    CHECK(glm::length(t.bounds(0.9f).center()) < 0.05f);
    CHECK(std::abs(t.bounds(0.9f).extent().x - 2.0f) < 0.05f);
    t.bakeTransform();
    CHECK(t.transform == glm::mat4(1.0f) && glm::length(t.bounds(0.9f).center()) < 0.05f);
    CHECK(cleanSplats(noisy, 0.05f, 0.9f) > 0);
}

SplatCloud testCloud(int degree) {
    SplatCloud c;
    c.resize(50, degree);
    for (size_t i = 0; i < c.size(); ++i) {
        const float f = float(i);
        c.positions[i] = glm::vec3(std::sin(f), std::cos(f * 0.7f), f * 0.01f);
        c.rotations[i] = glm::normalize(glm::quat(1.0f, 0.1f * f, -0.05f * f, 0.3f));
        c.scales[i] = glm::vec3(0.01f + 0.001f * f, 0.02f, 0.005f);
        c.opacities[i] = 0.1f + 0.015f * f;
        for (int k = 0; k < c.shCount(); ++k) c.sh[i * size_t(c.shCount()) + size_t(k)] = glm::vec3(0.1f * float(k), -0.2f * f / 50.0f, 0.3f);
    }
    return c;
}

bool sameRotation(const glm::quat& a, const glm::quat& b, float tol) { return std::abs(std::abs(glm::dot(a, b)) - 1.0f) < tol; }

TEST(splat_ply_and_dot_splat_roundtrip) {
    for (int degree : {0, 1, 3}) {
        SplatCloud c = testCloud(degree);
        const std::string path = tempPath("sculptforge_splats.ply");
        std::string err;
        CHECK(saveSplatPly(path, c, err));
        Mesh m;
        SplatCloud back;
        PlyContent content;
        CHECK(loadPly(path, m, back, content, err) && content == PlyContent::Splats);
        CHECK(back.size() == c.size() && back.shDegree == degree);
        bool same = true;
        for (size_t i = 0; i < c.size(); ++i) {
            same = same && glm::distance(back.positions[i], c.positions[i]) < 1e-6f;
            same = same && glm::distance(back.scales[i], c.scales[i]) < 1e-6f;
            same = same && std::abs(back.opacities[i] - c.opacities[i]) < 1e-5f;
            same = same && sameRotation(back.rotations[i], c.rotations[i], 1e-6f);
        }
        for (size_t k = 0; k < c.sh.size(); ++k) same = same && glm::distance(back.sh[k], c.sh[k]) < 1e-6f;
        CHECK(same);
        std::filesystem::remove(path);
    }
    SplatCloud c = testCloud(0);
    const std::string path = tempPath("sculptforge_splats.splat");
    std::string err;
    CHECK(saveDotSplat(path, c, err));
    SplatCloud back;
    CHECK(loadDotSplat(path, back, err) && back.size() == c.size());
    bool close = true;
    for (size_t i = 0; i < c.size(); ++i) {
        close = close && glm::distance(back.positions[i], c.positions[i]) < 1e-6f;
        close = close && glm::distance(back.color(i), glm::clamp(c.color(i), 0.0f, 1.0f)) < 0.01f;
        close = close && sameRotation(back.rotations[i], c.rotations[i], 0.01f);
    }
    CHECK(close);
    std::filesystem::remove(path);
}

TEST(ply_mesh_and_point_cloud) {
    const std::string path = tempPath("sculptforge_quad.ply");
    {
        std::ofstream f(path);
        f << "ply\nformat ascii 1.0\ncomment a unit quad pyramid\nelement vertex 5\nproperty float x\nproperty float y\n"
             "property float z\nelement face 5\nproperty list uchar int vertex_indices\nend_header\n"
             "0 0 0\n1 0 0\n1 1 0\n0 1 0\n0.5 0.5 1\n4 3 2 1 0\n3 0 1 4\n3 1 2 4\n3 2 3 4\n3 3 0 4\n";
    }
    Mesh m;
    SplatCloud s;
    PlyContent content;
    std::string err;
    CHECK(loadPly(path, m, s, content, err) && content == PlyContent::Mesh);
    CHECK(m.vertexCount() == 5 && m.triangleCount() == 6 && isClosedManifold(m));
    {
        std::ofstream f(path, std::ios::binary);
        f << "ply\nformat binary_big_endian 1.0\nelement vertex 2\nproperty double x\nproperty double y\nproperty double z\n"
             "property uchar red\nproperty uchar green\nproperty uchar blue\nend_header\n";
        auto be = [&](double d) {
            uint64_t u;
            std::memcpy(&u, &d, 8);
            for (int k = 7; k >= 0; --k) f.put(char((u >> (k * 8)) & 0xFF));
        };
        be(1); be(2); be(3); f.put(char(255)); f.put(0); f.put(0);
        be(-1); be(0); be(0); f.put(0); f.put(0); f.put(char(255));
    }
    CHECK(loadPly(path, m, s, content, err) && content == PlyContent::Splats);
    CHECK(s.size() == 2 && s.positions[0] == glm::vec3(1, 2, 3) && glm::distance(s.color(0), glm::vec3(1, 0, 0)) < 1e-5f);
    std::filesystem::remove(path);
    CHECK(!loadPly(tempPath("missing_sculptforge.ply"), m, s, content, err));
}

TEST(zip_and_usdz_alignment) {
    const char* digits = "123456789";
    CHECK(crc32(reinterpret_cast<const uint8_t*>(digits), 9) == 0xCBF43926u);
    const std::string path = tempPath("sculptforge_test.zip");
    std::vector<ZipEntry> entries = {{"a.usda", std::vector<uint8_t>(100, 'a')}, {"textures/b.png", std::vector<uint8_t>(7, 'b')}};
    std::string err;
    CHECK(writeStoredZip(path, entries, 64, err));
    std::vector<uint8_t> z;
    {
        std::ifstream f(path, std::ios::binary); // closed before remove(): Windows can't delete open files
        z.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    }
    // Find each entry's data via the local headers and check 64-byte alignment.
    size_t p = 0, aligned = 0;
    while (p + 30 <= z.size() && z[p] == 'P' && z[p + 1] == 'K' && z[p + 2] == 3 && z[p + 3] == 4) {
        const size_t nameLen = z[p + 26] | z[p + 27] << 8, extra = z[p + 28] | z[p + 29] << 8;
        const size_t size = z[p + 18] | z[p + 19] << 8 | z[p + 20] << 16 | size_t(z[p + 21]) << 24;
        const size_t data = p + 30 + nameLen + extra;
        aligned += data % 64 == 0;
        p = data + size;
    }
    CHECK(aligned == 2);
    std::vector<ZipEntry> back;
    CHECK(readStoredZip(path, back, err) && back.size() == 2 && back[1].name == "textures/b.png" && back[0].data == entries[0].data);
    std::filesystem::remove(path);
}

TEST(usd_roundtrip_mesh_layers_splats) {
    Mesh m = makeIcosphere(2);
    int smile = m.addLayer("Smile 2");
    int blink = m.addLayer("Blink");
    for (size_t v = 0; v < m.vertexCount(); ++v)
        if (m.positions[v].y > 0.5f) m.layers[smile].delta[v] = glm::vec3(0, 0.1f, 0);
    m.layers[blink].delta[3] = glm::vec3(0.2f, 0, 0);
    m.setLayerWeight(smile, 0.75f);
    m.setLayerWeight(blink, 0.0f);
    SplatCloud c = testCloud(1);
    c.transform = glm::translate(glm::mat4(1.0f), glm::vec3(0, 2, 0));

    for (const char* name : {"sculptforge_test.usda", "sculptforge_test.usdz"}) {
        const std::string path = tempPath(name);
        std::string err;
        CHECK(saveUsd(path, &m, &c, "Hero Head", err));
        Mesh back;
        SplatCloud sback;
        CHECK(loadUsd(path, back, sback, err));
        if (!err.empty()) std::printf("  %s\n", err.c_str());
        CHECK(back.vertexCount() == m.vertexCount() && back.triangleCount() == m.triangleCount());
        CHECK(back.layers.size() == 2 && back.layers[0].name == "Smile_2" && std::abs(back.layers[0].weight - 0.75f) < 1e-6f);
        bool same = true;
        for (size_t v = 0; v < m.vertexCount(); ++v) {
            same = same && glm::distance(back.base[v], m.base[v]) < 1e-6f;
            same = same && glm::distance(back.layers[0].delta[v], m.layers[0].delta[v]) < 1e-6f;
            same = same && glm::distance(back.layers[1].delta[v], m.layers[1].delta[v]) < 1e-6f;
            same = same && glm::distance(back.positions[v], m.positions[v]) < 1e-5f;
        }
        CHECK(same);
        CHECK(sback.size() == c.size() && sback.shDegree == 1);
        bool splatsSame = true;
        for (size_t i = 0; i < c.size(); ++i) {
            splatsSame = splatsSame && glm::distance(sback.worldPosition(i), c.worldPosition(i)) < 1e-5f;
            splatsSame = splatsSame && sameRotation(sback.rotations[i], c.rotations[i], 1e-6f);
            splatsSame = splatsSame && std::abs(sback.opacities[i] - c.opacities[i]) < 1e-6f;
        }
        for (size_t k = 0; k < c.sh.size(); ++k) splatsSame = splatsSame && glm::distance(sback.sh[k], c.sh[k]) < 1e-6f;
        CHECK(splatsSame);
        std::filesystem::remove(path);
    }
    CHECK(usdIdentifier("3 eyes!") == "_3_eyes_");
}

TEST(usd_reader_handles_foreign_files) {
    const std::string path = tempPath("sculptforge_foreign.usda");
    {
        std::ofstream f(path);
        f << R"(#usda 1.0
(
    doc = """A multi-line
    doc with { braces } and "quotes" """
    upAxis = "Z"
    customLayerData = { string creator = "test" }
)

class "Template" { def Mesh "Ignored" { point3f[] points = [(9,9,9),(9,9,8),(9,8,9)] int[] faceVertexCounts = [3] int[] faceVertexIndices = [0,1,2] } }

def Xform "World" (
    kind = "assembly"
)
{
    double3 xformOp:translate = (0, 0, 10)  # moved up 10 in Z-up
    float3 xformOp:scale = (2, 2, 2)
    uniform token[] xformOpOrder = ["xformOp:translate", "xformOp:scale"]

    def Mesh "Quad" (
        prepend apiSchemas = ["MaterialBindingAPI"]
    )
    {
        uniform token orientation = "leftHanded"
        int[] faceVertexCounts = [4]
        int[] faceVertexIndices = [0, 1, 2, 3]
        point3f[] points.timeSamples = {
            1: [(0, 0, 0), (1, 0, 0), (1, 1, 0), (0, 1, 0)],
            2: [(5, 5, 5), (6, 5, 5), (6, 6, 5), (5, 6, 5)],
        }
        color3f[] primvars:displayColor = [(1, 0, 0)] (
            interpolation = "constant"
        )
        rel material:binding = </World/Looks/Red>
    }

    variantSet "lod" = {
        "high" { def Mesh "Variant" { point3f[] points = [(7,7,7)] } }
    }
}
)";
    }
    Mesh m;
    SplatCloud s;
    std::string err;
    CHECK(loadUsd(path, m, s, err));
    if (!err.empty()) std::printf("  %s\n", err.c_str());
    CHECK(m.vertexCount() == 4 && m.triangleCount() == 2 && s.empty());
    const AABB b = m.bounds();
    // Z-up converted to Y-up: translate (0,0,10) becomes +10 in Y; scale 2 makes the quad 2x2 in the X/-Z plane.
    CHECK(std::abs(b.min.y - 10.0f) < 1e-5f && std::abs(b.max.y - 10.0f) < 1e-5f);
    CHECK(std::abs(b.extent().x - 2.0f) < 1e-5f && std::abs(b.extent().z - 2.0f) < 1e-5f);
    // Left-handed winding was flipped: the quad (CCW in XY seen from +Z) now faces -Z-up = ... down after conversion.
    const glm::vec3 n = glm::cross(m.positions[m.triangles[0].y] - m.positions[m.triangles[0].x],
                                   m.positions[m.triangles[0].z] - m.positions[m.triangles[0].x]);
    CHECK(n.y < 0.0f);
    {
        std::ofstream f(path, std::ios::binary);
        f << "PXR-USDC binary";
    }
    CHECK(!loadUsd(path, m, s, err) && err.find("usdc") != std::string::npos);
    std::filesystem::remove(path);
}

TEST(gltf_gaussian_splatting_roundtrip) {
    SplatCloud c = testCloud(2);
    c.transform = glm::translate(glm::mat4(1.0f), glm::vec3(1, 2, 3)) * glm::scale(glm::mat4(1.0f), glm::vec3(0.5f));
    const std::string path = tempPath("sculptforge_splats.glb");
    std::string err;
    CHECK(saveSplatGlb(path, c, err));
    Mesh m;
    SplatCloud back;
    CHECK(loadGltf(path, m, err, &back));
    CHECK(m.triangleCount() == 0 && back.size() == c.size() && back.shDegree == 2);
    bool same = true;
    for (size_t i = 0; i < c.size(); ++i) {
        same = same && glm::distance(back.worldPosition(i), c.worldPosition(i)) < 1e-5f;
        same = same && sameRotation(back.rotations[i], c.rotations[i], 1e-6f);
        same = same && glm::distance(back.scales[i], c.scales[i]) < 1e-7f;
    }
    for (size_t k = 0; k < c.sh.size(); ++k) same = same && glm::distance(back.sh[k], c.sh[k]) < 1e-6f;
    CHECK(same);
    // Opening it as a document keeps the mesh and adds the splats.
    CommandRegistry reg;
    Document d;
    CHECK(reg.execute(d, "new_sphere", {{"subdivisions", 1.0}}).ok);
    CHECK(reg.execute(d, "open", {{"path", path}}).ok && d.splats.size() == c.size() && d.mesh.triangleCount() == 80);
    CHECK(!loadGltf(path, m, err)); // without a splat target, a splat-only file has no meshes
    std::filesystem::remove(path);
}

TEST(splat_commands) {
    CommandRegistry reg;
    Document d;
    CHECK(!reg.execute(d, "splat_to_mesh").ok); // nothing loaded
    CHECK(reg.execute(d, "new_sphere", {{"subdivisions", 3.0}}).ok);
    const uint64_t v0 = d.splatsVersion;
    CHECK(reg.execute(d, "mesh_to_splats", {{"count", 20000.0}}).ok && d.splats.size() == 20000 && d.splatsVersion != v0);
    CHECK(reg.execute(d, "splat_transform", {{"rotate_x", 180.0}, {"fit", true}}).ok);
    CHECK(reg.execute(d, "splat_clean", {{"min_opacity", 0.5}}).ok);
    CHECK(reg.execute(d, "splat_to_mesh", {{"resolution", 48.0}}).ok && isClosedManifold(d.mesh));
    // Make sculptable adds SubTools (the hidden scan, the result) and leaves the sculpt as it was.
    CHECK(d.subtoolCount() == 3 && d.subtoolMesh(0).triangleCount() == 1280);
    for (const char* ext : {".ply", ".splat", ".glb", ".usda", ".usdz"}) {
        const std::string path = tempPath((std::string("sculptforge_cmd_splats") + ext).c_str());
        CHECK(reg.execute(d, "splat_save", {{"path", path}}).ok);
        Document e;
        CHECK(reg.execute(e, "open", {{"path", path}}).ok && e.splats.size() == d.splats.size());
        std::filesystem::remove(path);
    }
    CHECK(!reg.execute(d, "splat_save", {{"path", tempPath("sculptforge_bad.txt")}}).ok);
    const std::string usd = tempPath("sculptforge_cmd.usdz");
    CHECK(reg.execute(d, "export_usd", {{"path", usd}}).ok);
    std::filesystem::remove(usd);
    CHECK(reg.execute(d, "splat_clear").ok && d.splats.empty());
}

TEST(mcp_server_protocol) {
    CommandRegistry reg;
    Document d;
    auto call = [&](const std::string& line) {
        Json j;
        std::string err;
        const std::string out = handleMcpMessage(reg, d, line);
        if (!out.empty()) Json::parse(out, j, err);
        return j;
    };
    Json init = call(R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-06-18","capabilities":{},"clientInfo":{"name":"t","version":"1"}}})");
    CHECK(init["result"]["protocolVersion"].asString() == "2025-06-18");
    CHECK(init["result"]["serverInfo"]["name"].asString() == "sculptforge" && init["result"]["capabilities"]["tools"].isObject());
    CHECK(handleMcpMessage(reg, d, R"({"jsonrpc":"2.0","method":"notifications/initialized"})").empty());
    Json list = call(R"({"jsonrpc":"2.0","id":2,"method":"tools/list"})");
    bool hasRemesh = false, schemasOk = true, packTools = false;
    for (const auto& t : list["result"]["tools"].items()) {
        hasRemesh = hasRemesh || t["name"].asString() == "remesh";
        schemasOk = schemasOk && t["inputSchema"]["type"].asString() == "object";
        packTools = packTools || t["name"].asString() == "pack_install" || t["name"].asString() == "pack_remove";
    }
    // Every command but installing, removing and switching packs (the user's decision, never an agent's).
    CHECK(hasRemesh && schemasOk && !packTools && list["result"]["tools"].size() == reg.all().size() - 4);
    Json refused = call(R"({"jsonrpc":"2.0","id":9,"method":"tools/call","params":{"name":"pack_install","arguments":{"path":"x.sfpack"}}})");
    CHECK(refused.has("error"));
    Json ok = call(R"({"jsonrpc":"2.0","id":"a","method":"tools/call","params":{"name":"new_sphere","arguments":{"subdivisions":2}}})");
    CHECK(ok["id"].asString() == "a" && !ok["result"]["isError"].asBool(true) && d.mesh.triangleCount() == 320);
    CHECK(ok["result"]["content"][0]["text"].asString().find("320") != std::string::npos);
    Json bad = call(R"({"jsonrpc":"2.0","id":4,"method":"tools/call","params":{"name":"open","arguments":{"path":"/nonexistent/x.obj"}}})");
    CHECK(bad["result"]["isError"].asBool(false));
    CHECK(call(R"({"jsonrpc":"2.0","id":5,"method":"nope"})")["error"]["code"].asNumber() == -32601);
    CHECK(call("not json")["error"]["code"].asNumber() == -32700);
    CHECK(call(R"({"jsonrpc":"2.0","id":6,"method":"ping"})")["result"].isObject());
}

TEST(image_to_3d_service_requests) {
    const uint8_t man[] = {'M', 'a', 'n'};
    CHECK(ai::base64Encode(man, 3) == "TWFu" && ai::base64Encode(man, 2) == "TWE=" && ai::base64Encode(man, 1) == "TQ==");
    Json body;
    std::string err;
    CHECK(Json::parse(ai::falSubmitBody("fal-ai/trellis-2", "data:x"), body, err) && body["image_url"].asString() == "data:x");
    CHECK(Json::parse(ai::falSubmitBody("fal-ai/hunyuan-3d/v3.1/rapid/image-to-3d", "data:x"), body, err) &&
          body["input_image_url"].asString() == "data:x" && body["enable_geometry"].asBool());
    CHECK(Json::parse(ai::meshySubmitBody("", "data:x"), body, err) && body["ai_model"].asString() == "latest" &&
          !body["should_texture"].asBool(true) && body["target_formats"][0].asString() == "glb");

    std::string ext;
    Json r;
    Json::parse(R"({"model_glb":{"url":"https://cdn/x/model.glb","content_type":"model/gltf-binary"}})", r, err);
    CHECK(ai::findModelUrl(r, &ext) == "https://cdn/x/model.glb" && ext == ".glb");
    Json::parse(R"({"model_glb":{"url":"https://cdn/x/mesh.obj?sig=1","content_type":"text/plain"}})", r, err);
    CHECK(ai::findModelUrl(r, &ext) == "https://cdn/x/mesh.obj?sig=1" && ext == ".obj");
    Json::parse(R"({"model_urls":{"glb":"https://assets.meshy.ai/t/model.glb?Expires=1","fbx":"https://a/b.fbx"}})", r, err);
    CHECK(ai::findModelUrl(r, &ext) == "https://assets.meshy.ai/t/model.glb?Expires=1");
    Json::parse(R"({"outputs":[{"file":{"href":"https://x/deep.glb"}}]})", r, err);
    CHECK(ai::findModelUrl(r, &ext) == "https://x/deep.glb");
    Json::parse(R"({"images":[]})", r, err);
    CHECK(ai::findModelUrl(r, &ext).empty());

    // A small character on a big transparent canvas is cropped and centred on a square.
    Image canvas = makeImage(400, 200, [](float u, float v) {
        const bool body = u > 0.1f && u < 0.2f && v > 0.5f && v < 0.9f;
        return glm::vec4(1, 0, 0, body ? 1.0f : 0.0f);
    });
    Image framed = canvas.framedSubject(0.08f);
    CHECK(framed.width == framed.height && framed.width < 120 && framed.width > 80);
    CHECK(framed.rgba[(size_t(framed.height / 2) * size_t(framed.width) + size_t(framed.width / 2)) * 4 + 3] == 255);
    Image big = makeImage(3000, 1500, [](float, float) { return glm::vec4(0.5f, 0.5f, 0.5f, 1); });
    Image small = big.downscaled(1024);
    CHECK(small.width == 1024 && small.height == 512 && small.rgba[0] > 120 && small.rgba[0] < 135);
    std::vector<uint8_t> png;
    CHECK(small.encodePng(png) && png.size() > 8 && png[1] == 'P' && png[2] == 'N' && png[3] == 'G');

    // Missing keys fail fast with a helpful message, without touching the network.
    ai::AiSettings s;
    s.falKey.clear();
    s.generatorService = "fal";
    if (!std::getenv("FAL_KEY")) {
        ai::GeneratorResult g = ai::generate3D(s, "unused.png", tempPath("sculptforge_gen"));
        CHECK(!g.ok && g.error.find("fal.ai") != std::string::npos);
    }
    // Settings survive a save/load.
    s.generatorService = "meshy";
    s.falModel = "fal-ai/hunyuan-3d/v3.1/pro/image-to-3d";
    ai::AiSettings loaded = ai::AiSettings::fromJson(s.toJson());
    CHECK(loaded.generatorService == "meshy" && loaded.falModel == s.falModel && loaded.meshyEndpoint == "https://api.meshy.ai");
}

// --- FBX export/import, Unreal preset and link ---

TEST(fbx_roundtrip_layers_and_lods) {
    Mesh m = makeIcosphere(3);
    int smile = m.addLayer("Smile");
    for (size_t v = 0; v < m.vertexCount(); ++v)
        if (m.positions[v].y > 0.3f) m.layers[smile].delta[v] = glm::vec3(0.0f, 0.05f, 0.02f);
    m.setLayerWeight(smile, 0.3f);
    const std::string path = tempPath("sculptforge_test.fbx");
    std::string err;
    FbxExportOptions o;
    o.scale = 100.0f; // metres -> centimetres
    CHECK(saveFbx(path, "Head", {&m}, o, err));
    {
        std::ifstream f(path, std::ios::binary);
        std::vector<char> head(27);
        f.read(head.data(), 27);
        CHECK(std::memcmp(head.data(), "Kaydara FBX Binary  \0\x1a\0", 23) == 0);
        uint32_t version;
        std::memcpy(&version, head.data() + 23, 4);
        CHECK(version == 7400);
    }
    Mesh back;
    CHECK(loadFbx(path, back, err));
    if (!err.empty()) std::printf("  %s\n", err.c_str());
    CHECK(back.vertexCount() == m.vertexCount() && back.triangleCount() == m.triangleCount());
    CHECK(back.layers.size() == 1 && back.layers[0].name == "Smile" && std::abs(back.layers[0].weight - 0.3f) < 1e-5f);
    bool same = true;
    for (size_t v = 0; v < m.vertexCount(); ++v) {
        same = same && glm::distance(back.base[v], m.base[v]) < 1e-5f;
        same = same && glm::distance(back.layers[0].delta[v], m.layers[0].delta[v]) < 1e-5f;
    }
    CHECK(same);
    CHECK(signedVolume(back) > 0.0); // winding survives the trip

    // LOD group: import takes LOD0 only.
    std::vector<Mesh> lods = generateLods(makeIcosphere(4), 3, 0.5f);
    CHECK(saveFbx(path, "Rock", {&lods[0], &lods[1], &lods[2]}, o, err));
    CHECK(loadFbx(path, back, err) && back.triangleCount() == lods[0].triangleCount());
    CHECK(!saveFbx(path, "Empty", {}, o, err));
    std::filesystem::remove(path);
    CHECK(!loadFbx(tempPath("missing_sculptforge.fbx"), back, err));
}

TEST(unreal_export_preset) {
    Document d;
    CommandRegistry reg;
    CHECK(reg.execute(d, "new_sphere", {{"subdivisions", 3.0}}).ok);
    const std::string folder = tempPath("sculptforge_unreal_test");
    UnrealExportOptions o;
    o.folder = folder;
    o.heightCm = 180.0f;
    o.lods = 3;
    UnrealExportResult r;
    std::string err;
    CHECK(exportForUnreal(d, o, r, err));
    CHECK(r.assetName == "SM_Sculpt" && !r.skeletal && r.lodCount == 3 && std::abs(r.scale - 90.0f) < 1e-3f);
    Mesh back;
    CHECK(loadFbx(r.path, back, err) && std::abs(back.bounds().extent().y - 1.8f) < 1e-3f); // 180 cm = 1.8 m

    CHECK(reg.execute(d, "layer_add", {{"name", std::string("Blink")}}).ok);
    CHECK(exportForUnreal(d, o, r, err));
    CHECK(r.assetName == "SK_Sculpt" && r.skeletal && r.lodCount == 1); // morph targets need a skeletal mesh
    o.assetName = "My Rock!";
    CHECK(exportForUnreal(d, o, r, err) && r.assetName == "My_Rock_");
    CHECK(reg.execute(d, "export_unreal", {{"folder", folder}, {"lods", 2.0}}).ok);
    CHECK(reg.execute(d, "export_fbx", {{"path", folder + "/plain.fbx"}, {"lods", 2.0}}).ok);
    std::filesystem::remove_all(folder);
}

TEST(unreal_link_messages_and_script) {
    Json msg;
    std::string err;
    CHECK(Json::parse(link::unrealMessage("ping", "abc", "", ""), msg, err));
    CHECK(msg["version"].asNumber() == 1 && msg["magic"].asString() == "ue_py" && msg["type"].asString() == "ping" &&
          msg["source"].asString() == "abc" && !msg.has("dest") && !msg.has("data"));
    CHECK(Json::parse(link::unrealMessage("open_connection", "a", "b", R"({"command_ip":"127.0.0.1","command_port":6776})"), msg, err));
    CHECK(msg["dest"].asString() == "b" && msg["data"]["command_port"].asNumber() == 6776);

    link::UnrealImport im;
    im.fbxPath = "C:\\Art\\it's.fbx";
    im.assetName = "SK_Head";
    im.skeletal = true;
    std::string py = link::unrealImportScript(im);
    CHECK(py.find("'C:\\\\Art\\\\it\\'s.fbx'") != std::string::npos); // escaped Python literal
    CHECK(py.find("FBXIT_SKELETAL_MESH") != std::string::npos && py.find("import_morph_targets', True") != std::string::npos);
    CHECK(py.find("automated_import_should_detect_type', False") != std::string::npos);
    CHECK(py.find("SCULPTFORGE_IMPORTED") != std::string::npos && py.find("urllib") == std::string::npos);
    im.skeletal = false;
    im.lods = true;
    im.downloadUrl = "http://10.0.0.5:5000/token/SM_Rock.fbx";
    py = link::unrealImportScript(im);
    CHECK(py.find("import_mesh_lo_ds', True") != std::string::npos && py.find("FBXIT_STATIC_MESH") != std::string::npos);
    CHECK(py.find("urlopen('http://10.0.0.5:5000/token/SM_Rock.fbx'") != std::string::npos);

    link::UnrealEditor ed;
    ed.address = "127.0.0.1";
    CHECK(ed.local());
    ed.address = "192.0.2.10";
    ed.machine = "some-other-machine-name";
    CHECK(!ed.local());
    CHECK(link::UnrealEndpoint().multicastTtl == 0 && link::UnrealEndpoint::network().multicastTtl == 1);
}

TEST(free_huggingface_models) {
    const auto& presets = ai::hfPresets();
    CHECK(presets.size() >= 4);
    bool wellFormed = true, usesImage = true;
    for (const auto& p : presets) {
        wellFormed = wellFormed && std::string(p.space).rfind("https://", 0) == 0 && !p.steps.empty();
        bool image = false;
        for (const auto& step : p.steps)
            for (const auto& a : step.args) {
                image = image || a == "$IMG";
                if (a != "$IMG" && a.rfind("$OUT", 0) != 0) {
                    Json v;
                    std::string err;
                    wellFormed = wellFormed && Json::parse(a, v, err); // literal args must be valid JSON
                }
            }
        usesImage = usesImage && image;
    }
    CHECK(wellFormed && usesImage);
    ai::AiSettings defaults;
    CHECK(defaults.generatorService == "huggingface" && defaults.hfModel == presets[0].id);
    CHECK(defaults.hfModel == "hunyuan3d-2.1" && !defaults.hfModelChosen); // free, works with no token
    CHECK(ai::hunyuanModel(defaults.hfModel) && !ai::hunyuanModel("trellis-2") && ai::hunyuanModel("fal-ai/hunyuan-3d/v3.1/pro/image-to-3d"));
    {
        // Default selection: an explicit choice is kept; a model the user never chose follows the default.
        ai::AiSettings chosen;
        chosen.hfModel = "hunyuan3d-2-turbo";
        chosen.hfModelChosen = true;
        chosen.hfToken = "hf_test";
        ai::AiSettings loaded = ai::AiSettings::fromJson(chosen.toJson());
        CHECK(loaded.hfModel == "hunyuan3d-2-turbo" && loaded.hfModelChosen && loaded.resolvedHfToken() == "hf_test");
        ai::AiSettings unchosen;
        unchosen.hfModel = "trellis-2"; // saved by "Save settings" without picking a model
        CHECK(ai::AiSettings::fromJson(unchosen.toJson()).hfModel == "hunyuan3d-2.1");
        CHECK(ai::AiSettings::fromJson(Json::object()).hfModel == "hunyuan3d-2.1"); // new install
        // ai.json from 0.10.8 and earlier (no hfModelChosen): TRELLIS.2 was the default, kept only with a token.
        Json old = Json::object();
        old.set("hfModel", "trellis-2");
        CHECK(ai::AiSettings::fromJson(old).hfModel == "hunyuan3d-2.1" && !ai::AiSettings::fromJson(old).hfModelChosen);
        old.set("hfToken", "hf_x");
        CHECK(ai::AiSettings::fromJson(old).hfModel == "trellis-2");
        Json picked = Json::object();
        picked.set("hfModel", "stable-fast-3d");
        CHECK(ai::AiSettings::fromJson(picked).hfModel == "stable-fast-3d");
        // The licence notice is shown once, ever.
        CHECK(!ai::AiSettings::fromJson(old).hunyuanNoticeShown);
        ai::AiSettings noticed;
        noticed.hunyuanNoticeShown = true;
        CHECK(ai::AiSettings::fromJson(noticed.toJson()).hunyuanNoticeShown);
    }
    {
        // TRELLIS.2 without a token falls back to Hunyuan3D 2.1 with a note; with one it runs. No token from the
        // environment either, for the length of this block.
        std::vector<std::pair<std::string, std::string>> savedEnv;
        for (const char* name : {"HF_TOKEN", "HUGGING_FACE_HUB_TOKEN"}) {
            if (const char* v = std::getenv(name)) savedEnv.emplace_back(name, v);
#ifdef _WIN32
            _putenv_s(name, "");
#else
            unsetenv(name);
#endif
        }
        ai::AiSettings s;
        std::string note;
        CHECK(std::string(ai::resolveHfPreset(s, false, &note).id) == "hunyuan3d-2.1" && note.empty());
        s.hfModel = "trellis-2";
        CHECK(std::string(ai::resolveHfPreset(s, false, &note).id) == "hunyuan3d-2.1");
        CHECK(note == ai::kTrellisNoTokenNote && note.find("Preferences > AI") != std::string::npos);
        note.clear();
        s.hfToken = "hf_test";
        CHECK(std::string(ai::resolveHfPreset(s, false, &note).id) == "trellis-2" && note.empty());
        s.hfModel = "no-such-model";
        CHECK(std::string(ai::resolveHfPreset(s, false).id) == ai::hfPresets()[0].id);
        s.hfModel = "hunyuan3d-2.1"; // not a text model: text uses the first text preset
        CHECK(std::string(ai::resolveHfPreset(s, true).id) == ai::hfTextPresets()[0].id);
        for (const auto& [name, value] : savedEnv) {
#ifdef _WIN32
            _putenv_s(name.c_str(), value.c_str());
#else
            setenv(name.c_str(), value.c_str(), 1);
#endif
        }
    }
    CHECK(std::string(ai::kHunyuanLicenceNote).find("EU, UK or South Korea") != std::string::npos);
    CHECK(ai::friendlyGradioError("You have exceeded your ZeroGPU quota (120s requested vs. 0s left).").find("free GPU time") !=
          std::string::npos);
    CHECK(ai::friendlyGradioError("some other failure") == "some other failure");
    std::string png, err;
    CHECK(!ai::prepareImagePng(tempPath("missing_sculptforge.png"), png, err));

    // Text to 3D.
    CHECK(!ai::hfTextPresets().empty());
    bool usesPrompt = true;
    for (const auto& p : ai::hfTextPresets()) {
        bool prompt = false;
        for (const auto& step : p.steps)
            for (const auto& a : step.args) prompt = prompt || a == "$PROMPT";
        usesPrompt = usesPrompt && prompt;
    }
    CHECK(usesPrompt);
    Json body;
    CHECK(Json::parse(ai::meshyTextBody("", "a fox"), body, err) && body["mode"].asString() == "preview" &&
          body["prompt"].asString() == "a fox" && body["ai_model"].asString() == "latest");
    CHECK(Json::parse(ai::falTextBody(std::string(300, 'x')), body, err) && body["prompt"].asString().size() == 200 &&
          body["enable_geometry"].asBool());
    ai::AiSettings offline;
    offline.generatorService = "inflate";
    CHECK(!ai::generate3DFromText(offline, "a fox", tempPath("sf_text")).ok);
    CHECK(!ai::generate3DFromText(offline, "   ", tempPath("sf_text")).ok);
    Json r;
    Json::parse(R"({"model_obj":{"url":"https://cdn/x/model.obj"},"model_urls":{"obj":"https://cdn/x/model.obj"}})", r, err);
    std::string ext;
    CHECK(ai::findModelUrl(r, &ext) == "https://cdn/x/model.obj" && ext == ".obj");
}
