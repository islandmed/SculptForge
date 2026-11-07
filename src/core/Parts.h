// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Parts, classic: a scene is a list of separate meshes (body, eyes,
// teeth, armour) that are sculpted one at a time, shown or hidden, merged and
// split. The data model lives in Document (see Part); this file holds the
// mesh operations and the part_* commands.
#pragma once

#include "core/Document.h"
#include "io/MeshObject.h"

#include <string>
#include <vector>

namespace sf {

class CommandRegistry;

// Concatenates meshes into one. Every attribute any input has is kept (inputs
// without it get the default), face group ids are offset so each input keeps
// its own groups (inputs without face groups become one group each), and sculpt
// layers are matched by name.
Mesh mergeMeshes(const std::vector<const Mesh*>& meshes);

// A copy of `mesh` placed by `transform` (Part::transform): positions, the layers' base and offsets, normals.
// Subdivision levels are left out (they'd be in the old place).
Mesh placedMesh(const Mesh& mesh, const glm::mat4& transform);
// Part `i`'s mesh where it is in the scene: the mesh itself when it isn't moved, else a placed copy in `scratch`.
const Mesh& partPlacedMesh(const Document& doc, int i, Mesh& scratch);

// The visible Parts merged into one mesh (for exporters that write a single
// mesh), where they are in the scene. With one Part this is a copy of it, whether shown or not.
Mesh mergedVisibleMesh(const Document& doc);

// The visible Parts that have triangles, in list order and named after
// them, for exporters that write one object per Part. With a single Part
// it is that one (shown or not) named doc.assetName(), so one-object exports
// stay as they were. Moved Parts (linked copies, #94) come as placed copies, unless `instances`: then each
// object refers to its Part's mesh as is, with the Part's transform, and linked copies name the first object
// with their mesh (MeshObjectRef::instanceOf).
std::vector<MeshObjectRef> visiblePartObjects(const Document& doc, bool instances = false);

// Replaces the scene with one Part per object (the first one active), named
// after the objects. A single object, or an unnamed one, is named
// `fallbackName` (e.g. the file name). Clears the edit history like setMesh.
void setSceneObjects(Document& doc, std::vector<MeshObject> objects, const std::string& fallbackName);

// One mesh per face group, in increasing group id order (empty if there's only one group).
std::vector<Mesh> splitByFaceGroups(const Mesh& mesh);
// One mesh per connected piece, largest first (empty if the mesh is one piece).
std::vector<Mesh> splitIntoParts(const Mesh& mesh);

// Insert sets (kitbash sets): a folder of model files, or one model file with several objects, to insert
// from one mesh at a time (insert_mesh set=<path> index=<n>). Each item is a file in the folder (.obj, .ply,
// .glb, .gltf; not its subfolders; by name, at most kInsertSetMaxItems) or an object in the file.
struct InsertSetItem {
    std::string name; // the file's stem, or the object's name ("<stem> <n>" when it has none)
    std::string file; // the file it is in (the set itself for a multi-object file)
    int object = -1;  // its object in that file; -1 = the whole file
};
constexpr int kInsertSetMaxItems = 512;
// The set's items, without loading folder items (a multi-object file is read once to count its objects).
bool listInsertSet(const std::string& path, std::vector<InsertSetItem>& items, std::string& error);
// One item's mesh, and its name.
bool loadInsertSetItem(const std::string& path, int index, Mesh& out, std::string& name, std::string& error);
// The built-in insert sets, inside the app (no file to pick): "builtin:<id>" works wherever a set path does.
// Creature bones (#154): original pieces from tools/creature-bones (assets/insert-sets).
struct BuiltinInsertSet {
    const char* id;   // "creature-bones"
    const char* name; // "Creature bones" (English; the UI translates it)
};
const std::vector<BuiltinInsertSet>& builtinInsertSets();
inline constexpr const char* kBuiltinInsertSetPrefix = "builtin:";
// The file a set path names: a built-in set is written from the app to <app data>/insert-sets/<id>.obj when that
// file is missing or differs (else the system temp folder); any other path is itself.
bool resolveInsertSetPath(const std::string& path, std::string& file, std::string& error);

// part_list, part_select, part_append, part_duplicate, part_duplicate_linked, part_make_unique, part_delete,
// part_rename, part_visibility, part_solo, part_show_all, part_move,
// part_merge_down, part_merge_visible, part_split_groups, part_split_parts, part_split_hidden,
// part_split_masked, part_split_unmasked, part_select_folder, part_select_none, part_selection_visibility,
// part_move_faces.
void registerPartCommands(CommandRegistry& registry);

} // namespace sf
