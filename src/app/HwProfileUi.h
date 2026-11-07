// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The app's hardware profile state (PanelPerformanceProfile.cpp, PanelHardwareTier.cpp): the profile in effect,
// calibration, the frame governor, the hardware tier's memory guard and first-run card.
#pragma once

#include "app/Calibration.h"

#include "core/FrameGovernor.h"
#include "core/HardwareProfile.h"
#include "core/HardwareTier.h"
#include "core/TierConfig.h"

#include <deque>
#include <memory>
#include <string>
#include <vector>

namespace sf {

struct HwProfileUi {
    HardwareProfile profile;
    bool active = false;           // a profile is applied: its threshold governs Fast Navigation
    bool saved = false;            // the profile came from (or went to) the cache
    std::unique_ptr<Calibrator> calibrator;
    std::string calibrationReason; // "first launch", "GPU changed", ...
    bool announce = false;         // say what calibration found in the status bar
    int calibrationFrames = 0;     // frames shown since calibration started (the first shows the overlay)
    FrameGovernor governor;
    GpuFrameTimer timer;
    uint64_t frame = 0;
    double frameStartMs = 0.0, drawStartMs = -1.0; // (drawStartMs: the 3D view's draw began; -1: not this frame)
    struct Pending {
        uint64_t frame;
        FrameSample sample;
        double cpuBeforeDrawMs; // the GPU waited through it inside the frame's timer query
    };
    std::deque<Pending> pending;   // frames waiting for their GPU time
    uint64_t revision = 0;         // performanceSettingsRevision() when last applied
    int displayHz = 0;
    PowerSource power = PowerSource::Unknown;
    double nextPowerCheck = 0.0;   // seconds (SDL ticks)
    TierConfig tier;               // what currentTierConfig() holds
    double lastCpuMs = 0.0, lastGpuMs = -1.0;
    bool overridesOpen = false;
    // Hardware tiers (PanelHardwareTier.cpp)
    TierMemoryGuard memoryGuard;   // steps the tier in effect down when free memory runs low
    double nextMemoryCheck = 0.0;  // seconds (SDL ticks)
    bool tierCard = false;         // the first-run tier card is showing
    std::vector<GpuAdapter> gpus;  // the graphics cards (listed when the panel first opens)
    bool gpusListed = false;
};

} // namespace sf
