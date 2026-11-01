// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The Blender bridge: SculptForge and the SculptForge add-on for Blender
// (addons/blender) exchange meshes through a per-user folder, the way GoZ apps
// do with ZBrush. Meshes travel as .GoZ files (io/GozIO.h: quads, UVs,
// polypaint, mask, polygroups); a small JSON manifest per direction lists the
// objects, their Blender names and transforms, and a revision number so each
// side sees what is new. No sockets: each side polls the other's manifest.
// docs/blender.md describes the folder and the manifest.
//
//   <root>/to_sculptforge/manifest.json + r<rev>_<k>.GoZ   written by Blender
//   <root>/to_blender/manifest.json     + r<rev>_<k>.GoZ   written by SculptForge
//   <root>/app.json    where SculptForge is installed (the add-on starts it)
//   <root>/alive.json  SculptForge's heartbeat, so the add-on knows it is running
#pragma once

#include "io/MeshObject.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace sf {

// One object in a manifest.
struct BridgeObject {
    std::string name;            // the Blender object name (UTF-8, exact): the SubTool's gozName
    std::string file;            // .GoZ file name inside the manifest's folder
    bool hasMatrix = false;      // the mesh is in object space and `matrix` places it in the scene
    glm::dmat4 matrix{1.0};      // Blender's matrix_world (Z-up, metres; glm column-major)
    std::string shapes;          // optional: blend shapes file (writeBridgeShapes) beside the .GoZ
};

struct BridgeManifest {
    int64_t revision = 0;        // changes on every send (each side keeps the last one it loaded)
    std::string sender;          // "blender" or "sculptforge"
    std::string senderVersion;
    std::vector<BridgeObject> objects;
};

// The exchange folder: $SCULPTFORGE_BLENDER_EXCHANGE, else <app data>/blender
// (~/.local/share/SculptForge/SculptForge/blender, %APPDATA%\SculptForge\SculptForge\blender,
// ~/Library/Application Support/SculptForge/SculptForge/blender). The add-on computes the same path.
// Inside a Flatpak the app's own data folder is private (~/.var/app/<id>/data), so the folder is the host's:
// $HOST_XDG_DATA_HOME, else ~/.local/share.
std::string blenderExchangeRoot();
// Inside a Flatpak: the launcher Flatpak exports for the app (<installation>/exports/bin/<id>), which the add-on
// can start from outside the sandbox. `flatpakInfo` is the text of /.flatpak-info; empty when it names no
// installation.
std::string flatpakHostLauncher(const std::string& flatpakInfo);
std::string blenderInbox(const std::string& root);  // <root>/to_sculptforge
std::string blenderOutbox(const std::string& root); // <root>/to_blender
std::string bridgeManifestPath(const std::string& folder); // <folder>/manifest.json

bool parseBridgeManifest(const std::string& text, BridgeManifest& out, std::string& error);
std::string writeBridgeManifest(const BridgeManifest& manifest);
bool readBridgeManifest(const std::string& path, BridgeManifest& out, std::string& error);

// Blender is Z-up, SculptForge Y-up, both in metres: p_sf = (x, z, -y). These convert points and
// Blender object matrices (M_sf = A * M_blender * A^-1).
glm::dvec3 blenderToSculptForge(const glm::dvec3& p);
glm::dvec3 sculptForgeToBlender(const glm::dvec3& p);
glm::dmat4 blenderMatrixToSculptForge(const glm::dmat4& m);

// Blend shapes (sculpt layers) for the add-on's shape keys, sent beside an object's .GoZ (which then holds the base
// shape, every layer at zero). Little endian: "SFSHAPES", u32 version 1, u32 vertex count, u32 shape count, then per
// shape u32 name length, the name (UTF-8), f32 weight, u32 count N, u32[N] vertex indices, f32[3N] offsets in
// Blender's axes (Z up, metres, world space).
struct BridgeShape {
    std::string name;
    float weight = 0.0f;
    std::vector<uint32_t> index;
    std::vector<glm::vec3> offset; // Blender axes
};
bool writeBridgeShapes(const std::string& path, const Mesh& mesh, std::string& error);
bool readBridgeShapes(const std::string& path, uint32_t& vertexCount, std::vector<BridgeShape>& out, std::string& error);

// Loads what Blender sent (<root>/to_sculptforge): every object's .GoZ, moved by its matrix into place, named
// after its Blender object. `manifest` receives the manifest (for its revision).
bool blenderReceive(const std::string& root, std::vector<MeshObject>& out, BridgeManifest* manifest,
                    std::string& message);

// Writes the objects to <root>/to_blender (world space, so the add-on puts each mesh back into its object's own
// space) with the next revision, and removes the files of earlier sends. Names are kept exactly (duplicates
// get .001, .002... as Blender would name them); `sentNames` receives them.
bool blenderSend(const std::vector<MeshObjectRef>& objects, const std::string& root, std::string& message,
                 std::vector<std::string>* sentNames = nullptr, int64_t* revision = nullptr);

// Modification time of a file (0 when missing), for cheap polling.
long long bridgeFileTime(const std::string& path);
// The revision of the manifest in `folder`, or -1 when there is none.
int64_t bridgeRevision(const std::string& folder);

// Records where SculptForge is installed (<root>/app.json), so the add-on can start it. Writes only when the
// content changes.
void writeBridgeAppInfo(const std::string& root, const std::string& executable, const std::string& version);
// The heartbeat (<root>/alive.json: process id and time); the add-on treats SculptForge as running while it
// is fresh. `remove` deletes it (on exit).
void writeBridgeHeartbeat(const std::string& root, bool remove = false);

} // namespace sf
