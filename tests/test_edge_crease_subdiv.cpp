// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Subdivision with edge creases (#108 slice 3, core/Multires.h EdgeSharpness): Catmull-Clark and Loop semi-sharp
// creases against hand-computed values on a unit cube, the s - 1 rule across levels, bit-identical results without
// creases (and away from them), levels that follow a crease change at the base, Remove Below and projects; USD
// creases out and in (slice 4).
#include "TestHarness.h"

#include "core/Commands.h"
#include "core/Document.h"
#include "core/EdgeSelect.h"
#include "core/Multires.h"
#include "core/Primitives.h"
#include "core/Project.h"
#include "io/UsdIO.h"

#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>

using namespace sf;

namespace {

const CommandRegistry& registry() {
    static CommandRegistry r;
    return r;
}

uint32_t vertexAt(const Mesh& m, const glm::vec3& p) {
    for (uint32_t v = 0; v < m.vertexCount(); ++v)
        if (glm::length(m.positions[v] - p) < 1e-5f) return v;
    return ~0u;
}

// Selects the edge between the cube corners at `a` and `b` (adding to the selection).
bool selectEdge(Document& doc, const glm::vec3& a, const glm::vec3& b) {
    const uint32_t va = vertexAt(doc.mesh, a), vb = vertexAt(doc.mesh, b);
    if (va == ~0u || vb == ~0u) return false;
    return registry().execute(doc, "edge_select", {{"op", std::string("edge")}, {"a", double(va)}, {"b", double(vb)}, {"add", true}}).ok;
}

// The fine vertex of the coarse edge between vertices a and b on generated level `level`.
glm::vec3 edgePoint(const Mesh& fine, int level, uint32_t a, uint32_t b) {
    const SubdivTopology& tp = *subdivLevel(fine, level).topo;
    for (uint32_t e = 0; e < tp.edges.size(); ++e)
        if ((tp.edges[e].x == a && tp.edges[e].y == b) || (tp.edges[e].x == b && tp.edges[e].y == a))
            return fine.positions[tp.edgeChildOf(e)];
    return glm::vec3(1e9f);
}

bool near(const glm::vec3& a, const glm::vec3& b, float eps = 1e-5f) { return glm::length(a - b) < eps; }

bool samePositions(const Mesh& a, const Mesh& b, float eps) {
    if (a.vertexCount() != b.vertexCount()) return false;
    for (uint32_t v = 0; v < a.vertexCount(); ++v)
        if (glm::length(a.positions[v] - b.positions[v]) > eps) return false;
    return true;
}

const glm::vec3 A(-1, -1, -1), B(1, -1, -1), C(1, -1, 1), D(-1, -1, 1); // the bottom face's corners

} // namespace

TEST(edge_crease_subdiv_cube_reference_values) {
    // One fully sharp edge AB: its new point is its midpoint (plain Catmull-Clark: (A + B + two face centres) / 4);
    // A and B have one creased edge each (darts), so they move as without creases.
    Document plain = makeDoc(makeCube(1));
    Document one = makeDoc(makeCube(1));
    CHECK(selectEdge(one, A, B));
    CHECK(registry().execute(one, "edge_crease", {{"value", 1.0}}).ok);
    const uint32_t a = vertexAt(one.mesh, A), b = vertexAt(one.mesh, B);
    CHECK(registry().execute(plain, "subdiv_divide", {}).ok);
    CHECK(registry().execute(one, "subdiv_divide", {}).ok);
    CHECK(near(edgePoint(plain.mesh, 1, a, b), glm::vec3(0, -0.75f, -0.75f)));
    CHECK(near(edgePoint(one.mesh, 1, a, b), glm::vec3(0, -1, -1)));
    CHECK(one.mesh.positions[a] == plain.mesh.positions[a]);
    CHECK(subdivLevelSharpness(one.mesh, 1) && subdivLevelSharpness(one.mesh, 1)->edges.size() == 1);
    // Sharpness 0.5 (crease sqrt(0.05)): halfway between the smooth point and the midpoint.
    Document half = makeDoc(makeCube(1));
    CHECK(selectEdge(half, A, B));
    CHECK(registry().execute(half, "edge_crease", {{"value", std::sqrt(0.05)}}).ok);
    CHECK(registry().execute(half, "subdiv_divide", {}).ok);
    CHECK(near(edgePoint(half.mesh, 1, a, b), glm::vec3(0, -0.875f, -0.875f)));

    // The bottom face's ring creased: each of its corners lies on two creased edges, the crease rule
    // 3/4 P + 1/8 (the two neighbours along the crease): A -> (-0.75, -1, -0.75).
    Document ring = makeDoc(makeCube(1));
    CHECK(selectEdge(ring, A, B) && selectEdge(ring, B, C) && selectEdge(ring, C, D) && selectEdge(ring, D, A));
    CHECK(ring.edgeSelection.count == 4);
    CHECK(registry().execute(ring, "edge_crease", {}).ok);
    CHECK(registry().execute(ring, "subdiv_divide", {}).ok);
    CHECK(near(ring.mesh.positions[a], glm::vec3(-0.75f, -1, -0.75f)));
    CHECK(near(edgePoint(ring.mesh, 1, a, b), glm::vec3(0, -1, -1)));
    // The top corners have no creased edge: exactly as without creases.
    for (uint32_t v = 0; v < 8; ++v)
        if (plain.mesh.positions[v].y > 0.0f) CHECK(ring.mesh.positions[v] == plain.mesh.positions[v]);

    // Every edge fully sharp: the corners stay put (three creased edges) and the cube stays a cube on every level.
    Document all = makeDoc(makeCube(1));
    CHECK(registry().execute(all, "edge_select", {{"op", std::string("all")}}).ok);
    CHECK(registry().execute(all, "edge_crease", {}).ok);
    for (int i = 0; i < 3; ++i) CHECK(registry().execute(all, "subdiv_divide", {}).ok);
    bool cube = true;
    for (const glm::vec3& p : all.mesh.positions) {
        const float m = std::max({std::fabs(p.x), std::fabs(p.y), std::fabs(p.z)});
        cube = cube && std::fabs(m - 1.0f) < 1e-5f;
    }
    CHECK(cube);
    CHECK(all.mesh.positions[a] == A);
}

TEST(edge_crease_subdiv_sharpness_steps_down_per_level) {
    // Crease 0.5 = sharpness 2.5: 2.5, 1.5, 0.5 on levels 1-3, nothing on level 4.
    Document doc = makeDoc(makeCube(1));
    CHECK(selectEdge(doc, A, B));
    CHECK(registry().execute(doc, "edge_crease", {{"value", 0.5}}).ok);
    for (int i = 0; i < 4; ++i) CHECK(registry().execute(doc, "subdiv_divide", {}).ok);
    const float want[] = {2.5f, 1.5f, 0.5f};
    for (int k = 1; k <= 3; ++k) {
        const EdgeSharpness* s = subdivLevelSharpness(doc.mesh, k);
        CHECK(s != nullptr);
        if (!s) continue;
        CHECK(s->edges.size() == size_t(1) << (k - 1)); // the edge, its halves, their halves
        for (float x : s->sharpness) CHECK(std::fabs(x - want[k - 1]) < 1e-5f);
    }
    CHECK(subdivLevelSharpness(doc.mesh, 4) == nullptr);
}

TEST(edge_crease_subdiv_bit_identical_without_creases) {
    // Creased then uncreased (the array is freed): every level bit-identical to never creased.
    Document plain = makeDoc(makeCube(3));
    Document undone = makeDoc(makeCube(3));
    CHECK(registry().execute(undone, "edge_select", {{"op", std::string("angle")}}).ok);
    CHECK(registry().execute(undone, "edge_crease", {}).ok);
    CHECK(registry().execute(undone, "edge_crease", {{"clear", true}, {"all", true}}).ok);
    for (int i = 0; i < 3; ++i) {
        CHECK(registry().execute(plain, "subdiv_divide", {}).ok);
        CHECK(registry().execute(undone, "subdiv_divide", {}).ok);
        CHECK(plain.mesh.positions.size() == undone.mesh.positions.size());
        CHECK(std::memcmp(plain.mesh.positions.data(), undone.mesh.positions.data(),
                          plain.mesh.positions.size() * sizeof(glm::vec3)) == 0);
    }
    // Loop (triangles): all edges sharp keeps the icosahedron's vertices; none moves them as before.
    Document ico = makeDoc(makeIcosphere(0));
    Document icoPlain = makeDoc(makeIcosphere(0));
    const glm::vec3 v0 = ico.mesh.positions[0];
    CHECK(registry().execute(ico, "edge_select", {{"op", std::string("all")}}).ok);
    CHECK(registry().execute(ico, "edge_crease", {}).ok);
    CHECK(registry().execute(ico, "subdiv_divide", {}).ok);
    CHECK(registry().execute(icoPlain, "subdiv_divide", {}).ok);
    CHECK(ico.mesh.positions[0] == v0);
    CHECK(icoPlain.mesh.positions[0] != v0);
    // Edge points are midpoints.
    const SubdivTopology& tp = *subdivLevel(ico.mesh, 1).topo;
    CHECK(!tp.catmullClark);
    CHECK(near(ico.mesh.positions[tp.edgeChildOf(0)],
               (ico.mesh.positions[tp.edges[0].x] + ico.mesh.positions[tp.edges[0].y]) * 0.5f));
}

TEST(edge_crease_subdiv_levels_follow_the_base) {
    // Creasing at Level 1 under existing levels reshapes them as if divided with the crease; uncreasing (undo) gives
    // the smooth levels back; stepping is exact both ways.
    Document fresh = makeDoc(makeCube(2));
    CHECK(registry().execute(fresh, "edge_select", {{"op", std::string("angle")}}).ok);
    CHECK(registry().execute(fresh, "edge_crease", {}).ok);
    for (int i = 0; i < 2; ++i) CHECK(registry().execute(fresh, "subdiv_divide", {}).ok);

    Document doc = makeDoc(makeCube(2));
    for (int i = 0; i < 2; ++i) CHECK(registry().execute(doc, "subdiv_divide", {}).ok);
    const std::vector<glm::vec3> smooth = doc.mesh.positions;
    CHECK(registry().execute(doc, "subdiv_level", {{"level", 1.0}}).ok);
    CHECK(registry().execute(doc, "edge_select", {{"op", std::string("angle")}}).ok);
    CHECK(registry().execute(doc, "edge_crease", {}).ok);
    CHECK(registry().execute(doc, "subdiv_level", {{"level", 3.0}}).ok);
    CHECK(samePositions(doc.mesh, fresh.mesh, 1e-5f));
    const std::vector<glm::vec3> creased = doc.mesh.positions;
    CHECK(registry().execute(doc, "subdiv_level", {{"level", 1.0}}).ok);
    CHECK(registry().execute(doc, "subdiv_level", {{"level", 3.0}}).ok);
    CHECK(doc.mesh.positions == creased); // round trip: bit for bit
    // Uncreased again: the smooth levels come back.
    CHECK(registry().execute(doc, "subdiv_level", {{"level", 1.0}}).ok);
    CHECK(registry().execute(doc, "edge_crease", {{"clear", true}, {"all", true}}).ok);
    CHECK(registry().execute(doc, "subdiv_level", {{"level", 3.0}}).ok);
    bool back = doc.mesh.positions.size() == smooth.size();
    for (size_t v = 0; back && v < smooth.size(); ++v) back = glm::length(doc.mesh.positions[v] - smooth[v]) < 1e-5f;
    CHECK(back);
}

TEST(edge_crease_subdiv_delete_lower_and_projects_keep_the_shape) {
    Document doc = makeDoc(makeCube(2));
    CHECK(registry().execute(doc, "edge_select", {{"op", std::string("angle")}}).ok);
    CHECK(registry().execute(doc, "edge_crease", {{"value", 0.6}}).ok);
    for (int i = 0; i < 3; ++i) CHECK(registry().execute(doc, "subdiv_divide", {}).ok);
    const std::vector<glm::vec3> top = doc.mesh.positions;

    // Saved and loaded: the sharpness follows from the base's creases again.
    std::string err;
    const std::string path = tempPath("edge_crease_subdiv.sfproj");
    CHECK(saveProject(path, doc, err));
    Document loaded;
    CHECK(loadProject(path, loaded, err));
    std::remove(path.c_str());
    for (int k = 1; k <= 3; ++k) {
        const EdgeSharpness* a = subdivLevelSharpness(doc.mesh, k);
        const EdgeSharpness* b = subdivLevelSharpness(loaded.mesh, k);
        CHECK(bool(a) == bool(b));
        if (a && b) CHECK(a->edges == b->edges && a->sharpness == b->sharpness);
    }
    CHECK(registry().execute(loaded, "subdiv_level", {{"level", 1.0}}).ok);
    CHECK(registry().execute(loaded, "subdiv_level", {{"level", 4.0}}).ok);
    CHECK(samePositions(loaded.mesh, doc.mesh, 1e-5f));

    // Remove Below at Level 2: Level 2 keeps what is left of the creases (sharpness 3.6 - 1) as its own, so the
    // levels above keep their shape.
    CHECK(registry().execute(doc, "subdiv_level", {{"level", 2.0}}).ok);
    CHECK(registry().execute(doc, "subdiv_delete_lower", {}).ok);
    CHECK(doc.mesh.hasCreases());
    float most = 0.0f;
    for (float c : doc.mesh.creases) most = std::max(most, c);
    CHECK(std::fabs(creaseSharpness(most) - 2.6f) < 1e-4f);
    CHECK(registry().execute(doc, "subdiv_level", {{"level", 3.0}}).ok);
    bool same = doc.mesh.positions.size() == top.size();
    for (size_t v = 0; same && v < top.size(); ++v) same = glm::length(doc.mesh.positions[v] - top[v]) < 1e-4f;
    CHECK(same);
}

TEST(edge_crease_usd_export_import) {
    // A creased cage: USD creaseIndices / creaseLengths / creaseSharpnesses (sharpness 10 c^2), Catmull-Clark
    // scheme; read back onto both sides of each edge. A plain mesh keeps subdivisionScheme "none" and no creases.
    Document doc = makeDoc(makeCube(1));
    CHECK(selectEdge(doc, A, B) && selectEdge(doc, B, C));
    CHECK(registry().execute(doc, "edge_crease", {{"value", 1.0}}).ok);
    CHECK(registry().execute(doc, "edge_select", {{"op", std::string("none")}}).ok);
    CHECK(selectEdge(doc, C, D));
    CHECK(registry().execute(doc, "edge_crease", {{"value", 0.5}}).ok);
    std::string err;
    const std::string path = tempPath("edge_crease.usda");
    CHECK(saveUsd(path, &doc.mesh, nullptr, "Cage", err));
    std::string text;
    {
        std::ifstream f(path);
        text.assign(std::istreambuf_iterator<char>(f), {});
    }
    CHECK(text.find("int[] creaseLengths = [2, 2, 2]") != std::string::npos);
    CHECK(text.find("float[] creaseSharpnesses") != std::string::npos);
    CHECK(text.find("subdivisionScheme = \"catmullClark\"") != std::string::npos);
    Mesh back;
    SplatCloud splats;
    CHECK(loadUsd(path, back, splats, err));
    const auto topo = buildEdgeTopology(back);
    const auto own = buildEdgeTopology(doc.mesh);
    CHECK(edgeCreasedCount(back, *topo) == 3);
    size_t sharp = 0, half = 0;
    for (uint32_t e = 0; e < topo->edgeCount(); ++e) {
        const float c = edgeCrease(back, *topo, e);
        sharp += std::fabs(c - 1.0f) < 1e-5f;
        half += std::fabs(c - 0.5f) < 1e-5f;
    }
    CHECK(sharp == 2 && half == 1);
    // Both sides of each creased edge, as Crease sets them.
    size_t corners = 0;
    for (float c : back.creases) corners += c > 0.0f;
    CHECK(corners == 6);
    CHECK(edgeCreasedCount(doc.mesh, *own) == 3);

    // At Level 2 (of 3) the level's edges carry what is left of the creases below: each creased edge's two halves.
    CHECK(registry().execute(doc, "subdiv_divide", {}).ok);
    CHECK(registry().execute(doc, "subdiv_divide", {}).ok);
    CHECK(registry().execute(doc, "subdiv_level", {{"level", 2.0}}).ok);
    CHECK(saveUsd(path, &doc.mesh, nullptr, "Level", err));
    Mesh level;
    CHECK(loadUsd(path, level, splats, err));
    CHECK(edgeCreasedCount(level, *buildEdgeTopology(level)) == 6);

    // Hand-written chains: one 3-point crease with one sharpness, and per-edge sharpness; "none" without creases.
    {
        std::ofstream f(path);
        f << R"(#usda 1.0
def Mesh "Quads"
{
    int[] faceVertexCounts = [4, 4]
    int[] faceVertexIndices = [0, 1, 4, 3, 1, 2, 5, 4]
    point3f[] points = [(0,0,0), (1,0,0), (2,0,0), (0,1,0), (1,1,0), (2,1,0)]
    int[] creaseIndices = [0, 1, 2, 1, 4]
    int[] creaseLengths = [3, 2]
    float[] creaseSharpnesses = [10, 2.5]
}
)";
    }
    Mesh chain;
    CHECK(loadUsd(path, chain, splats, err));
    const auto ct = buildEdgeTopology(chain);
    CHECK(edgeCreasedCount(chain, *ct) == 3);
    std::filesystem::remove(path);
    Mesh plainCube = makeCube(1);
    CHECK(saveUsd(path, &plainCube, nullptr, "Plain", err));
    {
        std::ifstream f(path);
        text.assign(std::istreambuf_iterator<char>(f), {});
    }
    CHECK(text.find("crease") == std::string::npos);
    CHECK(text.find("subdivisionScheme = \"none\"") != std::string::npos);
    std::filesystem::remove(path);
}

TEST(edge_crease_usd_hostile_files) {
    // Two quads (6 points). Each malformed crease set is skipped whole with a warning naming the prim; the mesh
    // still opens. Huge finite sharpness clamps to 10 (crease 1); negative sharpness is no crease, no warning.
    const std::string path = tempPath("edge_crease_hostile.usda");
    auto load = [&](const std::string& creases, Mesh& out, std::string& warnings) {
        {
            std::ofstream f(path);
            f << "#usda 1.0\ndef Mesh \"Quads\"\n{\n    int[] faceVertexCounts = [4, 4]\n"
                 "    int[] faceVertexIndices = [0, 1, 4, 3, 1, 2, 5, 4]\n"
                 "    point3f[] points = [(0,0,0), (1,0,0), (2,0,0), (0,1,0), (1,1,0), (2,1,0)]\n"
              << creases << "}\n";
        }
        SplatCloud splats;
        std::string err;
        warnings.clear();
        const bool ok = loadUsd(path, out, splats, err, &warnings);
        return ok && out.triangleCount() == 4;
    };
    struct Case {
        const char* what;
        std::string attrs;
    };
    std::string many = "    int[] creaseIndices = [";
    for (int i = 0; i < 200; ++i) many += i ? (i % 2 ? ", 1" : ", 0") : "0";
    many += "]\n    int[] creaseLengths = [200]\n    float[] creaseSharpnesses = [1]\n";
    const Case hostile[] = {
        {"lengths add up to more", "int[] creaseIndices = [0, 1, 2]\nint[] creaseLengths = [4]\nfloat[] creaseSharpnesses = [1]\n"},
        {"lengths add up to less", "int[] creaseIndices = [0, 1, 2, 5]\nint[] creaseLengths = [2]\nfloat[] creaseSharpnesses = [1]\n"},
        {"negative index", "int[] creaseIndices = [-1, 1]\nint[] creaseLengths = [2]\nfloat[] creaseSharpnesses = [1]\n"},
        {"index past the points", "int[] creaseIndices = [0, 6]\nint[] creaseLengths = [2]\nfloat[] creaseSharpnesses = [1]\n"},
        {"huge index", "int[] creaseIndices = [0, 1e300]\nint[] creaseLengths = [2]\nfloat[] creaseSharpnesses = [1]\n"},
        {"fractional index", "int[] creaseIndices = [0, 1.5]\nint[] creaseLengths = [2]\nfloat[] creaseSharpnesses = [1]\n"},
        {"length 1", "int[] creaseIndices = [0]\nint[] creaseLengths = [1]\nfloat[] creaseSharpnesses = [1]\n"},
        {"negative length", "int[] creaseIndices = [0, 1]\nint[] creaseLengths = [-2]\nfloat[] creaseSharpnesses = [1]\n"},
        {"huge length", "int[] creaseIndices = [0, 1]\nint[] creaseLengths = [1e18]\nfloat[] creaseSharpnesses = [1]\n"},
        {"lengths that overflow", "int[] creaseIndices = [0, 1, 2]\nint[] creaseLengths = [2, 18446744073709551615]\nfloat[] creaseSharpnesses = [1, 1]\n"},
        {"sharpness count", "int[] creaseIndices = [0, 1, 1, 4]\nint[] creaseLengths = [2, 2]\nfloat[] creaseSharpnesses = [1, 2, 3]\n"},
        {"infinite sharpness", "int[] creaseIndices = [0, 1]\nint[] creaseLengths = [2]\nfloat[] creaseSharpnesses = [1e999]\n"},
        {"missing lengths", "int[] creaseIndices = [0, 1]\nfloat[] creaseSharpnesses = [1]\n"},
        {"too many indices", many},
    };
    for (const Case& c : hostile) {
        Mesh m;
        std::string warnings;
        const bool opened = load(c.attrs, m, warnings);
        CHECK(opened);
        CHECK(!m.hasCreases());
        const bool warned = warnings.find("Edge creases of /Quads skipped") != std::string::npos;
        CHECK(warned);
        if (!opened || m.hasCreases() || !warned) std::printf("  hostile case: %s (%s)\n", c.what, warnings.c_str());
    }
    // Clamped, not refused: 1e30 -> sharpness 10 (crease 1); -5 -> no crease; an edge the faces don't have
    // (0-2) and a repeated edge (the second, softer one loses) leave no trace.
    Mesh m;
    std::string warnings;
    CHECK(load("int[] creaseIndices = [0, 1, 1, 0, 1, 4, 0, 2]\nint[] creaseLengths = [2, 2, 2, 2]\n"
               "float[] creaseSharpnesses = [1e30, 2.5, -5, 10]\n", m, warnings));
    CHECK(warnings.empty());
    const auto topo = buildEdgeTopology(m);
    CHECK(edgeCreasedCount(m, *topo) == 1);
    float most = 0.0f;
    for (float c : m.creases) most = std::max(most, c);
    CHECK(most == 1.0f);

    // Two prims merged into one mesh: the second prim's crease indices are its own points (offset past the first's).
    {
        std::ofstream f(path);
        f << R"(#usda 1.0
def Mesh "A"
{
    int[] faceVertexCounts = [4]
    int[] faceVertexIndices = [0, 1, 2, 3]
    point3f[] points = [(0,0,0), (1,0,0), (1,1,0), (0,1,0)]
}
def Mesh "B"
{
    int[] faceVertexCounts = [4]
    int[] faceVertexIndices = [0, 1, 2, 3]
    point3f[] points = [(5,0,0), (6,0,0), (6,1,0), (5,1,0)]
    int[] creaseIndices = [0, 1]
    int[] creaseLengths = [2]
    float[] creaseSharpnesses = [10]
}
)";
    }
    Mesh two;
    SplatCloud splats;
    std::string err;
    warnings.clear();
    CHECK(loadUsd(path, two, splats, err, &warnings) && warnings.empty());
    CHECK(two.creases.size() == two.triangleCount() * 3);
    // (Positions may be scaled to metres: compare against the merged mesh's width. B's bottom edge: both ends at
    // more than 0.8 of the widest x, the same y, and the lowest y.)
    const AABB box = two.bounds();
    size_t onB = 0, elsewhere = 0;
    for (size_t c = 0; c < two.creases.size(); ++c) {
        if (two.creases[c] <= 0.0f) continue;
        const glm::uvec3& t = two.triangles[c / 3];
        const glm::vec3 a = two.positions[t[int(c % 3)]], b = two.positions[t[int((c + 1) % 3)]];
        const float wide = box.min.x + (box.max.x - box.min.x) * 0.8f;
        const bool edge01 = a.x > wide && b.x > wide && a.y == b.y && std::fabs(a.y - box.min.y) < 1e-6f;
        (edge01 ? onB : elsewhere)++;
    }
    CHECK(onB == 1 && elsewhere == 0);
    std::filesystem::remove(path);
}
