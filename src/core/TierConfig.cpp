// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/TierConfig.h"

#include <algorithm>
#include <atomic>
#include <mutex>

namespace sf {

const char* powerSourceName(PowerSource power) {
    switch (power) {
    case PowerSource::AC: return "ac";
    case PowerSource::Battery: return "battery";
    case PowerSource::Unknown: break;
    }
    return "unknown";
}

int targetHzForDisplay(int hz) {
    if (hz >= 143) return 144;
    if (hz >= 119) return 120;
    return 60;
}

TierConfig tierConfigFor(const HardwareProfile& p, PowerSource power, int displayHz) {
    TierConfig c;
    c.tier = p.tier;
    c.power = power;
    const uint64_t ram = p.hardware.ramMB ? p.hardware.ramMB : 8192;
    const PerformanceSettings& s = p.settings;
    switch (p.tier) {
    case ProfileTier::Software:
        c.residentCacheCapMB = std::clamp<uint64_t>(ram / 8, 256, 1024);
        c.activeTriangleBudget = 500'000;
        break;
    case ProfileTier::Laptop:
        c.residentCacheCapMB = std::clamp<uint64_t>(ram / 4, 512, 4096);
        c.activeTriangleBudget = 2'000'000;
        break;
    case ProfileTier::Workstation:
        c.residentCacheCapMB = std::clamp<uint64_t>(ram * 2 / 5, 4096, 65536);
        c.activeTriangleBudget = std::clamp<uint64_t>(s.renderTriangleBudget / 2, 8'000'000, 64'000'000);
        break;
    }
    c.uploadBudgetMBPerFrame = std::max<uint64_t>(s.uploadBudgetMBPerFrame, 4);
    c.zeroCopyUMA = p.hardware.gpuKind == GpuKind::Unified || p.hardware.gpuKind == GpuKind::Integrated;
    c.draftBrushMode = p.tier != ProfileTier::Workstation;
    c.targetHz = p.overrides.targetFps ? p.overrides.targetFps : targetHzForDisplay(displayHz);
    if (p.tier == ProfileTier::Software) c.targetHz = std::min(c.targetHz, 60);
    if (c.onBattery()) {
        // On battery: less heat and power. Draft brushes, half the uploads, 60 Hz (unless set by hand).
        c.draftBrushMode = true;
        c.uploadBudgetMBPerFrame = std::max<uint64_t>(c.uploadBudgetMBPerFrame / 2, 4);
        if (!p.overrides.targetFps) c.targetHz = 60;
    }
    if (p.overrides.draftBrush == 1) c.draftBrushMode = true;
    else if (p.overrides.draftBrush == 2) c.draftBrushMode = false;
    return c;
}

Json tierConfigJson(const TierConfig& c) {
    Json j = Json::object();
    j.set("tier", tierName(c.tier));
    j.set("resident_cache_cap_mb", double(c.residentCacheCapMB));
    j.set("active_triangle_budget", double(c.activeTriangleBudget));
    j.set("upload_budget_mb_per_frame", double(c.uploadBudgetMBPerFrame));
    j.set("draft_brush_mode", c.draftBrushMode);
    j.set("zero_copy_uma", c.zeroCopyUMA);
    j.set("target_hz", c.targetHz);
    j.set("power", powerSourceName(c.power));
    j.set("on_battery", c.onBattery());
    return j;
}

namespace {
std::mutex g_mutex;
TierConfig g_config;
std::atomic<uint64_t> g_revision{0};
} // namespace

TierConfig currentTierConfig() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_config;
}

void setCurrentTierConfig(const TierConfig& config) {
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_config = config;
    }
    g_revision.fetch_add(1, std::memory_order_relaxed);
}

uint64_t tierConfigRevision() { return g_revision.load(std::memory_order_relaxed); }

} // namespace sf
