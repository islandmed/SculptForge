// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Native project files (.sfproj): an uncompressed ZIP holding a JSON manifest
// ("manifest.json") and little-endian binary blobs for every Part (mesh,
// optional attributes, sculpt layers) and any loaded Gaussian splats.
//
// The manifest names every blob with its element type and count, so readers
// ignore anything they don't know (forward compatible) and refuse files whose
// `version` is newer than kProjectVersion with a clear message.
#pragma once

#include "core/Commands.h"
#include "core/Document.h"
#include "core/Image.h"

#include <functional>
#include <string>
#include <vector>

namespace sf {

// 1: one mesh. 2: a list of Parts (version 1 files still load, as one Part). 3: Parts may carry a
// painted texture with layers ("texture"; older files still load). 4: Parts may carry subdivision levels
// ("subdiv"; older files still load). 5: sculpt-layer details of subdivision levels are in the levels' local
// frames, not world space (format 4 files are converted when they load). 6: levels made by Divide keep no
// triangles, child maps or (where their positions say it) details; older files still load. 7: a Part texture
// may carry material channels (the texture's "pbr" key, core/LayerStackIO.h), and a Part a micro-displacement
// level ("micro", core/Micro.h); both keys are optional in 7. 8: Linked Duplicates (#94): a Part may share an
// earlier Part's mesh ("link": that Part's index, no mesh of its own) and have a "transform" (16 numbers,
// column-major) placing its mesh in the scene.
//
// kProjectVersion is the newest format this build reads. Saving writes the lowest version the content needs
// (ProjectVersionNeed): a project that uses nothing newer than format 6 is still written as 6, so older
// SculptForge versions keep opening it. A feature that needs a newer format calls need(N) while saving.
constexpr int kProjectVersion = 8;
constexpr int kProjectBaseWriteVersion = 6;
struct ProjectVersionNeed {
    int version = kProjectBaseWriteVersion;
    void need(int v) { version = v > version ? v : version; }
};
constexpr const char* kProjectExtension = ".sfproj";

// True for paths ending in .sfproj (any case).
bool isProjectPath(const std::string& path);

// Writes `doc` (every Part with its name, visibility, mesh, attributes,
// layers, active layer and Bridge name; the active Part; splats, brush library
// and doc.projectSettings) to `path`. Does not change the document.
bool saveProject(const std::string& path, const Document& doc, std::string& error);
// Replaces the scene with the project at `path`. On failure `doc` is untouched.
// Restores the active Part, sets doc.path, gives every Part an empty
// edit history and marks the document clean.
bool loadProject(const std::string& path, Document& doc, std::string& error);
// --- Thumbnails: a small PNG preview ("thumbnail.png") stored right after the manifest, so the
// Library can show a project without loading it. Optional: older projects have none.

constexpr int kProjectThumbnailSize = 256;
// Called by saveProject on the saving thread to render the preview (the app renders the current
// view; the CLI sets none). Returning false or an empty image saves no thumbnail.
using ProjectThumbnailProvider = std::function<bool(const Document& doc, Image& out)>;
void setProjectThumbnailProvider(ProjectThumbnailProvider provider);
// Reads only the thumbnail of the project at `path` (seeks past the other entries). False if the
// file is not a project or has no thumbnail; `error` says which.
bool loadProjectThumbnail(const std::string& path, Image& out, std::string& error);

// loadProject wrapped as a command result (open_project, and `open` for .sfproj paths).
CommandResult openProjectCommand(Document& doc, const std::string& path);

// save_project / open_project (called by the CommandRegistry constructor).
void registerProjectCommands(CommandRegistry& registry);

// --- Recent files (pure list logic; the app persists the list) ---

constexpr size_t kMaxRecentFiles = 10;
// Moves `path` to the front (adding it if new), keeping at most `limit` entries.
std::vector<std::string> addRecentFile(std::vector<std::string> list, const std::string& path,
                                       size_t limit = kMaxRecentFiles);
// Drops entries for which `exists` is false, and empty or duplicate entries.
std::vector<std::string> pruneRecentFiles(std::vector<std::string> list,
                                          const std::function<bool(const std::string&)>& exists);

} // namespace sf
