// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/Splats.h"

#include "core/Voxel.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <random>

namespace sf {

namespace {

glm::quat rotationFromZ(const glm::vec3& n) {
    const glm::vec3 z(0.0f, 0.0f, 1.0f);
    const float d = glm::dot(z, n);
    if (d < -0.999999f) return glm::quat(0.0f, 1.0f, 0.0f, 0.0f); // 180 degrees about X
    const glm::vec3 c = glm::cross(z, n);
    return glm::normalize(glm::quat(1.0f + d, c.x, c.y, c.z));
}

glm::mat4 eulerXYZ(const glm::vec3& degrees) {
    glm::mat4 r(1.0f);
    r = glm::rotate(r, glm::radians(degrees.z), glm::vec3(0, 0, 1));
    r = glm::rotate(r, glm::radians(degrees.y), glm::vec3(0, 1, 0));
    r = glm::rotate(r, glm::radians(degrees.x), glm::vec3(1, 0, 0));
    return r;
}

} // namespace

void SplatCloud::resize(size_t n, int degree) {
    shDegree = degree;
    positions.resize(n, glm::vec3(0.0f));
    rotations.resize(n, glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
    scales.resize(n, glm::vec3(0.01f));
    opacities.resize(n, 1.0f);
    sh.resize(n * size_t(shCount()), glm::vec3(0.0f));
}

AABB SplatCloud::bounds(float keepFraction) const {
    AABB b;
    if (empty()) return b;
    if (keepFraction >= 1.0f) {
        for (size_t i = 0; i < size(); ++i) b.expand(worldPosition(i));
        return b;
    }
    std::vector<float> axis(size());
    const float tail = std::clamp((1.0f - keepFraction) * 0.5f, 0.0f, 0.49f);
    for (int a = 0; a < 3; ++a) {
        for (size_t i = 0; i < size(); ++i) axis[i] = worldPosition(i)[a];
        const size_t lo = size_t(tail * float(size() - 1));
        const size_t hi = size() - 1 - lo;
        std::nth_element(axis.begin(), axis.begin() + std::ptrdiff_t(lo), axis.end());
        b.min[a] = axis[lo];
        std::nth_element(axis.begin(), axis.begin() + std::ptrdiff_t(hi), axis.end());
        b.max[a] = axis[hi];
    }
    return b;
}

std::string SplatCloud::validate() const {
    const size_t n = size();
    if (rotations.size() != n || scales.size() != n || opacities.size() != n)
        return "Splat attribute arrays have different lengths";
    if (shDegree < 0 || shDegree > 3) return "Unsupported spherical-harmonics degree " + std::to_string(shDegree);
    if (sh.size() != n * size_t(shCount())) return "Spherical-harmonics data does not match the splat count";
    return {};
}

float SplatCloud::transformScale() const { return glm::length(glm::vec3(transform[0])); }

glm::quat SplatCloud::transformRotation() const {
    const float s = transformScale();
    return glm::normalize(glm::quat_cast(glm::mat3(transform) / (s > 0.0f ? s : 1.0f)));
}

void SplatCloud::bakeTransform() {
    const float s = transformScale();
    const glm::quat r = transformRotation();
    for (size_t i = 0; i < size(); ++i) {
        positions[i] = worldPosition(i);
        rotations[i] = glm::normalize(r * rotations[i]);
        scales[i] *= s;
    }
    transform = glm::mat4(1.0f);
    if (shDegree > 0 && std::abs(std::abs(r.w) - 1.0f) > 1e-7f) {
        const ShRotation turn(glm::mat3_cast(r), shDegree);
        const size_t k = size_t(shCount());
        for (size_t i = 0; i < size(); ++i) turn.apply(&sh[i * k]);
    }
}

namespace {

// The basis functions of band l (1-3) at unit direction d, in the 3DGS coefficient order.
void shBand(int l, const glm::dvec3& d, double* y) {
    const double x = d.x, yy = d.y, z = d.z;
    const double xx = x * x, y2 = yy * yy, zz = z * z;
    if (l == 1) {
        const double c = 0.4886025119029199;
        y[0] = -c * yy, y[1] = c * z, y[2] = -c * x;
    } else if (l == 2) {
        y[0] = 1.0925484305920792 * x * yy;
        y[1] = -1.0925484305920792 * yy * z;
        y[2] = 0.31539156525252005 * (2.0 * zz - xx - y2);
        y[3] = -1.0925484305920792 * x * z;
        y[4] = 0.5462742152960396 * (xx - y2);
    } else {
        y[0] = -0.5900435899266435 * yy * (3.0 * xx - y2);
        y[1] = 2.890611442640554 * x * yy * z;
        y[2] = -0.4570457994644658 * yy * (4.0 * zz - xx - y2);
        y[3] = 0.3731763325901154 * z * (2.0 * zz - 3.0 * xx - 3.0 * y2);
        y[4] = -0.4570457994644658 * x * (4.0 * zz - xx - y2);
        y[5] = 1.445305721320277 * z * (xx - y2);
        y[6] = -0.5900435899266435 * x * (xx - 3.0 * y2);
    }
}

constexpr int kShSamples = 32;

// Least squares on kShSamples directions: P = (Y^T Y)^-1 Y^T, Y[k][j] = band function j at direction k. A turned
// band-l function is again a band-l function, so P times the turned samples is its exact matrix.
struct ShFit {
    glm::dvec3 dirs[kShSamples];
    double p[3][7][kShSamples];
    ShFit() {
        for (int k = 0; k < kShSamples; ++k) { // a Fibonacci sphere: well spread, never degenerate
            const double z = 1.0 - (2.0 * k + 1.0) / kShSamples, r = std::sqrt(1.0 - z * z);
            const double a = 2.399963229728653 * k;
            dirs[k] = glm::dvec3(r * std::cos(a), r * std::sin(a), z);
        }
        for (int l = 1; l <= 3; ++l) {
            const int n = 2 * l + 1;
            double y[kShSamples][7], g[7][14] = {};
            for (int k = 0; k < kShSamples; ++k) shBand(l, dirs[k], y[k]);
            for (int i = 0; i < n; ++i) {
                for (int j = 0; j < n; ++j)
                    for (int k = 0; k < kShSamples; ++k) g[i][j] += y[k][i] * y[k][j];
                g[i][n + i] = 1.0;
            }
            for (int c = 0; c < n; ++c) { // Gauss-Jordan with partial pivoting
                int best = c;
                for (int r = c + 1; r < n; ++r)
                    if (std::abs(g[r][c]) > std::abs(g[best][c])) best = r;
                for (int j = 0; j < 2 * n; ++j) std::swap(g[c][j], g[best][j]);
                const double inv = 1.0 / g[c][c];
                for (int j = 0; j < 2 * n; ++j) g[c][j] *= inv;
                for (int r = 0; r < n; ++r)
                    if (r != c && g[r][c] != 0.0) {
                        const double f = g[r][c];
                        for (int j = 0; j < 2 * n; ++j) g[r][j] -= f * g[c][j];
                    }
            }
            for (int i = 0; i < n; ++i)
                for (int k = 0; k < kShSamples; ++k) {
                    double v = 0.0;
                    for (int j = 0; j < n; ++j) v += g[i][n + j] * y[k][j];
                    p[l - 1][i][k] = v;
                }
        }
    }
};

} // namespace

glm::vec3 evalSH(const glm::vec3* coeffs, int degree, const glm::vec3& dir) {
    glm::vec3 c = kShC0 * coeffs[0];
    double y[7];
    for (int l = 1, at = 1; l <= degree; at += 2 * l + 1, ++l) {
        shBand(l, glm::dvec3(dir), y);
        for (int j = 0; j < 2 * l + 1; ++j) c += float(y[j]) * coeffs[at + j];
    }
    return c + glm::vec3(0.5f);
}

ShRotation::ShRotation(const glm::mat3& rotation, int degree) : degree_(std::clamp(degree, 0, 3)), band_{} {
    static const ShFit fit;
    // The turned colour at direction d is the old colour at R^T d.
    const glm::dmat3 back = glm::transpose(glm::dmat3(rotation));
    for (int l = 1; l <= degree_; ++l) {
        const int n = 2 * l + 1;
        double turned[kShSamples][7];
        for (int k = 0; k < kShSamples; ++k) shBand(l, back * fit.dirs[k], turned[k]);
        for (int i = 0; i < n; ++i)
            for (int j = 0; j < n; ++j) {
                double v = 0.0;
                for (int k = 0; k < kShSamples; ++k) v += fit.p[l - 1][i][k] * turned[k][j];
                band_[l - 1][i * n + j] = float(v);
            }
    }
}

void ShRotation::apply(glm::vec3* coeffs) const {
    glm::vec3 in[7];
    for (int l = 1, at = 1; l <= degree_; at += 2 * l + 1, ++l) {
        const int n = 2 * l + 1;
        std::copy(coeffs + at, coeffs + at + n, in);
        const float* m = band_[l - 1];
        for (int i = 0; i < n; ++i) {
            glm::vec3 v(0.0f);
            for (int j = 0; j < n; ++j) v += m[i * n + j] * in[j];
            coeffs[at + i] = v;
        }
    }
}

size_t cleanSplats(SplatCloud& cloud, float minOpacity, float keepFraction) {
    AABB keep = cloud.bounds(keepFraction);
    if (keepFraction < 1.0f) {
        const glm::vec3 margin = keep.extent() * 0.02f;
        keep.min -= margin;
        keep.max += margin;
    }
    return cloud.removeIf([&](size_t i) {
        if (cloud.opacities[i] < minOpacity) return true;
        const glm::vec3 p = cloud.worldPosition(i);
        return glm::any(glm::lessThan(p, keep.min)) || glm::any(glm::greaterThan(p, keep.max));
    });
}

void rotateSplats(SplatCloud& cloud, const glm::vec3& degrees) {
    const glm::vec3 c = cloud.bounds(0.9f).center();
    cloud.transform = glm::translate(glm::mat4(1.0f), c) * eulerXYZ(degrees) * glm::translate(glm::mat4(1.0f), -c) *
                      cloud.transform;
}

void fitSplats(SplatCloud& cloud) {
    if (cloud.empty()) return;
    const AABB b = cloud.bounds(0.9f);
    const glm::vec3 e = b.extent();
    const float radius = std::max({e.x, e.y, e.z, 1e-6f}) * 0.5f;
    cloud.transform = glm::scale(glm::mat4(1.0f), glm::vec3(1.0f / radius)) * glm::translate(glm::mat4(1.0f), -b.center()) *
                      cloud.transform;
}

Mesh splatsToMesh(const SplatCloud& cloud, const SplatToMeshOptions& o, std::string* error) {
    auto fail = [&](const std::string& msg) {
        if (error) *error = msg;
        return Mesh();
    };
    if (cloud.empty()) return fail("There are no splats to convert");
    AABB box = cloud.bounds(std::clamp(o.keepFraction, 0.5f, 1.0f));
    const float boxSize = std::max({box.extent().x, box.extent().y, box.extent().z});
    if (!(boxSize > 0.0f)) return fail("The splats have no extent");
    box.min -= box.extent() * 0.02f;
    box.max += box.extent() * 0.02f;

    VoxelGrid grid;
    const int closeRadius = std::clamp(o.closeRadius, 0, 8);
    if (!makeVoxelGrid(box, std::clamp(o.resolution, 16, 512), closeRadius + 4, grid))
        return fail("Resolution too high");

    // Splat each Gaussian's opacity into the grid. Variance is widened by half
    // a voxel so thin splats still register.
    const float s = cloud.transformScale();
    const glm::quat r = cloud.transformRotation();
    const float widen = 0.25f * grid.cell * grid.cell;
    const float maxReach = 6.0f * grid.cell;
    size_t used = 0;
    for (size_t i = 0; i < cloud.size(); ++i) {
        const float alpha = cloud.opacities[i];
        if (alpha < o.minOpacity) continue;
        const glm::vec3 p = cloud.worldPosition(i);
        if (glm::any(glm::lessThan(p, box.min)) || glm::any(glm::greaterThan(p, box.max))) continue;
        const glm::vec3 sigma = cloud.scales[i] * s;
        if (std::max({sigma.x, sigma.y, sigma.z}) > 0.25f * boxSize) continue; // background blobs
        const glm::mat3 R = glm::mat3_cast(glm::normalize(r * cloud.rotations[i]));
        glm::mat3 cov = R * glm::mat3(glm::vec3(sigma.x * sigma.x, 0, 0), glm::vec3(0, sigma.y * sigma.y, 0),
                                      glm::vec3(0, 0, sigma.z * sigma.z)) *
                        glm::transpose(R);
        cov[0][0] += widen;
        cov[1][1] += widen;
        cov[2][2] += widen;
        const glm::mat3 inv = glm::inverse(cov);
        const glm::vec3 reach = glm::min(3.0f * glm::sqrt(glm::vec3(cov[0][0], cov[1][1], cov[2][2])), glm::vec3(maxReach));
        const glm::ivec3 lo = glm::max(glm::ivec3(glm::floor((p - reach - grid.origin) / grid.cell)), glm::ivec3(0));
        const glm::ivec3 hi = glm::min(glm::ivec3(glm::ceil((p + reach - grid.origin) / grid.cell)), grid.dims - 1);
        for (int z = lo.z; z <= hi.z; ++z)
            for (int y = lo.y; y <= hi.y; ++y)
                for (int x = lo.x; x <= hi.x; ++x) {
                    const glm::vec3 d = grid.position(x, y, z) - p;
                    const float q = glm::dot(d, inv * d);
                    if (q < 9.0f) grid.values[grid.index(x, y, z)] += alpha * std::exp(-0.5f * q);
                }
        ++used;
    }
    if (used == 0) return fail("No splats are opaque enough - lower the minimum opacity");

    std::vector<uint8_t> solid = solidifyShell(grid, 0.3f, closeRadius, false);
    Mesh m = meshFromSolid(grid, solid, std::clamp(o.smoothIterations, 0, 50));
    if (m.triangleCount() == 0) return fail("No closed surface was found - try a larger close radius");
    return m;
}

SplatCloud meshToSplats(const Mesh& mesh, const MeshToSplatsOptions& o) {
    SplatCloud cloud;
    cloud.name = "Sculpt";
    const auto& P = mesh.positions;
    if (mesh.triangleCount() == 0 || o.count == 0) return cloud;

    // Per-vertex normals and a cavity estimate (how far the neighbour average
    // sits above the surface) from the triangle list alone.
    std::vector<glm::vec3> normals(P.size(), glm::vec3(0.0f)), ring(P.size(), glm::vec3(0.0f));
    std::vector<float> ringCount(P.size(), 0.0f), edgeLen(P.size(), 0.0f);
    std::vector<double> cumulative;
    cumulative.reserve(mesh.triangleCount());
    double total = 0.0;
    for (const auto& t : mesh.triangles) {
        const glm::vec3 n = glm::cross(P[t.y] - P[t.x], P[t.z] - P[t.x]);
        const uint32_t v[3] = {t.x, t.y, t.z};
        for (int k = 0; k < 3; ++k) {
            const uint32_t a = v[k], b = v[(k + 1) % 3], c = v[(k + 2) % 3];
            normals[a] += n;
            ring[a] += P[b] + P[c];
            ringCount[a] += 2.0f;
            edgeLen[a] += glm::distance(P[a], P[b]) + glm::distance(P[a], P[c]);
        }
        total += 0.5 * double(glm::length(n));
        cumulative.push_back(total);
    }
    std::vector<float> shade(P.size(), 1.0f);
    for (size_t v = 0; v < P.size(); ++v) {
        const float len = glm::length(normals[v]);
        normals[v] = len > 0.0f ? normals[v] / len : glm::vec3(0, 1, 0);
        if (!o.shading || ringCount[v] == 0.0f) continue;
        const glm::vec3 avg = ring[v] / ringCount[v];
        const float edge = std::max(edgeLen[v] / ringCount[v], 1e-12f);
        const float k = glm::dot(normals[v], avg - P[v]) / edge; // > 0 in creases
        const float cavity = std::clamp(1.0f - 3.0f * k, 0.45f, 1.1f);
        const float sky = 0.5f + 0.5f * normals[v].y;
        const float key = std::max(0.0f, glm::dot(normals[v], glm::normalize(glm::vec3(0.4f, 0.8f, 0.45f))));
        shade[v] = cavity * (0.55f + 0.3f * sky + 0.25f * key);
    }

    const float spacing = float(std::sqrt(total / double(o.count)));
    cloud.resize(o.count, 0);
    std::mt19937 rng(12345);
    std::uniform_real_distribution<double> pick(0.0, total);
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    for (size_t i = 0; i < o.count; ++i) {
        const size_t tri = size_t(std::upper_bound(cumulative.begin(), cumulative.end(), pick(rng)) - cumulative.begin());
        const auto& t = mesh.triangles[std::min(tri, mesh.triangleCount() - 1)];
        float a = unit(rng), b = unit(rng);
        if (a + b > 1.0f) {
            a = 1.0f - a;
            b = 1.0f - b;
        }
        const float c = 1.0f - a - b;
        const glm::vec3 n = glm::normalize(c * normals[t.x] + a * normals[t.y] + b * normals[t.z] + glm::vec3(1e-9f));
        cloud.positions[i] = c * P[t.x] + a * P[t.y] + b * P[t.z];
        cloud.rotations[i] = rotationFromZ(n);
        cloud.scales[i] = glm::vec3(spacing * 0.6f, spacing * 0.6f, spacing * 0.08f);
        cloud.opacities[i] = 0.95f;
        const float lit = c * shade[t.x] + a * shade[t.y] + b * shade[t.z];
        cloud.setColor(i, glm::clamp(o.color * lit, glm::vec3(0.0f), glm::vec3(1.0f)));
    }
    return cloud;
}

} // namespace sf
