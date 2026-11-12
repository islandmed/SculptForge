// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "render/MicroRenderer.h"
#include "core/AtomicRef.h"

#include "core/FocusDetail.h"
#include "render/MicroFocus.h"

#include "core/ThreadPool.h"
#include "core/TierConfig.h"

#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <chrono>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstring>

namespace sf {

namespace {

constexpr GLenum kUnitTris = 0x84C8, kUnitBase = 0x84C9, kUnitDisp = 0x84CA, kUnitLevels = 0x84CB, kUnitShade = 0x84CC;
constexpr uint32_t kChunkTexels = 1u << 26;
constexpr uint32_t kNoCluster = 0xFFFFFFFFu;

// Evaluates the micro surface exactly as MicroTriangle::evalWeights (corners in ascending vertex order), so
// both triangles of a shared edge compute bit-identical points there.
const char* kMicroVS = R"(
layout(location = 0) in uvec2 aIJ; // the patch's grid point at uLevel
uniform mat4 uView;
uniform mat4 uProj;
uniform usamplerBuffer uTris;   // per slot: (corner vertices, wireframe edges), (cluster across each edge, -)
uniform samplerBuffer uBase;    // per base vertex: position, normal
uniform isamplerBuffer uDisp;   // this chunk's displacement grids
uniform usamplerBuffer uLevels; // per cluster: the level it draws
uniform int uSlotBase, uChunkSlot, uLevel, uN, uSamples;
#ifdef FOCUS
// Focus Detail (render/MicroFocus.h; its own program, so levels in memory run the plain shader): uDisp holds per
// slot a record (the coarse grid, uCN segments, then the inner points of the three edges at full resolution, uRecord
// values in all); the tiles with a page (uPages: page + 1, bit 31 its shading is in, 0 = none) hold their full grid in uPool.
uniform int uCN, uCSamples, uRecord, uCShift;
uniform usamplerBuffer uPages;
uniform isamplerBuffer uPool;
flat out int vPage; // the page whose full shading to use (-1: the coarse shading)
#endif
// > 0: a plain (not instanced) draw with this many corners per base triangle, each corner's grid point packed
// i | j << 16 in uCorners (small patches: instancing a few triangles at a time costs more than it saves)
uniform int uUnrolled;
uniform int uCorners[48];
uniform float uStep;
flat out int vSlot;
flat out int vEdges; // the base triangle's edges the wireframe draws (bit e: edge e; quad diagonals left out)
out vec2 vIJ;
#ifdef FLAT
out vec3 vViewPos;
#endif
vec3 P[3];
vec3 Nn[3];
int ord[3];
int localSlot;
int sampleIndex(int i, int j) { return j * (uN + 1) - (j * (j - 1)) / 2 + i; }
#ifdef FOCUS
int gPage;
int coarsePoint(int i, int j) { return texelFetch(uDisp, localSlot * uRecord + j * (uCN + 1) - (j * (j - 1)) / 2 + i).r; }
// A record's grid point, as FocusDetail upsamples (core/FocusDetail.cpp, upsample: the same integer sums).
int coarseAt(int I, int J) {
    int e = -1, k = 0;
    if (J == 0) { e = 0; k = I; }
    else if (I + J == uN) { e = 1; k = J; }
    else if (I == 0) { e = 2; k = uN - J; }
    if (e >= 0 && k > 0 && k < uN) return texelFetch(uDisp, localSlot * uRecord + uCSamples + e * (uN - 1) + k - 1).r;
    int s = 1 << uCShift, ci = I >> uCShift, cj = J >> uCShift, fi = I & (s - 1), fj = J & (s - 1);
    int sum;
    if (fi + fj <= s) {
        sum = (s - fi - fj) * coarsePoint(ci, cj);
        if (fi > 0) sum += fi * coarsePoint(ci + 1, cj);
        if (fj > 0) sum += fj * coarsePoint(ci, cj + 1);
    } else {
        sum = (fi + fj - s) * coarsePoint(ci + 1, cj + 1) + (s - fi) * coarsePoint(ci, cj + 1) + (s - fj) * coarsePoint(ci + 1, cj);
    }
    int q = sum + s / 2;
    return q >= 0 ? q >> uCShift : -((-q + s - 1) >> uCShift);
}
float dispAt(int I, int J) {
    if (gPage >= 0) return float(texelFetch(uPool, gPage * uSamples + sampleIndex(I, J)).r);
    return float(coarseAt(I, J));
}
#else
float dispAt(int I, int J) { return float(texelFetch(uDisp, localSlot * uSamples + sampleIndex(I, J)).r); }
#endif
vec3 microPos(int I, int J) {
    float inv = 1.0 / float(uN);
    float wc[3] = float[3](float(uN - I - J) * inv, float(I) * inv, float(J) * inv);
    float w0 = wc[ord[0]], w1 = wc[ord[1]], w2 = wc[ord[2]];
    vec3 f = P[0] * w0;
    f += P[1] * w1;
    f += P[2] * w2;
    vec3 nn = Nn[0] * w0;
    nn += Nn[1] * w1;
    nn += Nn[2] * w2;
    vec3 pull = Nn[0] * (w0 * dot(f - P[0], Nn[0]));
    pull += Nn[1] * (w1 * dot(f - P[1], Nn[1]));
    pull += Nn[2] * (w2 * dot(f - P[2], Nn[2]));
    vec3 s = f - pull * 0.75;
    float len = length(nn);
    vec3 n = len > 1e-20 ? nn / len : vec3(0.0, 0.0, 1.0);
    float d = dispAt(I, J) * uStep;
    return s + n * d;
}
ivec2 edgePoint(int e, int k, int m) {
    if (e == 0) return ivec2(k, 0);
    if (e == 1) return ivec2(m - k, k);
    return ivec2(0, m - k);
}
void main() {
    int instance = gl_InstanceID;
    uvec2 ij = aIJ;
    if (uUnrolled > 0) {
        instance = gl_VertexID / uUnrolled;
        int c = uCorners[gl_VertexID - instance * uUnrolled];
        ij = uvec2(uint(c & 0xFFFF), uint(c >> 16));
    }
    int slot = uSlotBase + instance;
    localSlot = slot - uChunkSlot;
    vSlot = localSlot;
#ifdef FOCUS
    uint pg = texelFetch(uPages, slot).r;
    gPage = int(pg & 0x7FFFFFFFu) - 1;
    vPage = (pg & 0x80000000u) != 0u ? gPage : -1;
#endif
    if (uLevel == 0) {
        // The base triangle alone (a cluster far away, a scan's millions of tiny patches): each vertex is one
        // corner, where the weights below are (1, 0, 0) and the bulge 0, so the same point from one base vertex.
        uvec4 t0 = texelFetch(uTris, slot * 2);
        vEdges = int(t0.w);
        int corner = int(ij.x) + 2 * int(ij.y);
        int vi = int(corner == 0 ? t0.x : corner == 1 ? t0.y : t0.z);
        vec3 bp = texelFetch(uBase, vi * 2).xyz, bn = texelFetch(uBase, vi * 2 + 1).xyz;
        float bl = length(bn);
        vec3 nrm = bl > 1e-20 ? bn / bl : vec3(0.0, 0.0, 1.0);
        int I0 = int(ij.x) * uN, J0 = int(ij.y) * uN;
        float d0 = dispAt(I0, J0) * uStep;
        vIJ = vec2(float(I0), float(J0));
        vec4 p0 = uView * vec4(bp + nrm * d0, 1.0);
#ifdef FLAT
        vViewPos = p0.xyz;
#endif
        gl_Position = uProj * p0;
        return;
    }
    uvec4 tv = texelFetch(uTris, slot * 2);
    uvec4 nb = texelFetch(uTris, slot * 2 + 1);
    vEdges = int(tv.w);
    uint v[3] = uint[3](tv.x, tv.y, tv.z);
    ord[0] = 0; ord[1] = 1; ord[2] = 2;
    if (v[ord[0]] > v[ord[1]]) { int t = ord[0]; ord[0] = ord[1]; ord[1] = t; }
    if (v[ord[1]] > v[ord[2]]) { int t = ord[1]; ord[1] = ord[2]; ord[2] = t; }
    if (v[ord[0]] > v[ord[1]]) { int t = ord[0]; ord[0] = ord[1]; ord[1] = t; }
    for (int k = 0; k < 3; ++k) {
        int vi = int(v[ord[k]]);
        P[k] = texelFetch(uBase, vi * 2).xyz;
        Nn[k] = texelFetch(uBase, vi * 2 + 1).xyz;
    }
    int m = 1 << uLevel, sc = uN >> uLevel;
    int i = int(ij.x), j = int(ij.y);
    int edge = -1, k = 0;
    if (j == 0 && i > 0 && i < m) { edge = 0; k = i; }
    else if (i + j == m && j > 0 && j < m) { edge = 1; k = j; }
    else if (i == 0 && j > 0 && j < m) { edge = 2; k = m - j; }
    vec3 pos;
    int elevel = uLevel;
    if (edge >= 0) {
        uint c = edge == 0 ? nb.x : edge == 1 ? nb.y : nb.z;
        if (c != 0xFFFFFFFFu) elevel = min(uLevel, int(texelFetch(uLevels, int(c)).r));
    }
    int stepE = 1 << (uLevel - elevel);
    int r = k % stepE;
    if (edge >= 0 && r != 0) {
        ivec2 a = edgePoint(edge, k - r, m) * sc, b = edgePoint(edge, k - r + stepE, m) * sc;
        pos = mix(microPos(a.x, a.y), microPos(b.x, b.y), float(r) / float(stepE));
    } else {
        pos = microPos(i * sc, j * sc);
    }
    vIJ = vec2(float(i * sc), float(j * sc));
    vec4 p = uView * vec4(pos, 1.0);
#ifdef FLAT
    vViewPos = p.xyz;
#endif
    gl_Position = uProj * p;
}
)";

uint32_t spread10(uint32_t x) {
    x &= 0x3FF;
    x = (x | (x << 16)) & 0x030000FF;
    x = (x | (x << 8)) & 0x0300F00F;
    x = (x | (x << 4)) & 0x030C30C3;
    x = (x | (x << 2)) & 0x09249249;
    return x;
}

} // namespace

const char* microVertexShaderSource() { return kMicroVS; }

MicroRenderer::~MicroRenderer() { release(); }

void MicroRenderer::release() {
    for (Chunk& c : chunks_) {
        gl::DeleteBuffers(1, &c.disp);
        gl::DeleteBuffers(1, &c.shade);
        gl::DeleteTextures(1, &c.dispTex);
        gl::DeleteTextures(1, &c.shadeTex);
    }
    chunks_.clear();
    for (GLuint* b : {&triData_, &baseData_, &levelData_})
        if (*b) gl::DeleteBuffers(1, b), *b = 0;
    for (GLuint* t : {&triTex_, &baseTex_, &levelTex_})
        if (*t) gl::DeleteTextures(1, t), *t = 0;
    for (Patch& p : patches_) {
        if (p.vao) gl::DeleteVertexArrays(1, &p.vao);
        if (p.vbo) gl::DeleteBuffers(1, &p.vbo);
        if (p.ibo) gl::DeleteBuffers(1, &p.ibo);
    }
    patches_.clear();
    if (emptyVao_) gl::DeleteVertexArrays(1, &emptyVao_), emptyVao_ = 0;
    focus_.reset();
    serial_ = 0;
    bytes_ = 0;
}

size_t MicroRenderer::gpuBytes() const { return bytes_ + (focus_ ? focus_->gpuBytes() : 0); }

void MicroRenderer::ensurePatches(int maxLevel) {
    while (int(patches_.size()) <= maxLevel) {
        const int l = int(patches_.size());
        const uint32_t m = 1u << l;
        std::vector<uint16_t> verts, idx;
        for (uint32_t j = 0; j <= m; ++j)
            for (uint32_t i = 0; i + j <= m; ++i) verts.push_back(uint16_t(i)), verts.push_back(uint16_t(j));
        for (uint32_t j = 0; j < m; ++j) {
            const uint32_t row = MicroMesh::index(0, j, m), next = MicroMesh::index(0, j + 1, m);
            for (uint32_t i = 0; i + j < m; ++i) {
                idx.insert(idx.end(), {uint16_t(row + i), uint16_t(row + i + 1), uint16_t(next + i)});
                if (i + j + 1 < m) idx.insert(idx.end(), {uint16_t(row + i + 1), uint16_t(next + i + 1), uint16_t(next + i)});
            }
        }
        Patch p;
        gl::GenVertexArrays(1, &p.vao);
        gl::GenBuffers(1, &p.vbo);
        gl::GenBuffers(1, &p.ibo);
        gl::BindVertexArray(p.vao);
        gl::BindBuffer(GL_ARRAY_BUFFER, p.vbo);
        gl::BufferData(GL_ARRAY_BUFFER, GLsizeiptr(verts.size() * 2), verts.data(), GL_STATIC_DRAW);
        gl::EnableVertexAttribArray(0);
        gl::VertexAttribIPointer(0, 2, GL_UNSIGNED_SHORT, 4, nullptr);
        gl::BindBuffer(GL_ELEMENT_ARRAY_BUFFER, p.ibo);
        gl::BufferData(GL_ELEMENT_ARRAY_BUFFER, GLsizeiptr(idx.size() * 2), idx.data(), GL_STATIC_DRAW);
        gl::BindVertexArray(0);
        gl::BindBuffer(GL_ARRAY_BUFFER, 0);
        p.indices = GLsizei(idx.size());
        if (l <= kUnrolledLevels)
            for (uint16_t k : idx) p.corners.push_back(GLint(verts[size_t(k) * 2]) | GLint(verts[size_t(k) * 2 + 1]) << 16);
        patches_.push_back(p);
    }
}

void MicroRenderer::buildLayout(const Mesh& mesh) {
    const MicroMesh& mm = *mesh.micro;
    const uint32_t nt = mm.baseTriangles;
    // Slots in Morton order of the triangle centres: clusters of consecutive slots are compact.
    box_ = AABB();
    for (const glm::vec3& p : mesh.positions) box_.expand(p);
    const glm::vec3 lo = box_.min, ext = glm::max(box_.extent(), glm::vec3(1e-20f));
    std::vector<uint64_t> keys(nt);
    poolFor(nt, [&](size_t b, size_t e) {
        for (size_t t = b; t < e; ++t) {
            const glm::uvec3 tr = mesh.triangles[t];
            const glm::vec3 c = ((mesh.positions[tr.x] + mesh.positions[tr.y] + mesh.positions[tr.z]) / 3.0f - lo) / ext;
            const glm::uvec3 q = glm::uvec3(glm::clamp(c, 0.0f, 1.0f) * 1023.0f);
            keys[t] = uint64_t(spread10(q.x) | spread10(q.y) << 1 | spread10(q.z) << 2) << 32 | t;
        }
    }, 1 << 14);
    std::sort(keys.begin(), keys.end());
    order_.resize(nt);
    slotOf_.resize(nt);
    for (uint32_t s = 0; s < nt; ++s) {
        order_[s] = uint32_t(keys[s]);
        slotOf_[order_[s]] = s;
    }
    std::vector<uint64_t>().swap(keys);
    // Focus Detail: a record per slot in the chunks, full grids in MicroFocus's pool.
    if (mm.focus && mm.level > FocusDetail::kCoarseLevel) {
        if (!focus_) focus_ = std::make_unique<MicroFocus>();
        focus_->setup(mesh, nt);
    } else {
        focus_.reset();
    }
    const uint32_t dispPer = focus_ ? focus_->recordPoints() : mm.samples, shadePer = focus_ ? focus_->shadePoints() : mm.samples;
    const uint32_t clusterSize = std::clamp(65536u >> (2 * mm.level), 16u, 1024u);
    clusterShift_ = uint32_t(std::countr_zero(clusterSize));
    chunkSlots_ = std::max(clusterSize, (kChunkTexels / dispPer) & ~(clusterSize - 1));
    const uint32_t clusters = (nt + clusterSize - 1) >> clusterShift_;
    // Corners, the edges the wireframe draws (#128: the base's, without quad diagonals, as Renderer's own
    // wireframe) and the clusters across the edges, per slot.
    std::vector<uint32_t> tri(size_t(nt) * 8);
    poolFor(nt, [&](size_t b, size_t e) {
        for (size_t s = b; s < e; ++s) {
            const uint32_t t = order_[s];
            const glm::uvec3 tr = mesh.triangles[t];
            uint32_t* o = tri.data() + s * 8;
            const uint32_t partner = mesh.quadPartnerAt(t);
            const int diagonal = partner != Mesh::kNoQuad && !mesh.triangleHidden(partner) ? mesh.quadDiagonal(t, partner) : -1;
            o[0] = tr.x, o[1] = tr.y, o[2] = tr.z, o[3] = diagonal < 0 ? 7u : 7u & ~(1u << diagonal);
            for (int k = 0; k < 3; ++k) {
                const uint32_t nb = mm.edgeNeighbor[size_t(t) * 3 + size_t(k)];
                o[4 + k] = nb == MicroMesh::kNone ? kNoCluster : slotOf_[nb >> 2] >> clusterShift_;
            }
            o[7] = 0;
        }
    }, 1 << 14);
    auto makeTex = [&](GLuint& buffer, GLuint& tex, GLenum format, const void* data, size_t bytes) {
        if (!buffer) gl::GenBuffers(1, &buffer);
        if (!tex) gl::GenTextures(1, &tex);
        gl::BindBuffer(GL_TEXTURE_BUFFER, buffer);
        gl::BufferData(GL_TEXTURE_BUFFER, GLsizeiptr(bytes), data, GL_STATIC_DRAW);
        gl::BindTexture(GL_TEXTURE_BUFFER, tex);
        gl::TexBuffer(GL_TEXTURE_BUFFER, format, buffer);
        bytes_ += bytes;
    };
    bytes_ = 0;
    makeTex(triData_, triTex_, GL_RGBA32UI, tri.data(), tri.size() * 4);
    levels_.assign(clusters, 0);
    makeTex(levelData_, levelTex_, GL_R32UI, levels_.data(), levels_.size() * 4);
    // Grid chunks.
    for (Chunk& c : chunks_) {
        gl::DeleteBuffers(1, &c.disp);
        gl::DeleteBuffers(1, &c.shade);
        gl::DeleteTextures(1, &c.dispTex);
        gl::DeleteTextures(1, &c.shadeTex);
    }
    chunks_.clear();
    for (uint32_t first = 0; first < nt; first += chunkSlots_) {
        Chunk c;
        c.firstSlot = first;
        c.slots = std::min(chunkSlots_, nt - first);
        makeTex(c.disp, c.dispTex, GL_R16I, nullptr, size_t(c.slots) * dispPer * 2);
        makeTex(c.shade, c.shadeTex, GL_RGBA8, nullptr, size_t(c.slots) * shadePer * 4);
        chunks_.push_back(c);
    }
    gl::BindBuffer(GL_TEXTURE_BUFFER, 0);
    gl::BindTexture(GL_TEXTURE_BUFFER, 0);
    closed_ = closedOriented(mesh);
    bounds_.assign(clusters, ClusterBounds());
    clusterEdge_.assign(clusters, 0.0f);
    clusterRadius_.assign(clusters, 0.0f);
    clusterAngle_.assign(clusters, 0.0f);
    std::vector<uint32_t> all(clusters);
    for (uint32_t c = 0; c < clusters; ++c) all[c] = c;
    updateClusterBounds(mesh, all);
    pending_.assign((size_t(nt) + 63) / 64, 0);
    stale_.assign(pending_.size(), 0);
    pendingCount_ = staleCount_ = 0;
    ensurePatches(mm.level);
}

void MicroRenderer::uploadBase(const Mesh& mesh) {
    std::vector<glm::vec4> data(mesh.vertexCount() * 2);
    poolFor(mesh.vertexCount(), [&](size_t b, size_t e) {
        for (size_t v = b; v < e; ++v) {
            data[v * 2] = glm::vec4(mesh.positions[v], 1.0f);
            data[v * 2 + 1] = glm::vec4(mesh.normals[v], 0.0f);
        }
    }, 1 << 14);
    if (!baseData_) gl::GenBuffers(1, &baseData_);
    if (!baseTex_) gl::GenTextures(1, &baseTex_);
    gl::BindBuffer(GL_TEXTURE_BUFFER, baseData_);
    gl::BufferData(GL_TEXTURE_BUFFER, GLsizeiptr(data.size() * sizeof(glm::vec4)), data.data(), GL_STATIC_DRAW);
    gl::BindTexture(GL_TEXTURE_BUFFER, baseTex_);
    gl::TexBuffer(GL_TEXTURE_BUFFER, GL_RGBA32F, baseData_);
    gl::BindBuffer(GL_TEXTURE_BUFFER, 0);
    gl::BindTexture(GL_TEXTURE_BUFFER, 0);
    bytes_ += data.size() * sizeof(glm::vec4);
}

void MicroRenderer::updateClusterBounds(const Mesh& mesh, const std::vector<uint32_t>& clusters) {
    const MicroMesh& mm = *mesh.micro;
    const uint32_t nt = mm.baseTriangles, size = 1u << clusterShift_;
    poolFor(clusters.size(), [&](size_t b, size_t e) {
        for (size_t ci = b; ci < e; ++ci) {
            const uint32_t c = clusters[ci];
            const uint32_t s0 = c << clusterShift_, s1 = std::min(nt, s0 + size);
            AABB box;
            glm::vec3 axis(0.0f);
            float edge = 0.0f;
            uint16_t range = 0;
            for (uint32_t s = s0; s < s1; ++s) {
                const uint32_t t = order_[s];
                const glm::uvec3 tr = mesh.triangles[t];
                for (int k = 0; k < 3; ++k) {
                    box.expand(mesh.positions[tr[k]]);
                    axis += mesh.normals[tr[k]];
                }
                edge += glm::distance(mesh.positions[tr.x], mesh.positions[tr.y]) + glm::distance(mesh.positions[tr.y], mesh.positions[tr.z]) +
                        glm::distance(mesh.positions[tr.z], mesh.positions[tr.x]);
                range = std::max(range, mm.triRange[t]);
            }
            ClusterBounds cb;
            cb.center = box.center();
            float radius = 0.0f, cosMin = 1.0f;
            const float al = glm::length(axis);
            axis = al > 0.0f ? axis / al : glm::vec3(0, 0, 1);
            for (uint32_t s = s0; s < s1; ++s) {
                const glm::uvec3 tr = mesh.triangles[order_[s]];
                for (int k = 0; k < 3; ++k) {
                    radius = std::max(radius, glm::distance(mesh.positions[tr[k]], cb.center));
                    cosMin = std::min(cosMin, glm::dot(mesh.normals[tr[k]], axis));
                }
            }
            cb.radius = radius + mm.bulgeMax + float(range) * mm.step;
            cb.axis = axis;
            // Micro detail tilts the surface off the base normals: half a radian of margin, and none when the
            // displacement is large next to the cluster (its faces could point anywhere).
            const float angle = std::acos(std::clamp(cosMin, -1.0f, 1.0f)) + 0.5f;
            cb.coneAngle = angle < 1.45f && float(range) * mm.step < radius * 0.5f ? angle : -1.0f;
            bounds_[c] = cb;
            clusterRadius_[c] = radius;
            clusterAngle_[c] = angle;
            clusterEdge_[c] = edge / float(3 * (s1 - s0)) / float(mm.n) * float(mm.n); // (base edge)
        }
    }, 16);
}

void MicroRenderer::updateClusterRanges(const Mesh& mesh, const std::vector<uint32_t>& clusters) {
    const MicroMesh& mm = *mesh.micro;
    const uint32_t nt = mm.baseTriangles, size = 1u << clusterShift_;
    poolFor(clusters.size(), [&](size_t b, size_t e) {
        for (size_t ci = b; ci < e; ++ci) {
            const uint32_t c = clusters[ci];
            const uint32_t s0 = c << clusterShift_, s1 = std::min(nt, s0 + size);
            uint16_t range = 0;
            for (uint32_t s = s0; s < s1; ++s) range = std::max(range, mm.triRange[order_[s]]);
            ClusterBounds& cb = bounds_[c];
            const float radius = clusterRadius_[c], angle = clusterAngle_[c];
            cb.radius = radius + mm.bulgeMax + float(range) * mm.step;
            cb.coneAngle = angle < 1.45f && float(range) * mm.step < radius * 0.5f ? angle : -1.0f;
        }
    }, 64);
}

void MicroRenderer::uploadDispTiles(const Mesh& mesh, std::vector<uint32_t>& slots, bool all) {
    const MicroMesh& mm = *mesh.micro;
    const size_t S = mm.samples;
    if (all) {
        for (Chunk& c : chunks_) {
            gl::BindBuffer(GL_TEXTURE_BUFFER, c.disp);
            auto* dst = static_cast<int16_t*>(gl::MapBufferRange(GL_TEXTURE_BUFFER, 0, GLsizeiptr(size_t(c.slots) * S * 2),
                                                                  GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT));
            if (!dst) continue;
            poolFor(c.slots, [&](size_t b, size_t e) {
                for (size_t s = b; s < e; ++s) std::memcpy(dst + s * S, mm.disp.data() + size_t(order_[c.firstSlot + s]) * S, S * 2);
            }, 1024);
            gl::UnmapBuffer(GL_TEXTURE_BUFFER);
        }
        gl::BindBuffer(GL_TEXTURE_BUFFER, 0);
        return;
    }
    if (slots.empty()) return;
    if (slots.size() > 4096) { // (in slot order through a bitset: sorting millions of tiny patches cost more than sending them)
        slotBits_.assign((size_t(baseTriangles_) + 63) / 64, 0);
        for (uint32_t sl : slots) slotBits_[sl >> 6] |= uint64_t(1) << (sl & 63);
        size_t k = 0;
        for (size_t w = 0; w < slotBits_.size(); ++w)
            for (uint64_t bits = slotBits_[w]; bits; bits &= bits - 1) slots[k++] = uint32_t(w * 64 + size_t(std::countr_zero(bits)));
        slots.resize(k);
    } else {
        std::sort(slots.begin(), slots.end());
        slots.erase(std::unique(slots.begin(), slots.end()), slots.end());
    }
    staging_.resize(slots.size() * S * 2);
    auto* st = reinterpret_cast<int16_t*>(staging_.data());
    poolFor(slots.size(), [&](size_t b, size_t e) {
        for (size_t i = b; i < e; ++i) std::memcpy(st + i * S, mm.disp.data() + size_t(order_[slots[i]]) * S, S * 2);
    }, 256);
    for (size_t i = 0; i < slots.size();) {
        const uint32_t chunk = slots[i] / chunkSlots_;
        size_t j = i + 1;
        while (j < slots.size() && slots[j] == slots[j - 1] + 1 && slots[j] / chunkSlots_ == chunk) ++j;
        const Chunk& c = chunks_[chunk];
        gl::BindBuffer(GL_TEXTURE_BUFFER, c.disp);
        gl::BufferSubData(GL_TEXTURE_BUFFER, GLintptr(size_t(slots[i] - c.firstSlot) * S * 2), GLsizeiptr((j - i) * S * 2),
                          st + i * S);
        i = j;
    }
    gl::BindBuffer(GL_TEXTURE_BUFFER, 0);
}

// Shading for the base triangles marked pending: all of it (`all`), or up to shadeBudget grid points of it.
void MicroRenderer::shadePending(const Mesh& mesh, bool all) {
    const MicroMesh& mm = *mesh.micro;
    const size_t S = mm.samples;
    if (all) {
        std::vector<uint32_t*> mapped(chunks_.size(), nullptr);
        for (size_t i = 0; i < chunks_.size(); ++i) {
            gl::BindBuffer(GL_TEXTURE_BUFFER, chunks_[i].shade);
            mapped[i] = static_cast<uint32_t*>(gl::MapBufferRange(GL_TEXTURE_BUFFER, 0, GLsizeiptr(size_t(chunks_[i].slots) * S * 4),
                                                                   GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT));
        }
        thread_local std::vector<uint32_t> sink;
        shader_.shade(mesh, {}, true, written_, [&](uint32_t t) -> uint32_t* {
            const uint32_t s = slotOf_[t], c = s / chunkSlots_;
            if (!mapped[c]) {
                sink.resize(S);
                return sink.data();
            }
            return mapped[c] + size_t(s - chunks_[c].firstSlot) * S;
        });
        for (size_t i = 0; i < chunks_.size(); ++i) {
            gl::BindBuffer(GL_TEXTURE_BUFFER, chunks_[i].shade);
            if (mapped[i]) gl::UnmapBuffer(GL_TEXTURE_BUFFER);
        }
        gl::BindBuffer(GL_TEXTURE_BUFFER, 0);
        std::fill(pending_.begin(), pending_.end(), 0);
        std::fill(stale_.begin(), stale_.end(), 0);
        pendingCount_ = staleCount_ = 0;
        return;
    }
    // The pending triangles, newest first (scratch_ holds this sync's dirty ones, already marked), each written
    // alone: the neighbours whose shared edges and corners they change become stale_, written after them, and not
    // at all while a stroke runs (deferStale): its next dabs change most of them again, and what they'd change is
    // the brush's falloff tail at the patch border (usually nothing: a changed shared point changes both patches).
    // As many a frame as when each one also wrote its ring (~3 patches): shading as current as it was, for less.
    const size_t limit = shadeBudget ? std::max<size_t>(1, shadeBudget / (S * 3)) : SIZE_MAX;
    std::vector<uint32_t>& seeds = scratch_;
    const bool full = seeds.size() >= limit; // (this sync's dirty ones fill it: sorted and unique already)
    if (full) seeds.resize(limit);
    else if (seeds.size() < limit)
        for (size_t w = 0; w < pending_.size() && seeds.size() < limit; ++w)
            for (uint64_t bits = pending_[w]; bits && seeds.size() < limit; bits &= bits - 1) {
                const uint32_t t = uint32_t(w * 64 + size_t(std::countr_zero(bits)));
                seeds.push_back(t);
            }
    if (!full) {
        std::sort(seeds.begin(), seeds.end());
        seeds.erase(std::unique(seeds.begin(), seeds.end()), seeds.end());
        if (seeds.size() > limit) seeds.resize(limit);
    }
    const auto started = std::chrono::steady_clock::now();
    size_t shaded = 0;
    if (!seeds.empty()) {
        shaded += shadeSeeds(mesh, seeds, &outer_);
        for (uint32_t t : outer_) {
            uint64_t& w = stale_[t >> 6];
            const uint64_t bit = uint64_t(1) << (t & 63);
            if (!(w & bit)) w |= bit, ++staleCount_;
        }
    }
    if (const size_t room = shaded < limit ? limit - shaded : 0; !deferStale && staleCount_ && room) {
        seeds.clear();
        for (size_t w = 0; w < stale_.size() && seeds.size() < room; ++w)
            for (uint64_t bits = stale_[w] & ~pending_[w]; bits && seeds.size() < room; bits &= bits - 1)
                seeds.push_back(uint32_t(w * 64 + size_t(std::countr_zero(bits))));
        if (!seeds.empty()) shaded += shadeSeeds(mesh, seeds, nullptr);
    }
    if (shaded) { // the cost per point, smoothed (a stroke's shading budget in time follows it)
        const double ns = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - started).count() /
                          double(shaded * S);
        shadeNsPerPoint_ = shadeNsPerPoint_ > 0.0 ? shadeNsPerPoint_ * 0.7 + ns * 0.3 : ns;
    }
}

// Shades `seeds` (sorted) alone and sends them; `outer` gets their neighbours to write later. Returns how many.
size_t MicroRenderer::shadeSeeds(const Mesh& mesh, std::span<const uint32_t> seeds, std::vector<uint32_t>* outer) {
    const MicroMesh& mm = *mesh.micro;
    const size_t S = mm.samples;
    std::vector<uint32_t> sorted;
    if (stageIndex_.size() != mm.baseTriangles) stageIndex_.assign(mm.baseTriangles, 0);
    shader_.shade(mesh, seeds, false, written_, [&](uint32_t t) -> uint32_t* {
        return reinterpret_cast<uint32_t*>(staging_.data()) + size_t(stageIndex_[t]) * S;
    }, [&](std::span<const uint32_t> tris) {
        sorted.assign(tris.begin(), tris.end());
        std::sort(sorted.begin(), sorted.end(), [&](uint32_t a, uint32_t b) { return slotOf_[a] < slotOf_[b]; });
        for (size_t i = 0; i < sorted.size(); ++i) stageIndex_[sorted[i]] = uint32_t(i);
        staging_.resize(sorted.size() * S * 4);
    }, false, outer);
    const auto* st = reinterpret_cast<const uint32_t*>(staging_.data());
    for (size_t i = 0; i < sorted.size();) {
        const uint32_t s0 = slotOf_[sorted[i]], chunk = s0 / chunkSlots_;
        size_t j = i + 1;
        while (j < sorted.size() && slotOf_[sorted[j]] == slotOf_[sorted[j - 1]] + 1 && slotOf_[sorted[j]] / chunkSlots_ == chunk) ++j;
        const Chunk& c = chunks_[chunk];
        gl::BindBuffer(GL_TEXTURE_BUFFER, c.shade);
        gl::BufferSubData(GL_TEXTURE_BUFFER, GLintptr(size_t(s0 - c.firstSlot) * S * 4), GLsizeiptr((j - i) * S * 4), st + i * S);
        i = j;
    }
    gl::BindBuffer(GL_TEXTURE_BUFFER, 0);
    for (uint32_t t : written_) { // (written whole and current: neither pending nor stale)
        const uint64_t bit = uint64_t(1) << (t & 63);
        uint64_t& w = pending_[t >> 6];
        if (w & bit) w &= ~bit, --pendingCount_;
        uint64_t& v = stale_[t >> 6];
        if (v & bit) v &= ~bit, --staleCount_;
    }
    return written_.size();
}

bool MicroRenderer::sync(Mesh& mesh) {
    MicroMesh& mm = *mesh.micro;
    mm.ensureTopology(mesh);
    if (mm.dirtyBits.size() != (size_t(mm.baseTriangles) + 63) / 64) mm.dirtyBits.assign((size_t(mm.baseTriangles) + 63) / 64, 0);
    const bool baseMoved = mesh.dirtyBegin < mesh.dirtyEnd;
    const bool full = serial_ != mm.serial || baseTriangles_ != mm.baseTriangles || baseVertices_ != mm.baseVertices ||
                      level_ != mm.level || mesh.topologyDirty;
    for (int i = 0; i < 16 && gl::GetError() != GL_NO_ERROR; ++i) {
    }
    if (full) {
        serial_ = mm.serial;
        baseTriangles_ = mm.baseTriangles;
        baseVertices_ = mm.baseVertices;
        level_ = mm.level;
        n_ = mm.n;
        samples_ = mm.samples;
        buildLayout(mesh);
        uploadBase(mesh);
        if (focus_) {
            focus_->syncAll(mesh, *this);
        } else {
            uploadDispTiles(mesh, scratch_, true);
            shadePending(mesh, true);
        }
    } else {
        // The base cage moved (dual-scale Move, big brushes on a dense base, a transform, undo of any): the detail
        // rides along on the GPU (it is relative to the base); what changes is the base, the bounds and the shading
        // around moved vertices. Their triangles in a bitset (millions on a scan: no lists to sort).
        bool baseEverywhere = false;
        const size_t words = mm.dirtyBits.size();
        baseBits_.assign(words, 0);
        bool anyBase = false;
        if (baseMoved) {
            const uint32_t b0 = mesh.dirtyBegin, b1 = std::min<uint32_t>(mesh.dirtyEnd, uint32_t(mesh.vertexCount()));
            // ... and the runs of moved vertices to send: a scan's vertex order isn't spatial, so a small dab's few
            // thousand vertices can lie anywhere between the first and the last of 14M (Lucy: sending that span,
            // ~450 MB, was a 25 px stroke's slowest frames). Gaps shorter than kGap are sent with the run.
            constexpr uint32_t kGap = 64, kBlock = 1u << 14;
            std::atomic<size_t> moved{0};
            baseRuns_.assign((size_t(b1 - b0) + kBlock - 1) / kBlock, {});
            poolFor(size_t(b1 - b0), [&](size_t b, size_t e) {
                size_t count = 0;
                auto& runs = baseRuns_[b / kBlock];
                for (size_t i = b; i < e; ++i) {
                    const uint32_t v = b0 + uint32_t(i);
                    if (!mesh.vertexDirty(v)) continue;
                    ++count;
                    if (!runs.empty() && v - runs.back().second <= kGap) runs.back().second = v + 1;
                    else runs.push_back({v, v + 1});
                    for (uint32_t t : mesh.vertexTriangles(v))
                        AtomicRef<uint64_t>(baseBits_[t >> 6]).fetch_or(uint64_t(1) << (t & 63), std::memory_order_relaxed);
                }
                moved.fetch_add(count, std::memory_order_relaxed);
            }, kBlock);
            anyBase = moved.load() > 0;
            baseEverywhere = moved.load() * 4 > mesh.vertexCount();
            if (baseEverywhere) {
                uploadBase(mesh);
                bytes_ -= mesh.vertexCount() * 2 * sizeof(glm::vec4);
            } else if (anyBase) {
                std::vector<std::pair<uint32_t, uint32_t>> runs;
                for (const auto& block : baseRuns_) runs.insert(runs.end(), block.begin(), block.end());
                std::vector<size_t> at(runs.size() + 1, 0);
                for (size_t k = 0; k < runs.size(); ++k) at[k + 1] = at[k] + (runs[k].second - runs[k].first);
                std::vector<glm::vec4> part(at.back() * 2);
                poolFor(runs.size(), [&](size_t b, size_t e) {
                    for (size_t k = b; k < e; ++k)
                        for (uint32_t v = runs[k].first, o = 0; v < runs[k].second; ++v, ++o) {
                            part[(at[k] + o) * 2] = glm::vec4(mesh.positions[v], 1.0f);
                            part[(at[k] + o) * 2 + 1] = glm::vec4(mesh.normals[v], 0.0f);
                        }
                }, 64);
                gl::BindBuffer(GL_TEXTURE_BUFFER, baseData_);
                for (size_t k = 0; k < runs.size(); ++k)
                    gl::BufferSubData(GL_TEXTURE_BUFFER, GLintptr(size_t(runs[k].first) * 2 * sizeof(glm::vec4)),
                                      GLsizeiptr((runs[k].second - runs[k].first) * 2 * sizeof(glm::vec4)), part.data() + at[k] * 2);
                gl::BindBuffer(GL_TEXTURE_BUFFER, 0);
            }
        }
        // This sync's triangles whose heights changed: their tiles go to the GPU (a moved base sends no heights).
        scratch_.clear();
        for (size_t w = 0; w < words; ++w)
            for (uint64_t bits = mm.dirtyBits[w]; bits; bits &= bits - 1) scratch_.push_back(uint32_t(w * 64 + size_t(std::countr_zero(bits))));
        if (focus_) {
            static const std::vector<uint64_t> none;
            if (mm.allDirty || mm.rescaled) focus_->syncAll(mesh, *this);
            else focus_->sync(mesh, *this, scratch_, anyBase ? baseBits_ : none, mm.stroking, viewMoving);
        } else if (mm.allDirty || mm.rescaled) {
            uploadDispTiles(mesh, scratch_, true);
        } else if (!scratch_.empty()) {
            std::vector<uint32_t> slots(scratch_.size());
            for (size_t i = 0; i < scratch_.size(); ++i) slots[i] = slotOf_[scratch_[i]];
            uploadDispTiles(mesh, slots, false);
        }
        // Bounds: the clusters whose base moved in full, those whose heights alone changed by their range.
        if (!scratch_.empty() || anyBase) {
            std::vector<uint32_t> moved, heights;
            clusterSeen_.assign(bounds_.size(), 0);
            for (size_t w = 0; w < words; ++w)
                for (uint64_t bits = baseBits_[w]; bits; bits &= bits - 1) {
                    const uint32_t c = slotOf_[uint32_t(w * 64 + size_t(std::countr_zero(bits)))] >> clusterShift_;
                    if (clusterSeen_[c] != 1) clusterSeen_[c] = 1, moved.push_back(c);
                }
            for (uint32_t t : scratch_) {
                const uint32_t c = slotOf_[t] >> clusterShift_;
                if (!clusterSeen_[c]) clusterSeen_[c] = 2, heights.push_back(c);
            }
            if (!moved.empty()) updateClusterBounds(mesh, moved);
            if (!heights.empty()) updateClusterRanges(mesh, heights);
        }
        // Their shading follows within the frame's budget (moved base included: a stroke that moves a quarter of a
        // scan's base no longer re-shades all of it at once).
        if (anyBase && !focus_) // (MicroFocus shaded above)
            for (size_t w = 0; w < words; ++w)
                if (const uint64_t add = baseBits_[w] & ~pending_[w]) {
                    pending_[w] |= add;
                    pendingCount_ += size_t(std::popcount(add));
                }
        if (focus_) {
        } else if (mm.allDirty) {
            shadePending(mesh, true);
        } else {
            for (uint32_t t : scratch_) {
                uint64_t& w = pending_[t >> 6];
                const uint64_t bit = uint64_t(1) << (t & 63);
                if (!(w & bit)) w |= bit, ++pendingCount_;
            }
            // TierConfig (microShadeBudget): the budget every sync, a quarter of it while a draft stroke runs; while a
            // stroke runs or the view moves, no more than strokeShadeMs of it (at least one patch and its ring), so a
            // slower CPU doesn't spend the frame the dabs or the orbit need on shading; the rest follows.
            shadeBudget = microShadeBudget(currentTierConfig(), mm.stroking, viewMoving, shadeNsPerPoint_, size_t(mm.samples) * 3);
            deferStale = mm.stroking;
            if (pendingCount_ || staleCount_) shadePending(mesh, false);
        }
    }
    step_ = mm.step;
    mm.clearDirty();
    mesh.clearDirty();
    mesh.topologyDirty = false;
    mesh.facesDirty = false;
    bool oom = false;
    for (int i = 0; i < 16; ++i) {
        const GLenum e = gl::GetError();
        if (e == GL_NO_ERROR) break;
        oom = oom || e == GL_OUT_OF_MEMORY;
    }
    return !oom;
}

size_t MicroRenderer::draw(GLuint program, const CullView& cullIn, const glm::mat4& proj, const glm::vec4& viewport, size_t budget,
                           float targetPx) {
    if (chunks_.empty() || bounds_.empty()) return 0;
    const uint32_t clusters = uint32_t(bounds_.size()), size = 1u << clusterShift_;
    CullView cull = cullIn;
    // Clusters facing away can go when the mesh is closed and the eye is outside it.
    const glm::vec3 grow(box_.extent() * 0.1f);
    const bool eyeInside = glm::all(glm::greaterThanEqual(cull.eye, box_.min - grow)) && glm::all(glm::lessThanEqual(cull.eye, box_.max + grow));
    cull.backfaces = closed_ && !eyeInside && !cull.ortho;
    want_.resize(clusters);
    std::vector<uint8_t> visible(clusters);
    const float h = std::max(viewport.w, 1.0f);
    const bool ortho = proj[3][3] == 1.0f;
    const float scale = h * proj[1][1] * 0.5f;
    std::vector<float> pxEdge(clusters);
    for (uint32_t c = 0; c < clusters; ++c) {
        const ClusterBounds& b = bounds_[c];
        visible[c] = insideFrustum(cull, b) && !facingAway(cull, b);
        const float dist = ortho ? 1.0f : std::max(glm::distance(cull.eye, b.center) - b.radius, std::max(cull.nearDistance, 1e-6f));
        pxEdge[c] = clusterEdge_[c] * scale / dist; // a base edge in pixels
    }
    auto choose = [&](float target) {
        size_t total = 0;
        for (uint32_t c = 0; c < clusters; ++c) {
            const float ratio = pxEdge[c] / target;
            int l = ratio <= 1.0f ? 0 : int(std::ceil(std::log2(ratio)));
            l = std::clamp(l, 0, level_);
            want_[c] = int8_t(l);
            if (visible[c]) total += size_t(std::min(size, baseTriangles_ - (c << clusterShift_))) << (2 * l);
        }
        return total;
    };
    float target = std::max(targetPx, 0.25f);
    size_t total = choose(target);
    for (int it = 0; it < 24 && budget && total > budget; ++it) total = choose(target *= 1.4142f);
    lastTargetPx = target;
    for (uint32_t c = 0; c < clusters; ++c) levels_[c] = uint32_t(want_[c]);
    gl::BindBuffer(GL_TEXTURE_BUFFER, levelData_);
    gl::BufferSubData(GL_TEXTURE_BUFFER, 0, GLsizeiptr(levels_.size() * 4), levels_.data());
    gl::BindBuffer(GL_TEXTURE_BUFFER, 0);

    if (u_.program != program) {
        u_.program = program;
        auto at = [&](const char* name) { return gl::GetUniformLocation(program, name); };
        u_.slotBase = at("uSlotBase");
        u_.chunkSlot = at("uChunkSlot");
        u_.level = at("uLevel");
        u_.n = at("uN");
        u_.samples = at("uSamples");
        u_.step = at("uStep");
        u_.unrolled = at("uUnrolled");
        u_.corners = at("uCorners");
        gl::Uniform1i(at("uTris"), 8);
        gl::Uniform1i(at("uBase"), 9);
        gl::Uniform1i(at("uDisp"), 10);
        gl::Uniform1i(at("uLevels"), 11);
        gl::Uniform1i(at("uShade"), 12);
        // (always set: samplers of another type left on unit 0 make draws fail on some drivers)
        gl::Uniform1i(at("uPages"), 13);
        gl::Uniform1i(at("uPool"), 14);
        gl::Uniform1i(at("uPoolShade"), 15);
    }
    gl::Uniform1i(u_.n, int(n_));
    gl::Uniform1i(u_.samples, int(samples_));
    gl::Uniform1f(u_.step, step_);
    gl::ActiveTexture(kUnitTris);
    gl::BindTexture(GL_TEXTURE_BUFFER, triTex_);
    gl::ActiveTexture(kUnitBase);
    gl::BindTexture(GL_TEXTURE_BUFFER, baseTex_);
    gl::ActiveTexture(kUnitLevels);
    gl::BindTexture(GL_TEXTURE_BUFFER, levelTex_);
    if (focus_) focus_->bind(program, visible, want_);
    if (closed_) gl::Enable(GL_CULL_FACE);
    size_t drawn = 0;
    int boundChunk = -1;
    int unrolledLevel = -2; // the patch uCorners holds (-1: instanced draws)
    for (uint32_t c = 0; c < clusters;) {
        if (!visible[c]) {
            ++c;
            continue;
        }
        const int l = want_[c];
        const uint32_t chunk = (c << clusterShift_) / chunkSlots_;
        uint32_t e = c + 1;
        while (e < clusters && visible[e] && want_[e] == l && (e << clusterShift_) / chunkSlots_ == chunk) ++e;
        const uint32_t s0 = c << clusterShift_, s1 = std::min(baseTriangles_, e << clusterShift_);
        if (int(chunk) != boundChunk) {
            boundChunk = int(chunk);
            gl::ActiveTexture(kUnitDisp);
            gl::BindTexture(GL_TEXTURE_BUFFER, chunks_[chunk].dispTex);
            gl::ActiveTexture(kUnitShade);
            gl::BindTexture(GL_TEXTURE_BUFFER, chunks_[chunk].shadeTex);
            gl::Uniform1i(u_.chunkSlot, int(chunks_[chunk].firstSlot));
        }
        gl::Uniform1i(u_.level, l);
        gl::Uniform1i(u_.slotBase, int(s0));
        const Patch& p = patches_[size_t(l)];
        if (l <= kUnrolledLevels) {
            if (!emptyVao_) gl::GenVertexArrays(1, &emptyVao_);
            if (l != unrolledLevel) {
                gl::Uniform1i(u_.unrolled, GLsizei(p.corners.size()));
                gl::Uniform1iv(u_.corners, GLsizei(p.corners.size()), p.corners.data());
                unrolledLevel = l;
            }
            gl::BindVertexArray(emptyVao_);
            gl::DrawArrays(GL_TRIANGLES, 0, GLsizei(size_t(s1 - s0) * p.corners.size()));
        } else {
            if (unrolledLevel != -1) gl::Uniform1i(u_.unrolled, 0), unrolledLevel = -1;
            gl::BindVertexArray(p.vao);
            gl::DrawElementsInstanced(GL_TRIANGLES, p.indices, GL_UNSIGNED_SHORT, nullptr, GLsizei(s1 - s0));
        }
        drawn += size_t(s1 - s0) << (2 * l);
        c = e;
    }
    gl::Disable(GL_CULL_FACE);
    gl::BindVertexArray(0);
    if (focus_) MicroFocus::unbind();
    for (GLenum unit : {kUnitTris, kUnitBase, kUnitDisp, kUnitLevels, kUnitShade}) {
        gl::ActiveTexture(unit);
        gl::BindTexture(GL_TEXTURE_BUFFER, 0);
    }
    gl::ActiveTexture(GL_TEXTURE0);
    return drawn;
}

} // namespace sf
