// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The start-up splash (app/StartupSplash.h, idea by @lowenski #176): when it may show, when it is due, the preference
// read early from ui.json, and the picture.
#include "TestHarness.h"

#include "app/StartupSplash.h"
#include "core/Image.h"

using namespace sf;

TEST(splash_only_for_a_person_with_a_window_system_and_the_preference_on) {
    splash::Setting s;
    s.interactive = true;
    s.videoDriver = "x11";
    CHECK(splash::enabled(s));
    s.videoDriver = "wayland";
    CHECK(splash::enabled(s));
    s.videoDriver = "offscreen"; // headless Linux runs, CI, agents
    CHECK(!splash::enabled(s));
    s.videoDriver = "dummy";
    CHECK(!splash::enabled(s));
    s.videoDriver = "x11";
    s.preference = false; // Preferences > Interface > Show startup splash
    CHECK(!splash::enabled(s));
    s.preference = true;
    s.interactive = false; // tests, renders, recordings, benchmarks, the CLI
    CHECK(!splash::enabled(s));
    CHECK(!splash::enabled(splash::Setting{})); // (the default is not interactive)
}

TEST(splash_is_due_at_the_first_checkpoint_of_every_interactive_start_once) {
    CHECK(splash::due(true, false));  // every start: a brand moment, however fast the start-up is
    CHECK(!splash::due(true, true));  // already open (or tried)
    CHECK(!splash::due(false, false)); // not enabled
}

TEST(splash_can_be_forced_with_sf_splash_1_whatever_the_preference) {
    CHECK(splash::forcedFromEnv("1"));
    CHECK(!splash::forcedFromEnv(nullptr));
    CHECK(!splash::forcedFromEnv(""));
    CHECK(!splash::forcedFromEnv("0"));
    CHECK(!splash::forcedFromEnv("yes"));
}

TEST(splash_stays_at_least_half_a_second_and_adds_nothing_to_a_slower_start) {
    CHECK(splash::kMinVisibleMs == 500.0);
    CHECK(splash::holdMs(0.0) == splash::kMinVisibleMs); // opened and the first frame is already there: stays the minimum
    CHECK(splash::holdMs(100.0) == splash::kMinVisibleMs - 100.0);
    CHECK(splash::holdMs(splash::kMinVisibleMs) == 0.0);
    CHECK(splash::holdMs(5000.0) == 0.0); // a slow start that showed it for seconds: no extra wait
    CHECK(splash::holdMs(-3.0) == splash::kMinVisibleMs); // (a clock that stepped back never waits longer than the minimum)
    CHECK(splash::kFadeMs > 0.0 && splash::kFadeMs <= 250.0); // the fade into the main window is short
}

TEST(splash_preference_is_on_unless_ui_json_turns_it_off) {
    CHECK(splash::preferenceFromUiJson(""));
    CHECK(splash::preferenceFromUiJson("not json {"));
    CHECK(splash::preferenceFromUiJson("{\"version\":1}"));
    CHECK(splash::preferenceFromUiJson("{\"prefs\":{\"libraryAtStartup\":false}}"));
    CHECK(splash::preferenceFromUiJson("{\"prefs\":{\"startupSplash\":true}}"));
    CHECK(!splash::preferenceFromUiJson("{\"prefs\":{\"startupSplash\":false}}"));
    CHECK(splash::languageFromUiJson("") == "en");
    CHECK(splash::languageFromUiJson("{\"prefs\":{\"language\":\"zh-TW\"}}") == "zh-TW");
}

namespace {
Image solid(int w, int h, uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
    Image img;
    img.width = w;
    img.height = h;
    img.rgba.resize(size_t(w) * h * 4);
    for (size_t i = 0; i < img.rgba.size(); i += 4) img.rgba[i] = r, img.rgba[i + 1] = g, img.rgba[i + 2] = b, img.rgba[i + 3] = a;
    return img;
}
} // namespace

TEST(splash_layout_is_bust_at_the_right_wordmark_and_version_at_the_left) {
    for (int w : {640, 1280, 2880}) {
        const int h = w * 9 / 16;
        const splash::Layout l = splash::layout(w, h, 4.3);
        CHECK(l.bustX > w / 2 && l.bustX + l.bustSize <= w && l.bustY >= 0 && l.bustY + l.bustSize <= h);
        CHECK(l.wordX + l.wordW < l.bustX); // the wordmark never reaches the bust
        CHECK(l.wordY >= 0 && l.wordY + l.wordH < h);
        CHECK(l.versionX == l.wordX && l.versionY > l.wordY + l.wordH && l.versionY < h);
    }
}

TEST(splash_picture_is_the_bust_and_wordmark_on_the_dark_background_and_nothing_else) {
    const Image mark = solid(40, 10, 0xf2, 0x8c, 0x3c);
    const Image bust = solid(64, 64, 0x30, 0x60, 0x90);
    for (int w : {640, 1280}) { // the window at 100% and 200% display scale
        const int h = w * 9 / 16;
        std::vector<uint8_t> rgba;
        splash::Layout l;
        splash::compose(mark, bust, w, h, rgba, l);
        CHECK(rgba.size() == size_t(w) * size_t(h) * 4);
        const auto at = [&](int x, int y) { return &rgba[(size_t(y) * w + x) * 4]; };
        CHECK(at(2, 2)[0] == splash::kBackground[0] && at(2, 2)[1] == splash::kBackground[1] && at(2, 2)[2] == splash::kBackground[2]);
        CHECK(at(w - 3, h - 3)[0] == splash::kBackground[0]);
        const uint8_t* b = at(l.bustX + l.bustSize / 2, l.bustY + l.bustSize / 2);
        CHECK(b[0] == 0x30 && b[1] == 0x60 && b[2] == 0x90);
        const uint8_t* m = at(l.wordX + l.wordW / 2, l.wordY + l.wordH / 2);
        CHECK(m[0] == 0xf2 && m[1] == 0x8c && m[2] == 0x3c);
        // between the wordmark and the bust, and under the version line's row: background only
        CHECK(at((l.wordX + l.wordW + l.bustX) / 2, h / 2)[0] == splash::kBackground[0]);
        CHECK(at(w / 4, h - 4)[0] == splash::kBackground[0]);
        CHECK(at(0, 0)[3] == 255);
    }
}

TEST(splash_picture_without_art_is_still_the_background) {
    std::vector<uint8_t> rgba;
    splash::Layout l;
    splash::compose(Image{}, Image{}, 64, 36, rgba, l);
    CHECK(rgba.size() == size_t(64) * 36 * 4);
    CHECK(rgba[0] == splash::kBackground[0]);
}
