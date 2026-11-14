// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/Image.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <utility>

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

float Image::transparentFraction() const {
    if (empty()) return 0.0f;
    size_t n = 0;
    for (size_t i = 3; i < rgba.size(); i += 4) n += rgba[i] < 128;
    return float(n) / float(rgba.size() / 4);
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

Image Image::withoutPlainBackground(bool* removed) const {
    if (removed) *removed = false;
    if (empty() || transparentFraction() >= 0.10f || std::min(width, height) < 32) return *this; // (cut already)
    // Analysed on a copy of at most 512 px (the mask is scaled back up): fast on any size, and the 8 px of a thin rim
    // line at 4000 px is still a thin line.
    const Image a = downscaled(512);
    const int w = a.width, h = a.height;
    const size_t n = size_t(w) * size_t(h);
    auto at = [&](int x, int y) { return &a.rgba[(size_t(y) * size_t(w) + size_t(x)) * 4]; };
    auto lum = [&](const uint8_t* p) { return int(p[0]) + int(p[1]) + int(p[2]); };
    auto clear = [&](int x, int y) { return at(x, y)[3] < 128; }; // (the corners outside a rounded plate)
    auto dist = [](const uint8_t* p, const uint8_t* q) {
        return std::max({std::abs(int(p[0]) - int(q[0])), std::abs(int(p[1]) - int(q[1])), std::abs(int(p[2]) - int(q[2]))});
    };
    // The ring just inside the border: past a rim line or a rounded plate's corner margin, on the backdrop itself.
    const int r = std::max(3, int(float(std::min(w, h)) * 0.035f));
    std::vector<std::pair<int, int>> ring;
    for (int x = r; x < w - r; ++x) {
        ring.push_back({x, r});
        ring.push_back({x, h - 1 - r});
    }
    for (int y = r; y < h - r; ++y) {
        ring.push_back({r, y});
        ring.push_back({w - 1 - r, y});
    }
    if (ring.size() < 16) return *this;
    std::erase_if(ring, [&](const std::pair<int, int>& q) { return clear(q.first, q.second); });
    if (ring.size() < 16) return *this;
    std::vector<int> lums;
    for (auto [x, y] : ring) lums.push_back(lum(at(x, y)));
    std::vector<int> sorted = lums;
    std::nth_element(sorted.begin(), sorted.begin() + std::ptrdiff_t(sorted.size() / 2), sorted.end());
    const int medLum = sorted[sorted.size() / 2];
    std::vector<std::pair<int, int>> seeds;
    for (size_t i = 0; i < ring.size(); ++i)
        if (std::abs(lums[i] - medLum) <= 45) seeds.push_back(ring[i]);
    if (seeds.size() * 10 < ring.size() * 6) return *this; // the border isn't plain: a busy photo, a scene
    uint8_t med[4] = {0, 0, 0, 255};
    for (int c = 0; c < 3; ++c) {
        std::vector<int> v;
        for (auto [x, y] : seeds) v.push_back(at(x, y)[c]);
        std::nth_element(v.begin(), v.begin() + std::ptrdiff_t(v.size() / 2), v.end());
        med[c] = uint8_t(v[v.size() / 2]);
    }
    constexpr int kStep = 6; // neighbour to neighbour: follows a vignette, stops at an edge
    constexpr int kCap = 40; // and never far from the backdrop's own colour
    std::vector<uint8_t> bg(n, 0);
    std::vector<int> queue;
    auto push = [&](int x, int y) {
        const size_t i = size_t(y) * size_t(w) + size_t(x);
        if (!bg[i]) {
            bg[i] = 1;
            queue.push_back(int(i));
        }
    };
    for (auto [x, y] : seeds) push(x, y);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            if (clear(x, y)) push(x, y);
    for (int x = 0; x < w; ++x)
        for (int y : {0, h - 1})
            if (dist(at(x, y), med) <= kCap) push(x, y);
    for (int y = 0; y < h; ++y)
        for (int x : {0, w - 1})
            if (dist(at(x, y), med) <= kCap) push(x, y);
    for (size_t head = 0; head < queue.size(); ++head) {
        const int x = queue[head] % w, y = queue[head] / w;
        const uint8_t* c = at(x, y);
        const int nx[4] = {x + 1, x - 1, x, x}, ny[4] = {y, y, y + 1, y - 1};
        for (int k = 0; k < 4; ++k) {
            if (nx[k] < 0 || ny[k] < 0 || nx[k] >= w || ny[k] >= h || bg[size_t(ny[k]) * size_t(w) + size_t(nx[k])]) continue;
            const uint8_t* d = at(nx[k], ny[k]);
            if ((dist(c, d) <= kStep && dist(d, med) <= kCap) || clear(nx[k], ny[k])) push(nx[k], ny[k]);
        }
    }
    // The subject is what is left, minus thin leftovers (a rim line, a halo): opened by a few pixels, small bits dropped.
    std::vector<uint8_t> fg(n), tmp(n);
    for (size_t i = 0; i < n; ++i) fg[i] = bg[i] ? 0 : 1;
    const int open = std::max(2, int(float(std::min(w, h)) * 0.006f));
    auto pass = [&](std::vector<uint8_t>& m, bool grow) {
        std::fill(tmp.begin(), tmp.end(), uint8_t(0));
        for (int y = 1; y < h - 1; ++y)
            for (int x = 1; x < w - 1; ++x) {
                const size_t i = size_t(y) * size_t(w) + size_t(x);
                const bool any = m[i - 1] || m[i + 1] || m[i - size_t(w)] || m[i + size_t(w)] || m[i];
                const bool all = m[i - 1] && m[i + 1] && m[i - size_t(w)] && m[i + size_t(w)] && m[i];
                tmp[i] = grow ? any : all;
            }
        m.swap(tmp);
    };
    for (int k = 0; k < open; ++k) pass(fg, false);
    std::vector<int> label(n, 0), sizes{0};
    for (size_t i = 0; i < n; ++i) {
        if (!fg[i] || label[i]) continue;
        const int id = int(sizes.size());
        sizes.push_back(0);
        std::vector<int> stack{int(i)};
        label[i] = id;
        while (!stack.empty()) {
            const int j = stack.back();
            stack.pop_back();
            ++sizes[size_t(id)];
            const int x = j % w, y = j / w;
            const int nx[4] = {x + 1, x - 1, x, x}, ny[4] = {y, y, y + 1, y - 1};
            for (int k = 0; k < 4; ++k) {
                if (nx[k] < 0 || ny[k] < 0 || nx[k] >= w || ny[k] >= h) continue;
                const size_t q = size_t(ny[k]) * size_t(w) + size_t(nx[k]);
                if (fg[q] && !label[q]) {
                    label[q] = id;
                    stack.push_back(int(q));
                }
            }
        }
    }
    const int biggest = *std::max_element(sizes.begin(), sizes.end());
    if (biggest <= 0) return *this;
    for (size_t i = 0; i < n; ++i) fg[i] = label[i] > 0 && sizes[size_t(label[i])] * 10 >= biggest ? 1 : 0;
    for (int k = 0; k < open + 1; ++k) pass(fg, true);
    size_t subject = 0, backdrop = 0;
    for (size_t i = 0; i < n; ++i) {
        subject += fg[i];
        backdrop += bg[i];
    }
    const float subjectFrac = float(subject) / float(n), backdropFrac = float(backdrop) / float(n);
    // Sensible: a real backdrop went, and a subject of a fair size is left (not the whole picture, not a speck).
    if (backdropFrac < 0.25f || subjectFrac < 0.02f || subjectFrac > 0.85f) return *this;
    // Back to full size: bilinear, then a soft threshold, so the edge isn't blocky.
    Image out = *this;
    for (int y = 0; y < height; ++y) {
        const float v = (float(y) + 0.5f) * float(h) / float(height) - 0.5f;
        const int y0 = std::clamp(int(std::floor(v)), 0, h - 1), y1 = std::min(y0 + 1, h - 1);
        const float fy = std::clamp(v - float(y0), 0.0f, 1.0f);
        for (int x = 0; x < width; ++x) {
            const float u = (float(x) + 0.5f) * float(w) / float(width) - 0.5f;
            const int x0 = std::clamp(int(std::floor(u)), 0, w - 1), x1 = std::min(x0 + 1, w - 1);
            const float fx = std::clamp(u - float(x0), 0.0f, 1.0f);
            auto m = [&](int xx, int yy) { return float(fg[size_t(yy) * size_t(w) + size_t(xx)]); };
            const float val = (m(x0, y0) * (1 - fx) + m(x1, y0) * fx) * (1 - fy) + (m(x0, y1) * (1 - fx) + m(x1, y1) * fx) * fy;
            const float alpha = std::clamp((val - 0.5f) * 3.0f + 0.5f, 0.0f, 1.0f);
            out.rgba[(size_t(y) * size_t(width) + size_t(x)) * 4 + 3] = uint8_t(alpha * 255.0f + 0.5f);
        }
    }
    if (removed) *removed = true;
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
