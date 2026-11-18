// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The video encoder (ai/VideoEncoder.h: timelapse export, --record, the benchmark video). Invariants: every way it can
// fail ends in exactly ONE message that names the cause, given to the caller (the status bar) and once to the log;
// a stuck or dead ffmpeg is reported, never a hang; a failed run leaves no half file and keeps an existing one;
// ffmpeg is never started through a shell (paths with spaces and non-ASCII names, odd frame sizes).
// A fake ffmpeg (tests/tools/fake_ffmpeg.cpp) plays each failure; the real one, when installed, the odd sizes.
#include "TestHarness.h"

#include "ai/VideoEncoder.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>

using namespace sf::ai;
namespace fs = std::filesystem;

namespace {

void setEnv(const char* name, const std::string& value) {
#if defined(_WIN32)
    _putenv_s(name, value.c_str());
#else
    setenv(name, value.c_str(), 1);
#endif
}

// ffmpeg is the fake in `mode` for as long as this lives.
struct FakeFfmpeg {
    explicit FakeFfmpeg(const char* mode) {
        setEnv("SCULPTFORGE_FFMPEG", SF_FAKE_FFMPEG);
        setEnv("FAKE_FFMPEG_MODE", mode);
    }
    ~FakeFfmpeg() { setEnv("SCULPTFORGE_FFMPEG", ""); }
};

struct Run {
    VideoEncoderSpec spec;
    std::vector<std::string> logged;
    fs::path dir;
    Run(const char* name) {
        dir = fs::temp_directory_path() / (std::string("sf-video-encoder-") + name);
        std::error_code ec;
        fs::remove_all(dir, ec);
        fs::create_directories(dir, ec);
        spec.output = (dir / "out.mp4").string();
        spec.width = 64;
        spec.height = 48;
        spec.fps = 30;
        spec.what = "timelapse";
        spec.log = [this](const std::string& m) { logged.push_back(m); };
    }
    ~Run() {
        std::error_code ec;
        fs::remove_all(dir, ec);
    }
    size_t frameBytes() const { return size_t(spec.width) * size_t(spec.height) * 4; }
};

bool has(const std::string& text, const char* part) { return text.find(part) != std::string::npos; }

size_t filesIn(const fs::path& dir) {
    size_t n = 0;
    std::error_code ec;
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) ++n;
    return n;
}

// The one-status-one-log-line invariant: `error` is the status text, the log got exactly it, once.
void checkReportedOnce(const Run& r, const std::string& error) {
    CHECK(!error.empty());
    CHECK(r.logged.size() == 1);
    CHECK(r.logged.size() == 1 && r.logged[0] == error);
}

} // namespace

TEST(video_encoder_writes_every_frame_and_puts_the_file_in_place) {
    FakeFfmpeg fake("ok");
    Run r("ok");
    VideoEncoder enc;
    std::string error;
    CHECK(enc.start(r.spec, error));
    const std::vector<uint8_t> frame(r.frameBytes(), 1);
    for (int i = 0; i < 5; ++i) CHECK(enc.write(frame.data(), frame.size()));
    CHECK(enc.finish(error));
    CHECK(error.empty());
    CHECK(r.logged.empty());
    CHECK(enc.frames() == 5);
    CHECK(fs::file_size(r.dir / "out.mp4") == 5 * r.frameBytes());
    CHECK(filesIn(r.dir) == 1); // no .partial file left beside it
}

TEST(video_encoder_ffmpeg_not_found_says_how_to_get_it) {
    setEnv("SCULPTFORGE_FFMPEG", (fs::temp_directory_path() / "no-such-dir" / "ffmpeg").string());
    Run r("missing");
    VideoEncoder enc;
    std::string error;
    CHECK(!enc.start(r.spec, error));
    checkReportedOnce(r, error);
    CHECK(has(error, "needs ffmpeg"));
    CHECK(has(error, "wasn't found"));
    CHECK(has(error, "Install"));
    std::string second;
    CHECK(!enc.finish(second)); // asking again repeats the answer and does not log it twice
    CHECK(second == error);
    CHECK(r.logged.size() == 1);
    CHECK(!fs::exists(r.dir / "out.mp4"));
    setEnv("SCULPTFORGE_FFMPEG", "");
}

TEST(video_encoder_ffmpeg_exiting_reports_its_code_and_last_words) {
    FakeFfmpeg fake("exit1");
    Run r("exit1");
    VideoEncoder enc;
    std::string error;
    CHECK(enc.start(r.spec, error));
    const std::vector<uint8_t> frame(1 << 20, 1); // (more than a pipe holds: the writes must fail, not block)
    int accepted = 0;
    for (int i = 0; i < 50 && enc.write(frame.data(), frame.size()); ++i) ++accepted;
    CHECK(accepted < 50);
    CHECK(!enc.finish(error));
    checkReportedOnce(r, error);
    CHECK(has(error, "exited with code 1"));
    CHECK(has(error, "Unknown encoder"));
    CHECK(!fs::exists(r.dir / "out.mp4"));
    CHECK(filesIn(r.dir) == 0);
}

TEST(video_encoder_a_failed_run_keeps_the_file_that_was_there) {
    FakeFfmpeg fake("exit1");
    Run r("keep");
    { std::ofstream(r.dir / "out.mp4", std::ios::binary) << "the old timelapse"; }
    VideoEncoder enc;
    std::string error;
    CHECK(enc.start(r.spec, error));
    CHECK(!enc.finish(error));
    CHECK(r.logged.size() == 1);
    std::ifstream f(r.dir / "out.mp4", std::ios::binary);
    std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    CHECK(text == "the old timelapse");
}

TEST(video_encoder_no_frames_is_reported_not_a_silent_empty_file) {
    FakeFfmpeg fake("ok");
    Run r("zero");
    VideoEncoder enc;
    std::string error;
    CHECK(enc.start(r.spec, error));
    CHECK(!enc.finish(error));
    checkReportedOnce(r, error);
    CHECK(has(error, "recorded nothing"));
    CHECK(!fs::exists(r.dir / "out.mp4"));
}

TEST(video_encoder_an_empty_output_is_reported) {
    FakeFfmpeg fake("empty");
    Run r("empty");
    VideoEncoder enc;
    std::string error;
    CHECK(enc.start(r.spec, error));
    const std::vector<uint8_t> frame(r.frameBytes(), 1);
    CHECK(enc.write(frame.data(), frame.size()));
    CHECK(!enc.finish(error));
    checkReportedOnce(r, error);
    CHECK(has(error, "is empty"));
}

TEST(video_encoder_a_refused_write_names_the_folder_hint) {
    FakeFfmpeg fake("denied");
    Run r("denied");
    VideoEncoder enc;
    std::string error;
    CHECK(enc.start(r.spec, error));
    const std::vector<uint8_t> frame(r.frameBytes(), 1);
    enc.write(frame.data(), frame.size()); // (ffmpeg may be gone already or not: either way the exit is the cause)
    CHECK(!enc.finish(error));
    checkReportedOnce(r, error);
    CHECK(has(error, "Permission denied"));
    CHECK(has(error, "refused the write"));
}

TEST(video_encoder_a_folder_that_cannot_be_written_is_named_before_ffmpeg_starts) {
    FakeFfmpeg fake("ok");
    Run r("nowrite");
    { std::ofstream(r.dir / "a-file", std::ios::binary) << "x"; }
    r.spec.output = (r.dir / "a-file" / "sub" / "out.mp4").string(); // (a folder below a file: can't exist)
    VideoEncoder enc;
    std::string error;
    CHECK(!enc.start(r.spec, error));
    checkReportedOnce(r, error);
    CHECK(has(error, "can't be written to"));
    CHECK(has(error, "a-file"));
}

TEST(video_encoder_ffmpeg_that_stops_reading_is_stopped_not_waited_for) {
    FakeFfmpeg fake("stall");
    Run r("stall");
    r.spec.stallSeconds = 0.5;
    VideoEncoder enc;
    std::string error;
    CHECK(enc.start(r.spec, error));
    const auto t0 = std::chrono::steady_clock::now();
    const std::vector<uint8_t> frame(1 << 20, 1);
    bool stopped = false;
    for (int i = 0; i < 100 && !stopped; ++i) stopped = !enc.write(frame.data(), frame.size());
    CHECK(stopped);
    CHECK(!enc.finish(error));
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    CHECK(seconds < 20.0);
    checkReportedOnce(r, error);
    CHECK(has(error, "stopped taking frames"));
}

TEST(video_encoder_ffmpeg_that_never_finishes_is_reported) {
    FakeFfmpeg fake("hangfinish");
    Run r("hang");
    r.spec.finishSeconds = 0.5;
    VideoEncoder enc;
    std::string error;
    CHECK(enc.start(r.spec, error));
    const std::vector<uint8_t> frame(r.frameBytes(), 1);
    CHECK(enc.write(frame.data(), frame.size()));
    const auto t0 = std::chrono::steady_clock::now();
    CHECK(!enc.finish(error));
    CHECK(std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() < 20.0);
    checkReportedOnce(r, error);
    CHECK(has(error, "didn't finish"));
}

TEST(video_encoder_cancel_says_nothing_and_leaves_nothing) {
    FakeFfmpeg fake("stall");
    Run r("cancel");
    VideoEncoder enc;
    std::string error;
    CHECK(enc.start(r.spec, error));
    const std::vector<uint8_t> frame(r.frameBytes(), 1);
    CHECK(enc.write(frame.data(), frame.size()));
    enc.cancel();
    CHECK(r.logged.empty());
    CHECK(filesIn(r.dir) == 0);
}

TEST(video_encoder_a_name_without_an_extension_gets_mp4) {
    FakeFfmpeg fake("ok");
    Run r("noext");
    r.spec.output = (r.dir / "my timelapse").string();
    VideoEncoder enc;
    std::string error;
    CHECK(enc.start(r.spec, error));
    const std::vector<uint8_t> frame(r.frameBytes(), 1);
    CHECK(enc.write(frame.data(), frame.size()));
    CHECK(enc.finish(error));
    CHECK(fs::exists(r.dir / "my timelapse.mp4"));
}

#if defined(_WIN32)
// The Windows launch is CreateProcess with no console: a GUI app's export used to open an empty cmd.exe window.
TEST(video_encoder_spawns_no_console_window) {
    FakeFfmpeg fake("window");
    Run r("window");
    VideoEncoder enc;
    std::string error;
    CHECK(enc.start(r.spec, error));
    const std::vector<uint8_t> frame(r.frameBytes(), 1);
    CHECK(enc.write(frame.data(), frame.size()));
    CHECK(enc.finish(error));
    std::ifstream f(r.dir / "out.mp4", std::ios::binary);
    std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    CHECK(text == "window=0");
}
#endif

// The real ffmpeg, when there is one: odd frame sizes (yuv420p needs even ones) and a folder name with a space and
// non-ASCII letters (the user name in %USERPROFILE%) must both produce a playable file.
TEST(video_encoder_real_ffmpeg_takes_odd_sizes_and_unusual_paths) {
    setEnv("SCULPTFORGE_FFMPEG", "");
    if (findFfmpegExe().empty()) return; // (none here: the fake covers the contract)
    Run r("real");
    const fs::path odd = r.dir / fs::path(std::u8string(u8"Müller Zoë"));
    r.spec.output = (odd / "time lapse.mp4").string();
    r.spec.width = 101;
    r.spec.height = 75;
    r.spec.outputArgs = {"-vf", "crop=trunc(iw/2)*2:trunc(ih/2)*2", "-c:v", "libx264", "-preset", "ultrafast", "-pix_fmt", "yuv420p"};
    VideoEncoder enc;
    std::string error;
    CHECK(enc.start(r.spec, error));
    std::vector<uint8_t> frame(r.frameBytes());
    for (int i = 0; i < 10; ++i) {
        for (size_t p = 0; p < frame.size(); ++p) frame[p] = uint8_t((p * 7 + size_t(i) * 13) & 255);
        CHECK(enc.write(frame.data(), frame.size()));
    }
    CHECK(enc.finish(error));
    CHECK(error.empty());
    CHECK(r.logged.empty());
    CHECK(fs::exists(odd / "time lapse.mp4") && fs::file_size(odd / "time lapse.mp4") > 0);
}

TEST(video_encoder_tail_of_file_keeps_the_last_lines) {
    const std::string path = tempPath("sf-video-encoder-tail.txt");
    { std::ofstream(path, std::ios::binary) << "one\r\ntwo\n\nthree\nfour\n"; }
    CHECK(tailOfFile(path, 2) == "three / four");
    CHECK(tailOfFile(path, 10) == "one / two / three / four");
    CHECK(tailOfFile(tempPath("sf-video-encoder-no-such-file.txt")).empty());
    std::error_code ec;
    fs::remove(path, ec);
}
