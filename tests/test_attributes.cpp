// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Optional mesh attributes (mask, vertex paint, face groups, hidden, UVs): helpers,
// preservation through topology changes and undo, brush hooks and IO.
#include "TestHarness.h"

#include "core/Commands.h"
#include "core/Decimate.h"
#include "core/Primitives.h"
#include "core/Sculpt.h"
#include "core/Voxel.h"
#include "io/GltfIO.h"
#include "io/ObjIO.h"

#include <algorithm>
#include <cmath>
#include <filesystem>

using namespace sf;

namespace {

// Mask = 1 on the upper half (y > 0), red paint on the +x half, face group 1 on
// triangles whose centre has z > 0, the lowest triangle hidden.
void paintTestAttributes(Mesh& m) {
    m.ensureMask();
    m.ensureColors();
    for (uint32_t v = 0; v < m.vertexCount(); ++v) {
        m.mask[v] = m.positions[v].y > 0.0f ? 1.0f : 0.0f;
        m.colors[v] = m.positions[v].x > 0.0f ? glm::vec3(1, 0, 0) : glm::vec3(1);
    }
    m.ensureFaceGroups();
    m.ensureHidden();
    for (uint32_t t = 0; t < m.triangleCount(); ++t) {
        const auto& tri = m.triangles[t];
        glm::vec3 c = (m.positions[tri.x] + m.positions[tri.y] + m.positions[tri.z]) / 3.0f;
        m.facegroups[t] = c.z > 0.0f ? 1u : 0u;
        m.hidden[t] = c.y < -0.97f ? 1 : 0;
    }
    m.uvs.resize(m.triangleCount() * 3);
    for (size_t i = 0; i < m.uvs.size(); ++i) {
        const glm::vec3& p = m.positions[m.triangles[i / 3][int(i % 3)]];
        m.uvs[i] = glm::vec2(p.x, p.y) * 0.5f + 0.5f;
    }
}

bool allIn(const std::vector<float>& v, float lo, float hi) {
    return std::all_of(v.begin(), v.end(), [&](float x) { return x >= lo && x <= hi; });
}

} // namespace

TEST(attribute_helpers_default_empty) {
    Mesh m = makeIcosphere(2);
    CHECK(!m.hasMask() && !m.hasColors() && !m.hasFaceGroups() && !m.hasHidden() && !m.hasUvs());
    CHECK(m.maskAt(3) == 0.0f && m.facegroupAt(5) == 0u && !m.triangleHidden(5) && !m.vertexHidden(0));
    CHECK(m.colorAt(0, glm::vec3(0.5f)) == glm::vec3(0.5f));
    m.ensureMask(0.25f);
    CHECK(m.mask.size() == m.vertexCount() && m.maskAt(7) == 0.25f);
    m.ensureMask(0.9f); // already present: unchanged
    CHECK(m.maskAt(7) == 0.25f);
    m.ensureHidden();
    for (uint32_t t : m.vertexTriangles(0)) m.hidden[t] = 1;
    uint32_t opposite = 0;
    for (uint32_t v = 0; v < m.vertexCount(); ++v)
        if (glm::distance(m.positions[v], m.positions[0]) > glm::distance(m.positions[opposite], m.positions[0])) opposite = v;
    CHECK(m.vertexHidden(0) && !m.vertexHidden(opposite) && m.triangleHidden(m.vertexTriangles(0)[0]));
    m.clearMask();
    CHECK(!m.hasMask() && m.mask.capacity() == 0);
    // Stale attributes are dropped instead of being read out of range.
    m.colors.resize(3);
    m.rebuildTopology();
    CHECK(!m.hasColors() && m.hasHidden());
}

TEST(attributes_survive_subdivision) {
    Mesh m = makeIcosphere(2);
    paintTestAttributes(m);
    Mesh s = loopSubdivide(m);
    CHECK(s.mask.size() == s.vertexCount() && s.colors.size() == s.vertexCount());
    CHECK(s.facegroups.size() == s.triangleCount() && s.hidden.size() == s.triangleCount());
    CHECK(s.uvs.size() == s.triangleCount() * 3);
    for (uint32_t v = 0; v < m.vertexCount(); ++v) CHECK(s.mask[v] == m.mask[v] && s.colors[v] == m.colors[v]);
    CHECK(allIn(s.mask, 0.0f, 1.0f));
    for (uint32_t t = 0; t < m.triangleCount(); ++t)
        for (int k = 0; k < 4; ++k) {
            CHECK(s.facegroups[t * 4 + k] == m.facegroups[t]);
            CHECK(s.hidden[t * 4 + k] == m.hidden[t]);
        }
    // The first child keeps the parent's first corner UV; midpoints average.
    CHECK(s.uvs[0] == m.uvs[0]);
    CHECK(glm::distance(s.uvs[1], (m.uvs[0] + m.uvs[1]) * 0.5f) < 1e-6f);

    Document d = makeDoc(makeIcosphere(2));
    paintTestAttributes(d.mesh);
    CommandRegistry reg;
    CHECK(reg.execute(d, "subdivide", {{"levels", 2.0}}).ok);
    CHECK(d.mesh.mask.size() == d.mesh.vertexCount() && d.mesh.facegroups.size() == d.mesh.triangleCount());
}

TEST(attributes_survive_decimation) {
    Mesh m = loopSubdivide(makeIcosphere(3));
    paintTestAttributes(m);
    Mesh dm = decimate(m, m.triangleCount() / 4);
    CHECK(dm.triangleCount() < m.triangleCount() / 2);
    CHECK(dm.mask.size() == dm.vertexCount() && dm.colors.size() == dm.vertexCount());
    CHECK(dm.facegroups.size() == dm.triangleCount() && dm.hidden.size() == dm.triangleCount());
    CHECK(dm.uvs.size() == dm.triangleCount() * 3);
    // Kept vertices carry their exact values (vertices are a subset of the input).
    int mismatches = 0;
    for (uint32_t v = 0; v < dm.vertexCount(); ++v) {
        const glm::vec3& p = dm.positions[v];
        if ((p.y > 0.0f ? 1.0f : 0.0f) != dm.mask[v]) ++mismatches;
        if ((p.x > 0.0f ? glm::vec3(1, 0, 0) : glm::vec3(1)) != dm.colors[v]) ++mismatches;
    }
    CHECK(mismatches == 0);
    std::vector<Mesh> lods = generateLods(m, 3, 0.5f);
    CHECK(lods.size() == 3 && lods[2].mask.size() == lods[2].vertexCount() &&
          lods[2].facegroups.size() == lods[2].triangleCount());
}

TEST(attributes_transfer_through_remesh) {
    Mesh m = makeIcosphere(4);
    paintTestAttributes(m);
    std::string err;
    Mesh r = voxelRemesh(m, 48, &err);
    CHECK(r.triangleCount() > 0);
    CHECK(r.mask.size() == r.vertexCount() && r.colors.size() == r.vertexCount());
    CHECK(!r.hasHidden() && !r.hasUvs()); // documented reset
    // Face groups follow the nearest source triangle.
    CHECK(r.facegroups.size() == r.triangleCount());
    int wrongGroup = 0, groupChecked = 0;
    for (uint32_t t = 0; t < r.triangleCount(); ++t) {
        const auto& tri = r.triangles[t];
        glm::vec3 c = (r.positions[tri.x] + r.positions[tri.y] + r.positions[tri.z]) / 3.0f;
        if (std::abs(c.z) > 0.15f) {
            ++groupChecked;
            if (r.facegroups[t] != (c.z > 0.0f ? 1u : 0u)) ++wrongGroup;
        }
    }
    CHECK(groupChecked > 100 && wrongGroup == 0);
    int wrong = 0, checked = 0;
    for (uint32_t v = 0; v < r.vertexCount(); ++v) {
        const glm::vec3& p = r.positions[v];
        if (std::abs(p.y) > 0.15f) {
            ++checked;
            if (r.mask[v] != (p.y > 0.0f ? 1.0f : 0.0f)) ++wrong;
        }
        if (std::abs(p.x) > 0.15f && r.colors[v] != (p.x > 0.0f ? glm::vec3(1, 0, 0) : glm::vec3(1))) ++wrong;
    }
    CHECK(checked > 100 && wrong == 0);
}

TEST(attributes_carried_by_weld_and_snapshot_undo) {
    // Two triangles sharing an edge, with the shared vertices duplicated (a UV seam).
    Mesh raw;
    raw.positions = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}};
    raw.triangles = {{0, 1, 2}, {3, 4, 5}};
    raw.colors = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {1, 1, 0}, {1, 1, 1}, {0, 1, 1}};
    raw.facegroups = {4, 9};
    raw.uvs = {{0, 0}, {1, 0}, {0, 1}, {0.5f, 0}, {1, 1}, {0, 0.5f}};
    Mesh w = weldImported(raw);
    CHECK(w.vertexCount() == 4 && w.colors.size() == 4 && w.colors[1] == glm::vec3(0, 1, 0));
    CHECK(w.facegroups == std::vector<uint32_t>({4, 9}) && w.uvs.size() == 6 && w.uvs[3] == glm::vec2(0.5f, 0));

    Document d = makeDoc(makeIcosphere(2));
    paintTestAttributes(d.mesh);
    const Mesh before = d.mesh;
    CommandRegistry reg;
    CHECK(reg.execute(d, "remesh", {{"resolution", 40.0}}).ok);
    CHECK(d.undo.undo(d.mesh, d.bvh));
    CHECK(d.mesh.mask == before.mask && d.mesh.colors == before.colors && d.mesh.facegroups == before.facegroups &&
          d.mesh.hidden == before.hidden && d.mesh.uvs == before.uvs);
    CHECK(d.undo.redo(d.mesh, d.bvh));
    CHECK(d.mesh.vertexCount() != before.vertexCount() && d.mesh.mask.size() == d.mesh.vertexCount());
}

TEST(mask_and_color_stroke_undo) {
    Document d = makeDoc(makeIcosphere(3));
    // A paint/mask stroke: record before writing, through the sculptor.
    CHECK(d.sculptor.beginStroke(d.mesh, "Paint"));
    d.mesh.ensureMask();
    d.mesh.ensureColors();
    for (uint32_t v = 0; v < 10; ++v) {
        d.sculptor.recordMask(d.mesh, v);
        d.sculptor.recordColor(d.mesh, v);
        d.mesh.mask[v] = 0.5f;
        d.sculptor.recordMask(d.mesh, v); // recorded once per stroke
        d.mesh.mask[v] = 1.0f;
        d.mesh.colors[v] = glm::vec3(0, 0, 1);
    }
    UndoRecord r = d.sculptor.endStroke();
    CHECK(r.mask.indices.size() == 10 && r.colors.indices.size() == 10 && r.positions.empty() && !r.empty());
    d.undo.push(std::move(r));
    CHECK(d.undo.undo(d.mesh, d.bvh));
    CHECK(d.mesh.maskAt(3) == 0.0f && d.mesh.colorAt(3) == glm::vec3(1));
    CHECK(d.undo.redo(d.mesh, d.bvh));
    CHECK(d.mesh.maskAt(3) == 1.0f && d.mesh.colorAt(3) == glm::vec3(0, 0, 1));

    // Whole-attribute snapshots for operations like "clear mask" or "hide group".
    d.undo.push(UndoRecord::captureMask(d.mesh, "Clear Mask"));
    d.mesh.clearMask();
    d.undo.push(UndoRecord::captureHidden(d.mesh, "Hide"));
    d.mesh.ensureHidden()[0] = 1;
    d.undo.push(UndoRecord::captureFaceGroups(d.mesh, "Group", std::vector<uint32_t>{2}));
    d.mesh.ensureFaceGroups()[2] = 7;
    CHECK(d.undo.undo(d.mesh, d.bvh) && d.mesh.facegroupAt(2) == 0u);
    CHECK(d.undo.undo(d.mesh, d.bvh) && !d.mesh.triangleHidden(0));
    CHECK(d.undo.undo(d.mesh, d.bvh) && d.mesh.maskAt(3) == 1.0f);
    CHECK(d.undo.redo(d.mesh, d.bvh) && d.mesh.maskAt(3) == 0.0f);
}

TEST(brushes_respect_mask) {
    for (BrushType type : {BrushType::Standard, BrushType::Inflate, BrushType::Smooth, BrushType::Clay}) {
        Document d = makeDoc(makeIcosphere(4));
        d.mesh.ensureMask();
        for (uint32_t v = 0; v < d.mesh.vertexCount(); ++v) d.mesh.mask[v] = d.mesh.positions[v].x > 0.0f ? 1.0f : 0.0f;
        const std::vector<glm::vec3> before = d.mesh.positions;
        DabParams p;
        p.center = glm::vec3(0, 0, 1);
        p.radius = 0.6f;
        p.strength = 1.0f;
        d.sculptor.beginStroke(d.mesh, "Masked");
        for (int i = 0; i < 4; ++i) d.sculptor.dab(d.mesh, d.bvh, type, p, Symmetry{false, false, false});
        UndoRecord r = d.sculptor.endStroke();
        bool maskedStill = true, freeMoved = false;
        for (uint32_t v = 0; v < d.mesh.vertexCount(); ++v) {
            if (d.mesh.mask[v] == 1.0f) maskedStill &= d.mesh.positions[v] == before[v];
            else freeMoved |= d.mesh.positions[v] != before[v];
        }
        CHECK(maskedStill && freeMoved);
        for (uint32_t v : r.positions.indices) CHECK(d.mesh.mask[v] < 1.0f); // fully masked vertices aren't even recorded
    }
    // Partial mask scales the displacement; recipe and move brushes go through the same hook.
    Document d = makeDoc(makeIcosphere(4));
    d.mesh.ensureMask(0.5f);
    DabParams p;
    p.center = glm::vec3(0, 0, 1);
    p.radius = 0.4f;
    p.strength = 1.0f;
    d.sculptor.beginStroke(d.mesh, "Move");
    d.sculptor.beginGrab(d.mesh, d.bvh, p, Symmetry{false, false, false});
    d.sculptor.grab(d.mesh, d.bvh, glm::vec3(0, 0, 0.5f));
    d.sculptor.endStroke();
    float maxZ = 0;
    for (const auto& v : d.mesh.positions) maxZ = std::max(maxZ, v.z);
    CHECK(std::abs(maxZ - 1.25f) < 0.01f);
    d.mesh.ensureMask(); // already present
    std::fill(d.mesh.mask.begin(), d.mesh.mask.end(), 1.0f);
    const std::vector<glm::vec3> before = d.mesh.positions;
    d.sculptor.beginStroke(d.mesh, "Recipe");
    d.sculptor.dabRecipe(d.mesh, d.bvh, d.brushes.front(), p, Symmetry{});
    CHECK(d.sculptor.endStroke().empty() && d.mesh.positions == before);
}

TEST(hidden_triangles_skip_picking_and_brushes) {
    Document d = makeDoc(makeIcosphere(4));
    d.mesh.ensureHidden();
    for (uint32_t t = 0; t < d.mesh.triangleCount(); ++t) {
        const auto& tri = d.mesh.triangles[t];
        d.mesh.hidden[t] = d.mesh.positions[tri.x].z + d.mesh.positions[tri.y].z + d.mesh.positions[tri.z].z > 0.0f;
    }
    RayHit hit;
    CHECK(d.bvh.raycast(d.mesh, glm::vec3(0, 0, 5), glm::vec3(0, 0, -1), hit));
    CHECK(hit.position.z < -0.9f); // went through the hidden front to the visible back
    const std::vector<glm::vec3> before = d.mesh.positions;
    DabParams p;
    p.center = glm::vec3(0, 0, 1);
    p.radius = 0.5f;
    p.strength = 1.0f;
    d.sculptor.beginStroke(d.mesh, "Hidden");
    d.sculptor.dab(d.mesh, d.bvh, BrushType::Standard, p, Symmetry{false, false, false});
    d.sculptor.endStroke();
    for (uint32_t v = 0; v < d.mesh.vertexCount(); ++v)
        if (d.mesh.vertexHidden(v)) CHECK(d.mesh.positions[v] == before[v]);
    std::vector<uint32_t> verts;
    brushVertices(d.mesh, d.bvh, p.center, p.radius, verts);
    CHECK(verts.empty());
    std::fill(d.mesh.hidden.begin(), d.mesh.hidden.end(), uint8_t(1));
    CHECK(!d.bvh.raycast(d.mesh, glm::vec3(0, 0, 5), glm::vec3(0, 0, -1), hit));
}

TEST(mask_with_sculpt_layers) {
    Document d = makeDoc(makeIcosphere(3));
    CommandRegistry reg;
    CHECK(reg.execute(d, "layer_add", {{"name", std::string("Masked")}}).ok);
    d.mesh.ensureMask();
    for (uint32_t v = 0; v < d.mesh.vertexCount(); ++v) d.mesh.mask[v] = d.mesh.positions[v].x > 0.0f ? 1.0f : 0.0f;
    DabParams p;
    p.center = glm::vec3(0, 0, 1);
    p.radius = 0.6f;
    p.strength = 1.0f;
    d.sculptor.beginStroke(d.mesh, "Layer", d.activeLayer);
    d.sculptor.dab(d.mesh, d.bvh, BrushType::Standard, p, Symmetry{false, false, false});
    d.undo.push(d.sculptor.endStroke());
    bool ok = true;
    for (uint32_t v = 0; v < d.mesh.vertexCount(); ++v)
        if (d.mesh.mask[v] == 1.0f) ok &= d.mesh.layers[0].delta[v] == glm::vec3(0.0f);
    CHECK(ok);
    CHECK(reg.execute(d, "subdivide").ok); // layers and mask both subdivide
    CHECK(d.mesh.layers.size() == 1 && d.mesh.layers[0].delta.size() == d.mesh.vertexCount() &&
          d.mesh.mask.size() == d.mesh.vertexCount());
}

TEST(vertex_colors_in_obj_and_gltf) {
    Mesh m = makeIcosphere(2);
    m.ensureColors();
    for (uint32_t v = 0; v < m.vertexCount(); ++v) m.colors[v] = glm::vec3(float(v % 5) / 4.0f, 0.5f, 0.25f);
    std::string err;
    const std::string obj = tempPath("sculptforge_colors.obj");
    CHECK(saveObj(obj, m, err));
    Mesh o;
    CHECK(loadObj(obj, o, err));
    CHECK(o.colors.size() == o.vertexCount() && o.vertexCount() == m.vertexCount());
    float worst = 0.0f;
    for (uint32_t v = 0; v < o.vertexCount(); ++v) worst = std::max(worst, glm::length(o.colors[v] - m.colors[v]));
    CHECK(worst < 2e-3f);
    std::filesystem::remove(obj);

    const std::string glb = tempPath("sculptforge_colors.glb");
    CHECK(saveGlb(glb, m, "Painted", err));
    Mesh g;
    CHECK(loadGltf(glb, g, err));
    CHECK(g.colors.size() == g.vertexCount() && g.vertexCount() == m.vertexCount());
    worst = 0.0f;
    for (uint32_t v = 0; v < g.vertexCount(); ++v) worst = std::max(worst, glm::length(g.colors[v] - m.colors[v]));
    CHECK(worst < 1e-6f);
    std::filesystem::remove(glb);

    Mesh plain;
    CHECK(saveObj(obj, makeIcosphere(1), err) && loadObj(obj, plain, err) && !plain.hasColors());
    std::filesystem::remove(obj);
    CHECK(glm::length(linearToSrgb(srgbToLinear(glm::vec3(0.2f, 0.5f, 0.9f))) - glm::vec3(0.2f, 0.5f, 0.9f)) < 1e-5f);
}
