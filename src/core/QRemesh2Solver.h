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
        setDiagonal();
    }
    // diag from the CSR arrays (n, off, col, val set).
    void setDiagonal() {
        diag.assign(n, 0.0);
        for (size_t i = 0; i < n; ++i)
            for (uint32_t k = off[i]; k < off[i + 1]; ++k)
                if (col[k] == i) diag[i] = realOf(val[k]);
        for (double& d : diag)
            if (!(d > 0.0)) d = 1.0;
    }
    // Rows run in blocks of kRows: small enough to balance the threads (a 60k-row proxy is 60 blocks), large
    // enough that a block outweighs handing it out.
    static constexpr size_t kRows = 1024;
    S row(size_t i, const std::vector<S>& x) const {
        S s{};
        for (uint32_t k = off[i]; k < off[i + 1]; ++k) s += val[k] * x[col[k]];
        return s;
    }
    void mul(const std::vector<S>& x, std::vector<S>& y) const {
        poolFor(n, [&](size_t b, size_t e) {
            for (size_t i = b; i < e; ++i) y[i] = row(i, x);
        }, kRows);
    }
    // r = b - A x, in one pass.
    void residual(const std::vector<S>& b, const std::vector<S>& x, std::vector<S>& r) const {
        poolFor(n, [&](size_t b0, size_t e0) {
            for (size_t i = b0; i < e0; ++i) {
                const S s = row(i, x);
                r[i] = b[i] - s;
            }
        }, kRows);
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
        // Its transpose (rows per coarse unknown, fine unknowns ascending), so the restriction P^H r gathers in
        // parallel and adds in the same order as a scatter over the fine unknowns would.
        std::vector<uint32_t> rOff, rRow;
        std::vector<S> rVal;
        // Scratch reused by every V-cycle (one apply() at a time): the residual, the restricted residual and the
        // coarse correction, and the Jacobi sweeps' other buffer.
        mutable std::vector<S> r, rc, xc, swap;
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
            f.rOff.assign(size_t(nc) + 1, 0);
            for (uint32_t c : f.pCol) ++f.rOff[c + 1];
            for (size_t c = 0; c < nc; ++c) f.rOff[c + 1] += f.rOff[c];
            f.rRow.resize(f.pCol.size());
            f.rVal.resize(f.pCol.size());
            std::vector<uint32_t> fill(f.rOff.begin(), f.rOff.end() - 1);
            for (size_t i = 0; i < n; ++i)
                for (uint32_t m = f.pOff[i]; m < f.pOff[i + 1]; ++m) {
                    const uint32_t k = fill[f.pCol[m]]++;
                    f.rRow[k] = uint32_t(i), f.rVal[k] = conjOf(f.pVal[m]);
                }
        }
        // Galerkin: Ac = P^H A P, row by row in parallel: AP (rows fine, columns coarse), then each coarse row I
        // from the fine rows of P's column I (the transpose), ascending, so the sums don't depend on the threads.
        std::vector<uint32_t> apOff, apCol;
        std::vector<S> apVal;
        rowProduct(n, nc, apOff, apCol, apVal, [&](size_t i, auto&& add) {
            for (uint32_t k = A.off[i]; k < A.off[i + 1]; ++k) {
                const uint32_t j = A.col[k];
                for (uint32_t m = f.pOff[j]; m < f.pOff[j + 1]; ++m) add(f.pCol[m], A.val[k] * f.pVal[m]);
            }
        });
        rowProduct(nc, nc, c.A.off, c.A.col, c.A.val, [&](size_t I, auto&& add) {
            for (uint32_t m = f.rOff[I]; m < f.rOff[I + 1]; ++m) {
                const uint32_t i = f.rRow[m];
                for (uint32_t k = apOff[i]; k < apOff[i + 1]; ++k) add(apCol[k], f.rVal[m] * apVal[k]);
            }
        });
        c.A.n = nc;
        c.A.setDiagonal();
        return true;
    }

    // A sparse product, row by row in parallel: rows(r, add) calls add(column, value) for each term of row r (a
    // column may come many times: summed in call order). The result is CSR with columns sorted.
    template <class Rows>
    static void rowProduct(size_t rows, size_t cols, std::vector<uint32_t>& off, std::vector<uint32_t>& col,
                           std::vector<S>& val, Rows&& rowTerms) {
        constexpr size_t block = 512;
        const size_t blocks = (rows + block - 1) / block;
        std::vector<std::vector<uint32_t>> bCol(blocks);
        std::vector<std::vector<S>> bVal(blocks);
        std::vector<uint32_t> count(rows, 0);
        poolFor(rows, [&](size_t b0, size_t e0) {
            std::vector<uint32_t> slot(cols, ~0u), touched;
            std::vector<S> acc;
            auto& oc = bCol[b0 / block];
            auto& ov = bVal[b0 / block];
            for (size_t r = b0; r < e0; ++r) {
                touched.clear(), acc.clear();
                rowTerms(r, [&](uint32_t c, const S& v) {
                    if (slot[c] == ~0u) slot[c] = uint32_t(touched.size()), touched.push_back(c), acc.push_back(v);
                    else acc[slot[c]] += v;
                });
                const size_t start = oc.size();
                for (uint32_t c : touched) oc.push_back(c);
                std::sort(oc.begin() + long(start), oc.end());
                for (size_t k = start; k < oc.size(); ++k) ov.push_back(acc[slot[oc[k]]]);
                for (uint32_t c : touched) slot[c] = ~0u;
                count[r] = uint32_t(touched.size());
            }
        }, block, 2);
        off.assign(rows + 1, 0);
        for (size_t r = 0; r < rows; ++r) off[r + 1] = off[r] + count[r];
        col.clear(), val.clear();
        col.reserve(off[rows]), val.reserve(off[rows]);
        for (size_t b = 0; b < blocks; ++b) {
            col.insert(col.end(), bCol[b].begin(), bCol[b].end());
            val.insert(val.end(), bVal[b].begin(), bVal[b].end());
        }
    }

    // Smoothing on A x = b: damped Jacobi in parallel on big levels (two sweeps), Gauss-Seidel (forwards or
    // backwards, so the V-cycle stays symmetric) on small ones.
    static void sweep(const SparseMatrix<S>& A, const std::vector<S>& b, std::vector<S>& x, bool forwards,
                      std::vector<S>& other) {
        const size_t n = A.n;
        if (n > 20000) {
            // Jacobi reads only the old x: each sweep writes the new one into `other` in one pass, then they trade.
            other.resize(n);
            for (int k = 0; k < 2; ++k) {
                poolFor(n, [&](size_t b0, size_t e0) {
                    for (size_t i = b0; i < e0; ++i) {
                        const S r = A.row(i, x);
                        other[i] = x[i] + (0.7 / A.diag[i]) * (b[i] - r);
                    }
                }, SparseMatrix<S>::kRows);
                x.swap(other);
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
                for (int s = 0; s < 20; ++s) sweep(L.A, b, x, true, L.swap), sweep(L.A, b, x, false, L.swap);
            return;
        }
        sweep(L.A, b, x, true, L.swap);
        std::vector<S>& r = L.r;
        r.resize(n);
        L.A.residual(b, x, r);
        const size_t nc = levels_[li + 1].A.n;
        std::vector<S>& rc = L.rc;
        std::vector<S>& xc = L.xc;
        rc.resize(nc);
        poolFor(nc, [&](size_t b0, size_t e0) {
            for (size_t c = b0; c < e0; ++c) {
                S s{};
                for (uint32_t m = L.rOff[c]; m < L.rOff[c + 1]; ++m) s += L.rVal[m] * r[L.rRow[m]];
                rc[c] = s;
            }
        }, 1024);
        vcycle(li + 1, rc, xc);
        poolFor(n, [&](size_t b0, size_t e0) {
            for (size_t i = b0; i < e0; ++i)
                for (uint32_t m = L.pOff[i]; m < L.pOff[i + 1]; ++m) x[i] += L.pVal[m] * xc[L.pCol[m]];
        }, SparseMatrix<S>::kRows);
        sweep(L.A, b, x, false, L.swap);
    }
    std::vector<S> dense_;
};

constexpr size_t kSumBlock = 8192; // blockSumT's blocks (its partial sums, and so its rounding, depend on them)

template <class S, class F>
double blockSumT(size_t n, F&& term) {
    const size_t block = kSumBlock, blocks = (n + block - 1) / block;
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
    poolFor(n, [&](size_t b0, size_t e0) {
        for (size_t i = b0; i < e0; ++i) r[i] = b[i] - q[i];
    });
    auto precond = [&](const std::vector<S>& in, std::vector<S>& out) {
        if (mg) mg->apply(in, out);
        else
            for (size_t i = 0; i < n; ++i) out[i] = in[i] / diag[i];
    };
    precond(r, z);
    d = z;
    double rz = blockSumT<S>(n, [&](size_t i) { return realOf(conjOf(r[i]) * z[i]); });
    const double bn = std::sqrt(blockSumT<S>(n, [&](size_t i) { return absq(b[i]); })) + 1e-300;
    // |r|^2, from blockSumT at the start and then from the update pass itself (the same blocks, the same order).
    double rr = blockSumT<S>(n, [&](size_t i) { return absq(r[i]); });
    std::vector<double> part((n + kSumBlock - 1) / kSumBlock, 0.0);
    int it = 0;
    for (; it < maxIt; ++it) {
        const double rn = std::sqrt(rr);
        if (rn < tol * bn) break;
        if ((it & 7) == 0 && cancelled && cancelled()) {
            stop = true;
            return it;
        }
        mul(d, q);
        const double dq = blockSumT<S>(n, [&](size_t i) { return realOf(conjOf(d[i]) * q[i]); });
        if (!(dq > 1e-300)) break;
        const double alpha = rz / dq;
        poolFor(n, [&](size_t b0, size_t e0) {
            double s = 0.0;
            for (size_t i = b0; i < e0; ++i) {
                x[i] += alpha * d[i], r[i] -= alpha * q[i];
                s += absq(r[i]);
            }
            part[b0 / kSumBlock] = s;
        }, kSumBlock);
        rr = 0.0;
        for (double p : part) rr += p;
        precond(r, z);
        const double rzNew = blockSumT<S>(n, [&](size_t i) { return realOf(conjOf(r[i]) * z[i]); });
        const double beta = rzNew / rz;
        rz = rzNew;
        poolFor(n, [&](size_t b0, size_t e0) {
            for (size_t i = b0; i < e0; ++i) d[i] = z[i] + beta * d[i];
        });
    }
    return it;
}

} // namespace sf::qr2
