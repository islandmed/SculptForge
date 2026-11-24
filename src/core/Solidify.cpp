// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/Solidify.h"

#include "core/Commands.h"
#include "core/Document.h"
#include "core/ExtractMirror.h"
#include "core/Micro.h"
#include "core/Multires.h"

#include <algorithm>
#include <cmath>

namespace sf {

std::string solidifyBlocker(const Mesh& m) {
    if (m.triangleCount() == 0) return "The Part has no faces";
    if (!m.forgespheres.empty()) return "Solidify needs a mesh: make the Forge Spheres a mesh first (Tool > Forge Spheres)";
    if (!m.tube.empty()) return "Solidify needs a mesh: Bake the Tube first (Tool > Tube)";
    if (hasMicro(m)) return "Solidify works on a Part without a Micro level: Bake Micro first (Tool > Geometry)";
    if (subdivLevelCount(m) > 0)
        return "Solidify works on a Part without subdivision levels (it rebuilds the mesh): Freeze Levels first (Tool > Geometry)";
    return {};
}

Mesh solidifyMesh(const Mesh& mesh, const SolidifyOptions& o, std::string& error) {
    error.clear();
    if (const std::string why = solidifyBlocker(mesh); !why.empty()) {
        error = why;
        return {};
    }
    if (!std::isfinite(o.thickness) || std::abs(o.thickness) < 1e-6f) {
        error = "Thickness is 0: nothing to add";
        return {};
    }
    // Edges shared by more than two faces have no one back to join: the rim would cross itself there.
    {
        std::vector<uint64_t> keys;
        keys.reserve(mesh.triangles.size() * 3);
        for (const glm::uvec3& t : mesh.triangles)
            for (int k = 0; k < 3; ++k) {
                const uint32_t a = t[k], b = t[(k + 1) % 3];
                keys.push_back((uint64_t(std::min(a, b)) << 32) | std::max(a, b));
            }
        std::sort(keys.begin(), keys.end());
        for (size_t i = 0; i + 2 < keys.size(); ++i)
            if (keys[i] == keys[i + 2]) {
                error = "The Part has edges shared by more than two faces: Solidify needs a clean single surface";
                return {};
            }
    }
    const auto boundary = boundaryEdges(mesh.triangles);
    if (boundary.empty()) {
        error = "The Part is closed already: it has a back. Solidify gives a flat, open surface (a plane, an Extract with "
                "Thickness 0) its thickness";
        return {};
    }
    const uint32_t nv = uint32_t(mesh.vertexCount()), nt = uint32_t(mesh.triangleCount());
    const AABB box = mesh.bounds();
    const glm::vec3 e = box.extent();
    const float side = std::max({e.x, e.y, e.z, 1e-6f});
    // A negative thickness grows in front of the surface: the same shell built on the surface turned over (its
    // winding reversed, so its normals point the other way), which keeps every vertex and triangle index.
    const bool reversed = o.thickness < 0.0f;
    const float t = std::abs(o.thickness) * side;
    // The back goes along -normal by t (so the surface the artist sees stays put); Both Sides: half each way.
    const float front = o.bothSides ? 0.5f * t : 0.0f, back = o.bothSides ? -0.5f * t : -t;

    Mesh out = mesh; // the front keeps everything: indices, groups, UVs, paint, mask, creases, layers
    if (reversed) {
        for (uint32_t ti = 0; ti < nt; ++ti) {
            std::swap(out.triangles[ti].y, out.triangles[ti].z);
            if (out.uvs.size() == size_t(nt) * 3) std::swap(out.uvs[size_t(ti) * 3 + 1], out.uvs[size_t(ti) * 3 + 2]);
            // (the edge from corner k runs the other way: edge flags and creases move to the corner the edge now starts at)
            if (out.uvFlags.size() == size_t(nt) * 3) {
                uint8_t* f = &out.uvFlags[size_t(ti) * 3];
                const uint8_t a = f[0], b = f[1], c = f[2];
                f[0] = c, f[1] = b, f[2] = a;
            }
            if (out.creases.size() == size_t(nt) * 3) {
                float* f = &out.creases[size_t(ti) * 3];
                const float a = f[0], b = f[1], c = f[2];
                f[0] = c, f[1] = b, f[2] = a;
            }
        }
    }
    const Mesh& base = out; // (the surface as built on: its normals, triangles and corner attributes)
    Mesh normals = base;    // (the stored normals may be stale after a sparse edit)
    normals.recomputeAllNormals();
    out.positions.resize(size_t(nv) * 2);
    std::vector<glm::vec3> frontPositions(mesh.positions.begin(), mesh.positions.begin() + nv);
    for (uint32_t v = 0; v < nv; ++v) {
        const glm::vec3 n = normals.normals[v];
        out.positions[v] = frontPositions[v] + n * front;
        out.positions[nv + v] = frontPositions[v] + n * back;
    }
    auto doubleVertexAttr = [nv](auto& vec) {
        if (vec.size() != nv) return;
        vec.reserve(size_t(nv) * 2);
        for (uint32_t v = 0; v < nv; ++v) vec.push_back(vec[v]);
    };
    doubleVertexAttr(out.mask);
    doubleVertexAttr(out.colors);
    doubleVertexAttr(out.paintBase);
    for (PaintLayer& l : out.paintLayers) {
        doubleVertexAttr(l.color);
        doubleVertexAttr(l.alpha);
    }
    if (!out.layers.empty() && out.base.size() == nv) { // sculpt layers: the back carries the same deltas
        for (uint32_t v = 0; v < nv; ++v) out.base.push_back(mesh.base[v] + normals.normals[v] * back);
        for (uint32_t v = 0; v < nv; ++v) out.base[v] = mesh.base[v] + normals.normals[v] * front;
        for (SculptLayer& l : out.layers) doubleVertexAttr(l.delta);
    }

    // The back: every triangle again, reversed, on the copied vertices; its quads stay quads.
    // Face groups always: the front keeps its own (all 0 when it had none), the back the same, the rim a new one.
    if (mesh.facegroups.size() != nt) out.ensureFaceGroups(0);
    const bool groups = true, hidden = mesh.hidden.size() == nt;
    const bool uvs = mesh.uvs.size() == size_t(nt) * 3, uvFlags = mesh.uvFlags.size() == size_t(nt) * 3;
    const bool creases = mesh.creases.size() == size_t(nt) * 3;
    // The back copies the base's triangles (as oriented now) and corner attributes; read them before pushing.
    const std::vector<glm::uvec3> baseTriangles(out.triangles.begin(), out.triangles.begin() + nt);
    const std::vector<glm::vec2> baseUvs(out.uvs);
    const std::vector<uint8_t> baseUvFlags(out.uvFlags);
    const std::vector<float> baseCreases(out.creases);
    uint32_t rimGroup = 1;
    for (uint32_t ti = 0; ti < nt; ++ti) rimGroup = std::max(rimGroup, out.facegroups[ti] + 1);
    out.triangles.reserve(size_t(nt) * 2 + boundary.size() * 2);
    for (uint32_t ti = 0; ti < nt; ++ti) {
        const glm::uvec3& c = baseTriangles[ti];
        out.triangles.push_back({c.x + nv, c.z + nv, c.y + nv});
        if (groups) out.facegroups.push_back(out.facegroups[ti]);
        if (hidden) out.hidden.push_back(mesh.hidden[ti]);
        // Corner k of the back triangle is corner (0, 2, 1)[k] of the front one; the edge from corner k runs the
        // other way, so edge flags and creases come from the front edge that ends there.
        if (uvs) {
            out.uvs.push_back(baseUvs[size_t(ti) * 3 + 0]);
            out.uvs.push_back(baseUvs[size_t(ti) * 3 + 2]);
            out.uvs.push_back(baseUvs[size_t(ti) * 3 + 1]);
        }
        if (uvFlags) {
            out.uvFlags.push_back(baseUvFlags[size_t(ti) * 3 + 2]);
            out.uvFlags.push_back(baseUvFlags[size_t(ti) * 3 + 1]);
            out.uvFlags.push_back(baseUvFlags[size_t(ti) * 3 + 0]);
        }
        if (creases) {
            out.creases.push_back(baseCreases[size_t(ti) * 3 + 2]);
            out.creases.push_back(baseCreases[size_t(ti) * 3 + 1]);
            out.creases.push_back(baseCreases[size_t(ti) * 3 + 0]);
        }
    }
    if (mesh.hasQuads())
        for (uint32_t ti = 0; ti < nt; ++ti) {
            const uint32_t u = mesh.quadPartnerAt(ti);
            if (u != Mesh::kNoQuad && u > ti) out.pairQuad(nt + ti, nt + u);
        }
    // The rim: one quad per open edge (as wound in the base), between the edge and its copy on the back.
    const auto rimEdges = reversed ? boundaryEdges(baseTriangles) : boundary;
    for (const auto& [a, b] : rimEdges) {
        const uint32_t first = uint32_t(out.triangles.size());
        out.triangles.push_back({b, a, a + nv});
        out.triangles.push_back({b, a + nv, b + nv});
        if (groups) {
            out.facegroups.push_back(rimGroup);
            out.facegroups.push_back(rimGroup);
        }
        if (hidden) {
            out.hidden.push_back(0);
            out.hidden.push_back(0);
        }
        if (uvs)
            for (int k = 0; k < 6; ++k) out.uvs.push_back(glm::vec2(0.0f));
        if (uvFlags)
            for (int k = 0; k < 6; ++k) out.uvFlags.push_back(0);
        if (creases)
            for (int k = 0; k < 6; ++k) out.creases.push_back(0.0f);
        out.pairQuad(first, first + 1);
    }
    if (out.hasQuads()) out.quadPartners.resize(out.triangles.size(), Mesh::kNoQuad);
    out.rebuildTopology();
    out.recomputeAllNormals();
    return out;
}

void registerSolidifyCommands(CommandRegistry& reg) {
    using T = ParamSpec::Type;
    reg.add({"solidify",
             "Give the active Part (a flat, single-sided surface) thickness in place: a copy of the surface, pushed along "
             "the normals and facing the other way, joined to it by a rim of quads along every open edge; one closed "
             "mesh, one undo step. Face groups, UVs and paint stay; the rim gets a new face group.",
             {{"thickness", T::Number,
               "As a fraction of the Part's largest side (default 0.03; 0.03 is 3%). Positive grows behind the surface "
               "(along -normal), negative in front of it.",
               false},
              {"both", T::Bool, "Centre the thickness on the surface, half each way. Default false.", false}},
             [](Document& doc, const Args& a) -> CommandResult {
                 SolidifyOptions o;
                 o.thickness = float(std::clamp(argNumber(a, "thickness", 0.03), -1.0, 1.0));
                 o.bothSides = argBool(a, "both", false);
                 std::string error;
                 Mesh out = solidifyMesh(doc.mesh, o, error);
                 if (!error.empty()) return {false, error};
                 const size_t faces = doc.mesh.triangleCount() - doc.mesh.quadCount();
                 const size_t rim = (out.triangleCount() - 2 * doc.mesh.triangleCount()) / 2;
                 doc.replaceMeshUndoable(std::move(out), "Solidify");
                 return {true, "Solidified: " + std::to_string(faces) + " faces got a back and a rim of " + std::to_string(rim) +
                                   " quads"};
             }});
}

} // namespace sf
