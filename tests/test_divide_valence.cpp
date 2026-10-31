// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Divide on meshes with high-valence vertices (#16): Blender's cylinder with Triangle Fan caps (the fan centre has
// one edge per side) and its UV sphere (poles with one edge per segment) crashed Divide from 22 sides up. Built
// here as Blender builds them, through OBJ and through the Blender Bridge (.GoZ + manifest), then divided three
// times; the levels' adjacency must match a plain rebuild and stepping down and up must keep the shape.
#include "TestHarness.h"

#include "core/Commands.h"
#include "core/Multires.h"
#include "core/Primitives.h"
#include "io/BlenderBridge.h"
#include "io/GozIO.h"
#include "io/ObjIO.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>

using namespace sf;
namespace fs = std::filesystem;

namespace {

const CommandRegistry& registry() {
    static CommandRegistry r;
    return r;
}

// Blender's "Add Cylinder": `n` vertices per ring, quad sides, caps as triangle fans around a centre vertex
// (fan = true) or one n-gon each. UVs as Blender lays them out: the sides a strip with a seam, each cap a disc.
std::string cylinderObj(int n, bool fan) {
    std::ostringstream s;
    const double pi = 3.14159265358979323846;
    for (int ring = 0; ring < 2; ++ring)
        for (int i = 0; i < n; ++i)
            s << "v " << std::cos(2 * pi * i / n) << ' ' << (ring ? 1 : -1) << ' ' << std::sin(2 * pi * i / n) << '\n';
    for (int ring = 0; ring < 2; ++ring)
        for (int i = 0; i <= n; ++i) s << "vt " << double(i) / n << ' ' << (ring ? 0.5 : 0.0) << '\n';
    for (int cap = 0; cap < 2; ++cap)
        for (int i = 0; i < n; ++i)
            s << "vt " << 0.25 + cap * 0.5 + 0.2 * std::cos(2 * pi * i / n) << ' ' << 0.75 + 0.2 * std::sin(2 * pi * i / n) << '\n';
    const int capUv = 2 * (n + 1) + 1; // 1-based: bottom cap's first, then the top's
    for (int i = 0; i < n; ++i) {
        const int a = 1 + i, b = 1 + (i + 1) % n, ta = 1 + i, tb = 2 + i;
        s << "f " << a << '/' << ta << ' ' << b << '/' << tb << ' ' << b + n << '/' << tb + n + 1 << ' ' << a + n << '/'
          << ta + n + 1 << '\n';
    }
    auto cv = [&](int cap, int i) { return capUv + cap * n + (i % n); };
    if (fan) {
        s << "v 0 -1 0\nv 0 1 0\nvt 0.25 0.75\nvt 0.75 0.75\n";
        const int bottom = 2 * n + 1, top = 2 * n + 2, cb = capUv + 2 * n, ct = cb + 1;
        for (int i = 0; i < n; ++i) {
            const int a = 1 + i, b = 1 + (i + 1) % n;
            s << "f " << bottom << '/' << cb << ' ' << b << '/' << cv(0, i + 1) << ' ' << a << '/' << cv(0, i) << '\n';
            s << "f " << top << '/' << ct << ' ' << a + n << '/' << cv(1, i) << ' ' << b + n << '/' << cv(1, i + 1) << '\n';
        }
    } else {
        s << 'f';
        for (int i = n - 1; i >= 0; --i) s << ' ' << 1 + i << '/' << cv(0, i);
        s << "\nf";
        for (int i = 0; i < n; ++i) s << ' ' << 1 + i + n << '/' << cv(1, i);
        s << '\n';
    }
    return s.str();
}

// Blender's "Add UV Sphere": `segments` around, `rings` from pole to pole; triangle fans at the poles. UVs as
// Blender's: a grid with a seam, each pole triangle with its own pole UV.
std::string uvSphereObj(int segments, int rings) {
    std::ostringstream s;
    const double pi = 3.14159265358979323846;
    s << "v 0 1 0\n"; // 1: north pole
    for (int r = 1; r < rings; ++r)
        for (int i = 0; i < segments; ++i) {
            const double th = pi * r / rings, ph = 2 * pi * i / segments;
            s << "v " << std::sin(th) * std::cos(ph) << ' ' << std::cos(th) << ' ' << std::sin(th) * std::sin(ph) << '\n';
        }
    s << "v 0 -1 0\n";
    for (int r = 0; r <= rings; ++r)
        for (int i = 0; i <= segments; ++i)
            s << "vt " << (i + (r == 0 || r == rings ? 0.5 : 0.0)) / segments << ' ' << 1.0 - double(r) / rings << '\n';
    const int south = 2 + (rings - 1) * segments;
    auto at = [&](int r, int i) {
        if (r == 0) return 1;
        if (r == rings) return south;
        return 2 + (r - 1) * segments + (i % segments);
    };
    auto uv = [&](int r, int i) { return 1 + r * (segments + 1) + i; };
    auto corner = [&](int r, int i) { return std::to_string(at(r, i)) + '/' + std::to_string(uv(r, i)) + ' '; };
    for (int i = 0; i < segments; ++i)
        for (int r = 0; r < rings; ++r) {
            if (r == 0) s << "f " << corner(0, i) << corner(1, i) << corner(1, i + 1) << '\n';
            else if (r + 1 == rings) s << "f " << corner(r, i) << corner(r + 1, i) << corner(r, i + 1) << '\n';
            else s << "f " << corner(r, i) << corner(r + 1, i) << corner(r + 1, i + 1) << corner(r, i + 1) << '\n';
        }
    return s.str();
}

bool loadObjText(const std::string& text, Mesh& out) {
    const std::string path = tempPath("sf_divide_valence.obj");
    std::ofstream(path, std::ios::binary) << text;
    std::string err;
    const bool ok = loadObj(path, out, err);
    if (!ok) std::printf("  (loadObj: %s)\n", err.c_str());
    return ok;
}

// The level's adjacency (implicit or stored) matches a plain rebuild of the same triangles.
bool adjacencyMatchesRebuild(const Mesh& m) {
    Mesh plain;
    plain.positions = m.positions;
    plain.triangles = m.triangles;
    plain.rebuildTopology();
    for (uint32_t v = 0; v < m.vertexCount(); ++v) {
        const IndexRing a = m.neighbors(v), b = plain.neighbors(v);
        const IndexRing ta = m.vertexTriangles(v), tb = plain.vertexTriangles(v);
        if (!std::equal(a.begin(), a.end(), b.begin(), b.end())) return false;
        if (!std::equal(ta.begin(), ta.end(), tb.begin(), tb.end())) return false;
    }
    return true;
}

// Divides three times (the third only if under the triangle budget), checking each level, then steps down to the
// base and back up.
bool divideThrice(Document& d, const char* what) {
    const std::vector<glm::vec3> base = d.mesh.positions;
    bool ok = true;
    for (int level = 1; level <= 3; ++level) {
        const size_t before = d.mesh.triangleCount();
        const CommandResult r = registry().execute(d, "subdiv_divide");
        if (!r.ok) {
            std::printf("  (%s, divide %d: %s)\n", what, level, r.message.c_str());
            return false;
        }
        ok = ok && d.mesh.triangleCount() >= 2 * before && subdivCurrentLevel(d.mesh) == level;
        ok = ok && adjacencyMatchesRebuild(d.mesh);
        for (const glm::vec3& p : d.mesh.positions) ok = ok && std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z);
    }
    std::string err;
    ok = ok && subdivSetLevel(d.mesh, 0, err) && d.mesh.positions == base;
    ok = ok && subdivSetLevel(d.mesh, 3, err) && adjacencyMatchesRebuild(d.mesh);
    if (!ok) std::printf("  (%s: wrong result)\n", what);
    return ok;
}

} // namespace

TEST(divide_cylinder_triangle_fan_caps) {
    // 21 sides divided in 0.9.4, 22 crashed; any number must work (and the n-gon caps Blender also offers).
    for (int n : {3, 20, 21, 22, 23, 31, 32, 33, 40, 63, 64, 65, 128, 300}) {
        for (bool fan : {true, false}) {
            Mesh m;
            CHECK(loadObjText(cylinderObj(n, fan), m));
            Document d = makeDoc(std::move(m));
            const std::string what = "cylinder " + std::to_string(n) + (fan ? " fan" : " ngon");
            CHECK(divideThrice(d, what.c_str()));
        }
    }
}

TEST(divide_uv_sphere) {
    for (auto [segments, rings] : {std::pair{32, 16}, std::pair{64, 32}, std::pair{24, 12}, std::pair{200, 8}}) {
        Mesh m;
        CHECK(loadObjText(uvSphereObj(segments, rings), m));
        Document d = makeDoc(std::move(m));
        CHECK(divideThrice(d, ("uv sphere " + std::to_string(segments)).c_str()));
    }
}

TEST(divide_after_blender_bridge) {
    // The issue's path: Blender sends the mesh through the bridge (a .GoZ and a manifest), then Ctrl+D.
    const fs::path root = fs::temp_directory_path() / "sf_divide_bridge";
    fs::remove_all(root);
    const fs::path inbox = root / "to_sculptforge";
    fs::create_directories(inbox);
    std::string err;
    Mesh cylinder, sphere;
    CHECK(loadObjText(cylinderObj(22, true), cylinder));
    CHECK(saveGoz((inbox / "r1_000.GoZ").string(), cylinder, "Cylinder", err));
    CHECK(loadObjText(uvSphereObj(32, 16), sphere));
    CHECK(saveGoz((inbox / "r1_001.GoZ").string(), sphere, "Sphere", err));
    std::ofstream(inbox / "manifest.json") << R"({"format": "sculptforge-bridge", "version": 1, "revision": 1,
        "sender": "blender", "objects": [{"name": "Cylinder", "file": "r1_000.GoZ"},
        {"name": "Sphere", "file": "r1_001.GoZ"}]})";
    Document d = makeDoc(makeCube(2));
    CHECK(registry().execute(d, "blender_receive", {{"folder", root.string()}}).ok);
    CHECK(d.subtoolCount() == 2);
    CHECK(divideThrice(d, "bridge cylinder 22"));
    d.selectSubtool(1);
    CHECK(divideThrice(d, "bridge uv sphere"));
    fs::remove_all(root);
}
