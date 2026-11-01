// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The action registry behind shortcuts, help and the command palette (src/app/Actions.cpp).
#include "TestHarness.h"

#ifdef SF_TEST_ACTIONS
#include "app/Actions.h"
#include "core/Commands.h"

using namespace sf;

TEST(action_chords) {
    Chord c;
    CHECK(parseChord("Ctrl+Shift+Z", c) && c.key == SDLK_Z && c.ctrl && c.shift && !c.alt);
    CHECK(chordText(c) == "Ctrl+Shift+Z");
    CHECK(parseChord("shift+f", c) && c.key == SDLK_F && c.shift && chordText(c) == "Shift+F");
    CHECK(parseChord(",", c) && c.key == SDLK_COMMA && !c.ctrl && chordText(c) == ",");
    CHECK(parseChord("`", c) && c.key == SDLK_GRAVE);
    CHECK(parseChord("F3", c) && c.key == SDLK_F3 && chordText(c) == "F3");
    CHECK(parseChord("Ctrl++", c) && c.key == SDLK_PLUS && c.ctrl);
    CHECK(parseChord("Cmd+D", c) && c.ctrl); // macOS spelling
    CHECK(!parseChord("", c) && !parseChord("Hyper+X", c) && !parseChord("Ctrl+", c));
    const auto two = parseChords("Ctrl+Shift+Z / Ctrl+Y");
    CHECK(two.size() == 2 && two[1].key == SDLK_Y && two[1].ctrl);
    CHECK(chordFromEvent(SDLK_D, SDL_KMOD_LGUI).ctrl); // Cmd counts as Ctrl
    CHECK((chordKeys("Ctrl+Shift+I") == std::vector<std::string>{"Ctrl", "Shift", "I"}));
    CHECK((chordKeys("Enter") == std::vector<std::string>{"Enter"}));
}

TEST(action_registry_keys_and_overrides) {
    ActionRegistry reg;
    int undo = 0, redo = 0, wire = 0;
    reg.add({"edit.undo", "Undo", nullptr, "", "Edit", "Ctrl+Z", "undo", [&] { ++undo; }, nullptr, nullptr});
    reg.add({"edit.redo", "Redo", nullptr, "", "Edit", "Ctrl+Shift+Z / Ctrl+Y", "redo", [&] { ++redo; }, nullptr, nullptr});
    reg.add({"view.wireframe", "Wireframe", nullptr, "", "Render", "W / Shift+F", "", [&] { ++wire; }, nullptr,
             [] { return true; }});
    CHECK(reg.handleKey(SDLK_Z, SDL_KMOD_LCTRL) && undo == 1);
    CHECK(reg.handleKey(SDLK_Z, SDL_Keymod(SDL_KMOD_LCTRL | SDL_KMOD_LSHIFT)) && redo == 1);
    CHECK(reg.handleKey(SDLK_Y, SDL_KMOD_RCTRL) && redo == 2);
    CHECK(reg.handleKey(SDLK_F, SDL_KMOD_LSHIFT) && wire == 1);
    CHECK(!reg.handleKey(SDLK_F, SDL_KMOD_NONE)); // plain F is not bound here
    CHECK(reg.handleKey(SDLK_W, SDL_KMOD_LALT) && wire == 2); // plain keys ignore Alt/Shift when nothing else matches
    CHECK(!reg.handleKey(SDLK_W, SDL_KMOD_LCTRL));
    --wire;
    CHECK(!reg.handleKey(SDLK_LCTRL, SDL_KMOD_LCTRL));

    // ZBrush-style assignment: taking Ctrl+Y from Redo leaves Redo its other chord.
    CHECK(reg.assignShortcut("view.wireframe", "Ctrl+Y") == "Redo");
    CHECK(reg.shortcutText("view.wireframe") == "Ctrl+Y");
    CHECK(reg.shortcutText("edit.redo") == "Ctrl+Shift+Z");
    CHECK(reg.handleKey(SDLK_Y, SDL_KMOD_LCTRL) && wire == 2 && redo == 2);
    CHECK(!reg.handleKey(SDLK_W, SDL_KMOD_NONE));

    // Overrides round-trip through JSON (Store Config) and clear (Restore Standard UI).
    const Json saved = reg.overridesToJson();
    reg.clearOverrides();
    CHECK(reg.shortcutText("view.wireframe") == "W / Shift+F" && !reg.hasOverride("edit.redo"));
    reg.overridesFromJson(saved);
    CHECK(reg.shortcutText("view.wireframe") == "Ctrl+Y" && reg.hasOverride("edit.redo"));
    reg.setShortcut("view.wireframe", "W / Shift+F"); // back to the default: no override left
    CHECK(!reg.hasOverride("view.wireframe"));

    reg.touch("edit.undo");
    reg.touch("edit.redo");
    reg.touch("edit.undo");
    CHECK(reg.recent().size() == 2 && reg.recent()[0] == "edit.undo");
}

TEST(action_registry_commands) {
    CommandRegistry commands;
    ActionRegistry reg;
    std::string ran, prefilled;
    reg.registerCommands(commands, [&](const std::string& n) { ran = n; }, [&](const std::string& n) { prefilled = n; });
    const Action* sub = reg.find("cmd.subdivide");
    CHECK(sub && sub->command == "subdivide" && sub->description.find("Loop subdivision") != std::string::npos);
    sub->run();
    CHECK(ran == "subdivide");
    const Action* open = reg.find("cmd.open"); // path is required: goes to the console instead
    CHECK(open && open->label.find("...") != std::string::npos);
    open->run();
    CHECK(prefilled == "open" && ran == "subdivide");
}
#endif

#include "app/ZBrushNames.h"
#include "core/Sculpt.h"

TEST(zbrush_names_find_renamed_features) {
    using namespace sf;
    CHECK(zbrushNamesIn("Forge Spheres") == "ZSpheres");
    CHECK(zbrushNamesIn("Append", "Tool > Parts") == "SubTool");
    CHECK(zbrushNamesIn("Partial redo").empty()); // whole words only
    CHECK(zbrushNamesIn("Add") == "Zadd");
    CHECK(zbrushNamesIn("Add a folder").empty()); // "Add" only as the whole label
    CHECK(zbrushNamesIn("Load Lit Sphere...") == "MatCap");
    CHECK(zbrushNamesIn("Quad Remesh") == "ZRemesher");
    CHECK(zbrushNamesIn("Quad Remesh", {}, true) == "ZRemesher, QRemesh");
    // Brushes: stored names stay (old projects, hotkeys and scripts), labels are SculptForge's.
    BrushType t = BrushType::Standard;
    CHECK(std::string(brushName(BrushType::DamStandard)) == "DamStandard");
    CHECK(std::string(brushLabel(BrushType::DamStandard)) == "Sharp Crease");
    CHECK(brushFromName("DamStandard", t) && t == BrushType::DamStandard);
    CHECK(brushFromName("sharp crease", t) && t == BrushType::DamStandard);
    CHECK(brushFromName("Clay Block", t) && t == BrushType::ClayBuildup);
    CHECK(brushFromName("snakehook", t) && t == BrushType::SnakeHook);
    CHECK(!brushFromName("frobnicate", t));
}
