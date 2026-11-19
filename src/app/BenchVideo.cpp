// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/BenchVideo.h"

#include <algorithm>
#include <cstring>

namespace sf {

namespace {
constexpr GLenum kPixelPackBuffer = 0x88EB, kStreamRead = 0x88E1, kMapReadBit = 0x0001;
}

bool BenchVideo::start(const std::string& path, int w, int h) {
    w_ = w, h_ = h;
    // Bottom-up rows from glReadPixels: flipped; scaled to at most 1920 wide to keep the encoder light.
    ai::VideoEncoderSpec spec;
    spec.output = path;
    spec.width = w;
    spec.height = h;
    spec.fps = kFps;
    spec.what = "benchmark video";
    spec.outputArgs = {"-vf", "vflip,scale='min(1920,iw)':-2", "-c:v", "libx264", "-preset", "veryfast", "-crf", "20",
                       "-pix_fmt", "yuv420p", "-movflags", "+faststart"};
    spec.log = [](const std::string& m) { std::fprintf(stderr, "%s\n", m.c_str()); };
    error_.clear();
    if (!encoder_.start(spec, error_)) return false;
    started_ = true;
    thread_ = std::thread([this] { loop(); });
    gl::GenBuffers(3, pbo_);
    for (GLuint b : pbo_) {
        gl::BindBuffer(kPixelPackBuffer, b);
        gl::BufferData(kPixelPackBuffer, GLsizeiptr(size_t(w) * size_t(h) * 4), nullptr, kStreamRead);
    }
    gl::BindBuffer(kPixelPackBuffer, 0);
    return true;
}

void BenchVideo::grab(double t, int w, int h) {
    if (!started_ || w != w_ || h != h_) return;
    const int slot = pboNext_;
    if (pboFilled_ >= 3) { // the oldest read is done by now: hand it to the encoder
        gl::BindBuffer(kPixelPackBuffer, pbo_[slot]);
        if (const void* data = gl::MapBufferRange(kPixelPackBuffer, 0, GLsizeiptr(size_t(w) * size_t(h) * 4), kMapReadBit)) {
            std::vector<uint8_t> rgba(size_t(w) * size_t(h) * 4);
            std::memcpy(rgba.data(), data, rgba.size());
            gl::UnmapBuffer(kPixelPackBuffer);
            push(pboTime_[slot], std::move(rgba));
        }
    }
    gl::BindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    gl::BindBuffer(kPixelPackBuffer, pbo_[slot]);
    gl::PixelStorei(GL_PACK_ALIGNMENT, 1);
    gl::ReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    gl::BindBuffer(kPixelPackBuffer, 0);
    pboTime_[slot] = t;
    pboNext_ = (slot + 1) % 3;
    pboFilled_ = std::min(pboFilled_ + 1, 3);
}

// Takes the frame shown at `t` (seconds); drops it when the encoder is behind (or, `wait`, waits for it).
void BenchVideo::push(double t, std::vector<uint8_t>&& rgba, bool wait) {
    std::unique_lock<std::mutex> lock(m_);
    if (wait) cv_.wait(lock, [&] { return queue_.size() < 4; });
    else if (queue_.size() >= 4) return;
    queue_.push_back({t, std::move(rgba)});
    cv_.notify_all();
}

bool BenchVideo::finish() {
    if (!started_) return false;
    // The frames still in the ring, oldest first: the video runs to the last frame shown.
    const size_t bytes = size_t(w_) * size_t(h_) * 4;
    for (int k = 0; k < pboFilled_; ++k) {
        const int slot = (pboNext_ - pboFilled_ + k + 3) % 3;
        gl::BindBuffer(kPixelPackBuffer, pbo_[slot]);
        if (const void* data = gl::MapBufferRange(kPixelPackBuffer, 0, GLsizeiptr(bytes), kMapReadBit)) {
            std::vector<uint8_t> rgba(bytes);
            std::memcpy(rgba.data(), data, bytes);
            gl::UnmapBuffer(kPixelPackBuffer);
            push(pboTime_[slot], std::move(rgba), true);
        }
    }
    gl::BindBuffer(kPixelPackBuffer, 0);
    pboFilled_ = 0;
    {
        std::lock_guard<std::mutex> lock(m_);
        stop_ = true;
        cv_.notify_all();
    }
    thread_.join();
    started_ = false;
    const bool ok = encoder_.finish(error_) && written_ > 0; // (a failure is said once to the log)
    if (!ok && error_.empty()) error_ = "The benchmark video recorded nothing";
    gl::DeleteBuffers(3, pbo_);
    return ok;
}

BenchVideo::~BenchVideo() {
    if (!started_) return;
    { // (no GL here: the context may be gone; the buffers go with it)
        std::lock_guard<std::mutex> lock(m_);
        stop_ = true;
        cv_.notify_all();
    }
    thread_.join();
    encoder_.cancel();
}

// Frames to ffmpeg at a constant rate (frames repeat while the app is slower).
void BenchVideo::loop() {
    std::vector<uint8_t> last;
    double next = -1.0;
    for (;;) {
        std::pair<double, std::vector<uint8_t>> f;
        {
            std::unique_lock<std::mutex> lock(m_);
            cv_.wait(lock, [&] { return stop_ || !queue_.empty(); });
            if (queue_.empty()) break;
            f = std::move(queue_.front());
            queue_.pop_front();
            cv_.notify_all(); // (a waiting push)
        }
        if (next < 0) next = f.first;
        // Slots up to this frame's time show the previous frame; then this one.
        while (!last.empty() && next + 1.0 / kFps <= f.first) {
            if (encoder_.write(last.data(), last.size())) ++written_;
            next += 1.0 / kFps;
        }
        if (next <= f.first + 1e-9 || last.empty()) {
            if (encoder_.write(f.second.data(), f.second.size())) ++written_;
            next += 1.0 / kFps;
        }
        last = std::move(f.second);
    }
}

} // namespace sf
