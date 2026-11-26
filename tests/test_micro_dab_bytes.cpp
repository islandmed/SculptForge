// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// A dab on a micro level writes the same bytes through the fast write loop (interpolateWrite) as through the plain
// callback loop it replaced (perf/micro-dab-1b: at a billion triangles that write is most of a dab). The same fixed
// strokes on a seeded Micro 6 sphere run both ways (a stride 1 brush, strides 2 and 4, with and without X symmetry;
// Clay, Standard, Smooth and Smooth Peaks), and every grid value and range is hashed after each stroke: the hashes
// must be equal. (Taken once against the kernel before the change on the dev machine too: same bytes.)
#include "TestHarness.h"

#include "core/Benchmark.h"
#include "core/Micro.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

using namespace sf;

namespace {

constexpr uint64_t kFnvPrime = 1099511628211ull;

struct Hash {
    uint64_t h = 1469598103934665603ull;
    void add(uint64_t v) { h = (h ^ v) * kFnvPrime; }
};

// Deterministic detail on the grid (a seeded integer hash: nothing to smooth on a flat grid).
void seedDetail(Document& doc) {
    MicroMesh& mm = *doc.mesh.micro;
    for (uint32_t t = 0; t < mm.baseTriangles; ++t) {
        int16_t* g = mm.disp.data() + size_t(t) * mm.samples;
        int top = 0;
        for (uint32_t k = 0; k < mm.samples; ++k) {
            uint32_t x = (t * 2654435761u) ^ (k * 40503u + 12345u);
            x ^= x >> 15, x *= 2246822519u, x ^= x >> 13;
            g[k] = int16_t(int(x % 61u) - 30 + int((k % 29u)) * 2);
            top = std::max(top, std::abs(int(g[k])));
        }
        mm.triRange[t] = uint16_t(top);
    }
    mm.revision++;
}

uint64_t gridHash(const Document& doc) {
    const MicroMesh& mm = *doc.mesh.micro;
    Hash h;
    for (uint32_t t = 0; t < mm.baseTriangles; ++t) {
        const int16_t* g = mm.disp.data() + size_t(t) * mm.samples;
        for (uint32_t k = 0; k < mm.samples; ++k) h.add(uint16_t(g[k]));
        h.add(mm.triRange[t]);
    }
    return h.h;
}

void stroke(Document& doc, BrushType type, glm::vec3 from, glm::vec3 to, int count, float r, float s, bool symX) {
    Symmetry sym;
    sym.x = symX;
    CHECK(doc.sculptor.beginStroke(doc.mesh, brushName(type)));
    for (int k = 0; k < count; ++k) {
        const float u = count > 1 ? float(k) / float(count - 1) : 0.0f;
        DabParams p;
        p.center = glm::normalize(from + (to - from) * u);
        p.radius = r;
        p.strength = s;
        p.viewDir = glm::vec3(0, 0, -1);
        p.strokeDelta = (to - from) / float(std::max(1, count - 1));
        doc.sculptor.dab(doc.mesh, doc.bvh, type, p, sym);
    }
    doc.undo.push(doc.sculptor.endStroke());
}

struct Case {
    BrushType type;
    float radius, strength;
    bool symX;
};

} // namespace

namespace {

// Every stroke's hash, with the fast write loop or without.
std::string runStrokes(bool fast, uint32_t* lastStride) {
    Document doc = makeDoc(makeBenchSphere(600));
    std::string err;
    CHECK(microDivide(doc.mesh, 6, err));
    seedDetail(doc);
    doc.sculptor.microStroke().fastWrite = fast;
    const Case cases[] = {
        {BrushType::Clay, 0.08f, 0.4f, false},   {BrushType::Clay, 0.35f, 0.35f, true},   {BrushType::Clay, 0.7f, 0.35f, false},
        {BrushType::Standard, 0.3f, 0.4f, true}, {BrushType::Smooth, 0.08f, 0.5f, false}, {BrushType::Smooth, 0.35f, 0.5f, true},
        {BrushType::Smooth, 0.7f, 0.5f, false},  {BrushType::SmoothPeaks, 0.4f, 0.5f, false}, {BrushType::Clay, 3.0f, 0.5f, false},
        {BrushType::Smooth, 3.0f, 0.5f, true}, {BrushType::SmoothPeaks, 0.7f, 0.5f, true}, {BrushType::Smooth, 0.2f, 0.5f, false},
    };
    std::string line;
    char buf[32];
    for (const Case& c : cases) {
        stroke(doc, c.type, glm::vec3(-0.5f, -0.2f, 1.0f), glm::vec3(0.5f, 0.25f, 1.0f), 9, c.radius, c.strength, c.symX);
        std::snprintf(buf, sizeof buf, "%016llx ", (unsigned long long)gridHash(doc));
        line += buf;
    }
    if (lastStride) *lastStride = doc.sculptor.microStroke().lastStride;
    return line;
}

} // namespace

TEST(micro_fast_write_matches_the_plain_loop_bit_for_bit) {
    uint32_t stride = 0;
    const std::string fast = runStrokes(true, &stride), plain = runStrokes(false, nullptr);
    std::printf("  micro dab bytes: %s(stride of the last dab %u)\n", fast.c_str(), stride);
    if (fast != plain) std::printf("  plain loop:      %s\n", plain.c_str());
    CHECK(fast == plain);
}
