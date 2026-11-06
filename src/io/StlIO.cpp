// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "io/StlIO.h"

#include "core/Units.h"

#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>

namespace sf {

namespace {

std::filesystem::path utf8Path(const std::string& s) { return std::filesystem::path(std::u8string(s.begin(), s.end())); }

uint32_t readU32(const uint8_t* p) {
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}
float readF32(const uint8_t* p) {
    uint32_t bits = readU32(p);
    float f;
    std::memcpy(&f, &bits, 4);
    return f;
}
void putU32(std::vector<uint8_t>& out, uint32_t v) {
    for (int i = 0; i < 4; ++i) out.push_back(uint8_t(v >> (8 * i)));
}
void putF32(std::vector<uint8_t>& out, float f) {
    uint32_t bits;
    std::memcpy(&bits, &f, 4);
    putU32(out, bits);
}

// Header of the binary files SculptForge 0.5 and earlier wrote, in metres with Y up.
constexpr char kLegacyHeader[] = "SculptForge binary STL";
constexpr char kHeader[] = "SculptForge binary STL, millimetres, Z up";

// Millimetres (or `perMetre`) with Z up (the file) to metres with Y up (the sculpt). Divided in double and rounded
// once, so writing it back (saveStl, or OBJ at Document::unitlessScale) gives the file's numbers.
glm::vec3 fromPrintSpace(glm::vec3 p, double perMetre) {
    return glm::vec3(fromFileUnits(p.x, perMetre), fromFileUnits(p.z, perMetre), fromFileUnits(-p.y, perMetre));
}

// Turns a triangle soup into a welded mesh.
bool finish(Mesh& soup, Mesh& out, std::string& error, bool printSpace, double perMetre) {
    if (!(perMetre > 0.0) || !std::isfinite(perMetre)) {
        error = "The unit scale must be a positive number";
        return false;
    }
    if (printSpace)
        for (glm::vec3& p : soup.positions) p = fromPrintSpace(p, perMetre);
    for (uint32_t i = 0; i + 2 < soup.positions.size(); i += 3) soup.triangles.push_back({i, i + 1, i + 2});
    if (soup.triangles.empty()) {
        error = "STL file has no triangles";
        return false;
    }
    Mesh welded = weldImported(soup);
    if (welded.triangles.empty()) {
        error = "STL file has only degenerate triangles";
        return false;
    }
    out = std::move(welded);
    return true;
}

} // namespace

bool parseStl(std::span<const uint8_t> data, Mesh& out, std::string& error, double perMetre) {
    Mesh soup;
    // Binary: 80-byte header, uint32 count, 50 bytes per triangle. Some
    // binary files also start with "solid", so the size decides.
    if (data.size() >= 84) {
        const uint64_t count = readU32(data.data() + 80);
        if (84 + count * 50 == data.size()) {
            const bool legacy = std::memcmp(data.data(), kLegacyHeader, sizeof(kLegacyHeader)) == 0; // NUL after the name
            soup.positions.reserve(size_t(count) * 3);
            for (size_t t = 0; t < count; ++t) {
                const uint8_t* p = data.data() + 84 + t * 50 + 12; // skip the facet normal
                for (int c = 0; c < 3; ++c, p += 12) soup.positions.push_back({readF32(p), readF32(p + 4), readF32(p + 8)});
            }
            return finish(soup, out, error, !legacy, perMetre);
        }
    }
    const std::string text(reinterpret_cast<const char*>(data.data()), data.size());
    if (text.find_first_not_of(" \t\r\n") == std::string::npos || text.compare(text.find_first_not_of(" \t\r\n"), 5, "solid") != 0) {
        error = "Not an STL file (bad binary size and no \"solid\" header)";
        return false;
    }
    std::istringstream in(text);
    std::string word;
    while (in >> word) {
        if (word != "vertex") continue;
        glm::vec3 p;
        if (!(in >> p.x >> p.y >> p.z)) {
            error = "Malformed vertex in ASCII STL";
            return false;
        }
        soup.positions.push_back(p);
    }
    return finish(soup, out, error, true, perMetre);
}

bool loadStl(const std::string& path, Mesh& out, std::string& error, double perMetre) {
    std::ifstream f(utf8Path(path), std::ios::binary);
    if (!f) {
        error = "Cannot open " + path;
        return false;
    }
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    std::string err;
    if (!parseStl(data, out, err, perMetre)) {
        error = err + ": " + path;
        return false;
    }
    return true;
}

bool saveStl(const std::string& path, const Mesh& mesh, std::string& error, const StlExportOptions& options) {
    if (mesh.triangleCount() == 0) {
        error = "Nothing to export: the mesh is empty";
        return false;
    }
    double scale = options.scale;
    if (options.heightMm > 0.0f) {
        const float height = mesh.bounds().extent().y;
        if (!(height > 0.0f)) {
            error = "Cannot set a height: the mesh is flat";
            return false;
        }
        scale = double(options.heightMm) / height;
    }
    if (!(scale > 0.0f) || !std::isfinite(scale)) {
        error = "The scale must be a positive number";
        return false;
    }
    auto toFile = [&](glm::vec3 p) { return toFileUnits(options.zUp ? glm::vec3(p.x, -p.z, p.y) : p, scale); };
    std::vector<uint8_t> bytes;
    bytes.reserve(84 + mesh.triangleCount() * 50);
    // The header says millimetres only when the file is in millimetres with Z up.
    const bool printSpace = options.zUp && (options.heightMm > 0.0f || options.scale == 1000.0);
    const std::string header = printSpace ? kHeader : "SculptForge binary STL, custom scale";
    bytes.insert(bytes.end(), header.begin(), header.end());
    bytes.resize(80, 0);
    putU32(bytes, uint32_t(mesh.triangleCount()));
    for (const auto& t : mesh.triangles) {
        const glm::vec3 a = toFile(mesh.positions[t.x]), b = toFile(mesh.positions[t.y]), c = toFile(mesh.positions[t.z]);
        glm::vec3 n = glm::cross(b - a, c - a);
        const float len = glm::length(n);
        n = len > 0.0f ? n / len : glm::vec3(0.0f);
        for (const glm::vec3& v : {n, a, b, c}) {
            putF32(bytes, v.x);
            putF32(bytes, v.y);
            putF32(bytes, v.z);
        }
        bytes.push_back(0); // attribute byte count
        bytes.push_back(0);
    }
    std::ofstream f(utf8Path(path), std::ios::binary);
    if (!f || !f.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()))) {
        error = "Cannot write " + path;
        return false;
    }
    return true;
}

} // namespace sf
