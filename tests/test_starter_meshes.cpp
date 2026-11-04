// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Starter meshes (core/StarterMeshes.h): every one unpacks and loads, is all quads, edge-manifold (closed where
// it should be), mirror-symmetric where it should be, centred and standing on the ground at a life-like size;
// the set stays inside its size budget; new_starter_mesh adds the subdivision levels and leaves no path.
#include "TestHarness.h"

#include "core/Commands.h"
#include "core/Multires.h"
#include "core/Project.h"
#include "core/StarterMeshes.h"

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <map>
#include <set>
#include <tuple>

using namespace sf;

namespace {

Document load(const StarterMesh& m, int levels) {
    Document d;
    std::string error;
    CHECK(loadStarterMesh(m, d, levels, error));
    if (!error.empty()) std::printf("  %s: %s\n", m.id, error.c_str());
    return d;
}

} // namespace

TEST(starter_meshes_set_and_budget) {
    const auto& all = starterMeshes();
    CHECK(all.size() == 14);
    size_t total = 0;
    std::set<std::string> ids;
    for (const StarterMesh& m : all) {
        total += m.size;
        CHECK(ids.insert(m.id).second);
        CHECK(findStarterMesh(m.id) == &m);
        CHECK(findStarterMesh(m.name) == &m);
        CHECK(m.levels >= 1 && m.levels <= 3);
    }
    CHECK(findStarterMesh("head") == findStarterMesh("human-head"));
    CHECK(findStarterMesh("zsphere") == nullptr);
    CHECK(total < size_t(15) * 1000 * 1000); // the brief's budget; the set is about 1 MB
    std::printf("  starter meshes: %zu bytes compressed\n", total);
}

TEST(starter_meshes_topology_symmetry_and_placement) {
    for (const StarterMesh& m : starterMeshes()) {
        Document d = load(m, 0);
        CHECK(!d.subtools.empty());
        AABB box;
        // Mirror check: every vertex of every Part has a partner at (-x, y, z) within 0.01 mm.
        std::set<std::tuple<long, long, long>> points;
        auto key = [](glm::vec3 p) {
            return std::make_tuple(std::lround(p.x * 1000.0f), std::lround(p.y * 1000.0f), std::lround(p.z * 1000.0f));
        };
        for (int i = 0; i < int(d.subtools.size()); ++i) {
            const Mesh& mesh = d.subtoolMesh(i);
            CHECK(mesh.vertexCount() > 100);
            // All quads: every triangle is paired.
            CHECK(mesh.quadPartners.size() == mesh.triangleCount());
            size_t unpaired = 0;
            for (uint32_t q : mesh.quadPartners) unpaired += q == Mesh::kNoQuad;
            CHECK(unpaired == 0);
            std::map<std::pair<uint32_t, uint32_t>, int> edges;
            for (const glm::uvec3& t : mesh.triangles)
                for (int k = 0; k < 3; ++k) {
                    const uint32_t a = t[k], b = t[(k + 1) % 3];
                    ++edges[{std::min(a, b), std::max(a, b)}];
                }
            size_t open = 0, nonManifold = 0;
            for (const auto& edge : edges) {
                open += edge.second == 1;
                nonManifold += edge.second > 2;
            }
            CHECK(nonManifold == 0);
            if (m.closed) CHECK(open == 0);
            else CHECK(open > 0);
            for (const glm::vec3& p : mesh.positions) {
                box.expand(p);
                points.insert(key(p));
            }
        }
        if (m.symmetric) {
            size_t missing = 0;
            for (int i = 0; i < int(d.subtools.size()); ++i)
                for (const glm::vec3& p : d.subtoolMesh(i).positions) {
                    const auto [x, y, z] = key(glm::vec3(-p.x, p.y, p.z));
                    bool found = false;
                    for (long dx = -1; dx <= 1 && !found; ++dx)
                        for (long dy = -1; dy <= 1 && !found; ++dy)
                            for (long dz = -1; dz <= 1 && !found; ++dz)
                                found = points.count({x + dx, y + dy, z + dz}) > 0;
                    missing += !found;
                }
            if (missing) std::printf("  %s: %zu vertices without a mirror partner\n", m.id, missing);
            CHECK(missing == 0);
        }
        // Standing on the ground, centred, between 2 cm (the eye) and 2 m (the figure).
        CHECK(std::abs(box.min.y) < 0.01f);
        CHECK(std::abs(box.min.x + box.max.x) < 0.01f);
        CHECK(std::abs(box.min.z + box.max.z) < 0.01f);
        const glm::vec3 size = box.max - box.min;
        const float longest = std::max({size.x, size.y, size.z});
        CHECK(longest > 2.0f && longest < 200.0f);
        CHECK(d.path.empty());
    }
}

TEST(starter_meshes_open_with_levels) {
    CommandRegistry registry;
    Document d;
    CommandResult r = registry.execute(d, "new_starter_mesh", {{"name", std::string("human-head")}});
    CHECK(r.ok);
    CHECK(subdivLevelCount(d.mesh) == 3);
    CHECK(subdivCurrentLevel(d.mesh) == 2);
    CHECK(d.mesh.vertexCount() > 60000);
    CHECK(d.path.empty());
    CHECK(d.subtools.size() == 1 && d.subtools[0].name == "Human Head");
    // Multi-Part: every Part gets its levels.
    r = registry.execute(d, "new_starter_mesh", {{"name", std::string("Creature Head")}, {"levels", 1.0}});
    CHECK(r.ok);
    CHECK(d.subtools.size() == 4);
    for (int i = 0; i < int(d.subtools.size()); ++i) CHECK(subdivLevelCount(d.subtoolMesh(i)) == 2);
    CHECK(!registry.execute(d, "new_starter_mesh", {{"name", std::string("no-such-mesh")}}).ok);
    CHECK(d.subtools.size() == 4); // a failure leaves the scene alone
}

TEST(starter_meshes_unpack_to_folder) {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "sculptforge-test-starter-meshes";
    std::error_code ec;
    fs::remove_all(dir, ec);
    std::string error;
    CHECK(writeStarterMeshes(dir.string(), error));
    const fs::path head = dir / "human-head.sfproj";
    CHECK(fs::exists(head));
    const auto written = fs::last_write_time(head, ec);
    CHECK(writeStarterMeshes(dir.string(), error)); // identical files stay as they are (cached thumbnails)
    CHECK(fs::last_write_time(head, ec) == written);
    CHECK(starterMeshForPath(dir.string(), head.string()) == findStarterMesh("human-head"));
    CHECK(starterMeshForPath(dir.string(), (dir / "other.sfproj").string()) == nullptr);
    CHECK(starterMeshForPath((dir / "x").string(), head.string()) == nullptr);
    Document d;
    CHECK(loadProject(head.string(), d, error)); // a plain project the thumbnails and Open read
    CHECK(d.mesh.vertexCount() > 1000);
    size_t files = 0;
    for (const auto& e : fs::directory_iterator(dir)) files += e.path().extension() == ".sfproj";
    CHECK(files == starterMeshes().size());
    fs::remove_all(dir, ec);
}
