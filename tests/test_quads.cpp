// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Quads: imported quads stay paired (Mesh::quadPartners) through sculpting,
// undo, sculpt layers, Parts, projects and subdivision, are written back as
// quads (OBJ, FBX, PLY, USD, Bridge), and topology rebuilds (remesh, decimate,
// deleting part of a quad, edits that break a pair) never leave a stale pair.
#include "TestHarness.h"

#include "core/Commands.h"
#include "core/Decimate.h"
#include "core/FaceGroups.h"
#include "core/Primitives.h"
#include "core/Project.h"
#include "core/Parts.h"
#include "core/Voxel.h"
#include "io/FbxIO.h"
#include "io/BridgeIO.h"
#include "io/BridgeProtocol.h"
#include "io/ObjIO.h"
#include "io/PlyIO.h"
#include "io/UsdIO.h"

#include <filesystem>
#include <fstream>
#include <map>
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

// OBJ faces by corner count: {3: n, 4: n, ...}.
std::map<int, size_t> objFaceSizes(const std::string& text) {
    std::map<int, size_t> sizes;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        if (line.rfind("f ", 0) != 0) continue;
        std::istringstream f(line.substr(2));
        std::string corner;
        int n = 0;
        while (f >> corner) ++n;
        ++sizes[n];
    }
    return sizes;
}

// A closed cube of n x n quads per side (welded, wound outwards) or Blender would write it.
std::string quadCubeObj(int n) {
    std::map<std::tuple<int, int, int>, int> index;
    std::string verts, faces;
    auto vertex = [&](int x, int y, int z) {
        auto [it, inserted] = index.try_emplace({x, y, z}, int(index.size()) + 1);
        if (inserted)
            verts += "v " + std::to_string(2.0f * float(x) / float(n) - 1.0f) + " " + std::to_string(2.0f * float(y) / float(n) - 1.0f) +
                     " " + std::to_string(2.0f * float(z) / float(n) - 1.0f) + "\n";
        return it->second;
    };
    for (int a = 0; a < 3; ++a)
        for (int side : {0, n}) {
            const int u = (a + 1) % 3, v = (a + 2) % 3;
            for (int i = 0; i < n; ++i)
                for (int j = 0; j < n; ++j) {
                    int c[4];
                    const int du[4] = {0, 1, 1, 0}, dv[4] = {0, 0, 1, 1};
                    for (int k = 0; k < 4; ++k) {
                        int p[3];
                        p[a] = side;
                        p[u] = i + du[k];
                        p[v] = j + dv[k];
                        c[k] = vertex(p[0], p[1], p[2]);
                    }
                    if (side == 0) std::swap(c[1], c[3]); // the -a side faces the other way
                    faces += "f " + std::to_string(c[0]) + " " + std::to_string(c[1]) + " " + std::to_string(c[2]) + " " +
                             std::to_string(c[3]) + "\n";
                }
        }
    return verts + faces;
}

Mesh loadQuadCube(int n) {
    const std::string path = tempPath("sf_quads_cube_in.obj");
    writeText(path, quadCubeObj(n));
    Mesh m;
    std::string err;
    CHECK(loadObj(path, m, err));
    return m;
}

// Every stored pair is mutual and shares an edge (what sanitizeAttributes guarantees).
bool pairsValid(const Mesh& m) {
    if (m.quadPartners.empty()) return true;
    if (m.quadPartners.size() != m.triangleCount()) return false;
    for (uint32_t t = 0; t < m.triangleCount(); ++t) {
        const uint32_t u = m.quadPartners[t];
        if (u == Mesh::kNoQuad) continue;
        if (u >= m.triangleCount() || m.quadPartners[u] != t || m.quadDiagonal(t, u) < 0) return false;
    }
    return true;
}

} // namespace

TEST(quads_obj_import_pairs_quads_and_fans_ngons) {
    const Mesh cube = loadQuadCube(3);
    CHECK(cube.triangleCount() == 6 * 9 * 2);
    CHECK(cube.quadCount() == 6 * 9);
    CHECK(pairsValid(cube));
    // Every triangle has a partner, and the diagonal is the shared edge.
    for (uint32_t t = 0; t < cube.triangleCount(); ++t) CHECK(cube.quadPartnerAt(t) != Mesh::kNoQuad);

    // Mixed file: a quad, a triangle and a pentagon (fanned, unpaired).
    const std::string path = tempPath("sf_quads_mixed.obj");
    writeText(path, "v 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\nv 2 0 0\nv 3 0 0\nv 3 1 0\nv 2.5 1.5 0\nv 2 1 0\n"
                    "f 1 2 3 4\nf 2 5 3\nf 5 6 7 8 9\n");
    Mesh m;
    std::string err;
    CHECK(loadObj(path, m, err));
    CHECK(m.triangleCount() == 2 + 1 + 3);
    CHECK(m.quadCount() == 1);
    CHECK(m.quadPartnerAt(0) == 1 && m.quadPartnerAt(1) == 0);
    for (uint32_t t = 2; t < m.triangleCount(); ++t) CHECK(m.quadPartnerAt(t) == Mesh::kNoQuad);

    // Written back: one quad, four triangles (the pentagon stays fanned).
    const std::string out = tempPath("sf_quads_mixed_out.obj");
    CHECK(saveObj(out, m, err));
    const auto sizes = objFaceSizes(readText(out));
    CHECK(sizes.at(4) == 1 && sizes.at(3) == 4 && sizes.size() == 2);
    CHECK(readText(out).find("f 1 2 3 4\n") != std::string::npos); // same corners, same winding
}

TEST(quads_obj_round_trip_keeps_quad_count) {
    const Mesh cube = loadQuadCube(4);
    std::string err;
    const std::string a = tempPath("sf_quads_rt_a.obj"), b = tempPath("sf_quads_rt_b.obj");
    CHECK(saveObj(a, cube, err));
    const auto sizes = objFaceSizes(readText(a));
    CHECK(sizes.size() == 1 && sizes.at(4) == 6 * 16);
    Mesh back;
    CHECK(loadObj(a, back, err));
    CHECK(back.quadCount() == 6 * 16 && back.triangles == cube.triangles);
    CHECK(saveObj(b, back, err));
    CHECK(readText(a) == readText(b));
}

TEST(quads_invalid_pairs_are_dropped) {
    Mesh m = loadQuadCube(2);
    // A pair that doesn't share an edge, and a one-sided pair, are unpaired by rebuildTopology.
    m.quadPartners[0] = 5;
    m.quadPartners[5] = 0;
    m.quadPartners[10] = 13; // 13 still names 12, and 10's real partner 11 still names 10: both one-sided
    m.rebuildTopology();
    CHECK(pairsValid(m));
    CHECK(m.quadPartnerAt(0) == Mesh::kNoQuad && m.quadPartnerAt(5) == Mesh::kNoQuad);
    CHECK(m.quadPartnerAt(10) == Mesh::kNoQuad && m.quadPartnerAt(11) == Mesh::kNoQuad);
    CHECK(m.quadPartnerAt(12) == 13 && m.quadPartnerAt(13) == 12);
    CHECK(m.quadCount() < 6 * 4);
    // Out-of-range ids and a wrongly sized array (malformed project) never survive.
    Mesh n = loadQuadCube(2);
    n.quadPartners[3] = 1000000;
    n.rebuildTopology();
    CHECK(pairsValid(n) && n.quadPartnerAt(3) == Mesh::kNoQuad);
    n.quadPartners.pop_back();
    n.rebuildTopology();
    CHECK(!n.hasQuads());
    // Nothing paired at all frees the attribute.
    Mesh p = loadQuadCube(1);
    std::fill(p.quadPartners.begin(), p.quadPartners.end(), Mesh::kNoQuad);
    p.rebuildTopology();
    CHECK(!p.hasQuads());
}

TEST(quads_survive_sculpting_layers_and_undo) {
    Document d = makeDoc(loadQuadCube(8));
    const std::vector<uint32_t> pairs = d.mesh.quadPartners;
    CHECK(!pairs.empty());
    DabParams p;
    p.center = glm::vec3(0, 0, 1);
    p.radius = 0.6f;
    p.strength = 1.0f;
    const std::vector<glm::vec3> before = d.mesh.positions;
    CHECK(d.sculptor.beginStroke(d.mesh, "Standard"));
    d.sculptor.dab(d.mesh, d.bvh, BrushType::Standard, p, Symmetry{});
    d.sculptor.dab(d.mesh, d.bvh, BrushType::Inflate, p, Symmetry{});
    d.undo.push(d.sculptor.endStroke());
    CHECK(d.mesh.positions != before);
    CHECK(d.mesh.quadPartners == pairs);
    CommandRegistry reg;
    CHECK(reg.execute(d, "undo").ok);
    CHECK(d.mesh.positions == before && d.mesh.quadPartners == pairs);
    CHECK(reg.execute(d, "redo").ok);
    CHECK(d.mesh.quadPartners == pairs);

    // A stroke on a sculpt layer.
    d.activeLayer = d.mesh.addLayer("Detail");
    CHECK(d.sculptor.beginStroke(d.mesh, "Layer", d.activeLayer));
    d.sculptor.dab(d.mesh, d.bvh, BrushType::Standard, p, Symmetry{});
    d.undo.push(d.sculptor.endStroke());
    CHECK(d.mesh.quadPartners == pairs);

    // Subdivide (a full-mesh snapshot): every quad becomes four, and undo brings the original pairs back.
    CHECK(reg.execute(d, "subdivide").ok);
    CHECK(d.mesh.triangleCount() == pairs.size() * 4);
    CHECK(d.mesh.quadCount() == pairs.size() * 2 && pairsValid(d.mesh));
    CHECK(reg.execute(d, "undo").ok);
    CHECK(d.mesh.quadPartners == pairs);
}

TEST(quads_subdivision_makes_quads_and_keeps_triangles) {
    // A quad cube subdivides into quads only: 4 per quad, each one a valid pair.
    const Mesh cube = loadQuadCube(2);
    const Mesh s = loopSubdivide(cube);
    CHECK(s.triangleCount() == cube.triangleCount() * 4);
    CHECK(s.quadCount() == cube.quadCount() * 4 && pairsValid(s));
    const Mesh s2 = loopSubdivide(s);
    CHECK(s2.quadCount() == cube.quadCount() * 16 && pairsValid(s2));
    // Triangle meshes stay triangles.
    CHECK(!loopSubdivide(makeIcosphere(1)).hasQuads());
    // Primitives built from quads keep them.
    CHECK(makePlane(4).quadCount() == 16 && makeCube(3).quadCount() == 54);
}

TEST(quads_topology_rebuilds_drop_or_remake_pairs) {
    const Mesh cube = loadQuadCube(6);
    // Voxel remesh: none of the old pairs survive; the new surface is quads of its own (like voxel remesh).
    const Mesh r = voxelRemesh(cube, 32);
    CHECK(r.triangleCount() > 0 && pairsValid(r));
    CHECK(r.quadCount() * 2 == r.triangleCount());
    // Decimation gives triangles.
    const Mesh dec = decimate(cube, cube.triangleCount() / 3);
    CHECK(dec.triangleCount() < cube.triangleCount() && !dec.hasQuads());

    // Deleting one half of a quad unpairs the other half; whole quads stay paired.
    Mesh m = cube;
    m.ensureHidden();
    m.hidden[0] = 1; // one half of quad 0
    m.hidden[2] = m.hidden[m.quadPartnerAt(2)] = 1; // a whole quad
    m.facesDirty = true;
    const Mesh kept = deleteHiddenTriangles(m);
    CHECK(kept.triangleCount() == cube.triangleCount() - 3);
    CHECK(pairsValid(kept) && kept.quadCount() == cube.quadCount() - 2);

    // Splitting by face groups and merging Parts keep pairs (with shifted triangle ids).
    Mesh grouped = cube;
    grouped.ensureFaceGroups();
    for (uint32_t t = 0; t < grouped.triangleCount(); ++t) grouped.facegroups[t] = t < grouped.triangleCount() / 2 ? 1 : 2;
    const auto parts = splitByFaceGroups(grouped);
    CHECK(parts.size() == 2);
    size_t quads = 0;
    for (const auto& part : parts) {
        CHECK(pairsValid(part));
        quads += part.quadCount();
    }
    CHECK(quads == cube.quadCount());
    const Mesh plain = makeIcosphere(1);
    const Mesh merged = mergeMeshes({&plain, &cube, &parts[0]});
    CHECK(pairsValid(merged) && merged.quadCount() == cube.quadCount() + parts[0].quadCount());
    CHECK(merged.quadPartnerAt(0) == Mesh::kNoQuad);
}

TEST(quads_split_between_facegroups_show_and_export_as_triangles) {
    Mesh m = loadQuadCube(1);
    m.ensureFaceGroups(1);
    const uint32_t partner = m.quadPartnerAt(0);
    m.facegroups[partner] = 2;
    CHECK(m.quadPartnerAt(0) == Mesh::kNoQuad && m.quadCount() == 5);
    std::string err;
    const std::string path = tempPath("sf_quads_groups.obj");
    CHECK(saveObj(path, m, err));
    const auto sizes = objFaceSizes(readText(path));
    CHECK(sizes.at(4) == 5 && sizes.at(3) == 2);
}

TEST(quads_project_round_trip) {
    Document doc = makeDoc(loadQuadCube(3));
    doc.addPart(makeIcosphere(1), "Ball", false);
    const std::string path = tempPath("sf_quads.sfproj");
    std::string err;
    CHECK(saveProject(path, doc, err));
    Document back;
    CHECK(loadProject(path, back, err));
    CHECK(back.mesh.quadPartners == doc.mesh.quadPartners && back.mesh.quadCount() == 54);
    CHECK(!back.partMesh(1).hasQuads());
}

TEST(quads_round_trip_through_fbx_ply_usd_bridge) {
    const Mesh cube = loadQuadCube(3);
    const size_t quads = cube.quadCount();
    std::string err;

    const std::string fbx = tempPath("sf_quads.fbx");
    CHECK(saveFbx(fbx, "Cube", {&cube}, FbxExportOptions{}, err));
    Mesh f;
    CHECK(loadFbx(fbx, f, err));
    CHECK(f.quadCount() == quads && pairsValid(f));

    const std::string ply = tempPath("sf_quads.ply");
    CHECK(savePlyMesh(ply, cube, err));
    Mesh p;
    SplatCloud splats;
    PlyContent content;
    CHECK(loadPly(ply, p, splats, content, err));
    CHECK(p.quadCount() == quads && p.triangleCount() == cube.triangleCount());

    const std::string usd = tempPath("sf_quads.usda");
    CHECK(saveUsd(usd, &cube, nullptr, "Cube", err));
    CHECK(readText(usd).find("int[] faceVertexCounts = [4, 4, 4") != std::string::npos);
    Mesh u;
    CHECK(loadUsd(usd, u, splats, err));
    CHECK(u.quadCount() == quads);

    const std::string bridge = tempPath((std::string("sf_quads") + bridgefile::kExt).c_str());
    CHECK(saveBridge(bridge, cube, "Cube", err));
    Mesh g;
    BridgeInfo info;
    CHECK(loadBridge(bridge, g, err, &info));
    CHECK(info.quads == quads && info.triangles == 0);
    CHECK(g.quadCount() == quads && g.triangles == cube.triangles);
}
