// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Where the app keeps its data (settings, brushes, layouts, hotkeys, recent files, logs, caches): the one place that
// decides. A portable install carries its data with it: a file `portable.txt` (or a folder `data/`) next to the
// executable makes the data folder <executable folder>/data, created on first run. Without the marker it is the
// per-user folder, as it always was. appDataDir() (core/BrushLibrary.h) and userDataDir() (app/UserData.h) both ask
// here; nothing else reads APPDATA, HOME or XDG_* for user data.
#pragma once

#include <string>

namespace sf {

struct DataLocation {
    std::string dir;          // the folder in use, no trailing separator; "" when the platform names none
    bool portable = false;    // dir is <executable folder>/data
    bool readOnly = false;    // a portable marker was found but the folder can't be written: dir is the per-user folder
    std::string exeDir;       // where the marker was looked for (the executable's folder; an AppImage's or a .app's parent)
    std::string installedDir; // the per-user folder: only ever read from while portable (the import), never written
};

// The per-user data folder (what a non-portable run uses), no trailing separator, not created.
std::string perUserDataDir();

// The folder the portable marker is looked for in. SF_PORTABLE_DIR names another (tests); "" when unknown.
std::string executableDir();

// What the marker, the folder's permissions and the environment decide. Cached; follows SF_PORTABLE_DIR and
// SF_USER_DATA_DIR (a run isolated for tests or renders is never portable). The portable folder is created here.
const DataLocation& dataLocation();

// The next dataLocation() decides again (tests; the marker or the environment changed).
void resetDataLocation();

// The data folder the app writes to now (dataLocation().dir).
std::string activeDataDir();

// A folder for caches that are safe to lose: <data>/cache/<name> when portable, else `perUserCache` (the platform's
// cache folder, which the caller computes as it always did).
std::string portableCacheDir(const std::string& name, const std::string& perUserCache);

// The first-run import for a portable copy on a machine that has per-user data: is there something to offer, and the
// copy itself (never a move; never writes to the per-user folder). Logs, autosaves, caches and the update note stay.
bool importOffered();                 // the choice was made on an earlier run (either way)
bool importAvailable();               // portable, writable, per-user data exists, not yet offered
void markImportOffered();             // writes data/.import-offered
bool importInstalledData(std::string& summary, std::string& error);

// Said once in the status line when a portable marker was found but the stick is read-only.
std::string portableFallbackNote();

} // namespace sf
