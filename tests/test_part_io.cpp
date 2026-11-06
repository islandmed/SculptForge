// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Parts in every file path: project files (version 2 round trip, version 1
// still loading), exporters writing one object per visible Part (OBJ, glTF,
// FBX incl. LOD groups and the Unreal preset, USD, Bridge) or the merged visible
// mesh (PLY, STL, splats), importers giving the objects back as Parts, and
// single-object files behaving as before.
#include "TestHarness.h"

#include "core/Commands.h"
#include "core/EnginePresets.h"
#include "core/Json.h"
#include "core/Primitives.h"
#include "core/Project.h"
#include "core/Parts.h"
#include "io/FbxIO.h"
#include "io/GltfIO.h"
#include "io/BridgeIO.h"
#include "io/BridgeProtocol.h"
#include "io/ObjIO.h"
#include "io/PlyIO.h"
#include "io/StlIO.h"
#include "io/UsdIO.h"
#include "io/Zip.h"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <set>
#include <sstream>

using namespace sf;

namespace {

std::string readText(const std::string& path) {
    std::ifstream f(std::filesystem::path(path), std::ios::binary);
    std::stringstream s;
    s << f.rdbuf();
    return s.str();
}

void writeText(const std::string& path, const std::string& text) {
    std::ofstream f(std::filesystem::path(path), std::ios::binary);
    f << text;
}

size_t countOf(const std::string& text, const std::string& what) {
    size_t n = 0;
    for (size_t at = text.find(what); at != std::string::npos; at = text.find(what, at + what.size())) ++n;
    return n;
}

size_t groupCount(const Mesh& m) {
    std::set<uint32_t> ids;
    for (uint32_t t = 0; t < m.triangleCount(); ++t) ids.insert(m.facegroupAt(t));
    return ids.size();
}

bool near(const glm::vec3& a, const glm::vec3& b, float eps = 1e-3f) { return glm::length(a - b) < eps; }

// A body with every attribute and two layers (Smile active), a hidden eye , a sword with
// face groups; the eye is the active Part.
Document makeScene() {
    Mesh body = makeIcosphere(2);
    body.ensureMask();
    body.ensureColors(glm::vec3(0.2f, 0.4f, 0.6f));
    body.ensureFaceGroups(3);
    for (uint32_t t = 0; t < body.triangleCount(); t += 2) body.facegroups[t] = 7;
    body.ensureHidden();
    body.hidden[5] = 1;
    body.uvs.assign(body.triangleCount() * 3, glm::vec2(0.25f, 0.75f));
    for (uint32_t v = 0; v < body.vertexCount(); ++v) body.mask[v] = float(v % 5) * 0.25f;
    body.addLayer("Smile");
    body.addLayer("Blink");
    for (uint32_t v = 0; v < body.vertexCount(); v += 3) body.layers[0].delta[v] = glm::vec3(0.0f, 0.1f, 0.0f);
    for (uint32_t v = 1; v < body.vertexCount(); v += 4) body.layers[1].delta[v] = glm::vec3(0.05f, 0.0f, 0.0f);
    body.setLayerWeight(0, 0.5f);
    body.setLayerWeight(1, 1.0f);

    Mesh eye = makeIcosphere(1, 0.2f);
    for (auto& p : eye.positions) p += glm::vec3(0.4f, 0.5f, 0.8f);
    eye.recomputeAllNormals();

    Mesh sword = makeCube(4, 0.5f);
    for (auto& p : sword.positions) p += glm::vec3(2.0f, 0.0f, 0.0f);
    sword.facegroups.assign(sword.triangleCount(), 1);
    for (uint32_t t = 0; t < sword.triangleCount() / 2; ++t) sword.facegroups[t] = 2;
    sword.recomputeAllNormals();

    Document doc;
    doc.setMesh(std::move(body), "Body");
    doc.activeLayer = 0;
    doc.addPart(std::move(eye), "Eye", true);
    doc.bridgeName = "Eye_ZB";
    doc.activePartInfo().visible = false;
    doc.addPart(std::move(sword), "Sword", false);
    doc.path = tempPath("sf_part_scene.sfproj");
    return doc;
}

// A scene of the body (with layers) and the sword, both visible, the eye hidden.
Document makeExportScene() {
    Document doc = makeScene();
    doc.selectPart(0);
    return doc;
}

void checkSameMesh(const Mesh& a, const Mesh& b) {
    CHECK(a.positions == b.positions);
    CHECK(a.triangles == b.triangles);
    CHECK(a.mask == b.mask && a.colors == b.colors && a.facegroups == b.facegroups);
    CHECK(a.hidden == b.hidden && a.uvs == b.uvs);
    CHECK(a.layers.size() == b.layers.size());
    for (size_t i = 0; i < a.layers.size() && i < b.layers.size(); ++i) {
        CHECK(a.layers[i].name == b.layers[i].name && a.layers[i].weight == b.layers[i].weight);
        CHECK(a.layers[i].delta == b.layers[i].delta);
    }
    CHECK(a.basePositions() == b.basePositions());
}

// Rewrites the manifest of a project with `edit` applied.
bool editManifest(const std::string& path, const std::function<void(Json&)>& edit) {
    std::vector<ZipEntry> entries;
    std::string err;
    if (!readStoredZip(path, entries, err)) return false;
    for (auto& e : entries) {
        if (e.name != "manifest.json") continue;
        Json j;
        if (!Json::parse(std::string(e.data.begin(), e.data.end()), j, err)) return false;
        edit(j);
        const std::string text = j.dump();
        e.data.assign(text.begin(), text.end());
    }
    return writeStoredZip(path, entries, 1, err);
}

size_t visibleTriangles(const Document& doc) {
    size_t n = 0;
    for (int i = 0; i < doc.partCount(); ++i)
        if (doc.parts[size_t(i)].visible) n += doc.partMesh(i).triangleCount();
    return n;
}

} // namespace

TEST(part_project_v2_round_trip) {
    Document doc = makeScene();
    const std::string path = tempPath("sf_part_project.sfproj");
    std::string err;
    CHECK(saveProject(path, doc, err));

    Document back;
    CHECK(loadProject(path, back, err));
    CHECK(back.partCount() == 3);
    CHECK(back.activePart == 1 && back.activePartInfo().name == "Eye");
    CHECK(back.parts[0].name == "Body" && back.parts[1].name == "Eye" && back.parts[2].name == "Sword");
    CHECK(back.parts[0].visible && !back.parts[1].visible && back.parts[2].visible);
    CHECK(back.bridgeName == "Eye_ZB");
    CHECK(!back.dirty() && back.path == path);
    for (int i = 0; i < 3; ++i) checkSameMesh(back.partMesh(i), doc.partMesh(i));
    CHECK(groupCount(back.partMesh(2)) == 2);
    // Every Part starts with an empty history; the active layer is per Part.
    for (int i = 0; i < 3; ++i) {
        back.selectPart(i);
        CHECK(!back.undo.canUndo() && !back.undo.canRedo());
        CHECK(back.activeLayer == (i == 0 ? 0 : -1));
    }
    CHECK(!back.dirty());
    // The BVH of a Part selected after loading works for picking.
    back.selectPart(2);
    RayHit hit;
    const glm::vec3 c = back.mesh.bounds().center();
    CHECK(back.bvh.raycast(back.mesh, c + glm::vec3(0, 0, 10), glm::vec3(0, 0, -1), hit));

    // The manifest lists the Parts and the active one.
    std::vector<ZipEntry> entries;
    CHECK(readStoredZip(path, entries, err));
    Json manifest;
    CHECK(!entries.empty() && Json::parse(std::string(entries[0].data.begin(), entries[0].data.end()), manifest, err));
    CHECK(manifest["version"].asNumber() == kProjectBaseWriteVersion && manifest["parts"].size() == 3);
    CHECK(manifest["active_part"].asNumber() == 1 && !manifest.has("mesh"));
    CHECK(manifest["parts"][1]["bridge_name"].asString() == "Eye_ZB");

    // open_project through the command reports the Parts.
    CommandRegistry reg;
    Document third;
    CommandResult r = reg.execute(third, "open_project", {{"path", path}});
    CHECK(r.ok && r.message.find("3 Parts") != std::string::npos);
    std::filesystem::remove(path);
}

TEST(part_project_folders) {
    // #96: folders and which Parts are in them come back; no format change (older versions ignore the keys).
    Document doc = makeScene();
    const uint32_t gear = doc.addPartFolder("Gear");
    doc.addPartFolder("Empty");
    doc.partFolders[0].collapsed = true;
    doc.setPartFolder(2, gear); // Sword
    doc.setPartFolder(1, gear); // Eye (active): moves below the Sword
    CHECK(doc.parts[1].name == "Sword" && doc.parts[2].name == "Eye" && doc.activePart == 2);
    const std::string path = tempPath("sf_part_folders.sfproj");
    std::string err;
    CHECK(saveProject(path, doc, err));

    Document back;
    CHECK(loadProject(path, back, err));
    CHECK(back.partFolders.size() == 2 && back.partFolders[0].name == "Gear" && back.partFolders[0].collapsed);
    CHECK(back.partFolders[1].name == "Empty" && !back.partFolders[1].collapsed);
    CHECK(back.parts[0].folder == 0 && back.parts[1].folder == back.partFolders[0].id &&
          back.parts[2].folder == back.partFolders[0].id);
    CHECK(back.activePart == 2 && !back.dirty());

    std::vector<ZipEntry> entries;
    CHECK(readStoredZip(path, entries, err));
    Json manifest;
    CHECK(!entries.empty() && Json::parse(std::string(entries[0].data.begin(), entries[0].data.end()), manifest, err));
    CHECK(manifest["version"].asNumber() == kProjectBaseWriteVersion); // what a scene without folders writes
    CHECK(manifest["part_folders"].size() == 2 && manifest["parts"][1]["folder"].asNumber() == 0);
    CHECK(!manifest["parts"][0].has("folder"));

    // Saved again (by this version or one that kept the key but not the Parts' folder= index): nothing doubles,
    // and a Part pointing at a folder that isn't there is outside any folder.
    CHECK(saveProject(path, back, err));
    CHECK(editManifest(path, [](Json& j) {
        Json list = Json::array();
        for (size_t i = 0; i < j["parts"].size(); ++i) {
            Json st = j["parts"][i];
            if (i == 1) st.set("folder", 7.0);
            list.push(std::move(st));
        }
        j.set("parts", std::move(list));
    }));
    Document again;
    CHECK(loadProject(path, again, err));
    CHECK(again.partFolders.size() == 2 && again.parts[1].folder == 0 && again.parts[2].folder == again.partFolders[0].id);
    CHECK(!again.projectSettings.has("part_folders"));

    // A project without folders (every earlier version's) opens with none.
    Document plain = makeScene();
    CHECK(saveProject(path, plain, err));
    CHECK(loadProject(path, again, err) && again.partFolders.empty());
    for (const Part& s : again.parts) CHECK(s.folder == 0);
    std::filesystem::remove(path);
}

TEST(part_project_version1_still_loads) {
    // Write a single-Part project, then turn its manifest into the version 1 layout.
    Document doc = makeDoc(makeIcosphere(2));
    doc.mesh.ensureColors(glm::vec3(0.5f));
    Mesh m = doc.mesh;
    m.addLayer("Smile");
    m.layers[0].delta[3] = glm::vec3(0, 0.2f, 0);
    m.setLayerWeight(0, 0.75f);
    doc.setMesh(std::move(m));
    const std::string path = tempPath("sf_part_v1.sfproj");
    std::string err;
    CHECK(saveProject(path, doc, err));
    CHECK(editManifest(path, [](Json& j) {
        const Json st = j["parts"][0];
        j.set("version", 1);
        j.set("mesh", st["mesh"]);
        j.set("layers", st["layers"]);
        j.set("active_layer", 0);
        j.set("parts", Json());
        j.set("active_part", Json());
    }));
    Document back;
    CHECK(loadProject(path, back, err));
    CHECK(back.partCount() == 1 && back.activePartInfo().name == "Sculpt");
    checkSameMesh(back.mesh, doc.mesh);
    CHECK(back.activeLayer == 0 && !back.dirty());

    // A version 2 file without Parts, or with a broken one, is refused and leaves the scene alone.
    Document keep = makeDoc(makePlane(4));
    const size_t tris = keep.mesh.triangleCount();
    CHECK(saveProject(path, doc, err));
    CHECK(editManifest(path, [](Json& j) { j.set("parts", Json::array()); }));
    err.clear();
    CHECK(!loadProject(path, keep, err) && !err.empty());
    CHECK(saveProject(path, makeScene(), err));
    CHECK(editManifest(path, [](Json& j) {
        Json list = Json::array();
        for (size_t i = 0; i < j["parts"].size(); ++i) {
            Json st = j["parts"][i];
            if (i == 2) {
                Json mesh = st["mesh"];
                mesh.set("triangle_count", mesh["triangle_count"].asNumber() + 5);
                st.set("mesh", std::move(mesh));
            }
            list.push(std::move(st));
        }
        j.set("parts", std::move(list));
        j.set("active_part", 99.0); // out of range would fall back to 0, but the broken mesh fails first
    }));
    err.clear();
    CHECK(!loadProject(path, keep, err) && !err.empty());
    CHECK(keep.partCount() == 1 && keep.mesh.triangleCount() == tris);
    std::filesystem::remove(path);
}

TEST(part_obj_export_import) {
    Document doc = makeExportScene();
    CommandRegistry reg;
    const std::string path = tempPath("sf_part_scene.obj");
    CommandResult r = reg.execute(doc, "save_obj", {{"path", path}});
    CHECK(r.ok && r.message.find("2 Parts") != std::string::npos);
    const std::string text = readText(path);
    CHECK(countOf(text, "\no ") == 2);
    CHECK(text.find("\no Body\n") != std::string::npos && text.find("\no Sword\n") != std::string::npos);
    CHECK(text.find("Eye") == std::string::npos); // hidden Parts are skipped
    CHECK(countOf(text, "\ng ") >= 4);            // face groups as `g` groups (two per mesh at least)

    std::vector<MeshObject> objects;
    std::string err;
    CHECK(loadObj(path, objects, err));
    CHECK(objects.size() == 2 && objects[0].name == "Body" && objects[1].name == "Sword");
    if (objects.size() == 2) {
        CHECK(objects[0].mesh.vertexCount() == doc.partMesh(0).vertexCount());
        for (size_t v = 0; v < objects[0].mesh.vertexCount() && v < doc.partMesh(0).vertexCount(); ++v)
            if (!near(objects[0].mesh.positions[v], doc.partMesh(0).positions[v], 1e-5f)) {
                CHECK(false);
                break;
            }
        CHECK(objects[0].mesh.triangles == doc.partMesh(0).triangles);
        CHECK(objects[1].mesh.vertexCount() == doc.partMesh(2).vertexCount());
        CHECK(groupCount(objects[0].mesh) == 2 && groupCount(objects[1].mesh) == 2);
        CHECK(objects[0].mesh.hasColors() && !objects[1].mesh.hasColors());
    }

    // open: one Part per object; the single-mesh loader still merges everything.
    Document back;
    r = reg.execute(back, "open", {{"path", path}});
    CHECK(r.ok && back.partCount() == 2 && back.parts[0].name == "Body" && back.parts[1].name == "Sword");
    CHECK(!back.dirty() && back.path == path);
    Mesh merged;
    CHECK(loadObj(path, merged, err));
    CHECK(merged.triangleCount() == visibleTriangles(doc));

    // part_append adds each object as its own Part.
    Document target = makeDoc(makeIcosphere(1));
    r = reg.execute(target, "part_append", {{"path", path}});
    CHECK(r.ok && target.partCount() == 3 && target.activePart == 1);
    CHECK(target.parts[1].name == "Body" && target.parts[2].name == "Sword");

    // A file whose `o` lines name objects without faces, and faces before any `o`.
    writeText(path, "v 0 0 0\nv 1 0 0\nv 0 1 0\nv 0 0 1\nf 1 2 3\no Empty\no Tip\nf 1 2 4\nf -1 -2 -3\no Lines\nl 1 2\n");
    CHECK(loadObj(path, objects, err));
    CHECK(objects.size() == 2 && objects[0].name.empty() && objects[1].name == "Tip");
    if (objects.size() == 2) CHECK(objects[0].mesh.vertexCount() == 3 && objects[1].mesh.triangleCount() == 2);
    std::filesystem::remove(path);
}

TEST(part_obj_single_object_unchanged) {
    Document doc = makeDoc(makeIcosphere(2));
    CommandRegistry reg;
    const std::string path = tempPath("sf_part_single.obj");
    CHECK(reg.execute(doc, "save_obj", {{"path", path}}).ok);
    const std::string text = readText(path);
    CHECK(text.find("\no ") == std::string::npos && text.find("\ng ") == std::string::npos);
    Document back;
    CHECK(reg.execute(back, "open", {{"path", path}}).ok);
    CHECK(back.partCount() == 1 && back.activePartInfo().name == "sf_part_single");
    CHECK(back.mesh.vertexCount() == doc.mesh.vertexCount() && !back.mesh.hasFaceGroups());

    // One `o` object: every vertex kept (also unused ones), named after the file.
    writeText(path, "o Thing\nv 0 0 0\nv 1 0 0\nv 0 1 0\nv 5 5 5\nf 1 2 3\n");
    CHECK(reg.execute(back, "open", {{"path", path}}).ok);
    CHECK(back.partCount() == 1 && back.mesh.vertexCount() == 4 && back.activePartInfo().name == "sf_part_single");
    // `g` groups become face groups; a single group does not.
    writeText(path, "v 0 0 0\nv 1 0 0\nv 0 1 0\nv 0 0 1\ng default\ng Face\nf 1 2 3\ng Back\nf 1 2 4\ng Face\nf 2 3 4\n");
    Mesh m;
    std::string err;
    CHECK(loadObj(path, m, err) && m.hasFaceGroups() && groupCount(m) == 2);
    CHECK(m.facegroups.size() == 3 && m.facegroups[0] == m.facegroups[2] && m.facegroups[0] != m.facegroups[1]);
    writeText(path, "v 0 0 0\nv 1 0 0\nv 0 1 0\ng default\ng Only\nf 1 2 3\n");
    CHECK(loadObj(path, m, err) && !m.hasFaceGroups());
    std::filesystem::remove(path);
}

TEST(part_glb_export_import) {
    Document doc = makeExportScene();
    CommandRegistry reg;
    const std::string path = tempPath("sf_part_scene.glb");
    CHECK(reg.execute(doc, "export_glb", {{"path", path}}).ok);
    std::vector<MeshObject> objects;
    std::string err;
    CHECK(loadGltf(path, objects, err));
    CHECK(objects.size() == 2);
    if (objects.size() == 2) {
        CHECK(objects[0].name == "Body" && objects[1].name == "Sword");
        const Mesh& body = objects[0].mesh;
        CHECK(body.layers.size() == 2 && body.layers[0].name == "Smile" && body.layers[1].name == "Blink");
        CHECK(body.layers.size() == 2 && std::abs(body.layers[0].weight - 0.5f) < 1e-6f);
        CHECK(objects[1].mesh.layers.empty()); // each mesh has its own morph targets
        CHECK(body.triangleCount() == doc.partMesh(0).triangleCount());
        CHECK(near(body.bounds().min, doc.partMesh(0).bounds().min) && near(objects[1].mesh.bounds().max, doc.partMesh(2).bounds().max));
        CHECK(body.hasColors());
    }
    Mesh merged;
    CHECK(loadGltf(path, merged, err) && merged.triangleCount() == visibleTriangles(doc));

    Document back;
    CHECK(reg.execute(back, "open", {{"path", path}}).ok);
    CHECK(back.partCount() == 2 && back.parts[1].name == "Sword" && back.mesh.layers.size() == 2);

    // Node transforms still apply per object.
    const std::string gltf = tempPath("sf_part_nodes.gltf");
    writeText(gltf,
              R"({"asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":[0,1]}],)"
              R"("nodes":[{"name":"Left","mesh":0,"translation":[-5,0,0]},{"mesh":0,"translation":[5,0,0]}],)"
              R"("meshes":[{"name":"Tri","primitives":[{"attributes":{"POSITION":0}}]}],)"
              R"("accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3"}],)"
              R"("bufferViews":[{"buffer":0,"byteLength":36}],)"
              R"("buffers":[{"byteLength":36,"uri":"data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAA"}]})");
    CHECK(loadGltf(gltf, objects, err));
    CHECK(objects.size() == 2 && objects[0].name == "Left" && objects[1].name == "Tri");
    if (objects.size() == 2) CHECK(objects[0].mesh.bounds().max.x < -3.0f && objects[1].mesh.bounds().min.x > 3.0f);
    std::filesystem::remove(path);
    std::filesystem::remove(gltf);
}

TEST(part_glb_single_object_unchanged) {
    Document doc = makeDoc(makeIcosphere(2));
    doc.path = tempPath("Head.obj");
    CommandRegistry reg;
    const std::string path = tempPath("sf_part_single.glb");
    CHECK(reg.execute(doc, "export_glb", {{"path", path}}).ok);
    std::vector<MeshObject> objects;
    std::string err;
    CHECK(loadGltf(path, objects, err) && objects.size() == 1 && objects[0].name == "Head");
    Document back;
    CHECK(reg.execute(back, "open", {{"path", path}}).ok);
    CHECK(back.partCount() == 1 && back.activePartInfo().name == "sf_part_single");
    std::filesystem::remove(path);
}

TEST(part_fbx_export_import) {
    Document doc = makeExportScene();
    CommandRegistry reg;
    const std::string path = tempPath("sf_part_scene.fbx");
    CHECK(reg.execute(doc, "export_fbx", {{"path", path}}).ok);
    std::vector<MeshObject> objects;
    std::string err;
    CHECK(loadFbx(path, objects, err));
    CHECK(objects.size() == 2);
    if (objects.size() == 2) {
        CHECK(objects[0].name == "Body" && objects[1].name == "Sword");
        CHECK(objects[0].mesh.layers.size() == 2 && objects[1].mesh.layers.empty());
        CHECK(objects[0].mesh.triangleCount() == doc.partMesh(0).triangleCount());
        CHECK(near(objects[1].mesh.bounds().center(), doc.partMesh(2).bounds().center()));
    }

    // LODs: a LOD group per Part; import takes LOD0 of each, named after its group.
    CHECK(reg.execute(doc, "export_fbx", {{"path", path}, {"lods", 3.0}}).ok);
    CHECK(loadFbx(path, objects, err));
    CHECK(objects.size() == 2);
    if (objects.size() == 2) {
        CHECK(objects[0].name == "Body" && objects[1].name == "Sword");
        CHECK(objects[0].mesh.triangleCount() == doc.partMesh(0).triangleCount());
        CHECK(objects[1].mesh.triangleCount() == doc.partMesh(2).triangleCount());
    }
    Document back;
    CHECK(reg.execute(back, "open", {{"path", path}}).ok);
    CHECK(back.partCount() == 2 && back.parts[0].name == "Body");

    // Unreal preset: one model per visible Part; layers on any of them make it skeletal (LOD0 only).
    UnrealExportOptions uo;
    uo.folder = std::filesystem::temp_directory_path().string();
    UnrealExportResult ur;
    CHECK(exportForUnreal(doc, uo, ur, err));
    CHECK(ur.skeletal && ur.lodCount == 1);
    CHECK(loadFbx(ur.path, objects, err) && objects.size() == 2);
    std::filesystem::remove(ur.path);
    doc.parts[0].visible = false; // the layered body (active, but hidden from export)
    doc.parts[1].visible = true;
    CHECK(exportForUnreal(doc, uo, ur, err));
    CHECK(!ur.skeletal && ur.lodCount == 4);
    CHECK(loadFbx(ur.path, objects, err) && objects.size() == 2);
    if (objects.size() == 2) CHECK(objects[0].name == "Eye" && objects[1].name == "Sword");
    std::filesystem::remove(ur.path);
    std::filesystem::remove(path);
}

TEST(part_usd_export_import) {
    Document doc = makeExportScene();
    doc.path = tempPath("Hero.sfproj");
    CommandRegistry reg;
    const std::string path = tempPath("sf_part_scene.usda");
    CHECK(reg.execute(doc, "export_usd", {{"path", path}}).ok);
    const std::string text = readText(path);
    // One Mesh prim per visible Part under the root; the layered body sits in its own SkelRoot.
    CHECK(text.find("defaultPrim = \"Hero\"") != std::string::npos);
    CHECK(text.find("def SkelRoot \"Body\"") != std::string::npos && text.find("def Mesh \"Sword\"") != std::string::npos);
    CHECK(countOf(text, "def Mesh ") == 2 && text.find("Eye") == std::string::npos);
    CHECK(text.find("</Hero/Body/Skel>") != std::string::npos && text.find("</Hero/Body/Skel/Weights>") != std::string::npos);
    // Vertex paint on the body only: it binds the painted material, the sword the plain clay.
    CHECK(text.find("rel material:binding = </Hero/Materials/PaintedClay>") != std::string::npos);
    CHECK(text.find("rel material:binding = </Hero/Materials/Clay>") != std::string::npos);
    CHECK(countOf(text, "{") == countOf(text, "}") && countOf(text, "[") == countOf(text, "]"));

    std::vector<MeshObject> objects;
    SplatCloud splats;
    std::string err;
    CHECK(loadUsd(path, objects, splats, err));
    CHECK(objects.size() == 2);
    if (objects.size() == 2) {
        CHECK(objects[0].name == "Body" && objects[1].name == "Sword");
        CHECK(objects[0].mesh.layers.size() == 2 && objects[1].mesh.layers.empty());
        if (objects[0].mesh.layers.size() == 2) CHECK(std::abs(objects[0].mesh.layers[0].weight - 0.5f) < 1e-6f);
        CHECK(objects[0].mesh.triangleCount() == doc.partMesh(0).triangleCount());
    }
    Document back;
    CHECK(reg.execute(back, "open", {{"path", path}}).ok);
    CHECK(back.partCount() == 2 && back.parts[1].name == "Sword");

    // USDZ too, and names that aren't USD identifiers stay valid and unique.
    doc.parts[0].name = "Body 1";
    doc.parts[2].name = "Body+1";
    const std::string usdz = tempPath("sf_part_scene.usdz");
    CHECK(reg.execute(doc, "export_usd", {{"path", usdz}}).ok);
    CHECK(loadUsd(usdz, objects, splats, err) && objects.size() == 2);
    if (objects.size() == 2) CHECK(objects[0].name == "Body_1" && objects[1].name == "Body_1_2");

    // A single Part is still the prim "Sculpt".
    Document single = makeDoc(makeIcosphere(1));
    CHECK(reg.execute(single, "export_usd", {{"path", path}}).ok);
    CHECK(readText(path).find("def Mesh \"Sculpt\"") != std::string::npos);
    std::filesystem::remove(path);
    std::filesystem::remove(usdz);
}

TEST(part_bridge_send_receive_and_export) {
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / "sf_part_bridge";
    fs::remove_all(root);
    Document doc = makeExportScene();
    CommandRegistry reg;
    CommandResult r = reg.execute(doc, "bridge_send", {{"folder", root.string()}, {"launch", false}});
    CHECK(r.ok && r.message.find("1 more Parts") != std::string::npos);
    CHECK(doc.bridgeName == "Body" && doc.parts[2].bridgeName == "Sword" && doc.parts[1].bridgeName == "Eye_ZB");
    const fs::path project = root / bridgefile::kProjectsFolder / "Default";
    CHECK(fs::exists(project / (std::string("Body") + bridgefile::kExt)) && fs::exists(project / (std::string("Sword") + bridgefile::kExt)) && !fs::exists(project / (std::string("Eye_ZB") + bridgefile::kExt)));

    // Receiving into a new scene gives the Parts back with face groups, mask and vertex paint.
    Document back;
    r = reg.execute(back, "bridge_receive", {{"folder", root.string()}});
    CHECK(r.ok && back.partCount() == 2);
    CHECK(back.parts[0].name == "Body" && back.parts[1].name == "Sword");
    CHECK(back.bridgeName == "Body" && back.parts[1].bridgeName == "Sword");
    CHECK(back.mesh.triangleCount() == doc.partMesh(0).triangleCount() && back.mesh.hasColors() && back.mesh.hasMask());
    CHECK(groupCount(back.partMesh(1)) == 2);

    // The merged form for callers that want one mesh.
    Mesh merged;
    std::string msg;
    CHECK(bridgeReceive(root.string(), merged, msg) && msg.find("(merged)") != std::string::npos);
    CHECK(merged.triangleCount() == visibleTriangles(doc));

    // bridge_export: one file per visible Part beside the path.
    const std::string out = (root / (std::string("Scene") + bridgefile::kExt)).string();
    CHECK(reg.execute(doc, "bridge_export", {{"path", out}}).ok);
    Mesh body, sword;
    std::string err;
    BridgeInfo info;
    CHECK(loadBridge((root / (std::string("Scene_Body") + bridgefile::kExt)).string(), body, err, &info) && info.name == "Body");
    CHECK(loadBridge((root / (std::string("Scene_Sword") + bridgefile::kExt)).string(), sword, err) && sword.triangleCount() == doc.partMesh(2).triangleCount());
    fs::remove_all(root);
}

TEST(part_merged_exports) {
    Document doc = makeExportScene();
    CommandRegistry reg;
    const size_t tris = visibleTriangles(doc);
    std::string err;
    const std::string stl = tempPath("sf_part_scene.stl");
    CHECK(reg.execute(doc, "export_stl", {{"path", stl}}).ok);
    Mesh m;
    CHECK(loadStl(stl, m, err) && m.triangleCount() == tris);
    const std::string ply = tempPath("sf_part_scene.ply");
    CHECK(reg.execute(doc, "export_ply", {{"path", ply}}).ok);
    SplatCloud splats;
    PlyContent content;
    CHECK(loadPly(ply, m, splats, content, err) && content == PlyContent::Mesh && m.triangleCount() == tris);
    // Splats cover every visible Part (the sword lies at x = 2).
    CHECK(reg.execute(doc, "mesh_to_splats", {{"count", 4000.0}}).ok);
    CHECK(doc.splats.bounds(1.0f).max.x > 2.0f);
    // Nothing visible: exporters say so.
    for (auto& s : doc.parts) s.visible = false;
    CHECK(!reg.execute(doc, "export_glb", {{"path", tempPath("sf_part_none.glb")}}).ok);
    CHECK(!reg.execute(doc, "save_obj", {{"path", tempPath("sf_part_none.obj")}}).ok);
    CHECK(!reg.execute(doc, "export_ply", {{"path", ply}}).ok);
    std::filesystem::remove(stl);
    std::filesystem::remove(ply);
}

TEST(part_export_lods_per_part) {
    Document doc = makeExportScene();
    CommandRegistry reg;
    const std::string base = tempPath("sf_part_rock.glb");
    CHECK(reg.execute(doc, "export_lods", {{"path", base}, {"levels", 2.0}}).ok);
    std::vector<MeshObject> objects;
    std::string err;
    const std::string lod1 = tempPath("sf_part_rock_LOD1.glb");
    CHECK(loadGltf(lod1, objects, err) && objects.size() == 2);
    if (objects.size() == 2) CHECK(objects[0].name == "Body" && objects[0].mesh.triangleCount() < doc.partMesh(0).triangleCount());
    std::filesystem::remove(tempPath("sf_part_rock_LOD0.glb"));
    std::filesystem::remove(lod1);
}

TEST(part_loaders_survive_malformed_objects) {
    std::string err;
    std::vector<MeshObject> objects;
    SplatCloud splats;
    const std::string obj = tempPath("sf_part_bad.obj");
    // Bad indices in a later object, empty names, nothing but `o` lines.
    for (const char* text : {"o A\nv 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\no B\nf 1 2 9\n", "o\no\no\n", "o A\r\nv 0 0 0\r\nf 1 1 1\r\n",
                             "v 0 0 0\nv 1 0 0\nv 0 1 0\no X\nf -1 -2 -3\no Y\nf -4 1 2\n", "g\ng\nv 1 1 1\nf 1 1 1 1 1\n"}) {
        writeText(obj, text);
        loadObj(obj, objects, err); // either outcome; no crash
        Mesh m;
        loadObj(obj, m, err);
    }
    std::filesystem::remove(obj);

    const std::string usda = tempPath("sf_part_bad.usda");
    for (const char* text :
         {"#usda 1.0\ndef Mesh \"A\" { point3f[] points = [(0,0,0),(1,0,0),(0,1,0)] int[] faceVertexCounts = [3] "
          "int[] faceVertexIndices = [0, -1, 2] }\n",
          "#usda 1.0\ndef Mesh \"A\" { point3f[] points = [(0,0,0),(1,0,0),(0,1,0)] int[] faceVertexCounts = [3] "
          "int[] faceVertexIndices = [0, 1, 2] uniform token[] skel:blendShapes = [\"S\"] rel skel:blendShapeTargets = [<S>] "
          "rel skel:skeleton = </Nowhere> def BlendShape \"S\" { uniform vector3f[] offsets = [(1,1,1)] "
          "uniform int[] pointIndices = [-7] } }\n",
          "#usda 1.0\ndef SkelRoot \"R\" { def Mesh \"M\" { point3f[] points = [(0,0,0),(1,0,0),(0,1,0)] "
          "int[] faceVertexCounts = [3] int[] faceVertexIndices = [0, 1, 2] rel skel:skeleton = </R/K> } "
          "def Skeleton \"K\" { rel skel:animationSource = </R> } }\n"}) {
        writeText(usda, text);
        loadUsd(usda, objects, splats, err);
        Mesh m;
        loadUsd(usda, m, splats, err);
    }
    // The second file loads: the stray point index is ignored.
    writeText(usda, "#usda 1.0\ndef SkelRoot \"Rig\" { def Mesh \"Mesh\" { point3f[] points = [(0,0,0),(1,0,0),(0,1,0)] "
                    "int[] faceVertexCounts = [3] int[] faceVertexIndices = [0, 1, 2] } }\n");
    CHECK(loadUsd(usda, objects, splats, err) && objects.size() == 1 && objects[0].name == "Rig");
    std::filesystem::remove(usda);

    const std::string gltf = tempPath("sf_part_bad.gltf");
    writeText(gltf, R"({"asset":{"version":"2.0"},"scenes":[{"nodes":[0,1,7]}],"nodes":[{"mesh":5},{"mesh":0}],"meshes":[{"primitives":[{"attributes":{"POSITION":3}}]}]})");
    loadGltf(gltf, objects, err);
    std::filesystem::remove(gltf);
    CHECK(true);
}
