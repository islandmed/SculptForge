// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Shared by the exporters that carry UVs and textures (OBJ, FBX, USD, glTF,
// GoZ): faces with their per-corner UVs, and the texture of a SubTool
// (core/Texture.h) as a PNG written beside the model file or embedded in it.
//
// External PNGs are named after the model file: "<stem>_color.png" for a
// one-object export, "<stem>_<object>_color.png" with several objects.
#pragma once

#include "core/Image.h"
#include "core/MapExport.h"
#include "core/Mesh.h"
#include "core/Texture.h"
#include "io/MeshObject.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace sf {

// The mesh has one UV per triangle corner.
inline bool hasCornerUvs(const Mesh& mesh) { return mesh.hasUvs() && mesh.uvs.size() == mesh.triangleCount() * 3; }

// UV of vertex `v` at its corner of triangle `t`.
inline glm::vec2 cornerUv(const Mesh& mesh, uint32_t t, uint32_t v) {
    const glm::uvec3& tv = mesh.triangles[t];
    return mesh.uvs[size_t(t) * 3 + (tv.x == v ? 0 : tv.y == v ? 1 : 2)];
}

// Mesh::exportFace for formats with one UV per face corner: a quad whose halves have different UVs along their
// diagonal (a UV seam) is written as its two triangles. `uv` gets the corners' UVs when the mesh has them.
// Meshes without UVs get exactly exportFace's faces.
inline int exportFaceUv(const Mesh& mesh, uint32_t t, glm::uvec4& face, glm::vec2 uv[4]) {
    int corners = mesh.exportFace(t, face);
    if (!hasCornerUvs(mesh)) return corners;
    auto diagonalMatches = [&](uint32_t first, const glm::uvec4& q) {
        const uint32_t u = mesh.quadPartnerAt(first);
        return cornerUv(mesh, u, q.x) == cornerUv(mesh, first, q.x) && cornerUv(mesh, u, q.z) == cornerUv(mesh, first, q.z);
    };
    if (corners == 0) {
        glm::uvec4 q;
        mesh.exportFace(mesh.quadPartnerAt(t), q);
        if (diagonalMatches(mesh.quadPartnerAt(t), q)) return 0; // written with its partner
        corners = 3;
    } else if (corners == 4 && !diagonalMatches(t, face)) {
        corners = 3;
    }
    if (corners == 3) {
        face = glm::uvec4(mesh.triangles[t], Mesh::kNoQuad);
        for (int c = 0; c < 3; ++c) uv[c] = mesh.uvs[size_t(t) * 3 + size_t(c)];
        return 3;
    }
    for (int c = 0; c < 3; ++c) uv[c] = cornerUv(mesh, t, face[c]);
    uv[3] = cornerUv(mesh, mesh.quadPartnerAt(t), face.w);
    return 4;
}

// Text safe in file and material names: ASCII letters, digits, '-' and '_' (anything else becomes '_').
inline std::string safeTextureName(const std::string& text) {
    std::string out;
    for (unsigned char c : text) out += std::isalnum(c) || c == '-' || c == '_' ? char(c) : '_';
    return out.empty() ? std::string("Object") : out;
}

inline std::filesystem::path textureFsPath(const std::string& s) { return std::filesystem::path(std::u8string(s.begin(), s.end())); }

inline std::string textureUtf8(const std::filesystem::path& p) {
    const auto u = p.u8string();
    return std::string(u.begin(), u.end());
}

// The model file's stem for the files written beside it (<stem>.mtl, <stem>_color.png), with white space
// replaced by '_': OBJ's `mtllib` and MTL's `map_Kd` separate file names (and options) by spaces, so
// "my scene.mtl" would be read as two files "my" and "scene.mtl". Other characters (accents...) are kept.
inline std::string sidecarStem(const std::string& modelPath) {
    std::string stem = textureUtf8(textureFsPath(modelPath).stem());
    for (char& c : stem)
        if (c == ' ' || c == '\t') c = '_';
    return stem.empty() ? std::string("model") : stem;
}

// PNG file names (no folder) for the objects of an export to `modelPath`, "" for objects without a texture.
inline std::vector<std::string> textureFileNames(const std::string& modelPath, const std::vector<MeshObjectRef>& objects) {
    const std::string stem = sidecarStem(modelPath);
    std::vector<std::string> names(objects.size());
    for (size_t i = 0; i < objects.size(); ++i) {
        if (!objects[i].mesh || !hasTexture(*objects[i].mesh)) continue;
        std::string base = objects.size() == 1 ? stem : stem + "_" + safeTextureName(objects[i].name);
        std::string name = base + "_color.png";
        for (int k = 2; std::find(names.begin(), names.end(), name) != names.end(); ++k)
            name = base + "_" + std::to_string(k) + "_color.png";
        names[i] = name;
    }
    return names;
}

// The mesh has a texture with material channels (core/TextureChannels.h): exporters add PBR material slots.
inline bool hasPbrTexture(const Mesh& mesh) { return hasTexture(mesh) && hasMaterialChannels(*mesh.texture); }

// The mesh's texture (its composite) as PNG bytes; with material channels, opacity goes into alpha.
inline bool encodeTexturePng(const Mesh& mesh, std::vector<uint8_t>& png) {
    png.clear();
    if (!hasTexture(mesh)) return false;
    if (hasPbrTexture(mesh)) return encodeMapPng(*mesh.texture, pbrSlotSources("base_color"), false, png);
    return textureImage(*mesh.texture).encodePng(png);
}

// Writes the mesh's texture as `fileName` in the folder of `modelPath`; returns that file's path.
inline bool writeTextureBeside(const std::string& modelPath, const std::string& fileName, const Mesh& mesh,
                               std::string& pngPath, std::string& error) {
    pngPath = textureUtf8(textureFsPath(modelPath).parent_path() / textureFsPath(fileName));
    if (!hasTexture(mesh)) {
        error = "The mesh has no texture";
        return false;
    }
    if (hasPbrTexture(mesh)) {
        std::vector<uint8_t> png;
        if (!encodeTexturePng(mesh, png)) {
            error = "Cannot encode the texture";
            return false;
        }
        std::ofstream f(textureFsPath(pngPath), std::ios::binary);
        if (!f || !f.write(reinterpret_cast<const char*>(png.data()), std::streamsize(png.size()))) {
            error = "Cannot write " + pngPath;
            return false;
        }
        return true;
    }
    return textureImage(*mesh.texture).savePng(pngPath, error);
}

// --- PBR material slots (core/MapExport.h pbrSlots): one PNG per slot beside / inside the model ---

// The file name of a slot's PNG, made from the object's colour PNG name ("<stem>_color.png" ->
// "<stem>_roughness.png").
inline std::string pbrFileName(const std::string& colorName, const std::string& slot) {
    const std::string tail = "_color.png";
    std::string stem = colorName;
    if (stem.size() > tail.size() && stem.compare(stem.size() - tail.size(), tail.size(), tail) == 0)
        stem.resize(stem.size() - tail.size());
    else if (stem.size() > 4)
        stem.resize(stem.size() - 4);
    return stem + "_" + slot + ".png";
}

struct PbrPng {
    std::string slot, name; // name: pbrFileName of the colour PNG ("" when written inside the file)
    std::vector<uint8_t> png;
};
// Every PBR slot but base colour (which is the colour PNG) for the mesh, encoded. Empty without material channels.
inline bool encodePbrPngs(const Mesh& mesh, bool packedOrm, const std::string& colorName, std::vector<PbrPng>& out) {
    out.clear();
    if (!hasPbrTexture(mesh)) return true;
    for (const std::string& slot : pbrSlots(*mesh.texture, packedOrm)) {
        if (slot == "base_color") continue;
        PbrPng p;
        p.slot = slot;
        p.name = colorName.empty() ? std::string() : pbrFileName(colorName, slot);
        if (!encodeMapPng(*mesh.texture, pbrSlotSources(slot), false, p.png)) return false;
        out.push_back(std::move(p));
    }
    return true;
}
// Writes encodePbrPngs' files beside `modelPath`.
inline bool writePbrPngsBeside(const std::string& modelPath, const std::vector<PbrPng>& pngs, std::string& error) {
    for (const PbrPng& p : pngs) {
        const std::string path = textureUtf8(textureFsPath(modelPath).parent_path() / textureFsPath(p.name));
        std::ofstream f(textureFsPath(path), std::ios::binary);
        if (!f || !f.write(reinterpret_cast<const char*>(p.png.data()), std::streamsize(p.png.size()))) {
            error = "Cannot write " + path;
            return false;
        }
    }
    return true;
}
inline const PbrPng* findPbrPng(const std::vector<PbrPng>& pngs, const std::string& slot) {
    for (const PbrPng& p : pngs)
        if (p.slot == slot) return &p;
    return nullptr;
}

} // namespace sf
