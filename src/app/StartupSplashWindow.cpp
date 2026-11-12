// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/StartupSplashWindow.h"

#include "app/I18n.h"
#include "app/StartupSplash.h"
#include "core/Image.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
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

bool drawTexture(SDL_Renderer* renderer, SDL_Texture* tex) {
    return tex && SDL_SetRenderDrawColor(renderer, kBackground[0], kBackground[1], kBackground[2], 255) && SDL_RenderClear(renderer) &&
           SDL_RenderTexture(renderer, tex, nullptr, nullptr);
}

} // namespace

Path pathFromEnv(const char* value) {
    const std::string v = value ? value : "";
    return v == "surface" ? Path::Surface : v == "renderer" ? Path::Renderer : v == "software" ? Path::Software : Path::Auto;
}

namespace {

// A hidden, borderless, floating utility window in the middle of the screen, with its own app id (so a window manager rule can
// name it: sculptforge-splash; the main window keeps the app's). Hidden until the picture is on it.
SDL_Window* makeWindow() {
    // A software window surface: with the default SDL may back it with an accelerated renderer, i.e. its own OpenGL context
    // made current on this thread, under the main window's (#192: the main shaders then compiled in the wrong context).
    SDL_SetHint(SDL_HINT_FRAMEBUFFER_ACCELERATION, "0");
    const char* before = SDL_GetHint(SDL_HINT_APP_ID);
    const std::string previous = before ? before : "";
    SDL_SetHint(SDL_HINT_APP_ID, "sculptforge-splash");
    SDL_Window* w = SDL_CreateWindow("SculptForge", kLogicalWidth, kLogicalHeight,
                                     SDL_WINDOW_BORDERLESS | SDL_WINDOW_UTILITY | SDL_WINDOW_ALWAYS_ON_TOP |
                                         SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_HIDDEN);
    if (before) SDL_SetHint(SDL_HINT_APP_ID, previous.c_str());
    else SDL_ResetHint(SDL_HINT_APP_ID);
    if (w) SDL_SetWindowPosition(w, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED); // (Wayland leaves the place to the compositor)
    return w;
}

// An SDL renderer on the window: a GPU driver first (never the software one, nor the GPU API's, which wants its own set-up),
// the software one only if `software`.
SDL_Renderer* makeRenderer(SDL_Window* w, bool software) {
    if (software) return SDL_CreateRenderer(w, "software");
    for (int i = 0, n = SDL_GetNumRenderDrivers(); i < n; ++i) {
        const char* name = SDL_GetRenderDriver(i);
        if (!name || std::string(name) == "software" || std::string(name) == "gpu") continue;
        if (SDL_Renderer* r = SDL_CreateRenderer(w, name)) return r;
    }
    return nullptr;
}

} // namespace

bool Window::open(const std::string& versionText, Path path) {
    if (window_) return true;
    for (Path attempt : {Path::Surface, Path::Renderer, Path::Software}) {
        if (path != Path::Auto && path != attempt) continue;
        SDL_Window* w = makeWindow();
        if (!w) return false; // no window system at all
        int pw = 0, ph = 0;
        SDL_GetWindowSizeInPixels(w, &pw, &ph);
        std::vector<uint8_t> rgba;
        bool drawn = false;
        SDL_Renderer* renderer = nullptr;
        if (attempt == Path::Surface) {
            SDL_Surface* surface = SDL_GetWindowSurface(w);
            drawn = surface && pw > 0 && paint(pw, ph, versionText, rgba) && surface->w == pw && surface->h == ph &&
                    SDL_ConvertPixels(pw, ph, SDL_PIXELFORMAT_RGBA32, rgba.data(), pw * 4, surface->format, surface->pixels, surface->pitch);
        } else if ((renderer = makeRenderer(w, attempt == Path::Software)) != nullptr) {
            // The picture at the renderer's own pixel size, so a fractional or HiDPI scale is drawn, never stretched.
            if (SDL_GetCurrentRenderOutputSize(renderer, &pw, &ph) && pw > 0 && paint(pw, ph, versionText, rgba)) {
                SDL_Texture* tex = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, pw, ph);
                if (tex && SDL_UpdateTexture(tex, nullptr, rgba.data(), pw * 4)) {
                    texture_ = tex;
                    drawn = drawTexture(renderer, tex);
                }
                if (!drawn && tex) { SDL_DestroyTexture(tex); texture_ = nullptr; }
            }
        }
        if (!drawn) { // this way said no: before anything was shown, so no empty window flashes
            if (renderer) SDL_DestroyRenderer(renderer);
            SDL_DestroyWindow(w);
            texture_ = nullptr;
            continue;
        }
        SDL_ShowWindow(w);
        if (renderer) SDL_RenderPresent(renderer);
        else SDL_UpdateWindowSurface(w);
        window_ = w;
        renderer_ = renderer;
        used_ = attempt;
        SDL_PumpEvents();
        return true;
    }
    return false;
}

bool Window::readBack(std::vector<uint8_t>& rgba, int& width, int& height) const {
    if (!window_) return false;
    SDL_Surface* src = nullptr;
    bool own = false;
    if (renderer_) {
        if (!drawTexture(renderer_, texture_)) return false;
        src = SDL_RenderReadPixels(renderer_, nullptr);
        own = true;
    } else
        src = SDL_GetWindowSurface(window_);
    if (!src) return false;
    SDL_Surface* conv = SDL_ConvertSurface(src, SDL_PIXELFORMAT_RGBA32);
    if (own) SDL_DestroySurface(src);
    if (!conv) return false;
    width = conv->w;
    height = conv->h;
    rgba.resize(size_t(width) * size_t(height) * 4);
    for (int y = 0; y < height; ++y)
        std::memcpy(&rgba[size_t(y) * size_t(width) * 4], static_cast<const uint8_t*>(conv->pixels) + size_t(y) * size_t(conv->pitch), size_t(width) * 4);
    SDL_DestroySurface(conv);
    return true;
}

void Window::pump() {
    if (!window_) return;
    SDL_PumpEvents(); // (what comes stays queued for the app's loop)
}

void Window::fadeOut(double ms) {
    if (!window_ || ms <= 0.0 || !SDL_SetWindowOpacity(window_, 1.0f)) return; // (no opacity: Wayland, some X11 set-ups; it just closes)
    const Uint64 start = SDL_GetTicksNS();
    for (;;) {
        const double t = double(SDL_GetTicksNS() - start) / 1e6 / ms;
        if (t >= 1.0) break;
        SDL_SetWindowOpacity(window_, float(1.0 - t));
        SDL_PumpEvents();
        SDL_Delay(8);
    }
}

void Window::close() {
    if (!window_) return;
    if (renderer_) {
        if (texture_) SDL_DestroyTexture(texture_);
        SDL_DestroyRenderer(renderer_);
    }
    texture_ = nullptr;
    renderer_ = nullptr;
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

int selfTest() {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::printf("splash-selftest: no window system: %s\n", SDL_GetError());
        return 1;
    }
    std::printf("splash-selftest: video driver %s\n", SDL_GetCurrentVideoDriver() ? SDL_GetCurrentVideoDriver() : "?");
    int opened = 0, wrong = 0;
    bool softwareOpened = false;
    for (Path path : {Path::Surface, Path::Renderer, Path::Software}) {
        const char* name = path == Path::Surface ? "surface" : path == Path::Renderer ? "renderer" : "software";
        Window w;
        if (!w.open("Version 0.0.0", path)) {
            std::printf("splash-selftest: %-8s no (%s)\n", name, SDL_GetError());
            continue;
        }
        std::vector<uint8_t> rgba;
        int pw = 0, ph = 0;
        bool ok = w.usedPath() == path && w.readBack(rgba, pw, ph) && pw > 0 && ph > 0;
        if (ok) { // the background in the corner, and the brand bust (not the background) at its centre
            const auto at = [&](int x, int y) { return &rgba[(size_t(y) * size_t(pw) + size_t(x)) * 4]; };
            const Layout l = layout(pw, ph, 4.3);
            const uint8_t* corner = at(2, 2);
            const uint8_t* bust = at(l.bustX + l.bustSize / 2, l.bustY + l.bustSize / 2);
            ok = corner[0] == kBackground[0] && corner[1] == kBackground[1] && corner[2] == kBackground[2] &&
                 (bust[0] != kBackground[0] || bust[1] != kBackground[1] || bust[2] != kBackground[2]);
        }
        std::printf("splash-selftest: %-8s %s (%dx%d px)\n", name, ok ? "yes, picture right" : "opened but the picture is WRONG", pw, ph);
        ++opened;
        if (!ok) ++wrong;
        if (path == Path::Software && ok) softwareOpened = true;
    }
    SDL_Quit();
    // The software renderer needs nothing from the GPU or the compositor: if it cannot open here, nothing can.
    return (opened > 0 && wrong == 0 && softwareOpened) ? 0 : 1;
}

} // namespace splash
} // namespace sf
