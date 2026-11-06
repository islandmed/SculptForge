// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Brush sets (#143): named bottom shelves with their keys ("Wood", "Metal", "Stone"). A set is the bottom shelf's
// items in order plus the key of every brush on it; switching to a set swaps both in one step. Each set is one
// .sfset file (JSON) in the user library's sets/ folder, next to the user's brushes.
#pragma once

#include "core/BrushLibrary.h"
#include "core/Json.h"

#include <string>
#include <utility>
#include <vector>

namespace sf {

struct BrushSet {
    std::string name;
    std::vector<std::string> shelf; // the bottom shelf's item ids ("brush.clay", "brush.recipe.Rivet Row", ...)
    // The key of each brush on the shelf when it was saved, as the shortcut text ("1", "Shift+W / W"); "" = no key.
    std::vector<std::pair<std::string, std::string>> keys;
    std::string path; // the file it was read from or saved to
};

constexpr size_t kMaxBrushSetFileSize = 64 * 1024;
constexpr size_t kMaxBrushSetItems = 256;
constexpr size_t kMaxBrushSetName = 64;
constexpr const char* kBrushSetExtension = ".sfset";

// <user library>/sets ("" when there is no user library: renders and demo recordings save nothing).
std::string brushSetsDir(const BrushLibraryPaths& paths = brushLibraryPaths());

Json toJson(const BrushSet& set);
// Checks the name, the item count and lengths; false with `error` for anything else than a brush set.
bool brushSetFromJson(const Json& j, BrushSet& out, std::string& error);
// Every readable set in `dir`, A-Z by name (case-insensitive); unreadable files are skipped with a message.
std::vector<BrushSet> loadBrushSets(const std::string& dir, std::vector<std::string>* messages = nullptr);
// Writes `set` into `dir`, replacing the set of the same name (its file); sets `set.path`.
bool saveBrushSet(const std::string& dir, BrushSet& set, std::string& error);
bool deleteBrushSetFile(const BrushSet& set, std::string& error);

} // namespace sf
