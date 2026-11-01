// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Coordinate access for other tools (#36): read the active SubTool's positions and triangles, and write new
// positions back as one undoable step without touching topology, vertex order, layers, masks, UVs or polygroups.
// Data travels as base64 of little-endian float32 / uint32 arrays inside the command's JSON message. The running
// app serves these commands on its local API (app/LiveApi.cpp); sculptforge-cli and scripts can run them too.
#include "core/LiveApi.h"

#include "core/Commands.h"
#include "core/Document.h"
#include "core/Json.h"

#include <cmath>
#include <cstring>

namespace sf {
namespace {

const char kB64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string base64(const void* data, size_t size) {
    const auto* p = static_cast<const unsigned char*>(data);
    std::string out;
    out.reserve((size + 2) / 3 * 4);
    for (size_t i = 0; i < size; i += 3) {
        const uint32_t v = uint32_t(p[i]) << 16 | (i + 1 < size ? uint32_t(p[i + 1]) << 8 : 0u) |
                           (i + 2 < size ? uint32_t(p[i + 2]) : 0u);
        out += kB64[(v >> 18) & 63];
        out += kB64[(v >> 12) & 63];
        out += i + 1 < size ? kB64[(v >> 6) & 63] : '=';
        out += i + 2 < size ? kB64[v & 63] : '=';
    }
    return out;
}

bool unbase64(const std::string& in, std::string& out) {
    int map[256];
    std::fill(std::begin(map), std::end(map), -1);
    for (int i = 0; i < 64; ++i) map[uint8_t(kB64[i])] = i;
    out.clear();
    out.reserve(in.size() / 4 * 3);
    uint32_t acc = 0;
    int bits = 0;
    for (char c : in) {
        if (c == '=' || c == '\n' || c == '\r') continue;
        const int v = map[uint8_t(c)];
        if (v < 0) return false;
        acc = acc << 6 | uint32_t(v);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out += char((acc >> bits) & 0xff);
        }
    }
    return true;
}

} // namespace

uint64_t topologyHash(const Mesh& mesh) {
    uint64_t h = 1469598103934665603ull; // FNV-1a over the vertex count and the triangle indices
    auto mix = [&](uint64_t v) {
        for (int i = 0; i < 8; ++i) {
            h ^= (v >> (8 * i)) & 0xff;
            h *= 1099511628211ull;
        }
    };
    mix(mesh.vertexCount());
    for (const auto& t : mesh.triangles) mix(uint64_t(t[0]) | uint64_t(t[1]) << 21 | uint64_t(t[2]) << 42);
    return h;
}

void registerLiveApiCommands(CommandRegistry& reg) {
    using T = ParamSpec::Type;
    reg.add({"mesh_read",
             "The active SubTool's vertex positions and triangles, for tools that edit coordinates (#36): JSON with "
             "subtool, vertices, triangles, topology (a hash to send back with mesh_set_positions), positions (base64 "
             "of float32 x y z per vertex) and, unless indices=false, indices (base64 of uint32, three per triangle).",
             {{"indices", T::Bool, "Include the triangles (default true).", false}},
             [](Document& doc, const Args& a) -> CommandResult {
                 const Mesh& m = doc.mesh;
                 Json j;
                 j.set("subtool", doc.subtools[size_t(doc.activeSubtool)].name);
                 j.set("subtool_index", double(doc.activeSubtool));
                 j.set("vertices", double(m.vertexCount()));
                 j.set("triangles", double(m.triangleCount()));
                 j.set("topology", std::to_string(topologyHash(m)));
                 j.set("positions", base64(m.positions.data(), m.positions.size() * sizeof(glm::vec3)));
                 if (argBool(a, "indices", true)) {
                     std::vector<uint32_t> idx;
                     idx.reserve(m.triangles.size() * 3);
                     for (const auto& t : m.triangles) idx.insert(idx.end(), {t[0], t[1], t[2]});
                     j.set("indices", base64(idx.data(), idx.size() * sizeof(uint32_t)));
                 }
                 return {true, j.dump(false)};
             }});
    reg.add({"mesh_set_positions",
             "Replace the active SubTool's vertex positions in one undoable step (#36). positions: base64 of float32 "
             "x y z for every vertex, in the order mesh_read gave. topology: the hash from mesh_read; refused if the "
             "SubTool or its topology changed meanwhile. Layers, mask, UVs, polygroups and colours are kept.",
             {{"positions", T::String, "base64 float32 x y z per vertex", true},
              {"topology", T::String, "the topology hash from mesh_read", true},
              {"label", T::String, "Undo label (default 'Set positions').", false}},
             [](Document& doc, const Args& a) -> CommandResult {
                 Mesh& m = doc.mesh;
                 if (argString(a, "topology") != std::to_string(topologyHash(m)))
                     return {false, "The SubTool's topology changed since mesh_read (or another SubTool is active)"};
                 std::string bytes;
                 if (!unbase64(argString(a, "positions"), bytes)) return {false, "positions is not valid base64"};
                 if (bytes.size() != m.positions.size() * sizeof(glm::vec3))
                     return {false, "positions has " + std::to_string(bytes.size() / 12) + " vertices, the SubTool " +
                                        std::to_string(m.vertexCount())};
                 std::vector<glm::vec3> next(m.positions.size());
                 std::memcpy(next.data(), bytes.data(), bytes.size());
                 UndoRecord r;
                 r.label = argString(a, "label", "Set positions");
                 r.layer = -1;
                 std::vector<uint32_t> moved;
                 for (uint32_t i = 0; i < uint32_t(next.size()); ++i) {
                     if (!std::isfinite(next[i].x) || !std::isfinite(next[i].y) || !std::isfinite(next[i].z))
                         return {false, "positions contains a non-finite number at vertex " + std::to_string(i)};
                     if (next[i] != m.positions[i]) moved.push_back(i);
                 }
                 if (moved.empty()) return {true, "Set positions: nothing moved"};
                 r.positions.indices = moved;
                 for (uint32_t i : moved) r.positions.values.push_back(m.positions[i]);
                 for (uint32_t i : moved) m.positions[i] = next[i];
                 m.updateNormals(moved);
                 doc.bvhDirty = true;
                 doc.undo.push(std::move(r));
                 return {true, "Set positions: " + std::to_string(moved.size()) + " vertices moved"};
             }});
}

} // namespace sf
