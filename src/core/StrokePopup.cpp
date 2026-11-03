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

glm::vec2 StrokePopupGeometry::size() const {
    const int cols = columns > 0 ? columns : 1;
    const float w = 2.0f * pad + float(cols) * tile + float(cols - 1) * gap;
    const int r = rows(kStrokePopupStrokes) + rows(kStrokePopupModifiers);
    const float h = 2.0f * pad + 2.0f * header + float(r) * itemSize().y + float(r - 1) * gap + gap; // + the break
    return {w, h};
}

float StrokePopupGeometry::sectionTop(bool modifiers) const {
    if (!modifiers) return pad;
    const int r = rows(kStrokePopupStrokes);
    return pad + header + float(r) * (itemSize().y + gap) + gap;
}

glm::vec2 StrokePopupGeometry::itemMin(int item) const {
    if (columns <= 0 || item < 0 || item >= kStrokePopupItems) return {-1.0f, -1.0f};
    const bool mod = !strokePopupIsStroke(item);
    const int i = mod ? item - kStrokePopupStrokes : item;
    const int col = i % columns, row = i / columns;
    return {pad + float(col) * (tile + gap), sectionTop(mod) + header + float(row) * (itemSize().y + gap)};
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
