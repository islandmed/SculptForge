// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/StickyKeys.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace sf::stickykeys {

namespace {

const Spi* g_spi = nullptr;
Flags g_saved;                    // the user's flags, taken just before they were changed
std::atomic<bool> g_quiet{false}; // changed now (the crash handlers read it)
bool g_want = false;              // update's last request
bool g_recovered = false;
std::string g_file;
// The file's name as the system takes it, for the crash handlers (no allocation there).
std::filesystem::path::value_type g_fileNative[1024] = {};

void quietOne(uint32_t& f) {
    if (!(f & kOn)) f &= ~kHotkeyBits;
}

void restoreOne(uint32_t& now, uint32_t saved) {
    if (!(now & kOn) && !(saved & kOn)) now = (now & ~kHotkeyBits) | (saved & kHotkeyBits);
}

void removeFileNoAlloc() noexcept {
    if (!g_fileNative[0]) return;
#if defined(_WIN32)
    DeleteFileW(g_fileNative);
#else
    std::remove(g_fileNative);
#endif
}

bool writeFile(const Flags& f) {
    if (g_file.empty()) return true;
    std::ofstream out(std::filesystem::path(std::u8string(g_file.begin(), g_file.end())), std::ios::binary | std::ios::trunc);
    out << "SculptForge accessibility shortcut flags, put back at the next start\n"
        << "sticky " << f.sticky << "\nfilter " << f.filter << "\ntoggle " << f.toggle << "\n";
    return bool(out.flush());
}

bool readFile(Flags& f) {
    std::ifstream in(std::filesystem::path(std::u8string(g_file.begin(), g_file.end())), std::ios::binary);
    if (!in) return false;
    std::string line;
    std::getline(in, line); // (the heading)
    int found = 0;
    std::string key;
    uint32_t value = 0;
    while (in >> key >> value) {
        if (key == "sticky") f.sticky = value, found |= 1;
        else if (key == "filter") f.filter = value, found |= 2;
        else if (key == "toggle") f.toggle = value, found |= 4;
    }
    return found == 7;
}

#if defined(_WIN32)
template <class T> bool getStruct(UINT action, T& s) {
    s = {};
    s.cbSize = UINT(sizeof s);
    return SystemParametersInfoW(action, UINT(sizeof s), &s, 0) != FALSE;
}

// Only dwFlags changes; the rest of each structure (Filter Keys' timings...) is written back as read.
template <class T> bool setFlags(UINT getAction, UINT setAction, uint32_t flags) {
    T s;
    if (!getStruct(getAction, s)) return false;
    if (s.dwFlags == flags) return true;
    s.dwFlags = flags;
    return SystemParametersInfoW(setAction, UINT(sizeof s), &s, 0) != FALSE; // fWinIni 0: never SPIF_UPDATEINIFILE
}

bool systemGet(Flags& out) {
    STICKYKEYS sk;
    FILTERKEYS fk;
    TOGGLEKEYS tk;
    if (!getStruct(SPI_GETSTICKYKEYS, sk) || !getStruct(SPI_GETFILTERKEYS, fk) || !getStruct(SPI_GETTOGGLEKEYS, tk))
        return false;
    out = {uint32_t(sk.dwFlags), uint32_t(fk.dwFlags), uint32_t(tk.dwFlags)};
    return true;
}

bool systemSet(const Flags& f) {
    const bool sticky = setFlags<STICKYKEYS>(SPI_GETSTICKYKEYS, SPI_SETSTICKYKEYS, f.sticky);
    const bool filter = setFlags<FILTERKEYS>(SPI_GETFILTERKEYS, SPI_SETFILTERKEYS, f.filter);
    const bool toggle = setFlags<TOGGLEKEYS>(SPI_GETTOGGLEKEYS, SPI_SETTOGGLEKEYS, f.toggle);
    return sticky && filter && toggle;
}

BOOL WINAPI onConsoleClose(DWORD) {
    restoreFromCrash();
    return FALSE; // the next handler (the default one ends the process)
}
#endif

// Windows' defaults (Settings > Accessibility > Keyboard, all off, shortcuts on).
constexpr Flags kWindowsDefaults{0x1FE, 0x7E, 0x3E};
Flags g_fake = kWindowsDefaults;
bool fakeGet(Flags& out) {
    out = g_fake;
    return true;
}
bool fakeSet(const Flags& f) {
    g_fake = f;
    return true;
}

void onExit() { restoreFromCrash(); }

} // namespace

Flags quieted(const Flags& f) {
    Flags q = f;
    quietOne(q.sticky);
    quietOne(q.filter);
    quietOne(q.toggle);
    return q;
}

Flags restored(const Flags& now, const Flags& saved) {
    Flags r = now;
    restoreOne(r.sticky, saved.sticky);
    restoreOne(r.filter, saved.filter);
    restoreOne(r.toggle, saved.toggle);
    return r;
}

const Spi* systemSpi() {
#if defined(_WIN32)
    static const Spi spi{systemGet, systemSet};
    return &spi;
#else
    return nullptr;
#endif
}

const Spi* fakeSpi() {
    static const Spi spi{fakeGet, fakeSet};
    return &spi;
}

void start(const Spi* spi, const std::string& stateFile) {
    stop();
    g_spi = spi;
    if (spi == fakeSpi()) g_fake = kWindowsDefaults; // each run (and warm server script) starts at the defaults
    g_file = spi ? stateFile : std::string();
    const std::filesystem::path p(std::u8string(g_file.begin(), g_file.end()));
    const auto& native = p.native();
    if (native.size() < std::size(g_fileNative)) std::copy(native.c_str(), native.c_str() + native.size() + 1, g_fileNative);
    else g_fileNative[0] = 0;
    if (!g_spi) return;

    // A session that ended without putting the flags back (killed): put them back now.
    std::error_code ec;
    if (!g_file.empty() && std::filesystem::exists(p, ec)) {
        Flags saved, now;
        if (readFile(saved) && g_spi->get(now)) {
            const Flags back = restored(now, saved);
            if (back != now) g_spi->set(back);
            g_recovered = true;
        }
        std::filesystem::remove(p, ec);
    }

    static bool installed = false;
    if (!installed) {
        installed = true;
        std::atexit(onExit);
#if defined(_WIN32)
        SetConsoleCtrlHandler(onConsoleClose, TRUE); // a console window closed, Ctrl+C, sign-out, shut down
#endif
    }
}

void update(bool quiet) {
    if (!g_spi || quiet == g_want) return;
    g_want = quiet;
    if (!quiet) {
        restore();
        return;
    }
    Flags now;
    if (!g_spi->get(now)) return;
    const Flags q = quieted(now);
    if (q == now) return; // every feature on, or the shortcuts already off: nothing to change, nothing to put back
    g_saved = now;
    if (!writeFile(now)) return; // no change the next start couldn't undo
    g_quiet = true;
    if (!g_spi->set(q)) restoreFromCrash(); // (g_want stays: not tried again every frame)
}

void restore() {
    g_want = false;
    restoreFromCrash();
}

bool restoreFromCrash() noexcept {
    if (!g_quiet.exchange(false) || !g_spi) return false;
    Flags now;
    bool back = false;
    if (g_spi->get(now)) {
        const Flags flags = restored(now, g_saved);
        back = flags == now || g_spi->set(flags);
    }
    if (back) removeFileNoAlloc(); // (kept otherwise: the next start tries again)
    return back;
}

bool isQuiet() { return g_quiet; }
bool recoveredAtStart() { return g_recovered; }

Flags current() {
    Flags f;
    if (g_spi) g_spi->get(f);
    return f;
}

void stop() {
    restore();
    g_spi = nullptr;
    g_file.clear();
    g_fileNative[0] = 0;
    g_recovered = false;
}

} // namespace sf::stickykeys
