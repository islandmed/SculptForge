// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The stroke popup (core/StrokePopup.h): the grid's geometry, which item a point is over, and the latched
// modifiers (Smooth / Mask / Select kept on without their keys).
#include "TestHarness.h"

#include "core/StrokePopup.h"

#include <cmath>

using namespace sf;

namespace {

StrokePopupGeometry geometry() {
    StrokePopupGeometry g;
    g.tile = 60.0f;
    g.label = 20.0f;
    g.gap = 5.0f;
    g.header = 18.0f;
    g.pad = 8.0f;
    g.columns = 5;
    return g;
}

glm::vec2 centre(const StrokePopupGeometry& g, int item) { return g.itemMin(item) + g.itemSize() * 0.5f; }

} // namespace

TEST(stroke_popup_items) {
    CHECK(kStrokePopupItems == int(StrokeType::Count) + 5);
    CHECK(strokePopupIsStroke(0));
    CHECK(strokePopupIsStroke(int(StrokeType::LazyMouse)));
    CHECK(!strokePopupIsStroke(kStrokePopupStrokes));
    CHECK(!strokePopupIsStroke(-1));
    CHECK(strokePopupModifier(strokePopupItem(StrokeModifier::Mask)) == StrokeModifier::Mask);
    CHECK(strokeModifierLatch(StrokeModifier::Add) == StrokeLatch::None);
    CHECK(strokeModifierLatch(StrokeModifier::Sub) == StrokeLatch::None);
    CHECK(strokeModifierLatch(StrokeModifier::Select) == StrokeLatch::Select);
}

TEST(stroke_popup_every_item_hits_itself) {
    const StrokePopupGeometry g = geometry();
    for (int i = 0; i < kStrokePopupItems; ++i) CHECK(g.itemAt(centre(g, i)) == i);
    // Corners: the top-left pixel is in, the far edge is the next item's gap.
    CHECK(g.itemAt(g.itemMin(3)) == 3);
    CHECK(g.itemAt(g.itemMin(3) + glm::vec2(g.tile, 1.0f)) == -1);
}

TEST(stroke_popup_layout) {
    const StrokePopupGeometry g = geometry();
    // Strokes on the first row, left to right; the modifiers on a row of their own below a title.
    CHECK(g.itemMin(1).x > g.itemMin(0).x);
    CHECK(g.itemMin(1).y == g.itemMin(0).y);
    const int add = strokePopupItem(StrokeModifier::Add);
    CHECK(g.itemMin(add).x == g.itemMin(0).x);
    CHECK(g.itemMin(add).y >= g.itemMin(0).y + g.itemSize().y + g.header);
    // Everything fits inside the panel, with the padding left over.
    const glm::vec2 size = g.size();
    for (int i = 0; i < kStrokePopupItems; ++i) {
        const glm::vec2 b = g.itemMin(i) + g.itemSize();
        CHECK(b.x <= size.x - g.pad + 0.01f);
        CHECK(b.y <= size.y - g.pad + 0.01f);
    }
    CHECK(std::abs(size.x - (2 * 8.0f + 5 * 60.0f + 4 * 5.0f)) < 0.01f);
}

TEST(stroke_popup_misses) {
    const StrokePopupGeometry g = geometry();
    CHECK(g.itemAt({-1.0f, 40.0f}) == -1);
    CHECK(g.itemAt({2.0f, 2.0f}) == -1);                      // the padding
    CHECK(g.itemAt({30.0f, g.pad + 4.0f}) == -1);             // the Stroke title
    CHECK(g.itemAt({30.0f, g.sectionTop(true) + 4.0f}) == -1); // the Modifiers title
    CHECK(g.itemAt({g.size().x + 10.0f, 50.0f}) == -1);
    CHECK(g.itemAt({NAN, 50.0f}) == -1);
    CHECK(g.itemAt({INFINITY, INFINITY}) == -1);
    StrokePopupGeometry none = g;
    none.columns = 0;
    CHECK(none.itemAt({30.0f, 60.0f}) == -1);
    CHECK(none.itemMin(0).x < 0.0f);
    CHECK(g.itemMin(kStrokePopupItems).x < 0.0f);
}

TEST(stroke_popup_latch) {
    CHECK(toggledStrokeLatch(StrokeLatch::None, StrokeLatch::Mask) == StrokeLatch::Mask);
    CHECK(toggledStrokeLatch(StrokeLatch::Mask, StrokeLatch::Mask) == StrokeLatch::None);
    CHECK(toggledStrokeLatch(StrokeLatch::Mask, StrokeLatch::Smooth) == StrokeLatch::Smooth);
    CHECK(strokeLatchKeys(StrokeLatch::None).shift == false && strokeLatchKeys(StrokeLatch::None).ctrl == false);
    CHECK(strokeLatchKeys(StrokeLatch::Smooth).shift && !strokeLatchKeys(StrokeLatch::Smooth).ctrl);
    CHECK(!strokeLatchKeys(StrokeLatch::Mask).shift && strokeLatchKeys(StrokeLatch::Mask).ctrl);
    CHECK(strokeLatchKeys(StrokeLatch::Select).shift && strokeLatchKeys(StrokeLatch::Select).ctrl);
    CHECK(std::string(strokeLatchName(StrokeLatch::Select)) == "Select");
    CHECK(std::string(strokeLatchName(StrokeLatch::None)).empty());
}
