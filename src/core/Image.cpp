// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/Image.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBI_FAILURE_USERMSG
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#endif
#include "stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WRITE_NO_STDIO
#include "stb_image_write.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace sf {

float Image::sampleChannel(float u, float v, int channel) const {
    if (empty()) return 0.0f;
    float x = std::clamp(u, 0.0f, 1.0f) * float(width - 1);
    float y = std::clamp(v, 0.0f, 1.0f) * float(height - 1);
    int x0 = int(x), y0 = int(y);
    int x1 = std::min(x0 + 1, width - 1), y1 = std::min(y0 + 1, height - 1);
    float fx = x - float(x0), fy = y - float(y0);
    auto at = [&](int xi, int yi) { return float(rgba[(size_t(yi) * size_t(width) + size_t(xi)) * 4 + size_t(channel)]); };
    float top = at(x0, y0) + (at(x1, y0) - at(x0, y0)) * fx;
    float bottom = at(x0, y1) + (at(x1, y1) - at(x0, y1)) * fx;
    return (top + (bottom - top) * fy) / 255.0f;
}

float Image::luminance(float u, float v) const {
    return 0.2126f * sampleChannel(u, v, 0) + 0.7152f * sampleChannel(u, v, 1) + 0.0722f * sampleChannel(u, v, 2);
}

float Image::alpha(float u, float v) const { return sampleChannel(u, v, 3); }

bool Image::hasTransparency() const {
    for (size_t i = 3; i < rgba.size(); i += 4)
        if (rgba[i] < 128) return true;
    return false;
}

bool Image::encodePng(std::vector<uint8_t>& out) const {
    out.clear();
    if (empty()) return false;
    auto write = [](void* ctx, void* data, int size) {
        auto* o = static_cast<std::vector<uint8_t>*>(ctx);
        o->insert(o->end(), static_cast<uint8_t*>(data), static_cast<uint8_t*>(data) + size);
    };
    return stbi_write_png_to_func(write, &out, width, height, 4, rgba.data(), width * 4) != 0;
}

bool Image::savePng(const std::string& path, std::string& error) const {
    if (empty()) {
        error = "Empty image";
        return false;
    }
    std::vector<uint8_t> png;
    if (!encodePng(png)) {
        error = "PNG encoding failed";
        return false;
    }
    std::ofstream f(std::filesystem::path(std::u8string(path.begin(), path.end())), std::ios::binary);
    if (!f.write(reinterpret_cast<const char*>(png.data()), std::streamsize(png.size()))) {
        error = "Cannot write " + path;
        return false;
    }
    return true;
}

bool Image::saveJpg(const std::string& path, int quality, std::string& error) const {
    if (empty()) {
        error = "Empty image";
        return false;
    }
    std::vector<uint8_t> rgb(size_t(width) * size_t(height) * 3);
    for (size_t i = 0, n = size_t(width) * size_t(height); i < n; ++i)
        for (int c = 0; c < 3; ++c) rgb[i * 3 + size_t(c)] = uint8_t(unsigned(rgba[i * 4 + size_t(c)]) * rgba[i * 4 + 3] / 255u);
    std::vector<uint8_t> jpg;
    auto write = [](void* ctx, void* data, int size) {
        auto* o = static_cast<std::vector<uint8_t>*>(ctx);
        o->insert(o->end(), static_cast<uint8_t*>(data), static_cast<uint8_t*>(data) + size);
    };
    if (!stbi_write_jpg_to_func(write, &jpg, width, height, 3, rgb.data(), std::clamp(quality, 1, 100))) {
        error = "JPEG encoding failed";
        return false;
    }
    std::ofstream f(std::filesystem::path(std::u8string(path.begin(), path.end())), std::ios::binary);
    if (!f.write(reinterpret_cast<const char*>(jpg.data()), std::streamsize(jpg.size()))) {
        error = "Cannot write " + path;
        return false;
    }
    return true;
}

Image Image::downscaled(int maxSide) const {
    if (empty() || std::max(width, height) <= maxSide) return *this;
    const float f = float(std::max(width, height)) / float(maxSide);
    Image out;
    out.width = std::max(1, int(float(width) / f));
    out.height = std::max(1, int(float(height) / f));
    out.rgba.resize(size_t(out.width) * size_t(out.height) * 4);
    for (int y = 0; y < out.height; ++y)
        for (int x = 0; x < out.width; ++x) {
            const int x0 = int(float(x) * f), x1 = std::max(x0 + 1, std::min(width, int(float(x + 1) * f)));
            const int y0 = int(float(y) * f), y1 = std::max(y0 + 1, std::min(height, int(float(y + 1) * f)));
            // Alpha-weighted average so transparent pixels don't darken the edges.
            float sum[4] = {0, 0, 0, 0};
            for (int sy = y0; sy < y1; ++sy)
                for (int sx = x0; sx < x1; ++sx) {
                    const uint8_t* p = &rgba[(size_t(sy) * size_t(width) + size_t(sx)) * 4];
                    const float a = float(p[3]);
                    for (int c = 0; c < 3; ++c) sum[c] += float(p[c]) * a;
                    sum[3] += a;
                }
            const float n = float((x1 - x0) * (y1 - y0));
            uint8_t* o = &out.rgba[(size_t(y) * size_t(out.width) + size_t(x)) * 4];
            for (int c = 0; c < 3; ++c) o[c] = uint8_t(sum[3] > 0.0f ? sum[c] / sum[3] + 0.5f : 0.0f);
            o[3] = uint8_t(sum[3] / n + 0.5f);
        }
    return out;
}

Image Image::framedSubject(float margin) const {
    if (!hasTransparency()) return *this;
    int x0 = width, y0 = height, x1 = -1, y1 = -1;
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
            if (rgba[(size_t(y) * size_t(width) + size_t(x)) * 4 + 3] > 8) {
                x0 = std::min(x0, x);
                y0 = std::min(y0, y);
                x1 = std::max(x1, x);
                y1 = std::max(y1, y);
            }
    if (x1 < 0) return *this;
    const int w = x1 - x0 + 1, h = y1 - y0 + 1;
    const int side = int(float(std::max(w, h)) * (1.0f + 2.0f * margin)) + 1;
    Image out;
    out.width = out.height = side;
    out.rgba.assign(size_t(side) * size_t(side) * 4, 0);
    const int ox = (side - w) / 2, oy = (side - h) / 2;
    for (int y = 0; y < h; ++y)
        std::copy_n(&rgba[(size_t(y0 + y) * size_t(width) + size_t(x0)) * 4], size_t(w) * 4,
                    &out.rgba[(size_t(oy + y) * size_t(side) + size_t(ox)) * 4]);
    return out;
}

bool Image::isImagePath(const std::string& path) {
    std::string ext = std::filesystem::path(std::u8string(path.begin(), path.end())).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" || ext == ".tga" || ext == ".psd" ||
           ext == ".gif" || ext == ".hdr";
}

bool Image::load(const std::string& path, Image& out, std::string& error) {
    std::ifstream f(std::filesystem::path(std::u8string(path.begin(), path.end())), std::ios::binary);
    if (!f) {
        error = "Cannot open " + path;
        return false;
    }
    std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (bytes.empty() || bytes.size() > size_t(1) << 30) {
        error = "Unsupported image size: " + path;
        return false;
    }
    return decode(bytes.data(), bytes.size(), out, error);
}

bool Image::dimensions(const unsigned char* bytes, size_t size, int& width, int& height) {
    int channels = 0;
    return size <= size_t(INT_MAX) && stbi_info_from_memory(bytes, int(size), &width, &height, &channels) == 1;
}

bool Image::decode(const unsigned char* bytes, size_t size, Image& out, std::string& error) {
    int w = 0, h = 0, channels = 0;
    unsigned char* data = stbi_load_from_memory(bytes, int(size), &w, &h, &channels, 4);
    if (!data) {
        error = std::string("Cannot decode image: ") + stbi_failure_reason();
        return false;
    }
    if (size_t(w) * size_t(h) > size_t(16384) * 16384) {
        stbi_image_free(data);
        error = "Image is too large";
        return false;
    }
    out.width = w;
    out.height = h;
    out.rgba.assign(data, data + size_t(w) * size_t(h) * 4);
    stbi_image_free(data);
    return true;
}

} // namespace sf
