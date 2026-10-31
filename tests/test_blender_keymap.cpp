// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Reading a keymap exported from Blender (src/app/BlenderKeymap.cpp) and the numpad chords it needs.
#include "TestHarness.h"

#ifdef SF_TEST_ACTIONS
#include "app/BlenderKeymap.h"

#include <fstream>
#include <iterator>

using namespace sf;

namespace {

std::string readFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(f), {});
}

std::string keysOf(const BlenderKeymap& k, const std::string& action) {
    std::string out;
    for (const auto& b : k.bindings)
        if (b.action == action) out += (out.empty() ? "" : " / ") + chordText(b.chord);
    return out;
}

} // namespace

TEST(numpad_chords_round_trip) {
    Chord c;
    CHECK(parseChord("Numpad 1", c) && c.key == SDLK_KP_1 && chordText(c) == "Numpad 1");
    CHECK(parseChord("Ctrl+Numpad 7", c) && c.key == SDLK_KP_7 && c.ctrl && chordText(c) == "Ctrl+Numpad 7");
    CHECK(parseChord("numpad0", c) && c.key == SDLK_KP_0);
    CHECK(parseChord("Numpad .", c) && c.key == SDLK_KP_PERIOD && chordText(c) == "Numpad .");
    CHECK(parseChord("Numpad Plus", c) && c.key == SDLK_KP_PLUS && chordText(c) == "Numpad Plus");
    CHECK(parseChord("Shift+Numpad Minus", c) && c.key == SDLK_KP_MINUS && c.shift);
    CHECK(parseChord("Home", c) && c.key == SDLK_HOME && chordText(c) == "Home");
    CHECK((chordKeys("Ctrl+Numpad 3") == std::vector<std::string>{"Ctrl", "Numpad 3"}));
    // The number row and the numpad are different keys.
    Chord row;
    CHECK(parseChord("1", row) && parseChord("Numpad 1", c) && !(row == c));
    // An import adds a second key without dropping the first.
    ActionRegistry reg;
    reg.add({"view.front", "Front", nullptr, "", "Transform", "Numpad 1", "", [] {}, nullptr, nullptr});
    reg.assignShortcut("view.front", "Alt+F", true);
    CHECK(reg.shortcutText("view.front") == "Alt+F / Numpad 1");
}

TEST(blender_keymap_export_is_read_as_text) {
    BlenderKeymap k;
    std::string err;
    CHECK(parseBlenderKeymap(readFile(SF_TEST_DATA_DIR "/blender_keymap_export.py"), k, err));
    CHECK(err.empty());
    CHECK(keysOf(k, "brush.standard") == "D");
    CHECK(keysOf(k, "brush.claybuildup") == "Shift+Q"); // a 4.3+ brush asset
    CHECK(keysOf(k, "brush.crease") == "Shift+C");
    CHECK(keysOf(k, "brush.snakehook").empty());        // switched off in Blender ("active": False)
    CHECK(keysOf(k, "ui.size_popup") == "F");
    CHECK(keysOf(k, "ui.strength_popup") == "Shift+R");
    CHECK(keysOf(k, "mask.clear") == "Alt+M");
    CHECK(keysOf(k, "mask.invert") == "Ctrl+I");         // Cmd+I on a Mac
    CHECK(keysOf(k, "geometry.lower_res") == "Alt+1");
    // Sculpt mode's keys win over the 3D View's: Alt+1 and Shift+C stay with Sculpt mode.
    CHECK(keysOf(k, "view.front") == "Numpad 1");        // not the Shift (align to active) one either
    CHECK(keysOf(k, "view.frame") == "Home");
    CHECK(keysOf(k, "view.bottom") == "Ctrl+Numpad 7");
    CHECK(keysOf(k, "view.ortho") == "\\");
    CHECK(keysOf(k, "view.opposite") == "Numpad 9");
    // Read: the keyboard items of the 3D View and Sculpt keymaps (not the mouse, the modal map or Object Mode).
    CHECK(k.items == 19); // 9 in the 3D View (Numpad 1, Alt+1, Shift+Numpad 1, Ctrl+Numpad 7, \\, Home, Shift+C, 9, /), 10 in Sculpt
    // Skipped: Alt+1 front and Shift+C view all (shadowed), Shift+Numpad 1, local view, detail size, Alt+E menu.
    CHECK(k.skipped == 6);
    CHECK(k.bindings.size() == 13);
}

TEST(blender_keymap_rejects_other_text) {
    BlenderKeymap k;
    std::string err;
    CHECK(!parseBlenderKeymap("import os\nos.system('echo hi')\n", k, err) && !err.empty());
    err.clear();
    CHECK(!parseBlenderKeymap("keyconfig_data = []\n", k, err) && !err.empty()); // nothing SculptForge reads
    // Malformed tuples are skipped, not guessed at: an unterminated dict, a nested call, a missing comma.
    const char* odd = "keyconfig_data = [(\"Sculpt\", {\"space_type\": 'EMPTY', \"region_type\": 'WINDOW'}, {\"items\": [\n"
                      "(\"paint.brush_select\", {\"type\": 'X', \"value\": 'PRESS', {\"properties\": [(\"sculpt_tool\", 'DRAW')]}),\n"
                      "(\"paint.brush_select\" {\"type\": 'Y', \"value\": 'PRESS'}, None),\n"
                      "(\"paint.brush_select\", {\"type\": 'G', \"value\": 'PRESS'}, {\"properties\": [(\"sculpt_tool\", 'GRAB')]}),\n"
                      "]})]\n";
    CHECK(parseBlenderKeymap(odd, k, err));
    CHECK(k.bindings.size() == 1 && k.bindings[0].action == "brush.move" && chordText(k.bindings[0].chord) == "G");
}

#endif
