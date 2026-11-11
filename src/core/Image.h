// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace sf {

// 8-bit RGBA image with float helpers for sculpting (PNG, JPEG, BMP, TGA, PSD, GIF, HDR via stb_image).
struct Image {
    int width = 0, height = 0;
    std::vector<uint8_t> rgba;

    bool empty() const { return width <= 0 || height <= 0; }
    float aspect() const { return height > 0 ? float(width) / float(height) : 1.0f; }
    // Luminance and alpha in 0..1 with bilinear filtering; u, v in 0..1 (v = 0 at the top), clamped.
    float luminance(float u, float v) const;
    float alpha(float u, float v) const;
    bool hasTransparency() const;

    static bool load(const std::string& path, Image& out, std::string& error);
    static bool decode(const unsigned char* bytes, size_t size, Image& out, std::string& error);
    // The width and height a PNG/JPEG header claims, read without decoding (false if it isn't one).
    static bool dimensions(const unsigned char* bytes, size_t size, int& width, int& height);
    bool savePng(const std::string& path, std::string& error) const;
    bool encodePng(std::vector<uint8_t>& out) const;
    // JPEG (no alpha: transparent pixels go over black) at `quality` 1-100.
    bool saveJpg(const std::string& path, int quality, std::string& error) const;
    // Box-filtered copy whose longer side is at most `maxSide` (unchanged if already smaller).
    Image downscaled(int maxSide) const;
    // Crops to the opaque subject plus `margin` (fraction of its size) and centres
    // it on a transparent square canvas. Unchanged if there is no transparency.
    Image framedSubject(float margin) const;
    static bool isImagePath(const std::string& path);

private:
    float sampleChannel(float u, float v, int channel) const;
};

} // namespace sf
