// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Budgets for the two-tier engine (micro-displacement levels, core/Micro.h), by machine class. This branch only
// READS it: the hardware-profile work (feat/hw-profile) detects the machine and calls setTierConfig(); until then
// the Workstation defaults apply (or SF_TIER=laptop|workstation for testing).
#pragma once

#include <cstddef>
#include <string>

namespace sf {

struct TierConfig {
    std::string name = "Workstation";
    // Resident patch cache: the most memory out-of-core micro tiles may keep resident (MicroStore::trim). 0 = no cap.
    size_t residentPatchCacheMB = 0;
    // Active-triangle budget: the most triangles a frame draws for micro levels (render/MicroRenderer.h).
    size_t activeTriangleBudget = size_t(8) << 20;
    // Normal updates while sculpting: continuous (every frame, up to shadeBudgetPoints) or draft (a quarter of
    // that while the stroke runs; the rest when it ends).
    bool continuousNormals = true;
    size_t shadeBudgetPoints = size_t(3) << 20;
    // Micro levels larger than this many grid points go out of core (MicroStore; 0 = a quarter of RAM).
    size_t outOfCorePoints = 0;

    static TierConfig laptop();
    static TierConfig workstation();
};

const TierConfig& tierConfig();
void setTierConfig(const TierConfig& config);

} // namespace sf
