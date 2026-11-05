// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The one big-mesh tip (app/BigMeshTip.h): when it shows, and that it shows once.
#include "TestHarness.h"

#include "app/BigMeshTip.h"

using namespace sf;

TEST(big_mesh_tip_due_once_over_the_threshold_when_quiet) {
    const size_t t = kBigMeshTipTriangles;
    CHECK(!bigMeshTipDue(t - 1, t, false, false));  // under ~20M: never
    CHECK(bigMeshTipDue(t, t, false, false));       // the first scene over it
    CHECK(bigMeshTipDue(450'000'000, t, false, false));
    CHECK(!bigMeshTipDue(t, t, true, false));       // once only (shown or dismissed)
    CHECK(!bigMeshTipDue(t, t, false, true));       // waits while a stroke, a card, the tour or a benchmark is on
    CHECK(!bigMeshTipDue(t, 0, false, false));      // (a zero threshold never fires)
}
