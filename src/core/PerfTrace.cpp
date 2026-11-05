// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/PerfTrace.h"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <mutex>

namespace sf::perftrace {

namespace detail {
std::atomic<bool> recording{false};
}

namespace {

std::mutex g_mutex;
std::ofstream g_file;
std::string g_path, g_previous, g_header;
size_t g_bytes = 0;
std::chrono::steady_clock::time_point g_start;

std::filesystem::path fsPath(const std::string& utf8) {
    return std::filesystem::path(std::u8string(utf8.begin(), utf8.end()));
}

void writeLocked(const std::string& text) {
    g_file << text << '\n';
    g_file.flush(); // (a crash or a kill keeps what was written)
    g_bytes += text.size() + 1;
}

// The file is full: it becomes the previous one and a new one starts with the header.
void rotateLocked() {
    g_file.close();
    std::error_code ec;
    std::filesystem::remove(fsPath(g_previous), ec);
    std::filesystem::rename(fsPath(g_path), fsPath(g_previous), ec);
    g_file.open(fsPath(g_path), std::ios::binary | std::ios::trunc);
    g_bytes = 0;
    if (g_file) writeLocked(g_header + " (continued: the earlier lines are in sculptforge-trace-previous.log)");
}

} // namespace

bool start(const std::string& folder, const std::string& header) {
    std::lock_guard lock(g_mutex);
    if (detail::recording) return true;
    std::string dir = folder;
    if (!dir.empty() && dir.back() != '/' && dir.back() != '\\') dir += '/';
    g_path = dir + "sculptforge-trace.log";
    g_previous = dir + "sculptforge-trace-previous.log";
    g_header = header;
    std::error_code ec;
    if (!dir.empty()) std::filesystem::create_directories(fsPath(dir), ec);
    const auto size = std::filesystem::file_size(fsPath(g_path), ec);
    g_bytes = ec ? 0 : size_t(size);
    g_file.open(fsPath(g_path), std::ios::binary | std::ios::app);
    if (!g_file) {
        g_path.clear();
        return false;
    }
    g_start = std::chrono::steady_clock::now();
    if (g_bytes >= kMaxBytes) rotateLocked();
    else writeLocked(g_header);
    detail::recording = true;
    return true;
}

void stop() {
    std::lock_guard lock(g_mutex);
    if (!detail::recording) return;
    detail::recording = false;
    writeLocked("recording stopped");
    g_file.close();
    g_path.clear();
}

void line(const std::string& text) {
    if (!on()) return;
    std::lock_guard lock(g_mutex);
    if (!detail::recording) return;
    char at[32];
    std::snprintf(at, sizeof at, "%9.3f  ",
                  std::chrono::duration<double>(std::chrono::steady_clock::now() - g_start).count());
    writeLocked(at + text);
    if (g_bytes >= kMaxBytes) rotateLocked();
}

std::string path() {
    std::lock_guard lock(g_mutex);
    return g_path;
}

std::string previousPath() {
    std::lock_guard lock(g_mutex);
    return g_previous;
}

} // namespace sf::perftrace
