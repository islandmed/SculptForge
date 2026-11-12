// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The start-up splash's window (StartupSplash.h has the decisions and the picture): a small borderless, centred, floating
// window, drawn at the display's pixel size, kept alive by pumping events. Three ways to put the picture on it, tried in this
// order: the window's own surface (X11, Windows, macOS), then an SDL renderer on the window (Wayland has no window surface
// for some builds and compositors), then SDL's software renderer. Every way is tried before the window is shown, so a window
// system that says no costs no flash of an empty window.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct SDL_Renderer;
struct SDL_Texture;
struct SDL_Window;

namespace sf::splash {

enum class Path { Auto, Surface, Renderer, Software }; // Auto: all of them in that order (SF_SPLASH_PATH=surface|renderer|software picks one)
Path pathFromEnv(const char* value);                       // nullptr or anything unknown: Auto

class Window {
public:
    ~Window() { close(); }
    bool open(const std::string& versionText, Path path = Path::Auto); // false: no window system, or it said no; start-up goes on without
    void pump();                                // between start-up steps, so the system sees the window respond
    void fadeOut(double ms);                    // where the window system can fade a window (not Wayland): a short fade, then close() as usual
    void close();
    bool isOpen() const { return window_ != nullptr; }
    Path usedPath() const { return used_; }     // Surface, Renderer or Software once open
    // The splash as the window shows it, read back (RGBA, pixels): for the self-test. False on the surface path.
    bool readBack(std::vector<uint8_t>& rgba, int& width, int& height) const;

private:
    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    SDL_Texture* texture_ = nullptr;
    Path used_ = Path::Auto;
};

// --splash-selftest: opens the window by every path the window system allows, checks the picture is on it, prints one line each.
// 0: every path that opened showed the right picture and at least one path opened.
int selfTest();

// The splash as a PNG at width x height (--splash-shot FILE --size WxH): no window, for checking it by eye.
bool writeShot(const std::string& path, int width, int height, const std::string& versionText, std::string& error);

} // namespace sf::splash
