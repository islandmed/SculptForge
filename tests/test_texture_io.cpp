// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// UVs and textures in the model formats: per-corner UVs survive OBJ, FBX, USD,
// glTF and GoZ round trips (triangles, kept quads, quads cut by UV seams), and
// a textured SubTool exports its PNG with the reference each format uses
// (OBJ .mtl map_Kd, FBX Texture, USD UsdUVTexture, embedded glTF image, GoZ
// diffuse map). Meshes without UVs export without any of it.
#include "TestHarness.h"

#include "core/Commands.h"
#include "core/Image.h"
#include "core/Primitives.h"
#include "core/SubTools.h"
#include "core/Texture.h"
#include "io/FbxIO.h"
#include "io/GltfIO.h"
#include "io/GozIO.h"
#include "io/ObjIO.h"
#include "io/TextureExport.h"
#include "io/UsdIO.h"
#include "io/Zip.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <functional>
#include <sstream>

using namespace sf;

namespace {

namespace fs = std::filesystem;

std::string readText(const fs::path& path) {
    std::ifstream f(path, std::ios::binary);
    std::stringstream s;
    s << f.rdbuf();
    return s.str();
}

// A fresh, empty folder for one test's files.
fs::path testDir(const char* name) {
    const fs::path dir = fs::temp_directory_path() / name;
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    return dir;
}

std::string str(const fs::path& p) { return p.string(); }

// Box-projected UVs from each triangle's dominant normal axis: continuous across the quads of a face.
void boxUvs(Mesh& m) {
    m.uvs.clear();
    for (const auto& t : m.triangles) {
        const glm::vec3 n = glm::abs(glm::cross(m.positions[t.y] - m.positions[t.x], m.positions[t.z] - m.positions[t.x]));
        const int axis = n.x >= n.y && n.x >= n.z ? 0 : n.y >= n.z ? 1 : 2;
        for (int c = 0; c < 3; ++c) {
            const glm::vec3& p = m.positions[t[c]];
            const glm::vec2 q = axis == 0 ? glm::vec2(p.y, p.z) : axis == 1 ? glm::vec2(p.x, p.z) : glm::vec2(p.x, p.y);
            m.uvs.push_back(q * 0.4f + glm::vec2(0.5f) + glm::vec2(float(axis) * 0.01f, 0.0f));
        }
    }
}

// A sphere whose UVs differ per triangle (a seam on every edge).
Mesh seamSphere() {
    Mesh m = makeIcosphere(2);
    for (uint32_t t = 0; t < m.triangleCount(); ++t)
        for (int c = 0; c < 3; ++c) {
            const glm::vec3& p = m.positions[m.triangles[t][c]];
            m.uvs.push_back(glm::vec2(p.x * 0.3f + 0.5f, p.y * 0.3f + 0.5f) + glm::vec2(float(t % 7) * 0.01f, float(t % 3) * 0.02f));
        }
    return m;
}

// A cube of quads with box UVs (quads stay quads).
Mesh quadCube() {
    Mesh m = makeCube(3);
    boxUvs(m);
    return m;
}

// A plane of quads whose halves have different UVs along the diagonal (written as triangles).
Mesh seamPlane() {
    Mesh m = makePlane(4);
    boxUvs(m);
    for (uint32_t t = 0; t < m.triangleCount(); ++t)
        if (t % 2)
            for (int c = 0; c < 3; ++c) m.uvs[t * 3 + size_t(c)].x += 0.125f;
    return m;
}

// Every triangle of `a` is in `b` (same corners in winding order, within `posEps`) with the same UVs (1e-5).
bool sameUvs(const Mesh& a, const Mesh& b, float posEps = 1e-4f) {
    if (a.triangleCount() != b.triangleCount() || a.uvs.size() != a.triangleCount() * 3 ||
        b.uvs.size() != b.triangleCount() * 3)
        return false;
    std::vector<uint8_t> used(b.triangleCount(), 0);
    for (uint32_t t = 0; t < a.triangleCount(); ++t) {
        bool found = false;
        for (uint32_t u = 0; u < b.triangleCount() && !found; ++u) {
            if (used[u]) continue;
            for (int r = 0; r < 3 && !found; ++r) {
                bool match = true;
                for (int c = 0; c < 3 && match; ++c) {
                    const int k = (c + r) % 3;
                    match = glm::length(a.positions[a.triangles[t][c]] - b.positions[b.triangles[u][k]]) < posEps &&
                            glm::length(a.uvs[t * 3 + size_t(c)] - b.uvs[u * 3 + size_t(k)]) < 1e-5f;
                }
                if (match) found = true, used[u] = 1;
            }
        }
        if (!found) return false;
    }
    return true;
}

// For files whose reader may triangulate quads along the other diagonal (ufbx): the same triangle count and
// every corner of `b` is a corner of `a` (position and UV).
bool sameCornerUvs(const Mesh& a, const Mesh& b, float posEps = 1e-4f) {
    if (a.triangleCount() != b.triangleCount() || b.uvs.size() != b.triangleCount() * 3) return false;
    for (uint32_t u = 0; u < b.triangleCount(); ++u)
        for (int k = 0; k < 3; ++k) {
            bool found = false;
            for (size_t c = 0; c < a.uvs.size() && !found; ++c)
                found = glm::length(a.positions[a.triangles[c / 3][int(c % 3)]] - b.positions[b.triangles[u][k]]) < posEps &&
                        glm::length(a.uvs[c] - b.uvs[u * 3 + size_t(k)]) < 1e-5f;
            if (!found) return false;
        }
    return true;
}

struct Format {
    const char* ext;
    std::function<bool(const std::string&, const Mesh&, std::string&)> save;
    std::function<bool(const std::string&, Mesh&, std::string&)> load;
};

std::vector<Format> formats() {
    return {
        {".obj", [](const std::string& p, const Mesh& m, std::string& e) { return saveObj(p, m, e); },
         [](const std::string& p, Mesh& m, std::string& e) { return loadObj(p, m, e); }},
        {".fbx",
         [](const std::string& p, const Mesh& m, std::string& e) {
             FbxExportOptions o;
             o.scale = 100.0f; // FBX is in cm; loadFbx gives metres
             return saveFbx(p, "Sculpt", {&m}, o, e);
         },
         [](const std::string& p, Mesh& m, std::string& e) { return loadFbx(p, m, e); }},
        {".usda", [](const std::string& p, const Mesh& m, std::string& e) { return saveUsd(p, &m, nullptr, "Sculpt", e); },
         [](const std::string& p, Mesh& m, std::string& e) {
             SplatCloud s;
             return loadUsd(p, m, s, e);
         }},
        {".usdz", [](const std::string& p, const Mesh& m, std::string& e) { return saveUsd(p, &m, nullptr, "Sculpt", e); },
         [](const std::string& p, Mesh& m, std::string& e) {
             SplatCloud s;
             return loadUsd(p, m, s, e);
         }},
        {".glb", [](const std::string& p, const Mesh& m, std::string& e) { return saveGlb(p, m, "Sculpt", e); },
         [](const std::string& p, Mesh& m, std::string& e) { return loadGltf(p, m, e); }},
        {".GoZ", [](const std::string& p, const Mesh& m, std::string& e) { return saveGoz(p, m, "Sculpt", e); },
         [](const std::string& p, Mesh& m, std::string& e) { return loadGoz(p, m, e); }},
    };
}

// A 16x16 white texture whose bottom row (v = 0) is red.
std::shared_ptr<PaintTexture> redBottomTexture() {
    auto tex = makeTexture(16, 16);
    for (int x = 0; x < 16; ++x) {
        tex->pixels[size_t(x) * 4 + 1] = 0;
        tex->pixels[size_t(x) * 4 + 2] = 0;
    }
    return tex;
}

// The PNG exists, has the texture's size and is top-down (the red row last).
bool checkPng(const fs::path& path) {
    Image img;
    std::string err;
    if (!Image::load(str(path), img, err) || img.width != 16 || img.height != 16) return false;
    const uint8_t* bottom = &img.rgba[size_t(15 * 16) * 4];
    const uint8_t* top = img.rgba.data();
    return bottom[0] == 255 && bottom[1] == 0 && top[1] == 255;
}

} // namespace

TEST(texture_io_uv_round_trip_every_format) {
    const fs::path dir = testDir("sf_texture_io_uvs");
    const std::vector<std::pair<const char*, Mesh>> meshes = {{"sphere", seamSphere()}, {"cube", quadCube()}, {"plane", seamPlane()}};
    for (const auto& f : formats())
        for (const auto& [name, mesh] : meshes) {
            const std::string path = str(dir / (std::string(name) + f.ext));
            std::string err;
            Mesh back;
            const bool ok = f.save(path, mesh, err) && f.load(path, back, err);
            CHECK(ok);
            const bool fbx = std::string(f.ext) == ".fbx";
            const bool same = ok && (fbx ? sameCornerUvs(mesh, back) : sameUvs(mesh, back));
            if (!same) std::printf("  %s %s: UVs differ (%s)\n", f.ext, name, err.c_str());
            CHECK(same);
            // Kept quads stay quads where the UVs allow it (glTF has no quads).
            if (std::string(name) == "cube" && std::string(f.ext) != ".glb") CHECK(back.quadCount() == mesh.quadCount());
        }
    fs::remove_all(dir);
}

TEST(texture_io_quads_keep_their_uvs_in_files) {
    const fs::path dir = testDir("sf_texture_io_quads");
    std::string err;
    const Mesh cube = quadCube(), plane = seamPlane();
    // OBJ: quads written as 4-corner faces with vt indices; a quad cut by a seam as two triangles.
    CHECK(saveObj(str(dir / "cube.obj"), cube, err));
    const std::string obj = readText(dir / "cube.obj");
    CHECK(obj.find("\nvt ") != std::string::npos);
    size_t quads = 0;
    for (size_t at = obj.find("\nf "); at != std::string::npos; at = obj.find("\nf ", at + 1)) {
        const size_t end = obj.find('\n', at + 1);
        const std::string line = obj.substr(at + 1, end - at - 1);
        quads += std::count(line.begin(), line.end(), '/') == 4;
    }
    CHECK(quads == cube.quadCount());
    CHECK(saveObj(str(dir / "plane.obj"), plane, err));
    const std::string planeObj = readText(dir / "plane.obj");
    size_t faces = 0;
    for (size_t at = planeObj.find("\nf "); at != std::string::npos; at = planeObj.find("\nf ", at + 1)) ++faces;
    CHECK(faces == plane.triangleCount());

    // USD: face-varying st in the faces' corner order.
    CHECK(saveUsd(str(dir / "cube.usda"), &cube, nullptr, "Sculpt", err));
    const std::string usda = readText(dir / "cube.usda");
    CHECK(usda.find("texCoord2f[] primvars:st = [") != std::string::npos);
    CHECK(usda.find("interpolation = \"faceVarying\"") != std::string::npos);
    fs::remove_all(dir);
}

TEST(texture_io_meshes_without_uvs_unchanged) {
    const fs::path dir = testDir("sf_texture_io_plain");
    std::string err;
    Mesh m = makeCube(2);
    m.texture = redBottomTexture(); // a texture without UVs is not exported
    CHECK(saveObj(str(dir / "a.obj"), m, err));
    const std::string obj = readText(dir / "a.obj");
    CHECK(obj.find("vt ") == std::string::npos && obj.find("mtllib") == std::string::npos);
    CHECK(saveUsd(str(dir / "a.usda"), &m, nullptr, "Sculpt", err));
    const std::string usda = readText(dir / "a.usda");
    CHECK(usda.find("primvars:st") == std::string::npos && usda.find("UsdUVTexture") == std::string::npos);
    CHECK(saveGlb(str(dir / "a.glb"), m, "Sculpt", err));
    const std::string glb = readText(dir / "a.glb");
    CHECK(glb.find("TEXCOORD_0") == std::string::npos && glb.find("\"images\"") == std::string::npos);
    CHECK(saveFbx(str(dir / "a.fbx"), "Sculpt", {&m}, FbxExportOptions{}, err));
    CHECK(readText(dir / "a.fbx").find("LayerElementUV") == std::string::npos);
    size_t files = 0;
    for (const auto& e : fs::directory_iterator(dir)) files += e.path().extension() == ".png" || e.path().extension() == ".mtl";
    CHECK(files == 0);
    Mesh back;
    CHECK(loadObj(str(dir / "a.obj"), back, err) && !back.hasUvs());
    CHECK(loadGltf(str(dir / "a.glb"), back, err) && !back.hasUvs());
    fs::remove_all(dir);
}

TEST(texture_io_obj_writes_mtl_and_png) {
    const fs::path dir = testDir("sf_texture_io_obj");
    std::string err;
    Mesh m = quadCube();
    m.texture = redBottomTexture();
    CHECK(hasTexture(m));
    CHECK(saveObj(str(dir / "model.obj"), m, err));
    const std::string obj = readText(dir / "model.obj");
    const std::string mtl = readText(dir / "model.mtl");
    CHECK(obj.find("mtllib model.mtl\n") != std::string::npos && obj.find("usemtl ") != std::string::npos);
    CHECK(mtl.find("newmtl ") != std::string::npos && mtl.find("map_Kd model_color.png\n") != std::string::npos);
    CHECK(checkPng(dir / "model_color.png"));

    // Several objects: <stem>_<object>_color.png for the textured ones, a plain material for the others.
    Mesh plain = makeIcosphere(1);
    for (auto& p : plain.positions) p += glm::vec3(3.0f, 0.0f, 0.0f);
    CHECK(saveObj(str(dir / "scene.obj"), {{"Body Main", &m}, {"Eye", &plain}}, err));
    const std::string sceneMtl = readText(dir / "scene.mtl");
    CHECK(sceneMtl.find("map_Kd scene_Body_Main_color.png") != std::string::npos);
    CHECK(sceneMtl.find("newmtl Clay") != std::string::npos);
    CHECK(checkPng(dir / "scene_Body_Main_color.png"));
    CHECK(!fs::exists(dir / "scene_Eye_color.png"));
    std::vector<MeshObject> objects;
    CHECK(loadObj(str(dir / "scene.obj"), objects, err) && objects.size() == 2);
    if (objects.size() == 2) CHECK(sameUvs(m, objects[0].mesh) && !objects[1].mesh.hasUvs());
    fs::remove_all(dir);
}

// OBJ's mtllib and MTL's map_Kd split file names at spaces: a model saved as "my scene.obj" names its
// .mtl and PNGs without them (the OBJ keeps its name), so other apps find them.
TEST(texture_io_sidecar_names_without_spaces) {
    const fs::path dir = testDir("sf_texture_io_spaces");
    std::string err;
    Mesh m = quadCube();
    m.texture = redBottomTexture();
    CHECK(saveObj(str(dir / "my scene.obj"), m, err));
    const std::string obj = readText(dir / "my scene.obj");
    CHECK(obj.find("mtllib my_scene.mtl\n") != std::string::npos);
    CHECK(fs::exists(dir / "my_scene.mtl") && !fs::exists(dir / "my scene.mtl"));
    CHECK(readText(dir / "my_scene.mtl").find("map_Kd my_scene_color.png\n") != std::string::npos);
    CHECK(checkPng(dir / "my_scene_color.png"));
    Mesh back;
    CHECK(loadObj(str(dir / "my scene.obj"), back, err) && sameUvs(m, back));
    // The other formats name their PNGs the same way.
    CHECK(saveFbx(str(dir / "my scene.fbx"), "Sculpt", {&m}, FbxExportOptions{}, err));
    CHECK(readText(dir / "my scene.fbx").find("my_scene_color.png") != std::string::npos);
    CHECK(textureFileNames(str(dir / "a\tb c.usda"), {{"Body", &m}}) == std::vector<std::string>{"a_b_c_color.png"});
    fs::remove_all(dir);
}

TEST(texture_io_fbx_usd_glb_goz_reference_texture) {
    const fs::path dir = testDir("sf_texture_io_refs");
    std::string err;
    Mesh m = seamSphere();
    m.texture = redBottomTexture();

    // FBX: the PNG beside the file, a Texture/Video pair naming it.
    CHECK(saveFbx(str(dir / "model.fbx"), "Sculpt", {&m}, FbxExportOptions{}, err));
    CHECK(checkPng(dir / "model_color.png"));
    const std::string fbx = readText(dir / "model.fbx");
    CHECK(fbx.find("model_color.png") != std::string::npos && fbx.find("TextureVideoClip") != std::string::npos);
    CHECK(fbx.find("LayerElementUV") != std::string::npos);
    fs::remove(dir / "model_color.png");

    // USD text: a UsdUVTexture reading the PNG beside the layer.
    CHECK(saveUsd(str(dir / "model.usda"), &m, nullptr, "Sculpt", err));
    const std::string usda = readText(dir / "model.usda");
    CHECK(usda.find("asset inputs:file = @model_color.png@") != std::string::npos);
    CHECK(usda.find("UsdUVTexture") != std::string::npos && usda.find("UsdPrimvarReader_float2") != std::string::npos);
    CHECK(usda.find("rel material:binding = </Sculpt/Materials/Sculpt_Material>") != std::string::npos);
    CHECK(checkPng(dir / "model_color.png"));
    fs::remove(dir / "model_color.png");

    // USDZ: the PNG inside the package.
    CHECK(saveUsd(str(dir / "model.usdz"), &m, nullptr, "Sculpt", err));
    CHECK(!fs::exists(dir / "model_color.png"));
    std::vector<ZipEntry> entries;
    CHECK(readStoredZip(str(dir / "model.usdz"), entries, err) && entries.size() == 2);
    if (entries.size() == 2) {
        CHECK(entries[1].name == "model_color.png");
        Image img;
        CHECK(Image::decode(entries[1].data.data(), entries[1].data.size(), img, err) && img.width == 16);
        const std::string text(entries[0].data.begin(), entries[0].data.end());
        CHECK(text.find("@model_color.png@") != std::string::npos);
    }

    // GLB: an embedded PNG image used as the base colour texture.
    CHECK(saveGlb(str(dir / "model.glb"), m, "Sculpt", err));
    const std::string glb = readText(dir / "model.glb");
    CHECK(glb.find("\"images\":[{\"bufferView\"") != std::string::npos && glb.find("image/png") != std::string::npos);
    CHECK(glb.find("\"baseColorTexture\":{\"index\":0}") != std::string::npos && glb.find("\"material\":0") != std::string::npos);
    CHECK(glb.find("TEXCOORD_0") != std::string::npos);
    CHECK(!fs::exists(dir / "model_color.png"));
    Mesh back;
    CHECK(loadGltf(str(dir / "model.glb"), back, err) && sameUvs(m, back));

    // GoZ: the PNG beside the file, recorded as the diffuse map.
    CHECK(saveGoz(str(dir / "model.GoZ"), m, "Sculpt", err));
    CHECK(checkPng(dir / "model_color.png"));
    GozInfo info;
    CHECK(loadGoz(str(dir / "model.GoZ"), back, err, &info));
    CHECK(info.diffuseMap.ends_with("model_color.png"));
    fs::remove_all(dir);
}

TEST(texture_io_export_commands_write_textures) {
    const fs::path dir = testDir("sf_texture_io_commands");
    Mesh body = quadCube();
    body.texture = redBottomTexture();
    Mesh eye = makeIcosphere(1, 0.2f);
    for (auto& p : eye.positions) p += glm::vec3(2.0f, 0.0f, 0.0f);
    eye.recomputeAllNormals();
    Document doc;
    doc.setMesh(std::move(body), "Body");
    doc.addSubtool(std::move(eye), "Eye", false);
    // Merging keeps the UVs (meshes without get zeros).
    const Mesh merged = mergedVisibleMesh(doc);
    CHECK(merged.uvs.size() == merged.triangleCount() * 3);

    CommandRegistry reg;
    CHECK(reg.execute(doc, "save_obj", {{"path", str(dir / "scene.obj")}}).ok);
    CHECK(checkPng(dir / "scene_Body_color.png") && fs::exists(dir / "scene.mtl"));
    CHECK(reg.execute(doc, "export_fbx", {{"path", str(dir / "scene.fbx")}, {"lods", 2.0}}).ok);
    std::vector<MeshObject> objects;
    std::string err;
    CHECK(loadFbx(str(dir / "scene.fbx"), objects, err) && objects.size() == 2);
    if (objects.size() == 2) CHECK(sameCornerUvs(doc.subtoolMesh(0), objects[0].mesh) && !objects[1].mesh.hasUvs());
    CHECK(reg.execute(doc, "export_usd", {{"path", str(dir / "scene.usdz")}}).ok);
    CHECK(reg.execute(doc, "export_lods", {{"path", str(dir / "rock.glb")}, {"levels", 2.0}}).ok);
    // Every LOD level embeds the texture.
    CHECK(readText(dir / "rock_LOD1.glb").find("image/png") != std::string::npos);
    CHECK(reg.execute(doc, "goz_export", {{"path", str(dir / "out.GoZ")}}).ok);
    CHECK(checkPng(dir / "out_Body_color.png"));
    fs::remove_all(dir);
}

// --- PBR material slots from material channels (core/TextureChannels.h, core/MapExport.h) ---

namespace {

// The red-bottom texture with roughness, metallic, normal, AO, emissive and opacity painted in places.
std::shared_ptr<PaintTexture> pbrTexture() {
    auto t = redBottomTexture();
    for (Channel c : {Channel::Roughness, Channel::Metallic, Channel::Normal, Channel::AO, Channel::Emissive, Channel::Opacity})
        setChannelEnabled(*t, c, true);
    const float rough[1] = {0.2f}, metal[1] = {1.0f}, alpha[1] = {0.5f};
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 16; ++x) {
            t->channels[size_t(Channel::Roughness)].base.setValues(x, y, rough);
            t->channels[size_t(Channel::Metallic)].base.setValues(x, y, metal);
            t->channels[size_t(Channel::Opacity)].base.setValues(x, y, alpha);
        }
    return t;
}

} // namespace

TEST(texture_io_pbr_slots_in_gltf_usd_obj_fbx) {
    const fs::path dir = testDir("sf_texture_io_pbr");
    std::string err;
    Mesh m = seamSphere();
    m.texture = pbrTexture();

    // glTF: ORM image on metallicRoughness and occlusion, normal, emissive, opacity -> BLEND.
    CHECK(saveGlb(str(dir / "model.glb"), m, "Sculpt", err));
    const std::string glb = readText(dir / "model.glb");
    CHECK(glb.find("\"baseColorTexture\":{\"index\":0}") != std::string::npos);
    CHECK(glb.find("\"metallicRoughnessTexture\":{\"index\":1},\"metallicFactor\":1,\"roughnessFactor\":1") != std::string::npos);
    CHECK(glb.find("\"occlusionTexture\":{\"index\":1}") != std::string::npos);
    CHECK(glb.find("\"normalTexture\":{\"index\":2}") != std::string::npos);
    CHECK(glb.find("\"emissiveTexture\":{\"index\":3},\"emissiveFactor\":[1,1,1]") != std::string::npos);
    CHECK(glb.find("\"alphaMode\":\"BLEND\"") != std::string::npos);
    CHECK(glb.find("Sculpt_occlusionRoughnessMetallic") != std::string::npos);
    CHECK(glb.find("\"materials\":[{") != std::string::npos);
    Mesh back;
    CHECK(loadGltf(str(dir / "model.glb"), back, err));

    // USD: every UsdPreviewSurface input reads its own raw map; normal with scale / bias.
    CHECK(saveUsd(str(dir / "model.usda"), &m, nullptr, "Sculpt", err));
    const std::string usda = readText(dir / "model.usda");
    CHECK(usda.find("float inputs:roughness.connect = </Sculpt/Materials/Sculpt_Material/RoughnessTexture.outputs:r>") != std::string::npos);
    CHECK(usda.find("float inputs:metallic.connect") != std::string::npos);
    CHECK(usda.find("normal3f inputs:normal.connect") != std::string::npos);
    CHECK(usda.find("float inputs:occlusion.connect") != std::string::npos);
    CHECK(usda.find("color3f inputs:emissiveColor.connect") != std::string::npos);
    CHECK(usda.find("float inputs:opacity.connect") != std::string::npos);
    CHECK(usda.find("@model_roughness.png@") != std::string::npos && usda.find("@model_normal.png@") != std::string::npos);
    CHECK(usda.find("float4 inputs:bias = (-1, -1, -1, 0)") != std::string::npos);
    CHECK(usda.find("token inputs:sourceColorSpace = \"raw\"") != std::string::npos);
    Image color;
    for (const char* f : {"model_color.png", "model_roughness.png", "model_metallic.png", "model_normal.png", "model_occlusion.png",
                          "model_emissive.png", "model_opacity.png"})
        CHECK(fs::exists(dir / f) && Image::load(str(dir / f), color, err) && color.width == 16);
    // The roughness map holds the painted value at the bottom (image rows top-down: the last row).
    Image rough;
    CHECK(Image::load(str(dir / "model_roughness.png"), rough, err));
    CHECK(std::abs(int(rough.rgba[(size_t(rough.height - 1) * size_t(rough.width)) * 4]) - 51) <= 1);
    CHECK(std::abs(int(rough.rgba[0]) - 166) <= 1);
    // Base colour carries the opacity in alpha.
    CHECK(Image::load(str(dir / "model_color.png"), color, err));
    CHECK(std::abs(int(color.rgba[(size_t(color.height - 1) * size_t(color.width)) * 4 + 3]) - 128) <= 1);
    CHECK(color.rgba[3] == 255);
    // USDZ packs the maps.
    CHECK(saveUsd(str(dir / "model.usdz"), &m, nullptr, "Sculpt", err));
    std::vector<ZipEntry> entries;
    CHECK(readStoredZip(str(dir / "model.usdz"), entries, err));
    CHECK(entries.size() == 8);

    // OBJ: the PBR MTL keys.
    CHECK(saveObj(str(dir / "model.obj"), m, err));
    const std::string mtl = readText(dir / "model.mtl");
    CHECK(mtl.find("map_Pr model_roughness.png") != std::string::npos);
    CHECK(mtl.find("map_Pm model_metallic.png") != std::string::npos);
    CHECK(mtl.find("norm model_normal.png") != std::string::npos);
    CHECK(mtl.find("map_Ke model_emissive.png") != std::string::npos);
    CHECK(mtl.find("map_d model_opacity.png") != std::string::npos);

    // FBX: maps on the material properties importers read.
    CHECK(saveFbx(str(dir / "model.fbx"), "Sculpt", {&m}, FbxExportOptions{}, err));
    const std::string fbx = readText(dir / "model.fbx");
    for (const char* key : {"NormalMap", "ShininessExponent", "ReflectionFactor", "EmissiveColor", "TransparencyFactor",
                            "model_normal.png", "model_roughness.png"})
        CHECK(fbx.find(key) != std::string::npos);

    // A texture without material channels exports exactly as before (no extra maps).
    Mesh plain = seamSphere();
    plain.texture = redBottomTexture();
    const fs::path pdir = testDir("sf_texture_io_pbr_plain");
    CHECK(saveObj(str(pdir / "plain.obj"), plain, err));
    CHECK(!fs::exists(pdir / "plain_roughness.png"));
    CHECK(readText(pdir / "plain.mtl").find("map_Pr") == std::string::npos);
    CHECK(saveGlb(str(pdir / "plain.glb"), plain, "Sculpt", err));
    CHECK(readText(pdir / "plain.glb").find("\"metallicFactor\":0,\"roughnessFactor\":0.65") != std::string::npos);
    fs::remove_all(dir);
    fs::remove_all(pdir);
}
