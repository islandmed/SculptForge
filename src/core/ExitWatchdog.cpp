// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/ExitWatchdog.h"

#include <chrono>
#include <cstdio>
#include <thread>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace sf {

void armExitWatchdog(int timeoutMs, int code, std::function<void()> onTimeout) {
    std::thread([timeoutMs, code, onTimeout = std::move(onTimeout)] {
        std::this_thread::sleep_for(std::chrono::milliseconds(timeoutMs));
        if (onTimeout) return onTimeout();
        std::fprintf(stderr, "SculptForge: still running %d ms after quitting; ending it\n", timeoutMs);
        std::fflush(stderr);
#if defined(_WIN32)
        TerminateProcess(GetCurrentProcess(), UINT(code));
#endif
        _exit(code);
    }).detach();
}

bool waitWhileHeld(const std::function<bool()>& held, int maxMs, int pollMs, const std::function<void()>& pump) {
    const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(maxMs);
    for (;;) {
        if (pump) pump();
        if (!held()) return true;
        if (std::chrono::steady_clock::now() >= until) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(pollMs));
    }
}

} // namespace sf
