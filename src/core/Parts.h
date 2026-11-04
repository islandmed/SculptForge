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

// The visible Parts merged into one mesh (for exporters that write a single
// mesh). With one Part this is a copy of it, whether shown or not.
Mesh mergedVisibleMesh(const Document& doc);

// The visible Parts that have triangles, in list order and named after
// them, for exporters that write one object per Part. With a single Part
// it is that one (shown or not) named doc.assetName(), so one-object exports
// stay as they were.
std::vector<MeshObjectRef> visiblePartObjects(const Document& doc);

// Replaces the scene with one Part per object (the first one active), named
// after the objects. A single object, or an unnamed one, is named
// `fallbackName` (e.g. the file name). Clears the edit history like setMesh.
void setSceneObjects(Document& doc, std::vector<MeshObject> objects, const std::string& fallbackName);

// One mesh per face group, in increasing group id order (empty if there's only one group).
std::vector<Mesh> splitByFaceGroups(const Mesh& mesh);
// One mesh per connected piece, largest first (empty if the mesh is one piece).
std::vector<Mesh> splitIntoParts(const Mesh& mesh);

// part_list, part_select, part_append, part_duplicate, part_delete,
// part_rename, part_visibility, part_solo, part_show_all, part_move,
// part_merge_down, part_merge_visible, part_split_groups, part_split_parts.
void registerPartCommands(CommandRegistry& registry);

} // namespace sf
