// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Sandboxed plugins (core/Plugin.h, #20): the manifest rules, the module reader against malformed and hostile
// modules (imports, components, missing exports, random damage), the host ABI against plugins that loop, trap,
// allocate too much, read out of bounds or return broken results, the sample Noise Displace plugin, packs that
// carry plugins (review, install, run, undo, turned off after a failure), and the call overhead on a 1M-vertex mesh.
#include "TestHarness.h"

#include "core/Commands.h"
#include "core/Document.h"
#include "core/Macro.h"
#include "core/Pack.h"
#include "core/Plugin.h"
#include "core/Primitives.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

using namespace sf;
namespace fs = std::filesystem;

namespace {

bool contains(const std::string& s, const std::string& part) { return s.find(part) != std::string::npos; }

std::string readText(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    std::stringstream s;
    s << f.rdbuf();
    return s.str();
}

std::string dataDir() { return std::string(SF_TEST_DATA_DIR) + "/plugins/"; }

std::string manifestJson(const std::string& id, const char* kind = "deformer", const std::string& params = "") {
    return "{\"format\":\"sculptforge-plugin\",\"formatVersion\":1,\"id\":\"" + id + "\",\"name\":\"" + id +
           "\",\"version\":\"1.0\",\"author\":\"Tests\",\"license\":\"CC0-1.0\",\"kind\":\"" + kind + "\",\"params\":[" +
           params + "]}";
}

// The allocator every test plugin shares (the sample's), and a module around an sf_run.
const char* kAlloc = R"((global $heap (mut i32) (i32.const 1024))
  (func (export "sf_alloc") (param $n i32) (result i32)
    (local $p i32) (local $end i32) (local $need i32)
    (local.set $p (i32.and (i32.add (global.get $heap) (i32.const 7)) (i32.const -8)))
    (local.set $end (i32.add (local.get $p) (local.get $n)))
    (if (i32.or (i32.lt_u (local.get $end) (local.get $p)) (i32.gt_u (local.get $end) (i32.const 0xFFFF0000)))
      (then (return (i32.const 0))))
    (local.set $need (i32.sub (i32.shr_u (i32.add (local.get $end) (i32.const 65535)) (i32.const 16)) (memory.size)))
    (if (i32.gt_s (local.get $need) (i32.const 0))
      (then (if (i32.eq (memory.grow (local.get $need)) (i32.const -1)) (then (return (i32.const 0))))))
    (global.set $heap (local.get $end))
    (local.get $p)))";

// Writes the output record at 16 and returns it: abi 1, STATUS, VC, TC, positions POS, triangles TRI.
std::string output(const std::string& status, const std::string& vc, const std::string& tc, const std::string& pos,
                   const std::string& tri) {
    return "(i32.store (i32.const 16) (i32.const 1)) (i32.store (i32.const 20) " + status + ") (i32.store (i32.const 24) " +
           vc + ") (i32.store (i32.const 28) " + tc + ") (i32.store (i32.const 32) " + pos + ") (i32.store (i32.const 36) " +
           tri + ") (i32.store (i32.const 40) (i32.const 0)) (i32.store (i32.const 44) (i32.const 0)) (i32.const 16)";
}
// The identity deformer's result: the input positions as they are.
const std::string kSame = output("(i32.const 0)", "(i32.load offset=8 (local.get $in))", "(i32.const 0)",
                                 "(i32.load offset=20 (local.get $in))", "(i32.const 0)");

std::vector<uint8_t> wat(const std::string& text) {
    std::vector<uint8_t> out;
    std::string err;
    if (!pluginWatToWasm(text, out, err)) std::printf("WAT error: %s\n", err.c_str());
    return out;
}

// A plugin whose sf_run is BODY (with (param $in i32) (result i32)); EXTRA goes in the module too.
std::vector<uint8_t> plugin(const std::string& body, const char* kind = "deformer", const std::string& extra = "",
                            const std::string& id = "test-plugin") {
    return pluginWithManifest(wat("(module (memory (export \"memory\") 1) " + std::string(kAlloc) + extra +
                                  " (func (export \"sf_run\") (param $in i32) (result i32) " + body + "))"),
                              manifestJson(id, kind));
}

std::vector<uint8_t> noisePlugin() {
    return pluginWithManifest(wat(readText(dataDir() + "noise_displace.wat")), readText(dataDir() + "noise_displace.json"));
}

PluginFailure run(const std::vector<uint8_t>& wasm, const PluginInput& in, PluginOutput& out, std::string& err,
                  const PluginLimits& limits = {}, std::vector<double> params = {}, const std::atomic<bool>* cancel = nullptr) {
    PluginManifest m;
    if (!readPlugin(wasm, m, err)) return PluginFailure::BadModule;
    PluginRunOptions o;
    o.limits = limits;
    o.params = std::move(params);
    o.cancel = cancel;
    return runPlugin(wasm, m, in, o, out, err);
}

PluginInput sphereInput() {
    Mesh m = makeIcosphere(3);
    return pluginInputFrom(m);
}

struct TempPacks {
    fs::path dir;
    explicit TempPacks(const char* name) : dir(fs::temp_directory_path() / name) {
        std::error_code ec;
        fs::remove_all(dir, ec);
        fs::create_directories(dir);
        setPacksRoot((dir / "packs").string());
    }
    ~TempPacks() {
        setPacksRoot("");
        std::error_code ec;
        fs::remove_all(dir, ec);
    }
};

} // namespace

TEST(plugin_manifest_rules) {
    PluginManifest m;
    std::string err;
    CHECK(parsePluginManifest(readText(dataDir() + "noise_displace.json"), m, err));
    CHECK(m.id == "noise-displace" && m.kind == PluginKind::Deformer && m.params.size() == 3);
    CHECK(m.params[2].type == PluginParam::Type::Int && m.params[0].def == 0.05);
    CHECK(parsePluginManifest(manifestJson("gen", "generator"), m, err) && m.kind == PluginKind::Generator);
    auto refused = [&](const std::string& json, const char* why) {
        PluginManifest x;
        std::string e;
        const bool ok = parsePluginManifest(json, x, e);
        if (ok || !contains(e, why)) std::printf("manifest not refused for \"%s\": %s\n", why, e.c_str());
        return !ok && contains(e, why);
    };
    std::string base = manifestJson("p");
    base.pop_back();
    CHECK(refused(base + ",\"permissions\":[\"files\"]}", "permission \"files\""));
    CHECK(refused(base + ",\"network\":true}", "unknown key"));
    CHECK(refused(manifestJson("p", "brush"), "\"kind\""));
    CHECK(refused(manifestJson("Bad Id"), "\"id\""));
    CHECK(refused(manifestJson("p", "deformer", R"({"id":"a","label":"A","type":"number","min":0,"max":1,"default":2})"),
                  "min <= default <= max"));
    CHECK(refused(manifestJson("p", "deformer",
                               R"({"id":"a","label":"A","type":"bool"},{"id":"a","label":"B","type":"bool"})"),
                  "used twice"));
    CHECK(refused(manifestJson("p", "deformer", R"({"id":"a","label":"A","type":"string"})"), "number, int or bool"));
    std::string v2 = manifestJson("p");
    v2.replace(v2.find("\"formatVersion\":1"), 17, "\"formatVersion\":2");
    CHECK(refused(v2, "formatVersion"));
    CHECK(refused("{\"format\":\"sculptforge-plugin\",\"format\":\"x\"}", "valid JSON"));
}

TEST(plugin_reader_refuses_bad_modules) {
    PluginManifest m;
    std::string err;
    CHECK(!readPlugin({}, m, err) && contains(err, "not a WebAssembly"));
    CHECK(!readPlugin({'P', 'K', 3, 4, 0, 0, 0, 0}, m, err) && contains(err, "not a WebAssembly"));
    // A component (layer 1) is not a core module.
    CHECK(!readPlugin({0, 'a', 's', 'm', 0x0d, 0, 1, 0}, m, err) && contains(err, "core module"));
    // A section that runs past the end.
    CHECK(!readPlugin({0, 'a', 's', 'm', 1, 0, 0, 0, 1, 0x7f}, m, err) && contains(err, "damaged"));
    // An over-long LEB128 size.
    CHECK(!readPlugin({0, 'a', 's', 'm', 1, 0, 0, 0, 1, 0xff, 0xff, 0xff, 0xff, 0x7f}, m, err) && contains(err, "damaged"));
    // Too big.
    std::vector<uint8_t> big(kMaxPluginBytes + 1, 0);
    CHECK(!readPlugin(big, m, err) && contains(err, "too big"));
    if (!pluginRuntimeAvailable()) return;
    // No manifest; two manifests.
    const std::vector<uint8_t> bare = wat("(module (memory (export \"memory\") 1) " + std::string(kAlloc) +
                                          " (func (export \"sf_run\") (param $in i32) (result i32) (i32.const 0)))");
    CHECK(!bare.empty());
    CHECK(!readPlugin(bare, m, err) && contains(err, "no manifest"));
    CHECK(!readPlugin(pluginWithManifest(pluginWithManifest(bare, manifestJson("a")), manifestJson("b")), m, err) &&
          contains(err, "more than one"));
    CHECK(readPlugin(pluginWithManifest(bare, manifestJson("a")), m, err));
    // Imports of any kind: WASI, a host function, a memory.
    for (const char* imp : {R"((import "wasi_snapshot_preview1" "fd_write" (func (param i32 i32 i32 i32) (result i32))))",
                            R"((import "env" "now" (func (result f64))))"}) {
        const auto w = pluginWithManifest(wat("(module " + std::string(imp) + " (memory (export \"memory\") 1) " + kAlloc +
                                              " (func (export \"sf_run\") (param $in i32) (result i32) (i32.const 0)))"),
                                          manifestJson("a"));
        CHECK(!readPlugin(w, m, err) && contains(err, "can't import"));
    }
    const auto importedMemory = pluginWithManifest(
        wat(R"((module (import "env" "memory" (memory 1)) (export "memory" (memory 0)) )" + std::string(kAlloc) +
            R"( (func (export "sf_run") (param $in i32) (result i32) (i32.const 0))))"),
        manifestJson("a"));
    CHECK(!readPlugin(importedMemory, m, err) && contains(err, "env.memory"));
    // Missing exports.
    const auto noRun = pluginWithManifest(wat("(module (memory (export \"memory\") 1) " + std::string(kAlloc) + ")"),
                                          manifestJson("a"));
    CHECK(!readPlugin(noRun, m, err) && contains(err, "sf_run"));
    const auto noMemory = pluginWithManifest(
        wat("(module (memory 1) " + std::string(kAlloc) + " (func (export \"sf_run\") (param i32) (result i32) (i32.const 0)))"),
        manifestJson("a"));
    CHECK(!readPlugin(noMemory, m, err) && contains(err, "memory"));
}

TEST(plugin_noise_displacer_sample) {
    if (!pluginRuntimeAvailable()) return;
    const auto wasm = noisePlugin();
    PluginManifest m;
    std::string err;
    CHECK(readPlugin(wasm, m, err));
    CHECK(m.name == "Noise Displace");
    const PluginInput in = sphereInput();
    PluginOutput a, b, c;
    CHECK(run(wasm, in, a, err, {}, {0.1, 4.0, 1.0}) == PluginFailure::None);
    CHECK(a.positions.size() == in.positions.size() && a.triangles.empty());
    double moved = 0.0, most = 0.0;
    for (size_t i = 0; i < in.positions.size(); ++i) {
        const glm::vec3 d = a.positions[i] - in.positions[i];
        const double len = std::sqrt(double(d.x * d.x + d.y * d.y + d.z * d.z));
        moved += len;
        most = std::max(most, len);
        // Along the normal only.
        const glm::vec3 n = in.normals[i];
        const glm::vec3 across = d - n * (d.x * n.x + d.y * n.y + d.z * n.z);
        CHECK(std::sqrt(across.x * across.x + across.y * across.y + across.z * across.z) < 1e-4f);
    }
    CHECK(moved > 0.01 * in.positions.size()); // it does something
    CHECK(most <= 0.1 + 1e-4);                 // within the amount
    // Deterministic; another seed gives another surface; the default parameters are used when none are given.
    CHECK(run(wasm, in, b, err, {}, {0.1, 4.0, 1.0}) == PluginFailure::None && b.positions == a.positions);
    CHECK(run(wasm, in, c, err, {}, {0.1, 4.0, 2.0}) == PluginFailure::None && c.positions != a.positions);
    CHECK(run(wasm, in, c, err) == PluginFailure::None);
    // Out-of-range parameters are clamped (amount 1 at most).
    CHECK(run(wasm, in, c, err, {}, {50.0, 4.0, 1.0}) == PluginFailure::None);
    for (size_t i = 0; i < in.positions.size(); ++i) {
        const glm::vec3 d = c.positions[i] - in.positions[i];
        CHECK(std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z) <= 1.0f + 1e-4f);
    }
}

TEST(plugin_loops_are_stopped) {
    if (!pluginRuntimeAvailable()) return;
    const PluginInput in = sphereInput();
    PluginOutput out;
    std::string err;
    const auto loop = plugin("(loop $l (br $l)) (i32.const 0)");
    PluginLimits time;
    time.seconds = 0.3;
    time.fuel = 1ull << 50;
    const auto t0 = std::chrono::steady_clock::now();
    CHECK(run(loop, in, out, err, time) == PluginFailure::Timeout);
    const double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    CHECK(s < 3.0);
    CHECK(contains(err, "past its time"));
    CHECK(out.positions.empty());
    PluginLimits work;
    work.fuel = 1000000;
    CHECK(run(loop, in, out, err, work) == PluginFailure::OutOfFuel);
    CHECK(pluginFailureDisables(PluginFailure::Timeout) && pluginFailureDisables(PluginFailure::OutOfFuel));
    // A start function that never returns is stopped the same way (instantiating runs it).
    const auto start = plugin("(i32.const 0)", "deformer", " (func $forever (loop $l (br $l))) (start $forever)");
    CHECK(run(start, in, out, err, time) == PluginFailure::Timeout);
    // Deep recursion: a stack overflow trap, not a crash.
    const auto deep = plugin("(call $down (i32.const 0))", "deformer",
                             " (func $down (param i32) (result i32) (call $down (i32.add (local.get 0) (i32.const 1))))");
    CHECK(run(deep, in, out, err) == PluginFailure::Trap);
    CHECK(contains(err, "stack overflow"));
    // The user's Stop.
    std::atomic<bool> cancel{true};
    PluginLimits slow;
    slow.fuel = 1ull << 50;
    CHECK(run(loop, in, out, err, slow, {}, &cancel) == PluginFailure::Cancelled);
}

TEST(plugin_memory_is_capped) {
    if (!pluginRuntimeAvailable()) return;
    const PluginInput in = sphereInput();
    PluginOutput out;
    std::string err;
    PluginLimits cap;
    cap.memoryBytes = 64ull << 20;
    // Growing past the cap fails (-1); this plugin then traps.
    const auto grow = plugin("(if (i32.eq (memory.grow (i32.const 2000)) (i32.const -1)) (then unreachable)) (i32.const 0)");
    CHECK(run(grow, in, out, err, cap) == PluginFailure::Trap);
    CHECK(contains(err, "unreachable"));
    // An sf_alloc that hands back memory it doesn't have.
    const auto liar = pluginWithManifest(
        wat(R"((module (memory (export "memory") 1) (func (export "sf_alloc") (param i32) (result i32) (i32.const 65000))
              (func (export "sf_run") (param i32) (result i32) (i32.const 0))))"),
        manifestJson("liar"));
    CHECK(run(liar, in, out, err) == PluginFailure::BadOutput);
    CHECK(contains(err, "doesn't have"));
    // An allocator that can't get the memory (the mesh is bigger than the cap) returns 0: refused, nothing runs.
    PluginLimits tiny;
    tiny.memoryBytes = 64 * 1024;
    CHECK(run(plugin(kSame), pluginInputFrom(makeIcosphere(4)), out, err, tiny) == PluginFailure::BadOutput);
    // Reading or writing outside its memory traps.
    const auto oob = plugin("(drop (i32.load (i32.const 0xFFFFFFF0))) (i32.const 0)");
    CHECK(run(oob, in, out, err) == PluginFailure::Trap);
    CHECK(contains(err, "out of bounds"));
}

TEST(plugin_results_are_checked) {
    if (!pluginRuntimeAvailable()) return;
    const PluginInput in = sphereInput();
    PluginOutput out;
    std::string err;
    CHECK(run(plugin(kSame), in, out, err) == PluginFailure::None && out.positions == in.positions);
    auto bad = [&](const std::string& body, const char* why, const char* kind = "deformer") {
        PluginOutput o;
        std::string e;
        const PluginFailure f = run(plugin(body, kind), in, o, e);
        if (f != PluginFailure::BadOutput || !contains(e, why)) std::printf("not refused for \"%s\": %s\n", why, e.c_str());
        return f == PluginFailure::BadOutput && contains(e, why);
    };
    const std::string n = "(i32.load offset=8 (local.get $in))", pos = "(i32.load offset=20 (local.get $in))";
    CHECK(bad(output("(i32.const 0)", "(i32.const 5)", "(i32.const 0)", pos, "(i32.const 0)"), "vertices for a mesh"));
    CHECK(bad(output("(i32.const 0)", n, "(i32.const 1)", pos, "(i32.const 0)"), "can't change the triangles"));
    CHECK(bad(output("(i32.const 0)", n, "(i32.const 0)", "(i32.const 0x7FFFFFF0)", "(i32.const 0)"), "outside its memory"));
    CHECK(bad("(i32.const 0xFFFFFFF0)", "no result"));
    CHECK(bad("(i32.const 17)", "no result")); // misaligned
    CHECK(bad("(i32.store (i32.const 16) (i32.const 9)) (i32.const 16)", "ABI 9"));
    // A NaN position.
    CHECK(bad("(f32.store (i32.load offset=20 (local.get $in)) (f32.const nan)) " + kSame, "not a finite position"));
    // Generators: three vertices at 2048, triangles at 2560.
    const std::string tri = "(i32.store (i32.const 2560) (i32.const 0)) (i32.store (i32.const 2564) (i32.const 1)) ";
    CHECK(bad(tri + "(i32.store (i32.const 2568) (i32.const 99)) " +
                  output("(i32.const 0)", "(i32.const 3)", "(i32.const 1)", "(i32.const 2048)", "(i32.const 2560)"),
              "past the end", "generator"));
    CHECK(bad(tri + "(i32.store (i32.const 2568) (i32.const 1)) " +
                  output("(i32.const 0)", "(i32.const 3)", "(i32.const 1)", "(i32.const 2048)", "(i32.const 2560)"),
              "repeats a vertex", "generator"));
    CHECK(bad(output("(i32.const 0)", "(i32.const 3)", "(i32.const 0)", "(i32.const 2048)", "(i32.const 2560)"),
              "at least one triangle", "generator"));
    // A plugin's own error, with its message (cleaned and capped).
    const std::string msg = "(i32.store (i32.const 3000) (i32.const 0x216f6f54)) "
                            "(i32.store (i32.const 16) (i32.const 1)) (i32.store (i32.const 20) (i32.const 7)) "
                            "(i32.store (i32.const 40) (i32.const 3000)) (i32.store (i32.const 44) (i32.const 4)) (i32.const 16)";
    CHECK(run(plugin(msg), in, out, err) == PluginFailure::PluginError);
    CHECK(err == "Too!");
    // sf_run with the wrong signature.
    const auto sig = pluginWithManifest(wat("(module (memory (export \"memory\") 1) " + std::string(kAlloc) +
                                            " (func (export \"sf_run\") (result i32) (i32.const 0)))"),
                                        manifestJson("sig"));
    CHECK(run(sig, in, out, err) == PluginFailure::BadModule);
    CHECK(!pluginFailureDisables(PluginFailure::BadOutput) && !pluginFailureDisables(PluginFailure::PluginError));
}

TEST(plugin_generator_makes_a_mesh) {
    if (!pluginRuntimeAvailable()) return;
    // A tetrahedron: four vertices at 2048, four triangles at 2560.
    std::string body;
    const float v[12] = {0, 1, 0, -1, -1, 1, 1, -1, 1, 0, -1, -1};
    for (int i = 0; i < 12; ++i)
        body += "(f32.store (i32.const " + std::to_string(2048 + 4 * i) + ") (f32.const " + std::to_string(v[i]) + ")) ";
    const int t[12] = {0, 1, 2, 0, 2, 3, 0, 3, 1, 1, 3, 2};
    for (int i = 0; i < 12; ++i)
        body += "(i32.store (i32.const " + std::to_string(2560 + 4 * i) + ") (i32.const " + std::to_string(t[i]) + ")) ";
    body += output("(i32.const 0)", "(i32.const 4)", "(i32.const 4)", "(i32.const 2048)", "(i32.const 2560)");
    const auto gen = plugin(body, "generator", "", "tetra");
    PluginOutput out;
    std::string err;
    CHECK(run(gen, sphereInput(), out, err) == PluginFailure::None);
    CHECK(out.positions.size() == 4 && out.triangles.size() == 4);

    TempPacks root("sf_plugin_gen");
    PackManifest pm;
    pm.id = "gen-pack";
    pm.name = "Gen Pack";
    pm.version = "1.0";
    pm.author = "Tests";
    pm.license = "CC0-1.0";
    const std::string file = (root.dir / "gen.sfpack").string();
    CHECK(writePack(file, pm, {{PackItemType::Plugin, "", "Tetra.sfplugin", gen}}, "", "", err));
    PackReview review;
    CHECK(reviewPack(file, review, err));
    CHECK(installPack(review, err));
    CommandRegistry reg;
    Document doc;
    doc.setMesh(makeIcosphere(2), "Sphere");
    const size_t parts = doc.parts.size();
    const CommandResult r = reg.execute(doc, "plugin_run", {{"id", std::string("tetra")}});
    CHECK(r.ok);
    CHECK(contains(r.message, "added a Part"));
    CHECK(doc.parts.size() == parts + 1);
    CHECK(doc.mesh.vertexCount() == 4 && doc.mesh.triangleCount() == 4);
}

TEST(plugin_pack_install_run_undo_and_turn_off) {
    if (!pluginRuntimeAvailable()) return;
    TempPacks root("sf_plugin_pack");
    std::string err;
    PackManifest pm;
    pm.id = "plugin-pack";
    pm.name = "Plugin Pack";
    pm.version = "1.0";
    pm.author = "Tests";
    pm.license = "CC0-1.0";
    const std::string file = (root.dir / "plugins.sfpack").string();
    const auto loop = plugin("(loop $l (br $l)) (i32.const 0)", "deformer", "", "looper");
    CHECK(writePack(file, pm,
                    {{PackItemType::Plugin, "", "Noise Displace.sfplugin", noisePlugin()},
                     {PackItemType::Plugin, "", "Looper.sfplugin", loop}},
                    "CC0\n", "", err));
    PackReview review;
    CHECK(reviewPack(file, review, err));
    CHECK(review.counts[size_t(PackItemType::Plugin)] == 2);
    CHECK(packCountsText(review.counts) == "2 plugins");
    CHECK(review.plugins.size() == 2);
    CHECK((review.plugins[0].name == "looper") != (review.plugins[1].name == "looper"));
    CHECK(review.plugins[0].kind == "Deformer" && review.plugins[0].bytes > 0);
    CHECK(installPack(review, err));
    // A pack with a plugin that imports is refused at review, naming it.
    const auto importing = pluginWithManifest(
        wat(R"((module (import "env" "f" (func)) (memory (export "memory") 1) )" + std::string(kAlloc) +
            R"( (func (export "sf_run") (param i32) (result i32) (i32.const 0))))"),
        manifestJson("importer"));
    const std::string badFile = (root.dir / "bad.sfpack").string();
    CHECK(!writePack(badFile, pm, {{PackItemType::Plugin, "", "Importer.sfplugin", importing}}, "", "", err));
    CHECK(contains(err, "Importer.sfplugin") && contains(err, "can't import"));

    CommandRegistry reg;
    Document doc;
    doc.setMesh(makeIcosphere(3), "Sphere");
    const std::vector<glm::vec3> before = doc.mesh.positions;
    CHECK(contains(reg.execute(doc, "plugin_list").message, "noise-displace"));
    CommandResult r = reg.execute(doc, "plugin_run", {{"id", std::string("noise-displace")}, {"amount", 0.1}});
    CHECK(r.ok);
    CHECK(contains(r.message, "moved"));
    CHECK(doc.mesh.positions != before);
    CHECK(reg.execute(doc, "undo").ok);
    CHECK(doc.mesh.positions == before);
    CHECK(!reg.execute(doc, "plugin_run", {{"id", std::string("noise-displace")}, {"bogus", 1.0}}).ok);
    // The mask holds masked vertices in place.
    doc.mesh.ensureMask(0.0f);
    for (uint32_t v = 0; v < doc.mesh.vertexCount(); ++v)
        if (doc.mesh.positions[v].y > 0.0f) doc.mesh.mask[v] = 1.0f;
    CHECK(reg.execute(doc, "plugin_run", {{"id", std::string("Noise Displace")}, {"amount", 0.1}}).ok);
    for (uint32_t v = 0; v < doc.mesh.vertexCount(); ++v)
        if (before[v].y > 0.0f) CHECK(doc.mesh.positions[v] == before[v]);
    CHECK(reg.execute(doc, "undo").ok);
    doc.mesh.clearMask();

    // A plugin that runs too long: stopped, the mesh untouched, the plugin turned off until it's turned on again.
    InstalledPlugin looper;
    CHECK(findInstalledPlugin("looper", looper));
    CHECK(!looper.off);
    PluginLimits quick;
    quick.seconds = 0.2;
    quick.fuel = 1ull << 50;
    std::string msg;
    CHECK(!runPluginOnDocument(doc, looper, {}, msg, quick));
    CHECK(contains(msg, "unchanged") && contains(msg, "turned off"));
    CHECK(doc.mesh.positions == before);
    CHECK(findInstalledPlugin("looper", looper) && looper.off);
    r = reg.execute(doc, "plugin_run", {{"id", std::string("looper")}});
    CHECK(!r.ok && contains(r.message, "turned off"));
    CHECK(reg.execute(doc, "plugin_enable", {{"id", std::string("looper")}}).ok);
    CHECK(findInstalledPlugin("looper", looper) && !looper.off);

    // Turning the pack off takes its plugins away; removing it, too.
    CHECK(setPackEnabled("plugin-pack", false, err));
    CHECK(installedPlugins().empty());
    CHECK(setPackEnabled("plugin-pack", true, err));
    CHECK(installedPlugins().size() == 2);
    CHECK(removePack("plugin-pack", err));
    CHECK(installedPlugins().empty());
}

TEST(plugin_never_from_macros) {
    CommandRegistry reg;
    Macro m;
    std::string err;
    CHECK(!parseMacro("plugin_run id=noise-displace\n", &reg, m, err));
    CHECK(contains(err, "never from a macro"));
    CHECK(!macroCommandAllowed("plugin_enable"));
}

// Random damage to a good plugin: the reader and the runtime refuse or run it, and never crash.
TEST(plugin_fuzz_damaged_modules) {
    if (!pluginRuntimeAvailable()) return;
    const auto good = noisePlugin();
    const PluginInput in = pluginInputFrom(makeIcosphere(1));
    uint64_t seed = 0x5eed1234abcdull;
    auto next = [&] {
        seed ^= seed << 13;
        seed ^= seed >> 7;
        seed ^= seed << 17;
        return seed;
    };
    PluginLimits limits;
    limits.seconds = 1.0;
    limits.fuel = 50000000;
    limits.memoryBytes = 32ull << 20;
    int read = 0, ran = 0;
    for (int i = 0; i < 3000; ++i) {
        std::vector<uint8_t> w = good;
        const int flips = 1 + int(next() % 6);
        for (int k = 0; k < flips; ++k) w[next() % w.size()] = uint8_t(next());
        if (i % 9 == 0) w.resize(next() % w.size());
        PluginManifest m;
        std::string err;
        if (!readPlugin(w, m, err)) continue;
        ++read;
        PluginRunOptions o;
        o.limits = limits;
        PluginOutput out;
        if (runPlugin(w, m, in, o, out, err) == PluginFailure::None) {
            ++ran;
            CHECK(out.positions.size() == in.positions.size());
        }
    }
    std::printf("plugin fuzz: %d of 3000 damaged modules read, %d ran\n", read, ran);
    // Hostile numbers in the input header are the host's own; the plugin's output record is what's hostile: every
    // field set to extremes.
    const PluginInput small = pluginInputFrom(makeIcosphere(0));
    for (const char* v : {"0", "1", "0x7FFFFFFF", "0xFFFFFFFF", "0x80000000", "12", "65532"}) {
        std::string body;
        for (int f = 2; f < 8; ++f)
            body += "(i32.store (i32.const " + std::to_string(16 + 4 * f) + ") (i32.const " + v + ")) ";
        body += "(i32.store (i32.const 16) (i32.const 1)) (i32.store (i32.const 20) (i32.const 0)) (i32.const 16)";
        for (const char* kind : {"deformer", "generator"}) {
            PluginOutput out;
            std::string err;
            const PluginFailure f = run(plugin(body, kind), small, out, err);
            CHECK(f == PluginFailure::BadOutput);
        }
    }
}

// The cost of a call on a 1M-vertex mesh (2M triangles): copying the mesh in, the plugin returning it unchanged,
// and checking and copying the result out, against a plain copy of the same data (docs/design/plugins.md).
TEST(plugin_call_overhead_1m) {
    if (!pluginRuntimeAvailable()) return;
    PluginInput in;
    const size_t nv = 1000000, nt = 2000000;
    in.positions.resize(nv);
    in.normals.assign(nv, glm::vec3(0, 1, 0));
    for (size_t i = 0; i < nv; ++i) in.positions[i] = glm::vec3(float(i % 1000), float(i / 1000), 0.0f) * 0.001f;
    in.triangles.resize(nt);
    for (size_t i = 0; i < nt; ++i) in.triangles[i] = glm::uvec3(uint32_t(i % nv), uint32_t((i + 1) % nv), uint32_t((i + 2) % nv));
    const auto same = plugin(kSame);
    const auto noise = noisePlugin();
    PluginOutput out;
    std::string err;
    CHECK(run(same, in, out, err) == PluginFailure::None); // compiles it
    auto ms = [](auto f) {
        double best = 1e9;
        for (int i = 0; i < 3; ++i) {
            const auto t0 = std::chrono::steady_clock::now();
            f();
            best = std::min(best, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
        }
        return best;
    };
    const double call = ms([&] { CHECK(run(same, in, out, err) == PluginFailure::None); });
    const double copy = ms([&] {
        PluginInput c = in;
        out.positions = c.positions;
    });
    CHECK(run(noise, in, out, err) == PluginFailure::None);
    const double noiseMs = ms([&] { CHECK(run(noise, in, out, err) == PluginFailure::None); });
    std::printf("plugin call overhead, 1M vertices / 2M triangles: %.1f ms (a plain copy of the same data: %.1f ms); "
                "Noise Displace: %.1f ms\n", call, copy, noiseMs);
    CHECK(call < 2000.0);
}

// The samples tests/ui/plugins.sfui installs (tests/ui/data/packs/plugins) are these modules exactly
// (SF_UPDATE_PLUGIN_SAMPLES=1 writes them).
TEST(plugin_ui_samples_are_current) {
    if (!pluginRuntimeAvailable()) return;
    const fs::path dir = fs::path(SF_TEST_DATA_DIR) / ".." / "ui" / "data" / "packs" / "plugins" / "plugins";
    const std::string loopManifest =
        R"({"format":"sculptforge-plugin","formatVersion":1,"id":"endless-loop","name":"Endless Loop","version":"1.0",)"
        R"("author":"Tests","license":"CC0-1.0","description":"Never finishes: for trying Stop.","kind":"deformer"})";
    const std::pair<const char*, std::vector<uint8_t>> samples[] = {
        {"Noise Displace.sfplugin", noisePlugin()},
        {"Endless Loop.sfplugin",
         pluginWithManifest(wat("(module (memory (export \"memory\") 1) " + std::string(kAlloc) +
                                " (func (export \"sf_run\") (param $in i32) (result i32) (loop $l (br $l)) (i32.const 0)))"),
                            loopManifest)}};
    const char* update = std::getenv("SF_UPDATE_PLUGIN_SAMPLES");
    for (const auto& [name, bytes] : samples) {
        if (update && std::string(update) == "1") {
            fs::create_directories(dir);
            std::ofstream(dir / name, std::ios::binary).write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
        }
        const std::string have = readText((dir / name).string());
        CHECK(have == std::string(bytes.begin(), bytes.end()));
    }
}
