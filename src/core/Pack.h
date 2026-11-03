// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Packs (.sfpack): brushes, alphas, Lit Spheres, Material Recipes, VDMs, keymaps, pie menus and macros shared as
// one file (docs/pack-format.md). A pack is data only: no scripts, no programs, nothing runs when it is installed
// or a project opens, and nothing in it reaches the network. Every pack is treated as hostile input:
//   - the zip is read by its own strict reader: absolute paths, '..', backslashes, drive letters, symbolic links,
//     devices, encrypted entries, ZIP64, multi-disk archives, overlapping entries, duplicate names (in any case)
//     and names Windows can't hold are refused; sizes, file counts and the decompression ratio are capped;
//   - only the file types a pack may hold are accepted, each in its own folder, each listed in pack.json, and each
//     is parsed (brushes, recipes, keymaps, pie menus and macros strictly; images decoded within size caps);
//   - installing writes only inside <app data>/packs/<id>/, never over built-ins or the user's own files.
// Installed packs are listed, enabled, disabled and removed here; their items join the brush, alpha, Lit Sphere,
// Material Recipe, keymap, pie menu and macro lists tagged with the pack's name. --no-packs (safe mode) leaves
// every pack out for the session.
#pragma once

#include "core/Json.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace sf {

class CommandRegistry;
struct BrushLibraryPaths;

constexpr const char* kPackExtension = ".sfpack";
constexpr int kPackFormatVersion = 1;

struct PackLimits {
    uint64_t archiveBytes = 128ull << 20; // the .sfpack file
    size_t entries = 1000;                // files (and folders) in it
    uint64_t fileBytes = 64ull << 20;     // one file, unpacked
    uint64_t totalBytes = 256ull << 20;   // every file, unpacked
    uint64_t ratio = 100;                 // unpacked / packed, per file over 64 KB and for the whole pack
    size_t nameBytes = 160;               // one file name, with its folder
};

enum class PackItemType { Brush, Alpha, Matcap, Material, Vdm, Keymap, PieMenu, Macro, Count };
constexpr int kPackItemTypes = int(PackItemType::Count);
const char* packItemTypeId(PackItemType t);              // "brush" (pack.json)
const char* packItemFolder(PackItemType t);              // "brushes" (the folder inside the pack)
const char* packItemLabel(PackItemType t, int count);    // "brush" / "brushes", "Lit Sphere" / "Lit Spheres", ...
bool packItemTypeFromId(const std::string& id, PackItemType& out);
bool packItemExtensionOk(PackItemType t, const std::string& fileName); // .sfbrush; .png/.jpg/.jpeg; ...

struct PackItem {
    PackItemType type = PackItemType::Brush;
    std::string path; // inside the pack: "brushes/Dragon Scales.sfbrush"
};

struct PackManifest {
    std::string id;            // folder name: lower-case letters, digits, '.', '_', '-'
    std::string name, version, author, license, description;
    std::string minAppVersion; // "" = any
    std::string preview;       // "preview.png" / "preview.jpg", or ""
    std::string channel;       // "insider" marks the monthly pack or an Insider drop (display only; no DRM), or ""
    std::vector<PackItem> contents;
};

bool parsePackManifest(const std::string& text, PackManifest& out, std::string& error);
std::string packManifestText(const PackManifest& m);
std::string appVersion(); // this build's version

// One file of a pack, unpacked.
struct PackFile {
    std::string name; // as in the archive
    std::vector<uint8_t> data;
};

// The checks every archive entry name must pass (see the file comment).
bool packEntryNameOk(const std::string& name, std::string& error, size_t maxBytes = PackLimits{}.nameBytes);

// Reads a zip with the strict checks above (stored and deflated entries, CRC checked). Folder entries are skipped.
bool readPackArchive(const std::string& path, std::vector<PackFile>& files, std::string& error, const PackLimits& limits = {});
bool readPackArchiveBytes(const std::vector<uint8_t>& bytes, std::vector<PackFile>& files, std::string& error,
                          const PackLimits& limits = {});

// What the install review shows: the manifest, how many items of each type, the license text, the size, and
// whether it replaces an installed version.
struct PackReview {
    PackManifest manifest;
    std::array<int, kPackItemTypes> counts{};
    std::string licenseText;
    uint64_t bytes = 0;            // unpacked
    std::string installedVersion;  // "" = not installed
    bool needsNewerApp = false;    // minAppVersion is newer than this build (refused)
    std::vector<std::string> itemNames; // "brush: Dragon Scales", for the review list
    std::vector<PackFile> files;
    std::string source; // the .sfpack it was read from
};
// Reads and checks a pack completely (archive, manifest, every file). Macros are checked against REGISTRY when given
// (the app's), otherwise against what a macro may hold at all.
bool reviewPack(const std::string& path, PackReview& out, std::string& error, const CommandRegistry* registry = nullptr,
                const PackLimits& limits = {});
bool reviewPackFiles(std::vector<PackFile> files, PackReview& out, std::string& error,
                     const CommandRegistry* registry = nullptr);
// "12 brushes, 4 alphas, 1 macro"
std::string packCountsText(const std::array<int, kPackItemTypes>& counts);

// --- Installed packs (<app data>/packs/<id>/, state in <app data>/packs/packs.json) ---

struct InstalledPack {
    PackManifest manifest;
    std::string dir;
    bool enabled = true;
};

std::string packsRoot();                      // <app data>/packs (or the override)
void setPacksRoot(const std::string& dir);    // UI tests, unit tests; "" = the default
bool packsSafeMode();                         // --no-packs: no pack is loaded this session
void setPacksSafeMode(bool on);

// Every installed pack (by name); folders that don't hold a readable pack are skipped with a message.
std::vector<InstalledPack> listInstalledPacks(std::vector<std::string>* messages = nullptr);
// Writes the reviewed pack into its folder (replacing an installed version of the same id) and enables it.
bool installPack(const PackReview& review, std::string& error);
bool removePack(const std::string& id, std::string& error);
bool setPackEnabled(const std::string& id, bool enabled, std::string& error);

// One item of an enabled pack.
struct PackContentItem {
    PackItemType type = PackItemType::Brush;
    std::string file;     // absolute path of the installed file
    std::string name;     // its file name without the extension
    std::string packId, packName;
    std::string label() const { return name + " (" + packName + ")"; } // as the lists show it
};
// The items of every enabled pack (none in safe mode), by pack name then file name.
std::vector<PackContentItem> enabledPackItems(PackItemType type);
// Points the brush library at the enabled packs' brushes and VDMs (core/BrushLibrary.h).
void applyPacksToBrushLibrary(BrushLibraryPaths& paths);

// --- Keymap files (keymaps/*.json in a pack) ---
// {"format": "sculptforge-keymap", "version": 1, "name": "...", "shortcuts": {"brush.clay": "C", ...}}
struct KeymapFile {
    std::string name;
    std::vector<std::pair<std::string, std::string>> shortcuts; // action id, chord text
};
constexpr size_t kMaxKeymapShortcuts = 512;
bool parseKeymapFile(const std::string& text, KeymapFile& out, std::string& error);
std::string keymapFileText(const KeymapFile& k);

// --- Making a pack ---

struct PackSource {
    PackItemType type = PackItemType::Brush;
    std::string file;        // on disk
    std::string name;        // file name inside the pack ("" = the file's own)
    std::vector<uint8_t> data; // instead of `file` (a recipe or keymap made in memory)
};
// Writes OUTPATH (a stored zip) from the items, checking the result exactly as an install would.
bool writePack(const std::string& outPath, PackManifest manifest, const std::vector<PackSource>& items,
               const std::string& licenseText, const std::string& previewFile, std::string& error,
               const CommandRegistry* registry = nullptr);

// pack_install, pack_list, pack_remove, pack_enable, pack_disable, pack_create.
void registerPackCommands(CommandRegistry& registry);

} // namespace sf
