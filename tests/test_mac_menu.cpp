// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// SculptForge > Preferences... in the macOS app menu (#111, src/app/MacAppMenu.h): the item answers a click once,
// and leaves Cmd+, to the shortcut so Preferences never opens twice.
#include "TestHarness.h"

#include "app/MacAppMenu.h"

using namespace sf;

TEST(mac_menu_preferences_once) {
    macmenu::takePreferencesRequest(); // (start clean)
    CHECK(!macmenu::takePreferencesRequest());
    macmenu::requestPreferences();
    macmenu::requestPreferences(); // two clicks before the next frame still open it once
    CHECK(macmenu::takePreferencesRequest());
    CHECK(!macmenu::takePreferencesRequest());
}

TEST(mac_menu_skips_its_key_equivalent) {
    CHECK(macmenu::isOwnKeyEquivalent(true, true, ","));   // Cmd+,: the shortcut already ran
    CHECK(!macmenu::isOwnKeyEquivalent(false, false, nullptr)); // a click
    CHECK(!macmenu::isOwnKeyEquivalent(true, false, "\r")); // Return in a menu opened from the keyboard
    CHECK(!macmenu::isOwnKeyEquivalent(true, true, "p"));
    CHECK(!macmenu::isOwnKeyEquivalent(true, true, nullptr));
}
