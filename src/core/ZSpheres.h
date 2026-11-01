// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// ZSpheres, ZBrush style: block out an organic base mesh as a tree of connected spheres (a root, then limbs,
// fingers, tails grown from it), see the skin over them, and turn it into a sculptable SubTool. A ZSphere SubTool
// keeps its armature in Mesh::zspheres; its triangles are generated from it (the spheres and their links, or the
// skin preview), so undo, SubTool switching and projects carry the armature with the mesh. Edits replace the mesh
// as one undo step each. The skin is a smooth union of rounded cones (one per link) meshed with surface nets
// (core/Voxel.h), so it comes out watertight and in quads, one polygroup per link.
#pragma once

#include "core/Json.h"
#include "core/Mesh.h"
#include "core/Sculpt.h"

#include <string>
#include <vector>

namespace sf {

class CommandRegistry;

// Polygroups of the armature's display mesh: sphere i is group kZSphereGroup + i, the link from child c to its
// parent is kZSphereLinkGroup + c.
constexpr uint32_t kZSphereGroup = 1;
constexpr uint32_t kZSphereLinkGroup = 1u << 20;
// The sphere (or, for a link, the child sphere) a display-mesh polygroup belongs to; -1 for neither.
int zsphereOfGroup(uint32_t group, bool* link = nullptr);

// The armature's display mesh: a sphere per ZSphere and a tapered link to each parent, with polygroups as above
// and the spheres coloured (the root a little warmer). Carries `spheres` in Mesh::zspheres.
Mesh zsphereDisplayMesh(const std::vector<ZSphere>& spheres);

// The skin over the armature: watertight quads, `resolution` cells across its longest side, `smooth` Taubin passes,
// one polygroup per link (the root sphere's own group when there are no links). A plain mesh (no zspheres).
Mesh zsphereSkin(const std::vector<ZSphere>& spheres, int resolution = 128, int smooth = 2);
constexpr int kZSpherePreviewResolution = 72;

// The mesh a ZSphere SubTool shows: the display mesh, or with `preview` the skin at kZSpherePreviewResolution;
// both carry `spheres` and the preview flag.
Mesh zsphereMesh(const std::vector<ZSphere>& spheres, bool preview);

// The sphere that mirrors `i` across `m` (one of Sculptor::mirrors): `i` itself when it sits on the mirror plane,
// -1 when there is none.
int zsphereMirror(const std::vector<ZSphere>& spheres, int i, const glm::vec3& m);

// Symmetric edits (`sym`: the planes to mirror across, as the sculpt symmetry). Each returns false and leaves
// `spheres` alone when the arguments are out of range.
// Adds a sphere growing from `parent`; `added` gets the new sphere's index (its mirror copies follow it).
bool zsphereAdd(std::vector<ZSphere>& spheres, int parent, const glm::vec3& center, float radius, Symmetry sym,
                int* added = nullptr);
// Moves and resizes sphere `i`; a sphere on a mirror plane stays on it.
bool zsphereSet(std::vector<ZSphere>& spheres, int i, const glm::vec3& center, float radius, Symmetry sym);
// Deletes sphere `i` and everything that grows from it (never the root).
bool zsphereDelete(std::vector<ZSphere>& spheres, int i, Symmetry sym);
// Inserts a sphere halfway along the link from `child` to its parent; `added` gets its index.
bool zsphereInsert(std::vector<ZSphere>& spheres, int child, Symmetry sym, int* added = nullptr);
// Makes sphere `i` the root: the links on its way to the old root turn around, and it moves to index 0 (the old
// root takes its index). The spheres and links stay where they are, so the skin is the same. False for the root.
bool zsphereSetRoot(std::vector<ZSphere>& spheres, int i);

// "x,y,z,radius,parent" per sphere, as a flat array; FromJson drops an armature that doesn't form a tree.
Json zspheresToJson(const std::vector<ZSphere>& spheres);
std::vector<ZSphere> zspheresFromJson(const Json& json);
// Empty when `spheres` is a valid armature: a root at 0, parents in range, no loops, finite positive sizes.
std::string zspheresValidate(const std::vector<ZSphere>& spheres);

// zsphere_new, zsphere_list, zsphere_add, zsphere_set, zsphere_delete, zsphere_insert, zsphere_preview,
// zsphere_make_mesh.
void registerZSphereCommands(CommandRegistry& registry);

} // namespace sf
