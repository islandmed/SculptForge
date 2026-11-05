// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Help > Record a performance trace (#127): while on, the app writes what makes it slow to
// sculptforge-trace.log next to sculptforge.log (Help > Open log folder): every frame longer than kLongFrameMs
// with its breakdown, each stroke's timings, the commands that took long, background jobs starting and ending,
// with the mesh size, levels and the GPU. Off by default, and off it costs one relaxed atomic load per frame.
// The file stays small: past kMaxBytes it moves to sculptforge-trace-previous.log and a new one starts (with the
// header again). A local file only: nothing here uses the network.
#pragma once

#include <atomic>
#include <cstddef>
#include <string>

namespace sf::perftrace {

constexpr double kLongFrameMs = 33.0;       // longer than two frames at 60 Hz
constexpr size_t kMaxBytes = 2 * 1024 * 1024; // per file (two files at most)

namespace detail {
extern std::atomic<bool> recording;
}
inline bool on() { return detail::recording.load(std::memory_order_relaxed); }

// Starts recording into `folder`/sculptforge-trace.log (appending to an earlier recording there); `header` (the
// app version, GPU, driver, CPU...) starts the recording and every new file. False if the file can't be written.
bool start(const std::string& folder, const std::string& header);
void stop();
// A line in the trace, after the seconds since the recording started. Does nothing when off. Thread-safe.
void line(const std::string& text);
std::string path();         // "" when not recording
std::string previousPath(); // the file the last full one moved to

} // namespace sf::perftrace
