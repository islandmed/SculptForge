// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Whole-mesh deformations (Tool > Reshape, from the discussions): Smooth, Relax, Polish and Balloon, each over
// the whole active Part with an amount, the mask holding masked parts in place (partly masked ones move partly),
// hidden faces and open borders kept, and one undo step. They write through the Sculptor, so a sculpt layer that
// is being recorded takes the change as a stroke would.
//   Polish  - flattens fine noise while keeping planes and sharp edges: bilateral filtering of the face normals,
//             then the vertices fit them (see surfaceOp), so small bumps average out and creases sharper than the
//             edge angle barely count.
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
//                  along -axis), Depth (a perspective along the axis), Noise (smooth random bumps along the axes),
//                  Symmetrize (one side copied onto the other across the axis's plane, or both evened out).
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
    Noise, Symmetrize,                             // Shape (foirgairwolf on Discord, 0.12.0)
};
constexpr int kDeformOpCount = 19;
const char* deformName(DeformOp op);    // "Polish", ... "Symmetrize"
const char* deformCommand(DeformOp op); // "deform_polish", ... "deform_symmetrize"
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
    bool hasPivot = false;   // false: the Part's bounds centre (Symmetrize: the world origin, as symmetry)
    glm::vec3 pivot{0.0f};
    float edge = 20.0f;      // Polish: degrees; creases sharper than this stay sharp
    int passes = 6;          // Polish: filter passes at amount 100 (Surface Polish runs 12)
    float scale = 4.0f;      // Noise: bumps across the Part's longest side
    bool average = false;    // Symmetrize: both sides meet halfway instead of one copied onto the other
};

struct DeformStep {
    DeformOp op = DeformOp::Bend;
    DeformOptions options;
};
// "bend 40 y soft dir=90; twist 90 xz; noise 20 y scale=6; symmetrize 100 x average" (axes letters, "soft",
// "dir=", "scale=", "average", "edge=", ";" between steps) <-> steps.
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

// Tool > Geometry > Surface Polish (foirgairwolf on Discord): Polish at `amount` 0-1 (1 = 12 passes), creases
// sharper than `edgeDegrees` kept sharp, as one undo step ("Surface Polish").
size_t surfacePolishActivePart(Document& doc, float amount, float edgeDegrees, bool useMask, std::string& error);

// Resizes the active Part to `size` (scene units per axis; <= 0 keeps that axis) about its bounds centre, weighted
// by 1 - mask when `useMask`, as one undo step ("Size").
size_t resizeActivePart(Document& doc, const glm::vec3& size, bool useMask, std::string& error);

// deform_<op> for every operation, deform_size, deform_stack and surface_polish.
void registerDeformCommands(CommandRegistry& registry);

} // namespace sf
