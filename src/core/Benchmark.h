// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Performance benchmark (sculptforge-cli --bench, sculptforge --bench; numbers in docs/performance.md).
//
// For each size it builds a sphere the way a sculptor gets one (a quad cube projected onto a sphere, then
// Divide up to the size, so the mesh has subdivision levels) and measures, per dab and per stroke: the brush
// kernel, normal update, BVH refit and undo record for every kind of brush (with X symmetry, as in the app),
// plus opening / saving the project and stepping subdivision levels. The app adds what needs a GPU through
// BenchGpu: uploading what a dab changed and the frame time while orbiting.
#pragma once

#include "core/Mesh.h"

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace sf {

struct Document;

struct BenchGpu {
    // Uploads what the mesh marked dirty and waits for the GPU; returns milliseconds. First call: everything.
    std::function<double(Mesh&)> upload;
    // Average milliseconds per frame over a turn around the mesh (already uploaded), drawn with `samples`
    // antialiasing samples as the app's view draws; `moving`: as Fast Navigation draws while the view moves
    // (the renderer's motion proxy, if it has one; the app also drops to 1 sample).
    std::function<double(Mesh&, int samples, bool moving)> orbitFrameMs;
    // Milliseconds to build what moving the view needs first (the motion proxy) for the uploaded mesh (optional).
    std::function<double(Mesh&)> motionSetupMs;
    std::function<size_t()> gpuBytes;          // GPU memory the renderer holds for the mesh (optional)
    std::function<size_t()> drawnTriangles;    // triangles the last orbit frame drew, after culling (optional)
    std::string description; // GPU / driver, for the report
};

struct BenchOptions {
    std::vector<double> sizesMillions{4, 16, 32}; // triangle counts, in millions
    int dabsPerStroke = 40;
    std::string projectDir; // where the project round trip writes (default: the system temp folder)
    bool projectRoundTrip = true;
    std::string brushFilter; // only the brushes whose row name contains this (empty = all)
    // Only what the viewport costs (--bench-view): setup, the first upload, the strokes whose uploads differ
    // (Standard, Mask, Paint), orbiting; no other brushes, subdivision levels or project round trip.
    bool viewOnly = false;
    // Dynamic topology instead (--bench-dyntopo): spheres without subdivision levels, strokes that remesh.
    bool dyntopo = false;
    // Micro-displacement levels instead (--bench-micro, core/Micro.h): sizes are micro triangles (millions), on
    // a base sphere at micro level `microLevel` (4: 256 micro triangles per base triangle).
    bool micro = false;
    int microLevel = 4;
    // Focus Detail instead (--bench-focus, core/FocusDetail.h): the standard-1b asset (a billion micro triangles
    // streamed from a detail file), or a head of `focusBase` base triangles at micro level `microLevel`.
    bool focus = false;
    size_t focusBase = 0;
    std::function<void(const std::string&)> log; // progress lines (optional)
    std::function<void(const std::string&)> partial; // the report so far, after each size (optional)
};

// Command-line options shared by sculptforge-cli and the app: --bench, --bench-sizes 4,16,32 (millions of
// triangles), --bench-dabs N, --bench-no-project (skip saving / opening, which needs twice the memory),
// --bench-brushes TEXT (only the rows whose name contains TEXT), --bench-view (only the viewport's costs), --bench-dyntopo (dynamic topology strokes), --bench-micro (micro-displacement levels; --bench-micro-level N), --bench-out FILE. parseBenchArg returns true when argv[i] was one of them
// (advancing i past its value).
struct BenchCommandLine {
    bool run = false;
    BenchOptions options;
    std::string outPath;
};
bool parseBenchArg(int argc, char** argv, int& i, BenchCommandLine& cl);
// Runs the benchmark for the command line: progress on stderr, the report (under `heading`) to cl.outPath
// (rewritten after each size, so a long run keeps what it measured) or stdout. Returns the exit code.
int runBenchCommandLine(BenchCommandLine cl, const std::string& heading, const BenchGpu* gpu = nullptr);

// Runs the benchmark and returns the report as Markdown tables.
std::string runBenchmark(const BenchOptions& options, const BenchGpu* gpu = nullptr);

// The benchmark mesh: about `triangles` triangles, with subdivision levels.
Mesh makeBenchSphere(size_t triangles);
// Proof mode's standard scenes (standard-15m, standard-33m; docs/benchmark.md): a procedural head (skull, brow,
// eye sockets, nose, cheekbones, jaw, ears, neck) of about `triangles` triangles, with subdivision levels. Made
// from a formula, not a file, so every build and platform sculpts the same surface.
Mesh makeBenchHead(size_t triangles);

// Resident memory of this process in bytes (0 where unknown).
size_t residentBytes();
// The most resident memory since the last reset (Linux: VmHWM; 0 where unknown); the reset returns residentBytes().
size_t peakResidentBytes();
size_t resetPeakResidentBytes();

// What the scene holds in memory, summed over SubTools (Preferences > Performance, perf_info).
struct MemoryUse {
    size_t mesh = 0;   // meshes with topology (Mesh::memoryBytes)
    size_t bvh = 0;    // acceleration structures
    size_t levels = 0; // subdivision level stacks (subdivBytes)
    size_t undo = 0;   // undo history (UndoStack::undoBytes)
    size_t gpu = 0;    // vertex and index buffers the renderer keeps for the visible SubTools (an estimate)
    size_t textures = 0; // painted textures (core/Texture.h): colour, layers, material channels (each texture once)
    size_t channels = 0; // of `textures`, every material channel tile alive (copies and undo steps too; TileImage::liveBytes)
    size_t paintCaches = 0; // texture painting caches of dense meshes (UV tile bins, texel maps), shared per UV layout
    size_t triangles = 0, vertices = 0;
    size_t total() const { return mesh + bvh + levels + undo + textures + paintCaches; }
};
MemoryUse documentMemory(const Document& doc);

class CommandRegistry;
// perf_info (threads, memory) and set_multithreading.
void registerPerformanceCommands(CommandRegistry& registry);

} // namespace sf
