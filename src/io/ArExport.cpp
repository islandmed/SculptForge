// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "io/ArExport.h"

#include "core/AutoUv.h"
#include "core/Decimate.h"
#include "core/Texture.h"
#include "io/GltfIO.h"
#include "io/UsdIO.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <unordered_map>

namespace sf {

namespace fs = std::filesystem;

namespace {

constexpr size_t kClusterAbove = 3'000'000; // triangles: cluster vertices first (QEM on more needs gigabytes)
constexpr int kPadding = 4;                 // texels around each UV island

// One part's source of colour for a merged triangle.
struct PartColour {
    const PaintTexture* texture = nullptr; // sampled at the carried corner UVs
    bool colors = false;                   // vertex paint (per vertex, linear)
};

glm::vec3 sampleBaked(const PaintTexture& tex, glm::vec2 uv) {
    // Bilinear on the sRGB bytes (rows bottom-up, v = 0 at the bottom), clamped to the edge.
    const float x = std::clamp(uv.x, 0.0f, 1.0f) * float(tex.width) - 0.5f;
    const float y = std::clamp(uv.y, 0.0f, 1.0f) * float(tex.height) - 0.5f;
    const int x0 = std::clamp(int(std::floor(x)), 0, tex.width - 1), y0 = std::clamp(int(std::floor(y)), 0, tex.height - 1);
    const int x1 = std::min(x0 + 1, tex.width - 1), y1 = std::min(y0 + 1, tex.height - 1);
    const float fx = std::clamp(x - float(x0), 0.0f, 1.0f), fy = std::clamp(y - float(y0), 0.0f, 1.0f);
    auto at = [&](int px, int py) {
        const uint8_t* p = tex.pixels.data() + (size_t(py) * size_t(tex.width) + size_t(px)) * 4;
        return glm::vec3(p[0], p[1], p[2]) / 255.0f;
    };
    return glm::mix(glm::mix(at(x0, y0), at(x1, y0), fx), glm::mix(at(x0, y1), at(x1, y1), fx), fy);
}

// Rasterises every triangle into the texture at its UVs, colour from `colorAt(triangle, barycentrics)` (sRGB
// 0..1), then pads the islands ring by ring.
std::vector<uint8_t> bake(const std::vector<glm::uvec3>& triangles, const std::vector<glm::vec2>& uvs, int size,
                          const std::function<glm::vec3(size_t, glm::vec3)>& colorAt) {
    std::vector<uint8_t> rgba(size_t(size) * size_t(size) * 4, 255);
    std::vector<uint8_t> filled(size_t(size) * size_t(size), 0);
    for (size_t t = 0; t < triangles.size(); ++t) {
        const glm::vec2 a = uvs[t * 3] * float(size), b = uvs[t * 3 + 1] * float(size), c = uvs[t * 3 + 2] * float(size);
        const float area = (b.x - a.x) * (c.y - a.y) - (c.x - a.x) * (b.y - a.y);
        if (std::abs(area) < 1e-12f) continue;
        const int x0 = std::max(0, int(std::floor(std::min({a.x, b.x, c.x}))));
        const int x1 = std::min(size - 1, int(std::ceil(std::max({a.x, b.x, c.x}))));
        const int y0 = std::max(0, int(std::floor(std::min({a.y, b.y, c.y}))));
        const int y1 = std::min(size - 1, int(std::ceil(std::max({a.y, b.y, c.y}))));
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x) {
                const glm::vec2 p(float(x) + 0.5f, float(y) + 0.5f);
                float w0 = ((b.x - p.x) * (c.y - p.y) - (c.x - p.x) * (b.y - p.y)) / area;
                float w1 = ((c.x - p.x) * (a.y - p.y) - (a.x - p.x) * (c.y - p.y)) / area;
                float w2 = 1.0f - w0 - w1;
                // Texels whose centre lies just outside still get the triangle's colour (thin slivers, edges).
                constexpr float eps = -0.02f;
                if (w0 < eps || w1 < eps || w2 < eps) continue;
                w0 = std::max(w0, 0.0f);
                w1 = std::max(w1, 0.0f);
                w2 = std::max(w2, 0.0f);
                const float sum = w0 + w1 + w2;
                const glm::vec3 col = glm::clamp(colorAt(t, glm::vec3(w0, w1, w2) / sum), glm::vec3(0.0f), glm::vec3(1.0f));
                const size_t i = size_t(y) * size_t(size) + size_t(x);
                rgba[i * 4 + 0] = uint8_t(std::lround(col.x * 255.0f));
                rgba[i * 4 + 1] = uint8_t(std::lround(col.y * 255.0f));
                rgba[i * 4 + 2] = uint8_t(std::lround(col.z * 255.0f));
                filled[i] = 1;
            }
    }
    // Padding: each empty texel next to filled ones takes their average, ring after ring.
    for (int ring = 0; ring < kPadding; ++ring) {
        std::vector<uint8_t> next = filled;
        for (int y = 0; y < size; ++y)
            for (int x = 0; x < size; ++x) {
                const size_t i = size_t(y) * size_t(size) + size_t(x);
                if (filled[i]) continue;
                int sum[3] = {0, 0, 0}, n = 0;
                for (int dy = -1; dy <= 1; ++dy)
                    for (int dx = -1; dx <= 1; ++dx) {
                        const int nx = x + dx, ny = y + dy;
                        if (nx < 0 || ny < 0 || nx >= size || ny >= size) continue;
                        const size_t j = size_t(ny) * size_t(size) + size_t(nx);
                        if (!filled[j]) continue;
                        for (int k = 0; k < 3; ++k) sum[k] += rgba[j * 4 + size_t(k)];
                        ++n;
                    }
                if (!n) continue;
                for (int k = 0; k < 3; ++k) rgba[i * 4 + size_t(k)] = uint8_t(sum[k] / n);
                next[i] = 1;
            }
        filled.swap(next);
    }
    return rgba;
}

// A copy with only what AR needs: current positions, visible triangles, vertex paint, and the texture's corner UVs.
Mesh leanCopy(const Mesh& src, bool keepUvs) {
    Mesh m;
    m.positions = src.positions;
    if (src.hasColors()) m.colors = src.colors;
    m.triangles.reserve(src.triangleCount());
    if (keepUvs) m.uvs.reserve(src.triangleCount() * 3);
    for (uint32_t t = 0; t < src.triangleCount(); ++t) {
        if (src.triangleHidden(t)) continue;
        m.triangles.push_back(src.triangles[t]);
        if (keepUvs)
            for (int c = 0; c < 3; ++c) m.uvs.push_back(src.uvs[size_t(t) * 3 + size_t(c)]);
    }
    return m;
}

// Vertex clustering on a grid: a quick first reduction for meshes too big for quadric decimation. Colours are
// averaged; a texture is sampled into vertex colours first (its corner UVs can't survive clustering).
Mesh cluster(const Mesh& src, const PaintTexture* texture, size_t targetVertices) {
    double area = 0.0;
    for (const auto& t : src.triangles)
        area += 0.5 * double(glm::length(glm::cross(src.positions[t[1]] - src.positions[t[0]], src.positions[t[2]] - src.positions[t[0]])));
    const float h = float(std::sqrt(std::max(area, 1e-12) / double(std::max<size_t>(targetVertices, 1000))));
    std::vector<glm::vec3> vertexColor;
    const bool colored = texture || src.hasColors();
    if (texture) {
        vertexColor.assign(src.vertexCount(), glm::vec3(1.0f));
        for (size_t t = 0; t < src.triangleCount(); ++t)
            for (int c = 0; c < 3; ++c)
                vertexColor[src.triangles[t][c]] = srgbToLinear(sampleBaked(*texture, src.uvs[t * 3 + size_t(c)]));
    }
    struct Key {
        int x, y, z;
        bool operator==(const Key& o) const { return x == o.x && y == o.y && z == o.z; }
    };
    struct KeyHash {
        size_t operator()(const Key& k) const {
            return size_t(uint64_t(uint32_t(k.x)) * 73856093u ^ uint64_t(uint32_t(k.y)) * 19349663u ^ uint64_t(uint32_t(k.z)) * 83492791u);
        }
    };
    std::unordered_map<Key, uint32_t, KeyHash> cells;
    std::vector<uint32_t> remap(src.vertexCount());
    std::vector<glm::dvec3> sumP;
    std::vector<glm::dvec3> sumC;
    std::vector<uint32_t> count;
    for (uint32_t v = 0; v < src.vertexCount(); ++v) {
        const glm::vec3 p = src.positions[v] / h;
        const Key k{int(std::floor(p.x)), int(std::floor(p.y)), int(std::floor(p.z))};
        auto [it, added] = cells.try_emplace(k, uint32_t(sumP.size()));
        if (added) {
            sumP.emplace_back(0.0);
            sumC.emplace_back(0.0);
            count.push_back(0);
        }
        const uint32_t c = it->second;
        remap[v] = c;
        sumP[c] += glm::dvec3(src.positions[v]);
        if (colored) sumC[c] += glm::dvec3(texture ? vertexColor[v] : src.colors[v]);
        ++count[c];
    }
    Mesh out;
    out.positions.resize(sumP.size());
    if (colored) out.colors.resize(sumP.size());
    for (size_t c = 0; c < sumP.size(); ++c) {
        out.positions[c] = glm::vec3(sumP[c] / double(count[c]));
        if (colored) out.colors[c] = glm::vec3(sumC[c] / double(count[c]));
    }
    for (const auto& t : src.triangles) {
        const glm::uvec3 n(remap[t[0]], remap[t[1]], remap[t[2]]);
        if (n[0] != n[1] && n[1] != n[2] && n[0] != n[2]) out.triangles.push_back(n);
    }
    return out;
}

std::string readFile(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

std::string withThousands(size_t n) {
    std::string s = std::to_string(n);
    for (int i = int(s.size()) - 3; i > 0; i -= 3) s.insert(size_t(i), ",");
    return s;
}

} // namespace

std::vector<uint8_t> bakeCornerColors(const std::vector<glm::uvec3>& triangles, const std::vector<glm::vec2>& uvs,
                                      const std::vector<glm::vec3>& cornerColors, int size) {
    return bake(triangles, uvs, size, [&](size_t t, glm::vec3 w) {
        return cornerColors[t * 3] * w.x + cornerColors[t * 3 + 1] * w.y + cornerColors[t * 3 + 2] * w.z;
    });
}

bool exportArModel(const std::vector<const Mesh*>& meshes, const ArExportOptions& options, ArExportResult& out,
                   std::string& error) {
    out = {};
    size_t total = 0;
    std::vector<const Mesh*> parts;
    for (const Mesh* m : meshes)
        if (m && m->triangleCount() > 0) {
            parts.push_back(m);
            total += m->triangleCount();
        }
    if (parts.empty()) {
        error = "Nothing to show: the model is empty";
        return false;
    }
    const size_t budget = std::max<size_t>(options.maxTriangles, 200);

    // Reduce each part to its share of the budget, keeping colour sources.
    std::vector<Mesh> reducedParts;
    std::vector<PartColour> partInfo;
    bool anyColor = false;
    for (const Mesh* src : parts) {
        const bool textured = hasTexture(*src);
        const size_t share = std::max<size_t>(64, size_t(double(budget) * double(src->triangleCount()) / double(total)));
        Mesh lean = leanCopy(*src, textured);
        PartColour info;
        if (lean.triangleCount() > kClusterAbove) {
            lean = cluster(lean, textured ? src->texture.get() : nullptr,
                           std::min<size_t>(lean.triangleCount() / 2, std::max<size_t>(share * 4, 500'000)));
        } else if (textured) {
            info.texture = src->texture.get();
        }
        Mesh reduced = lean.triangleCount() > share ? decimate(lean, share) : std::move(lean);
        if (info.texture && reduced.uvs.size() != reduced.triangleCount() * 3) info.texture = nullptr;
        info.colors = reduced.hasColors();
        anyColor = anyColor || info.colors || info.texture;
        partInfo.push_back(info);
        reducedParts.push_back(std::move(reduced));
    }
    // One mesh: per triangle its part, per corner the part's texture UV, per vertex vertex paint (white elsewhere).
    Mesh merged;
    std::vector<uint32_t> triPart;
    std::vector<glm::vec2> sourceUvs;
    const bool anyVertexPaint = std::any_of(partInfo.begin(), partInfo.end(), [](const PartColour& p) { return p.colors; });
    for (size_t i = 0; i < reducedParts.size(); ++i) {
        const Mesh& reduced = reducedParts[i];
        const uint32_t base = uint32_t(merged.positions.size());
        merged.positions.insert(merged.positions.end(), reduced.positions.begin(), reduced.positions.end());
        if (anyVertexPaint) {
            if (partInfo[i].colors) merged.colors.insert(merged.colors.end(), reduced.colors.begin(), reduced.colors.end());
            else merged.colors.resize(merged.positions.size(), glm::vec3(1.0f));
        }
        for (size_t t = 0; t < reduced.triangleCount(); ++t) {
            merged.triangles.push_back(reduced.triangles[t] + glm::uvec3(base));
            triPart.push_back(uint32_t(i));
            for (int c = 0; c < 3; ++c)
                sourceUvs.push_back(partInfo[i].texture ? reduced.uvs[t * 3 + size_t(c)] : glm::vec2(0.0f));
        }
    }
    reducedParts.clear();
    if (merged.triangleCount() == 0) {
        error = "Nothing to show: every polygon is hidden";
        return false;
    }

    // Stand it on the desk: centred, base at y = 0, largest side = sizeMeters.
    AABB box;
    for (const auto& p : merged.positions) box.expand(p);
    const glm::vec3 extent = box.extent();
    const float largest = std::max({extent.x, extent.y, extent.z, 1e-6f});
    const float scale = std::max(options.sizeMeters, 0.001f) / largest;
    const glm::vec3 offset(-box.center().x, -box.min.y, -box.center().z);
    for (auto& p : merged.positions) p = (p + offset) * scale;

    // Colour: new UVs for the merged mesh and one texture baked from each part's vertex paint or texture.
    if (anyColor) {
        AutoUvResult atlas;
        std::string uvError;
        if (!computeAutoUv(merged.positions, merged.triangles, options.textureSize, atlas, uvError)) {
            error = "Couldn't lay out the colours (" + uvError + ")";
            return false;
        }
        std::vector<uint8_t> rgba = bake(merged.triangles, atlas.uvs, options.textureSize, [&](size_t t, glm::vec3 w) {
            const PartColour& part = partInfo[triPart[t]];
            if (part.texture)
                return sampleBaked(*part.texture, sourceUvs[t * 3] * w.x + sourceUvs[t * 3 + 1] * w.y + sourceUvs[t * 3 + 2] * w.z);
            if (part.colors) {
                const glm::uvec3 tri = merged.triangles[t];
                return linearToSrgb(merged.colors[tri[0]] * w.x + merged.colors[tri[1]] * w.y + merged.colors[tri[2]] * w.z);
            }
            return glm::vec3(1.0f);
        });
        auto tex = makeTexture(options.textureSize, options.textureSize);
        tex->pixels = std::move(rgba);
        merged.texture = tex;
        merged.uvs = std::move(atlas.uvs);
        merged.clearColors(); // the texture carries them (glTF would multiply vertex colours in again)
        out.colored = true;
    }
    merged.rebuildTopology();
    merged.recomputeAllNormals();

    // Write both files through the regular exporters.
    std::error_code ec;
    const std::string stamp = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    const fs::path dir = fs::temp_directory_path(ec);
    const fs::path glbPath = dir / ("sculptforge_ar_" + stamp + ".glb");
    const fs::path usdzPath = dir / ("sculptforge_ar_" + stamp + ".usdz");
    const auto text = [](const fs::path& p) {
        const auto u = p.u8string();
        return std::string(u.begin(), u.end());
    };
    bool ok = saveGlb(text(glbPath), merged, options.name, error) &&
              saveUsd(text(usdzPath), &merged, nullptr, options.name, error);
    if (ok) {
        out.glb = readFile(glbPath);
        out.usdz = readFile(usdzPath);
        ok = !out.glb.empty() && !out.usdz.empty();
        if (!ok) error = "Couldn't read the exported model back";
    }
    fs::remove(glbPath, ec);
    fs::remove(usdzPath, ec);
    if (!ok) return false;
    out.triangles = merged.triangleCount();
    char size[32];
    std::snprintf(size, sizeof size, "%.0f cm", double(options.sizeMeters) * 100.0);
    out.summary = withThousands(out.triangles) + " triangles" + (out.colored ? ", in colour" : "") + ", " + size;
    return true;
}

} // namespace sf
