// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Drape (docs/design/cloth.md, phase 2): the active Part simulated as cloth (core/Cloth.h) until it rests, the other
// visible Parts as colliders, the mask as pins. The result is the Part's new shape, one undo step.
#pragma once

#include "core/Cloth.h"

#include <atomic>
#include <string>
#include <vector>

namespace sf {

class CommandRegistry;
struct Document;

struct DrapeRequest {
    FabricPreset fabric = FabricPreset::Cotton;
    // World units per metre of the sculpt (1: modelled at real size, a SculptForge unit is a metre). A body
    // sculpted 2 units tall as a 1.8 m person: 1.11.
    float scale = 1.0f;
    int maxFrames = 600;
    bool useMask = true;  // masked vertices are pins (partly masked: soft pins)
    bool collide = true;  // the other visible Parts are colliders
};

// The active Part's simulation, ready to step: positions and colliders in metres (divided by `scale`).
struct DrapeScene {
    ClothSetup setup;
    ClothColliders colliders;
    Fabric fabric;
    float scale = 1.0f;
};
// False (and `error`) when the active Part can't drape (empty, a micro level, Forge Spheres, a stroke running).
bool drapeScene(const Document& doc, const DrapeRequest& request, DrapeScene& out, std::string& error);
// Applies simulated positions (metres, one per vertex of the active Part) as one undo step "Drape". Returns how
// many vertices moved.
size_t applyDrape(Document& doc, const std::vector<glm::vec3>& metres, float scale, std::string& error);

// cloth_drape: drapes the active Part to rest (scripts, the agent, tests).
void registerDrapeCommands(CommandRegistry& registry);

} // namespace sf
