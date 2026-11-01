// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/Project.h"
#include "core/Micro.h"
#include "core/Timeline.h"

#include "core/Multires.h"

#include "core/LayerStack.h"
#include "core/LayerStackIO.h"
#include "core/SelectionSets.h"
#include "core/Texture.h"
#include "core/ThreadPool.h"
#include "core/ZSpheres.h"
#include "io/Zip.h"

#include <algorithm>
#include <atomic>
#include <bit>
#include <cctype>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <map>
#include <system_error>
#include <thread>

#if defined(__unix__) || defined(__APPLE__)
#include <fcntl.h>
#include <unistd.h>
#define SF_PREAD 1
#else
#define SF_PREAD 0
#endif

#ifndef SCULPTFORGE_VERSION
#define SCULPTFORGE_VERSION "unknown"
#endif

namespace sf {

namespace {

constexpr const char* kFormatName = "SculptForge project";
constexpr const char* kManifestName = "manifest.json";
constexpr const char* kThumbnailName = "thumbnail.png";

ProjectThumbnailProvider& thumbnailProvider() {
    static ProjectThumbnailProvider provider;
    return provider;
}
// Top-level manifest keys owned by the core (mesh, layers and active_layer are
// version 1); every other key belongs to
// Document::projectSettings (camera, render, brush, symmetry, ...).
const char* const kCoreKeys[] = {"format", "version", "app_version", "mesh", "layers", "active_layer",
                                 "subtools", "active_subtool", "brushes", "active_brush", "splats", "attachments"};

std::filesystem::path fsPath(const std::string& p) { return std::filesystem::path(std::u8string(p.begin(), p.end())); }

constexpr size_t kMaxAttachmentSize = size_t(64) << 20;

// Document::projectFiles names: "matcaps/Gold.png" - letters, digits, space . _ - and '/' between parts.
bool validAttachmentName(const std::string& name) {
    if (name.empty() || name.size() > 200 || name.front() == '/' || name.back() == '/' || name.find("..") != std::string::npos)
        return false;
    return std::all_of(name.begin(), name.end(), [](unsigned char c) {
        return std::isalnum(c) || c == ' ' || c == '.' || c == '_' || c == '-' || c == '/' || c >= 0x80;
    });
}

bool isCoreKey(const std::string& key) {
    return std::any_of(std::begin(kCoreKeys), std::end(kCoreKeys), [&](const char* k) { return key == k; });
}

// --- Blobs: little-endian arrays of f32 / u32 / u8 with N components per element ---

enum class Scalar { F32, U32, U8 };

const char* scalarName(Scalar s) { return s == Scalar::F32 ? "f32" : s == Scalar::U32 ? "u32" : "u8"; }
size_t scalarSize(Scalar s) { return s == Scalar::U8 ? 1 : 4; }

[[maybe_unused]] void put32(std::vector<uint8_t>& out, uint32_t v) {
    for (int i = 0; i < 4; ++i) out.push_back(uint8_t(v >> (8 * i)));
}
uint32_t get32(const uint8_t* p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }

// `n` 32-bit values as little-endian bytes (a plain copy on little-endian machines).
template <class T>
std::vector<uint8_t> littleEndian(const T* v, size_t n) {
    static_assert(sizeof(T) == 4);
    std::vector<uint8_t> data;
    if constexpr (std::endian::native == std::endian::little) {
        data.resize(n * 4);
        if (n) std::memcpy(data.data(), v, n * 4);
    } else {
        data.reserve(n * 4);
        for (size_t i = 0; i < n; ++i) put32(data, std::bit_cast<uint32_t>(v[i]));
    }
    return data;
}

// Reads `n` little-endian 32-bit values (a plain copy on little-endian machines).
template <class T>
void fromLittleEndian(const uint8_t* p, size_t n, T* out) {
    static_assert(sizeof(T) == 4);
    if constexpr (std::endian::native == std::endian::little) {
        if (n) std::memcpy(out, p, n * 4);
    } else {
        for (size_t i = 0; i < n; ++i) out[i] = std::bit_cast<T>(get32(p + 4 * i));
    }
}

class Writer {
public:
    // Adds a blob and returns its manifest descriptor.
    Json add(const std::string& file, Scalar type, int components, size_t count, std::vector<uint8_t> data) {
        entries.push_back({file, std::move(data)});
        return descriptor(file, type, components, count);
    }
    // Adds `count` elements of `components` 32-bit values at `v` (which must outlive the save): written straight
    // from memory on little-endian machines, without a copy of the scene.
    Json add32(const std::string& file, Scalar type, int components, size_t count, const void* v) {
        const size_t n = count * size_t(components);
        if constexpr (std::endian::native == std::endian::little) {
            ZipEntry e{file, {}};
            e.external = n ? static_cast<const uint8_t*>(v) : nullptr;
            e.externalSize = n * 4;
            entries.push_back(std::move(e));
            return descriptor(file, type, components, count);
        } else {
            return add(file, type, components, count, littleEndian(static_cast<const uint32_t*>(v), n));
        }
    }
    Json floats(const std::string& file, const float* v, size_t count, int components) {
        return add32(file, Scalar::F32, components, count, v);
    }
    Json vec3s(const std::string& file, const std::vector<glm::vec3>& v) {
        return floats(file, v.empty() ? nullptr : &v[0].x, v.size(), 3);
    }
    Json uints(const std::string& file, const uint32_t* v, size_t count, int components) {
        return add32(file, Scalar::U32, components, count, v);
    }
    // Copied: for arrays that don't outlive the save.
    Json ownedUints(const std::string& file, const std::vector<uint32_t>& v) {
        return add(file, Scalar::U32, 1, v.size(), littleEndian(v.data(), v.size()));
    }
    Json bytes(const std::string& file, const std::vector<uint8_t>& v) { return add(file, Scalar::U8, 1, v.size(), v); }
    // `count` 16-bit integers at `v` (which must outlive the save), as u8 x 2, written straight from memory on
    // little-endian machines.
    Json int16s(const std::string& file, const int16_t* v, size_t count) {
        if constexpr (std::endian::native == std::endian::little) {
            ZipEntry e{file, {}};
            e.external = count ? reinterpret_cast<const uint8_t*>(v) : nullptr;
            e.externalSize = count * 2;
            entries.push_back(std::move(e));
            return descriptor(file, Scalar::U8, 2, count);
        } else {
            std::vector<uint8_t> data(count * 2);
            for (size_t i = 0; i < count; ++i) data[2 * i] = uint8_t(uint16_t(v[i])), data[2 * i + 1] = uint8_t(uint16_t(v[i]) >> 8);
            return add(file, Scalar::U8, 2, count, std::move(data));
        }
    }
    // Copied external-free: a byte array that must outlive the save.
    Json externalBytes(const std::string& file, const std::vector<uint8_t>& v) {
        ZipEntry e{file, {}};
        e.external = v.empty() ? nullptr : v.data();
        e.externalSize = v.size();
        entries.push_back(std::move(e));
        return descriptor(file, Scalar::U8, 1, v.size());
    }

    std::vector<ZipEntry> entries;

private:
    static Json descriptor(const std::string& file, Scalar type, int components, size_t count) {
        Json d = Json::object();
        d.set("file", file);
        d.set("type", scalarName(type));
        d.set("components", components);
        d.set("count", double(count));
        return d;
    }
};

// Reads blobs straight from the archive into the arrays they belong in (no copy of the file in memory).
class Reader {
public:
    std::map<std::string, ZipFileEntry> files;
    std::string error;
    // Big blobs are read on the shared thread pool, except while a BVH builds on another thread (it gets the pool).
    bool parallelReads = true;

    bool open(const std::string& path, std::string& err) {
        std::vector<ZipFileEntry> index;
        if (!readStoredZipIndex(path, index, err)) return false;
        for (auto& e : index) files[e.name] = std::move(e);
        file_.open(fsPath(path), std::ios::binary);
        if (!file_) {
            err = "Cannot open " + path;
            return false;
        }
#if SF_PREAD
        fd_ = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
#endif
        return true;
    }
    ~Reader() {
#if SF_PREAD
        if (fd_ >= 0) ::close(fd_);
#endif
    }
    // The whole entry `name` (false if missing or unreadable).
    bool entry(const std::string& name, std::vector<uint8_t>& out) {
        auto it = files.find(name);
        if (it == files.end() || it->second.size > (uint64_t(1) << 32)) return false;
        out.resize(size_t(it->second.size));
        return readAt(it->second.offset, out.size(), out.data());
    }
    // Validates `desc` against the expected layout and reads its bytes into `out` (false + error on mismatch).
    bool read(const Json& desc, const char* what, Scalar type, int components, size_t count, void* out) {
        if (!desc.isObject()) return fail(what, "is missing");
        if (desc["type"].asString() != scalarName(type) || int(desc["components"].asNumber()) != components)
            return fail(what, "has an unexpected layout");
        if (desc["count"].asNumber(-1) != double(count)) return fail(what, "has the wrong size");
        auto it = files.find(desc["file"].asString());
        if (it == files.end()) return fail(what, "data is missing from the archive");
        const size_t bytes = count * size_t(components) * scalarSize(type);
        if (it->second.size != bytes) return fail(what, "data is truncated");
        if (!readAt(it->second.offset, bytes, out)) return fail(what, "data is truncated");
        return true;
    }
    bool floats(const Json& desc, const char* what, size_t count, int components, float* out) {
        if (!read(desc, what, Scalar::F32, components, count, out)) return false;
        toNative(out, count * size_t(components));
        return true;
    }
    bool vec3s(const Json& desc, const char* what, size_t count, std::vector<glm::vec3>& out) {
        out.resize(count);
        return floats(desc, what, count, 3, count ? &out[0].x : nullptr);
    }
    bool uints(const Json& desc, const char* what, size_t count, int components, uint32_t* out) {
        if (!read(desc, what, Scalar::U32, components, count, out)) return false;
        toNative(out, count * size_t(components));
        return true;
    }
    bool bytes(const Json& desc, const char* what, size_t count, std::vector<uint8_t>& out) {
        out.resize(count);
        return read(desc, what, Scalar::U8, 1, count, out.data());
    }
    bool int16s(const Json& desc, const char* what, size_t count, std::vector<int16_t>& out) {
        out.resize(count);
        if (!read(desc, what, Scalar::U8, 2, count, out.data())) return false;
        if constexpr (std::endian::native != std::endian::little)
            for (int16_t& v : out) {
                const uint8_t* p = reinterpret_cast<const uint8_t*>(&v);
                v = int16_t(uint16_t(p[0]) | uint16_t(p[1]) << 8);
            }
        return true;
    }

private:
    std::ifstream file_;
#if SF_PREAD
    int fd_ = -1;
#endif

    bool readAt(uint64_t offset, size_t n, void* out) {
        if (n == 0) return true;
#if SF_PREAD
        // Big arrays in 8 MB pieces on every core (the copies and the page faults of the fresh array are the cost).
        constexpr size_t kPiece = size_t(8) << 20;
        if (fd_ >= 0 && n >= 2 * kPiece && parallelReads) {
            std::atomic<bool> ok = true;
            poolFor((n + kPiece - 1) / kPiece, [&](size_t b, size_t e) {
                for (size_t i = b; i < e; ++i) {
                    size_t done = i * kPiece;
                    const size_t end = std::min(n, done + kPiece);
                    while (done < end) {
                        const ssize_t got = ::pread(fd_, static_cast<char*>(out) + done, end - done, off_t(offset + done));
                        if (got <= 0) {
                            ok = false;
                            break;
                        }
                        done += size_t(got);
                    }
                }
            }, 1, 1);
            return ok;
        }
#endif
        file_.clear();
        file_.seekg(std::streamoff(offset));
        return bool(file_.read(static_cast<char*>(out), std::streamsize(n)));
    }
    // Little-endian values read in place become native ones (nothing to do on little-endian machines).
    template <class T>
    static void toNative(T* v, size_t n) {
        static_assert(sizeof(T) == 4);
        if constexpr (std::endian::native != std::endian::little) {
            for (size_t i = 0; i < n; ++i) {
                const uint8_t* p = reinterpret_cast<const uint8_t*>(&v[i]);
                v[i] = std::bit_cast<T>(get32(p));
            }
        }
    }
    bool fail(const char* what, const char* problem) {
        if (error.empty()) error = std::string("Project ") + what + " " + problem;
        return false;
    }
};

Json matrixJson(const glm::mat4& m) {
    Json a = Json::array();
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r) a.push(double(m[c][r]));
    return a;
}

glm::mat4 matrixFromJson(const Json& a) {
    glm::mat4 m(1.0f);
    if (a.size() != 16) return m;
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r) m[c][r] = float(a[size_t(c * 4 + r)].asNumber(c == r ? 1.0 : 0.0));
    return m;
}

size_t countOf(const Json& j) {
    const double n = j.asNumber(-1);
    return n >= 0 && n < 4.0e9 ? size_t(n) : size_t(-1);
}


// Writes a mesh's blobs under `prefix` ("subtools/0/") and sets `mesh` and `layers` in `out`. `generatedTriangles`:
// a generated subdivision level's triangle count; its triangles aren't written (format 6).
void writeMesh(Writer& w, const std::string& prefix, const Mesh& m, Json& out, size_t generatedTriangles = 0) {
    Json mesh = Json::object();
    mesh.set("vertex_count", double(m.vertexCount()));
    mesh.set("positions", w.vec3s(prefix + "mesh/positions.bin", m.positions));
    if (generatedTriangles) {
        mesh.set("triangle_count", double(generatedTriangles));
    } else {
        mesh.set("triangle_count", double(m.triangleCount()));
        mesh.set("triangles", w.uints(prefix + "mesh/triangles.bin", m.triangles.empty() ? nullptr : &m.triangles[0].x,
                                      m.triangleCount(), 3));
    }
    if (!m.layers.empty()) mesh.set("base", w.vec3s(prefix + "mesh/base.bin", m.base));
    if (m.hasMask()) mesh.set("mask", w.floats(prefix + "mesh/mask.bin", m.mask.data(), m.mask.size(), 1));
    if (m.hasColors()) mesh.set("colors", w.vec3s(prefix + "mesh/colors.bin", m.colors));
    if (m.hasPolygroups())
        mesh.set("polygroups", w.uints(prefix + "mesh/polygroups.bin", m.polygroups.data(), m.polygroups.size(), 1));
    if (m.hasHidden()) mesh.set("hidden", w.bytes(prefix + "mesh/hidden.bin", m.hidden));
    if (m.hasUvs()) mesh.set("uvs", w.floats(prefix + "mesh/uvs.bin", &m.uvs[0].x, m.uvs.size(), 2));
    if (m.hasUvFlags()) mesh.set("uv_flags", w.bytes(prefix + "mesh/uv_flags.bin", m.uvFlags)); // seams, pins (core/Uv.h)
    // Optional and ignored by older readers, so no format version bump.
    if (m.hasQuads())
        mesh.set("quad_partners", w.uints(prefix + "mesh/quad_partners.bin", m.quadPartners.data(), m.quadPartners.size(), 1));
    out.set("mesh", std::move(mesh));

    Json layers = Json::array();
    for (size_t i = 0; i < m.layers.size(); ++i) {
        Json l = Json::object();
        l.set("name", m.layers[i].name);
        l.set("weight", double(m.layers[i].weight));
        l.set("delta", w.vec3s(prefix + "layers/" + std::to_string(i) + ".bin", m.layers[i].delta));
        layers.push(std::move(l));
    }
    out.set("layers", std::move(layers));
}

// Reads a mesh from its `mesh` and `layers` manifest entries (size-checked against the blobs). `levelData`: a
// subdivision level's arrays, kept without topology or normals; a generated level's have no triangles.
// `topology` false: the caller calls finishMesh once the mesh has its subdivision levels (so it gets the
// adjacency they give).
bool readMesh(Reader& r, const Json& mj, const Json& lj, Mesh& m, std::string& error, bool levelData = false,
              bool topology = true) {
    const size_t nv = countOf(mj["vertex_count"]), nt = countOf(mj["triangle_count"]);
    if (nv == size_t(-1) || nt == size_t(-1)) {
        error = "Project mesh has no valid vertex/triangle count";
        return false;
    }
    const bool withTriangles = !levelData || mj.has("triangles");
    if (withTriangles) m.triangles.resize(nt);
    if (!r.vec3s(mj["positions"], "positions", nv, m.positions) ||
        (withTriangles && !r.uints(mj["triangles"], "triangles", nt, 3, nt ? &m.triangles[0].x : nullptr))) {
        error = r.error;
        return false;
    }
    for (const auto& t : m.triangles)
        if (t.x >= nv || t.y >= nv || t.z >= nv) {
            error = "Project triangles reference missing vertices";
            return false;
        }
    if (mj.has("mask")) {
        m.mask.resize(nv);
        r.floats(mj["mask"], "mask", nv, 1, m.mask.data());
    }
    if (mj.has("colors")) r.vec3s(mj["colors"], "colors", nv, m.colors);
    if (mj.has("polygroups")) {
        m.polygroups.resize(nt);
        r.uints(mj["polygroups"], "polygroups", nt, 1, m.polygroups.data());
    }
    if (mj.has("hidden")) {
        if (!r.bytes(mj["hidden"], "hidden flags", nt, m.hidden)) m.hidden.clear();
    }
    if (mj.has("uvs")) {
        m.uvs.resize(nt * 3);
        r.floats(mj["uvs"], "uvs", nt * 3, 2, nt ? &m.uvs[0].x : nullptr);
    }
    if (mj.has("uv_flags")) {
        if (!r.bytes(mj["uv_flags"], "UV seams", nt * 3, m.uvFlags)) m.uvFlags.clear();
    }
    if (mj.has("quad_partners")) { // rebuildTopology() unpairs anything that isn't a valid pair
        m.quadPartners.resize(nt);
        r.uints(mj["quad_partners"], "quad partners", nt, 1, m.quadPartners.data());
    }
    if (lj.size()) {
        r.vec3s(mj["base"], "base positions", nv, m.base);
        for (size_t i = 0; i < lj.size() && r.error.empty(); ++i) {
            SculptLayer layer;
            layer.name = lj[i]["name"].asString();
            layer.weight = float(lj[i]["weight"].asNumber(1.0));
            r.vec3s(lj[i]["delta"], "layer offsets", nv, layer.delta);
            m.layers.push_back(std::move(layer));
        }
    }
    if (!r.error.empty()) {
        error = r.error;
        return false;
    }
    if (levelData || !topology) {
        if (!m.layers.empty()) m.evaluateAll();
        if (!topology) m.sanitizeAttributes();
        return true;
    }
    m.rebuildTopology();
    if (!m.layers.empty()) m.evaluateAll();
    m.recomputeAllNormals();
    return true;
}

void finishMesh(Mesh& m) {
    m.rebuildTopology();
    m.recomputeAllNormals();
}

// Paint layers (core/PaintLayer.h) are their own SubTool key, "paint_layers", next to "mesh":
// readers that don't know it load `mesh.colors`, the composited (flattened) paint.
Json writePaintLayers(Writer& w, const std::string& prefix, const Mesh& m) {
    Json out = Json::object();
    out.set("active", m.activePaintLayer);
    out.set("base", w.vec3s(prefix + "paint/base.bin", m.paintBase));
    Json layers = Json::array();
    for (size_t i = 0; i < m.paintLayers.size(); ++i) {
        const PaintLayer& pl = m.paintLayers[i];
        Json l = Json::object();
        l.set("name", pl.name);
        l.set("opacity", double(pl.opacity));
        l.set("blend", blendModeId(pl.blend));
        l.set("visible", pl.visible);
        l.set("locked", pl.locked);
        l.set("color", w.vec3s(prefix + "paint/" + std::to_string(i) + "_color.bin", pl.color));
        l.set("alpha", w.floats(prefix + "paint/" + std::to_string(i) + "_alpha.bin", pl.alpha.data(), pl.alpha.size(), 1));
        layers.push(std::move(l));
    }
    out.set("layers", std::move(layers));
    return out;
}

bool readPaintLayers(Reader& r, const Json& pj, Mesh& m, std::string& error) {
    if (!pj.isObject() || pj["layers"].size() == 0) return true;
    const size_t nv = m.vertexCount();
    r.vec3s(pj["base"], "paint base layer", nv, m.paintBase);
    for (size_t i = 0; i < pj["layers"].size() && r.error.empty(); ++i) {
        const Json& lj = pj["layers"][i];
        PaintLayer l;
        l.name = lj["name"].asString();
        if (l.name.empty()) l.name = "Layer " + std::to_string(i + 1);
        l.opacity = std::clamp(float(lj["opacity"].asNumber(1.0)), 0.0f, 1.0f);
        if (!parseBlendMode(lj["blend"].asString(), l.blend)) l.blend = BlendMode::Normal; // a newer mode
        l.visible = lj["visible"].asBool(true);
        l.locked = lj["locked"].asBool(false);
        r.vec3s(lj["color"], "paint layer colours", nv, l.color);
        l.alpha.resize(nv);
        r.floats(lj["alpha"], "paint layer coverage", nv, 1, l.alpha.data());
        m.paintLayers.push_back(std::move(l));
    }
    if (!r.error.empty()) {
        error = r.error;
        return false;
    }
    const double active = pj["active"].asNumber(-1);
    m.activePaintLayer = active >= 0 && active < double(m.paintLayers.size()) ? int(active) : -1;
    sanitizePaintLayers(m); // recomposites if `colors` was missing
    return true;
}

// Texture painting (core/Texture.h) is the SubTool key "texture" (format 3): the size, where paint goes,
// and every layer as a PNG blob (top row = v 1, as image files are). Without layers "base" is the texture.
Json pngBlob(Writer& w, const std::string& file, int width, int height, const std::vector<uint8_t>& rgba) {
    Image img;
    img.width = width;
    img.height = height;
    img.rgba.resize(rgba.size());
    const size_t row = size_t(width) * 4;
    for (int y = 0; y < height; ++y) std::memcpy(&img.rgba[size_t(height - 1 - y) * row], &rgba[size_t(y) * row], row);
    std::vector<uint8_t> png;
    img.encodePng(png);
    Json d = w.bytes(file, png);
    d.set("encoding", "png");
    return d;
}

bool readPngBlob(Reader& r, const Json& desc, const char* what, int width, int height, std::vector<uint8_t>& rgba,
                 std::string& error) {
    const size_t n = countOf(desc["count"]);
    std::vector<uint8_t> png;
    const bool ok = n != size_t(-1) && n < (size_t(1) << 31) && r.bytes(desc, what, n, png);
    Image img;
    std::string why;
    if (!ok || !Image::decode(png.data(), n, img, why) || img.width != width || img.height != height) {
        error = r.error.empty() ? std::string("Project ") + what + " is not a " + std::to_string(width) + "x" +
                                      std::to_string(height) + " image" + (why.empty() ? "" : " (" + why + ")")
                                : r.error;
        return false;
    }
    rgba.resize(img.rgba.size());
    const size_t row = size_t(width) * 4;
    for (int y = 0; y < height; ++y) std::memcpy(&rgba[size_t(y) * row], &img.rgba[size_t(height - 1 - y) * row], row);
    return true;
}

Json writeTexture(Writer& w, const std::string& prefix, const Mesh& m) {
    const PaintTexture& t = *m.texture;
    Json out = Json::object();
    out.set("width", t.width);
    out.set("height", t.height);
    out.set("paint_to_texture", m.paintToTexture);
    out.set("active", t.activeLayer);
    out.set("base", pngBlob(w, prefix + "texture/base.png", t.width, t.height, t.layers.empty() ? t.pixels : t.base));
    Json layers = Json::array();
    for (size_t i = 0; i < t.layers.size(); ++i) {
        const TextureLayer& tl = t.layers[i];
        Json l = Json::object();
        l.set("name", tl.name);
        l.set("opacity", double(tl.opacity));
        l.set("blend", blendModeId(tl.blend));
        l.set("visible", tl.visible);
        l.set("locked", tl.locked);
        l.set("rgba", pngBlob(w, prefix + "texture/" + std::to_string(i) + ".png", t.width, t.height, tl.rgba));
        // A Fill layer (format 7, core/LayerStack.h): its settings and painted mask; `rgba` is its last evaluation,
        // so readers that don't know Fill layers show it as a paint layer.
        if (tl.fill) {
            l.set("fill", fillLayerJson(*tl.fill));
            if (tl.mask.size() == t.texelCount() * 4)
                l.set("mask", pngBlob(w, prefix + "texture/" + std::to_string(i) + "_mask.png", t.width, t.height, tl.mask));
        }
        layers.push(std::move(l));
    }
    out.set("layers", std::move(layers));
    // Material channels (format 7, core/LayerStackIO.h); absent for a base-colour-only texture.
    if (Json pbr = writeTextureChannels(t, prefix, [&](const std::string& file, const std::vector<uint8_t>& bytes) {
            return w.bytes(file, bytes);
        });
        pbr.isObject())
        out.set("pbr", std::move(pbr));
    return out;
}

bool readTexture(Reader& r, const Json& tj, Mesh& m, std::string& error) {
    if (!tj.isObject()) return true;
    const double w = tj["width"].asNumber(0), h = tj["height"].asNumber(0);
    if (!(w >= 1 && h >= 1 && w <= 16384 && h <= 16384)) {
        error = "Project texture has an invalid size";
        return false;
    }
    auto tex = makeTexture(int(w), int(h));
    std::vector<uint8_t> base;
    if (!readPngBlob(r, tj["base"], "texture", tex->width, tex->height, base, error)) return false;
    for (size_t i = 0; i < tj["layers"].size(); ++i) {
        const Json& lj = tj["layers"][i];
        TextureLayer l;
        l.name = lj["name"].asString();
        if (l.name.empty()) l.name = "Layer " + std::to_string(i + 1);
        l.opacity = std::clamp(float(lj["opacity"].asNumber(1.0)), 0.0f, 1.0f);
        if (!parseBlendMode(lj["blend"].asString(), l.blend)) l.blend = BlendMode::Normal;
        l.visible = lj["visible"].asBool(true);
        l.locked = lj["locked"].asBool(false);
        if (!readPngBlob(r, lj["rgba"], "texture layer", tex->width, tex->height, l.rgba, error)) return false;
        if (lj["fill"].isObject()) {
            auto fill = std::make_shared<FillLayer>();
            if (!fillLayerFromJson(lj["fill"], *fill, error)) return false;
            l.fill = std::move(fill);
            if (lj["mask"].isObject() && !readPngBlob(r, lj["mask"], "fill layer mask", tex->width, tex->height, l.mask, error))
                return false;
        }
        tex->layers.push_back(std::move(l));
    }
    if (tex->layers.empty()) {
        tex->pixels = std::move(base);
    } else {
        tex->base = std::move(base);
        const double active = tj["active"].asNumber(-1);
        tex->activeLayer = active >= 0 && active < double(tex->layers.size()) ? int(active) : -1;
        compositeTextureAll(*tex);
    }
    for (size_t i = 3; i < (tex->layers.empty() ? tex->pixels : tex->base).size(); i += 4)
        (tex->layers.empty() ? tex->pixels : tex->base)[i] = 255; // Base is opaque
    if (!readTextureChannels(tj["pbr"], *tex,
                             [&](const Json& desc, const char* what, size_t maxBytes, std::vector<uint8_t>& out) {
                                 const size_t n = countOf(desc["count"]);
                                 return n != size_t(-1) && n <= maxBytes && r.bytes(desc, what, n, out);
                             },
                             error)) {
        if (!r.error.empty()) error = r.error;
        return false;
    }
    tex->markAllDirty();
    m.texture = std::move(tex);
    m.paintToTexture = tj["paint_to_texture"].asBool(true);
    return true;
}

// Subdivision levels (core/Multires.h) are the SubTool key "subdiv" (format 4; sculpt-layer details in local
// frames from format 5): the current level and every level's mesh (as "mesh" / "layers" / "paint_layers"),
// details and topology (scheme, Smt, quad pairing and child maps). The SubTool's own "mesh" is the current level,
// so readers that don't know levels load what was shown. A current level identical to that mesh is not written
// twice ("same_as_mesh"). Format 6: a generated level (made by Divide, SubdivTopology::generated) has no
// triangles and no child maps ("generated"), and a level without "detail" that isn't stale takes the detail that
// fits its positions (SubdivLevel::detail).
Json writeSubdiv(Writer& w, const std::string& prefix, const Mesh& m) {
    Json out = Json::object();
    out.set("current", subdivCurrentLevel(m));
    Json levels = Json::array();
    const bool inSync = subdivInSync(m);
    for (int i = 0; i < subdivLevelCount(m); ++i) {
        const std::string p = prefix + "subdiv/" + std::to_string(i) + "/";
        const SubdivLevel& l = subdivLevel(m, i);
        const Mesh& data = *l.data;
        const bool generated = l.topo && l.topo->generated;
        Json lj = Json::object();
        if (i == subdivCurrentLevel(m) && inSync) {
            lj.set("same_as_mesh", true);
        } else {
            writeMesh(w, p, data, lj, generated ? l.triangleCount() : 0);
            if (hasPaintLayers(data)) lj.set("paint_layers", writePaintLayers(w, p, data));
        }
        if (l.stale) lj.set("stale", true);
        if (l.topo) {
            const SubdivTopology& tp = *l.topo;
            Json t = Json::object();
            t.set("catmull_clark", tp.catmullClark);
            t.set("smooth", tp.smooth);
            if (const std::vector<uint32_t> pairing = tp.pairing(); !pairing.empty())
                t.set("pairing", w.ownedUints(p + "pairing.bin", pairing));
            if (generated) {
                t.set("generated", true);
            } else {
                t.set("vertex_child", w.uints(p + "vertex_child.bin", tp.vertexChild.data(), tp.vertexChild.size(), 1));
                t.set("edge_child", w.uints(p + "edge_child.bin", tp.edgeChild.data(), tp.edgeChild.size(), 1));
                if (!tp.faceChild.empty()) t.set("face_child", w.uints(p + "face_child.bin", tp.faceChild.data(), tp.faceChild.size(), 1));
            }
            lj.set("topology", std::move(t));
        }
        if (l.detail) lj.set("detail", w.vec3s(p + "detail.bin", *l.detail));
        Json ld = Json::array();
        for (size_t k = 0; k < l.layerDetail.size(); ++k)
            ld.push(l.layerDetail[k] ? w.vec3s(p + "layer_detail_" + std::to_string(k) + ".bin", *l.layerDetail[k]) : Json());
        if (!l.layerDetail.empty()) lj.set("layer_detail", std::move(ld));
        levels.push(std::move(lj));
    }
    out.set("levels", std::move(levels));
    return out;
}

bool readUintList(Reader& r, const Json& desc, const char* what, std::vector<uint32_t>& out) {
    const size_t n = countOf(desc["count"]);
    if (n == size_t(-1) || n > (size_t(1) << 31)) {
        r.error = std::string("Project ") + what + " has an invalid size";
        return false;
    }
    out.resize(n);
    return r.uints(desc, what, n, 1, out.data());
}

// Reads "subdiv" into `m` (the SubTool's current level). Levels that don't fit are an error. Format 4 kept the
// sculpt-layer details in world space (`worldLayerDetail`): they're converted to local frames.
bool readSubdiv(Reader& r, const Json& sj, Mesh& m, std::string& error, bool worldLayerDetail) {
    if (!sj.isObject()) return true;
    const Json& list = sj["levels"];
    if (!list.isArray() || list.size() < 2 || list.size() > 32) {
        error = "Project has invalid subdivision levels";
        return false;
    }
    MultiresStack stack;
    const double current = sj["current"].asNumber(-1);
    if (current < 0 || current >= double(list.size())) {
        error = "Project has an invalid current subdivision level";
        return false;
    }
    stack.current = int(current);
    // The triangles of the level below (its own, or generated into one of the two buffers), to build each level's
    // topology from.
    const std::vector<glm::uvec3>* below = nullptr;
    std::vector<glm::uvec3> generatedBelow[2];
    for (size_t i = 0; i < list.size(); ++i) {
        const Json& lj = list[i];
        const bool generated = i > 0 && lj["topology"]["generated"].asBool(false);
        SubdivLevel level;
        auto data = std::make_shared<Mesh>();
        if (lj["same_as_mesh"].asBool(false) && i == size_t(stack.current)) {
            // Just the level's arrays (no topology, normals or texture), as the stack stores levels.
            if (!generated) data->triangles = m.triangles;
            data->positions = m.positions;
            data->mask = m.mask;
            data->colors = m.colors;
            data->polygroups = m.polygroups;
            data->hidden = m.hidden;
            data->uvs = m.uvs;
            data->quadPartners = m.quadPartners;
            data->base = m.base;
            data->layers = m.layers;
            data->paintBase = m.paintBase;
            data->paintLayers = m.paintLayers;
            data->activePaintLayer = m.activePaintLayer;
        } else {
            if (!readMesh(r, lj["mesh"], lj["layers"], *data, error, true)) return false;
            if (!readPaintLayers(r, lj["paint_layers"], *data, error)) return false;
            if (data->triangles.empty() && !generated && countOf(lj["mesh"]["triangle_count"]) != 0) {
                error = "Project subdivision level " + std::to_string(i + 1) + " has no triangles";
                return false;
            }
        }
        level.data = std::move(data);
        level.stale = lj["stale"].asBool(false);
        const size_t nv = level.data->vertexCount();
        if (lj.has("detail")) {
            level.detail = std::make_shared<std::vector<glm::vec3>>();
            if (!r.vec3s(lj["detail"], "subdivision detail", nv, *level.detail)) {
                error = r.error;
                return false;
            }
        }
        for (const Json& d : lj["layer_detail"].items()) {
            if (!d.isObject()) {
                level.layerDetail.push_back(nullptr);
                continue;
            }
            auto v = std::make_shared<std::vector<glm::vec3>>();
            if (!r.vec3s(d, "subdivision layer detail", nv, *v)) {
                error = r.error;
                return false;
            }
            level.layerDetail.push_back(std::move(v));
        }
        if (i > 0) {
            const Json& t = lj["topology"];
            std::vector<uint32_t> pairing, vc, ec, fc;
            if ((t.has("pairing") && !readUintList(r, t["pairing"], "subdivision quad pairing", pairing)) ||
                (!generated && (!readUintList(r, t["vertex_child"], "subdivision vertex map", vc) ||
                                !readUintList(r, t["edge_child"], "subdivision edge map", ec) ||
                                (t.has("face_child") && !readUintList(r, t["face_child"], "subdivision face map", fc))))) {
                error = r.error;
                return false;
            }
            level.topo = rebuildSubdivTopology(*below, uint32_t(stack.levels[i - 1].data->vertexCount()), uint32_t(nv),
                                               generated ? nullptr : &level.data->triangles, std::move(pairing),
                                               t["catmull_clark"].asBool(false), t["smooth"].asBool(true),
                                               std::move(vc), std::move(ec), std::move(fc), error);
            if (!level.topo) {
                error = "Project " + error;
                return false;
            }
            if (generated && level.topo->fineTriangles != countOf(lj["mesh"]["triangle_count"]) && !lj["same_as_mesh"].asBool(false)) {
                error = "Project subdivision level " + std::to_string(i + 1) + " doesn't fit the level below";
                return false;
            }
        }
        if (i + 1 < list.size()) { // the next level is built on this one's triangles
            if (level.topo && level.topo->generated && level.data->triangles.empty()) {
                generateFaces(*level.topo, generatedBelow[i % 2], nullptr);
                below = &generatedBelow[i % 2];
            } else {
                below = &level.data->triangles;
            }
        }
        stack.levels.push_back(std::move(level));
    }
    if (worldLayerDetail) convertWorldLayerDetail(stack);
    if (!attachSubdivStack(m, std::move(stack), error)) {
        error = "Project " + error;
        return false;
    }
    return true;
}

// Selection Sets (core/SelectionSets.h): `selection_sets` = {"version", "morph_target", "sets": [summary + "data"]},
// each set one packed u8 blob under subtools/i/sets/. Optional, so no format version bump: older readers ignore it.
constexpr int kSelectionSetsVersion = 1;

Json writeSelectionSets(Writer& w, const std::string& prefix, const SelectionSetList& list) {
    Json j = Json::object();
    j.set("version", kSelectionSetsVersion);
    if (!list.morphTarget.empty()) j.set("morph_target", list.morphTarget);
    Json sets = Json::array();
    for (size_t k = 0; k < list.sets.size(); ++k) {
        Json one = selectionSetSummary(*list.sets[k]);
        std::vector<uint8_t> packed = packSelectionSet(*list.sets[k]);
        const size_t n = packed.size();
        one.set("data", w.add(prefix + "sets/" + std::to_string(k) + ".bin", Scalar::U8, 1, n, std::move(packed)));
        sets.push(std::move(one));
    }
    j.set("sets", std::move(sets));
    return j;
}

// Sets that can't be read (damaged, or from a newer set format) are left out and named in `skipped`; the rest of
// the project still loads.
void readSelectionSets(Reader& r, const Json& j, Mesh& m, std::string& skipped) {
    if (!j.isObject() || !j["sets"].isArray()) return;
    auto list = std::make_shared<SelectionSetList>();
    for (const Json& one : j["sets"].items()) {
        const Json& desc = one["data"];
        auto it = r.files.find(desc["file"].asString());
        const double count = desc["count"].asNumber(-1);
        std::vector<uint8_t> bytes;
        SelectionSet set;
        std::string why;
        const std::string saved = r.error;
        const bool ok = it != r.files.end() && count >= 0 && double(it->second.size) == count &&
                        r.bytes(desc, "selection set", size_t(count), bytes) &&
                        unpackSelectionSet(bytes.data(), bytes.size(), set, why) && list->find(set.name) < 0;
        r.error = saved;
        if (!ok) {
            skipped += (skipped.empty() ? "" : ", ") + ("'" + one["name"].asString() + "'");
            continue;
        }
        list->sets.push_back(std::make_shared<const SelectionSet>(std::move(set)));
    }
    list->morphTarget = j["morph_target"].asString();
    if (list->find(list->morphTarget) < 0) list->morphTarget.clear();
    if (!list->sets.empty()) m.selectionSets = std::move(list);
}

// A SubTool read from a project, before it joins the document.
struct LoadedSubtool {
    std::string name, gozName;
    std::vector<QRemeshGuide> qremeshGuides;
    bool visible = true;
    Shading shading = Shading::Default;
    int activeLayer = -1;
    Mesh mesh;
    // A big mesh's BVH is built on its own thread once the positions and triangles are read (they don't change
    // after that), while the subdivision levels, topology and normals load.
    BVH bvh;
    std::thread bvhBuilder;
    LoadedSubtool() = default;
    LoadedSubtool(const LoadedSubtool&) = delete;
    ~LoadedSubtool() { finishBvh(); }
    void startBvh() {
        if (mesh.triangleCount() >= 1'000'000) bvhBuilder = std::thread([this] { bvh.build(mesh); });
    }
    // The BVH to hand over (null: build it on the spot).
    BVH* finishBvh() {
        if (bvhBuilder.joinable()) {
            bvhBuilder.join();
            built = true;
        }
        return built ? &bvh : nullptr;
    }
    bool built = false;
};

constexpr size_t kMaxProjectSubtools = 100000;
} // namespace

bool isProjectPath(const std::string& path) {
    std::string ext = fsPath(path).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return ext == kProjectExtension;
}

bool saveProject(const std::string& path, const Document& doc, std::string& error) {
    Writer w;
    Json manifest = Json::object();
    manifest.set("format", kFormatName);
    ProjectVersionNeed version; // the lowest format the content needs (set again once the SubTools are written)
    manifest.set("version", version.version);
    manifest.set("app_version", SCULPTFORGE_VERSION);

    // Every SubTool with its mesh, layers and state; blobs live under subtools/<i>/.
    Json subtools = Json::array();
    for (int i = 0; i < doc.subtoolCount(); ++i) {
        const SubTool& s = doc.subtools[size_t(i)];
        const bool active = i == doc.activeSubtool;
        Json st = Json::object();
        st.set("name", s.name);
        st.set("visible", s.visible);
        if (s.shading != Shading::Default) st.set("shading", shadingName(s.shading)); // (no format change: optional)
        writeMesh(w, "subtools/" + std::to_string(i) + "/", doc.subtoolMesh(i), st);
        st.set("active_layer", active ? doc.activeLayer : s.activeLayer);
        if (hasPaintLayers(doc.subtoolMesh(i)))
            st.set("paint_layers", writePaintLayers(w, "subtools/" + std::to_string(i) + "/", doc.subtoolMesh(i)));
        if (doc.subtoolMesh(i).texture && doc.subtoolMesh(i).texture->width > 0) {
            st.set("texture", writeTexture(w, "subtools/" + std::to_string(i) + "/", doc.subtoolMesh(i)));
            version.need(textureFormatVersion(*doc.subtoolMesh(i).texture));
        }
        if (subdivLevelCount(doc.subtoolMesh(i)))
            st.set("subdiv", writeSubdiv(w, "subtools/" + std::to_string(i) + "/", doc.subtoolMesh(i)));
        if (const Mesh& m = doc.subtoolMesh(i); m.micro && microMatches(m)) { // format 7: the micro level (core/Micro.h)
            const MicroMesh& mm = *m.micro;
            const std::string p = "subtools/" + std::to_string(i) + "/micro/";
            Json mj = Json::object();
            mj.set("level", mm.level);
            mj.set("step", double(mm.step));
            // Tiles packed (delta + per-tile range, core/MicroStore.h): untouched tiles are 3 bytes.
            std::vector<uint8_t> packed;
            microPackTiles(mm.disp.data(), mm.baseTriangles, mm.samples, packed);
            const size_t packedBytes = packed.size();
            mj.set("displacement_packed", w.add(p + "displacement.pack", Scalar::U8, 1, packedBytes, std::move(packed)));
            if (mm.hasMask()) mj.set("mask", w.externalBytes(p + "mask.bin", mm.mask));
            st.set("micro", std::move(mj));
            version.need(7);
        }
        const std::string& gozName = active ? doc.gozName : s.gozName;
        if (!gozName.empty()) st.set("goz_name", gozName);
        const std::vector<QRemeshGuide>& guides = active ? doc.qremeshGuides : s.qremeshGuides;
        if (!guides.empty()) st.set("qremesh_guides", qremeshGuidesToJson(guides)); // (no format change: optional)
        if (const Mesh& m = doc.subtoolMesh(i); m.selectionSets && !m.selectionSets->sets.empty())
            st.set("selection_sets", writeSelectionSets(w, "subtools/" + std::to_string(i) + "/", *m.selectionSets));
        if (const Mesh& m = doc.subtoolMesh(i); !m.zspheres.empty()) { // a ZSphere SubTool (core/ZSpheres.h); optional
            st.set("zspheres", zspheresToJson(m.zspheres));
            if (m.zspherePreview) st.set("zsphere_preview", true);
        }
        subtools.push(std::move(st));
    }
    manifest.set("subtools", std::move(subtools));
    manifest.set("active_subtool", doc.activeSubtool);

    Json brushes = Json::array();
    for (const auto& b : doc.brushes) brushes.push(b.toJson());
    manifest.set("brushes", std::move(brushes));
    if (doc.activeBrush >= 0 && doc.activeBrush < int(doc.brushes.size()))
        manifest.set("active_brush", doc.brushes[size_t(doc.activeBrush)].name);

    if (!doc.splats.empty()) {
        const SplatCloud& s = doc.splats;
        if (std::string v = s.validate(); !v.empty()) {
            error = "Cannot save splats: " + v;
            return false;
        }
        Json splats = Json::object();
        splats.set("count", double(s.size()));
        splats.set("sh_degree", s.shDegree);
        splats.set("name", s.name);
        splats.set("transform", matrixJson(s.transform));
        splats.set("positions", w.vec3s("splats/positions.bin", s.positions));
        std::vector<float> rot;
        rot.reserve(s.size() * 4);
        for (const auto& q : s.rotations) rot.insert(rot.end(), {q.x, q.y, q.z, q.w});
        splats.set("rotations", w.add("splats/rotations.bin", Scalar::F32, 4, s.size(), littleEndian(rot.data(), rot.size())));
        splats.set("scales", w.vec3s("splats/scales.bin", s.scales));
        splats.set("opacities", w.floats("splats/opacities.bin", s.opacities.data(), s.size(), 1));
        splats.set("sh", w.vec3s("splats/sh.bin", s.sh));
        manifest.set("splats", std::move(splats));
    }

    if (!doc.projectFiles.empty()) { // (no format change: optional, older readers ignore it)
        Json files = Json::array();
        for (const auto& [name, bytes] : doc.projectFiles) {
            if (!validAttachmentName(name)) continue;
            Json f = Json::object();
            f.set("name", name);
            f.set("data", w.bytes("attachments/" + name, bytes));
            files.push(std::move(f));
        }
        manifest.set("attachments", std::move(files));
    }

    if (doc.projectSettings.isObject())
        for (const auto& [key, value] : doc.projectSettings.members())
            if (!isCoreKey(key)) manifest.set(key, value);

    manifest.set("version", version.version);
    const std::string text = manifest.dump(true);
    w.entries.insert(w.entries.begin(), ZipEntry{kManifestName, std::vector<uint8_t>(text.begin(), text.end())});
    // The preview goes second, so loadProjectThumbnail finds it after reading two local headers.
    if (const ProjectThumbnailProvider& provider = thumbnailProvider()) {
        Image thumb;
        std::vector<uint8_t> png;
        if (provider(doc, thumb) && !thumb.empty() && thumb.encodePng(png))
            w.entries.insert(w.entries.begin() + 1, ZipEntry{kThumbnailName, std::move(png)});
    }

    // Write next to the target and rename, so a failed save never destroys the previous file.
    const std::string tmp = path + ".tmp";
    if (!writeStoredZip(tmp, w.entries, 64, error)) return false;
    std::error_code ec;
    std::filesystem::rename(fsPath(tmp), fsPath(path), ec);
    if (ec) {
        std::filesystem::remove(fsPath(tmp), ec);
        error = "Cannot write " + path;
        return false;
    }
    return true;
}

bool loadProject(const std::string& path, Document& doc, std::string& error) {
    Reader r;
    if (!r.open(path, error)) return false;
    std::vector<uint8_t> manifestText;
    if (!r.entry(kManifestName, manifestText)) {
        error = path + " is not a SculptForge project (no manifest)";
        return false;
    }
    Json manifest;
    if (!Json::parse(std::string(manifestText.begin(), manifestText.end()), manifest, error) || !manifest.isObject() ||
        manifest["format"].asString() != kFormatName) {
        error = path + " is not a SculptForge project" + (error.empty() ? "" : " (" + error + ")");
        return false;
    }
    const double version = manifest["version"].asNumber(0);
    if (version > kProjectVersion) {
        error = path + " was saved by a newer SculptForge (" + manifest["app_version"].asString() +
                ", project format " + std::to_string(int(version)) + "); this version reads format " +
                std::to_string(kProjectVersion) + ". Please update SculptForge to open it.";
        return false;
    }
    if (version < 1) {
        error = path + " has an invalid project version";
        return false;
    }

    // SubTools: version 1 has one mesh at the top level, version 2 a "subtools" list.
    std::deque<LoadedSubtool> subtools; // (stable addresses: BVH threads use them)
    std::string skippedSets;            // selection sets that couldn't be read
    int activeSubtool = 0;
    if (version < 2) {
        LoadedSubtool& s = subtools.emplace_back();
        s.name = "Sculpt";
        if (!readMesh(r, manifest["mesh"], manifest["layers"], s.mesh, error)) return false;
        s.activeLayer = int(manifest["active_layer"].asNumber(-1));
    } else {
        const Json& list = manifest["subtools"];
        if (!list.isArray() || list.size() == 0 || list.size() > kMaxProjectSubtools) {
            error = "Project has no valid Part list";
            return false;
        }
        for (const Json& sj : list.items()) {
            LoadedSubtool& s = subtools.emplace_back();
            s.name = sj["name"].asString();
            if (s.name.empty()) s.name = "Part";
            s.visible = sj["visible"].asBool(true);
            if (sj["shading"].isString()) parseShading(sj["shading"].asString(), s.shading); // unknown: Default
            s.gozName = sj["goz_name"].asString();
            s.qremeshGuides = qremeshGuidesFromJson(sj["qremesh_guides"]);
            if (!readMesh(r, sj["mesh"], sj["layers"], s.mesh, error, false, false)) return false;
            s.mesh.zspheres = zspheresFromJson(sj["zspheres"]);
            s.mesh.zspherePreview = !s.mesh.zspheres.empty() && sj["zsphere_preview"].asBool(false);
            readSelectionSets(r, sj["selection_sets"], s.mesh, skippedSets);
            // The BVH builds (on the pool) while the levels and topology load (off it), then the normals.
            s.startBvh();
            r.parallelReads = !s.bvhBuilder.joinable();
            if (!readPaintLayers(r, sj["paint_layers"], s.mesh, error)) return false;
            if (!readTexture(r, sj["texture"], s.mesh, error)) return false;
            if (!readSubdiv(r, sj["subdiv"], s.mesh, error, version < 5)) return false;
            s.mesh.rebuildTopology();
            s.finishBvh();
            r.parallelReads = true;
            s.mesh.recomputeAllNormals();
            if (const Json& mj = sj["micro"]; mj.isObject() && version >= 7) {
                const int level = int(mj["level"].asNumber(0));
                if (level < 1 || level > MicroMesh::kMaxLevel) {
                    error = "Project micro level is invalid";
                    return false;
                }
                const uint32_t n = 1u << level;
                const size_t count = s.mesh.triangleCount() * size_t((n + 1) * (n + 2) / 2);
                const uint32_t samples = (n + 1) * (n + 2) / 2;
                MicroStore disp;
                std::vector<uint8_t> mask;
                if (const Json& pj = mj["displacement_packed"]; pj.isObject()) { // tiles packed (MicroStore.h)
                    std::vector<uint8_t> packed;
                    const size_t bytes = countOf(pj["count"]);
                    if (bytes == size_t(-1) || !r.bytes(pj, "micro displacement", bytes, packed)) {
                        error = r.error.empty() ? "Project micro displacement is invalid" : r.error;
                        return false;
                    }
                    std::string why;
                    if (microWantsOutOfCore(count)) disp.assignMapped(count, why);
                    else disp.assign(count, 0);
                    if (!microUnpackTiles(packed.data(), packed.size(), s.mesh.triangleCount(), samples, disp.data())) {
                        error = "Project micro displacement is damaged";
                        return false;
                    }
                } else {
                    std::vector<int16_t> plain;
                    if (!r.int16s(mj["displacement"], "micro displacement", count, plain)) {
                        error = r.error;
                        return false;
                    }
                    disp = std::move(plain);
                }
                if (mj["mask"].isObject() && !r.bytes(mj["mask"], "micro mask", count, mask)) {
                    error = r.error;
                    return false;
                }
                if (!microAttach(s.mesh, level, float(mj["step"].asNumber(0)), std::move(disp), std::move(mask), error)) return false;
            }
            s.activeLayer = int(sj["active_layer"].asNumber(-1));
        }
        const double active = manifest["active_subtool"].asNumber(0);
        activeSubtool = active >= 0 && active < double(subtools.size()) ? int(active) : 0;
    }

    // Splats
    SplatCloud splats;
    if (const Json& sj = manifest["splats"]; sj.isObject()) {
        const size_t n = countOf(sj["count"]);
        const int degree = int(sj["sh_degree"].asNumber(0));
        if (n == size_t(-1) || degree < 0 || degree > 3) {
            error = "Project splats have an invalid size";
            return false;
        }
        splats.resize(n, degree);
        splats.name = sj["name"].isString() ? sj["name"].asString() : splats.name;
        splats.transform = matrixFromJson(sj["transform"]);
        std::vector<float> rot(n * 4);
        r.vec3s(sj["positions"], "splat positions", n, splats.positions);
        r.floats(sj["rotations"], "splat rotations", n, 4, rot.data());
        r.vec3s(sj["scales"], "splat scales", n, splats.scales);
        r.floats(sj["opacities"], "splat opacities", n, 1, splats.opacities.data());
        r.vec3s(sj["sh"], "splat colours", n * size_t(splats.shCount()), splats.sh);
        if (!r.error.empty()) {
            error = r.error;
            return false;
        }
        for (size_t i = 0; i < n; ++i) splats.rotations[i] = glm::quat(rot[i * 4 + 3], rot[i * 4], rot[i * 4 + 1], rot[i * 4 + 2]);
    }

    // Everything parsed: replace the scene. SubTool 0 is checked out first; the others are added
    // behind it, given their layer and GoZ name, and the saved active SubTool is selected last.
    auto validLayer = [](int layer, const Mesh& m) { return layer >= 0 && layer < int(m.layers.size()) ? layer : -1; };
    BVH* built = subtools[0].finishBvh();
    doc.setMesh(std::move(subtools[0].mesh), subtools[0].name, built);
    doc.activeLayer = validLayer(subtools[0].activeLayer, doc.mesh);
    doc.gozName = subtools[0].gozName;
    doc.qremeshGuides = std::move(subtools[0].qremeshGuides);
    doc.subtools[0].visible = subtools[0].visible;
    doc.subtools[0].shading = subtools[0].shading;
    for (size_t i = 1; i < subtools.size(); ++i) {
        BVH* builtI = subtools[i].finishBvh();
        const int at = doc.addSubtool(std::move(subtools[i].mesh), subtools[i].name, false, -1, builtI);
        SubTool& s = doc.subtools[size_t(at)];
        s.visible = subtools[i].visible;
        s.shading = subtools[i].shading;
        s.activeLayer = validLayer(subtools[i].activeLayer, s.mesh);
        s.gozName = subtools[i].gozName;
        s.qremeshGuides = std::move(subtools[i].qremeshGuides);
    }
    doc.selectSubtool(activeSubtool);
    doc.splats = std::move(splats);
    doc.splatsChanged();
    if (!skippedSets.empty())
        doc.notice += (doc.notice.empty() ? "" : "; ") + std::string("Selection sets left out (unreadable): ") + skippedSets;
    for (const auto& b : manifest["brushes"].items()) {
        if (!b.isObject()) continue;
        // Brushes the library already has, unchanged, stay library brushes; the rest are the project's own.
        BrushRecipe recipe = BrushRecipe::fromJson(b);
        const std::string json = recipe.toJson().dump();
        const bool known = std::any_of(doc.brushes.begin(), doc.brushes.end(), [&](const BrushRecipe& have) {
            return have.name == recipe.name && have.toJson().dump() == json;
        });
        if (known) continue;
        recipe.origin.source = BrushSource::Project;
        doc.addBrush(std::move(recipe));
    }
    doc.activeBrush = -1;
    const std::string& activeBrush = manifest["active_brush"].asString();
    for (size_t i = 0; i < doc.brushes.size() && !activeBrush.empty(); ++i)
        if (doc.brushes[i].name == activeBrush) doc.activeBrush = int(i);
    doc.projectSettings = Json::object();
    for (const auto& [key, value] : manifest.members())
        if (!isCoreKey(key)) doc.projectSettings.set(key, value);
    // Attachments are extras (an embedded MatCap): one that can't be read is left out rather than failing the file.
    doc.projectFiles.clear();
    for (const Json& f : manifest["attachments"].items()) {
        const std::string name = f["name"].asString();
        const size_t n = countOf(f["data"]["count"]);
        if (!validAttachmentName(name) || n == size_t(-1) || n > kMaxAttachmentSize || doc.projectFiles.size() >= 64) continue;
        std::vector<uint8_t> bytes;
        r.error.clear();
        if (r.bytes(f["data"], "attachment", n, bytes)) doc.projectFiles[name] = std::move(bytes);
    }
    r.error.clear();
    doc.path = path;
    doc.markClean();
    return true;
}

void setProjectThumbnailProvider(ProjectThumbnailProvider provider) { thumbnailProvider() = std::move(provider); }

bool loadProjectThumbnail(const std::string& path, Image& out, std::string& error) {
    std::ifstream f(fsPath(path), std::ios::binary);
    if (!f) {
        error = "Cannot open " + path;
        return false;
    }
    // Walk the local file headers from the start: the thumbnail is the second entry when present,
    // so this reads a few hundred bytes plus the PNG instead of the whole project.
    for (int entry = 0; entry < 4; ++entry) {
        uint8_t h[30];
        if (!f.read(reinterpret_cast<char*>(h), sizeof h) || get32(h) != 0x04034B50) break;
        const uint32_t method = uint32_t(h[8]) | uint32_t(h[9]) << 8;
        const uint32_t size = get32(h + 18);
        const size_t nameLen = size_t(h[26]) | size_t(h[27]) << 8, extraLen = size_t(h[28]) | size_t(h[29]) << 8;
        std::string name(nameLen, '\0');
        if (!f.read(name.data(), std::streamsize(nameLen))) break;
        f.seekg(std::streamoff(extraLen), std::ios::cur);
        if (name == kThumbnailName && method == 0) {
            std::vector<uint8_t> png(size);
            if (!f.read(reinterpret_cast<char*>(png.data()), std::streamsize(size))) break;
            return Image::decode(png.data(), png.size(), out, error);
        }
        f.seekg(std::streamoff(size), std::ios::cur);
    }
    error = path + " has no thumbnail";
    return false;
}

CommandResult openProjectCommand(Document& doc, const std::string& path) {
    std::string err;
    if (!loadProject(path, doc, err)) return {false, err};
    std::string msg = "Opened project " + std::filesystem::path(path).filename().string() + " (";
    // The Sculpt Timeline saved beside it, if it matches (only into the app's own scene: not thumbnails, scratch opens).
    const int histories = &doc == Timeline::historyDocument ? loadHistorySidecar(doc, path) : 0;
    if (doc.subtoolCount() > 1) msg += std::to_string(doc.subtoolCount()) + " Parts, ";
    size_t tris = 0, layers = 0;
    for (int i = 0; i < doc.subtoolCount(); ++i) {
        tris += doc.subtoolMesh(i).triangleCount();
        layers += doc.subtoolMesh(i).layers.size();
    }
    msg += std::to_string(tris) + " triangles";
    if (layers) msg += ", " + std::to_string(layers) + " layers";
    if (!doc.splats.empty()) msg += ", " + std::to_string(doc.splats.size()) + " splats";
    if (histories > 0) msg += ", with its history";
    return {true, msg + ")"};
}


void registerProjectCommands(CommandRegistry& registry) {
    using T = ParamSpec::Type;
    registry.add({"save_project",
                  "Save everything (mesh, mask, vertex paint, face groups, visibility, UVs, sculpt layers, splats, brushes "
                  "and view settings) as a SculptForge project (.sfproj).",
                  {{"path", T::String, "Output .sfproj path (UTF-8). Defaults to the current project path.", false}},
                  [](Document& doc, const Args& a) -> CommandResult {
                      std::string path = argString(a, "path", isProjectPath(doc.path) ? doc.path : std::string());
                      if (path.empty()) return {false, "No path given"};
                      // Any other extension too: "head.obj" must not overwrite an OBJ with a ZIP
                      // that open, isProjectPath and Ctrl+S would not recognise.
                      if (!isProjectPath(path)) path += kProjectExtension;
                      std::string err;
                      subdivCommit(doc.mesh); // fold pending edits into the levels, so they're written once
                      if (!saveProject(path, doc, err)) return {false, err};
                      doc.path = path;
                      doc.markClean();
                      // The Sculpt Timeline's history beside it (<name>.sfhist), when the app asks for it.
                      if (Timeline::saveWithProject && &doc == Timeline::historyDocument && !saveHistorySidecar(doc, path, &err))
                          return {true, "Saved project " + path + " (the history wasn't saved: " + err + ")"};
                      return {true, "Saved project " + path};
                  }});
    registry.add({"open_project", "Open a SculptForge project (.sfproj), replacing the current scene.",
                  {{"path", T::String, ".sfproj path (UTF-8).", true}},
                  [](Document& doc, const Args& a) -> CommandResult {
                      return openProjectCommand(doc, argString(a, "path"));
                  }});
}

// ---------------------------------------------------------------------------
// Recent files
// ---------------------------------------------------------------------------

std::vector<std::string> addRecentFile(std::vector<std::string> list, const std::string& path, size_t limit) {
    if (path.empty()) return list;
    list.erase(std::remove(list.begin(), list.end(), path), list.end());
    list.insert(list.begin(), path);
    if (list.size() > limit) list.resize(limit);
    return list;
}

std::vector<std::string> pruneRecentFiles(std::vector<std::string> list,
                                          const std::function<bool(const std::string&)>& exists) {
    std::vector<std::string> out;
    for (auto& p : list)
        if (!p.empty() && std::find(out.begin(), out.end(), p) == out.end() && exists(p)) out.push_back(std::move(p));
    return out;
}

} // namespace sf
