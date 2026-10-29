// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Polypaint (Paint brush, fills, cavity paint, colour commands, PLY/USD
// export) and the built-in MatCap materials.
#include "TestHarness.h"

#include "core/Commands.h"
#include "core/Matcap.h"
#include "core/Polypaint.h"
#include "core/Primitives.h"
#include "io/PlyIO.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>

using namespace sf;

namespace {

bool near(const glm::vec3& a, const glm::vec3& b, float eps = 1e-3f) { return glm::all(glm::lessThan(glm::abs(a - b), glm::vec3(eps))); }

uint32_t nearestVertex(const Mesh& m, const glm::vec3& p) {
    uint32_t best = 0;
    for (uint32_t v = 1; v < m.vertexCount(); ++v)
        if (glm::distance(m.positions[v], p) < glm::distance(m.positions[best], p)) best = v;
    return best;
}

DabParams paintParams(const glm::vec3& center, float radius) {
    DabParams p;
    p.center = center;
    p.viewDir = -glm::normalize(center);
    p.radius = radius;
    p.strength = 1.0f;
    return p;
}

const glm::vec3 kRed(1.0f, 0.0f, 0.0f);

} // namespace

TEST(polypaint_parse_color) {
    glm::vec3 c;
    CHECK(parseColor("#ff0000", c) && near(c, kRed));
    CHECK(parseColor(" #F00 ", c) && near(c, kRed));
    CHECK(parseColor("00ff00", c) && near(c, glm::vec3(0, 1, 0)));
    CHECK(parseColor("Red", c) && c.x > 0.5f && c.y < 0.05f);
    CHECK(parseColor("255,0,0", c) && near(c, kRed));
    CHECK(parseColor("0.5 0.5 0.5", c) && near(c, srgbToLinear(glm::vec3(0.5f))));
    CHECK(!parseColor("nope", c));
    CHECK(!parseColor("#12345", c));
    CHECK(!parseColor("1,2", c));
    CHECK(!parseColor("1,2,3,4", c));
    CHECK(!parseColor("", c));
    CHECK(colorToHex(srgbToLinear(glm::vec3(0.5f, 1.0f, 0.0f))) == "#80ff00");
    CHECK(parseColor(colorToHex(srgbToLinear(glm::vec3(0.2f, 0.4f, 0.6f))), c) &&
          near(linearToSrgb(c), glm::vec3(0.2f, 0.4f, 0.6f), 3e-3f));
}

TEST(polypaint_brush_falloff_symmetry_mask_undo) {
    Document doc = makeDoc(makeIcosphere(4));
    Mesh& m = doc.mesh;
    const std::vector<glm::vec3> before = m.positions;
    // Mask the upper half.
    m.ensureMask();
    for (uint32_t v = 0; v < m.vertexCount(); ++v) m.mask[v] = m.positions[v].y > 0.05f ? 1.0f : 0.0f;

    const uint32_t centre = nearestVertex(m, {1, 0, 0}), mirror = nearestVertex(m, {-1, 0, 0});
    const uint32_t masked = nearestVertex(m, {0.97f, 0.24f, 0}), far = nearestVertex(m, {0, 0, 1});
    CHECK(!m.hasColors());
    CHECK(doc.sculptor.beginStroke(m, "Paint"));
    const size_t n = paintDab(doc.sculptor, m, doc.bvh, paintParams(m.positions[centre], 0.5f), kRed, Symmetry{});
    doc.undo.push(doc.sculptor.endStroke());
    CHECK(n > 0 && m.hasColors() && m.colors.size() == m.vertexCount());
    CHECK(near(m.colors[centre], kRed, 0.02f));
    CHECK(near(m.colors[mirror], kRed, 0.05f)); // X symmetry is on by default
    CHECK(m.colors[masked] == glm::vec3(1.0f));
    CHECK(m.colors[far] == glm::vec3(1.0f));
    // Falloff: a vertex half-way out is painted less than the centre.
    const uint32_t mid = nearestVertex(m, glm::normalize(glm::vec3(1, -0.1f, 0.25f)));
    CHECK(m.colors[mid].y > m.colors[centre].y && m.colors[mid].y < 1.0f);
    CHECK(m.positions == before); // paint never sculpts

    CHECK(doc.undo.undo(m, doc.bvh));
    bool allWhite = true;
    for (const auto& c : m.colors) allWhite = allWhite && c == glm::vec3(1.0f);
    CHECK(allWhite);
    CHECK(doc.undo.redo(m, doc.bvh));
    CHECK(near(m.colors[centre], kRed, 0.02f));

    // Without an active stroke nothing happens.
    CHECK(paintDab(doc.sculptor, m, doc.bvh, paintParams(m.positions[far], 0.5f), kRed, Symmetry{}) == 0);
}

TEST(polypaint_invert_hidden_front_faces) {
    Document doc = makeDoc(makeIcosphere(3));
    Mesh& m = doc.mesh;
    m.ensureColors(kRed);
    const uint32_t v = nearestVertex(m, {0, 0, 1});
    // Hide the triangles around one vertex near the dab, off-centre.
    const uint32_t hiddenV = nearestVertex(m, glm::normalize(glm::vec3(0.15f, 0, 1)));
    m.ensureHidden();
    for (uint32_t t : m.vertexTriangles(hiddenV)) m.hidden[t] = 1;
    doc.sculptor.beginStroke(m, "Paint");
    DabParams p = paintParams(m.positions[v], 0.4f);
    p.invert = true; // Alt: back to white
    paintDab(doc.sculptor, m, doc.bvh, p, glm::vec3(0, 0, 1), Symmetry{false, false, false});
    // Front faces only: a dab "seen" from behind the sphere paints nothing.
    DabParams back = paintParams(m.positions[v], 0.4f);
    back.viewDir = glm::vec3(0, 0, 1);
    back.frontFacesOnly = true;
    const size_t backCount = paintDab(doc.sculptor, m, doc.bvh, back, glm::vec3(0, 1, 0), Symmetry{false, false, false});
    doc.undo.push(doc.sculptor.endStroke());
    CHECK(m.colors[v].y > 0.9f && m.colors[v].z > 0.9f); // painted towards white, not blue
    CHECK(m.colors[hiddenV] == kRed);
    CHECK(backCount == 0);
}

TEST(polypaint_with_sculpt_is_one_undo_step) {
    Document doc = makeDoc(makeIcosphere(3));
    Mesh& m = doc.mesh;
    const std::vector<glm::vec3> before = m.positions;
    const uint32_t v = nearestVertex(m, {0, 1, 0});
    DabParams p = paintParams(m.positions[v], 0.4f);
    doc.sculptor.beginStroke(m, "Standard");
    doc.sculptor.dab(m, doc.bvh, BrushType::Standard, p, Symmetry{});
    paintDab(doc.sculptor, m, doc.bvh, p, kRed, Symmetry{});
    UndoRecord r = doc.sculptor.endStroke();
    CHECK(!r.positions.empty() && !r.colors.empty());
    doc.undo.push(std::move(r));
    CHECK(m.positions != before && m.colors[v] != glm::vec3(1.0f));
    doc.undo.undo(m, doc.bvh);
    CHECK(m.positions == before && m.colors[v] == glm::vec3(1.0f));
}

TEST(polypaint_fill_and_clear_commands) {
    CommandRegistry reg;
    Document doc = makeDoc(makeIcosphere(3));
    Mesh& m = doc.mesh;
    CHECK(!reg.execute(doc, "paint_fill", {{"area", std::string("masked")}}).ok); // nothing masked
    CHECK(!reg.execute(doc, "paint_fill", {{"color", std::string("not a colour")}}).ok);
    CHECK(!reg.execute(doc, "paint_fill", {{"area", std::string("sideways")}}).ok);
    CHECK(!m.hasColors());

    // Default: the current colour over the unmasked area (= whole mesh without a mask).
    CHECK(reg.execute(doc, "set_color", {{"color", std::string("#0000ff")}}).ok);
    CHECK(near(doc.paintColor, glm::vec3(0, 0, 1)));
    CHECK(reg.execute(doc, "paint_fill").ok);
    CHECK(reg.execute(doc, "undo").ok && !m.hasColors()); // the first fill's undo removes polypaint again
    CHECK(reg.execute(doc, "redo").ok && m.hasColors());
    CHECK(std::all_of(m.colors.begin(), m.colors.end(), [](const glm::vec3& c) { return near(c, glm::vec3(0, 0, 1)); }));

    m.ensureMask();
    for (uint32_t v = 0; v < m.vertexCount(); ++v) m.mask[v] = m.positions[v].x > 0.0f ? 1.0f : 0.0f;
    const uint32_t right = nearestVertex(m, {1, 0, 0}), left = nearestVertex(m, {-1, 0, 0});
    CHECK(reg.execute(doc, "paint_fill", {{"color", std::string("red")}, {"area", std::string("masked")}}).ok);
    CHECK(m.colors[right].x > 0.5f && m.colors[right].z < 0.05f && near(m.colors[left], glm::vec3(0, 0, 1)));
    CHECK(reg.execute(doc, "paint_fill", {{"r", 1.0}, {"g", 1.0}, {"b", 1.0}, {"strength", 0.5}}).ok); // unmasked, half
    CHECK(near(m.colors[left], glm::vec3(0.5f, 0.5f, 1.0f)) && m.colors[right].z < 0.05f);
    CHECK(reg.execute(doc, "paint_fill", {{"color", std::string("#00ff00")}, {"area", std::string("all")}}).ok);
    CHECK(near(m.colors[right], glm::vec3(0, 1, 0)) && near(m.colors[left], glm::vec3(0, 1, 0)));

    // Each fill is one undo step.
    CHECK(reg.execute(doc, "undo").ok);
    CHECK(near(m.colors[left], glm::vec3(0.5f, 0.5f, 1.0f)));
    CHECK(reg.execute(doc, "paint_clear").ok);
    CHECK(!m.hasColors());
    CHECK(reg.execute(doc, "undo").ok);
    CHECK(m.hasColors() && near(m.colors[left], glm::vec3(0.5f, 0.5f, 1.0f)));
    CHECK(reg.execute(doc, "redo").ok && !m.hasColors());
    CHECK(reg.execute(doc, "paint_clear").ok); // no-op when there is nothing to clear

    CHECK(!reg.execute(doc, "set_color").ok);
    CHECK(!reg.execute(doc, "set_color", {{"color", std::string("#zzz")}}).ok);
    CHECK(reg.execute(doc, "set_color", {{"r", 255.0}, {"g", 0.0}, {"b", 0.0}}).ok && near(doc.paintColor, kRed));
}

TEST(polypaint_fill_skips_hidden) {
    Mesh m = makeIcosphere(2);
    m.ensureHidden();
    const uint32_t v = nearestVertex(m, {0, 1, 0});
    for (uint32_t t : m.vertexTriangles(v)) m.hidden[t] = 1;
    paintFill(m, kRed, PaintArea::All);
    CHECK(m.colors[v] == glm::vec3(1.0f) && m.colors[nearestVertex(m, {0, -1, 0})] == kRed);
}

TEST(polypaint_from_cavity) {
    Mesh m = makeIcosphere(4);
    // Push one vertex in to make a dent; the rest of the sphere is convex.
    const uint32_t dent = nearestVertex(m, {0, 0, 1}), far = nearestVertex(m, {0, 0, -1});
    m.positions[dent] *= 0.97f;
    m.recomputeAllNormals();
    const std::vector<float> cav = cavityMap(m, 0);
    CHECK(cav[dent] > 0.5f && cav[far] == 0.0f);
    CHECK(*std::min_element(cav.begin(), cav.end()) >= 0.0f && *std::max_element(cav.begin(), cav.end()) <= 1.0f);

    CommandRegistry reg;
    Document doc = makeDoc(m);
    CHECK(reg.execute(doc, "paint_from_cavity", {{"smoothing", 0.0}, {"strength", 1.0}}).ok);
    CHECK(doc.mesh.colors[dent].x < 0.5f && doc.mesh.colors[far] == glm::vec3(1.0f));
    CHECK(reg.execute(doc, "undo").ok);
    CHECK(!doc.mesh.hasColors()); // undo of the first colour edit removes polypaint
    // Masked vertices keep their colour.
    doc.mesh.ensureMask();
    doc.mesh.mask[dent] = 1.0f;
    CHECK(reg.execute(doc, "paint_from_cavity", {{"smoothing", 0.0}, {"color", std::string("#ff0000")}}).ok);
    CHECK(doc.mesh.colors[dent] == glm::vec3(1.0f));
}

TEST(polypaint_sample_color) {
    Mesh m = makeIcosphere(1);
    m.ensureColors(glm::vec3(0.0f));
    const glm::uvec3 t = m.triangles[0];
    m.colors[t.x] = glm::vec3(1, 0, 0);
    m.colors[t.y] = glm::vec3(0, 1, 0);
    m.colors[t.z] = glm::vec3(0, 0, 1);
    CHECK(near(sampleColor(m, 0, m.positions[t.y]), glm::vec3(0, 1, 0)));
    const glm::vec3 centre = (m.positions[t.x] + m.positions[t.y] + m.positions[t.z]) / 3.0f;
    CHECK(near(sampleColor(m, 0, centre), glm::vec3(1.0f / 3.0f)));
    CHECK(sampleColor(m, uint32_t(m.triangleCount()), centre) == glm::vec3(1.0f)); // out of range
}

TEST(polypaint_export_ply_and_usd) {
    CommandRegistry reg;
    Document doc = makeDoc(makeIcosphere(3));
    const std::string plain = tempPath("sf_polypaint_plain.ply"), painted = tempPath("sf_polypaint.ply");
    CHECK(reg.execute(doc, "export_ply", {{"path", plain}}).ok);
    for (uint32_t v = 0; v < doc.mesh.vertexCount(); ++v) {
        doc.mesh.ensureColors();
        doc.mesh.colors[v] = srgbToLinear(glm::clamp(doc.mesh.positions[v] * 0.5f + 0.5f, 0.0f, 1.0f));
    }
    CHECK(reg.execute(doc, "export_ply", {{"path", painted}}).ok);

    Mesh a, b;
    SplatCloud splats;
    PlyContent content;
    std::string err;
    CHECK(loadPly(plain, a, splats, content, err) && content == PlyContent::Mesh);
    CHECK(a.triangleCount() == doc.mesh.triangleCount() && !a.hasColors());
    CHECK(loadPly(painted, b, splats, content, err) && content == PlyContent::Mesh);
    CHECK(b.vertexCount() == doc.mesh.vertexCount() && b.hasColors());
    bool same = b.vertexCount() == doc.mesh.vertexCount();
    for (uint32_t v = 0; same && v < b.vertexCount(); ++v) {
        const uint32_t o = nearestVertex(doc.mesh, b.positions[v]);
        same = near(linearToSrgb(b.colors[v]), linearToSrgb(doc.mesh.colors[o]), 0.5f / 255.0f + 1e-4f);
    }
    CHECK(same);

    const std::string usd = tempPath("sf_polypaint.usda");
    CHECK(reg.execute(doc, "export_usd", {{"path", usd}}).ok);
    std::ifstream f(usd);
    std::stringstream text;
    text << f.rdbuf();
    CHECK(text.str().find("color3f[] primvars:displayColor") != std::string::npos);
    CHECK(text.str().find("UsdPrimvarReader_float3") != std::string::npos);
    CHECK(text.str().find("inputs:diffuseColor.connect") != std::string::npos);
    f.close();
    CHECK(!reg.execute(doc, "export_ply", {{"path", tempPath("no_such_dir_sf/x/y.ply")}}).ok);
    std::filesystem::remove(plain);
    std::filesystem::remove(painted);
    std::filesystem::remove(usd);
}

TEST(polypaint_commands_in_schema) {
    CommandRegistry reg;
    const std::string schema = reg.toolSchemaJson();
    for (const char* name : {"set_color", "paint_fill", "paint_clear", "paint_from_cavity", "export_ply"}) {
        CHECK(reg.find(name) != nullptr);
        CHECK(schema.find(std::string("\"") + name + "\"") != std::string::npos);
    }
}

TEST(matcap_builtin_materials) {
    CHECK(matcapCount() >= 4 && matcapCount() <= 8);
    for (int i = 0; i < matcapCount(); ++i) {
        CHECK(std::string(matcapName(i)).size() > 0);
        CHECK(findMatcap(matcapName(i)) == i);
        for (int j = 0; j < i; ++j) CHECK(std::string(matcapName(i)) != matcapName(j));
    }
    CHECK(findMatcap("red wax") >= 0 && findMatcap("RedWax") == findMatcap("Red Wax"));
    CHECK(findMatcap("chrome") >= 0 && findMatcap("unobtainium") == -1);
    CHECK(generateMatcap(-1).empty() && generateMatcap(matcapCount()).empty() && generateMatcap(0, 0).empty());

    const int size = 64;
    auto pixel = [&](const std::vector<uint8_t>& img, int i, int j) {
        const uint8_t* p = img.data() + (size_t(j) * size + size_t(i)) * 4;
        return glm::ivec4(p[0], p[1], p[2], p[3]);
    };
    const std::vector<uint8_t> wax = generateMatcap(findMatcap("Red Wax"), size);
    CHECK(wax.size() == size_t(size) * size * 4);
    const glm::ivec4 c = pixel(wax, size / 2, size / 2);
    CHECK(c.x > c.y + 40 && c.x > c.z + 40 && c.w == 255); // red
    // Lit from the upper left: that side of the sphere is brighter than the lower right (rows are bottom-up).
    const glm::ivec4 lit = pixel(wax, size / 4, size * 3 / 4), dark = pixel(wax, size * 3 / 4, size / 4);
    CHECK(lit.x > dark.x);
    // Corners repeat the rim instead of being undefined.
    CHECK(pixel(wax, 0, 0).w == 255);
    const std::vector<uint8_t> chrome = generateMatcap(findMatcap("Chrome"), size);
    CHECK(chrome != wax);
    const glm::ivec4 cc = pixel(chrome, size / 2, size / 2);
    CHECK(std::abs(cc.x - cc.z) < 40); // neutral metal
    // Deterministic.
    CHECK(generateMatcap(findMatcap("Chrome"), size) == chrome);

    // Sculpt, the readability-first material, comes first: a neutral warm clay with a strong key from the upper
    // left (a clear light-to-shadow range, so slopes read), and the deepest cavity shading of them all.
    CHECK(findMatcap("sculpt") == 0);
    const std::vector<uint8_t> sculpt = generateMatcap(0, size);
    const glm::ivec4 sc = pixel(sculpt, size / 2, size / 2);
    CHECK(sc.x >= sc.z && sc.x - sc.z < 60);
    CHECK(pixel(sculpt, size / 4, size * 3 / 4).x > pixel(sculpt, size * 3 / 4, size / 4).x + 60);
    for (int i = 1; i < matcapCount(); ++i) CHECK(matcapCavity(0).crease > matcapCavity(i).crease);

    // Soft Clay, the default (last, so the others keep their indices): a matte warm light grey, lit
    // from the upper left, with no highlight anywhere near white.
    CHECK(defaultMatcap() == findMatcap("soft clay") && defaultMatcap() == matcapCount() - 1);
    const std::vector<uint8_t> clay = generateMatcap(defaultMatcap(), size);
    const glm::ivec4 cl = pixel(clay, size / 2, size / 2);
    CHECK(cl.x >= cl.z && cl.x - cl.z < 30 && cl.x > 120);
    CHECK(pixel(clay, size / 4, size * 3 / 4).x > pixel(clay, size * 3 / 4, size / 4).x + 40);
    int brightest = 0;
    for (size_t k = 0; k < clay.size(); k += 4) brightest = std::max(brightest, int(clay[k]));
    CHECK(brightest < 225);
    CHECK(matcapCavity(-1).crease > 0.0f && matcapCavity(matcapCount()).crease == matcapCavity(-1).crease);
    // The pickers list the default first, then every other MatCap once, in index order.
    CHECK(matcapInListOrder(0) == defaultMatcap() && matcapInListOrder(1) == 0);
    CHECK(matcapInListOrder(-1) == -1 && matcapInListOrder(matcapCount()) == -1);
    std::vector<int> listed;
    for (int k = 0; k < matcapCount(); ++k) listed.push_back(matcapInListOrder(k));
    std::sort(listed.begin(), listed.end());
    for (int i = 0; i < matcapCount(); ++i) CHECK(listed[size_t(i)] == i);
}

TEST(polypaint_restore_stroke_marks_colors_dirty) {
    // DragRect re-stamps by restoring the stroke first: the reverted colours must reach the GPU.
    Document doc = makeDoc(makeIcosphere(3));
    Mesh& m = doc.mesh;
    const uint32_t centre = nearestVertex(m, {1, 0, 0});
    CHECK(doc.sculptor.beginStroke(m, "Paint"));
    CHECK(paintDab(doc.sculptor, m, doc.bvh, paintParams(m.positions[centre], 0.5f), kRed, Symmetry{}) > 0);
    m.clearDirty(); // uploaded
    doc.sculptor.restoreStroke(m, doc.bvh);
    CHECK(m.colors[centre] == glm::vec3(1.0f));
    CHECK(m.dirtyBegin <= centre && centre < m.dirtyEnd);
    doc.undo.push(doc.sculptor.endStroke());
}
