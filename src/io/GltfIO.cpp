// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "io/GltfIO.h"

#include "core/Json.h"
#include "io/TextureExport.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <cmath>
#include <functional>
#include <iterator>
#include <limits>
#include <map>
#include <unordered_map>

#include <array>
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace sf {

namespace {

std::string jsonEscape(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c == '"' || c == '\\') out += '\\';
        if (static_cast<unsigned char>(c) < 0x20) continue;
        out += c;
    }
    return out;
}

struct BinWriter {
    std::vector<uint8_t> data;
    // Appends 4-byte aligned data and returns its byte offset.
    size_t append(const void* src, size_t bytes) {
        size_t offset = data.size();
        data.resize(offset + bytes);
        std::memcpy(data.data() + offset, src, bytes);
        while (data.size() % 4) data.push_back(0);
        return offset;
    }
};

void minMax(const std::vector<glm::vec3>& v, glm::vec3& lo, glm::vec3& hi) {
    lo = glm::vec3(std::numeric_limits<float>::max());
    hi = glm::vec3(std::numeric_limits<float>::lowest());
    for (const auto& p : v) {
        lo = glm::min(lo, p);
        hi = glm::max(hi, p);
    }
    if (v.empty()) lo = hi = glm::vec3(0.0f);
}

std::string vec3Json(const glm::vec3& v) {
    std::ostringstream s;
    s.precision(9);
    s << "[" << v.x << "," << v.y << "," << v.z << "]";
    return s.str();
}

bool writeGlbFile(const std::string& path, std::string jsonStr, const std::vector<uint8_t>& bin, std::string& error) {
    while (jsonStr.size() % 4) jsonStr += ' ';
    const uint32_t totalLength = 12 + 8 + uint32_t(jsonStr.size()) + 8 + uint32_t(bin.size());
    std::ofstream file(std::filesystem::path(std::u8string(path.begin(), path.end())), std::ios::binary);
    if (!file) {
        error = "Cannot write " + path;
        return false;
    }
    auto u32 = [&](uint32_t v) { file.write(reinterpret_cast<const char*>(&v), 4); };
    u32(0x46546C67); // "glTF"
    u32(2);
    u32(totalLength);
    u32(uint32_t(jsonStr.size()));
    u32(0x4E4F534A); // "JSON"
    file.write(jsonStr.data(), std::streamsize(jsonStr.size()));
    u32(uint32_t(bin.size()));
    u32(0x004E4942); // "BIN\0"
    file.write(reinterpret_cast<const char*>(bin.data()), std::streamsize(bin.size()));
    if (!file) {
        error = "Write failed for " + path;
        return false;
    }
    return true;
}

} // namespace

bool saveGlb(const std::string& path, const Mesh& mesh, const std::string& name, std::string& error) {
    return saveGlb(path, std::vector<MeshObjectRef>{{name, &mesh}}, name, error);
}

bool saveGlb(const std::string& path, const std::vector<MeshObjectRef>& objects, const std::string& sceneName,
             std::string& error, const MorphAnimation* animation) {
    if (objects.empty()) {
        error = "Nothing to export";
        return false;
    }
    for (const auto& o : objects)
        if (!o.mesh || o.mesh->triangleCount() == 0) {
            error = "Mesh is empty";
            return false;
        }

    BinWriter bin;
    struct View {
        size_t offset, length;
        int target; // 34962 vertex data, 34963 indices, 0 none (images)
    };
    std::vector<View> views;
    std::ostringstream accessors, meshes, nodes, images, materials;
    int accessorCount = 0, textureCount = 0, materialCount = 0; // images and textures share an index

    auto addVec3 = [&](const std::vector<glm::vec3>& v, bool withMinMax) {
        size_t off = bin.append(v.data(), v.size() * sizeof(glm::vec3));
        views.push_back({off, v.size() * sizeof(glm::vec3), 34962});
        accessors << (accessorCount ? "," : "") << "{\"bufferView\":" << views.size() - 1
                  << ",\"componentType\":5126,\"count\":" << v.size() << ",\"type\":\"VEC3\"";
        if (withMinMax) {
            glm::vec3 lo, hi;
            minMax(v, lo, hi);
            accessors << ",\"min\":" << vec3Json(lo) << ",\"max\":" << vec3Json(hi);
        }
        accessors << "}";
        return accessorCount++;
    };
    auto addVec2 = [&](const std::vector<glm::vec2>& v) {
        size_t off = bin.append(v.data(), v.size() * sizeof(glm::vec2));
        views.push_back({off, v.size() * sizeof(glm::vec2), 34962});
        accessors << (accessorCount ? "," : "") << "{\"bufferView\":" << views.size() - 1
                  << ",\"componentType\":5126,\"count\":" << v.size() << ",\"type\":\"VEC2\"}";
        return accessorCount++;
    };
    auto addScalars = [&](const std::vector<float>& v, bool withMinMax) { // animation data (no buffer target)
        size_t off = bin.append(v.data(), v.size() * sizeof(float));
        views.push_back({off, v.size() * sizeof(float), 0});
        accessors << (accessorCount ? "," : "") << "{\"bufferView\":" << views.size() - 1
                  << ",\"componentType\":5126,\"count\":" << v.size() << ",\"type\":\"SCALAR\"";
        if (withMinMax && !v.empty()) {
            std::ostringstream mm;
            mm.precision(9);
            mm << ",\"min\":[" << *std::min_element(v.begin(), v.end()) << "],\"max\":["
               << *std::max_element(v.begin(), v.end()) << "]";
            accessors << mm.str();
        }
        accessors << "}";
        return accessorCount++;
    };

    // One node and one mesh per object, each with its own morph targets.
    for (size_t oi = 0; oi < objects.size(); ++oi) {
        const Mesh& mesh = *objects[oi].mesh;
        const std::string& name = objects[oi].name;
        // Base shape and its normals.
        Mesh baseMesh;
        baseMesh.positions = mesh.basePositions();
        baseMesh.triangles = mesh.triangles;
        baseMesh.rebuildTopology();
        baseMesh.recomputeAllNormals();

        // glTF UVs are per vertex: a vertex is split where its corners have different UVs (seams).
        // `source[i]` is the mesh vertex of glTF vertex i; meshes without UVs are written as they are.
        const bool uvs = hasCornerUvs(mesh);
        std::vector<uint32_t> source;
        std::vector<glm::vec2> texcoords;
        std::vector<glm::uvec3> triangles;
        if (uvs) {
            std::vector<uint32_t> head(mesh.vertexCount(), ~0u), next; // glTF vertices of each mesh vertex
            triangles.resize(mesh.triangleCount());
            for (size_t t = 0; t < mesh.triangleCount(); ++t)
                for (int c = 0; c < 3; ++c) {
                    const uint32_t v = mesh.triangles[t][c];
                    const glm::vec2 uv(mesh.uvs[t * 3 + size_t(c)].x, 1.0f - mesh.uvs[t * 3 + size_t(c)].y); // glTF: V down
                    uint32_t i = head[v];
                    while (i != ~0u && texcoords[i] != uv) i = next[i];
                    if (i == ~0u) {
                        i = uint32_t(source.size());
                        source.push_back(v);
                        texcoords.push_back(uv);
                        next.push_back(head[v]);
                        head[v] = i;
                    }
                    triangles[t][c] = i;
                }
        }
        auto split = [&](const std::vector<glm::vec3>& v) {
            std::vector<glm::vec3> out;
            out.reserve(source.size());
            for (uint32_t i : source) out.push_back(v[i]);
            return out;
        };

        int posAcc = addVec3(uvs ? split(baseMesh.positions) : baseMesh.positions, true);
        int nrmAcc = addVec3(uvs ? split(baseMesh.normals) : baseMesh.normals, false);
        const bool colors = mesh.hasColors() && mesh.colors.size() == mesh.vertexCount();
        int colorAcc = colors ? addVec3(uvs ? split(mesh.colors) : mesh.colors, false) : -1; // polypaint as COLOR_0 (linear)
        int uvAcc = uvs ? addVec2(texcoords) : -1;

        const std::vector<glm::uvec3>& tris = uvs ? triangles : mesh.triangles;
        size_t idxOff = bin.append(tris.data(), tris.size() * sizeof(glm::uvec3));
        views.push_back({idxOff, tris.size() * sizeof(glm::uvec3), 34963});
        accessors << ",{\"bufferView\":" << views.size() - 1 << ",\"componentType\":5125,\"count\":"
                  << tris.size() * 3 << ",\"type\":\"SCALAR\"}";
        int idxAcc = accessorCount++;

        std::vector<int> targetAccs;
        for (const auto& l : mesh.layers) targetAccs.push_back(addVec3(uvs ? split(l.delta) : l.delta, true));

        // The texture (core/Texture.h) embedded as PNGs: one image and texture per map. With material channels
        // (core/TextureChannels.h) the material gets the glTF metal-rough slots: occlusion / roughness / metallic
        // packed in one image, the normal map, emission, and opacity in the base colour's alpha.
        int material = -1;
        std::vector<uint8_t> png;
        auto addImage = [&](const std::vector<uint8_t>& bytes, const std::string& suffix) {
            const size_t off = bin.append(bytes.data(), bytes.size());
            views.push_back({off, bytes.size(), 0});
            const int index = textureCount++;
            images << (index ? "," : "") << "{\"bufferView\":" << views.size() - 1 << ",\"mimeType\":\"image/png\",\"name\":\""
                   << jsonEscape(name) << "_" << suffix << "\"}";
            return index;
        };
        if (encodeTexturePng(mesh, png)) {
            const int color = addImage(png, "color");
            std::vector<PbrPng> pbr;
            if (!encodePbrPngs(mesh, true, std::string(), pbr)) {
                error = "Cannot encode the material maps of " + name;
                return false;
            }
            material = materialCount++;
            materials << (material ? "," : "") << "{\"name\":\"" << jsonEscape(name) << "\",\"pbrMetallicRoughness\":{"
                      << "\"baseColorTexture\":{\"index\":" << color << "}";
            const PbrPng* orm = findPbrPng(pbr, "orm");
            int ormTex = -1;
            if (orm) {
                ormTex = addImage(orm->png, "occlusionRoughnessMetallic");
                materials << ",\"metallicRoughnessTexture\":{\"index\":" << ormTex << "},\"metallicFactor\":1,\"roughnessFactor\":1}";
            } else {
                materials << ",\"metallicFactor\":0,\"roughnessFactor\":0.65}";
            }
            if (orm && channelEnabled(*mesh.texture, Channel::AO)) materials << ",\"occlusionTexture\":{\"index\":" << ormTex << "}";
            if (const PbrPng* n = findPbrPng(pbr, "normal")) materials << ",\"normalTexture\":{\"index\":" << addImage(n->png, "normal") << "}";
            if (const PbrPng* e = findPbrPng(pbr, "emissive"))
                materials << ",\"emissiveTexture\":{\"index\":" << addImage(e->png, "emissive") << "},\"emissiveFactor\":[1,1,1]";
            if (hasPbrTexture(mesh) && channelEnabled(*mesh.texture, Channel::Opacity)) materials << ",\"alphaMode\":\"BLEND\"";
            materials << "}";
        }

        nodes << (oi ? "," : "") << "{\"mesh\":" << oi << ",\"name\":\"" << jsonEscape(name) << "\"}";
        std::ostringstream& json = meshes; // this object's entry in "meshes"
        json << (oi ? "," : "") << "{\"name\":\"" << jsonEscape(name) << "\",\"primitives\":[{\"attributes\":{\"POSITION\":"
             << posAcc << ",\"NORMAL\":" << nrmAcc;
        if (colors) json << ",\"COLOR_0\":" << colorAcc;
        if (uvs) json << ",\"TEXCOORD_0\":" << uvAcc;
        json << "},\"indices\":" << idxAcc << ",\"mode\":4";
        if (material >= 0) json << ",\"material\":" << material;
        if (!mesh.layers.empty()) {
            json << ",\"targets\":[";
            for (size_t i = 0; i < targetAccs.size(); ++i) json << (i ? "," : "") << "{\"POSITION\":" << targetAccs[i] << "}";
            json << "]";
        }
        json << "}]";
        if (!mesh.layers.empty()) {
            json << ",\"weights\":[";
            for (size_t i = 0; i < mesh.layers.size(); ++i) json << (i ? "," : "") << mesh.layers[i].weight;
            json << "],\"extras\":{\"targetNames\":[";
            for (size_t i = 0; i < mesh.layers.size(); ++i)
                json << (i ? "," : "") << "\"" << jsonEscape(mesh.layers[i].name) << "\"";
            json << "]}";
        }
        json << "}";
    }

    // The animation: one sampler per object with layers the animation names, keying all of its targets.
    std::ostringstream samplers, channels;
    int animated = 0;
    if (animation && !animation->empty()) {
        int input = -1;
        for (size_t oi = 0; oi < objects.size(); ++oi) {
            const Mesh& mesh = *objects[oi].mesh;
            std::vector<int> channelOf;
            bool any = false;
            for (const auto& l : mesh.layers) {
                channelOf.push_back(animation->channel(l.name));
                any |= channelOf.back() >= 0;
            }
            if (!any) continue;
            if (input < 0) input = addScalars(animation->times, true);
            std::vector<float> out;
            out.reserve(animation->times.size() * channelOf.size());
            for (size_t k = 0; k < animation->times.size(); ++k)
                for (int c : channelOf) out.push_back(c >= 0 ? animation->value(k, size_t(c)) : 0.0f);
            const int output = addScalars(out, false);
            samplers << (animated ? "," : "") << "{\"input\":" << input << ",\"output\":" << output
                     << ",\"interpolation\":\"LINEAR\"}";
            channels << (animated ? "," : "") << "{\"sampler\":" << animated << ",\"target\":{\"node\":" << oi
                     << ",\"path\":\"weights\"}}";
            ++animated;
        }
    }

    std::ostringstream json;
    json << "{\"asset\":{\"version\":\"2.0\",\"generator\":\"SculptForge\"},\"scene\":0,\"scenes\":[{";
    if (objects.size() > 1) json << "\"name\":\"" << jsonEscape(sceneName) << "\",";
    json << "\"nodes\":[";
    for (size_t i = 0; i < objects.size(); ++i) json << (i ? "," : "") << i;
    json << "]}],\"nodes\":[" << nodes.str() << "],\"meshes\":[" << meshes.str() << "],\"accessors\":["
         << accessors.str() << "],\"bufferViews\":[";
    for (size_t i = 0; i < views.size(); ++i) {
        json << (i ? "," : "") << "{\"buffer\":0,\"byteOffset\":" << views[i].offset << ",\"byteLength\":" << views[i].length;
        if (views[i].target) json << ",\"target\":" << views[i].target;
        json << "}";
    }
    json << "],\"buffers\":[{\"byteLength\":" << bin.data.size() << "}]";
    if (textureCount) {
        json << ",\"images\":[" << images.str() << "],\"samplers\":[{\"magFilter\":9729,\"minFilter\":9987,"
             << "\"wrapS\":10497,\"wrapT\":10497}],\"textures\":[";
        for (int i = 0; i < textureCount; ++i) json << (i ? "," : "") << "{\"sampler\":0,\"source\":" << i << "}";
        (void)materialCount;
        json << "],\"materials\":[" << materials.str() << "]";
    }
    if (animated)
        json << ",\"animations\":[{\"name\":\"" << jsonEscape(animation->name) << "\",\"samplers\":[" << samplers.str()
             << "],\"channels\":[" << channels.str() << "]}]";
    json << "}";

    return writeGlbFile(path, json.str(), bin.data, error);
}

bool saveSplatGlb(const std::string& path, const SplatCloud& c, std::string& error) {
    if (c.empty()) {
        error = "There are no splats to export";
        return false;
    }
    if (std::string v = c.validate(); !v.empty()) {
        error = v;
        return false;
    }
    BinWriter bin;
    std::ostringstream accessors, views;
    int count = 0;
    auto add = [&](const std::vector<float>& data, const char* type, bool withMinMax, const std::vector<glm::vec3>* v3) {
        const size_t off = bin.append(data.data(), data.size() * sizeof(float));
        views << (count ? "," : "") << "{\"buffer\":0,\"byteOffset\":" << off << ",\"byteLength\":"
              << data.size() * sizeof(float) << ",\"target\":34962}";
        accessors << (count ? "," : "") << "{\"bufferView\":" << count << ",\"componentType\":5126,\"count\":" << c.size()
                  << ",\"type\":\"" << type << "\"";
        if (withMinMax && v3) {
            glm::vec3 lo, hi;
            minMax(*v3, lo, hi);
            accessors << ",\"min\":" << vec3Json(lo) << ",\"max\":" << vec3Json(hi);
        }
        accessors << "}";
        return count++;
    };
    auto flat3 = [](const std::vector<glm::vec3>& v) {
        std::vector<float> f;
        f.reserve(v.size() * 3);
        for (const auto& x : v) f.insert(f.end(), {x.x, x.y, x.z});
        return f;
    };
    const size_t K = size_t(c.shCount());
    std::vector<float> rot, color, sh0;
    for (size_t i = 0; i < c.size(); ++i) {
        const glm::quat& q = c.rotations[i];
        rot.insert(rot.end(), {q.x, q.y, q.z, q.w});
        const glm::vec3 rgb = glm::clamp(c.color(i), glm::vec3(0.0f), glm::vec3(1.0f));
        color.insert(color.end(), {rgb.x, rgb.y, rgb.z, c.opacities[i]});
        sh0.insert(sh0.end(), {c.sh[i * K].x, c.sh[i * K].y, c.sh[i * K].z});
    }
    std::ostringstream attrs;
    const std::string prefix = "\"KHR_gaussian_splatting:";
    attrs << "\"POSITION\":" << add(flat3(c.positions), "VEC3", true, &c.positions);
    attrs << ",\"COLOR_0\":" << add(color, "VEC4", false, nullptr);
    attrs << "," << prefix << "ROTATION\":" << add(rot, "VEC4", false, nullptr);
    attrs << "," << prefix << "SCALE\":" << add(flat3(c.scales), "VEC3", false, nullptr);
    attrs << "," << prefix << "OPACITY\":" << add(c.opacities, "SCALAR", false, nullptr);
    attrs << "," << prefix << "SH_DEGREE_0_COEF_0\":" << add(sh0, "VEC3", false, nullptr);
    for (size_t k = 1; k < K; ++k) {
        const int d = int(std::sqrt(double(k)) + 1e-9), m = int(k) - d * d;
        std::vector<float> coef;
        coef.reserve(c.size() * 3);
        for (size_t i = 0; i < c.size(); ++i) coef.insert(coef.end(), {c.sh[i * K + k].x, c.sh[i * K + k].y, c.sh[i * K + k].z});
        attrs << "," << prefix << "SH_DEGREE_" << d << "_COEF_" << m << "\":" << add(coef, "VEC3", false, nullptr);
    }

    std::ostringstream json;
    json.precision(9);
    json << "{\"asset\":{\"version\":\"2.0\",\"generator\":\"SculptForge\"},\"extensionsUsed\":[\"KHR_gaussian_splatting\"],"
         << "\"scene\":0,\"scenes\":[{\"nodes\":[0]}],\"nodes\":[{\"mesh\":0,\"name\":\"" << jsonEscape(c.name) << "\"";
    if (c.transform != glm::mat4(1.0f)) {
        json << ",\"matrix\":[";
        for (int i = 0; i < 16; ++i) json << (i ? "," : "") << glm::value_ptr(c.transform)[i];
        json << "]";
    }
    json << "}],\"meshes\":[{\"name\":\"" << jsonEscape(c.name) << "\",\"primitives\":[{\"mode\":0,\"attributes\":{"
         << attrs.str() << "},\"extensions\":{\"KHR_gaussian_splatting\":{\"kernel\":\"ellipse\","
         << "\"colorSpace\":\"srgb_rec709_display\",\"projection\":\"perspective\",\"sortingMethod\":\"cameraDistance\"}}}]}],"
         << "\"accessors\":[" << accessors.str() << "],\"bufferViews\":[" << views.str() << "],\"buffers\":[{\"byteLength\":"
         << bin.data.size() << "}]}";
    return writeGlbFile(path, json.str(), bin.data, error);
}

namespace {

// Limits for files from outside. Real data is also bounded by the bytes actually present; these caps cover what a
// small file can make us allocate anyway (zero-filled accessors, instanced meshes, morph targets, repeated buffers).
constexpr uint64_t kMaxJsonInteger = uint64_t(1) << 53;      // doubles hold whole numbers exactly up to here
constexpr size_t kMaxZeroFilledElements = size_t(1) << 24;   // one accessor without a bufferView
constexpr size_t kMaxZeroFilledTotal = size_t(1) << 26;      // all of them together
constexpr size_t kMaxImportVertices = size_t(1) << 27;       // positions gathered over the whole file
constexpr size_t kMaxImportTriangles = size_t(1) << 28;
constexpr size_t kMaxMorphTargets = 4096;
constexpr size_t kMaxMorphDeltas = size_t(1) << 28;          // targets x vertices
constexpr size_t kMaxBuffers = 4096;
constexpr uint64_t kMaxBufferBytes = uint64_t(1) << 32;      // all buffers together

// A JSON value as a whole number in [0, max]: false when it is missing, not a number, negative, fractional,
// non-finite or larger than max (casting such doubles to an integer is undefined behaviour).
bool wholeNumber(const Json& v, uint64_t max, size_t& out) {
    if (!v.isNumber()) return false;
    const double d = v.asNumber();
    if (!(d >= 0.0) || d > double(std::min<uint64_t>(max, kMaxJsonInteger)) || d != std::floor(d)) return false;
    out = size_t(d);
    return true;
}

// Like wholeNumber, but a missing value is `fallback` (an invalid present value is still an error).
bool optionalWhole(const Json& v, uint64_t max, size_t fallback, size_t& out) {
    if (v.isNull()) {
        out = fallback;
        return true;
    }
    return wholeNumber(v, max, out);
}

// An object index (accessor, node, mesh...) or -1 when the value is not a valid one.
int jsonIndex(const Json& v) {
    size_t i;
    return wholeNumber(v, uint64_t(std::numeric_limits<int>::max()), i) ? int(i) : -1;
}

// A small enum-like integer (componentType, mode) or -1.
int jsonEnum(const Json& v) { return jsonIndex(v); }

// Doubles outside float range are clamped (the plain conversion is undefined); inf and NaN pass through.
float toFloat(double d) {
    if (std::isfinite(d)) d = std::clamp(d, -double(std::numeric_limits<float>::max()), double(std::numeric_limits<float>::max()));
    return float(d);
}

std::vector<uint8_t> base64Decode(const std::string& in) {
    static const auto table = [] {
        std::array<int, 256> t;
        t.fill(-1);
        const char* chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        for (int i = 0; i < 64; ++i) t[uint8_t(chars[i])] = i;
        return t;
    }();
    std::vector<uint8_t> out;
    out.reserve(in.size() / 4 * 3 + 3);
    // Only the last 14 bits matter (8 being emitted plus up to 6 pending), so the accumulator stays bounded.
    uint32_t val = 0;
    int bits = -8;
    for (unsigned char c : in) {
        if (table[c] < 0) continue;
        val = ((val << 6) | uint32_t(table[c])) & 0xFFFFFu;
        bits += 6;
        if (bits >= 0) {
            out.push_back(uint8_t((val >> bits) & 0xff));
            bits -= 8;
        }
    }
    return out;
}

bool readFile(const std::filesystem::path& p, std::vector<uint8_t>& out) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return true;
}

// True when `count` elements of `element` bytes, `stride` apart, fit in `available` bytes (no overflow).
bool elementsFit(size_t count, size_t stride, size_t element, size_t available) {
    if (count == 0) return true;
    if (element > available) return false;
    if (stride == 0) return true;
    return count - 1 <= (available - element) / stride;
}

struct GltfReader {
    Json doc;
    std::vector<std::vector<uint8_t>> buffers;
    std::string error;
    size_t zeroFilled = 0; // elements handed out for accessors without a bufferView

    // Reads accessor `index` as floats (VEC3) or indices (SCALAR); returns false on unsupported layouts.
    bool readVec3(int index, std::vector<glm::vec3>& out) {
        const Json* acc;
        size_t count;
        if (!accessor(index, acc, count)) return false;
        if ((*acc)["type"].asString() != "VEC3" || jsonEnum((*acc)["componentType"]) != 5126) {
            error = "Only float VEC3 positions are supported";
            return false;
        }
        if ((*acc)["bufferView"].isNull()) { // all zeros (sparse not supported)
            if (!zeroFill(count)) return false;
            out.assign(count, glm::vec3(0.0f));
            return true;
        }
        const uint8_t* base;
        size_t stride;
        if (!view(*acc, count, 12, base, stride)) return false;
        out.assign(count, glm::vec3(0.0f));
        for (size_t i = 0; i < count; ++i) std::memcpy(&out[i], base + i * stride, 12);
        return true;
    }

    // Reads `components` values per element as floats, decoding normalized integers.
    bool readFloats(int index, int components, std::vector<float>& out) {
        const Json* acc;
        size_t count;
        if (!accessor(index, acc, count)) return false;
        static const std::map<std::string, int> typeComponents = {{"SCALAR", 1}, {"VEC2", 2}, {"VEC3", 3}, {"VEC4", 4}};
        auto tc = typeComponents.find((*acc)["type"].asString());
        if (tc == typeComponents.end() || tc->second != components) return fail("Unexpected accessor type");
        const int type = jsonEnum((*acc)["componentType"]);
        const size_t size = type == 5126 ? 4 : (type == 5122 || type == 5123) ? 2 : (type == 5120 || type == 5121) ? 1 : 0;
        if (!size) return fail("Unsupported accessor component type");
        const bool normalized = (*acc)["normalized"].asBool(false);
        if ((*acc)["bufferView"].isNull()) {
            if (!zeroFill(count)) return false;
            out.assign(count * size_t(components), 0.0f);
            return true;
        }
        const uint8_t* base;
        size_t stride;
        const size_t element = size * size_t(components);
        if (!view(*acc, count, element, base, stride)) return false;
        out.assign(count * size_t(components), 0.0f);
        for (size_t i = 0; i < count; ++i)
            for (int c = 0; c < components; ++c) {
                const uint8_t* p = base + i * stride + size_t(c) * size;
                float v = 0.0f;
                switch (type) {
                case 5126: std::memcpy(&v, p, 4); break;
                case 5120: v = normalized ? std::max(float(int8_t(p[0])) / 127.0f, -1.0f) : float(int8_t(p[0])); break;
                case 5121: v = normalized ? float(p[0]) / 255.0f : float(p[0]); break;
                case 5122: {
                    int16_t x;
                    std::memcpy(&x, p, 2);
                    v = normalized ? std::max(float(x) / 32767.0f, -1.0f) : float(x);
                    break;
                }
                default: {
                    uint16_t x;
                    std::memcpy(&x, p, 2);
                    v = normalized ? float(x) / 65535.0f : float(x);
                }
                }
                out[i * size_t(components) + size_t(c)] = v;
            }
        return true;
    }

    bool readIndices(int index, std::vector<uint32_t>& out) {
        const Json* acc;
        size_t count;
        if (!accessor(index, acc, count)) return false;
        const int type = jsonEnum((*acc)["componentType"]);
        const size_t size = type == 5125 ? 4 : type == 5123 ? 2 : type == 5121 ? 1 : 0;
        if (!size) return fail("Unsupported index type");
        if ((*acc)["bufferView"].isNull()) { // glTF 2.0: an accessor without a bufferView is all zeros
            if (!zeroFill(count)) return false;
            out.assign(count, 0);
            return true;
        }
        const uint8_t* base;
        size_t stride;
        if (!view(*acc, count, size, base, stride)) return false;
        out.resize(count);
        for (size_t i = 0; i < count; ++i) {
            const uint8_t* p = base + i * stride;
            out[i] = size == 4 ? uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24
                     : size == 2 ? uint32_t(p[0]) | uint32_t(p[1]) << 8
                                 : uint32_t(p[0]);
        }
        return true;
    }

    // Looks up accessor `index` and its element count; nothing is allocated here.
    bool accessor(int index, const Json*& acc, size_t& count) {
        if (index < 0 || size_t(index) >= doc["accessors"].size()) return fail("Accessor index out of range");
        acc = &doc["accessors"][size_t(index)];
        if (!acc->isObject()) return fail("Accessor is not an object");
        if (!wholeNumber((*acc)["count"], kMaxJsonInteger, count)) return fail("Invalid accessor count");
        return true;
    }

    // Accounts for an accessor without a bufferView (zero-filled), refusing absurd sizes before allocating.
    bool zeroFill(size_t count) {
        if (count > kMaxZeroFilledElements || count > kMaxZeroFilledTotal - zeroFilled)
            return fail("Accessor without data is too large");
        zeroFilled += count;
        return true;
    }

    // Resolves the accessor's bytes and checks all `count` elements lie inside its buffer view and buffer.
    bool view(const Json& acc, size_t count, size_t elementSize, const uint8_t*& base, size_t& stride) {
        const int bvIndex = jsonIndex(acc["bufferView"]);
        if (bvIndex < 0 || size_t(bvIndex) >= doc["bufferViews"].size()) return fail("Buffer view index out of range");
        const Json& bv = doc["bufferViews"][size_t(bvIndex)];
        size_t buf, viewOffset, viewLength, accOffset;
        if (!wholeNumber(bv["buffer"], kMaxJsonInteger, buf) || buf >= buffers.size()) return fail("Missing buffer");
        if (!optionalWhole(bv["byteOffset"], kMaxJsonInteger, 0, viewOffset) ||
            !wholeNumber(bv["byteLength"], kMaxJsonInteger, viewLength) ||
            !optionalWhole(acc["byteOffset"], kMaxJsonInteger, 0, accOffset))
            return fail("Invalid buffer view offset or length");
        const size_t bufSize = buffers[buf].size();
        if (viewLength > bufSize || viewOffset > bufSize - viewLength || accOffset > viewLength)
            return fail("Buffer view out of range");
        if (bv["byteStride"].isNull()) {
            stride = elementSize;
        } else if (!wholeNumber(bv["byteStride"], 252, stride) || stride < elementSize) {
            return fail("Invalid buffer view stride");
        }
        if (!elementsFit(count, stride, elementSize, viewLength - accOffset)) return fail("Accessor out of range");
        base = buffers[buf].data() + viewOffset + accOffset;
        return true;
    }

    bool fail(const std::string& msg) {
        if (error.empty()) error = msg;
        return false;
    }
};

glm::mat4 nodeMatrix(const Json& node) {
    if (node["matrix"].size() == 16) {
        glm::mat4 m;
        for (int i = 0; i < 16; ++i) glm::value_ptr(m)[i] = toFloat(node["matrix"][size_t(i)].asNumber());
        return m;
    }
    auto f = [](const Json& a, size_t i) { return toFloat(a[i].asNumber()); };
    glm::mat4 m(1.0f);
    const Json& t = node["translation"];
    if (t.size() == 3) m = glm::translate(m, glm::vec3(f(t, 0), f(t, 1), f(t, 2)));
    const Json& r = node["rotation"];
    if (r.size() == 4) m *= glm::mat4_cast(glm::quat(f(r, 3), f(r, 0), f(r, 1), f(r, 2)));
    const Json& s = node["scale"];
    if (s.size() == 3) m = glm::scale(m, glm::vec3(f(s, 0), f(s, 1), f(s, 2)));
    return m;
}

} // namespace

namespace {

// Triangles gathered for one object (a node with a mesh, or the whole file when merging).
struct GltfObject {
    std::string name;
    Mesh m;
    std::vector<std::vector<glm::vec3>> targetDeltas;
    std::vector<std::string> targetNames;
    std::vector<float> targetWeights;
    bool targetsConsistent = true;
    bool uvs = false; // some primitive had TEXCOORD_0
};

// Splits a GLB container into its JSON text and BIN chunk; false with `error` on a malformed container.
bool parseGlb(const std::vector<uint8_t>& file, std::string& jsonText, std::vector<uint8_t>& bin, bool& hasBin,
              std::string& error) {
    uint32_t version, total;
    std::memcpy(&version, file.data() + 4, 4);
    std::memcpy(&total, file.data() + 8, 4);
    if (version != 2) {
        error = "Unsupported GLB version " + std::to_string(version);
        return false;
    }
    // The header's length may not exceed the file; trailing bytes after it are ignored.
    if (total < 12 || total > file.size()) {
        error = "Truncated GLB file";
        return false;
    }
    const size_t end = total;
    size_t offset = 12;
    bool hasJson = false;
    while (end - offset >= 8) {
        uint32_t len, type;
        std::memcpy(&len, file.data() + offset, 4);
        std::memcpy(&type, file.data() + offset + 4, 4);
        offset += 8;
        if (len > end - offset) {
            error = "Truncated GLB file";
            return false;
        }
        if (type == 0x4E4F534A && !hasJson) {
            jsonText.assign(reinterpret_cast<const char*>(file.data() + offset), len);
            hasJson = true;
        } else if (type == 0x004E4942 && !hasBin) {
            bin.assign(file.begin() + std::ptrdiff_t(offset), file.begin() + std::ptrdiff_t(offset + len));
            hasBin = true;
        }
        // Chunks are 4-byte aligned; a last chunk may end without its padding.
        offset += std::min<size_t>((size_t(len) + 3) & ~size_t(3), end - offset);
    }
    if (!hasJson) {
        error = "GLB file has no JSON chunk";
        return false;
    }
    return true;
}

bool readGltf(const std::string& path, bool split, std::vector<MeshObject>& out, std::string& error,
              SplatCloud* splatsOut) {
    const std::filesystem::path fsPath(std::u8string(path.begin(), path.end()));
    std::vector<uint8_t> file;
    if (!readFile(fsPath, file)) {
        error = "Cannot open " + path;
        return false;
    }

    GltfReader r;
    std::string jsonText;
    std::vector<uint8_t> glbBin;
    bool hasGlbBin = false;
    if (file.size() >= 12 && std::memcmp(file.data(), "glTF", 4) == 0) {
        if (!parseGlb(file, jsonText, glbBin, hasGlbBin, error)) return false;
    } else {
        jsonText.assign(file.begin(), file.end());
    }
    if (!Json::parse(jsonText, r.doc, error)) {
        error = "Invalid glTF JSON: " + error;
        return false;
    }

    if (r.doc["buffers"].size() > kMaxBuffers) {
        error = "Too many buffers";
        return false;
    }
    uint64_t bufferBytes = 0;
    for (size_t i = 0; i < r.doc["buffers"].size(); ++i) {
        const std::string& uri = r.doc["buffers"][i]["uri"].asString();
        std::vector<uint8_t> data;
        if (uri.empty() && i == 0 && hasGlbBin) {
            data = std::move(glbBin);
        } else if (uri.rfind("data:", 0) == 0) {
            size_t comma = uri.find(',');
            if (comma == std::string::npos) {
                error = "Bad data URI";
                return false;
            }
            data = base64Decode(uri.substr(comma + 1));
        } else if (!uri.empty()) {
            const std::filesystem::path rel(std::u8string(uri.begin(), uri.end()));
            if (uri.find("..") != std::string::npos || rel.is_absolute() || rel.has_root_name() || rel.has_root_directory()) {
                error = "Refusing buffer path outside the model's folder: " + uri;
                return false;
            }
            const std::filesystem::path full = fsPath.parent_path() / rel;
            std::error_code ec;
            if (!std::filesystem::is_regular_file(full, ec)) {
                error = "Missing buffer file: " + uri;
                return false;
            }
            const uintmax_t size = std::filesystem::file_size(full, ec);
            if (ec || size > kMaxBufferBytes - bufferBytes) {
                error = "Buffers are too large: " + uri;
                return false;
            }
            if (!readFile(full, data)) {
                error = "Missing buffer file: " + uri;
                return false;
            }
        }
        if (data.size() > kMaxBufferBytes - bufferBytes) {
            error = "Buffers are too large";
            return false;
        }
        bufferBytes += data.size();
        r.buffers.push_back(std::move(data));
    }

    std::vector<GltfObject> objects(1);
    size_t totalVertices = 0, totalTriangles = 0, totalDeltas = 0;

    SplatCloud splats;
    auto readSplats = [&](const Json& prim, const glm::mat4& world) -> bool {
        const Json& at = prim["attributes"];
        const char* prefix = "KHR_gaussian_splatting:";
        auto attr = [&](const std::string& n) { return jsonIndex(at[n]); };
        const int pos = attr("POSITION"), rot = attr(std::string(prefix) + "ROTATION"),
                  scl = attr(std::string(prefix) + "SCALE"), opa = attr(std::string(prefix) + "OPACITY"),
                  dc = attr(std::string(prefix) + "SH_DEGREE_0_COEF_0");
        if (pos < 0 || rot < 0 || scl < 0 || opa < 0 || dc < 0) return r.fail("Incomplete KHR_gaussian_splatting primitive");
        std::vector<glm::vec3> p;
        std::vector<float> q, sc, op, c0;
        if (!r.readVec3(pos, p) || !r.readFloats(rot, 4, q) || !r.readFloats(scl, 3, sc) || !r.readFloats(opa, 1, op) ||
            !r.readFloats(dc, 3, c0))
            return false;
        const size_t n = p.size();
        if (q.size() != n * 4 || sc.size() != n * 3 || op.size() != n || c0.size() != n * 3)
            return r.fail("KHR_gaussian_splatting attributes have different counts");
        int degree = 0;
        for (int d = 1; d <= 3; ++d) {
            bool complete = true;
            for (int m = 0; m < 2 * d + 1; ++m)
                complete &= attr(std::string(prefix) + "SH_DEGREE_" + std::to_string(d) + "_COEF_" + std::to_string(m)) >= 0;
            if (!complete) break;
            degree = d;
        }
        splats.resize(n, degree);
        const size_t K = size_t(splats.shCount());
        for (size_t i = 0; i < n; ++i) {
            splats.positions[i] = p[i];
            glm::quat qq(q[i * 4 + 3], q[i * 4], q[i * 4 + 1], q[i * 4 + 2]); // glTF stores x, y, z, w
            const float len = glm::length(qq);
            splats.rotations[i] = len > 0.0f ? qq / len : glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
            splats.scales[i] = glm::vec3(sc[i * 3], sc[i * 3 + 1], sc[i * 3 + 2]);
            splats.opacities[i] = op[i];
            splats.sh[i * K] = glm::vec3(c0[i * 3], c0[i * 3 + 1], c0[i * 3 + 2]);
        }
        for (size_t k = 1; k < K; ++k) {
            const int d = int(std::sqrt(double(k)) + 1e-9), m = int(k) - d * d;
            std::vector<float> coef;
            if (!r.readFloats(attr(std::string(prefix) + "SH_DEGREE_" + std::to_string(d) + "_COEF_" + std::to_string(m)), 3, coef))
                return false;
            if (coef.size() != n * 3) return r.fail("KHR_gaussian_splatting SH count mismatch");
            for (size_t i = 0; i < n; ++i) splats.sh[i * K + k] = glm::vec3(coef[i * 3], coef[i * 3 + 1], coef[i * 3 + 2]);
        }
        splats.transform = world;
        return true;
    };

    // glTF nodes form trees, so each node is visited once: shared children or cycles can't multiply the work.
    std::vector<char> visited(r.doc["nodes"].size(), 0);
    std::function<bool(int, const glm::mat4&, int)> visit = [&](int nodeIndex, const glm::mat4& parent, int depth) {
        if (depth > 64) return r.fail("Node hierarchy too deep");
        if (nodeIndex < 0 || size_t(nodeIndex) >= visited.size()) return r.fail("Node index out of range");
        if (visited[size_t(nodeIndex)]) return true;
        visited[size_t(nodeIndex)] = 1;
        const Json& node = r.doc["nodes"][size_t(nodeIndex)];
        const glm::mat4 world = parent * nodeMatrix(node);
        const glm::mat3 linear(world);
        if (!node["mesh"].isNull()) {
            const int meshIndex = jsonIndex(node["mesh"]);
            if (meshIndex < 0 || size_t(meshIndex) >= r.doc["meshes"].size()) return r.fail("Mesh index out of range");
            const Json& mesh = r.doc["meshes"][size_t(meshIndex)];
            // Splitting: every node with a mesh is its own object (its primitives are material slots).
            if (split && (objects.size() > 1 || !objects[0].m.triangles.empty() || !objects[0].name.empty()))
                objects.emplace_back();
            GltfObject& o = objects.back();
            if (split && o.name.empty()) {
                o.name = node["name"].isString() ? node["name"].asString() : mesh["name"].asString();
                if (o.name.empty()) o.name = "Mesh " + std::to_string(objects.size());
            }
            Mesh& m = o.m;
            auto& targetDeltas = o.targetDeltas;
            auto& targetNames = o.targetNames;
            auto& targetWeights = o.targetWeights;
            bool& targetsConsistent = o.targetsConsistent;
            for (const Json& prim : mesh["primitives"].items()) {
                const Json& gs = prim["extensions"]["KHR_gaussian_splatting"];
                if (gs.isObject()) {
                    if (splatsOut && splats.empty() && !readSplats(prim, world)) return false;
                    continue;
                }
                if (!prim["mode"].isNull() && jsonEnum(prim["mode"]) != 4) continue; // triangles only
                if (prim["attributes"]["POSITION"].isNull()) continue;
                std::vector<glm::vec3> pos;
                if (!r.readVec3(jsonIndex(prim["attributes"]["POSITION"]), pos)) return false;
                std::vector<uint32_t> idx;
                if (!prim["indices"].isNull()) {
                    if (!r.readIndices(jsonIndex(prim["indices"]), idx)) return false;
                } else {
                    idx.resize(pos.size());
                    for (uint32_t i = 0; i < idx.size(); ++i) idx[i] = i;
                }
                if (pos.size() > kMaxImportVertices - totalVertices || idx.size() / 3 > kMaxImportTriangles - totalTriangles)
                    return r.fail("Model is too large to import");
                totalVertices += pos.size();
                totalTriangles += idx.size() / 3;
                const uint32_t baseIndex = uint32_t(m.positions.size());
                for (const auto& p : pos) m.positions.push_back(glm::vec3(world * glm::vec4(p, 1.0f)));
                // Vertex colours (COLOR_0 is linear RGB or RGBA); primitives without them are white.
                const Json& colorAcc = prim["attributes"]["COLOR_0"];
                if (!colorAcc.isNull()) {
                    const int ci = jsonIndex(colorAcc);
                    const int comps = r.doc["accessors"][size_t(std::max(ci, 0))]["type"].asString() == "VEC4" ? 4 : 3;
                    std::vector<float> c;
                    if (!r.readFloats(ci, comps, c)) return false;
                    m.colors.resize(baseIndex, glm::vec3(1.0f));
                    for (size_t i = 0; i < pos.size(); ++i)
                        m.colors.push_back(i * size_t(comps) + 2 < c.size()
                                               ? glm::vec3(c[i * size_t(comps)], c[i * size_t(comps) + 1], c[i * size_t(comps) + 2])
                                               : glm::vec3(1.0f));
                } else if (!m.colors.empty()) {
                    m.colors.resize(m.positions.size(), glm::vec3(1.0f));
                }
                // TEXCOORD_0 (glTF V runs down); Mesh::uvs holds one per corner either way until the end.
                std::vector<float> uv;
                const Json& uvAcc = prim["attributes"]["TEXCOORD_0"];
                if (!uvAcc.isNull()) {
                    if (!r.readFloats(jsonIndex(uvAcc), 2, uv)) return false;
                    o.uvs = true;
                }
                for (size_t t = 0; t + 2 < idx.size(); t += 3) {
                    if (idx[t] >= pos.size() || idx[t + 1] >= pos.size() || idx[t + 2] >= pos.size())
                        return r.fail("Index out of range");
                    m.triangles.push_back({baseIndex + idx[t], baseIndex + idx[t + 1], baseIndex + idx[t + 2]});
                    for (size_t c = 0; c < 3; ++c) {
                        const size_t i = idx[t + c];
                        m.uvs.push_back(i * 2 + 1 < uv.size() ? glm::vec2(uv[i * 2], 1.0f - uv[i * 2 + 1]) : glm::vec2(0.0f));
                    }
                }

                // Morph targets -> sculpt layers (only when every primitive has the same set).
                const Json& targets = prim["targets"];
                if (targetDeltas.empty() && baseIndex == 0) {
                    if (targets.size() > kMaxMorphTargets) return r.fail("Too many morph targets");
                    targetDeltas.resize(targets.size());
                    for (size_t k = 0; k < targets.size(); ++k) {
                        const Json& names = mesh["extras"]["targetNames"];
                        targetNames.push_back(names[k].isString() ? names[k].asString() : "Shape " + std::to_string(k + 1));
                        targetWeights.push_back(toFloat(mesh["weights"][k].asNumber(0.0)));
                    }
                } else if (targets.size() != targetDeltas.size()) {
                    targetsConsistent = false;
                }
                if (!targetDeltas.empty() &&
                    (pos.size() > kMaxMorphDeltas / targetDeltas.size() ||
                     pos.size() * targetDeltas.size() > kMaxMorphDeltas - totalDeltas))
                    return r.fail("Morph targets are too large to import");
                totalDeltas += pos.size() * targetDeltas.size();
                for (size_t k = 0; k < targetDeltas.size(); ++k) {
                    std::vector<glm::vec3> d(pos.size(), glm::vec3(0.0f));
                    if (k < targets.size() && !targets[k]["POSITION"].isNull() &&
                        !r.readVec3(jsonIndex(targets[k]["POSITION"]), d))
                        return false;
                    d.resize(pos.size(), glm::vec3(0.0f));
                    for (const auto& v : d) targetDeltas[k].push_back(linear * v);
                }
            }
        }
        for (const Json& child : node["children"].items())
            if (!visit(jsonIndex(child), world, depth + 1)) return false;
        return true;
    };

    const Json& scenes = r.doc["scenes"];
    if (scenes.size() > 0) {
        size_t sceneIndex = 0;
        if (!optionalWhole(r.doc["scene"], kMaxJsonInteger, 0, sceneIndex) || sceneIndex >= scenes.size()) {
            error = "Scene index out of range";
            return false;
        }
        const Json& scene = scenes[sceneIndex];
        for (const Json& n : scene["nodes"].items())
            if (!visit(jsonIndex(n), glm::mat4(1.0f), 0)) {
                error = r.error;
                return false;
            }
    } else {
        // No scene: every root node (one that is nobody's child), so children keep their parents' transforms.
        std::vector<char> isChild(r.doc["nodes"].size(), 0);
        for (const Json& node : r.doc["nodes"].items())
            for (const Json& child : node["children"].items()) {
                const int c = jsonIndex(child);
                if (c >= 0 && size_t(c) < isChild.size()) isChild[size_t(c)] = 1;
            }
        for (size_t i = 0; i < r.doc["nodes"].size(); ++i)
            if (!isChild[i] && !visit(int(i), glm::mat4(1.0f), 0)) {
                error = r.error;
                return false;
            }
    }
    if (splatsOut && !splats.empty()) {
        auto stem = std::filesystem::path(fsPath).stem().u8string();
        splats.name = std::string(stem.begin(), stem.end());
        *splatsOut = std::move(splats);
    }
    out.clear();
    for (auto& o : objects) {
        if (o.m.triangles.empty()) continue;
        // Weld vertices split along UV/normal seams so strokes don't tear the surface.
        std::vector<SculptLayer> layers;
        if (o.targetsConsistent)
            for (size_t k = 0; k < o.targetDeltas.size(); ++k)
                layers.push_back({o.targetNames[k], std::move(o.targetDeltas[k]), o.targetWeights[k]});
        if (!o.m.colors.empty()) o.m.colors.resize(o.m.positions.size(), glm::vec3(1.0f));
        if (!o.uvs) o.m.uvs.clear();
        out.push_back({o.name, weldImported(o.m, layers)});
    }
    if (out.empty() && !(splatsOut && !splatsOut->empty())) {
        error = r.error.empty() ? "No triangle meshes found in " + path : r.error;
        return false;
    }
    return true;
}

} // namespace

bool loadGltf(const std::string& path, Mesh& out, std::string& error, SplatCloud* splatsOut) {
    std::vector<MeshObject> objects;
    if (!readGltf(path, false, objects, error, splatsOut)) return false;
    out = objects.empty() ? Mesh() : std::move(objects.front().mesh);
    return true;
}

bool loadGltf(const std::string& path, std::vector<MeshObject>& out, std::string& error, SplatCloud* splatsOut) {
    return readGltf(path, true, out, error, splatsOut);
}

} // namespace sf
