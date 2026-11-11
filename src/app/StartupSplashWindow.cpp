// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/StartupSplashWindow.h"

#include "app/I18n.h"
#include "app/StartupSplash.h"
#include "core/Image.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <vector>

// stb_truetype (with ImGui) draws the version line: Inter, and the interface's CJK font for a Chinese translation.
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wshadow"
#elif defined(_MSC_VER)
#pragma warning(push, 0)
#endif
#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#include <imstb_truetype.h>
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif

namespace sf {
namespace assets {
extern const unsigned char fontRegular[];
extern const unsigned long long fontRegular_size;
extern const unsigned char fontCjk[];
extern const unsigned long long fontCjk_size;
extern const unsigned char fontCjkSc[];
extern const unsigned long long fontCjkSc_size;
extern const unsigned char splashWordmark[];
extern const unsigned long long splashWordmark_size;
extern const unsigned char splashBust[];
extern const unsigned long long splashBust_size;
} // namespace assets

namespace splash {
namespace {

constexpr uint8_t kMuted[3] = {0x8a, 0x8a, 0x94}; // the version line

struct Face {
    stbtt_fontinfo info{};
    bool ok = false;
    explicit Face(const unsigned char* data) { ok = stbtt_InitFont(&info, data, stbtt_GetFontOffsetForIndex(data, 0)) != 0; }
};

std::vector<uint32_t> codepoints(const std::string& utf8) {
    std::vector<uint32_t> out;
    for (size_t i = 0; i < utf8.size();) {
        const unsigned char c = (unsigned char)utf8[i];
        const int n = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 1;
        uint32_t cp = n == 1 ? c : c & (0xff >> (n + 1));
        for (int k = 1; k < n && i + k < utf8.size(); ++k) cp = (cp << 6) | ((unsigned char)utf8[i + k] & 0x3f);
        out.push_back(cp);
        i += size_t(n);
    }
    return out;
}

// `text` starting at column `left`, its x-height centred on row `centreY`, `pixelHeight` tall.
void drawText(std::vector<uint8_t>& rgba, int width, int height, const std::string& text, int left, int centreY, float pixelHeight) {
    static const Face inter(assets::fontRegular);
    static const Face cjk(language() == Language::SimplifiedChinese ? assets::fontCjkSc : assets::fontCjk);
    if (!inter.ok) return;
    struct Glyph { const Face* face; int index; float advance; };
    std::vector<Glyph> glyphs;
    for (uint32_t cp : codepoints(text)) {
        const Face* face = &inter;
        int index = stbtt_FindGlyphIndex(&inter.info, int(cp));
        if (index == 0 && cjk.ok) {
            face = &cjk;
            index = stbtt_FindGlyphIndex(&cjk.info, int(cp));
        }
        int adv = 0, lsb = 0;
        stbtt_GetGlyphHMetrics(&face->info, index, &adv, &lsb);
        const float advance = float(adv) * stbtt_ScaleForPixelHeight(&face->info, pixelHeight);
        glyphs.push_back({face, index, advance});
    }
    float pen = float(left);
    const int baseline = centreY + int(pixelHeight * 0.35f); // (x-height centred on the row)
    for (const Glyph& g : glyphs) {
        const float scale = stbtt_ScaleForPixelHeight(&g.face->info, pixelHeight);
        int x0, y0, x1, y1;
        stbtt_GetGlyphBitmapBoxSubpixel(&g.face->info, g.index, scale, scale, std::fmod(pen, 1.0f), 0.0f, &x0, &y0, &x1, &y1);
        const int w = x1 - x0, h = y1 - y0;
        if (w > 0 && h > 0) {
            std::vector<unsigned char> coverage(size_t(w) * size_t(h));
            stbtt_MakeGlyphBitmapSubpixel(&g.face->info, coverage.data(), w, h, w, scale, scale, std::fmod(pen, 1.0f), 0.0f, g.index);
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; ++x) {
                    const int px = int(pen) + x0 + x, py = baseline + y0 + y;
                    if (px < 0 || py < 0 || px >= width || py >= height) continue;
                    const float a = coverage[size_t(y) * size_t(w) + size_t(x)] / 255.0f;
                    uint8_t* dst = &rgba[(size_t(py) * size_t(width) + size_t(px)) * 4];
                    for (int c = 0; c < 3; ++c) dst[c] = uint8_t(std::lround(dst[c] + (kMuted[c] - dst[c]) * a));
                }
        }
        pen += g.advance;
    }
}

bool paint(int width, int height, const std::string& versionText, std::vector<uint8_t>& rgba) {
    Image mark, bust;
    std::string err;
    if (!Image::decode(assets::splashWordmark, size_t(assets::splashWordmark_size), mark, err) ||
        !Image::decode(assets::splashBust, size_t(assets::splashBust_size), bust, err))
        return false;
    Layout where{};
    compose(mark, bust, width, height, rgba, where);
    if (!versionText.empty())
        drawText(rgba, width, height, versionText, where.versionX, where.versionY, 13.0f * float(width) / float(kLogicalWidth));
    return true;
}

} // namespace

bool Window::open(const std::string& versionText) {
    if (window_) return true;
    SDL_Window* w = SDL_CreateWindow("SculptForge", kLogicalWidth, kLogicalHeight,
                                     SDL_WINDOW_BORDERLESS | SDL_WINDOW_UTILITY | SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_HIDDEN);
    if (!w) return false;
    int pw = 0, ph = 0;
    SDL_GetWindowSizeInPixels(w, &pw, &ph);
    SDL_Surface* surface = SDL_GetWindowSurface(w);
    std::vector<uint8_t> rgba;
    if (!surface || pw <= 0 || !paint(pw, ph, versionText, rgba) || surface->w != pw || surface->h != ph ||
        !SDL_ConvertPixels(pw, ph, SDL_PIXELFORMAT_RGBA32, rgba.data(), pw * 4, surface->format, surface->pixels, surface->pitch)) {
        SDL_DestroyWindow(w);
        return false;
    }
    SDL_ShowWindow(w);
    SDL_UpdateWindowSurface(w);
    window_ = w;
    SDL_PumpEvents();
    return true;
}

void Window::pump() {
    if (!window_) return;
    SDL_PumpEvents(); // (what comes stays queued for the app's loop)
}

void Window::close() {
    if (!window_) return;
    SDL_DestroyWindow(window_);
    window_ = nullptr;
    SDL_PumpEvents();
}

bool writeShot(const std::string& path, int width, int height, const std::string& versionText, std::string& error) {
    Image img;
    img.width = std::clamp(width, 64, 8192);
    img.height = std::clamp(height, 64, 8192);
    if (!paint(img.width, img.height, versionText, img.rgba)) {
        error = "The splash picture could not be made";
        return false;
    }
    return img.savePng(path, error);
}

} // namespace splash
} // namespace sf
