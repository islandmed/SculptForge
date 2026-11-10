// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Tube: draw clay tubes freely in 3D space, like a 3D pen. A stroke becomes a smooth curve (centripetal
// Catmull-Rom through control points, each with its own radius) and a tube is swept along it: rings placed
// adaptively (closer where the curve bends), oriented by rotation-minimizing frames (double reflection), so the
// tube never twists. The cross-section is round, square, triangular or a flat ribbon, turned and tapered as asked;
// the ends are round or flat. Rope and Braid are three strands round the curve, Sleeve is hollow with open ends; Twist
// and Spin turn the cross-section along the length. A Curve Part is the same data swept as a thin wire (only the guide
// line): Resample sets its point count exactly, and Tube from curve makes it a tube. A Tube Part keeps its curve in Mesh::tube and its triangles are
// made from it, so the points can be moved and resized until Bake makes it a plain, sculptable mesh. Merged into
// the active Part instead, the tube is swept with even edge lengths (rings with as many sides as their size
// needs), which suits dynamic topology. Every change is one undo step.
#pragma once

#include "core/Json.h"
#include "core/Mesh.h"

#include <string>
#include <vector>

namespace sf {

class CommandRegistry;
struct Document;
struct CommandResult;

constexpr size_t kTubeMaxPoints = 2000;      // control points per tube
constexpr size_t kTubeStrokePoints = 500;    // a drawn stroke is simplified to at most this many
constexpr uint32_t kTubeBodyGroup = 1;       // face groups: the body, the start cap, the end cap
constexpr uint32_t kTubeStartCapGroup = 2;   // (the mirror copy adds 3)
constexpr uint32_t kTubeEndCapGroup = 3;

// Empty when `shape` can be swept: 1 to kTubeMaxPoints points, finite, radii above 0, edge >= 0.
std::string tubeValidate(const TubeShape& shape);

// The edge length a tube is swept with: shape.edge, or 2*pi times the average radius over 16.
float tubeEdgeLength(const TubeShape& shape);

// A drawn stroke (dense pen samples with pressure-scaled radii) as control points: samples closer than a fraction
// of their radius are dropped, `smoothing` (0..1) relaxes positions and radii (the ends stay), and the result is
// resampled along its length to at most `maxPoints`. A stroke shorter than its radius becomes one point (a ball).
// `spacing`: how far apart the control points are, in radii (kTubeSpacingMin..Max).
constexpr float kTubeSpacingMin = 0.5f, kTubeSpacingMax = 8.0f;
std::vector<TubePoint> tubeFromStroke(const std::vector<TubePoint>& samples, float smoothing,
                                      size_t maxPoints = kTubeStrokePoints, float spacing = 1.25f);
// Fewer control points: takes out, one at a time, the interior point whose removal changes the curve least (its
// distance from its neighbours' chord plus its radius change, in its radius) until `keep` (0..1) of them are left
// (at least 2), or earlier when the next would change it by more than `tolerance` radii (0 = no limit).
std::vector<TubePoint> tubeSimplify(const std::vector<TubePoint>& points, float keep, float tolerance = 0.0f);

// Exactly `count` (2..kTubeMaxPoints) points evenly spaced along the smooth curve through `points`, radii blended
// between them; the first and last stay as they were. Empty for fewer than two distinct points.
std::vector<TubePoint> tubeResample(const std::vector<TubePoint>& points, size_t count);

// "round", "square", "triangle", "ribbon", "rope", "braid", "sleeve".
const char* tubeProfileName(TubeShape::Profile p);
bool parseTubeProfile(const std::string& name, TubeShape::Profile& out);

// A point of the swept centre line: position, radius and unit tangent.
struct TubeSample {
    glm::vec3 position{0.0f};
    float radius = 0.0f;
    glm::vec3 tangent{0.0f, 0.0f, 1.0f};
};
// The centre line rings are placed on: at most `edge` apart along it and closer where it turns (about 10 degrees
// between rings), from the first point to the last.
std::vector<TubeSample> tubeCenterline(const TubeShape& shape, float edge);

// The ring orientation along `line`: a unit normal per sample, at right angles to its tangent, carried along without
// twisting (rotation-minimizing frames). The first normal is `up` at right angles to the tangent (0: any).
std::vector<glm::vec3> tubeFrames(const std::vector<TubeSample>& line, const glm::vec3& up = glm::vec3(0.0f));

// The tube as a plain mesh (no Mesh::tube): watertight, face groups as above. `uniform`: every edge near the edge
// length (triangles; rings get as many sides as their radius needs) for dynamic topology; else rings with the
// same number of sides, in quads. `edge` <= 0: tubeEdgeLength(shape). Empty when the shape is invalid.
Mesh tubeSweep(const TubeShape& shape, bool uniform = false, float edge = 0.0f);
// The mesh a Tube Part shows: tubeSweep in quads, carrying `shape` in Mesh::tube. `maxTriangles` > 0: a coarser
// sweep when the full one would have more (the live preview while a point is dragged: 60 fps on the floor tier).
Mesh tubeMesh(const TubeShape& shape, size_t maxTriangles = 0);
constexpr size_t kTubePreviewTriangles = 60000;
// The shape mirrored across x = 0 (the mirror copy symmetry X sweeps; `up` mirrored and `roll` negated, so the
// copy's cross-section is the exact mirror image).
TubeShape tubeMirrored(const TubeShape& shape);

// Adds a tube to the scene as one undo step: a new Tube Part (not made active; undo removes it), or with `merge`
// swept with even edges (near the active Part's own edge length) into the active Part. `edited` is the Part that
// holds it now.
CommandResult tubeAdd(Document& doc, const TubeShape& shape, bool merge, const std::string& name = "Tube",
                      int* edited = nullptr);
// True when `mesh` is still the tube its Mesh::tube sweeps (not moved, sculpted or otherwise changed since): a
// tube is only re-swept from its points then, so nothing done to it afterwards is lost silently.
bool tubeMatchesMesh(const Mesh& mesh);
// Replaces the active Tube Part's curve (one undo step labelled `label`).
CommandResult tubeEdit(Document& doc, TubeShape shape, const std::string& label);

// "x,y,z,radius" per point, a flat array, plus the options; FromJson gives an empty shape for a bad one.
Json tubeToJson(const TubeShape& shape);
TubeShape tubeFromJson(const Json& json);
// Parses "x,y,z[,radius] x,y,z[,radius] ..." (points separated by spaces, ';' or '|'); points without a radius get
// `radius`.
bool parseTubePoints(const std::string& text, float radius, std::vector<TubePoint>& out, std::string& error);

// tube_new (curve=true: a Curve Part), tube_list, tube_set_point, tube_insert_point, tube_delete_point, tube_set,
// tube_simplify, tube_resample, tube_from_curve, tube_bake.
void registerTubeCommands(CommandRegistry& registry);

} // namespace sf
