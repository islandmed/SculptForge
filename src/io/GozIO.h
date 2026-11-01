// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// GoZ: ZBrush's bridge format (.GoZ binary meshes plus a shared "GoZ folder"
// with an object list). The binary layout follows the open-source
// Blender add-on GoB (github.com/JoseConseco/GoB, GPL); see docs/goz.md.
//
// .GoZ carries positions, tri/quad faces, per-face-corner UVs, polygroups,
// mask and polypaint. Quads are triangulated on import as (a,b,c) + (a,c,d)
// and kept paired (Mesh::quadPartners); export writes the pairs back as quads
// (meshes without kept quads pair consecutive (a,b,c),(a,c,d) triangles).
// ZBrush's own .ZTL/.ZPR files are proprietary and cannot be read.
#pragma once

#include "core/Mesh.h"
#include "io/MeshObject.h"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace sf {

// Extra GoZ information that has no place in Mesh.
struct GozInfo {
    std::string name;            // object (SubTool) name without the "GoZMesh_" prefix
    int subdivisionLevel = 0;    // as recorded by ZBrush (informational)
    std::string diffuseMap, displacementMap, normalMap; // texture paths, if any
    size_t quads = 0, triangles = 0; // GoZ faces read
};

struct GozWriteOptions {
    bool quads = true; // write quad pairs (Mesh::quadPartners) back as quads
    std::string diffuseMap; // texture path recorded in the file (saveGoz fills it for a textured mesh)
};

// Parses an in-memory .GoZ file. Positions are converted from ZBrush's
// Y-down axes to SculptForge's Y-up ones (a 180 degree turn about X).
bool parseGoz(std::span<const uint8_t> data, Mesh& out, std::string& error, GozInfo* info = nullptr);
std::vector<uint8_t> writeGoz(const Mesh& mesh, const std::string& name, const GozWriteOptions& options = {});

// Paths are UTF-8. A mesh with a texture (core/Texture.h) also gets its PNG
// beside the file ("<stem>_color.png"), named as the diffuse map in the file.
bool loadGoz(const std::string& path, Mesh& out, std::string& error, GozInfo* info = nullptr);
bool saveGoz(const std::string& path, const Mesh& mesh, const std::string& name, std::string& error,
             const GozWriteOptions& options = {});

// GoZ object names: ASCII letters, digits, '_' and '-' (GoB's escaping).
std::string gozObjectName(const std::string& name);

// ---------------------------------------------------------------------------
// The GoZ bridge (ZBrush on Windows and macOS)
// ---------------------------------------------------------------------------

// The shared GoZ root: %PUBLIC%\Pixologic on Windows, /Users/Shared/Pixologic
// on macOS, empty elsewhere (ZBrush does not run on Linux).
std::string gozDefaultRoot();
// Message for platforms without ZBrush.
std::string gozUnavailableMessage();
// Where SculptForge registers itself as a GoZ application (GoZApps/SculptForge/GoZ_Config.txt).
void setGozApplicationPath(std::string executable);

struct GozSendOptions {
    std::string root;       // GoZ root; empty = gozDefaultRoot()
    std::string name;       // object name; empty = "SculptForge"
    bool launch = true;     // notify ZBrush (GoZBrushFromApp) or start it
    std::string zbrushPath; // ZBrush executable to start if GoZBrushFromApp is missing (optional)
};

// Launching goes through the shell ("start"/"open"), and goz_send is reachable
// from scripts and the MCP server, so a path it launches must be free of quotes
// and shell expansion characters ($ ` % ! and, outside Windows, backslash).
bool gozShellSafePath(const std::string& path);

// Writes the mesh to <root>/GoZProjects/Default/<name>.GoZ, lists it in
// GoZBrush/GoZ_ObjectList.txt, names SculptForge in GoZ_Application.txt and
// (optionally) tells ZBrush to import it. `message` describes what happened.
// With `launch`, a `zbrushPath` must name an existing ZBrush (its file name
// contains "zbrush") and it and `root` must pass gozShellSafePath.
bool gozSend(const Mesh& mesh, const GozSendOptions& options, std::string& message);
// Sends several objects at once (ZBrush gets one SubTool each): every object is
// written as <name>.GoZ (gozObjectName, made unique with _2, _3...) and listed
// in GoZ_ObjectList.txt in order; options.name is not used. `sentNames`
// receives the object names written.
bool gozSend(const std::vector<MeshObjectRef>& objects, const GozSendOptions& options, std::string& message,
             std::vector<std::string>* sentNames = nullptr);

// Reads every object in <root>/GoZBrush/GoZ_ObjectList.txt (what ZBrush last
// sent), one per listed SubTool, named after it.
bool gozReceive(const std::string& root, std::vector<MeshObject>& out, std::string& message);
// The same merged into one mesh.
bool gozReceive(const std::string& root, Mesh& out, std::string& message, std::vector<std::string>* names = nullptr);

// Path of the object list ZBrush rewrites on every send (for polling).
std::string gozObjectListPath(const std::string& root);

// Error text for ZBrush's proprietary files (.ztl, .zpr, .zbr).
std::string zbrushProprietaryMessage(const std::string& extension);

// ZTools converted in ZBrush by the SculptForge ZTL Export plug-in (integrations/zbrush): next to `<name>.ztl`
// it writes one `<name>.sf<NNN>_<SubTool>.GoZ` per SubTool (NNN keeps the SubTool order). A folder of .GoZ files
// (such as `<name>.sculptforge`) works too.
// The converted files for a .ztl path or a folder, in SubTool order; empty when there are none.
std::vector<std::string> convertedZToolFiles(const std::string& path);
// Loads them, one object per SubTool named after it. `note` gets a warning when the .ztl is newer than the
// conversion.
bool loadConvertedZTool(const std::string& path, std::vector<MeshObject>& out, std::string& error,
                        std::string* note = nullptr);

} // namespace sf
