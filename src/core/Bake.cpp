// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/Bake.h"

#include "core/BakeImage.h"
#include "core/MeshMaps.h"
#include "core/Micro.h"
#include "core/Multires.h"
#include "core/RayBvh.h"
#include "core/Document.h"
#include "core/Tangents.h"
#include "core/Texture.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdlib>
#include <limits>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <sstream>
#include <thread>

namespace sf {

namespace {

using Clock = std::chrono::steady_clock;
double secondsSince(Clock::time_point t) { return std::chrono::duration<double>(Clock::now() - t).count(); }

std::filesystem::path fsPath(const std::string& s) { return std::filesystem::path(std::u8string(s.begin(), s.end())); }
std::string utf8(const std::filesystem::path& p) {
    const std::u8string s = p.u8string();
    return std::string(s.begin(), s.end());
}

// ---------------------------------------------------------------------------
// The high-poly side: every source mesh's visible triangles in one list.
// ---------------------------------------------------------------------------

struct Source {
    std::vector<glm::vec3> pos, nrm;
    std::vector<glm::uvec3> tris;
    std::vector<uint32_t> triMesh, triLocal; // which source mesh / which of its triangles
    std::vector<uint32_t> vertexOffset;      // first vertex of each source mesh
    std::vector<float> curvature;            // per vertex, normalised to about -1..1 (cavity map)
    std::vector<float> meanCurvature;        // per vertex, 1 / length (curvature map; meshMeanCurvature)
    std::vector<const Mesh*> meshes;
};

// Cavity per vertex: a mean-curvature-like value (how far the one-ring's centre lies above the vertex along
// its normal, relative to the ring's size; > 0 in creases), lightly smoothed, minus a broadly smoothed copy so
// only the sculpted detail remains (the overall shape is neither cavity nor ridge). Divided by its 99.5th
// percentile (at least 0.01) so the map uses its range whatever the mesh's density.
void computeCurvature(Source& s) {
    s.curvature.assign(s.pos.size(), 0.0f);
    for (size_t m = 0; m < s.meshes.size(); ++m) {
        const Mesh& mesh = *s.meshes[m];
        const uint32_t off = s.vertexOffset[m];
        const size_t nv = mesh.vertexCount();
        std::vector<float> c(nv, 0.0f), tmp(nv);
        for (uint32_t v = 0; v < nv; ++v) {
            const auto ring = mesh.neighbors(v);
            if (ring.empty()) continue;
            glm::vec3 avg(0.0f);
            float edge = 0.0f;
            for (uint32_t n : ring) {
                avg += mesh.positions[n];
                edge += glm::distance(mesh.positions[n], mesh.positions[v]);
            }
            avg /= float(ring.size());
            edge /= float(ring.size());
            c[v] = edge > 0.0f ? glm::dot(mesh.normals[v], avg - mesh.positions[v]) / edge : 0.0f;
        }
        auto smooth = [&](std::vector<float>& f, int iterations) {
            for (int it = 0; it < iterations; ++it) {
                for (uint32_t v = 0; v < nv; ++v) {
                    const auto ring = mesh.neighbors(v);
                    float sum = f[v];
                    for (uint32_t n : ring) sum += f[n];
                    tmp[v] = sum / float(ring.size() + 1);
                }
                f.swap(tmp);
            }
        };
        smooth(c, 2);
        std::vector<float> broad = c;
        smooth(broad, 24);
        for (size_t v = 0; v < nv; ++v) s.curvature[off + v] = c[v] - broad[v];
    }
    if (s.curvature.empty()) return;
    std::vector<float> mag(s.curvature.size());
    for (size_t i = 0; i < mag.size(); ++i) mag[i] = std::abs(s.curvature[i]);
    const size_t k = std::min(mag.size() - 1, size_t(double(mag.size()) * 0.995));
    std::nth_element(mag.begin(), mag.begin() + std::ptrdiff_t(k), mag.end());
    const float scale = std::max(mag[k], 0.01f);
    for (float& c : s.curvature) c = std::clamp(c / scale, -1.0f, 1.0f);
}

// ---------------------------------------------------------------------------
// Sample rasterisation: texel (x, y) has samples (x * s + i, y * s + j) of a grid s times finer, at their
// centres. Fixed point (1/256 sample) with a top-left rule, so a sample on an edge two triangles share is
// baked once.
// ---------------------------------------------------------------------------

constexpr int64_t kSub = 256;

struct SampleTri {
    int64_t px[3], py[3];
    int order[3] = {0, 1, 2};
    int64_t area = 0;
    bool topLeft[3] = {};
    bool valid = false;
    int sx0 = 0, sy0 = 0, sx1 = -1, sy1 = -1; // inclusive sample bounds

    SampleTri(const glm::vec2* uv, int gw, int gh) {
        float minX = 1e30f, minY = 1e30f, maxX = -1e30f, maxY = -1e30f;
        for (int i = 0; i < 3; ++i) {
            if (!std::isfinite(uv[i].x) || !std::isfinite(uv[i].y)) return;
            const double x = std::clamp(double(uv[i].x), -4.0, 5.0) * gw, y = std::clamp(double(uv[i].y), -4.0, 5.0) * gh;
            px[i] = std::llround(x * kSub);
            py[i] = std::llround(y * kSub);
            minX = std::min(minX, float(x)), maxX = std::max(maxX, float(x));
            minY = std::min(minY, float(y)), maxY = std::max(maxY, float(y));
        }
        area = (px[1] - px[0]) * (py[2] - py[0]) - (py[1] - py[0]) * (px[2] - px[0]);
        if (area == 0) return;
        if (area < 0) {
            std::swap(px[1], px[2]);
            std::swap(py[1], py[2]);
            std::swap(order[1], order[2]);
            area = -area;
        }
        for (int i = 0; i < 3; ++i) {
            const int a = (i + 1) % 3, b = (i + 2) % 3;
            const int64_t dx = px[b] - px[a], dy = py[b] - py[a];
            topLeft[i] = dy < 0 || (dy == 0 && dx < 0);
        }
        sx0 = std::max(0, int(std::floor(minX - 0.5f)));
        sy0 = std::max(0, int(std::floor(minY - 0.5f)));
        sx1 = std::min(gw - 1, int(std::ceil(maxX - 0.5f)));
        sy1 = std::min(gh - 1, int(std::ceil(maxY - 0.5f)));
        valid = sx0 <= sx1 && sy0 <= sy1;
    }
    int64_t edge(int i, int64_t X, int64_t Y) const {
        const int a = (i + 1) % 3, b = (i + 2) % 3;
        return (px[b] - px[a]) * (Y - py[a]) - (py[b] - py[a]) * (X - px[a]);
    }
};

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------

inline uint32_t hash32(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}
inline float radicalInverse(uint32_t b) {
    b = (b << 16u) | (b >> 16u);
    b = ((b & 0x55555555u) << 1u) | ((b & 0xAAAAAAAAu) >> 1u);
    b = ((b & 0x33333333u) << 2u) | ((b & 0xCCCCCCCCu) >> 2u);
    b = ((b & 0x0F0F0F0Fu) << 4u) | ((b & 0xF0F0F0F0u) >> 4u);
    b = ((b & 0x00FF00FFu) << 8u) | ((b & 0xFF00FF00u) >> 8u);
    return float(b) * 2.3283064365386963e-10f;
}
// Orthonormal basis around a unit normal (Duff et al. 2017).
inline void basis(const glm::vec3& n, glm::vec3& t, glm::vec3& b) {
    const float sign = std::copysign(1.0f, n.z);
    const float a = -1.0f / (sign + n.z);
    const float bb = n.x * n.y * a;
    t = glm::vec3(1.0f + sign * n.x * n.x * a, sign * bb, -sign * n.x);
    b = glm::vec3(bb, sign + n.y * n.y * a, -n.y);
}
inline uint8_t unorm8(float x) { return uint8_t(std::lround(std::clamp(x, 0.0f, 1.0f) * 255.0f)); }
inline glm::vec3 safeNormalize(const glm::vec3& v, const glm::vec3& fallback) {
    const float l = glm::length(v);
    return l > 1e-12f ? v / l : fallback;
}

// Runs fn(worker) on `threads` threads (the caller is worker 0).
template <class Fn>
void runWorkers(int threads, Fn fn) {
    std::vector<std::thread> pool;
    for (int i = 1; i < threads; ++i) pool.emplace_back([&fn, i] { fn(i); });
    fn(0);
    for (auto& t : pool) t.join();
}

int threadCount(int wanted) {
    const int hw = int(std::max(1u, std::thread::hardware_concurrency()));
    return std::clamp(wanted > 0 ? wanted : hw, 1, 64);
}

std::mutex g_listenerMutex;
BakeListener g_listener;

} // namespace

// ---------------------------------------------------------------------------
// Names
// ---------------------------------------------------------------------------

const char* bakeMapId(BakeMap m) {
    switch (m) {
    case BakeMap::Normal: return "normal";
    case BakeMap::ObjectNormal: return "object_normal";
    case BakeMap::Displacement: return "displacement";
    case BakeMap::AmbientOcclusion: return "ao";
    case BakeMap::Cavity: return "cavity";
    case BakeMap::Color: return "color";
    case BakeMap::Curvature: return "curvature";
    case BakeMap::Thickness: return "thickness";
    case BakeMap::Position: return "position";
    case BakeMap::Id: return "id";
    }
    return "";
}

const char* bakeMapLabel(BakeMap m) {
    switch (m) {
    case BakeMap::Normal: return "Normal (tangent)";
    case BakeMap::ObjectNormal: return "Normal (object / world)";
    case BakeMap::Displacement: return "Displacement";
    case BakeMap::AmbientOcclusion: return "Ambient Occlusion";
    case BakeMap::Cavity: return "Cavity";
    case BakeMap::Color: return "Colour";
    case BakeMap::Curvature: return "Curvature";
    case BakeMap::Thickness: return "Thickness";
    case BakeMap::Position: return "Position";
    case BakeMap::Id: return "ID";
    }
    return "";
}

bool parseBakeMaps(const std::string& list, uint32_t& maps, std::string& error) {
    maps = 0;
    std::string item;
    std::stringstream ss(list);
    while (std::getline(ss, item, ',')) {
        std::string s;
        for (char c : item)
            if (!std::isspace(static_cast<unsigned char>(c))) s += char(std::tolower(static_cast<unsigned char>(c)));
        if (s.empty()) continue;
        if (s == "all") {
            maps = (1u << kBakeMapCount) - 1;
            continue;
        }
        // Common aliases.
        if (s == "normals" || s == "tangent" || s == "tangent_normal" || s == "nm") s = "normal";
        if (s == "object" || s == "objectnormal" || s == "object_space" || s == "world_normal") s = "object_normal";
        if (s == "disp" || s == "height") s = "displacement";
        if (s == "occlusion" || s == "ambient_occlusion") s = "ao";
        if (s == "convexity") s = "curvature";
        if (s == "thick" || s == "translucency") s = "thickness";
        if (s == "pos") s = "position";
        if (s == "material_id" || s == "polygroups" || s == "polygroup") s = "id";
        if (s == "colour" || s == "polypaint" || s == "albedo" || s == "texture") s = "color";
        bool found = false;
        for (int i = 0; i < kBakeMapCount; ++i)
            if (s == bakeMapId(BakeMap(i))) {
                maps |= 1u << i;
                found = true;
            }
        if (!found) {
            error = "Unknown map '" + item + "' (use normal, object_normal, displacement, ao, cavity, color, curvature, "
                    "thickness, position, id or all)";
            return false;
        }
    }
    if (!maps) {
        error = "Choose at least one map";
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// The bake
// ---------------------------------------------------------------------------

bool bakeMaps(const Mesh& low, const std::vector<const Mesh*>& sources, const BakeSettings& settingsIn, BakeResult& out,
              std::string& error, const BakeProgress& progress) {
    const auto start = Clock::now();
    BakeSettings st = settingsIn;
    out = BakeResult();
    const size_t nt = low.triangleCount();
    if (nt == 0) {
        error = "The low-poly Part is empty";
        return false;
    }
    if (low.uvs.size() != nt * 3) {
        error = "The low-poly Part has no UVs: import it with UVs or run Auto UV (Tool > Texture Map)";
        return false;
    }
    if (low.normals.size() != low.vertexCount()) {
        error = "The low-poly Part has no normals";
        return false;
    }
    if (st.size < 64 || st.size > 8192) {
        error = "Map size must be between 64 and 8192";
        return false;
    }
    if (!st.maps) {
        error = "Choose at least one map";
        return false;
    }
    st.samples = std::clamp(st.samples, 1, 4);
    st.padding = std::clamp(st.padding, 0, 64);
    st.aoRays = std::clamp(st.aoRays, 1, 1024);

    // --- Gather the source triangles ---
    Source src;
    for (const Mesh* m : sources) {
        if (!m || m == &low || m->triangleCount() == 0 || m->normals.size() != m->vertexCount()) continue;
        const uint32_t mi = uint32_t(src.meshes.size()), off = uint32_t(src.pos.size());
        src.meshes.push_back(m);
        src.vertexOffset.push_back(off);
        src.pos.insert(src.pos.end(), m->positions.begin(), m->positions.end());
        src.nrm.insert(src.nrm.end(), m->normals.begin(), m->normals.end());
        for (uint32_t t = 0; t < m->triangleCount(); ++t) {
            if (m->triangleHidden(t)) continue;
            src.tris.push_back(m->triangles[t] + glm::uvec3(off));
            src.triMesh.push_back(mi);
            src.triLocal.push_back(t);
        }
    }
    if (src.tris.empty()) {
        error = "There is no high-poly source to bake from (show the Parts to bake, or name one)";
        return false;
    }
    bool anyColor = false;
    for (const Mesh* m : src.meshes) anyColor = anyColor || hasTexture(*m) || m->hasColors();
    if ((st.maps & bakeBit(BakeMap::Color)) && !anyColor) {
        st.maps &= ~bakeBit(BakeMap::Color);
        if (!st.maps) {
            error = "The source has no vertex paint or texture to transfer";
            return false;
        }
    }
    const auto bvhStart = Clock::now();
    RayBvh bvh;
    bvh.build(src.pos, src.tris);
    if (st.maps & bakeBit(BakeMap::Cavity)) computeCurvature(src);
    if (st.maps & bakeBit(BakeMap::Curvature)) {
        src.meanCurvature.assign(src.pos.size(), 0.0f);
        for (size_t m = 0; m < src.meshes.size(); ++m) {
            const std::vector<float> h = meshMeanCurvature(*src.meshes[m]);
            std::copy(h.begin(), h.end(), src.meanCurvature.begin() + std::ptrdiff_t(src.vertexOffset[m]));
        }
    }
    out.bvhSeconds = secondsSince(bvhStart);
    out.sourceTriangles = src.tris.size();

    const bool wantNormal = st.maps & bakeBit(BakeMap::Normal);
    const bool wantObject = st.maps & bakeBit(BakeMap::ObjectNormal);
    const bool wantDisp = st.maps & bakeBit(BakeMap::Displacement);
    const bool wantAo = st.maps & bakeBit(BakeMap::AmbientOcclusion);
    const bool wantCavity = st.maps & bakeBit(BakeMap::Cavity);
    const bool wantColor = st.maps & bakeBit(BakeMap::Color);
    const bool wantCurv = st.maps & bakeBit(BakeMap::Curvature);
    const bool wantThick = st.maps & bakeBit(BakeMap::Thickness);
    const bool wantPos = st.maps & bakeBit(BakeMap::Position);
    const bool wantId = st.maps & bakeBit(BakeMap::Id);

    AABB lb = low.bounds();
    AABB all = lb;
    for (const glm::vec3& p : src.pos) all.expand(p);
    const float diag = std::max(glm::length(lb.extent()), 1e-6f);
    const float sceneDiag = std::max(glm::length(all.extent()), 1e-6f);
    out.diagonal = diag;
    const float eps = sceneDiag * 2e-6f;

    // MikkTSpace tangents of the low-poly (the tangent-space map and the viewport preview).
    if (wantNormal) out.tangents = computeTangents(low);

    // Smooth surface point and normal of the low-poly.
    auto lowPoint = [&](uint32_t t, const glm::vec3& b, glm::vec3& p, glm::vec3& n) {
        const glm::uvec3& tv = low.triangles[t];
        p = low.positions[tv.x] * b.x + low.positions[tv.y] * b.y + low.positions[tv.z] * b.z;
        const glm::vec3 face = glm::cross(low.positions[tv.y] - low.positions[tv.x], low.positions[tv.z] - low.positions[tv.x]);
        n = safeNormalize(low.normals[tv.x] * b.x + low.normals[tv.y] * b.y + low.normals[tv.z] * b.z,
                          safeNormalize(face, glm::vec3(0, 1, 0)));
    };

    // --- Ray range: measured from a sample of low-poly points when automatic ---
    if (st.autoDistance) {
        const float probe = diag * 0.1f;
        const uint32_t probes = uint32_t(std::clamp<size_t>(nt * 16, 20000, 200000));
        const int pt = threadCount(st.threads);
        std::vector<float> outMax(size_t(pt), 0.0f), inMax(size_t(pt), 0.0f);
        runWorkers(pt, [&](int w) {
            for (uint32_t i = uint32_t(w); i < probes; i += uint32_t(pt)) {
                const uint32_t t = hash32(i * 2654435761u + 17u) % uint32_t(nt);
                float r1 = radicalInverse(i + 1), r2 = float(hash32(i + 99u) & 0xFFFFFF) / float(0x1000000);
                if (r1 + r2 > 1.0f) r1 = 1.0f - r1, r2 = 1.0f - r2;
                glm::vec3 p, n;
                lowPoint(t, glm::vec3(1.0f - r1 - r2, r1, r2), p, n);
                RayHitInfo h;
                if (!bvh.nearest(p + n * probe, -n, 0.0f, 2.0f * probe, probe, h)) continue;
                const float d = probe - h.t;
                float& m = d >= 0 ? outMax[size_t(w)] : inMax[size_t(w)];
                m = std::max(m, std::abs(d));
            }
        });
        // The farthest the high-poly strays each way, plus a margin (small features between probes).
        const float floor = diag * 0.004f;
        st.front = std::min(probe, std::max(*std::max_element(outMax.begin(), outMax.end()) * 1.2f, floor)) / diag;
        st.rear = std::min(probe, std::max(*std::max_element(inMax.begin(), inMax.end()) * 1.2f, floor)) / diag;
    }
    const float front = std::max(st.front, 0.0f) * diag, rear = std::max(st.rear, 0.0f) * diag;
    out.front = front;
    out.rear = rear;
    if (front + rear <= 0.0f) {
        error = "Front and rear distances are both zero";
        return false;
    }
    const float aoDist = std::max(st.aoDistance, 1e-4f) * diag;
    const float thickDist = std::max(st.thicknessDistance, 1e-4f) * diag;
    const float curvRadius = std::max(st.curvatureRadius, 1e-5f) * diag;
    out.positionMin = all.min;
    out.positionMax = all.max;
    const glm::vec3 posScale = 1.0f / glm::max(all.max - all.min, glm::vec3(1e-12f));
    // ID colours: by polygroup (when any source has them and IdSource::Polygroups) or by source mesh.
    bool anyGroups = false;
    for (const Mesh* m : src.meshes) anyGroups = anyGroups || m->hasPolygroups();
    const bool idByGroup = st.idSource == BakeSettings::IdSource::Polygroups && anyGroups;

    // --- Output planes ---
    const int W = st.size, H = st.size, S = st.samples;
    const size_t texels = size_t(W) * size_t(H);
    out.width = W;
    out.height = H;
    out.maps = st.maps;
    out.flipGreen = st.flipGreen;
    if (wantNormal) out.normal.assign(texels * 3, 0);
    if (wantObject) out.objectNormal.assign(texels * 3, 0);
    if (wantColor) out.color.assign(texels * 3, 0);
    if (wantAo) out.ao.assign(texels, 255);
    if (wantCavity) out.cavity.assign(texels, 128);
    if (wantDisp) out.displacement.assign(texels, 0.0f);
    if (wantCurv) out.curvature.assign(texels, 128);
    if (wantThick) out.thickness.assign(texels, 255);
    if (wantPos) out.position.assign(texels * 3, 0);
    if (wantId) out.id.assign(texels * 3, 0);
    out.coverage.assign(texels, 0);
    // Float copies of the normals for padding (averaged and renormalised there), only while baking.
    std::vector<glm::vec3> nTs, nOs, col;
    std::vector<float> aoF, cavF;

    // --- Bin low-poly triangles into tiles ---
    constexpr int kTile = 32;
    const int tilesX = (W + kTile - 1) / kTile, tilesY = (H + kTile - 1) / kTile;
    const int gw = W * S, gh = H * S;
    std::vector<uint32_t> binCount(size_t(tilesX) * size_t(tilesY) + 1, 0);
    std::vector<glm::ivec4> triTiles(nt, glm::ivec4(0, 0, -1, -1));
    for (uint32_t t = 0; t < nt; ++t) {
        const SampleTri s(&low.uvs[size_t(t) * 3], gw, gh);
        if (!s.valid) continue;
        const glm::ivec4 r(s.sx0 / S / kTile, s.sy0 / S / kTile, s.sx1 / S / kTile, s.sy1 / S / kTile);
        triTiles[t] = r;
        for (int ty = r.y; ty <= r.w; ++ty)
            for (int tx = r.x; tx <= r.z; ++tx) ++binCount[size_t(ty) * size_t(tilesX) + size_t(tx) + 1];
    }
    for (size_t i = 1; i < binCount.size(); ++i) binCount[i] += binCount[i - 1];
    std::vector<uint32_t> binTris(binCount.back());
    {
        std::vector<uint32_t> fill(binCount.begin(), binCount.end() - 1);
        for (uint32_t t = 0; t < nt; ++t) {
            const glm::ivec4 r = triTiles[t];
            for (int ty = r.y; ty <= r.w; ++ty)
                for (int tx = r.x; tx <= r.z; ++tx) binTris[fill[size_t(ty) * size_t(tilesX) + size_t(tx)]++] = t;
        }
    }
    std::vector<uint32_t> work; // tiles with triangles, the busiest first for better load balance
    for (uint32_t i = 0; i + 1 < binCount.size(); ++i)
        if (binCount[i + 1] > binCount[i]) work.push_back(i);

    const int threads = threadCount(st.threads);
    std::atomic<size_t> next{0}, done{0}, island{0}, missed{0};
    std::atomic<bool> cancel{false};
    std::mutex rangeMutex;
    float dMin = std::numeric_limits<float>::max(), dMax = std::numeric_limits<float>::lowest();
    const int aoPerSample = std::max(1, (st.aoRays + S * S - 1) / (S * S));
    const int thickPerSample = std::max(1, (st.thicknessRays + S * S - 1) / (S * S));

    runWorkers(threads, [&](int worker) {
        struct Acc {
            glm::vec3 ts{0}, os{0}, color{0}, lowN{0}, pos{0}, id{0};
            float disp = 0, ao = 0, cav = 0, curv = 0, thick = 0;
            uint16_t hits = 0, samples = 0;
        };
        std::vector<Acc> acc(size_t(kTile) * kTile);
        float localMin = std::numeric_limits<float>::max(), localMax = std::numeric_limits<float>::lowest();
        size_t localIsland = 0, localMissed = 0;
        for (;;) {
            if (cancel.load(std::memory_order_relaxed)) break;
            const size_t wi = next.fetch_add(1);
            if (wi >= work.size()) break;
            const uint32_t tile = work[wi];
            const int tx = int(tile % uint32_t(tilesX)), ty = int(tile / uint32_t(tilesX));
            const int x0 = tx * kTile, y0 = ty * kTile;
            const int x1 = std::min(W, x0 + kTile), y1 = std::min(H, y0 + kTile);
            std::fill(acc.begin(), acc.end(), Acc());
            for (uint32_t k = binCount[tile]; k < binCount[tile + 1]; ++k) {
                const uint32_t t = binTris[k];
                const SampleTri s(&low.uvs[size_t(t) * 3], gw, gh);
                const int sx0 = std::max(s.sx0, x0 * S), sx1 = std::min(s.sx1, x1 * S - 1);
                const int sy0 = std::max(s.sy0, y0 * S), sy1 = std::min(s.sy1, y1 * S - 1);
                const double inv = 1.0 / double(s.area);
                for (int sy = sy0; sy <= sy1; ++sy) {
                    const int64_t Y = int64_t(sy) * kSub + kSub / 2;
                    for (int sx = sx0; sx <= sx1; ++sx) {
                        const int64_t X = int64_t(sx) * kSub + kSub / 2;
                        int64_t e[3];
                        bool inside = true;
                        for (int i = 0; i < 3 && inside; ++i) {
                            e[i] = s.edge(i, X, Y);
                            inside = e[i] > 0 || (e[i] == 0 && s.topLeft[i]);
                        }
                        if (!inside) continue;
                        float bw[3];
                        for (int i = 0; i < 3; ++i) bw[s.order[i]] = float(double(e[i]) * inv);
                        const glm::vec3 b(bw[0], bw[1], bw[2]);
                        const int x = sx / S, y = sy / S;
                        Acc& a = acc[size_t(y - y0) * kTile + size_t(x - x0)];
                        a.samples = uint16_t(std::min(a.samples + 1, 65535));

                        glm::vec3 p, n;
                        lowPoint(t, b, p, n);
                        a.lowN += n;
                        RayHitInfo h;
                        if (!bvh.nearest(p + n * front, -n, 0.0f, front + rear, front, h)) continue;
                        a.hits = uint16_t(std::min(a.hits + 1, 65535));
                        const glm::uvec3& ht = src.tris[h.tri];
                        const float w0 = 1.0f - h.u - h.v;
                        const glm::vec3 geo = glm::cross(src.pos[ht.y] - src.pos[ht.x], src.pos[ht.z] - src.pos[ht.x]);
                        glm::vec3 nh = safeNormalize(src.nrm[ht.x] * w0 + src.nrm[ht.y] * h.u + src.nrm[ht.z] * h.v,
                                                     safeNormalize(geo, n));
                        const glm::vec3 hp = p + n * (front - h.t);
                        if (wantNormal) {
                            // The exact inverse of the shader's normalize(tn.x * T + tn.y * B + tn.z * N), with
                            // B = sign * cross(N, T) per pixel (MikkTSpace's recommended setup).
                            const glm::vec4 &t0 = out.tangents[size_t(t) * 3], &t1 = out.tangents[size_t(t) * 3 + 1],
                                            &t2 = out.tangents[size_t(t) * 3 + 2];
                            glm::vec3 T = glm::vec3(t0) * b.x + glm::vec3(t1) * b.y + glm::vec3(t2) * b.z;
                            T = safeNormalize(T - n * glm::dot(n, T), glm::vec3(1, 0, 0));
                            const float sign = t0.w < 0.0f ? -1.0f : 1.0f;
                            const glm::vec3 B = sign * glm::cross(n, T);
                            a.ts += glm::vec3(glm::dot(nh, T), glm::dot(nh, B), glm::dot(nh, n));
                        }
                        if (wantObject) a.os += nh;
                        if (wantDisp) {
                            const float d = front - h.t;
                            a.disp += d;
                            localMin = std::min(localMin, d);
                            localMax = std::max(localMax, d);
                        }
                        if (wantCavity)
                            a.cav += src.curvature[ht.x] * w0 + src.curvature[ht.y] * h.u + src.curvature[ht.z] * h.v;
                        if (wantColor) {
                            const Mesh& m = *src.meshes[src.triMesh[h.tri]];
                            const uint32_t lt = src.triLocal[h.tri];
                            glm::vec3 c(0.5f);
                            if (hasTexture(m)) {
                                const glm::vec2 uv = m.uvs[size_t(lt) * 3] * w0 + m.uvs[size_t(lt) * 3 + 1] * h.u +
                                                     m.uvs[size_t(lt) * 3 + 2] * h.v;
                                c = sampleTexture(*m.texture, uv);
                                if (m.hasColors()) {
                                    const glm::uvec3& mt = m.triangles[lt];
                                    c *= m.colors[mt.x] * w0 + m.colors[mt.y] * h.u + m.colors[mt.z] * h.v;
                                }
                            } else if (m.hasColors()) {
                                const glm::uvec3& mt = m.triangles[lt];
                                c = m.colors[mt.x] * w0 + m.colors[mt.y] * h.u + m.colors[mt.z] * h.v;
                            }
                            a.color += c;
                        }
                        if (wantAo) {
                            // Cosine-weighted hemisphere rays from the high-poly point (Hammersley points with a
                            // per-sample rotation).
                            if (glm::dot(nh, n) < 0.0f) nh = -nh; // face the side the ray came from
                            glm::vec3 bt, bb;
                            basis(nh, bt, bb);
                            const uint32_t seed = hash32(uint32_t(sx) * 73856093u ^ uint32_t(sy) * 19349663u);
                            const float r1 = float(seed & 0xFFFF) / 65536.0f, r2 = float(seed >> 16) / 65536.0f;
                            const glm::vec3 o = hp + nh * (eps * 4.0f);
                            int hits = 0;
                            for (int r = 0; r < aoPerSample; ++r) {
                                float u1 = (float(r) + 0.5f) / float(aoPerSample) + r1, u2 = radicalInverse(uint32_t(r)) + r2;
                                u1 -= std::floor(u1);
                                u2 -= std::floor(u2);
                                const float rad = std::sqrt(u1), phi = 6.2831853f * u2;
                                const glm::vec3 dir = bt * (rad * std::cos(phi)) + bb * (rad * std::sin(phi)) +
                                                      nh * std::sqrt(std::max(0.0f, 1.0f - u1));
                                if (bvh.occluded(o, dir, eps, aoDist)) ++hits;
                            }
                            a.ao += 1.0f - float(hits) / float(aoPerSample);
                        }
                        if (wantCurv) { // mean curvature at the hit, scaled by the radius: > 0 convex
                            const float hc = src.meanCurvature[ht.x] * w0 + src.meanCurvature[ht.y] * h.u +
                                             src.meanCurvature[ht.z] * h.v;
                            a.curv += std::clamp(hc * curvRadius, -1.0f, 1.0f);
                        }
                        if (wantThick) {
                            // Cosine-weighted rays into the surface: the mean distance to the far side.
                            glm::vec3 in = -nh;
                            if (glm::dot(in, n) > 0.0f) in = -in; // (nh faces outwards here)
                            glm::vec3 bt, bb;
                            basis(in, bt, bb);
                            const uint32_t seed = hash32(uint32_t(sx) * 2654435761u ^ uint32_t(sy) * 40503u);
                            const float r1 = float(seed & 0xFFFF) / 65536.0f, r2 = float(seed >> 16) / 65536.0f;
                            const glm::vec3 o = hp + in * (eps * 4.0f);
                            float sum = 0.0f;
                            for (int r = 0; r < thickPerSample; ++r) {
                                float u1 = (float(r) + 0.5f) / float(thickPerSample) + r1, u2 = radicalInverse(uint32_t(r)) + r2;
                                u1 -= std::floor(u1);
                                u2 -= std::floor(u2);
                                const float rad = std::sqrt(u1), phi = 6.2831853f * u2;
                                const glm::vec3 dir = bt * (rad * std::cos(phi)) + bb * (rad * std::sin(phi)) +
                                                      in * std::sqrt(std::max(0.0f, 1.0f - u1));
                                RayHitInfo th;
                                sum += bvh.nearest(o, dir, eps, thickDist, 0.0f, th) ? th.t : thickDist;
                            }
                            a.thick += sum / (float(thickPerSample) * thickDist);
                        }
                        if (wantPos) a.pos += (hp - all.min) * posScale;
                        if (wantId) {
                            const Mesh& m = *src.meshes[src.triMesh[h.tri]];
                            const uint32_t key = idByGroup ? m.polygroupAt(src.triLocal[h.tri]) * 2654435761u + src.triMesh[h.tri]
                                                           : src.triMesh[h.tri] * 2246822519u + 7u;
                            a.id += idMapColor(key);
                        }
                    }
                }
            }
            // Resolve the tile.
            for (int y = y0; y < y1; ++y)
                for (int x = x0; x < x1; ++x) {
                    const Acc& a = acc[size_t(y - y0) * kTile + size_t(x - x0)];
                    if (!a.samples) continue;
                    const size_t i = size_t(y) * size_t(W) + size_t(x);
                    out.coverage[i] = 1;
                    ++localIsland;
                    if (!a.hits) {
                        ++localMissed;
                        if (wantNormal) out.normal[i * 3] = out.normal[i * 3 + 1] = 128, out.normal[i * 3 + 2] = 255;
                        if (wantObject) { // the low-poly's own normal
                            const glm::vec3 on = safeNormalize(a.lowN, glm::vec3(0, 1, 0));
                            for (int c = 0; c < 3; ++c) out.objectNormal[i * 3 + size_t(c)] = unorm8(on[c] * 0.5f + 0.5f);
                        }
                        continue;
                    }
                    const float inv = 1.0f / float(a.hits);
                    if (wantNormal) {
                        glm::vec3 tn = safeNormalize(a.ts, glm::vec3(0, 0, 1));
                        if (st.flipGreen) tn.y = -tn.y;
                        for (int c = 0; c < 3; ++c) out.normal[i * 3 + size_t(c)] = unorm8(tn[c] * 0.5f + 0.5f);
                    }
                    if (wantObject) {
                        const glm::vec3 on = safeNormalize(a.os, glm::vec3(0, 1, 0));
                        for (int c = 0; c < 3; ++c) out.objectNormal[i * 3 + size_t(c)] = unorm8(on[c] * 0.5f + 0.5f);
                    }
                    if (wantDisp) out.displacement[i] = a.disp * inv;
                    if (wantAo) out.ao[i] = unorm8(a.ao * inv);
                    if (wantCavity) out.cavity[i] = unorm8(0.5f - 0.5f * a.cav * inv);
                    if (wantColor) {
                        const glm::vec3 c = linearToSrgb(glm::clamp(a.color * inv, 0.0f, 1.0f));
                        for (int k = 0; k < 3; ++k) out.color[i * 3 + size_t(k)] = unorm8(c[k]);
                    }
                    if (wantCurv) out.curvature[i] = unorm8(0.5f + 0.5f * a.curv * inv);
                    if (wantThick) out.thickness[i] = unorm8(a.thick * inv);
                    if (wantPos)
                        for (int k = 0; k < 3; ++k)
                            out.position[i * 3 + size_t(k)] = uint16_t(std::lround(std::clamp(a.pos[k] * inv, 0.0f, 1.0f) * 65535.0f));
                    if (wantId)
                        for (int k = 0; k < 3; ++k) out.id[i * 3 + size_t(k)] = unorm8(a.id[k] * inv);
                }
            const size_t d = done.fetch_add(1) + 1;
            if (worker == 0 && progress && !progress(0.95f * float(d) / float(work.size()))) cancel = true;
        }
        island += localIsland;
        missed += localMissed;
        std::lock_guard lock(rangeMutex);
        dMin = std::min(dMin, localMin);
        dMax = std::max(dMax, localMax);
    });
    // Only worker 0 reports progress; when the other workers took every tile, ask once more here.
    if (!cancel && progress && !progress(0.95f)) cancel = true;
    if (cancel) {
        error = "Cancelled";
        out = BakeResult();
        return false;
    }
    out.islandTexels = island;
    out.missedTexels = missed;
    if (wantDisp && dMin <= dMax) {
        out.displacementMin = dMin;
        out.displacementMax = dMax;
    }
    out.displacementScale = st.displacementScale > 0.0f ? st.displacementScale
                                                        : std::max({std::abs(out.displacementMin), std::abs(out.displacementMax), diag * 1e-5f});

    // --- Padding: ring by ring, each empty texel next to filled ones takes their average ---
    if (st.padding > 0) {
        std::vector<uint8_t> ring(texels, 255);
        for (size_t i = 0; i < texels; ++i)
            if (out.coverage[i]) ring[i] = 0;
        std::vector<uint32_t> front;
        std::vector<std::vector<uint32_t>> perThread(static_cast<size_t>(threads));
        for (int k = 1; k <= st.padding; ++k) {
            // 1) Which empty texels touch a filled one (read-only pass).
            std::atomic<int> rowNext{0};
            for (auto& v : perThread) v.clear();
            runWorkers(threads, [&](int w) {
                for (;;) {
                    const int y = rowNext.fetch_add(1);
                    if (y >= H) break;
                    for (int x = 0; x < W; ++x) {
                        const size_t i = size_t(y) * size_t(W) + size_t(x);
                        if (ring[i] != 255) continue;
                        bool touch = false;
                        for (int dy = -1; dy <= 1 && !touch; ++dy)
                            for (int dx = -1; dx <= 1 && !touch; ++dx) {
                                const int nx = x + dx, ny = y + dy;
                                if (nx < 0 || ny < 0 || nx >= W || ny >= H) continue;
                                touch = ring[size_t(ny) * size_t(W) + size_t(nx)] < k;
                            }
                        if (touch) perThread[size_t(w)].push_back(uint32_t(i));
                    }
                }
            });
            front.clear();
            for (auto& v : perThread) front.insert(front.end(), v.begin(), v.end());
            if (front.empty()) break;
            // 2) Fill them from texels of earlier rings only (none of which is written here).
            std::atomic<size_t> idx{0};
            runWorkers(threads, [&](int) {
                for (;;) {
                    const size_t f = idx.fetch_add(256);
                    if (f >= front.size()) break;
                    for (size_t q = f; q < std::min(front.size(), f + 256); ++q) {
                        const size_t i = front[q];
                        const int x = int(i % size_t(W)), y = int(i / size_t(W));
                        glm::vec3 ts(0), os(0), cl(0), ps(0), idc(0);
                        float dp = 0, ao = 0, cv = 0, cu = 0, th = 0;
                        int n = 0;
                        for (int dy = -1; dy <= 1; ++dy)
                            for (int dx = -1; dx <= 1; ++dx) {
                                const int nx = x + dx, ny = y + dy;
                                if (nx < 0 || ny < 0 || nx >= W || ny >= H) continue;
                                const size_t j = size_t(ny) * size_t(W) + size_t(nx);
                                if (ring[j] >= k) continue;
                                ++n;
                                if (wantNormal)
                                    ts += glm::vec3(out.normal[j * 3], out.normal[j * 3 + 1], out.normal[j * 3 + 2]) / 127.5f - 1.0f;
                                if (wantObject)
                                    os += glm::vec3(out.objectNormal[j * 3], out.objectNormal[j * 3 + 1], out.objectNormal[j * 3 + 2]) / 127.5f - 1.0f;
                                if (wantColor) cl += glm::vec3(out.color[j * 3], out.color[j * 3 + 1], out.color[j * 3 + 2]);
                                if (wantDisp) dp += out.displacement[j];
                                if (wantAo) ao += out.ao[j];
                                if (wantCavity) cv += out.cavity[j];
                                if (wantCurv) cu += out.curvature[j];
                                if (wantThick) th += out.thickness[j];
                                if (wantPos) ps += glm::vec3(out.position[j * 3], out.position[j * 3 + 1], out.position[j * 3 + 2]);
                                if (wantId) idc += glm::vec3(out.id[j * 3], out.id[j * 3 + 1], out.id[j * 3 + 2]);
                            }
                        if (!n) continue;
                        const float inv = 1.0f / float(n);
                        if (wantNormal) {
                            const glm::vec3 v = safeNormalize(ts, glm::vec3(0, 0, 1));
                            for (int c = 0; c < 3; ++c) out.normal[i * 3 + size_t(c)] = unorm8(v[c] * 0.5f + 0.5f);
                        }
                        if (wantObject) {
                            const glm::vec3 v = safeNormalize(os, glm::vec3(0, 1, 0));
                            for (int c = 0; c < 3; ++c) out.objectNormal[i * 3 + size_t(c)] = unorm8(v[c] * 0.5f + 0.5f);
                        }
                        if (wantColor)
                            for (int c = 0; c < 3; ++c) out.color[i * 3 + size_t(c)] = uint8_t(std::lround(cl[c] * inv));
                        if (wantDisp) out.displacement[i] = dp * inv;
                        if (wantAo) out.ao[i] = uint8_t(std::lround(ao * inv));
                        if (wantCavity) out.cavity[i] = uint8_t(std::lround(cv * inv));
                        if (wantCurv) out.curvature[i] = uint8_t(std::lround(cu * inv));
                        if (wantThick) out.thickness[i] = uint8_t(std::lround(th * inv));
                        for (int c = 0; c < 3; ++c) {
                            if (wantPos) out.position[i * 3 + size_t(c)] = uint16_t(std::lround(ps[c] * inv));
                            if (wantId) out.id[i * 3 + size_t(c)] = uint8_t(std::lround(idc[c] * inv));
                        }
                    }
                }
            });
            for (uint32_t i : front) {
                ring[i] = uint8_t(k);
                out.coverage[i] = 2;
            }
        }
    }
    // Texels nobody reached: neutral values.
    for (size_t i = 0; i < texels; ++i) {
        if (out.coverage[i]) continue;
        if (wantNormal) out.normal[i * 3] = out.normal[i * 3 + 1] = 128, out.normal[i * 3 + 2] = 255;
        if (wantObject) out.objectNormal[i * 3] = out.objectNormal[i * 3 + 1] = out.objectNormal[i * 3 + 2] = 128;
    }
    if (progress) progress(1.0f);
    out.seconds = secondsSince(start);
    return true;
}

// ---------------------------------------------------------------------------
// UDIM tiles and level sources
// ---------------------------------------------------------------------------

std::vector<int> udimTiles(const Mesh& low) {
    std::vector<int> tiles;
    if (low.uvs.size() != low.triangleCount() * 3) return tiles;
    bool outside = false;
    std::vector<uint8_t> seen(100, 0); // u 0..9, v 0..9
    for (size_t t = 0; t < low.triangleCount(); ++t) {
        const glm::vec2 c = (low.uvs[t * 3] + low.uvs[t * 3 + 1] + low.uvs[t * 3 + 2]) / 3.0f;
        if (!std::isfinite(c.x) || !std::isfinite(c.y)) continue;
        const int u = int(std::floor(c.x)), v = int(std::floor(c.y));
        if (u != 0 || v != 0) outside = true;
        if (u < 0 || u > 9 || v < 0 || v > 9) continue; // (UDIM covers 10 columns; others are skipped)
        seen[size_t(v * 10 + u)] = 1;
    }
    if (!outside) return tiles;
    for (int i = 0; i < 100; ++i)
        if (seen[size_t(i)]) tiles.push_back(1001 + i);
    return tiles;
}

bool bakeMapsUdim(const Mesh& low, const std::vector<const Mesh*>& sources, const BakeSettings& settings,
                  std::vector<BakeResult>& results, std::string& error, const BakeProgress& progress) {
    results.clear();
    const std::vector<int> tiles = udimTiles(low);
    if (tiles.empty()) {
        results.emplace_back();
        return bakeMaps(low, sources, settings, results.back(), error, progress);
    }
    // Per tile, a copy of the low-poly whose UVs in that tile move to 0..1 and whose other triangles move far
    // outside (their samples are skipped).
    Mesh shifted = low;
    shifted.texture.reset();
    for (size_t k = 0; k < tiles.size(); ++k) {
        const int u = (tiles[k] - 1001) % 10, v = (tiles[k] - 1001) / 10;
        for (size_t t = 0; t < low.triangleCount(); ++t) {
            const glm::vec2 c = (low.uvs[t * 3] + low.uvs[t * 3 + 1] + low.uvs[t * 3 + 2]) / 3.0f;
            const bool in = int(std::floor(c.x)) == u && int(std::floor(c.y)) == v;
            for (size_t i = 0; i < 3; ++i)
                shifted.uvs[t * 3 + i] = in ? low.uvs[t * 3 + i] - glm::vec2(float(u), float(v)) : glm::vec2(-8.0f);
        }
        BakeResult r;
        const float lo = float(k) / float(tiles.size()), span = 1.0f / float(tiles.size());
        const BakeProgress tileProgress = [&](float f) { return !progress || progress(lo + f * span); };
        if (!bakeMaps(shifted, sources, settings, r, error, tileProgress)) return false;
        r.udim = tiles[k];
        results.push_back(std::move(r));
    }
    return true;
}

bool bakeLevelSources(const Mesh& part, const std::string& source, Mesh& low, Mesh& high, std::string& error) {
    if (source == "subdivision" || source == "levels" || source == "subdiv") {
        const int levels = subdivLevelCount(part);
        if (levels < 2) {
            error = "The Part has no subdivision levels (Tool > Geometry > Divide)";
            return false;
        }
        low = subdivLevelMesh(part, 0);
        high = subdivLevelMesh(part, levels - 1);
    } else if (source == "micro") {
        if (!hasMicro(part)) {
            error = "The Part has no micro level (Tool > Geometry > Micro)";
            return false;
        }
        low = part;
        low.micro.reset();
        if (!microBakeMesh(part, microLevel(part), high, error)) return false;
    } else {
        error = "Unknown level source '" + source + "' (use subdivision or micro)";
        return false;
    }
    low.texture.reset();
    high.texture = part.texture; // (colour comes from the Part's own texture)
    if (low.uvs.size() != low.triangleCount() * 3) {
        error = "The Part's lowest level has no UVs: run Auto UV on it first (Tool > Texture Map)";
        return false;
    }
    if (low.normals.size() != low.vertexCount()) low.recomputeAllNormals();
    if (high.normals.size() != high.vertexCount()) high.recomputeAllNormals();
    return true;
}

// ---------------------------------------------------------------------------
// Images and files
// ---------------------------------------------------------------------------

std::vector<uint16_t> displacement16(const BakeResult& r) {
    std::vector<uint16_t> px;
    if (!r.has(BakeMap::Displacement) || r.displacement.size() != size_t(r.width) * size_t(r.height)) return px;
    px.resize(r.displacement.size());
    const float k = r.displacementScale > 0.0f ? 0.5f / r.displacementScale : 0.0f;
    for (int y = 0; y < r.height; ++y)
        for (int x = 0; x < r.width; ++x) {
            const float v = std::clamp(0.5f + r.displacement[size_t(y) * size_t(r.width) + size_t(x)] * k, 0.0f, 1.0f);
            px[size_t(r.height - 1 - y) * size_t(r.width) + size_t(x)] = uint16_t(std::lround(v * 65535.0f));
        }
    return px;
}

Image bakeMapImage(const BakeResult& r, BakeMap map) {
    Image img;
    if (!r.has(map) || r.width <= 0) return img;
    img.width = r.width;
    img.height = r.height;
    img.rgba.assign(size_t(r.width) * size_t(r.height) * 4, 255);
    const std::vector<uint8_t>* rgb = map == BakeMap::Normal ? &r.normal : map == BakeMap::ObjectNormal ? &r.objectNormal
                                    : map == BakeMap::Color ? &r.color : map == BakeMap::Id ? &r.id : nullptr;
    const std::vector<uint8_t>* grey = map == BakeMap::AmbientOcclusion ? &r.ao : map == BakeMap::Cavity ? &r.cavity
                                     : map == BakeMap::Curvature ? &r.curvature : map == BakeMap::Thickness ? &r.thickness : nullptr;
    const float k = r.displacementScale > 0.0f ? 0.5f / r.displacementScale : 0.0f;
    for (int y = 0; y < r.height; ++y)
        for (int x = 0; x < r.width; ++x) {
            const size_t i = size_t(y) * size_t(r.width) + size_t(x);
            uint8_t* p = &img.rgba[(size_t(r.height - 1 - y) * size_t(r.width) + size_t(x)) * 4];
            if (rgb) {
                p[0] = (*rgb)[i * 3], p[1] = (*rgb)[i * 3 + 1], p[2] = (*rgb)[i * 3 + 2];
            } else if (grey) {
                p[0] = p[1] = p[2] = (*grey)[i];
            } else if (map == BakeMap::Position) {
                for (int c = 0; c < 3; ++c) p[c] = uint8_t(r.position[i * 3 + size_t(c)] >> 8);
            } else {
                p[0] = p[1] = p[2] = unorm8(0.5f + r.displacement[i] * k);
            }
        }
    return img;
}

std::string bakeFileName(const std::string& base, BakeMap map, const char* ext, int udim) {
    const char* suffix = map == BakeMap::ObjectNormal ? "normal_object" : bakeMapId(map);
    return base + "_" + suffix + (udim > 0 ? "." + std::to_string(udim) : std::string()) + ext;
}

bool saveBakeMaps(const BakeResult& r, const std::string& folder, const std::string& base, bool exr,
                  std::vector<std::string>& written, std::string& error) {
    written.clear();
    std::error_code ec;
    const std::filesystem::path dir = fsPath(folder.empty() ? std::string(".") : folder);
    std::filesystem::create_directories(dir, ec);
    for (int i = 0; i < kBakeMapCount; ++i) {
        const BakeMap m = BakeMap(i);
        if (!r.has(m)) continue;
        const std::string path = utf8(dir / fsPath(bakeFileName(base, m, ".png", r.udim)));
        if (m == BakeMap::Position) { // 16-bit RGB, top-down
            std::vector<uint16_t> px(r.position.size());
            for (int y = 0; y < r.height; ++y)
                std::memcpy(&px[size_t(r.height - 1 - y) * size_t(r.width) * 3], &r.position[size_t(y) * size_t(r.width) * 3],
                            size_t(r.width) * 3 * sizeof(uint16_t));
            if (!savePng16(path, r.width, r.height, 3, px, error)) return false;
            written.push_back(path);
            continue;
        }
        if (m == BakeMap::Displacement) {
            if (!savePng16(path, r.width, r.height, 1, displacement16(r), error)) return false;
            written.push_back(path);
            if (exr) {
                std::vector<float> f(r.displacement.size());
                for (int y = 0; y < r.height; ++y)
                    std::memcpy(&f[size_t(r.height - 1 - y) * size_t(r.width)], &r.displacement[size_t(y) * size_t(r.width)],
                                size_t(r.width) * sizeof(float));
                const std::string exrPath = utf8(dir / fsPath(bakeFileName(base, m, ".exr", r.udim)));
                if (!saveExr(exrPath, r.width, r.height, 1, f, error)) return false;
                written.push_back(exrPath);
            }
            continue;
        }
        if (!bakeMapImage(r, m).savePng(path, error)) return false;
        written.push_back(path);
    }
    return true;
}

std::string bakeSummary(const BakeResult& r) {
    std::ostringstream s;
    s.setf(std::ios::fixed);
    s.precision(1);
    std::string names;
    for (int i = 0; i < kBakeMapCount; ++i)
        if (r.has(BakeMap(i))) names += std::string(names.empty() ? "" : ", ") + bakeMapId(BakeMap(i));
    s << "Baked " << names << " at " << r.width << "x" << r.height;
    if (r.udim > 0) s << " (UDIM " << r.udim << ")";
    s << " from " << r.sourceTriangles << " triangles in " << r.seconds << " s";
    s.precision(4);
    s << "; rays " << r.front << " in front, " << r.rear << " behind";
    if (r.has(BakeMap::Displacement))
        s << "; displacement " << r.displacementMin << " to " << r.displacementMax << ", PNG mid-grey = 0, white = +"
          << r.displacementScale << ", black = -" << r.displacementScale;
    if (r.missRatio() >= 0.0005f) {
        s.precision(1);
        s << "; " << 100.0f * r.missRatio() << "% of texels found no surface (raise Front / Rear)";
    }
    return s.str();
}

bool bakeSourceSubtools(const Document& doc, const std::string& source, std::vector<int>& indices, std::string& error) {
    indices.clear();
    if (source.empty() || source == "all" || source == "visible") {
        for (int i = 0; i < doc.subtoolCount(); ++i)
            if (i != doc.activeSubtool && doc.subtools[size_t(i)].visible && doc.subtoolMesh(i).triangleCount())
                indices.push_back(i);
        if (indices.empty()) error = "No other visible Part to bake from: show the high-poly Part(s), or append one";
        return !indices.empty();
    }
    const int i = doc.findSubtool(source);
    if (i < 0) {
        error = "No Part called '" + source + "'";
        return false;
    }
    if (i == doc.activeSubtool) {
        error = "'" + source + "' is the active Part (the low-poly the maps go onto): pick another source";
        return false;
    }
    indices.push_back(i);
    return true;
}

std::string defaultBakeFolder(const Document& doc) {
    if (!doc.path.empty()) {
        const std::filesystem::path p = fsPath(doc.path).parent_path();
        if (!p.empty()) return utf8(p);
    }
    return ".";
}

void setBakeListener(BakeListener listener) {
    std::lock_guard lock(g_listenerMutex);
    g_listener = std::move(listener);
}

void notifyBakeListener(const Document& doc, std::shared_ptr<const BakeResult> result, const std::vector<std::string>& files) {
    BakeListener l;
    {
        std::lock_guard lock(g_listenerMutex);
        l = g_listener;
    }
    if (l) l(doc, std::move(result), files);
}

// ---------------------------------------------------------------------------
// Command
// ---------------------------------------------------------------------------

void registerBakeCommands(CommandRegistry& reg) {
    using T = ParamSpec::Type;
    reg.add({"bake",
             "Bake maps from a high-poly source onto the active Part's UVs (the low-poly): tangent-space normal "
             "(MikkTSpace), object-space (world) normal, displacement (16-bit PNG + 32-bit EXR), ambient occlusion, cavity, "
             "colour, curvature, thickness, position (16-bit) and ID (polygroups or Parts). Rays go along the low-poly's "
             "smooth normals from 'front' in front of it to 'rear' behind it. The source can be other Parts, or the "
             "active Part's own top subdivision level (baked onto its lowest level) or micro level (onto its base). "
             "UVs outside 0..1 bake as UDIM tiles. Writes <name>_<map>.png (<name>_<map>.1001.png per UDIM tile) into "
             "the output folder.",
             {{"source", T::String, "A Part to bake from, all (default: every visible Part but the active one), subdivision "
                                    "(the active Part's top level onto its lowest) or micro (its micro level onto its base).", false},
              {"maps", T::String, "Comma list: normal, object_normal (world_normal), displacement, ao, cavity, color, curvature, "
                                  "thickness, position, id, or all. Default normal.", false},
              {"size", T::Number, "Map size in texels: 512, 1024, 2048 (default), 4096 or 8192 (1k...8k also work).", false},
              {"out", T::String, "Output folder (default: the project's folder, or the current folder).", false},
              {"name", T::String, "File name prefix (default: the active Part's name).", false},
              {"front", T::String, "Ray start in front of the low-poly, as a fraction of its size (e.g. 0.02), or auto (default).", false},
              {"rear", T::String, "How far behind the low-poly rays look, as a fraction of its size, or auto (default).", false},
              {"samples", T::Number, "Anti-aliasing: samples x samples rays per texel, 1-4. Default 2.", false},
              {"padding", T::Number, "Texels filled around each UV island, 0-64. Default 8.", false},
              {"ao_rays", T::Number, "Ambient occlusion rays per texel. Default 48.", false},
              {"ao_distance", T::Number, "How far occluders count, as a fraction of the size. Default 0.05.", false},
              {"green", T::String, "Normal map convention: opengl (default; Blender, Unity, Godot) or directx (Unreal).", false},
              {"displacement_scale", T::Number, "World units at white in the displacement PNG (default: the largest distance).", false},
              {"exr", T::Bool, "Also write the displacement as a 32-bit float EXR in world units. Default true.", false},
              {"thickness_rays", T::Number, "Thickness rays per texel. Default 24.", false},
              {"thickness_distance", T::Number, "Rays longer than this (a fraction of the size) count as thick. Default 0.1.", false},
              {"curvature_radius", T::Number, "A bend of this radius (a fraction of the size) is fully bright or dark. Default 0.02.", false},
              {"id", T::String, "What the ID map colours: polygroups (default) or parts.", false},
              {"udim", T::Bool, "Bake UVs outside 0..1 as UDIM tiles (default true; false bakes the 0..1 square only).", false}},
             [](Document& doc, const Args& a) -> CommandResult {
                 BakeSettings st;
                 std::string err;
                 if (!parseBakeMaps(argString(a, "maps", "normal"), st.maps, err)) return {false, err};
                 if (a.contains("size")) {
                     std::string text = argString(a, "size", "");
                     if (text.empty()) text = std::to_string(int(argNumber(a, "size", 2048)));
                     for (char& c : text) c = char(std::tolower(static_cast<unsigned char>(c)));
                     int n = 0;
                     try {
                         n = !text.empty() && text.back() == 'k' ? std::stoi(text.substr(0, text.size() - 1)) * 1024 : std::stoi(text);
                     } catch (...) {
                         n = 0;
                     }
                     if (n != 256 && n != 512 && n != 1024 && n != 2048 && n != 4096 && n != 8192)
                         return {false, "size must be 256, 512, 1024, 2048, 4096 or 8192"};
                     st.size = n;
                 }
                 auto distance = [&](const char* key, float& value) -> bool {
                     if (!a.contains(key)) return true;
                     std::string s = argString(a, key, "");
                     if (s == "auto") return true;
                     const double v = s.empty() ? argNumber(a, key, -1) : std::atof(s.c_str());
                     if (!(v > 0.0 && v <= 1.0)) return false;
                     value = float(v);
                     st.autoDistance = false;
                     return true;
                 };
                 if (!distance("front", st.front) || !distance("rear", st.rear))
                     return {false, "front and rear are fractions of the model's size between 0 and 1 (e.g. 0.02), or auto"};
                 if (!st.autoDistance) { // one given, the other auto: keep the given one and default the other
                     if (!a.contains("front") || argString(a, "front", "") == "auto") st.front = 0.02f;
                     if (!a.contains("rear") || argString(a, "rear", "") == "auto") st.rear = 0.02f;
                 }
                 st.samples = int(argNumber(a, "samples", st.samples));
                 st.padding = int(argNumber(a, "padding", st.padding));
                 st.aoRays = int(argNumber(a, "ao_rays", st.aoRays));
                 st.aoDistance = float(argNumber(a, "ao_distance", st.aoDistance));
                 st.displacementScale = float(argNumber(a, "displacement_scale", 0.0));
                 const std::string green = argString(a, "green", "opengl");
                 if (green != "opengl" && green != "directx") return {false, "green must be opengl or directx"};
                 st.flipGreen = green == "directx";
                 st.thicknessRays = int(argNumber(a, "thickness_rays", st.thicknessRays));
                 st.thicknessDistance = float(argNumber(a, "thickness_distance", st.thicknessDistance));
                 st.curvatureRadius = float(argNumber(a, "curvature_radius", st.curvatureRadius));
                 const std::string id = argString(a, "id", "polygroups");
                 if (id != "polygroups" && id != "parts") return {false, "id must be polygroups or parts"};
                 st.idSource = id == "parts" ? BakeSettings::IdSource::Parts : BakeSettings::IdSource::Polygroups;

                 // The meshes: other Parts onto the active one, or the active Part's own levels.
                 const std::string sourceArg = argString(a, "source", "all");
                 Mesh levelLow, levelHigh;
                 const Mesh* low = &doc.mesh;
                 std::vector<const Mesh*> sources;
                 if (sourceArg == "subdivision" || sourceArg == "micro") {
                     if (!bakeLevelSources(doc.mesh, sourceArg, levelLow, levelHigh, err)) return {false, err};
                     low = &levelLow;
                     sources.push_back(&levelHigh);
                 } else {
                     std::vector<int> idx;
                     if (!bakeSourceSubtools(doc, sourceArg, idx, err)) return {false, err};
                     for (int i : idx) sources.push_back(&doc.subtoolMesh(i));
                 }
                 std::vector<BakeResult> results;
                 if (argBool(a, "udim", true)) {
                     if (!bakeMapsUdim(*low, sources, st, results, err)) return {false, err};
                 } else {
                     results.emplace_back();
                     if (!bakeMaps(*low, sources, st, results.back(), err)) return {false, err};
                 }
                 auto result = std::make_shared<BakeResult>(results.front());
                 std::string name = argString(a, "name", doc.activeSubtoolInfo().name);
                 for (char& c : name)
                     if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|')
                         c = '_';
                 if (name.empty()) name = "bake";
                 const std::string folder = argString(a, "out", defaultBakeFolder(doc));
                 std::vector<std::string> written, all;
                 std::string summary;
                 for (const BakeResult& r : results) {
                     if (!saveBakeMaps(r, folder, name, argBool(a, "exr", true), written, err)) return {false, err};
                     all.insert(all.end(), written.begin(), written.end());
                     summary += (summary.empty() ? "" : "\n") + bakeSummary(r);
                 }
                 // (the preview shows the bake on the active Part, which is the low-poly only for Part sources)
                 if (low == &doc.mesh && results.size() == 1) notifyBakeListener(doc, result, all);
                 std::string files;
                 for (const std::string& w : all) files += "\n  " + w;
                 return {true, summary + "\nWrote:" + files};
             }});
}

} // namespace sf
