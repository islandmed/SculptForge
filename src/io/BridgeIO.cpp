// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Bridge reader/writer and bridge. The format notes come from GoB, the GPL
// Blender add-on (github.com/JoseConseco/GoB: gob_import.py, gob_export.py,
// paths.py); docs/bridge-format.md describes the layout.
#include "io/BridgeIO.h"

#include "io/BridgeProtocol.h"

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
// Bridge text files use forward slashes on every platform (GoB writes them that way).
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
constexpr uint32_t kTagVertexPaint = 0x88b9;
constexpr uint32_t kTagFaceGroups = 0x9c41;
constexpr uint32_t kTagDiffuseMap = 0xafc9;
constexpr uint32_t kTagNormalMap = 0xc351;
constexpr uint32_t kTagDisplacementMap = 0xd6d9;
constexpr uint32_t kNoIndex = 0xffffffffu;

using bridgefile::kHeader;
constexpr size_t kHeaderSize = 36; // the text, six '.' and the uint32 1 that tags the name record
using bridgefile::kNamePrefix;

// the other application is Y-down; SculptForge (like OpenGL/glTF) is Y-up. GoB maps Bridge
// (x, y, z) to Blender (x, z, -y); in Y-up terms that is (x, -y, -z), a
// rotation (its own inverse) that keeps the winding.
glm::vec3 fromBridgeAxes(const glm::vec3& p) { return {p.x, -p.y, -p.z}; }
glm::vec3 toBridgeAxes(const glm::vec3& p) { return {p.x, -p.y, -p.z}; }

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

// A Bridge face: 4 indices, the fourth kNoIndex for triangles.
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

std::string bridgeObjectName(const std::string& name) {
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

bool parseBridge(std::span<const uint8_t> data, Mesh& out, std::string& error, BridgeInfo* info) {
    BridgeInfo local;
    BridgeInfo& gi = info ? *info : local;
    gi = BridgeInfo{};
    if (data.size() < kHeaderSize + 4 || std::memcmp(data.data(), bridgefile::kMagic, 4) != 0) {
        error = "Not a Bridge file (missing header)";
        return false;
    }
    Reader r(data);
    r.seek(kHeaderSize);
    // Object name record: length (counted from the tag at offset 32), uint64 1, kNamePrefix + name.
    uint32_t nameLen = 0;
    if (!r.u32(nameLen) || nameLen < 16 || nameLen - 8 > r.remaining()) {
        error = "Bridge file has a truncated object name";
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
                positions.push_back(fromBridgeAxes({readF32(p), readF32(p + 4), readF32(p + 8)}));
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
                // Bridge stores V downwards (GoB flips it by default both ways).
                faceUvs.push_back({readF32(p), 1.0f - readF32(p + 4)});
            }
            break;
        }
        case kTagVertexPaint: {
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
        case kTagFaceGroups: {
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
        error = "Bridge file has no " + std::string(positions.empty() ? "vertices" : "faces");
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
            if (haveGroups) m.facegroups.push_back(groups[fi]);
            if (haveUvs)
                for (int c = 0; c < 3; ++c) m.uvs.push_back(faceUvs[fi * 4 + size_t(tris[t][c])]);
        }
        if (m.triangles.size() == firstTri + 2) m.pairQuad(uint32_t(firstTri), uint32_t(firstTri + 1));
    }
    if (m.hasQuads()) m.quadPartners.resize(m.triangles.size(), Mesh::kNoQuad);
    if (m.triangles.empty()) {
        error = "Bridge file has no valid faces";
        return false;
    }
    if (mask.size() == nv && std::any_of(mask.begin(), mask.end(), [](float v) { return v > 0.0f; }))
        m.mask = std::move(mask);
    if (colors.size() == nv) m.colors = std::move(colors);
    m.rebuildTopology();
    m.recomputeAllNormals();
    if (skipped) error = std::to_string(skipped) + " Bridge file faces with out-of-range indices were skipped";
    out = std::move(m);
    return true;
}

std::vector<uint8_t> writeBridge(const Mesh& mesh, const std::string& name, const BridgeWriteOptions& options) {
    const std::string objName = bridgeObjectName(name);
    const size_t nv = mesh.vertexCount(), nt = mesh.triangleCount();
    const bool uvs = mesh.uvs.size() == nt * 3;
    const bool groups = mesh.facegroups.size() == nt;

    // Faces (and their 4 UV corners), quads written back as quads.
    std::vector<Face> faces;
    std::vector<glm::vec2> faceUvs;
    std::vector<uint16_t> faceGroups;
    faces.reserve(nt);
    // Bridge face groups are 16-bit. Truncating larger ids would merge unrelated groups,
    // so when any id is too big, number the distinct ids 0, 1, 2... instead (only
    // more than 65536 distinct groups still have to share ids).
    std::vector<uint32_t> groupIds;
    if (groups && std::any_of(mesh.facegroups.begin(), mesh.facegroups.end(), [](uint32_t g) { return g > 0xffffu; })) {
        groupIds = mesh.facegroups;
        std::sort(groupIds.begin(), groupIds.end());
        groupIds.erase(std::unique(groupIds.begin(), groupIds.end()), groupIds.end());
    }
    auto bridgeGroup = [&](uint32_t g) {
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
    // diagonal (Bridge has one UV per face corner); then both halves are written as triangles.
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
                     (!groups || mesh.facegroups[t] == mesh.facegroups[t + 1]) &&
                     (!uvs || (ub[0] == ua[0] && ub[1] == ua[2]));
            if (paired) {
                faces.push_back({{a.x, a.y, a.z, b.z}});
                if (uvs) faceUvs.insert(faceUvs.end(), {ua[0], ua[1], ua[2], ub[2]});
            }
        }
        if (!paired) {
            faces.push_back({{a.x, a.y, a.z, kNoIndex}});
            if (uvs) faceUvs.insert(faceUvs.end(), {ua[0], ua[1], ua[2], glm::vec2(0.0f, 0.0f)}); // GoB pads with (0, 1) in Bridge space
        }
        if (groups) faceGroups.push_back(bridgeGroup(mesh.facegroups[t]));
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
        glm::vec3 z = toBridgeAxes(p);
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
        w.section(kTagVertexPaint, nv * 4, nv);
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
        w.section(kTagFaceGroups, faceGroups.size() * 2, faceGroups.size());
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

bool loadBridge(const std::string& path, Mesh& out, std::string& error, BridgeInfo* info) {
    std::ifstream f(utf8Path(path), std::ios::binary);
    if (!f) {
        error = "Cannot open " + path;
        return false;
    }
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    std::string err;
    if (!parseBridge(data, out, err, info)) {
        error = err + ": " + path;
        return false;
    }
    error = err; // warnings, if any
    return true;
}

bool saveBridge(const std::string& path, const Mesh& mesh, const std::string& name, std::string& error,
             const BridgeWriteOptions& options) {
    if (mesh.triangleCount() == 0) {
        error = "Nothing to export: the mesh is empty";
        return false;
    }
    BridgeWriteOptions o = options;
    if (o.diffuseMap.empty() && hasTexture(mesh)) {
        // The texture as <stem>_color.png beside the file, recorded as the diffuse map.
        std::string png;
        const auto refs = std::vector<MeshObjectRef>{{name, &mesh}};
        if (!writeTextureBeside(path, textureFileNames(path, refs)[0], mesh, png, error)) return false;
        std::error_code ec;
        const fs::path absolute = fs::absolute(utf8Path(png), ec);
        o.diffuseMap = ec ? png : genericUtf8(absolute);
    }
    std::vector<uint8_t> bytes = writeBridge(mesh, name, o);
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

// Newest install of the other application in the default install locations (as GoB finds it).
std::string findOtherApp() {
    std::error_code ec;
#ifdef _WIN32
    const auto& bases = bridgefile::kWinInstallDirs;
    const char* exe = bridgefile::kWinExe;
#else
    const char* const bases[] = {"/Applications"};
    const char* exe = bridgefile::kMacApp;
#endif
    std::vector<fs::path> found;
    for (const auto& base : bases) {
        if (!fs::is_directory(base, ec)) continue;
        for (const auto& entry : fs::directory_iterator(base, ec)) {
            std::string n = pathUtf8(entry.path().filename());
            std::transform(n.begin(), n.end(), n.begin(), [](unsigned char c) { return char(std::tolower(c)); });
            if (n.find(bridgefile::kAppNameLower) == std::string::npos) continue;
            if (n == bridgefile::kMacAppLower) found.push_back(entry.path());
            else if (fs::exists(entry.path() / exe, ec)) found.push_back(entry.path() / exe);
        }
    }
    if (found.empty()) return {};
    std::sort(found.begin(), found.end()); // version numbers sort last-is-newest, like GoB's max()
    return pathUtf8(found.back());
}
#endif

} // namespace

std::string bridgeDefaultRoot() {
#ifdef _WIN32
    std::string pub = envUtf8(L"PUBLIC");
    if (pub.empty()) pub = "C:\\Users\\Public";
    return pathUtf8(utf8Path(pub) / bridgefile::kVendorFolder);
#elif defined(__APPLE__)
    return bridgefile::kMacRoot;
#else
    return {};
#endif
}

std::string bridgeUnavailableMessage() {
    return "The Bridge's default folder exists only on Windows and macOS. Use Export / Import Bridge file to move "
           "files by hand, or set a custom bridge folder (for example inside a Wine prefix).";
}

void setBridgeApplicationPath(std::string executable) { g_appPath = std::move(executable); }

std::string bridgeObjectListPath(const std::string& root) {
    return root.empty() ? std::string() : pathUtf8(utf8Path(root) / bridgefile::kInboxFolder / bridgefile::kObjectList);
}

bool bridgeShellSafePath(const std::string& path) {
    for (unsigned char c : path) {
        if (c < 0x20 || c == 0x7f || c == '"' || c == '$' || c == '`' || c == '%' || c == '!') return false;
#ifndef _WIN32
        if (c == '\\') return false;
#endif
    }
    return true;
}

bool bridgeSend(const Mesh& mesh, const BridgeSendOptions& o, std::string& message) {
    return bridgeSend(std::vector<MeshObjectRef>{{o.name.empty() ? "SculptForge" : o.name, &mesh}}, o, message);
}

bool bridgeSend(const std::vector<MeshObjectRef>& objects, const BridgeSendOptions& o, std::string& message,
             std::vector<std::string>* sentNames) {
    const std::string root = o.root.empty() ? bridgeDefaultRoot() : o.root;
    if (root.empty()) {
        message = bridgeUnavailableMessage();
        return false;
    }
    std::error_code ec;
    if (o.launch) { // these paths reach the shell (launchDetached)
        if (!bridgeShellSafePath(root)) {
            message = "The bridge folder path contains characters that are not allowed when launching the other application";
            return false;
        }
        if (!o.appPath.empty()) {
            std::string file = pathUtf8(utf8Path(o.appPath).filename());
            std::transform(file.begin(), file.end(), file.begin(), [](unsigned char c) { return char(std::tolower(c)); });
            if (!bridgeShellSafePath(o.appPath) || file.find(bridgefile::kAppNameLower) == std::string::npos ||
                !fs::exists(utf8Path(o.appPath), ec)) {
                message = "Not the other application's executable: " + o.appPath;
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
    const fs::path brush = rootPath / bridgefile::kInboxFolder;
    const fs::path project = rootPath / bridgefile::kProjectsFolder / "Default";
    const fs::path app = rootPath / bridgefile::kAppsFolder / "SculptForge";
    for (const auto& d : {brush, project, app}) fs::create_directories(d, ec);
    if (!fs::is_directory(project, ec)) {
        message = "Cannot create the bridge folder " + pathUtf8(project);
        return false;
    }
    // One Bridge per object (makes each a Part), all listed in Bridge_ObjectList.txt.
    std::vector<std::string> names;
    std::string list;
    bool ok = true;
    for (const auto& obj : objects) {
        const std::string wanted = bridgeObjectName(obj.name.empty() ? "SculptForge" : obj.name);
        std::string name = wanted;
        for (int k = 2; std::find(names.begin(), names.end(), name) != names.end(); ++k) name = wanted + "_" + std::to_string(k);
        names.push_back(name);
        // Object paths are written without the Bridge extension, the other application appends it.
        const std::string base = genericUtf8(project / name);
        std::string err;
        if (!saveBridge(pathUtf8(project / (name + bridgefile::kExt)), *obj.mesh, name, err)) {
            message = err;
            return false;
        }
        ok = ok && writeText(project / (name + ".ztn"), base);
        list += base + "\n";
    }
    ok = ok && writeText(brush / bridgefile::kObjectList, list) && writeText(brush / bridgefile::kApplicationFile, "SculptForge") &&
              writeText(brush / bridgefile::kProjectPathFile, genericUtf8(project) + "/");
    // Register SculptForge  Bridge application list.
    if (!fs::exists(app / bridgefile::kInfoFile, ec)) ok = ok && writeText(app / bridgefile::kInfoFile, bridgefile::kInfoText);
    if (!g_appPath.empty())
        ok = ok && writeText(app / bridgefile::kConfigFile, "PATH = \"" + genericUtf8(utf8Path(g_appPath)) + "\"");
    if (!ok) {
        message = "Cannot write the bridge files in " + root;
        return false;
    }
    message = "Sent " + names.front() + (names.size() > 1 ? " and " + std::to_string(names.size() - 1) + " more Parts" : "") +
              " to " + pathUtf8(project);
    if (sentNames) *sentNames = names;
    if (!o.launch) return true;

    // Official Bridge plug-ins notify the running application with its notifier; GoB
    // starts the application itself. Do the first, fall back to the second.
#ifdef _WIN32
    const fs::path notifier = brush / bridgefile::kNotifierWin;
#else
    const fs::path notifier = brush / bridgefile::kNotifierMac;
#endif
#if defined(_WIN32) || defined(__APPLE__)
    if (fs::exists(notifier, ec)) {
        launchDetached(pathUtf8(notifier));
        message += "; the other application was notified";
        return true;
    }
    const std::string otherApp = o.appPath.empty() ? findOtherApp() : o.appPath;
    if (!otherApp.empty() && launchDetached(otherApp)) {
        message += "; started " + otherApp + " (import from the bridge there)";
        return true;
    }
    message += "; the other application was not found (set it up once: Help > User Guide > Exporting, Bridge)";
#else
    (void)notifier;
    message += "; " + bridgeUnavailableMessage();
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
    appendAttribute(dst.facegroups, dt, src.facegroups, st, 0u);
    appendAttribute(dst.uvs, dt * 3, src.uvs, st * 3, glm::vec2(0.0f));
}

} // namespace

bool bridgeReceive(const std::string& rootIn, std::vector<MeshObject>& out, std::string& message) {
    const std::string root = rootIn.empty() ? bridgeDefaultRoot() : rootIn;
    if (root.empty()) {
        message = bridgeUnavailableMessage();
        return false;
    }
    std::ifstream list(utf8Path(bridgeObjectListPath(root)), std::ios::binary);
    if (!list) {
        message = "Nothing to receive: " + bridgeObjectListPath(root) + " does not exist (send from the other application first)";
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
        if (lower.size() < 4 || lower.compare(lower.size() - 4, 4, bridgefile::kExtLower) != 0) path += bridgefile::kExt;
        Mesh m;
        BridgeInfo info;
        std::string err;
        if (!loadBridge(path, m, err, &info)) {
            problems += (problems.empty() ? "" : "; ") + err;
            continue;
        }
        loaded.push_back({info.name.empty() ? pathUtf8(utf8Path(path).stem()) : info.name, std::move(m)});
    }
    if (loaded.empty()) {
        message = problems.empty() ? "Nothing to receive: the bridge's object list is empty" : problems;
        return false;
    }
    size_t tris = 0;
    for (const auto& obj : loaded) tris += obj.mesh.triangleCount();
    message = "Received " + loaded.front().name;
    if (loaded.size() > 1) message += " and " + std::to_string(loaded.size() - 1) + " more Parts";
    message += " through the Bridge (" + std::to_string(tris) + " triangles)";
    if (!problems.empty()) message += "; skipped: " + problems;
    out = std::move(loaded);
    return true;
}

bool bridgeReceive(const std::string& root, Mesh& out, std::string& message, std::vector<std::string>* names) {
    std::vector<MeshObject> objects;
    if (!bridgeReceive(root, objects, message)) return false;
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

namespace {

std::string lowerAscii(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return s;
}

} // namespace

std::vector<std::string> bridgeFolderFiles(const std::string& path) {
    std::error_code ec;
    const fs::path p = utf8Path(path);
    std::vector<fs::path> files;
    if (fs::is_directory(p, ec))
        for (fs::directory_iterator it(p, ec), end; !ec && it != end; it.increment(ec))
            if (lowerAscii(pathUtf8(it->path().extension())) == bridgefile::kExtLower && it->is_regular_file(ec))
                files.push_back(it->path());
    std::sort(files.begin(), files.end());
    std::vector<std::string> out;
    for (const fs::path& f : files) out.push_back(pathUtf8(f));
    return out;
}

bool loadBridgeFolder(const std::string& path, std::vector<MeshObject>& out, std::string& error) {
    const std::vector<std::string> files = bridgeFolderFiles(path);
    if (files.empty()) {
        error = "No Bridge files found in " + path;
        return false;
    }
    out.clear();
    for (const std::string& file : files) {
        const fs::path f = utf8Path(file);
        MeshObject obj;
        BridgeInfo info;
        std::string err;
        if (!loadBridge(file, obj.mesh, err, &info)) {
            error = pathUtf8(f.filename()) + ": " + err;
            return false;
        }
        // "007_Body" -> "Body": the number only keeps the order.
        std::string stem = pathUtf8(f.stem());
        size_t cut = 0, digits = 0;
        while (digits < stem.size() && std::isdigit(static_cast<unsigned char>(stem[digits]))) ++digits;
        if (digits > 0 && digits + 1 < stem.size() && stem[digits] == '_') cut = digits + 1;
        obj.name = info.name.empty() ? stem.substr(cut) : info.name;
        if (obj.mesh.triangleCount() > 0) out.push_back(std::move(obj));
    }
    if (out.empty()) {
        error = "The Bridge files in " + path + " have no polygons";
        return false;
    }
    return true;
}

} // namespace sf
