// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Malformed and hostile input files found by fuzzing the loaders: each must be
// rejected with an error (or loaded safely), never crash or allocate wildly.
#include "TestHarness.h"

#include "core/Mesh.h"
#include "core/Splats.h"
#include "io/ObjIO.h"
#include "io/PlyIO.h"
#include "core/LayerStackIO.h"
#include "core/Project.h"
#include "core/Primitives.h"
#include "core/Texture.h"
#include "io/Zip.h"

#include <cmath>
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
