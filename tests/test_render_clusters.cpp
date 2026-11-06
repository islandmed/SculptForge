// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The renderer's CPU side without a GPU: the cluster layout (core/UploadOrder.h), the packed vertex format, and
// cluster culling (render/Clusters.h) - culled clusters really face away, closed meshes are told apart from
// open or inconsistently wound ones, and the layout is the same with multithreading off.
#include "TestHarness.h"

#include "core/Benchmark.h"
#include "core/Primitives.h"
#include "core/ThreadPool.h"
#include "core/UploadOrder.h"
#include "render/Clusters.h"

#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <map>
#include <random>

using namespace sf;

namespace {

// Every corner of every cluster's triangles maps to a slot in the cluster's range holding that vertex.
bool layoutHolds(const Mesh& m, const UploadOrder& order) {
    std::vector<uint16_t> local;
    const auto clusters = order.clusters();
    if (clusters.size() != (m.triangleCount() + UploadOrder::kClusterTriangles - 1) / UploadOrder::kClusterTriangles) return false;
    // Every mesh triangle exactly once, clusters back to back in the clusters' triangle order.
    std::vector<uint8_t> seen(m.triangleCount(), 0);
    size_t next = 0;
    for (uint32_t c = 0; c < clusters.size(); ++c) {
        if (clusters[c].firstTriangle != next) return false;
        next += clusters[c].triangleCount;
        for (uint32_t i = 0; i < clusters[c].triangleCount; ++i) {
            const uint32_t t = order.triangle(c, i);
            if (t >= m.triangleCount() || seen[t]) return false;
            seen[t] = 1;
        }
    }
    if (next != m.triangleCount()) return false;
    for (uint32_t c = 0; c < clusters.size(); ++c) {
        const UploadOrder::Cluster& cl = clusters[c];
        if (cl.firstVertex % UploadOrder::kBlock != 0 || cl.vertexCount > 65535) return false;
        if (c + 1 < clusters.size() && cl.firstVertex + cl.vertexCount > clusters[c + 1].firstVertex) return false;
        const uint32_t n = order.clusterTriangles(c);
        local.resize(size_t(n) * 3);
        order.localTriangles(m, c, local.data());
        for (uint32_t i = 0; i < n; ++i)
            for (int k = 0; k < 3; ++k) {
                const uint32_t l = local[size_t(i) * 3 + size_t(k)];
                if (l >= cl.vertexCount) return false;
                if (order.fromGpu()[cl.firstVertex + l] != m.triangles[order.triangle(c, i)][k]) return false;
                if (order.clusterOfSlot(cl.firstVertex + l) != c) return false;
            }
    }
    return true;
}

} // namespace

TEST(cluster_layout_maps_every_corner) {
    Mesh m = makeBenchSphere(100000); // several clusters, divided-mesh numbering
    UploadOrder order;
    order.build(m);
    CHECK(order.clusters().size() > 10);
    CHECK(layoutHolds(m, order));
    // slotsOf lists exactly the slots holding v, the first being toGpu(v).
    std::vector<uint32_t> count(m.vertexCount(), 0);
    for (uint32_t c = 0; c < order.clusters().size(); ++c) {
        const auto& cl = order.clusters()[c];
        for (uint32_t i = cl.firstVertex; i < cl.firstVertex + cl.vertexCount; ++i) ++count[order.fromGpu()[i]];
    }
    bool slotsOk = true;
    size_t copies = 0;
    std::vector<uint32_t> slots;
    for (uint32_t v = 0; v < m.vertexCount(); ++v) {
        order.slotsOf(v, slots);
        slotsOk = slotsOk && slots.size() == count[v] && !slots.empty() && slots[0] == order.toGpu(v);
        for (size_t i = 0; i < slots.size(); ++i) slotsOk = slotsOk && order.fromGpu()[slots[i]] == v && (i == 0 || slots[i - 1] < slots[i]);
        copies += slots.size() - 1;
    }
    CHECK(slotsOk);
    // Clusters of a divided mesh are compact patches: few vertices are copied onto seams.
    CHECK(copies * 5 < m.vertexCount());
    CHECK(order.size() < m.vertexCount() * 13 / 10);
}

TEST(cluster_layout_odd_meshes) {
    // A single cluster, an empty mesh, unused vertices, and a mesh with an out-of-range index.
    Mesh small = makeCube(2);
    UploadOrder order;
    order.build(small);
    CHECK(order.clusters().size() == 1);
    CHECK(layoutHolds(small, order));
    Mesh empty;
    order.build(empty);
    CHECK(order.size() == 0 && order.clusters().empty());
    std::vector<std::pair<uint32_t, uint32_t>> runs;
    order.dirtyRuns(empty, runs);
    CHECK(runs.empty());
    Mesh m = makeCube(4);
    m.positions.push_back(glm::vec3(5.0f)); // used by no triangle
    m.normals.push_back(glm::vec3(0.0f, 0.0f, 1.0f));
    order.build(m);
    CHECK(order.toGpu(uint32_t(m.vertexCount() - 1)) == UploadOrder::kNone);
    m.clearDirty();
    m.markDirty(uint32_t(m.vertexCount() - 1));
    order.dirtyRuns(m, runs);
    CHECK(runs.empty());
    m.triangles[3].y = 1000000; // invalid: drawn as nothing
    order.build(m);
    std::vector<uint16_t> local(size_t(order.clusterTriangles(0)) * 3);
    order.localTriangles(m, 0, local.data());
    for (uint32_t i = 0; i < order.clusterTriangles(0); ++i)
        if (order.triangle(0, i) == 3) CHECK(local[i * 3] == 0 && local[i * 3 + 1] == 0 && local[i * 3 + 2] == 0);
}

TEST(cluster_layout_same_serially) {
    const Mesh m = makeBenchSphere(60000);
    UploadOrder par, ser;
    par.build(m);
    ThreadPool::shared().setSerial(true);
    ser.build(m);
    ThreadPool::shared().setSerial(false);
    CHECK(par.size() == ser.size());
    CHECK(std::equal(par.fromGpu().begin(), par.fromGpu().end(), ser.fromGpu().begin(), ser.fromGpu().end()));
    bool same = par.clusters().size() == ser.clusters().size();
    for (size_t c = 0; same && c < par.clusters().size(); ++c)
        same = par.clusters()[c].firstVertex == ser.clusters()[c].firstVertex && par.clusters()[c].vertexCount == ser.clusters()[c].vertexCount;
    CHECK(same);
}

TEST(packed_vertex_round_trip) {
    std::mt19937 rng(7);
    std::uniform_real_distribution<float> u(-1.0f, 1.0f);
    AABB box;
    box.expand(glm::vec3(-3.0f, -1.0f, -0.5f));
    box.expand(glm::vec3(2.0f, 4.0f, 0.5f));
    const PositionGrid grid = PositionGrid::around(box);
    float worstPos = 0.0f, worstAngle = 0.0f, worstCurv = 0.0f;
    bool inside = true;
    for (int i = 0; i < 20000; ++i) {
        const glm::vec3 p = box.min + (glm::vec3(u(rng), u(rng), u(rng)) * 0.5f + 0.5f) * box.extent();
        glm::vec3 n(u(rng), u(rng), u(rng));
        if (glm::length(n) < 1e-3f) continue;
        n = glm::normalize(n);
        const float curv = u(rng) * u(rng) * 1.2f; // mostly small, some beyond the +-1 kept
        PackedVertex v;
        inside = inside && packVertex(grid, p, n, curv, v);
        worstPos = std::max(worstPos, glm::length(unpackPosition(grid, v) - p) / grid.step);
        worstAngle = std::max(worstAngle, std::acos(std::clamp(glm::dot(unpackNormal(v), n), -1.0f, 1.0f)));
        const float expected = std::clamp(curv, -kCurvatureRange, kCurvatureRange);
        worstCurv = std::max(worstCurv, std::abs(unpackCurvature(v) - expected));
    }
    std::printf("  worst position error %.3f steps, normal %.4f deg, curvature %.5f\n", worstPos, glm::degrees(worstAngle), worstCurv);
    CHECK(inside);
    CHECK(worstPos < 1.25f);                      // half a step per axis, plus float rounding
    CHECK(worstAngle < glm::radians(0.15f));      // 11-bit octahedral normals
    CHECK(worstCurv <= 2.0f / 511.0f); // 10 bits, square-root companded: within 0.004, finer near 0
    // Exact values where it matters: zero curvature, axis normals, and the same point always packs the same.
    PackedVertex a, b;
    packVertex(grid, glm::vec3(0.1f), glm::vec3(0, 0, -1), 0.0f, a);
    packVertex(grid, glm::vec3(0.1f), glm::vec3(0, 0, -1), 0.0f, b);
    CHECK(unpackCurvature(a) == 0.0f);
    float worstSmall = 0.0f; // where the cavity shading ramps
    for (float c = -0.2f; c <= 0.2f; c += 0.0007f) {
        PackedVertex q;
        packVertex(grid, glm::vec3(0.1f), glm::vec3(0, 0, 1), c, q);
        worstSmall = std::max(worstSmall, std::abs(unpackCurvature(q) - c));
    }
    CHECK(worstSmall < 0.0011f);
    CHECK(glm::length(unpackNormal(a) - glm::vec3(0, 0, -1)) < 1e-6f);
    CHECK(a.pos[0] == b.pos[0] && a.pos[1] == b.pos[1] && a.normal == b.normal);
    // Outside the grid: reported (the renderer then fits a new one) and clamped.
    CHECK(!packVertex(grid, glm::vec3(100.0f, 0.0f, 0.0f), glm::vec3(0, 0, 1), 0.0f, a));
    CHECK(!packVertex(grid, glm::vec3(std::nanf(""), 0.0f, 0.0f), glm::vec3(0, 0, 1), 0.0f, a));
    // The margin: a little outside the box still fits.
    CHECK(grid.contains(box.max + glm::vec3(0.5f)));
    // Colours: 8-bit gamma 2.2, white and black exact.
    CHECK(packColor(glm::vec3(1.0f)) == 0xFFFFFFFFu);
    CHECK(packColor(glm::vec3(0.0f)) == 0xFF000000u);
    CHECK(packMask(1.0f) == 255 && packMask(0.0f) == 0 && packMask(2.0f) == 255);
}

TEST(closed_oriented_meshes) {
    CHECK(closedOriented(makeIcosphere(3)));
    CHECK(closedOriented(makeCube(3)));
    CHECK(closedOriented(makeBenchSphere(20000)));
    CHECK(!closedOriented(makePlane(4)));
    Mesh flipped = makeIcosphere(2);
    std::swap(flipped.triangles[5].y, flipped.triangles[5].z);
    flipped.rebuildTopology();
    CHECK(!closedOriented(flipped));
    Mesh holed = makeIcosphere(2);
    holed.triangles.pop_back();
    holed.rebuildTopology();
    CHECK(!closedOriented(holed));
    CHECK(!closedOriented(Mesh()));
    // The layout's own check (made while building) agrees, unwelded seams aside.
    UploadOrder order;
    for (const Mesh& m : {makeIcosphere(3), makeCube(5), makeBenchSphere(200000)}) {
        order.build(m);
        CHECK(order.closed());
    }
    for (const Mesh& m : {makePlane(4), flipped, holed}) {
        order.build(m);
        CHECK(!order.closed());
    }
    // An unwelded seam (two vertices at one point, each with half the triangles) is still closed; a real
    // crack is not.
    Mesh seam = makeIcosphere(2);
    const uint32_t v = seam.triangles[0].x, twin = uint32_t(seam.vertexCount());
    seam.positions.push_back(seam.positions[v]);
    seam.normals.push_back(seam.normals[v]);
    int moved = 0;
    for (glm::uvec3& t : seam.triangles)
        for (int k = 0; k < 3; ++k)
            if (t[k] == v && moved < 2 && (t.x == v ? 0 : 1) + moved >= 0) {
                t[k] = twin;
                ++moved;
                break;
            }
    seam.rebuildTopology();
    CHECK(closedOriented(seam));
    seam.positions[twin] += glm::vec3(1e-3f);
    CHECK(!closedOriented(seam));
}

TEST(cluster_culling_is_conservative) {
    // A sculpted-looking sphere with about a hundred clusters: bumps so their normal cones vary.
    Mesh m = makeBenchSphere(400000);
    for (size_t v = 0; v < m.vertexCount(); ++v) {
        const glm::vec3 p = m.positions[v];
        m.positions[v] = p * (1.0f + 0.01f * std::sin(p.x * 23.0f) * std::cos(p.y * 17.0f));
    }
    m.recomputeAllNormals();
    UploadOrder order;
    order.build(m);
    std::vector<ClusterBounds> bounds(order.clusters().size());
    for (uint32_t c = 0; c < bounds.size(); ++c) bounds[c] = clusterBounds(m, order, c);
    std::mt19937 rng(3);
    std::uniform_real_distribution<float> u(-1.0f, 1.0f);
    size_t culled = 0, total = 0;
    bool sound = true, contained = true;
    for (int view = 0; view < 24; ++view) {
        glm::vec3 eye(u(rng), u(rng), u(rng));
        eye = glm::normalize(eye) * (1.6f + 3.0f * std::abs(u(rng)));
        const glm::mat4 V = glm::lookAt(eye, glm::vec3(0.0f), glm::vec3(0, 1, 0));
        const glm::mat4 P = glm::perspective(glm::radians(35.0f), 1.5f, 0.04f, 100.0f);
        CullView cv = CullView::from(V, P);
        cv.backfaces = true;
        contained = contained && glm::length(cv.eye - eye) < 1e-4f && std::abs(cv.nearDistance - 0.04f) < 1e-4f;
        for (uint32_t c = 0; c < bounds.size(); ++c) {
            ++total;
            if (!facingAway(cv, bounds[c])) continue;
            ++culled;
            // Every triangle of a culled cluster faces away from the eye at all three corners.
            for (uint32_t i = 0; i < order.clusterTriangles(c); ++i) {
                const glm::uvec3 tri = m.triangles[order.triangle(c, i)];
                const glm::vec3 n = glm::cross(m.positions[tri.y] - m.positions[tri.x], m.positions[tri.z] - m.positions[tri.x]);
                for (int k = 0; k < 3; ++k) sound = sound && glm::dot(n, m.positions[tri[k]] - eye) > 0.0f;
            }
        }
    }
    std::printf("  culled %zu of %zu cluster views\n", culled, total);
    CHECK(contained);
    CHECK(sound);
    CHECK(culled * 4 > total); // a good part of the back half goes
    // Bounds hold every vertex of their cluster.
    bool inSphere = true;
    for (uint32_t c = 0; c < bounds.size(); ++c)
        for (uint32_t i = 0; i < order.clusterTriangles(c); ++i)
            for (int k = 0; k < 3; ++k)
                inSphere = inSphere && glm::distance(m.positions[m.triangles[order.triangle(c, i)][k]], bounds[c].center) <= bounds[c].radius;
    CHECK(inSphere);
    // Off by default, and never with the eye inside the sphere's bounds.
    CullView off = CullView::from(glm::lookAt(glm::vec3(0, 0, 3), glm::vec3(0.0f), glm::vec3(0, 1, 0)),
                                  glm::perspective(glm::radians(35.0f), 1.0f, 0.03f, 100.0f));
    bool none = true;
    for (const ClusterBounds& b : bounds) none = none && !facingAway(off, b);
    CHECK(none);
}

TEST(cluster_frustum_culling) {
    const glm::mat4 V = glm::lookAt(glm::vec3(0, 0, 5), glm::vec3(0.0f), glm::vec3(0, 1, 0));
    const glm::mat4 P = glm::perspective(glm::radians(35.0f), 1.0f, 0.05f, 100.0f);
    const CullView cv = CullView::from(V, P);
    ClusterBounds b;
    b.radius = 0.1f;
    b.center = glm::vec3(0.0f);
    CHECK(insideFrustum(cv, b));
    b.center = glm::vec3(0, 0, 6); // behind the eye
    CHECK(!insideFrustum(cv, b));
    b.center = glm::vec3(5, 0, 0); // far off to the side
    CHECK(!insideFrustum(cv, b));
    b.center = glm::vec3(1.6f, 0, 0); // straddling the edge of the view
    b.radius = 0.5f;
    CHECK(insideFrustum(cv, b));
    b.radius = -1.0f; // empty
    b.center = glm::vec3(0.0f);
    CHECK(!insideFrustum(cv, b));
}

TEST(motion_proxy_is_closed_and_light) {
    // The proxies of all clusters of a closed mesh, mapped back to mesh vertices, make a closed surface (no
    // holes while the view moves), each at most a quarter of its cluster.
    Mesh m = makeBenchSphere(300000);
    for (size_t v = 0; v < m.vertexCount(); ++v) { // some sculpted detail
        const glm::vec3 p = m.positions[v];
        m.positions[v] = p * (1.0f + 0.02f * std::sin(p.x * 31.0f) * std::sin(p.z * 29.0f));
    }
    m.recomputeAllNormals();
    UploadOrder order;
    order.build(m);
    std::map<std::pair<uint32_t, uint32_t>, int> edges; // directed edge -> count, minus the reverse's
    std::vector<uint16_t> proxy;
    size_t total = 0;
    bool light = true, built = true, inRange = true;
    for (uint32_t c = 0; c < order.clusters().size(); ++c) {
        ClusterBounds b = clusterBounds(m, order, c);
        const size_t cap = order.clusterTriangles(c) / 4;
        built = built && clusterProxy(m, order, c, cap, proxy, b);
        light = light && proxy.size() / 3 <= cap && proxy.size() % 3 == 0;
        total += proxy.size() / 3;
        const auto& cl = order.clusters()[c];
        for (size_t i = 0; i < proxy.size(); i += 3)
            for (int k = 0; k < 3; ++k) {
                inRange = inRange && proxy[i + size_t(k)] < cl.vertexCount;
                const uint32_t a = order.fromGpu()[cl.firstVertex + proxy[i + size_t(k)]];
                const uint32_t e = order.fromGpu()[cl.firstVertex + proxy[i + size_t((k + 1) % 3)]];
                ++edges[{a, e}];
                --edges[{e, a}];
            }
    }
    bool closed = true;
    for (const auto& [e, n] : edges) closed = closed && n == 0;
    std::printf("  motion proxy: %zu of %zu triangles\n", total, m.triangleCount());
    CHECK(built);
    CHECK(light);
    CHECK(inRange);
    CHECK(closed);
    CHECK(total * 4 < m.triangleCount());
    // Hidden triangles stay out of the proxy too.
    Mesh h = makeBenchSphere(20000);
    h.ensureHidden();
    for (size_t t = 0; t < h.triangleCount(); ++t) h.hidden[t] = 1;
    order.build(h);
    ClusterBounds b;
    CHECK(clusterProxy(h, order, 0, order.clusterTriangles(0), proxy, b) && proxy.empty());
}
