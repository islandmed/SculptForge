// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// OsModifiers.h: which modifier keys the system says are down.
#include "app/OsModifiers.h"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__APPLE__)
#include "app/MacAppMenu.h"
#endif

namespace sf {

int osModifiers() {
#if defined(_WIN32)
    // The physical state (not the thread's message-queue view, which is what SDL's events already are). Either side counts.
    auto down = [](int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; };
    return (down(VK_SHIFT) ? kOsModShift : 0) | (down(VK_CONTROL) ? kOsModCtrl : 0) | (down(VK_MENU) ? kOsModAlt : 0) |
           ((down(VK_LWIN) || down(VK_RWIN)) ? kOsModCmd : 0);
#elif defined(__APPLE__)
    return macmenu::osModifierFlags();
#else
    return -1;
#endif
}

} // namespace sf
