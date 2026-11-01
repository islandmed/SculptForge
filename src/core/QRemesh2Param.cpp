// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// QRemesh 2: the integer-grid map. A seamless parametrization whose gradients follow the cross field at the wanted
// quad size, with transitions across the cut that are grid symmetries (quarter turns plus integer shifts),
// singularities on lattice points and feature lines on lattice lines, so the integer lattice drawn on the surface is
// a quad mesh with poles only at the field's singularities. After Mixed-Integer Quadrangulation (Bommes, Zimmer &
// Kobbelt, SIGGRAPH 2009) and integer-grid maps (Bommes, Campen, Ebke, Alliez & Kobbelt, SIGGRAPH 2013); the
// constraint elimination and greedy rounding are our own small solver (no CoMISo).
//
//  1. Comb the field along a spanning tree of the triangles; the edges off the tree, pruned to the branches that
//     reach singularities, open edges or handles, are the cut.
//  2. Unknowns (u, v) per vertex wedge (corners not separated by the cut); linear constraints: across each cut edge
//     the two sides differ by the quarter turn and an integer shift, feature edges keep one coordinate on an
//     integer, singularities sit on integers. Eliminated one by one (integer unknowns stay integer combinations).
//  3. Least squares for the gradients, solved by conjugate gradients; integer unknowns rounded nearest-first in
//     batches, each followed by a warm-started re-solve.
#include "core/QRemesh2.h"

#include "core/QRemesh2Solver.h"
#include "core/ThreadPool.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <numbers>
#include <numeric>
#include <queue>

namespace sf::qr2 {

namespace {

constexpr double kPi = std::numbers::pi;

struct Term {
    uint32_t var;
    double a;
};

// Linear constraints sum a_i x_i + c = 0, eliminated as they come: each picks one unknown (the pivot) and expresses it
// by the others; expressions only ever mention unknowns still free.
class Eliminator {
public:
    // kind: 0 = real, 1..3 = integer (1 eliminated first: seam shifts, 2: feature lines, 3: singularity positions).
    uint32_t add(uint8_t kind) {
        kind_.push_back(kind);
        elim_.push_back(0);
        expr_.emplace_back();
        cst_.push_back(0.0);
        users_.emplace_back();
        return uint32_t(kind_.size() - 1);
    }
    size_t size() const { return kind_.size(); }
    bool eliminated(uint32_t v) const { return elim_[v] != 0; }
    uint8_t kind(uint32_t v) const { return kind_[v]; }
    const std::vector<Term>& expr(uint32_t v) const { return expr_[v]; }
    double constant(uint32_t v) const { return cst_[v]; }
    size_t lostIntegers() const { return lost_; }

    void constrain(const std::vector<Term>& row, double c) {
        // Substitute.
        std::vector<Term> r;
        double k = c;
        for (const Term& t : row) {
            if (std::abs(t.a) < 1e-14) continue;
            if (elim_[t.var]) {
                for (const Term& u : expr_[t.var]) r.push_back({u.var, t.a * u.a});
                k += t.a * cst_[t.var];
            } else {
                r.push_back(t);
            }
        }
        std::sort(r.begin(), r.end(), [](const Term& x, const Term& y) { return x.var < y.var; });
        std::vector<Term> m;
        for (const Term& t : r) {
            if (!m.empty() && m.back().var == t.var) m.back().a += t.a;
            else m.push_back(t);
        }
        m.erase(std::remove_if(m.begin(), m.end(), [](const Term& t) { return std::abs(t.a) < 1e-9; }), m.end());
        if (m.empty()) return; // redundant (or inconsistent, which the least squares then can't satisfy)
        // An all-integer row with integer coefficients: divide by their common factor (a parity row such as
        // 2a - 2b + 2c = 0 then keeps a pivot with coefficient 1, so integers stay integer).
        {
            bool allInt = std::abs(k - std::round(k)) < 1e-9;
            long g = 0;
            for (const Term& t : m) {
                if (kind_[t.var] == 0 || std::abs(t.a - std::round(t.a)) > 1e-9) allInt = false;
                else g = std::gcd(g, std::labs(std::lround(t.a)));
            }
            if (allInt && g > 1 && std::abs(k / double(g) - std::round(k / double(g))) < 1e-9) {
                for (Term& t : m) t.a /= double(g);
                k /= double(g);
            }
        }
        // Pivot: a real unknown with the largest coefficient; else an integer one with coefficient +-1, lowest kind.
        int best = -1;
        for (size_t i = 0; i < m.size(); ++i)
            if (kind_[m[i].var] == 0 && (best < 0 || std::abs(m[i].a) > std::abs(m[size_t(best)].a) + 1e-12)) best = int(i);
        if (best < 0)
            for (size_t i = 0; i < m.size(); ++i)
                if (std::abs(std::abs(m[i].a) - 1.0) < 1e-9 &&
                    (best < 0 || kind_[m[i].var] < kind_[m[size_t(best)].var]))
                    best = int(i);
        if (best < 0) {
            ++lost_;
            if (std::getenv("SF_QR2_LOST")) {
                std::fprintf(stderr, "    lost integer:");
                for (const Term& t : m) std::fprintf(stderr, " %g*[k%d]", t.a, kind_[t.var]);
                std::fprintf(stderr, " + %g\n", k);
            }
            for (size_t i = 0; i < m.size(); ++i)
                if (best < 0 || std::abs(m[i].a) > std::abs(m[size_t(best)].a)) best = int(i);
        }
        const uint32_t p = m[size_t(best)].var;
        const double ap = m[size_t(best)].a;
        std::vector<Term> e;
        for (const Term& t : m)
            if (t.var != p) e.push_back({t.var, -t.a / ap});
        const double ec = -k / ap;
        // Replace p in every expression using it.
        for (uint32_t q : users_[p]) {
            if (!elim_[q]) continue;
            auto& ex = expr_[q];
            double ap2 = 0.0;
            for (const Term& t : ex)
                if (t.var == p) ap2 += t.a;
            if (ap2 == 0.0) continue;
            std::vector<Term> merged;
            merged.reserve(ex.size() + e.size());
            for (const Term& t : ex)
                if (t.var != p) merged.push_back(t);
            for (const Term& t : e) merged.push_back({t.var, ap2 * t.a});
            std::sort(merged.begin(), merged.end(), [](const Term& x, const Term& y) { return x.var < y.var; });
            ex.clear();
            for (const Term& t : merged) {
                if (!ex.empty() && ex.back().var == t.var) ex.back().a += t.a;
                else ex.push_back(t);
            }
            ex.erase(std::remove_if(ex.begin(), ex.end(), [](const Term& t) { return std::abs(t.a) < 1e-12; }), ex.end());
            cst_[q] += ap2 * ec;
            for (const Term& t : e) addUser(t.var, q);
        }
        users_[p].clear();
        users_[p].shrink_to_fit();
        elim_[p] = 1;
        expr_[p] = e;
        cst_[p] = ec;
        for (const Term& t : e) addUser(t.var, p);
    }

private:
    void addUser(uint32_t var, uint32_t q) {
        auto& u = users_[var];
        if (!u.empty() && u.back() == q) return;
        u.push_back(q);
        if (u.size() >= 32 && (u.size() & (u.size() - 1)) == 0) { // at powers of two: drop duplicates and stale ones
            std::sort(u.begin(), u.end());
            u.erase(std::unique(u.begin(), u.end()), u.end());
            u.erase(std::remove_if(u.begin(), u.end(), [&](uint32_t x) {
                if (!elim_[x]) return true;
                for (const Term& t : expr_[x])
                    if (t.var == var) return false;
                return true;
            }), u.end());
        }
    }

    std::vector<uint8_t> kind_, elim_;
    std::vector<std::vector<Term>> expr_;
    std::vector<double> cst_;
    std::vector<std::vector<uint32_t>> users_;
    size_t lost_ = 0;
};

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

struct Csr {
    std::vector<uint32_t> off, col;
    std::vector<double> val;
    std::vector<double> diag;
    size_t n = 0;
};

struct ReducedOp {
    std::function<void(const std::vector<double>&, std::vector<double>&)> apply;
    const std::vector<double>* diag;
    size_t n;
};

// Conjugate gradients on the unknowns not fixed (Jacobi preconditioner); fixed entries of x stay. Returns iterations.
int conjugateGradients(const ReducedOp& A, const std::vector<double>& b, const std::vector<uint8_t>& fixed,
                       std::vector<double>& x, double tol, int maxIt, const std::function<bool()>& cancelled, bool& stop,
                       const std::function<void(const std::vector<double>&, std::vector<double>&)>& pre = {}) {
    const size_t n = A.n;
    const std::vector<double>& D = *A.diag;
    std::vector<double> r(n), z(n), d(n), q(n);
    auto precond = [&] {
        if (pre) {
            pre(r, z);
            for (size_t i = 0; i < n; ++i)
                if (fixed[i]) z[i] = 0.0;
        } else {
            for (size_t i = 0; i < n; ++i) z[i] = fixed[i] ? 0.0 : r[i] / D[i];
        }
    };
    A.apply(x, q);
    for (size_t i = 0; i < n; ++i) r[i] = fixed[i] ? 0.0 : b[i] - q[i];
    precond();
    d = z;
    double rz = blockSum(n, [&](size_t i) { return r[i] * z[i]; });
    const double bn = std::sqrt(blockSum(n, [&](size_t i) { return fixed[i] ? 0.0 : b[i] * b[i]; })) + 1e-30;
    int it = 0;
    for (; it < maxIt; ++it) {
        const double rn = std::sqrt(blockSum(n, [&](size_t i) { return r[i] * r[i]; }));
        if (rn < tol * bn) break;
        if ((it & 31) == 0 && cancelled && cancelled()) {
            stop = true;
            return it;
        }
        A.apply(d, q);
        const double dq = blockSum(n, [&](size_t i) { return fixed[i] ? 0.0 : d[i] * q[i]; });
        if (!(dq > 1e-300)) break;
        const double alpha = rz / dq;
        poolFor(n, [&](size_t b0, size_t e0) {
            for (size_t i = b0; i < e0; ++i) {
                if (fixed[i]) continue;
                x[i] += alpha * d[i];
                r[i] -= alpha * q[i];
            }
        });
        precond();
        const double rzNew = blockSum(n, [&](size_t i) { return r[i] * z[i]; });
        const double beta = rzNew / rz;
        rz = rzNew;
        poolFor(n, [&](size_t b0, size_t e0) {
            for (size_t i = b0; i < e0; ++i) d[i] = fixed[i] ? 0.0 : z[i] + beta * d[i];
        });
    }
    return it;
}
} // namespace

bool solveGridMap(const Proxy& p, const Frames& f, CrossField& field, GridMap& out, const std::function<bool()>& cancelled) {
    static const bool timing = std::getenv("SF_QREMESH_TIMING") != nullptr;
    const size_t nt = p.T.size(), nv = p.P.size();
    const size_t nc = 3 * nt;
    if (nt == 0) return false;

    // 1. Comb along a breadth-first spanning tree of the triangles.
    std::vector<uint8_t> tree(nc, 0), seen(nt, 0);
    {
        std::vector<uint32_t> queue;
        queue.reserve(nt);
        for (uint32_t root = 0; root < nt; ++root) {
            if (seen[root]) continue;
            seen[root] = 1;
            queue.clear();
            queue.push_back(root);
            for (size_t qi = 0; qi < queue.size(); ++qi) {
                const uint32_t t = queue[qi];
                for (int k = 0; k < 3; ++k) {
                    const uint32_t c = 3 * t + uint32_t(k), o = p.opp[c];
                    if (o == kNone || seen[o / 3]) continue;
                    const uint32_t g = o / 3;
                    seen[g] = 1;
                    // Turn g's cross so it matches t's across this edge.
                    field.theta[g] -= float(field.match[c]) * float(kPi / 2.0);
                    // match from t to g is now 0; refresh g's other corners lazily below.
                    tree[c] = tree[o] = 1;
                    // Update matchings of g's edges for the turn (q - m on corners of g, + m on their twins).
                    const int m = field.match[c];
                    for (int j = 0; j < 3; ++j) {
                        const uint32_t cg = 3 * g + uint32_t(j), og = p.opp[cg];
                        field.match[cg] = int8_t((field.match[cg] + m) % 4);
                        if (og != kNone) field.match[og] = int8_t((field.match[og] + 4 - m) % 4);
                    }
                    queue.push_back(g);
                }
            }
        }
    }
    // Sanity: tree edges have match 0.
    // 2. Cut graph: interior edges off the tree, pruned.
    std::vector<uint8_t> cut(nc, 0), open(nv, 0);
    for (uint32_t c = 0; c < nc; ++c) {
        if (p.opp[c] == kNone) {
            open[p.T[c / 3][c % 3]] = open[p.T[c / 3][(c % 3 + 1) % 3]] = 1;
            continue;
        }
        if (!tree[c]) cut[c] = 1;
    }
    {
        std::vector<std::vector<uint32_t>> vedges(nv); // per vertex: cut edges as their lower corner
        for (uint32_t c = 0; c < nc; ++c) {
            if (!cut[c] || p.opp[c] < c) continue;
            vedges[p.T[c / 3][c % 3]].push_back(c);
            vedges[p.T[c / 3][(c % 3 + 1) % 3]].push_back(c);
        }
        std::vector<int> deg(nv, 0);
        for (uint32_t v = 0; v < nv; ++v) deg[v] = int(vedges[v].size());
        std::vector<uint32_t> stack;
        for (uint32_t v = 0; v < nv; ++v)
            if (deg[v] == 1 && !open[v] && field.vertexIndex[v] == 0) stack.push_back(v);
        while (!stack.empty()) {
            const uint32_t v = stack.back();
            stack.pop_back();
            if (deg[v] != 1) continue;
            uint32_t e = kNone;
            for (uint32_t c : vedges[v])
                if (cut[c]) e = c;
            if (e == kNone) continue;
            cut[e] = cut[p.opp[e]] = 0;
            const uint32_t a = p.T[e / 3][e % 3], b = p.T[e / 3][(e % 3 + 1) % 3];
            --deg[a], --deg[b];
            const uint32_t other = a == v ? b : a;
            if (deg[other] == 1 && !open[other] && field.vertexIndex[other] == 0) stack.push_back(other);
        }
    }
    size_t cutEdges = 0;
    for (uint32_t c = 0; c < nc; ++c) cutEdges += cut[c] && p.opp[c] > c;

    // 3. Wedges: corners of a vertex joined across edges that are neither open nor cut.
    std::vector<uint32_t> parent(nc);
    std::iota(parent.begin(), parent.end(), 0u);
    auto find = [&](uint32_t x) {
        while (parent[x] != x) x = parent[x] = parent[parent[x]];
        return x;
    };
    auto cornerOf = [&](uint32_t t, uint32_t v) -> uint32_t {
        for (int k = 0; k < 3; ++k)
            if (p.T[t][k] == v) return 3 * t + uint32_t(k);
        return kNone;
    };
    for (uint32_t c = 0; c < nc; ++c) {
        const uint32_t o = p.opp[c];
        if (o == kNone || cut[c] || o < c) continue;
        const uint32_t t = c / 3, g = o / 3;
        const uint32_t a = p.T[t][c % 3], b = p.T[t][(c % 3 + 1) % 3];
        for (uint32_t v : {a, b}) {
            const uint32_t x = find(cornerOf(t, v)), y = find(cornerOf(g, v));
            if (x != y) parent[std::max(x, y)] = std::min(x, y);
        }
    }
    std::vector<uint32_t> copyOf(nc), copyId(nc, kNone);
    uint32_t copies = 0;
    for (uint32_t c = 0; c < nc; ++c) {
        const uint32_t r = find(c);
        if (copyId[r] == kNone) copyId[r] = copies++;
        copyOf[c] = copyId[r];
    }

    Eliminator el;
    for (uint32_t i = 0; i < 2 * copies; ++i) el.add(0);
    auto U = [&](uint32_t corner) { return 2 * copyOf[corner]; };

    // Axis of each triangle's field: X = (cos theta, sin theta) in its frame.
    auto axisOfEdge = [&](uint32_t t, const vec3& e) { // 0: the edge runs along u (so v is constant), 1: along v
        const double th = double(field.theta[t]);
        const double x = double(glm::dot(e, f.e1[t])), y = double(glm::dot(e, f.e2[t]));
        const double du = std::abs(x * std::cos(th) + y * std::sin(th)), dv = std::abs(-x * std::sin(th) + y * std::cos(th));
        return du >= dv ? 0 : 1;
    };

    // Singularities on lattice points.
    size_t ints = 0;
    {
        std::vector<uint8_t> done(nv, 0);
        for (uint32_t c = 0; c < nc; ++c) {
            const uint32_t v = p.T[c / 3][c % 3];
            if (field.vertexIndex[v] == 0 || open[v] || done[v]) continue;
            done[v] = 1;
            const uint32_t iu = el.add(3), iv = el.add(3);
            ints += 2;
            el.constrain({{U(c), 1.0}, {iu, -1.0}}, 0.0);
            el.constrain({{U(c) + 1, 1.0}, {iv, -1.0}}, 0.0);
        }
    }
    // Feature lines on lattice lines.
    for (uint32_t c = 0; c < nc; ++c) {
        if (!p.feature[c]) continue;
        const uint32_t t = c / 3, k = c % 3;
        const vec3 e = p.P[p.T[t][(k + 1) % 3]] - p.P[p.T[t][k]];
        const int along = axisOfEdge(t, e);
        const uint32_t coord = along == 0 ? 1 : 0; // the coordinate that stays constant
        const uint32_t s = el.add(2);
        ++ints;
        el.constrain({{U(c) + coord, 1.0}, {s, -1.0}}, 0.0);
        el.constrain({{U(3 * t + (k + 1) % 3) + coord, 1.0}, {s, -1.0}}, 0.0);
    }
    // Seams: across each cut edge, (u, v) on the far side = R^m (u, v) + integer shift.
    for (uint32_t c = 0; c < nc; ++c) {
        if (!cut[c] || p.opp[c] < c) continue;
        const uint32_t o = p.opp[c], t = c / 3, g = o / 3, k = c % 3;
        const int m = field.match[c];
        const uint32_t tu = el.add(1), tv = el.add(1);
        ints += 2;
        for (uint32_t v : {p.T[t][k], p.T[t][(k + 1) % 3]}) {
            const uint32_t a = U(cornerOf(t, v)), b = U(cornerOf(g, v));
            // R^m: m=1: (u, v) -> (v, -u).
            double r[2][2];
            switch (m) {
            case 0: r[0][0] = 1, r[0][1] = 0, r[1][0] = 0, r[1][1] = 1; break;
            case 1: r[0][0] = 0, r[0][1] = 1, r[1][0] = -1, r[1][1] = 0; break;
            case 2: r[0][0] = -1, r[0][1] = 0, r[1][0] = 0, r[1][1] = -1; break;
            default: r[0][0] = 0, r[0][1] = -1, r[1][0] = 1, r[1][1] = 0; break;
            }
            el.constrain({{b, 1.0}, {a, -r[0][0]}, {a + 1, -r[0][1]}, {tu, -1.0}}, 0.0);
            el.constrain({{b + 1, 1.0}, {a, -r[1][0]}, {a + 1, -r[1][1]}, {tv, -1.0}}, 0.0);
        }
    }
    if (timing)
        std::fprintf(stderr, "  grid map: %zu triangles, %u wedges, %zu cut edges, %zu integer unknowns (%zu lost)\n", nt,
                     copies, cutEdges, ints, el.lostIntegers());

    if (timing) {
        size_t maxLen = 0, total = 0;
        for (uint32_t v = 0; v < el.size(); ++v)
            if (el.eliminated(v)) maxLen = std::max(maxLen, el.expr(v).size()), total += el.expr(v).size();
        std::fprintf(stderr, "  grid map: expressions max %zu terms, %zu in all\n", maxLen, total);
    }
    // 4. Free unknowns. The reduced system C^T K C y = C^T (b - K d) (x = C y + d) is applied without forming it:
    // the cut tree makes C fill in (a transition is the composition of everything below it), C^T K C would not fit.
    const size_t nvar = el.size(), ncopy = 2 * size_t(copies);
    std::vector<uint32_t> freeId(nvar, kNone);
    std::vector<uint32_t> freeVars;
    for (uint32_t v = 0; v < nvar; ++v)
        if (!el.eliminated(v)) {
            freeId[v] = uint32_t(freeVars.size());
            freeVars.push_back(v);
        }
    const size_t nf = freeVars.size();
    auto expand = [&](uint32_t v, std::vector<Term>& terms, double& c) {
        terms.clear();
        if (!el.eliminated(v)) {
            terms.push_back({freeId[v], 1.0});
            c = 0.0;
            return;
        }
        for (const Term& t : el.expr(v)) terms.push_back({freeId[t.var], t.a});
        c = el.constant(v);
    };
    // K and b on the wedge unknowns: per triangle, the hat-function gradients against the target gradients.
    Csr K;
    K.n = ncopy;
    std::vector<double> bFull(ncopy, 0.0);
    {
        struct Trip {
            uint32_t r, c;
            double v;
        };
        std::vector<Trip> trips;
        trips.reserve(nt * 18);
        for (uint32_t t = 0; t < nt; ++t) {
            const glm::uvec3 tri = p.T[t];
            const vec3 o = p.P[tri.x];
            vec2 q[3];
            for (int k = 0; k < 3; ++k) {
                const vec3 d = p.P[tri[k]] - o;
                q[k] = {glm::dot(d, f.e1[t]), glm::dot(d, f.e2[t])};
            }
            const double A2 = double(q[1].x - q[0].x) * double(q[2].y - q[0].y) - double(q[2].x - q[0].x) * double(q[1].y - q[0].y);
            if (!(A2 > 1e-30)) continue;
            const double A = 0.5 * A2;
            double gx[3], gy[3];
            for (int k = 0; k < 3; ++k) {
                const vec2 e = q[(k + 2) % 3] - q[(k + 1) % 3];
                gx[k] = -double(e.y) / A2;
                gy[k] = double(e.x) / A2;
            }
            const double h = (double(p.size[tri.x]) + double(p.size[tri.y]) + double(p.size[tri.z])) / 3.0;
            const double th = double(field.theta[t]);
            const double gu[2] = {std::cos(th) / h, std::sin(th) / h}, gv[2] = {-std::sin(th) / h, std::cos(th) / h};
            for (int coord = 0; coord < 2; ++coord) {
                const double* g = coord == 0 ? gu : gv;
                for (int i = 0; i < 3; ++i) {
                    const uint32_t vi = U(3 * t + uint32_t(i)) + uint32_t(coord);
                    bFull[vi] += A * (gx[i] * g[0] + gy[i] * g[1]);
                    for (int j = 0; j < 3; ++j) {
                        const uint32_t vj = U(3 * t + uint32_t(j)) + uint32_t(coord);
                        trips.push_back({vi, vj, A * (gx[i] * gx[j] + gy[i] * gy[j])});
                    }
                }
            }
        }
        std::sort(trips.begin(), trips.end(), [](const Trip& x, const Trip& y) { return x.r < y.r || (x.r == y.r && x.c < y.c); });
        K.off.assign(ncopy + 1, 0);
        for (size_t i = 0; i < trips.size();) {
            size_t j = i;
            double v = 0.0;
            while (j < trips.size() && trips[j].r == trips[i].r && trips[j].c == trips[i].c) v += trips[j++].v;
            K.col.push_back(trips[i].c);
            K.val.push_back(v);
            ++K.off[trips[i].r + 1];
            i = j;
        }
        for (size_t i = 0; i < ncopy; ++i) K.off[i + 1] += K.off[i];
        K.diag.assign(ncopy, 0.0);
        for (size_t i = 0; i < ncopy; ++i)
            for (uint32_t k = K.off[i]; k < K.off[i + 1]; ++k)
                if (K.col[k] == i) K.diag[i] = K.val[k];
    }
    // C as rows (per wedge unknown) and columns (per free unknown).
    std::vector<uint32_t> cOff(ncopy + 1, 0), cCol;
    std::vector<double> cVal, dConst(ncopy, 0.0);
    {
        std::vector<Term> terms;
        for (uint32_t v = 0; v < ncopy; ++v) {
            double c;
            expand(v, terms, c);
            dConst[v] = c;
            for (const Term& t : terms) cCol.push_back(t.var), cVal.push_back(t.a);
            cOff[v + 1] = uint32_t(cCol.size());
        }
    }
    std::vector<uint32_t> tOff(nf + 1, 0), tRow(cCol.size());
    std::vector<double> tVal(cCol.size());
    for (uint32_t c : cCol) ++tOff[c + 1];
    for (size_t i = 0; i < nf; ++i) tOff[i + 1] += tOff[i];
    {
        std::vector<uint32_t> fill(tOff.begin(), tOff.end() - 1);
        for (uint32_t v = 0; v < ncopy; ++v)
            for (uint32_t k = cOff[v]; k < cOff[v + 1]; ++k) {
                const uint32_t at = fill[cCol[k]]++;
                tRow[at] = v;
                tVal[at] = cVal[k];
            }
    }
    if (timing) std::fprintf(stderr, "  grid map: C has %zu terms\n", cCol.size());
    auto mulK = [&](const std::vector<double>& x, std::vector<double>& z) {
        poolFor(ncopy, [&](size_t b0, size_t e0) {
            for (size_t i = b0; i < e0; ++i) {
                double s = 0.0;
                for (uint32_t k = K.off[i]; k < K.off[i + 1]; ++k) s += K.val[k] * x[K.col[k]];
                z[i] = s;
            }
        });
    };
    std::vector<double> rhs(nf, 0.0), diag(nf, 0.0);
    {
        std::vector<double> kd(ncopy);
        mulK(dConst, kd);
        for (size_t j = 0; j < nf; ++j) {
            double s = 0.0, dg = 0.0;
            for (uint32_t k = tOff[j]; k < tOff[j + 1]; ++k) {
                s += tVal[k] * (bFull[tRow[k]] - kd[tRow[k]]);
                dg += tVal[k] * tVal[k] * K.diag[tRow[k]];
            }
            rhs[j] = s;
            diag[j] = dg;
        }
    }
    double meanDiag = 0.0;
    for (double d : diag) meanDiag += d;
    meanDiag = nf ? meanDiag / double(nf) : 1.0;
    const double reg = 1e-7 * meanDiag;
    for (double& d : diag) d = d > 0.0 ? d + reg : 1.0;
    std::vector<double> xs(ncopy), zs(ncopy);
    const ReducedOp op{[&](const std::vector<double>& yv, std::vector<double>& outv) {
        poolFor(ncopy, [&](size_t b0, size_t e0) {
            for (size_t v = b0; v < e0; ++v) {
                double s = 0.0;
                for (uint32_t k = cOff[v]; k < cOff[v + 1]; ++k) s += cVal[k] * yv[cCol[k]];
                xs[v] = s;
            }
        });
        mulK(xs, zs);
        poolFor(nf, [&](size_t b0, size_t e0) {
            for (size_t j = b0; j < e0; ++j) {
                double s = reg * yv[j];
                for (uint32_t k = tOff[j]; k < tOff[j + 1]; ++k) s += tVal[k] * zs[tRow[k]];
                outv[j] = s;
            }
        });
    }, &diag, nf};
    // Multigrid preconditioner on a sparse stand-in for C^T K C: each wedge unknown mapped to its main free real
    // unknown only (the integer fill-in dropped), integer unknowns on their own diagonal.
    Multigrid<double> mg;
    {
        std::vector<int64_t> mainOf(ncopy, -1);
        std::vector<double> mainCoef(ncopy, 0.0);
        for (uint32_t v = 0; v < ncopy; ++v) {
            double best = 0.0;
            for (uint32_t k = cOff[v]; k < cOff[v + 1]; ++k)
                if (el.kind(freeVars[cCol[k]]) == 0 && std::abs(cVal[k]) > best) best = std::abs(cVal[k]), mainOf[v] = cCol[k], mainCoef[v] = cVal[k];
        }
        std::vector<std::pair<uint64_t, double>> trips;
        trips.reserve(K.val.size() + nf);
        for (uint32_t i = 0; i < ncopy; ++i) {
            if (mainOf[i] < 0) continue;
            for (uint32_t k = K.off[i]; k < K.off[i + 1]; ++k) {
                const uint32_t j = K.col[k];
                if (mainOf[j] < 0) continue;
                trips.push_back({(uint64_t(mainOf[i]) << 32) | uint64_t(mainOf[j]), mainCoef[i] * K.val[k] * mainCoef[j]});
            }
        }
        for (size_t j = 0; j < nf; ++j) trips.push_back({(uint64_t(j) << 32) | j, el.kind(freeVars[j]) != 0 ? diag[j] : reg});
        SparseMatrix<double> As;
        As.build(nf, trips);
        mg.build(As);
    }
    // The integer unknowns (singularity positions, seam shifts, feature lines) couple across the whole cut tree, which
    // the stand-in leaves out: they get an exact dense block A_ii = S^T K S (S: their columns of C), and the
    // preconditioner is a symmetric block Gauss-Seidel over (integers, reals, integers).
    std::vector<uint32_t> intIdx(nf, kNone), intVars;
    for (size_t j = 0; j < nf; ++j)
        if (el.kind(freeVars[j]) != 0) intIdx[j] = uint32_t(intVars.size()), intVars.push_back(uint32_t(j));
    const size_t mi = intVars.size();
    std::vector<double> Aii(mi * mi, 0.0);
    if (mi > 0 && mi <= 4000) {
        // S rows (per wedge unknown: its integer terms) and W = K S.
        std::vector<std::vector<std::pair<uint32_t, double>>> S(ncopy);
        for (uint32_t v = 0; v < ncopy; ++v)
            for (uint32_t k = cOff[v]; k < cOff[v + 1]; ++k)
                if (intIdx[cCol[k]] != kNone) S[v].push_back({intIdx[cCol[k]], cVal[k]});
        std::vector<double> acc(mi, 0.0);
        std::vector<uint32_t> touched;
        for (uint32_t v = 0; v < ncopy; ++v) {
            if (S[v].empty()) continue;
            // Row v of K S, then A_ii += S_v^T (K S)_v ... K is symmetric, so use (K S)_v = sum_u K_vu S_u.
            touched.clear();
            for (uint32_t k = K.off[v]; k < K.off[v + 1]; ++k)
                for (const auto& [j, c] : S[K.col[k]]) {
                    if (acc[j] == 0.0) touched.push_back(j);
                    acc[j] += K.val[k] * c;
                    if (acc[j] == 0.0) acc[j] = 1e-300;
                }
            for (const auto& [i, c] : S[v])
                for (uint32_t j : touched) Aii[size_t(i) * mi + j] += c * acc[j];
            for (uint32_t j : touched) acc[j] = 0.0;
        }
        for (size_t i = 0; i < mi; ++i) Aii[i * mi + i] += reg;
    }
    std::vector<double> Lii; // Cholesky of A_ii over the integers not fixed yet
    std::vector<uint32_t> liveInts;
    auto factorInts = [&](const std::vector<uint8_t>& fixedMask) {
        liveInts.clear();
        for (size_t a = 0; a < mi; ++a)
            if (!fixedMask[intVars[a]]) liveInts.push_back(uint32_t(a));
        const size_t m = liveInts.size();
        Lii.assign(m * m, 0.0);
        for (size_t a = 0; a < m; ++a)
            for (size_t b = 0; b < m; ++b) Lii[a * m + b] = Aii[size_t(liveInts[a]) * mi + liveInts[b]];
        for (size_t j = 0; j < m; ++j) {
            double d = Lii[j * m + j];
            for (size_t k = 0; k < j; ++k) d -= Lii[j * m + k] * Lii[j * m + k];
            d = std::sqrt(std::max(d, 1e-300));
            Lii[j * m + j] = d;
            for (size_t i = j + 1; i < m; ++i) {
                double s2 = Lii[i * m + j];
                for (size_t k = 0; k < j; ++k) s2 -= Lii[i * m + k] * Lii[j * m + k];
                Lii[i * m + j] = s2 / d;
            }
        }
    };
    auto solveInts = [&](std::vector<double>& rhsI) { // in place, over liveInts
        const size_t m = liveInts.size();
        for (size_t i = 0; i < m; ++i) {
            double s2 = rhsI[i];
            for (size_t k = 0; k < i; ++k) s2 -= Lii[i * m + k] * rhsI[k];
            rhsI[i] = s2 / Lii[i * m + i];
        }
        for (size_t i = m; i-- > 0;) {
            double s2 = rhsI[i];
            for (size_t k = i + 1; k < m; ++k) s2 -= Lii[k * m + i] * rhsI[k];
            rhsI[i] = s2 / Lii[i * m + i];
        }
    };
    const bool useIntBlock = mi > 0 && mi <= 4000;
    std::vector<double> pt(nf), pz(nf), pr(nf), ri;
    const std::function<void(const std::vector<double>&, std::vector<double>&)> precond =
        [&](const std::vector<double>& r, std::vector<double>& z) {
            if (!useIntBlock || liveInts.empty()) {
                mg.apply(r, z);
                return;
            }
            const size_t m = liveInts.size();
            // z_i1 = A_ii^-1 r_i
            ri.assign(m, 0.0);
            for (size_t a = 0; a < m; ++a) ri[a] = r[intVars[liveInts[a]]];
            solveInts(ri);
            std::fill(pz.begin(), pz.end(), 0.0);
            for (size_t a = 0; a < m; ++a) pz[intVars[liveInts[a]]] = ri[a];
            // z_r = MG(r_r - A_ri z_i1)
            op.apply(pz, pt);
            for (size_t j = 0; j < nf; ++j) pr[j] = intIdx[j] == kNone ? r[j] - pt[j] : 0.0;
            mg.apply(pr, z);
            for (size_t j = 0; j < nf; ++j)
                if (intIdx[j] != kNone) z[j] = 0.0;
            // z_i = A_ii^-1 (r_i - A_ir z_r)
            op.apply(z, pt);
            for (size_t a = 0; a < m; ++a) ri[a] = r[intVars[liveInts[a]]] - pt[intVars[liveInts[a]]];
            solveInts(ri);
            for (size_t a = 0; a < m; ++a) z[intVars[liveInts[a]]] = ri[a];
        };

    // 5. Solve, round the integers nearest-first, re-solve.
    std::vector<double> y(nf, 0.0);
    std::vector<uint8_t> fixed(nf, 0);
    bool stop = false;
    if (useIntBlock) factorInts(fixed);
    int iters = conjugateGradients(op, rhs, fixed, y, 1e-4, 2000, cancelled, stop, precond);
    if (stop) return false;
    // Before rounding: scale the map so its area (the number of unit squares, i.e. quads) is the wanted count. The
    // constraints are homogeneous, so a scaled solution is still a solution; distortion near singularities and
    // features otherwise leaves the count 10-20% off.
    if (out.wantQuads > 0.0) {
        double area = 0.0;
        std::vector<Term> terms;
        auto at = [&](uint32_t corner, int coord) {
            double k;
            expand(U(corner) + uint32_t(coord), terms, k);
            double s = k;
            for (const Term& t : terms) s += t.a * y[t.var];
            return s;
        };
        for (uint32_t t = 0; t < nt; ++t) {
            const double ax = at(3 * t, 0), ay = at(3 * t, 1), bx = at(3 * t + 1, 0), by = at(3 * t + 1, 1);
            const double cx = at(3 * t + 2, 0), cy = at(3 * t + 2, 1);
            area += 0.5 * std::abs((bx - ax) * (cy - ay) - (cx - ax) * (by - ay));
        }
        if (area > 0.0) {
            const double s = std::sqrt(out.wantQuads / area);
            for (double& v : y) v *= s;
            for (double& v : rhs) v *= s;
            if (timing) std::fprintf(stderr, "  grid map: area %.0f, scaled by %.3f\n", area, s);
        }
    }
    std::vector<uint32_t> intFree;
    for (size_t i = 0; i < nf; ++i)
        if (el.kind(freeVars[i]) != 0) intFree.push_back(uint32_t(i));
    size_t rounds = 0;
    {
        std::vector<uint32_t> left = intFree;
        while (!left.empty()) {
            std::vector<std::pair<double, uint32_t>> dist;
            for (uint32_t i : left) dist.push_back({std::abs(y[i] - std::round(y[i])), i});
            std::sort(dist.begin(), dist.end());
            static const double frac = std::getenv("SF_QR2_ROUND") ? std::atof(std::getenv("SF_QR2_ROUND")) : 0.7;
            size_t take = std::max<size_t>(1, size_t(double(left.size()) * frac + 0.999));
            while (take < dist.size() && dist[take].first < 0.08) ++take;
            if (left.size() <= 8) take = left.size();
            for (size_t k = 0; k < take; ++k) {
                const uint32_t i = dist[k].second;
                y[i] = std::round(y[i]);
                fixed[i] = 1;
            }
            std::vector<uint32_t> rest;
            for (size_t k = take; k < dist.size(); ++k) rest.push_back(dist[k].second);
            left.swap(rest);
            static const double midTol = std::getenv("SF_QR2_TOL") ? std::atof(std::getenv("SF_QR2_TOL")) : 1e-2;
            if (useIntBlock) factorInts(fixed);
            iters += conjugateGradients(op, rhs, fixed, y, left.empty() ? 1e-4 : midTol, left.empty() ? 2000 : 500, cancelled, stop, precond);
            if (stop) return false;
            ++rounds;
        }
    }
    if (timing)
        std::fprintf(stderr, "  grid map: %zu free unknowns (%zu integer), %zu rounding rounds, %d CG iterations\n", nf,
                     intFree.size(), rounds, iters);
    // 6. The map at each corner.
    out.uv.assign(nc, vec2(0.0f));
    std::vector<Term> terms;
    for (uint32_t c = 0; c < nc; ++c) {
        double val[2];
        for (int coord = 0; coord < 2; ++coord) {
            double k;
            expand(U(c) + uint32_t(coord), terms, k);
            double s = k;
            for (const Term& t : terms) s += t.a * y[t.var];
            val[coord] = s;
        }
        out.uv[c] = vec2(float(val[0]), float(val[1]));
    }
    if (timing) {
        double worst = 0.0;
        size_t off = 0;
        for (uint32_t c = 0; c < nc; ++c) {
            if (!p.feature[c]) continue;
            const uint32_t t = c / 3, k = c % 3;
            const vec3 e = p.P[p.T[t][(k + 1) % 3]] - p.P[p.T[t][k]];
            const int coord = axisOfEdge(t, e) == 0 ? 1 : 0;
            for (uint32_t cc : {c, 3 * t + (k + 1) % 3}) {
                const double v = coord ? out.uv[cc].y : out.uv[cc].x;
                const double d = std::abs(v - std::round(v));
                worst = std::max(worst, d);
                off += d > 0.01;
            }
        }
        std::fprintf(stderr, "  grid map: feature corners off their lattice line: %zu (worst %.3f)\n", off, worst);
    }
    out.flipped = 0;
    for (uint32_t t = 0; t < nt; ++t) {
        const vec2 a = out.uv[3 * t], b = out.uv[3 * t + 1], c = out.uv[3 * t + 2];
        const float area = (b.x - a.x) * (c.y - a.y) - (c.x - a.x) * (b.y - a.y);
        if (!(area > 0.0f)) ++out.flipped;
    }
    if (timing) {
        std::vector<uint8_t> fv(nv, 0);
        for (uint32_t c = 0; c < nc; ++c)
            if (p.feature[c]) fv[p.T[c / 3][c % 3]] = fv[p.T[c / 3][(c % 3 + 1) % 3]] = 1;
        size_t nearF = 0, nearS = 0;
        for (uint32_t t = 0; t < nt; ++t) {
            const vec2 a = out.uv[3 * t], b = out.uv[3 * t + 1], c = out.uv[3 * t + 2];
            if ((b.x - a.x) * (c.y - a.y) - (c.x - a.x) * (b.y - a.y) > 0.0f) continue;
            bool f2 = false, s2 = false;
            for (int k = 0; k < 3; ++k) f2 |= fv[p.T[t][k]] != 0, s2 |= field.vertexIndex[p.T[t][k]] != 0;
            nearF += f2, nearS += s2;
        }
        std::fprintf(stderr, "  grid map: flipped triangles touching a feature %zu, a singularity %zu\n", nearF, nearS);
    }
    out.cutEdges = cutEdges;
    out.integerVars = intFree.size();
    return true;
}

} // namespace sf::qr2
