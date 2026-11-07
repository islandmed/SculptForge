// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Forge Spheres, classic: block out an organic base mesh as a tree of connected spheres (a root, then limbs,
// fingers, tails grown from it), see the skin over them, and turn it into a sculptable Part. A Forge Sphere Part
// keeps its armature in Mesh::Forge Spheres; its triangles are generated from it (the spheres and their links, or the
// skin preview), so undo, Part switching and projects carry the armature with the mesh. Edits replace the mesh
// as one undo step each. The skin is a smooth union of rounded cones (one per link) meshed with surface nets
// (core/Voxel.h), so it comes out watertight and in quads, one face group per link.
#pragma once

#include "core/Json.h"
#include "core/Mesh.h"
#include "core/Sculpt.h"

#include <string>
#include <vector>

namespace sf {

class CommandRegistry;

// Face groups of the armature's display mesh: sphere i is group kForge SphereGroup + i, the link from child c to its
// parent is kForge SphereLinkGroup + c.
constexpr uint32_t kForgeSphereGroup = 1;
constexpr uint32_t kForgeSphereLinkGroup = 1u << 20;
// The sphere (or, for a link, the child sphere) a display-mesh face group belongs to; -1 for neither.
int forgesphereOfGroup(uint32_t group, bool* link = nullptr);

// The armature's display mesh: a sphere per Forge Sphere and a tapered link to each parent, with face groups as above
// and the spheres coloured (the root a little warmer). Carries `spheres` in Mesh::Forge Spheres.
Mesh forgesphereDisplayMesh(const std::vector<ForgeSphere>& spheres);

// The skin over the armature: watertight quads, `resolution` cells across its longest side, `smooth` Taubin passes,
// one face group per link (the root sphere's own group when there are no links). A plain mesh (no Forge Spheres).
Mesh forgesphereSkin(const std::vector<ForgeSphere>& spheres, int resolution = 128, int smooth = 2);
constexpr int kForgeSpherePreviewResolution = 72;

// The mesh a Forge Sphere Part shows: the display mesh, or with `preview` the skin at kForge SpherePreviewResolution;
// both carry `spheres` and the preview flag.
Mesh forgesphereMesh(const std::vector<ForgeSphere>& spheres, bool preview);

// The sphere that mirrors `i` across `m` (one of Sculptor::mirrors): `i` itself when it sits on the mirror plane,
// -1 when there is none.
int forgesphereMirror(const std::vector<ForgeSphere>& spheres, int i, const glm::vec3& m);

// Symmetric edits (`sym`: the planes to mirror across, as the sculpt symmetry). Each returns false and leaves
// `spheres` alone when the arguments are out of range.
// Adds a sphere growing from `parent`; `added` gets the new sphere's index (its mirror copies follow it). With
// symmetry, a sphere within its radius of a mirror plane snaps onto it as one sphere (unless the parent has a twin).
bool forgesphereAdd(std::vector<ForgeSphere>& spheres, int parent, const glm::vec3& center, float radius, Symmetry sym,
                int* added = nullptr);
// Moves and resizes sphere `i`; a sphere on a mirror plane stays on it.
bool forgesphereSet(std::vector<ForgeSphere>& spheres, int i, const glm::vec3& center, float radius, Symmetry sym);
// Deletes sphere `i` and everything that grows from it (never the root).
bool forgesphereDelete(std::vector<ForgeSphere>& spheres, int i, Symmetry sym);
// Inserts a sphere halfway along the link from `child` to its parent; `added` gets its index.
bool forgesphereInsert(std::vector<ForgeSphere>& spheres, int child, Symmetry sym, int* added = nullptr);
// Makes sphere `i` the root: the links on its way to the old root turn around, and it moves to index 0 (the old
// root takes its index). The spheres and links stay where they are, so the skin is the same. False for the root.
bool forgesphereSetRoot(std::vector<ForgeSphere>& spheres, int i);

// "x,y,z,radius,parent" per sphere, as a flat array; FromJson drops an armature that doesn't form a tree.
Json forgespheresToJson(const std::vector<ForgeSphere>& spheres);
std::vector<ForgeSphere> forgespheresFromJson(const Json& json);
// Empty when `spheres` is a valid armature: a root at 0, parents in range, no loops, finite positive sizes.
std::string forgespheresValidate(const std::vector<ForgeSphere>& spheres);

// Forge Sphere_new, Forge Sphere_list, Forge Sphere_add, Forge Sphere_set, Forge Sphere_delete, Forge Sphere_insert, Forge Sphere_preview,
// Forge Sphere_make_mesh.
void registerForgeSphereCommands(CommandRegistry& registry);

} // namespace sf
