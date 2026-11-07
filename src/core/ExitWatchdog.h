// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Quitting always ends the process. Armed once the window's loop has ended (the user's work is saved by then): if
// the process is still alive after the timeout (a graphics driver, a thread or a library stuck in the teardown or in
// the exit itself), it ends at once with the exit code, running nothing else (Windows: TerminateProcess, so no DLL
// detach can hang it; elsewhere _exit). A process left behind kept "Playing SculptForge" on Discord (@ebarranco).
#pragma once

#include <functional>

namespace sf {

// Starts a detached thread that waits timeoutMs, then calls onTimeout (tests) or ends the process with code.
// Nothing else changes: a normal exit before then just ends the thread with the process.
void armExitWatchdog(int timeoutMs, int code, std::function<void()> onTimeout = {});

} // namespace sf
