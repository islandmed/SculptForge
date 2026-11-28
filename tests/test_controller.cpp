// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Controller and SpaceMouse buttons as shortcuts (core/Controller.h): the ids and names, the trigger threshold, the
// binding table (one action per button, several buttons per action) and the settings' JSON round trip, including
// what a hand-edited or hostile ui.json can hold.
#include "TestHarness.h"

#include "core/Controller.h"

#include <set>

using namespace sf;

TEST(controller_button_ids) {
    CHECK(controllerButtonKnown("pad.south"));
    CHECK(controllerButtonKnown("pad.dpad_left"));
    CHECK(controllerButtonKnown("pad.righttrigger"));
    CHECK(controllerButtonKnown("spacemouse.1"));
    CHECK(controllerButtonKnown("spacemouse.31"));
    CHECK(controllerButtonKnown("spacemouse.32"));
    CHECK(!controllerButtonKnown(""));
    CHECK(!controllerButtonKnown("pad."));
    CHECK(!controllerButtonKnown("pad.nonsense"));
    CHECK(!controllerButtonKnown("spacemouse.0"));
    CHECK(!controllerButtonKnown("spacemouse.01"));
    CHECK(!controllerButtonKnown("spacemouse.33"));
    CHECK(!controllerButtonKnown("spacemouse.-1"));
    CHECK(!controllerButtonKnown("spacemouse.1x"));
    CHECK(!controllerButtonKnown("spacemouse.100"));
    CHECK(!controllerButtonKnown("key.a"));
    // Every button has its own number: a held pie menu waits on it.
    std::set<int> seen;
    for (const std::string& id : controllerPadButtons()) {
        CHECK(controllerButtonKnown(id));
        CHECK(!controllerButtonIsSpaceMouse(id));
        CHECK(seen.insert(controllerButtonIndex(id)).second);
        CHECK(controllerButtonIndex(id) > 0 && controllerButtonIndex(id) < 1000);
    }
    for (int i = 0; i < kControllerSpaceMouseButtons; ++i) {
        const std::string id = controllerSpaceMouseId(i);
        CHECK(controllerButtonIsSpaceMouse(id));
        CHECK(seen.insert(controllerButtonIndex(id)).second);
    }
    CHECK(controllerButtonIndex("nope") == 0);
    CHECK(controllerSpaceMouseId(-1).empty());
    CHECK(controllerSpaceMouseId(kControllerSpaceMouseButtons).empty());
    CHECK(controllerSpaceMouseId(0) == "spacemouse.1");
}

TEST(controller_button_names) {
    CHECK(controllerButtonName("pad.south") == "Pad A / Cross");
    CHECK(controllerButtonName("pad.leftshoulder") == "Pad Left bumper");
    CHECK(controllerButtonName("spacemouse.3") == "SpaceMouse button 3");
    CHECK(controllerButtonName("something.else") == "something.else"); // unknown: as is
    for (const std::string& id : controllerPadButtons()) CHECK(controllerButtonName(id) != id);
}

TEST(controller_trigger_threshold) {
    CHECK(!controllerTriggerPressed(false, 0.0f));
    CHECK(!controllerTriggerPressed(false, 0.59f));
    CHECK(controllerTriggerPressed(false, 0.6f));
    CHECK(controllerTriggerPressed(false, 1.0f));
    // Hysteresis: once pressed, it stays down until it is let well go (a trigger wobbling at the threshold is one press).
    CHECK(controllerTriggerPressed(true, 0.5f));
    CHECK(controllerTriggerPressed(true, 0.36f));
    CHECK(!controllerTriggerPressed(true, 0.35f));
    CHECK(!controllerTriggerPressed(true, 0.0f));
}

TEST(controller_bindings) {
    ControllerSettings s;
    CHECK(s.enabled);
    CHECK(s.buttons.empty());
    CHECK(s.actionFor("pad.south").empty());
    CHECK(s.bind("pad.south", "radial.menu1").empty()); // nothing was there
    CHECK(s.actionFor("pad.south") == "radial.menu1");
    CHECK(s.bind("pad.east", "ui.quickmenu").empty());
    CHECK(s.bind("pad.north", "radial.menu1").empty()); // one action on two buttons
    CHECK(s.buttonsOf("radial.menu1").size() == 2);
    CHECK(s.buttonsOf("radial.menu1")[0] == "pad.south");
    CHECK(s.buttonsOf("radial.menu1")[1] == "pad.north");
    CHECK(s.buttonsOf("nothing").empty());
    CHECK(s.buttonsOf("").empty());
    // A button runs one action: binding it again takes it over and says what it ran.
    CHECK(s.bind("pad.south", "edit.undo") == "radial.menu1");
    CHECK(s.actionFor("pad.south") == "edit.undo");
    CHECK(s.buttons.size() == 3);
    CHECK(s.buttonsOf("radial.menu1").size() == 1);
    CHECK(s.unbind("pad.east"));
    CHECK(!s.unbind("pad.east"));
    CHECK(s.buttons.size() == 2);
    CHECK(s.unbindAction("edit.undo"));
    CHECK(!s.unbindAction("edit.undo"));
    CHECK(s.buttons.size() == 1);
    CHECK(s.actionFor("pad.north") == "radial.menu1");
}

TEST(controller_settings_json_round_trip) {
    ControllerSettings s;
    s.enabled = false;
    s.bind("pad.leftshoulder", "radial.menu2");
    s.bind("spacemouse.4", "brush.clay");
    s.bind("pad.lefttrigger", "ui.quickmenu");
    Json j;
    std::string error;
    CHECK(Json::parse(toJson(s).dump(), j, error));
    ControllerSettings t;
    fromJson(j, t);
    CHECK(t == s);
    CHECK(!t.enabled);
    CHECK(t.buttons.size() == 3);
    CHECK(t.actionFor("spacemouse.4") == "brush.clay");
    // The order they were bound in survives.
    CHECK(t.buttons[0].first == "pad.leftshoulder");
    CHECK(t.buttons[2].first == "pad.lefttrigger");
}

TEST(controller_settings_json_is_forgiving) {
    // Not an object, or no "buttons": left as it was.
    ControllerSettings s;
    s.bind("pad.south", "edit.undo");
    fromJson(Json(), s);
    fromJson(Json(5.0), s);
    fromJson(Json("text"), s);
    CHECK(s.buttons.size() == 1);
    Json only = Json::object();
    only.set("enabled", false);
    fromJson(only, s);
    CHECK(!s.enabled);
    CHECK(s.buttons.size() == 1); // (no "buttons" key: the table stays)
    // Unknown buttons, non-string and empty ids, over-long ids and repeats are dropped; the rest is read.
    Json b = Json::object();
    b.set("pad.south", "radial.menu1");
    b.set("pad.bogus", "radial.menu2");
    b.set("pad.east", 7.0);
    b.set("pad.west", "");
    b.set("pad.north", std::string(kControllerActionIdMax + 1, 'x'));
    b.set("spacemouse.99", "edit.undo");
    b.set("pad.south", "edit.redo"); // (the same key again: Json::set replaces it)
    b.set("spacemouse.2", "view.frame");
    Json j = Json::object();
    j.set("buttons", b);
    ControllerSettings t;
    fromJson(j, t);
    CHECK(t.buttons.size() == 2);
    CHECK(t.actionFor("pad.south") == "edit.redo");
    CHECK(t.actionFor("spacemouse.2") == "view.frame");
    // "buttons" of the wrong type clears the table instead of crashing.
    Json bad = Json::object();
    bad.set("buttons", Json::array());
    fromJson(bad, t);
    CHECK(t.buttons.empty());
}
