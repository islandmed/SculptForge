// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Held modifier keys, re-synced from the system. SDL tracks Shift / Ctrl / Alt / Cmd from the key events it sees, and
// the sculpting reads that state live (SDL_GetModState). A key-up that never arrives while the window keeps focus (a
// native macOS menu or panel took it) leaves the modifier "held" for good: strokes smooth, mask or select, the Alt
// toggle never lets go. SDL already releases every key when focus is lost (SDL_ResetKeyboard), so the remaining gap is
// that one; macOS (NSEvent modifierFlags) and Windows (GetAsyncKeyState) can say what is really down. Linux can't beyond
// what SDL's X11 / Wayland code syncs on its own: no query there. Independent of ImGui and the App, so it unit tests.
#pragma once

#include <SDL3/SDL_keycode.h>

namespace sf {

constexpr int kOsModShift = 1, kOsModCtrl = 2, kOsModAlt = 4, kOsModCmd = 8;

// The modifiers the system says are down (kOsMod bits), or -1 where it can't be asked (Linux, Android).
int osModifiers();

// `sdl` with every modifier group the system says is up cleared (left and right bits together). Only ever clears: a
// modifier SDL doesn't know about stays unknown, locks (Caps, Num) are left alone. `os` < 0: unchanged.
inline SDL_Keymod modsWithStaleCleared(SDL_Keymod sdl, int os) {
    if (os < 0) return sdl;
    unsigned out = unsigned(sdl);
    if (!(os & kOsModShift)) out &= ~unsigned(SDL_KMOD_SHIFT);
    if (!(os & kOsModCtrl)) out &= ~unsigned(SDL_KMOD_CTRL);
    if (!(os & kOsModAlt)) out &= ~unsigned(SDL_KMOD_ALT);
    if (!(os & kOsModCmd)) out &= ~unsigned(SDL_KMOD_GUI);
    return SDL_Keymod(out);
}

} // namespace sf
