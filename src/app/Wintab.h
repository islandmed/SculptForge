// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Pen pressure through Wintab on Windows, for tablets whose driver doesn't use Windows Ink (Wacom Tablet
// Properties > Mapping > "Use Windows Ink" off, common with older Intuos and with Wacom Bridge setups). SDL3 reads
// pens only through Windows Ink; without it the driver moves the mouse and the pressure is lost. This reads just the
// pressure, the tip and the eraser from the driver's Wintab32.dll (System32 only, no files of ours); the mouse events
// keep doing the rest. The app uses it only while Windows Ink is silent (Tablet.cpp). Elsewhere it does nothing.
#pragma once

#include <SDL3/SDL.h>

namespace sf {

class WintabPressure {
public:
    struct Sample {
        float pressure = 0.0f; // 0..1
        bool tip = false;      // touching
        bool eraser = false;   // the eraser end
    };

    WintabPressure() = default;
    WintabPressure(const WintabPressure&) = delete;
    WintabPressure& operator=(const WintabPressure&) = delete;
    ~WintabPressure() { close(); }

    bool open(SDL_Window* window); // true once a context is open (again: harmless); false without a Wintab driver
    void close();
    bool isOpen() const { return ctx_ != nullptr; }
    // The newest packet since the last call (false: none came). Cheap: called on every mouse event.
    bool poll(Sample& out);

private:
    void* lib_ = nullptr;
    void* ctx_ = nullptr;
    long pressureMax_ = 1023;
};

} // namespace sf
