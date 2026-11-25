// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
// Preferences > Navigation (src/app/NavBindings.h): custom drag gestures, their text, what they take over in each key
// style, and the ui.json round trip. The hold keys live in the action registry (test_actions.cpp covers rebinding).
#include "TestHarness.h"

#include "app/NavBindings.h"

using namespace sf;

TEST(nav_drag_parse_and_text) {
    NavDrag d;
    CHECK(parseNavDrag("Ctrl+Middle", d) && d.button == 2 && d.ctrl && !d.shift && !d.alt);
    CHECK(navDragText(d) == "Ctrl+Middle");
    CHECK(parseNavDrag("shift + alt + left", d) && d.button == 1 && d.shift && d.alt && !d.ctrl);
    CHECK(navDragText(d) == "Shift+Alt+Left"); // canonical order: Ctrl, Shift, Alt
    CHECK(parseNavDrag("RIGHT", d) && d.button == 3 && !d.ctrl && !d.shift && !d.alt && navDragText(d) == "Right");
    CHECK(parseNavDrag("Cmd+rmb", d) && d.button == 3 && d.ctrl); // macOS spelling, short button names
    CHECK(parseNavDrag("", d) && !d.set() && navDragText(d).empty());
    CHECK(parseNavDrag("none", d) && !d.set());
    CHECK(parseNavDrag("Ctrl+Shift", d) && !d.set()); // modifiers without a button: nothing
    CHECK(!parseNavDrag("Ctrl+Z", d));                // a key, not a button
    CHECK(!parseNavDrag("Hyper+Middle", d));
    NavDrag same;
    parseNavDrag("Ctrl+Middle", same);
    NavDrag again;
    parseNavDrag("middle+ctrl", again);
    CHECK(same == again);
}

TEST(nav_drag_usable_and_match) {
    NavDrag plainLeft{1, false, false, false};
    CHECK(plainLeft.set() && !plainLeft.usable()); // a plain left drag sculpts: never a gesture
    CHECK(!plainLeft.matches(1, false, false, false));
    NavDrag shiftLeft{1, false, true, false};
    CHECK(shiftLeft.usable() && shiftLeft.matches(1, false, true, false));
    CHECK(!shiftLeft.matches(1, true, true, false)); // exact modifiers: Ctrl+Shift+left is not Shift+left
    CHECK(!shiftLeft.matches(2, false, true, false));
    NavDrag middle{2, false, false, false};
    CHECK(middle.usable() && middle.matches(2, false, false, false)); // a plain middle or right drag may be bound
    NavDrag none;
    CHECK(!none.usable() && !none.matches(0, false, false, false));

    NavBindings nav;
    CHECK(!nav.any());
    CHECK(navDragMatch(nav, 2, true, false, false) == -1); // nothing set: the key style's gestures
    nav.drag[2] = NavDrag{2, true, false, false};          // Ctrl+Middle zooms
    CHECK(nav.any());
    CHECK(navDragMatch(nav, 2, true, false, false) == 2);
    CHECK(navDragMatch(nav, 2, false, false, false) == -1);
    nav.drag[1] = NavDrag{2, true, false, false}; // the same gesture for Pan: the first in the list (Pan) wins
    CHECK(navDragMatch(nav, 2, true, false, false) == 1);
    nav.drag[0] = plainLeft; // set but unusable: never matches
    CHECK(navDragMatch(nav, 1, false, false, false) == -1);
}

TEST(nav_style_gestures) {
    const NavDrag ctrlMiddle{2, true, false, false}, shiftLeft{1, false, true, false}, altLeft{1, false, false, true},
        right{3, false, false, false}, altRight{3, false, false, true}, ctrlShiftRight{3, true, true, false},
        shiftAltRight{3, false, true, true}, allRight{3, true, true, true}, plainLeft{1, false, false, false};
    for (int style = 0; style < 4; ++style) {
        CHECK(std::string(navStyleGesture(style, plainLeft)).empty()); // unusable: no warning of its own
        CHECK(std::string(navStyleGesture(style, NavDrag())).empty());
        CHECK(std::string(navStyleGesture(style, shiftLeft)).find("Smooth") != std::string::npos); // Shift smooths everywhere
    }
    // Classic and SculptForge: Ctrl+middle zooms, right turns, Alt+right pans, Ctrl+Shift+right scrubs the size.
    for (int style : {0, 1}) {
        CHECK(std::string(navStyleGesture(style, ctrlMiddle)).find("Zoom") != std::string::npos);
        CHECK(std::string(navStyleGesture(style, right)).find("Turn") != std::string::npos);
        CHECK(std::string(navStyleGesture(style, altRight)).find("Pan") != std::string::npos);
        CHECK(std::string(navStyleGesture(style, ctrlShiftRight)).find("size") != std::string::npos);
        CHECK(std::string(navStyleGesture(style, altLeft)).find("Invert") != std::string::npos);
        CHECK(std::string(navStyleGesture(style, shiftAltRight)).find("Pan") != std::string::npos); // Alt pans, Shift or not
        CHECK(std::string(navStyleGesture(style, allRight)).empty()); // free in these styles
    }
    // Blender: Alt+left is the emulated middle button; Maya: Alt+left orbits, Alt+right zooms.
    CHECK(std::string(navStyleGesture(2, altLeft)).find("Turn") != std::string::npos);
    CHECK(std::string(navStyleGesture(2, ctrlMiddle)).find("Zoom") != std::string::npos);
    CHECK(std::string(navStyleGesture(3, altLeft)).find("Turn") != std::string::npos);
    CHECK(std::string(navStyleGesture(3, altRight)).find("Zoom") != std::string::npos);
    CHECK(std::string(navStyleGesture(3, right)).find("Turn") != std::string::npos); // Maya keeps the classic right drag
    // Which move a style gesture drives: a custom gesture that already does that move is no clash.
    CHECK(navStyleGestureMove(navStyleGesture(1, NavDrag{2, false, true, false})) == 1); // Shift+Middle pans
    CHECK(navStyleGestureMove(navStyleGesture(1, ctrlMiddle)) == 2);
    CHECK(navStyleGestureMove(navStyleGesture(1, right)) == 0);
    CHECK(navStyleGestureMove(navStyleGesture(1, shiftLeft)) == -1); // Smooth: not a camera move
    CHECK(navStyleGestureMove("") == -1);
}

TEST(nav_bindings_json_round_trip) {
    NavBindings nav;
    nav.drag[0] = NavDrag{3, false, true, false}; // Shift+Right orbits
    nav.drag[2] = NavDrag{2, true, false, true};  // Ctrl+Alt+Middle zooms
    const Json j = toJson(nav);
    CHECK(j["orbit"].isString() && j["orbit"].asString() == "Shift+Right");
    CHECK(!j["pan"].isString()); // unset moves are left out
    CHECK(j["zoom"].asString() == "Ctrl+Alt+Middle");
    NavBindings back;
    fromJson(j, back);
    CHECK(back.drag[0] == nav.drag[0] && back.drag[1] == nav.drag[1] && back.drag[2] == nav.drag[2]);
    // Nothing saved (an older ui.json, or no gestures): the defaults stay; a bad value is ignored.
    NavBindings untouched;
    fromJson(Json(), untouched);
    CHECK(!untouched.any());
    Json bad = Json::object();
    bad.set("pan", std::string("Ctrl+Z"));
    bad.set("zoom", std::string("Middle"));
    fromJson(bad, untouched);
    CHECK(!untouched.drag[1].set() && untouched.drag[2].button == 2);
}
