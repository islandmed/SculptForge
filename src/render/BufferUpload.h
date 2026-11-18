// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Partial updates of a buffer the GPU may still be reading (the micro levels' texture buffers during a stroke). On Mesa's
// OpenGL over D3D12 (WSL; Windows on ARM laptops, whose OpenGL is the same layer) glBufferSubData into a texture buffer
// the last frames still draw from removes the device ("D3D12: Removing Device") within a few micro strokes (11-17, RTX
// 3060, Mesa 25.2.8: 10 of 12 runs; GALLIUM_THREAD=0 avoids it too, so it is the layer's threaded buffer path). There
// (gl::copyUploads) the bytes go to a staging buffer (orphaned each time) and CopyBufferSubData puts each run where it
// belongs: 0 of 11 runs lost the device. Elsewhere it stays BufferSubData: copies cost large strokes on a 200M micro
// level a third of their frame rate on Mesa/Apple (frame p50 4.2 -> 7.3 ms, 3 of 3 runs).
#pragma once

#include "render/GL.h"

#include <cstddef>

namespace sf {

class BufferUpload {
public:
    BufferUpload() = default;
    BufferUpload(const BufferUpload&) = delete;
    BufferUpload& operator=(const BufferUpload&) = delete;
    ~BufferUpload() { release(); }
    void release() {
        if (staging_) gl::DeleteBuffers(1, &staging_), staging_ = 0;
    }
    // The bytes the next copy() calls read from.
    void stage(const void* data, size_t bytes) {
        copies_ = gl::copyUploads();
        data_ = static_cast<const char*>(data);
        if (!copies_) return;
        if (!staging_) gl::GenBuffers(1, &staging_);
        gl::BindBuffer(GL_COPY_READ_BUFFER, staging_);
        gl::BufferData(GL_COPY_READ_BUFFER, GLsizeiptr(bytes), data, GL_STREAM_DRAW);
    }
    // Staged bytes [from, from + bytes) into `target` at `to` (the staged data must still be there: it is read here when
    // the update is not a copy).
    void copy(GLuint target, size_t from, size_t to, size_t bytes) {
        if (!copies_) {
            gl::BindBuffer(GL_COPY_WRITE_BUFFER, target);
            gl::BufferSubData(GL_COPY_WRITE_BUFFER, GLintptr(to), GLsizeiptr(bytes), data_ + from);
            return;
        }
        gl::BindBuffer(GL_COPY_READ_BUFFER, staging_);
        gl::BindBuffer(GL_COPY_WRITE_BUFFER, target);
        gl::CopyBufferSubData(GL_COPY_READ_BUFFER, GL_COPY_WRITE_BUFFER, GLintptr(from), GLintptr(to), GLsizeiptr(bytes));
    }
    // Unbinds the copy targets (after the last copy of an update).
    static void done() {
        gl::BindBuffer(GL_COPY_READ_BUFFER, 0);
        gl::BindBuffer(GL_COPY_WRITE_BUFFER, 0);
    }
    // One run: stage, copy, done.
    void write(GLuint target, size_t to, const void* data, size_t bytes) {
        stage(data, bytes);
        copy(target, 0, to, bytes);
        done();
    }

private:
    GLuint staging_ = 0;
    bool copies_ = false;
    const char* data_ = nullptr;
};

} // namespace sf
