// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// GoZ reader/writer and bridge. The format notes come from GoB, the GPL
// Blender add-on (github.com/JoseConseco/GoB: gob_import.py, gob_export.py,
// paths.py); docs/goz.md describes the layout.
#include "io/GozIO.h"

#include "io/TextureExport.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>

namespace sf {

namespace {

namespace fs = std::filesystem;

fs::path utf8Path(const std::string& s) { return fs::path(std::u8string(s.begin(), s.end())); }
std::string pathUtf8(const fs::path& p) {
    auto u = p.u8string();
    return std::string(u.begin(), u.end());
}
// GoZ text files use forward slashes on every platform (GoB writes them that way).
std::string genericUtf8(const fs::path& p) {
    auto u = p.generic_u8string();
    return std::string(u.begin(), u.end());
}

// Section tags (little-endian uint32 values as GoB writes them).
constexpr uint32_t kTagMeshName = 0x1389;
constexpr uint32_t kTagSubdivision = 0x138a;
constexpr uint32_t kTagVertices = 0x2711;
constexpr uint32_t kTagFaces = 0x4e21;
constexpr uint32_t kTagUvs = 0x61a9;
constexpr uint32_t kTagMask = 0x7532;
constexpr uint32_t kTagPolypaint = 0x88b9;
constexpr uint32_t kTagPolygroups = 0x9c41;
constexpr uint32_t kTagDiffuseMap = 0xafc9;
constexpr uint32_t kTagNormalMap = 0xc351;
constexpr uint32_t kTagDisplacementMap = 0xd6d9;
constexpr uint32_t kNoIndex = 0xffffffffu;

const char kHeader[] = "GoZb 1.0 ZBrush GoZ Binary";
constexpr size_t kHeaderSize = 36; // the text, six '.' and the uint32 1 that tags the name record
constexpr char kNamePrefix[] = "GoZMesh_";

// ZBrush is Y-down; SculptForge (like OpenGL/glTF) is Y-up. GoB maps GoZ
// (x, y, z) to Blender (x, z, -y); in Y-up terms that is (x, -y, -z), a
// rotation (its own inverse) that keeps the winding.
glm::vec3 fromZBrush(const glm::vec3& p) { return {p.x, -p.y, -p.z}; }
glm::vec3 toZBrush(const glm::vec3& p) { return {p.x, -p.y, -p.z}; }

class Reader {
public:
    explicit Reader(std::span<const uint8_t> d) : data_(d) {}
    size_t pos() const { return pos_; }
    size_t remaining() const { return data_.size() - pos_; }
    void seek(size_t p) { pos_ = std::min(p, data_.size()); }
    bool u32(uint32_t& v) {
        if (remaining() < 4) return false;
        v = uint32_t(data_[pos_]) | uint32_t(data_[pos_ + 1]) << 8 | uint32_t(data_[pos_ + 2]) << 16 |
            uint32_t(data_[pos_ + 3]) << 24;
        pos_ += 4;
        return true;
    }
    bool u64(uint64_t& v) {
        uint32_t lo, hi;
        if (remaining() < 8 || !u32(lo) || !u32(hi)) return false;
        v = uint64_t(lo) | uint64_t(hi) << 32;
        return true;
    }
    const uint8_t* at(size_t p) const { return data_.data() + p; }

private:
    std::span<const uint8_t> data_;
    size_t pos_ = 0;
};

uint32_t readU32(const uint8_t* p) {
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}
uint16_t readU16(const uint8_t* p) { return uint16_t(p[0] | p[1] << 8); }
float readF32(const uint8_t* p) {
    uint32_t bits = readU32(p);
    float f;
    std::memcpy(&f, &bits, 4);
    return f;
}

class Writer {
public:
    std::vector<uint8_t> bytes;
    void u8(uint8_t v) { bytes.push_back(v); }
    void u16(uint16_t v) {
        bytes.push_back(uint8_t(v & 0xff));
        bytes.push_back(uint8_t(v >> 8));
    }
    void u32(uint32_t v) {
        for (int i = 0; i < 4; ++i) bytes.push_back(uint8_t(v >> (8 * i)));
    }
    void u64(uint64_t v) {
        u32(uint32_t(v & 0xffffffffu));
        u32(uint32_t(v >> 32));
    }
    void f32(float f) {
        uint32_t bits;
        std::memcpy(&bits, &f, 4);
        u32(bits);
    }
    void raw(const void* p, size_t n) {
        auto* b = static_cast<const uint8_t*>(p);
        bytes.insert(bytes.end(), b, b + n);
    }
    // Section header: tag, total length (tag + length + count + payload), element count.
    void section(uint32_t tag, size_t payloadBytes, uint64_t count) {
        u32(tag);
        u32(uint32_t(payloadBytes + 16));
        u64(count);
    }
};

// A GoZ face: 4 indices, the fourth kNoIndex for triangles.
struct Face {
    uint32_t v[4];
    bool quad() const { return v[3] != kNoIndex; }
};

std::string printable(const uint8_t* p, size_t n) {
    std::string s;
    for (size_t i = 0; i < n; ++i)
        if (p[i] >= 0x20 && p[i] < 0x7f) s += char(p[i]);
    while (!s.empty() && s.back() == ' ') s.pop_back();
    return s;
}

} // namespace

std::string gozObjectName(const std::string& name) {
    std::string out;
    for (char c : name) {
        const bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
        if (ok) out += c;
        else if (out.empty() || out.back() != '_') out += '_';
    }
    while (!out.empty() && (out.front() == '_' || out.front() == '-')) out.erase(out.begin());
    while (!out.empty() && (out.back() == '_' || out.back() == '-')) out.pop_back();
    return out.empty() ? "Object" : out;
}

bool parseGoz(std::span<const uint8_t> data, Mesh& out, std::string& error, GozInfo* info) {
    GozInfo local;
    GozInfo& gi = info ? *info : local;
    gi = GozInfo{};
    if (data.size() < kHeaderSize + 4 || std::memcmp(data.data(), "GoZb", 4) != 0) {
        error = "Not a GoZ file (missing \"GoZb\" header)";
        return false;
    }
    Reader r(data);
    r.seek(kHeaderSize);
    // Object name record: length (counted from the tag at offset 32), uint64 1, "GoZMesh_<name>".
    uint32_t nameLen = 0;
    if (!r.u32(nameLen) || nameLen < 16 || nameLen - 8 > r.remaining()) {
        error = "GoZ file has a truncated object name";
        return false;
    }
    {
        const size_t start = r.pos();
        const size_t n = nameLen - 8;
        if (n >= 8 + 8 && std::memcmp(r.at(start + 8), kNamePrefix, 8) == 0)
            gi.name = printable(r.at(start + 16), n - 16);
        r.seek(start + n);
    }

    std::vector<glm::vec3> positions;
    std::vector<Face> faces;
    std::vector<glm::vec2> faceUvs; // 4 per face
    std::vector<uint16_t> groups;
    std::vector<float> mask;
    std::vector<glm::vec3> colors;

    uint32_t tag = 0;
    while (r.u32(tag) && tag != 0) {
        uint32_t length = 0;
        if (!r.u32(length) || length < 8) break;
        const size_t bodyStart = r.pos();
        const size_t bodyLen = std::min<size_t>(length - 8, r.remaining());
        const size_t next = bodyStart + bodyLen;
        // Element sections: uint64 count then a payload of length - 16 bytes.
        uint64_t count = 0;
        const bool counted = bodyLen >= 8 && r.u64(count);
        const uint8_t* payload = counted ? r.at(bodyStart + 8) : nullptr;
        const size_t payloadLen = counted ? bodyLen - 8 : 0;
        auto records = [&](size_t recordSize) { return size_t(std::min<uint64_t>(count, payloadLen / recordSize)); };

        switch (tag) {
        case kTagVertices: {
            const size_t n = records(12);
            for (size_t i = 0; i < n; ++i) {
                const uint8_t* p = payload + i * 12;
                positions.push_back(fromZBrush({readF32(p), readF32(p + 4), readF32(p + 8)}));
            }
            break;
        }
        case kTagFaces: {
            const size_t n = records(16);
            for (size_t i = 0; i < n; ++i) {
                Face f;
                for (int c = 0; c < 4; ++c) f.v[c] = readU32(payload + i * 16 + size_t(c) * 4);
                faces.push_back(f);
            }
            break;
        }
        case kTagUvs: {
            const size_t n = records(32);
            for (size_t i = 0; i < n * 4; ++i) {
                const uint8_t* p = payload + i * 8;
                // GoZ stores V downwards (GoB flips it by default both ways).
                faceUvs.push_back({readF32(p), 1.0f - readF32(p + 4)});
            }
            break;
        }
        case kTagPolypaint: {
            const size_t n = records(4);
            for (size_t i = 0; i < n; ++i) {
                const uint8_t* p = payload + i * 4; // B, G, R, (alpha/unused), sRGB
                colors.push_back(srgbToLinear(glm::vec3(p[2], p[1], p[0]) / 255.0f));
            }
            break;
        }
        case kTagMask: {
            const size_t n = records(2);
            for (size_t i = 0; i < n; ++i) mask.push_back(1.0f - float(readU16(payload + i * 2)) / 65535.0f);
            break;
        }
        case kTagPolygroups: {
            const size_t n = records(2);
            for (size_t i = 0; i < n; ++i) groups.push_back(readU16(payload + i * 2));
            break;
        }
        case kTagSubdivision:
            if (records(16) > 0) gi.subdivisionLevel = int(readU32(payload + (records(16) - 1) * 16));
            break;
        case kTagDiffuseMap:
        case kTagNormalMap:
        case kTagDisplacementMap: {
            std::string p = counted ? std::string(reinterpret_cast<const char*>(payload), payloadLen) : std::string();
            while (!p.empty() && p.back() == '\0') p.pop_back();
            (tag == kTagDiffuseMap ? gi.diffuseMap : tag == kTagNormalMap ? gi.normalMap : gi.displacementMap) = p;
            break;
        }
        default: break; // kTagMeshName and unknown sections are skipped
        }
        r.seek(next);
    }

    if (positions.empty() || faces.empty()) {
        error = "GoZ file has no " + std::string(positions.empty() ? "vertices" : "faces");
        return false;
    }

    // Triangulate: quad (a,b,c,d) -> (a,b,c), (a,c,d). Faces with bad
    // indices and degenerate triangles are dropped.
    Mesh m;
    m.positions = std::move(positions);
    const uint32_t nv = uint32_t(m.positions.size());
    const bool haveUvs = faceUvs.size() == faces.size() * 4;
    const bool haveGroups = groups.size() == faces.size();
    size_t skipped = 0;
    for (size_t fi = 0; fi < faces.size(); ++fi) {
        const Face& f = faces[fi];
        const bool quad = f.quad();
        const int corners = quad ? 4 : 3;
        bool valid = true;
        for (int c = 0; c < corners; ++c) valid = valid && f.v[c] < nv;
        if (!valid) {
            ++skipped;
            continue;
        }
        if (quad) ++gi.quads;
        else ++gi.triangles;
        const int tris[2][3] = {{0, 1, 2}, {0, 2, 3}};
        const size_t firstTri = m.triangles.size();
        for (int t = 0; t < (quad ? 2 : 1); ++t) {
            glm::uvec3 tri(f.v[tris[t][0]], f.v[tris[t][1]], f.v[tris[t][2]]);
            if (tri.x == tri.y || tri.y == tri.z || tri.x == tri.z) continue;
            m.triangles.push_back(tri);
            if (haveGroups) m.polygroups.push_back(groups[fi]);
            if (haveUvs)
                for (int c = 0; c < 3; ++c) m.uvs.push_back(faceUvs[fi * 4 + size_t(tris[t][c])]);
        }
        if (m.triangles.size() == firstTri + 2) m.pairQuad(uint32_t(firstTri), uint32_t(firstTri + 1));
    }
    if (m.hasQuads()) m.quadPartners.resize(m.triangles.size(), Mesh::kNoQuad);
    if (m.triangles.empty()) {
        error = "GoZ file has no valid faces";
        return false;
    }
    if (mask.size() == nv && std::any_of(mask.begin(), mask.end(), [](float v) { return v > 0.0f; }))
        m.mask = std::move(mask);
    if (colors.size() == nv) m.colors = std::move(colors);
    m.rebuildTopology();
    m.recomputeAllNormals();
    if (skipped) error = std::to_string(skipped) + " GoZ faces with out-of-range indices were skipped";
    out = std::move(m);
    return true;
}

std::vector<uint8_t> writeGoz(const Mesh& mesh, const std::string& name, const GozWriteOptions& options) {
    const std::string objName = gozObjectName(name);
    const size_t nv = mesh.vertexCount(), nt = mesh.triangleCount();
    const bool uvs = mesh.uvs.size() == nt * 3;
    const bool groups = mesh.polygroups.size() == nt;

    // Faces (and their 4 UV corners), quads written back as quads.
    std::vector<Face> faces;
    std::vector<glm::vec2> faceUvs;
    std::vector<uint16_t> faceGroups;
    faces.reserve(nt);
    // GoZ polygroups are 16-bit. Truncating larger ids would merge unrelated groups,
    // so when any id is too big, number the distinct ids 0, 1, 2... instead (only
    // more than 65536 distinct groups still have to share ids).
    std::vector<uint32_t> groupIds;
    if (groups && std::any_of(mesh.polygroups.begin(), mesh.polygroups.end(), [](uint32_t g) { return g > 0xffffu; })) {
        groupIds = mesh.polygroups;
        std::sort(groupIds.begin(), groupIds.end());
        groupIds.erase(std::unique(groupIds.begin(), groupIds.end()), groupIds.end());
    }
    auto gozGroup = [&](uint32_t g) {
        if (groupIds.empty()) return uint16_t(g);
        return uint16_t(size_t(std::lower_bound(groupIds.begin(), groupIds.end(), g) - groupIds.begin()) & 0xffffu);
    };
    // Meshes that keep their quads (Mesh::quadPartners) write exactly those; others (older projects) fall back
    // to pairing consecutive (a,b,c),(a,c,d) triangles.
    const bool keptQuads = mesh.hasQuads();
    auto uvOf = [&](uint32_t tri, uint32_t v) {
        const glm::uvec3& tv = mesh.triangles[tri];
        return mesh.uvs[tri * 3 + (tv.x == v ? 0 : tv.y == v ? 1 : 2)];
    };
    // A kept quad starting at `t` (exportFace(t) == 4) is written as one face unless a UV seam runs along its
    // diagonal (GoZ has one UV per face corner); then both halves are written as triangles.
    auto quadUvsMatch = [&](uint32_t t, const glm::uvec4& q) {
        if (!uvs) return true;
        const uint32_t u = mesh.quadPartnerAt(t);
        return uvOf(u, q.x) == uvOf(t, q.x) && uvOf(u, q.z) == uvOf(t, q.z);
    };
    for (size_t t = 0; t < nt; ++t) {
        const glm::uvec3& a = mesh.triangles[t];
        const glm::vec2* ua = uvs ? &mesh.uvs[t * 3] : nullptr;
        bool paired = false;
        if (options.quads && keptQuads) {
            glm::uvec4 q;
            const int corners = mesh.exportFace(uint32_t(t), q);
            if (corners == 0) {
                const uint32_t first = mesh.quadPartnerAt(uint32_t(t));
                mesh.exportFace(first, q);
                if (quadUvsMatch(first, q)) continue; // written with its partner
            } else if (corners == 4 && quadUvsMatch(uint32_t(t), q)) {
                paired = true;
                faces.push_back({{q.x, q.y, q.z, q.w}});
                if (uvs) {
                    const uint32_t u = mesh.quadPartnerAt(uint32_t(t));
                    faceUvs.insert(faceUvs.end(), {uvOf(uint32_t(t), q.x), uvOf(uint32_t(t), q.y), uvOf(uint32_t(t), q.z), uvOf(u, q.w)});
                }
            }
        } else if (options.quads && t + 1 < nt) {
            const glm::uvec3& b = mesh.triangles[t + 1];
            const glm::vec2* ub = uvs ? &mesh.uvs[(t + 1) * 3] : nullptr;
            paired = b.x == a.x && b.y == a.z && b.z != a.y && b.z != a.x && b.z != a.z &&
                     (!groups || mesh.polygroups[t] == mesh.polygroups[t + 1]) &&
                     (!uvs || (ub[0] == ua[0] && ub[1] == ua[2]));
            if (paired) {
                faces.push_back({{a.x, a.y, a.z, b.z}});
                if (uvs) faceUvs.insert(faceUvs.end(), {ua[0], ua[1], ua[2], ub[2]});
            }
        }
        if (!paired) {
            faces.push_back({{a.x, a.y, a.z, kNoIndex}});
            if (uvs) faceUvs.insert(faceUvs.end(), {ua[0], ua[1], ua[2], glm::vec2(0.0f, 0.0f)}); // GoB pads with (0, 1) in GoZ space
        }
        if (groups) faceGroups.push_back(gozGroup(mesh.polygroups[t]));
        if (paired && !keptQuads) ++t;
    }

    Writer w;
    w.raw(kHeader, sizeof(kHeader) - 1);
    w.raw("......", 6);
    w.u32(1);
    w.u32(uint32_t(objName.size() + 24));
    w.u64(1);
    w.raw(kNamePrefix, 8);
    w.raw(objName.data(), objName.size());
    // Mesh name record exactly as GoB writes it.
    w.u32(kTagMeshName);
    w.u32(20);
    w.u64(1);
    w.u32(0);

    w.section(kTagVertices, nv * 12, nv);
    for (const auto& p : mesh.positions) {
        glm::vec3 z = toZBrush(p);
        w.f32(z.x);
        w.f32(z.y);
        w.f32(z.z);
    }
    w.section(kTagFaces, faces.size() * 16, faces.size());
    for (const Face& f : faces)
        for (uint32_t v : f.v) w.u32(v);
    if (uvs) {
        w.section(kTagUvs, faces.size() * 32, faces.size());
        for (const auto& uv : faceUvs) {
            w.f32(uv.x);
            w.f32(1.0f - uv.y);
        }
    }
    if (mesh.colors.size() == nv) {
        // GoB writes the count as uint32 + float 0 (same 8 bytes as a uint64).
        w.section(kTagPolypaint, nv * 4, nv);
        for (const auto& c : mesh.colors) {
            glm::vec3 s = glm::clamp(linearToSrgb(c), 0.0f, 1.0f) * 255.0f + 0.5f;
            w.u8(uint8_t(s.z));
            w.u8(uint8_t(s.y));
            w.u8(uint8_t(s.x));
            w.u8(0);
        }
    }
    if (mesh.mask.size() == nv) {
        w.section(kTagMask, nv * 2, nv);
        for (float m : mesh.mask) w.u16(uint16_t((1.0f - std::clamp(m, 0.0f, 1.0f)) * 65535.0f + 0.5f));
    }
    if (groups) {
        w.section(kTagPolygroups, faceGroups.size() * 2, faceGroups.size());
        for (uint16_t g : faceGroups) w.u16(g);
    }
    if (!options.diffuseMap.empty()) { // as GoB writes texture paths: count 1, then the path
        w.section(kTagDiffuseMap, options.diffuseMap.size(), 1);
        w.raw(options.diffuseMap.data(), options.diffuseMap.size());
    }
    // End: 16 zero bytes (a zero tag).
    for (int i = 0; i < 16; ++i) w.u8(0);
    return std::move(w.bytes);
}

bool loadGoz(const std::string& path, Mesh& out, std::string& error, GozInfo* info) {
    std::ifstream f(utf8Path(path), std::ios::binary);
    if (!f) {
        error = "Cannot open " + path;
        return false;
    }
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    std::string err;
    if (!parseGoz(data, out, err, info)) {
        error = err + ": " + path;
        return false;
    }
    error = err; // warnings, if any
    return true;
}

bool saveGoz(const std::string& path, const Mesh& mesh, const std::string& name, std::string& error,
             const GozWriteOptions& options) {
    if (mesh.triangleCount() == 0) {
        error = "Nothing to export: the mesh is empty";
        return false;
    }
    GozWriteOptions o = options;
    if (o.diffuseMap.empty() && hasTexture(mesh)) {
        // The texture as <stem>_color.png beside the file, recorded as the diffuse map.
        std::string png;
        const auto refs = std::vector<MeshObjectRef>{{name, &mesh}};
        if (!writeTextureBeside(path, textureFileNames(path, refs)[0], mesh, png, error)) return false;
        std::error_code ec;
        const fs::path absolute = fs::absolute(utf8Path(png), ec);
        o.diffuseMap = ec ? png : genericUtf8(absolute);
    }
    std::vector<uint8_t> bytes = writeGoz(mesh, name, o);
    std::ofstream f(utf8Path(path), std::ios::binary);
    if (!f || !f.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()))) {
        error = "Cannot write " + path;
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Bridge
// ---------------------------------------------------------------------------

namespace {

std::string g_appPath;

#ifdef _WIN32
std::string envUtf8(const wchar_t* name) {
    wchar_t* value = nullptr;
    size_t len = 0;
    if (_wdupenv_s(&value, &len, name) != 0 || !value) return {};
    std::string s = pathUtf8(fs::path(value));
    std::free(value);
    return s;
}
#endif

bool writeText(const fs::path& p, const std::string& text) {
    std::ofstream f(p, std::ios::binary);
    return f && f.write(text.data(), std::streamsize(text.size()));
}

#if defined(_WIN32) || defined(__APPLE__)
bool launchDetached(const std::string& target) {
#ifdef _WIN32
    const std::string cmd = "start \"\" \"" + target + "\"";
#else
    const std::string cmd = "open \"" + target + "\"";
#endif
    return std::system(cmd.c_str()) == 0;
}

// Newest ZBrush in the default install locations (GoB's find_zbrush).
std::string findZBrush() {
    std::error_code ec;
#ifdef _WIN32
    const fs::path bases[] = {"C:\\Program Files", "C:\\Program Files\\Pixologic", "C:\\Program Files\\Maxon"};
    const char* exe = "ZBrush.exe";
#else
    const fs::path bases[] = {"/Applications"};
    const char* exe = "ZBrush.app";
#endif
    std::vector<fs::path> found;
    for (const auto& base : bases) {
        if (!fs::is_directory(base, ec)) continue;
        for (const auto& entry : fs::directory_iterator(base, ec)) {
            std::string n = pathUtf8(entry.path().filename());
            std::transform(n.begin(), n.end(), n.begin(), [](unsigned char c) { return char(std::tolower(c)); });
            if (n.find("zbrush") == std::string::npos) continue;
            if (n == "zbrush.app") found.push_back(entry.path());
            else if (fs::exists(entry.path() / exe, ec)) found.push_back(entry.path() / exe);
        }
    }
    if (found.empty()) return {};
    std::sort(found.begin(), found.end()); // version numbers sort last-is-newest, like GoB's max()
    return pathUtf8(found.back());
}
#endif

} // namespace

std::string gozDefaultRoot() {
#ifdef _WIN32
    std::string pub = envUtf8(L"PUBLIC");
    if (pub.empty()) pub = "C:\\Users\\Public";
    return pathUtf8(utf8Path(pub) / "Pixologic");
#elif defined(__APPLE__)
    return "/Users/Shared/Pixologic";
#else
    return {};
#endif
}

std::string gozUnavailableMessage() {
    return "Send to ZBrush needs ZBrush, which runs only on Windows and macOS. Use Export / Import GoZ-compatible file "
           "to move .GoZ files by hand, or set a bridge folder (e.g. a Wine prefix's Users/Public/Pixologic).";
}

void setGozApplicationPath(std::string executable) { g_appPath = std::move(executable); }

std::string gozObjectListPath(const std::string& root) {
    return root.empty() ? std::string() : pathUtf8(utf8Path(root) / "GoZBrush" / "GoZ_ObjectList.txt");
}

bool gozShellSafePath(const std::string& path) {
    for (unsigned char c : path) {
        if (c < 0x20 || c == 0x7f || c == '"' || c == '$' || c == '`' || c == '%' || c == '!') return false;
#ifndef _WIN32
        if (c == '\\') return false;
#endif
    }
    return true;
}

bool gozSend(const Mesh& mesh, const GozSendOptions& o, std::string& message) {
    return gozSend(std::vector<MeshObjectRef>{{o.name.empty() ? "SculptForge" : o.name, &mesh}}, o, message);
}

bool gozSend(const std::vector<MeshObjectRef>& objects, const GozSendOptions& o, std::string& message,
             std::vector<std::string>* sentNames) {
    const std::string root = o.root.empty() ? gozDefaultRoot() : o.root;
    if (root.empty()) {
        message = gozUnavailableMessage();
        return false;
    }
    std::error_code ec;
    if (o.launch) { // these paths reach the shell (launchDetached)
        if (!gozShellSafePath(root)) {
            message = "The bridge folder path contains characters that are not allowed when launching ZBrush";
            return false;
        }
        if (!o.zbrushPath.empty()) {
            std::string file = pathUtf8(utf8Path(o.zbrushPath).filename());
            std::transform(file.begin(), file.end(), file.begin(), [](unsigned char c) { return char(std::tolower(c)); });
            if (!gozShellSafePath(o.zbrushPath) || file.find("zbrush") == std::string::npos ||
                !fs::exists(utf8Path(o.zbrushPath), ec)) {
                message = "Not a ZBrush executable: " + o.zbrushPath;
                return false;
            }
        }
    }
    if (objects.empty()) {
        message = "Nothing to send: no visible Parts";
        return false;
    }
    for (const auto& obj : objects)
        if (!obj.mesh || obj.mesh->triangleCount() == 0) {
            message = "Nothing to send: the mesh is empty";
            return false;
        }
    const fs::path rootPath = utf8Path(root);
    const fs::path brush = rootPath / "GoZBrush";
    const fs::path project = rootPath / "GoZProjects" / "Default";
    const fs::path app = rootPath / "GoZApps" / "SculptForge";
    for (const auto& d : {brush, project, app}) fs::create_directories(d, ec);
    if (!fs::is_directory(project, ec)) {
        message = "Cannot create the bridge folder " + pathUtf8(project);
        return false;
    }
    // One .GoZ per object (ZBrush makes each a SubTool), all listed in GoZ_ObjectList.txt.
    std::vector<std::string> names;
    std::string list;
    bool ok = true;
    for (const auto& obj : objects) {
        const std::string wanted = gozObjectName(obj.name.empty() ? "SculptForge" : obj.name);
        std::string name = wanted;
        for (int k = 2; std::find(names.begin(), names.end(), name) != names.end(); ++k) name = wanted + "_" + std::to_string(k);
        names.push_back(name);
        // Object paths are written without the .GoZ extension, ZBrush appends it.
        const std::string base = genericUtf8(project / name);
        std::string err;
        if (!saveGoz(pathUtf8(project / (name + ".GoZ")), *obj.mesh, name, err)) {
            message = err;
            return false;
        }
        ok = ok && writeText(project / (name + ".ztn"), base);
        list += base + "\n";
    }
    ok = ok && writeText(brush / "GoZ_ObjectList.txt", list) && writeText(brush / "GoZ_Application.txt", "SculptForge") &&
              writeText(brush / "GoZ_ProjectPath.txt", genericUtf8(project) + "/");
    // Register SculptForge in ZBrush's GoZ application list.
    if (!fs::exists(app / "GoZ_Info.txt", ec))
        ok = ok && writeText(app / "GoZ_Info.txt",
                             "NAME\t\t\t=\t\"SculptForge\"\nGOZ_VERSION\t\t=\t1\nEXTENSION\t\t=\t\".GoZ\"\n"
                             "TAMPLATE\t\t=\t\"GoZ Complete Binary.GoZ\"\n\nEXPORT_FLIP_Y\t\t\t=\tFALSE\n"
                             "EXPORT_FLIP_Z\t\t\t=\tFALSE\nIMPORT_FLIP_Y\t\t\t=\tFALSE\nIMPORT_FLIP_Z\t\t\t=\tFALSE\n");
    if (!g_appPath.empty())
        ok = ok && writeText(app / "GoZ_Config.txt", "PATH = \"" + genericUtf8(utf8Path(g_appPath)) + "\"");
    if (!ok) {
        message = "Cannot write the GoZ bridge files in " + root;
        return false;
    }
    message = "Sent " + names.front() + (names.size() > 1 ? " and " + std::to_string(names.size() - 1) + " more Parts" : "") +
              " to " + pathUtf8(project);
    if (sentNames) *sentNames = names;
    if (!o.launch) return true;

    // Official GoZ plug-ins notify a running ZBrush with GoZBrushFromApp; GoB
    // starts ZBrush itself. Do the first, fall back to the second.
#ifdef _WIN32
    const fs::path notifier = brush / "GoZBrushFromApp.exe";
#else
    const fs::path notifier = brush / "GoZBrushFromApp.app";
#endif
#if defined(_WIN32) || defined(__APPLE__)
    if (fs::exists(notifier, ec)) {
        launchDetached(pathUtf8(notifier));
        message += "; ZBrush notified";
        return true;
    }
    const std::string zbrush = o.zbrushPath.empty() ? findZBrush() : o.zbrushPath;
    if (!zbrush.empty() && launchDetached(zbrush)) {
        message += "; started " + zbrush + " (use GoZ in ZBrush to import it)";
        return true;
    }
    message += "; ZBrush was not found (install GoZ from ZBrush's Preferences > GoZ, then press GoZ in ZBrush)";
#else
    (void)notifier;
    message += "; " + gozUnavailableMessage();
#endif
    return true;
}

namespace {

// Appends one attribute; a side that lacks it gets `fill` (`dn`, `sn` = element counts).
template <class T>
void appendAttribute(std::vector<T>& d, size_t dn, const std::vector<T>& s, size_t sn, const T& fill) {
    if (d.empty() && s.empty()) return;
    d.resize(dn, fill);
    if (s.empty()) d.resize(dn + sn, fill);
    else d.insert(d.end(), s.begin(), s.end());
}

// Appends `src` to `dst`, filling attributes the other side lacks with defaults.
void appendMesh(Mesh& dst, const Mesh& src) {
    const uint32_t off = uint32_t(dst.vertexCount());
    const size_t dv = dst.vertexCount(), dt = dst.triangleCount();
    const size_t sv = src.vertexCount(), st = src.triangleCount();
    appendQuads(dst, dt, src);
    dst.positions.insert(dst.positions.end(), src.positions.begin(), src.positions.end());
    for (const auto& t : src.triangles) dst.triangles.push_back(t + glm::uvec3(off));
    appendAttribute(dst.mask, dv, src.mask, sv, 0.0f);
    appendAttribute(dst.colors, dv, src.colors, sv, glm::vec3(1.0f));
    appendAttribute(dst.polygroups, dt, src.polygroups, st, 0u);
    appendAttribute(dst.uvs, dt * 3, src.uvs, st * 3, glm::vec2(0.0f));
}

} // namespace

bool gozReceive(const std::string& rootIn, std::vector<MeshObject>& out, std::string& message) {
    const std::string root = rootIn.empty() ? gozDefaultRoot() : rootIn;
    if (root.empty()) {
        message = gozUnavailableMessage();
        return false;
    }
    std::ifstream list(utf8Path(gozObjectListPath(root)), std::ios::binary);
    if (!list) {
        message = "Nothing to receive: " + gozObjectListPath(root) + " does not exist (press GoZ in ZBrush first)";
        return false;
    }
    std::vector<MeshObject> loaded;
    std::string line, problems;
    while (std::getline(list, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\0')) line.pop_back();
        if (line.empty()) continue;
        std::string path = line;
        std::string lower = path;
        std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return char(std::tolower(c)); });
        if (lower.size() < 4 || lower.compare(lower.size() - 4, 4, ".goz") != 0) path += ".GoZ";
        Mesh m;
        GozInfo info;
        std::string err;
        if (!loadGoz(path, m, err, &info)) {
            problems += (problems.empty() ? "" : "; ") + err;
            continue;
        }
        loaded.push_back({info.name.empty() ? pathUtf8(utf8Path(path).stem()) : info.name, std::move(m)});
    }
    if (loaded.empty()) {
        message = problems.empty() ? "Nothing to receive: GoZ_ObjectList.txt is empty" : problems;
        return false;
    }
    size_t tris = 0;
    for (const auto& obj : loaded) tris += obj.mesh.triangleCount();
    message = "Received " + loaded.front().name;
    if (loaded.size() > 1) message += " and " + std::to_string(loaded.size() - 1) + " more Parts";
    message += " from ZBrush (" + std::to_string(tris) + " triangles)";
    if (!problems.empty()) message += "; skipped: " + problems;
    out = std::move(loaded);
    return true;
}

bool gozReceive(const std::string& root, Mesh& out, std::string& message, std::vector<std::string>* names) {
    std::vector<MeshObject> objects;
    if (!gozReceive(root, objects, message)) return false;
    Mesh merged;
    if (names) names->clear();
    for (const auto& obj : objects) {
        appendMesh(merged, obj.mesh);
        if (names) names->push_back(obj.name);
    }
    merged.rebuildTopology();
    merged.recomputeAllNormals();
    out = std::move(merged);
    if (objects.size() > 1) {
        const size_t from = message.find(" more Parts");
        if (from != std::string::npos) message.insert(from + 14, " (merged)");
    }
    return true;
}

std::string zbrushProprietaryMessage(const std::string& extension) {
    std::string ext = extension;
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return char(std::toupper(c)); });
    if (ext == ".ZTL")
        return ".ZTL is ZBrush's proprietary format and cannot be read directly. Convert it once in ZBrush with the "
               "SculptForge ZTL Export plug-in (Help > User Guide > Coming from ZBrush), then open the .ztl again; "
               "or send the model with GoZ (File > Send to ZBrush > Receive from ZBrush).";
    return ext + " is ZBrush's proprietary format and cannot be read. In ZBrush, send the model with GoZ "
                 "(File > Send to ZBrush > Receive from ZBrush) or export it as OBJ or FBX "
                 "(Tool > Export / the FBX ExportImport plug-in) and open that instead.";
}

namespace {

std::string lowerAscii(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return s;
}

// "Dragon.sf007_Body" -> 7 chars of prefix: the length of "<stem>.sf<digits>_" when `name` starts with it.
size_t ztoolPrefixLength(const std::string& name, const std::string& stem) {
    const std::string head = lowerAscii(stem) + ".sf";
    if (name.size() <= head.size() || lowerAscii(name.substr(0, head.size())) != head) return 0;
    size_t i = head.size();
    while (i < name.size() && std::isdigit(static_cast<unsigned char>(name[i]))) ++i;
    return i > head.size() && i + 1 < name.size() && name[i] == '_' ? i + 1 : 0;
}

} // namespace

std::vector<std::string> convertedZToolFiles(const std::string& path) {
    std::error_code ec;
    const fs::path p = utf8Path(path);
    std::vector<fs::path> files;
    auto scan = [&](const fs::path& dir, const std::string& stem) {
        for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
            if (lowerAscii(pathUtf8(it->path().extension())) != ".goz" || !it->is_regular_file(ec)) continue;
            if (!stem.empty() && ztoolPrefixLength(pathUtf8(it->path().stem()), stem) == 0) continue;
            files.push_back(it->path());
        }
    };
    if (fs::is_directory(p, ec)) {
        scan(p, "");
    } else if (lowerAscii(pathUtf8(p.extension())) == ".ztl") {
        scan(p.has_parent_path() ? p.parent_path() : fs::path("."), pathUtf8(p.stem()));
        if (files.empty()) {
            fs::path folder = p;
            folder.replace_extension(".sculptforge");
            if (fs::is_directory(folder, ec)) scan(folder, "");
        }
    }
    std::sort(files.begin(), files.end());
    std::vector<std::string> out;
    for (const fs::path& f : files) out.push_back(pathUtf8(f));
    return out;
}

bool loadConvertedZTool(const std::string& path, std::vector<MeshObject>& out, std::string& error, std::string* note) {
    std::error_code ec;
    const std::vector<std::string> files = convertedZToolFiles(path);
    if (files.empty()) {
        error = "No converted ZTool found for " + path + " (convert it in ZBrush with the SculptForge ZTL Export plug-in)";
        return false;
    }
    const fs::path source = utf8Path(path);
    const bool isZtl = lowerAscii(pathUtf8(source.extension())) == ".ztl";
    out.clear();
    fs::file_time_type newest{};
    for (const std::string& file : files) {
        const fs::path f = utf8Path(file);
        MeshObject obj;
        GozInfo info;
        std::string err;
        if (!loadGoz(file, obj.mesh, err, &info)) {
            error = pathUtf8(f.filename()) + ": " + err;
            return false;
        }
        newest = std::max(newest, fs::last_write_time(f, ec));
        // "Dragon.sf007_Body" or "007_Body" -> "Body": the number only keeps the SubTool order.
        std::string stem = pathUtf8(f.stem());
        size_t cut = isZtl ? ztoolPrefixLength(stem, pathUtf8(source.stem())) : 0;
        if (cut == 0) {
            size_t digits = 0;
            while (digits < stem.size() && std::isdigit(static_cast<unsigned char>(stem[digits]))) ++digits;
            if (digits > 0 && digits + 1 < stem.size() && stem[digits] == '_') cut = digits + 1;
        }
        obj.name = info.name.empty() ? stem.substr(cut) : info.name;
        if (obj.mesh.triangleCount() > 0) out.push_back(std::move(obj));
    }
    if (out.empty()) {
        error = "The converted ZTool for " + path + " has no polygons";
        return false;
    }
    if (note && isZtl && fs::last_write_time(source, ec) > newest && !ec)
        *note = "the .ztl was saved after it was converted; convert it again in ZBrush to get the latest version";
    return true;
}

} // namespace sf
