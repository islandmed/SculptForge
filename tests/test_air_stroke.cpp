// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
// Start strokes off the model (#62): which left presses on the background wait to become a stroke.
#include "TestHarness.h"

#include "app/AirStroke.h"

using namespace sf;

TEST(air_stroke_starts) {
    CHECK(airStrokeStarts(true, false, false, false));
    CHECK(!airStrokeStarts(false, false, false, false)); // off by default: a drag on the background turns the view
    CHECK(!airStrokeStarts(true, true, false, false));   // Alt+drag still pans (Blender / Maya: orbits)
    CHECK(!airStrokeStarts(true, false, true, false));   // Ctrl+Shift: the hide / show shapes
    CHECK(!airStrokeStarts(true, false, false, true));   // a LOD preview or a pending Insert
}
