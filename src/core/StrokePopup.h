// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The stroke popup (Discord 11-02): one key opens a plain grid at the cursor with the stroke types and the common
// stroke modifiers (Add / Sub, and Smooth, Mask, Select kept on without holding Shift, Ctrl or Ctrl+Shift), so a
// stroke change needs no key combination. A flat grid, not a pie ring. Independent of SDL and ImGui so it can be
// unit tested: the grid's geometry, which item a point is over, and the latched modifiers. The app side (opening,
// hold-and-release, clicks, drawing) is src/app/StrokePopup.cpp.
#pragma once

#include "core/Stroke.h"

#include <glm/glm.hpp>

namespace sf {

// A modifier kept on until it is picked again (or another brush is picked): strokes act as if its keys were held.
enum class StrokeLatch { None, Smooth, Mask, Select };
const char* strokeLatchName(StrokeLatch latch); // "Smooth", "Mask", "Select"; "" for None
// The keys a latch stands for (the ZBrush meanings the sculpting code reads): Smooth = Shift, Mask = Ctrl,
// Select = Ctrl+Shift.
struct StrokeLatchKeys {
    bool shift = false, ctrl = false;
};
StrokeLatchKeys strokeLatchKeys(StrokeLatch latch);
// Picking the latch that is on turns it off; picking another switches to it.
StrokeLatch toggledStrokeLatch(StrokeLatch now, StrokeLatch picked);

// The grid's items: the stroke types first (item i = StrokeType(i)), then the modifiers.
enum class StrokeModifier { Add, Sub, Smooth, Mask, Select, Count };
constexpr int kStrokePopupStrokes = int(StrokeType::Count);
constexpr int kStrokePopupModifiers = int(StrokeModifier::Count);
constexpr int kStrokePopupItems = kStrokePopupStrokes + kStrokePopupModifiers;
inline bool strokePopupIsStroke(int item) { return item >= 0 && item < kStrokePopupStrokes; }
inline StrokeModifier strokePopupModifier(int item) { return StrokeModifier(item - kStrokePopupStrokes); }
inline int strokePopupItem(StrokeModifier m) { return kStrokePopupStrokes + int(m); }
StrokeLatch strokeModifierLatch(StrokeModifier m); // Smooth / Mask / Select; None for Add and Sub

// Two sections, each a titled block of rows: Stroke, then Modifiers. All sizes in pixels (already scaled).
struct StrokePopupGeometry {
    float tile = 72.0f;   // the square icon area of an item
    float label = 30.0f;  // the item's label and key hint under the icon
    float gap = 6.0f;     // between items
    float header = 22.0f; // a section's title above its rows
    float pad = 10.0f;    // around everything
    int columns = 5;

    glm::vec2 itemSize() const { return {tile, tile + label}; }
    int rows(int count) const { return columns > 0 ? (count + columns - 1) / columns : 0; }
    glm::vec2 size() const;              // the whole panel
    float sectionTop(bool modifiers) const; // a section's title, from the panel's top
    glm::vec2 itemMin(int item) const;   // an item's top-left, from the panel's top-left
    int itemAt(glm::vec2 p) const;       // the item under p (panel space); -1 over a gap, a title or outside
};

} // namespace sf
