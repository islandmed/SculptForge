// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Brushes on micro-displacement levels (core/Micro.h). A dab:
//  1. finds the base triangles it can reach: the BVH over the base, with the radius grown by how far the micro
//     surface can leave the base (the Phong bulge plus the largest displacement), then each triangle's own box;
//  2. saves the grids of triangles the stroke touches for the first time (the undo step: whole tiles);
//  3. finds the brush plane (area normal and centre) from every second grid point under the brush;
//  4. runs the kernel over the triangles in parallel: each grid point inside the brush gets a new scalar;
//  5. stitches: copies of a point on a shared edge or corner are averaged (they differ only where a brush read
//     neighbours, like Smooth), so the surface stays closed.
#include "core/Micro.h"
#include "core/FocusDetail.h"

#include "core/BVH.h"
#include "core/BrushAlpha.h"
#include "core/Sculpt.h"
#include "core/ThreadPool.h"
#include "core/TierConfig.h"
#include "core/Wavelet.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>

namespace sf {

int microBrushSupport(BrushType type) {
    switch (type) {
    case BrushType::Standard:
    case BrushType::Clay:
    case BrushType::ClayBuildup:
    case BrushType::Smooth:
    case BrushType::Inflate:
    case BrushType::Flatten:
    case BrushType::Trim:
    case BrushType::Layer: return 2;
    case BrushType::Pinch:
    case BrushType::Crease:
    case BrushType::DamStandard:
    case BrushType::Move: return 1;
    default: return 0;
    }
}

namespace {

glm::vec3 projectOnPlane(const glm::vec3& v, const glm::vec3& n) { return v - n * glm::dot(v, n); }

// A base triangle's box grown by its Phong bulge and displacement range.
bool triangleNear(const Mesh& m, const MicroMesh& mm, uint32_t t, const glm::vec3& c, float r) {
    const glm::uvec3 tr = m.triangles[t];
    const glm::vec3 a = m.positions[tr.x], b = m.positions[tr.y], d = m.positions[tr.z];
    const glm::vec3 na = m.normals[tr.x], nb = m.normals[tr.y], nd = m.normals[tr.z];
    float bulge = 0.0f;
    bulge = std::max({bulge, std::abs(glm::dot(b - a, na)), std::abs(glm::dot(d - a, na)), std::abs(glm::dot(a - b, nb)),
                      std::abs(glm::dot(d - b, nb)), std::abs(glm::dot(a - d, nd)), std::abs(glm::dot(b - d, nd))});
    const float grow = bulge * MicroMesh::kPhong + float(mm.triRange[t]) * mm.step;
    const glm::vec3 lo = glm::min(a, glm::min(b, d)) - grow, hi = glm::max(a, glm::max(b, d)) + grow;
    const glm::vec3 q = glm::clamp(c, lo, hi);
    return glm::dot(q - c, q - c) <= r * r;
}

// Writes the change at every grid point of an n-grid from `coarse` changes on the points `stride` apart (a grid of
// n / stride): each point gets the coarse micro triangle's values interpolated (exactly the coarse value on coarse
// points). apply(index, change) is called for the points whose change isn't 0.
template <class F>
void interpolateCoarse(const float* coarse, uint32_t n, uint32_t stride, F&& apply) {
    const uint32_t cm = n / stride;
    const float inv = 1.0f / float(stride);
    uint32_t idx = 0;
    for (uint32_t j = 0; j <= n; ++j) {
        const uint32_t cj = j / stride, rj = j % stride;
        const float fj = float(rj) * inv;
        for (uint32_t i = 0; i + j <= n; ++i, ++idx) {
            const uint32_t ci = i / stride, ri = i % stride;
            const float fi = float(ri) * inv;
            float d;
            if (ri == 0 && rj == 0) d = coarse[MicroMesh::index(ci, cj, cm)];
            else if (fi + fj <= 1.0f)
                d = coarse[MicroMesh::index(ci, cj, cm)] * (1.0f - fi - fj) + coarse[MicroMesh::index(ci + 1, cj, cm)] * fi +
                    coarse[MicroMesh::index(ci, cj + 1, cm)] * fj;
            else
                d = coarse[MicroMesh::index(ci + 1, cj, cm)] * (1.0f - fj) + coarse[MicroMesh::index(ci + 1, cj + 1, cm)] * (fi + fj - 1.0f) +
                    coarse[MicroMesh::index(ci, cj + 1, cm)] * (1.0f - fi);
            if (d != 0.0f) apply(idx, d);
        }
    }
}

// Rounds `units` (a displacement over its step) to the nearest storable value (no libm call: brushes write
// millions of these).
inline int16_t quantizeUnits(float units) {
    units = std::clamp(units, -32767.0f, 32767.0f);
    return int16_t(units >= 0.0f ? int(units + 0.5f) : -int(0.5f - units));
}

// The six lattice neighbours of (i, j) inside the triangle.
template <class F>
void forNeighbors(uint32_t i, uint32_t j, uint32_t n, F&& f) {
    if (i > 0) f(i - 1, j);
    if (i + j < n) f(i + 1, j);
    if (j > 0) f(i, j - 1);
    if (i + j < n) f(i, j + 1);
    if (j > 0) f(i + 1, j - 1);
    if (i > 0) f(i - 1, j + 1);
}

} // namespace

void MicroStroke::begin(Mesh& m) {
    MicroMesh& mm = mutableMicro(m);
    mm.ensureTopology(m);
    if (mm.dirtyBits.size() != (size_t(mm.baseTriangles) + 63) / 64) mm.dirtyBits.assign((size_t(mm.baseTriangles) + 63) / 64, 0);
    tileOf_.assign(mm.baseTriangles, 0);
    tiles_.clear();
    oldDisp_.clear();
    oldMask_.clear();
    maskAdded_ = false;
    if (dabStamp_.size() != mm.baseTriangles) {
        dabStamp_.assign(mm.baseTriangles, 0);
        dabEpoch_ = 0;
    }
    if (vertStamp_.size() != m.vertexCount()) vertStamp_.assign(m.vertexCount(), 0);
    strokeStep_ = mm.step;
    grabMirrors_.clear();
    grabTris_.clear();
    grabWeight_.clear();
    grabNormal_.clear();
    baseEdge_ = mm.baseEdge;
    mm_ = &mm;
    coefOf_.assign(mm.baseTriangles, 0);
    stale_.assign(mm.baseTriangles, 0);
    coef_.clear();
    freeSlots_.clear();
    staleList_.clear();
    maskedTri_.clear();
    mm.lazy = nullptr;
    if (wavelet)
        mm.lazy = [alive = std::weak_ptr<char>(alive_), this](MicroMesh& level, std::span<const uint32_t> tris) {
            if (!alive.expired()) flush(level, tris);
        };
    mm.stroking = true;
    active_ = true;
}

bool MicroStroke::waveletDefault() {
    static const bool on = [] {
        const char* e = std::getenv("SF_MICRO_WAVELET");
        return e && e[0] == '1';
    }();
    return on;
}

void MicroStroke::flush(Mesh& m) {
    if (m.micro && m.micro.get() == mm_) flushAll(*mm_);
}

void MicroStroke::flushAll(MicroMesh& mm) {
    if (staleList_.empty()) return;
    for (uint32_t t : staleList_) stale_[t] = 0;
    work_.swap(staleList_);
    staleList_.clear();
    rebuild(mm, work_);
}

void MicroStroke::flush(MicroMesh& mm, std::span<const uint32_t> tris) {
    if (&mm != mm_ || staleList_.empty()) return;
    work_.clear();
    for (uint32_t t : tris)
        if (t < stale_.size() && stale_[t]) {
            stale_[t] = 0;
            work_.push_back(t);
        }
    if (work_.empty()) return;
    std::erase_if(staleList_, [&](uint32_t t) { return !stale_[t]; });
    rebuild(mm, work_);
}

// The heights of `tris` from their coefficients (the inverse transform), each patch's range exact again.
void MicroStroke::rebuild(MicroMesh& mm, std::span<const uint32_t> tris) {
    auto clock = std::chrono::steady_clock::now();
    const uint32_t n = mm.n, S = mm.samples;
    poolFor(tris.size(), [&](size_t b, size_t e) {
        for (size_t k = b; k < e; ++k) {
            const uint32_t t = tris[k];
            int16_t* g = mm.disp.data() + size_t(t) * S;
            std::copy_n(coef_.data() + size_t(coefOf_[t] - 1) * S, S, g);
            waveletInverse(g, n);
            int r = 0;
            for (uint32_t i = 0; i < S; ++i) r = std::max(r, std::abs(int(g[i])));
            mm.triRange[t] = uint16_t(std::min(r, 32767));
        }
    }, 8);
    for (uint32_t t : tris) {
        mm.rangeMax = std::max(mm.rangeMax, mm.triRange[t]);
        mm.markDirty(t);
    }
    ++mm.revision;
    rebuilt += tris.size();
    flushMs += lapMs(clock);
}

void MicroStroke::dropCoefficients(std::span<const uint32_t> tris) {
    for (uint32_t t : tris)
        if (coefOf_[t]) {
            freeSlots_.push_back(coefOf_[t] - 1);
            coefOf_[t] = 0;
        }
}

void MicroStroke::dropAllCoefficients(MicroMesh& mm) {
    if (coef_.empty()) return;
    flushAll(mm);
    std::fill(coefOf_.begin(), coefOf_.end(), 0);
    coef_.clear();
    freeSlots_.clear();
}

bool MicroStroke::waveletDab(const Mesh& m, MicroMesh& mm, BrushType type, const glm::vec3& center, const glm::vec3& viewDir,
                             float r, float s, float sign, float focal) {
    const uint32_t n = mm.n, S = mm.samples;
    // The coarsest lattice the radius still spans kWaveletSamples points of.
    const float microEdge = baseEdge_ / float(n);
    uint32_t sp = 1;
    if (microEdge > 0.0f)
        while (sp < n / 2 && r >= kWaveletSamples * microEdge * float(2 * sp)) sp <<= 1;
    if (sp < kWaveletMinSpacing) return false;
    // A mask (it scales each fine point's change) or a hidden neighbour (whose shared points stay) under the brush:
    // today's path.
    if (maskedTri_.size() != mm.baseTriangles) maskedTri_.assign(mm.baseTriangles, 0);
    for (uint32_t t : cand_) {
        if (m.hasHidden()) {
            const glm::uvec3 tr = m.triangles[t];
            for (int k = 0; k < 3; ++k)
                for (uint32_t u : m.vertexTriangles(tr[k]))
                    if (m.triangleHidden(u)) return false;
        }
        if (!mm.hasMask()) continue;
        if (!maskedTri_[t]) {
            const uint8_t* mk = mm.mask.data() + size_t(t) * S;
            maskedTri_[t] = std::any_of(mk, mk + S, [](uint8_t v) { return v != 0; }) ? 2 : 1;
        }
        if (maskedTri_[t] == 2) return false;
    }
    auto clock = std::chrono::steady_clock::now();
    const uint32_t cm = n / sp, L = (cm + 1) * (cm + 2) / 2;
    const size_t C = cand_.size();
    const float r2 = r * r, invStep = 1.0f / mm.step;
    lattice_.resize(C * L);
    latticeState_.assign(C, 0);
    latticeGrow_.assign(C, 0);
    auto slot = [&](uint32_t t) { return coef_.data() + size_t(coefOf_[t] - 1) * S; };
    // 1. The lattice's heights: a patch with coefficients turned to lattice form in place (until step 4), one
    // without read from its heights (they're current).
    poolFor(C, [&](size_t b, size_t e) {
        for (size_t ci = b; ci < e; ++ci) {
            const uint32_t t = cand_[ci];
            const int16_t* src = mm.disp.data() + size_t(t) * S;
            if (coefOf_[t]) {
                int16_t* c = slot(t);
                waveletToLattice(c, n, sp);
                src = c;
                latticeState_[ci] = 1;
            }
            int16_t* out = lattice_.data() + ci * L;
            for (uint32_t cj = 0, k = 0; cj <= cm; ++cj)
                for (uint32_t cii = 0; cii + cj <= cm; ++cii, ++k) out[k] = src[MicroMesh::index(cii * sp, cj * sp, n)];
        }
    }, 16);
    // 2. The brush plane (Standard, Clay) as dab() finds it, on the lattice.
    glm::vec3 areaNormal = -viewDir, areaCenter = center;
    if (type != BrushType::Inflate) {
        struct Sum {
            glm::dvec3 nrm{0.0}, ctr{0.0};
            double w = 0.0;
        };
        const size_t block = 8;
        std::vector<Sum> sums((C + block - 1) / block);
        poolFor(C, [&](size_t b, size_t e) {
            Sum acc;
            MicroTriangle T;
            for (size_t ci = b; ci < e; ++ci) {
                T.set(m, cand_[ci], n);
                const int16_t* lat = lattice_.data() + ci * L;
                for (uint32_t cj = 0, k = 0; cj <= cm; ++cj)
                    for (uint32_t cii = 0; cii + cj <= cm; ++cii, ++k) {
                        glm::vec3 p, nn;
                        T.eval(cii * sp, cj * sp, p, nn);
                        p += nn * (float(lat[k]) * mm.step);
                        const float d2 = glm::dot(p - center, p - center);
                        if (d2 >= r2) continue;
                        const float w = brushFalloff(std::sqrt(d2), r, focal);
                        acc.nrm += glm::dvec3(nn * w);
                        acc.ctr += glm::dvec3(p * w);
                        acc.w += w;
                    }
            }
            sums[b / block] = acc;
        }, block, 2);
        Sum total;
        for (const Sum& x : sums) total.nrm += x.nrm, total.ctr += x.ctr, total.w += x.w;
        if (total.w > 0.0) {
            areaCenter = glm::vec3(total.ctr / total.w);
            const double len = glm::length(total.nrm);
            if (len > 1e-12) areaNormal = glm::vec3(total.nrm / len);
        }
    }
    // 3. The kernel on the lattice (dab()'s formulas), each new lattice height rounded as dab() rounds.
    const size_t block = 8;
    std::vector<uint64_t> visited((C + block - 1) / block, 0);
    std::vector<float> clipped((C + block - 1) / block, 0.0f);
    poolFor(C, [&](size_t b, size_t e) {
        MicroTriangle T;
        uint64_t count = 0;
        float over = 0.0f;
        for (size_t ci = b; ci < e; ++ci) {
            T.set(m, cand_[ci], n);
            int16_t* lat = lattice_.data() + ci * L;
            int grow = 0;
            for (uint32_t cj = 0, k = 0; cj <= cm; ++cj)
                for (uint32_t cii = 0; cii + cj <= cm; ++cii, ++k) {
                    glm::vec3 p, nn;
                    T.eval(cii * sp, cj * sp, p, nn);
                    p += nn * (float(lat[k]) * mm.step);
                    const float d2 = glm::dot(p - center, p - center);
                    if (d2 >= r2) continue;
                    ++count;
                    const float f = brushFalloff(std::sqrt(d2), r, focal);
                    if (f <= 0.0f) continue;
                    float delta = 0.0f;
                    if (type == BrushType::Standard) delta = sign * s * r * 0.06f * f * glm::dot(areaNormal, nn);
                    else if (type == BrushType::Inflate) delta = sign * s * r * 0.05f * f;
                    else { // Clay
                        const float facing = std::max(glm::dot(nn, areaNormal), 0.3f);
                        const glm::vec3 pp = areaCenter + areaNormal * (sign * r * 0.05f * s);
                        const float h = glm::dot(p - pp, areaNormal);
                        if (sign * h < 0.0f) delta = -h * f * clayPull(s) / facing;
                    }
                    if (delta == 0.0f) continue;
                    const float nd = float(lat[k]) + delta * invStep;
                    if (std::abs(nd) > 32767.0f) over = std::max(over, std::abs(nd) * mm.step);
                    const int16_t q = quantizeUnits(nd);
                    if (q == lat[k]) continue;
                    grow = std::max(grow, std::abs(int(q) - int(lat[k])));
                    lat[k] = q;
                }
            if (grow) {
                latticeState_[ci] |= 2;
                latticeGrow_[ci] = uint16_t(std::min(grow, 65535));
            }
        }
        visited[b / block] = count;
        clipped[b / block] = over;
    }, block, 2);
    // 4. Changed patches without coefficients get a slot; each patch goes back to coefficients with its new lattice.
    for (size_t ci = 0; ci < C; ++ci) {
        if (!(latticeState_[ci] & 2)) continue;
        const uint32_t t = cand_[ci];
        if (!coefOf_[t]) {
            uint32_t sl;
            if (!freeSlots_.empty()) sl = freeSlots_.back(), freeSlots_.pop_back();
            else sl = uint32_t(coef_.size() / S), coef_.resize(coef_.size() + S);
            coefOf_[t] = sl + 1;
        }
        if (!stale_[t]) stale_[t] = 1, staleList_.push_back(t);
    }
    poolFor(C, [&](size_t b, size_t e) {
        for (size_t ci = b; ci < e; ++ci) {
            const uint32_t t = cand_[ci];
            const uint8_t st = latticeState_[ci];
            if (!st) continue; // (no coefficients, nothing changed)
            int16_t* c = slot(t);
            if (!(st & 1)) {
                std::copy_n(mm.disp.data() + size_t(t) * S, S, c);
                waveletHeightsToLattice(c, n, sp);
            }
            if (st & 2) {
                const int16_t* lat = lattice_.data() + ci * L;
                for (uint32_t cj = 0, k = 0; cj <= cm; ++cj)
                    for (uint32_t cii = 0; cii + cj <= cm; ++cii, ++k) c[MicroMesh::index(cii * sp, cj * sp, n)] = lat[k];
            }
            waveletFromLattice(c, n, sp);
        }
    }, 16);
    // The ranges stay upper bounds until the rebuild makes them exact: the fine points move by at most the lattice's
    // largest change, plus a unit per finer level for the floor averages.
    int levels = 0;
    for (uint32_t x = sp; x > 1; x >>= 1) ++levels;
    for (size_t ci = 0; ci < C; ++ci)
        if (latticeState_[ci] & 2) {
            const uint32_t t = cand_[ci];
            mm.triRange[t] = uint16_t(std::min(int(mm.triRange[t]) + int(latticeGrow_[ci]) + levels + 1, 32767));
            mm.rangeMax = std::max(mm.rangeMax, mm.triRange[t]);
        }
    for (uint64_t c : visited) points += c;
    lastStride = sp;
    ++waveletDabs;
    kernelMs += lapMs(clock);
    float over = 0.0f;
    for (float c : clipped) over = std::max(over, c);
    if (over > 0.0f) widen(mm, over * 1.5f);
    return true;
}

// Saves the grids of the triangles in `tris` that this stroke hasn't touched yet.
void MicroStroke::touch(MicroMesh& mm, std::span<const uint32_t> tris) {
    if (mm.focus) mm.focus->ensureFull(mm, tris, true); // (edits write full tiles only)
    const size_t S = mm.samples, before = tiles_.size();
    for (uint32_t t : tris)
        if (!tileOf_[t]) {
            tiles_.push_back(t);
            tileOf_[t] = uint32_t(tiles_.size());
        }
    if (tiles_.size() == before) return;
    oldDisp_.resize(tiles_.size() * S);
    if (mm.hasMask()) oldMask_.resize(tiles_.size() * S);
    poolFor(tiles_.size() - before, [&](size_t b, size_t e) {
        for (size_t i = before + b; i < before + e; ++i) {
            const size_t t = tiles_[i];
            std::copy_n(mm.disp.data() + t * S, S, oldDisp_.data() + i * S);
            if (mm.hasMask()) std::copy_n(mm.mask.data() + t * S, S, oldMask_.data() + i * S);
        }
    }, 64);
}

// Makes the copies of every shared point of triangles `tris` equal again (see the file comment).
void MicroStroke::stitch(const Mesh& m, MicroMesh& mm, std::span<const uint32_t> tris) {
    const uint32_t n = mm.n, S = mm.samples;
    const uint32_t epoch = dabEpoch_;
    auto inDab = [&](uint32_t t) { return dabStamp_[t] == epoch; };
    // Edges: each shared edge once, by the lower triangle of the two (both in this dab), or copied from a hidden
    // neighbour (which brushes leave alone).
    poolFor(tris.size(), [&](size_t b, size_t e) {
        for (size_t s = b; s < e; ++s) {
            const uint32_t t = tris[s];
            const glm::uvec3 tt = m.triangles[t];
            int16_t* g = mm.disp.data() + size_t(t) * S;
            uint8_t* mk = mm.hasMask() ? mm.mask.data() + size_t(t) * S : nullptr;
            for (int ed = 0; ed < 3; ++ed) {
                const uint32_t nb = mm.edgeNeighbor[size_t(t) * 3 + size_t(ed)];
                if (nb == MicroMesh::kNone) continue;
                const uint32_t u = nb >> 2;
                const int f = int(nb & 3);
                const bool hiddenU = m.triangleHidden(u);
                if (!(inDab(u) && t < u) && !(hiddenU && !inDab(u))) continue;
                int16_t* h = mm.disp.data() + size_t(u) * S;
                uint8_t* hk = mk ? mm.mask.data() + size_t(u) * S : nullptr;
                const bool reversed = m.triangles[u][f] != tt[ed];
                for (uint32_t k = 1; k < n; ++k) {
                    uint32_t i, j, i2, j2;
                    MicroMesh::edgePoint(ed, k, n, i, j);
                    MicroMesh::edgePoint(f, reversed ? n - k : k, n, i2, j2);
                    const uint32_t a = MicroMesh::index(i, j, n), c = MicroMesh::index(i2, j2, n);
                    if (hiddenU) {
                        g[a] = h[c];
                        if (mk) mk[a] = hk[c];
                    } else {
                        const int16_t v = int16_t((int(g[a]) + int(h[c]) + (int(g[a]) + int(h[c]) >= 0 ? 1 : 0)) / 2);
                        g[a] = h[c] = v;
                        if (mk) mk[a] = hk[c] = uint8_t((int(mk[a]) + int(hk[c]) + 1) / 2);
                    }
                }
            }
        }
    }, 64);
    // Corners: every base vertex of these triangles once, averaged over the triangles around it in this dab (or
    // taken from a hidden one).
    corners_.clear();
    const uint32_t vmark = epoch;
    for (uint32_t t : tris) {
        const glm::uvec3 tt = m.triangles[t];
        for (int k = 0; k < 3; ++k)
            if (vertStamp_[tt[k]] != vmark) {
                vertStamp_[tt[k]] = vmark;
                corners_.push_back(tt[k]);
            }
    }
    poolFor(corners_.size(), [&](size_t b, size_t e) {
        for (size_t s = b; s < e; ++s) {
            const uint32_t v = corners_[s];
            const IndexRing ring = m.vertexTriangles(v);
            auto cornerIndex = [&](uint32_t u) {
                const glm::uvec3 ut = m.triangles[u];
                const int c = ut.x == v ? 0 : ut.y == v ? 1 : 2;
                return size_t(u) * S + (c == 0 ? 0 : c == 1 ? MicroMesh::index(n, 0, n) : MicroMesh::index(0, n, n));
            };
            long sum = 0, msum = 0;
            int count = 0;
            int hiddenValue = 0, hiddenMask = 0;
            bool fromHidden = false;
            for (uint32_t u : ring) {
                const size_t ci = cornerIndex(u);
                if (!inDab(u)) {
                    if (m.triangleHidden(u) && !fromHidden) {
                        fromHidden = true;
                        hiddenValue = mm.disp[ci];
                        hiddenMask = mm.hasMask() ? mm.mask[ci] : 0;
                    }
                    continue;
                }
                sum += mm.disp[ci];
                if (mm.hasMask()) msum += mm.mask[ci];
                ++count;
            }
            if (!count) continue;
            const int16_t value = fromHidden ? int16_t(hiddenValue)
                                             : int16_t((sum + (sum >= 0 ? count / 2 : -(count / 2))) / count);
            const uint8_t mval = fromHidden ? uint8_t(hiddenMask) : uint8_t((msum + count / 2) / count);
            for (uint32_t u : ring) {
                if (!inDab(u)) continue;
                const size_t ci = cornerIndex(u);
                mm.disp[ci] = value;
                if (mm.hasMask()) mm.mask[ci] = mval;
            }
        }
    }, 256);
    // Ranges (the kernel grew them for what it wrote): stitching averaged shared points with the neighbours, so
    // a triangle's edges may now hold its neighbour's larger value.
    poolFor(tris.size(), [&](size_t b, size_t e) {
        for (size_t s = b; s < e; ++s) {
            const uint32_t t = tris[s];
            const int16_t* g = mm.disp.data() + size_t(t) * S;
            int r = mm.triRange[t];
            for (int ed = 0; ed < 3; ++ed)
                for (uint32_t k = 0; k < n; ++k) {
                    uint32_t i, j;
                    MicroMesh::edgePoint(ed, k, n, i, j);
                    r = std::max(r, std::abs(int(g[MicroMesh::index(i, j, n)])));
                }
            mm.triRange[t] = uint16_t(std::min(r, 32767));
        }
    }, 64);
    for (uint32_t t : tris) {
        mm.rangeMax = std::max(mm.rangeMax, mm.triRange[t]);
        mm.markDirty(t);
    }
    ++mm.revision;
}

void MicroStroke::dab(Mesh& m, const BVH& bvh, BrushType type, const glm::vec3& center, const glm::vec3& viewDir, float radius,
                      float strength, bool invert, float focal, const glm::vec3& alphaUp, bool maskBrush, float maskStrength,
                      const BrushAlpha* alpha) {
    if (!active_ || !m.micro || radius <= 0.0f) return;
    if (!maskBrush && microBrushSupport(type) == 0) return;
    auto clock = std::chrono::steady_clock::now();
    MicroMesh& mm = *m.micro; // (made unique by begin)
    const uint32_t n = mm.n, S = mm.samples;
    const float r = radius, r2 = r * r;
    const float s = std::clamp(strength, 0.0f, 1.0f);
    const float sign = invert ? -1.0f : 1.0f;
    // An alpha reaches its square's corners, the square ClayBuildup tip a little past the circle (as Sculpt.cpp).
    const bool useAlpha = alpha && !alpha->empty() && !maskBrush;
    const float reach = useAlpha ? kAlphaReach : type == BrushType::ClayBuildup && !maskBrush ? 1.3f : 1.0f;

    // The displacement range this dab may need: widened first (it costs precision) when what a dab adds could
    // pass it; a dab that still clips (planes far off) widens it for the next one.
    if (!maskBrush) widen(mm, float(mm.rangeMax) * mm.step + 0.12f * r * s);

    // 1. The base triangles within reach.
    if (++dabEpoch_ == 0) {
        std::fill(dabStamp_.begin(), dabStamp_.end(), 0);
        std::fill(vertStamp_.begin(), vertStamp_.end(), 0);
        dabEpoch_ = 1;
    }
    bvh.queryTriangles(m, center, r * reach + mm.bulgeMax + float(mm.rangeMax) * mm.step, cand_);
    std::erase_if(cand_, [&](uint32_t t) { return m.triangleHidden(t) || !triangleNear(m, mm, t, center, r * reach); });
    if (cand_.empty()) return;
    for (uint32_t t : cand_) dabStamp_[t] = dabEpoch_;
    if (maskBrush && !mm.hasMask()) {
        mm.mask.assign(mm.disp.size(), 0);
        oldMask_.assign(tiles_.size() * S, 0); // (the tiles saved so far had no mask: zeros)
        maskAdded_ = true;
        mm.maskDirty = true;
    }
    touch(mm, cand_);
    findMs += lapMs(clock);
    if (wavelet && !maskBrush && !useAlpha &&
        (type == BrushType::Standard || type == BrushType::Inflate || type == BrushType::Clay) &&
        waveletDab(m, mm, type, center, viewDir, r, s, sign, focal))
        return;
    // Any other dab works on heights: those of its triangles rebuilt first, their coefficients then out of date.
    if (!coef_.empty()) {
        flush(mm, cand_);
        dropCoefficients(cand_);
    }
    if (maskBrush) maskedTri_.clear();

    // 2. The brush plane: weighted area normal and centre over every second grid point under the brush.
    glm::vec3 areaNormal(0.0f), areaCenter(0.0f);
    const bool needPlane = !maskBrush && type != BrushType::Smooth && type != BrushType::Inflate && type != BrushType::Pinch;
    if (needPlane) {
        const uint32_t stride = n >= 4 ? 2 : 1;
        struct Sum {
            glm::dvec3 nrm{0.0}, ctr{0.0};
            double w = 0.0;
        };
        const size_t block = 8;
        std::vector<Sum> sums((cand_.size() + block - 1) / block);
        poolFor(cand_.size(), [&](size_t b, size_t e) {
            Sum acc;
            MicroTriangle T;
            for (size_t ci = b; ci < e; ++ci) {
                const uint32_t t = cand_[ci];
                T.set(m, t, n);
                const int16_t* g = mm.disp.data() + size_t(t) * S;
                for (uint32_t j = 0; j <= n; j += stride)
                    for (uint32_t i = 0; i + j <= n; i += stride) {
                        glm::vec3 sp, nn;
                        T.eval(i, j, sp, nn);
                        const glm::vec3 p = sp + nn * (float(g[MicroMesh::index(i, j, n)]) * mm.step);
                        const float d2 = glm::dot(p - center, p - center);
                        if (d2 >= r2) continue;
                        const float w = brushFalloff(std::sqrt(d2), r, focal);
                        acc.nrm += glm::dvec3(nn * w);
                        acc.ctr += glm::dvec3(p * w);
                        acc.w += w;
                    }
            }
            sums[b / block] = acc;
        }, block, 2);
        Sum total;
        for (const Sum& x : sums) total.nrm += x.nrm, total.ctr += x.ctr, total.w += x.w;
        if (total.w <= 0.0) {
            // nothing under the brush (it only grazed the boxes): the stitch below still runs for the saved tiles
            areaNormal = -viewDir;
            areaCenter = center;
        } else {
            areaCenter = glm::vec3(total.ctr / total.w);
            const double len = glm::length(total.nrm);
            areaNormal = len > 1e-12 ? glm::vec3(total.nrm / len) : -viewDir;
        }
    }
    glm::vec3 up = projectOnPlane(alphaUp, areaNormal);
    if (glm::length(up) < 1e-6f)
        up = projectOnPlane(std::abs(areaNormal.x) < 0.9f ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0), areaNormal);
    up = glm::normalize(up);
    const glm::vec3 right = glm::cross(up, areaNormal);
    const float layerHeight = r * 0.08f * s;
    const bool readsNeighbours = type == BrushType::Smooth || type == BrushType::Pinch || type == BrushType::Crease ||
                                 type == BrushType::DamStandard;

    // 3. The kernel, in parallel over the triangles (each writes only its own grid). Scale-adaptive: the brush is
    // evaluated on the grid points `stride` apart, about kBrushSamples across its radius, and the points in between
    // get the change interpolated over the coarse micro triangle holding them (a big brush moves big forms; the
    // finer displacement rides on top unchanged). Brush evaluation then costs the same at any micro level; what
    // grows with the level is only writing the interpolated values.
    const float microEdge = baseEdge_ / float(n);
    uint32_t stride = 1;
    if (adaptive && microEdge > 0.0f)
        while (stride < n && microEdge * float(stride * 2) <= r / float(kBrushSamples)) stride *= 2;
    lastStride = stride;
    const uint32_t cm = n / stride;
    std::vector<uint64_t> visited((cand_.size() + 7) / 8, 0);
    std::vector<float> clipped((cand_.size() + 7) / 8, 0.0f); // per block: the largest |d| that didn't fit
    const float invStep = 1.0f / mm.step;
    poolFor(cand_.size(), [&](size_t b, size_t e) {
        thread_local std::vector<int16_t> before;
        thread_local std::vector<float> coarse;
        MicroTriangle T;
        uint64_t count = 0;
        float over = 0.0f;
        for (size_t ci = b; ci < e; ++ci) {
            const uint32_t t = cand_[ci];
            T.set(m, t, n);
            int16_t* g = mm.disp.data() + size_t(t) * S;
            uint8_t* mk = mm.hasMask() ? mm.mask.data() + size_t(t) * S : nullptr;
            if (readsNeighbours && !maskBrush) before.assign(g, g + S); // Jacobi: neighbours as they were
            const int16_t* old = !maskBrush && type == BrushType::Layer ? oldDisp_.data() + size_t(tileOf_[t] - 1) * S : nullptr;
            coarse.assign(size_t(cm + 1) * (cm + 2) / 2, 0.0f);
            bool any = false;
            uint32_t cidx = 0;
            for (uint32_t cj = 0; cj <= cm; ++cj)
                for (uint32_t cii = 0; cii + cj <= cm; ++cii, ++cidx) {
                    const uint32_t i = cii * stride, j = cj * stride, idx = MicroMesh::index(i, j, n);
                    glm::vec3 sp, nn;
                    T.eval(i, j, sp, nn);
                    const float d = float(g[idx]) * mm.step;
                    const glm::vec3 p = sp + nn * d;
                    const glm::vec3 o = p - center;
                    const float dist2 = glm::dot(o, o);
                    if (dist2 >= r2 * reach * reach) continue;
                    ++count;
                    if (maskBrush) {
                        if (dist2 >= r2) continue;
                        coarse[cidx] = std::clamp(maskStrength, 0.0f, 1.0f) * brushFalloff(std::sqrt(dist2), r, focal);
                        any = true;
                        continue;
                    }
                    float f = useAlpha ? alpha->stamp(glm::dot(o, right) / r, glm::dot(o, up) / r, focal)
                                       : brushFalloff(std::sqrt(dist2), r, focal);
                    if (type == BrushType::ClayBuildup && !useAlpha) { // square tip, rounded corners (as Sculpt.cpp)
                        const float cheb = std::max(std::abs(glm::dot(o, right)), std::abs(glm::dot(o, up))) / r;
                        const float corner = std::clamp((1.3f - std::sqrt(dist2) / r) / 0.3f, 0.0f, 1.0f);
                        const float edge = std::clamp((1.0f - cheb) / 0.35f, 0.0f, 1.0f);
                        f = edge * edge * (3.0f - 2.0f * edge) * corner;
                    }
                    if (mk && stride == 1) f *= 1.0f - float(mk[idx]) / 255.0f; // (coarse: the fine points' mask, below)
                    if (f <= 0.0f) continue;
                    const float facing = std::max(glm::dot(nn, areaNormal), 0.3f); // moving along nn reaches a plane this much slower
                    float delta = 0.0f;
                    auto neighbourAverage = [&](float& lo, float& hi) {
                        float sum = 0.0f;
                        int c = 0;
                        lo = 1e30f, hi = -1e30f;
                        forNeighbors(cii, cj, cm, [&](uint32_t a2, uint32_t b2) {
                            const float v = float(before[MicroMesh::index(a2 * stride, b2 * stride, n)]);
                            sum += v, ++c;
                            lo = std::min(lo, v), hi = std::max(hi, v);
                        });
                        return c ? sum / float(c) : float(before[idx]);
                    };
                    auto sharpen = [&](float k) { // pinch, on a height field: away from the neighbour average, kept within them
                        float lo, hi;
                        const float avg = neighbourAverage(lo, hi);
                        const float cur = float(before[idx]);
                        const float target = std::clamp(cur + (cur - avg) * k, lo, hi);
                        return (target - cur) * mm.step;
                    };
                    switch (type) {
                    case BrushType::Standard: delta = sign * s * r * 0.06f * f * glm::dot(areaNormal, nn); break;
                    case BrushType::Inflate: delta = sign * s * r * 0.05f * f; break;
                    case BrushType::Smooth: {
                        float lo, hi;
                        const float avg = neighbourAverage(lo, hi);
                        delta = (avg - float(before[idx])) * mm.step * std::min(1.0f, s * f);
                        break;
                    }
                    case BrushType::Flatten: {
                        const float h = glm::dot(p - areaCenter, areaNormal);
                        delta = -h * s * f * (invert ? -0.5f : 1.0f) / facing;
                        break;
                    }
                    case BrushType::Clay: {
                        const glm::vec3 pp = areaCenter + areaNormal * (sign * r * 0.05f * s);
                        const float h = glm::dot(p - pp, areaNormal);
                        if (sign * h < 0.0f) delta = -h * f * clayPull(s) / facing;
                        break;
                    }
                    case BrushType::ClayBuildup: {
                        const glm::vec3 pp = areaCenter + areaNormal * (sign * r * 0.08f * s);
                        const float h = glm::dot(p - pp, areaNormal);
                        if (sign * h < 0.0f) delta = -h * f * clayPull(s) / facing;
                        break;
                    }
                    case BrushType::Trim: {
                        const glm::vec3 pp = areaCenter - areaNormal * (sign * r * 0.02f * s);
                        const float h = glm::dot(p - pp, areaNormal);
                        if (sign * h > 0.0f) delta = -h * std::min(1.0f, std::sqrt(f) * (0.5f + s)) / facing;
                        break;
                    }
                    case BrushType::Layer: {
                        // Up to one height per stroke above where the point started (overlaps never stack).
                        const float start = float(old[idx]) * mm.step;
                        const float target = start + sign * layerHeight * f;
                        delta = sign > 0.0f ? std::max(0.0f, target - d) : std::min(0.0f, target - d);
                        break;
                    }
                    case BrushType::Pinch: delta = sign * sharpen(0.6f * s * f); break;
                    case BrushType::Crease:
                        delta = -sign * s * r * 0.05f * f * f + sharpen(0.5f * s * f);
                        break;
                    case BrushType::DamStandard:
                        delta = -sign * s * r * 0.08f * f * f * f * f + sharpen(0.7f * s * f);
                        break;
                    default: break;
                    }
                    coarse[cidx] = delta;
                    any = any || delta != 0.0f;
                }
            if (!any) continue;
            // Write: every grid point gets the change interpolated from the coarse points around it (stride 1: its own).
            int triMax = mm.triRange[t];
            auto apply = [&](uint32_t idx, float delta) {
                if (maskBrush) {
                    const float a = std::clamp(delta, 0.0f, 1.0f);
                    const float cur = float(mk[idx]) * (1.0f / 255.0f);
                    const float nv = invert ? cur * (1.0f - a) : cur + (1.0f - cur) * a;
                    mk[idx] = uint8_t(std::clamp(int(nv * 255.0f + 0.5f), 0, 255));
                    return;
                }
                if (stride > 1 && mk) delta *= 1.0f - float(mk[idx]) * (1.0f / 255.0f);
                const float nd = float(g[idx]) + delta * invStep; // (in units of the step)
                if (std::abs(nd) > 32767.0f) over = std::max(over, std::abs(nd) * mm.step);
                const int16_t q = quantizeUnits(nd);
                g[idx] = q;
                triMax = std::max(triMax, q >= 0 ? int(q) : -int(q));
            };
            if (stride == 1) {
                for (uint32_t k = 0; k < S; ++k)
                    if (coarse[k] != 0.0f) apply(k, coarse[k]);
            } else {
                interpolateCoarse(coarse.data(), n, stride, apply);
                count += S;
            }
            mm.triRange[t] = uint16_t(std::min(triMax, 32767));
        }
        visited[b / 8] = count;
        clipped[b / 8] = over;
    }, 8, 2);
    for (uint64_t c : visited) points += c;
    kernelMs += lapMs(clock);
    float over = 0.0f;
    for (float c : clipped) over = std::max(over, c);

    // 4. Stitch the shared points.
    stitch(m, mm, cand_);
    if (over > 0.0f) widen(mm, over * 1.5f);
    stitchMs += lapMs(clock);
}

void MicroStroke::widen(MicroMesh& mm, float need) {
    if (need > mm.rangeWorld()) dropAllCoefficients(mm); // (the heights are rescaled, which coefficients can't follow)
    const int div = mm.widen(need);
    if (div == 1) return;
    for (int16_t& v : oldDisp_) v = int16_t(int(v) / div);
    strokeStep_ = mm.step;
}

void MicroStroke::beginGrab(Mesh& m, const BVH& bvh, const glm::vec3& center, float radius, float focal, const glm::mat3& mirror) {
    if (!active_ || !m.micro) return;
    MicroMesh& mm = *m.micro;
    const uint32_t n = mm.n;
    dropAllCoefficients(mm); // (Move works on heights)
    widen(mm, float(mm.rangeMax) * mm.step + radius); // room to move a radius either way
    // Scale-adaptive like dab(): the weights on grid points `stride` apart, interpolated in between when moving.
    uint32_t stride = 1;
    const float microEdge = baseEdge_ / float(n);
    if (adaptive && microEdge > 0.0f)
        while (stride < n && microEdge * float(stride * 2) <= radius / float(kBrushSamples)) stride *= 2;
    lastStride = stride;
    const uint32_t cm = n / stride, cs = (cm + 1) * (cm + 2) / 2;
    std::vector<uint32_t> cand;
    bvh.queryTriangles(m, center, radius + mm.bulgeMax + float(mm.rangeMax) * mm.step, cand);
    std::erase_if(cand, [&](uint32_t t) { return m.triangleHidden(t) || !triangleNear(m, mm, t, center, radius); });
    touch(mm, cand);
    GrabMirror gm;
    gm.mirror = mirror;
    gm.stride = stride;
    gm.firstTri = uint32_t(grabTris_.size());
    const size_t base = grabWeight_.size();
    grabTris_.insert(grabTris_.end(), cand.begin(), cand.end());
    grabWeight_.resize(base + cand.size() * cs, 0.0f);
    grabNormal_.resize(base + cand.size() * cs, glm::vec3(0.0f));
    std::vector<uint8_t> used(cand.size(), 0);
    poolFor(cand.size(), [&](size_t b, size_t e) {
        MicroTriangle T;
        for (size_t k = b; k < e; ++k) {
            const uint32_t t = cand[k];
            T.set(m, t, n);
            const int16_t* g = mm.disp.data() + size_t(t) * mm.samples;
            float* w = grabWeight_.data() + base + k * cs;
            glm::vec3* nrm = grabNormal_.data() + base + k * cs;
            uint32_t c = 0;
            for (uint32_t cj = 0; cj <= cm; ++cj)
                for (uint32_t ci = 0; ci + cj <= cm; ++ci, ++c) {
                    const uint32_t i = ci * stride, j = cj * stride;
                    glm::vec3 sp, nn;
                    T.eval(i, j, sp, nn);
                    const glm::vec3 p = sp + nn * (float(g[MicroMesh::index(i, j, n)]) * mm.step);
                    const float d = glm::distance(p, center);
                    if (d >= radius) continue;
                    w[c] = brushFalloff(d, radius, focal);
                    nrm[c] = nn;
                    used[k] = 1;
                }
        }
    }, 16);
    gm.tris = uint32_t(cand.size());
    grabMirrors_.push_back(gm);
    for (uint8_t u : used) points += u ? (size_t(cm + 1) * (cm + 2) / 2) : 0;
}

void MicroStroke::grab(Mesh& m, const glm::vec3& delta) {
    if (!active_ || !m.micro || grabMirrors_.empty()) return;
    auto clock = std::chrono::steady_clock::now();
    MicroMesh& mm = *m.micro;
    const uint32_t n = mm.n, S = mm.samples;
    const float invStep = 1.0f / mm.step;
    // One mirror after another (a point may be in two), each in parallel over its triangles.
    size_t weightBase = 0;
    for (const GrabMirror& gm : grabMirrors_) {
        const uint32_t cm = n / gm.stride, cs = (cm + 1) * (cm + 2) / 2;
        const glm::vec3 dm = gm.mirror * delta;
        poolFor(gm.tris, [&](size_t b, size_t e) {
            thread_local std::vector<float> coarse;
            coarse.resize(cs);
            for (size_t k = b; k < e; ++k) {
                const uint32_t t = grabTris_[gm.firstTri + k];
                const float* w = grabWeight_.data() + weightBase + k * cs;
                const glm::vec3* nrm = grabNormal_.data() + weightBase + k * cs;
                bool any = false;
                for (uint32_t c = 0; c < cs; ++c) {
                    coarse[c] = w[c] > 0.0f ? glm::dot(dm, nrm[c]) * w[c] : 0.0f;
                    any = any || coarse[c] != 0.0f;
                }
                if (!any) continue;
                int16_t* g = mm.disp.data() + size_t(t) * S;
                const uint8_t* mk = mm.hasMask() ? mm.mask.data() + size_t(t) * S : nullptr;
                int triMax = mm.triRange[t];
                interpolateCoarse(coarse.data(), n, gm.stride, [&](uint32_t idx, float dd) {
                    if (mk) dd *= 1.0f - float(mk[idx]) * (1.0f / 255.0f);
                    const int16_t q = quantizeUnits(float(g[idx]) + dd * invStep);
                    g[idx] = q;
                    triMax = std::max(triMax, q >= 0 ? int(q) : -int(q));
                });
                mm.triRange[t] = uint16_t(std::min(triMax, 32767));
            }
        }, 8);
        weightBase += size_t(gm.tris) * cs;
    }
    kernelMs += lapMs(clock);
    cand_.clear();
    if (++dabEpoch_ == 0) dabEpoch_ = 1;
    for (uint32_t t : grabTris_)
        if (dabStamp_[t] != dabEpoch_) {
            dabStamp_[t] = dabEpoch_;
            cand_.push_back(t);
        }
    stitch(m, mm, cand_);
    stitchMs += lapMs(clock);
}

void MicroStroke::restore(Mesh& m) {
    if (!active_ || !m.micro || tiles_.empty()) return;
    MicroMesh& mm = *m.micro;
    const size_t S = mm.samples;
    dropAllCoefficients(mm);
    poolFor(tiles_.size(), [&](size_t b, size_t e) {
        for (size_t i = b; i < e; ++i) {
            const size_t t = tiles_[i];
            std::copy_n(oldDisp_.data() + i * S, S, mm.disp.data() + t * S);
            if (mm.hasMask() && oldMask_.size() == tiles_.size() * S) std::copy_n(oldMask_.data() + i * S, S, mm.mask.data() + t * S);
        }
    }, 64);
    for (uint32_t t : tiles_) mm.markDirty(t);
    ++mm.revision;
}

std::unique_ptr<MicroUndo> MicroStroke::end(Mesh& m) {
    active_ = false;
    if (m.micro) {
        if (m.micro.get() == mm_) {
            dropAllCoefficients(*mm_); // (the step holds heights)
            mm_->lazy = nullptr;
        }
        mm_ = nullptr;
        m.micro->stroking = false;
        if (const uint64_t cap = currentTierConfig().residentCacheCapMB) m.micro->disp.trim(size_t(cap) << 20);
    }
    grabMirrors_.clear();
    grabTris_.clear();
    grabWeight_.clear();
    grabNormal_.clear();
    if (tiles_.empty() || !m.micro) {
        tiles_.clear();
        return nullptr;
    }
    auto u = std::make_unique<MicroUndo>();
    const MicroMesh& mm = *m.micro;
    const size_t S = mm.samples, count = tiles_.size();
    u->step = strokeStep_;
    u->samples = mm.samples;
    for (uint32_t t : tiles_) tileOf_[t] = 0;
    // The journal: tiles in Morton order of their centres, each the stroke's change, packed.
    AABB box;
    for (uint32_t t : tiles_) {
        const glm::uvec3 tr = m.triangles[t];
        box.expand(m.positions[tr.x]);
    }
    const glm::vec3 lo = box.min, ext = glm::max(box.extent(), glm::vec3(1e-20f));
    auto spread = [](uint32_t x) {
        uint64_t v = x & 0x1FFFFF;
        v = (v | v << 32) & 0x1F00000000FFFFull;
        v = (v | v << 16) & 0x1F0000FF0000FFull;
        v = (v | v << 8) & 0x100F00F00F00F00Full;
        v = (v | v << 4) & 0x10C30C30C30C30C3ull;
        v = (v | v << 2) & 0x1249249249249249ull;
        return v;
    };
    std::vector<std::pair<uint64_t, uint32_t>> order(count);
    for (size_t i = 0; i < count; ++i) {
        const glm::uvec3 tr = m.triangles[tiles_[i]];
        const glm::vec3 c = ((m.positions[tr.x] + m.positions[tr.y] + m.positions[tr.z]) / 3.0f - lo) / ext;
        const glm::uvec3 q(glm::clamp(c, 0.0f, 1.0f) * 2097151.0f);
        order[i] = {spread(q.x) | spread(q.y) << 1 | spread(q.z) << 2, uint32_t(i)};
    }
    std::sort(order.begin(), order.end());
    std::vector<int16_t> change(count * S), firsts(count);
    std::atomic<bool> fits{true};
    poolFor(count, [&](size_t b, size_t e) {
        for (size_t k = b; k < e; ++k) {
            const size_t i = order[k].second;
            const int16_t* now = mm.disp.data() + size_t(tiles_[i]) * S;
            const int16_t* was = oldDisp_.data() + i * S;
            // The change, predicted from the point before it in its row (a row's first point: from the row below;
            // the very first is kept apart), so a smooth change needs few bits.
            const uint32_t n = mm.n;
            uint32_t p = 0;
            for (uint32_t j = 0; j <= n; ++j)
                for (uint32_t i = 0; i + j <= n; ++i, ++p) {
                    const int d = int(now[p]) - int(was[p]);
                    int pred = 0;
                    if (i > 0) pred = int(now[p - 1]) - int(was[p - 1]);
                    else if (j > 0) {
                        const uint32_t q = MicroMesh::index(0, j - 1, n);
                        pred = int(now[q]) - int(was[q]);
                    }
                    const int r = d - pred;
                    if (r < -32767 || r > 32767) fits = false;
                    if (p == 0) firsts[k] = int16_t(std::clamp(d, -32767, 32767)), fits = fits && d >= -32767 && d <= 32767;
                    change[k * S + p] = p == 0 ? int16_t(0) : int16_t(r);
                }
        }
    }, 256);
    const bool masked = mm.hasMask() && oldMask_.size() == count * S;
    if (fits) {
        u->journal = true;
        u->tris.resize(count);
        u->keys.resize(count);
        for (size_t k = 0; k < count; ++k) u->tris[k] = tiles_[order[k].second], u->keys[k] = order[k].first;
        microPackTiles(change.data(), count, mm.samples, u->packed);
        u->firsts = std::move(firsts);
        if (masked) {
            u->mask.resize(count * S);
            for (size_t k = 0; k < count; ++k)
                std::copy_n(oldMask_.data() + size_t(order[k].second) * S, S, u->mask.data() + k * S);
        }
    } else { // (a change wider than 16 bits: whole old tiles)
        u->tris = tiles_;
        u->disp = std::move(oldDisp_);
        if (masked) u->mask = std::move(oldMask_);
    }
    u->maskAdded = maskAdded_;
    tiles_.clear();
    oldDisp_.clear();
    oldMask_.clear();
    return u;
}

} // namespace sf
