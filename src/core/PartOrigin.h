// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Part origins (Discord #ideas, Devon): a point per Part (Part::origin) that the gizmo turns and scales the Part
// about, set to the Part's bounds centre, its centre of mass, the world centre or any point (the gizmo's); and
// moving a Part so its origin sits at the world centre, or only onto a symmetry plane (an off-centre model before
// Mirror And Weld). Each is one undo step on that Part.
#pragma once

#include "core/Document.h"
#include "core/MeshTransform.h"

#include <glm/vec3.hpp>

namespace sf {

class CommandRegistry;

// The centre of the bounds of the Part's vertices (0,0,0 for an empty mesh).
glm::vec3 boundsCentre(const Mesh& mesh);
// The centre of mass of the Part as a solid of even density: the volume centroid when the triangles enclose a
// volume (closed or nearly), else the area-weighted centre of the surface (a shell, a plane). Hidden triangles count.
glm::vec3 centreOfMass(const Mesh& mesh);

// Where the gizmo pivots for Part `index`, in the scene: its origin, or its bounds centre without one.
glm::vec3 partPivot(const Document& doc, int index);
// Part::origin is in the mesh's space (a linked copy, #94, carries its own): where it is in the scene.
glm::vec3 partOriginInScene(const Part& p);

// After a transform that moved the whole of Part `index`'s mesh (TransformSession::whole; `t` in the mesh's space)
// and pushed its undo step onto `undo` (that Part's stack): the origin moves with it, in the same undo step.
// Nothing without an origin. (A move of the Part's transform carries it by itself.)
void carryPartOrigin(Document& doc, int index, const MeshTransform& t, UndoStack& undo);

// part_origin, part_to_world.
void registerPartOriginCommands(CommandRegistry& registry);

} // namespace sf
