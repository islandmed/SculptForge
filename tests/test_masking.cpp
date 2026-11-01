// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Masking: mask strokes (symmetry, erase, undo), the mask_* commands, the
// rectangle and lasso mask selections and brushes honouring a painted mask.
#include "TestHarness.h"

#include "core/Commands.h"
#include "core/Masking.h"
#include "core/Primitives.h"
#include "core/ScreenSelect.h"
#include "core/Sculpt.h"

#include <algorithm>
#include <cmath>

using namespace sf;

namespace {

const CommandRegistry& registry() {
    static CommandRegistry r;
    return r;
}

size_t countMasked(const Mesh& m, float above = 0.5f) {
    if (!m.hasMask()) return 0;
    return size_t(std::count_if(m.mask.begin(), m.mask.end(), [&](float x) { return x > above; }));
}

// Mask = 1 on vertices with x > `edge`, 0 elsewhere.
void maskHalf(Mesh& m, float edge = 0.0f) {
    m.ensureMask();
    for (uint32_t v = 0; v < m.vertexCount(); ++v) m.mask[v] = m.positions[v].x > edge ? 1.0f : 0.0f;
}

void maskStroke(Document& d, const glm::vec3& center, float radius, bool erase, Symmetry sym, float strength = 1.0f) {
    DabParams p;
    p.center = center;
    p.viewDir = -glm::normalize(center);
    p.radius = radius;
    p.strength = strength;
    p.invert = erase;
    CHECK(d.sculptor.beginStroke(d.mesh, erase ? "Unmask" : "Mask"));
    for (int i = 0; i < 3; ++i) d.sculptor.dabMask(d.mesh, d.bvh, p, sym);
    UndoRecord r = d.sculptor.endStroke();
    if (!r.empty()) d.undo.push(std::move(r));
}

} // namespace

TEST(mask_stroke_paints_with_symmetry_and_undoes) {
    Document d = makeDoc(makeIcosphere(4));
    const std::vector<glm::vec3> before = d.mesh.positions;
    maskStroke(d, glm::vec3(0.7f, 0.0f, 0.714f), 0.3f, false, Symmetry{true, false, false});
    CHECK(d.mesh.hasMask());
    CHECK(d.mesh.positions == before); // masking never moves vertices
    bool right = false, left = false, farAway = true;
    for (uint32_t v = 0; v < d.mesh.vertexCount(); ++v) {
        const glm::vec3& x = d.mesh.positions[v];
        if (glm::distance(x, glm::vec3(0.7f, 0.0f, 0.714f)) < 0.05f) right |= d.mesh.mask[v] > 0.9f;
        if (glm::distance(x, glm::vec3(-0.7f, 0.0f, 0.714f)) < 0.05f) left |= d.mesh.mask[v] > 0.9f;
        if (x.z < -0.2f) farAway &= d.mesh.mask[v] == 0.0f;
    }
    CHECK(right && left && farAway);
    const std::vector<float> painted = d.mesh.mask;

    CHECK(d.undo.undo(d.mesh, d.bvh));
    CHECK(countMasked(d.mesh, 0.0f) == 0);
    CHECK(d.undo.redo(d.mesh, d.bvh));
    CHECK(d.mesh.mask == painted);

    // Erasing (Ctrl+Alt) on one side only, without symmetry.
    maskStroke(d, glm::vec3(0.7f, 0.0f, 0.714f), 0.3f, true, Symmetry{false, false, false});
    bool rightErased = true, leftKept = false;
    for (uint32_t v = 0; v < d.mesh.vertexCount(); ++v) {
        const glm::vec3& x = d.mesh.positions[v];
        if (glm::distance(x, glm::vec3(0.7f, 0.0f, 0.714f)) < 0.05f) rightErased &= d.mesh.mask[v] < 0.1f;
        if (glm::distance(x, glm::vec3(-0.7f, 0.0f, 0.714f)) < 0.05f) leftKept |= d.mesh.mask[v] > 0.9f;
    }
    CHECK(rightErased && leftKept);
    CHECK(d.undo.undo(d.mesh, d.bvh));
    CHECK(d.mesh.mask == painted);
}

TEST(mask_stroke_soft_strength_and_hidden) {
    Document d = makeDoc(makeIcosphere(4));
    maskStroke(d, glm::vec3(0, 0, 1), 0.4f, false, Symmetry{false, false, false}, 0.2f);
    const float peak = *std::max_element(d.mesh.mask.begin(), d.mesh.mask.end());
    CHECK(peak > 0.2f && peak < 0.9f); // low strength builds up gradually

    // Hidden triangles' vertices are never masked.
    Document h = makeDoc(makeIcosphere(4));
    h.mesh.ensureHidden();
    for (uint32_t t = 0; t < h.mesh.triangleCount(); ++t) {
        const auto& tri = h.mesh.triangles[t];
        h.mesh.hidden[t] = uint8_t((h.mesh.positions[tri.x].x + h.mesh.positions[tri.y].x + h.mesh.positions[tri.z].x) > 0.0f);
    }
    h.bvh.build(h.mesh);
    maskStroke(h, glm::vec3(0, 0, 1), 0.5f, false, Symmetry{false, false, false});
    bool hiddenUntouched = true, visiblePainted = false;
    for (uint32_t v = 0; v < h.mesh.vertexCount(); ++v) {
        if (h.mesh.vertexHidden(v)) hiddenUntouched &= h.mesh.maskAt(v) == 0.0f;
        else visiblePainted |= h.mesh.maskAt(v) > 0.5f;
    }
    CHECK(hiddenUntouched && visiblePainted);
}

TEST(painted_mask_protects_from_brushes) {
    Document d = makeDoc(makeIcosphere(4));
    maskStroke(d, glm::vec3(0, 0, 1), 0.5f, false, Symmetry{false, false, false});
    CHECK(registry().execute(d, "mask_sharpen", {{"iterations", 10.0}}).ok); // soft edge -> exactly 0 or 1
    const std::vector<glm::vec3> before = d.mesh.positions;
    DabParams p;
    p.center = glm::vec3(0, 0, 1);
    p.radius = 0.9f;
    p.strength = 1.0f;
    for (BrushType type : {BrushType::Standard, BrushType::Clay, BrushType::Inflate, BrushType::Smooth}) {
        d.sculptor.beginStroke(d.mesh, "Sculpt");
        for (int i = 0; i < 3; ++i) d.sculptor.dab(d.mesh, d.bvh, type, p, Symmetry{false, false, false});
        d.undo.push(d.sculptor.endStroke());
    }
    bool maskedStill = true, freeMoved = false;
    for (uint32_t v = 0; v < d.mesh.vertexCount(); ++v) {
        if (d.mesh.mask[v] >= 1.0f) maskedStill &= d.mesh.positions[v] == before[v];
        else if (d.mesh.mask[v] == 0.0f) freeMoved |= d.mesh.positions[v] != before[v];
    }
    CHECK(countMasked(d.mesh, 0.999f) > 0);
    CHECK(maskedStill && freeMoved);
}

TEST(mask_commands_registered) {
    const std::string schema = registry().toolSchemaJson();
    for (const char* name : {"mask_clear", "mask_all", "mask_invert", "mask_blur", "mask_sharpen", "mask_grow",
                             "mask_shrink", "mask_by_cavity", "mask_by_polygroup", "mask_border", "mask_rect", "mask_lasso"}) {
        CHECK(registry().find(name) != nullptr);
        CHECK(schema.find(std::string("\"") + name + "\"") != std::string::npos);
    }
}

TEST(mask_all_invert_clear_undo) {
    Document d = makeDoc(makeIcosphere(3));
    const size_t n = d.mesh.vertexCount();
    CHECK(registry().execute(d, "mask_all").ok);
    CHECK(countMasked(d.mesh) == n);
    CHECK(d.modified);
    CHECK(registry().execute(d, "mask_invert").ok);
    CHECK(countMasked(d.mesh, 0.0f) == 0);
    CHECK(registry().execute(d, "mask_invert").ok);
    CHECK(countMasked(d.mesh) == n);
    CHECK(registry().execute(d, "mask_clear").ok);
    CHECK(!d.mesh.hasMask()); // clearing frees the attribute
    CHECK(registry().execute(d, "undo").ok);
    CHECK(countMasked(d.mesh) == n);
    CHECK(registry().execute(d, "undo").ok);
    CHECK(countMasked(d.mesh, 0.0f) == 0);
    CHECK(registry().execute(d, "redo").ok);
    CHECK(countMasked(d.mesh) == n);

    // A no-op adds no undo step.
    Document e = makeDoc(makeIcosphere(2));
    CHECK(registry().execute(e, "mask_clear").ok);
    CHECK(!e.undo.canUndo());

    // Invert of a plain mesh masks everything (Ctrl+click on the background).
    CHECK(registry().execute(e, "mask_invert").ok);
    CHECK(countMasked(e.mesh) == e.mesh.vertexCount());

    // Hidden vertices keep their mask through invert / all.
    Document h = makeDoc(makeIcosphere(3));
    h.mesh.ensureHidden();
    for (uint32_t t = 0; t < h.mesh.triangleCount(); ++t) h.mesh.hidden[t] = uint8_t(h.mesh.positions[h.mesh.triangles[t].x].y > 0.5f);
    CHECK(registry().execute(h, "mask_invert").ok);
    bool ok = true;
    for (uint32_t v = 0; v < h.mesh.vertexCount(); ++v) ok &= h.mesh.mask[v] == (h.mesh.vertexHidden(v) ? 0.0f : 1.0f);
    CHECK(ok);
}

TEST(mask_blur_sharpen_grow_shrink) {
    Document d = makeDoc(makeIcosphere(4));
    maskHalf(d.mesh);
    const size_t masked = countMasked(d.mesh);
    const std::vector<float> original = d.mesh.mask;

    // Blur creates intermediate values along the edge but keeps the overall balance.
    CHECK(registry().execute(d, "mask_blur", {{"iterations", 3.0}}).ok);
    const size_t soft = size_t(std::count_if(d.mesh.mask.begin(), d.mesh.mask.end(), [](float m) { return m > 0.05f && m < 0.95f; }));
    CHECK(soft > 0);
    CHECK(std::all_of(d.mesh.mask.begin(), d.mesh.mask.end(), [](float m) { return m >= 0.0f && m <= 1.0f; }));
    // Sharpen pushes them back towards 0/1.
    CHECK(registry().execute(d, "mask_sharpen", {{"iterations", 6.0}}).ok);
    const size_t softAfter = size_t(std::count_if(d.mesh.mask.begin(), d.mesh.mask.end(), [](float m) { return m > 0.05f && m < 0.95f; }));
    CHECK(softAfter < soft);
    CHECK(registry().execute(d, "undo").ok);
    CHECK(registry().execute(d, "undo").ok);
    CHECK(d.mesh.mask == original);

    CHECK(registry().execute(d, "mask_grow", {{"iterations", 2.0}}).ok);
    const size_t grown = countMasked(d.mesh);
    CHECK(grown > masked);
    CHECK(registry().execute(d, "mask_shrink", {{"iterations", 4.0}}).ok);
    CHECK(countMasked(d.mesh) < masked);
    CHECK(registry().execute(d, "undo").ok);
    CHECK(countMasked(d.mesh) == grown);

    // Shrink of a single masked vertex removes it; grow of an empty mask does nothing.
    Document e = makeDoc(makeIcosphere(2));
    e.mesh.ensureMask();
    e.mesh.mask[0] = 1.0f;
    CHECK(maskShrink(e.mesh, 1));
    CHECK(countMasked(e.mesh, 0.0f) == 0);
    CHECK(!maskGrow(e.mesh, 3));
}

TEST(mask_by_cavity_finds_creases) {
    // A sphere with a band pushed inwards: the rim of the band is concave.
    Mesh m = makeIcosphere(5);
    for (glm::vec3& p : m.positions)
        if (std::abs(p.y) < 0.3f) p *= 0.85f;
    m.recomputeAllNormals();
    Document d = makeDoc(std::move(m));
    CHECK(registry().execute(d, "mask_by_cavity", {{"strength", 1.0}, {"threshold", 0.02}}).ok);
    bool convexFree = true, rimMasked = false;
    for (uint32_t v = 0; v < d.mesh.vertexCount(); ++v) {
        const float y = std::abs(d.mesh.positions[v].y);
        const float r = glm::length(d.mesh.positions[v]);
        if (y > 0.6f) convexFree &= d.mesh.mask[v] == 0.0f; // smooth convex caps
        if (r < 0.9f && y > 0.12f) rimMasked |= d.mesh.mask[v] > 0.5f; // bottom of the step at the band's edge
    }
    CHECK(convexFree && rimMasked);

    // Strength scales the result; a huge threshold masks nothing.
    CHECK(registry().execute(d, "mask_by_cavity", {{"strength", 0.4}}).ok);
    CHECK(*std::max_element(d.mesh.mask.begin(), d.mesh.mask.end()) <= 0.4f + 1e-6f);
    CHECK(registry().execute(d, "mask_by_cavity", {{"threshold", 1.0}}).ok);
    CHECK(countMasked(d.mesh, 0.0f) == 0);
    CHECK(registry().execute(d, "undo").ok);
    CHECK(countMasked(d.mesh, 0.0f) > 0);
}

TEST(mask_by_polygroup) {
    Document d = makeDoc(makeIcosphere(3));
    Mesh& m = d.mesh;
    m.ensurePolygroups();
    for (uint32_t t = 0; t < m.triangleCount(); ++t) {
        const auto& tri = m.triangles[t];
        m.polygroups[t] = (m.positions[tri.x].z + m.positions[tri.y].z + m.positions[tri.z].z) > 0.0f ? 2u : 0u;
    }
    CHECK(registry().execute(d, "mask_by_polygroup", {{"group", 2.0}}).ok);
    bool ok = true;
    for (uint32_t v = 0; v < m.vertexCount(); ++v) {
        if (m.positions[v].z > 0.2f) ok &= m.mask[v] == 1.0f;
        if (m.positions[v].z < -0.2f) ok &= m.mask[v] == 0.0f;
    }
    CHECK(ok);
    // Replace vs add.
    CHECK(registry().execute(d, "mask_by_polygroup", {{"group", 0.0}}).ok);
    bool replaced = true;
    for (uint32_t v = 0; v < m.vertexCount(); ++v)
        if (m.positions[v].z > 0.2f) replaced &= m.mask[v] == 0.0f;
    CHECK(replaced);
    CHECK(registry().execute(d, "mask_by_polygroup", {{"group", 2.0}, {"add", true}}).ok);
    CHECK(countMasked(m) == m.vertexCount());
    CHECK(!registry().execute(d, "mask_by_polygroup", {}).ok); // group is required
    CHECK(registry().execute(d, "undo").ok);
    CHECK(registry().execute(d, "undo").ok);
    CHECK(registry().execute(d, "undo").ok);
    CHECK(countMasked(m, 0.0f) == 0);
}

TEST(screen_polygon_inside_test) {
    // A concave "C" opening to the right, and a long many-sided circle (band lookups).
    const ScreenPolygon c({{-1, -1}, {1, -1}, {1, -0.5f}, {-0.5f, -0.5f}, {-0.5f, 0.5f}, {1, 0.5f}, {1, 1}, {-1, 1}});
    CHECK(c.valid());
    CHECK(c.contains({-0.75f, 0.0f}) && c.contains({0.5f, -0.75f}) && c.contains({0.5f, 0.75f}));
    CHECK(!c.contains({0.25f, 0.0f}) && !c.contains({1.5f, 0.0f}) && !c.contains({0.0f, 1.5f}));
    std::vector<glm::vec2> ring;
    for (int i = 0; i < 400; ++i) {
        const float a = float(i) / 400.0f * 6.2831853f;
        ring.push_back({std::cos(a), std::sin(a)});
    }
    const ScreenPolygon circle(ring);
    int wrong = 0;
    for (int y = -12; y <= 12; ++y)
        for (int x = -12; x <= 12; ++x) {
            const glm::vec2 p(float(x) / 10.0f, float(y) / 10.0f);
            const float r = glm::length(p);
            if (std::abs(r - 1.0f) > 0.02f && circle.contains(p) != (r < 1.0f)) ++wrong;
        }
    CHECK(wrong == 0);
    // Too few points or no area: nothing is inside.
    CHECK(!ScreenPolygon({{0, 0}, {1, 1}}).valid());
    CHECK(!ScreenPolygon({{0, 0}, {1, 0}, {2, 0}}).valid());
}

TEST(mask_rect_and_lasso) {
    Document d = makeDoc(makeIcosphere(3));
    const Mesh& m = d.mesh;
    // Default view: the mesh seen from the front, NDC spanning its bounds. Mask x < 0 (both sides of the sphere).
    CHECK(registry().execute(d, "mask_rect", {{"x0", -1.0}, {"y0", -1.0}, {"x1", 0.0}, {"y1", 1.0}}).ok);
    int wrong = 0;
    for (uint32_t v = 0; v < m.vertexCount(); ++v)
        if (std::abs(m.positions[v].x) > 1e-3f && (m.mask[v] == 1.0f) != (m.positions[v].x < 0.0f)) ++wrong;
    CHECK(wrong == 0 && countMasked(m) > 0);
    CHECK(d.undo.canUndo() && d.modified);

    // Alt (unmask) with a lasso triangle over the bottom-left quarter: there, x < 0 and y < 0, is free again.
    CHECK(registry().execute(d, "mask_lasso", {{"points", std::string("-1.5 0  0 0  0 -1.5  -1.5 -1.5")}, {"unmask", true}}).ok);
    wrong = 0;
    for (uint32_t v = 0; v < m.vertexCount(); ++v) {
        const glm::vec3 p = m.positions[v];
        if (std::abs(p.x) < 1e-3f || std::abs(p.y) < 1e-3f) continue;
        if ((m.mask[v] == 1.0f) != (p.x < 0.0f && p.y > 0.0f)) ++wrong;
    }
    CHECK(wrong == 0);

    // Undo steps back one selection at a time.
    CHECK(registry().execute(d, "undo").ok);
    wrong = 0;
    for (uint32_t v = 0; v < m.vertexCount(); ++v)
        if (std::abs(m.positions[v].x) > 1e-3f && (m.mask[v] == 1.0f) != (m.positions[v].x < 0.0f)) ++wrong;
    CHECK(wrong == 0);
    CHECK(registry().execute(d, "undo").ok);
    CHECK(countMasked(m, 0.0f) == 0);

    // Partial strength, an explicit identity view (NDC = world), and hidden vertices keep their mask.
    Document h = makeDoc(makeIcosphere(3));
    h.mesh.ensureHidden();
    for (uint32_t t = 0; t < h.mesh.triangleCount(); ++t) h.mesh.hidden[t] = uint8_t(h.mesh.positions[h.mesh.triangles[t].x].y > 0.5f);
    const std::string identity = "1 0 0 0 0 1 0 0 0 0 1 0 0 0 0 1";
    CHECK(registry().execute(h, "mask_rect", {{"x0", -2.0}, {"y0", -2.0}, {"x1", 2.0}, {"y1", 2.0}, {"strength", 0.5},
                                              {"view_proj", identity}}).ok);
    bool ok = true;
    for (uint32_t v = 0; v < h.mesh.vertexCount(); ++v) ok &= h.mesh.mask[v] == (h.mesh.vertexHidden(v) ? 0.0f : 0.5f);
    CHECK(ok);

    // A selection that misses changes nothing and adds no undo step; bad arguments are errors.
    Document e = makeDoc(makeIcosphere(2));
    CHECK(registry().execute(e, "mask_rect", {{"x0", 5.0}, {"y0", 5.0}, {"x1", 6.0}, {"y1", 6.0}}).ok);
    CHECK(!e.undo.canUndo());
    CHECK(!registry().execute(e, "mask_lasso", {{"points", std::string("0 0 1 1")}}).ok);
    CHECK(!registry().execute(e, "mask_lasso", {{"points", std::string("0 0 1 1 0 x")}}).ok);
    CHECK(!registry().execute(e, "mask_lasso", {}).ok); // points is required
    CHECK(!registry().execute(e, "mask_rect", {{"view_proj", std::string("1 2 3")}}).ok);
}

TEST(mask_rect_and_lasso_symmetry_and_clear_outside) {
    // #51: with symmetry the mirror image of the shape is masked too, as a mask stroke mirrors. A rectangle over
    // x in -1..-0.4 (the front view spans the bounds) masks x < -0.4*r and, mirrored in X, x > 0.4*r.
    Document d = makeDoc(makeIcosphere(3));
    const Mesh& m = d.mesh;
    const Args rect = {{"x0", -1.0}, {"y0", -1.0}, {"x1", -0.4}, {"y1", 1.0}, {"symmetry", std::string("x")}};
    CHECK(registry().execute(d, "mask_rect", rect).ok);
    int wrong = 0, left = 0, right = 0;
    for (uint32_t v = 0; v < m.vertexCount(); ++v) {
        const float x = m.positions[v].x;
        if (std::abs(std::abs(x) - 0.4f) < 0.05f) continue; // (the rectangle's edge)
        if ((m.mask[v] == 1.0f) != (std::abs(x) > 0.4f)) ++wrong;
        left += m.mask[v] == 1.0f && x < 0.0f;
        right += m.mask[v] == 1.0f && x > 0.0f;
    }
    CHECK(wrong == 0);
    CHECK(left > 0 && left == right);
    // Y and Z mirror through the same centre; a lasso takes it too. Bad axes are an error.
    Document y = makeDoc(makeIcosphere(3));
    CHECK(registry().execute(y, "mask_lasso", {{"points", std::string("-1.5 0.5  1.5 0.5  1.5 1.5  -1.5 1.5")},
                                               {"symmetry", std::string("Y")}}).ok);
    wrong = 0;
    for (uint32_t v = 0; v < y.mesh.vertexCount(); ++v) {
        const float py = y.mesh.positions[v].y;
        if (std::abs(std::abs(py) - 0.5f) < 0.05f) continue;
        if ((y.mesh.mask[v] == 1.0f) != (std::abs(py) > 0.5f)) ++wrong;
    }
    CHECK(wrong == 0 && countMasked(y.mesh) > 0);
    CHECK(!registry().execute(y, "mask_rect", {{"symmetry", std::string("xw")}}).ok);

    // clear_outside (#48, ZBrush's Ctrl+drag on the background): a shape that covers none of the model clears the
    // mask, as one undo step; one that covers some masks as usual; without it a miss changes nothing.
    const size_t before = countMasked(m);
    CHECK(registry().execute(d, "mask_rect", {{"x0", 5.0}, {"y0", 5.0}, {"x1", 6.0}, {"y1", 6.0}}).ok);
    CHECK(countMasked(m) == before);
    CHECK(registry().execute(d, "mask_rect", {{"x0", 5.0}, {"y0", 5.0}, {"x1", 6.0}, {"y1", 6.0}, {"clear_outside", true}}).ok);
    CHECK(countMasked(m, 0.0f) == 0);
    CHECK(registry().execute(d, "undo").ok);
    CHECK(countMasked(m) == before);
    CHECK(registry().execute(d, "mask_rect", {{"x0", -0.1}, {"y0", -0.1}, {"x1", 0.1}, {"y1", 0.1}, {"clear_outside", true}}).ok);
    CHECK(countMasked(m) > before);
    // Nothing masked and nothing covered: no undo step.
    Document e = makeDoc(makeIcosphere(2));
    CHECK(registry().execute(e, "mask_rect", {{"x0", 5.0}, {"y0", 5.0}, {"x1", 6.0}, {"y1", 6.0}, {"clear_outside", true}}).ok);
    CHECK(!e.undo.canUndo());
    // Hidden parts don't count as covered.
    Document h = makeDoc(makeIcosphere(3));
    maskHalf(h.mesh, 0.5f);
    h.mesh.ensureHidden();
    for (uint32_t t = 0; t < h.mesh.triangleCount(); ++t) {
        const glm::uvec3 tri = h.mesh.triangles[t];
        h.mesh.hidden[t] = uint8_t(h.mesh.positions[tri.x].x < 0.0f || h.mesh.positions[tri.y].x < 0.0f ||
                                   h.mesh.positions[tri.z].x < 0.0f);
    }
    CHECK(registry().execute(h, "mask_rect", {{"x0", -1.0}, {"y0", -1.0}, {"x1", -0.6}, {"y1", 1.0}, {"clear_outside", true}}).ok);
    CHECK(countMasked(h.mesh, 0.0f) == 0);
}

// #61: Mask from the mesh border.
TEST(mask_border_masks_plane_rim_and_rings_widen_it) {
    Document d = makeDoc(makePlane(8)); // 9 x 9 vertices: a rim of 32, then rings of 24, 16, 8 and the centre
    auto onRim = [](const glm::vec3& p) { return std::max(std::abs(p.x), std::abs(p.z)) > 0.99f; };
    CommandResult r = registry().execute(d, "mask_border");
    CHECK(r.ok);
    CHECK(r.message.find("32 border vertices") != std::string::npos);
    CHECK(countMasked(d.mesh) == 32);
    bool rimOnly = true;
    for (uint32_t v = 0; v < d.mesh.vertexCount(); ++v) rimOnly &= (d.mesh.mask[v] == 1.0f) == onRim(d.mesh.positions[v]);
    CHECK(rimOnly);
    CHECK(d.undo.canUndo());

    // Two rings: the rim and the ring inside it (replacing the mask), then three soft rings.
    CHECK(registry().execute(d, "mask_border", {{"rings", 2.0}}).ok);
    CHECK(countMasked(d.mesh) == 56);
    CHECK(registry().execute(d, "mask_border", {{"rings", 3.0}, {"soft", true}}).ok);
    CHECK(countMasked(d.mesh, 0.0f) == 56 + 16);
    float rim = 0.0f, inner = 1.0f;
    for (uint32_t v = 0; v < d.mesh.vertexCount(); ++v) {
        if (onRim(d.mesh.positions[v])) rim = std::max(rim, d.mesh.mask[v]);
        else if (d.mesh.mask[v] > 0.0f) inner = std::min(inner, d.mesh.mask[v]);
    }
    CHECK(rim == 1.0f && inner < 0.5f); // full on the border, fading inwards

    // Each is one undo step, back to no mask.
    CHECK(registry().execute(d, "undo").ok);
    CHECK(countMasked(d.mesh, 0.0f) == 56 && countMasked(d.mesh, 0.999f) == 56);
    CHECK(registry().execute(d, "undo").ok);
    CHECK(countMasked(d.mesh) == 32);
    CHECK(registry().execute(d, "undo").ok);
    CHECK(countMasked(d.mesh, 0.0f) == 0);

    // Add keeps the current mask; strength sets the value.
    maskHalf(d.mesh, 0.5f);
    const size_t half = countMasked(d.mesh);
    CHECK(registry().execute(d, "mask_border", {{"add", true}, {"strength", 0.25}}).ok);
    CHECK(countMasked(d.mesh) == half);
    CHECK(countMasked(d.mesh, 0.0f) > half);
    // Ring counts are clamped (to 20), which fills this small plane.
    CHECK(registry().execute(d, "mask_border", {{"rings", 1000.0}}).ok);
    CHECK(countMasked(d.mesh) == d.mesh.vertexCount());
}

TEST(mask_border_closed_mesh_masks_nothing) {
    Document d = makeDoc(makeIcosphere(3));
    CommandResult r = registry().execute(d, "mask_border", {{"rings", 3.0}});
    CHECK(r.ok);
    CHECK(r.message.find("No open edges: the mesh is closed") != std::string::npos);
    CHECK(countMasked(d.mesh, 0.0f) == 0);
    CHECK(!d.undo.canUndo());
    // Nor are there polygroup borders on a mesh of one polygroup.
    r = registry().execute(d, "mask_border", {{"polygroups", true}});
    CHECK(r.message.find("No open edges") != std::string::npos);
    CHECK(!d.undo.canUndo());
}

TEST(mask_border_polygroup_borders) {
    Document d = makeDoc(makeIcosphere(3));
    Mesh& m = d.mesh;
    m.polygroups.assign(m.triangleCount(), 0);
    for (uint32_t t = 0; t < m.triangleCount(); ++t) {
        const glm::uvec3 tri = m.triangles[t];
        m.polygroups[t] = m.positions[tri.x].y + m.positions[tri.y].y + m.positions[tri.z].y > 0.0f ? 1u : 0u;
    }
    CHECK(registry().execute(d, "mask_border", {{"polygroups", true}}).ok);
    const size_t seam = countMasked(m);
    CHECK(seam > 0 && seam < m.vertexCount() / 4);
    bool nearEquator = true;
    for (uint32_t v = 0; v < m.vertexCount(); ++v)
        if (m.mask[v] > 0.5f) nearEquator &= std::abs(m.positions[v].y) < 0.25f;
    CHECK(nearEquator);
    CHECK(registry().execute(d, "mask_border", {{"polygroups", true}, {"rings", 3.0}}).ok);
    CHECK(countMasked(m) > seam);
    // Without polygroups the closed sphere still has no border to mask.
    CHECK(registry().execute(d, "mask_border").message.find("No open edges") != std::string::npos);
}
