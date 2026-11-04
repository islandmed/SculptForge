// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/StrokePopup.h"

#include <cmath>

namespace sf {

const char* strokeLatchName(StrokeLatch latch) {
    switch (latch) {
    case StrokeLatch::Smooth: return "Smooth";
    case StrokeLatch::Mask: return "Mask";
    case StrokeLatch::Select: return "Select";
    default: return "";
    }
}

StrokeLatchKeys strokeLatchKeys(StrokeLatch latch) {
    switch (latch) {
    case StrokeLatch::Smooth: return {true, false};
    case StrokeLatch::Mask: return {false, true};
    case StrokeLatch::Select: return {true, true};
    default: return {};
    }
}

StrokeLatch toggledStrokeLatch(StrokeLatch now, StrokeLatch picked) { return picked == now ? StrokeLatch::None : picked; }

StrokeLatch strokeModifierLatch(StrokeModifier m) {
    switch (m) {
    case StrokeModifier::Smooth: return StrokeLatch::Smooth;
    case StrokeModifier::Mask: return StrokeLatch::Mask;
    case StrokeModifier::Select: return StrokeLatch::Select;
    default: return StrokeLatch::None;
    }
}

namespace {

int sectionOf(int item) { return strokePopupIsStroke(item) ? 0 : strokePopupIsShape(item) ? 2 : 1; }
int sectionCount(int section) { return section == 0 ? kStrokePopupStrokes : section == 1 ? kStrokePopupModifiers : kStrokePopupShapes; }
int sectionFirst(int section) { return section == 0 ? 0 : section == 1 ? kStrokePopupStrokes : kStrokePopupStrokes + kStrokePopupModifiers; }

} // namespace

glm::vec2 StrokePopupGeometry::size() const {
    const int cols = columns > 0 ? columns : 1;
    const float w = 2.0f * pad + float(cols) * tile + float(cols - 1) * gap;
    const int r = rows(kStrokePopupStrokes) + rows(kStrokePopupModifiers) + rows(kStrokePopupShapes);
    const float h = 2.0f * pad + 3.0f * header + float(r) * itemSize().y + float(r - 1) * gap + 2.0f * gap; // + the breaks
    return {w, h};
}

float StrokePopupGeometry::sectionTop(int section) const {
    float y = pad;
    for (int s = 0; s < section && s < 2; ++s) y += header + float(rows(sectionCount(s))) * (itemSize().y + gap) + gap;
    return y;
}

glm::vec2 StrokePopupGeometry::itemMin(int item) const {
    if (columns <= 0 || item < 0 || item >= kStrokePopupItems) return {-1.0f, -1.0f};
    const int section = sectionOf(item);
    const int i = item - sectionFirst(section);
    const int col = i % columns, row = i / columns;
    return {pad + float(col) * (tile + gap), sectionTop(section) + header + float(row) * (itemSize().y + gap)};
}

int StrokePopupGeometry::itemAt(glm::vec2 p) const {
    if (!std::isfinite(p.x) || !std::isfinite(p.y) || columns <= 0) return -1;
    const glm::vec2 s = itemSize();
    for (int item = 0; item < kStrokePopupItems; ++item) {
        const glm::vec2 a = itemMin(item);
        if (p.x >= a.x && p.x < a.x + s.x && p.y >= a.y && p.y < a.y + s.y) return item;
    }
    return -1;
}

} // namespace sf
