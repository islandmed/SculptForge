// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Help > Record a performance trace (core/PerfTrace.h, #127): nothing written when off, lines after the header
// when on, appending to an earlier recording, and two small files at most.
#include "TestHarness.h"

#include "core/PerfTrace.h"

#include <filesystem>
#include <fstream>
#include <iterator>

using namespace sf;
namespace fs = std::filesystem;

namespace {

fs::path freshFolder(const char* name) {
    const fs::path dir = fs::temp_directory_path() / (std::string("sculptforge_perftrace_") + name);
    fs::remove_all(dir);
    return dir;
}

std::string readFile(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), {});
}

} // namespace

TEST(perf_trace_off_writes_nothing_on_writes_lines) {
    const fs::path dir = freshFolder("onoff");
    CHECK(!perftrace::on());
    perftrace::line("before"); // off: no file, no cost
    CHECK(!fs::exists(dir / "sculptforge-trace.log"));
    CHECK(perftrace::start(dir.string(), "HEADER gpu"));
    CHECK(perftrace::on());
    CHECK(perftrace::path() == (dir / "sculptforge-trace.log").string());
    perftrace::line("long frame 48.0 ms");
    perftrace::stop();
    CHECK(!perftrace::on());
    perftrace::line("after");
    std::string text = readFile(dir / "sculptforge-trace.log");
    CHECK(text.rfind("HEADER gpu\n", 0) == 0);
    CHECK(text.find("long frame 48.0 ms") != std::string::npos);
    CHECK(text.find("recording stopped") != std::string::npos);
    CHECK(text.find("before") == std::string::npos && text.find("after") == std::string::npos);
    // A second recording appends (one file for the whole report).
    CHECK(perftrace::start(dir.string(), "HEADER again"));
    perftrace::line("second");
    perftrace::stop();
    text = readFile(dir / "sculptforge-trace.log");
    CHECK(text.find("long frame 48.0 ms") != std::string::npos && text.find("second") != std::string::npos);
    fs::remove_all(dir);
}

TEST(perf_trace_rotates_at_the_size_limit) {
    const fs::path dir = freshFolder("rotate");
    CHECK(perftrace::start(dir.string(), "HEADER"));
    const std::string row(200, 'x');
    for (size_t written = 0; written < perftrace::kMaxBytes * 3; written += row.size()) perftrace::line(row);
    perftrace::stop();
    const fs::path current = dir / "sculptforge-trace.log", previous = dir / "sculptforge-trace-previous.log";
    CHECK(fs::exists(previous));
    CHECK(fs::file_size(current) <= perftrace::kMaxBytes + 512);
    CHECK(fs::file_size(previous) <= perftrace::kMaxBytes + 512);
    CHECK(readFile(current).rfind("HEADER (continued", 0) == 0); // every file starts with the GPU and driver
    fs::remove_all(dir);
}
