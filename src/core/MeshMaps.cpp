// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/MeshMaps.h"

#include "core/Commands.h"
#include "core/Document.h"
#include "core/RayBvh.h"
#include "core/Texture.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <mutex>
#include <sstream>
#include <thread>

namespace sf {

namespace {

using Clock = std::chrono::steady_clock;

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
inline void basis(const glm::vec3& n, glm::vec3& t, glm::vec3& b) {
    const float sign = std::copysign(1.0f, n.z);
    const float a = -1.0f / (sign + n.z);
    const float bb = n.x * n.y * a;
    t = glm::vec3(1.0f + sign * n.x * n.x * a, sign * bb, -sign * n.x);
    b = glm::vec3(bb, sign + n.y * n.y * a, -n.y);
}
inline glm::vec3 cosineDir(const glm::vec3& n, const glm::vec3& t, const glm::vec3& b, float u1, float u2) {
    const float r = std::sqrt(u1), phi = 6.2831853f * u2;
    return t * (r * std::cos(phi)) + b * (r * std::sin(phi)) + n * std::sqrt(std::max(0.0f, 1.0f - u1));
}

template <class Fn>
void runWorkers(Fn fn) {
    const int threads = int(std::clamp(std::thread::hardware_concurrency(), 1u, 32u));
    std::vector<std::thread> pool;
    for (int i = 1; i < threads; ++i) pool.emplace_back([&fn, i] { fn(i); });
    fn(0);
    for (auto& t : pool) t.join();
}

struct Format {
    int components, bits;
    std::array<float, 4> fill;
};
Format formatOf(MeshMapKind k) {
    switch (k) {
    case MeshMapKind::AmbientOcclusion: return {1, 8, {1, 0, 0, 0}};
    case MeshMapKind::Curvature: return {1, 8, {0.5f, 0, 0, 0}};
    case MeshMapKind::Thickness: return {1, 8, {1, 0, 0, 0}};
    case MeshMapKind::Position: return {3, 16, {0, 0, 0, 0}};
    case MeshMapKind::WorldNormal: return {3, 8, {0.5f, 1.0f, 0.5f, 0}};
    case MeshMapKind::Id: return {3, 8, {0, 0, 0, 0}};
    }
    return {1, 8, {0, 0, 0, 0}};
}

// What baking needs of a mesh (the whole Part, or the compact copy of an update) and its rays.
struct Baker {
    const Mesh& mesh;
    RayBvh bvh;
    std::vector<float> curvature;
    uint32_t kinds = 0;
    MeshMapSettings st;
    float aoDist = 0, thickDist = 0, curvRadius = 0, eps = 0;
    glm::vec3 boxMin{0.0f}, boxScale{1.0f};

    Baker(const Mesh& m, uint32_t k, const MeshMapSettings& s, float diagonal, const glm::vec3& bmin, const glm::vec3& bmax)
        : mesh(m), kinds(k), st(s) {
        const bool rays = kinds & (meshMapBit(MeshMapKind::AmbientOcclusion) | meshMapBit(MeshMapKind::Thickness));
        if (rays) bvh.build(m.positions, m.triangles);
        if (kinds & meshMapBit(MeshMapKind::Curvature)) curvature = meshMeanCurvature(m);
        aoDist = std::max(s.aoDistance, 1e-4f) * diagonal;
        thickDist = std::max(s.thicknessDistance, 1e-4f) * diagonal;
        curvRadius = std::max(s.curvatureRadius, 1e-5f) * diagonal;
        eps = diagonal * 2e-6f;
        boxMin = bmin;
        boxScale = 1.0f / glm::max(bmax - bmin, glm::vec3(1e-12f));
    }

    // Texels of tile `ti` (origin x0, y0) whose texel map names a triangle, into the writable tiles `out`.
    size_t bakeTile(const TexelTile& tt, int x0, int y0, int width, int height, const std::array<uint8_t*, kMeshMapCount>& out) const {
        size_t n = 0;
        for (int ly = 0; ly < kTextureTile; ++ly) {
            const int y = y0 + ly;
            if (y >= height) break;
            for (int lx = 0; lx < kTextureTile; ++lx) {
                const int x = x0 + lx;
                if (x >= width) break;
                const size_t li = size_t(ly) * kTextureTile + size_t(lx);
                const uint32_t t = tt.tri[li];
                if (t == kNoTexelTriangle) continue;
                ++n;
                const glm::vec2 b12 = tt.bary[li];
                const float b0 = 1.0f - b12.x - b12.y;
                const glm::uvec3& tv = mesh.triangles[t];
                const glm::vec3 p = mesh.positions[tv.x] * b0 + mesh.positions[tv.y] * b12.x + mesh.positions[tv.z] * b12.y;
                const glm::vec3 face = glm::cross(mesh.positions[tv.y] - mesh.positions[tv.x], mesh.positions[tv.z] - mesh.positions[tv.x]);
                glm::vec3 nrm = mesh.normals.size() == mesh.vertexCount()
                                    ? mesh.normals[tv.x] * b0 + mesh.normals[tv.y] * b12.x + mesh.normals[tv.z] * b12.y
                                    : face;
                const float len = glm::length(nrm);
                nrm = len > 1e-12f ? nrm / len : (glm::length(face) > 0 ? glm::normalize(face) : glm::vec3(0, 1, 0));
                const uint32_t seed = hash32(uint32_t(x) * 73856093u ^ uint32_t(y) * 19349663u);
                const float r1 = float(seed & 0xFFFF) / 65536.0f, r2 = float(seed >> 16) / 65536.0f;
                auto put8 = [&](MeshMapKind k, int c, float v) {
                    out[size_t(k)][li * size_t(formatOf(k).components) + size_t(c)] = uint8_t(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f));
                };
                if (out[size_t(MeshMapKind::AmbientOcclusion)]) {
                    glm::vec3 bt, bb;
                    basis(nrm, bt, bb);
                    const glm::vec3 o = p + nrm * (eps * 4.0f);
                    const int rays = std::max(1, st.aoRays);
                    int hits = 0;
                    for (int r = 0; r < rays; ++r) {
                        float u1 = (float(r) + 0.5f) / float(rays) + r1, u2 = radicalInverse(uint32_t(r)) + r2;
                        u1 -= std::floor(u1);
                        u2 -= std::floor(u2);
                        if (bvh.occluded(o, cosineDir(nrm, bt, bb, u1, u2), eps, aoDist)) ++hits;
                    }
                    put8(MeshMapKind::AmbientOcclusion, 0, 1.0f - float(hits) / float(rays));
                }
                if (out[size_t(MeshMapKind::Thickness)]) {
                    const glm::vec3 in = -nrm;
                    glm::vec3 bt, bb;
                    basis(in, bt, bb);
                    const glm::vec3 o = p + in * (eps * 4.0f);
                    const int rays = std::max(1, st.thicknessRays);
                    float sum = 0.0f;
                    for (int r = 0; r < rays; ++r) {
                        float u1 = (float(r) + 0.5f) / float(rays) + r2, u2 = radicalInverse(uint32_t(r)) + r1;
                        u1 -= std::floor(u1);
                        u2 -= std::floor(u2);
                        RayHitInfo h;
                        sum += bvh.nearest(o, cosineDir(in, bt, bb, u1, u2), eps, thickDist, 0.0f, h) ? h.t : thickDist;
                    }
                    put8(MeshMapKind::Thickness, 0, sum / (float(rays) * thickDist));
                }
                if (out[size_t(MeshMapKind::Curvature)]) {
                    const float h = curvature[tv.x] * b0 + curvature[tv.y] * b12.x + curvature[tv.z] * b12.y;
                    put8(MeshMapKind::Curvature, 0, 0.5f + 0.5f * std::clamp(h * curvRadius, -1.0f, 1.0f));
                }
                if (uint8_t* o = out[size_t(MeshMapKind::Position)]) {
                    const glm::vec3 q = glm::clamp((p - boxMin) * boxScale, 0.0f, 1.0f);
                    uint16_t v[3];
                    for (int c = 0; c < 3; ++c) v[c] = uint16_t(std::lround(q[c] * 65535.0f));
                    std::memcpy(o + li * 6, v, 6);
                }
                if (out[size_t(MeshMapKind::WorldNormal)])
                    for (int c = 0; c < 3; ++c) put8(MeshMapKind::WorldNormal, c, nrm[c] * 0.5f + 0.5f);
                if (out[size_t(MeshMapKind::Id)]) {
                    const glm::vec3 col = idMapColor(mesh.polygroupAt(t) * 2654435761u + 1u);
                    for (int c = 0; c < 3; ++c) put8(MeshMapKind::Id, c, col[c]);
                }
            }
        }
        return n;
    }
};

void resetImage(TileImage& img, MeshMapKind k, int w, int h) {
    const Format f = formatOf(k);
    img.reset(w, h, f.components, f.bits, TileImage::toUnits(f.bits, f.fill[0], f.fill[1], f.fill[2], f.fill[3]));
}

} // namespace

// ---------------------------------------------------------------------------
// Names
// ---------------------------------------------------------------------------

const char* meshMapId(MeshMapKind k) {
    switch (k) {
    case MeshMapKind::AmbientOcclusion: return "ao";
    case MeshMapKind::Curvature: return "curvature";
    case MeshMapKind::Thickness: return "thickness";
    case MeshMapKind::Position: return "position";
    case MeshMapKind::WorldNormal: return "world_normal";
    case MeshMapKind::Id: return "id";
    }
    return "";
}

const char* meshMapLabel(MeshMapKind k) {
    switch (k) {
    case MeshMapKind::AmbientOcclusion: return "Ambient Occlusion";
    case MeshMapKind::Curvature: return "Curvature";
    case MeshMapKind::Thickness: return "Thickness";
    case MeshMapKind::Position: return "Position";
    case MeshMapKind::WorldNormal: return "World Normal";
    case MeshMapKind::Id: return "ID";
    }
    return "";
}

bool parseMeshMap(const std::string& text, MeshMapKind& out) {
    std::string s;
    for (char c : text)
        if (!std::isspace(static_cast<unsigned char>(c))) s += char(std::tolower(static_cast<unsigned char>(c)));
    if (s == "occlusion" || s == "ambient_occlusion") s = "ao";
    if (s == "normal" || s == "worldnormal" || s == "object_normal") s = "world_normal";
    if (s == "polygroups" || s == "polygroup") s = "id";
    for (int i = 0; i < kMeshMapCount; ++i)
        if (s == meshMapId(MeshMapKind(i))) {
            out = MeshMapKind(i);
            return true;
        }
    return false;
}

bool parseMeshMaps(const std::string& list, uint32_t& kinds, std::string& error) {
    kinds = 0;
    std::stringstream ss(list);
    std::string item;
    while (std::getline(ss, item, ',')) {
        if (item.find_first_not_of(" \t") == std::string::npos) continue;
        if (item == "all") {
            kinds = kAllMeshMaps;
            continue;
        }
        MeshMapKind k;
        if (!parseMeshMap(item, k)) {
            error = "Unknown mesh map '" + item + "' (use ao, curvature, thickness, position, world_normal, id or all)";
            return false;
        }
        kinds |= meshMapBit(k);
    }
    if (!kinds) {
        error = "Choose at least one mesh map";
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Values
// ---------------------------------------------------------------------------

bool MeshMapSet::matches(const Mesh& m, int w, int h) const {
    return w == width && h == height && kinds && vertexCount == m.vertexCount() && triangleCount == m.triangleCount() &&
           uvKey == meshUvFingerprint(m);
}

size_t MeshMapSet::bytes() const {
    size_t n = sizeof(MeshMapSet);
    for (const TileImage& img : images) n += img.bytes();
    if (positions) n += positions->size() * sizeof(glm::vec3);
    return n;
}

float MeshMapSet::value(MeshMapKind k, int x, int y) const {
    const TileImage& img = images[size_t(k)];
    if (!img.valid()) return formatOf(k).fill[0];
    return img.value(std::clamp(x, 0, width - 1), std::clamp(y, 0, height - 1), 0);
}

glm::vec3 MeshMapSet::vector(MeshMapKind k, int x, int y) const {
    const TileImage& img = images[size_t(k)];
    float v[4] = {formatOf(k).fill[0], formatOf(k).fill[1], formatOf(k).fill[2], 0};
    if (img.valid()) img.values(std::clamp(x, 0, width - 1), std::clamp(y, 0, height - 1), v);
    glm::vec3 r(v[0], v[1], v[2]);
    if (k == MeshMapKind::WorldNormal) {
        r = r * 2.0f - 1.0f;
        const float l = glm::length(r);
        r = l > 1e-6f ? r / l : glm::vec3(0, 1, 0);
    }
    return r;
}

std::vector<float> meshMeanCurvature(const Mesh& m) {
    const size_t nv = m.vertexCount();
    std::vector<glm::vec3> sum(nv, glm::vec3(0.0f));
    std::vector<float> sq(nv, 0.0f), count(nv, 0.0f);
    for (const glm::uvec3& t : m.triangles)
        for (int e = 0; e < 3; ++e) { // each directed edge of the triangle, both ways
            const uint32_t a = t[e], b = t[(e + 1) % 3];
            const glm::vec3 d = m.positions[b] - m.positions[a];
            const float l2 = glm::dot(d, d);
            sum[a] += d, sq[a] += l2, count[a] += 1.0f;
            sum[b] -= d, sq[b] += l2, count[b] += 1.0f;
        }
    std::vector<float> h(nv, 0.0f);
    const bool normals = m.normals.size() == nv;
    for (size_t v = 0; v < nv; ++v) {
        if (count[v] <= 0.0f || sq[v] <= 0.0f || !normals) continue;
        // centroid - p = sum / count; mean squared edge = sq / count; H = -2 (centroid - p) . n / mean squared edge.
        h[v] = -2.0f * glm::dot(sum[v], m.normals[v]) / sq[v];
    }
    // One smoothing pass over the ring (as triangles see it).
    std::vector<float> acc(nv, 0.0f), w(nv, 0.0f);
    for (const glm::uvec3& t : m.triangles) {
        const float avg = (h[t.x] + h[t.y] + h[t.z]) / 3.0f;
        for (int c = 0; c < 3; ++c) acc[t[c]] += avg, w[t[c]] += 1.0f;
    }
    for (size_t v = 0; v < nv; ++v)
        if (w[v] > 0.0f) h[v] = 0.5f * h[v] + 0.5f * acc[v] / w[v];
    return h;
}

glm::vec3 idMapColor(uint32_t key) {
    const uint32_t h = hash32(key ^ 0x9E3779B9u);
    const float hue = float(h & 0xFFFF) / 65536.0f;
    const float sat = 0.55f + 0.35f * float((h >> 16) & 0xFF) / 255.0f;
    const float val = 0.75f + 0.25f * float((h >> 24) & 0xFF) / 255.0f;
    const glm::vec3 rgb = glm::clamp(glm::abs(glm::fract(hue + glm::vec3(0.0f, 2.0f / 3.0f, 1.0f / 3.0f)) * 6.0f - 3.0f) - 1.0f, 0.0f, 1.0f);
    return val * glm::mix(glm::vec3(1.0f), rgb, sat);
}

// ---------------------------------------------------------------------------
// Full bake
// ---------------------------------------------------------------------------

bool bakeMeshMaps(const Mesh& mesh, int width, int height, uint32_t kinds, const MeshMapSettings& settings, MeshMapSet& out,
                  std::string& error, const BakeProgress& progress) {
    const auto start = Clock::now();
    if (mesh.triangleCount() == 0 || mesh.uvs.size() != mesh.triangleCount() * 3) {
        error = "The Part has no UVs: run Auto UV (Tool > Texture Map) first";
        return false;
    }
    if (width < 64 || height < 64 || width > 16384 || height > 16384) {
        error = "Mesh Maps need a texture of 64 to 16384 texels";
        return false;
    }
    kinds &= kAllMeshMaps;
    if (!kinds) {
        error = "Choose at least one mesh map";
        return false;
    }
    MeshMapSet set;
    set.width = width;
    set.height = height;
    set.kinds = kinds;
    set.settings = settings;
    const AABB box = mesh.bounds();
    set.boundsMin = box.min;
    set.boundsMax = box.max;
    set.diagonal = std::max(glm::length(box.extent()), 1e-6f);
    set.uvKey = meshUvFingerprint(mesh);
    set.vertexCount = mesh.vertexCount();
    set.triangleCount = mesh.triangleCount();
    set.positions = std::make_shared<const std::vector<glm::vec3>>(mesh.positions);
    for (int k = 0; k < kMeshMapCount; ++k)
        if (kinds & (1u << k)) resetImage(set.images[size_t(k)], MeshMapKind(k), width, height);
    const Baker baker(mesh, kinds, settings, set.diagonal, set.boundsMin, set.boundsMax);
    const int tilesX = (width + kTextureTile - 1) / kTextureTile, tilesY = (height + kTextureTile - 1) / kTextureTile;
    const uint32_t tiles = uint32_t(tilesX * tilesY);
    std::atomic<uint32_t> next{0}, done{0};
    std::atomic<size_t> texels{0};
    std::atomic<bool> cancel{false};
    std::mutex writable;
    runWorkers([&](int worker) {
        TexelTile tt;
        for (;;) {
            if (cancel.load(std::memory_order_relaxed)) return;
            const uint32_t ti = next.fetch_add(1);
            if (ti >= tiles) break;
            if (!texelTile(mesh, width, height, ti, tt)) continue;
            if (std::all_of(tt.tri.begin(), tt.tri.end(), [](uint32_t t) { return t == kNoTexelTriangle; })) {
                done.fetch_add(1);
                continue;
            }
            std::array<uint8_t*, kMeshMapCount> outs{};
            {
                std::lock_guard lock(writable);
                for (int k = 0; k < kMeshMapCount; ++k)
                    if (kinds & (1u << k)) outs[size_t(k)] = set.images[size_t(k)].writableTile(ti);
            }
            texels += baker.bakeTile(tt, int(ti % uint32_t(tilesX)) * kTextureTile, int(ti / uint32_t(tilesX)) * kTextureTile,
                                     width, height, outs);
            const uint32_t d = done.fetch_add(1) + 1;
            if (worker == 0 && progress && !progress(float(d) / float(tiles))) cancel = true;
        }
    });
    if (cancel || (progress && !progress(1.0f))) {
        error = "Cancelled";
        return false;
    }
    for (TileImage& img : set.images)
        if (img.valid()) img.compact();
    set.texelsBaked = texels;
    set.seconds = std::chrono::duration<double>(Clock::now() - start).count();
    set.revision = out.revision + 1;
    out = std::move(set);
    return true;
}

// ---------------------------------------------------------------------------
// Live updates
// ---------------------------------------------------------------------------

AABB changedRegion(const MeshMapSet& set, const Mesh& mesh, std::vector<uint32_t>* moved) {
    AABB box;
    if (!set.positions || set.positions->size() != mesh.vertexCount()) return box;
    const std::vector<glm::vec3>& old = *set.positions;
    const size_t n = old.size();
    const int threads = int(std::clamp<size_t>(n / 200000 + 1, 1, 16));
    std::vector<AABB> boxes(static_cast<size_t>(threads));
    std::vector<std::vector<uint32_t>> lists(static_cast<size_t>(threads));
    std::vector<std::thread> pool;
    auto work = [&](int t) {
        const size_t b = n * size_t(t) / size_t(threads), e = n * size_t(t + 1) / size_t(threads);
        for (size_t i = b; i < e; ++i)
            if (old[i] != mesh.positions[i]) {
                boxes[size_t(t)].expand(old[i]);
                boxes[size_t(t)].expand(mesh.positions[i]);
                if (moved) lists[size_t(t)].push_back(uint32_t(i));
            }
    };
    for (int t = 1; t < threads; ++t) pool.emplace_back(work, t);
    work(0);
    for (auto& th : pool) th.join();
    for (int t = 0; t < threads; ++t) {
        if (boxes[size_t(t)].valid()) box.expand(boxes[size_t(t)]);
        if (moved) moved->insert(moved->end(), lists[size_t(t)].begin(), lists[size_t(t)].end());
    }
    return box;
}

bool prepareMeshMapUpdate(const Mesh& mesh, const std::shared_ptr<const MeshMapSet>& set, const AABB& region,
                          std::vector<uint32_t> moved, MeshMapUpdate& up) {
    up = MeshMapUpdate();
    if (!set || !region.valid() || !set->matches(mesh, set->width, set->height)) return false;
    const MeshMapSettings& st = set->settings;
    float reach = 0.0f;
    if (set->has(MeshMapKind::AmbientOcclusion)) reach = std::max(reach, st.aoDistance * set->diagonal);
    if (set->has(MeshMapKind::Thickness)) reach = std::max(reach, st.thicknessDistance * set->diagonal);
    const glm::vec3 r1(reach * 1.01f + set->diagonal * 1e-5f), r2(2.0f * reach * 1.01f + set->diagonal * 1e-5f);
    const AABB zone{region.min - r1, region.max + r1}, occ{region.min - r2, region.max + r2};
    const size_t nt = mesh.triangleCount();
    auto overlaps = [&](const AABB& a, uint32_t t) {
        const glm::uvec3& tv = mesh.triangles[t];
        const glm::vec3 lo = glm::min(mesh.positions[tv.x], glm::min(mesh.positions[tv.y], mesh.positions[tv.z]));
        const glm::vec3 hi = glm::max(mesh.positions[tv.x], glm::max(mesh.positions[tv.y], mesh.positions[tv.z]));
        return glm::all(glm::lessThanEqual(lo, a.max)) && glm::all(glm::greaterThanEqual(hi, a.min));
    };
    // 0: untouched, 1: occluder / curvature ring only, 2: baked again.
    std::vector<uint8_t> role(nt, 0);
    std::vector<uint8_t> ringVertex(mesh.vertexCount(), 0);
    for (uint32_t t = 0; t < nt; ++t) {
        if (overlaps(zone, t)) {
            role[t] = 2;
            for (int c = 0; c < 3; ++c) ringVertex[mesh.triangles[t][c]] = 1;
        } else if (overlaps(occ, t)) {
            role[t] = 1;
        }
    }
    // The curvature of a baked triangle's corners needs their ring, and its smoothing their neighbours' rings: two
    // rings of triangles around the baked ones.
    for (int pass = 0; pass < 2; ++pass) {
        std::vector<uint8_t> next = ringVertex;
        for (uint32_t t = 0; t < nt; ++t) {
            const glm::uvec3& tv = mesh.triangles[t];
            if (!(ringVertex[tv.x] || ringVertex[tv.y] || ringVertex[tv.z])) continue;
            if (!role[t]) role[t] = 1;
            next[tv.x] = next[tv.y] = next[tv.z] = 1;
        }
        ringVertex.swap(next);
    }
    // The compact copy: every triangle with a role, its vertices renumbered.
    std::vector<uint32_t> localTri(nt, kNoTexelTriangle), vmap(mesh.vertexCount(), kNoTexelTriangle);
    Mesh& L = up.local;
    const bool normals = mesh.normals.size() == mesh.vertexCount();
    for (uint32_t t = 0; t < nt; ++t) {
        if (!role[t]) continue;
        glm::uvec3 lt;
        for (int c = 0; c < 3; ++c) {
            uint32_t& v = vmap[mesh.triangles[t][c]];
            if (v == kNoTexelTriangle) {
                v = uint32_t(L.positions.size());
                L.positions.push_back(mesh.positions[mesh.triangles[t][c]]);
                L.normals.push_back(normals ? mesh.normals[mesh.triangles[t][c]] : glm::vec3(0, 1, 0));
            }
            lt[c] = v;
        }
        localTri[t] = uint32_t(L.triangles.size());
        L.triangles.push_back(lt);
        for (int c = 0; c < 3; ++c) L.uvs.push_back(mesh.uvs[size_t(t) * 3 + size_t(c)]);
        if (mesh.hasPolygroups()) L.polygroups.push_back(mesh.polygroups[t]);
    }
    // The tiles the baked triangles reach, and their texel maps in local triangles (others keep their values).
    const int tilesX = (set->width + kTextureTile - 1) / kTextureTile;
    std::vector<uint8_t> tileOn(size_t(tilesX) * size_t((set->height + kTextureTile - 1) / kTextureTile), 0);
    for (uint32_t t = 0; t < nt; ++t) {
        int tx0, ty0, tx1, ty1;
        if (role[t] != 2 || !triangleTiles(mesh, set->width, set->height, t, tx0, ty0, tx1, ty1)) continue;
        for (int ty = ty0; ty <= ty1; ++ty)
            for (int tx = tx0; tx <= tx1; ++tx) tileOn[size_t(ty) * size_t(tilesX) + size_t(tx)] = 1;
    }
    for (uint32_t ti = 0; ti < tileOn.size(); ++ti) {
        if (!tileOn[ti]) continue;
        TexelTile tt;
        if (!texelTile(mesh, set->width, set->height, ti, tt)) continue;
        bool any = false;
        for (uint32_t& t : tt.tri) {
            if (t == kNoTexelTriangle) continue;
            t = role[t] == 2 ? localTri[t] : kNoTexelTriangle;
            any = any || t != kNoTexelTriangle;
        }
        if (!any) continue;
        up.tiles.push_back(ti);
        up.texels.push_back(std::move(tt));
    }
    if (up.tiles.empty()) return false;
    up.movedVertices = std::move(moved);
    up.movedPositions.reserve(up.movedVertices.size());
    for (uint32_t v : up.movedVertices) up.movedPositions.push_back(mesh.positions[v]);
    up.base = set;
    return true;
}

void runMeshMapUpdate(MeshMapUpdate& up, const std::atomic<bool>* cancel) {
    const auto start = Clock::now();
    up.ok = false;
    if (!up.base || up.tiles.empty()) return;
    const MeshMapSet& set = *up.base;
    const Baker baker(up.local, set.kinds, set.settings, set.diagonal, set.boundsMin, set.boundsMax);
    // Copies of the maps share every tile; the ones baked again are made writable (copied) here.
    std::array<TileImage, kMeshMapCount> images = set.images;
    std::array<std::vector<uint8_t*>, kMeshMapCount> outs;
    for (int k = 0; k < kMeshMapCount; ++k) {
        if (!set.has(MeshMapKind(k))) continue;
        outs[size_t(k)].resize(up.tiles.size());
        for (size_t i = 0; i < up.tiles.size(); ++i) outs[size_t(k)][i] = images[size_t(k)].writableTile(up.tiles[i]);
    }
    const int tilesX = (set.width + kTextureTile - 1) / kTextureTile;
    std::atomic<size_t> next{0}, texels{0};
    runWorkers([&](int) {
        for (;;) {
            if (cancel && cancel->load(std::memory_order_relaxed)) return;
            const size_t i = next.fetch_add(1);
            if (i >= up.tiles.size()) break;
            std::array<uint8_t*, kMeshMapCount> o{};
            for (int k = 0; k < kMeshMapCount; ++k)
                if (!outs[size_t(k)].empty()) o[size_t(k)] = outs[size_t(k)][i];
            const uint32_t ti = up.tiles[i];
            texels += baker.bakeTile(up.texels[i], int(ti % uint32_t(tilesX)) * kTextureTile,
                                     int(ti / uint32_t(tilesX)) * kTextureTile, set.width, set.height, o);
        }
    });
    if (cancel && cancel->load()) return;
    for (int k = 0; k < kMeshMapCount; ++k) {
        up.result[size_t(k)].clear();
        if (!set.has(MeshMapKind(k))) continue;
        for (uint32_t ti : up.tiles) up.result[size_t(k)].push_back(images[size_t(k)].tilePtr(ti));
    }
    up.texelsBaked = texels;
    up.seconds = std::chrono::duration<double>(Clock::now() - start).count();
    up.ok = true;
}

std::shared_ptr<MeshMapSet> applyMeshMapUpdate(const MeshMapUpdate& up) {
    if (!up.ok || !up.base) return nullptr;
    auto set = std::make_shared<MeshMapSet>(*up.base);
    for (int k = 0; k < kMeshMapCount; ++k) {
        if (up.result[size_t(k)].size() != up.tiles.size()) continue;
        for (size_t i = 0; i < up.tiles.size(); ++i) set->images[size_t(k)].setTilePtr(up.tiles[i], up.result[size_t(k)][i]);
    }
    if (set->positions && !up.movedVertices.empty()) {
        auto pos = std::make_shared<std::vector<glm::vec3>>(*set->positions);
        for (size_t i = 0; i < up.movedVertices.size(); ++i)
            if (up.movedVertices[i] < pos->size()) (*pos)[up.movedVertices[i]] = up.movedPositions[i];
        set->positions = std::move(pos);
    }
    set->revision = up.base->revision + 1;
    set->texelsBaked = up.texelsBaked;
    set->seconds = up.seconds;
    return set;
}

// ---------------------------------------------------------------------------
// Command
// ---------------------------------------------------------------------------

std::string describeMeshMaps(const MeshMapSet& s) {
    std::string names;
    for (int k = 0; k < kMeshMapCount; ++k)
        if (s.has(MeshMapKind(k))) names += std::string(names.empty() ? "" : ", ") + meshMapId(MeshMapKind(k));
    char buf[256];
    std::snprintf(buf, sizeof buf, "Mesh Maps %s at %dx%d (%.1f MB, last bake %.2f s, %zu texels)%s", names.c_str(), s.width,
                  s.height, double(s.bytes()) / 1048576.0, s.seconds, s.texelsBaked, s.live ? ", live" : "");
    return buf;
}

void registerMeshMapCommands(CommandRegistry& reg) {
    using T = ParamSpec::Type;
    reg.add({"mesh_maps",
             "Mesh Maps of the active Part, on its texture's UVs at the texture's size, for Fill layers: ambient "
             "occlusion, curvature, thickness, position, world normal and ID (polygroups). bake makes them, info "
             "describes them, clear drops them, live=true re-bakes the changed tiles after sculpting (in the app). "
             "Derived data: not saved, not an undo step.",
             {{"action", T::String, "bake (default), info, clear, or live (with live=true/false).", false},
              {"maps", T::String, "Comma list: ao, curvature, thickness, position, world_normal, id, or all (default).", false},
              {"ao_rays", T::Number, "Ambient occlusion rays per texel (default 24).", false},
              {"ao_distance", T::Number, "How far occluders count, a fraction of the Part's size (default 0.05).", false},
              {"thickness_rays", T::Number, "Thickness rays per texel (default 12).", false},
              {"thickness_distance", T::Number, "Rays longer than this count as thick (default 0.1).", false},
              {"curvature_radius", T::Number, "A bend of this radius is fully bright / dark (default 0.02).", false},
              {"live", T::Bool, "Re-bake changed tiles after sculpting (default true).", false}},
             [](Document& doc, const Args& a) -> CommandResult {
                 Mesh& m = doc.mesh;
                 if (!hasTexture(m)) return {false, "The Part has no texture: Tool > Texture Map > New Texture"};
                 const std::string action = argString(a, "action", "bake");
                 PaintTexture& tex = *m.texture;
                 if (action == "info") return {true, tex.meshMaps ? describeMeshMaps(*tex.meshMaps) : std::string("No Mesh Maps")};
                 if (action == "clear") {
                     mutableTexture(m).meshMaps.reset();
                     return {true, "Mesh Maps cleared"};
                 }
                 if (action == "live") {
                     if (!tex.meshMaps) return {false, "No Mesh Maps yet (mesh_maps action=bake)"};
                     auto set = std::make_shared<MeshMapSet>(*tex.meshMaps);
                     set->live = argBool(a, "live", true);
                     mutableTexture(m).meshMaps = std::move(set);
                     return {true, std::string("Mesh Maps live updates ") + (argBool(a, "live", true) ? "on" : "off")};
                 }
                 if (action != "bake") return {false, "action must be bake, info, clear or live"};
                 uint32_t kinds = kAllMeshMaps;
                 std::string err;
                 if (a.contains("maps") && !parseMeshMaps(argString(a, "maps"), kinds, err)) return {false, err};
                 MeshMapSettings st;
                 st.aoRays = std::clamp(int(argNumber(a, "ao_rays", 24)), 1, 512);
                 st.aoDistance = std::clamp(float(argNumber(a, "ao_distance", st.aoDistance)), 0.001f, 1.0f);
                 st.thicknessRays = std::clamp(int(argNumber(a, "thickness_rays", 12)), 1, 512);
                 st.thicknessDistance = std::clamp(float(argNumber(a, "thickness_distance", st.thicknessDistance)), 0.001f, 1.0f);
                 st.curvatureRadius = std::clamp(float(argNumber(a, "curvature_radius", st.curvatureRadius)), 0.0001f, 1.0f);
                 auto set = std::make_shared<MeshMapSet>();
                 if (tex.meshMaps) set->revision = tex.meshMaps->revision;
                 if (!bakeMeshMaps(m, tex.width, tex.height, kinds, st, *set, err)) return {false, err};
                 set->live = argBool(a, "live", true);
                 const std::string text = describeMeshMaps(*set);
                 mutableTexture(m).meshMaps = std::move(set);
                 notifyMeshMapsChanged(doc, m);
                 return {true, text};
             }});
}

} // namespace sf
