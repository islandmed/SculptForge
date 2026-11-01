// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// GPU brushes (render/GpuSculpt.h) against the CPU brushes they stand in for: the same strokes on two copies of a
// mesh, one with every dab on the CPU and one with every dab on the GPU, must agree within rounding; the GPU
// strokes' undo and redo must be bit-exact, and the same GPU stroke twice must give the same bits.
//
// Needs a window and an OpenGL 4.3 context. Without a display, or where the driver has no compute shaders (macOS,
// old software renderers), it prints why and exits with 77, which CTest reports as skipped.
#include "TestHarness.h"

#include "core/Benchmark.h"
#include "core/BrushCompute.h"
#include "core/Micro.h"
#include "core/Sculpt.h"
#include "render/GL.h"
#include "render/GpuSculpt.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>

using namespace sf;

namespace {

GpuSculpt* g_gpu = nullptr;

struct StrokeSpec {
    BrushType type;
    bool invert = false;
    float radius = 0.25f, strength = 0.5f;
};

// Six dabs along an arc over the sphere's front, X symmetry on.
void stroke(Document& doc, const StrokeSpec& s, bool gpu) {
    setGpuBrushesEnabled(gpu);
    Symmetry sym;
    CHECK(doc.sculptor.beginStroke(doc.mesh, brushName(s.type)));
    for (int i = 0; i < 6; ++i) {
        const float a = -0.3f + 0.12f * float(i);
        DabParams p;
        p.center = glm::normalize(glm::vec3(0.25f + std::sin(a) * 0.3f, std::cos(a) * 0.3f, 1.0f));
        p.radius = s.radius;
        p.strength = s.strength;
        p.invert = s.invert;
        p.viewDir = glm::vec3(0, 0, -1);
        doc.sculptor.dab(doc.mesh, doc.bvh, s.type, p, sym);
    }
    doc.undo.push(doc.sculptor.endStroke());
    setGpuBrushesEnabled(true);
}

const Document& meshBase() {
    static const Document base = makeDoc(makeBenchSphere(80000));
    return base;
}

Document copyOf(const Document& d) {
    Document c = makeDoc(d.mesh);
    return c;
}

const Document& microBase() {
    static const Document base = [] {
        Document d = makeDoc(makeBenchSphere(8000));
        std::string err;
        CHECK(microDivide(d.mesh, 4, err));
        microNoise(d.mesh, 0.004f, 40.0f, 7);
        return d;
    }();
    return base;
}

} // namespace

TEST(gpu_mesh_brushes_match_the_cpu) {
    const StrokeSpec specs[] = {{BrushType::Standard}, {BrushType::Standard, true}, {BrushType::Clay},
                                {BrushType::Inflate},  {BrushType::Flatten},       {BrushType::Flatten, true},
                                {BrushType::Pinch},    {BrushType::Crease},        {BrushType::Crease, true}};
    for (const StrokeSpec& s : specs) {
        Document cpu = copyOf(meshBase()), gpu = copyOf(meshBase());
        const uint64_t dabs0 = g_gpu->stats.dabs;
        stroke(cpu, s, false);
        stroke(gpu, s, true);
        CHECK(g_gpu->stats.dabs >= dabs0 + 12); // every dab (both mirrors) ran on the GPU
        double worst = 0.0, moved = 0.0;
        for (uint32_t v = 0; v < cpu.mesh.vertexCount(); ++v) {
            worst = std::max(worst, double(glm::length(cpu.mesh.positions[v] - gpu.mesh.positions[v])));
            moved = std::max(moved, double(glm::length(cpu.mesh.positions[v] - meshBase().mesh.positions[v])));
        }
        std::printf("  %-9s%s moved %.5f, GPU - CPU at most %.2e (%.1e of the radius)\n", brushName(s.type), s.invert ? " (inv)" : "      ",
                    moved, worst, worst / s.radius);
        CHECK(moved > 1e-3);
        CHECK(worst <= 2e-5 * s.radius);
    }
}

TEST(gpu_mesh_strokes_undo_redo_and_repeat_bit_exactly) {
    Document a = copyOf(meshBase()), b = copyOf(meshBase());
    const std::vector<glm::vec3> before = a.mesh.positions;
    stroke(a, {BrushType::Clay}, true);
    stroke(b, {BrushType::Clay}, true);
    const std::vector<glm::vec3> after = a.mesh.positions;
    CHECK(after != before);
    // The same deltas from the same GPU: the same bits.
    CHECK(std::memcmp(a.mesh.positions.data(), b.mesh.positions.data(), after.size() * sizeof(glm::vec3)) == 0);
    CHECK(a.undo.undo(a.mesh, a.bvh));
    CHECK(std::memcmp(a.mesh.positions.data(), before.data(), before.size() * sizeof(glm::vec3)) == 0);
    CHECK(a.undo.redo(a.mesh, a.bvh));
    CHECK(std::memcmp(a.mesh.positions.data(), after.data(), after.size() * sizeof(glm::vec3)) == 0);
}

TEST(gpu_micro_brushes_match_the_cpu) {
    const StrokeSpec specs[] = {{BrushType::Standard}, {BrushType::Clay}, {BrushType::Inflate, true}, {BrushType::Smooth},
                                {BrushType::Flatten},  {BrushType::Pinch}, {BrushType::Crease},
                                {BrushType::Standard, false, 0.6f}}; // (a big brush: a coarser stride)
    for (const StrokeSpec& s : specs) {
        Document cpu = copyOf(microBase()), gpu = copyOf(microBase());
        const uint64_t dabs0 = g_gpu->stats.dabs;
        stroke(cpu, s, false);
        stroke(gpu, s, true);
        CHECK(g_gpu->stats.dabs >= dabs0 + 12);
        const MicroMesh &mc = *cpu.mesh.micro, &mg = *gpu.mesh.micro, &m0 = *microBase().mesh.micro;
        CHECK(mc.step == mg.step);
        int worst = 0;
        size_t changed = 0, off = 0;
        for (size_t i = 0; i < mc.disp.size(); ++i) {
            const int d = std::abs(int(mc.disp[i]) - int(mg.disp[i]));
            worst = std::max(worst, d);
            off += d != 0;
            changed += mc.disp[i] != m0.disp[i] || mc.step != m0.step;
        }
        std::printf("  %-9s r %.2f stride %u: %zu points changed, GPU - CPU at most %d units, %zu points differ (%.3f%%)\n",
                    brushName(s.type), s.radius, cpu.sculptor.microStroke().lastStride, changed, worst, off,
                    100.0 * double(off) / double(std::max<size_t>(changed, 1)));
        CHECK(changed > 1000);
        CHECK(worst <= 3);
        CHECK(double(off) <= 0.01 * double(changed));
    }
}

static int kGrabs = std::getenv("SF_GRABS") ? std::atoi(std::getenv("SF_GRABS")) : 5;
TEST(gpu_micro_move_matches_the_cpu) {
    Document cpu = copyOf(microBase()), gpu = copyOf(microBase());
    for (Document* d : {&cpu, &gpu}) {
        setGpuBrushesEnabled(d == &gpu);
        d->sculptor.microMoveAlongNormals = true;
        Symmetry sym;
        d->sculptor.beginStroke(d->mesh, "Move");
        DabParams p;
        p.center = glm::vec3(0, 0, 1);
        p.radius = 0.35f;
        p.strength = 1.0f;
        d->sculptor.beginGrab(d->mesh, d->bvh, p, sym);
        for (int i = 0; i < kGrabs; ++i) d->sculptor.grab(d->mesh, d->bvh, glm::vec3(0.003f, 0, 0.02f));
        d->undo.push(d->sculptor.endStroke());
    }
    setGpuBrushesEnabled(true);
    int worst = 0;
    size_t changed = 0;
    for (size_t i = 0; i < cpu.mesh.micro->disp.size(); ++i) {
        worst = std::max(worst, std::abs(int(cpu.mesh.micro->disp[i]) - int(gpu.mesh.micro->disp[i])));
        changed += cpu.mesh.micro->disp[i] != microBase().mesh.micro->disp[i];
    }
    std::printf("  Move (%d drags): %zu points changed, GPU - CPU at most %d units\n", kGrabs, changed, worst);
    CHECK(changed > 1000);
    // At most a unit per drag: the CPU rounds height + change in float, the GPU the change alone (a change whose
    // fraction lies within a float step of one half rounds differently, the same way every drag).
    CHECK(worst <= kGrabs);
}

TEST(gpu_micro_strokes_undo_redo_bit_exactly_and_respect_the_mask) {
    // A mask by position (the same on every copy of a shared point): the right of x = 0.1 fully masked.
    auto masked = [](Document& doc) {
        MicroMesh& mm = mutableMicro(doc.mesh);
        mm.mask.assign(mm.disp.size(), 0);
        const std::vector<uint32_t>& coords = microGridCoords(mm.n);
        for (uint32_t t = 0; t < mm.baseTriangles; ++t)
            for (uint32_t k = 0; k < mm.samples; ++k)
                if (microPoint(doc.mesh, t, coords[k] & 65535u, coords[k] >> 16).x > 0.1f) mm.mask[size_t(t) * mm.samples + k] = 255;
    };
    Document d = copyOf(microBase()), c = copyOf(microBase());
    masked(d);
    masked(c);
    const std::vector<int16_t> before = d.mesh.micro->disp;
    const std::vector<uint8_t> mask = d.mesh.micro->mask;
    stroke(d, {BrushType::Standard, false, 0.3f}, true);
    stroke(c, {BrushType::Standard, false, 0.3f}, false);
    const std::vector<int16_t> after = d.mesh.micro->disp;
    CHECK(after != before);
    int worst = 0;
    size_t gm = 0, cm = 0;
    for (size_t i = 0; i < after.size(); ++i) {
        worst = std::max(worst, std::abs(int(after[i]) - int(c.mesh.micro->disp[i])));
        if (mask[i] == 255) {
            gm += after[i] != before[i];
            cm += c.mesh.micro->disp[i] != before[i];
        }
    }
    std::printf("  masked Standard: GPU - CPU at most %d units; masked points moved: GPU %zu, CPU %zu\n", worst, gm, cm);
    CHECK(worst <= 3);
    CHECK(gm == 0 && cm == 0);
    CHECK(d.undo.undo(d.mesh, d.bvh));
    CHECK(d.mesh.micro->disp == before);
    CHECK(d.undo.redo(d.mesh, d.bvh));
    CHECK(d.mesh.micro->disp == after);
}

int main(int argc, char** argv) {
    using namespace sf::test;
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::printf("SKIP: no display (%s)\n", SDL_GetError());
        return 77;
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
    SDL_Window* window = SDL_CreateWindow("SculptForge GPU tests", 64, 64, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    SDL_GLContext context = window ? SDL_GL_CreateContext(window) : nullptr;
    if (!context) {
        std::printf("SKIP: no OpenGL context (%s)\n", SDL_GetError());
        if (window) SDL_DestroyWindow(window);
        SDL_Quit();
        return 77;
    }
    SDL_GL_MakeCurrent(window, context);
    auto loader = [](const char* n) { return reinterpret_cast<gl::GenericProc>(SDL_GL_GetProcAddress(n)); };
    if (const char* missing = gl::load(loader)) {
        std::printf("SKIP: OpenGL function %s missing\n", missing);
        return 77;
    }
    std::string reason;
    std::unique_ptr<GpuSculpt> gpu = GpuSculpt::create(loader, reason);
    std::printf("OpenGL %s on %s\n", reinterpret_cast<const char*>(gl::GetString(GL_VERSION)),
                reinterpret_cast<const char*>(gl::GetString(GL_RENDERER)));
    if (!gpu) {
        std::printf("SKIP: no GPU brushes here: %s\n", reason.c_str());
        SDL_GL_DestroyContext(context);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 77;
    }
    g_gpu = gpu.get();
    setBrushCompute(g_gpu);
    setGpuMinPoints(0); // every dab, however small, on the GPU
    for (const auto& t : registry()) {
        bool wanted = argc < 2;
        for (int i = 1; i < argc && !wanted; ++i) wanted = std::strstr(t.name, argv[i]) != nullptr;
        if (!wanted) continue;
        std::printf("[ RUN] %s\n", t.name);
        std::fflush(stdout);
        const int before = g_failures;
        t.fn();
        std::printf("%s %s\n", g_failures == before ? "[ OK ]" : "[FAIL]", t.name);
        std::fflush(stdout);
    }
    std::printf("\n%d checks, %d failures; GPU dabs %llu (upload %.0f ms, kernels %.0f ms, read back %.0f ms)\n", g_checks, g_failures,
                (unsigned long long)g_gpu->stats.dabs, g_gpu->stats.uploadMs, g_gpu->stats.kernelMs, g_gpu->stats.readbackMs);
    setBrushCompute(nullptr);
    gpu.reset();
    SDL_GL_DestroyContext(context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return g_failures == 0 ? 0 : 1;
}
