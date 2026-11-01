// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// QRemesh 2's sparse solver: conjugate gradients preconditioned by an aggregation multigrid V-cycle, for the
// symmetric (real) or Hermitian (complex) positive definite systems of the cross field and the integer-grid map.
// Plain Jacobi-CG needs thousands of iterations on a 100k-triangle surface (smooth fields are the slowest modes);
// with the V-cycle a few dozen do. Aggregates are a vertex and its free neighbours; the prolongation carries the
// phase of the connection (so it also works for the cross field's connection Laplacian); coarse matrices are
// Galerkin products; the smoother is symmetric Gauss-Seidel. Everything is sequential or in fixed blocks, so the
// results don't depend on the thread count.
#pragma once

#include "core/ThreadPool.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <functional>
#include <unordered_map>
#include <vector>

namespace sf::qr2 {

template <class S>
inline double absq(const S& v) {
    if constexpr (std::is_same_v<S, double>) return v * v;
    else return std::norm(v);
}
template <class S>
inline S conjOf(const S& v) {
    if constexpr (std::is_same_v<S, double>) return v;
    else return std::conj(v);
}
template <class S>
inline double realOf(const S& v) {
    if constexpr (std::is_same_v<S, double>) return v;
    else return v.real();
}

template <class S>
struct SparseMatrix {
    size_t n = 0;
    std::vector<uint32_t> off, col; // CSR, columns sorted, diagonal included
    std::vector<S> val;
    std::vector<double> diag;       // real diagonal

    // From (row, col, value) triplets (duplicates summed).
    void build(size_t size, std::vector<std::pair<uint64_t, S>>& trips) {
        n = size;
        std::sort(trips.begin(), trips.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
        off.assign(n + 1, 0);
        col.clear();
        val.clear();
        for (size_t i = 0; i < trips.size();) {
            size_t j = i;
            S v{};
            while (j < trips.size() && trips[j].first == trips[i].first) v += trips[j++].second;
            const uint32_t r = uint32_t(trips[i].first >> 32), c = uint32_t(trips[i].first & 0xffffffffu);
            col.push_back(c);
            val.push_back(v);
            ++off[r + 1];
            i = j;
        }
        for (size_t i = 0; i < n; ++i) off[i + 1] += off[i];
        diag.assign(n, 0.0);
        for (size_t i = 0; i < n; ++i)
            for (uint32_t k = off[i]; k < off[i + 1]; ++k)
                if (col[k] == i) diag[i] = realOf(val[k]);
        for (double& d : diag)
            if (!(d > 0.0)) d = 1.0;
    }
    void mul(const std::vector<S>& x, std::vector<S>& y) const {
        poolFor(n, [&](size_t b, size_t e) {
            for (size_t i = b; i < e; ++i) {
                S s{};
                for (uint32_t k = off[i]; k < off[i + 1]; ++k) s += val[k] * x[col[k]];
                y[i] = s;
            }
        });
    }
};

template <class S>
class Multigrid {
public:
    void build(const SparseMatrix<S>& A) {
        levels_.clear();
        levels_.push_back({A, {}, {}});
        while (levels_.back().A.n > 400 && levels_.size() < 30) {
            Level& f = levels_.back();
            Level c;
            if (!coarsen(f, c)) break;
            if (c.A.n * 10 > f.A.n * 9) break;
            levels_.push_back(std::move(c));
        }
        dense_.clear();
        if (levels_.back().A.n <= 2500) factorCoarsest();
    }
    // z = M^{-1} r (one V-cycle from zero).
    void apply(const std::vector<S>& r, std::vector<S>& z) const {
        if (levels_.empty()) {
            z = r;
            return;
        }
        vcycle(0, r, z);
    }
    size_t levels() const { return levels_.size(); }

private:
    struct Level {
        SparseMatrix<S> A;
        std::vector<uint32_t> agg; // fine unknown -> coarse unknown (this level -> next)
        std::vector<S> phase;      // tentative prolongation coefficient of each fine unknown
        // Smoothed prolongation P = (I - w D^-1 A) P0, rows per fine unknown.
        std::vector<uint32_t> pOff, pCol;
        std::vector<S> pVal;
    };
    std::vector<Level> levels_;

    // Aggregates: an unaggregated unknown takes its unaggregated neighbours; leftovers join the neighbour they're
    // most strongly coupled to.
    bool coarsen(Level& f, Level& c) {
        const SparseMatrix<S>& A = f.A;
        const size_t n = A.n;
        f.agg.assign(n, ~0u);
        f.phase.assign(n, S(1.0));
        uint32_t nc = 0;
        auto strong = [&](size_t i, uint32_t k) {
            return absq(A.val[k]) >= 0.0625 * A.diag[i] * A.diag[A.col[k]] / 9.0;
        };
        for (size_t i = 0; i < n; ++i) {
            if (f.agg[i] != ~0u) continue;
            bool free = true;
            for (uint32_t k = A.off[i]; k < A.off[i + 1] && free; ++k)
                if (A.col[k] != i && strong(i, k) && f.agg[A.col[k]] != ~0u) free = false;
            if (!free) continue;
            f.agg[i] = nc;
            f.phase[i] = S(1.0);
            for (uint32_t k = A.off[i]; k < A.off[i + 1]; ++k) {
                const uint32_t j = A.col[k];
                if (j == i || !strong(i, k)) continue;
                f.agg[j] = nc;
                // Value at j for a value at i: -A_ji / |A_ji| (A_ji = conj(A_ij)).
                const S aji = conjOf(A.val[k]);
                const double m = std::sqrt(absq(aji));
                f.phase[j] = m > 0.0 ? -aji / m : S(1.0);
            }
            ++nc;
        }
        for (int pass = 0; pass < 4; ++pass)
            for (size_t i = 0; i < n; ++i) {
                if (f.agg[i] != ~0u) continue;
                double best = -1.0;
                uint32_t bk = ~0u;
                for (uint32_t k = A.off[i]; k < A.off[i + 1]; ++k) {
                    const uint32_t j = A.col[k];
                    if (j == i || f.agg[j] == ~0u) continue;
                    if (absq(A.val[k]) > best) best = absq(A.val[k]), bk = k;
                }
                if (bk == ~0u) continue;
                const uint32_t j = A.col[bk];
                f.agg[i] = f.agg[j];
                const S aij = A.val[bk]; // couples i to j: value at i = -conj(A_ij)... phase via j
                const S aji = conjOf(aij);
                (void)aji;
                const double m = std::sqrt(absq(aij));
                f.phase[i] = m > 0.0 ? (-aij / m) * f.phase[j] : f.phase[j];
            }
        for (size_t i = 0; i < n; ++i)
            if (f.agg[i] == ~0u) f.agg[i] = nc++, f.phase[i] = S(1.0);
        if (nc == 0 || nc >= n) return false;
        // Smoothed aggregation: P = (I - w D^-1 A) P0 (w = 2/3), so coarse corrections are smooth and the V-cycle
        // converges in a few dozen iterations instead of hundreds.
        const double w = 2.0 / 3.0;
        f.pOff.assign(n + 1, 0);
        f.pCol.clear();
        f.pVal.clear();
        {
            std::vector<std::pair<uint32_t, S>> row;
            for (size_t i = 0; i < n; ++i) {
                row.clear();
                row.push_back({f.agg[i], f.phase[i]});
                for (uint32_t k = A.off[i]; k < A.off[i + 1]; ++k) {
                    const uint32_t j = A.col[k];
                    row.push_back({f.agg[j], -(w / A.diag[i]) * A.val[k] * f.phase[j]});
                }
                std::sort(row.begin(), row.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
                for (size_t k = 0; k < row.size(); ++k) {
                    if (!f.pCol.empty() && f.pOff[i + 1] > 0 && f.pCol.back() == row[k].first) f.pVal.back() += row[k].second;
                    else f.pCol.push_back(row[k].first), f.pVal.push_back(row[k].second), ++f.pOff[i + 1];
                }
            }
            for (size_t i = 0; i < n; ++i) f.pOff[i + 1] += f.pOff[i];
        }
        // Galerkin: Ac = P^H A P, via AP (rows fine, columns coarse).
        std::vector<std::pair<uint64_t, S>> trips;
        {
            std::vector<std::pair<uint32_t, S>> ap;
            std::unordered_map<uint32_t, S> acc;
            for (size_t i = 0; i < n; ++i) {
                acc.clear();
                for (uint32_t k = A.off[i]; k < A.off[i + 1]; ++k) {
                    const uint32_t j = A.col[k];
                    for (uint32_t m = f.pOff[j]; m < f.pOff[j + 1]; ++m) acc[f.pCol[m]] += A.val[k] * f.pVal[m];
                }
                for (uint32_t m = f.pOff[i]; m < f.pOff[i + 1]; ++m) {
                    const uint32_t I = f.pCol[m];
                    const S pc = conjOf(f.pVal[m]);
                    for (const auto& [J, v] : acc) trips.push_back({(uint64_t(I) << 32) | J, pc * v});
                }
            }
        }
        c.A.build(nc, trips);
        return true;
    }

    // Smoothing on A x = b: damped Jacobi in parallel on big levels (two sweeps), Gauss-Seidel (forwards or
    // backwards, so the V-cycle stays symmetric) on small ones.
    static void sweep(const SparseMatrix<S>& A, const std::vector<S>& b, std::vector<S>& x, bool forwards) {
        const size_t n = A.n;
        if (n > 20000) {
            std::vector<S> r(n);
            for (int k = 0; k < 2; ++k) {
                A.mul(x, r);
                poolFor(n, [&](size_t b0, size_t e0) {
                    for (size_t i = b0; i < e0; ++i) x[i] += (0.7 / A.diag[i]) * (b[i] - r[i]);
                });
            }
            return;
        }
        for (size_t s = 0; s < n; ++s) {
            const size_t i = forwards ? s : n - 1 - s;
            S r = b[i];
            for (uint32_t k = A.off[i]; k < A.off[i + 1]; ++k)
                if (A.col[k] != i) r -= A.val[k] * x[A.col[k]];
            x[i] = r / A.diag[i];
        }
    }

    // Dense Cholesky (A = L L^H) of the coarsest level.
    void factorCoarsest() {
        const SparseMatrix<S>& A = levels_.back().A;
        const size_t n = A.n;
        dense_.assign(n * n, S{});
        for (size_t i = 0; i < n; ++i)
            for (uint32_t k = A.off[i]; k < A.off[i + 1]; ++k) dense_[i * n + A.col[k]] = A.val[k];
        for (size_t j = 0; j < n; ++j) {
            double d = realOf(dense_[j * n + j]);
            for (size_t k = 0; k < j; ++k) d -= absq(dense_[j * n + k]);
            d = std::sqrt(std::max(d, 1e-300));
            dense_[j * n + j] = S(d);
            for (size_t i = j + 1; i < n; ++i) {
                S s = dense_[i * n + j];
                for (size_t k = 0; k < j; ++k) s -= dense_[i * n + k] * conjOf(dense_[j * n + k]);
                dense_[i * n + j] = s / d;
            }
        }
    }
    void solveCoarsest(const std::vector<S>& b, std::vector<S>& x) const {
        const size_t n = b.size();
        x = b;
        for (size_t i = 0; i < n; ++i) {
            S s = x[i];
            for (size_t k = 0; k < i; ++k) s -= dense_[i * n + k] * x[k];
            x[i] = s / realOf(dense_[i * n + i]);
        }
        for (size_t i = n; i-- > 0;) {
            S s = x[i];
            for (size_t k = i + 1; k < n; ++k) s -= conjOf(dense_[k * n + i]) * x[k];
            x[i] = s / realOf(dense_[i * n + i]);
        }
    }

    // Symmetric V-cycle: forward sweep, coarse correction, backward sweep.
    void vcycle(size_t li, const std::vector<S>& b, std::vector<S>& x) const {
        const Level& L = levels_[li];
        const size_t n = L.A.n;
        x.assign(n, S{});
        if (li + 1 == levels_.size()) {
            if (!dense_.empty()) solveCoarsest(b, x);
            else
                for (int s = 0; s < 20; ++s) sweep(L.A, b, x, true), sweep(L.A, b, x, false);
            return;
        }
        sweep(L.A, b, x, true);
        std::vector<S> r(n);
        L.A.mul(x, r);
        for (size_t i = 0; i < n; ++i) r[i] = b[i] - r[i];
        const size_t nc = levels_[li + 1].A.n;
        std::vector<S> rc(nc, S{}), xc;
        for (size_t i = 0; i < n; ++i)
            for (uint32_t m = L.pOff[i]; m < L.pOff[i + 1]; ++m) rc[L.pCol[m]] += conjOf(L.pVal[m]) * r[i];
        vcycle(li + 1, rc, xc);
        for (size_t i = 0; i < n; ++i)
            for (uint32_t m = L.pOff[i]; m < L.pOff[i + 1]; ++m) x[i] += L.pVal[m] * xc[L.pCol[m]];
        sweep(L.A, b, x, false);
    }
    std::vector<S> dense_;
};

template <class S, class F>
double blockSumT(size_t n, F&& term) {
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

// Preconditioned CG on A x = b with A applied by `mul` and the multigrid of `A` (or Jacobi when mg is null).
// Returns the iterations; stop is set when `cancelled` said so.
template <class S>
int pcg(const std::function<void(const std::vector<S>&, std::vector<S>&)>& mul, const Multigrid<S>* mg,
        const std::vector<double>& diag, const std::vector<S>& b, std::vector<S>& x, double tol, int maxIt,
        const std::function<bool()>& cancelled, bool& stop) {
    const size_t n = b.size();
    std::vector<S> r(n), z(n), d(n), q(n);
    mul(x, q);
    for (size_t i = 0; i < n; ++i) r[i] = b[i] - q[i];
    auto precond = [&](const std::vector<S>& in, std::vector<S>& out) {
        if (mg) mg->apply(in, out);
        else
            for (size_t i = 0; i < n; ++i) out[i] = in[i] / diag[i];
    };
    precond(r, z);
    d = z;
    double rz = blockSumT<S>(n, [&](size_t i) { return realOf(conjOf(r[i]) * z[i]); });
    const double bn = std::sqrt(blockSumT<S>(n, [&](size_t i) { return absq(b[i]); })) + 1e-300;
    int it = 0;
    for (; it < maxIt; ++it) {
        const double rn = std::sqrt(blockSumT<S>(n, [&](size_t i) { return absq(r[i]); }));
        if (rn < tol * bn) break;
        if ((it & 7) == 0 && cancelled && cancelled()) {
            stop = true;
            return it;
        }
        mul(d, q);
        const double dq = blockSumT<S>(n, [&](size_t i) { return realOf(conjOf(d[i]) * q[i]); });
        if (!(dq > 1e-300)) break;
        const double alpha = rz / dq;
        for (size_t i = 0; i < n; ++i) x[i] += alpha * d[i], r[i] -= alpha * q[i];
        precond(r, z);
        const double rzNew = blockSumT<S>(n, [&](size_t i) { return realOf(conjOf(r[i]) * z[i]); });
        const double beta = rzNew / rz;
        rz = rzNew;
        for (size_t i = 0; i < n; ++i) d[i] = z[i] + beta * d[i];
    }
    return it;
}

} // namespace sf::qr2
