// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The Blender bridge (io/BlenderBridge.h): manifest parsing, axes, and a round trip through the exchange
// folder with blender_receive / blender_send. tests/blender/test_exchange.py does the same from the add-on's
// side (its Python writer and reader against sculptforge-cli).
#include "TestHarness.h"

#include "core/Commands.h"
#include "core/Primitives.h"
#include "io/BlenderBridge.h"
#include "io/GozIO.h"

#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>

using namespace sf;

namespace {

namespace fs = std::filesystem;

void writeText(const fs::path& p, const std::string& text) {
    fs::create_directories(p.parent_path());
    std::ofstream f(p, std::ios::binary);
    f << text;
}

float maxDistance(const std::vector<glm::vec3>& a, const std::vector<glm::vec3>& b) {
    if (a.size() != b.size()) return 1e30f;
    float d = 0.0f;
    for (size_t i = 0; i < a.size(); ++i) d = std::max(d, glm::length(a[i] - b[i]));
    return d;
}

// A Blender matrix_world: rotation, non-uniform scale, translation (Z-up).
glm::dmat4 blenderMatrix(double sx) {
    glm::dmat4 m = glm::translate(glm::dmat4(1.0), glm::dvec3(1.5, -2.0, 0.75));
    m = glm::rotate(m, glm::radians(33.0), glm::normalize(glm::dvec3(1.0, 2.0, 0.5)));
    return glm::scale(m, glm::dvec3(sx, 0.5, 1.25));
}

std::string matrixJson(const glm::dmat4& m) {
    std::string s = "[";
    for (int r = 0; r < 4; ++r) {
        s += r ? ", [" : "[";
        for (int c = 0; c < 4; ++c) {
            char v[40];
            std::snprintf(v, sizeof v, "%.17g", m[c][r]);
            s += (c ? ", " : "") + std::string(v);
        }
        s += "]";
    }
    return s + "]";
}

} // namespace

TEST(blender_manifest_parse_and_write) {
    BridgeManifest m;
    std::string err;
    const std::string text = R"({"format": "sculptforge-bridge", "version": 1, "revision": 7, "sender": "blender",
        "sender_version": "4.2.3", "objects": [
          {"name": "Suzanne.001 ü", "file": "r7_000.GoZ",
           "matrix_world": [[1, 0, 0, 5], [0, 0, -1, 6], [0, 1, 0, 7], [0, 0, 0, 1]]},
          {"name": "Plain", "file": "r7_001.GoZ"}]})";
    CHECK(parseBridgeManifest(text, m, err));
    CHECK(m.revision == 7 && m.sender == "blender" && m.senderVersion == "4.2.3" && m.objects.size() == 2);
    CHECK(m.objects[0].name == "Suzanne.001 ü" && m.objects[0].hasMatrix && !m.objects[1].hasMatrix);
    // Rows as Blender prints them: the translation is the last column.
    CHECK(m.objects[0].matrix[3][0] == 5.0 && m.objects[0].matrix[3][1] == 6.0 && m.objects[0].matrix[3][2] == 7.0);
    CHECK(m.objects[0].matrix[2][1] == -1.0 && m.objects[0].matrix[1][2] == 1.0);

    // Written and read back unchanged (full double precision).
    m.objects[0].matrix[0][0] = 0.1234567890123456789;
    BridgeManifest back;
    CHECK(parseBridgeManifest(writeBridgeManifest(m), back, err));
    CHECK(back.revision == 7 && back.objects.size() == 2 && back.objects[0].name == m.objects[0].name);
    CHECK(back.objects[0].matrix == m.objects[0].matrix && back.objects[1].file == "r7_001.GoZ");

    // Refused: other JSON, newer versions, files outside the folder, broken matrices.
    CHECK(!parseBridgeManifest("{\"format\": \"other\"}", back, err));
    CHECK(!parseBridgeManifest("not json", back, err));
    // (Raw strings stay out of CHECK: MSVC can't stringize them inside a macro argument.)
    const std::string newer = R"({"format": "sculptforge-bridge", "version": 2, "objects": []})";
    CHECK(!parseBridgeManifest(newer, back, err));
    CHECK(err.find("newer") != std::string::npos);
    const std::string badStart = R"({"format": "sculptforge-bridge", "objects": [{"name": "x", "file": ")";
    for (const char* bad : {"../x.GoZ", "a/b.GoZ", "c:\\\\x.GoZ", ".."}) {
        const std::string text = badStart + bad + "\"}]}";
        CHECK(!parseBridgeManifest(text, back, err));
    }
    const std::string badMatrix =
        R"({"format": "sculptforge-bridge", "objects": [{"name": "x", "file": "a.GoZ", "matrix_world": [[1, 0, 0]]}]})";
    CHECK(!parseBridgeManifest(badMatrix, back, err));
}

TEST(blender_axes) {
    // Blender Z-up -> SculptForge Y-up, metres both: up stays up, Blender's front (-Y) faces the SculptForge camera (+Z).
    CHECK(blenderToSculptForge({0, 0, 1}) == glm::dvec3(0, 1, 0));
    CHECK(blenderToSculptForge({0, -1, 0}) == glm::dvec3(0, 0, 1));
    CHECK(blenderToSculptForge({1, 0, 0}) == glm::dvec3(1, 0, 0));
    CHECK(sculptForgeToBlender(blenderToSculptForge({0.3, -0.7, 2.0})) == glm::dvec3(0.3, -0.7, 2.0));
    // Matrices convert so that converting then moving == moving then converting.
    const glm::dmat4 mb = blenderMatrix(2.0), ms = blenderMatrixToSculptForge(mb);
    const glm::dvec3 p(0.25, -1.5, 3.0);
    const glm::dvec3 a = blenderToSculptForge(glm::dvec3(mb * glm::dvec4(p, 1.0)));
    const glm::dvec3 b = glm::dvec3(ms * glm::dvec4(blenderToSculptForge(p), 1.0));
    CHECK(glm::length(a - b) < 1e-12);
    CHECK(std::abs(glm::determinant(ms) - glm::determinant(mb)) < 1e-12);
}

TEST(blender_bridge_round_trip) {
    const fs::path root = fs::temp_directory_path() / "sf_blender_bridge";
    fs::remove_all(root);
    const std::string folder = root.string();
    CommandRegistry reg;
    std::string err;

    // What the add-on writes: object-space meshes as .GoZ (here written by SculptForge's own writer, whose
    // file holds Blender local coordinates for a mesh given in SculptForge axes) and each matrix_world.
    Mesh cube = makeCube(3); // quads
    Mesh ball = makeIcosphere(2);
    ball.ensurePolygroups();
    for (uint32_t t = 0; t < ball.triangleCount(); ++t) ball.polygroups[t] = t % 3;
    ball.uvs.assign(ball.triangleCount() * 3, glm::vec2(0.25f, 0.75f));
    const fs::path inbox = root / "to_sculptforge";
    fs::create_directories(inbox);
    CHECK(saveGoz((inbox / "r3_000.GoZ").string(), cube, "Cube", err));
    CHECK(saveGoz((inbox / "r3_001.GoZ").string(), ball, "Ball", err));
    const glm::dmat4 mCube = blenderMatrix(1.0), mBall = blenderMatrix(-1.0); // the ball is mirrored
    writeText(inbox / "manifest.json",
              R"({"format": "sculptforge-bridge", "version": 1, "revision": 3, "sender": "blender", "objects": [
                 {"name": "Cube", "file": "r3_000.GoZ", "matrix_world": )" + matrixJson(mCube) + R"(},
                 {"name": "Ball ü", "file": "r3_001.GoZ", "matrix_world": )" + matrixJson(mBall) + "}]}");
    CHECK(bridgeRevision(inbox.string()) == 3);

    // Into a fresh scene: the cube replaces it (renamed after the object), the ball is added.
    Document doc = makeDoc(makeIcosphere(1));
    CommandResult r = reg.execute(doc, "blender_receive", {{"folder", folder}});
    CHECK(r.ok && r.message.find("Received 2 objects") != std::string::npos);
    CHECK(doc.subtoolCount() == 2 && doc.activeSubtool == 0);
    CHECK(doc.subtools[0].name == "Cube" && doc.gozName == "Cube");
    CHECK(doc.subtools[1].name == "Ball ü" && doc.subtools[1].gozName == "Ball ü");
    CHECK(doc.mesh.quadCount() == cube.quadCount() && doc.mesh.quadCount() > 0);
    CHECK(doc.subtoolMesh(1).polygroups.size() == ball.triangleCount() && doc.subtoolMesh(1).hasUvs());
    // Placed as in Blender's scene: p_sf = A * M_blender * A^-1 * p.
    const glm::dmat4 placed = blenderMatrixToSculptForge(mCube);
    std::vector<glm::vec3> expect;
    for (const glm::vec3& p : cube.positions) expect.push_back(glm::vec3(placed * glm::dvec4(glm::dvec3(p), 1.0)));
    CHECK(maxDistance(doc.mesh.positions, expect) < 1e-5f);

    // Sculpt, then send back: world space, under the exact Blender names, faces exactly as they came.
    doc.mesh.positions[0] += glm::vec3(0.0f, 0.1f, 0.0f);
    r = reg.execute(doc, "blender_send", {{"folder", folder}});
    CHECK(r.ok);
    BridgeManifest sent;
    CHECK(readBridgeManifest(bridgeManifestPath(blenderOutbox(folder)), sent, err));
    CHECK(sent.revision == 1 && sent.sender == "sculptforge" && sent.objects.size() == 2);
    CHECK(sent.objects[0].name == "Cube" && sent.objects[1].name == "Ball ü" && !sent.objects[0].hasMatrix);
    Mesh cubeBack, ballBack;
    CHECK(loadGoz((root / "to_blender" / sent.objects[0].file).string(), cubeBack, err));
    CHECK(loadGoz((root / "to_blender" / sent.objects[1].file).string(), ballBack, err));
    CHECK(maxDistance(cubeBack.positions, doc.mesh.positions) < 1e-6f);
    // (GoZ quads come in split as (a,b,c) + (a,c,d): compare with the SubTool as it was received.)
    CHECK(cubeBack.triangles == doc.mesh.triangles && cubeBack.quadCount() == cube.quadCount());
    CHECK(ballBack.triangles == ball.triangles && ballBack.polygroups == ball.polygroups && ballBack.uvs == ball.uvs);
    // Undoing the placement gives the object-space mesh back (what the add-on does with matrix_world).
    const glm::dmat4 unplace = glm::inverse(blenderMatrixToSculptForge(mBall));
    std::vector<glm::vec3> local;
    for (const glm::vec3& p : ballBack.positions) local.push_back(glm::vec3(unplace * glm::dvec4(glm::dvec3(p), 1.0)));
    CHECK(maxDistance(local, ball.positions) < 1e-5f);

    // Blender sends the cube again: it updates its SubTool (undoably) instead of adding one.
    writeText(inbox / "manifest.json", R"({"format": "sculptforge-bridge", "revision": 4, "objects": [
                 {"name": "Cube", "file": "r3_000.GoZ"}]})");
    r = reg.execute(doc, "blender_receive", {{"folder", folder}});
    CHECK(r.ok && doc.subtoolCount() == 2 && doc.activeSubtool == 0);
    CHECK(maxDistance(doc.mesh.positions, cube.positions) < 1e-6f); // no matrix: as stored
    CHECK(doc.undo.canUndo());

    // A second send: the next revision, the first send's files removed.
    const std::string firstFile = sent.objects[0].file;
    CHECK(reg.execute(doc, "blender_send", {{"folder", folder}}).ok);
    CHECK(bridgeRevision(blenderOutbox(folder)) == 2);
    CHECK(!fs::exists(root / "to_blender" / firstFile));

    // Errors say what to do.
    fs::remove_all(root);
    r = reg.execute(doc, "blender_receive", {{"folder", folder}});
    CHECK(!r.ok && r.message.find("Nothing has been sent from Blender") != std::string::npos);
    writeText(inbox / "manifest.json", R"({"format": "sculptforge-bridge", "objects": [{"name": "Gone", "file": "r1_000.GoZ"}]})");
    r = reg.execute(doc, "blender_receive", {{"folder", folder}});
    CHECK(!r.ok && r.message.find("Gone") != std::string::npos);
    fs::remove_all(root);
}

TEST(blender_bridge_names_and_app_info) {
    const fs::path root = fs::temp_directory_path() / "sf_blender_names";
    fs::remove_all(root);
    const std::string folder = root.string();
    // Duplicate names get Blender's .001 suffixes.
    Mesh a = makeIcosphere(0), b = makeIcosphere(0);
    std::vector<std::string> names;
    std::string msg;
    CHECK(blenderSend({{"Rock", &a}, {"Rock", &b}}, folder, msg, &names));
    CHECK(names.size() == 2 && names[0] == "Rock" && names[1] == "Rock.001");
    CHECK(!blenderSend({}, folder, msg) && msg.find("Nothing") != std::string::npos);

    writeBridgeAppInfo(folder, "/opt/SculptForge/bin/sculptforge", "1.2.3");
    std::string text;
    { // closed before the folder is removed (Windows can't delete open files)
        std::ifstream f(root / "app.json");
        text.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    }
    CHECK(text.find("\"/opt/SculptForge/bin/sculptforge\"") != std::string::npos && text.find("1.2.3") != std::string::npos);
    writeBridgeHeartbeat(folder);
    CHECK(fs::exists(root / "alive.json"));
    writeBridgeHeartbeat(folder, true);
    CHECK(!fs::exists(root / "alive.json"));
    fs::remove_all(root);
}

TEST(blender_bridge_flatpak_launcher) {
    // A per-user install: the add-on starts ~/.local/share/flatpak/exports/bin/<id>.
    const std::string user = "[Application]\nname=org.sculptforge.SculptForge\nruntime=runtime/org.freedesktop.Platform/x86_64/26.08\n\n"
                             "[Instance]\ninstance-id=12345\n"
                             "app-path=/home/ana/.local/share/flatpak/app/org.sculptforge.SculptForge/x86_64/master/abc123/files\n"
                             "branch=master\n";
    CHECK(flatpakHostLauncher(user) == "/home/ana/.local/share/flatpak/exports/bin/org.sculptforge.SculptForge");
    // System-wide, with Windows line ends for good measure.
    const std::string system = "[Application]\r\nname=org.sculptforge.SculptForge\r\n[Instance]\r\n"
                               "app-path=/var/lib/flatpak/app/org.sculptforge.SculptForge/aarch64/stable/def/files\r\n";
    CHECK(flatpakHostLauncher(system) == "/var/lib/flatpak/exports/bin/org.sculptforge.SculptForge");
    // Not a Flatpak, or a path that doesn't name the app: nothing.
    CHECK(flatpakHostLauncher("").empty());
    CHECK(flatpakHostLauncher("[Application]\nname=org.sculptforge.SculptForge\n").empty());
    CHECK(flatpakHostLauncher("[Application]\nname=org.other.App\n[Instance]\napp-path=/var/lib/flatpak/app/x/files\n").empty());

#if !defined(_WIN32) && !defined(__APPLE__)
    // Inside the sandbox the exchange folder is the host's, where the Blender add-on looks.
    const char* oldCustom = std::getenv("SCULPTFORGE_BLENDER_EXCHANGE");
    const std::string keepCustom = oldCustom ? oldCustom : "";
    unsetenv("SCULPTFORGE_BLENDER_EXCHANGE");
    setenv("FLATPAK_ID", "org.sculptforge.SculptForge", 1);
    setenv("HOST_XDG_DATA_HOME", "/home/ana/.local/share", 1);
    CHECK(blenderExchangeRoot() == "/home/ana/.local/share/SculptForge/SculptForge/blender");
    unsetenv("HOST_XDG_DATA_HOME");
    unsetenv("FLATPAK_ID");
    if (oldCustom) setenv("SCULPTFORGE_BLENDER_EXCHANGE", keepCustom.c_str(), 1);
#endif
}
