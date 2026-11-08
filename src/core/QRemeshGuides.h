// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// QRemesh Guides (QRemesh Guides brush): curves drawn on the surface that QRemesh's edge loops
// follow, around the eyes, the mouth, a joint. Each Part keeps its own (Document::qremeshGuides while it is
// active, Part::qremeshGuides otherwise) and projects save them ("qremesh_guides" in a Part's manifest
// entry, so older versions ignore them and older projects open without any).
//
// QRemesh (core/QRemesh.cpp) uses them twice: the cross field turns to follow every curve, with a falloff
// over a few quads that Guide Strength widens, and the quad lattice gets a line along each curve, so one edge
// loop runs right on it.
#pragma once

#include "core/Json.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace sf {

class BVH;
class CommandRegistry;
class Mesh;
struct Document;
struct Symmetry;

struct QRemeshGuide {
    std::vector<glm::vec3> points; // on the surface, in the Part's space, evenly spaced
    bool closed = false;           // a loop: the last point joins the first
    uint32_t stroke = 0;           // guides drawn together (a curve and its mirror) share a stroke number
};

// Turns a drawn path (points on or near the surface) into guides: the path is resampled every `step` (<= 0:
// 0.5% of the Part's size), snapped onto the surface, closed when it ends near where it started, and copied
// across each mirror plane of `symmetry` (planes through the world's centre, as the brushes mirror). Appends to
// `guides` with a new stroke number; returns how many guides were added (0: the path was too short).
int addQRemeshGuide(std::vector<QRemeshGuide>& guides, const Mesh& mesh, const BVH& bvh,
                    const std::vector<glm::vec3>& path, const Symmetry& symmetry, float step = 0.0f);
// Removes the guides of the last stroke; false when there are none.
bool deleteLastQRemeshGuide(std::vector<QRemeshGuide>& guides);
// Total length of the guides (for the status line).
float qremeshGuideLength(const std::vector<QRemeshGuide>& guides);

// Projects: [{"closed": bool, "stroke": n, "points": [x, y, z, x, y, z, ...]}, ...]. Bad entries are skipped.
Json qremeshGuidesToJson(const std::vector<QRemeshGuide>& guides);
std::vector<QRemeshGuide> qremeshGuidesFromJson(const Json& json);

// qremesh_guide, qremesh_guide_delete_last, qremesh_guides_clear, qremesh_guides_info.
void registerQRemeshGuideCommands(CommandRegistry& registry);

} // namespace sf
