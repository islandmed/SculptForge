// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Whole-mesh deformations (Tool > Deformation, from the discussions): Polish, Relax, Smooth and Balloon, each over
// the whole active Part with an amount, the mask holding masked parts in place (partly masked ones move partly),
// hidden faces and open borders kept, and one undo step. They write through the Sculptor, so a sculpt layer that
// is being recorded takes the change as a stroke would.
//   Polish  - flattens fine noise while keeping planes and sharp edges: each vertex moves along its normal by a
//             bilateral average of its neighbours' heights (Fleishman, Drori and Cohen-Or 2003, one ring), so
//             small bumps average out and big steps (edges) barely count.
//   Relax   - evens out the spacing of the vertices along the surface without changing its shape: the Laplacian
//             step with its normal part removed.
//   Smooth  - Laplacian smoothing; with keepVolume, Taubin's lambda | mu steps, which don't shrink the form.
//   Balloon - pushes the surface out along its normals with a little smoothing each step, so the form fills out
//             toward a rounder volume (a negative amount lets the air out).
#pragma once

#include "core/Mesh.h"

#include <string>
#include <vector>

namespace sf {

class CommandRegistry;
struct Document;

enum class DeformOp { Polish, Relax, Smooth, Balloon };
const char* deformName(DeformOp op); // "Polish", "Relax", "Smooth", "Balloon"

struct DeformOptions {
    float amount = 50.0f;    // 0-100 (Balloon -100..100)
    bool keepVolume = false; // Smooth: Taubin smoothing
    bool useMask = true;     // false: the mask is ignored (hidden faces are still kept)
};

// The positions `mesh` gets from `op` (the mesh is left alone); every vertex, in order. Deterministic (the same
// result for any thread count).
std::vector<glm::vec3> deformPositions(const Mesh& mesh, DeformOp op, const DeformOptions& options);

// Runs `op` on the active Part of `doc` as one undo step. Returns how many vertices moved (0 with `error` set when
// nothing could: an empty Part, everything masked, a micro level, a stroke in progress).
size_t deformActivePart(Document& doc, DeformOp op, const DeformOptions& options, std::string& error);

// deform_polish, deform_relax, deform_smooth, deform_balloon.
void registerDeformCommands(CommandRegistry& registry);

} // namespace sf
