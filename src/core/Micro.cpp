// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Micro-displacement levels (core/Micro.h): the data, evaluation, Divide / Delete, picking, the renderer's
// shading, baking down and displacement maps. The brushes are in MicroSculpt.cpp, the commands in
// MicroCommands.cpp.
#include "core/Micro.h"
#include "core/FocusDetail.h"

#include "core/BakeImage.h"
#include "core/BVH.h"
#include "core/Multires.h"
#include "core/ThreadPool.h"
#include "core/TierConfig.h"

#include <algorithm>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <mutex>

#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace sf {

// ---------------------------------------------------------------------------
// MicroMesh
// ---------------------------------------------------------------------------

uint64_t MicroMesh::newSerial() {
    static std::atomic<uint64_t> next{1};
    return next++;
}

size_t MicroMesh::microVertices() const {
    size_t edges = size_t(baseTriangles) * 3 / 2;
    if (edgeNeighbor.size() == size_t(baseTriangles) * 3) {
        size_t boundary = 0;
        for (uint32_t e : edgeNeighbor) boundary += e == kNone;
        edges = (size_t(baseTriangles) * 3 + boundary) / 2;
    }
    const size_t interior = n >= 2 ? size_t(n - 1) * (n - 2) / 2 : 0;
    return size_t(baseVertices) + edges * (n - 1) + size_t(baseTriangles) * interior;
}

size_t MicroMesh::bytes() const {
    return disp.capacity() * sizeof(int16_t) + mask.capacity() + triRange.capacity() * sizeof(uint16_t) +
           edgeNeighbor.capacity() * sizeof(uint32_t) + dirtyBits.capacity() * sizeof(uint64_t);
}

void MicroMesh::clearDirty() {
    std::fill(dirtyBits.begin(), dirtyBits.end(), 0);
    allDirty = false;
    maskDirty = false;
    rescaled = false;
}

int MicroMesh::widen(float need) {
    if (!(need > rangeWorld())) return 1;
    int doublings = 2;
    while (need > 32767.0f * step * float(1 << doublings) && doublings < 12) ++doublings;
    const int div = 1 << doublings;
    poolFor(disp.size(), [&](size_t b, size_t e) {
        for (size_t i = b; i < e; ++i) disp[i] = int16_t(int(disp[i]) / div);
    }, 1 << 16);
    for (uint16_t& v : triRange) v = uint16_t(std::min(int(v) / div + 1, 32767));
    rangeMax = uint16_t(std::min(int(rangeMax) / div + 1, 32767));
    step *= float(div);
    rescaled = true;
    reach.all = true;
    ++revision;
    return div;
}

void MicroMesh::ensureTopology(const Mesh& base) {
    const size_t nt = base.triangleCount();
    if (edgeNeighbor.size() == nt * 3 && nt == baseTriangles) return;
    edgeNeighbor.assign(nt * 3, kNone);
    poolFor(nt, [&](size_t b, size_t e) {
        for (size_t t = b; t < e; ++t) {
            const glm::uvec3 tri = base.triangles[t];
            for (int k = 0; k < 3; ++k) {
                const uint32_t a = tri[k], c = tri[(k + 1) % 3];
                for (uint32_t u : base.vertexTriangles(a)) {
                    if (u == t) continue;
                    const glm::uvec3 o = base.triangles[u];
                    int f = -1;
                    for (int q = 0; q < 3 && f < 0; ++q)
                        if ((o[q] == a && o[(q + 1) % 3] == c) || (o[q] == c && o[(q + 1) % 3] == a)) f = q;
                    if (f >= 0) {
                        edgeNeighbor[t * 3 + size_t(k)] = (u << 2) | uint32_t(f);
                        break;
                    }
                }
            }
        }
    }, 1 << 12);
}

// ---------------------------------------------------------------------------
// Evaluation
// ---------------------------------------------------------------------------

void MicroTriangle::set(const Mesh& base, uint32_t tri, uint32_t segments) {
    const glm::uvec3 t = base.triangles[tri];
    uint8_t o[3] = {0, 1, 2};
    if (t[o[0]] > t[o[1]]) std::swap(o[0], o[1]);
    if (t[o[1]] > t[o[2]]) std::swap(o[1], o[2]);
    if (t[o[0]] > t[o[1]]) std::swap(o[0], o[1]);
    for (int k = 0; k < 3; ++k) {
        slot[k] = o[k];
        p[k] = base.positions[t[o[k]]];
        nrm[k] = base.normals[t[o[k]]];
    }
    n = segments;
    invN = 1.0f / float(segments);
}

// The same operations, in the same order, as the renderer's vertex shader (render/MicroRenderer.cpp): corners in
// ascending vertex order, so a point on a shared edge (whose third weight is exactly 0) comes out bit-identical
// from both triangles.
void MicroTriangle::evalWeights(const float wc[3], glm::vec3& surface, glm::vec3& normal) const {
    const float w0 = wc[slot[0]], w1 = wc[slot[1]], w2 = wc[slot[2]];
    glm::vec3 f = p[0] * w0;
    f += p[1] * w1;
    f += p[2] * w2;
    glm::vec3 nn = nrm[0] * w0;
    nn += nrm[1] * w1;
    nn += nrm[2] * w2;
    // Phong tessellation: the flat point pulled towards each corner's tangent plane.
    glm::vec3 pull = nrm[0] * (w0 * glm::dot(f - p[0], nrm[0]));
    pull += nrm[1] * (w1 * glm::dot(f - p[1], nrm[1]));
    pull += nrm[2] * (w2 * glm::dot(f - p[2], nrm[2]));
    surface = f - pull * MicroMesh::kPhong;
    const float len = glm::length(nn);
    normal = len > 1e-20f ? nn / len : glm::vec3(0.0f, 0.0f, 1.0f);
}

void MicroTriangle::eval(uint32_t i, uint32_t j, glm::vec3& surface, glm::vec3& normal) const {
    const float w[3] = {float(n - i - j) * invN, float(i) * invN, float(j) * invN};
    evalWeights(w, surface, normal);
}

glm::vec3 MicroTriangle::flat(uint32_t i, uint32_t j) const {
    const float wc[3] = {float(n - i - j) * invN, float(i) * invN, float(j) * invN};
    glm::vec3 f = p[0] * wc[slot[0]];
    f += p[1] * wc[slot[1]];
    f += p[2] * wc[slot[2]];
    return f;
}

MicroPatch microPatch(Mesh& m, uint32_t id) {
    MicroMesh& mm = mutableMicro(m);
    MicroPatch p;
    p.id = id;
    p.n = mm.n;
    p.heights = mm.disp.data() + size_t(id) * mm.samples;
    p.mask = mm.hasMask() ? mm.mask.data() + size_t(id) * mm.samples : nullptr;
    return p;
}

bool PatchPageTable::resident(uint32_t patch) const {
    if (!mm_.disp.mapped()) return true;
    return mm_.disp.rangeResident(size_t(patch) * mm_.samples, mm_.samples);
}

size_t PatchPageTable::residentPatches() const {
    if (!mm_.disp.mapped()) return mm_.baseTriangles;
    size_t count = 0;
    for (uint32_t t = 0; t < mm_.baseTriangles; ++t) count += resident(t);
    return count;
}

int microLevel(const Mesh& m) { return m.micro ? m.micro->level : 0; }
size_t microTriangleCount(const Mesh& m) { return m.micro ? m.micro->microTriangles() : 0; }
size_t microBytes(const Mesh& m) { return m.micro ? m.micro->bytes() : 0; }

std::string microText(const Mesh& m) {
    if (!m.micro) return {};
    char buf[64];
    std::snprintf(buf, sizeof buf, "Micro %d (%.1fM)", m.micro->level, double(m.micro->microTriangles()) / 1e6);
    return buf;
}

bool microMatches(const Mesh& m) {
    return m.micro && m.micro->baseTriangles == m.triangleCount() && m.micro->baseVertices == m.vertexCount();
}

MicroMesh& mutableMicro(Mesh& m) {
    if (m.micro.use_count() > 1) m.micro = std::make_shared<MicroMesh>(*m.micro);
    return *m.micro;
}

size_t microMaxTriangles() {
    size_t memory = 0;
#if defined(_WIN32)
    MEMORYSTATUSEX status{};
    status.dwLength = sizeof status;
    if (GlobalMemoryStatusEx(&status)) memory = size_t(status.ullTotalPhys);
#elif defined(_SC_PHYS_PAGES)
    const long pages = sysconf(_SC_PHYS_PAGES), page = sysconf(_SC_PAGESIZE);
    if (pages > 0 && page > 0) memory = size_t(pages) * size_t(page);
#endif
    // Per micro triangle about 0.6 grid points (level 4) x (2 bytes + 1 of mask + 6 on the GPU) + undo: ~6 bytes.
    return std::clamp<size_t>(memory / 10 * 5 / 6, 100'000'000, size_t(8'000'000'000));
}

int microLevelFor(const Mesh& m, size_t target) {
    const double nt = double(std::max<size_t>(m.triangleCount(), 1));
    int best = 1;
    double bestErr = 1e300;
    for (int l = 1; l <= MicroMesh::kMaxLevel; ++l) {
        const double err = std::abs(std::log(nt * std::pow(4.0, l) / double(std::max<size_t>(target, 1))));
        if (err < bestErr) bestErr = err, best = l;
    }
    return best;
}

glm::vec3 microPoint(const Mesh& m, uint32_t tri, uint32_t i, uint32_t j) {
    const MicroMesh& mm = *m.micro;
    MicroTriangle T;
    T.set(m, tri, mm.n);
    glm::vec3 s, nn;
    T.eval(i, j, s, nn);
    return s + nn * (float(mm.disp[size_t(tri) * mm.samples + MicroMesh::index(i, j, mm.n)]) * mm.step);
}

namespace {

// The micro triangle holding barycentric point w of a base triangle: its three grid points (i, j) and weights.
struct Cell {
    uint32_t i[3], j[3];
    float w[3];
};
Cell cellAt(const float wc[3], uint32_t n) {
    const float fi = std::clamp(wc[1], 0.0f, 1.0f) * float(n), fj = std::clamp(wc[2], 0.0f, 1.0f) * float(n);
    uint32_t i0 = std::min(uint32_t(fi), n - 1), j0 = std::min(uint32_t(fj), n - 1);
    if (i0 + j0 > n - 1) { // outside the last full cell along the hypotenuse
        if (fi >= fj) i0 = n - 1 - j0;
        else j0 = n - 1 - i0;
    }
    const float a = fi - float(i0), b = fj - float(j0);
    Cell c;
    if (a + b <= 1.0f || i0 + j0 + 2 > n) {
        c.i[0] = i0, c.j[0] = j0, c.i[1] = i0 + 1, c.j[1] = j0, c.i[2] = i0, c.j[2] = j0 + 1;
        c.w[0] = 1.0f - a - b, c.w[1] = a, c.w[2] = b;
    } else {
        c.i[0] = i0 + 1, c.j[0] = j0, c.i[1] = i0 + 1, c.j[1] = j0 + 1, c.i[2] = i0, c.j[2] = j0 + 1;
        c.w[0] = 1.0f - b, c.w[1] = a + b - 1.0f, c.w[2] = 1.0f - a;
    }
    return c;
}

} // namespace

glm::vec3 microPointAt(const Mesh& m, uint32_t tri, const float wc[3], glm::vec3* normal) {
    const MicroMesh& mm = *m.micro;
    MicroTriangle T;
    T.set(m, tri, mm.n);
    const Cell c = cellAt(wc, mm.n);
    glm::vec3 q[3];
    for (int k = 0; k < 3; ++k) {
        glm::vec3 s, nn;
        T.eval(c.i[k], c.j[k], s, nn);
        q[k] = s + nn * (float(mm.disp[size_t(tri) * mm.samples + MicroMesh::index(c.i[k], c.j[k], mm.n)]) * mm.step);
    }
    if (normal) {
        const glm::vec3 g = glm::cross(q[1] - q[0], q[2] - q[0]);
        const float len = glm::length(g);
        *normal = len > 0.0f ? g / len : glm::vec3(0.0f, 0.0f, 1.0f);
    }
    return q[0] * c.w[0] + q[1] * c.w[1] + q[2] * c.w[2];
}

// ---------------------------------------------------------------------------
// Divide / Delete
// ---------------------------------------------------------------------------

namespace {

// The most a base triangle's Phong surface leaves its plane (an upper bound).
float triangleBulge(const MicroTriangle& T) {
    float e = 0.0f;
    for (int k = 0; k < 3; ++k)
        for (int q = 0; q < 3; ++q) e = std::max(e, std::abs(glm::dot(T.p[q] - T.p[k], T.nrm[k])));
    return e * MicroMesh::kPhong;
}

} // namespace

void microRaiseBulge(Mesh& m) {
    if (!m.micro) return;
    MicroMesh& mm = *m.micro;
    const uint32_t nt = mm.baseTriangles, n = mm.n;
    const size_t block = 1 << 14;
    std::vector<float> bulge((nt + block - 1) / block, 0.0f);
    poolFor(nt, [&](size_t b, size_t e) {
        MicroTriangle T;
        float g = 0.0f;
        for (size_t t = b; t < e; ++t) {
            T.set(m, uint32_t(t), n);
            g = std::max(g, triangleBulge(T));
        }
        bulge[b / block] = g;
    }, block);
    for (float g : bulge) mm.bulgeMax = std::max(mm.bulgeMax, g);
}

bool microWantsOutOfCore(size_t points) {
    if (const char* e = std::getenv("SF_MICRO_OUT_OF_CORE"); e && *e) return std::string(e) == "1";
    if (const size_t limit = currentTierConfig().outOfCorePoints) return points > limit;
    size_t memory = 0;
#if defined(_SC_PHYS_PAGES)
    const long pages = sysconf(_SC_PHYS_PAGES), page = sysconf(_SC_PAGESIZE);
    if (pages > 0 && page > 0) memory = size_t(pages) * size_t(page);
#endif
    return memory && points * 2 > memory / 4;
}

bool microDivide(Mesh& m, int level, std::string& error, int outOfCore) {
    if (level < 1 || level > MicroMesh::kMaxLevel) {
        error = "Micro level must be 1 to " + std::to_string(MicroMesh::kMaxLevel);
        return false;
    }
    const size_t nt = m.triangleCount();
    if (nt == 0) {
        error = "Nothing to divide";
        return false;
    }
    if (m.micro) {
        error = "This Part already has a micro level (Delete micro or Bake it first)";
        return false;
    }
    if (subdivLevelCount(m) > 0 && subdivCurrentLevel(m) != subdivLevelCount(m) - 1) {
        error = "Go to the highest subdivision level first: the micro level goes on top of it";
        return false;
    }
    if (!m.forgespheres.empty()) {
        error = "Forge Sphere Parts can't have a micro level (make an adaptive skin first)";
        return false;
    }
    if (!m.layers.empty()) {
        error = "Sculpt layers can't carry a micro level yet: bake or delete the layers first";
        return false;
    }
    if (nt > (size_t(1) << 30)) {
        error = "Too many base triangles for a micro level";
        return false;
    }
    const size_t micro = nt << (2 * level);
    if (micro > microMaxTriangles()) {
        char buf[200];
        std::snprintf(buf, sizeof buf, "%.0fM micro triangles is more than this computer allows (%.0fM): pick a lower micro level",
                      double(micro) / 1e6, double(microMaxTriangles()) / 1e6);
        error = buf;
        return false;
    }
    if (m.normals.size() != m.vertexCount()) m.recomputeAllNormals();
    auto mm = std::make_shared<MicroMesh>();
    mm->level = level;
    mm->n = 1u << level;
    mm->samples = (mm->n + 1) * (mm->n + 2) / 2;
    mm->baseTriangles = uint32_t(nt);
    mm->baseVertices = uint32_t(m.vertexCount());
    if (outOfCore < 0 ? microWantsOutOfCore(nt * mm->samples) : outOfCore == 1) {
        std::string why;
        mm->disp.assignMapped(nt * mm->samples, why); // (falls back to memory)
    } else {
        mm->disp.assign(nt * mm->samples, 0);
    }
    mm->triRange.assign(nt, 0);
    mm->dirtyBits.assign((nt + 63) / 64, 0);
    mm->ensureTopology(m);
    // Edge length and bulge, in parallel blocks (combined in order: deterministic).
    const size_t block = 1 << 14, blocks = (nt + block - 1) / block;
    std::vector<double> edgeSum(blocks, 0.0);
    std::vector<float> bulge(blocks, 0.0f);
    poolFor(nt, [&](size_t b, size_t e) {
        double s = 0.0;
        float g = 0.0f;
        MicroTriangle T;
        for (size_t t = b; t < e; ++t) {
            T.set(m, uint32_t(t), mm->n);
            s += double(glm::distance(T.p[0], T.p[1]) + glm::distance(T.p[1], T.p[2]) + glm::distance(T.p[2], T.p[0]));
            g = std::max(g, triangleBulge(T));
        }
        edgeSum[b / block] = s;
        bulge[b / block] = g;
    }, block);
    double total = 0.0;
    for (size_t i = 0; i < blocks; ++i) {
        total += edgeSum[i];
        mm->bulgeMax = std::max(mm->bulgeMax, bulge[i]);
    }
    const float avgEdge = float(total / double(nt * 3));
    mm->baseEdge = avgEdge;
    // 1/4096 of a base edge per unit: +/-8 base edges of range, steps far below a micro edge (1/256 of one at
    // level 4). Brushes widen the range when they need more (MicroStroke).
    mm->step = std::max(avgEdge / 4096.0f, 1e-12f);
    mm->serial = MicroMesh::newSerial();
    mm->markAllDirty();
    m.micro = std::move(mm);
    m.topologyDirty = true; // the renderer draws it differently
    return true;
}

bool microAttach(Mesh& m, int level, float step, MicroStore disp, std::vector<uint8_t> mask, std::string& error) {
    if (level < 1 || level > MicroMesh::kMaxLevel || !(step > 0.0f) || !std::isfinite(step)) {
        error = "Project micro level is invalid";
        return false;
    }
    const size_t nt = m.triangleCount();
    const uint32_t n = 1u << level, S = (n + 1) * (n + 2) / 2;
    if (disp.size() != nt * S || (!mask.empty() && mask.size() != disp.size())) {
        error = "Project micro level doesn't fit its mesh";
        return false;
    }
    if (m.normals.size() != m.vertexCount()) m.recomputeAllNormals();
    auto mm = std::make_shared<MicroMesh>();
    mm->level = level;
    mm->n = n;
    mm->samples = S;
    mm->step = step;
    mm->baseTriangles = uint32_t(nt);
    mm->baseVertices = uint32_t(m.vertexCount());
    mm->disp = std::move(disp);
    mm->mask = std::move(mask);
    mm->triRange.assign(nt, 0);
    mm->dirtyBits.assign((nt + 63) / 64, 0);
    const size_t block = 1 << 12, blocks = (nt + block - 1) / block;
    std::vector<float> bulge(blocks, 0.0f);
    poolFor(nt, [&](size_t b, size_t e) {
        MicroTriangle T;
        float g = 0.0f;
        for (size_t t = b; t < e; ++t) {
            const int16_t* d = mm->disp.data() + t * S;
            int r = 0;
            for (uint32_t k = 0; k < S; ++k) r = std::max(r, std::abs(int(d[k])));
            mm->triRange[t] = uint16_t(std::min(r, 32767));
            T.set(m, uint32_t(t), n);
            g = std::max(g, triangleBulge(T));
        }
        bulge[b / block] = g;
    }, block);
    for (float g : bulge) mm->bulgeMax = std::max(mm->bulgeMax, g);
    {
        double e = 0.0;
        for (size_t t = 0; t < nt; ++t) {
            const glm::uvec3 tr = m.triangles[t];
            e += double(glm::distance(m.positions[tr.x], m.positions[tr.y]) + glm::distance(m.positions[tr.y], m.positions[tr.z]) +
                        glm::distance(m.positions[tr.z], m.positions[tr.x]));
        }
        mm->baseEdge = float(e / double(std::max<size_t>(nt * 3, 1)));
    }
    for (uint16_t r : mm->triRange) mm->rangeMax = std::max(mm->rangeMax, r);
    mm->ensureTopology(m);
    mm->serial = MicroMesh::newSerial();
    mm->markAllDirty();
    m.micro = std::move(mm);
    m.topologyDirty = true;
    return true;
}

void microDelete(Mesh& m) {
    if (!m.micro) return;
    m.micro.reset();
    m.topologyDirty = true;
}

// ---------------------------------------------------------------------------
// Picking
// ---------------------------------------------------------------------------

namespace {

bool rayTriangle(const glm::vec3& o, const glm::vec3& d, const glm::vec3& a, const glm::vec3& b, const glm::vec3& c, float& t) {
    const glm::vec3 e1 = b - a, e2 = c - a, pv = glm::cross(d, e2);
    const float det = glm::dot(e1, pv);
    if (std::abs(det) < 1e-30f) return false;
    const float inv = 1.0f / det;
    const glm::vec3 tv = o - a;
    const float u = glm::dot(tv, pv) * inv;
    if (u < 0.0f || u > 1.0f) return false;
    const glm::vec3 qv = glm::cross(tv, e1);
    const float v = glm::dot(d, qv) * inv;
    if (v < 0.0f || u + v > 1.0f) return false;
    t = glm::dot(e2, qv) * inv;
    return t > 0.0f;
}

} // namespace

namespace {

// Brings m's Reach up to date with `bvh` (call with reach.lock held): a full pass when the BVH, the step or
// everything changed, else the triangles marked since raise their leaves and ancestors.
void syncReach(const Mesh& m, const BVH& bvh) {
    const MicroMesh& mm = *m.micro;
    MicroMesh::Reach& r = mm.reach;
    const size_t words = (size_t(mm.baseTriangles) + 63) / 64;
    const bool full = r.all || r.bvh != &bvh || r.structure != bvh.structureSerial() || r.step != mm.step ||
                      r.pad.size() != bvh.nodeCount() || r.bits.size() != words;
    if (full) {
        r.bvh = &bvh, r.structure = bvh.structureSerial(), r.step = mm.step, r.all = false;
        r.bits.assign(words, 0);
        const size_t nodes = bvh.nodeCount();
        r.pad.assign(nodes, 0.0f);
        poolFor(nodes, [&](size_t b, size_t e) {
            for (size_t i = b; i < e; ++i) {
                uint16_t most = 0;
                for (uint32_t t : bvh.leafTriangles(uint32_t(i)))
                    if (t < mm.triRange.size()) most = std::max(most, mm.triRange[t]);
                r.pad[i] = float(most) * mm.step;
            }
        }, 1 << 12);
        for (size_t i = nodes; i-- > 0;)
            if (bvh.isInner(uint32_t(i))) {
                const uint32_t c = bvh.leftChild(uint32_t(i));
                r.pad[i] = std::max(r.pad[c], r.pad[c + 1]);
            }
        return;
    }
    for (size_t w = 0; w < words; ++w) {
        if (!r.bits[w]) continue;
        const uint64_t bits = std::atomic_ref<uint64_t>(r.bits[w]).exchange(0);
        for (uint64_t b = bits; b; b &= b - 1) {
            const uint32_t t = uint32_t(w * 64 + size_t(std::countr_zero(b)));
            const float v = float(mm.triRange[t]) * mm.step;
            for (uint32_t node = bvh.leafOf(t); node != ~0u && r.pad[node] < v; node = bvh.parentOf(node)) {
                r.pad[node] = v;
                if (node == 0) break;
            }
        }
    }
}

} // namespace

void microQueryTriangles(const Mesh& m, const BVH& bvh, const glm::vec3& center, float radius, std::vector<uint32_t>& out) {
    const MicroMesh& mm = *m.micro;
    std::lock_guard<std::mutex> hold(mm.reach.lock);
    syncReach(m, bvh);
    bvh.queryTrianglesPadded(m, center, radius, mm.reach.pad, mm.bulgeMax, out);
}

void microRayTriangles(const Mesh& m, const BVH& bvh, const glm::vec3& origin, const glm::vec3& dir, std::vector<uint32_t>& out) {
    const MicroMesh& mm = *m.micro;
    std::lock_guard<std::mutex> hold(mm.reach.lock);
    syncReach(m, bvh);
    bvh.rayTrianglesPadded(m, origin, dir, mm.reach.pad, mm.bulgeMax, out);
}

bool microRaycast(const Mesh& m, const BVH& bvh, const glm::vec3& origin, const glm::vec3& dir, RayHit& hit) {
    if (!microMatches(m)) return bvh.raycast(m, origin, dir, hit);
    const MicroMesh& mm = *m.micro;
    RayHit base;
    if (!bvh.raycast(m, origin, dir, base)) return false;
    std::vector<uint32_t> cand;
    const auto traceStart = std::chrono::steady_clock::now();
    microRayTriangles(m, bvh, origin, dir, cand);
    static const bool trace = [] { const char* e = std::getenv("SF_MICRO_TRACE"); return e && e[0] == '1'; }();
    if (trace)
        std::fprintf(stderr, "micropick %zu candidates in %.1f ms (pad root %.4f, bulgeMax %.4f)\n", cand.size(),
                     std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - traceStart).count(),
                     mm.reach.pad.empty() ? 0.0 : double(mm.reach.pad[0]), double(mm.bulgeMax));
    // Only the triangles whose own reach (their bulge and displacement range) the ray passes through.
    const glm::vec3 inv(1.0f / (std::abs(dir.x) > 1e-20f ? dir.x : 1e-20f), 1.0f / (std::abs(dir.y) > 1e-20f ? dir.y : 1e-20f),
                        1.0f / (std::abs(dir.z) > 1e-20f ? dir.z : 1e-20f));
    std::erase_if(cand, [&](uint32_t t) {
        if (t == base.triangle) return false;
        MicroTriangle T;
        T.set(m, t, mm.n);
        const float grow = triangleBulge(T) + float(mm.triRange[t]) * mm.step;
        const glm::vec3 lo = glm::min(T.p[0], glm::min(T.p[1], T.p[2])) - grow, hi = glm::max(T.p[0], glm::max(T.p[1], T.p[2])) + grow;
        const glm::vec3 t0 = (lo - origin) * inv, t1 = (hi - origin) * inv;
        const glm::vec3 tn = glm::min(t0, t1), tf = glm::max(t0, t1);
        const float enter = std::max({tn.x, tn.y, tn.z, 0.0f}), leave = std::min({tf.x, tf.y, tf.z});
        return enter > leave;
    });
    if (std::find(cand.begin(), cand.end(), base.triangle) == cand.end()) cand.push_back(base.triangle);
    constexpr size_t kMaxCandidates = 4096;
    if (cand.size() > kMaxCandidates) { // a huge displacement range: rank by distance and keep the nearest
        std::nth_element(cand.begin(), cand.begin() + kMaxCandidates, cand.end(), [&](uint32_t a, uint32_t b) {
            auto c = [&](uint32_t t) {
                const glm::uvec3 tr = m.triangles[t];
                return glm::distance(base.position, (m.positions[tr.x] + m.positions[tr.y] + m.positions[tr.z]) / 3.0f);
            };
            return c(a) < c(b);
        });
        cand.resize(kMaxCandidates);
    }
    const uint32_t n = mm.n, S = mm.samples;
    struct Best {
        float t = std::numeric_limits<float>::max();
        uint32_t tri = 0;
        glm::vec3 a{0.0f}, b{0.0f}, c{0.0f};
    };
    const size_t block = 4;
    std::vector<Best> best((cand.size() + block - 1) / block);
    poolFor(cand.size(), [&](size_t b0, size_t e0) {
        thread_local std::vector<glm::vec3> P;
        P.resize(S);
        Best bb;
        MicroTriangle T;
        for (size_t ci = b0; ci < e0; ++ci) {
            const uint32_t tri = cand[ci];
            if (m.triangleHidden(tri)) continue;
            T.set(m, tri, n);
            const int16_t* g = mm.disp.data() + size_t(tri) * S;
            uint32_t idx = 0;
            for (uint32_t j = 0; j <= n; ++j)
                for (uint32_t i = 0; i + j <= n; ++i, ++idx) {
                    glm::vec3 s, nn;
                    T.eval(i, j, s, nn);
                    P[idx] = s + nn * (float(g[idx]) * mm.step);
                }
            for (uint32_t j = 0; j < n; ++j) {
                const uint32_t row = MicroMesh::index(0, j, n), next = MicroMesh::index(0, j + 1, n);
                for (uint32_t i = 0; i + j < n; ++i) {
                    float t;
                    const glm::vec3 &a = P[row + i], &b = P[row + i + 1], &c = P[next + i];
                    if (rayTriangle(origin, dir, a, b, c, t) && t < bb.t) bb = {t, tri, a, b, c};
                    if (i + j + 1 < n) {
                        const glm::vec3 &d = P[next + i + 1];
                        if (rayTriangle(origin, dir, b, d, c, t) && t < bb.t) bb = {t, tri, b, d, c};
                    }
                }
            }
        }
        best[b0 / block] = bb;
    }, block, 2);
    Best win;
    for (const Best& b : best)
        if (b.t < win.t) win = b;
    if (win.t == std::numeric_limits<float>::max()) {
        // Missed every micro triangle near the base hit (a grazing ray): the micro point under the base hit.
        const glm::uvec3 tr = m.triangles[base.triangle];
        const glm::vec3 a = m.positions[tr.x], b = m.positions[tr.y], c = m.positions[tr.z];
        const glm::vec3 v0 = b - a, v1 = c - a, v2 = base.position - a;
        const float d00 = glm::dot(v0, v0), d01 = glm::dot(v0, v1), d11 = glm::dot(v1, v1), d20 = glm::dot(v2, v0),
                    d21 = glm::dot(v2, v1), den = d00 * d11 - d01 * d01;
        float w[3] = {1.0f, 0.0f, 0.0f};
        if (std::abs(den) > 0.0f) {
            w[1] = (d11 * d20 - d01 * d21) / den;
            w[2] = (d00 * d21 - d01 * d20) / den;
            w[0] = 1.0f - w[1] - w[2];
        }
        hit = base;
        hit.position = microPointAt(m, base.triangle, w, &hit.normal);
        hit.t = glm::dot(hit.position - origin, dir);
        return true;
    }
    hit.t = win.t;
    hit.triangle = win.tri;
    hit.position = origin + dir * win.t;
    const glm::vec3 g = glm::cross(win.b - win.a, win.c - win.a);
    const float len = glm::length(g);
    hit.normal = len > 0.0f ? g / len : base.normal;
    return true;
}

bool surfaceRaycast(const Mesh& m, const BVH& bvh, const glm::vec3& origin, const glm::vec3& dir, RayHit& hit) {
    if (m.micro && microMatches(m)) return microRaycast(m, bvh, origin, dir, hit);
    return bvh.raycast(m, origin, dir, hit);
}

// ---------------------------------------------------------------------------
// Shading for the renderer
// ---------------------------------------------------------------------------

namespace {

constexpr int kPartialFloats = 5; // normal sum (3), neighbour displacement sum, neighbour count

uint32_t encodeShade(glm::vec3 nrm, float curvature, uint8_t mask) {
    const float l1 = std::abs(nrm.x) + std::abs(nrm.y) + std::abs(nrm.z);
    glm::vec2 e(0.0f);
    if (l1 > 0.0f) {
        nrm /= l1;
        e = glm::vec2(nrm.x, nrm.y);
        if (nrm.z < 0.0f)
            e = glm::vec2((1.0f - std::abs(nrm.y)) * (nrm.x >= 0.0f ? 1.0f : -1.0f),
                          (1.0f - std::abs(nrm.x)) * (nrm.y >= 0.0f ? 1.0f : -1.0f));
    }
    // (rounding by +0.5 and truncation: the values are >= 0 there, and lround costs more than the rest of a point)
    auto q = [](float v) { return uint32_t(std::clamp((v * 0.5f + 0.5f) * 255.0f + 0.5f, 0.0f, 255.0f)); };
    const float c = std::clamp(curvature, -1.0f, 1.0f);
    const float s = (c >= 0.0f ? 1.0f : -1.0f) * std::sqrt(std::abs(c));
    const uint32_t cq = uint32_t(std::clamp(s * 127.0f + 128.5f, 1.0f, 255.0f));
    return q(e.x) | q(e.y) << 8 | cq << 16 | uint32_t(mask) << 24;
}

// The grid of one base triangle: positions, then per point the sum of the face normals of its micro triangles
// (area weighted) and of its neighbours' displacements, within this triangle.
struct GridWork {
    std::vector<glm::vec3> pos, nsum;
    std::vector<float> dsum, cnt;
    void compute(const Mesh& m, const MicroMesh& mm, uint32_t tri) {
        const uint32_t n = mm.n, S = mm.samples;
        pos.resize(S);
        nsum.assign(S, glm::vec3(0.0f));
        dsum.assign(S, 0.0f);
        cnt.assign(S, 0.0f);
        MicroTriangle T;
        T.set(m, tri, n);
        const int16_t* g = mm.disp.data() + size_t(tri) * S;
        uint32_t idx = 0;
        for (uint32_t j = 0; j <= n; ++j)
            for (uint32_t i = 0; i + j <= n; ++i, ++idx) {
                glm::vec3 s, nn;
                T.eval(i, j, s, nn);
                pos[idx] = s + nn * (float(g[idx]) * mm.step);
            }
        for (uint32_t j = 0; j < n; ++j) {
            const uint32_t row = MicroMesh::index(0, j, n), next = MicroMesh::index(0, j + 1, n);
            for (uint32_t i = 0; i + j < n; ++i) {
                const uint32_t a = row + i, b = row + i + 1, c = next + i;
                face(a, b, c, g, true);
                if (i + j + 1 < n) face(b, next + i + 1, c, g, false);
            }
        }
    }
    // The sums at some edge points only (a triangle of the outer ring: the points it shares with the ones written):
    // edges e (bit e: its points between the corners) and corners c (bit 3 + c). The micro triangles touching
    // them alone, added in the same order as compute(), so those sums come out bit-identical to a whole grid's
    // at a fraction of its cost.
    void computePart(const Mesh& m, const MicroMesh& mm, uint32_t tri, unsigned parts) {
        const uint32_t n = mm.n, S = mm.samples;
        pos.resize(S);
        nsum.resize(S);
        dsum.resize(S);
        cnt.resize(S);
        need.assign(S, 0);
        for (int e = 0; e < 3; ++e) {
            uint32_t i, j;
            if (parts & (8u << e)) MicroMesh::edgePoint(e, 0, n, i, j), need[MicroMesh::index(i, j, n)] = 1;
            if (parts & (1u << e))
                for (uint32_t k = 1; k < n; ++k) MicroMesh::edgePoint(e, k, n, i, j), need[MicroMesh::index(i, j, n)] = 1;
        }
        MicroTriangle T;
        T.set(m, tri, n);
        const int16_t* g = mm.disp.data() + size_t(tri) * S;
        auto at = [&](uint32_t i, uint32_t j) { // (each point once, the first time a triangle needs it)
            const uint32_t idx = MicroMesh::index(i, j, n);
            if (need[idx] & 2) return idx;
            need[idx] |= 2;
            glm::vec3 s, nn;
            T.eval(i, j, s, nn);
            pos[idx] = s + nn * (float(g[idx]) * mm.step);
            nsum[idx] = glm::vec3(0.0f), dsum[idx] = 0.0f, cnt[idx] = 0.0f; // (the others aren't read)
            return idx;
        };
        // (the micro triangles that can touch an edge: row 0, and the first and the last two of every other row)
        for (uint32_t j = 0; j < n; ++j) {
            const uint32_t row = MicroMesh::index(0, j, n), next = MicroMesh::index(0, j + 1, n), last = n - j;
            for (uint32_t i = 0; i < last; i = j == 0 || i + 3 >= last ? i + 1 : last - 2) {
                const uint32_t a = row + i, b = row + i + 1, c = next + i;
                if ((need[a] | need[b] | need[c]) & 1) face(at(i, j), at(i + 1, j), at(i, j + 1), g, true);
                if (i + j + 1 < n && ((need[b] | need[next + i + 1] | need[c]) & 1)) face(at(i + 1, j), at(i + 1, j + 1), at(i, j + 1), g, false);
            }
        }
    }

private:
    std::vector<uint8_t> need; // computePart: 1 = a wanted sum, 2 = its position computed
    // Micro triangle a b c (counter-clockwise) into the sums of its corners; `up` also adds its lattice edges a-b,
    // a-c, b-c (each once: every edge belongs to one upward triangle).
    void face(uint32_t a, uint32_t b, uint32_t c, const int16_t* g, bool up) {
        const glm::vec3 f = glm::cross(pos[b] - pos[a], pos[c] - pos[a]);
        nsum[a] += f, nsum[b] += f, nsum[c] += f;
        if (!up) return;
        const float da = g[a], db = g[b], dc = g[c];
        dsum[a] += db + dc, dsum[b] += da + dc, dsum[c] += da + db;
        cnt[a] += 2.0f, cnt[b] += 2.0f, cnt[c] += 2.0f;
    }
};

} // namespace

void MicroShader::shade(const Mesh& m, std::span<const uint32_t> dirty, bool all, std::vector<uint32_t>& written,
                        const std::function<uint32_t*(uint32_t tri)>& out, const Prepare& prepare, bool grow,
                        std::vector<uint32_t>* outer) {
    written.clear();
    if (outer) outer->clear();
    if (!m.micro && !level_) return;
    if (!all) {
        shadeSet(m, dirty, grow, written, out, prepare, grow ? nullptr : outer);
        return;
    }
    // Everything: in runs of triangles (neighbours by index on levels made by Divide), each with the ring its
    // shared points need, so the edge sums stay a few MB instead of 20 bytes per edge point of the whole mesh.
    const uint32_t nt = (level_ ? level_ : m.micro.get())->baseTriangles;
    constexpr uint32_t kRun = 1u << 16;
    std::vector<uint32_t> run;
    for (uint32_t b = 0; b < nt; b += kRun) {
        run.resize(std::min(kRun, nt - b));
        for (uint32_t i = 0; i < run.size(); ++i) run[i] = b + i;
        shadeSet(m, run, false, written, out, prepare);
    }
}

void MicroShader::shadeLevel(const Mesh& base, const MicroMesh& level, std::span<const uint32_t> dirty, bool all,
                             std::vector<uint32_t>& written, const std::function<uint32_t*(uint32_t tri)>& out, bool grow) {
    level_ = &level;
    shade(base, dirty, all, written, out, {}, grow);
    level_ = nullptr;
}

void MicroShader::shadeSet(const Mesh& m, std::span<const uint32_t> dirty, bool grow, std::vector<uint32_t>& written,
                           const std::function<uint32_t*(uint32_t tri)>& out, const Prepare& prepare, std::vector<uint32_t>* outer) {
    const MicroMesh& mm = level_ ? *level_ : *m.micro;
    const uint32_t n = mm.n, S = mm.samples, nt = mm.baseTriangles;
    if (mm.edgeNeighbor.size() != size_t(nt) * 3) return; // (ensureTopology first)
    if (stamp_.size() != nt) {
        stamp_.assign(nt, 0);
        slot_.assign(nt, 0);
        epoch_ = 0;
    }
    if (vstamp_.size() != m.vertexCount()) vstamp_.assign(m.vertexCount(), 0);
    if (epoch_ > 0xFFFFFF00u) {
        std::fill(stamp_.begin(), stamp_.end(), 0);
        std::fill(vstamp_.begin(), vstamp_.end(), 0);
        epoch_ = 0;
    }
    // R = the triangles to write: dirty ones and those sharing a vertex; R2 = R and those sharing a vertex with
    // R (whose edge sums the shared points need). stamp_: epoch + 1 = in R, epoch + 2 = only in R2.
    const uint32_t inR = ++epoch_;
    const uint32_t inR2 = ++epoch_;
    auto vertexRing = [&](const std::vector<uint32_t>& from, std::vector<uint32_t>& into, uint32_t mark, uint32_t vmark) {
        for (uint32_t t : from) {
            const glm::uvec3 tri = m.triangles[t];
            for (int k = 0; k < 3; ++k) {
                if (vstamp_[tri[k]] == vmark) continue;
                vstamp_[tri[k]] = vmark;
                for (uint32_t u : m.vertexTriangles(tri[k]))
                    if (stamp_[u] != inR && stamp_[u] != mark) {
                        stamp_[u] = mark;
                        into.push_back(u);
                    }
            }
        }
    };
    ring_.clear();
    ring2_.clear();
    for (uint32_t t : dirty)
        if (t < nt && stamp_[t] != inR) stamp_[t] = inR, ring_.push_back(t);
    if (grow) {
        seed_.assign(ring_.begin(), ring_.end());
        grown_.clear();
        const uint32_t v1 = ++epoch_;
        vertexRing(seed_, grown_, inR, v1); // (marks them inR directly: they're written too)
        ring_.insert(ring_.end(), grown_.begin(), grown_.end());
    }
    const uint32_t v2 = ++epoch_;
    vertexRing(ring_, ring2_, inR2, v2);
    if (outer) outer->insert(outer->end(), ring2_.begin(), ring2_.end());
    for (size_t i = 0; i < ring_.size(); ++i) slot_[ring_[i]] = uint32_t(i);
    for (size_t i = 0; i < ring2_.size(); ++i) slot_[ring2_[i]] = uint32_t(ring_.size() + i);
    if (prepare) prepare(ring_);
    const size_t slots = ring_.size() + ring2_.size();
    const size_t edgePoints = size_t(3) * n;
    partial_.resize(slots * edgePoints * kPartialFloats);
    auto tileOf = [&](size_t i) { return i < ring_.size() ? ring_[i] : ring2_[i - ring_.size()]; };
    auto microEdge = [&](uint32_t tri) {
        const glm::uvec3 t = m.triangles[tri];
        const float e = glm::distance(m.positions[t.x], m.positions[t.y]) + glm::distance(m.positions[t.y], m.positions[t.z]) +
                        glm::distance(m.positions[t.z], m.positions[t.x]);
        return std::max(e / (3.0f * float(n)), 1e-20f);
    };
    auto finish = [&](const glm::vec3& ns, float ds, float c, float d, float h, uint8_t mk) {
        const float curv = c > 0.0f ? (ds / c - d) * mm.step / h : 0.0f;
        return encodeShade(ns, curv, mk); // (unnormalized: the encoding divides by its L1 length; zero = +z either way)
    };
    // Phase 1: every R2 triangle's edge partials; R triangles' interior points written.
    poolFor(slots, [&](size_t b, size_t e) {
        thread_local GridWork w;
        for (size_t s = b; s < e; ++s) {
            const uint32_t tri = tileOf(s);
            if (s < ring_.size()) {
                w.compute(m, mm, tri);
            } else { // the outer ring: the sums at the points it shares with R (whose vertices are marked v2)
                const glm::uvec3 tv = m.triangles[tri];
                unsigned parts = 0;
                for (int k = 0; k < 3; ++k) {
                    const bool here = vstamp_[tv[k]] == v2, next = vstamp_[tv[(k + 1) % 3]] == v2;
                    parts |= (here ? 8u << k : 0u) | (here && next ? 1u << k : 0u);
                }
                w.computePart(m, mm, tri, parts);
            }
            float* part = partial_.data() + s * edgePoints * kPartialFloats;
            for (int ed = 0; ed < 3; ++ed)
                for (uint32_t k = 0; k < n; ++k) {
                    uint32_t i, j;
                    MicroMesh::edgePoint(ed, k, n, i, j);
                    const uint32_t idx = MicroMesh::index(i, j, n);
                    float* p = part + (size_t(ed) * n + k) * kPartialFloats;
                    p[0] = w.nsum[idx].x, p[1] = w.nsum[idx].y, p[2] = w.nsum[idx].z, p[3] = w.dsum[idx], p[4] = w.cnt[idx];
                }
            if (s >= ring_.size()) continue;
            uint32_t* dst = out(tri);
            const int16_t* g = mm.disp.data() + size_t(tri) * S;
            const uint8_t* mk = mm.hasMask() ? mm.mask.data() + size_t(tri) * S : nullptr;
            const float h = microEdge(tri);
            for (uint32_t j = 1; j + 1 < n; ++j)
                for (uint32_t i = 1; i + j < n; ++i) {
                    const uint32_t idx = MicroMesh::index(i, j, n);
                    dst[idx] = finish(w.nsum[idx], w.dsum[idx], w.cnt[idx], float(g[idx]), h, mk ? mk[idx] : 0);
                }
        }
    }, 16);
    // Phase 2: R triangles' edge and corner points, summed with the other triangles holding them.
    poolFor(ring_.size(), [&](size_t b, size_t e) {
        for (size_t s = b; s < e; ++s) {
            const uint32_t tri = ring_[s];
            uint32_t* dst = out(tri);
            const int16_t* g = mm.disp.data() + size_t(tri) * S;
            const uint8_t* mk = mm.hasMask() ? mm.mask.data() + size_t(tri) * S : nullptr;
            const float h = microEdge(tri);
            const float* own = partial_.data() + s * edgePoints * kPartialFloats;
            const glm::uvec3 ttri = m.triangles[tri];
            for (int ed = 0; ed < 3; ++ed) {
                const uint32_t nb = mm.edgeNeighbor[size_t(tri) * 3 + size_t(ed)];
                const float* other = nullptr;
                bool reversed = true;
                int f = 0;
                if (nb != MicroMesh::kNone && (stamp_[nb >> 2] == inR || stamp_[nb >> 2] == inR2)) {
                    const uint32_t u = nb >> 2;
                    f = int(nb & 3);
                    other = partial_.data() + size_t(slot_[u]) * edgePoints * kPartialFloats;
                    reversed = m.triangles[u][f] != ttri[ed]; // usual: the neighbour runs the edge the other way
                }
                for (uint32_t k = 1; k < n; ++k) {
                    const float* p = own + (size_t(ed) * n + k) * kPartialFloats;
                    glm::vec3 ns(p[0], p[1], p[2]);
                    float ds = p[3], c = p[4];
                    if (other) {
                        const float* q = other + (size_t(f) * n + (reversed ? n - k : k)) * kPartialFloats;
                        ns += glm::vec3(q[0], q[1], q[2]);
                        ds += q[3];
                        c += q[4];
                    }
                    uint32_t i, j;
                    MicroMesh::edgePoint(ed, k, n, i, j);
                    const uint32_t idx = MicroMesh::index(i, j, n);
                    dst[idx] = finish(ns, ds, c, float(g[idx]), h, mk ? mk[idx] : 0);
                }
                // The corner at the start of this edge: every triangle around the vertex.
                const uint32_t v = ttri[ed];
                glm::vec3 ns(0.0f);
                float ds = 0.0f, c = 0.0f;
                for (uint32_t u : m.vertexTriangles(v)) {
                    if (stamp_[u] != inR && stamp_[u] != inR2) continue;
                    const glm::uvec3 ut = m.triangles[u];
                    const int corner = ut.x == v ? 0 : ut.y == v ? 1 : 2;
                    const float* q = partial_.data() + (size_t(slot_[u]) * edgePoints + size_t(corner) * n) * kPartialFloats;
                    ns += glm::vec3(q[0], q[1], q[2]);
                    ds += q[3];
                    c += q[4];
                }
                uint32_t i, j;
                MicroMesh::edgePoint(ed, 0, n, i, j);
                const uint32_t idx = MicroMesh::index(i, j, n);
                dst[idx] = finish(ns, ds, c, float(g[idx]), h, mk ? mk[idx] : 0);
            }
        }
    }, 16);
    written.insert(written.end(), ring_.begin(), ring_.end());
}

// ---------------------------------------------------------------------------
// Undo
// ---------------------------------------------------------------------------

void MicroUndo::swapWith(Mesh& m) {
    if (swapWhole) {
        std::swap(m.micro, whole);
        if (m.micro) {
            m.micro->markAllDirty();
            m.micro->ensureTopology(m);
            if (m.micro->dirtyBits.size() != (size_t(m.micro->baseTriangles) + 63) / 64)
                m.micro->dirtyBits.assign((size_t(m.micro->baseTriangles) + 63) / 64, 0);
        }
        m.topologyDirty = true;
        return;
    }
    if (!m.micro || !microMatches(m) || m.micro->samples != samples) return;
    MicroMesh& mm = mutableMicro(m);
    if (mm.focus) mm.focus->ensureFull(mm, tris, true); // (undo writes full tiles only)
    const size_t S = samples;
    // Values kept in another step size (the range grew since): convert to the current one.
    if (!journal && step != mm.step && step > 0.0f) {
        const float r = step / mm.step;
        for (int16_t& v : disp) v = int16_t(std::clamp(std::lround(float(v) * r), -32767L, 32767L));
        step = mm.step;
    }
    if (maskAdded) {
        if (mm.hasMask()) {
            poolFor(tris.size(), [&](size_t b, size_t e) {
                for (size_t i = b; i < e; ++i)
                    std::swap_ranges(mask.begin() + std::ptrdiff_t(i * S), mask.begin() + std::ptrdiff_t((i + 1) * S),
                                     mm.mask.begin() + std::ptrdiff_t(size_t(tris[i]) * S));
            }, 64);
            std::vector<uint8_t>().swap(mm.mask);
        } else {
            mm.mask.assign(mm.disp.size(), 0);
            poolFor(tris.size(), [&](size_t b, size_t e) {
                for (size_t i = b; i < e; ++i)
                    std::swap_ranges(mask.begin() + std::ptrdiff_t(i * S), mask.begin() + std::ptrdiff_t((i + 1) * S),
                                     mm.mask.begin() + std::ptrdiff_t(size_t(tris[i]) * S));
            }, 64);
        }
        mm.maskDirty = true;
    } else if (!mask.empty() && mm.hasMask()) {
        poolFor(tris.size(), [&](size_t b, size_t e) {
            for (size_t i = b; i < e; ++i)
                std::swap_ranges(mask.begin() + std::ptrdiff_t(i * S), mask.begin() + std::ptrdiff_t((i + 1) * S),
                                 mm.mask.begin() + std::ptrdiff_t(size_t(tris[i]) * S));
        }, 64);
    }
    if (journal) { // the stroke's change: subtract it (undo) or add it back (redo)
        std::vector<int16_t> change(tris.size() * S, 0);
        if (!microUnpackTiles(packed.data(), packed.size(), tris.size(), samples, change.data())) return;
        poolFor(tris.size(), [&](size_t b, size_t e) { // (stored as differences to the point before, see MicroUndo)
            const uint32_t n = mm.n;
            for (size_t t = b; t < e; ++t) {
                int16_t* c = change.data() + t * S;
                uint32_t p = 0;
                for (uint32_t j = 0; j <= n; ++j)
                    for (uint32_t i = 0; i + j <= n; ++i, ++p) {
                        int v;
                        if (p == 0) v = firsts[t];
                        else if (i > 0) v = int(c[p - 1]) + int(c[p]);
                        else v = int(c[MicroMesh::index(0, j - 1, n)]) + int(c[p]);
                        c[p] = int16_t(std::clamp(v, -32767, 32767));
                    }
            }
        }, 256);
        const float ratio = step / mm.step; // (the range grew since: the change in today's units)
        const int sgn = applied ? -1 : 1;
        poolFor(tris.size(), [&](size_t b, size_t e) {
            for (size_t i = b; i < e; ++i) {
                int16_t* g = mm.disp.data() + size_t(tris[i]) * S;
                int r = 0;
                for (size_t k = 0; k < S; ++k) {
                    const float c = ratio == 1.0f ? float(change[i * S + k]) : float(change[i * S + k]) * ratio;
                    const int v = std::clamp(int(g[k]) + sgn * int(std::lround(c)), -32767, 32767);
                    g[k] = int16_t(v);
                    r = std::max(r, std::abs(v));
                }
                mm.triRange[tris[i]] = uint16_t(std::max<int>(r, mm.triRange[tris[i]]));
            }
        }, 64);
        applied = !applied;
        for (uint32_t t : tris) mm.rangeMax = std::max(mm.rangeMax, mm.triRange[t]);
    }
    if (!disp.empty()) {
        poolFor(tris.size(), [&](size_t b, size_t e) {
            for (size_t i = b; i < e; ++i) {
                int16_t* g = mm.disp.data() + size_t(tris[i]) * S;
                std::swap_ranges(disp.begin() + std::ptrdiff_t(i * S), disp.begin() + std::ptrdiff_t((i + 1) * S), g);
                int r = 0;
                for (size_t k = 0; k < S; ++k) r = std::max(r, std::abs(int(g[k])));
                mm.triRange[tris[i]] = uint16_t(r);
            }
        }, 64);
        for (uint32_t t : tris) mm.rangeMax = std::max(mm.rangeMax, mm.triRange[t]);
    }
    for (uint32_t t : tris) mm.markDirty(t);
    ++mm.revision;
}

size_t MicroUndo::bytes() const {
    return tris.size() * sizeof(uint32_t) + keys.size() * sizeof(uint64_t) + packed.size() + firsts.size() * sizeof(int16_t) + disp.size() * sizeof(int16_t) +
           mask.size() + (swapWhole && whole ? whole->bytes() : 0);
}

// ---------------------------------------------------------------------------
// Bake down
// ---------------------------------------------------------------------------

bool microBakeMesh(const Mesh& m, int level, Mesh& out, std::string& error, size_t maxTriangles) {
    if (!m.micro || !microMatches(m)) {
        error = "This Part has no micro level";
        return false;
    }
    const MicroMesh& mm = *m.micro;
    if (level < 0 || level > mm.level) level = mm.level;
    if (mm.edgeNeighbor.size() != size_t(mm.baseTriangles) * 3) {
        error = "Micro level topology missing";
        return false;
    }
    if (maxTriangles == 0) maxTriangles = subdivMaxTriangles();
    const uint32_t q = 1u << level, step = mm.n / q, nt = mm.baseTriangles, nv = mm.baseVertices;
    const size_t tris = size_t(nt) * q * q;
    if (tris > maxTriangles || tris >= (size_t(1) << 32)) {
        char buf[200];
        std::snprintf(buf, sizeof buf, "Baking micro level %d makes %.0fM triangles, more than this computer allows (%.0fM): pick a lower level",
                      level, double(tris) / 1e6, double(maxTriangles) / 1e6);
        error = buf;
        return false;
    }
    // Vertex numbering: base vertices, then each base edge's q - 1 inner points (numbered by the triangle that
    // owns the edge: the lower index of the two), then each triangle's inner points.
    std::vector<uint32_t> edgeStart(size_t(nt) * 3, 0);
    uint32_t next = nv;
    for (uint32_t t = 0; t < nt; ++t)
        for (int e = 0; e < 3; ++e) {
            const uint32_t nb = mm.edgeNeighbor[size_t(t) * 3 + size_t(e)];
            if (nb == MicroMesh::kNone || (nb >> 2) > t) {
                edgeStart[size_t(t) * 3 + size_t(e)] = next;
                next += q - 1;
            }
        }
    for (uint32_t t = 0; t < nt; ++t)
        for (int e = 0; e < 3; ++e) {
            const uint32_t nb = mm.edgeNeighbor[size_t(t) * 3 + size_t(e)];
            if (nb != MicroMesh::kNone && (nb >> 2) < t) edgeStart[size_t(t) * 3 + size_t(e)] = edgeStart[size_t(nb >> 2) * 3 + (nb & 3)];
        }
    const uint32_t inner = q >= 2 ? (q - 1) * (q - 2) / 2 : 0;
    const size_t verts = size_t(next) + size_t(nt) * inner;
    if (verts >= (size_t(1) << 32)) {
        error = "Too many vertices";
        return false;
    }
    out = Mesh();
    out.positions.resize(verts);
    out.triangles.resize(tris);
    const bool uvs = m.hasUvs(), groups = m.hasFaceGroups(), hidden = m.hasHidden(), masked = mm.hasMask();
    if (uvs) out.uvs.resize(tris * 3);
    if (groups) out.facegroups.resize(tris);
    if (hidden) out.hidden.resize(tris);
    if (masked) out.mask.assign(verts, 0.0f);
    // The vertex of grid point (a, b) (bake level) of triangle t.
    auto vertexOf = [&](uint32_t t, uint32_t a, uint32_t b) -> uint32_t {
        const glm::uvec3 tr = m.triangles[t];
        if (a == 0 && b == 0) return tr.x;
        if (a == q && b == 0) return tr.y;
        if (a == 0 && b == q) return tr.z;
        int e = -1;
        uint32_t k = 0;
        if (b == 0) e = 0, k = a;
        else if (a + b == q) e = 1, k = b;
        else if (a == 0) e = 2, k = q - b;
        if (e >= 0) {
            const uint32_t nb = mm.edgeNeighbor[size_t(t) * 3 + size_t(e)];
            const bool owner = nb == MicroMesh::kNone || (nb >> 2) > t;
            if (!owner) { // numbered from the owner's start corner
                const uint32_t u = nb >> 2;
                const bool reversed = m.triangles[u][nb & 3] != tr[e];
                if (reversed) k = q - k;
            }
            return edgeStart[size_t(t) * 3 + size_t(e)] + k - 1;
        }
        // inner point: rows b = 1..q-2, a = 1..q-1-b
        const uint32_t r = b - 1;
        const uint32_t before = r * (q - 2) - r * (r - 1) / 2; // points in rows 1..b-1: sum (q - 1 - b')
        return next + t * inner + before + (a - 1);
    };
    poolFor(nt, [&](size_t b0, size_t e0) {
        MicroTriangle T;
        for (size_t tt = b0; tt < e0; ++tt) {
            const uint32_t t = uint32_t(tt);
            T.set(m, t, mm.n);
            const int16_t* g = mm.disp.data() + size_t(t) * mm.samples;
            const uint8_t* mk = masked ? mm.mask.data() + size_t(t) * mm.samples : nullptr;
            for (uint32_t b = 0; b <= q; ++b)
                for (uint32_t a = 0; a + b <= q; ++a) {
                    const uint32_t v = vertexOf(t, a, b);
                    // Each shared point is written by every triangle holding it, with the same value (stitched).
                    const uint32_t i = a * step, j = b * step, idx = MicroMesh::index(i, j, mm.n);
                    glm::vec3 s, nn;
                    T.eval(i, j, s, nn);
                    out.positions[v] = s + nn * (float(g[idx]) * mm.step);
                    if (mk) out.mask[v] = float(mk[idx]) / 255.0f;
                }
            const size_t first = size_t(t) * q * q;
            size_t o = first;
            glm::vec2 uv[3];
            if (uvs) uv[0] = m.uvs[size_t(t) * 3], uv[1] = m.uvs[size_t(t) * 3 + 1], uv[2] = m.uvs[size_t(t) * 3 + 2];
            auto uvAt = [&](uint32_t a, uint32_t b) {
                const float fa = float(a) / float(q), fb = float(b) / float(q);
                return uv[0] * (1.0f - fa - fb) + uv[1] * fa + uv[2] * fb;
            };
            for (uint32_t b = 0; b < q; ++b)
                for (uint32_t a = 0; a + b < q; ++a) {
                    out.triangles[o] = {vertexOf(t, a, b), vertexOf(t, a + 1, b), vertexOf(t, a, b + 1)};
                    if (uvs) out.uvs[o * 3] = uvAt(a, b), out.uvs[o * 3 + 1] = uvAt(a + 1, b), out.uvs[o * 3 + 2] = uvAt(a, b + 1);
                    ++o;
                    if (a + b + 1 < q) {
                        out.triangles[o] = {vertexOf(t, a + 1, b), vertexOf(t, a + 1, b + 1), vertexOf(t, a, b + 1)};
                        if (uvs)
                            out.uvs[o * 3] = uvAt(a + 1, b), out.uvs[o * 3 + 1] = uvAt(a + 1, b + 1), out.uvs[o * 3 + 2] = uvAt(a, b + 1);
                        ++o;
                    }
                }
            for (size_t k = first; k < o; ++k) {
                if (groups) out.facegroups[k] = m.facegroups[t];
                if (hidden) out.hidden[k] = m.hidden[t];
            }
        }
    }, 256);
    out.texture = m.texture;
    out.paintToTexture = m.paintToTexture;
    out.rebuildTopology();
    out.recomputeAllNormals();
    return true;
}

// ---------------------------------------------------------------------------
// Displacement maps
// ---------------------------------------------------------------------------

bool microExportDisplacement(const Mesh& m, const std::string& path, int size, bool vector, const std::string& format,
                             float& scaleOut, std::string& error) {
    if (!m.micro || !microMatches(m)) {
        error = "This Part has no micro level";
        return false;
    }
    if (!m.hasUvs()) {
        error = "Displacement maps need UVs on the base (Tool > UV Map, at the lowest level)";
        return false;
    }
    if (size < 16 || size > 16384) {
        error = "Map size must be 16 to 16384";
        return false;
    }
    if (format != "exr" && format != "png") {
        error = "Format must be exr or png";
        return false;
    }
    const MicroMesh& mm = *m.micro;
    const int W = size, H = size, C = vector ? 3 : 1;
    std::vector<float> px(size_t(W) * H * C, 0.0f);
    std::vector<uint8_t> covered(size_t(W) * H, 0);
    const uint32_t n = mm.n, S = mm.samples;
    poolFor(mm.baseTriangles, [&](size_t b0, size_t e0) {
        MicroTriangle T;
        thread_local std::vector<glm::vec3> P, F, N;
        P.resize(S), F.resize(S), N.resize(S);
        for (size_t tt = b0; tt < e0; ++tt) {
            const uint32_t t = uint32_t(tt);
            if (m.triangleHidden(t)) continue;
            const glm::vec2 a = m.uvs[size_t(t) * 3], b = m.uvs[size_t(t) * 3 + 1], c = m.uvs[size_t(t) * 3 + 2];
            // texel space, top-down
            const glm::vec2 A(a.x * W, (1.0f - a.y) * H), B(b.x * W, (1.0f - b.y) * H), Cc(c.x * W, (1.0f - c.y) * H);
            const float area = (B.x - A.x) * (Cc.y - A.y) - (Cc.x - A.x) * (B.y - A.y);
            if (std::abs(area) < 1e-12f) continue;
            T.set(m, t, n);
            const int16_t* g = mm.disp.data() + size_t(t) * S;
            uint32_t idx = 0;
            for (uint32_t j = 0; j <= n; ++j)
                for (uint32_t i = 0; i + j <= n; ++i, ++idx) {
                    glm::vec3 s, nn;
                    T.eval(i, j, s, nn);
                    P[idx] = s + nn * (float(g[idx]) * mm.step);
                    F[idx] = T.flat(i, j);
                    N[idx] = nn;
                }
            const int x0 = std::max(0, int(std::floor(std::min({A.x, B.x, Cc.x})))),
                      x1 = std::min(W - 1, int(std::ceil(std::max({A.x, B.x, Cc.x})))),
                      y0 = std::max(0, int(std::floor(std::min({A.y, B.y, Cc.y})))),
                      y1 = std::min(H - 1, int(std::ceil(std::max({A.y, B.y, Cc.y}))));
            for (int y = y0; y <= y1; ++y)
                for (int x = x0; x <= x1; ++x) {
                    const glm::vec2 p(float(x) + 0.5f, float(y) + 0.5f);
                    const float w1 = ((p.x - A.x) * (Cc.y - A.y) - (Cc.x - A.x) * (p.y - A.y)) / area;
                    const float w2 = ((B.x - A.x) * (p.y - A.y) - (p.x - A.x) * (B.y - A.y)) / area;
                    const float w0 = 1.0f - w1 - w2;
                    if (w0 < -1e-5f || w1 < -1e-5f || w2 < -1e-5f) continue;
                    const float w[3] = {w0, w1, w2};
                    const Cell cl = cellAt(w, n);
                    glm::vec3 pm(0.0f), pf(0.0f), pn(0.0f);
                    for (int k = 0; k < 3; ++k) {
                        const uint32_t id = MicroMesh::index(cl.i[k], cl.j[k], n);
                        pm += P[id] * cl.w[k], pf += F[id] * cl.w[k], pn += N[id] * cl.w[k];
                    }
                    const size_t o = size_t(y) * W + size_t(x);
                    const glm::vec3 off = pm - pf;
                    if (vector) px[o * 3] = off.x, px[o * 3 + 1] = off.y, px[o * 3 + 2] = off.z;
                    else px[o] = glm::dot(off, glm::normalize(pn));
                    covered[o] = 1;
                }
        }
    }, 256);
    // Padding: 8 rings of texels around the islands take their neighbours' values (mip and filter seams).
    for (int pass = 0; pass < 8; ++pass) {
        std::vector<uint8_t> grow = covered;
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x) {
                const size_t o = size_t(y) * W + size_t(x);
                if (covered[o]) continue;
                float sum[3] = {0, 0, 0};
                int cnt = 0;
                for (int dy = -1; dy <= 1; ++dy)
                    for (int dx = -1; dx <= 1; ++dx) {
                        const int xx = x + dx, yy = y + dy;
                        if (xx < 0 || yy < 0 || xx >= W || yy >= H) continue;
                        const size_t q = size_t(yy) * W + size_t(xx);
                        if (!covered[q]) continue;
                        for (int k = 0; k < C; ++k) sum[k] += px[q * C + size_t(k)];
                        ++cnt;
                    }
                if (!cnt) continue;
                for (int k = 0; k < C; ++k) px[o * C + size_t(k)] = sum[k] / float(cnt);
                grow[o] = 1;
            }
        covered.swap(grow);
    }
    float maxAbs = 0.0f;
    for (float v : px) maxAbs = std::max(maxAbs, std::abs(v));
    scaleOut = maxAbs;
    if (format == "exr") return saveExr(path, W, H, C, px, error);
    std::vector<uint16_t> q(px.size());
    const float s = maxAbs > 0.0f ? 32767.0f / maxAbs : 0.0f;
    for (size_t i = 0; i < px.size(); ++i) q[i] = uint16_t(std::clamp(std::lround(px[i] * s) + 32768L, 0L, 65535L));
    return savePng16(path, W, H, C, q, error);
}

// ---------------------------------------------------------------------------
// Procedural detail
// ---------------------------------------------------------------------------

namespace {

float hashFloat(int x, int y, int z, uint32_t seed) {
    uint32_t h = uint32_t(x) * 0x8da6b343u ^ uint32_t(y) * 0xd8163841u ^ uint32_t(z) * 0xcb1ab31fu ^ seed * 0x9E3779B9u;
    h ^= h >> 13;
    h *= 0x85EBCA6Bu;
    h ^= h >> 16;
    return float(h & 0xFFFFFF) / float(0xFFFFFF) * 2.0f - 1.0f;
}

float valueNoise(const glm::vec3& p, uint32_t seed) {
    const glm::vec3 f = glm::floor(p);
    const glm::vec3 t = p - f;
    const glm::vec3 u = t * t * (3.0f - 2.0f * t);
    const int x = int(f.x), y = int(f.y), z = int(f.z);
    auto h = [&](int dx, int dy, int dz) { return hashFloat(x + dx, y + dy, z + dz, seed); };
    const float a = glm::mix(glm::mix(h(0, 0, 0), h(1, 0, 0), u.x), glm::mix(h(0, 1, 0), h(1, 1, 0), u.x), u.y);
    const float b = glm::mix(glm::mix(h(0, 0, 1), h(1, 0, 1), u.x), glm::mix(h(0, 1, 1), h(1, 1, 1), u.x), u.y);
    return glm::mix(a, b, u.z);
}

} // namespace

void microNoise(Mesh& m, float amplitude, float frequency, uint32_t seed) {
    if (!m.micro) return;
    MicroMesh& mm = mutableMicro(m);
    mm.ensureTopology(m);
    mm.widen(float(mm.rangeMax) * mm.step + std::abs(amplitude) * 1.5f);
    const uint32_t n = mm.n, S = mm.samples;
    poolFor(mm.baseTriangles, [&](size_t b0, size_t e0) {
        MicroTriangle T;
        for (size_t tt = b0; tt < e0; ++tt) {
            const uint32_t t = uint32_t(tt);
            if (m.triangleHidden(t)) continue;
            T.set(m, t, n);
            int16_t* g = mm.disp.data() + size_t(t) * S;
            const uint8_t* mk = mm.hasMask() ? mm.mask.data() + size_t(t) * S : nullptr;
            uint32_t idx = 0;
            int r = mm.triRange[t];
            for (uint32_t j = 0; j <= n; ++j)
                for (uint32_t i = 0; i + j <= n; ++i, ++idx) {
                    glm::vec3 s, nn;
                    T.eval(i, j, s, nn);
                    const glm::vec3 p = s * frequency;
                    // three octaves, ridged a little so it reads as skin-like pores and wrinkles
                    float v = valueNoise(p, seed) * 0.6f + valueNoise(p * 2.03f, seed + 1) * 0.28f +
                              valueNoise(p * 4.11f, seed + 2) * 0.12f;
                    v = 1.0f - std::abs(v) * 2.0f;
                    float d = float(g[idx]) * mm.step + amplitude * v * (mk ? 1.0f - float(mk[idx]) / 255.0f : 1.0f);
                    const long q = std::clamp(std::lround(d / mm.step), -32767L, 32767L);
                    g[idx] = int16_t(q);
                    r = std::max(r, int(std::abs(q)));
                }
            mm.triRange[t] = uint16_t(r);
        }
    }, 256);
    for (uint16_t r : mm.triRange) mm.rangeMax = std::max(mm.rangeMax, r);
    mm.markAllDirty();
    ++mm.revision;
}

} // namespace sf
