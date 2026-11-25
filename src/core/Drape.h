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
    // Wind (Tool > Drape > Wind): its speed in m/s (0: none; 3 a breeze, 8 makes flags fly, 15 is hard to walk in),
    // where it blows toward as a heading (degrees: 0 away from the front view, along -Z; 90 to the right, +X) and a
    // lift (degrees up from level), and gusts 0..1 (the wind varies over place and time: the cloth keeps moving, so
    // the panel runs it longer and Keep takes the frame you like).
    float windSpeed = 0.0f;
    float windHeading = 0.0f;
    float windLift = 0.0f;
    float gusts = 0.0f;
};
// The request's wind as a velocity (m/s, the sculpt's axes), and the solver settings for it. In wind the cloth folds
// onto itself (a flag), so with the scene's mean edge given it also keeps particles half an edge apart; a still drape
// runs as it always has.
glm::vec3 drapeWind(const DrapeRequest& request);
ClothSettings drapeSettings(const DrapeRequest& request, float meanEdge = 0.0f);

// The active Part's simulation, ready to step: positions and colliders in metres (divided by `scale`).
struct DrapeScene {
    ClothSetup setup;
    ClothColliders colliders;
    Fabric fabric;
    float scale = 1.0f;
    float meanEdge = 0.0f; // the sheet's mean edge, metres (self-collision in wind keeps particles half of it apart)
};
// False (and `error`) when the active Part can't drape (empty, a micro level, Forge Spheres, a stroke running).
bool drapeScene(const Document& doc, const DrapeRequest& request, DrapeScene& out, std::string& error);
// Applies simulated positions (metres, one per vertex of the active Part) as one undo step "Drape". Returns how
// many vertices moved.
size_t applyDrape(Document& doc, const std::vector<glm::vec3>& metres, float scale, std::string& error);

// cloth_drape: drapes the active Part to rest (scripts, the agent, tests).
void registerDrapeCommands(CommandRegistry& registry);

} // namespace sf
