// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Malformed and hostile input files found by fuzzing the loaders: each must be
// rejected with an error (or loaded safely), never crash or allocate wildly.
#include "TestHarness.h"

#include "core/Mesh.h"
#include "core/Splats.h"
#include "io/GltfIO.h"
#include "io/ObjIO.h"
#include "io/PlyIO.h"
#include "core/LayerStackIO.h"
#include "core/Project.h"
#include "core/Primitives.h"
#include "core/Texture.h"
#include "io/Zip.h"

#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>

using namespace sf;

namespace {

std::string writeFile(const char* name, const std::string& bytes) {
    const std::string path = tempPath(name);
    std::ofstream(path, std::ios::binary).write(bytes.data(), std::streamsize(bytes.size()));
    return path;
}

bool plyLoads(const std::string& text, std::string& err) {
    const std::string path = writeFile("sculptforge_malformed.ply", text);
    Mesh m;
    SplatCloud s;
    PlyContent c;
    const bool ok = loadPly(path, m, s, c, err);
    std::filesystem::remove(path);
    return ok;
}

} // namespace

TEST(ply_header_claiming_billions_of_rows_is_rejected) {
    // A splat header promising 3 billion rows in a file of a few hundred bytes used to allocate before reading.
    std::string err;
    CHECK(!plyLoads("ply\nformat binary_little_endian 1.0\nelement vertex 3000000000\nproperty float x\nproperty float y\n"
                    "property float z\nproperty float scale_0\nproperty float rot_0\nproperty float opacity\nend_header\n"
                    "0123456789abcdef",
                    err));
    CHECK(err.find("more data than the file contains") != std::string::npos);
    CHECK(!plyLoads("ply\nformat ascii 1.0\nelement vertex 3\nproperty float x\nproperty float y\nproperty float z\n"
                    "element face 2000000000\nproperty list uchar int vertex_indices\nend_header\n0 0 0\n1 0 0\n0 1 0\n3 0 1 2\n",
                    err));
}

TEST(ply_negative_or_huge_face_values_are_rejected) {
    std::string err;
    const std::string head = "ply\nformat ascii 1.0\nelement vertex 3\nproperty float x\nproperty float y\nproperty float z\n"
                             "element face 1\nproperty list int float vertex_indices\nend_header\n0 0 0\n1 0 0\n0 1 0\n";
    CHECK(!plyLoads(head + "-3 0 1 2\n", err));     // negative list count
    CHECK(!plyLoads(head + "3 0 -65 2\n", err));    // negative index
    CHECK(!plyLoads(head + "3 0 1 7.8e34\n", err)); // index beyond uint32
    CHECK(plyLoads(head + "3 0 1 2\n", err));       // the well-formed file still loads
}

TEST(non_finite_coordinates_do_not_break_import) {
    // NaN and infinite positions reach the attribute-transfer grid during welding; they must not be cast to int.
    const std::string path = writeFile("sculptforge_malformed.obj",
                                       "v nan 0 0 1 0 0\nv 0 inf 0 0 1 0\nv 0 0 1e38 0 0 1\nv 1 1 1 1 1 1\nf 1 2 3\nf 2 3 4\n");
    Mesh m;
    std::string err;
    loadObj(path, m, err); // either outcome is fine; it must not crash or trip the sanitizers
    std::filesystem::remove(path);
    CHECK(true);
}

TEST(weld_handles_far_off_and_non_finite_positions) {
    Mesh m;
    m.positions = {{1e30f, 0, 0}, {0, 1e30f, 0}, {0, 0, std::nanf("")}, {1, 2, 3}, {1, 2, 3}};
    m.triangles = {{0, 1, 2}, {1, 3, 4}};
    const Mesh w = weldImported(m);
    CHECK(w.positions.size() <= m.positions.size()); // no crash or UB; duplicates may merge
}

// --- Material channels (core/LayerStackIO.h): the sparse tile blobs and the project's "pbr" key ---

TEST(malformed_material_channel_blobs_are_rejected) {
    TileImage img;
    img.reset(256, 256, 1, 8, TileImage::toUnits(8, 0.5f));
    const float v[1] = {1.0f};
    img.setValues(3, 3, v);
    const std::vector<uint8_t> good = encodeTileImage(img);
    std::string err;
    TileImage out;
    out.reset(256, 256, 1, 8, {});
    CHECK(decodeTileImage(good, out, err));
    CHECK(out.sameTexels(img));
    auto rejects = [&](std::vector<uint8_t> bytes, TileImage target) {
        std::string why;
        const bool ok = decodeTileImage(bytes, target, why);
        return !ok && !why.empty();
    };
    TileImage same;
    same.reset(256, 256, 1, 8, {});
    CHECK(rejects({}, same));
    CHECK(rejects({1, 2, 3}, same));
    std::vector<uint8_t> truncated(good.begin(), good.begin() + std::ptrdiff_t(good.size() / 2));
    CHECK(rejects(truncated, same));
    std::vector<uint8_t> flipped = good;
    for (size_t i = 6; i < flipped.size(); i += 3) flipped[i] ^= 0x5A;
    CHECK(rejects(flipped, same));
    std::vector<uint8_t> huge = good;
    huge[0] = huge[1] = huge[2] = huge[3] = 0xFF; // claims a 4 GB payload
    CHECK(rejects(huge, same));
    TileImage other; // a different size or format than the file holds
    other.reset(512, 256, 1, 8, {});
    CHECK(rejects(good, other));
    TileImage deeper;
    deeper.reset(256, 256, 1, 16, {});
    CHECK(rejects(good, deeper));
}

TEST(malformed_material_channels_in_a_project_fail_cleanly) {
    Mesh m = makePlane(4);
    m.uvs.assign(m.triangleCount() * 3, glm::vec2(0.5f));
    for (size_t t = 0; t < m.triangleCount(); ++t) {
        m.uvs[t * 3] = glm::vec2(0, 0);
        m.uvs[t * 3 + 1] = glm::vec2(1, 0);
        m.uvs[t * 3 + 2] = glm::vec2(0, 1);
    }
    m.texture = makeTexture(1024, 1024);
    m.paintToTexture = true;
    setChannelEnabled(*m.texture, Channel::Roughness, true);
    const float v[1] = {0.1f};
    m.texture->channels[size_t(Channel::Roughness)].base.setValues(10, 10, v);
    addTextureLayer(*m.texture, "Layer 1");
    Document doc = makeDoc(std::move(m));
    const std::string path = tempPath("sf_malformed_pbr.sfproj");
    std::string err;
    CHECK(saveProject(path, doc, err));
    std::vector<ZipEntry> entries;
    CHECK(readStoredZip(path, entries, err));
    // A garbled channel blob: the load fails with a message and leaves the open document alone.
    for (auto& e : entries)
        if (e.name.find(".sftiles") != std::string::npos)
            for (size_t i = 4; i < e.data.size(); ++i) e.data[i] = uint8_t(i * 37);
    CHECK(writeStoredZip(path, entries, 64, err));
    Document keep = makeDoc(makePlane(2));
    const size_t tris = keep.mesh.triangleCount();
    err.clear();
    CHECK(!loadProject(path, keep, err));
    CHECK(!err.empty());
    CHECK(keep.mesh.triangleCount() == tris);
    // Unknown channel names, wrong types and absurd values in "pbr" are skipped or clamped.
    CHECK(saveProject(path, doc, err));
    entries.clear();
    CHECK(readStoredZip(path, entries, err));
    Json manifest;
    CHECK(Json::parse(std::string(entries[0].data.begin(), entries[0].data.end()), manifest, err));
    // Json is read-only through [], so edits copy the path down and set it back.
    auto editPbr = [&](const std::function<void(Json&)>& edit) {
        Json subtools = manifest["subtools"];
        Json st = subtools[0], tex = st["texture"], pbr = tex["pbr"];
        edit(pbr);
        tex.set("pbr", std::move(pbr));
        st.set("texture", std::move(tex));
        Json list = Json::array();
        list.push(std::move(st));
        for (size_t i = 1; i < subtools.size(); ++i) list.push(subtools[i]);
        manifest.set("subtools", std::move(list));
    };
    editPbr([](Json& pbr) {
        Json channels = pbr["channels"];
        Json extra = Json::object();
        extra.set("channel", "sparkle");
        channels.push(std::move(extra));
        pbr.set("channels", std::move(channels));
        pbr.set("height_normal_strength", -1e30);
        Json layers = pbr["layers"], first = layers[0];
        Json layerEntry = Json::object();
        layerEntry.set("channel", "roughness");
        layerEntry.set("blend", "normal_combine"); // only for normals: ignored
        layerEntry.set("opacity", "lots");
        first.push(std::move(layerEntry));
        Json newLayers = Json::array();
        newLayers.push(std::move(first));
        pbr.set("layers", std::move(newLayers));
    });
    const std::string text = manifest.dump(true);
    entries[0].data.assign(text.begin(), text.end());
    CHECK(writeStoredZip(path, entries, 64, err));
    Document back;
    CHECK(loadProject(path, back, err));
    CHECK(back.mesh.texture && channelEnabled(*back.mesh.texture, Channel::Roughness));
    CHECK(back.mesh.texture->heightNormalStrength == 0.0f);
    CHECK(back.mesh.texture->layers[0].channels[size_t(Channel::Roughness)].blend != kBlendNormalCombine);
    // A blob whose count is beyond what the texture could hold is refused before reading it.
    editPbr([](Json& pbr) {
        Json channels = Json::array();
        for (size_t i = 0; i < pbr["channels"].size(); ++i) {
            Json c = pbr["channels"][i];
            if (i == 0) {
                Json base = c["base"];
                base.set("count", 1e12);
                c.set("base", std::move(base));
            }
            channels.push(std::move(c));
        }
        pbr.set("channels", std::move(channels));
    });
    const std::string text2 = manifest.dump(true);
    entries[0].data.assign(text2.begin(), text2.end());
    CHECK(writeStoredZip(path, entries, 64, err));
    err.clear();
    CHECK(!loadProject(path, back, err));
    CHECK(!err.empty());
    std::filesystem::remove(path);
}

// --- glTF / GLB (io/GltfIO.cpp): accessor and buffer sizes from the file are validated before use ---

namespace {

std::string base64(const std::vector<uint8_t>& in) {
    static const char* chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    size_t i = 0;
    for (; i + 2 < in.size(); i += 3) {
        const uint32_t v = uint32_t(in[i]) << 16 | uint32_t(in[i + 1]) << 8 | in[i + 2];
        for (int s = 18; s >= 0; s -= 6) out += chars[(v >> s) & 63];
    }
    if (i < in.size()) {
        const uint32_t v = uint32_t(in[i]) << 16 | (i + 1 < in.size() ? uint32_t(in[i + 1]) << 8 : 0);
        out += chars[(v >> 18) & 63];
        out += chars[(v >> 12) & 63];
        out += i + 1 < in.size() ? chars[(v >> 6) & 63] : '=';
        out += '=';
    }
    return out;
}

// One triangle's positions (36 bytes) followed by its uint16 indices (6 bytes, padded to 8).
std::vector<uint8_t> triangleBytes() {
    const float p[9] = {0, 0, 0, 1, 0, 0, 0, 1, 0};
    const uint16_t idx[4] = {0, 1, 2, 0};
    std::vector<uint8_t> b(44);
    std::memcpy(b.data(), p, 36);
    std::memcpy(b.data() + 36, idx, 8);
    return b;
}

std::string dataUri(const std::vector<uint8_t>& bytes) { return "data:application/octet-stream;base64," + base64(bytes); }

// A glTF with one buffer (`bytes`, or none when `uri` is false), the given views and accessors, one mesh and node.
std::string gltfText(const std::string& views, const std::string& accessors, const std::string& prim,
                     const std::vector<uint8_t>& bytes = triangleBytes(), bool uri = true) {
    return "{\"asset\":{\"version\":\"2.0\"},\"buffers\":[{\"byteLength\":" + std::to_string(bytes.size()) +
           (uri ? ",\"uri\":\"" + dataUri(bytes) + "\"" : std::string()) + "}],\"bufferViews\":[" + views +
           "],\"accessors\":[" + accessors + "],\"meshes\":[{\"primitives\":[" + prim +
           "]}],\"nodes\":[{\"mesh\":0}],\"scenes\":[{\"nodes\":[0]}],\"scene\":0}";
}

const char* kViews = R"({"buffer":0,"byteOffset":0,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":6})";
const char* kPositions = R"({"bufferView":0,"componentType":5126,"count":3,"type":"VEC3"})";
const char* kIndices = R"({"bufferView":1,"componentType":5123,"count":3,"type":"SCALAR"})";
const char* kPrim = R"({"attributes":{"POSITION":0},"indices":1})";

bool gltfBytesLoad(const std::string& bytes, std::string& err, Mesh* mesh = nullptr,
                   const char* name = "sculptforge_malformed.gltf") {
    const std::string path = writeFile(name, bytes);
    Mesh m;
    err.clear();
    const bool ok = loadGltf(path, m, err);
    std::filesystem::remove(path);
    if (mesh) *mesh = std::move(m);
    return ok;
}

bool gltfRejected(const std::string& text) {
    std::string err;
    const bool ok = gltfBytesLoad(text, err);
    return !ok && !err.empty();
}

std::string glb(const std::string& json, const std::vector<uint8_t>& bin) {
    std::string j = json;
    while (j.size() % 4) j += ' ';
    std::string out;
    auto u32 = [&](uint32_t v) { out.append(reinterpret_cast<const char*>(&v), 4); };
    u32(0x46546C67);
    u32(2);
    u32(uint32_t(12 + 8 + j.size() + 8 + bin.size()));
    u32(uint32_t(j.size()));
    u32(0x4E4F534A);
    out += j;
    u32(uint32_t(bin.size()));
    u32(0x004E4942);
    out.append(reinterpret_cast<const char*>(bin.data()), bin.size());
    return out;
}

std::string replaced(std::string text, const std::string& from, const std::string& to) {
    const size_t at = text.find(from);
    if (at != std::string::npos) text.replace(at, from.size(), to);
    return text;
}

} // namespace

TEST(gltf_well_formed_triangle_still_loads) {
    std::string err;
    Mesh m;
    CHECK(gltfBytesLoad(gltfText(kViews, std::string(kPositions) + "," + kIndices, kPrim), err, &m));
    CHECK(m.triangleCount() == 1);
    CHECK(m.positions.size() == 3);
    // The same file as a GLB with its data in the BIN chunk.
    const std::string json = gltfText(kViews, std::string(kPositions) + "," + kIndices, kPrim, triangleBytes(), false);
    CHECK(gltfBytesLoad(glb(json, triangleBytes()), err, &m, "sculptforge_malformed.glb"));
    CHECK(m.triangleCount() == 1);
}

TEST(gltf_buffer_view_offset_that_wraps_around_is_rejected) {
    // byteOffset + byteLength past 2^64 used to wrap to a small number and pass the range check.
    const std::string acc = std::string(kPositions) + "," + kIndices;
    CHECK(gltfRejected(gltfText(R"({"buffer":0,"byteOffset":18446744073709551600,"byteLength":36},)"
                                R"({"buffer":0,"byteOffset":36,"byteLength":6})",
                                acc, kPrim)));
    CHECK(gltfRejected(gltfText(R"({"buffer":0,"byteOffset":9007199254740990,"byteLength":36},)"
                                R"({"buffer":0,"byteOffset":36,"byteLength":6})",
                                acc, kPrim)));
    // A view longer than its buffer, and an accessor offset past its view.
    CHECK(gltfRejected(gltfText(R"({"buffer":0,"byteLength":4000},{"buffer":0,"byteOffset":36,"byteLength":6})", acc, kPrim)));
    CHECK(gltfRejected(gltfText(kViews, R"({"bufferView":0,"byteOffset":40,"componentType":5126,"count":3,"type":"VEC3"},)" +
                                            std::string(kIndices),
                                kPrim)));
}

TEST(gltf_huge_accessor_count_is_rejected_without_allocating) {
    // 4e12 positions would be a 48 TB allocation before the range check.
    std::string err;
    CHECK(!gltfBytesLoad(gltfText(kViews, R"({"bufferView":0,"componentType":5126,"count":4e12,"type":"VEC3"},)" +
                                              std::string(kIndices),
                                  kPrim),
                         err));
    CHECK(err.find("out of range") != std::string::npos);
    CHECK(gltfRejected(gltfText(
        kViews, std::string(kPositions) + R"(,{"bufferView":1,"componentType":5123,"count":4e12,"type":"SCALAR"})", kPrim)));
    CHECK(gltfRejected(gltfText(kViews,
                                std::string(kPositions) + "," + kIndices +
                                    R"(,{"bufferView":0,"componentType":5126,"count":4e12,"type":"VEC2"})",
                                R"({"attributes":{"POSITION":0,"TEXCOORD_0":2},"indices":1})")));
}

TEST(gltf_stride_that_wraps_around_is_rejected) {
    // (count - 1) * stride: 4 * 2^62 wraps to 0 in 64 bits.
    const std::string views = R"({"buffer":0,"byteLength":36,"byteStride":4611686018427387904},)"
                              R"({"buffer":0,"byteOffset":36,"byteLength":6})";
    CHECK(gltfRejected(gltfText(views, R"({"bufferView":0,"componentType":5126,"count":5,"type":"VEC3"},)" +
                                           std::string(kIndices),
                                kPrim)));
    // Strides beyond glTF's 252 bytes, smaller than an element, or not whole numbers.
    for (const char* stride : {"256", "8", "12.5", "-12"})
        CHECK(gltfRejected(gltfText(std::string(R"({"buffer":0,"byteLength":36,"byteStride":)") + stride +
                                        R"(},{"buffer":0,"byteOffset":36,"byteLength":6})",
                                    std::string(kPositions) + "," + kIndices, kPrim)));
    // A valid stride that just doesn't fit: three elements 16 bytes apart need 44 bytes.
    CHECK(gltfRejected(gltfText(R"({"buffer":0,"byteLength":36,"byteStride":16},{"buffer":0,"byteOffset":36,"byteLength":6})",
                                std::string(kPositions) + "," + kIndices, kPrim)));
}

TEST(gltf_negative_fractional_and_huge_numbers_are_rejected) {
    const std::string idx = std::string(",") + kIndices;
    for (const char* count : {"-1", "2.5", "1e300", "-1e300"})
        CHECK(gltfRejected(gltfText(kViews,
                                    std::string(R"({"bufferView":0,"componentType":5126,"count":)") + count +
                                        R"(,"type":"VEC3"})" + idx,
                                    kPrim)));
    for (const char* bv : {"-1", "0.5", "1e30", "2"})
        CHECK(gltfRejected(gltfText(kViews,
                                    std::string(R"({"bufferView":)") + bv +
                                        R"(,"componentType":5126,"count":3,"type":"VEC3"})" + idx,
                                    kPrim)));
    for (const char* off : {"-4", "0.5", "1e300"})
        CHECK(gltfRejected(gltfText(std::string(R"({"buffer":0,"byteOffset":)") + off +
                                        R"(,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":6})",
                                    std::string(kPositions) + idx, kPrim)));
    for (const char* buf : {"-1", "1e20", "0.25", "1"})
        CHECK(gltfRejected(gltfText(std::string(R"({"buffer":)") + buf +
                                        R"(,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":6})",
                                    std::string(kPositions) + idx, kPrim)));
    CHECK(gltfRejected(gltfText(kViews, R"({"bufferView":0,"componentType":1e30,"count":3,"type":"VEC3"})" + idx, kPrim)));
    for (const char* a : {"-1", "1e20", "0.5", "7"})
        CHECK(gltfRejected(gltfText(kViews, std::string(kPositions) + idx,
                                    std::string(R"({"attributes":{"POSITION":)") + a + R"(},"indices":1})")));
    CHECK(gltfRejected(gltfText(kViews, std::string(kPositions) + idx, R"({"attributes":{"POSITION":0},"indices":-2})")));
    CHECK(gltfRejected(
        gltfText(kViews, std::string(kPositions) + idx, R"({"attributes":{"POSITION":0,"COLOR_0":-1e20},"indices":1})")));
    CHECK(gltfRejected(
        gltfText(kViews, std::string(kPositions) + idx, R"({"attributes":{"POSITION":0},"indices":1,"mode":1e30})")));
    // Scene, node and mesh indices.
    const std::string base = gltfText(kViews, std::string(kPositions) + idx, kPrim);
    CHECK(gltfRejected(replaced(base, "\"scene\":0", "\"scene\":-3")));
    CHECK(gltfRejected(replaced(base, "\"scene\":0", "\"scene\":1e300")));
    CHECK(gltfRejected(replaced(base, "\"scenes\":[{\"nodes\":[0]}]", "\"scenes\":[{\"nodes\":[-1]}]")));
    CHECK(gltfRejected(replaced(base, "\"scenes\":[{\"nodes\":[0]}]", "\"scenes\":[{\"nodes\":[1e20]}]")));
    CHECK(gltfRejected(replaced(base, "{\"mesh\":0}", "{\"mesh\":1.5}")));
    CHECK(gltfRejected(replaced(base, "{\"mesh\":0}", "{\"mesh\":-1}")));
    CHECK(gltfRejected(replaced(base, "{\"mesh\":0}", "{\"mesh\":0,\"children\":[1e20]}")));
    // Out-of-float-range transforms and weights are clamped, not cast blindly.
    std::string err;
    CHECK(gltfBytesLoad(replaced(base, "{\"mesh\":0}", "{\"mesh\":0,\"translation\":[1e300,0,0],\"scale\":[1,-1e300,1]}"), err));
}

TEST(gltf_index_accessor_without_buffer_view_reads_zeros) {
    // glTF 2.0: no bufferView means all zeros. It used to read buffer view 0 instead (here: float positions, which
    // as indices are far out of range, so the old reading failed the whole file).
    const std::string accessors = std::string(kPositions) + R"(,{"componentType":5125,"count":3,"type":"SCALAR"})";
    const std::string prims = R"({"attributes":{"POSITION":0},"indices":1},{"attributes":{"POSITION":0}})";
    std::string err;
    Mesh m;
    CHECK(gltfBytesLoad(gltfText(kViews, accessors, prims), err, &m));
    CHECK(m.triangleCount() >= 1); // the second primitive's triangle; the all-zero one is degenerate
    // Zero-filled accessors are capped, so a tiny file can't ask for terabytes of zeros.
    CHECK(gltfRejected(gltfText(kViews, std::string(kPositions) + R"(,{"componentType":5125,"count":4e12,"type":"SCALAR"})",
                                R"({"attributes":{"POSITION":0},"indices":1})")));
    CHECK(gltfRejected(gltfText(kViews, R"({"componentType":5126,"count":4e12,"type":"VEC3"})", R"({"attributes":{"POSITION":0}})")));
    CHECK(gltfRejected(
        gltfText(kViews, R"({"componentType":5126,"count":20000000,"type":"VEC3"})", R"({"attributes":{"POSITION":0}})")));
}

TEST(gltf_long_data_uri_decodes_exactly) {
    // A 1 MB base64 buffer: the decoder's accumulator used to shift left without bound (signed overflow).
    std::vector<uint8_t> bytes = triangleBytes();
    const size_t pad = (size_t(1) << 20) + 1;
    std::vector<uint8_t> big(pad);
    for (size_t i = 0; i < pad; ++i) big[i] = uint8_t(i * 131 + 7);
    bytes.insert(bytes.begin(), big.begin(), big.end());
    const std::string off = std::to_string(pad), off2 = std::to_string(pad + 36);
    std::string err;
    Mesh m;
    CHECK(gltfBytesLoad(gltfText(R"({"buffer":0,"byteOffset":)" + off + R"(,"byteLength":36},{"buffer":0,"byteOffset":)" +
                                     off2 + R"(,"byteLength":6})",
                                 std::string(kPositions) + "," + kIndices, kPrim, bytes),
                        err, &m));
    CHECK(m.triangleCount() == 1);
    float maxX = 0, maxY = 0;
    for (const auto& p : m.positions) maxX = std::max(maxX, p.x), maxY = std::max(maxY, p.y);
    CHECK(maxX == 1.0f && maxY == 1.0f);
}

TEST(gltf_node_graph_cannot_multiply_work) {
    // 60 levels of nodes that each list the next one twice: 2^60 visits if shared children were walked again.
    std::string nodes = "{\"mesh\":0,\"children\":[1,1]}";
    for (int i = 1; i < 60; ++i)
        nodes += ",{\"mesh\":0,\"children\":[" + std::to_string(i + 1) + "," + std::to_string(i + 1) + "]}";
    nodes += ",{\"mesh\":0,\"children\":[0]}"; // and a cycle back to the root
    std::string text = replaced(gltfText(kViews, std::string(kPositions) + "," + kIndices, kPrim), "\"nodes\":[{\"mesh\":0}]",
                                "\"nodes\":[" + nodes + "]");
    const auto start = std::chrono::steady_clock::now();
    std::string err;
    CHECK(gltfBytesLoad(text, err));
    CHECK(std::chrono::steady_clock::now() - start < std::chrono::seconds(20));
    // Without a scene, only root nodes are walked (children once, under their parents); here every node is a child.
    text = replaced(text, ",\"scenes\":[{\"nodes\":[0]}],\"scene\":0", "");
    CHECK(gltfRejected(text)); // no roots: nothing to load, reported as an error rather than looping
}

TEST(gltf_too_many_morph_targets_are_rejected) {
    std::string targets;
    for (int i = 0; i < 5000; ++i) targets += std::string(i ? "," : "") + "{}";
    CHECK(gltfRejected(gltfText(kViews, std::string(kPositions) + "," + kIndices,
                                R"({"attributes":{"POSITION":0},"indices":1,"targets":[)" + targets + "]}")));
    // A few targets with zero-filled or real deltas still load.
    std::string err;
    CHECK(gltfBytesLoad(gltfText(kViews, std::string(kPositions) + "," + kIndices,
                                 R"({"attributes":{"POSITION":0},"indices":1,"targets":[{},{"POSITION":0}]})"),
                        err));
}

TEST(gltf_external_buffer_paths_are_confined) {
    for (const char* uri : {"../secret.bin", "/etc/passwd", "sub/../../x.bin", "."})
        CHECK(gltfRejected("{\"asset\":{\"version\":\"2.0\"},\"buffers\":[{\"byteLength\":4,\"uri\":\"" + std::string(uri) +
                           "\"}],\"meshes\":[],\"nodes\":[]}"));
}

TEST(malformed_glb_containers_are_rejected) {
    const std::string json = gltfText(kViews, std::string(kPositions) + "," + kIndices, kPrim, triangleBytes(), false);
    const std::string good = glb(json, triangleBytes());
    std::string err;
    CHECK(gltfBytesLoad(good, err, nullptr, "sculptforge_malformed.glb"));
    auto rejects = [&](const std::string& bytes) {
        std::string why;
        return !gltfBytesLoad(bytes, why, nullptr, "sculptforge_malformed.glb") && !why.empty();
    };
    auto setU32 = [](std::string s, size_t at, uint32_t v) {
        std::memcpy(&s[at], &v, 4);
        return s;
    };
    CHECK(rejects(setU32(good, 12, 0xFFFFFFFFu)));                // JSON chunk claims 4 GB
    CHECK(rejects(setU32(good, 12, 0xFFFFFFFDu)));                // ... a length that wraps when padded
    const size_t binHeader = 20 + ((json.size() + 3) & ~size_t(3));
    CHECK(rejects(setU32(good, binHeader, 0xFFFFFFFFu)));         // BIN chunk past the end
    CHECK(rejects(setU32(good, 8, uint32_t(good.size() + 100)))); // header length past the end
    CHECK(rejects(setU32(good, 8, 4)));                           // header length shorter than the header
    CHECK(rejects(setU32(good, 4, 1)));                           // glTF 1.0 container
    CHECK(rejects(good.substr(0, good.size() - 10)));             // truncated
    CHECK(rejects(setU32(good, 16, 0x12345678u)));                // no JSON chunk
    CHECK(rejects(glb(json, std::vector<uint8_t>(20, 0))));       // BIN shorter than the buffer views need
}
