// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// classic masking. Mask strokes are Sculptor::dabMask (see Sculpt.h);
// the whole-mesh operations below edit Mesh::mask in place. They never touch
// fully hidden vertices (except clear, which frees the mask), mark the
// changed vertices dirty for the renderer and return false when nothing
// changed. The mask_* commands wrap them with an undo step.
#pragma once

#include "core/Commands.h"
#include "core/Mesh.h"
#include "core/Symmetry.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace sf {

class CommandRegistry;
class ScreenPolygon;

bool maskClear(Mesh& mesh);
bool maskAll(Mesh& mesh);
bool maskInvert(Mesh& mesh);
// Averages each value with its neighbours `iterations` times (softens edges).
bool maskBlur(Mesh& mesh, int iterations);
// Pushes values towards 0 or 1 and tightens soft edges (inverse of blur).
bool maskSharpen(Mesh& mesh, int iterations);
// Grows / shrinks the masked region by one edge ring per iteration.
bool maskGrow(Mesh& mesh, int iterations);
bool maskShrink(Mesh& mesh, int iterations);

// Per-vertex concavity: the offset of the neighbour average along the normal,
// relative to the mean edge length. > 0 in cavities, < 0 on convex areas, ~0 flat.
float vertexConcavity(const Mesh& mesh, uint32_t v);
// Masks concave areas: vertices whose concavity exceeds `threshold` get up to
// `strength` (0-1), with a soft ramp above the threshold. Replaces the mask.
bool maskByCavity(Mesh& mesh, float strength, float threshold);
// Masks convex areas (ridges, peaks, edges): the mirror of maskByCavity, for vertices whose concavity is below
// -threshold. Replaces the mask.
bool maskByPeaks(Mesh& mesh, float strength, float threshold);
// Masks vertices whose vertex paint is within `tolerance` (0-1, distance in RGB / sqrt 3) of `color`, with a soft
// ramp over the last fifth. A mesh without vertex paint is all white. With `add` the mask grows, else it is replaced.
bool maskByColor(Mesh& mesh, const glm::vec3& color, float tolerance, float strength, bool add);
// Masks vertices whose normal points within `angleDeg` (1-180) of `direction` (any length), with a soft ramp
// over the last 15 degrees, e.g. the view direction reversed to mask what faces the camera, or +Y for what faces
// up. With `add` the mask grows, else it is replaced.
bool maskByFacing(Mesh& mesh, const glm::vec3& direction, float angleDeg, float strength, bool add);
// Masks every vertex touching a visible triangle of face group `group`.
// With `add` the existing mask is kept, otherwise it is replaced.
bool maskByFaceGroup(Mesh& mesh, uint32_t group, bool add);

// Vertices on an open edge (an edge of only one triangle) and, with `face groups`, where the visible triangles
// around a vertex have more than one face group: 1 for those, 0 elsewhere.
std::vector<uint8_t> borderVertices(const Mesh& mesh, bool facegroups);
// Mask from the mesh border (#61; Blender: Mask from mesh boundary, Mask by face group border): the
// border vertices and `rings - 1` edge rings inside them (rings 1-20) get `strength` (0-1) of mask, fading
// towards the innermost ring with `soft`. `face groups` also masks face group borders. With `add` the mask
// grows (each vertex keeps the larger value), otherwise it is replaced. `found` gets the number of border
// vertices: 0 on a closed mesh (nothing to mask).
bool maskBorder(Mesh& mesh, int rings, bool facegroups, bool soft, float strength, bool add, size_t* found = nullptr);

// Mask selections (MaskLasso / MaskRect): visible vertices that
// project inside `shape` (NDC under `viewProj`, see ScreenSelect.h) gain
// `strength` (0-1) of mask, or lose it with `unmask`. Vertices behind the
// camera count as outside; the far side of the model is selected too.
// `mirrors` (Sculptor::copies of the symmetry) also selects each vertex whose
// mirror image or radial copy projects inside, as the brushes mirror (#51, #63); empty = none.
// `frontFacesOnly` (#91.3): only vertices whose normal faces the viewer (for a mirror image, its mirrored normal).
bool maskInShape(Mesh& mesh, const glm::mat4& viewProj, const ScreenPolygon& shape, float strength, bool unmask,
                 const std::vector<SymCopy>& mirrors = {}, bool frontFacesOnly = false);
// Whether any visible vertex (or, with `mirrors`, its mirror image) projects
// inside `shape`: a selection that covers none of the model clears the mask
// instead (Ctrl+drag on the background).
bool shapeCoversMesh(const Mesh& mesh, const glm::mat4& viewProj, const ScreenPolygon& shape,
                     const std::vector<SymCopy>& mirrors = {});

// A command's symmetry arguments: "symmetry" (mirror axes, "x", "xz" ...), "radial" (copies around an axis,
// 2-64; default 1, off), "radial_axis" (x, y or z; default y) and "local" (around `mesh`'s centre instead of
// the world origin). Without them, no symmetry. False, saying why, for a bad value.
bool symmetryArg(const Args& a, const Mesh& mesh, Symmetry& sym, std::string& error);
// The same as the list of copies (Sculptor::copies, the first one the identity).
bool symmetryCopiesArg(const Args& a, const Mesh& mesh, std::vector<SymCopy>& copies, std::string& error);
// The parameter specs of "radial", "radial_axis" and "local" (commands that take "symmetry" add them).
std::vector<ParamSpec> radialSymmetryParams();

// Registers mask_clear, mask_all, mask_invert, mask_blur, mask_sharpen,
// mask_grow, mask_shrink, mask_by_cavity, mask_by_peaks, mask_by_color, mask_by_facing, mask_by_face group, mask_border,
// mask_rect and mask_lasso.
void registerMaskingCommands(CommandRegistry& registry);

} // namespace sf
