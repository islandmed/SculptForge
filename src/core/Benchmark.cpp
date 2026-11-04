// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/Benchmark.h"

#include "core/BrushRecipe.h"
#include "core/Commands.h"
#include "core/Document.h"
#include "core/Micro.h"
#include "core/FocusDetail.h"
#include "core/Multires.h"
#include "core/Polypaint.h"
#include "core/Primitives.h"
#include "core/Project.h"
#include "core/Texture.h"
#include "core/ThreadPool.h"
#include "core/UploadOrder.h"

#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <sstream>

#if defined(__linux__)
#include <unistd.h>
#include <fstream>
#elif defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <psapi.h> // (K32GetProcessMemoryInfo lives in kernel32: no extra library)
#elif defined(__APPLE__)
#include <mach/mach.h>
#endif
#include <atomic>

namespace sf {

namespace {

std::string fmt(const char* f, double v) {
    char buf[64];
    std::snprintf(buf, sizeof buf, f, v);
    return buf;
}

std::string millions(double n) { return n >= 1e6 ? fmt("%.1fM", n / 1e6) : fmt("%.0fk", n / 1e3); }

} // namespace

namespace {
// Windows and macOS can't reset the process's peak: after resetPeakResidentBytes the peak is the most any
// residentBytes() call saw since (proof mode samples it every frame), before it the system's own peak.
std::atomic<size_t> g_sampledPeak{0};
std::atomic<bool> g_peakReset{false};
} // namespace

size_t residentBytes() {
    size_t bytes = 0;
#if defined(__linux__)
    std::ifstream f("/proc/self/statm");
    size_t pages = 0, resident = 0;
    if (f >> pages >> resident) bytes = resident * size_t(sysconf(_SC_PAGESIZE));
#elif defined(_WIN32)
    PROCESS_MEMORY_COUNTERS pmc{};
    if (K32GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof pmc)) bytes = size_t(pmc.WorkingSetSize);
#elif defined(__APPLE__)
    mach_task_basic_info info{};
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, task_info_t(&info), &count) == KERN_SUCCESS)
        bytes = size_t(info.resident_size);
#endif
    for (size_t peak = g_sampledPeak.load(std::memory_order_relaxed);
         bytes > peak && !g_sampledPeak.compare_exchange_weak(peak, bytes, std::memory_order_relaxed);) {
    }
    return bytes;
}

size_t resetPeakResidentBytes() {
#if defined(__linux__)
    std::ofstream("/proc/self/clear_refs") << "5"; // resets the peak (VmHWM) to the current RSS
#endif
    g_sampledPeak.store(0, std::memory_order_relaxed);
    g_peakReset.store(true, std::memory_order_relaxed);
    return residentBytes();
}

size_t peakResidentBytes() {
#if defined(__linux__)
    std::ifstream f("/proc/self/status");
    for (std::string line; std::getline(f, line);)
        if (line.rfind("VmHWM:", 0) == 0) return size_t(std::atoll(line.c_str() + 6)) * 1024;
    return 0;
#else
    const size_t now = residentBytes();
    if (g_peakReset.load(std::memory_order_relaxed)) return std::max(now, g_sampledPeak.load(std::memory_order_relaxed));
#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS pmc{};
    if (K32GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof pmc)) return size_t(pmc.PeakWorkingSetSize);
#elif defined(__APPLE__)
    mach_task_basic_info info{};
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, task_info_t(&info), &count) == KERN_SUCCESS)
        return size_t(info.resident_size_max);
#endif
    return now;
#endif
}

MemoryUse documentMemory(const Document& doc) {
    MemoryUse u;
    std::vector<const PaintTexture*> seen; // textures shared between SubTools count once
    for (int i = 0; i < doc.subtoolCount(); ++i) {
        const bool active = i == doc.activeSubtool;
        const SubTool& st = doc.subtools[size_t(i)];
        const Mesh& m = doc.subtoolMesh(i);
        u.mesh += m.memoryBytes();
        if (m.texture && std::find(seen.begin(), seen.end(), m.texture.get()) == seen.end()) {
            seen.push_back(m.texture.get());
            u.textures += m.texture->colourBytes();
        }
        u.bvh += (active ? doc.bvh : st.bvh).memoryBytes();
        u.levels += subdivBytes(m);
        u.undo += (active ? doc.undo : st.undo).undoBytes();
        u.triangles += m.triangleCount();
        u.vertices += m.vertexCount();
        if (st.visible) // the renderer's cluster layout (core/UploadOrder.h): packed vertices (12 bytes, + colour 4,
                        // mask 1), about a tenth more for the copies on cluster seams; 16-bit indices and polygroups
            u.gpu += m.vertexCount() * (12 + (m.hasColors() ? 4 : 0) + (m.hasMask() ? 1 : 0)) * 11 / 10 +
                     m.triangleCount() * (6 + (m.hasPolygroups() ? 2 : 0));
    }
    // Material channel tiles from the atomic count (every texture, copies and undo steps): no tile walk here, so
    // the readout is safe whatever else is painting.
    u.channels = TileImage::liveBytes();
    u.textures += u.channels;
    u.paintCaches = texturePaintCacheBytes();
    return u;
}

void registerPerformanceCommands(CommandRegistry& registry) {
    registry.add({"perf_info",
                  "Performance information: the threads brushes use, and the memory the scene holds (meshes, BVHs, "
                  "subdivision levels, undo history, GPU buffers), per Part and in total.",
                  {},
                  [](Document& doc, const Args&) -> CommandResult {
                      auto mb = [](size_t b) { return fmt("%.1f MB", double(b) / 1e6); };
                      const MemoryUse u = documentMemory(doc);
                      std::string msg = "Threads " + std::to_string(ThreadPool::shared().threads()) +
                                        (ThreadPool::shared().serial() ? " (multithreading off)" : "") + "; " +
                                        millions(double(u.triangles)) + " triangles; memory " + mb(u.total()) + " (meshes " +
                                        mb(u.mesh) + ", BVH " + mb(u.bvh) + ", subdivision levels " + mb(u.levels) +
                                        ", undo " + mb(u.undo) + ", textures " + mb(u.textures) + " of which material channels " + mb(u.channels) + ", texture painting caches " + mb(u.paintCaches) +
                                        "), GPU buffers about " + mb(u.gpu);
                      return {true, msg};
                  }});
    registry.add({"new_bench_sphere",
                  "Replace the scene with the benchmark's sphere of about `millions` million triangles (a cube made "
                  "round, then divided: subdivision levels like a real sculpt), for measuring brushes on heavy meshes.",
                  {{"millions", ParamSpec::Type::Number, "Triangles in millions (0.01-200, default 5).", false}},
                  [](Document& doc, const Args& a) -> CommandResult {
                      const double m = std::clamp(argNumber(a, "millions", 5.0), 0.01, 200.0);
                      doc.setMesh(makeBenchSphere(size_t(m * 1e6)), "Sphere");
                      doc.path.clear();
                      return {true, "Created benchmark sphere with " + std::to_string(doc.mesh.triangleCount()) + " triangles"};
                  }});
    registry.add({"set_multithreading",
                  "Run brushes, normal and BVH updates, project saving and other per-dab work on every core (on, the "
                  "default) or on one thread (off: slower, for comparing or troubleshooting). Results are the same.",
                  {{"enabled", ParamSpec::Type::Bool, "true = every core, false = one thread.", true}},
                  [](Document&, const Args& a) -> CommandResult {
                      const bool on = argBool(a, "enabled", true);
                      ThreadPool::shared().setSerial(!on);
                      return {true, on ? "Multithreading on (" + std::to_string(ThreadPool::shared().threads()) + " threads)"
                                       : std::string("Multithreading off (one thread)")};
                  }});
}

namespace {

// A cube of r x r quads per side has 12 r^2 triangles; each Divide multiplies that by 4.
void benchResolution(size_t triangles, int& res, int& levels) {
    for (levels = 1; levels <= 8; ++levels) {
        res = std::max(1, int(std::lround(std::sqrt(double(triangles) / (12.0 * std::pow(4.0, levels))))));
        if (res <= 40) break;
    }
}

// The standard scenes' head, as a displacement of the unit sphere: y up, +z the face. Every feature is broad
// (several base cells wide) so the 15M and 33M bases, which differ in resolution, give the same shape.
glm::vec3 headPoint(glm::vec3 d) {
    auto g = [](float a, float b) { return std::exp(-(a * a + b * b)); }; // a 2D Gaussian
    const float ax = std::abs(d.x);
    float r = 1.0f;
    const float front = std::max(0.0f, d.z);
    r += 0.06f * std::max(0.0f, -d.z) * std::max(0.0f, d.y + 0.2f);                 // the back of the skull
    r += 0.10f * front * g(d.x / 0.40f, (d.y - 0.30f) / 0.08f);                       // brow ridge
    r -= 0.13f * front * g((ax - 0.30f) / 0.14f, (d.y - 0.14f) / 0.11f);              // eye sockets
    r += 0.28f * front * g(d.x / 0.12f, (d.y + 0.02f) / 0.22f);                       // nose
    r += 0.07f * front * g((ax - 0.45f) / 0.16f, (d.y + 0.05f) / 0.11f);              // cheekbones
    r += 0.06f * front * g(d.x / 0.22f, (d.y + 0.33f) / 0.07f);                       // lips
    r += 0.09f * front * g(d.x / 0.20f, (d.y + 0.62f) / 0.12f);                       // chin
    r += 0.20f * g((ax - 0.98f) / 0.10f, d.y / 0.20f) * g((d.z + 0.05f) / 0.16f, 0.0f); // ears
    glm::vec3 p = d * r;
    // Narrower than deep, taller than wide; the jaw tapers below the cheeks, a neck below that.
    p.x *= 0.80f;
    p.z *= 0.95f;
    const float jaw = std::clamp((-d.y - 0.15f) / 0.55f, 0.0f, 1.0f);
    p.x *= 1.0f - 0.18f * jaw * jaw;
    p.z += 0.05f * jaw * front;
    if (d.y < -0.70f) { // the neck: a stub of cylinder under the jaw
        const float t = std::clamp((-d.y - 0.70f) / 0.30f, 0.0f, 1.0f);
        const float k = 1.0f - 0.30f * t;
        p.x *= k;
        p.z = p.z * k - 0.05f * t;
        p.y -= 0.12f * t;
    }
    p.y *= 1.08f;
    return p;
}

Mesh benchMesh(size_t triangles, bool head) {
    int levels = 1, res = 1;
    benchResolution(triangles, res, levels);
    Mesh m = makeCube(res, 2.0f);
    for (auto& p : m.positions) p = head ? headPoint(glm::normalize(p)) : glm::normalize(p);
    m.recomputeAllNormals();
    std::string error;
    for (int i = 0; i < levels; ++i)
        if (!subdivDivide(m, true, error, nullptr, size_t(1) << 40)) break;
    return m;
}

} // namespace

Mesh makeBenchSphere(size_t triangles) { return benchMesh(triangles, false); }
Mesh makeBenchHead(size_t triangles) { return benchMesh(triangles, true); }

namespace {

using Clock = std::chrono::steady_clock;

double msSince(Clock::time_point t) { return std::chrono::duration<double, std::milli>(Clock::now() - t).count(); }

enum class Kind { Brush, Recipe, Move, Mask, Paint };

struct BenchBrush {
    std::string name;
    Kind kind = Kind::Brush;
    BrushType type = BrushType::Standard;
    float radius = 0.12f; // world units on a unit sphere: ~64 px on a 1080p view framing the model
};

struct StrokeStats {
    int dabs = 0;
    double verts = 0, uploaded = 0, runs = 0, kernel = 0, normals = 0, bvh = 0, pick = 0, upload = 0, total = 0;
    double endStroke = 0, undo = 0, redo = 0;
    size_t undoBytes = 0;
    double remesh = 0;     // dynamic topology
    size_t triangles = 0;  // after the stroke
};

StrokeStats runStroke(Document& doc, const BenchBrush& b, int dabs, const BenchGpu* gpu, UploadOrder& order) {
    StrokeStats st;
    Mesh& m = doc.mesh;
    Sculptor& sc = doc.sculptor;
    const std::vector<BrushRecipe> recipes = builtinBrushRecipes();
    const BrushRecipe* recipe = nullptr;
    for (const auto& r : recipes)
        if (r.pattern != BrushRecipe::Pattern::None) recipe = &r;
    if (!recipe && !recipes.empty()) recipe = &recipes.front();
    if (b.kind == Kind::Paint) m.ensureColors();
    sc.beginStroke(m, b.name);
    sc.timings = {};
    Symmetry sym; // X on, as in the app
    // Dabs 0.15 radius apart (the app's spacing) along a line across the front of the sphere.
    const float step = 0.15f * b.radius;
    glm::vec3 last(0.0f);
    std::vector<std::pair<uint32_t, uint32_t>> runs;
    m.clearDirty();
    const auto stroke0 = Clock::now();
    for (int i = 0; i < dabs; ++i) {
        const float x = 0.15f + std::fmod(float(i) * step, 0.6f), y = 0.2f + 0.1f * std::sin(float(i) * 0.3f);
        auto t = Clock::now();
        RayHit hit;
        const bool hitOk = doc.bvh.raycast(m, glm::vec3(x, y, 5.0f), glm::vec3(0, 0, -1), hit);
        st.pick += msSince(t);
        if (!hitOk) continue;
        DabParams p;
        p.center = hit.position;
        p.viewDir = glm::vec3(0, 0, -1);
        p.radius = b.radius;
        p.strength = 0.5f;
        p.strokeDelta = i ? hit.position - last : glm::vec3(0.0f);
        last = hit.position;
        switch (b.kind) {
        case Kind::Brush: sc.dab(m, doc.bvh, b.type, p, sym); break;
        case Kind::Recipe:
            if (recipe) sc.dabRecipe(m, doc.bvh, *recipe, p, sym);
            break;
        case Kind::Move:
            if (i == 0) sc.beginGrab(m, doc.bvh, p, sym);
            else sc.grab(m, doc.bvh, glm::vec3(0.002f, 0.0015f, 0.0f));
            break;
        case Kind::Mask: sc.dabMask(m, doc.bvh, p, sym); break;
        case Kind::Paint: paintDab(sc, m, doc.bvh, p, glm::vec3(0.6f, 0.1f, 0.1f), sym); break;
        }
        // What a partial upload of this dab sends (as the renderer finds it), then the upload itself.
        order.dirtyRuns(m, runs);
        for (const auto& [b, e] : runs) st.uploaded += double(e - b);
        st.runs += double(runs.size());
        if (gpu && gpu->upload) st.upload += gpu->upload(m);
        else m.clearDirty();
        ++st.dabs;
    }
    st.total = msSince(stroke0);
    st.kernel = sc.timings.kernelMs;
    st.normals = sc.timings.normalsMs;
    st.bvh = sc.timings.bvhMs;
    st.remesh = sc.timings.remeshMs;
    st.triangles = m.triangleCount();
    st.verts = double(sc.timings.vertices) / double(std::max(st.dabs, 1)); // per step (both mirrors)
    st.uploaded /= double(std::max(st.dabs, 1));
    st.runs /= double(std::max(st.dabs, 1));

    auto t = Clock::now();
    UndoRecord r = sc.endStroke();
    st.undoBytes = r.approxBytes();
    doc.undo.push(std::move(r));
    st.endStroke = msSince(t);
    t = Clock::now();
    doc.undo.undo(m, doc.bvh);
    st.undo = msSince(t);
    if (gpu && gpu->upload) gpu->upload(m);
    t = Clock::now();
    doc.undo.redo(m, doc.bvh);
    st.redo = msSince(t);
    if (gpu && gpu->upload) gpu->upload(m);
    return st;
}

} // namespace

bool parseBenchArg(int argc, char** argv, int& i, BenchCommandLine& cl) {
    const std::string a = argv[i];
    auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : std::string(); };
    if (a == "--bench") {
        cl.run = true;
    } else if (a == "--bench-sizes") {
        cl.options.sizesMillions.clear();
        std::stringstream ss(next());
        for (std::string item; std::getline(ss, item, ',');)
            if (const double v = std::atof(item.c_str()); v > 0.0) cl.options.sizesMillions.push_back(v);
    } else if (a == "--bench-dabs") {
        cl.options.dabsPerStroke = std::max(1, std::atoi(next().c_str()));
    } else if (a == "--bench-brushes") {
        cl.options.brushFilter = next();
    } else if (a == "--bench-no-project") {
        cl.options.projectRoundTrip = false;
    } else if (a == "--bench-view") {
        cl.options.viewOnly = true;
        cl.options.projectRoundTrip = false;
    } else if (a == "--bench-dyntopo") {
        cl.options.dyntopo = true;
        cl.options.projectRoundTrip = false;
    } else if (a == "--bench-micro") {
        cl.run = true;
        cl.options.micro = true;
    } else if (a == "--bench-micro-level") {
        cl.options.micro = true;
        cl.options.microLevel = std::clamp(std::atoi(next().c_str()), 1, 7);
    } else if (a == "--bench-focus") {
        cl.run = true;
        cl.options.focus = true;
    } else if (a == "--bench-focus-base") {
        cl.options.focus = true;
        cl.options.focusBase = size_t(std::max(100, std::atoi(next().c_str())));
    } else if (a == "--bench-out") {
        cl.outPath = next();
    } else {
        return false;
    }
    return true;
}

int runBenchCommandLine(BenchCommandLine cl, const std::string& heading, const BenchGpu* gpu) {
    bool writeFailed = false;
    auto write = [&](const std::string& report) {
        FILE* f = std::fopen(cl.outPath.c_str(), "wb");
        if (!f) {
            writeFailed = true;
            return;
        }
        std::fputs((heading + report).c_str(), f);
        std::fclose(f);
    };
    cl.options.log = [](const std::string& line) { std::fprintf(stderr, "%s\n", line.c_str()); };
    if (!cl.outPath.empty()) cl.options.partial = write;
    const std::string report = runBenchmark(cl.options, gpu);
    if (cl.outPath.empty()) {
        std::fputs((heading + report).c_str(), stdout);
        return 0;
    }
    write(report);
    if (writeFailed) {
        std::fprintf(stderr, "error: cannot write %s\n", cl.outPath.c_str());
        return 1;
    }
    std::printf("Benchmark written to %s\n", cl.outPath.c_str());
    return 0;
}

namespace {

// Dynamic topology (core/Dyntopo.h): strokes that remesh as they go, on spheres without subdivision levels
// (Sculptris Pro works on such meshes only). Each row starts from the same sphere.
std::string runDyntopoBenchmark(const BenchOptions& o, const BenchGpu* gpu) {
    auto log = [&](const std::string& s) {
        if (o.log) o.log(s);
    };
    std::ostringstream md;
    md << "Threads: " << ThreadPool::shared().threads();
    if (gpu && !gpu->description.empty()) md << ", GPU: " << gpu->description;
    md << ". Dynamic topology on (relative detail), X symmetry (two dabs per step), " << o.dabsPerStroke
       << " dabs per stroke on a unit sphere without subdivision levels. Times in milliseconds.\n\n";
    struct Row {
        BenchBrush brush;
        float detail;
    };
    const std::vector<Row> rows = {
        {{"Standard, Detail 50%", Kind::Brush, BrushType::Standard}, 0.5f},
        {{"Standard, Detail 100%", Kind::Brush, BrushType::Standard}, 1.0f},
        {{"Tendril, Detail 50%", Kind::Brush, BrushType::SnakeHook}, 0.5f},
        {{"Clay Block, Detail 75%", Kind::Brush, BrushType::ClayBuildup}, 0.75f},
        {{"Smooth (simplifies), Detail 50%", Kind::Brush, BrushType::Smooth}, 0.5f},
        {{"Standard (large, r 0.4), Detail 50%", Kind::Brush, BrushType::Standard, 0.4f}, 0.5f},
    };
    for (double sizeM : o.sizesMillions) {
        const size_t target = size_t(sizeM * 1e6);
        log("Building a " + millions(double(target)) + " triangle sphere...");
        Document doc;
        doc.mesh = makeBenchSphere(target);
        doc.mesh.multires.reset(); // (as after Delete Lower)
        doc.mesh.rebuildTopology();
        doc.bvh.build(doc.mesh);
        Mesh& m = doc.mesh;
        const double nt = double(m.triangleCount()), nv = double(m.vertexCount());
        if (gpu && gpu->upload) gpu->upload(m);
        UploadOrder order;
        order.build(m);
        md << "### " << millions(nt) << " triangles (" << millions(nv) << " vertices, no subdivision levels)\n\n";
        md << "| Stroke | remesh | kernel | normals | BVH | " << (gpu ? "GPU upload | " : "")
           << "**dab total** | triangles after | stroke end | undo | redo | undo step |\n";
        md << "|---|---:|---:|---:|---:|" << (gpu ? "---:|" : "") << "---:|---:|---:|---:|---:|---:|\n";
        for (const Row& row : rows) {
            if (!o.brushFilter.empty() && row.brush.name.find(o.brushFilter) == std::string::npos) continue;
            log("  " + row.brush.name);
            doc.sculptor.dyntopo.enabled = true;
            doc.sculptor.dyntopo.detail = row.detail;
            const StrokeStats s = runStroke(doc, row.brush, o.dabsPerStroke, gpu, order);
            doc.sculptor.dyntopo.enabled = false;
            const double d = std::max(s.dabs, 1);
            md << "| " << row.brush.name << " | " << fmt("%.2f", s.remesh / d) << " | " << fmt("%.2f", s.kernel / d) << " | "
               << fmt("%.2f", s.normals / d) << " | " << fmt("%.2f", s.bvh / d) << " | ";
            if (gpu) md << fmt("%.2f", s.upload / d) << " | ";
            md << "**" << fmt("%.2f", s.total / d) << "** | " << millions(double(s.triangles)) << " | " << fmt("%.1f", s.endStroke)
               << " | " << fmt("%.1f", s.undo) << " | " << fmt("%.1f", s.redo) << " | " << fmt("%.2f MB", double(s.undoBytes) / 1e6)
               << " |\n";
            doc.undo.undo(m, doc.bvh); // back to the sphere for the next row
            if (gpu && gpu->upload) gpu->upload(m);
            order.build(m);
        }
        md << "\n";
        if (o.partial) o.partial(md.str());
    }
    return md.str();
}

// Micro-displacement levels (core/Micro.h): a base sphere with subdivision levels, divided to micro level
// o.microLevel, so the micro triangle count is each size. Strokes as in the mesh benchmark (X symmetry, dabs
// along a line over the front); "shading" is what the renderer recomputes for a dab (normals, curvature) on the
// CPU before its upload.
std::string runMicroBenchmark(const BenchOptions& o, const BenchGpu* gpu) {
    auto log = [&](const std::string& s) {
        if (o.log) o.log(s);
    };
    std::ostringstream md;
    md << "Threads: " << ThreadPool::shared().threads();
    if (gpu && !gpu->description.empty()) md << ", GPU: " << gpu->description;
    md << ". Micro level " << o.microLevel << " (" << (1 << (2 * o.microLevel))
       << " micro triangles per base triangle). Brushes use X symmetry (two dabs per step), radius 0.12 on a unit sphere "
          "unless noted, "
       << o.dabsPerStroke << " dabs per stroke. Times in milliseconds.\n\n";
    const std::vector<BenchBrush> brushes = {
        {"Standard", Kind::Brush, BrushType::Standard},
        {"Standard (large, r 0.4)", Kind::Brush, BrushType::Standard, 0.4f},
        {"Standard (300 px, r 0.56)", Kind::Brush, BrushType::Standard, 0.56f},
        {"Clay", Kind::Brush, BrushType::Clay},
        {"Clay Block", Kind::Brush, BrushType::ClayBuildup},
        {"Smooth", Kind::Brush, BrushType::Smooth},
        {"Smooth (large, r 0.4)", Kind::Brush, BrushType::Smooth, 0.4f},
        {"Inflate", Kind::Brush, BrushType::Inflate},
        {"Flatten", Kind::Brush, BrushType::Flatten},
        {"Pinch", Kind::Brush, BrushType::Pinch},
        {"Crease", Kind::Brush, BrushType::Crease},
        {"Layer", Kind::Brush, BrushType::Layer},
        {"Move", Kind::Move, BrushType::Move},
        {"Move (300 px, r 0.56)", Kind::Move, BrushType::Move, 0.56f},
        {"Mask", Kind::Mask},
    };
    for (double sizeM : o.sizesMillions) {
        const size_t target = size_t(sizeM * 1e6);
        const size_t baseTarget = std::max<size_t>(target >> (2 * o.microLevel), 1000);
        log("Building a " + millions(double(target)) + " micro triangle sphere (" + millions(double(baseTarget)) + " base)...");
        Document doc;
        const size_t rss0 = resetPeakResidentBytes();
        auto t = Clock::now();
        doc.mesh = makeBenchSphere(baseTarget);
        const double baseMs = msSince(t);
        t = Clock::now();
        doc.bvh.build(doc.mesh);
        const double bvhMs = msSince(t);
        Mesh& m = doc.mesh;
        const size_t rssBase = residentBytes();
        std::string err;
        t = Clock::now();
        if (!microDivide(m, o.microLevel, err)) {
            md << "### " << millions(double(target)) << ": " << err << "\n\n";
            continue;
        }
        const double divideMs = msSince(t);
        const MicroMesh& mm = *m.micro;
        const double nt = double(mm.microTriangles()), nv = double(mm.microVertices());
        // The renderer's first full shading pass (into a scratch tile per thread: no GPU here).
        MicroShader shader;
        std::vector<uint32_t> written;
        auto scratch = [&](uint32_t) -> uint32_t* {
            thread_local std::vector<uint32_t> tile;
            tile.resize(mm.samples);
            return tile.data();
        };
        t = Clock::now();
        shader.shade(m, {}, true, written, scratch);
        const double shadeAllMs = msSince(t);
        if (!gpu) m.micro->clearDirty();
        double uploadAll = 0;
        if (gpu && gpu->upload) uploadAll = gpu->upload(m);
        const size_t rss = residentBytes(), peak = peakResidentBytes();
        md << "### " << millions(nt) << " micro triangles (" << millions(nv) << " micro vertices; base "
           << millions(double(m.triangleCount())) << " triangles, " << subdivLevelCount(m) << " subdivision levels)\n\n";
        md << "| Setup | |\n|---|---|\n";
        md << "| Base sphere (Divide) | " << fmt("%.0f", baseMs) << " |\n";
        md << "| BVH build (base) | " << fmt("%.0f", bvhMs) << " |\n";
        md << "| Divide to micro level | " << fmt("%.0f", divideMs) << " |\n";
        md << "| Shading, whole surface (CPU) | " << fmt("%.0f", shadeAllMs) << " |\n";
        if (gpu) md << "| First GPU upload | " << fmt("%.0f", uploadAll) << " |\n";
        if (gpu && gpu->gpuBytes) {
            const double b = double(gpu->gpuBytes());
            md << "| GPU memory (buffers) | " << fmt("%.0f MB", b / 1e6) << " = " << fmt("%.2f", b / nt) << " B/micro triangle |\n";
        }
        md << "| Micro level (displacements, ranges, topology) | " << fmt("%.0f MB", double(mm.bytes()) / 1e6) << " = "
           << fmt("%.2f", double(mm.bytes()) / nt) << " B/micro triangle, " << fmt("%.2f", double(mm.bytes()) / nv)
           << " B/micro vertex |\n";
        md << "| Base mesh + BVH + levels | "
           << fmt("%.0f MB", double(m.memoryBytes() + doc.bvh.memoryBytes() + subdivBytes(m)) / 1e6) << " |\n";
        if (rss)
            md << "| Process memory (RSS) | " << fmt("%.0f MB", double(rss) / 1e6) << " (micro level and shading: +"
               << fmt("%.0f MB", double(rss - std::min(rss, rssBase)) / 1e6) << "; everything: +"
               << fmt("%.0f MB", double(rss - std::min(rss, rss0)) / 1e6) << " = "
               << fmt("%.2f", double(rss - std::min(rss, rss0)) / nt) << " B/micro triangle) |\n";
        if (peak > rss0) md << "| Peak (RSS) | " << fmt("%.0f MB", double(peak) / 1e6) << " |\n";
        md << "\n";

        md << "| Brush | stride | points/dab | pick | find + undo tiles | kernel | stitch | shading | " << (gpu ? "GPU upload | " : "")
           << "**dab total** | stroke end | undo | redo | undo step |\n";
        md << "|---|---:|---:|---:|---:|---:|---:|---:|" << (gpu ? "---:|" : "") << "---:|---:|---:|---:|---:|\n";
        for (const BenchBrush& b : brushes) {
            if (!o.brushFilter.empty() && b.name.find(o.brushFilter) == std::string::npos) continue;
            log("  " + b.name);
            Sculptor& sc = doc.sculptor;
            sc.beginStroke(m, b.name);
            Symmetry sym;
            MicroStroke& ms = sc.microStroke();
            const double f0 = ms.findMs, k0 = ms.kernelMs, s0 = ms.stitchMs;
            const uint64_t p0 = ms.points;
            double pick = 0, shade = 0, upload = 0;
            int dabs = 0;
            const float step = 0.15f * b.radius;
            const auto stroke0 = Clock::now();
            std::vector<uint32_t> dirty;
            for (int i = 0; i < o.dabsPerStroke; ++i) {
                const float x = 0.15f + std::fmod(float(i) * step, 0.6f), y = 0.2f + 0.1f * std::sin(float(i) * 0.3f);
                auto tp = Clock::now();
                RayHit hit;
                const bool ok = surfaceRaycast(m, doc.bvh, glm::vec3(x, y, 5.0f), glm::vec3(0, 0, -1), hit);
                pick += msSince(tp);
                if (!ok) continue;
                DabParams p;
                p.center = hit.position;
                p.viewDir = glm::vec3(0, 0, -1);
                p.radius = b.radius;
                p.strength = 0.5f;
                switch (b.kind) {
                case Kind::Move:
                    if (i == 0) sc.beginGrab(m, doc.bvh, p, sym);
                    else sc.grab(m, doc.bvh, glm::vec3(0.0f, 0.0f, 0.002f));
                    break;
                case Kind::Mask: sc.dabMask(m, doc.bvh, p, sym); break;
                default: sc.dab(m, doc.bvh, b.type, p, sym); break;
                }
                // What the renderer does next: the shading of what changed (and its neighbours), then the upload.
                tp = Clock::now();
                if (gpu && gpu->upload) {
                    upload += gpu->upload(m); // (the renderer shades inside its upload)
                } else {
                    MicroMesh& live = *m.micro;
                    dirty.clear();
                    for (size_t w = 0; w < live.dirtyBits.size(); ++w)
                        for (uint64_t bits = live.dirtyBits[w]; bits; bits &= bits - 1)
                            dirty.push_back(uint32_t(w * 64 + size_t(std::countr_zero(bits))));
                    shader.shade(m, dirty, live.allDirty, written, scratch);
                    live.clearDirty();
                    shade += msSince(tp);
                }
                ++dabs;
            }
            const double total = msSince(stroke0);
            const double d = std::max(dabs, 1);
            auto te = Clock::now();
            UndoRecord r = sc.endStroke();
            const double endMs = msSince(te);
            const size_t undoBytes = r.approxBytes();
            doc.undo.push(std::move(r));
            te = Clock::now();
            doc.undo.undo(m, doc.bvh);
            const double undoMs = msSince(te);
            if (gpu && gpu->upload) gpu->upload(m);
            te = Clock::now();
            doc.undo.redo(m, doc.bvh);
            const double redoMs = msSince(te);
            if (gpu && gpu->upload) gpu->upload(m);
            else m.micro->clearDirty();
            md << "| " << b.name << " | " << ms.lastStride << " | " << millions(double(ms.points - p0) / d) << " | "
               << fmt("%.2f", pick / d) << " | "
               << fmt("%.2f", (ms.findMs - f0) / d) << " | " << fmt("%.2f", (ms.kernelMs - k0) / d) << " | "
               << fmt("%.2f", (ms.stitchMs - s0) / d) << " | " << (gpu ? std::string("(in upload)") : fmt("%.2f", shade / d)) << " | ";
            if (gpu) md << fmt("%.2f", upload / d) << " | ";
            md << "**" << fmt("%.2f", total / d) << "** | " << fmt("%.1f", endMs) << " | " << fmt("%.1f", undoMs) << " | "
               << fmt("%.1f", redoMs) << " | " << fmt("%.1f MB", double(undoBytes) / 1e6) << " |\n";
        }
        md << "\n";
        md << "| Operation | ms |\n|---|---:|\n";
        {
            log("  surface noise (whole surface)");
            t = Clock::now();
            microNoise(m, 0.002f, 60.0f);
            md << "| Surface noise, every point | " << fmt("%.0f", msSince(t)) << " |\n";
            if (gpu && gpu->upload) {
                t = Clock::now();
                gpu->upload(m);
                md << "| GPU upload after it (shading + displacements) | " << fmt("%.0f", msSince(t)) << " |\n";
            } else {
                m.micro->clearDirty();
            }
        }
        if (o.projectRoundTrip) {
            namespace fs = std::filesystem;
            std::error_code ec;
            const fs::path dir = o.projectDir.empty() ? fs::temp_directory_path(ec) : fs::path(o.projectDir);
            const std::string path = (dir / "sculptforge-bench-micro.sfproj").string();
            log("  project save / open");
            t = Clock::now();
            const bool saved = saveProject(path, doc, err);
            const double saveMs = msSince(t);
            const double fileMb = saved ? double(fs::file_size(path, ec)) / 1e6 : 0.0;
            Document loaded;
            t = Clock::now();
            const bool opened = saved && loadProject(path, loaded, err);
            const double openMs = msSince(t);
            fs::remove(path, ec);
            md << "| Save project (" << fmt("%.0f MB", fileMb) << ") | " << (saved ? fmt("%.0f", saveMs) : "failed: " + err) << " |\n";
            md << "| Open project | " << (opened ? fmt("%.0f", openMs) : "failed: " + err) << " |\n";
        }
        if (gpu && gpu->orbitFrameMs) {
            log("  orbit");
            struct Orbit {
                int samples;
                bool moving;
                const char* name;
            };
            for (const Orbit& ob : {Orbit{4, false, "4x antialiasing"}, Orbit{1, false, "no antialiasing"},
                                    Orbit{1, true, "Fast Navigation (no antialiasing, coarser)"}}) {
                const double f = gpu->orbitFrameMs(m, ob.samples, ob.moving);
                md << "| Orbit: frame time, " << ob.name << " | " << fmt("%.1f", f) << " (" << fmt("%.0f", 1000.0 / std::max(f, 1e-3))
                   << " fps) |\n";
                if (gpu->drawnTriangles && ob.samples == 1)
                    md << "| Orbit: triangles drawn" << (ob.moving ? " (moving)" : "") << " | " << millions(double(gpu->drawnTriangles()))
                       << " |\n";
            }
        }
        md << "\n";
        if (o.partial) o.partial(md.str());
    }
    return md.str();
}

} // namespace

std::string runBenchmark(const BenchOptions& o, const BenchGpu* gpu) {
    if (o.dyntopo) return runDyntopoBenchmark(o, gpu);
    if (o.micro && !o.focus) return runMicroBenchmark(o, gpu);
    if (o.focus) return runFocusBenchmark(o);
    auto log = [&](const std::string& s) {
        if (o.log) o.log(s);
    };
    std::ostringstream md;
    md << "Threads: " << ThreadPool::shared().threads();
    if (gpu && !gpu->description.empty()) md << ", GPU: " << gpu->description;
    md << ". Brushes use X symmetry (two dabs per step), radius " << 0.12 << " on a unit sphere unless noted, "
       << o.dabsPerStroke << " dabs per stroke. Times in milliseconds.\n\n";

    const std::vector<BenchBrush> brushes = {
        {"Standard", Kind::Brush, BrushType::Standard},
        {"Standard (large, r 0.4)", Kind::Brush, BrushType::Standard, 0.4f},
        {"Clay", Kind::Brush, BrushType::Clay},
        {"Clay Block", Kind::Brush, BrushType::ClayBuildup},
        {"Smooth", Kind::Brush, BrushType::Smooth},
        {"Smooth (large, r 0.4)", Kind::Brush, BrushType::Smooth, 0.4f},
        {"Inflate", Kind::Brush, BrushType::Inflate},
        {"Sharp Crease", Kind::Brush, BrushType::DamStandard},
        {"Layer", Kind::Brush, BrushType::Layer},
        {"Move", Kind::Move, BrushType::Move},
        {"Move (large, r 0.4)", Kind::Move, BrushType::Move, 0.4f},
        {"Recipe (pattern)", Kind::Recipe},
        {"Mask", Kind::Mask},
        {"Paint", Kind::Paint},
    };

    for (double sizeM : o.sizesMillions) {
        const size_t target = size_t(sizeM * 1e6);
        log("Building a " + millions(double(target)) + " triangle sphere...");
        Document doc;
        const size_t rss0 = resetPeakResidentBytes();
        auto t = Clock::now();
        doc.mesh = makeBenchSphere(target);
        const double divideMs = msSince(t);
        t = Clock::now();
        doc.bvh.build(doc.mesh);
        const double bvhMs = msSince(t);
        Mesh& m = doc.mesh;
        const double nt = double(m.triangleCount()), nv = double(m.vertexCount());
        double uploadAll = 0;
        if (gpu && gpu->upload) uploadAll = gpu->upload(m);
        const size_t meshBytes = m.memoryBytes(), bvhBytes = doc.bvh.memoryBytes(), stackBytes = subdivBytes(m);
        const size_t rss = residentBytes(), setupPeak = peakResidentBytes();
        UploadOrder order;
        order.build(m);

        md << "### " << millions(nt) << " triangles (" << millions(nv) << " vertices, "
           << subdivLevelCount(m) << " subdivision levels)\n\n";
        md << "| Setup | |\n|---|---|\n";
        md << "| Divide up to this size | " << fmt("%.0f", divideMs) << " |\n";
        md << "| BVH build | " << fmt("%.0f", bvhMs) << " |\n";
        if (gpu) md << "| First GPU upload | " << fmt("%.0f", uploadAll) << " |\n";
        if (gpu && gpu->gpuBytes) {
            const double b = double(gpu->gpuBytes());
            md << "| GPU memory (buffers) | " << fmt("%.0f MB", b / 1e6) << " = " << fmt("%.1f", b / nt) << " B/triangle |\n";
        }
        md << "| Mesh (incl. topology) | " << fmt("%.0f MB", double(meshBytes) / 1e6) << " = "
           << fmt("%.0f", double(meshBytes) / nt) << " B/triangle |\n";
        md << "| BVH | " << fmt("%.0f MB", double(bvhBytes) / 1e6) << " = " << fmt("%.1f", double(bvhBytes) / nt)
           << " B/triangle |\n";
        md << "| Subdivision levels (stack) | " << fmt("%.0f MB", double(stackBytes) / 1e6) << " = "
           << fmt("%.0f", double(stackBytes) / nt) << " B/triangle |\n";
        if (rss) md << "| Process memory (RSS) | " << fmt("%.0f MB", double(rss) / 1e6) << " (+" << fmt("%.0f", double(rss - std::min(rss, rss0)) / 1e6) << " MB = "
                    << fmt("%.0f", double(rss - std::min(rss, rss0)) / nt) << " B/triangle) |\n";
        if (setupPeak > rss0) md << "| Peak while dividing (RSS) | " << fmt("%.0f MB", double(setupPeak - rss0) / 1e6) << " above the start |\n";
        md << "\n";

        md << "| Brush | verts/dab | uploaded verts (ranges) | kernel | normals | BVH refit | pick | " << (gpu ? "GPU upload | " : "")
           << "**dab total** | stroke end (undo record) | undo | redo | undo step |\n";
        md << "|---|---:|---:|---:|---:|---:|---:|" << (gpu ? "---:|" : "") << "---:|---:|---:|---:|---:|\n";
        for (const BenchBrush& b : brushes) {
            if (!o.brushFilter.empty() && b.name.find(o.brushFilter) == std::string::npos) continue;
            if (o.viewOnly && b.name != "Standard" && b.kind != Kind::Mask && b.kind != Kind::Paint) continue;
            log("  " + b.name);
            const StrokeStats s = runStroke(doc, b, o.dabsPerStroke, gpu, order);
            const double d = std::max(s.dabs, 1);
            md << "| " << b.name << " | " << millions(s.verts) << " | " << millions(s.uploaded) << " (" << fmt("%.0f", s.runs)
               << ") | " << fmt("%.2f", s.kernel / d) << " | "
               << fmt("%.2f", s.normals / d) << " | " << fmt("%.2f", s.bvh / d) << " | " << fmt("%.2f", s.pick / d) << " | ";
            if (gpu) md << fmt("%.2f", s.upload / d) << " | ";
            md << "**" << fmt("%.2f", s.total / d) << "** | " << fmt("%.2f", s.endStroke) << " | " << fmt("%.1f", s.undo)
               << " | " << fmt("%.1f", s.redo) << " | " << fmt("%.1f MB", double(s.undoBytes) / 1e6) << " |\n";
        }
        md << "\n";

        md << "| Operation | ms |\n|---|---:|\n";
        if (subdivLevelCount(m) > 1 && !o.viewOnly) {
            std::string err;
            const int top = subdivCurrentLevel(m);
            log("  subdivision levels");
            t = Clock::now();
            subdivSetLevel(m, top - 1, err, &doc.bvh);
            md << "| Lower Res (to " << millions(double(m.triangleCount())) << ", after edits) | " << fmt("%.0f", msSince(t)) << " |\n";
            if (gpu && gpu->upload) gpu->upload(m);
            t = Clock::now();
            subdivSetLevel(m, top, err, &doc.bvh);
            md << "| Higher Res (back up) | " << fmt("%.0f", msSince(t)) << " |\n";
            if (gpu && gpu->upload) gpu->upload(m);
            t = Clock::now();
            subdivSetLevel(m, top - 1, err, &doc.bvh);
            md << "| Lower Res again (no edits) | " << fmt("%.0f", msSince(t)) << " |\n";
            t = Clock::now();
            subdivSetLevel(m, top, err, &doc.bvh);
            md << "| Higher Res again (no edits) | " << fmt("%.0f", msSince(t)) << " |\n";
            if (gpu && gpu->upload) gpu->upload(m);
        }
        if (o.projectRoundTrip) {
            namespace fs = std::filesystem;
            std::error_code ec;
            const fs::path dir = o.projectDir.empty() ? fs::temp_directory_path(ec) : fs::path(o.projectDir);
            const std::string path = (dir / "sculptforge-bench.sfproj").string();
            std::string err;
            log("  project save / open");
            t = Clock::now();
            const bool saved = saveProject(path, doc, err);
            const double saveMs = msSince(t);
            const double fileMb = saved ? double(fs::file_size(path, ec)) / 1e6 : 0.0;
            Document loaded;
            const size_t beforeOpen = resetPeakResidentBytes();
            t = Clock::now();
            const bool opened = saved && loadProject(path, loaded, err);
            const double openMs = msSince(t);
            const size_t openPeak = peakResidentBytes(), afterOpen = residentBytes();
            fs::remove(path, ec);
            md << "| Save project (" << fmt("%.0f MB", fileMb) << ") | " << (saved ? fmt("%.0f", saveMs) : "failed: " + err) << " |\n";
            md << "| Open project | " << (opened ? fmt("%.0f", openMs) : "failed: " + err) << " |\n";
            if (opened && openPeak > beforeOpen)
                md << "| Open project: memory (RSS) | +" << fmt("%.0f MB", double(afterOpen - std::min(afterOpen, beforeOpen)) / 1e6) << ", peak +"
                   << fmt("%.0f MB", double(openPeak - beforeOpen) / 1e6) << " |\n";
        }
        if (gpu && gpu->orbitFrameMs) {
            log("  orbit");
            if (gpu->motionSetupMs)
                md << "| Motion proxy build (first move after an upload) | " << fmt("%.0f", gpu->motionSetupMs(m)) << " |\n";
            struct Orbit {
                int samples;
                bool moving;
                const char* name;
            };
            for (const Orbit& o : {Orbit{4, false, "4x antialiasing"}, Orbit{1, false, "no antialiasing"},
                                   Orbit{1, true, "Fast Navigation (no antialiasing, motion proxy)"}}) {
                const double f = gpu->orbitFrameMs(m, o.samples, o.moving);
                md << "| Orbit: frame time, " << o.name << " | " << fmt("%.1f", f) << " (" << fmt("%.0f", 1000.0 / std::max(f, 1e-3))
                   << " fps) |\n";
                if (gpu->drawnTriangles && o.samples == 1) {
                    const double d = double(gpu->drawnTriangles());
                    md << "| Orbit: triangles drawn" << (o.moving ? " (motion proxy)" : "") << " | " << millions(d) << " ("
                       << fmt("%.0f", 100.0 * d / nt) << "%) |\n";
                }
            }
            if (gpu->gpuBytes)
                md << "| GPU memory with the motion proxy | " << fmt("%.0f MB", double(gpu->gpuBytes()) / 1e6) << " = "
                   << fmt("%.1f", double(gpu->gpuBytes()) / nt) << " B/triangle |\n";
        }
        md << "\n";
        if (o.partial) o.partial(md.str());
    }
    return md.str();
}

} // namespace sf
