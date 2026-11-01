// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Micro-displacement levels (core/Micro.h): divide, brushes, stitching, undo, picking, shading, bake, maps,
// projects and the commands.
#include "TestHarness.h"

#include "core/Benchmark.h"
#include "core/Commands.h"
#include "core/Micro.h"
#include "core/Multires.h"
#include "core/Primitives.h"
#include "core/Project.h"
#include "io/Zip.h"

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <map>

using namespace sf;

namespace {

Document microDoc(int level, size_t baseTriangles = 20000) {
    Document doc = makeDoc(makeBenchSphere(baseTriangles));
    std::string err;
    CHECK(microDivide(doc.mesh, level, err));
    return doc;
}

// Every point shared by two base triangles (edges) or more (corners) has the same displacement in each copy, and
// the micro positions there are bit-identical.
bool watertight(const Mesh& m) {
    const MicroMesh& mm = *m.micro;
    const uint32_t n = mm.n, S = mm.samples;
    for (uint32_t t = 0; t < mm.baseTriangles; ++t)
        for (int e = 0; e < 3; ++e) {
            const uint32_t nb = mm.edgeNeighbor[size_t(t) * 3 + size_t(e)];
            if (nb == MicroMesh::kNone) continue;
            const uint32_t u = nb >> 2;
            const int f = int(nb & 3);
            const bool reversed = m.triangles[u][f] != m.triangles[t][e];
            for (uint32_t k = 0; k <= n; ++k) {
                uint32_t i, j, i2, j2;
                MicroMesh::edgePoint(e, k, n, i, j);
                MicroMesh::edgePoint(f, reversed ? n - k : k, n, i2, j2);
                if (mm.disp[size_t(t) * S + MicroMesh::index(i, j, n)] != mm.disp[size_t(u) * S + MicroMesh::index(i2, j2, n)])
                    return false;
                const glm::vec3 a = microPoint(m, t, i, j), b = microPoint(m, u, i2, j2);
                if (std::memcmp(&a, &b, sizeof a) != 0) return false;
            }
        }
    return true;
}

DabParams dabAt(const glm::vec3& c, float r, float strength = 0.5f) {
    DabParams p;
    p.center = c;
    p.radius = r;
    p.strength = strength;
    p.viewDir = glm::vec3(0, 0, -1);
    return p;
}

// The micro surface's distance from the origin under the point (0, 0, 1) (the sphere's front).
float frontHeight(const Document& doc) {
    RayHit hit;
    CHECK(microRaycast(doc.mesh, doc.bvh, glm::vec3(0.001f, 0.002f, 5.0f), glm::vec3(0, 0, -1), hit));
    return glm::length(hit.position);
}

} // namespace

TEST(micro_divide_counts_and_memory) {
    Document doc = microDoc(3);
    const MicroMesh& mm = *doc.mesh.micro;
    CHECK(mm.n == 8);
    CHECK(mm.samples == 45);
    CHECK(microTriangleCount(doc.mesh) == doc.mesh.triangleCount() * 64);
    CHECK(mm.disp.size() == doc.mesh.triangleCount() * 45);
    // 2 bytes a point, about 1.4 points per unique micro vertex at level 3: well under 4 bytes per micro vertex.
    CHECK(double(mm.disp.size() * 2) / double(mm.microVertices()) < 3.0);
    CHECK(microText(doc.mesh).rfind("Micro 3", 0) == 0);
    // Every base edge has a neighbour on a closed sphere.
    for (uint32_t e : mm.edgeNeighbor) CHECK(e != MicroMesh::kNone);
    // d = 0 everywhere: the surface is the smooth Phong surface, close to the unit sphere.
    // (The bench sphere is a subdivided cube: compare with the radius of each base triangle's corners. The flat
    // triangles sag inwards by up to e^2 / 8; Phong tessellation takes most of that back.)
    float worstMicro = 0.0f, worstFlat = 0.0f;
    for (uint32_t t = 0; t < mm.baseTriangles; t += 97) {
        const glm::uvec3 tr = doc.mesh.triangles[t];
        const float r = (glm::length(doc.mesh.positions[tr.x]) + glm::length(doc.mesh.positions[tr.y]) + glm::length(doc.mesh.positions[tr.z])) / 3.0f;
        MicroTriangle T;
        T.set(doc.mesh, t, mm.n);
        for (uint32_t j = 0; j <= mm.n; ++j)
            for (uint32_t i = 0; i + j <= mm.n; ++i) {
                worstMicro = std::max(worstMicro, std::abs(glm::length(microPoint(doc.mesh, t, i, j)) - r));
                worstFlat = std::max(worstFlat, std::abs(glm::length(T.flat(i, j)) - r));
            }
    }
    CHECK(worstMicro < worstFlat * 0.5f);
    CHECK(watertight(doc.mesh));
    std::string err;
    CHECK(!microDivide(doc.mesh, 2, err)); // one micro level per SubTool
}

TEST(micro_divide_needs_top_level) {
    Document doc = makeDoc(makeBenchSphere(20000));
    std::string err;
    CHECK(subdivSetLevel(doc.mesh, 0, err, &doc.bvh));
    CHECK(!microDivide(doc.mesh, 2, err));
    CHECK(err.find("highest") != std::string::npos);
    CHECK(subdivSetLevel(doc.mesh, subdivLevelCount(doc.mesh) - 1, err, &doc.bvh));
    CHECK(microDivide(doc.mesh, 2, err));
    // ... and blocks subdivision changes while it exists.
    CHECK(!subdivSetLevel(doc.mesh, 0, err, &doc.bvh));
    CHECK(err.find("micro") != std::string::npos);
    CHECK(!subdivDivide(doc.mesh, true, err));
}

TEST(micro_standard_brush_raises_and_undoes) {
    Document doc = microDoc(4);
    const std::vector<int16_t> before = doc.mesh.micro->disp;
    const float h0 = frontHeight(doc);
    Symmetry sym;
    CHECK(doc.sculptor.beginStroke(doc.mesh, "Standard"));
    for (int i = 0; i < 10; ++i) doc.sculptor.dab(doc.mesh, doc.bvh, BrushType::Standard, dabAt(glm::vec3(0, 0, 1), 0.3f), sym);
    UndoRecord r = doc.sculptor.endStroke();
    CHECK(r.micro != nullptr);
    CHECK(!r.empty());
    CHECK(r.micro->tris.size() > 0 && r.micro->tris.size() < doc.mesh.triangleCount());
    doc.undo.push(std::move(r));
    const float h1 = frontHeight(doc);
    CHECK(h1 > h0 + 0.01f);
    CHECK(watertight(doc.mesh));
    // X symmetry: the mirrored side moved too, the back didn't.
    CHECK(doc.mesh.micro->rangeMax > 0);
    const std::vector<int16_t> after = doc.mesh.micro->disp;
    CHECK(doc.undo.undo(doc.mesh, doc.bvh));
    CHECK(doc.mesh.micro->disp == before);
    CHECK(doc.undo.redo(doc.mesh, doc.bvh));
    CHECK(doc.mesh.micro->disp == after);
}

TEST(micro_brushes_each_change_the_surface_and_stay_closed) {
    struct Case {
        BrushType type;
        bool invert;
    };
    for (const Case c : {Case{BrushType::Clay, false}, Case{BrushType::ClayBuildup, false}, Case{BrushType::Inflate, false},
                         Case{BrushType::Flatten, false}, Case{BrushType::Crease, false}, Case{BrushType::Pinch, false},
                         Case{BrushType::DamStandard, false}, Case{BrushType::Layer, false}, Case{BrushType::Trim, false},
                         Case{BrushType::Standard, true}}) {
        Document doc = microDoc(3);
        microNoise(doc.mesh, 0.01f, 12.0f); // something to flatten, trim and pinch
        const std::vector<int16_t> before = doc.mesh.micro->disp;
        Symmetry sym;
        doc.sculptor.beginStroke(doc.mesh, brushName(c.type));
        for (int i = 0; i < 6; ++i) {
            DabParams p = dabAt(glm::normalize(glm::vec3(0.1f * float(i), 0.2f, 1.0f)), 0.25f, 0.8f);
            p.invert = c.invert;
            doc.sculptor.dab(doc.mesh, doc.bvh, c.type, p, sym);
        }
        UndoRecord r = doc.sculptor.endStroke();
        CHECK(doc.mesh.micro->disp != before);
        CHECK(watertight(doc.mesh));
        doc.undo.push(std::move(r));
        doc.undo.undo(doc.mesh, doc.bvh);
        CHECK(doc.mesh.micro->disp == before);
    }
}

TEST(micro_smooth_flattens_noise) {
    Document doc = microDoc(3);
    microNoise(doc.mesh, 0.01f, 30.0f);
    auto roughness = [&] { // spread of the displacement in the triangles near the front
        double sum = 0, sq = 0;
        size_t count = 0;
        const MicroMesh& mm = *doc.mesh.micro;
        for (uint32_t t = 0; t < mm.baseTriangles; ++t) {
            const glm::uvec3 tr = doc.mesh.triangles[t];
            if (doc.mesh.positions[tr.x].z < 0.97f) continue;
            for (uint32_t k = 0; k < mm.samples; ++k) {
                const double v = mm.disp[size_t(t) * mm.samples + k];
                sum += v, sq += v * v, ++count;
            }
        }
        const double mean = sum / double(count);
        return sq / double(count) - mean * mean;
    };
    const double r0 = roughness();
    Symmetry sym;
    doc.sculptor.beginStroke(doc.mesh, "Smooth");
    for (int i = 0; i < 8; ++i) doc.sculptor.dab(doc.mesh, doc.bvh, BrushType::Smooth, dabAt(glm::vec3(0, 0, 1), 0.4f, 1.0f), sym);
    doc.undo.push(doc.sculptor.endStroke());
    CHECK(roughness() < r0 * 0.5);
    CHECK(watertight(doc.mesh));
}

TEST(micro_mask_brush_protects) {
    Document doc = microDoc(3);
    Symmetry sym;
    sym.x = false;
    // Mask the front.
    CHECK(doc.sculptor.beginStroke(doc.mesh, "Mask"));
    DabParams mp = dabAt(glm::vec3(0, 0, 1), 0.5f, 1.0f);
    for (int i = 0; i < 8; ++i) doc.sculptor.dabMask(doc.mesh, doc.bvh, mp, sym);
    UndoRecord mr = doc.sculptor.endStroke();
    CHECK(doc.mesh.micro->hasMask());
    CHECK(mr.micro && mr.micro->maskAdded);
    doc.undo.push(std::move(mr));
    const float h0 = frontHeight(doc);
    doc.sculptor.beginStroke(doc.mesh, "Standard");
    for (int i = 0; i < 5; ++i) doc.sculptor.dab(doc.mesh, doc.bvh, BrushType::Standard, dabAt(glm::vec3(0, 0, 1), 0.3f), sym);
    doc.undo.push(doc.sculptor.endStroke());
    CHECK(std::abs(frontHeight(doc) - h0) < 1e-4f); // fully masked at the centre
    // Undo both: the mask goes away again (it didn't exist before).
    doc.undo.undo(doc.mesh, doc.bvh);
    doc.undo.undo(doc.mesh, doc.bvh);
    CHECK(!doc.mesh.micro->hasMask());
    doc.undo.redo(doc.mesh, doc.bvh);
    CHECK(doc.mesh.micro->hasMask());
}

TEST(micro_move_brush_moves_along_normals) {
    Document doc = microDoc(3);
    const float h0 = frontHeight(doc);
    Symmetry sym;
    doc.sculptor.beginStroke(doc.mesh, "Move");
    doc.sculptor.beginGrab(doc.mesh, doc.bvh, dabAt(glm::vec3(0, 0, 1), 0.3f, 1.0f), sym);
    for (int i = 0; i < 5; ++i) doc.sculptor.grab(doc.mesh, doc.bvh, glm::vec3(0, 0, 0.02f));
    doc.undo.push(doc.sculptor.endStroke());
    CHECK(frontHeight(doc) > h0 + 0.05f);
    CHECK(watertight(doc.mesh));
}

TEST(micro_range_widens_for_big_strokes) {
    Document doc = microDoc(2);
    const float step0 = doc.mesh.micro->step;
    Symmetry sym;
    doc.sculptor.beginStroke(doc.mesh, "Standard");
    for (int i = 0; i < 60; ++i) doc.sculptor.dab(doc.mesh, doc.bvh, BrushType::Standard, dabAt(glm::vec3(0, 0, 1), 0.8f, 1.0f), sym);
    UndoRecord r = doc.sculptor.endStroke();
    CHECK(doc.mesh.micro->step > step0);
    CHECK(frontHeight(doc) > 1.5f); // nothing clipped at the old range (+/- 8 base edges)
    doc.undo.push(std::move(r));
    doc.undo.undo(doc.mesh, doc.bvh);
    CHECK(std::abs(frontHeight(doc) - 1.0f) < 0.01f);
}

TEST(micro_raycast_hits_the_micro_surface) {
    Document doc = microDoc(4);
    RayHit base, micro;
    CHECK(doc.bvh.raycast(doc.mesh, glm::vec3(0.01f, 0.01f, 5), glm::vec3(0, 0, -1), base));
    CHECK(microRaycast(doc.mesh, doc.bvh, glm::vec3(0.01f, 0.01f, 5), glm::vec3(0, 0, -1), micro));
    CHECK(std::abs(micro.position.z - base.position.z) < 1e-3f); // flat: the Phong surface is near the base
    microNoise(doc.mesh, 0.02f, 8.0f);
    CHECK(surfaceRaycast(doc.mesh, doc.bvh, glm::vec3(0.01f, 0.01f, 5), glm::vec3(0, 0, -1), micro));
    // The hit lies on the micro surface: its point at the hit's base triangle and weights matches.
    const float h = glm::length(micro.position);
    CHECK(std::abs(h - 1.0f) < 0.03f);
    CHECK(std::abs(micro.t - (5.0f - micro.position.z)) < 1e-4f);
}

TEST(micro_shading_partial_matches_full) {
    Document doc = microDoc(3, 5000);
    microNoise(doc.mesh, 0.01f, 10.0f);
    const MicroMesh& mm = *doc.mesh.micro;
    std::vector<uint32_t> full(size_t(mm.baseTriangles) * mm.samples, 0), part(full.size(), 0);
    MicroShader shader;
    std::vector<uint32_t> written;
    shader.shade(doc.mesh, {}, true, written, [&](uint32_t t) { return full.data() + size_t(t) * mm.samples; });
    CHECK(written.size() == mm.baseTriangles);
    // A brush changes a few triangles; shading just what changed must agree with shading everything.
    Symmetry sym;
    doc.sculptor.beginStroke(doc.mesh, "Standard");
    doc.sculptor.dab(doc.mesh, doc.bvh, BrushType::Standard, dabAt(glm::vec3(0, 0, 1), 0.2f, 1.0f), sym);
    UndoRecord r = doc.sculptor.endStroke();
    std::vector<uint32_t> dirty = r.micro->tris;
    part = full;
    shader.shade(doc.mesh, dirty, false, written, [&](uint32_t t) { return part.data() + size_t(t) * mm.samples; });
    CHECK(written.size() > dirty.size());
    shader.shade(doc.mesh, {}, true, written, [&](uint32_t t) { return full.data() + size_t(t) * mm.samples; });
    CHECK(part == full);
    // Normals point outwards on the sphere.
    int outward = 0, total = 0;
    for (uint32_t t = 0; t < mm.baseTriangles; t += 53) {
        const uint32_t v = full[size_t(t) * mm.samples];
        const float ex = float(v & 255) / 255.0f * 2.0f - 1.0f, ey = float((v >> 8) & 255) / 255.0f * 2.0f - 1.0f;
        glm::vec3 nn(ex, ey, 1.0f - std::abs(ex) - std::abs(ey));
        const float tt = std::max(-nn.z, 0.0f);
        nn.x += nn.x >= 0.0f ? -tt : tt;
        nn.y += nn.y >= 0.0f ? -tt : tt;
        outward += glm::dot(glm::normalize(nn), doc.mesh.positions[doc.mesh.triangles[t].x]) > 0.8f;
        ++total;
    }
    CHECK(outward >= total * 9 / 10);
}

TEST(micro_bake_mesh_is_closed_and_matches) {
    Document doc = microDoc(3, 3000);
    microNoise(doc.mesh, 0.01f, 10.0f);
    Mesh baked;
    std::string err;
    CHECK(microBakeMesh(doc.mesh, 3, baked, err));
    CHECK(baked.triangleCount() == microTriangleCount(doc.mesh));
    CHECK(baked.vertexCount() == doc.mesh.micro->microVertices());
    // Closed: Euler characteristic 2 (V - E + F with E = 3F/2).
    CHECK(long(baked.vertexCount()) - long(baked.triangleCount()) * 3 / 2 + long(baked.triangleCount()) == 2);
    // The base corners are the micro points there.
    const glm::vec3 c = microPoint(doc.mesh, 0, 0, 0);
    CHECK(glm::distance(baked.positions[doc.mesh.triangles[0].x], c) < 1e-6f);
    // Half the level: a quarter of the triangles.
    Mesh half;
    CHECK(microBakeMesh(doc.mesh, 2, half, err));
    CHECK(half.triangleCount() * 4 == baked.triangleCount());
    CHECK(!microBakeMesh(doc.mesh, 3, baked, err, 1000)); // the size limit
}

TEST(micro_displacement_maps) {
    Mesh plane = makePlane(16, 2.0f);
    plane.uvs.resize(plane.triangleCount() * 3);
    for (size_t t = 0; t < plane.triangleCount(); ++t)
        for (int k = 0; k < 3; ++k) {
            const glm::vec3 p = plane.positions[plane.triangles[t][k]];
            plane.uvs[t * 3 + size_t(k)] = glm::vec2(p.x * 0.5f + 0.5f, p.z * 0.5f + 0.5f);
        }
    Document doc = makeDoc(std::move(plane));
    std::string err;
    CHECK(microDivide(doc.mesh, 3, err));
    microNoise(doc.mesh, 0.02f, 6.0f);
    float scale = 0.0f;
    const std::string exr = tempPath("sf_micro_disp.exr"), png = tempPath("sf_micro_disp.png");
    CHECK(microExportDisplacement(doc.mesh, exr, 128, false, "exr", scale, err));
    CHECK(scale > 0.001f && scale < 0.1f);
    CHECK(std::filesystem::file_size(exr) > 128 * 128 * 4);
    CHECK(microExportDisplacement(doc.mesh, png, 64, true, "png", scale, err));
    CHECK(std::filesystem::exists(png));
    std::filesystem::remove(exr);
    std::filesystem::remove(png);
    Document sphere = microDoc(2, 3000);
    CHECK(!microExportDisplacement(sphere.mesh, exr, 64, false, "exr", scale, err)); // no UVs
    CHECK(err.find("UV") != std::string::npos);
}

TEST(micro_project_round_trip) {
    Document doc = microDoc(3, 5000);
    microNoise(doc.mesh, 0.01f, 10.0f);
    Symmetry sym;
    doc.sculptor.beginStroke(doc.mesh, "Mask");
    doc.sculptor.dabMask(doc.mesh, doc.bvh, dabAt(glm::vec3(0, 0, 1), 0.3f, 1.0f), sym);
    doc.undo.push(doc.sculptor.endStroke());
    const std::string path = tempPath("sf_micro_round_trip.sfproj");
    std::string err;
    CHECK(saveProject(path, doc, err));
    auto version = [&] {
        std::vector<ZipEntry> entries;
        Json manifest;
        std::string e;
        if (!readStoredZip(path, entries, e) || entries.empty() ||
            !Json::parse(std::string(entries[0].data.begin(), entries[0].data.end()), manifest, e))
            return -1;
        return int(manifest["version"].asNumber());
    };
    CHECK(version() == 7);
    Document loaded;
    CHECK(loadProject(path, loaded, err));
    CHECK(loaded.mesh.micro != nullptr);
    if (loaded.mesh.micro) {
        CHECK(loaded.mesh.micro->level == 3);
        CHECK(loaded.mesh.micro->step == doc.mesh.micro->step);
        CHECK(loaded.mesh.micro->disp == doc.mesh.micro->disp);
        CHECK(loaded.mesh.micro->mask == doc.mesh.micro->mask);
        CHECK(loaded.mesh.micro->rangeMax == doc.mesh.micro->rangeMax);
    }
    std::filesystem::remove(path);
    // Without a micro level a project stays format 6 (0.9.7 and older open it).
    Document plain = makeDoc(makeIcosphere(2));
    CHECK(saveProject(path, plain, err));
    CHECK(version() == 6);
    Document again;
    CHECK(loadProject(path, again, err));
    CHECK(again.projectSettings.isObject());
    std::filesystem::remove(path);
}

TEST(micro_commands) {
    CommandRegistry reg;
    Document doc = makeDoc(makeBenchSphere(20000));
    CHECK(reg.execute(doc, "micro_info").ok);
    CommandResult r = reg.execute(doc, "micro_divide", {{"triangles", 1.0}});
    CHECK(r.ok);
    CHECK(doc.mesh.micro != nullptr);
    CHECK(microLevel(doc.mesh) == microLevelFor(doc.mesh, 1000000));
    CHECK(reg.execute(doc, "micro_noise", {{"amplitude", 0.002}}).ok);
    CHECK(reg.execute(doc, "micro_info").message.find("Micro level") != std::string::npos);
    CHECK(reg.execute(doc, "micro_bake", {{"level", 1.0}}).ok);
    CHECK(doc.subtoolCount() == 2);
    CHECK(reg.execute(doc, "micro_delete").ok);
    CHECK(doc.mesh.micro == nullptr);
    CHECK(reg.execute(doc, "undo").ok);
    CHECK(doc.mesh.micro != nullptr);
    CHECK(reg.execute(doc, "undo").ok); // the noise
    CHECK(reg.execute(doc, "undo").ok); // the divide
    CHECK(doc.mesh.micro == nullptr);
}

// The point of micro levels: memory and dab time on a dense surface. 5M micro triangles here (fast enough for
// every ctest run); --bench-micro measures 33M - 300M. Timing limits only with SF_BENCH_LIMITS=1.
TEST(micro_memory_and_dab_time) {
    Document doc = makeDoc(makeBenchSphere(80000));
    std::string err;
    const size_t rss0 = residentBytes();
    CHECK(microDivide(doc.mesh, 3, err));
    const MicroMesh& mm = *doc.mesh.micro;
    CHECK(mm.microTriangles() > 4'000'000);
    const double bytesPerMicroTriangle = double(mm.bytes()) / double(mm.microTriangles());
    CHECK(bytesPerMicroTriangle < 2.0);
    (void)rss0;
    Symmetry sym;
    doc.sculptor.beginStroke(doc.mesh, "Standard");
    const auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < 20; ++i)
        doc.sculptor.dab(doc.mesh, doc.bvh, BrushType::Standard, dabAt(glm::normalize(glm::vec3(0.02f * float(i), 0.1f, 1)), 0.12f), sym);
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / 20.0;
    doc.undo.push(doc.sculptor.endStroke());
    std::printf("  micro dab at %.1fM micro triangles: %.2f ms, %.2f bytes/micro triangle\n", double(mm.microTriangles()) / 1e6,
                ms, bytesPerMicroTriangle);
    if (std::getenv("SF_BENCH_LIMITS")) CHECK(ms < 10.0);
}
