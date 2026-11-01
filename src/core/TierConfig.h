// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The tier's knobs for the renderer, brushes and streaming code (feat/micromesh, feat/zero-latency read these):
// what the hardware profile (core/HardwareProfile.h) gives, adjusted for the power source (battery: draft brushes,
// smaller uploads, 60 Hz) and the display (target 60, 120 or 144 Hz).
//
//                      Software (safe)   Laptop / Budget          Workstation
//   resident cache     RAM / 8, <= 1 GB  RAM / 4, <= 4 GB         RAM * 2 / 5, <= 64 GB
//   active triangles   0.5M              2M                       8M+ (half the render budget, <= 64M)
//   draft brushes      yes               yes                      no (yes on battery)
//   zero-copy UMA      the GPU shares system memory (Apple Silicon, Asahi, integrated GPUs)
//   target Hz          60                display's (60 on battery) display's: 60 / 120 / 144
//
// Read it with currentTierConfig() (thread-safe, cheap) every frame or stroke; tierConfigRevision() changes
// whenever it does (a new profile, the power cable, another display). Nothing here changes the model's data.
#pragma once

#include "core/HardwareProfile.h"
#include "core/Json.h"

#include <cstdint>

namespace sf {

enum class PowerSource { Unknown, AC, Battery };
const char* powerSourceName(PowerSource power); // "unknown", "ac", "battery"

struct TierConfig {
    ProfileTier tier = ProfileTier::Laptop;
    uint64_t residentCacheCapMB = 2048;   // most memory resident caches (micromesh pages, streamed levels) may hold
    uint64_t activeTriangleBudget = 2'000'000; // triangles a brush / micro-displacement may keep "active" (full
                                          // detail, updated live) at once; the rest waits or uses coarser levels
    uint64_t uploadBudgetMBPerFrame = 64; // vertex data to send to the GPU per frame before deferring the rest
    bool draftBrushMode = true;           // defer normal and cavity recomputation to pen release (Laptop, battery);
                                          // false: continuous, every dab (Workstation)
    bool zeroCopyUMA = false;             // the GPU shares system memory: uploads may write in place (persistent
                                          // mapped buffers) instead of staging copies
    int targetHz = 60;                    // 60, 120 or 144: what the display shows (60 on battery, Software)
    PowerSource power = PowerSource::Unknown;
    // Micro-displacement levels (core/Micro.h): grid points of shading (normals, cavity) the renderer recomputes per
    // frame at most (a quarter of it while a stroke runs in draft mode; the rest follows), and the size in grid
    // points above which a level is kept out of core (MicroStore; 0 = when it would take a quarter of RAM).
    uint64_t shadeBudgetPoints = 3u << 20;
    uint64_t outOfCorePoints = 0;
    bool onBattery() const { return power == PowerSource::Battery; }
};

// 144 for displays of 143 Hz and up, 120 from 119 Hz, else 60 (also when unknown: 0).
int targetHzForDisplay(int displayHz);
// The knobs for `profile` on this power source and display (displayHz 0 = unknown). Deterministic.
TierConfig tierConfigFor(const HardwareProfile& profile, PowerSource power, int displayHz);
Json tierConfigJson(const TierConfig& config);

// The configuration in effect (the app sets it from the profile, the power source and the display).
TierConfig currentTierConfig();
void setCurrentTierConfig(const TierConfig& config);
uint64_t tierConfigRevision();

} // namespace sf
