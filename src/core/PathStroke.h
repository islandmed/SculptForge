// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Headless strokes: applies a brush along a path of world-space points, the
// way a cursor drag does in the viewport. Each dab lands where a ray along the
// view direction through the path point meets the active Part, so a path
// drawn in the camera's plane behaves like a screen-space stroke. Used by the
// `stroke` command (scripts, AI agents, brush preview renders) and tests.
#pragma once

#include "core/Document.h"

#include <cstdint>
#include <string>
#include <vector>

namespace sf {

struct PathStroke {
    std::vector<glm::vec3> points;   // the path (2+ points; 1 stamps a single dab)
    glm::vec3 view{0.0f, 0.0f, -1.0f}; // projection / camera direction (into the scene)
    float radius = 0.0f;   // world units; <= 0: 4% of the mesh's bounding box diagonal
    float strength = 0.5f; // 0..1
    float pressure = 1.0f; // constant pen pressure, 0..1
    bool invert = false;
    bool curve = true;     // pass a smooth Catmull-Rom curve through the points (else straight segments)
    float spacing = -1.0f; // dab spacing as a fraction of the radius; < 0: the recipe's (0.15 for built-ins)
    uint32_t seed = 1;     // recipe jitter is deterministic per seed
    Symmetry symmetry{false, false, false};
};

// Parses "x,y,z x,y,z ..." (points separated by spaces or '|'). False on malformed input.
bool parsePathPoints(const std::string& text, std::vector<glm::vec3>& out, std::string& error);
// Parses "x,y,z".
bool parseVec3(const std::string& text, glm::vec3& out);

// Samples the path at even `step` distance (curved through the points when `curve` is set).
std::vector<glm::vec3> samplePath(const std::vector<glm::vec3>& points, bool curve, float step);

// Strokes the active Part with `recipe` (when not null) or the built-in brush `type`,
// as one undoable step. Returns the number of dabs that hit the surface, or -1 on error.
int strokePath(Document& doc, const BrushRecipe* recipe, BrushType type, const PathStroke& stroke, std::string& error);

} // namespace sf
