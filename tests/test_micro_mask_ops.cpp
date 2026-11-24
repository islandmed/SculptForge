// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// On a micro level the brushes and the renderer use the level's own mask (one value per grid point). Ctrl+W
// (facegroup_from_mask) and Mask by Cavity / Peaks / Colour / Facing / Face Group / Border read and wrote only the
// base mesh's mask, so they did nothing visible there. The promises: each op sets the micro mask per grid point from the
// micro surface (replace or add as on the base mesh), seams closed, one undo step that brings the old mask back exactly;
// Ctrl+W groups the base triangles most of whose grid points are masked and then clears the micro mask.
#include "TestHarness.h"

#include "core/Benchmark.h"
#include "core/Commands.h"
#include "core/Masking.h"
#include "core/Micro.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace sf;

namespace {

const CommandRegistry& registry() {
    static CommandRegistry r;
    return r;
}

Document microDoc(bool keep = false) {
    Document doc = makeDoc(makeBenchSphere(20000));
    std::string err;
    CHECK(microDivide(doc.mesh, 3, err));
    doc.sculptor.keepEditable = keep;
    return doc;
}

std::vector<uint8_t> microMask(const Mesh& m) { return m.micro->hasMask() ? m.micro->mask : std::vector<uint8_t>(); }

size_t count(const std::vector<uint8_t>& mask, int above) {
    return size_t(std::count_if(mask.begin(), mask.end(), [&](uint8_t v) { return v > above; }));
}

// Every copy of a grid point on a shared edge or corner holds one value (as tests/test_kse_move_mask.cpp).
bool seamsAgree(const Mesh& m) {
    const MicroMesh& mm = *m.micro;
    if (!mm.hasMask()) return true;
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
                if (mm.mask[size_t(t) * S + MicroMesh::index(i, j, n)] != mm.mask[size_t(u) * S + MicroMesh::index(i2, j2, n)]) return false;
            }
        }
    return true;
}

// The op changes the micro mask (and not the base mesh's, which it hasn't got), seams closed; undo brings the old one
// back exactly, redo the new one.
void undoRedoExact(Document& doc, const std::vector<uint8_t>& before) {
    const std::vector<uint8_t> after = microMask(doc.mesh);
    CHECK(after != before);
    CHECK(!doc.mesh.hasMask());
    CHECK(seamsAgree(doc.mesh));
    CHECK(registry().execute(doc, "undo").ok);
    CHECK(microMask(doc.mesh) == before);
    CHECK(registry().execute(doc, "redo").ok);
    CHECK(microMask(doc.mesh) == after);
}

template <class F>
void forPoints(const Mesh& m, F f) {
    const MicroMesh& mm = *m.micro;
    for (uint32_t t = 0; t < mm.baseTriangles; ++t)
        for (uint32_t j = 0; j <= mm.n; ++j)
            for (uint32_t i = 0; i + j <= mm.n; ++i) f(t, i, j, size_t(t) * mm.samples + MicroMesh::index(i, j, mm.n));
}

} // namespace

TEST(micro_mask_by_cavity_and_peaks) {
    Document doc = microDoc();
    microNoise(doc.mesh, 0.01f, 60.0f, 3); // pores and ridges on the micro level only (the base stays a sphere)
    CHECK(registry().execute(doc, "mask_by_cavity").ok);
    const std::vector<uint8_t> cavity = microMask(doc.mesh);
    CHECK(doc.mesh.micro->hasMask());
    if (!doc.mesh.micro->hasMask()) return;
    const size_t total = doc.mesh.micro->mask.size();
    CHECK(count(cavity, 250) > total / 50 && count(cavity, 250) < total / 2);
    undoRedoExact(doc, {});
    CHECK(registry().execute(doc, "mask_by_peaks").ok); // (replaces)
    const std::vector<uint8_t> peaks = microMask(doc.mesh);
    CHECK(count(peaks, 250) > total / 50 && count(peaks, 250) < total / 2);
    undoRedoExact(doc, cavity);
    // Cavities sit low, peaks high, and no point is both.
    const std::vector<int16_t> d = doc.mesh.micro->disp.toVector();
    double lowSum = 0, highSum = 0;
    size_t lows = 0, highs = 0, both = 0;
    for (size_t k = 0; k < total; ++k) {
        if (cavity[k] > 250) lowSum += d[k], ++lows;
        if (peaks[k] > 250) highSum += d[k], ++highs;
        both += cavity[k] > 250 && peaks[k] > 250;
    }
    CHECK(lows > 0 && highs > 0);
    CHECK(lowSum / double(lows) < highSum / double(highs));
    CHECK(both == 0);
    // The smooth sphere has no cavity worth masking: the op leaves the mask empty.
    Document smooth = microDoc();
    CHECK(registry().execute(smooth, "mask_by_cavity").ok);
    CHECK(count(microMask(smooth.mesh), 0) == 0);
}

TEST(micro_mask_by_facing) {
    Document doc = microDoc();
    CHECK(registry().execute(doc, "mask_by_facing", {{"x", 0.0}, {"y", 0.0}, {"z", 1.0}, {"angle", 60.0}}).ok);
    const std::vector<uint8_t> front = microMask(doc.mesh);
    CHECK(doc.mesh.micro->hasMask());
    if (!doc.mesh.micro->hasMask()) return;
    // On the unit sphere a point faces +z by its own angle: fully masked inside 45 degrees, free beyond 60.
    bool inside = true, outside = true;
    forPoints(doc.mesh, [&](uint32_t t, uint32_t i, uint32_t j, size_t k) {
        const float z = glm::normalize(microPoint(doc.mesh, t, i, j)).z;
        if (z > std::cos(glm::radians(42.0f))) inside = inside && front[k] >= 250;
        if (z < std::cos(glm::radians(63.0f))) outside = outside && front[k] == 0;
    });
    CHECK(inside);
    CHECK(outside);
    undoRedoExact(doc, {});
    // add keeps what is masked: the back joins the front.
    CHECK(registry().execute(doc, "mask_by_facing", {{"x", 0.0}, {"y", 0.0}, {"z", -1.0}, {"angle", 30.0}, {"add", true}}).ok);
    const std::vector<uint8_t> both = microMask(doc.mesh);
    bool kept = true;
    for (size_t k = 0; k < both.size(); ++k) kept = kept && both[k] >= front[k];
    CHECK(kept);
    CHECK(count(both, 250) > count(front, 250));
    undoRedoExact(doc, front);
}

TEST(micro_mask_by_facegroup) {
    Document doc = microDoc();
    Mesh& m = doc.mesh;
    m.facegroups.assign(m.triangleCount(), 0u);
    for (uint32_t t = 0; t < m.triangleCount(); ++t)
        if (m.positions[m.triangles[t].x].z > 0.5f) m.facegroups[t] = 1;
    CHECK(registry().execute(doc, "mask_by_facegroup", {{"group", 1.0}}).ok);
    const std::vector<uint8_t> mask = microMask(m);
    CHECK(m.micro->hasMask());
    if (!m.micro->hasMask()) return;
    // Every point of a group 1 triangle is masked; a triangle not touching the group stays free.
    std::vector<uint8_t> nearGroup(m.vertexCount(), 0);
    for (uint32_t t = 0; t < m.triangleCount(); ++t)
        if (m.facegroups[t] == 1) nearGroup[m.triangles[t].x] = nearGroup[m.triangles[t].y] = nearGroup[m.triangles[t].z] = 1;
    bool in = true, out = true;
    forPoints(m, [&](uint32_t t, uint32_t, uint32_t, size_t k) {
        const glm::uvec3 tri = m.triangles[t];
        if (m.facegroups[t] == 1) in = in && mask[k] == 255;
        else if (!nearGroup[tri.x] && !nearGroup[tri.y] && !nearGroup[tri.z]) out = out && mask[k] == 0;
    });
    CHECK(in);
    CHECK(out);
    undoRedoExact(doc, {});
}

TEST(micro_mask_by_color) {
    Document doc = microDoc();
    Mesh& m = doc.mesh;
    m.colors.assign(m.vertexCount(), glm::vec3(1.0f));
    for (uint32_t v = 0; v < m.vertexCount(); ++v)
        if (m.positions[v].x > 0.5f) m.colors[v] = glm::vec3(1.0f, 0.0f, 0.0f);
    CHECK(registry().execute(doc, "mask_by_color", {{"color", std::string("#ff0000")}}).ok);
    const std::vector<uint8_t> mask = microMask(m);
    CHECK(m.micro->hasMask());
    if (!m.micro->hasMask()) return;
    bool red = true, white = true;
    forPoints(m, [&](uint32_t t, uint32_t, uint32_t, size_t k) {
        const glm::uvec3 tri = m.triangles[t];
        const int reds = (m.positions[tri.x].x > 0.5f) + (m.positions[tri.y].x > 0.5f) + (m.positions[tri.z].x > 0.5f);
        if (reds == 3) red = red && mask[k] == 255;
        if (reds == 0) white = white && mask[k] == 0;
    });
    CHECK(red);
    CHECK(white);
    undoRedoExact(doc, {});
}

TEST(micro_mask_border_facegroups) {
    Document doc = microDoc();
    Mesh& m = doc.mesh;
    m.facegroups.assign(m.triangleCount(), 0u);
    for (uint32_t t = 0; t < m.triangleCount(); ++t)
        if (m.positions[m.triangles[t].x].y > 0.0f) m.facegroups[t] = 1;
    CHECK(registry().execute(doc, "mask_border", {{"facegroups", true}}).ok);
    const std::vector<uint8_t> mask = microMask(m);
    CHECK(m.micro->hasMask());
    if (!m.micro->hasMask()) return;
    CHECK(count(mask, 250) > 0);
    // As the base mesh shades its border vertices (rings 1): a triangle with all three corners on the border is fully
    // masked, one with none of them free.
    const std::vector<uint8_t> border = borderVertices(m, true);
    bool full = true, free = true;
    forPoints(m, [&](uint32_t t, uint32_t, uint32_t, size_t k) {
        const glm::uvec3 tri = m.triangles[t];
        const int on = border[tri.x] + border[tri.y] + border[tri.z];
        if (on == 3) full = full && mask[k] == 255;
        if (on == 0) free = free && mask[k] == 0;
    });
    CHECK(full);
    CHECK(free);
    undoRedoExact(doc, {});
}

TEST(micro_mask_ops_keep_editable) {
    // Keep Strokes Editable on: a kept stroke under the op, the op's mask holds through a stroke and its undo.
    Document doc = microDoc(true);
    Sculptor& sc = doc.sculptor;
    DabParams p;
    p.center = glm::vec3(0.0f, 0.0f, 1.0f), p.radius = 0.15f, p.strength = 0.5f, p.viewDir = -p.center;
    Symmetry none;
    none.x = false;
    CHECK(sc.beginStroke(doc.mesh, "Standard"));
    for (int i = 0; i < 4; ++i) sc.dab(doc.mesh, doc.bvh, BrushType::Standard, p, none);
    doc.undo.push(sc.endStroke());
    CHECK(registry().execute(doc, "mask_by_facing", {{"x", 0.0}, {"y", 0.0}, {"z", 1.0}, {"angle", 40.0}}).ok);
    const std::vector<uint8_t> mask = microMask(doc.mesh);
    CHECK(count(mask, 250) > 0);
    const std::vector<int16_t> grid = doc.mesh.micro->disp.toVector();
    p.center = glm::vec3(0.0f, 0.0f, -1.0f), p.viewDir = -p.center; // (elsewhere: free)
    CHECK(sc.beginStroke(doc.mesh, "Standard"));
    for (int i = 0; i < 4; ++i) sc.dab(doc.mesh, doc.bvh, BrushType::Standard, p, none);
    doc.undo.push(sc.endStroke());
    CHECK(microMask(doc.mesh) == mask);
    CHECK(registry().execute(doc, "undo").ok);
    CHECK(microMask(doc.mesh) == mask);
    CHECK(doc.mesh.micro->disp.toVector() == grid);
    CHECK(registry().execute(doc, "undo").ok);
    CHECK(count(microMask(doc.mesh), 0) == 0);
}

TEST(micro_ctrl_w_groups_from_micro_mask) {
    Document doc = microDoc();
    Sculptor& sc = doc.sculptor; // (painted with the Mask brush: a soft edge across base triangles)
    DabParams p;
    p.center = glm::vec3(0.0f, 0.0f, 1.0f), p.radius = 0.2f, p.strength = 1.0f, p.viewDir = -p.center;
    Symmetry none;
    none.x = false;
    CHECK(sc.beginStroke(doc.mesh, "Mask"));
    for (int i = 0; i < 3; ++i) sc.dabMask(doc.mesh, doc.bvh, p, none);
    doc.undo.push(sc.endStroke());
    CHECK(!doc.mesh.hasMask());
    const std::vector<uint8_t> mask = microMask(doc.mesh);
    CHECK(count(mask, 127) > 0);
    if (mask.empty()) return;
    // Ctrl+W: facegroup_from_mask, then mask_clear (App::groupMasked).
    std::vector<uint32_t> before(doc.mesh.triangleCount());
    for (uint32_t t = 0; t < doc.mesh.triangleCount(); ++t) before[t] = doc.mesh.facegroupAt(t);
    const CommandResult r = registry().execute(doc, "facegroup_from_mask");
    CHECK(r.ok);
    const MicroMesh& mm = *doc.mesh.micro;
    auto maskedPoints = [&](uint32_t t) {
        return size_t(std::count_if(mask.begin() + std::ptrdiff_t(t * mm.samples), mask.begin() + std::ptrdiff_t((t + 1) * mm.samples),
                                    [](uint8_t v) { return v >= 128; }));
    };
    const uint32_t id = 1 + *std::max_element(before.begin(), before.end()); // (the new group)
    bool rule = true;
    size_t grouped = 0;
    for (uint32_t t = 0; t < doc.mesh.triangleCount(); ++t) {
        const size_t masked = maskedPoints(t);
        const bool in = doc.mesh.facegroupAt(t) == id;
        rule = rule && (in || doc.mesh.facegroupAt(t) == before[t]);
        grouped += in;
        rule = rule && in == (2 * masked > mm.samples);
    }
    CHECK(grouped > 0);
    CHECK(rule);
    CHECK(registry().execute(doc, "mask_clear").ok);
    CHECK(count(microMask(doc.mesh), 0) == 0);
    CHECK(registry().execute(doc, "undo").ok); // (the mask back, the group stays)
    CHECK(microMask(doc.mesh) == mask);
    size_t still = 0;
    for (uint32_t t = 0; t < doc.mesh.triangleCount(); ++t) still += doc.mesh.facegroupAt(t) == id;
    CHECK(still == grouped);
}

// Geometric invariants of every op on a micro level with detail: a mask op never moves the surface (the micro grid and
// the base, bit-identical), leaves hidden triangles' mask alone, and keeps every shared point one value.
TEST(micro_mask_ops_invariants) {
    Document doc = microDoc();
    Mesh& m = doc.mesh;
    microNoise(m, 0.01f, 60.0f, 5);
    m.facegroups.assign(m.triangleCount(), 0u);
    m.colors.assign(m.vertexCount(), glm::vec3(1.0f));
    for (uint32_t t = 0; t < m.triangleCount(); ++t)
        if (m.positions[m.triangles[t].x].y > 0.0f) m.facegroups[t] = 1;
    for (uint32_t v = 0; v < m.vertexCount(); ++v)
        if (m.positions[v].x > 0.3f) m.colors[v] = glm::vec3(1.0f, 0.0f, 0.0f);
    // Hide the back (z < -0.5): its mask must not change.
    m.hidden.assign(m.triangleCount(), uint8_t(0));
    for (uint32_t t = 0; t < m.triangleCount(); ++t)
        if (m.positions[m.triangles[t].x].z < -0.5f) m.hidden[t] = 1;
    CHECK(registry().execute(doc, "mask_all").ok); // (every visible point masked, the hidden ones free: replace keeps theirs)
    const std::vector<glm::vec3> positions = m.positions;
    const std::vector<int16_t> grid = m.micro->disp.toVector();
    const std::vector<std::pair<const char*, Args>> ops = {
        {"mask_by_cavity", {}},
        {"mask_by_peaks", {{"threshold", 0.0}}},
        {"mask_by_facing", {{"x", 0.0}, {"y", 0.0}, {"z", 1.0}}},
        {"mask_by_facing", {{"x", 1.0}, {"y", 0.0}, {"z", 0.0}, {"add", true}}},
        {"mask_by_color", {{"color", std::string("#ff0000")}}},
        {"mask_by_facegroup", {{"group", 1.0}}},
        {"mask_border", {{"facegroups", true}, {"rings", 2.0}, {"soft", true}}},
    };
    for (const auto& [name, args] : ops) {
        const std::vector<uint8_t> before = microMask(m);
        CHECK(registry().execute(doc, name, args).ok);
        const std::vector<uint8_t> after = microMask(m);
        CHECK(m.positions == positions);
        CHECK(m.micro->disp.toVector() == grid);
        CHECK(seamsAgree(m));
        bool hiddenKept = after.size() == before.size();
        const uint32_t S = m.micro->samples;
        for (uint32_t t = 0; t < m.triangleCount() && hiddenKept; ++t)
            if (m.hidden[t])
                hiddenKept = std::equal(before.begin() + std::ptrdiff_t(t * S), before.begin() + std::ptrdiff_t((t + 1) * S),
                                        after.begin() + std::ptrdiff_t(t * S));
        if (!hiddenKept) printf("  %s changed a hidden triangle's mask\n", name);
        CHECK(hiddenKept);
    }
}
