// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The Maya bridge (#42): SculptForge and the SculptForge shelf script for Maya (integrations/maya) exchange
// meshes through ONE folder the user picks on both sides, the way Bridge apps do . Nothing else is
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
//
// Live (opt-in on both sides): each side sends its changed objects by itself once it has been still for
// kMayaLiveIdleNs, still through files only. The manifest's "writer" (a random id per app session) and each
// side's own state keep an app from loading back what it just wrote; mayaMeshesMatch() drops an object that
// comes back unchanged, so an echo never becomes an undo step, let alone a loop.
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
inline constexpr unsigned long long kMayaLiveIdleNs = 300'000'000ull;  // Live sends once the scene is this still
inline constexpr unsigned long long kMayaPollNs = 250'000'000ull;      // the manifest's cheap check, this often

struct MayaObject {
    std::string name; // the Maya transform's short name (Maya characters only): the Part's bridgeName
    std::string file; // OBJ file name inside the manifest's folder
};

struct MayaManifest {
    int64_t revision = 0; // changes on every send (each side keeps the last one it loaded)
    std::string sender;   // "maya" or "sculptforge"
    std::string senderVersion;
    std::string unit;     // Maya's linear unit when Maya sent ("cm" by default); informational, OBJ is taken 1:1
    std::string writer;   // the sending app session's id (optional: "" from older versions)
    bool live = false;    // sent by Live (informational)
    std::vector<MayaObject> objects;
};

std::string mayaInbox(const std::string& folder);  // <folder>/maya_to_sculptforge
std::string mayaOutbox(const std::string& folder); // <folder>/sculptforge_to_maya
std::string mayaManifestPath(const std::string& box); // <box>/manifest.json

// A name Maya accepts as is: letters, digits and '_' (anything else becomes '_'), not starting with a digit,
// at most kMayaMaxNameBytes; "" becomes "Part".
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
// `writer` and `live` go into the manifest. Each OBJ is written to a temporary name and renamed, then the
// manifest likewise: a reader never sees half a file.
bool mayaSend(const std::vector<MeshObjectRef>& objects, const std::string& folder, std::string& message,
              std::vector<std::string>* sentNames = nullptr, int64_t* revision = nullptr,
              const std::string& writer = "", bool live = false);

// A new random writer id (16 hex digits) for this app session.
std::string mayaNewWriterId();
// A hash of what an OBJ send carries (positions, triangles, quads, UVs): Live skips Parts whose hash is unchanged.
uint64_t mayaMeshHash(const Mesh& mesh);
// Only what an OBJ send writes (positions, triangles, quads, UVs, vertex paint), so Live can write it off the
// main thread while sculpting goes on.
Mesh mayaSendCopy(const Mesh& mesh);
// The same faces and UVs, and every position within OBJ's 7 significant digits: what came back is what went out.
bool mayaMeshesMatch(const Mesh& a, const Mesh& b);

// Live's wait: changed() at every edit, due() once nothing changed for kMayaLiveIdleNs and no stroke is on.
struct MayaLiveDebounce {
    unsigned long long since = 0; // the last change (ns); 0 = nothing waits
    void changed(unsigned long long now) { since = now ? now : 1; }
    bool pending() const { return since != 0; }
    bool due(unsigned long long now, bool stroking) const {
        return since != 0 && !stroking && now >= since && now - since >= kMayaLiveIdleNs;
    }
    void clear() { since = 0; }
};

// The revision of the manifest in `box`, or -1 when there is none (or it is not valid).
int64_t mayaRevision(const std::string& box);
// The modification time and size of <box>/manifest.json in one number (0 when missing): the cheap check polling
// makes before reading it. (The size too: a copied file can keep its time, and some drives store it coarsely.)
long long mayaManifestStamp(const std::string& box);

} // namespace sf
