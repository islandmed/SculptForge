// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "io/PlyIO.h"

#include "core/Units.h"

#include <algorithm>
#include <bit>
#include <cctype>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <sstream>

namespace sf {

namespace {

enum class Type { I8, U8, I16, U16, I32, U32, F32, F64, Invalid };

Type parseType(const std::string& s) {
    if (s == "char" || s == "int8") return Type::I8;
    if (s == "uchar" || s == "uint8") return Type::U8;
    if (s == "short" || s == "int16") return Type::I16;
    if (s == "ushort" || s == "uint16") return Type::U16;
    if (s == "int" || s == "int32") return Type::I32;
    if (s == "uint" || s == "uint32") return Type::U32;
    if (s == "float" || s == "float32") return Type::F32;
    if (s == "double" || s == "float64") return Type::F64;
    return Type::Invalid;
}

size_t typeSize(Type t) {
    switch (t) {
    case Type::I8:
    case Type::U8: return 1;
    case Type::I16:
    case Type::U16: return 2;
    case Type::I32:
    case Type::U32:
    case Type::F32: return 4;
    case Type::F64: return 8;
    default: return 0;
    }
}

struct Property {
    std::string name;
    Type type = Type::Invalid;
    bool list = false;
    Type countType = Type::Invalid;
};

struct Element {
    std::string name;
    size_t count = 0;
    std::vector<Property> props;
    int find(const std::string& n) const {
        for (size_t i = 0; i < props.size(); ++i)
            if (props[i].name == n) return int(i);
        return -1;
    }
};

// Sequential reader over the body, binary (either endianness) or ASCII.
struct Body {
    const uint8_t* data;
    size_t size, pos = 0;
    bool ascii, bigEndian;
    bool ok = true;

    double read(Type t) {
        if (ascii) return readAscii();
        const size_t n = typeSize(t);
        if (pos + n > size) {
            ok = false;
            return 0.0;
        }
        uint8_t b[8];
        std::memcpy(b, data + pos, n);
        pos += n;
        if (bigEndian != (std::endian::native == std::endian::big)) std::reverse(b, b + n);
        switch (t) {
        case Type::I8: return double(int8_t(b[0]));
        case Type::U8: return double(b[0]);
        case Type::I16: { int16_t v; std::memcpy(&v, b, 2); return v; }
        case Type::U16: { uint16_t v; std::memcpy(&v, b, 2); return v; }
        case Type::I32: { int32_t v; std::memcpy(&v, b, 4); return v; }
        case Type::U32: { uint32_t v; std::memcpy(&v, b, 4); return v; }
        case Type::F32: { float v; std::memcpy(&v, b, 4); return v; }
        case Type::F64: { double v; std::memcpy(&v, b, 8); return v; }
        default: ok = false; return 0.0;
        }
    }

    double readAscii() {
        while (pos < size && std::isspace(data[pos])) ++pos;
        const size_t start = pos;
        while (pos < size && !std::isspace(data[pos])) ++pos;
        if (start == pos) {
            ok = false;
            return 0.0;
        }
        return std::strtod(std::string(reinterpret_cast<const char*>(data + start), pos - start).c_str(), nullptr);
    }
};

float sigmoid(float x) { return 1.0f / (1.0f + std::exp(-x)); }

} // namespace

bool loadPly(const std::string& path, Mesh& mesh, SplatCloud& splats, PlyContent& content, std::string& error) {
    std::ifstream f(std::filesystem::path(std::u8string(path.begin(), path.end())), std::ios::binary);
    if (!f) {
        error = "Cannot open " + path;
        return false;
    }
    std::vector<uint8_t> file((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());

    // Header.
    const std::string marker = "end_header";
    auto it = std::search(file.begin(), file.end(), marker.begin(), marker.end());
    if (file.size() < 4 || std::memcmp(file.data(), "ply", 3) != 0 || it == file.end()) {
        error = path + " is not a PLY file";
        return false;
    }
    size_t bodyStart = size_t(it - file.begin()) + marker.size();
    if (bodyStart < file.size() && file[bodyStart] == '\r') ++bodyStart;
    if (bodyStart < file.size() && file[bodyStart] == '\n') ++bodyStart;
    std::istringstream header(std::string(file.begin(), it));
    std::vector<Element> elements;
    std::string line, format;
    while (std::getline(header, line)) {
        std::istringstream ls(line);
        std::string word;
        ls >> word;
        if (word == "format") ls >> format;
        else if (word == "element") {
            Element e;
            ls >> e.name >> e.count;
            elements.push_back(e);
        } else if (word == "property" && !elements.empty()) {
            Property p;
            std::string t;
            ls >> t;
            if (t == "list") {
                std::string ct, it2;
                ls >> ct >> it2 >> p.name;
                p.list = true;
                p.countType = parseType(ct);
                p.type = parseType(it2);
            } else {
                p.type = parseType(t);
                ls >> p.name;
            }
            if (p.type == Type::Invalid || (p.list && p.countType == Type::Invalid)) {
                error = "Unsupported PLY property type in: " + line;
                return false;
            }
            elements.back().props.push_back(p);
        }
    }
    if (format != "ascii" && format != "binary_little_endian" && format != "binary_big_endian") {
        error = "Unsupported PLY format '" + format + "'";
        return false;
    }
    for (const auto& e : elements)
        if (e.name == "chunk") {
            error = "Compressed splat PLY is not supported yet - export an uncompressed .ply (e.g. from SuperSplat)";
            return false;
        }

    const Element* vertex = nullptr;
    const Element* face = nullptr;
    for (const auto& e : elements) {
        if (e.name == "vertex") vertex = &e;
        if (e.name == "face") face = &e;
    }
    if (!vertex || vertex->count == 0) {
        error = "The PLY file has no vertices";
        return false;
    }
    const bool isSplat = vertex->find("scale_0") >= 0 && vertex->find("rot_0") >= 0 && vertex->find("opacity") >= 0;
    const bool isMesh = !isSplat && face && face->count > 0;
    content = isMesh ? PlyContent::Mesh : PlyContent::Splats;

    // Column lookups for the vertex element.
    auto col = [&](const std::string& n) { return vertex->find(n); };
    const int px = col("x"), py = col("y"), pz = col("z");
    if (px < 0 || py < 0 || pz < 0) {
        error = "The PLY vertices have no x/y/z";
        return false;
    }
    int restCount = 0;
    while (col("f_rest_" + std::to_string(restCount)) >= 0) ++restCount;
    int degree = 0;
    for (int d = 3; d >= 1; --d)
        if (restCount == 3 * ((d + 1) * (d + 1) - 1)) {
            degree = d;
            break;
        }
    const int perChannel = (degree + 1) * (degree + 1) - 1;
    std::vector<int> rest(size_t(3 * perChannel));
    for (int k = 0; k < 3 * perChannel; ++k) rest[size_t(k)] = col("f_rest_" + std::to_string(k));
    const int dc[3] = {col("f_dc_0"), col("f_dc_1"), col("f_dc_2")};
    const int sc[3] = {col("scale_0"), col("scale_1"), col("scale_2")};
    const int rt[4] = {col("rot_0"), col("rot_1"), col("rot_2"), col("rot_3")};
    const int op = col("opacity");
    int rgb[3] = {col("red"), col("green"), col("blue")};
    if (rgb[0] < 0) rgb[0] = col("r"), rgb[1] = col("g"), rgb[2] = col("b");
    const bool rgbIsByte = rgb[0] >= 0 && typeSize(vertex->props[size_t(rgb[0])].type) == 1;

    Body body{file.data() + bodyStart, file.size() - bodyStart, 0, format == "ascii", format == "binary_big_endian"};
    // A malformed header can claim billions of rows: check the counts against the bytes actually present before
    // allocating anything (every binary row needs its fixed-size fields; every ASCII value at least two characters).
    {
        size_t needed = 0;
        for (const auto& e : elements) {
            size_t rowBytes = 0;
            for (const auto& prop : e.props) rowBytes += body.ascii ? 2 : typeSize(prop.list ? prop.countType : prop.type);
            rowBytes = std::max<size_t>(rowBytes, 1);
            if (e.count > (body.size - std::min(needed, body.size)) / rowBytes) {
                error = "The PLY header describes more data than the file contains";
                return false;
            }
            needed += e.count * rowBytes;
        }
    }
    Mesh m;
    SplatCloud cloud;
    std::vector<double> row;
    std::vector<glm::vec3> pointColors;

    for (const auto& e : elements) {
        const bool isVertex = &e == vertex, isFace = &e == face;
        if (isVertex) {
            if (isMesh) m.positions.reserve(e.count);
            else cloud.resize(e.count, isSplat ? degree : 0);
            if (!isSplat && !isMesh) pointColors.assign(e.count, glm::vec3(0.8f));
        }
        for (size_t r = 0; r < e.count; ++r) {
            row.assign(e.props.size(), 0.0);
            std::vector<uint32_t> poly;
            for (size_t p = 0; p < e.props.size(); ++p) {
                const Property& prop = e.props[p];
                if (!prop.list) {
                    row[p] = body.read(prop.type);
                    continue;
                }
                const double count = body.read(prop.countType);
                if (!body.ok || !(count >= 0.0 && count <= 1'000'000.0)) {
                    error = "Corrupt PLY list";
                    return false;
                }
                const size_t n = size_t(count);
                const bool wanted = isFace && (prop.name == "vertex_indices" || prop.name == "vertex_index");
                for (size_t k = 0; k < n; ++k) {
                    const double v = body.read(prop.type);
                    if (!wanted) continue;
                    if (!(v >= 0.0 && v < 4294967295.0)) {
                        error = "PLY face index out of range";
                        return false;
                    }
                    poly.push_back(uint32_t(v));
                }
            }
            if (!body.ok) {
                error = "Truncated PLY file";
                return false;
            }
            if (isVertex) {
                const glm::vec3 pos{float(row[size_t(px)]), float(row[size_t(py)]), float(row[size_t(pz)])};
                if (isMesh) {
                    m.positions.push_back(pos);
                    if (rgb[0] >= 0 && rgb[1] >= 0 && rgb[2] >= 0) {
                        const glm::vec3 c{float(row[size_t(rgb[0])]), float(row[size_t(rgb[1])]), float(row[size_t(rgb[2])])};
                        m.colors.push_back(srgbToLinear(rgbIsByte ? c / 255.0f : c));
                    }
                    continue;
                }
                cloud.positions[r] = pos;
                if (isSplat) {
                    const size_t K = size_t(cloud.shCount());
                    for (int c = 0; c < 3; ++c) {
                        if (dc[c] >= 0) cloud.sh[r * K][c] = float(row[size_t(dc[c])]);
                        for (int k = 0; k < perChannel; ++k) {
                            const int idx = rest[size_t(c * perChannel + k)];
                            cloud.sh[r * K + size_t(k) + 1][c] = idx >= 0 ? float(row[size_t(idx)]) : 0.0f;
                        }
                        cloud.scales[r][c] = std::exp(float(row[size_t(sc[c])]));
                    }
                    const glm::quat q{float(row[size_t(rt[0])]), float(row[size_t(rt[1])]), float(row[size_t(rt[2])]),
                                      float(row[size_t(rt[3])])};
                    const float len = glm::length(q);
                    cloud.rotations[r] = len > 0.0f ? q / len : glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
                    cloud.opacities[r] = sigmoid(float(row[size_t(op)]));
                } else if (rgb[0] >= 0 && rgb[1] >= 0 && rgb[2] >= 0) {
                    const glm::vec3 c{float(row[size_t(rgb[0])]), float(row[size_t(rgb[1])]), float(row[size_t(rgb[2])])};
                    pointColors[r] = rgbIsByte ? c / 255.0f : c;
                }
            } else if (isFace && poly.size() >= 3) {
                for (size_t k = 1; k + 1 < poly.size(); ++k) m.triangles.push_back({poly[0], poly[k], poly[k + 1]});
                if (poly.size() == 4) m.pairQuad(uint32_t(m.triangles.size() - 2), uint32_t(m.triangles.size() - 1));
            }
        }
    }

    if (isMesh) {
        for (const auto& t : m.triangles)
            if (t.x >= m.positions.size() || t.y >= m.positions.size() || t.z >= m.positions.size()) {
                error = "PLY face index out of range";
                return false;
            }
        if (m.hasQuads()) m.quadPartners.resize(m.triangles.size(), Mesh::kNoQuad);
        mesh = weldImported(m); // keeps vertex colours (scans, photogrammetry)
        if (mesh.triangleCount() == 0) {
            error = "The PLY mesh has no usable faces";
            return false;
        }
        return true;
    }
    if (!isSplat) {
        // Plain point cloud (LiDAR, photogrammetry): round splats sized to the point spacing.
        const AABB b = cloud.bounds();
        const glm::vec3 e = b.extent();
        const float area = std::max(e.x * e.y + e.y * e.z + e.z * e.x, 1e-12f);
        const float sigma = 0.7f * std::sqrt(area / float(cloud.size()));
        for (size_t i = 0; i < cloud.size(); ++i) {
            cloud.scales[i] = glm::vec3(sigma);
            cloud.setColor(i, pointColors[i]);
        }
    }
    auto stem = std::filesystem::path(std::u8string(path.begin(), path.end())).stem().u8string();
    cloud.name = std::string(stem.begin(), stem.end());
    splats = std::move(cloud);
    return true;
}

bool saveSplatPly(const std::string& path, const SplatCloud& input, std::string& error, bool bakePlacement) {
    if (input.empty()) {
        error = "There are no splats to save";
        return false;
    }
    if (std::string v = input.validate(); !v.empty()) {
        error = v;
        return false;
    }
    SplatCloud cloud = input;
    if (bakePlacement) cloud.bakeTransform();
    const int perChannel = cloud.shCount() - 1;

    std::ostringstream h;
    h << "ply\nformat binary_little_endian 1.0\ncomment Generated by SculptForge\nelement vertex " << cloud.size() << "\n";
    for (const char* n : {"x", "y", "z", "nx", "ny", "nz", "f_dc_0", "f_dc_1", "f_dc_2"}) h << "property float " << n << "\n";
    for (int k = 0; k < 3 * perChannel; ++k) h << "property float f_rest_" << k << "\n";
    for (const char* n : {"opacity", "scale_0", "scale_1", "scale_2", "rot_0", "rot_1", "rot_2", "rot_3"})
        h << "property float " << n << "\n";
    h << "end_header\n";

    std::vector<float> data;
    const size_t stride = size_t(9 + 3 * perChannel + 8);
    data.reserve(cloud.size() * stride);
    const size_t K = size_t(cloud.shCount());
    for (size_t i = 0; i < cloud.size(); ++i) {
        const glm::vec3& p = cloud.positions[i];
        data.insert(data.end(), {p.x, p.y, p.z, 0.0f, 0.0f, 0.0f});
        const glm::vec3& d = cloud.sh[i * K];
        data.insert(data.end(), {d.x, d.y, d.z});
        for (int c = 0; c < 3; ++c)
            for (int k = 0; k < perChannel; ++k) data.push_back(cloud.sh[i * K + size_t(k) + 1][c]);
        const float o = std::clamp(cloud.opacities[i], 1e-6f, 1.0f - 1e-6f);
        data.push_back(std::log(o / (1.0f - o)));
        for (int c = 0; c < 3; ++c) data.push_back(std::log(std::max(cloud.scales[i][c], 1e-12f)));
        const glm::quat& q = cloud.rotations[i];
        data.insert(data.end(), {q.w, q.x, q.y, q.z});
    }
    if constexpr (std::endian::native == std::endian::big)
        for (float& v : data) {
            uint32_t u;
            std::memcpy(&u, &v, 4);
            u = (u >> 24) | ((u >> 8) & 0xFF00u) | ((u << 8) & 0xFF0000u) | (u << 24);
            std::memcpy(&v, &u, 4);
        }

    std::ofstream f(std::filesystem::path(std::u8string(path.begin(), path.end())), std::ios::binary);
    if (!f) {
        error = "Cannot write " + path;
        return false;
    }
    const std::string hs = h.str();
    f.write(hs.data(), std::streamsize(hs.size()));
    f.write(reinterpret_cast<const char*>(data.data()), std::streamsize(data.size() * sizeof(float)));
    if (!f) {
        error = "Write failed for " + path;
        return false;
    }
    return true;
}

bool loadDotSplat(const std::string& path, SplatCloud& splats, std::string& error) {
    std::ifstream f(std::filesystem::path(std::u8string(path.begin(), path.end())), std::ios::binary);
    if (!f) {
        error = "Cannot open " + path;
        return false;
    }
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (data.empty() || data.size() % 32 != 0) {
        error = path + " is not a .splat file (size must be a multiple of 32 bytes)";
        return false;
    }
    SplatCloud cloud;
    const size_t n = data.size() / 32;
    cloud.resize(n, 0);
    for (size_t i = 0; i < n; ++i) {
        const uint8_t* p = data.data() + i * 32;
        float v[6];
        std::memcpy(v, p, sizeof v);
        if constexpr (std::endian::native == std::endian::big)
            for (float& x : v) {
                uint32_t u;
                std::memcpy(&u, &x, 4);
                u = (u >> 24) | ((u >> 8) & 0xFF00u) | ((u << 8) & 0xFF0000u) | (u << 24);
                std::memcpy(&x, &u, 4);
            }
        cloud.positions[i] = glm::vec3(v[0], v[1], v[2]);
        cloud.scales[i] = glm::vec3(v[3], v[4], v[5]);
        cloud.setColor(i, glm::vec3(p[24], p[25], p[26]) / 255.0f);
        cloud.opacities[i] = float(p[27]) / 255.0f;
        glm::quat q((float(p[28]) - 128.0f) / 128.0f, (float(p[29]) - 128.0f) / 128.0f, (float(p[30]) - 128.0f) / 128.0f,
                    (float(p[31]) - 128.0f) / 128.0f);
        const float len = glm::length(q);
        cloud.rotations[i] = len > 0.0f ? q / len : glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    }
    auto stem = std::filesystem::path(std::u8string(path.begin(), path.end())).stem().u8string();
    cloud.name = std::string(stem.begin(), stem.end());
    splats = std::move(cloud);
    return true;
}

bool saveDotSplat(const std::string& path, const SplatCloud& input, std::string& error) {
    if (input.empty()) {
        error = "There are no splats to save";
        return false;
    }
    SplatCloud c = input;
    c.bakeTransform();
    std::vector<uint8_t> out(c.size() * 32);
    auto byte = [](float x) { return uint8_t(std::clamp(int(std::lround(x * 255.0f)), 0, 255)); };
    auto qbyte = [](float x) { return uint8_t(std::clamp(int(std::lround(x * 128.0f + 128.0f)), 0, 255)); };
    for (size_t i = 0; i < c.size(); ++i) {
        uint8_t* p = out.data() + i * 32;
        const float v[6] = {c.positions[i].x, c.positions[i].y, c.positions[i].z, c.scales[i].x, c.scales[i].y, c.scales[i].z};
        std::memcpy(p, v, sizeof v);
        const glm::vec3 rgb = c.color(i);
        p[24] = byte(rgb.x);
        p[25] = byte(rgb.y);
        p[26] = byte(rgb.z);
        p[27] = byte(c.opacities[i]);
        const glm::quat& q = c.rotations[i];
        p[28] = qbyte(q.w);
        p[29] = qbyte(q.x);
        p[30] = qbyte(q.y);
        p[31] = qbyte(q.z);
    }
    std::ofstream f(std::filesystem::path(std::u8string(path.begin(), path.end())), std::ios::binary);
    if (!f || !f.write(reinterpret_cast<const char*>(out.data()), std::streamsize(out.size()))) {
        error = "Cannot write " + path;
        return false;
    }
    return true;
}

bool savePlyMesh(const std::string& path, const Mesh& mesh, std::string& error, double scale) {
    if (mesh.triangleCount() == 0) {
        error = "There is no mesh to save";
        return false;
    }
    const bool colors = mesh.hasColors() && mesh.colors.size() == mesh.vertexCount();
    const bool normals = mesh.normals.size() == mesh.vertexCount();
    std::ostringstream h;
    h << "ply\nformat binary_little_endian 1.0\ncomment Generated by SculptForge\nelement vertex " << mesh.vertexCount()
      << "\nproperty float x\nproperty float y\nproperty float z\n";
    if (normals) h << "property float nx\nproperty float ny\nproperty float nz\n";
    if (colors) h << "property uchar red\nproperty uchar green\nproperty uchar blue\n";

    std::vector<uint8_t> data, faces;
    data.reserve(mesh.vertexCount() * 27);
    faces.reserve(mesh.triangleCount() * 13);
    auto put32To = [](std::vector<uint8_t>& out, uint32_t u) {
        for (int k = 0; k < 4; ++k) out.push_back(uint8_t(u >> (8 * k))); // little-endian on any host
    };
    auto put32 = [&](uint32_t u) { put32To(data, u); };
    // Quads where the mesh keeps them (Mesh::quadPartners), else triangles.
    size_t faceCount = 0;
    for (uint32_t t = 0; t < mesh.triangleCount(); ++t) {
        glm::uvec4 f;
        const int corners = mesh.exportFace(t, f);
        if (corners == 0) continue;
        faces.push_back(uint8_t(corners));
        for (int c = 0; c < corners; ++c) put32To(faces, f[c]);
        ++faceCount;
    }
    h << "element face " << faceCount << "\nproperty list uchar int vertex_indices\nend_header\n";

    auto putFloat = [&](float v) { put32(std::bit_cast<uint32_t>(v)); };
    for (size_t v = 0; v < mesh.vertexCount(); ++v) {
        for (int c = 0; c < 3; ++c) putFloat(toFileUnits(mesh.positions[v][c], scale));
        if (normals)
            for (int c = 0; c < 3; ++c) putFloat(mesh.normals[v][c]);
        if (colors) {
            const glm::vec3 c = glm::clamp(linearToSrgb(mesh.colors[v]), 0.0f, 1.0f);
            for (int k = 0; k < 3; ++k) data.push_back(uint8_t(std::lround(c[k] * 255.0f)));
        }
    }
    data.insert(data.end(), faces.begin(), faces.end());

    std::ofstream f(std::filesystem::path(std::u8string(path.begin(), path.end())), std::ios::binary);
    if (!f) {
        error = "Cannot write " + path;
        return false;
    }
    const std::string hs = h.str();
    f.write(hs.data(), std::streamsize(hs.size()));
    f.write(reinterpret_cast<const char*>(data.data()), std::streamsize(data.size()));
    if (!f) {
        error = "Write failed for " + path;
        return false;
    }
    return true;
}

} // namespace sf
