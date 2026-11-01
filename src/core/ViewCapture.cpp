// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/ViewCapture.h"

#include "core/AtomicRef.h"
#include "core/BVH.h"
#include "core/BrushLibrary.h"
#include "core/Commands.h"
#include "core/Document.h"
#include "core/Mesh.h"
#include "core/PathStroke.h"
#include "core/ThreadPool.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <deque>
#include <filesystem>
#include <limits>

namespace sf {

namespace {

constexpr float kInf = std::numeric_limits<float>::infinity();

float smooth01(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

// 1 inside, fading to 0 over the last `fade` of the square (or circle) at u, v in -1..1.
float borderFade(float u, float v, bool circle, float fade) {
    const float d = circle ? std::sqrt(u * u + v * v) : std::max(std::abs(u), std::abs(v));
    if (d >= 1.0f) return 0.0f;
    if (fade <= 1e-4f) return 1.0f;
    return smooth01((1.0f - d) / fade);
}

// A triangle in texel coordinates (x right, y down; pixel centres at integers) with a depth (smaller = in front)
// and a vector attribute per corner.
struct RasterTri {
    glm::vec2 q[3];
    float z[3];
    glm::vec3 a[3];
};

// Front-most rasterization of `tris` into res x res: depth (kInf where nothing) and, when given, the attribute.
// Rows are split into bands rasterized in parallel, so no texel is written by two threads.
void rasterize(int res, const std::vector<RasterTri>& tris, std::vector<float>& depth, std::vector<glm::vec3>* attr) {
    depth.assign(size_t(res) * size_t(res), kInf);
    if (attr) attr->assign(depth.size(), glm::vec3(0.0f));
    const size_t bands = size_t(std::min(res, 64));
    std::vector<std::vector<uint32_t>> inBand(bands);
    for (uint32_t t = 0; t < tris.size(); ++t) {
        const RasterTri& r = tris[t];
        const float lo = std::min({r.q[0].y, r.q[1].y, r.q[2].y}), hi = std::max({r.q[0].y, r.q[1].y, r.q[2].y});
        const int r0 = std::max(0, int(std::ceil(lo - 1e-4f))), r1 = std::min(res - 1, int(std::floor(hi + 1e-4f)));
        if (r0 > r1) continue;
        const size_t b0 = size_t(r0) * bands / size_t(res), b1 = size_t(r1) * bands / size_t(res);
        for (size_t b = b0; b <= b1; ++b) inBand[b].push_back(t);
    }
    poolFor(bands, [&](size_t bb, size_t be) {
        for (size_t band = bb; band < be; ++band) {
            const int rowLo = int(band * size_t(res) / bands), rowHi = int((band + 1) * size_t(res) / bands) - 1;
            for (uint32_t t : inBand[band]) {
                const RasterTri& r = tris[t];
                const glm::vec2* q = r.q;
                const float area = (q[1].x - q[0].x) * (q[2].y - q[0].y) - (q[2].x - q[0].x) * (q[1].y - q[0].y);
                if (std::abs(area) < 1e-12f) continue;
                const int x0 = std::max(0, int(std::ceil(std::min({q[0].x, q[1].x, q[2].x}) - 1e-4f)));
                const int x1 = std::min(res - 1, int(std::floor(std::max({q[0].x, q[1].x, q[2].x}) + 1e-4f)));
                const int y0 = std::max(rowLo, int(std::ceil(std::min({q[0].y, q[1].y, q[2].y}) - 1e-4f)));
                const int y1 = std::min(rowHi, int(std::floor(std::max({q[0].y, q[1].y, q[2].y}) + 1e-4f)));
                for (int y = y0; y <= y1; ++y)
                    for (int x = x0; x <= x1; ++x) {
                        const glm::vec2 c{float(x), float(y)};
                        auto edge = [&](int a, int b) {
                            return ((q[b].x - q[a].x) * (c.y - q[a].y) - (c.x - q[a].x) * (q[b].y - q[a].y)) / area;
                        };
                        const float w0 = edge(1, 2), w1 = edge(2, 0), w2 = edge(0, 1);
                        if (w0 < -1e-4f || w1 < -1e-4f || w2 < -1e-4f) continue;
                        const float z = r.z[0] * w0 + r.z[1] * w1 + r.z[2] * w2;
                        const size_t i = size_t(y) * size_t(res) + size_t(x);
                        if (z >= depth[i]) continue;
                        depth[i] = z;
                        if (attr) (*attr)[i] = r.a[0] * w0 + r.a[1] * w1 + r.a[2] * w2;
                    }
            }
        }
    }, 1, 1);
}

glm::vec2 texel(float u, float v, int res) {
    return glm::vec2((u * 0.5f + 0.5f) * float(res) - 0.5f, (0.5f - v * 0.5f) * float(res) - 0.5f);
}

// The front-most depth along the view (dot(x - centre, forward)) over the square, from the triangles near it.
void viewDepth(const Mesh& mesh, const BVH& bvh, const CaptureFrame& f, int res, std::vector<float>& depth) {
    std::vector<uint32_t> tris;
    bvh.queryTriangles(mesh, f.center, f.halfSize * 2.6f, tris);
    std::vector<RasterTri> raster;
    raster.reserve(tris.size());
    const float inv = 1.0f / f.halfSize;
    for (uint32_t t : tris) {
        if (mesh.hasHidden() && mesh.triangleHidden(t)) continue;
        RasterTri r;
        float umin = kInf, umax = -kInf, vmin = kInf, vmax = -kInf;
        for (int k = 0; k < 3; ++k) {
            const glm::vec3 o = mesh.positions[mesh.triangles[t][k]] - f.center;
            const float u = glm::dot(o, f.right) * inv, v = glm::dot(o, f.up) * inv;
            umin = std::min(umin, u), umax = std::max(umax, u), vmin = std::min(vmin, v), vmax = std::max(vmax, v);
            r.q[k] = texel(u, v, res);
            r.z[k] = glm::dot(o, f.forward);
            r.a[k] = glm::vec3(0.0f);
        }
        if (umax < -1.0f || umin > 1.0f || vmax < -1.0f || vmin > 1.0f) continue;
        raster.push_back(r);
    }
    rasterize(res, raster, depth, nullptr);
}

// A wide blur of `h` weighted by `m` (normalized convolution: uncovered texels don't pull it down): three box
// blurs of running sums, which approximate a Gaussian of `sigma` in time independent of its width.
std::vector<float> maskedBlur(const std::vector<float>& h, const std::vector<float>& m, int res, float sigma) {
    const int rad = std::max(1, int(std::lround(std::sqrt(sigma * sigma + 1.0f) * 0.8660254f))); // 3 boxes ~ sigma
    const size_t n = size_t(res) * size_t(res);
    std::vector<float> hm(n), mm(m), tmp(n);
    for (size_t i = 0; i < n; ++i) hm[i] = h[i] * m[i];
    auto box = [&](std::vector<float>& img, bool horizontal) {
        poolFor(size_t(res), [&](size_t b, size_t e) {
            std::vector<double> line(size_t(res) + 1);
            for (size_t row = b; row < e; ++row) {
                auto at = [&](int c) -> float& {
                    return horizontal ? img[row * size_t(res) + size_t(c)] : img[size_t(c) * size_t(res) + row];
                };
                line[0] = 0.0;
                for (int c = 0; c < res; ++c) line[size_t(c) + 1] = line[size_t(c)] + double(at(c));
                for (int c = 0; c < res; ++c) {
                    const int lo = std::max(0, c - rad), hi = std::min(res - 1, c + rad);
                    at(c) = float((line[size_t(hi) + 1] - line[size_t(lo)]) / double(2 * rad + 1));
                }
            }
        }, 8);
    };
    for (int pass = 0; pass < 3; ++pass)
        for (bool horizontal : {true, false}) {
            box(hm, horizontal);
            box(mm, horizontal);
        }
    std::vector<float> out(n, 0.0f);
    for (size_t i = 0; i < n; ++i) out[i] = mm[i] > 1e-6f ? hm[i] / mm[i] : 0.0f;
    return out;
}

} // namespace

CaptureFrame makeCaptureFrame(const glm::vec3& center, const glm::vec3& forward, const glm::vec3& up, float halfSize,
                              bool circle) {
    CaptureFrame f;
    f.center = center;
    f.forward = glm::length(forward) > 1e-12f ? glm::normalize(forward) : glm::vec3(0.0f, 0.0f, -1.0f);
    glm::vec3 u = up - f.forward * glm::dot(up, f.forward);
    if (glm::length(u) < 1e-6f) {
        const glm::vec3 helper = std::abs(f.forward.y) > 0.9f ? glm::vec3(0.0f, 0.0f, -1.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
        u = helper - f.forward * glm::dot(helper, f.forward);
    }
    f.up = glm::normalize(u);
    f.right = glm::cross(f.forward, f.up); // forward x up = right for a camera looking along forward
    f.halfSize = std::max(halfSize, 1e-6f);
    f.circle = circle;
    return f;
}

// ---------------------------------------------------------------------------
// Height alpha
// ---------------------------------------------------------------------------

bool captureHeightAlpha(const Mesh& mesh, const BVH& bvh, const CaptureFrame& f, const CaptureOptions& opt,
                        BrushAlpha& out, std::string& error, CaptureInfo* info) {
    const int res = std::clamp(opt.resolution, 32, 1024);
    if (mesh.triangleCount() == 0) {
        error = "The active Part is empty";
        return false;
    }
    std::vector<float> depth;
    viewDepth(mesh, bvh, f, res, depth);
    const size_t n = depth.size();
    std::vector<float> h(n, 0.0f), m(n, 0.0f);
    int covered = 0;
    for (size_t i = 0; i < n; ++i)
        if (depth[i] < kInf) {
            h[i] = -depth[i]; // toward the viewer
            m[i] = 1.0f;
            ++covered;
        }
    if (covered < 16) {
        error = "Nothing to capture there: drag over the model";
        return false;
    }
    auto uvOf = [&](size_t i) {
        const int x = int(i % size_t(res)), y = int(i / size_t(res));
        return glm::vec2((float(x) + 0.5f) / float(res) * 2.0f - 1.0f, 1.0f - (float(y) + 0.5f) / float(res) * 2.0f);
    };
    if (opt.fitPlane) {
        // Least squares h = a u + b v + c over the covered texels: the tilt of the surface goes.
        double s[3][3] = {}, r[3] = {};
        for (size_t i = 0; i < n; ++i) {
            if (m[i] == 0.0f) continue;
            const glm::vec2 uv = uvOf(i);
            const double x[3] = {uv.x, uv.y, 1.0};
            for (int a = 0; a < 3; ++a) {
                r[a] += x[a] * h[i];
                for (int b = 0; b < 3; ++b) s[a][b] += x[a] * x[b];
            }
        }
        // Cramer's rule on the 3x3 normal equations.
        auto det3 = [](double A[3][3]) {
            return A[0][0] * (A[1][1] * A[2][2] - A[1][2] * A[2][1]) - A[0][1] * (A[1][0] * A[2][2] - A[1][2] * A[2][0]) +
                   A[0][2] * (A[1][0] * A[2][1] - A[1][1] * A[2][0]);
        };
        const double d = det3(s);
        if (std::abs(d) > 1e-12) {
            double coef[3];
            for (int c = 0; c < 3; ++c) {
                double A[3][3];
                for (int a = 0; a < 3; ++a)
                    for (int b = 0; b < 3; ++b) A[a][b] = b == c ? r[a] : s[a][b];
                coef[c] = det3(A) / d;
            }
            for (size_t i = 0; i < n; ++i) {
                if (m[i] == 0.0f) continue;
                const glm::vec2 uv = uvOf(i);
                h[i] -= float(coef[0] * uv.x + coef[1] * uv.y + coef[2]);
            }
        }
    }
    if (opt.smoothing > 0.0f) {
        // Detail only: the smooth form (a wide blur) is taken away, so a curved surface gives a flat stamp.
        const float sigma = std::max(1.0f, (1.05f - std::clamp(opt.smoothing, 0.0f, 1.0f)) * float(res) * 0.25f);
        const std::vector<float> base = maskedBlur(h, m, res, sigma);
        for (size_t i = 0; i < n; ++i)
            if (m[i] > 0.0f) h[i] -= base[i];
    }
    // Normalize: the 2nd percentile is 0, the top is 1.
    std::vector<float> vals;
    vals.reserve(size_t(covered));
    for (size_t i = 0; i < n; ++i)
        if (m[i] > 0.0f) vals.push_back(h[i]);
    std::sort(vals.begin(), vals.end());
    const float lo = vals[vals.size() / 50], hi = vals.back();
    const float range = std::max(hi - lo, 1e-9f);
    out = BrushAlpha{};
    out.name = "Captured";
    out.width = out.height = res;
    out.values.assign(n, 0.0f);
    const float fade = std::clamp(opt.edgeFade, 0.0f, 0.9f);
    for (size_t i = 0; i < n; ++i) {
        if (m[i] == 0.0f) continue;
        float v = std::clamp((h[i] - lo) / range, 0.0f, 1.0f);
        if (opt.invert) v = 1.0f - v;
        const glm::vec2 uv = uvOf(i);
        out.values[i] = v * borderFade(uv.x, uv.y, f.circle, fade);
    }
    if (info) {
        info->depthRange = hi - lo;
        info->coverage = covered;
    }
    return true;
}

// ---------------------------------------------------------------------------
// VDM
// ---------------------------------------------------------------------------

bool captureVdmFromView(const Mesh& mesh, const BVH& bvh, const CaptureFrame& f, const CaptureOptions& opt,
                        VectorDisplacement& out, std::string& error, CaptureInfo* info) {
    const int res = std::clamp(opt.resolution, 32, 1024);
    if (mesh.triangleCount() == 0) {
        error = "The active Part is empty";
        return false;
    }
    const float half = f.halfSize, inv = 1.0f / half;
    // 1. The front surface in the square: candidates near the centre whose projection is inside (a little
    // margin, so the base reaches the border) and that are not much behind the front-most surface there (the
    // undersides of overhangs stay, the far side of the model doesn't).
    constexpr int kVisRes = 128;
    std::vector<float> front;
    viewDepth(mesh, bvh, f, kVisRes, front);
    std::vector<uint32_t> cand;
    bvh.queryVertices(mesh, f.center, half * 2.6f, cand); // sorted
    const float tol = 0.75f * half;
    std::vector<uint8_t> ok(cand.size(), 0);
    poolFor(cand.size(), [&](size_t b, size_t e) {
        for (size_t i = b; i < e; ++i) {
            const uint32_t v = cand[i];
            if (mesh.hasHidden() && mesh.vertexHidden(v)) continue;
            const glm::vec3 o = mesh.positions[v] - f.center;
            const float u = glm::dot(o, f.right) * inv, w = glm::dot(o, f.up) * inv;
            if (std::abs(u) > 1.08f || std::abs(w) > 1.08f) continue;
            const glm::vec2 q = texel(std::clamp(u, -1.0f, 1.0f), std::clamp(w, -1.0f, 1.0f), kVisRes);
            const int x = std::clamp(int(std::lround(q.x)), 0, kVisRes - 1), y = std::clamp(int(std::lround(q.y)), 0, kVisRes - 1);
            float zf = kInf; // the nearest front depth around (holes in the coarse map at silhouettes)
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx) {
                    const int xx = std::clamp(x + dx, 0, kVisRes - 1), yy = std::clamp(y + dy, 0, kVisRes - 1);
                    zf = std::min(zf, front[size_t(yy) * kVisRes + size_t(xx)]);
                }
            if (zf < kInf && glm::dot(o, f.forward) <= zf + tol) ok[i] = 1;
        }
    });
    auto candIndex = [&](uint32_t v) -> int64_t {
        const auto it = std::lower_bound(cand.begin(), cand.end(), v);
        return it != cand.end() && *it == v ? int64_t(it - cand.begin()) : -1;
    };
    // 2. Connected to the surface under the centre: a flood fill over the candidates, a ring at a time, each ring
    // in parallel (a vertex joins once: an atomic flag).
    int64_t seed = -1;
    float best = kInf;
    for (size_t i = 0; i < cand.size(); ++i)
        if (ok[i]) {
            const float d = glm::distance(mesh.positions[cand[i]], f.center);
            if (d < best) best = d, seed = int64_t(i);
        }
    if (seed < 0) {
        error = "Nothing to capture there: drag over the model";
        return false;
    }
    std::vector<uint32_t> inRegion(cand.size(), 0);
    inRegion[size_t(seed)] = 1;
    std::vector<uint32_t> frontier{uint32_t(seed)};
    while (!frontier.empty()) {
        constexpr size_t kBlock = 1024;
        std::vector<std::vector<uint32_t>> next((frontier.size() + kBlock - 1) / kBlock);
        poolFor(frontier.size(), [&](size_t b, size_t e) {
            std::vector<uint32_t>& out = next[b / kBlock];
            for (size_t q = b; q < e; ++q)
                for (uint32_t nb : mesh.neighbors(cand[frontier[q]])) {
                    const int64_t j = candIndex(nb);
                    if (j < 0 || !ok[size_t(j)]) continue;
                    AtomicRef<uint32_t> flag(inRegion[size_t(j)]);
                    if (flag.load(std::memory_order_relaxed) || flag.exchange(1, std::memory_order_relaxed)) continue;
                    out.push_back(uint32_t(j));
                }
        }, kBlock);
        frontier.clear();
        for (const auto& part : next) frontier.insert(frontier.end(), part.begin(), part.end());
    }
    std::vector<uint32_t> region; // sorted (cand is)
    for (size_t i = 0; i < cand.size(); ++i)
        if (inRegion[i]) region.push_back(cand[i]);
    const size_t n = region.size();
    if (n < 16) {
        error = "Too few points in the square: drag a larger square, or divide the mesh";
        return false;
    }
    auto local = [&](uint32_t v) -> int64_t {
        const auto it = std::lower_bound(region.begin(), region.end(), v);
        return it != region.end() && *it == v ? int64_t(it - region.begin()) : -1;
    };
    // Adjacency inside the region (CSR, counted then filled in parallel) and the rim (vertices with a neighbour
    // outside it).
    std::vector<uint32_t> start(n + 1, 0), adj;
    std::vector<uint8_t> rim(n, 0);
    constexpr size_t kAdjBlock = 4096;
    std::vector<double> edgeParts((n + kAdjBlock - 1) / kAdjBlock, 0.0);
    poolFor(n, [&](size_t b, size_t e) {
        double sum = 0.0;
        for (size_t i = b; i < e; ++i) {
            uint32_t count = 0;
            for (uint32_t nb : mesh.neighbors(region[i])) {
                if (local(nb) < 0) {
                    rim[i] = 1;
                    continue;
                }
                ++count;
                sum += glm::distance(mesh.positions[region[i]], mesh.positions[nb]);
            }
            start[i + 1] = count;
        }
        edgeParts[b / kAdjBlock] = sum;
    }, kAdjBlock);
    for (size_t i = 0; i < n; ++i) start[i + 1] += start[i];
    adj.resize(start[n]);
    poolFor(n, [&](size_t b, size_t e) {
        for (size_t i = b; i < e; ++i) {
            uint32_t k = start[i];
            for (uint32_t nb : mesh.neighbors(region[i]))
                if (const int64_t j = local(nb); j >= 0) adj[k++] = uint32_t(j);
        }
    }, kAdjBlock);
    double edgeSum = 0.0;
    for (double part : edgeParts) edgeSum += part;
    const size_t edgeCount = adj.size();
    const float meanEdge = edgeCount ? float(edgeSum / double(edgeCount)) : half * 0.01f;
    std::vector<glm::vec3> X(n);
    for (size_t i = 0; i < n; ++i) X[i] = mesh.positions[region[i]];
    // The region's triangles (each once: from its lowest-numbered vertex), gathered per block in parallel.
    std::vector<std::vector<glm::uvec3>> triParts((n + kAdjBlock - 1) / kAdjBlock);
    poolFor(n, [&](size_t b, size_t e) {
        std::vector<glm::uvec3>& out = triParts[b / kAdjBlock];
        for (size_t i = b; i < e; ++i)
            for (uint32_t t : mesh.vertexTriangles(region[i])) {
                const glm::uvec3 tri = mesh.triangles[t];
                if (std::min({tri.x, tri.y, tri.z}) != region[i]) continue;
                if (mesh.hasHidden() && mesh.triangleHidden(t)) continue;
                const int64_t a = local(tri.x), bb = local(tri.y), c = local(tri.z);
                if (a < 0 || bb < 0 || c < 0) continue;
                out.emplace_back(uint32_t(a), uint32_t(bb), uint32_t(c));
            }
    }, kAdjBlock);
    std::vector<glm::uvec3> tris;
    for (const auto& part : triParts) tris.insert(tris.end(), part.begin(), part.end());
    if (tris.empty()) {
        error = "Too few points in the square";
        return false;
    }
    auto areaNormal = [&](const std::vector<glm::vec3>& P) {
        glm::vec3 nsum(0.0f);
        for (const glm::uvec3& t : tris) nsum += glm::cross(P[t.y] - P[t.x], P[t.z] - P[t.x]);
        if (glm::dot(nsum, -f.forward) < 0.0f) nsum = -nsum; // toward the viewer
        return glm::length(nsum) > 1e-20f ? glm::normalize(nsum) : -f.forward;
    };
    // 3. The base surface.
    std::vector<glm::vec3> base(n);
    int iterations = 0;
    const float smoothing = std::clamp(opt.smoothing, 0.0f, 1.0f);
    glm::vec3 np = areaNormal(X);
    if (smoothing <= 0.0f) {
        for (size_t i = 0; i < n; ++i) base[i] = X[i] - np * glm::dot(X[i] - f.center, np);
    } else {
        // The broad form first: a quadric height field over the plane fitted to the outer band of the square (where
        // the detail usually isn't). Fairing alone would flatten a curved surface (a harmonic fill can't hold a
        // sphere's dome), so the dome would come out as "detail"; the quadric's own curvature stays in the base.
        glm::vec3 qr = f.right - np * glm::dot(f.right, np);
        qr = glm::length(qr) > 1e-6f ? glm::normalize(qr) : f.right;
        const glm::vec3 qu = glm::cross(np, qr);
        auto terms = [](double u, double v, double t[6]) {
            t[0] = 1.0, t[1] = u, t[2] = v, t[3] = u * u, t[4] = u * v, t[5] = v * v;
        };
        double coef[6] = {0, 0, 0, 0, 0, 0};
        for (int pass = 0; pass < 2; ++pass) {
            double M[6][7] = {};
            size_t used = 0;
            for (size_t i = 0; i < n; ++i) {
                const glm::vec3 o = X[i] - f.center;
                const double u = glm::dot(o, qr) * inv, v = glm::dot(o, qu) * inv;
                if (pass == 0 && std::max(std::abs(u), std::abs(v)) < 0.55 && !rim[i]) continue;
                double t[6];
                terms(u, v, t);
                const double h = glm::dot(o, np) * inv;
                for (int a = 0; a < 6; ++a) {
                    for (int c = 0; c < 6; ++c) M[a][c] += t[a] * t[c];
                    M[a][6] += t[a] * h;
                }
                ++used;
            }
            if (pass == 0 && used < 40) continue; // too few in the band: fit everything
            for (int a = 0; a < 6; ++a) M[a][a] += 1e-9; // (a degenerate band: stay near flat)
            // Gaussian elimination with partial pivoting.
            bool ok = true;
            for (int c = 0; c < 6 && ok; ++c) {
                int piv = c;
                for (int r2 = c + 1; r2 < 6; ++r2)
                    if (std::abs(M[r2][c]) > std::abs(M[piv][c])) piv = r2;
                if (std::abs(M[piv][c]) < 1e-14) {
                    ok = false;
                    break;
                }
                for (int k = 0; k < 7; ++k) std::swap(M[c][k], M[piv][k]);
                for (int r2 = 0; r2 < 6; ++r2) {
                    if (r2 == c) continue;
                    const double fct = M[r2][c] / M[c][c];
                    for (int k = c; k < 7; ++k) M[r2][k] -= fct * M[c][k];
                }
            }
            if (ok)
                for (int a = 0; a < 6; ++a) coef[a] = M[a][6] / M[a][a];
            break;
        }
        auto quadric = [&](const glm::vec3& p) {
            const glm::vec3 o = p - f.center;
            double t[6];
            terms(glm::dot(o, qr) * inv, glm::dot(o, qu) * inv, t);
            double h = 0.0;
            for (int a = 0; a < 6; ++a) h += coef[a] * t[a];
            return float(h) * half;
        };
        for (size_t i = 0; i < n; ++i) X[i] -= np * quadric(X[i]); // flattened: the fairing works on the rest
        // Implicit fairing with the rim held: (1 + l deg_i) x_i - l sum_{j inner} x_j = X_i + l sum_{j rim} X_j,
        // solved for the inner vertices by Jacobi-preconditioned conjugate gradients (three right-hand sides).
        const float sigma = half * (0.2f + 1.6f * smoothing); // the size of form the base keeps
        const float lambda = (sigma / std::max(meanEdge, 1e-9f)) * (sigma / std::max(meanEdge, 1e-9f));
        std::vector<glm::vec3> b(n), x(X), r(n), z(n), p(n), Ap(n);
        std::vector<float> diag(n);
        for (size_t i = 0; i < n; ++i) {
            diag[i] = 1.0f + lambda * float(start[i + 1] - start[i]);
            glm::vec3 rhs = X[i];
            for (uint32_t k = start[i]; k < start[i + 1]; ++k)
                if (rim[adj[k]]) rhs += lambda * X[adj[k]];
            b[i] = rim[i] ? glm::vec3(0.0f) : rhs;
        }
        auto apply = [&](const std::vector<glm::vec3>& v, std::vector<glm::vec3>& o) {
            poolFor(n, [&](size_t lo, size_t hi) {
                for (size_t i = lo; i < hi; ++i) {
                    if (rim[i]) {
                        o[i] = glm::vec3(0.0f);
                        continue;
                    }
                    glm::vec3 s(0.0f);
                    for (uint32_t k = start[i]; k < start[i + 1]; ++k)
                        if (!rim[adj[k]]) s += v[adj[k]];
                    o[i] = diag[i] * v[i] - lambda * s;
                }
            });
        };
        auto dot3 = [&](const std::vector<glm::vec3>& a, const std::vector<glm::vec3>& c) {
            constexpr size_t kBlock = 4096;
            std::vector<glm::dvec3> parts((n + kBlock - 1) / kBlock, glm::dvec3(0.0));
            poolFor(n, [&](size_t lo, size_t hi) {
                glm::dvec3 acc(0.0);
                for (size_t i = lo; i < hi; ++i) acc += glm::dvec3(a[i] * c[i]);
                parts[lo / kBlock] = acc;
            }, kBlock);
            glm::dvec3 s(0.0);
            for (const glm::dvec3& q : parts) s += q;
            return s;
        };
        apply(x, Ap);
        for (size_t i = 0; i < n; ++i) {
            r[i] = rim[i] ? glm::vec3(0.0f) : b[i] - Ap[i];
            z[i] = r[i] / diag[i];
            p[i] = z[i];
        }
        glm::dvec3 rz = dot3(r, z);
        const glm::dvec3 bb = dot3(b, b);
        const double tol2 = 1e-8 * std::max({bb.x, bb.y, bb.z, 1e-30}); // 1e-4 relative residual
        const int maxIter = 800;
        for (iterations = 0; iterations < maxIter; ++iterations) {
            const glm::dvec3 rr = dot3(r, r);
            if (rr.x + rr.y + rr.z <= tol2) break;
            apply(p, Ap);
            const glm::dvec3 pAp = dot3(p, Ap);
            const glm::vec3 alpha(pAp.x > 0 ? rz.x / pAp.x : 0.0, pAp.y > 0 ? rz.y / pAp.y : 0.0, pAp.z > 0 ? rz.z / pAp.z : 0.0);
            poolFor(n, [&](size_t lo, size_t hi) {
                for (size_t i = lo; i < hi; ++i) {
                    x[i] += alpha * p[i];
                    r[i] -= alpha * Ap[i];
                    z[i] = r[i] / diag[i];
                }
            });
            const glm::dvec3 rzNew = dot3(r, z);
            const glm::vec3 beta(rz.x > 0 ? rzNew.x / rz.x : 0.0, rz.y > 0 ? rzNew.y / rz.y : 0.0, rz.z > 0 ? rzNew.z / rz.z : 0.0);
            rz = rzNew;
            poolFor(n, [&](size_t lo, size_t hi) {
                for (size_t i = lo; i < hi; ++i) p[i] = z[i] + beta * p[i];
            });
        }
        for (size_t i = 0; i < n; ++i) {
            base[i] = rim[i] ? X[i] : x[i];
            base[i] += np * quadric(base[i]); // the broad form back on the base
            X[i] += np * quadric(X[i]);        // and on the surface
        }
        np = areaNormal(base);
    }
    // 4. The base's normals and the stamp's frame (the screen's right and up laid on the base plane).
    std::vector<glm::vec3> bn(n, glm::vec3(0.0f));
    for (const glm::uvec3& t : tris) {
        const glm::vec3 c = glm::cross(base[t.y] - base[t.x], base[t.z] - base[t.x]);
        bn[t.x] += c, bn[t.y] += c, bn[t.z] += c;
    }
    glm::vec3 right = f.right - np * glm::dot(f.right, np);
    right = glm::length(right) > 1e-6f ? glm::normalize(right) : f.right;
    const glm::vec3 up = glm::cross(np, right);
    std::vector<glm::vec3> offset(n), uvz(n);
    float maxOffset = 0.0f;
    for (size_t i = 0; i < n; ++i) {
        glm::vec3 ni = bn[i];
        if (glm::dot(ni, np) < 0.0f) ni = -ni;
        ni = glm::length(ni) > 1e-20f ? glm::normalize(ni) : np;
        if (smoothing <= 0.0f) ni = np;
        glm::vec3 t = right - ni * glm::dot(right, ni);
        t = glm::length(t) > 1e-6f ? glm::normalize(t) : right;
        const glm::vec3 bt = glm::cross(ni, t);
        const glm::vec3 d = X[i] - base[i];
        offset[i] = glm::vec3(glm::dot(d, t), glm::dot(d, bt), glm::dot(d, ni)) * inv;
        const glm::vec3 o = base[i] - f.center;
        uvz[i] = glm::vec3(glm::dot(o, right) * inv, glm::dot(o, up) * inv, -glm::dot(o, np));
    }
    // 5. Rasterize the offsets through the base's position on the plane (the front-most base wins a texel).
    std::vector<RasterTri> raster;
    raster.reserve(tris.size());
    for (const glm::uvec3& t : tris) {
        RasterTri r;
        for (int k = 0; k < 3; ++k) {
            const uint32_t i = t[k];
            r.q[k] = texel(uvz[i].x, uvz[i].y, res);
            r.z[k] = uvz[i].z;
            r.a[k] = offset[i];
        }
        raster.push_back(r);
    }
    std::vector<float> depth;
    std::vector<glm::vec3> vectors;
    rasterize(res, raster, depth, &vectors);
    const float fade = std::clamp(opt.edgeFade, 0.0f, 0.9f);
    int covered = 0;
    for (int y = 0; y < res; ++y)
        for (int x = 0; x < res; ++x) {
            const size_t i = size_t(y) * size_t(res) + size_t(x);
            if (depth[i] == kInf) continue;
            ++covered;
            const float u = (float(x) + 0.5f) / float(res) * 2.0f - 1.0f, v = 1.0f - (float(y) + 0.5f) / float(res) * 2.0f;
            vectors[i] *= borderFade(u, v, f.circle, fade);
            maxOffset = std::max(maxOffset, glm::length(vectors[i]));
        }
    out = VectorDisplacement{};
    out.name = "Captured VDM";
    out.width = out.height = res;
    out.vectors = std::move(vectors);
    if (info) {
        info->vertices = n;
        info->maxOffset = maxOffset;
        info->coverage = covered;
        info->iterations = iterations;
    }
    return true;
}

Image alphaToImage(const BrushAlpha& alpha) {
    Image img;
    img.width = alpha.width;
    img.height = alpha.height;
    img.rgba.resize(size_t(img.width) * size_t(img.height) * 4);
    for (size_t i = 0; i < alpha.values.size(); ++i) {
        const uint8_t g = uint8_t(std::lround(std::clamp(alpha.values[i], 0.0f, 1.0f) * 255.0f));
        img.rgba[i * 4 + 0] = img.rgba[i * 4 + 1] = img.rgba[i * 4 + 2] = g;
        img.rgba[i * 4 + 3] = 255;
    }
    return img;
}

// ---------------------------------------------------------------------------
// Commands
// ---------------------------------------------------------------------------

namespace {

std::string utf8(const std::filesystem::path& p) {
    const std::u8string s = p.u8string();
    return std::string(s.begin(), s.end());
}
std::filesystem::path fsPath(const std::string& s) { return std::filesystem::path(std::u8string(s.begin(), s.end())); }

// The frame from a script's point / view / up / size, or an error.
bool frameFromArgs(Document& doc, const Args& a, CaptureFrame& f, std::string& error) {
    glm::vec3 point, view(0.0f, 0.0f, -1.0f), up(0.0f, 1.0f, 0.0f);
    if (!parseVec3(argString(a, "point"), point)) return error = "Bad point (expected x,y,z)", false;
    if (a.count("view") && !parseVec3(argString(a, "view"), view)) return error = "Bad view (expected x,y,z)", false;
    if (a.count("up") && !parseVec3(argString(a, "up"), up)) return error = "Bad up (expected x,y,z)", false;
    if (glm::length(view) < 1e-6f) return error = "The view direction must not be zero", false;
    view = glm::normalize(view);
    if (doc.mesh.triangleCount() == 0) return error = "The active Part is empty", false;
    doc.syncBvh();
    const AABB box = doc.mesh.bounds();
    const float diag = std::max(glm::length(box.extent()), 1e-4f);
    RayHit hit;
    const glm::vec3 origin = point - view * (glm::dot(point - box.center(), view) + diag);
    if (!doc.bvh.raycast(doc.mesh, origin, view, hit)) return error = "The ray misses the surface", false;
    float size = float(argNumber(a, "size", 0.0));
    if (!(size > 0.0f)) size = diag * 0.05f;
    f = makeCaptureFrame(hit.position, view, up, size, argBool(a, "circle", false));
    return true;
}

CaptureOptions optionsFromArgs(const Args& a) {
    CaptureOptions o;
    o.resolution = std::clamp(int(argNumber(a, "resolution", 256)), 32, 1024);
    o.smoothing = float(std::clamp(argNumber(a, "smoothing", 0.5), 0.0, 1.0));
    o.edgeFade = float(std::clamp(argNumber(a, "edge_fade", 0.25), 0.0, 0.9));
    o.fitPlane = argBool(a, "fit_plane", true);
    o.invert = argBool(a, "invert", false);
    return o;
}

} // namespace

void registerCaptureCommands(CommandRegistry& registry) {
    using T = ParamSpec::Type;
    const std::vector<ParamSpec> common = {
        {"point", T::String, "x,y,z the view ray goes through; where it meets the surface is the capture's centre.", true},
        {"view", T::String, "x,y,z view direction into the scene (default 0,0,-1).", false},
        {"up", T::String, "x,y,z screen up (default 0,1,0).", false},
        {"size", T::Number, "Half the square's side in world units (default 5% of the model's size).", false},
        {"circle", T::Bool, "Capture the circle inside the square only.", false},
        {"resolution", T::Number, "Pixels a side (32-1024, default 256).", false},
        {"smoothing", T::Number, "0-1: how much of the smooth form the base takes away (0 = flat plane; default 0.5).", false},
        {"edge_fade", T::Number, "0-0.9: border fade, as a fraction of the half size (default 0.25).", false},
    };
    std::vector<ParamSpec> alphaParams = common;
    alphaParams.push_back({"fit_plane", T::Bool, "Height over a plane fitted to the surface (default true) instead of the view plane.", false});
    alphaParams.push_back({"invert", T::Bool, "White = deepest.", false});
    alphaParams.push_back({"path", T::String, "Also save it as a PNG here (e.g. your library's Alphas folder).", false});
    alphaParams.push_back({"name", T::String, "Name of the alpha (default Captured).", false});
    registry.add({"capture_alpha",
                  "Capture the surface in a square seen along `view` as a grayscale height alpha and make it the brush "
                  "alpha (Alpha from viewport): its height toward the viewer, normalized to 0..1.",
                  alphaParams,
                  [](Document& doc, const Args& a) -> CommandResult {
                      CaptureFrame f;
                      std::string err;
                      if (!frameFromArgs(doc, a, f, err)) return {false, err};
                      const auto t0 = std::chrono::steady_clock::now();
                      BrushAlpha alpha;
                      CaptureInfo info;
                      if (!captureHeightAlpha(doc.mesh, doc.bvh, f, optionsFromArgs(a), alpha, err, &info)) return {false, err};
                      alpha.name = argString(a, "name", "Captured");
                      if (alpha.name.empty()) alpha.name = "Captured";
                      std::string note;
                      if (const std::string path = argString(a, "path"); !path.empty()) {
                          std::error_code ec;
                          std::filesystem::create_directories(fsPath(path).parent_path(), ec);
                          if (!alphaToImage(alpha).savePng(path, err)) return {false, err};
                          note = ", saved " + path;
                      }
                      doc.brushAlpha = std::move(alpha);
                      char buf[96];
                      std::snprintf(buf, sizeof buf, " (%d px, height range %.4g, %.0f ms)", doc.brushAlpha.width, info.depthRange,
                                    std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
                      return {true, "Captured alpha '" + doc.brushAlpha.name + "'" + buf + note};
                  }});
    std::vector<ParamSpec> vdmParams = common;
    vdmParams.push_back({"name", T::String, "Name of the new VDM (default 'Captured VDM').", false});
    vdmParams.push_back({"save", T::Bool, "Keep it in the brush library (default true).", false});
    vdmParams.push_back({"exr", T::String, "Also write it as a 32-bit OpenEXR here.", false});
    registry.add({"capture_vdm",
                  "Capture the surface in a square seen along `view` as a vector displacement brush (VDM from "
                  "viewport): every point's 3D offset from a smoothed base surface, overhangs included. It becomes the "
                  "VDM brush's (and Detail Clone's) map.",
                  vdmParams,
                  [](Document& doc, const Args& a) -> CommandResult {
                      CaptureFrame f;
                      std::string err;
                      if (!frameFromArgs(doc, a, f, err)) return {false, err};
                      const auto t0 = std::chrono::steady_clock::now();
                      VectorDisplacement v;
                      CaptureInfo info;
                      if (!captureVdmFromView(doc.mesh, doc.bvh, f, optionsFromArgs(a), v, err, &info)) return {false, err};
                      v.name = argString(a, "name", "Captured VDM");
                      if (v.name.empty()) v.name = "Captured VDM";
                      if (builtinVdm(v.name)) v.name += " (mine)";
                      std::string note;
                      if (argBool(a, "save", true)) {
                          const std::string dir = brushLibraryPaths().userDir;
                          if (dir.empty()) {
                              note = " (not saved: no brush library)";
                          } else {
                              std::error_code ec;
                              std::filesystem::create_directories(fsPath(dir), ec);
                              const std::string file = utf8(fsPath(dir) / fsPath(vdmFileName(v.name)));
                              if (writeVdmFile(file, v, err)) v.path = file;
                              else note = " (not saved: " + err + ")";
                          }
                      }
                      if (const std::string exr = argString(a, "exr"); !exr.empty()) {
                          if (!writeVdmExr(exr, v, err)) return {false, err};
                          note += ", EXR " + exr;
                      }
                      char buf[160];
                      std::snprintf(buf, sizeof buf, " (%d px, %zu points, reach %.2f radii, %d smoothing iterations, %.0f ms)",
                                    v.width, info.vertices, info.maxOffset, info.iterations,
                                    std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
                      const std::string msg = "Captured VDM '" + v.name + "'" + buf + note;
                      addVdm(doc, std::move(v));
                      return {true, msg};
                  }});
}

} // namespace sf
