// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The Focus Detail benchmark (core/FocusDetail.h: runFocusBenchmark; sculptforge-cli --bench-focus).
#include "core/Benchmark.h"
#include "core/BVH.h"
#include "core/Document.h"
#include "core/FocusDetail.h"
#include "core/Micro.h"
#include "core/MicroStore.h"
#include "core/Sculpt.h"
#include "core/ThreadPool.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <sstream>

namespace sf {

namespace {

using Clock = std::chrono::steady_clock;
double msSince(Clock::time_point t) { return std::chrono::duration<double, std::milli>(Clock::now() - t).count(); }

std::string fmt(const char* f, double v) {
    char b[64];
    std::snprintf(b, sizeof b, f, v);
    return b;
}
std::string mb(double bytes) { return fmt("%.0f MB", bytes / 1e6); }

double percentile(std::vector<double> v, double p) {
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    return v[std::min(v.size() - 1, size_t(std::ceil(p * double(v.size()))) - (p > 0 ? 1 : 0))];
}

// A 1920 x 1080 view, 40 degrees vertical, from `eye` towards `target`.
FocusView makeView(const glm::vec3& eye, const glm::vec3& target, float errorPx = 0.5f) {
    const float fov = glm::radians(40.0f), w = 1920.0f, h = 1080.0f;
    FocusView v;
    v.valid = true;
    v.eye = eye;
    v.viewProj = glm::perspective(fov, w / h, 0.01f, 100.0f) * glm::lookAt(eye, target, glm::vec3(0, 1, 0));
    v.focalPx = h / (2.0f * std::tan(fov / 2.0f));
    v.errorPx = errorPx;
    return v;
}

struct StrokeResult {
    std::vector<double> dab, frame; // per dab: the brush (with any promotion it waited for); dab + update
    uint64_t syncPromoted = 0;
    double undoMs = 0, redoMs = 0;
    size_t undoBytes = 0;
};

// A figure eight over the face, `dabs` dabs, 240 samples a second. The focus is updated before each dab with the
// brush's position and velocity (as the app does once a frame), unless `predict` is off (then only where the
// brush is now, no lookahead).
StrokeResult runStroke(Document& doc, FocusDetail& fd, BrushType type, bool move, float radius, int dabs, bool predict) {
    StrokeResult r;
    Mesh& m = doc.mesh;
    Sculptor& sc = doc.sculptor;
    sc.beginStroke(m, move ? "Move" : "Clay");
    Symmetry sym; // X on, as the app starts
    const FocusView view = makeView(glm::vec3(0.0f, 0.1f, 2.2f), glm::vec3(0.0f, 0.1f, 0.0f));
    const uint64_t sync0 = fd.stats().syncPromoted;
    const float lookahead = fd.lookaheadSeconds;
    if (!predict) fd.lookaheadSeconds = 0.0f;
    glm::vec3 prev(0.0f);
    bool havePrev = false;
    if (predict) { // the cursor hovers over the start before the pen goes down (a few frames)
        RayHit hit;
        if (surfaceRaycast(m, doc.bvh, glm::vec3(0.0f, 0.1f, 5.0f), glm::vec3(0, 0, -1), hit)) {
            FocusBrush b;
            b.present = true;
            b.position = hit.position;
            b.radius = radius;
            b.mirrorX = sym.x;
            for (int f = 0; f < 3; ++f) fd.update(m, view, b, 4.0);
        }
    }
    for (int i = 0; i < dabs; ++i) {
        const float a = float(i) / float(dabs) * 6.2831853f * 1.5f;
        const float x = 0.32f * std::sin(a), y = 0.1f + 0.22f * std::sin(2.0f * a);
        RayHit hit;
        if (!surfaceRaycast(m, doc.bvh, glm::vec3(x, y, 5.0f), glm::vec3(0, 0, -1), hit)) continue;
        FocusBrush b;
        b.present = true;
        b.position = hit.position;
        b.velocity = havePrev ? (hit.position - prev) * 240.0f : glm::vec3(0.0f);
        b.radius = radius;
        b.mirrorX = sym.x;
        prev = hit.position;
        havePrev = true;
        const auto tf = Clock::now();
        fd.update(m, view, b, 2.0);
        const auto td = Clock::now();
        DabParams p;
        p.center = hit.position;
        p.radius = radius;
        p.strength = move ? 0.25f : 0.35f;
        if (move) {
            if (i == 0) sc.beginGrab(m, doc.bvh, p, sym);
            else sc.grab(m, doc.bvh, glm::vec3(0.0f, 0.0f, 0.0008f));
        } else {
            sc.dab(m, doc.bvh, type, p, sym);
        }
        r.dab.push_back(msSince(td));
        r.frame.push_back(msSince(tf));
        if (move && i == dabs / 3) break; // a Move stroke grabs once and drags: a third of the path
    }
    fd.lookaheadSeconds = lookahead;
    r.syncPromoted = fd.stats().syncPromoted - sync0;
    UndoRecord rec = sc.endStroke();
    r.undoBytes = rec.approxBytes();
    doc.undo.push(std::move(rec));
    auto t = Clock::now();
    doc.undo.undo(m, doc.bvh);
    r.undoMs = msSince(t);
    t = Clock::now();
    doc.undo.redo(m, doc.bvh);
    r.redoMs = msSince(t);
    m.micro->clearDirty();
    return r;
}

} // namespace

std::string runFocusBenchmark(const BenchOptions& o) {
    auto log = [&](const std::string& s) {
        if (o.log) o.log(s);
    };
    std::ostringstream md;
    namespace fs = std::filesystem;
    std::string err, path;
    double writeMs = 0;
    auto tw = Clock::now();
    if (o.focusBase == 0) {
        path = focusStandard1bPath(err, log);
    } else {
        path = (fs::path(MicroStore::swapFolder()) / ("bench-focus-" + std::to_string(o.focusBase) + ".sfdetail")).string();
        std::error_code ec;
        fs::create_directories(MicroStore::swapFolder(), ec);
        if (!focusWriteSyntheticHead(path, o.focusBase, o.microLevel, err)) path.clear();
    }
    writeMs = msSince(tw);
    if (path.empty()) return "Focus Detail benchmark: " + err + "\n";
    const double fileBytes = double(fs::file_size(path));
    md << "Threads: " << ThreadPool::shared().threads() << ". Asset: `" << fs::path(path).filename().string() << "`, "
       << mb(fileBytes) << " on disk";
    if (writeMs > 2000) md << " (written in " << fmt("%.1f s", writeMs / 1000) << ")";
    md << ".\n\n";

    // --- Open ---
    const size_t rss0 = resetPeakResidentBytes();
    log("Opening " + path);
    auto t = Clock::now();
    Mesh m0;
    std::shared_ptr<FocusDetail> fd = FocusDetail::open(path, m0, err);
    if (!fd) return "Focus Detail benchmark: " + err + "\n";
    const double openMs = msSince(t);
    t = Clock::now();
    Document doc;
    doc.setMesh(std::move(m0));
    const double docMs = msSince(t);
    Mesh& m = doc.mesh;
    MicroMesh& mm = *m.micro;
    m.micro->clearDirty();
    const size_t rssOpen = residentBytes();
    const double points = double(mm.baseTriangles) * double(mm.samples);
    md << "### " << fmt("%.3f", double(mm.microTriangles()) / 1e9) << " billion micro triangles (base " << mm.baseTriangles
       << " triangles, micro level " << mm.level << ", " << fmt("%.0f", points / 1e6) << "M grid points)\n\n";
    md << "| Step | Result |\n|---|---:|\n";
    md << "| Open (file, base, coarse grids, upsample) | " << fmt("%.0f ms", openMs) << " |\n";
    md << "| Scene setup (BVH, topology) | " << fmt("%.0f ms", docMs) << " |\n";
    md << "| **Open total** | **" << fmt("%.2f s", (openMs + docMs) / 1000) << "** |\n";
    md << "| RSS after open | " << mb(double(rssOpen)) << " (+" << mb(double(rssOpen - std::min(rssOpen, rss0))) << ") |\n";
    md << "| Bytes on disk per micro triangle | " << fmt("%.2f", fileBytes / double(mm.microTriangles())) << " |\n";

    // --- Promotion latency: one tile at a time (a brush waiting), then in parallel batches ---
    {
        std::vector<double> one;
        for (uint32_t k = 0; k < 256; ++k) {
            const uint32_t tile = uint32_t((uint64_t(k) * 2654435761u) % mm.baseTriangles);
            if (fd->tileLevel(tile)) continue;
            const uint32_t tt[1] = {tile};
            t = Clock::now();
            fd->ensureFull(mm, tt, false);
            one.push_back(msSince(t));
        }
        md << "| Promote one tile (decompress + edges + markDirty), p50 / p99 | " << fmt("%.3f", percentile(one, 0.5)) << " / "
           << fmt("%.3f ms", percentile(one, 0.99)) << " |\n";
    }
    m.micro->clearDirty();

    // --- Orbit: a full turn close to the face, then a zoom into it; the focus follows the camera ---
    log("Orbit");
    fd->budgetBytes = size_t(512) << 20;
    std::vector<double> upd;
    const FocusStats s0 = fd->stats();
    const int frames = 240;
    size_t maxPending = 0;
    for (int f = 0; f < frames; ++f) {
        const float a = float(f) / float(frames) * 6.2831853f;
        const float dist = f < frames / 2 ? 2.2f : 2.2f - 1.4f * float(f - frames / 2) / float(frames / 2);
        const FocusView v = makeView(glm::vec3(dist * std::sin(a), 0.1f, dist * std::cos(a)), glm::vec3(0.0f, 0.1f, 0.0f));
        fd->update(m, v, FocusBrush{}, 4.0);
        upd.push_back(fd->stats().lastUpdateMs);
        maxPending = std::max(maxPending, fd->stats().lastPending);
        m.micro->clearDirty(); // (what the renderer's sync does)
    }
    const FocusStats s1 = fd->stats();
    const double orbitTiles = double(s1.promoted - s0.promoted);
    md << "| Orbit + zoom (" << frames << " frames, 4 ms budget): update p50 / p99 / max | " << fmt("%.2f", percentile(upd, 0.5))
       << " / " << fmt("%.2f", percentile(upd, 0.99)) << " / " << fmt("%.2f ms", percentile(upd, 1.0)) << " |\n";
    md << "| Orbit: tiles promoted / demoted, most waiting in a frame | " << fmt("%.0f", orbitTiles) << " / "
       << fmt("%.0f", double(s1.demoted - s0.demoted)) << ", " << maxPending << " |\n";
    md << "| Promotion throughput (parallel) | " << fmt("%.0f tiles/s", double(s1.promoted) / std::max(s1.promoteMs, 1e-3) * 1000.0)
       << " = " << fmt("%.0f M micro triangles/s", double(s1.promoted) * double(mm.n * mm.n) / std::max(s1.promoteMs, 1e-3) / 1e3) << " |\n";
    md << "| Full tiles after the zoom | " << s1.fullTiles << " of " << s1.tiles << " (" << mb(double(s1.fullBytes)) << ", budget "
       << mb(double(fd->budgetBytes)) << ") |\n";
    md << "| RSS, focus on | " << mb(double(residentBytes())) << " |\n\n";

    // --- Strokes ---
    md << "| Stroke (X symmetry, 240 Hz) | dabs | dab p50 | **dab p99** | max | dab + focus update p99 | tiles promoted while waiting | undo | redo | undo step |\n";
    md << "|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|\n";
    struct Row {
        const char* name;
        BrushType type;
        bool move;
        float radius;
        bool predict;
        bool alongNormals = true; // Move: the micro surface along its normals (writes tiles), else the base cage
    };
    const Row rows[] = {{"Clay r=0.03 (~25 px)", BrushType::Clay, false, 0.03f, true},
                        {"Clay r=0.08 (~60 px)", BrushType::Clay, false, 0.08f, true},
                        {"Clay r=0.16 (~120 px)", BrushType::Clay, false, 0.16f, true},
                        {"Move r=0.1 (~80 px), micro surface", BrushType::Move, true, 0.10f, true},
                        {"Move r=0.1, base cage (default)", BrushType::Move, true, 0.10f, true, false},
                        {"Clay r=0.08, no prediction", BrushType::Clay, false, 0.08f, false}};
    int stroke = 0;
    for (const Row& row : rows) {
        if (!o.brushFilter.empty() && std::string(row.name).find(o.brushFilter) == std::string::npos) continue;
        log(std::string("  ") + row.name);
        // Each stroke starts on a cold region: demote everything first (a fresh part of the model).
        fd->budgetBytes = 1;
        fd->update(m, FocusView{}, FocusBrush{}, 1e9);
        fd->update(m, FocusView{}, FocusBrush{}, 1e9);
        fd->update(m, FocusView{}, FocusBrush{}, 1e9);
        fd->budgetBytes = size_t(512) << 20;
        m.micro->clearDirty();
        doc.sculptor.microMoveAlongNormals = row.alongNormals;
        const StrokeResult r = runStroke(doc, *fd, row.type, row.move, row.radius, std::max(o.dabsPerStroke, 40), row.predict);
        md << "| " << row.name << " | " << r.dab.size() << " | " << fmt("%.2f", percentile(r.dab, 0.5)) << " | **"
           << fmt("%.2f", percentile(r.dab, 0.99)) << "** | " << fmt("%.2f", percentile(r.dab, 1.0)) << " | "
           << fmt("%.2f", percentile(r.frame, 0.99)) << " | " << r.syncPromoted << " | " << fmt("%.1f", r.undoMs) << " | "
           << fmt("%.1f", r.redoMs) << " | " << fmt("%.0f KB", double(r.undoBytes) / 1e3) << " |\n";
        ++stroke;
        if (o.partial) o.partial(md.str());
    }
    md << "\n";

    // --- Save: the changed tiles only ---
    {
        const FocusStats s = fd->stats();
        const double before = double(fs::file_size(path));
        t = Clock::now();
        const bool ok = fd->save(m, err);
        const double saveMs = msSince(t);
        const double after = double(fs::file_size(path));
        md << "| Save | Result |\n|---|---:|\n";
        md << "| Incremental save (" << s.dirtyTiles << " dirty + " << s.parkedTiles << " parked tiles) | "
           << (ok ? fmt("%.0f ms", saveMs) : "failed: " + err) << ", " << fmt("%.1f MB appended", (after - before) / 1e6) << " |\n";
        if (writeMs > 2000) md << "| Writing the whole asset (for comparison) | " << fmt("%.1f s", writeMs / 1000) << " |\n";
    }

    // --- Focus off: every tile decompressed (what a plain micro level holds) ---
    {
        log("Every tile full");
        std::vector<uint32_t> all(mm.baseTriangles);
        for (uint32_t k = 0; k < all.size(); ++k) all[k] = k;
        fd->budgetBytes = size_t(1) << 40;
        t = Clock::now();
        fd->ensureFull(mm, all, false);
        const double allMs = msSince(t);
        m.micro->clearDirty();
        const size_t rssAll = residentBytes(), peak = peakResidentBytes();
        // The renderer's GPU copy (render/MicroRenderer.h): 2 bytes of displacement + 4 of shading per grid point;
        // with per-tile GPU levels (docs/design/perf-moonshots.md #8) coarse tiles would hold their 45 points only.
        const double cs = double((8 + 1) * (8 + 2) / 2), full = double(s1.fullTiles);
        const double gpuNow = points * 6.0, gpuTiled = (double(mm.baseTriangles) - full) * cs * 6.0 + full * double(mm.samples) * 6.0;
        md << "| Every tile decompressed (focus off) | " << fmt("%.2f s", allMs / 1000) << " |\n";
        md << "| RSS, every tile full | " << mb(double(rssAll)) << " (peak " << mb(double(peak)) << ") |\n";
        md << "| GPU buffers (estimate), today's renderer | " << mb(gpuNow) << " |\n";
        md << "| GPU buffers (estimate), per-tile levels with this focus | " << mb(gpuTiled) << " |\n";
    }
    return md.str();
}

} // namespace sf
