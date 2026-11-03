// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Focus Detail (core/FocusDetail.h): the detail file, coarse and full tiles, crack-free borders, brushes and undo
// on streamed tiles, prediction, the LRU budget and incremental saves.
#include "TestHarness.h"

#include "core/Benchmark.h"
#include "core/BVH.h"
#include "core/FocusDetail.h"
#include "core/Micro.h"
#include "core/Sculpt.h"

#include <atomic>
#include <cstdio>
#include <thread>
#include <filesystem>
#include <vector>

using namespace sf;

#if defined(__unix__) || defined(__APPLE__)

namespace {

std::string focusTempPath(const char* name) {
    const auto dir = std::filesystem::temp_directory_path() / "sf-focus-detail-test";
    std::filesystem::create_directories(dir);
    return (dir / name).string();
}

// A small head (about 2,000 base triangles) at micro level 5.
std::string writeSmall(const char* name) {
    const std::string path = focusTempPath(name);
    std::string err;
    CHECK(focusWriteSyntheticHead(path, 2000, 5, err));
    return path;
}

// Every shared edge has equal values on both sides (micro positions are computed per edge in vertex order, so
// equal values = no crack).
bool edgesShared(const Mesh& m) {
    const MicroMesh& mm = *m.micro;
    const uint32_t n = mm.n, S = mm.samples;
    for (uint32_t t = 0; t < mm.baseTriangles; ++t)
        for (int ed = 0; ed < 3; ++ed) {
            const uint32_t nb = mm.edgeNeighbor[size_t(t) * 3 + size_t(ed)];
            if (nb == MicroMesh::kNone) continue;
            const uint32_t u = nb >> 2;
            const int f = int(nb & 3);
            const bool reversed = m.triangles[u][f] != m.triangles[t][ed];
            for (uint32_t k = 0; k <= n; ++k) {
                uint32_t i, j, i2, j2;
                MicroMesh::edgePoint(ed, k, n, i, j);
                MicroMesh::edgePoint(f, reversed ? n - k : k, n, i2, j2);
                if (mm.disp[size_t(t) * S + MicroMesh::index(i, j, n)] != mm.disp[size_t(u) * S + MicroMesh::index(i2, j2, n)])
                    return false;
            }
        }
    return true;
}

std::vector<uint32_t> allTiles(const Mesh& m) {
    std::vector<uint32_t> all(m.micro->baseTriangles);
    for (uint32_t t = 0; t < all.size(); ++t) all[t] = t;
    return all;
}

} // namespace

TEST(focus_detail_open_is_coarse_then_full_is_exact) {
    const std::string path = writeSmall("exact.sfdetail");
    Mesh m;
    std::string err;
    auto fd = FocusDetail::open(path, m, err);
    CHECK(fd != nullptr);
    if (!fd) return;
    CHECK(m.micro && m.micro->level == 5);
    CHECK(fd->stats().fullTiles == 0);
    CHECK(fd->tileLevel(0) == 0);
    CHECK(edgesShared(m)); // coarse everywhere: watertight
    const std::vector<int16_t> coarse = m.micro->disp.toVector();
    // Half the tiles full: still watertight across the border.
    std::vector<uint32_t> half;
    for (uint32_t t = 0; t < m.micro->baseTriangles; t += 2) half.push_back(t);
    fd->ensureFull(*m.micro, half, false);
    CHECK(fd->tileLevel(0) == 1 && fd->tileLevel(1) == 0);
    CHECK(edgesShared(m));
    // All full: the generated values (the file is watertight by construction) and the coarse points unchanged.
    fd->ensureFull(*m.micro, allTiles(m), false);
    CHECK(edgesShared(m));
    const MicroMesh& mm = *m.micro;
    const uint32_t s = mm.n >> FocusDetail::kCoarseLevel;
    size_t differ = 0, coarseMismatch = 0;
    for (uint32_t t = 0; t < mm.baseTriangles; ++t)
        for (uint32_t j = 0; j <= mm.n; ++j)
            for (uint32_t i = 0; i + j <= mm.n; ++i) {
                const size_t k = size_t(t) * mm.samples + MicroMesh::index(i, j, mm.n);
                if (mm.disp[k] != coarse[k]) ++differ;
                if (i % s == 0 && j % s == 0 && mm.disp[k] != coarse[k]) ++coarseMismatch;
            }
    CHECK(differ > 0);           // the full level has detail the coarse one hasn't
    CHECK(coarseMismatch == 0);  // the coarse grid is a subsample
    CHECK(mm.rangeMax > 0);
}

TEST(focus_detail_damaged_file_is_refused) {
    const std::string path = writeSmall("damaged.sfdetail");
    std::filesystem::resize_file(path, std::filesystem::file_size(path) - 100);
    Mesh m;
    std::string err;
    CHECK(FocusDetail::open(path, m, err) == nullptr);
    CHECK(!err.empty());
    CHECK(FocusDetail::open(focusTempPath("missing.sfdetail"), m, err) == nullptr);
}

TEST(focus_detail_brush_promotes_undo_and_incremental_save) {
    const std::string path = writeSmall("edit.sfdetail");
    Mesh m0;
    std::string err;
    auto fd = FocusDetail::open(path, m0, err);
    CHECK(fd != nullptr);
    if (!fd) return;
    Document doc = makeDoc(std::move(m0));
    Mesh& m = doc.mesh;
    CHECK(m.micro && m.micro->focus == fd);
    const size_t fileBefore = std::filesystem::file_size(path);
    RayHit hit;
    CHECK(surfaceRaycast(m, doc.bvh, glm::vec3(0.0f, 0.1f, 5.0f), glm::vec3(0, 0, -1), hit));
    doc.sculptor.beginStroke(m, "Clay");
    DabParams p;
    p.center = hit.position;
    p.radius = 0.08f;
    p.strength = 0.8f;
    Symmetry sym;
    sym.x = false;
    doc.sculptor.dab(m, doc.bvh, BrushType::Clay, p, sym);
    const FocusStats s1 = fd->stats();
    CHECK(s1.syncPromoted > 0);          // the brush's tiles became full before it wrote
    CHECK(s1.dirtyTiles > 0);
    CHECK(fd->tileLevel(hit.triangle) == 1);
    const std::vector<int16_t> edited = m.micro->disp.toVector();
    doc.undo.push(doc.sculptor.endStroke());
    doc.undo.undo(m, doc.bvh);
    CHECK(m.micro->disp != edited);
    doc.undo.redo(m, doc.bvh);
    CHECK(m.micro->disp == edited);
    // Save: only the changed tiles are appended.
    CHECK(fd->save(m, err));
    const size_t fileAfter = std::filesystem::file_size(path);
    CHECK(fileAfter > fileBefore);
    CHECK(fileAfter - fileBefore < fileBefore / 4);
    CHECK(fd->stats().dirtyTiles == 0);
    // Reopened: the edited tiles come back as saved.
    Mesh again;
    auto fd2 = FocusDetail::open(path, again, err);
    CHECK(fd2 != nullptr);
    if (!fd2) return;
    fd2->ensureFull(*again.micro, allTiles(again), false);
    std::vector<uint32_t> all = allTiles(m);
    fd->ensureFull(*m.micro, all, false);
    CHECK(again.micro->disp == m.micro->disp);
    // Move on a micro level moves the base cage: saved too.
    m.positions[3] += glm::vec3(0.0f, 0.01f, 0.0f);
    CHECK(fd->save(m, err));
    Mesh third;
    CHECK(FocusDetail::open(path, third, err) != nullptr);
    CHECK(third.positions.size() == m.positions.size() && third.positions[3] == m.positions[3]);
}

TEST(focus_detail_prediction_budget_and_parking) {
    const std::string path = writeSmall("budget.sfdetail");
    Mesh m;
    std::string err;
    auto fd = FocusDetail::open(path, m, err);
    CHECK(fd != nullptr);
    if (!fd) return;
    MicroMesh& mm = *m.micro;
    // The brush moving towards +x: tiles ahead of it are promoted before it gets there.
    FocusBrush brush;
    brush.present = true;
    brush.position = glm::vec3(-0.3f, 0.1f, 0.9f);
    brush.velocity = glm::vec3(2.0f, 0.0f, 0.0f); // 0.5 units in the lookahead
    brush.radius = 0.05f;
    fd->update(m, FocusView{}, brush, 1000.0);
    const FocusStats s = fd->stats();
    CHECK(s.fullTiles > 0);
    bool ahead = false;
    for (uint32_t t = 0; t < mm.baseTriangles; ++t) {
        const glm::uvec3 tr = m.triangles[t];
        const glm::vec3 c = (m.positions[tr.x] + m.positions[tr.y] + m.positions[tr.z]) / 3.0f;
        if (fd->tileLevel(t) && c.x > 0.05f) ahead = true;
    }
    CHECK(ahead);
    CHECK(edgesShared(m));
    // An edited tile, then a budget of a few tiles elsewhere: the edit is parked (compressed), not lost.
    std::vector<uint32_t> one{0};
    fd->ensureFull(mm, one, true);
    mm.disp[5] = int16_t(mm.disp[5] + 77);
    const int16_t editedValue = mm.disp[5];
    fd->budgetBytes = size_t(mm.samples) * 2 * 8;
    brush.position = glm::vec3(0.3f, 0.1f, 0.9f);
    brush.velocity = glm::vec3(0.0f);
    for (int i = 0; i < 12; ++i) fd->update(m, FocusView{}, brush, 1000.0);
    const FocusStats s2 = fd->stats();
    CHECK(s2.fullTiles <= std::max<size_t>(8, s2.lastWanted)); // (the brush's own tiles go over the budget)
    CHECK(s2.demoted > 0);
    CHECK(fd->tileLevel(0) == 0);
    CHECK(s2.parkedTiles == 1);
    CHECK(edgesShared(m));
    fd->ensureFull(mm, one, false);
    CHECK(mm.disp[5] == editedValue);
    // Camera: a close view promotes visible tiles whose coarse grid is off on screen, a far one doesn't.
    fd->budgetBytes = size_t(1) << 30;
    FocusView far;
    far.valid = true;
    far.eye = glm::vec3(0, 0, 400.0f);
    far.viewProj = glm::mat4(1.0f) * 0.001f;
    far.viewProj[3][3] = 1.0f;
    const size_t before = fd->stats().promoted;
    fd->update(m, far, FocusBrush{}, 1000.0);
    CHECK(fd->stats().promoted == before);
}

TEST(focus_detail_brush_thread_and_frames_together) {
    // A stroke's dabs (ensureFull on another thread) while frames promote and demote under a small budget.
    const std::string path = writeSmall("threads.sfdetail");
    Mesh m;
    std::string err;
    auto fd = FocusDetail::open(path, m, err);
    CHECK(fd != nullptr);
    if (!fd) return;
    MicroMesh& mm = *m.micro;
    fd->budgetBytes = size_t(mm.samples) * 2 * 64;
    std::atomic<bool> stop{false};
    std::thread brush([&] {
        uint32_t k = 1;
        while (!stop) {
            std::vector<uint32_t> tiles;
            for (int i = 0; i < 8; ++i) tiles.push_back((k = k * 1664525u + 1013904223u) % mm.baseTriangles);
            fd->ensureFull(mm, tiles, false);
        }
    });
    for (int f = 0; f < 60; ++f) {
        FocusBrush b;
        b.present = true;
        b.position = glm::vec3(std::sin(float(f) * 0.2f), 0.1f, 0.8f);
        b.radius = 0.1f;
        fd->update(m, FocusView{}, b, 1000.0);
    }
    stop = true;
    brush.join();
    CHECK(fd->stats().demoted > 0);
    CHECK(edgesShared(m));
}

#endif
