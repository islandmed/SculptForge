// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Garments, part 3 (docs/design/garments.md): the sewn pattern draped on a body. The user places each panel by
// clicking a point on the body (garment_place: the panel sits in front of it, facing the view it was placed from);
// the triangulated pattern starts there, rests at its flat shape (flatRest), its seams (GarmentMesh links) pulled
// closed by the cloth solver while it settles on the visible Parts (drapeToRest); garmentToMesh makes the Part.
// Nothing here computes a position from the body or sews anything: see "Do not build (patents)".
#pragma once

#include "core/Cloth.h"
#include "core/Garment.h"

#include <string>
#include <vector>

namespace sf {

class CommandRegistry;
struct Document;

namespace garment {

// The pattern's world units per metre (unitsPerMetre, or 1 when not chosen).
float patternScale(const Pattern& pat);
// A starting units-per-metre for a scene whose body is `bodyHeight` world units tall: 100 (centimetres, the starter
// meshes) above 20 units, else 1 (metres). Only a unit guess, shown to the user and editable.
float guessUnitsPerMetre(float bodyHeight);
// The wrap a panel starts with when placed: 300 degrees for a panel sewn to itself (a sleeve, a leg: a tube), else 0.
float defaultWrap(const Pattern& pat, uint32_t panel);
// Where the panel's sheet point `q` (mm) is in the world under its placement, `scale` world units per metre.
glm::vec3 placedPoint(const Panel& panel, glm::vec2 q, float scale);
// Every particle of `m` in the world (panels not placed: NaN). False (and `error`, naming the first one) when a
// panel isn't placed.
bool placedPositions(const Pattern& pat, const GarmentMesh& m, std::vector<glm::vec3>& world, std::string& error);

// The garment's simulation: particles at their placed positions (metres), resting at the flat pattern, the seams
// sewn, the visible Parts (all but Part id `skipPart`, the preview) as colliders.
struct GarmentDrapeScene {
    GarmentMesh mesh;
    ClothSetup setup;
    ClothColliders colliders;
    Fabric fabric;
    float scale = 1.0f; // world units per metre
};
bool garmentDrapeScene(const Document& doc, const Pattern& pat, GarmentDrapeScene& out, std::string& error,
                       uint32_t skipPart = 0);
// Simulation settings for a garment: finer substeps than a sheet (seams pull hard while they close).
ClothSettings garmentSettings(const Pattern& pat);
// The first frames sew with gravity off (the seams close where the panels were placed, nothing falls while they
// pull), then the garment settles under gravity.
constexpr int kSewFrames = 60;
// One frame of a garment drape (`frame` counted from 0, `base` from garmentSettings). True while moving; never at
// rest while it is still sewing.
bool garmentStep(ClothSim& sim, int frame, const ClothSettings& base, int sewFrames = kSewFrames);
// The whole drape (garment_drape, tests): sews, then steps until rest or `maxFrames`.
DrapeResult garmentDrapeToRest(const GarmentDrapeScene& scene, const ClothSettings& base, int maxFrames,
                               int sewFrames = kSewFrames);

// The panels as they are (seams open), in world units: the placed panels only (`world` from placedPositions, NaN
// = not placed), one face group per panel. What the view shows while placing and draping.
Mesh previewMesh(const GarmentMesh& m, const std::vector<glm::vec3>& world);
// The finished garment (welded, UVs, thickness: garmentToMesh) from simulated positions in metres, in world units.
Mesh drapedMesh(const Pattern& pat, const GarmentMesh& m, const std::vector<glm::vec3>& metres);
// Adds `mesh` as the Part "Garment" (or turns the preview Part `previewId` into it) as one undo step; the active
// Part stays active. Returns its index.
int addGarmentPart(Document& doc, Mesh mesh, uint32_t previewId = 0);

} // namespace garment

// garment_place, garment_drape (scripts, the agent, tests).
void registerGarmentDrapeCommands(CommandRegistry& registry);

} // namespace sf
