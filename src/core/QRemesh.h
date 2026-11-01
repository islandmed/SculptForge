// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// QRemesh (Tool > Remesh / Tool > Geometry > QRemesh, like ZBrush's ZRemesher): automatic quad retopology. Two
// methods: QRemesh 2, the default (an integer-grid map, core/QRemesh2.cpp), and the legacy one described here,
// which shares the steps around it (symmetry, guides, pieces, attributes). The legacy method is a field-aligned method
// after Instant Field-Aligned Meshes (Jakob, Tarini, Panozzo, Sorkine-Hornung, SIGGRAPH Asia 2015; the
// reference code is BSD-3, this is a fresh implementation of the parts we need):
//
//  1. A multi-resolution hierarchy of the surface's vertex graph (greedy edge matching, ~halving per level).
//  2. A smooth 4-RoSy orientation field (where the quad edges point), solved coarse to fine.
//  3. A position field: per vertex the nearest corner of a local quad lattice with the wanted edge length,
//     made consistent across neighbours (also coarse to fine). Adaptive Size scales the lattice with the
//     curvature; polypaint can ask for more (red) or fewer (blue) polygons.
//  4. Extraction: vertices whose lattice corners coincide merge, lattice-neighbours become edges, the graph's
//     faces are traced in each vertex's tangent plane, and every face (quad, or the odd triangle / pentagon
//     around a field singularity) is split into quads, so the result is all quads and closed (2-manifold)
//     where the sculpt is.
//  5. The quads are relaxed and snapped back onto the sculpt; the target count is met by adjusting the
//     lattice size and extracting again.
//
// QRemesh Guides (curves drawn on the surface) constrain the fields the same way: a lattice line along each
// curve, and the orientation turned towards it over a falloff (core/QRemeshGuides.h).
// Keep Groups constrains the fields to the polygroup borders, so edge loops run along them (and the result
// keeps the groups). Symmetry remeshes one half (cut at the plane) and mirrors it, welded on the seam; the plane
// is the model's own (found from it, so a model away from the origin works), and a model that isn't
// mirror-symmetric is remeshed whole instead. Separate pieces are kept: small ones (eyes, rivets) are remeshed
// on their own with a few dozen quads at least. Parts thinner than a quad keep both sides (clusters facing
// apart don't merge) and hard edges keep their loops and corners.
#pragma once

#include "core/Mesh.h"
#include "core/QRemeshGuides.h"

#include <functional>
#include <string>

namespace sf {

class CommandRegistry;
struct Document;

// QRemesh's methods. V2 (the default): a curvature-aligned cross field and an integer-grid map, poles only where
// the field needs them, guides as exact edge loops, the result at the target density (core/QRemesh2.cpp).
// Legacy: the 0.9 method (Instant Meshes style position field, extracted at twice the spacing and split).
enum class QRemeshMethod { V2 = 0, Legacy = 1 };

struct QRemeshOptions {
    QRemeshMethod method = QRemeshMethod::V2;
    int targetQuads = 5000;  // ZBrush's Target Polygons Count (here in polygons, not thousands)
    float adaptive = 0.5f;   // Adaptive Size: 0 = even quads, 1 = sizes follow the curvature strongly
    bool keepGroups = false; // edge loops along polygroup borders; the result keeps the groups
    bool detectEdges = true; // hard edges (faces meeting at more than 50 degrees) get an edge loop
    bool usePolypaint = false; // red polypaint: up to 4x the density, blue: down to 1/4
    int symmetryAxis = -1;   // -1 none, 0 X, 1 Y, 2 Z
    // The mirror plane's position along the axis. symmetryAuto: the plane is found from the model (the world
    // origin's plane when the model is symmetric about it, else the model's own middle, as for an off-centre
    // model), otherwise symmetryCenter is used as given.
    bool symmetryAuto = true;
    float symmetryCenter = 0.0f;
    int smoothIterations = 6;
    uint32_t seed = 1;       // the field's random start (the result is deterministic for a seed)
    // QRemesh Guides (core/QRemeshGuides.h), in the source's space: an edge loop runs along each curve and the
    // edge flow turns to follow it. guideStrength (ZBrush's Curves Strength, 0..1): how far around a curve the
    // flow follows it (0 = only the loop on the curve itself, 1 = about six quads to each side).
    std::vector<QRemeshGuide> guides;
    float guideStrength = 0.5f;
    // Measure the result against the source after remeshing (QRemeshStats::quality): poles, valences, angles and the
    // distance to the sculpt. Costs a closest-point query per vertex of both.
    bool measureQuality = true;
};

// How good a quad mesh is (the quality report shown after QRemesh).
struct QuadQuality {
    size_t vertices = 0, quads = 0, triangles = 0;
    size_t poles = 0;               // inside vertices with other than four edges
    size_t valence[8] = {};         // inside vertices by edge count: [2] .. [6], [7] = 7 or more
    size_t boundaryPoles = 0;       // vertices on open edges with other than three edges
    float angleDeviation = 0.0f;    // mean |corner angle - 90| over the quads, degrees
    float angleDeviationMax = 0.0f; // 99th percentile, degrees
    float edgeLengthCv = 0.0f;      // spread of the edge lengths (standard deviation / mean)
    float hausdorff = 0.0f;         // symmetric Hausdorff distance to the source, in % of the bounding-box diagonal
    float meanDistance = 0.0f;      // mean distance to the source, same units
    bool measured = false;          // distances filled in
};
// Valences and angles of `mesh` (faces as exported); with `source`, also the distances to it.
QuadQuality quadQuality(const Mesh& mesh, const Mesh* source = nullptr);

struct QRemeshStats {
    size_t quads = 0, triangles = 0; // faces of the result (triangles only for degenerate leftovers)
    size_t boundaryEdges = 0, nonManifoldEdges = 0;
    int attempts = 0;      // extractions run to approach the target count
    float edgeLength = 0;  // mean quad edge length
    int symmetryAxis = -1;       // the axis asked for (-1: none)
    bool symmetryUsed = false;   // false when symmetry was asked for but the model isn't mirror-symmetric
    float symmetryCenter = 0.0f; // the plane used (with symmetryUsed)
    float asymmetry = 0.0f;      // the model's mirror mismatch about that plane, in mean quad edges
    size_t pieces = 0;           // separate pieces of the source (small ones are remeshed on their own)
    size_t guides = 0;           // QRemesh Guides used
    size_t guidesSkipped = 0;    // guides left out: mostly off the surface (the SubTool moved since)
    double seconds = 0;
    QRemeshMethod method = QRemeshMethod::V2;
    size_t fallbackPieces = 0;   // V2: pieces it couldn't do well, remeshed with the legacy method
    size_t singularities = 0;    // V2: cross-field singularities (poles the layout needs)
    QuadQuality quality;         // with measureQuality
};

// Fraction 0..1 of the whole job; return false to cancel. Called from the thread running qremesh.
using QRemeshProgress = std::function<bool(float fraction)>;

// Retopologises `source` (its evaluated positions: sculpt layers are baked; hidden parts are remeshed too)
// into `out`: all quads (as triangle pairs, Mesh::quadPartners), mask and polypaint interpolated from the
// sculpt, polygroups with Keep Groups. False with `error` set on failure or cancel.
bool qremesh(const Mesh& source, const QRemeshOptions& options, Mesh& out, std::string& error,
             const QRemeshProgress& progress = {}, QRemeshStats* stats = nullptr);

// Edge-manifold check used by the tests and the status line: faces as exported (quads counted once).
struct TopologyReport {
    size_t faces = 0, quads = 0, triangles = 0;
    size_t boundaryEdges = 0;     // edges with one face
    size_t nonManifoldEdges = 0;  // edges with more than two faces, or two faces wound the same way
    size_t nonManifoldVertices = 0; // vertices whose faces form more than one fan
    bool manifold() const { return nonManifoldEdges == 0 && nonManifoldVertices == 0; }
    float quadFraction() const { return faces ? float(quads) / float(faces) : 0.0f; }
};
TopologyReport topologyReport(const Mesh& mesh);

// Puts a qremesh result into the active SubTool as one undo step. keepOriginal: first a hidden copy of the
// SubTool as it was goes right below it ("<name> Original"), ready to ProjectAll the detail back from.
// Returns the status message.
std::string applyQRemesh(Document& doc, Mesh result, bool keepOriginal, const QRemeshStats& stats);
// The quality report as text for the status line ("" when empty).
std::string qualitySummary(const QuadQuality& quality);

// qremesh (core/RetopoCommands.cpp) and project_all (core/Projection.h).
void registerRetopoCommands(CommandRegistry& registry);

} // namespace sf
