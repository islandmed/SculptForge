// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Vertex paint (Paint brush, fills, cavity paint, colour commands, PLY/USD
// export) and the built-in Lit Sphere materials.
#include "TestHarness.h"

#include "core/BrushAlpha.h"
#include "core/Commands.h"
#include "core/Image.h"
#include "core/LitSphere.h"
#include "core/VertexPaint.h"
#include "core/Primitives.h"
#include "core/Trash.h"
#include "core/Vdm.h"
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

TEST(vertexpaint_parse_color) {
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

TEST(vertexpaint_brush_falloff_symmetry_mask_undo) {
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

TEST(vertexpaint_invert_hidden_front_faces) {
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

TEST(vertexpaint_with_sculpt_is_one_undo_step) {
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

TEST(vertexpaint_fill_and_clear_commands) {
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
    CHECK(reg.execute(doc, "undo").ok && !m.hasColors()); // the first fill's undo removes vertex paint again
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

// Line Gradient (#99): left to right across the front view, the mask respected, one undo step per gradient.
TEST(vertexpaint_line_gradient) {
    CommandRegistry reg;
    Document doc = makeDoc(makeIcosphere(3));
    Mesh& m = doc.mesh;
    const uint32_t left = nearestVertex(m, {-1, 0, 0}), right = nearestVertex(m, {1, 0, 0}), mid = nearestVertex(m, {0, 0, 1});
    CHECK(!reg.execute(doc, "paint_gradient", {{"x0", 0.2}, {"y0", 0.1}, {"x1", 0.2}, {"y1", 0.1}}).ok); // no line
    CHECK(!reg.execute(doc, "paint_gradient", {{"to", std::string("not a colour")}}).ok);
    CHECK(!m.hasColors());

    CHECK(reg.execute(doc, "paint_gradient", {{"color", std::string("#ff0000")}, {"to", std::string("#0000ff")}}).ok);
    CHECK(near(m.colors[left], kRed) && near(m.colors[right], glm::vec3(0, 0, 1)));
    const glm::vec3 half = srgbToLinear(glm::vec3(0.5f, 0.0f, 0.5f)); // blended as display colours
    CHECK(near(m.colors[mid], half, 0.05f));
    CHECK(near(m.colors[nearestVertex(m, {0, 0, -1})], m.colors[mid], 0.05f)); // a ramp through the whole Part
    CHECK(reg.execute(doc, "undo").ok && !m.hasColors());
    CHECK(reg.execute(doc, "redo").ok && near(m.colors[left], kRed));

    // To clear: the start colour fades out over what is there; masked vertices keep their paint.
    m.ensureMask();
    for (uint32_t v = 0; v < m.vertexCount(); ++v) m.mask[v] = m.positions[v].y > 0.5f ? 1.0f : 0.0f;
    const uint32_t top = nearestVertex(m, {0, 1, 0});
    const glm::vec3 topBefore = m.colors[top];
    CHECK(reg.execute(doc, "paint_gradient", {{"color", std::string("#00ff00")}, {"x0", 0.0}, {"y0", -1.0}, {"x1", 0.0}, {"y1", 1.0}}).ok);
    CHECK(near(m.colors[nearestVertex(m, {0, -1, 0})], glm::vec3(0, 1, 0)));
    CHECK(near(m.colors[top], topBefore));
    CHECK(m.colors[mid].y > 0.1f && m.colors[mid].y < 0.9f && m.colors[mid].x > 0.05f); // half green over the old ramp
    CHECK(reg.execute(doc, "undo").ok && near(m.colors[mid], half, 0.05f));

    // Masked only, and hidden faces are never painted.
    CHECK(reg.execute(doc, "paint_gradient", {{"color", std::string("white")}, {"to", std::string("white")}, {"area", std::string("masked")}}).ok);
    CHECK(near(m.colors[top], glm::vec3(1.0f)) && near(m.colors[left], kRed));
    m.ensureHidden();
    for (uint32_t t : m.vertexTriangles(left)) m.hidden[t] = 1;
    CHECK(reg.execute(doc, "paint_gradient", {{"color", std::string("black")}, {"to", std::string("black")}, {"area", std::string("all")}}).ok);
    CHECK(near(m.colors[left], kRed) && near(m.colors[right], glm::vec3(0.0f)) && near(m.colors[top], glm::vec3(0.0f)));
}

TEST(vertexpaint_fill_skips_hidden) {
    Mesh m = makeIcosphere(2);
    m.ensureHidden();
    const uint32_t v = nearestVertex(m, {0, 1, 0});
    for (uint32_t t : m.vertexTriangles(v)) m.hidden[t] = 1;
    paintFill(m, kRed, PaintArea::All);
    CHECK(m.colors[v] == glm::vec3(1.0f) && m.colors[nearestVertex(m, {0, -1, 0})] == kRed);
}

TEST(vertexpaint_from_cavity) {
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
    CHECK(!doc.mesh.hasColors()); // undo of the first colour edit removes vertex paint
    // Masked vertices keep their colour.
    doc.mesh.ensureMask();
    doc.mesh.mask[dent] = 1.0f;
    CHECK(reg.execute(doc, "paint_from_cavity", {{"smoothing", 0.0}, {"color", std::string("#ff0000")}}).ok);
    CHECK(doc.mesh.colors[dent] == glm::vec3(1.0f));
}

TEST(vertexpaint_sample_color) {
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

TEST(vertexpaint_export_ply_and_usd) {
    CommandRegistry reg;
    Document doc = makeDoc(makeIcosphere(3));
    const std::string plain = tempPath("sf_vertexpaint_plain.ply"), painted = tempPath("sf_vertexpaint.ply");
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

    const std::string usd = tempPath("sf_vertexpaint.usda");
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

TEST(vertexpaint_commands_in_schema) {
    CommandRegistry reg;
    const std::string schema = reg.toolSchemaJson();
    for (const char* name : {"set_color", "paint_fill", "paint_gradient", "paint_clear", "paint_from_cavity", "export_ply"}) {
        CHECK(reg.find(name) != nullptr);
        CHECK(schema.find(std::string("\"") + name + "\"") != std::string::npos);
    }
}

TEST(litsphere_builtin_materials) {
    CHECK(builtinLitSphereCount() >= 4 && builtinLitSphereCount() <= 8);
    for (int i = 0; i < builtinLitSphereCount(); ++i) {
        CHECK(std::string(litsphereName(i)).size() > 0);
        CHECK(findLitSphere(litsphereName(i)) == i);
        for (int j = 0; j < i; ++j) CHECK(std::string(litsphereName(i)) != litsphereName(j));
    }
    CHECK(findLitSphere("red wax") >= 0 && findLitSphere("RedWax") == findLitSphere("Red Wax"));
    CHECK(findLitSphere("chrome") >= 0 && findLitSphere("unobtainium") == -1);
    CHECK(generateLitSphere(-1).empty() && generateLitSphere(builtinLitSphereCount()).empty() && generateLitSphere(0, 0).empty());

    const int size = 64;
    auto pixel = [&](const std::vector<uint8_t>& img, int i, int j) {
        const uint8_t* p = img.data() + (size_t(j) * size + size_t(i)) * 4;
        return glm::ivec4(p[0], p[1], p[2], p[3]);
    };
    const std::vector<uint8_t> wax = generateLitSphere(findLitSphere("Red Wax"), size);
    CHECK(wax.size() == size_t(size) * size * 4);
    const glm::ivec4 c = pixel(wax, size / 2, size / 2);
    CHECK(c.x > c.y + 40 && c.x > c.z + 40 && c.w == 255); // red
    // Lit from the upper left: that side of the sphere is brighter than the lower right (rows are bottom-up).
    const glm::ivec4 lit = pixel(wax, size / 4, size * 3 / 4), dark = pixel(wax, size * 3 / 4, size / 4);
    CHECK(lit.x > dark.x);
    // Corners repeat the rim instead of being undefined.
    CHECK(pixel(wax, 0, 0).w == 255);
    const std::vector<uint8_t> chrome = generateLitSphere(findLitSphere("Chrome"), size);
    CHECK(chrome != wax);
    const glm::ivec4 cc = pixel(chrome, size / 2, size / 2);
    CHECK(std::abs(cc.x - cc.z) < 40); // neutral metal
    // Deterministic.
    CHECK(generateLitSphere(findLitSphere("Chrome"), size) == chrome);

    // Sculpt, the readability-first material, comes first: a neutral warm clay with a strong key from the upper
    // left (a clear light-to-shadow range, so slopes read), and the deepest cavity shading of them all.
    CHECK(findLitSphere("sculpt") == 0);
    const std::vector<uint8_t> sculpt = generateLitSphere(0, size);
    const glm::ivec4 sc = pixel(sculpt, size / 2, size / 2);
    CHECK(sc.x >= sc.z && sc.x - sc.z < 60);
    CHECK(pixel(sculpt, size / 4, size * 3 / 4).x > pixel(sculpt, size * 3 / 4, size / 4).x + 60);
    for (int i = 1; i < builtinLitSphereCount(); ++i) CHECK(litsphereCavity(0).crease > litsphereCavity(i).crease);

    // Soft Clay, the default (last, so the others keep their indices): a matte warm light grey, lit
    // from the upper left, with no highlight anywhere near white.
    CHECK(defaultLitSphere() == findLitSphere("soft clay") && defaultLitSphere() == builtinLitSphereCount() - 1);
    const std::vector<uint8_t> clay = generateLitSphere(defaultLitSphere(), size);
    const glm::ivec4 cl = pixel(clay, size / 2, size / 2);
    CHECK(cl.x >= cl.z && cl.x - cl.z < 30 && cl.x > 120);
    CHECK(pixel(clay, size / 4, size * 3 / 4).x > pixel(clay, size * 3 / 4, size / 4).x + 40);
    int brightest = 0;
    for (size_t k = 0; k < clay.size(); k += 4) brightest = std::max(brightest, int(clay[k]));
    CHECK(brightest < 225);
    CHECK(litsphereCavity(-1).crease > 0.0f && litsphereCavity(builtinLitSphereCount()).crease == litsphereCavity(-1).crease);
    // The pickers list the default first, then every other Lit Sphere once, in index order.
    CHECK(litsphereInListOrder(0) == defaultLitSphere() && litsphereInListOrder(1) == 0);
    CHECK(litsphereInListOrder(-1) == -1 && litsphereInListOrder(builtinLitSphereCount()) == -1);
    std::vector<int> listed;
    for (int k = 0; k < builtinLitSphereCount(); ++k) listed.push_back(litsphereInListOrder(k));
    std::sort(listed.begin(), listed.end());
    for (int i = 0; i < builtinLitSphereCount(); ++i) CHECK(listed[size_t(i)] == i);
}

TEST(litsphere_custom_images) {
    // A lit-sphere image of our own (#15): a 48 x 32 PNG (cropped to its centred 32 x 32), green on the right.
    Image img;
    img.width = 48;
    img.height = 32;
    img.rgba.assign(size_t(48) * 32 * 4, 255);
    for (int y = 0; y < 32; ++y)
        for (int x = 0; x < 48; ++x) {
            uint8_t* p = &img.rgba[(size_t(y) * 48 + size_t(x)) * 4];
            p[0] = uint8_t(x < 24 ? 200 : 20);
            p[1] = uint8_t(x < 24 ? 30 : 220);
            p[2] = uint8_t(y < 16 ? 10 : 90); // top half differs from the bottom half
        }
    const std::string dir = tempPath("sf_litspheres_test");
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    std::string err;
    CHECK(img.savePng(dir + "/Green Gold.png", err));
    { std::ofstream(dir + "/broken.jpg") << "not an image"; }
    { std::ofstream(dir + "/notes.txt") << "ignored"; }

    const int before = litsphereCount();
    const std::vector<std::string> messages = loadLitSphereFolders({dir});
    CHECK(messages.size() == 1 && messages[0].find("broken.jpg") != std::string::npos);
    const int i = findLitSphere("green gold");
    CHECK(i == before && litsphereCount() == before + 1 && isCustomLitSphere(i) && !isCustomLitSphere(0));
    const CustomLitSphere* c = customLitSphere(i);
    CHECK(c && c->size == 32 && c->rgba.size() == size_t(32) * 32 * 4 && c->path.find("Green Gold.png") != std::string::npos);
    // Rows bottom-up like the built-ins: row 0 is the image's bottom row (blue 90); the crop keeps x 8..39.
    CHECK(c->rgba[2] == 90 && c->rgba[(size_t(31) * 32) * 4 + 2] == 10);
    CHECK(c->rgba[0] == 200 && c->rgba[(size_t(31)) * 4 + 1] == 220);
    CHECK(litsphereInListOrder(i) == i && litsphereKey(i).find("Green Gold#") == 0);
    CHECK(generateLitSphere(i, 16).size() == size_t(16) * 16 * 4);

    // Loading the same name again replaces its pixels (same index); a built-in's name is kept for the built-in.
    const uint64_t revision = litsphereRevision(), hash = c->hash;
    CustomLitSphere again;
    CHECK(readLitSphereImage(dir + "/Green Gold.png", again, err));
    again.rgba[0] = 1;
    again.hash = 0;
    CHECK(addCustomLitSphere(again) == i && litsphereRevision() != revision && customLitSphere(i)->hash != hash);
    CustomLitSphere chrome = again;
    chrome.name = "Chrome";
    const int ci = addCustomLitSphere(chrome);
    CHECK(std::string(litsphereName(ci)) == "Chrome 2" && findLitSphere("Chrome") < builtinLitSphereCount());

    // PNG round trip (what projects embed) keeps the pixels exactly.
    std::vector<uint8_t> png;
    CHECK(!encodeLitSpherePng(0, png)); // built-ins aren't embedded
    CHECK(encodeLitSpherePng(i, png));
    CustomLitSphere back;
    CHECK(decodeLitSphereImage(png.data(), png.size(), back, err) && back.hash == customLitSphere(i)->hash);
    CHECK(findLitSphereByHash(back.hash) == i);

    // OpenEXR, linear light: 1.0 is white, 0.5 is sRGB 188, values above 1 clamp.
    VectorDisplacement exr;
    exr.width = exr.height = 16;
    exr.vectors.assign(256, glm::vec3(0.5f, 1.0f, 4.0f));
    CHECK(writeVdmExr(dir + "/Linear.exr", exr, err));
    CustomLitSphere lin;
    CHECK(readLitSphereImage(dir + "/Linear.exr", lin, err) && lin.size == 16 && lin.name == "Linear");
    CHECK(std::abs(int(lin.rgba[0]) - 188) <= 1 && lin.rgba[1] == 255 && lin.rgba[2] == 255);

    // Too small, or not an image at all.
    Image tiny;
    tiny.width = tiny.height = 8;
    tiny.rgba.assign(8 * 8 * 4, 128);
    CHECK(tiny.savePng(dir + "/tiny.png", err));
    CHECK(!readLitSphereImage(dir + "/tiny.png", lin, err) && err.find("too small") != std::string::npos);
    CHECK(!readLitSphereImage(dir + "/missing.png", lin, err));
    CHECK(isLitSphereFile("a/B.EXR") && isLitSphereFile("x.jpeg") && !isLitSphereFile("x.sfbrush"));
    std::filesystem::remove_all(dir);
}

TEST(litsphere_remove_and_trash) {
    // Removing keeps the index taken (the renderer and the view refer to Lit Spheres by index) but takes the Lit Sphere
    // out of the lists and lookups; loading the same name again brings it back at that index.
    CustomLitSphere m;
    m.name = "Removable";
    m.size = 16;
    m.rgba.assign(16 * 16 * 4, 77);
    m.path = "/somewhere/Removable.png";
    const int i = addCustomLitSphere(m);
    CHECK(isLitSphereListed(i) && findLitSphereByPath("/somewhere/Removable.png") == i);
    const uint64_t revision = litsphereRevision();
    CHECK(removeCustomLitSphere(i) && !removeCustomLitSphere(i) && !removeCustomLitSphere(0));
    CHECK(litsphereRevision() != revision && isCustomLitSphere(i) && !isLitSphereListed(i));
    CHECK(findLitSphere("removable") == -1 && findLitSphereByPath(m.path) == -1 && findLitSphereByHash(customLitSphere(i)->hash) == -1);
    const int count = litsphereCount();
    CHECK(addCustomLitSphere(m) == i && isLitSphereListed(i) && litsphereCount() == count);

#if !defined(_WIN32) && !defined(__APPLE__)
    // The freedesktop trash: the file moves to $XDG_DATA_HOME/Trash/files with a .trashinfo saying where it was.
    const std::string root = tempPath("sf_trash_test");
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root + "/lib");
    const char* old = std::getenv("XDG_DATA_HOME");
    const std::string saved = old ? old : "";
    setenv("XDG_DATA_HOME", (root + "/data").c_str(), 1);
    for (int k = 0; k < 2; ++k) { std::ofstream(root + "/lib/Gold Leaf.png") << "png" << k; }
    std::string where, err;
    CHECK(trashSupported() && moveToTrash(root + "/lib/Gold Leaf.png", where, err) && where == "the trash");
    CHECK(!std::filesystem::exists(root + "/lib/Gold Leaf.png"));
    CHECK(std::filesystem::exists(root + "/data/Trash/files/Gold Leaf.png"));
    std::ifstream info(root + "/data/Trash/info/Gold Leaf.png.trashinfo");
    std::stringstream text;
    text << info.rdbuf();
    CHECK(text.str().find("[Trash Info]\nPath=") == 0 && text.str().find("/lib/Gold%20Leaf.png\n") != std::string::npos);
    CHECK(text.str().find("DeletionDate=20") != std::string::npos);
    // A second file of the same name gets a name of its own; a missing file fails and leaves the trash alone.
    { std::ofstream(root + "/lib/Gold Leaf.png") << "again"; }
    CHECK(moveToTrash(root + "/lib/Gold Leaf.png", where, err));
    CHECK(std::filesystem::exists(root + "/data/Trash/files/Gold Leaf.2.png"));
    CHECK(!moveToTrash(root + "/lib/missing.png", where, err) && !err.empty());
    if (old) setenv("XDG_DATA_HOME", saved.c_str(), 1);
    else unsetenv("XDG_DATA_HOME");
    std::filesystem::remove_all(root);
#endif
}

TEST(litsphere_rename_move_and_hash) {
    // #124: renaming keeps the index and the pixels, so a project that carries the image (matched by its pixel hash)
    // still finds it under the new name; a name clash or a name that can't be a file name is refused.
    CustomLitSphere m;
    m.name = "Rename Me";
    m.size = 16;
    m.rgba.assign(16 * 16 * 4, 91);
    m.path = "/lib/Lit Spheres/Rename Me.png";
    const int i = addCustomLitSphere(m);
    CustomLitSphere other = m;
    other.name = "Taken Name";
    other.rgba.assign(16 * 16 * 4, 92);
    other.hash = 0;
    const int j = addCustomLitSphere(other);
    const uint64_t hash = customLitSphere(i)->hash, revision = litsphereRevision();
    std::string err;
    CHECK(renameCustomLitSphere(i, "Brass Polished", err) && litsphereRevision() != revision);
    CHECK(std::string(litsphereName(i)) == "Brass Polished" && findLitSphere("brass polished") == i && findLitSphere("Rename Me") == -1);
    CHECK(findLitSphereByHash(hash) == i && customLitSphere(i)->hash == hash && litsphereKey(i).find("Brass Polished#") == 0);
    CHECK(!renameCustomLitSphere(i, "taken name", err) && err.find("Taken Name") != std::string::npos && findLitSphere("Taken Name") == j);
    CHECK(!renameCustomLitSphere(i, "Chrome", err) && std::string(litsphereName(i)) == "Brass Polished"); // a built-in's
    CHECK(!renameCustomLitSphere(0, "Anything", err));                                                 // built-ins stay
    for (const char* bad : {"", "a/b", "a\\b", "x:y", ".hidden", "trail.", "who?", "pipe|", " lead"})
        CHECK(!checkLitSphereFileName(bad, err) && !renameCustomLitSphere(i, bad, err));
    CHECK(checkLitSphereFileName("Gold Leaf (old) 2", err) && checkLitSphereFileName("Grès émaillé", err));
    // A removed one of the wanted name doesn't block it, and can't take it back later.
    CHECK(removeCustomLitSphere(j) && renameCustomLitSphere(i, "Taken Name", err) && findLitSphere("Taken Name") == i);
    const int k = addCustomLitSphere(m); // "Rename Me" again: a new one
    CHECK(k != i && findLitSphere("Taken Name") == i && removeCustomLitSphere(k));

    // Moving to a sub-folder: the path and the group change, nothing else; the lists' headings follow the groups.
    CHECK(relocateCustomLitSphere(i, "/lib/Lit Spheres/Metals/Taken Name.png", "Metals") && !relocateCustomLitSphere(0, "x", "y"));
    CHECK(customLitSphere(i)->group == "Metals" && findLitSphereByHash(hash) == i);
    const std::vector<std::string> groups = litsphereGroups();
    CHECK(std::find(groups.begin(), groups.end(), "Metals") != groups.end() &&
          std::find(groups.begin(), groups.end(), "") == groups.end());
    CHECK(relocateCustomLitSphere(i, m.path, ""));
    const std::vector<std::string> after = litsphereGroups();
    CHECK(std::find(after.begin(), after.end(), "Metals") == after.end());
    removeCustomLitSphere(i);
}

TEST(vertexpaint_restore_stroke_marks_colors_dirty) {
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

TEST(vertexpaint_dab_follows_the_alpha) {
    // #152: a paint dab with an alpha (Drag Stamp, Dots, Spray) is shaped by it, not a round blob, on both sides.
    Document doc = makeDoc(makeIcosphere(6));
    Mesh& m = doc.mesh;
    BrushAlpha half; // white on its left half, black on its right
    half.name = "Half";
    half.width = half.height = 8;
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x) half.values.push_back(x < 4 ? 1.0f : 0.0f);
    DabParams p = paintParams({1, 0, 0}, 0.4f);
    p.alpha = &half;
    p.alphaUp = {0, 1, 0};
    const glm::vec3 right = glm::cross(p.alphaUp, -p.viewDir); // the stamp's right on the view plane
    CHECK(doc.sculptor.beginStroke(m, "Paint"));
    CHECK(paintDab(doc.sculptor, m, doc.bvh, p, kRed, Symmetry{}) > 0);
    const uint32_t left = nearestVertex(m, glm::normalize(glm::vec3(1, 0, 0) - right * 0.15f));
    const uint32_t rightV = nearestVertex(m, glm::normalize(glm::vec3(1, 0, 0) + right * 0.15f));
    CHECK(m.colors[left].y < 0.5f);             // under the white half: painted
    CHECK(m.colors[rightV] == glm::vec3(1.0f)); // under the black half: untouched (a round dab paints it)
    // The X mirror copy is shaped too (oriented as the sculpt and texture dabs orient it): one side painted, one not.
    const glm::vec3 a = m.colors[nearestVertex(m, glm::normalize(glm::vec3(-1, 0, 0) - right * 0.15f))];
    const glm::vec3 b = m.colors[nearestVertex(m, glm::normalize(glm::vec3(-1, 0, 0) + right * 0.15f))];
    CHECK((a.y < 0.5f && b == glm::vec3(1.0f)) || (b.y < 0.5f && a == glm::vec3(1.0f)));
    doc.undo.push(doc.sculptor.endStroke());

    // Cracks on a dense sphere: a pattern of painted and unpainted vertices inside the dab, not a solid blob.
    Document d2 = makeDoc(makeIcosphere(6));
    BrushAlpha cracks;
    CHECK(BrushAlpha::builtin("Cracks", cracks));
    DabParams c = paintParams({0, 0, 1}, 0.5f);
    c.alpha = &cracks;
    CHECK(d2.sculptor.beginStroke(d2.mesh, "Paint"));
    paintDab(d2.sculptor, d2.mesh, d2.bvh, c, kRed, Symmetry{false, false, false});
    size_t inside = 0, painted = 0;
    for (uint32_t v = 0; v < d2.mesh.vertexCount(); ++v) {
        if (glm::distance(d2.mesh.positions[v], glm::vec3(0, 0, 1)) > 0.25f) continue;
        ++inside;
        painted += d2.mesh.colors[v].y < 0.95f;
    }
    CHECK(inside > 100 && painted > inside / 20 && painted < inside * 9 / 10);
    d2.undo.push(d2.sculptor.endStroke());
}
