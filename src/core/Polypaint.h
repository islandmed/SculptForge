// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Polypaint: per-vertex colour painting (Mesh::colors, linear RGB). The Paint
// brush, whole-mesh fills, cavity darkening and the paint_* commands. Every
// operation respects the mask (and hidden faces) like the sculpt brushes do.
#pragma once

#include "core/BVH.h"
#include "core/Mesh.h"
#include "core/Sculpt.h"

#include <algorithm>
#include <string>

namespace sf {

class CommandRegistry;

// Parses a colour given as "#rgb", "#rrggbb", "rrggbb", "r,g,b" (0..1 or
// 0..255) or a basic name ("red", "skin", ...). The text is a display (sRGB)
// colour; `linear` receives the linear value Mesh::colors stores.
bool parseColor(const std::string& text, glm::vec3& linear);
// "#rrggbb" (sRGB) for a linear colour.
std::string colorToHex(const glm::vec3& linear);
// The tint the active colour gives a Lit Sphere material on a mesh with no vertex paint (as ZBrush's main
// colour): its hue and saturation at full brightness, so a grey (the default colour) or black leaves the material
// as it is and a red turns it red while the material keeps its own light and shade.
inline glm::vec3 materialTintFor(const glm::vec3& linear) {
    const float m = std::max(linear.x, std::max(linear.y, linear.z));
    return m > 1e-4f ? glm::clamp(linear / m, 0.0f, 1.0f) : glm::vec3(1.0f);
}

// One Paint brush dab inside a Sculptor stroke: blends `color` into the
// vertices under the brush with the smoothstep falloff, strength x pressure,
// symmetry and vertexInfluence() (mask / hidden). `p.invert` paints back to
// white. Records old colours for the stroke's undo step and marks vertices
// dirty. Returns the number of vertices changed.
size_t paintDab(Sculptor& sculptor, Mesh& mesh, const BVH& bvh, const DabParams& p, const glm::vec3& color,
                Symmetry sym);

enum class PaintArea { All, Unmasked, Masked };

// Blends `color` over the mesh with weight `amount` (0..1). Unmasked scales by
// 1 - mask (the whole mesh when nothing is masked), Masked by the mask, All
// ignores it. Hidden faces are never painted. Not undoable by itself.
size_t paintFill(Mesh& mesh, const glm::vec3& color, PaintArea area = PaintArea::Unmasked, float amount = 1.0f);

// Per-vertex cavity amount 0..1 (1 = deep crease), from the same signed
// curvature the viewport's cavity shading uses, blurred `smoothing` times.
std::vector<float> cavityMap(const Mesh& mesh, int smoothing = 2);
// Blends `color` into the creases by cavity x strength (respects the mask).
size_t paintFromCavity(Mesh& mesh, const glm::vec3& color, float strength, int smoothing = 2);

// Colour under a surface point: barycentric blend of the hit triangle's vertex colours.
glm::vec3 sampleColor(const Mesh& mesh, uint32_t triangle, const glm::vec3& point);

// set_color, paint_fill, paint_clear, paint_from_cavity, export_ply and the paint_layer_* commands.
void registerPaintCommands(CommandRegistry& registry);

} // namespace sf
