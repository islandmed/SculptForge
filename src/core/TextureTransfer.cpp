// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/TextureTransfer.h"

#include "core/Parallel.h"
#include "core/SurfaceQuery.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>

namespace sf {

namespace {

using Clock = std::chrono::steady_clock;
double secondsSince(Clock::time_point t) { return std::chrono::duration<double>(Clock::now() - t).count(); }

// One texture a texel can read from: its RGBA8 planes (same order as the destination's) and its UVs.
struct Source {
    const PaintTexture* tex = nullptr; // null: the part has no texture (its texels are white)
    const std::vector<glm::vec2>* uvs = nullptr;
    std::vector<const std::vector<uint8_t>*> planes;
};

// Bilinear RGBA at `uv` (texel centres at (x + 0.5) / w), weighted by alpha so transparent texels of a layer
// don't darken its edges. Opaque planes give plain bilinear filtering.
inline void sampleRgba(const uint8_t* plane, int w, int h, const glm::vec2& uv, uint8_t* out) {
    const float fx = std::clamp(uv.x, 0.0f, 1.0f) * float(w) - 0.5f, fy = std::clamp(uv.y, 0.0f, 1.0f) * float(h) - 0.5f;
    const float flx = std::floor(fx), fly = std::floor(fy);
    const int x0 = std::clamp(int(flx), 0, w - 1), y0 = std::clamp(int(fly), 0, h - 1);
    const int x1 = std::min(x0 + 1, w - 1), y1 = std::min(y0 + 1, h - 1);
    const float ax = std::clamp(fx - flx, 0.0f, 1.0f), ay = std::clamp(fy - fly, 0.0f, 1.0f);
    const uint8_t* p[4] = {plane + (size_t(y0) * size_t(w) + size_t(x0)) * 4, plane + (size_t(y0) * size_t(w) + size_t(x1)) * 4,
                           plane + (size_t(y1) * size_t(w) + size_t(x0)) * 4, plane + (size_t(y1) * size_t(w) + size_t(x1)) * 4};
    const float wt[4] = {(1 - ax) * (1 - ay), ax * (1 - ay), (1 - ax) * ay, ax * ay};
    float r = 0, g = 0, b = 0, a = 0;
    for (int k = 0; k < 4; ++k) {
        const float wa = wt[k] * float(p[k][3]);
        r += wa * float(p[k][0]);
        g += wa * float(p[k][1]);
        b += wa * float(p[k][2]);
        a += wa;
    }
    if (a <= 0.0f) {
        std::memcpy(out, p[0], 3);
        out[3] = 0;
        return;
    }
    const float inv = 1.0f / a;
    out[0] = uint8_t(std::lround(std::clamp(r * inv, 0.0f, 255.0f)));
    out[1] = uint8_t(std::lround(std::clamp(g * inv, 0.0f, 255.0f)));
    out[2] = uint8_t(std::lround(std::clamp(b * inv, 0.0f, 255.0f)));
    out[3] = uint8_t(std::lround(std::clamp(a, 0.0f, 255.0f)));
}

// Material channels (core/TextureChannels.h) carried along: per destination channel image a dense buffer filled
// texel by texel like the RGBA8 planes (bilinear reads of the source images, any format), padded the same way,
// then stored back into the sparse image.
struct ChannelJob {
    std::vector<const TileImage*> src; // per Source (null: that source has no such channel; reads `def`)
    TileImage* dst = nullptr;
    std::vector<uint8_t> dense; // w * h texels of dst's format
    int comps = 1, bits = 8;
    bool alpha = false; // a layer channel: the last component is coverage (weights the padding)
    float def[4] = {0, 0, 0, 0};
};
using ChannelJobs = std::vector<ChannelJob>;

inline void readTexelUnits(const uint8_t* p, int comps, int bits, float* out) {
    for (int k = 0; k < comps; ++k) {
        if (bits == 16) {
            uint16_t u;
            std::memcpy(&u, p + k * 2, 2);
            out[k] = float(u) / 65535.0f;
        } else {
            out[k] = float(p[k]) / 255.0f;
        }
    }
}
inline void writeTexelUnits(uint8_t* p, int comps, int bits, const float* v) {
    for (int k = 0; k < comps; ++k) {
        if (bits == 16) {
            const uint16_t u = uint16_t(std::lround(std::clamp(v[k], 0.0f, 1.0f) * 65535.0f));
            std::memcpy(p + k * 2, &u, 2);
        } else {
            p[k] = uint8_t(std::lround(std::clamp(v[k], 0.0f, 1.0f) * 255.0f));
        }
    }
}

// Starts a job writing `dst` (already sized and formatted) with every texel at dst's fill.
ChannelJob channelJob(TileImage& dst, bool alpha, size_t sources) {
    ChannelJob j;
    j.dst = &dst;
    j.comps = dst.components();
    j.bits = dst.bits();
    j.alpha = alpha;
    j.src.assign(sources, nullptr);
    for (int k = 0; k < j.comps; ++k) j.def[k] = float(dst.fill()[size_t(k)]) / dst.maxValue();
    const size_t tb = size_t(dst.texelBytes());
    j.dense.resize(size_t(dst.width()) * size_t(dst.height()) * tb);
    uint8_t px[8];
    writeTexelUnits(px, j.comps, j.bits, j.def);
    for (size_t i = 0; i < j.dense.size(); i += tb) std::memcpy(&j.dense[i], px, tb);
    return j;
}

void finishChannelJobs(ChannelJobs& jobs) {
    for (ChannelJob& j : jobs) {
        j.dst->assignDense(j.dense);
        std::vector<uint8_t>().swap(j.dense);
    }
}

// Fills `planes` (each w * h RGBA8, already holding the blank value) from the sources: every texel inside a
// triangle of `uvs` asks lookup(tri, bary, source, sourceTri, sourceBary) where to read. `ring` gets 1 for
// filled texels. Returns the texels inside islands; `missed` those whose lookup failed.
template <class Lookup>
size_t fillPlanes(std::vector<std::vector<uint8_t>*>& planes, int w, int h, const std::vector<glm::vec2>& uvs,
                  size_t triangles, const std::vector<Source>& sources, std::vector<uint8_t>& ring, size_t& missed,
                  ChannelJobs& chans, Lookup&& lookup) {
    std::atomic<size_t> inside{0}, lost{0};
    ring.assign(size_t(w) * size_t(h), 0);
    parallelFor(triangles, [&](size_t b, size_t e) {
        size_t in = 0, miss = 0;
        for (size_t t = b; t < e; ++t) {
            rasterizeUvTriangle(uvs[t * 3], uvs[t * 3 + 1], uvs[t * 3 + 2], w, h, [&](int x, int y, float b0, float b1, float b2) {
                ++in;
                uint32_t s = 0, st = 0;
                glm::vec3 sb;
                if (!lookup(uint32_t(t), glm::vec3(b0, b1, b2), s, st, sb)) {
                    ++miss;
                    return;
                }
                const size_t i = size_t(y) * size_t(w) + size_t(x);
                const Source& src = sources[s];
                if (src.tex) {
                    const glm::vec2 uv = (*src.uvs)[size_t(st) * 3] * sb.x + (*src.uvs)[size_t(st) * 3 + 1] * sb.y +
                                         (*src.uvs)[size_t(st) * 3 + 2] * sb.z;
                    for (size_t k = 0; k < planes.size(); ++k)
                        sampleRgba(src.planes[k]->data(), src.tex->width, src.tex->height, uv, &(*planes[k])[i * 4]);
                    for (ChannelJob& j : chans) {
                        float v[4] = {j.def[0], j.def[1], j.def[2], j.def[3]};
                        if (const TileImage* img = j.src[s]) img->sample(uv.x, uv.y, v);
                        writeTexelUnits(&j.dense[i * size_t(j.dst->texelBytes())], j.comps, j.bits, v);
                    }
                } else {
                    for (size_t k = 0; k < planes.size(); ++k) std::memset(&(*planes[k])[i * 4], 255, 4);
                }
                ring[i] = 1;
            });
        }
        inside += in;
        lost += miss;
    }, 2000);
    missed = lost;
    return inside;
}

// Pads `rings` texels around the filled ones: ring by ring, each empty texel next to filled ones takes their
// alpha-weighted average (as the baker pads its maps).
void padPlanes(std::vector<std::vector<uint8_t>*>& planes, int w, int h, std::vector<uint8_t>& ring, int rings,
               ChannelJobs& chans) {
    std::vector<uint8_t> next(ring.size(), 0);
    for (int k = 2; k <= rings + 1; ++k) {
        std::atomic<size_t> added{0};
        parallelFor(size_t(h), [&](size_t y0, size_t y1) {
            size_t n = 0;
            for (int y = int(y0); y < int(y1); ++y)
                for (int x = 0; x < w; ++x) {
                    const size_t i = size_t(y) * size_t(w) + size_t(x);
                    if (ring[i]) continue;
                    int nb[8], cnt = 0;
                    for (int dy = -1; dy <= 1; ++dy)
                        for (int dx = -1; dx <= 1; ++dx) {
                            const int nx = x + dx, ny = y + dy;
                            if ((dx | dy) == 0 || nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
                            const size_t j = size_t(ny) * size_t(w) + size_t(nx);
                            if (ring[j] && ring[j] < k) nb[cnt++] = int(j);
                        }
                    if (!cnt) continue;
                    for (auto* plane : planes) {
                        float r = 0, g = 0, b = 0, a = 0;
                        for (int q = 0; q < cnt; ++q) {
                            const uint8_t* p = &(*plane)[size_t(nb[q]) * 4];
                            const float pa = float(p[3]) + 1e-3f; // keeps the colour of fully transparent texels
                            r += pa * float(p[0]);
                            g += pa * float(p[1]);
                            b += pa * float(p[2]);
                            a += pa;
                        }
                        uint8_t* o = &(*plane)[i * 4];
                        o[0] = uint8_t(std::lround(r / a));
                        o[1] = uint8_t(std::lround(g / a));
                        o[2] = uint8_t(std::lround(b / a));
                        o[3] = uint8_t(std::lround(std::clamp(a / float(cnt), 0.0f, 255.0f)));
                    }
                    for (ChannelJob& j : chans) {
                        const size_t tb = size_t(j.dst->texelBytes());
                        float sum[4] = {0, 0, 0, 0}, wsum = 0, asum = 0;
                        for (int q = 0; q < cnt; ++q) {
                            float v[4] = {0, 0, 0, 0};
                            readTexelUnits(&j.dense[size_t(nb[q]) * tb], j.comps, j.bits, v);
                            const float wq = j.alpha ? v[j.comps - 1] + 1e-3f : 1.0f;
                            for (int k = 0; k < j.comps; ++k) sum[k] += v[k] * wq;
                            wsum += wq;
                            if (j.alpha) asum += v[j.comps - 1];
                        }
                        float out[4];
                        for (int k = 0; k < j.comps; ++k) out[k] = sum[k] / wsum;
                        if (j.alpha) out[j.comps - 1] = asum / float(cnt);
                        writeTexelUnits(&j.dense[i * tb], j.comps, j.bits, out);
                    }
                    next[i] = uint8_t(k);
                    ++n;
                }
            added += n;
        }, 64);
        if (!added) break;
        parallelFor(ring.size(), [&](size_t b, size_t e) {
            for (size_t i = b; i < e; ++i)
                if (next[i]) ring[i] = next[i], next[i] = 0;
        });
    }
}

// A texture of `like`'s size and layer settings, every plane blank (Base white, layers transparent).
std::shared_ptr<PaintTexture> blankLike(const PaintTexture& like, bool withLayers) {
    auto t = makeTexture(like.width, like.height);
    if (withLayers && !like.layers.empty()) {
        t->base.assign(t->texelCount() * 4, 255);
        t->layers.reserve(like.layers.size());
        for (const TextureLayer& l : like.layers) { // the settings, not the texels
            TextureLayer c;
            c.id = l.id;
            c.name = l.name;
            c.opacity = l.opacity;
            c.blend = l.blend;
            c.visible = l.visible;
            c.locked = l.locked;
            for (int ch = 0; ch < kChannelCount; ++ch) { // channel settings; images come from syncLayerChannels
                c.channels[size_t(ch)].enabled = l.channels[size_t(ch)].enabled;
                c.channels[size_t(ch)].blend = l.channels[size_t(ch)].blend;
                c.channels[size_t(ch)].opacity = l.channels[size_t(ch)].opacity;
            }
            c.rgba.assign(t->texelCount() * 4, 0);
            t->layers.push_back(std::move(c));
        }
        t->activeLayer = like.activeLayer;
    }
    t->heightNormalStrength = like.heightNormalStrength;
    for (int ch = 1; ch < kChannelCount; ++ch)
        if (channelEnabled(like, Channel(ch))) setChannelEnabled(*t, Channel(ch), true);
    return t;
}

// Jobs moving every material channel of the single source `old` into `t` (made by blankLike): Base and each
// layer's channel when `withLayers`, else the composites into Base.
ChannelJobs channelJobsFor(const PaintTexture& old, PaintTexture& t, bool withLayers) {
    ChannelJobs jobs;
    syncLayerChannels(t);
    for (int c = 1; c < kChannelCount; ++c) {
        if (!channelEnabled(old, Channel(c)) || !channelEnabled(t, Channel(c))) continue;
        jobs.push_back(channelJob(t.channels[size_t(c)].base, false, 1));
        jobs.back().src[0] = withLayers ? &old.channels[size_t(c)].base : &channelImage(old, Channel(c));
        if (!withLayers) continue;
        for (size_t l = 0; l < t.layers.size() && l < old.layers.size(); ++l) {
            const TileImage& src = old.layers[l].channels[size_t(c)].image;
            TileImage& dst = t.layers[l].channels[size_t(c)].image;
            if (!src.valid() || !dst.valid()) continue;
            jobs.push_back(channelJob(dst, true, 1));
            jobs.back().src[0] = &src;
        }
    }
    return jobs;
}

// The destination planes of `t` and the matching source planes of `src` (Base / pixels first, then layers).
void planesOf(PaintTexture& t, std::vector<std::vector<uint8_t>*>& out) {
    out.clear();
    if (t.layers.empty()) {
        out.push_back(&t.pixels);
        return;
    }
    out.push_back(&t.base);
    for (auto& l : t.layers) out.push_back(&l.rgba);
}
void planesOf(const PaintTexture& t, bool withLayers, std::vector<const std::vector<uint8_t>*>& out) {
    out.clear();
    if (!withLayers || t.layers.empty() || t.base.size() != t.pixels.size()) {
        out.push_back(&t.pixels);
        return;
    }
    out.push_back(&t.base);
    for (const auto& l : t.layers) out.push_back(&l.rgba);
}

bool layersUsable(const PaintTexture& t) {
    if (t.layers.empty() || t.base.size() != t.pixels.size()) return false;
    for (const auto& l : t.layers)
        if (l.rgba.size() != t.pixels.size()) return false;
    return true;
}

void finish(PaintTexture& t, ChannelJobs& jobs) {
    finishChannelJobs(jobs);
    if (!t.layers.empty()) compositeTextureAll(t);
    t.markAllDirty();
}

} // namespace

std::shared_ptr<PaintTexture> retargetTexture(const Mesh& mesh, const std::vector<glm::vec2>& newUvs,
                                              TextureTransferStats* stats) {
    if (!hasTexture(mesh) || newUvs.size() != mesh.triangleCount() * 3) return nullptr;
    const auto start = Clock::now();
    const PaintTexture& old = *mesh.texture;
    const bool layers = layersUsable(old);
    auto tex = blankLike(old, layers);
    std::vector<std::vector<uint8_t>*> dst;
    planesOf(*tex, dst);
    std::vector<Source> sources(1);
    sources[0].tex = &old;
    sources[0].uvs = &mesh.uvs;
    planesOf(old, layers, sources[0].planes);
    ChannelJobs chans = channelJobsFor(old, *tex, layers);
    std::vector<uint8_t> ring;
    size_t missed = 0;
    const size_t texels = fillPlanes(dst, tex->width, tex->height, newUvs, mesh.triangleCount(), sources, ring, missed, chans,
                                     [](uint32_t t, const glm::vec3& b, uint32_t& s, uint32_t& st, glm::vec3& sb) {
                                         s = 0, st = t, sb = b;
                                         return true;
                                     });
    padPlanes(dst, tex->width, tex->height, ring, kTransferPadding, chans);
    finish(*tex, chans);
    if (stats) {
        stats->texels = texels;
        stats->missed = missed;
        stats->transferSeconds = secondsSince(start);
    }
    return tex;
}

std::string setUvsKeepingTexture(Mesh& mesh, std::vector<glm::vec2> uvs, bool keep) {
    std::string note;
    if (hasTexture(mesh)) {
        TextureTransferStats st;
        std::shared_ptr<PaintTexture> moved = keep ? retargetTexture(mesh, uvs, &st) : nullptr;
        if (moved) {
            const size_t layers = moved->layers.size();
            mesh.texture = std::move(moved);
            char buf[160];
            std::snprintf(buf, sizeof buf, "; paint moved to the new UVs (%s%.1f s)",
                          layers ? (std::to_string(layers) + " layer" + (layers > 1 ? "s, " : ", ")).c_str() : "",
                          st.transferSeconds);
            note = buf;
        } else {
            note = "; the texture was left on the old layout, so it no longer lines up (undo to get the old UVs back)";
        }
    }
    mesh.uvs = std::move(uvs);
    mesh.facesDirty = true;
    return note;
}

bool transferTextureFromSurface(const Mesh& source, Mesh& target, std::string& error, TextureTransferStats* stats) {
    if (!hasTexture(source)) {
        error = "The source has no texture";
        return false;
    }
    if (target.uvs.size() != target.triangleCount() * 3 || target.triangleCount() == 0) {
        error = "The new mesh has no UVs";
        return false;
    }
    const auto start = Clock::now();
    const PaintTexture& old = *source.texture;
    const bool layers = layersUsable(old);
    auto tex = blankLike(old, layers);
    std::vector<std::vector<uint8_t>*> dst;
    planesOf(*tex, dst);
    std::vector<Source> sources(1);
    sources[0].tex = &old;
    sources[0].uvs = &source.uvs;
    planesOf(old, layers, sources[0].planes);
    ChannelJobs chans = channelJobsFor(old, *tex, layers);

    TriangleBVH bvh;
    bvh.build(source.positions, source.triangles);
    // How far each new vertex is from the old surface bounds every texel's search: a point of a triangle is
    // within (corner distance + edge length) of the old surface, so the query stays local.
    const float inf = std::numeric_limits<float>::max();
    std::vector<float> vd(target.vertexCount(), inf);
    parallelFor(target.vertexCount(), [&](size_t b, size_t e) {
        for (size_t v = b; v < e; ++v) {
            SurfaceHit h;
            if (bvh.closestPoint(target.positions[v], inf, h)) vd[v] = h.distance;
        }
    }, 4096);
    const size_t nt = target.triangleCount();
    std::vector<float> reach(nt, inf);
    parallelFor(nt, [&](size_t b, size_t e) {
        for (size_t t = b; t < e; ++t) {
            const glm::uvec3 tv = target.triangles[t];
            const glm::vec3 &p0 = target.positions[tv.x], &p1 = target.positions[tv.y], &p2 = target.positions[tv.z];
            const float edge = std::max({glm::length(p1 - p0), glm::length(p2 - p1), glm::length(p0 - p2)});
            const float d = std::max({vd[tv.x], vd[tv.y], vd[tv.z]});
            if (d < inf) reach[t] = d + edge * 1.01f + 1e-6f;
        }
    });
    std::vector<uint8_t> ring;
    size_t missed = 0;
    const size_t texels = fillPlanes(dst, tex->width, tex->height, target.uvs, nt, sources, ring, missed, chans,
                                     [&](uint32_t t, const glm::vec3& b, uint32_t& s, uint32_t& st, glm::vec3& sb) {
                                         const glm::uvec3 tv = target.triangles[t];
                                         const glm::vec3 p = target.positions[tv.x] * b.x + target.positions[tv.y] * b.y +
                                                             target.positions[tv.z] * b.z;
                                         SurfaceHit h;
                                         if (!bvh.closestPoint(p, reach[t], h) && !bvh.closestPoint(p, inf, h)) return false;
                                         s = 0, st = h.prim, sb = h.bary;
                                         return true;
                                     });
    padPlanes(dst, tex->width, tex->height, ring, kTransferPadding, chans);
    finish(*tex, chans);
    target.texture = std::move(tex);
    target.paintToTexture = source.paintToTexture;
    target.facesDirty = true;
    if (stats) {
        stats->texels = texels;
        stats->missed = missed;
        stats->transferSeconds = secondsSince(start);
    }
    return true;
}

bool carryTexture(const Mesh& source, Mesh& target, std::string& error, TextureTransferStats* stats,
                  const AutoUvProgress& progress) {
    if (!hasTexture(source)) return true;
    TextureTransferStats local;
    TextureTransferStats& st = stats ? *stats : local;
    const auto start = Clock::now();
    AutoUvResult uv;
    const int res = std::max(source.texture->width, source.texture->height);
    if (!computeAutoUv(target.positions, target.triangles, res, uv, error, progress)) return false;
    st.uvSeconds = secondsSince(start);
    st.charts = uv.charts;
    target.uvs = std::move(uv.uvs);
    if (!transferTextureFromSurface(source, target, error, &st)) {
        target.uvs.clear();
        target.texture.reset();
        return false;
    }
    if (progress) progress(1.0f);
    return true;
}

std::string carryTextureNote(const Mesh& source, Mesh& target, bool keep, const AutoUvProgress& progress) {
    if (!hasTexture(source)) return {};
    if (!keep) return "; the texture was dropped (keep_texture=false)";
    std::string err;
    TextureTransferStats st;
    if (!carryTexture(source, target, err, &st, progress))
        return "; the texture could not be kept (" + err + "): undo, export it, or bake it to vertex paint first";
    char buf[160];
    std::snprintf(buf, sizeof buf, "; texture kept (Auto UV, %d islands, %.1f s)", st.charts, st.uvSeconds + st.transferSeconds);
    return buf;
}

bool carryMergedTextures(const std::vector<const Mesh*>& parts, Mesh& merged, std::string& error,
                         TextureTransferStats* stats) {
    const PaintTexture* biggest = nullptr;
    bool toTexture = false;
    size_t total = 0;
    for (const Mesh* m : parts) {
        total += m->triangleCount();
        if (!hasTexture(*m)) continue;
        if (!biggest || m->texture->width > biggest->width) biggest = m->texture.get();
        toTexture = toTexture || m->paintToTexture;
    }
    if (!biggest) return true;
    if (total != merged.triangleCount()) {
        error = "The merged mesh doesn't match its parts";
        return false;
    }
    TextureTransferStats local;
    TextureTransferStats& st = stats ? *stats : local;
    const auto start = Clock::now();
    AutoUvResult uv;
    if (!computeAutoUv(merged.positions, merged.triangles, biggest->width, uv, error)) return false;
    st.uvSeconds = secondsSince(start);
    st.charts = uv.charts;
    const auto t0 = Clock::now();
    merged.uvs = std::move(uv.uvs);
    auto tex = makeTexture(biggest->width, biggest->height);
    std::vector<std::vector<uint8_t>*> dst{&tex->pixels};
    std::vector<Source> sources(parts.size());
    std::vector<uint32_t> first(parts.size() + 1, 0); // first merged triangle of each part
    for (size_t i = 0; i < parts.size(); ++i) {
        first[i + 1] = first[i] + uint32_t(parts[i]->triangleCount());
        if (!hasTexture(*parts[i])) continue;
        sources[i].tex = parts[i]->texture.get();
        sources[i].uvs = &parts[i]->uvs;
        sources[i].planes = {&parts[i]->texture->pixels};
    }
    // Material channels: every channel any part has; parts without it read its default.
    ChannelJobs chans;
    for (int c = 1; c < kChannelCount; ++c) {
        bool any = false;
        for (const Mesh* m : parts) any = any || (hasTexture(*m) && channelEnabled(*m->texture, Channel(c)));
        if (!any) continue;
        setChannelEnabled(*tex, Channel(c), true);
        chans.push_back(channelJob(tex->channels[size_t(c)].base, false, parts.size()));
        for (size_t i = 0; i < parts.size(); ++i)
            if (hasTexture(*parts[i]) && channelEnabled(*parts[i]->texture, Channel(c)))
                chans.back().src[i] = &channelImage(*parts[i]->texture, Channel(c));
    }
    std::vector<uint8_t> ring;
    size_t missed = 0;
    st.texels = fillPlanes(dst, tex->width, tex->height, merged.uvs, merged.triangleCount(), sources, ring, missed, chans,
                           [&](uint32_t t, const glm::vec3& b, uint32_t& s, uint32_t& lt, glm::vec3& sb) {
                               s = uint32_t(std::upper_bound(first.begin(), first.end(), t) - first.begin()) - 1;
                               lt = t - first[s];
                               sb = b;
                               return true;
                           });
    st.missed = missed;
    padPlanes(dst, tex->width, tex->height, ring, kTransferPadding, chans);
    finish(*tex, chans);
    merged.texture = std::move(tex);
    merged.paintToTexture = toTexture;
    merged.facesDirty = true;
    st.transferSeconds = secondsSince(t0);
    return true;
}

std::string carryMergedTexturesNote(const std::vector<const Mesh*>& parts, Mesh& merged, bool keep) {
    size_t textured = 0;
    for (const Mesh* m : parts) textured += hasTexture(*m) ? 1 : 0;
    if (!textured) return {};
    const std::string what = textured > 1 ? "textures" : "texture";
    if (!keep) {
        merged.texture.reset();
        merged.paintToTexture = false;
        return "; the " + what + " were dropped (keep_texture=false)";
    }
    std::string err;
    TextureTransferStats st;
    if (!carryMergedTextures(parts, merged, err, &st)) {
        merged.texture.reset();
        merged.paintToTexture = false;
        return "; the " + what + " could not be kept (" + err + ")";
    }
    char buf[160];
    std::snprintf(buf, sizeof buf, "; %s kept in one texture (Auto UV, %d islands, %.1f s)", what.c_str(), st.charts,
                  st.uvSeconds + st.transferSeconds);
    return buf;
}

} // namespace sf
