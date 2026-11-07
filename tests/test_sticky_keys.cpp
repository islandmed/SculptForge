// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Windows' Sticky Keys prompt off while SculptForge has focus (app/StickyKeys.h), against a mock of
// SystemParametersInfo: only the shortcut flags of features that are off change, and every way out puts them back.
#include "TestHarness.h"

#include "app/StickyKeys.h"

#include <filesystem>
#include <fstream>

using namespace sf;
using namespace sf::stickykeys;

namespace {

Flags g_sys;      // the mock system's flags
int g_sets = 0;   // SPI_SET calls
bool g_fail = false;

bool mockGet(Flags& out) {
    out = g_sys;
    return true;
}
bool mockSet(const Flags& f) {
    ++g_sets;
    if (g_fail) return false;
    g_sys = f;
    return true;
}
const Spi kMock{mockGet, mockSet};

constexpr Flags kDefaults{0x1FE, 0x7E, 0x3E}; // Windows' defaults: all off, shortcuts and their prompts on

std::string stateFile() {
    const auto p = std::filesystem::temp_directory_path() / "sf_sticky_keys_test.txt";
    std::filesystem::remove(p);
    return p.string();
}

void reset(const Flags& sys) {
    stop();
    g_sys = sys;
    g_sets = 0;
    g_fail = false;
}

} // namespace

TEST(sticky_keys_quieted_clears_only_shortcut_flags) {
    const Flags q = quieted(kDefaults);
    CHECK(q.sticky == (kDefaults.sticky & ~kHotkeyBits));
    CHECK(q.filter == (kDefaults.filter & ~kHotkeyBits));
    CHECK(q.toggle == (kDefaults.toggle & ~kHotkeyBits));
    // A feature the user has on keeps every flag.
    const Flags on{kDefaults.sticky | kOn, kDefaults.filter, kDefaults.toggle | kOn};
    const Flags qOn = quieted(on);
    CHECK(qOn.sticky == on.sticky);
    CHECK(qOn.filter == (on.filter & ~kHotkeyBits));
    CHECK(qOn.toggle == on.toggle);
    CHECK(restored(q, kDefaults) == kDefaults);
    // Turned on meanwhile (Settings): left as the user set it.
    const Flags turnedOn{q.sticky | kOn, q.filter, q.toggle};
    CHECK(restored(turnedOn, kDefaults).sticky == turnedOn.sticky);
}

TEST(sticky_keys_focus_quiets_and_restores) {
    reset(kDefaults);
    const std::string file = stateFile();
    start(&kMock, file);
    CHECK(!recoveredAtStart());
    update(true); // focus
    CHECK(isQuiet());
    CHECK(g_sys == quieted(kDefaults));
    CHECK(std::filesystem::exists(file)); // kept for a hard kill
    const int sets = g_sets;
    update(true); // every frame: no more calls
    CHECK(g_sets == sets);
    update(false); // focus lost / minimised / preference off
    CHECK(!isQuiet());
    CHECK(g_sys == kDefaults);
    CHECK(!std::filesystem::exists(file));
    update(true); // focus again
    CHECK(g_sys == quieted(kDefaults));
    restore(); // quit
    CHECK(g_sys == kDefaults);
    CHECK(!std::filesystem::exists(file));
    stop();
}

TEST(sticky_keys_feature_on_is_never_touched) {
    const Flags allOn{kDefaults.sticky | kOn, kDefaults.filter | kOn, kDefaults.toggle | kOn};
    reset(allOn);
    const std::string file = stateFile();
    start(&kMock, file);
    update(true);
    CHECK(!isQuiet());
    CHECK(g_sets == 0);
    CHECK(g_sys == allOn);
    CHECK(!std::filesystem::exists(file));
    // Only Sticky Keys on: Filter and Toggle Keys' shortcuts go, Sticky Keys' flags stay.
    const Flags stickyOn{kDefaults.sticky | kOn, kDefaults.filter, kDefaults.toggle};
    reset(stickyOn);
    start(&kMock, file);
    update(true);
    CHECK(g_sys.sticky == stickyOn.sticky);
    CHECK(g_sys.filter == (kDefaults.filter & ~kHotkeyBits));
    restore();
    CHECK(g_sys == stickyOn);
    stop();
}

TEST(sticky_keys_crash_path_restores) {
    reset(kDefaults);
    const std::string file = stateFile();
    start(&kMock, file);
    update(true);
    CHECK(restoreFromCrash()); // the crash handlers, atexit and a console close
    CHECK(g_sys == kDefaults);
    CHECK(!std::filesystem::exists(file));
    CHECK(!restoreFromCrash()); // once
    stop();
}

TEST(sticky_keys_hard_kill_restored_at_next_start) {
    reset(kDefaults);
    const std::string file = stateFile();
    start(&kMock, file);
    update(true);
    CHECK(std::filesystem::exists(file));
    // Killed (no handler ran): the system keeps the quiet flags until sign-out; the next start puts them back.
    const Flags left = g_sys;
    CHECK(left == quieted(kDefaults));
    { // forget the session without restoring, as a killed process does
        const std::string keep = file + ".keep";
        std::filesystem::copy_file(file, keep, std::filesystem::copy_options::overwrite_existing);
        stop();
        std::filesystem::rename(keep, file);
    }
    g_sys = left;
    start(&kMock, file);
    CHECK(recoveredAtStart());
    CHECK(g_sys == kDefaults);
    CHECK(!std::filesystem::exists(file));
    stop();
    // A broken file is dropped, nothing changed.
    { std::ofstream(file) << "nonsense\n"; }
    g_sys = left;
    start(&kMock, file);
    CHECK(!recoveredAtStart());
    CHECK(g_sys == left);
    CHECK(!std::filesystem::exists(file));
    stop();
}

TEST(sticky_keys_failed_set_keeps_nothing_changed) {
    reset(kDefaults);
    const std::string file = stateFile();
    start(&kMock, file);
    g_fail = true;
    update(true);
    CHECK(!isQuiet());
    CHECK(g_sys == kDefaults);
    g_fail = false;
    stop();
}

TEST(sticky_keys_no_spi_does_nothing) {
    reset(kDefaults);
    const std::string file = stateFile();
    start(nullptr, file); // Linux, macOS
    update(true);
    CHECK(!isQuiet());
    CHECK(g_sets == 0);
    CHECK(!std::filesystem::exists(file));
    stop();
}
