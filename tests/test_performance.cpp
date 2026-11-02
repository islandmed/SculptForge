// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Performance work (docs/performance.md): the thread pool, brushes / normals / BVH refits that run on it give
// the same result as running serially, partial GPU uploads (core/UploadOrder.h) cover every changed vertex, the
// parallel BVH build answers like the serial one, undo after a big stroke is exact, project archives stream
// (and use ZIP64 past 4 GB), and the benchmark runs.
#include "TestHarness.h"

#include "core/Benchmark.h"
#include "core/BrushRecipe.h"
#include "core/IndexBits.h"
#include "core/Multires.h"
#include "core/Primitives.h"
#include "core/ThreadPool.h"
#include "core/UploadOrder.h"
#include "io/Zip.h"

#include <algorithm>
#include <atomic>
#include <bit>
#include <cmath>
#include <thread>

using namespace sf;

namespace {

// Runs `fn` with the shared pool forced serial, then restores it.
template <class F>
void serially(F&& fn) {
    ThreadPool::shared().setSerial(true);
    fn();
    ThreadPool::shared().setSerial(false);
}

bool sameVec3(const std::vector<glm::vec3>& a, const std::vector<glm::vec3>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (a[i] != b[i]) return false;
    return true;
}

// A stroke with every built-in brush type plus a recipe, mask and paint, big enough (a few blocks of vertices
// per dab) to run in parallel.
void bigStroke(Document& d) {
    const float radius = 0.4f;
    for (int t = 0; t < int(BrushType::Count); ++t) {
        const BrushType type = BrushType(t);
        d.sculptor.beginStroke(d.mesh, brushName(type));
        for (int i = 0; i < 4; ++i) {
            DabParams p;
            p.center = glm::normalize(glm::vec3(0.3f + 0.05f * float(i), 0.2f, 1.0f));
            p.radius = radius;
            p.strength = 0.6f;
            p.strokeDelta = glm::vec3(0.01f, 0.0f, 0.0f);
            if (type == BrushType::Move) {
                if (i == 0) d.sculptor.beginGrab(d.mesh, d.bvh, p, Symmetry{});
                else d.sculptor.grab(d.mesh, d.bvh, glm::vec3(0.01f, 0.005f, 0.0f));
            } else {
                d.sculptor.dab(d.mesh, d.bvh, type, p, Symmetry{});
            }
        }
        d.undo.push(d.sculptor.endStroke());
    }
    const std::vector<BrushRecipe> recipes = builtinBrushRecipes();
    for (const BrushRecipe& r : recipes) {
        d.sculptor.beginStroke(d.mesh, r.name);
        DabParams p;
        p.center = glm::normalize(glm::vec3(-0.2f, 0.4f, 1.0f));
        p.radius = radius;
        p.strokeDelta = glm::vec3(0.0f, 0.01f, 0.0f);
        d.sculptor.dabRecipe(d.mesh, d.bvh, r, p, Symmetry{});
        d.undo.push(d.sculptor.endStroke());
    }
    d.sculptor.beginStroke(d.mesh, "Mask");
    DabParams p;
    p.center = glm::normalize(glm::vec3(0.0f, -0.3f, 1.0f));
    p.radius = radius;
    d.sculptor.dabMask(d.mesh, d.bvh, p, Symmetry{});
    d.undo.push(d.sculptor.endStroke());
}

Document sphereDoc(int subdivisions) { return makeDoc(makeIcosphere(subdivisions)); }

} // namespace

TEST(thread_pool_runs_every_job_once) {
    ThreadPool& pool = ThreadPool::shared();
    CHECK(pool.threads() >= 1);
    std::vector<std::atomic<int>> hits(100000);
    poolFor(hits.size(), [&](size_t b, size_t e) {
        for (size_t i = b; i < e; ++i) hits[i].fetch_add(1);
    }, 1000);
    bool once = true;
    for (auto& h : hits) once = once && h.load() == 1;
    CHECK(once);

    // Nested use runs inline; two threads using the pool at once both finish.
    std::atomic<size_t> nested{0};
    poolFor(64, [&](size_t b, size_t e) {
        for (size_t i = b; i < e; ++i) poolFor(100, [&](size_t b2, size_t e2) { nested += e2 - b2; }, 10);
    }, 1);
    CHECK(nested.load() == 6400);
    std::atomic<size_t> a{0}, c{0};
    std::thread other([&] {
        for (int k = 0; k < 50; ++k) poolFor(10000, [&](size_t b, size_t e) { a += e - b; }, 100);
    });
    for (int k = 0; k < 50; ++k) poolFor(10000, [&](size_t b, size_t e) { c += e - b; }, 100);
    other.join();
    CHECK(a.load() == 500000 && c.load() == 500000);

    // One block runs on the calling thread, like a plain loop.
    std::thread::id where;
    poolFor(10, [&](size_t, size_t) { where = std::this_thread::get_id(); }, 4096);
    CHECK(where == std::this_thread::get_id());
}

TEST(parallel_brushes_match_serial) {
    // icosphere(7): 164k vertices, so each dab covers several blocks (> 4096 vertices) and runs in parallel.
    Document par = sphereDoc(7), ser = sphereDoc(7);
    bigStroke(par);
    serially([&] { bigStroke(ser); });
    CHECK(sameVec3(par.mesh.positions, ser.mesh.positions));
    CHECK(sameVec3(par.mesh.normals, ser.mesh.normals));
    CHECK(par.mesh.mask == ser.mesh.mask);
    CHECK(par.undo.undoCount() == ser.undo.undoCount());
    // Undo every stroke: back to the sphere exactly.
    const Mesh fresh = makeIcosphere(7);
    while (par.undo.canUndo()) par.undo.undo(par.mesh, par.bvh);
    CHECK(sameVec3(par.mesh.positions, fresh.positions));
    bool maskClear = true;
    for (float m : par.mesh.mask) maskClear = maskClear && m == 0.0f;
    CHECK(maskClear);
}

TEST(parallel_normals_match_full_recompute) {
    Mesh m = makeIcosphere(6);
    std::vector<uint32_t> moved;
    for (uint32_t v = 0; v < m.vertexCount(); v += 3) {
        m.positions[v] *= 1.0f + 0.05f * std::sin(float(v));
        moved.push_back(v);
    }
    m.updateNormals(moved);
    const std::vector<glm::vec3> partial = m.normals;
    m.recomputeAllNormals();
    float worst = 0.0f;
    for (size_t v = 0; v < partial.size(); ++v) worst = std::max(worst, glm::length(partial[v] - m.normals[v]));
    CHECK(worst < 1e-5f);
}

TEST(upload_runs_of_a_big_brush_match_one_thread) {
    // A brush over a good part of the mesh (more marked vertices than dirtyRuns walks on one thread, under the
    // three quarters it uploads whole): the GPU blocks are marked in parallel, and the runs are exactly the ones
    // a single thread finds, every slot of every marked vertex in one.
    Mesh m = makeBenchSphere(1600000);
    BVH bvh;
    bvh.build(m);
    UploadOrder order;
    order.build(m);
    m.clearDirty();
    Sculptor s;
    s.beginStroke(m, "Standard");
    DabParams p;
    p.center = glm::vec3(0.0f, 0.0f, 1.0f);
    p.radius = 1.0f;
    s.dab(m, bvh, BrushType::Standard, p, Symmetry{});
    size_t dirty = 0;
    for (uint32_t v = 0; v < m.vertexCount(); ++v) dirty += m.vertexDirty(v);
    CHECK(dirty > 65536 && dirty * 4 < m.vertexCount() * 3);
    std::vector<std::pair<uint32_t, uint32_t>> par, ser;
    order.dirtyRuns(m, par);
    serially([&] { order.dirtyRuns(m, ser); });
    CHECK(par == ser && !par.empty());
    CHECK(!(par.size() == 1 && par[0].first == 0 && par[0].second == order.size())); // (not everything)
    std::vector<uint8_t> inRun(order.size(), 0);
    for (const auto& [b, e] : par)
        for (uint32_t g = b; g < e; ++g) inRun[g] = 1;
    bool covered = true;
    std::vector<uint32_t> slots;
    for (uint32_t v = 0; v < m.vertexCount() && covered; ++v)
        if (m.vertexDirty(v)) {
            order.slotsOf(v, slots);
            for (uint32_t g : slots) covered = covered && inRun[g];
        }
    CHECK(covered);
}

TEST(dab_normals_from_the_query_leaves_match_a_full_recompute) {
    // A dab's normals are recomputed for the corners of the BVH leaves its queries listed: every normal comes
    // out exactly as recomputeAllNormals has it, and every vertex whose normal changed is marked for upload.
    // On a plain mesh and on a level made by Divide (implicit adjacency), with symmetry, for several brushes.
    for (int divided = 0; divided < 2; ++divided) {
        Mesh m = divided ? makeIcosphere(4) : makeBenchSphere(300000);
        if (divided) {
            std::string err;
            CHECK(subdivDivide(m, true, err) && subdivDivide(m, true, err));
            CHECK(m.implicitAdjacency());
        }
        BVH bvh;
        bvh.build(m);
        Sculptor s;
        s.beginStroke(m, "Standard");
        bool same = true, marked = true;
        for (BrushType type : {BrushType::Standard, BrushType::Smooth, BrushType::Clay, BrushType::Vdm}) {
            const std::vector<glm::vec3> before = m.normals;
            m.clearDirty();
            DabParams p;
            p.center = glm::normalize(glm::vec3(0.4f, 0.3f, 0.8f));
            p.viewDir = -p.center;
            p.radius = 0.5f;
            p.strength = 0.7f;
            s.dab(m, bvh, type, p, Symmetry{});
            Mesh fresh = m;
            fresh.recomputeAllNormals();
            for (uint32_t v = 0; v < m.vertexCount(); ++v) {
                same = same && m.normals[v] == fresh.normals[v];
                if (m.normals[v] != before[v]) marked = marked && m.vertexDirty(v);
            }
        }
        CHECK(same);
        CHECK(marked);
        s.endStroke();
    }
}

TEST(upload_runs_cover_changed_vertices) {
    // A subdivided mesh, whose vertex numbering scatters any region over the index range.
    Mesh m = makeBenchSphere(200000);
    BVH bvh;
    bvh.build(m);
    UploadOrder order;
    order.build(m);
    // Every vertex has a slot (the sphere uses them all); toGpu is its first.
    bool mapped = order.size() >= m.vertexCount();
    for (uint32_t v = 0; mapped && v < m.vertexCount(); ++v)
        mapped = order.toGpu(v) != UploadOrder::kNone && order.fromGpu()[order.toGpu(v)] == v;
    CHECK(mapped);

    m.clearDirty();
    std::vector<std::pair<uint32_t, uint32_t>> runs;
    order.dirtyRuns(m, runs);
    CHECK(runs.empty());
    CHECK(!m.vertexDirty(0));
    // A brush dab with symmetry: every vertex whose normal changed is in a run, and the runs hold not much
    // more than the dab touched.
    Sculptor s;
    s.beginStroke(m, "Standard");
    const std::vector<glm::vec3> before = m.normals;
    DabParams p;
    p.center = glm::normalize(glm::vec3(0.5f, 0.5f, 0.7f));
    p.radius = 0.2f;
    s.dab(m, bvh, BrushType::Standard, p, Symmetry{});
    order.dirtyRuns(m, runs);
    std::vector<uint8_t> slotInRun(order.size(), 0);
    size_t uploaded = 0;
    for (size_t i = 0; i < runs.size(); ++i) {
        CHECK(runs[i].first < runs[i].second && runs[i].second <= order.size());
        if (i) CHECK(runs[i - 1].second < runs[i].first);
        uploaded += runs[i].second - runs[i].first;
        for (uint32_t g = runs[i].first; g < runs[i].second; ++g) slotInRun[g] = 1;
    }
    // Every slot of a changed vertex, the copies on cluster seams included.
    bool allCovered = true;
    size_t changed = 0, dirty = 0;
    std::vector<uint32_t> slots;
    for (uint32_t v = 0; v < m.vertexCount(); ++v) {
        dirty += m.vertexDirty(v);
        if (m.normals[v] != before[v]) {
            ++changed;
            order.slotsOf(v, slots);
            allCovered = allCovered && m.vertexDirty(v) && !slots.empty();
            for (uint32_t g : slots) allCovered = allCovered && slotInRun[g];
        }
    }
    CHECK(changed > 1000);
    CHECK(allCovered);
    // In mesh order, the 2048-vertex blocks holding them would be several times more.
    size_t meshOrder = 0;
    for (size_t b = 0; b < m.dirtyBlocks.size(); ++b) meshOrder += m.dirtyBlocks[b] ? Mesh::kDirtyBlock : 0;
    CHECK(uploaded * 3 < meshOrder);
    m.clearDirty();
    order.dirtyRuns(m, runs);
    CHECK(runs.empty());
    CHECK(!m.vertexDirty(order.fromGpu()[0]));
    // Ranges mark per vertex too; most of the mesh dirty uploads everything as one range.
    m.markDirtyRange(70, 200);
    CHECK(!m.vertexDirty(69) && m.vertexDirty(70) && m.vertexDirty(199) && !m.vertexDirty(200));
    m.markAllDirty();
    order.dirtyRuns(m, runs);
    CHECK(runs.size() == 1 && runs[0].first == 0 && runs[0].second == order.size());
}

TEST(parallel_bvh_build_answers_like_serial) {
    // icosphere(7): 328k triangles, above the size the build splits over threads.
    const Mesh m = makeIcosphere(7);
    BVH par, ser;
    par.build(m);
    serially([&] { ser.build(m); });
    CHECK(par.nodeCount() == ser.nodeCount());
    std::vector<uint32_t> a, b;
    bool same = true;
    for (int i = 0; i < 50; ++i) {
        const glm::vec3 c = glm::normalize(glm::vec3(std::sin(float(i)), std::cos(float(i) * 1.3f), std::sin(float(i) * 0.7f)));
        par.queryVertices(m, c, 0.1f, a);
        ser.queryVertices(m, c, 0.1f, b);
        same = same && a == b;
        par.queryTriangles(m, c, 0.05f, a);
        ser.queryTriangles(m, c, 0.05f, b);
        same = same && a == b;
        RayHit h1, h2;
        const bool r1 = par.raycast(m, c * 3.0f, -c, h1), r2 = ser.raycast(m, c * 3.0f, -c, h2);
        same = same && r1 && r2 && h1.triangle == h2.triangle && h1.t == h2.t;
    }
    CHECK(same);
}

TEST(bvh_refit_after_big_stroke_finds_everything) {
    Document d = sphereDoc(7);
    bigStroke(d);
    // Brute force vs. the refitted tree.
    std::vector<uint32_t> found;
    bool ok = true;
    for (int i = 0; i < 20; ++i) {
        const glm::vec3 c = glm::normalize(glm::vec3(0.3f, 0.2f - 0.03f * float(i), 1.0f));
        d.bvh.queryVertices(d.mesh, c, 0.2f, found);
        size_t brute = 0;
        for (const glm::vec3& p : d.mesh.positions) brute += glm::distance(p, c) <= 0.2f;
        ok = ok && found.size() == brute;
    }
    CHECK(ok);
}

TEST(index_bits_list_members_in_order) {
    // Three chunks and a bit, with members at the chunk edges, added twice over.
    const uint32_t n = 3 * IndexBits::kChunk + 77;
    IndexBits bits;
    bits.resize(n);
    CHECK(bits.empty() && bits.capacity() >= n);
    std::vector<uint32_t> want;
    for (uint32_t i = 0; i < n; ++i)
        if ((i * 2654435761u) % 7 == 0 || i == IndexBits::kChunk - 1 || i == IndexBits::kChunk || i == n - 1) want.push_back(i);
    for (int pass = 0; pass < 2; ++pass)
        for (size_t k = want.size(); k-- > 0;) CHECK(bits.insert(want[k]) == (pass == 0));
    CHECK(bits.contains(want[0]) && !bits.contains(1) && !bits.empty());
    std::vector<uint32_t> got{12345}; // take appends
    bits.take(got);
    CHECK(got.size() == want.size() + 1 && got[0] == 12345 && std::equal(want.begin(), want.end(), got.begin() + 1));
    CHECK(bits.empty() && !bits.contains(want[0]));
    got.clear();
    bits.take(got);
    CHECK(got.empty());

    // insertParallel (each job into a set of its own, merged) finds the same set as one thread, and so does the
    // word callback of take().
    auto fill = [&](IndexBits& b) {
        b.insertParallel(200000, 1000, [&](size_t first, size_t last, IndexBits& set) {
            for (size_t i = first; i < last; ++i) set.insertLocal(uint32_t((i * 7919u) % n));
        });
    };
    IndexBits par, ser;
    par.resize(n);
    ser.resize(n);
    fill(par);
    serially([&] { fill(ser); });
    std::vector<uint32_t> a, b;
    size_t words = 0, bitsSeen = 0;
    par.take(a, [&](size_t, uint64_t w) {
        ++words;
        bitsSeen += size_t(std::popcount(w));
    });
    ser.take(b);
    CHECK(a == b && a.size() == n && bitsSeen == n && words == (n + 63) / 64);
    CHECK(std::is_sorted(a.begin(), a.end()));
}

TEST(bvh_refit_of_many_leaves_in_parallel) {
    // A big brush refits so many leaves that their ancestors are recomputed in parallel (BVH::refitAncestors):
    // the tree then finds exactly what brute force does, the same on one thread, and after the vertices move
    // back (the boxes shrink) as well.
    Document d = sphereDoc(7);
    const std::vector<glm::vec3> start = d.mesh.positions;
    std::vector<uint32_t> moved;
    const glm::vec3 c(0.0f, 0.0f, 1.0f);
    d.bvh.queryVertices(d.mesh, c, 1.3f, moved);
    CHECK(d.bvh.queriedLeaves().size() > 4096); // (the parallel path)
    auto check = [&](BVH& bvh) {
        std::vector<uint32_t> found;
        bool ok = true;
        for (int i = 0; i < 24; ++i) {
            const glm::vec3 q = glm::normalize(glm::vec3(std::sin(float(i)), std::cos(float(i) * 1.3f), 0.7f)) * 1.05f;
            bvh.queryVertices(d.mesh, q, 0.12f, found);
            size_t brute = 0;
            for (const glm::vec3& p : d.mesh.positions) brute += glm::distance(p, q) <= 0.12f;
            RayHit h;
            const bool hit = bvh.raycast(d.mesh, q * 3.0f, -q, h);
            ok = ok && found.size() == brute && hit;
        }
        return ok;
    };
    for (uint32_t v : moved) d.mesh.positions[v] *= 1.0f + 0.15f * (1.0f + std::sin(float(v) * 0.01f));
    BVH serialTree = d.bvh;
    d.bvh.refitQueried(d.mesh);
    serially([&] { serialTree.refit(d.mesh, moved); });
    CHECK(check(d.bvh));
    CHECK(check(serialTree));
    // Back where they were: the boxes shrink.
    d.mesh.positions = start;
    d.bvh.refit(d.mesh, moved);
    CHECK(check(d.bvh));
}

TEST(bvh_query_lists_inside_vertices_in_order) {
    Document d = sphereDoc(7);
    std::vector<uint32_t> found, brute;
    bool ok = true;
    for (int i = 0; i < 12; ++i) {
        const glm::vec3 c = glm::normalize(glm::vec3(std::sin(float(i)), std::cos(float(i) * 1.7f), 0.5f));
        const float r = i == 0 ? 0.0f : i == 1 ? 5.0f : 0.02f * float(i);
        d.bvh.queryVertices(d.mesh, c, r, found);
        brute.clear();
        for (uint32_t v = 0; v < d.mesh.vertexCount(); ++v) {
            const glm::vec3 o = d.mesh.positions[v] - c;
            if (glm::dot(o, o) <= r * r) brute.push_back(v);
        }
        ok = ok && found == brute;
    }
    CHECK(ok);
}

TEST(bvh_refit_from_query_matches_refit) {
    // Move what a query found, then refit one copy of the tree from the query's leaves and another the usual
    // way: both find exactly what a brute force search does.
    Document d = sphereDoc(7);
    BVH other = d.bvh;
    std::vector<uint32_t> moved;
    const glm::vec3 c = glm::normalize(glm::vec3(0.2f, 0.3f, 1.0f));
    d.bvh.queryVertices(d.mesh, c, 0.4f, moved);
    CHECK(!d.bvh.queriedLeaves().empty());
    for (uint32_t v : moved) d.mesh.positions[v] += glm::vec3(0.1f, -0.05f, 0.2f) * (1.0f + 0.3f * std::sin(float(v)));
    d.bvh.refitQueried(d.mesh);
    other.refit(d.mesh, moved);
    std::vector<uint32_t> a, b;
    bool ok = true;
    for (int i = 0; i < 20; ++i) {
        const glm::vec3 q = glm::normalize(glm::vec3(0.3f, 0.4f - 0.05f * float(i), 1.0f)) * 1.1f;
        d.bvh.queryVertices(d.mesh, q, 0.15f, a);
        other.queryVertices(d.mesh, q, 0.15f, b);
        size_t brute = 0;
        for (const glm::vec3& p : d.mesh.positions) {
            const glm::vec3 o = p - q;
            brute += glm::dot(o, o) <= 0.15f * 0.15f;
        }
        ok = ok && a == b && a.size() == brute;
    }
    CHECK(ok);
}

TEST(stroke_records_each_vertex_once_per_stroke) {
    // The Sculptor's per-stroke "already recorded" bits start empty for every stroke, also after one that never
    // ended: each stroke's undo step restores exactly what it changed.
    Document d = sphereDoc(6);
    const std::vector<glm::vec3> start = d.mesh.positions;
    DabParams p;
    p.center = glm::normalize(glm::vec3(0.1f, 0.2f, 1.0f));
    p.radius = 0.5f;
    auto stroke = [&](int dabs) {
        d.sculptor.beginStroke(d.mesh, "Standard");
        for (int i = 0; i < dabs; ++i) d.sculptor.dab(d.mesh, d.bvh, BrushType::Standard, p, Symmetry{});
    };
    stroke(3);
    UndoRecord first = d.sculptor.endStroke();
    const size_t recorded = first.positions.indices.size();
    std::vector<uint32_t> idx = first.positions.indices;
    std::sort(idx.begin(), idx.end());
    CHECK(recorded > 0 && std::unique(idx.begin(), idx.end()) == idx.end());
    d.undo.push(std::move(first));
    const std::vector<glm::vec3> afterFirst = d.mesh.positions;
    stroke(1); // abandoned: never ended (and not undoable)
    const std::vector<glm::vec3> afterAbandoned = d.mesh.positions;
    stroke(2);
    UndoRecord third = d.sculptor.endStroke();
    CHECK(!third.positions.indices.empty());
    d.undo.push(std::move(third));
    d.sculptor.beginStroke(d.mesh, "Mask");
    d.mesh.ensureMask();
    for (int k = 0; k < 2; ++k)
        for (uint32_t v = 0; v < 50; ++v) d.sculptor.recordMask(d.mesh, v);
    CHECK(d.sculptor.endStroke().mask.indices.size() == 50);
    d.undo.undo(d.mesh, d.bvh);
    CHECK(sameVec3(d.mesh.positions, afterAbandoned));
    d.undo.redo(d.mesh, d.bvh);
    d.undo.undo(d.mesh, d.bvh);
    d.undo.undo(d.mesh, d.bvh);
    bool restored = true; // the first stroke's vertices are back where they started
    for (uint32_t v : idx) restored = restored && d.mesh.positions[v] == start[v];
    CHECK(restored && !sameVec3(afterFirst, start));
}

TEST(first_paint_undo_is_cheap_and_exact) {
    // The app's first polypaint stroke on an unpainted mesh: the undo step holds the paint stack without
    // colours (not a copy of the mesh), and undo removes the attribute again.
    Document d = sphereDoc(5);
    CHECK(!d.mesh.hasColors());
    auto snapshot = std::make_unique<PaintStackUndo>(PaintStackUndo::capture(d.mesh));
    d.sculptor.beginStroke(d.mesh, "Paint");
    d.mesh.ensureColors();
    for (uint32_t v = 0; v < 100; ++v) {
        d.sculptor.recordColor(d.mesh, v);
        d.mesh.colors[v] = glm::vec3(1, 0, 0);
    }
    UndoRecord r = d.sculptor.endStroke();
    r.colors = {};
    r.paintAlpha = {};
    r.paint = std::move(snapshot);
    CHECK(r.approxBytes() < 1000);
    d.undo.push(std::move(r));
    d.undo.undo(d.mesh, d.bvh);
    CHECK(!d.mesh.hasColors());
    d.undo.redo(d.mesh, d.bvh);
    CHECK(d.mesh.hasColors() && d.mesh.colors[5] == glm::vec3(1, 0, 0) && d.mesh.colors[500] == glm::vec3(1.0f));
}

TEST(mesh_memory_has_no_face_normal_cache) {
    // Bytes per triangle of a sculpting mesh (positions, normals, triangles, both adjacency lists and stamps):
    // about 54 on a closed triangle mesh (it was 78 with a per-triangle face normal cache, its stamps and the
    // adjacency list's spare capacity).
    const Mesh m = makeIcosphere(6);
    const double perTriangle = double(m.memoryBytes()) / double(m.triangleCount());
    CHECK(perTriangle < 60.0);
}

TEST(benchmark_smoke) {
    BenchOptions o;
    o.sizesMillions = {0.02};
    o.dabsPerStroke = 3;
    o.projectDir = std::filesystem::temp_directory_path().string();
    const std::string report = runBenchmark(o);
    CHECK(report.find("| Standard |") != std::string::npos);
    CHECK(report.find("| Paint |") != std::string::npos);
    CHECK(report.find("Open project") != std::string::npos);
    CHECK(report.find("failed") == std::string::npos);
    const Mesh m = makeBenchSphere(50000);
    CHECK(m.triangleCount() > 40000 && m.triangleCount() < 60000);
}

TEST(zip_round_trip_and_zip64) {
    // Stored ZIPs stream to disk; ZIP64 records (used past 4 GB) read back the same.
    std::vector<ZipEntry> entries;
    entries.push_back({"manifest.json", std::vector<uint8_t>{'{', '}'}});
    std::vector<uint8_t> big(300001);
    for (size_t i = 0; i < big.size(); ++i) big[i] = uint8_t(i * 7 + (i >> 9));
    entries.push_back({"data/big.bin", big});
    entries.push_back({"empty.bin", {}});
    CHECK(crc32(reinterpret_cast<const uint8_t*>("123456789"), 9) == 0xCBF43926u); // the standard check value
    for (bool zip64 : {false, true}) {
        const std::string path = tempPath(zip64 ? "sf-zip64.zip" : "sf-zip32.zip");
        std::string error;
        CHECK(writeStoredZip(path, entries, 64, error, zip64));
        std::vector<ZipEntry> back;
        CHECK(readStoredZip(path, back, error));
        CHECK(back.size() == entries.size());
        bool same = back.size() == entries.size();
        for (size_t i = 0; same && i < back.size(); ++i) same = back[i].name == entries[i].name && back[i].data == entries[i].data;
        CHECK(same);
        std::filesystem::remove(path);
    }
}

TEST(benchmark_standard_head) {
    // Proof mode's standard scenes (standard-15m / standard-33m) sculpt a procedural head: the size asked for, a
    // face (the nose the frontmost point, near the middle), taller than wide, the same every time it is built.
    for (size_t want : {size_t(300000), size_t(1200000)}) {
        const Mesh a = makeBenchHead(want), b = makeBenchHead(want);
        CHECK(a.triangleCount() > want * 8 / 10 && a.triangleCount() < want * 12 / 10);
        CHECK(a.positions == b.positions);
        glm::vec3 lo(1e9f), hi(-1e9f), front(0.0f, 0.0f, -1e9f);
        for (const glm::vec3& p : a.positions) {
            lo = glm::min(lo, p), hi = glm::max(hi, p);
            if (p.z > front.z) front = p;
        }
        CHECK(std::abs(front.x) < 0.05f && front.y > -0.3f && front.y < 0.2f && front.z > 1.0f);
        CHECK(hi.y - lo.y > (hi.x - lo.x) * 1.1f);
    }
}
