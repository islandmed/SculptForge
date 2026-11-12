// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/Splats.h"

#include "core/Parallel.h"
#include "core/SurfaceQuery.h"
#include "core/Texture.h"
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

// Points in a uniform grid (counting sort), to ask whether any lies within `radius` of a place.
class PointGrid {
public:
    PointGrid(const std::vector<glm::vec3>& points, float radius) : points_(points), cell_(std::max(radius, 1e-9f)) {
        AABB b;
        for (const glm::vec3& p : points) b.expand(p);
        if (points.empty()) return;
        origin_ = b.min;
        const glm::vec3 ext = b.extent() + glm::vec3(1e-6f);
        while (double(std::ceil(ext.x / cell_) + 1) * double(std::ceil(ext.y / cell_) + 1) * double(std::ceil(ext.z / cell_) + 1) > 32e6)
            cell_ *= 1.5f;
        dims_ = glm::ivec3(glm::floor(ext / cell_)) + 1;
        start_.assign(size_t(dims_.x) * size_t(dims_.y) * size_t(dims_.z) + 1, 0);
        for (const glm::vec3& p : points) ++start_[cellOf(p) + 1];
        for (size_t c = 0; c + 1 < start_.size(); ++c) start_[c + 1] += start_[c];
        std::vector<uint32_t> fill(start_.begin(), start_.end() - 1);
        order_.resize(points.size());
        for (size_t i = 0; i < points.size(); ++i) order_[fill[cellOf(points[i])]++] = uint32_t(i);
    }
    bool anyWithin(const glm::vec3& p, float radius) const {
        if (points_.empty()) return false;
        const glm::ivec3 lo = glm::max(glm::ivec3(glm::floor((p - radius - origin_) / cell_)), glm::ivec3(0));
        const glm::ivec3 hi = glm::min(glm::ivec3(glm::floor((p + radius - origin_) / cell_)), dims_ - 1);
        const float r2 = radius * radius;
        for (int z = lo.z; z <= hi.z; ++z)
            for (int y = lo.y; y <= hi.y; ++y)
                for (int x = lo.x; x <= hi.x; ++x) {
                    const size_t c = (size_t(z) * size_t(dims_.y) + size_t(y)) * size_t(dims_.x) + size_t(x);
                    for (uint32_t k = start_[c]; k < start_[c + 1]; ++k) {
                        const glm::vec3 d = points_[order_[k]] - p;
                        if (glm::dot(d, d) <= r2) return true;
                    }
                }
        return false;
    }

private:
    size_t cellOf(const glm::vec3& p) const {
        const glm::ivec3 c = glm::clamp(glm::ivec3((p - origin_) / cell_), glm::ivec3(0), dims_ - 1);
        return (size_t(c.z) * size_t(dims_.y) + size_t(c.y)) * size_t(dims_.x) + size_t(c.x);
    }
    const std::vector<glm::vec3>& points_;
    float cell_;
    glm::vec3 origin_{0.0f};
    glm::ivec3 dims_{1};
    std::vector<uint32_t> start_, order_;
};

float quantile(std::vector<float>& v, float q) {
    if (v.empty()) return 0.0f;
    const size_t at = std::min(v.size() - 1, size_t(q * float(v.size())));
    std::nth_element(v.begin(), v.begin() + std::ptrdiff_t(at), v.end());
    return v[at];
}
float median(std::vector<float>& v) { return quantile(v, 0.5f); }

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

bool SplatCloud::bakeTransform() {
    const float s = transformScale();
    const glm::quat r = transformRotation();
    for (size_t i = 0; i < size(); ++i) {
        positions[i] = worldPosition(i);
        rotations[i] = glm::normalize(r * rotations[i]);
        scales[i] *= s;
    }
    transform = glm::mat4(1.0f);
    const bool rotated = std::abs(std::abs(r.w) - 1.0f) > 1e-6f;
    if (rotated && shDegree > 0) {
        std::vector<glm::vec3> dc(size());
        for (size_t i = 0; i < size(); ++i) dc[i] = sh[i * size_t(shCount())];
        sh.swap(dc);
        shDegree = 0;
        return false;
    }
    return true;
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

SplatCloud meshToSplats(const Mesh& mesh, const MeshToSplatsOptions& o, MeshToSplatsStats* statsOut) {
    SplatCloud cloud;
    cloud.name = "Sculpt";
    MeshToSplatsStats st;
    const SplatCloud* ref = o.reference && !o.reference->empty() ? o.reference : nullptr;
    const auto& P = mesh.positions;
    if (mesh.triangleCount() == 0 || (o.count == 0 && !ref)) return cloud;

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
    // Colour: the texture or vertex paint when the mesh has any; else Soft Clay with baked shading.
    const bool textured = mesh.paintToTexture && mesh.texture && mesh.hasUvs();
    const bool painted = textured || mesh.hasColors();
    const bool bake = !painted && o.shading;
    std::vector<float> shade(P.size(), 1.0f);
    for (size_t v = 0; v < P.size(); ++v) {
        const float len = glm::length(normals[v]);
        normals[v] = len > 0.0f ? normals[v] / len : glm::vec3(0, 1, 0);
        if (!bake || ringCount[v] == 0.0f) continue;
        const glm::vec3 avg = ring[v] / ringCount[v];
        const float edge = std::max(edgeLen[v] / ringCount[v], 1e-12f);
        const float k = glm::dot(normals[v], avg - P[v]) / edge; // > 0 in creases
        const float cavity = std::clamp(1.0f - 3.0f * k, 0.45f, 1.1f);
        const float sky = 0.5f + 0.5f * normals[v].y;
        const float key = std::max(0.0f, glm::dot(normals[v], glm::normalize(glm::vec3(0.4f, 0.8f, 0.45f))));
        shade[v] = cavity * (0.55f + 0.3f * sky + 0.25f * key);
    }

    // The capture's splats that already sit on the surface stay as they are.
    std::vector<uint32_t> keep;
    std::vector<glm::vec3> keptWorld;
    size_t target = o.count;
    TriangleBVH baseBvh;     // the scan the sculpt started from, when given
    bool hasBase = false;
    float edited = 0.0f;     // how far the sculpt may lie from it and still count as untouched
    if (ref) {
        const float rs = ref->transformScale();
        const glm::quat rq = ref->transformRotation();
        std::vector<float> sizes;
        const size_t stride = std::max<size_t>(1, ref->size() / 20000);
        for (size_t i = 0; i < ref->size(); i += stride)
            if (ref->opacities[i] >= o.minOpacity)
                sizes.push_back(std::max({ref->scales[i].x, ref->scales[i].y, ref->scales[i].z}) * rs);
        const float splatSize = median(sizes);
        TriangleBVH bvh;
        bvh.build(mesh);
        // With the scan the sculpt started from (Make sculptable's), "untouched" is measured against it: a splat stays
        // while the sculpt still follows that scan under it, however far the rebuilt surface sits from the splat (thin
        // detail it smoothed away stays too). Without it, a splat stays when it lies on the sculpt's own surface.
        hasBase = o.baseline && o.baseline->triangleCount() > 0;
        if (hasBase) baseBvh.build(*o.baseline);
        const TriangleBVH& surf = hasBase ? baseBvh : bvh;
        std::vector<float> off;
        for (size_t i = 0; i < ref->size(); i += stride) {
            SurfaceHit hit;
            if (ref->opacities[i] >= o.minOpacity && surf.closestPoint(ref->worldPosition(i), 8.0f * splatSize, hit))
                off.push_back(hit.distance);
        }
        // The rebuilt surface (voxels, smoothing, remeshing) sits a little off the splats even where nobody touched it:
        // measure how far (the median; an edit is a small part of the surface) and allow a margin over that.
        st.tolerance = o.tolerance;
        if (!(st.tolerance > 0.0f)) {
            const float typical = median(off);
            st.tolerance = hasBase ? std::max(2.5f * splatSize, 4.0f * typical) : std::max(1.5f * splatSize, 2.5f * typical);
        }
        // Edited = the sculpt has left the scan by more than it does where nobody worked.
        if (hasBase) {
            std::vector<float> dev;
            for (size_t i = 0; i < ref->size(); i += stride) {
                SurfaceHit hit, back;
                if (ref->opacities[i] >= o.minOpacity && surf.closestPoint(ref->worldPosition(i), st.tolerance, hit))
                    dev.push_back(bvh.closestPoint(hit.position, 8.0f * st.tolerance, back) ? back.distance : 8.0f * st.tolerance);
            }
            // (the 90th percentile: where nobody worked the sculpt follows the scan within the remesher's error)
            edited = std::max(2.0f * quantile(dev, 0.9f), 2.0f * splatSize);
        }
        std::vector<uint8_t> on(ref->size(), 0);
        std::vector<glm::vec3> onSurface(ref->size());
        parallelFor(ref->size(), [&](size_t b, size_t e) {
            for (size_t i = b; i < e; ++i) {
                if (!(ref->opacities[i] >= o.minOpacity)) continue;
                SurfaceHit hit;
                const glm::vec3 p = ref->worldPosition(i);
                if (!surf.closestPoint(p, st.tolerance, hit)) continue;
                if (hasBase) {
                    SurfaceHit now;
                    if (!bvh.closestPoint(hit.position, edited, now)) continue; // the sculpt moved this part of the scan
                }
                // A flat splat lies along the surface; a round one (a blob) says nothing about direction.
                glm::vec3 s = ref->scales[i];
                const int thin = s.x <= s.y && s.x <= s.z ? 0 : s.y <= s.z ? 1 : 2;
                const float thinS = s[thin];
                s[thin] = 1e30f;
                const float otherMin = std::min({s.x, s.y, s.z});
                if (thinS < 0.5f * otherMin) {
                    const glm::vec3 axis = glm::mat3_cast(glm::normalize(rq * ref->rotations[i]))[thin];
                    if (std::abs(glm::dot(axis, surf.faceNormal(hit.prim))) < 0.5f) continue;
                }
                on[i] = 1;
                onSurface[i] = hit.position;
            }
        }, 4096);
        for (size_t i = 0; i < on.size(); ++i)
            if (on[i]) keep.push_back(uint32_t(i));
        keptWorld.reserve(keep.size());
        // Where each sits on the surface: the rebuilt surface lies a little off the splats, but a new splat must
        // only be skipped where a kept one covers that spot of the surface.
        for (uint32_t i : keep) keptWorld.push_back(onSurface[i]);
        if (target == 0) {
            // As many as the capture's body has: its splats inside the surface's bounds (not the floaters around it).
            AABB box = mesh.bounds();
            box.min -= glm::vec3(st.tolerance);
            box.max += glm::vec3(st.tolerance);
            for (size_t i = 0; i < ref->size(); ++i) {
                const glm::vec3 p = ref->worldPosition(i);
                if (ref->opacities[i] >= o.minOpacity && !glm::any(glm::lessThan(p, box.min)) &&
                    !glm::any(glm::greaterThan(p, box.max)))
                    ++target;
            }
        }
        st.kept = keep.size();
    }
    target = std::max<size_t>(target, 1);

    // New splats where the surface has none: sample at the target density, skip what a kept splat already covers.
    const float spacing = float(std::sqrt(total / double(target)));
    const PointGrid covered(keptWorld, 1.2f * spacing);
    const glm::mat4 toLocal = ref ? glm::inverse(ref->transform) : glm::mat4(1.0f);
    const float invScale = ref ? 1.0f / std::max(ref->transformScale(), 1e-12f) : 1.0f;
    const glm::quat invRot = ref ? glm::conjugate(ref->transformRotation()) : glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    struct Fresh {
        glm::vec3 position, color;
        glm::quat rotation;
    };
    std::vector<Fresh> fresh;
    fresh.reserve(target);
    std::mt19937 rng(12345);
    std::uniform_real_distribution<double> pick(0.0, total);
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    for (size_t i = 0; i < target; ++i) {
        const size_t tri = std::min(size_t(std::upper_bound(cumulative.begin(), cumulative.end(), pick(rng)) - cumulative.begin()),
                                    mesh.triangleCount() - 1);
        const auto& t = mesh.triangles[tri];
        float a = unit(rng), b = unit(rng);
        if (a + b > 1.0f) {
            a = 1.0f - a;
            b = 1.0f - b;
        }
        const float c = 1.0f - a - b;
        const glm::vec3 p = c * P[t.x] + a * P[t.y] + b * P[t.z];
        if (covered.anyWithin(p, 1.2f * spacing)) continue;
        // Where the sculpt still follows the scan, the capture is the truth, its gaps included: nothing new.
        if (hasBase) {
            SurfaceHit near;
            if (baseBvh.closestPoint(p, edited, near)) continue;
        }
        const glm::vec3 n = glm::normalize(c * normals[t.x] + a * normals[t.y] + b * normals[t.z] + glm::vec3(1e-9f));
        glm::vec3 col;
        if (textured) {
            col = linearToSrgb(sampleTextureAt(mesh, uint32_t(tri), p));
            ++st.painted;
        } else if (painted) {
            col = linearToSrgb(c * mesh.colorAt(t.x) + a * mesh.colorAt(t.y) + b * mesh.colorAt(t.z));
            ++st.painted;
        } else {
            col = o.color * (c * shade[t.x] + a * shade[t.y] + b * shade[t.z]);
            ++st.clay;
        }
        fresh.push_back({p, glm::clamp(col, glm::vec3(0.0f), glm::vec3(1.0f)), rotationFromZ(n)});
    }

    const size_t k = ref ? size_t(ref->shCount()) : 1;
    cloud.resize(keep.size() + fresh.size(), ref ? ref->shDegree : 0);
    if (ref) {
        cloud.transform = ref->transform;
        for (size_t j = 0; j < keep.size(); ++j) {
            const size_t i = keep[j];
            cloud.positions[j] = ref->positions[i];
            cloud.rotations[j] = ref->rotations[i];
            cloud.scales[j] = ref->scales[i];
            cloud.opacities[j] = ref->opacities[i];
            for (size_t q = 0; q < k; ++q) cloud.sh[j * k + q] = ref->sh[i * k + q];
        }
    }
    for (size_t f = 0; f < fresh.size(); ++f) {
        const size_t j = keep.size() + f;
        cloud.positions[j] = glm::vec3(toLocal * glm::vec4(fresh[f].position, 1.0f));
        cloud.rotations[j] = glm::normalize(invRot * fresh[f].rotation);
        cloud.scales[j] = glm::vec3(spacing * 0.6f, spacing * 0.6f, spacing * 0.08f) * invScale;
        cloud.opacities[j] = 0.95f;
        cloud.setColor(j, fresh[f].color);
    }
    if (statsOut) *statsOut = st;
    return cloud;
}

} // namespace sf
