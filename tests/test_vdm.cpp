// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Vector displacement brushes (core/Vdm.h): EXR and .sfvdm round trips, the built-ins, capturing a VDM from
// the VDM plane and stamping it back, the tangent frame and rotation, symmetry, mask, invert, undo, the
// library and the commands.
#include "TestHarness.h"

#include "core/BrushLibrary.h"
#include "core/Commands.h"
#include "core/Primitives.h"
#include "core/Sculpt.h"
#include "core/ThreadPool.h"
#include "core/Vdm.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>

using namespace sf;
namespace fs = std::filesystem;

namespace {

bool run(Document& d, const std::string& name, const Args& args = {}) {
    static CommandRegistry registry;
    const CommandResult r = registry.execute(d, name, args);
    if (!r.ok) std::printf("  %s: %s\n", name.c_str(), r.message.c_str());
    return r.ok;
}

uint32_t nearest(const Mesh& m, const glm::vec3& p) {
    uint32_t best = 0;
    for (uint32_t v = 1; v < m.vertexCount(); ++v)
        if (glm::distance(m.positions[v], p) < glm::distance(m.positions[best], p)) best = v;
    return best;
}

// A small asymmetric map: a lump in the top-right quarter that leans toward +X and +Y.
VectorDisplacement leaningLump(int size = 32) {
    VectorDisplacement v;
    v.name = "Lump";
    v.width = v.height = size;
    v.vectors.resize(size_t(size) * size_t(size));
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x) {
            const float u = (float(x) + 0.5f) / float(size) * 2.0f - 1.0f, w = 1.0f - (float(y) + 0.5f) / float(size) * 2.0f;
            const float d = glm::length(glm::vec2(u - 0.3f, w - 0.2f)) / 0.45f;
            const float k = d < 1.0f ? (1.0f - d * d) * (1.0f - d * d) : 0.0f;
            v.vectors[size_t(y) * size_t(size) + size_t(x)] = glm::vec3(0.2f, 0.1f, 0.3f) * k;
        }
    return v;
}

// The deformation sculpted onto the VDM plane in vdm_capture_then_stamp: a lump that leans over to +X (an
// overhang) and toward the top of the map (world -Z).
glm::vec3 sculptedOffset(const glm::vec3& p) {
    const float d = glm::length(glm::vec2(p.x, p.z)) / 0.6f;
    const float k = d < 1.0f ? (1.0f - d * d) * (1.0f - d * d) : 0.0f;
    return glm::vec3(0.35f, 0.4f, -0.15f) * k;
}

struct VdmLibrary {
    fs::path root;
    BrushLibraryPaths saved = brushLibraryPaths();
    explicit VdmLibrary(const char* name) {
        root = fs::temp_directory_path() / (std::string("sculptforge_vdmlib_") + name);
        fs::remove_all(root);
        fs::create_directories(root / "user");
        fs::create_directories(root / "studio");
        brushLibraryPaths() = paths();
    }
    ~VdmLibrary() {
        brushLibraryPaths() = saved;
        std::error_code ec;
        fs::remove_all(root, ec);
    }
    BrushLibraryPaths paths() const { return {(root / "user").string(), {(root / "studio").string()}}; }
};

} // namespace

TEST(vdm_builtins_are_bounded_and_overhang) {
    const auto& all = builtinVdms();
    CHECK(all.size() == 10); // Horn, Ear Curl, Scales, Nostril and the six from #43
    for (const auto& v : all) {
        CHECK(v->builtin && v->width == 256 && v->height == 256);
        CHECK(v->extent() > 0.1f && v->extent() < 2.0f);
        // Zero along the border, so a stamp blends into any surface.
        float border = 0.0f;
        for (int i = 0; i < v->width; ++i) {
            border = std::max(border, glm::length(v->vectors[size_t(i)]));
            border = std::max(border, glm::length(v->vectors[size_t(v->height - 1) * size_t(v->width) + size_t(i)]));
            border = std::max(border, glm::length(v->vectors[size_t(i) * size_t(v->width)]));
        }
        CHECK(border < 1e-4f);
        // Sideways motion, not just height: what a grayscale alpha can't do.
        float lateral = 0.0f;
        for (const glm::vec3& d : v->vectors) lateral = std::max(lateral, std::hypot(d.x, d.y));
        CHECK(lateral > 0.06f);
        if (lateral <= 0.06f || v->extent() <= 0.1f || border >= 1e-4f)
            std::printf("  %s: lateral %.3f extent %.3f border %g\n", v->name.c_str(), lateral, v->extent(), border);
        CHECK(builtinVdm(v->name) == v);
    }
    // The horn's tip leans over past its base toward the map's top (+Y): an overhang.
    const auto horn = builtinVdm("horn");
    CHECK(horn != nullptr);
    if (horn) {
        const glm::vec3 tip = horn->sample(0.0f, 0.0f);
        CHECK(tip.z > 0.6f && tip.y > 0.3f);
    }
    // The nostril digs in.
    const auto nostril = builtinVdm("Nostril");
    CHECK(nostril && nostril->sample(0.0f, 0.0f).z < -0.3f);
    CHECK(builtinVdm("nope") == nullptr);
}

TEST(vdm_exr_round_trip) {
    VectorDisplacement v = makeEarCurlVdm(64);
    std::vector<unsigned char> bytes;
    std::string err;
    CHECK(encodeVdmExr(v, bytes, err));
    CHECK(bytes.size() > 100 && bytes[0] == 0x76 && bytes[1] == 0x2f && bytes[2] == 0x31 && bytes[3] == 0x01);
    VectorDisplacement back;
    CHECK(decodeVdmExr(bytes.data(), bytes.size(), back, err));
    CHECK(back.width == 64 && back.height == 64 && back.vectors == v.vectors); // 32-bit float: exact

    const std::string path = tempPath("sculptforge_vdm_round.exr");
    CHECK(writeVdmExr(path, v, err));
    VectorDisplacement file;
    CHECK(loadVdm(path, file, err));
    CHECK(file.vectors == v.vectors && file.name == "sculptforge_vdm_round" && file.path == path && !file.builtin);

    // Import options: swap Y/Z, flips, scale.
    VdmImportOptions opt;
    opt.swapYZ = true;
    opt.flipX = true;
    opt.scale = 2.0f;
    VectorDisplacement swapped;
    CHECK(readVdmExr(path, swapped, err, opt));
    const glm::vec3 a = v.vectors[size_t(30) * 64 + 33], b = swapped.vectors[size_t(30) * 64 + 33];
    CHECK(std::abs(b.x + 2.0f * a.x) < 1e-6f && std::abs(b.y - 2.0f * a.z) < 1e-6f && std::abs(b.z - 2.0f * a.y) < 1e-6f);
    fs::remove(path);

    // Garbage is refused.
    const unsigned char junk[] = {1, 2, 3, 4, 5, 6, 7, 8};
    CHECK(!decodeVdmExr(junk, sizeof junk, back, err) && !err.empty());
    CHECK(!loadVdm(tempPath("sculptforge_no_such.exr"), back, err));
}

TEST(vdm_sfvdm_round_trip_and_damaged_files) {
    VectorDisplacement v = leaningLump(40);
    v.name = "L\xc3\xbcmp \"two\""; // UTF-8 and quotes
    const std::string path = tempPath("sculptforge_vdm_round.sfvdm");
    std::string err;
    CHECK(writeVdmFile(path, v, err));
    VectorDisplacement back;
    CHECK(readVdmFile(path, back, err));
    CHECK(back.name == v.name && back.width == 40 && back.height == 40 && back.vectors == v.vectors);
    CHECK(isVdmFile(path) && isVdmFile("a.EXR") && !isVdmFile("a.png"));
    // Truncated, wrong magic, empty.
    const auto size = fs::file_size(path);
    fs::resize_file(path, size - 7);
    CHECK(!readVdmFile(path, back, err));
    { std::ofstream(path, std::ios::binary) << "SFVDM2\nxxxxxxxxxxxxxxxxxxxxxxx"; }
    CHECK(!readVdmFile(path, back, err));
    { std::ofstream(path, std::ios::binary | std::ios::trunc); }
    CHECK(!readVdmFile(path, back, err));
    fs::remove(path);
    CHECK(!writeVdmFile(path, VectorDisplacement{}, err));
}

TEST(vdm_sample_is_bilinear_and_zero_outside) {
    VectorDisplacement v;
    v.width = v.height = 2;
    v.vectors = {glm::vec3(0, 0, 1), glm::vec3(0, 0, 2), glm::vec3(0, 0, 3), glm::vec3(0, 0, 4)}; // top row first
    CHECK(std::abs(v.sample(0.0f, 0.0f).z - 2.5f) < 1e-5f);
    CHECK(std::abs(v.sample(-0.5f, 0.5f).z - 1.0f) < 1e-5f);  // top-left pixel centre
    CHECK(std::abs(v.sample(0.5f, -0.5f).z - 4.0f) < 1e-5f);  // bottom-right
    CHECK(v.sample(1.01f, 0.0f) == glm::vec3(0.0f) && v.sample(0.0f, -1.2f) == glm::vec3(0.0f));
}

// Sculpt a known shape onto the VDM plane, capture it, stamp it on another plane at radius 1: every point lands
// where the sculpted plane's point did (Z Intensity 50 = the map's own size).
TEST(vdm_capture_then_stamp_reproduces_the_shape) {
    Document plane = makeDoc(makeVdmPlane(128));
    CHECK(plane.mesh.hasUvs() && plane.mesh.hasMask());
    CHECK(plane.mesh.maskAt(0) == 1.0f && plane.mesh.maskAt(nearest(plane.mesh, glm::vec3(0.0f))) == 0.0f);
    for (glm::vec3& p : plane.mesh.positions) p += sculptedOffset(p);
    plane.mesh.recomputeAllNormals();

    VectorDisplacement captured;
    std::string err;
    CHECK(captureVdm(plane.mesh, 128, captured, err));
    CHECK(captured.width == 128);
    // The map holds the offsets in tangent axes: X = +X, Y = -Z, Z = +Y.
    for (const glm::vec2 uv : {glm::vec2(0.0f, 0.0f), glm::vec2(0.21f, -0.13f), glm::vec2(-0.4f, 0.3f), glm::vec2(0.8f, 0.8f)}) {
        const glm::vec3 world(uv.x, 0.0f, -uv.y);
        const glm::vec3 o = sculptedOffset(world);
        const glm::vec3 m = captured.sample(uv.x, uv.y);
        CHECK(glm::length(m - glm::vec3(o.x, -o.z, o.y)) < 0.01f);
    }
    // No UVs, no capture.
    VectorDisplacement none;
    CHECK(!captureVdm(makePlane(8, 2.0f), 64, none, err) && !err.empty());

    Document target = makeDoc(makePlane(128, 2.0f));
    target.brushVdm = std::make_shared<const VectorDisplacement>(captured);
    CHECK(run(target, "vdm_stamp", {{"point", std::string("0,1,0")}, {"view", std::string("0,-1,0")}, {"radius", 1.0}}));
    float worst = 0.0f;
    const Mesh flat = makePlane(128, 2.0f);
    for (uint32_t v = 0; v < flat.vertexCount(); ++v) {
        const glm::vec3 p = flat.positions[v];
        if (std::max(std::abs(p.x), std::abs(p.z)) > 0.95f) continue; // the 3% border fade
        worst = std::max(worst, glm::distance(target.mesh.positions[v], p + sculptedOffset(p)));
    }
    CHECK(worst < 0.01f);
    // Half the radius: half the offsets, at half the distance.
    Document small = makeDoc(makePlane(128, 2.0f));
    small.brushVdm = target.brushVdm;
    CHECK(run(small, "vdm_stamp", {{"point", std::string("0,1,0")}, {"view", std::string("0,-1,0")}, {"radius", 0.5}}));
    const uint32_t c = nearest(flat, glm::vec3(0.0f));
    CHECK(glm::distance(small.mesh.positions[c], sculptedOffset(glm::vec3(0.0f)) * 0.5f) < 0.01f);
}

TEST(vdm_tangent_frame_rotation_and_normal) {
    // Turning the stamp turns its sideways push: the lump leans to the stamp's +X and +Y.
    auto lump = std::make_shared<const VectorDisplacement>(leaningLump());
    auto leanOf = [&](float angle) {
        Document d = makeDoc(makePlane(96, 2.0f));
        d.brushVdm = lump;
        const auto before = d.mesh.positions;
        CHECK(run(d, "vdm_stamp", {{"point", std::string("0,1,0")}, {"view", std::string("0,-1,0")}, {"radius", 0.9},
                                   {"angle", double(angle)}}));
        glm::vec3 sum(0.0f);
        for (uint32_t v = 0; v < d.mesh.vertexCount(); ++v) sum += d.mesh.positions[v] - before[v];
        return sum;
    };
    const glm::vec3 a = leanOf(0.0f), b = leanOf(90.0f);
    // Seen from the top (view -Y): right = +X, up = -Z. The lump moves +X, -Z (and up).
    CHECK(a.x > 0.0f && a.z < 0.0f && a.y > 0.0f);
    // 90 degrees counter-clockwise: right -> up (-Z), up -> -right (-X).
    CHECK(std::abs(b.x - a.z) < std::abs(a.x) * 0.05f + 1e-3f);
    CHECK(std::abs(b.z - (-a.x)) < std::abs(a.x) * 0.05f + 1e-3f);
    CHECK(std::abs(b.y - a.y) < a.y * 0.02f);

    // On the side of a sphere, Z goes out along the surface normal there.
    Document s = makeDoc(makeIcosphere(5, 1.0f));
    s.brushVdm = builtinVdm("horn");
    const uint32_t tip = nearest(s.mesh, glm::vec3(1.0f, 0.0f, 0.0f));
    const glm::vec3 before = s.mesh.positions[tip];
    CHECK(run(s, "vdm_stamp", {{"point", std::string("0,0,0")}, {"view", std::string("-1,0,0")}, {"radius", 0.3}}));
    const glm::vec3 moved = s.mesh.positions[tip] - before;
    CHECK(moved.x > 0.15f && std::abs(moved.z) < 0.05f);
}

TEST(vdm_symmetry_mirrors_exactly) {
    Document d = makeDoc(makePlane(64, 2.0f));
    d.brushVdm = std::make_shared<const VectorDisplacement>(leaningLump());
    const auto before = d.mesh.positions;
    CHECK(run(d, "vdm_stamp", {{"point", std::string("0.45,1,0.1")}, {"view", std::string("0,-1,0")}, {"radius", 0.35},
                               {"angle", 30.0}, {"symmetry", std::string("x")}}));
    float worst = 0.0f, moved = 0.0f;
    // Mirror pairs by grid index (makePlane: row-major, x fastest): x index i <-> n - i.
    const int n = 64;
    for (int j = 0; j <= n; ++j)
        for (int i = 0; i <= n; ++i) {
            const uint32_t v = uint32_t(j * (n + 1) + i), w = uint32_t(j * (n + 1) + (n - i));
            const glm::vec3 dv = d.mesh.positions[v] - before[v], dw = d.mesh.positions[w] - before[w];
            worst = std::max(worst, glm::length(dw - glm::vec3(-dv.x, dv.y, dv.z)));
            moved = std::max(moved, glm::length(dv));
        }
    CHECK(moved > 0.02f);
    CHECK(worst < 1e-5f);
}

TEST(vdm_respects_mask_invert_and_undo) {
    Document d = makeDoc(makePlane(64, 2.0f));
    d.brushVdm = builtinVdm("horn");
    auto& mask = d.mesh.ensureMask(0.0f);
    for (uint32_t v = 0; v < d.mesh.vertexCount(); ++v)
        if (d.mesh.positions[v].x < 0.0f) mask[v] = 1.0f;
    const auto before = d.mesh.positions;
    const Args at = {{"point", std::string("0,1,0")}, {"view", std::string("0,-1,0")}, {"radius", 0.5}};
    CHECK(run(d, "vdm_stamp", at));
    bool maskedStill = true, rightMoved = false;
    for (uint32_t v = 0; v < d.mesh.vertexCount(); ++v) {
        if (before[v].x < 0.0f && d.mesh.positions[v] != before[v]) maskedStill = false;
        if (before[v].x > 0.05f && glm::distance(d.mesh.positions[v], before[v]) > 0.05f) rightMoved = true;
    }
    CHECK(maskedStill && rightMoved);
    const auto stamped = d.mesh.positions;
    CHECK(run(d, "undo"));
    CHECK(d.mesh.positions == before);
    CHECK(run(d, "redo"));
    CHECK(d.mesh.positions == stamped);

    // Alt: the same sideways motion, the height the other way.
    Document a = makeDoc(makePlane(64, 2.0f)), b = makeDoc(makePlane(64, 2.0f));
    a.brushVdm = b.brushVdm = std::make_shared<const VectorDisplacement>(leaningLump());
    CHECK(run(a, "vdm_stamp", at));
    Args inv = at;
    inv["invert"] = true;
    CHECK(run(b, "vdm_stamp", inv));
    float worst = 0.0f;
    for (uint32_t v = 0; v < a.mesh.vertexCount(); ++v) {
        const glm::vec3 da = a.mesh.positions[v] - before[v], db = b.mesh.positions[v] - before[v];
        worst = std::max(worst, glm::length(db - glm::vec3(da.x, -da.y, da.z)));
    }
    CHECK(worst < 1e-5f);
}

TEST(vdm_is_deterministic_with_and_without_threads) {
    auto stamp = [] {
        Document d = makeDoc(makeIcosphere(6, 1.0f));
        d.brushVdm = builtinVdm("scales");
        CHECK(run(d, "vdm_stamp", {{"point", std::string("0.2,0.3,0")}, {"view", std::string("0,0,-1")}, {"radius", 0.6},
                                   {"symmetry", std::string("x")}}));
        return d.mesh.positions;
    };
    const auto parallel = stamp();
    ThreadPool::shared().setSerial(true);
    const auto serial = stamp();
    ThreadPool::shared().setSerial(false);
    CHECK(parallel == serial);
    // Capture too.
    Document plane = makeDoc(makeVdmPlane(64));
    for (glm::vec3& p : plane.mesh.positions) p += sculptedOffset(p);
    VectorDisplacement a, b;
    std::string err;
    CHECK(captureVdm(plane.mesh, 96, a, err));
    ThreadPool::shared().setSerial(true);
    CHECK(captureVdm(plane.mesh, 96, b, err));
    ThreadPool::shared().setSerial(false);
    CHECK(a.vectors == b.vectors);
}

TEST(vdm_library_commands) {
    VdmLibrary lib("commands");
    Document d;
    // A studio VDM, then the user's capture.
    VectorDisplacement studio = leaningLump();
    studio.name = "Studio Lump";
    std::string err;
    CHECK(writeVdmFile((lib.root / "studio" / "Studio Lump.sfvdm").string(), studio, err));
    { std::ofstream(lib.root / "user" / "broken.sfvdm", std::ios::binary) << "nope"; }
    const auto messages = loadBrushLibrary(d, lib.paths());
    CHECK(messages.size() == 1); // broken.sfvdm
    CHECK(d.vdms.size() == builtinVdms().size() + 1 && findVdm(d, "studio lump") == int(builtinVdms().size()));
    fs::remove(lib.root / "user" / "broken.sfvdm");

    CHECK(run(d, "vdm_new_plane", {{"resolution", 64.0}}));
    CHECK(d.subtoolCount() == 2 && !d.subtools[0].visible && d.activeSubtool == 1 && d.mesh.hasUvs());
    for (glm::vec3& p : d.mesh.positions) p += sculptedOffset(p);
    const std::string exr = (lib.root / "Spike.exr").string();
    CHECK(run(d, "vdm_capture", {{"name", std::string("Spike")}, {"resolution", 64.0}, {"exr", exr}}));
    CHECK(d.brushVdm && d.brushVdm->name == "Spike" && fs::exists(lib.root / "user" / "Spike.sfvdm") && fs::exists(exr));
    CHECK(run(d, "vdm_capture", {{"name", std::string("Horn")}, {"resolution", 32.0}, {"save", false}}));
    CHECK(d.brushVdm->name == "Horn (mine)" && builtinVdm("Horn")->width == 256);

    // A fresh session finds the capture in the library; the EXR loads the same map.
    Document fresh;
    loadBrushLibrary(fresh, lib.paths());
    const int spike = findVdm(fresh, "Spike");
    CHECK(spike >= 0 && fresh.vdms[size_t(spike)]->vectors == d.vdms[size_t(findVdm(d, "Spike"))]->vectors);
    CHECK(run(fresh, "vdm_set", {{"path", exr}, {"save", false}}));
    CHECK(fresh.brushVdm->vectors == fresh.vdms[size_t(spike)]->vectors);
    CHECK(run(fresh, "vdm_set", {{"name", std::string("ear curl")}}) && fresh.brushVdm->name == "Ear Curl");
    CHECK(!run(fresh, "vdm_set", {{"name", std::string("nothing")}}));
    CHECK(run(fresh, "vdm_list"));

    const std::string out = (lib.root / "out.sfvdm").string();
    CHECK(run(fresh, "vdm_export", {{"path", out}, {"name", std::string("Scales")}}));
    VectorDisplacement scales;
    CHECK(readVdmFile(out, scales, err) && scales.vectors == builtinVdm("scales")->vectors);

    // Delete: library VDMs only.
    CHECK(!run(fresh, "vdm_delete", {{"name", std::string("Horn")}}));
    CHECK(!run(fresh, "vdm_delete", {{"name", std::string("Studio Lump")}}));
    // (fresh's Spike is the EXR now, loaded over the library one by name: not deletable from here.)
    CHECK(!run(fresh, "vdm_delete", {{"name", std::string("Spike")}}));
    CHECK(run(d, "vdm_delete", {{"name", std::string("Spike")}}));
    CHECK(!fs::exists(lib.root / "user" / "Spike.sfvdm") && findVdm(d, "Spike") < 0);

    // The stroke command stamps with the brush's VDM too.
    Document s = makeDoc(makePlane(48, 2.0f));
    const auto before = s.mesh.positions;
    CHECK(run(s, "stroke", {{"brush", std::string("vdm")}, {"points", std::string("0,1,0")}, {"view", std::string("0,-1,0")},
                            {"radius", 0.5}}));
    CHECK(s.mesh.positions != before);
}

// The VDM plane keeps its UVs through Divide (finer detail to sculpt), and a stamp at the highest subdivision
// level survives stepping down and back up.
TEST(vdm_with_subdivision_levels) {
    Document plane = makeDoc(makeVdmPlane(32));
    CHECK(run(plane, "subdiv_divide", {{"smooth", false}}));
    CHECK(run(plane, "subdiv_divide", {{"smooth", false}}));
    CHECK(plane.mesh.vertexCount() == 129u * 129u && plane.mesh.hasUvs());
    for (glm::vec3& p : plane.mesh.positions) p += sculptedOffset(p);
    VectorDisplacement captured;
    std::string err;
    CHECK(captureVdm(plane.mesh, 128, captured, err));
    const glm::vec3 o = sculptedOffset(glm::vec3(0.1f, 0.0f, -0.2f));
    CHECK(glm::length(captured.sample(0.1f, 0.2f) - glm::vec3(o.x, -o.z, o.y)) < 0.01f);

    Document d = makeDoc(makeIcosphere(3, 1.0f));
    CHECK(run(d, "subdiv_divide"));
    CHECK(run(d, "subdiv_divide"));
    d.brushVdm = builtinVdm("horn");
    CHECK(run(d, "vdm_stamp", {{"point", std::string("0,0,0")}, {"view", std::string("0,0,-1")}, {"radius", 0.4}}));
    const auto stamped = d.mesh.positions;
    CHECK(run(d, "subdiv_level", {{"level", 1.0}}));
    CHECK(run(d, "subdiv_level", {{"level", 3.0}}));
    float worst = 0.0f;
    for (size_t v = 0; v < stamped.size(); ++v) worst = std::max(worst, glm::distance(stamped[v], d.mesh.positions[v]));
    CHECK(worst < 1e-4f);
}
