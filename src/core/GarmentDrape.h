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

// The pattern's world units per metre (unitsPerMetre, set from core/Units.h sceneUnitsPerMetre at the first
// placing; 1 when not chosen).
float patternScale(const Pattern& pat);
// The wrap a panel starts with when placed: a panel sewn to itself (a sleeve, a leg) starts as a nearly closed tube,
// its own seam's edges 1.5 particle spacings apart (300..355 degrees, from the panel's width); others flat (0).
float defaultWrap(const Pattern& pat, uint32_t panel);
// The panel's own point that goes at the click (mm): across, the middle of its width; up, the top of its widest
// line (a bodice's chest line under the armholes, a sleeve's under its cap, trousers' hip). From the pattern only.
glm::vec2 panelAnchor(const Panel& panel);
// Where the panel's sheet point `q` (mm) is in the world under its placement, `scale` world units per metre
// (`anchor`: panelAnchor's answer, when it is known already; `normal`: the placed sheet's outward normal there).
glm::vec3 placedPoint(const Panel& panel, glm::vec2 q, float scale, const glm::vec2* anchor = nullptr,
                      glm::vec3* normal = nullptr);
// Every particle of `m` in the world (panels not placed: NaN), and with `normals` each one's outward normal. False (and `error`, naming the first one) when a
// panel isn't placed.
bool placedPositions(const Pattern& pat, const GarmentMesh& m, std::vector<glm::vec3>& world, std::string& error,
                     std::vector<glm::vec3>* normals = nullptr);

// The garment's simulation: particles at their placed positions (metres), resting at the flat pattern, the seams
// sewn, the visible Parts (all but Part id `skipPart`, the preview) as colliders.
struct GarmentDrapeScene {
    GarmentMesh mesh;
    ClothSetup setup;
    // Per particle, out of the body as placed (the panel's facing; out from the axis where it curls): which way a
    // particle that starts inside a body leaves it.
    std::vector<glm::vec3> outward;
    ClothColliders colliders;
    Fabric fabric;
    float scale = 1.0f; // world units per metre
};
bool garmentDrapeScene(const Document& doc, const Pattern& pat, GarmentDrapeScene& out, std::string& error,
                       uint32_t skipPart = 0);
// Simulation settings for a garment: finer substeps than a sheet (seams pull hard while they close).
ClothSettings garmentSettings(const Pattern& pat);
// The widest gap (world units) between two sewn particles, and the panels on its two sides.
struct SeamGap {
    float gap = 0.0f;
    uint32_t panelA = 0, panelB = 0;
};
SeamGap widestSeamGap(const GarmentDrapeScene& scene, const std::vector<glm::vec3>& metres);
// How many particles are inside a collider (behind the nearest surface): 0 when the garment is on the body.
size_t particlesInside(const GarmentDrapeScene& scene, const std::vector<glm::vec3>& metres);
// "widest seam gap 3 mm (Front / Left sleeve)" for the status line and garment_drape.
std::string seamGapText(const Pattern& pat, const GarmentDrapeScene& scene, const std::vector<glm::vec3>& metres);
// The first frames sew with gravity off (the seams close where the panels were placed, nothing falls while they
// pull), then the garment settles under gravity.
constexpr int kSewFrames = 60;
// How far a garment keeps off the body (m), at least: past the body's vertices between its particles.
constexpr float kGarmentContact = 0.007f;
// One frame of a garment drape (`frame` counted from 0, `base` from garmentSettings). True while moving; never at
// rest while it is still sewing.
bool garmentStep(ClothSim& sim, int frame, const ClothSettings& base, const Fabric& fabric, int sewFrames = kSewFrames);
// Velocity lost per frame while sewing (the seams' first pull is fast: damped, it closes without flinging cloth
// through the body).
inline float gSewDamping = 0.3f;
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
