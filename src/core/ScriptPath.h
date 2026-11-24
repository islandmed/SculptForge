// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The .py files the Script menu lists (docs/guide/python.md): the Scripts folder, then the folders of
// SCULPTFORGE_SCRIPT_PATH (':' separated, ';' on Windows; a studio profile's "scriptPaths" sets it), top level of each
// folder only: no sub-folders, so nothing is listed that the user didn't put in a folder they named. Listing is all a
// search-path folder gets: a script found there runs only when the user clicks it and can never run at start-up
// (app/Python.h keeps that to the Scripts folder).
#pragma once

#include <string>
#include <vector>

namespace sf {

struct ScriptFile {
    std::string path;
    bool fromSearchPath = false; // false: in the Scripts folder
};

// The Scripts folder's .py files, sorted, then each search-path folder's in the order given. Only files directly in a
// folder (top level only: no sub-folders), none hidden or named _helper.py. A file name already listed is skipped, so the Scripts
// folder wins and then the earlier folder; a folder repeated, or the Scripts folder itself, is read once. A folder that
// is missing or unreadable is skipped and added to `skipped` (when given), once.
std::vector<ScriptFile> listScripts(const std::string& scriptsDir, const std::vector<std::string>& searchPath,
                                    std::vector<std::string>* skipped = nullptr);

} // namespace sf
