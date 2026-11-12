// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Fine detail for AI agents and scripts (pores, skin, wrinkles, scales, scars), built on plain brush dabs: no
// generated geometry, nothing random that a seed doesn't fix, each call one undo step.
//   - Detail presets: names the `stroke` command (and live mode's stroke_screen) takes as a brush ("Detail Pores",
//     "Detail Wrinkle", "Detail Crease", "Detail Scales", "Detail Skin"): a brush type, a built-in alpha and sizes
//     that read as detail on a head, used for that stroke only (the user's own alpha stays as it is).
//   - detail_pass: one kind of detail over a region (a ball around a point, or the whole Part): dabs or short
//     strokes scattered evenly on the surface, each pressed along the surface's own normal there. It refuses, with
//     the subdivision that would do, when the mesh is too coarse to hold the detail asked for.
//   - closeupFraming: where a close-up picture of a point looks from (along the surface normal) and what the mesh
//     there can hold (triangles in view, the finest feature), for viewport tools (cli/McpTools.h, live mode).
#pragma once

#include "core/Sculpt.h"

#include <glm/glm.hpp>

#include <string>
#include <string_view>
#include <vector>

namespace sf {

class CommandRegistry;
struct Document;
struct Mesh;

struct DetailPreset {
    const char* name;  // "Detail Pores"
    const char* kind;  // detail_pass's kind it matches ("pores"), or a stroke-only one ("crease")
    BrushType type;
    const char* alpha; // built-in alpha name, "" = round
    float size;        // radius as a fraction of the Part's size (bounding-box diagonal)
    float strength;
    bool invert;       // carve (true) or build
    float spacing;     // dab spacing along a stroke, in radii
    const char* use;   // one line: what it's for
};

const std::vector<DetailPreset>& detailPresets();
// By name, ignoring case, spaces, '_' and '-' ("detail_pores", "Detail Pores"); null when it isn't one.
const DetailPreset* findDetailPreset(std::string_view name);
// "Detail Pores, Detail Skin, ..." for descriptions and messages.
std::string detailPresetNames();

struct DetailPassOptions {
    std::string kind = "pores";     // pores, skin, wrinkles, scales, scars
    bool whole = true;              // the whole Part; false: the ball (center, radius)
    glm::vec3 center{0.0f};         // the region, in the Part's space
    float radius = 0.0f;
    float density = 0.5f;           // 0..1: how much of the region it covers
    float size = 0.0f;              // dab radius as a fraction of the Part's size; <= 0: the kind's own
    float strength = -1.0f;         // < 0: the kind's own
    bool invert = false;            // the other way (raised pores, carved scales)
    glm::vec3 direction{0.0f};      // wrinkles and scales: their run on the surface (zero: across, horizontal)
    uint32_t seed = 1;
    Symmetry symmetry{false, false, false};
};

// Applies one detail pass to doc's active Part as one undo step. Returns the dabs placed (>= 1), or -1 with `error`.
// `message` says what it did, in numbers an agent can check.
int detailPass(Document& doc, const DetailPassOptions& options, std::string& message, std::string& error);

// The median edge length of the triangles with a corner within `radius` of `center` (radius <= 0: every triangle;
// a sample of at most 50k triangles). 0 for an empty mesh or region.
float medianEdgeNear(const Mesh& mesh, const glm::vec3& center, float radius);

// Subdivision levels (each halves the edges) that bring edges of `edge` down to `want`; 0 when already there.
int levelsToReach(float edge, float want);

struct Closeup {
    glm::vec3 point{0.0f};  // the surface point nearest the one asked for (scene space)
    glm::vec3 eye{0.0f, 0.0f, 1.0f}; // unit direction from the point toward the camera
    std::string part;       // the Part it is on
    size_t triangles = 0;   // triangles within the view's half span
    float edge = 0.0f;      // their median edge (scene units)
};
// Frames a close-up of the visible Parts around `point` (scene space), `span` wide: the nearest surface point, the
// camera along its normal (or along `eye` when it isn't zero), and what the mesh there holds. False when nothing
// visible is there.
bool closeupFraming(const Document& doc, const glm::vec3& point, float span, const glm::vec3& eye, Closeup& out,
                    std::string& error);
// One line on a close-up for the agent: what's in view and the finest detail the mesh can show there.
std::string closeupText(const Closeup& c, float span);

// detail_pass.
void registerDetailCommands(CommandRegistry& registry);

} // namespace sf
