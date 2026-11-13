// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The persistent brush library. Brushes live as one .sfbrush file each in:
//   - the user library, <app data>/brushes: brushes the artist made, refined or
//     installed (from a file, a pasted brush code or the community gallery);
//   - studio folders listed in SCULPTFORGE_BRUSH_PATH (':' separated, ';' on
//     Windows): shared, read-only - SculptForge never writes into them.
// The app, sculptforge-cli and the Python module load the library into
// Document::brushes at start-up. When several brushes share a name the user's
// wins over a studio one, which wins over a built-in (the first studio folder
// listed wins over later ones); each brush records its BrushOrigin.
//
// Brush codes share a brush as one line of text: "sfbrush1:" followed by the
// base64url (no padding) of the compact recipe JSON, at most 8 KB. Decoding
// validates and clamps like BrushRecipe::fromJson.
#pragma once

#include "core/BrushRecipe.h"

#include <string>
#include <vector>

namespace sf {

class CommandRegistry;
struct Document;

struct BrushLibraryPaths {
    std::string userDir;                 // empty = nothing is saved (tests, demo recordings)
    std::vector<std::string> studioDirs; // read-only, in precedence order
    // Folders of enabled packs (core/Pack.h sets them): read-only, below the studio folders. A pack brush or VDM
    // never replaces a built-in, studio or other pack's of the same name: it gets " (<pack>)" after its name.
    struct PackDir {
        std::string dir, pack; // the pack's brushes/ or vdms/ folder, and the pack's name
        bool vdm = false;
    };
    std::vector<PackDir> packs;
    // <app data>/brushes plus the folders in SCULPTFORGE_BRUSH_PATH.
    static BrushLibraryPaths fromEnvironment();
};

// The locations the brush commands use; starts as fromEnvironment(). The app
// points it elsewhere for UI tests and recordings, unit tests at temp folders.
BrushLibraryPaths& brushLibraryPaths();

// Per-user application data folder (where ai.json and brushes/ live), no trailing separator.
std::string appDataDir();
// Splits a search path (':' separated, ';' on Windows), dropping empty entries.
std::vector<std::string> splitSearchPath(const std::string& value);

constexpr size_t kMaxBrushCodeSize = 8 * 1024;
constexpr size_t kMaxBrushFileSize = 64 * 1024;

std::string encodeBrushCode(const BrushRecipe& recipe);
bool decodeBrushCode(const std::string& code, BrushRecipe& out, std::string& error);
// Text that holds a brush: a brush code or the contents of a .sfbrush file (as pasted from the clipboard).
bool parseBrushText(const std::string& text, BrushRecipe& out, std::string& error);
// Reads a .sfbrush file (size-limited, validated); `source` is set from its "source" key ("gallery").
bool readBrushFile(const std::string& path, BrushRecipe& out, std::string& error);

// File name (without folder) a brush is saved under: its name made safe on every OS.
std::string brushFileName(const std::string& name);

// Replaces doc.brushes with the built-ins, the studio folders and the user
// library (the active brush is kept by name). Files that can't be read are
// kept (never changed or deleted) and listed; a user brush file that can't be read comes from backups/ when that has it.
// Brushes that live only in this session or the open project are kept. Returns one message per file or folder.
std::vector<std::string> loadBrushLibrary(Document& doc, const BrushLibraryPaths& paths = brushLibraryPaths());

// Saves doc.brushes[index] into the user library (updating its file if it has
// one) and makes it a user brush; a built-in or studio brush becomes the
// artist's own version that overrides it. `gallery` marks a gallery install.
// Returns false with `error` when there is no user library or it can't be written.
bool saveBrushToLibrary(Document& doc, int index, std::string& error, bool gallery = false,
                        const BrushLibraryPaths& paths = brushLibraryPaths());

// A brush called `name` that isn't loaded (a shelf slot names it): looks for its file in the user library, then for its
// last good copy in <user library>/backups (kept by Save; a brush deleted on purpose is in backups/deleted and stays deleted), and puts it back, saved again when it
// came from the backup. Returns its index, or -1 when there is none; `message` says what was done.
int recoverBrush(Document& doc, const std::string& name, std::string& message,
                 const BrushLibraryPaths& paths = brushLibraryPaths());

// The brush a shelf slot points at, by its stable id first (a renamed brush, a file moved to another folder, a changed
// internal name), then by name (case-insensitive as a last resort: Windows and macOS files differ only in case), loaded or
// not: unloaded ones are looked for in the user library (subfolders too), the studio folders and the backups, as
// recoverBrush does. Returns its index or -1; the slot is never to be cleared for a -1. `message` says what was done.
int relinkBrush(Document& doc, const std::string& name, const std::string& id, std::string& message,
                const BrushLibraryPaths& paths = brushLibraryPaths());

// Index of the loaded brush with this stable id, or -1 (an empty id is never found).
int findBrushById(const Document& doc, const std::string& id);

// Adds (or updates, by name) `recipe`, saves it to the user library and makes
// it the active brush. Returns its index; on a save error the brush is still
// usable this session and `error` says why it isn't saved.
int installBrush(Document& doc, BrushRecipe recipe, std::string& error, bool gallery = false,
                 const BrushLibraryPaths& paths = brushLibraryPaths());

// Deletes a user or gallery brush and its file. A built-in or studio brush it
// overrode takes its place again. Other brushes can't be deleted.
bool deleteBrush(Document& doc, int index, std::string& error, const BrushLibraryPaths& paths = brushLibraryPaths());

// Renames a brush. A user brush's file is renamed with it; renaming a
// built-in or studio brush saves a renamed copy to the user library and keeps
// the original. Fails if another brush already has the name. Returns the
// renamed brush's index (or -1).
int renameBrush(Document& doc, int index, const std::string& newName, std::string& error,
                const BrushLibraryPaths& paths = brushLibraryPaths());

// True for brushes that live in the user library (user and gallery).
bool isUserLibraryBrush(const BrushRecipe& recipe);

// brush_install, brush_code, brush_delete, brush_rename, brush_library.
void registerBrushLibraryCommands(CommandRegistry& registry);

} // namespace sf
