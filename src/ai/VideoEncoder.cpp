// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "ai/VideoEncoder.h"

#include "core/FileReplace.h"

#include <atomic>
#include <chrono>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <system_error>
#include <thread>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace sf::ai {

namespace fs = std::filesystem;

namespace {
fs::path u8path(const std::string& s) { return fs::path(std::u8string(s.begin(), s.end())); }
std::string pathString(const fs::path& p) {
    const auto s = p.u8string();
    return std::string(s.begin(), s.end());
}
int processId() {
#if defined(_WIN32)
    return int(GetCurrentProcessId());
#else
    return int(::getpid());
#endif
}
bool contains(const std::string& text, const char* needle) { return text.find(needle) != std::string::npos; }
} // namespace

std::string tailOfFile(const std::string& path, int lines, size_t maxBytes) {
    std::ifstream f(u8path(path), std::ios::binary | std::ios::ate);
    if (!f) return {};
    const std::streamoff size = f.tellg();
    const std::streamoff from = size > std::streamoff(maxBytes) ? size - std::streamoff(maxBytes) : 0;
    f.seekg(from);
    std::string text(size_t(size - from), '\0');
    f.read(text.data(), std::streamsize(text.size()));
    std::vector<std::string> all;
    std::string line;
    for (char c : text) {
        if (c == '\n' || c == '\r') {
            if (!line.empty()) all.push_back(line);
            line.clear();
        } else if (c >= ' ' || c == '\t') {
            line += c;
        }
    }
    if (!line.empty()) all.push_back(line);
    std::string out;
    for (size_t i = all.size() > size_t(lines) ? all.size() - size_t(lines) : 0; i < all.size(); ++i) {
        if (!out.empty()) out += " / ";
        out += all[i];
    }
    return out;
}

std::string VideoEncoder::notFoundMessage(const std::string& what) {
    std::string m = "The " + what + " needs ffmpeg, and it wasn't found. ";
#if defined(_WIN32)
    m += "Install it (in a terminal: winget install Gyan.FFmpeg), then export again. To use another copy, set "
         "SCULPTFORGE_FFMPEG to the path of ffmpeg.exe.";
#elif defined(__APPLE__)
    m += "Install it (in Terminal: brew install ffmpeg), then export again. To use another copy, set "
         "SCULPTFORGE_FFMPEG to its path.";
#else
    m += "Install it with your package manager (for example: sudo apt install ffmpeg), then export again. To use "
         "another copy, set SCULPTFORGE_FFMPEG to its path.";
#endif
    return m;
}

VideoEncoder::~VideoEncoder() {
    if (started_ && !done_) cancel();
}

bool VideoEncoder::fail(const std::string& message) {
    if (error_.empty()) error_ = message;
    if (!reported_) {
        reported_ = true;
        if (spec_.log) spec_.log(error_);
        else std::fprintf(stderr, "%s\n", error_.c_str());
    }
    done_ = true;
    return false;
}

bool VideoEncoder::start(const VideoEncoderSpec& spec, std::string& error) {
    spec_ = spec;
    error_.clear();
    reported_ = false;
    done_ = started_ = writeFailed_ = false;
    frames_ = 0;
    auto failed = [&](const std::string& m) {
        fail(m);
        error = error_;
        return false;
    };
    if (spec.output.empty()) return failed("The " + spec.what + " has no file name to write to");
    if (spec.width <= 0 || spec.height <= 0) return failed("The " + spec.what + " has no picture size (" + std::to_string(spec.width) + "x" + std::to_string(spec.height) + ")");
    const std::string exe = findFfmpegExe();
    if (exe.empty()) return failed(notFoundMessage(spec.what));

    // Beside the target, so the last step is a rename; made here so a folder we can't write to is reported by name
    // before ffmpeg is started (and an existing good file survives a failed run).
    if (!u8path(spec_.output).has_extension()) spec_.output += ".mp4"; // (ffmpeg picks the container from it)
    const fs::path target = u8path(spec_.output);
    fs::path temp = target;
    const fs::path ext = target.extension();
    temp.replace_extension();
    temp = fs::path(temp.native() + fs::path(".partial").native() + ext.native()); // (the extension stays last: the muxer)
    temp_ = pathString(temp);
    {
        std::error_code ec;
        if (target.has_parent_path()) fs::create_directories(target.parent_path(), ec);
        errno = 0;
        std::ofstream probe(temp, std::ios::binary | std::ios::trunc);
        if (!probe) {
            const int e = errno;
            std::string m = "The " + spec.what + " can't be written to " + spec_.output + (e ? std::string(": ") + std::strerror(e) : std::string());
#if defined(_WIN32)
            m += " (if Windows Security's Controlled folder access is on, allow SculptForge there, or choose another folder)";
#endif
            return failed(m);
        }
    }
    static std::atomic<int> counter{0};
    std::error_code ec;
    log_ = pathString(fs::temp_directory_path(ec) / u8path("sculptforge-ffmpeg-" + std::to_string(processId()) + "-" + std::to_string(++counter) + ".log"));
    fs::remove(u8path(log_), ec);

    std::vector<std::string> args = {"-hide_banner", "-nostats", "-loglevel", "error", "-y", "-f", "rawvideo", "-pix_fmt", "rgba", "-s",
                                     std::to_string(spec.width) + "x" + std::to_string(spec.height), "-r", std::to_string(spec.fps), "-i", "-"};
    args.insert(args.end(), spec.outputArgs.begin(), spec.outputArgs.end());
    args.push_back(temp_);
    std::string startError;
    if (!process_.startWithInput(exe, args, log_, {}, startError, spec.stallSeconds)) {
        fs::remove(temp, ec);
        return failed("The " + spec.what + " couldn't start ffmpeg (" + exe + "): " + startError);
    }
    started_ = true;
    return true;
}

bool VideoEncoder::write(const void* rgba, size_t bytes) {
    if (!started_ || done_ || writeFailed_) return false;
    if (!process_.writeStdin(rgba, bytes)) {
        writeFailed_ = true;
        return false;
    }
    ++frames_;
    return true;
}

void VideoEncoder::cancel() {
    if (!started_) return;
    process_.kill(0.5);
    std::error_code ec;
    fs::remove(u8path(temp_), ec);
    fs::remove(u8path(log_), ec);
    done_ = true;
    reported_ = true; // (a cancel is no failure: nothing to say)
}

bool VideoEncoder::finish(std::string& error) {
    if (!started_ || done_) { // never started, failed at start, or already finished
        error = error_;
        return error_.empty() && started_ && done_;
    }
    process_.closeStdin();
    std::error_code ec;
    // ffmpeg ends once its input does; never wait forever for it.
    const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(int(spec_.finishSeconds * 1000));
    bool timedOut = false;
    while (process_.running()) {
        if (std::chrono::steady_clock::now() > until) {
            timedOut = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    const bool stalled = process_.stalled();
    if (timedOut) process_.kill(0.5);
    const int code = process_.exitCode();
    const std::string tail = tailOfFile(log_);
    fs::remove(u8path(log_), ec);
    auto bad = [&](const std::string& m) {
        fs::remove(u8path(temp_), ec);
        fail(m);
        error = error_;
        return false;
    };
    std::string hint;
    if (contains(tail, "Permission denied") || contains(tail, "Access is denied"))
        hint = " The folder refused the write" +
               std::string(
#if defined(_WIN32)
                   " (Windows Security's Controlled folder access blocks programs it doesn't know: allow ffmpeg and SculptForge there, or choose another folder)"
#else
                   " (check the folder's permissions, or choose another)"
#endif
               ) + ".";
    if (stalled)
        return bad("The " + spec_.what + " stopped: ffmpeg stopped taking frames for " + std::to_string(int(spec_.stallSeconds)) + " s" +
                   (tail.empty() ? std::string() : " (" + tail + ")"));
    if (timedOut)
        return bad("The " + spec_.what + " stopped: ffmpeg didn't finish within " + std::to_string(int(spec_.finishSeconds)) + " s" +
                   (tail.empty() ? std::string() : " (" + tail + ")"));
    if (frames_ == 0 && !writeFailed_) return bad("The " + spec_.what + " recorded nothing: no frames were produced, so no file was written");
    if (code != 0 || writeFailed_)
        return bad("The " + spec_.what + " stopped: ffmpeg exited with code " + std::to_string(code) +
                   (tail.empty() ? std::string(" and said nothing") : ", saying: " + tail) + "." + hint);
    std::error_code sizeEc;
    const auto size = fs::file_size(u8path(temp_), sizeEc);
    if (sizeEc || size == 0)
        return bad("The " + spec_.what + " wasn't written: ffmpeg finished but " + spec_.output + " is empty." + hint);
    std::string replaceError;
    if (!replaceFile(u8path(temp_), u8path(spec_.output), replaceError))
        return bad("The " + spec_.what + " couldn't be saved to " + spec_.output + (replaceError.empty() ? std::string() : ": " + replaceError));
    done_ = true;
    error.clear();
    return true;
}

} // namespace sf::ai
