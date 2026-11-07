// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// #155: the Stencil (core/Stencil.h): an image over the view the brushes work through. Its placement maps the right
// texel when turned and scaled, nothing happens off it (unless tiled), and the sculpt brushes, Vertex Paint and the
// symmetry copies take it at the place each vertex shows on screen.
#include "TestHarness.h"

#include "core/Commands.h"
#include "core/Primitives.h"
#include "core/Stencil.h"
#include "core/VertexPaint.h"

#include <glm/gtc/matrix_transform.hpp>

#include <cmath>

using namespace sf;

namespace {

bool near(float a, float b, float eps = 1e-4f) { return std::abs(a - b) <= eps; }

// w x h pixels, all black and opaque, with `white` (x, y) pixels white.
Image blackImage(int w, int h, std::initializer_list<glm::ivec2> white = {}) {
    Image img;
    img.width = w;
    img.height = h;
    img.rgba.assign(size_t(w) * size_t(h) * 4, 0);
    for (size_t i = 3; i < img.rgba.size(); i += 4) img.rgba[i] = 255;
    for (glm::ivec2 p : white)
        for (int k = 0; k < 3; ++k) img.rgba[(size_t(p.y) * size_t(w) + size_t(p.x)) * 4 + size_t(k)] = 255;
    return img;
}

// Looking straight down at makePlane (the XZ plane), orthographic: x runs across the canvas, z down it.
glm::mat4 topView() {
    return glm::ortho(-1.0f, 1.0f, -1.0f, 1.0f, 0.1f, 10.0f) * glm::lookAt(glm::vec3(0, 5, 0), glm::vec3(0.0f), glm::vec3(0, 0, -1));
}

DabParams topDab(const glm::vec3& center, float radius) {
    DabParams p;
    p.center = center;
    p.viewDir = glm::vec3(0, -1, 0);
    p.radius = radius;
    p.strength = 1.0f;
    return p;
}

float maxRise(const Mesh& m, bool left) {
    float best = 0.0f;
    for (const glm::vec3& v : m.positions)
        if ((v.x < -0.02f) == left && (v.x > 0.02f) == !left) best = std::max(best, std::abs(v.y));
    return best;
}

} // namespace

TEST(stencil_turned_scaled_maps_the_right_texel) {
    Stencil st;
    st.set(blackImage(2, 2, {{0, 0}}), "corner", ""); // the top-left pixel white
    st.center = {0.5f, 0.5f};
    st.size = 0.5f;
    st.angle = glm::radians(90.0f); // counter-clockwise: the image's top goes to the left
    const float aspect = 2.0f;      // a canvas twice as wide as it is tall
    // Round trip at a few points, turned and not.
    for (float a : {0.0f, 30.0f, 90.0f, -135.0f}) {
        st.angle = glm::radians(a);
        for (glm::vec2 uv : {glm::vec2(0.1f, 0.2f), glm::vec2(0.5f), glm::vec2(0.9f, 0.7f)}) {
            glm::vec2 back;
            CHECK(st.imageCoord(st.canvasPoint(uv, aspect), aspect, back));
            CHECK(near(back.x, uv.x) && near(back.y, uv.y));
        }
    }
    st.angle = glm::radians(90.0f);
    const glm::vec2 top = st.canvasPoint({0.5f, 0.0f}, aspect);
    CHECK(near(top.x, 0.5f - 0.25f / aspect) && near(top.y, 0.5f)); // the top edge's middle: left of the centre
    // The white top-left pixel lands below-left of the centre (top -> left, left -> bottom), the rest is black.
    const glm::vec2 white = st.canvasPoint({0.25f, 0.25f}, aspect);
    CHECK(white.x < 0.5f && white.y > 0.5f);
    glm::vec2 uv;
    CHECK(st.imageCoord(white, aspect, uv) && near(st.value(uv), 1.0f));
    CHECK(st.imageCoord(glm::vec2(0.5f + (0.5f - white.x), white.y), aspect, uv) && near(st.value(uv), 0.0f));
    // Twice the size: the same canvas point is nearer the centre of the image.
    st.size = 1.0f;
    CHECK(st.imageCoord(white, aspect, uv) && near(uv.x, 0.375f) && near(uv.y, 0.375f));
    // Invert: the negative.
    st.size = 0.5f;
    st.invert = true;
    CHECK(st.imageCoord(white, aspect, uv) && near(st.value(uv), 0.0f));
}

TEST(stencil_outside_is_no_effect_unless_tiled) {
    Stencil st;
    st.set(blackImage(4, 4, {{0, 0}, {1, 0}, {2, 0}, {3, 0}, {0, 1}, {1, 1}, {2, 1}, {3, 1}}), "top", ""); // top half white
    st.size = 0.2f;
    glm::vec2 uv;
    CHECK(!st.imageCoord({0.9f, 0.9f}, 1.0f, uv));
    StencilView view{&st, topView(), 1.0f};
    CHECK(view.weight(glm::vec3(0.0f, 0.0f, -0.05f)) > 0.99f); // on the image's top half
    CHECK(view.weight(glm::vec3(0.0f, 0.0f, 0.05f)) < 0.01f);  // its bottom half
    CHECK(view.weight(glm::vec3(0.8f, 0.0f, 0.8f)) == 0.0f);   // off the image
    st.tile = true; // repeats: one image height (0.2 of the canvas = 0.4 world) further down is the top half again
    CHECK(st.imageCoord({0.9f, 0.9f}, 1.0f, uv));
    CHECK(view.weight(glm::vec3(0.0f, 0.0f, -0.05f + 0.4f)) > 0.99f);
    st.visible = false;
    CHECK(!view.active());
    // Behind a perspective camera: nothing (in front, the same line of sight takes it).
    st.visible = true;
    st.tile = false;
    const StencilView persp{&st, glm::perspective(glm::radians(50.0f), 1.0f, 0.1f, 100.0f) *
                                     glm::lookAt(glm::vec3(0, 5, 0), glm::vec3(0.0f), glm::vec3(0, 0, -1)), 1.0f};
    CHECK(persp.weight(glm::vec3(0.0f, 0.0f, -0.2f)) > 0.99f);
    CHECK(persp.weight(glm::vec3(0.0f, 20.0f, -0.2f)) == 0.0f);
}

TEST(stencil_sculpts_only_under_the_image) {
    Document doc = makeDoc(makePlane(80));
    Mesh& m = doc.mesh;
    // A white image whose right edge sits on the canvas centre: the brush's left half is under it.
    Stencil st;
    st.set(blackImage(1, 1, {{0, 0}}), "white", "");
    st.size = 0.6f;
    st.center = {0.5f - 0.3f, 0.5f};
    DabParams p = topDab(glm::vec3(0.0f), 0.4f);
    p.stencil = {&st, topView(), 1.0f};
    CHECK(doc.sculptor.beginStroke(m, "Standard"));
    for (int i = 0; i < 4; ++i) doc.sculptor.dab(m, doc.bvh, BrushType::Standard, p, Symmetry{false, false, false});
    doc.sculptor.endStroke();
    CHECK(maxRise(m, true) > 0.01f);
    CHECK(maxRise(m, false) == 0.0f);
    // Without the stencil the same dab raises both sides.
    Document plain = makeDoc(makePlane(80));
    DabParams q = topDab(glm::vec3(0.0f), 0.4f);
    CHECK(plain.sculptor.beginStroke(plain.mesh, "Standard"));
    plain.sculptor.dab(plain.mesh, plain.bvh, BrushType::Standard, q, Symmetry{false, false, false});
    plain.sculptor.endStroke();
    CHECK(maxRise(plain.mesh, true) > 0.0f && maxRise(plain.mesh, false) > 0.0f);
}

TEST(stencil_symmetry_mirrors_the_pattern) {
    // The image covers only the right of the canvas; a dab there with X symmetry also raises its mirror on the left,
    // which takes the stencil at its mirror image's place (the right), so the pattern mirrors across.
    Document doc = makeDoc(makePlane(80));
    Mesh& m = doc.mesh;
    Stencil st;
    st.set(blackImage(1, 1, {{0, 0}}), "white", "");
    st.size = 0.4f;
    st.center = {0.75f, 0.5f};
    DabParams p = topDab(glm::vec3(0.5f, 0.0f, 0.0f), 0.25f);
    p.stencil = {&st, topView(), 1.0f};
    CHECK(doc.sculptor.beginStroke(m, "Standard"));
    doc.sculptor.dab(m, doc.bvh, BrushType::Standard, p, Symmetry{true, false, false});
    doc.sculptor.endStroke();
    CHECK(maxRise(m, false) > 0.0f);
    CHECK(near(maxRise(m, true), maxRise(m, false), 1e-5f));
}

TEST(stencil_vertex_paint_lays_its_colours) {
    Document doc = makeDoc(makePlane(40));
    Mesh& m = doc.mesh;
    Image red = blackImage(1, 1);
    red.rgba[0] = 255;
    Stencil st;
    st.set(red, "red", "");
    st.size = 2.0f; // covers the whole view
    DabParams p = topDab(glm::vec3(0.0f), 0.5f);
    p.stencil = {&st, topView(), 1.0f};
    CHECK(doc.sculptor.beginStroke(m, "Paint"));
    CHECK(paintDab(doc.sculptor, m, doc.bvh, p, glm::vec3(0, 0, 1), Symmetry{false, false, false}) > 0);
    const uint32_t centre = uint32_t(m.vertexCount() / 2);
    CHECK(m.colors[centre].x > 0.99f && m.colors[centre].z < 0.01f); // red, not the blue paint colour
    doc.sculptor.endStroke();
    // Colours off: its brightness masks the paint colour; a black stencil (red's luminance is low) paints little.
    st.set(blackImage(1, 1), "black", "");
    st.colors = false;
    Document doc2 = makeDoc(makePlane(40));
    CHECK(doc2.sculptor.beginStroke(doc2.mesh, "Paint"));
    CHECK(paintDab(doc2.sculptor, doc2.mesh, doc2.bvh, p, glm::vec3(0, 0, 1), Symmetry{false, false, false}) == 0);
    doc2.sculptor.endStroke();
}

TEST(stencil_commands) {
    Document doc = makeDoc(makePlane(4));
    CommandRegistry reg;
    CHECK(!reg.execute(doc, "stencil_load", {}).ok);
    CHECK(!reg.execute(doc, "stencil_load", {{"alpha", true}}).ok); // Round: no alpha
    CHECK(reg.execute(doc, "brush_set_alpha", {{"name", "cracks"}}).ok);
    CHECK(reg.execute(doc, "stencil_load", {{"alpha", true}, {"reset", true}}).ok);
    CHECK(doc.stencil.active() && doc.stencil.name == "Cracks");
    CHECK(reg.execute(doc, "stencil_set", {{"x", 0.25}, {"angle", 450.0}, {"size", 100.0}, {"opacity", 2.0}, {"tile", true}}).ok);
    CHECK(near(doc.stencil.center.x, 0.25f) && near(glm::degrees(doc.stencil.angle), 90.0f, 1e-3f));
    CHECK(doc.stencil.size == 20.0f && doc.stencil.opacity == 1.0f && doc.stencil.tile);
    CHECK(reg.execute(doc, "stencil_set", {{"visible", false}}).ok && !doc.stencil.active());
    CHECK(reg.execute(doc, "stencil_clear", {}).ok && !doc.stencil.image);
    CHECK(!reg.execute(doc, "stencil_load", {{"path", "/nonexistent/stencil.png"}}).ok);
}
