// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// A table the micro shaders index by slot or vertex (render/MicroRenderer.h: the per-slot record, the base vertices, the cluster
// levels, the Focus and tile page tables). A texture buffer holds at most GL_MAX_TEXTURE_BUFFER_SIZE texels (a Mali's: 65536),
// which a base mesh of 33k triangles already passes; there the table is a 2D texture, kWidth texels a row, and the micro programs
// are built with WIDE (the shader's fetch helpers read it as rows). One kind or the other for every table of a Part (`wide`).
#pragma once

#include "render/GL.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace sf {

class TexelStore {
public:
    enum class Format { R32UI, RGBA32UI, RGBA32F };
    static constexpr uint32_t kShift = 12, kWidth = 1u << kShift; // (the shader's WIDE_SHIFT)

    TexelStore() = default;
    TexelStore(const TexelStore&) = delete;
    TexelStore& operator=(const TexelStore&) = delete;
    ~TexelStore() { release(); }

    // Whether `texels` texels fit one buffer texture on this GPU.
    static bool fitsBuffer(size_t texels) { return texels <= size_t(std::max(gl::maxTextureBufferTexels(), 0)); }
    // Whether a 2D texture of rows of kWidth can hold `texels` texels on this GPU (never in a test of a tiny GPU).
    static bool fitsWide(size_t texels) {
        GLint maxSize = 0;
        gl::GetIntegerv(GL_MAX_TEXTURE_SIZE, &maxSize);
        return !gl::testTinyGpu() && maxSize >= GLint(kWidth) && rowsFor(texels) <= size_t(maxSize);
    }
    static size_t rowsFor(size_t texels) { return (texels + kWidth - 1) / kWidth; }

    // (Re)creates the table: `texels` texels from `data` (null: zeros), a buffer texture or, with `wide`, a 2D texture.
    void create(Format format, size_t texels, const void* data, bool wide, GLenum usage = GL_STATIC_DRAW) {
        format_ = format;
        texels_ = texels;
        if (wide != wide_) release();
        wide_ = wide;
        if (!texture_) gl::GenTextures(1, &texture_);
        if (!wide) {
            if (!buffer_) gl::GenBuffers(1, &buffer_);
            gl::BindBuffer(GL_TEXTURE_BUFFER, buffer_);
            gl::BufferData(GL_TEXTURE_BUFFER, GLsizeiptr(texels * texelBytes(format)), data, usage);
            gl::BindTexture(GL_TEXTURE_BUFFER, texture_);
            gl::TexBuffer(GL_TEXTURE_BUFFER, bufferFormat(format), buffer_);
            gl::BindTexture(GL_TEXTURE_BUFFER, 0);
            gl::BindBuffer(GL_TEXTURE_BUFFER, 0);
            return;
        }
        const size_t rows = std::max<size_t>(rowsFor(texels), 1);
        gl::BindTexture(GL_TEXTURE_2D, texture_);
        gl::PixelStorei(GL_UNPACK_ALIGNMENT, 1);
        // (the last row's unused texels: rows are whole, so the upload goes from a padded copy when the data is short)
        if (data && texels % kWidth != 0) {
            padded_.assign(rows * kWidth * texelBytes(format), 0);
            std::copy_n(static_cast<const char*>(data), texels * texelBytes(format), padded_.data());
            data = padded_.data();
        }
        gl::TexImage2D(GL_TEXTURE_2D, 0, GLint(internalFormat(format)), GLsizei(kWidth), GLsizei(rows), 0, pixelFormat(format),
                       pixelType(format), data);
        gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GLint(GL_NEAREST));
        gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GLint(GL_NEAREST));
        gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GLint(GL_CLAMP_TO_EDGE));
        gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GLint(GL_CLAMP_TO_EDGE));
        gl::BindTexture(GL_TEXTURE_2D, 0);
        padded_.clear();
        padded_.shrink_to_fit();
    }

    // Wide only: `count` texels from `first`, as the rows they cover (up to three updates).
    void writeWide(size_t first, size_t count, const void* data) {
        if (!wide_ || count == 0) return;
        const size_t bytes = texelBytes(format_);
        const char* src = static_cast<const char*>(data);
        gl::BindTexture(GL_TEXTURE_2D, texture_);
        gl::PixelStorei(GL_UNPACK_ALIGNMENT, 1);
        while (count > 0) {
            const size_t x = first % kWidth, y = first / kWidth, run = std::min<size_t>(count, kWidth - x);
            gl::TexSubImage2D(GL_TEXTURE_2D, 0, GLint(x), GLint(y), GLsizei(run), 1, pixelFormat(format_), pixelType(format_), src);
            src += run * bytes;
            first += run;
            count -= run;
        }
        gl::BindTexture(GL_TEXTURE_2D, 0);
    }

    bool wide() const { return wide_; }
    GLuint buffer() const { return buffer_; }   // (a buffer texture's data: BufferUpload copies go here)
    GLuint texture() const { return texture_; }
    GLenum target() const { return wide_ ? GL_TEXTURE_2D : GL_TEXTURE_BUFFER; }
    size_t bytes() const { return texels_ * texelBytes(format_); }
    explicit operator bool() const { return texture_ != 0; }
    void bind() const { gl::BindTexture(target(), texture_); }

    void release() {
        if (buffer_) gl::DeleteBuffers(1, &buffer_), buffer_ = 0;
        if (texture_) gl::DeleteTextures(1, &texture_), texture_ = 0;
        wide_ = false;
        texels_ = 0;
    }

    static size_t texelBytes(Format f) { return f == Format::R32UI ? 4 : 16; }

private:
    static constexpr GLenum kRgbaInteger = 0x8D99;
    static GLenum bufferFormat(Format f) { return f == Format::R32UI ? GL_R32UI : f == Format::RGBA32UI ? GL_RGBA32UI : GL_RGBA32F; }
    static GLenum internalFormat(Format f) { return bufferFormat(f); }
    static GLenum pixelFormat(Format f) { return f == Format::R32UI ? GL_RED_INTEGER : f == Format::RGBA32UI ? kRgbaInteger : GL_RGBA; }
    static GLenum pixelType(Format f) { return f == Format::RGBA32F ? GL_FLOAT : GL_UNSIGNED_INT; }

    GLuint buffer_ = 0, texture_ = 0;
    bool wide_ = false;
    Format format_ = Format::R32UI;
    size_t texels_ = 0;
    std::vector<char> padded_;
};

} // namespace sf
