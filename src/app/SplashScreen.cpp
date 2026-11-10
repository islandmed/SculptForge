// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/SplashScreen.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace sf {

namespace assets {
extern const unsigned char splashImage[];
extern const unsigned long long splashImage_size;
} // namespace assets

namespace {

constexpr int kDefaultSplashWidth = 768;
constexpr int kDefaultSplashHeight = 428; // matches 1024x571 aspect ratio (1.793)

bool envEnabled(const char* name) {
    const char* v = std::getenv(name);
    return v && *v && *v != '0';
}

// Bilinearly resamples `src` into `dst` (width x height RGBA8).
void resampleRgba(const Image& src, int width, int height, std::vector<uint8_t>& dst) {
    dst.resize(size_t(width) * size_t(height) * 4);
    const int sw = src.width;
    const int sh = src.height;
    const uint8_t* sp = src.rgba.data();
    for (int y = 0; y < height; ++y) {
        const float v = height > 1 ? float(y) / float(height - 1) : 0.0f;
        const float fyFull = std::clamp(v, 0.0f, 1.0f) * float(sh - 1);
        const int y0 = int(fyFull);
        const int y1 = std::min(y0 + 1, sh - 1);
        const float fy = fyFull - float(y0);
        uint8_t* row = dst.data() + size_t(y) * size_t(width) * 4;
        for (int x = 0; x < width; ++x) {
            const float u = width > 1 ? float(x) / float(width - 1) : 0.0f;
            const float fxFull = std::clamp(u, 0.0f, 1.0f) * float(sw - 1);
            const int x0 = int(fxFull);
            const int x1 = std::min(x0 + 1, sw - 1);
            const float fx = fxFull - float(x0);
            const uint8_t* p00 = sp + (size_t(y0) * size_t(sw) + size_t(x0)) * 4;
            const uint8_t* p10 = sp + (size_t(y0) * size_t(sw) + size_t(x1)) * 4;
            const uint8_t* p01 = sp + (size_t(y1) * size_t(sw) + size_t(x0)) * 4;
            const uint8_t* p11 = sp + (size_t(y1) * size_t(sw) + size_t(x1)) * 4;
            for (int c = 0; c < 3; ++c) {
                const float top = float(p00[c]) + (float(p10[c]) - float(p00[c])) * fx;
                const float bot = float(p01[c]) + (float(p11[c]) - float(p01[c])) * fx;
                const float val = top + (bot - top) * fy;
                row[x * 4 + c] = uint8_t(std::clamp(val + 0.5f, 0.0f, 255.0f));
            }
            row[x * 4 + 3] = 255;
        }
    }
}

void blendPixel(uint8_t* px, uint8_t r, uint8_t g, uint8_t b, float alpha) {
    const float a = std::clamp(alpha, 0.0f, 1.0f);
    const float inv = 1.0f - a;
    px[0] = uint8_t(std::clamp(float(px[0]) * inv + float(r) * a + 0.5f, 0.0f, 255.0f));
    px[1] = uint8_t(std::clamp(float(px[1]) * inv + float(g) * a + 0.5f, 0.0f, 255.0f));
    px[2] = uint8_t(std::clamp(float(px[2]) * inv + float(b) * a + 0.5f, 0.0f, 255.0f));
    px[3] = 255;
}

void fillRect(std::vector<uint8_t>& rgba, int width, int height,
              int x0, int y0, int x1, int y1,
              uint8_t r, uint8_t g, uint8_t b, float alpha = 1.0f) {
    const int cx0 = std::clamp(x0, 0, width);
    const int cy0 = std::clamp(y0, 0, height);
    const int cx1 = std::clamp(x1, 0, width);
    const int cy1 = std::clamp(y1, 0, height);
    for (int y = cy0; y < cy1; ++y) {
        uint8_t* row = rgba.data() + size_t(y) * size_t(width) * 4;
        for (int x = cx0; x < cx1; ++x) {
            blendPixel(row + x * 4, r, g, b, alpha);
        }
    }
}

} // namespace

bool shouldShowSplash(const AppOptions& options, bool headless, bool sharedWindow, bool automated) {
    if (options.noSplash || envEnabled("SF_NO_SPLASH")) return false;
    if (options.splash || envEnabled("SF_SPLASH")) return !headless && !sharedWindow;
    if (headless || sharedWindow || automated) return false;
    if (options.blender || !options.script.empty() || !options.demo.empty() || !options.uiTest.empty() ||
        !options.recordPath.empty() || !options.renderPath.empty() || !options.timelapsePath.empty() ||
        options.benchmark || !options.benchmarkScript.empty() ||
        options.measureIdle > 0.0f || options.measureStroke > 0.0f) {
        return false;
    }
    return true;
}

bool decodeSplashImage(Image& out, std::string& error) {
    return Image::decode(assets::splashImage, size_t(assets::splashImage_size), out, error);
}

bool renderSplashBuffer(const Image& art, int width, int height, float progress,
                        std::vector<uint8_t>& outRgba,
                        std::vector<uint8_t>* cachedBaseRgba) {
    if (art.empty() || width <= 0 || height <= 0) return false;
    const size_t totalBytes = size_t(width) * size_t(height) * 4;
    if (cachedBaseRgba) {
        if (cachedBaseRgba->size() != totalBytes) {
            resampleRgba(art, width, height, *cachedBaseRgba);
        }
        outRgba = *cachedBaseRgba;
    } else {
        resampleRgba(art, width, height, outRgba);
    }

    const float clamped = std::clamp(progress, 0.0f, 1.0f);
    const float scale = float(height) / float(kDefaultSplashHeight);

    // Clean dark footer band along the bottom edge housing the progress bar.
    const int footerH = std::max(16, int(std::round(32.0f * scale)));
    const int footerY = height - footerH;
    fillRect(outRgba, width, height, 0, footerY, width, height, 14, 14, 17, 0.94f);
    // Subtle top divider line on the footer band.
    const int lineH = std::max(1, int(std::round(1.0f * scale)));
    fillRect(outRgba, width, height, 0, footerY, width, footerY + lineH, 46, 44, 54, 1.0f);

    // Recessed progress bar track.
    const int padX = std::max(12, int(std::round(24.0f * scale)));
    const int barH = std::max(4, int(std::round(7.0f * scale)));
    const int barY = footerY + (footerH - barH) / 2;
    const int trackX0 = padX;
    const int trackX1 = std::max(trackX0 + 1, width - padX);
    fillRect(outRgba, width, height, trackX0 - 1, barY - 1, trackX1 + 1, barY + barH + 1, 52, 50, 62, 1.0f);
    fillRect(outRgba, width, height, trackX0, barY, trackX1, barY + barH, 26, 25, 31, 1.0f);

    // Forged-orange progress fill (#f28c3c) with warm highlight edge (#ffb877).
    const int fillW = int(std::round(float(trackX1 - trackX0) * clamped));
    if (fillW > 0) {
        const int fillX1 = std::min(trackX1, trackX0 + fillW);
        fillRect(outRgba, width, height, trackX0, barY, fillX1, barY + barH, 242, 140, 60, 1.0f);
        const int hiH = std::max(1, barH / 3);
        fillRect(outRgba, width, height, trackX0, barY, fillX1, barY + hiH, 255, 184, 119, 0.85f);
        // Bright ember glow at the leading edge of the progress bar.
        const int tipW = std::max(2, int(std::round(4.0f * scale)));
        fillRect(outRgba, width, height, std::max(trackX0, fillX1 - tipW), barY, fillX1, barY + barH, 255, 220, 170, 0.9f);
    }

    // Crisp 1px outer border around the borderless splash window.
    const int border = std::max(1, int(std::round(1.0f * scale)));
    fillRect(outRgba, width, height, 0, 0, width, border, 54, 52, 64, 1.0f);
    fillRect(outRgba, width, height, 0, height - border, width, height, 54, 52, 64, 1.0f);
    fillRect(outRgba, width, height, 0, 0, border, height, 54, 52, 64, 1.0f);
    fillRect(outRgba, width, height, width - border, 0, width, height, 54, 52, 64, 1.0f);
    return true;
}

SplashScreen::~SplashScreen() {
    close();
}

bool SplashScreen::open(float durationSeconds) {
    close();
    durationSeconds_ = std::clamp(durationSeconds, 0.1f, 30.0f);
    if (const char* envSec = std::getenv("SF_SPLASH_SECONDS"); envSec && *envSec) {
        durationSeconds_ = std::clamp(float(std::atof(envSec)), 0.05f, 30.0f);
    }
    std::string err;
    if (!decodeSplashImage(art_, err)) {
        SDL_Log("Splash image decode failed: %s", err.c_str());
        return false;
    }
    const SDL_WindowFlags flags = SDL_WINDOW_BORDERLESS | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    window_ = SDL_CreateWindow("SculptForge", kDefaultSplashWidth, kDefaultSplashHeight, flags);
    if (!window_) {
        SDL_Log("Splash window creation failed: %s", SDL_GetError());
        return false;
    }
    SDL_SetWindowPosition(window_, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    SDL_ShowWindow(window_);
    SDL_RaiseWindow(window_);
    openedAt_ = std::chrono::steady_clock::now();
    progress_ = 0.05f;
    present(progress_);
    SDL_PumpEvents();
    return true;
}

void SplashScreen::setProgress(float fraction) {
    progress_ = std::clamp(fraction, progress_, 1.0f);
    if (!window_) return;
    present(progress_);
    SDL_PumpEvents();
}

void SplashScreen::present(float displayProgress) {
    if (!window_) return;
    SDL_Surface* surf = SDL_GetWindowSurface(window_);
    if (!surf || surf->w <= 0 || surf->h <= 0) return;
    if (surf->w != cachedW_ || surf->h != cachedH_) {
        cachedW_ = surf->w;
        cachedH_ = surf->h;
        baseRgba_.clear();
    }
    if (!renderSplashBuffer(art_, surf->w, surf->h, displayProgress, frameRgba_, &baseRgba_)) return;
    if (SDL_MUSTLOCK(surf)) {
        if (!SDL_LockSurface(surf)) return;
    }
    SDL_ConvertPixels(surf->w, surf->h, SDL_PIXELFORMAT_RGBA32, frameRgba_.data(), surf->w * 4,
                      surf->format, surf->pixels, surf->pitch);
    if (SDL_MUSTLOCK(surf)) {
        SDL_UnlockSurface(surf);
    }
    SDL_UpdateWindowSurface(window_);
}

bool SplashScreen::finish() {
    if (!window_) return true;
    const float startProgress = progress_;
    while (true) {
        // Pump OS events without draining startup file-drop or device events meant for App::run.
        SDL_PumpEvents();
        if (SDL_HasEvent(SDL_EVENT_QUIT)) {
            close();
            return false;
        }
        const auto now = std::chrono::steady_clock::now();
        const float elapsed = std::chrono::duration<float>(now - openedAt_).count();
        if (elapsed >= durationSeconds_) break;
        const float t = std::clamp(elapsed / std::max(durationSeconds_, 0.001f), 0.0f, 1.0f);
        const float displayProgress = std::max(startProgress, t);
        progress_ = displayProgress;
        present(displayProgress);
        SDL_Delay(16);
    }
    progress_ = 1.0f;
    present(1.0f);
    SDL_Delay(32);
    close();
    return true;
}

void SplashScreen::close() {
    if (window_) {
        SDL_DestroyWindow(window_);
        window_ = nullptr;
        SDL_PumpEvents();
    }
    baseRgba_.clear();
    frameRgba_.clear();
    cachedW_ = 0;
    cachedH_ = 0;
}

} // namespace sf
