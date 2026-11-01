// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Unwrapping (UV Editor > Unwrap, uv_unwrap):
//
// 1. The surface is cut into pieces along the marked seams (By Face Groups: the face group borders too; Unfold
//    keeps the current islands). Without marked seams, a mesh that has UVs is cut along its island borders.
// 2. A piece that isn't a topological disk (a closed shell, a tube, a handle) gets extra cuts: the edges no dual
//    spanning tree crosses, pruned back to the loops that matter; a closed shell is first split across its middle.
// 3. Each piece is flattened: LSCM (least-squares conformal maps, Levy et al. 2002) solved by conjugate gradients
//    with an aggregation multigrid (core/QRemesh2Solver.h), or, for big pieces of Least Stretch, a Tutte embedding
//    (faster to solve); Least Stretch then refines it with as-rigid-as-possible local / global steps (Liu et al.
//    2008), so lengths are kept as well as angles.
// 4. The pieces are scaled to one texel density and packed (core/UvPack.cpp), or, for Unfold and for a
//    selection, fitted back where their old UVs were.
//
// Pieces are solved in parallel (big ones with parallel matrix products), so a million-triangle mesh unwraps in
// seconds rather than minutes.
#include "core/AutoUv.h"
#include "core/QRemesh2Solver.h"
#include "core/Mesh.h"
#include "core/ThreadPool.h"
#include "core/Uv.h"
#include "core/UvInternal.h"

#include <algorithm>
#include <array>
#include <complex>
#include <memory>
#include <atomic>
#include <cmath>
#include <limits>
#include <mutex>
#include <numeric>
#include <queue>

namespace sf {

using namespace uvdetail;

const char* uvUnwrapMethodId(UvUnwrapMethod m) {
    switch (m) {
    case UvUnwrapMethod::Auto: return "auto";
    case UvUnwrapMethod::Conformal: return "conformal";
    case UvUnwrapMethod::Isometric: return "isometric";
    case UvUnwrapMethod::Polygroups: return "polygroups";
    case UvUnwrapMethod::Planar: return "planar";
    case UvUnwrapMethod::Unfold: return "unfold";
    }
    return "isometric";
}

const char* uvUnwrapMethodLabel(UvUnwrapMethod m) {
    switch (m) {
    case UvUnwrapMethod::Auto: return "Auto Seams";
    case UvUnwrapMethod::Conformal: return "Conformal (LSCM)";
    case UvUnwrapMethod::Isometric: return "Least Stretch";
    case UvUnwrapMethod::Polygroups: return "By Face Groups";
    case UvUnwrapMethod::Planar: return "Planar";
    case UvUnwrapMethod::Unfold: return "Unfold Islands";
    }
    return "";
}

bool parseUvUnwrapMethod(const std::string& id, UvUnwrapMethod& m) {
    for (UvUnwrapMethod x : {UvUnwrapMethod::Auto, UvUnwrapMethod::Conformal, UvUnwrapMethod::Isometric,
                             UvUnwrapMethod::Polygroups, UvUnwrapMethod::Planar, UvUnwrapMethod::Unfold})
        if (id == uvUnwrapMethodId(x)) {
            m = x;
            return true;
        }
    if (id == "lscm" || id == "angle") return m = UvUnwrapMethod::Conformal, true;
    if (id == "arap" || id == "stretch" || id == "least_stretch") return m = UvUnwrapMethod::Isometric, true;
    if (id == "xatlas") return m = UvUnwrapMethod::Auto, true;
    if (id == "face_groups" || id == "groups") return m = UvUnwrapMethod::Polygroups, true;
    return false;
}

namespace {


// ---------------------------------------------------------------------------------------------------------------
// Sparse symmetric systems for one piece

template <class F>
void rowsFor(size_t n, bool parallel, F&& f) {
    if (parallel) poolFor(n, [&](size_t b, size_t e) { f(b, e); }, 1 << 12);
    else f(size_t(0), n);
}

// The triangle in its own plane: corners at (0,0), (l1,0), (x2,y2). False when degenerate.
bool localTriangle(const glm::vec3& p0, const glm::vec3& p1, const glm::vec3& p2, glm::dvec2 q[3]) {
    const glm::dvec3 e1 = glm::dvec3(p1 - p0), e2 = glm::dvec3(p2 - p0);
    const double l1 = glm::length(e1);
    const glm::dvec3 n = glm::cross(e1, e2);
    const double nl = glm::length(n);
    if (l1 <= 0.0 || nl <= 1e-30) return false;
    const glm::dvec3 x = e1 / l1, y = glm::cross(n / nl, x);
    q[0] = glm::dvec2(0.0);
    q[1] = glm::dvec2(l1, 0.0);
    q[2] = glm::dvec2(glm::dot(e2, x), glm::dot(e2, y));
    return true;
}

// Planar projection along the piece's mean normal (right-handed, so the layout isn't mirrored).
std::vector<glm::vec2> project(const std::vector<glm::vec3>& pos, const std::vector<glm::uvec3>& tris) {
    glm::dvec3 n(0.0), centre(0.0);
    double area = 0.0;
    for (const glm::uvec3& t : tris) {
        const glm::dvec3 c = glm::cross(glm::dvec3(pos[t.y] - pos[t.x]), glm::dvec3(pos[t.z] - pos[t.x]));
        n += c;
        area += glm::length(c);
    }
    for (const glm::vec3& p : pos) centre += glm::dvec3(p);
    centre /= double(std::max<size_t>(pos.size(), 1));
    if (glm::length(n) < 1e-3 * area || area <= 0.0) {
        // A closed piece: project along its thinnest direction instead (the smallest spread).
        glm::dvec3 lo(1e300), hi(-1e300);
        for (const glm::vec3& p : pos) lo = glm::min(lo, glm::dvec3(p)), hi = glm::max(hi, glm::dvec3(p));
        const glm::dvec3 d = hi - lo;
        n = d.x <= d.y && d.x <= d.z ? glm::dvec3(1, 0, 0) : d.y <= d.z ? glm::dvec3(0, 1, 0) : glm::dvec3(0, 0, 1);
    }
    n = glm::normalize(n);
    const glm::dvec3 helper = std::abs(n.z) < 0.9 ? glm::dvec3(0, 0, 1) : glm::dvec3(1, 0, 0);
    const glm::dvec3 x = glm::normalize(glm::cross(helper, n)), y = glm::cross(n, x);
    std::vector<glm::vec2> out(pos.size());
    for (size_t i = 0; i < pos.size(); ++i) {
        const glm::dvec3 d = glm::dvec3(pos[i]) - centre;
        out[i] = glm::vec2(float(glm::dot(d, x)), float(glm::dot(d, y)));
    }
    return out;
}

// Two vertices far apart on the piece (its extremes along its longest axis).
void autoPins(const std::vector<glm::vec3>& pos, uint32_t& a, uint32_t& b) {
    glm::vec3 lo(std::numeric_limits<float>::max()), hi(std::numeric_limits<float>::lowest());
    for (const glm::vec3& p : pos) lo = glm::min(lo, p), hi = glm::max(hi, p);
    const glm::vec3 d = hi - lo;
    const int axis = d.x >= d.y && d.x >= d.z ? 0 : d.y >= d.z ? 1 : 2;
    a = b = 0;
    for (uint32_t i = 0; i < pos.size(); ++i) {
        if (pos[i][axis] < pos[a][axis]) a = i;
        if (pos[i][axis] > pos[b][axis]) b = i;
    }
}

// Solves the free unknowns of A x = rhs (A from `trips` over all `n` unknowns, Hermitian positive semidefinite)
// with the `fixed` ones held at their values in x: A_ff x_f = rhs_f - A_fp x_p, by CG preconditioned with an
// aggregation multigrid (core/QRemesh2Solver.h): a few dozen iterations where Jacobi took thousands.
template <class S>
struct PinnedSystem {
    std::vector<uint32_t> freeIndex; // unknown -> free index (~0u = fixed)
    std::vector<uint32_t> freeList;
    qr2::SparseMatrix<S> A;
    std::vector<std::pair<uint64_t, S>> coupling; // (free row << 32 | fixed unknown, value)
    std::unique_ptr<qr2::Multigrid<S>> mg;

    void build(size_t n, const std::vector<uint8_t>& fixed, const std::vector<std::pair<uint64_t, S>>& trips) {
        freeIndex.assign(n, ~0u);
        freeList.clear();
        for (uint32_t i = 0; i < n; ++i)
            if (!fixed[i]) {
                freeIndex[i] = uint32_t(freeList.size());
                freeList.push_back(i);
            }
        std::vector<std::pair<uint64_t, S>> ff;
        ff.reserve(trips.size());
        coupling.clear();
        for (const auto& [key, v] : trips) {
            const uint32_t r = uint32_t(key >> 32), c = uint32_t(key & 0xffffffffu);
            if (freeIndex[r] == ~0u) continue;
            if (freeIndex[c] != ~0u) ff.push_back({(uint64_t(freeIndex[r]) << 32) | freeIndex[c], v});
            else coupling.push_back({(uint64_t(freeIndex[r]) << 32) | c, v});
        }
        A.build(freeList.size(), ff);
        mg.reset();
        if (freeList.size() > 600) {
            mg = std::make_unique<qr2::Multigrid<S>>();
            mg->build(A);
        }
    }
    // x: every unknown (fixed ones hold their values); `rhs` (optional, per unknown) is the right-hand side.
    void solve(std::vector<S>& x, const std::vector<S>* rhs, double tol, int maxIt) const {
        const size_t nf = freeList.size();
        if (nf == 0) return;
        std::vector<S> b(nf, S{}), xf(nf);
        for (size_t i = 0; i < nf; ++i) {
            xf[i] = x[freeList[i]];
            if (rhs) b[i] = (*rhs)[freeList[i]];
        }
        for (const auto& [key, v] : coupling) b[key >> 32] -= v * x[key & 0xffffffffu];
        bool stop = false;
        qr2::pcg<S>([&](const std::vector<S>& in, std::vector<S>& out) { A.mul(in, out); }, mg.get(), A.diag, b, xf, tol,
                    maxIt, {}, stop);
        for (size_t i = 0; i < nf; ++i) x[freeList[i]] = xf[i];
    }
};

bool flattenConformalImpl(const std::vector<glm::vec3>& pos, const std::vector<glm::uvec3>& tris, std::vector<glm::vec2>& uv,
                          std::vector<uint32_t> pins, bool) {
    using C = std::complex<double>;
    const size_t n = pos.size();
    if (n < 3 || tris.empty()) return false;
    const bool haveGuess = uv.size() == n;
    std::vector<glm::vec2> guess = project(pos, tris);
    if (pins.size() < 2) {
        uint32_t a = 0, b = 0;
        if (pins.size() == 1 && haveGuess) {
            // The other pin: the vertex farthest from it, placed by the projection relative to it.
            a = pins[0];
            float far = -1.0f;
            for (uint32_t i = 0; i < n; ++i)
                if (const float d = glm::distance(pos[i], pos[a]); d > far) far = d, b = i;
            for (uint32_t i = 0; i < n; ++i) guess[i] = guess[i] - guess[a] + uv[a];
            uv[b] = guess[b];
            pins.push_back(b);
        } else {
            autoPins(pos, a, b);
            if (a == b) b = (a + 1) % uint32_t(n);
            pins = {a, b};
        }
    }
    std::vector<C> x(n);
    for (uint32_t i = 0; i < n; ++i) {
        const glm::vec2 g = haveGuess ? uv[i] : guess[i];
        x[i] = C(g.x, g.y);
    }
    std::vector<uint8_t> fixed(n, 0);
    for (uint32_t p : pins) fixed[p] = 1;
    // |sum_j w_j U_j|^2 per triangle (U = u + i v): A_jk += conj(w_j) w_k.
    std::vector<std::pair<uint64_t, C>> trips;
    trips.reserve(tris.size() * 9);
    for (const glm::uvec3& t : tris) {
        glm::dvec2 q[3];
        if (!localTriangle(pos[t.x], pos[t.y], pos[t.z], q)) continue;
        const double dT = std::sqrt(std::abs((q[1].x - q[0].x) * (q[2].y - q[0].y) - (q[2].x - q[0].x) * (q[1].y - q[0].y)));
        if (dT <= 0.0) continue;
        C w[3];
        for (int j = 0; j < 3; ++j) {
            const glm::dvec2 d = q[(j + 2) % 3] - q[(j + 1) % 3];
            w[j] = C(d.x, d.y) / dT;
        }
        for (int j = 0; j < 3; ++j)
            for (int k = 0; k < 3; ++k) trips.push_back({(uint64_t(t[j]) << 32) | t[k], std::conj(w[j]) * w[k]});
    }
    PinnedSystem<C> sys;
    sys.build(n, fixed, trips);
    sys.solve(x, nullptr, 1e-8, 1500);
    uv.resize(n);
    for (uint32_t i = 0; i < n; ++i) uv[i] = glm::vec2(float(x[i].real()), float(x[i].imag()));
    for (const glm::vec2& u : uv)
        if (!std::isfinite(u.x) || !std::isfinite(u.y)) return false;
    return true;
}

bool flattenIsometricImpl(const std::vector<glm::vec3>& pos, const std::vector<glm::uvec3>& tris, std::vector<glm::vec2>& uv,
                          int iterations, const std::vector<uint32_t>& pins, bool parallel) {
    const size_t n = pos.size(), nt = tris.size();
    // Big pieces without pins start from a Tutte embedding (the border on a circle, the inside harmonic: one
    // well-conditioned solve, never folded) instead of LSCM, whose free border converges slowly; the
    // as-rigid-as-possible steps then take it to the same kind of result.
    const bool tutte = pins.size() < 2 && n > 3000 && iterations > 0;
    if (!tutte) {
        if (!flattenConformalImpl(pos, tris, uv, pins, parallel)) return false;
        if (iterations <= 0) return true;
    }
    // LSCM's result is free in scale: bring it to the surface's before matching lengths.
    if (!tutte && pins.size() < 2) {
        double a3 = 0.0, au = 0.0;
        for (const glm::uvec3& t : tris) {
            a3 += 0.5 * glm::length(glm::cross(glm::dvec3(pos[t.y] - pos[t.x]), glm::dvec3(pos[t.z] - pos[t.x])));
            au += 0.5 * std::abs(double(cross2(uv[t.y] - uv[t.x], uv[t.z] - uv[t.x])));
        }
        if (au > 0.0) {
            const float k = float(std::sqrt(a3 / au));
            for (glm::vec2& u : uv) u *= k;
        }
    }
    std::vector<std::array<glm::dvec2, 3>> local(nt);
    std::vector<std::array<double, 3>> cot(nt); // per triangle: half the cotangent at each corner (its opposite edge's weight)
    std::vector<uint8_t> valid(nt, 0);
    std::vector<std::pair<uint64_t, double>> trips;
    trips.reserve(nt * 12);
    for (size_t t = 0; t < nt; ++t) {
        glm::dvec2 q[3];
        if (!localTriangle(pos[tris[t].x], pos[tris[t].y], pos[tris[t].z], q)) continue;
        valid[t] = 1;
        for (int j = 0; j < 3; ++j) {
            local[t][size_t(j)] = q[j];
            const glm::dvec2 e1 = q[(j + 1) % 3] - q[j], e2 = q[(j + 2) % 3] - q[j];
            const double s = std::abs(e1.x * e2.y - e1.y * e2.x);
            const double c = glm::dot(e1, e2);
            const double w = std::max(s > 1e-30 ? 0.5 * c / s : 0.0, 1e-3); // (clamped: keeps the system positive)
            cot[t][size_t(j)] = w;
            const uint64_t a = tris[t][(j + 1) % 3], b = tris[t][(j + 2) % 3];
            trips.push_back({(a << 32) | a, w});
            trips.push_back({(b << 32) | b, w});
            trips.push_back({(a << 32) | b, -w});
            trips.push_back({(b << 32) | a, -w});
        }
    }
    if (tutte) {
        // The border: half-edges whose reverse isn't in the piece, chained into a loop.
        std::vector<std::pair<uint64_t, uint32_t>> half; // (a << 32 | b, ...)
        half.reserve(nt * 3);
        for (const glm::uvec3& t : tris)
            for (int j = 0; j < 3; ++j) half.push_back({(uint64_t(t[j]) << 32) | t[(j + 1) % 3], 0});
        std::sort(half.begin(), half.end());
        std::vector<uint32_t> next(n, ~0u);
        size_t borderEdges = 0;
        for (const auto& h : half) {
            const uint32_t a = uint32_t(h.first >> 32), b = uint32_t(h.first & 0xffffffffu);
            const uint64_t rev = (uint64_t(b) << 32) | a;
            if (std::binary_search(half.begin(), half.end(), std::pair<uint64_t, uint32_t>{rev, 0})) continue;
            next[a] = b;
            ++borderEdges;
        }
        std::vector<uint32_t> loop;
        uint32_t start = ~0u;
        for (uint32_t v = 0; v < n && start == ~0u; ++v)
            if (next[v] != ~0u) start = v;
        for (uint32_t v = start; v != ~0u && loop.size() <= borderEdges;) {
            loop.push_back(v);
            v = next[v];
            if (v == start) break;
        }
        if (loop.size() < 3 || loop.size() != borderEdges) {
            // Not one simple border loop: LSCM instead.
            if (!flattenConformalImpl(pos, tris, uv, pins, parallel)) return false;
        } else {
            double length = 0.0;
            std::vector<double> at(loop.size() + 1, 0.0);
            for (size_t k = 0; k < loop.size(); ++k)
                at[k + 1] = length += glm::distance(glm::dvec3(pos[loop[k]]), glm::dvec3(pos[loop[(k + 1) % loop.size()]]));
            const double r = length / (2.0 * 3.14159265358979);
            std::vector<uint8_t> border(n, 0);
            std::vector<double> hu(n, 0.0), hv(n, 0.0);
            for (size_t k = 0; k < loop.size(); ++k) {
                const double a = 2.0 * 3.14159265358979 * at[k] / length;
                border[loop[k]] = 1;
                hu[loop[k]] = r * std::cos(a), hv[loop[k]] = r * std::sin(a);
            }
            PinnedSystem<double> harmonic;
            harmonic.build(n, border, trips);
            harmonic.solve(hu, nullptr, 1e-8, 300);
            harmonic.solve(hv, nullptr, 1e-8, 300);
            uv.resize(n);
            for (uint32_t i = 0; i < n; ++i) uv[i] = glm::vec2(float(hu[i]), float(hv[i]));
            for (const glm::vec2& x : uv)
                if (!std::isfinite(x.x) || !std::isfinite(x.y)) return flattenConformalImpl(pos, tris, uv = {}, pins, parallel);
            iterations += 10; // (further from the result than LSCM: a few more steps)
        }
    }
    std::vector<uint8_t> fixed(n, 0);
    if (pins.empty()) fixed[0] = 1;
    for (uint32_t p : pins) fixed[p] = 1;
    PinnedSystem<double> sys;
    sys.build(n, fixed, trips);
    std::vector<double> xu(n), xv(n), bu(n), bv(n);
    for (uint32_t i = 0; i < n; ++i) xu[i] = uv[i].x, xv[i] = uv[i].y;
    std::vector<glm::dmat2> R(nt, glm::dmat2(1.0));
    for (int it = 0; it < iterations; ++it) {
        // Local step: each triangle's best rotation onto its current layout.
        rowsFor(nt, parallel, [&](size_t b0, size_t e0) {
            for (size_t t = b0; t < e0; ++t) {
                if (!valid[t]) continue;
                double s00 = 0, s01 = 0, s10 = 0, s11 = 0;
                for (int j = 0; j < 3; ++j) {
                    const uint32_t a = tris[t][(j + 1) % 3], b = tris[t][(j + 2) % 3];
                    const double w = cot[t][size_t(j)];
                    const glm::dvec2 du(xu[a] - xu[b], xv[a] - xv[b]);
                    const glm::dvec2 dx = local[t][size_t((j + 1) % 3)] - local[t][size_t((j + 2) % 3)];
                    s00 += w * du.x * dx.x, s01 += w * du.x * dx.y, s10 += w * du.y * dx.x, s11 += w * du.y * dx.y;
                }
                const double th = std::atan2(s10 - s01, s00 + s11);
                const double c = std::cos(th), s = std::sin(th);
                R[t] = glm::dmat2(c, s, -s, c); // (column-major: [[c, -s], [s, c]])
            }
        });
        // Global step: L u = the sum of each triangle's rotated edges.
        std::fill(bu.begin(), bu.end(), 0.0);
        std::fill(bv.begin(), bv.end(), 0.0);
        for (size_t t = 0; t < nt; ++t) {
            if (!valid[t]) continue;
            for (int j = 0; j < 3; ++j) {
                const uint32_t a = tris[t][(j + 1) % 3], b = tris[t][(j + 2) % 3];
                const double w = cot[t][size_t(j)];
                const glm::dvec2 e = R[t] * (local[t][size_t((j + 1) % 3)] - local[t][size_t((j + 2) % 3)]) * w;
                bu[a] += e.x, bv[a] += e.y;
                bu[b] -= e.x, bv[b] -= e.y;
            }
        }
        sys.solve(xu, &bu, 1e-7, 200);
        sys.solve(xv, &bv, 1e-7, 200);
    }
    for (uint32_t i = 0; i < n; ++i)
        if (!std::isfinite(xu[i]) || !std::isfinite(xv[i])) return true; // keep the conformal result
    for (uint32_t i = 0; i < n; ++i) uv[i] = glm::vec2(float(xu[i]), float(xv[i]));
    return true;
}

// ---------------------------------------------------------------------------------------------------------------
// Pieces

struct Pieces {
    std::vector<uint32_t> piece;     // per triangle (kNoCorner: not unwrapped)
    uint32_t count = 0;
    std::vector<uint32_t> vertex;    // per corner: its vertex of the cut surface (shared by the corners it joins)
    uint32_t vertexCount = 0;
};

Pieces buildPieces(const Mesh& m, const std::vector<uint32_t>& twin, const std::vector<uint8_t>& cut,
                   const std::vector<uint8_t>* faces) {
    const size_t nt = m.triangleCount(), nc = nt * 3;
    Pieces p;
    DisjointSets corners(nc), tris(nt);
    for (uint32_t c = 0; c < nc; ++c) {
        const uint32_t d = twin[c];
        if (cut[c] || d == kNoCorner || d < c) continue;
        if (faces && (!(*faces)[c / 3] || !(*faces)[d / 3])) continue;
        corners.unite(c, cornerNext(d));
        corners.unite(cornerNext(c), d);
        tris.unite(c / 3, d / 3);
    }
    std::vector<uint32_t> label;
    tris.labels(label);
    p.piece.assign(nt, kNoCorner);
    std::vector<uint32_t> id(nt, kNoCorner);
    for (uint32_t t = 0; t < nt; ++t) {
        if (faces && !(*faces)[t]) continue;
        if (id[label[t]] == kNoCorner) id[label[t]] = p.count++;
        p.piece[t] = id[label[t]];
    }
    p.vertexCount = corners.labels(p.vertex);
    return p;
}

// The cut-graph edges (corners) that make piece `triangles` a disk, or empty when it already is one.
std::vector<uint32_t> diskCuts(const Mesh& m, const std::vector<uint32_t>& twin, const std::vector<uint8_t>& cut,
                               const Pieces& pc, const std::vector<uint32_t>& triangles, long long* eulerOut = nullptr) {
    // Local numbering of the piece's vertices.
    std::vector<uint32_t> vlist;
    for (uint32_t t : triangles)
        for (uint32_t j = 0; j < 3; ++j) vlist.push_back(pc.vertex[t * 3 + j]);
    std::sort(vlist.begin(), vlist.end());
    vlist.erase(std::unique(vlist.begin(), vlist.end()), vlist.end());
    auto local = [&](uint32_t c) { return uint32_t(std::lower_bound(vlist.begin(), vlist.end(), pc.vertex[c]) - vlist.begin()); };
    const size_t V = vlist.size(), F = triangles.size();
    auto isIn = [&](uint32_t t) { return std::binary_search(triangles.begin(), triangles.end(), t); };
    size_t boundary = 0;
    for (uint32_t t : triangles)
        for (uint32_t j = 0; j < 3; ++j) {
            const uint32_t c = t * 3 + j, d = twin[c];
            if (cut[c] || d == kNoCorner || !isIn(d / 3)) ++boundary;
        }
    const long long E = (3 * (long long)F + (long long)boundary) / 2;
    const long long chi = (long long)V - E + (long long)F;
    if (eulerOut) *eulerOut = chi;
    if (chi == 1) return {};
    if (chi < 0 && eulerOut) {
        // Tangled (several holes or handles): a cut graph would make one island with slits everywhere. Cut it into
        // charts the way Auto Seams does instead (each then flattened like any piece).
        std::vector<uint32_t> vmap(m.vertexCount(), ~0u);
        std::vector<glm::vec3> pos;
        std::vector<glm::uvec3> tris(F);
        for (size_t k = 0; k < F; ++k)
            for (int j = 0; j < 3; ++j) {
                const uint32_t v = m.triangles[triangles[k]][j];
                if (vmap[v] == ~0u) vmap[v] = uint32_t(pos.size()), pos.push_back(m.positions[v]);
                tris[k][j] = vmap[v];
            }
        std::vector<uint32_t> chart;
        int charts = 0;
        std::string error;
        if (computeAutoCharts(pos, tris, chart, charts, error) && charts > 1) {
            std::vector<uint32_t> out;
            for (size_t k = 0; k < F; ++k)
                for (uint32_t j = 0; j < 3; ++j) {
                    const uint32_t c = triangles[k] * 3 + j, d = twin[c];
                    if (cut[c] || d == kNoCorner || d < c || !std::binary_search(triangles.begin(), triangles.end(), d / 3)) continue;
                    const size_t kd = size_t(std::lower_bound(triangles.begin(), triangles.end(), d / 3) - triangles.begin());
                    if (chart[k] != chart[kd]) out.push_back(c);
                }
            if (!out.empty()) return out;
        }
    }
    // Dual spanning tree (breadth first from the first triangle) over the piece's inner edges.
    auto inner = [&](uint32_t c) {
        const uint32_t d = twin[c];
        return !cut[c] && d != kNoCorner && isIn(d / 3);
    };
    std::vector<uint8_t> tree(m.triangleCount() * 3, 0);
    {
        std::vector<uint8_t> seen(F, 0);
        auto index = [&](uint32_t t) { return size_t(std::lower_bound(triangles.begin(), triangles.end(), t) - triangles.begin()); };
        std::queue<uint32_t> q;
        q.push(triangles[0]);
        seen[0] = 1;
        while (!q.empty()) {
            const uint32_t t = q.front();
            q.pop();
            for (uint32_t j = 0; j < 3; ++j) {
                const uint32_t c = t * 3 + j;
                if (!inner(c)) continue;
                const uint32_t u = twin[c] / 3;
                const size_t k = index(u);
                if (seen[k]) continue;
                seen[k] = 1;
                tree[c] = tree[twin[c]] = 1;
                q.push(u);
            }
        }
    }
    // Candidate cut edges: inner edges the tree doesn't cross. Prune those hanging off a vertex of degree one.
    struct Edge {
        uint32_t corner, a, b;
        bool alive = true;
    };
    std::vector<Edge> edges;
    std::vector<uint32_t> degree(V, 0);
    for (uint32_t t : triangles)
        for (uint32_t j = 0; j < 3; ++j) {
            const uint32_t c = t * 3 + j;
            const uint32_t a = local(c), b = local(cornerNext(c));
            if (!inner(c)) {
                ++degree[a], ++degree[b]; // a border half-edge (its loop gives each of its vertices two)
                continue;
            }
            if (tree[c] || twin[c] < c) continue;
            edges.push_back({c, a, b});
            ++degree[a], ++degree[b];
        }
    std::vector<std::vector<uint32_t>> incident(V);
    for (uint32_t e = 0; e < edges.size(); ++e) incident[edges[e].a].push_back(e), incident[edges[e].b].push_back(e);
    std::vector<uint32_t> stack;
    for (uint32_t v = 0; v < V; ++v)
        if (degree[v] == 1) stack.push_back(v);
    while (!stack.empty()) {
        const uint32_t v = stack.back();
        stack.pop_back();
        if (degree[v] != 1) continue;
        for (uint32_t e : incident[v]) {
            if (!edges[e].alive) continue;
            edges[e].alive = false;
            const uint32_t o = edges[e].a == v ? edges[e].b : edges[e].a;
            --degree[v], --degree[o];
            if (degree[o] == 1) stack.push_back(o);
            break;
        }
    }
    std::vector<uint32_t> out;
    for (const Edge& e : edges)
        if (e.alive) out.push_back(e.corner);
    if (!out.empty() || boundary > 0) return out;
    // A closed shell (a sphere): cut in two across the middle of its longest side, so each half flattens like a
    // dome (one cut along a path would leave the whole shell to unfold around it, with huge stretch). The halves
    // are checked again in the next round.
    {
        glm::vec3 lo(std::numeric_limits<float>::max()), hi(std::numeric_limits<float>::lowest());
        std::vector<float> key(F);
        for (uint32_t t : triangles)
            for (uint32_t j = 0; j < 3; ++j) lo = glm::min(lo, m.positions[m.triangles[t][j]]), hi = glm::max(hi, m.positions[m.triangles[t][j]]);
        const glm::vec3 d = hi - lo;
        const int axis = d.x >= d.y && d.x >= d.z ? 0 : d.y >= d.z ? 1 : 2;
        for (size_t k = 0; k < F; ++k) {
            const glm::uvec3 tri = m.triangles[triangles[k]];
            key[k] = (m.positions[tri.x][axis] + m.positions[tri.y][axis] + m.positions[tri.z][axis]) / 3.0f;
        }
        std::vector<float> sorted = key;
        std::nth_element(sorted.begin(), sorted.begin() + std::ptrdiff_t(F / 2), sorted.end());
        const float mid = sorted[F / 2];
        auto side = [&](uint32_t t) {
            const size_t k = size_t(std::lower_bound(triangles.begin(), triangles.end(), t) - triangles.begin());
            return key[k] >= mid;
        };
        for (uint32_t t : triangles)
            for (uint32_t j = 0; j < 3; ++j) {
                const uint32_t c = t * 3 + j;
                if (inner(c) && twin[c] > c && side(t) != side(twin[c] / 3)) out.push_back(c);
            }
        if (!out.empty() && out.size() * 4 < 3 * F) return out;
        out.clear();
    }
    // Too small to split: cut along the longest shortest path, between two far-apart vertices.
    std::vector<std::vector<std::pair<uint32_t, uint32_t>>> adj(V); // (neighbour, corner)
    std::vector<glm::vec3> vpos(V);
    for (uint32_t t : triangles)
        for (uint32_t j = 0; j < 3; ++j) {
            const uint32_t c = t * 3 + j;
            vpos[local(c)] = m.positions[m.triangles[t][j]];
            if (inner(c) && twin[c] > c) {
                const uint32_t a = local(c), b = local(cornerNext(c));
                adj[a].push_back({b, c});
                adj[b].push_back({a, c});
            }
        }
    auto dijkstra = [&](uint32_t s, std::vector<uint32_t>& via) {
        std::vector<float> dist(V, std::numeric_limits<float>::max());
        via.assign(V, kNoCorner);
        using Item = std::pair<float, uint32_t>;
        std::priority_queue<Item, std::vector<Item>, std::greater<>> q;
        dist[s] = 0.0f;
        q.push({0.0f, s});
        uint32_t far = s;
        while (!q.empty()) {
            const auto [d, v] = q.top();
            q.pop();
            if (d > dist[v]) continue;
            far = v;
            for (auto [w, c] : adj[v])
                if (const float nd = d + glm::distance(vpos[v], vpos[w]); nd < dist[w]) {
                    dist[w] = nd;
                    via[w] = c;
                    q.push({nd, w});
                }
        }
        return far;
    };
    std::vector<uint32_t> via;
    const uint32_t a = dijkstra(0, via);
    const uint32_t b = dijkstra(a, via);
    for (uint32_t v = b; v != a && via[v] != kNoCorner;) {
        const uint32_t c = via[v];
        out.push_back(c);
        const uint32_t x = local(c), y = local(cornerNext(c));
        v = x == v ? y : x;
    }
    if (out.empty() && !adj[0].empty()) out.push_back(adj[0][0].second);
    return out;
}

// The similarity (no mirror) that best maps `from` onto `to`: to ~ s R from + t.
void fitSimilarity(const std::vector<glm::vec2>& from, const std::vector<glm::vec2>& to, float& scale, float& angle,
                   glm::vec2& cf, glm::vec2& ct, bool fixedCentres = false) {
    glm::dvec2 a(cf), b(ct);
    if (!fixedCentres) {
        a = b = glm::dvec2(0.0);
        for (size_t i = 0; i < from.size(); ++i) a += glm::dvec2(from[i]), b += glm::dvec2(to[i]);
        a /= double(from.size()), b /= double(from.size());
    }
    double dotSum = 0.0, crossSum = 0.0, norm = 0.0;
    for (size_t i = 0; i < from.size(); ++i) {
        const glm::dvec2 p = glm::dvec2(from[i]) - a, q = glm::dvec2(to[i]) - b;
        dotSum += p.x * q.x + p.y * q.y;
        crossSum += p.x * q.y - p.y * q.x;
        norm += p.x * p.x + p.y * p.y;
    }
    angle = float(std::atan2(crossSum, dotSum));
    scale = norm > 0.0 ? float(std::sqrt(dotSum * dotSum + crossSum * crossSum) / norm) : 1.0f;
    cf = glm::vec2(a);
    ct = glm::vec2(b);
}

} // namespace

bool flattenConformal(const std::vector<glm::vec3>& positions, const std::vector<glm::uvec3>& triangles, std::vector<glm::vec2>& uv,
                      const std::vector<uint32_t>& pins) {
    return flattenConformalImpl(positions, triangles, uv, pins, positions.size() > 20000);
}

bool flattenIsometric(const std::vector<glm::vec3>& positions, const std::vector<glm::uvec3>& triangles, std::vector<glm::vec2>& uv,
                      int iterations, const std::vector<uint32_t>& pins) {
    return flattenIsometricImpl(positions, triangles, uv, iterations, pins, positions.size() > 20000);
}

bool uvUnwrap(const Mesh& m, const UvUnwrapOptions& o, UvUnwrapResult& result, std::string& error, const UvProgress& progress) {
    result = UvUnwrapResult();
    const size_t nt = m.triangleCount(), nc = nt * 3;
    if (nt == 0) {
        error = "There is no mesh to unwrap";
        return false;
    }
    const std::vector<uint8_t>* faces = o.faces && o.faces->size() == nt ? o.faces : nullptr;
    if (faces && std::find(faces->begin(), faces->end(), uint8_t(1)) == faces->end()) faces = nullptr;
    const bool hadUvs = m.uvs.size() == nc;
    if (o.method == UvUnwrapMethod::Unfold && !hadUvs) {
        error = "Unfold needs UVs to start from: unwrap first";
        return false;
    }
    auto report = [&](float f) { return !progress || progress(std::clamp(f, 0.0f, 1.0f)); };

    // Auto seams: xatlas on the whole mesh (or the selected triangles), then packed or fitted like the others.
    if (o.method == UvUnwrapMethod::Auto) {
        std::vector<uint32_t> list;
        for (uint32_t t = 0; t < nt; ++t)
            if (!faces || (*faces)[t]) list.push_back(t);
        std::vector<glm::uvec3> tris(list.size());
        for (size_t i = 0; i < list.size(); ++i) tris[i] = m.triangles[list[i]];
        AutoUvResult r;
        if (!computeAutoUv(m.positions, tris, o.resolution, r, error, [&](float f) { return report(f * 0.9f); })) return false;
        result.uvs = hadUvs ? m.uvs : std::vector<glm::vec2>(nc, glm::vec2(0.0f));
        for (size_t i = 0; i < list.size(); ++i)
            for (uint32_t j = 0; j < 3; ++j) result.uvs[size_t(list[i]) * 3 + j] = r.uvs[i * 3 + j];
        result.islands = size_t(r.charts);
        Mesh tmp;
        tmp.positions = m.positions;
        tmp.triangles = m.triangles;
        tmp.uvs = result.uvs;
        const bool repack = !faces && (o.packing.tiles > 1 || o.packing.density > 0.0f);
        if (repack || faces) {
            UvTopology topo = buildUvTopology(tmp);
            std::vector<uint8_t> isl(topo.islandCount, faces ? 0 : 1);
            if (faces)
                for (uint32_t t : list) isl[topo.island[t]] = 1;
            UvPackOptions po = o.packing;
            if (faces) po.tiles = 1, po.density = 0.0f;
            if (!uvPack(tmp, topo, isl, po, result.packing, error)) return false;
            if (faces && hadUvs) { // into the old place of the selection
                glm::vec2 lo(1e30f), hi(-1e30f);
                for (uint32_t t : list)
                    for (uint32_t j = 0; j < 3; ++j) lo = glm::min(lo, m.uvs[t * 3 + j]), hi = glm::max(hi, m.uvs[t * 3 + j]);
                const float side = std::max(hi.x - lo.x, hi.y - lo.y);
                if (side > 0.0f)
                    for (uint32_t t : list)
                        for (uint32_t j = 0; j < 3; ++j) {
                            glm::vec2& u = tmp.uvs[t * 3 + j];
                            u = lo + u * side;
                        }
            }
            result.uvs = std::move(tmp.uvs);
        } else {
            result.packing.islands = result.islands;
            result.packing.tilesUsed = 1;
        }
        report(1.0f);
        return true;
    }

    // 1. Cuts.
    const std::vector<uint32_t> twin = cornerTwins(m);
    const bool flags = m.uvFlags.size() == nc;
    bool anySeam = false;
    if (flags)
        for (uint8_t f : m.uvFlags) anySeam = anySeam || (f & kUvSeam);
    const bool useIslands = o.method == UvUnwrapMethod::Unfold || (!anySeam && hadUvs);
    UvTopology topo;
    if (useIslands) topo = buildUvTopology(m, twin);
    std::vector<uint8_t> cut(nc, 0);
    for (uint32_t c = 0; c < nc; ++c) {
        const uint32_t d = twin[c];
        bool x = d == kNoCorner;
        if (!x && flags) x = (m.uvFlags[c] | m.uvFlags[d]) & kUvSeam;
        if (!x && useIslands) x = topo.border[c];
        if (!x && o.method == UvUnwrapMethod::Polygroups) x = m.polygroupAt(c / 3) != m.polygroupAt(d / 3);
        cut[c] = x;
    }
    // 2. Pieces, with extra cuts until every piece is a disk.
    Pieces pc;
    std::vector<uint32_t> offsets, tris;
    auto group = [&]() {
        pc = buildPieces(m, twin, cut, faces);
        offsets.assign(pc.count + 1, 0);
        for (uint32_t t = 0; t < nt; ++t)
            if (pc.piece[t] != kNoCorner) ++offsets[pc.piece[t] + 1];
        for (uint32_t i = 0; i < pc.count; ++i) offsets[i + 1] += offsets[i];
        std::vector<uint32_t> at(offsets.begin(), offsets.end() - 1);
        tris.assign(offsets.back(), 0);
        for (uint32_t t = 0; t < nt; ++t)
            if (pc.piece[t] != kNoCorner) tris[at[pc.piece[t]]++] = t;
    };
    group();
    if (o.method != UvUnwrapMethod::Planar)
        for (int round = 0; round < 4; ++round) {
            std::vector<std::vector<uint32_t>> extra(pc.count);
            poolFor(pc.count, [&](size_t b, size_t e) {
                for (size_t i = b; i < e; ++i) {
                    const std::vector<uint32_t> list(tris.begin() + offsets[i], tris.begin() + offsets[i + 1]);
                    long long euler = 1;
                    extra[i] = diskCuts(m, twin, cut, pc, list, round == 0 ? &euler : nullptr);
                }
            }, 1);
            size_t added = 0;
            for (const auto& ex : extra)
                for (uint32_t c : ex) {
                    cut[c] = 1;
                    if (twin[c] != kNoCorner) cut[twin[c]] = 1;
                    ++added;
                }
            if (!added) break;
            result.extraCuts += added;
            group();
        }
    if (!report(0.05f)) {
        error = "Unwrap was cancelled";
        return false;
    }
    // 3. Flatten each piece.
    std::vector<glm::vec2> vuv(pc.vertexCount, glm::vec2(0.0f));
    std::vector<uint8_t> pinnedPiece(pc.count, 0);
    std::vector<uint32_t> pinCorner(pc.count, kNoCorner); // a piece with one pin: that corner (it stays put)
    std::vector<uint32_t> order(pc.count);
    std::iota(order.begin(), order.end(), 0u);
    std::sort(order.begin(), order.end(), [&](uint32_t a, uint32_t b) { return offsets[a + 1] - offsets[a] > offsets[b + 1] - offsets[b]; });
    std::atomic<size_t> done{0};
    std::atomic<bool> cancelled{false};
    std::mutex progressMutex;
    const bool keepPins = hadUvs && flags && o.method != UvUnwrapMethod::Planar;
    auto solve = [&](uint32_t i, bool parallel) {
        if (cancelled) return;
        const uint32_t b = offsets[i], e = offsets[i + 1];
        std::vector<uint32_t> verts;
        for (uint32_t k = b; k < e; ++k)
            for (uint32_t j = 0; j < 3; ++j) verts.push_back(pc.vertex[tris[k] * 3 + j]);
        std::sort(verts.begin(), verts.end());
        verts.erase(std::unique(verts.begin(), verts.end()), verts.end());
        auto local = [&](uint32_t c) { return uint32_t(std::lower_bound(verts.begin(), verts.end(), pc.vertex[c]) - verts.begin()); };
        std::vector<glm::vec3> pos(verts.size());
        std::vector<glm::uvec3> lt(e - b);
        std::vector<glm::vec2> uv;
        std::vector<uint32_t> pins;
        std::vector<uint8_t> isPin(verts.size(), 0);
        uint32_t firstPin = kNoCorner;
        if (hadUvs) uv.assign(verts.size(), glm::vec2(0.0f));
        for (uint32_t k = b; k < e; ++k) {
            const uint32_t t = tris[k];
            for (uint32_t j = 0; j < 3; ++j) {
                const uint32_t c = t * 3 + j, l = local(c);
                lt[k - b][int(j)] = l;
                pos[l] = m.positions[m.triangles[t][j]];
                if (hadUvs) uv[l] = m.uvs[c];
                if (keepPins && (m.uvFlags[c] & kUvPin) && !isPin[l]) {
                    isPin[l] = 1;
                    pins.push_back(l);
                    if (firstPin == kNoCorner) firstPin = c;
                }
            }
        }
        if (pins.size() == 1) pinCorner[i] = firstPin; // (placed by the fit below, about the pin)
        if (pins.size() < 2) { // (a starting guess only matters with pins)
            uv.clear();
            pins.clear();
        }
        bool ok = false;
        if (o.method == UvUnwrapMethod::Planar) {
            uv = project(pos, lt);
            ok = true;
        } else if (o.method == UvUnwrapMethod::Conformal) {
            ok = flattenConformalImpl(pos, lt, uv, pins, parallel);
        } else {
            ok = flattenIsometricImpl(pos, lt, uv, std::max(o.iterations, 0), pins, parallel);
        }
        if (ok)
            for (const glm::vec2& x : uv)
                if (!std::isfinite(x.x) || !std::isfinite(x.y)) ok = false;
        if (!ok || uv.size() != pos.size()) uv = project(pos, lt); // (a piece the solver can't flatten still gets UVs)
        if (pins.size() < 2) {
            // One texel density: UV area = surface area, centred.
            double a3 = 0.0, au = 0.0;
            for (const glm::uvec3& t : lt) {
                a3 += 0.5 * glm::length(glm::cross(glm::dvec3(pos[t.y] - pos[t.x]), glm::dvec3(pos[t.z] - pos[t.x])));
                au += double(cross2(uv[t.y] - uv[t.x], uv[t.z] - uv[t.x])) * 0.5;
            }
            if (au < 0.0) { // mirrored: turn it the right way round
                for (glm::vec2& u : uv) u.x = -u.x;
                au = -au;
            }
            const float k = au > 0.0 ? float(std::sqrt(a3 / au)) : 1.0f;
            glm::vec2 lo(1e30f), hi(-1e30f);
            for (const glm::vec2& u : uv) lo = glm::min(lo, u), hi = glm::max(hi, u);
            const glm::vec2 mid = (lo + hi) * 0.5f;
            for (glm::vec2& u : uv) u = (u - mid) * k;
        } else {
            pinnedPiece[i] = 1;
        }
        for (uint32_t l = 0; l < verts.size(); ++l) vuv[verts[l]] = uv[l];
        const size_t d = done.fetch_add(e - b) + (e - b);
        std::lock_guard lock(progressMutex);
        if (!report(0.05f + 0.85f * float(double(d) / double(std::max<size_t>(tris.size(), 1))))) cancelled = true;
    };
    // Pieces in parallel; a piece holding most of the mesh first on its own, with parallel products.
    size_t first = 0;
    while (first < order.size() && size_t(offsets[order[first] + 1] - offsets[order[first]]) * 3 > tris.size() &&
           offsets[order[first] + 1] - offsets[order[first]] > 20000)
        solve(order[first++], true);
    poolFor(order.size() - first, [&](size_t b, size_t e) {
        for (size_t k = b; k < e; ++k) solve(order[first + k], false);
    }, 1);
    if (cancelled) {
        error = "Unwrap was cancelled";
        return false;
    }
    // 4. Into the mesh's UVs, then packed or fitted in place.
    result.uvs = hadUvs ? m.uvs : std::vector<glm::vec2>(nc, glm::vec2(0.0f));
    for (uint32_t t = 0; t < nt; ++t) {
        if (pc.piece[t] == kNoCorner) continue;
        for (uint32_t j = 0; j < 3; ++j) result.uvs[t * 3 + j] = vuv[pc.vertex[t * 3 + j]];
    }
    result.islands = pc.count;
    const bool inPlace = hadUvs && (o.method == UvUnwrapMethod::Unfold || !o.pack);
    if (inPlace) {
        // Each piece fitted (turned, scaled, moved) onto where its old UVs were.
        poolFor(pc.count, [&](size_t b, size_t e) {
            for (size_t i = b; i < e; ++i) {
                if (pinnedPiece[i]) continue;
                std::vector<glm::vec2> from, to;
                for (uint32_t k = offsets[i]; k < offsets[i + 1]; ++k)
                    for (uint32_t j = 0; j < 3; ++j) {
                        const uint32_t c = tris[k] * 3 + j;
                        from.push_back(result.uvs[c]);
                        to.push_back(m.uvs[c]);
                    }
                float s = 1, a = 0;
                glm::vec2 cf(0.0f), ct(0.0f);
                const bool pinned = pinCorner[i] != kNoCorner;
                if (pinned) cf = result.uvs[pinCorner[i]], ct = m.uvs[pinCorner[i]];
                fitSimilarity(from, to, s, a, cf, ct, pinned);
                if (!(s > 0.0f) || !std::isfinite(s)) s = 1.0f;
                const float ca = std::cos(a), sa = std::sin(a);
                for (uint32_t k = offsets[i]; k < offsets[i + 1]; ++k)
                    for (uint32_t j = 0; j < 3; ++j) {
                        glm::vec2& u = result.uvs[tris[k] * 3 + j];
                        u = ct + rotate(u - cf, ca, sa) * s;
                    }
            }
        }, 8);
        result.packing.islands = pc.count;
        result.packing.tilesUsed = 1;
    } else {
        Mesh tmp;
        tmp.positions = m.positions;
        tmp.triangles = m.triangles;
        tmp.uvs = result.uvs;
        UvTopology pt;
        pt.island.assign(nt, 0);
        // Islands for the packer: the pieces (and, when only some faces were unwrapped, one per other triangle,
        // which isn't packed).
        pt.islandCount = pc.count;
        for (uint32_t t = 0; t < nt; ++t) pt.island[t] = pc.piece[t] == kNoCorner ? pc.count : pc.piece[t];
        pt.islandCount = pc.count + (faces ? 1 : 0);
        pt.uvVertex = pc.vertex; // (the corners each flattened vertex joins)
        pt.uvVertexCount = pc.vertexCount;
        pt.islandOffsets.assign(pt.islandCount + 1, 0);
        for (uint32_t t = 0; t < nt; ++t) ++pt.islandOffsets[pt.island[t] + 1];
        for (uint32_t i = 0; i < pt.islandCount; ++i) pt.islandOffsets[i + 1] += pt.islandOffsets[i];
        std::vector<uint32_t> at(pt.islandOffsets.begin(), pt.islandOffsets.end() - 1);
        pt.islandTriangles.resize(nt);
        for (uint32_t t = 0; t < nt; ++t) pt.islandTriangles[at[pt.island[t]]++] = t;
        std::vector<uint8_t> isl(pt.islandCount, 1);
        for (uint32_t i = 0; i < pc.count; ++i) isl[i] = !pinnedPiece[i];
        if (faces) isl[pc.count] = 0;
        UvPackOptions po = o.packing;
        po.resolution = o.resolution;
        if (faces && hadUvs) po.tiles = 1, po.density = 0.0f;
        if (std::find(isl.begin(), isl.end(), uint8_t(1)) != isl.end() && !uvPack(tmp, pt, isl, po, result.packing, error))
            return false;
        if (faces && hadUvs) { // the selection's new islands go where its old ones were
            glm::vec2 lo(1e30f), hi(-1e30f);
            for (uint32_t t = 0; t < nt; ++t)
                if ((*faces)[t])
                    for (uint32_t j = 0; j < 3; ++j) lo = glm::min(lo, m.uvs[t * 3 + j]), hi = glm::max(hi, m.uvs[t * 3 + j]);
            const float side = std::max(hi.x - lo.x, hi.y - lo.y);
            if (side > 0.0f)
                for (uint32_t t = 0; t < nt; ++t)
                    if ((*faces)[t] && !pinnedPiece[pc.piece[t]])
                        for (uint32_t j = 0; j < 3; ++j) {
                            glm::vec2& u = tmp.uvs[t * 3 + j];
                            u = lo + u * side;
                        }
        }
        result.uvs = std::move(tmp.uvs);
    }
    report(1.0f);
    return true;
}

} // namespace sf
