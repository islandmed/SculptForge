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
    // The share (0..1) of pixels that are mostly transparent (alpha < 128).
    float transparentFraction() const;

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
    // A picture with no transparency whose background is plain (a flat colour, a vignette, or a plate with rounded
    // corners behind one object): the same picture with that background made transparent, so the subject can be framed
    // and a reconstruction model doesn't build the backdrop too. Flooded from a ring just inside the border with a
    // small step tolerance (it follows a vignette, it stops at an edge), thin leftovers (a rim line) dropped, checked
    // to leave a sensible subject. Transparent pixels (the corners outside an icon's rounded plate) count as backdrop; a
    // picture that is mostly transparent (10% or more) is cut already. `removed` says whether it cut (false: cut already,
    // the border isn't plain, or the cut would leave nothing sensible: the picture is returned unchanged).
    Image withoutPlainBackground(bool* removed = nullptr) const;
    static bool isImagePath(const std::string& path);

private:
    float sampleChannel(float u, float v, int channel) const;
};

} // namespace sf
