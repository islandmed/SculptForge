// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The CPU half of GPU brushes (core/BrushCompute.h): the commit of Stroke Delta Tiles, with a stand-in for the GPU
// that returns known deltas. Runs everywhere (no GPU); tests/gpu/test_gpu_sculpt.cpp compares the real kernels.
#include "TestHarness.h"

#include "core/Benchmark.h"
#include "core/BrushCompute.h"
#include "core/Micro.h"
#include "core/Primitives.h"
#include "core/Sculpt.h"

#include <cmath>
#include <cstring>

using namespace sf;

namespace {

// A "GPU" that moves every vertex by a fixed integer delta and every micro point by a fixed number of units.
struct FakeCompute : BrushCompute {
    glm::ivec3 meshDelta{0, 0, 1000};
    int16_t microDelta = 5;
    int meshCalls = 0, microCalls = 0, grabCalls = 0;
    bool decline = false;
    std::string name() const override { return "fake"; }
    bool meshDab(const MeshDabJob& job, MeshDabResult& out) override {
        if (decline) return false;
        ++meshCalls;
        out.areaNormal = glm::vec3(0, 0, 1);
        out.areaCenter = job.center;
        out.weight = 1.0f;
        out.delta.assign(job.verts.size(), meshDelta);
        return true;
    }
    bool microDab(const MicroDabJob& job, MicroDabResult& out) override {
        if (decline) return false;
        ++microCalls;
        out.delta.assign(job.tris.size() * job.samples, microDelta);
        return true;
    }
    bool microGrab(const MicroGrabJob& job, std::vector<int16_t>& delta) override {
        ++grabCalls;
        delta.assign(job.tris.size() * job.samples, microDelta);
        return true;
    }
};

struct Installed {
    explicit Installed(BrushCompute* c) {
        setBrushCompute(c);
        setGpuBrushesEnabled(true);
        setGpuMinPoints(0);
    }
    ~Installed() {
        setBrushCompute(nullptr);
        setGpuMinPoints(16384);
    }
};

DabParams dab(const glm::vec3& c, float r) {
    DabParams p;
    p.center = c;
    p.radius = r;
    p.strength = 0.5f;
    p.viewDir = glm::vec3(0, 0, -1);
    return p;
}

} // namespace

TEST(brush_compute_delta_unit_is_exact) {
    for (float r : {1e-4f, 0.01f, 0.123f, 0.5f, 1.0f, 3.7f, 250.0f}) {
        const float u = meshDeltaUnit(r);
        int e = 0;
        CHECK(std::frexp(u, &e) == 0.5f); // a power of two
        CHECK(u * float(1 << 24) >= r && u * float(1 << 23) < r * 2.0f);
        // q * unit is exact: scaling by a power of two only changes the exponent.
        for (int q : {1, -7, 123456, -(1 << 25) + 3}) CHECK(meshDeltaValue(glm::ivec3(q), u).x == std::ldexp(float(q), e - 1));
    }
}

TEST(brush_compute_mesh_commit_masks_records_and_undoes) {
    FakeCompute fake;
    Installed on(&fake);
    Document doc = makeDoc(makeBenchSphere(20000));
    doc.mesh.mask.assign(doc.mesh.vertexCount(), 0.0f);
    // Mask the left half fully: its vertices must not move.
    for (uint32_t v = 0; v < doc.mesh.vertexCount(); ++v) doc.mesh.mask[v] = doc.mesh.positions[v].x < 0.0f ? 1.0f : 0.0f;
    const std::vector<glm::vec3> before = doc.mesh.positions;
    Symmetry sym;
    sym.x = false;
    CHECK(doc.sculptor.beginStroke(doc.mesh, "Standard"));
    const float r = 0.4f;
    doc.sculptor.dab(doc.mesh, doc.bvh, BrushType::Standard, dab(glm::vec3(0, 0, 1), r), sym);
    UndoRecord rec = doc.sculptor.endStroke();
    CHECK(fake.meshCalls == 1);
    const float step = float(fake.meshDelta.z) * meshDeltaUnit(r);
    size_t moved = 0;
    bool exact = true, maskedStill = true;
    for (uint32_t v = 0; v < doc.mesh.vertexCount(); ++v) {
        const glm::vec3 d = doc.mesh.positions[v] - before[v];
        if (doc.mesh.mask[v] >= 1.0f) maskedStill = maskedStill && d == glm::vec3(0.0f);
        else if (d != glm::vec3(0.0f)) {
            ++moved;
            exact = exact && doc.mesh.positions[v].z == before[v].z + step && d.x == 0.0f && d.y == 0.0f;
        }
    }
    CHECK(moved > 100);
    CHECK(exact);
    CHECK(maskedStill);
    const std::vector<glm::vec3> after = doc.mesh.positions;
    doc.undo.push(std::move(rec));
    CHECK(doc.undo.undo(doc.mesh, doc.bvh));
    CHECK(std::memcmp(doc.mesh.positions.data(), before.data(), before.size() * sizeof(glm::vec3)) == 0);
    CHECK(doc.undo.redo(doc.mesh, doc.bvh));
    CHECK(std::memcmp(doc.mesh.positions.data(), after.data(), after.size() * sizeof(glm::vec3)) == 0);
}

TEST(brush_compute_declined_or_unsupported_dabs_run_on_the_cpu) {
    FakeCompute fake;
    Installed on(&fake);
    Document doc = makeDoc(makeBenchSphere(20000));
    Document ref = makeDoc(makeBenchSphere(20000));
    Symmetry sym;
    // Smooth on a mesh level and an alpha brush never go to the GPU; a declined dab runs on the CPU.
    fake.decline = true;
    for (Document* d : {&doc, &ref}) {
        if (d == &ref) setBrushCompute(nullptr);
        d->sculptor.beginStroke(d->mesh, "Mix");
        d->sculptor.dab(d->mesh, d->bvh, BrushType::Clay, dab(glm::vec3(0, 0, 1), 0.3f), sym);
        d->sculptor.dab(d->mesh, d->bvh, BrushType::Smooth, dab(glm::vec3(0, 0, 1), 0.3f), sym);
        d->undo.push(d->sculptor.endStroke());
        setBrushCompute(&fake);
    }
    CHECK(fake.meshCalls == 0);
    CHECK(doc.mesh.positions == ref.mesh.positions);
    // Off: no calls either.
    fake.decline = false;
    setGpuBrushesEnabled(false);
    CHECK(brushPathName() == "CPU");
    doc.sculptor.beginStroke(doc.mesh, "Off");
    doc.sculptor.dab(doc.mesh, doc.bvh, BrushType::Standard, dab(glm::vec3(0, 0, 1), 0.3f), sym);
    doc.undo.push(doc.sculptor.endStroke());
    CHECK(fake.meshCalls == 0);
    setGpuBrushesEnabled(true);
    CHECK(brushPathName() == "fake");
}

TEST(brush_compute_micro_commit_is_integer_stitched_and_undoes) {
    FakeCompute fake;
    Installed on(&fake);
    Document doc = makeDoc(makeBenchSphere(5000));
    std::string err;
    CHECK(microDivide(doc.mesh, 3, err));
    const std::vector<int16_t> before = doc.mesh.micro->disp;
    Symmetry sym;
    sym.x = false;
    CHECK(doc.sculptor.beginStroke(doc.mesh, "Standard"));
    doc.sculptor.dab(doc.mesh, doc.bvh, BrushType::Standard, dab(glm::vec3(0, 0, 1), 0.3f), sym);
    UndoRecord rec = doc.sculptor.endStroke();
    CHECK(fake.microCalls == 1);
    // Every point of a touched triangle rose by exactly 5 units (shared points too: stitching averages equal values).
    const MicroMesh& mm = *doc.mesh.micro;
    size_t changed = 0;
    bool fives = true;
    for (size_t i = 0; i < mm.disp.size(); ++i)
        if (mm.disp[i] != before[i]) {
            ++changed;
            fives = fives && mm.disp[i] - before[i] == 5;
        }
    CHECK(changed > 0);
    CHECK(fives);
    const std::vector<int16_t> after = mm.disp;
    doc.undo.push(std::move(rec));
    CHECK(doc.undo.undo(doc.mesh, doc.bvh));
    CHECK(doc.mesh.micro->disp == before);
    CHECK(doc.undo.redo(doc.mesh, doc.bvh));
    CHECK(doc.mesh.micro->disp == after);
}
