// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The benchmark video (proof mode, ProofMode.cpp; benchmark takes, BenchTake.cpp): each frame about to be shown is
// read back asynchronously through a ring of pixel buffers and handed to ffmpeg on a thread of its own, which
// writes a constant 30 fps in real time (a frame repeats while the app is slower; frames drop when the encoder is
// behind). The HUD is part of the frame, so it is in the video.
#pragma once

#include "ai/VideoEncoder.h"
#include "render/GL.h"

#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace sf {

class BenchVideo {
public:
    static constexpr int kFps = 30;
    // Starts ffmpeg for frames of w x h (even sizes); false when it can't: error() says why (ffmpeg missing, a folder
    // that can't be written ...).
    bool start(const std::string& path, int w, int h);
    // Why start or finish failed ("" when they did not): one sentence, already given to the log.
    const std::string& error() const { return error_; }
    // The default framebuffer, about to be shown at `t` (seconds): read into the ring; the oldest read goes to the
    // encoder. A frame of another size than start's is skipped.
    void grab(double t, int w, int h);
    bool finish(); // the frames still being read included; false when nothing was written or ffmpeg failed
    bool running() const { return started_; }
    ~BenchVideo();

private:
    void push(double t, std::vector<uint8_t>&& rgba, bool wait = false);
    void loop();
    ai::VideoEncoder encoder_;
    bool started_ = false;
    std::string error_;
    int w_ = 0, h_ = 0;
    std::thread thread_;
    std::mutex m_;
    std::condition_variable cv_;
    std::deque<std::pair<double, std::vector<uint8_t>>> queue_;
    bool stop_ = false;
    long written_ = 0;
    GLuint pbo_[3] = {0, 0, 0};
    double pboTime_[3] = {0, 0, 0};
    int pboNext_ = 0, pboFilled_ = 0;
};

} // namespace sf
