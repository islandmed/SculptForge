// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// One registry of user-facing actions drives the shelves, palettes, Ctrl-hover
// help, the command palette, keyboard shortcuts and UI customization. Every
// CommandRegistry command without required parameters is also registered as
// "cmd.<name>", so anything scriptable is reachable from the UI.
#pragma once

#include <algorithm>
#include <cmath>

#include "core/Json.h"

#include <SDL3/SDL.h>

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace sf {

class CommandRegistry;

struct Action {
    std::string id;          // "geometry.subdivide", "ui.Library", "brush.clay", "view.wireframe"
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

// Two keys in a row (Artizmo on Discord): "A then S". Two different keys, never the same key twice; each step is a
// chord, so "Ctrl+K then C" works too. In an action's shortcut text a sequence is one more alternative ("A then S / F5").
// The second key must come within this long of the first, whether the first is already up (tap, tap) or still held
// (the two pressed together, in either order: the key that ends a sequence waits too, for the key that begins it). Preferences > Input > Two-key window sets it between kKeySequenceMin and Max.
constexpr float kKeySequenceSeconds = 0.4f, kKeySequenceMin = 0.35f, kKeySequenceMax = 1.0f;
constexpr std::string_view kThen = " then ";
struct KeySequence {
    Chord first, second;
    bool operator==(const KeySequence& o) const { return first == o.first && second == o.second; }
};
bool parseSequence(std::string_view text, KeySequence& out); // false for a single chord, or for the same key twice
std::string sequenceText(const KeySequence& sequence);       // "A then S"
// The steps of one alternative for drawing keycaps: "A then S" -> {"A", "S"}, "Ctrl+Z" -> {"Ctrl+Z"}.
std::vector<std::string> shortcutSteps(std::string_view alternative);
// One alternative ("ctrl+z", "a then s") spelled canonically, or "" if it isn't a chord or a sequence.
std::string canonicalShortcut(std::string_view alternative);
// Every alternative of an action's shortcut text (single chords and sequences), canonical, unreadable ones dropped.
std::vector<std::string> shortcutAlternatives(std::string_view text);

// -1 for Smaller brush (and by 1 px), +1 for Larger brush (and by 1 px), 0 otherwise: these repeat while their
// key is held, and step through an insert set, on whatever keys they are bound to.
int brushSizeStep(const Action* a);

// The view actions that move the camera while their key is held (Pan left/right/up/down, Zoom in/out): the
// direction as pixels per second of panning at speed 1 and zoom steps per second, or false for any other action.
bool keyNavMotion(const Action* a, float& panX, float& panY, float& zoomSteps);
constexpr float kKeyNavPanPixels = 600.0f, kKeyNavZoomSteps = 4.0f; // per second at speed 1.0 (Preferences > Input)
constexpr float kKeyNavTapSeconds = 0.1f;  // a press counts as held at least this long: a tap moves about a tenth of a second's worth
constexpr float kKeyNavEaseSeconds = 0.12f; // from standing to full speed on a press, and back to standing on a release

// One pan / zoom key's speed, 0 (standing) to 1 (full), easing linearly over kKeyNavEaseSeconds. advance() moves it
// by one frame and returns the seconds of full-speed travel that frame covers (the exact integral of the speed over
// the frame, so the distance after any time is the same at 30 fps and at 144 fps). No OS key repeat is involved: the
// caller says whether the key is down.
struct KeyNavEase {
    float level = 0.0f;
    float advance(bool held, float dt) {
        if (dt <= 0.0f) return 0.0f;
        const float target = held ? 1.0f : 0.0f;
        const float rate = 1.0f / kKeyNavEaseSeconds;
        const float toTarget = std::abs(target - level) / rate; // seconds until it gets there
        if (dt <= toTarget) {
            const float end = level + (held ? rate * dt : -rate * dt);
            const float area = dt * (level + end) * 0.5f;
            level = end;
            return area;
        }
        const float area = toTarget * (level + target) * 0.5f + (held ? dt - toTarget : 0.0f);
        level = target;
        return area;
    }
};

class ActionRegistry {
public:
    void add(Action a); // replaces by id
    void removePrefix(std::string_view prefix); // every action whose id starts with PREFIX (macros that went away)
    const Action* find(std::string_view id) const;
    const std::vector<Action>& all() const { return actions_; }
    // Runs the action bound to this chord; true if consumed. Callers skip it while ImGui wants the keyboard.
    // A key that starts a sequence waits for the second key: it is consumed, and if it is bound alone that action runs
    // when no second key comes in time (tick) or when another key that isn't the second one is pressed.
    // REPEAT: an OS key repeat of the key waiting for its second key changes nothing (it is not a new press).
    bool handleKey(SDL_Keycode key, SDL_Keymod mods, bool repeat = false);
    void setSequenceWindow(float seconds) { window_ = std::clamp(seconds, kKeySequenceMin, kKeySequenceMax); }
    float sequenceWindow() const { return window_; }
    // Every key-up: a waiting key let go is no longer "held", so a key that only ends a sequence (V of "C then V") pressed
    // before C counts as the two pressed together only while it is still down.
    void keyUp(SDL_Keycode key) { if (pending_.on && pending_.first.key == key) pending_.down = false; }
    // Called with the key and true just before the action it waited with runs alone, and false right after (the app
    // tells a pie menu whether that key is still down: a hold-open menu on a key that begins a two-key shortcut opens
    // late, after the window).
    void setAloneHook(std::function<void(SDL_Keycode, bool)> hook) { aloneHook_ = std::move(hook); }
    bool isSequenceStartKey(SDL_Keycode key, SDL_Keymod mods) const { return isSequenceStart(chordFromEvent(key, mods)); }
    // Once a frame, DT seconds: runs a first key that waited its kKeySequenceSeconds out. The registry counts time
    // from these (no wall clock: scripted tests and the app step it the same way).
    void tick(float dt);
    bool sequenceWaiting() const { return pending_.on; } // the app keeps drawing frames until it is over
    void flushSequence() { flushPending(); } // a click or pen down: the first key waiting runs alone now
    void dropSequence() { pending_ = {}; }   // focus lost: forget it, nothing runs
    std::string shortcutText(std::string_view id) const; // effective chord after user overrides
    void setShortcut(std::string_view id, std::string chord); // user override ("" clears)
    // Changes an action's default chord (Preferences > Input switches between the classic and SculptForge
    // defaults); user overrides still win. Remembered for an action added later (#147: the style is applied
    // before every action is registered, and add() would otherwise bring back the action's own default).
    void setDefaultShortcut(std::string_view id, std::string chord);
    Json overridesToJson() const;
    void overridesFromJson(const Json& j);
    void touch(std::string_view id); // most-recently-used, for the command palette
    const std::vector<std::string>& recent() const { return recent_; }
    void setRecent(std::vector<std::string> ids) { recent_ = std::move(ids); }

    // Like setShortcut, but takes CHORD away from any other action that has it (reassigns
    // on conflict). Returns the label of the action that lost it, or "".
    // keepOthers: add CHORD to the action's chords instead of replacing them (an imported keymap's second key).
    std::string assignShortcut(std::string_view id, const std::string& chord, bool keepOthers = false);
    bool hasOverride(std::string_view id) const;
    // The other actions that already use CHORD (exactly: Ctrl+Z is not Z), for the warning before a key is reassigned.
    std::vector<const Action*> holdersOf(const Chord& chord, std::string_view exceptId) const;
    // The same for one alternative in any spelling: a chord or a sequence ("A then S" is taken only by the same sequence;
    // A alone, bound elsewhere, still works as its own key).
    std::vector<const Action*> holdersOfShortcut(std::string_view alternative, std::string_view exceptId) const;
    // The action a two-key sequence runs (enabled or not), or nullptr.
    const Action* actionForSequence(const KeySequence& sequence) const;
    // Back to the action's default chord (the key style's); any action that holds it now loses it. Returns the
    // labels of those actions, ", " separated, or "".
    std::string resetShortcut(std::string_view id);
    void clearOverrides();
    // The action that CHORD runs (enabled or not), or nullptr.
    const Action* actionForChord(const Chord& chord) const;
    // The action a key press runs, as handleKey finds it (an unbound Shift or Alt chord falls back to the plain key).
    const Action* actionForKey(SDL_Keycode key, SDL_Keymod mods) const;
    // Every command becomes "cmd.<name>". Commands with required parameters are listed too: `prefill`
    // is called for them (the console opens with the name typed), `run` for the others. Commands that
    // another action already runs (Action::command) are skipped by the command palette, not here.
    void registerCommands(const CommandRegistry& commands, std::function<void(const std::string&)> run,
                          std::function<void(const std::string&)> prefill);
    // Bumped whenever actions or shortcuts change (lets views cache derived data).
    unsigned revision() const { return revision_; }

private:
    const std::vector<Chord>& chordsOf(size_t index) const;
    void rebuildChords() const;
    bool isSequenceStart(const Chord& chord) const;
    bool isSequenceEnd(const Chord& chord) const;
    bool runAction(const Action* a); // the enabled check and a copy of run (running may register actions)
    void flushPending();             // the first key that waited alone: its own action now, if it has one
    std::vector<Action> actions_;
    std::vector<std::pair<std::string, std::string>> overrides_; // id -> chord
    std::vector<std::pair<std::string, std::string>> styleDefaults_; // id -> chord, from setDefaultShortcut
    std::vector<std::string> recent_;
    unsigned revision_ = 0;
    mutable unsigned chordsRevision_ = ~0u;
    mutable std::vector<std::vector<Chord>> chords_; // parsed effective chords, per action
    mutable std::vector<std::vector<KeySequence>> sequences_; // parsed effective two-key sequences, per action
    struct Pending {
        bool on = false;
        Chord first;              // the key waiting (the sequence's first, or its second when pressed before the first)
        bool down = true;         // still held
        double until = 0.0;       // clock_ time the wait ends
        std::string aloneId;      // the action the first key runs alone ("" = none)
    } pending_;
    double clock_ = 0.0;
    float window_ = kKeySequenceSeconds;
    std::function<void(SDL_Keycode, bool)> aloneHook_;
};

} // namespace sf
