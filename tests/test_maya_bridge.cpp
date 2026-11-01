// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The Maya bridge (io/MayaBridge.h, #42): strict manifest parsing (traversal, oversized and malformed manifests
// are refused), Maya-safe names, and a round trip maya_send -> maya_receive through a temporary exchange folder.
// tests/maya/test_sculptforge_maya.py does the shelf script's side with a mocked maya.cmds.
#include "TestHarness.h"

#include "core/Commands.h"
#include "core/Primitives.h"
#include "io/MayaBridge.h"
#include "io/ObjIO.h"

#include <filesystem>
#include <fstream>
#include <iterator>

using namespace sf;

namespace {

namespace fs = std::filesystem;

void writeText(const fs::path& p, const std::string& text) {
    fs::create_directories(p.parent_path());
    std::ofstream f(p, std::ios::binary);
    f << text;
}

std::string manifestWith(const std::string& objects) {
    return "{\"format\": \"sculptforge-maya\", \"version\": 1, \"revision\": 2, \"sender\": \"maya\", \"objects\": [" +
           objects + "]}";
}

} // namespace

TEST(maya_names_and_files) {
    CHECK(mayaSafeName("pCube1") == "pCube1");
    CHECK(mayaSafeName("Body Hi") == "Body_Hi");
    CHECK(mayaSafeName("3D head") == "_3D_head");
    CHECK(mayaSafeName("") == "Part");
    CHECK(mayaSafeName("a|b:c/../d") == "a_b_c____d");
    CHECK(mayaSafeName(std::string(1000, 'x')).size() == kMayaMaxNameBytes);
    CHECK(mayaSafeName("9" + std::string(1000, 'x')).size() == kMayaMaxNameBytes);

    CHECK(mayaFileNameOk("r1_000.obj") && mayaFileNameOk("pCube1.OBJ") && mayaFileNameOk("a-b.c.obj"));
    for (const char* bad : {"", ".obj", "../x.obj", "..obj", "a..b.obj", "/etc/x.obj", "C:\\x.obj", "c:x.obj",
                            "a/b.obj", "a\\b.obj", "x.mel", "x.py", "x.obj.py", "x obj.obj", ".hidden.obj", "x\n.obj"})
        CHECK(!mayaFileNameOk(bad));
    CHECK(!mayaFileNameOk(std::string(70, 'a') + ".obj"));
}

TEST(maya_manifest_parse_and_write) {
    MayaManifest m;
    std::string err;
    CHECK(parseMayaManifest(manifestWith(R"({"name": "pCube1", "file": "r2_000.obj"},
                                            {"name": "Body Hi ü", "file": "r2_001.obj"})"),
                            m, err));
    CHECK(m.revision == 2 && m.sender == "maya" && m.objects.size() == 2);
    CHECK(m.objects[0].name == "pCube1" && m.objects[0].file == "r2_000.obj");
    CHECK(m.objects[1].name == "Body_Hi___"); // sanitised to Maya's characters (ü is two bytes)

    m.unit = "cm";
    m.senderVersion = "1.2 \"x\"";
    MayaManifest back;
    CHECK(parseMayaManifest(writeMayaManifest(m), back, err));
    CHECK(back.revision == 2 && back.unit == "cm" && back.senderVersion == m.senderVersion);
    CHECK(back.objects.size() == 2 && back.objects[1].file == "r2_001.obj" && back.objects[1].name == "Body_Hi___");

    // Path traversal and other files: refused, whatever the spelling.
    for (const char* bad : {"../x.obj", "..\\\\x.obj", "/tmp/x.obj", "C:\\\\x.obj", "sub/x.obj", "x.mel", "..", "",
                            "~/x.obj", "%2e%2e/x.obj"}) {
        CHECK(!parseMayaManifest(manifestWith(std::string("{\"name\": \"a\", \"file\": \"") + bad + "\"}"), back, err));
        CHECK(err.find("file") != std::string::npos);
    }
    // Malformed: other JSON, wrong types, missing fields, newer versions, duplicate files.
    const std::vector<std::string> malformed = {
        "not json",
        "[]",
        "{\"format\": \"sculptforge-bridge\", \"version\": 1, \"revision\": 1, \"objects\": []}",
        "{\"format\": \"sculptforge-maya\", \"revision\": 1, \"objects\": []}",
        "{\"format\": \"sculptforge-maya\", \"version\": \"1\", \"revision\": 1, \"objects\": []}",
        "{\"format\": \"sculptforge-maya\", \"version\": 1, \"objects\": []}",
        "{\"format\": \"sculptforge-maya\", \"version\": 1, \"revision\": -1, \"objects\": []}",
        "{\"format\": \"sculptforge-maya\", \"version\": 1, \"revision\": 1.5, \"objects\": []}",
        "{\"format\": \"sculptforge-maya\", \"version\": 1, \"revision\": 1}",
        "{\"format\": \"sculptforge-maya\", \"version\": 1, \"revision\": 1, \"objects\": {}}",
        "{\"format\": \"sculptforge-maya\", \"version\": 1, \"revision\": 1, \"sender\": 5, \"objects\": []}",
        manifestWith("{\"name\": \"a\"}"),
        manifestWith("{\"file\": \"a.obj\"}"),
        manifestWith("{\"name\": 1, \"file\": \"a.obj\"}"),
        manifestWith("\"a.obj\""),
        manifestWith("{\"name\": \"a\", \"file\": \"a.obj\"}, {\"name\": \"b\", \"file\": \"a.obj\"}"),
        manifestWith("{\"name\": \"" + std::string(2000, 'n') + "\", \"file\": \"a.obj\"}"),
        std::string(300, '[') + std::string(300, ']'),
    };
    for (const std::string& text : malformed) CHECK(!parseMayaManifest(text, back, err));
    const std::string newer = R"({"format": "sculptforge-maya", "version": 2, "revision": 1, "objects": []})";
    CHECK(!parseMayaManifest(newer, back, err) && err.find("newer") != std::string::npos);

    // Oversized: too many objects, too many bytes.
    std::string many;
    for (size_t i = 0; i <= kMayaMaxObjects; ++i)
        many += (i ? ", " : "") + std::string("{\"name\": \"a\", \"file\": \"f") + std::to_string(i) + ".obj\"}";
    CHECK(!parseMayaManifest(manifestWith(many), back, err) && err.find("too many") != std::string::npos);
    const std::string huge = manifestWith("") + std::string(kMayaManifestMaxBytes, ' ');
    CHECK(!parseMayaManifest(huge, back, err) && err.find("too big") != std::string::npos);
    const fs::path dir = fs::temp_directory_path() / "sf_maya_manifest";
    fs::remove_all(dir);
    writeText(dir / "manifest.json", huge);
    CHECK(!readMayaManifest((dir / "manifest.json").string(), back, err) && err.find("too big") != std::string::npos);
    CHECK(mayaRevision(dir.string()) == -1);
    fs::remove_all(dir);
}

TEST(maya_bridge_round_trip) {
    const fs::path root = fs::temp_directory_path() / "sf_maya_bridge";
    fs::remove_all(root);
    fs::create_directories(root);
    const std::string folder = root.string();
    CommandRegistry reg;
    std::string err;

    // Off until a folder is given; a folder that isn't there is refused.
    Document doc = makeDoc(makeCube(3)); // quads
    doc.subtools[0].name = "Body Hi";
    doc.mesh.uvs.assign(doc.mesh.triangleCount() * 3, glm::vec2(0.25f, 0.75f));
    doc.mesh.ensurePolygroups();
    for (uint32_t t = 0; t < doc.mesh.triangleCount(); ++t) { // one group per cube side (quads stay whole)
        const auto& tri = doc.mesh.triangles[t];
        const glm::vec3 n = glm::abs(glm::cross(doc.mesh.positions[tri[1]] - doc.mesh.positions[tri[0]],
                                                doc.mesh.positions[tri[2]] - doc.mesh.positions[tri[0]]));
        doc.mesh.polygroups[t] = n.x > n.y && n.x > n.z ? 1 : n.y > n.z ? 2 : 3;
    }
    CHECK(!reg.execute(doc, "maya_send", {{"folder", std::string()}}).ok);
    CHECK(!reg.execute(doc, "maya_send", {{"folder", (root / "missing").string()}}).ok);
    CHECK(!fs::exists(root / "missing"));

    // Send: OBJ with quads and UVs (polygroups stay home), a Maya-safe name, the manifest.
    const size_t quads = doc.mesh.quadCount();
    const size_t triangles = doc.mesh.triangleCount();
    CHECK(quads > 0);
    CommandResult r = reg.execute(doc, "maya_send", {{"folder", folder}});
    CHECK(r.ok && r.message.find("Sent 1 object to Maya") != std::string::npos);
    MayaManifest sent;
    CHECK(readMayaManifest(mayaManifestPath(mayaOutbox(folder)), sent, err));
    CHECK(sent.revision == 1 && sent.sender == "sculptforge" && sent.objects.size() == 1);
    CHECK(sent.objects[0].name == "Body_Hi" && doc.gozName == "Body_Hi");
    Mesh obj;
    CHECK(loadObj((root / "sculptforge_to_maya" / sent.objects[0].file).string(), obj, err));
    CHECK(obj.triangleCount() == triangles && obj.quadCount() == quads && obj.hasUvs() && !obj.hasPolygroups());

    // "Maya" sends it back changed (here: the file SculptForge wrote, moved up), plus a new object.
    const fs::path inbox = root / "maya_to_sculptforge";
    fs::create_directories(inbox);
    for (glm::vec3& p : obj.positions) p.y += 1.0f;
    CHECK(saveObj((inbox / "r5_000.obj").string(), obj, err));
    CHECK(saveObj((inbox / "r5_001.obj").string(), makeIcosphere(1), err));
    writeText(inbox / "manifest.json", R"({"format": "sculptforge-maya", "version": 1, "revision": 5,
        "sender": "maya", "unit": "cm", "objects": [
          {"name": "Body_Hi", "file": "r5_000.obj"}, {"name": "pSphere1", "file": "r5_001.obj"}]})");
    CHECK(mayaRevision(inbox.string()) == 5 && mayaManifestStamp(inbox.string()) != 0);
    r = reg.execute(doc, "maya_receive", {{"folder", folder}});
    CHECK(r.ok && r.message.find("Received 2 objects from Maya") != std::string::npos);
    // Replaced by name (undoably), the new one added.
    CHECK(doc.subtoolCount() == 2 && doc.activeSubtool == 0 && doc.subtools[0].name == "Body Hi");
    CHECK(doc.mesh.quadCount() == quads && doc.mesh.hasUvs() && doc.undo.canUndo());
    CHECK(std::abs(doc.mesh.positions[0].y - obj.positions[0].y) < 1e-5f);
    CHECK(doc.subtools[1].name == "pSphere1" && doc.subtools[1].gozName == "pSphere1");

    // A second send: the next revision, the first send's files removed, others in the folder left alone.
    writeText(root / "sculptforge_to_maya" / "notes.txt", "mine");
    const std::string firstFile = sent.objects[0].file;
    CHECK(reg.execute(doc, "maya_send", {{"folder", folder}}).ok);
    CHECK(mayaRevision(mayaOutbox(folder)) == 2);
    CHECK(!fs::exists(root / "sculptforge_to_maya" / firstFile));
    CHECK(fs::exists(root / "sculptforge_to_maya" / "notes.txt"));
    CHECK(readMayaManifest(mayaManifestPath(mayaOutbox(folder)), sent, err) && sent.objects.size() == 2);
    CHECK(sent.objects[0].name == "Body_Hi" && sent.objects[1].name == "pSphere1");

#ifndef _WIN32
    {
        // A link planted where the next send writes is replaced, not written through.
        writeText(root / "victim.txt", "keep me");
        std::error_code ec;
        fs::create_symlink(root / "victim.txt", root / "sculptforge_to_maya" / "r3_000.obj", ec);
        fs::create_symlink(root / "victim.txt", root / "sculptforge_to_maya" / "manifest.json.tmp", ec);
        CHECK(reg.execute(doc, "maya_send", {{"folder", folder}}).ok);
        std::ifstream victim(root / "victim.txt");
        std::string text((std::istreambuf_iterator<char>(victim)), std::istreambuf_iterator<char>());
        CHECK(text == "keep me" && !fs::is_symlink(root / "sculptforge_to_maya" / "r3_000.obj"));
        CHECK(mayaRevision(mayaOutbox(folder)) == 3);
    }
#endif
    // A traversal attempt in the manifest: nothing outside the folder is read.
    writeText(root / "outside.obj", "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n");
    writeText(inbox / "manifest.json", R"({"format": "sculptforge-maya", "version": 1, "revision": 6,
        "objects": [{"name": "x", "file": "../outside.obj"}]})");
    const int before = doc.subtoolCount();
    r = reg.execute(doc, "maya_receive", {{"folder", folder}});
    CHECK(!r.ok && doc.subtoolCount() == before);
#ifndef _WIN32
    // A link inside the folder that points out of it is not a plain file: refused.
    std::error_code ec;
    fs::create_symlink(root / "outside.obj", inbox / "link.obj", ec);
    if (!ec) {
        writeText(inbox / "manifest.json", R"({"format": "sculptforge-maya", "version": 1, "revision": 7,
            "objects": [{"name": "x", "file": "link.obj"}]})");
        r = reg.execute(doc, "maya_receive", {{"folder", folder}});
        CHECK(!r.ok && r.message.find("plain file") != std::string::npos && doc.subtoolCount() == before);
    }
#endif
    // A missing file says which.
    writeText(inbox / "manifest.json", R"({"format": "sculptforge-maya", "version": 1, "revision": 8,
        "objects": [{"name": "Gone", "file": "gone.obj"}]})");
    r = reg.execute(doc, "maya_receive", {{"folder", folder}});
    CHECK(!r.ok && r.message.find("Gone") != std::string::npos);
    fs::remove_all(root);
    r = reg.execute(doc, "maya_receive", {{"folder", folder}});
    CHECK(!r.ok && r.message.find("Nothing has been sent from Maya") != std::string::npos);
}
