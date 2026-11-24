// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The benchmark scene pack (core/BenchPack.h, bench/pack/pack.json): the manifest's checks, the hash a result carries,
// and that the repository's own manifest names triangle counts the Creature Head really divides to.
#include "TestHarness.h"

#include "core/BenchPack.h"
#include "core/Sha256.h"
#include "core/StarterMeshes.h"

#include <fstream>
#include <iterator>
#include <string>

using namespace sf;
namespace fs = std::filesystem;

namespace {

std::string slurp(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
}

void spit(const fs::path& p, const std::string& s) {
    std::ofstream f(p, std::ios::binary);
    f << s;
}

// A fresh pack folder: the Creature Head project and a manifest with `scenes` (JSON text of the array).
fs::path makePack(const char* name, const std::string& scenes, const std::string& schema = kBenchPackSchema) {
    const fs::path dir = fs::path(sf::test::tempPath(name));
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);
    std::string error;
    CHECK(writeStarterMeshes(dir.string(), error));
    spit(dir / "pack.json", "{\"schema\":\"" + schema + "\",\"name\":\"test pack\",\"version\":1,\"stroke_set\":\"pack/1\",\"scenes\":" + scenes + "}");
    return dir;
}

std::string scene(const std::string& id, const std::string& file, int divide, const std::string& tier, double triangles) {
    return "{\"id\":\"" + id + "\",\"title\":\"t\",\"file\":\"" + file + "\",\"divide\":" + std::to_string(divide) +
           ",\"tier\":\"" + tier + "\",\"triangles\":" + std::to_string(size_t(triangles)) + "}";
}

fs::path repoPack() { return fs::path(SF_TEST_DATA_DIR) / ".." / ".." / "bench" / "pack"; }

constexpr double kCreatureTriangles = 5632; // the Creature Head's four Parts

} // namespace

TEST(bench_pack_builds_a_scene_and_hashes_the_files) {
    const fs::path dir = makePack("sf-bench-pack-ok", "[" + scene("creature-small", "creature-head.sfproj", 2, "floor", kCreatureTriangles * 16) + "]");
    BenchPack pack;
    std::string error;
    CHECK(loadBenchPack(dir.string(), pack, error));
    CHECK(error.empty());
    CHECK(pack.scenes.size() == 1 && pack.scenes[0].tier == "floor" && pack.strokeSet == "pack/1");
    CHECK(pack.sha256 == sha256Hex(slurp(dir / "pack.json") + slurp(dir / "creature-head.sfproj"))); // what make_pack.py computes too
    Mesh a, b;
    CHECK(buildBenchPackScene(pack, pack.scenes[0], a, error));
    CHECK(a.triangleCount() == size_t(kCreatureTriangles) * 16);
    CHECK(buildBenchPackScene(pack, pack.scenes[0], b, error)); // the same mesh on every run
    CHECK(a.positions == b.positions);
    spit(dir / "creature-head.sfproj", slurp(dir / "creature-head.sfproj") + " "); // any change to a file changes the hash
    BenchPack changed;
    if (loadBenchPack(dir.string(), changed, error)) CHECK(changed.sha256 != pack.sha256);
}

TEST(bench_pack_refuses_a_wrong_triangle_count_and_bad_manifests) {
    std::string error;
    BenchPack pack;
    const fs::path wrong = makePack("sf-bench-pack-count", "[" + scene("x", "creature-head.sfproj", 1, "floor", 1234) + "]");
    CHECK(loadBenchPack(wrong.string(), pack, error));
    Mesh m;
    CHECK(!buildBenchPackScene(pack, pack.scenes[0], m, error));
    CHECK(error.find("manifest says 1234") != std::string::npos);

    const fs::path ok = makePack("sf-bench-pack-bad", "[" + scene("x", "creature-head.sfproj", 1, "floor", kCreatureTriangles * 4) + "]");
    auto refused = [&](const std::string& scenes, const char* what) {
        spit(ok / "pack.json", std::string("{\"schema\":\"") + kBenchPackSchema + "\",\"name\":\"p\",\"version\":1,\"stroke_set\":\"pack/1\",\"scenes\":" + scenes + "}");
        BenchPack p;
        std::string e;
        const bool loaded = loadBenchPack(ok.string(), p, e);
        if (loaded) std::printf("  not refused: %s\n", what);
        CHECK(!loaded);
        CHECK(!e.empty());
    };
    refused("[]", "no scenes");
    refused("[" + scene("x", "../creature-head.sfproj", 1, "floor", 22528) + "]", "a path out of the folder");
    refused("[" + scene("x", "sub/creature-head.sfproj", 1, "floor", 22528) + "]", "a subfolder");
    refused("[" + scene("x", "missing.sfproj", 1, "floor", 22528) + "]", "a missing file");
    refused("[" + scene("x", "pack.json", 1, "floor", 22528) + "]", "not a project file");
    refused("[" + scene("x", "creature-head.sfproj", 1, "tiny", 22528) + "]", "an unknown tier");
    refused("[" + scene("x", "creature-head.sfproj", 9, "floor", 22528) + "]", "too many levels");
    refused("[" + scene("X Y", "creature-head.sfproj", 1, "floor", 22528) + "]", "an id with a space");
    refused("[" + scene("x", "creature-head.sfproj", 1, "floor", 22528) + "," + scene("x", "creature-head.sfproj", 2, "high", 90112) + "]", "a duplicate id");
    spit(ok / "pack.json", "{\"schema\":\"other/1\",\"name\":\"p\",\"version\":1,\"stroke_set\":\"pack/1\",\"scenes\":[]}");
    BenchPack p;
    CHECK(!loadBenchPack(ok.string(), p, error));
    CHECK(!loadBenchPack(sf::test::tempPath("sf-bench-pack-nothing-here"), p, error));
    CHECK(error.find("pack.json") != std::string::npos);
}

TEST(bench_pack_repository_manifest_has_a_floor_scene_and_exact_counts) {
    // The manifest in the repository, with the starter's own project beside it.
    const fs::path dir = fs::path(sf::test::tempPath("sf-bench-pack-repo"));
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);
    std::string error;
    CHECK(writeStarterMeshes(dir.string(), error));
    fs::copy_file(repoPack() / "pack.json", dir / "pack.json");
    BenchPack pack;
    CHECK(loadBenchPack(dir.string(), pack, error));
    CHECK(error.empty());
    bool floor = false;
    double last = 0;
    for (const BenchPackScene& s : pack.scenes) {
        floor = floor || s.tier == "floor";
        CHECK(s.triangles == kCreatureTriangles * double(size_t(1) << (2 * s.divide))); // each divide is four times
        CHECK(s.triangles > last); // smallest first: a computer that stops early has the entry level
        last = s.triangles;
    }
    CHECK(floor);
    // The smallest scene, built for real: the counts above are what the app divides to.
    Mesh m;
    BenchPackScene probe = pack.scenes[0];
    probe.divide = 2;
    probe.triangles = kCreatureTriangles * 16;
    CHECK(buildBenchPackScene(pack, probe, m, error));
}
