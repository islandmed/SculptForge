// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Raw RGBA frames to an MP4 through ffmpeg: the timelapse export, --record / --ui-record and the benchmark video.
// ffmpeg is started without a shell (no cmd.exe console on Windows, no quoting of paths with spaces or non-ASCII
// names), found where installers put it (a desktop-launched app's PATH misses Homebrew's and winget's folders),
// its stderr is captured, and the file is written beside the target and put in place at the end.
//
// Every way it can fail ends in ONE message that names the cause (not found + how to get it, exited + its code and
// last words, the file can't be written + the path and the OS error, no frames), handed to the caller for the status
// bar and given once to the log sink.
#pragma once

#include "ai/Process.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace sf::ai {

struct VideoEncoderSpec {
    std::string output;            // the .mp4 to write
    int width = 0, height = 0;     // of the raw frames
    int fps = 30;
    std::string what = "video";    // for messages: "timelapse", "recording"
    std::vector<std::string> outputArgs; // between the input and the file: codec, preset, -vf ...
    std::function<void(const std::string&)> log; // gets each failure's message once (null: stderr)
    double stallSeconds = 60.0;    // ffmpeg not taking frames for this long = stopped
    double finishSeconds = 180.0;  // ffmpeg not finishing after the last frame for this long = stopped
};

class VideoEncoder {
public:
    VideoEncoder() = default;
    VideoEncoder(const VideoEncoder&) = delete;
    VideoEncoder& operator=(const VideoEncoder&) = delete;
    ~VideoEncoder();

    // False with `error` set (and logged once) when it could not start.
    bool start(const VideoEncoderSpec& spec, std::string& error);
    // One frame (width*height*4 bytes). False once ffmpeg has stopped: stop sending, call finish() for the why.
    bool write(const void* rgba, size_t bytes);
    // Ends the stream and waits for ffmpeg. True: the file is in place. False: `error` says why (logged once, as
    // start's was; repeated calls return the same message without logging again). Never hangs.
    bool finish(std::string& error);
    // Stops without a result (the user cancelled): ffmpeg is ended and the partial file removed. No message.
    void cancel();
    long frames() const { return frames_; }
    bool running() const { return started_ && !done_; }
    const std::string& error() const { return error_; }

    // The message for "ffmpeg isn't installed", per platform.
    static std::string notFoundMessage(const std::string& what);

private:
    bool fail(const std::string& message);
    VideoEncoderSpec spec_;
    ChildProcess process_;
    std::string temp_, log_, error_;
    long frames_ = 0;
    bool started_ = false, done_ = false, writeFailed_ = false, reported_ = false;
};

// The last `lines` non-empty lines of a text file (ffmpeg's stderr), single-spaced, "" when there are none.
std::string tailOfFile(const std::string& path, int lines = 3, size_t maxBytes = 4096);

} // namespace sf::ai
