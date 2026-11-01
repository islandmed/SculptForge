// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// One registry of user-facing actions drives the shelves, palettes, Ctrl-hover
// help, the command palette, keyboard shortcuts and UI customization. Every
// CommandRegistry command without required parameters is also registered as
// "cmd.<name>", so anything scriptable is reachable from the UI.
#pragma once

#include "core/Json.h"

#include <SDL3/SDL.h>

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace sf {

class CommandRegistry;

struct Action {
    std::string id;          // "geometry.subdivide", "ui.lightbox", "brush.clay", "view.wireframe"
    std::string label;       // "Subdivide"
    const char* icon = nullptr; // ICON_LC_* or nullptr
    std::string description; // Ctrl-hover help; defaults to the command's description
    std::string palette;     // "Tool > Geometry" (help footer, command palette)
    std::string shortcut;    // default chord: "Ctrl+D", "Shift+F", ","; "" = none
    std::string command;     // CommandRegistry name when the action runs a command
    std::function<void()> run;
    std::function<bool()> checked; // toggles; null = not a toggle
    std::function<bool()> enabled; // null = always
};

// A key chord: "Ctrl+Shift+Z", "Shift+F", ",", "F3". Ctrl also matches Cmd (macOS).
struct Chord {
    SDL_Keycode key = SDLK_UNKNOWN;
    bool ctrl = false, shift = false, alt = false;
    bool operator==(const Chord& o) const { return key == o.key && ctrl == o.ctrl && shift == o.shift && alt == o.alt; }
};
bool parseChord(std::string_view text, Chord& out);
// An action may have several chords, separated by " / " ("Ctrl+Shift+Z / Ctrl+Y").
std::vector<Chord> parseChords(std::string_view text);
std::string chordText(const Chord& chord); // canonical spelling, e.g. "Ctrl+Shift+Z"
Chord chordFromEvent(SDL_Keycode key, SDL_Keymod mods);
bool isModifierKey(SDL_Keycode key);
// The individual keys of a chord, for drawing keycaps: "Ctrl+Shift+Z" -> {"Ctrl", "Shift", "Z"}.
std::vector<std::string> chordKeys(std::string_view chord);

class ActionRegistry {
public:
    void add(Action a); // replaces by id
    void removePrefix(std::string_view prefix); // every action whose id starts with PREFIX (macros that went away)
    const Action* find(std::string_view id) const;
    const std::vector<Action>& all() const { return actions_; }
    // Runs the action bound to this chord; true if consumed. Callers skip it while ImGui wants the keyboard.
    bool handleKey(SDL_Keycode key, SDL_Keymod mods);
    std::string shortcutText(std::string_view id) const; // effective chord after user overrides
    void setShortcut(std::string_view id, std::string chord); // user override ("" clears)
    // Changes an action's default chord (Preferences > Input switches between the ZBrush and SculptForge
    // defaults); user overrides still win.
    void setDefaultShortcut(std::string_view id, std::string chord);
    Json overridesToJson() const;
    void overridesFromJson(const Json& j);
    void touch(std::string_view id); // most-recently-used, for the command palette
    const std::vector<std::string>& recent() const { return recent_; }
    void setRecent(std::vector<std::string> ids) { recent_ = std::move(ids); }

    // Like setShortcut, but takes CHORD away from any other action that has it (ZBrush reassigns
    // on conflict). Returns the label of the action that lost it, or "".
    // keepOthers: add CHORD to the action's chords instead of replacing them (an imported keymap's second key).
    std::string assignShortcut(std::string_view id, const std::string& chord, bool keepOthers = false);
    bool hasOverride(std::string_view id) const;
    void clearOverrides();
    // The action that CHORD runs (enabled or not), or nullptr.
    const Action* actionForChord(const Chord& chord) const;
    // Every command becomes "cmd.<name>". Commands with required parameters are listed too: `prefill`
    // is called for them (the console opens with the name typed), `run` for the others. Commands that
    // another action already runs (Action::command) are skipped by the command palette, not here.
    void registerCommands(const CommandRegistry& commands, std::function<void(const std::string&)> run,
                          std::function<void(const std::string&)> prefill);
    // Bumped whenever actions or shortcuts change (lets views cache derived data).
    unsigned revision() const { return revision_; }

private:
    const std::vector<Chord>& chordsOf(size_t index) const;
    std::vector<Action> actions_;
    std::vector<std::pair<std::string, std::string>> overrides_; // id -> chord
    std::vector<std::string> recent_;
    unsigned revision_ = 0;
    mutable unsigned chordsRevision_ = ~0u;
    mutable std::vector<std::vector<Chord>> chords_; // parsed effective chords, per action
};

} // namespace sf
