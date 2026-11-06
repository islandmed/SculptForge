// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// A big project being opened (App::opening_, PanelProject.cpp): its preview, and the thread reading the rest.
#pragma once

#include "app/App.h"
#include "core/Project.h"

#include <atomic>
#include <string>
#include <thread>

namespace sf {

struct App::Opening {
    std::string path;
    Mesh preview;
    std::thread reader;
    std::atomic<bool> done{false};
    LoadedProjectPtr loaded{nullptr, [](LoadedProject* p) { delete p; }};
    std::string error;
    ~Opening() {
        if (reader.joinable()) reader.join();
    }
};

} // namespace sf
