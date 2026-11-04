// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Wintab pressure (see Wintab.h). The few Wintab 1.4 types and constants used are declared here from the public
// specification; nothing is linked: Wintab32.dll is loaded at run time from System32, where tablet drivers put it.
#include "app/Wintab.h"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
#endif

namespace sf {

#if defined(_WIN32)

namespace {

// Wintab 1.4 (public specification).
using WTPKT = DWORD;
using FIX32 = DWORD;
using HCTX = HANDLE;
struct AXIS {
    LONG axMin, axMax;
    UINT axUnits;
    FIX32 axResolution;
};
struct LOGCONTEXTW {
    WCHAR lcName[40];
    UINT lcOptions, lcStatus, lcLocks, lcMsgBase, lcDevice, lcPktRate;
    WTPKT lcPktData, lcPktMode, lcMoveMask;
    DWORD lcBtnDnMask, lcBtnUpMask;
    LONG lcInOrgX, lcInOrgY, lcInOrgZ, lcInExtX, lcInExtY, lcInExtZ;
    LONG lcOutOrgX, lcOutOrgY, lcOutOrgZ, lcOutExtX, lcOutExtY, lcOutExtZ;
    FIX32 lcSensX, lcSensY, lcSensZ;
    BOOL lcSysMode;
    int lcSysOrgX, lcSysOrgY, lcSysExtX, lcSysExtY;
    FIX32 lcSysSensX, lcSysSensY;
};
constexpr UINT WTI_DEFSYSCTX = 4, WTI_DEVICES = 100, DVC_NPRESSURE = 15;
constexpr UINT CXO_SYSTEM = 0x0001;
constexpr WTPKT PK_CURSOR = 0x0020, PK_BUTTONS = 0x0040, PK_NORMAL_PRESSURE = 0x0400;
// A packet with PK_CURSOR | PK_BUTTONS | PK_NORMAL_PRESSURE: the fields in bit order.
struct Packet {
    UINT cursor;
    DWORD buttons;
    UINT pressure;
};

using WTInfoWFn = UINT(WINAPI*)(UINT, UINT, LPVOID);
using WTOpenWFn = HCTX(WINAPI*)(HWND, LOGCONTEXTW*, BOOL);
using WTCloseFn = BOOL(WINAPI*)(HCTX);
using WTPacketsGetFn = int(WINAPI*)(HCTX, int, LPVOID);

template <class F> F proc(void* lib, const char* name) {
    return reinterpret_cast<F>(reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(lib), name)));
}

} // namespace

bool WintabPressure::open(SDL_Window* window) {
    if (ctx_) return true;
    if (!window) return false;
    if (!lib_) lib_ = LoadLibraryExW(L"Wintab32.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!lib_) return false;
    const auto info = proc<WTInfoWFn>(lib_, "WTInfoW");
    const auto openCtx = proc<WTOpenWFn>(lib_, "WTOpenW");
    if (!info || !openCtx || !proc<WTPacketsGetFn>(lib_, "WTPacketsGet") || !proc<WTCloseFn>(lib_, "WTClose")) return false;
    const auto hwnd = static_cast<HWND>(
        SDL_GetPointerProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
    if (!hwnd) return false;
    LOGCONTEXTW lc{};
    if (info(WTI_DEFSYSCTX, 0, &lc) == 0) return false; // no tablet (yet): tried again when a device is added
    AXIS pressure{};
    if (info(WTI_DEVICES, DVC_NPRESSURE, &pressure) != 0 && pressure.axMax > pressure.axMin)
        pressureMax_ = pressure.axMax;
    lc.lcOptions |= CXO_SYSTEM; // the driver keeps moving the cursor
    lc.lcPktData = PK_CURSOR | PK_BUTTONS | PK_NORMAL_PRESSURE;
    lc.lcPktMode = 0; // absolute
    lc.lcMoveMask = PK_BUTTONS | PK_NORMAL_PRESSURE;
    ctx_ = openCtx(hwnd, &lc, TRUE);
    if (ctx_) SDL_Log("Wintab: pressure from the tablet driver (levels %ld)", pressureMax_ + 1);
    return ctx_ != nullptr;
}

void WintabPressure::close() {
    if (ctx_ && lib_)
        if (const auto closeCtx = proc<WTCloseFn>(lib_, "WTClose")) closeCtx(static_cast<HCTX>(ctx_));
    ctx_ = nullptr;
    if (lib_) FreeLibrary(static_cast<HMODULE>(lib_));
    lib_ = nullptr;
}

bool WintabPressure::poll(Sample& out) {
    if (!ctx_) return false;
    const auto get = proc<WTPacketsGetFn>(lib_, "WTPacketsGet");
    if (!get) return false;
    Packet packets[64];
    int n = 0, last = 0;
    while ((n = get(static_cast<HCTX>(ctx_), 64, packets)) > 0) last = n; // drain; the newest is what counts
    if (last == 0) return false;
    const Packet& p = packets[last - 1];
    out.pressure = std::clamp(float(p.pressure) / float(std::max(pressureMax_, 1L)), 0.0f, 1.0f);
    out.tip = (p.buttons & 1u) != 0 || out.pressure > 0.0f;
    out.eraser = p.cursor % 3 == 2; // cursor types repeat per tablet: puck, pen, eraser
    return true;
}

#else

bool WintabPressure::open(SDL_Window*) { return false; }
void WintabPressure::close() {}
bool WintabPressure::poll(Sample&) { return false; }

#endif

} // namespace sf
