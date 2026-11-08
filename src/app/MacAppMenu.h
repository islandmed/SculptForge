// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// SculptForge > Preferences... in the macOS app menu (#111). SDL builds that menu with the item greyed out
// (no action); install() gives it one (MacAppMenu.mm). A click only asks: the main loop opens Preferences at the
// start of its next frame, the same action Cmd+, and Edit > Preferences run. Nothing on other systems.
#pragma once

#include <atomic>
#include <cstring>

namespace sf::macmenu {

// SDL hands Cmd+, to the app (our shortcut opens Preferences) before the menu sees it as the item's key
// equivalent, so the item answers only clicks (and Return in a menu opened from the keyboard), not that key.
inline bool isOwnKeyEquivalent(bool keyDown, bool command, const char* chars) {
    return keyDown && command && chars && std::strcmp(chars, ",") == 0;
}

inline std::atomic<bool> preferencesWanted{false};

inline void requestPreferences() { preferencesWanted.store(true); }
inline bool takePreferencesRequest() { return preferencesWanted.exchange(false); } // true once per request

#if defined(__APPLE__)
void install(); // after SDL_Init: gives SDL's Preferences... item its action
#else
inline void install() {}
#endif

} // namespace sf::macmenu
