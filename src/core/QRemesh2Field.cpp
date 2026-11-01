// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// QRemesh 2: curvature and the cross field on the proxy surface.
//
// Curvature: the per-edge dihedral angle tensor of Cohen-Steiner & Morvan ("Restricted Delaunay triangulations and
// normal cycle", 2003), summed per triangle and smoothed over about one quad, so a dense sculpt's pores and
// scratches don't steer the edge flow but its brow, lips and limbs do.
//
// Cross field: a 4-RoSy field per triangle as u = e^{4 i theta} (Ray, Vallet, Li & Levy, "N-symmetry direction
// field design", 2008; Knoppel, Crane, Pinkall & Schroder, "Globally optimal direction fields", 2013): the
// connection Laplacian (smoothness) plus a pull towards the principal curvature directions weighted by how
// anisotropic the bending is, with feature edges (open edges, borders, hard edges, guides) as fixed directions.
// One sparse Hermitian system, solved by preconditioned conjugate gradients with deterministic reductions.
#include "core/QRemesh2.h"

#include "core/QRemesh2Solver.h"
#include "core/ThreadPool.h"

#include <cstdio>
#include <cstdlib>

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace sf::qr2 {

namespace {

constexpr double kPi = std::numbers::pi;
using cd = std::complex<double>;

float angleIn(const Frames& f, uint32_t t, const vec3& v) { return std::atan2(glm::dot(v, f.e2[t]), glm::dot(v, f.e1[t])); }

double wrapHalfPi(double a) { // to (-pi/4, pi/4]
    const double q = kPi / 2.0;
    return a - q * std::round(a / q);
}

// Deterministic block reductions.
template <class F>
double blockSum(size_t n, F&& term) {
    const size_t block = 8192, blocks = (n + block - 1) / block;
    std::vector<double> part(blocks, 0.0);
    poolFor(n, [&](size_t b, size_t e) {
        double s = 0.0;
        for (size_t i = b; i < e; ++i) s += term(i);
        part[b / block] = s;
    }, block);
    double s = 0.0;
    for (double p : part) s += p;
    return s;
}

} // namespace

void Frames::build(const Proxy& p) {
    const size_t nt = p.T.size();
    e1.resize(nt), e2.resize(nt), n.resize(nt), area.resize(nt);
    for (size_t t = 0; t < nt; ++t) {
        const glm::uvec3 tri = p.T[t];
        const vec3 a = p.P[tri.y] - p.P[tri.x], b = p.P[tri.z] - p.P[tri.x];
        const vec3 c = glm::cross(a, b);
        area[t] = 0.5f * glm::length(c);
        n[t] = safeNormalize(c);
        e1[t] = safeNormalize(a - n[t] * glm::dot(n[t], a), vec3(1, 0, 0));
        e2[t] = glm::cross(n[t], e1[t]);
    }
}

Curvature computeCurvature(const Proxy& p, const Frames& f, float radius) {
    const size_t nt = p.T.size();
    // Per triangle 3x3 symmetric tensor (xx, xy, xz, yy, yz, zz), area-weighted sums.
    std::vector<std::array<float, 6>> tensor(nt, std::array<float, 6>{});
    double edgeSum = 0.0;
    size_t edgeCount = 0;
    for (uint32_t t = 0; t < nt; ++t)
        for (int k = 0; k < 3; ++k) {
            const uint32_t c = 3 * t + uint32_t(k), o = p.opp[c];
            const vec3 e = p.P[p.T[t][(k + 1) % 3]] - p.P[p.T[t][k]];
            const float len = glm::length(e);
            edgeSum += len, ++edgeCount;
            if (o == kNone || o < c || p.feature[c]) continue; // a feature edge is a crease, not curvature
            const uint32_t g = o / 3;
            const vec3 ed = e / std::max(len, 1e-30f);
            // Signed dihedral angle: positive where the surface bends away from its normal (convex).
            const float beta = std::atan2(glm::dot(glm::cross(f.n[t], f.n[g]), ed), glm::dot(f.n[t], f.n[g]));
            const float w = 0.5f * beta * len;
            const std::array<float, 6> m{w * ed.x * ed.x, w * ed.x * ed.y, w * ed.x * ed.z,
                                         w * ed.y * ed.y, w * ed.y * ed.z, w * ed.z * ed.z};
            for (int i = 0; i < 6; ++i) tensor[t][i] += m[i], tensor[g][i] += m[i];
        }
    const float meanEdge = edgeCount ? float(edgeSum / double(edgeCount)) : 1.0f;
    // Smoothing: area-weighted averages over edge neighbours; diffusion reaches about meanEdge * sqrt(rounds).
    const int rounds = std::clamp(int(std::lround(double(radius / meanEdge) * double(radius / meanEdge))), 1, 60);
    std::vector<float> area(f.area);
    std::vector<std::array<float, 6>> next(nt);
    // tensor holds sums (curvature x area); smooth the sums and the areas together.
    for (int r = 0; r < rounds; ++r) {
        std::vector<float> nextArea(nt);
        poolFor(nt, [&](size_t b, size_t e) {
            for (size_t t = b; t < e; ++t) {
                std::array<float, 6> s = tensor[t];
                float a = area[t];
                for (int k = 0; k < 3; ++k) {
                    const uint32_t o = p.opp[3 * t + size_t(k)];
                    if (o == kNone || p.feature[3 * t + size_t(k)]) continue; // don't blur across feature lines
                    const uint32_t g = o / 3;
                    for (int i = 0; i < 6; ++i) s[i] += tensor[g][i];
                    a += area[g];
                }
                const float w = 1.0f / float(1 + 3);
                for (int i = 0; i < 6; ++i) next[t][i] = s[i] * w;
                nextArea[t] = a * w;
            }
        });
        tensor.swap(next);
        area.swap(nextArea);
    }
    Curvature c;
    c.angle.resize(nt), c.anisotropy.resize(nt), c.magnitude.resize(nt);
    for (size_t t = 0; t < nt; ++t) {
        const auto& m = tensor[t];
        const float inv = 1.0f / std::max(area[t], 1e-30f);
        auto apply = [&](const vec3& v) {
            return vec3(m[0] * v.x + m[1] * v.y + m[2] * v.z, m[1] * v.x + m[3] * v.y + m[4] * v.z,
                        m[2] * v.x + m[4] * v.y + m[5] * v.z) * inv;
        };
        const vec3 a1 = apply(f.e1[t]), a2 = apply(f.e2[t]);
        const float xx = glm::dot(f.e1[t], a1), xy = glm::dot(f.e2[t], a1), yy = glm::dot(f.e2[t], a2);
        c.angle[t] = 0.5f * std::atan2(2.0f * xy, xx - yy);
        c.anisotropy[t] = std::sqrt((xx - yy) * (xx - yy) + 4.0f * xy * xy);
        c.magnitude[t] = 0.5f * std::abs(xx + yy) + 0.5f * c.anisotropy[t];
    }
    return c;
}

bool solveCrossField(const Proxy& p, const Frames& f, const Curvature& curv, const FieldSettings& s, CrossField& out,
                     const std::function<bool()>& cancelled) {
    const size_t nt = p.T.size();
    // Fixed triangles: those with a feature edge, along the longest one.
    std::vector<uint8_t> fixed(nt, 0);
    std::vector<cd> z(nt, cd(0.0, 0.0)), target(nt, cd(0.0, 0.0));
    std::vector<double> weight(nt, 0.0);
    for (uint32_t t = 0; t < nt; ++t) {
        float best = -1.0f;
        for (int k = 0; k < 3; ++k) {
            if (!p.feature[3 * t + uint32_t(k)]) continue;
            const vec3 e = p.P[p.T[t][(k + 1) % 3]] - p.P[p.T[t][k]];
            const float l = glm::length(e);
            if (l > best) {
                best = l;
                const double a = angleIn(f, t, e);
                z[t] = std::polar(1.0, 4.0 * a);
            }
        }
        if (best > 0.0f && t < p.fixedDir.size() && glm::dot(p.fixedDir[t], p.fixedDir[t]) > 0.0f)
            z[t] = std::polar(1.0, 4.0 * double(angleIn(f, t, p.fixedDir[t]))); // a guide: its smooth tangent
        if (best > 0.0f) {
            fixed[t] = 1;
            continue;
        }
        // Curvature: anisotropy times the quad size is the bend across one quad (radians); full weight from ~17 degrees.
        const glm::uvec3 tri = p.T[t];
        const double h = (double(p.size[tri.x]) + double(p.size[tri.y]) + double(p.size[tri.z])) / 3.0;
        const double bend = std::min(1.0, double(curv.anisotropy[t]) * h / 0.3);
        weight[t] = double(s.alignWeight) * bend * bend;
        target[t] = std::polar(1.0, 4.0 * double(curv.angle[t]));
        if (t < s.guideAngle.size() && !std::isnan(s.guideAngle[t]) && s.guideWeight[t] > 0.0f) {
            // A soft guide replaces the curvature pull near the curve.
            const double gw = double(s.guideWeight[t]);
            target[t] = (1.0 - std::min(gw, 1.0)) * target[t] * weight[t] + gw * 10.0 * std::polar(1.0, 4.0 * double(s.guideAngle[t]));
            weight[t] = std::abs(target[t]);
            target[t] = weight[t] > 0.0 ? target[t] / weight[t] : cd(1.0, 0.0);
        }
    }
    // Transport factors per corner: from the twin triangle's frame into this one's.
    std::vector<cd> transport(3 * nt, cd(0.0, 0.0));
    for (uint32_t t = 0; t < nt; ++t)
        for (int k = 0; k < 3; ++k) {
            const uint32_t c = 3 * t + uint32_t(k), o = p.opp[c];
            if (o == kNone) continue;
            const uint32_t g = o / 3;
            const vec3 e = p.P[p.T[t][(k + 1) % 3]] - p.P[p.T[t][k]];
            const double at = angleIn(f, t, e), ag = angleIn(f, g, e);
            // Wanted quad size h: the field lives in the metric scaled by 1/h, whose connection turns by
            // (log h_b - log h_a) times the dual / primal edge length ratio (cotangent weight) crossing into t.
            // Without it, (1/h) X can't be a gradient and the map smears the density out.
            double size = 0.0;
            if (!p.fieldSize.empty()) {
                const uint32_t a = p.T[t][k], b = p.T[t][(k + 1) % 3];
                auto cotAt = [&](uint32_t tri, uint32_t opposite) {
                    const glm::uvec3 q = p.T[tri];
                    int j = 0;
                    while (j < 3 && q[j] != opposite) ++j;
                    const vec3 u = p.P[q[(j + 1) % 3]] - p.P[q[j]], v = p.P[q[(j + 2) % 3]] - p.P[q[j]];
                    return double(glm::dot(u, v)) / std::max(1e-20, double(glm::length(glm::cross(u, v))));
                };
                const uint32_t ct = p.T[t][(k + 2) % 3];
                uint32_t cg = p.T[g][0];
                for (int j = 0; j < 3; ++j)
                    if (p.T[g][j] != a && p.T[g][j] != b) cg = p.T[g][j];
                const double w = std::clamp(0.5 * (cotAt(t, ct) + cotAt(g, cg)), 0.0, 5.0);
                static const double gain = std::getenv("SF_QR2_SIZEGAIN") ? std::atof(std::getenv("SF_QR2_SIZEGAIN")) : 2.0;
                size = gain * w * (std::log(double(p.fieldSize[b])) - std::log(double(p.fieldSize[a])));
            }
            transport[c] = std::polar(1.0, 4.0 * (at - ag + size));
        }
    // System: for free t, (deg + w) z_t - sum r z_g = w target (+ fixed neighbours).
    std::vector<double> diag(nt, 1.0);
    std::vector<cd> rhs(nt, cd(0.0, 0.0));
    for (uint32_t t = 0; t < nt; ++t) {
        if (fixed[t]) continue;
        double d = weight[t];
        cd b = weight[t] * target[t];
        for (int k = 0; k < 3; ++k) {
            const uint32_t c = 3 * t + uint32_t(k), o = p.opp[c];
            if (o == kNone) continue;
            d += 1.0;
            if (fixed[o / 3]) b += transport[c] * z[o / 3];
        }
        diag[t] = std::max(d, 1e-12);
        rhs[t] = b;
    }
    // The system over the free triangles, preconditioned by multigrid.
    std::vector<uint32_t> fid(nt, kNone), freeTris;
    for (uint32_t t = 0; t < nt; ++t)
        if (!fixed[t]) fid[t] = uint32_t(freeTris.size()), freeTris.push_back(t);
    const size_t nfree = freeTris.size();
    std::vector<cd> x(nfree), b(nfree);
    if (nfree > 0) {
        SparseMatrix<cd> A;
        std::vector<std::pair<uint64_t, cd>> trips;
        trips.reserve(nfree * 4);
        for (uint32_t i = 0; i < nfree; ++i) {
            const uint32_t t = freeTris[i];
            trips.push_back({(uint64_t(i) << 32) | i, cd(diag[t], 0.0)});
            for (int k = 0; k < 3; ++k) {
                const uint32_t c = 3 * t + uint32_t(k), o = p.opp[c];
                if (o == kNone || fixed[o / 3]) continue;
                trips.push_back({(uint64_t(i) << 32) | fid[o / 3], -transport[c]});
            }
            b[i] = rhs[t];
            x[i] = weight[t] > 0.0 ? target[t] : cd(0.0, 0.0);
        }
        // A tiny shift keeps a closed surface's system definite.
        double meanDiag = 0.0;
        for (uint32_t i = 0; i < nfree; ++i) meanDiag += diag[freeTris[i]];
        meanDiag /= double(nfree);
        for (uint32_t i = 0; i < nfree; ++i) trips.push_back({(uint64_t(i) << 32) | i, cd(1e-4 * meanDiag, 0.0)});
        A.build(nfree, trips);
        Multigrid<cd> mg;
        mg.build(A);
        bool stop = false;
        // Inverse power iteration (the smoothest field, Knoppel et al. 2013) with the alignment and the fixed
        // triangles as a steady pull: x <- A^-1 (sigma x / |x| + b). Where nothing pulls, the field is the smoothest
        // one rather than noise.
        int its = 0;
        if (x.empty()) x.assign(nfree, cd(0.0, 0.0));
        {
            double bn = 0.0;
            for (const cd& v : b) bn += std::norm(v);
            bn = std::sqrt(bn / double(nfree));
            // Start: the pull, or a constant phase where there is none.
            for (uint32_t i = 0; i < nfree; ++i)
                if (std::abs(x[i]) == 0.0) x[i] = cd(1.0, 0.0);
            // With fixed directions (feature lines) the pull towards them must dominate: only a light smoothest-field
            // term. Without any, the smoothest field is the answer and the scale only sets the step.
            static const double tune_sigma = std::getenv("SF_QR2_SIGMA") ? std::atof(std::getenv("SF_QR2_SIGMA")) : 2e-3;
            const double sigma = nfree < nt ? tune_sigma * meanDiag : std::max(bn, 1e-3 * meanDiag);
            std::vector<cd> rhsk(nfree);
            static const int projected = std::getenv("SF_QR2_PROJ") ? std::atoi(std::getenv("SF_QR2_PROJ")) : 30;
            std::vector<double> local; // per free triangle: the projected rounds' weight (empty: not run)
            int lastRound = 6 + projected;
            for (int round = 0; round < lastRound; ++round) {
                if (round == 6) {
                    // Zeros of order two or more (four poles merged by symmetry, as inside a closed guide loop) are
                    // split by projected rounds around them, strongly there and lightly elsewhere; none: done.
                    CrossField probe;
                    probe.theta.resize(nt);
                    for (size_t t = 0; t < nt; ++t) {
                        const cd v = fixed[t] ? z[t] : x[fid[t]];
                        probe.theta[t] = std::abs(v) > 1e-30 ? float(std::arg(v) / 4.0) : 0.0f;
                    }
                    computeMatching(p, f, probe);
                    std::vector<int> ring(nt, -1);
                    std::vector<uint32_t> queue;
                    for (uint32_t t = 0; t < nt; ++t)
                        for (int k = 0; k < 3; ++k)
                            if (std::abs(probe.vertexIndex[p.T[t][k]]) >= 2 && ring[t] < 0) ring[t] = 0, queue.push_back(t);
                    if (queue.empty()) { // none: a few light projected rounds everywhere (they smooth the field)
                        local.assign(nfree, sigma * 4.0);
                        lastRound = 6 + 8;
                        continue;
                    }
                    for (size_t qi = 0; qi < queue.size(); ++qi) {
                        const uint32_t t = queue[qi];
                        if (ring[t] >= 30) continue;
                        for (int k = 0; k < 3; ++k) {
                            const uint32_t o = p.opp[3 * t + uint32_t(k)];
                            if (o == kNone || ring[o / 3] >= 0) continue;
                            ring[o / 3] = ring[t] + 1;
                            queue.push_back(o / 3);
                        }
                    }
                    const double strong = std::max(bn, 1e-3 * meanDiag) * 4.0;
                    local.assign(nfree, sigma);
                    for (uint32_t i = 0; i < nfree; ++i)
                        if (ring[freeTris[i]] >= 0) local[i] = strong;
                }
                double xn = 0.0;
                for (const cd& v : x) xn += std::norm(v);
                xn = std::sqrt(xn / double(nfree)) + 1e-300;
                // Later rounds normalise per triangle (a projected heat flow, like Ginzburg-Landau): a zero of order
                // two or more (a closed guide's centre, by symmetry) splits into simple ones that move apart.
                if (round < 6)
                    for (uint32_t i = 0; i < nfree; ++i) rhsk[i] = sigma * x[i] / xn + b[i];
                else
                    for (uint32_t i = 0; i < nfree; ++i) {
                        const double m = std::abs(x[i]);
                        rhsk[i] = local[i] * (m > 1e-12 ? x[i] / m : cd(0.0, 0.0)) + b[i];
                    }
                std::vector<cd> nx(x);
                its += pcg<cd>([&](const std::vector<cd>& in, std::vector<cd>& o) { A.mul(in, o); }, &mg, A.diag, rhsk, nx,
                               round < 3 ? 1e-3 : 1e-5, 200, cancelled, stop);
                if (stop) return false;
                x.swap(nx);
            }
        }
        if (std::getenv("SF_QREMESH_TIMING"))
            std::fprintf(stderr, "  cross field: %zu free triangles, %zu levels, %d iterations\n", nfree, mg.levels(), its);
    }
    out.theta.resize(nt);
    for (size_t t = 0; t < nt; ++t) {
        const cd v = fixed[t] ? z[t] : x[fid[t]];
        out.theta[t] = std::abs(v) > 1e-30 ? float(std::arg(v) / 4.0) : 0.0f;
    }
    // A few rounds of local smoothing of the normalised field (the linear solve shrinks |u| near singularities and
    // under-smooths there).
    for (int round = 0; round < 4; ++round) {
        std::vector<float> nextTheta(out.theta);
        poolFor(nt, [&](size_t b, size_t e) {
            for (size_t t = b; t < e; ++t) {
                if (fixed[t]) continue;
                cd s = std::polar(1.0, 4.0 * double(out.theta[t])) * (1.0 + weight[t]);
                s += weight[t] * target[t];
                for (int k = 0; k < 3; ++k) {
                    const size_t c = 3 * t + size_t(k);
                    const uint32_t o = p.opp[c];
                    if (o == kNone) continue;
                    s += transport[c] * std::polar(1.0, 4.0 * double(out.theta[o / 3]));
                }
                if (std::abs(s) > 1e-30) nextTheta[t] = float(std::arg(s) / 4.0);
            }
        });
        out.theta.swap(nextTheta);
    }
    computeMatching(p, f, out);
    return true;
}

void computeMatching(const Proxy& p, const Frames& f, CrossField& field) {
    const size_t nt = p.T.size(), nv = p.P.size();
    field.match.assign(3 * nt, 0);
    std::vector<float> eps(3 * nt, 0.0f); // field rotation across the edge, beyond the matching
    for (uint32_t t = 0; t < nt; ++t)
        for (int k = 0; k < 3; ++k) {
            const uint32_t c = 3 * t + uint32_t(k), o = p.opp[c];
            if (o == kNone) continue;
            const uint32_t g = o / 3;
            const vec3 e = p.P[p.T[t][(k + 1) % 3]] - p.P[p.T[t][k]];
            const double moved = double(field.theta[t]) - angleIn(f, t, e) + angleIn(f, g, e);
            const double d = double(field.theta[g]) - moved;
            const long q = std::lround(d / (kPi / 2.0));
            field.match[c] = int8_t(((q % 4) + 4) % 4);
            eps[c] = float(d - double(q) * kPi / 2.0);
        }
    // Singularity index: (sum of eps going round + angle defect) / (pi/2), interior vertices only.
    field.vertexIndex.assign(nv, 0);
    std::vector<double> angleSum(nv, 0.0), epsSum(nv, 0.0);
    std::vector<uint8_t> open(nv, 0);
    for (uint32_t t = 0; t < nt; ++t)
        for (int k = 0; k < 3; ++k) {
            const uint32_t v = p.T[t][k];
            const vec3 a = p.P[p.T[t][(k + 1) % 3]] - p.P[v], b = p.P[p.T[t][(k + 2) % 3]] - p.P[v];
            angleSum[v] += std::atan2(double(glm::length(glm::cross(a, b))), double(glm::dot(a, b)));
            // Going round v counter-clockwise, triangle t hands over to the twin across the edge (T[k+2] -> v).
            const uint32_t c = 3 * t + uint32_t((k + 2) % 3);
            if (p.opp[c] == kNone) open[v] = 1;
            else epsSum[v] += double(eps[c]);
            if (p.opp[3 * t + uint32_t(k)] == kNone) open[v] = 1;
        }
    field.singularities = 0;
    for (uint32_t v = 0; v < nv; ++v) {
        if (open[v]) continue;
        const double defect = 2.0 * kPi - angleSum[v];
        const long q = std::lround((epsSum[v] + defect) / (kPi / 2.0));
        field.vertexIndex[v] = int8_t(std::clamp<long>(q, -4, 4));
        if (q != 0) ++field.singularities;
    }
    (void)wrapHalfPi;
}

} // namespace sf::qr2
