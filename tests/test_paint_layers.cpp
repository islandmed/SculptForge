// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Paint layers (core/PaintLayer.h): blend maths, compositing, painting into a
// layer, the paint_layer_* commands with undo / redo, .sfproj round trips,
// topology changes and the cost of compositing per dab on a big mesh.
#include "TestHarness.h"

#include "core/Commands.h"
#include "core/Decimate.h"
#include "core/PaintLayer.h"
#include "core/Polygroups.h"
#include "core/Polypaint.h"
#include "core/Primitives.h"
#include "core/Project.h"
#include "io/PlyIO.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>

using namespace sf;

namespace {

bool near(const glm::vec3& a, const glm::vec3& b, float eps = 2e-3f) { return glm::all(glm::lessThan(glm::abs(a - b), glm::vec3(eps))); }
bool nearF(float a, float b, float eps = 2e-3f) { return std::abs(a - b) < eps; }

uint32_t nearestVertex(const Mesh& m, const glm::vec3& p) {
    uint32_t best = 0;
    for (uint32_t v = 1; v < m.vertexCount(); ++v)
        if (glm::distance(m.positions[v], p) < glm::distance(m.positions[best], p)) best = v;
    return best;
}

DabParams dabAt(const glm::vec3& center, float radius) {
    DabParams p;
    p.center = center;
    p.viewDir = -glm::normalize(center);
    p.radius = radius;
    p.strength = 1.0f;
    return p;
}

// Display (sRGB) <-> linear for expectations.
glm::vec3 lin(float r, float g, float b) { return srgbToLinear(glm::vec3(r, g, b)); }

bool sameStack(const Mesh& a, const Mesh& b) {
    if (a.colors != b.colors || a.paintBase != b.paintBase || a.activePaintLayer != b.activePaintLayer ||
        a.paintLayers.size() != b.paintLayers.size())
        return false;
    for (size_t i = 0; i < a.paintLayers.size(); ++i) {
        const PaintLayer &x = a.paintLayers[i], &y = b.paintLayers[i];
        if (x.name != y.name || x.opacity != y.opacity || x.blend != y.blend || x.visible != y.visible ||
            x.locked != y.locked || x.color != y.color || x.alpha != y.alpha)
            return false;
    }
    return true;
}

const glm::vec3 kRed(1.0f, 0.0f, 0.0f);

} // namespace

TEST(paint_layers_blend_maths) {
    const glm::vec3 b(0.25f, 0.5f, 0.75f), grey(0.5f);
    // Photoshop reference values (display space, 0..1).
    CHECK(near(blendColors(BlendMode::Normal, b, grey), grey));
    CHECK(near(blendColors(BlendMode::Multiply, b, grey), glm::vec3(0.125f, 0.25f, 0.375f)));
    CHECK(near(blendColors(BlendMode::Screen, b, grey), glm::vec3(0.625f, 0.75f, 0.875f)));
    CHECK(near(blendColors(BlendMode::Overlay, b, grey), b)); // 50% grey is neutral in Overlay
    CHECK(near(blendColors(BlendMode::Overlay, glm::vec3(0.25f), glm::vec3(0.8f)), glm::vec3(0.4f)));
    CHECK(near(blendColors(BlendMode::Overlay, glm::vec3(0.75f), glm::vec3(0.8f)), glm::vec3(0.9f)));
    CHECK(near(blendColors(BlendMode::SoftLight, b, grey), b)); // and in Soft Light
    CHECK(near(blendColors(BlendMode::SoftLight, glm::vec3(0.5f), glm::vec3(1.0f)), glm::vec3(std::sqrt(0.5f))));
    CHECK(near(blendColors(BlendMode::Add, b, grey), glm::vec3(0.75f, 1.0f, 1.0f))); // clamped
    CHECK(near(blendColors(BlendMode::Subtract, b, grey), glm::vec3(0.0f, 0.0f, 0.25f)));
    CHECK(near(blendColors(BlendMode::Darken, b, grey), glm::vec3(0.25f, 0.5f, 0.5f)));
    CHECK(near(blendColors(BlendMode::Lighten, b, grey), glm::vec3(0.5f, 0.5f, 0.75f)));
    CHECK(near(blendColors(BlendMode::Difference, b, grey), glm::vec3(0.25f, 0.0f, 0.25f)));
    // White multiplies to nothing, black screens to nothing.
    CHECK(near(blendColors(BlendMode::Multiply, b, glm::vec3(1.0f)), b));
    CHECK(near(blendColors(BlendMode::Screen, b, glm::vec3(0.0f)), b));

    BlendMode m;
    CHECK(parseBlendMode("multiply", m) && m == BlendMode::Multiply);
    CHECK(parseBlendMode("Soft Light", m) && m == BlendMode::SoftLight);
    CHECK(parseBlendMode("soft_light", m) && m == BlendMode::SoftLight);
    CHECK(parseBlendMode("Linear-Dodge", m) && m == BlendMode::Add);
    CHECK(parseBlendMode("OVERLAY", m) && m == BlendMode::Overlay);
    CHECK(!parseBlendMode("vivid", m));
    for (int i = 0; i < kBlendModeCount; ++i)
        CHECK(parseBlendMode(blendModeId(BlendMode(i)), m) && m == BlendMode(i) && parseBlendMode(blendModeLabel(m), m) &&
              m == BlendMode(i));
}

TEST(paint_layers_compositing) {
    Mesh m = makeIcosphere(2);
    m.ensureColors(lin(0.8f, 0.4f, 0.2f));
    const std::vector<glm::vec3> before = m.colors;
    // One layer added and removed again: the polypaint is exactly what it was.
    CHECK(addPaintLayer(m, "Temp") == 0 && hasPaintLayers(m) && m.paintBase == before);
    CHECK(m.colors == before); // a fresh layer is transparent
    CHECK(removePaintLayer(m, 0) && !hasPaintLayers(m) && m.paintBase.empty());
    CHECK(m.colors == before && m.activePaintLayer == -1);

    // Normal at 50%: halfway in display space. Multiply on top of it at full coverage.
    const int normal = addPaintLayer(m, "Normal");
    const int mult = addPaintLayer(m, "Mult");
    CHECK(normal == 0 && mult == 1 && m.activePaintLayer == 1);
    const uint32_t v = 3, untouched = 5;
    m.paintLayers[0].color[v] = lin(0.2f, 0.6f, 1.0f);
    m.paintLayers[0].alpha[v] = 1.0f;
    m.paintLayers[0].opacity = 0.5f;
    m.paintLayers[1].color[v] = lin(0.5f, 0.5f, 0.5f);
    m.paintLayers[1].alpha[v] = 1.0f;
    m.paintLayers[1].blend = BlendMode::Multiply;
    compositePaintAll(m);
    const glm::vec3 afterNormal(0.5f, 0.5f, 0.6f);
    CHECK(near(linearToSrgb(m.colors[v]), afterNormal * 0.5f));
    CHECK(m.colors[untouched] == before[untouched]); // no layer covers it: Base exactly

    // Hidden layers don't count; opacity scales coverage; alpha scales too.
    m.paintLayers[1].visible = false;
    compositePaintAll(m);
    CHECK(near(linearToSrgb(m.colors[v]), afterNormal));
    m.paintLayers[1].visible = true;
    m.paintLayers[1].alpha[v] = 0.5f;
    compositePaintAll(m);
    CHECK(near(linearToSrgb(m.colors[v]), afterNormal * 0.75f));
    // Incremental compositing gives the same result as the whole-mesh pass.
    const glm::vec3 full = m.colors[v];
    m.colors[v] = glm::vec3(0.0f);
    const uint32_t one[] = {v};
    compositePaint(m, one);
    CHECK(m.colors[v] == full);

    // Overlay with 50% grey changes nothing (blending happens in display space, as in Photoshop).
    Mesh o = makeIcosphere(1);
    o.ensureColors(lin(0.3f, 0.6f, 0.9f));
    addPaintLayer(o, "Overlay");
    o.paintLayers[0].blend = BlendMode::Overlay;
    std::fill(o.paintLayers[0].color.begin(), o.paintLayers[0].color.end(), lin(0.5f, 0.5f, 0.5f));
    std::fill(o.paintLayers[0].alpha.begin(), o.paintLayers[0].alpha.end(), 1.0f);
    compositePaintAll(o);
    CHECK(near(linearToSrgb(o.colors[0]), glm::vec3(0.3f, 0.6f, 0.9f)));
}

TEST(paint_layers_brush_paints_active_layer_with_undo) {
    Document doc = makeDoc(makeIcosphere(4));
    Mesh& m = doc.mesh;
    CommandRegistry reg;
    CHECK(reg.execute(doc, "paint_fill", {{"color", std::string("#808080")}}).ok); // Base: grey
    const std::vector<glm::vec3> base = m.colors;
    CHECK(reg.execute(doc, "paint_layer_add", {{"name", std::string("Red")}, {"blend", std::string("multiply")}}).ok);
    CHECK(m.paintLayers.size() == 1 && m.paintLayers[0].blend == BlendMode::Multiply && m.activePaintLayer == 0);

    const uint32_t c = nearestVertex(m, {1, 0, 0}), far = nearestVertex(m, {0, 0, 1});
    CHECK(doc.sculptor.beginStroke(m, "Paint"));
    CHECK(paintDab(doc.sculptor, m, doc.bvh, dabAt(m.positions[c], 0.5f), kRed, Symmetry{false, false, false}) > 0);
    doc.undo.push(doc.sculptor.endStroke());
    CHECK(m.paintBase == base); // non-destructive: Base untouched
    CHECK(nearF(m.paintLayers[0].alpha[c], 1.0f) && near(m.paintLayers[0].color[c], kRed));
    CHECK(m.paintLayers[0].alpha[far] == 0.0f && m.colors[far] == base[far]);
    // Multiply red over grey: (0.5 * 1, 0.5 * 0, 0.5 * 0) in display space.
    CHECK(near(linearToSrgb(m.colors[c]), glm::vec3(128.0f / 255.0f * 1.0f, 0.0f, 0.0f), 3e-3f));

    // Alt erases the layer's coverage instead of painting white.
    DabParams erase = dabAt(m.positions[c], 0.5f);
    erase.invert = true;
    CHECK(doc.sculptor.beginStroke(m, "Paint"));
    paintDab(doc.sculptor, m, doc.bvh, erase, kRed, Symmetry{false, false, false});
    doc.undo.push(doc.sculptor.endStroke());
    CHECK(m.paintLayers[0].alpha[c] < 1e-4f && near(m.colors[c], base[c], 1e-3f));
    CHECK(reg.execute(doc, "undo").ok);
    CHECK(nearF(m.paintLayers[0].alpha[c], 1.0f));
    CHECK(reg.execute(doc, "undo").ok); // the paint stroke
    CHECK(m.paintLayers[0].alpha[c] == 0.0f && m.colors[c] == base[c]);
    CHECK(reg.execute(doc, "redo").ok);
    CHECK(nearF(m.paintLayers[0].alpha[c], 1.0f) && m.colors[c] != base[c]);

    // A locked (or hidden) layer can't be painted or filled.
    CHECK(reg.execute(doc, "paint_layer_set", {{"locked", true}}).ok);
    CHECK(doc.sculptor.beginStroke(m, "Paint"));
    CHECK(paintDab(doc.sculptor, m, doc.bvh, dabAt(m.positions[far], 0.5f), kRed, Symmetry{}) == 0);
    doc.sculptor.endStroke();
    CHECK(!reg.execute(doc, "paint_fill").ok);
    CHECK(!reg.execute(doc, "paint_layer_clear").ok);
    CHECK(reg.execute(doc, "paint_layer_set", {{"locked", false}, {"visible", false}}).ok);
    CHECK(!reg.execute(doc, "paint_fill").ok);
    CHECK(reg.execute(doc, "undo").ok && m.paintLayers[0].visible && m.paintLayers[0].locked);

    // Painting Base with layers present writes Base and recomposites.
    CHECK(reg.execute(doc, "paint_layer_select", {{"layer", std::string("Base")}}).ok && m.activePaintLayer == -1);
    CHECK(reg.execute(doc, "paint_fill", {{"color", std::string("#ffffff")}}).ok);
    CHECK(near(m.paintBase[far], glm::vec3(1.0f)) && near(m.colors[far], glm::vec3(1.0f)));
    CHECK(near(linearToSrgb(m.colors[c]), kRed, 3e-3f)); // red multiplied over white
    CHECK(reg.execute(doc, "undo").ok && m.paintBase == base);
}

TEST(paint_layers_commands_and_undo) {
    Document doc = makeDoc(makeIcosphere(3));
    Mesh& m = doc.mesh;
    CommandRegistry reg;
    for (const char* name : {"paint_layer_add", "paint_layer_set", "paint_layer_select", "paint_layer_duplicate",
                             "paint_layer_delete", "paint_layer_move", "paint_layer_merge", "paint_layer_flatten",
                             "paint_layer_clear", "paint_layer_list"})
        CHECK(reg.find(name) != nullptr);
    CHECK(!reg.execute(doc, "paint_layer_add", {{"blend", std::string("vivid")}}).ok);
    CHECK(!m.hasColors());

    // Adding the first layer to an unpainted mesh creates white Base; undo removes it all again.
    CHECK(reg.execute(doc, "paint_layer_add", {{"name", std::string("Skin")}}).ok);
    CHECK(m.hasColors() && m.paintLayers.size() == 1);
    CHECK(reg.execute(doc, "undo").ok && !m.hasColors() && m.paintLayers.empty() && m.paintBase.empty());
    CHECK(reg.execute(doc, "redo").ok && m.paintLayers.size() == 1 && m.paintLayers[0].name == "Skin");

    CHECK(reg.execute(doc, "paint_fill", {{"color", std::string("#ff0000")}}).ok); // fills the active layer
    CHECK(std::all_of(m.paintLayers[0].alpha.begin(), m.paintLayers[0].alpha.end(), [](float a) { return nearF(a, 1.0f); }));
    CHECK(reg.execute(doc, "paint_layer_add", {{"name", std::string("Dirt")}, {"opacity", 0.5}, {"blend", std::string("multiply")}}).ok);
    CHECK(reg.execute(doc, "paint_fill", {{"color", std::string("#808080")}}).ok);
    CHECK(reg.execute(doc, "paint_layer_add", {{"name", std::string("Dirt")}}).ok); // unique names
    CHECK(m.paintLayers.size() == 3 && m.paintLayers[2].name == "Dirt 2");
    CHECK(reg.execute(doc, "paint_layer_select", {{"index", 1.0}}).ok && m.activePaintLayer == 1);
    const Mesh three = m;

    // Settings: one undo step each, and the composite follows.
    const glm::vec3 shown = m.colors[0];
    CHECK(reg.execute(doc, "paint_layer_set", {{"layer", std::string("Dirt")}, {"opacity", 1.0}, {"name", std::string("Grime")}}).ok);
    CHECK(m.paintLayers[1].name == "Grime" && m.paintLayers[1].opacity == 1.0f && m.colors[0] != shown);
    CHECK(!reg.execute(doc, "paint_layer_set", {{"index", -1.0}, {"opacity", 0.5}}).ok); // Base is opaque
    CHECK(!reg.execute(doc, "paint_layer_set", {{"index", 7.0}}).ok);
    CHECK(!reg.execute(doc, "paint_layer_set", {{"layer", std::string("Nope")}}).ok);
    CHECK(reg.execute(doc, "undo").ok && sameStack(m, three));

    // Move, duplicate, delete, merge, flatten: each undoes exactly.
    CHECK(reg.execute(doc, "paint_layer_move", {{"direction", std::string("up")}}).ok);
    CHECK(m.paintLayers[2].name == "Dirt" && m.activePaintLayer == 2);
    CHECK(!reg.execute(doc, "paint_layer_move", {{"direction", std::string("up")}}).ok); // already on top
    CHECK(reg.execute(doc, "undo").ok && sameStack(m, three));
    CHECK(reg.execute(doc, "paint_layer_duplicate").ok && m.paintLayers.size() == 4 && m.paintLayers[2].name == "Dirt copy");
    CHECK(m.paintLayers[2].alpha == m.paintLayers[1].alpha && m.activePaintLayer == 2);
    CHECK(reg.execute(doc, "undo").ok && sameStack(m, three));
    CHECK(reg.execute(doc, "paint_layer_delete", {{"index", 0.0}}).ok && m.paintLayers.size() == 2);
    CHECK(!reg.execute(doc, "paint_layer_delete", {{"index", -1.0}}).ok);
    CHECK(reg.execute(doc, "undo").ok && sameStack(m, three));

    // Merge "Dirt" (multiply 50%) onto "Skin" (normal, opaque): looks the same as before.
    const std::vector<glm::vec3> look = m.colors;
    CHECK(reg.execute(doc, "paint_layer_merge", {{"index", 1.0}}).ok);
    CHECK(m.paintLayers.size() == 2 && m.paintLayers[0].name == "Skin" && m.activePaintLayer == 0);
    bool same = true;
    for (size_t v = 0; v < look.size(); ++v) same = same && near(m.colors[v], look[v], 3e-3f);
    CHECK(same);
    CHECK(reg.execute(doc, "undo").ok && sameStack(m, three));
    // Merging into a hidden or locked layer is refused.
    CHECK(reg.execute(doc, "paint_layer_set", {{"index", 0.0}, {"visible", false}}).ok);
    CHECK(!reg.execute(doc, "paint_layer_merge", {{"index", 1.0}}).ok);
    CHECK(reg.execute(doc, "undo").ok && sameStack(m, three));
    CHECK(!reg.execute(doc, "paint_layer_merge", {{"index", -1.0}}).ok);

    // Merge the lowest layer into Base: exact.
    CHECK(reg.execute(doc, "paint_layer_merge", {{"index", 0.0}}).ok && m.paintLayers.size() == 2);
    same = true;
    for (size_t v = 0; v < look.size(); ++v) same = same && near(m.colors[v], look[v], 3e-3f);
    CHECK(same && near(linearToSrgb(m.paintBase[0]), kRed, 3e-3f));
    CHECK(reg.execute(doc, "undo").ok && sameStack(m, three));

    // Flatten: plain polypaint that looks the same; undo brings the layers back.
    CHECK(reg.execute(doc, "paint_layer_flatten").ok && m.paintLayers.empty() && m.paintBase.empty());
    CHECK(m.colors == look);
    CHECK(reg.execute(doc, "undo").ok && sameStack(m, three));
    CHECK(reg.execute(doc, "redo").ok && m.paintLayers.empty());
    CHECK(reg.execute(doc, "undo").ok);

    // Clear a layer (not the active one) and undo.
    CHECK(reg.execute(doc, "paint_layer_clear", {{"layer", std::string("Skin")}}).ok);
    CHECK(std::all_of(m.paintLayers[0].alpha.begin(), m.paintLayers[0].alpha.end(), [](float a) { return a == 0.0f; }));
    CHECK(reg.execute(doc, "undo").ok && sameStack(m, three));

    const CommandResult list = reg.execute(doc, "paint_layer_list");
    CHECK(list.ok && list.message.find("Dirt 2") < list.message.find("Dirt (multiply, 50%)") &&
          list.message.find("Dirt (multiply, 50%)") < list.message.find("-1 Base"));

    // paint_clear removes every layer too (undoable).
    CHECK(reg.execute(doc, "paint_clear").ok && !m.hasColors() && m.paintLayers.empty());
    CHECK(reg.execute(doc, "undo").ok && sameStack(m, three));
}

TEST(paint_layers_project_round_trip) {
    CommandRegistry reg;
    Document doc = makeDoc(makeIcosphere(3));
    CHECK(reg.execute(doc, "paint_fill", {{"color", std::string("#336699")}}).ok);
    CHECK(reg.execute(doc, "paint_layer_add", {{"name", std::string("Freckles")}, {"blend", std::string("overlay")}, {"opacity", 0.75}}).ok);
    CHECK(reg.execute(doc, "paint_fill", {{"color", std::string("#c08040")}, {"strength", 0.6}}).ok);
    CHECK(reg.execute(doc, "paint_layer_add", {{"name", std::string("Hidden")}}).ok);
    CHECK(reg.execute(doc, "paint_fill", {{"color", std::string("#00ff00")}}).ok);
    CHECK(reg.execute(doc, "paint_layer_set", {{"visible", false}, {"locked", true}}).ok);
    CHECK(reg.execute(doc, "paint_layer_select", {{"index", 0.0}}).ok);
    // A second SubTool without paint layers.
    doc.addSubtool(makeIcosphere(1), "Plain", false);

    const std::string path = tempPath("sf_paint_layers.sfproj");
    std::string err;
    CHECK(saveProject(path, doc, err));
    Document back;
    CHECK(loadProject(path, back, err));
    CHECK(back.subtoolCount() == 2);
    CHECK(sameStack(back.mesh, doc.mesh));
    CHECK(back.mesh.paintLayers[0].blend == BlendMode::Overlay && back.mesh.paintLayers[0].opacity == 0.75f);
    CHECK(!back.mesh.paintLayers[1].visible && back.mesh.paintLayers[1].locked && back.mesh.activePaintLayer == 0);
    CHECK(back.subtools[1].mesh.paintLayers.empty());
    // Undo after loading has no history, but painting works on the loaded stack.
    CHECK(reg.execute(back, "paint_fill", {{"color", std::string("#ffffff")}}).ok);
    CHECK(reg.execute(back, "undo").ok && sameStack(back.mesh, doc.mesh));
    std::filesystem::remove(path);

    // Exports carry the composited colours (what the viewport shows).
    const std::string ply = tempPath("sf_paint_layers.ply");
    CHECK(reg.execute(doc, "subtool_select", {{"index", 0.0}}).ok);
    CHECK(reg.execute(doc, "subtool_visibility", {{"index", 1.0}}).ok); // hide "Plain"
    CHECK(reg.execute(doc, "export_ply", {{"path", ply}}).ok);
    Mesh loaded;
    SplatCloud splats;
    PlyContent content;
    CHECK(loadPly(ply, loaded, splats, content, err) && loaded.hasColors() && loaded.vertexCount() == doc.mesh.vertexCount());
    bool same = true;
    for (uint32_t v = 0; same && v < loaded.vertexCount(); ++v) {
        const uint32_t o = nearestVertex(doc.mesh, loaded.positions[v]);
        same = near(linearToSrgb(loaded.colors[v]), linearToSrgb(doc.mesh.colors[o]), 0.5f / 255.0f + 1e-4f);
    }
    CHECK(same);
    std::filesystem::remove(ply);
}

TEST(paint_layers_topology_changes) {
    Mesh m = makeIcosphere(2);
    m.ensureColors(lin(0.2f, 0.4f, 0.6f));
    addPaintLayer(m, "Spots");
    m.paintLayers[0].blend = BlendMode::Screen;
    for (uint32_t v = 0; v < m.vertexCount(); v += 3) {
        m.paintLayers[0].alpha[v] = 1.0f;
        m.paintLayers[0].color[v] = lin(1.0f, 0.5f, 0.0f);
    }
    compositePaintAll(m);
    auto consistent = [](const Mesh& x) {
        if (x.paintLayers.empty() || x.paintBase.size() != x.vertexCount() || x.colors.size() != x.vertexCount()) return false;
        for (uint32_t v = 0; v < x.vertexCount(); ++v)
            if (!near(x.colors[v], compositePaintVertex(x, v), 1e-5f)) return false;
        return true;
    };

    const Mesh sub = loopSubdivide(m);
    CHECK(sub.paintLayers.size() == 1 && sub.paintLayers[0].blend == BlendMode::Screen && consistent(sub));
    CHECK(sub.paintLayers[0].alpha.size() == sub.vertexCount());
    const Mesh dec = decimate(sub, sub.triangleCount() / 2);
    CHECK(dec.paintLayers.size() == 1 && consistent(dec));
    std::vector<uint8_t> keep(m.triangleCount(), 0);
    for (size_t t = 0; t < keep.size() / 2; ++t) keep[t] = 1;
    const Mesh half = extractTriangles(m, keep);
    CHECK(half.paintLayers.size() == 1 && consistent(half));

    // Something that changes the vertex count without knowing paint layers flattens them.
    Mesh grown = m;
    grown.positions.push_back(glm::vec3(5.0f));
    grown.colors.push_back(glm::vec3(1.0f));
    grown.rebuildTopology();
    CHECK(grown.paintLayers.empty() && grown.paintBase.empty() && grown.hasColors() && grown.activePaintLayer == -1);

    // Duplicating a SubTool copies its layers.
    Document doc = makeDoc(m);
    CommandRegistry reg;
    CHECK(reg.execute(doc, "subtool_duplicate").ok && doc.mesh.paintLayers.size() == 1 && doc.mesh.paintLayers[0].name == "Spots");
}

TEST(paint_layers_dab_cost_on_big_mesh) {
    // ~655k vertices with three layers: a dab recomposites only what it touches.
    Document doc = makeDoc(makeIcosphere(8));
    Mesh& m = doc.mesh;
    CommandRegistry reg;
    using Clock = std::chrono::steady_clock;
    CHECK(reg.execute(doc, "paint_fill", {{"color", std::string("#808080")}}).ok);
    for (const char* blend : {"multiply", "overlay", "screen"})
        CHECK(reg.execute(doc, "paint_layer_add", {{"blend", std::string(blend)}, {"opacity", 0.8}}).ok);
    auto t0 = Clock::now();
    compositePaintAll(m);
    const double fullMs = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();

    CHECK(doc.sculptor.beginStroke(m, "Paint"));
    const uint32_t c = nearestVertex(m, {1, 0, 0});
    t0 = Clock::now();
    size_t touched = 0;
    for (int i = 0; i < 20; ++i)
        touched += paintDab(doc.sculptor, m, doc.bvh, dabAt(m.positions[c] + glm::vec3(0, 0.005f * float(i), 0), 0.08f), kRed, Symmetry{});
    const double dabMs = std::chrono::duration<double, std::milli>(Clock::now() - t0).count() / 20.0;
    doc.undo.push(doc.sculptor.endStroke());
    std::printf("    paint layers: %zu vertices, full composite %.1f ms, dab %.2f ms (%zu vertices per dab)\n", m.vertexCount(),
                fullMs, dabMs, touched / 20);
    CHECK(touched > 0);
    CHECK(dabMs < timeLimit(25.0)); // generous for debug builds; a whole-mesh composite per dab would be far slower
}
