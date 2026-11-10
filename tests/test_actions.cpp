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

    // classic assignment: taking Ctrl+Y from Redo leaves Redo its other chord.
    CHECK(reg.assignShortcut("view.wireframe", "Ctrl+Y") == "Redo");
    CHECK(reg.shortcutText("view.wireframe") == "Ctrl+Y");
    CHECK(reg.shortcutText("edit.redo") == "Ctrl+Shift+Z");
    CHECK(reg.handleKey(SDLK_Y, SDL_KMOD_LCTRL) && wire == 2 && redo == 2);
    CHECK(!reg.handleKey(SDLK_W, SDL_KMOD_NONE));

    // Overrides round-trip through JSON (saved automatically) and clear (Reset Layout and Shortcuts).
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

TEST(action_registry_style_default_before_add) {
    // #147: the input style is applied at startup before every action is registered; one added afterwards keeps
    // the style's key, not its own default, and a user override still wins.
    ActionRegistry reg;
    reg.setDefaultShortcut("ui.size_popup", "F");
    reg.add({"ui.size_popup", "Size at cursor", nullptr, "", "Draw", "S", "", [] {}, nullptr, nullptr});
    CHECK(reg.shortcutText("ui.size_popup") == "F");
    reg.add({"ui.size_popup", "Size at cursor", nullptr, "", "Draw", "S", "", [] {}, nullptr, nullptr}); // again
    CHECK(reg.shortcutText("ui.size_popup") == "F");
    reg.setDefaultShortcut("ui.size_popup", "S"); // the style changes
    CHECK(reg.shortcutText("ui.size_popup") == "S");
    reg.setShortcut("ui.size_popup", "Ctrl+Alt+S");
    reg.setDefaultShortcut("ui.size_popup", "F");
    reg.add({"ui.size_popup", "Size at cursor", nullptr, "", "Draw", "S", "", [] {}, nullptr, nullptr});
    CHECK(reg.shortcutText("ui.size_popup") == "Ctrl+Alt+S");
}

TEST(action_brush_size_keys_follow_rebinding) {
    // Ziel on Discord: holding a rebound Smaller / Larger brush key repeats like [ and ] do (App.cpp asks
    // brushSizeStep(actionForKey(...)) for key repeats, so this is what decides it).
    ActionRegistry reg;
    reg.add({"brush.size_down", "Smaller brush", nullptr, "", "Draw", "[", "", [] {}, nullptr, nullptr});
    reg.add({"brush.size_up", "Larger brush", nullptr, "", "Draw", "]", "", [] {}, nullptr, nullptr});
    reg.add({"brush.size_down_fine", "Smaller brush by 1 px", nullptr, "", "Draw", "Shift+[", "", [] {}, nullptr, nullptr});
    reg.add({"brush.size_up_fine", "Larger brush by 1 px", nullptr, "", "Draw", "Shift+]", "", [] {}, nullptr, nullptr});
    reg.add({"view.wireframe", "Wireframe", nullptr, "", "Render", "W", "", [] {}, nullptr, nullptr});
    const auto step = [&](SDL_Keycode k, SDL_Keymod m = SDL_KMOD_NONE) { return brushSizeStep(reg.actionForKey(k, m)); };
    CHECK(step(SDLK_LEFTBRACKET) == -1 && step(SDLK_RIGHTBRACKET) == 1);
    CHECK(step(SDLK_LEFTBRACKET, SDL_KMOD_LSHIFT) == -1 && step(SDLK_W) == 0 && step(SDLK_Q) == 0);
    reg.setShortcut("brush.size_down", "Q");
    reg.setShortcut("brush.size_up", "E");
    reg.setShortcut("brush.size_down_fine", "Shift+A");
    reg.setShortcut("brush.size_up_fine", "Alt+D");
    CHECK(step(SDLK_Q) == -1 && step(SDLK_E) == 1);
    CHECK(step(SDLK_A, SDL_KMOD_LSHIFT) == -1 && step(SDLK_D, SDL_KMOD_LALT) == 1);
    CHECK(step(SDLK_LEFTBRACKET) == 0 && step(SDLK_RIGHTBRACKET) == 0); // the old keys no longer repeat
    CHECK(step(SDLK_W) == 0 && step(SDLK_LSHIFT, SDL_KMOD_LSHIFT) == 0);
    CHECK(brushSizeStep(nullptr) == 0);
}

TEST(action_mac_modifiers) {
    // fenix on Discord: macOS Option is Alt, Cmd is Ctrl, never Cmd as Alt (SDL reports Option as ALT, Cmd as GUI;
    // Cmd standing in for Alt comes from Wine's Mac driver when the Windows build runs under CrossOver).
    CHECK(chordFromEvent(SDLK_D, SDL_KMOD_LALT).alt && !chordFromEvent(SDLK_D, SDL_KMOD_LALT).ctrl);
    CHECK(chordFromEvent(SDLK_D, SDL_KMOD_RGUI).ctrl && !chordFromEvent(SDLK_D, SDL_KMOD_RGUI).alt);
    ActionRegistry reg;
    int mask = 0, save = 0;
    reg.add({"mask.invert", "Invert mask", nullptr, "", "Mask", "Alt+I", "", [&] { ++mask; }, nullptr, nullptr});
    reg.add({"file.save", "Save", nullptr, "", "File", "Ctrl+S", "", [&] { ++save; }, nullptr, nullptr});
    CHECK(reg.handleKey(SDLK_I, SDL_KMOD_LALT) && mask == 1);  // Option+I
    CHECK(!reg.handleKey(SDLK_I, SDL_KMOD_LGUI) && mask == 1); // Cmd+I is not Alt+I
    CHECK(reg.handleKey(SDLK_S, SDL_KMOD_LGUI) && save == 1);  // Cmd+S saves
    reg.setShortcut("mask.invert", "Alt+K"); // rebound: still Option, still not Cmd
    CHECK(reg.handleKey(SDLK_K, SDL_KMOD_RALT) && mask == 2);
    CHECK(!reg.handleKey(SDLK_K, SDL_KMOD_RGUI) && mask == 2);
    reg.setShortcut("file.save", "Cmd+J"); // the macOS spelling binds Ctrl, which Cmd presses
    CHECK(reg.handleKey(SDLK_J, SDL_KMOD_LGUI) && reg.handleKey(SDLK_J, SDL_KMOD_LCTRL) && save == 3);
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

#include <algorithm>
#include "app/SearchAliases.h"
#include "core/Sculpt.h"

TEST(search_aliases_are_generic_words) {
    using namespace sf;
    auto has = [](const std::vector<std::string>& v, const char* w) { return std::find(v.begin(), v.end(), w) != v.end(); };
    CHECK(has(searchAliasesIn("Quad Remesh"), "retopo"));
    CHECK(has(searchAliasesIn("Voxel Remesh"), "block out"));
    CHECK(has(searchAliasesIn("Append", "Tool > Parts"), "kitbash"));
    CHECK(has(searchAliasesIn("Duplicate", "Tool > Parts"), "folders"));
    CHECK(has(searchAliasesIn("Clip", "Tool > Cut"), "trim"));
    CHECK(has(searchAliasesIn("Add Level"), "multires"));
    CHECK(has(searchAliasesIn("Show face groups"), "groups"));
    CHECK(searchAliasesIn("Partial redo").empty());       // whole words only
    CHECK(searchAliasesIn("Dynamic stroke spacing").empty()); // "Dynamic" only as the whole label
    // Brushes: SculptForge's stored names; the old ones still load (core/LegacyNames.h).
    BrushType t = BrushType::Standard;
    CHECK(std::string(brushName(BrushType::SharpCrease)) == "SharpCrease");
    CHECK(std::string(brushLabel(BrushType::SharpCrease)) == "Sharp Crease");
    CHECK(brushFromName("SharpCrease", t) && t == BrushType::SharpCrease);
    CHECK(brushFromName("sharp crease", t) && t == BrushType::SharpCrease);
    CHECK(brushFromName("Clay Block", t) && t == BrushType::ClayBlock);
    CHECK(brushFromName("snakehook", t) && t == BrushType::SnakeHook);
    CHECK(!brushFromName("frobnicate", t));
}

TEST(action_registry_conflicts_reset_and_key_nav) {
    ActionRegistry reg;
    reg.add({"edit.undo", "Undo", nullptr, "", "Edit", "Ctrl+Z", "", [] {}, nullptr, nullptr});
    reg.add({"brush.clay", "Clay", nullptr, "", "Brush", "Z", "", [] {}, nullptr, nullptr});
    reg.add({"view.pan_left", "Pan left", nullptr, "", "View", "Ctrl+Numpad 4", "", [] {}, nullptr, nullptr});
    reg.add({"view.zoom_in", "Zoom in", nullptr, "", "View", "Numpad Plus", "", [] {}, nullptr, nullptr});
    Chord z, ctrlZ, shiftZ;
    CHECK(parseChord("Z", z) && parseChord("Ctrl+Z", ctrlZ) && parseChord("Shift+Z", shiftZ));
    // Modifiers count: Z is Clay's, Ctrl+Z is Undo's, Shift+Z nobody's.
    auto h = reg.holdersOf(z, "view.pan_left");
    CHECK(h.size() == 1 && h[0]->id == "brush.clay");
    h = reg.holdersOf(ctrlZ, "view.pan_left");
    CHECK(h.size() == 1 && h[0]->id == "edit.undo");
    CHECK(reg.holdersOf(shiftZ, "view.pan_left").empty());
    CHECK(reg.holdersOf(z, "brush.clay").empty()); // its own key is not a conflict

    // Replace: the other loses it; per-shortcut reset gives it back and takes it from the new holder.
    CHECK(reg.assignShortcut("view.pan_left", "Z") == "Clay");
    CHECK(reg.shortcutText("brush.clay").empty() && reg.hasOverride("view.pan_left"));
    CHECK(reg.resetShortcut("brush.clay") == "Pan left");
    CHECK(reg.shortcutText("brush.clay") == "Z" && !reg.hasOverride("brush.clay"));
    CHECK(reg.shortcutText("view.pan_left").empty());
    CHECK(reg.resetShortcut("view.pan_left").empty());
    CHECK(reg.shortcutText("view.pan_left") == "Ctrl+Numpad 4" && !reg.hasOverride("view.pan_left"));

    // Pan and zoom keys: directions, and nothing for other actions.
    float px, py, zoom;
    CHECK(keyNavMotion(reg.find("view.pan_left"), px, py, zoom) && px < 0 && py == 0 && zoom == 0);
    CHECK(keyNavMotion(reg.find("view.zoom_in"), px, py, zoom) && zoom > 0 && px == 0);
    CHECK(!keyNavMotion(reg.find("brush.clay"), px, py, zoom));
    CHECK(px == 0 && py == 0 && zoom == 0);
}
