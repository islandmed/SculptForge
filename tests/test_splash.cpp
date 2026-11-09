// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "TestHarness.h"

#if defined(SF_TEST_ACTIONS)
#include "app/SplashScreen.h"

TEST(splash_decode_and_render) {
    sf::Image art;
    std::string err;
    CHECK(sf::decodeSplashImage(art, err));
    CHECK(art.width == 1024);
    CHECK(art.height == 571);
    CHECK(!art.empty());

    std::vector<uint8_t> rgba0, rgbaHalf, rgbaFull, cachedBase;
    CHECK(sf::renderSplashBuffer(art, 384, 214, 0.0f, rgba0, &cachedBase));
    CHECK(rgba0.size() == size_t(384 * 214 * 4));
    CHECK(cachedBase.size() == size_t(384 * 214 * 4));

    CHECK(sf::renderSplashBuffer(art, 384, 214, 0.5f, rgbaHalf, &cachedBase));
    CHECK(sf::renderSplashBuffer(art, 384, 214, 1.0f, rgbaFull, &cachedBase));

    // Count forged-orange fill pixels along the center row of the bottom progress bar track as progress increases.
    auto countOrangePixels = [](const std::vector<uint8_t>& buf, int w, int h) {
        int count = 0;
        const int y = h - 8;
        for (int x = 0; x < w; ++x) {
            const uint8_t* p = buf.data() + (size_t(y) * size_t(w) + size_t(x)) * 4;
            if (p[0] >= 230 && p[1] >= 120 && p[2] <= 180) ++count;
        }
        return count;
    };
    const int c0 = countOrangePixels(rgba0, 384, 214);
    const int cHalf = countOrangePixels(rgbaHalf, 384, 214);
    const int cFull = countOrangePixels(rgbaFull, 384, 214);
    CHECK(c0 == 0);
    CHECK(cHalf > c0);
    CHECK(cFull > cHalf);
}

TEST(splash_startup_gating_rules) {
    sf::AppOptions opts;
    // Normal interactive desktop startup shows the splash screen.
    CHECK(sf::shouldShowSplash(opts, false, false, false));

    // Headless, shared window (--ui-serve), or automated runs skip the splash screen by default.
    CHECK(!sf::shouldShowSplash(opts, true, false, false));
    CHECK(!sf::shouldShowSplash(opts, false, true, false));
    CHECK(!sf::shouldShowSplash(opts, false, false, true));

    // Explicit --no-splash skips it on interactive desktop runs.
    opts.noSplash = true;
    CHECK(!sf::shouldShowSplash(opts, false, false, false));
    opts.noSplash = false;

    // Explicit --splash forces the splash screen even when automated is true (unless headless or sharedWindow).
    opts.splash = true;
    CHECK(sf::shouldShowSplash(opts, false, false, true));
    CHECK(!sf::shouldShowSplash(opts, true, false, true));
    opts.splash = false;

    // Benchmarks, UI tests, demos, recordings, Blender bridge, scripts, and idle/stroke measurements skip splash.
    opts.benchmark = true;
    CHECK(!sf::shouldShowSplash(opts, false, false, false));
    opts.benchmark = false;

    opts.uiTest = "smoke.sfui";
    CHECK(!sf::shouldShowSplash(opts, false, false, false));
    opts.uiTest.clear();

    opts.measureIdle = 2.0f;
    CHECK(!sf::shouldShowSplash(opts, false, false, false));
    opts.measureIdle = 0.0f;

    opts.blender = true;
    CHECK(!sf::shouldShowSplash(opts, false, false, false));
    opts.blender = false;

    opts.script = "new_sphere";
    CHECK(!sf::shouldShowSplash(opts, false, false, false));
    opts.script.clear();
}
#endif
