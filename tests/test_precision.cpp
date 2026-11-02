// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Precision work (core/Precision.h): fine Draw Size steps, dab spacing for tiny brushes, the precision drag, the
// pick filter, Steady Stroke's string and the point-based pressure curve. tests/ui/precision.sfui drives the same
// in the app (every stroke type, World Size across zoom levels, the keys, Shift+Alt, a curve per brush).
#include "TestHarness.h"

#include "core/Precision.h"
#include "core/Stroke.h"
#include "render/Camera.h"

#include <cmath>
#include <vector>

using namespace sf;

namespace {
bool near(float a, float b, float eps = 1e-3f) { return std::abs(a - b) < eps; }
} // namespace

TEST(precision_draw_size_steps) {
    CHECK(near(drawSizeStep(60.0f, 1, true), 61.0f));
    CHECK(near(drawSizeStep(60.0f, -1, true), 59.0f));
    CHECK(near(drawSizeStep(60.4f, 1, true), 61.0f)); // between pixels: the next whole one
    CHECK(near(drawSizeStep(60.4f, -1, true), 60.0f));
    CHECK(near(drawSizeStep(3.0f, -1, true), 2.0f));
    CHECK(near(drawSizeStep(2.0f, -1, true), 1.5f)); // half pixels at the small end
    CHECK(near(drawSizeStep(1.0f, -1, true), 0.5f));
    CHECK(near(drawSizeStep(0.5f, -1, true), kMinDrawSizePx)); // never below 0.5 px
    CHECK(near(drawSizeStep(0.5f, 1, true), 1.0f));
    CHECK(near(drawSizeStep(1.5f, 1, true), 2.0f));
    CHECK(near(drawSizeStep(2.0f, 1, true), 3.0f));
    CHECK(near(drawSizeStep(800.0f, 1, true), kMaxDrawSizePx));
    // [ / ]: 10%, now down to 0.5 px (was 2)
    CHECK(near(drawSizeStep(100.0f, 1, false), 110.0f));
    float r = 10.0f;
    for (int i = 0; i < 60; ++i) r = drawSizeStep(r, -1, false);
    CHECK(near(r, kMinDrawSizePx));
    // Ctrl+Shift+right-drag: fine and symmetric
    CHECK(near(drawSizeScrub(10.0f, 0.0f), 10.0f));
    CHECK(drawSizeScrub(10.0f, 10.0f) > 10.0f && drawSizeScrub(10.0f, 10.0f) < 10.5f); // 10 px of drag: under 5%
    CHECK(near(drawSizeScrub(drawSizeScrub(10.0f, 50.0f), -50.0f), 10.0f));
    CHECK(near(drawSizeScrub(1.0f, -10000.0f), kMinDrawSizePx));
    CHECK(near(clampDrawSize(0.1f), 0.5f) && near(clampDrawSize(NAN), 60.0f));
}

TEST(precision_tiny_brush_spacing_continuous) {
    // The usual spacing (15% of the radius, at least 1 px) for normal brushes...
    CHECK(near(dabSpacingPx(60.0f, 0.15f), 9.0f));
    CHECK(near(dabSpacingPx(4.0f, 0.15f), 1.0f));
    // ... and tiny ones space by half their radius, so dabs overlap and the line has no gaps.
    for (float r : {0.5f, 0.75f, 1.0f, 1.5f, 2.0f, 3.0f}) {
        const float spacing = dabSpacingPx(r, 0.15f);
        CHECK(spacing <= r + 1e-4f); // neighbouring dabs (2r wide) overlap by at least half
        StrokeSpacer sp;
        sp.reset({0.0f, 0.0f});
        std::vector<glm::vec2> dabs;
        for (int i = 1; i <= 40; ++i) sp.moveTo({float(i) * 0.37f, 0.0f}, spacing, dabs); // a slow, short stroke
        CHECK(!dabs.empty());
        float worst = dabs.empty() ? 1e9f : dabs.front().x;
        for (size_t i = 1; i < dabs.size(); ++i) worst = std::max(worst, dabs[i].x - dabs[i - 1].x);
        CHECK(worst < 2.0f * r); // every gap is bridged by the dabs either side
    }
    // A recipe's own wider spacing still wins for big brushes.
    CHECK(near(dabSpacingPx(40.0f, 0.5f), 20.0f));
}

TEST(precision_drag_quarter_speed) {
    PrecisionDrag d;
    d.reset({100.0f, 100.0f});
    CHECK(d.caughtUp());
    // Held: 40 px of pen motion move the brush 10 px.
    glm::vec2 p = d.follow({140.0f, 100.0f}, true);
    CHECK(near(p.x, 110.0f) && near(p.y, 100.0f));
    p = d.follow({140.0f, 140.0f}, true);
    CHECK(near(p.x, 110.0f) && near(p.y, 110.0f));
    CHECK(!d.caughtUp());
    // Let go: the brush moves with the pen and closes the gap as it goes, without a jump.
    const glm::vec2 before = p;
    p = d.follow({141.0f, 140.0f}, false);
    CHECK(glm::distance(p, before) <= 2.0f + 1e-3f); // at most twice the pen's 1 px
    for (int i = 0; i < 200 && !d.caughtUp(); ++i) p = d.follow({141.0f + float(i + 1), 140.0f}, false);
    CHECK(d.caughtUp());
    // Not held from the start: the brush is the pen.
    d.reset({0.0f, 0.0f});
    p = d.follow({25.0f, -5.0f}, false);
    CHECK(near(p.x, 25.0f) && near(p.y, -5.0f));
}

TEST(precision_pick_smoother) {
    PickSmoother f;
    // Jitter about a steady depth is clearly reduced (RMS)...
    double in2 = 0.0, out2 = 0.0;
    f.reset();
    for (int i = 0; i < 400; ++i) {
        const float noise = float((i * 7919) % 11 - 5) * 0.002f; // +-0.01
        const float out = f.filter(10.0f + noise, 0.5f);
        if (i > 10) in2 += double(noise) * noise, out2 += double(out - 10.0f) * (out - 10.0f);
    }
    CHECK(out2 < 0.6 * in2);
    // ... a steady slope (across a curved surface) is followed without lag ...
    f.reset();
    float out = 0.0f;
    for (int i = 0; i < 100; ++i) out = f.filter(5.0f + 0.01f * float(i), 0.5f);
    CHECK(near(out, 5.99f, 2e-3f));
    // ... and a real step (a jump past the brush) is taken at once.
    out = f.filter(9.0f, 0.5f);
    CHECK(near(out, 9.0f));
    // Off: the input.
    f.amount = 0.0f;
    CHECK(near(f.filter(3.0f, 0.5f), 3.0f));
}

TEST(precision_steady_string) {
    // Steady Stroke is the same string for every stroke type: the brush point waits until the string is taut,
    // then follows a radius behind.
    LazyMouse lm;
    lm.radius = 30.0f;
    lm.reset({0.0f, 0.0f});
    CHECK(near(lm.follow({20.0f, 0.0f}).x, 0.0f)); // within the string: no motion (hand shake filtered)
    const glm::vec2 p = lm.follow({100.0f, 0.0f});
    CHECK(near(p.x, 70.0f));
    // Catch up on release: a slack string (radius 0) puts the brush on the pointer.
    lm.radius = 0.0f;
    CHECK(near(lm.follow({100.0f, 0.0f}).x, 100.0f));
}

TEST(precision_world_size_across_zoom) {
    // World Size keeps radius_px * world-per-pixel fixed: zooming in twice as close doubles the pixels.
    Camera cam;
    cam.target = glm::vec3(0.0f);
    cam.distance = 4.0f;
    const glm::vec3 surface(0.0f, 0.0f, 0.0f);
    const float h = 1080.0f;
    const float worldRadius = 0.002f; // 2 mm
    const float px0 = worldRadius / cam.worldPerPixel(surface, h);
    cam.distance = 2.0f;
    const float px1 = worldRadius / cam.worldPerPixel(surface, h);
    CHECK(near(px1 / px0, 2.0f, 0.02f));
    cam.distance = 0.25f;
    const float px2 = worldRadius / cam.worldPerPixel(surface, h);
    CHECK(near(px2 / px0, 16.0f, 0.2f));
    CHECK(near(px2 * cam.worldPerPixel(surface, h) * kMmPerUnit, 2.0f));
}

TEST(precision_pressure_points) {
    const PressurePoints lin = PressurePoints::linear();
    CHECK(lin.isLinear());
    for (float x : {0.0f, 0.25f, 0.5f, 1.0f}) CHECK(near(lin.apply(x), x));
    // Every preset is monotone, starts at 0 and stays within 0..1.
    for (const auto& preset : PressurePoints::presets()) {
        const PressurePoints c = preset.make();
        CHECK(c.count >= 2 && c.count <= PressurePoints::kMaxPoints);
        CHECK(std::string(c.presetName()) == preset.name);
        float last = -1.0f;
        for (int i = 0; i <= 100; ++i) {
            const float y = c.apply(float(i) / 100.0f);
            CHECK(y >= last - 1e-5f && y >= 0.0f && y <= 1.0f);
            last = y;
        }
        for (int i = 0; i < c.count; ++i) CHECK(near(c.apply(c.points[size_t(i)].x), c.points[size_t(i)].y)); // through its points
    }
    CHECK(PressurePoints::soft().apply(0.3f) > 0.5f);
    CHECK(PressurePoints::firm().apply(0.5f) < 0.3f);
    CHECK(PressurePoints::fineDetail().apply(0.3f) < 0.1f); // light stays light
    CHECK(PressurePoints::fineDetail().apply(1.0f) < 0.8f);
    // Editing: add up to 5 points, remove inner ones, the ends stay.
    PressurePoints c;
    CHECK(c.addPoint(0.5f) && c.count == 3 && near(c.apply(0.5f), 0.5f));
    CHECK(!c.addPoint(0.51f)); // too close
    CHECK(c.addPoint(0.25f) && c.addPoint(0.75f) && c.count == 5);
    CHECK(!c.addPoint(0.9f)); // full
    CHECK(!c.removePoint(0) && !c.removePoint(4));
    CHECK(c.removePoint(2) && c.count == 4);
    // A dragged point never makes the curve go down, nor crosses its neighbour.
    c.points[1] = {0.9f, 0.95f};
    c.points[2].y = 0.1f;
    c.sort();
    for (int i = 1; i < c.count; ++i) {
        CHECK(c.points[size_t(i)].x > c.points[size_t(i - 1)].x);
        CHECK(c.points[size_t(i)].y >= c.points[size_t(i - 1)].y);
    }
    // JSON round trip, and lenient reading.
    const PressurePoints f = PressurePoints::fineDetail();
    CHECK(PressurePoints::fromJson(f.toJson()) == f);
    Json bad = Json::array();
    bad.push(Json("x"));
    CHECK(PressurePoints::fromJson(bad).isLinear());
    CHECK(PressurePoints::fromJson(Json()).isLinear());
}

TEST(precision_brush_centre_under_cursor_at_high_zoom) {
    // The ray for a cursor position meets the surface at a point that projects back to that position, to a small
    // fraction of a pixel, even zoomed far in on a model away from the origin (HiDPI: fractional positions).
    for (bool ortho : {false, true}) {
        Camera cam;
        cam.target = glm::vec3(0.31f, 1.27f, -0.43f);
        cam.yaw = 0.7f;
        cam.pitch = -0.35f;
        cam.ortho = ortho;
        const float w = 2560.0f, h = 1440.0f;
        cam.aspect = w / h;
        // Down to a view about 6 mm across on a model a metre from the origin. (Closer still, 0.002 = 1.3 mm across,
        // float world coordinates themselves give ~0.2 px, for the GPU's drawing as for the ray.)
        for (float distance : {4.0f, 0.05f, 0.01f}) {
            cam.distance = distance;
            const glm::mat4 vp = cam.projection() * cam.view();
            float worst = 0.0f;
            for (glm::vec2 px : {glm::vec2(1280.25f, 720.5f), glm::vec2(1333.75f, 701.125f), glm::vec2(1210.5f, 777.375f)}) {
                glm::vec3 o, d;
                cam.screenRay(px.x, px.y, w, h, o, d);
                // a surface through the target, facing the camera
                const glm::vec3 n = -cam.forward();
                const float t = glm::dot(cam.target - o, n) / glm::dot(d, n);
                const glm::vec4 clip = vp * glm::vec4(o + d * t, 1.0f);
                const glm::vec2 ndc = glm::vec2(clip) / clip.w;
                const glm::vec2 back((ndc.x + 1.0f) * 0.5f * w, (1.0f - ndc.y) * 0.5f * h);
                worst = std::max(worst, glm::distance(back, px));
            }
            CHECK(worst < 0.05f);
        }
    }
}
