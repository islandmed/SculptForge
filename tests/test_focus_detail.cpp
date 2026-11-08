// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Focus Detail (core/FocusDetail.h): the detail file, coarse and full tiles, crack-free borders, brushes and undo
// on streamed tiles, prediction, the LRU budget and incremental saves; in the app (phase 3): a widened range,
// create / attach / Save As / compaction, projects naming their detail files, Divide to Micro switching to it,
// and every micro brush writing exactly what it writes in memory.
#include "TestHarness.h"

#include "core/Benchmark.h"
#include "core/BVH.h"
#include "core/Commands.h"
#include "core/FocusDetail.h"
#include "core/Json.h"
#include "core/Micro.h"
#include "core/Project.h"
#include "core/Sculpt.h"
#include "io/Zip.h"

#include <atomic>
#include <sys/stat.h>
#include <cstdio>
#include <cstring>
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

// The renderer's per-tile record (render/MicroFocus.h): a coarse tile's record rebuilds exactly what disp holds
// (its upsampled inside and its edges, real ones next to full tiles), so the GPU draws what brushes and picks see,
// crack-free on both sides of the focus border; a full tile's gives back its edges and coarse points.
TEST(focus_detail_gpu_record_rebuilds_coarse_tiles_exactly) {
    const std::string path = writeSmall("record.sfdetail");
    Mesh m;
    std::string err;
    auto fd = FocusDetail::open(path, m, err);
    CHECK(fd != nullptr);
    if (!fd) return;
    const MicroMesh& mm = *m.micro;
    const uint32_t n = mm.n, cn = 1u << FocusDetail::kCoarseLevel, s = n / cn;
    const uint32_t R = (cn + 1) * (cn + 2) / 2 + 3 * (n - 1);
    auto check = [&](size_t& coarseOff, size_t& fullOff, size_t& coarseTiles) {
        std::vector<int16_t> rec(R);
        coarseOff = fullOff = coarseTiles = 0;
        for (uint32_t t = 0; t < mm.baseTriangles; ++t) {
            const int16_t* g = mm.disp.data() + size_t(t) * mm.samples;
            focusFillRecord(g, n, cn, rec.data());
            const bool coarse = fd->tileLevel(t) == 0;
            coarseTiles += coarse;
            for (uint32_t J = 0; J <= n; ++J)
                for (uint32_t I = 0; I + J <= n; ++I) {
                    const bool kept = J == 0 || I == 0 || I + J == n || (I % s == 0 && J % s == 0);
                    if (!coarse && !kept) continue;
                    if (focusRecordValue(rec.data(), n, cn, I, J) != g[MicroMesh::index(I, J, n)]) ++(coarse ? coarseOff : fullOff);
                }
        }
    };
    size_t coarseOff, fullOff, coarseTiles;
    check(coarseOff, fullOff, coarseTiles);
    CHECK(coarseTiles == mm.baseTriangles);
    CHECK(coarseOff == 0);
    // A focus: every third tile full (their real edges go into coarse neighbours), then some demoted again.
    std::vector<uint32_t> some;
    for (uint32_t t = 0; t < mm.baseTriangles; t += 3) some.push_back(t);
    fd->ensureFull(*m.micro, some, false);
    check(coarseOff, fullOff, coarseTiles);
    CHECK(coarseTiles < mm.baseTriangles && coarseTiles > 0);
    CHECK(coarseOff == 0);
    CHECK(fullOff == 0);
    const size_t focusCoarse = coarseTiles;
    fd->budgetBytes = 1; // (demoted again once not wanted for a few frames: their neighbours take back upsampled edges)
    FocusView view;
    FocusBrush brush;
    for (int f = 0; f < 20; ++f) fd->update(m, view, brush, 100.0);
    check(coarseOff, fullOff, coarseTiles);
    CHECK(coarseTiles > focusCoarse);
    CHECK(coarseOff == 0);
    CHECK(fullOff == 0);
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

// Crafted fields (security read of the reader): offsets past the end or wrapping 64 bits, bad counts, a directory
// entry past the end (directory hash fixed up so the check behind it runs), a triangle index past the vertices.
TEST(focus_detail_crafted_fields_are_refused) {
    const std::string good = writeSmall("crafted-src.sfdetail");
    std::vector<uint8_t> file(std::filesystem::file_size(good));
    {
        FILE* f = std::fopen(good.c_str(), "rb");
        CHECK(f && std::fread(file.data(), 1, file.size(), f) == file.size());
        if (f) std::fclose(f);
    }
    auto get64 = [&](const std::vector<uint8_t>& b, size_t at) { uint64_t v; std::memcpy(&v, b.data() + at, 8); return v; };
    auto get32 = [&](const std::vector<uint8_t>& b, size_t at) { uint32_t v; std::memcpy(&v, b.data() + at, 4); return v; };
    auto rehash = [&](std::vector<uint8_t>& b) { // the directory hash (fnv1a), as the writer stores it
        uint64_t h = 1469598103934665603ull;
        for (uint64_t i = 0; i < get64(b, 64); ++i) h = (h ^ b[get64(b, 56) + i]) * 1099511628211ull;
        std::memcpy(b.data() + 88, &h, 8);
    };
    auto refused = [&](const char* what, auto&& patch) {
        std::vector<uint8_t> b = file;
        patch(b);
        const std::string path = focusTempPath("crafted.sfdetail");
        FILE* f = std::fopen(path.c_str(), "wb");
        CHECK(f && std::fwrite(b.data(), 1, b.size(), f) == b.size());
        if (f) std::fclose(f);
        Mesh m;
        std::string err;
        const bool ok = FocusDetail::open(path, m, err) == nullptr && !err.empty();
        if (!ok) std::printf("  not refused: %s\n", what);
        CHECK(ok);
    };
    auto put64 = [](std::vector<uint8_t>& b, size_t at, uint64_t v) { std::memcpy(b.data() + at, &v, 8); };
    auto put32 = [](std::vector<uint8_t>& b, size_t at, uint32_t v) { std::memcpy(b.data() + at, &v, 4); };
    refused("truncated header", [](std::vector<uint8_t>& b) { b.resize(64); });
    refused("bad magic", [](std::vector<uint8_t>& b) { b[0] = 'X'; });
    refused("newer version", [&](std::vector<uint8_t>& b) { put32(b, 8, 99); });
    refused("unknown codec", [&](std::vector<uint8_t>& b) { put32(b, 12, 77); });
    refused("level too high", [&](std::vector<uint8_t>& b) { put32(b, 16, 40); });
    refused("no triangles", [&](std::vector<uint8_t>& b) { put32(b, 24, 0); });
    refused("more triangles", [&](std::vector<uint8_t>& b) { put32(b, 24, get32(b, 24) + 1); });
    refused("huge vertex count", [&](std::vector<uint8_t>& b) { put32(b, 28, 0xFFFFFFFFu); });
    refused("base past the end", [&](std::vector<uint8_t>& b) { put64(b, 40, b.size()); });
    refused("base offset wraps", [&](std::vector<uint8_t>& b) { put64(b, 40, ~uint64_t(0) - 7); });
    refused("directory offset wraps", [&](std::vector<uint8_t>& b) { put64(b, 56, ~uint64_t(0) - 7); });
    refused("directory size wrong", [&](std::vector<uint8_t>& b) { put64(b, 64, get64(b, 64) - 24); });
    refused("directory hash wrong", [&](std::vector<uint8_t>& b) { b[get64(b, 56)] ^= 1; });
    refused("tile offset wraps", [&](std::vector<uint8_t>& b) { put64(b, get64(b, 56), ~uint64_t(0) - 3); rehash(b); });
    refused("tile past the end", [&](std::vector<uint8_t>& b) { put64(b, get64(b, 56), b.size() - 2); rehash(b); });
    refused("tile size past the end", [&](std::vector<uint8_t>& b) { put32(b, get64(b, 56) + 8, 0xFFFFFFFFu); rehash(b); });
    refused("chunk offset wraps", [&](std::vector<uint8_t>& b) {
        put64(b, get64(b, 56) + size_t(get32(b, 24)) * 24, ~uint64_t(0) - 3);
        rehash(b);
    });
    refused("chunk size wrong", [&](std::vector<uint8_t>& b) {
        put32(b, get64(b, 56) + size_t(get32(b, 24)) * 24 + 8, 5);
        rehash(b);
    });
    refused("triangle index past the vertices", [&](std::vector<uint8_t>& b) {
        put32(b, get64(b, 40) + size_t(get32(b, 28)) * 24, get32(b, 28));
    });
    // The untouched file still opens.
    Mesh m;
    std::string err;
    CHECK(FocusDetail::open(good, m, err) != nullptr);
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


namespace {

// The detail file at `path` fully decompressed (a reference).
std::vector<int16_t> fullValues(const std::string& path) {
    Mesh m;
    std::string err;
    auto fd = FocusDetail::open(path, m, err);
    if (!fd) return {};
    fd->ensureFull(*m.micro, allTiles(m), false);
    return m.micro->disp.toVector();
}

std::vector<int16_t> streamedFull(Mesh& m) {
    m.micro->focus->ensureAllFull(*m.micro, false);
    return m.micro->disp.toVector();
}

// One dab where a ray from `from` along -Z meets `pickOn` (default: doc's own mesh; a streamed mesh picks on what
// it holds, so two meshes compared dab at one point picked on the full one).
void dabAt(Document& doc, BrushType type, const char* name, glm::vec3 from, float radius, float strength,
           Document* pickOn = nullptr) {
    RayHit hit;
    Document& pd = pickOn ? *pickOn : doc;
    if (!surfaceRaycast(pd.mesh, pd.bvh, from, glm::vec3(0, 0, -1), hit)) return;
    doc.sculptor.beginStroke(doc.mesh, name);
    DabParams p;
    p.center = hit.position;
    p.radius = radius;
    p.strength = strength;
    Symmetry sym;
    sym.x = false;
    doc.sculptor.dab(doc.mesh, doc.bvh, type, p, sym);
    doc.undo.push(doc.sculptor.endStroke());
}

} // namespace

// A brush that widens the displacement range (MicroMesh::widen) divides every value: coarse grids, parked tiles
// and the tiles still in the file (read with a shift) follow, so later promotions match, watertight, saved and
// reopened alike.
TEST(focus_detail_widened_range_rescales_streamed_tiles) {
    const std::string path = writeSmall("widen.sfdetail");
    const std::vector<int16_t> ref = fullValues(path);
    Mesh m;
    std::string err;
    auto fd = FocusDetail::open(path, m, err);
    CHECK(fd != nullptr && !ref.empty());
    if (!fd || ref.empty()) return;
    MicroMesh& mm = *m.micro;
    std::vector<uint32_t> parked, full;
    for (uint32_t t = 0; t < mm.baseTriangles; t += 5) parked.push_back(t);
    for (uint32_t t = 2; t < mm.baseTriangles; t += 5) full.push_back(t);
    fd->ensureFull(mm, parked, true);
    fd->budgetBytes = 0; // demoted while dirty: parked (compressed in memory)
    for (int f = 0; f < 12; ++f) fd->update(m, FocusView{}, FocusBrush{}, 1000.0);
    CHECK(fd->stats().parkedTiles > 0);
    fd->budgetBytes = size_t(1) << 30;
    fd->ensureFull(mm, full, true); // full and dirty; the rest coarse, read from the file
    const int div = mm.widen(mm.rangeWorld() * 3.0f);
    CHECK(div >= 4);
    CHECK(edgesShared(m));
    CHECK(fd->unsaved());
    auto expected = ref;
    for (int16_t& v : expected) v = int16_t(int(v) / div);
    CHECK(streamedFull(m) == expected);
    CHECK(fd->save(m, err));
    CHECK(!fd->unsaved());
    CHECK(fullValues(path) == expected);
}

// Divide to Micro beyond the budget: create() writes the live level to a detail file and streams it; Save As writes
// a compact copy and continues there; saves past half garbage compact; attach() opens a file for a project's Part.
TEST(focus_detail_create_save_as_compaction_and_attach) {
    const std::string src = writeSmall("create-src.sfdetail");
    const std::vector<int16_t> ref = fullValues(src);
    Mesh m;
    std::string err;
    {
        auto fd = FocusDetail::open(src, m, err);
        CHECK(fd != nullptr);
        if (!fd) return;
        fd->ensureFull(*m.micro, allTiles(m), false);
        m.micro->focus.reset(); // a level in memory, as Divide to Micro leaves it
    }
    CHECK(m.micro->disp.toVector() == ref);
    const std::string a = focusTempPath("create-a.sfdetail"), b = focusTempPath("create-b.sfdetail");
    auto fd = FocusDetail::create(a, m, err);
    CHECK(fd != nullptr);
    if (!fd) return;
    CHECK(m.micro->focus == fd && fd->stats().fullTiles == 0);
    CHECK(edgesShared(m));
    Document doc = makeDoc(std::move(m));
    dabAt(doc, BrushType::Clay, "Clay", glm::vec3(0.0f, 0.1f, 5.0f), 0.1f, 0.8f);
    CHECK(fd->unsaved());
    const std::vector<int16_t> edited = streamedFull(doc.mesh);
    CHECK(edited != ref);
    CHECK(fd->saveAs(doc.mesh, b, err));
    CHECK(fd->path() == b && !fd->unsaved());
    CHECK(fullValues(a) == ref);    // the old file stays as it was
    CHECK(fullValues(b) == edited);
    // Saves that replace most of the file compact it (written beside it, renamed over it).
    const size_t compactSize = std::filesystem::file_size(b);
    for (int k = 0; k < 4; ++k) {
        doc.mesh.micro->focus->ensureAllFull(*doc.mesh.micro, true);
        CHECK(fd->save(doc.mesh, err));
        CHECK(std::filesystem::file_size(b) < compactSize * 2 + 4096);
    }
    CHECK(!std::filesystem::exists(b + ".part"));
    CHECK(fullValues(b) == edited);
    // attach: a Part with the same base triangles streams the file; another mesh is refused.
    Mesh part;
    part.positions = doc.mesh.positions;
    part.triangles = doc.mesh.triangles;
    part.rebuildTopology();
    part.recomputeAllNormals();
    auto fd2 = FocusDetail::attach(b, part, {}, err);
    CHECK(fd2 != nullptr);
    if (fd2) CHECK(streamedFull(part) == edited);
    Mesh other;
    other.positions = part.positions;
    other.triangles.assign(part.triangles.begin(), part.triangles.end() - 1);
    CHECK(FocusDetail::attach(b, other, {}, err) == nullptr);
    CHECK(!other.micro);
}

// A project names its streamed Parts' detail files: Save writes <stem>.sfdetail beside it (a compact copy the first
// time, in place after that) and loading streams from it; autosave only names the file; a missing file is refused.
TEST(focus_detail_project_names_its_detail_file) {
    const std::string src = writeSmall("proj-src.sfdetail");
    Mesh m;
    std::string err;
    auto fd = FocusDetail::open(src, m, err);
    CHECK(fd != nullptr);
    if (!fd) return;
    Document doc = makeDoc(std::move(m));
    dabAt(doc, BrushType::Standard, "Standard", glm::vec3(0.1f, 0.0f, 5.0f), 0.1f, 0.7f);
    const std::vector<int16_t> edited = streamedFull(doc.mesh);
    const auto dir = std::filesystem::path(focusTempPath("proj"));
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    const std::string proj = (dir / "head.sfproj").string(), detail = (dir / "head.sfdetail").string();
    CHECK(saveProject(proj, doc, err));
    CHECK(std::filesystem::exists(detail));
    CHECK(fd->path() == detail);
    // Edit, save again: in place (incremental).
    dabAt(doc, BrushType::Inflate, "Inflate", glm::vec3(-0.1f, 0.1f, 5.0f), 0.08f, 0.6f);
    const std::vector<int16_t> edited2 = streamedFull(doc.mesh);
    CHECK(saveProject(proj, doc, err));
    CHECK(fd->path() == detail);
    Document back;
    CHECK(loadProject(proj, back, err));
    CHECK(back.mesh.micro && back.mesh.micro->focus);
    if (back.mesh.micro && back.mesh.micro->focus) {
        CHECK(back.mesh.micro->focus->stats().fullTiles == 0); // streamed: opened coarse
        CHECK(streamedFull(back.mesh) == edited2);
    }
    // Autosave names the file without writing it.
    dabAt(doc, BrushType::Clay, "Clay", glm::vec3(0.0f, -0.1f, 5.0f), 0.08f, 0.6f);
    const auto stamp = std::filesystem::last_write_time(detail);
    const std::string autosave = (dir / "autosave.sfproj").string();
    setProjectAutosaveFolder(dir.string()); // (the app's own autosave may name its detail file by path)
    CHECK(saveProject(autosave, doc, err, true));
    CHECK(std::filesystem::last_write_time(detail) == stamp);
    CHECK(!std::filesystem::exists(dir / "autosave.sfdetail"));
    CHECK(fd->unsaved());
    Document recovered;
    CHECK(loadProject(autosave, recovered, err));
    if (recovered.mesh.micro && recovered.mesh.micro->focus) CHECK(streamedFull(recovered.mesh) == edited2);
    // The same autosave anywhere else (a shared project naming a file by path) is refused.
    setProjectAutosaveFolder(focusTempPath("elsewhere"));
    Document refused;
    CHECK(!loadProject(autosave, refused, err));
    CHECK(err == "Detail file name is invalid");
    setProjectAutosaveFolder("");
    // Missing detail file: a clear error.
    back = Document();
    recovered = Document();
    std::filesystem::rename(detail, detail + ".moved");
    Document none;
    CHECK(!loadProject(proj, none, err));
    CHECK(err.find("head.sfdetail") != std::string::npos);
    (void)edited;
}

// A shared project can't make us open other files: its detail file is a plain name beside it ending in .sfdetail,
// and the file must be a regular file reached without a symlink (a FIFO or a device is refused without blocking).
TEST(focus_detail_project_detail_names_are_confined) {
    const std::string src = writeSmall("confined-src.sfdetail");
    Mesh m;
    std::string err;
    auto fd = FocusDetail::open(src, m, err);
    CHECK(fd != nullptr);
    if (!fd) return;
    Document doc = makeDoc(std::move(m));
    const auto dir = std::filesystem::path(focusTempPath("confined"));
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir / "a");
    const std::string proj = (dir / "head.sfproj").string();
    CHECK(saveProject(proj, doc, err));
    std::filesystem::copy_file(dir / "head.sfdetail", dir / "a" / "b.sfdetail");
    std::filesystem::copy_file(dir / "head.sfdetail", dir.parent_path() / "x.sfdetail",
                               std::filesystem::copy_options::overwrite_existing);
    auto withDetail = [&](const std::string& name) { // the project naming `name`; true when it loads
        std::vector<ZipEntry> entries;
        std::string e;
        CHECK(readStoredZip(proj, entries, e));
        for (auto& z : entries)
            if (z.name == "manifest.json") {
                std::string text(z.data.begin(), z.data.end());
                const size_t at = text.find("\"head.sfdetail\"");
                CHECK(at != std::string::npos);
                if (at != std::string::npos) text.replace(at, 15, jsonQuote(name));
                z.data.assign(text.begin(), text.end());
            }
        const std::string copy = (dir / "named.sfproj").string();
        CHECK(writeStoredZip(copy, entries, 1, e));
        Document d;
        const bool ok = loadProject(copy, d, err);
        if (ok) CHECK(d.mesh.micro && d.mesh.micro->focus);
        return ok;
    };
    CHECK(withDetail("head.sfdetail"));
    for (const std::string bad : {(dir / "head.sfdetail").string(), std::string("../x.sfdetail"), std::string("a/b.sfdetail"),
                                  std::string("a\\b.sfdetail"), std::string("head.sfproj"), std::string(".sfdetail"),
                                  std::string("/dev/zero")}) {
        CHECK(!withDetail(bad));
        CHECK(err == "Detail file name is invalid");
    }
    // A symlink beside the project, a FIFO, a directory: refused when opened (no hang on the FIFO).
    std::filesystem::create_symlink(dir / "head.sfdetail", dir / "link.sfdetail");
    CHECK(!withDetail("link.sfdetail"));
    CHECK(::mkfifo((dir / "pipe.sfdetail").c_str(), 0600) == 0);
    CHECK(!withDetail("pipe.sfdetail"));
    std::filesystem::create_directories(dir / "dir.sfdetail");
    CHECK(!withDetail("dir.sfdetail"));
    // Devices: FocusDetail itself only maps regular files.
    Mesh none;
    CHECK(FocusDetail::open("/dev/zero", none, err) == nullptr);
    CHECK(FocusDetail::open((dir / "pipe.sfdetail").string(), none, err) == nullptr);
}

// Divide to Micro past the memory budget streams the new level; undo and redo swap it whole.
TEST(focus_detail_divide_to_micro_switches_when_large) {
    setenv("SF_FOCUS_DETAIL", "1", 1); // (the budget is hundreds of millions of micro triangles)
    setenv("SF_MICRO_SWAP_DIR", focusTempPath("swap").c_str(), 1);
    Mesh base;
    {
        Mesh head;
        std::string err;
        CHECK(FocusDetail::open(writeSmall("divide.sfdetail"), head, err) != nullptr);
        base.positions = head.positions;
        base.triangles = head.triangles;
        base.rebuildTopology();
        base.recomputeAllNormals();
    }
    Document doc = makeDoc(std::move(base));
    std::string msg;
    CHECK(microDivideUndoable(doc, 5, msg));
    unsetenv("SF_FOCUS_DETAIL");
    unsetenv("SF_MICRO_SWAP_DIR");
    CHECK(doc.mesh.micro && doc.mesh.micro->focus);
    CHECK(msg.find("Focus Detail") != std::string::npos);
    if (!doc.mesh.micro || !doc.mesh.micro->focus) return;
    const std::string file = doc.mesh.micro->focus->path();
    CHECK(file.find("unsaved-") != std::string::npos);
    dabAt(doc, BrushType::Clay, "Clay", glm::vec3(0.0f, 0.1f, 5.0f), 0.1f, 0.8f);
    doc.undo.undo(doc.mesh, doc.bvh); // the stroke
    doc.undo.undo(doc.mesh, doc.bvh); // the divide
    CHECK(!doc.mesh.micro);
    doc.undo.redo(doc.mesh, doc.bvh);
    CHECK(doc.mesh.micro && doc.mesh.micro->focus);
    CHECK(!microWantsFocus(size_t(100) << 20, 5));
    CHECK(microWantsFocus(size_t(1) << 30, 7));
    CHECK(!microWantsFocus(size_t(1) << 30, FocusDetail::kCoarseLevel));
}

// A micro level in memory still saves as format 7 (no detail file) and loads back exactly: format 9 is written
// only when a level streams, so builds before 0.11.12 keep opening these projects.
TEST(focus_detail_in_memory_micro_project_stays_format_7) {
    setenv("SF_FOCUS_DETAIL", "0", 1);
    Mesh base;
    {
        Mesh head;
        std::string err;
        CHECK(FocusDetail::open(writeSmall("format7.sfdetail"), head, err) != nullptr);
        base.positions = head.positions;
        base.triangles = head.triangles;
        base.rebuildTopology();
        base.recomputeAllNormals();
    }
    Document doc = makeDoc(std::move(base));
    std::string msg, err;
    CHECK(microDivideUndoable(doc, 5, msg));
    unsetenv("SF_FOCUS_DETAIL");
    CHECK(doc.mesh.micro && !doc.mesh.micro->focus);
    if (!doc.mesh.micro) return;
    dabAt(doc, BrushType::Clay, "Clay", glm::vec3(0.0f, 0.1f, 5.0f), 0.1f, 0.8f);
    const auto dir = std::filesystem::path(focusTempPath("format7"));
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    const std::string proj = (dir / "head.sfproj").string();
    CHECK(saveProject(proj, doc, err));
    std::vector<ZipEntry> entries;
    CHECK(readStoredZip(proj, entries, err));
    Json manifest;
    for (const auto& e : entries)
        if (e.name == "manifest.json") CHECK(Json::parse(std::string(e.data.begin(), e.data.end()), manifest, err));
    CHECK(manifest["version"].asNumber() == 7);
    CHECK(manifest.dump().find(".sfdetail") == std::string::npos);
    CHECK(!std::filesystem::exists(dir / "head.sfdetail"));
    Document back;
    CHECK(loadProject(proj, back, err));
    CHECK(back.mesh.micro && !back.mesh.micro->focus);
    if (back.mesh.micro) CHECK(back.mesh.micro->disp == doc.mesh.micro->disp);
}

// Every micro brush writes on a streamed level exactly what it writes on the same level in memory (the tiles it
// touches are made full first), including a Move and a whole-surface step (noise) with its undo.
TEST(focus_detail_every_micro_brush_matches_in_memory) {
    const std::string path = writeSmall("brushes.sfdetail");
    std::string err;
    Mesh a, b;
    auto fdA = FocusDetail::open(path, a, err);
    CHECK(fdA != nullptr);
    if (!fdA) return;
    {
        auto fdB = FocusDetail::open(path, b, err);
        fdB->ensureFull(*b.micro, allTiles(b), false);
        b.micro->focus.reset();
    }
    Document streamed = makeDoc(std::move(a)), memory = makeDoc(std::move(b));
    const struct {
        BrushType type;
        const char* name;
    } brushes[] = {{BrushType::Standard, "Standard"}, {BrushType::Clay, "Clay"},       {BrushType::ClayBlock, "ClayBlock"},
                   {BrushType::Smooth, "Smooth"},     {BrushType::Inflate, "Inflate"}, {BrushType::Flatten, "Flatten"},
                   {BrushType::Trim, "Trim"},         {BrushType::Layer, "Layer"},     {BrushType::Pinch, "Pinch"},
                   {BrushType::Crease, "Crease"},     {BrushType::SharpCrease, "SharpCrease"}};
    int k = 0;
    for (const auto& br : brushes) {
        CHECK(microBrushSupport(br.type) > 0);
        const glm::vec3 from(0.3f * std::sin(float(k)), 0.3f * std::cos(float(k)), 5.0f);
        dabAt(streamed, br.type, br.name, from, 0.09f, 0.7f, &memory);
        dabAt(memory, br.type, br.name, from, 0.09f, 0.7f);
        ++k;
        const bool same = streamedFull(streamed.mesh) == memory.mesh.micro->disp.toVector();
        if (!same) std::fprintf(stderr, "  differs after %s\n", br.name);
        CHECK(same);
    }
    CommandRegistry reg;
    CHECK(reg.execute(streamed, "micro_noise", {{"amplitude", 0.002}}).ok);
    CHECK(reg.execute(memory, "micro_noise", {{"amplitude", 0.002}}).ok);
    CHECK(streamedFull(streamed.mesh) == memory.mesh.micro->disp.toVector());
    streamed.undo.undo(streamed.mesh, streamed.bvh);
    memory.undo.undo(memory.mesh, memory.bvh);
    CHECK(streamedFull(streamed.mesh) == memory.mesh.micro->disp.toVector());
    CHECK(edgesShared(streamed.mesh));
}

#endif
