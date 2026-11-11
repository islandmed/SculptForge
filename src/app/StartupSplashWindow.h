// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The start-up splash's window (StartupSplash.h has the decisions and the picture): a small borderless window drawn in
// software (no OpenGL: the main window's context is being made on the same thread), kept alive by pumping events.
#pragma once

#include <string>

struct SDL_Window;

namespace sf::splash {

class Window {
public:
    ~Window() { close(); }
    bool open(const std::string& versionText); // false: no window system, or it said no; start-up goes on without
    void pump();                                // between start-up steps, so the system sees the window respond
    void close();
    bool isOpen() const { return window_ != nullptr; }

private:
    SDL_Window* window_ = nullptr;
};

// The splash as a PNG at width x height (--splash-shot FILE --size WxH): no window, for checking it by eye.
bool writeShot(const std::string& path, int width, int height, const std::string& versionText, std::string& error);

} // namespace sf::splash
