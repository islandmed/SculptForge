// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// A stand-in for ffmpeg as the video encoder calls it (tests/test_video_encoder.cpp): raw frames on stdin, the output
// file as the last argument. FAKE_FFMPEG_MODE picks how it behaves:
//   ok          reads every byte of stdin and writes them to the output (so the test can count them)
//   empty       reads stdin, writes a 0-byte output, exits 0
//   exit1       says "fake ffmpeg: Unknown encoder 'libx264'" on stderr and exits 1 without reading
//   denied      says "Error opening output file: Permission denied" on stderr and exits 1
//   stall       never reads stdin (a hung encoder): sleeps a minute
//   hangfinish  reads stdin to its end, then sleeps a minute (never finishes)
//   window      reads stdin; writes "window=1" or "window=0" (does this process own a console window?) to the output
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

int main(int argc, char** argv) {
#if defined(_WIN32)
    _setmode(_fileno(stdin), _O_BINARY);
#endif
    const char* modeEnv = std::getenv("FAKE_FFMPEG_MODE");
    const std::string mode = modeEnv ? modeEnv : "ok";
    const std::string out = argc > 1 ? argv[argc - 1] : "";
    auto readAll = [] {
        std::vector<char> buf(1 << 16);
        size_t total = 0;
        for (size_t n; (n = std::fread(buf.data(), 1, buf.size(), stdin)) > 0;) total += n;
        return total;
    };
    if (mode == "exit1") {
        std::fprintf(stderr, "fake ffmpeg: Unknown encoder 'libx264'\n");
        return 1;
    }
    if (mode == "denied") {
        std::fprintf(stderr, "%s: Permission denied\nError opening output file\n", out.c_str());
        return 1;
    }
    if (mode == "stall") {
        std::this_thread::sleep_for(std::chrono::seconds(60));
        return 0;
    }
    const size_t total = readAll();
    if (mode == "hangfinish") {
        std::this_thread::sleep_for(std::chrono::seconds(60));
        return 0;
    }
    std::FILE* f = std::fopen(out.c_str(), "wb");
    if (!f) return 2;
    if (mode == "ok") {
        std::vector<char> bytes(total, 7);
        std::fwrite(bytes.data(), 1, bytes.size(), f);
    } else if (mode == "window") {
#if defined(_WIN32)
        std::fputs(GetConsoleWindow() ? "window=1" : "window=0", f);
#else
        std::fputs("window=0", f);
#endif
    }
    std::fclose(f);
    return 0;
}
