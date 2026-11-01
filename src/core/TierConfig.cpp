// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/TierConfig.h"

#include <cstdlib>
#include <mutex>

namespace sf {

TierConfig TierConfig::laptop() {
    TierConfig c;
    c.name = "Laptop";
    c.residentPatchCacheMB = 1024;
    c.activeTriangleBudget = size_t(2) << 20;
    c.continuousNormals = false;
    c.shadeBudgetPoints = size_t(1) << 20;
    c.outOfCorePoints = size_t(256) << 20; // 512 MB of tiles
    return c;
}

TierConfig TierConfig::workstation() { return TierConfig(); }

namespace {

std::mutex& tierMutex() {
    static std::mutex m;
    return m;
}

TierConfig& current() {
    static TierConfig c = [] {
        const char* e = std::getenv("SF_TIER");
        return e && std::string(e) == "laptop" ? TierConfig::laptop() : TierConfig::workstation();
    }();
    return c;
}

} // namespace

const TierConfig& tierConfig() {
    std::lock_guard<std::mutex> lock(tierMutex());
    return current();
}

void setTierConfig(const TierConfig& config) {
    std::lock_guard<std::mutex> lock(tierMutex());
    current() = config;
}

} // namespace sf
