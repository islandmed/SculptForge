// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The one way the app opens a system dialog (file, folder, message box) or hands a URL to the desktop (browser,
// file manager). tests/check_native_dialogs.py fails the build's
// tests if any other source calls SDL_Show*Dialog, SDL_Show*MessageBox or SDL_OpenURL.
//
// No native dialog ever opens under a UI test (--ui-test / --ui-serve scripts), the offscreen or dummy video driver
// (agents, CI) or SF_NO_DIALOGS=1: SDL's portal dialogs open on the desktop even with no window of ours there. The
// request is logged ("dialog requested: open file") and counted (UI tests: dialogs_requested, dialog_kind), and the
// callback gets the path a script queued (dialog_answer PATH) or a cancel.
#pragma once

#include <SDL3/SDL.h>

#include <string>

namespace sf::dialogs {

enum class Kind { None, OpenFile, SaveFile, OpenFolder, Message, Url };

void openFile(SDL_DialogFileCallback callback, void* userdata, SDL_Window* window, const SDL_DialogFileFilter* filters,
              int nfilters, const char* defaultLocation, bool allowMany);
void saveFile(SDL_DialogFileCallback callback, void* userdata, SDL_Window* window, const SDL_DialogFileFilter* filters,
              int nfilters, const char* defaultLocation);
void openFolder(SDL_DialogFileCallback callback, void* userdata, SDL_Window* window, const char* defaultLocation,
                bool allowMany);
bool message(SDL_MessageBoxFlags flags, const char* title, const char* text, SDL_Window* window);
bool openUrl(const char* url); // false when suppressed (callers say it couldn't open), as SDL_OpenURL

bool suppressed();           // no native dialog now (see above)
void setScripted(bool on);   // a UI test script is running (Demo::loadScript)
void answerNext(std::string path); // the next suppressed dialog answers PATH ("" = cancel)
int requested();             // suppressed requests so far
Kind lastKind();

} // namespace sf::dialogs
