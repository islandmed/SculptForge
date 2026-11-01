// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The log file: sculptforge.log in the user data folder (SDL_GetPrefPath: %APPDATA%\SculptForge\SculptForge on
// Windows, ~/Library/Application Support/SculptForge/SculptForge on macOS, ~/.local/share/SculptForge/SculptForge
// on Linux). It holds what SDL_Log says (the Windows app has no console to say it on), the commands run, and, when
// SculptForge crashes, what crashed and after which command. Each start keeps the last session's log as
// sculptforge-previous.log and notes there if that session ended without closing (a crash no handler can catch,
// such as a stack overrun on Windows, still leaves its last command there). Lines are flushed as written.
#pragma once

#include <string>

namespace sf::crashlog {

// Opens the log in `folder` (empty: the user data folder) and installs the crash handlers. Safe to call once.
void start(const std::string& folder = {});
// A line in the log (with the time).
void note(const std::string& line);
// Something about to be done (a command): logged before it runs, so even a crash no handler sees leaves it as the
// log's last line, and named by a crash note. A repeat of the last one isn't written again.
void action(const std::string& what);
// For --crash-test: no system crash dialog after the note (Windows Error Reporting would wait for a click), and on
// Linux no core dump (systemd-coredump would keep one and notify the desktop on every test run).
void quietSystemCrashDialog();
// The session ended normally (the next start doesn't call it a crash).
void stop();
std::string path();         // the log file ("" before start or when it couldn't be opened)
std::string previousPath(); // the last session's log
std::string folderUrl();    // file:// URL of the log's folder, for SDL_OpenURL ("" without a log)
// True when the last session ended without stop() (its log said so; see sculptforge-previous.log).
bool previousCrashed();

} // namespace sf::crashlog
