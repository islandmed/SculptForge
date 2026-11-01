// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The Maya bridge (#42): SculptForge and the SculptForge shelf script for Maya (integrations/maya) exchange
// meshes through ONE folder the user picks on both sides, the way GoZ apps do with ZBrush. Nothing else is
// shared: no sockets, ports or other IPC, and nothing in the folder is ever run. Meshes travel as OBJ (quads,
// UVs; one object per file, world space, Maya's Y-up and units as they are); a small JSON manifest per
// direction lists the objects, their Maya names and a revision number so each side sees what is new.
// docs/guide/maya.md describes the folder and the manifest.
//
//   <folder>/maya_to_sculptforge/manifest.json + r<rev>_<k>.obj   written by Maya
//   <folder>/sculptforge_to_maya/manifest.json + r<rev>_<k>.obj   written by SculptForge
//
// What SculptForge reads is checked strictly (it may come from anything that can write to the folder): the
// manifest's size, version and every field's type; at most kMayaMaxObjects objects; names are reduced to Maya's
// characters; files must be plain *.obj names in the manifest's own folder (no folders, no "..", no absolute
// paths, no links) and regular files no bigger than kMayaMaxObjBytes.
#pragma once

#include "io/MeshObject.h"

#include <cstdint>
#include <string>
#include <vector>

namespace sf {

inline constexpr size_t kMayaManifestMaxBytes = 256 * 1024;
inline constexpr size_t kMayaMaxObjects = 256;
inline constexpr size_t kMayaMaxNameBytes = 128;
inline constexpr size_t kMayaMaxFileNameBytes = 64;
inline constexpr unsigned long long kMayaMaxObjBytes = 8ull << 30; // 8 GiB: a 30M-triangle OBJ with UVs fits

struct MayaObject {
    std::string name; // the Maya transform's short name (Maya characters only): the SubTool's gozName
    std::string file; // OBJ file name inside the manifest's folder
};

struct MayaManifest {
    int64_t revision = 0; // changes on every send (each side keeps the last one it loaded)
    std::string sender;   // "maya" or "sculptforge"
    std::string senderVersion;
    std::string unit;     // Maya's linear unit when Maya sent ("cm" by default); informational, OBJ is taken 1:1
    std::vector<MayaObject> objects;
};

std::string mayaInbox(const std::string& folder);  // <folder>/maya_to_sculptforge
std::string mayaOutbox(const std::string& folder); // <folder>/sculptforge_to_maya
std::string mayaManifestPath(const std::string& box); // <box>/manifest.json

// A name Maya accepts as is: letters, digits and '_' (anything else becomes '_'), not starting with a digit,
// at most kMayaMaxNameBytes; "" becomes "SubTool".
std::string mayaSafeName(const std::string& name);
// A file name a manifest may list: 1..kMayaMaxFileNameBytes of [A-Za-z0-9_.-], not starting with '.', no "..",
// ending in ".obj".
bool mayaFileNameOk(const std::string& file);

bool parseMayaManifest(const std::string& text, MayaManifest& out, std::string& error);
std::string writeMayaManifest(const MayaManifest& manifest);
// Reads at most kMayaManifestMaxBytes (a bigger file is refused unread).
bool readMayaManifest(const std::string& path, MayaManifest& out, std::string& error);

// Loads what Maya sent (<folder>/maya_to_sculptforge): every object's OBJ, named after its Maya object.
bool mayaReceive(const std::string& folder, std::vector<MeshObject>& out, MayaManifest* manifest,
                 std::string& message);
// Writes the objects to <folder>/sculptforge_to_maya with the next revision (names made Maya-safe and unique;
// `sentNames` receives them) and removes the files of earlier sends.
bool mayaSend(const std::vector<MeshObjectRef>& objects, const std::string& folder, std::string& message,
              std::vector<std::string>* sentNames = nullptr, int64_t* revision = nullptr);

// The revision of the manifest in `box`, or -1 when there is none (or it is not valid).
int64_t mayaRevision(const std::string& box);
// The modification time and size of <box>/manifest.json in one number (0 when missing): the cheap check polling
// makes before reading it. (The size too: a copied file can keep its time, and some drives store it coarsely.)
long long mayaManifestStamp(const std::string& box);

} // namespace sf
