// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/StartupSplash.h"

#include "core/Image.h"
#include "core/Json.h"

#include <algorithm>
#include <cmath>

namespace sf::splash {

bool enabled(const Setting& s) {
    if (!s.interactive || !s.preference) return false;
    return s.videoDriver != "offscreen" && s.videoDriver != "dummy";
}

bool due(bool isEnabled, bool alreadyShown, double elapsedMs) {
    return isEnabled && !alreadyShown && elapsedMs > kShowAfterMs;
}

bool preferenceFromUiJson(const std::string& text) {
    Json j;
    std::string err;
    if (!Json::parse(text, j, err)) return true;
    return j["prefs"]["startupSplash"].asBool(true);
}

std::string languageFromUiJson(const std::string& text) {
    Json j;
    std::string err;
    if (!Json::parse(text, j, err)) return "en";
    const Json& language = j["prefs"]["language"];
    return language.isString() ? language.asString() : "en";
}

Layout layout(int width, int height, double wordmarkAspect) {
    Layout l{};
    l.bustSize = int(std::lround(height * 0.74));
    const int margin = (height - l.bustSize) / 2; // the same above, below and at the right
    l.bustX = width - margin - l.bustSize;
    l.bustY = margin;
    l.wordX = int(std::lround(width * 0.09));
    l.wordW = int(std::lround(width * 0.37));
    l.wordH = std::max(2, int(std::lround(l.wordW / std::max(wordmarkAspect, 0.1))));
    l.wordY = int(std::lround(height * 0.46)) - l.wordH / 2;
    l.versionX = l.wordX;
    l.versionY = l.wordY + l.wordH + std::max(14, height / 9);
    return l;
}

namespace {

// `src` scaled into the dw x dh box at (x0, y0), composited over the picture. Shrinking takes the exact area of the
// source under each pixel, enlarging is bilinear; both work on premultiplied colour, so transparent edges don't darken.
void blit(const Image& src, int width, int height, std::vector<uint8_t>& rgba, int x0, int y0, int dw, int dh) {
    if (src.empty() || dw < 1 || dh < 1) return;
    const double sx = double(src.width) / dw, sy = double(src.height) / dh;
    auto px = [&](int x, int y, int c) { return double(src.rgba[(size_t(y) * src.width + x) * 4 + c]); };
    for (int y = 0; y < dh; ++y) {
        const int py = y0 + y;
        if (py < 0 || py >= height) continue;
        for (int x = 0; x < dw; ++x) {
            const int qx = x0 + x;
            if (qx < 0 || qx >= width) continue;
            double r = 0, g = 0, b = 0, a = 0, weight = 0;
            if (sx > 1.0 || sy > 1.0) {
                const double fx0 = x * sx, fx1 = (x + 1) * sx, fy0 = y * sy, fy1 = (y + 1) * sy;
                for (int yy = int(fy0); yy < std::min(src.height, int(std::ceil(fy1))); ++yy) {
                    const double wy = std::min(fy1, yy + 1.0) - std::max(fy0, double(yy));
                    for (int xx = int(fx0); xx < std::min(src.width, int(std::ceil(fx1))); ++xx) {
                        const double w = wy * (std::min(fx1, xx + 1.0) - std::max(fx0, double(xx)));
                        const double al = px(xx, yy, 3) / 255.0;
                        r += w * al * px(xx, yy, 0);
                        g += w * al * px(xx, yy, 1);
                        b += w * al * px(xx, yy, 2);
                        a += w * al;
                        weight += w;
                    }
                }
            } else {
                const double fx = std::clamp((x + 0.5) * sx - 0.5, 0.0, src.width - 1.0);
                const double fy = std::clamp((y + 0.5) * sy - 0.5, 0.0, src.height - 1.0);
                const int ix = int(fx), iy = int(fy), jx = std::min(ix + 1, src.width - 1), jy = std::min(iy + 1, src.height - 1);
                const double tx = fx - ix, ty = fy - iy;
                const int xs[4] = {ix, jx, ix, jx}, ys[4] = {iy, iy, jy, jy};
                const double ws[4] = {(1 - tx) * (1 - ty), tx * (1 - ty), (1 - tx) * ty, tx * ty};
                for (int k = 0; k < 4; ++k) {
                    const double al = px(xs[k], ys[k], 3) / 255.0;
                    r += ws[k] * al * px(xs[k], ys[k], 0);
                    g += ws[k] * al * px(xs[k], ys[k], 1);
                    b += ws[k] * al * px(xs[k], ys[k], 2);
                    a += ws[k] * al;
                    weight += ws[k];
                }
            }
            if (weight <= 0.0 || a <= 0.0) continue;
            r /= weight; g /= weight; b /= weight; a /= weight; // r, g, b still carry alpha: composite over the picture
            uint8_t* dst = &rgba[(size_t(py) * width + qx) * 4];
            dst[0] = uint8_t(std::lround(std::clamp(r + (1.0 - a) * dst[0], 0.0, 255.0)));
            dst[1] = uint8_t(std::lround(std::clamp(g + (1.0 - a) * dst[1], 0.0, 255.0)));
            dst[2] = uint8_t(std::lround(std::clamp(b + (1.0 - a) * dst[2], 0.0, 255.0)));
        }
    }
}

} // namespace

void compose(const Image& wordmark, const Image& bust, int width, int height, std::vector<uint8_t>& rgba, Layout& where) {
    rgba.resize(size_t(width) * size_t(height) * 4);
    for (size_t i = 0; i < rgba.size(); i += 4) {
        rgba[i] = kBackground[0];
        rgba[i + 1] = kBackground[1];
        rgba[i + 2] = kBackground[2];
        rgba[i + 3] = 255;
    }
    where = layout(width, height, wordmark.empty() ? 4.3 : double(wordmark.width) / wordmark.height);
    if (width < 8 || height < 8) return;
    blit(bust, width, height, rgba, where.bustX, where.bustY, where.bustSize, where.bustSize);
    blit(wordmark, width, height, rgba, where.wordX, where.wordY, where.wordW, where.wordH);
}

} // namespace sf::splash
