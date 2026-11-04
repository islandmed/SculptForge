// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Bridge files: binary meshes plus a shared bridge folder with an object list (io/BridgeProtocol.h holds the
// protocol's names). The binary layout follows the open-source Blender add-on GoB (github.com/JoseConseco/GoB,
// GPL); see docs/bridge-format.md.
//
// A Bridge file carries positions, tri/quad faces, per-face-corner UVs, face groups, mask and vertex paint. Quads
// are triangulated on import as (a,b,c) + (a,c,d) and kept paired (Mesh::quadPartners); export writes the pairs
// back as quads (meshes without kept quads pair consecutive (a,b,c),(a,c,d) triangles).
#pragma once

#include "core/Mesh.h"
#include "io/MeshObject.h"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace sf {

// Extra Bridge information that has no place in Mesh.
struct BridgeInfo {
    std::string name;            // object (Part) name without the bridgefile::kNamePrefix prefix
    int subdivisionLevel = 0;    // as recorded by the other application (informational)
    std::string diffuseMap, displacementMap, normalMap; // texture paths, if any
    size_t quads = 0, triangles = 0; // Bridge faces read
};

struct BridgeWriteOptions {
    bool quads = true; // write quad pairs (Mesh::quadPartners) back as quads
    std::string diffuseMap; // texture path recorded in the file (saveBridge fills it for a textured mesh)
};

// Parses an in-memory Bridge file. Positions are converted 
// Y-down axes to SculptForge's Y-up ones (a 180 degree turn about X).
bool parseBridge(std::span<const uint8_t> data, Mesh& out, std::string& error, BridgeInfo* info = nullptr);
std::vector<uint8_t> writeBridge(const Mesh& mesh, const std::string& name, const BridgeWriteOptions& options = {});

// Paths are UTF-8. A mesh with a texture (core/Texture.h) also gets its PNG
// beside the file ("<stem>_color.png"), named as the diffuse map in the file.
bool loadBridge(const std::string& path, Mesh& out, std::string& error, BridgeInfo* info = nullptr);
bool saveBridge(const std::string& path, const Mesh& mesh, const std::string& name, std::string& error,
             const BridgeWriteOptions& options = {});

// Bridge object names: ASCII letters, digits, '_' and '-' (GoB's escaping).
std::string bridgeObjectName(const std::string& name);

// ---------------------------------------------------------------------------
// The Bridge bridge (on Windows and macOS)
// ---------------------------------------------------------------------------

// The shared Bridge root on Windows and macOS (io/BridgeProtocol.h: kVendorFolder under %PUBLIC%, kMacRoot);
// empty elsewhere (the other application does not run on Linux).
std::string bridgeDefaultRoot();
// Message for platforms without the other application.
std::string bridgeUnavailableMessage();
// Where SculptForge registers itself as a Bridge application (BridgeApps/SculptForge/Bridge_Config.txt).
void setBridgeApplicationPath(std::string executable);

struct BridgeSendOptions {
    std::string root;       // Bridge root; empty = bridgeDefaultRoot()
    std::string name;       // object name; empty = "SculptForge"
    bool launch = true;     // notify the other application (its notifier) or start it
    std::string appPath; // the other application executable to start if its notifier is missing (optional)
};

// Launching goes through the shell ("start"/"open"), and bridge_send is reachable
// from scripts and the MCP server, so a path it launches must be free of quotes
// and shell expansion characters ($ ` % ! and, outside Windows, backslash).
bool bridgeShellSafePath(const std::string& path);

// Writes the mesh to <root>/BridgeProjects/Default/<name>.<ext>, lists it in
// the bridge folder's object list, names SculptForge in Bridge_Application.txt and
// (optionally) tells the other application to import it. `message` describes what happened.
// With `launch`, a `appPath` must name an existing executable (its file name
// contains kAppNameLower) and it and `root` must pass bridgeShellSafePath.
bool bridgeSend(const Mesh& mesh, const BridgeSendOptions& options, std::string& message);
// Sends several objects at once (gets one Part each): every object is
// written as <name>.<ext> (bridgeObjectName, made unique with _2, _3...) and listed
// in Bridge_ObjectList.txt in order; options.name is not used. `sentNames`
// receives the object names written.
bool bridgeSend(const std::vector<MeshObjectRef>& objects, const BridgeSendOptions& options, std::string& message,
             std::vector<std::string>* sentNames = nullptr);

// Reads every object in <root>/the bridge folder's object list (what the other application last
// sent), one per listed Part, named after it.
bool bridgeReceive(const std::string& root, std::vector<MeshObject>& out, std::string& message);
// The same merged into one mesh.
bool bridgeReceive(const std::string& root, Mesh& out, std::string& message, std::vector<std::string>* names = nullptr);

// Path of the object list the other application rewrites on every send (for polling).
std::string bridgeObjectListPath(const std::string& root);

// A folder of numbered Bridge files (`001_Body`, `002_Horns`, ...; such as `<name>.sculptforge`) opens as one
// scene, one object per file in file-name order.
// The folder's Bridge files, sorted; empty when there are none.
std::vector<std::string> bridgeFolderFiles(const std::string& path);
// Loads them, one object per file, named after the object name inside or the file name without its number.
bool loadBridgeFolder(const std::string& path, std::vector<MeshObject>& out, std::string& error);

} // namespace sf
