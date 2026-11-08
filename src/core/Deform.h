// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Whole-mesh deformations (Tool > Reshape, from the discussions): Smooth, Relax, Polish and Balloon, each over
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
// And shape operations (docs/design/deform-panel.md, foirgairwolf on Discord), each about a pivot (the Part's bounds centre
// by default) along the chosen axes, borders moving with the rest:
//   Transform    - Move, Turn, Scale, Squash (scale along an axis, keeping the volume).
//   Bend & Twist - Bend, Twist, Taper, Shear, each along an axis; Soft eases the effect out toward the ends.
//   Shape        - Round (toward a sphere or cylinder), Box, Flatten (cut one side flat), Sag (outer parts droop
//                  along -axis), Depth (a perspective along the axis).
// Several run in order as one step (deformStackPositions, the deform_stack command).
#pragma once

#include "core/Mesh.h"

#include <cstdint>
#include <string>
#include <vector>

namespace sf {

class CommandRegistry;
struct Document;

enum class DeformOp {
    Polish, Relax, Smooth, Balloon,                // Surface
    Move, Turn, Scale, Squash,                     // Transform
    Bend, Twist, Taper, Shear,                     // Bend & Twist
    Round, Box, Flatten, Sag, Depth,               // Shape
};
constexpr int kDeformOpCount = 17;
const char* deformName(DeformOp op);    // "Polish", ... "Depth"
const char* deformCommand(DeformOp op); // "deform_polish", ... "deform_depth"
bool deformFromName(const std::string& name, DeformOp& op); // a name or command, any case
// The amount's range and default ("Turn" in degrees, the rest mostly percent).
void deformRange(DeformOp op, float& lo, float& hi, float& def);
bool deformUsesDirection(DeformOp op); // Bend, Shear
bool deformUsesSoft(DeformOp op);      // Bend, Twist, Taper, Shear, Flatten
bool deformUsesAxes(DeformOp op);      // every shape operation (not Polish, Relax, Smooth, Balloon)

enum DeformAxis : uint8_t { DeformX = 1, DeformY = 2, DeformZ = 4 };

struct DeformOptions {
    float amount = 50.0f;    // see deformRange
    bool keepVolume = false; // Smooth: Taubin smoothing
    bool useMask = true;     // false: the mask is ignored (hidden faces are still kept)
    uint8_t axes = DeformY;  // DeformAxis bits; shape operations only
    float direction = 0.0f;  // Bend, Shear: degrees about the axis (0: +Y for X and Z, +X for Y)
    bool soft = false;       // Bend, Twist, Taper, Shear, Flatten: eased profile
    bool hasPivot = false;   // false: the Part's bounds centre
    glm::vec3 pivot{0.0f};
};

struct DeformStep {
    DeformOp op = DeformOp::Bend;
    DeformOptions options;
};
// "bend 40 y soft dir=90; twist 90 xz" (axes letters, "soft", "dir=", ";" between steps) <-> steps.
bool parseDeformStack(const std::string& text, std::vector<DeformStep>& steps, std::string& error);
std::string deformStackText(const std::vector<DeformStep>& steps);
std::string axesLetters(uint8_t axes); // "xz"

// The positions `mesh` gets from `op` (the mesh is left alone); every vertex, in order. Deterministic (the same
// result for any thread count).
std::vector<glm::vec3> deformPositions(const Mesh& mesh, DeformOp op, const DeformOptions& options);
// Every step in order, each on the result of the one before (each with its own mask setting).
std::vector<glm::vec3> deformStackPositions(const Mesh& mesh, const std::vector<DeformStep>& steps);

// Runs `op` on the active Part of `doc` as one undo step. Returns how many vertices moved (0 with `error` set when
// nothing could: an empty Part, everything masked, a micro level, a stroke in progress).
size_t deformActivePart(Document& doc, DeformOp op, const DeformOptions& options, std::string& error);
// The steps as one undo step labelled `label`.
size_t deformActivePart(Document& doc, const std::vector<DeformStep>& steps, std::string& error,
                        const std::string& label = "Deform stack");

// Resizes the active Part to `size` (scene units per axis; <= 0 keeps that axis) about its bounds centre, weighted
// by 1 - mask when `useMask`, as one undo step ("Size").
size_t resizeActivePart(Document& doc, const glm::vec3& size, bool useMask, std::string& error);

// deform_<op> for every operation, deform_size and deform_stack.
void registerDeformCommands(CommandRegistry& registry);

} // namespace sf
