// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The warm UI test server: sculptforge --ui-serve [--size WxH] [--threads N] makes its hidden window, OpenGL context and SDL once,
// then runs UI test scripts one after another, each a whole run of the app (main's own code, a new App, its own
// isolated settings folder) in that window. Starting a process, SDL, a window and a GL context for every script is
// most of a short UI test's time. tools/test/warm_ui.py keeps a pool of them for ctest.
//
// Requests come on standard input only (no port, no socket: nothing another user or program can reach), one per
// line, fields separated by tabs:
//   run <TAB> LOG [<TAB> -e <TAB> NAME=VALUE]... [<TAB> -C <TAB> DIR] <TAB> ARGS...   e.g. --ui-test x.sfui --ui-out d
//   quit
// The script's standard output and error go to LOG; the answer, on standard output, is "done <TAB> EXIT <TAB> MS".
// The server starts with "ready". Between scripts it puts back what a script may change outside its App: the
// environment, the working directory, the clipboard, queued events, rand()'s seed and the globals below. A script
// that changes something else for good says `# cold` and runs in a process of its own (warm_ui.py).
#pragma once

namespace sf {

int runUiServer(int argc, char** argv, int (*runOne)(int argc, char** argv));
// SDL keeps a copy of the environment, made when it starts (SDL_getenv, SDL_GetPrefPath's XDG folders): App::init
// makes the server's copy what a new process's SDL_Init would see now (isolateSettings' folders, the test's -e).
void syncSdlEnvironment();

// What the app keeps outside its App between runs, put back as a new process has it.
void resetConfigGlobals(); // Config.cpp: filter fields, pending file dialogs
void resetDemoGlobals();   // Demo.cpp: the items drawn last frame
void resetPickerGlobals(); // Pickers.cpp: recent brushes, the palette filter
namespace ui {
void resetUiGlobals(); // Ui.cpp: hover rings, animations, drag scrolling
}

} // namespace sf
