// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
// Hold Space and drag to move the camera (#43): what each press does, and when Space counts as held.
#include "TestHarness.h"

#include "app/SpaceCamera.h"

using namespace sf;

TEST(space_camera_buttons) {
    CHECK(spaceCameraMove(1, false) == SpaceCameraMove::Orbit); // left turns
    CHECK(spaceCameraMove(3, false) == SpaceCameraMove::Pan);   // right pans
    CHECK(spaceCameraMove(2, false) == SpaceCameraMove::Pan);   // middle pans
    for (int b = 1; b <= 3; ++b) CHECK(spaceCameraMove(b, true) == SpaceCameraMove::Zoom); // Ctrl zooms
    CHECK(spaceCameraMove(4, false) == SpaceCameraMove::None); // side buttons are left alone
    CHECK(spaceCameraMove(0, true) == SpaceCameraMove::None);
}

TEST(space_camera_key) {
    CHECK(spaceCameraKeyDown(true, false));
    CHECK(!spaceCameraKeyDown(false, false)); // off by default (ZBrush's Space opens the QuickMenu)
    CHECK(!spaceCameraKeyDown(true, true));   // typing a space in a text field
}
