// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "io/ObjIO.h"

#include "io/TextureExport.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>

namespace sf {

namespace {

std::filesystem::path utf8Path(const std::string& s) {
    return std::filesystem::path(std::u8string(s.begin(), s.end()));
}

const char* skipSpace(const char* p, const char* end) {
    while (p < end && (*p == ' ' || *p == '\t')) ++p;
    return p;
}

// True for a "<c> ..." or bare "<c>" statement.
bool isStatement(const char* s, char c) { return s[0] == c && (s[1] == ' ' || s[1] == '\t' || s[1] == '\r' || s[1] == 0); }

// The text after a one-letter statement, without surrounding blanks.
std::string statementName(const char* s) {
    std::string name(s + 1);
    while (!name.empty() && (name.back() == ' ' || name.back() == '\t' || name.back() == '\r')) name.pop_back();
    size_t start = 0;
    while (start < name.size() && (name[start] == ' ' || name[start] == '\t')) ++start;
    return name.substr(start);
}

// One `o` object while parsing: triangles index the file's global vertex list.
struct ObjObject {
    std::string name;
    std::vector<glm::uvec3> triangles;
    std::vector<uint32_t> groups;          // per triangle: index into groupNames
    std::vector<uint32_t> quads;           // first triangle of each quad face (the second one follows it)
    std::vector<glm::vec2> uvs;            // per triangle corner (when any face has `vt` indices)
    bool hasUvs = false;
    std::vector<std::string> groupNames;   // `g` names in order of first use ("" = before any `g`)
    std::map<std::string, uint32_t> groupIndex;
    std::string pendingGroup;  // the last `g`, registered by its first face (unused names don't count)
    uint32_t currentGroup = ~0u;
};

// Quad faces were split as (a,b,c) + (a,c,d) into consecutive triangles: keep them paired.
void applyQuads(const ObjObject& o, Mesh& m) {
    for (uint32_t t : o.quads) m.pairQuad(t, t + 1);
    if (m.hasQuads()) m.quadPartners.resize(m.triangles.size(), Mesh::kNoQuad);
}

// UVs from `vt` indices, when any face of the object has them (faces without get (0, 0)).
void applyUvs(ObjObject& o, Mesh& m) {
    if (o.hasUvs) m.uvs = std::move(o.uvs);
}

// Polygroups from `g` names: only when the object uses two or more (ids 1, 2, ... in order of first use).
void applyGroups(const ObjObject& o, Mesh& m) {
    if (o.groupNames.size() < 2) return;
    m.polygroups.resize(o.groups.size());
    for (size_t t = 0; t < o.groups.size(); ++t) m.polygroups[t] = o.groups[t] + 1;
}

bool parseObj(const std::string& path, bool splitObjects, std::vector<MeshObject>& out, std::string& error) {
    std::ifstream file(utf8Path(path), std::ios::binary);
    if (!file) {
        error = "Cannot open " + path;
        return false;
    }
    std::string data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

    std::vector<glm::vec3> positions, colors; // colours: empty until a vertex has one
    std::vector<glm::vec2> texcoords;         // `vt` lines
    std::vector<uint8_t> colored;             // per vertex (while colours exist): had an explicit colour
    std::vector<ObjObject> objects(1);
    std::vector<uint32_t> poly;
    std::vector<int64_t> polyUv; // per corner: index into texcoords, or -1
    const char* p = data.c_str();
    const char* end = p + data.size();
    size_t lineNo = 0;

    auto groupFor = [](ObjObject& o, const std::string& name) {
        auto [it, inserted] = o.groupIndex.try_emplace(name, uint32_t(o.groupNames.size()));
        if (inserted) o.groupNames.push_back(name);
        o.currentGroup = it->second;
    };

    while (p < end) {
        const char* lineEnd = p;
        while (lineEnd < end && *lineEnd != '\n') ++lineEnd;
        ++lineNo;
        std::string line(p, lineEnd); // null-terminated copy for strtof/strtol
        p = lineEnd + 1;

        const char* s = skipSpace(line.c_str(), line.c_str() + line.size());
        if (s[0] == 'v' && (s[1] == ' ' || s[1] == '\t')) {
            char* cur = const_cast<char*>(s + 1);
            glm::vec3 v;
            for (int i = 0; i < 3; ++i) {
                char* next;
                v[i] = std::strtof(cur, &next);
                if (next == cur) {
                    error = "Malformed vertex on line " + std::to_string(lineNo);
                    return false;
                }
                cur = next;
            }
            // Optional "v x y z r g b" vertex colours (MeshLab, scanners), 0..1 display RGB.
            glm::vec3 rgb;
            int channels = 0;
            for (; channels < 3; ++channels) {
                char* next;
                rgb[channels] = std::strtof(cur, &next);
                if (next == cur) break;
                cur = next;
            }
            if (channels == 3) {
                if (colors.size() < positions.size()) {
                    colors.resize(positions.size(), glm::vec3(1.0f));
                    colored.resize(positions.size(), 0);
                }
                colors.push_back(srgbToLinear(rgb));
                colored.push_back(1);
            } else if (!colors.empty()) {
                colors.push_back(glm::vec3(1.0f));
                colored.push_back(0);
            }
            positions.push_back(v);
        } else if (s[0] == 'v' && s[1] == 't' && (s[2] == ' ' || s[2] == '\t')) {
            char* cur = const_cast<char*>(s + 2);
            glm::vec2 uv(0.0f);
            for (int i = 0; i < 2; ++i) { // "vt u" alone means v = 0; a third (w) value is ignored
                char* next;
                const float x = std::strtof(cur, &next);
                if (next == cur) break;
                uv[i] = x;
                cur = next;
            }
            texcoords.push_back(uv);
        } else if (s[0] == 'f' && (s[1] == ' ' || s[1] == '\t')) {
            poly.clear();
            polyUv.clear();
            char* cur = const_cast<char*>(s + 1);
            while (true) {
                char* next;
                long idx = std::strtol(cur, &next, 10);
                if (next == cur) break;
                cur = next;
                // Optional /vt (the /vn after it is ignored); out-of-range texture indices are dropped.
                int64_t uv = -1;
                if (*cur == '/' && cur[1] != '/') {
                    const long t = std::strtol(cur + 1, &next, 10);
                    const long nt = static_cast<long>(texcoords.size());
                    const long resolved = t > 0 ? t - 1 : nt + t;
                    if (next != cur + 1 && t != 0 && resolved >= 0 && resolved < nt) uv = resolved;
                }
                while (*cur && *cur != ' ' && *cur != '\t' && *cur != '\r') ++cur; // skip /vt/vn
                long n = static_cast<long>(positions.size());
                long resolved = idx > 0 ? idx - 1 : n + idx;
                if (idx == 0 || resolved < 0 || resolved >= n) {
                    error = "Invalid face index on line " + std::to_string(lineNo);
                    return false;
                }
                poly.push_back(static_cast<uint32_t>(resolved));
                polyUv.push_back(uv);
            }
            ObjObject& o = objects.back();
            if (poly.size() >= 3 && o.currentGroup == ~0u) groupFor(o, o.pendingGroup);
            if (poly.size() == 4) o.quads.push_back(uint32_t(o.triangles.size())); // n-gons are fanned unpaired
            const bool faceUvs = poly.size() >= 3 && std::all_of(polyUv.begin(), polyUv.end(), [](int64_t i) { return i >= 0; });
            if (faceUvs && !o.hasUvs) {
                o.hasUvs = true;
                o.uvs.resize(o.triangles.size() * 3, glm::vec2(0.0f));
            }
            auto uvAt = [&](size_t corner) { return faceUvs ? texcoords[size_t(polyUv[corner])] : glm::vec2(0.0f); };
            for (size_t i = 2; i < poly.size(); ++i) {
                o.triangles.push_back({poly[0], poly[i - 1], poly[i]});
                o.groups.push_back(o.currentGroup);
                if (o.hasUvs) o.uvs.insert(o.uvs.end(), {uvAt(0), uvAt(i - 1), uvAt(i)});
            }
        } else if (isStatement(s, 'g')) {
            objects.back().pendingGroup = statementName(s);
            objects.back().currentGroup = ~0u;
        } else if (isStatement(s, 'o') && splitObjects) {
            // A new object; an `o` before any faces just names the current one.
            if (!objects.back().triangles.empty()) objects.emplace_back();
            objects.back() = ObjObject{};
            objects.back().name = statementName(s);
        }
    }

    size_t withFaces = 0;
    for (const auto& o : objects) withFaces += !o.triangles.empty();
    if (withFaces == 0) {
        error = "No faces found in " + path;
        return false;
    }
    if (!colors.empty()) {
        colors.resize(positions.size(), glm::vec3(1.0f));
        colored.resize(positions.size(), 0);
    }
    out.clear();
    if (withFaces == 1) {
        // One object: every vertex is kept, as stored.
        for (auto& o : objects) {
            if (o.triangles.empty()) continue;
            MeshObject obj;
            obj.name = o.name;
            obj.mesh.positions = std::move(positions);
            obj.mesh.colors = std::move(colors);
            obj.mesh.triangles = std::move(o.triangles);
            applyGroups(o, obj.mesh);
            applyQuads(o, obj.mesh);
            applyUvs(o, obj.mesh);
            out.push_back(std::move(obj));
        }
    } else {
        // Several: each object gets the vertices its faces use.
        std::vector<uint32_t> local(positions.size(), ~0u);
        for (auto& o : objects) {
            if (o.triangles.empty()) continue;
            MeshObject obj;
            obj.name = o.name;
            Mesh& m = obj.mesh;
            // Vertices keep their file order (so an object written as one block reads back unchanged).
            std::vector<uint32_t> used;
            for (const auto& t : o.triangles)
                for (int k = 0; k < 3; ++k)
                    if (local[t[k]] == ~0u) {
                        local[t[k]] = 0;
                        used.push_back(t[k]);
                    }
            std::sort(used.begin(), used.end());
            bool anyColor = false;
            for (size_t i = 0; i < used.size(); ++i) {
                local[used[i]] = uint32_t(i);
                anyColor |= !colored.empty() && colored[used[i]];
            }
            for (auto& t : o.triangles)
                for (int k = 0; k < 3; ++k) t[k] = local[t[k]];
            m.positions.reserve(used.size());
            for (uint32_t v : used) m.positions.push_back(positions[v]);
            if (anyColor)
                for (uint32_t v : used) m.colors.push_back(colors[v]);
            for (uint32_t v : used) local[v] = ~0u;
            m.triangles = std::move(o.triangles);
            applyGroups(o, m);
            applyQuads(o, m);
            applyUvs(o, m);
            out.push_back(std::move(obj));
        }
    }
    for (auto& obj : out) {
        obj.mesh.rebuildTopology();
        obj.mesh.recomputeAllNormals();
    }
    return true;
}

} // namespace

bool loadObj(const std::string& path, Mesh& out, std::string& error) {
    std::vector<MeshObject> objects;
    if (!parseObj(path, false, objects, error)) return false;
    out = std::move(objects.front().mesh);
    return true;
}

bool loadObj(const std::string& path, std::vector<MeshObject>& out, std::string& error) {
    return parseObj(path, true, out, error);
}

bool saveObj(const std::string& path, const Mesh& mesh, std::string& error) {
    return saveObj(path, std::vector<MeshObjectRef>{{"", &mesh}}, error);
}

bool saveObj(const std::string& path, const std::vector<MeshObjectRef>& objects, std::string& error) {
    if (objects.empty()) {
        error = "Nothing to export";
        return false;
    }
    std::ofstream file(utf8Path(path), std::ios::binary);
    if (!file) {
        error = "Cannot write " + path;
        return false;
    }
    // Textured objects: a PNG each beside the file and a material in <stem>.mtl that maps it.
    const std::vector<std::string> pngs = textureFileNames(path, objects);
    const bool textured = std::any_of(pngs.begin(), pngs.end(), [](const std::string& n) { return !n.empty(); });
    std::vector<std::string> materials(objects.size());
    std::string mtl;
    if (textured) {
        mtl = "# Exported by SculptForge\n";
        // Objects without a texture share a plain clay material (so they don't inherit the one before them).
        const std::string clay = "Clay";
        bool clayUsed = false;
        for (size_t i = 0; i < objects.size(); ++i) {
            if (pngs[i].empty()) {
                materials[i] = clay;
                clayUsed = true;
                continue;
            }
            const std::string base = safeTextureName(objects[i].name);
            std::string name = base;
            for (int k = 2; name == clay || std::find(materials.begin(), materials.end(), name) != materials.end(); ++k)
                name = base + "_" + std::to_string(k);
            materials[i] = name;
            std::string pngPath;
            if (!writeTextureBeside(path, pngs[i], *objects[i].mesh, pngPath, error)) return false;
            mtl += "\nnewmtl " + name + "\nKa 0 0 0\nKd 1 1 1\nKs 0 0 0\nillum 1\nmap_Kd " + pngs[i] + "\n";
            // Material channels: the PBR extension of MTL (Pr / Pm / Ke, map_Pr, map_Pm, map_Ke, norm), as Blender
            // and other importers read it; occlusion has no MTL key and goes out as its file only.
            std::vector<PbrPng> pbr;
            if (!encodePbrPngs(*objects[i].mesh, false, pngs[i], pbr) || !writePbrPngsBeside(path, pbr, error)) {
                if (error.empty()) error = "Cannot encode the material maps of " + objects[i].name;
                return false;
            }
            for (const PbrPng& p : pbr) {
                if (p.slot == "roughness") mtl += "Pr 1\nmap_Pr " + p.name + "\n";
                else if (p.slot == "metallic") mtl += "Pm 1\nmap_Pm " + p.name + "\n";
                else if (p.slot == "normal") mtl += "norm " + p.name + "\nmap_Bump -bm 1 " + p.name + "\n";
                else if (p.slot == "emissive") mtl += "Ke 1 1 1\nmap_Ke " + p.name + "\n";
                else if (p.slot == "opacity") mtl += "map_d " + p.name + "\n";
            }
        }
        if (clayUsed) mtl += "\nnewmtl " + clay + "\nKa 0 0 0\nKd 0.8 0.55 0.45\nKs 0 0 0\nillum 1\n";
        const std::filesystem::path mtlPath = utf8Path(path).parent_path() / utf8Path(sidecarStem(path) + ".mtl");
        std::ofstream mtlFile(mtlPath, std::ios::binary);
        if (!mtlFile || !mtlFile.write(mtl.data(), std::streamsize(mtl.size()))) {
            error = "Cannot write " + textureUtf8(mtlPath);
            return false;
        }
    }

    std::string buf;
    size_t reserve = 0;
    for (const auto& o : objects) reserve += o.mesh->vertexCount() * 40 + o.mesh->triangleCount() * (hasCornerUvs(*o.mesh) ? 90 : 30);
    buf.reserve(reserve);
    buf += "# Exported by SculptForge\n";
    if (textured) buf += "mtllib " + sidecarStem(path) + ".mtl\n";
    char line[192];
    uint32_t first = 1;   // OBJ indices are 1-based and global across objects
    uint32_t firstVt = 1; // as are texture coordinate indices
    for (size_t oi = 0; oi < objects.size(); ++oi) {
        const MeshObjectRef& o = objects[oi];
        const Mesh& mesh = *o.mesh;
        if (objects.size() > 1) {
            std::string name = o.name.empty() ? std::string("Object") : o.name;
            for (char& c : name)
                if (c == '\n' || c == '\r') c = ' ';
            buf += "o " + name + "\n";
        }
        const bool colors = mesh.colors.size() == mesh.positions.size() && mesh.hasColors();
        for (size_t i = 0; i < mesh.positions.size(); ++i) {
            const glm::vec3& v = mesh.positions[i];
            int n;
            if (colors) {
                const glm::vec3 c = linearToSrgb(mesh.colors[i]);
                n = std::snprintf(line, sizeof line, "v %.7g %.7g %.7g %.4f %.4f %.4f\n", v.x, v.y, v.z, c.x, c.y, c.z);
            } else {
                n = std::snprintf(line, sizeof line, "v %.7g %.7g %.7g\n", v.x, v.y, v.z);
            }
            buf.append(line, n);
        }
        // UVs: one `vt` per distinct value (faces index them per corner).
        const bool uvs = hasCornerUvs(mesh);
        std::map<std::pair<float, float>, uint32_t> vtIndex;
        if (uvs)
            for (const auto& uv : mesh.uvs) {
                auto [it, inserted] = vtIndex.try_emplace({uv.x, uv.y}, uint32_t(vtIndex.size()));
                if (!inserted) continue;
                const int n = std::snprintf(line, sizeof line, "vt %.7g %.7g\n", uv.x, uv.y);
                buf.append(line, n);
            }
        if (textured) buf += "usemtl " + materials[oi] + "\n";
        // Polygroups as `g` groups (ZBrush's OBJ convention), when there is more than one.
        bool groups = false;
        for (uint32_t t = 1; t < mesh.triangleCount() && !groups; ++t) groups = mesh.polygroupAt(t) != mesh.polygroupAt(0);
        uint32_t written = 0; // group of the last face written
        for (uint32_t t = 0; t < mesh.triangleCount(); ++t) {
            glm::uvec4 f;
            glm::vec2 fuv[4];
            const int corners = exportFaceUv(mesh, t, f, fuv); // quads as quads, the second half of one as nothing
            if (corners == 0) continue;
            if (groups && (t == 0 || mesh.polygroupAt(t) != written)) {
                written = mesh.polygroupAt(t);
                const int n = std::snprintf(line, sizeof line, "g group%u\n", written);
                buf.append(line, n);
            }
            if (uvs) {
                buf += 'f';
                for (int c = 0; c < corners; ++c) {
                    const int n = std::snprintf(line, sizeof line, " %u/%u", f[c] + first, vtIndex[{fuv[c].x, fuv[c].y}] + firstVt);
                    buf.append(line, n);
                }
                buf += '\n';
                continue;
            }
            const int n = corners == 4 ? std::snprintf(line, sizeof line, "f %u %u %u %u\n", f.x + first, f.y + first,
                                                       f.z + first, f.w + first)
                                       : std::snprintf(line, sizeof line, "f %u %u %u\n", f.x + first, f.y + first, f.z + first);
            buf.append(line, n);
        }
        first += uint32_t(mesh.vertexCount());
        firstVt += uint32_t(vtIndex.size());
    }
    file.write(buf.data(), static_cast<std::streamsize>(buf.size()));
    if (!file) {
        error = "Write failed for " + path;
        return false;
    }
    return true;
}

} // namespace sf
