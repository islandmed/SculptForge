// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The Linux build's SDL must carry both video drivers: native Wayland (preferred) and X11. SDL quietly leaves
// Wayland out when its headers are missing at build time, and the app would then run under XWayland.
#include "TestHarness.h"

#if defined(SF_TEST_ACTIONS) && defined(__linux__)
#include <SDL3/SDL.h>

#include <string>

TEST(linux_sdl_has_wayland_and_x11_drivers) {
    bool wayland = false, x11 = false;
    for (int i = 0; i < SDL_GetNumVideoDrivers(); ++i) {
        const std::string name = SDL_GetVideoDriver(i);
        wayland |= name == "wayland";
        x11 |= name == "x11";
    }
    CHECK(wayland);
    CHECK(x11);
}
#endif
