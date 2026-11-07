// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Windows: Shift pressed five times (smooth, the camera keys) pops the Sticky Keys prompt over the canvas, and a held
// Shift can pop Filter Keys'. While SculptForge has focus (full screen or windowed: the prompt comes in both) the
// shortcuts that open those prompts are turned off, Microsoft's documented way for games: SystemParametersInfo
// SPI_GET/SETSTICKYKEYS, FILTERKEYS and TOGGLEKEYS, clearing only the *_HOTKEYACTIVE and *_CONFIRMHOTKEY flags,
// and only of a feature that is off. A user who has Sticky, Filter or Toggle Keys on is never touched.
//
// Nothing is written to the user's profile (never SPIF_UPDATEINIFILE): the change lives in the session only. It is
// undone when the window loses focus or is minimised, on quit, at exit, when a console is closed, and in the crash
// handlers (CrashLog.cpp). A hard kill (Task Manager) runs no handler: the flags the user had are kept in a small
// file in the user data folder while they are changed, and the next start puts them back; failing that, signing out
// does (the change was never saved).
//
// Preferences > Input > "Stop the Sticky Keys prompt while SculptForge has focus" (on by default, input.sticky_keys).
#pragma once

#include <cstdint>
#include <string>

namespace sf::stickykeys {

// The dwFlags bits these use; STICKYKEYS, FILTERKEYS and TOGGLEKEYS share them (SKF_/FKF_/TKF_ ...).
constexpr uint32_t kOn = 0x1;             // *_STICKYKEYSON / FILTERKEYSON / TOGGLEKEYSON: the feature is on
constexpr uint32_t kHotkeyActive = 0x4;   // *_HOTKEYACTIVE: the shortcut (Shift x5, Shift held 8 s, Num Lock 5 s)
constexpr uint32_t kConfirmHotkey = 0x8;  // *_CONFIRMHOTKEY: its prompt
constexpr uint32_t kHotkeyBits = kHotkeyActive | kConfirmHotkey;

struct Flags {
    uint32_t sticky = 0, filter = 0, toggle = 0;
    bool operator==(const Flags&) const = default;
};

// The flags with the shortcuts off, for each feature that is off itself (a feature that is on keeps its flags).
Flags quieted(const Flags& f);
// `now` with the shortcut bits `saved` had put back, for each feature that is off in both (the ones quieted
// changes). Only those bits: a feature the user turned on meanwhile stays as they set it.
Flags restored(const Flags& now, const Flags& saved);

// The system calls: SystemParametersInfo on Windows; a fake (scripts) or a mock (unit tests) elsewhere.
struct Spi {
    bool (*get)(Flags& out) = nullptr;
    bool (*set)(const Flags& f) = nullptr; // the session only: never SPIF_UPDATEINIFILE
};
const Spi* systemSpi(); // Windows' (nullptr elsewhere)
const Spi* fakeSpi();   // in memory, starting at Windows' defaults: UI test scripts and automated runs

// Starts the guard with `spi` (nullptr: nothing is ever changed) and the file kept while the flags are changed
// (`stateFile`, "" for none). A file left by a session that ended without putting them back (a hard kill, a
// power cut) is put back now. Installs the atexit and console close handlers (once per process).
void start(const Spi* spi, const std::string& stateFile);
// Shortcuts off (true: the preference is on and the window has focus and isn't minimised) or back (false).
// Cheap when nothing changes: the app calls it every frame.
void update(bool quiet);
// Puts the user's flags back now (quit; also the atexit and console close handlers) and removes the file.
void restore();
// The same from a crash handler: no allocation, no locks, no exceptions. True when it put flags back.
bool restoreFromCrash() noexcept;
bool isQuiet();           // the shortcuts are off now
bool recoveredAtStart();  // start() found a left file and put its flags back
Flags current();          // the flags as the Spi reports them now (scripts, tests)
void stop();              // tests and the warm UI test server: restore, then forget the Spi and the file

} // namespace sf::stickykeys
